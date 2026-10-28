// ============================================================================
// menu_challenge_objective_screen.cpp
// Batch: batch_816
// Address range: 0x802d6670 - 0x802d6df8, 0x802d6f70 - 0x802d6fa8,
//                0x802d7510 - 0x802d75b0
// Date: 2026-09-11T04:05:00Z
//
// Arcade / challenge objective-select + results screens (reached from the
// challenge-select screen). Covers the objective list builders (per game mode,
// with locked rows + award icons), the mode-select push, and the objective /
// best-record render callbacks that draw markers, formatted result lines and
// scissored record bars.
//
// Layout:
//   menu_objective_list_build         0x802d6670  objective list (award-gated)
//   menu_objective_push_mode7         0x802d6880  set mode 7 + navigate
//   menu_objective_single_build       0x802d68c0  single-objective screen
//   menu_objective_push_selected      0x802d6958  stash objective id + push
//   menu_objective_row_render         0x802d69bc  objective marker + record line
//   menu_objective_award_list_build   0x802d6df8  per-objective award list
//   menu_objective_preview_push       0x802d6f70  fill preview state + push
//   menu_objective_record_render      0x802d6fa8  difficulty record table render
//   menu_objective_display_render     0x802d7510  objective display render cb
//   menu_objective_detail_build       0x802d75b0  objective detail screen build
//
// NON-MATCHING: incoming paired-single / FP spill regs (in_f31/in_ps31_1,
// in_f7/in_f8, extraout_f1) are modelled as explicit locals; frame-actor thunk
// pairs (FUN_803826xx / FUN_803827xx) return 64-bit register pairs split by
// shifts; CONCAT44 magic-bias int->float conversions are kept verbatim. The
// worm_* result/objective formatters and mode_fill_state_word are declared void
// in the generated headers but their return values are consumed here, so they
// are called through address-bound function pointers. Render callbacks are
// passed to hud_* builders as function pointers.
// ============================================================================

#include "common.h"
#include "menu.h"     // pulls generated_decls.h
#include "hud.h"
#include "gx.h"
#include "sprite.h"
#include "text.h"

// ---- frame / prologue helper thunks ---------------------------------------
extern "C" int       FUN_803826d8(void);   // frame-actor accessor (element ptr)
extern "C" int       FUN_803826e4(void);   // frame-actor accessor (element ptr)
extern "C" int       FUN_803826e8(void);   // frame-actor accessor (element ptr)
extern "C" long long FUN_803826d0(void);   // frame-actor accessor: (focus,element) pair
extern "C" long long FUN_803826b4(void);   // frame-actor accessor: (focus,element) pair
extern "C" unsigned int FUN_803825c0(double intensity); // fade/alpha 0..255
extern "C" unsigned int FUN_80366414(void);  // net/online-active query (0 = local)
extern "C" void      FUN_80382700(void);    // frame-actor tail
extern "C" void      FUN_8038271c(void);    // frame-actor tail
extern "C" void      FUN_80382724(void);    // frame-actor tail
extern "C" void      FUN_80382730(void);    // frame-actor tail
extern "C" void      FUN_80382734(void);    // frame-actor tail

// ---- void-but-return-used helpers: address-bound function pointers --------
typedef unsigned long long (*fill_state_fp)(int);
static const fill_state_fp __obj_fill_state = (fill_state_fp)0x802c7d48; // mode_fill_state_word
typedef int (*results_fmt_fp)(unsigned long long, double, double, double, long long, double,
                              unsigned long long, unsigned long long, int, int, int,
                              unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);
static const results_fmt_fp __obj_results_fmt = (results_fmt_fp)0x8022a8dc; // worm_results_format_line
typedef unsigned char *(*obj_line_fp)(double, double, double, double, double, double,
                                      unsigned long long, unsigned long long);
static const obj_line_fp __obj_line_fmt = (obj_line_fp)0x8022b580; // worm_mode_format_objective_line
typedef unsigned int (*obj_draw_fp)(double, double, double, unsigned long long, unsigned long long,
                                    unsigned long long, unsigned long long, unsigned long long);
