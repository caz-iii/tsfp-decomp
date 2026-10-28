// =============================================================================
// HUD Screen/Page Management System
// Manages screen stacks, viewport rendering, element lookup, and page lifecycle.
// Source: tsfp-decomp | Batch 083
// Date: 2026-08-10
// =============================================================================

#include "common.h"
#include "hud.h"

// Globals
static int g_active_viewport;           // 0x806119f8
static int g_screen_state_ptr;          // 0x806119e4 - screen rebuild state block
static int g_screen_item_buffer;        // 0x806119f0 - item snapshot ring buffer
static int g_screen_item_buffer_size;   // 0x806119c8 - max entries in ring buffer
static int g_screen_stack_index;        // 0x806119ec - current screen stack index
static float g_scroll_zero;             // 0x80613bb0 - constant 0.0f

// Screen stack table at 0x8051a820, stride 0x1E0 per entry

// =============================================================================
// Address: 0x80046908 | hud_screen_find_child_by_id
// Walks the child linked list (offset +0x168) to find element with matching ID
// at offset +0x4. Returns NULL if not found.
// =============================================================================
static int hud_screen_find_child_by_id(int screen, int child_id)
{
    int child = *(int*)(screen + 0x168);

    while (child != 0) {
        if (*(int*)(child + 4) == child_id) {
            return child;
        }
        child = *(int*)(child + 0x30);
    }

    return 0;
}

// =============================================================================
// Address: 0x800468d0 | hud_screen_show_by_id
// Finds a child element by ID and links+shows it on the parent screen.
// =============================================================================
void hud_screen_show_by_id(int screen, int child_id)
{
    int child = hud_screen_find_child_by_id(screen, child_id);
    hud_element_link_and_show(screen, child);
}

// =============================================================================
// Address: 0x80046930 | hud_screen_reset_scroll
// Resets scroll position (current and target) to zero for a screen element.
// Offsets +0x80 = scroll_current, +0x84 = scroll_target, +0xBC = scroll_max
// =============================================================================
void hud_screen_reset_scroll(int screen)
{
    float zero = g_scroll_zero;
    *(float*)(screen + 0x80) = zero;
    *(float*)(screen + 0xBC) = zero;
    *(float*)(screen + 0x84) = zero;
}

// =============================================================================
// Address: 0x80046944 | hud_screen_set_active_viewport
// Sets the global active viewport index for rendering.
// =============================================================================
void hud_screen_set_active_viewport(int viewport_id)
{
    g_active_viewport = viewport_id;
}

// =============================================================================
// Address: 0x8004694c | hud_screen_get_active_viewport
// Returns the current active viewport index.
// =============================================================================
int hud_screen_get_active_viewport(void)
{
    return g_active_viewport;
}

// =============================================================================
// Address: 0x80046954 | hud_screen_render_viewport
// Renders a specific viewport: sets active viewport, renders all elements,
// then renders the overlay layer (button prompts, cursor).
// =============================================================================
void hud_screen_render_viewport(int screen, int viewport_id)
{
    hud_screen_set_active_viewport(viewport_id);
    hud_element_render(screen, viewport_id, 0);
    hud_element_render_overlay();
}

