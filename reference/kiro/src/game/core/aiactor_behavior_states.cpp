// =============================================================================
// aiactor_behavior_states.cpp - AI actor per-behaviour-state tick handlers
//
// Continuation of aiactor_behavior_combat.cpp / aiactor_behavior_mode_tick.
// These are the individual behaviour-state ticks the AI dispatcher calls when an
// actor is in a particular high-level state (prop-operating, holding position,
// fleeing, gang-up hunting, swimming), plus a couple of small scan helpers and
// the clip-selection table initialiser.
//
//   - aiactor_build_clipset_table (0x802bc1a4): populate the global AI clip-set
//     descriptor table (DAT_805cbd48..) - six 0x40-stride blocks, each a clip
//     count, an rng-seeded start cursor, and a list of clip ids.
//   - aiactor_behavior_prop_tick (0x802bc414): behaviour when the actor is
//     operating a prop/turret (rec[0x305]); moves via nav, aims the prop with
//     prop_yaw_to_point, and fires with prop_fire_tick.
//   - aiactor_behavior_hold_position_tick (0x802bc654): aim at a fixed angle (or
//     the leader), hold position, and play the melee/fidget clip.
//   - aiactor_count_distinct_weapons (0x802bc780): count the actor's distinct
//     owned weapon slots (dedup against earlier list entries).
//   - aiactor_behavior_flee_tick (0x802bc840): flee/regroup mode FSM - pick a
//     flee/spawn zone, follow the path, and retarget.
//   - aiactor_find_nearest_alarmed_ally (0x802bcf70): nearest bot with a high
//     alarm value (rec[0x2a0]) that is not disabled.
//   - aiactor_scan_nearest_squadb_hostile (0x802bd00c): nearest squad-B actor of
//     a different team (distance weighted if a squadmate already targets it).
//   - aiactor_squadmate_targeting (0x802bd118): does any squadmate (same squad
//     id at rec+0x84) already have entity `candidate` as its follow target.
//   - aiactor_behavior_gangup_tick (0x802bd198): squad-B group-hunt mode FSM -
//     gather the squad onto a shared target and close in.
//   - aiactor_behavior_swim_tick (0x802bd884): water behaviour - warp to / move
//     toward the sound-pad water surface height.
//
// AI actor control record lives at (actor+0x7c) ("rec"), viewed both int-indexed
// (rec[N]) and byte-offset (rec+0xNNN). Key fields used here:
//   rec+0x20  behaviour mode (0..4)     rec+0x9d0 queued mode / 0x9d4 prev / 0x9d8 timer
//   rec+0x04  current target            rec+0x30..0x38 aim-at world point
//   rec+0x678 resolved zone id          rec+0x684 fallback zone id
//   rec+0xaac/0xaa8 pending action + latch   rec+0xabc/0xab8 secondary action + latch
//   rec+0xaf0 follow target             rec+0x84 squad id
//   rec+0xb30 gather timer              rec+0xb34 retarget cooldown
//   rec+0x305 (int idx 0x305) operated prop entity
//
// Sound-pad zone table: DAT_80611b8c (stride 0x20). Active-actor pointer array:
// DAT_80588610 (stride 4). Squad-B actor array: DAT_80588888 (stride 8).
//
// Address range: 0x802bc1a4 - 0x802bda2b
// Subsystem: game (AI actor behaviour states)
//
// NON-MATCHING: PPC frame-enter/leave register pair modelled by
// aiactor_current_frame_actor(); the mode-switch bookkeeping is factored into
// ai_set_mode(); CONCAT44(0x43300000,x) int->double bias idioms reduced to plain
// float conversions; incoming FP-register spill args on swim_tick expressed as an
// explicit (unused) seed and the actor taken as the last integer parameter.
// =============================================================================

#include "common.h"

