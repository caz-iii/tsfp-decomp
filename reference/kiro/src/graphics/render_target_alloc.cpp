// Address range: 0x8017a62c - 0x8017b0c0 | batch_459 (part 2/2) | Date: 2026-09-06
// Subsystem: graphics (render-target / off-screen texture slot management)
//
// The render-target subsystem hands out entries from a fixed pool (base pointer
// DAT_80610a20[0], count [3], free cursor [2]) each 0x58 bytes.  A slot holds a GX
// EFB->texture configuration (dimensions at +0x4c/+0x4e/+0x50/+0x52, flags at +0x40,
// pow2 markers at +0x48/+0x49).  render_target_alloc_slot pops the free cursor or
// scavenges a slot flagged 0x800000; render_target_setup programs the GX copy /
// texture-object registers (FUN_803c8*).  gx_draw_quad_points is the shared 4-corner
// textured-quad emitter used across HUD, particles and projectiles.
//
//   gx_draw_quad_points          0x8017a62c  emit one textured quad from 4 corners
//   render_target_alloc_slot     0x8017afc4  allocate / scavenge a RT slot
//   render_target_setup          0x8017acf8  program GX EFB-copy + texture object
//   render_target_bind_sized     0x8017ae9c  setup using the slot's stored dims
//   render_target_init_slot      0x8017aee8  init a slot (dims + pow2 flags)
//   render_target_get_half_dim   0x8017b034  slot half-dimension ([+0x18] >> 1)
//   render_target_sweep_flagged  0x8017b040  cleanup slots flagged 0x8000000
//   render_target_restore_active 0x8017b0c0  restore the saved active RT id

#include "common.h"
#include "generated_decls.h"
#include "graphics.h"

extern void FUN_8017c658(void);   // enter 2D draw state
extern void FUN_8017fab4(void);   // set immediate vertex format
extern void FUN_8017c670(void);   // finish 2D draw state
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);

// ---- GX FIFO write-gather port (0xCC008000) --------------------------------
extern volatile float        DAT_cc008000;
extern volatile unsigned int DAT_cc008000_u;
#define GXF(v) (DAT_cc008000   = (float)(v))
#define GXU(v) (DAT_cc008000_u = (unsigned int)(v))

// ---- render-target pool + render-state globals -----------------------------
extern int* DAT_80610a20;      // [0]=slot table base, [2]=free cursor, [3]=slot count
extern int  DAT_80611570, DAT_80611574, DAT_80611578;  // draw-stat counters
extern int  DAT_80611d54;      // render context
extern unsigned char DAT_80612320;   // colour-update suppression flag
extern unsigned char DAT_80612322;    // untextured-quad flag
extern int  DAT_806122dc;      // active render-target id
extern int  DAT_806122f4;      // saved render-target id
extern float FLOAT_806165a4, FLOAT_806165a8;

// ---- spawn-frame accessors -------------------------------------------------
extern unsigned long long FUN_803826e8(void);   // frame push (quad points / scratch dims)
extern void FUN_80382734(void);   // frame pop (of 803826e8)
extern unsigned long long FUN_803826c8(void);  // frame ptr (hi=slot index)
extern void FUN_80382714(void);   // frame pop (of 803826c8)
extern unsigned long long FUN_803826f0(void);
extern void FUN_8038273c(void);

// ---- low-level GX texture / EFB-copy services ------------------------------
extern unsigned int FUN_803c8960(unsigned int w, unsigned int h, unsigned int fmt, char a, int b);
extern unsigned int FUN_803c9040(int texObj);
extern unsigned int FUN_8017b0d8(void);
extern void FUN_803c8b84(unsigned int* texObj, unsigned int data, unsigned int w, unsigned int h,
                         unsigned int fmt, unsigned int wl2, unsigned int hl2, char a);
extern void FUN_803c8fa8(unsigned int* texObj, int a, int b);
extern void FUN_803c9030(int texObj, unsigned int v);
extern void FUN_803c78c8(unsigned int a, unsigned int b, unsigned int c, unsigned int d);
extern void FUN_803c7978(unsigned short a, unsigned short b, unsigned int c, unsigned char d);
extern void FUN_803c8224(unsigned int a, unsigned int b);
extern void FUN_803c7080(void);
void render_target_clear_slot(int slot);  // 0x8017c454 (defined below)

// ---- render-target sweep table (per-group base + count, stride 0x10) --------
extern unsigned char DAT_8058fb60;   // per-group slot table base
extern unsigned char DAT_8058fb6c;   // per-group slot count

// gx_draw_quad_points (0x8017a62c)
// Emit a single textured (or untextured, when DAT_80612322 set) quad directly into
// the GX FIFO from four explicit corner points (param_4=TL, param_5=BR, param_6=BL,
// param_7=TR) with a shared byte-swapped colour.  Bumps the per-frame draw-stat
// counters.  Frame push/pop via FUN_803826e8 / FUN_80382734.
void gx_draw_quad_points(unsigned int param_1, unsigned int param_2, unsigned int color,
                         float* pTL, float* pBR, float* pBL, float* pTR)
{
    FUN_803826e8();
    FUN_8017c658();
    if (DAT_80612322 == 0) {
        unsigned int c = (color << 0x18) | ((color & 0xff00) << 8) | (color >> 8 & 0xff00) | (color >> 0x18);
        FUN_8017fab4();
        gx_begin_primitive(0x98, 0, 4);
        GXF(pTL[0]); GXF(pTL[1]); GXF(pTL[2]); GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a4);
        GXF(pBL[0]); GXF(pBL[1]); GXF(pBL[2]); GXU(c); GXF(FLOAT_806165a4); GXF(FLOAT_806165a8);
        GXF(pBR[0]); GXF(pBR[1]); GXF(pBR[2]); GXU(c); GXF(FLOAT_806165a8); GXF(FLOAT_806165a4);
        GXF(pTR[0]); GXF(pTR[1]); GXF(pTR[2]); GXU(c); GXF(FLOAT_806165a8); GXF(FLOAT_806165a8);
    } else {
        FUN_8017fab4();
        gx_begin_primitive(0x98, 0, 4);
        GXF(pTL[0]); GXF(pTL[1]); GXF(pTL[2]); GXF(FLOAT_806165a4); GXF(FLOAT_806165a4);
        GXF(pBL[0]); GXF(pBL[1]); GXF(pBL[2]); GXF(FLOAT_806165a4); GXF(FLOAT_806165a8);
        GXF(pBR[0]); GXF(pBR[1]); GXF(pBR[2]); GXF(FLOAT_806165a8); GXF(FLOAT_806165a4);
        GXF(pTR[0]); GXF(pTR[1]); GXF(pTR[2]); GXF(FLOAT_806165a8); GXF(FLOAT_806165a8);
    }
    FUN_8017c670();
    DAT_80611570 = DAT_80611570 + 4;
    DAT_80611574 = DAT_80611574 + 2;
    DAT_80611578 = DAT_80611578 + 2;
    FUN_80382734();
}

// render_target_alloc_slot (0x8017afc4)
// Return the next free render-target slot.  Prefer the free cursor (DAT_80610a20[2],
// counting down); otherwise scavenge the first slot flagged 0x800000 (clearing the
// flag).  Returns the slot index, or -1 if none available.
int render_target_alloc_slot(void)
{
    int cursor = DAT_80610a20[2];
    if (-1 < cursor) {
        DAT_80610a20[2] = cursor - 1;
        return cursor;
    }
    int count = DAT_80610a20[3];
    int index = 0;
    int off = 0;
    if (0 < count) {
        do {
            if ((*(unsigned int*)(*DAT_80610a20 + off + 0x40) & 0x800000) != 0) {
                int s = *DAT_80610a20 + off;
                *(unsigned int*)(s + 0x40) = *(unsigned int*)(s + 0x40) & 0xff7fffff;
                return index;
            }
            index = index + 1;
            off = off + 0x58;
            count = count - 1;
        } while (count != 0);
    }
    return -1;
}

