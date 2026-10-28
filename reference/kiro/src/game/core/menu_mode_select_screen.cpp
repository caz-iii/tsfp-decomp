// ============================================================================
// menu_mode_select_screen.cpp
// Batch: batch_817
// Address range: 0x802d7890 - 0x802d7e88, 0x802d8228 - 0x802d8388
// Date: 2026-09-11T04:35:00Z
//
// Front-end game-mode select screen: the mode grid/list with locked + award-
// gated entries, the per-cell render callback, the selection-confirm handler
// (routes to preview / settings / push depending on mode), mode-specific push
// handlers, an options subscreen and the player-count screen.
//
// Layout:
//   menu_mode_select_group_push    0x802d7890  stash group id + push
//   menu_mode_options_screen_build 0x802d78cc  options subscreen (award-gated)
//   menu_mode_select_confirm       0x802d7a14  commit sub-mode + route push
//   menu_mode_grid_cell_render     0x802d7b1c  per-cell render callback
//   menu_mode_grid_populate        0x802d7e88  populate mode grid rows
//   menu_mode_screen_pop           0x802d8228  hud_screen_pop_current thunk
//   menu_mode_select_screen_build  0x802d8248  mode-select screen build
//   menu_mode_push_mode5           0x802d8308  set mode 5 (+flag) + push
//   menu_mode_push_mode6           0x802d834c  set mode 6 + push
//   menu_mode_players_screen_build 0x802d8388  1P/2P player-count screen
//
// NON-MATCHING: incoming paired-single / FP spill regs (in_f28..in_f31,
// in_ps28_1..in_ps31_1) are modelled as explicit locals; the frame-actor thunk
// pairs (FUN_803826e8/FUN_803826d4/FUN_80382720/FUN_80382734) return register
// pairs; CONCAT44 magic-bias int->float conversions are kept verbatim.
// award_grant_dispatch / award_grant_wrapper1 / hud_add_list_item are declared
// void in the generated headers but their return values are consumed here, so
// they are called through address-bound function pointers.
// ============================================================================

#include "common.h"
#include "menu.h"     // pulls generated_decls.h
#include "hud.h"
#include "gx.h"
#include "sprite.h"
#include "text.h"

// ---- frame / prologue helper thunks ---------------------------------------
extern "C" int       FUN_803826e8(void);   // frame-actor accessor (element ptr)
extern "C" long long FUN_803826d4(void);   // frame-actor accessor (pair; r4 = "has back")
extern "C" unsigned int FUN_803825c0(double intensity); // fade/alpha 0..255
extern "C" void      FUN_80382720(void);    // frame-actor tail (populate)
extern "C" void      FUN_80382734(void);    // frame-actor tail (cell render)

// ---- raw FUN_ helpers not yet in generated headers ------------------------
extern "C" int  *FUN_80339b90(int a, int b);  // resolve pointer record by (ctx, key)
extern "C" unsigned int FUN_8030f0d0(int mode); // per-mode availability query

// ---- void-but-return-used helpers via address-bound function pointers ------
typedef int (*award_q_fp)(void);
static const award_q_fp __ms_award_dispatch = (award_q_fp)0x801c0f6c; // award_grant_dispatch
static const award_q_fp __ms_award_wrap1    = (award_q_fp)0x801c12a4; // award_grant_wrapper1
typedef int (*add_item_fp)(void);
static const add_item_fp __ms_add_item = (add_item_fp)0x8003ea8c;      // hud_add_list_item

// Named helpers (hud_*, gx_*, text_*, sprite_*, mode_get_const_five,
// level_get_info_by_id, menu_preview_system_init, get_string_by_id) resolve via
// generated_decls.h / generated_externs.h.

