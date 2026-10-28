// HUD Keyboard Screen - Creation, Layout, Rendering, Input
// Decompiled functions for the on-screen keyboard screen system
// Batch 092 | Date: 2026-08-10

#include "common.h"
#include "hud.h"
#include "hud_keyboard.h"
#include "sound.h"
#include "text.h"

// ============================================================================
// Address: 0x8004aad0 | Name: keyboard_render_input_field
// Renders the keyboard text input field: per-character drawing with cursor
// blink (blinking vertical line), alignment modes (left/right/center),
// selection highlight in numeric wrapping mode, pixel-width overflow
// detection, font scaling, background fill.
// ============================================================================
void keyboard_render_input_field(int param_1, int param_2, float* param_3) {
    float local_ac, local_a8, local_a4, local_a0;
    float local_b8, local_bc;
    u32 local_b0, uStack_b4;
    int iVar4, iVar9;
    int iVar11 = 0;
    bool bVar2 = false;

    // Get keyboard element pair (parent + child)
    u64 uVar21 = FUN_803826d4();
    iVar4 = (int)(uVar21 >> 32);
    iVar9 = (int)uVar21;

    float line_height = FLOAT_80611674;

    // Compute input field bounds with padding
    local_ac = *param_3 + FLOAT_80613cf0 * FLOAT_80611a68;
    local_a8 = param_3[1];
    local_a4 = param_3[2] - FLOAT_80613cf0 * FLOAT_80611a68;
    local_a0 = param_3[3];

    float font_pad_x = (FLOAT_80613cf0 + *(float*)(*(int*)(iVar4 + 0x1c) + 0x1c)) * FLOAT_80611a68;
    float font_pad_y = (FLOAT_80613cf0 + *(float*)(*(int*)(iVar4 + 0x1c) + 0x20)) * FLOAT_80611a68;

    text_set_font_table(*(int*)(iVar9 + 0x1c));
    double available_width = keyboard_compute_available_width(iVar4, iVar9, param_3);

    // Apply alpha fade if flag 0x400 set (e.g. during transitions)
    if ((*(u32*)(iVar4 + 0x224) & 0x400) != 0) {
        text_apply_alpha_fade(
            (double)(float)(FLOAT_80613cf4 * (float)available_width),
            *(u8**)(iVar4 + 0x21c), 0, 1);
    }

    // Y position from child layout offset
    local_bc = *(float*)(iVar9 + 0x40) * FLOAT_80611a64 + param_3[1];

    // Auto-center if explicit width < available and alignment is center (mode 2)
    float explicit_width = *(float*)(iVar4 + 0x234);
    if (FLOAT_80613c88 < explicit_width && explicit_width < (float)available_width &&
        *(int*)(iVar9 + 0x30) == 2) {
        float half_w = FLOAT_80613c9c * (explicit_width * FLOAT_80611a68);
        local_a4 = FLOAT_80613c9c * (local_a4 - local_ac) + local_ac;
        local_ac = local_a4 - (half_w + font_pad_x);
        local_a4 = local_a4 + (half_w + font_pad_y);
    }

    // Compute colors and draw background
    hud_item_compute_colors(iVar4, iVar9, &local_b0, &uStack_b4);
    u32 text_color = local_b0;
    hud_draw_element_background(iVar4, &local_ac);

    // Numeric wrapping mode: enable monospace rendering
    if ((*(u32*)(iVar4 + 0x224) & 0x20) != 0) {
        leaf_92c0(1);
    }

    // Measure text with scaling
    text_set_justify_mode(1);
    u8* scaled_text = text_check_scale(available_width, *(u8**)(iVar4 + 0x21c));
    text_set_justify_mode(0);
    int text_len = text_strlen((char*)scaled_text);

    // Compute X start position based on alignment mode
    float start_x = local_ac + font_pad_x;
    if (*(int*)(iVar9 + 0x30) == 1) {
        start_x = local_a4 - font_pad_y;  // Right-aligned
    }
    local_b8 = start_x;

    if (*(int*)(iVar9 + 0x30) == 1) {
        double tw = text_get_char_width_scaled(scaled_text);
        local_b8 = start_x - (float)tw;
    } else if (*(int*)(iVar9 + 0x30) == 2) {
        double tw = text_get_char_width_scaled(scaled_text);
        local_b8 = FLOAT_80613c9c * ((local_a4 - local_ac) - (float)tw) + local_ac;
    }

    // Y cursor positions
    local_bc += FLOAT_80613cf8;
    float cursor_top = local_bc - FLOAT_80613c8c;
    float cursor_bot = FLOAT_80613c8c + (local_bc + line_height);

    // Select-all highlight (flag 0x10)
    if ((*(u32*)(iVar4 + 0x224) & 0x10) != 0) {
        float pulse = *(float*)(iVar4 + 0xf0);
        FUN_803825c0((double)(FLOAT_80613cc0 * pulse));
        FUN_803825c0((double)(FLOAT_80613cfc * pulse));
        float tw, th;
        text_measure_string(&tw, &th, scaled_text, 0);
        FUN_8016f7d8((double)local_b8, (double)local_bc,
                     (double)(local_b8 + tw),
                     (double)(FLOAT_80613c98 + local_bc + th));
    }

    // Per-character rendering loop with cursor
    u8* char_ptr = scaled_text;
    float cur_line_height = line_height;

    for (int i = 0; i < text_len + 1; i++) {
        // Draw cursor at current position
        if (iVar11 == *(int*)(iVar4 + 0x228) && !bVar2) {
            if ((*(u32*)(iVar4 + 0x224) & 0x20) == 0 || text_len < i) {
                // Text mode cursor: blinking vertical line
                int closing = hud_screen_all_children_closing(iVar4);
                if (closing != 0) {
                    float alpha = FLOAT_80613c8c;
                    if (0x28 < DAT_80612020 % 0x3c) {
                        alpha = FLOAT_80613c88;  // Blink off
                    }
                    u32 cursor_color = FUN_80132348((double)alpha, text_color);
                    float cursor_w = FLOAT_80613c8c;
                    if (i == text_len + 1) {
                        cursor_w = FLOAT_80613cd8;
                    }
                    FUN_8016fde8((double)(cursor_w + local_b8), (double)cursor_top,
                                 (double)(cursor_w + local_b8), (double)cursor_bot, cursor_color);
                }
            } else {
                // Numeric mode cursor: highlight box around character
                u8 ch_buf[2];
                ch_buf[0] = *char_ptr;
                ch_buf[1] = 0;
                float cw, ch;
                text_measure_string(&cw, &ch, ch_buf, 0);
                FUN_8016f6d0((double)local_b8, (double)(local_bc - FLOAT_80613c98),
                             (double)(local_b8 + cw),
                             (double)(local_bc + FLOAT_80611674));
            }
            bVar2 = true;
        }

        // Render the character
        if (i <= text_len) {
            u8 ch_buf[2];
            ch_buf[0] = *char_ptr;
            ch_buf[1] = 0;
            text_render_string_internal(&local_b8, &local_bc, ch_buf, local_b0);

            if (*char_ptr == '\n') {
                // Newline: reset X, advance Y
                local_b8 = start_x;
                if (*(int*)(iVar9 + 0x30) == 1) {
                    double tw = text_get_char_width_scaled(scaled_text);
                    local_b8 = start_x - (float)tw;
                } else if (*(int*)(iVar9 + 0x30) == 2) {
                    double tw = text_get_char_width_scaled(scaled_text);
                    local_b8 = FLOAT_80613c9c * ((local_a4 - local_ac) - (float)tw) + local_ac;
                }
                cur_line_height += line_height;
                cursor_top += cur_line_height - line_height;
                cursor_bot += cur_line_height - line_height;
            } else {
                iVar11++;
            }
        }
        char_ptr++;
    }

    // Reset text state
    text_set_color_rgb((double)FLOAT_80613c8c, (double)FLOAT_80613c8c, (double)FLOAT_80613c8c);
    leaf_92c0(0);
    FUN_80382720();
}