// render_target_setup (0x8017acf8)
// Program a render-target's GX EFB-copy + texture object.  Reads the target slot
// from the spawn frame, configures the copy region (FUN_803c8960), initialises the
// texture object (FUN_803c8b84/fa8/9030) either from a fresh EFB copy or the shared
// scratch (FUN_8017b0d8), then applies scissor/viewport (FUN_803c78c8/7978) and the
// colour-update toggle around the copy.
void render_target_setup(unsigned int param_1, unsigned int param_2, unsigned int param_3,
                         unsigned int param_4, unsigned int param_5, unsigned int param_6,
                         unsigned int param_7, unsigned int param_8, int param_9, int param_10)
{
    unsigned long long frame = FUN_803826c8();
    unsigned int w = (int)param_6 >> (param_8 & 0x3f);
    unsigned int h = (int)param_7 >> (param_8 & 0x3f);
    unsigned int* slot = (unsigned int*)(*DAT_80610a20 + (int)(frame >> 0x20) * 0x58);
    FUN_803c8960(w & 0xffff, h & 0xffff, param_5, '\0', 0);

    bool useCopy = ((slot[0x10] & 0x10000) != 0) || (param_10 == 0);
    unsigned int data = useCopy ? FUN_803c9040((int)slot) : FUN_8017b0d8();

    unsigned int* texObj = slot;
    if (param_9 != 0) {
        *(unsigned char*)((int)slot + 0x4a) = 9;
        texObj = slot + 8;
    }
    unsigned int wl2 = countLeadingZeros((unsigned int)*(unsigned char*)(slot + 0x12));
    *(short*)((int)slot + 0x4e) = (short)(w << param_8);
    unsigned int hl2 = countLeadingZeros((unsigned int)*(unsigned char*)((int)slot + 0x49));
    *(short*)((int)slot + 0x52) = (short)(h << param_8);
    FUN_803c8b84(texObj, data, w & 0xffff, h & 0xffff, param_5, wl2 >> 5, hl2 >> 5, '\0');
    FUN_803c8fa8(texObj, 1, 1);
    FUN_803c9030((int)texObj, 0xffffffff);
    FUN_803c78c8((unsigned int)frame & 0xffff, param_3 & 0xffff, param_6 & 0xffff, param_7 & 0xffff);
    FUN_803c7978((unsigned short)w, (unsigned short)h, param_4, (unsigned char)(param_8 & 0xff));
    if (DAT_80612320 != 0) {
        gx_get_color_update_packed();
        gx_set_color_update_packed(0);
    }
    unsigned int copy = FUN_803c9040((int)texObj);
    FUN_803c8224(copy, (unsigned int)DAT_80612320);
    if (DAT_80612320 != 0) {
        gx_set_color_update_packed(0);
    }
    FUN_803c7080();
    FUN_80382714();
}

// render_target_bind_sized (0x8017ae9c)
// Thin wrapper: bind/setup a render-target using the dimensions already stored in
// the slot (+0x4c width, +0x50 height), delegating to render_target_setup.
void render_target_bind_sized(int slot, unsigned int param_2, unsigned int param_3,
                              unsigned int param_4, unsigned int param_5)
{
    int s = *DAT_80610a20 + slot * 0x58;
    render_target_setup(slot, param_2, param_3, param_4, param_5,
                        (unsigned int)*(unsigned short*)(s + 0x4c),
                        (unsigned int)*(unsigned short*)(s + 0x50), 0, 0, 1);
}

// render_target_init_slot (0x8017aee8)
// Allocate a slot (render_target_alloc_slot) and initialise it from an optional
// descriptor (in_r7): copies the flag word / two pow2 bits, or clears them when
// none is given.  Sets the width/height (from the frame w/h in r3:hi/lo) and marks
// non-power-of-two dimensions via ceil_log2_u32.
// hgt/wid arrive as the spawn-frame pair (r3 hi:lo); descriptor (r7) and the
// out slot-index pointer (r8) arrive in registers at the call site.
void render_target_init_slot(int hgt, int wid, int in_r7, int* in_r8)
{
    *in_r8 = render_target_alloc_slot();
    int slot = *DAT_80610a20 + *in_r8 * 0x58;
    if (in_r7 == 0) {
        *(unsigned char*)(slot + 0x49) = 0;
        *(unsigned char*)(slot + 0x48) = 0;
        *(unsigned int*)(slot + 0x40) = 0;
    } else {
        *(unsigned int*)(slot + 0x40) = *(unsigned int*)(in_r7 + 0xc);
        *(unsigned char*)(slot + 0x48) = (unsigned char)*(unsigned int*)(in_r7 + 8) & 1;
        *(unsigned char*)(slot + 0x49) = (unsigned char)(*(unsigned int*)(in_r7 + 8) >> 1) & 1;
    }
    if (hgt != ceil_log2_u32(hgt)) {
        *(unsigned char*)(slot + 0x48) = 1;
    }
    if (wid != ceil_log2_u32(wid)) {
        *(unsigned char*)(slot + 0x49) = 1;
    }
    *(unsigned char*)(slot + 0x4a) = 0;
    *(unsigned short*)(slot + 0x4c) = (unsigned short)hgt;
    *(short*)(slot + 0x50) = (short)wid;
    *(unsigned short*)(slot + 0x4e) = (unsigned short)hgt;
    *(short*)(slot + 0x52) = (short)wid;
}

// render_target_get_half_dim (0x8017b034)
// Return half of the slot's stored dimension word ([+0x18] >> 1).
int render_target_get_half_dim(int slot)
{
    return *(int*)(slot + 0x18) >> 1;
}

// render_target_sweep_flagged (0x8017b040)
// Sweep the render-target slot table for the current group (frame FUN_803826f0),
// invoking the per-slot cleanup (FUN_8017c454) on every slot whose flag word has
// bit 0x8000000 set but bit 0x10000 clear.
void render_target_sweep_flagged(void)
{
    int g = (int)FUN_803826f0();
    int index = 0;
    int off = 0;
    while (index < *(int*)(&DAT_8058fb6c + g * 0x10)) {
        unsigned int flags = *(unsigned int*)(*(int*)(&DAT_8058fb60 + g * 0x10) + off + 0x40);
        if (((flags & 0x8000000) != 0) && ((flags & 0x10000) == 0)) {
            render_target_clear_slot(index);
        }
        index = index + 1;
        off = off + 0x58;
    }
    FUN_8038273c();
}

// render_target_restore_active (0x8017b0c0)
// Restore the active render-target id from the saved slot.
void render_target_restore_active(void)
{
    DAT_806122dc = DAT_806122f4;
}


