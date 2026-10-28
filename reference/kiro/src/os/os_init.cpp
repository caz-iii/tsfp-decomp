// Dolphin OS initialisation core.
//
// The OS bring-up path reached from system_bootstrap: the top-level __OSInit
// that resets the performance monitor, detects the console type, installs the
// exception-handler table (OSInit / os_set_exception_handler), brings up the
// kernel/DVD, and prints the "Dolphin OS" boot banner; plus the default
// debugger (DB) exception reporter that prints and halts, the OS bootinfo
// accessors (console type, feature-flag test), and a couple of FPSCR/register
// helpers used during bring-up.
//
// Batch: batch_1097
// Source module: os/os_init.cpp

#include "common.h"

// --- OS bootinfo / global state -------------------------------------------
extern void  *DAT_80613038;         // DB exception context block (&DAT_80000040)
extern int    DAT_8061303c;         // DB exception installed flag
extern int    DAT_80613040;         // OS bootinfo block (console type at +0x2c)
extern unsigned int *DAT_80613044;  // boot region descriptor
extern unsigned int  DAT_80613048;
extern unsigned int  DAT_80613278;
extern int    DAT_80613060;         // __OSInit "already ran" guard
extern int    DAT_80613068;         // running-under-emulator/host flag
extern unsigned int DAT_80613070;   // boot timestamp hi
extern unsigned int DAT_80613074;   // boot timestamp lo
extern void  *DAT_80613064;         // OS exception-handler table base
extern int    DAT_8061304c;
extern int    DAT_80613174;
extern double DOUBLE_80613050;      // cached bus/time constant

// Low-memory / hardware globals.
extern unsigned int DAT_80000030;
extern unsigned int DAT_80000034;
extern unsigned int DAT_80000060;
extern unsigned int DAT_800000f4;
extern unsigned char DAT_800030e8;
extern unsigned short DAT_800030e6;
extern unsigned int DAT_80398624;   // OSInit scratch: exception-vector base word
extern unsigned int DAT_805f26fc;
extern unsigned int DAT_805f2700;

// --- SPR/FPSCR intrinsics (os/os_spr_intrinsics.cpp) -----------------------
extern void os_set_mmcr0(unsigned int);
extern void os_set_mmcr1(unsigned int);
extern void os_set_pmc1(unsigned int);
extern void os_set_pmc2(unsigned int);
extern void os_set_pmc3(unsigned int);
extern void os_set_pmc4(unsigned int);
extern void os_hid0_enable_feature(void);
extern unsigned int os_get_hid2(void);
extern void os_set_hid2(unsigned int);

// --- OS bring-up callees (raw addresses) -----------------------------------
extern long long FUN_803a0c50(void);        // read system timer (64-bit)
extern unsigned long long FUN_8039ccb4(void);
extern void FUN_8039cca0(void);
extern void FUN_8039ad60(void *);
extern void FUN_803991c8(int);
extern void FUN_803991c0(int);
extern void FUN_8039f10c(void);
extern void FUN_80398744(void);
extern void FUN_8039d50c(void);
extern void FUN_8039cd1c(void);
extern void FUN_8039ccec(int irq, void *handler);
extern void FUN_8039df2c(void);
extern void FUN_8039a430(void);
extern unsigned long long FUN_80399ae4(void);
extern int FUN_803991b0(void);
extern int FUN_803991b8(void);
extern unsigned int FUN_8039def4(void);
extern int FUN_8039d6e4(void);
extern int trk_call_idle_hook(void);
extern int os_register_version(int, ...);
extern int OSReport(int, ...);
extern void OSInit(void);
extern int DVDInit(int, ...);
extern int FUN_803994a4(int, int);
extern int FUN_803a4c54(unsigned int *, void *, void *);
extern void memset_wrapper(int dst, int val, unsigned int len);
extern void os_halt(void);
extern unsigned long long FUN_8039a104(unsigned long long, ...);

// OSInit callees.
extern void leaf_31e8(unsigned int dst, unsigned int src, unsigned int len);
extern void FUN_80399530(unsigned int addr, unsigned int len);
extern void FUN_803995b4(unsigned int addr, unsigned int len);

// String literals referenced by the banner (defined elsewhere / autogen).
extern char s_DBExceptionDestination_80498090[];

