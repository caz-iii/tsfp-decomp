// ============================================================================
// menu_mode_select_render.cpp
// Batch: batch_818
// Address range: 0x802d84a4 - 0x802d9060
// Date: 2026-09-11T05:05:00Z
//
// Front-end game-mode select screen (render + secondary screens), the direct
// continuation of menu_mode_select_screen.cpp (batch_817). Contains:
//   - the group-select push handler that seeds the sub-mode snapshot,
//   - two scissored per-row / per-cell render callbacks (best-record bar and
//     mode-name cell) for the mode-select grid,
//   - the mode-list populate + screen build for a second grid variant,
//   - an autosave-countdown tick thunk,
//   - a small "record unavailable"-style confirm screen build + its push, and
//   - a screen-enter handler that fires a UI sound and pushes.
//
// Layout:
//   menu_mode_group_push_submode   0x802d84a4  stash group id, seed sub-mode, push
//   menu_mode_record_bar_render    0x802d8510  best-record bar render callback
//   menu_mode_grid2_populate       0x802d87bc  populate mode grid (variant 2)
//   menu_mode_grid2_screen_pop     0x802d8a20  commit sub-mode + pop screen
//   menu_mode_grid2_screen_build   0x802d8b6c  build mode-select grid (variant 2)
//   menu_mode_autosave_tick        0x802d8c2c  autosave-countdown tick thunk
//   menu_mode_name_cell_render     0x802d8c4c  mode-name cell render callback
//   menu_mode_record_push          0x802d8f18  hud_screen_push thunk
//   menu_mode_record_screen_build  0x802d8f44  "record" confirm screen build
//   menu_mode_enter_with_sound     0x802d9060  screen enter (UI sound) + push
//
// NON-MATCHING: incoming paired-single / FP spill regs (in_f30/in_f31,
// in_ps30_1/in_ps31_1, extraout_*) are modelled as explicit locals; the
// frame-actor thunk pairs (FUN_803826d8/FUN_803826e4/FUN_803826f0 head,
// FUN_80382724/FUN_80382730/FUN_8038273c tail) return register pairs; CONCAT44
// magic-bias int->float conversions are kept verbatim. hud_add_list_item is
// declared void in the generated headers but its return value is consumed here,
// so it is called through an address-bound function pointer.
// ============================================================================

#include "common.h"
#include "menu.h"     // pulls generated_decls.h
#include "hud.h"
#include "gx.h"
#include "sprite.h"
#include "text.h"
#include "sound.h"

// ---- frame / prologue helper thunks (return register pairs, non-matching) --
extern "C" unsigned long long FUN_803826d8(void); // frame-actor head (record-bar)
extern "C" unsigned long long FUN_803826e4(void); // frame-actor head (name-cell)
extern "C" void      FUN_803826f0(void);           // frame-actor head (pop path)
extern "C" void      FUN_80382724(void);           // frame-actor tail (record-bar)
extern "C" void      FUN_80382730(void);           // frame-actor tail (name-cell)
extern "C" void      FUN_8038273c(void);           // frame-actor tail (pop path)
extern "C" unsigned int FUN_803825c0(double intensity); // fade/alpha 0..255

// ---- raw FUN_ helpers not yet in generated headers ------------------------
extern "C" void  FUN_80366490(void);   // record/settings finalize helper

// ---- void-but-return-used helpers via address-bound function pointers ------
typedef int (*add_item_fp)(void);
static const add_item_fp __mr_add_item = (add_item_fp)0x8003ea8c;      // hud_add_list_item

// Named helpers (hud_*, gx_*, text_*, sprite_*, sound_*, mode_get_const_five,
// level_get_info_by_id, game_get_settings_ptr, leaf_31e8, get_string_by_id,
// autosave_countdown_tick, award_*) resolve via generated_decls.h.