// ---- raw scalar ROM globals -----------------------------------------------
extern "C" int  DAT_805cc614;   // active game mode id
extern "C" int  DAT_805cc62c;   // active mode-group id
extern "C" int  DAT_805cc634;   // pushed screen kind
extern "C" int  DAT_805c7ae0;   // resolved availability mask
extern "C" int  DAT_80611014;   // player/flag count
extern "C" int  DAT_8060f948;   // player-count / split context
extern "C" int  DAT_80612614;   // roster/bot gate
extern "C" int *DAT_80612be8;   // screen-push context (first word = kind tag)
extern "C" int *DAT_806119e0;   // layout center context (array of ptrs)
extern "C" unsigned char DAT_805cc68c; // pushed sub-mode snapshot (char)
extern "C" unsigned char DAT_805cc68d; // mode selector byte
extern "C" unsigned char DAT_805cc68e; // sub-mode selector byte
extern "C" unsigned char DAT_805cc68f; // extra mode flag
extern "C" unsigned char DAT_803f35a4; // default sub-mode source byte
extern "C" int  DAT_803f2dcc;   // mode table: enabled word (stride 0x20)
extern "C" int  DAT_803f2dd0;   // mode table: selector source (stride 0x20)
extern "C" int  DAT_803f2dd4;   // mode table: availability mask (stride 0x20)
extern "C" short DAT_803f2de4;  // mode table: label id (short, stride 0x20)
extern "C" short DAT_803f2de6;  // mode table: score short A (stride 0x20)
extern "C" short DAT_803f2de8;  // mode table: score short B (stride 0x20)
extern "C" short DAT_803f2dea;  // mode table: sprite id (short, stride 0x20)
extern "C" unsigned char DAT_8047629c; // lock icon A
extern "C" unsigned char DAT_804762b0; // lock icon B

// ---- float / double ROM constants -----------------------------------------
extern "C" float  FLOAT_8061a0fc, FLOAT_80619eec, FLOAT_80619f30, FLOAT_80619f6c;
extern "C" float  FLOAT_80611a68, FLOAT_80619f38, FLOAT_8060f958, FLOAT_8060f95c;
extern "C" float  FLOAT_80619f08, FLOAT_8061a100, FLOAT_80611a64, FLOAT_8061a064;
extern "C" float  FLOAT_80619f0c, FLOAT_8061167c, FLOAT_8061a0c8, FLOAT_8061a104;
extern "C" float  FLOAT_80619fc4, FLOAT_80619fec;
extern "C" double DOUBLE_80619f00; // int->float magic-bias constant

// ============================================================================
// 0x802d7890 - stash selected mode-group id then push
// ============================================================================
void menu_mode_select_group_push(int screen)
{
    DAT_805cc62c = *(int *)(screen + 0x188);
    hud_screen_push();
}

// ============================================================================
// 0x802d78cc - options subscreen (award-gated rows)
// ============================================================================
void menu_mode_options_screen_build(int screen)
{
    int titleStr;
    titleStr = get_string_by_id(0x422);
    hud_element_init_from_layout(0x800a000, (void *)titleStr, 0, 0);
    *(int *)(screen + 0x214) = 1;
    hud_set_context_property(0x32200);
    hud_set_item_padding_mode(FLOAT_8061a0fc, 2, 1, 1);
    get_string_by_id(0x423);
    hud_add_list_item();
    get_string_by_id(0x424);
    hud_add_list_item();
    if (__ms_award_dispatch() == 0) {
        int last = (int)hud_get_last_created_item();
        hud_item_set_locked(last, 1);
        int *last2 = hud_get_last_created_item();
        hud_item_set_icon_visible(last2, (int)&DAT_8047629c);
    }
    get_string_by_id(0x425);
    hud_add_list_item();
    if (__ms_award_dispatch() == 0) {
        int last = (int)hud_get_last_created_item();
        hud_item_set_locked(last, 1);
        int *last2 = hud_get_last_created_item();
        hud_item_set_icon_visible(last2, (int)&DAT_804762b0);
    }
    hud_element_apply_center_xy(DAT_806119e0);
}

// ============================================================================
// 0x802d7a14 - commit selected sub-mode then route push
// ============================================================================
void menu_mode_select_confirm(unsigned long long p1, double p2, double p3,
                              unsigned long long p4, unsigned long long p5,
                              unsigned long long p6, unsigned long long p7,
                              unsigned long long p8, int screen)
{
    unsigned int sub;
    int          mode;

    sub = *(unsigned int *)(*(int *)(screen + 0x194) + 0x14);
    if ((int)sub < 0x14) {
        DAT_805cc68e = (unsigned char)sub;
        DAT_805cc68d = (unsigned char)*(int *)(&DAT_803f2dd0 + (sub & 0xff) * 0x20);
        if ((DAT_805cc68c == '&') || (DAT_805cc68c == '\x1b')) {
            mode = *(int *)(*(int *)(screen + 0x194) + 0x14);
            if ((mode == 0x10) || (mode == 0x11)) {
                DAT_805cc68c = '\x1b';
            } else {
                DAT_805cc68c = '&';
            }
        }
        if ((DAT_805cc614 == 0xb) || (DAT_805cc614 == 0xd) || (DAT_805cc614 == 0xc)) {
            hud_screen_push();
        } else if (DAT_805cc614 == 9) {
            if (DAT_805cc68e == '\x02') {
                menu_preview_system_init(p1, p2, p3, (int)p4, (int)p5, (int)p6, (int)p7, (int)p8);
            } else {
                hud_screen_push();
            }
        } else {
            hud_screen_push();
        }
    }
}