// ============================================================================
// Address: 0x8004afd8 | Name: keyboard_input_update
// Per-frame keyboard input update: polls D-pad left/right for cursor,
// X button for delete, L/R triggers for case toggle. Validates confirm
// button state (locks OK if string is blank or too short for name entry).
// ============================================================================
void keyboard_input_update(int param_1) {
    int iVar1 = hud_element_is_closing(param_1);
    if (iVar1 == 0) {
        // Only process input if player slot is active and not locked
        if (*(int*)(&DAT_8051a824 + *(int*)(param_1 + 0x38) * 0x1e0) != 0 &&
            (*(u32*)(param_1 + 0x40) & 0x40000) == 0) {

            // D-pad left: move cursor left
            u32 buttons = input_get_buttons_held(*(int*)(param_1 + 0x38));
            if ((buttons & 1) != 0) {
                keyboard_cursor_move_left(param_1);
            }

            // D-pad right: move cursor right
            buttons = input_get_buttons_held(*(int*)(param_1 + 0x38));
            if ((buttons & 2) != 0) {
                keyboard_cursor_move_right(param_1);
            }
        }

        // X button: delete character
        if ((*(u32*)(param_1 + 0xc0) & 0x800000) != 0) {
            keyboard_delete_char(param_1);
        }

        // L or R trigger: toggle case
        u32 held = input_get_buttons_held(*(int*)(param_1 + 0x38));
        if ((held & 0x200) != 0) {
            hud_keyboard_toggle_case(param_1);
        } else {
            held = input_get_buttons_held(*(int*)(param_1 + 0x38));
            if ((held & 0x400) != 0) {
                hud_keyboard_toggle_case(param_1);
            }
        }

        // Validate confirm button state (name entry mode)
        if (*(void**)(param_1 + 4) == &PTR_FUN_803fdf98) {
            if (*(int*)(param_1 + 0x21c) == 0 ||
                (*(u32*)(param_1 + 0x224) & 0x800) == 0 ||
                FUN_8038b5c8(*(int*)(param_1 + 0x21c)) > 2) {
                // Check if string is non-blank
                if (*(char**)(param_1 + 0x21c) == NULL ||
                    keyboard_string_is_blank(*(char**)(param_1 + 0x21c)) == 0) {
                    // Enable OK button
                    hud_screen_set_handler_param_b(param_1, 3, 0);
                    int ok_item = hud_find_child_by_id(param_1, 0);
                    *(u32*)(ok_item + 0x28) &= ~1u;
                } else {
                    // Disable OK button (blank input)
                    hud_screen_set_handler_param_b(param_1, 3, 1);
                    int ok_item = hud_find_child_by_id(param_1, 0);
                    *(u32*)(ok_item + 0x28) |= 1;
                }
            } else {
                // Too short: disable OK
                hud_screen_set_handler_param_b(param_1, 3, 1);
                int ok_item = hud_find_child_by_id(param_1, 0);
                *(u32*)(ok_item + 0x28) |= 1;
            }
        }
    }

    // Clear pending event
    if (*(int*)(param_1 + 0x188) == -1) {
        *(int*)(param_1 + 0x188) = 0;
    }
}

