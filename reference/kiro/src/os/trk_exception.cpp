// batch_1089: the MetroTRK exception-handler body and monitor re-entry glue - the
// address translator, the exception-vector reinstaller, the core exception handler
// that unpacks the saved CPU frame and posts a stop, the monitor re-entry that
// re-inits TRK and runs the message pump, the idle/callback hooks, and the OSReport
// wrapper.
//
// Non-matching decomp: the saved exception frame is a raw u32 array indexed by slot
// (param_1[N]); live MSR/register reads are modelled as inputs; TRK helpers and the
// two indirect hooks resolve as cross-module externs / function-pointer globals.
//
// Address range: 0x80395ce8 - 0x8039604b

#include "common.h"

// --- cross-module TRK helpers ----------------------------------------------
extern void trk_flush_cache_range(unsigned int addr, unsigned int count);
extern void leaf_3268(int dst, int src, unsigned int n);
extern void trk_save_sprs(void);
extern int  trk_post_stop_message(void);
extern void FUN_80396f44(void);                 // transport reset / step hook
extern int  trk_init(void);
extern int  trk_comm_bringup(void);
extern unsigned long long trk_send_version(unsigned long long a1, unsigned long long a2,
        unsigned long long a3, unsigned long long a4, unsigned long long a5,
        unsigned long long a6, unsigned long long a7, unsigned long long a8,
        int a9, void *a10, unsigned int *a11, int a12, int a13, int a14, int a15, int a16);
extern void msg_pump_loop(unsigned long long f1, unsigned long long a2, unsigned long long a3,
        unsigned long long a4, unsigned long long a5, unsigned long long a6,
        unsigned long long a7, unsigned long long a8, int hi, int lo,
        unsigned int *a11, int a12, int a13, int a14, int a15, int a16);
extern void FUN_803995b4(int addr, int n);      // breakpoint table install helper
extern void FUN_803994d0(int addr, int n);      // breakpoint table flush helper
extern void OSReport(unsigned long long a1, unsigned long long a2, unsigned long long a3,
        unsigned long long a4, unsigned long long a5, unsigned long long a6,
        unsigned long long a7, unsigned long long a8, void *fmt, int a10, void *a11,
        int a12, int a13, int a14, int a15, int a16);
// PPC intrinsics.
extern "C" void sync(int x);
extern "C" unsigned int returnFromInterrupt(unsigned int msr, unsigned int srr0);

// --- TRK state globals ------------------------------------------------------
extern unsigned int DAT_805f1930;      // monitor/cached-window base
extern unsigned int DAT_805f1690;      // DBAT0L (mapping-present bits)
extern int          DAT_805f1938;      // last init/bringup status
extern unsigned int DAT_805f1940;      // idle-hook-done flag
extern unsigned int DAT_805f0000;      // TRK data segment base
extern short        DAT_805f13b0;      // last exception vector number
extern unsigned int DAT_805f1450;      // suspend-notify guard (low byte used)
extern unsigned int DAT_805f1454;      // step-support flag pointer target
extern unsigned int DAT_805f1750;      // exception cause word
extern unsigned int DAT_805f1884;      // saved SRR0 for external interrupt
extern unsigned int DAT_805f1894;      // external-interrupt return value

// Saved FP/exception context (byte 0 = in-handler flag, byte 1 = fault flag).
extern unsigned int DAT_80498004;
extern unsigned int DAT_80498008;
extern unsigned int DAT_8049800c;
extern unsigned int DAT_80498010;
extern unsigned int DAT_80498028;      // vector-present bitmap base

// Context-save array (base 0x805f1458).
extern unsigned int  DAT_805f1458, DAT_805f145c, DAT_805f1460, DAT_805f1464;
extern unsigned int  DAT_805f1468, DAT_805f146c, DAT_805f1470, DAT_805f1474;
extern unsigned int  DAT_805f1478, DAT_805f147c, DAT_805f1480, DAT_805f1484;
extern unsigned int  DAT_805f1488, DAT_805f148c, DAT_805f1490, DAT_805f1494;
extern unsigned int  DAT_805f1498, DAT_805f149c, DAT_805f14a0, DAT_805f14a4;
extern unsigned int  DAT_805f14a8, DAT_805f14ac, DAT_805f14b0, DAT_805f14b4;
extern unsigned int  DAT_805f14b8, DAT_805f14bc, DAT_805f14c0, DAT_805f14c4;
extern unsigned int  DAT_805f14c8, DAT_805f14cc, DAT_805f14d0, DAT_805f14d4;
extern unsigned int  DAT_805f14d8, DAT_805f14dc, DAT_805f14e0, DAT_805f14e4;
extern unsigned int  DAT_805f14e8;

