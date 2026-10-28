// Address: 0x8000602c | Name: game_entry_point | Date: 2026-07-23
// Description: The game's main entry point after OS bootstrap. Initializes video/display
// (FUN_8017f490, FUN_8018064c, FUN_80180850), then enters the eternal game loop.
// Called from game_start_init. Never returns.
// Call chain: system_bootstrap → game_start_init → game_entry_point → outer_game_loop (forever)

#include "common.h"

extern void FUN_8017f490(void);
extern void FUN_8018064c(/* args */);
extern void FUN_80180850(void);
extern void game_main_loop(void);
extern void outer_game_loop(/* args */);

void game_entry_point(/* params */) {
    FUN_8017f490();     // Video init
    FUN_8018064c(/* args */);  // Display setup
    FUN_80180850();     // VI configure

    game_main_loop();

    // Eternal loop - game never exits
    do {
        outer_game_loop(/* args */);
    } while (true);
}

// Address: 0x8016bf88 | Name: boot_frame_driver | Date: 2026-09-06 | batch_442
// Description: Top-level boot/game-frame driver wrapper. Called from
// system_bootstrap: stashes its two arguments into globals (DAT_80612248/24c),
// runs the main game tick (dispatch_5690 @ 0x80005690) then the game start/entry
// init (func_text_574c @ 0x8000574c), and returns 0.
extern int DAT_80612248;
extern int DAT_8061224c;
extern void dispatch_5690(void);   // 0x80005690 main game tick
extern void func_text_574c(void);  // 0x8000574c game start/entry init
int boot_frame_driver(int param_1, int param_2) {
    DAT_80612248 = param_1;
    DAT_8061224c = param_2;
    dispatch_5690();
    func_text_574c();
    return 0;
}
