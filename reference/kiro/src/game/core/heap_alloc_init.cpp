// Address: 0x800057c0 | Name: func_text_57c0 | Date: 2026-07-23
// Description: Heap allocation initialization. Calls FUN_8017f240 for initial setup,
// then allocates the main game heap from arena. Aligns allocation to 64-byte boundary.
// DAT_80611540 = heap base pointer
// DAT_8060f944 = remaining heap size
// DAT_806113c0 = arena handle

#include "common.h"

extern void* DAT_80611540;  // heap base
extern s32 DAT_8060f944;    // heap remaining size
extern s32 DAT_806113c0;    // arena handle

extern void FUN_8017f240(/* params */);
extern void* FUN_80398f4c(s32 arena, s32 size);
extern void FUN_8017e02c(void);

void func_text_57c0(/* params */) {
    FUN_8017f240(/* forward params */);

    // Allocate heap - try decreasing sizes until allocation succeeds
    while (DAT_8060f944 > 0) {
        DAT_80611540 = FUN_80398f4c(DAT_806113c0, DAT_8060f944 + 0x40);
        if (DAT_80611540 != NULL) break;
        DAT_8060f944 -= 0x40;
    }

    // Align to 64-byte boundary
    u32 aligned = ((u32)DAT_80611540 + 0x3F) & 0xFFFFFFC0;
    DAT_8060f944 -= (s32)(aligned - (u32)DAT_80611540);
    DAT_80611540 = (void*)aligned;

    FUN_8017e02c();
}
