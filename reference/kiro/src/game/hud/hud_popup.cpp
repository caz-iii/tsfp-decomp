// Address: 0x80049838-0x800498f0 | Name: hud_popup | Date: 2026-08-10
// HUD popup/dropdown system for option selection.

#include "hud_internal.h"

// ============================================================================
// Popup Handlers
// ============================================================================

// Address: 0x80049838
// Simple popup cancel handler. Clears parent's popup reference and pops.
// Used as the cancel/back callback for dropdown popups.
void hud_popup_on_cancel(int popup) {
    // Clear parent's popup pointer
    *(int*)(*(int*)(popup + 0x2c) + 0x198) = 0;
    hud_screen_pop(popup);
}

// Address: 0x80049864
// Measures the maximum text width across all event strings in an item.
// Iterates the event chain (count stored at +0xc4), measures each string
// at popup scale (FLOAT_80613ccc), returns the widest measurement.
// Used to auto-size the dropdown popup width.
float hud_item_measure_max_event_width(int param_1, int item) {
    int event;
    float max_width = 0.0f;

    for (int i = 0; i < *(int*)(item + 0xc4); i++) {
        event = hud_item_get_event_at_index(item, i);
        float width = hud_measure_text_width_scaled(
            POPUP_TEXT_SCALE,              // FLOAT_80613ccc
            *(int*)(item + 0xbc),          // font table
            *(char**)(event + 4)           // text string
        );
        if (max_width < width) {
            max_width = width;
        }
    }

    return max_width;
}

// ============================================================================
// Dropdown Popup Creation
// ============================================================================

// Address: 0x800498f0
// MASSIVE: Creates and displays a dropdown/option popup for value selection.
//
// Full creation pipeline:
//   1. Create a child element (type 2 = popup)
//   2. Get the target item (+0x194) that triggered the popup
//   3. Compute text bounds for positioning
//   4. If flag 0x8000000 is NOT set:
//      - Measure max event width for auto-sizing
//      - If popup would be wider than available + minimum X offset, shift left
//   5. Store target item reference (+0x198)
//   6. Push element, get layout style (3 or 4 based on flag 0x800)
//   7. Init from layout with:
//      - Flags: 0x8000
//      - Dismiss callback: hud_popup_on_dismiss
//      - Cancel callback: hud_popup_on_cancel
//   8. Set flag 0x10 at +0x44 (animated)
//   9. Set margins
//  10. Configure scroll (max 8 items visible, or 7 if flag 0x8000000)
//  11. Create list items for each option (count from +0xc4)
//  12. Apply scroll, pop element stack
//  13. Position popup (X from text bounds, Y from item bottom)
//  14. Clamp Y if popup extends beyond screen bottom
//  15. Link to parent and show with animation
//  16. Find child matching current value (+0xc8) and scroll to it
//  17. Snap scroll position (no animation on initial display)
void hud_popup_create_dropdown(void) {
    int parent_element;
    int popup_element;
    int target_item;
    int layout_style;
    int prev_style;
    int selected_child;
    float text_bounds[4];
    float popup_x;
    float popup_y;
    float available_width;
    float max_event_width;
    int max_visible;

    parent_element = /* from register save stub */;
    popup_element = hud_element_create(parent_element, 0, 2);
    target_item = *(int*)(parent_element + 0x194);

    // Get text bounds for positioning
    hud_item_compute_text_bounds(target_item, text_bounds, NULL, NULL);
    popup_x = text_bounds[0];
    available_width = NORM_TO_PIXEL * (text_bounds[2] - popup_x);

    // Auto-size popup width
    if ((*(unsigned int*)(target_item + 0x28) & 0x8000000) == 0) {
        max_event_width = hud_item_measure_max_event_width(parent_element, target_item);
        if (available_width < POPUP_PADDING + max_event_width) {
            float min_x = POPUP_MIN_X_OFFSET * PIXEL_SCALE_X + SCREEN_OFFSET_X;
            if (min_x < popup_x) {
                popup_x = -(PIXEL_SCALE_X *
                    ((POPUP_PADDING + max_event_width) - available_width) - text_bounds[0]);
            }
        }
    }

    // Store popup reference
    *(int*)(parent_element + 0x198) = target_item;

    // Initialize popup element
    hud_element_push(popup_element);

    layout_style = 3;
    if ((*(unsigned int*)(parent_element + 0x40) & 0x800) != 0) {
        layout_style = 4;
    }
    prev_style = hud_set_layout_style(layout_style);

    hud_element_init_from_layout(0x8000, 0, hud_popup_on_dismiss, hud_popup_on_cancel);

    // Configure popup
    *(unsigned int*)(popup_element + 0x44) |= 0x10;  // animated flag
    hud_set_margins(0.0f, POPUP_MARGIN_BOTTOM, 1);

    // Set scroll constraints
    if ((*(unsigned int*)(target_item + 0x28) & 0x8000000) == 0) {
        max_visible = 8;
        if (*(int*)(target_item + 0xc4) < 10) {
            max_visible = *(int*)(target_item + 0xc4);
        }
    } else {
        max_visible = 7;
        if (*(int*)(target_item + 0xc4) < 10) {
            max_visible = *(int*)(target_item + 0xc4);
        }
    }
    hud_element_scroll_horizontal(0.0f, max_visible);

    // Create list items for each option
    for (int i = 0; i < *(int*)(target_item + 0xc4); i++) {
        int font = *(int*)(*(int*)(parent_element + 0x194) + 0x1c);
        int list_item = hud_add_list_item();
        *(int*)(list_item + 0x1c) = font;
    }

    hud_element_apply_scroll();
    hud_element_pop();

    // Position: Y from item bottom, clamp if needed
    popup_y = PIXEL_TO_NORM_Y * (text_bounds[3] - SCREEN_OFFSET_Y);
    if (0.0f < popup_y + *(float*)(popup_element + 0x64) -
        (*(float*)(parent_element + 0x6c) + *(float*)(parent_element + 0x60))) {
        if (POPUP_MAX_Y_OFFSET < popup_y) {
            popup_y = PIXEL_TO_NORM_Y * (text_bounds[1] - SCREEN_OFFSET_Y)
                      - *(float*)(popup_element + 0x64);
        }
    }

    // Set position and show
    popup_x = NORM_TO_PIXEL * (popup_x - SCREEN_OFFSET_X);
    hud_element_set_pos_xy(popup_x, popup_y, popup_element);
    hud_element_link_and_show(parent_element, popup_element);

    // Restore layout style
    hud_set_layout_style(prev_style);

    // Scroll to current selection
    selected_child = hud_find_child_by_id(popup_element, *(int*)(target_item + 0xc8));
    hud_scroll_to_item(popup_element, selected_child);
    hud_scroll_snap_position(popup_element);
}
