// ============================================================================
// menu_loadout_screen_build.cpp
// Batch: batch_806
// Address range: 0x802cc260 - 0x802ccff8
// Date: 2026-09-11T04:00:00Z
//
// Character / loadout select screen construction + a 4-way direction indicator
// renderer. Builds the loadout screen layout (10 slots), the character-select
// sub-list, commits a chosen character into a slot, and ticks the selection
// screen. The tail functions decode a facing angle / button bitmask into a
// direction code (2 variants each) and render the animated D-pad indicator.
// Continues the menu_loadout_* cluster (batches 801-805).
//
// NON-MATCHING: menu_dpad_indicator_render reads many incoming FP registers
// (paired-single spill slots f16..f31); these are modelled as explicit params
// / locals seeded from the four rect coords rather than undeclared register
// reads. The 64-bit frame-actor thunks (FUN_803826e8 / FUN_803826b4) return a
// hi/lo register pair split with shifts. CONCAT44 magic-bias int<->double
// conversions are written as plain casts. Named helpers resolve through the
// generated shared headers; only raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols get
// typed file-local externs.
// ============================================================================

#include "common.h"
#include "menu.h"    // pulls generated_decls.h (all named cross-module helpers)
#include "hud.h"
#include "sprite.h"
#include "sound.h"
#include "weapon.h"
#include "text.h"

// ---- raw frame-actor thunks -------------------------------------------------
extern "C" int       FUN_803826f0(void);    // current frame actor (screen ptr)
extern "C" long long FUN_803826e8(void);    // pair: hi=screen, lo=actor
extern "C" long long FUN_803826b4(void);    // pair: hi=side flag, lo=actor
extern "C" void      FUN_8038273c(void);    // scope-end variants
extern "C" void      FUN_80382734(void);
extern "C" void      FUN_80382700(void);

// ---- raw data symbols -------------------------------------------------------
// Per-slot loadout arrays (stride 0x1c bytes == 7 ints).
extern "C" int   DAT_805cc744;   // [slot*7] character/actor id (-1 == empty)
extern "C" int   DAT_805cc74c;   // [slot*7] team id
extern "C" int   DAT_805cc750;   // [slot*7] ready flag
extern "C" int   DAT_805cc758;   // [slot*7] source char-select index
extern "C" int   DAT_805cc75c;   // [slot*7] weapon id

// Character table (stride 0x30): +0x0 actor id, +0x1d char, +0x2c weapon id byte.
extern "C" int   DAT_80474680;   // char table base (actor id at [i*0x30])
extern "C" char  DAT_8047467d;   // char table weapon-id byte at [i*0x30]
extern "C" int   DAT_8048eba8;   // per-side facing-angle float table (stride 0x14)
extern "C" int   DAT_8048ebb8;   // per-side button-mask table (stride 0x14)

extern "C" int   DAT_80612614;
extern "C" int   DAT_80611a54;
extern "C" int   DAT_80611a58;
extern "C" int   DAT_80612b00;
extern "C" int   DAT_806119e0;   // active hud element handle
extern "C" int*  DAT_80610a20;   // sprite record table base

extern "C" float FLOAT_80619f38;
extern "C" float FLOAT_80619f48;
extern "C" float FLOAT_80619f50;
extern "C" float FLOAT_80619f58;
extern "C" float FLOAT_80619f6c;
extern "C" float FLOAT_80619f74;
extern "C" float FLOAT_80619f7c;
extern "C" float FLOAT_80619f8c;
extern "C" float FLOAT_80619f98;
extern "C" float FLOAT_80619fc4;
extern "C" float FLOAT_80619fdc;
extern "C" float FLOAT_80619fe0;
extern "C" float FLOAT_80619fe4;
extern "C" float FLOAT_80619fe8;
extern "C" float FLOAT_80619fec;
extern "C" float FLOAT_80619ff0;
extern "C" float FLOAT_80619ee8;
extern "C" float FLOAT_80619eec;
extern "C" float FLOAT_80619f08;
extern "C" float FLOAT_80619f0c;
extern "C" float FLOAT_80619ff4;
extern "C" float FLOAT_80619ff8;
extern "C" float FLOAT_80619ffc;
extern "C" float FLOAT_80611674;
extern "C" double DOUBLE_80619f00;