extern "C" {

// --- level / global state (raw symbols) --------------------------------------
extern char DAT_805015bd;          // game/level mode selector
extern int  DAT_80588610;          // active-actor pointer array (stride 4)
extern int  DAT_80588888;          // squad-B actor array (stride 8; [i*2] = ptr)
extern int  DAT_80611928;          // designated leader/reference actor
extern int  DAT_80611b8c;          // sound-pad zone table base (stride 0x20)
extern int  DAT_80611c14;          // sound-pad zone count
extern int  DAT_80611d78;          // world/level context base
extern int  DAT_80611d90;          // actor record array base (stride 0xea0)
extern int  DAT_80612098;          // active actor count
extern int  DAT_806120f8;          // round-robin scan cursor
extern int  DAT_80612610;          // first bot slot index / bot count
extern int  DAT_806109c0;          // frame-delta raw bits
extern unsigned char DAT_8048ca94; // per-character nav-table X step (stride 0x30)
extern unsigned char DAT_8048ca9c; // per-character nav-table Z step (stride 0x30)
extern float DAT_803e07d8;         // swim probe scratch vec.x
extern float DAT_803e07dc;         // swim probe scratch vec.y
extern float DAT_803e07e0;         // swim probe scratch vec.z

// clip-set descriptor table (six 0x40-stride blocks)
extern int DAT_805cbd48, DAT_805cbd4c, DAT_805cbd50, DAT_805cbd54, DAT_805cbd58;
extern int DAT_805cbd5c, DAT_805cbd60, DAT_805cbd64, DAT_805cbd68, DAT_805cbd6c;
extern int DAT_805cbd70, DAT_805cbd74, DAT_805cbd80, DAT_805cbd84, DAT_805cbd88;
extern int DAT_805cbd8c, DAT_805cbd90, DAT_805cbd94, DAT_805cbd98, DAT_805cbd9c;
extern int DAT_805cbda0, DAT_805cbda4, DAT_805cbda8;
extern int DAT_805cbdc0, DAT_805cbdc4, DAT_805cbdc8, DAT_805cbdcc, DAT_805cbdd0;
extern int DAT_805cbdd4, DAT_805cbdd8, DAT_805cbddc, DAT_805cbde0, DAT_805cbde4, DAT_805cbde8;
extern int DAT_805cbe00, DAT_805cbe04, DAT_805cbe08, DAT_805cbe0c, DAT_805cbe10;
extern int DAT_805cbe14, DAT_805cbe18, DAT_805cbe1c, DAT_805cbe20;
extern int DAT_805cbe40, DAT_805cbe44, DAT_805cbe48, DAT_805cbe4c, DAT_805cbe50;
extern int DAT_805cbe54, DAT_805cbe58, DAT_805cbe5c, DAT_805cbe60;
extern int DAT_805cbe80, DAT_805cbe84;

// float / double literal pool
extern float FLOAT_806108ec;       // aim-timer decay scale
extern float FLOAT_80619c30;       // scan initial best distance (large)
extern float FLOAT_80619c38;       // clip playback rate
extern float FLOAT_80619c48;       // gather-timer threshold / 0.5
extern float FLOAT_80619c4c;       // 0.0f
extern float FLOAT_80619c50;       // 1.0f
extern float FLOAT_80619c58;       // follow playback rate
extern float FLOAT_80619c5c;       // surface-snap tolerance
extern float FLOAT_80619c64;       // range gate multiplier
extern float FLOAT_80619c78;       // alarm threshold (min)
extern float FLOAT_80619c9c;       // gather-timer step
extern float FLOAT_80619cc0;       // help range gate
extern float FLOAT_80619cd4;       // squadmate-shared distance weight
extern float FLOAT_80619cd8;       // gather cohesion distance squared
extern float FLOAT_80619cdc;       // swim clip playback rate
extern float FLOAT_80619ce0;       // water surface clamp height
extern double DOUBLE_80619c68;     // frame-delta bias base

// --- named helpers (declared to match the decompiler's call sites) -----------
extern int  aiactor_retarget_visible_teammate(void);
extern void aiactor_retarget_scan(void);
extern void aiactor_follow_zone_path_void(void);   // header spelling (unused ret sites use below)
extern int  aiactor_follow_zone_path(void);        // C-linkage: returns advance-result
extern void aiactor_set_stance_flags(int actor, int flags);
extern void aiactor_turn_toward_leader(void);
extern bool aiactor_turn_toward_angle(double targetYaw, int actor, int instant);
extern void aiactor_play_turn_animation(int actor);
extern void aiactor_facing_update(void);
extern void aiactor_movement_tick(unsigned long long a, double b, double c, double d,
                                  double e, double f, double g, unsigned long long h);
extern unsigned int aiactor_squad_b_count(void);
extern int  tracker_select_best_target_scored(void);
extern unsigned int tracker_class_owner_active_flag(int actor);
extern void tracker_play_melee_fidget_clip(int actor);
extern unsigned int nav_movement_dispatch(double xoff, double zoff, int navState, int posPtr,
                                          float *outPos, float *outVel, int *outCollision,
                                          int enableScene, int flags);
extern void prop_compute_eye_point(void);
extern double prop_yaw_to_point(int obj, float *target);
extern void prop_fire_tick(int self_unused, int owner, int side, int a4, int mode, int *rec);
extern int  spawn_find_random_by_group(int groupId);
extern int  spawn_find_random_in_range(void);
extern int  soundpad_find_zone_by_id(int id);
extern int  soundpad_find_trigger_by_id(int trigger_id);
extern int  scene_ray_traverse_portals(unsigned long long ctx, double h, int zonePtr,
                                       int zoneRow, float *outPos, int flags);
extern int  anim_is_clip_active(int actor, int clip);
extern int  anim_active_slot_userflags(int slot);
extern void anim_set_playback_rate(double r, int *slot, int a);
extern unsigned long long anim_play_scaled(double a, double b);
extern unsigned int rng_counter_tick(void);
extern bool aiactor_aim_alignment_test(double threshold, int actor, float *targetPos);
extern void *memset_wrapper(int dst, int val, int len);

// --- unnamed callees (external, not in this batch) ---------------------------
extern int  aiactor_all_zone_targets_inactive(void); // (was FUN_802bdf68)

// --- PPC frame-context intrinsic model ---------------------------------------
extern int  aiactor_current_frame_actor(void);

// forward decls (mutual references)
int  aiactor_scan_nearest_squadb_hostile_ret(void);
void aiactor_scan_nearest_squadb_hostile(void);
int  aiactor_find_nearest_alarmed_ally(int actor);
int  aiactor_squadmate_targeting(int actor, int candidate);

// Behaviour mode-switch bookkeeping (byte-offset record view).
static void ai_set_mode(int rec, int newMode)
{
    if (*(int *)(rec + 0x20) == newMode)
        return;
    *(int *)(rec + 0x9d4) = *(int *)(rec + 0x20);
    *(float *)(rec + 0x9d8) = FLOAT_80619c4c;
    *(int *)(rec + 0x20) = newMode;
    *(int *)(rec + 0x9d0) = *(int *)(rec + 0x20);
}

// Common queued-mode consume + aim-timer decay preamble shared by the tick FSMs.
static void ai_tick_preamble(int rec)
{
    if (-1 < *(int *)(rec + 0x9d0)) {
        if (*(int *)(rec + 0x9d0) != *(int *)(rec + 0x20)) {
            *(float *)(rec + 0x9d8) = FLOAT_80619c4c;
            *(int *)(rec + 0x9d4) = *(int *)(rec + 0x20);
            *(int *)(rec + 0x20) = *(int *)(rec + 0x9d0);
        }
        *(int *)(rec + 0x9d0) = -1;
    }
    if ((*(unsigned int *)(rec + 0x2c) & 0x80) == 0) {
        *(float *)(rec + 0xa90) = FLOAT_80619c4c;
    } else {
        *(float *)(rec + 0xa90) =
            FLOAT_806108ec *
            (FLOAT_80619c50 /
             (float)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80619c68)) +
            *(float *)(rec + 0xa90);
    }
}

