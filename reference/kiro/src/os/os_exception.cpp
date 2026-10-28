// os_exception.cpp - Dolphin OS exception-handler registration + default dump handler
//
// Address: 0x8039a624 | Name: os_set_error_handler          | Date: 2026-09-14
// Address: 0x8039a83c | Name: os_default_exception_handler  | Date: 2026-09-14
//
// OSSetErrorHandler installs a C error/exception handler for one of the CPU
// exception indices in the OS handler table at DAT_805f2710. Registering (or
// clearing) the FP-unavailable handler (index 0x10) additionally walks the live
// thread-context queue toggling each context's MSR-FP-available image and the
// FPSCR non-IEEE bits so lazy FP save/restore stays consistent, and mirrors the
// same change into the CPU's live FPSCR. os_default_exception_handler is the
// fatal-error dispatcher: it runs a registered handler if present, otherwise
// reports the fault (OSReport), dumps the register/DMA context, decodes the
// specific fault, and halts. No-return.
//
// NON-MATCHING: the original OSReport call sites carry the PowerPC varargs
// FP-spill (eight FP doubles + an accumulator register) that the decompiler
// surfaced as extra parameters; the real ABI is OSReport(const char*, ...), so
// those spill artifacts are dropped here and the integer format arguments are
// passed directly. os_dump_context / os_save_fpu_context_current / os_load_context
// resolve to their real one-argument prototypes via generated_decls.h.

#include "common.h"

// code: Ghidra's function-body placeholder type (a callable target).
typedef void code;

extern "C" {
// OS exception-handler table (one code* per exception index) and the current
// running thread context.
extern int   DAT_805f2710;      // base of handler-pointer table
extern int   DAT_800000dc;      // head of the live thread-context queue
extern int   DAT_806113d0;      // saved FPSCR feature bits (0xf8 mask: NI etc.)
extern void *DAT_805f2750;      // registered FP-context relinquish callback
extern int  *DAT_800000d8;      // context that currently owns the FP unit

// AI/ARAM/DI DMA hardware registers dumped for a program (0xf) exception.
extern ushort DAT_cc005030;
extern ushort DAT_cc005032;
extern ushort DAT_cc005020;
extern ushort DAT_cc005022;
extern int    DAT_cc006014;

// Last-interrupt bookkeeping printed at the end of the dump.
extern int  DAT_806130b8;
extern int  DAT_806130b4;
extern int  DAT_806130c0;
extern int  DAT_806130c4;

extern char DAT_806113d4;       // "" empty separator string

// Report format strings.
extern char s_Non_recoverable_Exception__d_80498794[];
extern char s_Unhandled_Exception__d_804987b4[];
extern char s_DSISR___0x_08x_DAR___0x_08x_804987cc[];
extern char s_TB___0x_016llx_80498800[];
extern char s_Instruction_at_0x_x__read_from_S_80498810[];
extern char s_Attempted_to_fetch_instruction_f_80498870[];
extern char s_Instruction_at_0x_x__read_from_S_804988bc[];
extern char s_Program_exception___Possible_ill_80498920[];
extern char s_AI_DMA_Address___0x_04x_04x_80498980[];
extern char s_ARAM_DMA_Address___0x_04x_04x_804989a0[];
extern char s_DI_DMA_Address___0x_08x_804989c0[];
extern char s_Last_interrupt___d___SRR0___0x_0_804989dc[];

// Interrupt-critical-region bracketing / FP-context relinquish plumbing.
extern void FUN_8039cca0(void);   // OSDisableInterrupts
extern void FUN_8039ccc8(void);   // OSRestoreInterrupts
extern void FUN_8039f35c(void);
extern void FUN_8039f39c(void);
extern int  FUN_8039f868(void);
extern u64  FUN_803a0c30(void);   // read time-base (u64)

// Named cross-module helpers (real signatures per generated_decls.h). Declared
// per-file because the os/ tree does not route through a module shim header.
extern void gdev_hal_flush_c(void);
extern void gdev_hal_stub_d(void);
extern u32  os_get_fpscr(void);
extern void os_set_fpscr(u32 value);
extern void os_save_fpu_context_current(void *ctx);
extern u64  os_load_context(u32 ctx);
extern void os_dump_context(void *ctx);
extern void os_halt(void);
// OSReport is declared in sdk_stubs.h (void OSReport(const char*, ...)).
}

