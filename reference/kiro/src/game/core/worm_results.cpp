// worm_results.cpp
// End-of-match / outro results sequence for the worm (mission) game modes.
//
// A small state block drives a scripted outro when a match ends:
//   DAT_8061294c = primary outro state   (worm_results_get_state)
//   DAT_80612948 = secondary sub-state    (worm_results_get_substate)
//   DAT_80612944 / DAT_80612940 = timeline flag / spawned-effect handle
// worm_results_enter freezes the match (pause, models out, splitscreen camera),
// worm_results_set_state selects the outro variant, worm_results_outro_tick runs
// the per-frame timeline (animation sampling, sound, column effect), and
// worm_results_exit tears everything back down.
//
// batch_705: 10 functions 0x802386ec .. 0x80238f9b. subsystem:game.
// NON-MATCHING decompilation.

#include "common.h"

// ---- outro state block + globals (file-local externs) ----
extern int   DAT_8061294c;   // primary outro state (-1 = idle)
extern int   DAT_80612948;   // secondary sub-state (-1 = idle)
extern int   DAT_80612944;   // timeline flag
extern int   DAT_80612940;   // spawned effect / emitter handle
extern int   DAT_805015c0;   // mode flags (0x200 = results active)
extern int   DAT_80612610;   // player count
extern int   DAT_80611d78;   // per-player slot table (0x141c stride)
extern int   DAT_80611d54;   // primary player slot pointer
extern int   DAT_80611d74;   // active mission record pointer
extern int   DAT_80611b68;   // scene root pointer
extern int   DAT_80611824;   // animation bank base
extern int   DAT_803e01d8;   // default spawn position .x
extern int   DAT_803e01dc;   // default spawn position .y
extern int   DAT_803e01e0;   // default spawn position .z
extern short DAT_804d1cd0;   // model-index -> anim-id table
extern float FLOAT_8060faf8; // frames-per-second time scale
extern float FLOAT_80618830; // effect spawn param
extern float FLOAT_80618834; // effect spawn param
extern float FLOAT_80618838; // effect fade param
extern float FLOAT_80618840; // timeline threshold
extern float FLOAT_80618844; // timeline param
extern float FLOAT_80618848; // timeline threshold
extern float FLOAT_8061884c; // timeline window lo
extern float FLOAT_80618850; // timeline window hi
extern float FLOAT_80618854; // timeline threshold
extern float FLOAT_80618868; // reset object field value

// ---- raw helpers (file-local externs) ----
extern int  FUN_8023c32c(unsigned int tag);      // resolve object by tag
extern void FUN_8025755c(int slot);              // per-player slot cleanup
extern int  FUN_80355348(double a, double b, int c, void *pos, int type); // spawn effect
extern void FUN_80355574(double fade, int handle);
extern void FUN_80357874(int handle);
extern void FUN_80357898(int root);
extern void FUN_803577c0(int handle, void *pos);
extern void FUN_8035787c(void);
extern int  FUN_8035560c(double param, int handle);
extern void FUN_8035b1ec(int a, int entity, int desc);
extern int  FUN_8036fe90(int slot);
extern void FUN_8036fea4(int a);
extern void FUN_8024281c(void);
extern void FUN_80238fc8(int state);             // per-state teardown (later batch)

// ---- named cross-module helpers (file-local externs; TU includes only common.h) ----
extern void match_set_paused(void);
extern void match_clear_paused(void);
extern void input_set_rumble_intensity(int player, int intensity);
extern void object_model_swap_out_and_clear(int model);
extern int  object_model_swap_in(int model);
extern int  is_widescreen(void);
extern void hud_banner_show_persistent(int duration, int widescreen);
extern void hud_banner_hide(void);
extern void camera_splitscreen_setup(void);
extern void camera_splitscreen_restore(void);
extern void aiactor_deactivate_all_idle(void);
extern void aiactor_activate_all_valid(void);
extern void object_slot_deselect(void);
extern void ghz_mcht_exit_menu(void);
extern void worm_prop_sound_fade_b(void);
extern int  worm_event_fade_sounds(void);
extern void column_shut_timeline_update(double t);
extern void column_shut_final_trigger(int a, ...);
extern void column_fire_payload(int a, ...);
extern void anim_sample_keyframe(int bank, int b, unsigned int frame, void *out);
extern void memset_wrapper(int dst, int val, int len);
extern void player_slot_set_entity(int slot, int entity);
extern void sound_set_mute_flag(int flag);

// Forward declarations for intra-file helpers called before their definition.
void worm_results_reset_state(void);
void worm_results_set_object_field(double value, unsigned int tag);

// 0x8023870c | worm_results_reset_state
// Reset the outro state block to idle (states = -1, flags = 0).
void worm_results_reset_state(void)
{
    DAT_8061294c = -1;
    DAT_80612948 = -1;
    DAT_80612944 = 0;
    DAT_80612940 = 0;
}

// 0x802386ec | worm_results_reset_wrapper
// Thin wrapper that resets the outro state block.
void worm_results_reset_wrapper(void)
{
    worm_results_reset_state();
}