// Latch-checked secondary-action queue (returns 1 = caller should bail).
static int ai_queue_secondary(int rec, int code)
{
    if (*(int *)(rec + 0xaac) == 1 && *(int *)(rec + 0xaa8) == 0) {
        unsigned int gate;
        if (DAT_805015bd == '\n')
            gate = *(unsigned int *)(rec + 0x14);
        else
            gate = *(unsigned int *)(rec + 0x1c);
        if ((gate & 1) == 0)
            return 1;
    }
    *(int *)(rec + 0xaac) = code;
    *(int *)(rec + 0xaa8) = 0;
    return 0;
}

// =============================================================================
// 0x802bc1a4  aiactor_build_clipset_table
// Populate the global AI clip-set descriptor table (six 0x40-stride blocks).
// Each block: [0]=count, [+0x38]=leader/reference actor, [+0x3c]=rng start cursor
// (rng % count), then a list of clip ids.
// =============================================================================
void aiactor_build_clipset_table(void)
{
    aiactor_current_frame_actor();
    memset_wrapper((int)0x8005cd48, 0, 0x180); // clear the 0x180-byte table (base 0x805cbd48-... region)

    // block 0
    DAT_805cbd48 = 0xb; DAT_805cbd4c = 5; DAT_805cbd50 = 1; DAT_805cbd54 = 0x30;
    DAT_805cbd58 = 0x31; DAT_805cbd5c = 10; DAT_805cbd60 = 0x22; DAT_805cbd64 = 6;
    DAT_805cbd68 = 0x38; DAT_805cbd6c = 0x34; DAT_805cbd70 = 0x3a; DAT_805cbd74 = 0x14;
    DAT_805cbd80 = DAT_80611928;
    unsigned int r = rng_counter_tick();
    DAT_805cbd88 = 8; DAT_805cbd8c = 0xe; DAT_805cbd90 = 0xf;
    DAT_805cbd84 = (int)(r - (r / (unsigned int)DAT_805cbd48) * (unsigned int)DAT_805cbd48);
    DAT_805cbd94 = 0x10; DAT_805cbd98 = 0x12; DAT_805cbd9c = 0; DAT_805cbda0 = 0x23;
    DAT_805cbda4 = 0x21; DAT_805cbda8 = 0x39;

    // block 1
    DAT_805cbdc0 = DAT_80611928;
    r = rng_counter_tick();
    DAT_805cbdc8 = 8; DAT_805cbdcc = 2;
    DAT_805cbdc4 = (int)(r - (r / (unsigned int)DAT_805cbd88) * (unsigned int)DAT_805cbd88);
    DAT_805cbdd0 = 3; DAT_805cbdd4 = 4; DAT_805cbdd8 = 0xb; DAT_805cbddc = 0x17;
    DAT_805cbde0 = 0x26; DAT_805cbde4 = 7; DAT_805cbde8 = 0x32;

    // block 2
    DAT_805cbe00 = DAT_80611928;
    r = rng_counter_tick();
    DAT_805cbe08 = 6; DAT_805cbe0c = 0x28; DAT_805cbe10 = 0x29;
    DAT_805cbe04 = (int)(r - (r / (unsigned int)DAT_805cbdc8) * (unsigned int)DAT_805cbdc8);
    DAT_805cbe14 = 0x2a; DAT_805cbe18 = 0x2b; DAT_805cbe1c = 0x2c; DAT_805cbe20 = 0x2d;

    // block 3
    DAT_805cbe40 = DAT_80611928;
    r = rng_counter_tick();
    DAT_805cbe48 = 6; DAT_805cbe4c = 0x11; DAT_805cbe50 = 0x24;
    DAT_805cbe44 = (int)(r - (r / (unsigned int)DAT_805cbe08) * (unsigned int)DAT_805cbe08);
    DAT_805cbe54 = 0x25; DAT_805cbe58 = 0x35; DAT_805cbe5c = 0x36; DAT_805cbe60 = 0x37;

    // block 4
    DAT_805cbe80 = DAT_80611928;
    r = rng_counter_tick();
    DAT_805cbe84 = (int)(r - (r / (unsigned int)DAT_805cbe48) * (unsigned int)DAT_805cbe48);
}

