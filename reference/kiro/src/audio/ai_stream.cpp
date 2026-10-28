//
// Address: 0x803a9880 | Name: ai_set_stream_sample_rate | Date: 2026-09-15
//
// AI (Audio Interface) streaming sample-rate programming. The GameCube DSP/AI
// block exposes the streaming ARAM DMA sample-rate divider through a set of
// 16-bit registers in the DSP register window (0xcc0050xx). This routine packs
// two rate/divider values (the ARAM stream rate and the sample-count reload)
// into those registers under an interrupt lock, preserving the reserved/control
// bits in each register. Called by the THP video/audio player when (re)starting
// the audio stream for a movie.
//
// NON-MATCHING: the DSP registers are reached through per-file volatile externs
// at their hardware addresses; the os_* interrupt-lock helpers resolve via the
// generated decls. The bit layout matches the original store sequence verbatim.

#include "common.h"

extern "C" {

// DSP register window (AI streaming rate/divider registers).
extern volatile unsigned short DAT_cc005030;  // stream rate hi (top 6 bits reserved)
extern volatile unsigned short DAT_cc005032;  // stream rate lo (low 5 bits reserved)
extern volatile unsigned short DAT_cc005036;  // sample-count reload (bit 15 reserved)

extern void os_disable_interrupts(void);
extern int  os_get_interrupts_enabled(void);

// ============================================================================
// ai_set_stream_sample_rate (0x803a9880): program the AI stream rate registers.
//   rate       - packed 32-bit stream rate; hi 16 bits -> reg030, lo -> reg032
//   sampleCount- sample-count reload; (>>5) stored into reg036 low 15 bits
// ============================================================================
void ai_set_stream_sample_rate(unsigned int rate, unsigned int sampleCount)
{
    unsigned short r;

    os_disable_interrupts();
    r = DAT_cc005030;
    DAT_cc005030 = (unsigned short)((r & 0xfc00) | (unsigned short)(rate >> 0x10));
    r = DAT_cc005032;
    DAT_cc005032 = (unsigned short)((r & 0x1f) | (unsigned short)rate);
    r = DAT_cc005036;
    DAT_cc005036 = (unsigned short)((r & 0x8000) | (unsigned short)(sampleCount >> 5));
    os_get_interrupts_enabled();
}

} // extern "C"

// ============================================================================
// batch_1129: AI (Audio Interface) stream/DSP control-register layer.
//
// Address: 0x803a9908 | Name: ai_enable_stream_sample_count   | Date: 2026-09-15
// Address: 0x803a9920 | Name: ai_disable_stream_sample_count  | Date: 2026-09-15
// Address: 0x803a9938 | Name: ai_get_stream_sample_rate_regs  | Date: 2026-09-15
// Address: 0x803a9954 | Name: ai_set_dsp_sample_rate          | Date: 2026-09-15
// Address: 0x803a9a2c | Name: ai_get_dsp_sample_rate          | Date: 2026-09-15
// Address: 0x803a9a3c | Name: ai_set_stream_play_state        | Date: 2026-09-15
// Address: 0x803a9b1c | Name: ai_get_stream_play_state        | Date: 2026-09-15
// Address: 0x803a9b30 | Name: ai_set_stream_sample_rate_afr   | Date: 2026-09-15
// Address: 0x803a9c04 | Name: ai_get_stream_sample_rate_afr   | Date: 2026-09-15
// Address: 0x803a9c14 | Name: ai_set_stream_vol_left          | Date: 2026-09-15
//
// The AI control register (AICR) at 0xcc006c00 holds:
//   bit0  DSP sample-rate select
//   bit1  AFR stream sample-rate select
//   bit5  SCRESET (sample-count reset pulse)
//   bit6  stream play / interrupt enable (read inverted for "playing")
// The stream volume register (left) is 0xcc006c04 (low byte). The DSP streaming
// rate/count regs 0xcc005030/32/36 live in the DSP register window. Changing
// either sample-rate bit requires a save/disable, SCRESET pulse, restore dance
// under an interrupt lock so the ARAM DMA is re-synced.
//
// NON-MATCHING: registers reached via per-file volatile externs; the AI reset
// helper (FUN_803a9f58) and the not-yet-decompiled DMA getters (FUN_803a9c30,
// FUN_803a9c40, FUN_803a9c5c) resolve via per-file externs.

extern volatile unsigned int   DAT_cc006c00;  // AICR (AI control register)
extern volatile unsigned int   DAT_cc006c04;  // AI volume register (left = low byte)

// Not-yet-decompiled AI DMA state accessors (later batch) and reset helper.
extern unsigned int FUN_803a9c30(void);   // get DMA start-addr / length hi
extern void         FUN_803a9c40(unsigned int v);
extern unsigned int FUN_803a9c5c(void);   // get DMA length
extern void         FUN_803a9f58(void);   // AI reset helper

// Intra-file forward decls (the setters call each other and the getters).
unsigned int ai_get_stream_sample_rate_afr(void);
unsigned int ai_get_dsp_sample_rate(void);
unsigned int ai_get_stream_play_state(void);
void         ai_set_dsp_sample_rate(unsigned int rate);
void         ai_set_stream_vol_left(unsigned int vol);

// ============================================================================
// ai_enable_stream_sample_count (0x803a9908)
// ============================================================================
void ai_enable_stream_sample_count(void)
{
    DAT_cc005036 = (unsigned short)(DAT_cc005036 | 0x8000);
}

// ============================================================================
// ai_disable_stream_sample_count (0x803a9920)
// ============================================================================
void ai_disable_stream_sample_count(void)
{
    DAT_cc005036 = (unsigned short)(DAT_cc005036 & 0x7fff);
}