// =============================================================================
// Address: 0x800469ac | hud_screen_rebuild_items
// MASSIVE: Snapshots all current items (IDs, values, focus state, scroll pos),
// tears down the screen, re-invokes the build callback, then restores item
// values and focus from the snapshot. Used when screen content changes
// dynamically (e.g. option toggled that adds/removes items).
//
// Snapshot format per item (0x18 bytes):
//   +0x00: item ID (from +0x14)
//   +0x04: occurrence index (nth child with same ID, or unnamed index)
//   +0x08: value count (from +0xC8)
//   +0x0C: value/callback ptr (from +0xD0 or callback)
//   +0x10: secondary value (from +0xD4)
//   +0x14: user tag (from +0x34)
// =============================================================================
void hud_screen_rebuild_items(void)
{
    int screen;
    int child;
    int unnamed_index;
    int item_count;
    int* snapshot_entry;
    int restored_item;
    float scroll_current;
    float scroll_target;
    float max_scroll;
    int focus_id;
    int focus_occurrence;
    int content_version;
    int i;
    BOOL has_callback;

    screen = hud_element_get_current();  // FUN_803826ec - gets current stack top
    unnamed_index = 0;

    hud_element_push(screen);

    child = *(int*)(screen + 0x174);  // first child in item list

    // Save screen state before teardown
    *(int*)(g_screen_state_ptr + 4) = 0;
    *(int*)(g_screen_state_ptr + 0x58) = *(int*)(screen + 0x44);    // flags
    *(int*)(g_screen_state_ptr + 0x74) = *(int*)(screen + 0xC0);    // state A
    *(int*)(g_screen_state_ptr + 0x78) = *(int*)(screen + 0xC4);    // state B
    *(int*)(g_screen_state_ptr + 0x5C) = *(int*)(screen + 0x188);   // focused item ID
    *(int*)(g_screen_state_ptr + 0x60) = *(int*)(screen + 0x18C);   // focused occurrence
    *(float*)(g_screen_state_ptr + 0x64) = *(float*)(screen + 0x80); // scroll_current
    *(float*)(g_screen_state_ptr + 0x68) = *(float*)(screen + 0x84); // scroll_target
    *(int*)(g_screen_state_ptr + 0x7C) = *(int*)(screen + 0x218);   // content version

    // Adjust scroll positions relative to parent if nested
    if (*(int*)(screen + 0x194) != 0) {
        int parent = *(int*)(screen + 0x194);
        *(float*)(g_screen_state_ptr + 0x64) -= *(float*)(parent + 0x68);
        *(float*)(g_screen_state_ptr + 0x68) -= *(float*)(parent + 0x68);
    }

    // Snapshot all items into ring buffer
    for (; child != 0; child = *(int*)(child + 4)) {
        int write_idx = *(int*)(g_screen_state_ptr + 0x6C) + *(int*)(g_screen_state_ptr + 0x70);
        snapshot_entry = (int*)(g_screen_item_buffer + write_idx * 0x18);

        has_callback = hud_item_has_callback(child, 1);  // FUN_80047968

        if (has_callback) {
            int cb = hud_item_get_callback(child);  // FUN_800479ec
            snapshot_entry[3] = cb;
        } else if (*(int*)(child + 0xD8) != 0) {
            snapshot_entry[2] = *(int*)(child + 0xC8);  // value count
            snapshot_entry[3] = *(int*)(child + 0xD0);  // value ptr
            snapshot_entry[4] = *(int*)(child + 0xD4);  // secondary
        }

        snapshot_entry[0] = *(int*)(child + 0x14);  // item ID
        snapshot_entry[5] = *(int*)(child + 0x34);  // user tag

        if (*(int*)(child + 0x14) == -1) {
            // Unnamed item: track by sequential index
            snapshot_entry[1] = unnamed_index;
        } else {
            // Named item: count occurrence (nth with same ID)
            int walk = *(int*)(screen + 0x174);
            snapshot_entry[1] = 0;
            for (; walk != 0 && walk != child; walk = *(int*)(walk + 4)) {
                if (*(int*)(walk + 0x14) == *(int*)(child + 0x14)) {
                    snapshot_entry[1]++;
                }
            }
        }

        *(int*)(g_screen_state_ptr + 0x70) += 1;

        if (*(int*)(child + 0x14) == -1) {
            unnamed_index++;
        }
    }

    // Tear down and rebuild
    hud_element_clear_all_items(screen);
    hud_element_init_state(screen, 0);

    // Re-invoke build callback
    if (*(void**)(screen + 8) != NULL) {
        (*(void (*)(int))(*(int*)(screen + 8)))(screen);
        hud_element_finalize_frame();
    }

    // Restore flags
    *(unsigned int*)(screen + 0x44) |= *(unsigned int*)(g_screen_state_ptr + 0x58);
    *(unsigned int*)(screen + 0xC0) |= *(unsigned int*)(g_screen_state_ptr + 0x74);
    *(unsigned int*)(screen + 0xC4) |= *(unsigned int*)(g_screen_state_ptr + 0x78);

    // Restore item values from snapshot
    item_count = *(int*)(g_screen_state_ptr + 0x70);
    for (i = 0; i < item_count; i++) {
        int* entry = (int*)(g_screen_item_buffer + *(int*)(g_screen_state_ptr + 0x6C) * 0x18);

        restored_item = hud_find_nth_child_by_id(screen, entry[0], entry[1]);

        if (restored_item != 0) {
            *(int*)(restored_item + 0x34) = entry[5];  // restore user tag

            if (*(int*)(restored_item + 0xD8) != 0 && entry[2] < *(int*)(restored_item + 0xC4)) {
                has_callback = hud_item_has_callback(restored_item, 0);

                if (has_callback && (void*)entry[3] != NULL) {
                    // Invoke restore callback
                    (*(void (*)(int))(entry[3]))(restored_item);
                } else {
                    // Direct value restore
                    *(int*)(restored_item + 0xD0) = entry[3];
                    *(int*)(restored_item + 0xD4) = entry[4];
                    hud_item_on_value_change();
                }
            }
        }

        *(int*)(g_screen_state_ptr + 0x6C) += 1;
        if (g_screen_item_buffer_size < *(int*)(g_screen_state_ptr + 0x6C)) {
            *(int*)(g_screen_state_ptr + 0x6C) = 0;
        }
    }

    // Restore scroll position
    focus_occurrence = *(int*)(g_screen_state_ptr + 0x60);
    if (focus_occurrence < 0) {
        // No focus - restore raw scroll
        *(float*)(screen + 0x80) = *(float*)(g_screen_state_ptr + 0x64);
        *(float*)(screen + 0x84) = *(float*)(g_screen_state_ptr + 0x68);
    } else if (*(int*)(g_screen_state_ptr + 0x7C) == *(int*)(screen + 0x218)) {
        // Content version unchanged - restore focus by ID
        focus_id = *(int*)(g_screen_state_ptr + 0x5C);
        hud_set_focus_by_id_nth(screen, focus_id, focus_occurrence);

        if (*(int*)(screen + 0x194) != 0) {
            int parent = *(int*)(screen + 0x194);
            *(float*)(screen + 0x80) = *(float*)(g_screen_state_ptr + 0x64) + *(float*)(parent + 0x68);
            *(float*)(screen + 0x84) = *(float*)(g_screen_state_ptr + 0x68) + *(float*)(parent + 0x68);
        }
    }

    // Clamp scroll positions
    max_scroll = *(float*)(screen + 0x58) - (*(float*)(screen + 0x6C) - *(float*)(screen + 0x68));

    if (*(float*)(screen + 0x80) < 0.0f) {
        *(float*)(screen + 0x80) = 0.0f;
    }
    if (*(float*)(screen + 0x84) < 0.0f) {
        *(float*)(screen + 0x84) = 0.0f;
    }
    if (*(float*)(screen + 0x80) > max_scroll) {
        *(float*)(screen + 0x80) = (max_scroll < 0.0f) ? 0.0f : max_scroll;
    }
    if (*(float*)(screen + 0x84) > max_scroll) {
        *(float*)(screen + 0x84) = (max_scroll < 0.0f) ? 0.0f : max_scroll;
    }

    hud_element_pop();
    hud_element_restore_current();  // FUN_80382738
}

