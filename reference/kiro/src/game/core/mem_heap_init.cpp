// Address: Multiple | Date: 2026-07-16T00:00:00Z
// Description: Heap initialization, reset, and utility functions for the game's
//              custom memory allocator.

#include "types.h"
#include "sdk_stubs.h"
#include "forward_decls.h"

extern s32 DAT_80611568;
extern s32 DAT_806115c4;
extern s32 DAT_806115c8;
extern s32 DAT_806115cc;
extern u32* DAT_806115d0;
extern s32 DAT_806115d4;
extern s32 DAT_806115e4;
extern s32 DAT_806115e8;
extern s32 DAT_806115fc;
extern s32 DAT_80611600;
extern u8 DAT_80501524;
extern u8 DAT_80501525;
extern u8 DAT_80501528;

// Forward declarations
extern "C" void func_text_3100(s32, s32, s32);
extern "C" void FUN_80006b5c(void);
extern "C" void FUN_80006c40(void);

// Address: 0x80006bf8 | Name: mem_heap_reset_counters
// Resets heap tracking counters to zero (if not in a locked state)
void mem_heap_reset_counters(void) {
    if (DAT_80611568 != 0) {
        return;
    }
    DAT_806115c4 = 0;
    DAT_806115c8 = 0;
    DAT_806115cc = 0;
    DAT_806115d0 = 0;
    return;
}

// Address: 0x80006b60 | Name: mem_heap_init
// Initializes the main game heap. Sets up heap boundaries based on platform
// configuration, zeroes the heap, and calls subsystem init stubs.
void mem_heap_init(void) {
    if (DAT_80501524 == 'e') {
        DAT_806115d4 = 1;
    }
    else if ((DAT_80501525 == '\n') && ((DAT_80501528 & 0x40) == 0)) {
        DAT_806115d4 = 0;
    }
    DAT_806115c4 = DAT_806115e8 + 0x69c00;
    DAT_806115c8 = DAT_806115e8 + DAT_806115e4;
    DAT_806115cc = DAT_806115c8 - DAT_806115c4;
    func_text_3100(DAT_806115c4, 0, DAT_806115cc);
    DAT_806115d0 = (u32*)DAT_806115c8;
    FUN_80006b5c();
    FUN_80006c40();
    return;
}

// Address: 0x80006f74 | Name: mem_heap_free_all_top
// Frees all top-allocated blocks by resetting the top pointer back to heap end.
void mem_heap_free_all_top(void) {
    s32 iVar1;

    iVar1 = DAT_806115c8 - (s32)DAT_806115d0;
    DAT_806115d0 = (u32*)DAT_806115c8;
    DAT_806115cc = DAT_806115cc + iVar1;
    return;
}

// Address: 0x800074cc | Name: mem_advance_secondary
// Advances a secondary allocator pointer by param_1 bytes (aligned to 32).
void mem_advance_secondary(s32 param_1) {
    u32 uVar1;

    uVar1 = (u32)(param_1 + 0x1f) & 0xffffffe0;
    DAT_80611600 = DAT_80611600 + uVar1;
    DAT_806115fc = DAT_806115fc + uVar1;
    return;
}

// Address: 0x80006b5c | Name: stub_heap_post_init
// Empty stub - called after heap init
void stub_heap_post_init(void) {
    return;
}

// Address: 0x80006c1c | Name: stub_heap_pre_alloc
// Empty stub - called before allocation in some paths
void stub_heap_pre_alloc(void) {
    return;
}

// Address: 0x80006c40 | Name: stub_heap_subsys_init
// Empty stub - subsystem init called from mem_heap_init
void stub_heap_subsys_init(void) {
    return;
}
