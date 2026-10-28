// Address: 0x802dad64 | Name: menu_cheats_screen | Date: 2026-09-11T07:35:00Z
//
// Front-end "cheats / gameplay-modifiers" menu family (batch_823), continuation
// of the menu screens in batch_817-822. Contains:
//   - the options list screen build + its per-frame refresh callback,
//   - the award-unlock list screen (shows which awards/cheats are unlocked),
//   - the cheat toggle screen (per-modifier on/off rows) with its refresh/apply
//     logic, and
//   - the status-bar render callback that draws the OK / Cancel glyphs plus the
//     two cheat-indicator sprites (invert / mirror style icons).
//
// Address range: 0x802dad64 - 0x802db86c

#include "common.h"

// ---- Per-file externs for raw symbols --------------------------------------

// Screen/state globals.
extern int   DAT_805cc61c;      // active savegame slot id
extern int   DAT_805cc614;      // pending screen mode selector
extern int   DAT_805cc624;      // pending screen index payload
extern u32   DAT_80612600;      // cheat-availability mask (bit per modifier slot)
extern int   DAT_80611014;      // active player count
extern int   DAT_80612614;      // progress/completion percent
extern int   DAT_80612ac4;      // memcard session slot handle
extern int  *DAT_806119e0;      // layout centering context
extern int  *DAT_80610a20;      // sprite material record table base
extern int   DAT_806109c4;      // screen width  (px)
extern int   DAT_806109c8;      // screen height (px)

extern float FLOAT_80619f38;
extern float FLOAT_80619f08;
extern float FLOAT_80619f0c;
extern float FLOAT_8061a13c;
extern float FLOAT_80611a64;
extern float FLOAT_80611a6c;
extern double DOUBLE_80619f00;

extern u8    DAT_80471c60[];    // award label-id table (stride 0x24)
extern u8    DAT_8044ee14[];    // modifier label-id table (stride 0x1c, short field)

extern void *PTR_FUN_804771e8;  // memcard-all session callback vtable

// Frame-actor scope thunks (head returns the current screen element ptr in r3;
// tails close the frame scope). Modelled per prior batches.
extern int   FUN_803826d8(void);
extern int   FUN_803826e8(void);
extern int   FUN_803826f0(void);
extern uint  FUN_803825c0(double v);
extern void  FUN_80382724(void);
extern void  FUN_80382734(void);
extern void  FUN_8038273c(void);

// HUD / screen builder helpers.
extern int   hud_element_is_closing(int element);
extern int   hud_find_child_by_id(int element, int id);
extern int   get_string_by_id(int id);
extern int   stats_get_player_ptr(int slot);
extern void  hud_item_on_value_change(void);
extern void  hud_screen_set_handler_param_b(int element, int slot, int value);
extern void  memcard_session_begin_all(int slot, void *callbacks);
extern void  hud_element_init_from_layout(int flags, int titleStr, int a, void *callback);
extern void  hud_set_item_padding_mode(double pad, int a, int b, int c);
extern void  hud_set_context_property(int prop);
extern void  hud_add_list_item(void);
extern int   gameplay_modifier_apply_all(void);
extern void  hud_add_callback_item_default(int a);
extern void  hud_item_add_spacer(double h, int flags);
extern int   player_get_entity_ptr(int index);
extern void  hud_event_add_string(int str);
extern int   hud_get_last_created_item(void);
extern void  hud_item_set_change_callback(int item, void *cb);
extern void  hud_element_apply_center_xy(int *ctx);
extern void  hud_screen_push(void);
extern int   award_is_unlocked(int index);
extern void  hud_screen_register_handler_global(int slot, int str, int a, int b);
extern void  hud_element_scroll_vertical(double h, int a);
extern void  hud_element_apply_scroll(void);
extern int   award_modifier_check_slot(int slot);
extern void  gameplay_modifier_clear(int slot);
extern void  gameplay_modifier_set(int slot);
extern void  hud_clear_destructor(void);
extern void  hud_event_add_ok_cancel(void);
extern int   hud_get_focused_item(int element);
extern void  gx_set_scissor_rect(int x, int y, int w, int h);
extern void  text_set_font_table(int table);
extern uint  gameplay_modifier_get_flags(int slot);
extern int   sprite_get_texture_id(int id);
extern void  gx_set_active_material(int material);
extern void  gx_draw_sprite_rect(int x0, int y0, int x1, int y1, int u0, int v0, int u1, int v1);
extern int   text_get_char_width(byte *str);
extern void  text_draw_char(double x, double y, byte *str, uint color);

