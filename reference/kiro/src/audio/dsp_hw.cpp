// TSFP GameCube DSP/AI hardware command + interrupt handling (from main.dol).
//
// Address: 0x803a1650 | Name: dsp_init                 | Date: 2026-09-15
// Address: 0x803a1690 | Name: dsp_reset_handler        | Date: 2026-09-15
// Address: 0x803a1970 | Name: dsp_process_next_command | Date: 2026-09-15
//
// This is the low-level DSP/AI (audio interface, hardware regs at 0xCC006000)
// command-queue + interrupt subsystem. dsp_init sets up the queue; the reset
// handler services AI/DSP interrupts (mailbox + the two AI cause bits), keeps a
// 64-bit "last serviced" timestamp, runs a user callback under a saved OS
// context, and advances the queue; dsp_process_next_command pops one command.
//
// The command queue at DAT_805f3490 is an array of 5-word records:
//   word0 = type (1 or 2), word1..4 = arguments. The queue index is DAT_8061315c.
//   type 1 -> dsp_cmd_type1(a,b,c,d)  (FUN_803a1a64, next batch)
//   type 2 -> dsp_cmd_type2(c,d)      (FUN_803a1e8c, next batch)
//
// NON-MATCHING: the AI hardware registers and the many DAT_806131xx / DAT_805f34xx
// state globals are accessed through per-file externs matching the original
// widths; the CONCAT44(hi,lo) 64-bit timestamp split across DAT_80613148/4c is
// transcribed via a u64 pack/unpack helper. Predicates and control flow are
// preserved verbatim.

#include "common.h"

