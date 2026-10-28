// Batch 827 | Date: 2026-09-11
// HUD options menu list + the profile-name text-entry (inline keyboard) editor
// screen and its per-row renderers. Continuation of the menu family
// (batch_817-826).
//
//   0x802df25c hud_options_menu_build         (options list screen builder)
//   0x802df568 hud_profile_row_fill_bg        (fill the row background rect)
//   0x802df5ec hud_profile_name_row_render    (render one profile name row)
//   0x802df778 hud_profile_name_masked_render (render masked '*' name row)
//   0x802df868 hud_profile_slot0_is_empty     (slot-0 name empty test)
//   0x802df898 hud_profile_slot1_is_empty     (slot-1 name empty test)
//   0x802df8cc hud_profile_name_edit_confirm  (keyboard confirm -> show screen)
//   0x802df904 hud_profile_name_edit_confirm_sfx (confirm + click sound)
//   0x802df96c hud_profile_name_edit_screen_build (inline-keyboard editor screen)
//   0x802dfa84 hud_profile_name_edit_open      (open the name editor)
//
// Subsystem: game (HUD menu logic)
//
// hud_*/text_*/get_string_by_id/sound_*/keyboard_* signatures resolve via
// hud.h (generated_decls.h). The text/color helpers that only exist as
// permissive externs, and all raw FUN_/DAT_/FLOAT_ symbols, are declared
// locally below.

#include "common.h"
#include "hud.h"   // shared HUD/menu helper prototypes (generated_decls.h)

// ============================================================================
// Per-file externs for raw / externs-only symbols.
// ============================================================================

// color_scale_lo / text_get_char_width / text_draw_char_shadowed /
// text_set_color_rgb resolve via hud.h -> generated_decls.h ->
// generated_externs.h (permissive `int f(...)` declarations).

// raw helpers
extern unsigned int FUN_803825c0(double v);  // float -> clamped color byte
extern int FUN_8038b5c8(int strPtr);         // string length / non-empty probe
extern void FUN_802df568(int actor, int element, int rect);  // row bg fill (defined below, fwd for callbacks)

// globals
extern int   DAT_8060f948;      // active screen/layout mode id
extern float FLOAT_8060f958;    // profile-row layout scale
extern int   DAT_80611014;      // unlock progress tier (gates locked entries)
extern int   DAT_80612adc;      // main-menu saved focus index
extern int   DAT_806119e0;      // shared center-XY layout token
extern unsigned char DAT_805ceb8c; // per-slot "name set" bitmask
extern int   DAT_805ce80c;      // profile name string table base (0x40 stride)
extern int   DAT_805ce94c;      // profile name max-length table (int[], 0x04 stride)
extern int   DAT_805ce98c;      // active profile slot index (edit target)
extern int   DAT_805ceb90;      // editor: owning screen ptr
extern int   DAT_80611018;      // editor layout descriptor
extern int   DAT_80611a54;      // click-sound enabled flag
extern int*  DAT_80611a58;      // click-sound id ptr
extern int   DAT_80612af4;      // name-editor: child screen id to show on confirm

// float layout constants
extern float FLOAT_80619fc0;    // default item padding
extern float FLOAT_80619f38;    // compact item padding / inset
extern float FLOAT_8061a170;    // row left extent
extern float FLOAT_80619fbc;    // row right extent / inset
extern float FLOAT_80619f6c;    // name right margin
extern float FLOAT_80619fac;    // selected alpha input scale
extern float FLOAT_80619ee8;    // white (1.0) restore color
extern float FLOAT_80619fe0;    // editor scroll amount
extern float FLOAT_8061a174;    // editor Y position

