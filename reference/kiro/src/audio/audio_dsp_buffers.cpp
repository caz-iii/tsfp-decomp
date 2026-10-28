// ============================================================================
// Address range: 0x80184d80 - 0x801851af | batch_476 | Date: 2026-09-06
// Subsystem: audio (DSP mixer bring-up + streaming scratch-buffer / command ring).
// tag audio-dsp.
//
// The audio/DSP mixer's low-level state layer: the linear->centibel volume
// conversion, the voice-state predicate, the shared streaming scratch buffer
// (carved from the occlusion pool), the mixer reset/enable, the DSP command-slot
// round-robin, and the stream-instance id counter.  The same scratch buffer +
// command-slot machinery is reused by the texture-streaming pipeline
// (render_target_build_full / render_target_pool_alloc call in here), so the
// final staged-load path bridges audio and the graphics streamer.
//
//   audio_linear_to_centibels  0x80184d80  linear amplitude -> centibel attenuation [-904,0]
//   audio_voice_is_active       0x80184e08  voice flags bit2(playing)/bit0x40(paused) test
//   audio_scratch_reserve       0x80184e3c  reserve the streaming scratch region
//   audio_system_reset          0x80184e78  clear all mixer state + DSP work areas
//   audio_set_enabled           0x80184f58  set the audio-enabled flag
//   audio_hw_regs_clear         0x80184f6c  one-time zero of the 0x2d0 DSP register block
//   audio_wait_voices_idle      0x80184f9c  spin until all 0x14 DSP voice slots idle
//   dsp_cmd_slot_alloc          0x80184fd0  claim the next round-robin DSP command slot
//   stream_instance_id_alloc    0x80185074  monotonic stream-instance id counter
//   stream_texture_staged_load  0x80185088  staged device read into the scratch buffer + GX bind
//
// Structurally-faithful non-matching port.  The int->double magic-bias dequant
// idiom (0x43300000 / ^0x80000000) in the volume conversion is preserved; the
// voice/slot tables are expressed as element-base DAT_ symbols indexed by the
// ROM strides (0xc / 0x24 / 0x18 / 6).

#include "common.h"
#include "generated_decls.h"

// ---- math / runtime helpers --------------------------------------------------
extern double FUN_80390b24(double x);        // log()
extern void   FUN_803994d0(unsigned int addr, int len);   // DCFlushRange (partial)
extern void   FUN_803abe54(void *reqBlock, int cmd, int mode, int prio,
                           unsigned int devOffset, unsigned int memAddr,
                           int len, int callback);          // async device xfer
extern void   FUN_803c8f7c(int gxObj, int dataPtr);          // GX bind tex data ptr
extern void   FUN_803c9030(int gxObj, int slot);             // GX invalidate/commit tex

// occ_scratch_alloc / mem_alloc_aligned are declared void-returning in
// generated_decls but the ROM consumes their r3 (allocated address).  Call
// through correctly-typed function pointers to recover the return.
typedef int (*OccScratchAllocFn)(int size, int commit);
typedef int (*MemAllocAlignedFn)(int alignment, int size, int allocType);
#define OCC_SCRATCH_ALLOC ((OccScratchAllocFn)occ_scratch_alloc)
#define MEM_ALLOC_ALIGNED ((MemAllocAlignedFn)mem_alloc_aligned)
// audio_hw_regs_clear / audio_wait_voices_idle / dsp_cmd_slot_alloc /
// stream_instance_id_alloc resolve through generated_decls.h.

// ---- spawn-frame register helpers --------------------------------------------
extern unsigned long long FUN_803826dc(void);
extern void               FUN_80382728(void);

// ---- mixer / DSP state globals -----------------------------------------------
extern int            DAT_80612420;   // scratch buffer base
extern int            DAT_80612424;
extern int            DAT_80612428;   // scratch buffer write cursor
extern int            DAT_8061242c;   // scratch buffer size
extern int            DAT_80612430;
extern int            DAT_80612434;
extern int            DAT_80612438;
extern short          DAT_8061243c;
extern short          DAT_8061243e;
extern int            DAT_8061244c;
extern int            DAT_80612448;   // stream-instance id counter
extern unsigned char  DAT_80612450;   // DSP command-slot cursor (mod 0x14)
extern int            DAT_80612458;
extern unsigned char  DAT_80612452;   // hw-init-done latch
extern int            DAT_80612464;
extern int            DAT_80612468;
extern int            DAT_8061246c;
extern int            DAT_80612470;
extern int            DAT_80610abc;   // stream device id
extern int            DAT_80610ab8;   // audio-enabled flag
extern float          FLOAT_80610ab4; // master volume

