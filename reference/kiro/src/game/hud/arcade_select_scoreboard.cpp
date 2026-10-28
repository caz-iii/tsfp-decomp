// Batch: batch_858 | Module: arcade-select scoreboard / worm-stat leaderboard
// Address range: 0x802fcbbc - 0x802fe1fc
//
// The arcade-select "scoreboard" cluster. arcsel_draw_scoreboard_panel is the
// top-level panel: it lays out the KILLS/DEATHS/(mode-specific) column headers,
// draws the up/down selection arrows and per-player rows, and (in team modes)
// appends the team-score summary. arcsel_draw_scoreboard_row draws one player's
// name + kill/death pair; arcsel_draw_team_scores loops the 4 teams and prints
// each team's aggregate score. arcsel_scoreboard_x_pos returns the panel's
// horizontal origin from the current menu scroll/x offsets.
//
// The worm-stat (challenge) leaderboard: arcsel_accumulate_worm_stats walks every
// active player and folds their worm-stat value into the running accumulator;
// arcsel_rank_stats sorts the 7 computed stat entries descending into a ranking
// array; arcsel_draw_worm_leaderboard picks the top-5 (de-duplicated, ties broken
// by stat index) and renders each row via the formatted-text builder, with
// arcsel_format_time_stat formatting time-type stats as mm:ss.ss; the row text is
// emitted by arcsel_draw_worm_stat_rows. arcsel_clear_flag zeroes a scratch word.
//
// Named cross-module helpers (text_*, gx_*, sprite_*, hud_*, score_get_team,
// profile_get_slot, get_string_by_id, worm_stat_get, color_scale_lo,
// player_ai_is_story_mode, arcsel_wind_oscillate, arcsel_draw_separator,
// arcsel_menu_scroll_offset, arcsel_menu_x_offset, arcsel_mode_needs_confirm,
// arcsel_promptbar_write_color, arcsel_prompt_set_arrow_flags,
// arcsel_arrows_ud_render, leaf_92c0) resolve through the shared generated
// headers below; only raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols get per-file externs.

#include "common.h"
#include "hud.h"
#include "text.h"
#include "sprite.h"

// ---- raw symbols referenced by this translation unit -------------------------
extern "C" {
u64  __save_gpr_25(void);
void __restore_gpr_25(void);

// hud active-screen context lookups (each returns a register pair: hi=element ptr)
u64  FUN_803826b8(void);
u64  FUN_803826c0(void);
u64  FUN_803826c4(void);
int  FUN_803826e4(void);
int  FUN_803826e8(void);
// hud screen finalize variants
void FUN_80382704(void);
void FUN_8038270c(void);
void FUN_80382710(void);
void FUN_80382730(void);
void FUN_80382734(void);

u32  FUN_803825c0(double);          // color/float -> packed byte
u64  FUN_80387fb8(double, double, double, double, double, double, double, double,
                  int, void*, int, int, u32, u32, u32, u32); // formatted text builder
double FUN_8038b510(int*, int*);    // worm-stat name/label lookup

extern float  FLOAT_80611674;
extern float  FLOAT_80611a5c;
extern float  FLOAT_80611a64;
extern float  FLOAT_80611a68;
extern float  FLOAT_8061a418;
extern float  FLOAT_8061a41c;
extern float  FLOAT_8061a440;
extern float  FLOAT_8061a444;
extern float  FLOAT_8061a450;
extern float  FLOAT_8061a45c;
extern float  FLOAT_8061a480;
extern float  FLOAT_8061a488;
extern float  FLOAT_8061a490;
extern float  FLOAT_8061a494;
extern float  FLOAT_8061a4b0;
extern float  FLOAT_8061a504;
extern float  FLOAT_8061a50c;
extern float  FLOAT_8061a560;
extern float  FLOAT_8061a594;
extern float  FLOAT_8061a5a0;
extern float  FLOAT_8061a5a4;
extern float  FLOAT_8061a5a8;
extern float  FLOAT_8061a5ac;
extern float  FLOAT_8061a5b0;
extern float  FLOAT_8061a5b4;
extern double DOUBLE_8061a430;

extern int   DAT_806110ec;
extern int   DAT_806110f8;
extern int   DAT_80611138;
extern int   DAT_80611140;
extern int   DAT_806110f0;
extern int   DAT_80611748;   // active menu element ptr (x-offset source)
extern int*  DAT_80611d90;   // player table base (0xea0 / 0x3a8-word stride)
extern int*  DAT_80611d78;   // profile/slot table base (0x507-word stride)
extern int   DAT_80612610;   // human player count
extern int   DAT_806120fc;   // ai/bot player count
extern int   DAT_80612bac;   // currently-selected column index (-1 == none)
extern int   DAT_8060f948;   // current game-mode id
extern u8    DAT_805015be;   // active screen/mode id
extern u8    DAT_805015bd;   // secondary mode flag
extern char  DAT_805015e0;   // team-enabled flag table base
extern int   DAT_805014a0;   // per-column visible player-index table base
extern char  DAT_803f2dea;   // per-mode sprite-id table base (0x20 stride)
extern int   DAT_803f2c7c;   // per-team color table base (5-word stride)

extern int   s__d__02d__02d_804777b0;   // "%d:%02d.%02d" format string
}

// ---- forward declarations for this file's own (mutually-recursive) helpers ---
u64 arcsel_draw_scoreboard_row(double, double, double, double, double, double,
                               double, double, char*, float*, int, u32, u32, u32,
                               u32, u32);
u64 arcsel_draw_scoreboard_name_row(double, double, double, double, double, double,
                                    double, double, char*, void*, float*, int, u32,
                                    u32, u32, u32);
double arcsel_format_time_stat(double, double, double, double, double, double, double,
                               double, int, void*, void*, u32, u32, u32, u32, u32);
double arcsel_accumulate_worm_stats(void);
void arcsel_draw_worm_stat_rows(double, double, double, int, int, int, int, int, int, int);