// =============================================================================
// Address: 0x80046dd8 | hud_screen_mark_hidden
// Sets the hidden flag (bit 0x20) on a screen element's flags at +0x44.
// =============================================================================
void hud_screen_mark_hidden(int screen)
{
    *(unsigned int*)(screen + 0x44) |= 0x20;
}

// =============================================================================
// Address: 0x80046de8 | hud_screen_get_parent
// Returns the parent screen pointer from +0x16C of the given element.
// =============================================================================
int hud_screen_get_parent(int element)
{
    return *(int*)(element + 0x16C);
}

// =============================================================================
// Address: 0x80046df0 | hud_screen_find_root
// Walks up the screen hierarchy via parent pointers (+0x16C) to find the
// topmost (root) screen. Uses a recursive child-validity check (FUN_80046efc)
// to determine if a node qualifies. Returns the first node whose parent is NULL
// or whose parent fails the validity check.
// =============================================================================
static int hud_screen_find_root(int screen)
{
    int current = screen;
    int parent;
    int valid;

    while (1) {
        if (current == 0) {
            return 0;
        }

        valid = hud_screen_node_is_valid(current);  // FUN_80046efc - recursive validity
        if (valid == 0) {
            break;
        }

        parent = *(int*)(current + 0x16C);
        if (parent == 0) {
            return current;
        }
        current = parent;
    }

    return current;
}

// =============================================================================
// HUD Screen Navigation & Transition System (Batch 084)
// Screen stack push/pop, page transitions, close readiness checks.
// Date: 2026-08-10
// =============================================================================

// Screen stack table at 0x8051a820, stride 0x1E0 per entry
// Navigation history list at 0x8051a818, stride 0x78 per viewport (select_list)
// g_screen_stack_index: DAT_806119ec
// Multi-screen mode flag at DAT_80612614 (0x65 = split-screen multiplayer)
// Per-viewport mask: DAT_806119b8

// =============================================================================
// Address: 0x80046e48 | hud_screen_destroy_current
// Finds root screen from current stack entry, then walks hierarchy to destroy
// all type-1 children (tears down active screen tree).
// =============================================================================
void hud_screen_destroy_current(void)
{
    int* root = (int*)hud_screen_find_root(
        *(int*)(&g_screen_stack[g_screen_stack_index * 0x1E0])
    );
    hud_walk_hierarchy_by_type(root, 1);
}

// =============================================================================
// Address: 0x80046e88 | hud_screen_find_popup_child
// Searches child list of a screen for a popup element (type==1, sub-type==4
// at offset +0x1AC). Returns NULL if no popup child exists.
// =============================================================================
static int* hud_screen_find_popup_child(int screen)
{
    if (screen == 0) return NULL;

    int* child = *(int**)(screen + 0x168);
    for (; child != NULL; child = (int*)child[0xC]) {
        if (child[0] == 1 && child[0x6B] == 4) {
            return child;
        }
    }
    return NULL;
}

// =============================================================================
// Address: 0x80046ec4 | hud_screen_find_current_popup
// Convenience wrapper: finds popup child of the current screen stack entry.
// =============================================================================
int* hud_screen_find_current_popup(void)
{
    return hud_screen_find_popup_child(
        *(int*)(&g_screen_stack[g_screen_stack_index * 0x1E0])
    );
}