// ---- voice / slot tables (element base + ROM stride) -------------------------
extern unsigned char  DAT_80599300;   // voice owner table    (stride 6 words)
extern int            DAT_80599304;   // voice buffer-addr    (stride 0x18)
extern int            DAT_80599308;   // voice staging-addr   (stride 0x18)
extern short          DAT_80599310;   // voice size-hi        (stride 0xc)
extern short          DAT_80599312;   // voice size-lo        (stride 0x18)
extern unsigned char  DAT_80599314;   // voice flags          (stride 0xc)
extern int            DAT_8059d4a2;   // voice-id sentinel table (0x400 entries, stride 4)
extern unsigned int   DAT_8059e4a0;   // DSP command block base  (stride 0x24)
extern int            DAT_8059e4c0;   // DSP command tag         (stride 0x24 = 9 words)

// ---- rodata constants for the volume conversion ------------------------------
extern float  FLOAT_80616740;   // divisor (dynamic range)
extern float  FLOAT_80616750;   // silence threshold
extern float  FLOAT_80616760;   // default master volume
extern double DOUBLE_80616748;  // centibel scale
extern double DOUBLE_80616758;  // magic-bias 2^52 offset

// -----------------------------------------------------------------------------
// audio_linear_to_centibels (0x80184d80)
// Convert a linear amplitude (unsigned) to a centibel attenuation value.  Below
// the silence threshold it returns the floor (-0x388 = -904, ~full attenuation);
// otherwise it takes log() of the normalized amplitude and scales.  Result is
// clamped to [-0x388, 0].  12 xrefs across the mixer.
// -----------------------------------------------------------------------------
int audio_linear_to_centibels(unsigned int linear)
{
    // int->double magic-bias dequant of the (unsigned) amplitude.
    double norm = (double)((float)((double)CONCAT44(0x43300000, linear ^ 0x80000000)
                                   - DOUBLE_80616758) / FLOAT_80616740);
    int cb;
    if (norm <= (double)FLOAT_80616750) {
        cb = -0x388;
    }
    else {
        norm = FUN_80390b24(norm);           // log()
        cb = (int)(DOUBLE_80616748 * norm);
    }
    if (cb < -0x388) {
        cb = -0x388;
    }
    if (0 < cb) {
        cb = 0;
    }
    return cb;
}

// -----------------------------------------------------------------------------
// audio_voice_is_active (0x80184e08)
// Return 1 if voice `v` is playing (flag bit 2) or paused (flag bit 0x40).
// -----------------------------------------------------------------------------
int audio_voice_is_active(int v)
{
    if ((((&DAT_80599314)[v * 0xc] & 2) == 0) &&
        (((&DAT_80599314)[v * 0xc] & 0x40) == 0)) {
        return 0;
    }
    return 1;
}

// -----------------------------------------------------------------------------
// audio_scratch_reserve (0x80184e3c)
// Reserve the streaming scratch region: take (occlusion-pool scratch remaining -
// 0x66800) bytes and allocate them; store base + write cursor.
// -----------------------------------------------------------------------------
void audio_scratch_reserve(void)
{
    int remaining = occ_pool_scratch_remaining();
    DAT_8061242c = remaining + -0x66800;
    DAT_80612420 = OCC_SCRATCH_ALLOC(DAT_8061242c, 0);
    DAT_80612428 = DAT_80612420;
}

// -----------------------------------------------------------------------------
// audio_system_reset (0x80184e78)
// Reset the whole mixer: one-time hw-register clear, wait for voices idle, zero
// all scratch/cursor state, clear the DSP work areas (0x1000 + 0x41a0), fill the
// 0x400-entry voice-id table with 0xffff sentinels, and set the default master
// volume.  Called from render_target_pool_alloc (shared streaming scratch).
// -----------------------------------------------------------------------------
void audio_system_reset(void)
{
    if (DAT_80612452 == '\0') {
        audio_hw_regs_clear();
        DAT_80612452 = '\x01';
    }
    audio_wait_voices_idle();

    DAT_8061242c = 0;
    DAT_80612420 = 0;
    DAT_80612428 = 0;
    DAT_80612430 = 0;
    DAT_80612424 = 0;
    DAT_80612434 = 0;
    DAT_8061244c = 0;
    DAT_8061243c = 0;
    DAT_80612438 = 0;
    DAT_8061243e = 0;
    memset_wrapper(-0x7fa62b60, 0, 0x1000);      // DSP work area A

    int off = 0;
    for (int n = 0x400; n != 0; n--) {           // voice-id sentinel table
        *(unsigned short *)((int)&DAT_8059d4a2 + off) = 0xffff;
        off += 4;
    }

    DAT_80612448 = 0;
    memset_wrapper(-0x7fa66d00, 0, 0x41a0);      // DSP work area B (voice mix state)
    DAT_80612470 = 0;
    FLOAT_80610ab4 = FLOAT_80616760;             // master volume default
    DAT_8061246c = 0;
    DAT_80612468 = 0;
    DAT_80612464 = 0;
    DAT_80610ab8 = 1;                            // audio enabled
}

// -----------------------------------------------------------------------------
// audio_set_enabled (0x80184f58)
// Clear the pending-command marker and (re)enable audio processing.
// -----------------------------------------------------------------------------
void audio_set_enabled(void)
{
    DAT_80612458 = 0;
    DAT_80610ab8 = 1;
}

