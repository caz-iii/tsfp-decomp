// ============================================================================
// HUD Text System
// Handles HUD text element creation, positioning, font selection, visibility
// management, and multi-line localized text rendering.
// Batch 325 | Date: 2026-08-25
// ============================================================================

#include "common.h"
#include "graphics.h"
#include "text.h"
#include "localization.h"

// ---- Globals ----
static int g_hud_visibility_flags[14];           // DAT_8057ff5c - per-element visibility
// Indices into visibility array:
//  [1] = show health       (DAT_8057ff60)
//  [2] = ammo mode         (DAT_8057ff64) - 3 for single, 1 for split
//  [3] = show minimap      (DAT_8057ff68)
//  [4] = show compass      (DAT_8057ff6c)
//  [5] = show crosshair    (DAT_8057ff70)
//  [6] = paired-single     (DAT_8057ff74) - computed from screen mode
//  [7] = show weapon name  (DAT_8057ff78)
//  [8] = show objectives   (DAT_8057ff7c)
//  [9] = show score        (DAT_8057ff80)
//  [10] = show timer       (DAT_8057ff84)
//  [11] = show messages    (DAT_8057ff88)
//  [12] = show pickup      (DAT_8057ff8c)
//  [13] = show border      (DAT_8057ff90)

// HUD text element pool: per-player (0x177c stride), 16 slots (0x16C stride)
// Element layout: type at +0x00, owner_id at +0x168, ...
// DAT_805786b8 = base of HUD text element pool

static int g_language_mode;                      // DAT_8060f948 (0=EN, 1=JP)

// String format table (indexed by string_id * 4)
// DAT_803e1fb0 = localized separator string ID lookup

// ============================================================================
// Address: 0x8010535c | hud_select_base_y
// Selects the HUD base Y position depending on screen mode and game state.
// Single-screen (mode < 2): bottom row unless challenge mode with timer.
// Split-screen (mode >= 2): centered.
// ============================================================================
void hud_select_base_y(void) {
    int screen_mode = get_screen_mode();

    if (screen_mode < 2) {
        // Single-screen: check if we need center positioning
        if (g_game_sub_mode != 0x0A || get_screen_mode() != 1 ||
            game_get_round_timer() == 0) {
            hud_get_bottom_row_y(1);
        } else {
            hud_get_center_y();
        }
    } else {
        hud_get_center_y();
    }
}

// ============================================================================
// Address: 0x801053c0 | hud_get_offset_row_y
// Computes Y position for a given row index with viewport offset.
// Mode < 2: subtracts line spacing (top-down offset).
// Mode >= 2: adds scaled offset (bottom-up for split-screen).
// ============================================================================
int hud_get_offset_row_y(u32 row_index) {
    int screen_mode = get_screen_mode();
    int prev_font = text_set_font_table(g_font_table_a[screen_mode]);

    float shift = get_hud_vertical_shift();  // FUN_8010f4a0

    // Base from viewport top
    int vp_height = g_viewport_data[6] - g_viewport_offset_y;
    float base = (float)(s32)vp_height - shift;
    float row_offset = g_font_height * (float)(s32)row_index;
    u32 raw_y = (u32)(-(row_offset - base));

    int mode = get_screen_mode();
    float result;
    if (mode < 2) {
        // Single-screen: offset down from top
        result = -(LINE_SPACING_A * g_render_scale_y -
                   (float)(s32)raw_y);
    } else {
        // Split-screen: offset up from computed position
        result = LINE_SPACING_B * g_render_scale_y +
                 (float)(s32)raw_y;
    }

    text_set_font_table(prev_font);
    return (int)result;
}

