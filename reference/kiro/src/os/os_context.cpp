// Address: 0x80399984 | Name: os_context (Dolphin OS FP context + current-context + machine-check) | Date: 2026-09-14T22:45:00Z
//
// Dolphin OS thread context management for the floating-point / paired-single
// register file, the current-context accessors, and the machine-check DMA-error
// reporter.
//
// The OSContext block stores GPRs, then the FPR file (f0..f31) at +0x90, the
// packed FPSCR at +0x190, and - when paired-single mode is active (HID2 bit
// 0x20000000, PSE) - the low words of the paired-single registers (ps0..ps31) at
// +0x1c8. The current-context pointer lives at DAT_800000d4 with a masked physical
// mirror at DAT_800000c0; DAT_800000d8 holds the FP-owner context so the FP-avail
// bit (0x2000 in the context state word at +0x19c) can be toggled lazily.

#include "common.h"

extern void  os_halt(void);
extern uint  os_get_hid2(void);
extern void  os_set_hid2(uint value);
extern void  instructionSynchronize(void);
// OSReport is declared by the generated headers (void return, varargs); do not
// redeclare it here (would trip C2556/C2371).

// machine-check register/context dumper (defined below) + report strings
void os_dump_context(void *ctx);
extern const char s_Machine_check_received_8049835c[];
extern const char s_HID2___0x_x_SRR1___0x_x_80498374[];
extern const char s_Machine_check_was_not_DMA_locked_80498390[];
extern const char s_DMAErrorHandler____An_error_occu_804983c0[];
extern const char s_The_following_errors_have_been_d_804983fc[];
extern const char s___Requested_a_locked_cache_tag_t_80498434[];
extern const char s___DMA_attempted_to_access_normal_80498474[];
extern const char s___DMA_missed_in_data_cache_804984a0[];
extern const char s___DMA_queue_overflowed_804984c0[];

// current / FP-owner context globals
extern u32 DAT_800000c0;   // current context, masked to physical (& 0x3fffffff)
extern u32 DAT_800000d4;   // current context pointer (OSGetCurrentContext)
extern u32 DAT_800000d8;   // FP-owner context

// os_dma_error_handler 0x80399984  (machine-check DMA-error handler)
// Reads HID2, reports the machine-check banner and the HID2/SRR1 pair, and when the
// fault is a locked-cache DMA error (HID2 DMA-error bits set and the context's SRR1
// carries the DMA bit) enumerates the specific DMA error causes and re-arms HID2.
// Otherwise it reports a non-DMA machine check, dumps the context, and halts.
void os_dma_error_handler(void *ctx)
{
    uint hid2;
    u32  srr1;

    hid2 = os_get_hid2();
    srr1 = ((u32 *)ctx)[0x67];

    OSReport(s_Machine_check_received_8049835c);
    OSReport(s_HID2___0x_x_SRR1___0x_x_80498374, hid2, srr1);

    if (((hid2 & 0xf00000) != 0) && ((srr1 & 0x200000) != 0)) {
        OSReport(s_DMAErrorHandler____An_error_occu_804983c0);
        OSReport(s_The_following_errors_have_been_d_804983fc);
        if ((hid2 & 0x800000) != 0) {
            OSReport(s___Requested_a_locked_cache_tag_t_80498434);
        }
        if ((hid2 & 0x400000) != 0) {
            OSReport(s___DMA_attempted_to_access_normal_80498474);
        }
        if ((hid2 & 0x200000) != 0) {
            OSReport(s___DMA_missed_in_data_cache_804984a0);
        }
        if ((hid2 & 0x100000) != 0) {
            OSReport(s___DMA_queue_overflowed_804984c0);
        }
        os_set_hid2(hid2);
        return;
    }

    OSReport(s_Machine_check_was_not_DMA_locked_80498390);
    os_dump_context(ctx);
    os_halt();   // no return
}

// os_fp_context_is_saved 0x80399bd8
// Reads the "FP context saved" flag (bit 0 of the halfword at +0x1a2) of the given
// context; when set, returns the saved f0 image at +0x98, else returns the passed
// default (used to recover the caller's f0 when restoring context).
u64 os_fp_context_is_saved(u64 def, void *ctx)
{
    if ((*(u16 *)((char *)ctx + 0x1a2) & 1) != 0) {
        return *(u64 *)((char *)ctx + 0x98);
    }
    return def;
}

