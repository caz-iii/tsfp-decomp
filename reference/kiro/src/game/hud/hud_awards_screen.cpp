// Batch 826 | Date: 2026-09-11
// HUD awards / medals menu screen and a small cluster of adjacent menu-action
// callbacks. The awards screen lays out a grid of map-completion medals: each
// map row queries award_check_map_completion() and adds a HUD list item with
// its medal icon shown only when the map is completed. Empty grid slots (the
// leading/trailing cells that do not map to a real level) get a blank padding
// item so the grid stays rectangular.
//
//   0x802dec68 hud_awards_on_select        (store focus index, push screen)
//   0x802dec9c hud_awards_populate_grid     (map-completion medal grid, 8 pages)
//   0x802def44 hud_awards_screen_build      (build the awards menu screen)
//   0x802defe0 hud_awards_get_saved_focus   (return last saved focus index)
//   0x802defe8 hud_menu_extras_on_select    (set mode 10, push screen)
//   0x802df024 hud_menu_push_screen_simple  (plain push-screen callback)
//   0x802df050 hud_menu_build_two_item_list (two-entry submenu list)
//   0x802df118 hud_menu_main_dispatch       (focus-index switch + select sound)
//   0x802df210 hud_menu_push_screen_simple2 (plain push-screen callback)
//   0x802df23c hud_screen_forward_to_handler(tail-call to shared handler)
//
// Subsystem: game (HUD menu logic)
//
// Signatures for these functions and the hud_*/get_string_by_id/sound_* helpers
// come from generated_decls.h (via hud.h). Only raw FUN_/DAT_/FLOAT_ symbols
// are declared locally below.

#include "common.h"
#include "hud.h"   // shared HUD/menu helper prototypes (generated_decls.h)

// ============================================================================
// Per-file externs for raw symbols.
// ============================================================================

// HUD push/pop context helpers (raw variants used by this screen cluster)
extern int FUN_803826dc(void);
extern int FUN_80382728(void);

// shared menu-action handler this screen forwards to (defined elsewhere)
extern void FUN_802e0a64(int screen);

// scalar float constants (menu layout)
extern float FLOAT_8061a14c;   // grid item X (or width) constant
extern float FLOAT_8061a160;   // grid item Y (or height) constant
extern float FLOAT_80619f88;   // content-bounds extent
extern float FLOAT_8061a008;   // trailing padding-item size
extern float FLOAT_80619f38;   // list item padding size

// globals
extern int DAT_80612ad8;       // awards screen: saved focus index
extern int DAT_80612ac4;       // awards screen: last-page / secondary saved index
extern int DAT_80612adc;       // main menu: saved focus index
extern int DAT_805cc614;       // pending sub-mode selector (7/10 = extras pages)
extern int DAT_806119e0;       // shared center-XY layout token

// map-completion table (queried per grid cell):
//   +0x00 (DAT_80474680) : level record pointer array (int[], 0x30 stride)
//   +0x0d (DAT_8047467d) : per-level award slot id (char, 0x30 stride)
//   +0x10 (DAT_80474690) : per-level medal icon id (0x30 stride)
extern int  DAT_80474680;
extern char DAT_8047467d;
extern int  DAT_80474690;

// ============================================================================
// 0x802dec68 | hud_awards_on_select
// Awards-menu item callback: remembers the currently focused grid index
// (screen +0x188) and pushes the next screen.
// ============================================================================
void hud_awards_on_select(int screen) {
    DAT_80612ad8 = *(int*)(screen + 0x188);
    hud_screen_push();
    return;
}