// ===========================================================================
// batch_460 : 0x8017b0cc - 0x8017b3d8
// Render-target group / context management: the pool allocator, per-group slot
// table allocation, cursor/context reset helpers, the scratch-heap advance used
// by render_target_setup, and the slot-table compactor.  Companion to the
// render_target_* layer added in batch_459.
//
//   render_target_save_active       0x8017b0cc  save active RT id
//   render_target_advance_scratch   0x8017b0d8  advance the RT scratch heap (budget)
//   render_target_reset_context     0x8017b1b0  reset RT context counters
//   render_target_reset_cursor      0x8017b1cc  reset alloc cursor to base
//   render_target_compact_slots     0x8017b1d8  compact / repack the slot table
//   render_target_group_alloc       0x8017b2e0  allocate a group's slot table
//   render_target_set_group         0x8017b34c  select the active RT group
//   render_target_queue_animation   0x8017b364  thunk to stream_queue_animation
//   render_target_group_activate    0x8017b390  reset + activate a RT group
//   render_target_pool_alloc        0x8017b3d8  allocate the RT pool memory
// ===========================================================================

// group-context globals + budget
extern unsigned int DAT_80610a24;    // RT scratch budget (bytes)
extern int  DAT_806122e8;            // first-frame scratch mark (-1 = unset)
extern int  DAT_806122ec;            // current scratch cursor
extern int  DAT_806122f0;            // RT pool base pointer
extern int  DAT_806122f8;            // pending-reset flag
extern unsigned char DAT_80501524;   // current stage/mode id
extern unsigned char DAT_805014d8;   // sub-mode / player count
extern unsigned char DAT_8058fb64;   // per-group aux buffer base (stride 0x10)
extern unsigned char DAT_8058fb68;   // per-group compaction write cursor (stride 0x10)

extern int  FUN_80186aa0(void);      // scratch-heap mode query
extern unsigned int FUN_80186a98(void); // scratch-heap alt budget
extern void FUN_80184e78(void);      // RT pool post-init
extern void FUN_80184f58(void);      // RT group post-activate
extern void FUN_8017bffc(int pool, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                         void* cfg, int* flags, void* out);  // RT config apply
extern void sprite_replace_texture_id(int old_id, int new_id);
extern void memset_wrapper(int dst, int val, int len);
extern void stream_queue_animation(const char* path, int priority);

// default RT config blobs referenced by address
extern unsigned char DAT_8040f2c0;
extern unsigned char DAT_80612304;

// forward declarations for intra-file callers below
void render_target_set_group(int group);
void render_target_compact_slots(void);
void render_target_group_activate(void);

// mem_alloc_aligned is declared void-returning (stores the block via a side effect);
// route the value-returning call sites through a typed alias, matching gx_material_state.
typedef int (*MemAllocAlignedFn)(int alignment, int size, int allocType);
#define MEM_ALLOC_ALIGNED_R ((MemAllocAlignedFn)&mem_alloc_aligned)

// render_target_save_active (0x8017b0cc)
// Snapshot the active render-target id into the saved slot.
void render_target_save_active(void)
{
    DAT_806122f4 = DAT_806122dc;
}

// render_target_reset_cursor (0x8017b1cc)
// Rewind the scratch cursor to the saved base.
void render_target_reset_cursor(void)
{
    DAT_806122ec = DAT_806122f4;
}

// render_target_reset_context (0x8017b1b0)
// Clear all render-target context counters (scratch cursor, first-frame mark, base
// and the pending-reset flag).
void render_target_reset_context(void)
{
    DAT_806122ec = 0;
    DAT_806122e8 = 0xffffffff;
    DAT_806122f4 = 0;
    DAT_806122f8 = 0;
}

// render_target_advance_scratch (0x8017b0d8)
// Advance the render-target scratch heap by the frame's requested size (spawn frame
// FUN_803826f0, hi word).  Enforces the per-frame budget (DAT_80610a24, or an
// alternate from FUN_80186a98 when the scratch mode is active), wrapping the cursor
// back to the base when it would overflow, and records the first non-zero request's
// start (DAT_806122e8).  Returns nothing; the cursor lives in DAT_806122ec.
void render_target_advance_scratch(void)
{
    unsigned long long frame = (unsigned long long)FUN_803826f0();
    int size = (int)(frame >> 0x20);
    unsigned int budget = DAT_80610a24;
    if (FUN_80186aa0() != 0) {
        budget = FUN_80186a98();
    }
    if (budget <= (unsigned int)(DAT_806122ec + size)) {
        DAT_806122ec = DAT_806122f4;
    }
    if (DAT_806122f8 != 0) {
        DAT_806122f8 = 0;
        DAT_806122ec = DAT_806122f4;
        DAT_806122f4 = DAT_806122f4 + size;
    }
    FUN_80186aa0();
    if (((int)frame != 0) && (DAT_806122e8 == -1)) {
        DAT_806122e8 = DAT_806122ec;
    }
    DAT_806122ec = DAT_806122ec + size;
    FUN_8038273c();
}

// render_target_compact_slots (0x8017b1d8)
// Compact the current group's render-target slot table.  First passes over the slots
// bottom-up, moving every persistent slot (flag 0x20000) down into the write cursor
// (DAT_8058fb68) and remapping its texture id (sprite_replace_texture_id); then zeroes
// every non-persistent slot (setting its texture id to -1); finally clears the group's
// aux buffer.  Group index arrives via __save_gpr_28.
void render_target_compact_slots(void)
{
    int g = (int)__save_gpr_28() * 0x10;
    int* base = (int*)(&DAT_8058fb60 + g);
    int i = *(int*)(&DAT_8058fb6c + g) - 1;
    int off = i * 0x58;
    *(int*)(&DAT_8058fb68 + g) = i;
    for (; i >= 0; i = i - 1, off = off - 0x58) {
        unsigned int slot = *base + off;
        if ((*(unsigned int*)(slot + 0x40) & 0x20000) != 0) {
            int w = *(int*)(&DAT_8058fb68 + g);
            if (i != w) {
                leaf_31e8(*base + w * 0x58, slot, 0x58);
                sprite_replace_texture_id(i, w);
            }
            *(int*)(&DAT_8058fb68 + g) = w - 1;
        }
    }
    int j = 0, joff = 0;
    for (; j < (int)*(unsigned int*)(&DAT_8058fb6c + g); j = j + 1, joff = joff + 0x58) {
        if ((*(unsigned int*)(*base + joff + 0x40) & 0x20000) == 0) {
            memset_wrapper(*base + joff, 0, 0x58);
            *(int*)(*base + joff + 0x44) = 0xffffffff;
        }
    }
    memset_wrapper(*(int*)(&DAT_8058fb64 + g), 0, *(unsigned int*)(&DAT_8058fb6c + g));
}

// render_target_queue_animation (0x8017b364)
// Register-passthrough thunk to stream_queue_animation (queue an animation stream).
void render_target_queue_animation(const char* path, int priority)
{
    stream_queue_animation(path, priority);
}

// render_target_set_group (0x8017b34c)
// Point the active render-target pool at group `param_1` (each group is a 0x10-byte
// table header at DAT_8058fb60).
void render_target_set_group(int group)
{
    DAT_80610a20 = (int*)(&DAT_8058fb60 + group * 0x10);
}

// render_target_group_alloc (0x8017b2e0)
// Allocate a render-target group's slot table (count * 0x58 bytes) and its aux
// buffer (count bytes), storing both pointers in the group's 0x10-byte header.
void render_target_group_alloc(int group, int count)
{
    int g = group * 0x10;
    *(int*)(&DAT_8058fb6c + g) = count;
    *(int*)(&DAT_8058fb60 + g) = mem_alloc((*(int*)(&DAT_8058fb6c + g) * 0x58 + 0xf) & 0xfffffff0, 2);
    *(int*)(&DAT_8058fb64 + g) = mem_alloc((*(int*)(&DAT_8058fb6c + g) + 0xf) & 0xfffffff0, 2);
}

