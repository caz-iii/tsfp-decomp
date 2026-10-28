// ============================================================================
// menu_loadout_preview_render.cpp
// Batch: batch_807
// Address range: 0x802cd020 - 0x802cdeac
// Date: 2026-09-11T04:40:00Z
//
// Loadout character-preview rendering + team/ready tally. Renders the animated
// D-pad control hint (rect-param variant), the character portrait swatch with
// optional team cursor, the composed preview cell, the loadout screen container
// (three sub-screens), and the per-player "ready" cursor rows plus the
// READY/WAITING status label. Continues the menu_loadout_* cluster (801-806).
//
// NON-MATCHING: several functions read incoming paired-single FP spill regs
// (f25..f31 / psXX_1); those spills are dead (copied to unused locals) so they
// are dropped. The alpha/scale value the decompiler surfaces as `extraout_f1`
// is the return of the preceding FUN_803825c0 alpha call; modelled as a local
// seeded from that call. 64-bit frame-actor thunks return a hi/lo pair split
// with shifts. CONCAT44 magic-bias int<->double conversions -> plain casts.
// Named helpers resolve via generated shared headers; only raw
// FUN_/DAT_/FLOAT_/DOUBLE_/PTR_ symbols get typed file-local externs.
// ============================================================================

#include "common.h"
#include "menu.h"    // pulls generated_decls.h (all named cross-module helpers)
#include "hud.h"
#include "sprite.h"
#include "text.h"
#include "savegame.h"

// ---- raw frame-actor / prologue thunks --------------------------------------
extern "C" long long FUN_803826bc(void);   // pair: hi=side row, lo=actor
extern "C" long long FUN_803826e4(void);    // pair: hi=screen, lo=slot idx
extern "C" long long FUN_803826d4(void);    // pair: hi=team idx, lo=accum ptr seed
extern "C" long long FUN_803826cc(void);    // pair: hi=team idx, lo=accum ptr seed
extern "C" long long __save_gpr_28(void);   // prologue: returns saved pair
extern "C" void      __restore_gpr_28(void);
extern "C" float     FUN_803825c0(double alpha);  // set frame-actor alpha, ret prev
extern "C" void      FUN_803642b4(int a, int b);  // bind portrait/model resource
extern "C" void      FUN_80382708(void);          // scope-end variants
extern "C" void      FUN_80382730(void);
extern "C" void      FUN_80382720(void);
extern "C" void      FUN_80382718(void);

// ---- PTR vtables for the sub-screen builders (batch_806 functions) ----------
extern "C" int PTR_menu_character_select_list_build_804763f0;
extern "C" int PTR_FUN_804763e0;
extern "C" int PTR_menu_loadout_screen_layout_build_804763d0;

// ---- raw data symbols -------------------------------------------------------
// Per-slot loadout arrays (stride 0x1c bytes == 7 ints).
extern "C" int   DAT_805cc744;   // [slot*7] character/actor id (-1 == empty)
extern "C" int   DAT_805cc748;   // [slot*7] +4: team-source id
extern "C" int   DAT_805cc750;   // [slot*7] ready flag
extern "C" char  DAT_805cc68e;
extern "C" int   DAT_805cc900;   // per-player team assignment array (stride 4)

// Character table (stride 0x30): actor id at [i*0xc] (int view), sprite id +8.
extern "C" int   DAT_80474680;   // char actor id [i*0xc]
extern "C" int   DAT_80474688;   // char portrait sprite id [i*0xc]
extern "C" int   DAT_8048eba8;   // per-side facing-angle float table (stride 0x14)
extern "C" int   DAT_8048ebb8;   // per-side button-mask table (stride 0x14)
extern "C" int   DAT_803f2c7c;   // per-team color table (stride 5 ints)

extern "C" int   DAT_80612614;
extern "C" int   DAT_80612b00;   // current previewed character index
extern "C" int   DAT_80612afc;
extern "C" int   DAT_80612af8;
extern "C" int   DAT_806119e0;   // active hud element handle
extern "C" int   DAT_80611014;   // active player count
extern "C" char  DAT_80612b18;   // per-player controller-status bytes
extern "C" int   DAT_8060f948;   // layout-mode flag (1 == alt)
extern "C" int*  DAT_80610a20;   // sprite record table base