// ============================================================================
// 0x802df25c | hud_options_menu_build
// Builds the options menu list. Loads saved settings, then adds the standard
// entries (each with an icon): sound/audio (0x3ff/0x400 depending on
// DAT_80611014==2), plus display/controls/etc. Some entries switch to a
// "locked" icon variant and are marked locked when the unlock tier
// (DAT_80611014) is too low. Restores the saved focus index and registers the
// global A/B handlers (0x19c7 / 0xe4d).
// ============================================================================
void hud_options_menu_build(int screen) {
    float pad;
    bool compact;
    int strId;
    int item;

    menu_options_settings_load();
    strId = get_string_by_id(0x35f);
    hud_element_init_from_layout(0, (void*)strId, (int)&hud_menu_main_dispatch,
                                 (int)&hud_menu_push_screen_simple2);

    compact = (DAT_8060f948 == 4) || (DAT_8060f948 == 1);
    pad = compact ? FLOAT_80619f38 : FLOAT_80619fc0;
    hud_set_item_padding_mode(pad, 1, 1, 1);
    hud_set_context_property(0x32200);
    *(int*)(screen + 0x214) = 9;

    // entry 0: audio (label varies by unlock tier)
    strId = (DAT_80611014 == 2) ? 0x400 : 0x3ff;
    get_string_by_id(strId);
    hud_add_list_item();
    if (DAT_80611014 < 3) {
        strId = get_string_by_id(0x1647);
        item = (int)hud_get_last_created_item();
        hud_item_set_icon((int*)item, strId);
    } else {
        strId = get_string_by_id(0x1653);
        item = (int)hud_get_last_created_item();
        hud_item_set_icon((int*)item, strId);
        item = (int)hud_get_last_created_item();
        hud_item_set_locked(item, 1);
    }

    // entry 1
    get_string_by_id(0x3fd);
    hud_add_list_item();
    strId = get_string_by_id(0x1648);
    item = (int)hud_get_last_created_item();
    hud_item_set_icon((int*)item, strId);

    // entry 2 (label/icon vary by unlock tier)
    get_string_by_id(0x404);
    hud_add_list_item();
    if (DAT_80611014 < 2) {
        strId = get_string_by_id(0x1649);
        item = (int)hud_get_last_created_item();
        hud_item_set_icon((int*)item, strId);
    } else {
        strId = get_string_by_id(0x1654);
        item = (int)hud_get_last_created_item();
        hud_item_set_icon((int*)item, strId);
        item = (int)hud_get_last_created_item();
        hud_item_set_locked(item, 1);
    }

    // entries 3..6
    get_string_by_id(0x364);
    hud_add_list_item();
    strId = get_string_by_id(0x164c);
    item = (int)hud_get_last_created_item();
    hud_item_set_icon((int*)item, strId);

    get_string_by_id(0x3d3);
    hud_add_list_item();
    strId = get_string_by_id(0x164d);
    item = (int)hud_get_last_created_item();
    hud_item_set_icon((int*)item, strId);

    get_string_by_id(0x402);
    hud_add_list_item();
    strId = get_string_by_id(0x164e);
    item = (int)hud_get_last_created_item();
    hud_item_set_icon((int*)item, strId);

    get_string_by_id(0x403);
    hud_add_list_item();
    strId = get_string_by_id(0x1650);
    item = (int)hud_get_last_created_item();
    hud_item_set_icon((int*)item, strId);

    if ((-1 < DAT_80612adc) && (DAT_80612adc < 8)) {
        hud_set_focus_by_id(screen, DAT_80612adc);
    }
    hud_element_apply_center_xy((int*)DAT_806119e0);
    strId = get_string_by_id(0x19c7);
    hud_screen_register_handler_global(0, strId, 0, 0);
    strId = get_string_by_id(0xe4d);
    hud_screen_register_handler_global(1, strId, 0, 0);
    return;
}

// ============================================================================
// 0x802df568 | hud_profile_row_fill_bg
// Fills the translucent background rectangle behind a profile-name row. The
// row rect is at (rect+4 .. rect+0xc); left/right extents scale by the shared
// row scale FLOAT_8060f958.
// ============================================================================
void hud_profile_row_fill_bg(int actor, int element, int rect) {
    (void)actor; (void)element;
    gx_fill_rect_clipped(
        (unsigned int)(int)-(FLOAT_8061a170 * FLOAT_8060f958 - *(float*)(rect + 8)),
        (unsigned int)(int)(FLOAT_80619f38 + *(float*)(rect + 4)),
        (int)-(FLOAT_80619fbc * FLOAT_8060f958 - *(float*)(rect + 8)),
        (int)(*(float*)(rect + 0xc) - FLOAT_80619f38),
        0x283c6440);
    return;
}