// ============================================================================
// 0x802fcecc  arcsel_scoreboard_x_pos
// Horizontal origin of the scoreboard panel: negated element width minus the
// current menu (x + scroll) offset.
// ============================================================================
double arcsel_scoreboard_x_pos(void)
{
    double scroll = arcsel_menu_scroll_offset(0);
    double xoff   = arcsel_menu_x_offset(0);
    return (double)((float)((double)CONCAT44(0x43300000,
                    *(uint*)(DAT_80611748 + 0x20) ^ 0x80000000) - DOUBLE_8061a430)
                    - (float)(xoff + scroll));
}

// ============================================================================
// 0x802fd8ec  arcsel_clear_flag
// Zero a scratch flag word.
// ============================================================================
void arcsel_clear_flag(u32* pFlag)
{
    *pFlag = 0;
}

// ============================================================================
// 0x802fcbbc  arcsel_draw_scoreboard_name_row
// Draw one player's row: profile name (left) plus the kill/death numeric pair
// (right-aligned to two columns).
// ============================================================================
u64 arcsel_draw_scoreboard_name_row(double param_1, double yRow, double param_3,
                                    double param_4, double param_5, double param_6,
                                    double param_7, double param_8, char* pPlayer,
                                    void* pProfile, float* pCols, int fontId,
                                    u32 param_13, u32 colFg, u32 colBg, u32 param_16)
{
    char* pName;
    int   iVar4;
    u32   uVar5;
    u32   uVar6;
    double extraout_f1 = param_1;
    u64   uVar7;
    double dVar8;
    double dVar9;
    double dVar10;
    double dVar11;
    byte  abStack_88[112];

    u64 uVar12 = __save_gpr_25();
    pName = (char*)((u64)uVar12 >> 0x20);
    iVar4 = fontId;
    uVar5 = colFg;
    uVar6 = colBg;
    dVar9 = extraout_f1;
    dVar11 = extraout_f1;
    dVar8 = yRow;
    leaf_92c0(1);
    if (fontId < 0) {
        dVar10 = dVar11;
        FUN_80387fb8(dVar9, yRow, param_3, param_4, param_5, param_6, param_7, param_8,
                     (int)abStack_88, &DAT_806110ec, *(u32*)(pName + 0x48), iVar4,
                     param_13, uVar5, uVar6, param_16);
        dVar11 = dVar10;
        uVar7 = text_draw_char_shadowed((double)*pCols, dVar10, abStack_88, colFg, colBg);
    } else {
        dVar10 = (double)(float)dVar11;
        param_3 = (double)*pCols;
        hud_sprite_draw();
        uVar7 = 0;
    }
    leaf_92c0(0);
    FUN_80387fb8(uVar7, dVar10, param_3, param_4, param_5, param_6, param_7, param_8,
                 (int)abStack_88, &DAT_806110f8, (int)(pName + 0x20), iVar4,
                 param_13, uVar5, uVar6, param_16);
    text_apply_alpha_fade(dVar8, abStack_88, 0, 1);
    text_draw_char_shadowed((double)pCols[1], dVar11, abStack_88, colFg, colBg);
    dVar8 = (double)FLOAT_8061a41c;
    dVar9 = dVar8;
    uVar7 = text_set_color_rgb(dVar8, dVar8, dVar8);

    short sKills = *(short*)((char*)uVar12 + *pName * 2 + 2);
    short sDeaths = *(short*)(pName + *(char*)uVar12 * 2 + 2);
    leaf_92c0(1);
    FUN_80387fb8(uVar7, dVar8, dVar9, param_4, param_5, param_6, param_7, param_8,
                 (int)abStack_88, &DAT_806110ec, (int)sKills, iVar4, param_13,
                 uVar5, uVar6, param_16);
    dVar10 = text_get_char_width(abStack_88);
    dVar8 = dVar11;
    uVar12 = text_draw_char_shadowed((double)(float)((double)pCols[2] - dVar10),
                                     dVar11, abStack_88, colFg, colBg);
    FUN_80387fb8(uVar12, dVar11, dVar9, param_4, param_5, param_6, param_7, param_8,
                 (int)abStack_88, &DAT_806110ec, (int)sDeaths, iVar4, param_13,
                 uVar5, uVar6, param_16);
    dVar9 = text_get_char_width(abStack_88);
    text_draw_char_shadowed((double)(float)((double)pCols[3] - dVar9), dVar8,
                            abStack_88, colFg, colBg);
    leaf_92c0(0);
    __restore_gpr_25();
    return uVar12;
}

// ============================================================================
// 0x802fcda4  arcsel_draw_team_scores
// Loop the (up to 4) enabled teams, printing each team's aggregate score at the
// current row, advancing one line per enabled team.
// ============================================================================
void arcsel_draw_team_scores(double param_1, double param_2, double param_3,
                             double param_4, double param_5, double param_6,
                             double param_7, double param_8, u32 param_9, u32 param_10,
                             u32 colTeam, int drawEnabled, u32 param_13, u32 param_14,
                             u32 param_15, u32 param_16)
{
    int iVar2;
    u32 uVar3;
    double extraout_f1 = param_1;
    u64 uVar6;
    double dVar7;
    double dVar8;

    u64 uVar9 = FUN_803826e4();
    int iVar1 = (int)((u64)uVar9 >> 0x20);
    int iVar4 = drawEnabled;
    dVar7 = extraout_f1;
    text_set_font_table(3);
    int iVar5 = 0;
    dVar7 = (double)(float)(dVar7 + (double)FLOAT_80611a64);
    do {
        if ((&DAT_805015e0)[iVar5] == '\x01') {
            dVar8 = dVar7;
            if (drawEnabled != 0) {
                u32 local_44, local_48;
                arcsel_promptbar_write_color(iVar5, (int)&local_44, &local_48);
                iVar2 = get_string_by_id(iVar5 + 0x18a3);
                dVar8 = dVar7;
                uVar6 = text_draw_char_shadowed((double)*(float*)(iVar1 + 4), dVar7,
                                                iVar2, local_44, local_48);
                uVar3 = score_get_team(iVar5 + 1);
                byte abStack_40[40];
                FUN_80387fb8(uVar6, dVar7, param_3, param_4, param_5, param_6, param_7,
                             param_8, (int)abStack_40, &DAT_806110ec, uVar3, iVar4,
                             param_13, param_14, param_15, param_16);
                dVar7 = text_get_char_width(abStack_40);
                text_draw_char_shadowed((double)(float)((double)*(float*)(iVar1 + 0x10) - dVar7),
                                        dVar8, abStack_40, (int)uVar9, colTeam);
            }
            dVar7 = (double)(float)(dVar8 + (double)FLOAT_80611674);
        }
        iVar5 = iVar5 + 1;
    } while (iVar5 < 4);
    FUN_80382730();
}

