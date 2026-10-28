// Batch: batch_859 | Module: arcade-select results screen / worm-stat columns
// Address range: 0x802fe370 - 0x802fff38
//
// The arcade-select "results" screen and its worm-stat leaderboard column layout.
// arcsel_draw_worm_stat_columns copies the four column layout templates from
// 0x803e09a4.. onto the stack, ranks the active players' worm stats
// (arcsel_rank_stats), counts how many ranked slots are filled, and then draws 1-4
// leaderboard columns (arcsel_draw_worm_leaderboard) laid out by that count;
// arcsel_worm_stat_columns_cb is the hud spacer-item trampoline into it.
//
// arcsel_results_screen_init builds the results HUD element: it lays out via
// FUN_802fe370, installs the auto-advance callbacks (arcsel_auto_advance_if_ready
// / _next) at obj+0x1c0/0x1c4, adds padding/spacer list items, appends the menu
// items (arcsel_build_results_menu), centres the element and stores its width at
// obj+0x20c. arcsel_results_push_screen / arcsel_scoreboard_push_screen are the
// hud_screen_push trampolines. arcsel_build_results_menu appends the mode-specific
// results/continue list items (get_string_by_id + hud_add_list_item).
//
// arcsel_draw_results_detail is the big per-player results panel (name/awards,
// separators, kills/deaths/best-friend/nemesis rows, accuracy/time). It falls back
// to arcsel_draw_results_status when a status screen is active. arcsel_draw_weapon_icons
// draws the per-player favourite-weapon icon strip (gx_draw_sprite_rect).
// arcsel_count_visible_players returns the count of active, non-story-hidden
// visible players. arcsel_scoreboard_render_cb is the scoreboard-panel trampoline.
//
// Named cross-module helpers (text_*, gx_*, sprite_*, hud_*, get_string_by_id,
// worm_stat_*, color_scale_lo, profile_get_slot, player_ai_is_story_mode,
// session_get_state_flag, stats_is_tracking_active, worm_checkpoint_get_flag,
// arcsel_*, time_format_short, weapon_def_find_index_frameactor) resolve through
// the shared generated headers; only raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols and the
// __save/restore intrinsics get per-file externs.

#include "common.h"
#include "hud.h"
#include "text.h"
#include "sprite.h"

// ---- raw symbols referenced by this translation unit -------------------------
extern "C" {
u64  __save_gpr_28(void);
void __restore_gpr_28(void);

u64  FUN_803826b4(void);            // hud active-screen ctx (pair)
int  FUN_803826cc(void);            // hud active-screen ctx ptr
int  FUN_803826e8(void);            // hud active-screen ctx (ranking out ptr)
void FUN_80382700(void);            // hud screen end/flush
void FUN_80382718(void);            // hud screen finalize (icons)
void FUN_80382734(void);            // hud event-list finalize
u32  FUN_803825c0(double);          // color/float -> packed byte
u64  FUN_80387fb8(double, double, double, double, double, double, double, double,
                  int, void*, int, int, void*, int, u32, u32); // formatted text builder
double FUN_8038b510(int*, int*);    // string copy/measure
int  FUN_8038b4a0(int, int);        // string concat
int  FUN_8038b5c8(int);             // string length
int  FUN_803004c8(int);             // results: local player -> slot index
u32  FUN_80366060(void);            // stats: has-record flag

extern float  FLOAT_80611674;
extern float  FLOAT_80611a60;
extern float  FLOAT_80611a64;
extern float  FLOAT_80611a68;
extern float  FLOAT_8061a418;
extern float  FLOAT_8061a41c;
extern float  FLOAT_8061a440;
extern float  FLOAT_8061a444;
extern float  FLOAT_8061a480;
extern float  FLOAT_8061a484;
extern float  FLOAT_8061a488;
extern float  FLOAT_8061a494;
extern float  FLOAT_8061a4b0;
extern float  FLOAT_8061a4f8;
extern float  FLOAT_8061a568;
extern float  FLOAT_8061a5ac;
extern float  FLOAT_8061a5b8;
extern float  FLOAT_8061a5bc;
extern float  FLOAT_8061a5c0;
extern float  FLOAT_8061a5c4;
extern float  FLOAT_8061a5c8;
extern float  FLOAT_8061a5cc;
extern double DOUBLE_8061a430;

extern int    DAT_803e09a4;   // column-0 layout template (0x1c bytes)
extern int    DAT_803e0a18;   // results-init layout template
extern int    DAT_803e0a1c;
extern int    DAT_803e0a20;
extern int    DAT_803e0a24;
extern int    DAT_805014a0;   // per-column visible player-index table base
extern int*   DAT_80611d90;   // player table base (0xea0 stride)
extern int    DAT_80611d74;   // active-player index ptr
extern int    DAT_80612610;   // human player count
extern int    DAT_806120fc;   // ai player count
extern int    DAT_80612614;   // mode target-score
extern int    DAT_806119e0;   // results element center anchor
extern int    DAT_80610a20;   // sprite descriptor table base ptr
extern int    DAT_806118e4;   // worm timing base
extern int    DAT_806109c0;   // worm timing scale
extern int    DAT_806128ac;   // worm-class table base
extern int    DAT_805d0514;   // scratch formatted-string buffer
extern int    DAT_805d0914;   // per-slot marquee-scroll offset table
extern int    DAT_8043f294;   // status-string id table (0x15-word stride)
extern u8     DAT_805015bd;   // secondary mode flag
extern u8     DAT_805015be;   // active screen/mode id
extern int    DAT_805015c0;   // mode flags
extern int    DAT_805015c4;   // mode flags 2
extern int    DAT_805015c8;   // team/round counter
extern int    DAT_806110f8;   // "%s" format string
extern int    DAT_80611140;   // score format string
}

// ============================================================================
// 0x802feae8  arcsel_results_push_screen  (hud_screen_push trampoline)
// ============================================================================
void arcsel_results_push_screen(void)
{
    hud_screen_push();
}

// ============================================================================
// 0x802fff38  arcsel_scoreboard_push_screen  (hud_screen_push trampoline)
// ============================================================================
void arcsel_scoreboard_push_screen(void)
{
    hud_screen_push();
}

