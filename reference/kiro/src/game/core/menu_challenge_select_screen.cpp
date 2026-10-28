// ============================================================================
// menu_challenge_select_screen.cpp
// Batch: batch_815
// Address range: 0x802d5db8 - 0x802d5f7c, 0x802d6128 - 0x802d63a8
// Date: 2026-09-11T03:35:00Z
//
// Front-end map / challenge-select screen (the screen reached after the
// match-settings screen). Covers the screen-push thunks, a small "record
// unavailable" confirm screen, the challenge/difficulty list builder (with
// locked rows + lock icons), and two HUD render callbacks: a scissored
// title/label renderer and the objective-marker row renderer that draws each
// challenge objective's marker + best-record bar.
//
// Layout:
//   menu_challenge_push_settings          0x802d5db8  thunk -> match_settings_push
//   menu_challenge_record_screen_build    0x802d5dd8  small confirm screen
//   menu_challenge_push_mode8             0x802d5eac  set mode 8 + push
//   menu_challenge_push_conditional       0x802d5ee8  push settings or plain
//   menu_challenge_stream_kick            0x802d5f4c  kick a stream/DVD request
//   menu_challenge_title_render           0x802d5f7c  scissored title label draw
//   menu_challenge_screen_enter           0x802d6128  hud_screen_push thunk
//   menu_challenge_list_build             0x802d6154  difficulty list + lock icons
//   menu_challenge_push_selected          0x802d636c  stash selection + push
//   menu_challenge_objective_row_render   0x802d63a8  objective markers + record bar
//
// NON-MATCHING: incoming paired-single FP spill regs (in_f31/in_ps31_1) are
// modelled as explicit locals; the frame-actor thunk pairs (FUN_803826d4 /
// FUN_80382720 / FUN_80382734) return 64-bit register pairs split by shifts;
// CONCAT44 magic-bias int->float conversions are kept verbatim. Render
// callbacks are passed to hud_* builders as function pointers (cast through the
// generated int-param prototypes).
// ============================================================================

#include "common.h"
#include "menu.h"     // pulls generated_decls.h (named cross-module helpers)
#include "hud.h"
#include "gx.h"
#include "sprite.h"
#include "text.h"

// ---- frame / prologue helper thunks ---------------------------------------
extern "C" int       FUN_803826e8(void);   // frame-actor accessor (hud element ptr)
extern "C" long long FUN_803826d4(void);   // frame-actor accessor: (element,flag) pair
extern "C" void      FUN_80382720(void);    // frame-actor tail (row render)
extern "C" void      FUN_80382734(void);    // frame-actor tail (title render)
extern "C" unsigned int FUN_803825c0(double intensity); // fade/alpha 0..255 compute

// ---- raw FUN_ helpers not yet in generated headers ------------------------
extern "C" int  *FUN_80339b90(int a, int b);  // resolve pointer record by (ctx, key)
extern "C" int   FUN_8038b5c8(int p);          // record-state query (0 = has settings)
extern "C" int   FUN_80339c28(void);           // challenge-unlocked query (0 = locked)
extern "C" void  FUN_8033c468(int req, int a, int b, int c); // kick stream/DVD request

// Named cross-module helpers (hud_*, gx_*, text_*, sprite_*, worm_*, stats_*,
// mode_get_const_five, get_string_by_id, menu_match_settings_screen_push)
// resolve through generated_decls.h / generated_externs.h.

// ---- raw scalar ROM globals -----------------------------------------------
extern "C" int  *DAT_80612be8;   // screen-push context (first word = kind tag)
extern "C" int   DAT_806119e0;   // layout center context handle
extern "C" int   DAT_805cc614;   // active game mode id
extern "C" int   DAT_805cc620;   // selected challenge/objective id
extern "C" int   DAT_80611014;   // active flag-bit id (entity test)
extern "C" int   DAT_80611654;   // text batch-active flag
extern "C" int   DAT_80612ac4;   // active stream/DVD request handle

