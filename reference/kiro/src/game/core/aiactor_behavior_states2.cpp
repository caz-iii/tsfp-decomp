// =============================================================================
// aiactor_behavior_states2.cpp - more AI actor per-behaviour-state ticks + the
// weapon-firing tick.
//
// Continuation of aiactor_behavior_states.cpp / aiactor_behavior_special.cpp.
// These are additional behaviour-state ticks dispatched per frame by the AI
// behaviour table (aiactor_script_fsm), plus the shared weapon-firing tick and
// two small helpers.
//
//   - aiactor_behavior_guard_tick (0x802c1730): guard/hold-and-fidget; aim at
//     leader, resolve target zone through the zone door, retarget.
//   - aiactor_behavior_patrol_pingpong_tick (0x802c1c70): patrol between two
//     fixed sound-pad zones (ids 0x3ea <-> 0x3ee), following the path.
//   - aiactor_behavior_wander_tick (0x802c1ec4): random wander (mode 1 pick a
//     random zone, mode 2 follow the path) with a timer-based speed modifier.
//   - aiactor_behavior_investigate_tick (0x802c22d0): investigate/search FSM;
//     plays search clips by difficulty, heads to a zone near the last-known
//     target, follows the path.
//   - aiactor_weapon_fire_tick (0x802c27f0): the AI weapon-firing tick - solves
//     aim, applies recoil/spread/accuracy jitter (rec+0x27f), decrements the
//     burst counter (rec+0x2eb), and fires via object_melee_thrown_fire.
//   - aiactor_should_use_melee_variant (0x802c3524): predicate - roll to use the
//     alt/melee fire variant based on the weapon flag + secondary ammo state.
//   - aiactor_behavior_alerted_tick (0x802c35b0): "spotted the player / raise
//     alarm" FSM (startle, alert-broadcast, fire, alarm anim, hand off).
//   - aiactor_behavior_teleport_in_tick (0x802c3b38): spawn/teleport-in
//     behaviour (init model anim + activate, hold, then activate-all + destroy
//     self instance). Actor is the last integer parameter.
//   - aiactor_coop_hook_stub (0x802c3dac): empty co-op hook (no-op).
//   - aiactor_find_flagged_bot (0x802c3db0): first bot with flag 0x200000 set.
//
// AI actor control record at (actor+0x7c) ("rec"), int-indexed (rec[N]) and
// byte-offset (rec+0xNNN). Field views mirror the sibling files:
//   rec+0x20 mode  rec+0x9d0 queued  rec+0x9d4 prev  rec+0x9d8 timer
//   rec+0x684 fallback zone  rec+0x678 zone id  rec+0x94 active weapon slot
//   rec+0xaac/0xaa8 action + latch  rec+0xabc/0xab8 secondary action + latch
//   rec+0x27f accuracy/spread accumulator  rec+0x2eb burst counter
//   rec+0xbf0 weapon-descriptor block ptr
//
// Address range: 0x802c1730 - 0x802c3dff
// Subsystem: game (AI actor behaviour states + weapon fire)
//
// NON-MATCHING: PPC frame register-pair -> aiactor_current_frame_actor(); the
// FP-register spill args on the fire/alert/teleport ticks are frame junk (the
// helpers re-read from the record); CONCAT44 int->double bias reduced to plain
// float; the recoil rsqrt refinement uses a self-contained SQRT; helpers are
// declared file-local (C-linkage) matched to the call sites.
// =============================================================================

#include "common.h"

