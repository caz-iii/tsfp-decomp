// HUD Keyboard Open/Launch API + Slider Interaction
// Functions for opening keyboard screens (popup/inline), buffer management,
// slider click handling, and value callbacks.
// Batch 093 | Date: 2026-08-10

#include "common.h"
#include "hud.h"
#include "hud_keyboard.h"
#include "text.h"

// 4 x 256-byte input buffers, one per player slot
static char g_keyboard_buffers[4][256];  // DAT_80526038, stride 0x100

// ============================================================================
// Address: 0x8004c3c4 | Name: keyboard_slider_cursor_click
// Handles cursor click on a slider/value item. Hit-tests 3 regions (track,
// left arrow, right arrow). For discrete selectors (type 6): opens numeric
// keyboard. For continuous sliders: opens text keyboard popup.
// ============================================================================
void keyboard_slider_cursor_click(int param_1) {
    float track_rect[4];
    float left_arrow_rect[4];
    float right_arrow_rect[4];

    int focused = hud_get_focused_item(param_1);
    hud_item_compute_slider_bounds(focused, track_rect, left_arrow_rect, right_arrow_rect);

    int hit_track = hud_cursor_hit_test_rect(track_rect);

    if (hit_track == 0) {
        // Not on track: check arrows
        if (*(int*)(focused + 0xb4) == 6) {
            // Discrete value selector: check left/right arrows
            int hit_left = hud_cursor_hit_test_rect(left_arrow_rect);
            if (hit_left == 0) {
                int hit_right = hud_cursor_hit_test_rect(right_arrow_rect);
                if (hit_right != 0) {
                    hud_item_value_increment();
                }
            } else {
                hud_item_value_decrement();
            }
        }
    } else if (*(int*)(focused + 0xb4) == 6) {
        // Discrete selector: click on track → open numeric keyboard
        keyboard_open_popup_numeric(param_1,
            *(int*)(focused + 0x48),   // current value
            *(int*)(focused + 0xa0),   // format params
            *(int*)(focused + 0x9c),   // validation ptr
            *(u32*)(focused + 0x94));  // flags
    } else {
        // Continuous slider: click on track → open text keyboard
        keyboard_open_popup_text(param_1,
            *(int*)(focused + 0x48),          // current value
            (int)*(short*)(focused + 0x98),   // max length
            *(int*)(focused + 0xa8),          // width limit
            *(int*)(focused + 0x94),          // flags
            &DAT_8004c134,                    // callback
            0);                               // cancel callback
    }
}

// ============================================================================
// Address: 0x8004c4b0 | Name: keyboard_buffer_pool_init
// Initializes the keyboard input buffer pool. Zeroes 4 x 256-byte buffers
// (one per player slot, stride 0x100). Called at HUD system init.
// ============================================================================
void keyboard_buffer_pool_init(void) {
    for (int i = 0; i < 4; i++) {
        g_keyboard_buffers[i][0] = '\0';
    }
}

// ============================================================================
// Address: 0x8004c4d8 | Name: keyboard_buffer_save_state
// Saves the current keyboard state strings into the 4-slot backup buffer pool.
// Copies the keyboard element pair plus two additional string parameters.
// ============================================================================
void keyboard_buffer_save_state(int param_1, int param_2, int* param_3, int* param_4) {
    u64 uVar1 = FUN_803826f0();
    int elem_a = (int)(uVar1 >> 32);
    int elem_b = (int)uVar1;

    FUN_8038b510((int*)&g_keyboard_buffers[0], (int*)elem_a);  // slot 0
    FUN_8038b510((int*)&g_keyboard_buffers[1], (int*)elem_b);  // slot 1
    FUN_8038b510((int*)&g_keyboard_buffers[2], param_3);       // slot 2
    FUN_8038b510((int*)&g_keyboard_buffers[3], param_4);       // slot 3
    FUN_8038273c();
}