// Change-callback target referenced by address in menu_cheats_options_screen_build.
// (Code label inside menu_cheats_screen_refresh; typed as a byte so &LAB is valid.)
extern char  LAB_802dad50;

// ---------------------------------------------------------------------------

// 0x802dad64 - Per-frame refresh of the cheats options screen: set the header
// label (0xe4b), refresh the player stat item, and mirror the screen's index
// payload into the handler enable/disable flag.
void menu_cheats_screen_refresh(int element)
{
    int closing = hud_element_is_closing(element);
    if (closing == 0) {
        int child = hud_find_child_by_id(element, 1);
        if (child != 0) {
            int str = get_string_by_id(0xe4b);
            *(int *)(child + 0x20) = str;
        }
        child = hud_find_child_by_id(element, 2);
        if (child != 0) {
            stats_get_player_ptr(DAT_805cc61c);
            hud_item_on_value_change();
        }
    }
    if (*(int *)(element + 0x188) == 0) {
        hud_screen_set_handler_param_b(element, 0, 1);
    } else {
        hud_screen_set_handler_param_b(element, 0, 0);
    }
}

// 0x802dae24 - Reset the active slot and start a memcard "load all" session.
void menu_cheats_memcard_load_begin(void)
{
    DAT_805cc61c = 0;
    memcard_session_begin_all(DAT_80612ac4, (void *)&PTR_FUN_804771e8);
}

// 0x802dae60 - Build the cheats options list screen: title 0x402, the main
// options rows, an optional "apply all modifiers" row, and (for >1 player) a
// per-player selector list whose change callback is menu_cheats_screen_refresh.
void menu_cheats_options_screen_build(int element)
{
    int str = get_string_by_id(0x402);
    hud_element_init_from_layout(0x8008000, str, 0, (void *)menu_cheats_memcard_load_begin);
    *(int *)(element + 0x214) = 9;
    hud_set_item_padding_mode((double)FLOAT_80619f38, 1, 1, 1);

    hud_set_context_property(0x32200);
    get_string_by_id(0x17d4);
    hud_add_list_item();

    if (gameplay_modifier_apply_all() != 0) {
        get_string_by_id(0x19b0);
        hud_add_list_item();
    }
    hud_add_callback_item_default(0);
    hud_set_context_property(0x26200);

    if (1 < DAT_80611014) {
        get_string_by_id(0x395);
        hud_add_list_item();
        hud_item_add_spacer((double)FLOAT_80619f08, 0xa000);
        for (int i = 0; i < DAT_80611014; i++) {
            int entity = player_get_entity_ptr(i);
            hud_event_add_string(entity);
        }
        int item = hud_get_last_created_item();
        hud_item_set_change_callback(item, (void *)&LAB_802dad50);
        hud_get_last_created_item();
        hud_item_on_value_change();
    }

    get_string_by_id(0x1749);
    hud_add_list_item();
    hud_add_list_item();
    hud_element_apply_center_xy(DAT_806119e0);
}

// 0x802db010 - Queue the award-list screen (mode 0xe) with the selected index.
void menu_cheats_push_award_list(int element)
{
    DAT_805cc614 = 0xe;
    DAT_805cc624 = *(int *)(element + 0x188);
    hud_screen_push();
}

// 0x802db054 - Build the award-unlock list screen. Counts unlocked awards
// (0..0x13); if none, shows an empty-state message, otherwise lists each
// unlocked award's label from DAT_80471c60 (stride 0x24) in a scrolling list.
void menu_cheats_award_list_build(void)
{
    int element = FUN_803826e8();

    int unlockedCount = 0;
    for (int i = 0; i < 0x14; i++) {
        if (award_is_unlocked(i)) {
            unlockedCount++;
        }
    }

    int str = get_string_by_id(0x19aa);
    hud_element_init_from_layout(0x8002000, str, 0, 0);
    *(int *)(element + 0x214) = 1;
    hud_set_item_padding_mode((double)FLOAT_80619f38, 1, 1, 1);

    if (unlockedCount == 0) {
        get_string_by_id(0x199d);
        hud_add_list_item();
        hud_screen_register_handler_global(1, get_string_by_id(0x19c8), 0, 0);
        hud_screen_register_handler_global(2, get_string_by_id(0x3d3), 0, 0);
    } else {
        hud_element_scroll_vertical((double)FLOAT_80619f08, 9);
        int rowOff = 0;
        for (int i = 0; i < 0x14; i++) {
            if (award_is_unlocked(i)) {
                get_string_by_id(*(int *)(&DAT_80471c60[rowOff]));
                hud_add_list_item();
            }
            rowOff += 0x24;
        }
        hud_element_apply_scroll();
        hud_screen_register_handler_global(0, get_string_by_id(0x19c7), 0, 0);
        hud_screen_register_handler_global(1, get_string_by_id(0x19c8), 0, 0);
        hud_screen_register_handler_global(2, get_string_by_id(0x3d3), 0, 0);
    }

    hud_element_apply_center_xy(DAT_806119e0);
    FUN_80382734();
}