// ---- raw scalar ROM globals -----------------------------------------------
extern "C" int  DAT_805cc614;   // active game mode id
extern "C" int  DAT_805cc634;   // pushed screen kind
extern "C" int  DAT_805cc63c;   // enter-sound screen kind
extern "C" int  DAT_805cc630;   // active record/objective index
extern "C" int  DAT_80612614;   // roster/bot gate
extern "C" int  DAT_80612ad0;   // "all unlocked" / debug flag
extern "C" int  DAT_80612ac4;   // stream request context (unused here)
extern "C" unsigned char DAT_805cc68c; // pushed sub-mode snapshot (char)
extern "C" unsigned char DAT_805cc68d; // mode selector byte
extern "C" unsigned char DAT_805cc68e; // sub-mode selector byte
extern "C" unsigned char DAT_805cc6fc; // record screen flag A
extern "C" unsigned char DAT_805cc6fd; // record screen flag B
extern "C" float DAT_805cc708;         // record screen fade seed

// mode table (stride 0x3c) used by the grid populate/render paths
extern "C" int  DAT_803f31dc;   // mode table: label id (stride 0x3c)
extern "C" int  DAT_803f31e0;   // mode table: alt label id (stride 0x3c)
extern "C" int  DAT_803f31e4;   // mode table: level id (stride 0x3c)
extern "C" int  DAT_803f31e8;   // mode table: sprite id (stride 0x3c)
extern "C" int  DAT_803f3200;   // mode table: icon/lock id (stride 0x3c)
// availability mask table (stride 0x20) shared with menu_mode_select_screen.cpp
extern "C" unsigned int DAT_803f2dd4; // mode table: availability mask (stride 0x20)
// record/objective label + sprite short tables (stride 0x22 shorts / 0x44 bytes)
extern "C" short DAT_803f2918;  // record table: label short A (stride 0x22)
extern "C" short DAT_803f291a;  // record table: label short B (stride 0x22)
extern "C" short DAT_803f292c;  // record table: sprite short (stride 0x44)
extern "C" int   DAT_803f2928;  // record table: detail str id (stride 0x44)

extern "C" int *DAT_806119e0;   // layout center context (array of ptrs)

// ---- PTR table entry referenced by the name-cell render --------------------
extern "C" void *PTR_FUN_8047657c; // name-cell "compact" element vtable marker

// ---- float / double ROM constants -----------------------------------------
extern "C" float  FLOAT_80619f38, FLOAT_80619ee8, FLOAT_8061a10c, FLOAT_80611a68;
extern "C" float  FLOAT_80619f58, FLOAT_80619f2c, FLOAT_8060f958, FLOAT_80619f6c;
extern "C" float  FLOAT_8061a0dc, FLOAT_80619f84, FLOAT_80619f08, FLOAT_8061a110;
extern "C" float  FLOAT_8061a114, FLOAT_80619f8c, FLOAT_80619f0c, FLOAT_80619f74;
extern "C" double DOUBLE_80619f00; // int->float magic-bias constant

// ============================================================================
// 0x802d84a4 - stash selected group id, seed sub-mode snapshot, then push
// ============================================================================
void menu_mode_group_push_submode(int screen)
{
    DAT_805cc634 = *(int *)(screen + 0x188);
    DAT_805cc68c = (unsigned char)(&DAT_803f31e4)[DAT_805cc634 * 0xf];
    if ((DAT_805cc614 != 0xb) && (1 < (unsigned int)(DAT_805cc614 - 0xc))) {
        hud_screen_push();
    }
}

