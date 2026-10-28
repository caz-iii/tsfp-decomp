// menu_options_screens.cpp
// Options / controls menu screen builders and their button callbacks, recovered
// in batch_810. Companion to menu_options_settings.cpp (batch_808). These build
// the HUD list layouts (controls config, reset prompt) and handle the
// back/apply/confirm button callbacks, plus a focus-box overlay renderer and the
// screen's resource-pool allocator.
//
// Screen state gate: DAT_80612614 < 0x65 means "in a live game session" (pause
// menu path) vs the front-end menu path.
//
// NON-MATCHING decompilation.

#include "common.h"
#include "menu.h"    // pulls generated_decls.h (all named cross-module helpers)
#include "hud.h"
#include "sound.h"
#include "gx_draw.h"

// ---------------------------------------------------------------------------
// Per-file externs for raw ROM symbols / helpers (types match call sites).
// ---------------------------------------------------------------------------
extern int   DAT_80612614;    // session/menu state (0x65 running, <0x65 in-game overlay)
extern int   DAT_80611a54;    // "confirm sound available" flag
extern int   DAT_80611a58;    // confirm-sound descriptor block (+4 = sound id)
extern int   DAT_80612bd4;    // options screen active latch
extern int   DAT_806119e0;    // front-end center-layout anchor
extern int   DAT_80611748;    // focus-box geometry record (int[7])
extern int   DAT_80611a51;    // focus-box color low byte source

extern float FLOAT_80619f38;  // list item padding value
extern float FLOAT_80619f08;  // spacer size 0.0
extern float FLOAT_80619fc8;  // controls-screen margin A
extern float FLOAT_80619fb8;  // controls-screen margin B
extern float FLOAT_8061a048;  // horizontal scroll amount
extern float FLOAT_8061a04c;  // list vertical nudge

// Scoped frame-actor accessor pair (push/pop) used by the render/build callbacks.
extern int  FUN_803826e4(void);   // frame-actor push (returns actor block)
extern void FUN_80382730(void);   // frame-actor pop
extern int  FUN_803826e8(void);   // frame-actor push (variant)
extern void FUN_80382734(void);   // frame-actor pop (variant)

// Neighboring menu helpers (not yet named; per-file typed externs).
extern void FUN_802eec10(int a);          // 0x802eec10 restore prior menu context
extern void FUN_802eeb2c(int a);          // 0x802eeb2c in-game layout finalise
extern int  FUN_802f5204(int a);          // 0x802f5204 controls layout query (bool)
extern int  FUN_802f527c(void);           // 0x802f527c controls scroll query (bool)

// resource_pool_create resolves via generated_externs.h (declared int (...)).

// hud_add_list_item is declared void in the shim, but the ROM function returns
// the newly-added item pointer (used to set field +0x40). Bind an address-typed
// alias to recover the return value (0x8003ea8c).
typedef int (*hud_add_list_item_ret_fp)(void);
static const hud_add_list_item_ret_fp __hud_add_list_item_ret = (hud_add_list_item_ret_fp)0x8003ea8c;

// ---------------------------------------------------------------------------
// 0x802cea8c  menu_options_back_confirm
// "Back" button callback: play the confirm sound (if available), restore the
// prior menu context and pop the screen (front-end path just pops), then restart
// music. 'screen' is the HUD screen object (+0x38 = prior-context handle).
// ---------------------------------------------------------------------------
void menu_options_back_confirm(int screen)
{
    if (DAT_80612614 < 0x65) {
        if (DAT_80611a54 != 0) {
            int params[12];
            int soundId = *(int *)(DAT_80611a58 + 4);
            sound_init_play_params((int)params);
            params[0] = soundId;
            sound_play_request(params);
        }
        FUN_802eec10(*(int *)(screen + 0x38));
        hud_screen_pop_with_restore();
    } else {
        hud_screen_pop_with_restore();
    }
    music_start_playback();
}

