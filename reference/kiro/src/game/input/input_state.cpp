// Address: 0x8000c100 | Name: input_check_skip_cutscene | Date: 2026-07-23
// Address: 0x8000c15c | Name: stub_c15c | Date: 2026-07-23
// Address: 0x8000c160 | Name: input_find_controller_index | Date: 2026-07-23
// Address: 0x8000c1a0 | Name: input_get_controller_id | Date: 2026-07-23
// Address: 0x8000c1b4 | Name: input_state_clear | Date: 2026-07-23
// Address: 0x8000c1e8 | Name: input_get_active_count | Date: 2026-07-23
// Address: 0x8000c234 | Name: input_is_connected | Date: 2026-07-23
// Address: 0x8000c24c | Name: input_sort_controllers | Date: 2026-07-23
// Address: 0x8000c2fc | Name: input_process_analog | Date: 2026-07-23
// Address: 0x8000c3cc | Name: input_poll_controller | Date: 2026-07-23
// Address: 0x8000c420 | Name: input_any_activity | Date: 2026-07-23
// Address: 0x8000c4e4 | Name: input_update_all | Date: 2026-07-23
// Address: 0x8000c690 | Name: input_get_buttons_pressed | Date: 2026-07-23
// Address: 0x8000c6fc | Name: input_get_buttons_held | Date: 2026-07-23
// Address: 0x8000c758 | Name: input_get_buttons_released | Date: 2026-07-23
//
// Description: Input system for TimeSplitters: Future Perfect.
// Supports up to 4 controllers (GameCube PAD) with button mapping for multiplayer.
//
// Key globals:
//   DAT_804b1130[4] = controller-to-player mapping (which port → which player)
//   DAT_804b1140 + N*0x3C = per-controller state struct (0x3C bytes each, 8 slots)
//   DAT_804b1264 + N*0x3C = per-player mapped state (for network play)
//   DAT_80611690 = input disabled flag (during loading/transitions)
//   DAT_80612e70 = network play active flag
//   DAT_8060f9e0 = active player count
//
// Per-controller state struct (0x3C bytes):
//   [0x00] connected flag
//   [0x04] unused/pad ID
//   [0x08-0x14] button state fields
//   [0x18-0x34] analog axis values (7 axes, default 0x80 = center)
//
// Button masks (GameCube):
//   0x0100 = A, 0x0200 = B, 0x0400 = X, 0x0800 = Y
//   0x0010 = Start, 0x0001 = DPad Left, 0x0002 = Right, 0x0004 = Down, 0x0008 = Up
//   0x0040 = Z, 0x0080 = ??, 0x1000 = L, 0x2000 = R
//   Skip cutscene mask: 0x81C0 (A + Start + Z + ??)
//
// input_get_buttons_pressed (57 xrefs): current & ~previous (newly pressed this frame)
// input_get_buttons_held (97 xrefs): currently held buttons
// input_process_analog: Applies deadzone + sensitivity curve to analog axes

#include "common.h"

// Returns newly-pressed buttons for player
u32 input_get_buttons_pressed(s32 player) {
    extern s32 DAT_80611690;
    extern s32 DAT_80612e70;
    if (DAT_80611690 != 0) return 0;
    // Network mode uses per-player mapped state
    // Local mode uses controller-to-player mapping
    // Returns: current_buttons & ~previous_buttons
}

// Returns currently-held buttons
u32 input_get_buttons_held(s32 player) {
    extern s32 DAT_80611690;
    extern s32 DAT_80612e70;
    if (DAT_80611690 != 0) return 0;
    // Same mapping logic as pressed, but returns raw current state
}