// ============================================================================
// Address: 0x801054f8 | hud_get_weapon_label_id
// Returns the localized string ID for the weapon HUD label.
// Level 100 (challenge): returns 0x15e6 (no team) or 0x15e7 (has team).
// Story mode (0x03): maps weapon type to specific string IDs for special weapons.
// Default: 0x15e6.
// ============================================================================
int hud_get_weapon_label_id(int* entity) {
    if (g_level_id == 100) {
        if (entity[6] != 0) {  // offset 0x18 = team
            return 0x15e7;
        }
        return 0x15e6;
    }

    if (g_game_sub_mode != 0x03) {
        return 0x15e6;
    }

    // Story mode: map weapon type to specific label
    u16 weapon_type = *(u16*)(entity[2] + 0x94);  // offset 0x08 -> +0x94

    switch (weapon_type) {
        case 0x90:  return 0x11b1;  // Shotgun
        case 0x8F:  return 0x11b0;  // Pistol (0x8E < uVar1)
        case 0xBF:  return 0x11ae;  // Sniper (0xBE < uVar1)
        case 0xC0:  return 0x15e6;  // Default
        case 0xC1:  return 0x11ad;  // Rocket (< 0xC2)
        case 0xC2:  return 0x11ac;  // Minigun
        case 0x4D5: return 0x11af;  // Special
        default:    return 0x15e6;
    }
}

// ============================================================================
// Address: 0x801055c4 | hud_check_use_progress
// Checks if a use/switch prompt should show a progress indicator.
// Level 100: checks entity flags (0x20000 for team, 0x10000 for no-team)
//            and calls switch_get_use_duration.
// Otherwise: reads timer float at entity+0x28 and returns (timer > 0.0).
// ============================================================================
void hud_check_use_progress(int* entity, u32* out_has_progress) {
    double duration;

    if (g_level_id == 100) {
        int has_team = entity[6];        // offset 0x18
        u32 flags = entity[4];           // offset 0x10

        if ((has_team != 0 && (flags & 0x20000) != 0) ||
            (has_team == 0 && (flags & 0x10000) != 0)) {
            duration = switch_get_use_duration(entity);
        } else {
            goto use_timer;
        }
    } else {
use_timer:
        duration = (double)*(float*)((int)entity + 0x28);
    }

    *out_has_progress = (u32)(0.0 < duration);
}

// ============================================================================
// Address: 0x80105644 | hud_select_font_for_width
// Selects the best font table that fits within the current viewport width.
// Iterates from (screen_mode - 1) upward to index 4, measuring character
// width against available viewport space minus horizontal padding.
// Called from 7 HUD draw functions.
// ============================================================================
void hud_select_font_for_width(void) {
    int* font_tables = gx_begin_font_pass();  // FUN_803826e0

    int start_idx = get_screen_mode() - 1;
    int idx = start_idx;
    int offset = start_idx * 4;

    do {
        int* table_ptr = (int*)((int)font_tables + offset);
        idx++;
        offset += 4;

        text_set_font_table(*table_ptr);

        if (idx > 4) break;

        // Determine max line count for viewport
        u32 max_chars = 0x30;  // 48 chars default
        if (get_screen_mode() == 1) {
            max_chars = 0x38;  // 56 chars for mode 1
        }

        // Measure text width
        double char_width = text_get_char_width((u8*)font_tables);

        // Check if text fits in viewport (width - 2 * horizontal padding)
        int vp_width = (g_viewport_data[1] - g_viewport_offset_right) -
                       (g_viewport_offset_left + g_viewport_data[0]);
        float available = (float)(vp_width +
                          (int)(g_render_scale_x * (float)(s32)max_chars) * -2);

    } while (char_width > (double)available);

    gx_end_font_pass();  // FUN_8038272c
}

// ============================================================================
// Address: 0x80105750 | hud_visibility_init
// Initializes 14 HUD element visibility flags to defaults.
// Most elements enabled (1). Ammo mode = 3 for single-screen, 1 for split.
// Paired-single flag computed from screen mode (1 if mode < 3).
// Called from screen_effect_system_init.
// ============================================================================
void hud_visibility_init(void) {
    // Clear all 14 flags
    for (int i = 0; i < 14; i++) {
        g_hud_visibility_flags[i] = 0;
    }

    g_hud_visibility_flags[1]  = 1;  // health
    
    int screen_mode = get_screen_mode();
    g_hud_visibility_flags[2]  = (screen_mode > 2) ? 1 : 3;  // ammo mode
    
    g_hud_visibility_flags[3]  = 1;  // minimap
    g_hud_visibility_flags[4]  = 1;  // compass
    g_hud_visibility_flags[5]  = 1;  // crosshair
    g_hud_visibility_flags[11] = 1;  // messages
    g_hud_visibility_flags[13] = 1;  // border

    // Paired-single: 1 if mode < 3, 0 if mode >= 3
    int mode2 = get_screen_mode();
    g_hud_visibility_flags[6] = (u32)(mode2 < 3) - ((s32)mode2 >> 31);

    g_hud_visibility_flags[7]  = 1;  // weapon name
    g_hud_visibility_flags[8]  = 1;  // objectives
    g_hud_visibility_flags[9]  = 1;  // score
    g_hud_visibility_flags[10] = 1;  // timer
    g_hud_visibility_flags[12] = 1;  // pickup
}