extern "C" float FLOAT_80619eec;
extern "C" float FLOAT_80619f0c;
extern "C" float FLOAT_80619f48;
extern "C" float FLOAT_80619f50;
extern "C" float FLOAT_80619f58;
extern "C" float FLOAT_80619f6c;
extern "C" float FLOAT_80619f88;
extern "C" float FLOAT_80619f8c;
extern "C" float FLOAT_80619fb8;
extern "C" float FLOAT_80619fe0;
extern "C" float FLOAT_80619ee8;
extern "C" float FLOAT_8060f958;
extern "C" float FLOAT_80611674;
extern "C" float FLOAT_80611a64;
extern "C" float FLOAT_80611a68;
extern "C" float FLOAT_8061a000;
extern "C" float FLOAT_8061a004;
extern "C" float FLOAT_8061a008;
extern "C" float FLOAT_8061a00c;
extern "C" float FLOAT_8061a010;
extern "C" float FLOAT_8061a014;
extern "C" float FLOAT_8061a018;
extern "C" float FLOAT_8061a01c;
extern "C" float FLOAT_8061a020;
extern "C" float FLOAT_8060f958b;   // (reserved; not used)
extern "C" double DOUBLE_80619f00;
extern "C" double DOUBLE_80619f20;

// Char table accessors (int stride 0xc == 0x30 bytes).
#define CHAR_ACTOR(i)    ((&DAT_80474680)[(i) * 0xc])
#define CHAR_PORTRAIT(i) ((&DAT_80474688)[(i) * 0xc])

// ---------------------------------------------------------------------------
// 0x802cd020 : render the 4-way D-pad control hint using explicit rect params
// (a rect-parameterised variant of menu_dpad_indicator_render). param_7 selects
// the larger/font layout; param_6 is the base text color.
// ---------------------------------------------------------------------------
void menu_dpad_indicator_render_ex(int /*p1*/, int /*p2*/, unsigned int x0,
                                   unsigned int y0, int right, unsigned int color,
                                   int large)
{
    long long fa = FUN_803826bc();
    int row = (int)((unsigned long long)fa >> 0x20) * 0x14;
    int faLo = (int)fa;

    int tex = sprite_get_texture_id(0x159);
    int stepX = large ? 0x14 : 0xc;
    int gap   = large ? 0xc : 8;

    double bias = DOUBLE_80619f00;
    double baseX = (double)(float)(((double)(int)x0 - bias) - FLOAT_8061a000);
    double cursorX = (double)FLOAT_80619f50 + baseX;
    int rec = *DAT_80610a20 + tex * 0x58;
    double rowY = (double)(int)((double)((double)(int)y0 - bias)
                    + FLOAT_80619eec * (double)((double)(right - (gap + stepX * 3)) - bias));
    unsigned short uMax = *(unsigned short*)(rec + 0x4e);
    unsigned short vMax = *(unsigned short*)(rec + 0x52);
    double labelBase = (double)(float)(baseX - (double)((double)faLo - bias));

    text_set_font_table(large ? 0x12 : 0xf);

    int labels[4];
    labels[0] = get_string_by_id(0x42d);
    labels[1] = get_string_by_id(0x42c);
    labels[2] = get_string_by_id(0x42e);
    labels[3] = get_string_by_id(0x42f);

    int codes[4];
    codes[0] = menu_dir_indicator_code_from_angle((double)*(float*)(&DAT_8048eba8 + row));
    codes[1] = menu_dir_indicator_code_from_angle_mirror((double)*(float*)(&DAT_8048eba8 + row));
    codes[2] = menu_dir_code_from_buttons(*(unsigned int*)(&DAT_8048ebb8 + row));
    codes[3] = menu_dir_code_from_buttons_alt(*(unsigned int*)(&DAT_8048ebb8 + row));

    double f48  = (double)FLOAT_80619f48;
    double a004 = (double)FLOAT_8061a004;
    double a008 = (double)FLOAT_8061a008;
    unsigned int uCol = (unsigned int)uMax << 4;
    double curY = rowY;

    for (int i = 0, o = 0; i < 4; ++i, o += 4)
    {
        double codeD = (double)(int)codes[i];
        double scaleC = (double)(float)(f48 * (double)(float)(codeD - bias));
        int yTop = (int)((double)(float)((double)(curY + gap) - bias) - FLOAT_80611674);
        unsigned int glyphX = (unsigned int)(a004 * scaleC + (double)(float)((double)(int)cursorX - bias));
        unsigned int uSpan = (unsigned int)((double)(float)((double)(int)uCol - bias) * scaleC);

        gx_set_active_material(tex);
        gx_draw_sprite_rect((unsigned int)cursorX, yTop, glyphX, (unsigned int)(curY + gap),
                            0, 0, uSpan, (unsigned int)vMax << 4);

        unsigned char* label = (unsigned char*)(unsigned long long)labels[i];
        text_apply_alpha_fade(labelBase, label, 0, 1);
        double cw = text_get_char_width(label);
        double glyphLabelX = (double)(int)(baseX - cw);
        text_draw_char((double)(float)((double)glyphLabelX - bias),
                       -(double)(float)(a008 * (double)FLOAT_80611a64
                                        - (double)(float)((double)(int)curY - bias)),
                       label, color & 0xff | 0x64646400);
        double one = (double)FLOAT_80619ee8;
        text_set_color_rgb(one, one, one);
        curY += stepX;
    }
    FUN_80382708();
}