// ============================================================================
// 0x802dec9c | hud_awards_populate_grid
// Fills the map-completion medal grid. There are 8 pages of 0x12 (18) cells;
// the first and last pages reserve a few cells as blank padding (the leading 4
// / trailing 1 slots that don't correspond to real levels). For each real cell
// it adds a HUD item and, when award_check_map_completion() reports the level
// complete, marks the medal icon visible on the item.
// ============================================================================
void hud_awards_populate_grid(void) {
    int tableOff;
    int cellState;
    int* item;
    unsigned int page;
    int col;
    int mapIndex;

    FUN_803826dc();
    mapIndex = 0;
    tableOff = 0;
    hud_begin_sub_list(0, 1, 0);

    // first page: skip leading 4 cells + trailing (>0xd) cells as padding
    col = 0;
    do {
        if ((col < 4) || (0xd < col)) {
            cellState = award_check_map_completion(*(int*)((int)&DAT_80474680 + tableOff),
                                                   (int)(char)(&DAT_8047467d)[tableOff]);
            countLeadingZeros(cellState);
            hud_add_item_internal(FLOAT_8061a14c, FLOAT_8061a160);
            if (award_check_map_completion(*(int*)((int)&DAT_80474680 + tableOff),
                                           (int)(char)(&DAT_8047467d)[tableOff]) == 0) {
                item = hud_get_last_created_item();
                hud_item_set_icon_visible(item, (int)(&DAT_80474690)[tableOff]);
            }
            mapIndex = mapIndex + 1;
            tableOff = tableOff + 0x30;
        } else {
            hud_add_item_internal(FLOAT_8061a14c, FLOAT_8061a160);
        }
        col = col + 1;
    } while (col < 0x12);
    hud_end_sub_list();

    // middle pages 1..6: fully populated 0x12-cell grids
    page = 1;
    do {
        hud_begin_sub_list(0, 1, 0);
        col = 0;
        tableOff = mapIndex * 0x30;
        do {
            cellState = award_check_map_completion(*(int*)((int)&DAT_80474680 + tableOff),
                                                   (int)(char)(&DAT_8047467d)[tableOff]);
            countLeadingZeros(cellState);
            hud_add_item_internal(FLOAT_8061a14c, FLOAT_8061a160);
            if (award_check_map_completion(*(int*)((int)&DAT_80474680 + tableOff),
                                           (int)(char)(&DAT_8047467d)[tableOff]) == 0) {
                item = hud_get_last_created_item();
                hud_item_set_icon_visible(item, (int)(&DAT_80474690)[tableOff]);
            }
            col = col + 1;
            tableOff = tableOff + 0x30;
            mapIndex = mapIndex + 1;
        } while (col < 0x12);
        hud_end_sub_list();
        page = page + 1;
    } while (page < 8);

    // last page: skip leading 1 cell + trailing (>0x10) cells as padding
    hud_begin_sub_list(0, 1, 0);
    col = 0;
    tableOff = mapIndex * 0x30;
    do {
        if ((col < 1) || (0x10 < col)) {
            hud_add_item_internal(FLOAT_8061a14c, FLOAT_8061a160);
        } else {
            cellState = award_check_map_completion(*(int*)((int)&DAT_80474680 + tableOff),
                                                   (int)(char)(&DAT_8047467d)[tableOff]);
            countLeadingZeros(cellState);
            hud_add_item_internal(FLOAT_8061a14c, FLOAT_8061a160);
            if (award_check_map_completion(*(int*)((int)&DAT_80474680 + tableOff),
                                           (int)(char)(&DAT_8047467d)[tableOff]) == 0) {
                item = hud_get_last_created_item();
                hud_item_set_icon_visible(item, (int)(&DAT_80474690)[tableOff]);
            }
            tableOff = tableOff + 0x30;
        }
        col = col + 1;
    } while (col < 0x12);
    hud_end_sub_list();

    FUN_80382728();
    return;
}

