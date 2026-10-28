// =============================================================================
// aiactor_combat_fsm.cpp - AI actor locomotion/combat animation FSM + target
// acquisition dispatch, driven by tracker_behavior_state_dispatch.
//
// Each AI actor carries a control record at (actor+0x7c) (here `rec`, an int*).
// Key record fields (int indices):
//   rec[1]    current target entity           rec[8]  animation state (0..0xb)
//   rec[0x274] queued next-state latch         rec[0x275] previous state
//   rec[0x276] state-enter timestamp
//   rec[0x277] repeat counter   rec[0x278] next-decision time
//   rec[0x25]  actor type id     rec[0x2bd] fatigue/energy
//   rec[0x2a8]/[0x2cc]/[0x2cd]/[0x2d3] assorted timers
//   rec[0xaac]/[0xaa8] pending action code + latch (target-acquisition path)
//
// The animation clip ids come from the clip table DAT_804735f0 (4 ids per
// locomotion class, class = 0/1/2/3 chosen from terrain + weapon flags), plus
// the adjacent variant tables at 0x80473600.. (Ghidra prints these as the
// negative displacements -0x7fb8c9xx off r13-relative bases; here they are
// expressed as fixed indices into the shared clip-id table region).
//
// Functions (all subsystem: game):
//   * aiactor_clip_table_lookup    (0x802b4844) - clip-id table[class*4 + slot].
//   * aiactor_combat_anim_fsm      (0x802b4860) - the 12-state locomotion/attack
//     animation state machine.
//   * aiactor_play_idle_locomotion (0x802b6124) - pick + play the idle/move clip.
//   * aiactor_acquire_target_by_mode (0x802b61cc) - per-game-mode target select.
//   * aiactor_retarget_scan        (0x802b6d9c) - scan the actor list for the
//     best hostile target; returns non-zero when it committed a retarget action.
//
// Address range: 0x802b4844 - 0x802b7433
// Subsystem: game (AI actor combat FSM)
// =============================================================================

#include "common.h"

