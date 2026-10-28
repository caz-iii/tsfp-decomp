// menu_loadout_screen.cpp - "Select loadout" menu screen: builders + handlers.
//
// Constructs and drives the weapon-loadout selection screen (registered from
// the screen-handler tables at 0x80476380-0x804763a0). The screen offers a
// 16-entry weapon-slot list plus per-slot enable/disable logic, commits the
// choice into the game settings block, and shows/pops child screens.
//
//   menu_loadout_slot_activate  0x802c9294 - open child screen for a slot group.
//   menu_loadout_screen_close   0x802c92dc - back/close handler.
//   menu_loadout_screen_update  0x802c9334 - per-frame selection refresh + input.
//   menu_slot_is_selectable     0x802c9494 - slot-selectable predicate.
//   menu_slot_commit_setting    0x802c94f0 - commit selected slot into settings.
//   menu_slot_validate_setting  0x802c9520 - reset selection if not selectable.
//   menu_loadout_build_list     0x802c9564 - build the 16-entry slot list.
//   menu_loadout_screen_build   0x802c9630 - top-level screen builder.
//   menu_weapon_preview_build   0x802c98e8 - weapon-preview panel builder.
//
// Address range: 0x802c9294 - 0x802c994f

#include "common.h"

// Cross-file callback defined in hud_bar_render.cpp with C++ linkage; declared
// here (outside extern "C") so the linkage matches.
void hud_weapon_preview_render(unsigned int p0, unsigned int p1, float *rect);

