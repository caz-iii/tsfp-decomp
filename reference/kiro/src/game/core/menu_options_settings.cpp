// ============================================================================
// menu_options_settings.cpp
// Batch: batch_808
// Address range: 0x802cdecc - 0x802cea8b
// Date: 2026-09-11T05:20:00Z
//
// Game options / settings screen: loads and snapshots the working setting
// values from the game settings block, detects when they diverge (dirty),
// renders the option rows (icons + slider progress bars), ticks the screen to
// handle +/- adjustments on the focused item, and applies/cancels the settings
// (writing them back + popping the screen). Also renders both ready-cursor
// columns for the loadout screen. Continues the menu_* cluster (801-807).
//
// NON-MATCHING: the __save_gpr_28 prologue pair and FUN_803826e8 frame-actor
// pair are split into hi/lo with shifts. extraout_f1-style alpha values come
// from the preceding FUN_803825c0 call. CONCAT44 magic-bias conversions are
// plain casts. Named helpers resolve via generated shared headers; only raw
// FUN_/DAT_/FLOAT_/DOUBLE_ symbols get typed file-local externs.
// ============================================================================

#include "common.h"
#include "menu.h"    // pulls generated_decls.h (all named cross-module helpers)
#include "hud.h"
#include "sprite.h"
#include "sound.h"

// ---- raw frame-actor / prologue thunks --------------------------------------
extern "C" long long FUN_803826e8(void);    // pair: hi=screen, lo=item
extern "C" long long __save_gpr_28(void);    // prologue: saved pair
extern "C" void      __restore_gpr_28(void);
extern "C" float     FUN_803825c0(double alpha);   // set frame-actor alpha
extern "C" double    FUN_802f20a0(double v);        // pulse/flash modulation
extern "C" void      FUN_80382734(void);            // scope-end
extern "C" int       FUN_8039eadc(void);            // widescreen/aspect query
extern "C" void      FUN_8039eb5c(char v);          // apply aspect setting
extern "C" void      FUN_803af118(int v);           // apply related toggle
extern "C" void      FUN_802eec10(int a);           // teardown helper
extern "C" void      FUN_802ce50c_thunk(int a);     // (unused placeholder)

// ---- raw data symbols -------------------------------------------------------
extern "C" int   DAT_80611004;    // working: sound mode / speaker config
extern "C" int   DAT_80611008;    // working: aspect ratio (0=off else idx+1)
extern "C" int   DAT_8061100c;    // working: music volume (0..0x10)
extern "C" int   DAT_80611010;    // working: sfx volume (0..0x10)
extern "C" int   DAT_80612ab0;
extern "C" int   DAT_80612ab4;
extern "C" int   DAT_80612ab8;    // working: brightness (-0x40..0x40)
extern "C" int   DAT_80612abc;    // working: subtitle/other option
extern "C" int   DAT_80612614;
extern "C" int   DAT_80612bd4;
extern "C" int   DAT_80611a54;
extern "C" int   DAT_80611a58;
extern "C" int   DAT_805cc60c;
extern "C" int*  DAT_80610a20;    // sprite record table base

extern "C" float FLOAT_80619eec;
extern "C" float FLOAT_80619ee8;
extern "C" float FLOAT_80619f0c;
extern "C" float FLOAT_80619f6c;
extern "C" float FLOAT_80619fd8;
extern "C" float FLOAT_8061a014;
extern "C" float FLOAT_8061a024;
extern "C" float FLOAT_8061a02c;
extern "C" float FLOAT_8061a030;
extern "C" float FLOAT_8061a034;

