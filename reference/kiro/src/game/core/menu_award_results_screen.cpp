// ============================================================================
// menu_award_results_screen.cpp
// Batch: batch_821
// Address range: 0x802da04c - 0x802daa60
// Date: 2026-09-11T06:35:00Z
//
// Award-unlock results screen: the screen that presents the notification feed
// built by menu_award_unlock_feed (batch_820). It shows the challenge/difficulty
// header (with team-color difficulty label), a rotating 3D preview of the
// unlocked entity, and the scroll/close animation drive, plus a second compact
// variant and a savegame-block comparison helper.
//
// Layout:
//   menu_results_layout_is_offset  0x802da04c  layout mode: centered vs offset
//   menu_results_header_render      0x802da064  difficulty header render cb (fmt+shadow)
//   menu_results_reset_preview      0x802da224  reset tracked preview entity thunk
//   menu_results_screen_build       0x802da244  results screen build (feed rows)
//   menu_results_preview_render      0x802da430  render tracked preview entity
//   menu_results_preview_tick        0x802da474  spawn/animate the preview model
//   menu_results_feed_advance        0x802da868  advance feed / rebuild or push
//   menu_results_close_tick          0x802da8c8  close-timer tick -> advance
//   menu_results_compact_build       0x802da92c  compact results screen build
//   menu_results_savegame_changed    0x802daa60  compare savegame player block
//
// NON-MATCHING: incoming paired-single / FP spill regs (in_f31/in_ps31_1,
// extraout_f1) are modelled as locals; the frame-actor head thunks
// (FUN_803826e4/FUN_803826f0) and the __save_gpr_28/__restore_gpr_28 prologue
// helpers are declared extern; CONCAT44 magic-bias int->float conversions and
// the int->float register shuffles for text placement are kept verbatim.
// debug_log_formatted / entity_spawn_by_type / mode_reset_tracked_entity are
// declared void or wrong-arity in the generated headers but their results are
// consumed here, so they are called through address-bound function pointers.
// ============================================================================

#include "common.h"
#include "menu.h"     // pulls generated_decls.h
#include "hud.h"
#include "gx.h"
#include "text.h"
#include "entity.h"

// ---- frame / prologue helper thunks ---------------------------------------
extern "C" int  FUN_803826e4(void);   // frame-actor head (element ptr)
extern "C" int  FUN_803826f0(void);   // frame-actor head (element ptr)
extern "C" void FUN_80382730(void);   // frame-actor tail (header render)
extern "C" void FUN_8038273c(void);   // frame-actor tail (compact build)
extern "C" unsigned long long __save_gpr_28(void);
extern "C" void __restore_gpr_28(void);

// ---- void/wrong-arity helpers via address-bound function pointers -----------
// debug_log_formatted @0x8012e878 returns the formatted-string pointer (PPC
// printf-like: accumulator in the FP arg, format string in a GPR arg-9 slot).
typedef int (*DebugLogFmtFn)(unsigned long long, unsigned long long, unsigned long long,
                             unsigned long long, unsigned long long, unsigned long long,
                             unsigned long long, unsigned long long, const char *fmt,
                             unsigned int, unsigned int, unsigned int, unsigned int,
                             unsigned int, unsigned int, unsigned int);
static const DebugLogFmtFn __mr_debug_log_formatted = (DebugLogFmtFn)0x8012e878;
// entity_spawn_by_type @0x80072d70 (13 args, returns entity ptr)
typedef int (*spawntype_fp)(int, double, double, double, int, int, int, int, int,
                            void *, void *, int, int);
static const spawntype_fp __mr_spawn_type = (spawntype_fp)0x80072d70;
// mode_reset_tracked_entity @0x802c7d78 returns the reset spawn context (r3)
typedef int (*reset_ctx_fp)(void);
static const reset_ctx_fp __mr_reset_ctx = (reset_ctx_fp)0x802c7d78;