// ============================================================================
// 0x802fdd44  arcsel_format_time_stat
// Format a worm-stat value: as mm:ss for normal values, or the "no score" string
// when zero. Emits via the formatted-text builder.
// ============================================================================
double arcsel_format_time_stat(double statValue, double param_2, double param_3,
                               double param_4, double param_5, double param_6,
                               double param_7, double param_8, int param_9,
                               void* param_10, void* param_11, u32 param_12,
                               u32 param_13, u32 param_14, u32 param_15, u32 param_16)
{
    uint uVar3 = (uint)statValue;
    double dVar5 = (double)FLOAT_8061a440;
    int iVar1 = (int)uVar3 / 0x3c;
    int iVar2 = (int)(dVar5 * (double)(float)(statValue -
                (double)(float)((double)CONCAT44(0x43300000, uVar3 ^ 0x80000000) - DOUBLE_8061a430)));
    if (99 < iVar1) {
        iVar1 = 99;
    }
    if (statValue == (double)FLOAT_8061a418) {
        double dVar4 = DOUBLE_8061a430;
        int sId = get_string_by_id(0xe38);
        FUN_80387fb8(statValue, dVar4, dVar5, param_4, param_5, param_6, param_7,
                     param_8, param_9, &DAT_806110f8, sId, param_12, iVar2, uVar3,
                     param_15, param_16);
    } else {
        FUN_80387fb8(statValue, DOUBLE_8061a430, dVar5, param_4, param_5, param_6,
                     param_7, param_8, param_9, &s__d__02d__02d_804777b0, iVar1,
                     (int)uVar3 % 0x3c, iVar2, uVar3, param_15, param_16);
    }
    return statValue;
}

// ============================================================================
// 0x802fd8f8  arcsel_draw_worm_stat_rows
// Draw the worm/challenge stat rows: an optional header separator block, then one
// row per stat slot (label + formatted value), colouring completed vs incomplete
// entries differently.
// ============================================================================
void arcsel_draw_worm_stat_rows(double param_1, double yTop, double param_3,
                                int pLabels, int pValueStrs, int pDoneFlags,
                                int pNames, int rowCount, int param_9, int drawEnabled)
{
    uint uVar1, uVar2, uVar3, uVar4;
    byte* pbVar5;
    int iVar6, iVar7, iVar8, iVar9;
    double extraout_f1 = param_1;
    double dVar10, dVar11, dVar12, dVar13, dVar14, dVar15, dVar16;

    u64 uVar17 = FUN_803826c4();
    dVar14 = extraout_f1;
    uVar1 = color_scale_lo(param_3, 0x7878787f);
    uVar2 = color_scale_lo(param_3, 0x1e1e1e7f);
    uVar3 = color_scale_lo(param_3, 0x7f7f7f7f);
    uVar4 = color_scale_lo(param_3, 0x5235a50);
    dVar10 = (double)FLOAT_80611a68;
    dVar15 = (double)(float)(dVar14 + dVar10);
    dVar16 = (double)(float)((double)FLOAT_8061a5ac * dVar10);
    dVar10 = (double)(float)((double)FLOAT_8061a5b0 * dVar10);
    text_set_font_table(3);
    if (drawEnabled != 0) {
        dVar12 = (double)(float)((double)FLOAT_8061a444 * dVar16 + dVar14);
        dVar13 = (double)(float)((double)FLOAT_8061a444 * param_3);
        arcsel_draw_separator(dVar14, (double)(float)(yTop - (double)FLOAT_8061a494),
                              dVar12, (double)FLOAT_8061a418, dVar13);
        dVar11 = (double)(float)(dVar14 + dVar16);
        arcsel_draw_separator(dVar12, (double)(float)(yTop - (double)FLOAT_8061a494),
                              dVar11, dVar13, (double)FLOAT_8061a418);
        arcsel_draw_separator(dVar14,
                              (double)(FLOAT_8061a494 + (float)(yTop + (double)FLOAT_80611674)),
                              dVar12, (double)FLOAT_8061a418, dVar13);
        arcsel_draw_separator(dVar12,
                              (double)(FLOAT_8061a494 + (float)(yTop + (double)FLOAT_80611674)),
                              dVar11, dVar13, (double)FLOAT_8061a418);
        pbVar5 = text_string_to_upper((uint)uVar17);
        text_apply_alpha_fade(dVar16, pbVar5, 0, 1);
        dVar11 = text_get_char_width(pbVar5);
        text_draw_char_shadowed(
            (double)(float)((double)FLOAT_8061a444 * (double)(float)(dVar16 - dVar11) + dVar14),
            yTop, pbVar5, uVar3, uVar4);
        dVar11 = (double)FLOAT_8061a41c;
        text_set_color_rgb(dVar11, dVar11, dVar11);
    }
    dVar12 = (double)(float)((double)FLOAT_8061a594 * (double)FLOAT_80611674 + yTop);
    dVar11 = arcsel_wind_oscillate(param_3);
    uVar3 = color_scale_lo(dVar11, 0x7f7f7f7f);
    dVar11 = arcsel_wind_oscillate(param_3);
    uVar4 = color_scale_lo(dVar11, 0x5235a50);
    text_set_font_table(0x12);
    if (drawEnabled != 0) {
        iVar6 = sprite_get_texture_id(1);
        gx_set_active_material(iVar6);
    }
    dVar11 = (double)(float)(dVar14 + dVar16);
    iVar9 = 0;
    iVar8 = 0;
    for (iVar6 = 0; iVar6 < rowCount; iVar6 = iVar6 + 1) {
        pbVar5 = (byte*)((int)((u64)uVar17 >> 0x20) + iVar8);
        if ((pbVar5 == (byte*)0x0) ||
            (dVar13 = text_get_char_width(pbVar5), dVar13 <= (double)FLOAT_8061a418)) {
            if (drawEnabled != 0) {
                iVar7 = get_string_by_id(0xe38);
                pbVar5 = (byte*)get_string_by_id(0xe38);
                dVar13 = text_get_char_width(pbVar5);
                text_draw_char_shadowed((double)(float)(dVar11 - dVar13), dVar12,
                                        iVar7, uVar1, uVar2);
            }
        } else if (drawEnabled != 0) {
            if (param_9 != 0) {
                dVar15 = text_get_char_width(pbVar5);
                dVar15 = (double)(float)((double)FLOAT_8061a444 *
                         (double)(float)(dVar16 - dVar15) + dVar14);
            }
            if (*(int*)(pLabels + iVar9) == 0) {
                text_apply_alpha_fade(dVar10, pbVar5, 0, 1);
                text_draw_char_shadowed(dVar15, dVar12, pbVar5, uVar1, uVar2);
                dVar13 = (double)FLOAT_8061a41c;
                text_set_color_rgb(dVar13, dVar13, dVar13);
                leaf_92c0(1);
                dVar13 = text_get_char_width((byte*)(pNames + iVar8));
                text_draw_char_shadowed((double)(float)(dVar11 - dVar13), dVar12,
                                        (byte*)(pNames + iVar8), uVar1, uVar2);
                leaf_92c0(0);
            } else {
                text_apply_alpha_fade(dVar10, pbVar5, 0, 1);
                text_draw_char_shadowed(dVar15, dVar12, pbVar5, uVar3, uVar4);
                dVar13 = (double)FLOAT_8061a41c;
                text_set_color_rgb(dVar13, dVar13, dVar13);
                leaf_92c0(1);
                dVar13 = text_get_char_width((byte*)(pNames + iVar8));
                text_draw_char_shadowed((double)(float)(dVar11 - dVar13), dVar12,
                                        (byte*)(pNames + iVar8), uVar3, uVar4);
                leaf_92c0(0);
            }
        }
        iVar9 = iVar9 + 4;
        iVar8 = iVar8 + 0x80;
        dVar12 = (double)(float)(dVar12 + (double)FLOAT_80611674);
    }
    FUN_80382710();
    (void)pValueStrs;
    (void)dVar15;
}