extern "C" {

// --- level / global state (raw symbols) --------------------------------------
extern char DAT_805015bd;          // game/level mode selector
extern char DAT_805015be;          // sub-mode
extern unsigned char DAT_805015c0; // co-op flags
extern int  DAT_80588610;          // active-actor pointer array (stride 4)
extern int  DAT_805887c0;          // leader/reference zone id
extern int  DAT_80610ba0;          // character-def table base (stride 0x29c)
extern int  DAT_80611824;          // clip -> "no interrupt" flag table (stride 4)
extern int  DAT_80611928;          // designated leader/reference actor
extern int  DAT_80611b8c;          // sound-pad zone table base (stride 0x20)
extern int  DAT_80611b9c;          // per-zone leader-word table base (stride 0x2c)
extern int  DAT_80611c14;          // sound-pad zone count
extern int  DAT_80611d54;          // co-op secondary-player context
extern int  DAT_80611d74;          // local-player input handle
extern int  DAT_80611d78;          // world/level context base
extern int  DAT_80612098;          // active actor count
extern int  DAT_806120a4;          // door-tick scan cursor
extern int  DAT_806120f4;          // leader-zone-valid count
extern int  DAT_80612610;          // first bot slot index
extern int  DAT_806108e4;          // aim-lead enable count
extern short DAT_8043e23c;         // per-weapon character-def index (stride 0xf)
extern int  DAT_804308bc;          // per-weapon burst-count table A (stride 0x24)
extern int  DAT_804308c0;          // per-weapon burst-count table B (stride 0x24)
extern short DAT_804d1cd0;          // clip -> attr index table
extern int  DAT_804d34d8;          // clip attr table +0 (stride 8)
extern int  DAT_804d34dc;          // clip attr table +4 (stride 8)
extern int  DAT_806109c0;          // frame-delta raw bits
extern float DAT_803e0800;         // aim basis vector .x
extern float DAT_803e0804;         // aim basis vector .y
extern float DAT_803e0808;         // aim basis vector .z

// float / double literal pool
extern float FLOAT_806108ec;
extern float FLOAT_80619c4c;   // 0.0f (guard/investigate tables)
extern float FLOAT_80619c50;   // 1.0f
extern float FLOAT_80619c58;
extern float FLOAT_80619c64;
extern float FLOAT_80619c8c;
extern float FLOAT_80619c90;
extern float FLOAT_80619c9c;
extern float FLOAT_80619d20;   // wander slow-speed factor
extern float FLOAT_80619d24;   // co-op search clip rate
extern float FLOAT_80619d28;   // 0.0f (alt zero, weapon tables)
extern float FLOAT_80619d2c;
extern float FLOAT_80619d30;   // rng scale base
extern float FLOAT_80619d34;   // rng normaliser
extern float FLOAT_80619d38;
extern float FLOAT_80619d3c;
extern float FLOAT_80619d40;
extern float FLOAT_80619d44;
extern float FLOAT_80619d48;
extern float FLOAT_80619d4c;
extern float FLOAT_80619d50;
extern float FLOAT_80619d54;
extern float FLOAT_80619d58;
extern float FLOAT_80619d5c;
extern float FLOAT_80619d60;
extern float FLOAT_80619d7c;   // (melee-variant roll, used by helper file)
extern float FLOAT_80619d80;   // 0.0f (alert-tick pool)
extern float FLOAT_80619d84;   // 1.0f (alert-tick pool)
extern float FLOAT_80619d88;
extern double DOUBLE_80619c68;
extern double DOUBLE_80619d68;
extern double DOUBLE_80619d70;
extern double DOUBLE_80619d90;

// --- named helpers (file-local, C-linkage, matched to the call sites) --------
extern int  aiactor_retarget_visible_teammate(void);
extern void aiactor_follow_zone_path_void(void);
extern int  aiactor_follow_zone_path(void);
extern void aiactor_set_stance_flags(int actor, int flags);
extern void aiactor_leader_zone_update(int actor);
extern void aiactor_turn_toward_leader(void);
extern void aiactor_play_turn_animation(int actor);
extern void aiactor_facing_update(void);
extern void aiactor_acquire_target_by_mode(void);
extern int  aiactor_resolve_target_zone(int actor);
extern int  aiactor_nav_advance_path(int actor, int *nav);
extern int  aiactor_find_squad_index(int actor);
extern int  aiactor_zone_door_tick(void);
extern int  aiactor_evaluate_target_los(void);
extern unsigned int aiactor_alert_group_flagA_any(unsigned int mask);
extern void aiactor_alert_broadcast_group(unsigned int a, int entity, int zoneId);
extern int  aiactor_activate_all_valid(void);
extern void aiactor_destroy_instance(unsigned long long a, double b, double c, double d,
        unsigned long long e, unsigned long long f, unsigned long long g, unsigned long long h, int actor);
extern void aiactor_deactivate_all_idle(void);
extern void aiactor_deactivate_stuck_projectiles(unsigned long long a, double b, double c, double d,
        double e, unsigned long long f, unsigned long long g, unsigned long long h);
extern int  soundpad_find_zone_by_id(int id);
extern int  soundpad_get_entity_id_from_zone(int zone);
extern unsigned int rng_counter_tick(void);
extern void tracker_play_melee_fidget_clip(int actor);
extern void anim_set_playback_rate(double r, int *slot, int a);
extern unsigned long long anim_play_scaled(double a, double b);
extern int  anim_is_clip_active(int actor, int clip);
extern int  anim_active_slot_userflags(int slot);
extern int  anim_event_scan_active(void);
extern int  anim_clip_has_flags(int actor, int clip, int mask, int a);
extern int  anim_primary_clip_time_left(double a, int slot);
extern int  anim_get_descriptor(int animId);
extern int  anim_has_event_with_mask(int animHandle, unsigned short mask);
extern unsigned int object_aim_target_solve(unsigned long long a, double b, double c, double d,
        double e, unsigned long long f, unsigned long long g, unsigned long long h);
extern void object_weapon_compute_ammo_state(unsigned int a, int *rec, int resultOut);
extern void object_switch_to_next_available(int *rec);
extern void object_reload_ammo_apply(void);
extern int  object_melee_thrown_fire(unsigned long long a, double b, int actor, int c, int d, int e);
extern int  object_slot_stub(void);
extern int  object_weapon_fuse_stage_count(void);
extern void object_sound_slot_accumulate(int object, int channel, short delta);
extern int  match_build_weapon_list(void);
extern void gib_find_nearest_pickup(unsigned long long a, double b, double c, double d,
        unsigned long long e, unsigned long long f, unsigned long long g, unsigned long long h, int actor);
extern int  player_ai_is_sniper_weapon(int weaponType);
extern int  player_ai_is_valid_weapon_type(int weaponType);
extern int  player_slot_get_entity_id(int pSlot);
extern int  level_get_current_id(void);
extern int  level_actor_check_active(int actorId);
extern int  level_actor_get_load_status(int actorId);
extern void model_instance_anim_init(unsigned long long ctx, double t, int model);
extern int  entity_handle_resolve(unsigned int id);
extern void prop_fire_tick(int self_unused, int owner, int side, int a4, int mode, int *rec);
extern double math_poly_trig(double x);
extern double math_sqrt(double x);
extern int  FUN_802c3524(int actor, int ammoState); // melee-variant predicate (this file)

// --- PPC frame-context intrinsic model ---------------------------------------
extern int  aiactor_current_frame_actor(void);

// Ghidra SQRT intrinsic (single-precision Newton-Raphson).
static inline float SQRT(float x) {
    if (x <= 0.0f) return 0.0f;
    float g = x;
    for (int i = 0; i < 6; i = i + 1) g = 0.5f * (g + x / g);
    return g;
}

// forward decls
int aiactor_should_use_melee_variant(int actor, int ammoState);
void aiactor_coop_hook_stub(void);
int aiactor_find_flagged_bot(void);

// state-switch bookkeeping (byte-offset view), timer seed = FLOAT_80619c4c.
static void ai_set_mode(int rec, int newMode)
{
    if (*(int *)(rec + 0x20) == newMode)
        return;
    *(int *)(rec + 0x9d4) = *(int *)(rec + 0x20);
    *(float *)(rec + 0x9d8) = FLOAT_80619c4c;
    *(int *)(rec + 0x20) = newMode;
    *(int *)(rec + 0x9d0) = *(int *)(rec + 0x20);
}

// alert-tick variant uses a distinct zero literal (FLOAT_80619d80) for the timer.
static void ai_set_mode_alert(int rec, int newMode)
{
    if (*(int *)(rec + 0x20) == newMode)
        return;
    *(int *)(rec + 0x9d4) = *(int *)(rec + 0x20);
    *(float *)(rec + 0x9d8) = FLOAT_80619d80;
    *(int *)(rec + 0x20) = newMode;
    *(int *)(rec + 0x9d0) = *(int *)(rec + 0x20);
}

// queued-mode consume + aim-timer decay (0-timer variant selectable).
static void ai_preamble(int rec, float zero, double decayBiasBase)
{
    if (-1 < *(int *)(rec + 0x9d0)) {
        if (*(int *)(rec + 0x9d0) != *(int *)(rec + 0x20)) {
            *(float *)(rec + 0x9d8) = zero;
            *(int *)(rec + 0x9d4) = *(int *)(rec + 0x20);
            *(int *)(rec + 0x20) = *(int *)(rec + 0x9d0);
        }
        *(int *)(rec + 0x9d0) = -1;
    }
    if ((*(unsigned int *)(rec + 0x2c) & 0x80) == 0) {
        *(float *)(rec + 0xa90) = zero;
    } else {
        *(float *)(rec + 0xa90) =
            FLOAT_806108ec *
            (FLOAT_80619c50 /
             (float)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) - decayBiasBase)) +
            *(float *)(rec + 0xa90);
    }
}

// latch-checked action queue: writes action `code` at (rec+aOff)/(rec+lOff);
// returns 1 if the caller should bail (pending code-1 with unset gate).
static int ai_queue(int rec, int aOff, int lOff, int code)
{
    if (*(int *)(rec + aOff) == 1 && *(int *)(rec + lOff) == 0) {
        unsigned int gate;
        if (DAT_805015bd == '\n') gate = *(unsigned int *)(rec + 0x14);
        else                       gate = *(unsigned int *)(rec + 0x1c);
        if ((gate & 1) == 0) return 1;
    }
    *(int *)(rec + aOff) = code;
    *(int *)(rec + lOff) = 0;
    return 0;
}

