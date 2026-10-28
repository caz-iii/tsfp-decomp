// =============================================================================
// aiactor_behavior_combat.cpp - AI actor per-frame combat / mode behaviour ticks
//
// Direct continuation of aiactor_behavior_dispatch.cpp. These routines are the
// per-actor behaviour ticks invoked from the behaviour-state table each frame:
//   - aiactor_behavior_combat_tick (0x802b90a4): the big engage/combat FSM that
//     animates an actor whose behaviour state (rec[8]) is 0..5, playing the
//     stance / fire / melee clips, resolving take-cover, and switching sub-state.
//   - aiactor_los_visibility_tick (0x802ba204): per-actor line-of-sight sweep
//     that updates the shared relation-visibility table via scene raycasts.
//   - aiactor_pick_best_weapon (0x802ba7e4): scan the actor's owned weapon list
//     and switch to the best available slot (was FUN_802ba7e4).
//   - aiactor_behavior_mode_tick (0x802baa58): top-level behaviour-mode FSM
//     (rec.mode at rec+0x20 = 0..4) selecting patrol / follow / combat / flee.
//   - aiactor_cover_fail_target8 / _target16 (0x802bb580 / 0x802bb87c): fallback
//     handlers when a cover/zone request against a class-8 / class-0x10 target
//     fails; queue the default action.
//   - aiactor_target_candidate_valid (0x802bbb78): predicate - is a candidate a
//     legitimate hostile the actor may pursue via the sound-pad zone graph.
//   - aiactor_scan_soundpad_hostile (0x802bbc6c): nearest valid hostile scan over
//     the actor's current + adjacent sound-pad zones.
//   - aiactor_consider_leader_follow / _follower_follow (0x802bbe8c / 0x802bc000):
//     if idle and a squad leader/follower slot is closer, adopt it as target.
//
// AI actor control record lives at (actor+0x7c) ("rec"). Two field views are
// used by the game code: an int-indexed view (rec[N]) and a byte-offset view
// (rec+0xNNN); both alias the same record. Key fields:
//   rec+0x20  (rec[8])   behaviour state / mode        rec+0x9d0  queued state
//   rec+0x9d4 prev state    rec+0x9d8 state timer       rec+0xa90  aim timer
//   rec+0xaac/0xaa8 pending action code + latch
//   rec+0xabc/0xab8 secondary action code + latch
//   rec+0xaf0  follow-target entity   rec+0x94  active weapon slot
//   rec+0x678  resolved zone id       rec+0x684  fallback zone id
//   rec+0xb04  squad rank / difficulty band
//   rec+0xb34  retarget cooldown      rec+0x1cc (on actor) squared range metric
//
// Address range: 0x802b90a4 - 0x802bc17f
// Subsystem: game (AI actor combat / behaviour ticks)
//
// NON-MATCHING: the PPC frame-enter/leave intrinsics (__save_gpr_*, FUN_80382xxx)
// return a register pair whose high word is the incoming actor handle; that is
// modelled here by calling aiactor_current_frame_actor() which returns the actor.
// Incoming paired-single spill temporaries (f29-f31 etc.) that the combat tick
// reads before writing are expressed as an explicit seed struct. The repeated
// "action-latch" idiom
//   if (!(rec.action==1 && rec.latch==0 && ((mode==10?rec.f14:rec.f1c)&1)==0)) {
//       rec.action = CODE; rec.latch = 0; }
// is factored into ai_queue_action(). CONCAT44(0x43300000,x) int->double bias
// idioms are reduced to plain float conversions.
// =============================================================================

#include "common.h"