// ---- float / double ROM constants -----------------------------------------
extern "C" float  FLOAT_80619f74;  // record-screen item height
extern "C" float  FLOAT_80619f0c;  // title fade scale
extern "C" float  FLOAT_80619eec;  // interpolation midpoint (0.5-ish)
extern "C" float  FLOAT_80619f38;  // title horizontal scale
extern "C" float  FLOAT_80619f50;  // spacer height (default)
extern "C" float  FLOAT_80619fbc;  // spacer height (challenge variant)
extern "C" float  FLOAT_80619fc4;  // extra-row padding
extern "C" float  FLOAT_8061a014;  // marker base x
extern "C" float  FLOAT_8060f958;  // ui scale fraction
extern "C" float  FLOAT_80619f5c;  // marker row vertical scale
extern "C" float  FLOAT_80611a64;  // marker row pixel offset
extern "C" float  FLOAT_8061a088;  // marker row advance
extern "C" float  FLOAT_80619f08;  // small threshold / spacer
extern "C" float  FLOAT_80619f6c;  // scissor header margin
extern "C" float  FLOAT_80611a68;  // scissor header pixel offset
extern "C" float  FLOAT_80619f2c;  // scroll-bar vertical scale
extern "C" float  FLOAT_8061a0dc;  // scroll-bar top offset
extern "C" float  FLOAT_80619f84;  // footer margin / scissor bias
extern "C" double DOUBLE_80619f00; // int->float magic-bias constant

// ============================================================================
// 0x802d5db8 - push the match-settings screen
// ============================================================================
void menu_challenge_push_settings(void)
{
    menu_match_settings_screen_push();
}

// ============================================================================
// 0x802d5dd8 - "record unavailable" small confirm screen
// ============================================================================
void menu_challenge_record_screen_build(void)
{
    int titleStr;
    int item;

    FUN_80339b90((int)DAT_80612be8, 0xc77668);
    titleStr = get_string_by_id(0x3d4);
    hud_element_init_from_layout(0xa00c, (void *)titleStr,
                                 (int)&menu_challenge_push_settings, 0);
    hud_add_list_item();
    item = hud_get_item_counter();
    *(float *)(item + 0x44) = FLOAT_80619f74;
    hud_add_callback_item_default(1);
    hud_add_list_item();
    hud_element_finalize_frame();
    hud_element_apply_center_xy((int *)DAT_806119e0);
}

// ============================================================================
// 0x802d5eac - set game mode 8 then push
// ============================================================================
void menu_challenge_push_mode8(void)
{
    DAT_805cc614 = 8;
    hud_screen_push();
}

// ============================================================================
// 0x802d5ee8 - push settings screen when record has state, else plain push
// ============================================================================
void menu_challenge_push_conditional(void)
{
    int *rec;

    rec = FUN_80339b90((int)DAT_80612be8, 0xc77668);
    if (FUN_8038b5c8((int)rec + 0x156) == 0) {
        menu_match_settings_screen_push();
    } else {
        hud_screen_push();
    }
}

// ============================================================================
// 0x802d5f4c - kick a stream/DVD request for the active handle
// ============================================================================
void menu_challenge_stream_kick(void)
{
    FUN_8033c468(DAT_80612ac4, 0, 0, 1);
}