// =============================================================================
// 0x802c1730  aiactor_behavior_guard_tick
// Guard/hold-and-fidget: aim at the leader, resolve the target zone through the
// zone door, retarget, and play the fidget/melee clip.
// =============================================================================
void aiactor_behavior_guard_tick(void)
{
    int actor = aiactor_current_frame_actor();
    int *rec = *(int **)(actor + 0x7c);
    int hasTeammate = aiactor_retarget_visible_teammate();

    if (-1 < rec[0x274]) {
        if (rec[0x274] != rec[8]) {
            rec[0x276] = (int)FLOAT_80619c4c;
            rec[0x275] = rec[8];
            rec[8] = rec[0x274];
        }
        rec[0x274] = -1;
    }
    if ((rec[0xb] & 0x80) == 0) {
        rec[0x2a4] = (int)FLOAT_80619c4c;
    } else {
        rec[0x2a4] = (int)(FLOAT_806108ec *
            (FLOAT_80619c50 / (float)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80619c68))
            + (float)rec[0x2a4]);
    }

    // aim origin
    {
        int obj = (rec == (int *)0) ? 0 : rec[0];
        int useOwn = 1;
        if ((rec[1] == 0) ||
            (((rec != (int *)0 && obj != 0) && (DAT_80612610 <= rec[2])) &&
             (((*(unsigned int *)(obj + 0x28) & 0x100) != 0 ||
               ((*(unsigned int *)(obj + 0x10) & 0xffff) == 0)))))
            useOwn = 1;
        else
            useOwn = 0;
        if (useOwn) {
            rec[0xc] = *(int *)(rec[0] + 100);
            rec[0xd] = *(int *)(rec[0] + 0x68);
            rec[0xe] = *(int *)(rec[0] + 0x6c);
        } else {
            rec[0xc] = *(int *)(rec[1] + 100);
            rec[0xd] = *(int *)(rec[1] + 0x68);
            rec[0xe] = *(int *)(rec[1] + 0x6c);
        }
    }

    if ((rec[2] == DAT_806120a4) && (aiactor_zone_door_tick() != 0)) {
        rec[0x26b] = 0;
        rec[0x26a] = 0;
        ai_queue((int)rec, 0xaac, 0xaa8, rec[0x2af]); // rec[0x2ab/0x2aa] <- rec[0x2af/0x2ae]
        rec[0x2aa] = rec[0x2ae];
        return;
    }

    int state = rec[8];
    if (state == 1) {
        tracker_play_melee_fidget_clip(actor);
        int attr = *(int *)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[*(int *)(*(int *)(actor + 4) + 0xcc)] * 8);
        if ((attr & 0x100) != 0) {
            rec[10] = rec[10] & 0xfffbffff;
            if (((*(int **)(actor + 4))[0x32] & 1) != 0)
                anim_set_playback_rate((double)FLOAT_80619c58, *(int **)(actor + 4), 2);
        }
        aiactor_acquire_target_by_mode();
        aiactor_turn_toward_leader();
        if (hasTeammate == 0) {
            if ((rec[2] == DAT_806120a4) && (rec[1] != 0)) {
                rec[0x26b] = 0;
                rec[0x26a] = 0;
                int zone = aiactor_resolve_target_zone(actor);
                if (zone < 0)
                    rec[0x1a1] = *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    rec[0x1a1] = aiactor_resolve_target_zone(actor);
                rec[0x26b] = 0;
                rec[0x26a] = 0;
                if (aiactor_nav_advance_path(actor, rec + 0x19e) != -2) {
                    if (ai_queue((int)rec, 0xaac, 0xaa8, rec[0x2af]))
                        return;
                    rec[0x2aa] = rec[0x2ae];
                }
            }
        } else {
            if (ai_queue((int)rec, 0xaac, 0xaa8, 4))
                return;
        }
    } else if (state < 1) {
        if (-1 < state) {
            tracker_play_melee_fidget_clip(actor);
            if (rec[8] != 1) {
                rec[0x275] = rec[8];
                rec[0x276] = (int)FLOAT_80619c4c;
                rec[8] = 1;
                rec[0x274] = rec[8];
            }
        }
    } else if (state < 3) {
        tracker_play_melee_fidget_clip(actor);
        if (hasTeammate != 0)
            rec[10] = rec[10] | 2;
        aiactor_turn_toward_leader();
        int attr = *(int *)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[*(int *)(*(int *)(actor + 4) + 0xcc)] * 8);
        if ((attr & 0x100) != 0) {
            rec[10] = rec[10] & 0xfffbffff;
            if (((*(int **)(actor + 4))[0x32] & 1) != 0)
                anim_set_playback_rate((double)FLOAT_80619c58, *(int **)(actor + 4), 2);
        }
        if ((hasTeammate == 0) && (rec[8] != 1)) {
            rec[0x275] = rec[8];
            rec[0x276] = (int)FLOAT_80619c4c;
            rec[8] = 1;
            rec[0x274] = rec[8];
        }
    }
}

// =============================================================================
// 0x802c1c70  aiactor_behavior_patrol_pingpong_tick
// Patrol between two fixed sound-pad zones (entity ids 0x3ea <-> 0x3ee): on
// arrival at one, retarget to the other; follow the path.
// =============================================================================
void aiactor_behavior_patrol_pingpong_tick(int actor)
{
    int rec = *(int *)(actor + 0x7c);
    aiactor_retarget_visible_teammate();
    ai_preamble(rec, FLOAT_80619c4c, DOUBLE_80619c68);

    if (*(int *)(rec + 0x684) == -1) {
        int id = soundpad_get_entity_id_from_zone(*(int *)(rec + 0x678));
        if (id < 0x3ee) {
            if (id == 0x3ea) {
                int z = soundpad_find_zone_by_id(0x3ee);
                if (z < 0)
                    *(int *)(rec + 0x684) =
                        *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    *(int *)(rec + 0x684) = soundpad_find_zone_by_id(0x3ee);
                *(int *)(rec + 0x9ac) = 0;
                *(int *)(rec + 0x9a8) = 0;
            }
        } else if (id < 0x3f0) {
            int z = soundpad_find_zone_by_id(0x3ea);
            if (z < 0)
                *(int *)(rec + 0x684) =
                    *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            else
                *(int *)(rec + 0x684) = soundpad_find_zone_by_id(0x3ea);
            *(int *)(rec + 0x9ac) = 0;
            *(int *)(rec + 0x9a8) = 0;
        }
    }
    if (aiactor_follow_zone_path() != 0) {
        int id = soundpad_get_entity_id_from_zone(*(int *)(rec + 0x684));
        if (id == 0x3ee) {
            int z = soundpad_find_zone_by_id(0x3ea);
            if (z < 0)
                *(int *)(rec + 0x684) =
                    *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            else
                *(int *)(rec + 0x684) = soundpad_find_zone_by_id(0x3ea);
            *(int *)(rec + 0x9ac) = 0;
            *(int *)(rec + 0x9a8) = 0;
        } else if ((id < 0x3ee) && (id == 0x3ea)) {
            int z = soundpad_find_zone_by_id(0x3ee);
            if (z < 0)
                *(int *)(rec + 0x684) =
                    *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            else
                *(int *)(rec + 0x684) = soundpad_find_zone_by_id(0x3ee);
            *(int *)(rec + 0x9ac) = 0;
            *(int *)(rec + 0x9a8) = 0;
        }
    }
    aiactor_set_stance_flags(actor, 1);
    aiactor_play_turn_animation(actor);
    aiactor_facing_update();
}

