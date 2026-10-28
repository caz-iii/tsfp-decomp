// HUD Keyboard Input Handler
// Decompiled functions for on-screen keyboard text entry system
// Batch 091 | Date: 2026-08-10

#include "common.h"
#include "hud.h"
#include "hud_keyboard.h"
#include "sound.h"
#include "text.h"

// ============================================================================
// Address: 0x8004a1d0 | Name: keyboard_cursor_move_left
// Moves cursor one position left in the keyboard input buffer.
// In numeric wrapping mode (flag 0x20), skips non-digit characters.
// ============================================================================
void keyboard_cursor_move_left(int pKeyboard) {
    if ((*(u32*)(pKeyboard + 0x224) & 0x20) == 0) {
        // Normal mode: simple decrement with floor at 0
        if (*(int*)(pKeyboard + 0x228) > 0) {
            *(int*)(pKeyboard + 0x228) -= 1;
        }
    } else {
        // Numeric wrapping mode: skip non-digit chars (wrap around)
        char c;
        do {
            do {
                int len = text_strlen(*(char**)(pKeyboard + 0x21c));
                int total_len = text_strlen(*(char**)(pKeyboard + 0x21c));
                int pos = *(int*)(pKeyboard + 0x228) + total_len - 1;
                *(int*)(pKeyboard + 0x228) = pos - (pos / len) * len;
                c = *(char*)(*(int*)(pKeyboard + 0x21c) + *(int*)(pKeyboard + 0x228));
            } while (c < '0');
        } while (c > '9');
    }

    // Clear insert/select flags (mask off bits 1 and 4)
    *(u32*)(pKeyboard + 0x224) &= ~0x12u;

    // If not in standalone mode (flag 0x100), hide the screen element
    if ((*(u32*)(pKeyboard + 0x224) & 0x100) == 0) {
        hud_screen_mark_hidden(pKeyboard);
    }
}

// ============================================================================
// Address: 0x8004a298 | Name: keyboard_cursor_move_right
// Moves cursor one position right in the keyboard input buffer.
// In numeric wrapping mode (flag 0x20), skips non-digit characters.
// ============================================================================
void keyboard_cursor_move_right(int pKeyboard) {
    if ((*(u32*)(pKeyboard + 0x224) & 0x20) == 0) {
        // Normal mode: increment up to string length
        int len = text_strlen(*(char**)(pKeyboard + 0x21c));
        if (*(int*)(pKeyboard + 0x228) < len) {
            *(int*)(pKeyboard + 0x228) += 1;
        }
    } else {
        // Numeric wrapping mode: skip non-digit chars (wrap forward)
        char c;
        do {
            do {
                int len = text_strlen(*(char**)(pKeyboard + 0x21c));
                int pos = *(int*)(pKeyboard + 0x228) + 1;
                *(int*)(pKeyboard + 0x228) = pos - (pos / len) * len;
                c = *(char*)(*(int*)(pKeyboard + 0x21c) + *(int*)(pKeyboard + 0x228));
            } while (c < '0');
        } while (c > '9');
    }

    *(u32*)(pKeyboard + 0x224) &= ~0x12u;

    if ((*(u32*)(pKeyboard + 0x224) & 0x100) == 0) {
        hud_screen_mark_hidden(pKeyboard);
    }
}

// ============================================================================
// Address: 0x8004a350 | Name: keyboard_delete_char
// Deletes character at cursor (backspace). If clear-all flag (0x10) is set,
// zeroes entire buffer. In numeric mode (0x20), writes '0' at cursor and
// moves left instead of shifting characters.
// ============================================================================
void keyboard_delete_char(int pKeyboard) {
    if ((*(u32*)(pKeyboard + 0x224) & 0x20) == 0) {
        // Text mode
        int str_len = FUN_8038b5c8(*(int*)(pKeyboard + 0x21c));
        if (str_len != 0) {
            if ((*(u32*)(pKeyboard + 0x224) & 0x10) == 0) {
                // Normal backspace: shift chars left
                str_len = FUN_8038b5c8(*(int*)(pKeyboard + 0x21c));
                if (*(int*)(pKeyboard + 0x228) > 0) {
                    *(int*)(pKeyboard + 0x228) -= 1;
                    *(u32*)(pKeyboard + 0x224) &= ~0x02u;
                }
                u32 src = *(int*)(pKeyboard + 0x21c) + *(int*)(pKeyboard + 0x228);
                leaf_31e8(src, src + 1, str_len - *(int*)(pKeyboard + 0x228));
            } else {
                // Clear-all: reset cursor and zero the buffer
                *(int*)(pKeyboard + 0x228) = 0;
                **(u8**)(pKeyboard + 0x21c) = 0;
                *(u32*)(pKeyboard + 0x224) &= ~0x10u;
            }
        }
    } else {
        // Numeric mode: write '0' at current position, move cursor left
        *(u8*)(*(int*)(pKeyboard + 0x21c) + *(int*)(pKeyboard + 0x228)) = '0';
        keyboard_cursor_move_left(pKeyboard);
    }

    if ((*(u32*)(pKeyboard + 0x224) & 0x100) == 0) {
        hud_screen_mark_hidden(pKeyboard);
    }
}