// ============================================================================
// 0x802df5ec | hud_profile_name_row_render
// Renders one profile-name list row: fills the background, fades the name in,
// and draws the name right-aligned. If the slot's "name set" bit
// (DAT_805ceb8c) is clear it draws plain; if set it draws a shadowed name with
// a color-scaled highlight (selected animation via element+0xf0 alpha).
// The two register-recovered values Ghidra modeled via __save_gpr_28 are the
// incoming actor (high) and element (low) pointers.
// ============================================================================
void hud_profile_name_row_render(int actor, int element, int rect) {
    int slot = *(int*)(element + 0x14);
    unsigned int nameSetMask = (unsigned int)DAT_805ceb8c;

    hud_profile_row_fill_bg(actor, element, rect);
    text_apply_alpha_fade(
        (double)((FLOAT_8061a170 * FLOAT_8060f958 - FLOAT_80619fbc * FLOAT_8060f958)
                 - FLOAT_80619fbc * FLOAT_8060f958),
        (unsigned char*)(&DAT_805ce80c + slot * 0x40), 0, 1);

    if ((nameSetMask & (1u << slot)) == 0) {
        unsigned int col = FUN_803825c0((double)(FLOAT_80619fac * *(float*)(actor + 0xf0)));
        double w = text_get_char_width((unsigned char*)(&DAT_805ce80c + slot * 0x40));
        text_draw_char(
            (double)(float)((double)(*(float*)(rect + 8)
                - (FLOAT_80619fbc * FLOAT_8060f958 + FLOAT_80619f6c * FLOAT_8060f958)) - w),
            (double)*(float*)(rect + 4),
            (unsigned char*)(&DAT_805ce80c + slot * 0x40),
            (col & 0xff) | 0x64646400);
    } else {
        unsigned int hi = color_scale_lo((double)*(float*)(actor + 0xf0), 0x140a0a64);
        unsigned int shadow = color_scale_lo((double)*(float*)(actor + 0xf0), 0x7f323264);
        double w = text_get_char_width((unsigned char*)(&DAT_805ce80c + slot * 0x40));
        text_draw_char_shadowed(
            (double)(float)((double)(*(float*)(rect + 8)
                - (FLOAT_80619fbc * FLOAT_8060f958 + FLOAT_80619f6c * FLOAT_8060f958)) - w),
            (double)*(float*)(rect + 4),
            (unsigned char*)(&DAT_805ce80c + slot * 0x40),
            shadow, hi);
    }
    double white = (double)FLOAT_80619ee8;
    text_set_color_rgb(white, white, white);
    return;
}

// ============================================================================
// 0x802df778 | hud_profile_name_masked_render
// Renders a profile-name row as a masked string ("****" of the name's length,
// via FUN_8038b5c8 giving the length), right-aligned, after filling the row
// background. Used for hidden/locked name display.
// ============================================================================
void hud_profile_name_masked_render(int actor, int element, int rect) {
    unsigned char masked[40];
    int len = FUN_8038b5c8((int)(&DAT_805ce80c + *(int*)(element + 0x14) * 0x40));

    hud_profile_row_fill_bg(actor, element, rect);

    int i = 0;
    while (len > 0) {
        masked[i] = 0x2a;   // '*'
        i++;
        len--;
    }
    masked[i] = 0;

    unsigned int col = FUN_803825c0((double)(FLOAT_80619fac * *(float*)(actor + 0xf0)));
    double w = text_get_char_width(masked);
    text_draw_char(
        (double)((float)((double)*(float*)(rect + 8) - w)
            - (FLOAT_80619fbc * FLOAT_8060f958 + FLOAT_80619f6c * FLOAT_8060f958)),
        (double)*(float*)(rect + 4),
        masked,
        (col & 0xff) | 0x64646400);
    return;
}