// Callback hooks.
typedef void (*trk_void_fn)(void);
extern trk_void_fn DAT_8049806c;       // idle hook
extern trk_void_fn DAT_80498088;       // hook 88
extern trk_void_fn DAT_8049808c;       // hook 8c

// OSReport format string + arg block.
extern unsigned char DAT_803e1bcc;
extern unsigned char DAT_803e0000;

// ---------------------------------------------------------------------------
// trk_install_vectors (0x80395ce8) - (re)install the exception-handler vectors from
// trk_cmd_reset. Reads the vector-present bitmap, and for each set bit (except slot
// 4) copies the 0x100-byte handler stub to the target vector address (translating a
// cached address to physical when the mapping window is not active) and flushes it
// to the instruction cache.
void trk_install_vectors(void)
{
    unsigned int bitmap;
    unsigned int *slotp;
    int i;

    // bitmap lives at fixed low address 0x44, read through the mapping window.
    if (DAT_805f1930 < 0x45 && 0x44 < DAT_805f1930 + 0x4000 && (DAT_805f1690 & 3) != 0) {
        bitmap = *(unsigned int *)(uintptr_t)0x00000044;
    } else {
        bitmap = *(unsigned int *)(uintptr_t)0x80000044;
    }

    slotp = &DAT_80498028;
    i = 0;
    do {
        if ((bitmap & (1u << i)) != 0 && i != 4) {
            unsigned int vaddr = *slotp;
            unsigned int taddr = vaddr;
            if ((vaddr < DAT_805f1930 || DAT_805f1930 + 0x4000 <= vaddr || (DAT_805f1690 & 3) == 0)
                && (vaddr < 0x7e000000 || 0x80000000 < vaddr)) {
                taddr = (vaddr & 0x3fffffff) | 0x80000000;
            }
            leaf_3268((int)taddr, (int)(vaddr + 0x8000328c), 0x100);
            trk_flush_cache_range(taddr, 0x100);
        }
        i = i + 1;
        slotp = slotp + 1;
    } while (i < 0xf);
}

// ---------------------------------------------------------------------------
// trk_translate_address (0x80395e14) - map a target virtual address to the address
// TRK should touch: return it unchanged when it lies in the active mapping window or
// the uncached 0x7e000000..0x80000000 range, otherwise fold it to physical | 0x80000000.
unsigned int trk_translate_address(unsigned int addr)
{
    if (DAT_805f1930 <= addr) {
        if (addr < DAT_805f1930 + 0x4000 && (DAT_805f1690 & 3) != 0) {
            return addr;
        }
    }
    if (0x7dffffff < addr && addr < 0x80000001) {
        return addr;
    }
    return (addr & 0x3fffffff) | 0x80000000;
}

// ---------------------------------------------------------------------------
// trk_call_idle_hook (0x80395e6c) - thin wrapper that runs the idle callback once
// (FUN_80396100): if the one-shot flag is clear and a hook is installed, call it.
void trk_call_idle_hook(void)
{
    if (DAT_805f1940 == 0 && DAT_8049806c != (trk_void_fn)0) {
        (*DAT_8049806c)();
    }
}