// ============================================================================
// Address: 0x8004b184 | Name: keyboard_cancel_handler
// Keyboard cancel/dismiss handler. Pops the keyboard screen and dispatches
// the cancel callback at +0x1cc with the parent element.
// ============================================================================
void keyboard_cancel_handler(int param_1) {
    hud_screen_pop(param_1);
    if (*(void**)(param_1 + 0x1cc) != NULL) {
        (*(void (**)(int))(param_1 + 0x1cc))(*(int*)(param_1 + 0x2c));
    }
}

// ============================================================================
// Address: 0x8004b1c8 | Name: keyboard_render_background
// Renders the keyboard panel background sprite with position-based parallax
// alpha fade (left/right edges fade) and focus glow pulse. Draws a 512x512
// texture with blend modes based on DAT_80612614 state (alternate theme).
// ============================================================================
void keyboard_render_background(int param_1, int param_2, float* param_3) {
    u64 uVar8 = FUN_803826e4();
    int iVar3 = (int)(uVar8 >> 32);

    // Compute normalized position for alpha fade (parallax)
    float norm_left = (*param_3 - *(float*)(iVar3 + 0xa4)) / *(float*)(iVar3 + 0xac);
    float norm_right = (param_3[2] - *(float*)(iVar3 + 0xa4)) / *(float*)(iVar3 + 0xac);

    // Left edge alpha: ramp 0..0.5 then fall off
    double alpha_left;
    if (norm_left <= FLOAT_80613c9c) {
        alpha_left = (double)(FLOAT_80613c98 * norm_left);
    } else {
        alpha_left = -(double)(FLOAT_80613c98 * (norm_left - FLOAT_80613c9c) - FLOAT_80613c8c);
    }

    // Right edge alpha
    double alpha_right;
    if (norm_right <= FLOAT_80613c9c) {
        alpha_right = (double)(FLOAT_80613c98 * norm_right);
    } else {
        alpha_right = -(double)(FLOAT_80613c98 * (norm_right - FLOAT_80613c9c) - FLOAT_80613c8c);
    }

    // Clamp and apply sqrt for smoother falloff
    double final_left = (double)FLOAT_80613c88;
    if (final_left < alpha_left) {
        final_left = FUN_80131538(alpha_left);  // sqrt
    }
    double final_right = (double)FLOAT_80613c88;
    if (final_right < alpha_right) {
        final_right = FUN_80131538(alpha_right);  // sqrt
    }

    // Draw background sprite (512x512 tiled)
    FUN_8016f00c(1);
    int sprite = FUN_8004d850(0);
    FUN_8016e77c(sprite);
    FUN_803825c0((double)(FLOAT_80613d00 * (float)final_right));
    FUN_803825c0((double)(FLOAT_80613d00 * (float)final_left));
    FUN_803825c0((double)(FLOAT_80613cc0 * (float)final_right));
    FUN_803825c0((double)(FLOAT_80613cc0 * (float)final_left));
    FUN_80170b10((double)*param_3, (double)param_3[1],
                 (double)param_3[2], (double)param_3[3], 0, 0, 0x200, 0x200);

    // Focus highlight glow
    int is_focused = hud_item_is_focused((int)uVar8);
    if (is_focused != 0) {
        if (DAT_80612614 == 0x66) {
            FUN_803825c0((double)(FLOAT_80613cc0 * *(float*)(iVar3 + 0xf0)));
        } else {
            float pulse = *(float*)(iVar3 + 0xf0);
            FUN_803825c0((double)(FLOAT_80613d04 * pulse));
            FUN_803825c0((double)(FLOAT_80613cbc * pulse));
        }
        FUN_8016f00c(2);
        FUN_80170b10((double)*param_3, (double)param_3[1],
                     (double)param_3[2], (double)param_3[3], 0, 0, 0x200, 0x200);
    }
    FUN_80382730();
}

