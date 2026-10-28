// =============================================================================
// hud_keyboard.cpp - On-screen keyboard layout and input parsing
// TimeSplitters: Future Perfect (GameCube) decompilation
// Batch 145 | Date: 2026-08-10
// =============================================================================

#include "hud.h"

// Forward declarations
extern void FUN_803826dc(void);  // returns pair (string_ptr, flags)
extern void FUN_80382728(void);
extern void hud_screen_mark_hidden(int element);
extern int hud_util_power_of_10(int exponent);

// Layout table pointers (0x803fe010 - 0x803fe6e4)
extern void* PTR_DAT_803fe010;   // alphanumeric lowercase
extern void* PTR_DAT_803fe0e0;   // alphanumeric uppercase
extern void* PTR_DAT_803fe1b0;   // numbers only
extern void* PTR_DAT_803fe420;   // alpha lowercase
extern void* PTR_DAT_803fe4b4;   // alpha uppercase
extern void* PTR_DAT_803fe548;   // alpha numbers
extern void* PTR_DAT_803fe5dc;   // alpha extended lowercase
extern void* PTR_DAT_803fe670;   // alpha extended uppercase

extern char s_1234567890abcdefghijklmnopqrstuv_803fe0b4[];
extern char s_1234567890ABCDEFGHIJKLMNOPQRSTUV_803fe184[];
extern char s_1234567890_803fe254[];
extern char s_abcdefghijklmnopqrstuvwxyz___803fe494[];
extern char s_ABCDEFGHIJKLMNOPQRSTUVWXYZ___803fe528[];
extern char s_1234567890___________________803fe5bc[];
extern char DAT_803fe280;
extern char DAT_803fe324;
extern char DAT_803fe350;
extern char DAT_803fe3f4;
extern char DAT_803fe650;
extern char DAT_803fe6e4;

// =============================================================================
// Address: 0x80049f1c | hud_parse_string_to_value
// Parses a numeric string back to an integer value.
// Walks string backwards, extracts digit characters ('0'-'9'),
// multiplies by appropriate power of 10.
// Handles time mode (flag 0x20) with different positional weights.
// =============================================================================
void hud_parse_string_to_value(void)
{
    char* str;
    unsigned int flags;  // in_r6
    int length;
    int digit_index;

    // FUN_803826dc returns pair: string pointer (high), flags (low)
    // str = ...; flags = ...;
    FUN_803826dc();

    length = 0;
    digit_index = 0;

    // Count string length
    char* p = str;
    while (*p != '\0') {
        length++;
        p++;
    }

    // Walk backwards
    length--;
    p = str + length;

    for (; length >= 0; length--) {
        if (*p >= '0' && *p <= '9') {
            if ((flags & 0x20) == 0) {
                // Normal mode: standard positional value
                hud_util_power_of_10(digit_index);
            } else if (digit_index < 2) {
                // Time mode: seconds portion (same as normal)
                hud_util_power_of_10(digit_index);
            } else {
                // Time mode: minutes portion (offset by -2)
                hud_util_power_of_10(digit_index - 2);
            }
            digit_index++;
        }
        p--;
    }

    FUN_80382728();
}

// =============================================================================
// Address: 0x8004a01c | hud_keyboard_get_layout
// Returns keyboard layout (character grid pointer + display string) based on
// input mode flags.
// Modes: flag 0x80 = alpha mode (lowercase/uppercase/mixed)
//        else = numeric mode
// Sub-flags: 0x2/0x4 = case variants (both = mixed)
//            0x8 = numbers only
//            0x1000 = extended chars
// Layout tables at 0x803fe010-0x803fe6e4
// =============================================================================
void** hud_keyboard_get_layout(unsigned int mode_flags, void** out_display_string)
{
    void** layout;

    if ((mode_flags & 0x80) == 0) {
        // Alphanumeric mode
        layout = &PTR_DAT_803fe010;
        if (out_display_string != NULL) {
            *out_display_string = s_1234567890abcdefghijklmnopqrstuv_803fe0b4;
        }

        // Check case flags
        if (((mode_flags & 2) != 0) || ((mode_flags & 4) != 0)) {
            if ((mode_flags & 6) != 6) {
                // Uppercase variant
                layout = &PTR_DAT_803fe0e0;
                if (out_display_string != NULL) {
                    *out_display_string = s_1234567890ABCDEFGHIJKLMNOPQRSTUV_803fe184;
                }
            }
        }

        if ((mode_flags & 8) == 0) {
            // Check extended chars
            if ((mode_flags & 0x1000) != 0) {
                layout = (void**)&DAT_803fe280;
                if (out_display_string != NULL) {
                    *out_display_string = &DAT_803fe324;
                }
                if (((mode_flags & 2) != 0 || (mode_flags & 4) != 0) &&
                    (mode_flags & 6) != 6) {
                    layout = (void**)&DAT_803fe350;
                    if (out_display_string != NULL) {
                        *out_display_string = &DAT_803fe3f4;
                    }
                }
            }
        } else {
            // Numbers only
            layout = &PTR_DAT_803fe1b0;
            if (out_display_string != NULL) {
                *out_display_string = s_1234567890_803fe254;
            }
        }
    } else {
        // Alpha-only mode
        layout = &PTR_DAT_803fe420;
        if (out_display_string != NULL) {
            *out_display_string = s_abcdefghijklmnopqrstuvwxyz___803fe494;
        }

        if (((mode_flags & 2) != 0) || ((mode_flags & 4) != 0)) {
            if ((mode_flags & 6) != 6) {
                layout = &PTR_DAT_803fe4b4;
                if (out_display_string != NULL) {
                    *out_display_string = s_ABCDEFGHIJKLMNOPQRSTUVWXYZ___803fe528;
                }
            }
        }

        if ((mode_flags & 8) == 0) {
            if ((mode_flags & 0x1000) != 0) {
                layout = &PTR_DAT_803fe5dc;
                if (out_display_string != NULL) {
                    *out_display_string = &DAT_803fe650;
                }
                if (((mode_flags & 2) != 0 || (mode_flags & 4) != 0) &&
                    (mode_flags & 6) != 6) {
                    layout = &PTR_DAT_803fe670;
                    if (out_display_string != NULL) {
                        *out_display_string = &DAT_803fe6e4;
                    }
                }
            }
        } else {
            layout = &PTR_DAT_803fe548;
            if (out_display_string != NULL) {
                *out_display_string = s_1234567890___________________803fe5bc;
            }
        }
    }

    return layout;
}

// =============================================================================
// Address: 0x8004a198 | hud_keyboard_toggle_case
// Toggles keyboard case mode.
// XORs flag 2 at +0x224 (switches upper/lower),
// clears flag 8 (numeric lock),
// marks element hidden for rebuild.
// =============================================================================
void hud_keyboard_toggle_case(int keyboard_element)
{
    // Toggle upper/lower case flag
    *(unsigned int*)(keyboard_element + 0x224) ^= 2;
    // Clear numeric lock flag
    *(unsigned int*)(keyboard_element + 0x224) &= ~8u;
    // Mark hidden so UI rebuilds with new layout
    hud_screen_mark_hidden(keyboard_element);
}
