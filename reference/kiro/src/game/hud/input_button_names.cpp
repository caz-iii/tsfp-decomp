// Address: 0x8004f2fc | Name: input_get_confirm_button_name | Date: 2026-08-10
// Address: 0x8004f350 | Name: input_get_cancel_button_name | Date: 2026-08-10
// Address: 0x8004f3a8 | Name: input_get_action_button_name | Date: 2026-08-10
// Address: 0x8004f400 | Name: input_get_special_button_name | Date: 2026-08-10
//
// Input Button Name System
// Returns localized button name strings for the current controller configuration.
//
// The button name table at 0x803fe918 has 5 entries (controller modes 0-4),
// each 0x14 (20) bytes containing 5 string IDs:
//   +0x00: Confirm button name (A)
//   +0x04: Cancel button name (B)
//   +0x08: Action button name (X)
//   +0x0C: Special button name (Y)
//   +0x10: (unused/reserved)
//
// Controller mode is retrieved via FUN_80050704 (returns current pad config index).
// String IDs are resolved via get_string_by_id to localized text.
// Falls back to empty string at DAT_806103f4 if mode is out of range.

#include "common.h"

// Button name lookup table (5 modes x 5 string IDs = 20 bytes each)
extern u32 g_button_name_table[];  // DAT_803fe918

// Empty string fallback
extern char g_empty_string[];      // DAT_806103f4

// External functions
extern int input_get_controller_mode(void);  // FUN_80050704
extern int input_get_pad_config(void);       // FUN_8005070c (variant)
extern char* get_string_by_id(int string_id);

// -----------------------------------------------------------------------------
// Address: 0x8004f2fc
// Returns the localized name of the confirm button (typically "A" on GameCube).
// Lookup: g_button_name_table[mode * 5 + 0] (offset +0x00 in 0x14-stride entry).
// Falls back to empty string if controller mode is out of range [0,4].
// Note: uses input_get_pad_config (0x8005070c) for the actual index lookup,
// while input_get_controller_mode is used for range validation.
// -----------------------------------------------------------------------------
char* input_get_confirm_button_name(void)
{
    int mode = input_get_controller_mode();

    if (mode < 0 || mode > 4) {
        return g_empty_string;
    }

    int config = input_get_pad_config();
    int string_id = *(int*)((u8*)&g_button_name_table + config * 0x14 + 0x00);
    return get_string_by_id(string_id);
}

// -----------------------------------------------------------------------------
// Address: 0x8004f350
// Returns the localized name of the cancel button (typically "B" on GameCube).
// Lookup: g_button_name_table[mode * 5 + 1] (offset +0x04 in 0x14-stride entry).
// -----------------------------------------------------------------------------
char* input_get_cancel_button_name(void)
{
    int mode = input_get_controller_mode();

    if (mode < 0 || mode > 4) {
        return g_empty_string;
    }

    int config = input_get_controller_mode();
    int string_id = *(int*)((u8*)&g_button_name_table + config * 0x14 + 0x04);
    return get_string_by_id(string_id);
}

// -----------------------------------------------------------------------------
// Address: 0x8004f3a8
// Returns the localized name of the action button (typically "X" on GameCube).
// Lookup: g_button_name_table[mode * 5 + 2] (offset +0x08 in 0x14-stride entry).
// -----------------------------------------------------------------------------
char* input_get_action_button_name(void)
{
    int mode = input_get_controller_mode();

    if (mode < 0 || mode > 4) {
        return g_empty_string;
    }

    int config = input_get_controller_mode();
    int string_id = *(int*)((u8*)&g_button_name_table + config * 0x14 + 0x08);
    return get_string_by_id(string_id);
}

// -----------------------------------------------------------------------------
// Address: 0x8004f400
// Returns the localized name of the special button (typically "Y" on GameCube).
// Lookup: g_button_name_table[mode * 5 + 3] (offset +0x0C in 0x14-stride entry).
// -----------------------------------------------------------------------------
char* input_get_special_button_name(void)
{
    int mode = input_get_controller_mode();

    if (mode < 0 || mode > 4) {
        return g_empty_string;
    }

    int config = input_get_controller_mode();
    int string_id = *(int*)((u8*)&g_button_name_table + config * 0x14 + 0x0C);
    return get_string_by_id(string_id);
}
