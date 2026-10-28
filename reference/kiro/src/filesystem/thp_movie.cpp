// thp_movie.cpp
// THP movie (Dolphin SDK "THP" format, .thp) streaming/playback front-end.
// Opens a movie file from DVD, parses the THP header (magic 0x11000: frame
// dimensions, component list, per-frame sizes), partitions a work buffer into a
// 10-deep ring of decoded-frame buffers (+3 audio buffers when the movie has an
// audio component), and drives async DVD reads that fill the ring. The global
// THP state block lives at DAT_805ace40 (active flag) .. DAT_805acf24.
//
// Address range: 0x8018fbb0 - 0x80190630
// Batch: batch_498
#include "common.h"

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
// --- GX render-state guard (thp_texture_state_guard) ---
extern int  gx_set_sub_material_id(int id);   // returns previous id
extern int  gx_set_blend_mode(int mode);      // returns previous mode
extern void stream_tex_nop_b(void);

// --- Open / decoder setup ---
extern int  memset_wrapper(int dst, int val, int len);   // returns dst
extern int  FUN_803c37f0(unsigned long long a1, double a2, double a3, unsigned long long a4,
                         unsigned long long a5, unsigned long long a6, unsigned long long a7,
                         unsigned long long a8, int workBuf, int a10, int workLen,
                         int a12, int a13, int a14, int a15, int a16);   // THP decoder open
extern void FUN_8039cca0(void);                 // enter critical section / disable irq
extern void FUN_8039ccc8(void);                 // leave critical section / enable irq
extern int  FUN_803a983c(int callback);         // install VI/retrace callback, ret previous
extern void FUN_803994d0(int buf, int len);     // DCInvalidateRange-style
extern void FUN_803a9880(int buf, int len);     // register audio buffer
extern void FUN_803a9908(void);                 // kick audio playback

// --- DVD I/O ---
// NOTE: DVDOpen is declared canonically in sdk_stubs.h (BOOL DVDOpen(const char*,
// DVDFileInfo)); we call that shared stub rather than the raw 16-arg register form.
extern int  dvd_async_read_6cac(unsigned long long a1, double a2, double a3, unsigned long long a4,
                                unsigned long long a5, unsigned long long a6, unsigned long long a7,
                                unsigned long long a8, int fileInfo, int dst, int len, int offset,
                                unsigned int a13, int a14, int a15, int a16);
extern void dvd_read_stream_02e8(unsigned long long a1, double a2, double a3, unsigned long long a4,
                                 unsigned long long a5, unsigned long long a6, unsigned long long a7,
                                 unsigned long long a8);
// Raw register-form DVDReadAsync (the canonical typed DVDReadAsync lives in
// sdk_stubs.h; the streaming path calls the raw form with the DVD callback as an
// extra arg, so we use a file-local alias to avoid a signature clash).
extern int  dvd_read_async_raw(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                               unsigned long long a4, unsigned long long a5, unsigned long long a6,
                               unsigned long long a7, unsigned long long a8, unsigned int *fileInfo,
                               int dst, int len, int offset, void (*cb)(unsigned long long, double,
                               double, unsigned long long, unsigned long long, unsigned long long,
                               unsigned long long, unsigned long long, int), int prio,
                               int a15, int a16);
extern void FUN_803a2934(int *fileInfo);        // DVDClose
extern int  leaf_31e8(int dst, int src, int len);   // byteswap/copy header field, ret next src
extern int  FUN_8038b378(unsigned int *a, unsigned int *b);   // magic/tag compare
extern void FUN_803994a4(unsigned int addr, unsigned int len);  // range register / invalidate

// --- Buffer allocator ---
extern int  __save_gpr_25(void);
extern void __restore_gpr_25(void);
extern unsigned int FUN_803826e8(void);   // spill-frame accessor (returns flag/int)
extern void FUN_80382734(void);
extern unsigned int FUN_803826f0(void);   // spill-frame accessor (returns work-buffer base)
extern void FUN_8038273c(void);

// --- THP global state block ---
extern int  DAT_806124c8;   // decoder-ready flag (set by thp_open)
extern int  DAT_806124cc;   // saved retrace callback
extern int  DAT_806124d0;   // no-audio-callback mode flag
extern int  DAT_806124d4;
extern int  DAT_806124d8;
extern int  DAT_806124dc;   // audio buffer index
extern int  DAT_806124c0;   // saved blend mode (texture guard)
extern int  DAT_806124c4;   // saved sub-material id (texture guard)

extern int  DAT_805ace40;   // playback active flag
extern char DAT_805ace44;   // reads-queued flag
extern char DAT_805ace45;   // stop-requested flag
extern char DAT_805ace46;   // wrap-pending flag
extern char DAT_805ace47;   // has-audio flag
extern int  DAT_805ace48;   // current DVD read offset
extern int  DAT_805ace4c;   // error flag
extern int  DAT_805ace50;   // outstanding-read flag
extern int  DAT_805ace54;
extern int  DAT_805ace58;   // ring write index (0..9)
extern int  DAT_805ace5c;   // current frame size
extern int  DAT_805ace60;   // frame counter
extern int  DAT_805ace64;   // playback timer A (float bits)
extern int  DAT_805ace68;   // playback timer B (float bits)
extern int  DAT_805ace70;
extern int  DAT_805ace3c;   // running buffer cursor
extern int  DAT_805ace04;   // first-frame DVD offset
extern int  DAT_805ace20;   // frame width
extern int  DAT_805ace24;   // frame height
extern int  DAT_805acdf0;   // total frame count
extern int  DAT_805acdf4;   // first-frame size
extern int  DAT_805acde4;   // max compressed frame size
extern int  DAT_805acde8;   // audio samples per frame
extern int  DAT_805acde0;   // header magic (expect 0x11000)
extern int  DAT_805acdfc;   // header component-table offset
extern char DAT_805ace0c;   // component count
extern char DAT_805ace10;   // component-type table (byte per component)
extern int  DAT_805aceec;   // buffer ptr: Y plane
extern int  DAT_805acef0;   // buffer ptr: U plane
extern int  DAT_805acef4;   // buffer ptr: V plane
extern int  DAT_805acef8;
extern int  DAT_805acf20;
extern int  DAT_805acf24;
extern int  DAT_805ace74;   // ring: per-frame buffer ptr[0]
extern int  DAT_805ace78;   // ring: per-frame id[0]
extern int  DAT_805ace7c;   // ring: per-frame ready-flag[0]
extern int  DAT_805acefc;   // audio ring: ptr[0]
extern int  DAT_805acf00;   // audio ring: ptr copy[0]
extern int  DAT_805acf04;   // audio ring: flag[0]
extern int  DAT_805acda0;   // DVDFileInfo for the movie
extern int  DAT_805ad440;   // scratch header read buffer
extern int  DAT_805acddc;   // parsed header (magic at +0)
extern int  DAT_805ace0c_hdr;  // (alias kept distinct — see note)
extern int  DAT_80610ad0;   // expected THP magic/tag constant
extern int  DAT_805acf40;   // audio double-buffer base (0x500 bytes)

extern float FLOAT_806168e0;
extern void FUN_80190d90(void);   // per-retrace VI callback (installed by thp_open)
} // extern "C"

// Ring geometry constants (10 video frame buffers, 3 audio buffers).
#define THP_FRAME_RING   10
#define THP_AUDIO_RING   3