// ============================================================================
// 0x802d8510 - best-record bar render callback (scissored)
// ============================================================================
void menu_mode_record_bar_render(unsigned int a, unsigned int b, float *rect)
{
    bool     useAlt;
    unsigned int uy;
    unsigned int ux;
    float    fVar4;
    int      focusElem;
    unsigned int color;
    unsigned char *label;
    unsigned char *upper;
    int      elem;
    int      modeIdx;
    int      labelId;
    long long framePair;
    double   span;
    double   drawScale;
    int      sx0, sy0, sw, sh;
    float    bar[4];   // {left, top, right, bottom}
    // paired-single FP spill inputs (non-matching, modelled as locals)
    double in_f31 = 0.0, in_ps31_1 = 0.0;

    (void)a; (void)b;
    (void)in_f31; (void)in_ps31_1;

    framePair = FUN_803826d8();
    focusElem = (int)((unsigned long long)framePair >> 0x20);
    elem      = (int)framePair;
    fVar4     = FLOAT_80619f38 * *(float *)(elem + 0x34);
    modeIdx   = *(int *)(elem + 0x14);
    if (FLOAT_80619ee8 < fVar4) {
        fVar4 = FLOAT_80619ee8;
    }

    span = (double)(float)((double)rect[3] - (double)rect[1]);
    uy = (unsigned int)((double)FLOAT_8061a10c * span + (double)rect[1]);
    ux = (unsigned int)(*(float *)(*(int *)(focusElem + 0x1c) + 0x1c) * FLOAT_80611a68 + *rect);

    color = FUN_803825c0((double)(*(float *)(focusElem + 0xf0) *
                                  (FLOAT_80619f58 * fVar4 + FLOAT_80619f58)));

    labelId = (&DAT_803f31dc)[modeIdx * 0xf];
    if ((*(int *)(&DAT_803f31e8 + modeIdx * 0x3c) != -1) && (100 < DAT_80612614)) {
        int drawLabel = labelId;
        if (modeIdx == 0x10) {
            drawLabel = 0x289;
        }
        useAlt = *(int *)(&DAT_803f31e0 + modeIdx * 0x3c) != labelId;
        labelId = drawLabel;
        if (useAlt && (DAT_805cc614 != 0xc) && (DAT_805cc614 != 0xd) &&
            (DAT_805cc614 != 0xb) && (DAT_805cc68e != '\x10')) {
            labelId = *(int *)(&DAT_803f31e0 + modeIdx * 0x3c);
        }
        gx_get_scissor_rect(&sx0, &sy0, &sw, &sh);
        int newBottom = (int)(FLOAT_80619f84 +
            (float)((double)(CONCAT44(0x43300000, sh) ^ 0x80000000) - DOUBLE_80619f00));
        gx_set_scissor_rect(sx0, sy0, sw, newBottom);
        bar[2] = rect[2] - FLOAT_80619f6c * FLOAT_80611a68;
        bar[0] = -((float)((double)FLOAT_80619f2c * span) * FLOAT_8060f958 - bar[2]);
        bar[1] = (float)((double)FLOAT_8061a0dc * span + (double)rect[1]);
        bar[3] = FLOAT_80619f84 + rect[3];
        int mat = sprite_get_texture_id(*(int *)(&DAT_803f31e8 + modeIdx * 0x3c));
        hud_bar_draw_split(focusElem, elem, bar, mat, -1);
        modeIdx = *(int *)(elem + 0x14);   // decompiler reloads; keep verbatim
        (void)modeIdx;
    }

    bar[0] = -((float)((double)FLOAT_80619f2c * span) * FLOAT_8060f958 -
               -(FLOAT_80619f6c * FLOAT_80611a68 - rect[2]));
    drawScale = -(double)(FLOAT_80619f38 * *(float *)(*(int *)(focusElem + 0x1c) + 0x1c) *
                          FLOAT_80611a68 - (bar[0] - *rect));
    text_set_font_table(*(int *)(*(int *)(focusElem + 0x1c) + 0x44));
    label = (unsigned char *)get_string_by_id(labelId);
    upper = text_check_scale(drawScale, label);
    upper = text_string_to_upper((unsigned int)upper);
    text_draw_char((double)(float)((double)(CONCAT44(0x43300000, ux ^ 0x80000000)) - DOUBLE_80619f00),
                   (double)(float)((double)(CONCAT44(0x43300000, uy ^ 0x80000000)) - DOUBLE_80619f00),
                   upper, color & 0xff | 0x64646400);
    FUN_80382724();
}

