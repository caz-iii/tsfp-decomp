// hw_voice_stream.cpp
// Hardware dual-channel voice / AX streaming layer. This is the hardware path
// beneath the stream_voice_3d control API (stream_voice's FUN_8018b4b0/3ac/448/
// 514 route here). A hardware voice owns one (mono) or two (stereo) mixer voices
// at rec+0x1c / rec+0x20, streams ADPCM out of a DVD double-buffer
// (DAT_805a45a0 / DAT_805a4600) refilled via dvd2_handle_read_async, and pushes
// per-voice AX register state (format / loop / end / ADPCM coefficients / gain).
// Batch: batch_487
#include "common.h"

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
// Audio scalar helper.
extern int audio_linear_to_centibels(int linear);

// Mixer voice back-end.
extern int *FUN_803ac3a8(uint prio, int callback, int userData);  // alloc mixer voice
extern void FUN_803af120(int voice, uint fmt, int cb, u32 a4, u32 a5, u32 pitch,
                         u32 a7, u32 a8);                          // init voice params

// AX per-voice register setters (voice base + field, sets dirty bits in +0x1c).
extern void FUN_803aeb08(int voice, u32 *pAdpcmCtx);  // set ADPCM loop/predictor context
extern void FUN_803aea60(int voice, short endFlag);   // set end/loop flag (+0x146)
extern void FUN_803aeabc(int voice, u16 fmt);         // set sample format (+0x148)
extern void FUN_803aec64(int voice, u32 loopAddr);    // set loop-start addr (+0x1aa)
extern void FUN_803aecb8(int voice, u32 endAddr);     // set end addr (+0x1ae)
extern void FUN_803aeee0(int voice, u16 *pAdpcmState);// set ADPCM running state (+0x1ec)
extern void FUN_803aee48(double gain, int voice);     // set voice gain (+0x1de)

// Spawn-frame guard pairs (register save/restore around the record access).
extern void *FUN_803826dc(void);            // frame-alloc: returns zeroed hw-voice record
extern void FUN_80382728(void);
extern unsigned long long FUN_803826e8(void);
extern void FUN_80382734(void);
extern unsigned long long FUN_803826f0(void);  // frame pair: hi = record ptr, lo = seg index
extern void FUN_8038273c(void);

// The stream-kick pass hwvoice_stream_kick (0x8018a564) is defined below (batch_488).
void hwvoice_stream_kick(void);

// DVD streaming read (padded ABI form; canonical name dvd2_handle_read_async).
extern int dvd2_handle_read_async(u32 f1, double a2, double a3, u32 a4, u32 a5, u32 a6,
                                  u32 a7, u32 a8, int fileCtx, int dst, int offset,
                                  int len, int prio, u32 a14, u32 a15, u32 a16);

// Sync guards + fatal stub.
extern void FUN_8039cca0(void);
extern void FUN_8039ccc8(void);
extern void memset_wrapper(int dst, int val, int len);
extern void halt_stub_assert(void);

// Data.
extern int  DAT_805a45a0;   // DVD stream double-buffer A (base symbol)
extern int  DAT_805a4600;   // DVD stream double-buffer B
extern u8   DAT_80612491;   // stream refill state A
extern u8   DAT_80612492;   // stream refill state B (bit0=pending, bit1=armed)
extern int  DAT_8061248c;   // last stream chunk size

extern double DOUBLE_806167f0; // gain dequant magic-bias
extern float  FLOAT_806167e8;  // gain dequant divisor
}

// ---------------------------------------------------------------------------
// Address: 0x80189c78 | Name: hwvoice_alloc_channel | Date: 2026-09-06
// Allocate one mixer voice (priority 0x1f, streaming callback at 0x80189c20 =
// -0x7fe763e0) and initialize its parameters: convert the linear volume to
// centibels and program pan/pitch defaults. Returns the mixer-voice pointer.
int *hwvoice_alloc_channel(uint linear, u32 pitch, int userData)
{
    int cb = audio_linear_to_centibels((int)linear);
    int *voice = FUN_803ac3a8(0x1f, (int)0x80189c20, userData);
    FUN_803af120((int)voice, 0, cb, 0xfffffc78, 0xfffffc78, pitch, 0x7f, 0);
    return voice;
}