// =============================================================================
// 0x802c1ec4  aiactor_behavior_wander_tick
// Random wander: mode 1 picks a random sound-pad zone (not the current one),
// mode 2 follows the path; a timer (rec+0xb30) slows the walk speed (rec+0xad0).
// =============================================================================
void aiactor_behavior_wander_tick(int actor)
{
    extern int spawn_find_random_in_range(void);
    int rec = *(int *)(actor + 0x7c);
    aiactor_retarget_visible_teammate();
    ai_preamble(rec, FLOAT_80619c4c, DOUBLE_80619c68);

    if (*(float *)(rec + 0xb30) <= FLOAT_80619c9c)
        *(int *)(rec + 0xad0) = *(int *)(rec + 0xb14);
    else
        *(float *)(rec + 0xad0) = FLOAT_80619d20 * *(float *)(rec + 0xb14);
    *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) | 0x40000;

    int mode = *(int *)(rec + 0x20);
    if (mode == 1) {
        int zone = spawn_find_random_in_range();
        if (zone == 0) zone = spawn_find_random_in_range();
        if (zone == 0) zone = spawn_find_random_in_range();
        if (DAT_80611c14 == 1) {
            zone = *(int *)(DAT_80611b8c + *(int *)(rec + 0x678) * 0x20);
        } else {
            while (zone < 1 || (zone == *(int *)(DAT_80611b8c + *(int *)(rec + 0x678) * 0x20))) {
                unsigned int r = rng_counter_tick();
                zone = *(int *)(DAT_80611b8c + (r - (r / (unsigned int)DAT_80611c14) * (unsigned int)DAT_80611c14) * 0x20);
            }
        }
        int z = soundpad_find_zone_by_id(zone);
        if (z < 0)
            *(int *)(rec + 0x684) =
                *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        else
            *(int *)(rec + 0x684) = soundpad_find_zone_by_id(zone);
        *(int *)(rec + 0x9ac) = 0;
        *(int *)(rec + 0x9a8) = 0;
        ai_set_mode(rec, 2);
    } else if (mode < 1) {
        if ((-1 < mode) && (mode != 1)) {
            *(int *)(rec + 0x9d4) = mode;
            *(float *)(rec + 0x9d8) = FLOAT_80619c4c;
            *(int *)(rec + 0x20) = 1;
            *(int *)(rec + 0x9d0) = *(int *)(rec + 0x20);
        }
    } else if ((mode < 3) && (aiactor_follow_zone_path() != 0)) {
        if ((DAT_805015be == '\x12') &&
            ((*(int *)(DAT_80611b9c + *(int *)(rec + 0x678) * 0x2c) != 0) &&
             ((*(unsigned int *)(*(int *)(DAT_80611b9c + *(int *)(rec + 0x678) * 0x2c) + 0x28) & 1) != 0))) {
            *(int *)(rec + 0xaf0) = *(int *)(DAT_80611b9c + *(int *)(rec + 0x678) * 0x2c);
            if (ai_queue(rec, 0xaac, 0xaa8, 0x400000))
                return;
            return;
        }
        ai_set_mode(rec, 1);
    }

    if ((*(unsigned int *)(rec + 0x28) & 1) != 0) {
        int codeA, codeB;
        if (DAT_805015bd == '\n') { codeA = *(int *)(rec + 0x10); codeB = *(int *)(rec + 0x14); }
        else                       { codeA = *(int *)(rec + 0x18); codeB = *(int *)(rec + 0x1c); }
        *(int *)(rec + 0xabc) = codeB;
        *(int *)(rec + 0xab8) = codeA;
        ai_queue(rec, 0xaac, 0xaa8, 0x20);
    }
}

// =============================================================================
// 0x802c22d0  aiactor_behavior_investigate_tick
// Investigate/search FSM: if not disturbed, queue the idle action; else play a
// search clip (by co-op/difficulty), pick a zone near the last-known target,
// follow the path, and retarget.
// =============================================================================
void aiactor_behavior_investigate_tick(void)
{
    int actor = aiactor_current_frame_actor();
    int rec = *(int *)(actor + 0x7c);
    aiactor_retarget_visible_teammate();
    ai_preamble(rec, FLOAT_80619c4c, DOUBLE_80619c68);

    if (((*(unsigned int *)(rec + 0xc6c) & 4) == 0) && ((*(unsigned int *)(rec + 0x28) & 0x2000) == 0)) {
        aiactor_set_stance_flags(actor, 1);
        *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) & 0xefffffff;
        ai_queue(rec, 0xaac, 0xaa8, 8);
        return;
    }

    if (*(int *)(rec + 0x44) == 1) {
        if (anim_is_clip_active(actor, 0x113) == 0)
            anim_play_scaled((double)FLOAT_80619c64, (double)FLOAT_80619c50);
    } else if (DAT_805015bd == '\n') {
        if (anim_is_clip_active(actor, 0x36) == 0)
            anim_play_scaled((double)FLOAT_80619c64, (double)FLOAT_80619d24);
    } else if (*(int *)(rec + 0xb04) < 5) {
        if (anim_is_clip_active(actor, 0x26) == 0)
            anim_play_scaled((double)FLOAT_80619c64, (double)FLOAT_80619c90);
    } else {
        if (anim_is_clip_active(actor, 0x36) == 0)
            anim_play_scaled((double)FLOAT_80619c64, (double)FLOAT_80619c90);
    }

    int mode = *(int *)(rec + 0x20);
    if (mode == 1) {
        int myZone = *(int *)(rec + 0x678);
        int pick = myZone;
        if (DAT_805015bd == '\x02') {
            if (*(int *)(rec + 0x80) != 0)
                pick = aiactor_find_squad_index(actor);
        } else if ((DAT_805015bd != '\n') && (*(int *)(rec + 0xb04) < 6)) {
            pick = DAT_805887c0;
            int target = *(int *)(rec + 4);
            if ((DAT_806120f4 < 1) ||
                ((target == 0 || (*(int *)(target + 0x20) != 8)) ||
                 ((*(unsigned int *)(*(int *)(target + 0x7c) + 0xc6c) & 4) != 0))) {
                if (((DAT_806120f4 < 1) && (target = *(int *)(rec + 4), pick = myZone, target != 0)) &&
                    (*(int *)(target + 0x20) == 8))
                    pick = *(int *)(*(int *)(target + 0x7c) + 0x678);
            } else {
                int zt = DAT_80611b8c + DAT_805887c0 * 0x20;
                float dx = *(float *)(actor + 100) - *(float *)(zt + 0xc);
                float dy = *(float *)(actor + 0x68) - *(float *)(zt + 0x10);
                float dz = *(float *)(actor + 0x6c) - *(float *)(zt + 0x14);
                if (*(float *)(rec + 0xaf4) <= dz * dz + dx * dx + dy * dy)
                    pick = *(int *)(*(int *)(target + 0x7c) + 0x678);
            }
        }
        if (pick == *(int *)(rec + 0x678)) {
            unsigned int r = rng_counter_tick();
            pick = r - (r / (unsigned int)DAT_80611c14) * (unsigned int)DAT_80611c14;
        }
        if (pick < 0)
            *(int *)(rec + 0x684) =
                *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        else
            *(int *)(rec + 0x684) = pick;
        *(int *)(rec + 0x9ac) = 0;
        *(int *)(rec + 0x9a8) = 0;
        ai_set_mode(rec, 2);
    } else if (mode < 1) {
        if ((-1 < mode) && (mode != 1)) {
            *(int *)(rec + 0x9d4) = mode;
            *(float *)(rec + 0x9d8) = FLOAT_80619c4c;
            *(int *)(rec + 0x20) = 1;
            *(int *)(rec + 0x9d0) = *(int *)(rec + 0x20);
        }
    } else if (mode < 3) {
        aiactor_set_stance_flags(actor, 1);
        if (aiactor_follow_zone_path() != 0) {
            *(int *)(rec + 4) = 0;
            ai_set_mode(rec, 1);
        }
        aiactor_leader_zone_update(actor);
    }

    if (((*(unsigned int *)(rec + 0x28) & 0x2000) != 0) && (FLOAT_80619c8c < *(float *)(rec + 0xb30)))
        ai_queue(rec, 0xaac, 0xaa8, 8);
}