// OSSetErrorHandler(exceptionIndex, handler) -> previousHandler.
// Registering the FP-unavailable handler (0x10) reconfigures every live thread
// context's MSR/FPSCR feature bits for lazy FP handling.
int os_set_error_handler(ushort exceptionIndex, int handler)
{
    int prevHandler;
    int ctx;
    int gqr;
    int slot;
    uint fpscr;

    FUN_8039cca0();
    prevHandler = *(int *)(&DAT_805f2710 + (uint)exceptionIndex * 4);
    *(int *)(&DAT_805f2710 + (uint)exceptionIndex * 4) = handler;

    if (exceptionIndex == 0x10) {
        gdev_hal_flush_c();
        gdev_hal_stub_d();
        fpscr = os_get_fpscr();
        ctx = DAT_800000dc;

        if (handler == 0) {
            // Clearing: strip FP-available / NI bits from every context.
            for (; ctx != 0; ctx = *(int *)(ctx + 0x2fc)) {
                *(uint *)(ctx + 0x19c) = *(uint *)(ctx + 0x19c) & 0xfffff6ff;
                *(uint *)(ctx + 0x194) = *(uint *)(ctx + 0x194) & 0xffffff07;
                *(uint *)(ctx + 0x194) = *(uint *)(ctx + 0x194) & 0x6005f8ff;
            }
            fpscr = fpscr & 0xffffff07;
        } else {
            // Registering: enable FP in each context, seeding the FP image and
            // the paired-single GQR images the first time.
            for (; ctx != 0; ctx = *(int *)(ctx + 0x2fc)) {
                *(uint *)(ctx + 0x19c) = *(uint *)(ctx + 0x19c) | 0x900;
                if ((*(ushort *)(ctx + 0x1a2) & 1) == 0) {
                    *(ushort *)(ctx + 0x1a2) = *(ushort *)(ctx + 0x1a2) | 1;
                    slot = 4;
                    gqr = ctx;
                    do {
                        *(undefined4 *)(gqr + 0x94)  = 0xffffffff;
                        *(undefined4 *)(gqr + 0x90)  = 0xffffffff;
                        *(undefined4 *)(gqr + 0x1cc) = 0xffffffff;
                        *(undefined4 *)(gqr + 0x1c8) = 0xffffffff;
                        *(undefined4 *)(gqr + 0x9c)  = 0xffffffff;
                        *(undefined4 *)(gqr + 0x98)  = 0xffffffff;
                        *(undefined4 *)(gqr + 0x1d4) = 0xffffffff;
                        *(undefined4 *)(gqr + 0x1d0) = 0xffffffff;
                        *(undefined4 *)(gqr + 0xa4)  = 0xffffffff;
                        *(undefined4 *)(gqr + 0xa0)  = 0xffffffff;
                        *(undefined4 *)(gqr + 0x1dc) = 0xffffffff;
                        *(undefined4 *)(gqr + 0x1d8) = 0xffffffff;
                        *(undefined4 *)(gqr + 0xac)  = 0xffffffff;
                        *(undefined4 *)(gqr + 0xa8)  = 0xffffffff;
                        *(undefined4 *)(gqr + 0x1e4) = 0xffffffff;
                        *(undefined4 *)(gqr + 0x1e0) = 0xffffffff;
                        *(undefined4 *)(gqr + 0xb4)  = 0xffffffff;
                        *(undefined4 *)(gqr + 0xb0)  = 0xffffffff;
                        *(undefined4 *)(gqr + 0x1ec) = 0xffffffff;
                        *(undefined4 *)(gqr + 0x1e8) = 0xffffffff;
                        *(undefined4 *)(gqr + 0xbc)  = 0xffffffff;
                        *(undefined4 *)(gqr + 0xb8)  = 0xffffffff;
                        *(undefined4 *)(gqr + 500)   = 0xffffffff;
                        *(undefined4 *)(gqr + 0x1f0) = 0xffffffff;
                        *(undefined4 *)(gqr + 0xc4)  = 0xffffffff;
                        *(undefined4 *)(gqr + 0xc0)  = 0xffffffff;
                        *(undefined4 *)(gqr + 0x1fc) = 0xffffffff;
                        *(undefined4 *)(gqr + 0x1f8) = 0xffffffff;
                        *(undefined4 *)(gqr + 0xcc)  = 0xffffffff;
                        *(undefined4 *)(gqr + 200)   = 0xffffffff;
                        *(undefined4 *)(gqr + 0x204) = 0xffffffff;
                        *(undefined4 *)(gqr + 0x200) = 0xffffffff;
                        gqr = gqr + 0x40;
                        slot = slot + -1;
                    } while (slot != 0);
                    *(undefined4 *)(ctx + 0x194) = 4;
                }
                *(uint *)(ctx + 0x194) = *(uint *)(ctx + 0x194) | DAT_806113d0 & 0xf8;
                *(uint *)(ctx + 0x194) = *(uint *)(ctx + 0x194) & 0x6005f8ff;
            }
            fpscr = fpscr | DAT_806113d0 & 0xf8;
        }
        os_set_fpscr(fpscr & 0x6005f8ff);
        gdev_hal_stub_d();
    }

    FUN_8039ccc8();
    return prevHandler;
}