// =============================================================================
// 0x802bc414  aiactor_behavior_prop_tick
// Behaviour when the actor is operating a prop / mounted weapon (rec[0x305]).
// Moves via nav_movement_dispatch, aims the prop with prop_yaw_to_point, fires
// with prop_fire_tick when a target is visible.
// =============================================================================
void aiactor_behavior_prop_tick(void)
{
    int actor = aiactor_current_frame_actor();
    int *rec = *(int **)(actor + 0x7c);
    int hasTeammate = aiactor_retarget_visible_teammate();
    rec[10] = rec[10] | 0x800;

    if (rec[0x305] == 0) {
        if (FLOAT_80619c50 < (float)rec[0x2cc]) {
            ai_queue_secondary((int)rec, 0x10);
        }
        return;
    }

    int propRec = *(int *)(rec[0x305] + 0x7c);
    if (rec[8] == 0) {
        int best = tracker_select_best_target_scored();
        rec[1] = best;
        if (rec[8] != 1)
            rec[0x274] = 1;
    }
    if ((DAT_806120f8 == rec[2])) {
        int best = tracker_select_best_target_scored();
        if (best != 0)
            rec[1] = best;
    }
    if (hasTeammate != 0)
        aiactor_turn_toward_leader();
    prop_compute_eye_point();

    float outPos[3];  // local_48/44/40
    int collide[10];  // aiStack_3c[10]
    outPos[0] = 0; outPos[1] = 0; outPos[2] = 0;
    unsigned int navRes = nav_movement_dispatch(
        (double)*(float *)(&DAT_8048ca94 + *(short *)(rec + 0x12) * 0x30),
        (double)*(float *)(&DAT_8048ca9c + *(short *)(rec + 0x12) * 0x30),
        rec[2], actor + 100, &outPos[0], (float *)0, collide, 1, 0);
    rec[0x300] = collide[9];  // local_14 (last collide slot)
    if ((navRes & 2) != 0) {
        double yaw = prop_yaw_to_point(rec[0x305], &outPos[0]);
        *(float *)(actor + 0x70) = (float)yaw;
    }
    *(float *)(actor + 100) = outPos[0];
    *(int *)(actor + 0x68) = (int)outPos[1];
    *(int *)(actor + 0x6c) = (int)outPos[2];

    if ((hasTeammate != 0) && (rec[1] != 0)) {
        *(int *)(propRec + 0xb0) = *(int *)(actor + 0x34);
        prop_fire_tick(rec[0x305], *(int *)(propRec + 0x6c), 0, 0, 3, rec);
    }
    if (anim_is_clip_active(actor, 0x25) == 0)
        anim_play_scaled((double)FLOAT_80619c38, (double)FLOAT_80619c50);
}

// =============================================================================
// 0x802bc654  aiactor_behavior_hold_position_tick
// Aim at a fixed angle (from the actor's zone record) or the leader, hold the
// current position, and play the melee/fidget clip.
// =============================================================================
void aiactor_behavior_hold_position_tick(void)
{
    int actor = aiactor_current_frame_actor();
    int rec = *(int *)(actor + 0x7c);
    int hasTeammate = aiactor_retarget_visible_teammate();
    int best = tracker_select_best_target_scored();
    if (best != 0)
        *(int *)(rec + 4) = best;

    if (hasTeammate == 0) {
        aiactor_turn_toward_angle((double)*(float *)(*(int *)(rec + 0x67c) + 0x18), actor, 0);
        *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) | 0x40000;
    } else {
        aiactor_turn_toward_leader();
        *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) | 2;
        *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) & 0xfffbffff;
    }
    *(int *)(rec + 0x30) = *(int *)(actor + 100);
    *(int *)(rec + 0x34) = *(int *)(actor + 0x68);
    *(int *)(rec + 0x38) = *(int *)(actor + 0x6c);

    if ((DAT_805015bd == '\x02') &&
        ((tracker_class_owner_active_flag(actor) != 0) || (*(int *)(rec + 0x80) == 0))) {
        *(int *)(rec + 0xaf0) = 0;
    } else {
        tracker_play_melee_fidget_clip(actor);
        if ((hasTeammate != 0) && (((*(int **)(actor + 4))[0x32] & 1) != 0))
            anim_set_playback_rate((double)FLOAT_80619c58, *(int **)(actor + 4), 2);
    }
}

// =============================================================================
// 0x802bc780  aiactor_count_distinct_weapons
// Count the actor's distinct owned weapon slots (deduplicating against earlier
// entries in the match weapon list).
// =============================================================================
int aiactor_count_distinct_weapons(int actor)
{
    extern int match_build_weapon_list(void); // returns weapon-list ptr
    int object = *(int *)(actor + 0x7c);
    int list = match_build_weapon_list();
    int count = 0;
    int idx = 0;
    int off = 0;
    while (idx < *(int *)(list + 4)) {
        if (*(char *)(object + *(int *)(list + off + 8) + 0x54c) != '\0') {
            if (idx < 1) {
                count = count + 1;
            } else {
                int dupPos = 0;
                int dupOff = 0;
                int remaining = idx;
                do {
                    if (*(int *)(list + dupOff + 8) == *(int *)(list + off + 8))
                        break;
                    dupPos = dupPos + 1;
                    dupOff = dupOff + 4;
                    remaining = remaining - 1;
                } while (remaining != 0);
                if (dupPos == idx)
                    count = count + 1;
            }
        }
        idx = idx + 1;
        off = off + 4;
    }
    return count;
}