// -----------------------------------------------------------------------------
// audio_hw_regs_clear (0x80184f6c)
// One-time zero of the 0x2d0-byte DSP hardware-register shadow block.
// -----------------------------------------------------------------------------
void audio_hw_regs_clear(void)
{
    memset_wrapper(-0x7fa61b60, 0, 0x2d0);
}

// -----------------------------------------------------------------------------
// audio_wait_voices_idle (0x80184f9c)
// Spin until all 0x14 DSP voice command slots (DAT_8059e4c0, stride 0x24) have
// drained (tag word cleared).
// -----------------------------------------------------------------------------
void audio_wait_voices_idle(void)
{
    int off = 0;
    int n = 0x14;
    do {
        do {
        } while (*(int *)((int)&DAT_8059e4c0 + off) != 0);
        off += 0x24;
        n -= 1;
    } while (n != 0);
}

// -----------------------------------------------------------------------------
// dsp_cmd_slot_alloc (0x80184fd0)
// Claim the next round-robin DSP command slot (0x14 slots).  Waits for the slot
// to be free, stamps it with `tag`, advances the cursor, and returns a pointer
// to the slot's 0x24-byte command block.
// -----------------------------------------------------------------------------
unsigned char *dsp_cmd_slot_alloc(unsigned int tag)
{
    unsigned int slot = (unsigned int)DAT_80612450;
    do {
    } while ((&DAT_8059e4c0)[slot * 9] != 0);
    (&DAT_8059e4c0)[slot * 9] = tag;
    DAT_80612450 = DAT_80612450 + 1;
    if (DAT_80612450 == '\x14') {
        DAT_80612450 = '\0';
    }
    return (unsigned char *)(&DAT_8059e4a0 + slot * 0x24);
}

// -----------------------------------------------------------------------------
// stream_instance_id_alloc (0x80185074)
// Monotonic stream-instance / voice id counter (post-increment).
// -----------------------------------------------------------------------------
int stream_instance_id_alloc(void)
{
    int id = DAT_80612448;
    DAT_80612448 = DAT_80612448 + 1;
    return id;
}

// -----------------------------------------------------------------------------
// stream_texture_staged_load (0x80185088)
// Staged streaming load through the audio scratch buffer: allocate a stream
// instance + a DSP command slot, allocate an aligned staging buffer, DMA-upload
// it (occ_dma_upload), issue a device read (cmd 'TEXT' 0x56544558) into the
// scratch cursor, then bind the resulting data to a GX texture object.  Called by
// render_target_build_full for large streamed textures.  The GX descriptor/dims
// arrive in the spawn-frame pair.
// -----------------------------------------------------------------------------
void stream_texture_staged_load(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                                unsigned long long f4, unsigned long long f5, unsigned long long f6,
                                unsigned long long f7, unsigned long long f8,
                                unsigned int a9, unsigned int a10, unsigned int totalLen,
                                unsigned int dataLen, int a13, unsigned int a14,
                                unsigned int a15, unsigned int a16)
{
    unsigned long long frame = FUN_803826dc();
    int gxObj = (int)(frame >> 0x20);
    unsigned int devOffset = (unsigned int)frame;

    int instId = stream_instance_id_alloc();
    int vrec = instId * 0x18;
    unsigned int *cmd = (unsigned int *)dsp_cmd_slot_alloc(DAT_80612428);

    // staging buffer (0x20-aligned, alloc-type 1) sized by dataLen.
    int staging = MEM_ALLOC_ALIGNED(0x20, dataLen, 1);
    *(int *)(&DAT_80599308 + vrec) = staging;

    FUN_803994d0(devOffset, dataLen + totalLen);       // flush header+data span
    unsigned int alignedData = totalLen & 0xfffffffc;
    occ_dma_upload(*(int *)(&DAT_80599308 + vrec), devOffset + alignedData, dataLen);
    *(short *)(&DAT_80599312 + vrec) = (short)(dataLen >> 5);

    FUN_803994d0(devOffset, totalLen);
    FUN_803abe54(cmd, 0x56544558, 0, DAT_80610abc, devOffset, DAT_80612428, totalLen, -0x7fe7afe4);
    do {
    } while (cmd[8] != 0);                              // wait for the read to complete

    *(int *)(&DAT_80599304 + vrec) = DAT_80612428;
    DAT_80612428 = DAT_80612428 + alignedData;          // advance scratch cursor
    (&DAT_80599310)[instId * 0xc] = (short)(totalLen >> 5);
    (&DAT_80599314)[instId * 0xc] = 0;
    (&DAT_80599300)[instId * 6] = (unsigned char)gxObj;

    FUN_803c8f7c(gxObj, *(int *)(&DAT_80599308 + vrec) - alignedData);
    FUN_803c9030(gxObj, instId);
    FUN_80382728();
}