// Named helpers resolve via generated_decls.h. difficulty_get_name_string is
// declared variadic there; call it directly.

// ---- raw FUN_ helper not yet in generated headers -------------------------
extern "C" int FUN_80387b68(int a, int b, int len);  // savegame block memcmp-like

// ---- forward declarations for intra-file callbacks used by address ---------
void menu_results_header_render(unsigned long long, unsigned long long, unsigned long long,
                                unsigned long long, unsigned long long, unsigned long long,
                                unsigned long long, unsigned long long, unsigned int, unsigned int,
                                float *, unsigned int, unsigned int, unsigned int, unsigned int,
                                unsigned int);
void menu_results_reset_preview(void);
void menu_results_feed_advance(void);

// ---- format string + label globals ----------------------------------------
extern "C" char s__s__s_8061104c[];  // "%s %s %s" style format string

// ---- raw scalar ROM globals -----------------------------------------------
extern "C" int  DAT_805cc608;   // active screen-kind selector
extern "C" int  DAT_805cc628;   // active challenge/objective index
extern "C" int  DAT_805cc61c;   // active player index (savegame)
extern "C" int  DAT_8060f948;   // player-count / split context
extern "C" int  DAT_80612b1c;   // feed advance cursor
extern "C" int  DAT_80612b34;   // tracked preview-entity pointer
extern "C" int  DAT_806109c0;   // frame delta (int, magic-bias converted)
extern "C" int  DAT_806109c4;   // preview render rect param
extern "C" int  DAT_806109c8;   // preview render rect param
extern "C" int  DAT_806119ec;   // savegame block stride index
extern "C" int  DAT_805ce7bc;   // feed: label id per entry (word stride)
extern "C" int  DAT_805ce7e4;   // feed: value/icon per entry (word stride)
extern "C" int  DAT_8051a918;   // savegame comparison snapshot base
extern "C" int *DAT_806119e0;   // layout center context (array of ptrs)

// ---- preview model transform block (DAT_80532a30 area) ---------------------
extern "C" float DAT_80532a30, DAT_80532a34, DAT_80532a38;
extern "C" float DAT_80532a3c, DAT_80532a40, DAT_80532a44, DAT_80532a48;
extern "C" float DAT_80532a4c, DAT_80532a50, DAT_80532a60, DAT_80532a64;
extern "C" float DAT_80532a6c, DAT_80532a70, DAT_80532a78, DAT_80532a7c;

// ---- model/material seed constants (copied into the spawn transform) --------
extern "C" int  DAT_803e08cc, DAT_803e08d0, DAT_803e08d4, DAT_803e08d8;
extern "C" int  DAT_803e08dc, DAT_803e08e0;

// ---- float ROM constants ---------------------------------------------------
extern "C" float FLOAT_80619eec, FLOAT_80619f84, FLOAT_8061a03c, FLOAT_80619ee8;
extern "C" float FLOAT_80619f74, FLOAT_80619f08, FLOAT_80619f6c, FLOAT_80619f5c;
extern "C" float FLOAT_80619ff4, FLOAT_806108e8, FLOAT_80619f38, FLOAT_80619fa8;
extern "C" float FLOAT_80619f30, FLOAT_80619f4c, FLOAT_8061a064, FLOAT_8061a084;
extern "C" float FLOAT_8061a0ec, FLOAT_8061a008, FLOAT_8061a134, FLOAT_8061a124;
extern "C" float FLOAT_8061a128, FLOAT_8061a12c, FLOAT_8061a130;
extern "C" float FLOAT_80612ad4;  // close-timer accumulator
extern "C" float FLOAT_80612b30;  // preview grow/shrink scale
extern "C" float FLOAT_80612b2c;  // preview aux scale
extern "C" double DOUBLE_80619f00; // int->float magic-bias constant

