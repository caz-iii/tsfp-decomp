// ============================================================================
// Address range: 0x801851b0 - 0x801859a8 | batch_477 | Date: 2026-09-06
// Subsystem: audio (texture-streaming state machine built on the audio scratch
//            ring / DSP command slots).  tag audio-stream-tex.
//
// The streaming-texture pipeline that sits directly on top of the batch_476
// audio scratch buffer + DSP command-slot machinery.  stream_texture_staged_load
// (batch_476, 0x80185088) is the one-shot path; this file is the incremental
// path: a small state machine (DAT_805a0780) that reads a texture from the DVD
// device in 0x2000-byte chunks into the streaming scratch region, tracks a
// 0x32-entry request ring (DAT_805a0dbc, stride 0x88), then stages the finished
// data into GX texture objects and kicks the DSP DMA that publishes it.
//
//   stream_tex_state_reset        0x801851b0  snapshot cursors, clear the busy flag
//   stream_tex_flush_all          0x801851d8  drain the state machine + free every slot
//   stream_tex_request_slot_insert 0x801852f0 find/allocate a request-ring slot
//   stream_tex_poll_asset_size    0x801853b4  asset_get_size wrapper (returns 1)
//   stream_tex_pump_state_machine 0x801853d8  the DVD-read / DSP-upload state machine
//   stream_tex_compute_gx_texobj  0x80185680  build main + half-res GX texture objects
//   stream_tex_bind_slot          0x80185744  bind one streamed slot (double the dims)
//   stream_tex_stage_upload       0x80185844  stage a slot's data into the DSP scratch
//   stream_tex_nop                0x801859a4  empty hook
//   stream_tex_dsp_dma_kick       0x801859a8  alloc DSP cmd slot + device xfer to publish
//
// Structurally-faithful non-matching port.  The register-pair returns from the
// spawn-frame helpers (FUN_803826e8 / FUN_803826d8) carry the GX texture
// descriptor in the high word and the source/handle in the low word; they are
// read through typed function-pointer aliases so the pair is recovered without a
// void-return capture.  The __save/__restore_gpr_25 spill stubs are dropped as
// ABI artifacts.

#include "common.h"
#include "generated_decls.h"

// ---- spawn-frame register-pair helpers --------------------------------------
// Both return r3:r4.  high 32 bits = GX texture-descriptor pointer, low 32 bits =
// staging/source address (or stream handle).  Declared void in generated_decls,
// so call through typed aliases to recover the pair.
typedef unsigned long long (*FramePairFn)(void);
#define FRAME_PAIR_E ((FramePairFn)FUN_803826e8)
#define FRAME_PAIR_D ((FramePairFn)FUN_803826d8)
extern void FUN_803826e8(void);
extern void FUN_803826d8(void);
extern void FUN_80382734(void);   // spawn-frame epilogue (matches *_e prologue)
extern void FUN_80382724(void);   // spawn-frame epilogue (matches *_d prologue)

// ---- GX texture-descriptor accessors (SDK-ish, in the 0x803c8xxx block) ------
extern double       FUN_803c90d8(int desc);   // detail / LOD scalar
extern unsigned int FUN_803c904c(int desc);   // width
extern unsigned int FUN_803c905c(int desc);   // height
extern int          FUN_803c906c(int desc);   // format
extern unsigned int FUN_803c9074(int desc);   // wrap-s / filter
extern unsigned int FUN_803c9080(int desc);   // wrap-t / filter
extern int          FUN_803c9040(int desc);   // source data pointer
extern int  FUN_803c8960(unsigned int w, unsigned int h, int fmt, char mip, unsigned int detail); // GXInitTexObj-alike, returns tex-obj handle
extern void FUN_803c8b84(unsigned int *desc, int dataAddr, unsigned int w, unsigned int h,
                         unsigned int fmt, unsigned int wrapS, unsigned int wrapT, char mip);
extern void FUN_803c8e18(double lo, double hi, double detail, unsigned int *desc,
                         int a5, int a6, int a7, char a8, int a9);