// ============================================================================
// Address: 0x8004c560 | Name: keyboard_open_inline
// Opens the on-screen keyboard as an inline (embedded) element within a HUD
// item. Selects layout by param_7: 0/1=compact, 2=locked, else=standard.
// Sets flag 0x100 (inline mode). Configures input buffer from per-player pool,
// max length, width, callbacks. Auto-toggles case if shift state active.
// ============================================================================
void keyboard_open_inline(int param_1, int param_2, int param_3, int param_4,
                          u32 param_5, int param_6, int param_7) {
    u64 uVar4 = FUN_803826e0();
    int iVar1 = (int)(uVar4 >> 32);

    // Measure char width for layout calculation
    int font_id = hud_element_get_dimension(iVar1, 0);
    text_set_font_table(font_id);
    text_get_char_width(&DAT_8061028c);

    // Set inline mode flag
    *(u32*)(iVar1 + 0x224) |= 0x100;

    // Select layout builder based on mode
    if (param_7 == 0 || param_7 == 1) {
        *(u32*)(iVar1 + 0x224) |= 0x80;
        keyboard_build_layout_compact();
    } else if (param_7 == 2) {
        keyboard_build_layout_locked();
    } else {
        keyboard_build_layout_standard();
    }

    // Configure keyboard state
    *(u32*)(iVar1 + 0x224) |= param_5;
    *(int*)(iVar1 + 0x220) = (int)uVar4;  // output destination
    *(char**)(iVar1 + 0x21c) = &g_keyboard_buffers[*(int*)(iVar1 + 0x38)][0];

    if ((param_5 & 0x200) == 0) {
        // Clear buffer
        *(char*)(*(int*)(iVar1 + 0x21c) + 0xff) = '\0';
    } else {
        // Copy existing value to buffer and set cursor at end
        FUN_8038b4cc(*(int*)(iVar1 + 0x21c), *(int*)(iVar1 + 0x220), 0xff);
        int len = FUN_8038b5c8(*(int*)(iVar1 + 0x21c));
        *(int*)(iVar1 + 0x228) = len;  // cursor at end
    }

    // Set max length (clamped to 255)
    int max_len = (param_3 < 0x100) ? param_3 : 0xff;
    *(int*)(iVar1 + 0x22c) = max_len;

    // Store width limit and callbacks
    *(float*)(iVar1 + 0x234) = *(float*)&param_2;  // width (float via register)
    *(int*)(iVar1 + 0x230) = param_4;              // validation ptr
    *(int*)(iVar1 + 0x1c8) = param_6;              // confirm callback

    // Auto-start with shift if global state says so
    if (DAT_806119e4 == 0 || *(int*)(DAT_806119e4 + 4) == 0) {
        // Check for auto-select flag
        if ((param_5 & 0x8000) != 0) {
            *(u32*)(iVar1 + 0x224) &= ~0x100Cu;
            *(u32*)(iVar1 + 0x224) |= 2;
            hud_screen_mark_hidden(iVar1);
        }
    } else {
        hud_keyboard_toggle_case(iVar1);
    }
    FUN_8038272c();
}

// ============================================================================
// Address: 0x8004c714 | Name: keyboard_open_popup_text
// Opens the keyboard as a popup screen for text entry. Creates a new HUD
// element with keyboard_screen_create vtable. Copies existing text to input
// buffer, positions cursor at end, sets max length and width. If existing
// value is non-empty, sets select-all flag (0x10). Auto-toggles case.
// ============================================================================
void keyboard_open_popup_text(int param_1, int param_2, int param_3,
                              int param_4, int param_5, int param_6, int param_7) {
    u64 uVar4 = FUN_803826dc();
    int parent = (int)(uVar4 >> 32);

    // Store global max-length for screen create to pick up
    DAT_80611a00 = (param_3 < 0x100) ? param_3 : 0xff;
    fRam80611a04 = *(float*)&param_2;  // width limit (float via register)

    // Create keyboard element with text-mode vtable
    int kbd_elem = hud_element_create(parent, &PTR_keyboard_screen_create_803fdf98, 2);
    hud_element_link_and_show(parent, kbd_elem);

    // Configure keyboard state
    *(int*)(kbd_elem + 0x224) = param_5;            // flags
    *(int*)(kbd_elem + 0x220) = (int)uVar4;         // output destination
    *(char**)(kbd_elem + 0x21c) = &g_keyboard_buffers[*(int*)(parent + 0x38)][0];

    // Copy existing text to buffer
    FUN_8038b4cc(*(int*)(kbd_elem + 0x21c), *(int*)(kbd_elem + 0x220), 0xff);
    *(char*)(*(int*)(kbd_elem + 0x21c) + 0xff) = '\0';

    // Set cursor at end of existing text
    int len = text_strlen(*(char**)(kbd_elem + 0x21c));
    *(int*)(kbd_elem + 0x228) = len;

    // Max length and width
    int max_len = (param_3 < 0x100) ? param_3 : 0xff;
    *(int*)(kbd_elem + 0x22c) = max_len;
    *(float*)(kbd_elem + 0x234) = fRam80611a04;

    // Callbacks
    *(int*)(kbd_elem + 0x230) = param_4;   // validation ptr
    *(int*)(kbd_elem + 0x1c8) = param_6;   // confirm callback
    *(int*)(kbd_elem + 0x1cc) = param_7;   // cancel callback

    // If existing value is non-empty, enable select-all (0x10)
    if (*(int**)(kbd_elem + 0x230) != NULL && **(int**)(kbd_elem + 0x230) != 0) {
        *(u32*)(kbd_elem + 0x224) |= 0x10;
    }

    // Auto-toggle case
    if (DAT_806119e4 != 0 && *(int*)(DAT_806119e4 + 4) != 0) {
        hud_keyboard_toggle_case(kbd_elem);
    }
    FUN_80382728();
}

