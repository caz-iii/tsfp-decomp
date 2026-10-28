// =============================================================================
// HUD Focus Management
// TimeSplitters: Future Perfect (GameCube)
// Decompiled: 2026-08-06
// =============================================================================
// Manages which menu item has focus (selection highlight). Provides multiple
// ways to set focus: by item pointer, by list offset, by child ID, or by
// ID + occurrence index. Handles scroll-to-visible and snap behaviors.
// =============================================================================

#include "common.h"
#include "hud.h"

// External references
extern float FLOAT_80613bb0;   // 0.0f
extern float FLOAT_80611a64;   // screen scale Y
extern int DAT_806119e4;       // focus restore state pointer (0 if none)

// Element focus state offsets:
//   +0x174: item linked list head
//   +0x17C: max selectable index
//   +0x180: current row index (focus_row)
//   +0x184: current column index (focus_col)
//   +0x188: focused item's user ID
//   +0x18C: focused item's occurrence index
//   +0x194: focused item pointer

// Item linked list node layout:
//   +0x00: item type (4=selectable, 7=sub-list, 8=callback)
//   +0x04: next sibling pointer
//   +0x0C: sub-list next pointer (column traversal)
//   +0x14: item user ID
//   +0x28: flags (bit 2 = disabled/locked)

// Focus restore state (DAT_806119e4, 0 when inactive):
//   +0x04: snap mode (0=smooth, nonzero=instant)
//   +0x08: "focus set" flag
//   +0x0C: recursion guard
//   +0x5C: target item ID
//   +0x60: target occurrence index

// -----------------------------------------------------------------------------
// Address: 0x80045f08 | hud_set_focus_to_item
// Core focus-setting function. Walks the item linked list to find the target
// item, determines its row/column indices, updates all focus state fields,
// and scrolls to make the item visible.
//
// Parameters:
//   param_1 - element pointer
//   param_2 - target item pointer (NULL to clear focus)
//
// Behavior:
//   - If target is NULL: marks focus as "set" in restore state, clears focus
//   - If restore state is active with pending target: delegates to
//     hud_set_focus_by_id_nth with saved ID/occurrence
//   - Walks linked list counting selectable items (types 4/7/8) to find row
//   - Traverses sub-list chain (+0x0C) to find column
//   - If found item is disabled (flag bit 2): recurse to next sibling
//   - Clamps row to max selectable index (-1)
//   - Updates: focus_row, focus_col, focused_item_ptr, item_id, occurrence_idx
//   - Scrolls to focused item (smooth or snap based on restore state)
//   - Computes pixel scroll position for working rect
//   - If item not found: resets scroll to 0, clears all focus fields
// -----------------------------------------------------------------------------
void hud_set_focus_to_item(int element, int* target_item) {
    if (target_item == NULL) {
        // Clear focus
        if (DAT_806119e4 != 0) {
            *(int*)(DAT_806119e4 + 8) = 1;  // mark "focus was set"
        }
        // Fall through to reset
    } else {
        int row = 0;
        int* current = *(int**)(element + 0x174);  // item list head
        int col = 0;

        // Check if focus restore state wants to redirect
        if (DAT_806119e4 != 0 &&
            *(int*)(DAT_806119e4 + 4) == 0 &&     // not in snap mode
            *(int*)(DAT_806119e4 + 0x60) >= 0 &&   // valid occurrence
            *(int*)(DAT_806119e4 + 0x0C) == 0) {   // not recursing
            // Redirect to saved target
            *(int*)(DAT_806119e4 + 0x0C) = 1;      // set recursion guard
            hud_set_focus_by_id_nth(element,
                                    *(int*)(DAT_806119e4 + 0x5C),
                                    *(int*)(DAT_806119e4 + 0x60));
            *(int*)(DAT_806119e4 + 0x0C) = 0;
            return;
        }

        // Walk item list to find target
        while (current != NULL) {
            if (current == target_item) {
                // Found it
                if (DAT_806119e4 != 0) {
                    *(int*)(DAT_806119e4 + 8) = 1;
                }

                *(int*)(element + 0x180) = row;    // focus_row
                *(int*)(element + 0x184) = col;    // focus_col

                // Clamp row to max
                if (*(int*)(element + 0x17C) <= *(int*)(element + 0x180)) {
                    *(int*)(element + 0x180) = *(int*)(element + 0x17C) - 1;
                }

                // Resolve actual item at this offset (may differ if clamped)
                int resolved = hud_get_item_at_offset(element, *(int*)(element + 0x180));

                // If item is disabled, try next sibling
                if ((*(u32*)(resolved + 0x28) & 4) != 0) {
                    hud_set_focus_to_item(element, (int*)target_item[1]);  // next in list
                    return;
                }

                // Set focused item pointer
                int focused = hud_get_item_at_offset(element, *(int*)(element + 0x180));
                *(int*)(element + 0x194) = focused;

                // Store item ID and occurrence index
                *(int*)(element + 0x188) = *(int*)(*(int*)(element + 0x194) + 0x14);
                int occurrence = hud_get_child_occurrence_index(element, *(int*)(element + 0x194));
                *(int*)(element + 0x18C) = occurrence;

                // Scroll to make item visible
                int viewport_owner = *(int*)(&DAT_8051a87c[*(int*)(element + 0x38) * 0x1E0] + 0x10);
                if (viewport_owner == 0 || (*(u32*)(element + 0x40) & 0x40000) != 0) {
                    hud_scroll_to_item(element, *(int*)(element + 0x194));
                }

                // Snap if restore state requests it
                if (DAT_806119e4 != 0 && *(int*)(DAT_806119e4 + 4) != 0) {
                    hud_scroll_snap_to_focus(element);
                }

                // Compute pixel scroll position
                *(float*)(element + 0xBC) = *(float*)(element + 0x80) * FLOAT_80611a64;
                return;
            }

            // Advance through list
            if ((int*)current[3] == NULL) {
                // No sub-list: advance to next row item
                int type = current[0];
                if (type == 4 || type == 7 || type == 8) {
                    row++;
                }
                current = (int*)current[1];  // next sibling
                col = 0;
            } else {
                // Sub-list: advance column
                col++;
                current = (int*)current[3];  // sub-list next
            }
        }

        // Item not found: fall through to reset
    }

    // Reset all focus state
    if (DAT_806119e4 != 0) {
        *(float*)(element + 0x80) = 0.0f;
        *(float*)(element + 0xBC) = 0.0f;
        *(float*)(element + 0x84) = 0.0f;
    }
    *(int*)(element + 0x180) = -1;       // focus_row = invalid
    *(int*)(element + 0x194) = 0;        // focused_item = NULL
    *(int*)(element + 0x188) = -1;       // item_id = invalid
    *(int*)(element + 0x18C) = -1;       // occurrence = invalid
}

