// menu_loadout_screen2.cpp - "Select loadout / gamemode" screen (part 2).
//
// Continuation of menu_loadout_screen.cpp covering the commit path, the
// per-frame tick, the gamemode-select list builder, the 3D weapon/character
// preview animator, the top-level screen builder, and the slot-eligibility
// predicates used across the loadout UI.
//
//   menu_loadout_commit_selection  0x802c9950 - commit the highlighted slot.
//   menu_loadout_screen_tick       0x802c9a24 - per-frame flag/highlight tick.
//   menu_gamemode_build_list       0x802c9ae0 - build the gamemode list.
//   menu_loadout_preview_reset     0x802c9ca0 - screen-destroy: drop preview.
//   menu_loadout_preview_animate   0x802c9cc0 - spin/scale the preview model.
//   menu_loadout_screen_create     0x802c9f70 - top-level screen creator.
//   menu_slot_is_extra_bot         0x802ca040 - "extra bot" slot predicate.
//   menu_slot_tier_scale           0x802ca0a8 - (0xf - tableField) / 3.
//   menu_slot_is_last              0x802ca0cc - slot == 0x11 predicate.
//   menu_slot_is_last_in_mode9     0x802ca0dc - mode==9 && slot == 0x11.
//
// Address range: 0x802c9950 - 0x802ca12b

#include "common.h"

// Cross-file callback defined in hud_bar_render.cpp with C++ linkage.
void hud_weapon_preview_render(unsigned int p0, unsigned int p1, float *rect);