// render_target_group_activate (0x8017b390)
// Reset + activate render-target group 0: select the group, clear the context
// counters, sweep flagged slots and run the post-activate hook.
void render_target_group_activate(void)
{
    render_target_set_group(0);
    DAT_806122ec = 0;
    DAT_806122e8 = 0xffffffff;
    DAT_806122f8 = 0;
    DAT_806122f4 = 0;
    render_target_sweep_flagged();
    FUN_80184f58();
}

// render_target_pool_alloc (0x8017b3d8)
// Allocate the render-target pool's backing memory.  Compacts the slot table, picks
// the scratch budget (0x4b000, or 0x32000 in stage 5 with >1 players), allocates the
// pool 0x20-aligned, runs post-init, activates the group and applies the default RT
// config (FUN_8017bffc with DAT_8040f2c0 / DAT_80612304).
void render_target_pool_alloc(void)
{
    render_target_compact_slots();
    DAT_80610a24 = 0x4b000;
    if ((DAT_80501524 == 5) && (1 < DAT_805014d8)) {
        DAT_80610a24 = 0x32000;
    }
    DAT_806122f0 = MEM_ALLOC_ALIGNED_R(0x20, DAT_80610a24, 1);
    FUN_80184e78();
    render_target_group_activate();
    int cfg[3];
    cfg[0] = 0xffffffff;   // local_18[0]
    cfg[1] = 3;            // local_10
    cfg[2] = 0;            // local_c
    FUN_8017bffc(DAT_806122f0, 0, 0, 0, 0, 0, 0, 0, &DAT_8040f2c0, cfg, &DAT_80612304);
}

// ============================================================================
// batch_461  0x8017b480 - 0x8017bf24  | Date: 2026-09-06
// Render-target BUILD / BIND / SCRATCH layer (continues render_target_alloc.cpp
// from batch_459/460). render_target_build_full is the full-featured slot builder
// (mip chain + optional aux/env copy) sitting above render_target_init_slot;
// render_target_activate/bind/setup_scratch are the public entry points other
// subsystems (character outline, weather gate, nametag) call to (re)configure and
// (re)bind an off-screen texture target.  subsystem:graphics, tag gx-rendertarget.
//
//   render_target_build_full     0x8017b480  full mip/aux/env RT texture-object build
//   render_target_clear_bind     0x8017bbe4  bind wrapper (offset 0,0)
//   render_target_bind           0x8017bc10  bind using slot stored dims +0x4c/+0x50
//   render_target_alloc_scratch  0x8017bc4c  alloc off-screen scratch RT (log2 dims)
//   render_target_setup_scratch  0x8017bd38  scratch alloc + context flags + viewport
//   render_target_activate       0x8017bdb4  format-select frame worker -> setup
//   render_target_activate_default 0x8017be90 activate wrapper (fmt 4,4)
//   render_target_bind_default   0x8017bed4  bind wrapper (0,0,4,4)
//   render_target_flag_scavenge  0x8017bf04  mark slot flag 0x8000 (scavengeable)
//   render_target_restore_scratch 0x8017bf24 restore saved scratch mark
// ----------------------------------------------------------------------------

// ---- additional GX texture / EFB-copy services used by the build path ------
extern unsigned long long FUN_803826b4(void);   // frame ptr (hi=aux descriptor)
extern void FUN_80382700(void);                  // frame pop (of 803826b4)
extern void FUN_803c8dd0(unsigned int* texObj, unsigned int data, unsigned int w, unsigned int h,
                         unsigned int fmt, unsigned int wl2, unsigned int hl2, char a, int b);
extern unsigned long long FUN_803c8e18(double a, double b, double c, unsigned int* texObj,
                                       int d, int e, int f, char g, unsigned int h);
extern void FUN_803c92dc(unsigned int* texObj, unsigned int data, int a, int b);
extern unsigned long long FUN_80185088(unsigned long long a, unsigned long long b,
                                       unsigned long long c, unsigned long long d,
                                       unsigned long long e, unsigned long long f,
                                       unsigned long long g, unsigned long long h,
                                       unsigned int* texObj, unsigned int data,
                                       unsigned int t0, unsigned int t1, unsigned int fmt,
                                       unsigned int wl2, unsigned int hl2, unsigned int a2);
extern void FUN_801c1714(void);                  // pre-build render-state prime
extern void FUN_803c94d8(void);                  // re-select GX texture-copy state

// render_target_init_slot reads its arguments from registers (r4..r8); the scratch
// allocator invokes it with the frame already primed, so call it via a 0-arg alias.
typedef void (*RtInitSlotFn)(void);
#define RT_INIT_SLOT ((RtInitSlotFn)&render_target_init_slot)

// ---- raw data referenced by the build path ---------------------------------
extern int   DAT_80612300;             // shared EFB-copy scratch buffer size
extern float FLOAT_8061663c;           // clear/border constant 1.0-ish
extern float FLOAT_80616638;           // alternate clear constant
extern double DOUBLE_80616640;         // int->double magic bias (0x43300000<<32)

// mem_alloc_aligned as used by the build path: the decompiler surfaces the PPC
// register-spill parameter list, but only (align, size, allocType) are meaningful.
typedef unsigned int (*MemAllocAligned3)(int align, int size, int allocType);
#define MEM_ALLOC_ALIGNED3 ((MemAllocAligned3)&mem_alloc_aligned)

