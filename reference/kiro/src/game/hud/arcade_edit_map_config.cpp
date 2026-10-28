// Batch: batch_958 | Module: arcade map-editor map-config screen + objective staging/validation
// Address range: 0x80338f54 - 0x803394e0
// Date: 2026-09-12
//
// The "map configuration" sub-screen of the arcade match editor plus the objective staging-buffer
// edit/compare layer. arcade_edit_open_map_config seeds the cursor from the selected cell's grid
// coords and enters the config focus view; arcade_edit_build_map_config_screen lays out that
// screen's HUD (three placement-count list items + a callback item). The staging buffers at
// +0x11618 (0xb4, a placed-cell copy) and +0x116cc (0x8c, its roster descriptor copy) hold the
// entry currently being edited so changes can be diffed (arcade_edit_staging_changed) and committed
// or reverted. Editor context is DAT_80612be4; placed-cell list at +0x81a8 (0xb4 stride, count
// +0x81a4), roster table at +0xdd14 (0x8c stride).
//
//   arcade_edit_open_map_config       0x80338f54  seed cursor from the selected cell, enter config view.
//   arcade_edit_build_map_config_screen 0x80339040 build the map-config HUD (counts + callback item).
//   arcade_edit_clear_staging         0x80339204  zero both staging buffers.
//   arcade_edit_stage_entry           0x80339250  copy a placed-cell (+ its roster) into staging.
//   arcade_edit_staging_changed       0x803392d4  1 when the staged entry differs from the live one.
//   arcade_edit_end_staging           0x80339370  clear the staging flag + buffers.
//   arcade_edit_list_contains         0x803393a0  1 when a value is in a cell's (+0x20/+0x24,8) list.
//   arcade_edit_find_type3_with_id    0x803393e0  1 when a type-3 placed cell's roster holds an id.
//   arcade_edit_call_first_hook       0x80339488  invoke the first non-null hook in a 16-slot vtable.
//   arcade_edit_any_cell_dirty        0x803394e0  1 when any placed cell is flagged dirty (+0xb0&1).
//
// NON-MATCHING: arcade_edit_find_type3_with_id / arcade_edit_any_cell_dirty /
// arcade_edit_build_map_config_screen are emitted with the compiler _savegpr_/_restgpr_ runtime
// helpers (FUN_803826f0/ec/8038273c/38 and the named __save_gpr_28/__restore_gpr_28). Those are
// pure register-spill prologue/epilogue with no semantic effect and are omitted here; the real id
// argument recovered from the disassembly is expressed as an explicit parameter (Compile-Clean
// rules 1/3). The int->float 0x43300000 / DOUBLE_8061aa48 magic-double idiom is written as plain
// (float)(int) casts. This TU includes only common.h (not hud.h); helpers are declared file-locally
// so returns can be typed correctly (several shared decls carry void/permissive signatures).

#include "common.h"

extern "C" {

// editor context block: flags at +0x238, cursor/selection at +0x2f1c, staging buffers at +0x11618
// (0xb4) and +0x116cc (0x8c), placed-cell list at +0x81a8 (0xb4 stride, count +0x81a4).
extern int DAT_80612be4;

// map-config focus view descriptor + build-screen callback padding constant.
extern int   PTR_aveditor_build_map_config_80480c7c;
extern int   DAT_806119e0;    // parent element handle for the config frame
extern int   DAT_80480f4c;    // base of the 16-slot hook vtable (0x4c stride) scanned by call_first_hook
extern double DOUBLE_8061aa48; // int->float conversion magic double
extern float FLOAT_8061aa40;  // padding amount for the config list
extern float FLOAT_8061aa14;  // callback-item height
extern float FLOAT_8061aad4;  // base Y for the config frame

// named cross-module helpers (declared file-locally, not via hud.h)
extern int   aveditor_get_active_context(void);
extern int   aveditor_find_member_owning_cell(int cell);   // returns a float[3] coord record ptr
extern void  cursor_set_fields(unsigned short* pSrc);
extern void  screen_enter_focus_view(int viewDesc);
extern int   aveditor_count_placed_cells_of_type(int typeFilter);
extern void  hud_element_init_from_layout(int flags, void* data, int cancel_cb, int confirm_cb);
extern void  hud_set_item_padding_mode(float amount, int mode, int set_before, int set_after);
extern void  hud_set_context_property(int value);
extern int   get_string_by_id(int id);
extern void  hud_begin_scroll_region(void);
extern void  hud_add_list_item(void);
extern void  hud_set_last_child_tag(int tag);
extern void  hud_end_scroll_region(void);
extern int*  hud_add_callback_item(float height);
extern void  hud_element_finalize_frame(void);
extern void  hud_element_set_pos_y(double y, int element);
extern int   memset_wrapper(int dst, int value, int len);
extern void  leaf_31e8(unsigned dst, unsigned src, int len);
extern unsigned* match_mode_get_config(int nMode);

// raw FUN_ helper not yet named
extern int  FUN_80387b68(int a, int b, int len);   // byte-compare two records, returns nonzero on diff

} // extern "C"