// ---------------------------------------------------------------------------
// 0x802ceb0c  menu_options_apply_and_close
// Apply working settings (menu_options_settings_load), restart music, then
// restore context and pop the screen; clears the options-active latch on the
// in-game path.
// ---------------------------------------------------------------------------
void menu_options_apply_and_close(int screen)
{
    menu_options_settings_load();
    music_start_playback();
    menu_loadout_noop_stub();
    if (DAT_80612614 < 0x65) {
        FUN_802eec10(*(int *)(screen + 0x38));
        hud_screen_pop_with_restore();
        DAT_80612bd4 = 0;
    } else {
        hud_screen_pop_with_restore();
    }
}

// ---------------------------------------------------------------------------
// 0x802ceb6c  menu_options_reset_prompt_build
// Build the "reset to defaults?" confirmation list: title (string 0x17d3),
// destructor from screen+0x1e0, a prompt line (0x1790) with default callback,
// and yes/no items (0x173e/0x173f). Focuses item 1.
// ---------------------------------------------------------------------------
void menu_options_reset_prompt_build(int screen)
{
    hud_set_item_padding_mode((double)FLOAT_80619f38, 1, 1, 1);
    int title = get_string_by_id(0x17d3);
    hud_element_init_from_layout(0x8000, (void *)title, 0, 0);
    hud_set_destructor(*(void **)(screen + 0x1e0));
    get_string_by_id(0x1790);
    hud_add_list_item();
    hud_add_callback_item_default(1);
    get_string_by_id(0x173e);
    hud_add_list_item();
    get_string_by_id(0x173f);
    hud_add_list_item();
    hud_set_focus_by_id(screen, 1);
    if (DAT_80612614 < 0x65) {
        FUN_802eeb2c(screen);
    } else {
        hud_element_apply_center_xy(&DAT_806119e0);
    }
}

// ---------------------------------------------------------------------------
// 0x802cec7c  menu_options_back_dirty_check
// Back button with unsaved-changes handling: if settings are not dirty, behave
// like back_confirm (pop). If dirty, play the confirm sound and PUSH the
// save-prompt screen instead of popping.
// ---------------------------------------------------------------------------
void menu_options_back_dirty_check(int screen)
{
    if (menu_options_settings_dirty() == 0) {
        if (DAT_80612614 < 0x65) {
            if (DAT_80611a54 != 0) {
                int params[12];
                int soundId = *(int *)(DAT_80611a58 + 4);
                sound_init_play_params((int)params);
                params[0] = soundId;
                sound_play_request(params);
            }
            FUN_802eec10(*(int *)(screen + 0x38));
            hud_screen_pop_with_restore();
            DAT_80612bd4 = 0;
        } else {
            hud_screen_pop_with_restore();
        }
    } else if (DAT_80612614 < 0x65) {
        if (DAT_80611a54 != 0) {
            int params[12];
            int soundId = *(int *)(DAT_80611a58 + 4);
            sound_init_play_params((int)params);
            params[0] = soundId;
            sound_play_request(params);
        }
        FUN_802eec10(*(int *)(screen + 0x38));
        hud_screen_push();
    } else {
        hud_screen_push();
    }
}

