// Batch 828 | Date: 2026-09-11
// Profile submenu + display/video options menu cluster: the profile-name edit
// entry point and menu refresh, the profile submenu builder, and the display
// options screens (widescreen toggle, screen-position calibration, and the
// VI/GX render-mode reconfigure they drive). Continuation of the menu family
// (batch_817-827).
//
//   0x802dfab4 hud_profile_name_edit_begin  (frame-actor: save kbd state, open editor)
//   0x802dfbd4 hud_profile_menu_refresh      (recompute empty-slot mask + labels)
//   0x802dfcb8 hud_profile_submenu_build     (profile submenu list)
//   0x802dfe10 hud_display_apply_render_mode  (VI/GX render-mode reconfigure)
//   0x802dfef0 hud_display_confirm_and_push   (confirm display change, push screen)
//   0x802dff7c hud_display_widescreen_changed (widescreen toggle change cb)
//   0x802dffb4 hud_display_calibrate_tick     (screen-position calibration anim)
//   0x802e00c4 hud_display_calibrate_apply    (apply calibration, navigate back)
//   0x802e00fc hud_display_calibrate_build     (build calibration screen)
//   0x802e020c hud_display_options_build        (build display options list)
//
// Subsystem: game (HUD menu logic; hud_display_apply_render_mode also touches
// VI/GX via the SDK to switch progressive/interlaced + widescreen).
//
// Named hud_*/gx_*/text_*/input_*/get_string_by_id/sound_* helpers resolve via
// hud.h -> generated_decls.h (which also pulls generated_externs.h for the
// permissive-only names). Only raw FUN_/DAT_/FLOAT_/PTR_ symbols are declared
// locally.

#include "common.h"
#include "hud.h"

// ============================================================================
// Per-file externs for raw symbols.
// ============================================================================

// frame-actor push/pop (raw variant)
extern int  FUN_803826f0(void);
extern void FUN_8038273c(void);

// display / render-mode side helpers (raw)
extern void FUN_8039edf0(unsigned char progressive);  // set field-render mode
extern void FUN_8039ec70(char flag);                  // apply render flag
extern void FUN_802e59e0(void);                        // display refresh / re-layout

// callback + screen-descriptor pointer data (code addresses)
extern int DAT_802df864;   // profile-menu cancel callback (code ptr near profile edit)
extern int PTR_FUN_8047728c; // screen descriptor -> hud_profile_submenu_build
extern int PTR_FUN_804772ac; // screen descriptor -> hud_display_options_build

// globals
extern int   DAT_805ce80c;   // profile name string table base (0x40 stride)
extern int   DAT_805ce94c;   // profile name max-length table (int[], 0x04 stride)
extern int   DAT_805ce98c;   // active profile slot index
extern int   DAT_805ce990;   // previously-focused profile slot (for restore)
extern unsigned char DAT_805ceb8c; // per-slot "name empty" mask
extern int   DAT_805ceb90;   // profile editor: owning screen ptr
extern int   DAT_806119e0;   // shared center-XY layout token
extern int   DAT_80612244;   // widescreen flag
extern int   DAT_80612ae4;   // progressive-scan flag
extern int   DAT_80612ac4;   // active display-options screen ptr
extern int   DAT_80612ae0;   // display-transition state
extern int   DAT_8061109c;   // frame countdown (0x3c == 1s)
extern int   DAT_8060f948;   // active layout mode id
extern int   DAT_80612af0;   // widescreen-toggle bound value
extern int   DAT_80611058;   // calibration format context
extern float FLOAT_806110a0; // calibration current Y offset (animated)

