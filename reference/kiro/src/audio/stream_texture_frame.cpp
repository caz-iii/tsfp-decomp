// ============================================================================
// Address range: 0x801861f8 - 0x80186ac4 | batch_479 | Date: 2026-09-06
// Subsystem: audio (streamed-texture per-frame driver + visibility marking).
//            tag audio-stream-tex.
//
// The top of the texture-streaming stack: the per-frame update driver
// (stream_tex_frame_update, 0x80186638) that pumps the batch_477 read state
// machine, lazily allocates the streaming page pool from spare heap, commits any
// finished voice DMAs (batch_478), promotes/demotes voices between the resident
// working set and the page ring, and adjusts each resident texture's LOD by
// distance.  Plus the "mark this texture used this frame" helpers that the
// renderer calls to drive the working set, and a handful of small accessors.
//
//   stream_voice_unlock_once     0x801861f8  clear the locked flag, unlock pages
//   stream_tex_mark_slot_used    0x80186288  mark a slot's tex-id "used" byte(s)
//   stream_tex_mark_instance_used 0x8018635c mark main + optional aux + shadow tex
//   stream_tex_mark_prop_visible 0x801863ec  distance-gated mark-used from a render prop
//   stream_tex_scratch_fits      0x8018660c  does `size` fit the remaining scratch?
//   stream_tex_frame_update      0x80186638  THE per-frame streaming driver
//   stream_tex_nop_b             0x80186a94  empty hook
//   stream_tex_get_pool_size     0x80186a98  return the streaming pool byte size
//   stream_tex_is_pool_ready     0x80186aa0  pool allocated + streaming disabled?
//   stream_tex_set_device_id     0x80186ac4  set the stream device id
//
// Structurally-faithful non-matching port.  The spawn-frame register pair
// (FUN_803826f0) high word = slot / voice index.  mem_alloc_aligned is the
// 3-arg (align,size,type) allocator (batch_476); the decompiler's long FP-arg
// spill at the call site is an ABI artifact and is dropped, calling it through
// the typed alias.  The int->double magic-bias dequant for the LOD ramp is
// preserved.  __save/__restore_gpr_25 spill stubs are dropped.

#include "common.h"
#include "generated_decls.h"

// ---- spawn-frame register-pair helper ---------------------------------------
typedef unsigned long long (*FramePairFn)(void);
#define FRAME_PAIR_F ((FramePairFn)FUN_803826f0)
extern void FUN_803826f0(void);
extern void FUN_8038273c(void);   // spawn-frame epilogue

// mem_alloc_aligned is 3-arg (alignment, size, type) and returns the address;
// generated_decls declares it void, so call via a typed alias (matches batch_476).
typedef int (*MemAllocAlignedFn)(int alignment, int size, int type);
#define MEM_ALLOC_ALIGNED ((MemAllocAlignedFn)mem_alloc_aligned)

// ---- GX descriptor / bind helpers (0x803c8xxx / 0x803c9xxx) ------------------
extern int    FUN_803c9040(int desc);   // source data pointer (0 if none)
extern int    FUN_803c9038(int desc);   // resident texture-id (or -1)
extern double FUN_803c90a4(int desc);   // per-texture detail / priority scalar
extern void   FUN_803c8f7c(int gxObj, int dataAddr);       // bind tex data ptr
extern void   FUN_803c8fe0(double lodBias, int gxObj);     // set tex LOD / commit
extern void   FUN_803994d0(int addr, int len);             // DCStoreRange (partial)

// ---- entity / prop helpers ---------------------------------------------------
extern int  render_prop_get_value(int *prop);             // resolve prop -> value index
extern void entity_get_center_position(float *out, int entity);

// occ helpers, audio_voice_is_active, stream_* (batch_476..478) resolve through
// generated_decls.

// ---- voice slot tables (element base + ROM stride) --------------------------
extern unsigned char  DAT_80599300;   // voice GX-object id      (stride 6 words)
extern int            DAT_8059930c;   // voice page cursor/addr  (stride 6 words)
extern unsigned short DAT_80599314;   // voice flags             (stride 0xc)

// ---- "used this frame" + "resident last frame" byte tables (stride 1) --------
extern char DAT_805a0844;   // used-this-frame flags (indexed by resident tex-id)
extern char DAT_805a0b00;   // resident-last-frame flags (indexed by voice)