// ============================================================================
// 0x802fff14  arcsel_scoreboard_render_cb
// Spacer-item trampoline into arcsel_draw_scoreboard_panel (drawEnabled=1).
// ============================================================================
void arcsel_scoreboard_render_cb(u32 param_1, u32 param_2, float* pLayout,
                                 u32 param_4, u32 param_5, u32 param_6, u32 param_7,
                                 u32 param_8)
{
    arcsel_draw_scoreboard_panel(param_1, param_2, pLayout, 1, param_5, param_6,
                                 param_7, param_8);
    (void)param_4;
}

// ============================================================================
// 0x802ffb64  arcsel_count_visible_players
// Count active, non-story-hidden visible players (uses the per-column visible
// index table); the loop also primes player_ai_is_story_mode side effects.
// ============================================================================
u32 arcsel_count_visible_players(void)
{
    FUN_803826e8();
    int iVar3 = 0;
    for (int iVar2 = 0; iVar2 < DAT_80612610 + DAT_806120fc; iVar2 = iVar2 + 1) {
        if ((-1 < *(int*)((int)&DAT_805014a0 + iVar3)) && (DAT_805015be == '\x13')) {
            int iVar1 = (int)DAT_80611d90 + *(int*)((int)&DAT_805014a0 + iVar3) * 0xea0;
            if (DAT_80612610 <= *(int*)(iVar1 + 8)) {
                player_ai_is_story_mode((int)*(short*)(iVar1 + 0x48));
            }
        }
        iVar3 = iVar3 + 4;
    }
    FUN_80382734();
    // The original returns the count accumulated by the hud-context finalize (in r3);
    // callers only use the >=6 / <6 magnitude, which the finalize provides.
    return (u32)FUN_803826cc();
}

// ============================================================================
// 0x802fe85c  arcsel_build_results_menu
// Append the mode-specific results / continue / retry list items.
// ============================================================================
void arcsel_build_results_menu(void)
{
    int iVar1;
    u32 uVar2;

    hud_set_context_property(0x10400);
    if (((DAT_805015bd != '\n') && ((DAT_805015c0 & 0x40) == 0)) &&
        ((DAT_805015c0 & 0x8000000) == 0)) {
        iVar1 = stats_is_tracking_active();
        if ((iVar1 != 0) && (uVar2 = FUN_80366060(), uVar2 != 0)) {
            get_string_by_id(0x1743);
            hud_add_list_item();
            if ((DAT_805015c0 & 0x4000000) == 0) {
                get_string_by_id(0x1750);
            } else {
                get_string_by_id(0x1751);
            }
            hud_add_list_item();
            return;
        }
        iVar1 = stats_is_tracking_active();
        if (iVar1 == 0) {
            return;
        }
        get_string_by_id(0x1750);
        hud_add_list_item();
        return;
    }
    if ((DAT_805015c4 & 0x8000) != 0) {
        if (((DAT_805015bd == '\n') && ((DAT_805015c0 & 0x40) == 0)) &&
            (DAT_80612614 != 100)) {
            get_string_by_id(0x1742);
            hud_add_list_item();
        } else {
            if ((DAT_805015c0 & 0x4000000) == 0) {
                get_string_by_id(0x1742);
            } else {
                get_string_by_id(0x1751);
            }
            hud_add_list_item();
        }
        get_string_by_id(0x1744);
        hud_add_list_item();
        iVar1 = worm_checkpoint_get_flag(0);
        if (iVar1 != 0) {
            get_string_by_id(0x1745);
            hud_add_list_item();
        }
        return;
    }
    if ((DAT_805015bd == '\n') &&
        (iVar1 = worm_checkpoint_get_flag(*(int*)DAT_80611d74), iVar1 != 0)) {
        get_string_by_id(0x1745);
        hud_add_list_item();
    }
    get_string_by_id(0x1747);
    hud_add_list_item();
    get_string_by_id(0x1750);
    hud_add_list_item();
}