// forward prototypes for this file's helpers referenced before their definition
void arcade_edit_clear_staging(void);
int  arcade_edit_list_contains(int list, int value);
void arcade_edit_open_map_config(void);

// -----------------------------------------------------------------------------

// Seed the cursor from the currently-selected cell's grid position (converting the cell's packed
// short coords at +0xc/+0xe/+8 into the owning member's float coords) and enter the map-config
// focus view. Ensures a valid selection context first (+0x2f1c, type 0x43).
void arcade_edit_open_map_config(void)
{
    if (*(int**)(DAT_80612be4 + 0x2f1c) == 0 ||
        **(int**)(DAT_80612be4 + 0x2f1c) != 0x43) {
        *(int*)(DAT_80612be4 + 0x2f1c) = aveditor_get_active_context();
    }
    float* coord = (float*)(intptr_t)aveditor_find_member_owning_cell(*(int*)(DAT_80612be4 + 0x2f1c));

    unsigned short fields[8];
    int cx = (int)(coord[0] + (float)(int)*(short*)(*(int*)(DAT_80612be4 + 0x2f1c) + 0xc));
    int cy = (int)(coord[1] + (float)(int)*(short*)(*(int*)(DAT_80612be4 + 0x2f1c) + 0xe));
    int cz = (int)coord[2];
    fields[0] = (unsigned short)cx;
    fields[1] = (unsigned short)cy;
    fields[2] = (unsigned short)cz;

    cursor_set_fields(fields);
    screen_enter_focus_view((int)(intptr_t)&PTR_aveditor_build_map_config_80480c7c);
}

// Build the arcade map-config HUD screen: prime the three placement counts (types 0x36/0x35/0x37),
// lay out the frame with three tagged list items (0x87/0x88/0x89), a callback item, and a final
// list item, then finalize and position the frame. `element` is the build context (+0x214 flag,
// +0x64 height field). NON-MATCHING: the _savegpr_28 spill is omitted; `element` is the ctx ptr.
void arcade_edit_build_map_config_screen(int element)
{
    aveditor_count_placed_cells_of_type(0x36);
    aveditor_count_placed_cells_of_type(0x35);
    aveditor_count_placed_cells_of_type(0x37);

    hud_element_init_from_layout(0x8400, 0, 0, (int)(intptr_t)&arcade_edit_open_map_config);
    *(int*)(element + 0x214) = 1;
    hud_set_item_padding_mode(FLOAT_8061aa40, 1, 1, 1);
    hud_set_context_property(0x20000);

    get_string_by_id(0xac6);
    hud_begin_scroll_region();
    get_string_by_id(0xab4);
    hud_add_list_item();
    hud_set_last_child_tag(0x87);
    get_string_by_id(0xab5);
    hud_add_list_item();
    hud_set_last_child_tag(0x88);
    get_string_by_id(0xab6);
    hud_add_list_item();
    hud_set_last_child_tag(0x89);
    hud_end_scroll_region();

    hud_add_callback_item(FLOAT_8061aa14);
    get_string_by_id(0xb03);
    hud_add_list_item();
    hud_element_finalize_frame();
    hud_element_set_pos_y((double)(FLOAT_8061aad4 - *(float*)(element + 0x64)), DAT_806119e0);
}

// Zero both staging buffers (the placed-cell copy at +0x11618 and its roster copy at +0x116cc).
void arcade_edit_clear_staging(void)
{
    memset_wrapper(DAT_80612be4 + 0x11618, 0, 0xb4);
    memset_wrapper(DAT_80612be4 + 0x116cc, 0, 0x8c);
}

