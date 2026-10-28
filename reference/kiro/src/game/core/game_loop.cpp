// Address: 0x80005d70 | Name: dispatch_5d70 | Date: 2026-07-23
// Description: Inner game loop. Runs until DAT_8060f96c becomes non-zero (exit signal).
// Each iteration: wait for vsync, run frame update (func_text_5e04), handle timing,
// present frame. After loop exits, runs 5 cleanup iterations via FUN_80182184.

#include "common.h"

extern s32 DAT_8060f96c;  // game loop exit flag

extern void FUN_80006f9c(void);
extern void FUN_803994d0(void* heap, s32 size);
extern void FUN_8017f4c8(void);
extern void FUN_8017f3a4(void);
extern void func_text_5e04(/* params */);
extern void FUN_8017d45c(void);
extern void FUN_8018188c(void);
extern void FUN_8017f538(/* params */);
extern void FUN_8017f300(s32 param);
extern void FUN_80182184(/* params */);
extern void FUN_80181c50(void);

extern void* DAT_80611540;
extern s32 DAT_8060f944;

void dispatch_5d70(/* params */) {
    FUN_80006f9c();
    FUN_803994d0(DAT_80611540, DAT_8060f944);

    // Main game loop
    while (DAT_8060f96c == 0) {
        FUN_8017f4c8();          // wait for vsync / timing
        FUN_8017f3a4();          // begin frame
        func_text_5e04(/* params */);  // full frame update + render
        FUN_8017d45c();          // end frame logic
        FUN_8018188c();          // swap buffers / present
        FUN_8017f538(/* params */);
        FUN_8017f300(1);         // advance frame counter
    }

    // Cleanup: run 5 iterations of shutdown
    for (s32 i = 4; i >= 0; i--) {
        FUN_80182184(/* params */);
    }

    FUN_80181c50();
}