// ============================================================================
// 0x802da04c - layout mode predicate: 0 = centered, nonzero = offset (mode 4)
// ============================================================================
int menu_results_layout_is_offset(void)
{
    int t = -DAT_805cc608 + 4;
    return t - ((unsigned int)(t == 0) + -DAT_805cc608 + 3);
}

// ============================================================================
// 0x802da064 - difficulty header render callback (formatted + team-color shadow)
// ============================================================================
void menu_results_header_render(unsigned long long p1, unsigned long long p2,
                                unsigned long long p3, unsigned long long p4,
                                unsigned long long p5, unsigned long long p6,
                                unsigned long long p7, unsigned long long p8,
                                unsigned int p9, unsigned int p10, float *rect,
                                unsigned int p12, unsigned int p13, unsigned int p14,
                                unsigned int p15, unsigned int p16)
{
    int      elem;
    unsigned char *rec;
    int      diff;
    int      nameStr;
    int      titleStr;
    unsigned int textPtr;
    unsigned char *upper;
    unsigned int colB;
    unsigned int colC;
    float    charW;
    float    charH;

    (void)p1; (void)p9; (void)p10;

    elem = FUN_803826e4();
    rec  = (unsigned char *)stats_get_challenge_record(0, DAT_805cc628);
    if (*(int *)(rec + 0xc) < 4) {
        rec  = (unsigned char *)stats_get_challenge_record(0, DAT_805cc628);
        diff = *(int *)(rec + 0xc);
    } else {
        diff = 4;
    }

    if (DAT_8060f948 == 4) {
        nameStr  = difficulty_get_name_string(diff);
        titleStr = get_string_by_id(0x160e);
        textPtr  = __mr_debug_log_formatted(0, p2, p3, p4, p5, p6, p7, p8,
                                            s__s__s_8061104c, titleStr, nameStr,
                                            p12, p13, p14, p15, p16);
        upper = text_string_to_upper(textPtr);
    } else {
        titleStr = get_string_by_id(0x160e);
        nameStr  = difficulty_get_name_string(diff);
        textPtr  = __mr_debug_log_formatted(0, p2, p3, p4, p5, p6, p7, p8,
                                            s__s__s_8061104c, nameStr, titleStr,
                                            p12, p13, p14, p15, p16);
        upper = text_string_to_upper(textPtr);
    }

    gx_set_sub_material_id(1);
    text_set_font_table(7);
    charW = (float)text_get_char_width(upper);
    charH = text_get_char_height(upper);

    // x = magic-bias round of (eec * ((rect.right-rect.left) - charW) + rect.left)
    int px = (int)((double)FLOAT_80619eec *
        (double)(float)((double)(float)((double)rect[2] - (double)*rect) - (double)charW) + (double)*rect);
    int py = (int)((double)FLOAT_80619eec *
        (double)(float)((double)(float)((double)rect[3] - (double)rect[1]) - (double)charH) + (double)rect[1]);
    double dx = (double)(float)((double)(CONCAT44(0x43300000, px ^ 0x80000000)) - DOUBLE_80619f00);
    double dy = (double)(float)((double)(CONCAT44(0x43300000, py ^ 0x80000000)) - DOUBLE_80619f00);

    colC = color_fade_team_c((double)*(float *)(elem + 0xf0), diff);
    colB = color_fade_team_b((double)*(float *)(elem + 0xf0), diff);
    text_draw_char_shadowed(dx, dy, upper, colB, colC);
    FUN_80382730();
}

// ============================================================================
// 0x802da224 - reset the tracked preview entity (thunk)
// ============================================================================
void menu_results_reset_preview(void)
{
    mode_reset_tracked_entity();
}