extern "C" {

// --- level / region / global state (raw symbols this file reads) -------------
extern char DAT_805015bd;          // game/level mode selector
extern char DAT_805015be;          // sub-mode
extern int  DAT_80588610;          // active actor pointer array (stride 4)
extern int  DAT_80588690;          // sound-pad adjacent-actor pointer array (stride 4)
extern int  DAT_80611910;          // designated leader/special actor
extern int  DAT_80611b1c;          // per-entity "no-collide" flag table base
extern int  DAT_80611b8c;          // sound-pad zone table base (stride 0x20)
extern int  DAT_80611b94;          // spawn descriptor table base
extern int  DAT_80611b9c;          // per-zone action/leader word table base (stride 0x2c)
extern int  DAT_80611d78;          // world/level context base
extern int  DAT_80611d90;          // actor record array base (stride 0xea0)
extern int  DAT_80612098;          // active actor count
extern int  DAT_806120d8;          // per-slot flee-permitted bitmask
extern int  DAT_806120f8;          // round-robin LOS scan cursor
extern int  DAT_806120fc;          // actor slot span
extern int  DAT_80612610;          // first bot slot index
extern unsigned char DAT_8048ca8b; // per-character nav-table selector byte (stride 0x30)
extern short DAT_8043e230;         // per-character-def priority value (stride 0x3c)
extern int  DAT_8043e234;          // per-weapon priority table (stride 0xf ints)
extern short DAT_8043e23c;         // per-weapon character-def index (stride 0xf)
extern int  DAT_804308b8;          // per-weapon-class category table (stride 0x24 ints)
extern int  DAT_806109c0;          // frame-delta raw bits (bias-converted below)
extern int  DAT_804739d0;          // AI clip-choice table base
extern int  DAT_80473a0c;          // clip-choice modulus table (state 3)
extern int  DAT_80473a48;          // clip-choice modulus table (state 2)
extern int  DAT_80473a84;          // clip-choice modulus table (state 4)
extern short DAT_804d1cd0;          // clip -> attr index table
extern int  DAT_804d34dc;          // clip attr table (stride 8, bit0 = one-shot)

// float / double literal pool
extern float FLOAT_806108ec;       // aim-timer decay scale
extern float FLOAT_80619c34;       // rng normalisation divisor
extern float FLOAT_80619c38;       // clip playback rate A
extern float FLOAT_80619c48;       // teammate splash reduction (0.5)
extern float FLOAT_80619c4c;       // 0.0f
extern float FLOAT_80619c50;       // 1.0f
extern float FLOAT_80619c58;       // playback rate (follow)
extern float FLOAT_80619c5c;       // weapon-switch playback rate
extern float FLOAT_80619c64;       // range gate multiplier
extern float FLOAT_80619c78;       // melee-range squared threshold
extern float FLOAT_80619c7c;       // clip id 0x26 arg (as float)
extern float FLOAT_80619c80;       // clip playback rate B
extern float FLOAT_80619c84;       // burn-lethal timer threshold
extern float FLOAT_80619c88;       // strafe timer (attack)
extern float FLOAT_80619c8c;       // strafe timer (no attack)
extern float FLOAT_80619c90;       // clip playback rate C
extern float FLOAT_80619c94;       // melee reach A
extern float FLOAT_80619c98;       // melee reach B / range factor
extern float FLOAT_80619c9c;       // melee reach C / timer step
extern float FLOAT_80619ca0;       // melee splash radius squared
extern float FLOAT_80619ca4;       // melee facing dot threshold
extern float FLOAT_80619cac;       // sub-state hysteresis distance
extern float FLOAT_80619cb0;       // melee falloff scale
extern float FLOAT_80619cb4;       // eye-height offset
extern float FLOAT_80619cb8;       // ceiling offset
extern float FLOAT_80619cbc;       // weapon-switch ammo band high
extern float FLOAT_80619cc0;       // weapon-switch ammo band low
extern float FLOAT_80619cc4;       // teammate-help range gate
extern float FLOAT_80619cc8;       // zone-reuse distance gate
extern float FLOAT_80619ccc;       // leader-follow distance gate
extern float FLOAT_80619cd0;       // follower-follow distance gate
extern float FLOAT_80619c30;       // scan initial best distance (large)
extern double DOUBLE_80619c40;     // rng bias base
extern double DOUBLE_80619c68;     // frame-delta bias base

// --- named helpers resolved through generated_decls.h ------------------------
extern int  aiactor_retarget_visible_teammate(void);
extern void aiactor_leader_zone_update(int actor);
extern void aiactor_set_stance_flags(int actor, int flags);
extern int  tracker_scan_class_flag400(void);
extern int  aiactor_request_take_cover(int rec);
extern unsigned int aiactor_roll_attack_chance(int rec);
extern int  aiactor_retarget_scan(void);
extern int  aiactor_follow_zone_path(void);
extern void aiactor_aim_at_target(int actor, int a, int b);
extern double aiactor_get_step_offset(void);
extern int  aiactor_get_active_index(void);
extern int *aiactor_get_scratch_slot(int idx);
extern int  soundpad_find_zone_by_id(int id);
extern int *soundpad_resolve_zone_node(int zone);
extern int  soundpad_get_entity_zone_id(int node);
extern int  tracker_event_state_is_idle(int rec);
extern int  aiactor_relation_entry(int a, int b);
extern void aiactor_set_relation_visible(int cursor, int actorId, int state);
extern int  match_build_weapon_list(void);  // returns weapon-list ptr
extern int  object_slot_apply_params(int object, int slot, int mode);
extern int  object_sound_slot_has_capacity(int object, int slot_index);
extern int  character_burn_is_lethal(int pCharacter);
extern void tracker_play_melee_fidget_clip(int actor);
extern void anim_play_scaled(double a, double b);
extern int  anim_is_clip_active(int actor, int clip);
extern int  anim_primary_clip_time_left(double a, int slot);
extern unsigned int anim_slot_is_primary_clip(int actor, int clip);
extern int  anim_active_slot_userflags(int slot);
extern unsigned int anim_zone_path_step(int actor, int arg, float *out);
extern int  anim_event_scan_active(void);
extern void anim_set_playback_rate(double r, int *slot, int a);
extern int  scene_raycast(int node, float *origin, float *dir, int mode, float *hitPos,
                          float *hitNormal, int *hitNode, int mask, int anyHit, int *extra);
extern void switch_slot_reset(int slot);
extern double math_sqrt(double x);
extern unsigned int rng_counter_tick(void);
extern int  aiactor_aim_alignment_test(double threshold, int actor, float *targetPos);
extern bool aiactor_facing_dot_test(double threshold, int actor, float *targetPos);
extern void damage_dispatch_to_entity(double a, double b, double c, double d, double e,
                                      double f, double g, double h, int *eventData);

// --- unnamed callees (leave as raw externs) ----------------------------------
extern void FUN_802bc1a4(void);     // secondary combat-visibility helper (external)
extern int  FUN_80364290(int rec, int flag); // clip-set query (external)
extern int  aiactor_all_zone_targets_inactive(void); // (was FUN_802bdf68)
extern int  FUN_802bc780(int actor);// owned-weapon count (external)

// --- PPC frame-context intrinsic model ---------------------------------------
// The originals enter through __save_gpr_*/FUN_80382xxx which materialise a
// register pair; the high word is the actor handle these ticks operate on.
extern int  aiactor_current_frame_actor(void);

// forward decls (mutual references inside this file)
int  aiactor_target_candidate_valid(int actor, int candidate);
void aiactor_los_visibility_tick(void);
void aiactor_scan_soundpad_hostile(void);
int  aiactor_scan_soundpad_hostile_ret(void);
static int ai_queue_action_secondary(int rec, int code);

// -----------------------------------------------------------------------------
// Factored "action-latch" idiom.
// Returns non-zero if the caller should skip queueing (the current action is a
// pending code 1 with an unset latch and the mode bit is clear); otherwise it
// writes the new action code + clears the latch and returns 0.
// -----------------------------------------------------------------------------
static int ai_queue_action(int rec, int code)
{
    int actionOff = 0xaac;
    int latchOff  = 0xaa8;
    if (*(int *)(rec + actionOff) == 1 && *(int *)(rec + latchOff) == 0) {
        unsigned int gate;
        if (DAT_805015bd == '\n')
            gate = *(unsigned int *)(rec + 0x14);
        else
            gate = *(unsigned int *)(rec + 0x1c);
        if ((gate & 1) == 0)
            return 1; // caller should bail out (goto exit)
    }
    *(int *)(rec + actionOff) = code;
    *(int *)(rec + latchOff)  = 0;
    return 0;
}

// Set behaviour sub-state (rec[8]) with the prev/queued/timer bookkeeping.
static void ai_set_substate(int *rec, int newState)
{
    if (rec[8] == newState)
        return;
    rec[0x275] = rec[8];
    rec[0x276] = (int)FLOAT_80619c4c;
    rec[8]     = newState;
    rec[0x274] = rec[8];
}

// Byte-offset variant of the state switch used by the mode tick.
static void ai_set_mode(int rec, int newMode)
{
    if (*(int *)(rec + 0x20) == newMode)
        return;
    *(int *)(rec + 0x9d4) = *(int *)(rec + 0x20);
    *(float *)(rec + 0x9d8) = FLOAT_80619c4c;
    *(int *)(rec + 0x20) = newMode;
    *(int *)(rec + 0x9d0) = *(int *)(rec + 0x20);
}

// =============================================================================
// 0x802b90a4  aiactor_behavior_combat_tick
// The engage/combat animation FSM. rec[8] holds the combat sub-state:
//   0 idle-aim, 1 melee, 2 close strafe, 3 fire, 4 far strafe, 5 charge.
// Plays stance/fire/melee clips, applies melee splash, resolves take-cover,
// runs the visibility helper, and picks the next action code.
// =============================================================================
void aiactor_behavior_combat_tick(void)
{
    int actor = aiactor_current_frame_actor();
    int *rec  = *(int **)(actor + 0x7c);
    int hasTeammate = aiactor_retarget_visible_teammate();

    // consume any queued state
    if (-1 < rec[0x274]) {
        if (rec[0x274] != rec[8]) {
            rec[0x276] = (int)FLOAT_80619c4c;
            rec[0x275] = rec[8];
            rec[8]     = rec[0x274];
        }
        rec[0x274] = -1;
    }

    // aim timer decay
    if ((rec[0xb] & 0x80) == 0) {
        rec[0x2a4] = (int)FLOAT_80619c4c;
    } else {
        float frameDelta = FLOAT_80619c50 /
            (float)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80619c68);
        rec[0x2a4] = (int)(FLOAT_806108ec * frameDelta + (float)rec[0x2a4]);
    }

    unsigned int attack = 0;
    int navSel = (int)(unsigned char)(&DAT_8048ca8b)[*(short *)(rec + 0x12) * 0x30] * 300;

    aiactor_leader_zone_update(actor);
    aiactor_set_stance_flags(actor, 4);
    tracker_scan_class_flag400();

    // lethal-burn / forced-panic short circuit
    int burnLethal = character_burn_is_lethal((int)(rec + 0x31a));
    if (((rec[10] & 0x2000) != 0) || (burnLethal != 0 && 4 < rec[0x2c1])) {
        int target = rec[1];
        if (target == 0) {
            if (ai_queue_action((int)rec, 0x1000))
                goto exit;
        } else {
            float dx = *(float *)(actor + 100) - *(float *)(target + 100);
            float dz = *(float *)(actor + 0x6c) - *(float *)(target + 0x6c);
            if (FLOAT_80619c78 <= dx * dx + dz * dz) {
                if ((rec[0x19e] == rec[0x1a1]) || (aiactor_follow_zone_path() != 0)) {
                    if (anim_is_clip_active(actor, 0x26) == 0)
                        anim_play_scaled((double)FLOAT_80619c7c, (double)FLOAT_80619c80);
                } else {
                    int zone = *(int *)(*(int *)(rec[1] + 0x7c) + 0x678);
                    if (zone < 0)
                        rec[0x1a1] = *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                    else
                        rec[0x1a1] = zone;
                    rec[0x26b] = 0;
                    rec[0x26a] = 0;
                    if (anim_is_clip_active(actor, 0x26) == 0)
                        anim_play_scaled((double)FLOAT_80619c7c, (double)FLOAT_80619c80);
                }
            } else {
                tracker_play_melee_fidget_clip(actor);
            }
        }
        goto exit;
    }

    // panic / cover request
    if (rec[0x25] == 0x14) {
        if ((rec[0x11] == 0) &&
            (aiactor_get_step_offset() < (double)(float)rec[0x2d3])) {
            unsigned int flag = *(unsigned int *)(actor + 0x10) & 0xffff;
            if ((flag == 0) || (FLOAT_80619c84 < (float)rec[0x2bd]))
                rec[10] = rec[10] | 2;
        }
    } else if ((hasTeammate != 0) &&
               (((rec[0xaf] != 0 || rec[99] != 0) && (rec[0x11] == 0)) &&
                (aiactor_get_step_offset() < (double)(float)rec[0x2d3]))) {
        rec[10] = rec[10] | 2;
    }

    if ((rec[0xaf] == 0) && (rec[99] == 0)) {
        if (aiactor_request_take_cover((int)rec) != 0)
            goto exit;
        attack = 1;
    }
    if (attack == 0)
        attack = aiactor_roll_attack_chance((int)rec);

    // maybe force charge state
    {
        int obj;
        if (((rec[8] != 5) && (rec != (int *)0)) && ((obj = rec[0], obj != 0)) &&
            ((DAT_80612610 <= rec[2] &&
              (((*(unsigned int *)(obj + 0x28) & 0x100) != 0 ||
                ((*(unsigned int *)(obj + 0x10) & 0xffff) == 0)))))
            && (rec[0x25] != 0x36) && (rec[8] != 5)) {
            ai_set_substate(rec, 5);
        }
    }

    if (rec[2] == DAT_806120f8)
        aiactor_los_visibility_tick();

    double strafeTimer;
    if (rec[0x25] == 0x36)
        strafeTimer = (double)FLOAT_80619c80;
    else if (attack == 0)
        strafeTimer = (double)FLOAT_80619c8c;
    else
        strafeTimer = (double)FLOAT_80619c88;

    // aim origin: use own or target position depending on visibility
    {
        int obj = (rec == (int *)0) ? 0 : rec[0];
        int useTarget = 1;
        if ((rec == (int *)0) || (obj == 0) ||
            (rec[2] < DAT_80612610) ||
            (((*(unsigned int *)(obj + 0x28) & 0x100) == 0 &&
              ((*(unsigned int *)(obj + 0x10) & 0xffff) != 0))))
            useTarget = 0;
        if (useTarget) {
            rec[0xc] = *(int *)(actor + 100);
            rec[0xd] = *(int *)(actor + 0x68);
            rec[0xe] = *(int *)(actor + 0x6c);
        } else {
            rec[0xc] = *(int *)(rec[1] + 100);
            rec[0xd] = *(int *)(rec[1] + 0x68);
            rec[0xe] = *(int *)(rec[1] + 0x6c);
        }
    }

    int state = rec[8];
    if (state == 3) {
        // FIRE
        if (anim_primary_clip_time_left((double)FLOAT_80619c7c, *(int *)(actor + 4)) != 0) {
            if (attack == 0) {
                unsigned int r = rng_counter_tick();
                unsigned int mod = *(unsigned int *)(&DAT_80473a0c + navSel);
                int arg = *(int *)(&DAT_804739d0 + (r - (r / mod) * mod) * 4 + navSel + 0x40);
                anim_zone_path_step(actor, arg, (float *)0);
                anim_play_scaled((double)FLOAT_80619c38, (double)FLOAT_80619c90);
            } else {
                anim_play_scaled((double)FLOAT_80619c38, (double)FLOAT_80619c90);
            }
            float hi = (float)((double)FLOAT_80619cac + strafeTimer);
            if ((float)rec[0x2bd] <= hi * hi) {
                float lo = (float)(strafeTimer - (double)FLOAT_80619cac);
                if (((float)rec[0x2bd] < lo * lo) && (rec[8] != 4))
                    ai_set_substate(rec, 4);
            } else if (rec[8] != 2) {
                ai_set_substate(rec, 2);
            }
        }
    } else if (state < 3) {
        if (state == 1) {
            // MELEE
            if (anim_active_slot_userflags(*(int *)(actor + 4)) != 0) {
                if ((anim_is_clip_active(actor, 0x1e1) != 0) ||
                    (anim_is_clip_active(actor, 0x111) != 0)) {
                    int meleeEvent[16];
                    switch_slot_reset((int)&meleeEvent[0]);
                    meleeEvent[3] = 0;  // local_ac
                    meleeEvent[4] = 0;  // local_a8
                    float reach = FLOAT_80619c48;
                    meleeEvent[2] = actor; // local_b8 alias for event target chain
                    meleeEvent[1] = actor; // local_bc
                    if (FUN_80364290((int)rec, 0x80) != 0)
                        reach = reach * FLOAT_80619c9c;
                    (void)reach;
                    int idx = 0;
                    for (int i = 0; i < DAT_80612098; i = i + 1) {
                        int *other = *(int **)((int)&DAT_80588610 + idx);
                        if (other != rec) {
                            int oe = *other;
                            float dx = *(float *)(oe + 100) - *(float *)(actor + 100);
                            float dy = *(float *)(oe + 0x68) - *(float *)(actor + 0x68);
                            float dz = *(float *)(oe + 0x6c) - *(float *)(actor + 0x6c);
                            if ((dz * dz + dx * dx + dy * dy) <= FLOAT_80619ca0) {
                                if (aiactor_facing_dot_test((double)FLOAT_80619ca4, actor,
                                                            (float *)(oe + 100))) {
                                    meleeEvent[0] = *other;
                                    damage_dispatch_to_entity((double)FLOAT_80619ca4, 0, 0, 0, 0,
                                                              0, 0, 0, &meleeEvent[0]);
                                }
                            }
                        }
                        idx = idx + 4;
                    }
                }
                anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
            }
        } else if (state < 1) {
            if (-1 < state) {
                // IDLE / AIM (state 0)
                if ((anim_primary_clip_time_left((double)FLOAT_80619c7c, *(int *)(actor + 4)) != 0) ||
                    (anim_slot_is_primary_clip(actor, 0x25) != 0)) {
                    if (attack == 0) {
                        if (rec[0x25] == 0x36) {
                            anim_play_scaled((double)FLOAT_80619c38, (double)FLOAT_80619c90);
                            if (rec[8] != 1)
                                ai_set_substate(rec, 1);
                        } else {
                            rng_counter_tick();
                            anim_play_scaled((double)FLOAT_80619c38, (double)FLOAT_80619c90);
                            if (rec[8] != 3)
                                ai_set_substate(rec, 3);
                        }
                    } else {
                        anim_play_scaled((double)FLOAT_80619c38, (double)FLOAT_80619c90);
                        if (rec[8] != 3)
                            ai_set_substate(rec, 3);
                    }
                }
                rec[0x2cd] = (int)FLOAT_80619c4c;
                rec[0x278] = (int)FLOAT_80619c4c;
            }
        } else {
            // state 2: CLOSE STRAFE
            if (anim_primary_clip_time_left((double)FLOAT_80619c7c, *(int *)(actor + 4)) != 0) {
                int clip;
                if (attack == 0) {
                    unsigned int r = rng_counter_tick();
                    unsigned int mod = *(unsigned int *)(&DAT_80473a48 + navSel);
                    clip = *(int *)(&DAT_804739d0 + (r - (r / mod) * mod) * 4 + navSel + 0x7c);
                } else {
                    clip = 0x1e4;
                }
                if ((anim_is_clip_active(actor, clip) == 0) ||
                    ((*(unsigned int *)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[clip] * 8) & 1) == 0))
                    anim_play_scaled((double)FLOAT_80619c38, (double)FLOAT_80619c90);
                if (((float)rec[0x2bd] < (float)(strafeTimer * strafeTimer)) && (rec[8] != 3))
                    ai_set_substate(rec, 3);
            }
        }
    } else if (state == 5) {
        // CHARGE
        int obj = (rec == (int *)0) ? 0 : rec[0];
        if ((rec == (int *)0) || (obj == 0) || (rec[2] < DAT_80612610) ||
            (((*(unsigned int *)(obj + 0x28) & 0x100) == 0 &&
              ((*(unsigned int *)(obj + 0x10) & 0xffff) != 0)))) {
            rec[0x275] = 5;
            rec[0x276] = (int)FLOAT_80619c4c;
            rec[8]     = 3;
            rec[0x274] = rec[8];
        }
        if (anim_is_clip_active(actor, 0x25) == 0)
            anim_play_scaled((double)FLOAT_80619c38, (double)FLOAT_80619c90);

        if (((attack != 0) && (rec[1] != 0)) && ((float)rec[0x278] < (float)rec[0x2cd])) {
            if ((*(char *)(DAT_80611b1c + *(int *)(actor + 0x34)) == '\0') &&
                (*(char *)(DAT_80611b1c + *(int *)(rec[1] + 0x34)) == '\0')) {
                int meleeEvent[16];
                switch_slot_reset((int)&meleeEvent[0]);
                meleeEvent[9]  = 0x17; // local_104
                meleeEvent[10] = 0x17; // local_108
                meleeEvent[8]  = 1;    // local_dc
                int tgt = rec[1];
                float tx = *(float *)(tgt + 100);
                float ty = *(float *)(tgt + 0x68);
                float tz = *(float *)(tgt + 0x6c);
                meleeEvent[1] = actor;
                meleeEvent[2] = actor;
                meleeEvent[3] = actor;
                meleeEvent[4] = (int)rec;
                int idx = 0;
                for (int i = 0; i < DAT_80612098; i = i + 1) {
                    int *other = *(int **)((int)&DAT_80588610 + idx);
                    if ((rec != other) && ((other[9] & 8) == 0)) {
                        int oe = *other;
                        float dx = (float)((double)*(float *)(oe + 100) - (double)tx);
                        float dy = (float)((double)*(float *)(oe + 0x68) - (double)ty);
                        float dz = (float)((double)*(float *)(oe + 0x6c) - (double)tz);
                        double dist = math_sqrt((double)(dz * dz + dx * dx + dy * dy));
                        double falloff = (double)FLOAT_80619c50 -
                            (double)(((float)dist / FLOAT_80619cac) * ((float)dist / FLOAT_80619cac));
                        double clamped = falloff;
                        if ((double)FLOAT_80619c50 < falloff)
                            clamped = (double)FLOAT_80619c50;
                        if ((double)FLOAT_80619c4c <= clamped) {
                            if ((double)FLOAT_80619c50 < falloff)
                                clamped = (double)FLOAT_80619c50;
                            else
                                clamped = falloff;
                        } else {
                            clamped = (double)FLOAT_80619c4c;
                        }
                        float amount = (float)((double)FLOAT_80619cb0 * clamped);
                        if ((double)FLOAT_80619c4c != (double)amount) {
                            meleeEvent[0] = *other;
                            if (other[0x21] == rec[0x21])
                                amount = (float)((double)amount * (double)FLOAT_80619c48);
                            (void)amount;
                            damage_dispatch_to_entity((double)amount, falloff, clamped,
                                                      0, 0, 0, 0, 0, &meleeEvent[0]);
                        }
                    }
                    idx = idx + 4;
                }
            }
            unsigned int r = rng_counter_tick();
            rec[0x278] = (int)((float)rec[0x2cd] + FLOAT_80619c9c +
                (float)((double)(unsigned int)r - DOUBLE_80619c40) / FLOAT_80619c34);
        }
    } else if ((state < 5) &&
               (anim_primary_clip_time_left((double)FLOAT_80619c7c, *(int *)(actor + 4)) != 0)) {
        // state 4: FAR STRAFE
        int clip;
        if (attack == 0) {
            unsigned int r = rng_counter_tick();
            unsigned int mod = *(unsigned int *)(&DAT_80473a84 + navSel);
            clip = *(int *)(&DAT_804739d0 + (r - (r / mod) * mod) * 4 + navSel + 0xb8);
        } else {
            clip = 0x1e5;
        }
        if ((anim_is_clip_active(actor, clip) == 0) ||
            ((*(unsigned int *)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[clip] * 8) & 1) == 0))
            anim_play_scaled((double)FLOAT_80619c38, (double)FLOAT_80619c90);
        if (((float)(strafeTimer * strafeTimer) < (float)rec[0x2bd]) && (rec[8] != 3))
            ai_set_substate(rec, 3);
    }

    if ((rec[2] == DAT_806120f8) && (FLOAT_80619c9c < (float)rec[0x2cd]))
        aiactor_retarget_scan();

    // aim + refill fire timer if a fire clip is active and an event fired
    if ((((anim_slot_is_primary_clip(actor, 0x1e5) != 0) ||
          (anim_slot_is_primary_clip(actor, 0x1e3) != 0)) ||
         (anim_slot_is_primary_clip(actor, 0x1e4) != 0)) &&
        ((anim_event_scan_active() != 0) && (rec[8] != 5))) {
        aiactor_aim_at_target(actor, 1, 0);
        unsigned int r = rng_counter_tick();
        rec[0x278] = (int)((float)rec[0x2cd] + FLOAT_80619c9c +
            (float)((double)(unsigned int)r - DOUBLE_80619c40) / FLOAT_80619c34);
    }

    // out-of-target-visibility fallback action selection
    if ((rec[1] == 0) || ((*(unsigned int *)(*(int *)(rec[1] + 0x7c) + 0x24) & 8) != 0)) {
        if (DAT_805015bd == 7) {
            if (ai_queue_action((int)rec, 0x10))
                goto exit;
            goto exit;
        }
        if (DAT_805015bd < 7) {
            if ((DAT_805015bd < 4) && (1 < DAT_805015bd)) {
                if (ai_queue_action((int)rec, 0x10))
                    goto exit;
                goto exit;
            }
        } else if (DAT_805015bd == 9) {
            unsigned int r = rng_counter_tick();
            if (((float)((double)(unsigned int)r - DOUBLE_80619c40) / FLOAT_80619c34 < FLOAT_80619cb4) &&
                ((rec[0xaf] != 0 || rec[99] != 0))) {
                rec[700] = 0;
                if (ai_queue_action((int)rec, 0x40000))
                    goto exit;
                goto exit;
            }
        }
        if ((DAT_805015be != 0x12) || (rec[1] != DAT_80611910)) {
            if (ai_queue_action((int)rec, 0x10))
                goto exit;
            goto exit;
        }
    }

    if (hasTeammate == 0) {
        if (FLOAT_80619c9c < (float)rec[0x2cc]) {
            if (ai_queue_action((int)rec, 0x10))
                goto exit;
        }
    } else {
        rec[0x2cc] = (int)FLOAT_80619c4c;
    }

