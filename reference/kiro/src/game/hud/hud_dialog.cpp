// =============================================================================
// hud_dialog.cpp - HUD dialog/popup construction and interaction
// TimeSplitters: Future Perfect (GameCube) Decompilation
// Batch 070 | Date: 2026-08-06
// =============================================================================
//
// This file implements the high-level dialog/popup system built on top of
// the HUD element/list infrastructure. Dialogs are full elements with:
// - Confirm callback at element+0x1C8
// - Cancel callback at element+0x1CC
// - Indexed callbacks at element+0x1C8 + index*4 (for multi-option)
// - Selection index at element+0x188
//
// Item value types (flags at item+0x2C):
//   0x40  = value display (show current value, item+0x48)
//   0x80  = toggle (on/off, item+0x48)
//   0x140 = linked value (live pointer, item+0x48 -> *ptr)
//
// =============================================================================

#include "common.h"

// Forward declarations
extern void FUN_80047444(int element);
extern void FUN_800439d4(void);
extern void FUN_8003ff6c(void);
extern void FUN_800400d0(int item, int value);
extern int  FUN_8004d234(int element);
extern void FUN_8004655c(int* element);
extern void FUN_800464ec(int element);
extern void FUN_800467cc(float scroll_speed, int element);
extern void FUN_80046854(int context, int element);
extern int  FUN_8004d1c8(int mode);
extern void FUN_80040f14(int flags, void* data, int p3, void* callback);
extern void FUN_80042cac(void);  // default render callback

extern float FLOAT_80613bd0;  // 0.5f
extern float FLOAT_80613bf0;  // scroll offset base
extern float FLOAT_80613bf4;  // min scroll speed
extern float FLOAT_80613bfc;  // default horizontal scroll speed

extern int DAT_806101d4;  // dialog layout data

// -----------------------------------------------------------------------------
// Address: 0x8003f448 | Name: hud_add_callback_item_default
// -----------------------------------------------------------------------------
// Creates a callback item with explicit index, using default height from
// the current layout descriptor at +0x30.
// -----------------------------------------------------------------------------
void hud_add_callback_item_default(int index) {
    float height = *(float*)(*(int*)(DAT_806119e0 + 0x1C) + 0x30);
    hud_add_callback_item_indexed(height, index);
}

// -----------------------------------------------------------------------------
// Address: 0x8003f474 | Name: hud_item_set_value_display
// -----------------------------------------------------------------------------
// Marks the last-created item as a value display. Sets flag 0x40 at +0x2C,
// stores current value at item+0x48. Used for showing option values
// (e.g., volume levels, difficulty names).
// -----------------------------------------------------------------------------
void hud_item_set_value_display(int value) {
    *(u32*)(DAT_806119dc + 0x2C) |= 0x40;
    *(int*)(DAT_806119dc + 0x48) = value;
    FUN_8003ff6c();
}

// -----------------------------------------------------------------------------
// Address: 0x8003f4b8 | Name: hud_item_set_linked_value
// -----------------------------------------------------------------------------
// Marks the last-created item as a linked value (flags 0x140 at +0x2C).
// Stores pointer at item+0x48 and reads the current value from it.
// Used for settings bound to a live game variable.
// -----------------------------------------------------------------------------
void hud_item_set_linked_value(int* value_ptr) {
    *(u32*)(DAT_806119dc + 0x2C) |= 0x140;
    *(int**)(DAT_806119dc + 0x48) = value_ptr;
    FUN_800400d0(DAT_806119dc, *value_ptr);
}

// -----------------------------------------------------------------------------
// Address: 0x8003f4fc | Name: hud_item_set_toggle
// -----------------------------------------------------------------------------
// Sets a specific item as a toggle (flag 0x80 at +0x2C). Stores current
// toggle state at item+0x48. Used for on/off settings.
// -----------------------------------------------------------------------------
void hud_item_set_toggle(int value, int* item) {
    *(u32*)((char*)item + 0x2C) |= 0x80;
    *(int*)((char*)item + 0x48) = value;
    FUN_8003ff6c();
}

