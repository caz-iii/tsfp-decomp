// Address: 0x80005e04 | Name: func_text_5e04 | Date: 2026-07-23
// Description: Per-frame update wrapper. Increments frame counter (DAT_80611594),
// resets per-frame accumulators, calls dispatch_58a8 (main frame render),
// then calls post-frame cleanup and double-buffer toggle.

#include "common.h"

extern s32 DAT_80611570;   // per-frame accumulator 0
extern s32 DAT_80611574;   // per-frame accumulator 1
extern s32 DAT_80611578;   // per-frame accumulator 2
extern s32 DAT_8061157c;   // per-frame accumulator 3
extern s32 DAT_80611580;   // per-frame accumulator 4
extern s32 DAT_80611584;   // per-frame accumulator 5
extern s32 DAT_80611588;   // per-frame accumulator 6
extern s32 DAT_80611594;   // frame counter

extern void dispatch_58a8(/* params */);
extern void FUN_80130fd8(void);
extern void leaf_5e5c(void);
extern void FUN_80130d9c(void);

void func_text_5e04(/* params */) {
    s32 prev_frame = DAT_80611594;

    // Reset per-frame accumulators
    DAT_80611570 = 0;
    DAT_80611594 = DAT_80611594 + 1;
    DAT_80611574 = 0;
    DAT_80611578 = 0;
    DAT_80611580 = 0;
    DAT_80611584 = 0;
    DAT_8061157c = 0;
    DAT_80611588 = 0;

    // Main frame render/update
    dispatch_58a8(/* params, prev_frame, 0 */);

    // Post-frame
    FUN_80130fd8();
    leaf_5e5c();      // toggle double-buffer flag
    FUN_80130d9c();
}