// ---------------------------------------------------------------------------
// trk_reenter_monitor (0x80395e8c) - the continuation both exception vectors fall
// into after dispatch when not resuming: reset the transport, re-init TRK, and if
// that succeeds send the version reply and enter the message-pump loop; on exit
// bring the comm layer back up. (Register-passing args are threaded through.)
void trk_reenter_monitor(unsigned long long a1, unsigned long long a2, unsigned long long a3,
        unsigned long long a4, unsigned long long a5, unsigned long long a6,
        unsigned long long a7, unsigned long long a8, int a9, int a10,
        unsigned int *a11, int a12, int a13, int a14, int a15, int a16)
{
    (void)a1; (void)a9; (void)a10;
    FUN_80396f44();
    DAT_805f1938 = trk_init();
    if (DAT_805f1938 == 0) {
        unsigned long long v = trk_send_version(0, a2, a3, a4, a5, a6, a7, a8, 0,
                                                &DAT_805f0000, a11, a12, a13, a14, a15, a16);
        msg_pump_loop(0, a2, a3, a4, a5, a6, a7, a8,
                      (int)(v >> 0x20), (int)v, a11, a12, a13, a14, a15, a16);
    }
    DAT_805f1938 = trk_comm_bringup();
}

// ---------------------------------------------------------------------------
// trk_handle_exception (0x80395ee4) - the core exception handler body. Given the
// saved CPU frame it unpacks the GPRs, the packed CR word and the request slots,
// handles the external-interrupt case (vector 0x500) by running the idle path and
// returning early, bails out with the fault status when already in a nested fault,
// otherwise records the cause, copies the whole frame into the context-save array,
// snapshots the SPRs, marks the handler active, and posts the stop message.
unsigned int trk_handle_exception(unsigned int *frame, int vector, unsigned int a3,
        unsigned int a4, unsigned int a5, unsigned int a6, unsigned int a7, unsigned int a8)
{
    unsigned int r0    = frame[0];
    unsigned int sp    = frame[1];
    unsigned int r2    = frame[2];
    unsigned short flags = *(unsigned short *)((int)frame + 0x1a2);
    unsigned int gpr5 = flags;
    unsigned int gpr6 = flags & 2;
    unsigned int in_r11 = 0, in_r12 = 0;

    if ((flags & 2) == 0) {
        // registers r14..r31 come from the frame
    } else {
        *(unsigned short *)((int)frame + 0x1a2) = flags & 0xfffd;
        gpr5 = frame[5];
        gpr6 = frame[6];
        a5   = frame[7];
        a6   = frame[8];
        a7   = frame[9];
        a8   = frame[10];
        in_r11 = frame[11];
        in_r12 = frame[12];
    }
    unsigned int r14 = frame[0xd],  r15 = frame[0xe],  r16 = frame[0xf];
    unsigned int r17 = frame[0x10], r18 = frame[0x11], r19 = frame[0x12];
    unsigned int r20 = frame[0x13], r21 = frame[0x14], r22 = frame[0x15];
    unsigned int r23 = frame[0x16], r24 = frame[0x17], r25 = frame[0x18];
    unsigned int r26 = frame[0x19], r27 = frame[0x1a], r28 = frame[0x1b];
    unsigned int r29 = frame[0x1c], r30 = frame[0x1d], r31 = frame[0x1e];

    unsigned int cr    = frame[0x20];
    unsigned int spr7  = frame[0x22];
    unsigned int spr8  = frame[0x23];
    unsigned int msr_masked = 0;                 // in_MSR & 0x9000 in the original
    unsigned int srr0  = frame[3];
    unsigned int handle= frame[4];
    unsigned int lr    = frame[0x66];
    unsigned int srr1  = frame[0x67];
    unsigned int ctr   = frame[0x1f];

    // re-pack CR nibbles (identity re-pack preserving the 8 fields)
    cr = (cr & 0xf0000000)
       | (((cr >> 0x18) & 0xf) << 0x18) | (((cr >> 0x14) & 0xf) << 0x14)
       | (((cr >> 0x10) & 0xf) << 0x10) | (((cr >> 0xc)  & 0xf) << 0xc)
       | (((cr >> 8)    & 0xf) << 8)    | (((cr >> 4)   & 0xf) << 4)
       | (cr & 0xf);
    sync(0);
    sync(0);
    DAT_805f13b0 = (short)vector;
    unsigned int nia = frame[0x21];

    if (vector == 0x500) {
        DAT_805f1884 = frame[0x21];
        // trk_nop_5f6c() - no-op elided
        if (DAT_805f1454 == 0 || (DAT_80498010 & 0xff) == 1) {
            returnFromInterrupt(msr_masked, srr1);
            return DAT_805f1894;
        }
        DAT_805f1450 = (DAT_805f1450 & 0xffffff00) | 1;
        handle = 0;
        nia = DAT_805f1884;
    }

    if ((DAT_80498010 & 0xff) != 0) {
        DAT_8049800c = (DAT_8049800c & 0xffff0000) | (unsigned short)DAT_805f13b0;
        DAT_80498004 = lr;
        DAT_80498010 = (DAT_80498010 & 0xffff00ff) | 0x100;   // set fault flag
        returnFromInterrupt(msr_masked, srr1);
        return srr0;
    }

    DAT_805f1750 = ((unsigned int)(unsigned short)DAT_805f13b0)
                 | ((unsigned int)(unsigned short)DAT_805f13b0 << 16);
    DAT_805f1458 = r0;   DAT_805f145c = sp;   DAT_805f1460 = r2;
    DAT_805f1464 = srr0; DAT_805f1468 = handle;
    DAT_805f146c = gpr5; DAT_805f1470 = gpr6;
    DAT_805f1474 = a5;   DAT_805f1478 = a6;   DAT_805f147c = a7;   DAT_805f1480 = a8;
    DAT_805f1484 = in_r11; DAT_805f1488 = in_r12;
    DAT_805f148c = r14;  DAT_805f1490 = r15;  DAT_805f1494 = r16;  DAT_805f1498 = r17;
    DAT_805f149c = r18;  DAT_805f14a0 = r19;  DAT_805f14a4 = r20;  DAT_805f14a8 = r21;
    DAT_805f14ac = r22;  DAT_805f14b0 = r23;  DAT_805f14b4 = r24;  DAT_805f14b8 = r25;
    DAT_805f14bc = r26;  DAT_805f14c0 = r27;  DAT_805f14c4 = r28;  DAT_805f14c8 = r29;
    DAT_805f14cc = r30;  DAT_805f14d0 = r31;  DAT_805f14d4 = ctr;
    DAT_805f14d8 = lr;   DAT_805f14dc = nia;
    DAT_805f14e0 = cr;   DAT_805f14e4 = spr7; DAT_805f14e8 = spr8;
    trk_save_sprs();
    DAT_80498010 = (DAT_80498010 & 0xffffff00) | 1;           // mark handler active
    sync(0);
    sync(0);
    return (unsigned int)trk_post_stop_message();
}

