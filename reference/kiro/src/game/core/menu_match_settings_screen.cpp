// ============================================================================
// menu_match_settings_screen.cpp
// Batch: batch_814
// Address range: 0x802d4ec0 - 0x802d4fec, 0x802d5a94 - 0x802d5c8c
// Date: 2026-09-11T03:05:00Z
//
// Front-end "match settings" options screen. This is the list-builder screen
// that presents the per-match rule set (score/time limits, weapon class,
// handicap, team options, award/bot toggles, etc.) as a HUD list, plus the
// screen init/render/push entry points and a scissored scroll-bar renderer.
//
// Layout:
//   menu_match_settings_screen_enter / _enter2  0x802d4ec0 / 0x802d4eec
//       thin thunks that push the screen (hud_screen_push).
//   menu_match_settings_weapon_class_string     0x802d4f18
//       resolve weapon-class option label (string id 0x356 default,
//       else 0x36d + index).
//   menu_match_settings_time_string             0x802d4f50
//       resolve time/duration option label (0x356 default, else 0x377 + index).
//   menu_match_settings_item_measure            0x802d4f88
//       set the row font table then measure a glyph and compute item colors.
//   menu_match_settings_list_build              0x802d4fec
//       the big builder: emits every applicable option row for the current
//       game mode (DAT_805cc614) and sub-mode (DAT_805cc68e), gating rows by
//       mode/handicap/team rules, wiring ok/cancel and string event lists.
//   menu_match_settings_screen_init             0x802d5a94
//       title + layout init, invokes list_build, centers, syncs settings->items.
//   menu_match_settings_screen_render           0x802d5b64
//       per-frame scissor reset + ready-status overlay (non-loadout modes).
//   menu_match_settings_screen_push             0x802d5c10
//       seed mode/sub-mode globals then push the screen.
//   menu_match_settings_scroll_bar_render       0x802d5c8c
//       scissored scroll-bar sprite draw (progress within the scissor rect).
//
// NON-MATCHING: incoming paired-single FP spill regs (in_f31/in_ps31_1) are
// modelled as explicit locals; __save_gpr_28 and the frame-actor thunk pairs
// return a 64-bit pair split by shifts; CONCAT44 magic-bias int->float
// conversions are kept verbatim. weapon_get_name_string is called through an
// address-bound function pointer because its generated prototype is void but
// the return value (the name-string handle) is consumed here.
// ============================================================================

#include "common.h"
#include "menu.h"     // pulls generated_decls.h (named cross-module helpers)
#include "hud.h"
#include "gx.h"
#include "sprite.h"
#include "weapon.h"
#include "gamemode.h"

// ---- frame / prologue helper thunks ---------------------------------------
extern "C" long long __save_gpr_28(void);
extern "C" void      __restore_gpr_28(void);
extern "C" long long FUN_803826d4(void);   // frame-actor accessor: returns (element,flag) pair
extern "C" void      FUN_80382720(void);    // frame-actor tail (screen build)

// ---- raw FUN_ helpers not yet in generated headers ------------------------
extern "C" int  *FUN_80339b90(int a, int b);  // resolve pointer record by (ctx, key)
extern "C" int   FUN_8031d47c(int handle);    // resolve sprite/material id from item handle
extern "C" unsigned int FUN_80366414(void);   // net/online-active query (0 = local)

// weapon_get_name_string @ 0x802c7848 returns the name-string handle; the
// generated decl is void, so bind by address to consume the return value.
typedef int (*weapon_name_fp)(int);
static const weapon_name_fp __ms_weapon_name = (weapon_name_fp)0x802c7848;

// All other named cross-module helpers (hud_*, gx_*, text_*, sprite_*,
// menu_*, award_*, gamemode_*, get_string_by_id) resolve through
// generated_decls.h / generated_externs.h pulled in by the shim includes.