// float layout constants
extern float FLOAT_8061a014;   // profile submenu Y position
extern float FLOAT_80619f08;   // clip rect / calibration min
extern float FLOAT_80619f5c;   // clip rect param
extern float FLOAT_80619ff4;   // clip rect param
extern float FLOAT_80619f4c;   // clip rect param
extern float FLOAT_80619eec;   // clip rect param
extern float FLOAT_80619f38;   // item padding
extern float FLOAT_80619fc4;   // item padding (display list)
extern float FLOAT_80619fbc;   // calibration reset value
extern float FLOAT_80619ee8;   // 1.0 constant
extern float FLOAT_806108e8;   // calibration step scale
extern unsigned int DAT_806109c0; // render width (int->float bias source)
extern double DOUBLE_80619f00;    // int->double magic bias

// FUN_80387fb8: variadic formatted-text/anim helper used across menu code
extern int FUN_80387fb8(double, double, double, double, unsigned long long, unsigned long long,
                        unsigned long long, unsigned long long, int fmt, int* ctx, int val,
                        unsigned long long, unsigned long long, unsigned long long,
                        unsigned long long, unsigned long long);

// forward decl: render-mode reconfigure called by tick/apply below
void hud_display_apply_render_mode(double p1, double p2, double p3, double p4,
                                   unsigned long long p5, unsigned long long p6,
                                   unsigned long long p7, unsigned long long p8, int mode);

// ============================================================================
// 0x802dfab4 | hud_profile_name_edit_begin
// Frame-actor entry that opens the profile-name editor for the focused slot:
// plays the select sound (0xd1e), records the active slot (screen+0x188),
// snapshots the current name buffer via keyboard_buffer_save_state, toggles the
// editor's "new profile" flag (screen+0x224 bit 0x4000) for slot 0, hides the
// current screen, opens the editor bound to a screen descriptor, and seeds the
// editor's name buffer/length fields.
// ============================================================================
void hud_profile_name_edit_begin(void) {
    int soundParams[18];
    int actor = FUN_803826f0();

    sound_init_play_params((int)soundParams);
    soundParams[0] = 0xd1e;
    sound_play_request(soundParams);

    DAT_805ce98c = *(int*)(actor + 0x188);
    unsigned char* nameBuf = (unsigned char*)(&DAT_805ce80c + DAT_805ce98c * 0x40);
    DAT_805ce990 = DAT_805ce98c;
    keyboard_buffer_save_state((int)nameBuf, (int)nameBuf, (int*)nameBuf, (int*)nameBuf);

    if (*(int*)(actor + 0x188) == 0) {
        *(unsigned int*)(DAT_805ceb90 + 0x224) |= 0x4000;
    } else {
        *(unsigned int*)(DAT_805ceb90 + 0x224) &= 0xffffbfff;
    }
    hud_screen_mark_hidden(DAT_805ceb90);
    hud_profile_name_edit_open(actor, (int)&PTR_FUN_8047728c);
    *(int*)(DAT_805ceb90 + 0x22c) = *(int*)(&DAT_805ce94c + DAT_805ce98c * 4);
    *(int*)(DAT_805ceb90 + 0x220) = (int)(&DAT_805ce80c + DAT_805ce98c * 0x40);
    int len = text_strlen(*(char**)(DAT_805ceb90 + 0x220));
    *(int*)(DAT_805ceb90 + 0x228) = len;
    FUN_8038273c();
    return;
}

// ============================================================================
// 0x802dfbd4 | hud_profile_menu_refresh
// Recomputes the two-slot "name empty" mask (DAT_805ceb8c bits 0/1 from the
// slot-empty tests), swaps the primary handler label between 0x19c7 and 0x35a
// depending on focus, and enables/disables child item 9 (the "continue"
// entry, +0x28 bit 0) based on whether any profile name is set.
// ============================================================================
void hud_profile_menu_refresh(int screen) {
    int* child;

    DAT_805ceb8c = 0;
    DAT_805ceb8c |= (unsigned char)hud_profile_slot0_is_empty();
    DAT_805ceb8c |= (unsigned char)(hud_profile_slot1_is_empty() << 1);

    if (*(int*)(screen + 0x188) == 9) {
        hud_screen_set_handler_param_a(screen, 0, get_string_by_id(0x19c7));
    } else {
        hud_screen_set_handler_param_a(screen, 0, get_string_by_id(0x35a));
    }

    if (DAT_805ceb8c == 0) {
        child = hud_find_child_by_id(screen, 9);
        *(unsigned int*)((int)child + 0x28) &= 0xfffffffe;
    } else {
        child = hud_find_child_by_id(screen, 9);
        *(unsigned int*)((int)child + 0x28) |= 1;
    }
    return;
}