// ============================================================================
// 0x802fe144  arcsel_accumulate_worm_stats
// Walk every active (non-story-mode-hidden) player, folding each player's
// worm-stat into the accumulator maintained by worm_stat_get.
// ============================================================================
double arcsel_accumulate_worm_stats(void)
{
    int iVar2;
    int iVar1 = FUN_803826e4();
    int iVar5 = 0;
    for (int iVar4 = 0; iVar4 < DAT_80612610 + DAT_806120fc; iVar4 = iVar4 + 1) {
        int iVar3 = (int)DAT_80611d90 + iVar5;
        if ((iVar3 != 0) &&
            (((DAT_805015be != '\x13' || (*(int*)(iVar3 + 8) < DAT_80612610)) ||
              (iVar2 = player_ai_is_story_mode((int)*(short*)(iVar3 + 0x48)), iVar2 == 0)))) {
            worm_stat_get(iVar1, *(int*)(iVar3 + 8));
        }
        iVar5 = iVar5 + 0xea0;
    }
    FUN_80382730();
    return (double)FLOAT_8061a418;
}

// ============================================================================
// 0x802fe1fc  arcsel_rank_stats
// Compute a per-slot weighted stat for the 7 tracked slots, then selection-sort
// them descending, writing the ranked slot indices (-1 for empty) into the output
// array returned by FUN_803826e8.
// ============================================================================
void arcsel_rank_stats(void)
{
    int iVar2;
    uint uVar3;
    double dVar11;
    float local_58[8];
    u32 local_38, local_30;
    uint uStack_34, uStack_2c;

    int iVar1 = FUN_803826e8();
    dVar11 = (double)FLOAT_8061a418;
    uint uVar8 = 0;
    int iVar6 = 0;
    int iVar7 = 0;
    do {
        if ((iVar6 == 6) && (iVar2 = arcsel_mode_needs_confirm(), iVar2 != 0)) {
            *(float*)((int)local_58 + iVar7) = FLOAT_8061a418;
        } else {
            uVar3 = (uint)arcsel_accumulate_worm_stats();
            uStack_2c = 7U - iVar6 ^ 0x80000000;
            local_30 = 0x43300000;
            uStack_34 = uVar3 ^ 0x80000000;
            local_38 = 0x43300000;
            *(float*)((int)local_58 + iVar7) =
                (FLOAT_8061a45c * (float)((double)CONCAT44(0x43300000, uStack_2c) - DOUBLE_8061a430) +
                 FLOAT_8061a41c) *
                (float)((double)CONCAT44(0x43300000, uVar3 ^ 0x80000000) - DOUBLE_8061a430);
        }
        iVar6 = iVar6 + 1;
        iVar7 = iVar7 + 4;
    } while (iVar6 < 7);

    double dVar10 = (double)FLOAT_8061a418;
    iVar7 = 0;
    iVar6 = 0;
    do {
        int iBest = -1;
        int iVar5 = 0;
        int iVar4 = 0;
        int iVar9 = 7;
        do {
            if ((dVar11 <= (double)*(float*)((int)local_58 + iVar4)) &&
                ((uVar8 & 1 << iVar5) == 0)) {
                iBest = iVar5;
                dVar11 = (double)*(float*)((int)local_58 + iVar4);
            }
            iVar5 = iVar5 + 1;
            iVar4 = iVar4 + 4;
            iVar9 = iVar9 + -1;
        } while (iVar9 != 0);
        if (dVar11 <= dVar10) {
            *(u32*)(iVar1 + iVar6) = 0xffffffff;
        } else {
            *(int*)(iVar1 + iVar6) = iBest;
        }
        if (-1 < iBest) {
            uVar8 = uVar8 | 1 << iBest;
        }
        iVar7 = iVar7 + 1;
        dVar11 = (double)FLOAT_8061a418;
        iVar6 = iVar6 + 4;
    } while (iVar7 < 7);
    FUN_80382734();
    (void)local_38;
    (void)local_30;
    (void)uStack_34;
    (void)uStack_2c;
}