// ============================================================================
// Address: 0x8004b414 | Name: keyboard_build_layout_standard
// Builds the standard keyboard layout: 5 rows with 10 key buttons each.
// Row 0-3: character keys. Row 4: spacer + spacer + Space(0x19D1) + Back(0x19CC).
// Special buttons on rows: OK(0x19CD), Cancel(0x19CE), Caps(0x19CF), Del(0x19D0).
// ============================================================================
void keyboard_build_layout_standard(void) {
    int iVar1 = FUN_803826e0();
    hud_keyboard_get_layout(*(u32*)(iVar1 + 0x224), NULL);
    hud_add_list_item();
    hud_add_padding_item((double)FLOAT_80613c98);
    hud_set_context_property(0x1130000);

    int last_item = hud_get_last_created_item();
    *(int*)(last_item + 0x30) = 2;  // Center alignment

    for (int row = 0; row < 5; row++) {
        hud_begin_sub_list(0, 1, 0);

        if (row == 0) {
            get_string_by_id(0x19cd);  // "OK"
            hud_add_item_internal((double)FLOAT_80613d08, (double)FLOAT_80613ca4);
        } else if (row == 1) {
            get_string_by_id(0x19ce);  // "Cancel"
            hud_add_item_internal((double)FLOAT_80613d08, (double)FLOAT_80613ca4);
        } else if (row == 2) {
            get_string_by_id(0x19cf);  // "Caps"
            hud_add_item_internal((double)FLOAT_80613d08, (double)FLOAT_80613ca4);
        } else if (row == 3) {
            get_string_by_id(0x19d0);  // "Del"
            hud_add_item_internal((double)FLOAT_80613d08, (double)FLOAT_80613ca4);
        } else {  // row == 4
            // Spacers + Space + Back
            hud_add_item_internal((double)FLOAT_80613cb4, (double)FLOAT_80613ca4);
            hud_add_item_internal((double)FLOAT_80613cb4, (double)FLOAT_80613ca4);
            get_string_by_id(0x19d1);  // "Space"
            hud_add_item_internal((double)FLOAT_80613d0c, (double)FLOAT_80613ca4);
            get_string_by_id(0x19cc);  // "Back"
            hud_add_item_internal((double)FLOAT_80613d08, (double)FLOAT_80613ca4);
        }

        // Add 10 character key slots for rows 0-3
        if (row < 4) {
            for (int key = 0; key < 10; key++) {
                hud_add_item_internal((double)FLOAT_80613ca4, (double)FLOAT_80613ca4);
            }
        }

        hud_end_sub_list();
    }
    FUN_8038272c();
}