// ---------------------------------------------------------------------------
// 0x802cd3c8 : draw a character portrait swatch for the current slot, with an
// optional team-color cursor when team editing is enabled.
// ---------------------------------------------------------------------------
void menu_loadout_portrait_render(int /*p1*/, int /*p2*/, float* rect)
{
    long long fa = FUN_803826e4();
    int screen = (int)((unsigned long long)fa >> 0x20);
    int slot   = (int)fa;

    double left  = (double)(FLOAT_80619f88 * FLOAT_80611a68 + rect[0]);
    double top   = (double)(rect[1] + FLOAT_80611a64);
    double bottom = (double)(rect[3] - FLOAT_80611a64);

    FUN_803825c0((double)(FLOAT_80619f0c * *(float*)(screen + 0xf0)));

    double right = (double)(float)((double)FLOAT_8060f958 *
        (double)(FLOAT_80619f88 *
            (float)((double)(float)((double)FLOAT_8061a00c *
                (double)(float)(bottom - top) + left) - left)) + left);

    int tex = sprite_get_texture_id(CHAR_PORTRAIT(slot));
    int rec = *DAT_80610a20 + tex * 0x58;
    unsigned short uMax = *(unsigned short*)(rec + 0x4e);
    unsigned short vMax = *(unsigned short*)(rec + 0x52);
    gx_set_sub_material_id(1);
    gx_set_active_material(tex);
    gx_draw_sprite_quad_c4(left, top, right, bottom, 0, 0,
                           (unsigned int)uMax << 4, (unsigned int)vMax << 4);

    if (menu_loadout_can_edit_teams() != 0)
    {
        float cursorRect[4];
        cursorRect[0] = (float)(right - (double)FLOAT_8061a010);
        cursorRect[1] = (float)top;
        cursorRect[2] = (float)((double)FLOAT_80619f6c + right);
        cursorRect[3] = (float)((double)FLOAT_8061a014 + top);
        unsigned int color = (unsigned int)menu_slot_tier_scale(CHAR_ACTOR(slot));
        menu_loadout_cursor_render(screen, color, cursorRect);
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x802cd570 : compose the preview cell - portrait swatch + D-pad hint for the
// currently previewed character.
// ---------------------------------------------------------------------------
void menu_loadout_preview_cell_render(int screen, int /*p2*/, float* rect)
{
    menu_loadout_portrait_render(screen, DAT_80612b00, rect);
    double top = (double)rect[1];
    float prevAlpha = FUN_803825c0((double)(FLOAT_80619f0c * *(float*)(screen + 0xf0)));
    menu_dpad_indicator_render_ex(CHAR_ACTOR(DAT_80612b00), (int)rect[0],
                                  (unsigned int)(int)(rect[2] - FLOAT_80611a68),
                                  (unsigned int)(int)top,
                                  (int)((double)rect[3] - top),
                                  (unsigned int)prevAlpha, 1);
}

// ---------------------------------------------------------------------------
// 0x802cd64c : build the preview element (a spacer item whose render callback
// draws the preview cell), positioned on the loadout screen.
// ---------------------------------------------------------------------------
void menu_loadout_preview_build(void)
{
    DAT_80612b00 = 0;
    hud_element_init_from_layout(0x10, (void*)0, 0, 0);
    hud_add_spacer_item((float)FLOAT_8061a018, 0, (int)menu_loadout_preview_cell_render);
    hud_element_set_pos_x((double)FLOAT_80619f6c, DAT_806119e0);
    hud_element_set_pos_y((double)FLOAT_8061a01c, DAT_806119e0);
    hud_element_finalize_frame();
}

// ---------------------------------------------------------------------------
// 0x802cd6b8 : build the loadout screen container - creates the character
// select list, (optional) preview, and slot layout sub-screens, then shows it.
// ---------------------------------------------------------------------------
void menu_loadout_container_build(int screen)
{
    int title = get_string_by_id(0x366);
    hud_element_init_from_layout(0x40000000, (void*)(unsigned long long)(unsigned int)title, 0, 0);
    hud_element_create(screen, (unsigned int)(unsigned long long)&PTR_menu_character_select_list_build_804763f0, 2);
    if (DAT_80612614 != 0x66)
        hud_element_create(screen, (unsigned int)(unsigned long long)&PTR_FUN_804763e0, 2);
    hud_element_create(screen, (unsigned int)(unsigned long long)&PTR_menu_loadout_screen_layout_build_804763d0, 2);
    hud_screen_show_by_id(screen, -0x7fb89c30);
    DAT_80612afc = 0;
    DAT_80612af8 = 0;
}

// ---------------------------------------------------------------------------
// 0x802cd764 : empty hook stub.
// ---------------------------------------------------------------------------
void menu_loadout_noop_stub(void)
{
}

// ---------------------------------------------------------------------------
// 0x802cd768 : count ready human (non-bot, non-story-AI) slots; returns the
// number of contributing teams (via mode_team_count per contributor).
// ---------------------------------------------------------------------------
int menu_loadout_count_ready_humans(void)
{
    FUN_803826e4();
    int result = 0;
    int off = 0;
    for (int slot = 0; slot < 10; ++slot)
    {
        if (menu_slot_is_extra_bot(slot) == 0 &&
            (DAT_805cc68e != '\x13' ||
             player_ai_is_story_mode(*(int*)((int)&DAT_805cc744 + off)) == 0) &&
            *(int*)((int)&DAT_805cc750 + off) != 0 &&
            *(int*)((int)&DAT_805cc744 + off) >= 0)
        {
            result = mode_team_count();
        }
        off += 0x1c;
    }
    FUN_80382730();
    return result;
}

// ---------------------------------------------------------------------------
// 0x802cd84c : draw the per-player ready cursors down the left column
// (iterates active players high->low). accum points to the running x offset.
// ---------------------------------------------------------------------------
void menu_loadout_ready_cursors_left(int /*p1*/, unsigned int* accum, unsigned int flags, int teamMode)
{
    long long fa = FUN_803826d4();
    int teamIdx = (int)((unsigned long long)fa >> 0x20);
    unsigned int* px = (unsigned int*)(unsigned long long)(unsigned int)fa;
    if (accum) px = accum;

    int player = DAT_80611014 - 1;
    float alpha = FUN_803825c0((double)(float)((double)FLOAT_80619f58 * 1.0));
    double dAlpha = (double)alpha;
    int off = player * 4;

    for (; player >= 0; --player, off -= 4)
    {
        bool teamOk = (mode_feature_enabled_check() == 0) ||
                      (teamIdx == *(int*)((int)&DAT_805cc900 + off) + 1);
        if (teamOk && (3 < (unsigned char)(&DAT_80612b18)[player]))
        {
            if ((int)flags < 0)
            {
                *px += 1;
            }
            else
            {
                bool useTeamColor = (mode_feature_enabled_check() != 0) && (teamMode != 0);
                unsigned int color = useTeamColor ? (&DAT_803f2c7c)[teamIdx * 5] : 0x7f7f7f7f;
                FUN_803825c0((double)(float)((double)(float)((double)(color & 0xff) - DOUBLE_80619f20) * dAlpha));
                int* block = (int*)savegame_get_player_block(player);
                FUN_803642b4(*(int*)((char*)block + 0x60), 1);
                // NON-MATCHING: engine cursor_draw_at_size takes one packed
                // position arg (see batch_804); the decompiler's 2nd (y) operand
                // is folded away to match the shared 1-arg contract.
                cursor_draw_at_size((double)(FLOAT_8061a010 * FLOAT_8060f958 + (float)((double)(int)*px)));
                cursor_draw_at_size((double)(FLOAT_80619fb8 * FLOAT_8060f958 + (float)((double)(int)*px)));
                unsigned int adv = (DAT_8060f948 == 1) ? 10 : 0xc;
                *px = (int)(FLOAT_8060f958 * (float)((double)(int)adv) + (float)((double)(int)*px));
            }
        }
    }
    FUN_80382720();
}

// ---------------------------------------------------------------------------
// 0x802cdaac : draw the per-player ready cursors down the right column
// (iterates slots 9->0, mirrored x advance, team-filtered).
// ---------------------------------------------------------------------------
void menu_loadout_ready_cursors_right(int /*p1*/, unsigned int* accum, unsigned int flags, int teamMode)
{
    long long fa = FUN_803826cc();
    int teamIdx = (int)((unsigned long long)fa >> 0x20);
    unsigned int* px = (unsigned int*)(unsigned long long)(unsigned int)fa;
    if (accum) px = accum;

    float alpha = FUN_803825c0((double)(float)((double)FLOAT_80619f58 * 1.0));
    double dAlpha = (double)alpha;
    int off = 0xfc;

    for (int slot = 9; slot >= 0; --slot, off -= 0x1c)
    {
        int teamCount = mode_team_count();
        int* actorPtr = (int*)((int)&DAT_805cc744 + off);
        int teamSrc = *(int*)((int)&DAT_805cc748 + off);
        bool teamOk = (mode_feature_enabled_check() == 0) ||
                      ((teamSrc - (teamSrc / teamCount) * teamCount) + 1 == teamIdx);
        if (teamOk && menu_slot_is_extra_bot(slot) == 0 &&
            (DAT_805cc68e != '\x13' || player_ai_is_story_mode(*actorPtr) == 0) &&
            *(int*)((int)&DAT_805cc750 + off) != 0 && *actorPtr >= 0)
        {
            if ((int)flags < 0)
            {
                *px += 1;
            }
            else
            {
                bool useTeamColor = (mode_feature_enabled_check() != 0) && (teamMode != 0);
                unsigned int color = useTeamColor ? (&DAT_803f2c7c)[teamIdx * 5] : 0x7f7f7f7f;
                FUN_803825c0((double)(float)((double)(float)((double)(color & 0xff) - DOUBLE_80619f20) * dAlpha));
                FUN_803642b4(*actorPtr, 1);
                // NON-MATCHING: 1-arg cursor_draw_at_size contract (see above).
                cursor_draw_at_size((double)(FLOAT_8061a020 * FLOAT_8060f958 + (float)((double)(int)*px)));
                cursor_draw_at_size(-(double)(FLOAT_80619fb8 * FLOAT_8060f958 - (float)((double)(int)*px)));
                unsigned int adv = (DAT_8060f948 == 1) ? 10 : 0xc;
                *px = (int)-(FLOAT_8060f958 * (float)((double)(int)adv) - (float)((double)(int)*px));
            }
        }
    }
    FUN_80382718();
}

// ---------------------------------------------------------------------------
// 0x802cdd48 : top-level ready-status renderer - draws the ready cursors (per
// team when team-play is on) plus a READY / WAITING status label.
// ---------------------------------------------------------------------------
void menu_loadout_ready_status_render(void)
{
    long long saved = __save_gpr_28();
    unsigned int accum[2];
    accum[0] = (unsigned int)((unsigned long long)saved >> 0x20);
    unsigned int flags = (unsigned int)saved;
    double dAlpha = 1.0;

    gx_set_sub_material_id(1);
    if (menu_loadout_count_ready_humans() != 0)
    {
        if (mode_feature_enabled_check() == 0)
        {
            menu_loadout_ready_cursors_right(0, accum, flags, 0);
        }
        else
        {
            int team = mode_team_count();
            while (team - 1 >= 0)
            {
                menu_loadout_ready_cursors_right(team, accum, flags, 1);
                team -= 1;
            }
        }
    }

    if (DAT_8060f948 != 1)
    {
        unsigned char* label;
        if (menu_loadout_count_ready_humans() == 0)
            label = (unsigned char*)(unsigned long long)get_string_by_id(0x470);
        else
            label = (unsigned char*)(unsigned long long)get_string_by_id(0x46f);
        text_set_font_table(0x15);
        unsigned int c0 = (unsigned int)FUN_803825c0((double)(float)((double)FLOAT_80619fe0 * dAlpha));
        unsigned int c1 = (unsigned int)FUN_803825c0((double)(float)((double)FLOAT_80619f0c * dAlpha));
        double cw = text_get_char_width(label);
        text_draw_char_shadowed(
            (double)((float)((double)(float)((double)(int)accum[0]) - cw) - FLOAT_80619f8c),
            (double)(float)((double)(int)(flags - 2)),
            label,
            c1 & 0xff | 0x64646400,
            c0 & 0xff | 0xa0a1e00);
    }
    __restore_gpr_28();
}