exit:
    return;
}

// =============================================================================
// 0x802ba204  aiactor_los_visibility_tick
// Round-robin per-actor line-of-sight sweep. Raycasts from the scan cursor
// actor (DAT_806120f8) toward its current target and toward the closest
// unseen hostile, then updates the shared relation-visibility flags.
// =============================================================================
void aiactor_los_visibility_tick(void)
{
    int hitTarget = 0;
    int targetOwnerId = 0;
    int *rec = (int *)(DAT_80611d90 + DAT_806120f8 * 0xea0);
    if (*rec == 0)
        rec = (int *)0;

    if ((rec != (int *)0) && (rec[1] != 0)) {
        unsigned int savedFlags = *(unsigned int *)(*rec + 0x28);
        *(unsigned int *)(*rec + 0x28) = savedFlags & 0xffffff9f;

        float origin[3];
        origin[0] = (float)rec[0x2ed];
        origin[1] = (float)rec[0x2ee];
        origin[2] = (float)rec[0x2ef];
        int target = rec[1];
        int cls = *(int *)(target + 0x20);
        float dir[3];
        float hitPos[3];
        if (cls == 0x200) {
            dir[0] = (*(float *)(target + 100) + *(float *)(target + 0x1a8)) - origin[0];
            dir[1] = (*(float *)(target + 0x68) + *(float *)(target + 0x1ac)) - origin[1];
            dir[2] = (*(float *)(target + 0x6c) + *(float *)(target + 0x1b0)) - origin[2];
            hitTarget = scene_raycast(*(int *)(*rec + 0x34), origin, dir, 0, hitPos,
                                      (float *)0, (int *)0, 0x2000070, 0, (int *)0);
            hitPos[0] = hitPos[0] - origin[0];
            hitPos[1] = hitPos[1] - origin[1];
            hitPos[2] = hitPos[2] - origin[2];
            if ((hitTarget == 0) ||
                (dir[2] * dir[2] + dir[0] * dir[0] + dir[1] * dir[1] <
                 hitPos[2] * hitPos[2] + hitPos[0] * hitPos[0] + hitPos[1] * hitPos[1]))
                hitTarget = rec[1];
        } else if (cls < 0x200) {
            if (cls == 8) {
                dir[0] = *(float *)(target + 100) - origin[0];
                targetOwnerId = *(int *)(*(int *)(target + 0x7c) + 8);
                dir[1] = (*(float *)(*(int *)(target + 0x7c) + 3000) - FLOAT_80619cb4) - origin[1];
                dir[2] = *(float *)(target + 0x6c) - origin[2];
                hitTarget = scene_raycast(*(int *)(*rec + 0x34), origin, dir, 0, (float *)0,
                                          (float *)0, (int *)0, 0x2010270, 1, (int *)0);
            } else if (cls < 8) {
                if (cls == 1) {
                    dir[0] = (*(float *)(target + 100) + *(float *)(target + 0x1a8)) - origin[0];
                    int t = rec[1];
                    dir[1] = (*(float *)(t + 0x68) + *(float *)(t + 0x1ac)) - origin[1];
                    dir[2] = (*(float *)(t + 0x6c) + *(float *)(t + 0x1b0)) - origin[2];
                    hitTarget = scene_raycast(*(int *)(*rec + 0x34), origin, dir, 0, (float *)0,
                                              (float *)0, (int *)0, 0x2000070, 0, (int *)0);
                }
            } else if (cls == 0x80) {
                dir[0] = (*(float *)(target + 100) + *(float *)(target + 0x1a8)) - origin[0];
                int t = rec[1];
                dir[1] = (*(float *)(t + 0x68) + *(float *)(t + 0x1ac)) - origin[1];
                dir[2] = (*(float *)(t + 0x6c) + *(float *)(t + 0x1b0)) - origin[2];
                hitTarget = scene_raycast(*(int *)(*rec + 0x34), origin, dir, 0, (float *)0,
                                          (float *)0, (int *)0, 0x2000070, 0, (int *)0);
            }
        } else {
            if (cls == 0x8000000) {
                dir[0] = (*(float *)(target + 100) + *(float *)(target + 0x1a8)) - origin[0];
                int t = rec[1];
                dir[1] = (*(float *)(t + 0x68) + *(float *)(t + 0x1ac)) - origin[1];
                dir[2] = (*(float *)(t + 0x6c) + *(float *)(t + 0x1b0)) - origin[2];
                hitTarget = scene_raycast(*(int *)(*rec + 0x34), origin, dir, 0, (float *)0,
                                          (float *)0, (int *)0, 0x2000070, 0, (int *)0);
            } else if ((cls < 0x8000000) && (cls == 0x2000)) {
                dir[0] = (*(float *)(target + 100) + *(float *)(target + 0x1a8)) - origin[0];
                dir[1] = (FLOAT_80619cb8 + *(float *)(target + 0x68)) - origin[1];
                dir[2] = (*(float *)(target + 0x6c) + *(float *)(target + 0x1b0)) - origin[2];
                hitTarget = scene_raycast(*(int *)(*rec + 0x34), origin, dir, 0, (float *)0,
                                          (float *)0, (int *)0, 0x2000070, 0, (int *)0);
            }
        }

        if (hitTarget == 0) {
            rec[0x2b7] = 0;
            if (*(int *)(rec[1] + 0x20) == 8)
                aiactor_set_relation_visible(DAT_806120f8, targetOwnerId, 0);
            if (FLOAT_80619c50 < (float)rec[0x2d3])
                rec[0x2d3] = (int)FLOAT_80619c4c;
        } else {
            if (*(int *)(hitTarget + 0x20) == 8)
                aiactor_set_relation_visible(DAT_806120f8,
                                             *(int *)(*(int *)(hitTarget + 0x7c) + 8), 1);
            if (hitTarget == rec[1]) {
                rec[0x2b7] = 1;
            } else {
                rec[0x2b7] = 0;
                if (*(int *)(rec[1] + 0x20) == 8)
                    aiactor_set_relation_visible(DAT_806120f8, targetOwnerId, 0);
            }
        }
        *(unsigned int *)(*rec + 0x28) = savedFlags;
    }

    // closest unseen hostile
    int bestRel = 0xfffffff;
    int bestActor = -1;
    if (rec != (int *)0) {
        int idx = 0;
        for (int i = 0; i < DAT_80612098; i = i + 1) {
            int *other = *(int **)((int)&DAT_80588610 + idx);
            if (((other[2] != DAT_806120f8) && (*other != rec[1])) &&
                (*(int *)(aiactor_relation_entry(other[2], DAT_806120f8) + 4) < bestRel)) {
                int e = aiactor_relation_entry(other[2], DAT_806120f8);
                bestRel = *(int *)(e + 4);
                bestActor = other[2];
            }
            idx = idx + 4;
        }
    }

    float eyeHeight = FLOAT_80619cb4;
    if (bestActor != -1) {
        unsigned int savedFlags = *(unsigned int *)(*rec + 0x28);
        int tgt = *(int *)(DAT_80611d90 + bestActor * 0xea0);
        *(unsigned int *)(*rec + 0x28) = savedFlags & 0xffffff9f;
        float origin[3];
        origin[0] = (float)rec[0x2ed];
        origin[1] = (float)rec[0x2ee];
        origin[2] = (float)rec[0x2ef];
        float dir[3];
        dir[0] = *(float *)(tgt + 100) - origin[0];
        dir[1] = (*(float *)(*(int *)(tgt + 0x7c) + 3000) - eyeHeight) - origin[1];
        dir[2] = *(float *)(tgt + 0x6c) - origin[2];
        int hit = scene_raycast(*(int *)(*rec + 0x34), origin, dir, 0, (float *)0,
                                (float *)0, (int *)0, 0x2010070, 1, (int *)0);
        if (hit == tgt) {
            aiactor_set_relation_visible(DAT_806120f8, bestActor, 1);
        } else if ((hit == 0) || (*(int *)(hit + 0x20) != 8)) {
            aiactor_set_relation_visible(DAT_806120f8, bestActor, 0);
        } else {
            aiactor_set_relation_visible(DAT_806120f8, *(int *)(*(int *)(hit + 0x7c) + 8), 1);
            aiactor_set_relation_visible(DAT_806120f8, bestActor, 0);
        }
        *(unsigned int *)(*rec + 0x28) = savedFlags;
    }

    // advance round-robin cursor
    DAT_806120f8 = DAT_806120f8 + 1;
    if (DAT_80612610 + DAT_806120fc <= DAT_806120f8)
        DAT_806120f8 = DAT_80612610;
}