// ============================================================================
// Address: 0x80105814 | hud_create_text_colored
// Creates a HUD text element with a custom color/priority value.
// Wraps hud_text_submit (FUN_80105d5c) and stores param_4 at slot+0x15C.
// ============================================================================
void hud_create_text_colored(int x, int y, u32* text_buf, int color) {
    int slot = hud_text_submit(x, y, text_buf);  // FUN_80105d5c
    if (slot != 0) {
        *(int*)(slot + 0x15C) = color;
    }
}

// ============================================================================
// Address: 0x8010584c | hud_create_localized_text
// Creates a single-line HUD text from a localized string ID.
// Formats as "%s [separator] %s" using the string and its associated label.
// Handles JP mode spacing (uses different separator).
// ============================================================================
void hud_create_localized_text(int string_id) {
    int owner = gx_begin_text_pass();  // FUN_803826ec

    // Select language separator
    const char* separator = g_empty_string;  // DAT_8061083c
    if (g_language_mode == 1) {
        separator = g_jp_separator;  // DAT_80610850 (JP spacing)
    }

    const char* text = get_string_by_id(string_id);
    const char* label = get_string_by_id(g_string_label_table[string_id]);  // DAT_803e1fb0

    // Format: "%s %s  %s"
    u32* formatted = text_format_buffer(
        "%s %s  %s",  // s__s_s___s_8040cf74
        label, separator, text);

    hud_text_submit(owner, 0, formatted);  // FUN_80105d5c
    gx_end_text_pass();  // FUN_80382738
}