// ============================================================================
// Address: 0x8004c860 | Name: keyboard_open_popup_numeric
// Opens the keyboard as a popup screen for numeric value editing. Creates
// element with keyboard_screen_create_numeric vtable. Formats current integer
// value to string via hud_format_value_to_string. Sets cursor at end (text)
// or 0 (wrapping/time mode).
// ============================================================================
void keyboard_open_popup_numeric(int param_1, int param_2, int param_3,
                                 int param_4, u32 param_5) {
    u64 uVar4 = FUN_803826e4();
    int parent = (int)(uVar4 >> 32);

    // Create numeric keyboard element
    int kbd_elem = hud_element_create(parent, &PTR_keyboard_screen_create_numeric_803fe704, 2);
    hud_element_link_and_show(parent, kbd_elem);

    // Configure state
    *(u32*)(kbd_elem + 0x224) = param_5;
    *(int**)(kbd_elem + 0x220) = (int*)uVar4;   // output destination
    *(int*)(kbd_elem + 0x234) = param_3;         // format params
    *(int*)(kbd_elem + 0x230) = param_4;         // validation
    *(char**)(kbd_elem + 0x21c) = &g_keyboard_buffers[*(int*)(parent + 0x38)][0];

    // Compute max length from digit count
    int digits = hud_util_digit_count();
    int max_len = (digits < 0x101) ? digits : 0x100;
    *(int*)(kbd_elem + 0x22c) = max_len;
    *(float*)(kbd_elem + 0x234) = FLOAT_80613c88;  // no width limit

    // Format current value to string
    hud_format_value_to_string(*(int*)(kbd_elem + 0x21c), 0x100,
                               *(int*)uVar4, param_4, param_5, 0, 0);

    // Set cursor position
    if ((param_5 & 0x20) == 0) {
        // Text mode: cursor at end
        int len = text_strlen(*(char**)(kbd_elem + 0x21c));
        *(int*)(kbd_elem + 0x228) = len;
    } else {
        // Wrapping/time mode: cursor at start
        *(int*)(kbd_elem + 0x228) = 0;
    }
    FUN_80382730();
}

// ============================================================================
// Address: 0x8004c96c | Name: keyboard_value_change_callback_a
// Value change callback A: thin wrapper that calls hud_item_on_value_change.
// Used as the confirm callback for inline keyboard mode.
// ============================================================================
void keyboard_value_change_callback_a(void) {
    hud_item_on_value_change();
}

// ============================================================================
// Address: 0x8004c998 | Name: keyboard_value_change_callback_b
// Value change callback B: thin wrapper that calls hud_item_on_value_change.
// Used as the confirm callback for popup keyboard mode.
// ============================================================================
void keyboard_value_change_callback_b(void) {
    hud_item_on_value_change();
}

// ============================================================================
// Address: 0x8004c9c4 | Name: slider_snap_to_nearest_tick
// Snaps a slider thumb to the nearest discrete tick mark. Given cursor X
// position (param_1 + 0x20), computes tick spacing from bounds and step count,
// finds nearest tick boundary, writes normalized (0-1) position to
// param_2 + 0x19c.
// ============================================================================
void slider_snap_to_nearest_tick(int param_1, int param_2, int param_3, float* param_4) {
    float cursor_x = *(float*)(param_1 + 0x20);
    float start_x = *param_4;
    float total_width = param_4[2] - start_x;
    int num_ticks = (int)*(short*)(param_3 + 0x9a);

    // Tick spacing
    float tick_spacing = total_width / (float)num_ticks;
    float tick_pos = start_x + tick_spacing;

    for (int i = 0; i < num_ticks; i++) {
        float next_tick = tick_pos + tick_spacing;

        if (cursor_x < tick_pos) {
            // Cursor is before this tick: snap to closer of start or tick
            if (cursor_x - start_x < tick_pos - cursor_x) {
                tick_pos = start_x;
            }
            *(float*)(param_2 + 0x19c) = (tick_pos - start_x) / total_width;
            return;
        }

        if (tick_pos <= cursor_x && cursor_x <= next_tick) {
            // Between two ticks: snap to nearer
            if (cursor_x - tick_pos < next_tick - cursor_x) {
                next_tick = tick_pos;
            }
            *(float*)(param_2 + 0x19c) = (next_tick - start_x) / total_width;
            return;
        }

        // Advance to next tick
        float step = (i == 0) ? FLOAT_80613c88 : tick_spacing;
        tick_pos += step;
    }
}

// ============================================================================
// Address: 0x8004caa8 | Name: float_round_to_nearest_int
// Rounds a floating-point value to the nearest integer. Compares distance
// to floor vs ceil and returns the closer one.
// ============================================================================
u32 float_round_to_nearest_int(double param_1) {
    double ceil_val = FUN_803900e4(param_1);  // ceil
    double floor_val = FUN_80390324(param_1); // floor
    u32 result = (u32)(int)floor_val;

    // Compare: if closer to ceil, use ceil
    float dist_to_ceil = (float)ceil_val - (float)param_1;
    float dist_to_floor = (float)param_1 - (float)floor_val;

    if (dist_to_ceil < dist_to_floor) {
        result = (u32)(int)ceil_val;
    }
    return result;
}
