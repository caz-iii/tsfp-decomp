// os_interrupt.cpp - Dolphin OS interrupt controller (OSInterrupt)
//
// Address: 0x8039cca0 | Name: os_disable_interrupts     | Date: 2026-09-14
// Address: 0x8039ccb4 | Name: os_enable_interrupts      | Date: 2026-09-14
// Address: 0x8039ccc8 | Name: os_get_interrupts_enabled | Date: 2026-09-14
// Address: 0x8039ccec | Name: os_set_interrupt_handler  | Date: 2026-09-14
// Address: 0x8039cd08 | Name: os_get_interrupt_handler  | Date: 2026-09-14
// Address: 0x8039cd1c | Name: os_interrupt_init         | Date: 2026-09-14
// Address: 0x8039cd90 | Name: os_mask_to_hardware       | Date: 2026-09-14
// Address: 0x8039d068 | Name: os_mask_interrupts        | Date: 2026-09-14
// Address: 0x8039d0f0 | Name: os_unmask_interrupts      | Date: 2026-09-14
// Address: 0x8039d178 | Name: os_dispatch_interrupt     | Date: 2026-09-14
//
// The GameCube interrupt controller sits behind the Processor Interface (PI)
// Cause/Mask registers at 0xcc003000/0xcc003004; each peripheral (VI/PE, DSP, AI,
// EXI, DI, ...) has its own pending/enable bits that the OS folds into a single
// 32-bit "OS interrupt cause" bitmap. os_disable/enable/get_interrupts toggle the
// PowerPC MSR external-interrupt-enable bit. os_mask/unmask_interrupts maintain
// the software disabled-mask (DAT_800000c4 user + DAT_800000c8 kernel) and push
// it down to the peripheral hardware masks via os_mask_to_hardware.
// os_dispatch_interrupt is the vector-4 external-interrupt handler: it collects
// every pending peripheral bit into the cause bitmap, drops the masked ones,
// selects the highest-priority pending source via the priority table
// DAT_80499578, and dispatches its registered handler.
//
// NON-MATCHING: os_disable/enable/get_interrupts read the MSR via os_read_msr()
// and return the prior EE state exactly as the decompiler surfaced them (the
// mtmsr write-back the SDK performs is an intrinsic the decompiler did not
// render); the CONCAT44 spill on the two toggles is preserved. The large
// bit-by-bit peripheral cause/mask folds are transcribed verbatim.

#include "common.h"

// code: Ghidra's function-body placeholder type (a callable target).
typedef void code;

extern "C" {
extern int    DAT_806130b0;   // base of the OS interrupt-handler table (code*[])
extern int    DAT_80003040;   // low-mem handler-table storage
extern uint   DAT_800000c4;   // user disabled-interrupt mask
extern uint   DAT_800000c8;   // kernel disabled-interrupt mask
extern int    DAT_8039d4bc;   // external-interrupt exception handler body
extern uint   DAT_80499578;   // interrupt priority table (bit groups, MSB first)
extern int    DAT_806130b8;   // last-interrupt index (for the panic dump)
extern int    DAT_806130b4;   // last-interrupt SRR0
extern int    DAT_806130c0;   // last-interrupt time-base hi
extern int    DAT_806130c4;   // last-interrupt time-base lo

// PI Cause/Mask + per-peripheral pending/enable hardware registers.
extern uint   DAT_cc003000;   // PI INTSR (cause)
extern uint   DAT_cc003004;   // PI INTMR (mask)
extern ushort DAT_cc00401c;   // DSP control (interrupt enables) - write
extern ushort DAT_cc00401e;   // DSP control (interrupt status)  - read
extern ushort DAT_cc00500a;   // AI control
extern uint   DAT_cc006800;   // DI status/cover
extern uint   DAT_cc006814;   // DI (channel 1)
extern uint   DAT_cc006828;   // DI (channel 2)
extern uint   DAT_cc006c00;   // SI/EXI control

// Named cross-module helpers (real signatures per generated_decls.h; declared
// per-file because the os/ tree does not route through a module shim header).
extern void *os_set_exception_handler(unsigned int index, void *handler);
extern u64   os_load_context(int ctx);
extern u32   os_read_msr(void);          // mfmsr intrinsic
extern void  FUN_8039f35c(void);
extern void  FUN_8039f39c(void);
extern int   FUN_8039f868(void);
extern u64   FUN_803a0c30(void);         // read time-base
extern int   memset_wrapper(int dst, int val, int len);
}