// ---- raw scalar ROM globals -----------------------------------------------
extern "C" int            DAT_80612ad0;   // options-vs-loadout flag (0 = full settings list)
extern "C" int            DAT_805cc614;   // active game mode id
extern "C" int            DAT_8060f948;   // player-count / split context
extern "C" int            DAT_80611014;   // list default-callback target id
extern "C" int            DAT_80612614;   // bot-count / roster gate
extern "C" int            DAT_80612be8;   // screen-push context handle
extern "C" unsigned char  DAT_805cc68d;   // mode selector byte (row-gating)
extern "C" unsigned char  DAT_805cc68e;   // sub-mode selector byte (row-gating)
extern "C" unsigned char  DAT_805cc68c;   // pushed sub-mode snapshot
extern "C" unsigned char  DAT_803f35a4;   // default sub-mode source byte
extern "C" int            DAT_805c7ae0;   // resolved record value (from FUN_80339b90)
extern "C" int            DAT_805cc634;   // pushed screen kind
extern "C" int            DAT_806109c4;   // framebuffer width (scissor)
extern "C" int            DAT_806109c8;   // framebuffer height (scissor)
extern "C" int            DAT_806119e0;   // layout center context handle

// row option string-id source tables (indexed run of undefined4 label ids)
extern "C" int            DAT_805cc584;   // weapon/class option label table
extern "C" int            DAT_805cc53c;   // award option label table
extern "C" short          DAT_803f2de4;   // per-submode default score (short)
extern "C" int            DAT_803f2dd8;   // per-submode team-mode flag (stride 0x20)

// ---- float / double ROM constants -----------------------------------------
extern "C" float          FLOAT_80619f08;  // standard spacer height
extern "C" float          FLOAT_80619f6c;  // header margin
extern "C" float          FLOAT_80619f84;  // footer margin / scissor bias
extern "C" float          FLOAT_80619ee8;  // item padding amount
extern "C" float          FLOAT_80619f2c;  // scroll-bar vertical scale
extern "C" float          FLOAT_8060f958;  // scroll fraction
extern "C" float          FLOAT_8061a0dc;  // scroll-bar top offset
extern "C" float          FLOAT_80611a68;  // header pixel offset
extern "C" double         DOUBLE_80619f00; // int->float magic-bias constant
extern "C" unsigned char  DAT_8061103c;    // measurement glyph (single char)

// ============================================================================
// 0x802d4ec0 - screen enter thunk
// ============================================================================
void menu_match_settings_screen_enter(void)
{
    hud_screen_push();
}

// ============================================================================
// 0x802d4eec - screen enter thunk (second entry id)
// ============================================================================
void menu_match_settings_screen_enter2(void)
{
    hud_screen_push();
}

// ============================================================================
// 0x802d4f18 - weapon-class option label lookup
// ============================================================================
void menu_match_settings_weapon_class_string(int index)
{
    if (index == 0) {
        get_string_by_id(0x356);
    } else {
        get_string_by_id(index + 0x36d);
    }
}

// ============================================================================
// 0x802d4f50 - time/duration option label lookup
// ============================================================================
void menu_match_settings_time_string(int index)
{
    if (index == 0) {
        get_string_by_id(0x356);
    } else {
        get_string_by_id(index + 0x377);
    }
}

// ============================================================================
// 0x802d4f88 - measure a settings row glyph and compute item colors
// ============================================================================
void menu_match_settings_item_measure(int item, unsigned int style)
{
    unsigned int fg;
    unsigned int bg[3];

    text_set_font_table(*(int *)(*(int *)(item + 0x1c) + 0x48));
    text_get_char_width((u8 *)&DAT_8061103c);
    text_get_char_height((u8 *)&DAT_8061103c);
    hud_item_compute_colors(item, (int)style, bg, &fg);
}

