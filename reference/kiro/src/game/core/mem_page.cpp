// Address: 0x80006054 | Name: FUN_80006054 (mem_page_alloc) | Date: 2026-07-23
// Address: 0x80006160 | Name: mem_page_free | Date: 2026-07-23
// Description: Simple page allocator using a bitmap (DAT_80611598).
// Pages are 4KB (0x1000 bytes) starting at base address 0xE0000000 (physical mem mapped).
// DAT_806115a4[n] stores the allocation mask for page n.
// mem_page_alloc: Finds first free page(s) in bitmap, marks as allocated.
// mem_page_free: Clears the bit(s) for a given page address.

#include "common.h"

extern u8 DAT_80611598;    // page allocation bitmap
extern u8 DAT_806115a4[]; // per-page allocation masks
extern s32 DAT_8061159c;   // secondary page pointer
extern s32 DAT_806115a0;   // secondary page info

extern void FUN_803826f0(void);
extern void FUN_8038273c(void);
extern void stub_61ac(void);

// Free a memory page by address
void mem_page_free(s32 addr) {
    u32 offset = (u32)(addr + 0x20000000);
    s32 page_idx = (s32)(offset >> 12);
    if ((s32)offset < 0 && (offset & 0xFFF) != 0) {
        page_idx++;
    }
    DAT_80611598 &= ~DAT_806115a4[page_idx];
}
