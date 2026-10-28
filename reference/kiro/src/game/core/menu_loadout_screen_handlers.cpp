// ============================================================================
// menu_loadout_screen_handlers.cpp
// Batch: batch_805
// Address range: 0x802cb754 - 0x802cc260
// Date: 2026-09-10T21:00:00Z
//
// Loadout / options screen handler + callback layer. These are the HUD screen
// callbacks that drive the character/loadout select screen: per-slot cursor
// and "ready" swatch rendering, active-bot counting, navigation/button-state
// updates, and the award/options sub-list builder. Continues the menu_loadout_*
// cluster written across batches 801-804.
//
// NON-MATCHING: the frame-actor accessor thunks (FUN_803826e8 / FUN_803826d8)
// return a 64-bit register pair (hi = screen node, lo = actor); modelled here
// as a long long and split with shifts. GameCube int<->float
// CONCAT44(0x43300000,...) magic-bias conversions are written as plain casts.
// Named cross-module helpers resolve through the generated shared headers; only
// raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols get typed file-local externs below.
// ============================================================================

#include "common.h"
#include "menu.h"    // pulls generated_decls.h (all named cross-module helpers)
#include "hud.h"
#include "sprite.h"
#include "sound.h"
#include "weapon.h"

// ---- raw frame-actor thunks (64-bit register-pair returns) ------------------
extern "C" long long FUN_803826e8(void);   // pair: hi=screen, lo=actor
extern "C" long long FUN_803826d8(void);   // pair: hi=screen, lo=actor
extern "C" int  FUN_803826f0(void);         // current frame actor (screen ptr)
extern "C" void FUN_80382734(void);         // scope-end variants
extern "C" void FUN_80382724(void);
extern "C" void FUN_8038273c(void);
extern "C" void FUN_803825c0(double alpha);  // apply alpha to frame actor
extern "C" void FUN_80320524(void);          // alt options-list builder

// ---- raw data symbols -------------------------------------------------------
// Per-slot loadout arrays (stride 0x1c bytes == 7 ints).
extern "C" int   DAT_805cc744;   // [slot*7] character/actor id (-1 == empty)
extern "C" int   DAT_805cc74c;   // [slot*7] team id
extern "C" int   DAT_805cc750;   // [slot*7] ready/host flag
extern "C" int   DAT_805cc754;   // [slot*7] locked marker (-1 == unlocked)
extern "C" int   DAT_805cc758;   // [slot*7] secondary id
extern "C" int   DAT_805cc75c;   // [slot*7] weapon id
extern "C" char  DAT_805cc68e;
extern "C" int   DAT_805cc53c;   // award/option string-id table

extern "C" int   DAT_80612244;
extern "C" int   DAT_80612614;
extern "C" int   DAT_80611a54;
extern "C" int   DAT_80611a58;
extern "C" int   DAT_80612be4;
extern "C" int   DAT_80612b00;
extern "C" int*  DAT_80610a20;
extern "C" float FLOAT_80619f38;
extern "C" float FLOAT_80619fac;
extern "C" float FLOAT_80619f0c;
extern "C" float FLOAT_80619f08;
extern "C" float FLOAT_80619fd4;
extern "C" double DOUBLE_80619f00;
extern "C" unsigned char DAT_802cc0f0;   // change-callback address (taken as &)

// Convenience: the per-slot arrays are addressed as base[slot*7] (int stride 7).
#define SLOT_ACTOR(i)   ((&DAT_805cc744)[(i) * 7])
#define SLOT_TEAM(i)    ((&DAT_805cc74c)[(i) * 7])
#define SLOT_READY(i)   ((&DAT_805cc750)[(i) * 7])
#define SLOT_LOCK(i)    ((&DAT_805cc754)[(i) * 7])
#define SLOT_SEC(i)     ((&DAT_805cc758)[(i) * 7])
#define SLOT_WEAPON(i)  ((&DAT_805cc75c)[(i) * 7])

