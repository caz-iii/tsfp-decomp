// Addresses: Multiple | Date: 2026-07-16T00:00:00Z
// Description: Input/controller state management. These functions read controller
//              data, track button presses/releases, and provide the per-frame
//              input state to the game logic.

#include "types.h"
#include "sdk_stubs.h"
#include "forward_decls.h"

extern s32 DAT_80611690;
extern s32 DAT_80612e70;
extern u32 DAT_804b1130[];
extern u32 DAT_804b114c[];
extern u32 DAT_804b1150[];
extern u32 DAT_804b1178[];
extern u32 DAT_804b1240[];
extern u32 DAT_804b123c[];
extern u32 DAT_804b1268[];

// Address: 0x8000c690 | Name: input_get_buttons_pressed | Xrefs: 57
// Description: Returns newly-pressed buttons for the given player index.
//              (current_held & ~previous_held) = buttons just pressed this frame.
u32 input_get_buttons_pressed(s32 param_1) {
    if (DAT_80611690 != 0) {
        return 0;
    }
    if (DAT_80612e70 != 0) {
        return *(u32*)((u8*)&DAT_804b123c + param_1 * 0x3c) &
               ~*(u32*)((u8*)&DAT_804b1268 + param_1 * 0x3c);
    }
    return DAT_804b114c[DAT_804b1130[param_1] * 0xf] &
           ~DAT_804b1178[DAT_804b1130[param_1] * 0xf];
}

// Address: 0x8000c6fc | Name: input_get_buttons_held | Xrefs: 97
// Description: Returns currently-held buttons for the given player index.
u32 input_get_buttons_held(s32 param_1) {
    if (DAT_80611690 != 0) {
        return 0;
    }
    if (DAT_80612e70 != 0) {
        return *(u32*)((u8*)&DAT_804b1240 + param_1 * 0x3c);
    }
    return DAT_804b1150[DAT_804b1130[param_1] * 0xf];
}

// Address: 0x8000c7b4 | Name: input_clear_buttons | Xrefs: 17
// Description: Clears specific buttons from a player's held state and marks
//              them as consumed in the mask field.
void input_clear_buttons(s32 param_1, u32 param_2) {
    u32* puVar1;

    if (DAT_80612e70 == 0) {
        puVar1 = (u32*)((u8*)&DAT_804b1130 + DAT_804b1130[param_1] * 0x3c + 0x10);
    } else {
        puVar1 = (u32*)((u8*)&DAT_804b1230 + param_1 * 0x3c);
    }
    puVar1[0xe] = puVar1[0xe] | param_2;
    puVar1[4] = puVar1[4] & ~param_2;
    return;
}

// Address: 0x8000c80c | Name: input_reset_pad | Xrefs: 19
// Description: Resets a controller's state: clears all buttons and sets
//              analog stick axes to neutral position (0x80 = center).
void input_reset_pad(s32 param_1) {
    u32* puVar3;
    s32 iVar1;
    s32 iVar2;

    if (DAT_80612e70 == 0) {
        puVar3 = (u32*)((u8*)&DAT_804b1130 + DAT_804b1130[param_1] * 0x3c + 0x10);
    } else {
        puVar3 = (u32*)((u8*)&DAT_804b1230 + param_1 * 0x3c);
    }
    input_clear_buttons(param_1, 0xfff);
    iVar2 = 0;
    for (s32 i = 0; i < 7; i++) {
        *(u32*)((u8*)puVar3 + iVar2 + 0x18) = 0x80;
        iVar2 += 4;
    }
    return;
}