// Forward declarations for intra-file references.
uint os_mask_to_hardware(uint remaining, uint activeMask);
uint os_mask_interrupts(uint bits);

// OSDisableInterrupts: clear the MSR external-interrupt-enable bit, returning the
// prior enable state.
ulonglong os_disable_interrupts(void)
{
    uint msr = os_read_msr();
    return CONCAT44(msr >> 0xf, msr) & 0x1ffff7fff;
}

// OSEnableInterrupts: set the MSR external-interrupt-enable bit (0x8000),
// returning the prior enable state.
ulonglong os_enable_interrupts(void)
{
    uint msr = os_read_msr();
    return CONCAT44(msr >> 0xf, msr) & 0x1ffffffff | 0x8000;
}

// OSGetInterruptsEnabled: the current MSR external-interrupt-enable bit.
uint os_get_interrupts_enabled(void)
{
    uint msr = os_read_msr();
    return msr >> 0xf & 1;
}

// __OSSetInterruptHandler(intr, handler): install a handler in the OS table,
// returning the previous entry.
undefined4 os_set_interrupt_handler(short intr, undefined4 handler)
{
    undefined4 prev;
    undefined4 *slot;

    slot = (undefined4 *)(DAT_806130b0 + intr * 4);
    prev = *slot;
    *slot = handler;
    return prev;
}

// __OSGetInterruptHandler(intr): the currently installed handler.
undefined4 os_get_interrupt_handler(short intr)
{
    return *(undefined4 *)(DAT_806130b0 + intr * 4);
}

// __OSInterruptInit: point the handler table at low memory, clear it and the
// software masks, seed the PI mask, mask all interrupts, and install the
// external-interrupt exception handler on vector 4.
void os_interrupt_init(void)
{
    DAT_806130b0 = (int)&DAT_80003040;
    memset_wrapper((int)0x80003040, 0, 0x80);
    DAT_800000c4 = 0;
    DAT_800000c8 = 0;
    DAT_cc003004 = 0xf0;
    os_mask_interrupts(0xffffffe0);
    os_set_exception_handler(4, &DAT_8039d4bc);
}