extern void FUN_803c9030(int desc, int slot);

// ---- device / DMA / memory helpers ------------------------------------------
extern void FUN_803994a4(unsigned int addr, int len);   // DCFlushRange
extern void FUN_803994d0(unsigned int addr, int len);   // DCStoreRange (partial)
extern int  FUN_80187544(int handle);                    // poll: device read ready?
extern int  FUN_80187260(int handle);                    // close/release device handle
extern int  dvd_read_wrapper_7414(unsigned long long a1, unsigned long long a2,
                                  unsigned long long a3, unsigned long long a4,
                                  unsigned long long a5, unsigned long long a6,
                                  unsigned long long a7, unsigned long long a8,
                                  int handle, void *dst, int devOffset, int len,
                                  int a13, int a14, int a15, int a16); // queue device read
extern void FUN_803abe54(void *reqBlock, int cmd, int mode, int prio,
                         void *memAddr, int devOffset, int len, int callback); // async device xfer
extern void FUN_8038b510(int *dst, int *src);            // string/struct copy (name store)

// occ scratch flush helper used by the chunk path (partial-line DCStore)
extern void leaf_31e8(int a, int b, int c);

// dsp_cmd_slot_alloc / stream_instance_id_alloc resolve through generated_decls.
// The ROM consumes dsp_cmd_slot_alloc's r3 (slot block ptr); it is declared
// unsigned char*-returning in batch_476, and generated_decls carries that
// prototype, so call it directly.

// ---- streaming state machine globals ----------------------------------------
extern int   DAT_805a0780;   // state: 0 idle, 1 issue-read, 2 read-pending, 3 upload
extern int   DAT_805a0784;   // current stream name buffer
extern int   DAT_805a0804;   // scratch write base for the active stream
extern int   DAT_805a0808;   // bytes consumed so far
extern int   DAT_805a080c;   // total stream length
extern int   DAT_805a0810;   // bind-slot index (or -1)
extern int   DAT_805a0814;   // active device handle (or -1)
extern int   DAT_805a0818;   // abort/flush flag
extern int   DAT_805a081c;   // active request-ring index
extern int   DAT_805a0820;   // async request block (0x20 bytes)
extern int   DAT_805a0840;   // request-block completion tag ('TVTX' marker)
extern int   DAT_805a0844;   // request-ring "in-use" byte table (stride 1)
extern int   DAT_805a0b00;   // request-ring secondary byte table (stride 1)
extern int   DAT_805a0dbc;   // request-ring: state word (stride 0x88, 0x32 entries)
extern int   DAT_805a0dc0;   // request-ring: name buffer  (stride 0x88)
extern int   DAT_805a0e40;   // request-ring: key/id       (stride 0x88)
extern int   DAT_8059e780;   // chunk staging buffer (0x2000 bytes)

// ---- voice / slot tables shared with the audio mixer (batch_476) -------------
extern unsigned char *DAT_80599300;   // voice owner/descriptor ptr (stride 6 words)
extern int            DAT_80599304;   // voice buffer-addr          (stride 0x18)
extern int            DAT_80599308;   // voice staging-addr         (stride 0x18)
extern short          DAT_80599310;   // voice size-hi              (stride 0xc)
extern short          DAT_80599312;   // voice size-lo              (stride 0x18)
extern unsigned char  DAT_80599314;   // voice flags                (stride 0xc)

// ---- misc globals ------------------------------------------------------------
extern int   DAT_80611858;   // outstanding-open counter
extern int   DAT_80610abc;   // stream device id
extern int   DAT_80612428;   // streaming scratch write cursor (shared w/ batch_476)
extern int   DAT_80612440;   // low water-mark of the active id window
extern int   DAT_80612444;   // saved scratch base
extern int   DAT_80612448;   // stream-instance id counter (shared)
extern int   DAT_8059930c;   // voice aux word (stride 0x18)
extern int  *DAT_80610a20;   // pointer to the streamed-slot descriptor array (0x58 stride)
extern float FLOAT_80616760; // GX lod bias low
extern float FLOAT_80616764; // GX detail threshold