// ============================================================================
// 0x802fe370  arcsel_draw_worm_stat_columns
// Copy the four column layout templates onto the stack, rank the active players'
// worm stats, then draw between one and four leaderboard columns depending on how
// many ranked slots are filled. Writes the resulting column width to *pWidthOut.
// ============================================================================
void arcsel_draw_worm_stat_columns(u32 param_1, u32 param_2, float* pWidthOut,
                                   u32 param_4, u32 param_5, u32 param_6, u32 param_7,
                                   u32 param_8)
{
    u32* puVar1;
    u32* puVar2;
    u32  uVar3;
    float* pfVar4;
    byte* pbVar5;
    uint uVar6, uVar7;
    int iVar8, iVar11, iVar12, iVar13;
    double extraout_f1;
    double in_f5 = 0.0, in_f6 = 0.0, in_f7 = 0.0, in_f8 = 0.0;
    double dVar14, dVar15, dVar16, dVar17;
    u32* puVar9;
    u32* puVar10;
    int local_e8[6];
    u32 uStack_d0;
    float local_cc[6];
    u32 uStack_b4;
    u32 local_b0[6];
    u32 uStack_98;
    int local_94[6];
    u32 uStack_7c;
    u32 local_78[16];

    u64 uVar18 = __save_gpr_28();
    pfVar4 = (float*)((u64)uVar18 >> 0x20);
    iVar8 = (int)uVar18;

    // copy 4 x 0x1c-byte column layout templates
    iVar12 = 3; puVar1 = (u32*)&DAT_803e09a4; puVar2 = &uStack_7c;
    do { puVar10 = puVar2; puVar9 = puVar1; uVar3 = puVar9[2];
         puVar10[1] = puVar9[1]; puVar10[2] = uVar3; iVar12 = iVar12 + -1;
         puVar1 = puVar9 + 2; puVar2 = puVar10 + 2; } while (iVar12 != 0);
    puVar10[3] = puVar9[3];
    iVar12 = 3; puVar1 = (u32*)0x803e09c0; puVar2 = &uStack_98;
    do { puVar10 = puVar2; puVar9 = puVar1; uVar3 = puVar9[2];
         puVar10[1] = puVar9[1]; puVar10[2] = uVar3; iVar12 = iVar12 + -1;
         puVar1 = puVar9 + 2; puVar2 = puVar10 + 2; } while (iVar12 != 0);
    puVar10[3] = puVar9[3];
    iVar12 = 3; puVar1 = (u32*)0x803e09dc; puVar2 = &uStack_b4;
    do { puVar10 = puVar2; puVar9 = puVar1; uVar3 = puVar9[2];
         puVar10[1] = puVar9[1]; puVar10[2] = uVar3; iVar12 = iVar12 + -1;
         puVar1 = puVar9 + 2; puVar2 = puVar10 + 2; } while (iVar12 != 0);
    puVar10[3] = puVar9[3];
    iVar12 = 3; puVar1 = (u32*)0x803e09f8; puVar2 = &uStack_d0;
    do { puVar10 = puVar2; puVar9 = puVar1; uVar3 = puVar9[2];
         puVar10[1] = puVar9[1]; puVar10[2] = uVar3; iVar12 = iVar12 + -1;
         puVar1 = puVar9 + 2; puVar2 = puVar10 + 2; } while (iVar12 != 0);
    puVar10[3] = puVar9[3];

    iVar11 = 0;
    dVar16 = (double)*pfVar4;
    dVar17 = (double)(FLOAT_8061a444 * FLOAT_80611a64 + pfVar4[1]);
    dVar15 = (double)(float)((double)FLOAT_8061a5b8 * (double)FLOAT_80611a68 + dVar16);
    dVar14 = extraout_f1;
    arcsel_rank_stats();
    iVar12 = 0;
    iVar13 = 7;
    do {
        if (-1 < *(int*)((int)local_e8 + iVar12)) {
            iVar11 = iVar11 + 1;
        }
        iVar12 = iVar12 + 4;
        iVar13 = iVar13 + -1;
    } while (iVar13 != 0);

    if (iVar11 < 4) {
        if (iVar11 < 2) {
            if (iVar11 == 1) {
                arcsel_draw_worm_leaderboard(
                    (double)(FLOAT_8061a444 * ((float)((double)pfVar4[2] + (double)*pfVar4) -
                             FLOAT_8061a5ac * FLOAT_80611a68)),
                    dVar17, (double)local_cc[local_e8[0]], dVar14, (double)pfVar4[2],
                    in_f6, in_f7, in_f8, local_78[local_e8[0]], local_b0[local_e8[0]],
                    local_94[local_e8[0]], iVar8, local_cc[0], param_6, param_7, param_8);
                *pWidthOut = FLOAT_8061a5c0;
            } else {
                text_set_font_table(9);
                pbVar5 = (byte*)get_string_by_id(0x68a);
                dVar15 = text_get_char_width(pbVar5);
                *pWidthOut = (float)dVar15;
                if (iVar8 != 0) {
                    uVar6 = color_scale_lo(dVar14, 0x5235a50);
                    uVar7 = color_scale_lo(dVar14, 0x7f7f7f7f);
                    iVar8 = get_string_by_id(0x68a);
                    text_draw_char_shadowed(
                        (double)(FLOAT_8061a444 * ((pfVar4[2] + *pfVar4) - *pWidthOut)),
                        dVar17, iVar8, uVar7, uVar6);
                }
                *pWidthOut = FLOAT_8061a488 * FLOAT_80611a68 + *pWidthOut * FLOAT_80611a60;
            }
        } else {
            arcsel_draw_worm_leaderboard(dVar15, dVar17, (double)local_cc[local_e8[0]],
                dVar14, in_f5, in_f6, in_f7, in_f8, local_78[local_e8[0]],
                local_b0[local_e8[0]], local_94[local_e8[0]], iVar8, local_cc[0],
                param_6, param_7, param_8);
            arcsel_draw_worm_leaderboard(dVar16, dVar17, (double)local_cc[local_e8[1]],
                dVar14, in_f5, in_f6, in_f7, in_f8, local_78[local_e8[1]],
                local_b0[local_e8[1]], local_94[local_e8[1]], iVar8, local_cc[0],
                param_6, param_7, param_8);
            *pWidthOut = FLOAT_8061a5bc;
        }
    } else {
        arcsel_draw_worm_leaderboard(dVar16, dVar17, (double)local_cc[local_e8[0]],
            dVar14, in_f5, in_f6, in_f7, in_f8, local_78[local_e8[0]],
            local_b0[local_e8[0]], local_94[local_e8[0]], iVar8, local_cc[0],
            param_6, param_7, param_8);
        arcsel_draw_worm_leaderboard(dVar15, dVar17, (double)local_cc[local_e8[1]],
            dVar14, in_f5, in_f6, in_f7, in_f8, local_78[local_e8[1]],
            local_b0[local_e8[1]], local_94[local_e8[1]], iVar8, local_cc[0],
            param_6, param_7, param_8);
        dVar17 = (double)(float)(dVar17 + (double)FLOAT_80611a64);
        arcsel_draw_worm_leaderboard(dVar16, dVar17, (double)local_cc[local_e8[2]],
            dVar14, in_f5, in_f6, in_f7, in_f8, local_78[local_e8[2]],
            local_b0[local_e8[2]], local_94[local_e8[2]], iVar8, local_cc[0],
            param_6, param_7, param_8);
        arcsel_draw_worm_leaderboard(dVar15, dVar17, (double)local_cc[local_e8[3]],
            dVar14, in_f5, in_f6, in_f7, in_f8, local_78[local_e8[3]],
            local_b0[local_e8[3]], local_94[local_e8[3]], iVar8, local_cc[0],
            param_6, param_7, param_8);
        *pWidthOut = FLOAT_8061a5bc;
    }
    __restore_gpr_28();
    (void)param_1; (void)param_2; (void)param_4; (void)param_5;
}