// Convenience: per-slot arrays addressed as base[slot*7] (int stride 7).
#define SLOT_ACTOR(i)   ((&DAT_805cc744)[(i) * 7])
#define SLOT_TEAM(i)    ((&DAT_805cc74c)[(i) * 7])
#define SLOT_READY(i)   ((&DAT_805cc750)[(i) * 7])
#define SLOT_SRC(i)     ((&DAT_805cc758)[(i) * 7])
#define SLOT_WEAPON(i)  ((&DAT_805cc75c)[(i) * 7])

// Character table accessors (stride 0x30 bytes).
#define CHAR_ACTOR(i)   ((&DAT_80474680)[(i) * 0xc])
#define CHAR_WEAPON(i)  ((char)(&DAT_8047467d)[(i) * 0x30])

// ---------------------------------------------------------------------------
// 0x802cc260 : build the loadout options sub-screen (a small list element with
// a close handler), link it under the current screen and flag the parent.
// ---------------------------------------------------------------------------
void menu_options_subscreen_build(void)
{
    int screen = FUN_803826f0();
    int styleSave = hud_style_apply_variant(5);
    hud_element_create(screen, 0, 2);
    int* elem = hud_get_last_created_item();   // the element just created
    hud_element_push((int)elem);
    hud_element_init_from_layout(0, 0, 0, (int)menu_options_screen_close);
    get_string_by_id(0x21);
    hud_add_list_item();
    int backStr = get_string_by_id(0x19c8);
    hud_screen_register_handler_global(1, backStr, 0, 0);
    elem[0x79] = *(int*)(screen + 0x188);
    hud_element_pop();
    hud_element_apply_center_xy(elem);
    hud_element_link_and_show(screen, (int)elem);
    hud_style_apply_variant(styleSave);
    hud_input_reset_repeat_timers();
    *(unsigned int*)(screen + 0x40) |= 0x200000;
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x802cc34c : build the main loadout screen layout - a header list plus one
// sub-list per player slot (10), whose item counts depend on game mode.
// ---------------------------------------------------------------------------
void menu_loadout_screen_layout_build(void)
{
    hud_element_init_from_layout(0x10, 0, 0, (int)menu_loadout_screen_dismiss);
    hud_set_margins((double)FLOAT_80619f6c, (double)FLOAT_80619f6c, 1);
    get_string_by_id(0xe30);
    hud_add_list_item();
    hud_add_list_item();
    menu_options_list_build();
    hud_add_callback_item_default(0);

    for (int slot = 0; slot < 10; ++slot)
    {
        hud_begin_sub_list(0, 1, 4);
        if (DAT_80612614 == 0x66)
        {
            hud_add_item_internal((double)FLOAT_80619f74, (double)FLOAT_80619fc4);
        }
        else
        {
            float w = (mode_feature_enabled_check() != 0) ? FLOAT_80619fdc : FLOAT_80619fe0;
            hud_add_item_internal((double)w, (double)FLOAT_80619fc4);
            if (mode_feature_enabled_check() != 0)
                hud_add_item_internal((double)FLOAT_80619f6c, (double)FLOAT_80619fc4);
            if (menu_slot_is_extra_bot(slot) == 0)
            {
                menu_loadout_can_edit_teams();
                hud_add_item_internal((double)FLOAT_80619f6c, (double)FLOAT_80619fc4);
                hud_add_item_internal((double)FLOAT_80619f6c, (double)FLOAT_80619fc4);
            }
        }
        hud_end_sub_list();
    }

    hud_element_apply_center_x(DAT_806119e0);
    hud_element_set_pos_xy((double)FLOAT_80619f6c, (double)FLOAT_80619fe4, DAT_806119e0);
    int str;
    str = get_string_by_id(0xe2d);
    hud_screen_register_handler_global(0, str, 1, 0);
    str = get_string_by_id(0x3ab);
    hud_screen_register_handler_global(3, str, 1, 0);
    str = get_string_by_id(0x19c8);
    hud_screen_register_handler_global(1, str, 0, 0);
}

// ---------------------------------------------------------------------------
// 0x802cc5a0 : commit the highlighted character into the active loadout slot,
// inherit team color from the previous filled slot, then advance focus.
// ---------------------------------------------------------------------------
void menu_loadout_commit_character(void)
{
    int screen = FUN_803826f0();
    int soundBuf[18];

    if ((*(unsigned int*)(*(int*)(screen + 0x194) + 0x28) & 1) == 0)
    {
        int list = hud_screen_find_child_by_id(*(int*)(screen + 0x2c), -0x7fb89c30);
        int listSel = *(int*)(list + 0x180);
        int slot = listSel - 2;
        int charSel = *(int*)(screen + 0x188);
        int actorId = CHAR_ACTOR(charSel);

        if (charSel != SLOT_SRC(slot))
        {
            if (SLOT_ACTOR(slot) < 0)
            {
                // Walk back to the nearest previously-filled slot for its team.
                int prev = listSel - 3;
                int remain = listSel - 2;
                int off = prev * 0x1c;
                if (prev >= 0)
                {
                    do {
                        if (*(int*)((int)&DAT_805cc744 + off) >= 0) break;
                        prev -= 1;
                        off -= 0x1c;
                        remain -= 1;
                    } while (remain != 0);
                }
                if (prev < 0)
                    SLOT_TEAM(slot) = 2;
                else
                    SLOT_TEAM(slot) = SLOT_TEAM(prev);
            }
            SLOT_ACTOR(slot)  = actorId;
            bool haveSound = DAT_80611a54 != 0;
            SLOT_SRC(slot)    = charSel;
            SLOT_WEAPON(slot) = (int)CHAR_WEAPON(charSel);
            SLOT_READY(slot)  = 1;
            if (haveSound)
            {
                int snd = *(int*)(DAT_80611a58 + 4);
                sound_init_play_params((int)soundBuf);
                soundBuf[0] = snd;
                sound_play_request(soundBuf);
            }
        }

        if (*(int*)(list + 0x188) < 9 &&
            menu_slot_is_extra_bot(*(int*)(list + 0x188) + 1) == 0)
        {
            *(int*)(list + 0x188) += 1;
            hud_set_focus_by_id_nth(list, *(int*)(list + 0x188), 0);
        }
        hud_screen_show_by_id(*(int*)(screen + 0x2c), -0x7fb89c30);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x802cc750 : per-frame tick for the selection screen: track closing state,
// and on B-button (0x10) play back-out sound and return to the list.
// ---------------------------------------------------------------------------
void menu_loadout_selection_tick(int screen)
{
    int soundBuf[12];

    if (hud_screen_all_children_closing(screen) == 0)
    {
        *(unsigned int*)(screen + 0x40) |= 0x100;
    }
    else
    {
        DAT_80612b00 = *(int*)(screen + 0x188);
        *(unsigned int*)(screen + 0x40) &= 0xfffffeff;
    }

    unsigned int held = input_get_buttons_held(0);
    if ((held & 0x10) != 0)
    {
        if (DAT_80611a54 != 0)
        {
            int snd = *(int*)DAT_80611a58;
            sound_init_play_params((int)soundBuf);
            soundBuf[0] = snd;
            sound_play_request(soundBuf);
        }
        hud_screen_show_by_id(*(int*)(screen + 0x2c), -0x7fb89c30);
    }
}

// ---------------------------------------------------------------------------
// 0x802cc7f8 : build the character-select list - one entry per unlockable
// character (0x96 total), gated by map completion; locked ones are disabled.
// ---------------------------------------------------------------------------
void menu_character_select_list_build(void)
{
    FUN_803826e8();
    hud_element_init_from_layout(0, 0, 0, 0);
    get_string_by_id(0xe31);
    hud_add_list_item();
    hud_add_callback_item_default(0);
    hud_set_item_padding_mode((double)FLOAT_80619f98, 1, 1, 1);
    hud_element_scroll_vertical((double)FLOAT_80619f58, 0);

    unsigned int idx = 0;
    int off = 0;
    do {
        int player = *(int*)((int)&DAT_80474680 + off);
        int weaponByte = (int)(char)(&DAT_8047467d)[off];
        if (award_check_map_completion(player, weaponByte) != 0)
        {
            int icon = weapon_def_get_hud_icon(idx);
            get_string_by_id(icon);
            hud_add_list_item();
            int scored = mode_player_scored(player);
            unsigned int locked = countLeadingZeros((unsigned int)scored) >> 5;
            int* item = hud_get_last_created_item();
            hud_item_set_locked((int)item, locked);
        }
        idx += 1;
        off += 0x30;
    } while (idx < 0x96);

    hud_element_apply_scroll();
    hud_element_set_pos_xy((double)FLOAT_80619fe8, (double)FLOAT_80619fe4, DAT_806119e0);
    int str;
    str = get_string_by_id(0x3aa);
    hud_screen_register_handler_global(0, str, 0, 0);
    str = get_string_by_id(0x19c8);
    hud_screen_register_handler_global(1, str, 0, 0);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x802cc978 : facing-angle -> direction code (left column layout).
// ---------------------------------------------------------------------------
int menu_dir_indicator_code_from_angle(double angle)
{
    if ((double)FLOAT_80619fec == angle) return 8;
    if ((double)FLOAT_80619ff0 == angle) return 6;
    if ((double)FLOAT_80619f50 == angle) return 5;
    if ((double)FLOAT_80619f7c == angle) return 4;
    return 2;
}

// ---------------------------------------------------------------------------
// 0x802cc9d0 : facing-angle -> direction code (mirrored / right column).
// ---------------------------------------------------------------------------
int menu_dir_indicator_code_from_angle_mirror(double angle)
{
    if ((double)FLOAT_80619fec == angle) return 2;
    if ((double)FLOAT_80619ff0 == angle) return 4;
    if ((double)FLOAT_80619f50 == angle) return 5;
    if ((double)FLOAT_80619f7c == angle) return 6;
    return 8;
}

// ---------------------------------------------------------------------------
// 0x802cca28 : button bitmask -> direction code (variant A).
// ---------------------------------------------------------------------------
int menu_dir_code_from_buttons(unsigned int buttons)
{
    if ((buttons & 0x10) != 0) return 8;
    if ((buttons & 0x20) == 0) return 5;
    return 2;
}

// ---------------------------------------------------------------------------
// 0x802cca4c : button bitmask -> direction code (variant B).
// ---------------------------------------------------------------------------
int menu_dir_code_from_buttons_alt(unsigned int buttons)
{
    if ((buttons & 2) != 0) return 8;
    if ((buttons & 4) != 0) return 6;
    if ((buttons & 8) == 0) return 5;
    return 2;
}

// ---------------------------------------------------------------------------
// 0x802cca80 : render the animated 4-way D-pad direction indicator with the
// four control labels, scaled/positioned inside the given rect.
//
// The original reads the incoming FP argument registers f16..f31 as paired
// singles; only the four rect coords (l,t,r,b via param_2..param_5) and the
// clamp factor (param_5) actually feed the geometry, so we model it with those
// and drop the unused spill reads (non-matching).
// ---------------------------------------------------------------------------
void menu_dpad_indicator_render(long long /*ctx*/, double left, double top,
                                double right, double bottom)
{
    long long fa = FUN_803826b4();
    int side = (int)fa;

    // Clamp the intensity factor (bottom) to [FLOAT_80619f08, FLOAT_80619ee8].
    double clampF = bottom;
    if ((double)FLOAT_80619ee8 < clampF) clampF = (double)FLOAT_80619ee8;
    if (clampF < (double)FLOAT_80619f08) clampF = (double)FLOAT_80619f08;

    double intensity = (double)FLOAT_80619f0c;

    int tex = sprite_get_texture_id(0x159);
    int rec = *DAT_80610a20 + tex * 0x58;
    unsigned int uMax = (unsigned int)*(unsigned short*)(rec + 0x4e) << 4;
    unsigned int vMax = (unsigned int)*(unsigned short*)(rec + 0x52) << 4;

    int blendSave = gx_set_blend_mode(0);
    int matSave   = gx_set_sub_material_id(1);
    int fontSave  = text_set_font_table(0x13);

    double width  = (double)(float)(right - left);
    double height = (double)(float)(top - top);   // vertical extent (see note)
    double stepX  = (double)(float)((double)FLOAT_80619ff4 * width);
    double insetY = (double)(float)((double)FLOAT_80619eec * height);
    double halfW  = (double)(float)((double)FLOAT_80619ff8 * width);
    double alpha  = (double)(float)(stepX / (double)FLOAT_80611674);
    double centerY = (double)(float)(top + insetY);
    double startX  = (double)(float)((double)FLOAT_80619eec * stepX + left);
    if ((double)FLOAT_80619ee8 < alpha) alpha = (double)FLOAT_80619ee8;

    double baseX;
    if (side == 0)
        baseX = -(double)(float)((double)FLOAT_80619ffc * height - centerY);
    else
        baseX =  (double)(float)((double)FLOAT_80619ffc * height + centerY);

    if ((float)(width / insetY) < FLOAT_80619f38)
    {
        insetY = (double)(float)(insetY * (double)(FLOAT_80619eec * (float)(width / insetY)));
        if (side == 0)
        {
            centerY = (double)(float)(top - insetY);
            baseX = -(double)(float)((double)FLOAT_80619ffc * height - centerY);
        }
        else
        {
            centerY = (double)(float)(top + insetY);
            baseX =  (double)(float)((double)FLOAT_80619ffc * height + centerY);
        }
    }

    text_set_color_rgb(alpha, alpha, (double)FLOAT_80619ee8);

    int labels[4];
    labels[0] = get_string_by_id(0x42d);
    labels[1] = get_string_by_id(0x42c);
    labels[2] = get_string_by_id(0x42e);
    labels[3] = get_string_by_id(0x42f);

    int row = side * 0x14;   // hi word of the frame-actor pair selects the row
    int codes[4];
    codes[0] = menu_dir_indicator_code_from_angle((double)*(float*)(&DAT_8048eba8 + row));
    codes[1] = menu_dir_indicator_code_from_angle_mirror((double)*(float*)(&DAT_8048eba8 + row));
    codes[2] = menu_dir_code_from_buttons(*(unsigned int*)(&DAT_8048ebb8 + row));
    codes[3] = menu_dir_code_from_buttons_alt(*(unsigned int*)(&DAT_8048ebb8 + row));

    double eec   = (double)FLOAT_80619eec;
    double halfLo = (double)(float)(centerY - insetY);
    double halfHi = (double)(float)(centerY + insetY);
    double halfHW = (double)(float)(eec * halfW);
    double f48   = (double)FLOAT_80619f48;
    double bias  = DOUBLE_80619f00;

    for (int i = 0, o = 0; i < 4; ++i, o += 4)
    {
        double codeD = (double)(CONCAT44(0x43300000, codes[i] ^ 0x80000000));
        double uD    = (double)CONCAT44(0x43300000, uMax ^ 0x80000000);
        double scaleC = (double)(float)(f48 * (double)(float)(codeD - bias));
        unsigned int uCode = (unsigned int)(scaleC * (double)(float)(uD - bias));

        double glyphX;
        double drawR;
        double curBase = baseX;
        if (side == 0)
        {
            glyphX = (double)(float)(insetY * scaleC + centerY);
            drawR  = halfHi;
            double cw = text_get_char_width((unsigned char*)(unsigned long long)labels[i]);
            baseX  = (double)(float)(curBase - cw);
        }
        else
        {
            glyphX = -(double)(float)(insetY * scaleC - centerY);
            drawR  = halfLo;
        }

        gx_set_active_material(tex);
        double y0 = (double)(float)(startX - halfHW);
        double y1 = (double)(float)(startX + halfHW);
        gx_draw_sprite_quad_c4(centerY, y0, drawR, y1, 0, 0, uMax, vMax);
        gx_draw_sprite_quad_c4(centerY, y0, glyphX, y1, 0, 0, uCode, vMax);

        unsigned char* label = (unsigned char*)(unsigned long long)labels[i];
        double ch = text_get_char_height(label);
        text_draw_char_shadowed(curBase, -(double)(float)(eec * ch - startX), label,
                                (int)(intensity * clampF) & 0xff | 0x7f7f7f00,
                                (int)(intensity * clampF) & 0xff);
        startX = (double)(float)(startX + stepX);
    }

    gx_set_blend_mode(blendSave);
    gx_set_sub_material_id(matSave);
    double one = (double)FLOAT_80619ee8;
    text_set_color_rgb(one, one, one);
    text_set_font_table(fontSave);
    FUN_80382700();
}
