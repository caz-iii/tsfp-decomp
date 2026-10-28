// Address: 0x80006c44 | Name: mem_alloc | Date: 2026-07-23
// Address: 0x80006da4 | Name: mem_alloc_aligned | Date: 2026-07-23
// Address: 0x80006ee4 | Name: mem_free_top | Date: 2026-07-23
// Address: 0x80006b04 | Name: FUN_80006b04 (heap_setup_boundaries) | Date: 2026-07-23
// Address: 0x80006b60 | Name: mem_heap_init | Date: 2026-07-23
// Address: 0x80006bf8 | Name: mem_heap_reset_counters | Date: 2026-07-23
// Address: 0x80006f74 | Name: mem_heap_free_all_top | Date: 2026-07-23
// Address: 0x80006f94 | Name: FUN_80006f94 (get_free_heap_size) | Date: 2026-07-23
// Address: 0x80006c1c | Name: stub_heap_pre_alloc | Date: 2026-07-23
// Address: 0x80006c20 | Name: FUN_80006c20 (mem_alloc_dispatch) | Date: 2026-07-23
// Address: 0x80006c40 | Name: stub_heap_subsys_init | Date: 2026-07-23
// Address: 0x80006b5c | Name: stub_heap_post_init | Date: 2026-07-23
//
// Description: TimeSplitters: Future Perfect memory allocation system.
// Uses a dual-ended heap with three allocation strategies:
//   type 0: Top-down allocation with 16-byte header (LOCL/MEML magic + size + self-ptr)
//   type 1: Bottom-up allocation (no header, bumps DAT_806115c4 upward)
//   type 2: Static/permanent allocation (bumps DAT_806115b8 upward)
//   type 4: Fatal - calls halt_stub_assert()
//
// Heap layout:
//   DAT_806115b8 = static alloc pointer (grows up from base)
//   DAT_806115bc = boundary between static and dynamic regions (base + 0x69C00)
//   DAT_806115c0 = static region remaining
//   DAT_806115c4 = bottom-up alloc pointer (grows up)
//   DAT_806115c8 = heap end (base + total_size)
//   DAT_806115cc = free space between bottom-up and top-down
//   DAT_806115d0 = top-down alloc pointer (grows down)
//   DAT_806115d4 = allocation mode flag
//   DAT_806115e4 = total heap size
//   DAT_806115e8 = heap base address
//
// Top-down block header (16 bytes before returned pointer):
//   [0x00] 'LOCL' (0x41434F4C)  - magic 1
//   [0x04] 'MEML' (0x4D454D4C)  - magic 2
//   [0x08] size                   - allocation size
//   [0x0C] self_ptr               - pointer to header start (for validation)

#include "common.h"

#define HEAP_MAGIC_LOCL 0x41434F4C
#define HEAP_MAGIC_MEML 0x4D454D4C
#define HEAP_STATIC_REGION_SIZE 0x69C00

extern s32 DAT_806115b8;  // static alloc ptr
extern s32 DAT_806115bc;  // static/dynamic boundary
extern s32 DAT_806115c0;  // static region remaining
extern s32 DAT_806115c4;  // bottom-up alloc ptr
extern s32 DAT_806115c8;  // heap end
extern s32 DAT_806115cc;  // free space
extern u32* DAT_806115d0; // top-down alloc ptr
extern s32 DAT_806115d4;  // mode flag
extern s32 DAT_806115e0;  // secondary alloc ptr
extern s32 DAT_806115e4;  // total size
extern s32 DAT_806115e8;  // base address

extern void OSReport(/* variadic */);
extern void stub_61b8(void);
extern void halt_stub_assert(void);
extern void FUN_8018fba8(void);
extern s32 memset_wrapper(s32 dst, u8 val, u32 size);
extern void stub_heap_post_init(void);
extern void stub_heap_subsys_init(void);

// Setup heap boundaries from base and size
void heap_setup_boundaries(s32 base, s32 size) {
    DAT_806115c8 = base + size;
    DAT_806115bc = base + HEAP_STATIC_REGION_SIZE;
    DAT_806115c0 = DAT_806115bc - base;
    DAT_806115cc = DAT_806115c8 - DAT_806115bc;
    DAT_806115b8 = base;
    DAT_806115c4 = DAT_806115bc;
    DAT_806115d0 = (u32*)DAT_806115c8;
    DAT_806115e4 = size;
    DAT_806115e8 = base;
    FUN_8018fba8();
}