// =============================================================================
// Address: 0x80046efc | hud_screen_node_is_valid
// Recursive check: validates a screen node by walking into its child at +0x2C.
// Uses CRT register-save (FUN_803826f0) for stack frame. If child at +0x2C
// exists, recurses. Used by hud_screen_find_root to validate hierarchy nodes.
// =============================================================================
int hud_screen_node_is_valid(void)
{
    int node = crt_save_regs_3();  // FUN_803826f0
    if (*(int*)(node + 0x2C) != 0) {
        hud_screen_node_is_valid();  // recurse into child
    }
    crt_restore_regs_3();  // FUN_8038273c
}

// =============================================================================
// Address: 0x80046f6c | hud_screen_all_children_closing
// Checks if ALL children of a screen are in closing state. Returns TRUE only
// if the node is valid AND every child that is either the active sub-screen
// (+0x16C) or type==3 (overlay) is currently closing. Used to determine if
// a screen transition is complete.
// =============================================================================
int hud_screen_all_children_closing(int screen)
{
    int valid = hud_screen_node_is_valid();
    if (valid == 0) return 0;

    int* child = *(int**)(screen + 0x168);
    for (; child != NULL; child = (int*)child[0xC]) {
        // Check active sub-screen or overlay children
        if (*(int**)(screen + 0x16C) == child || child[0] == 3) {
            if (!hud_element_is_closing((int)child)) {
                return 0;  // found non-closing child
            }
        }
    }

    return 1;  // all relevant children are closing
}

// =============================================================================
// Address: 0x80046ff8 | hud_screen_can_close
// Like hud_screen_all_children_closing but with additional check: for the
// active sub-screen (+0x16C), also requires no pending navigation (+0x198==0).
// Used by item_is_selected and element_needs_dim to gate user interaction.
// =============================================================================
int hud_screen_can_close(int screen)
{
    int valid = hud_screen_node_is_valid();
    if (valid == 0) return 0;

    int* child = *(int**)(screen + 0x168);
    for (; child != NULL; child = (int*)child[0xC]) {
        BOOL is_active_sub = (*(int**)(screen + 0x16C) == child) &&
                             (*(int*)(screen + 0x198) == 0);
        if (is_active_sub || child[0] == 3) {
            if (!hud_element_is_closing((int)child)) {
                return 0;
            }
        }
    }

    return 1;
}

// =============================================================================
// Address: 0x80047090 | hud_screen_navigate_to
// MASSIVE: Core screen navigation. Closes all non-target children with
// transition state 4 (close animation), saves their scroll/focus state to
// the navigation history (select_list), then creates/shows the target screen.
// Restores focus or scroll position from history if available.
// Also handles split-screen viewport masking (flag 0x1000000, mode 0x65).
// =============================================================================
void hud_screen_navigate_to(int parent, int target_id, int focus_index)
{
    int* prev_active = NULL;
    int* nav_history = *(int**)(&g_screen_nav_history[g_screen_stack_index * 0x1E0]);
    int* page_stack = (int*)(&g_screen_page_stack[g_screen_stack_index * 0x78]);

    // Close all children except the target
    if (parent != 0) {
        int* child = *(int**)(parent + 0x168);
        for (; child != NULL; child = (int*)child[0xC]) {
            int* keep = prev_active;

            // Skip overlays from other viewports in split-screen
            if (child[0] == 3 && g_screen_stack_index != child[0xE]) {
                keep = prev_active;
            } else if (child[1] != target_id) {
                // Save scroll/focus state to nav history before closing
                if (child != NULL && child[1] != 0 && !hud_element_is_closing((int)child)) {
                    int hist_idx = select_list_find_value(nav_history, child[1]);
                    if (hist_idx >= 0) {
                        int save_mode = nav_history[0x3D];
                        if (save_mode == 1) {
                            nav_history[hist_idx + 0x3E] = child[0x21]; // scroll pos
                        } else if (save_mode == 0) {
                            nav_history[hist_idx + 0x3E] = child[0x60]; // focus index
                        }
                    }
                }
                hud_element_set_transition((int)child, 4);  // start close anim
                hud_screen_close_cleanup((int)child);       // FUN_80047608
                keep = prev_active;
            } else {
                keep = child;
            }
            prev_active = keep;
        }
    }

    // Create target screen if not already present
    if (target_id != 0 && prev_active == NULL) {
        int* new_screen;
        float scroll_restore = 0.0f;

        if ((*(unsigned int*)(parent + 0x40) & 0x200000) == 0) {
            new_screen = (int*)hud_element_create(parent, target_id, 1);
        } else {
            new_screen = (int*)hud_element_create(parent, target_id, 3);
        }
        hud_element_link_and_show(parent, (int)new_screen);

        // Restore state from nav history
        int hist_idx = select_list_find_value(nav_history, target_id);
        int restore_mode = 0;
        if (hist_idx >= 0) {
            restore_mode = nav_history[0x3D];
            if (restore_mode == 1) {
                scroll_restore = *(float*)&nav_history[hist_idx + 0x3E];
            } else if (restore_mode == 0) {
                focus_index = nav_history[hist_idx + 0x3E];
            }
        }

        // Apply restored focus or scroll
        if (new_screen == NULL || focus_index == -1 || restore_mode != 0) {
            if (new_screen != NULL && restore_mode == 1) {
                hud_scroll_set_target(scroll_restore, (int)new_screen);
                hud_scroll_snap_position((int)new_screen);
            }
        } else {
            hud_set_focus_by_offset((int)new_screen, focus_index);
            hud_scroll_snap_to_focus((int)new_screen);
        }
        prev_active = new_screen;
    }

    // Update page stack (trim entries after current target)
    if (target_id != 0 && page_stack != NULL) {
        int count = page_stack[0];
        for (int i = 0; i < count; i++) {
            if (page_stack[i + 1] == target_id) {
                page_stack[0] = i;  // trim stack to this entry
                break;
            }
        }
    }

    hud_input_reset_repeat_timers();
    hud_element_link_and_show(parent, (int)prev_active);

    // Apply viewport mask for split-screen
    if (prev_active != NULL && (prev_active[0x10] & 0x1000000) != 0 &&
        g_multi_screen_mode == 0x65) {
        prev_active[0xF] = 1 << g_active_viewport_idx;
        prev_active[0xE] = g_active_viewport_idx;
    }
}