static const obj_draw_fp __obj_draw = (obj_draw_fp)0x8022cfe4; // worm_objective_draw_display
// hud_add_list_item returns the new item handle (generated decl is void).
typedef int (*add_item_fp)(void);
static const add_item_fp __obj_add_item = (add_item_fp)0x8003ea8c;
// hud_text_apply_case_scale returns the transformed text ptr (generated decl is void).
typedef unsigned char *(*case_scale_fp)(float, char *, unsigned int);
static const case_scale_fp __obj_case_scale = (case_scale_fp)0x8003ebe4;

// Named helpers (hud_*, gx_*, text_*, sprite_*, worm_*, award_*, stats_*,
// difficulty_get_name_string, color_fade_team_*, get_string_by_id,
// menu_preview_system_init) resolve via generated_decls.h / generated_externs.h.

// ---- raw scalar ROM globals -----------------------------------------------
extern "C" int   DAT_805cc614;   // active game mode id
extern "C" int   DAT_805cc620;   // selected challenge id
extern "C" int   DAT_805cc628;   // selected objective/mode id
extern "C" int   DAT_805cc62c;   // active challenge group id
extern "C" int   DAT_80611014;   // active flag-bit id
extern "C" int   DAT_80612ac4;   // stream/nav context handle
extern "C" int  *DAT_806119e0;   // layout center context (array of ptrs)
extern "C" short DAT_803f2de4;   // per-submode default score (short, stride 0x20)
extern "C" int   PTR_FUN_804764fc; // navigation target descriptor
extern "C" unsigned char DAT_80611040; // format scratch buffer

// ---- float / double ROM constants -----------------------------------------
extern "C" float  FLOAT_80619f38, FLOAT_80619fe0, FLOAT_80619f08, FLOAT_80611a68;
extern "C" float  FLOAT_80611674, FLOAT_80619f6c, FLOAT_80619ee8, FLOAT_80619fc0;
extern "C" float  FLOAT_8060f958, FLOAT_8061a010, FLOAT_8061a088, FLOAT_8061a060;
extern "C" float  FLOAT_80611a64, FLOAT_80619f70, FLOAT_80619f0c, FLOAT_80619f2c;
extern "C" float  FLOAT_8061a0dc, FLOAT_80619f84, FLOAT_80619f50, FLOAT_8061a0ec;
extern "C" float  FLOAT_80619f34, FLOAT_8061a014, FLOAT_80619ff4, FLOAT_80619ff8;
extern "C" float  FLOAT_80619fbc, FLOAT_80611a5c, FLOAT_8061a0f4, FLOAT_8061a0f8;
extern "C" double DOUBLE_80619f00; // int->float magic-bias constant

// ============================================================================
// 0x802d6670 - objective list build (award-gated rows)
// ============================================================================
void menu_objective_list_build(void)
{
    int   elem;
    unsigned int online;
    int   titleStr;
    int  *desc;
    unsigned char *modeObj;
    unsigned char *label;
    unsigned int index;
    int   endIndex;
    int   count;
    int   idx;
    int   byteOff;
    double h;

    elem = FUN_803826d8();
    endIndex = 0x10;
    index = ~-(unsigned int)(DAT_805cc614 == 7) & 7;
    if (DAT_805cc614 == 7) { endIndex = 7; }
    online = FUN_80366414();
    titleStr = (DAT_805cc614 == 7) ? 0x361 : 0x426;
    titleStr = get_string_by_id(titleStr);
    hud_element_init_from_layout((int)(~-(unsigned int)(online != 0) & 0x8000000 | 0xe000),
                                 (void *)titleStr, 0, 0);
    *(int *)(elem + 0x214) = 1;
    hud_set_margins(FLOAT_80619f38, FLOAT_80619fe0, 1);
    hud_element_scroll_vertical((int)FLOAT_80619f08, 3);

    for (; (int)index < endIndex; index++) {
        desc = (int *)worm_objective_get_descriptor(index);
        count = 0;
        idx = 0;
        byteOff = 0;
        while (idx < desc[1]) {
            modeObj = worm_mode_get_object(*(int *)((int)desc + byteOff + 8));
            if (modeObj != (unsigned char *)0) {
                if (worm_entity_test_flag_bit((int)modeObj, DAT_80611014) &&
                    ((*(unsigned int *)(modeObj + 0x18) & 0x20) == 0)) {
                    count++;
                }
            }
            idx++;
            byteOff += 4;
        }
        if ((count != 0) && ((*desc == DAT_805cc62c) || (DAT_805cc614 == 7))) {
            int item;
            get_string_by_id(desc[7]);
            item = __obj_add_item();
            text_set_font_table(*(int *)(item + 0x1c));
            label = __obj_case_scale(
                        *(float *)(item + 0x44) * FLOAT_80611a68,
                        *(char **)(item + 0x20), *(unsigned int *)(item + 0x28));
            h = text_get_char_height(label);
            if (h <= (double)FLOAT_80611674) {
                *(float *)(item + 0x40) = FLOAT_80619f6c;
            } else {
                *(float *)(item + 0x40) = FLOAT_80619ee8;
            }
            if (award_check_by_id(index) == 0) {
                int *last = hud_get_last_created_item();
                hud_item_set_locked((int)last, 1);
                int *last2 = hud_get_last_created_item();
                hud_item_set_icon_visible(last2, (int)(desc + 9));
            }
        }
    }
    hud_element_apply_scroll();
    hud_element_apply_center_xy(DAT_806119e0);
    FUN_80382724();
}