// =============================================================================
// 0x802bc840  aiactor_behavior_flee_tick
// Flee / regroup mode FSM (rec.mode 0..4): pick a flee/spawn zone, follow the
// path, and retarget along the way.
// =============================================================================
void aiactor_behavior_flee_tick(void)
{
    int actor = aiactor_current_frame_actor();
    int rec = *(int *)(actor + 0x7c);
    int hasTeammate = aiactor_retarget_visible_teammate();
    ai_tick_preamble(rec);

    aiactor_set_stance_flags(actor, 1);
    int active = anim_is_clip_active(actor, 0x36);
    if ((active == 0) || (anim_active_slot_userflags(*(int *)(actor + 4)) != 0))
        aiactor_play_turn_animation(actor);

    int mode = *(int *)(rec + 0x20);
    if (mode == 2) {
        int nearby = aiactor_find_nearest_alarmed_ally(actor);
        if (nearby == 0) {
            int best = tracker_select_best_target_scored();
            if (best == 0) {
                if (*(int *)(rec + 4) == 0)
                    *(int *)(rec + 4) = tracker_select_best_target_scored();
            } else {
                *(int *)(rec + 4) = best;
                int zone = *(int *)(*(int *)(*(int *)(rec + 4) + 0x7c) + 0x678);
                if (zone < 0)
                    *(int *)(rec + 0x684) =
                        *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    *(int *)(rec + 0x684) = zone;
                *(int *)(rec + 0x9ac) = 0;
                *(int *)(rec + 0x9a8) = 0;
            }
            if (aiactor_follow_zone_path() == 0) {
                if (hasTeammate == 0)
                    *(float *)(rec + 0xb30) = FLOAT_80619c4c;
                if ((FLOAT_80619c48 < *(float *)(rec + 0xb30)) && (*(int *)(rec + 0x20) != 1))
                    ai_set_mode(rec, 1);
            } else if (((hasTeammate == 0) || (*(int *)(rec + 4) == 0)) ||
                       (*(int *)(rec + 0x678) !=
                        *(int *)(*(int *)(*(int *)(rec + 4) + 0x7c) + 0x678))) {
                if (*(int *)(rec + 0x20) != 1)
                    ai_set_mode(rec, 1);
            } else if (*(int *)(rec + 0x20) != 4) {
                ai_set_mode(rec, 4);
            }
        } else {
            *(int *)(rec + 4) = nearby;
            if (*(int *)(rec + 0x20) != 3)
                ai_set_mode(rec, 3);
        }
    } else if (mode < 2) {
        if (mode == 0) {
            *(int *)(rec + 0x9d4) = 0;
            *(float *)(rec + 0x9d8) = FLOAT_80619c4c;
            *(int *)(rec + 0x20) = 1;
            *(int *)(rec + 0x9d0) = *(int *)(rec + 0x20);
            *(int *)(rec + 4) = tracker_select_best_target_scored();
        } else if (-1 < mode) {
            int zone = -1;
            if (*(int *)(rec + 4) != 0)
                zone = spawn_find_random_by_group(
                    (int)*(short *)(DAT_80611b8c +
                        *(int *)(*(int *)(*(int *)(rec + 4) + 0x7c) + 0x678) * 0x20 + 10));
            if (zone == -1) {
                zone = spawn_find_random_in_range();
                if (zone == 0) zone = spawn_find_random_in_range();
                if (zone == 0) zone = spawn_find_random_in_range();
                while (zone < 1 || (zone == *(int *)(rec + 0x678))) {
                    unsigned int r = rng_counter_tick();
                    zone = *(int *)(DAT_80611b8c +
                        (r - (r / (unsigned int)DAT_80611c14) * (unsigned int)DAT_80611c14) * 0x20);
                }
                int z = soundpad_find_zone_by_id(zone);
                if (z < 0)
                    *(int *)(rec + 0x684) =
                        *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    *(int *)(rec + 0x684) = soundpad_find_zone_by_id(zone);
                *(int *)(rec + 0x9ac) = 0;
                *(int *)(rec + 0x9a8) = 0;
            } else {
                if (zone < 0)
                    *(int *)(rec + 0x684) =
                        *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    *(int *)(rec + 0x684) = zone;
                *(int *)(rec + 0x9ac) = 0;
                *(int *)(rec + 0x9a8) = 0;
            }
            if (*(int *)(rec + 0x20) != 2)
                ai_set_mode(rec, 2);
            *(float *)(rec + 0xb30) = FLOAT_80619c4c;
        }
    } else if (mode == 4) {
        *(int *)(rec + 0x30) = *(int *)(*(int *)(rec + 4) + 100);
        *(int *)(rec + 0x34) = *(int *)(*(int *)(rec + 4) + 0x68);
        *(int *)(rec + 0x38) = *(int *)(*(int *)(rec + 4) + 0x6c);
        if (((*(unsigned int *)(*(int *)(*(int *)(rec + 4) + 0x7c) + 0xc6c) & 4) != 0) &&
            (*(int *)(rec + 0x20) != 1))
            ai_set_mode(rec, 1);
        if ((*(int *)(rec + 0x678) != *(int *)(*(int *)(*(int *)(rec + 4) + 0x7c) + 0x678)) &&
            (*(int *)(rec + 0x20) != 2))
            ai_set_mode(rec, 2);
    } else if (mode < 4) {
        int nearby = aiactor_find_nearest_alarmed_ally(actor);
        if (nearby == 0) {
            if ((FLOAT_80619c9c < *(float *)(rec + 0xb30)) || (*(int *)(rec + 4) == 0)) {
                *(float *)(rec + 0xb30) = FLOAT_80619c4c;
                if (*(int *)(rec + 0x20) != 2)
                    ai_set_mode(rec, 2);
                goto after;
            }
        } else {
            *(int *)(rec + 4) = nearby;
            *(float *)(rec + 0xb30) = FLOAT_80619c4c;
        }
        {
            int zone = *(int *)(*(int *)(*(int *)(rec + 4) + 0x7c) + 0x678);
            if (zone < 0)
                *(int *)(rec + 0x684) =
                    *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            else
                *(int *)(rec + 0x684) = zone;
            *(int *)(rec + 0x9ac) = 0;
            *(int *)(rec + 0x9a8) = 0;
            if (aiactor_follow_zone_path() == 0) {
                if (((*(int *)(rec + 4) != 0) &&
                     ((*(unsigned int *)(*(int *)(*(int *)(rec + 4) + 0x7c) + 0xc6c) & 4) != 0)) &&
                    (*(int *)(rec + 0x20) != 1))
                    ai_set_mode(rec, 1);
            } else if ((*(int *)(rec + 4) == 0) ||
                       (*(int *)(rec + 0x678) !=
                        *(int *)(*(int *)(*(int *)(rec + 4) + 0x7c) + 0x678))) {
                if (*(int *)(rec + 0x20) != 1)
                    ai_set_mode(rec, 1);
            } else if (*(int *)(rec + 0x20) != 4) {
                ai_set_mode(rec, 4);
            }
        }
    }
after:
    if ((*(unsigned int *)(rec + 0x28) & 1) != 0) {
        int codeA, codeB;
        if (DAT_805015bd == '\n') {
            codeA = *(int *)(rec + 0x10);
            codeB = *(int *)(rec + 0x14);
        } else {
            codeA = *(int *)(rec + 0x18);
            codeB = *(int *)(rec + 0x1c);
        }
        *(int *)(rec + 0xabc) = codeB;
        *(int *)(rec + 0xab8) = codeA;
        ai_queue_secondary(rec, 0x20);
    }
}