// OSDefaultExceptionHandler(exceptionIndex, ctx, dsisr, dar): the fatal-error
// dispatcher. Runs a registered handler if present; otherwise reports the
// exception, dumps the register/DMA context, decodes the specific fault, and
// halts (no return).
void os_default_exception_handler(byte exceptionIndex, undefined4 *ctx, uint dsisr, uint dar)
{
    ushort dmaHi;
    ushort dmaLo;
    uint fpscr;
    uint srr1;
    u64 tb;

    tb = FUN_803a0c30();
    srr1 = ctx[0x67];

    if ((srr1 & 2) == 0) {
        OSReport(s_Non_recoverable_Exception__d_80498794, (uint)exceptionIndex, srr1, dar);
    } else {
        if (((exceptionIndex == 6) && ((srr1 & 0x100000) != 0)) && (DAT_805f2750 != (void *)0x0)) {
            exceptionIndex = 0x10;
            gdev_hal_flush_c();
            gdev_hal_stub_d();
            if (DAT_800000d8 != (int *)0x0) {
                os_save_fpu_context_current(DAT_800000d8);
            }
            fpscr = os_get_fpscr();
            os_set_fpscr(fpscr & 0x6005f8ff);
            gdev_hal_stub_d();
            if (DAT_800000d8 == (int *)ctx) {
                FUN_8039f35c();
                (*(void (*)(int, undefined4 *))DAT_805f2750)(0x10, ctx);
                ctx[0x67] = ctx[0x67] & 0xffffdfff;
                DAT_800000d8 = (int *)0x0;
                ctx[0x65] = ctx[0x65] & 0x6005f8ff;
                FUN_8039f39c();
                FUN_8039f868();
            } else {
                ctx[0x67] = ctx[0x67] & 0xffffdfff;
                DAT_800000d8 = (int *)0x0;
            }
            os_load_context((u32)(uintptr_t)ctx);
        }
        if (*(int *)(&DAT_805f2710 + (uint)exceptionIndex * 4) != 0) {
            FUN_8039f35c();
            (**(void (**)(uint, undefined4 *))(&DAT_805f2710 + (uint)exceptionIndex * 4))(
                exceptionIndex, ctx);
            FUN_8039f39c();
            FUN_8039f868();
            os_load_context((u32)(uintptr_t)ctx);
        }
        if (exceptionIndex == 8) {
            os_load_context((u32)(uintptr_t)ctx);
        }
        OSReport(s_Unhandled_Exception__d_804987b4, (uint)exceptionIndex, srr1, dar);
    }

    OSReport(&DAT_806113d4);
    os_dump_context(ctx);
    OSReport(s_DSISR___0x_08x_DAR___0x_08x_804987cc, dsisr, dar);
    OSReport(s_TB___0x_016llx_80498800, (int)((u64)tb >> 0x20), (int)tb);

    switch (exceptionIndex) {
    case 2:
        OSReport(s_Instruction_at_0x_x__read_from_S_80498810, ctx[0x66], dar);
        break;
    case 3:
        OSReport(s_Attempted_to_fetch_instruction_f_80498870, ctx[0x66]);
        break;
    case 5:
        OSReport(s_Instruction_at_0x_x__read_from_S_804988bc, ctx[0x66], dar);
        break;
    case 6:
        OSReport(s_Program_exception___Possible_ill_80498920, ctx[0x66], dar);
        break;
    case 0xf:
        OSReport(&DAT_806113d4);
        dmaHi = DAT_cc005030;
        dmaLo = DAT_cc005032;
        OSReport(s_AI_DMA_Address___0x_04x_04x_80498980, (uint)dmaHi, (uint)dmaLo);
        dmaHi = DAT_cc005020;
        dmaLo = DAT_cc005022;
        OSReport(s_ARAM_DMA_Address___0x_04x_04x_804989a0, (uint)dmaHi, (uint)dmaLo);
        OSReport(s_DI_DMA_Address___0x_08x_804989c0, DAT_cc006014);
        break;
    }

    OSReport(s_Last_interrupt___d___SRR0___0x_0_804989dc, (int)DAT_806130b8, DAT_806130b4,
             DAT_806130c0, DAT_806130c4);
    // WARNING: Subroutine does not return
    os_halt();
}