// ============================================================================
// ai_get_stream_sample_rate_regs (0x803a9938)
// ============================================================================
unsigned int ai_get_stream_sample_rate_regs(void)
{
    unsigned short hi = DAT_cc005030;
    unsigned short lo = DAT_cc005032;
    return ((unsigned int)(hi & 0x3ff) << 0x10) | (unsigned int)(lo & 0xffe0);
}

// ============================================================================
// ai_set_dsp_sample_rate (0x803a9954): AICR bit0.
// ============================================================================
void ai_set_dsp_sample_rate(unsigned int rate)
{
    unsigned int cur;
    unsigned int savedVol;
    unsigned int savedRate;

    cur = ai_get_dsp_sample_rate();
    if (rate != cur) {
        if ((ai_get_stream_sample_rate_afr() == 0) && (rate == 1)) {
            savedVol  = FUN_803a9c5c();
            savedRate = FUN_803a9c30();
            FUN_803a9c40(0);
            ai_set_stream_vol_left(0);
            os_disable_interrupts();
            FUN_803a9f58();
            DAT_cc006c00 = (DAT_cc006c00 & 0xffffffdf) | 0x20;   /* SCRESET pulse */
            DAT_cc006c00 = (DAT_cc006c00 & 0xfffffffe) | 1;      /* bit0 = 1 */
            os_get_interrupts_enabled();
            ai_set_stream_vol_left(savedVol);
            FUN_803a9c40(savedRate);
        } else {
            DAT_cc006c00 = (DAT_cc006c00 & 0xfffffffe) | rate;
        }
    }
}

// ============================================================================
// ai_get_dsp_sample_rate (0x803a9a2c): AICR bit0.
// ============================================================================
unsigned int ai_get_dsp_sample_rate(void)
{
    return DAT_cc006c00 & 1;
}

// ============================================================================
// ai_set_stream_play_state (0x803a9a3c): AICR bit6, with reset on disable.
// ============================================================================
void ai_set_stream_play_state(unsigned int state)
{
    unsigned int cur;
    unsigned int savedVol;
    unsigned int savedRate;
    unsigned int savedDspRate;
    unsigned int afr;

    cur = ai_get_stream_play_state();
    if (state != cur) {
        DAT_cc006c00 = DAT_cc006c00 & 0xffffffbf;      /* clear bit6 */
        if (state == 0) {
            savedRate    = FUN_803a9c30();
            savedVol     = FUN_803a9c5c();
            savedDspRate = DAT_cc006c00;
            afr          = ai_get_stream_sample_rate_afr();
            ai_set_stream_vol_left(0);
            FUN_803a9c40(0);
            os_disable_interrupts();
            FUN_803a9f58();
            DAT_cc006c00 = (DAT_cc006c00 & 0xffffffdf) | 0x20;              /* SCRESET */
            DAT_cc006c00 = (DAT_cc006c00 & 0xfffffffd) | (afr << 1);        /* bit1 = afr */
            DAT_cc006c00 = (DAT_cc006c00 & 0xfffffffe) | (savedDspRate & 1);/* bit0 */
            DAT_cc006c00 = DAT_cc006c00 | 0x40;                            /* bit6 */
            os_get_interrupts_enabled();
            ai_set_stream_vol_left(savedRate);
            FUN_803a9c40(savedVol);
        }
    }
}

// ============================================================================
// ai_get_stream_play_state (0x803a9b1c): inverted AICR bit6.
// ============================================================================
unsigned int ai_get_stream_play_state(void)
{
    return (DAT_cc006c00 >> 6 & 1) ^ 1;
}

// ============================================================================
// ai_set_stream_sample_rate_afr (0x803a9b30): AICR bit1, with reset dance.
// ============================================================================
void ai_set_stream_sample_rate_afr(unsigned int afr)
{
    unsigned int cur;
    unsigned int savedDspRate;
    unsigned int savedRate;
    unsigned int savedVol;
    unsigned int playBit;

    cur = ai_get_stream_sample_rate_afr();
    if (afr != cur) {
        savedDspRate = DAT_cc006c00;
        savedRate    = FUN_803a9c30();
        savedVol     = FUN_803a9c5c();
        FUN_803a9c40(0);
        ai_set_stream_vol_left(0);
        playBit = DAT_cc006c00;
        DAT_cc006c00 = DAT_cc006c00 & 0xffffffbf;                 /* clear bit6 */
        os_disable_interrupts();
        FUN_803a9f58();
        DAT_cc006c00 = DAT_cc006c00 | (playBit & 0x40);           /* restore bit6 */
        DAT_cc006c00 = (DAT_cc006c00 & 0xffffffdf) | 0x20;        /* SCRESET */
        DAT_cc006c00 = (DAT_cc006c00 & 0xfffffffd) | (afr << 1);  /* bit1 = afr */
        os_get_interrupts_enabled();
        ai_set_dsp_sample_rate(savedDspRate & 1);
        ai_set_stream_vol_left(savedRate);
        FUN_803a9c40(savedVol);
    }
}

// ============================================================================
// ai_get_stream_sample_rate_afr (0x803a9c04): AICR bit1.
// ============================================================================
unsigned int ai_get_stream_sample_rate_afr(void)
{
    return DAT_cc006c00 >> 1 & 1;
}

// ============================================================================
// ai_set_stream_vol_left (0x803a9c14): low byte of 0xcc006c04.
// ============================================================================
void ai_set_stream_vol_left(unsigned int vol)
{
    DAT_cc006c04 = (vol & 0xff) | (DAT_cc006c04 & 0xffffff00);
}