// ---- streaming pool / bookkeeping globals -----------------------------------
extern int           *DAT_80610a20;   // ptr to streamed-slot descriptor array (0x58 stride)
extern int            DAT_80610ab0;   // heap reserve floor
extern int            DAT_80610ab8;   // streaming-enabled flag
extern int            DAT_80610abc;   // stream device id
extern int            DAT_80612420;   // scratch region base
extern int            DAT_80612424;   // page-ring base
extern int            DAT_80612428;   // scratch write cursor
extern int            DAT_8061242c;   // scratch region size
extern int            DAT_80612430;   // streaming pool byte size
extern unsigned char  DAT_80612434;   // pool-allocated latch
extern short          DAT_8061243c;   // free page count
extern int            DAT_80612448;   // active voice count
extern int            DAT_8061244c;   // total page count
extern int            DAT_80612454;   // any-evicting accumulator
extern int            DAT_8061245c;   // stat: failed-resident count
extern int            DAT_80612460;   // stat: used-slot count
extern int            DAT_80612464;   // stat: locked count
extern int            DAT_80612468;   // stat: unlocked-kept count
extern int            DAT_8061246c;   // stat: newly-allocated count
extern int            DAT_80612614;   // current level/mode id
extern float          DAT_80583498;   // LOD ramp numerator
extern float          DAT_80583228;   // LOD ramp denominator table (0x800 entries)
extern int            DAT_80611aa8;   // camera/light record base
extern int            DAT_80611d54;   // active viewport/camera record
extern unsigned int   DAT_806108e4;   // resident-byte accumulator (dequant source)

// ---- rodata ------------------------------------------------------------------
extern float  FLOAT_80616760;  // default LOD bias
extern float  FLOAT_80616764;  // detail threshold
extern float  FLOAT_80616768;  // radius bias (type-2 gate)
extern float  FLOAT_8061676c;  // distance^2 gate (default)
extern float  FLOAT_80616770;  // LOD ramp k0
extern float  FLOAT_80616774;  // LOD ramp k1
extern float  FLOAT_80616778;  // LOD ramp k2
extern float  FLOAT_8061677c;  // LOD ramp divisor
extern float  FLOAT_80616780;  // LOD dequant scale
extern float  FLOAT_80610ab4;  // computed per-frame distance scale
extern double DOUBLE_80616788; // magic-bias 2^52 offset

// -----------------------------------------------------------------------------
// stream_voice_unlock_once (0x801861f8)
// If the voice is loaded and currently locked (flag 4 set), unlock its pages and
// clear the flag; return 1.  Returns 0 otherwise.  Inverse of
// stream_voice_lock_once (batch_478).
// -----------------------------------------------------------------------------
int stream_voice_unlock_once(int v)
{
    unsigned short *pf = &DAT_80599314 + v * 0xc;
    unsigned short f = *pf;

    bool loaded = ((f & 1) != 0) && ((f & 2) != 0) && ((f & 0x10) == 0);
    if (!loaded) {
        return 0;
    }
    if ((f & 4) == 0) {
        return 0;
    }
    stream_voice_pages_unlock(v);
    *pf = *pf & 0xfffb;
    return 1;
}