// ---------------------------------------------------------------------------
// os_set_fpscr_ni - set the FPSCR non-IEEE (NI) mode bit (mtfsb1 fp_ni).
// Address: 0x80397b84
// ---------------------------------------------------------------------------
void os_set_fpscr_ni(void)
{
    // mtfsb1 fp_ni : enable non-IEEE-mode floating point.
}

// ---------------------------------------------------------------------------
// os_install_db_exception - install the default debugger (DB) exception vector.
// Points the DB context block at the low-memory slot (&DAT_80000040), writes the
// handler entry, and marks it installed. Called from system_bootstrap just
// before __OSInit. Address: 0x80397b8c
// ---------------------------------------------------------------------------
void os_install_db_exception(void)
{
    DAT_80613038 = (void *)0x80000040;   // &DAT_80000040
    *(unsigned int *)0x80000048 = 0x397bfc;  // DB handler entry
    DAT_8061303c = 1;
}

// ---------------------------------------------------------------------------
// os_db_exception_report - default DB exception handler: report the fault via
// OSReport("DBExceptionDestination"), dump context, then halt. No-return.
// Address: 0x80397bb4
// ---------------------------------------------------------------------------
void os_db_exception_report(int arg)
{
    unsigned int *ctx = (unsigned int *)(*(int *)0xc0 - 0x80000000);
    unsigned long long r = OSReport(arg, s_DBExceptionDestination_80498090,
                                    *(int *)0xc0);
    FUN_8039a104(r, ctx);
    os_halt();   // does not return
}

// ---------------------------------------------------------------------------
// os_bootinfo_test_flag - test bit n of the boot-region feature-flags word
// (DAT_80613038[+4]). Returns the masked bit (nonzero if set). Address: 0x80397c0c
// ---------------------------------------------------------------------------
unsigned int os_bootinfo_test_flag(unsigned int bit)
{
    return *(unsigned int *)((char *)DAT_80613038 + 4) & (1u << (bit & 0xff));
}

// ---------------------------------------------------------------------------
// os_spill_volatiles - spill the volatile GPRs (r3-r10) and, when the FP flag in
// cr1 is set, the volatile FPRs (f1-f8) to a scratch frame, then return. Used as
// a context guard around the OS exception-table setup. Address: 0x80397c28
// ---------------------------------------------------------------------------
void os_spill_volatiles(void)
{
    // Register spill to a local frame that is discarded on return; no effect on
    // the C build (the ABI arg registers are preserved by the compiler already).
}

// ---------------------------------------------------------------------------
// os_get_time_constant - return the cached bus/timer scaling constant.
// Address: 0x80397c78
// ---------------------------------------------------------------------------
double os_get_time_constant(void)
{
    return DOUBLE_80613050;
}

// ---------------------------------------------------------------------------
// os_get_console_type - return the console-type word from the OS bootinfo block
// (DAT_80613040 + 0x2c), defaulting to 0x10000002 (EPPC/Arthur) when unset.
// Address: 0x80397da0
// ---------------------------------------------------------------------------
int os_get_console_type(void)
{
    int type;
    if ((DAT_80613040 == 0) ||
        (type = *(int *)(DAT_80613040 + 0x2c), type == 0)) {
        type = 0x10000002;
    }
    return type;
}

// ---------------------------------------------------------------------------
// os_set_exception_handler - install a handler into the OS exception-handler
// table (DAT_80613064) for exception index n, returning the previous entry.
// Address: 0x8039858c
// ---------------------------------------------------------------------------
void *os_set_exception_handler(unsigned int index, void *handler)
{
    void **slot = (void **)((char *)DAT_80613064 + (index & 0xff) * 4);
    void *prev = *slot;
    *slot = handler;
    return prev;
}

// Additional bring-up callees referenced by OSInit / __OSInit.
extern unsigned int DAT_804981cc;    // per-vector source-stub table
extern unsigned int DAT_80398564;
extern unsigned int DAT_80398614;
extern unsigned int DAT_80398588;
extern unsigned int DAT_803985bc;
extern unsigned int DAT_80398658;
extern unsigned int DAT_80003000;
extern unsigned int os_bootinfo_test_flag_bit;  // guard (unused)