// ---------------------------------------------------------------------------
// 0x802cb754 : render the team-color cursor over a bot slot (if editable).
// ---------------------------------------------------------------------------
void menu_loadout_bot_cursor_render(int /*p1*/, int /*p2*/, float* rect)
{
    long long pair = FUN_803826e8();
    int screen = (int)((unsigned long long)pair >> 0x20);
    int actor  = (int)pair;
    int slot   = *(int*)(actor + 0x14);

    gx_set_sub_material_id(1);

    if (SLOT_ACTOR(slot) >= 0 && SLOT_LOCK(slot) == -1 &&
        (DAT_805cc68e != '\x13' || player_ai_is_story_mode(SLOT_ACTOR(slot)) == 0))
    {
        unsigned int color;
        if (menu_loadout_can_edit_teams() == 0)
            color = (unsigned int)SLOT_TEAM(slot);
        else
            color = (unsigned int)menu_slot_tier_scale(SLOT_ACTOR(slot));

        menu_loadout_cursor_render(screen, color, rect);

        if (hud_screen_all_children_closing(screen) != 0 &&
            (int)hud_get_focused_item(screen) == actor)
        {
            hud_draw_pulsing_box((int)rect[0], (int)(rect[2] - FLOAT_80619f38),
                                 (int)rect[1], (int)rect[3]);
        }
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x802cb8cc : draw the "ready" / host swatch sprite for a slot.
// ---------------------------------------------------------------------------
void menu_loadout_ready_swatch_render(int /*p1*/, int /*p2*/, float* rect)
{
    long long pair = FUN_803826d8();
    int screen = (int)((unsigned long long)pair >> 0x20);
    int actor  = (int)pair;
    int slot   = *(int*)(actor + 0x14);

    gx_set_sub_material_id(1);

    if (SLOT_ACTOR(slot) >= 0 && SLOT_LOCK(slot) == -1 &&
        (DAT_805cc68e != '\x13' || player_ai_is_story_mode(SLOT_ACTOR(slot)) == 0))
    {
        int* readyPtr = &SLOT_READY(slot);
        int  spriteSel = (*readyPtr != 0) ? 0x15a : 0x15b;
        int  tex = (int)sprite_get_texture_id(spriteSel);

        // Inset: 2px when not ready, 0 when ready.
        float inset  = (float)((*readyPtr != 0) ? 0 : 2);
        float pad2   = (float)(4 - (DAT_80612244 == 0));

        float x0 = FLOAT_80619f38 + rect[0];
        float y0 = rect[1];
        float x1 = rect[2] - FLOAT_80619f38;
        float y1 = rect[3];

        gx_set_active_material(tex);

        float alphaScale = (*readyPtr == 0) ? FLOAT_80619fac : FLOAT_80619f0c;
        FUN_803825c0((double)(alphaScale * *(float*)(actor + 0xf0)));

        int rec = *DAT_80610a20 + tex * 0x58;
        gx_draw_sprite_rect(
            (int)(x0 + inset),
            (int)(inset + y0 + pad2),
            (int)(x1 - inset),
            (int)((y1 - inset) - inset),
            0, 0,
            (unsigned int)*(unsigned short*)(rec + 0x4e) << 4,
            (unsigned int)*(unsigned short*)(rec + 0x52) << 4);

        if (hud_screen_all_children_closing(screen) != 0 &&
            (int)hud_get_focused_item(screen) == actor)
        {
            hud_draw_pulsing_box((int)rect[0], (int)(rect[2] - FLOAT_80619f38),
                                 (int)rect[1], (int)rect[3]);
        }
    }
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// 0x802cbbc0 : count how many bot slots are filled & ready.
// ---------------------------------------------------------------------------
int menu_loadout_count_active_bots(void)
{
    int count = 0;
    int off = 0;
    for (int i = 10; i != 0; --i)
    {
        int ready = *(int*)((int)&DAT_805cc750 + off);
        int actor = *(int*)((int)&DAT_805cc744 + off);
        if (ready != 0 && actor >= 0)
            ++count;
        off += 0x1c;
    }
    return count;
}

// ---------------------------------------------------------------------------
// 0x802cbc04 : dismiss the loadout screen (clear a mode-0x66 flag then pop).
// ---------------------------------------------------------------------------
void menu_loadout_screen_dismiss(void)
{
    if (hud_screen_node_is_valid() != 0)
    {
        if (DAT_80612614 == 0x66)
            *(unsigned int*)(DAT_80612be4 + 0x238) &= 0xfffffeff;
        hud_screen_pop_current();
    }
}

// ---------------------------------------------------------------------------
// 0x802cbc5c : slot -> group rank (0 for the mode-9 last slot, else sign+>0xb).
// ---------------------------------------------------------------------------
int menu_slot_group_rank(unsigned int slot)
{
    if (menu_slot_is_last_in_mode9(slot) != 0)
        return 0;
    return ((int)slot >> 0x1f) + (unsigned int)(0xb < slot);
}

// ---------------------------------------------------------------------------
// 0x802cbcac : confirm / advance the current slot; play confirm or deny sound.
// ---------------------------------------------------------------------------
void menu_loadout_slot_confirm(int screen)
{
    int variantIdx = *(int*)(screen + 0x180) - 2;
    int localBuf[12];
    int denyBuf[12];

    if (variantIdx >= 0 && variantIdx < mode_variant_count_remaining())
    {
        unsigned int active = (unsigned int)menu_get_active_slot_index();
        if (menu_slot_group_rank(active) != 0 &&
            menu_slot_is_extra_bot(variantIdx) == 0)
        {
            hud_screen_show_by_id(*(int*)(screen + 0x2c), -0x7fb89c10);
            if (DAT_80611a54 == 0)
                return;
            int snd = *(int*)(DAT_80611a58 + 4);
            sound_init_play_params((int)localBuf);
            localBuf[0] = snd;
            sound_play_request(localBuf);
            return;
        }
    }

    if (DAT_80611a54 != 0)
    {
        int snd = *(int*)(DAT_80611a58 + 0xc);
        sound_init_play_params((int)denyBuf);
        denyBuf[0] = snd;
        sound_play_request(denyBuf);
    }
}

// ---------------------------------------------------------------------------
// 0x802cbd78 : per-frame navigation / button-enable-state update for the
// loadout screen. Sets the up/down (idx 0) and confirm (idx 3) button states,
// handles slot clearing, and refreshes the previewed weapon def index.
// ---------------------------------------------------------------------------
void menu_loadout_nav_update(void)
{
    int screen = FUN_803826f0();
    int variantIdx = *(int*)(screen + 0x180) - 2;
    unsigned int childSlot = (unsigned int)menu_get_child_slot_index(screen);
    int rank = menu_slot_group_rank(childSlot);

    if ((*(unsigned int*)(screen + 0x40) & 0x200000) == 0)
    {
        int childSel = *(int*)(screen + 0x188);
        if (childSel == -2)
        {
            hud_screen_set_handler_param_b(screen, 0, 1);
            hud_screen_set_handler_param_b(screen, 3, 1);
        }
        else if (rank == 0)
        {
            if (DAT_80612614 == 0x65 && *(int*)(screen + 0x184) > 0)
            {
                hud_screen_set_handler_param_b(screen, 3, 1);
                int feature = mode_feature_enabled_check();
                if (feature == 0)
                {
                    if (*(int*)(screen + 0x184) == 1)
                        hud_screen_set_handler_param_b(screen, 0, 1);
                    else
                        hud_screen_set_handler_param_b(screen, 0, 0);
                }
                else if (*(int*)(screen + 0x184) == 2)
                    hud_screen_set_handler_param_b(screen, 0, 1);
                else
                    hud_screen_set_handler_param_b(screen, 0, 0);
            }
            else
            {
                hud_screen_set_handler_param_b(screen, 0, 1);
                hud_screen_set_handler_param_b(screen, 3, 1);
            }
        }
        else
        {
            if (menu_slot_is_extra_bot(childSel) == 0)
            {
                hud_screen_set_handler_param_b(screen, 0, 0);
                if (SLOT_ACTOR(childSel) < 0)
                    hud_screen_set_handler_param_b(screen, 3, 1);
                else
                    hud_screen_set_handler_param_b(screen, 3, 0);
            }
            else
            {
                hud_screen_set_handler_param_b(screen, 0, 1);
                hud_screen_set_handler_param_b(screen, 3, 1);
            }
        }
    }
    else
    {
        hud_screen_set_handler_param_b(screen, 3, 2);
        hud_screen_set_handler_param_b(screen, 0, 2);
    }

    // Slot clear / confirm-sound handling when the confirm button is pressed.
    if (*(int*)(screen + 0x180) > 0 &&
        (*(int*)(screen + 0x184) > 0 || rank != 0) &&
        (*(int*)(screen + 0x184) != 0 || variantIdx < mode_variant_count_remaining()))
    {
        int hasSound = DAT_80611a54;
        if (rank != 0 && (*(unsigned int*)(screen + 0xc0) & 0x800000) != 0)
        {
            int localBuf[12];
            int clearBuf[18];
            if (SLOT_ACTOR(variantIdx) < 0)
            {
                if (DAT_80611a54 != 0)
                {
                    int snd = *(int*)(DAT_80611a58 + 0xc);
                    sound_init_play_params((int)localBuf);
                    localBuf[0] = snd;
                    sound_play_request(localBuf);
                }
            }
            else
            {
                SLOT_ACTOR(variantIdx) = -1;
                SLOT_SEC(variantIdx)   = -1;
                if (hasSound != 0)
                {
                    int snd = *(int*)(DAT_80611a58 + 4);
                    sound_init_play_params((int)clearBuf);
                    clearBuf[0] = snd;
                    sound_play_request(clearBuf);
                }
            }
        }
    }

    if (DAT_80612614 == 0x66 && hud_screen_all_children_closing(screen) != 0)
        *(unsigned int*)(screen + 0x44) |= 0x20;

    // Refresh previewed weapon-def index.
    if (*(int*)(screen + 0x188) == -2)
    {
        int idx = weapon_def_find_index(DAT_805cc744, DAT_805cc75c);
        if (idx >= 0)
            DAT_80612b00 = weapon_def_find_index(DAT_805cc744, DAT_805cc75c);
    }
    else
    {
        if (menu_slot_is_extra_bot(*(int*)(screen + 0x188)) == 0)
        {
            if (variantIdx >= 0)
            {
                int idx = weapon_def_find_index(SLOT_ACTOR(variantIdx), SLOT_WEAPON(variantIdx));
                if (idx >= 0)
                    DAT_80612b00 = weapon_def_find_index(SLOT_ACTOR(variantIdx),
                                                         SLOT_WEAPON(variantIdx));
            }
        }
        else
        {
            DAT_80612b00 = weapon_def_find_index(0x5d, -1);
        }
    }

    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x802cc0f4 : clamp the options-screen selection to a selectable entry.
// ---------------------------------------------------------------------------
void menu_options_clamp_selection(void)
{
    char* settings = (char*)game_get_settings_ptr();
    if (menu_slot_is_selectable_ex(*(int*)(settings + 0x6a8)) == 0)
    {
        settings = (char*)game_get_settings_ptr();
        *(int*)(settings + 0x6a8) = 1;
    }
}

// ---------------------------------------------------------------------------
// 0x802cc138 : build the award / options selection list for this screen.
// ---------------------------------------------------------------------------
void menu_options_list_build(void)
{
    FUN_803826f0();
    if (DAT_80612614 == 0x66)
    {
        FUN_80320524();
        FUN_8038273c();
        return;
    }

    menu_options_clamp_selection();
    hud_item_add_spacer((double)FLOAT_80619f08, 0x32400);
    {
        int item = (int)hud_get_last_created_item();
        *(float*)(item + 0xe0) = FLOAT_80619fd4;
    }

    int idx = 0;
    int strOff = 0;
    do {
        hud_event_add_string(*(unsigned int*)((int)&DAT_805cc53c + strOff));
        if (award_validate_id_range(idx) == 0 || menu_slot_is_selectable_ex(idx) == 0)
        {
            int counter = (int)hud_get_item_counter();
            hud_event_set_disabled(counter, 1);
        }
        ++idx;
        strOff += 4;
    } while (idx < 0x12);

    int item = (int)hud_get_last_created_item();
    *(unsigned int*)(item + 0x28) |= 0x4000000;
    item = (int)hud_get_last_created_item();
    hud_item_set_change_callback(item, (void*)&DAT_802cc0f0);
    game_get_settings_ptr();
    hud_get_last_created_item();
    hud_item_on_value_change();

    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x802cc230 : close the options sub-screen (clear a flag then pop it).
// ---------------------------------------------------------------------------
void menu_options_screen_close(int screen)
{
    *(unsigned int*)(*(int*)(screen + 0x2c) + 0x40) &= 0xffdfffff;
    hud_screen_pop(screen);
}