// =============================================================================
// 0x802bcf70  aiactor_find_nearest_alarmed_ally
// Scan bot slots for the closest actor whose alarm value (rec[0x2a0]) exceeds
// the threshold and which is not disabled (rec[0x31b] bit 4). Returns entity.
// =============================================================================
int aiactor_find_nearest_alarmed_ally(int actor)
{
    int best = 0;
    int off = 0;
    int remaining = DAT_80612610;
    float bestDist = FLOAT_80619c30;
    if (0 < DAT_80612610) {
        do {
            int *rec = (int *)(DAT_80611d90 + off);
            if ((FLOAT_80619c78 < (float)rec[0x2a0]) && ((rec[0x31b] & 4) == 0)) {
                int e = *rec;
                float dx = *(float *)(actor + 100) - *(float *)(e + 100);
                float dy = *(float *)(actor + 0x68) - *(float *)(e + 0x68);
                float dz = *(float *)(actor + 0x6c) - *(float *)(e + 0x6c);
                float d = dz * dz + dx * dx + dy * dy;
                if (d < bestDist) {
                    best = e;
                    bestDist = d;
                }
            }
            off = off + 0xea0;
            remaining = remaining - 1;
        } while (remaining != 0);
    }
    return best;
}

// =============================================================================
// 0x802bd00c  aiactor_scan_nearest_squadb_hostile
// Nearest squad-B actor of a different team (distance is weighted up when a
// squadmate already targets it, to spread the squad out). Result via reg pair.
// =============================================================================
static float g_squadb_scan_dist;

int aiactor_scan_nearest_squadb_hostile_ret(void)
{
    aiactor_scan_nearest_squadb_hostile();
    return (int)g_squadb_scan_dist;
}

void aiactor_scan_nearest_squadb_hostile(void)
{
    int actor = aiactor_current_frame_actor();
    int *rec = *(int **)(actor + 0x7c);
    double bestDist = (double)FLOAT_80619c30;
    int off = 0;
    for (int i = 0; i < (int)aiactor_squad_b_count(); i = i + 1) {
        int other = *(int *)((int)&DAT_80588888 + off);
        if (*(int *)(*(int *)(other + 0x7c) + 0x1c) != rec[0x21]) {
            int e = *rec;
            float dx = *(float *)(e + 100) - *(float *)(other + 100);
            float dy = *(float *)(e + 0x68) - *(float *)(other + 0x68);
            float dz = *(float *)(e + 0x6c) - *(float *)(other + 0x6c);
            double d = (double)(dz * dz + dx * dx + dy * dy);
            if (aiactor_squadmate_targeting(actor, other) != 0)
                d = (double)(float)(d * (double)FLOAT_80619cd4);
            if (d < bestDist)
                bestDist = d;
        }
        off = off + 8;
    }
    g_squadb_scan_dist = (float)bestDist;
}

