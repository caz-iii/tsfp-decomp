// Batch: batch_913 | Module: arcade map-editor config screens (grid/count/duration) + helpers
// Address range: 0x80324d98 - 0x8032568c
// Date: 2026-09-12
//
// Second slice of the arcade map-editor config/options screens (continues
// arcade_edit_config_screens.cpp, batch_912). It holds the grid-cell settings screen and its
// unpack helper, a couple more small value-selector/toggle screens, the on-select commit that
// copies a matched cell's clamped position into the active edit record, the usual dirty-rebuild
// glue, and a float-pair clamp helper.
//
//   aveditor_unpack_grid_toggles    0x80324d98  expand the grid cell's packed flags into a record.
//   aveditor_build_grid_config      0x80324e08  build the grid-cell settings screen.
//   aveditor_commit_selected_cell   0x80325010  on select, copy the matched cell position, rebuild.
//   aveditor_build_pair_config      0x803250a0  build a two-header linked-value settings screen.
//   aveditor_clamp_selection_index  0x80325258  clamp the selection index to >=1 (def 1), rebuild.
//   aveditor_build_count_config     0x80325298  build the count screen (selector, or an add item).
//   aveditor_build_index_config     0x803253e4  build a single value-selector screen (+0x18, 1..8).
//   aveditor_config2_rebuild_dirty  0x803254f4  rebuild the screen items if it is dirty.
//   aveditor_build_duration_config  0x80325534  build the duration selector screen (+0x1c, 1..300).
//   aveditor_clamp_pos_pair         0x8032568c  clamp a pair of floats to [max, min] bounds.
//
// The grid-cell record lives at DAT_80612be4 + (context+0x2f78)*0xc + 0x3070; +8 packs the level
// index (as a power-of-two) and +9 packs three enable flags. aveditor_unpack_grid_toggles is the
// read path (inverse of batch_912's aveditor_commit_grid_toggles write path).
//
// Raw FUN_/DAT_/FLOAT_ symbols get correctly-typed per-file externs; named cross-module HUD helpers
// (and this-module helpers defined in the batch_909/912 files) are re-declared here with their real
// ABI so the call-site types match exactly.

#include "common.h"

extern "C" {

extern int   DAT_80612be4;    // editor context base
extern int   DAT_806119e0;    // shared position/anim descriptor arg

// -- layout / geometry float constants --
extern float FLOAT_8061a894;  // item padding value
extern float FLOAT_8061a8c0;  // common header/text display size
extern float FLOAT_8061a8d4;  // callback-item size
extern float FLOAT_8061a8e8;  // clamp lower bound (pos pair)
extern float FLOAT_8061a920;  // screen top Y
extern float FLOAT_8061a924;  // clamp upper bound (pos pair)

// -- raw helpers not yet individually named --
extern int   FUN_803826f0(void);    // fetch editor screen element (variant B)
extern void  FUN_8038273c(void);    // finalize screen (variant B)

// -- named cross-module helpers (real ABI) --
extern int   get_string_by_id(int stringId);
extern void  screen_exit_dirty_check(void);
extern int   hud_element_has_unsaved_changes(int element);
extern void  hud_screen_rebuild_items(void);
extern int   hud_item_is_selectable(int item, int which);

// this-module helpers defined in the batch_909 / batch_912 files
extern int   aveditor_pow2_to_log2(int x);
extern int  *aveditor_find_matching_cell(int *activeMember);
extern void  aveditor_copy_clamp_sign3(int *dst, unsigned int *src);

extern void  hud_element_init_from_layout(int layoutId, int a, int b, void *closeCb);
extern void  hud_set_item_padding_mode(double pad, int a, int b, int c);
extern void  hud_set_context_property(int prop);
extern void  hud_begin_scroll_with_item(void);
extern void  hud_item_add_header(double size, int flags);
extern void  hud_set_last_child_tag(int tag);
extern void  hud_event_add_pair(void);
extern void  hud_item_set_linked_value(int *value);
extern void  hud_end_scroll_region(void);
extern void  hud_item_add_value_selector(int a, int *value, int lo, int hi, int step, int c, int e);
extern void  hud_add_list_item(void);
extern void  hud_item_add_text_display(double size, int flags);
extern void  hud_item_set_value_display(int value);
extern void  hud_add_callback_item(double size);
extern void  hud_element_set_pos_y(double y, int descriptor);
extern void  hud_element_finalize_frame(void);

} // extern "C"