// os_save_fpu_context 0x80399cfc  (OSSaveFPUContext)
// Saves the full FP register file into the context: f0 (from the incoming f0 image)
// then f1..f31, the packed FPSCR at +0x190, and marks the context as FP-saved (bit
// 0 at +0x1a2). When paired-single mode is active (HID2 PSE bit 0x20000000) it also
// stores the single-precision low words of ps0..ps31 at +0x1c8.
//
// Represented here with concrete accessors instead of the decompiler's ~60 phantom
// register-image locals; the store layout (offsets/order) matches the original.
extern u64    os_read_fpr_bits(int index);   // raw f<index> as a 64-bit image
extern double os_read_fpr(int index);        // f<index>
extern double os_read_ps1(int index);        // paired-single element 1 of ps<index>
extern u32    os_read_fpscr_packed(void);    // FPSCR packed into 32 bits

void os_save_fpu_context(void *ctx)
{
    char *c = (char *)ctx;
    int   i;

    *(u16 *)(c + 0x1a2) |= 1;               // mark FP context saved
    *(u64 *)(c + 0x90) = os_read_fpr_bits(0);   // f0 image

    // f0..f31 doubles at +0x98, 8 bytes apart
    for (i = 0; i < 32; i++) {
        *(double *)(c + 0x98 + i * 8) = os_read_fpr(i);
    }

    // packed FPSCR at +0x190 (stored as a 64-bit slot)
    *(u64 *)(c + 0x190) = (u64)os_read_fpscr_packed();

    // paired-single element 1 low words at +0x1c8 when PSE is enabled
    if ((os_get_hid2() >> 0x1d & 1) != 0) {
        for (i = 0; i < 32; i++) {
            *(float *)(c + 0x1c8 + i * 8)     = (float)os_read_fpr(i);
            *(float *)(c + 0x1c8 + i * 8 + 4) = (float)os_read_ps1(i);
        }
    }
}

// os_save_fpu_context_current 0x80399e24
// Thin wrapper: saves the FP context into the caller-supplied context (param passed
// as both the FP-arg spill target and the context pointer in the original).
void os_save_fpu_context_current(void *ctx)
{
    os_save_fpu_context(ctx);
}

// os_set_current_context 0x80399e2c  (OSSetCurrentContext)
// Records `ctx` as the current context (physical mirror + pointer) and toggles the
// FP-available bit (0x2000 in the context state word at +0x19c): set when this
// context already owns the FP unit, cleared (with isync) otherwise so the next FP
// use faults in a lazy save/restore.
void os_set_current_context(u32 ctx)
{
    DAT_800000c0 = ctx & 0x3fffffff;
    DAT_800000d4 = ctx;
    if (DAT_800000d8 == ctx) {
        *(u32 *)(uintptr_t)(ctx + 0x19c) |= 0x2000;
        return;
    }
    *(u32 *)(uintptr_t)(ctx + 0x19c) &= 0xffffdfff;
    instructionSynchronize();
}

// os_get_current_context 0x80399e88  (OSGetCurrentContext)
u32 os_get_current_context(void)
{
    return DAT_800000d4;
}

// ---------------------------------------------------------------------------
// Context save/restore, init, and dump (0x80399e94 .. 0x8039a430)
// ---------------------------------------------------------------------------

extern float FLOAT_80617940;
extern float FLOAT_8061b5e0;
extern int   FUN_8039cca0(void);           // OSDisableInterrupts
extern void  FUN_8039ccc8(int state);      // OSRestoreInterrupts
extern void  returnFromInterrupt(uint msr, u32 target);
extern uint  rt_double_to_uint(double v);
extern void  os_set_exception_handler(int index, void *handler);
extern void  os_spill_volatiles(void);
extern char  LAB_8039a3ac[];               // FP-unavailable exception body

// non-volatile GPR / SPR reads for the context saver (r14..r31, CR, LR, MSR, CTR, XER)
extern u32 os_read_gpr(int index);
extern u32 os_read_cr(void);
extern u32 os_read_lr(void);
extern u32 os_read_msr(void);
extern u32 os_read_ctr(void);
extern u32 os_read_xer(void);
extern u32 os_read_sp(void);               // r1 (stack pointer)

// context-dump report strings
extern const char s___________________________Contex_80498560[];
extern const char s_r__2d___0x_08x___14d__r__2d___0x_804985a4[];
extern const char s_LR___0x_08x_CR___0x_08x_804985d4[];
extern const char s_SRR0___0x_08x_SRR1___0x_08x_80498604[];
extern const char s_GQRs___________80498634[];
extern const char s_gqr_d___0x_08x_gqr_d___0x_08x_80498648[];
extern const char s_FPRs___________8049866c[];
extern const char s_fr_d____d_fr_d____d_80498680[];
extern const char s_PSFs___________8049869c[];
extern const char s_ps_d___0x_x_ps_d___0x_x_804986b0[];
extern const char s_Address__Back_Chain_LR_Save_804986d0[];
extern const char s_0x_08x__0x_08x_0x_08x_804986f8[];