// render_target_build_full (0x8017b480)
// Build a complete render-target texture object for a slot described by a small
// descriptor fetched from the spawn frame (FUN_803826b4): dimensions [0],[1], copy
// dims [2],[3], flag word [6], format nibble at +0x12 (uVar18), mip count at +0x08
// (uVar2 low byte), colour packed at [5].  For each of the mip level count derived
// from the format, the routine either (a) copies from an inline data blob following
// the descriptor, or (b) allocates a fresh aligned EFB-copy buffer via
// mem_alloc_aligned + leaf_31e8 and programs the texture object (FUN_803c8b84 for the
// generic path, FUN_803c8dd0 for the 0xe "double" format).  For formats in the
// 0x610 mask it also builds a second (chroma/env) texture set, and format 0xe adds a
// palette/lut copy via FUN_803c92dc.  Records the allocated slot id back into the
// caller (*param_11) and stores it into the descriptor flag word.
//   pDescIn : incoming param_1 -> pDesc via FUN_803826b4 (register-passed)
//   isStreamed (param_12): non-zero => stream through allocated buffers (path b)
void render_target_build_full(unsigned int isStreamed, int* pOutSlot)
{
    unsigned long long frame = FUN_803826b4();
    int auxDesc = (int)(frame >> 0x20);
    unsigned int* pDesc = (unsigned int*)(unsigned int)frame;

    unsigned int width  = pDesc[0];
    unsigned int flagW  = pDesc[6] & 1;
    unsigned int height = pDesc[1];
    unsigned int copyW  = pDesc[2];
    unsigned int copyH  = pDesc[3];
    unsigned short mipCountRaw = *(unsigned short*)(pDesc + 4);
    unsigned int mipCount = (unsigned int)mipCountRaw;
    unsigned short fmtRaw = *(unsigned short*)((int)pDesc + 0x12);
    unsigned int fmt = (unsigned int)fmtRaw;
    unsigned int color = pDesc[5];

    unsigned int auxTex0 = 0, auxTex1 = 0;   // uVar17, uVar16
    unsigned int chroma0 = 0, chroma1 = 0;   // local_54, local_58
    unsigned int block = 0;                  // unaff_r24 (streamed buffer)
    double dLo = 0.0, dHi = 0.0;

    if (((int)width < 0) || ((int)height < 0)) {
        *pOutSlot = -1;
        FUN_80382700();
        return;
    }

    FUN_801c1714();
    int slotId = render_target_alloc_slot();
    *pOutSlot = slotId;
    unsigned int* slot = (unsigned int*)(*DAT_80610a20 + slotId * 0x58);

    if (auxDesc == 0) {
        *(unsigned char*)((int)slot + 0x49) = 0;
        *(unsigned char*)(slot + 0x12) = 0;
        slot[0x10] = 0;
    } else {
        slot[0x10] = *(unsigned int*)(auxDesc + 0xc);
        *(unsigned char*)(slot + 0x12) = (unsigned char)(*(unsigned int*)(auxDesc + 8) & 1);
        *(unsigned char*)((int)slot + 0x49) = (unsigned char)((*(unsigned int*)(auxDesc + 8) >> 1) & 1);
    }

    *(char*)((int)slot + 0x4a)  = (char)fmtRaw;
    *(short*)(slot + 0x13)      = (short)width;
    *(short*)(slot + 0x14)      = (short)height;
    *(short*)((int)slot + 0x4e) = (short)copyW;
    *(short*)((int)slot + 0x52) = (short)copyH;
    *(char*)(slot + 0x15)       = (char)(color >> 0x18);
    *(char*)((int)slot + 0x55)  = (char)(color >> 0x10);
    *(char*)((int)slot + 0x56)  = (char)(color >> 8);
    *(unsigned char*)((int)slot + 0x57) = 0;
    if (copyH != height) *(unsigned char*)((int)slot + 0x49) = 1;
    if (copyW != width)  *(unsigned char*)(slot + 0x12) = 1;

    unsigned int fmtB = 0xffffffff;   // uVar14 (secondary format)
    unsigned int wl2 = countLeadingZeros((unsigned int)*(unsigned char*)(slot + 0x12)) >> 5;
    unsigned int hl2 = countLeadingZeros((unsigned int)*(unsigned char*)((int)slot + 0x49)) >> 5;
    unsigned int fmtA;   // uVar15 (primary GX texel format)

    switch (fmt) {
    case 0:  fmtA = 6;  break;
    case 1:  fmtA = 4;  break;
    case 2:  fmtA = 0xe; break;
    case 3:  fmtA = 0;  break;
    case 4:  fmtA = 0xe; fmtB = 0;   break;
    case 5:  fmtA = 2;  break;
    case 6:  fmtA = 5;  break;
    case 7:  fmtA = 1;  break;
    case 8:  fmtA = 3;  break;
    case 9:  fmtA = 0xe; fmtB = 1;   break;
    case 10: fmtA = 0xe; fmtB = 0xe; break;
    case 0xb: fmtA = 0; break;
    case 0xc: fmtA = 1; break;
    case 0xd: fmtA = 0xe; break;
    case 0xe: fmtA = 9; break;
    default:
        render_target_clear_slot(*pOutSlot);
        *pOutSlot = -1;
        FUN_80382700();
        return;
    }

    unsigned int copyWHalf = ((int)copyW >> 1) + (unsigned int)((int)copyW < 0 && (copyW & 1) != 0);
    unsigned int copyHHalf = ((int)copyH >> 1) + (unsigned int)((int)copyH < 0 && (copyH & 1) != 0);
    unsigned int mipByte = (unsigned int)(mipCountRaw & 0xff);

    if ((mipCount != 0) && (isStreamed != 0)) {
        auxTex0 = FUN_803c8960(copyW & 0xffff, copyH & 0xffff, fmtA, '\0', 1);
        auxTex1 = FUN_803c8960(copyWHalf & 0xffff, copyHHalf & 0xffff, fmtA, '\x01', mipByte);
        if ((1 << fmt & 0x610U) != 0) {
            chroma0 = FUN_803c8960(copyW & 0xffff, copyH & 0xffff, fmtB, '\0', 1);
            chroma1 = FUN_803c8960(copyWHalf & 0xffff, copyHHalf & 0xffff, fmtB, '\x01', mipByte);
        }
    }

    // ---- primary texture-object build ----
    if (((mipCount == 0) || (isStreamed == 0)) || (flagW != 0)) {
        if (isStreamed == 0) {
            if (fmt == 0xe) {
                FUN_803c8dd0(slot, (unsigned int)(pDesc + 8), copyW & 0xffff, copyH & 0xffff,
                             fmtA, wl2, hl2, mipCount != 0, 0);
            } else {
                FUN_803c8b84(slot, (unsigned int)(pDesc + 8), copyW & 0xffff, copyH & 0xffff,
                             fmtA, wl2, hl2, mipCount != 0);
            }
        } else {
            block = MEM_ALLOC_ALIGNED3(0x20, (DAT_80612300 + 0xf) & 0xfffffff0, 1);
            leaf_31e8(block, (unsigned int)(pDesc + 8), DAT_80612300);
            if (fmt == 0xe) {
                FUN_803c8dd0(slot, block, copyW & 0xffff, copyH & 0xffff, fmtA, wl2, hl2,
                             mipCount != 0, 0);
            } else {
                FUN_803c8b84(slot, block, copyW & 0xffff, copyH & 0xffff, fmtA, wl2, hl2,
                             mipCount != 0);
            }
        }
    } else {
        unsigned long long tex;
        if (fmt == 0xe) {
            FUN_803c8dd0(slot, 0, copyW & 0xffff, copyH & 0xffff, fmtA, wl2, hl2, '\x01', 0);
        } else {
            FUN_803c8b84(slot, 0, copyW & 0xffff, copyH & 0xffff, fmtA, wl2, hl2, '\x01');
        }
        tex = 0;   // build result is carried via the slot object; the frame ptr is 0 here
        // Stream the freshly-built object into the shared upload path.
        FUN_80185088(tex, 0, 0, 0, 0, 0, 0, 0, slot, (unsigned int)(pDesc + 8),
                     auxTex0, auxTex1, fmtA, wl2, hl2, mipCount != 0);
    }

    // ---- texture mode / border colour ----
    if (mipCount == 0) {
        double dc = (double)FLOAT_8061663c;
        FUN_803c8e18(dc, dc, dc, slot, 1, 1, 0, '\0', 0);
    } else {
        int filt = 5; unsigned int m = 2;
        if (fmt == 0xe) { filt = 3; m = 0; }
        double lodMax = (double)(float)((double)((unsigned long long)0x4330000000000000ULL |
                                                 (unsigned long long)mipCount) - DOUBLE_80616640);
        if (isStreamed == 0) {
            dHi = (double)FLOAT_8061663c;
            dLo = lodMax;
            FUN_803c8e18(dHi, dLo, dHi, slot, filt, 1, 0, '\x01', m);
        } else {
            dHi = (double)FLOAT_8061663c;
            dLo = lodMax;
            FUN_803c8e18((double)FLOAT_80616638, dLo, dHi, slot, filt, 1, 0, '\x01', m);
        }
    }

    // ---- secondary (chroma / env) texture set for 0x610-mask formats ----
    if ((1 << fmt & 0x610U) != 0) {
        unsigned int cw = copyW, ch = copyH, mips = mipCount + 1;
        int off = (isStreamed != 0) ? 0 : 0x20;
        if (((mipCount != 0) && (isStreamed != 0)) && (flagW == 0)) {
            cw = copyWHalf; ch = copyHHalf; mips = mipCount;
        }
        for (; mips != 0; mips--) {
            unsigned int a = cw * ch;
            off += ((int)a >> 1) + (unsigned int)((int)a < 0 && (a & 1) != 0);
            cw = ((int)cw >> 1) + (unsigned int)((int)cw < 0 && (cw & 1) != 0);
            ch = ((int)ch >> 1) + (unsigned int)((int)ch < 0 && (ch & 1) != 0);
        }
        if (((isStreamed == 0) || (mipCount == 0)) || (flagW != 0)) {
            unsigned int src = (isStreamed == 0) ? ((unsigned int)pDesc + off) : (block + off);
            FUN_803c8b84(slot + 8, src, copyW & 0xffff, copyH & 0xffff, fmtB, wl2, hl2,
                         mipCount != 0);
        } else {
            FUN_803c8b84(slot + 8, 0, copyW & 0xffff, copyH & 0xffff, fmtB, wl2, hl2,
                         mipCount != 0);
            FUN_80185088(0, dLo, dHi, 0, 0, 0, 0, 0, slot + 8,
                         (unsigned int)pDesc + auxTex1 + auxTex0 + 0x20, chroma0, chroma1,
                         fmtB, wl2, hl2, mipCount != 0);
        }
        if (mipCount != 0) {
            float border = (isStreamed != 0) ? FLOAT_80616638 : FLOAT_8061663c;
            double lodMax = (double)(float)((double)((unsigned long long)0x4330000000000000ULL |
                                                     (unsigned long long)mipCount) - DOUBLE_80616640);
            FUN_803c8e18((double)border, lodMax, (double)FLOAT_8061663c, slot + 8, 5, 1, 0, '\x01', 2);
        }
    }

    // ---- format 0xe palette / LUT copy ----
    if (fmt == 0xe) {
        int lutOff = 0x20;
        unsigned int cw = copyW, ch = copyH;
        for (int i = mipCount + 1; i != 0; i--) {
            int a = cw * ch;
            cw = ((int)cw >> 1) + (unsigned int)((int)cw < 0 && (cw & 1) != 0);
            lutOff += a;
            ch = ((int)ch >> 1) + (unsigned int)((int)ch < 0 && (ch & 1) != 0);
        }
        unsigned int lut;
        if (isStreamed == 0) {
            lut = (unsigned int)pDesc + lutOff;
        } else {
            lut = MEM_ALLOC_ALIGNED3(0x20, 0x200, 1);
            leaf_31e8(lut, (unsigned int)pDesc + lutOff, 0x200);
        }
        FUN_803c92dc(slot + 8, lut, 1, 0x100);
    }

    // ---- finalise: invalidate texture regions ----
    if ((((isStreamed == 0) || (mipCount == 0)) || (flagW != 0))) {
        FUN_803c9030((int)slot, 0xffffffff);
        if (fmt != 0xe) {
            FUN_803c9030((int)(slot + 8), 0xffffffff);
        }
    }

    pDesc[6] = pDesc[6] | *pOutSlot << 1;
    FUN_80382700();
}

