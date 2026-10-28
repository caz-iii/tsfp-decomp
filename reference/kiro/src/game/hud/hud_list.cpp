// =============================================================================
// hud_list.cpp - HUD list item creation and management
// TimeSplitters: Future Perfect (GameCube) Decompilation
// Batch 067 | Date: 2026-08-05
// =============================================================================
//
// This file implements the list item creation system for HUD menus.
// Items are allocated from the dynamic pool (stride 0x10C) and linked into
// parent elements via a singly-linked child list (next at +4).
//
// Item types:
//   4 = standard list item
//   7 = selectable list item
//   8 = header/separator item
//
// Sub-lists allow horizontal grouping within a vertical list. Items within
// a sub-list are linked via +1/+3 (doubly-linked within group).
//
// Pool item layout (key offsets):
//   [0]  = type (4/7/8)
//   [1]  = next sibling in sub-list
//   [2]  = prev sibling in sub-list
//   [3]  = sub-list next
//   [4]  = child count (for group headers)
//   [5]  = parent index (-1 if top-level)
//   [6]  = tag (-1 = no tag)
//   [7]  = height/dimension
//   [10] = flags
//   [0xB] = state flags
//   [0x17] = layout rect top
//   [0x18] = layout rect bottom
//   [0x1B] = computed size
//   [0x1D] = margin/padding
//   [0x22] = owner element
//   [0x24] = layout descriptor reference
//   [0x2D] = render callback
//   [0x30] = sort order
//   [0x32] = selection state
//   [0x34] = focus target (-1 = none)
//   [0x3C] = timing/offset value
//   [0x42] = user state
//
// =============================================================================

#include "common.h"

// State
extern int* DAT_806119d4;   // current sub-list head
extern int  DAT_806119d8;   // last item pointer
extern int  DAT_806119dc;   // most recently created item
extern int  DAT_806119d0;   // item counter
extern int  DAT_806119cc;   // clip group context
extern int  DAT_806119e0;   // current element
extern int* DAT_806119e4;   // current context frame

extern float FLOAT_80613bb0;  // 0.0f

// Forward declarations
extern void FUN_8003f41c(void);
extern void FUN_8003ec2c(float width, float height);
extern void FUN_803826f0(void);
extern void FUN_803826e8(void);
extern void FUN_8038273c(void);
extern void FUN_80382734(void);
extern void FUN_8003fed8(void);

// From hud_viewport.cpp
extern int  hud_element_get_dimension(int element, int flags);
extern int* hud_pool_alloc_element(void);

// -----------------------------------------------------------------------------
// Address: 0x8003e60c | Name: hud_get_last_created_item
// -----------------------------------------------------------------------------
int* hud_get_last_created_item(void) {
    return (int*)DAT_806119dc;
}

// -----------------------------------------------------------------------------
// Address: 0x8003e614 | Name: hud_get_item_counter
// -----------------------------------------------------------------------------
int hud_get_item_counter(void) {
    return DAT_806119d0;
}

