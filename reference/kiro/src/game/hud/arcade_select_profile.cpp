// Batch: batch_846 | Module: arcade-select profile / unlock screen
// Address range: 0x802f44f4 - 0x802f5008
//
// The arcade "select profile / view unlocks" screen. arcsel_collect_locked_slots
// scans the current player's savegame character/weapon slot flags (0x1e0-byte
// blocks based at DAT_8051a994, indexed by DAT_806119ec) and marks which of the
// 22 tracked entries are locked, returning the locked count. The build_* helpers
// construct HUD list screens (locked-item list vs. confirm list); the transition
// handlers (confirm/back/cancel/pop) push and pop HUD screens; arcsel_render_score_column
// renders the per-profile score/name column for the arcade-select ticker rows.
//
// Named cross-module helpers (session_*, text_*, hud_*, gx_*, get_string_by_id,
// arcsel_count_profiles_for_mode, arcsel_format_score, player_ai_is_story_mode,
// savegame_get_player_block, player_apply_model_def, leaf_31e8, leaf_92c0)
// resolve through the shared generated headers below; only raw FUN_/DAT_/FLOAT_/
// DOUBLE_ symbols and the __save/restore intrinsics get per-file externs.

#include "common.h"
#include "hud.h"
#include "text.h"
#include "sprite.h"

// ---- raw symbols referenced by this translation unit -------------------------
extern "C" {
extern u64  __save_gpr_25(void);
extern void __restore_gpr_25(void);
extern u64  __save_gpr_28(void);
extern void __restore_gpr_28(void);

u64  FUN_803826b4(void);            // hud active-screen context lookup (returns pair)
void FUN_80382700(void);            // hud screen end/flush
int  FUN_803826f0(void);            // hud active-screen context ptr
u64  FUN_803826e4(void);            // hud active-screen ctx (returns element ptr)
void FUN_80382730(void);            // hud event-list finalize
void FUN_8038273c(void);            // hud screen finalize
u32  FUN_803825c0(double);          // color/float -> packed byte
u64  FUN_80387fb8(double,double,double,double,double,double,double,u64,
                  int,void*,int,int,byte*,byte*,u32,u32); // formatted text builder
u64  FUN_8038b4a0(int,int);         // string concat/measure

extern int   DAT_80612244;
extern int   DAT_80611d74;
extern u8    DAT_805015be;
extern u8    DAT_805015bd;
extern int   DAT_805015c0;
extern int   DAT_80612610;
extern int   DAT_806120fc;
extern int   DAT_80611d90;
extern int   DAT_805014a0;   // per-column visible-index table base
extern int   DAT_806110ec;
extern int   DAT_806110f8;
extern int   DAT_806110f0;
extern int   DAT_806119ec;   // current player savegame block index
extern int   DAT_805cc61c;
extern int   DAT_805cc60c;
extern int   DAT_80612614;
extern int   DAT_80612bc4;
extern int   DAT_806119e0;
extern short DAT_8051a918;   // profile block header (per-block, 0x1e0 stride)
extern short DAT_8051a91a;   // profile block header high half (flags, +2)
extern int   DAT_8051a91c;   // profile block payload (copy destination)
extern int   DAT_8051a814;   // profile block base (slot flags at +0x180 == DAT_8051a994)
extern int   DAT_8051a820;   // profile block root-screen id (0x1e0 stride)
extern int   DAT_8051a994;   // character/weapon slot flags table base
extern int   DAT_8051a9c8;   // slot-flag entry (char slot 0xd)
extern int   DAT_8051a9ec;   // profile block: last-changed list index (0x1e0 stride)
extern int   DAT_8051a9f0;   // profile block control-map field (0x1e0 stride)
extern int   DAT_804774b0;   // string-id table for unlock entries
extern int   DAT_804774e4;   // string-id for the trailing unlock entry
extern int   DAT_80477518;   // char-slot id table (7 entries) for index build
extern int   DAT_805d03a4;   // char event-index table (per unlock slot)
extern int   DAT_805d03d8;   // char event-index for the trailing slot
extern int   DAT_80477534;   // weapon event-index / slot-mode table (0xc stride)
extern unsigned char DAT_805d03fc;  // per-screen scroll/repeat state block (record path)
extern int   DAT_80477664;   // screen-id target for confirm-selection navigation
extern int   DAT_805cf280;   // per-player ticker state block base (indexed by DAT_806119ec)
extern int   DAT_80612bc0;   // arcsel profile dirty/state flag
extern int   DAT_80612bbc;   // arcsel last-focused weapon list index

extern float  FLOAT_8060f958;
extern float  FLOAT_8061a508;
extern float  FLOAT_8061a504;
extern float  FLOAT_8061a444;
extern float  FLOAT_80611a64;
extern float  FLOAT_80611a5c;
extern float  FLOAT_80611674;
extern float  FLOAT_8061a480;
extern float  FLOAT_8061a50c;
extern float  FLOAT_8061a418;
extern float  FLOAT_8061a488;
extern float  FLOAT_80611a68;
extern float  FLOAT_8061a41c;
extern float  FLOAT_8061a494;
extern float  FLOAT_8061a514;
extern float  FLOAT_8061a484;
extern float  FLOAT_8061a518;
extern float  FLOAT_8061a490;
extern float  FLOAT_8061a524;
extern float  FLOAT_8061a520;
extern double DOUBLE_8061a430;
}

// forward decls for the intra-file callback handlers used before definition
void arcsel_profile_cancel(void);
void arcsel_load_profile_block(int param_1);
uint arcsel_collect_locked_slots(u32 *param_1);
void arcsel_build_unlock_list(void);
void arcsel_build_confirm_list(int param_1);
int  arcsel_profile_slot_state(int param_1);   // 0x802f5204 (batch_847)
void arcsel_apply_selection_to_slots(int param_1);  // 0x802f5d2c (batch_848)
void arcsel_clear_slot_flags(void);                 // 0x802f5aac (batch_848)
void arcsel_save_char_selection(int, int, u32);     // 0x802f5b38 (batch_848)
void arcsel_save_weapon_selection(int, int, int, u32); // 0x802f5bb0 (batch_848)
void arcsel_save_weapon_pair(u32, u32, int, u32, u32); // 0x802f5c48 (batch_848)
void arcsel_save_all_selections(int param_1);       // 0x802f5918 (batch_848)
void arcsel_build_char_index_table(void);           // 0x802f5f3c (batch_848)
void arcsel_profile_mark_fresh(void);               // 0x802f5fd0 (batch_849)
void arcsel_add_char_row(int param_1);              // 0x802f6240 (batch_849)
void arcsel_build_profile_list(int param_1);        // 0x802f629c (batch_849)
void arcsel_profile_confirm_selection(int param_1); // 0x802f6580 (batch_849)
void arcsel_profile_selection_changed(int param_1); // 0x802f619c (batch_849)

// -----------------------------------------------------------------------------
// arcsel_render_score_column @ 0x802f44f4
// Renders the per-profile column of the arcade-select ticker: for each visible
// row it resolves the profile block, picks highlight colors, draws the gamemode
// label, the profile/score text and (optionally) the score value with shadow.
void arcsel_render_score_column(u32 param_1, u32 param_2, float *param_3, int param_4,
                                byte *param_5, byte *param_6, u32 param_7, u32 param_8)
{
    uint uVar1;
    int iVar2, iVar3, iVar4, iVar7, iVar8, iVar10, iVar11, iVar12;
    uint uVar5, uVar6, uVar13;
    byte *pbVar9;
    u64 uVar14, uVar25;
    double dVar15, dVar16, dVar17, dVar18, dVar19 = 0.0, dVar20, dVar21, dVar22, dVar23, dVar24;
    double in_f6 = 0.0, in_f7 = 0.0;
    u64 in_f8 = 0;
    uint local_158, local_154;
    unsigned char auStack_150[8];
    byte abStack_148[128];
    u32 local_c8;
    uint uStack_c4;
    double local_c0;

    uVar25 = FUN_803826b4();
    iVar2 = (int)(uVar25 >> 0x20);
    iVar8 = param_4;
    pbVar9 = param_5;
    iVar3 = session_get_state_flag();
    if (iVar3 == 1) {
        uStack_c4 = (DAT_80612244 != 0) ? 0x11 : 0xf;
    } else {
        uStack_c4 = 9 - (DAT_80612244 == 0);
    }
    uStack_c4 = uStack_c4 ^ 0x80000000;
    local_c8 = 0x43300000;
    dVar17 = (double)*param_3;
    dVar18 = (double)FLOAT_8060f958;
    dVar24 = (double)(float)((double)CONCAT44(0x43300000, uStack_c4) - DOUBLE_8061a430);
    dVar16 = (double)FLOAT_8061a508;
    dVar22 = (double)(float)((double)FLOAT_8061a504 * dVar18 + dVar17);
    dVar21 = -(double)(float)(dVar16 * dVar18 - (double)param_3[2]);
    iVar10 = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);
    dVar20 = (double)(FLOAT_8061a444 * FLOAT_80611a64 + param_3[1]);
    dVar23 = dVar17;
    iVar3 = session_get_state_flag();
    if (iVar3 == 1) {
        iVar4 = 8;
    } else {
        iVar3 = session_get_state_flag();
        iVar4 = (iVar3 == 2) ? 3 : 0xf;
    }
    text_set_font_table(iVar4);
    session_get_state_flag();
    uStack_c4 = 0x80000001;
    local_c8 = 0x43300000;
    (void)local_c8;
    dVar15 = (double)FLOAT_80611674;
    uVar1 = (uint)(dVar15 + (double)(float)(4503601774854145.0 - DOUBLE_8061a430));
    local_c0 = (double)(longlong)(int)uVar1;
    (void)local_c0;
    gx_set_sub_material_id(1);
    uVar13 = 0;
    iVar12 = 0;
    iVar3 = 0;
    for (;;) {
        iVar7 = DAT_80612610;
        if (DAT_80612610 + DAT_806120fc <= iVar12) {
            if ((param_4 != 0) && ((int)uVar25 != 0) &&
                (FLOAT_80611a5c * (float)(dVar20 - (double)param_3[1]) !=
                 *(float *)((int)uVar25 + 0x74))) {
                hud_screen_mark_hidden(iVar2);
            }
            FUN_80382700();
            return;
        }
        if ((-1 < *(int *)((int)&DAT_805014a0 + iVar3)) &&
            (iVar11 = DAT_80611d90 + *(int *)((int)&DAT_805014a0 + iVar3) * 0xea0, iVar11 != 0)) {
            dVar19 = (double)*(float *)(iVar2 + 0xf0);
            uVar5 = FUN_803825c0((double)(float)((double)FLOAT_8061a480 * dVar19));
            dVar19 = (double)(float)((double)FLOAT_8061a50c * dVar19);
            uVar6 = FUN_803825c0(dVar19);
            if ((DAT_805015be != 0x13) ||
                ((*(int *)(iVar11 + 8) < iVar7 ||
                  (iVar7 = player_ai_is_story_mode((int)*(short *)(iVar11 + 0x48)), iVar7 == 0)))) {
                if (DAT_805015bd == 0x03) {
                    arcsel_promptbar_write_color(
                        *(int *)(iVar11 + 0x84) + -1, (int)&local_154, &local_158);
                } else if ((DAT_805015c0 & 1) == 0) {
                    if (*(int *)(iVar11 + 8) == *(int *)(iVar10 + 8)) {
                        if ((param_5 + -1 < (byte *)0x2) || (*(int *)(iVar11 + 0xa68) < 0)) {
                            local_158 = uVar6 & 0xff;
                            local_154 = (uVar5 & 0xff) | 0x7f1e1e00;
                        } else {
                            local_154 = (uVar5 & 0xff) | 0x40787800;
                            local_158 = (uVar5 & 0xff) | 0x5000;
                        }
                    } else {
                        local_158 = uVar6 & 0xff;
                        local_154 = (uVar5 & 0xff) | 0x7f7f7f00;
                    }
                } else {
                    uVar5 = 1u << *(int *)(iVar11 + 0x84);
                    if ((uVar13 & uVar5) != 0) goto LAB_802f49ec;
                    uVar13 = uVar13 | uVar5;
                    arcsel_promptbar_write_color(
                        *(int *)(iVar11 + 0x84) + -1, (int)&local_154, &local_158);
                }
                iVar7 = session_get_state_flag();
                if (iVar7 == 1) {
                    text_set_font_table(8);
                }
                uVar14 = FUN_80387fb8(dVar19, dVar15, dVar16, dVar17, dVar18, in_f6, in_f7, in_f8,
                                      (int)abStack_148, &DAT_806110ec,
                                      *(int *)(iVar11 + 0xa68), iVar8, pbVar9, param_6,
                                      param_7, param_8);
                dVar19 = dVar20;
                if (param_4 != 0) {
                    dVar18 = (double)*(float *)(iVar2 + 0xa8);
                    dVar17 = (double)(float)(dVar18 - dVar24);
                    dVar16 = (double)FLOAT_8061a418;
                    in_f6 = (double)(float)(dVar18 + (double)*(float *)(iVar2 + 0xb0));
                    in_f7 = (double)(float)(dVar24 + in_f6);
                    text_draw_string_shadowed(dVar23, dVar20, dVar16, dVar17, dVar18, in_f6, in_f7);
                    dVar15 = dVar20;
                }
                if ((DAT_805015bd == 0x03) || ((DAT_805015c0 & 1) == 0) ||
                    (param_5 != (byte *)0x0)) {
                    uVar14 = FUN_80387fb8(uVar14, dVar15, dVar16, dVar17, dVar18, in_f6, in_f7,
                                          in_f8, (int)abStack_148, &DAT_806110f8,
                                          *(int *)(iVar11 + 0x70), iVar8, pbVar9, param_6,
                                          param_7, param_8);
                } else {
                    iVar7 = arcsel_count_profiles_for_mode(*(int *)(iVar11 + 0x84));
                    FUN_80387fb8(uVar14, dVar15, dVar16, dVar17, dVar18, in_f6, in_f7, in_f8,
                                 (int)auStack_150, &DAT_806110f0, iVar7, iVar8, pbVar9,
                                 param_6, param_7, param_8);
                    gamemode_get_name_string((char *)abStack_148, *(int *)(iVar11 + 0x84));
                    uVar14 = FUN_8038b4a0((int)abStack_148, (int)auStack_150);
                }
                text_set_font_table(iVar4);
                if (param_4 != 0) {
                    text_apply_alpha_fade(
                        (double)(float)(dVar21 - (double)(float)((double)FLOAT_8061a488 *
                                                                 (double)FLOAT_80611a68 + dVar22)),
                        abStack_148, 0, 1);
                    dVar18 = (double)*(float *)(iVar2 + 0xa8);
                    dVar17 = (double)(float)(dVar18 - dVar24);
                    in_f6 = (double)(float)(dVar18 + (double)*(float *)(iVar2 + 0xb0));
                    in_f7 = (double)(float)(dVar24 + in_f6);
                    text_draw_string_shadowed(dVar22, dVar19, (double)FLOAT_8061a418, dVar17,
                                              dVar18, in_f6, in_f7);
                    dVar15 = (double)FLOAT_8061a41c;
                    dVar16 = dVar15;
                    text_set_color_rgb(dVar15, dVar15, dVar15);
                }
                iVar8 = session_get_state_flag();
                if (iVar8 == 1) {
                    text_set_font_table(8);
                }
                param_6 = param_5;
                {
                    int scoreField = ((DAT_805015bd == 0x01) && (*(int *)(iVar11 + 0x80) != 0))
                                          ? *(int *)(iVar11 + 0xa64)
                                          : *(int *)(iVar11 + 0xa6c);
                    pbVar9 = abStack_148;
                    iVar8 = *(int *)(iVar11 + 0xa70);
                    iVar7 = (int)arcsel_format_score(uVar14, (u64)dVar15, (u64)dVar16, (u64)dVar17,
                                                (u64)dVar18, (u64)in_f6, (u64)in_f7, in_f8,
                                                *(int *)(iVar11 + 8),
                                                *(u32 *)(iVar11 + 0xa68), scoreField, iVar8,
                                                (int)pbVar9, (int)param_5, param_7, param_8);
                }
                if (0 < iVar7) {
                    leaf_92c0(1);
                }
                if (param_4 != 0) {
                    dVar18 = (double)*(float *)(iVar2 + 0xa8);
                    in_f6 = (double)(float)(dVar18 + (double)*(float *)(iVar2 + 0xb0));
                    dVar15 = dVar19;
                    dVar20 = (double)text_get_char_width(abStack_148);
                    dVar16 = (double)FLOAT_8061a418;
                    dVar17 = (double)(float)(dVar18 - dVar24);
                    in_f7 = (double)(float)(dVar24 + in_f6);
                    dVar19 = dVar15;
                    text_draw_string_shadowed((double)(float)(dVar21 - dVar20), dVar15, dVar16,
                                              dVar17, dVar18, in_f6, in_f7);
                }
                if (0 < iVar7) {
                    leaf_92c0(0);
                }
                local_c0 = (double)CONCAT44(0x43300000, uVar1 ^ 0x80000000);
                dVar20 = (double)(float)(dVar19 + (double)(float)(local_c0 - DOUBLE_8061a430));
            }
        }
    LAB_802f49ec:
        iVar12 = iVar12 + 1;
        iVar3 = iVar3 + 4;
    }
}

// -----------------------------------------------------------------------------
// arcsel_collect_locked_slots @ 0x802f4a90
// Zeroes a 22-entry table, then for each tracked character/weapon slot in the
// current player's savegame block (base DAT_8051a994, 0x1e0 stride), marks the
// entry locked (1) when its flag word is zero. Returns the number locked.
uint arcsel_collect_locked_slots(u32 *param_1)
{
    uint uVar1;
    int iVar2, iVar3;
    int blk = DAT_806119ec * 0x1e0;
    int base = (int)&DAT_8051a994 + blk;

    iVar2 = 0;
    iVar3 = 0x16;
    do {
        *(u32 *)((int)param_1 + iVar2) = 0;
        iVar2 = iVar2 + 4;
        iVar3 = iVar3 + -1;
    } while (iVar3 != 0);

    iVar2 = *(int *)(base);
    if (iVar2 == 0) {
        *param_1 = 1;
    }
    uVar1 = (uint)(iVar2 == 0);
    if (*(int *)(base + 0x04) == 0) { uVar1 += 1; param_1[1]   = 1; }
    if (*(int *)(base + 0x08) == 0) { uVar1 += 1; param_1[2]   = 1; }
    if (*(int *)(base + 0x0c) == 0) { uVar1 += 1; param_1[3]   = 1; }
    if (*(int *)(base + 0x10) == 0) { uVar1 += 1; param_1[4]   = 1; }
    if (*(int *)(base + 0x14) == 0) { uVar1 += 1; param_1[5]   = 1; }
    if (*(int *)(base + 0x34) == 0) { uVar1 += 1; param_1[0xd] = 1; }

    iVar2 = 0x38;
    iVar3 = 4;
    do {
        if (*(int *)(base + iVar2) == 0) {
            *(u32 *)((int)param_1 + iVar2) = 1;
            uVar1 = uVar1 + 1;
        }
        iVar2 = iVar2 + 4;
        iVar3 = iVar3 + -1;
    } while (iVar3 != 0);
    return uVar1;
}

// -----------------------------------------------------------------------------
// arcsel_load_profile_block @ 0x802f4c30
// Ensures the profile header block is initialised, then copies the active
// profile block either from the savegame slot (param_1 == 0) or from the live
// player context (param_1 != 0, also re-applies the model def).
void arcsel_load_profile_block(int param_1)
{
    uint uVar2;
    int iVar1 = DAT_806119ec * 0x1e0;

    if (*(short *)((int)&DAT_8051a918 + iVar1) == 0) {
        leaf_31e8((u32)((int)&DAT_8051a91c + iVar1), (u32)((int)&DAT_8051a994 + iVar1), 0x58);
    }
    if (param_1 == 0) {
        uVar2 = (uint)(u32)(uintptr_t)savegame_get_player_block(DAT_805cc61c);
        leaf_31e8(uVar2, (u32)((int)&DAT_8051a918 + DAT_806119ec * 0x1e0), 0x7c);
        DAT_805cc60c = DAT_805cc60c | 0x10000;
    } else {
        leaf_31e8(*(u32 *)(DAT_80611d74 + 0x1398),
                  (u32)((int)&DAT_8051a918 + DAT_806119ec * 0x1e0), 0x7c);
        player_apply_model_def(DAT_80611d74);
    }
}

// -----------------------------------------------------------------------------
// arcsel_profile_confirm @ 0x802f4d08
void arcsel_profile_confirm(void)
{
    if (DAT_80612614 < 0x65) {
        arcsel_load_profile_block(1);
        hud_screen_push();
    } else {
        DAT_80612bc4 = 1;
        arcsel_load_profile_block(0);
        hud_awards_get_saved_focus();
        hud_screen_pop_with_restore();
        hud_awards_get_saved_focus();
        hud_screen_pop_with_restore();
    }
}

// -----------------------------------------------------------------------------
// arcsel_profile_back @ 0x802f4d7c
void arcsel_profile_back(void)
{
    if (DAT_80612614 < 0x65) {
        hud_screen_push();
    } else {
        DAT_80612bc4 = 0;
        hud_awards_get_saved_focus();
        hud_screen_pop_with_restore();
        hud_awards_get_saved_focus();
        hud_screen_pop_with_restore();
    }
}

// -----------------------------------------------------------------------------
// arcsel_screen_pop_restore @ 0x802f4de0
void arcsel_screen_pop_restore(void)
{
    hud_screen_pop_with_restore();
}

// -----------------------------------------------------------------------------
// arcsel_profile_cancel @ 0x802f4e14
void arcsel_profile_cancel(void)
{
    if (DAT_80612614 < 0x65) {
        arcsel_screen_pop_restore();
    } else {
        hud_awards_get_saved_focus();
        hud_screen_pop_with_restore();
    }
}

// -----------------------------------------------------------------------------
// arcsel_build_unlock_list @ 0x802f4e4c
// Builds the HUD list of locked/unlocked entries. If more than the mode limit
// (4, or 6 in story mode) are locked, shows a single "all locked" line; else it
// lists each locked entry by its string id from the DAT_804774b0 id table.
void arcsel_build_unlock_list(void)
{
    int iVar1, iVar3;
    uint uVar2;
    int local_68[26];

    uVar2 = arcsel_collect_locked_slots((u32 *)local_68);
    hud_set_context_property(0x30504);
    iVar3 = session_get_state_flag();
    iVar1 = (iVar3 == 1) ? 6 : 4;
    if (iVar1 < (int)uVar2) {
        get_string_by_id(0x17b5);
        hud_add_list_item();
    } else {
        get_string_by_id(0x17b4);
        hud_add_list_item();
        hud_add_padding_item((double)FLOAT_8061a494);
        iVar1 = 0;
        iVar3 = 0;
        do {
            if (*(int *)((int)local_68 + iVar3) != 0) {
                get_string_by_id(*(int *)((int)&DAT_804774b0 + iVar3));
                hud_add_list_item();
            }
            iVar1 = iVar1 + 1;
            iVar3 = iVar3 + 4;
        } while (iVar1 < 0x16);
    }
}

// -----------------------------------------------------------------------------
// arcsel_build_confirm_list @ 0x802f4f48
void arcsel_build_confirm_list(int param_1)
{
    hud_set_item_padding_mode((double)FLOAT_8061a494, 1, 1, 1);
    get_string_by_id(0x1790);
    hud_add_list_item();
    hud_add_default_callback_item();
    get_string_by_id(0x173e);
    hud_add_list_item();
    get_string_by_id(0x173f);
    hud_add_list_item();
    hud_set_focus_by_id(param_1, 1);
}

// -----------------------------------------------------------------------------
// arcsel_build_profile_screen @ 0x802f5008
// Top-level builder for the arcade-select profile screen: initialises the layout
// from string 0x1749 with the cancel handler, then builds either the unlock list
// or the confirm list depending on whether the profile block is fresh and any
// entries are locked, applies centering and registers the OK/back handlers.
void arcsel_build_profile_screen(void)
{
    int iVar1;
    uint uVar2;
    u32 auStack_68[26];

    iVar1 = FUN_803826f0();
    uVar2 = arcsel_collect_locked_slots(auStack_68);
    arcsel_profile_slot_state(*(int *)(iVar1 + 0x38));
    {
        int strId = get_string_by_id(0x1749);
        void (*cancelCb)(void) = &arcsel_profile_cancel;  // bind the void(void) overload
        hud_element_init_from_layout(0, (void *)(intptr_t)strId, 0,
                                     (int)(intptr_t)cancelCb);
    }
    hud_set_destructor(*(void **)(iVar1 + 0x1e0));
    if ((*(short *)((int)&DAT_8051a918 + DAT_806119ec * 0x1e0) == 0) && (0 < (int)uVar2)) {
        arcsel_build_unlock_list();
    } else {
        arcsel_build_confirm_list(iVar1);
    }
    hud_element_apply_center_xy((int *)(intptr_t)DAT_806119e0);
    if ((*(short *)((int)&DAT_8051a918 + DAT_806119ec * 0x1e0) != 0) || ((int)uVar2 < 1)) {
        iVar1 = get_string_by_id(0x19c7);
        hud_screen_register_handler_global(0, iVar1, 0, 0);
    }
    iVar1 = get_string_by_id(0x19c8);
    hud_screen_register_handler_global(1, iVar1, 0, 0);
    FUN_8038273c();
}

// =============================================================================
// batch_847 | arcade-select profile control-map + event-list wiring
// Address range: 0x802f5140 - 0x802f5774
// =============================================================================

// -----------------------------------------------------------------------------
// arcsel_apply_control_map @ 0x802f5140
// If the profile block is fresh, seed the control map from the slot-flag table
// (player_fixup_control_map + copy); otherwise rebuild it from the stored header.
void arcsel_apply_control_map(void)
{
    int iVar1 = DAT_806119ec * 0x1e0;
    if (*(ushort *)((int)&DAT_8051a918 + iVar1) == 0) {
        player_fixup_control_map((int)&DAT_8051a91c + iVar1);
        leaf_31e8((u32)((int)&DAT_8051a994 + DAT_806119ec * 0x1e0),
                  (u32)(DAT_806119ec * 0x1e0 + (int)&DAT_8051a91c), 0x58);
    } else {
        player_build_control_map((uint)*(ushort *)((int)&DAT_8051a918 + iVar1),
                                 (u32 *)((int)&DAT_8051a994 + iVar1));
    }
}