// Initialize/reset heap for level load
void mem_heap_init(void) {
    // Check game mode flags for allocation mode
    extern char DAT_80501524;
    extern char DAT_80501525;
    extern u32 DAT_80501528;

    if (DAT_80501524 == 'e') {
        DAT_806115d4 = 1;
    } else if (DAT_80501525 == '\n' && (DAT_80501528 & 0x40) == 0) {
        DAT_806115d4 = 0;
    }

    DAT_806115c4 = DAT_806115e8 + HEAP_STATIC_REGION_SIZE;
    DAT_806115c8 = DAT_806115e8 + DAT_806115e4;
    DAT_806115cc = DAT_806115c8 - DAT_806115c4;
    memset_wrapper(DAT_806115c4, 0, DAT_806115cc);
    DAT_806115d0 = (u32*)DAT_806115c8;
    stub_heap_post_init();
    stub_heap_subsys_init();
}

// Core allocator (269 xrefs)
s32 mem_alloc(s32 size, s32 type) {
    u32 aligned_size = (u32)(size + 0xF) & 0xFFFFFFF0;

    if (type == 2) {
        // Static allocation - bump DAT_806115b8
        s32 result = DAT_806115b8;
        DAT_806115b8 += aligned_size;
        DAT_806115c0 -= aligned_size;
        return result;
    }

    if (type == 0) {
        // Top-down allocation with header
        if (DAT_806115cc < (s32)(aligned_size + 0x10)) {
            // Out of memory - fatal
            OSReport(/* "memAlloc: Cannot allocate %d bytes (%d free)" */);
            stub_61b8();
            halt_stub_assert();
        }
        DAT_806115d0 = (u32*)((s32)DAT_806115d0 - (aligned_size + 0x10));
        DAT_806115d0[0] = HEAP_MAGIC_LOCL;
        DAT_806115d0[1] = HEAP_MAGIC_MEML;
        DAT_806115d0[2] = aligned_size;
        DAT_806115d0[3] = (u32)DAT_806115d0;
        DAT_806115cc -= (aligned_size + 0x10);
        return (s32)(&DAT_806115d0[4]);
    }

    if (type == 1) {
        // Bottom-up allocation
        if (DAT_806115cc < (s32)aligned_size) {
            OSReport(/* "memAlloc: Cannot allocate %d bytes (%d free)" */);
            stub_61b8();
            halt_stub_assert();
        }
        s32 result = DAT_806115c4;
        DAT_806115c4 += aligned_size;
        DAT_806115cc -= aligned_size;
        return result;
    }

    if (type == 4) {
        halt_stub_assert();
    }

    return 0;
}

// Free top-down allocated block (48 xrefs)
void mem_free_top(s32 ptr) {
    // Validate header magic
    if (*(s32*)(ptr - 0x10) != HEAP_MAGIC_LOCL || *(s32*)(ptr - 0x0C) != HEAP_MAGIC_MEML) {
        halt_stub_assert();
    }
    // Validate self-pointer
    if (*(s32*)(ptr - 4) != (s32)DAT_806115d0) {
        halt_stub_assert();
    }
    // Restore heap pointer
    s32 block_size = *(s32*)(ptr - 8);
    DAT_806115d0 = (u32*)((s32)DAT_806115d0 + block_size + 0x10);
    DAT_806115cc += block_size + 0x10;
}

// Free all top-down allocations
void mem_heap_free_all_top(void) {
    s32 freed = DAT_806115c8 - (s32)DAT_806115d0;
    DAT_806115d0 = (u32*)DAT_806115c8;
    DAT_806115cc += freed;
}

// Get remaining free heap
u32 get_free_heap_size(void) {
    return DAT_806115cc;
}

// Reset counters (cold start only)
void mem_heap_reset_counters(void) {
    extern s32 DAT_80611568;
    if (DAT_80611568 != 0) return;
    DAT_806115c4 = 0;
    DAT_806115c8 = 0;
    DAT_806115cc = 0;
    DAT_806115d0 = 0;
}
