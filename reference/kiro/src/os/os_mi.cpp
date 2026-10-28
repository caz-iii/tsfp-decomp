// os_mi.cpp - Dolphin OS Memory Interface (MI) protection/interrupt subsystem
//
// Address: 0x8039d50c | Name: os_mi_clear_state       | Date: 2026-09-14
// Address: 0x8039d524 | Name: os_mi_enable_protection | Date: 2026-09-14
// Address: 0x8039d560 | Name: os_mi_interrupt_handler | Date: 2026-09-14
// Address: 0x8039d6cc | Name: os_mi_return_from_intr  | Date: 2026-09-14
// Address: 0x8039d6e4 | Name: os_mi_init              | Date: 2026-09-14
//
// The Memory Interface (MI) at 0xcc004000 guards RAM regions and raises a
// protection interrupt (OS cause bit 0xf) on an illegal access. os_mi_init
// (reached from os_kernel_init) programs the MI protection registers, installs
// the MI interrupt handler on the five MI causes, detects the console memory size
// (24 MB retail vs 48 MB devkit at DAT_800000f0 / DAT_80000028) and invalidates
// the extended-memory region, then unmasks the region-0 protection interrupt.
// os_mi_interrupt_handler reads the MI interrupt-status registers, acknowledges
// them, and dispatches the registered protection callback (DAT_805f274c) or the
// default exception handler with cause 0xf.
//
// NON-MATCHING: the MI register bit-twiddling is transcribed verbatim; the
// interrupt-primitive helpers resolve as permissive int(...) in the full build
// so they are declared per-file with the signatures used here; the varargs
// FP-spill on the default-handler call is dropped (real prototype takes
// (byte, ctx, dsisr, dar)).

#include "common.h"

// code: Ghidra's function-body placeholder type (a callable target).
typedef void code;

extern "C" {
extern int    DAT_800030cc;   // MI error-handler state word
extern int    DAT_800030c8;   // MI fault address
extern int    DAT_800030d0;   // MI fault info
extern code  *DAT_805f274c;   // registered MI protection callback
extern uint   DAT_800000f0;   // detected simulated-memory size
extern uint   DAT_80000028;   // physical memory size (from bootinfo)

// MI hardware registers (0xcc004000 block).
extern int    DAT_cc004000;   // MI protection region base (dumped on fault)
extern ushort DAT_cc00401e;   // MI interrupt status (which region)
extern ushort DAT_cc004010;   // MI protection mask/enable
extern ushort DAT_cc004020;   // MI interrupt acknowledge
extern ushort DAT_cc004022;   // MI fault address low
extern ushort DAT_cc004024;   // MI fault address high / flags
extern ushort DAT_cc004028;   // MI extended-memory config

// Named cross-module helpers (real signatures per generated_decls.h; interrupt
// primitives are permissive int(...) in the full build, declared here to match
// the call sites).
extern int  os_mask_interrupts(uint bits);
extern int  os_unmask_interrupts(uint bits);
extern int  os_set_interrupt_handler(short intr, void *handler);
extern int  os_disable_interrupts(void);
extern int  os_get_interrupts_enabled(void);
extern int  os_dc_invalidate_range(int start, int len);
extern int  returnFromInterrupt(uint cur, uint next);
extern u32  os_read_msr(void);       // mfmsr intrinsic
extern void os_default_exception_handler(byte exceptionIndex, undefined4 *ctx, uint dsisr, uint dar);
extern void FUN_8039daa8(int arg);   // MI region-0 protection programming
}

// Forward declaration (installed as the handler by os_mi_init).
void os_mi_interrupt_handler(undefined8 param_1, double param_2, double param_3, double param_4,
                             double param_5, double param_6, double param_7, double param_8,
                             undefined4 param_9, undefined4 *ctx);

// Reset the MI error-handler bookkeeping.
void os_mi_clear_state(void)
{
    DAT_800030cc = 0;
    DAT_800030c8 = 0;
    DAT_800030d0 = 0;
}

// Enable the MI protection interrupt (arm the mask register and mask the MI bits
// in the OS interrupt controller). Always returns 1.
undefined4 os_mi_enable_protection(int enable)
{
    if (enable != 0) {
        DAT_cc004010 = 0xff;
        os_mask_interrupts(0xf0000000);
    }
    return 1;
}

// MI protection-fault interrupt handler: snapshot the fault registers, ack the
// interrupt, then dispatch the registered protection callback or the default
// exception handler with cause 0xf (MI). Installed on MI causes 0..4.
void os_mi_interrupt_handler(undefined8 param_1, double param_2, double param_3, double param_4,
                             double param_5, double param_6, double param_7, double param_8,
                             undefined4 param_9, undefined4 *ctx)
{
    ushort status;
    ushort faultLo;
    ushort faultHi;

    faultHi = DAT_cc004024;
    faultLo = DAT_cc004022;
    status = DAT_cc00401e;
    DAT_cc004020 = 0;
    if (DAT_805f274c == (code *)0x0) {
        os_default_exception_handler(0xf, ctx, (uint)status,
                                     (faultHi & 0x3ff) << 0x10 | (uint)faultLo);
    } else {
        (*(void (*)(int))DAT_805f274c)(0xf);
    }
}

// Clear MSR data/instruction address-translation-related bits (0x30) and rfi;
// used during memory bring-up. Returns the new MSR image.
uint os_mi_return_from_intr(void)
{
    uint msr = os_read_msr();
    returnFromInterrupt(msr, msr & 0xffffffcf);
    return msr & 0xffffffcf;
}

// __OSModuleInit-style MI init (from os_kernel_init): disable interrupts, program
// MI protection, install the MI interrupt handler on the five MI causes, detect
// the console memory size and invalidate the extended region on a devkit, then
// unmask the region-0 protection interrupt.
void os_mi_init(void)
{
    uint memSize;

    memSize = DAT_800000f0;
    os_disable_interrupts();
    DAT_cc004020 = 0;
    DAT_cc004010 = 0xff;
    os_mask_interrupts(0xf0000000);
    os_set_interrupt_handler(0, (void *)os_mi_interrupt_handler);
    os_set_interrupt_handler(1, (void *)os_mi_interrupt_handler);
    os_set_interrupt_handler(2, (void *)os_mi_interrupt_handler);
    os_set_interrupt_handler(3, (void *)os_mi_interrupt_handler);
    os_set_interrupt_handler(4, (void *)os_mi_interrupt_handler);
    FUN_8039daa8(-0x7fb66a58);
    if ((DAT_800000f0 < DAT_80000028) && (DAT_800000f0 == 0x1800000)) {
        os_dc_invalidate_range(0x81800000, 0x1800000);
        DAT_cc004028 = 2;
    }
    if (memSize < 0x1800001) {
        os_mi_return_from_intr();
    } else if (memSize < 0x3000001) {
        os_mi_return_from_intr();
    }
    os_unmask_interrupts(0x8000000);
    os_get_interrupts_enabled();
}