// ============================================================================
// 0x802fde10  arcsel_draw_worm_leaderboard
// Build a top-5 leaderboard from the active players' worm stats: for each of the
// 5 slots, pick the highest remaining stat (de-duplicated, ties resolved by slot),
// build its label/value strings via the mode-appropriate formatter, then hand the
// assembled rows to arcsel_draw_worm_stat_rows.
// ============================================================================
void arcsel_draw_worm_leaderboard(double param_1, double param_2, double param_3,
                                  double param_4, double param_5, double param_6,
                                  double param_7, double param_8, u32 param_9,
                                  u32 param_10, int strId, int drawEnabled,
                                  u32 param_13, u32 param_14, u32 param_15, u32 param_16)
{
    int iVar2, iVar3;
    float* pfVar5;
    float* pfVar6;
    u32 uVar7;
    int iVar8, iVar9;
    double dVar14;
    u32 local_5d8[5];
    float local_5c4[5];
    float local_5b0[5];
    byte auStack_59c[640];
    int local_31c[161];
    long long local_98;

    u64 uVar20 = FUN_803826c0();
    int iVar4 = (int)uVar20;
    pfVar6 = local_5b0;
    pfVar5 = local_5c4;
    int iVar1 = 0;
    uVar7 = 0xffffffff;
    int iVar13 = 5;
    do {
        *(u32*)((int)pfVar6 + iVar1) = 0xffffffff;
        *(float*)((int)pfVar5 + iVar1) = FLOAT_8061a418;
        iVar1 = iVar1 + 4;
        iVar13 = iVar13 + -1;
    } while (iVar13 != 0);

    double dVar19 = (double)FLOAT_8061a5b4;
    int iVar10 = -1;
    int iVar12 = 0;
    iVar13 = 0;
    iVar1 = 0;
    dVar14 = param_1;
    double dVar15 = param_1;
    double dVar16 = param_2;
    double dVar17 = param_3;
    double dVar18 = param_4;
    do {
        iVar8 = 0;
        for (int iVar11 = 0; iVar11 < DAT_80612610 + DAT_806120fc; iVar11 = iVar11 + 1) {
            iVar9 = (int)DAT_80611d90 + iVar8;
            if ((iVar9 != 0) &&
                (((DAT_805015be != '\x13' || (*(int*)(iVar9 + 8) < DAT_80612610)) ||
                  (iVar2 = player_ai_is_story_mode((int)*(short*)(iVar9 + 0x48)), iVar2 == 0)))) {
                int playerIdx = *(int*)(iVar9 + 8);
                dVar14 = worm_stat_get((int)((u64)uVar20 >> 0x20), playerIdx);
                if ((dVar19 <= dVar14) || (dVar14 <= (double)*(float*)((int)local_5c4 + iVar1))) {
                    if ((dVar14 == dVar19) &&
                        ((double)*(float*)((int)local_5c4 + iVar1) < dVar14)) {
                        int iScan = 0;
                        int iRem = iVar12;
                        int fresh = 1;
                        if (0 < iVar12) {
                            do {
                                if (iVar11 == *(int*)((int)local_5b0 + iScan)) {
                                    fresh = 0;
                                    break;
                                }
                                iScan = iScan + 4;
                                iRem = iRem + -1;
                            } while (iRem != 0);
                        }
                        if (fresh != 0) {
                            *(int*)((int)local_5b0 + iVar1) = iVar11;
                            iVar10 = *(int*)(iVar9 + 8);
                            *(float*)((int)local_5c4 + iVar1) = (float)dVar14;
                        }
                    }
                } else {
                    *(int*)((int)local_5b0 + iVar1) = iVar11;
                    iVar10 = *(int*)(iVar9 + 8);
                    *(float*)((int)local_5c4 + iVar1) = (float)dVar14;
                }
            }
            iVar8 = iVar8 + 0xea0;
        }
        dVar19 = (double)*(float*)((int)local_5c4 + iVar1);
        if (*(int*)((int)local_5b0 + iVar1) < 0) {
            *(byte*)((int)local_31c + iVar13) = 0;
        } else {
            dVar14 = FUN_8038b510((int*)((int)local_31c + iVar13),
                                  (int*)DAT_80611d90[*(int*)((int)local_5b0 + iVar1) * 0x3a8 + 0x1c]);
        }
        if (iVar4 == 0) {
            int scoreI = (int)*(float*)((int)local_5c4 + iVar1);
            local_98 = (long long)scoreI;
            dVar14 = arcsel_format_time_stat(dVar14, param_2, param_3, param_4, param_5,
                                             param_6, param_7, param_8,
                                             (int)(auStack_59c + iVar13), &DAT_806110ec,
                                             (void*)scoreI, uVar7, param_13, param_14,
                                             param_15, param_16);
        } else if (iVar4 == 1) {
            dVar14 = FUN_80387fb8((double)(float)(dVar17 * (double)*(float*)((int)local_5c4 + iVar1)),
                                  param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                  (int)(auStack_59c + iVar13), &DAT_80611138, iVar10, uVar7,
                                  param_13, param_14, param_15, param_16);
        } else if (iVar4 == 3) {
            dVar14 = arcsel_format_time_stat((double)*(float*)((int)local_5c4 + iVar1),
                                             param_2, param_3, param_4, param_5, param_6,
                                             param_7, param_8, (int)(auStack_59c + iVar13),
                                             (void*)0, (void*)0, uVar7, param_13, param_14,
                                             param_15, param_16);
        } else {
            dVar14 = FUN_80387fb8((double)(float)(dVar17 * (double)*(float*)((int)local_5c4 + iVar1)),
                                  param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                  (int)(auStack_59c + iVar13), &DAT_80611140, iVar10, uVar7,
                                  param_13, param_14, param_15, param_16);
        }
        *(u32*)((int)local_5d8 + iVar1) = 0;
        if (((DAT_80611d90[iVar10 * 0x3a8] != 0) &&
             (*(int*)((int)DAT_80611d90[iVar10 * 0x3a8] + 0x150) != 0)) &&
            (-1 < (int)DAT_80611d78[iVar10 * 0x507 + 1])) {
            *(u32*)((int)local_5d8 + iVar1) = 1;
        }
        iVar12 = iVar12 + 1;
        iVar1 = iVar1 + 4;
        iVar13 = iVar13 + 0x80;
        if (4 < iVar12) {
            int sId = get_string_by_id(strId);
            arcsel_draw_worm_stat_rows(dVar15, dVar16, dVar18, (int)local_31c, sId,
                                       (int)local_5d8, (int)auStack_59c, 5, 0, drawEnabled);
            FUN_8038270c();
            (void)local_98;
            (void)pfVar5;
            (void)pfVar6;
            return;
        }
    } while (true);
}