// ============================================================================
// 0x802da244 - results screen build (header + feed rows + preview spacer)
// ============================================================================
void menu_results_screen_build(void)
{
    int  screen;
    int  total;
    int  titleId;
    int  titleStr;
    short *obj;
    int  i;

    screen = (int)__save_gpr_28();
    total  = menu_award_feed_total();
    titleId = (total != 0) ? 0x1610 : 0x160f;
    titleStr = get_string_by_id(titleId);
    hud_element_init_from_layout(0x100000, (void *)titleStr, (int)&menu_results_feed_advance, 0);
    *(int *)(screen + 0x214) = 1;
    hud_set_context_property(0x10c04);

    if (DAT_805cc608 == 4) {
        hud_add_padding_item(FLOAT_80619f84);
    } else {
        obj = (short *)worm_mode_get_object(DAT_805cc628);
        hud_set_margins(FLOAT_80619ee8, FLOAT_80619ee8, 1);
        get_string_by_id((int)*obj);
        hud_add_list_item();
        hud_add_spacer_item(FLOAT_8061a03c, 0, (int)&menu_results_header_render);
        if (total != 0) {
            hud_add_default_callback_item();
        }
    }

    i = 0;
    do {
        if (DAT_80612b1c + i < total) {
            get_string_by_id(*(int *)(&DAT_805ce7bc + (DAT_80612b1c + i) * 4));
            hud_add_list_item();
            get_string_by_id(*(int *)(&DAT_805ce7e4 + (DAT_80612b1c + i) * 4));
            hud_add_list_item();
            hud_add_padding_item(FLOAT_80619eec);
        }
        i = i + 1;
    } while (i < 3);

    if (DAT_805cc608 == 4) {
        hud_add_padding_item(FLOAT_80619f84);
    }
    int handlerStr = get_string_by_id(0x351);
    hud_screen_register_handler_global(0, handlerStr, 0, 0);

    if (menu_results_layout_is_offset() == 0) {
        hud_element_apply_center_xy(DAT_806119e0);
    } else {
        hud_element_set_pos_x((double)FLOAT_80619f74, (int)DAT_806119e0);
        hud_element_apply_content_y(DAT_806119e0);
    }
    mode_reset_tracked_entity();
    hud_element_set_user_data(screen, (int)&menu_results_reset_preview);
    __restore_gpr_28();
}

// ============================================================================
// 0x802da430 - render the tracked preview entity
// ============================================================================
void menu_results_preview_render(void)
{
    gx_set_sub_material_id(1);
    if (DAT_80612b34 != 0) {
        menu_preview_render_extra_entity(DAT_80612b34, 0, 0, DAT_806109c4, DAT_806109c8);
    }
}