// -----------------------------------------------------------------------------

// Expand the current grid cell's packed flags into the record at `record` (read path, inverse of
// aveditor_commit_grid_toggles). cell+9 bit1 -> +4 (as 1/2), bit2 -> +0x10, bit3 -> +0xc; cell+8
// (a power-of-two level) -> +8 via aveditor_pow2_to_log2.
void aveditor_unpack_grid_toggles(int record)
{
    int cell = DAT_80612be4 + *(int *)(DAT_80612be4 + 0x2f78) * 0xc + 0x3070;
    *(unsigned int *)(record + 4)   = (*(unsigned char *)(cell + 9) >> 1 & 1) + 1;
    *(unsigned int *)(record + 0x10) = *(unsigned char *)(cell + 9) >> 2 & 1;
    *(unsigned int *)(record + 0xc)  = *(unsigned char *)(cell + 9) >> 3 & 1;
    *(int *)(record + 8) = aveditor_pow2_to_log2((unsigned int)*(unsigned char *)(cell + 8));
}

// Build the grid-cell settings screen: unpack the current cell flags into the scratch record at
// ctx+0x2f4c, then lay out a level header (0x18b2/0x18b3 linked to ctx+0x2f50), a 5-entry type
// selector (0xa29..0xa2d linked to ctx+0x2f54), two read-only value displays, and the exit row.
void aveditor_build_grid_config(void)
{
    int screen = FUN_803826f0();
    int base   = DAT_80612be4;

    aveditor_unpack_grid_toggles(DAT_80612be4 + 0x2f4c);
    hud_element_init_from_layout(0x8400, 0, 0, (void *)screen_exit_dirty_check);
    *(int *)(screen + 0x214) = 1;
    hud_set_item_padding_mode((double)FLOAT_8061a894, 1, 1, 1);
    hud_set_context_property(0x20000);

    get_string_by_id(0xaa3);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a8c0, 0);
    hud_set_last_child_tag(0x95);
    get_string_by_id(0x18b2);
    hud_event_add_pair();
    get_string_by_id(0x18b3);
    hud_event_add_pair();
    hud_item_set_linked_value((int *)(base + 0x2f50));
    hud_end_scroll_region();

    get_string_by_id(0xa26);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a8c0, 0);
    hud_set_last_child_tag(0x96);
    get_string_by_id(0xa29);
    hud_event_add_pair();
    int i = 1;
    do {
        get_string_by_id(i + 0xa29);
        hud_event_add_pair();
        i = i + 1;
    } while (i < 5);
    hud_item_set_linked_value((int *)(base + 0x2f54));
    hud_end_scroll_region();

    get_string_by_id(0xa2e);
    hud_add_list_item();
    hud_item_add_text_display((double)FLOAT_8061a8c0, 0);
    hud_set_last_child_tag(0x97);
    hud_item_set_value_display(base + 0x2f58);

    get_string_by_id(0xa2f);
    hud_add_list_item();
    hud_item_add_text_display((double)FLOAT_8061a8c0, 0);
    hud_set_last_child_tag(0x98);
    hud_item_set_value_display(base + 0x2f5c);

    hud_add_callback_item((double)FLOAT_8061a8d4);
    get_string_by_id(0xb03);
    hud_add_list_item();
    hud_element_finalize_frame();
    hud_element_set_pos_y((double)(FLOAT_8061a920 - *(float *)(screen + 100)), DAT_806119e0);
    FUN_8038273c();
}