// ============================================================================
// 0x802d87bc - populate mode grid (variant 2, level-availability gated)
// ============================================================================
void menu_mode_grid2_populate(void)
{
    bool     coop;
    unsigned int *info;
    int      item;
    int      strv;
    int      lockCheck;
    int      modeIdx;
    int      off;
    int      hasBack;   // r4 result of the frame-actor head thunk

    // FUN_803826d8 leaves the "has back item" flag in r4 (extraout_r4).
    hasBack = (int)FUN_803826d8();
    if (hasBack != 0) {
        get_string_by_id(0x1a5f);
        hud_add_list_item();
    }

    modeIdx = 0;
    off = 0;
    do {
        info = (unsigned int *)level_get_info_by_id(*(int *)((int)&DAT_803f31e4 + off));
        coop = false;
        if ((DAT_805cc614 == 0xc) || (DAT_805cc614 == 0xd) || (DAT_805cc614 == 0xb)) {
            coop = true;
        }
        if (((!coop) || ((modeIdx != 0xb) && (modeIdx != -100)) ||
             ((hasBack != 0) && ((modeIdx == 0xb) && (DAT_805cc68e == 0x10)))) &&
            (((100 < DAT_80612614) || (*(int *)((int)&DAT_803f31e4 + off) != 100)) &&
             (((coop || (modeIdx != 8)) || (DAT_805cc68e != 0xc)) &&
              ((coop || (modeIdx != -100)) || (DAT_805cc68e != 0x10))))) {
            if (((info != (unsigned int *)0) &&
                 ((*info & *(unsigned int *)(&DAT_803f2dd4 + (unsigned int)DAT_805cc68e * 0x20)) != 0)) ||
                ((DAT_80612ad0 != 0) || (coop))) {
                if (*(int *)((int)&DAT_803f3200 + off) != 0) {
                    int allowed = 1;
                    if ((DAT_805cc68e == 0x10) && (DAT_80612ad0 == 0)) {
                        allowed = (int)award_stub_true1();
                    } else if (DAT_805cc68e == 0x11) {
                        allowed = (int)award_stub_true2();
                    }
                    if (DAT_80612614 < 0x65) {
                        get_string_by_id(*(int *)((int)&DAT_803f31dc + off));
                        item = __mr_add_item();
                    } else {
                        item = __mr_add_item();
                    }
                    *(float *)(item + 0x40) = FLOAT_80619f38;
                    lockCheck = (int)award_check_slot2();
                    if ((lockCheck == 0) || (allowed == 0)) {
                        int *last = hud_get_last_created_item();
                        hud_item_set_icon_visible(last, (int)&DAT_803f3200 + off);
                        hud_item_set_locked(item, 1);
                    }
                }
            }
        }
        modeIdx++;
        off += 0x3c;
    } while (modeIdx < 0x10);

    (void)strv;
    FUN_80382724();
}

// ============================================================================
// 0x802d8a20 - commit sub-mode selection then pop the screen
// ============================================================================
void menu_mode_grid2_screen_pop(void)
{
    bool     resolved;
    void    *settings;
    unsigned int *info;
    int      probe;
    unsigned char slot;
    int      count;

    FUN_803826f0();
    if ((DAT_805cc614 == 0xc) || (DAT_805cc614 == 0xd) || (DAT_805cc614 == 0xb)) {
        leaf_31e8(0x805cc640, 0x805cd0e0, 0x98);
        settings = game_get_settings_ptr();
        leaf_31e8((unsigned int)settings, 0x805cd178, 0x1210);
        if (DAT_805cc68c == 'd') {
            resolved = false;
            info = (unsigned int *)level_get_info_by_id(100);
            if ((info == (unsigned int *)0) ||
                ((*info & *(unsigned int *)(&DAT_803f2dd4 + (unsigned int)DAT_805cc68e * 0x20)) == 0)) {
                if (info != (unsigned int *)0) {
                    slot  = 2;
                    probe = 0x40;
                    count = 0x14;
                    do {
                        if ((*info & *(unsigned int *)(&DAT_803f2dd4 + probe)) != 0) {
                            resolved = true;
                            DAT_805cc68d = (unsigned char)*(int *)((&DAT_803f2dd4 - 4) + (unsigned int)slot * 0x20);
                            DAT_805cc68e = slot;
                            break;
                        }
                        slot  = slot + 1;
                        probe = probe + 0x20;
                        count = count + -1;
                    } while (count != 0);
                }
            } else {
                resolved = true;
            }
            if (!resolved) {
                FUN_80366490();
            }
        }
    }
    hud_screen_pop_current();
    FUN_8038273c();
}