// ---------------------------------------------------------------------------
// thp_texture_state_guard (FUN_8018fbb0)
// Save sub-material id + blend mode, run the (no-op) texture-stream tick, then
// restore. Used to keep the movie blit from disturbing material state.
// ---------------------------------------------------------------------------
void thp_texture_state_guard(void) {
    DAT_806124c4 = gx_set_sub_material_id(1);
    DAT_806124c0 = gx_set_blend_mode(0);
    stream_tex_nop_b();
    gx_set_sub_material_id(DAT_806124c4);
    gx_set_blend_mode(DAT_806124c0);
}

// ---------------------------------------------------------------------------
// thp_stub_noop (FUN_8018fbf8)
// ---------------------------------------------------------------------------
void thp_stub_noop(void) {}

// ---------------------------------------------------------------------------
// thp_open (FUN_8018fbfc)
// Zero the 0x188-byte decoder work area, open the THP via FUN_803c37f0, and on
// success install the per-retrace VI callback (FUN_80190d90). When running with
// its own audio callback (noAudioCbMode == 0) it also clears the 0x500-byte
// audio double buffer and hands the first half to the audio HW.
// Returns 1 on success, 0 on failure. Caller: game_resource_loader_21bc.
// ---------------------------------------------------------------------------
unsigned int thp_open(unsigned long long a1, double a2, double a3, unsigned long long a4,
                      unsigned long long a5, unsigned long long a6, unsigned long long a7,
                      unsigned long long a8, int noAudioCbMode,
                      int a10, int a11, int a12, int a13, int a14, int a15, int a16) {
    (void)a10; (void)a11;
    int workBuf = memset_wrapper(-0x7fa53260, 0, 0x188);
    int ok = FUN_803c37f0(a1, a2, a3, a4, a5, a6, a7, a8, workBuf, 0, 0x188,
                          a12, a13, a14, a15, a16);
    if (ok == 0) {
        return 0;
    }

    FUN_8039cca0();
    DAT_806124dc = 0;
    DAT_806124d8 = 0;
    DAT_806124d4 = 0;
    DAT_806124d0 = noAudioCbMode;
    DAT_806124cc = FUN_803a983c((int)(unsigned int)(unsigned long)&FUN_80190d90);
    if (DAT_806124cc == 0 && DAT_806124d0 != 0) {
        FUN_803a983c(0);
        FUN_8039ccc8();
        return 0;
    }

    FUN_8039ccc8();
    if (DAT_806124d0 == 0) {
        memset_wrapper(-0x7fa530c0, 0, 0x500);
        FUN_803994d0(0x805acf40, 0x500);
        FUN_803a9880((int)(&DAT_805acf40 + DAT_806124dc * 0x280), 0x280);
        FUN_803a9908();
    }
    DAT_806124c8 = 1;
    return 1;
}

// ---------------------------------------------------------------------------
// thp_close (FUN_8018fd0c)
// Restore the saved retrace callback and clear the decoder-ready flag.
// ---------------------------------------------------------------------------
void thp_close(void) {
    FUN_8039cca0();
    if (DAT_806124cc != 0) {
        FUN_803a983c(DAT_806124cc);
    }
    FUN_8039ccc8();
    DAT_806124c8 = 0;
}