// ============================================================================
// Address: 0x8004b770 | Name: keyboard_build_layout_locked
// Builds the keyboard layout with locked-confirm: same 5-row grid as standard
// but locks the OK button (row 0) if the input string is blank. Uses different
// item sizing constants.
// ============================================================================
void keyboard_build_layout_locked(void) {
    int iVar1 = FUN_803826e0();
    hud_keyboard_get_layout(*(u32*)(iVar1 + 0x224), NULL);
    hud_add_list_item();
    hud_set_context_property(0x1130040);

    int last_item = hud_get_last_created_item();
    *(int*)(last_item + 0x30) = 2;  // Center alignment

    for (int row = 0; row < 5; row++) {
        hud_begin_sub_list(0, 1, 0);

        if (row == 0) {
            get_string_by_id(0x19cd);  // "OK"
            hud_add_item_internal((double)FLOAT_80613d14, (double)FLOAT_80613d18);
            // Lock OK if input is blank
            int locked = 0;
            if (*(char**)(iVar1 + 0x21c) != NULL &&
                keyboard_string_is_blank(*(char**)(iVar1 + 0x21c)) != 0) {
                locked = 1;
            }
            int ok_item = hud_get_last_created_item();
            hud_item_set_locked(ok_item, locked);
        } else if (row == 1) {
            get_string_by_id(0x19ce);  // "Cancel"
            hud_add_item_internal((double)FLOAT_80613d14, (double)FLOAT_80613d18);
        } else if (row == 2) {
            get_string_by_id(0x19cf);  // "Caps"
            hud_add_item_internal((double)FLOAT_80613d14, (double)FLOAT_80613d18);
        } else if (row == 3) {
            get_string_by_id(0x19d0);  // "Del"
            hud_add_item_internal((double)FLOAT_80613d14, (double)FLOAT_80613d18);
        } else {  // row == 4
            hud_add_item_internal((double)FLOAT_80613d1c, (double)FLOAT_80613d18);
            hud_add_item_internal((double)FLOAT_80613d1c, (double)FLOAT_80613d18);
            get_string_by_id(0x19d1);  // "Space"
            hud_add_item_internal((double)FLOAT_80613d0c, (double)FLOAT_80613d18);
            get_string_by_id(0x19cc);  // "Back"
            hud_add_item_internal((double)FLOAT_80613d14, (double)FLOAT_80613d18);
        }

        if (row < 4) {
            for (int key = 0; key < 10; key++) {
                hud_add_item_internal((double)FLOAT_80613d20, (double)FLOAT_80613d18);
            }
        }

        hud_end_sub_list();
    }
    FUN_8038272c();
}