// os_save_gprs_context 0x80399e94  (__OSSaveContext GPR half)
// Saves the caller's non-volatile GPRs r14..r31, condition register, LR (twice: the
// resume slot at +0x84 and the SRR0 slot at +0x198), MSR, CTR, XER, and the stack
// pointer into the context, seeds the two exception-mode float sentinels at +0x08
// and +0x34, clears the GQR image (+0x1a8..+0x1c0), marks the context state word
// (+0x0c) as "in use", and returns 0.
u32 os_save_gprs_context(u32 ctx)
{
    char *c = (char *)(uintptr_t)ctx;
    int   i;

    *(float **)(c + 0x34) = &FLOAT_80617940;
    for (i = 14; i <= 31; i++) {
        *(u32 *)(c + 0x38 + (i - 14) * 4) = os_read_gpr(i);
    }
    for (i = 0; i < 8; i++) {
        *(u32 *)(c + 0x1a8 + i * 4) = 0;   // clear GQR image
    }
    *(u32 *)(c + 0x80)  = os_read_cr();
    *(u32 *)(c + 0x84)  = os_read_lr();
    *(u32 *)(c + 0x198) = os_read_lr();
    *(u32 *)(c + 0x19c) = os_read_msr();
    *(u32 *)(c + 0x88)  = os_read_ctr();
    *(u32 *)(c + 0x8c)  = os_read_xer();
    *(u32 *)(c + 4)     = os_read_sp();
    *(float **)(c + 8)  = &FLOAT_8061b5e0;
    *(u32 *)(c + 0xc)   = 1;               // context in use
    return 0;
}

// os_load_context 0x80399f14  (__OSLoadContext / resume)
// If the saved resume address (+0x198) falls inside the OSDisableInterrupts critical
// region [FUN_8039cca0, 0x8039ccac), rewinds it to the region start so the resume
// re-runs the guard atomically; clears the "FP restore pending" flag (bit 1 at
// +0x1a2); then returns from interrupt into the saved MSR/PC, resuming the context.
u64 os_load_context(u32 ctx)
{
    char *c   = (char *)(uintptr_t)ctx;
    u32   resume = *(u32 *)(c + 0x198);

    if (((u32)(uintptr_t)FUN_8039cca0 < resume) && (resume < 0x8039ccac)) {
        *(u32 *)(c + 0x198) = (u32)(uintptr_t)FUN_8039cca0;
    }
    if ((*(u16 *)(c + 0x1a2) & 2) != 0) {
        *(u16 *)(c + 0x1a2) &= 0xfffd;
    }
    returnFromInterrupt(os_read_msr() & 0x9000, *(u32 *)(c + 0x19c));
    return *(u64 *)(c + 0xc);
}

// os_get_stack_pointer 0x80399fec  (OSGetStackPointer)
u32 os_get_stack_pointer(void)
{
    return os_read_sp();
}

// os_switch_stack_call 0x80399ff4  (call func on a given stack, e.g. OSSwitchStack)
// Writes the back-chain just below the new stack top then calls func on it.
void os_switch_stack_call(void (*func)(void), u32 stack_top)
{
    *(u32 *)(uintptr_t)(stack_top - 8) = os_read_sp();
    func();
}

// os_clear_context 0x8039a024  (OSClearContext)
// Clears the context mode/flags halfwords (+0x1a0/+0x1a2) and, if this context owns
// the FP unit, relinquishes ownership (DAT_800000d8 = 0).
void os_clear_context(u32 ctx)
{
    char *c = (char *)(uintptr_t)ctx;

    *(u16 *)(c + 0x1a0) = 0;
    *(u16 *)(c + 0x1a2) = 0;
    if (ctx == DAT_800000d8) {
        DAT_800000d8 = 0;
    }
}