// render_target_clear_bind (0x8017bbe4)
// Bind render-target `slot` with a zero offset, forwarding a control flag.
void render_target_clear_bind(int slot, unsigned int flags)
{
    render_target_bind(slot, 0, 0, flags);
}

// render_target_bind (0x8017bc10)
// Re-bind an existing render-target slot as the active target using its stored copy
// dimensions (+0x4c width, +0x50 height), delegating to render_target_activate.
void render_target_bind(int slot, unsigned int param_2, unsigned int param_3, unsigned int flags)
{
    int s = *DAT_80610a20 + slot * 0x58;
    render_target_activate(slot, param_2, param_3,
                           (unsigned int)*(unsigned short*)(s + 0x4c),
                           (unsigned int)*(unsigned short*)(s + 0x50), flags);
}

// render_target_alloc_scratch (0x8017bc4c)
// Allocate the off-screen "scratch" render target: rounds the frame-supplied
// dimensions (FUN_803826e8) up to power-of-two via ceil_log2_u32, initialises a slot
// (render_target_init_slot), marks it non-resident (flag 0x8000000) and builds the
// texture object from the shared scratch buffer DAT_806122f0.
void render_target_alloc_scratch(unsigned int param_1, unsigned int param_2, int* pOutSlot)
{
    unsigned long long frame = FUN_803826e8();
    int hgt = (int)(frame >> 0x20);
    int lg2h = ceil_log2_u32(hgt);
    int lg2w = ceil_log2_u32((int)frame);
    unsigned int wPad = hgt + 0xf;
    unsigned int wPlus1 = (unsigned int)frame + 1;

    RT_INIT_SLOT();
    unsigned int buf = DAT_806122f0;
    unsigned int hl2 = countLeadingZeros(lg2w - (wPlus1 & 0xfffffffe)) >> 5;
    unsigned int wl2 = countLeadingZeros(lg2h - (wPad & 0xfffffff0)) >> 5;
    unsigned int* slot = (unsigned int*)(*DAT_80610a20 + *pOutSlot * 0x58);
    slot[0x10] = slot[0x10] | 0x8000000;
    FUN_803c8b84(slot, buf, wPad & 0xfff0, wPlus1 & 0xfffe, 4, wl2, hl2, '\0');
    FUN_803c8fa8(slot, 1, 1);
    FUN_803c9030((int)slot, 0xffffffff);
    FUN_80382734();
}

// render_target_setup_scratch (0x8017bd38)
// Allocate the scratch render target (render_target_alloc_scratch, dims via
// __save_gpr_28 frame) and configure the owning render-context object: set the
// scratch-active flag +0x21c bit 3, clear bit 4, stash the slot id at +0x220, and
// program its viewport rect to the full scratch dimensions.
void render_target_setup_scratch(unsigned int param_1, unsigned int param_2, int ctx, int* pOutSlot)
{
    unsigned long long frame = __save_gpr_28();
    int hgt = (int)(frame >> 0x20);
    render_target_alloc_scratch(hgt, (int)frame, pOutSlot);
    *(unsigned int*)(ctx + 0x21c) = *(unsigned int*)(ctx + 0x21c) | 8;
    *(unsigned int*)(ctx + 0x21c) = *(unsigned int*)(ctx + 0x21c) & 0xffffffef;
    *(int*)(ctx + 0x220) = *pOutSlot;
    viewport_set_rect((int*)(ctx + 0x224), 0, 0, hgt - 1, (int)frame - 1);
    __restore_gpr_28();
}

// render_target_activate (0x8017bdb4)
// Frame worker that maps a caller-supplied surface-format enum (param_6 low byte,
// 0..7) to the GX EFB-copy format pair (uVar1/uVar2), extracts the pending-reset,
// scissor and clear flags from the high bits of param_6, and hands off to
// render_target_setup.  Frame ptr via FUN_803826f0.  Public entry used by the
// character outline / weather-gate / nametag off-screen passes.
void render_target_activate(unsigned int param_1, unsigned int param_2, unsigned int param_3,
                            unsigned int param_4, unsigned int param_5, unsigned int flags)
{
    unsigned long long frame = FUN_803826f0();
    unsigned int fA = param_4, fB = param_5;
    switch (flags & 0xff) {
    case 0: fA = 4;    fB = 4; break;
    case 1: fA = 5;    fB = 5; break;
    case 2: fA = 6;    fB = 6; break;
    case 3: fA = 0x27; fB = 1; break;
    case 4: fA = 0x20; fB = 0; break;
    case 5: fA = 0x28; fB = 1; break;
    case 6: fA = 1;    fB = 1; break;
    case 7: fA = 0x22; fB = 2; break;
    }
    DAT_806122f8 = flags >> 0xe & 1;
    render_target_setup((int)(frame >> 0x20), (int)frame, param_3, fA, fB, param_4, param_5,
                        flags >> 0xf & 1, flags >> 0xc & 1, (flags >> 0xd & 1) ^ 1);
    FUN_8038273c();
}