// ============================================================================
// Address: 0x801058f4 | hud_create_multiline_text
// MASSIVE multi-line HUD text creator.
// Formats localized strings with 1-4 sub-lines based on line_count.
// Splits result by newlines, creates separate HUD elements per line with
// proportional Y offset and font size scaling. Supports custom highlight
// color on the last line segment (param_14 >= 0).
// ============================================================================
void hud_create_multiline_text(float total_height, int x_pos, int string_id,
                                int line_count, int highlight_color) {
    int owner = gx_begin_text_pass_b();  // FUN_803826cc

    const char* separator = g_empty_string;
    if (g_language_mode == 1) {
        separator = g_jp_separator;
    }

    u32* formatted;

    // Format string based on line count
    switch (line_count) {
        case 1: {
            const char* line1 = get_string_by_id(string_id + 1);
            const char* line0 = get_string_by_id(string_id);
            const char* label = get_string_by_id(g_string_label_table[string_id]);
            formatted = text_format_buffer("%s %s  %s\n%s", label, separator, line0, line1);
            break;
        }
        case 2: {
            const char* line2 = get_string_by_id(string_id + 2);
            const char* line1 = get_string_by_id(string_id + 1);
            const char* line0 = get_string_by_id(string_id);
            const char* label = get_string_by_id(g_string_label_table[string_id]);
            formatted = text_format_buffer("%s %s  %s\n%s\n%s", label, separator, line0, line1, line2);
            break;
        }
        case 3: {
            const char* line3 = get_string_by_id(string_id + 3);
            const char* line2 = get_string_by_id(string_id + 2);
            const char* line1 = get_string_by_id(string_id + 1);
            const char* line0 = get_string_by_id(string_id);
            const char* label = get_string_by_id(g_string_label_table[string_id]);
            formatted = text_format_buffer("%s %s  %s\n%s\n%s\n%s",
                                           label, separator, line0, line1, line2, line3);
            break;
        }
        default: {
            // Single line (0 or other)
            const char* line0 = get_string_by_id(string_id);
            const char* label = get_string_by_id(g_string_label_table[string_id]);
            formatted = text_format_buffer("%s %s  %s", label, separator, line0);
            break;
        }
    }

    // Split by newlines and create per-line elements
    char* newline_pos = strchr((char*)formatted, '\n');  // FUN_8038b308
    int total_len = strlen((char*)formatted);            // FUN_8038b5c8

    if (newline_pos != NULL && strlen(newline_pos + 1) > 0) {
        do {
            // Compute line length (capped at 0x7FF)
            int line_len = (int)newline_pos - (int)formatted;
            if (line_len > 0x7FF) {
                line_len = 0x7FF;
            }

            // Proportional Y offset for this line
            float y_proportion = total_height * ((float)line_len / (float)total_len);

            // Copy line to temp buffer and null-terminate
            char temp[2048];
            memcpy(temp, formatted, line_len);  // FUN_8038b4cc
            temp[line_len] = '\0';

            // Submit this line as a HUD element
            int slot = hud_text_submit(owner, x_pos, (u32*)temp);
            if (slot != 0) {
                // Scale font size proportionally with minimum
                float font_size = FONT_BASE_OFFSET + y_proportion;
                if (font_size < FONT_MIN_SIZE) {
                    font_size = FONT_MIN_SIZE;
                }
                *(float*)(slot + 0x154) = font_size;  // scale X
                *(float*)(slot + 0x150) = font_size;  // scale Y
                *(float*)(slot + 0x148) = font_size;  // scale Z
                *(int*)(slot + 0x168) = owner;        // owner ref
            }

            // Advance past newline
            formatted = (u32*)((char*)formatted + line_len + 1);
            newline_pos = strchr((char*)formatted, '\n');
        } while (newline_pos != NULL);
    }

    // Process final line (or only line if no newlines)
    int remaining_len = strlen((char*)formatted);
    if (remaining_len > 0) {
        float y_proportion = total_height * ((float)remaining_len / (float)total_len);

        int slot = hud_text_submit(owner, x_pos, formatted);
        if (slot != 0) {
            float font_size = FONT_BASE_OFFSET + y_proportion;
            if (font_size < FONT_MIN_SIZE) {
                font_size = FONT_MIN_SIZE;
            }
            *(float*)(slot + 0x154) = font_size;
            *(float*)(slot + 0x150) = font_size;
            *(float*)(slot + 0x148) = font_size;
            *(int*)(slot + 0x168) = owner;

            // Apply highlight color to last line if specified
            if (highlight_color >= 0) {
                *(int*)(slot + 0x160) = highlight_color;
                float highlight_size = FONT_HIGHLIGHT_SIZE;
                *(float*)(slot + 0x154) = highlight_size;
                *(float*)(slot + 0x150) = highlight_size;
                *(float*)(slot + 0x148) = highlight_size;
            }
        }
    }

    gx_end_text_pass_b();  // FUN_80382718
}

// ============================================================================
// Address: 0x80105cc4 | hud_clear_text_by_owner
// Clears HUD text elements of type 9 matching a specific owner ID.
// Iterates per-player (stride 0x177c), 16 slots per player (stride 0x16C).
// Sets matching element type to 0 (inactive).
// ============================================================================
void hud_clear_text_by_owner(void) {
    int owner_id = gx_get_current_owner();  // FUN_803826e4

    int player_offset = 0;
    int num_players = get_screen_mode();

    for (int p = 0; p < num_players; p++) {
        int slot_offset = 0;
        for (int s = 0; s < 16; s++) {
            int* element = (int*)(&g_hud_text_pool + player_offset + slot_offset);

            if (element[0] == 9 && element[0x5A] == owner_id) {  // type==9, owner at +0x168
                element[0] = 0;  // deactivate
            }

            slot_offset += 0x16C;
        }
        player_offset += 0x177C;
    }

    gx_end_owner_pass();  // FUN_80382730
}