// ============================================================================
// 0x802d8b6c - build the mode-select grid (variant 2)
// ============================================================================
void menu_mode_grid2_screen_build(int screen)
{
    unsigned int flags;
    int titleStr;

    flags = 0xe000;
    if ((DAT_805cc614 != 0xc) && (DAT_805cc614 != 0xd) && (DAT_805cc614 != 0xb)) {
        flags = 0x800e000;
    }
    hud_element_init_from_layout((int)flags, 0, 0, (int)&menu_mode_grid2_screen_pop);
    titleStr = get_string_by_id(0x360);
    *(int *)(screen + 0x200) = titleStr;
    *(int *)(screen + 0x214) = 0x21;
    hud_element_scroll_horizontal((int)FLOAT_80619f08, 3);
    menu_mode_grid2_populate();
    hud_element_apply_scroll();
    hud_element_apply_center_xy(DAT_806119e0);
}

// ============================================================================
// 0x802d8c2c - autosave-countdown tick thunk
// ============================================================================
void menu_mode_autosave_tick(void)
{
    autosave_countdown_tick();
}

// ============================================================================
// 0x802d8c4c - mode-name cell render callback (scissored)
// ============================================================================
void menu_mode_name_cell_render(unsigned int a, unsigned int b, float *rect)
{
    unsigned int ux;
    unsigned int uy;
    int      newBottom;
    float    fVar4;
    int      focusElem;
    unsigned int color;
    unsigned char *label;
    unsigned char *upper;
    int      elem;
    int      modeIdx;
    long long framePair;
    double   span;
    double   scaleArg;
    double   drawScale;
    int      sx0, sy0, sw, sh;
    float    bar[4];   // {left, top, right, bottom}
    // paired-single FP spill inputs (non-matching, modelled as locals)
    double in_f30 = 0.0, in_f31 = 0.0, in_ps30_1 = 0.0, in_ps31_1 = 0.0;

    (void)a; (void)b;
    (void)in_f30; (void)in_f31; (void)in_ps30_1; (void)in_ps31_1;

    framePair = FUN_803826e4();
    focusElem = (int)((unsigned long long)framePair >> 0x20);
    elem      = (int)framePair;
    span      = (double)(float)((double)rect[3] - (double)rect[1]);
    modeIdx   = *(int *)(elem + 0x14);
    uy = (unsigned int)((double)FLOAT_8061a110 * span + (double)rect[1]);
    ux = (unsigned int)(*(float *)(*(int *)(focusElem + 0x1c) + 0x1c) * FLOAT_80611a68 + *rect);

    if (*(void ***)(focusElem + 4) == &PTR_FUN_8047657c) {
        fVar4 = FLOAT_80619f0c * *(float *)(focusElem + 0xf0);
    } else {
        fVar4 = (FLOAT_8061a114 * *(float *)(elem + 0x34) + FLOAT_80619f8c) *
                *(float *)(focusElem + 0xf0);
    }
    scaleArg = (double)fVar4;

    gx_get_scissor_rect(&sx0, &sy0, &sw, &sh);
    newBottom = (int)(FLOAT_80619f84 +
        (float)((double)(CONCAT44(0x43300000, sh ^ 0x80000000)) - DOUBLE_80619f00));
    gx_set_scissor_rect(sx0, sy0, sw, newBottom);
    bar[2] = rect[2] - FLOAT_80619f6c * FLOAT_80611a68;
    bar[0] = -((float)((double)FLOAT_80619f2c * span) * FLOAT_8060f958 - bar[2]);
    bar[1] = (float)((double)FLOAT_8061a0dc * span + (double)rect[1]);
    bar[3] = FLOAT_80619f84 + rect[3];
    gx_set_sub_material_id(1);
    text_set_font_table(0x12);

    if ((*(unsigned int *)(elem + 0x28) & 1) == 0) {
        drawScale = -(double)(FLOAT_80619f38 * *(float *)(*(int *)(focusElem + 0x1c) + 0x1c) *
                              FLOAT_80611a68 - (bar[0] - *rect));
        text_set_font_table(3);
        color = FUN_803825c0(scaleArg);
        label = (unsigned char *)get_string_by_id((int)(short)(&DAT_803f291a)[modeIdx * 0x22]);
        upper = text_check_scale(drawScale, label);
        upper = text_string_to_upper((unsigned int)upper);
        text_draw_char((double)(float)((double)(CONCAT44(0x43300000, ux ^ 0x80000000)) - DOUBLE_80619f00),
                       (double)(float)((double)(CONCAT44(0x43300000, uy ^ 0x80000000)) - DOUBLE_80619f00),
                       upper, color & 0xff | 0x64646400);
    } else {
        text_set_font_table(3);
        color = FUN_803825c0(scaleArg);
        label = (unsigned char *)get_string_by_id(0x471);
        text_draw_char((double)(float)((double)(CONCAT44(0x43300000, ux ^ 0x80000000)) - DOUBLE_80619f00),
                       (double)(float)((double)(CONCAT44(0x43300000, uy ^ 0x80000000)) - DOUBLE_80619f00),
                       label, color & 0xff | 0x64281e00);
    }

    if (*(int *)(&DAT_803f292c + modeIdx * 0x44) != -1) {
        int mat = sprite_get_texture_id(*(int *)(&DAT_803f292c + modeIdx * 0x44));
        hud_bar_draw_split(focusElem, elem, bar, mat, -1);
    }
    FUN_80382730();
}