// =============================================================================
// 0x802bd118  aiactor_squadmate_targeting
// Does any active squadmate (same squad id at rec+0x84, excluding self) already
// have entity `candidate` as its follow target (rec+0xaf0)?
// =============================================================================
int aiactor_squadmate_targeting(int actor, int candidate)
{
    int off = 0;
    int remaining = DAT_80612098;
    if (0 < DAT_80612098) {
        do {
            int other = *(int *)((int)&DAT_80588610 + off);
            if (((*(int *)(other + 0x84) == *(int *)(*(int *)(actor + 0x7c) + 0x84)) &&
                 (other != *(int *)(actor + 0x7c))) && (*(int *)(other + 0xaf0) == candidate))
                return 1;
            off = off + 4;
            remaining = remaining - 1;
        } while (remaining != 0);
    }
    return 0;
}

// =============================================================================
// 0x802bd198  aiactor_behavior_gangup_tick
// Squad-B group-hunt mode FSM: gather the squad onto a shared target, path to
// its zone, and close in / hold at cohesion distance.
// =============================================================================
void aiactor_behavior_gangup_tick(void)
{
    int actor = aiactor_current_frame_actor();
    int rec = *(int *)(actor + 0x7c);
    int hasTeammate = aiactor_retarget_visible_teammate();
    ai_tick_preamble(rec);
    aiactor_retarget_scan();

    int teammateGate;
    if ((hasTeammate != 0) || (FLOAT_80619c64 <= *(float *)(rec + 0xb34))) {
        teammateGate = hasTeammate;
        if (hasTeammate != 0)
            *(float *)(rec + 0xb34) = FLOAT_80619c4c;
    } else {
        teammateGate = 1;
    }

    if ((*(int *)(rec + 700) == 0) && (aiactor_all_zone_targets_inactive() == 0)) {
        if (ai_queue_secondary(rec, 0x40000))
            return;
        return;
    }

    int mode = *(int *)(rec + 0x20);
    if (mode == 2) {
        if (aiactor_follow_zone_path() != 0) {
            *(int *)(rec + 0x30) = *(int *)(*(int *)(rec + 0xaf0) + 100);
            *(int *)(rec + 0x34) = *(int *)(*(int *)(rec + 0xaf0) + 0x68);
            *(int *)(rec + 0x38) = *(int *)(*(int *)(rec + 0xaf0) + 0x6c);
            if (*(int *)(rec + 0x20) != 3)
                ai_set_mode(rec, 3);
            *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) & 0xfffbffff;
            *(float *)(rec + 0xb30) = FLOAT_80619c4c;
        }
        aiactor_play_turn_animation(actor);
        aiactor_facing_update();
        if ((*(int *)(rec + 4) == 0) ||
            (!aiactor_aim_alignment_test((double)*(float *)(rec + 0x9bc), actor,
                                         (float *)(*(int *)(rec + 4) + 100)))) {
            *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) | 0x40000;
        } else if (hasTeammate != 0) {
            *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) | 2;
            *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) & 0xfffbffff;
        }
        if (((*(int **)(actor + 4))[0x32] & 1) != 0)
            anim_set_playback_rate((double)FLOAT_80619c58, *(int **)(actor + 4), 2);
    } else if (mode < 2) {
        if (mode == 0) {
            *(int *)(rec + 0x9d4) = 0;
            *(float *)(rec + 0x9d8) = FLOAT_80619c4c;
            *(int *)(rec + 0x20) = 1;
            *(int *)(rec + 0x9d0) = *(int *)(rec + 0x20);
            *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) | 0x20000;
            *(int *)(rec + 0xaf0) = 0;
        } else if (-1 < mode) {
            aiactor_play_turn_animation(actor);
            aiactor_facing_update();
            int nearest = aiactor_scan_nearest_squadb_hostile_ret();
            if ((nearest == 0) ||
                (*(int *)(*(int *)(nearest + 0x7c) + 0x38) == *(int *)(rec + 0x678))) {
                unsigned int cnt = aiactor_squad_b_count();
                unsigned int r = rng_counter_tick();
                int pick = (&DAT_80588888)[(r - (r / cnt) * cnt) * 2];
                *(int *)(rec + 0xaf0) = pick;
                int zone = *(int *)(*(int *)(pick + 0x7c) + 0x38);
                if (zone < 0)
                    *(int *)(rec + 0x684) =
                        *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    *(int *)(rec + 0x684) = zone;
                *(int *)(rec + 0x9ac) = 0;
                *(int *)(rec + 0x9a8) = 0;
            } else {
                *(int *)(rec + 0xaf0) = nearest;
                int zone = *(int *)(*(int *)(nearest + 0x7c) + 0x38);
                if (zone < 0)
                    *(int *)(rec + 0x684) =
                        *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    *(int *)(rec + 0x684) = zone;
                *(int *)(rec + 0x9ac) = 0;
                *(int *)(rec + 0x9a8) = 0;
            }
            *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) | 0x20000;
            if (*(int *)(rec + 0x20) != 2)
                ai_set_mode(rec, 2);
        }
    } else if (mode < 4) {
        int followRec = *(int *)(*(int *)(rec + 0xaf0) + 0x7c);
        int nearest;
        if ((*(float *)(rec + 0xb30) <= FLOAT_80619c9c) ||
            ((nearest = aiactor_scan_nearest_squadb_hostile_ret()), nearest == 0)) {
            float fx = *(float *)(*(int *)(rec + 0xaf0) + 100);
            float fz = *(float *)(*(int *)(rec + 0xaf0) + 0x6c);
            float dx = *(float *)(actor + 100) - fx;
            float dz = *(float *)(actor + 0x6c) - fz;
            if ((*(int *)(followRec + 0x1c) == *(int *)(rec + 0x84)) ||
                (dx * dx + dz * dz <= FLOAT_80619cd8)) {
                if (teammateGate == 0) {
                    int trig = soundpad_find_trigger_by_id(
                        *(int *)(DAT_80611b8c + *(int *)(rec + 0x678) * 0x20));
                    if (trig == 0)
                        aiactor_turn_toward_angle(
                            (double)*(float *)(DAT_80611b8c + *(int *)(rec + 0x678) * 0x20 + 0x18),
                            actor, 0);
                    *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) | 0x40000;
                } else if (*(int *)(rec + 4) != 0) {
                    aiactor_turn_toward_leader();
                    *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) & 0xfffbffff;
                    if (*(float *)(rec + 0xaf4) < FLOAT_80619cc0) {
                        if (ai_queue_secondary(rec, 4))
                            return;
                    }
                }
                *(int *)(rec + 0x30) = *(int *)(actor + 100);
                *(int *)(rec + 0x34) = *(int *)(actor + 0x68);
                *(int *)(rec + 0x38) = *(int *)(actor + 0x6c);
                tracker_play_melee_fidget_clip(actor);
            } else {
                *(float *)(rec + 0x30) = fx;
                *(int *)(rec + 0x34) = *(int *)(*(int *)(rec + 0xaf0) + 0x68);
                *(int *)(rec + 0x38) = *(int *)(*(int *)(rec + 0xaf0) + 0x6c);
                aiactor_set_stance_flags(actor, 1);
                aiactor_play_turn_animation(actor);
                aiactor_facing_update();
            }
            if (((*(int **)(actor + 4))[0x32] & 1) != 0)
                anim_set_playback_rate((double)FLOAT_80619c58, *(int **)(actor + 4), 2);
            if (teammateGate != 0)
                *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) | 2;
        } else if (*(int *)(rec + 0x20) != 1) {
            ai_set_mode(rec, 1);
        }
    }

    if ((*(int *)(rec + 0x20) != 3) && ((*(unsigned int *)(rec + 0x28) & 1) != 0)) {
        int codeA, codeB;
        if (DAT_805015bd == '\n') {
            codeA = *(int *)(rec + 0x10);
            codeB = *(int *)(rec + 0x14);
        } else {
            codeA = *(int *)(rec + 0x18);
            codeB = *(int *)(rec + 0x1c);
        }
        *(int *)(rec + 0xabc) = codeB;
        *(int *)(rec + 0xab8) = codeA;
        ai_queue_secondary(rec, 0x20);
    }
}