// =============================================================================
// Address: 0x80047364 | hud_screen_push
// Pushes a new screen onto the navigation stack. Saves the current screen's
// parent to the page stack (for back navigation), then calls navigate_to
// with the new target. Updates g_screen_stack_index for split-screen.
// Called from menu system when entering sub-menus.
// =============================================================================
void hud_screen_push(void)
{
    int parent = crt_save_regs_4();  // FUN_803826ec - gets params
    int target_id = /* second param */;

    select_list_nop();

    if (parent != 0) {
        g_screen_stack_index = *(int*)(parent + 0x38);
        int* page_stack = (int*)(&g_screen_page_stack[g_screen_stack_index * 0x78]);

        // Save current active child to page stack for back navigation
        int current_child = hud_screen_get_parent(parent);
        if (current_child != 0 && *(int*)(current_child + 4) != 0 &&
            !hud_element_is_closing(current_child)) {
            select_list_append_entry(page_stack, current_child);
        }

        // Trim page stack to find target entry
        int count = page_stack[0];
        for (int i = 0; i < count; i++) {
            if (page_stack[i + 1] == target_id) {
                page_stack[0] = i;
                break;
            }
        }
    }

    select_list_nop();
    hud_screen_navigate_to(parent, target_id, -1);
}

// =============================================================================
// Address: 0x80047444 | hud_screen_pop
// Pops current screen: starts close transition (state 4), then shows and
// links the parent screen. Triggers viewport navigation update if not
// flagged with 0x40000 (no-nav-update flag).
// Called from hud_element_confirm/cancel/dispatch when user backs out.
// =============================================================================
void hud_screen_pop(int screen)
{
    int parent_screen = *(int*)(screen + 0x2C);
    if (parent_screen == 0) return;

    hud_element_set_transition(screen, 4);  // close animation
    int grandparent = *(int*)(parent_screen + 0x2C);
    hud_element_link_and_show(grandparent, parent_screen);

    // Trigger viewport navigation update unless suppressed
    int stack_idx = *(int*)(parent_screen + 0x38);
    if (*(int*)(&g_screen_stack[stack_idx * 0x1E0] + 4) != 0 &&
        (*(unsigned int*)(parent_screen + 0x40) & 0x40000) == 0) {
        hud_viewport_navigation_update();
    }
}

// =============================================================================
// Address: 0x800474c0 | hud_screen_pop_current
// Convenience wrapper: pops the current screen (calls the internal pop
// implementation FUN_800474e4 which handles history restore).
// =============================================================================
void hud_screen_pop_current(void)
{
    hud_screen_pop_with_restore();  // FUN_800474e4
}

// =============================================================================
// HUD Screen Pop/Close + Handler System (Batch 085)
// Screen pop with history restore, recursive close, and 4-slot event handlers.
// Date: 2026-08-10
// =============================================================================

// Handler table layout per screen element (at offset +0x124):
//   4 slots, stride 0x10 each:
//     +0x00: handler_id (int, -1 = empty)
//     +0x04: param_a (callback/value)
//     +0x08: param_b (user data)
//     +0x0C: param_c (context)
// Global handler state at DAT_806119e0