// ============================================================================
// 0x802d6880 - set mode 7 then navigate
// ============================================================================
void menu_objective_push_mode7(void)
{
    DAT_805cc614 = 7;
    hud_screen_navigate_to(DAT_80612ac4, (int)&PTR_FUN_804764fc, -1);
}

// ============================================================================
// 0x802d68c0 - single-objective screen
// ============================================================================
void menu_objective_single_build(int screen)
{
    int titleStr;
    titleStr = get_string_by_id(0x361);
    hud_element_init_from_layout(0xa000, (void *)titleStr, 0, 0);
    hud_set_item_padding_mode(FLOAT_80619fc0, 1, 1, 1);
    *(int *)(screen + 0x214) = 1;
    get_string_by_id(7);
    hud_add_list_item();
    hud_element_apply_center_xy(DAT_806119e0);
    hud_element_finalize_frame();
}

// ============================================================================
// 0x802d6958 - stash selected objective mode then push
// ============================================================================
void menu_objective_push_selected(int screen)
{
    int desc;
    desc = worm_objective_get_descriptor(DAT_805cc620);
    DAT_805cc628 = *(int *)(desc + *(int *)(screen + 0x188) * 4 + 8);
    hud_screen_push();
}

// ============================================================================
// 0x802d69bc - objective marker + best-record line render callback
// ============================================================================
void menu_objective_row_render(unsigned int a, unsigned int b, float *rect,
                               unsigned int p4, unsigned int p5,
                               unsigned int p6, unsigned int p7, unsigned int p8)
{
    int      focusElem;
    int      elem;
    int      objMode;
    unsigned char *modeObj;
    unsigned char *rec;
    int     *recPtr;
    unsigned char *label;
    unsigned int alphaLabel;
    unsigned int alphaMarker;
    unsigned int markerX;
    unsigned int markerXadj;
    int      markerY;
    long long framePair;
    double   fx;
    double   heightSpan;
    int      scissX0, scissY0, scissX1;
    unsigned int scissY1;
    float    barRect[4];
    int      lineStr;
    double   in_f7 = 0.0;
    double   in_f8 = 0.0;
    double   in_f31 = 0.0;
    double   in_ps31_1 = 0.0;

    (void)a; (void)b; (void)p4; (void)p5; (void)in_f31; (void)in_ps31_1;

    framePair = FUN_803826d0();
    focusElem = (int)((unsigned long long)framePair >> 0x20);
    elem      = (int)framePair;
    objMode   = worm_objective_get_descriptor(DAT_805cc620);
    objMode   = *(int *)(objMode + *(int *)(elem + 0x14) * 4 + 8);
    modeObj   = worm_mode_get_object(objMode);

    markerX  = (unsigned int)((double)FLOAT_8061a010 * (double)FLOAT_8060f958 + (double)*rect);
    markerY  = (int)(FLOAT_8061a060 * *(float *)(elem + 0x70) * FLOAT_80611a64 + rect[1]);
    markerXadj = (unsigned int)((double)FLOAT_8061a088 * (double)FLOAT_8060f958 +
        (double)(float)((double)CONCAT44(0x43300000, markerX ^ 0x80000000) - DOUBLE_80619f00));

    fx = (double)*(float *)(focusElem + 0xf0);
    alphaMarker = FUN_803825c0((double)(float)((double)FLOAT_80619f70 * fx)) & 0xff;
    alphaMarker |= (DAT_805cc614 == 7) ? 0x5a144600 : 0x14141400;
    alphaLabel = FUN_803825c0((double)(float)((double)FLOAT_80619f0c * fx)) & 0xff;
    alphaLabel |= ((*(unsigned int *)(elem + 0x28) & 1) != 0) ? 0x3c3c3c00 : 0x64646400;

    gx_set_sub_material_id(1);
    rec = (unsigned char *)stats_get_challenge_record(0, objMode);
    hud_marker_draw(*(int *)(rec + 0xc), markerX, markerY - 1, 0x14, 0x14);
    gx_set_sub_material_id(1);

    if (DAT_805cc614 == 7) {
        if ((*(unsigned int *)(elem + 0x28) & 1) == 0) {
            text_set_font_table(0x15);
            recPtr = (int *)stats_get_challenge_record(0, objMode);
            if (*recPtr == 0) {
                lineStr = get_string_by_id(0x3d2);
            } else {
                lineStr = __obj_results_fmt(0, 0, 0, 0, 0, 0, 0, 0, objMode, objMode,
                                            markerY - 1, 0x14, 0x14, p6, p7, p8);
            }
            double lx = (double)(float)((double)CONCAT44(0x43300000, markerXadj ^ 0x80000000) - DOUBLE_80619f00);
            double ly = (double)(float)((double)CONCAT44(0x43300000, markerX ^ 0x80000000) - DOUBLE_80619f00);
            text_draw_char_shadowed(lx, ly, lineStr, alphaLabel, alphaMarker);
        } else {
            unsigned int a2 = FUN_803825c0((double)(FLOAT_80619f0c * *(float *)(focusElem + 0xf0)));
            text_set_font_table(3);
            label = (unsigned char *)get_string_by_id(0x471);
            double lx = (double)(float)((double)CONCAT44(0x43300000, markerXadj ^ 0x80000000) - DOUBLE_80619f00);
            double ly = (double)(float)((double)CONCAT44(0x43300000, markerX ^ 0x80000000) - DOUBLE_80619f00);
            text_draw_char(lx, ly, label, (a2 & 0xff) | 0x64281e00);
        }
    } else {
        unsigned char *mObj2 = worm_mode_get_object(objMode);
        if ((*(unsigned int *)(elem + 0x28) & 1) == 0) {
            text_set_font_table(3);
            unsigned int raw = get_string_by_id(
                (int)*(short *)(&DAT_803f2de4 + *(int *)(mObj2 + 4) * 0x20));
            label = text_string_to_upper(raw);
            double lx = (double)(float)((double)CONCAT44(0x43300000, markerXadj ^ 0x80000000) - DOUBLE_80619f00);
            double ly = (double)(float)((double)CONCAT44(0x43300000, markerX ^ 0x80000000) - DOUBLE_80619f00);
            text_draw_char_shadowed(lx, ly, label, alphaLabel, alphaMarker);
        } else {
            unsigned int a2 = FUN_803825c0((double)(FLOAT_80619f0c * *(float *)(focusElem + 0xf0)));
            text_set_font_table(3);
            label = (unsigned char *)get_string_by_id(0x471);
            double lx = (double)(float)((double)CONCAT44(0x43300000, markerXadj ^ 0x80000000) - DOUBLE_80619f00);
            double ly = (double)(float)((double)CONCAT44(0x43300000, markerX ^ 0x80000000) - DOUBLE_80619f00);
            text_draw_char(lx, ly, label, (a2 & 0xff) | 0x64281e00);
        }
    }

    if (*(int *)(modeObj + 0x88) != -1) {
        heightSpan = (double)(rect[3] - rect[1]);
        gx_get_scissor_rect(&scissX0, &scissY0, &scissX1, (int *)&scissY1);
        int clampedBottom = (int)(FLOAT_80619f84 +
            (float)((double)((unsigned long long)CONCAT44(0x43300000, scissY1 ^ 0x80000000)) - DOUBLE_80619f00));
        gx_set_scissor_rect((unsigned int)scissX0, (unsigned int)scissY0, scissX1, clampedBottom);
        barRect[2] = rect[2] - FLOAT_80619f6c * FLOAT_80611a68;
        barRect[0] = -((float)((double)FLOAT_80619f2c * heightSpan) * FLOAT_8060f958 - barRect[2]);
        barRect[1] = (float)((double)FLOAT_8061a0dc * heightSpan + (double)rect[1]);
        barRect[3] = FLOAT_80619f84 + rect[3];
        int texId = sprite_get_texture_id(*(int *)(modeObj + 0x88));
        hud_bar_draw_split(focusElem, elem, barRect, texId, -1);
    }
    (void)in_f7; (void)in_f8;
    FUN_8038271c();
}