extern "C" {

// ---- data globals (per-file; may be typed differently in other TUs) ----
extern int   DAT_805cc614;    // active game mode id
extern int   DAT_80612614;    // active-state selector (0x66 = shutdown)
extern int   DAT_80612b08;    // last-shown weapon slot index
extern int   DAT_80612b34;    // spawned preview entity ptr
extern float FLOAT_80612b04;  // slot-change highlight timer
extern float FLOAT_80612b2c;  // preview base scale
extern float FLOAT_80612b30;  // preview spin/scale accumulator
extern int   DAT_80611a54;    // UI-sound enabled flag
extern int   DAT_80611a58;    // *ptr -> UI-sound descriptor block
extern int   DAT_806119e0;    // active layout element handle
extern unsigned char DAT_805cc68e;  // current gamemode selector byte
extern int   DAT_806109c0;    // frame-time raw counter (signed magic-bias src)

// gamemode/string tables
extern int   DAT_8044098c;    // gamemode id table (stride 4)
extern int   DAT_8043f294;    // gamemode string-id table (stride 0x15 ints)
extern unsigned int DAT_8043f29c;  // gamemode preview model-id table (stride 0x15)
extern float DAT_8043f2a4;    // preview per-mode scale table (stride 0x54)
extern float DAT_8043f2a8;    // preview per-mode pos table (stride 0x54)
extern float DAT_8043f2ac;
extern int   DAT_8043f2b0;
extern int   DAT_8048ca8c;    // per-slot tier table (stride 0x30)

// preview spawn template block (0x803e088c..0x803e08a0)
extern int   DAT_803e088c;
extern int   DAT_803e0890;
extern int   DAT_803e0894;
extern float DAT_803e0898;
extern int   DAT_803e089c;
extern int   DAT_803e08a0;
extern int   DAT_80532a30;    // preview entity vtable/behaviour ptr

// float/double constants
extern const float  FLOAT_80619ee8;   // preview spin span
extern const float  FLOAT_80619f08;   // 0.0 / min timer
extern const float  FLOAT_80619f38;   // preview drift rate
extern const float  FLOAT_80619f5c;   // slot-change highlight reset value
extern const float  FLOAT_80619f6c;   // element x (normal)
extern const float  FLOAT_80619f7c;   // element y (shutdown)
extern const float  FLOAT_80619f98;   // list item padding
extern const float  FLOAT_80619f9c;   // list scroll amount
extern const float  FLOAT_80619fa0;   // list element x
extern const float  FLOAT_80619fa4;   // preview scale threshold
extern const float  FLOAT_80619fa8;   // preview drift clamp
extern const float  FLOAT_806108e8;   // frame delta (drift)
extern const float  FLOAT_806108ec;   // frame delta (spin)
extern const double DOUBLE_80619f00;  // magic-bias reference

#define LOADOUT_CHILD_DATA (-0x7fb89c70)

// builder-table data pointers referenced by the screen creator (0x80476390..)
extern void *PTR_menu_loadout_screen_build_80476390;
extern void *PTR_FUN_804763b0;
extern void *PTR_menu_weapon_preview_build_804763a0;

// ---- named cross-module helpers (resolve via generated_decls) ----
extern int   get_string_by_id(int id);
extern void *game_get_settings_ptr(void);
extern void  sound_init_play_params(int params_ptr);
extern unsigned int sound_play_request(int *params);
extern int   hud_screen_all_children_closing(int screen);
extern void  hud_screen_show_by_id(int screen, int child_id);
extern void  hud_element_init_from_layout(int flags, int data, int cancel_cb, int confirm_cb);
extern void  hud_element_create(int screen, void *builder, int kind);
extern void  hud_element_set_user_data(int screen, void *callback);
extern void  hud_element_finalize_frame(void);
extern void  hud_element_set_pos_x(double x, int element);
extern void  hud_element_set_pos_y(double y, int element);
extern void  hud_element_apply_scroll(void);
extern void  hud_element_scroll_vertical(double amount, int mode);
extern void  hud_set_item_padding_mode(double pad, int a, int b, int c);
extern void  hud_add_list_item(void);
extern void  hud_add_callback_item_default(int index);
extern int   hud_get_last_created_item(void);
extern void  hud_item_set_locked(int item, int locked);
extern int   hud_element_is_closing(int element);
extern void  hud_screen_register_handler_global(int handler_id, int a, int b, int c);
extern int   award_grant_generic(void);
extern int   gamemode_supports_bots(unsigned int mode, int entry);
extern int   mode_reset_tracked_entity(void);
extern int   resource_get_load_state(int a, int b);
extern void  model_begin_load(int model, double a, double b, double c,
                              double d, double e, double f, double g,
                              int h, int i, unsigned int modelId,
                              int j, int k, int l, int m, int n);
extern int   entity_spawn_by_type(double a, double b, double c, double d,
                                  double e, double f, double g, double h,
                                  unsigned int modelId, void *tmpl,
                                  void *pos, int i, int j);

extern int   hud_screen_find_child_by_id(int screen, int child_id);
extern void  hud_set_focus_by_id_nth(int element, int item_id, int nth);
extern unsigned int input_get_buttons_held(int pad);

// ---- raw helpers (per-file; same addr may differ across TUs) ----
extern void  FUN_803826e4(void);    // frame-enter stub
extern void  FUN_80382730(void);    // frame-leave stub
extern int   menu_screen_ctx_get(void);  // FUN_803826e8 spawn-frame ctx accessor
extern void  menu_screen_ctx_end(void);  // FUN_80382734 spawn-frame ctx close
extern void  FUN_8032068c(int slot, int value);  // shutdown-path slot write
extern void  FUN_802cf21c(int mode); // set preview render mode
// countLeadingZeros: PPC cntlzw intrinsic provided by common.h.


// 0x802c9ae0 - build the gamemode-select list: one entry per supported mode
// (0x45 entries), locking modes that don't support bots. Positions the list
// element and registers the confirm/back handlers.
static void menu_gamemode_build_list(void)
{
    int i, modeId, granted;

    FUN_803826e4();
    hud_element_init_from_layout(~-(int)(DAT_80612614 == 0x66) & 0x20000, 0, 0, 0);
    get_string_by_id(0xe2c);
    hud_add_list_item();
    hud_add_callback_item_default(0);
    hud_set_item_padding_mode((double)FLOAT_80619f98, 1, 1, 1);
    hud_element_scroll_vertical((double)FLOAT_80619f9c, 0);

    for (i = 1; i < 0x46; i++) {
        modeId = *(int *)((int)&DAT_8044098c + i * 4);
        granted = award_grant_generic();
        if (granted != 0) {
            get_string_by_id((&DAT_8043f294)[modeId * 0x15]);
            hud_add_list_item();
            if (gamemode_supports_bots((unsigned int)DAT_805cc68e, modeId) != 0) {
                hud_item_set_locked(hud_get_last_created_item(), 1);
            }
        }
    }

    hud_element_apply_scroll();
    hud_element_set_pos_x((double)FLOAT_80619fa0, DAT_806119e0);
    if (DAT_80612614 == 0x66) {
        hud_element_set_pos_y((double)FLOAT_80619f7c, DAT_806119e0);
    } else {
        hud_element_set_pos_y((double)FLOAT_80619f6c, DAT_806119e0);
    }
    hud_screen_register_handler_global(0, get_string_by_id(0xe2f), 0, 0);
    hud_screen_register_handler_global(1, get_string_by_id(0x19c8), 0, 0);
    hud_element_finalize_frame();
    FUN_80382730();
}

// 0x802c9ca0 - screen-destroy callback: release the tracked preview entity.
static void menu_loadout_preview_reset(void)
{
    mode_reset_tracked_entity();
}

// 0x802c9950 - commit the currently highlighted slot into the settings block
// (or the shutdown path), advance the focus, and re-show the child screen.
void menu_loadout_commit_selection(void)
{
    int ctx = menu_screen_ctx_get();
    if ((*(unsigned int *)(*(int *)(ctx + 0x194) + 0x28) & 1) != 0) {
        return;
    }
    int child = hud_screen_find_child_by_id(*(int *)(ctx + 0x2c), LOADOUT_CHILD_DATA);
    int idx = *(int *)(child + 0x180) + -2;
    int value = *(int *)(ctx + 0x188);
    if (DAT_80612614 == 0x66) {
        FUN_8032068c(idx, value);
    } else {
        unsigned char *settings = (unsigned char *)game_get_settings_ptr();
        *(int *)(settings + idx * 4 + (*(int *)(settings + 0x6a4) + -10) * 0x44 + 0x13c) = value;
    }
    if (*(int *)(child + 0x188) < 5) {
        *(int *)(child + 0x188) += 1;
        hud_set_focus_by_id_nth(child, *(int *)(child + 0x188), 0);
    }
    hud_screen_show_by_id(*(int *)(ctx + 0x2c), LOADOUT_CHILD_DATA);
    menu_screen_ctx_end();
}

// 0x802c9a24 - per-frame screen tick: toggle the screen's closing flag, refresh
// the slot-change highlight, and on the confirm-input flag play the UI sound
// before re-showing the child screen.
void menu_loadout_screen_tick(int screen)
{
    if (hud_screen_all_children_closing(screen) == 0) {
        *(unsigned int *)(screen + 0x40) |= 0x100;
    } else {
        *(unsigned int *)(screen + 0x40) &= ~0x100u;
        if (DAT_80612b08 != *(int *)(screen + 0x188)) {
            FLOAT_80612b04 = FLOAT_80619f5c;
            DAT_80612b08 = *(int *)(screen + 0x188);
        }
    }
    unsigned int held = input_get_buttons_held(0);
    if ((held & 0x10) != 0) {
        if (DAT_80611a54 != 0) {
            int local_38[13];
            int soundId = *(int *)(DAT_80611a58 + 0xc);
            sound_init_play_params((int)local_38);
            local_38[0] = soundId;
            sound_play_request(local_38);
        }
    }
    hud_screen_show_by_id(*(int *)(screen + 0x2c), LOADOUT_CHILD_DATA);
}

// 0x802c9cc0 - animate the loadout preview: fade the spin timer in, lazily
// spawn the preview entity/model for the selected mode, then apply the
// per-frame spin + vertical drift to the spawned entity.
void menu_loadout_preview_animate(int screen, int p1, int p2, int p3,
                                  int p4, int p5, int p6, int p7)
{
    unsigned int modelId = 0xffffffff;

    if (DAT_80612614 == 0x66) {
        return;
    }

    // spin timer ramp toward FLOAT_80612b04, clamped to a minimum.
    double frameT = (double)((double)(int)(DAT_806109c0) - DOUBLE_80619f00);
    double spin = -(double)(float)((double)FLOAT_806108ec *
                    (double)(float)((double)FLOAT_80619ee8 / (double)(float)frameT) -
                    (double)FLOAT_80612b04);
    if (spin < (double)FLOAT_80619f08) {
        spin = (double)FLOAT_80619f08;
    }
    FLOAT_80612b04 = (float)spin;

    if ((spin <= (double)FLOAT_80619f08) &&
        (hud_element_is_closing(screen) == 0) &&
        (DAT_80612b08 >= 0)) {
        modelId = (&DAT_8043f29c)[DAT_80612b08 * 0x15];
        if ((modelId != 0xffffffff) &&
            ((DAT_80612b34 == 0) ||
             (*(unsigned short *)(DAT_80612b34 + 0x94) != (unsigned short)modelId))) {
            int loaded;
            double posX = (double)FLOAT_80619ee8;
            double posY = (double)FLOAT_806108ec;
            mode_reset_tracked_entity();
            model_begin_load((int)modelId, posX, posY, spin,
                             0, 0, 0, 0, 0, 0, modelId, p4, p5, p6, p7, 0);
            loaded = resource_get_load_state(0, 0);
            if (loaded != 0) {
                int tmpl[6];
                float local_pos[3];
                int slotBase = DAT_80612b08 * 0x54;
                tmpl[0] = DAT_803e088c;
                tmpl[1] = DAT_803e0890;
                tmpl[2] = DAT_803e0894;
                tmpl[3] = *(int *)&DAT_803e0898;
                tmpl[4] = DAT_803e089c;
                tmpl[5] = DAT_803e08a0;
                mode_reset_tracked_entity();
                local_pos[0] = *(float *)((int)&DAT_8043f2a8 + slotBase);
                local_pos[1] = *(float *)((int)&DAT_8043f2ac + slotBase);
                *(int *)&local_pos[2] = *(int *)((int)&DAT_8043f2b0 + slotBase);
                DAT_80612b34 = entity_spawn_by_type(
                    (double)local_pos[1], (double)local_pos[0], posY, spin,
                    0, 0, 0, 0, modelId, &tmpl[1], &local_pos[0], 0, 0);
                *(int **)(DAT_80612b34 + 0x254) = &DAT_80532a30;
                FLOAT_80612b30 = FLOAT_80619f08;
                *(unsigned int *)(DAT_80612b34 + 0x28) |= 0x40000000;
                FLOAT_80612b2c = *(float *)((int)&DAT_8043f2a4 + DAT_80612b08 * 0x54);
            }
        }
    }

    if (DAT_80612b34 != 0) {
        double dT = (double)((double)(int)(DAT_806109c0) - DOUBLE_80619f00);
        FLOAT_80612b30 = FLOAT_806108e8 * (float)((double)FLOAT_80619ee8 / dT) + FLOAT_80612b30;
        if (FLOAT_80619ee8 < FLOAT_80612b30) {
            FLOAT_80612b30 = FLOAT_80619ee8;
        }
        if (FLOAT_80619fa4 < FLOAT_80612b30) {
            *(float *)(DAT_80612b34 + 0x6c) = FLOAT_80619ee8 / (FLOAT_80612b30 * FLOAT_80612b2c);
        }
        float drift = FLOAT_80619f38;
        float clamp = FLOAT_80619fa8;
        *(float *)(DAT_80612b34 + 0x70) = drift * FLOAT_806108e8 + *(float *)(DAT_80612b34 + 0x70);
        *(float *)(DAT_80612b34 + 0xc4) = drift * FLOAT_806108e8 + *(float *)(DAT_80612b34 + 0xc4);
        if (clamp < *(float *)(DAT_80612b34 + 0x70)) {
            *(float *)(DAT_80612b34 + 0xc4) -= clamp;
            *(float *)(DAT_80612b34 + 0x70) -= clamp;
        }
    }

    if (modelId == 0xffffffff) {
        mode_reset_tracked_entity();
    }

    (void)p1; (void)p2; (void)p3;
}

// 0x802c9f70 - top-level loadout-screen creator: title, child builders, initial
// preview state, and the screen-destroy callback.
void menu_loadout_screen_create(int screen)
{
    hud_element_init_from_layout(0x40100000, get_string_by_id(0x365), 0, 0);
    hud_element_create(screen, &PTR_menu_loadout_screen_build_80476390, 2);
    hud_element_create(screen, &PTR_FUN_804763b0, 2);
    if (DAT_80612614 != 0x66) {
        hud_element_create(screen, &PTR_menu_weapon_preview_build_804763a0, 2);
    }
    hud_screen_show_by_id(screen, LOADOUT_CHILD_DATA);
    hud_element_finalize_frame();
    DAT_80612b08 = 0;
    FLOAT_80612b04 = FLOAT_80619f08;
    mode_reset_tracked_entity();
    FUN_802cf21c(4);
    hud_element_set_user_data(screen, (void *)menu_loadout_preview_reset);
    (void)(void *)&menu_gamemode_build_list;  // referenced via a builder table pointer
}

// 0x802ca040 - "extra bot" slot predicate: true when mode 0x13 and the slot
// index falls in the settings-driven extra-bot range.
int menu_slot_is_extra_bot(int slot)
{
    if (DAT_805cc68e == 0x13) {
        unsigned char *settings = (unsigned char *)game_get_settings_ptr();
        if (10 - (*(int *)(settings + 0xc) + 1) <= slot) {
            return 1;
        }
    }
    return 0;
}

// 0x802ca0a8 - tier scale from the per-slot table: (0xf - field) / 3.
int menu_slot_tier_scale(int slot)
{
    return (0xf - *(int *)((int)&DAT_8048ca8c + slot * 0x30)) / 3;
}

// 0x802ca0cc - true when the slot index is exactly 0x11.
unsigned int menu_slot_is_last(int slot)
{
    return countLeadingZeros((unsigned int)(0x11 - slot)) >> 5;
}

// 0x802ca0dc - true when the active gamemode flag is 9 and the slot is 0x11.
int menu_slot_is_last_in_mode9(int slot)
{
    if ((DAT_805cc614 == 9) && (menu_slot_is_last(slot) != 0)) {
        return 1;
    }
    return 0;
}

} // extern "C"