extern "C" {

// ---- AI / DSP hardware registers (0xCC006000 block) ----
extern unsigned int DAT_cc006000;   // AI DMA control / interrupt status
extern unsigned int DAT_cc006004;   // AI DMA secondary control / interrupt

// ---- subsystem state globals ----
extern unsigned int  DAT_8061315c;   // command-queue read index
extern int           DAT_805f3490;   // command array base (word0 of record 0 = type)
extern unsigned int  DAT_80613148;   // last-serviced timestamp, high 32 bits
extern unsigned int  DAT_8061314c;   // last-serviced timestamp, low 32 bits
extern unsigned int  DAT_80613158;   // "fresh reset" flag
extern unsigned int  DAT_80613118;   // pending-mailbox flag
extern unsigned int  DAT_80611400;   // reset bookkeeping flag
extern unsigned int  DAT_805f3548;   // saved reset param copies (dst)
extern unsigned int  DAT_805f354c;
extern unsigned int  DAT_805f3550;
extern unsigned int  DAT_805f3554;   // saved reset param copies (src)
extern unsigned int  DAT_805f3558;
extern unsigned int  DAT_805f355c;
extern int           DAT_80613130;   // reset-in-progress flag
extern int           DAT_80613134;   // secondary-interrupt-pending flag
extern unsigned int  DAT_8061312c;   // reset deadline timestamp, low
extern unsigned int  DAT_80613128;   // reset deadline timestamp, high
extern unsigned int  DAT_800000f8;   // bus clock (Hz); TB freq = clock>>2
extern void        (*DAT_80613124)(int);  // secondary-interrupt callback
extern int           DAT_80613138;   // mailbox-armed flag
extern void        (*DAT_80613120)(unsigned int);  // main DSP callback
extern int           DAT_805f34f8;   // reset alarm object

// ---- OS helpers (declared per-file; not in common.h) ----
extern long long os_get_time(void);
extern void os_cancel_alarm(int *alarm);
extern void os_clear_context(unsigned int ctx);
extern void os_set_current_context(unsigned int ctx);
extern void os_alarm_init(void);

// aram_set_stream_mode (formerly FUN_803a2450) is defined below in this file;
// forward-declare it for dsp_init.
extern "C" void aram_set_stream_mode(int mode, unsigned int streamBase);

// aram_dma_read_write / aram_dma_write_small are defined below in this file
// (formerly FUN_803a1a64 / FUN_803a1e8c). Forward-declare them so
// dsp_process_next_command, which precedes them, can call them.
static void aram_dma_read_write(unsigned int mainAddrHi, unsigned int mainAddrLo,
                                unsigned int aramAddr, void (*cb)(unsigned int));
static int  aram_dma_write_small(unsigned int aramAddr, void (*cb)(unsigned int));
static void aram_dma_write_streaming(unsigned int mainAddrHi, unsigned int mainAddrLo,
                                     unsigned int aramAddr, void (*cb)(unsigned int));

static unsigned long long dsp_timestamp_get(void)
{
    return ((unsigned long long)DAT_80613148 << 32) | DAT_8061314c;
}
static void dsp_timestamp_set(unsigned long long t)
{
    DAT_80613148 = (unsigned int)(t >> 32);
    DAT_8061314c = (unsigned int)t;
}

// dsp_init: reset the command queue and arm the alarm subsystem.
void dsp_init(void)
{
    DAT_8061315c = 0;
    DAT_805f3490 = -1;
    aram_set_stream_mode(0, 0);
    os_alarm_init();
}

// dsp_process_next_command: pop and dispatch the next queued command.
void dsp_process_next_command(void)
{
    int *q = &DAT_805f3490;
    int base = DAT_8061315c * 5;      // 5 words per record
    int type = q[base];
    if (type == 1) {
        DAT_8061315c = DAT_8061315c + 1;
        aram_dma_read_write((unsigned int)q[base + 1], (unsigned int)q[base + 2],
                            (unsigned int)q[base + 3], (void (*)(unsigned int))q[base + 4]);
    } else if (type == 2) {
        DAT_8061315c = DAT_8061315c + 1;
        aram_dma_write_small((unsigned int)q[base + 3], (void (*)(unsigned int))q[base + 4]);
    }
}

// dsp_reset_handler: AI/DSP interrupt + reset service routine.
void dsp_reset_handler(unsigned int savedContext, unsigned int resumeContext)
{
    unsigned char ctxBuf[716];
    unsigned int cause = 0;
    unsigned long long ts = dsp_timestamp_get();
    unsigned int reg, bits;

    if (DAT_80613158 != 0) {
        ts = (unsigned long long)os_get_time();
        DAT_80611400 = 0;
        DAT_805f3548 = DAT_805f3554;
        DAT_805f354c = DAT_805f3558;
        DAT_805f3550 = DAT_805f355c;
        if (DAT_80613118 == 1) {
            cause = 8;
        }
    }
    dsp_timestamp_set(ts);
    DAT_80613158 = 0;
    DAT_80613118 = 0;

    reg  = DAT_cc006000;
    bits = ((reg & 0x2a) << 1) & reg & 0x54;
    if ((bits & 0x40) != 0) cause = 8;
    if ((bits & 0x10) != 0) cause |= 1;
    if ((bits & 0x04) != 0) cause |= 2;

    if (cause != 0) {
        DAT_80613130 = 0;
        os_cancel_alarm(&DAT_805f34f8);
        ts = dsp_timestamp_get();
    }
    dsp_timestamp_set(ts);
    DAT_cc006000 = bits | (reg & 0x2a);

    if (DAT_80613130 == 0) {
        goto check_secondary;
    } else {
        long long now = os_get_time();
        ts = dsp_timestamp_get();
        // deadline compare (signed) against reset time + 200 * (clock/4000) ticks
        unsigned int nowLo = (unsigned int)now;
        int nowHi = (int)((unsigned long long)now >> 32);
        unsigned int span = (DAT_800000f8 / 4000) * 200;
        int lhs = (int)((nowLo - DAT_8061312c < span) + 0x80000000u);
        int rhs = (int)((unsigned int)(nowHi - ((unsigned int)(nowLo < DAT_8061312c) + DAT_80613128)) ^ 0x80000000u);
        if (lhs <= rhs) {
            goto check_secondary;
        }
        bits = DAT_cc006004;
        if ((bits & 4 & ((bits & 2) << 1)) != 0) {
            if (DAT_80613124 != (void (*)(int))0) {
                (*DAT_80613124)(4);
            }
            DAT_80613124 = (void (*)(int))0;
        }
        ts = dsp_timestamp_get();
        DAT_cc006004 = DAT_cc006004;
    }
    goto after_secondary;

check_secondary:
    if (DAT_80613134 == 0) {
        DAT_cc006004 = 0;
    } else {
        reg  = DAT_cc006004;
        bits = reg & 4 & ((reg & 2) << 1);
        if (bits != 0) cause |= 4;
        DAT_cc006004 = bits | (reg & 2);
        DAT_80613134 = 0;
    }

after_secondary:
    dsp_timestamp_set(ts);

    if (((cause & 8) != 0) && (DAT_80613138 == 0)) {
        cause &= 0xfffffff7u;
    }

    if ((cause & 1) == 0) {
        DAT_805f3490 = -1;
        DAT_8061315c = 0;
    } else {
        int *q = &DAT_805f3490;
        int base = DAT_8061315c * 5;
        int type = q[base];
        bool dispatched = false;
        if (type == 1) {
            DAT_8061315c = DAT_8061315c + 1;
            aram_dma_read_write((unsigned int)q[base + 1], (unsigned int)q[base + 2],
                                (unsigned int)q[base + 3], (void (*)(unsigned int))q[base + 4]);
            ts = dsp_timestamp_get();
            dispatched = true;
        } else if (type == 2) {
            DAT_8061315c = DAT_8061315c + 1;
            aram_dma_write_small((unsigned int)q[base + 3], (void (*)(unsigned int))q[base + 4]);
            ts = dsp_timestamp_get();
            dispatched = true;
        }
        if (dispatched) {
            dsp_timestamp_set(ts);
            return;
        }
    }

    dsp_timestamp_set(ts);
    os_clear_context((unsigned int)(unsigned long long)ctxBuf);
    os_set_current_context((unsigned int)(unsigned long long)ctxBuf);
    {
        void (*cb)(unsigned int) = DAT_80613120;
        if (cause != 0) {
            bool have = DAT_80613120 != (void (*)(unsigned int))0;
            DAT_80613120 = (void (*)(unsigned int))0;
            if (have) {
                (*cb)(cause);
            }
            DAT_80613138 = 0;
        }
    }
    os_clear_context((unsigned int)(unsigned long long)ctxBuf);
    os_set_current_context(resumeContext);
    ts = dsp_timestamp_get();
    dsp_timestamp_set(ts);
}

// ============================================================================
// batch_1116: ARAM (Audio RAM) DMA command builders + completion alarm.
//
// Address: 0x803a19f4 | Name: aram_dma_complete_alarm  | Date: 2026-09-15
// Address: 0x803a1a64 | Name: aram_dma_read_write      | Date: 2026-09-15
// Address: 0x803a1b74 | Name: aram_dma_write_streaming | Date: 2026-09-15
// Address: 0x803a1bf4 | Name: aram_dma_dispatch        | Date: 2026-09-15
// Address: 0x803a1e8c | Name: aram_dma_write_small     | Date: 2026-09-15
// Address: 0x803a1f20 | Name: aram_dma_reset_request   | Date: 2026-09-15
// Address: 0x803a1f4c | Name: aram_cmd_write_64        | Date: 2026-09-15
// Address: 0x803a1ff0 | Name: aram_cmd_e3              | Date: 2026-09-15
// Address: 0x803a207c | Name: aram_cmd_e0              | Date: 2026-09-15
// Address: 0x803a2108 | Name: aram_cmd_12              | Date: 2026-09-15
//
// These write the ARAM/DSP DMA controller registers at 0xCC006008..0xCC00601C
// (mailbox command word, ARAM address, main-mem address, byte count, and the
// start/control word) and arm the completion alarm DAT_805f34f8, whose handler
// (aram_dma_complete_alarm) masks the AI interrupt, runs a callback under a
// saved OS context, then restores. The alarm timeout is scaled from the bus
// clock DAT_800000f8 (TB freq = clock>>2): ~10 ticks-per-us * 10us normally,
// or *20us for the high ARAM region (> 0xA00000). aram_dma_dispatch chooses
// between an immediate submit, a streaming submit, or a deferred (alarm-timed)
// submit depending on the reset/streaming mode DAT_8061313c and how close the
// requested ARAM address is to the streaming read cursor.
//
// NON-MATCHING: AI DMA registers and the DAT_806131xx / DAT_805f34xx state are
// accessed via per-file externs matching original widths; the CONCAT44 64-bit
// time arithmetic is transcribed with explicit hi/lo. Control flow verbatim.

// ---- additional ARAM DMA controller registers (0xCC006008 block) ----
extern unsigned int DAT_cc006008;   // mailbox / command word (opcode | flags)
extern unsigned int DAT_cc00600c;   // ARAM address (words: byte addr >> 2)
extern unsigned int DAT_cc006010;   // main-mem address, high word
extern unsigned int DAT_cc006014;   // main-mem address, low word
extern unsigned int DAT_cc006018;   // byte count
extern unsigned int DAT_cc00601c;   // DMA start/control (1 or 3 = kick)

// ---- additional state globals ----
extern unsigned int  DAT_80613150;   // start-of-transfer timestamp, high
extern unsigned int  DAT_80613154;   // start-of-transfer timestamp, low
extern unsigned int  DAT_8061311c;   // last ARAM address submitted
extern unsigned int  DAT_80613140;   // streaming ARAM base offset
extern int           DAT_8061313c;   // transfer mode (0 = plain, 1 = streaming)
extern unsigned int  DAT_805f3494;   // queued command record: main-mem hi
extern unsigned int  DAT_805f3498;   // queued command record: main-mem lo
extern unsigned int  DAT_805f349c;   // queued command record: ARAM address
extern unsigned int  DAT_805f34a0;   // queued command record: callback
extern int           DAT_805f34a4;   // queue busy/state flag
extern unsigned int  DAT_805f34a8;   // saved streaming params
extern unsigned int  DAT_805f34ac;
extern unsigned int  DAT_805f34b0;
extern unsigned int  DAT_805f34b4;
extern int           DAT_805f34b8;   // streaming sentinel (-1)
extern int           DAT_805f34d0;   // deferred-submit alarm object

// ---- OS alarm helpers (declared per-file; not in common.h) ----
extern int  os_mask_interrupts(unsigned int mask);
extern void os_alarm_clear(unsigned int *alarm);
// The alarm callback is invoked by the OS with the interrupted/resume context
// pair; aram_dma_complete_alarm therefore takes two words. Declare the alarm
// callback slot as an untyped function pointer so both the 1-arg queue kicker
// and the 2-arg completion handler install cleanly (matches the original,
// which stores a raw code pointer).
typedef void (*OSAlarmCallback)();
extern void os_set_alarm(unsigned int *alarm, int unused, unsigned int deadlineHi,
                         unsigned int deadlineLo, OSAlarmCallback cb);
extern int  FUN_803a520c(void);      // disc/stream status record (byte+8 = ready)

// aram_dma_complete_alarm (0x803a19f4): alarm fired when a DMA should complete.
// Masks the AI interrupt (0x400), swaps in a scratch OS context, runs the
// pending callback DAT_80613120 with cause 0x10, then restores the context.
void aram_dma_complete_alarm(unsigned int /*param_1*/, unsigned int resumeContext)
{
    void (*cb)(unsigned int);
    bool have;
    unsigned char ctxBuf[716];

    os_mask_interrupts(0x400);
    os_clear_context((unsigned int)(unsigned long long)ctxBuf);
    os_set_current_context((unsigned int)(unsigned long long)ctxBuf);
    cb = DAT_80613120;
    have = DAT_80613120 != (void (*)(unsigned int))0;
    DAT_80613120 = (void (*)(unsigned int))0;
    if (have) {
        (*cb)(0x10);
    }
    os_clear_context((unsigned int)(unsigned long long)ctxBuf);
    os_set_current_context(resumeContext);
}

// aram_dma_read_write (0x803a1a64): build and kick a full ARAM DMA transfer.
// mailbox command 0xA8000000 = ARAM write from main memory. Records the start
// timestamp, programs the addresses/count, and arms the completion alarm with a
// timeout scaled by the ARAM region (10us for low ARAM, 20us for high ARAM).
static void aram_dma_read_write(unsigned int mainAddrHi, unsigned int mainAddrLo,
                                unsigned int aramAddr, void (*cb)(unsigned int))
{
    unsigned int ticks;
    long long now;

    DAT_80613118 = 0;
    DAT_80613158 = 1;
    DAT_80613120 = cb;
    now = os_get_time();
    DAT_80613150 = (unsigned int)((unsigned long long)now >> 32);
    DAT_80613154 = (unsigned int)now;
    DAT_cc006008 = 0xa8000000;
    DAT_cc00600c = aramAddr >> 2;
    DAT_cc006010 = mainAddrHi;
    DAT_cc006014 = mainAddrLo;
    DAT_cc006018 = mainAddrHi;
    DAT_cc00601c = 3;
    DAT_8061311c = mainAddrHi;
    ticks = DAT_800000f8 >> 2;
    os_alarm_clear((unsigned int *)&DAT_805f34f8);
    if (mainAddrHi < 0xa00001) {
        os_set_alarm((unsigned int *)&DAT_805f34f8, 0, 0, ticks * 10, (OSAlarmCallback)aram_dma_complete_alarm);
    } else {
        os_set_alarm((unsigned int *)&DAT_805f34f8, 0, 0, ticks * 0x14, (OSAlarmCallback)aram_dma_complete_alarm);
    }
}

// aram_dma_write_streaming (0x803a1b74): submit an ARAM DMA whose target address
// is aligned into the streaming window (mask 0xffff8000). Records the pending
// request into the queue slot and immediately submits a small write.
static void aram_dma_write_streaming(unsigned int mainAddrHi, unsigned int mainAddrLo,
                                     unsigned int aramAddr, void (*cb)(unsigned int))
{
    if ((aramAddr & 0xffff8000) == 0) {
        DAT_805f349c = 0;
    } else {
        DAT_805f349c = (aramAddr & 0xffff8000) + DAT_80613140;
    }
    DAT_805f3490 = 2;
    DAT_805f34a4 = 1;
    DAT_805f34b8 = -1;
    DAT_8061315c = 0;
    DAT_805f34a0 = (unsigned int)cb;
    DAT_805f34a8 = mainAddrHi;
    DAT_805f34ac = mainAddrLo;
    DAT_805f34b0 = aramAddr;
    DAT_805f34b4 = (unsigned int)cb;
    aram_dma_write_small(DAT_805f349c, cb);
}

// aram_dma_dispatch (0x803a1bf4): top-level ARAM write scheduler. In plain mode
// (DAT_8061313c==0) submits immediately. In streaming mode (==1) it decides
// whether the requested ARAM address collides with the active streaming read
// cursor: if so and within one page, it either defers via a timed alarm (when
// less than ~5ms since the last serviced transfer) or submits immediately;
// otherwise it routes through aram_dma_write_streaming.
int aram_dma_dispatch(unsigned int mainAddrHi, unsigned int mainAddrLo,
                      unsigned int aramAddr, void (*cb)(unsigned int))
{
    unsigned int cursorPage, reqPage, slack, ticks;
    int record, ready;
    long long now;
    unsigned int elapsedLo, elapsedHi;

    DAT_cc006018 = mainAddrLo;
    DAT_805f3554 = mainAddrHi;
    DAT_805f3558 = mainAddrLo;
    DAT_805f355c = aramAddr;

    if (DAT_8061313c == 0) {
        DAT_805f3490 = -1;
        DAT_8061315c = 0;
        aram_dma_read_write(mainAddrHi, mainAddrLo, aramAddr, cb);
    } else if (DAT_8061313c == 1) {
        if (DAT_80611400 == 0) {
            cursorPage = (DAT_805f3550 + DAT_805f354c - 1) >> 15;
            record = FUN_803a520c();
            ready = (*(char *)(record + 8) == '\0') ? 0xf : 5;
            bool collide;
            if ((cursorPage - 2 < (aramAddr >> 15)) ||
                ((aramAddr >> 15) < cursorPage + ready + 3)) {
                collide = true;
            } else {
                collide = false;
            }
            if (collide) {
                cursorPage = (DAT_805f3550 + DAT_805f354c - 1) >> 15;
                reqPage = DAT_805f355c >> 15;
                if ((cursorPage == reqPage) || (cursorPage + 1 == reqPage)) {
                    now = os_get_time();
                    elapsedLo = (unsigned int)now - DAT_8061314c;
                    elapsedHi = (int)((unsigned long long)now >> 32) -
                                ((unsigned int)((unsigned int)now < DAT_8061314c) + DAT_80613148);
                    slack = ((DAT_800000f8 >> 2) / 1000) * 5;   // ~5ms of TB ticks
                    if ((unsigned int)((slack < elapsedLo) + (elapsedHi ^ 0x80000000u)) < 0x80000001u) {
                        DAT_805f3490 = 1;
                        DAT_805f34a4 = -1;
                        ticks = (((DAT_800000f8 >> 2) / 0x1e848) * 500) >> 3;
                        DAT_8061315c = 0;
                        DAT_805f3494 = mainAddrHi;
                        DAT_805f3498 = mainAddrLo;
                        DAT_805f349c = aramAddr;
                        DAT_805f34a0 = (unsigned int)cb;
                        os_alarm_clear((unsigned int *)&DAT_805f34d0);
                        {
                            // CARRY4(slack-elapsedLo, ticks): carry-out of the 32-bit low add
                            unsigned int lowSum = (slack - elapsedLo) + ticks;
                            unsigned int carry = (lowSum < ticks) ? 1u : 0u;
                            unsigned int deadlineHi = carry - ((slack < elapsedLo) + elapsedHi);
                            os_set_alarm((unsigned int *)&DAT_805f34d0, 0,
                                         deadlineHi, lowSum,
                                         (OSAlarmCallback)dsp_process_next_command);
                        }
                    } else {
                        DAT_805f3490 = -1;
                        DAT_8061315c = 0;
                        aram_dma_read_write(mainAddrHi, mainAddrLo, aramAddr, cb);
                    }
                } else {
                    aram_dma_write_streaming(mainAddrHi, mainAddrLo, aramAddr, cb);
                }
            } else {
                DAT_805f3490 = -1;
                DAT_8061315c = 0;
                aram_dma_read_write(mainAddrHi, mainAddrLo, aramAddr, cb);
            }
        } else {
            aram_dma_write_streaming(mainAddrHi, mainAddrLo, aramAddr, cb);
        }
    }
    return 1;
}

// aram_dma_write_small (0x803a1e8c): kick a short ARAM write (command 0xAB000000)
// of just the ARAM address word, then arm the completion alarm (10us).
static int aram_dma_write_small(unsigned int aramAddr, void (*cb)(unsigned int))
{
    unsigned int ticks;

    DAT_80613118 = 0;
    DAT_cc006008 = 0xab000000;
    DAT_cc00600c = aramAddr >> 2;
    DAT_cc00601c = 1;
    ticks = DAT_800000f8 >> 2;
    DAT_80613120 = cb;
    os_alarm_clear((unsigned int *)&DAT_805f34f8);
    os_set_alarm((unsigned int *)&DAT_805f34f8, 0, 0, ticks * 10, (OSAlarmCallback)aram_dma_complete_alarm);
    return 1;
}

// aram_dma_reset_request (0x803a1f20): request an AI DMA reset. Installs the
// callback, flags a secondary interrupt (DAT_80613134), and writes reset bit 2
// to the AI secondary control register 0xCC006004.
int aram_dma_reset_request(void (*cb)(unsigned int))
{
    DAT_80613120 = cb;
    DAT_80613134 = 1;
    DAT_80613118 = 0;
    DAT_cc006004 = 2;
    return 1;
}

// aram_cmd_write_64 (0x803a1f4c): ARAM write of a fixed 64-byte (0x20-word)
// block from main memory (command 0xA8000040), then arm the completion alarm.
int aram_cmd_write_64(unsigned int mainAddrLo, void (*cb)(unsigned int))
{
    unsigned int ticks;

    DAT_80613118 = 0;
    DAT_cc006008 = 0xa8000040;
    DAT_cc00600c = 0;
    DAT_cc006010 = 0x20;
    DAT_cc006014 = mainAddrLo;
    DAT_cc006018 = 0x20;
    DAT_cc00601c = 3;
    ticks = DAT_800000f8 >> 2;
    DAT_80613120 = cb;
    os_alarm_clear((unsigned int *)&DAT_805f34f8);
    os_set_alarm((unsigned int *)&DAT_805f34f8, 0, 0, ticks * 10, (OSAlarmCallback)aram_dma_complete_alarm);
    return 1;
}

// aram_cmd_e3 (0x803a1ff0): issue mailbox command 0xE3000000 (DSP task control)
// and arm the completion alarm.
int aram_cmd_e3(void (*cb)(unsigned int))
{
    unsigned int ticks;

    DAT_80613118 = 0;
    DAT_cc006008 = 0xe3000000;
    DAT_cc00601c = 1;
    ticks = DAT_800000f8 >> 2;
    DAT_80613120 = cb;
    os_alarm_clear((unsigned int *)&DAT_805f34f8);
    os_set_alarm((unsigned int *)&DAT_805f34f8, 0, 0, ticks * 10, (OSAlarmCallback)aram_dma_complete_alarm);
    return 1;
}

// aram_cmd_e0 (0x803a207c): issue mailbox command 0xE0000000 (DSP task control)
// and arm the completion alarm.
int aram_cmd_e0(void (*cb)(unsigned int))
{
    unsigned int ticks;

    DAT_80613118 = 0;
    DAT_cc006008 = 0xe0000000;
    DAT_cc00601c = 1;
    ticks = DAT_800000f8 >> 2;
    DAT_80613120 = cb;
    os_alarm_clear((unsigned int *)&DAT_805f34f8);
    os_set_alarm((unsigned int *)&DAT_805f34f8, 0, 0, ticks * 10, (OSAlarmCallback)aram_dma_complete_alarm);
    return 1;
}

// aram_cmd_12 (0x803a2108): issue mailbox command 0x12000000 with a 64-byte
// (0x20) main-memory block, then arm the completion alarm.
int aram_cmd_12(unsigned int mainAddrLo, void (*cb)(unsigned int))
{
    unsigned int ticks;

    DAT_80613118 = 0;
    DAT_cc006008 = 0x12000000;
    DAT_cc006010 = 0x20;
    DAT_cc006014 = mainAddrLo;
    DAT_cc006018 = 0x20;
    DAT_cc00601c = 3;
    ticks = DAT_800000f8 >> 2;
    DAT_80613120 = cb;
    os_alarm_clear((unsigned int *)&DAT_805f34f8);
    os_set_alarm((unsigned int *)&DAT_805f34f8, 0, 0, ticks * 10, (OSAlarmCallback)aram_dma_complete_alarm);
    return 1;
}

// ============================================================================
// batch_1117 (part): more ARAM/DSP mailbox command builders + reset helpers.
//
// Address: 0x803a21a4 | Name: aram_cmd_e1            | Date: 2026-09-15
// Address: 0x803a223c | Name: aram_cmd_e2            | Date: 2026-09-15
// Address: 0x803a22c8 | Name: aram_cmd_e4            | Date: 2026-09-15
// Address: 0x803a2364 | Name: dsp_hw_reset_pulse     | Date: 2026-09-15
// Address: 0x803a2420 | Name: aram_dma_arm_mailbox   | Date: 2026-09-15
// Address: 0x803a2434 | Name: aram_dma_reset_ack     | Date: 2026-09-15
// Address: 0x803a2450 | Name: aram_set_stream_mode   | Date: 2026-09-15
// Address: 0x803a2494 | Name: dsp_is_known_alarm     | Date: 2026-09-15
//
// The e1/e2/e4 builders issue further DSP mailbox opcodes and arm the shared
// completion alarm (same 10us pattern as the a8/ab/e0/e3 builders above).
// dsp_hw_reset_pulse drives the physical DSP reset line through the PI reset
// register 0xCC003024 with a busy-wait; the arm/ack/mode helpers manage the AI
// secondary-interrupt handshake and the streaming-mode globals.
//
// NON-MATCHING: hardware regs + DAT_806131xx state via per-file externs;
// 64-bit time compares transcribed with explicit hi/lo. Control flow verbatim.

// ---- additional hardware regs + OS helpers used below ----
extern unsigned int DAT_cc003024;   // PI reset register (bit0 = system reset, bit2 = DSP reset)
extern int  os_disable_interrupts(void);
extern int  os_get_interrupts_enabled(void);

// aram_cmd_e1 (0x803a21a4): mailbox command 0xE1000000 | flags, with an ARAM
// address and a main-mem word, then arm the completion alarm.
int aram_cmd_e1(unsigned int flags, unsigned int mainWord, unsigned int aramAddr,
                void (*cb)(unsigned int))
{
    unsigned int ticks;

    DAT_80613118 = 0;
    DAT_cc006008 = flags | 0xe1000000;
    DAT_cc00600c = aramAddr >> 2;
    DAT_cc006010 = mainWord;
    DAT_cc00601c = 1;
    ticks = DAT_800000f8 >> 2;
    DAT_80613120 = cb;
    os_alarm_clear((unsigned int *)&DAT_805f34f8);
    os_set_alarm((unsigned int *)&DAT_805f34f8, 0, 0, ticks * 10, (OSAlarmCallback)aram_dma_complete_alarm);
    return 1;
}

// aram_cmd_e2 (0x803a223c): mailbox command 0xE2000000 | flags, then arm alarm.
int aram_cmd_e2(unsigned int flags, void (*cb)(unsigned int))
{
    unsigned int ticks;

    DAT_80613118 = 0;
    DAT_cc006008 = flags | 0xe2000000;
    DAT_cc00601c = 1;
    ticks = DAT_800000f8 >> 2;
    DAT_80613120 = cb;
    os_alarm_clear((unsigned int *)&DAT_805f34f8);
    os_set_alarm((unsigned int *)&DAT_805f34f8, 0, 0, ticks * 10, (OSAlarmCallback)aram_dma_complete_alarm);
    return 1;
}

// aram_cmd_e4 (0x803a22c8): mailbox command 0xE4000000 | flags, with an extra
// 0x10000 bit when the boolean arg is set, then arm the completion alarm.
int aram_cmd_e4(int setFlag, unsigned int flags, void (*cb)(unsigned int))
{
    unsigned int extra;
    unsigned int ticks;

    extra = 0;
    DAT_80613118 = 0;
    if (setFlag != 0) {
        extra = 0x10000;
    }
    DAT_cc006008 = flags | extra | 0xe4000000;
    DAT_cc00601c = 1;
    ticks = DAT_800000f8 >> 2;
    DAT_80613120 = cb;
    os_alarm_clear((unsigned int *)&DAT_805f34f8);
    os_set_alarm((unsigned int *)&DAT_805f34f8, 0, 0, ticks * 10, (OSAlarmCallback)aram_dma_complete_alarm);
    return 1;
}

// dsp_hw_reset_pulse (0x803a2364): drive the physical DSP reset line via the PI
// reset register 0xCC003024. Asserts reset (clear bit2, set bit0), busy-waits
// ~ (busClock/500000)*12/8 TB ticks, then releases (set bits 0 and 2), and marks
// the reset in-progress flag with a deadline timestamp.
void dsp_hw_reset_pulse(void)
{
    unsigned int saved;
    unsigned int waitTicks;
    long long start, now;

    DAT_cc006004 = 2;
    saved = DAT_cc003024;
    DAT_cc003024 = (saved & 0xfffffffb) | 1;
    start = os_get_time();
    waitTicks = DAT_800000f8 / 500000;
    do {
        now = os_get_time();
    } while ((((int)((unsigned long long)now >> 32) -
               ((unsigned int)((unsigned int)now < (unsigned int)start) +
                (int)((unsigned long long)start >> 32))) ^ 0x80000000u) <
             (unsigned int)(((unsigned int)now - (unsigned int)start < ((waitTicks * 0xc) >> 3))
                            + 0x80000000u));
    DAT_cc003024 = saved | 5;
    DAT_80613130 = 1;
    start = os_get_time();
    DAT_8061312c = (unsigned int)start;
    DAT_80613128 = (unsigned int)((unsigned long long)start >> 32);
}

// aram_dma_arm_mailbox (0x803a2420): flag a pending mailbox interrupt as armed.
int aram_dma_arm_mailbox(void)
{
    DAT_80613118 = 1;
    DAT_80613138 = 1;
    return 1;
}

// aram_dma_reset_ack (0x803a2434): acknowledge/clear the AI secondary control
// register, clear the secondary-interrupt flag, and hand back the pending
// callback pointer (clearing it).
int aram_dma_reset_ack(void)
{
    void (*cb)(unsigned int);

    DAT_cc006004 = 0;
    cb = DAT_80613120;
    DAT_80613134 = 0;
    DAT_80613120 = (void (*)(unsigned int))0;
    return (int)(unsigned long long)cb;
}

// aram_set_stream_mode (0x803a2450): set the transfer mode + streaming base
// under an interrupt lock. Called by dsp_init.
void aram_set_stream_mode(int mode, unsigned int streamBase)
{
    os_disable_interrupts();
    DAT_8061313c = mode;
    DAT_80613140 = streamBase;
    os_get_interrupts_enabled();
}

// dsp_is_known_alarm (0x803a2494): true if the pointer is one of the two DSP
// alarm objects (0x805f3520 or DAT_805f34f8).
int dsp_is_known_alarm(void *alarm)
{
    if (alarm == (void *)0x805f3520) {
        return 1;
    }
    if (alarm == (void *)&DAT_805f34f8) {
        return 1;
    }
    return 0;
}

} // extern "C"