// ============================================================================
// 0x802d4fec - build the full match-settings option list
// ============================================================================
void menu_match_settings_list_build(void)
{
    int      focusElem;
    int      isOnline;
    int      strId;
    int      tmp;
    int      itemHandle;
    int      row;
    int      rowByteOff;
    unsigned int modeSel;
    unsigned int subSel;
    long long framePair;
    int      defaultScore;

    framePair = FUN_803826d4();
    focusElem = (int)((unsigned long long)framePair >> 0x20);
    isOnline  = (int)framePair;
    modeSel   = (unsigned int)DAT_805cc68d;
    subSel    = (unsigned int)DAT_805cc68e;

    if (DAT_80612ad0 == 0) {
        defaultScore = (int)*(short *)(&DAT_803f2de4 + subSel * 0x20);
    } else {
        defaultScore = 0;
    }
    get_string_by_id(defaultScore);

    if ((DAT_80612ad0 == 0) && ((DAT_805cc614 != 9) || (1 < (unsigned int)(DAT_8060f948 - 3)))) {
        hud_add_list_item();
        hud_add_callback_item_default(0);
    }
    hud_set_margins(FLOAT_80619f6c, FLOAT_80619f84, 1);
    hud_set_context_property(0x22100);

    // --- sub-mode 0x13 special weapon-set block ---
    if ((subSel == 0x13) && (DAT_80612ad0 == 0)) {
        get_string_by_id(0x3bf);
        hud_add_list_item();
        hud_item_add_spacer(FLOAT_80619f08, 0);
        if (isOnline != 0) {
            strId = get_string_by_id(0x1a5f);
            hud_event_add_string(strId);
        }
        for (row = 0; row < 5; row++) {
            tmp = get_string_by_id(row + 0x368);
            hud_event_add_string(tmp);
        }
    }

    // --- weapon-class row (modes 9/2, not sub 7/9) ---
    if (((modeSel == 9) || (modeSel == 2)) && (subSel != 7) &&
        (subSel != 9) && (DAT_80612ad0 == 0)) {
        get_string_by_id(0x36d);
        hud_add_list_item();
        hud_item_add_spacer(FLOAT_80619f08, 0);
        if (isOnline != 0) {
            strId = get_string_by_id(0x1a5f);
            hud_event_add_string(strId);
        }
        for (row = 0; row < 0xb; row++) {
            menu_match_settings_weapon_class_string(row);
            hud_event_add_string(0);
        }
    }

    // --- sub 9 handicap block ---
    if ((subSel == 9) && (isOnline == 0)) {
        get_string_by_id(0x352);
        hud_add_list_item();
        hud_item_add_spacer(FLOAT_80619f08, 0);
        strId = get_string_by_id(0x355);
        hud_event_add_string(strId);
        strId = get_string_by_id(0x354);
        hud_event_add_string(strId);
        strId = get_string_by_id(0x353);
        hud_event_add_string(strId);
    }

    // --- mode 9 team-type block ---
    if ((modeSel == 9) && (subSel != 7) && (subSel != 0x12) &&
        (subSel != 9) && (subSel != 10) && (DAT_80612ad0 == 0)) {
        get_string_by_id(0x3bc);
        hud_add_list_item();
        hud_item_add_spacer(FLOAT_80619f08, 0);
        if (isOnline != 0) {
            strId = get_string_by_id(0x1a5f);
            hud_event_add_string(strId);
        }
        strId = get_string_by_id(0x391);
        hud_event_add_string(strId);
        strId = get_string_by_id(0x392);
        hud_event_add_string(strId);
        strId = get_string_by_id(0x393);
        hud_event_add_string(strId);
    }

    // --- team-count block (several modes, not sub 0xd/0xe) ---
    if (((modeSel == 9) || (modeSel == 6) || ((unsigned int)(modeSel - 1) < 2) || (modeSel == 3)) &&
        (subSel != 0xe) && (subSel != 0xd) && (DAT_80612ad0 == 0)) {
        get_string_by_id(0x3bd);
        hud_add_list_item();
        hud_item_add_spacer(FLOAT_80619f08, 0);
        if (isOnline != 0) {
            strId = get_string_by_id(0x1a5f);
            hud_event_add_string(strId);
        }
        strId = get_string_by_id(0x358);
        hud_event_add_string(strId);
        strId = get_string_by_id(0x357);
        hud_event_add_string(strId);
    }

    // --- sub 7/9 weapon roster block ---
    if (((subSel == 7) || (subSel == 9)) && (DAT_80612ad0 == 0)) {
        get_string_by_id(0x37e);
        hud_add_list_item();
        hud_item_add_spacer(FLOAT_80619f08, 0);
        if (isOnline != 0) {
            strId = get_string_by_id(0x1a5f);
            hud_event_add_string(strId);
        }
        for (row = 0; row < 0xc; row++) {
            tmp = get_string_by_id(row + 0x37f);
            hud_event_add_string(tmp);
            if ((subSel == 7) && (row == 0xb)) {
                tmp = hud_get_item_counter();
                hud_event_set_disabled(tmp, 1);
            }
        }
    }

    // --- time-limit row (always) ---
    get_string_by_id(0x3b5);
    hud_add_list_item();
    hud_item_add_spacer(FLOAT_80619f08, 0);
    if (isOnline != 0) {
        strId = get_string_by_id(0x1a5f);
        hud_event_add_string(strId);
    }
    for (row = 0; row < 7; row++) {
        menu_match_settings_time_string(row);
        hud_event_add_string(0);
    }

    // --- score-limit ok/cancel rows, gated by mode/sub-mode ---
    if (((modeSel == 1) || (modeSel == 2)) && (DAT_80612ad0 == 0)) {
        get_string_by_id(0x3b6);
        hud_add_list_item();
        hud_item_add_spacer(FLOAT_80619f08, 0);
        if (isOnline != 0) {
            strId = get_string_by_id(0x1a5f);
            hud_event_add_string(strId);
        }
        hud_event_add_ok_cancel();
    }
    if ((*(int *)(&DAT_803f2dd8 + subSel * 0x20) == 2) && (DAT_80612ad0 == 0)) {
        get_string_by_id(0x3b7);
        hud_add_list_item();
        hud_item_add_spacer(FLOAT_80619f08, 0);
        if (isOnline != 0) {
            strId = get_string_by_id(0x1a5f);
            hud_event_add_string(strId);
        }
        hud_event_add_ok_cancel();
    }
    tmp = *(int *)(&DAT_803f2dd8 + subSel * 0x20);
    if (((tmp == 1) || (tmp == 2)) && (DAT_80612ad0 == 0)) {
        get_string_by_id(0x3b8);
        hud_add_list_item();
        hud_item_add_spacer(FLOAT_80619f08, 0);
        if (isOnline != 0) {
            strId = get_string_by_id(0x1a5f);
            hud_event_add_string(strId);
        }
        hud_event_add_ok_cancel();
    }

    // --- fixed ok/cancel rows ---
    get_string_by_id(0x3b9);
    hud_add_list_item();
    hud_item_add_spacer(FLOAT_80619f08, 0);
    if (isOnline != 0) {
        strId = get_string_by_id(0x1a5f);
        hud_event_add_string(strId);
    }
    hud_event_add_ok_cancel();

    get_string_by_id(0x3ba);
    hud_add_list_item();
    hud_item_add_spacer(FLOAT_80619f08, 0);
    if (isOnline != 0) {
        strId = get_string_by_id(0x1a5f);
        hud_event_add_string(strId);
    }
    hud_event_add_ok_cancel();

    if ((DAT_805cc614 != 0xc) && (DAT_805cc614 != 0xd) && (DAT_805cc614 != 0xb) && (modeSel != 5)) {
        get_string_by_id(0x3bb);
        hud_add_list_item();
        hud_item_add_spacer(FLOAT_80619f08, 0);
        if (isOnline != 0) {
            strId = get_string_by_id(0x1a5f);
            hud_event_add_string(strId);
        }
        hud_event_add_ok_cancel();
    }
    if (modeSel != 5) {
        get_string_by_id(0x3b3);
        hud_add_list_item();
        hud_item_add_spacer(FLOAT_80619f08, 0);
        if (isOnline != 0) {
            strId = get_string_by_id(0x1a5f);
            hud_event_add_string(strId);
        }
        hud_event_add_ok_cancel();
    }
    get_string_by_id(0x3b4);
    hud_add_list_item();
    hud_item_add_spacer(FLOAT_80619f08, 0);
    if (isOnline != 0) {
        strId = get_string_by_id(0x1a5f);
        hud_event_add_string(strId);
    }
    hud_event_add_ok_cancel();

    // --- local-only weapon-enable roster ---
    if (isOnline == 0) {
        get_string_by_id(0x3be);
        hud_add_list_item();
        hud_item_add_spacer(FLOAT_80619f08, 0);
        hud_event_add_ok_cancel();
        get_string_by_id(0x17d6);
        hud_add_list_item();
        hud_item_add_spacer(FLOAT_80619f08, 0);
        for (row = 0; row < 0x2d; row++) {
            if (weapon_always_enabled_stub() != 0) {
                hud_event_add_string(__ms_weapon_name(row));
            }
        }
    }

    hud_set_margins(FLOAT_80619f08, FLOAT_80619f08, 0);
    if (((DAT_805cc614 == 9) || (DAT_805cc614 == 0xc)) &&
        ((DAT_8060f948 == 4) || (DAT_8060f948 == 3))) {
        hud_set_context_property(0x22400);
    } else {
        hud_set_context_property(0x22600);
    }
    hud_add_callback_item_default(1);

    // --- team-assignment roster (local only) ---
    if (isOnline == 0) {
        get_string_by_id(0x3b0);
        hud_add_list_item();
        (void)DAT_80612614;   // roster-size gate (both branches emit same row)
        menu_slot_validate_setting();
        hud_item_add_spacer(FLOAT_80619f08, 0);
        rowByteOff = 0;
        for (row = 0; row < 0x10; row++) {
            hud_event_add_string(*(int *)((int)&DAT_805cc584 + rowByteOff));
            if (menu_slot_is_selectable(row) == 0) {
                itemHandle = hud_get_item_counter();
                hud_event_set_disabled(itemHandle, 1);
            }
            rowByteOff += 4;
        }
        if (((DAT_805cc614 == 9) || (DAT_805cc614 == 0xc)) &&
            ((DAT_8060f948 == 4) || (DAT_8060f948 == 3))) {
            int *last = hud_get_last_created_item();
            *(int *)((int)last + 0xc0) = 4;
        }
    }

    // --- award roster (not modes 0xb/0xc/0xd) ---
    if ((DAT_805cc614 != 0xc) && (DAT_805cc614 != 0xd) && (DAT_805cc614 != 0xb)) {
        get_string_by_id(0x3b1);
        hud_add_list_item();
        menu_options_clamp_selection();
        hud_item_add_spacer(FLOAT_80619f08, 0);
        if (isOnline != 0) {
            strId = get_string_by_id(0x1a5f);
            hud_event_add_string(strId);
        }
        rowByteOff = 0;
        for (row = 0; row < 0x12; row++) {
            hud_event_add_string(*(int *)((int)&DAT_805cc53c + rowByteOff));
            if ((award_validate_id_range(row) == 0) ||
                (menu_slot_is_selectable_ex(row) == 0)) {
                itemHandle = hud_get_item_counter();
                hud_event_set_disabled(itemHandle, 1);
            }
            rowByteOff += 4;
        }
        if ((DAT_805cc614 == 9) && ((DAT_8060f948 == 4) || (DAT_8060f948 == 3))) {
            int *last = hud_get_last_created_item();
            *(int *)((int)last + 0xc0) = 4;
        }
    }
    hud_add_callback_item_default(1);
    hud_set_context_property(0x22600);

    // --- start/confirm row + focus resolution ---
    get_string_by_id(0x3b2);
    hud_add_list_item();
    isOnline = (int)FUN_80366414();
    if (isOnline == 0) {
        row = menu_loadout_count_active_bots();
        tmp = gamemode_get_score_type(subSel);
        if ((defaultScore + row < tmp) && (isOnline == 0)) {
            int *last = hud_get_last_created_item();
            hud_item_set_locked((int)last, 1);
            hud_set_focus_by_id(focusElem, 0x11);
        } else {
            hud_set_focus_by_id(focusElem, 0x13);
        }
    } else {
        hud_set_focus_by_id(focusElem, 0x13);
    }
    FUN_80382720();
}