// =============================================================================
// Address: 0x800474e4 | hud_screen_pop_with_restore
// Internal implementation of screen pop. Pops the top entry from the page
// stack (select_list at 0x8051a818), retrieves the previous screen ID and
// its saved scroll/focus state, then navigates back to it. In split-screen
// mode (0x65), resets to viewport 0 if the screen doesn't have the viewport
// flag (0x1000000). Finally clears B-button input.
// =============================================================================
void hud_screen_pop_with_restore(void)
{
    int screen = crt_save_regs_2();  // FUN_803826e8
    float scroll_target = 0.0f;
    int focus_index = -1;
    int restore_mode = 0;
    int prev_screen_id = 0;
    int* page_stack = (int*)(&g_screen_page_stack[g_screen_stack_index * 0x78]);

    // In split-screen, if screen lacks viewport flag, reset to viewport 0
    if (g_multi_screen_mode == 0x65 &&
        (*(unsigned int*)(screen + 0x40) & 0x1000000) == 0) {
        g_screen_stack_index = 0;
        page_stack = g_screen_page_stack;
    }

    if (screen != 0) {
        select_list_nop();

        if (page_stack[0] != 0) {
            // Pop top of page stack
            page_stack[0]--;
            int top = page_stack[0];
            restore_mode = page_stack[0x3D];
            prev_screen_id = page_stack[top + 1];

            if (restore_mode == 1) {
                scroll_target = *(float*)&page_stack[top + 0x3E];
            } else if (restore_mode == 0) {
                focus_index = page_stack[top + 0x3E];
            }
        }

        select_list_nop();
    }

    int new_screen = hud_screen_navigate_to(screen, prev_screen_id, focus_index);

    if (restore_mode == 1) {
        hud_scroll_set_target(scroll_target, new_screen);
        hud_scroll_snap_position(new_screen);
    }

    input_clear_buttons(g_screen_stack_index, 0x10);  // clear B button
}

// =============================================================================
// Address: 0x80047608 | hud_screen_close_children_recursive
// Recursively sets transition state 5 (immediate close) on all children
// of a screen element. Walks the child list (+0x168, next at +0x30) and
// recurses into each. Called by navigate_to during page transitions.
// =============================================================================
void hud_screen_close_children_recursive(int screen)
{
    if (screen == 0) return;

    int child = *(int*)(screen + 0x168);
    for (; child != 0; child = *(int*)(child + 0x30)) {
        hud_element_set_transition(child, 5);  // immediate close (no animation)
        hud_screen_close_children_recursive(child);  // recurse
    }
}

// =============================================================================
// Address: 0x8004765c | hud_screen_find_handler
// Searches the 4-slot handler table (at +0x124, stride 0x10) for a handler
// matching the given ID. Returns pointer to slot, or 0 if not found.
// =============================================================================
static int hud_screen_find_handler(int screen, int handler_id)
{
    int offset = 0;
    for (int i = 0; i < 4; i++) {
        if (*(int*)(screen + offset + 0x124) == handler_id) {
            return screen + offset + 0x124;
        }
        offset += 0x10;
    }
    return 0;
}

// =============================================================================
// Address: 0x80047694 | hud_screen_clear_all_handlers
// Clears all 4 handler slots by setting their IDs to -1 (empty).
// =============================================================================
void hud_screen_clear_all_handlers(int screen)
{
    int offset = 0;
    for (int i = 0; i < 4; i++) {
        *(int*)(screen + offset + 0x124) = -1;
        offset += 0x10;
    }
}

// =============================================================================
// Address: 0x800476b8 | hud_screen_remove_handler
// Removes handler by ID: scans all 4 slots and sets matching IDs to -1.
// =============================================================================
void hud_screen_remove_handler(int screen, int handler_id)
{
    int offset = 0;
    for (int i = 0; i < 4; i++) {
        if (*(int*)(screen + offset + 0x124) == handler_id) {
            *(int*)(screen + offset + 0x124) = -1;
        }
        offset += 0x10;
    }
}

// =============================================================================
// Address: 0x800476e8 | hud_screen_remove_handler_current
// Convenience: removes handler by ID from the current screen stack entry.
// =============================================================================
void hud_screen_remove_handler_current(int handler_id)
{
    hud_screen_remove_handler(
        *(int*)(&g_screen_stack[g_screen_stack_index * 0x1E0]),
        handler_id
    );
}

// =============================================================================
// Address: 0x80047724 | hud_screen_set_handler_param_b
// Finds handler by ID in screen's table, sets param_b (offset +0x08 in slot).
// =============================================================================
void hud_screen_set_handler_param_b(int screen, int handler_id, int value)
{
    int slot = hud_screen_find_handler(screen, handler_id);
    if (slot != 0) {
        *(int*)(slot + 8) = value;
    }
}

// =============================================================================
// Address: 0x8004775c | hud_screen_set_handler_param_a
// Finds handler by ID in screen's table, sets param_a (offset +0x04 in slot).
// =============================================================================
void hud_screen_set_handler_param_a(int screen, int handler_id, int value)
{
    int slot = hud_screen_find_handler(screen, handler_id);
    if (slot != 0) {
        *(int*)(slot + 4) = value;
    }
}

// =============================================================================
// Address: 0x80047794 | hud_screen_register_handler
// Registers a handler in the screen's 4-slot table. Finds first empty slot
// (ID == -1) or overwrites existing slot with same ID. Writes all 4 fields:
//   +0x00 = handler_id
//   +0x04 = param_a (typically a callback pointer)
//   +0x08 = param_b (user data)
//   +0x0C = param_c (context/flags)
// Silently fails if all 4 slots are full and ID not already present.
// =============================================================================
void hud_screen_register_handler(int screen, int handler_id,
                                  int param_a, int param_b, int param_c)
{
    int offset = 0;
    for (int i = 0; i < 4; i++) {
        int slot_id = *(int*)(screen + offset + 0x124);
        if (slot_id == -1 || slot_id == handler_id) {
            int base = screen + offset;
            *(int*)(base + 0x124) = handler_id;
            *(int*)(base + 0x128) = param_a;
            *(int*)(base + 0x12C) = param_b;
            *(int*)(base + 0x130) = param_c;
            return;
        }
        offset += 0x10;
    }
}