// ============================================================================
// 0x802da474 - spawn + animate the unlocked-entity preview model
// ============================================================================
void menu_results_preview_tick(unsigned long long p1, double p2, double p3,
                               unsigned long long p4, unsigned long long p5,
                               unsigned long long p6, unsigned long long p7,
                               unsigned long long p8)
{
    int   screen;
    unsigned char *rec;
    int   diff;
    unsigned int typeId;
    int   closing;
    int   loadState;
    int   spawnParams[3];  // local_3c/38/34 material words
    float spawnFloats[3];  // local_48/44/40 transform words
    int   i;
    // paired-single FP spill inputs (non-matching, modelled as locals)
    double in_f31 = 0.0, in_ps31_1 = 0.0;
    (void)in_f31; (void)in_ps31_1;

    screen = (int)__save_gpr_28();
    rec = (unsigned char *)stats_get_challenge_record(0, DAT_805cc628);
    if (*(int *)(rec + 0xc) < 4) {
        rec  = (unsigned char *)stats_get_challenge_record(0, DAT_805cc628);
        diff = *(int *)(rec + 0xc);
    } else {
        diff = 4;
    }
    typeId  = entity_get_type_id(diff);
    closing = hud_element_is_closing(screen);

    if ((closing == 0) && (menu_results_layout_is_offset() != 0) &&
        ((DAT_80612b34 == 0) || (*(unsigned short *)(DAT_80612b34 + 0x94) != typeId))) {
        model_begin_load(0, (int)p2, (int)p3, (int)p4, (int)p5, (int)p6, (int)p7, (int)p8,
                         0, 0, typeId, 0, 0, 0, 0, 0);
        loadState = resource_get_load_state(0, 0);
        if (loadState != 0) {
            spawnParams[0] = DAT_803e08cc;
            spawnParams[1] = DAT_803e08d0;
            spawnParams[2] = DAT_803e08d4;
            spawnFloats[0] = *(float *)&DAT_803e08d8;
            spawnFloats[1] = *(float *)&DAT_803e08dc;
            spawnFloats[2] = *(float *)&DAT_803e08e0;
            int ctx = __mr_reset_ctx();
            DAT_80612b34 = __mr_spawn_type(ctx, p2, p3, p4, (int)p5, (int)p6, (int)p7, (int)p8,
                                           typeId, spawnParams, spawnFloats, 0, 0);
            FLOAT_80612b2c = FLOAT_80619ee8;
            *(int **)(DAT_80612b34 + 0x254) = (int *)&DAT_80532a30;
            *(unsigned int *)(DAT_80612b34 + 0x28) = *(unsigned int *)(DAT_80612b34 + 0x28) | 0x40000000;
            render_instance_set_material((double)FLOAT_80619f08, *(int **)(DAT_80612b34 + 4), 1);
            memset_wrapper((int)&DAT_80532a30, 0, 0x54);
            DAT_80532a60 = FLOAT_8061a124;
            DAT_80532a64 = FLOAT_8061a128;
            DAT_80532a6c = FLOAT_8061a12c;
            DAT_80532a70 = FLOAT_8061a130;
            DAT_80532a78 = FLOAT_80619f6c;
            DAT_80532a7c = FLOAT_80619f6c;
            DAT_80532a3c = FLOAT_80619ee8;
            DAT_80532a40 = FLOAT_80619ee8;
            DAT_80532a44 = FLOAT_80619ee8;
            if (diff == 3) {
                DAT_80532a48 = FLOAT_80619ee8;
                DAT_80532a4c = FLOAT_8061a084;
                DAT_80532a50 = FLOAT_8061a0ec;
            } else if (diff < 3) {
                if (diff == 1) {
                    DAT_80532a48 = FLOAT_80619ee8;
                    DAT_80532a4c = FLOAT_8061a064;
                    DAT_80532a50 = FLOAT_80619f08;
                } else if (0 < diff) {
                    DAT_80532a48 = FLOAT_80619f30;
                    DAT_80532a4c = FLOAT_8061a084;
                    DAT_80532a50 = FLOAT_80619ee8;
                }
            } else if (diff < 5) {
                DAT_80532a48 = FLOAT_8061a008;
                DAT_80532a4c = FLOAT_8061a134;
                DAT_80532a50 = FLOAT_80619ee8;
            }
            DAT_80532a30 = FLOAT_80619f4c * DAT_80532a48;
            DAT_80532a34 = FLOAT_80619f4c * DAT_80532a4c;
            DAT_80532a38 = FLOAT_80619f4c * DAT_80532a50;
        }
    }

    if (DAT_80612b34 != 0) {
        if (FLOAT_80619f08 < FLOAT_80612b30) {
            *(float *)(DAT_80612b34 + 0x6c) = FLOAT_80619f84 / FLOAT_80612b30;
        }
        closing = hud_element_is_closing(screen);
        double frameDelta = FLOAT_80619ee8 /
            (float)((double)(CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000)) - DOUBLE_80619f00);
        if (closing == 0) {
            FLOAT_80612b30 = FLOAT_806108e8 * (float)frameDelta + FLOAT_80612b30;
            if (FLOAT_80619ee8 < FLOAT_80612b30) {
                FLOAT_80612b30 = FLOAT_80619ee8;
            }
        } else {
            FLOAT_80612b30 = -(FLOAT_80619f38 * FLOAT_806108e8 * (float)frameDelta - FLOAT_80612b30);
            if (FLOAT_80612b30 <= FLOAT_80619f08) {
                mode_reset_tracked_entity();
            }
        }
        if (DAT_80612b34 != 0) {
            *(float *)(DAT_80612b34 + 0x70) = *(float *)(DAT_80612b34 + 0x70) + FLOAT_806108e8;
            if (FLOAT_80619fa8 <= *(float *)(DAT_80612b34 + 0x70)) {
                *(float *)(DAT_80612b34 + 0x70) = *(float *)(DAT_80612b34 + 0x70) - FLOAT_80619fa8;
            }
            if ((*(unsigned int *)(DAT_80612b34 + 0x28) & 0x100) == 0) {
                double s = (double)FLOAT_80619f08;
                if (s < (double)FLOAT_80612b30) {
                    s = math_sqrt((double)FLOAT_80612b30);
                }
                for (i = 0; i < *(int *)(*(int *)(*(int *)(DAT_80612b34 + 4) + 4) + 4); i = i + 1) {
                    mtx4_set_scale((float *)&DAT_80532a30, (float)s, (float)s, (float)s);
                }
            }
        }
    }
    __restore_gpr_28();
}