// ============================================================================
// 0x802fcf38  arcsel_draw_scoreboard_panel
// Top-level scoreboard panel: measures and lays out the KILLS / DEATHS /
// (mode-specific) column headers, draws the selection cursor arrows and the
// per-player rows (delegating team-mode summaries and selected-column detail to
// the helpers above), and frames the whole block with separators.
// ============================================================================
void arcsel_draw_scoreboard_panel(u32 param_1, u32 param_2, float* pLayout,
                                  int drawEnabled, u32 param_5, u32 param_6,
                                  u32 param_7, u32 param_8)
{
    float fVar1, fVar4;
    bool bVar3;
    int iVar9, iVar16, iVar17;
    uint uVar6, uVar7, uVar8, uVar10, uVar15, uVar18;
    byte* pbVar11;
    char* pcVar13;
    byte* puVar14;
    double dVar20, dVar21, dVar22, dVar23, dVar24, dVar25, dVar26;
    double in_f5 = 0.0;
    double in_f6 = 0.0, in_f7 = 0.0, in_f8 = 0.0;
    int local_b8[3];
    float local_ac, local_a8, local_a4, local_a0, local_9c;

    u64 uVar28 = FUN_803826b8();
    int iVar5 = (int)((u64)uVar28 >> 0x20);
    double dVar27 = (double)pLayout[1];
    dVar20 = arcsel_scoreboard_x_pos();
    dVar24 = (double)*(float*)(iVar5 + 0xf0);
    bVar3 = false;
    uVar6 = FUN_803825c0((double)(float)((double)FLOAT_8061a480 * dVar24));
    uVar18 = uVar6 & 0xff | 0x7f7f7f00;
    uVar6 = FUN_803825c0((double)(float)((double)FLOAT_8061a50c * dVar24));
    uVar6 = uVar6 & 0xff;
    uVar7 = color_scale_lo(dVar24, 0x7f7f7f7f);
    uVar8 = color_scale_lo((double)*(float*)(iVar5 + 0xf0), 0x5235a50);
    iVar9 = arcsel_mode_needs_confirm();
    bool bVar19 = iVar9 == 0;
    if (((int)uVar28 != 0) &&
        (dVar20 <= (double)(*(float*)((int)uVar28 + 0x74) * FLOAT_80611a64))) {
        bVar3 = true;
    }
    if (!bVar3) {
        dVar27 = (double)(float)(dVar27 + (double)FLOAT_8061a488);
    }
    local_ac = *pLayout;
    local_a8 = FLOAT_8061a488 * FLOAT_80611a68 + local_ac;
    fVar1 = pLayout[2];
    local_9c = fVar1 - FLOAT_8061a494 * FLOAT_80611a68;
    if (DAT_80612bac < 0) {
        local_a0 = -(FLOAT_8061a490 * FLOAT_80611a68 - fVar1);
        local_b8[0] = 0x17e0;
        local_b8[1] = 0x6cd;
        local_b8[2] = 0x98a;
        fVar4 = FLOAT_8061a5a0;
    } else {
        local_b8[0] = 0x99e;
        if (DAT_8060f948 == 3) {
            local_b8[0] = 0x6c9;
        }
        local_b8[1] = 0x189c;
        if (DAT_8060f948 == 3) {
            local_b8[1] = 0x6ca;
        }
        local_b8[2] = 0;
        local_a0 = local_9c;
        fVar4 = FLOAT_8061a504;
    }
    local_a4 = -(fVar4 * FLOAT_80611a68 - fVar1);
    text_set_font_table(9);
    dVar26 = -(double)(FLOAT_8061a4b0 * FLOAT_80611a68 - (local_a0 - local_a4));
    uVar10 = get_string_by_id(local_b8[0]);
    pbVar11 = text_string_to_upper(uVar10);
    dVar20 = text_get_char_width(pbVar11);
    iVar9 = local_b8[1];
    uVar10 = get_string_by_id(local_b8[1]);
    pbVar11 = text_string_to_upper(uVar10);
    dVar24 = text_get_char_width(pbVar11);
    iVar17 = local_b8[2];
    uVar10 = get_string_by_id(local_b8[2]);
    pbVar11 = text_string_to_upper(uVar10);
    dVar21 = text_get_char_width(pbVar11);
    dVar23 = (double)FLOAT_8061a5a0;
    dVar22 = (double)FLOAT_8061a450;
    dVar25 = (double)(float)(dVar22 * (double)(-(float)(dVar23 * (double)FLOAT_80611a68 -
             (double)pLayout[2]) - local_a8));
    if (dVar24 <= dVar20) {
        if (dVar20 <= dVar21) {
            iVar16 = 2;
        } else {
            iVar16 = 0;
        }
    } else if (dVar24 <= dVar21) {
        iVar16 = 2;
    } else {
        iVar16 = 1;
    }
    if (drawEnabled != 0) {
        dVar20 = (double)(float)(dVar27 + (double)FLOAT_80611a64);
        text_set_font_table(2);
        if ((DAT_80612bac == -1) || ((int)(&DAT_805014a0)[DAT_80612bac] < 0)) {
            dVar24 = -(double)(FLOAT_8061a494 * FLOAT_80611a68 -
                     ((local_a4 - (local_a0 - local_a4)) - local_a8));
            pbVar11 = (byte*)get_string_by_id(0x17e4);
            text_apply_alpha_fade(dVar24, pbVar11, 0, 1);
            int sId = get_string_by_id(0x17e4);
            text_draw_char_shadowed((double)local_a8, dVar20, sId, uVar7, uVar8);
        } else {
            dVar24 = (double)(local_a4 - (FLOAT_80611a68 + (float)(dVar26 + (double)local_a8)));
            puVar14 = (byte*)profile_get_slot((&DAT_805014a0)[DAT_80612bac]);
            text_apply_alpha_fade(dVar24, puVar14 + 0x20, 0, 1);
            puVar14 = (byte*)profile_get_slot((&DAT_805014a0)[DAT_80612bac]);
            text_draw_char_shadowed((double)local_a8, dVar20, puVar14 + 0x20, uVar7, uVar8);
        }
        dVar24 = (double)FLOAT_8061a41c;
        text_set_color_rgb(dVar24, dVar24, dVar24);
        dVar23 = (double)(FLOAT_80611a68 * FLOAT_8061a5a4 * FLOAT_80611674 * FLOAT_80611a5c);
        arcsel_prompt_set_arrow_flags(-0x7fa2faf8, 0, 0);
        in_f5 = (double)*(float*)(iVar5 + 0xf0);
        arcsel_arrows_ud_render(
            (double)(float)((double)local_ac + dVar23),
            (double)(float)(dVar20 - (double)(float)((double)FLOAT_8061a444 * dVar23)),
            (double)(float)(dVar20 + (double)(float)((double)FLOAT_8061a444 * dVar23)),
            dVar23, in_f5);
        text_set_font_table(9);
        uVar10 = get_string_by_id(local_b8[iVar16]);
        pbVar11 = text_string_to_upper(uVar10);
        text_apply_alpha_fade(dVar26, pbVar11, 0, 1);
        uVar10 = get_string_by_id(local_b8[0]);
        pbVar11 = text_string_to_upper(uVar10);
        dVar20 = text_get_char_width(pbVar11);
        text_draw_char_shadowed((double)(float)((double)local_a4 - dVar20), dVar27,
                                pbVar11, uVar7, uVar8);
        uVar10 = get_string_by_id(iVar9);
        pbVar11 = text_string_to_upper(uVar10);
        dVar20 = text_get_char_width(pbVar11);
        text_draw_char_shadowed((double)(float)((double)local_a0 - dVar20), dVar27,
                                pbVar11, uVar7, uVar8);
        if (DAT_80612bac == -1) {
            uVar10 = get_string_by_id(iVar17);
            pbVar11 = text_string_to_upper(uVar10);
            dVar20 = text_get_char_width(pbVar11);
            text_draw_char_shadowed((double)(float)((double)local_9c - dVar20), dVar27,
                                    pbVar11, uVar7, uVar8);
        }
        dVar22 = (double)FLOAT_8061a41c;
        text_set_color_rgb(dVar22, dVar22, dVar22);
    }
    fVar1 = FLOAT_80611a64;
    if (bVar3) {
        fVar1 = FLOAT_8061a444 * FLOAT_80611a64;
    }
    dVar20 = (double)((float)(dVar27 + (double)FLOAT_80611674) + fVar1);
    if (drawEnabled != 0) {
        bool bVar2 = false;
        if ((!bVar19) && (DAT_805015bd != '\x03')) {
            bVar2 = true;
        }
        fVar1 = FLOAT_8061a418;
        if (bVar2) {
            dVar24 = (double)pLayout[2];
            arcsel_draw_team_scores((double)pLayout[2], (double)*(float*)(iVar5 + 0xf0),
                                    dVar22, dVar23, in_f5, in_f6, in_f7, in_f8,
                                    (u32)(uintptr_t)&local_ac, uVar18, uVar6, 0,
                                    param_5, param_6, param_7, param_8);
            fVar1 = (float)(dVar24 - (double)pLayout[2]);
        }
        dVar24 = (double)fVar1;
        arcsel_draw_separator((double)*pLayout, dVar20, (double)local_a4,
                              (double)FLOAT_8061a418,
                              (double)(FLOAT_8061a450 * *(float*)(iVar5 + 0xf0)));
        dVar22 = (double)pLayout[2];
        in_f5 = (double)FLOAT_8061a418;
        arcsel_draw_separator((double)local_a4, dVar20, dVar22,
                              (double)(FLOAT_8061a450 * *(float*)(iVar5 + 0xf0)), in_f5);
        dVar23 = (double)FLOAT_8061a560;
        if (dVar23 < (double)(pLayout[3] - (float)(dVar24 + dVar20))) {
            dVar22 = (double)(float)((double)local_9c - dVar23);
            FUN_803825c0((double)(FLOAT_8061a5a8 * *(float*)(iVar5 + 0xf0)));
            sprite_get_texture_id((int)*(short*)(&DAT_803f2dea + (uint)DAT_805015be * 0x20));
            hud_sprite_draw_bg();
        }
    }
    if (!bVar3) {
        dVar20 = (double)(float)(dVar20 + (double)FLOAT_80611a64);
    }
    dVar24 = arcsel_wind_oscillate((double)*(float*)(iVar5 + 0xf0));
    uVar7 = color_scale_lo(dVar24, 0x7f7f7f7f);
    dVar24 = arcsel_wind_oscillate((double)*(float*)(iVar5 + 0xf0));
    uVar8 = color_scale_lo(dVar24, 0x5235a50);
    text_set_font_table(0x12);
    iVar17 = 0;
    for (iVar9 = 0; iVar9 < DAT_80612610 + DAT_806120fc; iVar9 = iVar9 + 1) {
        if (((-1 < *(int*)((int)&DAT_805014a0 + iVar17)) &&
             (iVar16 = (int)DAT_80611d90 + *(int*)((int)&DAT_805014a0 + iVar17) * 0xea0, iVar16 != 0)) &&
            ((DAT_805015be != 0x13 ||
              ((*(int*)(iVar16 + 8) < DAT_80612610 ||
                (iVar16 = player_ai_is_story_mode((int)*(short*)(iVar16 + 0x48)), iVar16 == 0)))))) {
            if (drawEnabled != 0) {
                pcVar13 = (char*)profile_get_slot(*(int*)((int)&DAT_805014a0 + iVar17));
                if (((*pcVar13 < DAT_80612610) &&
                     (iVar16 = (int)DAT_80611d78 + *pcVar13 * 0x141c, -1 < *(int*)(iVar16 + 4))) &&
                    ((*(uint*)(iVar16 + 0xc) & 0x40000) != 0)) {
                    bVar3 = true;
                } else {
                    bVar3 = false;
                }
                if (DAT_80612bac == -1) {
                    if (bVar3) {
                        dVar24 = arcsel_wind_oscillate((double)*(float*)(iVar5 + 0xf0));
                        param_5 = uVar7;
                        param_6 = uVar8;
                    } else {
                        dVar24 = (double)*(float*)(iVar5 + 0xf0);
                        param_5 = uVar18;
                        param_6 = uVar6;
                    }
                    if (bVar19) {
                        arcsel_draw_scoreboard_row(dVar20, dVar25, dVar22, dVar23, in_f5,
                                                   in_f6, in_f7, in_f8, pcVar13,
                                                   &local_ac, -1, 0, param_5, param_6,
                                                   param_7, param_8);
                    } else {
                        uVar10 = color_scale_lo(dVar24, (&DAT_803f2c7c)[pcVar13[1] * 5]);
                        iVar16 = sprite_get_texture_id(
                            (int)*(short*)(&DAT_803f2dea + (uint)DAT_805015be * 0x20));
                        arcsel_draw_scoreboard_row(dVar20, dVar25, dVar22, dVar23, in_f5,
                                                   in_f6, in_f7, in_f8, pcVar13,
                                                   &local_ac, iVar16, uVar10, param_5,
                                                   param_6, param_7, param_8);
                    }
                } else {
                    if (DAT_80612bac == iVar9) {
                        dVar24 = arcsel_wind_oscillate((double)*(float*)(iVar5 + 0xf0));
                        uVar10 = uVar8;
                        uVar15 = uVar7;
                    } else {
                        dVar24 = (double)*(float*)(iVar5 + 0xf0);
                        uVar10 = uVar6;
                        uVar15 = uVar18;
                    }
                    if (-1 < (int)(&DAT_805014a0)[DAT_80612bac]) {
                        if (bVar19) {
                            puVar14 = (byte*)profile_get_slot((&DAT_805014a0)[DAT_80612bac]);
                            param_5 = 0;
                            arcsel_draw_scoreboard_name_row(dVar20, dVar25, dVar22, dVar23,
                                                            in_f5, in_f6, in_f7, in_f8,
                                                            pcVar13, puVar14, &local_ac,
                                                            -1, 0, uVar15, uVar10, param_8);
                            param_6 = uVar15;
                            param_7 = uVar10;
                        } else {
                            param_5 = color_scale_lo(dVar24, (&DAT_803f2c7c)[pcVar13[1] * 5]);
                            iVar16 = sprite_get_texture_id(
                                (int)*(short*)(&DAT_803f2dea + (uint)DAT_805015be * 0x20));
                            puVar14 = (byte*)profile_get_slot((&DAT_805014a0)[DAT_80612bac]);
                            arcsel_draw_scoreboard_name_row(dVar20, dVar25, dVar22, dVar23,
                                                            in_f5, in_f6, in_f7, in_f8,
                                                            pcVar13, puVar14, &local_ac,
                                                            iVar16, param_5, uVar15,
                                                            uVar10, param_8);
                            param_6 = uVar15;
                            param_7 = uVar10;
                        }
                    }
                }
            }
            dVar20 = (double)(float)(dVar20 + (double)FLOAT_80611674);
        }
        iVar17 = iVar17 + 4;
    }
    if ((!bVar19) && (DAT_805015bd != '\x03')) {
        if (drawEnabled != 0) {
            arcsel_draw_separator((double)*pLayout, dVar20, (double)local_a4,
                                  (double)FLOAT_8061a418,
                                  (double)(FLOAT_8061a450 * *(float*)(iVar5 + 0xf0)));
            dVar23 = (double)(FLOAT_8061a450 * *(float*)(iVar5 + 0xf0));
            dVar22 = (double)pLayout[2];
            in_f5 = (double)FLOAT_8061a418;
            arcsel_draw_separator((double)local_a4, dVar20, dVar22, dVar23, in_f5);
        }
        arcsel_draw_team_scores(dVar20, (double)*(float*)(iVar5 + 0xf0), dVar22, dVar23,
                                in_f5, in_f6, in_f7, in_f8, (u32)(uintptr_t)&local_ac,
                                uVar18, uVar6, drawEnabled, param_5, param_6, param_7,
                                param_8);
    }
    FUN_80382704();
    (void)uVar15;
    (void)param_1;
    (void)param_2;
}