// ============================================================================
// 0x802d7b1c - per-mode grid cell render callback
// ============================================================================
void menu_mode_grid_cell_render(unsigned int a, unsigned int b, float *rect)
{
    int      focusElem;
    int      elem;
    int      spriteId;
    int      labelId;
    int      modeIdx;
    unsigned int color;
    unsigned char *label;
    unsigned char *upper;
    unsigned int fiveA;
    unsigned int fiveB;
    long long framePair;
    double   baseX;
    double   scaleX;
    double   rowY;
    double   fade;
    float    panel[4];   // {left, top, right, bottom}
    short    sv;
    float    span;
    float    inset;
    // paired-single FP spill inputs (non-matching, modelled as locals)
    double in_f28 = 0.0, in_f29 = 0.0, in_f30 = 0.0, in_f31 = 0.0;
    double in_ps28_1 = 0.0, in_ps29_1 = 0.0, in_ps30_1 = 0.0, in_ps31_1 = 0.0;

    (void)a; (void)b;
    (void)in_f28; (void)in_f29; (void)in_f30; (void)in_f31;
    (void)in_ps28_1; (void)in_ps29_1; (void)in_ps30_1; (void)in_ps31_1;

    framePair = FUN_803826e8();
    focusElem = (int)((unsigned long long)framePair >> 0x20);
    elem      = (int)framePair;
    spriteId  = -1;
    labelId   = 0;

    fiveA = mode_get_const_five() ^ 0x80000000;
    span  = FLOAT_80619eec * (rect[3] - rect[1]);
    inset = FLOAT_80619f30 * span;
    panel[2] = -(FLOAT_80619f6c * FLOAT_80611a68 - rect[2]);
    span     = span - inset;
    panel[1] = rect[1] + span;
    panel[0] = -(FLOAT_80619f38 * FLOAT_8060f958 * (inset / FLOAT_8060f95c) - panel[2]);
    panel[3] = rect[3] - span;

    baseX = (double)(FLOAT_80611a68 *
        (float)((double)CONCAT44(0x43300000, fiveA) - DOUBLE_80619f00) + *rect);
    fiveB = mode_get_const_five() ^ 0x80000000;
    modeIdx = *(int *)(elem + 0x14);
    scaleX = -(double)(FLOAT_80611a68 *
        ((float)((double)CONCAT44(0x43300000, fiveB) - DOUBLE_80619f00) - FLOAT_80619f38) - panel[0]);

    if (modeIdx < 0x14) {
        int off = modeIdx * 0x20;
        if ((modeIdx != 2) || (DAT_80611014 < 2)) {
            if ((*(unsigned int *)(elem + 0x28) & 1) == 0) {
                sv = *(short *)(&DAT_803f2de6 + off);
            } else {
                sv = *(short *)(&DAT_803f2de8 + off);
            }
            labelId = (int)sv;
        }
        spriteId = (int)*(short *)(&DAT_803f2dea + off);
    } else {
        labelId = 0x168c;
    }

    text_set_font_table(0x12);
    gx_set_sub_material_id(1);
    float rowFrac = (DAT_8060f948 == 3) ? FLOAT_8061a100 : FLOAT_80619f08;
    rowY = (double)(*(float *)(elem + 0x70) * FLOAT_80611a64 * (FLOAT_8061a064 - rowFrac) + rect[1]);

    if (FLOAT_80619f08 < *(float *)(elem + 0x34)) {
        fade = (double)(FLOAT_80619f0c * *(float *)(focusElem + 0xf0) * *(float *)(elem + 0x34));
        int gated = 0;
        if (((DAT_805cc614 == 9) ||
             (((DAT_805cc614 == 0xb) || (DAT_805cc614 == 0xd) || (DAT_805cc614 == 0xc)) &&
              (DAT_805cc68c == 'd'))) && (__ms_award_wrap1() != 0)) {
            gated = 1;
        }
        if (gated || ((*(unsigned int *)(elem + 0x28) & 1) == 0)) {
            if (labelId != 0) {
                if ((*(unsigned int *)(elem + 0x28) & 1) == 0) {
                    color = FUN_803825c0(fade) & 0xff | 0x64646400;
                } else {
                    color = FUN_803825c0(fade) & 0xff | 0x28282800;
                }
                if (DAT_8060f948 == 2) {
                    FLOAT_8061167c = FLOAT_8061167c * FLOAT_8061a0c8;
                    label = (unsigned char *)get_string_by_id(labelId);
                    upper = text_check_scale((double)(float)(scaleX - baseX), label);
                    text_draw_char(baseX, rowY, upper, color);
                    FLOAT_8061167c = FLOAT_8061167c * FLOAT_8061a104;
                } else {
                    label = (unsigned char *)get_string_by_id(labelId);
                    upper = text_check_scale((double)(float)(scaleX - baseX), label);
                    text_draw_char(baseX, rowY, upper, color);
                }
            }
        } else {
            text_set_font_table(3);
            color = FUN_803825c0(fade);
            label = (unsigned char *)get_string_by_id(0x471);
            text_draw_char(baseX, rowY, label, (color & 0xff) | 0x64281e00);
        }
    }
    if ((spriteId != -1) && (100 < DAT_80612614)) {
        sprite_get_texture_id(spriteId);
        hud_panel_draw_award(focusElem, elem, panel);
    }
    FUN_80382734();
}