// ============================================================================
// Address: 0x8004a448 | Name: keyboard_string_is_blank
// Returns 1 if the string contains only spaces or is empty, 0 otherwise.
// Used to validate keyboard input before confirm.
// ============================================================================
int keyboard_string_is_blank(char* str) {
    while (1) {
        if (*str == '\0') {
            return 1;
        }
        if (*str != ' ') {
            break;
        }
        str++;
    }
    return 0;
}

// ============================================================================
// Address: 0x8004a45c | Name: keyboard_confirm_input
// Confirms/submits keyboard input. For numeric mode (PTR_FUN_803fe704),
// parses string to numeric value. For text mode, validates non-blank then
// copies input buffer to output destination. Dispatches completion callback.
// ============================================================================
void keyboard_confirm_input(void) {
    int iVar1 = FUN_803826f0();
    int parent_item = *(int*)(*(int*)(iVar1 + 0x2c) + 0x194);
    bool changed;

    if (*(void**)(iVar1 + 4) == &PTR_FUN_803fe704) {
        // Numeric mode: parse the text as a number
        int old_val = **(int**)(iVar1 + 0x220);
        int new_val = hud_parse_string_to_value();
        **(int**)(iVar1 + 0x220) = new_val;
        changed = (old_val != new_val);
    } else {
        // Text mode: validate and copy
        int is_blank = keyboard_string_is_blank(*(char**)(iVar1 + 0x21c));
        if (is_blank == 1) {
            goto done;
        }

        int cmp_result = FUN_8038b378(*(u32**)(iVar1 + 0x220), *(u32**)(iVar1 + 0x21c));
        changed = (cmp_result != 0);

        if (changed) {
            // Mark output as modified (clear validation flag)
            if (*(int**)(iVar1 + 0x230) != NULL) {
                **(int**)(iVar1 + 0x230) = 0;
            }
        }

        // Copy input buffer to output buffer
        int len = FUN_8038b5c8(*(int*)(iVar1 + 0x21c));
        leaf_31e8(*(u32*)(iVar1 + 0x220), *(u32*)(iVar1 + 0x21c), len + 1);
    }

    if ((*(u32*)(iVar1 + 0x224) & 0x100) == 0) {
        // Standard mode: update parent dirty flag and pop
        if (parent_item != 0) {
            if (changed) {
                *(u32*)(parent_item + 0x2c) |= 8;
            } else {
                *(u32*)(parent_item + 0x2c) &= ~8u;
            }
        }
        hud_screen_pop(iVar1);
        hud_screen_mark_hidden(*(int*)(iVar1 + 0x2c));

        // Dispatch completion callback
        if (*(void**)(iVar1 + 0x1c8) != NULL) {
            (*(void (**)(int))(iVar1 + 0x1c8))(*(int*)(iVar1 + 0x2c));
        }
    } else {
        // Standalone mode: callback with self
        if (*(void**)(iVar1 + 0x1c8) != NULL) {
            (*(void (**)(int))(iVar1 + 0x1c8))(iVar1);
        }
    }

done:
    FUN_8038273c();
}

// ============================================================================
// Address: 0x8004a5bc | Name: keyboard_clear_all
// Clears the entire keyboard input buffer. Resets cursor to 0 and zeroes
// the string. In numeric wrapping mode, fills all positions with '0' by
// calling cursor_move_right repeatedly until wrapping.
// ============================================================================
void keyboard_clear_all(void) {
    int iVar1 = FUN_803826f0();

    if ((*(u32*)(iVar1 + 0x224) & 0x20) == 0) {
        // Text mode: simple clear
        *(int*)(iVar1 + 0x228) = 0;
        **(u8**)(iVar1 + 0x21c) = 0;
    } else {
        // Numeric mode: fill all digit positions with '0'
        int start_pos = *(int*)(iVar1 + 0x228);
        do {
            *(u8*)(*(int*)(iVar1 + 0x21c) + *(int*)(iVar1 + 0x228)) = '0';
            keyboard_cursor_move_right(iVar1);
        } while (*(int*)(iVar1 + 0x228) != start_pos);
    }

    *(u32*)(iVar1 + 0x224) &= ~0x12u;
    hud_screen_mark_hidden(iVar1);
    FUN_8038273c();
}