// -----------------------------------------------------------------------------
// Address: 0x80046134 | hud_set_focus_by_offset
// Sets focus by list offset index. Resolves index to item pointer, then
// delegates to hud_set_focus_to_item.
//
// Parameters:
//   param_1 - element pointer
//   param_2 - list offset index (0-based row)
// -----------------------------------------------------------------------------
void hud_set_focus_by_offset(int element, int offset) {
    int* item = (int*)hud_get_item_at_offset(element, offset);
    hud_set_focus_to_item(element, item);
}

// -----------------------------------------------------------------------------
// Address: 0x8004616c | hud_set_focus_by_id
// Sets focus by child ID. Resolves ID to item pointer via linked list search,
// then delegates to hud_set_focus_to_item.
//
// Parameters:
//   param_1 - element pointer
//   param_2 - item user ID to find
// -----------------------------------------------------------------------------
void hud_set_focus_by_id(int element, int item_id) {
    int* item = (int*)hud_find_child_by_id(element, item_id);
    hud_set_focus_to_item(element, item);
}

// -----------------------------------------------------------------------------
// Address: 0x800461a4 | hud_set_focus_by_id_nth
// Sets focus by ID + occurrence index (for menus with multiple items sharing
// the same ID, e.g. weapon slots). Resolves via hud_find_nth_child_by_id.
//
// Parameters:
//   param_1 - element pointer
//   param_2 - item user ID
//   param_3 - occurrence index (0 = first, 1 = second, etc.)
// -----------------------------------------------------------------------------
void hud_set_focus_by_id_nth(int element, int item_id, int occurrence) {
    int* item = (int*)hud_find_nth_child_by_id(element, item_id, occurrence);
    hud_set_focus_to_item(element, item);
}