// On a select of any of the item's first three slots: find the cell matching the active edit record
// (ctx+0x2f1c) and copy its clamped position (cell+6 ints) into the active record at +0x18, then
// rebuild the screen.
void aveditor_commit_selected_cell(int item)
{
    if ((hud_item_is_selectable(item, 0) != 0) ||
        (hud_item_is_selectable(item, 1) != 0) ||
        (hud_item_is_selectable(item, 2) != 0)) {
        int *cell = aveditor_find_matching_cell(*(int **)(DAT_80612be4 + 0x2f1c));
        if (cell != (int *)0) {
            aveditor_copy_clamp_sign3(cell + 6,
                                      (unsigned int *)(*(int *)(DAT_80612be4 + 0x2f1c) + 0x18));
        }
        hud_screen_rebuild_items();
    }
}

// Build a two-header linked-value settings screen: a 5-entry header (0xad0..0xad4 linked to
// ctx+0x1c) and a 3-entry header (0xad7..0xad9 linked to ctx+0x20), then the exit row.
void aveditor_build_pair_config(int screen)
{
    int context = *(int *)(DAT_80612be4 + 0x2f1c);
    hud_element_init_from_layout(0x8400, 0, 0, (void *)screen_exit_dirty_check);
    *(int *)(screen + 0x214) = 1;
    hud_set_item_padding_mode((double)FLOAT_8061a894, 1, 1, 1);
    hud_set_context_property(0x20000);

    get_string_by_id(0xacf);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a8c0, 0);
    hud_set_last_child_tag(0x99);
    get_string_by_id(0xad0);
    hud_event_add_pair();
    get_string_by_id(0xad1);
    hud_event_add_pair();
    get_string_by_id(0xad2);
    hud_event_add_pair();
    get_string_by_id(0xad3);
    hud_event_add_pair();
    get_string_by_id(0xad4);
    hud_event_add_pair();
    hud_item_set_linked_value((int *)(context + 0x1c));
    hud_end_scroll_region();

    get_string_by_id(0xad6);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a8c0, 0);
    hud_set_last_child_tag(0x9b);
    get_string_by_id(0xad7);
    hud_event_add_pair();
    get_string_by_id(0xad8);
    hud_event_add_pair();
    get_string_by_id(0xad9);
    hud_event_add_pair();
    hud_item_set_linked_value((int *)(context + 0x20));
    hud_end_scroll_region();

    hud_add_callback_item((double)FLOAT_8061a8d4);
    get_string_by_id(0xb03);
    hud_add_list_item();
    hud_element_finalize_frame();
    hud_element_set_pos_y((double)(FLOAT_8061a920 - *(float *)(screen + 100)), DAT_806119e0);
}

// Clamp the selection index at *(ctx+0x2f1c)+0x18 up to at least 1 (a -1 becomes the default 1),
// then rebuild the screen.
void aveditor_clamp_selection_index(void)
{
    int cur = *(int *)(*(int *)(DAT_80612be4 + 0x2f1c) + 0x18);
    int clamped = 1;
    if (cur != -1) {
        clamped = cur;
    }
    *(int *)(*(int *)(DAT_80612be4 + 0x2f1c) + 0x18) = clamped;
    hud_screen_rebuild_items();
}

