// Address: 0x8000574c | Name: func_text_574c | Date: 2026-07-23
// Description: Game start/entry initialization. Clears the game state buffer at 0x804a0040,
// checks for 'g' flag in DAT_804a008c, resets frame counter, calls game_entry_point.
// This is the transition from the OS bootstrap to actual game code.

#include "common.h"

extern s32 DAT_8060f948;   // frame counter or game state flag
extern char DAT_804a008c;  // mode flag character

extern s32 func_text_3100(s32 dst, u8 value, u32 size);
extern void FUN_801a6f7c(/* args */);
extern void FUN_80031a40(/* args */);
extern void FUN_800067d0(/* args */);
extern void stub_6050(void);
extern void game_entry_point(/* args */);

void func_text_574c(void) {
    // Clear 0x98 bytes of game state
    func_text_3100(0x804A0040, 0, 0x98);

    FUN_801a6f7c(/* args */);
    FUN_80031a40(/* args, 0x804a0040 */);

    if (DAT_804a008c != 'g') {
        DAT_8060f948 = 0;
        FUN_800067d0(/* args */);
    }

    stub_6050();
    game_entry_point(/* args, 1 */);
}