extern "C" {

extern int   DAT_805cc614;    // active game mode id
extern int   DAT_80612614;    // active-state selector (0x66 = shutdown)
extern int   DAT_80612be4;    // active screen object ptr
extern int   DAT_80612b08;    // last-shown weapon slot
extern float FLOAT_80612b04;  // slot-change highlight timer
extern int   DAT_80611a54;    // UI-sound enabled flag
extern int   DAT_80611a58;    // *ptr -> UI-sound descriptor block
extern int   DAT_806119e0;    // active layout element handle
extern int   DAT_805cc584;    // per-slot flag/string table (stride 4)

extern const float FLOAT_80619f08;  // 0.0
extern const float FLOAT_80619f5c;  // slot-change highlight reset value
extern const float FLOAT_80619f6c;  // element x position
extern const float FLOAT_80619f7c;  // element y (shutdown)
extern const float FLOAT_80619f80;  // element y (normal)
extern const float FLOAT_80619f90;  // preview element y
extern const float FLOAT_80619f8c;  // preview spacer height

// screen-child data pointer passed to hud_screen_show_by_id (0x80476...)
#define LOADOUT_CHILD_DATA (-0x7fb89c50)

// ---- named cross-module helpers (resolve via generated_decls) ----
extern int   mode_slot_group_id(unsigned int slot);
extern unsigned int mode_slot_is_high(int slot);
extern int   mode_loadout_get_slot(int slotIndex);
extern void  hud_screen_show_by_id(int screen, int child_id);
extern int   hud_screen_node_is_valid(void);
extern void  hud_screen_pop_current(void);
extern int   hud_screen_all_children_closing(int screen);
extern void  hud_screen_set_handler_param_b(int screen, int handler_id, int value);
extern void  hud_item_add_spacer(float height, unsigned int flags);
extern int  *hud_event_add_string(int string_ptr);
extern int   hud_get_item_counter(void);
extern void  hud_event_set_disabled(int event, int disabled);
extern int  *hud_get_last_created_item(void);
extern void  hud_item_set_change_callback(int item, void *callback);
extern void  hud_item_on_value_change(void);
extern void  hud_element_init_from_layout(int flags, void *data, int cancel_cb, int confirm_cb);
extern void  hud_add_list_item(void);
extern void  hud_add_callback_item_default(int index);
extern void  hud_add_spacer_item(float height, int adjust_margins, int param);
extern void  hud_element_set_pos_x(double x, int element);
extern void  hud_element_set_pos_y(double y, int element);
extern void  hud_screen_register_handler_global(int handler_id, int a, int b, int c);
extern void  hud_set_focus_by_id(int element, int item_id);
extern void  hud_element_finalize_frame(void);
extern int   get_string_by_id(int id);
extern void *game_get_settings_ptr(void);
extern void  sound_init_play_params(int params_ptr);
extern unsigned int sound_play_request(int *params);

// ---- raw helpers (per-file) ----
extern unsigned int FUN_802ca2e4(int screen);     // resolve slot index from screen
extern void  FUN_8032068c(int slot, int value);   // shutdown-path slot reset
extern void  FUN_80320700(void);                   // shutdown-path list build
extern unsigned long long FUN_803826f0(void);      // frame-enter stub
extern void  FUN_8038273c(void);                   // frame-leave stub
extern unsigned long long __save_gpr_28(void);
extern void  __restore_gpr_28(void);


// 0x802c9294 - open the child screen for a slot when its group id is non-zero.
void menu_loadout_slot_activate(int screen)
{
    unsigned int slot = FUN_802ca2e4(screen);
    if (mode_slot_group_id(slot) != 0) {
        hud_screen_show_by_id(*(int *)(screen + 0x2c), LOADOUT_CHILD_DATA);
    }
}

// 0x802c92dc - back/close handler: clear the active screen's flag bit 0x100
// during shutdown, then pop the current screen.
void menu_loadout_screen_close(void)
{
    if (hud_screen_node_is_valid() != 0) {
        if (DAT_80612614 == 0x66) {
            *(unsigned int *)(DAT_80612be4 + 0x238) &= 0xfffffeff;
        }
        hud_screen_pop_current();
    }
}

// 0x802c9334 - per-frame update: when children are closing, refresh the shown
// weapon slot + highlight timer and toggle the per-slot handler flags. On the
// commit input flag (0xc0 bit 0x800000), clear the weapon-selection setting
// (or run the shutdown reset) and play the UI sound.
void menu_loadout_screen_update(int screen)
{
    int slot;
    int local_38[13];

    if (hud_screen_all_children_closing(screen) != 0) {
        if (*(int *)(screen + 0x188) == -5) {
            slot = mode_loadout_get_slot(0);
            if (DAT_80612b08 != slot) {
                FLOAT_80612b04 = FLOAT_80619f5c;
                DAT_80612b08 = slot;
            }
            hud_screen_set_handler_param_b(screen, 0, 1);
            hud_screen_set_handler_param_b(screen, 3, 1);
        } else {
            slot = mode_loadout_get_slot(*(int *)(screen + 0x188));
            if (slot != DAT_80612b08) {
                FLOAT_80612b04 = FLOAT_80619f5c;
                DAT_80612b08 = slot;
            }
            if (mode_slot_group_id(FUN_802ca2e4(screen)) != 0) {
                hud_screen_set_handler_param_b(screen, 0, 0);
                if (0 < DAT_80612b08) {
                    hud_screen_set_handler_param_b(screen, 3, 0);
                }
            }
        }
    }

    if ((*(unsigned int *)(screen + 0xc0) & 0x800000) != 0) {
        unsigned char *settings = (unsigned char *)game_get_settings_ptr();
        int idx = *(int *)(screen + 0x180) + -2;
        if (idx >= 0) {
            if (DAT_80612614 == 0x66) {
                FUN_8032068c(idx, 0);
            } else {
                *(int *)(settings + idx * 4 + (*(int *)(settings + 0x6a4) + -10) * 0x44 + 0x13c) = 0;
            }
            if (DAT_80611a54 != 0) {
                int soundId = *(int *)(DAT_80611a58 + 0xc);
                sound_init_play_params((int)local_38);
                local_38[0] = soundId;
                sound_play_request(local_38);
            }
        }
    }
}

// 0x802c9494 - slot-selectable predicate: 1 for mode 9, or when the slot is
// neither high (>=15) nor exactly 9; else 0.
int menu_slot_is_selectable(int slot)
{
    if ((DAT_805cc614 == 9) ||
        ((mode_slot_is_high(slot) == 0) && (slot != 9))) {
        return 1;
    }
    return 0;
}

// 0x802c94f0 - commit the menu item's value (+0xc8) into the settings block
// (+0x6a4). item change callback.
void menu_slot_commit_setting(unsigned int p0, int item)
{
    int value = *(int *)(item + 0xc8);
    unsigned char *settings = (unsigned char *)game_get_settings_ptr();
    *(int *)(settings + 0x6a4) = value;
    (void)p0;
}

// 0x802c9520 - validate the current settings selection; reset to 0 if the slot
// is not selectable.
void menu_slot_validate_setting(void)
{
    unsigned char *settings = (unsigned char *)game_get_settings_ptr();
    if (menu_slot_is_selectable(*(int *)(settings + 0x6a4)) == 0) {
        settings = (unsigned char *)game_get_settings_ptr();
        *(int *)(settings + 0x6a4) = 0;
    }
}

// 0x802c9564 - build the 16-entry weapon-slot list: validate, add a spacer,
// then add each slot's string (disabling non-selectable entries) and install
// the change callback.
void menu_loadout_build_list(void)
{
    FUN_803826f0();
    if (DAT_80612614 == 0x66) {
        FUN_80320700();
    } else {
        int slot;
        menu_slot_validate_setting();
        hud_item_add_spacer((float)FLOAT_80619f08, 0x30400);
        for (slot = 0; slot < 0x10; slot++) {
            hud_event_add_string(*(int *)((int)&DAT_805cc584 + slot * 4));
            if (menu_slot_is_selectable(slot) == 0) {
                hud_event_set_disabled(hud_get_item_counter(), 1);
            }
        }
        int *last = hud_get_last_created_item();
        *(unsigned int *)((int)last + 0x28) |= 0x4000000;
        hud_item_set_change_callback((int)hud_get_last_created_item(),
                                     (void *)menu_slot_commit_setting);
        game_get_settings_ptr();
        hud_get_last_created_item();
        hud_item_on_value_change();
    }
    FUN_8038273c();
}

// 0x802c9630 - top-level "select loadout" screen builder.
void menu_loadout_screen_build(void)
{
    int element;

    element = (int)(unsigned int)__save_gpr_28();
    hud_element_init_from_layout(0x20010, 0, 0, (int)menu_loadout_screen_close);
    get_string_by_id(0xe2b);
    hud_add_list_item();
    hud_add_list_item();
    menu_loadout_build_list();
    hud_add_callback_item_default(0);
    for (int i = 0; i < 6; i++) {
        hud_add_list_item();
    }
    hud_element_set_pos_x((double)FLOAT_80619f6c, DAT_806119e0);
    if (DAT_80612614 == 0x66) {
        hud_element_set_pos_y((double)FLOAT_80619f7c, DAT_806119e0);
    } else {
        hud_element_set_pos_y((double)FLOAT_80619f80, DAT_806119e0);
    }
    hud_screen_register_handler_global(0, get_string_by_id(0xe2d), 1, 0);
    hud_screen_register_handler_global(3, get_string_by_id(0xe2e), 1, 0);
    hud_screen_register_handler_global(1, get_string_by_id(0x19c8), 0, 0);
    hud_set_focus_by_id(element, -5);
    hud_element_finalize_frame();
    __restore_gpr_28();
}

// 0x802c98e8 - build the weapon-preview panel (custom-render spacer item that
// calls hud_weapon_preview_render).
void menu_weapon_preview_build(void)
{
    hud_element_init_from_layout(0x20010, 0, 0, 0);
    hud_add_spacer_item(FLOAT_80619f8c, 0, (int)hud_weapon_preview_render);
    hud_element_set_pos_x((double)FLOAT_80619f6c, DAT_806119e0);
    hud_element_set_pos_y((double)FLOAT_80619f90, DAT_806119e0);
    hud_element_finalize_frame();
}

} // extern "C"