// 0x802db220 - Refresh cheat-toggle row states from the current modifier flags.
// Row 0 encodes a 3-state (off / A / A+B) value; rows 2..0xf are simple on/off
// gated by both the availability mask (DAT_80612600) and award_modifier_check_slot.
void menu_cheats_toggle_refresh(void)
{
    int element = FUN_803826e8();
    int row0 = hud_find_child_by_id(element, 0);
    if (row0 != 0) {
        *(int *)(row0 + 200) = 0;
        if ((DAT_80612600 & 2) != 0 && award_modifier_check_slot(1) != 0) {
            *(int *)(row0 + 200) = 1;
            goto rows;
        }
        if ((DAT_80612600 & 1) != 0 && award_modifier_check_slot(0) != 0) {
            *(int *)(row0 + 200) = 1;
            if (award_modifier_check_slot(1) != 0) {
                *(int *)(row0 + 200) = 2;
            }
        }
    }
rows:
    for (int slot = 2; slot < 0x10; slot++) {
        int row = hud_find_child_by_id(element, slot);
        if (row != 0) {
            byte on = 0;
            if (((DAT_80612600 & (1 << slot)) != 0) &&
                (award_modifier_check_slot(slot) != 0)) {
                on = 1;
            }
            *(uint *)(row + 200) = (uint)on;
        }
    }
    FUN_80382734();
}

// 0x802db328 - Apply the cheat-toggle row states back into the gameplay
// modifiers (inverse of menu_cheats_toggle_refresh), then re-read to refresh UI.
void menu_cheats_toggle_apply(int element)
{
    int row0 = hud_find_child_by_id(element, 0);
    gameplay_modifier_clear(0);
    gameplay_modifier_clear(1);
    if (row0 != 0) {
        if (*(int *)(row0 + 200) == 1) {
            if (award_modifier_check_slot(1) == 0) {
                gameplay_modifier_set(0);
            } else {
                gameplay_modifier_set(1);
            }
        } else if (*(int *)(row0 + 200) == 2) {
            gameplay_modifier_set(0);
        }
    }
    for (int slot = 2; slot < 0x10; slot++) {
        int row = hud_find_child_by_id(element, slot);
        if (row != 0) {
            if (*(int *)(row + 200) == 1) {
                gameplay_modifier_set(slot);
            } else {
                gameplay_modifier_clear(slot);
            }
        }
    }
    menu_cheats_toggle_refresh();
}

// 0x802db40c - Build the cheat-toggle screen: an optional "special" header block
// when slot 0/1 is available, then one row per available modifier slot (2..0xf,
// excluding 0xc) labelled from DAT_8044ee14 (stride 0x1c). Registers back / reset
// handlers and seeds the initial states via menu_cheats_toggle_refresh.
void menu_cheats_toggle_screen_build(void)
{
    int element = FUN_803826f0();

    int str = get_string_by_id(0x19b0);
    hud_element_init_from_layout(0x48002000, str, 0, 0);
    *(int *)(element + 0x214) = 9;
    hud_clear_destructor();
    hud_set_item_padding_mode((double)FLOAT_80619f38, 1, 1, 1);
    hud_set_context_property(0x22000);

    if ((award_modifier_check_slot(0) != 0) || (award_modifier_check_slot(1) != 0)) {
        get_string_by_id(0x19b6);
        hud_add_list_item();
        hud_item_add_spacer((double)FLOAT_80619f08, 0);
        hud_event_add_string(get_string_by_id(0x19b8));
        if (award_modifier_check_slot(1) != 0) {
            hud_event_add_string(get_string_by_id(0x19b7));
        }
        if (award_modifier_check_slot(0) != 0) {
            hud_event_add_string(get_string_by_id(0x19b9));
        }
    }

    int labelOff = 0;
    for (uint slot = 0; (int)slot < 0x10; slot++) {
        if ((award_modifier_check_slot(slot) != 0) && (1 < slot) && (slot != 0xc)) {
            get_string_by_id((int)*(short *)(&DAT_8044ee14[labelOff]));
            hud_add_list_item();
            hud_item_add_spacer((double)FLOAT_80619f08, 0);
            hud_event_add_ok_cancel();
        }
        labelOff += 0x1c;
    }

    hud_element_apply_center_xy(DAT_806119e0);
    hud_screen_register_handler_global(1, get_string_by_id(0x19c8), 0, 0);
    if (100 < DAT_80612614) {
        hud_screen_register_handler_global(2, get_string_by_id(0x3d3), 0, 0);
    }
    menu_cheats_toggle_refresh();
    FUN_8038273c();
}