// =============================================================================
// Address: 0x800477e0 | hud_screen_register_handler_global
// Convenience wrapper: registers handler into the global screen state
// (DAT_806119e0) rather than a specific screen element. Used heavily by
// menu build callbacks and dialog creation (45+ callers).
// =============================================================================
void hud_screen_register_handler_global(int handler_id, int param_a,
                                         int param_b, int param_c)
{
    hud_screen_register_handler(g_screen_state_global, handler_id,
                                param_a, param_b, param_c);
}

// =============================================================================
// HUD Screen Handler Wrappers + Item Callback/Layout System (Batch 086)
// Current-screen handler wrappers, item callback dispatch table, layout config,
// dirty flag clearing, popup height query, sort key.
// Date: 2026-08-10
// =============================================================================

// Item callback table at 0x803fdfa8, stride 8 per entry:
//   +0x00: callback_data (returned by get_callback_data)
//   +0x04: validator_func (called by has_callback to check if callback is active)
// Item's layout_type index at +0xB4 selects the table entry.

// =============================================================================
// Address: 0x80047820 | hud_screen_register_handler_current
// Convenience: registers handler into the current screen stack entry.
// =============================================================================
void hud_screen_register_handler_current(int handler_id, int param_a,
                                          int param_b, int param_c)
{
    hud_screen_register_handler(
        *(int*)(&g_screen_stack[g_screen_stack_index * 0x1E0]),
        handler_id, param_a, param_b, param_c
    );
}

// =============================================================================
// Address: 0x8004786c | hud_screen_set_handler_b_current
// Convenience: sets param_b on handler in current screen stack entry.
// =============================================================================
void hud_screen_set_handler_b_current(int handler_id, int value)
{
    hud_screen_set_handler_param_b(
        *(int*)(&g_screen_stack[g_screen_stack_index * 0x1E0]),
        handler_id, value
    );
}

// =============================================================================
// Address: 0x800478ac | hud_screen_clear_dirty_flags
// Clears bit 1 (dirty) and bit 17 (needs-layout) from flags (+0x40) on the
// screen and ALL its children. Used after layout finalization to reset state.
// =============================================================================
void hud_screen_clear_dirty_flags(int screen)
{
    int child = *(int*)(screen + 0x168);

    // Clear flags on all children
    if (child != 0) {
        *(unsigned int*)(child + 0x40) &= ~0x02;       // clear dirty
        *(unsigned int*)(child + 0x40) &= ~0x00020000;  // clear needs-layout

        while (*(int*)(child + 0x30) != 0) {
            child = *(int*)(child + 0x30);
            *(unsigned int*)(child + 0x40) &= ~0x02;
            *(unsigned int*)(child + 0x40) &= ~0x00020000;
        }
    }

    // Clear flags on parent screen itself
    *(unsigned int*)(screen + 0x40) &= ~0x02;
    *(unsigned int*)(screen + 0x40) &= ~0x00020000;
}

// =============================================================================
// Address: 0x80047918 | hud_screen_get_popup_height
// Returns available height for popup content. Finds the current popup child
// and subtracts its used height (+0x1B4) from the full height constant
// (FLOAT_80613bc8, likely 1.0 normalized). Returns full height if no popup.
// =============================================================================
float hud_screen_get_popup_height(void)
{
    int popup = hud_screen_find_current_popup();
    float full_height = FLOAT_80613bc8;  // likely 1.0f (normalized)

    if (popup != 0) {
        full_height -= *(float*)(popup + 0x1B4);
    }

    return full_height;
}

// =============================================================================
// Address: 0x80047968 | hud_item_has_callback
// Checks if an item's layout_type (+0xB4) has a registered callback in the
// global callback table (0x803fdfa8, stride 8). If param_2==0, just checks
// existence. If param_2==1, also invokes the validator function (+0x04 in
// table entry) and returns TRUE only if validator returns non-zero.
// Used by hud_screen_rebuild_items to determine value save/restore mode.
// =============================================================================
BOOL hud_item_has_callback(int item, int validate)
{
    int table_offset = *(int*)(item + 0xB4) * 8;

    if (validate == 0) {
        // Simple existence check
        return *(int*)(&g_item_callback_table + table_offset) != 0;
    } else {
        // Existence + validator invocation
        if (*(int*)(&g_item_callback_table + table_offset) == 0) return FALSE;
        if (*(void**)(&g_item_callback_table + table_offset + 4) == NULL) return FALSE;

        int result = (*(int(*)(void))(*(int*)(&g_item_callback_table + table_offset + 4)))();
        return result != 0;
    }
}