// ---------------------------------------------------------------------------
// 0x802cdecc : render both ready-cursor columns (right then left) for the
// loadout screen; per-team when team-play is enabled.
// ---------------------------------------------------------------------------
void menu_loadout_ready_cursors_render(int /*p1*/, int p2, unsigned int flags)
{
    unsigned int accumR = (unsigned int)p2;
    unsigned int accumL = 0;   // seeded from param_1 in the original (unused x-base)

    gx_set_sub_material_id(1);
    if (mode_feature_enabled_check() == 0)
    {
        menu_loadout_ready_cursors_right(0, &accumR, flags, 0);
        menu_loadout_ready_cursors_left(0, &accumL, flags, 0);
    }
    else
    {
        int team = mode_team_count();
        while (team - 1 >= 0)
        {
            menu_loadout_ready_cursors_right(team, &accumR, flags, 1);
            menu_loadout_ready_cursors_left(team, &accumL, flags, 1);
            team -= 1;
        }
    }
}

// ---------------------------------------------------------------------------
// 0x802cdfa0 : snapshot the persisted game settings into the working option
// globals, and (re)start music playback.
// ---------------------------------------------------------------------------
void menu_options_settings_load(void)
{
    char* s = (char*)game_get_settings_ptr();
    DAT_80612ab0 = *(int*)(s + 0x114);
    DAT_80612ab4 = 0;
    s = (char*)game_get_settings_ptr();
    DAT_80611004 = *(int*)(s + 0x118);
    s = (char*)game_get_settings_ptr();
    DAT_80611008 = *(int*)(s + 0x11c);
    if (DAT_80611008 != 0)
    {
        int wide = FUN_8039eadc();
        DAT_80611008 = wide + 1;
    }
    s = (char*)game_get_settings_ptr();
    DAT_8061100c = *(int*)(s + 0x120);
    s = (char*)game_get_settings_ptr();
    DAT_80611010 = *(int*)(s + 0x124);
    s = (char*)game_get_settings_ptr();
    DAT_80612ab8 = *(int*)(s + 300);
    s = (char*)game_get_settings_ptr();
    DAT_80612abc = *(int*)(s + 0x130);
    music_start_playback();
}