// ---------------------------------------------------------------------------
// 0x802ced6c  menu_controls_screen_build
// Build the controls / configuration list screen. Layout style and margins
// depend on two queries (FUN_802f5204 on the frame actor, FUN_802f527c). Adds
// the title, several list rows with string labels (0x17d6..0x1aaa), spacers,
// ok/cancel events, and registers global handlers. Finalises layout differently
// for the in-game vs front-end path. Uses the frame-actor push/pop pair.
// ---------------------------------------------------------------------------
void menu_controls_screen_build(void)
{
    int actor = FUN_803826e4();
    bool layoutB = (FUN_802f5204(*(int *)(actor + 0x38)) == 0);
    bool scroll  = (FUN_802f527c() != 0);

    int title = get_string_by_id(0x17d3);
    int backCb = 0x802cec7c;   // &menu_options_back_dirty_check (bound by ROM address)
    if (DAT_80612614 < 0x65) {
        hud_element_init_from_layout(0, (void *)title, 0, backCb);
    } else {
        hud_element_init_from_layout(0x8000000, (void *)title, 0, backCb);
    }

    if (!scroll) {
        hud_set_context_property(0x2000);
    }
    hud_clear_destructor();

    if (layoutB) {
        hud_set_margins((double)FLOAT_80619fc8, (double)FLOAT_80619f38, 1);
    } else {
        hud_set_margins((double)FLOAT_80619fb8, (double)FLOAT_80619f38, 1);
    }
    if (scroll) {
        hud_element_scroll_horizontal((double)FLOAT_8061a048, 0);
    }

    unsigned int spacerFlags = ((scroll ? 0xffffffffu : 0u) & 0x100) | 0x20000;

    get_string_by_id(0x17d6);
    hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_80619f08, spacerFlags);
    hud_event_add_ok_cancel();

    get_string_by_id(0x17d7);
    int row = __hud_add_list_item_ret();
    *(float *)(row + 0x40) = FLOAT_80619f38;

    get_string_by_id(0x17d8);
    hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_80619f08, spacerFlags);
    hud_event_add_string(get_string_by_id(0x17dc));
    hud_event_add_string(get_string_by_id(0x17dd));
    hud_event_add_string(get_string_by_id(0x17de));

    get_string_by_id(0x17d9);
    row = __hud_add_list_item_ret();
    *(float *)(row + 0x40) = FLOAT_80619f38;

    get_string_by_id(0x19a9);
    hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_80619f08, spacerFlags);
    hud_event_add_ok_cancel();
    hud_add_callback_item_default(0);

    get_string_by_id(0x1aa7);
    hud_add_list_item();
    get_string_by_id(0x1aaa);
    hud_add_list_item();

    hud_screen_register_handler_global(0, get_string_by_id(0x19c7), 1, 0);
    if (scroll) {
        hud_element_apply_scroll();
    }
    hud_screen_register_handler_global(1, get_string_by_id(0x19c8), 0, 0);

    if (DAT_80612614 < 0x65) {
        FUN_802eeb2c(actor);
        if ((!scroll) || (!layoutB)) {
            *(float *)(actor + 0x20c) = FLOAT_8061a04c;
        }
    } else {
        hud_screen_register_handler_global(2, get_string_by_id(0x3d3), 0, 0);
        hud_element_apply_center_xy(&DAT_806119e0);
    }

    menu_options_apply_to_items(actor);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x802cf124  menu_options_focus_box_render
// Draw the four-sided focus box border around the currently-focused list item
// (only when its type field +0x188 == 4). Geometry comes from DAT_80611748;
// color is DAT_80611a51 OR'd into 0x7f7f7f00. Uses the frame-actor variant pair.
// ---------------------------------------------------------------------------
void menu_options_focus_box_render(void)
{
    int item = FUN_803826e8();
    if (*(int *)(item + 0x188) == 4) {
        int left   = *(int *)(&DAT_80611748) + 5;
        int top    = *(int *)((int)&DAT_80611748 + 5 * 4) + 5;
        int bottom = *(int *)((int)&DAT_80611748 + 6 * 4);
        unsigned int right = (unsigned int)*(int *)((int)&DAT_80611748 + 1 * 4) - 5;
        unsigned int color = (unsigned int)DAT_80611a51 | 0x7f7f7f00;
        gx_draw_line_i(left, top, right, top, color);
        gx_draw_line_i(left, bottom - 5, right, bottom - 5, color);
        gx_draw_line_i(left, top, left, bottom - 6, color);
        gx_draw_line_i(right, top, right, bottom - 6, color);
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x802cf1ec  menu_options_pool_alloc
// Allocate the options-screen resource pool (0x8e800 bytes, type 4) via the wide
// mem/pool ABI. The trailing register args (r6..r10) are forwarded from the
// caller's context, expressed here as explicit parameters per the compile-clean
// standard.
// ---------------------------------------------------------------------------
void menu_options_pool_alloc(unsigned long long a1, unsigned long long a2,
                             unsigned long long a3, unsigned long long a4,
                             unsigned long long a5, unsigned long long a6,
                             unsigned long long a7, unsigned long long a8,
                             int r6, int r7, int r8, int r9, int r10)
{
    resource_pool_create(a1, a2, a3, a4, a5, a6, a7, a8,
                         0, 4, 0x8e800, r6, r7, r8, r9, r10);
}