// ============================================================================
// 0x802d6df8 - per-objective award list build
// ============================================================================
void menu_objective_award_list_build(void)
{
    int   elem;
    int   desc;
    unsigned int online;
    int   titleStr;
    short *entity;
    int   idx;
    int   byteOff;
    int   mode;

    elem = FUN_803826e4();
    desc = worm_objective_get_descriptor(DAT_805cc620);
    online = FUN_80366414();
    titleStr = (DAT_805cc614 == 7) ? get_string_by_id(0x362) : get_string_by_id(0x427);
    hud_element_init_from_layout((int)(~-(unsigned int)(online != 0) & 0x8000000 | 0xe000),
                                 (void *)titleStr, 0, 0);
    *(int *)(elem + 0x214) = 1;
    hud_set_margins(FLOAT_80619f38, FLOAT_80619fe0, 1);
    hud_element_scroll_vertical((int)FLOAT_80619f08, 3);

    idx = 0;
    byteOff = 0;
    while (idx < *(int *)(desc + 4)) {
        mode = *(int *)(desc + byteOff + 8);
        entity = (short *)worm_mode_get_object(mode);
        if (entity != (short *)0) {
            if (worm_entity_test_flag_bit((int)entity, DAT_80611014) &&
                ((*(unsigned int *)(entity + 0xc) & 0x20) == 0)) {
                int item;
                get_string_by_id((int)*entity);
                item = __obj_add_item();
                *(float *)(item + 0x40) = FLOAT_80619f38;
                if (award_check_slot_by_ptr(mode, DAT_805cc620) == 0) {
                    int *last = hud_get_last_created_item();
                    hud_item_set_icon_visible(last, (int)(entity + 0x54));
                    int *last2 = hud_get_last_created_item();
                    hud_item_set_locked((int)last2, 1);
                }
            }
        }
        idx++;
        byteOff += 4;
    }
    hud_element_apply_scroll();
    hud_element_apply_center_xy(DAT_806119e0);
    FUN_80382730();
}