// =============================================================================
// 0x802bd884  aiactor_behavior_swim_tick
// Water behaviour: mode 1 moves toward the sound-pad water surface height and
// snaps to it; mode 0 warps up to the surface via scene_ray_traverse_portals.
// The incoming FP-register spill args (a..g) are Ghidra frame junk; the actor is
// the final integer parameter.
// =============================================================================
void aiactor_behavior_swim_tick(unsigned long long a, double b, double c, double d,
                                double e, double f, double g, unsigned long long h, int actor)
{
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h;
    int rec = *(int *)(actor + 0x7c);
    ai_tick_preamble(rec);

    int mode = *(int *)(rec + 0x20);
    if (mode == 1) {
        *(int *)(rec + 0x30) = *(int *)(actor + 100);
        *(int *)(rec + 0x34) = *(int *)(actor + 0x68);
        *(int *)(rec + 0x38) = *(int *)(actor + 0x6c);
        aiactor_movement_tick((unsigned long long)0, (double)FLOAT_80619c50,
                              (double)FLOAT_806108ec, 0, 0, 0, 0, (unsigned long long)0);
        float surfY = *(float *)(DAT_80611b8c + *(int *)(rec + 0x678) * 0x20 + 0x10);
        if ((ABS(*(float *)(actor + 0x68) - surfY) < FLOAT_80619c5c) ||
            ((*(unsigned int *)(rec + 0x24) & 0x40) == 0)) {
            if (FLOAT_80619ce0 < *(float *)(actor + 0x68))
                *(int *)(actor + 0x68) =
                    *(int *)(DAT_80611b8c + *(int *)(rec + 0x678) * 0x20 + 0x10);
            ai_queue_secondary(rec, 0x10);
        }
    } else if ((mode < 1) && (-1 < mode)) {
        float probe[3];      // local_28/24/20
        probe[0] = DAT_803e07d8;
        probe[1] = DAT_803e07dc;
        probe[2] = DAT_803e07e0;
        unsigned long long clip = anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619cdc);
        *(unsigned int *)(rec + 0x24) = *(unsigned int *)(rec + 0x24) | 0x40;
        int zoneOff = *(int *)(rec + 0x678) * 0x20;
        int surf = scene_ray_traverse_portals(clip, (double)FLOAT_80619cdc,
                                              DAT_80611b8c + zoneOff + 0xc,
                                              (int)*(short *)(DAT_80611b8c + zoneOff + 10),
                                              &probe[0], 0);
        *(int *)(actor + 0x34) = surf;
        if (*(int *)(rec + 0x20) != 1) {
            *(int *)(rec + 0x9d4) = *(int *)(rec + 0x20);
            *(float *)(rec + 0x9d8) = FLOAT_80619c4c;
            *(int *)(rec + 0x20) = 1;
            *(int *)(rec + 0x9d0) = *(int *)(rec + 0x20);
        }
    }
}

} // extern "C"