// ============================================================================
// Address: 0x8004bae0 | Name: keyboard_build_layout_compact
// Builds the compact keyboard layout for split-screen: 7 keys per row (vs 10),
// smaller font table (0x15), conditional padding. Locks OK if blank.
// Row 4 has Back + Space only (no spacers).
// ============================================================================
void keyboard_build_layout_compact(void) {
    u64 uVar9 = FUN_803826e0();
    int iVar3 = (int)(uVar9 >> 32);
    hud_keyboard_get_layout(*(u32*)(iVar3 + 0x224), NULL);

    // Conditional padding based on context
    float pad_top = FLOAT_80613d24;
    if ((int)uVar9 != 0) {
        pad_top = FLOAT_80613c88;
    }
    float pad_inner = FLOAT_80613cac;
    if ((int)uVar9 != 0) {
        pad_inner = FLOAT_80613c8c;
    }

    hud_add_padding_item_alt((double)pad_top);
    hud_add_list_item();
    hud_set_context_property(0x1130040);

    int last_item = hud_get_last_created_item();
    *(int*)(last_item + 0x30) = 2;  // Center

    // Set compact font
    last_item = hud_get_last_created_item();
    *(int*)(last_item + 0x1c) = 0x15;

    hud_add_padding_item_alt((double)pad_inner);

    for (int row = 0; row < 5; row++) {
        hud_begin_sub_list(0, 1, 0);

        if (row == 0) {
            get_string_by_id(0x19cd);  // "OK"
            hud_add_item_internal((double)FLOAT_80613d28, (double)FLOAT_80613d18);
            // Lock if blank
            char* input_str = *(char**)(iVar3 + 0x21c);
            int locked = 0;
            if (input_str != NULL && keyboard_string_is_blank(input_str) != 0) {
                locked = 1;
            }
            int ok_item = hud_get_last_created_item();
            hud_item_set_locked(ok_item, locked);
        } else if (row == 1) {
            get_string_by_id(0x19ce);  // "Cancel"
            hud_add_item_internal((double)FLOAT_80613d28, (double)FLOAT_80613d18);
        } else if (row == 2) {
            get_string_by_id(0x19cf);  // "Caps"
            hud_add_item_internal((double)FLOAT_80613d28, (double)FLOAT_80613d18);
        } else if (row == 3) {
            get_string_by_id(0x19d0);  // "Del"
            hud_add_item_internal((double)FLOAT_80613d28, (double)FLOAT_80613d18);
        } else {  // row == 4
            get_string_by_id(0x19cc);  // "Back"
            hud_add_item_internal((double)FLOAT_80613d28, (double)FLOAT_80613d18);
            get_string_by_id(0x19d1);  // "Space"
            hud_add_item_internal((double)FLOAT_80613d2c, (double)FLOAT_80613d18);
        }

        // 7 character keys per row (compact)
        if (row < 4) {
            for (int key = 0; key < 7; key++) {
                hud_add_item_internal((double)FLOAT_80613d30, (double)FLOAT_80613d18);
            }
        }

        hud_end_sub_list();
    }
    FUN_8038272c();
}

// ============================================================================
// Address: 0x8004be78 | Name: keyboard_compute_field_width
// Computes available input field width from global screen dimensions.
// Subtracts padding and font metrics from total viewport width.
// ============================================================================
double keyboard_compute_field_width(int param_1) {
    float screen_right = *(float*)(DAT_806119e4 + 0x34);
    float screen_left = *(float*)(DAT_806119e4 + 0x30);
    float total_width = screen_right - screen_left;

    float pad = FLOAT_80613cf0;
    float font_w = FLOAT_80613cf0 + *(float*)(*(int*)(param_1 + 0x1c) + 0x1c);
    float font_h = FLOAT_80613cf0 + *(float*)(*(int*)(param_1 + 0x1c) + 0x20);

    return (double)((total_width - pad - pad) - font_w - font_h);
}

// ============================================================================
// Address: 0x8004beb4 | Name: keyboard_screen_create
// Creates the full keyboard screen: initializes layout from descriptor,
// registers OK/Cancel/Clear handlers (strings 0x19C7/0x19C9/0x19CB),
// measures text for auto-height, builds key grid via build_layout_standard,
// finalizes frame and applies focus.
// ============================================================================
void keyboard_screen_create(void) {
    int* piVar1 = (int*)FUN_803826f0();
    int saved_state = FUN_8004d1c8(6);

    // Initialize screen element with cancel handler as dismiss callback
    hud_element_init_from_layout(0, 0, 0, keyboard_cancel_handler);
    hud_clear_both_callbacks();

    // Register 3 handler slots: OK, Cancel, Clear
    int str_ok = get_string_by_id(0x19c7);
    hud_screen_register_handler_global(0, str_ok, 0, 0);
    int str_cancel = get_string_by_id(0x19c9);
    hud_screen_register_handler_global(1, str_cancel, 0, 0);
    int str_clear = get_string_by_id(0x19cb);
    hud_screen_register_handler_global(3, str_clear, 0, 0);

    // Measure text to determine needed height
    int font_id = hud_element_get_dimension((int)piVar1, 0);
    text_set_font_table(font_id);
    double char_width = text_get_char_width(&DAT_8061028c);

    // Calculate required rows from max chars and field width
    float line_h = FLOAT_80611674 * FLOAT_80611a5c;
    double max_chars_width = FLOAT_80611a60 * (char_width * (double)(float)(DAT_80611a00 - DOUBLE_80613c90));

    // If flag 0x2000: use fullscreen field width calculation
    if ((piVar1[0x89] & 0x2000u) != 0) {
        double fw = keyboard_compute_field_width((int)piVar1);
        piVar1[0x8d] = (int)(float)fw;
    }

    // Compute number of text lines needed
    u32 num_rows;
    if (max_chars_width == 0.0 && (double)fRam80611a04 == 0.0) {
        num_rows = 1;
    } else {
        double field_w = keyboard_compute_field_width((int)piVar1);
        double min_dim = (double)fRam80611a04;
        if (max_chars_width != 0.0) {
            min_dim = max_chars_width;
            if (fRam80611a04 != 0.0 && max_chars_width >= (double)fRam80611a04) {
                min_dim = (double)fRam80611a04;
            }
        }
        num_rows = (u32)(min_dim / field_w);
        if (FLOAT_80613c88 < (float)(min_dim / field_w) - (float)num_rows) {
            num_rows++;
        }
        // If input text exists, ensure enough rows for word-wrap
        if ((char*)piVar1[0x87] != NULL) {
            double lines = text_count_lines((double)(float)(field_w * FLOAT_80611a68),
                                            (char*)piVar1[0x87]);
            if ((int)num_rows < (int)lines) {
                num_rows = (int)lines;
            }
        }
    }

    // Enable scroll flag if layout would be too tall (alternate theme only)
    if (FLOAT_80613d2c < (float)((float)num_rows * line_h + FLOAT_80613cd8) &&
        DAT_80612614 == 0x66) {
        piVar1[0x10] |= 0x4000000;
    }

    // Build the key grid and finalize
    keyboard_build_layout_standard();
    hud_element_finalize_frame();
    hud_element_apply_focus(piVar1);
    FUN_8004d1c8(saved_state);
    FUN_8038273c();
}