// ============================================================================
// 0x802d6f70 - fill preview state then push
// ============================================================================
void menu_objective_preview_push(unsigned long long p1, double p2, double p3,
                                 unsigned long long p4, unsigned long long p5,
                                 unsigned long long p6, unsigned long long p7,
                                 unsigned long long p8)
{
    unsigned long long state;
    (void)p1;
    state = __obj_fill_state(1);
    menu_preview_system_init(state, p2, p3, (int)p4, (int)p5, (int)p6, (int)p7, (int)p8);
    hud_screen_push();
}

// ============================================================================
// 0x802d6fa8 - difficulty record table render callback
// ============================================================================
void menu_objective_record_render(unsigned int a, unsigned int b, float *rect,
                                  unsigned int p4, unsigned int p5,
                                  unsigned int p6, unsigned int p7, unsigned int p8)
{
    int      focusElem;
    int      elem;
    unsigned char *modeObj;
    unsigned char *rec;
    int     *recPtr;
    unsigned char *label;
    unsigned int colName;
    unsigned int colVal;
    unsigned int colX;
    unsigned int rowY;
    int      widthName;
    int      widthVal;
    int      diff;
    unsigned int fadeOff;
    long long framePair;
    double   rowScale;
    double   fx;
    double   in_f7 = 0.0;
    double   in_f8 = 0.0;
    double   extraout_f1 = 0.0;

    (void)a; (void)b; (void)p4; (void)p5;

    framePair = FUN_803826b4();
    focusElem = (int)((unsigned long long)framePair >> 0x20);
    elem      = (int)framePair;
    rowScale  = (double)(*(float *)(elem + 0x70) * FLOAT_80611a64);
    colX = (unsigned int)((double)FLOAT_80619f50 * (double)FLOAT_80611a68 + (double)*rect);
    colVal = (unsigned int)((double)FLOAT_8061a0ec * rowScale);
    rowY = (unsigned int)((double)FLOAT_80619f34 * rowScale + (double)rect[1]);
    modeObj = worm_mode_get_object(DAT_805cc628);
    widthName = 0;
    widthVal = 0;
    double rightX = -(double)(float)((double)FLOAT_8061a014 * (double)FLOAT_80611a68 - (double)rect[2]);
    unsigned int rightXi = (unsigned int)rightX;

    fx = (double)*(float *)(focusElem + 0xf0);
    colName = FUN_803825c0((double)(float)((double)FLOAT_80619f70 * fx)) & 0xff | 0x5a144600;
    unsigned int colBody = FUN_803825c0((double)(float)((double)FLOAT_80619f0c * fx)) & 0xff | 0x64646400;

    text_set_font_table(0x12);
    gx_set_sub_material_id(1);

    // measure pass: widest difficulty name and value
    for (diff = 3; diff > 0; diff--) {
        label = (unsigned char *)difficulty_get_name_string(diff);
        int w = (int)text_get_char_width(label);
        if (widthName < w) widthName = w;
        label = __obj_line_fmt((double)w, rightX, (double)FLOAT_8061a014,
                               (double)FLOAT_80611a68, (double)FLOAT_80619f50, rowScale, in_f7, in_f8);
        w = (int)text_get_char_width(label);
        if (widthVal < w) widthVal = w;
    }

    // header row: record marker + title + best value
    rec = (unsigned char *)stats_get_challenge_record(0, DAT_805cc628);
    double markerXd = (double)(float)((double)CONCAT44(0x43300000, (colVal + 10) ^ 0x80000000) - DOUBLE_80619f00);
    hud_marker_draw(*(int *)(rec + 0xc),
        (int)-(float)(markerXd * (double)FLOAT_8060f958 -
            (double)(float)((double)CONCAT44(0x43300000, colX ^ 0x80000000) - DOUBLE_80619f00)),
        rowY,
        (int)((double)(float)((double)CONCAT44(0x43300000, colVal ^ 0x80000000) - DOUBLE_80619f00) * (double)FLOAT_8060f958),
        colVal);

    int titleStr = get_string_by_id(0x3d1);
    double rowYd = (double)(float)((double)CONCAT44(0x43300000, rowY ^ 0x80000000) - DOUBLE_80619f00);
    double colXd = (double)(float)((double)CONCAT44(0x43300000, colX ^ 0x80000000) - DOUBLE_80619f00);
    text_draw_char_shadowed(colXd, rowYd, titleStr, colBody, colName);

    recPtr = (int *)stats_get_challenge_record(0, DAT_805cc628);
    if (*recPtr == 0) {
        label = (unsigned char *)get_string_by_id(0x3d2);
    } else {
        label = (unsigned char *)__obj_results_fmt(0, rowYd, markerXd, 0, 0, rowScale, 0, 0,
                    DAT_805cc628, DAT_805cc628, colName, 0x43300000, colVal, p6, p7, p8);
    }
    text_get_char_width(label);
    fadeOff = rightXi - (int)(FLOAT_80619f38 * FLOAT_80611a68 +
        (float)((double)CONCAT44(0x43300000, (colX + widthName) ^ 0x80000000) - DOUBLE_80619f00));
    double fadeX = (double)(float)((double)CONCAT44(0x43300000, fadeOff ^ 0x80000000) - DOUBLE_80619f00);
    text_apply_alpha_fade(fadeX, label, 0, 1);
    double valW = text_get_char_width(label);
    text_draw_char_shadowed(
        (double)(float)((double)(float)((double)CONCAT44(0x43300000, rightXi ^ 0x80000000) - DOUBLE_80619f00) - valW),
        (double)(float)((double)CONCAT44(0x43300000, rowY ^ 0x80000000) - DOUBLE_80619f00),
        label, colBody, colName);

    text_set_color_rgb((double)FLOAT_80619ee8, (double)FLOAT_80619ee8, (double)FLOAT_80619ee8);

    // per-difficulty rows
    double rowY2 = (double)FLOAT_80619ff4 * (double)(*(float *)(elem + 0x70) * FLOAT_80611a64) +
        (double)(float)((double)CONCAT44(0x43300000, rowY ^ 0x80000000) - DOUBLE_80619f00);
    for (diff = 3; diff > 0; diff--) {
        unsigned int rowYi = (unsigned int)rowY2;
        gx_set_sub_material_id(1);
        int nameStr = difficulty_get_name_string(diff);
        debug_log_formatted((char *)&DAT_80611040, 0, 0, 0, 0, 0, in_f7, in_f8,
                            (unsigned int)nameStr, colName, 0x43300000, colVal, p6, p7, p8, 0);
        double nx = (double)(float)((double)CONCAT44(0x43300000, colX ^ 0x80000000) - DOUBLE_80619f00);
        double ny = (double)(float)((double)CONCAT44(0x43300000, rowYi ^ 0x80000000) - DOUBLE_80619f00);
        text_draw_char_shadowed(nx, ny, (int)(unsigned int)(unsigned long long)(&DAT_80611040), colBody, colName);
        label = __obj_line_fmt(ny, 0, 0, (double)FLOAT_80619f50, 0, rowScale, in_f7, in_f8);
        text_apply_alpha_fade(fadeX, label, 0, 1);
        unsigned int cc = color_fade_team_c(*(float *)(focusElem + 0xf0), diff);
        unsigned int cb = color_fade_team_b(*(float *)(focusElem + 0xf0), diff);
        double vw = text_get_char_width(label);
        text_draw_char_shadowed(
            (double)(float)((double)(float)((double)CONCAT44(0x43300000, rightXi ^ 0x80000000) - DOUBLE_80619f00) - vw),
            ny, label, cb, cc);
        text_set_color_rgb((double)FLOAT_80619ee8, (double)FLOAT_80619ee8, (double)FLOAT_80619ee8);
        double markerX2 = (double)(float)((double)CONCAT44(0x43300000, (colVal + 10) ^ 0x80000000) - DOUBLE_80619f00);
        hud_marker_draw(diff,
            (int)-(float)(markerX2 * (double)FLOAT_8060f958 -
                (double)(float)((double)CONCAT44(0x43300000, colX ^ 0x80000000) - DOUBLE_80619f00)),
            rowYi + 2,
            (int)((double)(float)((double)CONCAT44(0x43300000, colVal ^ 0x80000000) - DOUBLE_80619f00) * (double)FLOAT_8060f958),
            colVal);
        rowY2 = (double)FLOAT_80619ff8 * (double)(float)(*(float *)(elem + 0x70) * FLOAT_80611a64) +
            (double)(float)((double)((unsigned long long)CONCAT44(0x43300000, rowYi)) - DOUBLE_80619f00);
    }

    if (*(int *)(modeObj + 0x88) != -1) {
        int texId = sprite_get_texture_id(*(int *)(modeObj + 0x88));
        hud_bar_draw_pulsed(focusElem, elem, (int)rect, texId);
    }
    (void)extraout_f1;
    FUN_80382700();
}