// ============================================================================
// 0x802d7e88 - populate the mode grid rows (lock / award gating)
// ============================================================================
void menu_mode_grid_populate(void)
{
    unsigned int mask;
    unsigned int *info;
    int   item;
    int   labelStr;
    int   locked;
    int   mode;
    int   off;
    long long framePair;
    int   hasBack;

    framePair = FUN_803826d4();
    hasBack = (int)framePair;   // r4 result: whether a back item is present
    if (hasBack != 0) {
        get_string_by_id(0x1a5f);
        hud_add_list_item();
    }
    if (((DAT_805cc614 == 0xb) || (DAT_805cc614 == 0xd) || (DAT_805cc614 == 0xc)) &&
        (DAT_805cc68c == '\x1b')) {
        DAT_805cc68c = '&';
    }

    mode = 2;
    off = 0x40;
    do {
        if ((*(int *)(&DAT_803f2dcc + off) != 0) && (mode != 0x11)) {
            locked = 0;
            bool skip = false;
            if ((DAT_805cc614 == 0xb) || (DAT_805cc614 == 0xd) || (DAT_805cc614 == 0xc)) {
                if ((DAT_805cc68c == 'd') && (hasBack == 0)) {
                    info = (unsigned int *)FUN_80339b90((int)DAT_80612be8, 0xc77668);
                    DAT_805c7ae0 = *info;
                    DAT_805cc634 = 0x10;
                    DAT_805cc68c = (char)DAT_803f35a4;
                }
            }
            // row-eligibility gate (matches the original's mode-id whitelist)
            bool eligible =
                (mode == 3) || (mode == 4) || (mode == 0x10) || (mode == 10) ||
                (mode == 0x12) || (mode == 5) || (mode == 0xf) || (mode == 7) ||
                (mode == 8) || (mode == 0xc) || (mode == 9) || (mode == 6);
            bool notCoop5 = (DAT_805cc68c != '5') || (mode != 0xc);

            if (((DAT_805cc614 == 0xb) || (DAT_805cc614 == 0xd) || (DAT_805cc614 == 0xc)) &&
                (DAT_805cc68c == 'd') && (hasBack == 0)) {
                // (availability record already resolved above)
            }

            if (eligible && notCoop5) {
                // availability filtering (level_get_info_by_id mask) — when the
                // masked availability is zero the row is skipped entirely.
                if ((DAT_805cc68c == '&') && (hasBack == 0)) {
                    if ((mode != 0x10) && (mode != 0x11)) {
                        info = (unsigned int *)level_get_info_by_id(0x26);
                        if (info != (unsigned int *)0) {
                            mask = *info & *(unsigned int *)(&DAT_803f2dd4 + off);
                            if (mask == 0) skip = true;
                        }
                    }
                } else if (hasBack == 0) {
                    info = (unsigned int *)level_get_info_by_id((int)DAT_805cc68c);
                    if (info != (unsigned int *)0) {
                        mask = *info & *(unsigned int *)(&DAT_803f2dd4 + off);
                        if (mask == 0) skip = true;
                    }
                }

                if (!skip) {
                    bool doRow;
                    if (((DAT_805cc614 == 9) ||
                         (((DAT_805cc614 == 0xb) || (DAT_805cc614 == 0xd) || (DAT_805cc614 == 0xc)) &&
                          (DAT_805cc68c == 'd'))) && (hasBack == 0)) {
                        if ((DAT_805c7ae0 & *(unsigned int *)(&DAT_803f2dd4 + off)) == 0) {
                            locked = 1;
                        } else if (FUN_8030f0d0(mode) == 0) {
                            if ((mode == 2) && (1 < DAT_80611014)) { locked = 1; }
                        } else {
                            locked = 1;
                        }
                        doRow = (mode < 0x14);
                    } else {
                        doRow = (mode != 2);
                    }

                    if (doRow && (mode < 0x14 ? true : true)) {
                        if (mode < 0x14) {
                            if (__ms_award_wrap1() == 0) { locked = 1; }
                            item = __ms_add_item();
                            labelStr = get_string_by_id((int)*(short *)(&DAT_803f2de4 + off));
                            *(int *)(item + 0x20) = labelStr;
                            *(float *)(item + 0x40) = FLOAT_80619f38;
                            hud_item_set_locked(item, locked);
                        } else {
                            item = __ms_add_item();
                            labelStr = get_string_by_id(mode + 0x40c);
                            *(int *)(item + 0x20) = labelStr;
                            *(float *)(item + 0x40) = FLOAT_80619f38;
                            hud_item_set_locked(item, locked);
                            *(unsigned int *)(item + 0x28) = *(unsigned int *)(item + 0x28) | 1;
                        }
                    }
                }
            }
        }
        mode++;
        off += 0x20;
    } while (mode <= 0x15);

    FUN_80382720();
}

