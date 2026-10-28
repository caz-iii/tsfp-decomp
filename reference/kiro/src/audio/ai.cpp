//
// AI (Audio Interface) driver core: init, interrupt handlers, ARAM sample-count
// reset/calibration, stream volume byte accessors, and the callback setter.
//
// batch_1130:
// Address: 0x803a9c30 | Name: ai_get_stream_vol_left       | Date: 2026-09-15
// Address: 0x803a9c40 | Name: ai_set_stream_vol_right      | Date: 2026-09-15
// Address: 0x803a9c5c | Name: ai_get_stream_vol_right      | Date: 2026-09-15
// Address: 0x803a9c6c | Name: AIInit                       | Date: 2026-09-15
// Address: 0x803a9dd8 | Name: ai_dma_interrupt_handler     | Date: 2026-09-15
// Address: 0x803a9e54 | Name: ai_dsp_interrupt_handler     | Date: 2026-09-15
// Address: 0x803a9f00 | Name: ai_run_callback_with_stack   | Date: 2026-09-15
// Address: 0x803a9f58 | Name: ai_reset_stream_sample_count | Date: 2026-09-15
// Address: 0x803aa13c | Name: ai_set_callback              | Date: 2026-09-15
//
// The AI volume register 0xcc006c04 packs left (low byte) and right (byte1)
// stream volume. The AI control register (AICR) 0xcc006c00 holds the rate/reset/
// play bits; 0xcc006c08 is the running sample-count register; 0xcc006c0c the
// interrupt-count register. AIInit computes calibration windows (measured in
// bus-clock ticks) so ai_reset_stream_sample_count can land the ARAM sample
// count in the valid band before the stream starts.
//
// NON-MATCHING: hardware registers reached via per-file volatile externs; the
// pure stream-rate/play accessors live in ai_stream.cpp and resolve via per-file
// externs; os_* helpers resolve via the generated decls (declared per-file since
// ai.cpp includes only common.h). The interrupt/calibration control flow is
// preserved verbatim.

#include "common.h"