// 0x802db5e0 - Render callback for the cheat status bar: draws two indicator
// sprites (id 0x15a "on" / 0x15b "off" depending on modifier flag bits) and the
// OK (0x401) / Cancel-ish (0x3fc) glyph labels, right-aligned along the top edge.
void menu_cheats_status_bar_render(void)
{
    int element = FUN_803826d8();
    int focused = hud_get_focused_item(element);
    int modifierIndex = *(int *)(focused + 0x14);

    uint fade = FUN_803825c0((double)(FLOAT_80619f0c * *(float *)(element + 0xf0)));
    uint color = fade & 0xff | 0x64646400;
    uint y = (uint)(FLOAT_8061a13c * FLOAT_80611a64 + FLOAT_80611a6c);

    gx_set_scissor_rect(0, 0, DAT_806109c4, DAT_806109c8);
    text_set_font_table(0x15);

    int screenW = DAT_806109c4;
    uint rightEdge = DAT_806109c4 - 0x19;
    int iconX = DAT_806109c4 - 0x2d;

    // First indicator sprite: modifier flag bit 1 selects "on" (0x15a) vs "off".
    uint flagsA = gameplay_modifier_get_flags(modifierIndex);
    int spriteA = ((flagsA & 2) != 0) ? 0x15a : 0x15b;
    spriteA = sprite_get_texture_id(spriteA);
    gx_set_active_material(spriteA);
    int recA = *DAT_80610a20 + spriteA * 0x58;
    FUN_803825c0((double)(FLOAT_80619f0c * *(float *)(element + 0xf0)));
    gx_draw_sprite_rect(iconX, y, rightEdge, y + 0x18, 0, 0,
                        (uint)*(ushort *)(recA + 0x4e) << 4,
                        (uint)*(ushort *)(recA + 0x52) << 4);

    byte *okStr = (byte *)get_string_by_id(0x401);
    double okW = text_get_char_width(okStr);
    uint okX = (uint)((double)(float)((double)CONCAT44(0x43300000, screenW - 0x32U ^ 0x80000000) -
                                      DOUBLE_80619f00) - okW);
    text_draw_char((double)(float)((double)CONCAT44(0x43300000, okX ^ 0x80000000) - DOUBLE_80619f00),
                   (double)(float)((double)CONCAT44(0x43300000, y ^ 0x80000000) - DOUBLE_80619f00),
                   okStr, color);

    // Second indicator sprite: modifier flag bit 2 selects "on" vs "off".
    uint flagsB = gameplay_modifier_get_flags(modifierIndex);
    int spriteB = ((flagsB & 4) != 0) ? 0x15a : 0x15b;
    spriteB = sprite_get_texture_id(spriteB);
    gx_set_active_material(spriteB);
    int recB = *DAT_80610a20 + spriteB * 0x58;
    FUN_803825c0((double)(FLOAT_80619f0c * *(float *)(element + 0xf0)));
    gx_draw_sprite_rect(okX - 0x24, y, okX - 0x10, y + 0x18, 0, 0,
                        (uint)*(ushort *)(recB + 0x4e) << 4,
                        (uint)*(ushort *)(recB + 0x52) << 4);

    byte *cancelStr = (byte *)get_string_by_id(0x3fc);
    double cancelW = text_get_char_width(cancelStr);
    uint cancelX = (uint)((double)(float)((double)CONCAT44(0x43300000, okX - 0x29 ^ 0x80000000) -
                                          DOUBLE_80619f00) - cancelW);
    text_draw_char((double)(float)((double)CONCAT44(0x43300000, cancelX ^ 0x80000000) - DOUBLE_80619f00),
                   (double)(float)((double)CONCAT44(0x43300000, y ^ 0x80000000) - DOUBLE_80619f00),
                   cancelStr, color);

    FUN_80382724();
}

// 0x802db86c - Push the current screen (thin wrapper).
void menu_cheats_screen_push(void)
{
    hud_screen_push();
}