// ============================================================================
// 0x802dfcb8 | hud_profile_submenu_build
// Builds the profile submenu list (title 0x492): a few labeled entries
// (0x28e/0x493/0x494/0x49c) with two default callback items, restores the
// remembered focus (DAT_805ce990), registers the A/B handlers, positions and
// finalizes. The cancel callback is the profile-edit tick (DAT_802df864).
// ============================================================================
void hud_profile_submenu_build(int screen) {
    int strId;

    strId = get_string_by_id(0x492);
    hud_element_init_from_layout(0, (void*)strId, 0, (int)&DAT_802df864);
    get_string_by_id(0x28e);
    hud_add_list_item();
    hud_add_callback_item_default(0);
    get_string_by_id(0x493);
    hud_add_list_item();
    get_string_by_id(0x494);
    hud_add_list_item();
    hud_add_callback_item_default(1);
    get_string_by_id(0x49c);
    hud_add_list_item();
    hud_set_focus_by_id(screen, DAT_805ce990);
    strId = get_string_by_id(0x35a);
    hud_screen_register_handler_global(0, strId, 0, 0);
    strId = get_string_by_id(0x19c8);
    hud_screen_register_handler_global(1, strId, 0, 0);
    hud_element_set_pos_y((double)FLOAT_8061a014, DAT_806119e0);
    hud_element_apply_center_x(DAT_806119e0);
    hud_element_finalize_frame();
    return;
}

// ============================================================================
// 0x802dfe10 | hud_display_apply_render_mode
// Reconfigures the VI/GX render mode for the selected display setting. mode
// selects among: interlaced-4:3 (mode<=0 branch), progressive (1<=mode<3), and
// the widescreen/other fallback. Where the target differs from the current
// (DAT_80612244 widescreen / DAT_80612ae4 progressive) it waits for retrace and
// re-inits the render mode, then updates the field-render flags and re-lays out
// the viewports/particles/HUD.
// NON-MATCHING: the SDK VIWaitForRetrace() stub returns void here, so its
// return (the current render mode, used as gx_init_render_mode arg1 in the
// original) is passed as 0; the retrace sync call itself is preserved.
// ============================================================================
void hud_display_apply_render_mode(double p1, double p2, double p3, double p4,
                                   unsigned long long p5, unsigned long long p6,
                                   unsigned long long p7, unsigned long long p8, int mode) {
    (void)p1;
    if (mode != 1) {
        if (mode < 1) {
            if (-1 < mode) {
                if (DAT_80612244 == 0) {
                    VIWaitForRetrace();
                    gx_init_render_mode(0, p2, p3, p4, p5, p6, p7, p8, 0);
                }
                FUN_8039edf0(0);
                FUN_8039ec70('\0');
                goto done;
            }
        } else if (mode < 3) {
            if (DAT_80612ae4 == 0) {
                VIWaitForRetrace();
                gx_init_render_mode(0, p2, p3, p4, p5, p6, p7, p8, 0);
                FUN_8039ec70('\x01');
            }
            goto done;
        }
    }
    if ((DAT_80612244 != 0) || (DAT_80612ae4 != 0)) {
        VIWaitForRetrace();
        gx_init_render_mode(0, p2, p3, p4, p5, p6, p7, p8, 0);
    }
    FUN_8039edf0(1);
    FUN_8039ec70('\0');
done:
    viewport_reset_default_bounds();
    screen_scale_update();
    particle_set_pool_sizes();
    hud_init_viewports();
    return;
}