extern "C" {

// ---- AI / DSP hardware registers ----
extern volatile unsigned int   DAT_cc006c00;  // AICR (control)
extern volatile unsigned int   DAT_cc006c04;  // volume (L=low byte, R=byte1)
extern volatile unsigned int   DAT_cc006c08;  // AI sample-count register
extern volatile unsigned int   DAT_cc006c0c;  // AI interrupt-count register
extern volatile unsigned short DAT_cc00500a;  // DSP mailbox/int-status reg

// ---- AI driver state (0x806132xx / 0x8061147x) ----
extern int          DAT_80611470;  // AIInit version string
extern unsigned int DAT_800000f8;  // bus clock (Hz) from low memory
extern void       (*DAT_80613280)(unsigned int dmaAddr); // DMA-complete callback
extern void       (*DAT_80613284)(void);                 // stream callback
extern int          DAT_80613288;  // "use private stack for stream callback"
extern int          DAT_8061328c;  // saved stack marker for stream callback
extern int          DAT_80613290;  // "AIInit already ran" latch
extern int          DAT_80613294;  // stream-callback re-entrancy guard
extern int          DAT_806132c0;  // generic AI callback slot (ai_set_callback)

// Calibration windows + accumulators (64-bit split lo/hi across pairs).
extern unsigned int DAT_80613298, DAT_8061329c;  // window A hi/lo-delta
extern unsigned int DAT_806132a0, DAT_806132a4;  // window B
extern unsigned int DAT_806132a8, DAT_806132ac;  // result A
extern unsigned int DAT_806132b0, DAT_806132b4;  // result B
extern unsigned int DAT_806132b8, DAT_806132bc;  // tolerance

// ---- pure stream-rate/play accessors (ai_stream.cpp) ----
extern void ai_set_stream_sample_rate_afr(unsigned int afr);
extern void ai_set_stream_play_state(unsigned int state);

// ---- OS helpers ----
extern void               os_register_version(int a, ...);
extern void               os_set_interrupt_handler(int idx, void *handler);
extern void               os_unmask_interrupts(unsigned int mask);
extern void               os_clear_context(int ctx);
extern void               os_set_current_context(unsigned int ctx);
extern unsigned long long os_read_time_base(void);
extern void               os_disable_interrupts(void);
extern int                os_get_interrupts_enabled(void);

// Forward decls (AIInit installs these; the DSP handler chains the runner).
void ai_dma_interrupt_handler(unsigned int unused, unsigned int savedCtx);
void ai_dsp_interrupt_handler(unsigned int unused, unsigned int savedCtx);
void ai_run_callback_with_stack(void (*cb)(void));

// ============================================================================
// ai_get_stream_vol_left (0x803a9c30)
// ============================================================================
unsigned int ai_get_stream_vol_left(void)
{
    return DAT_cc006c04 & 0xff;
}

// ============================================================================
// ai_set_stream_vol_right (0x803a9c40): byte1 of 0xcc006c04.
// ============================================================================
void ai_set_stream_vol_right(unsigned int vol)
{
    DAT_cc006c04 = ((vol & 0xff) << 8) | (DAT_cc006c04 & 0xffff00ff);
}

// ============================================================================
// ai_get_stream_vol_right (0x803a9c5c): byte1 of 0xcc006c04.
// ============================================================================
unsigned int ai_get_stream_vol_right(void)
{
    return DAT_cc006c04 >> 8 & 0xff;
}

// ============================================================================
// AIInit (0x803a9c6c): one-time AI init; installs the DSP/AI interrupt handlers.
// ============================================================================
void AIInit(void *callback)
{
    unsigned int busDiv;
    unsigned int cr;
    unsigned int vol;

    if (DAT_80613290 != 1) {
        os_register_version(DAT_80611470);
        busDiv = DAT_800000f8 / 500000;
        DAT_8061329c = (busDiv * 0x7b24) / 8000;
        DAT_806132a4 = (busDiv * 0xa428) / 8000;
        DAT_806132ac = (busDiv * 42000) / 8000;
        DAT_806132b4 = (busDiv * 63000) / 8000;
        DAT_806132bc = (busDiv * 3000) / 8000;
        cr  = DAT_cc006c00;
        DAT_80613298 = 0;
        DAT_806132a0 = 0;
        DAT_806132a8 = 0;
        DAT_806132b0 = 0;
        DAT_806132b8 = 0;
        vol = DAT_cc006c04;
        DAT_cc006c00 = (cr & 0xffffffdf) | 0x20;   /* SCRESET */
        DAT_cc006c04 = vol & 0xffff00ff;
        DAT_cc006c04 = DAT_cc006c04 & 0xffffff00;
        DAT_cc006c0c = 0;
        ai_set_stream_sample_rate_afr(1);
        ai_set_stream_play_state(0);
        DAT_80613280 = 0;
        DAT_80613284 = 0;
        DAT_80613288 = (int)callback;
        os_set_interrupt_handler(5, (void *)ai_dsp_interrupt_handler);
        os_unmask_interrupts(0x4000000);
        os_set_interrupt_handler(8, (void *)ai_dma_interrupt_handler);
        os_unmask_interrupts(0x800000);
        DAT_80613290 = 1;
    }
}

// ============================================================================
// ai_dma_interrupt_handler (0x803a9dd8): AI DMA completion interrupt (int 8).
// ============================================================================
void ai_dma_interrupt_handler(unsigned int unused, unsigned int savedCtx)
{
    unsigned char ctx[712];
    unsigned int dmaAddr;

    (void)unused;
    DAT_cc006c00 = DAT_cc006c00 | 8;   /* ack */
    os_clear_context((int)ctx);
    os_set_current_context((unsigned int)ctx);
    if (DAT_80613280 != 0) {
        dmaAddr = DAT_cc006c08;
        (*DAT_80613280)(dmaAddr);
    }
    os_clear_context((int)ctx);
    os_set_current_context(savedCtx);
}

// ============================================================================
// ai_dsp_interrupt_handler (0x803a9e54): DSP mailbox interrupt (int 5).
// ============================================================================
void ai_dsp_interrupt_handler(unsigned int unused, unsigned int savedCtx)
{
    unsigned char ctx[716];

    (void)unused;
    DAT_cc00500a = (unsigned short)((DAT_cc00500a & 0xff5f) | 8);  /* ack */
    os_clear_context((int)ctx);
    os_set_current_context((unsigned int)ctx);
    if ((DAT_80613284 != 0) && (DAT_80613294 == 0)) {
        DAT_80613294 = 1;
        if (DAT_80613288 == 0) {
            (*DAT_80613284)();
        } else {
            ai_run_callback_with_stack(DAT_80613284);
        }
        DAT_80613294 = 0;
    }
    os_clear_context((int)ctx);
    os_set_current_context(savedCtx);
}

// ============================================================================
// ai_run_callback_with_stack (0x803a9f00): record stack marker then call cb.
// ============================================================================
void ai_run_callback_with_stack(void (*cb)(void))
{
    int stackMarker;
    DAT_8061328c = (int)&stackMarker;
    (*cb)();
}

// ============================================================================
// ai_reset_stream_sample_count (0x803a9f58): ARAM SCRESET calibration loop.
// Pulses the sample-count reset and toggles the rate bits while timing the AI
// sample-count register against the PowerPC time base, comparing the measured
// interval against the precomputed calibration windows, then busy-waits the
// trailing settle interval. (64-bit compares preserved from the decompiler.)
// ============================================================================
void ai_reset_stream_sample_count(void)
{
    bool done;
    unsigned int cr;
    int cnt0, cnt1;
    unsigned int deltaLo;
    unsigned int deltaHi;
    unsigned int resLo;
    unsigned int resHi;
    long long t0;
    long long t1;
    unsigned long long tb;

    t1 = 0;
    done = false;
    resLo = 0;
    resHi = 0;
    while (!done) {
        cr = DAT_cc006c00; DAT_cc006c00 = (cr & 0xffffffdf) | 0x20;  /* SCRESET */
        cr = DAT_cc006c00; DAT_cc006c00 = cr & 0xfffffffd;           /* rate lo=0 */
        cr = DAT_cc006c00; DAT_cc006c00 = (cr & 0xfffffffe) | 1;     /* start */
        cnt0 = (int)DAT_cc006c08;
        do { cnt1 = (int)DAT_cc006c08; } while (cnt0 == cnt1);
        t0 = (long long)os_read_time_base();

        cr = DAT_cc006c00; DAT_cc006c00 = (cr & 0xfffffffd) | 2;     /* rate lo=1 */
        cr = DAT_cc006c00; DAT_cc006c00 = (cr & 0xfffffffe) | 1;
        cnt0 = (int)DAT_cc006c08;
        do { cnt1 = (int)DAT_cc006c08; } while (cnt0 == cnt1);
        t1 = (long long)os_read_time_base();

        deltaLo = (unsigned int)t1 - (unsigned int)t0;
        cr = DAT_cc006c00;
        deltaHi = (unsigned int)((int)((unsigned long long)t1 >> 0x20)
                    - ((unsigned int)((unsigned int)t1 < (unsigned int)t0)
                       + (int)((unsigned long long)t0 >> 0x20))) ^ 0x80000000;
        DAT_cc006c00 = cr & 0xfffffffd;
        cr = DAT_cc006c00; DAT_cc006c00 = cr & 0xfffffffe;          /* stop */

        if (deltaHi < (unsigned int)((unsigned int)(deltaLo < DAT_8061329c - DAT_806132bc)
                + (DAT_80613298 - ((unsigned int)(DAT_8061329c < DAT_806132bc) + DAT_806132b8)
                   ^ 0x80000000))) {
            done = true;
            resLo = DAT_806132ac;
            resHi = DAT_806132a8;
        } else if ((deltaHi < (unsigned int)((unsigned int)(deltaLo < DAT_8061329c + DAT_806132bc)
                       + (DAT_80613298 + DAT_806132b8
                          + (unsigned int)((unsigned int)(0xffffffffu - DAT_8061329c < DAT_806132bc))
                          ^ 0x80000000)))
                   || ((unsigned int)((unsigned int)(deltaLo < DAT_806132a4 - DAT_806132bc)
                          + (DAT_806132a0 - ((unsigned int)(DAT_806132a4 < DAT_806132bc) + DAT_806132b8)
                             ^ 0x80000000)) <= deltaHi)) {
            done = false;
        } else {
            done = true;
            resLo = DAT_806132b4;
            resHi = DAT_806132b0;
        }
    }
    t1 = t1 + (long long)(((unsigned long long)resHi << 32) | resLo);
    do {
        tb = os_read_time_base();
    } while ((((unsigned int)((unsigned long long)tb >> 0x20) ^ 0x80000000)
              < (unsigned int)((unsigned int)tb < (unsigned int)t1)
                + ((unsigned int)((unsigned long long)t1 >> 0x20) ^ 0x80000000)));
}

// ============================================================================
// ai_set_callback (0x803aa13c): atomically install a callback pointer.
// ============================================================================
int ai_set_callback(int cb)
{
    int prev;
    prev = DAT_806132c0;
    os_disable_interrupts();
    DAT_806132c0 = cb;
    os_get_interrupts_enabled();
    return prev;
}

} // extern "C"