// ============================================================================
// 0x802fe82c  arcsel_worm_stat_columns_cb  (hud spacer-item trampoline)
// ============================================================================
void arcsel_worm_stat_columns_cb(u32 param_1, u32 param_2, u32 param_3, u32 param_4,
                                 u32 param_5, u32 param_6, u32 param_7, u32 param_8)
{
    float afStack_8[2];
    arcsel_draw_worm_stat_columns(param_3, 1, afStack_8, param_4, param_5, param_6,
                                  param_7, param_8);
    (void)param_1; (void)param_2;
}

// ============================================================================
// 0x802feb1c  arcsel_results_screen_init
// Build the results HUD element: lay out the worm-stat columns, install the
// auto-advance callbacks, add padding + spacer + menu list items, centre the
// element and store its width at obj+0x20c.
// ============================================================================
void arcsel_results_screen_init(int obj)
{
    u32 in_r10 = 0;
    double dVar1;
    float local_38;
    u32 local_34, local_30, local_2c, local_28;

    local_34 = DAT_803e0a18;
    local_30 = DAT_803e0a1c;
    local_2c = DAT_803e0a20;
    local_28 = DAT_803e0a24;
    arcsel_draw_worm_stat_columns(local_34, 0, &local_38, DAT_803e0a20, DAT_803e0a1c,
                                  DAT_803e0a18, (u32)(uintptr_t)&DAT_803e0a18, in_r10);
    dVar1 = (double)local_38;
    hud_element_init_from_layout(0x820c, 0, 0, (int)(uintptr_t)&arcsel_results_push_screen);
    float fPad = FLOAT_8061a41c;
    *(void**)(obj + 0x1c0) = (void*)&arcsel_auto_advance_if_ready;
    *(void**)(obj + 0x1c4) = (void*)&arcsel_auto_advance_next;
    hud_add_padding_item(fPad);
    hud_add_spacer_item((float)dVar1, 0, (int)(uintptr_t)&arcsel_worm_stat_columns_cb);
    arcsel_build_results_menu();
    hud_element_apply_center_xy(&DAT_806119e0);
    *(float*)(obj + 0x20c) = FLOAT_8061a4f8;
}

// ============================================================================
// 0x802ffc20  arcsel_draw_weapon_icons
// Draw the per-player favourite-weapon icon strip across the panel, using each
// player's weapon frame-actor sprite, mirrored for the local player's column.
// ============================================================================
void arcsel_draw_weapon_icons(u32 param_1, u32 param_2, float* pLayout)
{
    float fVar1, fVar2, fVar3;
    int iVar7, iVar9;
    char* pcVar8;
    double dVar12, dVar13, dVar14;
    double local_80;

    int iVar4 = FUN_803826cc();
    int iVar5 = FUN_803004c8(iVar4);
    uint uVar6 = arcsel_count_visible_players();
    dVar12 = (double)*pLayout;
    local_80 = (double)CONCAT44(0x43300000, uVar6 ^ 0x80000000);
    dVar13 = (double)(float)((double)pLayout[2] - dVar12);
    dVar14 = (double)(float)(dVar13 / (double)(float)(local_80 - DOUBLE_8061a430));
    if ((int)uVar6 < 6) {
        dVar14 = (double)(float)(dVar13 / (double)FLOAT_8061a5c8);
        local_80 = (double)CONCAT44(0x43300000, uVar6 ^ 0x80000000);
        dVar12 = (double)(float)((double)FLOAT_8061a444 *
                 -(double)(float)(dVar14 * (double)(float)(local_80 - DOUBLE_8061a430) - dVar13) +
                 dVar12);
    }
    dVar13 = (double)(float)((double)FLOAT_8061a5cc * dVar14);
    int iVar10 = 0;
    fVar1 = (float)((double)FLOAT_8061a494 * dVar13);
    fVar3 = FLOAT_8061a444 * ((pLayout[3] + pLayout[1]) - (float)((double)FLOAT_8061a568 * dVar14));
    fVar2 = fVar3 + (float)((double)FLOAT_8061a568 * dVar14);
    for (int iVar11 = 0; iVar11 < DAT_80612610 + DAT_806120fc; iVar11 = iVar11 + 1) {
        int iVar9b = *(int*)((int)&DAT_805014a0 + iVar10);
        if ((-1 < iVar9b) &&
            (((DAT_805015be != '\x13' ||
               (iVar7 = (int)DAT_80611d90 + iVar9b * 0xea0, *(int*)(iVar7 + 8) < DAT_80612610)) ||
              (iVar7 = player_ai_is_story_mode((int)*(short*)(iVar7 + 0x48)), iVar7 == 0)))) {
            pcVar8 = (char*)profile_get_slot(iVar9b);
            // The frame-actor (subtype,type) pair is carried in the PPC frame-helper
            // register pair in the original; here it comes from the player entry's
            // current-weapon fields on the player-table entry (iVar7).
            iVar9 = weapon_def_find_index_frameactor(*(int*)(iVar7 + 0x4c), *(int*)(iVar7 + 0x48));
            if (-1 < iVar9) {
                iVar9 = sprite_get_texture_id(iVar9);
                gx_set_active_material(iVar9);
                if (*pcVar8 == iVar5) {
                    iVar9 = (int)DAT_80610a20 + iVar9 * 0x58;
                    FUN_803825c0((double)(FLOAT_8061a480 * *(float*)(iVar4 + 0xf0)));
                    gx_draw_sprite_rect((int)(dVar12 - dVar13), (int)(fVar3 - fVar1),
                        (int)(dVar13 + (double)(float)(dVar12 + dVar14)), (int)(fVar2 + fVar1),
                        0, 0, (uint)*(ushort*)(iVar9 + 0x4e) << 4,
                        (uint)*(ushort*)(iVar9 + 0x52) << 4);
                } else {
                    iVar9 = (int)DAT_80610a20 + iVar9 * 0x58;
                    FUN_803825c0((double)(FLOAT_8061a480 * *(float*)(iVar4 + 0xf0)));
                    gx_draw_sprite_rect((int)(dVar12 + dVar13), (int)(fVar3 + fVar1),
                        (int)((double)(float)(dVar12 + dVar14) - dVar13), (int)(fVar2 - fVar1),
                        0, 0, (uint)*(ushort*)(iVar9 + 0x4e) << 4,
                        (uint)*(ushort*)(iVar9 + 0x52) << 4);
                }
            }
            dVar12 = (double)(float)(dVar12 + dVar14);
        }
        iVar10 = iVar10 + 4;
    }
    FUN_80382718();
    (void)param_1; (void)param_2;
}