// ============================================================================
// 0x802dfef0 | hud_display_confirm_and_push
// Confirms a display setting: (optionally scales spawn timers when the frame
// countdown is at 0x3c), sets the element clip rect, clears the transition
// state, tears down the previous display screen's entity elements, refreshes
// the display, pushes the next screen, and plays the select sound.
// ============================================================================
void hud_display_confirm_and_push(int screen) {
    int soundParams[13];

    if (DAT_8061109c == 0x3c) {
        spawn_pack_scale_timers();
    }
    hud_element_set_clip_rect(FLOAT_80619f08, FLOAT_80619f5c, FLOAT_80619ff4, FLOAT_80619ff4, screen);
    DAT_80612ae0 = 0;
    hud_destroy_entity_elements(DAT_80612ac4);
    FUN_802e59e0();
    hud_screen_push();
    sound_init_play_params((int)soundParams);
    soundParams[0] = 0xd1e;
    sound_play_request(soundParams);
    return;
}

// ============================================================================
// 0x802dff7c | hud_display_widescreen_changed
// Change callback for the widescreen toggle: sets 16:9 or 4:3 display mode from
// the item's current value (item+0xc8) and refreshes the display.
// ============================================================================
void hud_display_widescreen_changed(int param_1, int item) {
    (void)param_1;
    if (*(int*)(item + 0xc8) == 1) {
        set_display_mode_16x9();
    } else {
        set_display_mode_4x3();
    }
    FUN_802e59e0();
    return;
}