// os_init_context 0x8039a048  (OSInitContext)
// Initialises a context block for a new thread entry: sets the entry PC (+0x198) and
// r2/TOC-ish slot (+4), a default MSR of 0x9032 (EE|PR|ME|IR|DR), zeroes CR/XER and
// all GPR slots and the GQR image, seeds the float sentinels, and clears the context
// via OSClearContext.
void os_init_context(u32 ctx, u32 entry, u32 arg)
{
    char *c = (char *)(uintptr_t)ctx;
    int   i;

    *(u32 *)(c + 0x198) = entry;
    *(u32 *)(c + 4)     = arg;
    *(u32 *)(c + 0x19c) = 0x9032;
    *(u32 *)(c + 0x80)  = 0;
    *(u32 *)(c + 0x8c)  = 0;
    *(float **)(c + 8)    = &FLOAT_8061b5e0;
    *(float **)(c + 0x34) = &FLOAT_80617940;
    // zero the general-purpose register images (+0x0c..+0x30 and +0x38..+0x7c)
    for (i = 0x0c; i <= 0x30; i += 4) { *(u32 *)(c + i) = 0; }
    for (i = 0x38; i <= 0x7c; i += 4) { *(u32 *)(c + i) = 0; }
    // zero the mode/GQR image tail (+0x1a4..+0x1c0)
    for (i = 0x1a4; i <= 0x1c0; i += 4) { *(u32 *)(c + i) = 0; }
    os_clear_context(ctx);
}

// os_dump_context 0x8039a104  (OSDumpContext)
// Prints a full register/stack dump of a context: the 32 GPRs (two per line), LR/CR,
// SRR0/SRR1, the four GQRs, and - when the context has FP state saved - the FPRs and
// paired-single low words (briefly making the context current so the FP file can be
// read), followed by a stack backtrace walking the saved back-chain.
void os_dump_context(void *ctx)
{
    u32 *reg = (u32 *)ctx;
    int  i;

    OSReport(s___________________________Contex_80498560, reg);
    for (i = 0; i < 16; i++) {
        OSReport(s_r__2d___0x_08x___14d__r__2d___0x_804985a4,
                 i, reg[i], reg[i], i + 0x10, reg[i + 0x10], reg[i + 0x10]);
    }
    OSReport(s_LR___0x_08x_CR___0x_08x_804985d4, reg[0x21], reg[0x20]);
    OSReport(s_SRR0___0x_08x_SRR1___0x_08x_80498604, reg[0x66], reg[0x67]);

    OSReport(s_GQRs___________80498634);
    for (i = 0; i < 4; i++) {
        OSReport(s_gqr_d___0x_08x_gqr_d___0x_08x_80498648,
                 i, reg[i + 0x69], i + 4, reg[i + 0x6d]);
    }

    if ((*(u16 *)((char *)ctx + 0x1a2) & 1) != 0) {
        int   state = FUN_8039cca0();
        u32   prev  = DAT_800000d4;

        if ((u32)(uintptr_t)ctx == DAT_800000d8) { DAT_800000d8 = 0; }
        os_set_current_context((u32)(uintptr_t)ctx);

        OSReport(s_FPRs___________8049866c);
        for (i = 0; i < 32; i += 2) {
            double d0 = *(double *)(reg + i * 2 + 0x24);
            double d1 = *(double *)(reg + i * 2 + 0x26);
            OSReport(s_fr_d____d_fr_d____d_80498680,
                     i, rt_double_to_uint(d0), i + 1, rt_double_to_uint(d1));
        }
        OSReport(s_PSFs___________8049869c);
        for (i = 0; i < 32; i += 2) {
            double d0 = *(double *)(reg + i * 2 + 0x72);
            double d1 = *(double *)(reg + i * 2 + 0x74);
            OSReport(s_ps_d___0x_x_ps_d___0x_x_804986b0,
                     i, rt_double_to_uint(d0), i + 1, rt_double_to_uint(d1));
        }

        if ((u32)(uintptr_t)ctx == DAT_800000d8) { DAT_800000d8 = 0; }
        os_set_current_context(prev);
        FUN_8039ccc8(state);
    }

    OSReport(s_Address__Back_Chain_LR_Save_804986d0);
    {
        u32 *fp = (u32 *)(uintptr_t)reg[1];
        i = 0;
        while ((fp != 0) && (fp != (u32 *)(uintptr_t)0xffffffff) && (i < 16)) {
            i++;
            OSReport(s_0x_08x__0x_08x_0x_08x_804986f8, fp, fp[0], fp[1]);
            fp = (u32 *)(uintptr_t)fp[0];
        }
    }
}

// os_context_init 0x8039a430  (__OSContextInit)
// Installs the FP-unavailable exception handler (vector 7) that lazily saves/restores
// FP context, and clears the FP-owner so the first FP use faults it in.
void os_context_init(void)
{
    os_set_exception_handler(7, &LAB_8039a3ac);
    DAT_800000d8 = 0;
    os_spill_volatiles();
}