// =============================================================================
// 0x802c27f0  aiactor_weapon_fire_tick
// The AI weapon-firing tick: solves aim, gates on clip/ammo/LOS, applies recoil
// + spread + accuracy jitter into the aim accumulator (rec+0x27f), decrements
// the burst counter (rec+0x2eb), and fires via object_melee_thrown_fire. The
// FP-register spill args are frame junk.
// =============================================================================
void aiactor_weapon_fire_tick(void)
{
    int actor = aiactor_current_frame_actor();
    int *rec = *(int **)(actor + 0x7c);
    int hasTeammate = aiactor_retarget_visible_teammate();
    int slot = *(int *)(actor + 4);

    bool useAlt = (*(unsigned int *)(&DAT_804d34d8 + (short)(&DAT_804d1cd0)[*(int *)(slot + 0xcc)] * 8) & 0x100000) != 0;
    double aimBlend = (double)*(float *)(slot + 400);
    if (((double)FLOAT_80619d28 < aimBlend) &&
        ((*(unsigned int *)(&DAT_804d34d8 + (short)(&DAT_804d1cd0)[*(int *)(slot + 0x124)] * 8) & 0x100000) != 0))
        useAlt = true;
    {
        unsigned int mode = (DAT_805015bd == '\n') ? (unsigned int)rec[5] : (unsigned int)rec[7];
        if ((mode & 8) != 0) useAlt = false;
    }
    if ((*(int *)(DAT_80611d74 + 0x114) == 0x100) &&
        ((countLeadingZeros(*(unsigned int *)(rec[0x2fc] + 0x54)) >> 5 & 0x200000) != 0))
        return;
    {
        unsigned int mode = (DAT_805015bd == '\n') ? (unsigned int)rec[5] : (unsigned int)rec[7];
        if (((mode & 1) != 0) && (anim_clip_has_flags(actor, 0x10000, 0x8000, 0) == 0))
            return;
    }

    object_aim_target_solve(0, 0, 0, 0, 0, 0, 0, 0);
    object_aim_target_solve(0, 0, 0, 0, 0, 0, 0, 0);

    if (((rec[10] & 2) != 0) && (rec[0x305] != 0)) {
        prop_fire_tick(rec[0x305], *(int *)(*(int *)(rec[0x305] + 0x7c) + 0x6c), 0, 0, 3, rec);
        return;
    }
    if (object_slot_stub() != 0)
        aiactor_deactivate_stuck_projectiles(0, 0, 0, 0, 0, 0, 0, 0);
    if (anim_is_clip_active(actor, 0x35) != 0)
        return;
    if ((anim_is_clip_active(actor, 0x34) != 0) ||
        ((rec[0xaf] == 0 && (rec[0x25] != 0x42)) && (rec[0x25] != 0x43)) ||
        (anim_clip_has_flags(actor, 0x34, 0x2000, 0) != 0) ||
        (((useAlt && (rec[0x2b7] == 0)) && ((rec[0xb] & 0x400) == 0)) ||
         (((rec[10] & 2) == 0 && (!useAlt)))))
        return;

    if (!useAlt) {
        int burst;
        if (DAT_805015bd == '\n')      burst = *(int *)(&DAT_804308bc + rec[0x81] * 0x24);
        else if (rec[0x25] == 0x23)    burst = object_weapon_fuse_stage_count();
        else                            burst = *(int *)(&DAT_804308c0 + rec[0x81] * 0x24);
        if ((rec[0xaf] != 0) && (rec[99] != 0))
            burst = burst << 1;
        if ((rec[0xb] & 0x400) == 0) {
            int cur = rec[0x2eb];
            if (cur == 0) {
                float roll = FLOAT_80619d2c;
                if (rec[0x25] != 0x23) {
                    unsigned int r = rng_counter_tick();
                    roll = FLOAT_80619d30 * ((float)((double)(unsigned int)r - DOUBLE_80619d68) / FLOAT_80619d34) + FLOAT_80619d30;
                }
                if ((float)rec[0x2d3] <= roll)
                    return;
                rec[0x2eb] = burst;
            } else {
                if (cur < burst) burst = cur;
                rec[0x2eb] = burst;
            }
        } else {
            rec[0x2eb] = 1;
        }
    }

    int ammo[16]; // auStack_c4 result block (local_bc..local_98 map into this)
    if ((rec[0xaf] == 0) || (rec[99] == 0))
        object_weapon_compute_ammo_state(1, rec, (int)&ammo[0]);
    else
        object_weapon_compute_ammo_state(0, rec, (int)&ammo[0]);
    // decode the result block fields the original reads (offsets within ammo[])
    int local_98 = ammo[10];
    int local_9c = ammo[9];
    int local_a0 = ammo[8];
    int local_a4 = ammo[7];
    int local_a8 = ammo[6];
    int local_ac = ammo[5];
    int local_b8 = ammo[2];
    int local_bc = ammo[1];

    if ((local_98 != 0) || ((local_a0 != 0 && ((DAT_805015bd == '\n' || (local_9c != 0)))))) {
        rec[10] = rec[10] & 0xfffffffd;
        if (local_98 == 0) {
            if ((local_a4 != 0) || (DAT_805015bd == '\n'))
                object_reload_ammo_apply();
        } else {
            object_switch_to_next_available(rec);
        }
        return;
    }

    // spread/accuracy accumulator update (rec[0x27f])
    double aimTimer = (double)(float)rec[0x2a4];
    if (aimTimer <= (double)FLOAT_80619d38) {
        int target = rec[1];
        if (((target == 0) || (*(int *)(target + 0x20) != 8)) || (DAT_806108e4 < 1)) {
            if ((target != 0) && (0 < DAT_806108e4)) {
                double cur = (double)(float)rec[0x27f];
                if ((double)FLOAT_80619d28 != cur) {
                    double step = (double)(FLOAT_806108ec /
                        (FLOAT_80619d30 * (float)((double)(CONCAT44(0x43300000, DAT_806109c0) ^ 0x80000000) - DOUBLE_80619d70)));
                    if (step <= ABS(cur)) {
                        if (cur <= (double)FLOAT_80619d28) rec[0x27f] = (int)(float)(cur + step);
                        else                                rec[0x27f] = (int)(float)(cur - step);
                    } else {
                        rec[0x27f] = (int)FLOAT_80619d28;
                    }
                }
            }
        } else {
            // aim-lead toward a moving class-8 target (velocity extrapolation)
            float bx = DAT_803e0800, by = DAT_803e0804, bz = DAT_803e0808;
            float dvx = FLOAT_80619d44 * *(float *)(actor + 0xf8) - *(float *)(target + 0xf8);
            float dvy = FLOAT_80619d44 * *(float *)(actor + 0xfc) - *(float *)(target + 0xfc);
            float dvz = FLOAT_80619d44 * *(float *)(actor + 0x100) - *(float *)(target + 0x100);
            if ((*(unsigned int *)(*(int *)(target + 0x7c) + 0xc6c) & 0x10) != 0) {
                float k = FLOAT_80619d48;
                dvx = dvx * k; dvy = dvy * k; dvz = dvz * k;
                unsigned int r = rng_counter_tick();
                unsigned int r2 = DAT_806109c0 * 5;
                int zero = (int)((float)((double)(unsigned int)(r2 ^ 0x80000000) - DOUBLE_80619d70) *
                    ((float)((double)(unsigned int)r - DOUBLE_80619d68) / FLOAT_80619d34) + FLOAT_80619d3c);
                if (zero == 0) {
                    unsigned int r3 = rng_counter_tick();
                    rec[0x27f] = (int)(FLOAT_80619d3c * ((float)((double)(unsigned int)r3 - DOUBLE_80619d68) / FLOAT_80619d34) + FLOAT_80619d3c);
                    unsigned int r4 = rng_counter_tick();
                    if (FLOAT_80619d3c < (float)((double)(unsigned int)r4 - DOUBLE_80619d68) / FLOAT_80619d34)
                        rec[0x27f] = (int)((float)rec[0x27f] * FLOAT_80619d40);
                }
            }
            float rx = (float)rec[0x2e2] - *(float *)(actor + 100);
            float ry = (float)rec[0x2e3] - *(float *)(actor + 0x68);
            float rz = (float)rec[0x2e4] - *(float *)(actor + 0x6c);
            // cross(basis, rel)
            double cx = (double)(bz * rx - bx * rz);
            double cy = (double)(bx * ry - by * rx);
            double cz = (double)(by * rz - bz * ry);
            float len2 = (float)(cy * cy + (double)(float)(cz * cz + (double)(float)(cx * cx)));
            if (FLOAT_80619d4c < len2) {
                float inv = 1.0f / SQRT(len2);
                inv = inv * -(FLOAT_80619d3c * len2 * inv * inv - FLOAT_80619d30);
                double scale = (double)(FLOAT_80619d50 * inv * -(FLOAT_80619d3c * len2 * inv * inv - FLOAT_80619d30));
                cz = (double)(float)(cz * scale);
                cx = (double)(float)(cx * scale);
                cy = (double)(float)(cy * scale);
            }
            double dot = (double)(float)(dvz * cy + (double)(float)(dvx * cx + (double)(float)(dvy * cz)));
            double biasAng = math_poly_trig((double)((float)((double)(unsigned int)(DAT_80611928 ^ 0x80000000) - DOUBLE_80619d70) *
                (FLOAT_80619d50 / (float)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80619d70)) - FLOAT_80619d58));
            double lead = (double)(float)((double)FLOAT_80619d54 + biasAng);
            double mag = math_sqrt((double)(float)((cz * dot) * (cz * dot) + (double)(float)((cy * dot) * (cy * dot))));
            double clamped = (mag < lead) ? mag : lead;
            if ((double)FLOAT_80619d5c <= clamped) {
                if (mag < lead) lead = mag;
            } else {
                lead = (double)FLOAT_80619d5c;
            }
            // signedUnit = +/-1.0f carrying the sign bit of `dot`
            float dotf = (float)dot;
            unsigned int su = ((*(unsigned int *)&dotf) & 0x80000000u) | 0x3f800000u;
            float signedUnit = *(float *)&su;
            double blended = (double)(float)((double)(float)((double)(float)(lead *
                (double)(float)((double)FLOAT_80619d40 * (double)signedUnit)) - (double)(float)rec[0x27f]) *
                (double)FLOAT_80619d60 + (double)(float)rec[0x27f]);
            double c2 = ((double)FLOAT_80619d50 < blended) ? (double)FLOAT_80619d50 : blended;
            if ((double)FLOAT_80619d40 <= c2)
                c2 = ((double)FLOAT_80619d50 < blended) ? (double)FLOAT_80619d50 : blended;
            else
                c2 = (double)FLOAT_80619d40;
            rec[0x27f] = (int)(float)c2;
            if (((player_ai_is_sniper_weapon((int)*(short *)(rec + 0x12)) != 0) ||
                 (player_ai_is_valid_weapon_type((int)*(short *)(rec + 0x12)) != 0)) &&
                (DAT_805015bd == '\n'))
                rec[0x27f] = (int)FLOAT_80619d28;
        }
    } else {
        unsigned int r = rng_counter_tick();
        rec[0x27f] = (int)(FLOAT_80619d3c * ((float)((double)(unsigned int)r - DOUBLE_80619d68) / FLOAT_80619d34) + FLOAT_80619d3c);
        unsigned int r2 = rng_counter_tick();
        double roll = (double)(float)(((float)((double)(unsigned int)r2 - DOUBLE_80619d68)) / FLOAT_80619d34);
        if ((double)FLOAT_80619d3c < roll)
            rec[0x27f] = (int)(float)((double)(float)rec[0x27f] * (double)FLOAT_80619d40);
    }

    if (((hasTeammate == 0) && ((rec[5] & 0x60000) == 0)) && ((rec[5] & 0x80000) == 0)) {
        rec[10] = rec[10] & 0xfffffffd;
        return;
    }
    rec[0xb] = rec[0xb] | 0x80;

    if (!useAlt) {
        rec[0x2ec] = 0;
        if (((DAT_805015bd == '\n') || (rec[1] == 0)) ||
            (((*(unsigned int *)(actor + 0x10) & 0xffff) != 0 || ((*(unsigned int *)(rec[1] + 0x10) & 0xffff) != 0)))) {
            if (aiactor_should_use_melee_variant(actor, (int)&ammo[0]) == 0) {
                if ((local_ac != 0) &&
                    (object_melee_thrown_fire(0, 0, actor, local_bc, 0, 1) != 0) &&
                    (rec[0x2eb] = rec[0x2eb] - 1, rec[0x2eb] < 1))
                    rec[0x2eb] = 0;
            } else {
                if ((object_melee_thrown_fire(0, 0, actor, local_b8, 1, 1) != 0) &&
                    (rec[0x2eb] = rec[0x2eb] - 1, rec[0x2eb] < 1))
                    rec[0x2eb] = 0;
            }
            return;
        }
        if ((local_a8 == 0) || ((*(unsigned int *)(rec[0x2fc] + 0x54) & 0x10) == 0)) {
            if ((local_ac != 0) &&
                (object_melee_thrown_fire(0, 0, actor, local_bc, 0, 1) != 0) &&
                (rec[0x2eb] = rec[0x2eb] - 1, rec[0x2eb] < 1))
                rec[0x2eb] = 0;
        } else {
            int doAlt = 1;
            if (local_ac != 0) {
                unsigned int r = rng_counter_tick();
                if ((double)FLOAT_80619d44 <= (double)(float)(((float)((double)(unsigned int)r - DOUBLE_80619d68)) / FLOAT_80619d34))
                    doAlt = 0;
            }
            if (doAlt) {
                if ((object_melee_thrown_fire(0, 0, actor, local_b8, 1, 1) != 0) &&
                    (rec[0x2eb] = rec[0x2eb] - 1, rec[0x2eb] < 1))
                    rec[0x2eb] = 0;
            } else {
                if ((local_ac != 0) &&
                    (object_melee_thrown_fire(0, 0, actor, local_bc, 0, 1) != 0) &&
                    (rec[0x2eb] = rec[0x2eb] - 1, rec[0x2eb] < 1))
                    rec[0x2eb] = 0;
            }
        }
        return;
    }

    // alt / melee-swing path
    if ((anim_active_slot_userflags(*(int *)(actor + 4)) != 0) ||
        ((double)FLOAT_80619d28 == (double)*(float *)(*(int *)(actor + 4) + 0xd4)))
        rec[0x2ec] = 0;
    int gate = 1;
    if (rec[0x25] == 0x14) {
        unsigned int mode = (DAT_805015bd == '\n') ? (unsigned int)rec[5] : (unsigned int)rec[7];
        if ((mode & 1) == 0) gate = 0;
    }
    if (gate) {
        if (anim_event_scan_active() == 0)
            return;
    }
    if ((rec[0x2ec] == 0) ||
        (((rec[0x25] != 0x1d && (rec[0x25] != 0x1e)) && (1 < (unsigned int)(rec[0x25] - 0x12))) &&
         (rec[0x25] != 0x20))) {
        object_melee_thrown_fire(0, 0, actor, local_bc, 0, 1);
        rec[0x2ec] = rec[0x2ec] + 1;
    }
}