// ---------------------------------------------------------------------------
// trk_nop_5f6c (0x80395f6c) - empty hook, no-op.
void trk_nop_5f6c(void)
{
}

// ---------------------------------------------------------------------------
// trk_install_breakpoint_handler (0x80395f70) - from trk_init: patch the 4-byte
// breakpoint trampoline into low memory and register/flush it through the two
// breakpoint-table helpers so the trap vector reaches the TRK handler.
void trk_install_breakpoint_handler(void)
{
    leaf_3268((int)0x80397ab0, (int)0x803e1bc8, 4);    // copy 4-byte branch stub
    FUN_803995b4(0x80397ab0, 4);
    FUN_803994d0(0x80397ab0, 4);
}

// ---------------------------------------------------------------------------
// trk_report (0x80395fc8) - format-string wrapper around OSReport for TRK diagnostic
// output.
void trk_report(unsigned long long a1, unsigned long long a2, unsigned long long a3,
        unsigned long long a4, unsigned long long a5, unsigned long long a6,
        unsigned long long a7, unsigned long long a8, int a9, int a12,
        int a13, int a14, int a15, int a16)
{
    OSReport(a1, a2, a3, a4, a5, a6, a7, a8, &DAT_803e1bcc, a9, &DAT_803e0000,
             a12, a13, a14, a15, a16);
}

// ---------------------------------------------------------------------------
// trk_call_hook_88 (0x80395ff8) - invoke the stored callback at DAT_80498088.
void trk_call_hook_88(void)
{
    (*DAT_80498088)();
}

// ---------------------------------------------------------------------------
// trk_call_hook_8c (0x80396028) - invoke the stored callback at DAT_8049808c.
void trk_call_hook_8c(void)
{
    (*DAT_8049808c)();
}