// __OSMaskToHardware: given the leftover OS-cause bits to program and the active
// (enabled) mask, push the enable/disable state down to the peripheral hardware
// mask registers (DSP/AI/EXI/DI/SI/PI). Returns the bits it did not handle. The
// bit group is selected by countLeadingZeros of the remaining set.
uint os_mask_to_hardware(uint remaining, uint activeMask)
{
    int grp;
    ushort h16;
    uint h32;

    grp = countLeadingZeros(remaining);
    if (grp < 0xc) {
        if (grp == 8) {
            h32 = DAT_cc006c00;
            h32 = h32 & 0xffffffd3;
            if ((activeMask & 0x800000) == 0) {
                h32 = h32 | 4;
            }
            DAT_cc006c00 = h32;
            remaining = remaining & 0xff7fffff;
        } else if (grp < 8) {
            if (grp < 5) {
                if (-1 < grp) {
                    h16 = (ushort)((activeMask & 0x80000000) == 0);
                    if ((activeMask & 0x40000000) == 0) {
                        h16 = h16 | 2;
                    }
                    if ((activeMask & 0x20000000) == 0) {
                        h16 = h16 | 4;
                    }
                    if ((activeMask & 0x10000000) == 0) {
                        h16 = h16 | 8;
                    }
                    if ((activeMask & 0x8000000) == 0) {
                        h16 = h16 | 0x10;
                    }
                    DAT_cc00401c = h16;
                    remaining = remaining & 0x7ffffff;
                }
            } else {
                h16 = DAT_cc00500a;
                h16 = h16 & 0xfe07;
                if ((activeMask & 0x4000000) == 0) {
                    h16 = h16 | 0x10;
                }
                if ((activeMask & 0x2000000) == 0) {
                    h16 = h16 | 0x40;
                }
                if ((activeMask & 0x1000000) == 0) {
                    h16 = h16 | 0x100;
                }
                DAT_cc00500a = h16;
                remaining = remaining & 0xf8ffffff;
            }
        } else {
            h32 = DAT_cc006800;
            h32 = h32 & 0xffffd3f0;
            if ((activeMask & 0x400000) == 0) {
                h32 = h32 | 1;
            }
            if ((activeMask & 0x200000) == 0) {
                h32 = h32 | 4;
            }
            if ((activeMask & 0x100000) == 0) {
                h32 = h32 | 0x400;
            }
            DAT_cc006800 = h32;
            remaining = remaining & 0xff8fffff;
        }
    } else if (grp < 0x11) {
        if (grp < 0xf) {
            h32 = DAT_cc006814;
            h32 = h32 & 0xfffff3f0;
            if ((activeMask & 0x80000) == 0) {
                h32 = h32 | 1;
            }
            if ((activeMask & 0x40000) == 0) {
                h32 = h32 | 4;
            }
            if ((activeMask & 0x20000) == 0) {
                h32 = h32 | 0x400;
            }
            DAT_cc006814 = h32;
            remaining = remaining & 0xfff1ffff;
        } else {
            h32 = DAT_cc006828;
            h32 = h32 & 0xfffffff0;
            if ((activeMask & 0x10000) == 0) {
                h32 = h32 | 1;
            }
            if ((activeMask & 0x8000) == 0) {
                h32 = h32 | 4;
            }
            DAT_cc006828 = h32;
            remaining = remaining & 0xfffe7fff;
        }
    } else if (grp < 0x1b) {
        h32 = 0xf0;
        if ((activeMask & 0x4000) == 0) {
            h32 = 0x8f0;
        }
        if ((activeMask & 0x800) == 0) {
            h32 = h32 | 8;
        }
        if ((activeMask & 0x400) == 0) {
            h32 = h32 | 4;
        }
        if ((activeMask & 0x200) == 0) {
            h32 = h32 | 2;
        }
        if ((activeMask & 0x100) == 0) {
            h32 = h32 | 1;
        }
        if ((activeMask & 0x80) == 0) {
            h32 = h32 | 0x100;
        }
        if ((activeMask & 0x40) == 0) {
            h32 = h32 | 0x1000;
        }
        if ((activeMask & 0x2000) == 0) {
            h32 = h32 | 0x200;
        }
        if ((activeMask & 0x1000) == 0) {
            h32 = h32 | 0x400;
        }
        if ((activeMask & 0x20) == 0) {
            h32 = h32 | 0x2000;
        }
        DAT_cc003004 = h32;
        remaining = remaining & 0xffff801f;
    }
    return remaining;
}

// __OSMaskInterrupts(bits): add bits to the user disabled-mask and reprogram the
// peripheral hardware masks. Returns the previous user mask.
uint os_mask_interrupts(uint bits)
{
    uint prev;
    uint activeBefore;
    uint activeAfter;

    os_disable_interrupts();
    prev = DAT_800000c4;
    activeBefore = DAT_800000c4 | DAT_800000c8;
    DAT_800000c4 = bits | DAT_800000c4;
    activeAfter = DAT_800000c4 | DAT_800000c8;
    for (activeBefore = bits & ~activeBefore; activeBefore != 0;
         activeBefore = os_mask_to_hardware(activeBefore, activeAfter)) {
    }
    os_get_interrupts_enabled();
    return prev;
}

// __OSUnmaskInterrupts(bits): clear bits from the user disabled-mask and
// reprogram the peripheral hardware masks. Returns the previous user mask.
uint os_unmask_interrupts(uint bits)
{
    uint prev;
    uint activeBefore;
    uint activeAfter;

    os_disable_interrupts();
    prev = DAT_800000c4;
    activeBefore = DAT_800000c4 | DAT_800000c8;
    DAT_800000c4 = DAT_800000c4 & ~bits;
    activeAfter = DAT_800000c4 | DAT_800000c8;
    for (activeBefore = bits & activeBefore; activeBefore != 0;
         activeBefore = os_mask_to_hardware(activeBefore, activeAfter)) {
    }
    os_get_interrupts_enabled();
    return prev;
}