// =============================================================================
// 0x802ba7e4  aiactor_pick_best_weapon  (was FUN_802ba7e4)
// Scan the actor's owned-weapon list and switch to the highest-priority slot
// that is loaded / available for the current ammo band.
// =============================================================================
void aiactor_pick_best_weapon(void)
{
    int actor = aiactor_current_frame_actor();
    int bestPriority = 100;
    int object = *(int *)(actor + 0x7c);
    int weaponCount = FUN_802bc780(actor);
    int bestSlot = -1;
    int list = match_build_weapon_list();

    if (((*(int *)(object + 4) != 0) && (1 < weaponCount)) && (*(int *)(object + 0x94) != 0)) {
        if (*(float *)(object + 0xaf4) <= FLOAT_80619cbc) {
            if ((*(float *)(object + 0xaf4) < FLOAT_80619cc0) &&
                (*(int *)(&DAT_804308b8 + *(int *)(object + 0x204) * 0x24) == 1)) {
                int off = 0;
                for (int i = 0; i < *(int *)(list + 4); i = i + 1) {
                    int slot = *(int *)(list + off + 8);
                    if ((*(char *)(object + slot + 0x54c) != '\0') &&
                        ((int)(&DAT_8043e234)[slot * 0xf] < bestPriority)) {
                        int cat = *(int *)(&DAT_804308b8 + (&DAT_8043e23c)[slot * 0xf] * 0x24);
                        int applies = 0;
                        if (cat == 1) applies = 0;
                        else if (cat < 1) { if (-1 < cat) applies = 1; }
                        else if (cat < 3) applies = 1;
                        if (applies) {
                            int ok = object_slot_apply_params(object, slot, 0);
                            if (ok != 0) {
                                bestSlot = *(int *)(list + off + 8);
                                bestPriority = (&DAT_8043e234)[bestSlot * 0xf];
                            }
                        }
                    }
                    off = off + 4;
                }
                if (0 < bestSlot) {
                    anim_set_playback_rate((double)FLOAT_80619c5c, *(int **)(actor + 4), 2);
                    *(int *)(object + 0x94) = bestSlot;
                }
            }
        } else if (*(int *)(&DAT_804308b8 + *(int *)(object + 0x204) * 0x24) == 0) {
            int off = 0;
            for (int i = 0; i < *(int *)(list + 4); i = i + 1) {
                int slot = *(int *)(list + off + 8);
                if (((*(char *)(object + slot + 0x54c) != '\0') &&
                     ((int)(&DAT_8043e234)[slot * 0xf] < bestPriority)) &&
                    (*(int *)(&DAT_804308b8 + (&DAT_8043e23c)[slot * 0xf] * 0x24) < 3) &&
                    ((0 < *(int *)(&DAT_804308b8 + (&DAT_8043e23c)[slot * 0xf] * 0x24) &&
                      (object_slot_apply_params(object, slot, 0) != 0)))) {
                    bestSlot = *(int *)(list + off + 8);
                    bestPriority = (&DAT_8043e234)[bestSlot * 0xf];
                }
                off = off + 4;
            }
            if (0 < bestSlot) {
                anim_set_playback_rate((double)FLOAT_80619c5c, *(int **)(actor + 4), 2);
                *(int *)(object + 0x94) = bestSlot;
            }
        }
    }
}