// ============================================================================
// 0x802dffb4 | hud_display_calibrate_tick
// Per-frame screen-position calibration: sorts controllers, ramps the current
// Y offset (FLOAT_806110a0) toward the target using a width-derived rate,
// clamps to the min (FLOAT_80619f08), draws the calibration overlay via
// FUN_80387fb8, and binds the "done" handler state. When the offset has
// reached the min it applies the render mode and navigates back.
// ============================================================================
void hud_display_calibrate_tick(int screen, unsigned long long p2, unsigned long long p3,
                                unsigned long long p4, unsigned long long p5, unsigned long long p6,
                                unsigned long long p7, unsigned long long p8) {
    input_sort_controllers();
    double one = (double)FLOAT_80619ee8;
    double stepScale = (double)FLOAT_806108e8;
    double invWidth = (double)(float)(one / (double)(float)(
        (double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80619f00));
    double target = -(double)(float)(stepScale * invWidth - (double)FLOAT_806110a0);
    FLOAT_806110a0 = (float)target;
    if (target < (double)FLOAT_80619f08) {
        FLOAT_806110a0 = FLOAT_80619f08;
    }
    FUN_80387fb8(target, invWidth, one, stepScale, p5, p6, p7, p8,
                 (int)0x80612af0, &DAT_80611058, (int)FLOAT_806110a0, p4, p5, p6, p7, p8);

    int* child = hud_find_child_by_id(screen, 1);
    *(int*)((int)child + 0x20) = (int)&DAT_80612af0;
    if (hud_screen_all_children_closing(screen) == 0) {
        hud_screen_set_handler_param_b(screen, 1, 1);
    } else {
        hud_screen_set_handler_param_b(screen, 1, 0);
    }
    if ((double)FLOAT_80619f08 == (double)FLOAT_806110a0) {
        hud_display_apply_render_mode((double)FLOAT_80619f08, invWidth, one, stepScale,
                                      p5, p6, p7, p8, 0);
        hud_screen_navigate_to(DAT_80612ac4, (int)&PTR_FUN_804772ac, -1);
    }
    return;
}

// ============================================================================
// 0x802e00c4 | hud_display_calibrate_apply
// Applies the render mode and navigates back to the display options screen
// (used as the immediate "accept" path).
// ============================================================================
void hud_display_calibrate_apply(double p1, double p2, double p3, double p4,
                                 unsigned long long p5, unsigned long long p6,
                                 unsigned long long p7, unsigned long long p8) {
    hud_display_apply_render_mode(p1, p2, p3, p4, p5, p6, p7, p8, 0);
    hud_screen_navigate_to(DAT_80612ac4, (int)&PTR_FUN_804772ac, -1);
    return;
}

// ============================================================================
// 0x802e00fc | hud_display_calibrate_build
// Builds the screen-position calibration screen: resets the animated offset,
// two labeled entries (0x4a4 / 0x4a6) with default callbacks, centers, and sets
// the clip rect. The accept callback is hud_display_calibrate_apply.
// ============================================================================
void hud_display_calibrate_build(int screen) {
    int strId;

    FLOAT_806110a0 = FLOAT_80619fbc;
    hud_element_init_from_layout(0, 0, 0, (int)&hud_display_calibrate_apply);
    hud_set_item_padding_mode((double)FLOAT_80619f38, 1, 1, 1);
    get_string_by_id(0x4a4);
    hud_add_list_item();
    hud_add_callback_item_default(0);
    get_string_by_id(0x4a6);
    hud_add_list_item();
    hud_add_callback_item_default(0);
    hud_add_list_item();
    hud_element_apply_center_xy((int*)DAT_806119e0);
    strId = get_string_by_id(0x19c8);
    hud_screen_register_handler_global(1, strId, 1, 0);
    hud_element_set_clip_rect(FLOAT_80619f08, FLOAT_80619ff4, FLOAT_80619f4c, FLOAT_80619eec, screen);
    return;
}

// ============================================================================
// 0x802e020c | hud_display_options_build
// Builds the display options list (padding varies with layout mode
// DAT_8060f948): a widescreen toggle item (label 0x49f) whose value strings are
// 0x358/0x357 with the change callback hud_display_widescreen_changed and its
// initial value from is_widescreen(), plus a calibration entry (0x4a5). Focuses
// entry 4, seeds the screen scroll fields, and registers the A handler.
// ============================================================================
void hud_display_options_build(int screen) {
    int strId;
    int item;

    if (DAT_8060f948 == 3) {
        hud_element_init_from_layout(0, 0, 0, 0);
    } else {
        hud_element_init_from_layout(0, 0, 0, 0);
    }
    hud_set_item_padding_mode((double)FLOAT_80619fc4, 1, 1, 1);
    hud_set_context_property(0x22200);
    *(int*)(screen + 0x214) = 3;

    get_string_by_id(0x49f);
    hud_add_list_item();
    hud_item_add_spacer((double)FLOAT_80619f08, 0);
    strId = get_string_by_id(0x358);
    hud_event_add_string(strId);
    strId = get_string_by_id(0x357);
    hud_event_add_string(strId);
    item = (int)hud_get_last_created_item();
    hud_item_set_change_callback(item, (void*)&hud_display_widescreen_changed);
    is_widescreen();
    hud_get_last_created_item();
    hud_item_on_value_change();
    hud_add_callback_item_default(1);
    get_string_by_id(0x4a5);
    hud_add_list_item();
    hud_element_apply_center_xy((int*)DAT_806119e0);
    hud_set_focus_by_id(screen, 4);
    *(int*)(screen + 0x38) = 0;
    *(int*)(screen + 0x3c) = 0xf;
    strId = get_string_by_id(0x19c7);
    hud_screen_register_handler_global(0, strId, 1, 0);
    return;
}

// ============================================================================
// Batch 829 additions | Date: 2026-09-11
// Display-settings confirm/screen-build pairs (framerate 50/60Hz, progressive
// scan, aspect), the auto-confirm timeout tick, the shared close/tick
// dispatcher, and a cancel/navigate callback + prev-screen getter. Continuation
// of the display options cluster (batch_828).
//
//   0x802e0378 hud_display_confirm_timeout_tick (auto-confirm after ~1s idle)
//   0x802e0420 hud_display_cancel_to_options     (sound + navigate back)
//   0x802e0468 hud_display_framerate_build        (framerate/refresh screen)
//   0x802e05a4 hud_display_framerate_apply         (apply 50/60Hz render mode)
//   0x802e0620 hud_display_progressive_prompt_build(progressive prompt screen)
//   0x802e0778 hud_display_progressive_build        (progressive toggle screen)
//   0x802e08b4 hud_display_progressive_apply         (apply progressive/interlaced)
//   0x802e092c hud_display_aspect_build               (aspect select screen)
//   0x802e0a64 hud_screen_tick_advance_if_open        (shared close/tick dispatcher)
//   0x802e0a9c hud_display_get_prev_screen             (return DAT_80612ac0)
// ============================================================================

// --- batch 829 per-file externs (new symbols not already declared above) ---
extern int   DAT_80612aec;   // confirm-timeout accumulator
extern int   DAT_80612ac0;   // previous screen ptr (getter)
extern int   DAT_806108e0;   // per-frame timeout increment
extern float FLOAT_80619f84; // list padding-item size
extern int   PTR_FUN_804772bc; // screen descriptor -> hud_display_framerate_build
extern int   PTR_FUN_804772dc; // screen descriptor -> hud_display_progressive_build
extern unsigned int FUN_802e0e28(void); // pending page-advance count
extern void  FUN_802e0dec(void);          // advance page / commit

// forward decls for render-mode / navigate helpers defined earlier in this file
// (hud_display_apply_render_mode, hud_display_confirm_and_push) are already
// declared/defined above.

// ============================================================================
// 0x802e0378 | hud_display_confirm_timeout_tick
// Per-frame tick for a display-setting confirmation screen: disables the OK
// handler on entries 3/4, accumulates an idle timer (DAT_80612aec += step),
// resets it on any button press, and auto-confirms (hud_display_confirm_and_push)
// once ~1s (render_width * 0x1e) has elapsed with no input.
// ============================================================================
void hud_display_confirm_timeout_tick(int screen) {
    input_sort_controllers();
    if ((*(int*)(screen + 0x188) == 4) || (*(int*)(screen + 0x188) == 3)) {
        hud_screen_set_handler_param_b(screen, 0, 0);
    } else {
        hud_screen_set_handler_param_b(screen, 0, 1);
    }
    DAT_80612aec = DAT_80612aec + DAT_806108e0;
    if (input_get_buttons_pressed(0) != 0) {
        DAT_80612aec = 0;
    }
    if ((int)(DAT_806109c0 * 0x1e) <= DAT_80612aec) {
        hud_display_confirm_and_push(screen);
    }
    return;
}

// ============================================================================
// 0x802e0420 | hud_display_cancel_to_options
// Cancel callback: plays the select sound and navigates back to the display
// options screen.
// ============================================================================
void hud_display_cancel_to_options(void) {
    int soundParams[14];
    sound_init_play_params((int)soundParams);
    soundParams[0] = 0xd1e;
    sound_play_request(soundParams);
    hud_screen_navigate_to(DAT_80612ac4, (int)&PTR_FUN_804772ac, -1);
    return;
}

// ============================================================================
// 0x802e0468 | hud_display_framerate_build
// Builds the framerate/refresh confirmation screen: a padding item, a status
// line (0x1975 when the countdown is 0x32 else 0x1976), a default callback
// item, and the 0x4a5 entry. Focuses entry 4 and registers the A handler.
// ============================================================================
void hud_display_framerate_build(int screen) {
    int strId;

    hud_element_init_from_layout(0, 0, 0, 0);
    hud_set_item_padding_mode((double)FLOAT_80619f38, 1, 1, 1);
    hud_set_context_property(0x2200);
    *(int*)(screen + 0x214) = 3;
    hud_add_padding_item(FLOAT_80619f84);
    if (DAT_8061109c == 0x32) {
        get_string_by_id(0x1975);
        hud_add_list_item();
    } else {
        get_string_by_id(0x1976);
        hud_add_list_item();
    }
    hud_add_callback_item_default(1);
    hud_add_padding_item(FLOAT_80619f84);
    get_string_by_id(0x4a5);
    hud_add_list_item();
    hud_element_apply_center_xy((int*)DAT_806119e0);
    hud_set_focus_by_id(screen, 4);
    *(int*)(screen + 0x38) = 0;
    *(int*)(screen + 0x3c) = 0xf;
    strId = get_string_by_id(0x19c7);
    hud_screen_register_handler_global(0, strId, 0, 0);
    return;
}

// ============================================================================
// 0x802e05a4 | hud_display_framerate_apply
// Applies the framerate choice: sets the countdown to 0x3c (60Hz) or 0x32
// (50Hz, when focus index == 5), reconfigures the render mode accordingly,
// plays the select sound, and navigates to the next screen (PTR_FUN_804772bc).
// ============================================================================
void hud_display_framerate_apply(double p1, double p2, double p3, double p4,
                                 unsigned long long p5, unsigned long long p6,
                                 unsigned long long p7, unsigned long long p8, int screen) {
    int soundParams[14];

    DAT_8061109c = 0x3c;
    if (*(int*)(screen + 0x188) == 5) {
        DAT_8061109c = 0x32;
    }
    if (DAT_8061109c == 0x3c) {
        hud_display_apply_render_mode(p1, p2, p3, p4, p5, p6, p7, p8, 1);
    } else {
        hud_display_apply_render_mode(p1, p2, p3, p4, p5, p6, p7, p8, 0);
    }
    sound_init_play_params((int)soundParams);
    soundParams[0] = 0xd1e;
    sound_play_request(soundParams);
    hud_screen_navigate_to(DAT_80612ac4, (int)&PTR_FUN_804772bc, -1);
    return;
}

// ============================================================================
// 0x802e0620 | hud_display_progressive_prompt_build
// Builds the progressive-scan prompt screen (title line 0x1972, extra lines
// 0x1973/0x1974). Sets the countdown from boot_is_ready() and focuses entry 5
// or 6 accordingly.
// ============================================================================
void hud_display_progressive_prompt_build(int screen) {
    int strId;
    int focus;

    hud_element_init_from_layout(0, 0, 0, 0);
    hud_set_item_padding_mode((double)FLOAT_80619f38, 1, 1, 1);
    hud_set_context_property(0x200);
    *(int*)(screen + 0x214) = 3;
    hud_add_padding_item(FLOAT_80619f84);
    get_string_by_id(0x1972);
    hud_add_list_item();
    DAT_8061109c = (boot_is_ready() != 0) ? 0x3c : 0x32;
    hud_add_callback_item_default(1);
    hud_add_padding_item(FLOAT_80619f84);
    get_string_by_id(0x1973);
    hud_add_list_item();
    get_string_by_id(0x1974);
    hud_add_list_item();
    hud_element_apply_center_xy((int*)DAT_806119e0);
    focus = (boot_is_ready() != 0) ? 6 : 5;
    hud_set_focus_by_id(screen, focus);
    *(int*)(screen + 0x38) = 0;
    *(int*)(screen + 0x3c) = 0xf;
    strId = get_string_by_id(0x19c7);
    hud_screen_register_handler_global(0, strId, 0, 0);
    return;
}

// ============================================================================
// 0x802e0778 | hud_display_progressive_build
// Builds the progressive-scan toggle screen: status line 0x197b (off) / 0x197a
// (on) from DAT_80612ae4, default callback item, and the 0x4a5 entry. Focuses
// entry 4, registers the A handler.
// ============================================================================
void hud_display_progressive_build(int screen) {
    int strId;

    hud_element_init_from_layout(0, 0, 0, 0);
    hud_set_item_padding_mode((double)FLOAT_80619f38, 1, 1, 1);
    hud_set_context_property(0x200);
    *(int*)(screen + 0x214) = 3;
    hud_add_padding_item(FLOAT_80619f84);
    if (DAT_80612ae4 == 0) {
        get_string_by_id(0x197b);
        hud_add_list_item();
    } else {
        get_string_by_id(0x197a);
        hud_add_list_item();
    }
    hud_add_callback_item_default(1);
    hud_add_padding_item(FLOAT_80619f84);
    get_string_by_id(0x4a5);
    hud_add_list_item();
    hud_element_apply_center_xy((int*)DAT_806119e0);
    hud_set_focus_by_id(screen, 4);
    *(int*)(screen + 0x38) = 0;
    *(int*)(screen + 0x3c) = 0xf;
    strId = get_string_by_id(0x19c7);
    hud_screen_register_handler_global(0, strId, 0, 0);
    return;
}

// ============================================================================
// 0x802e08b4 | hud_display_progressive_apply
// Applies the progressive-scan choice: focus index 7 -> enable progressive
// (DAT_80612ae4 = 1, render mode 2); else disable (render mode 1,
// DAT_80612ae4 = 0). Plays the select sound and navigates on (PTR_FUN_804772dc).
// ============================================================================
void hud_display_progressive_apply(double p1, double p2, double p3, double p4,
                                   unsigned long long p5, unsigned long long p6,
                                   unsigned long long p7, unsigned long long p8, int screen) {
    int soundParams[14];

    if (*(int*)(screen + 0x188) == 7) {
        DAT_80612ae4 = 1;
        hud_display_apply_render_mode(p1, p2, p3, p4, p5, p6, p7, p8, 2);
    } else {
        hud_display_apply_render_mode(p1, p2, p3, p4, p5, p6, p7, p8, 1);
        DAT_80612ae4 = 0;
    }
    sound_init_play_params((int)soundParams);
    soundParams[0] = 0xd1e;
    sound_play_request(soundParams);
    hud_screen_navigate_to(DAT_80612ac4, (int)&PTR_FUN_804772dc, -1);
    return;
}

// ============================================================================
// 0x802e092c | hud_display_aspect_build
// Builds the aspect/display confirm screen (status line 0x1977, extra lines
// 0x1978/0x1979). Sets the countdown to 0x3c and focuses entry 7.
// ============================================================================
void hud_display_aspect_build(int screen) {
    int strId;

    hud_element_init_from_layout(0, 0, 0, 0);
    hud_set_item_padding_mode((double)FLOAT_80619f38, 1, 1, 1);
    hud_set_context_property(0x200);
    *(int*)(screen + 0x214) = 3;
    hud_add_padding_item(FLOAT_80619f84);
    get_string_by_id(0x1977);
    hud_add_list_item();
    hud_add_callback_item_default(1);
    hud_add_padding_item(FLOAT_80619f84);
    DAT_8061109c = 0x3c;
    get_string_by_id(0x1978);
    hud_add_list_item();
    get_string_by_id(0x1979);
    hud_add_list_item();
    hud_element_apply_center_xy((int*)DAT_806119e0);
    hud_set_focus_by_id(screen, 7);
    *(int*)(screen + 0x38) = 0;
    *(int*)(screen + 0x3c) = 0xf;
    strId = get_string_by_id(0x19c7);
    hud_screen_register_handler_global(0, strId, 0, 0);
    return;
}

// ============================================================================
// 0x802e0a64 | hud_screen_tick_advance_if_open
// Shared close/tick dispatcher used by this screen family: while the element
// is not closing and there is a pending page advance (FUN_802e0e28 > 0), commit
// the advance (FUN_802e0dec). (hud_screen_forward_to_handler, batch_826, tail-
// calls this.)
// ============================================================================
void hud_screen_tick_advance_if_open(int element) {
    if ((hud_element_is_closing(element) == 0) && (0 < (int)FUN_802e0e28())) {
        FUN_802e0dec();
    }
    return;
}

// ============================================================================
// 0x802e0a9c | hud_display_get_prev_screen
// Returns the stored previous screen pointer (DAT_80612ac0).
// ============================================================================
int hud_display_get_prev_screen(void) {
    return DAT_80612ac0;
}