// __OSDispatchInterrupt: the vector-4 external-interrupt handler. Reads the PI
// cause, folds every pending peripheral bit into the unified OS-cause bitmap,
// drops masked bits, selects the highest-priority pending source via the priority
// table DAT_80499578, dispatches its handler, and returns to the interrupted
// context.
undefined8 os_dispatch_interrupt(undefined4 param_1, int ctx)
{
    ushort h16;
    uint h32;
    uint cause;
    uint *pPri;
    uint sel;
    undefined4 clz;
    int idx;
    code *handler;
    u64 tb;

    cause = DAT_cc003000;
    if (((cause & 0xfffeffff) == 0) || (h32 = DAT_cc003004, (cause & 0xfffeffff & h32) == 0)) {
        os_load_context(ctx);
    }
    tb = CONCAT44(DAT_806130c0, DAT_806130c4);
    h32 = 0;

    if ((cause & 0x80) != 0) {
        h16 = DAT_cc00401e;
        if ((h16 & 1) != 0)    h32 = 0x80000000;
        if ((h16 & 2) != 0)    h32 = h32 | 0x40000000;
        if ((h16 & 4) != 0)    h32 = h32 | 0x20000000;
        if ((h16 & 8) != 0)    h32 = h32 | 0x10000000;
        if ((h16 & 0x10) != 0) h32 = h32 | 0x8000000;
    }
    if ((cause & 0x40) != 0) {
        h16 = DAT_cc00500a;
        if ((h16 & 8) != 0)    h32 = h32 | 0x4000000;
        if ((h16 & 0x20) != 0) h32 = h32 | 0x2000000;
        if ((h16 & 0x80) != 0) h32 = h32 | 0x1000000;
    }
    if (((cause & 0x20) != 0) && ((DAT_cc006c00 & 8) != 0)) {
        h32 = h32 | 0x800000;
    }
    if ((cause & 0x10) != 0) {
        uint di = DAT_cc006800;
        if ((di & 2) != 0)    h32 = h32 | 0x400000;
        if ((di & 8) != 0)    h32 = h32 | 0x200000;
        if ((di & 0x800) != 0) h32 = h32 | 0x100000;
        di = DAT_cc006814;
        if ((di & 2) != 0)    h32 = h32 | 0x80000;
        if ((di & 8) != 0)    h32 = h32 | 0x40000;
        if ((di & 0x800) != 0) h32 = h32 | 0x20000;
        di = DAT_cc006828;
        if ((di & 2) != 0)    h32 = h32 | 0x10000;
        if ((di & 8) != 0)    h32 = h32 | 0x8000;
    }
    if ((cause & 0x2000) != 0) h32 = h32 | 0x20;
    if ((cause & 0x1000) != 0) h32 = h32 | 0x40;
    if ((cause & 0x400) != 0)  h32 = h32 | 0x1000;
    if ((cause & 0x200) != 0)  h32 = h32 | 0x2000;
    if ((cause & 0x100) != 0)  h32 = h32 | 0x80;
    if ((cause & 8) != 0)      h32 = h32 | 0x800;
    if ((cause & 4) != 0)      h32 = h32 | 0x400;
    if ((cause & 2) != 0)      h32 = h32 | 0x200;
    if ((cause & 0x800) != 0)  h32 = h32 | 0x4000;
    if ((cause & 1) != 0)      h32 = h32 | 0x100;

    h32 = h32 & ~(DAT_800000c4 | DAT_800000c8);
    if (h32 != 0) {
        for (pPri = &DAT_80499578; sel = h32 & *pPri, sel == 0; pPri = pPri + 1) {
        }
        clz = countLeadingZeros(sel);
        idx = (int)(short)clz;
        handler = *(code **)(DAT_806130b0 + idx * 4);
        if (handler != (code *)0x0) {
            if (4 < idx) {
                DAT_806130b8 = (short)clz;
                tb = FUN_803a0c30();
                DAT_806130b4 = *(int *)(ctx + 0x198);
            }
            DAT_806130c0 = (int)((u64)tb >> 0x20);
            DAT_806130c4 = (int)tb;
            FUN_8039f35c();
            (*(void (*)(int, int))handler)(idx, ctx);
            FUN_8039f39c();
            FUN_8039f868();
            os_load_context(ctx);
        }
    }
    return os_load_context(ctx);
}