// =============================================================================
// Address: 0x800479ec | hud_item_get_callback_data
// Returns the callback_data field (+0x00 in table entry) for the item's
// layout_type. This is the value stored/restored during screen rebuild.
// =============================================================================
int hud_item_get_callback_data(int item)
{
    return *(int*)(&g_item_callback_table + *(int*)(item + 0xB4) * 8);
}

// =============================================================================
// Address: 0x80047a04 | hud_item_apply_layout_style
// Applies layout styling to an item based on its flags and parent context:
//   - Sets color palette index (+0xC0) from parent layout descriptor
//   - Propagates disable flag (0x20000) if item is disabled
//   - Resolves alignment mode from flags (0x4000=left, 0x8000=center, 0x10000=right)
//   - Clears alignment bits after resolution
//   - Sets text width limit (+0xDC) based on alignment mode
// =============================================================================
void hud_item_apply_layout_style(int item)
{
    // Get color palette from parent layout
    *(int*)(item + 0xC0) = *(int*)(*(int*)(g_screen_state_global + 0x1C) + 8);

    // Propagate disabled flag
    if (*(unsigned int*)(item + 0x28) & 0x20000) {
        *(unsigned int*)(item + 0xB8) |= 0x20000;
    }

    unsigned int flags = *(unsigned int*)(item + 0xB8);

    // Resolve alignment mode
    if (flags & 0x4000) {
        *(int*)(item + 0xC0) = 0;       // left-aligned
    } else if (flags & 0x8000) {
        *(int*)(item + 0xC0) = 1;       // center-aligned
    } else if (flags & 0x10000) {
        *(int*)(item + 0xC0) = 2;       // right-aligned
    }

    // Clear alignment bits after resolution
    *(unsigned int*)(item + 0xB8) &= ~0x0001C000;

    // Set text width limit
    *(float*)(item + 0xDC) = *(float*)(g_screen_state_global + 0x11C);
    if (*(int*)(item + 0xC0) == 1) {
        // Center-aligned: reduce width by right margin
        *(float*)(item + 0xDC) = *(float*)(item + 0x6C) - *(float*)(g_screen_state_global + 0x120);
    }
}

// =============================================================================
// Address: 0x80047aa4 | hud_item_configure_layout
// Full item layout configuration. Combines:
//   - Merging parent style flags with item-specific overrides
//   - Setting layout_type (+0xB4) and combined flags (+0xB8)
//   - Resolving color palette via FUN_8004879c
//   - Handling spacer type (type==3: sets disabled bit)
//   - Adjusting Y position based on parent padding mode (mode 1/2)
//   - Setting item height (+0xF0)
//   - Calling apply_layout_style for final styling
// =============================================================================
void hud_item_configure_layout(float height, int item, int layout_type, unsigned int flags)
{
    unsigned int parent_flags = *(unsigned int*)(g_screen_state_ptr + 0x28);

    // Clear parent bits that this item overrides
    if (flags & 0x1C000) {
        parent_flags &= ~0x1C000;  // clear alignment
    }
    if (flags & 0x300) {
        parent_flags &= ~0x300;    // clear size mode
    }

    *(int*)(item + 0xB4) = layout_type;
    *(unsigned int*)(item + 0xB8) = flags | parent_flags;

    // Resolve color palette
    int palette = hud_resolve_palette(g_screen_state_global, *(unsigned int*)(item + 0xB8));
    *(int*)(item + 0xBC) = palette;

    // Type-specific handling
    if (*(int*)(item + 0xB4) == 3) {
        // Spacer: mark as non-interactive
        *(unsigned int*)(item + 0x28) |= 4;
    } else if (*(int*)(g_screen_state_ptr + 0x4C) == 2) {
        // Padding mode 2: add padding to height
        height += *(float*)(g_screen_state_ptr + 0x54);
    } else if (*(int*)(g_screen_state_ptr + 0x4C) == 1) {
        // Padding mode 1: add padding + set indent flag
        *(unsigned int*)(item + 0xB8) |= 0x400;
        height += *(float*)(g_screen_state_ptr + 0x54);
    }

    *(float*)(item + 0xF0) = height;
    hud_item_apply_layout_style(item);
}

// =============================================================================
// Address: 0x80047b80 | hud_item_set_sort_key
// Sets the sort key (+0x9A as u16) for an item. If param_2 is 0, sets to 0;
// otherwise sets to the low 16 bits of param_2. Used for render ordering.
// =============================================================================
void hud_item_set_sort_key(int item, int sort_key)
{
    *(unsigned short*)(item + 0x9A) = (unsigned short)sort_key & -(unsigned short)(sort_key != 0);
}

// =============================================================================
// Address: 0x80047b94 | hud_item_add_spacer
// Adds a spacer item with layout_type=1 to the current layout target
// (DAT_806119dc). Spacers are non-interactive items used for vertical padding.
// Height is passed as first param, flags control alignment/style.
// =============================================================================
void hud_item_add_spacer(float height, unsigned int flags)
{
    hud_item_configure_layout(height, g_current_layout_target, 1, flags);
}
