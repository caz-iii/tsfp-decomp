// Address: 0x8000542c | Name: func_text_542c | Date: 2026-07-23
// Description: Copies initialized data sections and clears BSS sections at boot.
// Iterates a table of (src, dst, size) triples to copy code/data sections,
// then iterates a table of (dst, size) pairs to zero-fill BSS.
// Standard CRT startup pattern for embedded systems.

#include "common.h"

extern void* PTR_func_text_3100_80005544;  // Section copy table start
extern void* PTR_DAT_800055b0;             // BSS clear table start

extern void leaf_31e8(u32 src, u32 dst, s32 size);  // memcpy
extern void leaf_5510(u32 addr, s32 size);           // cache flush
extern s32 func_text_3100(s32 dst, u8 value, u32 size);  // memset

void func_text_542c(void) {
    // Copy initialized data sections
    void** entry = &PTR_func_text_3100_80005544;
    while (entry[2] != NULL) {
        void* dst = entry[1];
        void* src = entry[0];
        if (dst != src) {
            leaf_31e8((u32)dst, (u32)src, (s32)entry[2]);
            leaf_5510((u32)dst, (s32)entry[2]);
        }
        entry += 3;
    }

    // Zero-fill BSS sections
    void** bss_entry = &PTR_DAT_800055b0;
    while (bss_entry[1] != NULL) {
        func_text_3100((s32)bss_entry[0], 0, (u32)bss_entry[1]);
        bss_entry += 2;
    }
}