// =============================================================================
// 0x802c3524  aiactor_should_use_melee_variant
// Predicate: if the weapon's flag 0x10 is set, roll (weighted by the secondary
// ammo state) to decide whether to use the alt/melee fire variant. Returns 0/1.
// =============================================================================
int aiactor_should_use_melee_variant(int actor, int ammoState)
{
    if ((*(unsigned int *)(*(int *)(*(int *)(actor + 0x7c) + 0xbf0) + 0x54) & 0x10) != 0) {
        if ((*(int *)(ammoState + 0x1c) == 0) ||
            ((*(int *)(ammoState + 0x18) != 0) &&
             ((float)((double)(unsigned int)rng_counter_tick() - DOUBLE_80619d68) / FLOAT_80619d34 >= FLOAT_80619d7c)))
            return 0;
        return 1;
    }
    return 0;
}

// =============================================================================
// 0x802c35b0  aiactor_behavior_alerted_tick
// "Spotted the player / raise alarm" FSM: state 0 startle-turn, state 1 evaluate
// + broadcast alert, state 2 fire + gib pickup, state 0x65 alarm animation, then
// hand off to combat. FP-register spill args are frame junk.
// =============================================================================
void aiactor_behavior_alerted_tick(void)
{
    int actor = aiactor_current_frame_actor();
    int rec = *(int *)(actor + 0x7c);
    aiactor_retarget_visible_teammate();
    ai_preamble(rec, FLOAT_80619d80, DOUBLE_80619d90);

    int los = aiactor_evaluate_target_los();
    int state = *(int *)(rec + 0x20);
    if (state == 2) {
        if (anim_event_scan_active() == 0) {
            *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) & 0xfffffffd;
        } else {
            *(unsigned int *)(rec + 0x2c) = *(unsigned int *)(rec + 0x2c) | 0x400;
            object_melee_thrown_fire(0, 0, actor, 1, 0, 0);
            *(unsigned int *)(rec + 0x2c) = *(unsigned int *)(rec + 0x2c) & 0xfffffbff;
        }
        if (anim_active_slot_userflags(*(int *)(actor + 4)) == 0) {
            if (((*(unsigned int *)(*(int *)(rec + 0xbf0) + 0x58) & 0x80) != 0) &&
                (anim_event_scan_active() != 0))
                gib_find_nearest_pickup(0, 0, 0, 0, 0, 0, 0, 0, actor);
        } else {
            ai_set_mode_alert(rec, 0x65);
        }
    } else if (state < 2) {
        if (state == 0) {
            if (anim_get_descriptor((int)*(short *)(*(int *)(rec + 0xbf0) + 0x48)) != 0) {
                double clipTime = (double)FLOAT_80619d80;
                if (anim_is_clip_active(actor, (int)*(short *)(*(int *)(rec + 0xbf0) + 0x48)) != 0)
                    clipTime = (double)*(float *)(*(int *)(actor + 4) + 0xd4);
                anim_play_scaled((double)FLOAT_80619d80, (double)FLOAT_80619d84);
                *(float *)(*(int *)(actor + 4) + 0xd4) = (float)clipTime;
                *(float *)(actor + 0x70) = FLOAT_80619d80;
                *(float *)(actor + 0xc4) = FLOAT_80619d80;
                ai_set_mode_alert(rec, 1);
            }
        } else if (-1 < state) {
            bool alert = false;
            if (aiactor_alert_group_flagA_any(*(unsigned int *)(*(int *)(rec + 0xbf0) + 0x70)) == 0) {
                if (los == 1)
                    alert = true;
                else if (((*(unsigned int *)(*(int *)(rec + 0xbf0) + 0x58) & 0x20) != 0) && (los == 2))
                    alert = true;
            } else {
                alert = true;
            }
            int a = entity_handle_resolve(0x4f544834);
            if ((a != 0) && (entity_handle_resolve(0x41544352) != 0)) {
                int b = entity_handle_resolve(0x41544352);
                *(int *)(*(int *)(b + 4) + 0xd4) = *(int *)(*(int *)(a + 4) + 0xd4);
            }
            if (alert) {
                if (*(short *)(*(int *)(rec + 0xbf0) + 0x4c) != -1)
                    anim_play_scaled((double)FLOAT_80619d88, (double)FLOAT_80619d84);
                aiactor_alert_broadcast_group(0, actor, *(int *)(rec + 0x678));
                ai_set_mode_alert(rec, 2);
            }
        }
    } else if (state == 0x65) {
        if (*(short *)(*(int *)(rec + 0xbf0) + 0x4c) != -1) {
            if (anim_primary_clip_time_left((double)FLOAT_80619d88, *(int *)(actor + 4)) == 0)
                return;
        }
        if ((anim_has_event_with_mask((int)*(short *)(*(int *)(rec + 0xbf0) + 0x4c), 0x8000) != 0) ||
            (anim_has_event_with_mask((int)*(short *)(*(int *)(rec + 0xbf0) + 0x48), 0x8000) != 0))
            gib_find_nearest_pickup(0, 0, 0, 0, 0, 0, 0, 0, actor);
        if ((*(int *)(rec + 0x94) == 0) && (*(short *)(*(int *)(rec + 0xbf0) + 0x18) != -1)) {
            int list = match_build_weapon_list();
            *(int *)(rec + 0x94) = *(int *)(list + *(short *)(*(int *)(rec + 0xbf0) + 0x18) * 4 + 8);
            int def = DAT_80610ba0 + (&DAT_8043e23c)[*(int *)(rec + 0x94) * 0xf] * 0x29c;
            object_sound_slot_accumulate(rec, *(int *)(def + 0x14), 1000);
            object_sound_slot_accumulate(rec, *(int *)(def + 0x1c), 10);
        }
        if (*(int *)(rec + 0xac4) == 0 && *(int *)(rec + 0xac0) == 0) {
            if (ai_queue(rec, 0xaac, 0xaa8, 4))
                return;
        } else {
            if (*(int *)(rec + 0xaac) == 1 && *(int *)(rec + 0xaa8) == 0) {
                unsigned int gate;
                if (DAT_805015bd == '\n') gate = *(unsigned int *)(rec + 0x14);
                else                       gate = *(unsigned int *)(rec + 0x1c);
                if ((gate & 1) == 0) return;
            }
            *(int *)(rec + 0xaac) = *(int *)(rec + 0xac4);
            *(int *)(rec + 0xaa8) = *(int *)(rec + 0xac0);
        }
    } else if (((state < 0x65) && (99 < state)) &&
               (*(int *)(DAT_80611824 + (short)(&DAT_804d1cd0)[*(short *)(*(int *)(rec + 0xbf0) + 0x48)] * 4) != 0) &&
               (state != 0)) {
        *(int *)(rec + 0x9d4) = state;
        *(float *)(rec + 0x9d8) = FLOAT_80619d80;
        *(int *)(rec + 0x20) = 0;
        *(int *)(rec + 0x9d0) = *(int *)(rec + 0x20);
    }
}