// =============================================================================
// 0x802baa58  aiactor_behavior_mode_tick
// Top-level behaviour-mode FSM. rec.mode (rec+0x20) is 0..4:
//   0 idle/select, 1 goto-zone, 2 patrol, 3 approach-sound-pad, 4 combat.
// Selects the next mode based on target availability, sound-pad zone graph,
// and the level game-mode selector.
// =============================================================================
void aiactor_behavior_mode_tick(void)
{
    int actor = aiactor_current_frame_actor();
    int rec = *(int *)(actor + 0x7c);
    int hasTeammate = aiactor_retarget_visible_teammate();

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

    if ((hasTeammate != 0) || (FLOAT_80619c64 <= *(float *)(rec + 0xb34))) {
        if (hasTeammate != 0)
            *(float *)(rec + 0xb34) = FLOAT_80619c4c;
    } else {
        hasTeammate = 1;
    }

    if (((*(unsigned int *)(rec + 0x28) & 8) != 0) && (*(int *)(rec + 0x20) != 4))
        ai_set_mode(rec, 4);

    if (*(int *)(rec + 8) == DAT_806120f8) {
        aiactor_retarget_scan();
        if ((((*(int *)(rec + 4) != 0) && (*(int *)(*(int *)(rec + 4) + 0x20) == 8)) &&
             (aiactor_retarget_visible_teammate() != 0)) &&
            (*(float *)(rec + 0xaf4) < FLOAT_80619cc4)) {
            if ((tracker_scan_class_flag400() != 0) && (*(int *)(rec + 0x20) != 4))
                ai_set_mode(rec, 4);

            if ((*(int *)(rec + 700) != 0) || (*(int *)(rec + 0x18c) != 0)) {
                int band = 1;
                if (*(int *)(rec + 0xb04) < 6)
                    band = 3;
                int myPrio = *(int *)(&DAT_8043e230 + *(int *)(rec + 0x94) * 0x3c);
                int tgtPrio = *(int *)(&DAT_8043e230 +
                    *(int *)(*(int *)(*(int *)(rec + 4) + 0x7c) + 0x94) * 0x3c);
                if (tgtPrio < myPrio - band) {
                    if ((FLOAT_80619c98 * *(float *)(*(int *)(rec + 4) + 0x1cc) <
                         *(float *)(actor + 0x1cc)) && (*(int *)(rec + 0x20) != 4))
                        ai_set_mode(rec, 4);
                } else if (*(int *)(rec + 0x20) != 4) {
                    ai_set_mode(rec, 4);
                }
            }
        }
    }

    if ((*(unsigned int *)(rec + 0x28) & 1) == 0) {
        int mode = *(int *)(rec + 0x20);
        if (mode == 2) {
            unsigned int active = aiactor_get_active_index();
            if ((int)active < 1) {
                if (ai_queue_action(rec, 0x10))
                    goto exit;
            } else {
                unsigned int r = rng_counter_tick();
                int *slot = aiactor_get_scratch_slot(r - (r / active) * active);
                int val = *slot;
                bool isSoundPad = (7999 < val) && (val < 0x209e);
                if (isSoundPad) {
                    int zone = soundpad_find_zone_by_id(
                        (int)*(short *)(DAT_80611b94 + val * 0x20 + -0x3e7f8));
                    if (zone < 0)
                        *(int *)(rec + 0x684) =
                            *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                    else
                        *(int *)(rec + 0x684) = soundpad_find_zone_by_id(
                            (int)*(short *)(DAT_80611b94 + *slot * 0x20 + -0x3e7f8));
                    *(int *)(rec + 0x9ac) = 0;
                    *(int *)(rec + 0x9a8) = 0;
                } else {
                    if (val < 0)
                        *(int *)(rec + 0x684) =
                            *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                    else
                        *(int *)(rec + 0x684) = val;
                    *(int *)(rec + 0x9ac) = 0;
                    *(int *)(rec + 0x9a8) = 0;
                }
                if (-1 < *(int *)(rec + 0x684)) {
                    aiactor_set_stance_flags(actor, 1);
                    if (*(int *)(rec + 0x20) != 3)
                        ai_set_mode(rec, 3);
                }
            }
        } else if (mode < 2) {
            if (mode == 0) {
                int follow = (int)aiactor_scan_soundpad_hostile_ret();
                *(int *)(rec + 0xaf0) = follow;
                if (*(int *)(rec + 0xaf0) == 0) {
                    if (*(int *)(rec + 0x94) == 0) {
                        if (*(int *)(rec + 0x20) != 2)
                            ai_set_mode(rec, 2);
                    } else if (*(int *)(rec + 0x20) != 4) {
                        ai_set_mode(rec, 4);
                    }
                } else {
                    int zone = *(int *)(*(int *)(*(int *)(rec + 0xaf0) + 0x7c) + 0x38);
                    if (zone < 0) {
                        if (*(int *)(rec + 0x94) == 0) {
                            if (*(int *)(rec + 0x20) != 2)
                                ai_set_mode(rec, 2);
                        } else if (*(int *)(rec + 0x20) != 4) {
                            ai_set_mode(rec, 4);
                        }
                    } else {
                        *(int *)(rec + 0x684) = zone;
                        *(int *)(rec + 0x9ac) = 0;
                        *(int *)(rec + 0x9a8) = 0;
                        if (*(int *)(rec + 0x20) != 1)
                            ai_set_mode(rec, 1);
                    }
                }
            } else if (-1 < mode) {
                int followTarget = *(int *)(rec + 0xaf0);
                if (((followTarget == 0) || (*(int *)(followTarget + 0x7c) == 0)) ||
                    ((*(unsigned int *)(followTarget + 0x28) & 1) == 0) ||
                    ((*(unsigned int *)(followTarget + 0x28) & 0x80000000) != 0)) {
                    *(int *)(rec + 0xaf0) = 0;
                    if (*(int *)(rec + 0x20) != 0)
                        ai_set_mode(rec, 0);
                } else {
                    if (aiactor_follow_zone_path() == 0) {
                        if (*(int *)(rec + 8) == DAT_806120f8)
                            aiactor_retarget_scan();
                        if ((*(int *)(rec + 4) == 0) ||
                            (!aiactor_aim_alignment_test((double)*(float *)(rec + 0x9bc), actor,
                                                         (float *)(*(int *)(rec + 4) + 100)))) {
                            *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) | 0x40000;
                        } else if (hasTeammate != 0) {
                            *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) | 2;
                            *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) & 0xfffbffff;
                        }
                        if ((*(int *)(rec + 0x94) != 0) &&
                            (((*(int **)(actor + 4))[0x32] & 1) != 0))
                            anim_set_playback_rate((double)FLOAT_80619c58, *(int **)(actor + 4), 2);
                    } else {
                        aiactor_leader_zone_update(actor);
                        if (*(int *)(rec + 0x20) != 0)
                            ai_set_mode(rec, 0);
                    }
                }
            }
        } else if (mode == 4) {
            *(int *)(rec + 0xaf0) = 0;
            if (DAT_805015bd == '\x05') {
                if ((DAT_806120d8 & 1 << *(int *)(rec + 8)) == 0) {
                    if ((*(int *)(rec + 0xaac) != 1 || *(int *)(rec + 0xaa8) != 0) ||
                        ((*(unsigned int *)(rec + 0x1c) & 1) != 0)) {
                        *(int *)(rec + 0xaac) = 0x800;
                        *(int *)(rec + 0xaa8) = 0;
                    }
                } else if ((*(int *)(rec + 0xaac) != 1 || *(int *)(rec + 0xaa8) != 0) ||
                           ((*(unsigned int *)(rec + 0x1c) & 1) != 0)) {
                    *(int *)(rec + 0xaac) = 0x2000;
                    *(int *)(rec + 0xaa8) = 0;
                }
            } else if (DAT_805015bd == '\x03') {
                if ((*(int *)(rec + 0xaac) != 1 || *(int *)(rec + 0xaa8) != 0) ||
                    ((*(unsigned int *)(rec + 0x1c) & 1) != 0)) {
                    *(int *)(rec + 0xaac) = 0x10;
                    *(int *)(rec + 0xaa8) = 0;
                }
            } else if (DAT_805015bd == '\x02') {
                if ((*(int *)(rec + 0xaac) != 1 || *(int *)(rec + 0xaa8) != 0) ||
                    ((*(unsigned int *)(rec + 0x1c) & 1) != 0)) {
                    *(int *)(rec + 0xaac) = 0x10;
                    *(int *)(rec + 0xaa8) = 0;
                }
            } else if (DAT_805015bd == '\x06') {
                if ((*(int *)(rec + 0xaac) != 1 || *(int *)(rec + 0xaa8) != 0) ||
                    ((*(unsigned int *)(rec + 0x1c) & 1) != 0)) {
                    *(int *)(rec + 0xaac) = 0x4000;
                    *(int *)(rec + 0xaa8) = 0;
                }
            } else {
                if (ai_queue_action(rec, 0x10))
                    goto exit;
            }
        } else if ((mode < 4) && (aiactor_follow_zone_path() != 0)) {
            if (aiactor_all_zone_targets_inactive() == 0) {
                if (*(int *)(rec + 0x20) != 0)
                    ai_set_mode(rec, 0);
            } else {
                if (ai_queue_action(rec, 0x10))
                    goto exit;
            }
        }
    } else {
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
        if (ai_queue_action_secondary(rec, 0x20))
            goto exit;
    }