// ---------------------------------------------------------------------------
// OSInit - build the low-memory exception-handler table.
// Copies the 0x24-byte common stub to 0x80000060 (once), then for each of the
// 15 exception vectors copies either the 0x98-byte handler body (when the
// vector is enabled by the boot flags via os_bootinfo_test_flag) or a 4-instr
// "blr-filled" placeholder, flushing each region from the data cache to the
// instruction cache. Finally seeds the dispatch table at DAT_80003000 with the
// default handler for every vector. Address: 0x803982e4
// ---------------------------------------------------------------------------
void OSInit(void)
{
    unsigned int base = DAT_80398624;

    if (DAT_80000060 == 0) {
        os_spill_volatiles();
        leaf_31e8(0x80000060, 0x80398564, 0x24);
        FUN_80399530(0x80000060, 0x24);
        // sync
        FUN_803995b4(0x80000060, 0x24);
    }

    int *stub = (int *)&DAT_804981cc;
    for (unsigned int v = 0; (v & 0xff) < 0xf; v = v + 1) {
        if ((DAT_80613044 == (unsigned int *)0) || (*DAT_80613044 < 2) ||
            (os_bootinfo_test_flag(v) == 0)) {
            DAT_80398624 = base | (v & 0xff);
            if (os_bootinfo_test_flag(v) == 0) {
                unsigned int *p = (unsigned int *)&DAT_80398614;
                p[0] = 0x60000000;   // ori 0,0,0 (nop) placeholder
            } else {
                os_spill_volatiles();
                leaf_31e8(0x80398614, 0x80398588, 4);
            }
            unsigned int dst = *stub + 0x80000000;
            leaf_31e8(dst, 0x803985bc, 0x98);
            FUN_80399530(dst, 0x98);
            // sync
            FUN_803995b4(dst, 0x98);
        } else {
            os_spill_volatiles();
        }
        stub = stub + 1;
    }

    DAT_80613064 = (void *)0x80003000;
    for (unsigned int v = 0; (v & 0xff) < 0xf; v = v + 1) {
        os_set_exception_handler(v, &DAT_80398658);
    }
    DAT_80398624 = base;
    os_spill_volatiles();
}

