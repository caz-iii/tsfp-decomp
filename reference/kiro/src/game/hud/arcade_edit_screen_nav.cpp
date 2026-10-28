// arcade_edit_screen_nav.cpp
// Arcade map-editor screen lifecycle, menu-action dispatch, and per-frame tick.
// The editor screen is driven by the descriptor/vtable at PTR_FUN_804802b0; the
// selected menu action is stored at screen+0x188 and dispatched by
// aveditor_dispatch_menu_action. Layout coordinate pairs live in the float table
// at DAT_80480288 (x at +0, y at +4, stride 8).
//
// Address: 0x8031d730..0x8031dc2c | Batch: batch_898 | Date: 2026-09-12

#include "common.h"

// ---- raw symbols referenced by this translation unit ----
extern unsigned char DAT_80480288;        // layout coordinate pair table (float[2] stride 8)
extern unsigned char DAT_8048028c;        // = DAT_80480288 + 4 (y component)
extern void         *PTR_FUN_804802b0;    // editor screen descriptor/vtable
extern int  DAT_80612be4;                 // editor screen context base
extern int  DAT_80612be8;                 // active level command stream base
extern int  DAT_80612bf4;                 // parent screen handle
extern int  DAT_80611a54;
extern int *DAT_80611a58;
extern int  DAT_805d59dc;                 // unsaved-changes / dirty flag
extern int  DAT_806119ec;
extern unsigned char DAT_8051a820;

extern void hud_screen_navigate_to(int screen, void *desc, int arg);
extern void hud_screen_push(void);
extern void hud_screen_rebuild_items(void);
extern int  hud_screen_find_root(int screen);
extern void hud_input_reset_repeat_timers(void);
extern void input_clear_buttons(int pad, int mask);
extern void sound_init_play_params(int params);
extern void sound_play_request(int *params);
extern int  get_string_by_id(int stringId);
extern void hud_create_yes_no_dialog(int screen, int msg, int cb, int arg);
extern int  hud_cursor_hit_test_rect(float *rect);
extern int  heap_arena_lookup(void);

extern void asel_reset_screen(int arg);
extern void aveditor_serialize_map(void);
extern void aveditor_view_from_camera(void);
extern void aveditor_cursor_slot_store(int a, int b);
extern void aveditor_fit_view_to_bounds(void);
extern void avanim_play_move_sound(int soundId);
extern void aveditor_ensure_models_loaded(unsigned int ctx, unsigned long long a2,
                                          unsigned long long a3, unsigned long long a4,
                                          unsigned long long a5, unsigned long long a6,
                                          unsigned long long a7, unsigned long long a8);

extern unsigned long long FUN_80346df0(int arg);
extern void FUN_80345c2c(int arg);
extern void FUN_80345da8(int arg);
extern void FUN_80320af0(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8);
extern unsigned long long FUN_8034757c(unsigned long long a1, unsigned long long a2,
                                       unsigned long long a3, unsigned long long a4,
                                       unsigned long long a5, unsigned long long a6,
                                       unsigned long long a7, unsigned long long a8, int stream);
extern void FUN_803473c4(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int stream, int mode);
extern void FUN_8033c4d8(void);
extern void FUN_8033c518(void);
extern void FUN_80348e64(void);
extern void FUN_80348298(void);
extern int  FUN_8032197c(void);
extern bool FUN_8034737c(void);
extern bool FUN_803473a0(void);

// 0x8031d730: open/enter the editor screen - navigate to it, set the active bit,
// play the open sound, silence some channels and restore the saved cursor slots.
void aveditor_enter_screen(void)
{
    int soundId;
    int playParams[13];

    hud_screen_navigate_to(DAT_80612bf4, &PTR_FUN_804802b0, -1);
    *(unsigned int *)(DAT_80612be4 + 0x25c) = *(unsigned int *)(DAT_80612be4 + 0x25c) | 8;
    if (DAT_80611a54 != 0)
    {
        soundId = *DAT_80611a58;
        sound_init_play_params((int)playParams);
        playParams[0] = soundId;
        sound_play_request(playParams);
    }
    FUN_80345c2c(0);
    FUN_80345da8(2);
    FUN_80345da8(3);
    FUN_80345da8(4);
    aveditor_cursor_slot_store(-1, 0);
    return;
}

// 0x8031d7cc: leave/suspend the editor screen - navigate back to the parent, clear
// the active bit, reset all pad repeat state and buttons, mark the parent dirty.
void aveditor_leave_screen(int screen)
{
    if (screen != 0)
    {
        hud_screen_navigate_to(*(int *)(screen + 0x2c), 0, -1);
    }
    *(unsigned int *)(DAT_80612be4 + 0x25c) = *(unsigned int *)(DAT_80612be4 + 0x25c) & 0xfffffff7;
    FUN_80345da8(0);
    FUN_80345da8(1);
    FUN_80345da8(5);
    FUN_80345da8(8);
    FUN_80345c2c(2);
    FUN_80345c2c(3);
    if (*(int *)(DAT_80612be4 + 0x200) == 0)
    {
        FUN_80345c2c(4);
    }
    aveditor_cursor_slot_store(0, -1);
    hud_input_reset_repeat_timers();
    input_clear_buttons(0, 0x8000);
    input_clear_buttons(1, 0x8000);
    input_clear_buttons(2, 0x8000);
    input_clear_buttons(3, 0x8000);
    *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x4000;
    return;
}

// 0x8031d8b8: predicate - true (1) when mode == 2.
unsigned int aveditor_is_mode_2(int mode)
{
    return countLeadingZeros(2 - mode) >> 5;
}

