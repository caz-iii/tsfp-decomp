// Address: 0x803998ec | Name: os_l2cache (Dolphin OS L2 cache init + cache bring-up) | Date: 2026-09-14T22:45:00Z
//
// Gekko L2 cache initialisation and the top-level cache bring-up run during
// __OSInit. The L2 cache is controlled through the L2CR SPR:
//   bit 0x80000000 = L2E   (L2 enable)
//   bit 0x00400000 = L2I   (global invalidate)
//   bit 0x00200000 = L2IP  (invalidate-in-progress, cleared when done)
//   bit 0x00000001 = L2IP status polled during the invalidate
//
// os_cache_init enables the instruction cache and locked cache via HID0, then if
// the L2 is not already enabled runs a global invalidate (through
// os_l2_global_invalidate) and turns the L2 on. It brackets the GX/gdev flushes
// that must bracket the invalidate.

#include "common.h"

extern void sync(int);
extern void os_spill_volatiles(void);

// L2CR SPR accessors
extern uint os_get_l2cr(void);
extern void os_set_l2cr(uint value);

// HID0 accessors + feature-bit enable helpers (os_cache.cpp / os_locked_cache.cpp)
extern uint os_get_hid0(void);
extern uint os_enable_icache_hid0(void);          // 0x803995f8
extern uint os_enable_locked_cache_hid0(void);    // 0x80399490

// GX/gdev flush hooks bracketing the L2 invalidate
extern void gdev_hal_flush_c(void);
extern void gdev_hal_stub_d(void);

// downstream init hook (future batch)
extern int  FUN_8039a624(ushort param_1, int param_2);

// os_l2_global_invalidate 0x803998ec  (L2GlobalInvalidate)
// Disables the L2 (clear L2E), issues a global invalidate (set L2I), waits for the
// invalidate to start and finish (poll L2IP), then clears L2I and spins until the
// controller reports idle, spilling volatiles while it waits.
void os_l2_global_invalidate(void)
{
    uint l2cr;

    sync(0);
    l2cr = os_get_l2cr();
    os_set_l2cr(l2cr & 0x7fffffff);        // clear L2E
    sync(0);
    l2cr = os_get_l2cr();
    os_set_l2cr(l2cr | 0x200000);          // set L2I (global invalidate)
    do {
        l2cr = os_get_l2cr();
    } while ((l2cr & 1) != 0);
    l2cr = os_get_l2cr();
    os_set_l2cr(l2cr & 0xffdfffff);        // clear L2I
    while (l2cr = os_get_l2cr(), (l2cr & 1) != 0) {
        os_spill_volatiles();
    }
}

// os_cache_init 0x80399ae4  (__OSCacheInit)
// Top-level cache bring-up from os_kernel_init / os_subsystem_init: enables the
// instruction cache and locked cache via HID0 if not already on, and if the L2 is
// disabled runs a bracketed global invalidate then enables the L2 (set L2E, clear
// L2I). Finally registers the machine-check / DMA-error handler via FUN_8039a624.
void os_cache_init(void)
{
    uint hid0;
    uint l2cr;

    hid0 = os_get_hid0();
    if ((hid0 & 0x8000) == 0) {
        os_enable_icache_hid0();
        os_spill_volatiles();
    }
    hid0 = os_get_hid0();
    if ((hid0 & 0x4000) == 0) {
        os_enable_locked_cache_hid0();
        os_spill_volatiles();
    }
    l2cr = os_get_l2cr();
    if ((l2cr & 0x80000000) == 0) {
        gdev_hal_flush_c();
        sync(0);
        gdev_hal_stub_d();
        sync(0);
        sync(0);
        l2cr = os_get_l2cr();
        os_set_l2cr(l2cr & 0x7fffffff);        // clear L2E
        sync(0);
        os_l2_global_invalidate();
        gdev_hal_stub_d();
        l2cr = os_get_l2cr();
        os_set_l2cr((l2cr & 0xffdfffff) | 0x80000000);   // clear L2I, set L2E
        os_spill_volatiles();
    }
    // register the machine-check / DMA-error handler (0x80399984, sign-extended constant)
    FUN_8039a624(1, (int)0x80399984);
    os_spill_volatiles();
}