// -----------------------------------------------------------------------------
// Address: 0x8003e61c | Name: hud_item_init_layout
// -----------------------------------------------------------------------------
// Initializes pool item's layout: sort order from parent layout+4,
// alignment flags (0x4000=left/0, 0x8000=center/1, 0x10000=right/2),
// timing value from parent element.
// -----------------------------------------------------------------------------
void hud_item_init_layout(int* item) {
    // Get sort order from parent's layout descriptor
    item[0x0C] = *(int*)(*(int*)(DAT_806119e0 + 0x1C) + 4);  // offset 0x30

    // Check alignment override flags
    u32 flags = (u32)item[0x0A];  // offset 0x28
    if (flags & 0x1C000) {
        if (flags & 0x4000) {
            item[0x0C] = 0;       // left align
        } else if (flags & 0x8000) {
            item[0x0C] = 1;       // center align
        } else if (flags & 0x10000) {
            item[0x0C] = 2;       // right align
        }
        // Clear alignment flags
        item[0x0A] = (int)((u32)item[0x0A] & 0xFFFE3FFF);
    }

    // Copy timing value from parent
    item[0x0F] = *(int*)(DAT_806119e0 + 0x11C);  // offset 0x3C

    // For center-aligned items, adjust timing by offset
    if (item[0x0C] == 1) {
        *(float*)&item[0x0F] = *(float*)&item[0x1B] - *(float*)(DAT_806119e0 + 0x120);
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003e6ac | Name: hud_element_find_last_child
// -----------------------------------------------------------------------------
int* hud_element_find_last_child(int* element) {
    int* child = *(int**)(element + 0x174 / 4);
    while (*(int*)((char*)child + 4) != 0) {
        child = (int*)*(int*)((char*)child + 4);
    }
    return child;
}

// -----------------------------------------------------------------------------
// Address: 0x8003e6c8 | Name: hud_item_set_tag
// -----------------------------------------------------------------------------
void hud_item_set_tag(int* item, int tag) {
    *(int*)((char*)item + 0x18) = tag;
}

// -----------------------------------------------------------------------------
// Address: 0x8003e6d0 | Name: hud_set_last_child_tag
// -----------------------------------------------------------------------------
void hud_set_last_child_tag(int tag) {
    if (DAT_806119e0 != 0) {
        int* last = hud_element_find_last_child((int*)DAT_806119e0);
        hud_item_set_tag(last, tag);
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003e714 | Name: hud_item_create
// -----------------------------------------------------------------------------
// Main list item creation function. Allocates from pool, configures type,
// links into parent's child list, sets dimensions and layout properties.
// Handles sub-list mode (horizontal grouping), scroll flags, alignment,
// and clip group membership.
// -----------------------------------------------------------------------------
void hud_item_create(int type, float width, float height) {
    u64 result = FUN_803826f0();
    int item_type = (int)(result >> 32);
    int item_flags = (int)result;

    // If type is a list item (4/7/8) and parent has overflow flag
    if (item_type == 4 || item_type == 7 || item_type == 8) {
        if (*(u32*)(DAT_806119e0 + 0x40) & 0x4000) {
            // Check if we need to trigger overflow handling
            int* child = *(int**)(DAT_806119e0 + 0x174);
            while (child != NULL) {
                int ct = child[0];
                if ((ct == 4 || ct == 7 || ct == 8) &&
                    (*(int*)((char*)DAT_806119e4 + 0x14) == 0 || DAT_806119d4 == NULL)) {
                    FUN_8003f41c();
                    break;
                }
                child = (int*)child[1];
            }
        }
        *(int*)((char*)DAT_806119e4 + 0x44) += 1;
    }

    // Allocate from pool
    int* item = hud_pool_alloc_element();
    *(int*)(DAT_806119e0 + 0x178) += 1;  // increment parent's child count
    item[0] = item_type;

    // Track visible item count
    if (*(int*)((char*)DAT_806119e4 + 0x14) == 0) {
        if (item[0] == 4 || item[0] == 7 || item[0] == 8) {
            *(int*)(DAT_806119e0 + 0x17C) += 1;
        }
    }

    // Link to clip group if active
    if (DAT_806119cc != 0 && *(int*)(DAT_806119cc + 0x54) == 0) {
        *(int**)(DAT_806119cc + 0x54) = item;
    }

    // Update last-created tracking
    DAT_806119d8 = (int)item;
    DAT_806119dc = (int)item;

    // Initialize item fields
    item[6] = -1;                    // tag = none
    item[0x22] = DAT_806119e0;       // owner element
    item[0x24] = *(int*)(DAT_806119e0 + 0x20);  // layout ref
    item[0x0A] = item_flags;         // flags from params
    item[0x0B] |= 2;                 // mark as active

    // Get dimension from parent
    int dim = hud_element_get_dimension(DAT_806119e0, item[0x0A]);
    item[7] = dim;                   // height

    item[0x32] = 0;                  // selection state
    item[0x34] = -1;                 // focus target
    item[0x2D] = 0;                  // render callback
    item[0x17] = *(int*)((char*)DAT_806119e4 + 0x30);  // layout rect top
    item[0x18] = *(int*)((char*)DAT_806119e4 + 0x34);  // layout rect bottom

    // Propagate scroll flag
    if (*(int*)((char*)DAT_806119e4 + 0x38) != 0) {
        item[0x0A] |= 0x40;
    }

    // Link into list
    if (DAT_806119d4 == NULL) {
        // Top-level insertion (vertical list)
        item[4] = 1;  // child count = 1

        int* first_child = *(int**)(DAT_806119e0 + 0x174);
        if (first_child == NULL) {
            *(int**)(DAT_806119e0 + 0x174) = item;
        } else {
            // Walk to end and append
            int* tail = first_child;
            int* last_with_next = first_child;
            while (*(int*)((char*)tail + 4) != 0) {
                tail = (int*)*(int*)((char*)tail + 4);
                if (*(int*)((char*)tail + 0x10) != 0) {
                    last_with_next = tail;
                }
            }
            *(int*)((char*)tail + 4) = (int)item;
            *(int*)((char*)last_with_next + 8) = (int)item;
        }

        if (*(int*)((char*)DAT_806119e4 + 0x14) == 0) {
            // Full-width item
            item[0x0A] |= 0xC0000;
            item[0x1B] = (int)((float)item[0x18] - (float)item[0x17]);
            item[0x2C] = 1;
        } else {
            // Sub-list header
            DAT_806119d4 = item;
            item[0x0B] |= 0x10;
            item[0x1B] = (int)width;
        }
        item[0x1D] = (int)height;
    } else {
        // Sub-list insertion (horizontal within group)
        int* tail = DAT_806119d4;
        while ((int*)tail[3] != NULL) {
            tail = (int*)tail[3];
        }

        DAT_806119d4[4] += 1;  // increment group child count
        item[1] = tail[1];     // next sibling
        item[2] = tail[2];     // prev sibling
        tail[1] = (int)item;   // link prev to new
        tail[3] = (int)item;   // sub-list next

        if (width == 0.0f) {
            item[0x1B] = 0;
        } else {
            item[0x1B] = (int)width;
        }
        item[0x1D] = (int)height;
        item[0x0B] |= 0x10;
    }

    // Clear overflow flags if in clip group
    if (DAT_806119cc != 0) {
        item[0x0A] &= 0xFFF3FFFF;
    }

    // Handle size-from-layout flag
    if (item[0x0A] & 0x800) {
        item[0x11] = (int)((float)item[0x1B] - *(float*)(DAT_806119e0 + 0x120)
                           - *(float*)(DAT_806119e0 + 0x11C));
    }

    hud_item_init_layout(item);

    // Register with clip group
    if (*(int*)((char*)DAT_806119e4 + 0x10) != 0) {
        *(int**)(DAT_806119e0 + 0x1E8) = item;
        *(int*)((char*)DAT_806119e4 + 0x10) = 0;
    }

    item[0x42] = 0;  // clear user state

    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// Address: 0x8003ea8c | Name: hud_add_list_item
// -----------------------------------------------------------------------------
// Adds a list item with default height to the current element.
// -----------------------------------------------------------------------------
void hud_add_list_item(void) {
    float height = (float)FUN_803826e8();
    FUN_8003fed8();
    FUN_8003ec2c(0.0f, height);
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// Address: 0x8003eb04 | Name: hud_add_list_item_sized
// -----------------------------------------------------------------------------
// Adds a list item with a specified height.
// -----------------------------------------------------------------------------
void hud_add_list_item_sized(float height) {
    FUN_8003ec2c(0.0f, height);
}

// -----------------------------------------------------------------------------
// Address: 0x8003eb2c | Name: hud_begin_sub_list
// -----------------------------------------------------------------------------
// Begins a horizontal sub-list group within the current vertical list.
// Items added after this call will be linked horizontally until the
// group is ended. Stores group type and parameters.
// -----------------------------------------------------------------------------
void hud_begin_sub_list(int type, int param_a, int param_b) {
    u64 result = FUN_803826f0();
    FUN_8003fed8();

    DAT_806119d4 = 0;  // clear sub-list head
    *(int*)((char*)DAT_806119e4 + 0x14) = 1;  // set group mode
    *(int*)((char*)DAT_806119e4 + 0x18) = (int)(result >> 32);  // group type
    *(int*)((char*)DAT_806119e4 + 0x1C) = (int)result;          // group data
    *(int*)((char*)DAT_806119e4 + 0x24) = param_b;              // group param

    FUN_8038273c();
}


// =============================================================================
// List Item Addition / Scroll Regions / Utilities (Batch 068)
// =============================================================================

extern void FUN_800409d4(float x, float y, int mode);
extern int  FUN_8004d130(void);

// -----------------------------------------------------------------------------
// Address: 0x8003eb94 | Name: hud_end_sub_list
// -----------------------------------------------------------------------------
// Ends a horizontal sub-list group. Clears group mode flag, marks last item
// as group tail (offset 0xB0=1), increments visible child count.
// -----------------------------------------------------------------------------
void hud_end_sub_list(void) {
    *(int*)((char*)DAT_806119e4 + 0x14) = 0;   // clear group mode
    *(int*)(DAT_806119dc + 0xB0) = 1;           // mark last as tail
    FUN_8003fed8();
    DAT_806119d4 = 0;                           // clear sub-list head
    *(int*)(DAT_806119e0 + 0x17C) += 1;         // increment visible count
}

// -----------------------------------------------------------------------------
// Address: 0x8003ebe4 | Name: hud_text_apply_case_scale
// -----------------------------------------------------------------------------
// Applies text transformations: uppercases if flag 0x2000 is set, then
// applies font scale.
// -----------------------------------------------------------------------------
void hud_text_apply_case_scale(float scale, char* text, u32 flags) {
    if (flags & 0x2000) {
        text = (char*)text_string_to_upper((u32)text);
    }
    text_check_scale(scale, text);
}

// -----------------------------------------------------------------------------
// Address: 0x8003ec2c | Name: hud_add_item_internal
// -----------------------------------------------------------------------------
// Core item addition function. Handles sub-list auto-wrap (checks if current
// row has hit capacity and wraps to new row), adjusts offsets for layout modes,
// calls hud_item_create, stores text/callback/sprite data, and sets font table.
// -----------------------------------------------------------------------------
void hud_add_item_internal(float width, float height) {
    u64 result = FUN_803826e8();

    // Adjust height for layout padding modes
    if (*(int*)((char*)DAT_806119e4 + 0x48) == 2 ||
        *(int*)((char*)DAT_806119e4 + 0x48) == 1) {
        height += *(float*)((char*)DAT_806119e4 + 0x50);
    }

    float item_width = (float)(result >> 32);  // from register pair

    // Check sub-list auto-wrap
    if (*(int*)((char*)DAT_806119e4 + 0x14) != 0 &&
        DAT_806119d4 != NULL &&
        *(int*)((char*)DAT_806119e4 + 0x24) != 0 &&
        *(int*)((char*)DAT_806119e4 + 0x24) <= *(int*)((char*)DAT_806119d4 + 0x10)) {
        // Row is full - end current sub-list and start new one
        hud_end_sub_list();
        hud_begin_sub_list(
            *(int*)((char*)DAT_806119e4 + 0x18),
            *(int*)((char*)DAT_806119e4 + 0x1C),
            *(int*)((char*)DAT_806119e4 + 0x24)
        );
    }

    int* item;
    if (*(int*)((char*)DAT_806119e4 + 0x14) == 0 || DAT_806119d4 != NULL) {
        // Normal add: use width from params/register
        if (*(int*)((char*)DAT_806119e4 + 0x14) != 0 &&
            *(int*)((char*)DAT_806119e4 + 0x18) != 0) {
            item_width = *(float*)((char*)DAT_806119d4 + 0x6C);
        }
        item = (int*)hud_item_create(item_width, height);
    } else {
        // First item in new sub-list: adjust layout rect
        float prev = *(float*)((char*)DAT_806119e4 + 0x34);
        *(float*)((char*)DAT_806119e4 + 0x34) =
            *(float*)((char*)DAT_806119e4 + 0x30) + item_width;
        item = (int*)hud_item_create(item_width, height);
        *(float*)((char*)DAT_806119e4 + 0x34) = prev;
    }

    // Store item properties
    item[8] = (int)(result >> 32);  // text string pointer (offset 0x20)
    item[5] = (int)result;          // callback data (offset 0x14)
    // item+0x38 = in_r6 (sprite/render data)
    // item+0x50 = in_r7 (user param)
    item[9] = 0;                    // clear icon (offset 0x24)

    // Set font for this item
    text_set_font_table(*(int*)((char*)item + 0x1C));

    FUN_80382734();
}

// -----------------------------------------------------------------------------
// Address: 0x8003edc8 | Name: hud_item_set_icon
// -----------------------------------------------------------------------------
void hud_item_set_icon(int* item, int icon_id) {
    *(int*)((char*)item + 0x24) = icon_id;
}

// -----------------------------------------------------------------------------
// Address: 0x8003edd0 | Name: hud_item_set_icon_visible
// -----------------------------------------------------------------------------
// Sets icon with visibility flag (bit 1 at offset 0x2C = has_icon).
// -----------------------------------------------------------------------------
void hud_item_set_icon_visible(int* item, int icon_id) {
    *(u32*)((char*)item + 0x2C) |= 1;  // set has_icon flag
    *(int*)((char*)item + 0x24) = icon_id;
}

// -----------------------------------------------------------------------------
// Address: 0x8003ede4 | Name: hud_add_spacer_item
// -----------------------------------------------------------------------------
// Creates a spacer/divider item. If adjust_margins is set, applies parent's
// header/footer offset to top and subtracts both from bottom.
// -----------------------------------------------------------------------------
void hud_add_spacer_item(float height, int adjust_margins, int param) {
    FUN_8003fed8();
    int* item = (int*)hud_item_create(0.0f, height);
    *(int*)((char*)item + 0x50) = param;

    if (adjust_margins != 0) {
        *(float*)((char*)item + 0x64) += *(float*)(DAT_806119e0 + 0x11C);
        *(float*)((char*)item + 0x6C) -=
            (*(float*)(DAT_806119e0 + 0x11C) + *(float*)(DAT_806119e0 + 0x120));
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003ee80 | Name: hud_begin_scroll_region
// -----------------------------------------------------------------------------
// Begins a scrollable sub-region. Creates a clip container item that tracks
// the content bounds. Configures scroll area from parent layout descriptor.
// Sets DAT_806119cc to the container for child items to reference.
// -----------------------------------------------------------------------------
void hud_begin_scroll_region(void) {
    u64 result = FUN_803826e8();
    FUN_8003fed8();

    // Create container item
    DAT_806119cc = (int)hud_item_create(0.0f, 0.0f);
    int* container = (int*)DAT_806119cc;

    container[8] = (int)(result >> 32);  // offset 0x20
    container[5] = (int)result;          // offset 0x14
    // container+0x50 = in_r7

    // Adjust margins if needed (in_r6 check)
    // ... margin adjustment ...

    FUN_8003fed8();

    // Configure scroll bounds from parent layout
    float* layout = *(float**)(DAT_806119e0 + 0x24);
    float scroll_top = *(float*)((char*)DAT_806119e4 + 0x30) + layout[0];
    float scroll_bottom = *(float*)((char*)DAT_806119e4 + 0x34) - layout[2];
    FUN_800409d4(scroll_top, scroll_bottom, 1);

    // Advance time with layout spacing
    hud_element_advance_time(layout[1], DAT_806119e0, DAT_806119cc);

    FUN_80382734();
}

// -----------------------------------------------------------------------------
// Address: 0x8003ef6c | Name: hud_end_scroll_region
// -----------------------------------------------------------------------------
// Ends the scrollable sub-region. Computes total content height from the first
// and last items in the region, stores last item, advances accumulated time,
// resets DAT_806119cc to 0.
// -----------------------------------------------------------------------------
void hud_end_scroll_region(void) {
    FUN_8003fed8();

    int* container = (int*)DAT_806119cc;

    // Store last item reference
    *(int*)((char*)container + 0x58) = DAT_806119d8;

    // Compute total content height: (last.top + last.height) - first.top
    int* last_item = (int*)*(int*)((char*)container + 0x58);
    int* first_item = (int*)*(int*)((char*)container + 0x54);
    float total_h = (*(float*)((char*)last_item + 0x68) + *(float*)((char*)last_item + 0x70))
                    - *(float*)((char*)first_item + 0x68);
    *(float*)((char*)container + 0x70) += total_h;

    // Get spacing from current context
    int ctx = FUN_8004d130();
    hud_element_advance_time(*(float*)(ctx + 0x0C), DAT_806119e0, DAT_806119cc);

    // Clear scroll region context
    DAT_806119cc = 0;
    FUN_800409d4(0.0f, 0.0f, 0);
}

// -----------------------------------------------------------------------------
// Address: 0x8003eff4 | Name: hud_begin_scroll_with_item
// -----------------------------------------------------------------------------
// Convenience: begins a scroll region and adds the first list item.
// -----------------------------------------------------------------------------
void hud_begin_scroll_with_item(void) {
    hud_begin_scroll_region();
    hud_add_list_item();
}

// -----------------------------------------------------------------------------
// Address: 0x8003f04c | Name: hud_item_desc_init
// -----------------------------------------------------------------------------
// Initializes a 0x14-byte item descriptor struct. Sets type=6 (callback item),
// clears all fields, sets parent reference to -1.
// -----------------------------------------------------------------------------
void hud_item_desc_init(int* desc) {
    memset_wrapper((int)desc, 0, 0x14);
    desc[0] = 6;     // type = callback
    desc[2] = -1;    // parent = none
}


// =============================================================================
// Event Descriptors / Padding / Callback Items (Batch 069)
// =============================================================================
//
// Event descriptors (type=6, stride 0x14, 1500-entry pool at DAT_8051dbf4)
// represent menu actions/buttons. They are linked into an element's event
// chain at element+0xD8 (singly-linked via +0x10).
//
// Callback items have a render function pointer at item+0x50, looked up
// from a table in the layout descriptor at (layout + index*4 + 0xB4).
//
// =============================================================================

extern void FUN_80047e34(void);
extern void FUN_80042cac(void);  // default item render callback

// Event pool (1500 entries, stride 0x14)
extern u8 g_event_pool[];  // DAT_8051dbf4

// -----------------------------------------------------------------------------
// Address: 0x8003f090 | Name: hud_event_alloc
// -----------------------------------------------------------------------------
// Allocates an event descriptor from the 1500-entry pool. Walks the element's
// event chain (at +0xD8, linked via +0x10) to find the tail, scans for an
// empty slot, initializes it via hud_item_desc_init, and links at the end.
// Increments element's event count (at +0xC4).
// Returns: pointer to the new event descriptor.
// -----------------------------------------------------------------------------
int* hud_event_alloc(void) {
    int element = FUN_803826f0();

    // Walk to tail of event chain
    int* tail = NULL;
    int* node = *(int**)(element + 0xD8);
    while (node != NULL) {
        tail = node;
        node = (int*)*(int*)((char*)node + 0x10);
    }

    // Find empty slot in pool
    for (int i = 0; i < 1500; i++) {
        int* slot = (int*)&g_event_pool[i * 0x14];
        if (*slot == 0) {
            hud_item_desc_init(slot);
            DAT_806119d0 = (int)slot;

            // Link to chain
            if (tail == NULL) {
                *(int**)(element + 0xD8) = slot;
            } else {
                *(int*)((char*)tail + 0x10) = (int)slot;
            }

            *(int*)(element + 0xC4) += 1;
            break;
        }
    }

    FUN_8038273c();
    return (int*)DAT_806119d0;
}

// -----------------------------------------------------------------------------
// Address: 0x8003f144 | Name: hud_event_add_string
// -----------------------------------------------------------------------------
// Creates an event descriptor and stores a string/label at event+4.
// Calls FUN_80047e34 for button configuration. Returns event pointer.
// -----------------------------------------------------------------------------
int* hud_event_add_string(int string_ptr) {
    int* event = hud_event_alloc();
    *(int*)((char*)event + 4) = string_ptr;
    FUN_80047e34();
    return event;
}

// -----------------------------------------------------------------------------
// Address: 0x8003f18c | Name: hud_event_add_pair
// -----------------------------------------------------------------------------
// Creates an event descriptor with two 32-bit values at event+4 and event+8.
// Used for callback+data pairs.
// -----------------------------------------------------------------------------
void hud_event_add_pair(void) {
    u64 result = FUN_803826f0();
    int* event = hud_event_alloc();
    *(int*)((char*)event + 4) = (int)(result >> 32);
    *(int*)((char*)event + 8) = (int)result;
    FUN_80047e34();
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// Address: 0x8003f1dc | Name: hud_event_add_ok_cancel
// -----------------------------------------------------------------------------
// Adds standard OK (string ID 0x17DC) and Cancel (string ID 0x17DB) event
// descriptors. Common for confirmation dialogs.
// -----------------------------------------------------------------------------
void hud_event_add_ok_cancel(void) {
    int ok_str = get_string_by_id(0x17DC);
    hud_event_add_string(ok_str);

    int cancel_str = get_string_by_id(0x17DB);
    hud_event_add_string(cancel_str);
}

// -----------------------------------------------------------------------------
// Address: 0x8003f210 | Name: hud_add_padding_item
// -----------------------------------------------------------------------------
// Creates a zero-width padding/spacing item with specified height.
// Used for vertical spacing between list items.
// -----------------------------------------------------------------------------
void hud_add_padding_item(float height) {
    FUN_8003fed8();
    hud_item_create(0.0f, height);
}

// -----------------------------------------------------------------------------
// Address: 0x8003f258 | Name: hud_add_default_padding
// -----------------------------------------------------------------------------
// Creates a padding item using the default height from layout+0x2C.
// -----------------------------------------------------------------------------
void hud_add_default_padding(void) {
    float height = *(float*)(*(int*)(DAT_806119e0 + 0x1C) + 0x2C);
    hud_add_padding_item(height);
}

// -----------------------------------------------------------------------------
// Address: 0x8003f284 | Name: hud_add_padding_item_alt
// -----------------------------------------------------------------------------
// Alternate entry point for padding item creation. Identical behavior to
// hud_add_padding_item.
// -----------------------------------------------------------------------------
void hud_add_padding_item_alt(float height) {
    FUN_8003fed8();
    hud_item_create(0.0f, height);
}

// -----------------------------------------------------------------------------
// Address: 0x8003f2cc | Name: hud_add_callback_item
// -----------------------------------------------------------------------------
// Creates a list item with a render callback. Looks up the callback from
// the layout's function table: layout + layout[0xA0]*4 + 0xB4.
// If the table entry is NULL, uses FUN_80042cac as default renderer.
// -----------------------------------------------------------------------------
int* hud_add_callback_item(float height) {
    FUN_8003fed8();
    int* item = (int*)hud_item_create(0.0f, height);

    int* layout = *(int**)(DAT_806119e0 + 0x1C);
    int index = *(int*)((char*)layout + 0xA0);
    int callback = *(int*)((char*)layout + index * 4 + 0xB4);

    if (callback == 0) {
        *(void**)((char*)item + 0x50) = (void*)FUN_80042cac;
    } else {
        FUN_8003fed8();
        *(int*)((char*)item + 0x50) = callback;
    }

    return item;
}

// -----------------------------------------------------------------------------
// Address: 0x8003f374 | Name: hud_add_callback_item_indexed
// -----------------------------------------------------------------------------
// Same as hud_add_callback_item but with explicit table index parameter.
// Callback at: layout + param_2*4 + 0xB4.
// -----------------------------------------------------------------------------
int* hud_add_callback_item_indexed(float height, int index) {
    FUN_8003fed8();
    int* item = (int*)hud_item_create(0.0f, height);

    int* layout = *(int**)(DAT_806119e0 + 0x1C);
    int callback = *(int*)((char*)layout + index * 4 + 0xB4);

    if (callback == 0) {
        *(void**)((char*)item + 0x50) = (void*)FUN_80042cac;
    } else {
        FUN_8003fed8();
        *(int*)((char*)item + 0x50) = callback;
    }

    return item;
}

// -----------------------------------------------------------------------------
// Address: 0x8003f41c | Name: hud_add_default_callback_item
// -----------------------------------------------------------------------------
// Creates a callback item using height from layout+0x30 and the auto-selected
// render callback (from current layout index at +0xA0).
// -----------------------------------------------------------------------------
void hud_add_default_callback_item(void) {
    float height = *(float*)(*(int*)(DAT_806119e0 + 0x1C) + 0x30);
    hud_add_callback_item(height);
}