// Copy a placed-cell into the staging buffer for editing: set the staging flag (+0x238 |= 0x800),
// clear staging, copy the cell's roster descriptor (when its id +0xc >= 0) then the cell itself.
void arcade_edit_stage_entry(int cell)
{
    *(unsigned*)(DAT_80612be4 + 0x238) |= 0x800;
    arcade_edit_clear_staging();
    if (*(short*)(cell + 0xc) >= 0) {
        leaf_31e8(DAT_80612be4 + 0x116cc,
                  DAT_80612be4 + *(short*)(cell + 0xc) * 0x8c + 0xdd14, 0x8c);
    }
    leaf_31e8(DAT_80612be4 + 0x11618, (unsigned)cell, 0xb4);
}

// Return 1 when the staged entry differs from the live cell (or its roster descriptor), else 0.
int arcade_edit_staging_changed(int cell)
{
    int cellDiff = FUN_80387b68(DAT_80612be4 + 0x11618, cell, 0xb4);
    bool rosterDiff = false;
    if (*(short*)(cell + 0xc) >= 0) {
        int d = FUN_80387b68(DAT_80612be4 + 0x116cc,
                             DAT_80612be4 + *(short*)(cell + 0xc) * 0x8c + 0xdd14, 0x8c);
        rosterDiff = (d != 0);
    }
    return (cellDiff != 0 || rosterDiff) ? 1 : 0;
}

// Clear the staging flag (+0x238 &= ~0x800) and zero the staging buffers.
void arcade_edit_end_staging(void)
{
    *(unsigned*)(DAT_80612be4 + 0x238) &= 0xfffff7ff;
    arcade_edit_clear_staging();
}

// Return 1 when `value` appears in the list at `list` (count at +0x20, entries at +0x24, 8 stride).
int arcade_edit_list_contains(int list, int value)
{
    int n = *(int*)(list + 0x20);
    int off = 0;
    while (n > 0) {
        if (*(int*)(list + off + 0x24) == value) {
            return 1;
        }
        off += 8;
        --n;
    }
    return 0;
}

// Return 1 when some type-3 placed cell (field +4 == 3, valid id +0xc) has `id` in its roster
// descriptor's list. NON-MATCHING: the _savegpr_ spill is omitted; `id` is the incoming r3 param.
int arcade_edit_find_type3_with_id(int id)
{
    for (int i = 0, off = 0; i < *(int*)(DAT_80612be4 + 0x81a4); i++, off += 0xb4) {
        int cell = DAT_80612be4 + off + 0x81a8;
        int cellId = (int)*(short*)(cell + 0xc);
        if (*(int*)(cell + 4) != 3 || cellId == -1) {
            continue;
        }
        if (arcade_edit_list_contains(DAT_80612be4 + cellId * 0x8c + 0xdd14, id) != 0) {
            return 1;
        }
    }
    return 0;
}

// Invoke the first non-null hook in the 16-slot hook vtable (DAT_80480f4c, 0x4c stride) and return
// its result; returns 0 when all slots are empty.
int arcade_edit_call_first_hook(void)
{
    typedef int (*Hook)(void);
    for (int i = 0, off = 0; i < 0x10; i++, off += 0x4c) {
        Hook hook = *(Hook*)((char*)&DAT_80480f4c + off);
        if (hook != 0) {
            return hook();
        }
    }
    return 0;
}

// Return 1 when any placed cell is flagged dirty: for empty cells (field 0) the +0xb0 bit-0 flag,
// for kind 0..2 cells the flag only counts when the match-mode config (+0x20 bit-9) is clear.
// NON-MATCHING: the _savegpr_28 spill is omitted (this function takes no argument).
int arcade_edit_any_cell_dirty(void)
{
    bool dirty = false;
    for (int i = 0, off = 0; i < *(int*)(DAT_80612be4 + 0x81a4); i++, off += 0xb4) {
        int* cell = (int*)(intptr_t)(DAT_80612be4 + off + 0x81a8);
        int kind = *cell;
        if (kind == 0) {
            if ((*((unsigned char*)cell + 0xb0) & 1) != 0) {
                dirty = true;
            }
        } else if (kind >= 0 && kind < 3) {
            unsigned* cfg = match_mode_get_config(cell[1]);
            if ((cfg[8] & 0x200) == 0 && (*((unsigned char*)cell + 0xb0) & 1) != 0) {
                dirty = true;
            }
        }
        if (dirty) {
            break;
        }
    }
    return dirty ? 1 : 0;
}