// ============================================================================
// Address: 0x8004a64c | Name: keyboard_play_key_sound
// Plays a random keyboard key click sound effect. Selects from SFX bank
// 0x163E-0x1640 (normal) or 0x1641-0x1643 (alternate state 0x66).
// Applies randomized pitch offset.
// ============================================================================
void keyboard_play_key_sound(void) {
    int sound_params[5];
    float pitch;
    u32 rng_val;

    if (DAT_80612614 == 0x66) {
        // Alternate sound bank (0x1641-0x1643)
        u32 rng1 = FUN_8013104c();
        double d = (double)(FLOAT_80613ce0 *
                   ((float)((double)rng1 - DOUBLE_80613ce8) / FLOAT_80613ce4) +
                   FLOAT_80613ca8);
        rng_val = FUN_8013104c();
        sound_init_play_params((int)sound_params);
        pitch = (float)d;
        sound_params[0] = (rng_val % 3) + 0x1641;
        // sound_params pitch = pitch;
        sound_play_request(sound_params);
    } else {
        // Normal sound bank (0x163E-0x1640)
        u32 rng1 = FUN_8013104c();
        double d = (double)(FLOAT_80613ce0 *
                   ((float)((double)rng1 - DOUBLE_80613ce8) / FLOAT_80613ce4) +
                   FLOAT_80613ca8);
        rng_val = FUN_8013104c();
        sound_init_play_params((int)sound_params);
        pitch = (float)d;
        sound_params[0] = (rng_val % 3) + 0x163e;
        sound_play_request(sound_params);
    }
}

// ============================================================================
// Address: 0x8004a750 | Name: keyboard_event_handler
// Main keyboard event dispatcher. Routes button/key events to actions:
//   0x000 = confirm (submit input)
//   0x3E8 = toggle uppercase/lowercase
//   0x3E9 = toggle input mode (numeric/alpha) via flag bit 2
//   0x3EA = space/shift key (toggles bit 3 + bit 12 interaction)
//   0x3EB = delete/backspace
//   0x3EC = cursor move left
//   0x3ED = cursor move right
//   0x3EE = clear all
// ============================================================================
void keyboard_event_handler(int param_1) {
    int event_id = *(int*)(param_1 + 0x188);

    keyboard_play_key_sound();

    if (event_id == 0x3EA) {
        // Space/shift key
        u32 flags = *(u32*)(param_1 + 0x224);
        if ((flags & 8) == 0) {
            if ((flags & 0x1000) == 0) {
                *(u32*)(param_1 + 0x224) = flags | 8;
            } else {
                *(u32*)(param_1 + 0x224) = flags & ~0x1000u;
            }
        } else {
            *(u32*)(param_1 + 0x224) = flags & ~8u;
            if ((*(u32*)(param_1 + 0x224) & 0x4000) == 0) {
                *(u32*)(param_1 + 0x224) |= 0x1000;
            }
        }
        hud_screen_mark_hidden(param_1);
    } else if (event_id < 0x3EA) {
        if (event_id == 1000) {
            // 0x3E8 = toggle case
            hud_keyboard_toggle_case(param_1);
        } else if (event_id < 1000) {
            if (event_id == 0) {
                // Confirm/submit
                keyboard_confirm_input();
            }
        } else {
            // 0x3E9 = toggle mode (XOR bit 2, clear bits 1+3)
            *(u32*)(param_1 + 0x224) ^= 4;
            *(u32*)(param_1 + 0x224) &= ~0x0Au;
            hud_screen_mark_hidden(param_1);
        }
    } else if (event_id == 0x3ED) {
        // Cursor right
        keyboard_cursor_move_right(param_1);
    } else if (event_id < 0x3ED) {
        if (event_id < 0x3EC) {
            // 0x3EB = delete
            keyboard_delete_char(param_1);
        } else {
            // 0x3EC = cursor left
            keyboard_cursor_move_left(param_1);
        }
    } else if (event_id < 0x3EF) {
        // 0x3EE = clear all
        keyboard_clear_all();
    }
}