// render_target_activate_default (0x8017be90)
// Activate a target with the default RGBA8 copy format (4,4) and no special flags.
void render_target_activate_default(unsigned int param_1, unsigned int param_2, unsigned int param_3,
                                    unsigned int param_4, unsigned int param_5)
{
    render_target_setup(param_1, param_2, param_3, 4, 4, param_4, param_5, 0, 0, 1);
}

// render_target_bind_default (0x8017bed4)
// Re-bind a slot with default (4,4) copy format and zero offset.
void render_target_bind_default(int slot)
{
    render_target_bind_sized(slot, 0, 0, 4, 4);
}

// render_target_flag_scavenge (0x8017bf04)
// Mark a slot's flag word (+0x40) with bit 0x8000, tagging it as reusable/scavengeable
// by render_target_alloc_slot on a later frame.
void render_target_flag_scavenge(int slot)
{
    int off = slot * 0x58 + 0x40;
    *(unsigned int*)(*DAT_80610a20 + off) = *(unsigned int*)(*DAT_80610a20 + off) | 0x8000;
}

// render_target_restore_scratch (0x8017bf24)
// Restore the previously-latched scratch render-target mark: if a scratch target was
// active (DAT_806122e8 != -1), copy it to the "current" cursor DAT_806122ec, clear the
// latch, and re-select the GX texture-copy state (FUN_803c94d8).
void render_target_restore_scratch(void)
{
    if (DAT_806122e8 != -1) {
        DAT_806122ec = DAT_806122e8;
        DAT_806122e8 = -1;
        FUN_803c94d8();
    }
}

// ============================================================================
// batch_462  0x8017bf5c - 0x8017c430  | Date: 2026-09-06
// Render-target LOAD (asset -> texture object) + GROUP-alloc sizing + EFB readback
// alloc.  These are the public entry points the streaming / scene-load / cutscene
// paths call to turn a packed texture asset into a live render-target slot, plus the
// per-frame group table sizing driven from game_tick_update / frame_render_update.
// Continues render_target_alloc.cpp (batch_459/460/461).  subsystem:graphics,
// tag gx-rendertarget.
//
//   render_target_alloc_readback     0x8017bf5c  alloc aligned EFB readback buffer + bind
//   render_target_bind_sized_default 0x8017bfd4  bind_sized wrapper (fmt 4,4)
//   render_target_load_from_data     0x8017bffc  build RT from an in-memory data blob
//   render_target_load_named         0x8017c03c  load named ".gct" asset -> build RT
//   render_target_load_by_id         0x8017c154  load "textures/%06x.gct" by id (dedup)
//   render_target_alloc_scratch_wrapper 0x8017c330  thunk -> render_target_alloc_scratch
//   render_target_nop_a              0x8017c354  empty
//   render_target_nop_b              0x8017c358  empty
//   render_target_group_alloc_default 0x8017c35c  size + alloc the default RT group
//   render_target_reset_and_advance  0x8017c430  reset cursor + advance frame
// ----------------------------------------------------------------------------

// ---- asset / path / string services (raw, not in generated_decls) ----------
extern void FUN_803c8f7c(int slot, unsigned int data);        // bind readback buffer to slot
extern int  FUN_8038b5c8(int str);                            // strlen
extern int  FUN_8038b378(unsigned int* a, unsigned int* b);   // 3-char tag compare
extern unsigned long long FUN_80387fb8(unsigned long long f1, double a, double b,
                                       unsigned long long d, unsigned long long e,
                                       unsigned long long g, unsigned long long h,
                                       unsigned long long i, int outBuf, const char* fmt,
                                       unsigned int arg0, void* arg1, unsigned int a13,
                                       unsigned int a14, unsigned int a15, unsigned int a16);
extern int  FUN_8018660c(unsigned int size);                  // "needs readback staging?" predicate
extern void FUN_80186638(void);                               // frame advance worker
extern unsigned long long FUN_803826e4(void);                 // frame ptr (hi=texId, lo=descriptor)
extern void FUN_80382730(void);                               // frame pop (of 803826e4)

// ---- data globals referenced by the load / group-size paths ----------------
extern int DAT_806109c4;         // screen / render-target width
extern int DAT_806109c8;         // screen / render-target height
extern int DAT_80612614;         // game-mode id (0x65 => fixed 0x200 target)
extern unsigned int DAT_80610a28;  // ".gct" extension tag compare source
extern unsigned char DAT_80610a2c; // ".gct" extension append template
extern int DAT_806115d8;         // cumulative texture bytes-loaded stat
extern int DAT_806122fc;         // temp readback-staging buffer ptr

// ---- format path templates (rodata) ----------------------------------------
extern const char s_textures__06x_gct_80411354[];   // "textures/%06x.gct"
extern const char s_textures_CT_06x_gct_80411368[];  // "textures/CT%06x.gct"

// render_target_alloc_readback (0x8017bf5c)
// Allocate an aligned EFB read-back staging buffer sized for the slot's copy region
// (+0x4c width, +0x50 height, RGBA8 fmt 4), mark the slot's flag word (+0x40) with
// bit 0x10000 (readback-resident), and bind the buffer to the slot texture object
// via FUN_803c8f7c.  The mem_alloc_aligned register-spill list is reduced to
// (align, size, allocType).
void render_target_alloc_readback(int slot)
{
    int s = *DAT_80610a20 + slot * 0x58;
    unsigned int size = FUN_803c8960((unsigned int)*(unsigned short*)(s + 0x4c),
                                     (unsigned int)*(unsigned short*)(s + 0x50), 4, '\0', 0);
    unsigned int buf = MEM_ALLOC_ALIGNED3(0x20, (int)size, 1);
    *(unsigned int*)(s + 0x40) = *(unsigned int*)(s + 0x40) | 0x10000;
    FUN_803c8f7c(s, buf);
}

// render_target_bind_sized_default (0x8017bfd4)
// Bind a render-target slot at (param_2, param_3) using the default RGBA8 copy
// format (4,4) -> render_target_bind_sized.
void render_target_bind_sized_default(int slot, unsigned int param_2, unsigned int param_3)
{
    render_target_bind_sized(slot, param_2, param_3, 4, 4);
}

// render_target_load_from_data (0x8017bffc)
// Public entry: build a render-target texture object directly from an already-resident
// data blob (isStreamed=0).  Resets *pOutSlot to -1 then delegates to
// render_target_build_full.  Widely called (scene block load, cutscene, loadscreen,
// texture stream, render_target_pool_alloc default config).
void render_target_load_from_data(int* pOutSlot)
{
    if (pOutSlot != (int*)0) {
        *pOutSlot = -1;
    }
    render_target_build_full(0, pOutSlot);   // isStreamed = 0 (data already in memory)
}