// 0x8031d8c8: predicate - true (1) when mode == 3.
unsigned int aveditor_is_mode_3(int mode)
{
    return countLeadingZeros(3 - mode) >> 5;
}

// 0x8031d8d8: enter test/play mode - reset the select screen, serialize the map,
// rebuild the view from the camera, then leave the editor and load the play models.
void aveditor_enter_test_mode(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                              unsigned long long a4, unsigned long long a5, unsigned long long a6,
                              unsigned long long a7, unsigned long long a8, int screen)
{
    unsigned long long ctx;

    asel_reset_screen(1);
    aveditor_serialize_map();
    aveditor_view_from_camera();
    FUN_80346df0(1);
    ctx = FUN_80346df0(0);         // note: original calls leave_screen result path
    aveditor_leave_screen((int)screen);
    ctx = (unsigned long long)(unsigned int)screen;
    aveditor_ensure_models_loaded((unsigned int)ctx, a2, a3, a4, a5, a6, a7, a8);
    DAT_805d59dc = 0;
    return;
}

// 0x8031d93c: exit to the parent screen - serialize the map then pop back.
void aveditor_exit_to_parent(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                             unsigned long long a4, unsigned long long a5, unsigned long long a6,
                             unsigned long long a7, unsigned long long a8)
{
    unsigned long long ret;

    aveditor_serialize_map();
    ret = FUN_80346df0(0);
    FUN_80320af0(ret, a2, a3, a4, a5, a6, a7, a8);
    return;
}

// 0x8031d970: dispatch the selected editor menu action (screen+0x188).
void aveditor_dispatch_menu_action(unsigned long long a1, unsigned long long a2,
                                   unsigned long long a3, unsigned long long a4,
                                   unsigned long long a5, unsigned long long a6,
                                   unsigned long long a7, unsigned long long a8, int screen)
{
    int stringId;
    unsigned long long ctx;

    switch (*(int *)(screen + 0x188))
    {
    case 0:
        aveditor_leave_screen(screen);
        break;
    case 1:
        hud_screen_push();
        break;
    case 2:
        ctx = FUN_8034757c(a1, a2, a3, a4, a5, a6, a7, a8, DAT_80612be8);
        FUN_803473c4(ctx, a2, a3, a4, a5, a6, a7, a8, DAT_80612be8, 1);
        aveditor_leave_screen(screen);
        aveditor_ensure_models_loaded((unsigned int)screen, a2, a3, a4, a5, a6, a7, a8);
        break;
    case 3:
        ctx = FUN_8034757c(a1, a2, a3, a4, a5, a6, a7, a8, DAT_80612be8);
        FUN_803473c4(ctx, a2, a3, a4, a5, a6, a7, a8, DAT_80612be8, 2);
        aveditor_leave_screen(screen);
        aveditor_ensure_models_loaded((unsigned int)screen, a2, a3, a4, a5, a6, a7, a8);
        break;
    case 4:
    case 5:
        hud_screen_push();
        break;
    case 6:
        aveditor_fit_view_to_bounds();
        aveditor_leave_screen(screen);
        break;
    case 7:
        avanim_play_move_sound(0x1575);
        stringId = get_string_by_id(0xa86);
        hud_create_yes_no_dialog(screen, stringId, -0x7fce2728, 0);
        break;
    case 8:
        *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x400;
        FUN_8033c4d8();
        break;
    case 9:
        *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x400;
        if (heap_arena_lookup() == 0)
        {
            FUN_8033c518();
        }
        else
        {
            FUN_80348e64();
        }
        break;
    case 10:
        hud_screen_push();
        break;
    case 0xb:
        if (DAT_805d59dc == 0)
        {
            aveditor_exit_to_parent(a1, a2, a3, a4, a5, a6, a7, a8);
        }
        else
        {
            avanim_play_move_sound(0x1575);
            stringId = get_string_by_id(0xa9f);
            hud_create_yes_no_dialog(screen, stringId, -0x7fce26c4, 0);
        }
        break;
    }
    return;
}

// 0x8031db24: per-frame editor screen tick - handle test-mode entry transitions
// (modes 2/3), then run the active cursor callback and rebuild the item list.
void aveditor_screen_tick(int screen)
{
    int root;
    unsigned int atRoot;
    bool ok;

    root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    atRoot = countLeadingZeros(root - screen);
    if ((*(int *)(screen + 0x188) == 2) && (ok = FUN_8034737c(), ok))
    {
        if ((atRoot >> 5) != 0)
        {
            FUN_80348298();
            goto rebuild;
        }
        FUN_80345da8(8);
    }
    else if (*(int *)(screen + 0x188) == 3)
    {
        ok = FUN_803473a0();
        if (ok)
        {
            if ((atRoot >> 5) != 0)
            {
                FUN_80348298();
                goto rebuild;
            }
        }
        FUN_80345da8(8);
    }
    else
    {
        FUN_80345da8(8);
    }
rebuild:
    if (((*(unsigned int *)(screen + 0xc0) & 0x100000) != 0) &&
        (hud_cursor_hit_test_rect((float *)(screen + 0xe0)) == 0))
    {
        if ((FUN_8032197c() != 0) && ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x2000) == 0))
        {
            (**(void (**)(int))(screen + 0x1bc))(screen);
        }
    }
    hud_screen_rebuild_items();
    return;
}

// 0x8031dc18: layout X coordinate for entry i (float table DAT_80480288 + i*8).
double aveditor_screen_param_x(int index)
{
    return (double)*(float *)(&DAT_80480288 + index * 8);
}

// 0x8031dc2c: layout Y coordinate for entry i (float table DAT_8048028c + i*8).
double aveditor_screen_param_y(int index)
{
    return (double)*(float *)(&DAT_8048028c + index * 8);
}