extern int   s_Invalid_8041e740;   // "Invalid" name sentinel

// -----------------------------------------------------------------------------
// stream_tex_state_reset (0x801851b0)
// Snapshot the current scratch cursor and instance-id window as the new base for
// the next stream, and clear the busy/abort flag.
// -----------------------------------------------------------------------------
void stream_tex_state_reset(void)
{
    DAT_80612444 = DAT_80612428;
    DAT_805a0804 = DAT_80612428;
    DAT_80612440 = DAT_80612448;
    DAT_805a0818 = 0;
}

// -----------------------------------------------------------------------------
// stream_tex_flush_all (0x801851d8)
// Force the state machine to drain (pump until idle), then tear down every
// request-ring slot allocated in the current id window: clear the in-use bytes,
// zero the voice-table rows, reset the stream name to "Invalid", and wipe the
// 0x1a90-byte per-stream work area.  Called by level_actor_load_state_machine.
//
// The register-pair frame arguments feed straight back into the pump; they are
// captured explicitly as parameters (matching the __save_gpr_25 spill) rather
// than left as implicit register reads.
// -----------------------------------------------------------------------------
void stream_tex_flush_all(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                          unsigned long long f4, unsigned long long f5, unsigned long long f6,
                          unsigned long long f7, unsigned long long f8)
{
    DAT_805a0818 = 1;
    while (DAT_805a0780 != 0) {
        stream_tex_pump_state_machine(f1, f2, f3, f4, f5, f6, f7, f8);
    }

    unsigned char *inUseA = (unsigned char *)(&DAT_805a0844) + DAT_80612440;
    unsigned char *inUseB = (unsigned char *)(&DAT_805a0b00) + DAT_80612440;
    int vrec = DAT_80612440 * 0x18;

    for (int i = DAT_80612440; i < DAT_80612448; i++) {
        stream_voice_evict(i);   // FUN_80185c38 (batch_478)
        *inUseA = 0;
        *(int *)((int)&DAT_80599300 + vrec) = 0;
        inUseA++;
        *(int *)((int)&DAT_8059930c + vrec) = 0;
        *(int *)(&DAT_80599308 + vrec) = 0;
        *(int *)(&DAT_80599304 + vrec) = 0;
        *(short *)((int)&DAT_80599314 + vrec) = 0;
        *(short *)(&DAT_80599312 + vrec) = 0;
        *inUseB = 0;
        inUseB++;
        *(short *)((int)&DAT_80599310 + vrec) = 0;
        vrec += 0x18;
    }

    FUN_8038b510(&DAT_805a0784, &s_Invalid_8041e740);
    DAT_805a0818 = 0;
    DAT_805a0804 = DAT_80612444;
    DAT_805a0808 = 0;
    DAT_805a080c = 0;
    DAT_805a0810 = 0;
    DAT_805a0814 = -1;
    DAT_80612448 = DAT_80612440;
    memset_wrapper(-0x7fa5f244, 0, 0x1a90);
}

// -----------------------------------------------------------------------------
// stream_tex_request_slot_insert (0x801852f0)
// Look for an existing request-ring slot (state 1 or 2) whose key matches `key`;
// if none is found within the 0x32 entries, allocate the first free slot (state
// 0 -> 1), copy the name, and record the key.  stride 0x88.  Called by
// render_entity_resolve_texture_from_pack.
// -----------------------------------------------------------------------------
void stream_tex_request_slot_insert(int *name, int key)
{
    int off = 0;
    int n = 0x32;
    while (((*(int *)((int)&DAT_805a0dbc + off) != 1) &&
            (*(int *)((int)&DAT_805a0dbc + off) != 2)) ||
           (key != *(int *)((int)&DAT_805a0e40 + off))) {
        off += 0x88;
        n -= 1;
        if (n == 0) {
            off = 0;
            n = 0x32;
            do {
                if (*(int *)((int)&DAT_805a0dbc + off) == 0) {
                    *(int *)((int)&DAT_805a0dbc + off) = 1;
                    FUN_8038b510((int *)(&DAT_805a0dc0 + off), name);
                    *(int *)((int)&DAT_805a0e40 + off) = key;
                    return;
                }
                off += 0x88;
                n -= 1;
            } while (n != 0);
            return;
        }
    }
}