// ---------------------------------------------------------------------------
// thp_poll_finished (FUN_8019006c)
// Returns 1 (and tears down the DVD file) once playback is done: the movie must
// be active, not mid-read, and (if it has audio) not paused on the last frame,
// with no outstanding read. Returns 0 while still playing.
// ---------------------------------------------------------------------------
unsigned int thp_poll_finished(void) {
    if (DAT_805ace40 != 0 && DAT_805ace44 == '\0') {
        if (DAT_805ace47 == '\0') {
            DAT_805ace45 = '\0';
        } else if (DAT_805ace45 == '\x01') {
            return 0;
        }
        if (DAT_805ace50 == 0) {
            DAT_805ace40 = 0;
            FUN_803a2934(&DAT_805acda0);
            return 1;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// thp_compute_buffer_size (FUN_801900fc)
// Total work-buffer bytes needed: 10 * ceil32(maxFrameSize) for the frame ring,
// plus one ceil32(W*H) luma buffer and two ceil32(W*H/4) chroma buffers, plus
// (when audio present) 3 * ceil32(audioSamples*4), plus a 0x1000 slack page.
// Returns 0 if no movie is active.
// ---------------------------------------------------------------------------
int thp_compute_buffer_size(void) {
    if (DAT_805ace40 == 0) {
        return 0;
    }
    unsigned int wh    = (unsigned int)(DAT_805ace20 * DAT_805ace24);
    int size = ((unsigned int)(DAT_805acde4 + 0x1f) & 0xffffffe0) * THP_FRAME_RING
             + ((wh + 0x1f) & 0xffffffe0)
             + (((wh >> 2) + 0x1f) & 0xffffffe0) * 2;
    if (DAT_805ace47 != '\0') {
        size += ((unsigned int)(DAT_805acde8 * 4 + 0x1f) & 0xffffffe0) * THP_AUDIO_RING;
    }
    return size + 0x1000;
}

// ---------------------------------------------------------------------------
// thp_alloc_frame_buffers (FUN_80190180)
// Partition the work buffer (base from the spill frame) into: the three YUV
// plane buffers, the 10-entry frame ring (buffer ptr + ready flag per slot), and
// (when audio present) the 3-entry audio ring. Registers each plane range.
// ---------------------------------------------------------------------------
void thp_alloc_frame_buffers(void) {
    unsigned int cursor = FUN_803826f0();

    if (DAT_805ace40 != 0 && DAT_805ace44 == '\0' && DAT_805ace45 != '\x01') {
        unsigned int wh     = (unsigned int)(DAT_805ace20 * DAT_805ace24);
        unsigned int lumaSz = (wh + 0x1f) & 0xffffffe0;
        unsigned int chromaSz = ((wh >> 2) + 0x1f) & 0xffffffe0;

        DAT_805aceec = cursor;                  // Y
        FUN_803994a4(cursor, lumaSz);
        cursor += lumaSz;
        DAT_805acef0 = cursor;                  // U
        FUN_803994a4(cursor, chromaSz);
        cursor += chromaSz;
        DAT_805acef4 = cursor;                  // V
        FUN_803994a4(cursor, chromaSz);
        DAT_805ace3c = cursor + chromaSz;

        // 10-entry frame ring: {ptr @+0, id @+4, flag @+8}, stride 0xc.
        int step = (int)(((unsigned int)(DAT_805acde4 + 0x1f)) & 0xffffffe0);
        int off = 0;
        for (int i = 0; i < THP_FRAME_RING; ++i) {
            *(int *)((int)&DAT_805ace74 + off) = DAT_805ace3c;
            *(int *)((int)&DAT_805ace7c + off) = 0;
            DAT_805ace3c += step;
            off += 0xc;
        }

        // 3-entry audio ring when the movie carries an audio component.
        if (DAT_805ace47 != '\0') {
            int astep = (int)(((unsigned int)(DAT_805acde8 * 4 + 0x1f)) & 0xffffffe0);
            int aoff = 0;
            for (int i = 0; i < THP_AUDIO_RING; ++i) {
                *(int *)((int)&DAT_805acefc + aoff) = DAT_805ace3c;
                *(int *)((int)&DAT_805acf00 + aoff) = DAT_805ace3c;
                *(int *)((int)&DAT_805acf04 + aoff) = 0;
                DAT_805ace3c += astep;
                aoff += 0xc;
            }
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// thp_read_complete_cb (FUN_801903c8)
// DVD async-read completion callback. On success advance the ring write index
// (wrapping at 10), mark the slot ready, bump the read offset by the frame size,
// and if the next slot is free kick another read (dvd_read_stream_02e8).
// param_9: DVD result (-1 = error latch, -3 = cancelled/ignore).
// ---------------------------------------------------------------------------
void thp_read_complete_cb(unsigned long long a1, double a2, double a3, unsigned long long a4,
                          unsigned long long a5, unsigned long long a6, unsigned long long a7,
                          unsigned long long a8, int result) {
    if (result == -1) {
        DAT_805ace4c = 1;
        return;
    }
    if (result == -3) {
        return;
    }

    DAT_805ace50 = 0;
    *(int *)(&DAT_805ace78 + DAT_805ace58 * 0xc) = DAT_805ace60;
    DAT_805ace60 = DAT_805ace60 + 1;
    (&DAT_805ace7c)[DAT_805ace58 * 3] = 1;
    DAT_805ace48 = DAT_805ace48 + DAT_805ace5c;
    int slot = DAT_805ace58 * 3;
    // advance index, wrap 9 -> 0 (branchless form preserved)
    DAT_805ace58 = DAT_805ace58 + 1 & ~-(unsigned int)(DAT_805ace58 - 9 < 0x7ffffff6);
    DAT_805ace5c = *(int *)(&DAT_805ace74)[slot];
    if ((&DAT_805ace7c)[DAT_805ace58 * 3] == 0) {
        dvd_read_stream_02e8(a1, a2, a3, a4, a5, a6, a7, a8);
    }
}

// ---------------------------------------------------------------------------
// dvd_read_stream_02e8 (FUN_801902e8)
// Kick a single async DVD read for the next ring slot. No-op if the error flag
// is latched or reads aren't primed yet. At the last frame it either rewinds to
// the first frame (when looping) or bails. Sets the outstanding-read flag, issues
// the read into the current ring buffer with thp_read_complete_cb as completion;
// if the DVD call doesn't accept the request (rc != 1) it clears the outstanding
// flag and latches the error flag. Callers: thp_read_complete_cb, thp_decode_frame.
// ---------------------------------------------------------------------------
void dvd_read_stream_02e8(unsigned long long a1, double a2, double a3, unsigned long long a4,
                          unsigned long long a5, unsigned long long a6, unsigned long long a7,
                          unsigned long long a8) {
    if (DAT_805ace4c != 0 || DAT_805ace44 != '\x01') {
        return;
    }

    // At/past the final frame: rewind if looping, otherwise nothing to read.
    if ((unsigned int)(DAT_805acdf0 - 1) < (unsigned int)DAT_805ace60) {
        if (DAT_805ace46 != '\x01') {
            return;
        }
        DAT_805ace60 = 0;
        DAT_805ace48 = DAT_805ace04;
        DAT_805ace5c = DAT_805acdf4;
    }

    DAT_805ace50 = 1;
    int rc = dvd_read_async_raw(a1, (unsigned long long)a2, (unsigned long long)a3, a4, a5, a6, a7, a8,
                                (unsigned int *)&DAT_805acda0,
                                (&DAT_805ace74)[DAT_805ace58 * 3],
                                DAT_805ace5c, DAT_805ace48,
                                thp_read_complete_cb, 2, 0, 0);
    if (rc != 1) {
        DAT_805ace50 = 0;
        DAT_805ace4c = 1;
    }
}

// ---------------------------------------------------------------------------
// thp_queue_frame_reads (FUN_801904a8)
// Prime the ring: issue up to 10 async frame reads (or fewer near end-of-movie
// when not looping). Each read fills the next ring slot and advances the offset;
// on the loop point (last frame + loop flag) it rewinds to the first frame. Sets
// the reads-queued flag when done. A negative DVD result latches the error flag.
// ---------------------------------------------------------------------------
void thp_queue_frame_reads(unsigned long long a1, double a2, double a3, unsigned long long a4,
                           unsigned long long a5, unsigned long long a6, unsigned long long a7,
                           unsigned long long a8) {
    unsigned int loopFlag = FUN_803826e8();
    if (DAT_805ace40 != 0 && DAT_805ace44 == '\0') {
        unsigned int count = THP_FRAME_RING;
        if (loopFlag == 0 && (unsigned int)DAT_805acdf0 < (unsigned int)THP_FRAME_RING) {
            count = (unsigned int)DAT_805acdf0;
        }

        unsigned int prevOffset = 0;
        for (unsigned int i = 0; i < count; ++i) {
            int rc = dvd_async_read_6cac(a1, a2, a3, a4, a5, a6, a7, a8,
                                         (int)&DAT_805acda0,
                                         (&DAT_805ace74)[DAT_805ace58 * 3],
                                         DAT_805ace5c, DAT_805ace48,
                                         prevOffset, 0, 0, 0);
            prevOffset = (unsigned int)DAT_805ace48;
            if (rc < 0) {
                DAT_805ace4c = 1;
                goto done;
            }
            DAT_805ace48 = DAT_805ace48 + DAT_805ace5c;
            DAT_805ace5c = *(int *)(&DAT_805ace74)[DAT_805ace58 * 3];
            (&DAT_805ace7c)[DAT_805ace58 * 3] = 1;
            *(unsigned int *)(&DAT_805ace78 + DAT_805ace58 * 0xc) = DAT_805ace60;
            DAT_805ace60 = DAT_805ace60 + 1;
            DAT_805ace58 = DAT_805ace58 + 1 & ~-(unsigned int)(DAT_805ace58 - 9 < 0x7ffffff6);
            if ((unsigned int)(DAT_805acdf0 - 1) < (unsigned int)DAT_805ace60
                && DAT_805ace46 == '\x01') {
                DAT_805ace60 = 0;
                DAT_805ace48 = DAT_805ace04;
                DAT_805ace5c = DAT_805acdf4;
            }
        }
        DAT_805ace46 = (char)loopFlag;
        DAT_805ace44 = '\x01';
    }
done:
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// thp_request_stop (FUN_80190630)
// Request playback to stop at the next opportunity.
// ---------------------------------------------------------------------------
void thp_request_stop(void) {
    DAT_805ace45 = 1;
}

// ===========================================================================
// Batch: batch_499 - THP frame decode / audio mix / YUV->RGB blit.
// Appended: video decode of a queued frame, audio resample+mix into the VI
// double-buffer, dimension/rate/count getters, the VI-retrace audio callback,
// and the GX TEV setup that converts the decoded YUV planes to RGB on screen.
// Address range: 0x80190644 - 0x80191264
// ===========================================================================

extern "C" {
// --- Additional raw symbols for this group (per-file externs) ---
extern int  FUN_803bc328(int jpegSrc, int planeY, int planeU, int planeV, int scratch); // THP/JPEG decode -> YUV
extern int  FUN_803c3890(short *dst, int *adpcmSrc, int a3);   // THP audio (ADPCM) decode -> PCM
extern void FUN_8039ccb4(void);                                // audio critical-section helper
extern unsigned int FUN_803a9938(void);                        // AIGetDMAStartAddr-style query

// GX TEV / material helpers used by the YUV->RGB stage setup
extern void gx_cached_set_tev_order(unsigned int stage, int texMap, int texCoord, int channel);
extern void gx_cached_set_tev_color_op(unsigned int stage, int a2, int a3, int a4);
extern void gx_cached_set_tev_color_in(unsigned int stage, int a, int b, int c, int d);
extern void gx_cached_set_tev_color_env(unsigned int stage, int a, int b, int c, int d, int e);
extern void gx_cached_set_tev_alpha_in(unsigned int stage, int a, int b, int c, int d);
extern void gx_cached_set_tev_alpha_env(unsigned int stage, int a, int b, int c, int d, int e);
extern void FUN_803ca238(int slot, unsigned int *rgba);        // set TEV konst colour
extern void FUN_803ca29c(unsigned int stage, int kSel);        // konst colour selector
extern void FUN_803ca2f8(unsigned int stage, int kSel);        // konst alpha selector
extern void FUN_803ca354(unsigned int stage, int a2, int a3);  // tev swap-mode table sel
extern void FUN_803ca1d4(int slot, unsigned int *rgba);        // set konst colour (variant)
extern void FUN_803ca39c(int a1, int a2, int a3, int a4, int a5); // indirect/order commit
extern void FUN_803c8874(int mode);                            // vtx descriptor preset
extern void gx_config_vtx_format(int fmt);
extern void gx_set_blend_tag(int a1, int a2);
extern int  gx_set_material_override_cb(int cb);

extern void FUN_801912ac(int a1);                              // begin YUV blit primitive
extern void FUN_80191428(int planeY, int planeU, int planeV, int a4, short a5,
                         int w, int h, short a8, short a9);     // emit YUV quad
extern void FUN_80382734(void);
extern unsigned int FUN_803826e8(void);

// --- Additional THP state ---
extern int  DAT_805ace54;    // ring read (decode) index
extern int  DAT_805ace38;    // audio total-sample count guard
extern int  DAT_805acf20;    // audio ring write index (decode side)
extern int  DAT_805acf24;    // audio ring read index (playback side)
extern int  DAT_805ace6c;    // playback rate step
extern int  DAT_805acdec;    // frame-rate value (float bits)
extern int  DAT_806124d4;    // (already declared above) current audio src ptr
extern int  DAT_8041e8e8;    // volume-ramp lookup table (u16)
extern int  DAT_806168e8;    // YUV->RGB konst: coeff pair 0
extern int  DAT_806168ec;    // YUV->RGB konst: coeff pair 0 alpha
extern int  DAT_806168f0;    // YUV->RGB konst: coeff 1
extern int  DAT_806168f4;    // YUV->RGB konst: coeff 2
extern int  DAT_806168f8;    // YUV->RGB konst: coeff 3
extern float FLOAT_806168e4; // default frame rate when inactive
} // extern "C"

// Intra-file forward declarations (defined below in this TU).
static bool thp_decode_video(int jpegSrc);
void thp_audio_resample_mix(short *dst, short *src, unsigned int samples);
int  thp_tev_setup_yuv(unsigned int stage, int *coordCounter, int *texCounter);
void thp_gx_finalize(void);

// ---------------------------------------------------------------------------
// thp_decode_video (FUN_801908a4)
// Decode one THP/JPEG video component into the current Y/U/V plane buffers.
// On success latches the decoded frame id into DAT_805acef8. Returns true when
// the decode succeeded (FUN_803bc328 returned 0).
// ---------------------------------------------------------------------------
static bool thp_decode_video(int jpegSrc) {
    unsigned int rc = FUN_803bc328(jpegSrc, DAT_805aceec, DAT_805acef0, DAT_805acef4, DAT_805ace3c);
    if (rc == 0) {
        DAT_805acef8 = *(int *)(&DAT_805ace78 + DAT_805ace54 * 0xc);
    }
    return rc == 0;
}

// ---------------------------------------------------------------------------
// thp_decode_frame (FUN_80190644)
// Consume the next ready ring slot: walk the movie's component list, decode the
// video component (thp_decode_video) and, when the movie has audio, decode the
// audio component (FUN_803c3890) into the audio ring. Then release the slot,
// advance the decode read index (branchless wrap 9->0) and kick the next DVD
// read if the write slot is free and none is outstanding.
// ---------------------------------------------------------------------------
void thp_decode_frame(unsigned long long a1, double a2, double a3, unsigned long long a4,
                      unsigned long long a5, unsigned long long a6, unsigned long long a7,
                      unsigned long long a8) {
    if ((&DAT_805ace7c)[DAT_805ace54 * 3] == 0) {
        return;
    }

    int *sizeTab = (int *)((&DAT_805ace74)[DAT_805ace54 * 3] + 8);
    int cursor   = (&DAT_805ace74)[DAT_805ace54 * 3] + DAT_805ace0c * 4 + 8;

    if (DAT_805ace47 == '\0') {
        // Video-only: decode each non-audio component in turn.
        for (unsigned int i = 0; i < (unsigned int)DAT_805ace0c; ++i) {
            if ((&DAT_805ace10)[i] == '\0' && !thp_decode_video(cursor)) {
                return;
            }
            cursor += *sizeTab;
            ++sizeTab;
        }
    } else {
        // With audio: bail if the audio ring write slot is still busy.
        if ((&DAT_805acf04)[DAT_805acf20 * 3] != 0) {
            return;
        }
        for (unsigned int i = 0; i < (unsigned int)DAT_805ace0c; ++i) {
            if ((&DAT_805ace10)[i] == '\x01') {
                int decoded = FUN_803c3890((short *)(&DAT_805acefc)[DAT_805acf20 * 3],
                                           (int *)cursor, 0);
                FUN_8039cca0();
                (&DAT_805acf04)[DAT_805acf20 * 3] = decoded;
                (&DAT_805acf00)[DAT_805acf20 * 3] = (&DAT_805acefc)[DAT_805acf20 * 3];
                FUN_8039ccc8();
                DAT_805acf20 = DAT_805acf20 + 1;
                if (2 < DAT_805acf20) {
                    DAT_805acf20 = 0;
                }
            } else if ((&DAT_805ace10)[i] == '\0' && !thp_decode_video(cursor)) {
                return;
            }
            cursor += *sizeTab;
            ++sizeTab;
        }
    }

    (&DAT_805ace7c)[DAT_805ace54 * 3] = 0;
    DAT_805ace54 = DAT_805ace54 + 1 & ~-(unsigned int)(DAT_805ace54 - 9 < 0x7ffffff6);
    FUN_8039cca0();
    if ((&DAT_805ace7c)[DAT_805ace58 * 3] == 0 && DAT_805ace50 == 0) {
        dvd_read_stream_02e8(a1, a2, a3, a4, a5, a6, a7, a8);
    }
    FUN_8039ccc8();
}

// ---------------------------------------------------------------------------
// thp_draw_frame (FUN_8019090c)
// If a decoded frame is available (DAT_805acef8 >= 0), set up the YUV->RGB TEV
// stages, emit the full-frame quad from the Y/U/V planes, then finalize GX.
// Spill frame FUN_803826e8 supplies the base TEV stage.
// ---------------------------------------------------------------------------
void thp_draw_frame(int param_1, int param_2, short param_3, short param_4, short param_5) {
    (void)param_1; (void)param_2;
    int baseStage = (int)FUN_803826e8();
    if (0 <= DAT_805acef8) {
        FUN_801912ac(baseStage);
        FUN_80191428(DAT_805aceec, DAT_805acef0, DAT_805acef4, 0, param_3,
                     (int)(short)DAT_805ace20, (int)(short)DAT_805ace24, param_4, param_5);
        thp_gx_finalize();
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// thp_audio_resample_mix (FUN_801909ac)
// Fill `samples` stereo frames at `dst`. When src==NULL the output is generated
// from the decoded audio ring (scaled by the volume-ramp LUT DAT_8041e8e8, index
// walked by DAT_805ace64 with step DAT_805ace6c over DAT_805ace70 ramp samples);
// otherwise src is mixed on top. If the movie is inactive / stopping / has no
// audio, dst is just cleared (or copied straight from src).
// ---------------------------------------------------------------------------
void thp_audio_resample_mix(short *dst, short *src, unsigned int samples) {
    // float-in-int state (DAT_805ace64/68 hold float bit patterns in the ROM).
    float *rampPos = (float *)&DAT_805ace64;
    float  rampTgt = *(float *)&DAT_805ace68;
    float  rampStep = *(float *)&DAT_805ace6c;

    if (src == (short *)0) {
        if (DAT_805ace40 == 0 || DAT_805ace45 != '\x01' || DAT_805ace47 == '\0') {
            memset_wrapper((int)dst, 0, samples << 2);
            return;
        }
        do {
            unsigned int avail = (unsigned int)(&DAT_805acf04)[DAT_805acf24 * 3];
            if (avail == 0) {
                memset_wrapper((int)dst, 0, samples << 2);
                return;
            }
            unsigned int n = (samples <= avail) ? samples : avail;
            short *asrc = (short *)(&DAT_805acf00)[DAT_805acf24 * 3];
            for (unsigned int k = n; k != 0; --k) {
                float pos = rampTgt;
                if (DAT_805ace70 != 0) {
                    DAT_805ace70 = DAT_805ace70 - 1;
                    pos = *rampPos + rampStep;
                }
                *rampPos = pos;
                unsigned short vol = *(unsigned short *)(&DAT_8041e8e8 + (int)*rampPos * 2);
                int l = (int)((unsigned int)vol * (int)asrc[0]) >> 0xf;
                if (l < -0x8000) l = -0x8000; if (0x7fff < l) l = 0x7fff;
                dst[0] = (short)l;
                int r = (int)((unsigned int)vol * (int)asrc[1]) >> 0xf;
                if (r < -0x8000) r = -0x8000; if (0x7fff < r) r = 0x7fff;
                dst[1] = (short)r;
                dst += 2; asrc += 2;
            }
            samples -= n;
            (&DAT_805acf04)[DAT_805acf24 * 3] = (&DAT_805acf04)[DAT_805acf24 * 3] - n;
            (&DAT_805acf00)[DAT_805acf24 * 3] = (int)asrc;
            if ((&DAT_805acf04)[DAT_805acf24 * 3] == 0) {
                DAT_805acf24 = DAT_805acf24 + 1;
                if (2 < DAT_805acf24) DAT_805acf24 = 0;
            }
        } while (samples != 0);
        return;
    }

    if (DAT_805ace40 == 0 || DAT_805ace45 != '\x01' || DAT_805ace47 == '\0') {
        leaf_31e8((int)dst, (int)src, samples << 2);
        return;
    }
    do {
        unsigned int avail = (unsigned int)(&DAT_805acf04)[DAT_805acf24 * 3];
        if (avail == 0) {
            leaf_31e8((int)dst, (int)src, samples << 2);
            return;
        }
        unsigned int n = (samples <= avail) ? samples : avail;
        short *asrc = (short *)(&DAT_805acf00)[DAT_805acf24 * 3];
        for (unsigned int k = n; k != 0; --k) {
            float pos = rampTgt;
            if (DAT_805ace70 != 0) {
                DAT_805ace70 = DAT_805ace70 - 1;
                pos = *rampPos + rampStep;
            }
            *rampPos = pos;
            unsigned short vol = *(unsigned short *)(&DAT_8041e8e8 + (int)*rampPos * 2);
            int l = (int)src[0] + ((int)((unsigned int)vol * (int)asrc[0]) >> 0xf);
            if (l < -0x8000) l = -0x8000; if (0x7fff < l) l = 0x7fff;
            dst[0] = (short)l;
            int r = (int)src[1] + ((int)((unsigned int)vol * (int)asrc[1]) >> 0xf);
            if (r < -0x8000) r = -0x8000; if (0x7fff < r) r = 0x7fff;
            dst[1] = (short)r;
            dst += 2; src += 2; asrc += 2;
        }
        samples -= n;
        (&DAT_805acf04)[DAT_805acf24 * 3] = (&DAT_805acf04)[DAT_805acf24 * 3] - n;
        (&DAT_805acf00)[DAT_805acf24 * 3] = (int)asrc;
        if ((&DAT_805acf04)[DAT_805acf24 * 3] == 0) {
            DAT_805acf24 = DAT_805acf24 + 1;
            if (2 < DAT_805acf24) DAT_805acf24 = 0;
        }
    } while (samples != 0);
}

// ---------------------------------------------------------------------------
// thp_get_dimensions (FUN_80190d00)
// Copy the 12-byte {width,height,?} header block to `out` when a movie is
// active. Returns true if a movie is loaded.
// ---------------------------------------------------------------------------
bool thp_get_dimensions(int out) {
    bool active = DAT_805ace40 != 0;
    if (active) {
        leaf_31e8(out, 0x805ace20, 0xc);
    }
    return active;
}

// ---------------------------------------------------------------------------
// thp_get_frame_rate (FUN_80190d48)
// Frame rate of the active movie (DAT_805acdec), or the default when inactive.
// ---------------------------------------------------------------------------
double thp_get_frame_rate(void) {
    if (DAT_805ace40 != 0) {
        return (double)*(float *)&DAT_805acdec;
    }
    return (double)FLOAT_806168e4;
}

// ---------------------------------------------------------------------------
// thp_get_frame_count (FUN_80190d6c)
// Total frame count of the active movie, or 0.
// ---------------------------------------------------------------------------
int thp_get_frame_count(void) {
    if (DAT_805ace40 != 0) {
        return DAT_805acdf0;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// thp_audio_retrace_cb (FUN_80190d90)
// VI-retrace audio double-buffer callback (installed by thp_open). Flips the
// 0x280-byte audio double buffer, hands the freed half to the audio HW, and
// refills it via thp_audio_resample_mix. In chained mode (DAT_806124d0 != 0) it
// also invokes the previously-installed retrace callback and tracks the DMA
// start address so the chained stream is mixed in.
// ---------------------------------------------------------------------------
void thp_audio_retrace_cb(void) {
    if (DAT_806124d0 == 0) {
        DAT_806124dc = DAT_806124dc ^ 1;
        FUN_803a9880((int)(&DAT_805acf40 + DAT_806124dc * 0x280), 0x280);
        FUN_8039ccb4();
        thp_audio_resample_mix((short *)(&DAT_805acf40 + DAT_806124dc * 0x280),
                               (short *)0, 0xa0);
        FUN_803994d0((int)(&DAT_805acf40 + DAT_806124dc * 0x280), 0x280);
        FUN_8039ccc8();
        return;
    }

    if (DAT_806124d0 == 1) {
        if (DAT_806124d8 != 0) {
            DAT_806124d4 = DAT_806124d8;
        }
        ((void (*)(void))(unsigned int)DAT_806124cc)();
        DAT_806124d8 = (int)(FUN_803a9938() + 0x80000000);
    } else {
        ((void (*)(void))(unsigned int)DAT_806124cc)();
        DAT_806124d4 = (int)(FUN_803a9938() + 0x80000000);
    }
    DAT_806124dc = DAT_806124dc ^ 1;
    FUN_803a9880((int)(&DAT_805acf40 + DAT_806124dc * 0x280), 0x280);
    FUN_8039ccb4();
    if (DAT_806124d4 != 0) {
        FUN_803994a4((unsigned int)DAT_806124d4, 0x280);
    }
    thp_audio_resample_mix((short *)(&DAT_805acf40 + DAT_806124dc * 0x280),
                           (short *)(unsigned int)DAT_806124d4, 0xa0);
    FUN_803994d0((int)(&DAT_805acf40 + DAT_806124dc * 0x280), 0x280);
    FUN_8039ccc8();
}

// ---------------------------------------------------------------------------
// thp_tev_setup_yuv (FUN_80190f08)
// Program 4 TEV stages that reconstruct RGB from the Y/U/V texture planes using
// the ITU colour-conversion konst coefficients (DAT_806168e8..f8). Advances the
// caller's coord (+2) and tex (+3) counters and returns the next free stage
// (base + 4).
// ---------------------------------------------------------------------------
int thp_tev_setup_yuv(unsigned int base, int *coordCounter, int *texCounter) {
    *coordCounter = *coordCounter + 2;
    *texCounter   = *texCounter + 3;
    gx_cached_set_tev_order(1, 1, 4, 0x3c);

    unsigned int s1 = base + 1;
    gx_cached_set_tev_color_op(base, 1, 1, 0xff);
    gx_cached_set_tev_color_in(base, 0xf, 8, 0xe, 2);
    gx_cached_set_tev_color_env(base, 0, 0, 0, 0, 0);
    gx_cached_set_tev_alpha_in(base, 7, 4, 6, 1);
    gx_cached_set_tev_alpha_env(base, 1, 0, 0, 0, 0);
    FUN_803ca29c(base, 0xc);
    FUN_803ca2f8(base, 0x1c);
    FUN_803ca354(base, 0, 0);

    unsigned int s2 = base + 2;
    gx_cached_set_tev_color_op(s1, 1, 2, 0xff);
    gx_cached_set_tev_color_in(s1, 0xf, 8, 0xe, 0);
    gx_cached_set_tev_color_env(s1, 0, 0, 1, 0, 0);
    gx_cached_set_tev_alpha_in(s1, 7, 4, 6, 0);
    gx_cached_set_tev_alpha_env(s1, 1, 0, 0, 0, 0);
    FUN_803ca29c(s1, 0xd);
    FUN_803ca2f8(s1, 0x1d);
    FUN_803ca354(s1, 0, 0);

    unsigned int s3 = base + 3;
    gx_cached_set_tev_color_op(s2, 0, 0, 0xff);
    gx_cached_set_tev_color_in(s2, 0xf, 8, 0xc, 0);
    gx_cached_set_tev_color_env(s2, 0, 0, 0, 1, 0);
    gx_cached_set_tev_alpha_in(s2, 4, 7, 7, 0);
    gx_cached_set_tev_alpha_env(s2, 0, 0, 0, 1, 0);
    FUN_803ca354(s2, 0, 0);

    gx_cached_set_tev_color_op(s3, 0xff, 0xff, 0xff);
    gx_cached_set_tev_color_in(s3, 1, 0, 0xe, 0xf);
    gx_cached_set_tev_color_env(s3, 0, 0, 0, 1, 0);
    gx_cached_set_tev_alpha_in(s3, 7, 7, 7, 7);
    gx_cached_set_tev_alpha_env(s3, 0, 0, 0, 1, 0);
    FUN_803ca354(s3, 0, 0);
    FUN_803ca29c(s3, 0xe);

    unsigned int konst01[2];
    konst01[0] = (unsigned int)DAT_806168e8;
    konst01[1] = (unsigned int)DAT_806168ec;
    FUN_803ca1d4(1, konst01);
    unsigned int k0 = (unsigned int)DAT_806168f0; FUN_803ca238(0, &k0);
    unsigned int k1 = (unsigned int)DAT_806168f4; FUN_803ca238(1, &k1);
    unsigned int k2 = (unsigned int)DAT_806168f8; FUN_803ca238(2, &k2);
    FUN_803ca39c(0, 0, 1, 2, 3);
    return base + 4;
}

// ---------------------------------------------------------------------------
// thp_gx_finalize (FUN_80191264)
// Restore GX state after the YUV blit: reset vtx descriptor/format, clear the
// sub-material override callback and blend tag.
// ---------------------------------------------------------------------------
void thp_gx_finalize(void) {
    FUN_803c8874(1);
    gx_set_sub_material_id(1);
    gx_set_material_override_cb(0);
    gx_config_vtx_format(7);
    gx_set_blend_tag(0, 0);
}

// ===========================================================================
// Batch: batch_500 (part) - THP YUV blit primitive begin + quad emit.
// The final two pieces of the on-screen movie blit: thp_begin_yuv_blit sets up
// the scissor / tex-coord transform / indirect-TEV state and installs the YUV
// material override callback; thp_emit_yuv_quad binds the Y/U/V texture objects
// and streams the 4-vertex quad to the GX FIFO.
// Address range: 0x801912ac - 0x80191428
// ===========================================================================

extern "C" {
extern void gx_set_scissor_rect(int x, int y, int w, int h);
extern void gx_set_tex_transform_params(double a, double b, double c, double d, double e, double f);
extern void gx_set_reflect_matrix_b(float *mtx);
extern void gx_indirect_stage_config(void);
extern void gx_begin_primitive(int vtxfmt, int prim, int count);
extern void FUN_803a142c(double a, double b, double c, double d, double e, double f, float *out);
extern void FUN_803c94d8(void);
extern void FUN_803c6274(int a1, int a2, int a3, int a4, int a5);   // tev stage input select
extern void FUN_803c8b84(unsigned int *texObj, unsigned int data, unsigned int w, unsigned int h,
                         int fmt, int a6, int a7, char a8);         // GXInitTexObj-style
extern void FUN_803c8e18(double sx, double sy, double sz, unsigned int *texObj,
                         int a5, int a6, int a7, char a8, int a9);  // GXInitTexObjLOD-style
extern unsigned long long FUN_803826dc(void);                      // spill frame (matrix pair)
extern void FUN_80382728(void);
extern void FUN_803c9288(unsigned int *texObj, int slot);          // bind tex object to slot
extern void mtx4_transpose(float *out, float *in);
extern void mtx4_set_identity(int outMtx);

extern int   DAT_80612250;
extern int   DAT_80612258;
extern int   DAT_8058e3cc;     // env/reset matrix source
extern float FLOAT_806168fc;   // tex-transform scale
extern float FLOAT_80616900;   // tex-transform param Y
extern float FLOAT_80616904;   // tex-transform param (chroma)
extern double DOUBLE_80616908; // int->double bias base for dim dequant
// FIFO write port (GX gather-pipe) at 0xcc008000 - accessed as volatile store.
extern volatile unsigned short DAT_cc008000;
} // extern "C"

// int<->double "magic bias" (0x43300000 high word) dequant, matching the PPC
// idiom used by the tex-transform dimension conversion below.
static inline double u32_to_double_bias(unsigned int lo) {
    union { unsigned int u[2]; double d; } bias, val;
    bias.u[0] = 0x43300000u; bias.u[1] = 0x00000000u;
    val.u[0]  = 0x43300000u; val.u[1]  = lo;
    return val.d - bias.d;
}

// ---------------------------------------------------------------------------
// thp_begin_yuv_blit (FUN_801912ac)
// Set up GX for the YUV->RGB movie blit: bind sub-material 0xe, opaque blend,
// scissor to the frame size (from ctx+4/+6 = W/H), build the luma and chroma
// tex-coord transform matrices (dequantising dims via the 0x43300000 bias),
// install the environment reflect matrix, and select the indirect-TEV inputs +
// the YUV material override callback.
// ---------------------------------------------------------------------------
void thp_begin_yuv_blit(int ctx) {
    unsigned int w = *(unsigned short *)(ctx + 4);
    unsigned int h = *(unsigned short *)(ctx + 6);
    gx_set_sub_material_id(0xe);
    gx_set_blend_mode(0);
    gx_set_scissor_rect(0, 0, (int)w, (int)h);

    double scale = (double)FLOAT_806168fc;
    double fw = (double)(float)(u32_to_double_bias(w ^ 0x80000000u) - DOUBLE_80616908);
    double fh = (double)(float)(u32_to_double_bias(h ^ 0x80000000u) - DOUBLE_80616908);
    gx_set_tex_transform_params(scale, scale, fw, fh, scale, (double)FLOAT_80616900);

    float chromaMtx[16];
    float envMtx[16];
    // Chroma tex-coord matrix uses the height/width swapped as the original does.
    FUN_803a142c(scale, fh, scale, fw, scale, (double)FLOAT_80616904, chromaMtx);
    mtx4_transpose(chromaMtx, chromaMtx);
    gx_set_reflect_matrix_b(chromaMtx);
    mtx4_set_identity((int)envMtx);
    mtx4_transpose(envMtx, (float *)&DAT_8058e3cc);

    DAT_80612250 = 1;
    DAT_80612258 = 0;
    gx_set_blend_tag(3, 0);
    FUN_803c8874(0);
    FUN_803c94d8();
    gx_indirect_stage_config();
    FUN_803c6274(7, 9, 1, 3, 0);
    FUN_803c6274(7, 0xd, 1, 2, 0);
    gx_set_material_override_cb(-0x7fe6f0f8);
}

// ---------------------------------------------------------------------------
// thp_emit_yuv_quad (FUN_80191428)
// Bind the three YUV planes as GX texture objects (luma full-res on slot 0,
// chroma half-res on slots 1/2) and stream a screen-space quad (4 verts, pos +
// 2 UV sets) to the GX FIFO at 0xcc008000. Screen rect = (x,y,w,h) in the short
// args; the spill frame supplies the shared texObj data word.
// ---------------------------------------------------------------------------
void thp_emit_yuv_quad(int planeY, int planeU, int planeV, short x, short y,
                       unsigned int w, unsigned int h, short qw, short qh) {
    (void)planeY;
    unsigned int texObjY[8];
    unsigned int texObjU[8];
    unsigned int texObjV[18];

    unsigned long long frame = FUN_803826dc();
    unsigned int lumaData = (unsigned int)(frame >> 0x20);
    unsigned int chromaData = (unsigned int)frame;

    // Luma plane: full W x H.
    FUN_803c8b84(texObjV, lumaData, w & 0xffff, h & 0xffff, 1, 0, 0, '\0');
    double s = (double)FLOAT_806168fc;
    FUN_803c8e18(s, s, s, texObjV, 0, 0, 0, '\0', 0);
    FUN_803c9288(texObjV, 0);

    unsigned int cw = (unsigned int)((int)(short)w >> 1);
    unsigned int ch = (unsigned int)((int)(short)h >> 1);

    // U plane: half res, slot 1.
    FUN_803c8b84(texObjU, chromaData, cw & 0xffff, ch & 0xffff, 1, 0, 0, '\0');
    s = (double)FLOAT_806168fc;
    FUN_803c8e18(s, s, s, texObjU, 0, 0, 0, '\0', 0);
    FUN_803c9288(texObjU, 1);

    // V plane: half res, slot 2.
    FUN_803c8b84(texObjY, (unsigned int)planeV, cw & 0xffff, ch & 0xffff, 1, 0, 0, '\0');
    s = (double)FLOAT_806168fc;
    FUN_803c8e18(s, s, s, texObjY, 0, 0, 0, '\0', 0);
    FUN_803c9288(texObjY, 2);
    (void)planeU;

    gx_begin_primitive(0x80, 7, 4);
    // 4 vertices: {pos.xyz, uv0, uv1} written to the gather-pipe FIFO. The raw
    // decomp writes each field to the same volatile FIFO port in sequence.
    short vx0 = x,       vy0 = y;
    short vx1 = x + qw,  vy1 = y;
    short vx2 = x + qw,  vy2 = y + qh;
    short vx3 = x,       vy3 = y + qh;
    // v0
    DAT_cc008000 = (unsigned short)vx0; DAT_cc008000 = (unsigned short)vy0;
    DAT_cc008000 = 0; DAT_cc008000 = 0; DAT_cc008000 = 0;
    // v1
    DAT_cc008000 = (unsigned short)vx1; DAT_cc008000 = (unsigned short)vy1;
    DAT_cc008000 = 0; DAT_cc008000 = 1; DAT_cc008000 = 0;
    // v2
    DAT_cc008000 = (unsigned short)vx2; DAT_cc008000 = (unsigned short)vy2;
    DAT_cc008000 = 0; DAT_cc008000 = 1; DAT_cc008000 = 1;
    // v3
    DAT_cc008000 = (unsigned short)vx3; DAT_cc008000 = (unsigned short)vy3;
    DAT_cc008000 = 0; DAT_cc008000 = 0; DAT_cc008000 = 1;
    FUN_80382728();
}

// ============================================================================
// thp_movie_play_fullscreen (0x801821bc) | batch_501 | Date: 2026-09-06
// Subsystem: filesystem (THP movie playback front-end / loading video driver).
//
// The high-level "play a full-screen movie" entry point built on the thp_* helpers
// defined above.  It:
//   1. mutes the music track and switches the occlusion-read mode off, opens the
//      movie (thp_open) and kicks the initial DVD header read
//      (dvd_open_and_read_fd5c);
//   2. on open failure, closes the decoder, restores occlusion mode, presents a few
//      blank loading frames and resumes music (early-out);
//   3. otherwise queries frame count / rate / dimensions, computes and allocates the
//      decode work buffer (mem_alloc_aligned), allocates the frame-buffer ring
//      (thp_alloc_frame_buffers) and queues the first reads (thp_queue_frame_reads);
//   4. runs the per-frame playback loop: poll input (any button on any pad aborts
//      when skippable), pace to the movie frame rate against the CPU time-base while
//      servicing the disc-error/message screen, decode one frame (thp_decode_frame),
//      clear the target, then blit the frame full-screen — letterboxed and, in
//      widescreen, vertically scaled (is_widescreen);
//   5. on exit: stop/flush the stream (thp_request_stop / thp_poll_finished),
//      close it, free the work buffer, reset GX render state, present blank frames
//      and resume music.
//
// Non-matching: in this port the thp_* decode/queue helpers return void (see their
// definitions above), so the loop is bounded by the parsed frame count and the
// stop/abort predicates rather than by reading a decoder status int back from each
// call.  The many f1/paired-single spill temporaries the decompiler surfaced are
// dropped; the widescreen letterbox maths keep the integer geometry.
// ============================================================================
extern "C" {
extern int   dvd_open_and_read_fd5c(unsigned long long a1, double a2, double a3, unsigned long long a4,
                                    unsigned long long a5, unsigned long long a6, unsigned long long a7,
                                    unsigned long long a8);   // open movie + first read; 0 = fail
extern void  thp_get_dimensions(unsigned int outWH);          // fill width/height scratch
extern unsigned long long music_track_mute(void);
extern void  music_track_resume(void);
extern unsigned char occ_read_mode_set(int mode);             // returns previous mode
extern unsigned int mem_alloc_aligned(double a1, double a2, double a3, unsigned long long a4,
                                      unsigned long long a5, unsigned long long a6, unsigned long long a7,
                                      unsigned long long a8, int align, int size, int a11,
                                      int a12, int a13, int a14, int a15, int a16);
extern void  mem_free_top(unsigned int block);
extern unsigned int is_widescreen(void);
extern unsigned long long gx_present_loading_frame(unsigned long long a1, double a2, double a3,
                                                   unsigned long long a4, unsigned long long a5,
                                                   unsigned long long a6, unsigned long long a7,
                                                   unsigned long long a8);
extern void  gx_frame_flush_wait_m(unsigned long long a1, double a2, double a3, unsigned long long a4,
                                   unsigned long long a5, unsigned long long a6, unsigned long long a7,
                                   unsigned long long a8);
extern void  gx_post_present_setup_m(void);
extern void  occ_query_flush_guard_m(void);
extern unsigned long long gx_fill_rect_clipped_m(int x0, int y0, int x1, int y1, unsigned int color);
extern unsigned long long gx_reset_render_state_m(void);
extern unsigned long long vi_present_frame_m(int a);
extern void  input_update_all_m(void);
extern int   input_get_buttons_held_m(int pad);
extern int   boot_disc_error_tick(unsigned long long a1, double a2, double a3, unsigned long long a4,
                                  unsigned long long a5, unsigned long long a6, unsigned long long a7,
                                  unsigned long long a8);
extern int   FUN_803a5214(void);            // disc-error retry gate
extern unsigned int FUN_8039e020(void);     // disc-error abort poll
extern void  FUN_803994d0_m(unsigned int addr, int len);   // DCFlushRange (movie-side alias)
extern void  FUN_803994d0_free(unsigned int block, int size);  // release aligned block companion
extern void  FUN_803826b8_prof(void);       // profiler push (movie loader)
extern void  FUN_80382724_prof(void);       // profiler pop  (movie loader)
extern int   os_get_time_base(void);        // CPU time-base low word
extern void  FUN_80399710_movie(void);      // post-playback heap fixup A
extern void  FUN_803996d8_movie(void);      // post-playback heap fixup B

extern int   DAT_80612380;    // "movie playing" present-path flag
extern int   DAT_806123b8_thp;// active render-mode obj (present path)
extern int   DAT_806109c4_m;  // framebuffer width  (present path)
extern int   DAT_806109c8_m;  // framebuffer height (present path)
extern int   DAT_800000f8_m;  // CPU bus clock
extern float FLOAT_806166f4, FLOAT_806166f8, FLOAT_806166fc, FLOAT_80616700;
extern double DOUBLE_806166c0_m;
} // extern "C"

void thp_movie_play_fullscreen(unsigned long long param_1, double param_2, double param_3,
                               unsigned long long param_4, unsigned long long param_5,
                               unsigned long long param_6, unsigned long long param_7,
                               unsigned long long param_8, int allowSkip)
{
    FUN_803826b8_prof();

    unsigned long long ctx = music_track_mute();
    unsigned char prevOcc = occ_read_mode_set(0);

    thp_open(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
             1, 0, 0, 0, 0, 0, 0, 0);
    int opened = dvd_open_and_read_fd5c(ctx, param_2, param_3, param_4, param_5, param_6,
                                        param_7, param_8);

    if (opened == 0) {
        thp_close();
        occ_read_mode_set(prevOcc);
        for (int i = 4; i >= 0; --i) {
            gx_present_loading_frame(ctx, param_2, param_3, param_4, param_5, param_6,
                                     param_7, param_8);
        }
        music_track_resume();
        FUN_80382724_prof();
        return;
    }

    int frameCount = thp_get_frame_count();
    double frameRate = thp_get_frame_rate();
    unsigned char dimScratch[12];
    thp_get_dimensions((unsigned int)(unsigned long long)&dimScratch[0]);
    int workSize = thp_compute_buffer_size();
    unsigned int workBuf = mem_alloc_aligned(frameRate, param_2, param_3, param_4, param_5,
                                             param_6, param_7, param_8, 0x20, workSize, 0,
                                             0, 0, 0, 0, 0);
    thp_alloc_frame_buffers();
    thp_queue_frame_reads(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8);

    gx_frame_flush_wait_m(0, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    vi_present_frame_m(1);
    DAT_80612380 = 1;

    bool firstStop = true;
    for (int frame = 0; frame < frameCount - 1; ++frame) {
        bool needPace = false;

        input_update_all_m();
        if (allowSkip != 0 &&
            (input_get_buttons_held_m(0) != 0 || input_get_buttons_held_m(1) != 0 ||
             input_get_buttons_held_m(2) != 0 || input_get_buttons_held_m(3) != 0)) {
            break;
        }

        // Service the disc-error / message screen; break out if it terminates.
        bool aborted = false;
        for (;;) {
            int st = boot_disc_error_tick(ctx, param_2, param_3, param_4, param_5, param_6,
                                          param_7, param_8);
            if (st == 0) break;
            needPace = true;
        }
        if (aborted) break;

        if (needPace) {
            int t0 = os_get_time_base();
            while ((unsigned int)(os_get_time_base() - t0) < (unsigned int)((DAT_800000f8_m >> 2) * 3)
                   || FUN_803a5214() == 0) {
                if (FUN_8039e020() != 0) break;
                if (boot_disc_error_tick(ctx, param_2, param_3, param_4, param_5, param_6,
                                         param_7, param_8) != 0) break;
            }
        }

        thp_decode_frame(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8);

        occ_query_flush_guard_m();
        gx_post_present_setup_m();
        for (int i = 0; i < 0xf; ++i) {
            gx_fill_rect_clipped_m(0, 0, DAT_806109c4_m, DAT_806109c8_m, 0);
        }

        if (allowSkip == 0) {
            thp_draw_frame(DAT_806123b8_thp, 0, 0,
                           (short)(DAT_806109c4_m - 1), (short)(DAT_806109c8_m - 1));
        } else {
            // Widescreen letterbox: scale the vertical extent when 16:9.
            float scale = FLOAT_806166fc;
            if ((is_widescreen() & 0xff) != 0) {
                scale = FLOAT_806166f4 +
                        (float)((double)(int)DAT_806109c8_m - DOUBLE_806166c0_m) / FLOAT_806166f8;
            }
            int top = (int)-scale;
            int bottom = (int)(FLOAT_80616700 * (double)scale + (double)(int)(DAT_806109c8_m - 1));
            thp_draw_frame(DAT_806123b8_thp, 0xffffffe2, (short)top,
                           (short)(DAT_806109c4_m + 0x3b), (short)bottom);
        }

        if (firstStop) {
            thp_request_stop();
            firstStop = false;
        }
        gx_frame_flush_wait_m(0, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
        vi_present_frame_m(1);
    }

    DAT_80612380 = 0;
    gx_present_loading_frame(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    thp_poll_finished();
    thp_close();
    mem_free_top(workBuf);
    FUN_803994d0_free(workBuf, workSize);
    gx_reset_render_state_m();
    for (int i = 4; i >= 0; --i) {
        gx_present_loading_frame(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    }
    music_track_resume();
    occ_query_flush_guard_m();
    gx_post_present_setup_m();
    occ_read_mode_set(prevOcc);
    FUN_80399710_movie();
    FUN_803996d8_movie();

    FUN_80382724_prof();
}