// ---------------------------------------------------------------------------
// Address: 0x80189cf0 | Name: hwvoice_open | Date: 2026-09-06
// Open a hardware streaming voice: zero the 0x48-byte record (frame-allocated),
// seed the DVD cursor window (+0xc = start, +0x18 = start + 0x20000), copy the
// stream id pair, mark it as a source (+0x45 = 1) with an invalid page (+0x44 =
// 0xff). Mono (twoChannel == 0) allocates a single mixer voice at rec+0x20;
// stereo allocates rec+0x20 (right, pan 0x7f) and rec+0x1c (left, pan 0).
void hwvoice_open(u32 arg0, u32 arg1, u32 *pIdPair, int startAddr, uint linear,
                  u32 pitch, int loopFlag, int twoChannel)
{
    (void)arg0;
    (void)arg1;

    u32 *rec = (u32 *)FUN_803826dc();
    FUN_8039cca0();
    memset_wrapper((int)rec, 0, 0x48);
    rec[3] = (u32)startAddr;
    rec[6] = (u32)(startAddr + 0x20000);
    rec[0] = pIdPair[0];
    rec[1] = pIdPair[1];
    *(u8 *)((int)rec + 0x45) = 1;
    *(u8 *)(rec + 0x11) = 0xff;

    if (twoChannel == 0) {
        *(u16 *)(rec + 0x10) |= 1;
        rec[8] = (u32)(uintptr_t)hwvoice_alloc_channel(linear, pitch, (int)((uintptr_t)rec | 1));
    } else {
        rec[8] = (u32)(uintptr_t)hwvoice_alloc_channel(linear, 0x7f, (int)((uintptr_t)rec | 1));
        rec[7] = (u32)(uintptr_t)hwvoice_alloc_channel(linear, 0, (int)(uintptr_t)rec);
    }
    if (loopFlag != 0) {
        *(u16 *)(rec + 0x10) |= 2;
    }
    FUN_8039ccc8();
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// Address: 0x80189df0 | Name: hwvoice_compute_page | Date: 2026-09-06
// Compute the page/segment index for a stream cursor: (cursor - 2*rec[3]) then
// shift right by 15 (mono) or 16 (stereo, +0x40 bit 0 set), clamped so index 8
// wraps to 7.
uint hwvoice_compute_page(int rec, int cursor)
{
    uint delta = (uint)(cursor + *(int *)(rec + 0xc) * -2);
    uint page = delta >> 0xf;
    if ((*(u16 *)(rec + 0x40) & 1) != 0) {
        page = delta >> 0x10;
    }
    return (page != 8) ? page : 7u;
}

// ---------------------------------------------------------------------------
// Address: 0x80189e20 | Name: hwvoice_set_adpcm_loop | Date: 2026-09-06
// Build a 4-word ADPCM loop context on the stack (fmt selector, loopStart,
// loopEnd, loopStart) and hand it to the AX register setter FUN_803aeb08.
void hwvoice_set_adpcm_loop(int voice, u32 loopStart, u32 loopEnd, int fmtNonZero)
{
    u16 ctx[10];
    ((u16 *)ctx)[0] = (u16)(fmtNonZero != 0);
    ((u16 *)ctx)[1] = 0;
    ((u32 *)ctx)[1] = loopStart;
    ((u32 *)ctx)[2] = loopEnd;
    ((u32 *)ctx)[3] = loopStart;
    FUN_803aeb08(voice, (u32 *)ctx);
}

// ---------------------------------------------------------------------------
// Address: 0x80189e78 | Name: hwvoice_set_end_flag | Date: 2026-09-06
// Set the end/loop flag on both mixer channels of the record (rec+0x1c and
// rec+0x20), if allocated.
void hwvoice_set_end_flag(int rec)
{
    if (*(int *)(rec + 0x1c) != 0) {
        FUN_803aea60(*(int *)(rec + 0x1c), 1);
    }
    if (*(int *)(rec + 0x20) != 0) {
        FUN_803aea60(*(int *)(rec + 0x20), 1);
    }
}

// ---------------------------------------------------------------------------
// Address: 0x80189ec8 | Name: hwvoice_submit_channel | Date: 2026-09-06
// Submit one channel's stream segment to its mixer voice: set the format, then
// either program a plain loop-end pair (looping == 0) or a full ADPCM loop
// context (looping != 0), and finally push the ADPCM running state. The
// spawn-frame pair supplies { voice, loopStart }.
void hwvoice_submit_channel(u32 arg0, u32 arg1, u32 endAddr, u16 *pAdpcmState, int looping)
{
    (void)arg0;
    (void)arg1;

    unsigned long long frame = FUN_803826e8();
    int voice = (int)((unsigned long long)frame >> 0x20);
    int loopStart = (int)frame;

    FUN_803aeabc(voice, 0);
    if (looping == 0) {
        FUN_803aec64(voice, (u32)loopStart);
        FUN_803aecb8(voice, endAddr);
    } else {
        hwvoice_set_adpcm_loop(voice, (u32)loopStart, endAddr, 1);
    }
    FUN_803aeee0(voice, pAdpcmState);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// Address: 0x80189f54 | Name: hwvoice_start | Date: 2026-09-06
// Start playback on a hardware voice: submit the initial ADPCM segment to each
// allocated channel (left at rec+0x1c, right at rec+0x20) using the per-channel
// cursor/coefficient blocks, mark the voice started (+0x40 |= 0x20, +0x45 = 4),
// re-arm the DVD stream when a second block is required, and set the page byte
// to invalid (+0x44 = 0xff).
void hwvoice_start(void)
{
    int rec = (int)((unsigned long long)FUN_803826f0() >> 0x20);

    uint looping = countLeadingZeros((uint)(2 - (uint)*(u8 *)(rec + 0x45)));
    looping = looping >> 5;

    int sampBase;
    if ((*(u16 *)(rec + 0x40) & 1) == 0) {
        sampBase = *(int *)(rec + 0x14);
    } else {
        sampBase = *(int *)(rec + 4);
    }

    if (*(int *)(rec + 0x1c) != 0) {
        hwvoice_submit_channel(
            (u32)*(int *)(rec + 0x1c), (u32)(*(int *)(rec + 0x18) * 2 + 2),
            (u32)((*(int *)(rec + 0x18) + sampBase + -0x60) * 2 + -1),
            (u16 *)(rec + 0x28), (int)looping);
    }
    if (*(int *)(rec + 0x20) != 0) {
        hwvoice_submit_channel(
            (u32)*(int *)(rec + 0x20), (u32)(*(int *)(rec + 0xc) * 2 + 2),
            (u32)((*(int *)(rec + 0xc) + sampBase + -0x60) * 2 + -1),
            (u16 *)(rec + 0x34), (int)looping);
    }
    *(u16 *)(rec + 0x40) |= 0x20;
    *(u8 *)(rec + 0x45) = 4;
    if (looping != 0) {
        hwvoice_stream_kick();
    }
    *(u8 *)(rec + 0x44) = 0xff;
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// Address: 0x8018a040 | Name: hwvoice_advance_loop | Date: 2026-09-06
// Advance the loop window for both channels by one 0x4000 (mono) / 0x8000
// (stereo) sample block: recompute each channel's end and loop-start addresses
// from the running cursor and re-flag the end.
void hwvoice_advance_loop(void)
{
    unsigned long long frame = FUN_803826f0();
    int rec = (int)(frame >> 0x20);
    int segIndex = (int)frame;   // low word = current segment index

    int block = 0x4000;
    if ((*(u16 *)(rec + 0x40) & 1) != 0) {
        block = 0x8000;
    }
    int scaled = segIndex * block;

    int cur;
    if (*(int *)(rec + 0x20) != 0) {
        cur = *(int *)(rec + 0xc);
        FUN_803aecb8(*(int *)(rec + 0x20), (u32)((cur + scaled) * 2 + -1));
        FUN_803aec64(*(int *)(rec + 0x20), (u32)(cur * 2 + 2));
        FUN_803aeabc(*(int *)(rec + 0x20), 1);
    }
    if (*(int *)(rec + 0x1c) != 0) {
        cur = *(int *)(rec + 0x18);
        FUN_803aecb8(*(int *)(rec + 0x1c), (u32)((cur + scaled) * 2 + -1));
        FUN_803aec64(*(int *)(rec + 0x1c), (u32)(cur * 2 + 2));
        FUN_803aeabc(*(int *)(rec + 0x1c), 1);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// Address: 0x8018a10c | Name: hwvoice_stream_fill | Date: 2026-09-06
// Refill the DVD stream double-buffer for a hardware voice. Compute the next
// chunk size (clamped to the 0x8000-byte window less the head reserve), kick an
// async DVD read into DAT_805a45a0 + offset, advance the read cursor, and set
// the loop/end/page flags. When the tail of the stream is reached it either
// wraps (loop) or arms the final segment and calls hwvoice_advance_loop.
void hwvoice_stream_fill(u32 arg0, double a2, double a3, u32 arg4, u32 arg5, u32 arg6,
                         u32 arg7, u32 arg8, u32 *rec, int firstFill)
{
    uint reserve;
    if (firstFill == 0) {
        reserve = 0xc0;
        if (DAT_80612491 == 1) {
            reserve = (uint)DAT_8061248c + 0xc0;
        }
    } else {
        DAT_80612492 |= 1;
        *(u8 *)((int)rec + 0x45) = 2;
        reserve = (uint)(-(int)(*(u16 *)(rec + 0x10) & 1)) & 0x60;
    }

    uint window = 0x80c0 - reserve;
    DAT_8061248c = (int)(rec[1] - rec[2]);
    if (window < (uint)(rec[1] - rec[2])) {
        DAT_8061248c = (int)window;
    }
    DAT_80612491 = 2;
    if ((int)(rec[1] - rec[2]) < DAT_8061248c) {
        DAT_8061248c = (int)(rec[1] - rec[2]);
    }

    int rc = dvd2_handle_read_async(
        arg0, a2, a3, arg4, arg5, arg6, arg7, arg8, (int)rec[0],
        (int)((uintptr_t)&DAT_805a45a0 + reserve), (int)rec[2],
        (int)((uint)(DAT_8061248c + 0x1f) & 0xffffffe0u), 100, 0, 0, 0);
    if (rc < 0) {
        halt_stub_assert();
    }

    rec[2] = rec[2] + window;
    *(u16 *)(rec + 0x10) |= 8;
    DAT_80612492 |= 2;

    u16 flags = *(u16 *)(rec + 0x10);
    if ((flags & 2) == 0) {
        if ((int)rec[1] <= (int)rec[2]) {
            *(u16 *)(rec + 0x10) = flags | 0x20;
        }
        return;
    }

    uint end = rec[1];
    if (end < rec[2] + 0x8000) {
        bool tail;
        if ((flags & 1) == 0) {
            uint v = (rec[5] - 0x60) & (uint)(-(int)(rec[5] != 0));
            tail = (v < 0x20001);
        } else {
            tail = ((end - 0x60) < 0x40001);
        }

        if (tail) {
            if ((int)end <= (int)rec[2]) {
                *(u16 *)(rec + 0x10) |= 0x800;
            }
        } else {
            u32 rewind = ((flags & 1) != 0) ? 0x60u : 0xc0u;
            rec[2] = rewind;
            *(u8 *)(rec + 0x11) = *(u8 *)((int)rec + 0x42);
        }
        if (*(char *)(rec + 0x11) == '\0') {
            *(u8 *)(rec + 0x11) = 1;
            hwvoice_advance_loop();
        }
    }
}

// ---------------------------------------------------------------------------
// Address: 0x8018a320 | Name: hwvoice_apply_gain | Date: 2026-09-06
// Apply the record's cached gain (rec+0x24) to both mixer channels, scaling by
// the per-channel playback-position ratio (packed 16.16 cursor at +0x30/+0x32
// for the left channel, +0x3c/+0x3e for the right) dequantized against the
// gain magic-bias.
void hwvoice_apply_gain(int rec)
{
    double gain = (double)*(float *)(rec + 0x24);

    if (*(int *)(rec + 0x1c) != 0) {
        uint packed = (uint)*(u16 *)(rec + 0x30) * 0x10000 + (uint)*(u16 *)(rec + 0x32);
        double ratio = (double)((float)((double)CONCAT44(0x43300000, packed ^ 0x80000000u) -
                                        DOUBLE_806167f0) / FLOAT_806167e8);
        FUN_803aee48((double)(float)(gain * ratio), *(int *)(rec + 0x1c));
    }
    if (*(int *)(rec + 0x20) != 0) {
        uint packed = (uint)*(u16 *)(rec + 0x3c) * 0x10000 + (uint)*(u16 *)(rec + 0x3e);
        double ratio = (double)((float)((double)CONCAT44(0x43300000, packed ^ 0x80000000u) -
                                        DOUBLE_806167f0) / FLOAT_806167e8);
        FUN_803aee48((double)(float)(gain * ratio), *(int *)(rec + 0x20));
    }
}

// ===========================================================================
// Batch: batch_488 — hw-voice DVD segment submit + DMA paging + per-voice
//                     service state machine
// ===========================================================================

// ---- Additional raw symbols referenced by the batch_488 functions ----
extern "C" {
// Frame guard pairs used by this batch.
extern unsigned long long FUN_803826d8(void);  // segment-submit frame: hi=rec, lo=voice
extern void FUN_80382724(void);
extern int  FUN_803826e4(void);                 // stream-kick frame: record ptr
extern void FUN_80382730(void);
extern int  FUN_803826c8(void);                 // dma-submit frame: record ptr
extern void FUN_80382714(void);

// AX register setters + DSP DMA enqueue + mixer voice free.
extern int  FUN_803825c0(double x);             // float->int quantize
extern void FUN_803aed0c(int voice, u32 *pCoefs);   // set 10-word ADPCM coefficient block
extern void FUN_803aedb0(int voice, u16 *pGainEnv); // set 7-word gain envelope block
extern void FUN_803abe54(u32 *slot, int rec, int a3, int flag, void *pName,
                         int offset, int len, int completionCb);  // enqueue DSP DMA read
extern void FUN_803aec18(int voice, u16 v);         // set format2 (+0x1a6)
extern void FUN_803ac328(int *voice);               // free mixer voice back to pool
extern void FUN_803aff9c(int voice);                // stop mixer voice

// DVD streaming handle queries (named; padded to the ABI at the call sites).
extern int  dvd2_handle_is_busy(int handle);
extern int  dvd2_handle_close(int handle);

// Data.
extern int  DAT_805ac660;   // DMA-descriptor slot table A (left, stride 0x20)
extern int  DAT_805ac664;   // name slot table A ("none")
extern int  DAT_805ac6e0;   // DMA-descriptor slot table B (right, stride 0x20)
extern int  DAT_805ac6e4;   // name slot table B ("none")
extern int  DAT_805a4660;   // DVD page-name scratch base
extern int  DAT_805a465c;   // default sample-rate seed
extern int  DAT_805ac7a0;   // hw-voice slot state table (0x48 stride, 8 slots)
extern char DAT_805ac7a5;   // hw-voice slot in-use byte (within the 0x48 record)
extern u8   DAT_80612490;   // active DVD-stream slot index
extern float  FLOAT_806167f8;  // ADPCM predictor divisor
extern double DOUBLE_80616800; // ADPCM predictor magic-bias
extern float  FLOAT_80616808;  // gain/rate threshold
}

// Forward declaration: hwvoice_stream_kick submits via hwvoice_dvd_submit_segment.
static void hwvoice_dvd_submit_segment(u32 pNameSlot, u32 voice, u32 loopStart, u32 endAddr,
                                       u16 *pOut, int looping, int fmtNonZero);
void hwvoice_service_stream(u32 arg0, double a2, double a3, u32 arg4, u32 arg5, u32 arg6,
                            u32 arg7, u32 arg8);
void hwvoice_dma_submit_pages(void);
void hwvoice_reset_adpcm_state(int rec);
void hwvoice_update_loop_page(u32 arg0, u32 arg1, uint targetPage);
void hwvoice_release(int *rec);

// ---------------------------------------------------------------------------
// Address: 0x8018a3ec | Name: hwvoice_dvd_submit_segment | Date: 2026-09-06
// Build and submit one DVD-streamed ADPCM segment to a mixer voice. Quantize
// the predictor scale (rec+8) two ways for the coefficient/gain blocks, copy the
// 16-entry ADPCM coefficient table (rec+0x1c) and loop context (rec+0x3c), then
// push the coefficient block (FUN_803aed0c) and gain envelope (FUN_803aedb0).
// When looping, also program the ADPCM loop context + format. The spawn-frame
// pair supplies { record, voice }.
static void hwvoice_dvd_submit_segment(u32 pNameSlot, u32 voice, u32 loopStart, u32 endAddr,
                                       u16 *pOut, int looping, int fmtNonZero)
{
    (void)pNameSlot;
    (void)voice;

    unsigned long long frame = FUN_803826d8();
    int rec = (int)((unsigned long long)frame >> 0x20);
    int mixVoice = (int)frame;

    if (mixVoice != 0) {
        u32 scale = *(u32 *)(rec + 8);
        // Two predictor quantizations (differ by the (double)(float) intermediate
        // rounding in the original); modelled with the same scale/bias math.
        int predA = FUN_803825c0((double)(FLOAT_806167e8 *
                        ((float)((double)(float)((double)CONCAT44(0x43300000, scale) -
                                                 DOUBLE_80616800)) / FLOAT_806167f8)));
        int predB = FUN_803825c0((double)(FLOAT_806167e8 *
                        ((float)((double)CONCAT44(0x43300000, scale) - DOUBLE_80616800) /
                         FLOAT_806167f8)));

        // 10-word coefficient block: 16 shorts of ADPCM coefficients (rec+0x1c)
        // followed by the 4-short loop context (rec+0x3c).
        u32 coefBlock[10];
        u16 *src = (u16 *)(rec + 0x1c);
        u16 *dst = (u16 *)coefBlock;
        for (int i = 0x10; i != 0; i = i - 1) {
            *dst = *src;
            src = src + 1;
            dst = dst + 1;
        }
        ((u16 *)coefBlock)[16] = *(u16 *)(rec + 0x3c);
        ((u16 *)coefBlock)[17] = *(u16 *)(rec + 0x3e);
        ((u16 *)coefBlock)[18] = *(u16 *)(rec + 0x40);
        ((u16 *)coefBlock)[19] = *(u16 *)(rec + 0x42);

        // 7-short gain envelope block.
        u16 gainEnv[7];
        memset_wrapper((int)gainEnv, 0, 0xe);
        gainEnv[0] = (u16)((uint)predA >> 0x10);
        gainEnv[1] = (u16)predA;
        pOut[4] = (short)((uint)predB >> 0x10);
        pOut[5] = (short)predB;
        pOut[0] = *(u16 *)(rec + 0x44);
        pOut[1] = *(u16 *)(rec + 0x46);
        pOut[2] = *(u16 *)(rec + 0x48);

        if (looping != 0) {
            hwvoice_set_adpcm_loop(mixVoice, loopStart, endAddr, fmtNonZero);
            FUN_803aeabc(mixVoice, 1);
        }
        FUN_803aed0c(mixVoice, coefBlock);
        FUN_803aedb0(mixVoice, gainEnv);
    }
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// Address: 0x8018a564 | Name: hwvoice_stream_kick | Date: 2026-09-06
// Arm the next DVD segment for each channel of a hardware voice via
// hwvoice_dvd_submit_segment (left from DAT_805a45a0, right from DAT_805a4600;
// mono uses the right buffer only). On the final segment set the retire state
// (+0x45 = 3, +0x40 |= 0x400). Advances the cursor (+0x10) and re-flags end.
void hwvoice_stream_kick(void)
{
    int rec = FUN_803826e4();
    int cursor = *(int *)(rec + 0xc);
    bool notStarted = *(char *)(rec + 0x45) != '\x04';
    int endR = (*(int *)(rec + 0x18) + 0x20000) * 2 + -1;
    int startR = cursor * 2 + 2;

    uint fmtSel = ((*(u16 *)(rec + 0x40) >> 5) & 1) ^ 1;
    if (!notStarted) {
        fmtSel = 1;
    }
    uint looping = (uint)notStarted;

    if ((*(u16 *)(rec + 0x40) & 1) == 0) {
        hwvoice_dvd_submit_segment((u32)(uintptr_t)&DAT_805a45a0, (u32)*(int *)(rec + 0x1c),
                                   (u32)(*(int *)(rec + 0x18) * 2 + 2), (u32)endR,
                                   (u16 *)(rec + 0x28), (int)looping, (int)fmtSel);
        hwvoice_dvd_submit_segment((u32)(uintptr_t)&DAT_805a4600, (u32)*(int *)(rec + 0x20),
                                   (u32)startR, (u32)((cursor + 0x20000) * 2 + -1),
                                   (u16 *)(rec + 0x34), (int)looping, (int)fmtSel);
    } else {
        hwvoice_dvd_submit_segment((u32)(uintptr_t)&DAT_805a4600, (u32)*(int *)(rec + 0x20),
                                   (u32)startR, (u32)endR,
                                   (u16 *)(rec + 0x34), (int)looping, (int)fmtSel);
    }
    if (looping != 0) {
        *(u8 *)(rec + 0x45) = 3;
        *(u16 *)(rec + 0x40) |= 0x400;
    }
    *(int *)(rec + 0x10) = startR;
    DAT_80612492 = DAT_80612492 & 0xfe;
    hwvoice_set_end_flag(rec);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// Address: 0x8018a6a8 | Name: hwvoice_dvd_read_complete_cb | Date: 2026-09-06
// DSP-DMA read completion callback: reset the 8 page-name slots to "none"
// (0x6e6f6e65), clear the stream refill flags, advance the wrap counter (+0x43),
// and re-arm the stream (hwvoice_stream_kick) or restart playback
// (hwvoice_start) depending on the loop/end flags.
void hwvoice_dvd_read_complete_cb(int cbArg)
{
    int rec = *(int *)(cbArg + 4);
    int off = 0;
    for (int i = 4; i != 0; i = i - 1) {
        *(u32 *)((int)&DAT_805ac664 + off) = 0x6e6f6e65;
        *(u32 *)((int)&DAT_805ac6e4 + off) = 0x6e6f6e65;
        off = off + 0x20;
    }
    DAT_80612492 = DAT_80612492 & 0xfd;
    DAT_80612491 = 0;
    *(u16 *)(rec + 0x40) &= 0xffe7;
    *(char *)(rec + 0x43) = *(char *)(rec + 0x43) + '\x01';

    u16 flags = *(u16 *)(rec + 0x40);
    if ((flags & 2) == 0) {
        if (((flags & 0x20) != 0) && (*(char *)(rec + 0x45) == '\x02')) {
            hwvoice_stream_kick();
        }
    } else if ((flags & 0x800) != 0) {
        hwvoice_start();
    }
}

// ---------------------------------------------------------------------------
// Address: 0x8018a764 | Name: hwvoice_reset_adpcm_state | Date: 2026-09-06
// Reset the ADPCM running state on both mixer channels from the record's stored
// predictor seeds (rec+0x3a right, rec+0x2e left).
void hwvoice_reset_adpcm_state(int rec)
{
    u16 st[3];
    st[1] = 0;
    st[2] = 0;
    if (*(int *)(rec + 0x20) != 0) {
        st[0] = *(u16 *)(rec + 0x3a);
        FUN_803aeee0(*(int *)(rec + 0x20), st);
    }
    if (*(int *)(rec + 0x1c) != 0) {
        st[0] = *(u16 *)(rec + 0x2e);
        FUN_803aeee0(*(int *)(rec + 0x1c), st);
    }
}

// ---------------------------------------------------------------------------
// Address: 0x8018a7d0 | Name: hwvoice_dma_submit_pages | Date: 2026-09-06
// Enqueue the DSP DMA reads that fill the ADPCM page ring for a hardware voice.
// Mono streams issue four staggered 0x1000-byte page reads (the last carrying
// the read-complete callback); stereo issues a single 0x8000-byte read. Seeds
// the per-page ADPCM predictor state on the first page.
void hwvoice_dma_submit_pages(void)
{
    int rec = FUN_803826c8();
    uint page = (uint)*(u8 *)(rec + 0x42);
    *(u8 *)(rec + 0x42) = (u8)((*(u8 *)(rec + 0x42) + 1) & 7);
    *(u16 *)(rec + 0x40) |= 0x10;

    if ((*(u16 *)(rec + 0x40) & 1) == 0) {
        int idx = 3;
        int slotOff = 0x60;
        int pageOff = 0x3000;
        int nameOff = 0x6000;
        do {
            if ((page == 0) && (idx == 0)) {
                *(u16 *)(rec + 0x3a) = (u16)*(u8 *)(nameOff + -0x7fa5a9a0);
                *(u16 *)(rec + 0x2e) = (u16)(&DAT_805a4660)[nameOff];
                if (*(int *)(rec + 0x14) == 0) {
                    *(int *)(rec + 0x14) = DAT_805a465c;
                }
            }
            int chunk = (int)page * 0x4000 + pageOff;
            int cursor = *(int *)(rec + 0xc);
            FUN_803abe54((u32 *)(&DAT_805ac660 + slotOff), rec, 0, 1,
                         &DAT_805a4660 + nameOff, *(int *)(rec + 0x18) + chunk, 0x1000, 0);
            FUN_803abe54((u32 *)(&DAT_805ac6e0 + slotOff), rec, 0, 1,
                         (void *)(nameOff + -0x7fa5a9a0), cursor + chunk, 0x1000,
                         (int)(-(uint)(idx == 0) & 0x8018a6a8));
            idx = idx + -1;
            pageOff = pageOff + -0x1000;
            nameOff = nameOff + -0x2000;
            slotOff = slotOff + -0x20;
        } while (-1 < idx);
    } else {
        if (page == 0) {
            *(u16 *)(rec + 0x3a) = (u16)DAT_805a4660;
        }
        FUN_803abe54((u32 *)&DAT_805ac6e0, rec, 0, 1, &DAT_805a4660,
                     *(int *)(rec + 0xc) + (int)page * 0x8000, 0x8000, (int)0x8018a6a8);
    }
    DAT_80612491 = 3;
    if ((page == 0) && ((*(u16 *)(rec + 0x40) & 0x80) == 0)) {
        hwvoice_reset_adpcm_state(rec);
    }
    FUN_80382714();
}

// ---------------------------------------------------------------------------
// Address: 0x8018a990 | Name: hwvoice_update_loop_page | Date: 2026-09-06
// Advance the mixer voices' loop/end addresses to the next page when the target
// page differs from the record's current page (+0x44). Recomputes per-channel
// start/end from the block size (0x4000 mono / 0x8000 stereo). The spawn-frame
// pair supplies { record, currentPage }.
void hwvoice_update_loop_page(u32 arg0, u32 arg1, uint targetPage)
{
    (void)arg0;
    (void)arg1;

    unsigned long long frame = FUN_803826e8();
    int rec = (int)((unsigned long long)frame >> 0x20);
    uint page = ((uint)(int)frame + 1) & ~(uint)(-(int)(*(char *)(rec + 0x44) == '\x07'));

    if (page != targetPage) {
        int block;
        int tail;
        if ((*(u16 *)(rec + 0x40) & 1) == 0) {
            block = 0x4000;
            tail = (int)(((uint)*(int *)(rec + 4) - 0xc0) >> 1) - (*(int *)(rec + 0x14) + -0x60);
        } else {
            block = 0x8000;
            tail = (int)(((uint)*(int *)(rec + 4) + 0x7f9f) & 0xffff8000) -
                   (*(int *)(rec + 4) + -0x60);
        }
        int endDelta = (block - tail) + (int)frame * block;

        if (*(int *)(rec + 0x20) != 0) {
            int cur = *(int *)(rec + 0xc);
            FUN_803aec64(*(int *)(rec + 0x20), (u32)((cur + page * block) * 2 + 2));
            FUN_803aecb8(*(int *)(rec + 0x20), (u32)((cur + endDelta) * 2 + -1));
            FUN_803aeabc(*(int *)(rec + 0x20), 0);
        }
        if (*(int *)(rec + 0x1c) != 0) {
            int cur = *(int *)(rec + 0x18);
            FUN_803aec64(*(int *)(rec + 0x1c), (u32)((cur + page * block) * 2 + 2));
            FUN_803aecb8(*(int *)(rec + 0x1c), (u32)((cur + endDelta) * 2 + -1));
            FUN_803aeabc(*(int *)(rec + 0x1c), 0);
        }
        *(u16 *)(rec + 0x40) |= 0x80;
        *(char *)(rec + 0x44) = (char)page;
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// Address: 0x8018aacc | Name: hwvoice_service_stream | Date: 2026-09-06
// Per-frame stream servicer for a playing hardware voice (state 2). Refill the
// DVD double-buffer (hwvoice_stream_fill) or issue the DSP page DMAs
// (hwvoice_dma_submit_pages) depending on the refill state and whether the DVD
// handle is busy; commit finished ADPCM pages to the mixer voices; and advance
// the loop page (hwvoice_update_loop_page) when playback crosses a page.
void hwvoice_service_stream(u32 arg0, double a2, double a3, u32 arg4, u32 arg5, u32 arg6,
                            u32 arg7, u32 arg8)
{
    int *rec = (int *)FUN_803826f0();

    if (DAT_80612491 == 0) {
        if ((uint)DAT_80612490 == (int)((intptr_t)rec + 0x7fa538a0) / 0x48) {
            hwvoice_stream_fill(0, a2, a3, arg4, arg5, arg6, arg7, arg8, (u32 *)rec, 0);
        }
    } else if (((DAT_80612491 == 2) || (DAT_80612491 == 1)) &&
               ((*(u16 *)(rec + 0x10) & 8) != 0) && (dvd2_handle_is_busy(rec[0]) != 0)) {
        if (DAT_80612491 == 1) {
            hwvoice_stream_fill(0, a2, a3, arg4, arg5, arg6, arg7, arg8, (u32 *)rec, 0);
        } else {
            hwvoice_dma_submit_pages();
        }
    }

    if (((*(u16 *)(rec + 0x10) & 0x20) != 0) && (*(char *)((int)rec + 0x45) == '\x03') &&
        (((*(u8 *)((int)rec + 0x43) == 2 && *(char *)((int)rec + 0x42) != '\x01') ||
          (*(u8 *)((int)rec + 0x43) < 2))) && ((*(u16 *)(rec + 0x10) & 0x40) == 0)) {
        int block = 0x4000;
        *(u16 *)(rec + 0x10) |= 0x40;
        if ((*(u16 *)(rec + 0x10) & 1) != 0) {
            block = 0x8000;
        }
        uint page = hwvoice_compute_page((int)rec, *(int *)(rec[8] + 0x1b2));
        int tail;
        if ((*(u16 *)(rec + 0x10) & 1) == 0) {
            tail = (int)(((uint)rec[2] - 0xc0) >> 1) - rec[5];
        } else {
            tail = rec[2] - rec[1];
        }
        if ((*(char *)((int)rec + 0x43) != '\x01') && (page = page + 1, page == 8)) {
            page = 0;
        }
        int endDelta = (block - tail) + (int)(page * block);
        if (rec[7] != 0) {
            int cur = rec[6];
            FUN_803aeabc(rec[7], 0);
            FUN_803aeee0(rec[7], (u16 *)(rec + 10));
            FUN_803aecb8(rec[7], (u32)((cur + endDelta) * 2 + -1));
            FUN_803aec18(rec[7], 0);
        }
        if (rec[8] != 0) {
            int cur = rec[3];
            FUN_803aeabc(rec[8], 0);
            FUN_803aeee0(rec[8], (u16 *)(rec + 0xd));
            FUN_803aecb8(rec[8], (u32)((cur + endDelta) * 2 + -1));
            FUN_803aec18(rec[8], 0);
        }
    }

    if (((*(u16 *)(rec + 0x10) & 0x80) == 0) && (-1 < *(char *)(rec + 0x11))) {
        uint page = hwvoice_compute_page((int)rec, *(int *)(rec[8] + 0x1b2));
        if (page <= (uint)(int)*(char *)(rec + 0x11)) {
            hwvoice_update_loop_page((u32)(uintptr_t)rec, (u32)(int)*(char *)(rec + 0x11), page);
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// Address: 0x8018ad2c | Name: hwvoice_release | Date: 2026-09-06
// Release a hardware voice: stop and free both mixer channels (FUN_803aff9c +
// FUN_803ac328), then either close the DVD handle immediately (no pending read)
// or defer the close by moving to state 6/7 until the in-flight read finishes.
void hwvoice_release(int *rec)
{
    FUN_8039cca0();
    if (rec[7] != 0) {
        FUN_803aff9c(rec[7]);
        FUN_803ac328((int *)rec[7]);
        rec[7] = 0;
    }
    if (rec[8] != 0) {
        FUN_803aff9c(rec[8]);
        FUN_803ac328((int *)rec[8]);
        rec[8] = 0;
    }
    if (*(char *)((int)rec + 0x45) == '\x02') {
        DAT_80612492 = DAT_80612492 & 0xfe;
    }
    if ((*(u16 *)(rec + 0x10) & 8) == 0) {
        dvd2_handle_close(rec[0]);
        rec[0] = -1;
        *(u8 *)((int)rec + 0x45) = 0;
    } else {
        *(u16 *)(rec + 0x10) &= 0xf7ff;
        if ((*(u16 *)(rec + 0x10) & 0x10) == 0) {
            *(u8 *)((int)rec + 0x45) = 6;
        } else {
            *(u8 *)((int)rec + 0x45) = 7;
        }
    }
    FUN_8039ccc8();
}

// ---------------------------------------------------------------------------
// Address: 0x8018ae18 | Name: hwvoice_find_free_slot | Date: 2026-09-06
// Scan the 8-entry hardware-voice slot table (0x48 stride) for the first free
// slot (in-use byte +0x45 clear and no 0x200 flag at +0x40). Returns the slot
// index, or -1 when all are busy.
int hwvoice_find_free_slot(void)
{
    int idx = 0;
    int off = 0;
    for (int guard = 8; guard != 0; guard = guard - 1) {
        if (((&DAT_805ac7a5)[off] == '\0') &&
            ((*(u16 *)((int)&DAT_805ac7a0 + off) & 0x200) == 0)) {
            return idx;
        }
        idx = idx + 1;
        off = off + 0x48;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Address: 0x8018ae60 | Name: hwvoice_service | Date: 2026-09-06
// Main per-voice service dispatcher (called once per frame per hardware voice).
// First applies any pending fade command (+0x46: 1 = fade to silence, 2 = restore
// gain), then runs the state machine on +0x45: 1 = initial DVD fill, 2 = stream
// service, 3 = playing (gain/page update + stream service), 5 = release, 6/7 =
// deferred DVD-handle close.
void hwvoice_service(u32 arg0, double a2, double a3, u32 arg4, u32 arg5, u32 arg6,
                     u32 arg7, u32 arg8)
{
    int *rec = (int *)FUN_803826f0();

    if (*(char *)((int)rec + 0x46) != '\0') {
        u16 flags = *(u16 *)(rec + 0x10);
        if ((flags & 4) == 0) {
            if (*(char *)((int)rec + 0x46) == '\x01') {
                *(u16 *)(rec + 0x10) = flags | 4;
                if (rec[7] != 0) { FUN_803aee48((double)FLOAT_80616808, rec[7]); }
                if (rec[8] != 0) { FUN_803aee48((double)FLOAT_80616808, rec[8]); }
            }
        } else if (*(char *)((int)rec + 0x46) == '\x02') {
            *(u16 *)(rec + 0x10) = flags & 0xfffb;
            hwvoice_apply_gain((int)rec);
        }
    }
    *(u8 *)((int)rec + 0x46) = 0;

    switch (*(u8 *)((int)rec + 0x45)) {
    case 1:
        if ((DAT_80612492 & 3) == 0) {
            hwvoice_stream_fill(0, a2, a3, arg4, arg5, arg6, arg7, arg8, (u32 *)rec, 1);
        }
        break;
    case 2:
        hwvoice_service_stream(0, a2, a3, arg4, arg5, arg6, arg7, arg8);
        if ((7 < *(u8 *)((int)rec + 0x43)) && ((*(u16 *)(rec + 0x10) & 4) == 0) &&
            (FLOAT_80616808 < *(float *)&rec[9])) {
            hwvoice_stream_kick();
        }
        break;
    case 3: {
        int voice = rec[8];
        if (voice != 0) {
            if (*(short *)(voice + 0x146) == 0) {
                *(u8 *)((int)rec + 0x45) = 5;
            } else {
                int loopAddr = *(int *)(voice + 0x1b2);
                if (*(char *)((int)rec + 0x43) == '\0') {
                    if (((*(u16 *)(rec + 0x10) & 0x20) == 0) &&
                        ((*(u16 *)(rec + 0x10) & 0x100) == 0)) {
                        if (rec[7] != 0) { FUN_803aee48((double)FLOAT_80616808, rec[7]); }
                        if (rec[8] != 0) { FUN_803aee48((double)FLOAT_80616808, rec[8]); }
                        *(u16 *)(rec + 0x10) |= 0x100;
                    }
                } else {
                    if (((*(u16 *)(rec + 0x10) & 0x100) != 0) &&
                        ((*(u16 *)(rec + 0x10) & 4) == 0)) {
                        hwvoice_apply_gain((int)rec);
                        *(u16 *)(rec + 0x10) &= 0xfeff;
                    }
                    uint pageA = hwvoice_compute_page((int)rec, loopAddr);
                    uint pageB = hwvoice_compute_page((int)rec, rec[4]);
                    if ((pageA != pageB) && (*(char *)((int)rec + 0x43) != '\0') &&
                        (*(char *)((int)rec + 0x43) = *(char *)((int)rec + 0x43) + -1,
                         (*(u16 *)(rec + 0x10) & 0x80) != 0) &&
                        (pageA == (uint)(int)*(char *)(rec + 0x11))) {
                        hwvoice_advance_loop();
                        hwvoice_reset_adpcm_state((int)rec);
                        *(u16 *)(rec + 0x10) &= 0xff7f;
                        *(u8 *)(rec + 0x11) = 0xff;
                    }
                }
                hwvoice_service_stream(0, a2, a3, arg4, arg5, arg6, arg7, arg8);
                rec[4] = loopAddr;
            }
        }
        break;
    }
    case 5:
        hwvoice_release(rec);
        break;
    case 6:
        if (dvd2_handle_is_busy(rec[0]) != 0) {
            dvd2_handle_close(rec[0]);
            rec[0] = -1;
            *(u8 *)((int)rec + 0x45) = 0;
            DAT_80612492 = DAT_80612492 & 0xfd;
            DAT_80612491 = 0;
        }
        break;
    case 7:
        if ((*(u16 *)(rec + 0x10) & 0x10) == 0) {
            dvd2_handle_close(rec[0]);
            rec[0] = -1;
            *(u8 *)((int)rec + 0x45) = 0;
        }
        break;
    }
    FUN_8038273c();
}