// 0x80238728 | worm_results_enter
// Begin the end-of-match results sequence: raise the results mode flag (0x200),
// pause the match, and for each player disable rumble, swap the character model
// out, flag the slot (+0x140c = 1) and run per-slot cleanup. Finally show the
// persistent results banner and set up the splitscreen camera layout.
void worm_results_enter(void)
{
    DAT_805015c0 = DAT_805015c0 | 0x200;
    match_set_paused();
    int off = 0;
    for (int p = 0; p < DAT_80612610; p = p + 1) {
        input_set_rumble_intensity(p, 0);
        object_model_swap_out_and_clear(*(int *)(*(int *)(DAT_80611d78 + off + 0x14) + 0x7c));
        *(int *)(DAT_80611d78 + off + 0x140c) = 1;
        FUN_8025755c(DAT_80611d78 + off);
        off = off + 0x141c;
    }
    int wide = is_widescreen();
    hud_banner_show_persistent(0x7fffffff, wide & 0xff);
    camera_splitscreen_setup();
}

// 0x802387e4 | worm_results_set_state
// Select the outro variant (state,substate) and prime it: deactivate idle AI,
// disable rumble, then dispatch by state - 5 exits the cheat/menu screen, 3
// deselects placed objects per player, 2 spawns the outro effect emitter at the
// default position and starts it, 0x12 clears the effect handle.
void worm_results_set_state(int state, int substate)
{
    DAT_80612948 = substate;
    DAT_8061294c = state;
    aiactor_deactivate_all_idle();
    for (int p = 0; p < DAT_80612610; p = p + 1) {
        input_set_rumble_intensity(p, 0);
    }

    if (DAT_8061294c == 5) {
        ghz_mcht_exit_menu();
    } else if (DAT_8061294c < 5) {
        if (DAT_8061294c == 3) {
            for (int p = 0; p < DAT_80612610; p = p + 1) {
                object_slot_deselect();
            }
        } else if ((DAT_8061294c < 3) && (1 < DAT_8061294c)) {
            int root = DAT_80611b68 + 0xb4;
            int pos[3];
            pos[0] = DAT_803e01d8;
            pos[1] = DAT_803e01dc;
            pos[2] = DAT_803e01e0;
            DAT_80612940 = FUN_80355348((double)FLOAT_80618830, (double)FLOAT_80618834, 1, pos, 0xd5);
            FUN_80355574((double)FLOAT_80618838, DAT_80612940);
            FUN_80357874(DAT_80612940);
            FUN_80357898(root);
        }
    } else if (DAT_8061294c == 0x12) {
        DAT_80612940 = 0;
    }
}

// 0x80238914 | worm_results_outro_tick
// Per-frame update of the active outro variant. 'timeline' carries the elapsed
// time at +8 and the model index at +0x10c; the switch on the outro state samples
// keyframe transforms (anim_sample_keyframe) to drive the spawned emitter, fires
// timed sound / column effects, and handles the multi-window objective outro
// (state 0x16) by triggering payloads at discrete frame windows.
void worm_results_outro_tick(int timeline)
{
    float elapsed = *(float *)(timeline + 8);

    switch (DAT_8061294c) {
    case 1:
        if (FLOAT_80618848 < elapsed) {
            worm_prop_sound_fade_b();
        }
        break;
    case 2:
        if (DAT_80612940 != 0) {
            unsigned int frame = (unsigned int)(elapsed * FLOAT_8060faf8);
            float xform[6];
            anim_sample_keyframe(
                *(int *)(DAT_80611824 + (short)(&DAT_804d1cd0)[*(int *)(timeline + 0x10c)] * 4),
                0, frame, &xform[3]);
            xform[0] = xform[3];
            xform[1] = xform[4];
            xform[2] = xform[5];
            FUN_803577c0(DAT_80612940, &xform[0]);
            if (FLOAT_80618840 < elapsed) {
                FUN_8035787c();
                FUN_8035560c((double)FLOAT_80618844, DAT_80612940);
            }
        }
        break;
    case 4:
        column_shut_timeline_update((double)elapsed);
        break;
    case 0x12: {
        unsigned int frame = (unsigned int)(elapsed * FLOAT_8060faf8);
        float xform[6];
        anim_sample_keyframe(
            *(int *)(DAT_80611824 + (short)(&DAT_804d1cd0)[*(int *)(timeline + 0x190)] * 4),
            0, frame, &xform[3]);
        xform[0] = xform[3];
        xform[1] = xform[4];
        xform[2] = xform[5];
        if ((DAT_80612940 == 0) && (FLOAT_8061884c < elapsed) && (elapsed < FLOAT_80618850)) {
            DAT_80612940 = FUN_80355348((double)FLOAT_80618830, (double)FLOAT_80618834, 1, &xform[0], 0xd5);
            FUN_80355574((double)FLOAT_80618838, DAT_80612940);
        }
        if (DAT_80612940 != 0) {
            FUN_803577c0(DAT_80612940, &xform[0]);
            if (FLOAT_80618854 < elapsed) {
                FUN_8035560c((double)FLOAT_80618838, DAT_80612940);
                DAT_80612940 = 0;
            }
        }
        break;
    }
    case 0x16: {
        int frame = (int)(elapsed * FLOAT_8060faf8);
        int desc[5];
        if ((0x1039 <= frame) && (frame < 0x1058)) {
            memset_wrapper((int)desc, 0, 0x14);
            desc[0] = 1;
            FUN_8035b1ec(1, timeline, (int)desc);
        } else if ((0x1082 <= frame) && (frame < 0x10a1)) {
            memset_wrapper((int)desc, 0, 0x14);
            desc[0] = 1;
            FUN_8035b1ec(1, timeline, (int)desc);
        } else if ((0x135a <= frame) && (frame < 0x1379)) {
            memset_wrapper((int)desc, 0, 0x14);
            desc[0] = 2;
            FUN_8035b1ec(1, timeline, (int)desc);
        }
        break;
    }
    default:
        break;
    }
}