// ============================================================================
// 0x802d5f7c - scissored title/label render callback
// ============================================================================
void menu_challenge_title_render(unsigned int a, unsigned int b, float *rect)
{
    int          elem;
    unsigned int alpha;
    int         *rec;
    unsigned int five;
    unsigned char *label;
    double       x;
    double       y;
    double       scaleW;

    elem  = FUN_803826e8();
    alpha = FUN_803825c0((double)(FLOAT_80619f0c * *(float *)(elem + 0xf0))) & 0xff;
    x = (double)(FLOAT_80619eec * (rect[2] - rect[0]) + rect[0]);
    y = (double)(FLOAT_80619eec * (rect[3] - rect[1]) + rect[1]);

    gx_set_sub_material_id(1);
    text_set_font_table(*(int *)(*(int *)(elem + 0x1c) + 0x44));
    DAT_80611654 = 1;

    if (*DAT_80612be8 == 0xc77667) {
        rec = FUN_80339b90((int)DAT_80612be8, 0xc77668);
        five = (unsigned int)mode_get_const_five();
        scaleW = -(double)(FLOAT_80619f38 *
            (float)((double)CONCAT44(0x43300000, five ^ 0x80000000) - DOUBLE_80619f00) -
            (rect[2] - rect[0]));
        text_set_font_table(*(int *)(*(int *)(elem + 0x1c) + 0x48));
        label = text_check_scale(scaleW, (u8 *)((int)rec + 0x62));
        if (*label == 0) {
            label = (unsigned char *)get_string_by_id(0x3cb);
        }
        {
            double h = text_get_char_height(label);
            text_draw_char_drop_shadow(x,
                -(double)(float)((double)FLOAT_80619eec * h - y),
                label, alpha | 0x64646400, alpha);
        }
    } else {
        unsigned int raw = get_string_by_id(0x3cd);
        label = text_string_to_upper(raw);
        {
            double h = text_get_char_height(label);
            text_draw_char_drop_shadow(x,
                -(double)(float)((double)FLOAT_80619eec * h - y),
                label, alpha | 0x64646400, alpha);
        }
    }
    DAT_80611654 = 0;
    FUN_80382734();
}

// ============================================================================
// 0x802d6128 - screen enter thunk
// ============================================================================
void menu_challenge_screen_enter(void)
{
    hud_screen_push();
}

// ============================================================================
// 0x802d6154 - challenge/difficulty list builder (locked rows + lock icons)
// ============================================================================
void menu_challenge_list_build(int screen)
{
    float spacerH;
    int   titleStr;
    int   item;
    int   iconStr;
    int  *rec;

    titleStr = get_string_by_id(0x364);
    hud_element_init_from_layout(0x8000, (void *)titleStr, 0,
                                 (int)&menu_challenge_screen_enter);
    hud_set_context_property(0x32a00);
    get_string_by_id(0xe2a);
    hud_add_list_item();
    get_string_by_id(0x3c9);
    hud_add_list_item();
    get_string_by_id(0x3ca);
    hud_add_list_item();

    if ((*DAT_80612be8 != 0xc77667) || (FUN_80339c28() == 0)) {
        item = hud_get_item_counter();
        hud_item_set_locked(item, 1);
        iconStr = get_string_by_id(0x1656);
        int *lastItem = hud_get_last_created_item();
        hud_item_set_icon(lastItem, iconStr);
    }
    hud_add_default_callback_item();

    spacerH = FLOAT_80619f50;
    if (*DAT_80612be8 == 0xc77667) {
        spacerH = FLOAT_80619fbc;
    }
    hud_add_spacer_item(spacerH, 0, (int)&menu_challenge_title_render);

    if ((*DAT_80612be8 == 0xc77667) &&
        (rec = FUN_80339b90((int)DAT_80612be8, 0xc77668),
         *(char *)((int)rec + 0x76) != '\0')) {
        hud_set_item_padding_mode(FLOAT_80619fc4, 1, 1, 1);
        hud_add_list_item();
    }
    hud_element_apply_center_xy((int *)DAT_806119e0);
    *(unsigned int *)(screen + 0x214) = *(unsigned int *)(screen + 0x214) | 8;
}

// ============================================================================
// 0x802d636c - stash selected challenge id then push
// ============================================================================
void menu_challenge_push_selected(int screen)
{
    DAT_805cc620 = *(int *)(screen + 0x188);
    hud_screen_push();
}