// ---------------------------------------------------------------------------
// os_kernel_init (__OSInit) - top-level Dolphin OS bring-up.
// Guarded to run once. Snapshots the boot timestamp, resets the performance
// monitor (MMCR0/1, PMC1-4) and enables the HID0 feature bit, resolves the
// boot region descriptor and console type from the bootinfo block, then brings
// up the kernel (OSInit, the alarm/thread/cache subsystems), registers the
// external-interrupt vector 0x16, starts the heap, and prints the "Dolphin OS"
// boot banner: kernel build stamp, console type (Retail / Mac Emulator / PC
// Emulator / EPPC-Arthur / EPPC-Minnow / Development-HW), memory size, arena
// bounds, and the OS version. Clears the unused arena, then DVDInit when not
// running under the host. Address: 0x80397e04
// ---------------------------------------------------------------------------
void os_kernel_init(void)
{
    if (DAT_80613060 != 0) {
        return;
    }
    DAT_80613060 = 1;

    long long now = FUN_803a0c50();
    DAT_80613070 = (unsigned int)((unsigned long long)now >> 0x20);
    DAT_80613074 = (unsigned int)now;

    FUN_8039cca0();
    FUN_8039ad60((void *)0x805f26f0);
    os_set_mmcr0(0);
    os_set_mmcr1(0);
    os_set_pmc1(0);
    os_set_pmc2(0);
    os_set_pmc3(0);
    os_set_pmc4(0);
    os_hid0_enable_feature();
    os_set_fpscr_ni();

    DAT_80613044 = (unsigned int *)0;
    DAT_80613040 = (int)0x80000000;
    if (DAT_800000f4 == 0) {
        if (DAT_80000034 != 0) {
            DAT_80613048 = (unsigned int)*(unsigned char *)((char *)&DAT_800030e8 + 0);
            DAT_80613044 = &DAT_80613048;
            DAT_80613278 = (unsigned int)*(unsigned char *)((char *)&DAT_800030e8 + 1);
        }
    } else {
        DAT_80613044 = (unsigned int *)(DAT_800000f4 + 0xc);
        DAT_80613278 = *(unsigned int *)(DAT_800000f4 + 0x24);
        *((unsigned char *)&DAT_800030e8 + 0) = (unsigned char)*DAT_80613044;
        *((unsigned char *)&DAT_800030e8 + 1) = (unsigned char)DAT_80613278;
    }
    DAT_80613174 = 1;

    int arena_lo = DAT_80000030 ? (int)DAT_80000030 : (int)0x80626200;
    FUN_803991c8(arena_lo);
    if ((*(int *)(DAT_80613040 + 0x30) == 0) && (DAT_80613044 != (unsigned int *)0) &&
        (*DAT_80613044 < 2)) {
        FUN_803991c8(0x8062c1c0);
    }
    int arena_hi = *(int *)(DAT_80613040 + 0x34);
    if (arena_hi == 0) {
        arena_hi = (int)0x81700000;
    }
    FUN_803991c0(arena_hi);

    OSInit();
    FUN_8039f10c();
    FUN_80398744();
    FUN_8039d50c();
    FUN_8039cd1c();
    FUN_8039ccec(0x16, (void *)FUN_8039df2c);
    FUN_8039a430();
    FUN_80399ae4();

    unsigned int hid2 = os_get_hid2();
    os_set_hid2(hid2 & 0xbfffffff);
    if (DAT_80613068 == 0) {
        FUN_8039d6e4();
    }

    // Boot banner: version, kernel build stamp, console type, memory, arena.
    OSReport((int)"Dolphin OS");
    OSReport((int)"Kernel built %s %s");
    OSReport((int)"Console Type : ");

    int console = os_get_console_type();
    unsigned int hi = (unsigned int)console & 0xf0000000;
    unsigned int lo = (unsigned int)console & 0x0fffffff;
    if (hi == 0x00000000) {
        OSReport((int)"Retail %d");
    } else if (hi == 0x10000000 || hi == 0x20000000) {
        if (console == 0x10000002) {
            OSReport((int)"EPPC/Arthur");
        } else if (lo == 0x0) {
            OSReport((int)"Mac Emulator");
        } else if (lo < 0x2) {
            OSReport((int)"PC Emulator");
        } else if (lo < 0x4) {
            OSReport((int)"EPPC/Minnow");
        } else {
            OSReport((int)"Development HW %d 0x%08x");
        }
    } else {
        OSReport((int)"<unknown %d 0x%08x>");
    }

    OSReport((int)"Memory %d MB", *(unsigned int *)(DAT_80613040 + 0x28) >> 0x14);
    int arena_top = FUN_803991b0();
    int arena_bot = FUN_803991b8();
    OSReport((int)"Arena : 0x%x - 0x%x", arena_bot, arena_top);
    os_register_version((int)"OS version");

    if ((DAT_80613044 != (unsigned int *)0) && (1 < *DAT_80613044)) {
        trk_call_idle_hook();
    }

    // Zero the free arena (single- or split-range depending on the reserved hole).
    unsigned int dbg = FUN_8039def4();
    if ((dbg & 0x80000000) == 0 || DAT_805f26fc == 0) {
        int top = FUN_803991b0();
        int bot = FUN_803991b8();
        memset_wrapper(bot, 0, (unsigned int)(top - bot));
    } else {
        unsigned int bot = (unsigned int)FUN_803991b8();
        if (bot < DAT_805f26fc) {
            unsigned int top = (unsigned int)FUN_803991b0();
            if (DAT_805f26fc < top) {
                int b = FUN_803991b8();
                memset_wrapper(b, 0, DAT_805f26fc - (unsigned int)b);
                unsigned int t2 = (unsigned int)FUN_803991b0();
                if (DAT_805f2700 < t2) {
                    int t = FUN_803991b0();
                    memset_wrapper((int)DAT_805f2700, 0, (unsigned int)t - DAT_805f2700);
                }
            } else {
                int t = FUN_803991b0();
                int b = FUN_803991b8();
                memset_wrapper(b, 0, (unsigned int)(t - b));
            }
        }
    }

    FUN_8039ccb4();
    if (DAT_80613068 == 0) {
        DVDInit((int)"DVD");
        if (DAT_8061304c == 0) {
            FUN_803994a4(0x805f26a0, 0x20);
            FUN_803a4c54((unsigned int *)0x805f26c0, (void *)0x805f26a0, (void *)0x80397dc8);
        } else {
            DAT_800030e6 = 0x9000;
        }
    }
}