// =============================================================================
// 0x802c3b38  aiactor_behavior_teleport_in_tick
// Spawn / teleport-in behaviour: state 0 init model anim + activate + optional
// co-op sync; state 1 hold; state 2 activate-all-valid + destroy self instance.
// Actor is the last integer parameter (param_9).
// =============================================================================
void aiactor_behavior_teleport_in_tick(unsigned long long a, double b, double c, double d,
                                       unsigned long long e, unsigned long long f,
                                       unsigned long long g, unsigned long long h, int actor)
{
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h;
    int rec = *(int *)(actor + 0x7c);
    aiactor_retarget_visible_teammate();
    ai_preamble(rec, FLOAT_80619d80, DOUBLE_80619d90);

    *(unsigned int *)(actor + 0x10) = *(unsigned int *)(actor + 0x10) | 1;
    *(float *)(rec + 0xb3c) = FLOAT_80619d80;

    int state = *(int *)(rec + 0x20);
    if (state == 1) {
        *(int *)(actor + 0x34) = 0xd5;
        if ((anim_active_slot_userflags(*(int *)(actor + 4)) != 0) && (*(int *)(rec + 0x20) != 2))
            ai_set_mode_alert(rec, 2);
    } else if (state < 1) {
        if (-1 < state) {
            level_get_current_id();
            if ((level_actor_check_active(0) == 0) &&
                (anim_get_descriptor((int)*(short *)(*(int *)(rec + 0xbf0) + 0x48)) != 0)) {
                int lvl = level_get_current_id();
                if (level_actor_get_load_status(lvl) == 0) {
                    model_instance_anim_init((double)FLOAT_80619d80, (double)FLOAT_80619d84, actor);
                    *(unsigned int *)(rec + 0x2c) = *(unsigned int *)(rec + 0x2c) | 2;
                    if (rec == aiactor_find_flagged_bot())
                        aiactor_deactivate_all_idle();
                    if (player_slot_get_entity_id(DAT_80611d54) != 0x1b)
                        return;
                    *(float *)(*(int *)(actor + 4) + 0xd4) =
                        FLOAT_806108ec *
                        (FLOAT_80619d84 / (float)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80619d90)) +
                        *(float *)(DAT_80611d54 + 0xfd4);
                    if (*(int *)(rec + 0x20) == 1)
                        return;
                    ai_set_mode_alert(rec, 1);
                    return;
                }
            }
            *(unsigned int *)(actor + 0x10) = *(unsigned int *)(actor + 0x10) & 0xfffffffe;
        }
    } else if (state < 3) {
        aiactor_activate_all_valid();
        aiactor_destroy_instance(0, 0, 0, 0, 0, 0, 0, 0, actor);
        if (((DAT_805015c0 & 0x200) != 0) && (DAT_80611d54 != 0))
            aiactor_coop_hook_stub();
    }
}

// =============================================================================
// 0x802c3dac  aiactor_coop_hook_stub
// Empty co-op synchronisation hook (no-op in this build).
// =============================================================================
void aiactor_coop_hook_stub(void)
{
    return;
}

// =============================================================================
// 0x802c3db0  aiactor_find_flagged_bot
// Return the first bot slot (>= DAT_80612610) whose flags (entity+0x14) have bit
// 0x200000 set, or 0 if none.
// =============================================================================
int aiactor_find_flagged_bot(void)
{
    int off = DAT_80612610 << 2;
    int remaining = DAT_80612098 - DAT_80612610;
    if (DAT_80612610 < DAT_80612098) {
        do {
            int ent = *(int *)((int)&DAT_80588610 + off);
            if ((*(unsigned int *)(ent + 0x14) & 0x200000) != 0)
                return ent;
            off = off + 4;
            remaining = remaining - 1;
        } while (remaining != 0);
    }
    return 0;
}

} // extern "C"