// ============================================================================
// 0x802d63a8 - objective-marker row render (markers + best-record bar)
// ============================================================================
void menu_challenge_objective_row_render(unsigned int a, unsigned int b, float *rect)
{
    int      focusElem;
    int      elem;
    int      desc;
    unsigned char *modeObj;
    bool     flagged;
    unsigned int alpha;
    unsigned char *label;
    int      markerY;
    int      idx;
    int      mode;
    unsigned int markerX;
    int      objByteOff;
    long long framePair;
    double   heightSpan;
    // objective-marker accumulators (double-precision scratch the decompiler used)
    double   accA;
    double   accB;
    double   accC;
    // scissor rect
    int      scissX0, scissY0, scissX1;
    unsigned int scissY1;
    float    barRect[4];
    double   in_f31 = 0.0;
    double   in_ps31_1 = 0.0;

    (void)in_f31;
    (void)in_ps31_1;
    (void)accA; (void)accB; (void)accC;

    framePair = FUN_803826d4();
    focusElem = (int)((unsigned long long)framePair >> 0x20);
    elem      = (int)framePair;
    desc      = worm_objective_get_descriptor(*(int *)(elem + 0x14));
    objByteOff = 0;
    markerX = (unsigned int)(FLOAT_8061a014 * FLOAT_8060f958 + *rect);
    markerY = (int)(FLOAT_80619f5c * *(float *)(elem + 0x70) * FLOAT_80611a64 + rect[1]);

    for (idx = 0; idx < *(int *)(desc + 4); idx++) {
        mode = *(int *)(desc + objByteOff + 8);
        modeObj = worm_mode_get_object(mode);
        if ((modeObj != (unsigned char *)0) &&
            (flagged = worm_entity_test_flag_bit((int)modeObj, DAT_80611014), flagged) &&
            ((*(unsigned int *)(modeObj + 0x18) & 0x20) == 0)) {
            unsigned char *rec = (unsigned char *)stats_get_challenge_record(0, mode);
            hud_marker_draw(*(int *)(rec + 0xc), markerX, markerY,
                            (int)(FLOAT_8061a014 * FLOAT_8060f958), 0x14);
            markerX = (unsigned int)(FLOAT_8061a088 * FLOAT_8060f958 +
                (float)((double)CONCAT44(0x43300000, markerX ^ 0x80000000) - DOUBLE_80619f00));
        }
        objByteOff += 4;
    }

    if ((FLOAT_80619f08 < *(float *)(elem + 0x34)) && ((*(unsigned int *)(elem + 0x28) & 1) != 0)) {
        alpha = FUN_803825c0((double)(FLOAT_80619f0c * *(float *)(focusElem + 0xf0) *
                                      *(float *)(elem + 0x34)));
        text_set_font_table(3);
        label = (unsigned char *)get_string_by_id(0x471);
        {
            double lx = (double)(float)((double)CONCAT44(0x43300000,
                            (markerX + 10) ^ 0x80000000) - DOUBLE_80619f00);
            double ly = (double)(float)((double)CONCAT44(0x43300000,
                            (unsigned int)(markerY + 3) ^ 0x80000000) - DOUBLE_80619f00);
            text_draw_char(lx, ly, label, (alpha & 0xff) | 0x64281e00);
        }
    }

    gx_set_sub_material_id(1);
    if (*(int *)(desc + 0x20) != -1) {
        heightSpan = (double)(rect[3] - rect[1]);
        gx_get_scissor_rect(&scissX0, &scissY0, &scissX1, (int *)&scissY1);
        int clampedBottom = (int)(FLOAT_80619f84 +
            (float)((double)((unsigned long long)CONCAT44(0x43300000, scissY1 ^ 0x80000000)) -
                    DOUBLE_80619f00));
        gx_set_scissor_rect((unsigned int)scissX0, (unsigned int)scissY0, scissX1, clampedBottom);
        barRect[2] = rect[2] - FLOAT_80619f6c * FLOAT_80611a68;
        barRect[0] = -((float)((double)FLOAT_80619f2c * heightSpan) * FLOAT_8060f958 - barRect[2]);
        barRect[1] = (float)((double)FLOAT_8061a0dc * heightSpan + (double)rect[1]);
        barRect[3] = FLOAT_80619f84 + rect[3];
        int texId = sprite_get_texture_id(*(int *)(desc + 0x20));
        hud_bar_draw_split(focusElem, elem, barRect, texId, -1);
    }
    FUN_80382720();
}