// render_target_load_named (0x8017c03c)
// Load a named texture asset into a render-target slot (from stream_queue_process /
// stream_secondary_process).  Reads the asset name from the spawn frame (FUN_803826e8),
// appends the ".gct" extension when it is not already present (tag compare FUN_8038b378
// vs DAT_80610a28), sizes and allocates a copy buffer (staging alignment 0x40 when the
// descriptor requests it, flag 0x20000), reads the asset (asset_read_to_buffer), and
// builds the texture object via render_target_build_full.
void render_target_load_named(int* pOutSlot)
{
    unsigned char nameBuf[155];
    unsigned long long frame = FUN_803826e8();
    unsigned char* name = (unsigned char*)(unsigned int)(frame >> 0x20);
    int descriptor = (int)frame;

    int len = FUN_8038b5c8((int)name);
    if (FUN_8038b378((unsigned int*)(name + len + -3), (unsigned int*)&DAT_80610a28) == 0) {
        // No ".gct" extension present: build "<name>.gct" into nameBuf.
        FUN_80387fb8(0, 0.0, 0.0, 0, 0, 0, 0, 0, (int)(nameBuf + 3), (const char*)&DAT_80610a2c,
                     (unsigned int)name, 0, 0, 0, 0, 0);
        int l2 = FUN_8038b5c8((int)(nameBuf + 3));
        nameBuf[l2 + 3] = 'g';
        nameBuf[l2 + 4] = 'c';
        nameBuf[l2 + 5] = 't';
        name = nameBuf + 3;
    }

    unsigned int size = (unsigned int)asset_get_size((const char*)name);
    if (0 < (int)size) {
        int allocType = 1;
        if ((descriptor != 0) && ((*(unsigned int*)(descriptor + 0xc) & 0x20000) != 0)) {
            allocType = 2;
        }
        unsigned int buf = MEM_ALLOC_ALIGNED3(0x40, (size + 0xf) & 0xfffffff0, allocType);
        DAT_806115d8 = DAT_806115d8 + size;
        asset_read_to_buffer((const char*)name, (void*)buf, size);
        // isStreamed=0 here: the descriptor is `descriptor`, the built slot goes to pOutSlot.
        (void)buf; (void)descriptor;
        render_target_build_full(0, pOutSlot);
    }
    FUN_80382734();
}

// render_target_load_by_id (0x8017c154)
// Load a render-target texture by numeric id (from stream_queue_process /
// stream_secondary_process).  First scans the current group's slot table for an entry
// already carrying this id (+0x44) and reuses it; otherwise formats the asset path
// "textures/%06x.gct" (falling back to "textures/CT%06x.gct"), sizes+reads the asset,
// and builds the texture object.  Large textures needing a staging copy take the
// FUN_8018660c path (0x40-aligned buffer DAT_806122fc, DAT_80612300 = size,
// isStreamed=1) and free the staging buffer via mem_free_top afterwards.  Records the
// resolved id back into the slot (+0x44) and a per-slot byte at DAT_80610a20[1].
void render_target_load_by_id(int* pOutSlot)
{
    unsigned char pathBuf[1028];
    unsigned long long frame = FUN_803826e4();
    int texId = (int)(frame >> 0x20);
    int descriptor = (int)frame;
    int outSlot = -1;

    // ---- dedup: find an existing slot already holding texId ----
    int cursor = DAT_80610a20[2] + 1;
    int remaining = DAT_80610a20[3] - cursor;
    int off = cursor * 0x58;
    if (cursor < DAT_80610a20[3]) {
        do {
            if (texId == *(int*)(*DAT_80610a20 + off + 0x44)) {
                if (pOutSlot != (int*)0) *pOutSlot = cursor;
                FUN_80382730();
                return;
            }
            cursor++;
            off += 0x58;
            remaining--;
        } while (remaining != 0);
    }

    int* group = DAT_80610a20;
    FUN_80387fb8(0, 0.0, 0.0, 0, 0, 0, 0, 0, (int)pathBuf, s_textures__06x_gct_80411354,
                 (unsigned int)texId, group, 0, 0, 0, 0);
    unsigned int size = (unsigned int)asset_get_size((const char*)pathBuf);
    pack_stub_noop_b0fc();
    if (size == 0xffffffff) {
        FUN_80387fb8(0, 0.0, 0.0, 0, 0, 0, 0, 0, (int)pathBuf, s_textures_CT_06x_gct_80411368,
                     (unsigned int)texId, group, 0, 0, 0, 0);
        size = (unsigned int)asset_get_size((const char*)pathBuf);
    }

    if (0 < (int)size) {
        int needsStaging = FUN_8018660c(size);
        unsigned int buf;
        if (needsStaging) {
            buf = MEM_ALLOC_ALIGNED3(0x40, size + 0x40, 0);
            DAT_806122fc = (int)buf;
        } else {
            buf = MEM_ALLOC_ALIGNED3(0x20, size, 1);
        }
        asset_read_to_buffer((const char*)pathBuf, (void*)buf, size);
        DAT_806115d8 = DAT_806115d8 + size;
        if (needsStaging) {
            DAT_80612300 = size;
            render_target_build_full(1, &outSlot);   // isStreamed = 1
            mem_free_top(DAT_806122fc);
        } else {
            render_target_build_full(0, &outSlot);   // isStreamed = 0
        }
        *(char*)(DAT_80610a20[1] + outSlot) = (char)*(unsigned int*)(descriptor + 0xc);
        *(int*)(*DAT_80610a20 + outSlot * 0x58 + 0x44) = texId;
    }
    if (pOutSlot != (int*)0) *pOutSlot = outSlot;
    FUN_80382730();
}

// render_target_alloc_scratch_wrapper (0x8017c330)
// Passthrough thunk to render_target_alloc_scratch.
void render_target_alloc_scratch_wrapper(unsigned int param_1, unsigned int param_2,
                                         unsigned int param_3, unsigned int param_4, int* pOutSlot)
{
    render_target_alloc_scratch(param_1, param_2, pOutSlot);
}

// render_target_nop_a (0x8017c354) / render_target_nop_b (0x8017c358)
// Empty render-target callback stubs.
void render_target_nop_a(void) {}
void render_target_nop_b(void) {}

// render_target_group_alloc_default (0x8017c35c)
// Compute the default render-target group's slot count from the screen dimensions
// (DAT_806109c4 width x DAT_806109c8 height, rounded up to 0x20; a fixed 0x200 is used
// in game-mode 0x65) and allocate the group (render_target_group_alloc group 0).  The
// count formula reserves space for the colour (x4) + depth (x8) EFB copies, converted
// to 0x100-byte tiles then 0x20-slot pages.  Called from game_tick_update.
void render_target_group_alloc_default(void)
{
    int h = (DAT_80612614 != 0x65) ? DAT_806109c8 : 0x200;
    int colorBytes = DAT_806109c4 * ((h + 0x1f) & 0xffffffe0) * 4;
    int depthBytes = DAT_806109c4 * ((h + 0x1f) & 0xffffffe0) * 8;
    int count = ((colorBytes + depthBytes >> 8) + 0x1f >> 5);
    render_target_group_alloc(0, count);
}

// render_target_reset_and_advance (0x8017c430)
// Rewind the scratch cursor (render_target_reset_cursor) then run the per-frame
// render-target advance worker (FUN_80186638).  Called from frame_render_update.
void render_target_reset_and_advance(void)
{
    render_target_reset_cursor();
    FUN_80186638();
}

// ============================================================================
// batch_463  0x8017c454  (grouped with the gx-2d-state cluster; the RT member lives
// here where the pool globals are).  subsystem:graphics, tag gx-rendertarget.
// ----------------------------------------------------------------------------

// render_target_clear_slot (0x8017c454)
// Reset a render-target slot's flag word (+0x40) to 0x800000, releasing the slot back
// to the free-scavenge pool (render_target_alloc_slot may reclaim it).  Called by the
// build path on a format error and by the sweep pass.
void render_target_clear_slot(int slot)
{
    *(unsigned int*)(*DAT_80610a20 + slot * 0x58 + 0x40) = 0x800000;
}
