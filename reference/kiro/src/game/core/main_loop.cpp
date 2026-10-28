// Address: 0x80005e6c | Name: dispatch_5e6c | Date: 2026-07-23
// Description: Outermost game loop. This is the top-level game execution function.
// Contains the eternal do-while loop that runs the entire game lifecycle:
//   - First iteration: full initialization (level load, entity spawn, etc.)
//   - Subsequent iterations: continue from checkpoint/restart
// Key behaviors:
//   DAT_80611568 = 0 → cold start (full init)
//   DAT_80611568 != 0 → warm restart (skip init, reload state)
//   DAT_8060f96c = exit flag (set by dispatch_5d70's inner loop)
//   DAT_8060f968 = game active flag
//
// Cold start sequence:
//   mem_heap_init, FUN_8000651c, FUN_80181a74, func_text_7594, func_text_7ab4
//   FUN_80029588, FUN_80034bb4 (PAK loading: "pak\\gct.pak", "pak\\arcade_l_103.pak")
//   FUN_8017b3d8, func_text_7348, FUN_8002b27c, FUN_8002a66c (asset loading)
//   func_text_8b00, FUN_8001c6a4, FUN_8013383c, FUN_80133358
//   FUN_80031d18, FUN_80183c50, FUN_801c17a8, FUN_801c1888
//   FUN_8000ec7c, FUN_8002d71c, FUN_80057000, FUN_8002dd80
//   FUN_801332e4, FUN_8003bc88 (check save), FUN_8002f0c8, FUN_8002f174
//   FUN_8003bc90, FUN_801c1a3c, FUN_800379a8, FUN_80033c88, FUN_80031e94
//
// Warm restart sequence:
//   stub_heap_post_init, func_text_7528, func_text_7b94
//   FUN_800295a8, FUN_80031abc, FUN_801c171c, FUN_801c197c
//   FUN_80037ae8, FUN_80033c88, FUN_80031e94, FUN_80183c50
//
// After init: FUN_8005205c, FUN_80130fd8, FUN_8012ddc0, FUN_80130d9c
// Then: FUN_80051924, dispatch_5d70 (inner game loop), FUN_8003338c
// Loop repeats forever (do-while true)

#include "common.h"

extern s32 DAT_80611568;   // init state (0=cold, nonzero=warm)
extern s32 DAT_8060f96c;   // game loop exit flag
extern s32 DAT_8060f968;   // game active flag
extern u32 DAT_805015c0;   // system config flags
extern s32 DAT_80612614;   // game mode ID
extern u32 DAT_806109c4;   // screen width
extern u32 DAT_806109c8;   // screen height

// Placeholder - this function is the outermost game loop and references
// dozens of subsystems. Full implementation requires extensive struct/type work.
void dispatch_5e6c(/* params */) {
    // Viewport setup
    // do {
    //   if (DAT_80611568 == 0) { /* cold start init */ }
    //   else { /* warm restart */ }
    //   // ... run game ...
    //   dispatch_5d70(/* inner game loop */);
    // } while (true);
}
