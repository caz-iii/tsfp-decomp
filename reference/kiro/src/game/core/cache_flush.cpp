// Address: 0x80005510 | Name: leaf_5510 | Date: 2026-07-23
// Description: Flushes data cache and invalidates instruction cache over a memory range.
// This is a standard PowerPC cache coherency routine used after code patching or DMA.

#include "common.h"

void leaf_5510(u32 addr, s32 length) {
    u32 aligned = addr & 0xFFFFFFF1;
    s32 remaining = length + (s32)(addr - aligned);

    do {
        asm volatile("dcbst 0, %0" :: "r"(aligned));
        asm volatile("sync");
        asm volatile("icbi 0, %0" :: "r"(aligned));
        aligned += 8;
        remaining -= 8;
    } while (remaining >= 0);

    asm volatile("isync");
}