// ============================================================================
// 0x802da868 - advance the feed cursor; rebuild items or push next screen
// ============================================================================
void menu_results_feed_advance(void)
{
    int total;
    DAT_80612b1c = DAT_80612b1c + 3;
    total = menu_award_feed_total();
    if (DAT_80612b1c < total) {
        hud_screen_rebuild_items();
    } else {
        hud_screen_push();
    }
}

// ============================================================================
// 0x802da8c8 - close-timer tick: accumulate then advance when past threshold
// ============================================================================
void menu_results_close_tick(void)
{
    double frameDelta = FLOAT_80619ee8 /
        (float)((double)(CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000)) - DOUBLE_80619f00);
    FLOAT_80612ad4 = FLOAT_806108e8 * (float)frameDelta + FLOAT_80612ad4;
    if (FLOAT_80619f6c < FLOAT_80612ad4) {
        menu_results_feed_advance();
    }
}

// ============================================================================
// 0x802da92c - compact results screen build (feed rows, clipped)
// ============================================================================
void menu_results_compact_build(void)
{
    int screen;
    int total;
    int titleStr;
    int i;

    screen = FUN_803826f0();
    total  = menu_award_feed_total();
    FLOAT_80612ad4 = FLOAT_80619f08;
    titleStr = get_string_by_id(0x1610);
    hud_element_init_from_layout(0x100000, (void *)titleStr, (int)&menu_results_feed_advance, 0);
    *(int *)(screen + 0x214) = 1;
    hud_set_context_property(0x10c04);
    hud_add_padding_item(FLOAT_80619f84);

    i = 0;
    do {
        if (DAT_80612b1c + i < total) {
            get_string_by_id(*(int *)(&DAT_805ce7bc + (DAT_80612b1c + i) * 4));
            hud_add_list_item();
            get_string_by_id(*(int *)(&DAT_805ce7e4 + (DAT_80612b1c + i) * 4));
            hud_add_list_item();
            hud_add_padding_item(FLOAT_80619eec);
        }
        i = i + 1;
    } while (i < 3);

    hud_add_padding_item(FLOAT_80619f84);
    hud_element_apply_center_xy(DAT_806119e0);
    hud_element_set_clip_rect(FLOAT_80619f08, FLOAT_80619f5c, FLOAT_80619ff4, FLOAT_80619ff4, screen);
    FUN_8038273c();
}

// ============================================================================
// 0x802daa60 - compare the active savegame player block against the snapshot
// ============================================================================
bool menu_results_savegame_changed(void)
{
    int block;
    int diff;
    block = (int)savegame_get_player_block(DAT_805cc61c);
    diff  = FUN_80387b68(block, (int)(&DAT_8051a918 + DAT_806119ec * 0x1e0), 0x7c);
    return diff != 0;
}