// ============================================================================
// 0x802d7510 - objective display render callback
// ============================================================================
void menu_objective_display_render(unsigned long long p1, unsigned long long p2, double p3,
                                   unsigned long long p4, unsigned long long p5,
                                   unsigned long long p6, unsigned long long p7,
                                   unsigned long long p8)
{
    int elem;
    double y;
    double rowScale;
    (void)p1; (void)p2;

    elem = FUN_803826e8();
    worm_mode_get_object(DAT_805cc628);
    y = (double)FLOAT_80619f34;
    rowScale = (double)(*(float *)(elem + 0x70) * FLOAT_80611a64);
    text_set_font_table(0xf);
    gx_set_sub_material_id(1);
    __obj_draw(rowScale, y, p3, p4, p5, p6, p7, p8);
    FUN_80382734();
}

// ============================================================================
// 0x802d75b0 - objective detail screen build
// ============================================================================
void menu_objective_detail_build(unsigned long long p1, double p2, double p3,
                                 unsigned long long p4, unsigned long long p5,
                                 unsigned long long p6, unsigned long long p7,
                                 unsigned long long p8)
{
    int    elem;
    short *modeObj;
    unsigned int online;
    unsigned int drawRet;
    int    titleStr;
    unsigned int scrollAmt;
    double advance;
    (void)p1;

    elem = FUN_803826e8();
    modeObj = (short *)worm_mode_get_object(DAT_805cc628);
    advance = (double)FLOAT_8061a088 * (double)FLOAT_80611a5c;
    online = FUN_80366414();
    drawRet = __obj_draw((double)FLOAT_8061a088, p2, p3, p4, p5, p6, p7, p8);

    titleStr = (DAT_805cc614 == 7) ? 0x363 : 0x428;
    titleStr = get_string_by_id(titleStr);
    hud_element_init_from_layout((int)(~-(unsigned int)(online != 0) & 0x8000000 | 0x4000a100),
                                 (void *)titleStr, 0, 0);
    double drawRetF = (double)CONCAT44(0x43300000, drawRet ^ 0x80000000) - DOUBLE_80619f00;
    *(int *)(elem + 0x214) = 1;
    *(void **)(elem + 0x1b8) = (void *)&menu_objective_preview_push;
    scrollAmt = (unsigned int)((FLOAT_80619fbc + (float)drawRetF) * FLOAT_80611a5c);
    hud_set_margins(FLOAT_80619ee8, FLOAT_80619f08, 1);
    get_string_by_id((int)*modeObj);
    hud_add_list_item();
    hud_add_default_padding();
    hud_add_callback_item_default(0);

    if (worm_mode_has_display_row((int)modeObj) == 0) {
        hud_element_scroll_vertical((int)FLOAT_8061a0f4, 0);
    } else {
        double scr = (double)CONCAT44(0x43300000, (scrollAmt + (int)advance) ^ 0x80000000);
        hud_element_scroll_vertical(
            (int)(FLOAT_8061a0f4 -
                (*(float *)(DAT_806119e0[7] + 0x30) + *(float *)(DAT_806119e0[7] + 0x2c) +
                 (float)(scr - DOUBLE_80619f00))),
            0);
    }
    get_string_by_id((int)modeObj[1]);
    hud_add_list_item();
    hud_element_apply_scroll();

    if (worm_mode_has_display_row((int)modeObj) != 0) {
        get_string_by_id(0x4b2);
        hud_add_list_item();
        hud_add_default_padding();
        hud_add_callback_item_default(0);
        hud_add_spacer_item(
            (float)((double)CONCAT44(0x43300000, scrollAmt ^ 0x80000000) - DOUBLE_80619f00),
            1, (int)&menu_objective_display_render);
    }
    hud_add_callback_item_default(0);
    hud_add_spacer_item(FLOAT_8061a0f8, 1, (int)&menu_objective_record_render);
    hud_element_apply_center_xy(DAT_806119e0);

    online = FUN_80366414();
    if (online != 0) {
        titleStr = get_string_by_id(0x1c1f);
        hud_screen_register_handler_global(2, titleStr, 0, 0);
    }
    FUN_80382734();
}