// Build the count screen: when the value at *(ctx+0x2f1c)+0x18 is set, show a 1..8 value selector;
// when it is -1, show an "add" list item instead. Followed by the exit row.
void aveditor_build_count_config(int screen)
{
    int *value = (int *)(*(int *)(DAT_80612be4 + 0x2f1c) + 0x18);
    hud_element_init_from_layout(0x8400, 0, 0, (void *)screen_exit_dirty_check);
    *(int *)(screen + 0x214) = 1;
    hud_set_item_padding_mode((double)FLOAT_8061a894, 1, 1, 1);
    hud_set_context_property(0x20000);

    if (*value == -1) {
        get_string_by_id(0xadd);
        hud_add_list_item();
        hud_set_last_child_tag(0x9d);
    } else {
        get_string_by_id(0xadb);
        hud_begin_scroll_with_item();
        hud_item_add_value_selector(0, value, 1, 8, 1, 0, 0);
        hud_set_last_child_tag(0x9c);
        hud_end_scroll_region();
    }

    hud_add_callback_item((double)FLOAT_8061a8d4);
    get_string_by_id(0xb03);
    hud_add_list_item();
    hud_element_finalize_frame();
    hud_element_set_pos_y((double)(FLOAT_8061a920 - *(float *)(screen + 100)), DAT_806119e0);
}

// Build a single value-selector screen for the value at ctx+0x18 (range 1..8), then the exit row.
void aveditor_build_index_config(int screen)
{
    int context = *(int *)(DAT_80612be4 + 0x2f1c);
    hud_element_init_from_layout(0x8400, 0, 0, (void *)screen_exit_dirty_check);
    *(int *)(screen + 0x214) = 1;
    hud_set_item_padding_mode((double)FLOAT_8061a894, 1, 1, 1);
    hud_set_context_property(0x20000);

    get_string_by_id(0xada);
    hud_begin_scroll_with_item();
    hud_item_add_value_selector(0, (int *)(context + 0x18), 1, 8, 1, 0, 0);
    hud_set_last_child_tag(0x9e);
    hud_end_scroll_region();

    hud_add_callback_item((double)FLOAT_8061a8d4);
    get_string_by_id(0xb03);
    hud_add_list_item();
    hud_element_finalize_frame();
    hud_element_set_pos_y((double)(FLOAT_8061a920 - *(float *)(screen + 100)), DAT_806119e0);
}

// Rebuild the config screen items if the element is dirty.
void aveditor_config2_rebuild_dirty(int element)
{
    if (hud_element_has_unsaved_changes(element) != 0) {
        hud_screen_rebuild_items();
    }
}

// Build the duration selector screen: a 1..300 value selector (linked to ctx+0x1c), a read-only
// value display (ctx+0x18), then the exit row.
void aveditor_build_duration_config(void)
{
    int screen  = FUN_803826f0();
    int context = *(int *)(DAT_80612be4 + 0x2f1c);
    hud_element_init_from_layout(0x8400, 0, 0, (void *)screen_exit_dirty_check);
    *(int *)(screen + 0x214) = 1;
    hud_set_item_padding_mode((double)FLOAT_8061a894, 1, 1, 1);
    hud_set_context_property(0x20000);

    get_string_by_id(0xae0);
    hud_begin_scroll_with_item();
    hud_item_add_value_selector(0, (int *)(context + 0x1c), 1, 300, 1, 0, 0x20);
    hud_set_last_child_tag(0x9f);
    hud_end_scroll_region();

    get_string_by_id(0xadf);
    hud_add_list_item();
    hud_item_add_text_display((double)FLOAT_8061a8c0, 0);
    hud_set_last_child_tag(0xa0);
    hud_item_set_value_display(context + 0x18);

    hud_add_callback_item((double)FLOAT_8061a8d4);
    get_string_by_id(0xb03);
    hud_add_list_item();
    hud_element_finalize_frame();
    hud_element_set_pos_y((double)(FLOAT_8061a920 - *(float *)(screen + 100)), DAT_806119e0);
    FUN_8038273c();
}

// Clamp a position pair: *a is clamped down to at most FLOAT_8061a924, *b is clamped up to at least
// FLOAT_8061a8e8.
void aveditor_clamp_pos_pair(float *a, float *b)
{
    float lower = FLOAT_8061a8e8;
    float va = *a;
    float vb = *b;
    if (FLOAT_8061a924 < va) {
        va = FLOAT_8061a924;
    }
    *a = va;
    if (vb < lower) {
        vb = lower;
    }
    *b = vb;
}