exit:
    return;
}

// The mode tick uses the same latch idiom but on the *secondary* action fields
// only for the final else-branch (writes 0x20). All other sites write the
// primary action word; ai_queue_action() above operates on the primary fields,
// which matches every primary-write site in the original. The single secondary
// write is handled here.
static int ai_queue_action_secondary(int rec, int code)
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
// 0x802bb580  aiactor_cover_fail_target8
// Fallback when a cover/zone request against a class-8 target fails: mark the
// "cannot advance" flag and queue the default action (or a secondary code when
// the actor is already engaged).
// =============================================================================
void aiactor_cover_fail_target8(int actor)
{
    int rec = *(int *)(actor + 0x7c);
    *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) | 0x40000;
    if ((*(unsigned int *)(rec + 0x28) & 4) == 0) {
        if ((*(unsigned int *)(rec + 0x28) & 1) == 0) {
            int follow = *(int *)(rec + 0xaf0);
            if ((follow != 0) && (*(int *)(*(int *)(follow + 0x7c) + 0xc) == 8)) {
                if ((*(unsigned int *)(follow + 0x28) & 1) != 0) {
                    if (aiactor_follow_zone_path() == 0)
                        return;
                    ai_queue_action(rec, 0x10);
                    return;
                }
                *(int *)(rec + 0xaf0) = 0;
                ai_queue_action(rec, 0x10);
                return;
            }
            ai_queue_action(rec, 0x10);
        } else {
            *(int *)(rec + 0xabc) = 0x400000;
            *(int *)(rec + 0xab8) = 0;
            ai_queue_action_secondary(rec, 0x20);
        }
    } else {
        *(int *)(rec + 0xaf0) = 0;
        ai_queue_action(rec, 0x10);
    }
}