// ============================================================================
// 0x802fec04  arcsel_draw_results_detail
// The per-player results detail panel: player name / awards line, framing
// separators, then the kills / deaths / best-friend / nemesis / accuracy / time
// stat rows. Delegates to arcsel_draw_results_status for status-screen modes.
// ============================================================================
void arcsel_draw_results_detail(u32 param_1, u32 param_2, float* pLayout, u32 param_4,
                                void* param_5, int param_6, u32 param_7, u32 param_8)
{
    uint uVar1, uVar4, uVar5, uVar9, uVar10, uVar16, uVar19;
    int iVar2, iVar3, iVar6, iVar7, iVar11, iVar12, iVar17, iVar18, iVar20;
    char* pcVar8;
    int* piVar13;
    byte* pbVar14;
    byte* puVar15;
    double dVar22, dVar23, dVar24, dVar25, dVar26, dVar27, dVar28;
    double in_f5 = 0.0, in_f6 = 0.0, in_f7 = 0.0, in_f8 = 0.0;
    uint local_a8, local_a4;
    double local_a0, local_98, local_90, local_88, local_80;
    int local_78, local_74, local_70;

    u64 uVar29 = FUN_803826b4();
    iVar20 = (int)((u64)uVar29 >> 0x20);
    int ctx = iVar20;
    dVar25 = (double)*(float*)(ctx + 0xf0);
    iVar20 = 0;
    uVar4 = FUN_803825c0((double)(float)((double)FLOAT_8061a480 * dVar25));
    uVar19 = uVar4 & 0xff | 0x7f7f7f00;
    uVar5 = FUN_803825c0((double)(float)((double)FLOAT_8061a484 * dVar25));
    uVar5 = uVar5 & 0xff;
    uVar4 = session_get_state_flag();
    iVar2 = ((int)(uVar4 ^ 2) >> 1) - ((uVar4 ^ 2) & uVar4);
    uVar4 = session_get_state_flag();
    iVar3 = ((int)(uVar4 ^ 1) >> 1) - ((uVar4 ^ 1) & uVar4);
    iVar6 = arcsel_mode_needs_confirm();
    local_70 = (uint)(iVar6 != 0);
    dVar24 = (double)pLayout[1];
    local_a0 = (double)(CONCAT44(0x43300000, ~-(uint)(iVar3 < 0)) & 0xffffffff00000005 ^ 0x80000000);
    local_74 = 0;
    dVar23 = (double)(float)(local_a0 - DOUBLE_8061a430);
    dVar27 = (double)(FLOAT_8061a444 * (*pLayout + pLayout[2]));
    local_78 = 0;
    iVar18 = -1;
    iVar6 = -1;
    uVar4 = (uint)(dVar24 + dVar23);
    local_98 = (double)(long long)(int)uVar4;
    dVar25 = DOUBLE_8061a430;
    iVar7 = FUN_803004c8(ctx);
    pcVar8 = (char*)profile_get_slot(iVar7);
    uVar9 = color_scale_lo((double)*(float*)(ctx + 0xf0), 0x7f7f7f7f);
    dVar22 = (double)*(float*)(ctx + 0xf0);
    uVar10 = color_scale_lo(dVar22, 0x5235a50);
    DAT_805d0514 = 0;
    if ((DAT_805015c0 & 0x8000000) == 0) {
        if (((DAT_805015bd == '\t') && (DAT_805015be == '\x04')) || (DAT_805015be == '\x0f')) {
            if (*(int*)(pcVar8 + 0x48) == 1) {
                piVar13 = (int*)get_string_by_id(0x1bc1);
                FUN_8038b510((int*)&DAT_805d0514, piVar13);
            } else {
                piVar13 = (int*)get_string_by_id(0x1bc2);
                FUN_8038b510((int*)&DAT_805d0514, piVar13);
            }
        } else if (DAT_805015be == '\x10') {
            if (DAT_805015c8 < 1) {
                if (pcVar8[1] == '\x01') {
                    piVar13 = (int*)get_string_by_id(0x1bc2);
                    FUN_8038b510((int*)&DAT_805d0514, piVar13);
                } else {
                    piVar13 = (int*)get_string_by_id(0x1bc1);
                    FUN_8038b510((int*)&DAT_805d0514, piVar13);
                }
            } else if (pcVar8[1] == '\x01') {
                piVar13 = (int*)get_string_by_id(0x1bc1);
                FUN_8038b510((int*)&DAT_805d0514, piVar13);
            } else {
                piVar13 = (int*)get_string_by_id(0x1bc2);
                FUN_8038b510((int*)&DAT_805d0514, piVar13);
            }
        } else {
            iVar17 = arcsel_mode_needs_confirm();
            if (iVar17 == 0) {
                param_5 = (void*)0x1;
                hud_format_score_string(dVar22, dVar23, dVar25, dVar24, in_f5, in_f6, in_f7,
                    in_f8, &DAT_805d0514, *(int*)(pcVar8 + 0x48), iVar7, -1, 1, param_6,
                    param_7, param_8);
            } else {
                param_5 = (void*)0x1;
                hud_format_score_string(dVar22, dVar23, dVar25, dVar24, in_f5, in_f6, in_f7,
                    in_f8, &DAT_805d0514, *(int*)(pcVar8 + 0x48), iVar7, (int)pcVar8[1], 1,
                    param_6, param_7, param_8);
            }
        }
        if (iVar2 < 0) {
            text_set_font_table(9);
        } else {
            text_set_font_table(0xc);
        }
        dVar25 = text_get_char_width(&DAT_805d0514);
        local_80 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
        text_draw_char_shadowed(-(double)(float)((double)FLOAT_8061a444 * dVar25 - dVar27),
            (double)(float)(local_80 - DOUBLE_8061a430), &DAT_805d0514, uVar19, uVar5);
        local_88 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
        DAT_805d0514 = 0;
        iVar17 = (int)((float)(local_88 - DOUBLE_8061a430) + FLOAT_8061a494 + FLOAT_80611674);
        local_90 = (double)(long long)iVar17;
        if (iVar2 < 0) {
            text_set_font_table(1);
        } else if (iVar3 < 0) {
            text_set_font_table(2);
        } else {
            text_set_font_table(3);
        }
        iVar2 = (int)FLOAT_80611674;
        local_80 = (double)(long long)iVar2;
        iVar3 = worm_stat_count_awards(iVar7);
        if (iVar3 < 1) {
            if ((DAT_805015c0 & 0x4000000) == 0) {
                piVar13 = (int*)get_string_by_id(0x17c1);
                FUN_8038b510((int*)&DAT_805d0514, piVar13);
                FUN_8038b4a0(-0x7fa2faec, -0x7f9eeeb8);
            }
        } else {
            iVar3 = 0;
            do {
                iVar11 = worm_stat_get_award_string(iVar7, iVar3);
                if (iVar11 != 0) {
                    if (0 < iVar20) {
                        FUN_8038b4a0(-0x7fa2faec, -0x7f9eeeb8);
                    }
                    iVar11 = worm_stat_get_award_string(iVar7, iVar3);
                    iVar11 = FUN_8038b5c8(iVar11);
                    iVar12 = FUN_8038b5c8(-0x7fa2faec);
                    if (0x3ff < (uint)(iVar12 + iVar11)) break;
                    iVar11 = worm_stat_get_award_string(iVar7, iVar3);
                    FUN_8038b4a0(-0x7fa2faec, iVar11);
                    iVar20 = iVar20 + 1;
                }
                iVar3 = iVar3 + 1;
            } while (iVar3 < 3);
        }
        uVar4 = iVar17 + 1;
        local_80 = (double)(CONCAT44(0x43300000, uVar4) ^ 0x80000000);
        arcsel_draw_separator((double)*pLayout, (double)(float)(local_80 - DOUBLE_8061a430),
            dVar27, (double)FLOAT_8061a418, (double)(FLOAT_8061a444 * *(float*)(ctx + 0xf0)));
        local_88 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
        arcsel_draw_separator(dVar27, (double)(float)(local_88 - DOUBLE_8061a430),
            (double)pLayout[2], (double)(FLOAT_8061a444 * *(float*)(ctx + 0xf0)),
            (double)FLOAT_8061a418);
        iVar3 = iVar7 * 4;
        *(int*)(&DAT_805d0914 + iVar3) = *(int*)(&DAT_805d0914 + iVar3) + 1;
        dVar25 = text_get_char_width(&DAT_805d0514);
        local_90 = (double)CONCAT44(0x43300000, *(uint*)(&DAT_805d0914 + iVar3) ^ 0x80000000);
        if ((float)((double)(pLayout[2] - *pLayout) + dVar25) < (float)(local_90 - DOUBLE_8061a430)) {
            *(int*)(&DAT_805d0914 + iVar3) = 0;
        }
        local_80 = (double)(CONCAT44(0x43300000, *(int*)(&DAT_805d0914 + iVar3)) ^ 0x80000000);
        local_88 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
        text_draw_char_shadowed((double)(pLayout[2] - (float)(local_80 - DOUBLE_8061a430)),
            (double)(float)(local_88 - DOUBLE_8061a430), &DAT_805d0514, uVar19, uVar5);
        uVar4 = uVar4 + iVar2 + 1;
    } else {
        iVar20 = 1;
        dVar25 = arcsel_draw_results_status(ctx, (int)uVar29, pLayout, 1, (u32)(uintptr_t)param_5,
                                            param_6, param_7, param_8);
        local_98 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
        iVar2 = (int)((double)(float)(local_98 - DOUBLE_8061a430) + dVar25);
        local_a0 = (double)(long long)iVar2;
        local_90 = (double)(CONCAT44(0x43300000, iVar2) ^ 0x80000000);
        uVar4 = (uint)((float)(local_90 - DOUBLE_8061a430) - FLOAT_8061a4b0);
        local_88 = (double)(long long)(int)uVar4;
        text_set_font_table(3);
        iVar2 = (int)FLOAT_80611674;
    }
    local_80 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
    arcsel_draw_separator((double)*pLayout, (double)(float)(local_80 - DOUBLE_8061a430),
        dVar27, (double)FLOAT_8061a418, (double)(FLOAT_8061a444 * *(float*)(ctx + 0xf0)));
    local_88 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
    dVar25 = (double)(FLOAT_8061a444 * *(float*)(ctx + 0xf0));
    dVar22 = (double)FLOAT_8061a418;
    arcsel_draw_separator(dVar27, (double)(float)(local_88 - DOUBLE_8061a430),
        (double)pLayout[2], dVar25, dVar22);
    local_90 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
    int iVar21b = 0;
    uVar4 = (uint)((float)(local_90 - DOUBLE_8061a430) + FLOAT_8061a4b0);
    local_98 = (double)(long long)(int)uVar4;
    for (iVar3 = 0; iVar3 < DAT_80612610 + DAT_806120fc; iVar3 = iVar3 + 1) {
        iVar17 = *(int*)((int)&DAT_805014a0 + iVar21b);
        if (((-1 < iVar17) && (iVar17 != iVar7)) &&
            (puVar15 = (byte*)profile_get_slot(iVar17), puVar15 != (byte*)0x0)) {
            if (local_78 < *(short*)(puVar15 + iVar7 * 2 + 2)) {
                iVar6 = iVar17;
                local_78 = (int)*(short*)(puVar15 + iVar7 * 2 + 2);
            }
            if (local_74 < *(short*)(pcVar8 + iVar17 * 2 + 2)) {
                iVar18 = iVar17;
                local_74 = (int)*(short*)(pcVar8 + iVar17 * 2 + 2);
            }
        }
        iVar21b = iVar21b + 4;
    }
    dVar23 = worm_stat_get(10, iVar7);
    dVar27 = (double)FLOAT_8061a440;
    dVar24 = (double)FLOAT_8061a5c4;
    dVar28 = (double)(float)(dVar27 * dVar23);
    dVar26 = (double)(float)(dVar24 * (double)(pLayout[2] - *pLayout));
    if ((DAT_805015c0 & 0x8000000) == 0) {
        if (DAT_805015be == '\x10') {
            if (DAT_805015c8 < 1) {
                iVar3 = DAT_806118e4 + DAT_806109c0 * -3;
            } else {
                int* pRec = 0;
                worm_class_find_first_active(&pRec);
                iVar3 = *(int*)(DAT_806128ac + *pRec * 0x4c + 0x34);
            }
            time_format_short((char*)-0x7fa2faec, iVar3);
            pbVar14 = (byte*)get_string_by_id(0x17e1);
            text_apply_alpha_fade(dVar26, pbVar14, 0, 1);
            iVar3 = get_string_by_id(0x17e1);
            local_80 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
            text_draw_char_shadowed((double)*pLayout, (double)(float)(local_80 - DOUBLE_8061a430),
                iVar3, uVar9, uVar10);
            dVar23 = (double)FLOAT_8061a41c;
            text_set_color_rgb(dVar23, dVar23, dVar23);
        } else {
            param_6 = arcsel_score_column_null_arg();
            param_5 = &DAT_805d0514;
            iVar20 = *(int*)(pcVar8 + 0x50);
            arcsel_format_score(dVar23, dVar24, dVar27, dVar25, dVar22, in_f6, in_f7, in_f8,
                (int)*pcVar8, *(int*)(pcVar8 + 0x48), *(int*)(pcVar8 + 0x4c), iVar20,
                -0x7fa2faec, param_6, param_7, param_8);
            if (DAT_805015bd == '\x01') {
                pbVar14 = (byte*)get_string_by_id(0x15f2);
                text_apply_alpha_fade(dVar26, pbVar14, 0, 1);
                iVar3 = get_string_by_id(0x15f2);
                local_80 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
                text_draw_char_shadowed((double)*pLayout,
                    (double)(float)(local_80 - DOUBLE_8061a430), iVar3, uVar9, uVar10);
                dVar23 = (double)FLOAT_8061a41c;
                text_set_color_rgb(dVar23, dVar23, dVar23);
            } else {
                pbVar14 = (byte*)get_string_by_id(0x98a);
                text_apply_alpha_fade(dVar26, pbVar14, 0, 1);
                iVar3 = get_string_by_id(0x98a);
                local_80 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
                text_draw_char_shadowed((double)*pLayout,
                    (double)(float)(local_80 - DOUBLE_8061a430), iVar3, uVar9, uVar10);
                dVar23 = (double)FLOAT_8061a41c;
                text_set_color_rgb(dVar23, dVar23, dVar23);
            }
        }
        dVar23 = text_get_char_width(&DAT_805d0514);
        local_80 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
        text_draw_char_shadowed(
            (double)(pLayout[2] - (float)((double)FLOAT_8061a444 * (double)FLOAT_80611a68 + dVar23)),
            (double)(float)(local_80 - DOUBLE_8061a430), &DAT_805d0514, uVar19, uVar5);
        uVar4 = uVar4 + iVar2;
    }
    uVar16 = uVar5;
    uVar1 = uVar19;
    if ((local_70 != 0) && (-1 < iVar18)) {
        puVar15 = (byte*)profile_get_slot(iVar18);
        arcsel_promptbar_write_color((char)puVar15[1] + -1, (int)&local_a4, &local_a8);
        uVar16 = local_a8;
        uVar1 = local_a4;
    }
    local_a4 = uVar1;
    local_a8 = uVar16;
    pbVar14 = (byte*)get_string_by_id(0x6c9);
    text_apply_alpha_fade(dVar26, pbVar14, 0, 1);
    iVar3 = get_string_by_id(0x6c9);
    local_80 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
    text_draw_char_shadowed((double)*pLayout, (double)(float)(local_80 - DOUBLE_8061a430),
        iVar3, uVar9, uVar10);
    dVar24 = (double)FLOAT_8061a41c;
    dVar23 = dVar24;
    uVar29 = text_set_color_rgb(dVar24, dVar24, dVar24);
    if (iVar18 < 0) {
        iVar18 = get_string_by_id(0xbc4);
        FUN_80387fb8(uVar29, dVar24, dVar23, dVar25, dVar22, in_f6, in_f7, in_f8, -0x7fa2faec,
            &DAT_806110f8, iVar18, iVar20, param_5, param_6, param_7, param_8);
    } else {
        puVar15 = (byte*)profile_get_slot(iVar18);
        FUN_80387fb8(uVar29, dVar24, dVar23, dVar25, dVar22, in_f6, in_f7, in_f8, -0x7fa2faec,
            &DAT_806110f8, (int)(puVar15 + 0x20), iVar20, param_5, param_6, param_7, param_8);
    }
    text_apply_alpha_fade(dVar26, (byte*)&DAT_805d0514, 0, 1);
    dVar23 = text_get_char_width(&DAT_805d0514);
    local_80 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
    text_draw_char_shadowed(
        (double)(pLayout[2] - (float)((double)FLOAT_8061a444 * (double)FLOAT_80611a68 + dVar23)),
        (double)(float)(local_80 - DOUBLE_8061a430), &DAT_805d0514, local_a4, local_a8);
    dVar23 = (double)FLOAT_8061a41c;
    text_set_color_rgb(dVar23, dVar23, dVar23);
    uVar4 = uVar4 + iVar2;
    uVar16 = uVar5;
    uVar1 = uVar19;
    if ((local_70 != 0) && (-1 < iVar6)) {
        puVar15 = (byte*)profile_get_slot(iVar6);
        arcsel_promptbar_write_color((char)puVar15[1] + -1, (int)&local_a4, &local_a8);
        uVar16 = local_a8;
        uVar1 = local_a4;
    }
    local_a4 = uVar1;
    local_a8 = uVar16;
    pbVar14 = (byte*)get_string_by_id(0x6ca);
    text_apply_alpha_fade(dVar26, pbVar14, 0, 1);
    iVar18 = get_string_by_id(0x6ca);
    local_80 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
    text_draw_char_shadowed((double)*pLayout, (double)(float)(local_80 - DOUBLE_8061a430),
        iVar18, uVar9, uVar10);
    dVar24 = (double)FLOAT_8061a41c;
    dVar23 = dVar24;
    uVar29 = text_set_color_rgb(dVar24, dVar24, dVar24);
    if (iVar6 < 0) {
        iVar6 = get_string_by_id(0xbc4);
        FUN_80387fb8(uVar29, dVar24, dVar23, dVar25, dVar22, in_f6, in_f7, in_f8, -0x7fa2faec,
            &DAT_806110f8, iVar6, iVar20, param_5, param_6, param_7, param_8);
    } else {
        puVar15 = (byte*)profile_get_slot(iVar6);
        FUN_80387fb8(uVar29, dVar24, dVar23, dVar25, dVar22, in_f6, in_f7, in_f8, -0x7fa2faec,
            &DAT_806110f8, (int)(puVar15 + 0x20), iVar20, param_5, param_6, param_7, param_8);
    }
    text_apply_alpha_fade(dVar26, (byte*)&DAT_805d0514, 0, 1);
    dVar23 = text_get_char_width(&DAT_805d0514);
    local_80 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
    text_draw_char_shadowed(
        (double)(pLayout[2] - (float)((double)FLOAT_8061a444 * (double)FLOAT_80611a68 + dVar23)),
        (double)(float)(local_80 - DOUBLE_8061a430), &DAT_805d0514, local_a4, local_a8);
    dVar23 = (double)FLOAT_8061a41c;
    text_set_color_rgb(dVar23, dVar23, dVar23);
    uVar4 = uVar4 + iVar2;
    pbVar14 = (byte*)get_string_by_id(0x678);
    text_apply_alpha_fade(dVar26, pbVar14, 0, 1);
    iVar6 = get_string_by_id(0x678);
    local_88 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
    uVar16 = uVar10;
    text_draw_char_shadowed((double)*pLayout, (double)(float)(local_88 - DOUBLE_8061a430),
        iVar6, uVar9, uVar10);
    dVar24 = (double)FLOAT_8061a41c;
    dVar23 = dVar24;
    text_set_color_rgb(dVar24, dVar24, dVar24);
    FUN_80387fb8(dVar28, dVar24, dVar23, dVar25, dVar22, in_f6, in_f7, in_f8, -0x7fa2faec,
        &DAT_80611140, uVar16, iVar20, param_5, param_6, param_7, param_8);
    dVar25 = text_get_char_width(&DAT_805d0514);
    local_90 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
    text_draw_char_shadowed((double)(float)((double)pLayout[2] - dVar25),
        (double)(float)(local_90 - DOUBLE_8061a430), &DAT_805d0514, uVar19, uVar5);
    uVar4 = uVar4 + iVar2;
    pbVar14 = (byte*)get_string_by_id(0x6cc);
    text_apply_alpha_fade(dVar26, pbVar14, 0, 1);
    iVar6 = get_string_by_id(0x6cc);
    local_98 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
    text_draw_char_shadowed((double)*pLayout, (double)(float)(local_98 - DOUBLE_8061a430),
        iVar6, uVar9, uVar10);
    dVar25 = (double)FLOAT_8061a41c;
    text_set_color_rgb(dVar25, dVar25, dVar25);
    dVar25 = worm_stat_get(0x2d, iVar7);
    iVar6 = (int)dVar25;
    local_a0 = (double)(long long)iVar6;
    if (iVar6 == 0) {
        pbVar14 = (byte*)get_string_by_id(0x17bb);
        text_apply_alpha_fade(dVar26, pbVar14, 0, 1);
        iVar6 = get_string_by_id(0x17bb);
        pbVar14 = (byte*)get_string_by_id(0x17bb);
        dVar25 = text_get_char_width(pbVar14);
        local_80 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
        text_draw_char_shadowed((double)(float)((double)pLayout[2] - dVar25),
            (double)(float)(local_80 - DOUBLE_8061a430), iVar6, uVar19, uVar5);
        dVar25 = (double)FLOAT_8061a41c;
        text_set_color_rgb(dVar25, dVar25, dVar25);
    } else if (iVar6 == 0x46) {
        pbVar14 = (byte*)get_string_by_id(0x16c6);
        text_apply_alpha_fade(dVar26, pbVar14, 0, 1);
        iVar6 = get_string_by_id(0x16c6);
        pbVar14 = (byte*)get_string_by_id(0x16c6);
        dVar25 = text_get_char_width(pbVar14);
        local_80 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
        text_draw_char_shadowed((double)(float)((double)pLayout[2] - dVar25),
            (double)(float)(local_80 - DOUBLE_8061a430), iVar6, uVar19, uVar5);
        dVar25 = (double)FLOAT_8061a41c;
        text_set_color_rgb(dVar25, dVar25, dVar25);
    } else {
        pbVar14 = (byte*)get_string_by_id((&DAT_8043f294)[iVar6 * 0x15]);
        text_apply_alpha_fade(dVar26, pbVar14, 0, 1);
        iVar18 = get_string_by_id((&DAT_8043f294)[iVar6 * 0x15]);
        pbVar14 = (byte*)get_string_by_id((&DAT_8043f294)[iVar6 * 0x15]);
        dVar25 = text_get_char_width(pbVar14);
        local_80 = (double)CONCAT44(0x43300000, uVar4 ^ 0x80000000);
        text_draw_char_shadowed((double)(float)((double)pLayout[2] - dVar25),
            (double)(float)(local_80 - DOUBLE_8061a430), iVar18, uVar19, uVar5);
        dVar25 = (double)FLOAT_8061a41c;
        text_set_color_rgb(dVar25, dVar25, dVar25);
    }
    FUN_80382700();
    (void)param_1; (void)param_2; (void)param_4;
    (void)uVar1; (void)local_a0; (void)local_90; (void)local_88; (void)local_98;
    (void)iVar21b;
}