// -----------------------------------------------------------------------------
// arcsel_clear_duplicate_events @ 0x802f51b4
// Walks the event list of a HUD element; for events bound to value param_2 on a
// slot other than param_3 (when param_3 < 0xe), clears the event id/value/flag.
void arcsel_clear_duplicate_events(int param_1, int param_2, int param_3)
{
    int iVar1 = *(int *)(param_1 + 0x174);
    if (param_3 < 0xe) {
        for (; *(int *)(iVar1 + 0x14) < 0xe; iVar1 = *(int *)(iVar1 + 4)) {
            if ((*(int *)(iVar1 + 0xcc) == param_2) && (*(int *)(iVar1 + 0x14) != param_3)) {
                *(u32 *)(iVar1 + 0xc8) = 0;
                *(u32 *)(iVar1 + 0xcc) = 0;
                *(u32 *)(iVar1 + 0xb8) = *(u32 *)(iVar1 + 0xb8) & 0xffffffef;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// arcsel_profile_slot_state @ 0x802f5204
// Returns 1 when the profile slot is selectable for the current session: only in
// non-record screens (id < 0x65) with mode > 2, and either mode 4 or slot 1/2.
int arcsel_profile_slot_state(int param_1)
{
    int iVar1;
    if ((DAT_80612614 < 0x65) && (iVar1 = session_get_state_flag(), 2 < iVar1)) {
        iVar1 = session_get_state_flag();
        if (iVar1 == 4) return 1;
        if ((param_1 == 1) || (param_1 == 2)) return 1;
        return 0;
    }
    return 0;
}

// -----------------------------------------------------------------------------
// arcsel_profile_is_multiplayer @ 0x802f527c
// Returns 1 when in a non-record screen (id < 0x65) with session mode > 1.
int arcsel_profile_is_multiplayer(void)
{
    int iVar1;
    if ((DAT_80612614 < 0x65) && (iVar1 = session_get_state_flag(), 1 < iVar1)) {
        return 1;
    }
    return 0;
}

// -----------------------------------------------------------------------------
// arcsel_profile_advance @ 0x802f52bc
// Advance handler: seed the control map when fresh, then push the next screen if
// there are locked entries, otherwise pop-with-restore. The >= 0x65 (record)
// path additionally restores the saved award focus.
void arcsel_profile_advance(void)
{
    uint uVar1;
    u32 auStack_68[25];

    if (*(short *)((int)&DAT_8051a918 + DAT_806119ec * 0x1e0) == 0) {
        arcsel_apply_selection_to_slots(*(int *)((int)&DAT_8051a9f0 + DAT_806119ec * 0x1e0));
    }
    if (DAT_80612614 < 0x65) {
        if ((*(short *)((int)&DAT_8051a918 + DAT_806119ec * 0x1e0) == 0) &&
            (uVar1 = arcsel_collect_locked_slots(auStack_68), 0 < (int)uVar1)) {
            hud_screen_push();
        } else {
            hud_screen_pop_with_restore();
        }
    } else if ((*(short *)((int)&DAT_8051a918 + DAT_806119ec * 0x1e0) == 0) &&
               (uVar1 = arcsel_collect_locked_slots(auStack_68), 0 < (int)uVar1)) {
        hud_awards_get_saved_focus();
        hud_screen_push();
    } else {
        hud_awards_get_saved_focus();
        hud_screen_pop_with_restore();
    }
}

// -----------------------------------------------------------------------------
// arcsel_build_char_event_list @ 0x802f53bc
// Allocates the character-selection event list on the active HUD element: entry
// 0 is the "none" string (0x173d, disabled), then one event per unlock slot from
// the DAT_805d03a4 index table / DAT_804774b0 string table, plus a trailing entry.
void arcsel_build_char_event_list(void)
{
    int iVar1, iVar2, iVar3, iVar4, iVar5;
    uint uVar6;

    iVar1 = (int)FUN_803826e4();
    hud_event_alloc();
    iVar2 = (int)(intptr_t)hud_item_get_event_at_index(iVar1, 0);
    iVar3 = get_string_by_id(0x173d);
    *(int *)(iVar2 + 4) = iVar3;
    uVar6 = 1;
    *(u32 *)(iVar2 + 0xc) = *(u32 *)(iVar2 + 0xc) | 8;
    *(u32 *)(iVar2 + 8) = 0;
    do {
        hud_event_alloc();
        uVar6 = uVar6 + 1;
    } while (uVar6 < 8);

    iVar2 = 0;
    iVar3 = 0;
    do {
        if (*(int *)((int)&DAT_805d03a4 + iVar3) != -1) {
            iVar4 = (int)(intptr_t)hud_item_get_event_at_index(iVar1, *(int *)((int)&DAT_805d03a4 + iVar3));
            iVar5 = get_string_by_id(*(int *)((int)&DAT_804774b0 + iVar3));
            *(int *)(iVar4 + 4) = iVar5;
            *(int *)(iVar4 + 8) = iVar2 + 2;
        }
        iVar2 = iVar2 + 1;
        iVar3 = iVar3 + 4;
    } while (iVar2 < 6);

    if (DAT_805d03d8 != -1) {
        iVar1 = (int)(intptr_t)hud_item_get_event_at_index(iVar1, DAT_805d03d8);
        iVar2 = get_string_by_id(DAT_804774e4);
        *(int *)(iVar1 + 4) = iVar2;
        *(u32 *)(iVar1 + 8) = 0xf;
    }
    FUN_80382730();
}

// -----------------------------------------------------------------------------
// arcsel_build_weapon_event_list @ 0x802f54c8
// Builds the weapon-selection event list on the active HUD element (ctx passed in
// the saved GPR pair). Entry 0 is "none" (0x173d), then one event per weapon slot
// whose mode table (DAT_80477534, 0xc stride) matches param_3 or is 2 (any).
void arcsel_build_weapon_event_list(u32 param_1, u32 param_2, int param_3)
{
    int iVar1, iVar2, iVar3, iVar4, iVar5, iVar6;
    u64 ctx = __save_gpr_25();
    iVar1 = (int)(ctx >> 0x20);

    hud_item_add_spacer((double)FLOAT_8061a418, 0);
    iVar4 = 0;
    do {
        iVar2 = (int)(intptr_t)hud_event_alloc();
        hud_event_set_disabled(iVar2, 1);
        iVar4 = iVar4 + 1;
    } while (iVar4 < 5);

    iVar4 = (int)(intptr_t)hud_item_get_event_at_index(iVar1, 0);
    iVar2 = get_string_by_id(0x173d);
    *(int *)(iVar4 + 4) = iVar2;
    iVar6 = 0xe;
    iVar5 = 0x38;
    *(u32 *)(iVar4 + 0xc) = *(u32 *)(iVar4 + 0xc) | 8;
    iVar2 = 0xa8;
    *(u32 *)(iVar4 + 8) = 0;
    do {
        if ((*(int *)((int)&DAT_80477534 + iVar2 + (int)ctx * 4) != -1) &&
            ((*(int *)((int)&DAT_80477534 + iVar2 + 8) == 2 ||
              (param_3 == *(int *)((int)&DAT_80477534 + iVar2 + 8))))) {
            iVar4 = (int)(intptr_t)hud_item_get_event_at_index(
                iVar1, *(int *)((int)&DAT_80477534 + iVar2 + (int)ctx * 4));
            iVar3 = get_string_by_id(*(int *)((int)&DAT_804774b0 + iVar5));
            *(int *)(iVar4 + 4) = iVar3;
            *(u32 *)(iVar4 + 0xc) = *(u32 *)(iVar4 + 0xc) & 0xfffffff6;
            *(int *)(iVar4 + 8) = iVar6 + 2;
        }
        iVar6 = iVar6 + 1;
        iVar2 = iVar2 + 0xc;
        iVar5 = iVar5 + 4;
    } while (iVar6 < 0x16);
    __restore_gpr_25();
}

// -----------------------------------------------------------------------------
// arcsel_set_char_event_value @ 0x802f55f0
// Finds a child list item by id and binds its selected event to the character
// slot matching param_3 (via DAT_8051a994 slot flags / DAT_805d03a4 index table).
void arcsel_set_char_event_value(int param_1, int param_2, int param_3)
{
    int iVar1, iVar2, iVar3, iVar4;

    iVar1 = (int)(intptr_t)hud_find_child_by_id(param_1, param_2);
    if (iVar1 != 0) {
        iVar2 = 0;
        *(u32 *)(iVar1 + 0xc8) = 0;
        *(u32 *)(iVar1 + 0xcc) = 0;
        iVar3 = 0;
        iVar4 = 0xe;
        do {
            if ((param_3 == *(int *)((int)&DAT_8051a994 + DAT_806119ec * 0x1e0 + iVar2)) &&
                (*(int *)((int)&DAT_805d03a4 + iVar2) != -1)) {
                *(int *)(iVar1 + 0xc8) = *(int *)((int)&DAT_805d03a4 + iVar2);
                *(int *)(iVar1 + 0xcc) = iVar3 + 2;
                *(u32 *)(iVar1 + 0xb8) = *(u32 *)(iVar1 + 0xb8) & 0xffffffef;
            }
            iVar3 = iVar3 + 1;
            iVar2 = iVar2 + 4;
            iVar4 = iVar4 + -1;
        } while (iVar4 != 0);
    }
}

// -----------------------------------------------------------------------------
// arcsel_set_weapon_event_value @ 0x802f5698
// Finds a child item (ctx/id in saved GPR pair) whose element state == param_3,
// and binds its event to the weapon slot matching param_4 via the DAT_80477534
// mode table indexed by the current element state.
void arcsel_set_weapon_event_value(u32 param_1, u32 param_2, int param_3, int param_4)
{
    int iVar1, iVar2, iVar3, iVar4, iVar5;
    u64 ctx = __save_gpr_28();
    iVar2 = (int)(ctx >> 0x20);

    iVar1 = (int)(intptr_t)hud_find_child_by_id(iVar2, (int)ctx);
    if ((iVar1 != 0) && (iVar2 = hud_element_get_state(iVar2), param_3 == iVar2)) {
        *(u32 *)(iVar1 + 0xc8) = 0;
        *(u32 *)(iVar1 + 0xcc) = 0;
        iVar4 = 0xe;
        iVar2 = 0xa8;
        iVar3 = 0x38;
        iVar5 = 8;
        do {
            if ((param_4 == *(int *)((int)&DAT_8051a994 + DAT_806119ec * 0x1e0 + iVar3)) &&
                (*(int *)((int)&DAT_80477534 + iVar2 + param_3 * 4) != -1)) {
                *(int *)(iVar1 + 0xc8) = *(int *)((int)&DAT_80477534 + iVar2 + param_3 * 4);
                *(int *)(iVar1 + 0xcc) = iVar4 + 2;
                *(u32 *)(iVar1 + 0xb8) = *(u32 *)(iVar1 + 0xb8) & 0xffffffef;
            }
            iVar4 = iVar4 + 1;
            iVar2 = iVar2 + 0xc;
            iVar3 = iVar3 + 4;
            iVar5 = iVar5 + -1;
        } while (iVar5 != 0);
    }
    __restore_gpr_28();
}

// -----------------------------------------------------------------------------
// arcsel_match_weapon_event_pair @ 0x802f5774
// Like set_weapon_event_value but for a two-slot pair (param_4, param_5): resolves
// the weapon event index for each slot and, if both resolve to the same non-(-1)
// event, binds it; otherwise leaves the item cleared.
void arcsel_match_weapon_event_pair(u32 param_1, u32 param_2, int param_3, int param_4, int param_5)
{
    int iVar1, iVar2, iVar3, iVar4, iVar7;
    int iVar6 = -1;
    int iVar5 = -1;
    u64 ctx = __save_gpr_25();
    iVar3 = (int)(ctx >> 0x20);

    iVar2 = (int)(intptr_t)hud_find_child_by_id(iVar3, (int)ctx);
    if ((iVar2 != 0) && (iVar3 = hud_element_get_state(iVar3), param_3 == iVar3)) {
        iVar4 = 0;
        *(u32 *)(iVar2 + 0xc8) = 0;
        *(u32 *)(iVar2 + 0xcc) = 0;
        iVar3 = 0;
        iVar7 = 0xe;
        do {
            if ((param_4 == *(int *)((int)&DAT_8051a994 + DAT_806119ec * 0x1e0 + iVar4)) &&
                (iVar1 = *(int *)((int)&DAT_80477534 + iVar3 + param_3 * 4), iVar1 != -1)) {
                iVar6 = iVar1;
            }
            if ((param_5 == *(int *)((int)&DAT_8051a994 + DAT_806119ec * 0x1e0 + iVar4)) &&
                (iVar1 = *(int *)((int)&DAT_80477534 + iVar3 + param_3 * 4), iVar1 != -1)) {
                iVar5 = iVar1;
            }
            iVar3 = iVar3 + 0xc;
            iVar4 = iVar4 + 4;
            iVar7 = iVar7 + -1;
        } while (iVar7 != 0);
        if ((iVar6 == iVar5) && (iVar6 != -1)) {
            *(int *)(iVar2 + 0xc8) = iVar6;
            *(u32 *)(iVar2 + 0xb8) = *(u32 *)(iVar2 + 0xb8) & 0xffffffef;
        }
    }
    __restore_gpr_25();
}

// =============================================================================
// batch_848 | arcade-select profile selection write-back + commit
// Address range: 0x802f5874 - 0x802f5fa4
// =============================================================================

// -----------------------------------------------------------------------------
// arcsel_update_event_dimming @ 0x802f5874
// Refreshes the dim (0x10) flag across a HUD element's event list: with no active
// selection, clears dim on entries >2; otherwise dims entries that duplicate the
// current selection (same event id, index != selected, selected != -1).
void arcsel_update_event_dimming(int param_1)
{
    int iVar1;
    int iVar3 = *(int *)(param_1 + 0x188);
    int iVar2 = *(int *)(*(int *)(param_1 + 0x194) + 0xcc);
    int iVar4 = *(int *)(param_1 + 0x174);

    if (iVar2 == 0) {
        for (; *(int *)(iVar4 + 0x14) < 0xe; iVar4 = *(int *)(iVar4 + 4)) {
            if (2 < *(int *)(iVar4 + 0x14)) {
                *(u32 *)(iVar4 + 0xb8) = *(u32 *)(iVar4 + 0xb8) & 0xffffffef;
            }
        }
        return;
    }
    if (iVar3 < 0xe) {
        for (; iVar1 = *(int *)(iVar4 + 0x14), iVar1 < 0xe; iVar4 = *(int *)(iVar4 + 4)) {
            if (((*(int *)(iVar4 + 0xcc) == iVar2) && (2 < iVar1)) &&
                (iVar1 != iVar3) && (iVar3 != -1)) {
                *(u32 *)(iVar4 + 0xb8) = *(u32 *)(iVar4 + 0xb8) | 0x10;
            } else {
                *(u32 *)(iVar4 + 0xb8) = *(u32 *)(iVar4 + 0xb8) & 0xffffffef;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// arcsel_save_all_selections @ 0x802f5918
// Dispatch table: read each list item's current selection back into the slot
// flag table (7 character slots + 4 weapon slots x2 columns + 2 paired weapons).
void arcsel_save_all_selections(int param_1)
{
    arcsel_set_char_event_value(param_1, 3, 0x200);
    arcsel_set_char_event_value(param_1, 5, 0x800);
    arcsel_set_char_event_value(param_1, 4, 0x400);
    arcsel_set_char_event_value(param_1, 6, 0x10);
    arcsel_set_char_event_value(param_1, 7, 0x40);
    arcsel_set_char_event_value(param_1, 8, 0x20);
    arcsel_set_char_event_value(param_1, 9, 0x80);
    arcsel_set_weapon_event_value(param_1, 10, 0, 4);
    arcsel_set_weapon_event_value(param_1, 0xb, 0, 3);
    arcsel_set_weapon_event_value(param_1, 0xc, 0, 2);
    arcsel_set_weapon_event_value(param_1, 0xd, 0, 1);
    arcsel_match_weapon_event_pair(param_1, 0xe, 0, 4, 8);
    arcsel_match_weapon_event_pair(param_1, 0xf, 0, 1, 2);
    arcsel_set_weapon_event_value(param_1, 10, 1, 4);
    arcsel_set_weapon_event_value(param_1, 0xb, 1, 3);
    arcsel_set_weapon_event_value(param_1, 0xc, 1, 2);
    arcsel_set_weapon_event_value(param_1, 0xd, 1, 1);
    arcsel_match_weapon_event_pair(param_1, 0xe, 1, 4, 8);
    arcsel_match_weapon_event_pair(param_1, 0xf, 1, 1, 2);
}

// -----------------------------------------------------------------------------
// arcsel_clear_slot_flags @ 0x802f5aac
// Zero the 6 character slot-flag words + the char slot 0xd flag, then zero each
// weapon slot flag whose mode table (DAT_80477534, 0xc stride) entry is valid.
void arcsel_clear_slot_flags(void)
{
    int iVar1, iVar2, iVar3, iVar4;

    iVar2 = DAT_806119ec * 0x1e0;
    iVar3 = 6;
    iVar1 = 0;
    do {
        *(u32 *)((int)&DAT_8051a994 + iVar2 + iVar1) = 0;
        iVar3 = iVar3 + -1;
        iVar1 = iVar1 + 4;
    } while (iVar3 != 0);

    iVar3 = DAT_806119ec * 0x1e0;
    iVar1 = 0x38;
    *(u32 *)((int)&DAT_8051a9c8 + iVar3) = 0;
    iVar2 = 0xa8;
    iVar4 = 8;
    do {
        if (*(int *)((int)&DAT_80477534 + iVar2) != -1) {
            *(u32 *)((int)&DAT_8051a994 + iVar3 + iVar1) = 0;
        }
        iVar1 = iVar1 + 4;
        iVar2 = iVar2 + 0xc;
        iVar4 = iVar4 + -1;
    } while (iVar4 != 0);
}

// -----------------------------------------------------------------------------
// arcsel_save_char_selection @ 0x802f5b38
// Find the child list item by id; when its selected event matches a char slot in
// the DAT_805d03a4 index table, write param_3 into that slot's flag word.
void arcsel_save_char_selection(int param_1, int param_2, u32 param_3)
{
    int iVar2, iVar3;
    int iVar1 = (int)(intptr_t)hud_find_child_by_id(param_1, param_2);
    iVar2 = 0;
    iVar3 = 0xe;
    do {
        if (*(int *)(iVar1 + 0xc8) == *(int *)((int)&DAT_805d03a4 + iVar2)) {
            *(u32 *)((int)&DAT_8051a994 + DAT_806119ec * 0x1e0 + iVar2) = param_3;
            return;
        }
        iVar2 = iVar2 + 4;
        iVar3 = iVar3 + -1;
    } while (iVar3 != 0);
}

// -----------------------------------------------------------------------------
// arcsel_save_weapon_selection @ 0x802f5bb0
// Find the child item by id; when its selected event matches a weapon slot in the
// DAT_80477534 mode table (indexed by param_3), write param_4 into slot 0xe flag.
void arcsel_save_weapon_selection(int param_1, int param_2, int param_3, u32 param_4)
{
    int iVar2, iVar3, iVar4;
    int iVar1 = (int)(intptr_t)hud_find_child_by_id(param_1, param_2);
    iVar3 = 0xe;
    iVar2 = 0xa8;
    iVar4 = 8;
    do {
        if (*(int *)(iVar1 + 0xc8) == *(int *)((int)&DAT_80477534 + iVar2 + param_3 * 4)) {
            *(u32 *)((int)&DAT_8051a994 + DAT_806119ec * 0x1e0 + iVar3 * 4) = param_4;
            return;
        }
        iVar3 = iVar3 + 1;
        iVar2 = iVar2 + 0xc;
        iVar4 = iVar4 + -1;
    } while (iVar4 != 0);
}

// -----------------------------------------------------------------------------
// arcsel_save_weapon_pair @ 0x802f5c48
// Active-element handler for a paired weapon row (ctx in saved GPR pair). For each
// matching weapon slot, write param_4 to the first match and param_5 to the second
// into the profile block flag region (&DAT_8051a814 + 0x180 == DAT_8051a994).
void arcsel_save_weapon_pair(u32 param_1, u32 param_2, int param_3, u32 param_4, u32 param_5)
{
    int iVar1, iVar2, iVar3, iVar4, iVar5, iVar6;
    u64 uVar7 = FUN_803826e4();
    iVar1 = (int)(uVar7 >> 0x20);
    iVar5 = 0;
    iVar2 = hud_element_get_state(iVar1);
    if ((param_3 == iVar2) &&
        (iVar2 = (int)(intptr_t)hud_find_child_by_id(iVar1, (int)uVar7), iVar2 != 0)) {
        iVar2 = (int)(intptr_t)hud_find_child_by_id(iVar1, (int)uVar7);
        iVar1 = DAT_806119ec;
        iVar4 = *(int *)(iVar2 + 0xc8);
        iVar2 = 0;
        iVar3 = 0;
        iVar6 = 0xe;
        do {
            if (iVar4 == *(int *)((int)&DAT_80477534 + iVar3 + param_3 * 4)) {
                if (iVar5 == 0) {
                    iVar5 = 1;
                    *(u32 *)((int)&DAT_8051a814 + iVar2 + 0x180 + iVar1 * 0x1e0) = param_4;
                } else {
                    iVar5 = iVar5 + 1;
                    *(u32 *)((int)&DAT_8051a814 + iVar2 + 0x180 + iVar1 * 0x1e0) = param_5;
                }
            }
            iVar2 = iVar2 + 4;
            iVar3 = iVar3 + 0xc;
            iVar6 = iVar6 + -1;
        } while (iVar6 != 0);
    }
    FUN_80382730();
}

// -----------------------------------------------------------------------------
// arcsel_apply_selection_to_slots @ 0x802f5d2c
// Clear slot flags then save every character/weapon selection from the list items
// back into the profile block (mirror of arcsel_save_all_selections' bindings).
void arcsel_apply_selection_to_slots(int param_1)
{
    int iVar1;
    arcsel_clear_slot_flags();
    arcsel_save_char_selection(param_1, 3, 0x200);
    arcsel_save_char_selection(param_1, 5, 0x800);
    arcsel_save_char_selection(param_1, 4, 0x400);
    arcsel_save_char_selection(param_1, 6, 0x10);
    arcsel_save_char_selection(param_1, 7, 0x40);
    arcsel_save_char_selection(param_1, 8, 0x20);
    arcsel_save_char_selection(param_1, 9, 0x80);
    iVar1 = hud_element_get_state(param_1);
    arcsel_save_weapon_selection(param_1, 10, iVar1, 4);
    iVar1 = hud_element_get_state(param_1);
    arcsel_save_weapon_selection(param_1, 0xb, iVar1, 3);
    iVar1 = hud_element_get_state(param_1);
    arcsel_save_weapon_selection(param_1, 0xc, iVar1, 2);
    iVar1 = hud_element_get_state(param_1);
    arcsel_save_weapon_selection(param_1, 0xd, iVar1, 1);
    iVar1 = hud_element_get_state(param_1);
    arcsel_save_weapon_pair(param_1, 0xe, iVar1, 4, 8);
    iVar1 = hud_element_get_state(param_1);
    arcsel_save_weapon_pair(param_1, 0xf, iVar1, 1, 2);
}

// -----------------------------------------------------------------------------
// arcsel_commit_profile_screen @ 0x802f5e78
// On screen close (all children closing): if block fresh apply selection + copy
// header, clear dirty flag, store the header id from the active element, re-seed
// the control map, then re-run the selection save. Finalizes the HUD screen.
void arcsel_commit_profile_screen(void)
{
    int iVar1;
    u64 uVar3 = FUN_803826f0();
    int iVar2 = *(int *)((int)&DAT_8051a9f0 + DAT_806119ec * 0x1e0);

    iVar1 = hud_screen_all_children_closing((int)(uVar3 >> 0x20));
    if (iVar1 != 0) {
        if (*(short *)((int)&DAT_8051a918 + DAT_806119ec * 0x1e0) == 0) {
            arcsel_apply_selection_to_slots(iVar2);
            leaf_31e8((u32)(DAT_806119ec * 0x1e0 + (int)&DAT_8051a91c),
                      (u32)((int)&DAT_8051a994 + DAT_806119ec * 0x1e0), 0x58);
        }
        DAT_80612bc0 = 0;
        *(short *)((int)&DAT_8051a918 + DAT_806119ec * 0x1e0) =
            (short)*(u32 *)((int)uVar3 + 0xc8);
        arcsel_apply_control_map();
        arcsel_save_all_selections(iVar2);
    }
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// arcsel_build_char_index_table @ 0x802f5f3c
// Build the DAT_805d03a4 event-index table (22 entries): for each slot id, look it
// up in the 7-entry DAT_80477518 char-id table and store (found index + 1), or -1.
void arcsel_build_char_index_table(void)
{
    int iVar1, iVar2, iVar4, iVar5;
    int iVar3 = 0;
    iVar1 = 0;
    do {
        *(u32 *)((int)&DAT_805d03a4 + iVar1) = 0xffffffff;
        iVar4 = 0;
        iVar2 = 0;
        iVar5 = 7;
        do {
            if (iVar3 == *(int *)((int)&DAT_80477518 + iVar2)) {
                *(int *)((int)&DAT_805d03a4 + iVar1) = iVar4 + 1;
                break;
            }
            iVar4 = iVar4 + 1;
            iVar2 = iVar2 + 4;
            iVar5 = iVar5 + -1;
        } while (iVar5 != 0);
        iVar3 = iVar3 + 1;
        iVar1 = iVar1 + 4;
    } while (iVar3 < 0x16);
}

// -----------------------------------------------------------------------------
// arcsel_reset_profile_state @ 0x802f5fa4
// Clear the profile dirty flag, re-seed the control map, rebuild the char index.
void arcsel_reset_profile_state(void)
{
    DAT_80612bc0 = 0;
    arcsel_apply_control_map();
    arcsel_build_char_index_table();
}

// =============================================================================
// batch_849 | arcade-select profile screen build + input handling
// Address range: 0x802f5fd0 - 0x802f677c
// =============================================================================

// -----------------------------------------------------------------------------
// arcsel_profile_mark_fresh @ 0x802f5fd0
// Clear the profile block header (mark fresh) and notify the value-change hook.
// Used as the confirm callback for the "reset profile" yes/no dialog.
void arcsel_profile_mark_fresh(void)
{
    *(u16 *)((int)&DAT_8051a918 + DAT_806119ec * 0x1e0) = 0;
    hud_item_on_value_change();
}

// -----------------------------------------------------------------------------
// arcsel_profile_reset_prompt @ 0x802f6014
// First activation sets the 0x100 header flag and marks the block fresh; on a
// second activation, shows a reset-confirm yes/no dialog (styled + nudged when
// the slot is selectable).
void arcsel_profile_reset_prompt(int param_1)
{
    u16 *puVar1 = (u16 *)((int)&DAT_8051a91a + DAT_806119ec * 0x1e0);
    if ((*puVar1 & 0x100) == 0) {
        *puVar1 = *puVar1 | 0x100;
        arcsel_profile_mark_fresh();
    } else {
        int iVar2 = arcsel_profile_slot_state(*(int *)(param_1 + 0x38));
        if (iVar2 == 0) {
            int str = get_string_by_id(0x175f);
            hud_create_yes_no_dialog(param_1, str, (int)(intptr_t)&arcsel_profile_mark_fresh, 0);
        } else {
            int str = get_string_by_id(0x175f);
            hud_create_yes_no_dialog_styled(param_1, str,
                                            (int)(intptr_t)&arcsel_profile_mark_fresh, 0, 0x12);
            int elem = (int)(intptr_t)hud_get_last_created_item();
            arcsel_element_tick_scroll(elem);
            *(float *)(elem + 0x60) = *(float *)(elem + 0x60) - FLOAT_8061a488;
        }
    }
}

// -----------------------------------------------------------------------------
// arcsel_profile_toggle_locks @ 0x802f60e4
// Enable (fresh block) or disable+dim (loaded block) the profile's child list
// items 2..0x10 by clearing/setting the 0x5 bits in their state word (+0x28).
void arcsel_profile_toggle_locks(void)
{
    int iVar2, iVar3;
    int iVar1 = FUN_803826f0();
    if (*(short *)((int)&DAT_8051a918 + DAT_806119ec * 0x1e0) == 0) {
        iVar3 = 2;
        do {
            iVar2 = (int)(intptr_t)hud_find_child_by_id(iVar1, iVar3);
            if (iVar2 != 0) {
                *(u32 *)(iVar2 + 0x28) = *(u32 *)(iVar2 + 0x28) & 0xfffffffa;
            }
            iVar3 = iVar3 + 1;
        } while (iVar3 < 0x11);
    } else {
        iVar3 = 2;
        do {
            iVar2 = (int)(intptr_t)hud_find_child_by_id(iVar1, iVar3);
            if (iVar2 != 0) {
                *(u32 *)(iVar2 + 0x28) = *(u32 *)(iVar2 + 0x28) | 5;
            }
            iVar3 = iVar3 + 1;
        } while (iVar3 < 0x11);
    }
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// arcsel_profile_selection_changed @ 0x802f619c
// When a list selection changes on a fresh block: if the previous index was a
// header (<3) or unchanged, refresh dimming; otherwise clear duplicate events on
// the previously-selected item. Records the new selected index.
void arcsel_profile_selection_changed(int param_1)
{
    int iVar1;
    if (*(short *)((int)&DAT_8051a918 + DAT_806119ec * 0x1e0) == 0) {
        iVar1 = *(int *)((int)&DAT_8051a9ec + DAT_806119ec * 0x1e0);
        if ((iVar1 < 3) || (*(int *)(param_1 + 0x188) == iVar1)) {
            arcsel_update_event_dimming(param_1);
        } else {
            iVar1 = (int)(intptr_t)hud_find_child_by_id(param_1, iVar1);
            arcsel_clear_duplicate_events(
                param_1, *(int *)(iVar1 + 0xcc),
                *(int *)((int)&DAT_8051a9ec + DAT_806119ec * 0x1e0));
        }
        *(u32 *)((int)&DAT_8051a9ec + DAT_806119ec * 0x1e0) = *(u32 *)(param_1 + 0x188);
    }
}

// -----------------------------------------------------------------------------
// arcsel_add_char_row @ 0x802f6240
// Append a character-selection list row (label string param_1) with a spacer and
// its own char event list.
void arcsel_add_char_row(int param_1)
{
    get_string_by_id(param_1);
    hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_8061a418, 0);
    hud_get_last_created_item();
    arcsel_build_char_event_list();
    hud_get_last_created_item();
}

// -----------------------------------------------------------------------------
// arcsel_build_profile_list @ 0x802f629c
// Build the full profile selection list: 7 character rows, 4 weapon rows (each
// with its weapon event list), and 2 trailing string rows, then persist the
// current selections back to the slot flags.
void arcsel_build_profile_list(int param_1)
{
    int iVar1;
    int uVar2;
    double dVar3 = (double)FLOAT_8061a418;

    *(u32 *)((int)&DAT_8051a9ec + DAT_806119ec * 0x1e0) = 0;
    hud_set_item_padding_mode(dVar3, 1, 1, 1);
    iVar1 = arcsel_profile_is_multiplayer();
    if (iVar1 == 0) {
        hud_element_scroll_vertical((int)FLOAT_8061a514, 0);
    } else {
        hud_element_scroll_vertical((int)FLOAT_8061a484, 0);
    }
    hud_set_context_property(0x2100);
    hud_begin_clip_group();
    arcsel_add_char_row(0x1904);
    arcsel_add_char_row(0x1905);
    arcsel_add_char_row(0x190e);
    arcsel_add_char_row(0x18f9);
    arcsel_add_char_row(0x18fa);
    arcsel_add_char_row(0x18f8);
    arcsel_add_char_row(0x18fb);

    hud_add_list_item();
    uVar2 = (int)(intptr_t)hud_get_last_created_item();
    arcsel_build_weapon_event_list(uVar2, 0, 1);
    hud_add_list_item();
    uVar2 = (int)(intptr_t)hud_get_last_created_item();
    arcsel_build_weapon_event_list(uVar2, 0, 0);
    hud_add_list_item();
    uVar2 = (int)(intptr_t)hud_get_last_created_item();
    arcsel_build_weapon_event_list(uVar2, 0, 1);
    hud_add_list_item();
    uVar2 = (int)(intptr_t)hud_get_last_created_item();
    arcsel_build_weapon_event_list(uVar2, 0, 0);

    hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_8061a418, 0);
    iVar1 = get_string_by_id(0x1785);
    hud_event_add_string(iVar1);
    hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_8061a418, 0);
    iVar1 = get_string_by_id(0x177b);
    hud_event_add_string(iVar1);
    hud_end_clip_group();
    hud_element_apply_scroll();
    arcsel_save_all_selections(param_1);
}

// -----------------------------------------------------------------------------
// arcsel_profile_confirm_selection @ 0x802f6580
// Confirm handler: clear duplicate events on the last-changed item, then navigate
// to the next screen (DAT_80477664 target id).
void arcsel_profile_confirm_selection(int param_1)
{
    int iVar1 = (int)(intptr_t)hud_find_child_by_id(
        param_1, *(int *)((int)&DAT_8051a9ec + DAT_806119ec * 0x1e0));
    arcsel_clear_duplicate_events(param_1, *(int *)(iVar1 + 0xcc),
                                  *(int *)((int)&DAT_8051a9ec + DAT_806119ec * 0x1e0));
    hud_screen_show_by_id(*(int *)(param_1 + 0x2c), (int)&DAT_80477664);
}

// -----------------------------------------------------------------------------
// arcsel_open_profile_screen @ 0x802f65f4
// Top-level open handler: store the control-map field, build the ticker layout
// (per-player block or record variant), init the screen layout with the confirm
// callback, build the selection list, apply centering/scroll, register handlers.
void arcsel_open_profile_screen(int param_1)
{
    int iVar2;
    bool record = DAT_80612614 >= 0x65;

    *(int *)((int)&DAT_8051a9f0 + DAT_806119ec * 0x1e0) = param_1;
    if (!record) {
        iVar2 = arcsel_profile_is_multiplayer();
        ticker_build_layout((u64)(u32)(int)(double)FLOAT_8061a418, (double)FLOAT_8061a508,
                            (&DAT_805cf280)[DAT_806119ec] + 0x268, 0, 0, iVar2);
    } else {
        ticker_build_layout((u64)(u32)(int)(double)FLOAT_8061a518, (double)FLOAT_8061a490,
                            (int)&DAT_805d03fc, 0, 1, 0);
    }
    iVar2 = get_string_by_id(0x1749);
    hud_element_init_from_layout(0, (void *)(intptr_t)iVar2, 0,
                                 (int)(intptr_t)&arcsel_profile_confirm_selection);
    hud_element_clear_dirty_flag(param_1);
    arcsel_build_profile_list(param_1);
    if (DAT_80612614 < 0x65) {
        iVar2 = session_get_state_flag();
        if (iVar2 < 2) {
            hud_element_apply_center_xy((int *)(intptr_t)DAT_806119e0);
            *(float *)(param_1 + 0x60) = *(float *)(param_1 + 0x60) - FLOAT_8061a524;
        } else {
            arcsel_element_tick_scroll(param_1);
            *(float *)(param_1 + 0x60) = *(float *)(param_1 + 0x60) - FLOAT_8061a520;
        }
    } else {
        hud_element_apply_center_xy((int *)(intptr_t)DAT_806119e0);
    }
    iVar2 = get_string_by_id(0x19c7);
    hud_screen_register_handler_global(0, iVar2, 1, 0);
    iVar2 = get_string_by_id(0x19c8);
    hud_screen_register_handler_global(1, iVar2, 0, 0);
}

// -----------------------------------------------------------------------------
// arcsel_profile_tick @ 0x802f675c
// Per-frame update wrapper -> selection-changed handler.
void arcsel_profile_tick(int param_1)
{
    arcsel_profile_selection_changed(param_1);
}

// -----------------------------------------------------------------------------
// arcsel_profile_input_tick @ 0x802f677c
// Input tick: decays the L/R repeat timers on the per-screen scroll state block,
// tracks the focused index, and (multiplayer, non-root) cycles the focused weapon
// list index left/right through the category boundaries (6/10/0xc/0xe) on L/R.
void arcsel_profile_input_tick(int param_1)
{
    int iVar1;
    uint uVar2;
    unsigned char *puVar3;

    if ((int)(((int)(DAT_80612614 ^ 100) >> 1) - ((DAT_80612614 ^ 100) & DAT_80612614)) < 0) {
        puVar3 = &DAT_805d03fc;
    } else {
        puVar3 = (unsigned char *)((&DAT_805cf280)[DAT_806119ec] + 0x268);
    }
    if (puVar3[0xe8] != '\0') { puVar3[0xe8] = puVar3[0xe8] + -1; }
    if (puVar3[0xe9] != '\0') { puVar3[0xe9] = puVar3[0xe9] + -1; }
    if (-1 < *(int *)(param_1 + 0x188)) {
        DAT_80612bbc = *(int *)(param_1 + 0x188);
    }
    arcsel_profile_tick(param_1);
    iVar1 = arcsel_profile_is_multiplayer();
    if ((iVar1 != 0) &&
        (iVar1 = hud_screen_find_root(*(int *)((int)&DAT_8051a820 + DAT_806119ec * 0x1e0)),
         iVar1 != param_1) && (puVar3[0xea] != '\0')) {
        uVar2 = input_get_buttons_held(*(int *)(param_1 + 0x38));
        if ((uVar2 & 0x200) == 0) {
            uVar2 = input_get_buttons_held(*(int *)(param_1 + 0x38));
            if ((uVar2 & 0x400) != 0) {
                if (DAT_80612bbc < 6)          { *(u32 *)(param_1 + 0x188) = 6; }
                else if (DAT_80612bbc < 10)    { *(u32 *)(param_1 + 0x188) = 10; }
                else if (DAT_80612bbc < 0xc)   { *(u32 *)(param_1 + 0x188) = 0xc; }
                else if (DAT_80612bbc < 0xe)   { *(u32 *)(param_1 + 0x188) = 0xe; }
                else                           { *(u32 *)(param_1 + 0x188) = 3; }
                DAT_80612bbc = *(int *)(param_1 + 0x188);
                puVar3[0xe9] = 8;
            }
        } else {
            if (DAT_80612bbc < 6)        { *(u32 *)(param_1 + 0x188) = 0xe; }
            else if (DAT_80612bbc < 10)  { *(u32 *)(param_1 + 0x188) = 3; }
            else if (DAT_80612bbc < 0xe) { *(u32 *)(param_1 + 0x188) = 10; }
            else                         { *(u32 *)(param_1 + 0x188) = 0xc; }
            DAT_80612bbc = *(int *)(param_1 + 0x188);
            puVar3[0xe8] = 8;
        }
    }
}

// =============================================================================
// Batch: batch_850 | Module: arcade-select profile / settings screen
// Address range: 0x802f695c - 0x802f76e8
//
// Continuation of the arcade "select profile" screen. This block covers the
// per-row render path (arcsel_render_profile_row draws either the single-player
// centre logo or, in multiplayer, the left/right profile selector arrows, then
// dispatches the ticker), the profile-options HUD list builder, the settings
// control screen (widget<->flag-word marshalling in
// arcsel_load_settings_to_widgets / arcsel_store_widgets_to_settings), and the
// small apply/pop transition handlers.
//
// All named cross-module helpers (text_*, hud_*, sprite_*, select_list_*,
// ticker_render_dispatch, get_string_by_id, arcsel_profile_is_multiplayer,
// arcsel_draw_center_logo, arcsel_profile_slot_state, arcsel_profile_toggle_locks,
// player_apply_model_def, leaf_31e8, arcsel_profile_advance,
// arcsel_commit_profile_screen) resolve through the shared generated headers.
// Only the raw FUN_/DAT_/FLOAT_ symbols new to this batch get per-file externs.

extern "C" {
int  FUN_803826e8(void);            // hud active-screen begin / root element ptr
void FUN_80382734(void);            // hud screen end/flush (font-table restore path)

extern int   DAT_8051a980;   // profile block: control-map field value (0x1e0 stride)
extern int   DAT_8051a98c;   // profile block: primary widget-1 stored value (0x1e0 stride)
extern int   DAT_80403050;   // gamemode label string-id table (5 entries)

extern float FLOAT_8061a4b0;
extern float FLOAT_8061a4b4;
extern float FLOAT_8061a4f4;
extern float FLOAT_8061a500;
extern float FLOAT_8061a528;
extern float FLOAT_8061a52c;
extern float FLOAT_8061a530;
extern float FLOAT_8061a534;
extern float FLOAT_8061a538;
extern float FLOAT_8061a548;
extern float FLOAT_8061a54c;

// HUD screen-element callback pointer tables (created via hud_element_create)
extern void *PTR_arcsel_open_profile_screen_80477654;
extern void *PTR_FUN_80477664;
}

// forward decls for this batch's own handlers referenced before definition
void arcsel_draw_mp_profile_selector(u64 param_1, double param_2, double param_3, double param_4);

// -----------------------------------------------------------------------------
// arcsel_draw_mp_profile_selector @ 0x802f695c
// Draws the multiplayer profile "< prev / next >" selector: resolves the active
// ticker state block, computes the weighted highlight tint for the two adjacent
// entries, draws the left/right arrow glyphs and the "<" / ">" prompt characters.
void arcsel_draw_mp_profile_selector(u64 param_1, double param_2, double param_3, double param_4)
{
    int iVar1;
    uint uVar2;
    int iVar3;
    uint uVar4;
    byte *pbVar5;
    int iVar6;
    int iVar7;
    unsigned char *puVar8;
    double dVar9;
    double dVar10;

    iVar1 = FUN_803826e8();
    if ((int)(((int)(DAT_80612614 ^ 100) >> 1) - ((DAT_80612614 ^ 100) & DAT_80612614)) < 0) {
        puVar8 = &DAT_805d03fc;
    } else {
        puVar8 = (unsigned char *)((&DAT_805cf280)[DAT_806119ec] + 0x268);
    }
    iVar7 = 3;
    iVar6 = 3;
    dVar9 = 0.0;
    uVar2 = FUN_803825c0((double)(float)((double)FLOAT_8061a480 * param_4));
    dVar9 = (double)(float)(dVar9 + (double)FLOAT_8061a4b0);
    uVar2 = (uVar2 & 0xff) | 0x7f7f7f00;
    dVar10 = (double)(float)(param_3 - (double)FLOAT_8061a4b0);
    text_set_font_table(0x15);
    iVar3 = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    if (iVar3 == iVar1) {
        uVar2 = FUN_803825c0((double)(float)((double)FLOAT_8061a50c * param_4));
        uVar2 = (uVar2 & 0xff) | 0x40404000;
    } else if (puVar8[0xe8] == '\0') {
        iVar7 = 1;
        if (puVar8[0xe9] == '\0') {
            iVar6 = 1;
        } else {
            iVar6 = 2;
        }
    } else {
        iVar7 = 2;
        iVar6 = 1;
    }
    uVar4 = select_list_get_weight(*(int *)(iVar1 + 0x174), iVar7);
    color_scale_lo(param_4, uVar4);
    uVar4 = select_list_get_weight(*(int *)(iVar1 + 0x174), iVar6);
    color_scale_lo(param_4, uVar4);
    dVar9 = (double)(float)(dVar9 - (double)FLOAT_8061a528);
    dVar10 = (double)(float)(dVar10 + (double)FLOAT_8061a528);
    sprite_draw_arrow_left(
        dVar9, param_2, (double)(float)(dVar9 + (double)(FLOAT_8061a52c * FLOAT_8060f958)),
        (double)(float)(param_2 + (double)(FLOAT_8061a52c * FLOAT_8060f958)));
    sprite_draw_arrow_right(
        (double)(float)(dVar10 - (double)(FLOAT_8061a52c * FLOAT_8060f958)), param_2, dVar10,
        (double)(float)(param_2 + (double)(FLOAT_8061a52c * FLOAT_8060f958)));
    pbVar5 = (byte *)get_string_by_id(0x1904);
    text_draw_char((double)(FLOAT_8061a4b4 +
                            (float)((double)FLOAT_8061a52c * (double)FLOAT_8060f958 + dVar9)),
                   param_2, pbVar5, uVar2);
    pbVar5 = (byte *)get_string_by_id(0x1905);
    text_draw_char((double)(-(float)((double)FLOAT_8061a52c * (double)FLOAT_8060f958 - dVar10) -
                            FLOAT_8061a530),
                   param_2, pbVar5, uVar2);
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// arcsel_render_profile_row @ 0x802f6ba0
// Per-frame render for one arcade-select profile row. In single-player (or when
// DAT_80612614 >= 0x65) it draws the centre logo; in multiplayer it draws the
// profile selector arrows. Always dispatches the ticker for the row, choosing the
// per-player ticker state block or the shared default (-0x7fa2fc04).
void arcsel_render_profile_row(int param_1)
{
    int iVar1;
    double dVar2;
    double dVar3;
    double dVar4;
    double dVar5;
    double dVar6;
    double dVar7;

    dVar7 = (double)*(float *)(param_1 + 0xa4);
    dVar2 = (double)(FLOAT_8061a534 * *(float *)(param_1 + 0xb0) + *(float *)(param_1 + 0xa8));
    dVar6 = (double)(float)((double)FLOAT_8061a500 * (double)*(float *)(param_1 + 0xac) + dVar7);
    dVar4 = (double)(float)((double)FLOAT_8061a444 * (double)*(float *)(param_1 + 0xac) + dVar7);
    dVar3 = (double)(float)(dVar2 - (double)FLOAT_8061a490);
    dVar5 = dVar2;
    if (DAT_80612614 < 0x65) {
        iVar1 = arcsel_profile_is_multiplayer();
        if (iVar1 == 0) {
            arcsel_draw_center_logo(dVar6, dVar5, (double)(FLOAT_8061a4f4 * *(float *)(param_1 + 0xf0)));
        } else {
            dVar3 = (double)(float)(dVar3 - (double)(FLOAT_8061a538 * FLOAT_80611a64));
            dVar2 = (double)(float)(dVar2 - (double)(FLOAT_8061a538 * FLOAT_80611a64));
            arcsel_draw_mp_profile_selector(
                (u64)dVar7, dVar3, (double)(float)(dVar7 + (double)*(float *)(param_1 + 0xac)),
                (double)*(float *)(param_1 + 0xf0));
        }
    } else {
        arcsel_draw_center_logo(dVar6, dVar2, (double)*(float *)(param_1 + 0xf0));
    }
    iVar1 = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    if (iVar1 == param_1) {
        if (DAT_80612614 < 0x65) {
            ticker_render_dispatch(dVar6, dVar5, dVar4, dVar2, dVar3,
                                   (&DAT_805cf280)[DAT_806119ec] + 0x268, param_1, 1, 0);
        } else {
            ticker_render_dispatch(dVar6, dVar5, dVar4, dVar2, dVar3, -0x7fa2fc04, param_1, 1, 0);
        }
    } else if (DAT_80612614 < 0x65) {
        ticker_render_dispatch(dVar6, dVar5, dVar4, dVar2, dVar3,
                               (&DAT_805cf280)[DAT_806119ec] + 0x268, param_1, 0, 0);
    } else {
        ticker_render_dispatch(dVar6, dVar5, dVar4, dVar2, dVar3, -0x7fa2fc04, param_1, 0, 0);
    }
}

// -----------------------------------------------------------------------------
// arcsel_show_profile_subscreen @ 0x802f6dec
// Resets the per-block header short and shows the child profile screen stored at
// (param_1 + 0x2c).
void arcsel_show_profile_subscreen(int param_1)
{
    *(unsigned short *)(&DAT_8051a918 + DAT_806119ec * 0x1e0) = 0;
    hud_item_on_value_change();
    hud_screen_show_by_id(*(int *)(param_1 + 0x2c), -0x7fb889ac);
}

// -----------------------------------------------------------------------------
// arcsel_build_profile_options_list @ 0x802f6e4c
// Builds the profile options HUD list. Single-player and multiplayer variants
// differ in layout id and locking; both add a 5-entry gamemode cycle item
// (strings from DAT_80403050) plus a trailing toggle item.
void arcsel_build_profile_options_list(void)
{
    float fVar1;
    int iVar2;
    int iVar3;
    int iVar4;
    int iVar5;

    iVar2 = (int)__save_gpr_28();
    iVar3 = arcsel_profile_is_multiplayer();
    if (iVar3 == 0) {
        iVar3 = get_string_by_id(0x1749);
        hud_element_init_from_layout(0x38000, (void *)iVar3, 0, (int)(size_t)&arcsel_profile_advance);
        hud_set_item_padding_mode((double)FLOAT_8061a418, 1, 1, 1);
        hud_add_list_item();
        hud_item_add_spacer((double)FLOAT_8061a418, 0x30200);
        iVar3 = 0;
        iVar5 = 0;
        do {
            iVar4 = get_string_by_id(*(int *)((int)&DAT_80403050 + iVar5));
            hud_event_add_string(iVar4);
            iVar3 = iVar3 + 1;
            iVar5 = iVar5 + 4;
        } while (iVar3 < 5);
        iVar3 = (int)hud_get_last_created_item();
        hud_item_set_change_callback(iVar3, (void *)&arcsel_commit_profile_screen);
        hud_get_last_created_item();
        hud_item_on_value_change();
        get_string_by_id(0x175e);
        hud_add_list_item();
        fVar1 = FLOAT_8061a538;
        *(float *)(iVar2 + 0x5c) = -(FLOAT_8061a444 * *(float *)(iVar2 + 0x4c) - FLOAT_8061a548);
        *(float *)(iVar2 + 0x60) = fVar1;
    } else {
        iVar3 = arcsel_profile_slot_state(*(int *)(iVar2 + 0x38));
        iVar3 = get_string_by_id(0x1749);
        hud_element_init_from_layout(0x39000, (void *)iVar3, 0, (int)(size_t)&arcsel_profile_advance);
        hud_set_item_padding_mode((double)FLOAT_8061a418, 1, 1, 1);
        hud_add_list_item();
        hud_item_add_spacer((double)FLOAT_8061a418, 0x32200);
        iVar3 = 0;
        iVar5 = 0;
        do {
            iVar4 = get_string_by_id(*(int *)((int)&DAT_80403050 + iVar5));
            hud_event_add_string(iVar4);
            iVar3 = iVar3 + 1;
            iVar5 = iVar5 + 4;
        } while (iVar3 < 5);
        iVar3 = (int)hud_get_last_created_item();
        hud_item_set_change_callback(iVar3, (void *)&arcsel_commit_profile_screen);
        hud_get_last_created_item();
        hud_item_on_value_change();
        get_string_by_id(0x175e);
        hud_add_list_item();
        arcsel_profile_toggle_locks();
        hud_element_apply_center_x(iVar2);
        *(float *)(iVar2 + 0x60) =
            FLOAT_8061a41c + *(float *)(*(int *)(&DAT_8051a9f0 + DAT_806119ec * 0x1e0) + 0x60);
    }
    __restore_gpr_28();
}

// -----------------------------------------------------------------------------
// arcsel_create_profile_control_screen @ 0x802f7174
// Creates the profile control screen elements (open-profile handler + secondary
// pointer-table handler), shows it, clears the dirty flag and finalizes the frame.
void arcsel_create_profile_control_screen(int param_1)
{
    hud_element_init_from_layout(0x40000000, 0, 0, 0);
    hud_element_create(param_1, (u32)(size_t)&PTR_arcsel_open_profile_screen_80477654, 2);
    hud_element_create(param_1, (u32)(size_t)&PTR_FUN_80477664, 2);
    hud_screen_show_by_id(param_1, -0x7fb8899c);
    DAT_80612bc4 = 0;
    hud_element_finalize_frame();
    *(float *)(param_1 + 0x20c) = FLOAT_8061a54c;
}

// -----------------------------------------------------------------------------
// arcsel_load_settings_to_widgets @ 0x802f7200
// Unpacks the stored profile settings flag word (DAT_8051a91a) into the HUD child
// widget values (ids 1-8): individual bit flags plus a 2-bit team/handicap mode
// packed in bits 5-6 that maps to widget-8 states 0..3.
void arcsel_load_settings_to_widgets(void)
{
    unsigned short uVar1;
    int iVar2;
    int iVar3;
    int uVar4;

    iVar2 = FUN_803826f0();
    uVar1 = *(unsigned short *)(&DAT_8051a91a + DAT_806119ec * 0x1e0);
    uVar4 = *(int *)(&DAT_8051a98c + DAT_806119ec * 0x1e0);
    iVar3 = (int)hud_find_child_by_id(iVar2, 1);
    *(int *)(iVar3 + 200) = uVar4;
    iVar3 = (int)hud_find_child_by_id(iVar2, 5);
    *(uint *)(iVar3 + 200) = (uint)(uVar1 >> 1 & 1) ^ 1;
    iVar3 = (int)hud_find_child_by_id(iVar2, 7);
    *(uint *)(iVar3 + 200) = (uint)(uVar1 >> 2 & 1) ^ 1;
    iVar3 = (int)hud_find_child_by_id(iVar2, 4);
    *(uint *)(iVar3 + 200) = (uint)(uVar1 >> 3 & 1);
    iVar3 = (int)hud_find_child_by_id(iVar2, 3);
    *(uint *)(iVar3 + 200) = (uint)(uVar1 & 1);
    iVar3 = (int)hud_find_child_by_id(iVar2, 2);
    *(uint *)(iVar3 + 200) = (uint)(uVar1 >> 7 & 1);
    uVar4 = *(int *)(&DAT_8051a980 + DAT_806119ec * 0x1e0);
    iVar3 = (int)hud_find_child_by_id(iVar2, 6);
    *(int *)(iVar3 + 200) = uVar4;
    if ((uVar1 & 0x10) == 0) {
        iVar2 = (int)hud_find_child_by_id(iVar2, 8);
        *(int *)(iVar2 + 200) = 0;
    } else {
        uVar1 = uVar1 & 0x60;
        if (uVar1 == 0x40) {
            iVar2 = (int)hud_find_child_by_id(iVar2, 8);
            *(int *)(iVar2 + 200) = 1;
        } else if (uVar1 < 0x40) {
            if (uVar1 == 0x20) {
                iVar2 = (int)hud_find_child_by_id(iVar2, 8);
                *(int *)(iVar2 + 200) = 2;
            }
        } else if (uVar1 == 0x60) {
            iVar2 = (int)hud_find_child_by_id(iVar2, 8);
            *(int *)(iVar2 + 200) = 3;
        }
    }
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// arcsel_store_widgets_to_settings @ 0x802f7378
// Inverse of arcsel_load_settings_to_widgets: reads the HUD child widget values
// (ids 1-8) and repacks them into the profile settings flag word (DAT_8051a91a),
// including the 2-bit team/handicap mode packed into bits 5-6.
void arcsel_store_widgets_to_settings(int param_1)
{
    int iVar1;
    int iVar2;
    unsigned short *pFlags = (unsigned short *)(&DAT_8051a91a + DAT_806119ec * 0x1e0);

    *pFlags = *pFlags & 0xfffd;
    iVar1 = (int)hud_find_child_by_id(param_1, 5);
    if (*(int *)(iVar1 + 200) == 0) {
        *pFlags = *pFlags | 2;
    }
    *pFlags = *pFlags & 0xfffb;
    iVar1 = (int)hud_find_child_by_id(param_1, 7);
    if (*(int *)(iVar1 + 200) == 0) {
        *pFlags = *pFlags | 4;
    }
    *pFlags = *pFlags & 0xfff7;
    iVar1 = (int)hud_find_child_by_id(param_1, 4);
    if (*(int *)(iVar1 + 200) == 1) {
        *pFlags = *pFlags | 8;
    }
    *pFlags = *pFlags & 0xfffe;
    iVar1 = (int)hud_find_child_by_id(param_1, 3);
    if (*(int *)(iVar1 + 200) == 1) {
        *pFlags = *pFlags | 1;
    }
    *pFlags = *pFlags & 0xff7f;
    iVar1 = (int)hud_find_child_by_id(param_1, 2);
    if (*(int *)(iVar1 + 200) != 0) {
        *pFlags = *pFlags | 0x80;
    }
    iVar1 = (int)hud_find_child_by_id(param_1, 6);
    iVar2 = DAT_806119ec * 0x1e0;
    *(int *)(&DAT_8051a980 + iVar2) = *(int *)(iVar1 + 200);
    *(unsigned short *)(&DAT_8051a91a + iVar2) = *(unsigned short *)(&DAT_8051a91a + iVar2) & 0xff9f;
    iVar1 = (int)hud_find_child_by_id(param_1, 8);
    if (*(int *)(iVar1 + 200) == 0) {
        *pFlags = *pFlags & 0xffef;
    } else {
        *pFlags = *pFlags | 0x10;
        iVar1 = (int)hud_find_child_by_id(param_1, 8);
        iVar1 = *(int *)(iVar1 + 200);
        if (iVar1 == 2) {
            *pFlags = *pFlags | 0x20;
        } else if (iVar1 < 2) {
            if (0 < iVar1) {
                *pFlags = *pFlags | 0x40;
            }
        } else if (iVar1 < 4) {
            *pFlags = *pFlags | 0x60;
        }
    }
    iVar1 = (int)hud_find_child_by_id(param_1, 1);
    *(int *)(&DAT_8051a98c + DAT_806119ec * 0x1e0) = *(int *)(iVar1 + 200);
}

// -----------------------------------------------------------------------------
// arcsel_apply_and_pop_two_screens @ 0x802f7608
// Copies the 0x7c-byte profile block into the player's savegame slot, applies the
// player model def, and pops two HUD screens (confirm path).
void arcsel_apply_and_pop_two_screens(void)
{
    leaf_31e8(*(uint *)(DAT_80611d74 + 0x1398), (uint)(&DAT_8051a918 + DAT_806119ec * 0x1e0), 0x7c);
    player_apply_model_def(DAT_80611d74);
    hud_screen_pop_with_restore();
    hud_screen_pop_with_restore();
}

// -----------------------------------------------------------------------------
// arcsel_apply_and_pop_screen @ 0x802f7684
// As arcsel_apply_and_pop_two_screens but pops a single HUD screen.
void arcsel_apply_and_pop_screen(void)
{
    leaf_31e8(*(uint *)(DAT_80611d74 + 0x1398), (uint)(&DAT_8051a918 + DAT_806119ec * 0x1e0), 0x7c);
    player_apply_model_def(DAT_80611d74);
    hud_screen_pop_with_restore();
}

// -----------------------------------------------------------------------------
// arcsel_pop_screen @ 0x802f76e8
// Cancel/back handler: pops one HUD screen with restore.
void arcsel_pop_screen(void)
{
    hud_screen_pop_with_restore();
}

// =============================================================================
// Batch: batch_851 | Module: arcade-select profile / settings screen
// Address range: 0x802f771c - 0x802f8494
//
// Continuation of the arcade "select profile" screen: the settings list itself.
// arcsel_build_settings_list constructs the full profile options list (gamemode,
// bots, handicap, teams, limits...), arcsel_build_settings_screen is the screen
// builder that picks a layout, populates the list and registers the global
// handlers. arcsel_settings_item_changed marshals a changed widget back into the
// settings and (for the slider item 9) nudges the stored slider value and plays a
// tick sound. arcsel_draw_setting_slider renders a row's progress bar. The
// back/enter/save handlers (arcsel_profile_back_handler / _enter_handler /
// _build_save_prompt_list) drive screen transitions, and
// arcsel_profile_block_differs / arcsel_restore_profile_block manage the
// working-vs-saved profile block.
//
// Named helpers resolve through the shared generated headers; only raw
// FUN_/DAT_/FLOAT_ symbols new to this batch get per-file externs.

extern "C" {
int  FUN_80387b68(int, int, int);   // block memcmp (returns nonzero if differ)

extern int   DAT_8060f948;   // game-type / mode discriminator
extern int   DAT_8051a990;   // profile block: slider value field (0x1e0 stride)
extern int   DAT_80611a54;   // sound-enabled flag
extern int   DAT_80611a58;   // sound descriptor table base

extern float FLOAT_8061a550;
extern float FLOAT_8061a554;
extern float FLOAT_8061a560;
extern float FLOAT_8061a564;
extern float FLOAT_8061a568;
extern float FLOAT_8061a56c;
extern float FLOAT_8061a570;
}

// (forward decls for arcsel_profile_back_handler / arcsel_build_settings_list /
//  arcsel_profile_block_differs come from the shared generated_decls.h)

// -----------------------------------------------------------------------------
// arcsel_restore_profile_block @ 0x802f771c
// Copies the saved profile block back into the working profile block (reverse of
// the apply/pop handlers) so a cancel restores the previous settings.
void arcsel_restore_profile_block(void)
{
    leaf_31e8((uint)(&DAT_8051a918 + DAT_806119ec * 0x1e0), *(uint *)(DAT_80611d74 + 0x1398), 0x7c);
}

// -----------------------------------------------------------------------------
// arcsel_build_save_prompt_list @ 0x802f7760
// Builds the small save/discard confirm HUD list shown when leaving the profile
// screen with unsaved changes.
void arcsel_build_save_prompt_list(int param_1)
{
    int iVar1;

    iVar1 = get_string_by_id(0x1749);
    hud_element_init_from_layout(0, (void *)iVar1, 0, 0);
    hud_set_item_padding_mode((double)FLOAT_8061a494, 1, 1, 1);
    get_string_by_id(0x1790);
    hud_add_list_item();
    hud_add_default_callback_item();
    get_string_by_id(0x173e);
    hud_add_list_item();
    get_string_by_id(0x173f);
    hud_add_list_item();
    hud_set_focus_by_id(param_1, 1);
    hud_element_apply_center_xy((int *)DAT_806119e0);
    hud_swap_callbacks();
    iVar1 = get_string_by_id(0x19c7);
    hud_screen_register_handler_global(0, iVar1, 0, 0);
}

// -----------------------------------------------------------------------------
// arcsel_profile_block_differs @ 0x802f78c8
// Returns true if the working profile block differs from the saved copy.
bool arcsel_profile_block_differs(void)
{
    int iVar1;

    iVar1 = FUN_80387b68(*(int *)(DAT_80611d74 + 0x1398),
                         (int)(&DAT_8051a918 + DAT_806119ec * 0x1e0), 0x7c);
    return (bool)('\x01' - (iVar1 == 0));
}

// -----------------------------------------------------------------------------
// arcsel_profile_back_handler @ 0x802f7914
// Back/cancel handler. In the arcade path it pushes/pops based on whether the
// block changed; in the menu path it checks profile/savegame changes and either
// re-enters, pops, or begins a profile edit.
void arcsel_profile_back_handler(void)
{
    bool bVar2;
    int iVar1;

    if (DAT_80612614 < 0x65) {
        bVar2 = arcsel_profile_block_differs();
        if (bVar2) {
            hud_screen_push();
        } else {
            hud_screen_pop_with_restore();
            hud_screen_push();
        }
    } else {
        iVar1 = menu_profile_settings_changed();
        if ((iVar1 != 0) || (bVar2 = menu_results_savegame_changed(), bVar2)) {
            hud_awards_get_saved_focus();
            hud_screen_push();
        } else if (DAT_80612bc4 == 0) {
            hud_awards_get_saved_focus();
            hud_screen_pop_with_restore();
        } else {
            menu_profile_edit_begin();
        }
    }
}

// -----------------------------------------------------------------------------
// arcsel_profile_enter_handler @ 0x802f79f4
// Enter/advance handler: reset profile state and push the screen (arcade), or
// build the profile options screen (menu path).
void arcsel_profile_enter_handler(void)
{
    if (DAT_80612614 < 0x65) {
        arcsel_reset_profile_state();
        hud_screen_push();
    } else {
        menu_profile_options_screen_build();
    }
}

// -----------------------------------------------------------------------------
// arcsel_draw_setting_slider @ 0x802f7a54
// Renders the progress/slider bar for a setting row. The focused row's alpha
// oscillates via arcsel_wind_oscillate. Bar width is clamped to FLOAT_8061a554.
// NON-MATCHING: sprite_draw_progress_bar uses the 3-arg (width,height,direction)
// project contract; the decompiler's leading x/y operands fold into the sprite
// render stack.
void arcsel_draw_setting_slider(u32 param_1, u32 param_2, float *param_3)
{
    float fVar1;
    int iVar2;
    int iVar3;
    double dVar4;
    double dVar5;
    u64 uVar6;

    uVar6 = FUN_803826e8();
    iVar2 = (int)((ulonglong)uVar6 >> 0x20);
    iVar3 = (int)uVar6;
    if (*(int *)(iVar2 + 0x188) == *(int *)(iVar3 + 0x14)) {
        dVar4 = arcsel_wind_oscillate((double)*(float *)(iVar2 + 0xf0));
    } else {
        dVar4 = (double)*(float *)(iVar2 + 0xf0);
    }
    color_scale_lo(dVar4, 0x11193c7f);
    color_scale_lo(dVar4, 0x11193c7f);
    color_scale_lo(dVar4, 0x69737f7f);
    dVar4 = text_get_char_height(*(byte **)(iVar3 + 0x20));
    text_set_font_table(*(int *)(iVar3 + 0x1c));
    dVar5 = (double)text_get_char_width(*(byte **)(iVar3 + 0x20));
    fVar1 = (float)((double)(param_3[2] - *param_3) - dVar5) - FLOAT_8061a550;
    if (FLOAT_8061a554 < fVar1) {
        fVar1 = FLOAT_8061a554;
    }
    sprite_draw_progress_bar((u32)(int)fVar1, (int)dVar4, 0);
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// arcsel_build_settings_list @ 0x802f7bd4
// Builds the full profile settings HUD list: gamemode label, bots, handicap,
// teams, time/score limits and related toggles. Layout ids/spacer flags differ
// between single-player and multiplayer; item field 0x1c/0xbc receive the shared
// per-item value uVar2.
void arcsel_build_settings_list(void)
{
    uint uVar1;
    int uVar2;
    int iVar3;
    int iVar4;
    int iVar5;
    double dVar6;

    iVar3 = FUN_803826e4();
    iVar3 = arcsel_profile_slot_state(*(int *)(iVar3 + 0x38));
    iVar4 = arcsel_profile_is_multiplayer();
    uVar1 = 0x400;
    if (iVar4 == 0) {
        uVar2 = 3;
    } else {
        uVar1 = 0x100;
        uVar2 = 0x12;
        if (iVar3 != 0) {
            uVar2 = 0xf;
        }
    }
    if (iVar4 == 0) {
        hud_set_context_property(0x2000);
        hud_set_item_padding_mode((double)FLOAT_8061a41c, 1, 1, 1);
        get_string_by_id(0x1748);
        hud_add_list_item();
        iVar4 = (int)hud_get_last_created_item();
    } else {
        get_string_by_id(0x1748);
        hud_add_list_item();
        iVar4 = (int)hud_get_last_created_item();
        hud_set_item_padding_mode((double)FLOAT_8061a418, 1, 1, 1);
    }
    hud_item_add_spacer((double)FLOAT_8061a418, uVar1 | 0x400000);
    iVar5 = get_string_by_id((&DAT_80403050)[*(ushort *)(&DAT_8051a918 + DAT_806119ec * 0x1e0)]);
    hud_event_add_string(iVar5);
    *(int *)(iVar4 + 0x1c) = uVar2;
    *(int *)(iVar4 + 0xbc) = uVar2;
    get_string_by_id(0x17ae);
    hud_add_list_item();
    iVar4 = (int)hud_get_last_created_item();
    dVar6 = (double)FLOAT_8061a418;
    *(int *)(iVar4 + 0x1c) = uVar2;
    hud_item_add_spacer(dVar6, uVar1);
    iVar5 = get_string_by_id(0x17af);
    hud_event_add_string(iVar5);
    iVar5 = get_string_by_id(0x17b0);
    hud_event_add_string(iVar5);
    iVar5 = get_string_by_id(0x17b1);
    hud_event_add_string(iVar5);
    *(int *)(iVar4 + 0xbc) = uVar2;
    get_string_by_id(0x179b);
    hud_add_list_item();
    iVar4 = (int)hud_get_last_created_item();
    dVar6 = (double)FLOAT_8061a418;
    *(int *)(iVar4 + 0x1c) = uVar2;
    hud_item_add_spacer(dVar6, uVar1);
    hud_event_add_ok_cancel();
    *(int *)(iVar4 + 0xbc) = uVar2;
    get_string_by_id(0x17a7);
    hud_add_list_item();
    iVar4 = (int)hud_get_last_created_item();
    dVar6 = (double)FLOAT_8061a418;
    *(int *)(iVar4 + 0x1c) = uVar2;
    hud_item_add_spacer(dVar6, uVar1);
    hud_event_add_ok_cancel();
    *(int *)(iVar4 + 0xbc) = uVar2;
    get_string_by_id(0x179e);
    hud_add_list_item();
    iVar4 = (int)hud_get_last_created_item();
    dVar6 = (double)FLOAT_8061a418;
    *(int *)(iVar4 + 0x1c) = uVar2;
    hud_item_add_spacer(dVar6, uVar1);
    hud_event_add_ok_cancel();
    *(int *)(iVar4 + 0xbc) = uVar2;
    get_string_by_id(0x179c);
    hud_add_list_item();
    iVar4 = (int)hud_get_last_created_item();
    hud_item_add_spacer((double)FLOAT_8061a418, uVar1);
    iVar5 = get_string_by_id(0x1792);
    hud_event_add_string(iVar5);
    iVar5 = get_string_by_id(0x1793);
    hud_event_add_string(iVar5);
    *(int *)(iVar4 + 0xbc) = uVar2;
    *(int *)(iVar4 + 0x1c) = uVar2;
    get_string_by_id(0x1774);
    hud_add_list_item();
    iVar4 = (int)hud_get_last_created_item();
    dVar6 = (double)FLOAT_8061a418;
    *(int *)(iVar4 + 0x1c) = uVar2;
    hud_item_add_spacer(dVar6, uVar1);
    iVar5 = get_string_by_id(0x1792);
    hud_event_add_string(iVar5);
    iVar5 = get_string_by_id(0x1793);
    hud_event_add_string(iVar5);
    *(int *)(iVar4 + 0xbc) = uVar2;
    get_string_by_id(0x17a8);
    hud_add_list_item();
    iVar4 = (int)hud_get_last_created_item();
    dVar6 = (double)FLOAT_8061a418;
    *(int *)(iVar4 + 0x1c) = uVar2;
    hud_item_add_spacer(dVar6, uVar1);
    iVar5 = get_string_by_id(0x179f);
    hud_event_add_string(iVar5);
    iVar5 = get_string_by_id(0x17a0);
    hud_event_add_string(iVar5);
    iVar5 = get_string_by_id(0x17a1);
    hud_event_add_string(iVar5);
    iVar5 = get_string_by_id(0x17a2);
    hud_event_add_string(iVar5);
    iVar5 = get_string_by_id(0x17a3);
    hud_event_add_string(iVar5);
    *(int *)(iVar4 + 0xbc) = uVar2;
    if ((iVar3 != 0) && (DAT_8060f948 != 3)) {
        iVar3 = (int)hud_get_last_created_item();
        *(int *)(iVar3 + 0xc0) = 4;
    }
    get_string_by_id(0x17ac);
    hud_add_list_item();
    iVar3 = (int)hud_get_last_created_item();
    dVar6 = (double)FLOAT_8061a418;
    *(int *)(iVar3 + 0x1c) = uVar2;
    hud_item_add_spacer(dVar6, uVar1);
    iVar4 = get_string_by_id(0x1a61);
    hud_event_add_string(iVar4);
    iVar4 = get_string_by_id(0x17a4);
    hud_event_add_string(iVar4);
    iVar4 = get_string_by_id(0x17a5);
    hud_event_add_string(iVar4);
    iVar4 = get_string_by_id(0x17a6);
    hud_event_add_string(iVar4);
    *(int *)(iVar3 + 0xbc) = uVar2;
    get_string_by_id(0x17b2);
    hud_add_list_item();
    iVar3 = (int)hud_get_last_created_item();
    *(int *)(iVar3 + 0x1c) = uVar2;
    *(int *)(iVar3 + 0xbc) = uVar2;
    hud_add_callback_item_default(1);
    if (100 < DAT_80612614) {
        hud_add_default_padding();
    }
    get_string_by_id(0x1aa7);
    hud_add_list_item();
    get_string_by_id(0x1aaa);
    hud_add_list_item();
    arcsel_load_settings_to_widgets();
    FUN_80382730();
}

// -----------------------------------------------------------------------------
// arcsel_build_settings_screen @ 0x802f80d4
// Top-level builder for the profile settings screen: chooses the layout by
// slot-state/mode, builds the list (arcsel_build_settings_list), applies scroll
// and positioning, and registers the ok/back global handlers.
void arcsel_build_settings_screen(void)
{
    int element;
    int iVar1;
    int iVar2;
    int iVar3;
    uint uVar4;

    element = FUN_803826f0();
    iVar1 = arcsel_profile_slot_state(*(int *)(element + 0x38));
    iVar2 = arcsel_profile_is_multiplayer();
    if (iVar1 == 0) {
        if (iVar2 == 0) {
            iVar3 = get_string_by_id(0x1749);
            hud_element_init_from_layout(0x2000, (void *)iVar3, 0, (int)(size_t)&arcsel_profile_back_handler);
        } else {
            iVar3 = get_string_by_id(0x1749);
            hud_element_init_from_layout(0x40002000, (void *)iVar3, 0, (int)(size_t)&arcsel_profile_back_handler);
        }
    } else {
        iVar3 = get_string_by_id(0x1749);
        hud_element_init_from_layout(0x40003000, (void *)iVar3, 0, (int)(size_t)&arcsel_profile_back_handler);
    }
    if (iVar2 != 0) {
        hud_element_scroll_horizontal((int)FLOAT_8061a560, 0);
    }
    arcsel_build_settings_list();
    if (iVar2 != 0) {
        hud_element_apply_scroll();
    }
    if (DAT_80612614 < 0x65) {
        arcsel_element_tick_scroll(element);
        iVar3 = session_get_state_flag();
        if ((1 < iVar3) && (uVar4 = arcsel_menu_column_count(*(int *)(element + 0x38)), uVar4 != 0)) {
            *(float *)(element + 0x60) = *(float *)(element + 0x60) + FLOAT_8061a488;
        }
    } else {
        hud_element_apply_center_xy((int *)DAT_806119e0);
    }
    iVar3 = get_string_by_id(0x19c7);
    hud_screen_register_handler_global(0, iVar3, 0, 0);
    iVar3 = get_string_by_id(0x19c8);
    hud_screen_register_handler_global(1, iVar3, 0, 0);
    if (iVar1 == 0) {
        if (iVar2 == 0) {
            *(float *)(element + 0x20c) = FLOAT_8061a568;
        } else {
            *(float *)(element + 0x20c) = FLOAT_8061a564;
        }
    } else {
        *(float *)(element + 0x20c) = FLOAT_8061a54c;
    }
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// arcsel_settings_item_changed @ 0x802f82c0
// Change handler for a settings list item. Stores widgets back into the profile,
// then for the slider item (0x188 == 9) nudges the stored slider value within
// [FLOAT_8061a418, FLOAT_8061a41c] and plays a tick sound; for other items it
// updates the ok/back handler prompt text and enable flag.
void arcsel_settings_item_changed(int param_1)
{
    float fVar1;
    float fVar2;
    float fVar3;
    int iVar4;
    double dVar5;
    int local_48[12];

    arcsel_store_widgets_to_settings(param_1);
    iVar4 = *(int *)(param_1 + 0x188);
    if (iVar4 == 9) {
        dVar5 = (double)*(float *)(&DAT_8051a990 + DAT_806119ec * 0x1e0);
        hud_screen_set_handler_param_b(param_1, 0, 1);
        fVar3 = FLOAT_8061a56c;
        fVar2 = FLOAT_8061a41c;
        fVar1 = FLOAT_8061a418;
        if ((*(uint *)(param_1 + 0xc0) & 0x40000) == 0) {
            if ((*(uint *)(param_1 + 0xc0) & 0x80000) != 0) {
                iVar4 = DAT_806119ec * 0x1e0;
                if ((*(float *)(&DAT_8051a990 + iVar4) < FLOAT_8061a41c) &&
                    (*(float *)(&DAT_8051a990 + iVar4) =
                         *(float *)(&DAT_8051a990 + iVar4) + FLOAT_8061a528,
                     fVar3 < *(float *)(&DAT_8051a990 + iVar4))) {
                    *(float *)(&DAT_8051a990 + iVar4) = fVar2;
                }
            }
        } else {
            iVar4 = DAT_806119ec * 0x1e0;
            if ((FLOAT_8061a418 < *(float *)(&DAT_8051a990 + iVar4)) &&
                (*(float *)(&DAT_8051a990 + iVar4) =
                     *(float *)(&DAT_8051a990 + iVar4) - FLOAT_8061a528,
                 *(float *)(&DAT_8051a990 + iVar4) < fVar1)) {
                *(float *)(&DAT_8051a990 + iVar4) = fVar1;
            }
        }
        if ((dVar5 != (double)*(float *)(&DAT_8051a990 + DAT_806119ec * 0x1e0)) &&
            (DAT_80611a54 != 0)) {
            iVar4 = *(int *)(DAT_80611a58 + 8);
            sound_init_play_params((int)local_48);
            local_48[0] = iVar4;
            sound_play_request(local_48);
        }
    } else if (iVar4 == 0) {
        iVar4 = get_string_by_id(0x1791);
        hud_screen_set_handler_param_a(param_1, 0, iVar4);
        hud_screen_set_handler_param_b(param_1, 0, 0);
    } else if ((iVar4 == 10) || (iVar4 == 0xb)) {
        iVar4 = get_string_by_id(0x19c7);
        hud_screen_set_handler_param_a(param_1, 0, iVar4);
        hud_screen_set_handler_param_b(param_1, 0, 0);
    } else {
        iVar4 = get_string_by_id(0x19c7);
        hud_screen_set_handler_param_a(param_1, 0, iVar4);
        hud_screen_set_handler_param_b(param_1, 0, 1);
    }
    (void)fVar2;
    (void)fVar3;
}

// -----------------------------------------------------------------------------
// arcsel_init_auto_advance_screen @ 0x802f8494
// Initializes a bare auto-advance screen element: installs the per-frame tick
// callback (arcsel_auto_advance_if_ready) and centres the element.
void arcsel_init_auto_advance_screen(int param_1)
{
    float fVar1;

    hud_element_init_from_layout(0, 0, 0, 0);
    fVar1 = FLOAT_8061a570;
    *(void **)(param_1 + 0x1c0) = (void *)&arcsel_auto_advance_if_ready;
    *(float *)(param_1 + 100) = fVar1;
    hud_element_apply_center_xy((int *)DAT_806119e0);
}

// =============================================================================
// Batch: batch_852 | Module: arcade-select results / match-transition handlers
// Address range: 0x802f84f0 - 0x802f8c0c
//
// The arcade results screen award column plus the match-transition button
// handlers reachable from it. arcsel_draw_award_column renders the per-player
// awards (worm_stat_get_award_string) and final rank (worm_stat_get 0x2d) as
// shadowed text. The retry/restart/quit handlers push the transition screen,
// stamp the ticker state block ((&DAT_805cf280)[player]) and either reset the
// match with a savegame checkpoint (match_reset_with_save) or trigger match end
// (match_end_trigger). arcsel_build_message_screen builds a one-line message
// screen with a cancel callback.
//
// Named helpers (worm_stat_*, text_*, hud_*, gx_*, sound_*, match_*,
// arcsel_mark_all_dirty, get_string_by_id, debug_log_formatted) resolve through
// the shared generated headers; raw FUN_/DAT_/FLOAT_ symbols get per-file externs.

extern "C" {
int  FUN_803826dc(void);            // hud active-screen element ptr
void FUN_80382728(void);            // hud screen finalize (results path)
uint FUN_80366060(void);            // match confirm gate A
uint FUN_80366088(void);            // match confirm gate B
void FUN_803661d8(void);            // match transition cancel/cleanup
void FUN_80366140(void);            // match pre-end hook

extern int   DAT_80611748;   // results/session context block base
extern int   DAT_8043f294;   // rank-string id table (0x15 stride per rank)

extern float FLOAT_8060f95c;
extern float FLOAT_8061a450;
}

// -----------------------------------------------------------------------------
// arcsel_draw_award_column @ 0x802f84f0
// Renders the arcade results award/rank text column: an "Awards" header, up to 3
// per-player award strings (worm_stat_get_award_string), then the final rank
// header and the rank string selected by worm_stat_get(0x2d). All text is drawn
// via text_draw_char_shadowed with layout scaling by FLOAT_8060f95c/FLOAT_8061a444.
void arcsel_draw_award_column(void)
{
    int iVar1;
    float fVar2;
    uint uVar3;
    uint uVar4;
    int iVar5;
    int iVar6;
    byte *pbVar7;
    int iVar9;
    u32 uVar10;
    u32 uVar11;
    int iVar12;
    double dVar13;
    double dVar15;
    double dVar16;
    double dVar17;
    double in_f5 = 0.0, in_f6 = 0.0, in_f7 = 0.0, in_f8 = 0.0;
    u32 in_r7 = 0, in_r8 = 0, in_r9 = 0, in_r10 = 0;

    iVar5 = FUN_803826dc();
    uVar3 = 0x1c;
    iVar9 = *(int *)(DAT_80611748 + 0x34);
    if (iVar9 != 0) {
        uVar3 = 0x10;
    }
    uVar4 = 0x1a;
    iVar1 = (int)(FLOAT_8060f95c *
                  (float)((double)CONCAT44(0x43300000, uVar3 ^ 0x80000000) - DOUBLE_8061a430));
    if (iVar9 != 0) {
        uVar4 = 0xf;
    }
    uVar3 = 3;
    iVar12 = (int)(FLOAT_8060f95c *
                   (float)((double)CONCAT44(0x43300000, uVar4 ^ 0x80000000) - DOUBLE_8061a430));
    if (iVar9 != 0) {
        uVar3 = 0xd;
    }
    fVar2 = FLOAT_8060f95c *
            (float)((double)CONCAT44(0x43300000, uVar3 ^ 0x80000000) - DOUBLE_8061a430);
    iVar9 = session_get_state_flag();
    if (iVar9 == 1) {
        iVar9 = 10;
    } else {
        iVar6 = session_get_state_flag();
        iVar9 = 0x12;
        if (iVar6 == 2) {
            iVar9 = 3;
        }
    }
    gx_set_sub_material_id(1);
    iVar6 = session_get_state_flag();
    if (2 < iVar6) {
        iVar12 = iVar12 + 2;
    }
    text_set_font_table(iVar9);
    dVar17 = (double)*(float *)(iVar5 + 0xa8);
    uVar3 = (uint)(dVar17 + (double)(float)((double)CONCAT44(0x43300000, (int)fVar2 ^ 0x80000000) -
                                            DOUBLE_8061a430));
    uVar4 = (uint)(*(float *)(iVar5 + 0xac) * FLOAT_8061a444 + *(float *)(iVar5 + 0xa4));
    pbVar7 = (byte *)get_string_by_id(0x17c0);
    dVar13 = (double)text_get_char_width(pbVar7);
    uVar11 = 0x43300000;
    dVar16 = (double)FLOAT_8061a444;
    uVar10 = 0x5080;
    dVar15 = (double)(float)((double)CONCAT44(0x43300000, uVar3 ^ 0x80000000) - DOUBLE_8061a430);
    text_draw_char_shadowed(
        -(double)(float)(dVar13 * dVar16 -
                         (double)(float)((double)CONCAT44(0x43300000, uVar4 ^ 0x80000000) -
                                         DOUBLE_8061a430)),
        dVar15, pbVar7, 0x40787880, 0x5080);
    uVar3 = uVar3 + iVar1;
    iVar5 = 0;
    do {
        iVar6 = worm_stat_get_award_string(*(int *)DAT_80611d74, iVar5);
        if (iVar6 != 0) {
            int award = worm_stat_get_award_string(*(int *)DAT_80611d74, iVar5);
            debug_log_formatted((char *)&DAT_806110f8, (u64)award, uVar10, uVar11, in_f5, in_f6,
                                in_f7, in_f8, (u32)(size_t)&DAT_806110f8, (u32)award, uVar10, uVar11,
                                in_r7, in_r8, in_r9, in_r10);
            pbVar7 = (byte *)&DAT_806110f8;
            text_auto_select_font();
            dVar13 = (double)text_get_char_width(pbVar7);
            uVar11 = 0x43300000;
            dVar16 = (double)FLOAT_8061a444;
            uVar10 = 0x50;
            dVar15 = (double)(float)((double)CONCAT44(0x43300000, uVar3 ^ 0x80000000) -
                                     DOUBLE_8061a430);
            text_draw_char_shadowed(
                -(double)(float)(dVar13 * dVar16 -
                                 (double)(float)((double)CONCAT44(0x43300000, uVar4 ^ 0x80000000) -
                                                 DOUBLE_8061a430)),
                dVar15, pbVar7, 0x7f7f7f80, 0x50);
            uVar3 = uVar3 + iVar12;
        }
        iVar5 = iVar5 + 1;
    } while (iVar5 < 3);
    pbVar7 = (byte *)get_string_by_id(0x17b9);
    text_set_font_table(iVar9);
    dVar13 = (double)text_get_char_width(pbVar7);
    dVar16 = (double)FLOAT_8061a444;
    dVar15 = (double)(float)((double)CONCAT44(0x43300000, uVar3 + iVar1 ^ 0x80000000) -
                             DOUBLE_8061a430);
    text_draw_char_shadowed(
        -(double)(float)(dVar13 * dVar16 -
                         (double)(float)((double)CONCAT44(0x43300000, uVar4 ^ 0x80000000) -
                                         DOUBLE_8061a430)),
        dVar15, pbVar7, 0x40787880, 0x5080);
    dVar13 = worm_stat_get(0x2d, *(int *)DAT_80611d74);
    iVar5 = (int)dVar13;
    if (iVar5 == 0) {
        pbVar7 = (byte *)get_string_by_id(0x17bb);
    } else if (iVar5 == 0x46) {
        pbVar7 = (byte *)get_string_by_id(0x16c6);
    } else {
        iVar5 = get_string_by_id((&DAT_8043f294)[iVar5 * 0x15]);
        debug_log_formatted((char *)&DAT_806110f8, (u64)iVar5, uVar10, uVar11, in_f5, in_f6, in_f7,
                            in_f8, (u32)(size_t)&DAT_806110f8, (u32)iVar5, uVar10, uVar11, in_r7,
                            in_r8, in_r9, in_r10);
        pbVar7 = (byte *)&DAT_806110f8;
    }
    text_auto_select_font();
    dVar13 = (double)text_get_char_width(pbVar7);
    text_draw_char_shadowed(
        -(double)(float)(dVar13 * (double)FLOAT_8061a444 -
                         (double)(float)((double)CONCAT44(0x43300000, uVar4 ^ 0x80000000) -
                                         DOUBLE_8061a430)),
        (double)(float)((double)CONCAT44(0x43300000, uVar3 + iVar1 + iVar1 ^ 0x80000000) -
                        DOUBLE_8061a430),
        pbVar7, 0x7f7f7f80, 0x50);
    FUN_80382728();
}

// -----------------------------------------------------------------------------
// arcsel_retry_match @ 0x802f8904
// Retry handler: push the transition screen, stamp the ticker state block, mark
// all arcade-select entries dirty, and reset the match with a savegame checkpoint.
void arcsel_retry_match(void)
{
    uint *puVar1;

    puVar1 = (uint *)(&DAT_805cf280)[DAT_806119ec];
    hud_screen_push();
    puVar1[3] = (uint)FLOAT_8061a418;
    *puVar1 = *puVar1 | 8;
    arcsel_mark_all_dirty();
    match_reset_with_save();
}

// -----------------------------------------------------------------------------
// arcsel_retry_match_if_open @ 0x802f896c
// As arcsel_retry_match, but skips if the element is already closing.
void arcsel_retry_match_if_open(int param_1)
{
    int iVar1;
    uint *puVar2;

    iVar1 = hud_element_is_closing(param_1);
    if (iVar1 == 0) {
        puVar2 = (uint *)(&DAT_805cf280)[DAT_806119ec];
        hud_screen_push();
        puVar2[3] = (uint)FLOAT_8061a418;
        *puVar2 = *puVar2 | 8;
        arcsel_mark_all_dirty();
        match_reset_with_save();
    }
}

// -----------------------------------------------------------------------------
// arcsel_restart_match_muted @ 0x802f89e0
// Restart variant: stamps FLOAT_8061a450 into the ticker block and resets the sfx
// volume scale before the match reset.
void arcsel_restart_match_muted(void)
{
    uint *puVar1;

    puVar1 = (uint *)(&DAT_805cf280)[DAT_806119ec];
    hud_screen_push();
    puVar1[3] = (uint)FLOAT_8061a450;
    *puVar1 = *puVar1 | 8;
    arcsel_mark_all_dirty();
    sound_set_sfx_volume_scale(0xffffffff);
    match_reset_with_save();
}

// -----------------------------------------------------------------------------
// arcsel_push_screen @ 0x802f8a50
// Minimal handler: push the current HUD screen.
void arcsel_push_screen(void)
{
    hud_screen_push();
}

// -----------------------------------------------------------------------------
// arcsel_quit_to_end_match @ 0x802f8a8c
// Quit handler: push the transition screen, stamp the ticker block, mark dirty,
// run the pre-end hook and trigger match end.
void arcsel_quit_to_end_match(void)
{
    uint *puVar1;

    puVar1 = (uint *)(&DAT_805cf280)[DAT_806119ec];
    hud_screen_push();
    puVar1[3] = (uint)FLOAT_8061a418;
    *puVar1 = *puVar1 | 8;
    arcsel_mark_all_dirty();
    FUN_80366140();
    match_end_trigger(0);
}

// -----------------------------------------------------------------------------
// arcsel_quit_if_confirmed @ 0x802f8afc
// Runs arcsel_quit_to_end_match only if the confirm gate FUN_80366060 is set.
void arcsel_quit_if_confirmed(void)
{
    uint uVar1;

    uVar1 = FUN_80366060();
    if (uVar1 != 0) {
        arcsel_quit_to_end_match();
    }
}

// -----------------------------------------------------------------------------
// arcsel_cancel_and_pop @ 0x802f8b38
// Cancel handler: run the transition cleanup then pop the HUD screen.
void arcsel_cancel_and_pop(void)
{
    FUN_803661d8();
    hud_screen_pop_with_restore();
}

// -----------------------------------------------------------------------------
// arcsel_build_message_screen @ 0x802f8b78
// Builds a one-line message HUD screen (string 0xe32) with a cancel callback
// (arcsel_cancel_and_pop) and registers the back handler.
void arcsel_build_message_screen(void)
{
    int iVar1;

    hud_element_init_from_layout(0, 0, 0, (int)(size_t)&arcsel_cancel_and_pop);
    hud_set_item_padding_mode((double)FLOAT_8061a41c, 1, 1, 1);
    get_string_by_id(0xe32);
    hud_add_list_item();
    hud_element_apply_center_xy((int *)DAT_806119e0);
    iVar1 = get_string_by_id(0x19c8);
    hud_screen_register_handler_global(1, iVar1, 0, 0);
}

// -----------------------------------------------------------------------------
// arcsel_confirm_end_or_cancel @ 0x802f8c0c
// If the FUN_80366088 gate is clear, run the full quit-to-end-match path;
// otherwise push the screen and run the transition cleanup.
void arcsel_confirm_end_or_cancel(void)
{
    uint uVar1;
    uint *puVar2;

    uVar1 = FUN_80366088();
    if (uVar1 == 0) {
        puVar2 = (uint *)(&DAT_805cf280)[DAT_806119ec];
        hud_screen_push();
        puVar2[3] = (uint)FLOAT_8061a418;
        *puVar2 = *puVar2 | 8;
        arcsel_mark_all_dirty();
        FUN_80366140();
        match_end_trigger(0);
    } else {
        hud_screen_push();
        FUN_803661d8();
    }
}

// =============================================================================
// Batch: batch_853 | Module: arcade-select results status + end-game dialogs
// Address range: 0x802f8cb8 - 0x802f9ab0
//
// The arcade results status text block and the results/confirm-dialog screens.
// arcsel_draw_results_status renders the pass/fail/rank lines (worm_results_get_*),
// handicap rank, and (story) the completion-level name + hud_marker_draw; it
// returns the running Y so arcsel_build_results_screen can size the spacer.
// arcsel_build_results_screen assembles the results option list (retry / continue
// / quit) per game mode + story flags + checkpoint state. arcsel_show_confirm_dialog
// / arcsel_build_confirm_dialog / arcsel_dialog_cancel / arcsel_confirm_end_match /
// arcsel_prompt_quit_confirm implement the generic yes/no confirm dialog used to
// quit or restart. The remaining handlers push the transition screen and end the
// match.
//
// NON-MATCHING notes: this project's debug_log_formatted is a void logging stub
// (src/game/hud/string_parse.cpp) that composes no string, so where the original
// used its return we call it for side-effect and pass the format string onward.
// hud_screen_push returns void in the shared decl, so the pushed-screen pointer is
// re-fetched via FUN_803826f0 (active-screen ptr).

extern "C" {
int  FUN_803826c8(void);            // hud active-screen element ptr (results path)
void FUN_80382714(void);            // hud screen finalize (results-status path)

extern unsigned int DAT_805015c4;   // results state flags word B
extern unsigned int DAT_80612bd0;   // confirm-dialog pending player mask
extern int   DAT_80612ba0;          // confirm-dialog title string-id
extern int   DAT_80612ba4;          // confirm-dialog body string-id (0 = none)
extern int   DAT_80612bb4;          // confirm-dialog confirm callback
extern int   DAT_80612bb8;          // confirm-dialog cancel callback
extern int   DAT_80477408;          // arcade-select screen slot table (3-int stride)

extern float FLOAT_8061a578;
extern float FLOAT_80611a6c;

extern char s__s__s_8061110c[];     // "%s %s %s" style format for completion line
}

// -----------------------------------------------------------------------------
// arcsel_quit_with_checkpoint @ 0x802f8cb8
// Quit handler ending the match with the worm checkpoint flag as the end reason.
void arcsel_quit_with_checkpoint(void)
{
    int iVar1;
    uint *puVar2;

    puVar2 = (uint *)(&DAT_805cf280)[DAT_806119ec];
    hud_screen_push();
    puVar2[3] = (uint)FLOAT_8061a418;
    *puVar2 = *puVar2 | 8;
    arcsel_mark_all_dirty();
    FUN_80366140();
    iVar1 = worm_checkpoint_get_flag(*(int *)DAT_80611d74);
    match_end_trigger(iVar1);
}

// -----------------------------------------------------------------------------
// arcsel_draw_results_status @ 0x802f8d30
// Renders (or measures, when param_4 == 0) the results status text block: the
// pass/fail line, an optional secondary line, the rank line, and (story mode) the
// completion-level name plus a difficulty marker. Returns the running Y baseline.
double arcsel_draw_results_status(u32 param_1, u32 param_2, float *param_3, int param_4,
                                  u32 param_5, u32 param_6, u32 param_7, u32 param_8)
{
    uint uVar1;
    uint uVar2;
    bool bVar3;
    bool bVar4;
    bool bVar5;
    int iVar6;
    int iVar7;
    int iVar8;
    int iVar9;
    uint uVar10;
    uint uVar11;
    uint uVar12;
    byte *puVar13;
    int iVar16;
    double dVar17;
    double dVar19;
    longlong lVar20;
    double dVar21;
    double dVar22;
    double in_f6 = 0.0, in_f7 = 0.0, in_f8 = 0.0;
    double dVar23;
    double dVar24;
    double dVar25;
    double dVar26;
    byte *local_1b8;
    byte *local_1b4;
    byte *local_1b0;
    byte abStack_1ac[260];
    longlong local_a8;
    longlong local_a0;
    uint uStack_94;
    int uVar14;

    iVar7 = FUN_803826c8();
    iVar16 = param_4;
    iVar8 = session_get_state_flag();
    iVar6 = 0xb;
    if (2 < iVar8) {
        iVar6 = 7;
    }
    iVar9 = session_get_state_flag();
    iVar8 = 4;
    if (2 < iVar9) {
        iVar8 = 3;
    }
    uVar10 = FUN_803825c0((double)(FLOAT_8061a480 * *(float *)(iVar7 + 0xf0)));
    uVar10 = uVar10 & 0xff;
    bVar4 = false;
    bVar3 = false;
    if ((DAT_80612614 == 100) && ((DAT_805015c4 & 0x1000000) != 0)) {
        bVar3 = true;
    }
    local_1b0 = (byte *)0x0;
    dVar22 = (double)(FLOAT_8061a494 * FLOAT_80611a68);
    uVar1 = (uint)(FLOAT_8061a538 * FLOAT_80611a68);
    local_a8 = (longlong)(int)uVar1;
    local_1b4 = (byte *)0x0;
    uVar2 = (uint)(FLOAT_8061a4b4 * FLOAT_80611a64);
    lVar20 = (longlong)(int)uVar2;
    local_1b8 = (byte *)0x0;
    uStack_94 = uVar1 ^ 0x80000000;
    dVar26 = (double)*param_3;
    dVar21 = (double)param_3[2];
    dVar24 = (double)(float)(dVar21 - dVar26);
    dVar23 = (double)(float)(dVar22 + (double)((float)((double)CONCAT44(0x43300000, uStack_94) -
                                                       DOUBLE_8061a430) +
                                               (float)(dVar26 + dVar22)));
    local_a0 = lVar20;
    gx_set_sub_material_id(1);
    bVar5 = false;
    if (((((DAT_805015c0 & 0x40) != 0) || ((DAT_805015c0 & 0x8000000) != 0)) || (bVar3)) &&
        (((DAT_805015c4 & 0x8000) != 0 || ((DAT_805015c4 & 0x1000000) != 0)))) {
        bVar5 = true;
    }
    dVar19 = (double)FLOAT_8061a494;
    dVar17 = (double)FLOAT_80611a64;
    dVar25 = (double)(float)(dVar19 * dVar17 + (double)param_3[1]);
    uVar11 = game_is_handicap_enabled();
    if ((((uVar11 != 0) && ((DAT_805015bd != '\n' || (DAT_80612614 != 100)))) &&
         (DAT_805015bd != '\x03')) &&
        (DAT_805015bd != '\a')) {
        iVar16 = 0;
        hud_format_rank_string(dVar17, dVar19, lVar20, dVar21, dVar22, in_f6, in_f7, in_f8,
                               abStack_1ac,
                               *(int *)(DAT_80611d90 + DAT_806119ec * 0xea0 + 0xa68), 1, 0, param_5,
                               param_6, param_7, param_8);
        local_1b4 = abStack_1ac;
    }
    if (((DAT_805015c0 & 0x40) != 0) || ((DAT_805015c0 & 0x8000000) != 0)) {
        iVar6 = iVar8;
        if ((DAT_805015c4 & 0x10000) == 0) {
            if ((DAT_805015c4 & 0x8000) != 0) {
                worm_results_get_pass_strings((int *)&local_1b8, (int *)&local_1b4);
            }
        } else {
            worm_results_get_three_strings((int *)&local_1b0, (int *)&local_1b4, (int *)&local_1b0);
        }
    }
    if (((DAT_805015bd == '\n') || ((DAT_805015c0 & 0x40) != 0)) ||
        ((DAT_805015c0 & 0x8000000) != 0)) {
        if (((DAT_805015c0 & 0x10000000) == 0) || ((DAT_805015c4 & 0x8000) == 0)) {
            if ((DAT_805015c4 & 0x20000) == 0) {
                if ((DAT_805015c4 & 0x10000) == 0) {
                    if ((DAT_805015c4 & 0x8000) != 0) {
                        if (((DAT_805015c0 & 0x40) == 0) && ((DAT_805015c0 & 0x8000000) == 0)) {
                            local_1b8 = (byte *)get_string_by_id(0x17be);
                        } else if ((DAT_805015c4 & 0x40000) == 0) {
                            if ((DAT_805015c4 & 0x80000) != 0) {
                                local_1b8 = (byte *)get_string_by_id(0xe3c);
                            }
                        } else {
                            local_1b8 = (byte *)get_string_by_id(0xe3f);
                        }
                    }
                } else {
                    local_1b8 = (byte *)get_string_by_id(0xe3d);
                }
            } else {
                local_1b8 = (byte *)get_string_by_id(0x17bf);
            }
        } else {
            text_set_font_table(0xb);
            local_1b8 = (byte *)get_string_by_id(0x17cb);
        }
    }
    if ((local_1b8 != (byte *)0x0) && ((local_1b4 != (byte *)0x0 || (local_1b0 != (byte *)0x0)))) {
        dVar17 = (double)*(float *)(iVar7 + 0xf0);
        uVar11 = FUN_803825c0((double)(float)((double)FLOAT_8061a480 * dVar17));
        uVar12 = FUN_803825c0((double)(float)((double)FLOAT_8061a504 * dVar17));
        text_set_font_table(4);
        dVar17 = dVar23;
        if (!bVar5) {
            puVar13 = text_string_to_upper((uint)(size_t)local_1b8);
            dVar17 = dVar23;
            dVar23 = (double)text_get_char_width(puVar13);
            dVar23 = (double)(float)((double)FLOAT_8061a444 * (double)(float)(dVar24 - dVar23) +
                                     dVar26);
        }
        if (param_4 != 0) {
            puVar13 = text_string_to_upper((uint)(size_t)local_1b8);
            text_draw_char_shadowed(dVar23, dVar25, puVar13, (uVar11 & 0xff) | 0x64646400,
                                    (uVar12 & 0xff) | 0x7f141400);
        }
        dVar23 = dVar17;
        dVar17 = (double)text_get_char_height(local_1b8);
        bVar4 = true;
        dVar25 = (double)(float)(dVar25 + (double)(float)((double)FLOAT_80611a64 + dVar17));
    }
    if (local_1b0 != (byte *)0x0) {
        dVar17 = (double)*(float *)(iVar7 + 0xf0);
        uVar11 = FUN_803825c0((double)(float)((double)FLOAT_8061a480 * dVar17));
        uVar12 = FUN_803825c0((double)(float)((double)FLOAT_8061a490 * dVar17));
        text_set_font_table(4);
        dVar17 = dVar23;
        if (!bVar5) {
            dVar23 = (double)text_get_char_width(local_1b0);
            dVar23 = (double)(float)((double)(float)(dVar24 - dVar23) * (double)FLOAT_8061a444 +
                                     dVar26);
        }
        if (param_4 != 0) {
            text_draw_char_shadowed(dVar23, dVar25, local_1b0, (uVar11 & 0xff) | 0x64646400,
                                    (uVar12 & 0xff) | 0x46466e00);
        }
        dVar23 = dVar17;
        dVar17 = (double)text_get_char_height(local_1b0);
        dVar25 = (double)(float)(dVar25 + (double)(float)((double)FLOAT_80611a64 + dVar17));
    }
    if ((local_1b4 != (byte *)0x0) || ((local_1b8 != (byte *)0x0 && (!bVar4)))) {
        byte *pbVar15 = local_1b8;
        if (local_1b4 != (byte *)0x0) {
            pbVar15 = local_1b4;
        }
        text_set_font_table(iVar6);
        dVar17 = dVar23;
        if (!bVar5) {
            dVar23 = (double)text_get_char_width(pbVar15);
            dVar23 = (double)(float)((double)FLOAT_8061a444 * (double)(float)(dVar24 - dVar23) +
                                     dVar26);
        }
        if (param_4 != 0) {
            text_draw_char_shadowed(dVar23, dVar25, pbVar15, uVar10 | 0x6e6e6e00, uVar10);
        }
        dVar23 = dVar17;
        dVar24 = (double)text_get_char_height(pbVar15);
        dVar25 = (double)(float)(dVar25 + (double)(float)((double)FLOAT_80611a64 + dVar24));
    }
    if (bVar5) {
        byte *pbVar15;
        dVar24 = (double)FLOAT_8061a494;
        dVar17 = (double)(float)(dVar24 * (double)FLOAT_80611a64 + (double)param_3[1]);
        uVar11 = match_get_completion_level();
        if (DAT_8060f948 == 4) {
            uVar14 = difficulty_get_name_string(uVar11);
            iVar6 = get_string_by_id(0x160e);
            debug_log_formatted(s__s__s_8061110c, (u64)iVar6, (u64)(u32)uVar14, (u64)iVar16, in_f6,
                                in_f7, in_f8, 0, (u32)(size_t)s__s__s_8061110c, (u32)iVar6,
                                (u32)uVar14, (u32)iVar16, param_5, param_6, param_7, param_8);
            pbVar15 = text_string_to_upper((uint)(size_t)s__s__s_8061110c);
        } else {
            iVar6 = get_string_by_id(0x160e);
            uVar14 = difficulty_get_name_string(uVar11);
            debug_log_formatted(s__s__s_8061110c, (u64)(u32)uVar14, (u64)iVar6, (u64)iVar16, in_f6,
                                in_f7, in_f8, 0, (u32)(size_t)s__s__s_8061110c, (u32)uVar14,
                                (u32)iVar6, (u32)iVar16, param_5, param_6, param_7, param_8);
            pbVar15 = text_string_to_upper((uint)(size_t)s__s__s_8061110c);
        }
        text_set_font_table(3);
        dVar21 = (double)text_get_char_height(pbVar15);
        if (param_4 == 0) {
            uStack_94 = uVar2 ^ 0x80000000;
        } else {
            uStack_94 = uVar2 ^ 0x80000000;
            dVar22 = (double)(float)(dVar17 + (double)(float)((double)CONCAT44(0x43300000, uStack_94) -
                                                              DOUBLE_8061a430));
            if ((double)(float)(dVar25 + (double)(float)((double)FLOAT_80611a64 + dVar21)) < dVar22) {
                dVar25 = (double)(float)(dVar22 - (double)(float)((double)FLOAT_80611a64 + dVar21));
            }
            text_draw_char_shadowed(dVar23, dVar25, pbVar15, uVar10 | 0x6e6e6e00, uVar10);
            gx_set_scissor_rect(*(uint *)DAT_80611748, ((uint *)DAT_80611748)[5],
                                ((int *)DAT_80611748)[1], ((int *)DAT_80611748)[6]);
            local_a8 = (longlong)(int)dVar17;
            iVar16 = (int)((double)FLOAT_8061a494 * (double)FLOAT_80611a68 + dVar26);
            local_a0 = (longlong)iVar16;
            hud_marker_draw(uVar11, iVar16, (int)dVar17, uVar1, uVar2);
        }
    }
    FUN_80382714();
    (void)local_a8;
    (void)local_a0;
    (void)param_1;
    (void)param_2;
    return dVar25;
}

// -----------------------------------------------------------------------------
// arcsel_draw_results_status_visible @ 0x802f943c
// Thunk: draw the results status block (param_4 = 1). Returns the running Y.
double arcsel_draw_results_status_visible(u32 param_1, u32 param_2, float *param_3, u32 param_4,
                                          u32 param_5, u32 param_6, u32 param_7, u32 param_8)
{
    (void)param_4;
    return arcsel_draw_results_status(param_1, param_2, param_3, 1, param_5, param_6, param_7,
                                      param_8);
}

// -----------------------------------------------------------------------------
// arcsel_build_results_screen @ 0x802f9464
// Builds the arcade results screen: a measured status-block spacer (sized from
// arcsel_draw_results_status) plus the option list (retry / continue / quit /
// checkpoint reload) selected by game mode, story flags and tracking state.
void arcsel_build_results_screen(int param_1)
{
    int iVar1;
    uint uVar2;
    u32 in_r7 = 0, in_r8 = 0, in_r9 = 0, in_r10 = 0;
    double dVar3;
    float local_18;
    float local_14;
    float local_10;
    float local_c;

    local_10 = FLOAT_8061a578;
    local_18 = FLOAT_8061a418;
    local_14 = FLOAT_8061a418;
    local_c = FLOAT_8061a418;
    hud_element_init_from_layout(0x28000, 0, 0, 0);
    *(void **)(param_1 + 0x1c4) = (void *)&arcsel_auto_advance_next;
    hud_set_item_padding_mode((double)FLOAT_8061a494, 1, 1, 1);
    hud_set_context_property(0x32200);
    dVar3 = arcsel_draw_results_status(param_1, 0, &local_18, 0, in_r7, in_r8, in_r9, in_r10);
    hud_add_spacer_item((float)(FLOAT_80611a5c * (float)(dVar3 - (double)FLOAT_80611a6c)), 0,
                        (int)(size_t)&arcsel_draw_results_status_visible);
    hud_add_default_callback_item();
    if (DAT_806119ec != 0) {
        goto LAB_center;
    }
    stats_is_tracking_active();
    if ((DAT_805015c4 & 0x8000) == 0) {
        iVar1 = arcsel_screen_find_index(-0x7fb8892c);
        (&DAT_80477408)[iVar1 * 3] = 0x174e;
    } else {
        iVar1 = arcsel_screen_find_index(-0x7fb8892c);
        (&DAT_80477408)[iVar1 * 3] = 0x174f;
    }
    if (DAT_805015bd != '\n') {
        if (((DAT_805015c0 & 0x40) == 0) && ((DAT_805015c0 & 0x8000000) == 0)) {
            iVar1 = stats_is_tracking_active();
            if (iVar1 != 0) {
                uVar2 = FUN_80366060();
                if (uVar2 != 0) {
                    get_string_by_id(0x1743);
                    hud_add_list_item();
                    if ((DAT_805015c0 & 0x4000000) == 0) {
                        get_string_by_id(0x1741);
                    } else {
                        get_string_by_id(0x1751);
                    }
                    hud_add_list_item();
                    goto LAB_center;
                }
            }
            iVar1 = stats_is_tracking_active();
            if (iVar1 != 0) {
                get_string_by_id(0x1750);
                hud_add_list_item();
            }
            goto LAB_center;
        }
    }
    if ((DAT_805015c4 & 0x8000) == 0) {
        if ((DAT_805015bd == '\n') &&
            (iVar1 = worm_checkpoint_get_flag(*(int *)DAT_80611d74), iVar1 != 0)) {
            get_string_by_id(0x1745);
            hud_add_list_item();
        }
        get_string_by_id(0x1747);
        hud_add_list_item();
        get_string_by_id(0x1750);
        hud_add_list_item();
        goto LAB_center;
    }
    if ((DAT_805015bd == '\n') && ((DAT_805015c0 & 0x40) == 0) && (DAT_80612614 != 100)) {
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
LAB_center:
    hud_element_apply_center_xy((int *)DAT_806119e0);
}

// -----------------------------------------------------------------------------
// arcsel_build_confirm_dialog @ 0x802f97f4
// Builds a generic confirm dialog screen from the stored params: title
// (DAT_80612ba0), optional body (DAT_80612ba4), yes/no items, callback DAT_80612bb4.
void arcsel_build_confirm_dialog(int param_1)
{
    int iVar1;

    hud_set_item_padding_mode((double)FLOAT_8061a494, 1, 1, 1);
    hud_element_init_from_layout(0xa000, 0, 0, DAT_80612bb4);
    *(int *)(param_1 + 0x1c0) = 0;
    *(int *)(param_1 + 0x1c4) = 0;
    get_string_by_id(DAT_80612ba0);
    hud_add_list_item();
    hud_add_callback_item_default(1);
    if (DAT_80612ba4 != 0) {
        get_string_by_id(DAT_80612ba4);
        hud_add_list_item();
        hud_add_callback_item_default(1);
    }
    get_string_by_id(0x173e);
    hud_add_list_item();
    get_string_by_id(0x173f);
    hud_add_list_item();
    hud_set_focus_by_id(param_1, 1);
    hud_element_apply_center_xy((int *)DAT_806119e0);
    iVar1 = arcsel_profile_slot_state(*(int *)(param_1 + 0x38));
    if (iVar1 != 0) {
        *(float *)(param_1 + 0x20c) = FLOAT_8061a564;
    }
}

// -----------------------------------------------------------------------------
// arcsel_show_confirm_dialog @ 0x802f992c
// Stores the confirm-dialog params (title, body, confirm/cancel callbacks), marks
// this player's pending bit, pushes the dialog screen and ORs param_5 into flags.
void arcsel_show_confirm_dialog(int param_1, uint param_2, int param_3, int param_4, uint param_5)
{
    int iVar1;

    DAT_80612bd0 = DAT_80612bd0 | 1 << DAT_806119ec;
    DAT_80612ba4 = param_2 & (0xffffffffU - ((int)param_2 >> 0x1f));
    DAT_80612ba0 = param_1;
    DAT_80612bb4 = param_4;
    DAT_80612bb8 = param_3;
    hud_screen_push();
    iVar1 = FUN_803826f0();
    *(uint *)(iVar1 + 0x40) = *(uint *)(iVar1 + 0x40) | param_5;
}

// -----------------------------------------------------------------------------
// arcsel_push_screen_marked @ 0x802f99b0
// Push the screen and set the ticker state flag (bit 3) without a match reset.
void arcsel_push_screen_marked(void)
{
    uint *puVar1;

    puVar1 = (uint *)(&DAT_805cf280)[DAT_806119ec];
    hud_screen_push();
    puVar1[3] = (uint)FLOAT_8061a418;
    *puVar1 = *puVar1 | 8;
}

// -----------------------------------------------------------------------------
// arcsel_dialog_cancel @ 0x802f9a10
// Confirm-dialog cancel: clear the pending mask and pop the current screen.
void arcsel_dialog_cancel(void)
{
    DAT_80612bd0 = 0;
    hud_screen_pop_current();
}

// -----------------------------------------------------------------------------
// arcsel_confirm_end_match @ 0x802f9a38
// Confirm-dialog OK path: push, mark dirty, per-frame stats update, end match,
// clear the pending mask.
void arcsel_confirm_end_match(void)
{
    uint *puVar1;

    puVar1 = (uint *)(&DAT_805cf280)[DAT_806119ec];
    hud_screen_push();
    puVar1[3] = (uint)FLOAT_8061a418;
    *puVar1 = *puVar1 | 8;
    arcsel_mark_all_dirty();
    stats_update_per_frame();
    match_end_trigger(0);
    DAT_80612bd0 = 0;
}

// -----------------------------------------------------------------------------
// arcsel_prompt_quit_confirm @ 0x802f9ab0
// Shows the quit confirm dialog with story vs arcade title/body strings, wiring
// arcsel_confirm_end_match (OK) and arcsel_dialog_cancel (cancel).
void arcsel_prompt_quit_confirm(void)
{
    if (DAT_805015bd == '\n') {
        arcsel_show_confirm_dialog(0x174d, 0x1753, (int)(size_t)&arcsel_confirm_end_match,
                                   (int)(size_t)&arcsel_dialog_cancel, 0);
    } else {
        arcsel_show_confirm_dialog(0x174a, 0xffffffff, (int)(size_t)&arcsel_confirm_end_match,
                                   (int)(size_t)&arcsel_dialog_cancel, 0);
    }
}

// =============================================================================
// Batch: batch_854 | Module: arcade-select in-match pause menu + loading title
// Address range: 0x802f9b24 - 0x802fa304
//
// The in-match pause/quit menu reachable from the arcade-select results screens.
// arcsel_quit_end_match_no_save / arcsel_restart_match_now push the transition
// screen, stamp this player's ticker state block ((&DAT_805cf280)[player]) and
// end (match_end_trigger) or reset (match_reset_with_save) the match. Their
// prompt wrappers (arcsel_prompt_end_match / arcsel_prompt_restart_match) pop the
// generic confirm dialog with the right strings. arcsel_build_pause_menu_list is
// the pause-menu HUD list (resume / options / restart / quit / optional checkpoint
// + end-match rows) with per-item locking, arcsel_open_options_settings enters the
// options screen, arcsel_cancel_restore_and_push restores the working profile block
// on cancel, and arcsel_input_open_pause opens the pause screen on a Start press.
// arcsel_draw_paused_title / arcsel_draw_loading_title render the paused and
// loading screen titles as shadowed text.
//
// NON-MATCHING: format_string_varargs (0x80006218) is named in the IDB but has no
// shared header yet, so it is declared here as a per-file variadic extern to
// accept the mixed int/void*/double argument list of the loading-title path. The
// PowerPC int->float magic-bias idiom (CONCAT44(0x43300000, x ^ 0x80000000) -
// DOUBLE_8061a430) is preserved verbatim; incoming register-spill FP args are
// modelled as locals.

extern "C" {
int format_string_varargs(...);     // 0x80006218 formatted string builder (variadic)

extern int   DAT_80612bd4;          // per-player options-open bitmask
extern int   DAT_80612bcc;          // pause-menu max active player index
extern int   DAT_80611118;          // loading-title string-id
extern int   DAT_8061111c;          // loading-title percent format buffer/arg
extern int   DAT_80611114;          // loading-title raw progress numerator
extern int   DAT_806109c0;          // progress denominator (frames-per-percent)
extern int   DAT_80611654;          // text single-line/no-wrap flag

extern float FLOAT_8061a4f8;
extern float FLOAT_8061a57c;
extern float FLOAT_8061a580;
extern float FLOAT_8061a584;
}

// -----------------------------------------------------------------------------
// arcsel_quit_end_match_no_save @ 0x802f9b24
// Pause-menu quit handler: push the transition screen, stamp the ticker state
// block, mark all arcade-select entries dirty, run the per-frame stats update and
// end the match using the worm checkpoint flag as the end reason. Clears the
// confirm-dialog pending mask.
void arcsel_quit_end_match_no_save(void)
{
    int iVar1;
    uint *puVar2;

    puVar2 = (uint *)(&DAT_805cf280)[DAT_806119ec];
    hud_screen_push();
    puVar2[3] = (uint)FLOAT_8061a418;
    *puVar2 = *puVar2 | 8;
    arcsel_mark_all_dirty();
    stats_update_per_frame();
    iVar1 = worm_checkpoint_get_flag(*(int *)DAT_80611d74);
    match_end_trigger(iVar1);
    DAT_80612bd0 = 0;
}

// -----------------------------------------------------------------------------
// arcsel_prompt_end_match @ 0x802f9ba4
// Show the end-match confirm dialog (title 0x174b / body 0x1753) wiring
// arcsel_quit_end_match_no_save (OK) and arcsel_dialog_cancel (cancel).
void arcsel_prompt_end_match(void)
{
    arcsel_show_confirm_dialog(0x174b, 0x1753, (int)(size_t)&arcsel_quit_end_match_no_save,
                               (int)(size_t)&arcsel_dialog_cancel, 0);
}

// -----------------------------------------------------------------------------
// arcsel_restart_match_now @ 0x802f9be0
// Pause-menu restart handler: push the transition screen, stamp the ticker block,
// mark dirty, run per-frame stats and reset the match with a savegame checkpoint.
void arcsel_restart_match_now(void)
{
    uint *puVar1;

    puVar1 = (uint *)(&DAT_805cf280)[DAT_806119ec];
    hud_screen_push();
    puVar1[3] = (uint)FLOAT_8061a418;
    *puVar1 = *puVar1 | 8;
    arcsel_mark_all_dirty();
    stats_update_per_frame();
    match_reset_with_save();
}

// -----------------------------------------------------------------------------
// arcsel_prompt_restart_match @ 0x802f9c4c
// Pick the restart confirm strings by session flags: the "no checkpoint" title
// 0x1751 when bit 0x4000000 is set; otherwise the 0x1750 title with a story body
// (0x1753) when in story mode (DAT_805015bd == '\n') or no body for arcade.
void arcsel_prompt_restart_match(void)
{
    if ((DAT_805015c0 & 0x4000000) == 0) {
        if (DAT_805015bd == '\n') {
            arcsel_show_confirm_dialog(0x1750, 0x1753, (int)(size_t)&arcsel_restart_match_now,
                                       (int)(size_t)&arcsel_dialog_cancel, 0);
        } else {
            arcsel_show_confirm_dialog(0x1750, 0xffffffff, (int)(size_t)&arcsel_restart_match_now,
                                       (int)(size_t)&arcsel_dialog_cancel, 0);
        }
    } else {
        arcsel_show_confirm_dialog(0x1751, 0xffffffff, (int)(size_t)&arcsel_restart_match_now,
                                   (int)(size_t)&arcsel_dialog_cancel, 0);
    }
}

// -----------------------------------------------------------------------------
// arcsel_cancel_restore_and_push @ 0x802f9cf0
// Cancel handler: restore the working profile block, clear the pending flag
// DAT_80612bc4 and push the screen.
void arcsel_cancel_restore_and_push(void)
{
    arcsel_restore_profile_block();
    DAT_80612bc4 = 0;
    hud_screen_push();
}

// -----------------------------------------------------------------------------
// arcsel_open_options_settings @ 0x802f9d44
// Set this player's options-open bit in DAT_80612bd4, load the options/settings
// screen data and push it.
void arcsel_open_options_settings(void)
{
    DAT_80612bd4 = DAT_80612bd4 | 1 << DAT_806119ec;
    menu_options_settings_load();
    hud_screen_push();
}

// -----------------------------------------------------------------------------
// arcsel_build_pause_menu_list @ 0x802f9da0
// Builds the in-match pause menu HUD list: resume (0x1742), options (0x1749),
// restart (0x17d4/0x17d5 by slot state), quit (0x174a/0x174d by story mode, locked
// while a confirm dialog is pending), an optional checkpoint-reload row (0x174b in
// story mode when the checkpoint flag is set), and an optional end-match row
// (0x1750/0x1751) unless the match-end bit 0x10000000 is set. In record screens
// (>= 0x65) it centres the element, otherwise it applies the scroll tick.
void arcsel_build_pause_menu_list(void)
{
    int element;
    int iVar1;
    int iVar2;
    double dVar3;

    element = FUN_803826f0();
    iVar1 = arcsel_profile_slot_state(*(int *)(element + 0x38));
    iVar2 = arcsel_profile_is_multiplayer();
    hud_element_init_from_layout(0x28000, 0, 0, 0);
    dVar3 = (double)FLOAT_8061a4f8;
    *(void **)(element + 0x1c4) = (void *)&arcsel_auto_advance_next;
    hud_set_item_padding_mode(dVar3, 1, 1, 1);
    hud_set_context_property(~-(uint)(iVar2 != 0) & 0x200 | 0x32000);

    get_string_by_id(0x1742);
    hud_add_list_item();
    get_string_by_id(0x1749);
    hud_add_list_item();

    if (iVar1 == 0) {
        get_string_by_id(0x17d4);
    } else {
        get_string_by_id(0x17d5);
    }
    hud_add_list_item();

    if (DAT_805015bd == '\n') {
        get_string_by_id(0x174d);
    } else {
        get_string_by_id(0x174a);
    }
    hud_add_list_item();
    iVar1 = (int)hud_get_last_created_item();
    hud_item_set_locked(iVar1, (-DAT_80612bd0 & ~DAT_80612bd0) >> 0x1f);

    if (DAT_805015bd == '\n') {
        iVar1 = worm_checkpoint_get_flag(DAT_806119ec);
        if (iVar1 != 0) {
            get_string_by_id(0x174b);
            hud_add_list_item();
            iVar1 = (int)hud_get_last_created_item();
            hud_item_set_locked(iVar1, (-DAT_80612bd0 & ~DAT_80612bd0) >> 0x1f);
        }
    }

    if ((DAT_805015c0 & 0x10000000) == 0) {
        if ((DAT_805015c0 & 0x4000000) == 0) {
            get_string_by_id(0x1750);
        } else {
            get_string_by_id(0x1751);
        }
        hud_add_list_item();
        iVar1 = (int)hud_get_last_created_item();
        hud_item_set_locked(iVar1, (-DAT_80612bd0 & ~DAT_80612bd0) >> 0x1f);
    }

    if (DAT_80612614 < 0x65) {
        arcsel_element_tick_scroll(element);
    } else {
        hud_element_apply_center_xy((int *)DAT_806119e0);
    }
    *(u32 *)((&DAT_805cf280)[*(int *)(element + 0x38)] + 0x264) = 1;
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// arcsel_draw_paused_title @ 0x802fa08c
// Renders the paused-screen title (string 0x17bc) and its scale-checked subtitle
// (0x175c) as shadowed text, using the session-mode font tables and the results
// context layout block (DAT_80611748).
void arcsel_draw_paused_title(void)
{
    uint uVar1;
    float fVar2;
    int iVar3;
    int iVar4;
    byte *pbVar5;
    int iVar6;
    byte *puVar7;
    double dVar8;

    __save_gpr_28();
    iVar3 = session_get_state_flag();
    iVar4 = 0xb;
    if (2 < iVar3) {
        iVar4 = 9;
    }
    iVar4 = text_set_font_table(iVar4);
    iVar3 = DAT_80611748;
    fVar2 = FLOAT_8061a57c * FLOAT_80611674;
    dVar8 = arcsel_menu_scroll_offset(*(int *)(DAT_80611d74 + 4));
    uVar1 = (uint)-(FLOAT_8061a580 *
                    (float)((double)CONCAT44(0x43300000, *(uint *)(iVar3 + 0x20) ^ 0x80000000) -
                            DOUBLE_8061a430) -
                    (float)((double)(float)((double)CONCAT44(0x43300000,
                                                             *(uint *)(iVar3 + 0x18) ^ 0x80000000) -
                                            DOUBLE_8061a430) - dVar8));
    gx_set_sub_material_id(1);
    pbVar5 = (byte *)get_string_by_id(0x17bc);
    dVar8 = text_get_char_width(pbVar5);
    text_draw_char_shadowed(
        -(double)(float)(dVar8 * (double)FLOAT_8061a444 -
                         (double)(float)((double)CONCAT44(0x43300000,
                                                          *(uint *)(DAT_80611748 + 8) ^ 0x80000000) -
                                         DOUBLE_8061a430)),
        (double)(float)((double)CONCAT44(0x43300000, uVar1 ^ 0x80000000) - DOUBLE_8061a430),
        pbVar5, 0x64646480, 0x40);
    iVar3 = session_get_state_flag();
    iVar6 = 0x12;
    if (2 < iVar3) {
        iVar6 = 0x15;
    }
    text_set_font_table(iVar6);
    puVar7 = (byte *)get_string_by_id(0x175c);
    puVar7 = (byte *)text_check_scale(
        (double)(FLOAT_8061a584 *
                 (float)((double)CONCAT44(0x43300000, *(uint *)(DAT_80611748 + 0xc) ^ 0x80000000) -
                         DOUBLE_8061a430)),
        puVar7);
    DAT_80611654 = 1;
    text_draw_char_shadowed(
        (double)(float)((double)CONCAT44(0x43300000, *(uint *)(DAT_80611748 + 8) ^ 0x80000000) -
                        DOUBLE_8061a430),
        (double)(float)((double)CONCAT44(0x43300000, uVar1 + (int)fVar2 ^ 0x80000000) -
                        DOUBLE_8061a430),
        puVar7, 0x64646480, 0x40);
    DAT_80611654 = 0;
    text_set_font_table(iVar4);
    __restore_gpr_28();
}

// -----------------------------------------------------------------------------
// arcsel_input_open_pause @ 0x802fa26c
// If the Start button (mask 0x100) is held on the element's controller, set the
// pause bits (clear 0x2, set 0x20) in this player's ticker state block, track the
// highest active player index and push the pause screen.
void arcsel_input_open_pause(int param_1)
{
    uint uVar1;
    uint *puVar2;

    uVar1 = input_get_buttons_held(*(int *)(param_1 + 0x38));
    if ((uVar1 & 0x100) != 0) {
        puVar2 = (uint *)(&DAT_805cf280)[*(int *)(param_1 + 0x38)];
        *puVar2 = *puVar2 & 0xfffffffd;
        *puVar2 = *puVar2 | 0x20;
        if (DAT_80612bcc < *(int *)(param_1 + 0x38)) {
            DAT_80612bcc = *(int *)(param_1 + 0x38);
        }
        hud_screen_push();
    }
}

// -----------------------------------------------------------------------------
// arcsel_draw_loading_title @ 0x802fa304
// Renders the loading-screen title (string DAT_80611118) and a formatted progress
// line (string 0x17c4 with the percentage DAT_80611114 / DAT_806109c0 composed via
// format_string_varargs) as shadowed text, right-aligned to the layout column.
void arcsel_draw_loading_title(void)
{
    int iVar1;
    int iVar2;
    byte *pbVar3;
    double dVar4;
    double dVar6;
    double dVar7;
    byte abStack_98[104];

    iVar1 = session_get_state_flag();
    iVar2 = 0xb;
    if (2 < iVar1) {
        iVar2 = 9;
    }
    iVar1 = text_set_font_table(iVar2);

    pbVar3 = (byte *)get_string_by_id(DAT_80611118);
    dVar4 = text_get_char_width(pbVar3);
    dVar7 = (double)FLOAT_8061a444;
    dVar6 = (double)(float)((double)CONCAT44(0x43300000,
                                             (uint)(*(int *)(DAT_80611748 + 0x1c) - 0x14) ^
                                                 0x80000000) -
                            DOUBLE_8061a430);
    text_draw_char_shadowed(
        -(double)(float)(dVar4 * dVar7 -
                         (double)(float)((double)CONCAT44(0x43300000,
                                                          *(uint *)(DAT_80611748 + 8) ^ 0x80000000) -
                                         DOUBLE_8061a430)),
        dVar6, pbVar3, 0x40787880, 0x5080);

    iVar2 = get_string_by_id(0x17c4);
    format_string_varargs(abStack_98, iVar2, &DAT_8061111c, DAT_80611114 / DAT_806109c0);
    dVar4 = text_get_char_width(abStack_98);
    text_draw_char_shadowed(
        -(double)(float)(dVar4 * (double)FLOAT_8061a444 -
                         (double)(float)((double)CONCAT44(0x43300000,
                                                          *(uint *)(DAT_80611748 + 8) ^ 0x80000000) -
                                         DOUBLE_8061a430)),
        (double)(float)((double)CONCAT44(0x43300000,
                                         (uint)(*(int *)(DAT_80611748 + 0x1c) + 0x32) ^
                                             0x80000000) -
                        DOUBLE_8061a430),
        abStack_98, 0x40787880, 0x5080);
    text_set_font_table(iVar1);
}

// =============================================================================
// Batch: batch_855 | Module: arcade-select results scoreboard + stats/team tables
// Address range: 0x802fa450 - 0x802fb788
//
// The arcade-select results scoreboard render subsystem. arcsel_countdown_advance /
// arcsel_init_countdown_screen drive a "match resets in N" countdown that triggers
// match_full_reset. arcsel_build_score_column_screen wraps arcsel_render_score_column
// (batch_846) in a horizontally-scrolled spacer, rendered per-frame by
// arcsel_score_column_spacer_render (viewport-gated). arcsel_draw_stats_panel is the
// per-player results stats table (kills/deaths/frags, objective, and the league
// score/streak/time block, selected by the DAT_805015d8 game-mode); its screen is
// built by arcsel_build_stats_panel_screen and drawn via the _visible thunk.
// arcsel_draw_team_results_table renders the per-team class-results rows (one row per
// class via arcsel_render_row_label, batch_845), wrapped by arcsel_draw_team_results_visible.
//
// NON-MATCHING: arcsel_render_score_column and arcsel_render_row_label are void in the
// shared decls (their register return was dropped), so the measure/height calls here
// go through typed fn-ptr aliases (__arcsel_render_score_column_d / __arcsel_render_row_label_d)
// to the same ROM addresses to recover the running-Y double without changing the
// canonical void signatures. debug_log_formatted is a void logging stub, so composed
// strings are taken from its destination buffer arg. The PowerPC int->float magic-bias
// idiom is preserved; incoming register-spill FP args are modelled as locals.

extern "C" {
// typed fn-ptr aliases (leading __ so gen_externs skips them) to recover the
// dropped register-return of the void-declared renderers:
typedef double (*ArcselScoreColRetFn)(u32, u32, float *, int, byte *, byte *, u32, u32);
typedef double (*ArcselRowLabelRetFn)(double, double, u64, u64, u64, u64, u64, u64,
                                      int, int, uint, uint, uint, uint, uint, int);
static const ArcselScoreColRetFn __arcsel_render_score_column_d =
    (ArcselScoreColRetFn)0x802f44f4;
static const ArcselRowLabelRetFn __arcsel_render_row_label_d =
    (ArcselRowLabelRetFn)0x802f3a8c;

int  FUN_803826d0(void);            // hud active-screen element ptr (team-results path)
void FUN_8038271c(void);            // hud screen finalize (team-results path)

extern int   DAT_80612ba8;          // countdown-screen match-reset latch
extern int   DAT_805015d8;          // results scoreboard game-mode selector
extern int   DAT_80611114;          // countdown timer (frames)
extern int   DAT_80611120;          // stats-panel label format buffer
extern int   DAT_80611124;          // stats-panel ratio format buffer
extern int   DAT_8061112c;          // stats-panel value format buffer (mode2)
extern int   DAT_80611130;          // stats-panel streak/time format buffer
extern int   DAT_806117b4;          // level stat context (team-class enumeration)

extern int   DAT_803e0978;          // score-column default rect (x,y,w,h)
extern int   DAT_803e097c;
extern int   DAT_803e0980;
extern int   DAT_803e0984;
extern int   DAT_803e0988;          // stats-panel default rect (x,y,w,h)
extern int   DAT_803e098c;
extern int   DAT_803e0990;
extern int   DAT_803e0994;

extern float FLOAT_8061a510;
extern float FLOAT_8061a58c;
extern float FLOAT_8061a590;
extern float FLOAT_8061a594;
extern float FLOAT_8061a440;

extern char s__________80477744[];        // "%s -" style row format
extern char s__02d__02d__01d_80477734[];  // "%02d:%02d.%01d" time format
}

// -----------------------------------------------------------------------------
// arcsel_countdown_advance @ 0x802fa450
// Per-frame countdown tick: decrement DAT_80611114; at <=0, stamp this player's
// ticker state block (unless already closing bit 4), mark all dirty + refresh the
// screen, then (once, guarded by DAT_80612ba8) trigger a full match reset.
void arcsel_countdown_advance(void)
{
    uint *puVar1;

    DAT_80611114 = DAT_80611114 + -1;
    puVar1 = (uint *)(&DAT_805cf280)[DAT_806119ec];
    if (DAT_80611114 < 1) {
        if ((*puVar1 & 4) == 0) {
            puVar1[3] = (uint)FLOAT_8061a418;
            *puVar1 = *puVar1 | 8;
            arcsel_mark_all_dirty();
            arcsel_screen_update();
        }
        if (DAT_80612ba8 == 0) {
            DAT_80612ba8 = 1;
            match_full_reset();
        }
    }
}

// -----------------------------------------------------------------------------
// arcsel_init_countdown_screen @ 0x802fa4d8
// Initialise the countdown screen element: seed the timer to 10 seconds worth of
// frames (DAT_806109c0 * 10), install the auto-advance tick callbacks, centre the
// element and clear the reset latch.
void arcsel_init_countdown_screen(int param_1)
{
    DAT_80611114 = DAT_806109c0 * 10;
    *(void **)(param_1 + 0x1c0) = (void *)&arcsel_auto_advance_if_ready;
    *(void **)(param_1 + 0x1c4) = (void *)&arcsel_auto_advance_next;
    hud_element_init_from_layout(0, 0, 0, 0);
    *(float *)(param_1 + 100) = FLOAT_8061a510;
    hud_element_apply_center_xy((int *)DAT_806119e0);
    *(float *)(param_1 + 0x20c) = FLOAT_8061a4f8;
    DAT_80612ba8 = 0;
}

// -----------------------------------------------------------------------------
// arcsel_score_column_null_arg @ 0x802fa55c
// Stub returning 0, used as the null string argument for arcsel_render_score_column.
int arcsel_score_column_null_arg(void)
{
    return 0;
}

// -----------------------------------------------------------------------------
// arcsel_score_column_spacer_render @ 0x802fa564
// HUD spacer render callback: when the active viewport is not 3/4/6, draw the
// score column (draw pass) via arcsel_render_score_column.
void arcsel_score_column_spacer_render(u32 param_1, u32 param_2, float *param_3, u32 param_4,
                                       u32 param_5, byte *param_6, u32 param_7, u32 param_8)
{
    int iVar1;
    byte *pbVar2;
    u64 uVar3;

    (void)param_1;
    (void)param_2;
    (void)param_4;
    (void)param_5;
    uVar3 = (u64)(u32)FUN_803826f0();
    iVar1 = hud_screen_get_active_viewport();
    if ((iVar1 != 3) && (iVar1 = hud_screen_get_active_viewport(), iVar1 != 4) &&
        (iVar1 = hud_screen_get_active_viewport(), iVar1 != 6)) {
        pbVar2 = (byte *)(intptr_t)arcsel_score_column_null_arg();
        arcsel_render_score_column((u32)(uVar3 >> 0x20), (u32)uVar3, param_3, 1, pbVar2, param_6,
                                   param_7, param_8);
    }
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// arcsel_build_score_column_screen @ 0x802fa5dc
// Build the score-column screen: measure the column height (measure pass), install
// the auto-advance callbacks, and add a horizontally-scrolled spacer item that
// renders arcsel_score_column_spacer_render each frame.
void arcsel_build_score_column_screen(int param_1)
{
    float fVar1;
    byte *pbVar2;
    int iVar3;
    byte *in_r8 = 0;
    u32 in_r9 = 0, in_r10 = 0;
    double dVar4;
    float local_28[4];

    local_28[0] = (float)DAT_803e0978;
    local_28[1] = (float)DAT_803e097c;
    local_28[2] = (float)DAT_803e0980;
    local_28[3] = (float)DAT_803e0984;
    pbVar2 = (byte *)(intptr_t)arcsel_score_column_null_arg();
    dVar4 = __arcsel_render_score_column_d(param_1, 0, local_28, 0, pbVar2, in_r8, in_r9, in_r10);
    iVar3 = arcsel_profile_is_multiplayer();
    arcsel_profile_slot_state(*(int *)(param_1 + 0x38));
    *(void **)(param_1 + 0x1c0) = (void *)&arcsel_auto_advance_if_ready;
    *(void **)(param_1 + 0x1c4) = (void *)&arcsel_auto_advance_next;
    hud_element_init_from_layout(0xc, 0, 0, 0);
    fVar1 = FLOAT_8061a560;
    if (iVar3 != 0) {
        fVar1 = FLOAT_8061a58c;
    }
    hud_element_scroll_horizontal((double)fVar1, 0);
    hud_add_spacer_item((float)dVar4, 0, (int)(size_t)&arcsel_score_column_spacer_render);
    hud_element_apply_scroll();
    arcsel_element_tick_scroll(param_1);
}

// -----------------------------------------------------------------------------
// arcsel_draw_stats_panel @ 0x802fa6f4
// Renders (or measures, param_4 == 0) the per-player results stats table. The
// game-mode selector DAT_805015d8 picks which blocks to draw: kills (0x674) and
// frags (0x677/0x678 + ratio), objective points (0x675/0x676), and the league
// block (0x679-0x67e: score/streak/time records via worm_stat_get_snapshot with
// time values formatted by time_format_long and a raw "mm:ss.d" via s_..80477734).
// Returns the running Y baseline.
double arcsel_draw_stats_panel(u32 param_1, u32 param_2, float *param_3, int param_4,
                               u32 param_5, u32 param_6, u32 param_7, u32 param_8)
{
    uint uVar1;
    uint uVar2;
    int iVar3;
    uint uVar4;
    int iVar5;
    int iVar11;
    u32 uVar7 = 0;
    u32 uVar10 = 0;
    byte *pbVar8;
    float *pfVar9;
    uint uVar13;
    uint uVar14;
    uint uVar15;
    double dVar17 = 0.0, dVar18 = 0.0, dVar19, dVar20, dVar21;
    double in_f5 = 0.0, in_f6 = 0.0, in_f7 = 0.0, in_f8 = 0.0;
    byte auStack_1e8[200];
    byte auStack_120[200];
    uint colorMain;
    u32 *pfVar12;

    iVar3 = FUN_803826dc();
    dVar21 = (double)*(float *)(iVar3 + 0xf0);
    pfVar9 = param_3;
    iVar11 = param_4;
    uVar4 = FUN_803825c0((double)(FLOAT_8061a444 * (float)((double)FLOAT_8061a590 * dVar21)));
    pfVar12 = (u32 *)(uVar4 & 0xff | 0x5a144600);
    uVar4 = FUN_803825c0((double)(FLOAT_8061a444 * (float)((double)FLOAT_8061a480 * dVar21)));
    iVar3 = DAT_805015d8;
    colorMain = uVar4 & 0xff | 0x64646400;
    iVar5 = worm_mode_get_current_target();
    get_string_by_id((iVar5 < 0) ? 0x15f5 : 0x15f7);
    gx_set_sub_material_id(1);
    text_set_font_table(0x16);
    dVar20 = (double)FLOAT_8061a490;
    dVar19 = (double)(float)(dVar20 + (double)*param_3);
    uVar1 = (uint)(dVar20 + (double)*param_3);
    uVar2 = (uint)((double)param_3[2] - dVar20);
    uVar13 = (uint)(FLOAT_8061a508 + param_3[1]);

    if ((iVar3 == 0) || (iVar3 == 2)) {
        if (param_4 != 0) {
            iVar5 = get_string_by_id(0x674);
            debug_log_formatted((char *)&DAT_80611120, (u64)iVar5, uVar10, uVar7,
                                in_f5, in_f6, in_f7, in_f8, (u32)(size_t)&DAT_80611120,
                                (u32)iVar5, (u32)(size_t)pfVar9, (u32)iVar11, param_5,
                                param_6, param_7, param_8);
            dVar17 = (double)(float)((double)CONCAT44(0x43300000, uVar13 ^ 0x80000000) -
                                     DOUBLE_8061a430);
            text_draw_char_shadowed(
                (double)(float)((double)CONCAT44(0x43300000, uVar1 ^ 0x80000000) - DOUBLE_8061a430),
                dVar17, (byte *)&DAT_80611120, colorMain, (u32)(size_t)pfVar12);
            dVar21 = worm_stat_get_snapshot(0, *(int *)DAT_80611d74);
            pbVar8 = (byte *)&DAT_806110ec;
            debug_log_formatted((char *)&DAT_806110ec, (u64)(int)dVar21, uVar10, uVar7, in_f5, in_f6,
                                in_f7, in_f8, (u32)(size_t)&DAT_806110ec, (u32)(int)dVar21,
                                (u32)(size_t)pfVar9, uVar10, param_5, param_6, param_7, param_8);
            dVar21 = (double)text_get_char_width(pbVar8);
            text_draw_char_shadowed(
                (double)(float)((double)(float)((double)CONCAT44(0x43300000, uVar2 ^ 0x80000000) -
                                                DOUBLE_8061a430) - dVar21),
                dVar17, pbVar8, colorMain, (u32)(size_t)pfVar12);
        }
        uVar13 = uVar13 + 0x28;
    }

    if ((iVar3 == 0) || (iVar3 == 1)) {
        if (param_4 != 0) {
            iVar5 = get_string_by_id(0x677);
            debug_log_formatted((char *)&DAT_80611120, (u64)iVar5, uVar10, uVar7, in_f5, in_f6,
                                in_f7, in_f8, (u32)(size_t)&DAT_80611120, (u32)iVar5,
                                (u32)(size_t)pfVar9, (u32)iVar11, param_5, param_6, param_7, param_8);
            dVar17 = (double)(float)((double)CONCAT44(0x43300000, uVar13 ^ 0x80000000) -
                                     DOUBLE_8061a430);
            text_draw_char_shadowed(
                (double)(float)((double)CONCAT44(0x43300000, uVar1 ^ 0x80000000) - DOUBLE_8061a430),
                dVar17, (byte *)&DAT_80611120, colorMain, (u32)(size_t)pfVar12);
            dVar21 = worm_stat_get_snapshot(3, *(int *)DAT_80611d74);
            pbVar8 = (byte *)&DAT_806110ec;
            debug_log_formatted((char *)&DAT_806110ec, (u64)(int)dVar21, uVar10, uVar7, in_f5, in_f6,
                                in_f7, in_f8, (u32)(size_t)&DAT_806110ec, (u32)(int)dVar21,
                                (u32)(size_t)pfVar9, uVar10, param_5, param_6, param_7, param_8);
            dVar21 = (double)text_get_char_width(pbVar8);
            text_draw_char_shadowed(
                (double)(float)((double)(float)((double)CONCAT44(0x43300000, uVar2 ^ 0x80000000) -
                                                DOUBLE_8061a430) - dVar21),
                dVar17, pbVar8, colorMain, (u32)(size_t)pfVar12);
            dVar21 = worm_stat_get_snapshot(3, *(int *)DAT_80611d74);
            if (dVar21 != (double)FLOAT_8061a418) {
                iVar5 = get_string_by_id(0x678);
                debug_log_formatted((char *)&DAT_80611120, (u64)iVar5, uVar10, uVar7, in_f5, in_f6,
                                    in_f7, in_f8, (u32)(size_t)&DAT_80611120, (u32)iVar5,
                                    (u32)(size_t)pfVar9, (u32)iVar11, param_5, param_6, param_7,
                                    param_8);
                dVar18 = (double)(float)((double)CONCAT44(0x43300000, (uVar13 + 0x28) ^ 0x80000000) -
                                         DOUBLE_8061a430);
                text_draw_char_shadowed(
                    (double)(float)((double)CONCAT44(0x43300000, uVar1 ^ 0x80000000) -
                                    DOUBLE_8061a430),
                    dVar18, (byte *)&DAT_80611120, colorMain, (u32)(size_t)pfVar12);
                iVar11 = get_string_by_id(0x3f0);
                dVar21 = worm_stat_get_snapshot(3, *(int *)DAT_80611d74);
                dVar17 = worm_stat_get_snapshot(4, *(int *)DAT_80611d74);
                pbVar8 = (byte *)&DAT_80611124;
                debug_log_formatted((char *)&DAT_80611124,
                                    (u64)(float)(FLOAT_8061a440 * (float)(dVar17 / dVar21)), uVar10,
                                    uVar7, in_f5, in_f6, in_f7, in_f8, (u32)(size_t)&DAT_80611124,
                                    (u32)iVar11, (u32)(size_t)pfVar9, uVar10, param_5, param_6,
                                    param_7, param_8);
                dVar21 = (double)text_get_char_width(pbVar8);
                text_draw_char_shadowed(
                    (double)(float)((double)(float)((double)CONCAT44(0x43300000, uVar2 ^ 0x80000000) -
                                                    DOUBLE_8061a430) - dVar21),
                    dVar18, pbVar8, colorMain, (u32)(size_t)pfVar12);
            }
        }
        uVar13 = uVar13 + 0x50;
    }

    if (iVar3 == 2) {
        if (param_4 != 0) {
            iVar5 = get_string_by_id(0x675);
            debug_log_formatted((char *)&DAT_80611120, (u64)iVar5, uVar10, uVar7, in_f5, in_f6,
                                in_f7, in_f8, (u32)(size_t)&DAT_80611120, (u32)iVar5,
                                (u32)(size_t)pfVar9, (u32)iVar11, param_5, param_6, param_7, param_8);
            dVar17 = (double)(float)((double)CONCAT44(0x43300000, uVar13 ^ 0x80000000) -
                                     DOUBLE_8061a430);
            text_draw_char_shadowed(
                (double)(float)((double)CONCAT44(0x43300000, uVar1 ^ 0x80000000) - DOUBLE_8061a430),
                dVar17, (byte *)&DAT_80611120, colorMain, (u32)(size_t)pfVar12);
            dVar21 = worm_stat_get_snapshot(1, *(int *)DAT_80611d74);
            pbVar8 = (byte *)&DAT_806110ec;
            debug_log_formatted((char *)&DAT_806110ec, (u64)(int)dVar21, uVar10, uVar7, in_f5, in_f6,
                                in_f7, in_f8, (u32)(size_t)&DAT_806110ec, (u32)(int)dVar21,
                                (u32)(size_t)pfVar9, uVar10, param_5, param_6, param_7, param_8);
            dVar21 = (double)text_get_char_width(pbVar8);
            text_draw_char_shadowed(
                (double)(float)((double)(float)((double)CONCAT44(0x43300000, uVar2 ^ 0x80000000) -
                                                DOUBLE_8061a430) - dVar21),
                dVar17, pbVar8, colorMain, (u32)(size_t)pfVar12);
            iVar5 = get_string_by_id(0x676);
            debug_log_formatted((char *)&DAT_80611120, (u64)iVar5, uVar10, uVar7, in_f5, in_f6,
                                in_f7, in_f8, (u32)(size_t)&DAT_80611120, (u32)iVar5,
                                (u32)(size_t)pfVar9, uVar10, param_5, param_6, param_7, param_8);
            dVar17 = (double)(float)((double)CONCAT44(0x43300000, (uVar13 + 0x28) ^ 0x80000000) -
                                     DOUBLE_8061a430);
            text_draw_char_shadowed(
                (double)(float)((double)CONCAT44(0x43300000, uVar1 ^ 0x80000000) - DOUBLE_8061a430),
                dVar17, (byte *)&DAT_80611120, colorMain, (u32)(size_t)pfVar12);
            dVar21 = worm_stat_get_snapshot(2, *(int *)DAT_80611d74);
            pbVar8 = (byte *)&DAT_8061112c;
            debug_log_formatted((char *)&DAT_8061112c, (u64)(int)dVar21, uVar10, uVar7, in_f5, in_f6,
                                in_f7, in_f8, (u32)(size_t)&DAT_8061112c, (u32)(int)dVar21,
                                (u32)(size_t)pfVar9, uVar10, param_5, param_6, param_7, param_8);
            dVar21 = (double)text_get_char_width(pbVar8);
            text_draw_char_shadowed(
                (double)(float)((double)(float)((double)CONCAT44(0x43300000, uVar2 ^ 0x80000000) -
                                                DOUBLE_8061a430) - dVar21),
                dVar17, pbVar8, colorMain, (u32)(size_t)pfVar12);
        }
        uVar13 = uVar13 + 0x50;
    }

    uVar15 = uVar13;
    if (((uint)(iVar3 - 6) < 2) || (iVar3 == 8)) {
        uVar14 = uVar13 + 200;
        uVar15 = uVar13 + 200;
        if (param_4 != 0) {
            static const int leagueLabels[6] = {0x679, 0x67a, 0x67b, 0x67c, 0x67e, 0x67d};
            static const int leagueRows[6]   = {5, 6, 7, 8, 9, 10};
            int slotY = uVar13;
            for (int col = 0; col < 5; ++col) {
                iVar5 = get_string_by_id(leagueLabels[col]);
                debug_log_formatted((char *)&DAT_80611120, (u64)iVar5, uVar10, uVar7, in_f5, in_f6,
                                    in_f7, in_f8, (u32)(size_t)&DAT_80611120, (u32)iVar5,
                                    (u32)(size_t)pfVar9, uVar10, param_5, param_6, param_7, param_8);
                dVar17 = (double)(float)((double)CONCAT44(0x43300000, (uint)slotY ^ 0x80000000) -
                                         DOUBLE_8061a430);
                text_draw_char_shadowed(
                    (double)(float)((double)CONCAT44(0x43300000, uVar1 ^ 0x80000000) -
                                    DOUBLE_8061a430),
                    dVar17, (byte *)&DAT_80611120, colorMain, (u32)(size_t)pfVar12);
                dVar21 = worm_stat_get_snapshot(leagueRows[col], *(int *)DAT_80611d74);
                if ((leagueRows[col] == 7) || (leagueRows[col] == 8)) {
                    byte *dst = (leagueRows[col] == 7) ? auStack_120 : auStack_1e8;
                    time_format_long((char *)dst, (int)dVar21);
                    pbVar8 = dst;
                } else {
                    pbVar8 = (byte *)&DAT_80611130;
                    debug_log_formatted((char *)&DAT_80611130, (u64)(int)dVar21, uVar10, uVar7,
                                        in_f5, in_f6, in_f7, in_f8, (u32)(size_t)&DAT_80611130,
                                        (u32)(int)dVar21, (u32)(size_t)pfVar9, uVar10, param_5,
                                        param_6, param_7, param_8);
                }
                dVar21 = (double)text_get_char_width(pbVar8);
                text_draw_char_shadowed(
                    (double)(float)((double)(float)((double)CONCAT44(0x43300000, uVar2 ^ 0x80000000) -
                                                    DOUBLE_8061a430) - dVar21),
                    dVar17, pbVar8, colorMain, (u32)(size_t)pfVar12);
                slotY += 0x28;
            }
            // final row: league rank/points 0x67d with either "-" or "mm:ss.d"
            uVar13 = (uint)worm_stat_get_snapshot(10, *(int *)DAT_80611d74);
            iVar5 = get_string_by_id(0x67d);
            debug_log_formatted((char *)&DAT_80611120, (u64)iVar5, uVar10, uVar7, in_f5, in_f6,
                                in_f7, in_f8, (u32)(size_t)&DAT_80611120, (u32)iVar5,
                                (u32)(size_t)pfVar9, uVar10, param_5, param_6, param_7, param_8);
            dVar21 = (double)(float)((double)CONCAT44(0x43300000, uVar14 ^ 0x80000000) -
                                     DOUBLE_8061a430);
            text_draw_char_shadowed(
                (double)(float)((double)CONCAT44(0x43300000, uVar1 ^ 0x80000000) - DOUBLE_8061a430),
                dVar21, (byte *)&DAT_80611120, colorMain, (u32)(size_t)pfVar12);
            if (uVar13 == 0) {
                pbVar8 = (byte *)s__________80477744;
                debug_log_formatted((char *)s__________80477744, 0, uVar10, uVar7, in_f5, in_f6,
                                    in_f7, in_f8, (u32)(size_t)s__________80477744, 0,
                                    (u32)(size_t)pfVar9, uVar10, param_5, param_6, param_7, param_8);
            } else {
                int total = (int)uVar13 / (int)DAT_806109c0;
                int frac = (int)((double)FLOAT_8061a4b4 *
                                 (double)(float)((double)(int)uVar13 / (double)(int)DAT_806109c0));
                pbVar8 = (byte *)s__02d__02d__01d_80477734;
                debug_log_formatted((char *)s__02d__02d__01d_80477734, 0, uVar10, uVar7, in_f5,
                                    in_f6, in_f7, in_f8, (u32)((total / 0x3c) % 0x3c),
                                    (u32)(total % 0x3c), (u32)(frac % 10), (u32)frac, 0x3c,
                                    (u32)total, param_8, 0);
            }
            dVar21 = (double)text_get_char_width(pbVar8);
            text_draw_char_shadowed(
                (double)(float)((double)(float)((double)CONCAT44(0x43300000, uVar2 ^ 0x80000000) -
                                                DOUBLE_8061a430) - dVar21),
                (double)(float)((double)CONCAT44(0x43300000, uVar14 ^ 0x80000000) - DOUBLE_8061a430),
                pbVar8, colorMain, (u32)(size_t)pfVar12);
            uVar15 = uVar14;
        }
    }

    FUN_80382728();
    (void)dVar18;
    (void)param_1;
    (void)param_2;
    return (double)(float)((double)CONCAT44(0x43300000, (uVar15 + 0x28) ^ 0x80000000) -
                           DOUBLE_8061a430);
}

// -----------------------------------------------------------------------------
// arcsel_draw_stats_panel_visible @ 0x802fb28c
// Thunk: draw the stats panel (param_4 = 1). Returns the running Y.
double arcsel_draw_stats_panel_visible(u32 param_1, u32 param_2, float *param_3, u32 param_4,
                                       u32 param_5, u32 param_6, u32 param_7, u32 param_8)
{
    (void)param_4;
    return arcsel_draw_stats_panel(param_1, param_2, param_3, 1, param_5, param_6, param_7, param_8);
}

// -----------------------------------------------------------------------------
// arcsel_build_stats_panel_screen @ 0x802fb2b0
// Build the stats-panel screen: measure the panel height, install auto-advance
// callbacks, add a horizontally-scrolled spacer rendering arcsel_draw_stats_panel_visible,
// and centre the element.
void arcsel_build_stats_panel_screen(int param_1)
{
    double dVar1;
    float local_28[4];

    local_28[0] = (float)DAT_803e0988;
    local_28[1] = (float)DAT_803e098c;
    local_28[2] = (float)DAT_803e0990;
    local_28[3] = (float)DAT_803e0994;
    dVar1 = arcsel_draw_stats_panel(param_1, 0, local_28, 0, 0, 0, 0, 0);
    *(void **)(param_1 + 0x1c0) = (void *)&arcsel_auto_advance_if_ready;
    *(void **)(param_1 + 0x1c4) = (void *)&arcsel_auto_advance_next;
    hud_element_init_from_layout(0xc, 0, 0, 0);
    hud_element_scroll_horizontal((double)FLOAT_8061a58c, 0);
    hud_add_spacer_item((float)(dVar1 * (double)FLOAT_80611a5c), 0,
                        (int)(size_t)&arcsel_draw_stats_panel_visible);
    hud_element_apply_scroll();
    hud_element_apply_center_xy((int *)DAT_806119e0);
}

// -----------------------------------------------------------------------------
// arcsel_draw_team_results_table @ 0x802fb384
// Renders (or measures, param_12 == 0) the team results table: for each of the two
// teams that has classes (statlvl_count_classes_by_team), draws the team result
// string (award_result_string) then one row per class via arcsel_render_row_label.
// If neither team has classes, draws the "none" (0x173d) row. Returns nothing; the
// height feeds the containing spacer through the register return recovered by the
// __arcsel_render_row_label_d alias for each row measurement.
void arcsel_draw_team_results_table(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                                    u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                                    u32 param_9, u32 param_10, float *param_11, int param_12)
{
    uint uVar1;
    uint uVar2;
    uint uVar3;
    int iVar5;
    int iVar6;
    byte *pbVar7;
    int iVar8;
    int iVar10;
    int iVar12;
    uint team;
    uint uVar13;
    double dVar14;
    double dVar15;
    u64 uVar18;
    uint local_a4 = 0;
    uint uStack_a8 = 0;

    (void)param_9;
    (void)param_10;
    uVar18 = FUN_803826d0();
    iVar10 = (int)((u64)(u32)uVar18);      // element ptr (low word of the pair)
    uVar1 = (uint)*param_11;
    uVar2 = (uint)param_11[2];
    uVar13 = (uint)param_11[1];
    iVar12 = 0;
    if ((int)uVar18 != 0) {
        hud_item_compute_colors(iVar10, (int)uVar18, &local_a4, &uStack_a8);
    }
    team = 0;
    do {
        iVar5 = statlvl_count_classes_by_team(team, DAT_806117b4);
        if (0 < iVar5) {
            iVar12 = iVar12 + 1;
            iVar5 = session_get_state_flag();
            text_set_font_table((iVar5 < 3) ? 9 : 3);
            if (param_12 != 0) {
                pbVar7 = (byte *)award_result_string(team);
                text_draw_char(
                    (double)(float)((double)CONCAT44(0x43300000, uVar1 ^ 0x80000000) -
                                    DOUBLE_8061a430),
                    (double)(float)((double)CONCAT44(0x43300000, uVar13 ^ 0x80000000) -
                                    DOUBLE_8061a430),
                    pbVar7, local_a4);
            }
            uVar13 = (uint)(FLOAT_8061a594 * FLOAT_80611674 +
                            (float)((double)CONCAT44(0x43300000, uVar13 ^ 0x80000000) -
                                    DOUBLE_8061a430));
            for (iVar5 = 0; iVar6 = statlvl_count_classes_by_team(team, DAT_806117b4), iVar5 < iVar6;
                 iVar5 = iVar5 + 1) {
                iVar6 = statlvl_find_class_by_team_index(iVar5, team, DAT_806117b4);
                iVar8 = session_get_state_flag();
                text_set_font_table((iVar8 < 3) ? 3 : 0x12);
                uVar3 = (uint)((double)FLOAT_8061a480 * (double)*(float *)(iVar10 + 0xf0));
                double rowY = (double)(float)((double)CONCAT44(0x43300000, uVar13 ^ 0x80000000) -
                                              DOUBLE_8061a430);
                dVar14 = __arcsel_render_row_label_d(
                    (double)FLOAT_80611674, rowY, param_3, param_4, param_5, param_6, param_7,
                    param_8, iVar6, iVar10, uVar1, uVar13, uVar2,
                    (uint)(rowY + (double)FLOAT_80611674), uVar3, param_12);
                iVar6 = session_get_state_flag();
                text_set_font_table((iVar6 < 3) ? 3 : 0x12);
                uVar13 = (uint)((float)((double)CONCAT44(0x43300000, uVar13 ^ 0x80000000) -
                                        DOUBLE_8061a430) +
                                (float)((double)FLOAT_8061a444 * (double)FLOAT_80611674 + dVar14));
            }
            uVar13 = (uint)((float)((double)CONCAT44(0x43300000, uVar13 ^ 0x80000000) -
                                    DOUBLE_8061a430) + FLOAT_80611674);
        }
        team = team + 1;
    } while ((int)team < 2);

    if (iVar12 == 0) {
        pbVar7 = (byte *)get_string_by_id(0x173d);
        iVar10 = session_get_state_flag();
        text_set_font_table((iVar10 < 3) ? 9 : 3);
        if (param_12 != 0) {
            byte *pbVar11 = (byte *)award_result_string(0);
            text_draw_char(
                (double)(float)((double)CONCAT44(0x43300000, uVar1 ^ 0x80000000) - DOUBLE_8061a430),
                (double)(float)((double)CONCAT44(0x43300000, uVar13 ^ 0x80000000) - DOUBLE_8061a430),
                pbVar11, local_a4);
        }
        uVar1 = (uint)(FLOAT_8061a594 * FLOAT_80611674 +
                       (float)((double)CONCAT44(0x43300000, uVar13 ^ 0x80000000) - DOUBLE_8061a430));
        iVar10 = session_get_state_flag();
        text_set_font_table((iVar10 < 3) ? 3 : 0x12);
        if (param_12 != 0) {
            dVar15 = text_get_char_width(pbVar7);
            text_draw_char(
                (double)(float)((double)(float)((double)CONCAT44(0x43300000, uVar2 ^ 0x80000000) -
                                                DOUBLE_8061a430) - dVar15),
                (double)(float)((double)CONCAT44(0x43300000, uVar1 ^ 0x80000000) - DOUBLE_8061a430),
                pbVar7, local_a4);
        }
        uVar13 = (uint)((float)((double)CONCAT44(0x43300000, uVar1 ^ 0x80000000) - DOUBLE_8061a430) +
                        FLOAT_80611674);
    }
    (void)uVar13;
    FUN_8038271c();
}

// -----------------------------------------------------------------------------
// arcsel_draw_team_results_visible @ 0x802fb788
// Viewport-gated wrapper: when the active viewport is not 3/4/6, draw the team
// results table (draw pass) via arcsel_draw_team_results_table.
void arcsel_draw_team_results_visible(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                                      u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                                      u32 param_9, u32 param_10, float *param_11)
{
    int iVar1;
    u64 uVar3;

    (void)param_1;
    (void)param_9;
    (void)param_10;
    uVar3 = (u64)(u32)FUN_803826f0();
    iVar1 = hud_screen_get_active_viewport();
    if ((iVar1 != 3) && (iVar1 = hud_screen_get_active_viewport(), iVar1 != 4) &&
        (iVar1 = hud_screen_get_active_viewport(), iVar1 != 6)) {
        arcsel_draw_team_results_table(0, param_2, param_3, param_4, param_5, param_6, param_7,
                                       param_8, (u32)(uVar3 >> 0x20), (u32)uVar3, param_11, 1);
    }
    FUN_8038273c();
}

// =============================================================================
// Batch: batch_856 | Module: arcade-select challenge/mission results screen
// Address range: 0x802fb7f8 - 0x802fc308
//
// The arcade-select challenge/mission results screen. arcsel_draw_challenge_stats
// renders the mission target name + timer (worm_mode_get_*), the player-name
// strings, and the per-difficulty challenge records (stats_get_challenge_record)
// each with a hud_marker_draw difficulty medal. arcsel_is_mission_mode gates the
// challenge vs. team-results layout; arcsel_draw_challenge_stats_visible is the
// draw-pass render callback; arcsel_build_challenge_screen assembles the screen
// (challenge stats spacer + handlers for mission mode, else the team-results table
// from batch_855). arcsel_build_results_header builds the map/mission title header.
// The remaining helpers are small gates/handlers: arcsel_results_auto_advance_ready
// (DAT_80612bb0 state), arcsel_mode_needs_confirm (per-mode config), and
// arcsel_open_mode_submenu (copy mode config block + push submenu).
//
// NON-MATCHING: arcsel_draw_challenge_stats is declared returning double here (its
// register-return running-Y was dropped by the void auto-signature); the batch_855
// arcsel_draw_team_results_table (void) is called through the typed fn-ptr alias
// __arcsel_draw_team_results_table_d to recover its return. time_format_long /
// debug_log_formatted use the shared void buffer-writer / 16-arg contracts. The
// PowerPC int->float magic-bias idiom is preserved; register-spill FP args are
// modelled as locals.

// arcsel_draw_challenge_stats is declared (double) in generated_decls.h (C++ linkage).
double arcsel_draw_challenge_stats(u32 param_1, float *param_3, int param_4,
                                   u32 param_5, u32 param_6, u32 param_7, u32 param_8);

// typed fn-ptr aliases (leading __ so gen_externs skips them) to recover the
// dropped register-return of void-declared helpers:
typedef double (*ArcselTeamResultsRetFn)(u64, u64, u64, u64, u64, u64, u64, u64,
                                         u32, u32, float *, int);
static const ArcselTeamResultsRetFn __arcsel_draw_team_results_table_d =
    (ArcselTeamResultsRetFn)0x802fb384;
typedef int (*WormMissionDispatchRetFn)(void);
static const WormMissionDispatchRetFn __worm_mission_mode_dispatch_r =
    (WormMissionDispatchRetFn)0x8022a8a0;
typedef byte *(*WormFormatObjRetFn)(double, double, double, double, double, double, double, double);
static const WormFormatObjRetFn __worm_mode_format_objective_line_p =
    (WormFormatObjRetFn)0x8022b580;
typedef byte *(*WormResultsLineRetFn)(double, double, double, double, double, double, double, double,
                                      int, int, int, int, int, int);
static const WormResultsLineRetFn __worm_results_format_line_p =
    (WormResultsLineRetFn)0x8022a8dc;

extern "C" {
int  FUN_80300634(void);            // challenge-screen action (mission restart/next)

extern int   DAT_80612bb0;          // results auto-advance state (2 == ready)
extern int   DAT_80612bc8;          // results auto-advance latch
extern int   DAT_803f2dd8;          // per-sub-mode config table (0x20 stride)
extern int   DAT_803f31e4;          // per-mode submenu id table (0xf stride)
extern int   DAT_805cc640;          // mode submenu working config block (0x98 bytes)
extern u8    DAT_805cc68c;          // mode submenu selected-type byte
extern int   DAT_805cc634;          // mode submenu selected item index
extern int   DAT_80501570;          // mode config source block
extern int   DAT_803e0998;          // challenge-screen default rect (x,y,w,h)
extern int   DAT_803e099c;
extern int   DAT_803e09a0;
extern int   DAT_803e09a4;

extern float FLOAT_8061a598;
}

// -----------------------------------------------------------------------------
// arcsel_draw_challenge_stats @ 0x802fb7f8
// Renders (or measures, param_4 == 0) the challenge/mission results stats block:
// the mission target name + record time (worm_mode_get_* / time_format_long), the
// player-name strings (worm_mode_get_player_name_strings), and the per-difficulty
// challenge records from stats_get_challenge_record, each with a difficulty medal
// drawn via hud_marker_draw. Returns the running Y baseline.
double arcsel_draw_challenge_stats(u32 param_1, float *param_3, int param_4,
                                   u32 param_5, u32 param_6, u32 param_7, u32 param_8)
{
    uint uVar1;
    int iVar3;
    uint uVar4;
    int iVar5;
    byte *pbVar6;
    int *piVar8;
    byte *puVar9;
    uint uVar10;
    float *pfVar11;
    uint uVar12;
    uint uVar13;
    uint uVar14;
    uint uVar15;
    uint uVar16;
    double dVar17;
    double dVar18;
    double dVar19, dVar20;
    double in_f5 = 0.0, in_f6 = 0.0, in_f7 = 0.0, in_f8 = 0.0;
    double dVar21, dVar22;
    int local_198;
    int local_194;
    byte abStack_190[256];
    u64 uVar23;

    uVar23 = FUN_803826c8();
    iVar3 = (int)((u64)(u32)uVar23);          // element ptr
    pfVar11 = param_3;
    dVar21 = (double)*(float *)(iVar3 + 0xf0);
    uVar4 = FUN_803825c0((double)(FLOAT_8061a444 * (float)((double)FLOAT_8061a590 * dVar21)));
    uVar16 = uVar4 & 0xff | 0x5a144600;
    uVar4 = FUN_803825c0((double)(FLOAT_8061a444 * (float)((double)FLOAT_8061a480 * dVar21)));
    uVar15 = uVar4 & 0xff | 0x64646400;
    iVar5 = __worm_mission_mode_dispatch_r();
    uVar12 = *(uint *)(iVar5 + 0x18);
    uVar4 = worm_mode_get_current_target();
    uVar4 = uVar4 >> 0x1f ^ 1;
    if (uVar4 == 0) {
        worm_mode_get_next_target_time();
    } else {
        worm_mode_get_current_target();
    }
    pbVar6 = (byte *)get_string_by_id((uVar4 != 0) ? 0x15f7 : 0x15f5);
    gx_set_sub_material_id(1);
    text_set_font_table(0x16);
    dVar20 = (double)FLOAT_8061a490;
    dVar19 = (double)(float)(dVar20 + (double)*pfVar11);
    uVar4 = (uint)(dVar20 + (double)*pfVar11);
    uVar1 = (uint)((double)pfVar11[2] - dVar20);
    uVar14 = (uint)(FLOAT_8061a508 + pfVar11[1]);
    dVar21 = 0.0;

    if ((uVar12 & 0x10) == 0) {
        if (param_4 != 0) {
            dVar21 = (double)(float)((double)CONCAT44(0x43300000, uVar14 ^ 0x80000000) -
                                     DOUBLE_8061a430);
            text_draw_char_shadowed(
                (double)(float)((double)CONCAT44(0x43300000, uVar4 ^ 0x80000000) - DOUBLE_8061a430),
                dVar21, pbVar6, uVar15, uVar16);
            time_format_long((char *)abStack_190, (int)worm_mode_get_current_target());
            dVar21 = (double)text_get_char_width(abStack_190);
            text_draw_char_shadowed(
                (double)(float)((double)(float)((double)CONCAT44(0x43300000, uVar1 ^ 0x80000000) -
                                                DOUBLE_8061a430) - dVar21),
                (double)(float)((double)CONCAT44(0x43300000, uVar14 ^ 0x80000000) - DOUBLE_8061a430),
                abStack_190, uVar15, uVar16);
        }
        uVar14 = (uint)((float)((double)CONCAT44(0x43300000, uVar14 ^ 0x80000000) -
                                DOUBLE_8061a430) + FLOAT_8061a598);
        dVar21 = 0.0;
    }

    local_194 = 0;
    local_198 = 0;
    worm_mode_get_player_name_strings(&local_194, &local_198, 0);
    byte *pName2 = (byte *)(intptr_t)local_198;
    if ((param_4 != 0) && (local_194 != 0)) {
        debug_log_formatted((char *)&DAT_80611120, (u64)local_194, 0, 0, in_f5, in_f6, in_f7, in_f8,
                            (u32)(size_t)&DAT_80611120, (u32)local_194, 0, 0, param_5, param_6,
                            param_7, param_8);
        text_draw_char_shadowed(
            (double)(float)((double)CONCAT44(0x43300000, uVar4 ^ 0x80000000) - DOUBLE_8061a430),
            (double)(float)((double)CONCAT44(0x43300000, uVar14 ^ 0x80000000) - DOUBLE_8061a430),
            (byte *)&DAT_80611120, uVar15, uVar16);
    }
    if (pName2 != (byte *)0x0) {
        if (param_4 != 0) {
            dVar17 = (double)text_get_char_width(pName2);
            text_draw_char_shadowed(
                (double)(float)((double)(float)((double)CONCAT44(0x43300000, uVar1 ^ 0x80000000) -
                                                DOUBLE_8061a430) - dVar17),
                (double)(float)((double)CONCAT44(0x43300000, uVar14 ^ 0x80000000) - DOUBLE_8061a430),
                pName2, uVar15, uVar16);
        }
        uVar14 = uVar14 + 0x32;
    }

    text_set_font_table(0x15);
    if (param_4 != 0) {
        pbVar6 = (byte *)get_string_by_id(0x3d1);
        text_draw_char_shadowed(
            (double)(float)((double)CONCAT44(0x43300000, (uVar4 + 0x20) ^ 0x80000000) -
                            DOUBLE_8061a430),
            (double)(float)((double)CONCAT44(0x43300000, uVar14 ^ 0x80000000) - DOUBLE_8061a430),
            pbVar6, uVar15, uVar16);
    }
    dVar17 = (double)text_get_char_width(pbVar6);
    uVar10 = (uint)dVar17;

    iVar5 = DAT_805015d8;
    piVar8 = (int *)stats_get_challenge_record(0, DAT_805015d8);
    if (*piVar8 == 0) {
        pbVar6 = (byte *)get_string_by_id(0x3d2);
    } else {
        pbVar6 = __worm_results_format_line_p(dVar17, 0, dVar19, dVar20, in_f5, in_f6, in_f7,
                                              in_f8, DAT_805015d8, iVar5, 0, 0,
                                              (int)param_5, (int)param_6);
    }
    dVar17 = (double)text_get_char_width(pbVar6);
    uVar13 = (uint)dVar17;

    // measure the difficulty label columns (3..0)
    dVar22 = (double)FLOAT_8061a4b4;
    iVar5 = 3;
    do {
        pbVar6 = (byte *)difficulty_get_name_string(iVar5);
        dVar18 = (double)(float)(dVar22 + (double)text_get_char_width(pbVar6));
        if ((double)(float)((double)CONCAT44(0x43300000, uVar10 ^ 0x80000000) - DOUBLE_8061a430) <=
            dVar18) {
            dVar18 = (double)FLOAT_8061a4b4 + (double)text_get_char_width(pbVar6);
        } else {
            dVar18 = (double)(float)((double)CONCAT44(0x43300000, uVar10 ^ 0x80000000) -
                                     DOUBLE_8061a430);
        }
        uVar10 = (uint)dVar18;
        pbVar6 = __worm_mode_format_objective_line_p(dVar18, 0, dVar19, dVar20, in_f5, in_f6,
                                                     in_f7, in_f8);
        dVar18 = (double)text_get_char_width(pbVar6);
        if ((double)(float)((double)CONCAT44(0x43300000, uVar13 ^ 0x80000000) - DOUBLE_8061a430) >
            dVar18) {
            dVar18 = (double)(float)((double)CONCAT44(0x43300000, uVar13 ^ 0x80000000) -
                                     DOUBLE_8061a430);
        }
        uVar13 = (uint)dVar18;
        iVar5 = iVar5 + -1;
    } while (0 < iVar5);

    if ((int)((uVar1 - uVar4) - (uVar13 + uVar10)) < 0) {
        text_set_font_table(0x14);
    }
    iVar5 = DAT_805015d8;
    piVar8 = (int *)stats_get_challenge_record(0, DAT_805015d8);
    if (*piVar8 == 0) {
        pbVar6 = (byte *)get_string_by_id(0x3d2);
    } else {
        pbVar6 = __worm_results_format_line_p(0, 0, dVar19, dVar20, in_f5, in_f6, in_f7, in_f8,
                                              DAT_805015d8, iVar5, 0, 0, (int)param_5, (int)param_6);
    }
    if (param_4 != 0) {
        dVar17 = (double)text_get_char_width(pbVar6);
        text_draw_char_shadowed(
            (double)(float)((double)(float)((double)CONCAT44(0x43300000, uVar1 ^ 0x80000000) -
                                            DOUBLE_8061a430) - dVar17),
            (double)(float)((double)CONCAT44(0x43300000, uVar14 ^ 0x80000000) - DOUBLE_8061a430),
            pbVar6, uVar15, uVar16);
        puVar9 = (byte *)stats_get_challenge_record(0, DAT_805015d8);
        hud_marker_draw(*(u32 *)(puVar9 + 0xc), uVar4, uVar14, 0x16, 0x16);
    }

    iVar5 = 3;
    uVar14 = uVar14 + 0x1d;
    do {
        gx_set_sub_material_id(1);
        if (param_4 != 0) {
            int nameId = difficulty_get_name_string(iVar5);
            debug_log_formatted((char *)&DAT_80611120, (u64)nameId, 0, 0, in_f5, in_f6, in_f7,
                                in_f8, (u32)(size_t)&DAT_80611120, (u32)nameId, 0, 0, param_5,
                                param_6, param_7, param_8);
            text_draw_char_shadowed(
                (double)(float)((double)CONCAT44(0x43300000, (uVar4 + 0x20) ^ 0x80000000) -
                                DOUBLE_8061a430),
                (double)(float)((double)CONCAT44(0x43300000, uVar14 ^ 0x80000000) - DOUBLE_8061a430),
                (byte *)&DAT_80611120, uVar15, uVar16);
            pbVar6 = __worm_mode_format_objective_line_p(0, 0, dVar19, dVar20, in_f5, in_f6,
                                                         in_f7, in_f8);
            uVar12 = color_fade_team_c((float)(FLOAT_8061a444 * *(float *)(iVar3 + 0xf0)), iVar5);
            uVar10 = color_fade_team_b(*(float *)(iVar3 + 0xf0), iVar5);
            dVar17 = (double)text_get_char_width(pbVar6);
            text_draw_char_shadowed(
                (double)(float)((double)(float)((double)CONCAT44(0x43300000, uVar1 ^ 0x80000000) -
                                                DOUBLE_8061a430) - dVar17),
                (double)(float)((double)CONCAT44(0x43300000, uVar14 ^ 0x80000000) - DOUBLE_8061a430),
                pbVar6, uVar10, uVar12);
            hud_marker_draw(iVar5, uVar4, uVar14, 0x16, 0x16);
        }
        iVar5 = iVar5 + -1;
        uVar14 = uVar14 + 0x16;
    } while (0 < iVar5);

    FUN_80382714();
    (void)dVar21;
    (void)param_1;
    return (double)(float)((double)CONCAT44(0x43300000, uVar14 ^ 0x80000000) - DOUBLE_8061a430);
}

// -----------------------------------------------------------------------------
// arcsel_is_mission_mode @ 0x802fbe74
// Returns 1 when the session flags DAT_805015c0 indicate a mission/challenge mode
// (bit 0x40 or 0x8000000), else 0.
int arcsel_is_mission_mode(void)
{
    if (((DAT_805015c0 & 0x40) == 0) && ((DAT_805015c0 & 0x8000000) == 0)) {
        return 0;
    }
    return 1;
}

// -----------------------------------------------------------------------------
// arcsel_draw_challenge_stats_visible @ 0x802fbe9c
// Render callback: in mission mode, draw the challenge stats block (draw pass)
// using the element's rect (+0xa4..0xb0).
void arcsel_draw_challenge_stats_visible(int param_1, u32 param_2, u32 param_3, u32 param_4,
                                         u32 param_5, u32 param_6, u32 param_7, u32 param_8)
{
    int iVar1;
    float local_18[4];

    (void)param_2;
    (void)param_3;
    iVar1 = arcsel_is_mission_mode();
    if (iVar1 != 0) {
        local_18[0] = *(float *)(param_1 + 0xa4);
        local_18[1] = *(float *)(param_1 + 0xa8);
        local_18[2] = local_18[0] + *(float *)(param_1 + 0xac);
        local_18[3] = local_18[1] + *(float *)(param_1 + 0xb0);
        arcsel_draw_challenge_stats((u32)param_1, local_18, 1, param_5, param_6, param_7, param_8);
    }
}

// -----------------------------------------------------------------------------
// arcsel_push_screen_simple @ 0x802fbf08
// hud_screen_push thunk.
void arcsel_push_screen_simple(void)
{
    hud_screen_push();
}

// -----------------------------------------------------------------------------
// arcsel_challenge_screen_action @ 0x802fbf3c
// Challenge-screen select/action handler; delegates to FUN_80300634.
void arcsel_challenge_screen_action(void)
{
    FUN_80300634();
}

// -----------------------------------------------------------------------------
// arcsel_build_challenge_screen @ 0x802fbf5c
// Builds the challenge/team-results screen. In mission mode it installs the
// select (arcsel_push_screen_simple) / action (arcsel_challenge_screen_action)
// handlers and draws arcsel_draw_challenge_stats via arcsel_draw_challenge_stats_visible;
// otherwise it draws the team-results table (batch_855) via arcsel_draw_team_results_visible.
// The panel is horizontally scrolled and scroll-ticked.
void arcsel_build_challenge_screen(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                                   u64 param_5, u64 param_6, u64 param_7, u64 param_8, int param_9)
{
    int iVar1;
    int iVar2;
    void (*pcVar3)(void);
    u32 in_r7 = 0, in_r8 = 0, in_r9 = 0, in_r10 = 0;
    double dVar4;
    float local_28[4];

    (void)param_1;
    local_28[0] = (float)DAT_803e0998;
    local_28[1] = (float)DAT_803e099c;
    local_28[2] = (float)DAT_803e09a0;
    local_28[3] = (float)DAT_803e09a4;
    iVar1 = arcsel_prompt_flag4_and_tracking();
    *(void **)(param_9 + 0x1c0) = (void *)&arcsel_auto_advance_if_ready;
    *(void **)(param_9 + 0x1c4) = (void *)&arcsel_auto_advance_next;
    arcsel_profile_slot_state(*(int *)(param_9 + 0x38));
    if (iVar1 == 0) {
        pcVar3 = 0;
        hud_element_init_from_layout(0xc, 0, 0, 0);
    } else {
        pcVar3 = &arcsel_challenge_screen_action;
        hud_element_init_from_layout(0x20c, 0, (int)(size_t)&arcsel_push_screen_simple,
                                     (int)(size_t)&arcsel_challenge_screen_action);
    }
    iVar2 = arcsel_is_mission_mode();
    if (iVar2 == 0) {
        dVar4 = __arcsel_draw_team_results_table_d(0, param_2, param_3, param_4, param_5, param_6,
                                                   param_7, param_8, param_9, 0, local_28, 0);
    } else {
        dVar4 = arcsel_draw_challenge_stats((u32)param_9, local_28, 0, in_r7, in_r8, in_r9, in_r10);
    }
    hud_element_scroll_horizontal((double)FLOAT_8061a58c, 0);
    iVar2 = arcsel_is_mission_mode();
    if (iVar2 == 0) {
        hud_add_spacer_item((float)(dVar4 * (double)FLOAT_80611a5c), 0,
                            (int)(size_t)&arcsel_draw_team_results_visible);
    } else {
        hud_add_spacer_item((float)(dVar4 * (double)FLOAT_80611a5c), 0,
                            (int)(size_t)&arcsel_draw_challenge_stats_visible);
    }
    hud_element_apply_scroll();
    arcsel_element_tick_scroll(param_9);
    if (iVar1 == 0) {
        *(float *)(param_9 + 0x20c) = FLOAT_8061a568;
    } else {
        iVar1 = get_string_by_id(0xb02);
        hud_screen_register_handler_global(0, iVar1, 0, 0);
        iVar1 = get_string_by_id(0x19c8);
        hud_screen_register_handler_global(1, iVar1, 0, 0);
        *(float *)(param_9 + 0x20c) = FLOAT_8061a4f8;
    }
    (void)pcVar3;
}

// -----------------------------------------------------------------------------
// arcsel_build_results_header @ 0x802fc148
// Builds the results header list: the map name (award_get_map_name_string) for
// standard modes, or the two mission title strings (worm_mission_mode_dispatch)
// for mission/challenge modes.
void arcsel_build_results_header(int param_1)
{
    short *psVar1;

    hud_element_init_from_layout(0xc, 0, 0, 0);
    *(void **)(param_1 + 0x1c0) = (void *)&arcsel_auto_advance_if_ready;
    *(void **)(param_1 + 0x1c4) = (void *)&arcsel_auto_advance_next;
    hud_set_item_padding_mode((double)FLOAT_8061a418, 1, 1, 1);
    hud_set_context_property(0x20204);
    hud_element_scroll_horizontal((double)FLOAT_8061a570, 0);
    if (((DAT_805015c0 & 0x40) == 0) && ((DAT_805015c0 & 0x8000000) == 0)) {
        award_get_map_name_string();
        hud_add_list_item();
    } else {
        psVar1 = (short *)(intptr_t)__worm_mission_mode_dispatch_r();
        get_string_by_id((int)*psVar1);
        hud_add_list_item();
        get_string_by_id((int)psVar1[1]);
        hud_add_list_item();
    }
    hud_element_apply_scroll();
    hud_element_apply_center_xy((int *)DAT_806119e0);
}

// -----------------------------------------------------------------------------
// arcsel_results_auto_advance_ready @ 0x802fc25c
// Returns 1 when the results auto-advance state DAT_80612bb0 == 2 (also sets the
// latch DAT_80612bc8 and clears the ticker state bit 0x400), else 0.
int arcsel_results_auto_advance_ready(void)
{
    if (DAT_80612bb0 == 2) {
        DAT_80612bc8 = 1;
        *(uint *)DAT_805cf280 = *(uint *)DAT_805cf280 & 0xfffffbff;
        return 1;
    }
    return 0;
}

// -----------------------------------------------------------------------------
// arcsel_mode_needs_confirm @ 0x802fc294
// Returns 1 when the current sub-mode config (DAT_803f2dd8[DAT_805015be*0x20]) is
// type 1, or type 2 with the per-difficulty settings flag set (game_get_settings_ptr
// + DAT_805015bd*0x14 + 0x24).
int arcsel_mode_needs_confirm(void)
{
    byte *puVar1;

    puVar1 = (byte *)game_get_settings_ptr();
    if ((*(int *)(&DAT_803f2dd8 + (uint)DAT_805015be * 0x20) == 1) ||
        ((*(int *)(&DAT_803f2dd8 + (uint)DAT_805015be * 0x20) == 2) &&
         (*(int *)(puVar1 + (uint)DAT_805015bd * 0x14 + 0x24) != 0))) {
        return 1;
    }
    return 0;
}

// -----------------------------------------------------------------------------
// arcsel_open_mode_submenu @ 0x802fc308
// Copy the mode config block (leaf_31e8 0x805cc640 <- 0x80501570, 0x98 bytes), set
// DAT_805cc68c from the per-mode table DAT_803f31e4 and DAT_805cc634 to the item
// index, then push the submenu screen.
void arcsel_open_mode_submenu(int param_1)
{
    int iVar1 = *(int *)(param_1 + 0x188);
    leaf_31e8((u32)(uintptr_t)&DAT_805cc640, (u32)(uintptr_t)&DAT_80501570, 0x98);
    DAT_805cc68c = (u8)(&DAT_803f31e4)[iVar1 * 0xf];
    DAT_805cc634 = iVar1;
    hud_screen_push();
}

// =============================================================================
// Batch: batch_857 | Module: arcade-select mode-grid submenus + scoreboard row
// Address range: 0x802fc39c - 0x802fc8c0
//
// The arcade-select mode-selection submenus and the multiplayer scoreboard row
// renderer. arcsel_build_mode_grid_screen / arcsel_build_mode_grid2_screen build
// the two vertically-scrolled mode-grid submenus (menu_mode_grid[2]_populate) with
// their pop handlers (arcsel_grid_screen_pop / arcsel_grid2_screen_pop);
// arcsel_mode_grid_select is the grid item-select handler. arcsel_build_match_settings_screen
// builds the match settings/loadout screen (menu_match_settings_list_build +
// menu_loadout_settings_to_items) and arcsel_apply_match_settings writes the items
// back into the settings block. arcsel_commit_transition_thunk forwards to
// arcsel_commit_transition (batch_831). arcsel_layout_scoreboard_center computes the
// centered scoreboard element bounds per viewport, and arcsel_draw_scoreboard_row
// renders one scoreboard row (name/sprite + kills + deaths + score/prefetch columns).
//
// NON-MATCHING: FUN_80387fb8 (the formatted text builder) and the frame-actor head/
// tail thunks resolve as before; hud_element_scroll_vertical takes int args in the
// shared decl; hud_sprite_draw returns void (called for side-effect). The PowerPC
// int->float magic-bias idiom is preserved; register-spill FP args are modelled as
// locals.

extern "C" {
int  FUN_803826d8(void);            // hud active-screen element ptr (scoreboard-row path)
void FUN_80382724(void);            // hud screen finalize (scoreboard-row path)

extern u8    DAT_805cc68e;          // mode-grid selected item index
extern u8    DAT_805cc68d;          // mode-grid selected sub-type
extern int   DAT_803f2dd0;          // per-mode config table (0x20 stride; sub-type at +0)
extern int   DAT_805015dc;          // scoreboard "hide score" flag
}

// arcsel_prefetch_profiles is void in the shared decl (its register-return count was
// dropped); recover it via a typed fn-ptr alias to the ROM address.
typedef u32 (*ArcselPrefetchRetFn)(void);
static const ArcselPrefetchRetFn __arcsel_prefetch_profiles_r = (ArcselPrefetchRetFn)0x802f41ec;

// -----------------------------------------------------------------------------
// arcsel_grid2_screen_pop @ 0x802fc39c
void arcsel_grid2_screen_pop(void)
{
    hud_screen_pop_with_restore();
}

// -----------------------------------------------------------------------------
// arcsel_build_mode_grid2_screen @ 0x802fc3d0
// Builds the secondary mode-grid submenu (menu_mode_grid2_populate) with vertical
// scroll, centering and ok/back global handlers.
void arcsel_build_mode_grid2_screen(int param_1)
{
    int iVar1;

    hud_element_init_from_layout(0x200, 0, 0, (int)(size_t)&arcsel_grid2_screen_pop);
    hud_set_item_padding_mode((double)FLOAT_8061a494, 1, 1, 1);
    hud_element_scroll_vertical((int)FLOAT_8061a418, 5);
    menu_mode_grid2_populate();
    hud_element_apply_scroll();
    hud_element_apply_center_xy((int *)DAT_806119e0);
    iVar1 = get_string_by_id(0x19c7);
    hud_screen_register_handler_global(0, iVar1, 0, 0);
    iVar1 = get_string_by_id(0x19c8);
    hud_screen_register_handler_global(1, iVar1, 0, 0);
    *(float *)(param_1 + 0x20c) = FLOAT_8061a494;
}

// -----------------------------------------------------------------------------
// arcsel_mode_grid_select @ 0x802fc498
// Mode-grid item select: record the item index (DAT_805cc68e) and its sub-type
// from the per-mode config table (DAT_805cc68d = DAT_803f2dd0[index*0x20]); if the
// current mode type (DAT_805cc68c) is 0x26 or 0x1b, flip it to 0x1b/0x26 based on
// whether the selected sub-type is 3; then push the screen.
void arcsel_mode_grid_select(int param_1)
{
    DAT_805cc68e = (u8)*(uint *)(param_1 + 0x188);
    DAT_805cc68d = (char)*(int *)(&DAT_803f2dd0 + (*(uint *)(param_1 + 0x188) & 0xff) * 0x20);
    if ((DAT_805cc68c == '&') || ((DAT_805cc68c < '&') && (DAT_805cc68c == '\x1b'))) {
        if (DAT_805cc68d == '\x03') {
            DAT_805cc68c = '\x1b';
        } else {
            DAT_805cc68c = '&';
        }
    }
    hud_screen_push();
}

// -----------------------------------------------------------------------------
// arcsel_grid_screen_pop @ 0x802fc540
void arcsel_grid_screen_pop(void)
{
    hud_screen_pop_with_restore();
}

// -----------------------------------------------------------------------------
// arcsel_build_mode_grid_screen @ 0x802fc574
// Builds the mode-grid submenu (menu_mode_grid_populate) with vertical scroll,
// centering and ok/back global handlers.
void arcsel_build_mode_grid_screen(int param_1)
{
    int iVar1;

    hud_element_init_from_layout(0x200, 0, 0, (int)(size_t)&arcsel_grid_screen_pop);
    hud_set_item_padding_mode((double)FLOAT_8061a494, 1, 1, 1);
    hud_element_scroll_vertical((int)FLOAT_8061a418, 5);
    menu_mode_grid_populate();
    hud_element_apply_scroll();
    hud_element_apply_center_xy((int *)DAT_806119e0);
    iVar1 = get_string_by_id(0x19c7);
    hud_screen_register_handler_global(0, iVar1, 0, 0);
    iVar1 = get_string_by_id(0x19c8);
    hud_screen_register_handler_global(1, iVar1, 0, 0);
    *(float *)(param_1 + 0x20c) = FLOAT_8061a494;
}

// -----------------------------------------------------------------------------
// arcsel_commit_transition_thunk @ 0x802fc63c
// 16-arg pass-through thunk to arcsel_commit_transition (batch_831).
void arcsel_commit_transition_thunk(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                                    u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                                    u32 param_9, u32 param_10, u32 param_11, u32 param_12,
                                    u32 param_13, u32 param_14, u32 param_15, u32 param_16)
{
    arcsel_commit_transition(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             param_9, param_10, param_11, param_12, param_13, param_14, param_15,
                             param_16);
}

// -----------------------------------------------------------------------------
// arcsel_build_match_settings_screen @ 0x802fc65c
// Builds the match settings/loadout screen: menu_match_settings_list_build then
// menu_loadout_settings_to_items (reading the live game settings block), centering
// and ok/back handlers.
void arcsel_build_match_settings_screen(int param_1)
{
    int iVar1;

    hud_element_init_from_layout(0x22200, 0, 0, 0);
    hud_set_item_padding_mode((double)FLOAT_8061a41c, 1, 1, 1);
    menu_match_settings_list_build();
    game_get_settings_ptr();
    menu_loadout_settings_to_items();
    hud_element_apply_center_xy((int *)DAT_806119e0);
    iVar1 = get_string_by_id(0x19c7);
    hud_screen_register_handler_global(0, iVar1, 0, 0);
    iVar1 = get_string_by_id(0x19c8);
    hud_screen_register_handler_global(1, iVar1, 0, 0);
    *(float *)(param_1 + 0x20c) = FLOAT_8061a494;
}

// -----------------------------------------------------------------------------
// arcsel_apply_match_settings @ 0x802fc720
// Apply the match-settings list items back into the live game settings block.
void arcsel_apply_match_settings(int param_1)
{
    int *piVar1 = (int *)game_get_settings_ptr();
    menu_loadout_items_to_settings(param_1, 1, piVar1);
}

// -----------------------------------------------------------------------------
// arcsel_layout_scoreboard_center @ 0x802fc75c
// Compute and apply the scoreboard element's centered X/Y bounds. The Y extent is
// derived from the scroll offset relative to the results context height
// (DAT_80611748 + 0x20); the X/Y placement varies by viewport count and the column
// position param_3 (0/1/2). Uses hud_element_apply_center_xy_bounds.
void arcsel_layout_scoreboard_center(u32 param_1, u32 param_2, uint param_3)
{
    float fVar1;
    int iVar3;
    double dVar4;
    double dVar5;
    double dVar6;
    u64 uVar7;

    (void)param_1;
    (void)param_2;
    uVar7 = (u64)(u32)FUN_803826f0();
    iVar3 = (int)uVar7;
    dVar4 = arcsel_menu_scroll_offset(0);
    dVar6 = (double)((FLOAT_8061a440 -
                      (float)(dVar4 / (double)(float)((double)CONCAT44(0x43300000,
                                                                       *(uint *)(DAT_80611748 + 0x20) ^
                                                                           0x80000000) -
                                                      DOUBLE_8061a430))) -
                     FLOAT_8061a488);
    dVar4 = dVar6;
    if (2 < iVar3) {
        dVar4 = (double)(float)((double)FLOAT_8061a444 * dVar6);
    }
    fVar1 = FLOAT_8061a58c;
    if (iVar3 == 1) {
        fVar1 = FLOAT_8061a440;
    }
    if (iVar3 == 3) {
        if (param_3 == 0) {
            dVar6 = (double)FLOAT_8061a4f8;
        } else {
            dVar6 = (double)(float)((double)FLOAT_8061a444 * dVar6 - (double)FLOAT_8061a488);
        }
    } else if (iVar3 == 4) {
        if (param_3 < 2) {
            dVar6 = (double)FLOAT_8061a4f8;
        } else {
            dVar6 = (double)(float)((double)FLOAT_8061a444 * dVar6 - (double)FLOAT_8061a488);
        }
    } else {
        dVar6 = (double)FLOAT_8061a4f8;
    }
    if (iVar3 == 3) {
        dVar5 = (param_3 == 2) ? (double)FLOAT_8061a58c : (double)FLOAT_8061a4f8;
    } else {
        bool onLeft = (param_3 == 2) || (param_3 == 0);
        dVar5 = onLeft ? (double)FLOAT_8061a4f8 : (double)FLOAT_8061a58c;
    }
    hud_element_apply_center_xy_bounds(dVar5, dVar6, (double)fVar1, dVar4,
                                       (int *)(intptr_t)(int)(uVar7 >> 0x20));
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// arcsel_draw_scoreboard_row @ 0x802fc8c0
// Renders one multiplayer scoreboard row for the profile at the active element
// (FUN_803826d8): the player name (or a HUD sprite when param_11 >= 0), the summed
// kill count across the other player slots, the death count, and the score column
// (arcsel_format_score) or, in online 4-player mode, the prefetched profile count.
// All numeric columns are right-aligned via text_get_char_width + shadowed draw.
void arcsel_draw_scoreboard_row(u64 param_1, double param_2, double param_3, u64 param_4,
                                u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                                u32 param_9, u32 param_10, int param_11, u32 param_12,
                                u32 param_13, u32 param_14, u32 param_15, u32 param_16)
{
    short sVar1;
    char *pcVar2;
    char *pcVar3;
    u32 nameId;
    float *pfVar5;
    int iVar8;
    int iVar9;
    int iVar10;
    int iVar11;
    double dVar12;
    double dVar13 = 0.0;
    double dVar14;
    double dVar15 = 0.0;
    u64 uVar16;
    byte abStack_88[112];

    (void)param_1;
    uVar16 = (u64)(u32)FUN_803826d8();
    pcVar2 = (char *)(intptr_t)(int)(uVar16 >> 0x20);   // profile record
    pfVar5 = (float *)(intptr_t)(int)uVar16;            // column-x layout
    iVar11 = 0;
    dVar12 = param_2;
    leaf_92c0(1);
    if (param_11 < 0) {
        FUN_80387fb8(dVar13, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     (int)abStack_88, &DAT_806110ec, *(int *)(pcVar2 + 0x48), (int)param_12,
                     (byte *)(uintptr_t)param_13, (byte *)(uintptr_t)param_14, param_15, param_16);
        text_draw_char_shadowed((double)*pfVar5, dVar15, abStack_88, param_13, param_14);
    } else {
        param_3 = (double)*pfVar5;
        hud_sprite_draw();
    }
    leaf_92c0(0);
    FUN_80387fb8(0, dVar15, param_3, param_4, param_5, param_6, param_7, param_8, (int)abStack_88,
                 &DAT_806110f8, (int)(intptr_t)(pcVar2 + 0x20), (int)param_12,
                 (byte *)(uintptr_t)param_13, (byte *)(uintptr_t)param_14, param_15, param_16);
    text_apply_alpha_fade(dVar12, abStack_88, 0, 1);
    text_draw_char_shadowed((double)pfVar5[1], dVar15, abStack_88, param_13, param_14);
    dVar12 = (double)FLOAT_8061a41c;
    text_set_color_rgb(dVar12, dVar12, dVar12);
    leaf_92c0(1);

    iVar10 = 0;
    iVar8 = 0;
    for (iVar9 = 0; iVar9 < DAT_80612610 + DAT_806120fc; iVar9 = iVar9 + 1) {
        if ((-1 < *(int *)((int)&DAT_805014a0 + iVar8)) &&
            (pcVar3 = (char *)profile_get_slot(*(int *)((int)&DAT_805014a0 + iVar8)),
             pcVar2[1] != pcVar3[1])) {
            iVar10 = iVar10 + *(short *)(pcVar2 + *pcVar3 * 2 + 2);
        }
        iVar8 = iVar8 + 4;
    }
    sVar1 = *(short *)(pcVar2 + 0x40);
    FUN_80387fb8(0, dVar12, dVar12, param_4, param_5, param_6, param_7, param_8, (int)abStack_88,
                 &DAT_806110ec, iVar10 + *(short *)(pcVar2 + 0x1e), (int)param_12,
                 (byte *)(uintptr_t)param_13, (byte *)(uintptr_t)param_14, param_15, param_16);
    dVar14 = (double)text_get_char_width(abStack_88);
    text_draw_char_shadowed((double)(float)((double)pfVar5[2] - dVar14), dVar15, abStack_88,
                            param_13, param_14);
    FUN_80387fb8(0, dVar15, dVar12, param_4, param_5, param_6, param_7, param_8, (int)abStack_88,
                 &DAT_806110ec, (int)sVar1, (int)param_12, (byte *)(uintptr_t)param_13,
                 (byte *)(uintptr_t)param_14, param_15, param_16);
    dVar14 = (double)text_get_char_width(abStack_88);
    text_draw_char_shadowed((double)(float)((double)pfVar5[3] - dVar14), dVar15, abStack_88,
                            param_13, param_14);

    if ((DAT_805015bd == '\t') && (DAT_805015be == '\x04')) {
        u32 count = __arcsel_prefetch_profiles_r();
        FUN_80387fb8(0, dVar15, dVar12, param_4, param_5, param_6, param_7, param_8, (int)abStack_88,
                     &DAT_806110ec, count, (int)param_12, (byte *)(uintptr_t)param_13,
                     (byte *)(uintptr_t)param_14, param_15, param_16);
    } else {
        iVar11 = arcsel_score_column_null_arg();
        iVar11 = (int)arcsel_format_score(0, (u64)dVar15, (u64)dVar12, param_4, param_5, param_6,
                                          param_7, param_8, (int)*pcVar2, *(u32 *)(pcVar2 + 0x48),
                                          *(int *)(pcVar2 + 0x4c), *(int *)(pcVar2 + 0x50),
                                          (int)abStack_88, iVar11, param_15, param_16);
    }
    if (((DAT_805015dc != 0) && (*(int *)(pcVar2 + 0x50) != 0)) || (iVar11 == -1)) {
        leaf_92c0(0);
    }
    dVar13 = (double)text_get_char_width(abStack_88);
    text_draw_char_shadowed((double)(float)((double)pfVar5[4] - dVar13), dVar15, abStack_88,
                            param_13, param_14);
    leaf_92c0(0);
    FUN_80382724();
}