// =============================================================================
// 0x802bb87c  aiactor_cover_fail_target16
// Twin of aiactor_cover_fail_target8 for class-0x10 targets (checks the 0x10
// engaged flag and the 0x10 target class code).
// =============================================================================
void aiactor_cover_fail_target16(int actor)
{
    int rec = *(int *)(actor + 0x7c);
    *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) | 0x40000;
    if ((*(unsigned int *)(rec + 0x28) & 0x10) == 0) {
        if ((*(unsigned int *)(rec + 0x28) & 1) == 0) {
            int follow = *(int *)(rec + 0xaf0);
            if ((follow != 0) && (*(int *)(*(int *)(follow + 0x7c) + 0xc) == 0x10)) {
                if ((*(unsigned int *)(follow + 0x28) & 1) != 0) {
                    if (aiactor_follow_zone_path() == 0)
                        return;
                    ai_queue_action(rec, 0x10);
                    return;
                }
                *(int *)(rec + 0xaf0) = 0;
                ai_queue_action(rec, 0x10);
                return;
            }
            ai_queue_action(rec, 0x10);
        } else {
            *(int *)(rec + 0xabc) = 0x800000;
            *(int *)(rec + 0xab8) = 0;
            ai_queue_action_secondary(rec, 0x20);
        }
    } else {
        *(int *)(rec + 0xaf0) = 0;
        ai_queue_action(rec, 0x10);
    }
}