// ============================================================================
// 0x802df868 | hud_profile_slot0_is_empty
// Returns 1 when profile-name slot 0 (string at 0x805ce80c) is empty:
// FUN_8038b5c8 returns 0 for an empty string, and countLeadingZeros(0)>>5 == 1.
// ============================================================================
unsigned int hud_profile_slot0_is_empty(void) {
    int len = FUN_8038b5c8((int)0x805ce80c);
    return countLeadingZeros(len) >> 5;
}

// ============================================================================
// 0x802df898 | hud_profile_slot1_is_empty
// Returns 1 when profile-name slot 1 (string at 0x805ce84c) is empty.
// ============================================================================
unsigned int hud_profile_slot1_is_empty(void) {
    int len = FUN_8038b5c8((int)0x805ce84c);
    return countLeadingZeros(len) >> 5;
}

// ============================================================================
// 0x802df8cc | hud_profile_name_edit_confirm
// Inline-keyboard confirm callback: re-inits the keyboard buffer pool and shows
// the pending child screen (screen+0x2c) with the saved child id DAT_80612af4.
// ============================================================================
void hud_profile_name_edit_confirm(int screen) {
    keyboard_buffer_pool_init();
    hud_screen_show_by_id(*(int*)(screen + 0x2c), DAT_80612af4);
    return;
}

// ============================================================================
// 0x802df904 | hud_profile_name_edit_confirm_sfx
// Same as hud_profile_name_edit_confirm, but first plays the configured click
// sound (DAT_80611a58[0]) when click sounds are enabled (DAT_80611a54).
// ============================================================================
void hud_profile_name_edit_confirm_sfx(int screen) {
    int soundParams[12];

    if (DAT_80611a54 != 0) {
        int soundId = *DAT_80611a58;
        sound_init_play_params((int)soundParams);
        soundParams[0] = soundId;
        sound_play_request(soundParams);
    }
    keyboard_buffer_pool_init();
    hud_screen_show_by_id(*(int*)(screen + 0x2c), DAT_80612af4);
    return;
}

// ============================================================================
// 0x802df96c | hud_profile_name_edit_screen_build
// Builds the inline profile-name editor screen: opens an inline keyboard bound
// to the active profile slot's name buffer (DAT_805ce80c + slot*0x40) with its
// max length (DAT_805ce94c[slot]) and the confirm callback
// hud_profile_name_edit_confirm. Positions/centers the frame, and registers the
// A/B/X global handlers (0x35a / 0x19c8 / 0xe5c). Remembers the owning screen.
// ============================================================================
void hud_profile_name_edit_screen_build(int screen) {
    int strId;

    hud_element_init_from_layout(0, &DAT_80611018, 0, (int)&hud_profile_name_edit_confirm_sfx);
    hud_element_scroll_vertical((int)FLOAT_80619fe0, 0);
    keyboard_open_inline(
        screen,
        (int)(&DAT_805ce80c + DAT_805ce98c * 0x40),
        *(int*)(&DAT_805ce94c + DAT_805ce98c * 4),
        0, 0x400,
        (int)&hud_profile_name_edit_confirm, 2);
    hud_element_apply_scroll();
    hud_set_focus_by_id(screen, 0);
    hud_element_set_pos_y((double)FLOAT_8061a174, DAT_806119e0);
    hud_element_apply_center_x(DAT_806119e0);
    hud_element_finalize_frame();
    strId = get_string_by_id(0x35a);
    hud_screen_register_handler_global(0, strId, 0, 0);
    strId = get_string_by_id(0x19c8);
    hud_screen_register_handler_global(1, strId, 0, 0);
    strId = get_string_by_id(0xe5c);
    hud_screen_register_handler_global(3, strId, 0, 0);
    DAT_805ceb90 = screen;
    return;
}

// ============================================================================
// 0x802dfa84 | hud_profile_name_edit_open
// Opens the name editor: stores the child id to show on confirm (DAT_80612af4)
// and shows the editor screen (screen+0x2c) with the editor screen id.
// ============================================================================
void hud_profile_name_edit_open(int screen, int childId) {
    DAT_80612af4 = childId;
    hud_screen_show_by_id(*(int*)(screen + 0x2c), (int)0x80477c7c);
    return;
}