// ============================================================================
// 0x802def44 | hud_awards_screen_build
// Builds the awards menu screen: title from string 0x407, the on-select
// callback (hud_awards_on_select), lays out content bounds, populates the
// medal grid, appends a trailing padding item, restores the saved focus index
// (screen +0x188), then centers and finalizes the frame.
// ============================================================================
void hud_awards_screen_build(int screen) {
    int title;

    title = get_string_by_id(0x407);
    hud_element_init_from_layout(0xa000, (void*)title, (int)&hud_awards_on_select, 0);
    hud_set_content_bounds(FLOAT_80619f88, FLOAT_80619f88, 1);
    hud_set_context_property(0x30000);
    hud_awards_populate_grid();
    hud_add_padding_item(FLOAT_8061a008);
    *(int*)(screen + 0x188) = DAT_80612ad8;
    hud_set_focus_by_id_nth(screen, *(int*)(screen + 0x188), 0);
    hud_element_apply_center_xy((int*)DAT_806119e0);
    hud_element_finalize_frame();
    return;
}

// ============================================================================
// 0x802defe0 | hud_awards_get_saved_focus
// Returns the awards screen's secondary saved index.
// ============================================================================
int hud_awards_get_saved_focus(void) {
    return DAT_80612ac4;
}

// ============================================================================
// 0x802defe8 | hud_menu_extras_on_select
// Extras-menu item callback: selects extras sub-mode 10 and pushes the screen.
// ============================================================================
void hud_menu_extras_on_select(void) {
    DAT_805cc614 = 10;
    hud_screen_push();
    return;
}

// ============================================================================
// 0x802df024 | hud_menu_push_screen_simple
// Plain menu callback: just push the next screen.
// ============================================================================
void hud_menu_push_screen_simple(void) {
    hud_screen_push();
    return;
}

// ============================================================================
// 0x802df050 | hud_menu_build_two_item_list
// Builds a two-entry submenu list (title 0x403, entries 0x408 / 0x409),
// records the parent screen's list-length field (+0x214 = 9), applies the
// standard item padding + context property, then centers and finalizes.
// ============================================================================
void hud_menu_build_two_item_list(int screen) {
    int title;

    title = get_string_by_id(0x403);
    hud_element_init_from_layout(0x800a000, (void*)title, 0, 0);
    *(int*)(screen + 0x214) = 9;
    hud_set_item_padding_mode(FLOAT_80619f38, 1, 1, 1);
    hud_set_context_property(0x32200);
    get_string_by_id(0x408);
    hud_add_list_item();
    get_string_by_id(0x409);
    hud_add_list_item();
    hud_element_apply_center_xy((int*)DAT_806119e0);
    hud_element_finalize_frame();
    return;
}

// ============================================================================
// 0x802df118 | hud_menu_main_dispatch
// Main-menu select handler: plays the select sound (0xd1e), remembers the
// focused index (+0x188), then branches per index. Case 2 additionally selects
// sub-mode 7 before pushing; every valid case pushes the next screen. Indices
// 3 and >=8 fall through with no action.
// ============================================================================
void hud_menu_main_dispatch(int screen) {
    int soundParams[12];

    sound_init_play_params((int)soundParams);
    soundParams[0] = 0xd1e;
    sound_play_request(soundParams);
    DAT_80612adc = *(int*)(screen + 0x188);
    switch (*(int*)(screen + 0x188)) {
    case 0:
        hud_screen_push();
        break;
    case 1:
        hud_screen_push();
        break;
    case 2:
        DAT_805cc614 = 7;
        hud_screen_push();
        break;
    case 4:
        hud_screen_push();
        break;
    case 5:
        hud_screen_push();
        break;
    case 6:
        hud_screen_push();
        break;
    case 7:
        hud_screen_push();
        break;
    }
    return;
}

// ============================================================================
// 0x802df210 | hud_menu_push_screen_simple2
// Plain menu callback: push the next screen (distinct entry point).
// ============================================================================
void hud_menu_push_screen_simple2(void) {
    hud_screen_push();
    return;
}

// ============================================================================
// 0x802df23c | hud_screen_forward_to_handler
// Thin wrapper that forwards the screen to the shared menu handler
// FUN_802e0a64 (the common close/tick dispatcher used by this screen family).
// ============================================================================
void hud_screen_forward_to_handler(int screen) {
    FUN_802e0a64(screen);
    return;
}