// ---------------------------------------------------------------------------
// 0x802ce030 : return 1 if any working option differs from its persisted
// value (settings are dirty), else 0.
// ---------------------------------------------------------------------------
int menu_options_settings_dirty(void)
{
    char* s = (char*)game_get_settings_ptr();
    if (DAT_8061100c == *(int*)(s + 0x120) &&
        (s = (char*)game_get_settings_ptr(), DAT_80611004 == *(int*)(s + 0x118)) &&
        (s = (char*)game_get_settings_ptr(), DAT_80611008 == *(int*)(s + 0x11c)) &&
        (s = (char*)game_get_settings_ptr(), DAT_80612ab8 == *(int*)(s + 300)) &&
        (s = (char*)game_get_settings_ptr(), DAT_80611010 == *(int*)(s + 0x124)) &&
        (s = (char*)game_get_settings_ptr(), DAT_80612abc == *(int*)(s + 0x130)))
    {
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// 0x802ce0d0 : draw the option-row state icon (on/off/mid glyph) at the row.
// ---------------------------------------------------------------------------
void menu_options_row_icon_render(int /*p1*/, int /*p2*/, float* rect)
{
    long long fa = __save_gpr_28();
    int screen = (int)((unsigned long long)fa >> 0x20);
    int item   = (int)fa;
    int sprite = -1;

    FUN_803825c0((double)(FLOAT_80619f0c * *(float*)(screen + 0xf0)));
    gx_set_sub_material_id(1);

    int state = *(int*)(item + 0x14);
    if (state == 0)      sprite = 0x158;
    else if (state == 2) sprite = 0x150;
    else if (state == 4) sprite = 0x151;

    if (sprite != -1)
    {
        double top  = (double)rect[1];
        double left = (double)(float)((double)FLOAT_80619f6c + (double)rect[0]);
        double y0   = (double)(float)((double)FLOAT_80619f6c + top);
        int tex = sprite_get_texture_id(sprite);
        gx_set_active_material(tex);
        int rec = *DAT_80610a20 + tex * 0x58;
        gx_draw_sprite_quad_c4(left, y0,
                               (double)(float)((double)FLOAT_8061a024 + left),
                               (double)(float)((double)FLOAT_8061a014 + top),
                               0, 0,
                               (unsigned int)*(unsigned short*)(rec + 0x4e) << 4,
                               (unsigned int)*(unsigned short*)(rec + 0x52) << 4);
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// Shared: compute the fade alpha for a settings slider row (bright when the
// item is focused, dim when disabled).
// ---------------------------------------------------------------------------
static double options_slider_alpha(int screen, int item)
{
    double a;
    if ((*(unsigned int*)(item + 0x28) & 1) == 0)
    {
        if (*(int*)(screen + 0x188) == *(int*)(item + 0x14))
            a = FUN_802f20a0((double)FLOAT_80619ee8);
        else
            a = (double)FLOAT_80619ee8;
    }
    else
    {
        a = (double)FLOAT_8061a02c;
    }
    return (double)(float)((double)*(float*)(screen + 0xf0) * a);
}

// ---------------------------------------------------------------------------
// 0x802ce20c : render a settings slider progress bar (0x14 segments) + icon.
// ---------------------------------------------------------------------------
void menu_options_slider_render(int /*p1*/, int /*p2*/, float* rect)
{
    long long fa = FUN_803826e8();
    int screen = (int)((unsigned long long)fa >> 0x20);
    int item   = (int)fa;

    double alpha = options_slider_alpha(screen, item);
    color_scale_lo(alpha, 0x11193c7f);
    color_scale_lo(alpha, 0x11193c7f);
    color_scale_lo(alpha, 0x69737f7f);
    // NON-MATCHING: real sprite_draw_progress_bar(width,height,direction) reads
    // its base x/y from the sprite render stack; the decompiler's leading x
    // operand is folded away to match the 3-arg contract.
    sprite_draw_progress_bar(
        (unsigned int)(int)((rect[2] - rect[0]) - FLOAT_8061a030),
        (int)(FLOAT_80619eec * (rect[3] - rect[1]) + rect[1]),
        0x14);
    menu_options_row_icon_render(screen, item, rect);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x802ce38c : render a thinner settings slider progress bar (10 segments).
// ---------------------------------------------------------------------------
void menu_options_slider_render_thin(int /*p1*/, int /*p2*/, float* rect)
{
    long long fa = FUN_803826e8();
    int screen = (int)((unsigned long long)fa >> 0x20);
    int item   = (int)fa;

    double alpha = options_slider_alpha(screen, item);
    color_scale_lo(alpha, 0x11193c7f);
    color_scale_lo(alpha, 0x11193c7f);
    color_scale_lo(alpha, 0x69737f7f);
    // NON-MATCHING: 3-arg contract (see menu_options_slider_render).
    sprite_draw_progress_bar(
        (unsigned int)(int)((rect[2] - rect[0]) - FLOAT_8061a030),
        (int)(FLOAT_8061a034 * (rect[3] - rect[1]) + rect[1]),
        10);
    menu_options_row_icon_render(screen, item, rect);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x802ce50c : push the working sound/aspect/subtitle values into their child
// item value fields (item+0xc8).
// ---------------------------------------------------------------------------
void menu_options_apply_to_items(int screen)
{
    int child;
    child = (int)hud_find_child_by_id(screen, 0);
    *(int*)(child + 0xc8) = DAT_80611004;
    child = (int)hud_find_child_by_id(screen, 2);
    *(int*)(child + 0xc8) = DAT_80611008;
    child = (int)hud_find_child_by_id(screen, 5);
    *(int*)(child + 0xc8) = DAT_80612abc;
}

// ---------------------------------------------------------------------------
// 0x802ce574 : per-frame settings screen tick. Reads the child item values,
// handles +/- adjustment on the focused slider (sound mode / music / sfx /
// brightness), clamps ranges, plays tick sounds, then re-syncs item values.
// ---------------------------------------------------------------------------
void menu_options_settings_tick(int screen)
{
    int sndBuf[12];
    int musBuf[12];
    int sfxBuf[12];

    if (hud_screen_node_is_valid() == 0)
        return;

    int child = (int)hud_find_child_by_id(screen, 0);
    int soundMode = *(int*)(child + 0xc8);
    bool soundChanged = DAT_80611004 != soundMode;
    DAT_80611004 = soundMode;
    if (soundChanged)
        music_start_playback();

    child = (int)hud_find_child_by_id(screen, 2);
    DAT_80611008 = *(int*)(child + 0xc8);

    child = (int)hud_find_child_by_id(screen, 1);
    *(unsigned int*)(child + 0x28) &= 0xfffffffe;
    if (DAT_80611004 == 0)
        *(unsigned int*)(child + 0x28) |= 1;

    child = (int)hud_find_child_by_id(screen, 3);
    *(unsigned int*)(child + 0x28) &= 0xfffffffe;
    if (DAT_80611008 == 0)
        *(unsigned int*)(child + 0x28) |= 1;

    int focus = (int)hud_get_focused_item(screen);
    int oldBright = DAT_80612ab8;
    unsigned int oldSfx = (unsigned int)DAT_80611010;
    unsigned int oldMusic = (unsigned int)DAT_8061100c;

    if (focus != 0)
    {
        int kind = *(int*)(focus + 0x14);
        if (kind == 4)   // brightness
        {
            int nb;
            if ((*(unsigned int*)(screen + 0xc0) & 0x40000) == 0)
            {
                nb = DAT_80612ab8;
                if ((*(unsigned int*)(screen + 0xc0) & 0x80000) != 0)
                {
                    nb = 0x40;
                    if (DAT_80612ab8 + 1 < 0x40)
                        nb = DAT_80612ab8 + 1;
                }
            }
            else
            {
                nb = -0x40;
                if (-0x40 < DAT_80612ab8 - 1)
                    nb = DAT_80612ab8 - 1;
            }
            DAT_80612ab8 = nb;
            if (oldBright != DAT_80612ab8)
            {
                menu_loadout_noop_stub();
                if (DAT_80611a54 != 0)
                {
                    int snd = *(int*)(DAT_80611a58 + 8);
                    sound_init_play_params((int)sndBuf);
                    sndBuf[0] = snd;
                    sound_play_request(sndBuf);
                }
            }
        }
        else if (kind == 1 && (*(unsigned int*)(focus + 0x28) & 1) == 0)   // music vol
        {
            if ((*(unsigned int*)(screen + 0xc0) & 0x40000) == 0)
            {
                if ((*(unsigned int*)(screen + 0xc0) & 0x80000) != 0)
                {
                    unsigned int nv = DAT_8061100c + 1;
                    DAT_8061100c = 0x10;
                    if ((int)nv < 0x10)
                        DAT_8061100c = nv;
                }
            }
            else
            {
                unsigned int nv = DAT_8061100c - 1;
                DAT_8061100c = nv & (int)(-nv & ~nv) >> 0x1f;   // clamp >= 0
            }
            if (oldMusic != (unsigned int)DAT_8061100c)
            {
                music_start_playback();
                if (DAT_80611a54 != 0)
                {
                    int snd = *(int*)(DAT_80611a58 + 8);
                    sound_init_play_params((int)musBuf);
                    musBuf[0] = snd;
                    sound_play_request(musBuf);
                }
            }
        }
        else if (kind == 3 && (*(unsigned int*)(focus + 0x28) & 1) == 0)   // sfx vol
        {
            if ((*(unsigned int*)(screen + 0xc0) & 0x40000) == 0)
            {
                if ((*(unsigned int*)(screen + 0xc0) & 0x80000) != 0)
                {
                    unsigned int nv = DAT_80611010 + 1;
                    DAT_80611010 = 0x10;
                    if ((int)nv < 0x10)
                        DAT_80611010 = nv;
                }
            }
            else
            {
                unsigned int nv = DAT_80611010 - 1;
                DAT_80611010 = nv & (int)(-nv & ~nv) >> 0x1f;   // clamp >= 0
            }
            if (oldSfx != (unsigned int)DAT_80611010 && DAT_80611a54 != 0)
            {
                int snd = *(int*)(DAT_80611a58 + 8);
                sound_init_play_params((int)sfxBuf);
                sfxBuf[0] = snd;
                sound_play_request(sfxBuf);
            }
        }
        else if (kind == 5)   // subtitle/other
        {
            DAT_80612abc = *(int*)(focus + 0xc8);
        }
    }

    if (*(int*)(screen + 0x188) == 6 || *(int*)(screen + 0x188) == 7)
        hud_screen_set_handler_param_b(screen, 0, 0);
    else
        hud_screen_set_handler_param_b(screen, 0, 1);

    menu_options_apply_to_items(screen);
}

// ---------------------------------------------------------------------------
// 0x802ce890 : confirm & apply the settings - write the working values back to
// the game settings block, apply aspect/related toggles, play confirm sound,
// and pop the settings screen(s). Then reload the snapshot + restart music.
// ---------------------------------------------------------------------------
void menu_options_confirm_apply(int screen)
{
    int sndBuf[12];
    char* s;

    s = (char*)game_get_settings_ptr(); *(int*)(s + 0x120) = DAT_8061100c;
    s = (char*)game_get_settings_ptr(); *(int*)(s + 0x118) = DAT_80611004;
    s = (char*)game_get_settings_ptr(); *(int*)(s + 0x11c) = DAT_80611008;
    if (DAT_80611008 != 0)
    {
        FUN_8039eb5c((char)(DAT_80611008 - 1));
        FUN_803af118(DAT_80611008 == 1 ? 0 : 1);
    }
    s = (char*)game_get_settings_ptr(); *(int*)(s + 300)   = DAT_80612ab8;
    s = (char*)game_get_settings_ptr(); *(int*)(s + 0x124) = DAT_80611010;
    s = (char*)game_get_settings_ptr(); *(int*)(s + 0x130) = DAT_80612abc;
    DAT_805cc60c |= 0x10000;

    if (DAT_80612614 < 0x65)
    {
        if (DAT_80611a54 != 0)
        {
            int snd = *(int*)(DAT_80611a58 + 4);
            sound_init_play_params((int)sndBuf);
            sndBuf[0] = snd;
            sound_play_request(sndBuf);
        }
        FUN_802eec10(*(int*)(screen + 0x38));
        hud_screen_pop_with_restore();
        FUN_802eec10(*(int*)(screen + 0x38));
        hud_screen_pop_with_restore();
        DAT_80612bd4 = 0;
    }
    else
    {
        hud_screen_pop_with_restore();
        hud_screen_pop_with_restore();
    }
    menu_options_settings_load();
    music_start_playback();
}

// ---------------------------------------------------------------------------
// 0x802ce9c0 : apply the settings silently (no confirm sound, single pop path)
// - used for the non-interactive apply/back route.
// ---------------------------------------------------------------------------
void menu_options_apply_silent(int screen)
{
    char* s;
    s = (char*)game_get_settings_ptr(); *(int*)(s + 0x120) = DAT_8061100c;
    s = (char*)game_get_settings_ptr(); *(int*)(s + 0x118) = DAT_80611004;
    s = (char*)game_get_settings_ptr(); *(int*)(s + 0x11c) = DAT_80611008;
    if (DAT_80611008 != 0)
        FUN_8039eb5c((char)(DAT_80611008 - 1));
    s = (char*)game_get_settings_ptr(); *(int*)(s + 300)   = DAT_80612ab8;
    s = (char*)game_get_settings_ptr(); *(int*)(s + 0x124) = DAT_80611010;
    s = (char*)game_get_settings_ptr(); *(int*)(s + 0x130) = DAT_80612abc;
    DAT_805cc60c |= 0x10000;

    if (DAT_80612614 < 0x65)
    {
        FUN_802eec10(*(int*)(screen + 0x38));
        hud_screen_pop_with_restore();
        DAT_80612bd4 = 0;
    }
    else
    {
        hud_screen_pop_with_restore();
    }
}