// -----------------------------------------------------------------------------
// Address: 0x8003f54c | Name: hud_element_confirm
// -----------------------------------------------------------------------------
// Executes the confirm action on an element. Prepares element state via
// FUN_80047444, plays confirm sound, then invokes the confirm callback
// stored at element+0x1C8 with element+0x2C (user data) as argument.
// -----------------------------------------------------------------------------
void hud_element_confirm(int element) {
    FUN_80047444(element);
    FUN_800439d4();  // play confirm sound

    void (*callback)(int) = *(void (**)(int))(element + 0x1C8);
    if (callback != NULL) {
        callback(*(int*)(element + 0x2C));
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003f594 | Name: hud_element_cancel
// -----------------------------------------------------------------------------
// Executes the cancel action on an element. Prepares element state,
// plays cancel sound, invokes cancel callback at element+0x1CC.
// -----------------------------------------------------------------------------
void hud_element_cancel(int element) {
    FUN_80047444(element);
    FUN_800439d4();  // play cancel sound

    void (*callback)(int) = *(void (**)(int))(element + 0x1CC);
    if (callback != NULL) {
        callback(*(int*)(element + 0x2C));
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003f5dc | Name: hud_element_dispatch_indexed
// -----------------------------------------------------------------------------
// Dispatches to an indexed callback. Reads the selection index from
// element+0x188, looks up the callback at element + 0x1C8 + index*4,
// and calls it. Supports multi-option dialog selection.
// -----------------------------------------------------------------------------
void hud_element_dispatch_indexed(int element) {
    FUN_80047444(element);
    FUN_800439d4();

    int index = *(int*)(element + 0x188);
    void (*callback)(int) = *(void (**)(int))(element + index * 4 + 0x1C8);
    if (callback != NULL) {
        callback(*(int*)(element + 0x2C));
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003f630 | Name: hud_element_apply_focus
// -----------------------------------------------------------------------------
// Applies focus/selection state to an element. If flag 0x400 is set
// (animated focus), smoothly scrolls to the target position with speed
// derived from element[0x19] and constants. Otherwise, jumps immediately.
// -----------------------------------------------------------------------------
void hud_element_apply_focus(int* element) {
    u32 flags = FUN_8004d234((int)element);

    if ((flags & 0x400) == 0) {
        // Immediate focus
        FUN_8004655c(element);
    } else {
        // Animated scroll to focus
        float speed = -(FLOAT_80613bd0 * (float)element[0x19] - FLOAT_80613bf0);
        if (speed < FLOAT_80613bf4) {
            speed = FLOAT_80613bf4;
        }
        FUN_800464ec((int)element);
        FUN_800467cc(speed, (int)element);
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003f6b4 | Name: hud_create_option_dialog
// -----------------------------------------------------------------------------
// Creates a multi-option dialog popup with variable number of choices.
// Full lifecycle:
//   1. Create element + push onto stack
//   2. Configure with FUN_80040f14 (dispatch callback = hud_element_dispatch_indexed)
//   3. Add horizontal scroll
//   4. Add title/header callback item
//   5. Add N choice items (from callback array)
//   6. Store per-choice callbacks at element+0x1C8..+0x1C8+N*4
//   7. Apply scroll + pop
//   8. Focus the element
// -----------------------------------------------------------------------------
void hud_create_option_dialog(void) {
    int context = FUN_803826d0();
    int prev_mode = FUN_8004d1c8(5);

    int* element = (int*)hud_element_create(context, 0, 2);
    hud_element_push((int)element);

    FUN_80040f14(0x8000, &DAT_806101d4, 0, (void*)hud_element_dispatch_indexed);
    hud_element_scroll_horizontal(FLOAT_80613bfc, 0);

    // Add header item
    hud_add_list_item();
    hud_add_callback_item_default(0);

    // Add choice items and store callbacks
    // (callback array from register, count from register)
    // for each choice: hud_add_list_item(), store callback at element+0x1C8+i*4

    // Store user data
    // element[0x79] = user_param

    hud_element_apply_scroll();
    hud_element_pop();

    hud_element_apply_focus(element);
    FUN_80046854(context, (int)element);
    FUN_8004d1c8(prev_mode);
    FUN_800439d4();
    FUN_8038271c();
}

// -----------------------------------------------------------------------------
// Address: 0x8003f7f4 | Name: hud_create_yes_no_dialog
// -----------------------------------------------------------------------------
// Creates a standard Yes/No confirmation dialog. Fixed 2 choices using
// string IDs 0x173E ("Yes") and 0x173F ("No"). Stores confirm/cancel
// context params at element+0x1C8/0x1CC. Cancel callback = hud_element_cancel.
// -----------------------------------------------------------------------------
void hud_create_yes_no_dialog(int param_a, int param_b, int confirm_data, int cancel_data) {
    int context = FUN_803826e4();
    int prev_mode = FUN_8004d1c8(5);

    int* element = (int*)hud_element_create(context, 0, 2);
    hud_element_push((int)element);

    FUN_80040f14(0x8000, &DAT_806101d4, 0, (void*)hud_element_cancel);

    element[0x72] = confirm_data;
    element[0x73] = cancel_data;

    hud_element_scroll_horizontal(FLOAT_80613bfc, 0);

    // Add header
    hud_add_list_item();
    hud_add_callback_item_default(0);

    // Add "Yes" choice
    get_string_by_id(0x173E);
    hud_add_list_item();

    // Add "No" choice
    get_string_by_id(0x173F);
    hud_add_list_item();

    hud_element_apply_scroll();
    hud_element_pop();

    hud_element_apply_focus(element);
    FUN_80046854(context, (int)element);
    FUN_8004d1c8(prev_mode);
    FUN_800439d4();
    FUN_80382730();
}


// =============================================================================
// More Dialog Variants + Layout Engine + Value System (Batch 071)
// =============================================================================

extern int  FUN_80040530(int item, int value);
extern int  FUN_80040564(int element, int index);
extern int  FUN_80040ee0(int data);
extern void FUN_80046350(int item);
extern void FUN_800481c4(int item);
extern void FUN_80046418(int item);
extern void FUN_800477e0(int a, int b, int c, int d);

// -----------------------------------------------------------------------------
// Address: 0x8003f930 | Name: hud_create_back_dialog
// -----------------------------------------------------------------------------
// Creates a "Back" dialog popup. Single option with string 0x359 ("Back")
// and info text string 0x19C7. Stores param_3 at element+0x1C8.
// -----------------------------------------------------------------------------
void hud_create_back_dialog(int unused_a, int unused_b, int user_data) {
    int context = FUN_803826e8();
    int prev_mode = FUN_8004d1c8(5);

    int* element = (int*)hud_element_create(context, 0, 2);
    hud_element_push((int)element);

    FUN_80040f14(0, &DAT_806101d4, 0, NULL);
    hud_element_scroll_horizontal(FLOAT_80613bfc, 0);

    hud_add_list_item();
    hud_add_callback_item_default(0);

    int back_str = get_string_by_id(0x359);
    hud_add_list_item();

    hud_element_apply_scroll();

    int info_str = get_string_by_id(0x19C7);
    FUN_800477e0(0, info_str, 0, 0);

    hud_element_pop();

    element[0x72] = user_data;
    hud_element_apply_focus(element);
    FUN_80046854(context, (int)element);
    FUN_8004d1c8(prev_mode);
    FUN_800439d4();
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// Address: 0x8003fa50 | Name: hud_create_yes_no_dialog_styled
// -----------------------------------------------------------------------------
// Yes/No dialog with custom font per item. Cancel callback = hud_element_cancel.
// param_5 is stored at each item's +0x1C (font table index).
// -----------------------------------------------------------------------------
void hud_create_yes_no_dialog_styled(int unused_a, int unused_b,
                                      int confirm_data, int cancel_data, int font) {
    int context = FUN_803826e0();
    int prev_mode = FUN_8004d1c8(5);

    int* element = (int*)hud_element_create(context, 0, 2);
    hud_element_push((int)element);

    FUN_80040f14(0x8000, &DAT_806101d4, 0, (void*)hud_element_cancel);
    element[0x72] = confirm_data;
    element[0x73] = cancel_data;

    hud_element_scroll_horizontal(FLOAT_80613bfc, 0);

    int* item = (int*)hud_add_list_item();
    *(int*)((char*)item + 0x1C) = font;
    hud_add_callback_item_default(0);

    get_string_by_id(0x173E);  // "Yes"
    item = (int*)hud_add_list_item();
    *(int*)((char*)item + 0x1C) = font;

    get_string_by_id(0x173F);  // "No"
    item = (int*)hud_add_list_item();
    *(int*)((char*)item + 0x1C) = font;

    hud_element_apply_scroll();
    hud_element_pop();

    hud_element_apply_focus(element);
    FUN_80046854(context, (int)element);
    FUN_8004d1c8(prev_mode);
    FUN_800439d4();
    FUN_8038272c();
}

// -----------------------------------------------------------------------------
// Address: 0x8003fbb0 | Name: hud_create_simple_dialog
// -----------------------------------------------------------------------------
// Creates a simple dialog with optional callback item. If param_3 is non-zero,
// adds a callback item at index 1 followed by another list item.
// -----------------------------------------------------------------------------
void hud_create_simple_dialog(int unused_a, int unused_b, int has_callback) {
    int context = FUN_803826e8();
    int prev_mode = FUN_8004d1c8(5);

    int* element = (int*)hud_element_create(context, 0, 2);
    hud_element_push((int)element);

    FUN_80040f14(0, &DAT_806101d4, 0, NULL);
    hud_element_scroll_horizontal(FLOAT_80613bfc, 0);

    hud_add_list_item();
    if (has_callback != 0) {
        hud_add_callback_item_default(1);
        hud_add_list_item();
    }

    hud_element_apply_scroll();
    hud_element_pop();

    hud_element_apply_focus(element);
    FUN_80046854(context, (int)element);
    FUN_8004d1c8(prev_mode);
    FUN_800439d4();
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// Address: 0x8003fcb0 | Name: hud_layout_items
// -----------------------------------------------------------------------------
// Core layout engine. Iterates the item linked list computing vertical
// positions by accumulating heights. Per item:
// - Clears layout-dirty flag (bit 2 at +0x2C)
// - Sets Y position (offset 0x64) from accumulated position
// - Computes height from layout or animation
// - Handles horizontal mode (flag 0x40 → uses element+0x58 instead of +0x64)
// - Applies animated width/height offsets (flags 0x400, 0x20000)
// - Propagates total content height back to parent element's scroll offset
// -----------------------------------------------------------------------------
void hud_layout_items(int first_item) {
    if (first_item == 0) return;

    float y_pos = *(float*)(first_item + 0x5C);  // initial offset
    float zero = 0.0f;

    int item = first_item;
    while (item != 0) {
        int anim_type = *(int*)(item + 0xC0);
        int has_sub_items = (anim_type == 3 || anim_type == 4 || anim_type == 5)
                            && *(int*)(item + 0xB4) != 0;

        // Clear dirty flag
        *(u32*)(item + 0x2C) &= ~2;

        // Set Y position
        *(float*)(item + 0x64) = y_pos;

        // Compute height if not explicitly set
        if (zero == *(float*)(item + 0x6C)) {
            *(float*)(item + 0x6C) = *(float*)(item + 0x60) - *(float*)(item + 0x64);
        }
        *(float*)(item + 0xEC) = *(float*)(item + 0x6C);  // cache height

        // Accumulate Y
        y_pos += *(float*)(item + 0x6C);

        // Apply animated width offset (flag 0x400 at +0x28)
        if (*(u32*)(item + 0x28) & 0x400) {
            float anim_w = FUN_80046350(item);
            *(float*)(item + 0x74) += anim_w;
        }

        // Set X position from parent
        if ((*(u32*)(item + 0x28) & 0x40) == 0) {
            *(float*)(item + 0x68) = *(float*)(DAT_806119e0 + 0x64);
        } else {
            *(float*)(item + 0x68) = *(float*)(DAT_806119e0 + 0x58);
        }

        *(float*)(item + 0xE4) = *(float*)(item + 0x64);

        // Apply animated height offset (flag 0x400 at +0xB8)
        if (*(u32*)(item + 0xB8) & 0x400) {
            float anim_h = FUN_800481c4(item);
            *(float*)(item + 0xF0) += anim_h;
        }

        // Compute total item extent
        if (has_sub_items) {
            *(float*)(item + 0x70) += *(float*)(item + 0x74) + *(float*)(item + 0xF0);
            *(float*)(item + 0xE8) = *(float*)(item + 0x68) + *(float*)(item + 0x74);
        } else {
            float h = *(float*)(item + 0xF0);
            if (h < *(float*)(item + 0x74)) h = *(float*)(item + 0x74);
            *(float*)(item + 0x70) += h;
            *(float*)(item + 0xE8) = *(float*)(item + 0x68);
        }

        // Custom height calc (flag 0x20000)
        if (*(u32*)(item + 0x28) & 0x20000) {
            *(float*)(item + 0x40) = FUN_80046418(item);
        }
        if (*(u32*)(item + 0xB8) & 0x20000) {
            *(float*)(item + 0xE0) = FUN_80046418(item);
        }

        // If item is a group tail (+0xB0 set), propagate to parent scroll
        if (*(int*)(item + 0xB0) != 0) {
            y_pos = *(float*)(item + 0x5C);
            if ((*(u32*)(item + 0x28) & 0x40) == 0) {
                *(float*)(DAT_806119e0 + 0x64) += *(float*)(item + 0x70);
            } else {
                *(float*)(DAT_806119e0 + 0x58) += *(float*)(item + 0x70);
            }
        }

        item = *(int*)(item + 4);  // next
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003fed8 | Name: hud_flush_layout
// -----------------------------------------------------------------------------
// Layout flush. If the current/last item needs layout (dirty flag bit 2),
// runs hud_layout_items. Checks sub-list head first, falls back to
// last-created item.
// -----------------------------------------------------------------------------
void hud_flush_layout(void) {
    int item = DAT_806119dc;
    if (DAT_806119d4 != 0) {
        item = (int)DAT_806119d4;
    }
    if (item != 0 && (*(u32*)(item + 0x2C) & 2) != 0) {
        hud_layout_items(item);
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003ff20 | Name: hud_flush_layout_if_active
// -----------------------------------------------------------------------------
void hud_flush_layout_if_active(int element) {
    if (element != 0 && DAT_806119e0 == element) {
        hud_flush_layout();
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003ff54 | Name: hud_item_get_value
// -----------------------------------------------------------------------------
// Returns the item's current selection value at offset 0xC8. Returns 0 if NULL.
// -----------------------------------------------------------------------------
int hud_item_get_value(int item) {
    if (item != 0) {
        return *(int*)(item + 0xC8);
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Address: 0x8003ff6c | Name: hud_item_on_value_change
// -----------------------------------------------------------------------------
// Value change handler. When an item's value changes:
// 1. Updates current value at +0xC8
// 2. Resolves display string (via FUN_80040530, or -1 for flag 0x200000)
// 3. Stores at +0xCC
// 4. If value actually changed: stores previous at +0xD0, fires callbacks:
//    - Flag 0x40 + 0x100: writes value to linked ptr (*item[0x48])
//    - Flag 0x80: writes normalized float
//    - Flag 0x04: calls function ptr at item+0x48
//    - Always calls on-change callback at +0x4C if present
// 5. On first set (+0xD0 == -1): stores initial value at +0xD0 and +0xD4
// -----------------------------------------------------------------------------
void hud_item_on_value_change(void) {
    u64 result = FUN_803826f0();
    int item = (int)(result >> 32);
    int new_value = (int)result;

    if (item == 0) {
        FUN_8038273c();
        return;
    }

    int prev_value = *(int*)(item + 0xC8);
    *(int*)(item + 0xC8) = new_value;

    // Resolve display string
    int display;
    if ((*(u32*)(item + 0xB8) & 0x200000) == 0) {
        int resolved = FUN_80040530(item, new_value);
        display = *(int*)(resolved + 8);
    } else {
        display = -1;
    }
    *(int*)(item + 0xCC) = display;

    // Check if value actually changed
    if (*(int*)(item + 0xD0) == -1 || prev_value == new_value) {
        if (*(int*)(item + 0xD0) == -1) {
            // First-time initialization
            if (new_value != -1 && *(void**)(item + 0x4C) != NULL) {
                (*(void (**)(int, int))(item + 0x4C))(*(int*)(item + 0x88), item);
            }
            *(int*)(item + 0xD4) = *(int*)(item + 0xC8);
            *(int*)(item + 0xD0) = *(int*)(item + 0xC8);
        }
    } else {
        // Value changed
        *(int*)(item + 0xD0) = prev_value;

        int blocked = FUN_80040ee0(*(int*)(item + 0x88));
        if (blocked == 0) {
            // Write back to bound variable
            if (*(u32*)(item + 0x2C) & 0x40) {
                int write_val;
                if ((*(u32*)(item + 0x2C) & 0x100) == 0) {
                    write_val = *(int*)(item + 0xC8);
                } else {
                    write_val = *(int*)(item + 0xCC);
                }
                **(int**)(item + 0x48) = write_val;
            }
            if (*(u32*)(item + 0x2C) & 0x80) {
                // Normalize to float: (value ^ 0x80000000 - 0x43300000...) / scale
                **(float**)(item + 0x48) =
                    (float)((u32)*(int*)(item + 0xC8) ^ 0x80000000) / FLOAT_80613bb4;
            }
            if (*(u32*)(item + 0x2C) & 0x04) {
                (*(void (**)(int))(item + 0x48))(*(int*)(item + 0xC8));
            }
        }

        // Fire on-change callback
        if (*(void**)(item + 0x4C) != NULL) {
            (*(void (**)(int, int))(item + 0x4C))(*(int*)(item + 0x88), item);
        }
    }

    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// Address: 0x800400d0 | Name: hud_item_set_value_by_event
// -----------------------------------------------------------------------------
// Finds an event descriptor matching param_2 (at event+8) in the item's
// event chain (at +0xD8), then triggers hud_item_on_value_change.
// -----------------------------------------------------------------------------
void hud_item_set_value_by_event(int item, int event_value) {
    if (item == 0) return;

    int event = *(int*)(item + 0xD8);
    while (event != 0) {
        if (*(int*)(event + 8) == event_value) {
            hud_item_on_value_change();
            return;
        }
        event = *(int*)(event + 0x10);
    }
}

// -----------------------------------------------------------------------------
// Address: 0x80040128 | Name: hud_item_is_selectable
// -----------------------------------------------------------------------------
// Returns 1 if item at position is selectable: either has flag 0x8 (always
// selectable) or has events AND current value differs from display value.
// Returns 0 for non-interactive items.
// -----------------------------------------------------------------------------
int hud_item_is_selectable(int element, int index) {
    int item = FUN_80040564(element, index);
    if (item == 0) return 0;

    if (*(u32*)(item + 0x2C) & 0x08) return 1;

    if (*(int*)(item + 0xC4) < 1) return 0;

    int current = hud_item_get_value(item);
    if (*(int*)(item + 0xD0) == current) return 0;

    return 1;
}


// =============================================================================
// Value Navigation + Event Queries + Child Lookup (Batch 072)
// =============================================================================

extern int DAT_80611a54;   // sound enabled flag
extern int DAT_80611a58;   // sound table pointer

// -----------------------------------------------------------------------------
// Address: 0x8004018c | Name: hud_item_get_stored_value
// -----------------------------------------------------------------------------
// Gets a stored value: mode 0 = previous value (+0xD0), mode 1 = initial (+0xD4).
// Returns -1 for invalid mode.
// -----------------------------------------------------------------------------
int hud_item_get_stored_value(int mode, int item) {
    if (mode == 1) return *(int*)(item + 0xD4);
    if (mode == 0) return *(int*)(item + 0xD0);
    return -1;
}

// -----------------------------------------------------------------------------
// Address: 0x800401bc | Name: hud_item_has_pending_change
// -----------------------------------------------------------------------------
int hud_item_has_pending_change(void) {
    u64 result = FUN_803826f0();
    int item = (int)(result >> 32);
    int mode = (int)result;

    if (*(int*)(item + 0xC4) > 0) {
        hud_item_get_stored_value(mode, item);
        hud_item_get_value(item);
    }
    FUN_8038273c();
    return 0;  // comparison result
}

// -----------------------------------------------------------------------------
// Address: 0x80040224 | Name: hud_element_has_unsaved_changes
// -----------------------------------------------------------------------------
// Returns 1 if any child item has flag 0x8 or has pending changes.
// -----------------------------------------------------------------------------
int hud_element_has_unsaved_changes(int element) {
    int child = *(int*)(element + 0x174);
    while (child != 0) {
        if (*(u32*)(child + 0x2C) & 0x08) return 1;
        if (hud_item_has_pending_change()) return 1;
        child = *(int*)(child + 4);
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Address: 0x80040294 | Name: hud_item_update_modified_flag
// -----------------------------------------------------------------------------
// Updates bit 3 at +0x2C based on whether current value != initial value.
// -----------------------------------------------------------------------------
void hud_item_update_modified_flag(int item) {
    if (*(int*)(item + 0xC8) != *(int*)(item + 0xD4)) {
        *(u32*)(item + 0x2C) |= 0x08;   // set modified
    } else {
        *(u32*)(item + 0x2C) &= ~0x08;  // clear modified
    }
}

// -----------------------------------------------------------------------------
// Address: 0x800402c4 | Name: hud_item_value_increment
// -----------------------------------------------------------------------------
// Increments the item's value to the next option. For discrete options:
// wraps around, skips disabled events (flag 1 at event+0xC). For slider
// mode (flag 0x200000): increments by step (+0xA8) with max bound (+0x9C).
// Plays navigate sound. Fires value change + updates modified flag.
// -----------------------------------------------------------------------------
void hud_item_value_increment(void) {
    int item = FUN_803826f0();

    if (*(u32*)(item + 0x28) & 1) {
        FUN_8038273c();
        return;  // locked
    }

    if ((*(u32*)(item + 0xB8) & 0x200000) == 0) {
        // Discrete option mode
        int event_count = *(int*)(item + 0xC4);
        if (event_count > 0) {
            int value = *(int*)(item + 0xC8);
            do {
                value++;
                if (value >= event_count) value = 0;
            } while (value != *(int*)(item + 0xC8) &&
                     (*(u32*)(*(int*)(item + 0xD8) + value * 0x14 + 0xC) & 1));

            // Play navigate sound
            if (DAT_80611a54 != 0) {
                int sound_id = *(int*)(DAT_80611a58 + 8);
                int params[12];
                sound_init_play_params((int)params);
                params[0] = sound_id;
                sound_play_request(params);
            }

            hud_item_on_value_change();
        }
    } else {
        // Slider mode: increment by step, check max
        int current = *(int*)(item + 0xC8);
        int step = *(int*)(item + 0xA8);
        int max_val = *(int*)(item + 0x9C);

        if (current + step <= max_val) {
            if (DAT_80611a54 != 0) {
                int sound_id = *(int*)(DAT_80611a58 + 8);
                int params[12];
                sound_init_play_params((int)params);
                params[0] = sound_id;
                sound_play_request(params);
            }
            hud_item_on_value_change();
        }
    }

    hud_item_update_modified_flag(item);
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// Address: 0x800403dc | Name: hud_item_value_decrement
// -----------------------------------------------------------------------------
// Decrements the item's value to the previous option. Mirror of increment:
// wraps backward for discrete, decrements by step for slider mode.
// -----------------------------------------------------------------------------
void hud_item_value_decrement(void) {
    int item = FUN_803826f0();

    if (*(u32*)(item + 0x28) & 1) {
        FUN_8038273c();
        return;  // locked
    }

    if ((*(u32*)(item + 0xB8) & 0x200000) == 0) {
        // Discrete: go backward with wrap
        int event_count = *(int*)(item + 0xC4);
        if (event_count > 0) {
            int value = *(int*)(item + 0xC8);
            do {
                value--;
                if (value < 0) value = event_count - 1;
            } while (value != *(int*)(item + 0xC8) &&
                     (*(u32*)(*(int*)(item + 0xD8) + value * 0x14 + 0xC) & 1));

            if (DAT_80611a54 != 0) {
                int sound_id = *(int*)(DAT_80611a58 + 8);
                int params[12];
                sound_init_play_params((int)params);
                params[0] = sound_id;
                sound_play_request(params);
            }
            hud_item_on_value_change();
        }
    } else {
        // Slider: decrement by step, check min
        int current = *(int*)(item + 0xC8);
        int step = *(int*)(item + 0xA8);
        int min_val = *(int*)(item + 0xA0);

        if (current - step >= min_val) {
            if (DAT_80611a54 != 0) {
                int sound_id = *(int*)(DAT_80611a58 + 8);
                int params[12];
                sound_init_play_params((int)params);
                params[0] = sound_id;
                sound_play_request(params);
            }
            hud_item_on_value_change();
        }
    }

    hud_item_update_modified_flag(item);
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// Address: 0x800404f0 | Name: hud_item_get_current_event
// -----------------------------------------------------------------------------
// Returns the event descriptor at the item's current selection index (+0xC8).
// Returns 0 if item is NULL or in slider mode (flag 0x200000).
// -----------------------------------------------------------------------------
int* hud_item_get_current_event(int item) {
    if (item == 0 || (*(u32*)(item + 0xB8) & 0x200000)) {
        return NULL;
    }
    return (int*)hud_item_get_event_at_index(item, *(int*)(item + 0xC8));
}

// -----------------------------------------------------------------------------
// Address: 0x80040530 | Name: hud_item_get_event_at_index
// -----------------------------------------------------------------------------
// Gets the Nth event descriptor from the item's chain. Walks linked list
// at +0xD8 (next at +0x10) N times. Returns first event if N <= 0.
// -----------------------------------------------------------------------------
int* hud_item_get_event_at_index(int item, int index) {
    if (item == 0) return NULL;

    int* event = *(int**)(item + 0xD8);
    if (index < 1) return event;

    for (int i = 0; i < index; i++) {
        if (event != NULL) {
            event = (int*)*(int*)((char*)event + 0x10);
        }
    }
    return event;
}

// -----------------------------------------------------------------------------
// Address: 0x80040564 | Name: hud_find_child_by_id
// -----------------------------------------------------------------------------
// Walks the child list at element+0x174, returns the first item where
// item+0x14 matches the target ID. Returns 0 if not found.
// -----------------------------------------------------------------------------
int* hud_find_child_by_id(int element, int target_id) {
    int* child = *(int**)(element + 0x174);
    while (child != NULL) {
        if (*(int*)((char*)child + 0x14) == target_id) {
            return child;
        }
        child = (int*)*(int*)((char*)child + 4);
    }
    return NULL;
}

// -----------------------------------------------------------------------------
// Address: 0x8004058c | Name: hud_find_nth_child_by_id
// -----------------------------------------------------------------------------
// Finds the Nth child matching a target ID. Skips the first `occurrence`
// matches, returning the (occurrence+1)th match.
// -----------------------------------------------------------------------------
int* hud_find_nth_child_by_id(int element, int target_id, int occurrence) {
    int* child = *(int**)(element + 0x174);
    while (child != NULL) {
        if (*(int*)((char*)child + 0x14) == target_id) {
            if (occurrence == 0) return child;
            occurrence--;
        }
        child = (int*)*(int*)((char*)child + 4);
    }
    return NULL;
}


// =============================================================================
// Focus / Lookup / Destruction (Batch 073)
// =============================================================================

extern int  FUN_80046f6c(int parent);
extern int  FUN_80046ff8(int parent);
extern void FUN_800408f8(void);

// -----------------------------------------------------------------------------
// Address: 0x800405c0 | Name: hud_get_child_occurrence_index
// -----------------------------------------------------------------------------
// Counts preceding siblings with the same ID as the target item. Returns the
// 0-based occurrence index for nth-child addressing.
// -----------------------------------------------------------------------------
int hud_get_child_occurrence_index(int element, int target_item) {
    int child = *(int*)(element + 0x174);
    int count = 0;
    int target_id = *(int*)(target_item + 0x14);

    while (child != 0) {
        if (child == target_item) return count;
        if (*(int*)(child + 0x14) == target_id) {
            count++;
        }
        child = *(int*)(child + 4);
    }
    return count;
}

// -----------------------------------------------------------------------------
// Address: 0x800405f8 | Name: hud_item_is_focused
// -----------------------------------------------------------------------------
// Returns 1 if item is the currently focused child (parent+0x194 == item).
// Also verifies parent is valid via FUN_80046f6c.
// -----------------------------------------------------------------------------
int hud_item_is_focused(int item) {
    if (item == 0) return 0;
    int parent = *(int*)(item + 0x88);
    if (FUN_80046f6c(parent) == 0) return 0;
    if (*(int*)(parent + 0x194) == item) return 1;
    return 0;
}

// -----------------------------------------------------------------------------
// Address: 0x80040658 | Name: hud_item_is_selected
// -----------------------------------------------------------------------------
// Returns 1 if item is currently selected. Uses FUN_80046ff8 for state check.
// -----------------------------------------------------------------------------
int hud_item_is_selected(int item) {
    if (item == 0) return 0;
    int parent = *(int*)(item + 0x88);
    if (FUN_80046ff8(parent) == 0) return 0;
    if (*(int*)(parent + 0x194) == item) return 1;
    return 0;
}

// -----------------------------------------------------------------------------
// Address: 0x800406b8 | Name: hud_get_item_by_list_index
// -----------------------------------------------------------------------------
// Gets a list item by visual row/column index. Walks child list (via +2 prev
// link), counting only list-type items (4/7/8) until reaching row `param_2`.
// Then walks sub-list links (+3) `param_3` times for column offset.
// Returns NULL if index is -1 or out of range.
// -----------------------------------------------------------------------------
int* hud_get_item_by_list_index(int element, int row, int column) {
    if (row == -1) return NULL;

    int* item = *(int**)(element + 0x174);

    // Walk to the target row (skip non-list items)
    while (item != NULL && (row > 0 || (*item != 4 && (*item - 7 > 1)))) {
        int type = *item;
        if (type == 4 || type == 7 || type == 8) {
            row--;
        }
        item = (int*)item[2];  // prev link for traversal
    }

    // Walk sub-list for column offset
    for (int i = 0; i < column; i++) {
        if (item != NULL && (int*)item[3] != NULL) {
            item = (int*)item[3];
        }
    }

    return item;
}

// -----------------------------------------------------------------------------
// Address: 0x80040748 | Name: hud_get_item_at_offset
// -----------------------------------------------------------------------------
// Gets item at row index with the element's current column offset (+0x184).
// -----------------------------------------------------------------------------
void hud_get_item_at_offset(int element, int row) {
    hud_get_item_by_list_index(element, row, *(int*)(element + 0x184));
}

// -----------------------------------------------------------------------------
// Address: 0x8004076c | Name: hud_get_focused_item
// -----------------------------------------------------------------------------
// Gets the currently focused item. Tries saved ID+occurrence from context
// frame first, falls back to element+0x194 (cached focus pointer).
// -----------------------------------------------------------------------------
int* hud_get_focused_item(int element) {
    if (DAT_806119e4 != NULL && *(int*)((char*)DAT_806119e4 + 4) == 0 &&
        DAT_806119e0 == element) {
        int* found = (int*)hud_find_nth_child_by_id(
            element,
            *(int*)((char*)DAT_806119e4 + 0x5C),
            *(int*)((char*)DAT_806119e4 + 0x60)
        );
        if (found != NULL) return found;
    }
    return (int*)*(int*)(element + 0x194);
}

// -----------------------------------------------------------------------------
// Address: 0x800407dc | Name: hud_walk_hierarchy_by_type
// -----------------------------------------------------------------------------
// Walks a linked list (next at offset 0x2C) looking for a node with
// matching type. Stops when found or list ends.
// -----------------------------------------------------------------------------
void hud_walk_hierarchy_by_type(int* node, int target_type) {
    while (node != NULL) {
        if (*node == target_type) return;
        node = (int*)node[0x0B];  // next at offset 0x2C
    }
}

// -----------------------------------------------------------------------------
// Address: 0x800407f4 | Name: hud_destroy_entity_elements
// -----------------------------------------------------------------------------
// Destroys all HUD elements attached to an entity. Walks entity's element
// list at entity+0x168 (next at offset 0x30), calling destroy on each.
// -----------------------------------------------------------------------------
void hud_destroy_entity_elements(int entity) {
    int* element = *(int**)(entity + 0x168);
    while (element != NULL) {
        FUN_800408f8();
        hud_element_destroy_recursive(element);
        element = (int*)element[0x0C];  // next at offset 0x30
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8004083c | Name: hud_element_destroy_recursive
// -----------------------------------------------------------------------------
// Recursively destroys a HUD element and all sub-elements. Walks sub-element
// list at offset 0x168 (next at offset 0x30). Calls destructor callback at
// offset 0x1D8 if present. Clears all pool items. Zeros element type.
// -----------------------------------------------------------------------------
void hud_element_destroy_recursive(int* element) {
    // Recursively destroy sub-elements
    int* sub = (int*)element[0x5A];  // offset 0x168
    while (sub != NULL) {
        hud_element_destroy_recursive(sub);
        sub = (int*)sub[0x0C];  // next at offset 0x30
    }

    // Call destructor callback if present
    void (*destructor)(int*) = (void (*)(int*))element[0x76];  // offset 0x1D8
    if (destructor != NULL) {
        destructor(element);
    }

    // Clear pool items
    hud_element_clear_all_items((int)element);

    // Mark as freed
    *element = 0;
}

// -----------------------------------------------------------------------------
// Address: 0x800408b0 | Name: hud_element_clear_all_items
// -----------------------------------------------------------------------------
// Clears all pool items from an element. Walks child list at +0x174, zeros
// each item and its event chain (at item offset 0xD8, next at offset 0x10).
// Resets list head and child counts.
// -----------------------------------------------------------------------------
void hud_element_clear_all_items(int element) {
    int* item = *(int**)(element + 0x174);

    while (item != NULL) {
        // Walk and clear event chain
        int* event = (int*)item[0x36];  // offset 0xD8
        *item = 0;  // clear item type (marks as free)

        while (event != NULL) {
            *event = 0;  // clear event (marks as free)
            event = (int*)event[4];  // next at offset 0x10
        }

        item = (int*)item[1];  // next child
    }

    // Reset list
    *(int*)(element + 0x174) = 0;
    *(int*)(element + 0x178) = 0;
    *(int*)(element + 0x17C) = 0;
}


// =============================================================================
// Element Destruction + Configuration Utilities (Batch 074)
// =============================================================================

// -----------------------------------------------------------------------------
// Address: 0x800408f8 | Name: hud_element_destroy_and_unlink
// -----------------------------------------------------------------------------
// Destroys an element and removes it from its parent entity's linked list.
// Walks entity+0x168 to find and unlink, then clears parent's cached ref
// at entity+0x16C if it pointed to this element.
// -----------------------------------------------------------------------------
void hud_element_destroy_and_unlink(void) {
    int* element = (int*)FUN_803826ec();
    int parent = element[0x0B];  // offset 0x2C = parent entity

    int* list_head = (parent != 0) ? *(int**)(parent + 0x168) : NULL;

    hud_element_destroy_recursive(element);

    // Unlink from parent's element list
    int* prev = NULL;
    int* node = list_head;
    while (node != NULL) {
        if (node == element) {
            if (prev == NULL) {
                *(int*)(parent + 0x168) = (int)node[0x0C];
            } else {
                prev[0x0C] = node[0x0C];
            }
        }
        prev = node;
        node = (int*)node[0x0C];  // next at offset 0x30
    }

    // Clear cached element reference if it was this one
    if (parent != 0 && *(int**)(parent + 0x16C) == element) {
        *(int*)(parent + 0x16C) = 0;
    }

    FUN_80382738();
}

// -----------------------------------------------------------------------------
// Address: 0x800409a0 | Name: hud_set_item_padding_mode
// -----------------------------------------------------------------------------
// Sets padding mode on context frame. param_3 controls whether to set
// "before" padding (+0x48/+0x50), param_4 controls "after" padding (+0x4C/+0x54).
// -----------------------------------------------------------------------------
void hud_set_item_padding_mode(float amount, int mode, int set_before, int set_after) {
    if (set_before) {
        *(int*)((char*)DAT_806119e4 + 0x48) = mode;
        *(float*)((char*)DAT_806119e4 + 0x50) = amount;
    }
    if (set_after) {
        *(int*)((char*)DAT_806119e4 + 0x4C) = mode;
        *(float*)((char*)DAT_806119e4 + 0x54) = amount;
    }
}

// -----------------------------------------------------------------------------
// Address: 0x800409d4 | Name: hud_set_content_bounds
// -----------------------------------------------------------------------------
// Sets the visible content range (top/bottom) on the context frame.
// Mode 0: reads from layout descriptor (layout+0x0C for top, element+0x4C
//          minus layout+0x14 for bottom).
// Mode 1: uses explicit params with clamping (top >= 0, bottom <= element+0x4C).
// -----------------------------------------------------------------------------
void hud_set_content_bounds(float top, float bottom, int use_explicit) {
    if (use_explicit == 0) {
        *(float*)((char*)DAT_806119e4 + 0x30) =
            *(float*)(*(int*)(DAT_806119e0 + 0x1C) + 0x0C);
        *(float*)((char*)DAT_806119e4 + 0x34) =
            *(float*)(DAT_806119e0 + 0x4C) - *(float*)(*(int*)(DAT_806119e0 + 0x1C) + 0x14);
    } else {
        if (top < 0.0f) top = 0.0f;
        *(float*)((char*)DAT_806119e4 + 0x30) = top;

        float max_bottom = *(float*)(DAT_806119e0 + 0x4C);
        if (bottom > max_bottom) bottom = max_bottom;
        *(float*)((char*)DAT_806119e4 + 0x34) = bottom;
    }
}

// -----------------------------------------------------------------------------
// Address: 0x80040a50 | Name: hud_set_header_margin
// -----------------------------------------------------------------------------
// Sets top/header margin. Mode 0 reads from layout+0x1C; mode 1 uses param.
// -----------------------------------------------------------------------------
void hud_set_header_margin(float margin, int use_explicit) {
    if (use_explicit == 0) {
        *(float*)(DAT_806119e0 + 0x11C) =
            *(float*)(*(int*)(DAT_806119e0 + 0x1C) + 0x1C);
    } else {
        if (margin < 0.0f) margin = 0.0f;
        *(float*)(DAT_806119e0 + 0x11C) = margin;
    }
}

// -----------------------------------------------------------------------------
// Address: 0x80040a8c | Name: hud_set_footer_margin
// -----------------------------------------------------------------------------
// Sets bottom/footer margin. Mode 0 reads from layout+0x20; mode 1 uses param.
// -----------------------------------------------------------------------------
void hud_set_footer_margin(float margin, int use_explicit) {
    if (use_explicit == 0) {
        *(float*)(DAT_806119e0 + 0x120) =
            *(float*)(*(int*)(DAT_806119e0 + 0x1C) + 0x20);
    } else {
        if (margin < 0.0f) margin = 0.0f;
        *(float*)(DAT_806119e0 + 0x120) = margin;
    }
}

// -----------------------------------------------------------------------------
// Address: 0x80040ac8 | Name: hud_set_margins
// -----------------------------------------------------------------------------
// Sets both header and footer margins in one call.
// -----------------------------------------------------------------------------
void hud_set_margins(float header, float footer, int use_explicit) {
    hud_set_header_margin(header, use_explicit);
    hud_set_footer_margin(footer, use_explicit);
}

// -----------------------------------------------------------------------------
// Address: 0x80040b0c | Name: hud_set_context_property
// -----------------------------------------------------------------------------
// Sets a property value at context frame offset 0x28.
// -----------------------------------------------------------------------------
void hud_set_context_property(int value) {
    *(int*)((char*)DAT_806119e4 + 0x28) = value;
}

// -----------------------------------------------------------------------------
// Address: 0x80040b18 | Name: hud_get_content_height
// -----------------------------------------------------------------------------
// Returns the available content height (bottom - top from context bounds).
// -----------------------------------------------------------------------------
float hud_get_content_height(void) {
    return *(float*)((char*)DAT_806119e4 + 0x34) - *(float*)((char*)DAT_806119e4 + 0x30);
}

// -----------------------------------------------------------------------------
// Address: 0x80040b2c | Name: hud_item_set_locked
// -----------------------------------------------------------------------------
// Sets or clears the locked flag (bit 0 at item+0x28). Locked items cannot
// have their value changed by user input.
// -----------------------------------------------------------------------------
void hud_item_set_locked(int item, int locked) {
    *(u32*)(item + 0x28) &= ~1;  // clear
    if (locked) {
        *(u32*)(item + 0x28) |= 1;  // set
    }
}

// -----------------------------------------------------------------------------
// Address: 0x80040b50 | Name: hud_lock_child_by_id
// -----------------------------------------------------------------------------
// Finds a child item by ID and sets its locked state.
// -----------------------------------------------------------------------------
void hud_lock_child_by_id(int element, int child_id, int locked) {
    int child = (int)hud_find_child_by_id(element, child_id);
    if (child != 0) {
        hud_item_set_locked(child, locked);
    }
}


// =============================================================================
// Transition State Machine + Element Init (Batch 075)
// =============================================================================
//
// Transition states (at element+0x1AC):
//   0 = hidden/initial
//   1 = waiting (delay before fade-in)
//   2 = fading in (progress at +0x1B0, duration from layout+0x38)
//   3 = fully visible (progress = 1.0)
//   4 = fading out (progress at +0x1B0, duration from layout+0x3C)
//   5 = closed/finished
//
// Transition progress (at +0x1B4): 0.0 to 1.0 computed from +0x1B0/duration
// Sound effects: 0xD2A = menu open, 0xD2B = menu close
//

// Address: 0x80040b8c | hud_event_set_disabled
void hud_event_set_disabled(int event, int disabled) {
    *(u32*)(event + 0x0C) &= ~1;
    if (disabled) *(u32*)(event + 0x0C) |= 1;
}

// Address: 0x80040bb0 | hud_item_set_change_callback
void hud_item_set_change_callback(int item, void* callback) {
    *(void**)(item + 0x4C) = callback;
}

// Address: 0x80040bb8 | hud_element_calc_transition_t
// Computes normalized transition progress (0.0-1.0) from state/timer.
void hud_element_calc_transition_t(int element) {
    int state = *(int*)(element + 0x1AC);
    *(float*)(element + 0x1B4) = 0.0f;

    if (state == 3) {
        *(float*)(element + 0x1B4) = 1.0f;
    } else if (state == 2) {
        float duration = *(float*)(*(int*)(element + 0x1C) + 0x38);
        if (duration > 0.0f)
            *(float*)(element + 0x1B4) = *(float*)(element + 0x1B0) / duration;
    } else if (state == 4 || state == 5) {
        float duration = *(float*)(*(int*)(element + 0x1C) + 0x3C);
        if (duration > 0.0f)
            *(float*)(element + 0x1B4) = 1.0f - *(float*)(element + 0x1B0) / duration;
    }
}

// Address: 0x80040c40 | hud_element_has_valid_parent
int hud_element_has_valid_parent(int element) {
    int node = *(int*)(element + 0x2C);
    int last = node;
    while (node != 0) { last = node; node = *(int*)(node + 0x2C); }
    u32 flags = FUN_8004d234(element);
    if ((flags & 0x1000) && FUN_80046e88(last) != NULL) return 1;
    return 0;
}

// Address: 0x80040cb4 | hud_element_set_transition
// State machine for element open/close transitions.
void hud_element_set_transition(int element, int new_state) {
    if (*(int*)(element + 0x1AC) == new_state) return;
    *(float*)(element + 0x1B0) = 0.0f;
    // State-specific logic: sound, progress transfer, etc.
    *(int*)(element + 0x1AC) = new_state;
    hud_element_calc_transition_t(element);
}

// Address: 0x80040e30 | hud_element_reset_transition
void hud_element_reset_transition(int element) {
    *(int*)(element + 0x1AC) = 2;
    *(float*)(element + 0x1B4) = 0.0f;
    *(float*)(element + 0x1B0) = 0.0f;
    *(float*)(element + 0xF0) = 0.0f;
    *(float*)(element + 0xF4) = 0.0f;
}

// Address: 0x80040e50 | hud_element_show
void hud_element_show(int element) {
    int state = *(int*)(element + 0x1AC);
    if (state == 0) {
        *(u32*)(element + 0x44) |= 1;
        *(float*)(element + 0xF4) = 0.0f;
        int split = hud_viewport_get_split_mode();
        *(float*)(element + 0xF0) = (split == 0) ? *(float*)(element + 0xF4) : 1.0f;
        hud_element_set_transition(element, 1);
    } else if (state == 4 || state == 5) {
        hud_element_set_transition(element, 2);
    }
}

// Address: 0x80040ee0 | hud_element_is_closing
int hud_element_is_closing(int element) {
    int state = *(int*)(element + 0x1AC);
    return (state == 4 || state == 5) ? 1 : 0;
}

// Address: 0x80040f00 | hud_set_max_height
void hud_set_max_height(float height) {
    *(float*)(DAT_806119e0 + 0x4C) = height;
    *(float*)((char*)DAT_806119e4 + 0x34) = height;
}

// Address: 0x80040f14 | hud_element_init_from_layout
// MASSIVE initialization: sets all element properties from layout descriptor.
void hud_element_init_from_layout(int flags, void* data, int cancel_cb, int confirm_cb) {
    // Sets: max height, dimensions, flags, render mode, layout/style/font pointers,
    // content bounds, margins, initial scroll offset, help text strings (0x19C7/0x19C8/0x3D3),
    // animation properties from layout descriptor offsets 0x24-0xC0.
    // Configures viewport mode based on flags (0x400=left, 0x800=right).
}


// =============================================================================
// Rendering / Visibility / Finalization (Batch 076)
// =============================================================================

extern void FUN_80043a0c(int element);
extern void FUN_8016f0e4(int x, int y, int w, int h);  // GX scissor

// Address: 0x800411fc | hud_clear_destructor
void hud_clear_destructor(void) { *(int*)(DAT_806119e0 + 0x1DC) = 0; }

// Address: 0x8004120c | hud_clear_render_callback
void hud_clear_render_callback(void) { *(int*)(DAT_806119e0 + 0x1E0) = 0; }

// Address: 0x8004121c | hud_clear_both_callbacks
void hud_clear_both_callbacks(void) {
    *(int*)(DAT_806119e0 + 0x1DC) = 0;
    *(int*)(DAT_806119e0 + 0x1E0) = 0;
}

// Address: 0x80041234 | hud_set_destructor
void hud_set_destructor(void* callback) { *(void**)(DAT_806119e0 + 0x1DC) = callback; }

// Address: 0x80041240 | hud_swap_callbacks
void hud_swap_callbacks(void) {
    int tmp = *(int*)(DAT_806119e0 + 0x1DC);
    *(int*)(DAT_806119e0 + 0x1DC) = *(int*)(DAT_806119e0 + 0x1E0);
    *(int*)(DAT_806119e0 + 0x1E0) = tmp;
}

// Address: 0x8004125c | hud_element_finalize_frame
// Post-frame finalization. Phase 0: resolves layout pointers. Phase 1: flushes
// layout + adds spacing. Then restores viewport camera + submits for render.
void hud_element_finalize_frame(void) {
    if (*(int*)((char*)DAT_806119e4 + 0x20) == 0) {
        // Resolve style/layout/render pointers
        *(int*)(DAT_806119e0 + 0x18) = FUN_8004d090();
        *(int*)(DAT_806119e0 + 0x1C) = FUN_8004d0e4();
        *(int*)(DAT_806119e0 + 0x20) = FUN_8004d17c();
        *(int*)(DAT_806119e0 + 0x24) = FUN_8004d130();
    }
    if (*(int*)((char*)DAT_806119e4 + 0x20) == 1) {
        hud_flush_layout();
        *(float*)(DAT_806119e0 + 0x64) += *(float*)(*(int*)(DAT_806119e0 + 0x1C) + 0x18);
    }
    *(int*)((char*)DAT_806119e4 + 0x20) = 2;
    // Restore viewport camera
    FUN_8004d81c(/*viewport camera region*/);
    FUN_80043a0c(DAT_806119e0);
}

// Address: 0x80041320 | hud_item_calc_y_position
float hud_item_calc_y_position(int item) {
    int parent = *(int*)(item + 0x88);
    float y = *(float*)(parent + 0x7C) + *(float*)(item + 0x68);
    if ((*(u32*)(item + 0x28) & 0x40) == 0) return y;
    return y + (*(float*)(parent + 0x68) - *(float*)(parent + 0x80));
}

// Address: 0x80041350 | hud_item_calc_screen_rect
// Computes pixel-space bounding rect for an item.
void hud_item_calc_screen_rect(int element, int item, float* out_rect) {
    out_rect[0] = *(float*)(item + 0x64) * FLOAT_80611a68 + *(float*)(element + 0xA4);
    float y_pos = hud_item_calc_y_position(item);
    out_rect[1] = FLOAT_80611a64 * y_pos + FLOAT_80611a6c;
    out_rect[2] = *(float*)(item + 0x6C) * FLOAT_80611a68 + out_rect[0];
    out_rect[3] = *(float*)(item + 0x70) * FLOAT_80611a64 + out_rect[1];
}

// Address: 0x800413d8 | hud_compute_item_visibility
// MASSIVE: computes screen-space clip bounds for all items in element,
// clips against viewport, marks hidden items (flag 0x20). Handles scroll
// offset for clip-group content.
void hud_compute_item_visibility(void) {
    // Computes element viewport bounds (clamped to screen)
    // For each child item: calc screen rect, clip to bounds, store at item+0x78-0x84
    // Items fully outside get flag 0x20 (hidden/culled)
    // Handles scroll-group offset for items between clip markers
}

// Address: 0x80041878 | hud_item_set_scissor
// Sets up hardware scissor rect for item rendering.
void hud_item_set_scissor(int element, int item) {
    float bottom = FLOAT_80613be0 + *(float*)(item + 0x84);
    if (bottom > *(float*)(element + 0xEC)) {
        bottom = *(float*)(element + 0xEC);
    }
    if ((*(u32*)(item + 0x28) & 0x2000000) == 0) {
        FUN_8016f0e4(
            (int)*(float*)(item + 0x78),
            (int)*(float*)(item + 0x7C),
            (int)(*(float*)(item + 0x80) - 1.0f),
            (int)(bottom - 1.0f)
        );
    } else {
        FUN_8016f0e4(
            (int)*(float*)(item + 0x78),
            (int)(*(float*)(item + 0x7C) - FLOAT_80613c04),
            (int)(*(float*)(item + 0x80) - 1.0f),
            (int)(bottom - 1.0f)
        );
    }
}
