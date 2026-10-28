// HUD Slider Drag Interaction + Item Confirm Dispatch
// Batch 094 | Date: 2026-08-10

#include "common.h"
#include "hud.h"
#include "hud_keyboard.h"

// ============================================================================
// Address: 0x8004cb48 | Name: slider_drag_update
// Handles continuous slider drag interaction. Computes thumb bounds, snaps
// to nearest tick if discrete, rounds to integer, updates value, recomputes
// cursor pixel position. Also handles initial drag grab.
// ============================================================================
void slider_drag_update(void) {
    float thumb_rect[4];
    float track_rect[4];
    float local_58[2];

    int iVar3 = FUN_803826f0();
    int item = *(int*)(iVar3 + 0x88);
    int player_offset = DAT_806119ec * 0x1e0;
    u32* cursor_data = (u32*)(&DAT_8051a814 + player_offset);

    hud_item_compute_slider_thumb_bounds(iVar3, track_rect, local_58);

    if ((*(u32*)(item + 0xc4) & 0x100000) == 0 || *(short*)(iVar3 + 0x9a) == 0) {
        // Continuous slider or no ticks
        if ((*(u32*)(item + 0xc4) & 0x10) != 0 && *(int*)(item + 400) == -6) {
            // Active drag in progress
            double thumb_offset = (double)(*(float*)(item + 0x19c) *
                                  (local_58[2] - local_58[0]));

            // Compute analog drag speed from stick input
            float drag_speed = FLOAT_80613d38 * *(float*)(&DAT_8051a844 + player_offset);
            float base_val = (float)((double)(*(u32*)(iVar3 + 200) ^ 0x80000000) -
                             DOUBLE_80613c90) / FLOAT_80613cb0;
            // longlong cast for value accumulation
            hud_item_on_value_change();
            hud_item_compute_slider_thumb_bounds(iVar3, NULL, local_58);

            // Update cursor position to track thumb
            *(float*)(&DAT_8051a834 + player_offset) = (float)((double)local_58[0] + thumb_offset);

            if (*(float*)(&DAT_8051a844 + player_offset) != FLOAT_80613c88) {
                *(float*)(&DAT_8051a82c + player_offset) =
                    FLOAT_80611a60 * (*(float*)(&DAT_8051a834 + player_offset) - FLOAT_80611a70);
            }
            hud_viewport_compute_pixel_pos(cursor_data);

            // Reset cursor velocity
            *(float*)(&DAT_8051a84c + player_offset) = FLOAT_80613c8c;
            *(float*)(&DAT_8051a850 + player_offset) = FLOAT_80613c8c;
            *(float*)(&DAT_8051a854 + player_offset) = FLOAT_80613c8c;
            *(float*)(&DAT_8051a858 + player_offset) = FLOAT_80613c8c;
            *(float*)(&DAT_8051a83c + player_offset) = FLOAT_80613c88;
            *(float*)(&DAT_8051a840 + player_offset) = FLOAT_80613c88;
            goto done;
        }
    } else {
        // Discrete slider with ticks: snap
        slider_snap_to_nearest_tick((int)cursor_data, item, iVar3, track_rect);
        float_round_to_nearest_int((double)(FLOAT_80613cb0 * *(float*)(item + 0x19c)));
        hud_item_on_value_change();
        hud_element_cache_scroll_pos(item, local_58);
    }

    // Check for initial grab on track
    int hit = hud_cursor_hit_test_rect(track_rect);
    if (hit != 0 && (*(u32*)(item + 0xc0) & 0x10) != 0) {
        if (*(short*)(iVar3 + 0x9a) == 0) {
            hud_element_cache_scroll_pos(item, track_rect);
        } else {
            slider_snap_to_nearest_tick((int)cursor_data, item, iVar3, track_rect);
        }
        float_round_to_nearest_int((double)(FLOAT_80613cb0 * *(float*)(item + 0x19c)));
        hud_item_on_value_change();
        *(int*)(item + 400) = -6;  // Enter drag state
        hud_element_cache_scroll_pos(item, local_58);
    }

done:
    FUN_8038273c();
}

// ============================================================================
// Address: 0x8004cd64 | Name: slider_drag_gate
// Gate function: only calls slider_drag_update if the item type is 7
// (slider with drag support).
// ============================================================================
void slider_drag_gate(int param_1) {
    if (*(int*)(param_1 + 0xb4) == 7) {
        slider_drag_update();
    }
}

// ============================================================================
// Address: 0x8004cd94 | Name: hud_item_confirm_dispatch
// Dispatches confirm/click action by HUD item type:
//   type 2 → dropdown popup (hud_popup_create_dropdown)
//   type 4 → text keyboard value change callback
//   type 5/6 → slider cursor click (keyboard_slider_cursor_click)
// Called when user presses A/confirm on a focused item.
// ============================================================================
void hud_item_confirm_dispatch(int param_1, int param_2) {
    int item_type = *(int*)(param_2 + 0xb4);

    if (item_type == 4) {
        keyboard_value_change_callback_b();
    } else if (item_type < 4) {
        if (item_type == 2) {
            hud_popup_create_dropdown();
        }
    } else if (item_type != 7 && item_type < 7) {
        // type 5 or 6: slider click
        keyboard_slider_cursor_click(param_1);
    }
}