// ============================================================================
// Address: 0x8004a888 | Name: keyboard_insert_char
// Inserts a character from the on-screen keyboard layout at the current cursor
// position. Handles buffer shifting, max-width overflow check (measures pixel
// width of text against field bounds), and numeric mode clamping (digit 0-5
// at last position for time entry).
// ============================================================================
void keyboard_insert_char(void) {
    int iVar4 = FUN_803826f0();
    int max_len = *(int*)(iVar4 + 0x22c);
    int layout_info[6];

    hud_keyboard_get_layout(*(u32*)(iVar4 + 0x224), layout_info);
    char insert_char = *(char*)(layout_info[0] + *(int*)(iVar4 + 0x188));

    keyboard_play_key_sound();

    if ((*(u32*)(iVar4 + 0x224) & 0x20) == 0) {
        // Text/normal mode
        if ((*(u32*)(iVar4 + 0x224) & 0x10) == 0) {
            // Normal insert (not clear-all pending)
            if (*(void**)(iVar4 + 4) == &PTR_FUN_803fe704) {
                // Numeric mode: strip leading zeros
                int cursor = *(int*)(iVar4 + 0x228);
                bool has_nonzero = false;
                for (int i = cursor; i > 0; i--) {
                    if (**(char**)(iVar4 + 0x21c) != '0') {
                        has_nonzero = true;
                    }
                }
                if (!has_nonzero) {
                    // All leading zeros — delete them first
                    while (*(int*)(iVar4 + 0x228) != 0) {
                        keyboard_delete_char(iVar4);
                    }
                }
            }
        } else {
            // Clear-all was pending: reset buffer
            *(int*)(iVar4 + 0x228) = 0;
            **(u8**)(iVar4 + 0x21c) = 0;
        }

        // Check if there's room to insert
        int str_len = FUN_8038b5c8(*(int*)(iVar4 + 0x21c));
        int chars_after_cursor = str_len - *(int*)(iVar4 + 0x228);

        if (str_len < max_len) {
            // Shift characters right to make room
            int shift_count = chars_after_cursor + 1;
            int tail_len = chars_after_cursor;
            if (tail_len >= 0) {
                for (int i = tail_len; i >= 0; i--) {
                    int idx = *(int*)(iVar4 + 0x228) + i;
                    u8* p = (u8*)(*(int*)(iVar4 + 0x21c) + idx);
                    p[1] = p[0];
                }
            }

            // Check pixel width overflow
            float max_width = FLOAT_80613c88;
            *(char*)(*(int*)(iVar4 + 0x21c) + *(int*)(iVar4 + 0x228)) = insert_char;

            if (max_width < *(float*)(iVar4 + 0x234)) {
                // Width limit active: measure text pixel width
                int text_child = hud_find_child_by_id(iVar4, -2);
                text_set_font_table(*(int*)(text_child + 0x1c));
                double text_width = text_get_char_width(*(u8**)(iVar4 + 0x21c));

                if (text_width > (double)(*(float*)(iVar4 + 0x234) * FLOAT_80611a68)) {
                    // Overflow: undo the insert (shift back left)
                    u32 src = *(int*)(iVar4 + 0x21c) + *(int*)(iVar4 + 0x228);
                    leaf_31e8(src, src + 1, chars_after_cursor + 1);
                    goto exit;
                }
            }

            // Success: advance cursor
            *(int*)(iVar4 + 0x228) += 1;
            *(u32*)(iVar4 + 0x224) &= ~0x02u;
            keyboard_play_key_sound();
        }
    } else {
        // Numeric wrapping mode: overwrite at cursor position
        int str_len = FUN_8038b5c8(*(int*)(iVar4 + 0x21c));
        char write_char = insert_char;

        // Clamp last position to '5' max (for time entry MM:SS format)
        if (*(int*)(iVar4 + 0x228) == str_len - 2) {
            if (insert_char >= '5') {
                write_char = '5';
            } else {
                write_char = insert_char;
            }
        }

        *(char*)(*(int*)(iVar4 + 0x21c) + *(int*)(iVar4 + 0x228)) = write_char;
        keyboard_cursor_move_right(iVar4);
    }

    *(u32*)(iVar4 + 0x224) &= ~0x10u;
    hud_screen_mark_hidden(iVar4);

exit:
    FUN_8038273c();
}

// ============================================================================
// Address: 0x8004aa88 | Name: keyboard_compute_available_width
// Computes the available pixel width for keyboard text input by subtracting
// padding and font metrics from the layout bounds. Returns the usable width
// as a float (via double return for PPC ABI).
// ============================================================================
double keyboard_compute_available_width(int param_1, int param_2, float* param_3) {
    float left_pad = FLOAT_80613cf0 * FLOAT_80611a68;
    float right_pad = FLOAT_80613cf0 * FLOAT_80611a68;
    float font_w = (FLOAT_80613cf0 + *(float*)(*(int*)(param_1 + 0x1c) + 0x1c)) * FLOAT_80611a68;
    float font_h = (FLOAT_80613cf0 + *(float*)(*(int*)(param_1 + 0x1c) + 0x20)) * FLOAT_80611a68;

    float total_width = param_3[2] - left_pad;
    float start_x = param_3[0] + right_pad;

    return (double)((total_width - start_x) - font_w - font_h);
}