// -----------------------------------------------------------------------------
// stream_tex_poll_asset_size (0x801853b4)
// Thin wrapper around asset_get_size for a request-ring slot's name; always
// returns 1 (the caller distinguishes "not ready yet" via asset_get_size's own
// out-param).
// -----------------------------------------------------------------------------
int stream_tex_poll_asset_size(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                               unsigned long long f4, unsigned long long f5, unsigned long long f6,
                               unsigned long long f7, unsigned long long f8, unsigned char *nameSlot)
{
    // The ROM frame threads through to the pack asset-size query; the canonical
    // asset_get_size(const char*) takes the resolved slot name.
    (void)f1; (void)f2; (void)f3; (void)f4; (void)f5; (void)f6; (void)f7; (void)f8;
    asset_get_size((const char *)nameSlot);
    return 1;
}

// -----------------------------------------------------------------------------
// stream_tex_pump_state_machine (0x801853d8)
// The incremental texture-streaming state machine (state in DAT_805a0780):
//   0 -> scan the request ring for a slot to start (poll asset size); on success
//        move to 1, else free the slot.
//   1 -> queue a 0x2000-byte device read into the chunk staging buffer; on
//        success -> 2.
//   2 -> when the read is ready, DCStore the chunk into the scratch region and
//        issue the async 'TVTX' device transfer; -> 3.
//   3 -> on transfer completion advance the consumed counter; loop to 1 for the
//        next chunk, or finish: close the handle, publish the bind slot, and
//        advance the scratch base.
// Self-recursive via stream_tex_flush_all; also called by FUN_80186638.
// -----------------------------------------------------------------------------
void stream_tex_pump_state_machine(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                                   unsigned long long f4, unsigned long long f5, unsigned long long f6,
                                   unsigned long long f7, unsigned long long f8)
{
    unsigned long long frame = FRAME_PAIR_E();

    if (DAT_805a0780 == 2) {
        int ready = FUN_80187544(DAT_805a0814);
        if (ready != 0) {
            unsigned int chunk = 0x2000;
            if (DAT_805a080c < DAT_805a0808 + 0x2000) {
                chunk = (DAT_805a080c - DAT_805a0808) + 0x1f & 0xffffffe0;
            }
            if (DAT_805a0818 == 0) {
                if ((DAT_805a0810 < 0) && (DAT_805a0808 < 0x2000)) {
                    leaf_31e8(0x805a284c, 0x8059e780, 0x448);
                }
                FUN_803994d0(0x8059e780, chunk);
                DAT_805a0840 = 0x53565458;   // 'SVTX' completion marker
                FUN_803abe54((void *)&DAT_805a0820, 0x53565458, 0, 1, &DAT_8059e780,
                             DAT_805a0804 + (DAT_805a0808 - 0x20), chunk, -0x7fe7afe4);
                DAT_805a0780 = 3;
            }
            else {
                DAT_805a0780 = 3;
            }
        }
    }
    else if (DAT_805a0780 < 2) {
        if (DAT_805a0780 == 0) {
            int off = 0;
            int idx = 0;
            do {
                int *state = (int *)((int)&DAT_805a0dbc + off);
                if (*state == 1) {
                    int rc = stream_tex_poll_asset_size(frame, f2, f3, f4, f5, f6, f7, f8,
                                                        (unsigned char *)(&DAT_805a0dc0 + off));
                    if (rc == 0) {
                        *state = 2;
                        DAT_805a081c = idx;
                        break;
                    }
                    if (0 < DAT_80611858) {
                        DAT_80611858 = DAT_80611858 + -1;
                    }
                    *state = 0;
                }
                idx += 1;
                off += 0x88;
            } while (idx < 0x32);
        }
        else if (-1 < DAT_805a0780) {   // state 1: queue the device read
            FUN_803994a4(0x8059e780, 0x2000);
            int rc = dvd_read_wrapper_7414(frame, f2, f3, f4, f5, f6, f7, f8, DAT_805a0814,
                                           &DAT_8059e780, DAT_805a0808, 0x2000, 0, 0, 0, 0);
            if (0 < rc) {
                DAT_805a0780 = 2;
            }
        }
    }
    else if ((DAT_805a0780 < 4) && (DAT_805a0840 == 0)) {   // state 3: chunk done
        DAT_805a0808 = DAT_805a0808 + 0x2000;
        if ((DAT_805a0808 < DAT_805a080c) && (DAT_805a0818 == 0)) {
            DAT_805a0780 = 1;
        }
        else {
            FUN_80187260(DAT_805a0814);
            DAT_805a0814 = -1;
            int notAborted = (DAT_805a0818 == 0);
            DAT_805a0780 = 0;
            (&DAT_805a0dbc)[DAT_805a081c * 0x22] = 0;
            if (notAborted && (-1 < DAT_805a0810)) {
                stream_tex_bind_slot(DAT_805a0810);
            }
            DAT_805a0804 = DAT_805a0804 + (DAT_805a080c - 0x20);
        }
    }

    FUN_80382734();
}