// =============================================================================
// 0x802bbb78  aiactor_target_candidate_valid
// Predicate: is `candidate` a legitimate hostile the actor may pursue via the
// sound-pad zone graph? Rejects dead/hidden candidates, same-slot self-targets,
// and zones the actor is already close to.
// =============================================================================
int aiactor_target_candidate_valid(int actor, int candidate)
{
    int object = *(int *)(actor + 0x7c);
    if ((candidate == 0) || ((*(unsigned int *)(candidate + 0x28) & 1) == 0))
        return 0;

    if ((*(int *)(object + 0x94) == *(int *)(*(int *)(candidate + 0x7c) + 0x14)) &&
        (object_sound_slot_has_capacity(object, *(int *)(object + 0x94)) != 0))
        return 0;

    int zone = *(int *)(*(int *)(candidate + 0x7c) + 0x38);
    if ((*(int *)(object + 0x678) == zone) && (*(float *)(object + 0x9b0) < FLOAT_80619cc8))
        return 0;

    int *node = soundpad_resolve_zone_node(zone);
    if (((((unsigned int)node[1] >> 0xb & 1) == 0) && ((node[1] & 0x400000) == 0)) ||
        ((int)*(short *)(node + 2) != *(int *)(DAT_80611b8c + *(int *)(object + 0x678) * 0x20)) ||
        (FLOAT_80619cc8 <= *(float *)(object + 0x9b0)))
        return 1;
    return 0;
}

// =============================================================================
// 0x802bbc6c  aiactor_scan_soundpad_hostile
// Nearest valid-hostile scan over the actor's current + adjacent sound-pad
// zones. Returns the chosen candidate entity (0 if none). The original returns
// via a register pair; exposed here through aiactor_scan_soundpad_hostile_ret().
// =============================================================================
static int g_soundpad_scan_result;

int aiactor_scan_soundpad_hostile_ret(void)
{
    aiactor_scan_soundpad_hostile();
    return g_soundpad_scan_result;
}

void aiactor_scan_soundpad_hostile(void)
{
    int actor = aiactor_current_frame_actor();
    int rec = *(int *)(actor + 0x7c);
    double bestDist = (double)FLOAT_80619c30;
    int best = 0;
    int bestPrio = 0x32;
    int zoneBase = DAT_80611b8c + *(int *)(rec + 0x678) * 0x20;

    int off = 0;
    for (int i = 0; i < 3; i = i + 1) {
        int cand = *(int *)(DAT_80611b9c + *(int *)(rec + 0x678) * 0x2c + off + 0x20);
        int candSelf = (cand != 0) ? *(int *)(*(int *)(cand + 0x7c) + 0x14) : actor;
        if ((cand != 0) && (candSelf != actor) &&
            (aiactor_target_candidate_valid(actor, cand) != 0)) {
            int prio = *(int *)(&DAT_8043e230 + candSelf * 0x3c);
            if (prio < bestPrio) {
                float dx = *(float *)(cand + 100) - *(float *)(actor + 100);
                float dy = *(float *)(cand + 0x68) - *(float *)(actor + 0x68);
                float dz = *(float *)(cand + 0x6c) - *(float *)(actor + 0x6c);
                bestPrio = prio;
                best = cand;
                bestDist = (double)(dz * dz + dx * dx + dy * dy);
            } else if (prio == bestPrio) {
                float dx = *(float *)(cand + 100) - *(float *)(actor + 100);
                float dy = *(float *)(cand + 0x68) - *(float *)(actor + 0x68);
                float dz = *(float *)(cand + 0x6c) - *(float *)(actor + 0x6c);
                double d = (double)(dz * dz + dx * dx + dy * dy);
                if (d < bestDist) {
                    best = cand;
                    bestDist = d;
                }
            }
        }
        off = off + 4;
    }

    if (best != 0) {
        int *node = soundpad_resolve_zone_node(*(int *)(*(int *)(best + 0x7c) + 0x38));
        int zid = soundpad_get_entity_zone_id((int)node);
        if (*(short *)(zoneBase + 8) == zid) {
            g_soundpad_scan_result = best;
            return;
        }
    }

    // widen search to adjacent-actor list matching the same zone id
    best = 0;
    off = 0;
    for (int i = 0; i < 0x14; i = i + 1) {
        int cand = *(int *)((int)&DAT_80588690 + off);
        if (cand != 0) {
            int *node = soundpad_resolve_zone_node(*(int *)(*(int *)(cand + 0x7c) + 0x38));
            int zid = soundpad_get_entity_zone_id((int)node);
            if ((*(short *)(zoneBase + 8) == zid) &&
                (*(int *)(*(int *)(cand + 0x7c) + 0x14) != actor) &&
                (aiactor_target_candidate_valid(actor, cand) != 0)) {
                best = cand;
                break;
            }
        }
        off = off + 4;
    }
    g_soundpad_scan_result = best;
}

// =============================================================================
// 0x802bbe8c  aiactor_consider_leader_follow
// If the actor is idle and a squad leader occupying the first zone slot is in
// range, adopt it as the follow target (rec+0xaf0).
// =============================================================================
void aiactor_consider_leader_follow(void)
{
    int actor = aiactor_current_frame_actor();
    int rec = *(int *)(actor + 0x7c);
    int leader = *(int *)(DAT_80611b9c + *(int *)(rec + 0x678) * 0x2c);
    if ((*(int *)(rec + 0xb04) < 0xb) && (DAT_805015bd != '\n') && (leader != 0) &&
        (*(int *)(rec + 0x44) == 0) && (tracker_event_state_is_idle(rec) != 0) &&
        (*(float *)(actor + 0x1cc) <= FLOAT_80619c64 * *(float *)(actor + 0x1d8))) {
        int target = *(int *)(rec + 4);
        int targetVisible = (target == 0) || (*(int *)(target + 0x20) != 8);
        int targetGate = (*(int *)(rec + 0x1c) == 4 && *(int *)(rec + 0x18) == 0 &&
                          (FLOAT_80619cb4 + *(float *)(actor + 0x1cc) <= *(float *)(target + 0x1cc)));
        if ((targetVisible || targetGate) &&
            ((*(unsigned int *)(leader + 0x28) & 1) != 0) &&
            (*(float *)(DAT_80611b9c + *(int *)(rec + 0x678) * 0x2c + 0xc) <= FLOAT_80619ccc)) {
            *(int *)(rec + 0xaf0) = leader;
        }
    }
}

// =============================================================================
// 0x802bc000  aiactor_consider_follower_follow
// Twin of aiactor_consider_leader_follow for the *second* zone slot (a squad
// follower), with its own rank / distance gates.
// =============================================================================
void aiactor_consider_follower_follow(void)
{
    int actor = aiactor_current_frame_actor();
    int rec = *(int *)(actor + 0x7c);
    int follower = *(int *)(DAT_80611b9c + *(int *)(rec + 0x678) * 0x2c + 4);
    if ((*(int *)(rec + 0xb04) < 6) && (DAT_805015bd != '\n') && (follower != 0) &&
        (*(int *)(rec + 0x44) == 0) && (tracker_event_state_is_idle(rec) != 0) &&
        (*(float *)(rec + 0x78) <= FLOAT_80619c4c) &&
        (*(float *)(actor + 0x1cc) <= FLOAT_80619c64)) {
        int target = *(int *)(rec + 4);
        if ((target != 0) && (*(int *)(target + 0x20) == 8)) {
            unsigned int gate;
            if (DAT_805015bd == '\n')
                gate = *(unsigned int *)(rec + 0x14);
            else
                gate = *(unsigned int *)(rec + 0x1c);
            if (((gate & 4) != 0) && (*(float *)(target + 0x1cc) < *(float *)(actor + 0x1cc)))
                return;
        }
        if (((*(unsigned int *)(follower + 0x28) & 1) != 0) &&
            (*(float *)(DAT_80611b9c + *(int *)(rec + 0x678) * 0x2c + 0x10) <= FLOAT_80619cd0)) {
            *(int *)(rec + 0xaf0) = follower;
        }
    }
}

} // extern "C"