// -----------------------------------------------------------------------------
// stream_tex_mark_slot_used (0x80186288)
// For the slot descriptor in the spawn-frame's high word, mark its resident
// texture id(s) "used this frame": the main texture (if it has source data) and,
// for dual-texture slots (flag byte 0x4a bits in mask 0x610), the aux texture.
// Increments the used-slot stat for newly-used ids.
// -----------------------------------------------------------------------------
void stream_tex_mark_slot_used(void)
{
    int slot = (int)FRAME_PAIR_F();
    int rec = *DAT_80610a20 + slot * 0x58;
    if (DAT_80610ab8 != 0) {
        for (int pass = 0; pass < 2; pass++) {
            int texId = -1;
            if (pass == 0) {
                if (FUN_803c9040(rec) != 0) {
                    texId = FUN_803c9038(rec);
                }
            }
            else if ((1 << (unsigned int)*(unsigned char *)(rec + 0x4a) & 0x610U) != 0) {
                texId = FUN_803c9038(rec + 0x20);
            }
            if (texId != -1) {
                if ((&DAT_805a0844)[texId] == '\0') {
                    DAT_80612460 = DAT_80612460 + 1;
                }
                (&DAT_805a0844)[texId] = 1;
            }
        }
    }
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// stream_tex_mark_instance_used (0x8018635c)
// Mark a render instance's textures used: always the main slot; the linked slot
// at +0xc if valid; and the shadow/aux slot if the flag field at +0x1c selects
// it (mask 0x180 == 0x80).
// -----------------------------------------------------------------------------
void stream_tex_mark_instance_used(unsigned int a1, unsigned int a2, int inst)
{
    FRAME_PAIR_F();
    stream_tex_mark_slot_used();
    if (*(int *)(inst + 0xc) != -1) {
        stream_tex_mark_slot_used();
    }
    if ((*(unsigned int *)(inst + 0x1c) & 0x180) == 0x80) {
        stream_tex_mark_slot_used();
    }
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// stream_tex_mark_prop_visible (0x801863ec)
// Called per submitted render prop (caller render_instance_submit_pass): resolve
// the prop's texture-slot value, and if it maps to a resident slot whose texture
// is not yet marked used this frame, mark it used — but only if the object is
// close enough.  Three distance regimes keyed by the frame's mode word at +0x20:
//   0x10 -> always mark (mode 0)
//   2    -> camera/light record distance vs radius^2 * scale (mode 1)
//   else -> entity center distance vs FLOAT_8061676c * scale (mode 2)
// The frame's high word carries a context record pointer.
// -----------------------------------------------------------------------------
void stream_tex_mark_prop_visible(unsigned int a1, unsigned int a2, int propBase, short *propIndex)
{
    unsigned long long frame = FRAME_PAIR_F();
    int ctx = (int)(frame >> 0x20);

    if (*propIndex < 0) {
        return;
    }
    int *prop = (int *)(propBase + *propIndex * 0x30);
    int val = render_prop_get_value(prop);
    if (-1 < val) {
        val = *(int *)((int)frame + val * 0x10);
    }
    if ((val < 0) || (0x401 <= val)) {
        return;
    }

    int mainId = -1;
    int auxId = -1;
    int rec = *DAT_80610a20 + val * 0x58;
    if (FUN_803c9040(rec) != 0) {
        mainId = FUN_803c9038(rec);
    }
    if ((1 << (unsigned int)*(unsigned char *)(rec + 0x4a) & 0x610U) != 0) {
        auxId = FUN_803c9038(rec + 0x20);
    }

    bool needMark = ((mainId != -1) && ((&DAT_805a0844)[mainId] == '\0')) ||
                    ((auxId != -1) && ((&DAT_805a0844)[auxId] == '\0'));
    if (!needMark) {
        return;
    }

    int mode = *(int *)(ctx + 0x20);
    if (mode == 0x10) {
        stream_tex_mark_instance_used(val, 0, (int)prop);
    }
    else if (mode == 2) {
        int cam = DAT_80611aa8 + *(int *)(ctx + 0x34) * 0xb4;
        float dx = *(float *)(DAT_80611d54 + 0x27c) - *(float *)(cam + 0x94);
        float dy = *(float *)(DAT_80611d54 + 0x280) - *(float *)(cam + 0x98);
        float dz = *(float *)(DAT_80611d54 + 0x284) - *(float *)(cam + 0x9c);
        float r  = FLOAT_80616768 + *(float *)(cam + 0xa0);
        if (dz * dz + dx * dx + dy * dy < FLOAT_80610ab4 * r * r) {
            stream_tex_mark_instance_used(val, 1, (int)prop);
        }
    }
    else {
        float c[3];
        entity_get_center_position(c, ctx);
        c[0] = *(float *)(DAT_80611d54 + 0x27c) - c[0];
        c[1] = *(float *)(DAT_80611d54 + 0x280) - c[1];
        c[2] = *(float *)(DAT_80611d54 + 0x284) - c[2];
        if (c[2] * c[2] + c[0] * c[0] + c[1] * c[1] < FLOAT_8061676c * FLOAT_80610ab4) {
            stream_tex_mark_instance_used(val, 2, (int)prop);
        }
    }
}

// -----------------------------------------------------------------------------
// stream_tex_scratch_fits (0x8018660c)
// Predicate: would `size` bytes fit within the remaining streaming scratch
// region (write cursor offset from base + size <= region size)?
// -----------------------------------------------------------------------------
bool stream_tex_scratch_fits(int size)
{
    return (unsigned int)(size + (DAT_80612428 - DAT_80612420)) <= (unsigned int)DAT_8061242c;
}

// -----------------------------------------------------------------------------
// stream_tex_frame_update (0x80186638)
// The per-frame texture-streaming driver (caller render_target_reset_and_advance).
// While streaming is enabled:
//   1. Pump the read/upload state machine.
//   2. Recompute the per-frame distance scale FLOAT_80610ab4 from the LOD ramp
//      table and the camera near-plane (int->double magic-bias dequant).
//   3. Lazily allocate the streaming page pool from spare heap once per level
//      (mem_alloc_aligned 0x20-aligned, DCStore, derive the page count).
//   4. For voices marked evicting (0x10), rebind their GX object to the resident
//      copy and clear the evicting bit.
//   5. Commit finished voice DMAs (stream_voice_dma_commit + dsp_dma_kick), and
//      lock the pages of newly-resident-but-unused textures.
//   6. Reconcile used vs resident: keep, unlock, (re)allocate pages, or count as
//      a miss; then ramp each resident texture's LOD toward its target by the
//      per-texture priority; clear the used byte for next frame.
// -----------------------------------------------------------------------------
void stream_tex_frame_update(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                             unsigned long long f4, unsigned long long f5, unsigned long long f6,
                             unsigned long long f7, unsigned long long f8)
{
    if (DAT_80610ab8 == 0) {
        return;
    }

    stream_tex_pump_state_machine(f1, f2, f3, f4, f5, f6, f7, f8);

    // per-frame distance scale from the LOD ramp table (indexed by near-plane).
    double k0 = (double)FLOAT_80616770;
    double k2 = (double)FLOAT_80616778;
    double k1 = (double)FLOAT_80616774;
    double num = (double)DAT_80583498;
    int idx = (int)(k1 * (double)((float)(k2 * (double)(float)((double)*(float *)(DAT_80611d54 + 900) * k0))
                                  / FLOAT_8061677c) + k0) & 0x7ff;
    double scale = (double)(float)(num / (double)(&DAT_80583228)[idx]);
    FLOAT_80610ab4 = (float)(scale * scale);

    // lazy one-shot pool allocation from spare heap.
    if ((DAT_80612434 == '\0') && (DAT_80612614 != 0x18) && (DAT_80612614 != 0x2c)) {
        DAT_80612430 = (unsigned int)(DAT_80612428 - DAT_80612420) >> 1;
        if (0x200000 < DAT_80612430) {
            DAT_80612430 = 0x200000;
        }
        int freeHeap = get_free_heap_size();
        int avail = freeHeap - DAT_80610ab0;
        if ((DAT_80612614 == 1) || (DAT_80612614 == 0x66)) {
            avail = avail - 0x25800;
        }
        if (avail < DAT_80612430) {
            DAT_80612430 = avail;
        }
        if (DAT_80612430 < 1) {
            DAT_80612430 = 0;
            DAT_80612424 = 0;
            DAT_8061243c = 0;
            DAT_8061244c = 0;
        }
        else {
            DAT_80612424 = MEM_ALLOC_ALIGNED(0x20, DAT_80612430, 1);
            FUN_803994d0(DAT_80612424, DAT_80612430);
            unsigned int pages = ((int)DAT_80612430 >> 0xb) +
                                 (unsigned int)((int)DAT_80612430 < 0 && (DAT_80612430 & 0x7ff) != 0);
            DAT_8061244c = pages & 0xffff;
            DAT_8061243c = (short)pages;
        }
        DAT_80612434 = '\x01';
        get_display_dimensions();
    }

    DAT_8061245c = 0;
    DAT_80612460 = 0;

    // release GX objects of voices that were evicting last frame.
    if (DAT_80612454 != 0) {
        int off = 0;
        for (int v = 0; v < DAT_80612448; v++) {
            unsigned short f = *(unsigned short *)((int)&DAT_80599314 + off);
            if ((f & 0x10) != 0) {
                if (((f & 1) != 0) && ((f & 2) != 0)) {
                    FUN_803c8f7c(*(int *)((int)&DAT_80599300 + off),
                                 *(int *)((int)&DAT_8059930c + off));
                    FUN_803c8fe0((double)FLOAT_80616760, *(int *)((int)&DAT_80599300 + off));
                }
                *(unsigned short *)((int)&DAT_80599314 + off) =
                    *(unsigned short *)((int)&DAT_80599314 + off) & 0xffef;
            }
            off += 0x18;
        }
    }

    // commit finished DMAs + lock newly-resident-but-unused textures.
    DAT_80612454 = 0;
    {
        char *resident = &DAT_805a0b00;
        int off = 0;
        for (int v = 0; v < DAT_80612448; v++) {
            if ((*(unsigned short *)((int)&DAT_80599314 + off) & 0x40) != 0) {
                stream_voice_dma_commit();
                stream_tex_dsp_dma_kick();
            }
            if ((*resident != '\0') && ((&DAT_805a0844)[v] == '\0') &&
                (stream_voice_lock_once(v) != 0)) {
                DAT_80612464 = DAT_80612464 + 1;
            }
            resident++;
            off += 0x18;
        }
    }

    // reconcile used-this-frame vs resident-last-frame, then ramp LOD.
    {
        char *used = &DAT_805a0844;
        int off = 0;
        for (int v = 0; v < DAT_80612448; v++) {
            if ((*used == '\0') || ((&DAT_805a0b00)[v] != '\0')) {
                (&DAT_805a0b00)[v] = *used;
            }
            else if (((*(unsigned short *)((int)&DAT_80599314 + off) & 4) == 0) ||
                     (stream_voice_unlock_once(v) == 0)) {
                if (stream_voice_page_alloc(v) == 0) {
                    unsigned short f = *(unsigned short *)((int)&DAT_80599314 + off);
                    (&DAT_805a0b00)[v] = 0;
                    bool loaded = ((f & 1) != 0) && ((f & 2) != 0) && ((f & 0x10) == 0);
                    if (!loaded) {
                        DAT_8061245c = DAT_8061245c + 1;
                    }
                }
                else {
                    DAT_8061246c = DAT_8061246c + 1;
                    (&DAT_805a0b00)[v] = *used;
                }
            }
            else {
                DAT_80612468 = DAT_80612468 + 1;
                (&DAT_805a0b00)[v] = *used;
            }

            unsigned short f = *(unsigned short *)((int)&DAT_80599314 + off);
            unsigned short *pf = (unsigned short *)((int)&DAT_80599314 + off);
            bool loaded = ((f & 1) != 0) && ((f & 2) != 0) && ((f & 0x10) == 0);
            if (loaded && ((f & 8) != 0)) {
                double detail = FUN_803c90a4(*(int *)((int)&DAT_80599300 + off));
                double thresh = (double)FLOAT_80616764;
                if (detail <= thresh) {
                    *pf = *pf & 0xfff7;
                }
                else {
                    double bytes = (double)(CONCAT44(0x43300000, DAT_806108e4) ^ 0x80000000);
                    double lod = -(double)(float)((double)FLOAT_80616780 *
                                                  (double)(float)(bytes - DOUBLE_80616788) - detail);
                    if (thresh < lod) {
                        thresh = lod;
                    }
                    FUN_803c8fe0(thresh, *(int *)((int)&DAT_80599300 + off));
                }
            }
            f = *pf;
            *used = '\0';
            DAT_80612454 = DAT_80612454 | (f & 0x10);
            used++;
            off += 0x18;
        }
    }
}

// -----------------------------------------------------------------------------
// stream_tex_nop_b (0x80186a94)   empty hook.
// -----------------------------------------------------------------------------
void stream_tex_nop_b(void)
{
}

// -----------------------------------------------------------------------------
// stream_tex_get_pool_size (0x80186a98)  return the streaming pool byte size.
// -----------------------------------------------------------------------------
int stream_tex_get_pool_size(void)
{
    return DAT_80612430;
}

// -----------------------------------------------------------------------------
// stream_tex_is_pool_ready (0x80186aa0)
// True (1) only if streaming is disabled AND the page-ring pool is allocated.
// Caller render_target_advance_scratch gates scratch reuse on this.
// -----------------------------------------------------------------------------
int stream_tex_is_pool_ready(void)
{
    if (DAT_80610ab8 != 0) {
        return 0;
    }
    if (DAT_80612424 == 0) {
        return 0;
    }
    return 1;
}

// -----------------------------------------------------------------------------
// stream_tex_set_device_id (0x80186ac4)  set the stream device id.
// -----------------------------------------------------------------------------
void stream_tex_set_device_id(unsigned char devId)
{
    DAT_80610abc = devId;
}