// -----------------------------------------------------------------------------
// stream_tex_compute_gx_texobj (0x80185680)
// Build up to two GX texture objects from a texture descriptor: the full-res
// object (written through *outMain) and a half-res object with the dimensions
// masked to 15 bits and doubled (written through the frame's low-word pointer).
// -----------------------------------------------------------------------------
void stream_tex_compute_gx_texobj(unsigned int a1, unsigned int a2, int *outMain)
{
    unsigned long long frame = FRAME_PAIR_E();
    int desc = (int)(frame >> 0x20);

    double detail = FUN_803c90d8(desc);
    unsigned int w   = FUN_803c904c(desc);
    unsigned int h   = FUN_803c905c(desc);
    int fmt          = FUN_803c906c(desc);

    if (outMain != (int *)0) {
        int obj = FUN_803c8960(w, h, fmt, '\x01', (unsigned int)(int)detail);
        *outMain = obj;
    }
    int lowPtr = (int)frame;
    if (lowPtr != 0) {
        int obj = FUN_803c8960((w & 0x7fff) << 1, (h & 0x7fff) << 1, fmt, '\0', 0);
        *(int *)lowPtr = obj;
    }
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// stream_tex_bind_slot (0x80185744)
// Bind one streamed slot: double the four packed dimension shorts in the slot
// descriptor (0x58 stride, base *DAT_80610a20), compute its GX object footprint,
// and — if it fits in the remaining stream length — stage it (once, or twice for
// dual-texture slots selected by flag byte 0x4a bits in mask 0x610).
// -----------------------------------------------------------------------------
void stream_tex_bind_slot(int slot)
{
    unsigned int sizeHalf = 0;
    int sizeMain[3];

    int rec = *DAT_80610a20 + slot * 0x58;
    *(short *)(rec + 0x4c) = *(short *)(rec + 0x4c) << 1;
    *(short *)(rec + 0x50) = *(short *)(rec + 0x50) << 1;
    *(short *)(rec + 0x4e) = *(short *)(rec + 0x4e) << 1;
    *(short *)(rec + 0x52) = *(short *)(rec + 0x52) << 1;

    if ((1 << (unsigned int)*(unsigned char *)(rec + 0x4a) & 0x610U) != 0) {
        stream_tex_compute_gx_texobj(rec + 0x20, (unsigned int)(int *)&sizeHalf, (int *)0);
    }
    stream_tex_compute_gx_texobj(rec, (unsigned int)(int *)sizeMain, (int *)0);
    sizeHalf = sizeHalf + sizeMain[0];

    if ((sizeHalf <= (unsigned int)DAT_805a080c) &&
        (stream_tex_stage_upload(),
         (1 << (unsigned int)*(unsigned char *)(rec + 0x4a) & 0x610U) != 0)) {
        stream_tex_stage_upload();
    }
}

// -----------------------------------------------------------------------------
// stream_tex_stage_upload (0x80185844)
// Stage the active texture descriptor's data into the streaming scratch region:
// if its detail scalar exceeds the threshold, allocate a stream instance, fill
// the voice-table row (staging addr, size, owner), build the GX descriptor via
// FUN_803c8b84, set its LOD window via FUN_803c8e18, and commit it.  The paired
// register/paired-single reads carry the descriptor pointer and are captured via
// the frame pair.
// -----------------------------------------------------------------------------
void stream_tex_stage_upload(void)
{
    unsigned long long frame = FRAME_PAIR_D();
    unsigned int *desc = (unsigned int *)(frame >> 0x20);

    double detail = FUN_803c90d8((int)desc);
    if ((double)FLOAT_80616764 < detail) {
        int instId = stream_instance_id_alloc();
        int vrec = instId * 0x18;

        unsigned int w   = FUN_803c904c((int)desc);
        unsigned int h   = FUN_803c905c((int)desc);
        unsigned int fmt = FUN_803c906c((int)desc);
        unsigned int wrapS = FUN_803c9074((int)desc);
        unsigned int wrapT = FUN_803c9080((int)desc);

        unsigned int sizeMain[15];
        unsigned int sizeHalf;
        stream_tex_compute_gx_texobj((unsigned int)desc, (unsigned int)sizeMain,
                                     (int *)&sizeHalf);

        *(int *)(&DAT_80599304 + vrec) = (int)frame;
        (&DAT_80599314)[instId * 0xc] = 0x20;
        int srcData = FUN_803c9040((int)desc);
        *(int *)(&DAT_80599308 + vrec) = srcData + -0x80000000;
        *(short *)(&DAT_80599312 + vrec) = (short)(sizeHalf >> 5);
        (&DAT_80599310)[instId * 0xc] = (short)(sizeMain[0] >> 5);
        (&DAT_80599300)[instId * 6] = (unsigned char *)desc;

        FUN_803c8b84(desc, *(int *)(&DAT_80599308 + vrec) - (sizeMain[0] & 0xfffffffc),
                     (w & 0x7fff) << 1, (h & 0x7fff) << 1, fmt, wrapS, wrapT, '\x01');
        FUN_803c8e18((double)FLOAT_80616760,
                     (double)(float)((double)FLOAT_80616760 + detail),
                     (double)FLOAT_80616764, desc, 5, 1, 0, '\x01', 2);
        FUN_803c9030((int)desc, instId);
    }
    FUN_80382724();
}

// -----------------------------------------------------------------------------
// stream_tex_nop (0x801859a4)
// Empty callback hook.
// -----------------------------------------------------------------------------
void stream_tex_nop(void)
{
}

// -----------------------------------------------------------------------------
// stream_tex_dsp_dma_kick (0x801859a8)
// Publish a staged slot: claim a DSP command slot tagged with the stream's GX
// object id + 'TEXX' base, flush the staging span, and issue the async device
// transfer that pushes the scratch data out to the DSP/AI ring.  Called by
// FUN_80186638.
// -----------------------------------------------------------------------------
void stream_tex_dsp_dma_kick(void)
{
    unsigned long long frame = FRAME_PAIR_E();
    int gxObj = (int)(frame >> 0x20);
    unsigned int devId = (unsigned int)frame;

    unsigned int *cmd = (unsigned int *)dsp_cmd_slot_alloc(devId);
    FUN_803994a4(devId, (unsigned int)(unsigned short)(&DAT_80599310)[gxObj * 0xc] << 5);
    FUN_803abe54(cmd, gxObj + 0x56544558, 1, (int)DAT_80610abc,
                 (void *)*(unsigned int *)(&DAT_80599304 + gxObj * 0x18),
                 devId,
                 (int)((unsigned int)(unsigned short)(&DAT_80599310)[gxObj * 0xc] << 5),
                 -0x7fe7afd0);
    FUN_80382734();
}