// ============================================================================
// 0x802d8228 - pop current screen thunk
// ============================================================================
void menu_mode_screen_pop(void)
{
    hud_screen_pop_current();
}

// ============================================================================
// 0x802d8248 - mode-select screen build
// ============================================================================
void menu_mode_select_screen_build(int screen)
{
    unsigned int flags;
    int titleStr;

    flags = 0xc000;
    if ((DAT_805cc614 != 0xb) && (1 < (unsigned int)(DAT_805cc614 - 0xc))) {
        flags = 0x800c000;
    }
    hud_element_init_from_layout((int)flags, 0, 0, (int)&menu_mode_screen_pop);
    titleStr = get_string_by_id(0x35e);
    *(int *)(screen + 0x200) = titleStr;
    *(int *)(screen + 0x214) = 0x21;
    hud_set_margins(FLOAT_80619fc4, FLOAT_80619fec, 1);
    hud_element_scroll_horizontal((int)FLOAT_80619f08, 3);
    menu_mode_grid_populate();
    hud_element_apply_scroll();
    hud_element_apply_center_xy(DAT_806119e0);
    hud_element_finalize_frame();
}

// ============================================================================
// 0x802d8308 - set mode 5 (+flag) then push
// ============================================================================
void menu_mode_push_mode5(void)
{
    DAT_805cc614 = 5;
    DAT_805cc68f = 1;
    hud_screen_push();
}

// ============================================================================
// 0x802d834c - set mode 6 then push
// ============================================================================
void menu_mode_push_mode6(void)
{
    DAT_805cc614 = 6;
    hud_screen_push();
}

// ============================================================================
// 0x802d8388 - player-count (1P/2P) screen build
// ============================================================================
void menu_mode_players_screen_build(void)
{
    int titleStr;
    int iconStr;
    int item;

    titleStr = get_string_by_id(0x28d);
    hud_element_init_from_layout(0x800a000, (void *)titleStr, 0, 0);
    hud_set_item_padding_mode(FLOAT_80619f38, 1, 1, 1);

    get_string_by_id(0x3fd);
    hud_add_list_item();
    iconStr = get_string_by_id(0x1651);
    item = (int)hud_get_last_created_item();
    hud_item_set_icon((int *)item, iconStr);

    get_string_by_id(0x3fe);
    hud_add_list_item();
    if (DAT_80611014 < 2) {
        iconStr = get_string_by_id(0x1652);
        item = (int)hud_get_last_created_item();
        hud_item_set_icon((int *)item, iconStr);
    } else {
        iconStr = get_string_by_id(0x1655);
        item = (int)hud_get_last_created_item();
        hud_item_set_icon((int *)item, iconStr);
        int locked = (int)hud_get_last_created_item();
        hud_item_set_locked(locked, 1);
    }
    hud_element_apply_center_xy(DAT_806119e0);
    hud_element_finalize_frame();
}