// ============================================================================
// Address: 0x8004c138 | Name: keyboard_screen_create_numeric
// Creates the numeric-only keyboard screen: simplified 3-row layout
// (row 0-1: 5 digit keys + spacers, row 2: OK + Enter/0x19D2).
// Used for time entry and number input. No case toggle.
// ============================================================================
void keyboard_screen_create_numeric(void) {
    int* piVar1 = (int*)FUN_803826d8();
    int saved_state = FUN_8004d1c8(6);

    hud_keyboard_get_layout(piVar1[0x89], NULL);
    hud_element_init_from_layout(0, 0, 0, hud_screen_pop);
    hud_clear_both_callbacks();

    // Register handlers
    int str_ok = get_string_by_id(0x19c7);
    hud_screen_register_handler_global(0, str_ok, 0, 0);
    int str_cancel = get_string_by_id(0x19c9);
    hud_screen_register_handler_global(1, str_cancel, 0, 0);
    int str_clear = get_string_by_id(0x19cb);
    hud_screen_register_handler_global(3, str_clear, 0, 0);

    int font_id = hud_element_get_dimension((int)piVar1, 0);
    text_set_font_table(font_id);

    hud_add_list_item();
    hud_add_padding_item((double)FLOAT_80613c98);
    hud_set_context_property(0x1030000);

    for (int row = 0; row < 3; row++) {
        hud_begin_sub_list(0, 1, 0);

        if (row == 2) {
            // Bottom row: OK + Enter
            get_string_by_id(0x19cd);  // "OK"
            hud_add_item_internal((double)FLOAT_80613d2c, (double)FLOAT_80613ca4);
            get_string_by_id(0x19d2);  // "Enter"
            hud_add_item_internal((double)FLOAT_80613d14, (double)FLOAT_80613ca4);
        } else if (row == 0 || row == 1) {
            // Digit rows: 5 keys + special buttons
            for (int key = 0; key < 5; key++) {
                hud_add_item_internal((double)FLOAT_80613ca4, (double)FLOAT_80613ca4);
            }
            if (row == 0) {
                // Spacer + spacer
                hud_add_item_internal((double)FLOAT_80613d1c, (double)FLOAT_80613ca4);
                hud_add_item_internal((double)FLOAT_80613d1c, (double)FLOAT_80613ca4);
            } else {
                // "Back" button
                get_string_by_id(0x19cc);
                hud_add_item_internal((double)FLOAT_80613d14, (double)FLOAT_80613ca4);
            }
        }

        hud_end_sub_list();
    }

    hud_element_finalize_frame();
    hud_element_apply_focus(piVar1);
    FUN_8004d1c8(saved_state);
    FUN_80382724();
}