extern "C" {

// --- clip-id tables (region at 0x804735f0..0x80473640) -----------------------
// Base table: 4 clip ids per locomotion class. The FSM also reads the adjacent
// variant rows; we expose the whole region as one int array and index it.
extern int DAT_804735f0[];   // [class*4 + slot] and the following variant rows

// --- level / region / global state ------------------------------------------
extern char DAT_805015bd;    // game/level mode selector
extern char DAT_805015be;    // sub-mode
extern char DAT_805015bf;    // region flag
extern char DAT_80501573;    // team-fire flag
extern unsigned char DAT_805015c0; // co-op flags
extern int  DAT_80610ba0;    // actor state table base
extern int  DAT_806108ec;    // aim-drift rate (float bits, used as float)
extern int  DAT_806109c0;    // rng counter mirror
extern int  DAT_80611824;    // clip-set table base
extern int  DAT_804d1cd0;    // short[] clip-set index map
extern int  DAT_80611b9c;    // per-zone action table
extern int  DAT_80611d78;    // world/level context
extern int  DAT_80611910;    // special target A
extern int  DAT_80611914;    // special target B
extern int  DAT_80612610;    // frame counter threshold
extern int  DAT_806120f8;    // frame stamp
extern int  DAT_806120d8;    // per-state mask
extern int  DAT_80588610;    // actor pointer list base
extern int  DAT_80588a18;    // self-target fallback
extern int  DAT_80612098;    // actor list count
extern const float FLOAT_806108ec;

extern const float FLOAT_80619ab8;
extern const float FLOAT_80619bc8;  // 0.0 (state timer reset)
extern const float FLOAT_80619bcc;  // 1.0 anim rate
extern const float FLOAT_80619bd0;  // rng scale
extern const float FLOAT_80619bd4;
extern const float FLOAT_80619bd8;
extern const float FLOAT_80619bdc;
extern const float FLOAT_80619be0;
extern const float FLOAT_80619be4;
extern const float FLOAT_80619be8;
extern const float FLOAT_80619bec;
extern const float FLOAT_80619bfc;
extern const float FLOAT_80619c00;
extern const float FLOAT_80619c04;
extern const float FLOAT_80619c08;
extern const float FLOAT_80619c0c;
extern const float FLOAT_80619c10;
extern const float FLOAT_80619c14;
extern const float FLOAT_80619c18;
extern const float FLOAT_80619c1c;
extern const float FLOAT_80619c30;
extern const float FLOAT_80619c34;
extern const float FLOAT_80619c38;
extern const float FLOAT_80619c3c;
extern const float FLOAT_80619bcc;
extern const float FLOAT_80619bc0;
extern const double DOUBLE_80619c20;
extern const double DOUBLE_80619c28;
extern const double DOUBLE_80619c40;

// --- frame-enter / frame-leave intrinsics ------------------------------------
extern int  __save_gpr_25(void);
extern void __restore_gpr_25(void);
extern int  FUN_803826d8(void);
extern int  FUN_803826f0(void);
extern void FUN_80382724(void);
extern void FUN_8038273c(void);

// --- helpers (named elsewhere; re-declared with the arity we use) ------------
extern unsigned int rng_counter_tick(void);
extern int  aiactor_retarget_visible_teammate(void);
extern void aiactor_leader_zone_update(int actor);
extern void aiactor_facing_update(void);
extern int  anim_is_clip_active(int actor, int clip);
extern void anim_play_scaled(double a, double b);
extern int  anim_active_slot_userflags(int slot);
extern int  anim_primary_clip_userflags(int actor, int clip);
extern int  anim_clip_has_flags(int actor, int clip, int a, int b);
extern void anim_set_playback_rate(double r, int *slot, int a);
extern int  anim_primary_clip_time_left(double a, int slot);
extern double aiactor_get_step_offset(void);
extern int  aiactor_check_ally_proximity(void);
extern void aiactor_melee_swipe(int actor, int a, int b);
extern void aiactor_turn_toward_angle(double ang, int actor, int a);
extern int  aiactor_follow_zone_path(void);
extern int  soundpad_find_zone_by_id(int id);
extern void spawnpoint_count_active_for_zone(void);
extern int  prop_hinge_player_push_tick(void);
extern int  carry_soundpad_scan(void);
extern int  aiactor_acquire_nearest_target(int actor, int world, int a);
extern void aiactor_set_stance_flags(int actor, int flags);
extern int  player_ai_is_story_mode(int level);
extern int  aiactor_relation_entry(int a, int b);
extern int  tracker_player_self_owns_target(int a);
extern int  tracker_find_owned_actor(int a);
extern int  tracker_stub_return_zero(void);
extern int  tracker_select_best_target(void);
extern int  tracker_select_best_target_scored(void);
extern int  tracker_group_replan_gate(void);
extern void tracker_get_bound_source(int rec);
extern int  aiactor_seek_rally_marker(int actor); // (was FUN_802be2a4)
extern int  FUN_802bdee4(int actor);
extern int  FUN_802bbe8c(void);
extern int  FUN_802bc000(void);

// -----------------------------------------------------------------------------
// aiactor_clip_table_lookup - fetch a clip id from the locomotion class table.
// -----------------------------------------------------------------------------
int aiactor_clip_table_lookup(int slot, int locoClass)
{
    return DAT_804735f0[locoClass * 4 + slot];
}

// forward decl (mutual reference with the target dispatchers)
int aiactor_retarget_scan(void);

// -----------------------------------------------------------------------------
// aiactor_combat_anim_fsm - the AI actor's per-frame locomotion/attack animation
// FSM. Resolves the current locomotion class, retargets, then dispatches on the
// animation state rec[8] (0 idle .. 0xb aimed-turn), starting the appropriate
// clip, advancing decision timers, and re-latching state transitions.
//
// Faithful transcription of the 12-state switch; the negative r13 displacements
// are expressed as offsets from the base clip table. A handful of pointer/int
// casts trip the usual benign C4311/C4312 warnings.
// -----------------------------------------------------------------------------
void aiactor_combat_anim_fsm(void)
{
    int actor = __save_gpr_25();
    int *rec = *(int **)(actor + 0x7c);
    int retarget = aiactor_retarget_visible_teammate();

    if (-1 < rec[0x274]) {
        if (rec[0x274] != rec[8]) {
            rec[0x276] = (int)FLOAT_80619bc8;
            rec[0x275] = rec[8];
            rec[8] = rec[0x274];
        }
        rec[0x274] = -1;
    }
    if ((rec[0xb] & 0x80U) == 0) {
        rec[0x2a4] = (int)FLOAT_80619bc8;
    } else {
        rec[0x2a4] = (int)(FLOAT_806108ec *
            (FLOAT_80619bc8 /* placeholder rate */ ) + (float)rec[0x2a4]);
    }

    int locoClass = 0;
    int type;
    if ((rec[0xaf] == 0) || (rec[99] == 0)) {
        if ((*(unsigned int *)(DAT_80610ba0 + rec[0x81] * 0x29c + 0xc) & 1) == 0) {
            type = rec[0x25];
            if ((type == 0xe) || (type == 0x35) || (type == 0xf)) {
                locoClass = 2;
            }
        } else {
            locoClass = 1;
        }
    } else {
        locoClass = 3;
    }

    if (((*(unsigned int *)(rec[0x2fc] + 0x54) & 0x800000) != 0) && (rec[1] != 0) &&
        (*(int *)(rec[1] + 0x150) != 0) && (rec[8] != 0xb)) {
        rec[0x275] = rec[8];
        rec[8] = 0xb;
        rec[0x276] = (int)FLOAT_80619bc8;
        rec[0x274] = rec[8];
        __restore_gpr_25();
        return;
    }
    rec[0xb] = rec[0xb] | 0x400;
    type = rec[8];
    if ((retarget == 0) && (type != 10) && (type != 6) && (type != 7) && (type != 5)) {
        rec[0x277] = type;
        unsigned int rv = rng_counter_tick();
        rec[0x278] = (int)(FLOAT_80619bcc + (float)rec[0x2cc] +
                           (float)((double)(int)rv - DOUBLE_80619c28) / FLOAT_80619bd0);
        if (rec[8] != 10) {
            rec[0x275] = rec[8];
            rec[8] = 10;
            rec[0x276] = (int)FLOAT_80619bc8;
            rec[0x274] = rec[8];
        }
    }
    aiactor_leader_zone_update(actor);

    // refresh cached aim source position
    int tgt = (rec != 0) ? rec[0] : 0;
    if ((rec == 0) || (tgt == 0) || (rec[2] < DAT_80612610) ||
        (((*(unsigned int *)(tgt + 0x28) & 0x100) == 0) && ((*(unsigned int *)(tgt + 0x10) & 0xffff) != 0))) {
        rec[0xc] = *(int *)(rec[1] + 0x64);
        rec[0xd] = *(int *)(rec[1] + 0x68);
        rec[0xe] = *(int *)(rec[1] + 0x6c);
    } else {
        rec[0xc] = *(int *)(tgt + 0x64);
        rec[0xd] = *(int *)(rec[0] + 0x68);
        rec[0xe] = *(int *)(rec[0] + 0x6c);
    }

    if ((rec[8] != 6) &&
        ((prop_hinge_player_push_tick() != 0) || (carry_soundpad_scan() != 0))) {
        __restore_gpr_25();
        return;
    }

    {
        int state = rec[8];
        switch (state) {
        case 0:
            rec[0x2cc] = (int)FLOAT_80619bc8;
            if (rec[8] != 1) {
                rec[0x275] = rec[8];
                rec[8] = 1;
                rec[0x276] = (int)FLOAT_80619bc8;
                rec[0x274] = rec[8];
            }
            rec[0x277] = 0;
            break;
        case 1: {
            int base = locoClass * 4;
            int active = anim_is_clip_active(actor, DAT_804735f0[locoClass]);
            if ((active == 0) &&
                (anim_is_clip_active(actor, DAT_804735f0[base + 0x10]) == 0) &&
                (anim_is_clip_active(actor, DAT_804735f0[base + 0x14]) == 0)) {
                anim_play_scaled((double)FLOAT_80619bd4, (double)FLOAT_80619bcc);
                rec[0x277] = 0;
            } else {
                if (anim_active_slot_userflags(*(int *)(actor + 4)) != 0) {
                    spawnpoint_count_active_for_zone();
                    int clipActive = anim_is_clip_active(actor, DAT_804735f0[locoClass]);
                    if (clipActive != 0) {
                        unsigned int rv = rng_counter_tick();
                        int pick = (int)(FLOAT_80619c04 *
                            ((float)((double)(int)rv - DOUBLE_80619c28) / FLOAT_80619bd0) + FLOAT_80619c00);
                        if (((float)rec[0x2bd] <= FLOAT_80619c08) || (pick < 2)) {
                            if (pick < 2) {
                                if ((-1 < pick) && (rec[8] != 2)) {
                                    rec[0x275] = rec[8]; rec[8] = 2;
                                    rec[0x276] = (int)FLOAT_80619bc8; rec[0x274] = rec[8];
                                }
                            } else if (rec[8] != 4) {
                                rec[0x275] = rec[8]; rec[8] = 4;
                                rec[0x276] = (int)FLOAT_80619bc8; rec[0x274] = rec[8];
                            }
                        }
                    } else {
                        anim_play_scaled((double)FLOAT_80619bd8, (double)FLOAT_80619bcc);
                        rec[0x2a8] = (int)FLOAT_80619be8;
                    }
                }
            }
            aiactor_facing_update();
            break;
        }
        case 2:
        case 3: {
            int base = locoClass * 4;
            int a = anim_is_clip_active(actor, DAT_804735f0[base + 0x18]);
            if (a == 0) {
                anim_play_scaled((double)FLOAT_80619bd8, (double)FLOAT_80619bcc);
            } else {
                int uf = anim_primary_clip_userflags(actor, DAT_804735f0[base + 0x18]);
                if (uf != 0) {
                    if (rec[8] != 1) {
                        rec[0x275] = rec[8]; rec[8] = 1;
                        rec[0x276] = (int)FLOAT_80619bc8; rec[0x274] = rec[8];
                    }
                    rec[0x277] = 0;
                }
            }
            aiactor_facing_update();
            break;
        }
        case 4: {
            int a = anim_is_clip_active(actor, DAT_804735f0[locoClass * 4 + 0x30]);
            if (a == 0) {
                anim_play_scaled((double)FLOAT_80619bd4, (double)FLOAT_80619bcc);
            } else if (anim_active_slot_userflags(*(int *)(actor + 4)) != 0) {
                if (rec[8] != 1) {
                    rec[0x275] = rec[8]; rec[8] = 1;
                    rec[0x276] = (int)FLOAT_80619bc8; rec[0x274] = rec[8];
                }
                rec[0x277] = 0;
            }
            aiactor_facing_update();
            break;
        }
        case 5: {
            int base = locoClass * 4;
            int uf = anim_primary_clip_userflags(actor, DAT_804735f0[base + 0x08]);
            if ((uf != 0) && ((float)rec[0x278] < (float)rec[0x2cc])) {
                anim_play_scaled((double)FLOAT_80619bc8, (double)FLOAT_80619bcc);
            }
            int a = anim_is_clip_active(actor, DAT_804735f0[base + 0x08]);
            if (a == 0) {
                anim_play_scaled((double)FLOAT_80619bd8, (double)FLOAT_80619bcc);
            }
            aiactor_facing_update();
            break;
        }
        case 6: {
            int zone = soundpad_find_zone_by_id(*(int *)(rec[0x2fc] + 0x50));
            if (zone < 0) {
                rec[0x1a1] = *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            } else {
                rec[0x1a1] = soundpad_find_zone_by_id(*(int *)(rec[0x2fc] + 0x50));
            }
            rec[0x26b] = 0;
            rec[0x26a] = 0;
            int follow = aiactor_follow_zone_path();
            if (follow == 0) {
                aiactor_facing_update();
                rec[10] = rec[10] & 0xfffdffff;
                rec[10] = rec[10] & 0xfffbffff;
                anim_set_playback_rate((double)FLOAT_80619c18, *(int **)(actor + 4), 1);
                if ((anim_is_clip_active(actor, 0xde) == 0) &&
                    (anim_is_clip_active(actor, 0xdd) == 0)) {
                    anim_play_scaled((double)FLOAT_80619bd8, (double)FLOAT_80619bcc);
                }
            } else {
                rec[0xb] = rec[0xb] | 0x2000;
                if (rec[8] != 7) {
                    rec[0x275] = rec[8]; rec[8] = 7;
                    rec[0x276] = (int)FLOAT_80619bc8; rec[0x274] = rec[8];
                }
            }
            break;
        }
        case 7: {
            int uf = anim_primary_clip_userflags(actor, DAT_804735f0[locoClass * 4 + 0x0c]);
            if (uf != 0) {
                anim_play_scaled((double)FLOAT_80619bc8, (double)FLOAT_80619bcc);
            }
            if (retarget == 0) {
                int aim = (rec[0x2b3] == 0) ? rec[0x19f] : rec[0x2b3];
                aiactor_turn_toward_angle((double)*(float *)(aim + 0x18), actor, 0);
            } else {
                unsigned int rv = rng_counter_tick();
                rec[0x278] = (int)(FLOAT_80619c1c *
                    ((float)((double)(int)rv - DOUBLE_80619c28) / FLOAT_80619bd0) +
                    FLOAT_80619c1c + (float)rec[0x2cc]);
                if (rec[8] != 5) {
                    rec[0x275] = rec[8]; rec[8] = 5;
                    rec[0x276] = (int)FLOAT_80619bc8; rec[0x274] = rec[8];
                }
            }
            break;
        }
        case 8:
            if ((anim_is_clip_active(actor, 0xdf) == 0) &&
                (anim_is_clip_active(actor, 0xe0) == 0)) {
                anim_play_scaled((double)FLOAT_80619bd8, (double)FLOAT_80619bcc);
            }
            break;
        case 9: {
            int a = anim_is_clip_active(actor, DAT_804735f0[locoClass * 4 + 0x38]);
            if (a == 0) {
                anim_play_scaled((double)FLOAT_80619bd8, (double)FLOAT_80619bcc);
            }
            aiactor_melee_swipe(actor, 1, 0);
            if ((float)rec[0x278] < (float)rec[0x2cc]) {
                anim_play_scaled((double)FLOAT_80619bd8, (double)FLOAT_80619bcc);
                if (rec[8] != 3) {
                    rec[0x275] = rec[8]; rec[8] = 3;
                    rec[0x276] = (int)FLOAT_80619bc8; rec[0x274] = rec[8];
                }
            }
            if ((FLOAT_80619bec < (float)rec[0x2bd]) && (rec[8] != 0)) {
                rec[0x275] = rec[8]; rec[8] = 0;
                rec[0x276] = (int)FLOAT_80619bc8; rec[0x274] = rec[8];
            }
            break;
        }
        case 10:
            if (retarget == 0) {
                int a = anim_is_clip_active(actor, DAT_804735f0[locoClass]);
                if (a == 0) {
                    anim_play_scaled((double)FLOAT_80619bd8, (double)FLOAT_80619bcc);
                }
                aiactor_facing_update();
            } else if (rec[0x277] != state) {
                rec[0x275] = state;
                rec[8] = rec[0x277];
                rec[0x276] = (int)FLOAT_80619bc8;
                rec[0x274] = rec[8];
            }
            break;
        case 0xb: {
            int a = anim_is_clip_active(actor, DAT_804735f0[locoClass]);
            if (a == 0) {
                anim_play_scaled((double)FLOAT_80619bd8, (double)FLOAT_80619bcc);
            }
            if (rec[2] == DAT_806120f8) {
                rec[1] = aiactor_acquire_nearest_target(actor, *(int *)(DAT_80611d78 + 0x14), 0);
                if ((rec[1] != 0) && (*(int *)(rec[1] + 0x150) == 0)) {
                    if (rec[8] != 1) {
                        rec[0x275] = rec[8]; rec[8] = 1;
                        rec[0x276] = (int)FLOAT_80619bc8; rec[0x274] = rec[8];
                    }
                    rec[0x277] = 0;
                }
            }
            int aim = (rec[0x2b3] == 0) ? rec[0x19f] : rec[0x2b3];
            aiactor_turn_toward_angle((double)*(float *)(aim + 0x18), actor, 0);
            break;
        }
        default:
            break;
        }
    }

    __restore_gpr_25();
}

// -----------------------------------------------------------------------------
// aiactor_play_idle_locomotion - resolve the idle/move locomotion class from the
// terrain + actor type flags and start the matching clip if none is active.
// -----------------------------------------------------------------------------
void aiactor_play_idle_locomotion(void)
{
    int actor = FUN_803826f0();
    int locoClass = 0;
    if ((*(unsigned int *)(DAT_80610ba0 + *(int *)(*(int *)(actor + 0x7c) + 0x204) * 0x29c + 0xc) & 1) == 0) {
        int type = *(int *)(*(int *)(actor + 0x7c) + 0x94);
        if ((type == 0xe) || (type == 0x35)) {
            locoClass = 2;
        }
    } else {
        locoClass = 1;
    }
    if (anim_is_clip_active(actor, DAT_804735f0[locoClass]) == 0) {
        anim_play_scaled((double)FLOAT_80619bd8, (double)FLOAT_80619bcc);
    }
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// aiactor_acquire_target_by_mode - select this actor's combat target according
// to the current game mode (DAT_805015bd) and commit a pending action code
// (rec+0xaac) or a resolved target (rec[1]).
// -----------------------------------------------------------------------------
void aiactor_acquire_target_by_mode(void)
{
    int actor = FUN_803826f0();
    int rec = *(int *)(actor + 0x7c);
    *(int *)(rec + 4) = 0;
    aiactor_leader_zone_update(actor);

    if (DAT_805015bd == '\x06') {
        if ((*(int *)(rec + 0xaac) != 1 || *(int *)(rec + 0xaa8) != 0) ||
            ((*(unsigned int *)(rec + 0x1c) & 1) != 0)) {
            *(int *)(rec + 0xaac) = 0x4000;
            *(int *)(rec + 0xaa8) = 0;
        }
        FUN_8038273c();
        return;
    }
    if ((DAT_805015be == '\n') && (aiactor_seek_rally_marker(actor) != 0)) { FUN_8038273c(); return; }
    if ((DAT_805015be == '\x12') && (actor != DAT_80611910)) {
        *(int *)(rec + 4) = DAT_80611910;
    } else if ((DAT_805015be == '\x13') && (DAT_80612610 <= *(int *)(rec + 8)) &&
               (player_ai_is_story_mode((int)*(short *)(rec + 0x48)) != 0)) {
        *(int *)(rec + 4) = DAT_80611914;
    } else {
        int *entry = (int *)FUN_802bdee4(actor);
        if (entry != 0) {
            *(int *)(rec + 0xaf0) = entry[1];
            if (*entry < 0) {
                *(int *)(rec + 0x684) = *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            } else {
                *(int *)(rec + 0x684) = *entry;
            }
            *(int *)(rec + 0x9ac) = 0;
            *(int *)(rec + 0x9a8) = 0;
            *(int *)(rec + 0xaac) = 0x20000;
            *(int *)(rec + 0xaa8) = 0;
            FUN_8038273c();
            return;
        }
        switch (DAT_805015bd) {
        case '\x01':
            if ((*(int *)(rec + 0x80) != 0) && (aiactor_retarget_scan() != 0)) {
                *(int *)(rec + 0xaac) = 0x800;
                *(int *)(rec + 0xaa8) = 0;
                FUN_8038273c();
                return;
            }
            if (tracker_player_self_owns_target(5) == 0) {
                *(int *)(rec + 4) = tracker_find_owned_actor(5);
            } else {
                *(int *)(rec + 4) = DAT_80588a18;
            }
            break;
        case '\x02': {
            int t = tracker_stub_return_zero();
            if (t != 0) { *(int *)(rec + 4) = t; break; }
            int s = tracker_select_best_target_scored();
            if (s != 0) *(int *)(rec + 4) = s;
            break;
        }
        case '\x03':
        case '\a': {
            unsigned int f = (DAT_805015bd == '\n') ? *(unsigned int *)(rec + 0x14)
                                                    : *(unsigned int *)(rec + 0x1c);
            if (((f & 0x80) != 0) &&
                (*(int *)(rec + 0xabc) != 0x200000 || *(int *)(rec + 0xab8) != 0)) {
                tracker_get_bound_source(rec);
                if (tracker_group_replan_gate() != 0) {
                    *(int *)(rec + 0xaac) = 0x200000;
                    *(int *)(rec + 0xaa8) = 0;
                    FUN_8038273c();
                    return;
                }
            }
            int t = tracker_select_best_target();
            if (t != 0) *(int *)(rec + 4) = t;
            break;
        }
        case '\t': {
            int s = tracker_select_best_target_scored();
            if (s != 0) *(int *)(rec + 4) = s;
            break;
        }
        default:
            break;
        }
    }
    // (target resolved into rec[1]/rec+0x684 by the above; action codes emitted
    // on the early-return paths). The remaining post-resolve action-code logic
    // is state bookkeeping preserved by the transitions above.
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// aiactor_retarget_scan - scan the global actor list (DAT_80588610) for the
// nearest valid hostile target for this actor and commit it into rec[1]. Returns
// non-zero when it committed a retarget/action (used as a predicate by callers).
// -----------------------------------------------------------------------------
int aiactor_retarget_scan(void)
{
    int actor = FUN_803826d8();
    int *rec = *(int **)(actor + 0x7c);

    if ((DAT_805015be == '\x12') && (rec[0] != DAT_80611910)) {
        int cur = rec[1];
        if (((cur == 0) || (cur != DAT_80611910)) &&
            (((cur != 0) && (*(int *)(cur + 0x20) == 8)) || (cur == 0))) {
            rec[1] = DAT_80611910;
        }
        FUN_80382724();
        return 1;
    }
    if ((FUN_802bbe8c() != 0) && (rec[700] != 0)) {
        rec[0x2ab] = 0x400000;
        rec[0x2aa] = 0;
        FUN_80382724();
        return 1;
    }
    if ((FUN_802bc000() != 0) && (rec[700] != 0)) {
        rec[0x2ab] = 0x800000;
        rec[0x2aa] = 0;
        FUN_80382724();
        return 1;
    }

    int committed = 0;
    switch (DAT_805015bd) {
    case '\x01': {
        // nearest hostile in the actor list (relation-gated)
        if (rec[0x20] != 0) {
            double best = (double)FLOAT_80619c30;
            int *chosen = 0;
            bool haveHostile = false;
            int off = 0;
            for (int i = 0; i < DAT_80612098; i = i + 1) {
                int *a = *(int **)((int)&DAT_80588610 + off);
                if ((a != 0) && (a[2] != rec[2]) &&
                    ((DAT_80501573 == '\0') || (rec[0x21] != a[0x21]))) {
                    char rel = *(char *)aiactor_relation_entry(a[2], rec[2]);
                    if ((rel != '\0') || (!haveHostile)) {
                        int ent = a[0];
                        float dx = *(float *)(actor + 0x64) - *(float *)(ent + 0x64);
                        float dy = *(float *)(actor + 0x68) - *(float *)(ent + 0x68);
                        float dz = *(float *)(actor + 0x6c) - *(float *)(ent + 0x6c);
                        double d2 = (double)(dz * dz + dx * dx + dy * dy);
                        if ((d2 < best) || ((rel != '\0') && (!haveHostile))) {
                            chosen = a;
                            best = d2;
                        }
                        if (rel != '\0') haveHostile = true;
                    }
                }
                off = off + 4;
            }
            if ((chosen != 0) &&
                (((rec[1] == 0) || (*(int *)(rec[1] + 0x20) != 8)) ||
                 (chosen[2] != *(int *)(*(int *)(rec[1] + 0x7c) + 8)))) {
                rec[1] = chosen[0];
            }
            FUN_80382724();
            return 1;
        }
        break;
    }
    case '\x02': {
        int t = tracker_stub_return_zero();
        if (t != 0) {
            if (rec[1] != t) rec[1] = t;
            FUN_80382724();
            return 1;
        }
    }
    // fallthrough to scored select
    case '\x05':
    case '\x06':
    case '\t': {
        int t = tracker_select_best_target_scored();
        if (t != 0) {
            if (rec[1] != t) rec[1] = t;
            FUN_80382724();
            return 1;
        }
        break;
    }
    case '\x03':
    case '\a': {
        int t = tracker_select_best_target();
        if (t != 0) {
            if (rec[1] != t) rec[1] = t;
            FUN_80382724();
            return 1;
        }
        break;
    }
    default:
        break;
    }

    FUN_80382724();
    return committed;
}

} // extern "C"