// ============================================================================
// 0x802d8f18 - record screen push thunk
// ============================================================================
void menu_mode_record_push(void)
{
    hud_screen_push();
}

// ============================================================================
// 0x802d8f44 - "record" confirm screen build
// ============================================================================
void menu_mode_record_screen_build(int screen)
{
    int titleStr;
    int rowStr;
    int last;
    double h;

    titleStr = get_string_by_id(0x3d4);
    *(int *)(screen + 0x200) = titleStr;
    *(int *)(screen + 0x214) = 0x50;
    rowStr = get_string_by_id(0x3d4);
    hud_element_init_from_layout(0x800a00c, (void *)rowStr, (int)&menu_mode_record_push, 0);

    get_string_by_id((int)(short)(&DAT_803f2918)[DAT_805cc630 * 0x22]);
    hud_add_list_item();
    last = (int)hud_get_last_created_item();
    *(int *)(last + 0x1c) = 0xb;
    h = (double)FLOAT_80619f84;
    last = (int)hud_get_last_created_item();
    *(float *)(last + 0x40) = (float)h;
    hud_add_callback_item_default(1);

    get_string_by_id(*(int *)(&DAT_803f2928 + DAT_805cc630 * 0x44));
    hud_add_list_item();
    hud_element_finalize_frame();
    hud_element_apply_center_xy(DAT_806119e0);
}

// ============================================================================
// 0x802d9060 - screen enter: seed fade, fire UI sound, push
// ============================================================================
void menu_mode_enter_with_sound(int screen)
{
    int params[14];

    DAT_805cc708 = FLOAT_80619f08;
    DAT_805cc63c = *(int *)(screen + 0x188);
    sound_init_play_params((int)params);
    params[0] = 0xd1e;
    sound_play_request(params);
    DAT_805cc6fc = 0;
    DAT_805cc6fd = 0;
    hud_screen_push();
}