// 0x80238d14 | worm_results_exit
// Tear down the results sequence (mirror of worm_results_enter): clear the 0x200
// mode flag, hide the banner, run the per-state teardown, unpause, re-activate
// AI, swap each player's model back in and repair its state fields, dispatch the
// per-state effect cleanup, restore the splitscreen camera, and reset the outro
// state block to idle.
void worm_results_exit(void)
{
    if ((DAT_805015c0 & 0x200) != 0) {
        DAT_805015c0 = DAT_805015c0 & 0xfffffdff;
        if ((*(unsigned int *)(DAT_80611d54 + 0x21c) & 0x2000000) == 0) {
            hud_banner_hide();
        }
        FUN_80238fc8(DAT_8061294c);
        *(unsigned int *)(DAT_80611d54 + 0x21c) = *(unsigned int *)(DAT_80611d54 + 0x21c) & 0xfdffffff;
    }
    match_clear_paused();
    player_slot_set_entity(DAT_80611d54, 0);
    sound_set_mute_flag(0);
    aiactor_activate_all_valid();
    FUN_8024281c();
    worm_event_fade_sounds();

    int off = 0;
    for (int p = 0; p < DAT_80612610; p = p + 1) {
        int object = *(int *)(*(int *)(DAT_80611d78 + off + 0x14) + 0x7c);
        int a = FUN_8036fe90(DAT_80611d78 + off);
        FUN_8036fea4(a);
        object_model_swap_in(object);
        if (*(int *)(object + 0x94) == 0) {
            *(int *)(object + 0x94) = *(int *)(object + 0x9c);
        }
        if (*(int *)(object + 0x94) == 0) {
            *(int *)(object + 0x94) = 1;
        }
        off = off + 0x141c;
    }

    switch (DAT_8061294c) {
    case 1:
        worm_prop_sound_fade_b();
        break;
    case 2:
        FUN_8035787c();
        column_fire_payload(FUN_8035560c((double)FLOAT_80618830, DAT_80612940));
        break;
    case 3:
        if (DAT_80611d74 != 0) {
            *(int *)(DAT_80611d74 + 0xe8c) = 0;
        }
        break;
    case 4:
        column_shut_final_trigger(0);
        break;
    case 0x12:
        if (DAT_80612940 != 0) {
            FUN_8035560c((double)FLOAT_80618830, DAT_80612940);
        }
        break;
    default:
        break;
    }

    camera_splitscreen_restore();
    DAT_8061294c = -1;
    DAT_80612948 = -1;
}

// 0x80238eac | worm_results_reset_objects
// Fade the outro sounds and, for the state-2 (effect) variant, reset the render
// field (+0xd4) of the six named outro objects (tags 'OTH0'..'OTH4','ATCR') back
// to the default value FLOAT_80618868.
void worm_results_reset_objects(void)
{
    worm_event_fade_sounds();
    if (DAT_8061294c == 2) {
        worm_results_set_object_field((double)FLOAT_80618868, 0x4f544830);  // 'OTH0'
        worm_results_set_object_field((double)FLOAT_80618868, 0x4f544831);  // 'OTH1'
        worm_results_set_object_field((double)FLOAT_80618868, 0x4f544832);  // 'OTH2'
        worm_results_set_object_field((double)FLOAT_80618868, 0x4f544833);  // 'OTH3'
        worm_results_set_object_field((double)FLOAT_80618868, 0x4f544834);  // 'OTH4'
        worm_results_set_object_field((double)FLOAT_80618868, 0x41544352);  // 'ATCR'
    }
}

// 0x80238f50 | worm_results_set_object_field
// Resolve the object with tag 'tag'; if present, write 'value' to its render
// field at obj->+4 -> +0xd4.
void worm_results_set_object_field(double value, unsigned int tag)
{
    int obj = FUN_8023c32c(tag);
    if (obj != 0) {
        *(float *)(*(int *)(obj + 4) + 0xd4) = (float)value;
    }
}

// 0x80238f8c | worm_results_get_state
// Return the current primary outro state.
int worm_results_get_state(void)
{
    return DAT_8061294c;
}

// 0x80238f94 | worm_results_get_substate
// Return the current secondary outro sub-state.
int worm_results_get_substate(void)
{
    return DAT_80612948;
}