// ============================================================================
// 0x802d5a94 - match-settings screen init (title + layout + list build)
// ============================================================================
void menu_match_settings_screen_init(int screen)
{
    int titleStr;
    unsigned int layoutFlags;

    layoutFlags = 0x4001a000;
    titleStr = get_string_by_id(0x402);
    *(int *)(screen + 0x200) = titleStr;
    *(int *)(screen + 0x214) = 0x20;
    if ((DAT_805cc614 != 0xc) && (DAT_805cc614 != 0xd) && (DAT_805cc614 != 0xb)) {
        layoutFlags = 0x4801a000;
    }
    hud_element_init_from_layout((int)layoutFlags, 0, 0, 0);
    hud_clear_destructor();
    hud_set_item_padding_mode(FLOAT_80619ee8, 1, 1, 1);
    menu_match_settings_list_build();
    hud_element_apply_center_xy((int *)DAT_806119e0);
    game_get_settings_ptr();
    menu_loadout_settings_to_items();
}

// ============================================================================
// 0x802d5b64 - per-frame render: scissor reset + ready-status overlay
// ============================================================================
void menu_match_settings_screen_render(int screen)
{
    if (hud_element_is_closing(screen) == 0) {
        gx_set_scissor_rect(0, 0, DAT_806109c4, DAT_806109c8);
        if ((DAT_805cc614 != 0xc) && (DAT_805cc614 != 0xd) && (DAT_805cc614 != 0xb)) {
            menu_loadout_ready_status_render();
        }
    }
}

// ============================================================================
// 0x802d5c10 - push the match-settings screen (seeds mode/sub-mode globals)
// ============================================================================
void menu_match_settings_screen_push(void)
{
    int *rec;

    rec = FUN_80339b90(DAT_80612be8, 0xc77668);
    DAT_805c7ae0 = *rec;
    DAT_805cc614 = 9;
    DAT_805cc634 = 0x10;
    DAT_805cc68c = (unsigned char)DAT_803f35a4;
    hud_screen_push();
}

// ============================================================================
// 0x802d5c8c - scissored scroll-bar sprite render
// ============================================================================
void menu_match_settings_scroll_bar_render(unsigned int a, unsigned int b, int rect)
{
    int      spriteId;
    int      texId;
    int      scissX0, scissY0, scissX1;
    unsigned int scissY1;
    double   heightSpan;
    long long framePair;
    int      clampedBottom;
    float    barRect[4];   // {left, top, right, bottom} laid out at &barRect[0]
    double   in_f31 = 0.0;
    double   in_ps31_1 = 0.0;

    (void)in_f31;
    (void)in_ps31_1;

    framePair = __save_gpr_28();
    spriteId  = FUN_8031d47c(*(int *)((int)framePair + 0x14));
    heightSpan = (double)(*(float *)(rect + 0xc) - *(float *)(rect + 4));

    gx_get_scissor_rect(&scissX0, &scissY0, &scissX1, (int *)&scissY1);
    clampedBottom = (int)(FLOAT_80619f84 +
        (float)((double)(unsigned int)scissY1 - DOUBLE_80619f00));
    gx_set_scissor_rect((unsigned int)scissX0, (unsigned int)scissY0, scissX1, clampedBottom);

    // barRect layout: [0]=left(computed later), [1]=... matches the +0x48/-0x48
    // spill window the decompiler emitted; fields written in ROM order.
    barRect[2] = *(float *)(rect + 8) - FLOAT_80619f6c * FLOAT_80611a68;                 // right/base x
    barRect[0] = -((float)((double)FLOAT_80619f2c * heightSpan) * FLOAT_8060f958 - barRect[2]); // left
    barRect[1] = (float)((double)FLOAT_8061a0dc * heightSpan + (double)*(float *)(rect + 4));    // top
    barRect[3] = FLOAT_80619f84 + *(float *)(rect + 0xc);                                 // bottom

    if (spriteId != -1) {
        texId = sprite_get_texture_id(spriteId);
        hud_bar_draw_split((unsigned int)((unsigned long long)framePair >> 0x20),
                           (unsigned int)framePair, barRect, texId, -1);
    }
    __restore_gpr_28();
}
