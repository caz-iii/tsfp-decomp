// =============================================================================
// aiactor_behavior_special.cpp - AI actor rally-marker system + scripted boss FSMs
//
// Continuation of the aiactor behaviour-state family. Two concerns live here:
//
//  (A) the "rally marker" system. A commanding actor can drop a marker entity in
//      a sound-pad zone; nearby hostile-related bots then path toward it. The
//      live markers are held in parallel arrays:
//        DAT_805cbcd0[i]  marker entity handle     DAT_805cbec8[i]  its zone id
//        DAT_80612a98     live marker count (cap 0x1e)
//      Functions: spawn (0x802bdfc8), rally-all (0x802be128), seek (0x802be2a4),
//      remove/compact (0x802be43c), the goto-marker tick (0x802be4d4), and the
//      "all zone targets inactive" predicate (0x802bdf68).
//
//  (B) two large scripted boss movement FSMs that teleport a boss between
//      sound-pad zones and drive its clip playback:
//        aiactor_behavior_boss_scripted_tick (0x802be860): 15 states (0..0xe),
//          includes a physics rope-swing toward the grappled entity at the
//          shared grapple context DAT_806128c0.
//        aiactor_behavior_worm_boss_tick (0x802c01d8): 9 states (0..8), the
//          "sprg" runner boss that flees, leaps, and throws; its leap-landing
//          handler is worm_boss_leap_land (0x802c1520).
//      Plus aiactor_find_nearby_special_actor (0x802c163c) - scan the special
//      actor array (DAT_805887d0) for a state-5 actor within range.
//
// AI actor control record at (actor+0x7c) ("rec"), viewed int-indexed (rec[N])
// and byte-offset (rec+0xNNN). Key fields used here mirror the sibling files:
//   rec+0x20 mode  rec+0x9d0 queued  rec+0x9d4 prev  rec+0x9d8 timer
//   rec+0x684 fallback zone  rec+0x678 zone id  rec+0x08 boss-variant selector
//   rec+0xaa0 script step index  rec+0x9dc script sequence id
//   rec+0x1cc/0x1d8 move-speed fields  rec+0x1a4 blend/health-frac
//
// Address range: 0x802bdf68 - 0x802c1793
// Subsystem: game (AI actor behaviour - rally markers + scripted boss FSMs)
//
// NON-MATCHING: PPC frame-enter register pair -> aiactor_current_frame_actor();
// the incoming FP-register spill args on the boss ticks are Ghidra frame junk and
// are ignored (movement helpers re-read them from the record); CONCAT44 int->double
// bias idioms reduced to plain float conversions; the state-transition bookkeeping
// is written inline to stay faithful to the original per-state code.
// =============================================================================

#include "common.h"

extern "C" {

// --- level / global state (raw symbols) --------------------------------------
extern char DAT_805015bd;          // game/level mode selector
extern char DAT_805015be;          // sub-mode
extern int  DAT_80473798;          // boss zone-sequence table (state 4/5)
extern int  DAT_80473828;          // boss spawn zone table (state 1)
extern int  DAT_8047383c;          // boss patrol zone table (state 1)
extern int  DAT_80473884;          // boss zone-sequence table (state 0xc/0xd)
extern int  DAT_8047389c;          // boss clip table (state 3)
extern int  DAT_80473988;          // worm-boss spawn zone table
extern int  DAT_80473998;          // worm-boss flee zone table
extern int  DAT_805887d0;          // special-actor pointer array (stride 4)
extern int  DAT_80588610;          // active-actor pointer array (stride 4)
extern int  DAT_803e07d8;          // scratch vec (swim/probe)
extern int  DAT_803e07e4;          // scratch vec (state 9 approach)
extern int  DAT_803e07e8;
extern int  DAT_803e07ec;
extern int  DAT_803e07f0;          // scratch vec (leap-land explosion)
extern int  DAT_803e07f4;
extern int  DAT_803e07f8;
extern int  DAT_805cbccc;          // marker array back-pad (swap source, entity)
extern int  DAT_805cbcd0;          // rally marker entity array (stride 4)
extern int  DAT_805cbec4;          // marker array back-pad (swap source, zone)
extern int  DAT_805cbec8;          // rally marker zone-id array (stride 4)
extern int  DAT_80611b04;          // scene node -> collision block table
extern int  DAT_80611b8c;          // sound-pad zone table base (stride 0x20)
extern int  DAT_80611d74;          // local-player input handle ptr
extern int  DAT_80611d78;          // world/level context base
extern int  DAT_80611d90;          // actor record array base (stride 0xea0)
extern int  DAT_80612098;          // active actor count
extern int  DAT_806120ec;          // special-actor count
extern int  DAT_806120f8;          // round-robin scan cursor
extern int  DAT_80612610;          // first bot slot index
extern int  DAT_80612a98;          // live rally-marker count
extern int  DAT_80612a9c;          // grapple "held" flag
extern int  DAT_806128c0;          // grapple/swing context base
extern int  DAT_806109c0;          // frame-delta raw bits

// float / double literal pool
extern float FLOAT_806108ec;
extern float FLOAT_80619c34;
extern float FLOAT_80619c38;
extern float FLOAT_80619c48;
extern float FLOAT_80619c4c;   // 0.0f
extern float FLOAT_80619c50;   // 1.0f
extern float FLOAT_80619c58;
extern float FLOAT_80619c5c;
extern float FLOAT_80619c64;
extern float FLOAT_80619c74;
extern float FLOAT_80619c78;
extern float FLOAT_80619c7c;
extern float FLOAT_80619c80;
extern float FLOAT_80619c90;
extern float FLOAT_80619c9c;
extern float FLOAT_80619cac;
extern float FLOAT_80619cb4;
extern float FLOAT_80619cb8;
extern float FLOAT_80619cec;
extern float FLOAT_80619cf0;
extern float FLOAT_80619cf4;
extern float FLOAT_80619cf8;
extern float FLOAT_80619cfc;
extern float FLOAT_80619d00;
extern float FLOAT_80619d04;
extern float FLOAT_80619d08;
extern float FLOAT_80619d0c;
extern float FLOAT_80619d10;
extern float FLOAT_80619d14;
extern float FLOAT_80619d18;
extern float FLOAT_80619d1c;
extern float FLOAT_80619ce4;
extern float FLOAT_80619ce8;
extern double DOUBLE_80619c40;
extern double DOUBLE_80619c68;

// --- named helpers (file-local, C-linkage, matched to the decompiler) --------
extern int  aiactor_retarget_visible_teammate(void);
extern void aiactor_retarget_scan(void);
extern int  aiactor_follow_zone_path(void);
extern void aiactor_set_stance_flags(int actor, int flags);
extern unsigned long long aiactor_turn_toward_leader(void);
extern void aiactor_turn_toward_entity(int actor, int target, int instant);
extern void aiactor_turn_toward_position(int actor, int *pos, int instant);
extern unsigned long long aiactor_facing_update(void);
extern void aiactor_movement_tick(unsigned long long a, double b, double c, double d,
                                  double e, double f, double g, unsigned long long h);
extern int  aiactor_relation_entry(int a, int b);
extern int  aiactor_get_active_index(void);
extern int *aiactor_get_scratch_slot(int idx);
extern void aiactor_update_zone_anchor_position(void);
extern void aiactor_compute_flee_target(int actor, float *outPos);
extern bool aiactor_aim_alignment_test(double threshold, int actor, float *targetPos);
extern int  soundpad_find_zone_by_id(int id);
extern int  soundpad_find_nearest_entity_in_zone(double radius, float *pos, int zone);
extern int  soundpad_resolve_entity_to_zone(int entity_id);
extern unsigned int spawn_create_instance(double a1, double a2, double a3, double a4,
        unsigned long long a5, unsigned long long a6, unsigned long long a7, unsigned long long a8,
        int a9, int a10, int a11, int a12, void *pPos, unsigned int *a14, int a15, int a16,
        int a17, int a18);
extern void player_ai_teleport_wrapper(int actor, int zone, int flags);
extern void object_set_weapon_slot(int object, int slot);
extern void player_ai_explosion_spawn(int a, int b, float *pos, float *nrm, int ent, int room, int snd);
extern void particle_kill_by_entity(void);
extern void prop_explode_tick(void);
extern void pickup_blink_start(double vis, double hid, int pData);
extern int  entity_handle_resolve(unsigned int id);
extern int  worm_sprg_select_runner(void);
extern int  worm_asl_obj_dec_active(void);
extern unsigned int subobj_is_idle(unsigned int handle);
extern void subobj_set_material_and_mass(void);
extern unsigned int input_get_buttons_held(int player);
extern void object_melee_thrown_fire(unsigned long long a, double b, int actor, int c, int d, int e);
extern void sound_init_play_params(int params_ptr);
extern unsigned int sound_play_request(int *params);
extern void *scene_node_get_texture_data(int node, int mode);
extern int  scene_node_get_material_count(int node, int mode);
extern float floor_polygon_search(int a, void *b, unsigned int mask, float *pos, unsigned int *out);
extern int  anim_is_clip_active(int actor, int clip);
extern int  anim_active_slot_userflags(int slot);
extern int  anim_primary_clip_userflags(int actor, int clip);
extern unsigned long long anim_play_scaled(double a, double b);
extern void anim_set_blend_weight(double weight, int animState);
extern void anim_set_playback_rate(double r, int *slot, int a);
extern unsigned int rng_counter_tick(void);
extern double math_sqrt(double x);

// --- PPC frame-context intrinsic model ---------------------------------------
extern int  aiactor_current_frame_actor(void);

// forward decls
void aiactor_spawn_rally_marker(int actor, int zone);
void worm_boss_leap_land(int actor, int entity);

// Ghidra SQRT intrinsic (single-precision). Self-contained Newton-Raphson to
// avoid a <cmath> include that conflicts with the project's own math headers.
static inline float SQRT(float x) {
    if (x <= 0.0f) return 0.0f;
    float g = x;
    for (int i = 0; i < 6; i = i + 1) g = 0.5f * (g + x / g);
    return g;
}

// rng in [0,1): (float)((unsigned)rng - DOUBLE_bias) / FLOAT_scale
static float ai_rand01(void)
{
    unsigned int r = rng_counter_tick();
    return (float)((double)(unsigned int)r - DOUBLE_80619c40) / FLOAT_80619c34;
}

// queued-mode consume + aim-timer decay preamble.
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

// Set queued mode (rec+0x9d0) with prev/timer bookkeeping.
static void ai_queue_mode(int rec, int newMode)
{
    if (*(int *)(rec + 0x20) == newMode)
        return;
    *(int *)(rec + 0x9d4) = *(int *)(rec + 0x20);
    *(float *)(rec + 0x9d8) = FLOAT_80619c4c;
    *(int *)(rec + 0x20) = newMode;
    *(int *)(rec + 0x9d0) = *(int *)(rec + 0x20);
}

// Latch-checked secondary-action queue (returns 1 = caller should bail).
static int ai_queue_secondary(int rec, int code)
{
    if (*(int *)(rec + 0xaac) == 1 && *(int *)(rec + 0xaa8) == 0) {
        unsigned int gate;
        if (DAT_805015bd == '\n') gate = *(unsigned int *)(rec + 0x14);
        else                       gate = *(unsigned int *)(rec + 0x1c);
        if ((gate & 1) == 0) return 1;
    }
    *(int *)(rec + 0xaac) = code;
    *(int *)(rec + 0xaa8) = 0;
    return 0;
}

// =============================================================================
// 0x802bdf68  aiactor_all_zone_targets_inactive
// Returns 1 if none of the actor's scratch-slot zone targets is active, else 0.
// =============================================================================
int aiactor_all_zone_targets_inactive(void)
{
    int i = 0;
    while (true) {
        if (aiactor_get_active_index() <= i)
            return 1;
        int *slot = aiactor_get_scratch_slot(i);
        if ((*(unsigned int *)(slot[1] + 0x28) & 1) != 0)
            break;
        i = i + 1;
    }
    return 0;
}

// =============================================================================
// 0x802bdfc8  aiactor_spawn_rally_marker
// Spawn a marker instance at a sound-pad zone (nearest zone if zone == -1),
// with a randomized height, and record it in the rally-marker arrays.
// =============================================================================
void aiactor_spawn_rally_marker(int actor, int zone)
{
    extern int FUN_803660e0(void); // marker-system enabled gate (external)
    if (FUN_803660e0() == 0)
        return;
    if (zone == -1)
        zone = soundpad_find_nearest_entity_in_zone((double)FLOAT_80619c4c,
                                                    (float *)(actor + 100),
                                                    *(int *)(actor + 0x34));
    if (zone == -1)
        return;

    float pos[3];  // local_30/2c/28
    int zt = DAT_80611b8c + zone * 0x20;
    pos[0] = *(float *)(zt + 0xc) + (FLOAT_80619c48 * ai_rand01() - FLOAT_80619cb8);
    pos[1] = *(float *)(zt + 0x10);
    float r2 = ai_rand01();
    float dz = (float)((double)FLOAT_80619c48 * (double)r2 - (double)FLOAT_80619cb8);
    pos[2] = (float)((double)*(float *)(DAT_80611b8c + zone * 0x20 + 0x14) + (double)dz);

    unsigned int marker = spawn_create_instance((double)dz, (double)FLOAT_80619c48, (double)r2,
                                                0, 0, 0, 0, 0, 1, 0x400, 0, 0, &pos[0],
                                                (unsigned int *)0, 0, 0, 0, 0);
    int idx = DAT_80612a98;
    (&DAT_805cbcd0)[idx] = (int)marker;
    (&DAT_805cbec8)[idx] = zone;
    *(int *)(*(int *)((&DAT_805cbcd0)[idx] + 0x7c) + 0x38) = zone;
    DAT_80612a98 = DAT_80612a98 + 1;
}

// =============================================================================
// 0x802be128  aiactor_rally_squad_to_zone
// Drop a rally marker at the actor's zone, then order every hostile-related bot
// to path toward it (set follow target + fallback zone + queue action 0x20000).
// =============================================================================
void aiactor_rally_squad_to_zone(void)
{
    int actor = aiactor_current_frame_actor();
    int rec = *(int *)(actor + 0x7c);
    if (DAT_80612a98 < 0x1e) {
        aiactor_spawn_rally_marker(actor, *(int *)(rec + 0x678));
        int off = DAT_80612610 * 0xea0;
        for (int i = DAT_80612610; i < DAT_80612098; i = i + 1) {
            if (*(int *)(rec + 8) != i) {
                char *rel = (char *)aiactor_relation_entry(*(int *)(rec + 8), i);
                if (*rel != '\0') {
                    int target = DAT_80611d90 + off;
                    if ((*(unsigned int *)(target + 0x24) & 8) == 0) {
                        *(int *)(target + 0xaf0) = (&DAT_805cbccc)[DAT_80612a98];
                        if (*(int *)(rec + 0x678) < 0)
                            *(int *)(target + 0x684) =
                                *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                        else
                            *(int *)(target + 0x684) = *(int *)(rec + 0x678);
                        *(int *)(target + 0x9ac) = 0;
                        *(int *)(target + 0x9a8) = 0;
                        ai_queue_secondary(target, 0x20000);
                    }
                }
            }
            off = off + 0xea0;
        }
    }
}

// =============================================================================
// 0x802be2a4  aiactor_seek_rally_marker
// If the actor's mode flags permit (bits 0xc), scan the rally-marker array for
// one in the same room and, if found, adopt its zone as the zone-path goal and
// queue action 0x20000. Returns 1 if it committed.
// =============================================================================
int aiactor_seek_rally_marker(int actor)
{
    int *rec = *(int **)(actor + 0x7c);
    if ((DAT_805015be != '\n') && (rec[2] != DAT_806120f8))
        return 0;

    unsigned int flags;
    if (DAT_805015bd == '\n') flags = rec[5];
    else                       flags = rec[7];
    if ((flags & 0xc) == 0)
        return 0;

    int off = 0;
    for (int i = 0; i < DAT_80612a98; i = i + 1) {
        int marker = *(int *)((int)&DAT_805cbcd0 + off);
        if (((marker != 0) && (*(int *)(marker + 0x7c) != 0)) &&
            (*(int *)(*rec + 0x34) == *(int *)(marker + 0x34)) &&
            (rec[0x19e] != *(int *)(*(int *)(marker + 0x7c) + 0x38))) {
            rec[700] = marker;
            int zone = *(int *)(*(int *)(*(int *)((int)&DAT_805cbcd0 + off) + 0x7c) + 0x38);
            if (zone < 0)
                rec[0x1a1] = *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            else
                rec[0x1a1] = zone;
            rec[0x26b] = 0;
            rec[0x26a] = 0;
            if (ai_queue_secondary((int)rec, 0x20000))
                return 1;
            return 1;
        }
        off = off + 4;
    }
    return 0;
}

// =============================================================================
// 0x802be43c  aiactor_remove_rally_marker
// Find `marker` in the array, swap-remove it (compact with the last element),
// decrement the count, and return its zone id (or -1 if not found).
// =============================================================================
int aiactor_remove_rally_marker(int marker)
{
    int found = -1;
    int off = 0;
    for (int i = 0; i < DAT_80612a98; i = i + 1) {
        if (*(int *)((int)&DAT_805cbcd0 + off) == marker) {
            found = i;
            break;
        }
        off = off + 4;
    }
    if (found != -1) {
        int zone = (&DAT_805cbec8)[found];
        (&DAT_805cbcd0)[found] = (&DAT_805cbccc)[DAT_80612a98];
        (&DAT_805cbec8)[found] = *(int *)(&DAT_805cbec4 + DAT_80612a98 * 4);
        DAT_80612a98 = DAT_80612a98 - 1;
        return zone;
    }
    return -1;
}

// =============================================================================
// 0x802be4d4  aiactor_behavior_goto_marker_tick
// Behaviour tick that moves toward a rally-marker zone (mode 0 -> 1): follow the
// zone path, aim, and queue the appropriate action codes.
// =============================================================================
void aiactor_behavior_goto_marker_tick(void)
{
    extern int DAT_804d1cd0;   // clip -> attr index table (short)
    extern int DAT_804d34dc;   // clip attr table (stride 8)
    int actor = aiactor_current_frame_actor();
    int rec = *(int *)(actor + 0x7c);
    int hasTeammate = aiactor_retarget_visible_teammate();
    ai_tick_preamble(rec);

    if (*(int *)(rec + 8) == DAT_806120f8)
        aiactor_retarget_scan();
    if ((hasTeammate != 0) || (FLOAT_80619c64 <= *(float *)(rec + 0xb34))) {
        if (hasTeammate != 0)
            *(float *)(rec + 0xb34) = FLOAT_80619c4c;
    } else {
        hasTeammate = 1;
    }

    int mode = *(int *)(rec + 0x20);
    if (mode == 1) {
        if (aiactor_follow_zone_path() != 0) {
            *(int *)(rec + 0xaf0) = 0;
            ai_queue_secondary(rec, 0x10);
            return;
        }
        aiactor_set_stance_flags(actor, 1);
        if ((*(int *)(rec + 4) == 0) ||
            (!aiactor_aim_alignment_test((double)*(float *)(rec + 0x9bc), actor,
                                         (float *)(*(int *)(rec + 4) + 100)))) {
            *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) | 0x40000;
        } else if (hasTeammate != 0) {
            *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) | 2;
            *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) & 0xfffbffff;
        }
        int attr = *(int *)(&DAT_804d34dc +
            (short)(&DAT_804d1cd0)[*(int *)(*(int *)(actor + 4) + 0xcc)] * 8);
        if ((attr & 0x100) != 0) {
            *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) & 0xfffbffff;
            if (((*(int **)(actor + 4))[0x32] & 1) != 0)
                anim_set_playback_rate((double)FLOAT_80619c58, *(int **)(actor + 4), 2);
        }
    } else if ((mode < 1) && (-1 < mode)) {
        aiactor_set_stance_flags(actor, 1);
        *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) | 0x20000;
        ai_queue_mode(rec, 1);
    }

    if ((*(unsigned int *)(rec + 0x28) & 1) != 0) {
        int codeA, codeB;
        if (DAT_805015bd == '\n') { codeA = *(int *)(rec + 0x10); codeB = *(int *)(rec + 0x14); }
        else                       { codeA = *(int *)(rec + 0x18); codeB = *(int *)(rec + 0x1c); }
        *(int *)(rec + 0xabc) = codeB;
        *(int *)(rec + 0xab8) = codeA;
        ai_queue_secondary(rec, 0x20);
    }
}

// =============================================================================
// 0x802be860  aiactor_behavior_boss_scripted_tick
// 15-state (0..0xe) scripted boss movement FSM: teleports between sound-pad
// zones, drives clip playback, and (states 6-9) performs a physics rope-swing
// toward the grappled entity at DAT_806128c0. The FP-register spill args are
// frame junk (movement helpers re-read from the record).
// =============================================================================
void aiactor_behavior_boss_scripted_tick(void)
{
    int actor = aiactor_current_frame_actor();
    int rec = *(int *)(actor + 0x7c);
    (void)aiactor_retarget_visible_teammate();
    ai_tick_preamble(rec);

    int state = *(int *)(rec + 0x20);
    switch (state) {
    case 0:
        break;
    case 1: {
        int spawnIdx = (int)(FLOAT_80619c78 * ai_rand01() + FLOAT_80619c48);
        int patrolIdx = (int)(FLOAT_80619ce4 * ai_rand01() + FLOAT_80619c48);
        int clipIdx = (int)(FLOAT_80619c74 * ai_rand01() + FLOAT_80619c48);
        *(float *)(actor + 0x1d8) = FLOAT_80619c5c;
        *(int *)(actor + 0x1cc) = *(int *)(actor + 0x1d8);
        *(int *)(rec + 0xaa0) = clipIdx;
        int spawnZone = soundpad_find_zone_by_id(*(int *)(&DAT_80473828 + DAT_80611d78 * 4));
        player_ai_teleport_wrapper(rec, spawnZone, 1);
        object_set_weapon_slot(rec, 0);
        int patrolZoneId = -1;
        int patrolZone = -1;
        while (true) {
            if (patrolZoneId == -1) {
                patrolZoneId = *(int *)(&DAT_8047383c + patrolIdx * 4);
                patrolZone = soundpad_find_zone_by_id(patrolZoneId);
                int off = 0;
                int reoccupied = 0;
                for (int i = 0; i < DAT_80612098; i = i + 1) {
                    int other = *(int *)((int)&DAT_80588610 + off);
                    int cA, cB;
                    if (DAT_805015bd == '\n') { cA = *(int *)(other + 0x10); cB = *(int *)(other + 0x14); }
                    else                       { cA = *(int *)(other + 0x18); cB = *(int *)(other + 0x1c); }
                    if ((cB != 0 || cA != 0) && (*(int *)(other + 0x684) == patrolZone)) {
                        patrolZoneId = -1;
                        patrolIdx = (int)(FLOAT_80619ce4 * ai_rand01() + FLOAT_80619c48);
                        reoccupied = 1;
                        break;
                    }
                    off = off + 4;
                }
                if (reoccupied)
                    continue;
                // fallthrough to commit
            }
            if (patrolZone < 0)
                *(int *)(rec + 0x684) =
                    *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            else
                *(int *)(rec + 0x684) = patrolZone;
            *(int *)(rec + 0x9ac) = 0;
            *(int *)(rec + 0x9a8) = 0;
            aiactor_update_zone_anchor_position();
            aiactor_set_stance_flags(actor, 1);
            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c9c);
            if (*(int *)(rec + 0x20) != 2)
                *(int *)(rec + 0x9d0) = 2;
            break;
        }
        break;
    }
    case 2:
        if (anim_is_clip_active(actor, 0x26) == 0)
            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c9c);
        if ((aiactor_follow_zone_path() != 0) && (*(int *)(rec + 0x20) != 3))
            *(int *)(rec + 0x9d0) = 3;
        *(float *)(rec + 0x9e0) = FLOAT_80619c50;
        *(float *)(rec + 0x9dc) = FLOAT_80619c48;
        aiactor_movement_tick(aiactor_facing_update(), 0, 0, 0, 0, 0, 0, 0);
        aiactor_facing_update();
        break;
    case 3: {
        *(unsigned int *)(actor + 0x28) = *(unsigned int *)(actor + 0x28) | 0x70;
        int clip = *(int *)(&DAT_8047389c + *(int *)(rec + 0xaa0) * 4);
        if (anim_is_clip_active(actor, clip) == 0)
            anim_play_scaled((double)FLOAT_80619c4c, (double)(FLOAT_80619c9c * *(float *)(rec + 0x9e0)));
        // decay the swing amplitude toward zero, then invert
        float delta = FLOAT_806108ec *
            (FLOAT_80619c50 / (float)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80619c68));
        *(float *)(rec + 0x9dc) = *(float *)(rec + 0x9dc) - delta / FLOAT_80619ce8;
        if (*(float *)(rec + 0x9dc) <= FLOAT_80619c4c) {
            *(float *)(rec + 0x9e0) = -(FLOAT_806108ec *
                (FLOAT_80619c50 / (float)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80619c68))
                - *(float *)(rec + 0x9e0));
            *(float *)(rec + 0x9dc) = FLOAT_80619c48;
            anim_set_blend_weight((double)(FLOAT_80619c9c * *(float *)(rec + 0x9e0)), *(int *)(actor + 4));
        }
        if ((double)*(float *)(rec + 0x9e0) <= (double)FLOAT_80619cec) {
            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c50);
            if (*(int *)(rec + 0x20) != 0)
                *(int *)(rec + 0x9d0) = 0;
            worm_asl_obj_dec_active();
        }
        aiactor_movement_tick(0, 0, 0, 0, 0, 0, 0, 0);
        aiactor_facing_update();
        break;
    }
    case 4: {
        int seqIdx = (int)(FLOAT_80619cac * ai_rand01() + FLOAT_80619c48);
        if (*(int *)(rec + 0x20) != 5)
            *(int *)(rec + 0x9d0) = 5;
        *(int *)(rec + 0xaa0) = 0;
        int seqOff = seqIdx * 0x18;
        *(float *)(actor + 0x1d8) = FLOAT_80619c5c;
        *(int *)(actor + 0x1cc) = *(int *)(actor + 0x1d8);
        int z = soundpad_find_zone_by_id(*(int *)(&DAT_80473798 + seqOff));
        if (z < 0)
            *(int *)(rec + 0x684) =
                *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        else
            *(int *)(rec + 0x684) = soundpad_find_zone_by_id(*(int *)(&DAT_80473798 + seqOff));
        *(int *)(rec + 0x9ac) = 0;
        *(int *)(rec + 0x9a8) = 0;
        int tz = soundpad_find_zone_by_id(*(int *)(&DAT_80473798 + seqOff));
        player_ai_teleport_wrapper(rec, tz, 1);
        *(int *)(rec + 0x9dc) = seqIdx;
        object_set_weapon_slot(rec, 0);
        aiactor_update_zone_anchor_position();
        aiactor_set_stance_flags(actor, 1);
        anim_play_scaled((double)FLOAT_80619c4c, (double)(FLOAT_80619cb4 * ai_rand01() + FLOAT_80619cf0));
        break;
    }
    case 5: {
        aiactor_movement_tick(0, 0, 0, 0, 0, 0, 0, 0);
        if (aiactor_follow_zone_path() == 0) {
            if (anim_active_slot_userflags(*(int *)(actor + 4)) != 0) {
                double rate = (double)(FLOAT_80619cb4 * ai_rand01() + FLOAT_80619cf0);
                if (*(int *)(actor + 0x20c) == 0)
                    anim_play_scaled((double)FLOAT_80619c7c, rate);
                else
                    anim_play_scaled((double)FLOAT_80619c4c, rate);
            }
        } else {
            *(int *)(rec + 0xaa0) = *(int *)(rec + 0xaa0) + 1;
            int nextZoneId = *(int *)(&DAT_80473798 +
                *(int *)(rec + 0x9dc) * 0x18 + *(int *)(rec + 0xaa0) * 4);
            if (nextZoneId == -1) {
                if (*(int *)(rec + 0x20) != 0)
                    *(int *)(rec + 0x9d0) = 0;
                particle_kill_by_entity();
                *(unsigned int *)(actor + 0x28) = *(unsigned int *)(actor + 0x28) & 0xffffff9e;
                if (*(int *)(actor + 0x20c) != 0) {
                    particle_kill_by_entity();
                    int tp = *(int *)(actor + 0x20c);
                    *(unsigned int *)(tp + 0x28) = *(unsigned int *)(tp + 0x28) & 0xffffff9e;
                    *(int *)(tp + 0x78) = 0;
                    *(int *)(tp + 0x128) = 0;
                    *(int *)(tp + 300) = -1;
                    *(int *)(actor + 0x20c) = 0;
                }
            } else {
                int z = soundpad_find_zone_by_id(nextZoneId);
                if (z < 0)
                    *(int *)(rec + 0x684) =
                        *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    *(int *)(rec + 0x684) = soundpad_find_zone_by_id(nextZoneId);
                *(int *)(rec + 0x9ac) = 0;
                *(int *)(rec + 0x9a8) = 0;
            }
        }
        aiactor_movement_tick(0, 0, 0, 0, 0, 0, 0, 0);
        aiactor_facing_update();
        break;
    }
    case 6: {
        // begin swing: snap to a start zone based on boss variant
        float d0 = *(float *)(actor + 0x68) - *(float *)(DAT_80611d78 + 0xac);
        float d1 = *(float *)(actor + 100) - *(float *)(DAT_80611d78 + 0xa8);
        float d2 = *(float *)(actor + 0x6c) - *(float *)(DAT_80611d78 + 0xb0);
        double dist = math_sqrt((double)(d2 * d2 + d1 * d1 + d0 * d0));
        *(float *)(actor + 0x1cc) = FLOAT_80619c5c;
        *(float *)(actor + 0x1d8) = FLOAT_80619c5c;
        *(unsigned int *)(actor + 0x28) = *(unsigned int *)(actor + 0x28) | 1;
        *(unsigned int *)(actor + 0x28) = *(unsigned int *)(actor + 0x28) & 0xffffff9f;
        *(float *)(actor + 0x1a4) = FLOAT_80619c50;
        object_set_weapon_slot(rec, 0);
        int z;
        if (*(int *)(rec + 8) == 1) z = soundpad_find_zone_by_id(0x41e);
        else                         z = soundpad_find_zone_by_id(0x41f);
        player_ai_teleport_wrapper(rec, z, 1);
        anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c50);
        int grabbed = *(int *)(DAT_80611d78 + 8);
        int anchor = *(int *)(DAT_806128c0 + *(int *)(DAT_806128c0 + 0x28) * 4 + 0x34);
        if ((grabbed != 0) && (*(int *)(grabbed + 0x218) == 4) &&
            (*(int *)(grabbed + 0x2ac) == anchor) &&
            (((double)FLOAT_80619cac < dist && dist < (double)FLOAT_80619cf4) &&
             (*(int *)(rec + 0x20) != 7)))
            *(int *)(rec + 0x9d0) = 7;
        break;
    }
    case 7: {
        int anchor = *(int *)(DAT_806128c0 + *(int *)(DAT_806128c0 + 0x28) * 4 + 0x34);
        float ay = *(float *)(anchor + 0x68) + *(float *)(DAT_806128c0 + 0x1c);
        float ax = *(float *)(anchor + 100) + *(float *)(DAT_806128c0 + 0x18);
        float az = *(float *)(anchor + 0x6c) + *(float *)(DAT_806128c0 + 0x20);
        float len2 = az * az + ax * ax + ay * ay;
        if (FLOAT_80619cf8 < len2) {
            float inv = 1.0f / SQRT(len2);
            inv = inv * -(FLOAT_80619c48 * len2 * inv * inv - FLOAT_80619c80);
            float scale = FLOAT_80619c50 * inv * -(FLOAT_80619c48 * len2 * inv * inv - FLOAT_80619c80);
            ax = ax * scale; ay = ay * scale; az = az * scale;
        }
        float bias;
        if (*(int *)(rec + 8) == 1) {
            bias = FLOAT_80619cfc - (float)(4503601774854144.0 - DOUBLE_80619c68);
            ax = ax * bias - FLOAT_80619c50;
        } else {
            double b = (double)(unsigned int)((*(int *)(rec + 8) - 1) ^ 0x80000000);
            bias = FLOAT_80619cfc - (float)(b - DOUBLE_80619c68);
            ax = ax * bias + FLOAT_80619c50;
        }
        double vx = (double)((ax + *(float *)(anchor + 100)) - *(float *)(actor + 100));
        double vy = (double)((ay * bias + *(float *)(anchor + 0x68)) - *(float *)(actor + 0x68));
        double vz = (double)((az * bias + *(float *)(anchor + 0x6c)) - *(float *)(actor + 0x6c));
        double d2 = (double)(float)(vz * vz + (double)(float)(vy * vy + (double)(float)(vx * vx)));
        double dist = math_sqrt(d2);
        aiactor_turn_toward_entity(actor, anchor, 0);
        if ((double)FLOAT_80619cb8 <= dist) {
            if ((double)FLOAT_80619cf8 < d2) {
                double inv = 1.0 / SQRT(d2);
                float k = (float)(inv * -(double)((float)((double)FLOAT_80619c48 * d2) *
                                                  (float)(inv * inv) - FLOAT_80619c80));
                double scale = (double)(FLOAT_80619c50 * k *
                    -((float)((double)FLOAT_80619c48 * d2) * k * k - FLOAT_80619c80));
                vy = (double)(float)(vy * scale);
                vx = (double)(float)(vx * scale);
                vz = (double)(float)(vz * scale);
            }
            double step = (double)FLOAT_80619cb4;
            if (dist < step) step = dist;
            *(float *)(actor + 100) = *(float *)(actor + 100) + (float)(vy * step);
            *(float *)(actor + 0x68) = *(float *)(actor + 0x68) + (float)(vx * step);
            *(float *)(actor + 0x6c) = *(float *)(actor + 0x6c) + (float)(vz * step);
            if (anim_is_clip_active(actor, 0x26) == 0)
                anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c64);
            *(float *)(actor + 0x68) = FLOAT_80619c4c;
            if (*(float *)(actor + 0x1a4) <= FLOAT_80619c50) {
                subobj_set_material_and_mass();
                DAT_80612a9c = 0;
            } else {
                *(float *)(actor + 0x1a4) = *(float *)(actor + 0x1a4) - FLOAT_80619cec;
                subobj_set_material_and_mass();
                DAT_80612a9c = 1;
            }
        } else {
            if (*(int *)(rec + 0x20) != 8)
                *(int *)(rec + 0x9d0) = 8;
            if (anim_is_clip_active(actor, 0x110) == 0)
                anim_play_scaled((double)FLOAT_80619c64, (double)*(float *)(actor + 0x1a4));
        }
        break;
    }
    case 8: {
        int anchor = *(int *)(DAT_806128c0 + *(int *)(DAT_806128c0 + 0x28) * 4 + 0x34);
        unsigned int held = input_get_buttons_held(*(int *)DAT_80611d74);
        int grabbed = *(int *)(DAT_80611d78 + 8);
        if (((subobj_is_idle(*(unsigned int *)(anchor + 0x80)) == 0) && (grabbed != 0)) &&
            (*(int *)(grabbed + 0x218) == 4) &&
            (*(int *)(grabbed + 0x2ac) == *(int *)(DAT_806128c0 + *(int *)(DAT_806128c0 + 0x28) * 4 + 0x34))) {
            float d0 = *(float *)(actor + 0x68) - *(float *)(DAT_80611d78 + 0xac);
            float d1 = *(float *)(actor + 100) - *(float *)(DAT_80611d78 + 0xa8);
            float d2 = *(float *)(actor + 0x6c) - *(float *)(DAT_80611d78 + 0xb0);
            double dist = math_sqrt((double)(d2 * d2 + d1 * d1 + d0 * d0));
            if ((double)FLOAT_80619cf4 <= dist) {
                if (anim_is_clip_active(actor, 0x25) == 0)
                    anim_play_scaled((double)FLOAT_80619c48, (double)FLOAT_80619c50);
                if ((grabbed == 0) || (*(int *)(grabbed + 0x218) != 4) ||
                    (*(int *)(grabbed + 0x2ac) != *(int *)(DAT_806128c0 + *(int *)(DAT_806128c0 + 0x28) * 4 + 0x34))) {
                    if (*(int *)(rec + 0x20) != 9)
                        *(int *)(rec + 0x9d0) = 9;
                    break;
                }
            } else {
                float ay = *(float *)(anchor + 0x68) + *(float *)(DAT_806128c0 + 0x1c);
                float ax = *(float *)(anchor + 100) + *(float *)(DAT_806128c0 + 0x18);
                float az = *(float *)(anchor + 0x6c) + *(float *)(DAT_806128c0 + 0x20);
                float len2 = az * az + ax * ax + ay * ay;
                if (FLOAT_80619cf8 < len2) {
                    float inv = 1.0f / SQRT(len2);
                    inv = inv * -(FLOAT_80619c48 * len2 * inv * inv - FLOAT_80619c80);
                    float scale = FLOAT_80619c50 * inv * -(FLOAT_80619c48 * len2 * inv * inv - FLOAT_80619c80);
                    ax = ax * scale; ay = ay * scale; az = az * scale;
                }
                float bias = FLOAT_80619cfc;
                float unit = FLOAT_80619c50;
                if (*(int *)(rec + 8) == 1) {
                    *(float *)(actor + 0x70) = FLOAT_80619cf4;
                    bias = bias - (float)(4503601774854144.0 - DOUBLE_80619c68);
                    ax = ax * bias - unit;
                } else {
                    double b = (double)(unsigned int)((*(int *)(rec + 8) - 1) ^ 0x80000000);
                    *(float *)(actor + 0x70) = FLOAT_80619d00;
                    bias = bias - (float)(b - DOUBLE_80619c68);
                    ax = ax * bias + unit;
                }
                *(float *)(actor + 100) = *(float *)(actor + 100) +
                    ((ax + *(float *)(anchor + 100)) - *(float *)(actor + 100));
                *(float *)(actor + 0x68) = *(float *)(actor + 0x68) +
                    ((ay * bias + *(float *)(anchor + 0x68)) - *(float *)(actor + 0x68));
                *(float *)(actor + 0x6c) = *(float *)(actor + 0x6c) +
                    ((az * bias + *(float *)(anchor + 0x6c)) - *(float *)(actor + 0x6c));
                if ((held & 0x10) != 0) {
                    float cap = FLOAT_80619c9c;
                    *(float *)(actor + 0x1a4) = *(float *)(actor + 0x1a4) + FLOAT_80619c64;
                    if (*(float *)(actor + 0x1a4) < cap) cap = *(float *)(actor + 0x1a4);
                    *(float *)(actor + 0x1a4) = cap;
                    if (anim_is_clip_active(actor, 0x110) == 0)
                        anim_play_scaled((double)FLOAT_80619c48, (double)*(float *)(actor + 0x1a4));
                }
            }
            anim_set_blend_weight((double)*(float *)(actor + 0x1a4), *(int *)(actor + 4));
            if (*(float *)(actor + 0x1a4) <= FLOAT_80619c50) {
                subobj_set_material_and_mass();
                DAT_80612a9c = 0;
            } else {
                *(float *)(actor + 0x1a4) = *(float *)(actor + 0x1a4) - FLOAT_80619cec;
                subobj_set_material_and_mass();
                DAT_80612a9c = 1;
            }
        } else if (*(int *)(rec + 0x20) != 9) {
            *(int *)(rec + 0x9d0) = 9;
        }
        break;
    }
    case 9: {
        int anchor = *(int *)(DAT_806128c0 + *(int *)(DAT_806128c0 + 0x28) * 4 + 0x34);
        float approach[3];
        approach[0] = *(float *)&DAT_803e07e4;
        approach[1] = *(float *)&DAT_803e07e8;
        approach[2] = *(float *)&DAT_803e07ec;
        int zoneEnt = soundpad_resolve_entity_to_zone(0x41f - (unsigned int)(*(int *)(rec + 8) == 1));
        double vx = (double)(*(float *)(zoneEnt + 0x10) - *(float *)(actor + 0x68));
        double vy = (double)(*(float *)(zoneEnt + 0xc) - *(float *)(actor + 100));
        double vz = (double)(*(float *)(zoneEnt + 0x14) - *(float *)(actor + 0x6c));
        double d2 = (double)(float)(vz * vz + (double)(float)(vy * vy + (double)(float)(vx * vx)));
        double dist = math_sqrt(d2);
        if ((double)FLOAT_80619d04 <= dist) {
            if (dist <= (double)FLOAT_80619cac) {
                if (anim_is_clip_active(actor, 0x25) == 0)
                    anim_play_scaled((double)FLOAT_80619cb4, (double)FLOAT_80619c50);
                aiactor_turn_toward_position(actor, (int *)&approach[0], 0);
            } else {
                if (anim_is_clip_active(actor, 0x26) == 0)
                    anim_play_scaled((double)FLOAT_80619cb4, (double)FLOAT_80619c48);
                aiactor_turn_toward_entity(actor, anchor, 0);
            }
            if ((double)FLOAT_80619cf8 < d2) {
                double inv = 1.0 / SQRT(d2);
                float k = (float)(inv * -(double)((float)((double)FLOAT_80619c48 * d2) *
                                                  (float)(inv * inv) - FLOAT_80619c80));
                double scale = (double)(FLOAT_80619c50 * k *
                    -((float)((double)FLOAT_80619c48 * d2) * k * k - FLOAT_80619c80));
                vy = (double)(float)(vy * scale);
                vx = (double)(float)(vx * scale);
                vz = (double)(float)(vz * scale);
            }
            double step = (double)FLOAT_80619cb4;
            if (dist < step) step = dist;
            *(float *)(actor + 100) = *(float *)(actor + 100) + (float)(vy * step);
            *(float *)(actor + 0x68) = *(float *)(actor + 0x68) + (float)(vx * step);
            *(float *)(actor + 0x6c) = *(float *)(actor + 0x6c) + (float)(vz * step);
        } else {
            if (*(int *)(rec + 0x20) != 6)
                *(int *)(rec + 0x9d0) = 6;
            *(float *)(actor + 0x1a4) = FLOAT_80619c50;
        }
        break;
    }
    case 10: {
        int trail = entity_handle_resolve(*(int *)(rec + 8) + 0x42544c30);
        if (trail != 0) {
            *(int *)(trail + 0x78) = *(int *)(actor + 4);
            *(int *)(trail + 300) = *(int *)(*(int *)(*(int *)(actor + 4) + 0x84) + 0x30);
            *(float *)(actor + 0x1d8) = FLOAT_80619cec;
            *(float *)(actor + 0x1cc) = FLOAT_80619cec;
            *(unsigned int *)(actor + 0x28) = *(unsigned int *)(actor + 0x28) | 0x61;
            object_set_weapon_slot(rec, 0);
            int variant = *(int *)(rec + 8);
            int zoneId;
            *(float *)(trail + 0x134) = FLOAT_80619c5c;
            *(float *)(trail + 0x138) = (variant == 1) ? FLOAT_80619c58 : FLOAT_80619cb4;
            *(float *)(trail + 0x13c) = (variant == 1) ? FLOAT_80619c4c : FLOAT_80619c5c;
            *(float *)(trail + 0x140) = FLOAT_80619d08;
            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c50);
            if (variant == 3)       zoneId = 0x424;
            else if (variant == 1)  zoneId = 0x426;
            else if (variant < 3)   zoneId = (0 < variant) ? 0x3f8 : -1;
            else if (variant == 5)  zoneId = 0x40e;
            else                    zoneId = 0x403; // variant < 5 (i.e. 4)
            if (zoneId != -1) {
                int z = soundpad_find_zone_by_id(zoneId);
                player_ai_teleport_wrapper(rec, z, 1);
            }
            if (*(int *)(rec + 0x20) != 0xb)
                *(int *)(rec + 0x9d0) = 0xb;
        }
        break;
    }
    case 0xb: {
        int variant = *(int *)(rec + 8);
        int clip;
        if (variant == 3)       clip = 0x107;
        else if (variant == 1)  clip = 0x103;
        else if (variant < 3)   clip = (0 < variant) ? 0x105 : -1;
        else                    clip = 0x109; // 5 or <5
        if (clip != -1 && anim_is_clip_active(actor, clip) == 0)
            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c50);
        break;
    }
    case 0xc: {
        int variant = *(int *)(rec + 8);
        int spawnZoneId = -1;
        if (variant == 3)       { spawnZoneId = 0x476; *(int *)(rec + 0x9dc) = 0x476; }
        else if (variant == 1)  { spawnZoneId = 0x491; *(int *)(rec + 0x9dc) = 0x4db; }
        else if (variant < 3)   { if (0 < variant) { spawnZoneId = 0x4dd; *(int *)(rec + 0x9dc) = 0x4da; } }
        else                    { spawnZoneId = 0x44a; *(int *)(rec + 0x9dc) = 0x2340; }
        if (*(int *)(rec + 0x20) != 0xd)
            *(int *)(rec + 0x9d0) = 0xd;
        *(int *)(rec + 0xaa0) = 0;
        *(float *)(actor + 0x1d8) = FLOAT_80619c5c;
        *(int *)(actor + 0x1cc) = *(int *)(actor + 0x1d8);
        int seqZoneId = *(int *)(&DAT_80473884 + variant * 0x28 + *(int *)(rec + 0xaa0) * 4);
        int z = soundpad_find_zone_by_id(seqZoneId);
        if (z < 0)
            *(int *)(rec + 0x684) =
                *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        else
            *(int *)(rec + 0x684) = soundpad_find_zone_by_id(seqZoneId);
        *(int *)(rec + 0x9ac) = 0;
        *(int *)(rec + 0x9a8) = 0;
        int tz = soundpad_find_zone_by_id(spawnZoneId);
        player_ai_teleport_wrapper(rec, tz, 1);
        object_set_weapon_slot(rec, 0);
        aiactor_update_zone_anchor_position();
        aiactor_set_stance_flags(actor, 1);
        anim_play_scaled((double)FLOAT_80619c4c, (double)(FLOAT_80619cb4 * ai_rand01() + FLOAT_80619c50));
        break;
    }
    case 0xd: {
        aiactor_movement_tick(0, 0, 0, 0, 0, 0, 0, 0);
        if ((*(unsigned int *)(rec + 0x28) & 1) == 0) {
            if (aiactor_follow_zone_path() == 0) {
                if (anim_active_slot_userflags(*(int *)(actor + 4)) != 0) {
                    double rate = (double)(FLOAT_80619cb4 * ai_rand01() + FLOAT_80619c50);
                    if (*(int *)(actor + 0x20c) == 0)
                        anim_play_scaled((double)FLOAT_80619c7c, rate);
                    else
                        anim_play_scaled((double)FLOAT_80619c4c, rate);
                }
            } else {
                *(int *)(rec + 0xaa0) = *(int *)(rec + 0xaa0) + 1;
                int variant = *(int *)(rec + 8);
                if (*(int *)(&DAT_80473884 + variant * 0x28 + *(int *)(rec + 0xaa0) * 4) == -1)
                    *(int *)(rec + 0xaa0) = 0;
                int seqZoneId = *(int *)(&DAT_80473884 + variant * 0x28 + *(int *)(rec + 0xaa0) * 4);
                int z = soundpad_find_zone_by_id(seqZoneId);
                if (z < 0)
                    *(int *)(rec + 0x684) =
                        *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    *(int *)(rec + 0x684) = soundpad_find_zone_by_id(seqZoneId);
                *(int *)(rec + 0x9ac) = 0;
                *(int *)(rec + 0x9a8) = 0;
            }
            aiactor_movement_tick(0, 0, 0, 0, 0, 0, 0, 0);
            aiactor_facing_update();
        } else {
            int codeA, codeB;
            if (DAT_805015bd == '\n') { codeA = *(int *)(rec + 0x10); codeB = *(int *)(rec + 0x14); }
            else                       { codeA = *(int *)(rec + 0x18); codeB = *(int *)(rec + 0x1c); }
            *(int *)(rec + 0xabc) = codeB;
            *(int *)(rec + 0xab8) = codeA;
            ai_queue_secondary(rec, 0x20);
        }
        break;
    }
    case 0xe: {
        if (aiactor_follow_zone_path() == 0) {
            aiactor_movement_tick(0, 0, 0, 0, 0, 0, 0, 0);
            if ((*(unsigned int *)(rec + 0x28) & 1) != 0) {
                int codeA, codeB;
                if (DAT_805015bd == '\n') { codeA = *(int *)(rec + 0x10); codeB = *(int *)(rec + 0x14); }
                else                       { codeA = *(int *)(rec + 0x18); codeB = *(int *)(rec + 0x1c); }
                *(int *)(rec + 0xabc) = codeB;
                *(int *)(rec + 0xab8) = codeA;
                ai_queue_secondary(rec, 0x20);
                break;
            }
        } else {
            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c50);
            if (*(int *)(actor + 0x20c) != 0) {
                int tp = *(int *)(actor + 0x20c);
                *(int *)(tp + 0x78) = 0;
                *(int *)(tp + 0x128) = 0;
                *(int *)(tp + 300) = -1;
                int node = *(int *)(actor + 0x34);
                void *tex = scene_node_get_texture_data(node, 1);
                int matCount = scene_node_get_material_count(*(int *)(actor + 0x34), 1);
                float y = floor_polygon_search(matCount, tex,
                    *(unsigned int *)(*(int *)(*(int *)(DAT_80611b04 + node * 4) + 4) + 0x30),
                    (float *)(actor + 100), (unsigned int *)0);
                *(float *)(*(int *)(actor + 0x20c) + 0x68) = y;
            }
        }
        aiactor_facing_update();
        break;
    }
    }
}

// =============================================================================
// 0x802c01d8  aiactor_behavior_worm_boss_tick
// The "sprg" runner boss FSM (states 0..8): selects a runner target, teleports
// between zones, flees, leaps, and throws. FP-register spill args are frame junk.
// =============================================================================
void aiactor_behavior_worm_boss_tick(void)
{
    int actor = aiactor_current_frame_actor();
    int object = *(int *)(actor + 0x7c);
    int hasTeammate = aiactor_retarget_visible_teammate();
    ai_tick_preamble(object);

    *(int *)(object + 0x30) = *(int *)(actor + 100);
    *(int *)(object + 0x34) = *(int *)(actor + 0x68);
    *(int *)(object + 0x38) = *(int *)(actor + 0x6c);
    *(unsigned int *)(object + 0x28) = *(unsigned int *)(object + 0x28) | 0x4000;
    pickup_blink_start((double)FLOAT_80619c80, (double)FLOAT_80619c78, object + 0xc68);
    *(int *)(object + 4) = worm_sprg_select_runner();
    if (*(int *)(object + 4) == 0) {
        ai_queue_mode(object, 3);
        goto done;
    }

    {
    int state = *(int *)(object + 0x20);
    switch (state) {
    case 0:
        if (((*(unsigned int *)(object + 0xab4) & 0x20) == 0) && ((*(unsigned int *)(object + 0xab4) & 0x40) == 0))
            ai_queue_mode(object, 1);
        else
            ai_queue_mode(object, 3);
        break;
    case 1:
        *(float *)(actor + 0x1d8) = FLOAT_80619c5c;
        *(int *)(actor + 0x1cc) = *(int *)(actor + 0x1d8);
        *(unsigned int *)(actor + 0x28) = *(unsigned int *)(actor + 0x28) & 0xffffff9e;
        break;
    case 2: {
        aiactor_movement_tick(aiactor_facing_update(), 0, 0, 0, 0, 0, 0, 0);
        *(float *)(actor + 0x1d8) = FLOAT_80619c5c;
        *(int *)(actor + 0x1cc) = *(int *)(actor + 0x1d8);
        *(unsigned int *)(actor + 0x28) = *(unsigned int *)(actor + 0x28) | 0x61;
        object_set_weapon_slot(object, 0x40);
        int variant = *(int *)(object + 8);
        int sign = variant >> 0x1f;
        int idx = ((sign * 4 | (unsigned int)(variant * 0x40000000 + sign) >> 0x1e) - sign);
        int z = soundpad_find_zone_by_id(*(int *)(&DAT_80473988 + idx * 4));
        player_ai_teleport_wrapper(object, z, 1);
        // (fidget clip)
        extern void tracker_play_melee_fidget_clip(int actor);
        tracker_play_melee_fidget_clip(actor);
        if ((*(float *)(object + 0xaf4) < FLOAT_80619d0c) ||
            (*(int *)(object + 0x678) == *(int *)(object + 0x684))) {
            *(float *)(object + 0x9e0) = FLOAT_80619cb8 * ai_rand01() + FLOAT_80619cb8 + *(float *)(object + 0xb30);
            ai_queue_mode(object, 3);
            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
        } else {
            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
            aiactor_set_stance_flags(actor, 1);
            ai_queue_mode(object, 5);
        }
        break;
    }
    case 3: {
        aiactor_movement_tick(aiactor_turn_toward_leader(), 0, 0, 0, 0, 0, 0, 0);
        if (hasTeammate == 0) {
            *(int *)(object + 4) = worm_sprg_select_runner();
            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
            ai_queue_mode(object, 5);
        } else if ((anim_is_clip_active(actor, 0x581) == 0) &&
                   (anim_is_clip_active(actor, 0x582) == 0) &&
                   (anim_is_clip_active(actor, 0x588) == 0)) {
            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
        } else if (anim_primary_clip_userflags(actor, 0x588) == 0) {
            if ((anim_primary_clip_userflags(actor, 0x581) != 0) ||
                (anim_primary_clip_userflags(actor, 0x582) != 0)) {
                if ((*(float *)(object + 0x9e0) < *(float *)(object + 0xb30)) ||
                    (FLOAT_80619d0c < *(float *)(object + 0xaf4))) {
                    int action = (int)(FLOAT_80619cac * ai_rand01() + FLOAT_80619c48);
                    if (FLOAT_80619d10 < *(float *)(object + 0x9cc)) {
                        int fleeIdx = (int)(FLOAT_80619c74 * ai_rand01() + FLOAT_80619c48);
                        int z = soundpad_find_zone_by_id(*(int *)(&DAT_80473998 + fleeIdx * 4));
                        if (z < 0)
                            *(int *)(object + 0x684) =
                                *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                        else
                            *(int *)(object + 0x684) = soundpad_find_zone_by_id(*(int *)(&DAT_80473998 + fleeIdx * 4));
                        *(int *)(object + 0x9ac) = 0;
                        *(int *)(object + 0x9a8) = 0;
                        anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
                        ai_queue_mode(object, 7);
                    }
                    if (action == 4) {
                        if (*(int *)(object + 0x678) != *(int *)(object + 0x684)) {
                            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
                            ai_queue_mode(object, 5);
                        }
                    } else if (action < 4) {
                        if (action < 3) {
                            if (-1 < action) {
                                anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
                                ai_queue_mode(object, 6);
                                *(float *)(object + 0xb30) = ai_rand01();
                            }
                        } else if ((hasTeammate != 0) && (*(float *)(object + 0xaf4) < FLOAT_80619d14)) {
                            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
                            ai_queue_mode(object, 8);
                        }
                    } else if (action == 6) {
                        if (FLOAT_80619d10 < *(float *)(object + 0x9cc)) {
                            int fleeIdx = (int)(FLOAT_80619c74 * ai_rand01() + FLOAT_80619c48);
                            int z = soundpad_find_zone_by_id(*(int *)(&DAT_80473998 + fleeIdx * 4));
                            if (z < 0)
                                *(int *)(object + 0x684) =
                                    *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                            else
                                *(int *)(object + 0x684) = soundpad_find_zone_by_id(*(int *)(&DAT_80473998 + fleeIdx * 4));
                            *(int *)(object + 0x9ac) = 0;
                            *(int *)(object + 0x9a8) = 0;
                            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
                            ai_queue_mode(object, 7);
                        }
                    } else if (action < 6) {
                        anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
                        aiactor_compute_flee_target(actor, (float *)(object + 0xa74));
                        ai_queue_mode(object, 4);
                    }
                } else {
                    if (anim_is_clip_active(actor, 0x582) == 0) {
                        if (ai_rand01() <= FLOAT_80619c64) {
                            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
                            break;
                        }
                    }
                    anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
                }
            }
        } else {
            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
        }
        break;
    }
    case 4: {
        *(int *)(object + 0x30) = *(int *)(object + 0xa74);
        *(int *)(object + 0x34) = *(int *)(object + 0xa78);
        *(int *)(object + 0x38) = *(int *)(object + 0xa7c);
        aiactor_movement_tick(aiactor_facing_update(), 0, 0, 0, 0, 0, 0, 0);
        if ((anim_is_clip_active(actor, 0x586) == 0) && (anim_is_clip_active(actor, 0x58b) == 0) &&
            (anim_is_clip_active(actor, 0x57f) == 0) && (anim_is_clip_active(actor, 0x57e) == 0) &&
            (anim_is_clip_active(actor, 0x589) == 0) && (anim_is_clip_active(actor, 0x580) == 0)) {
            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
        } else if (anim_primary_clip_userflags(actor, 0x586) == 0) {
            if (anim_primary_clip_userflags(actor, 0x580) == 0) {
                if (anim_primary_clip_userflags(actor, 0x589) == 0) {
                    if ((anim_primary_clip_userflags(actor, 0x57e) != 0) ||
                        (anim_primary_clip_userflags(actor, 0x57f) != 0)) {
                        int sel = (int)(FLOAT_80619d18 * ai_rand01() + FLOAT_80619c48);
                        if (*(float *)(object + 0xaf4) < FLOAT_80619d0c)
                            sel = 0;
                        if (sel == 1) {
                            if ((*(int *)(object + 0x678) != *(int *)(object + 0x684)) || (hasTeammate == 0))
                                anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
                        } else {
                            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
                        }
                    }
                } else {
                    ai_queue_mode(object, 5);
                    anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
                }
            } else {
                *(float *)(object + 0x9e0) = FLOAT_80619cb8 * ai_rand01() + FLOAT_80619cb8 + *(float *)(object + 0xb30);
                ai_queue_mode(object, 3);
                anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
            }
        } else {
            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
        }
        break;
    }
    case 5:
        if ((aiactor_follow_zone_path() != 0) ||
            ((hasTeammate != 0) && (*(float *)(object + 0xaf4) < FLOAT_80619d0c))) {
            anim_play_scaled((double)FLOAT_80619c38, (double)FLOAT_80619c90);
            ai_queue_mode(object, 3);
        } else {
            if ((anim_is_clip_active(actor, 0x587) == 0) && (anim_is_clip_active(actor, 0x58a) == 0))
                anim_play_scaled((double)FLOAT_80619c38, (double)FLOAT_80619c90);
            else if (anim_primary_clip_userflags(actor, 0x58a) != 0)
                anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
            if ((*(unsigned int *)(object + 0x28) & 1) == 0) {
                aiactor_movement_tick(aiactor_facing_update(), 0, 0, 0, 0, 0, 0, 0);
            } else {
                int codeA, codeB;
                if (DAT_805015bd == '\n') { codeA = *(int *)(object + 0x10); codeB = *(int *)(object + 0x14); }
                else                       { codeA = *(int *)(object + 0x18); codeB = *(int *)(object + 0x1c); }
                *(int *)(object + 0xabc) = codeB;
                *(int *)(object + 0xab8) = codeA;
                ai_queue_secondary(object, 0x20);
            }
        }
        break;
    case 6:
        aiactor_movement_tick(aiactor_facing_update(), 0, 0, 0, 0, 0, 0, 0);
        if ((anim_is_clip_active(actor, 0x58e) == 0) && (anim_is_clip_active(actor, 0x58c) == 0) &&
            (anim_is_clip_active(actor, 0x58d) == 0) && (anim_is_clip_active(actor, 0x591) == 0) &&
            (anim_is_clip_active(actor, 0x58f) == 0) && (anim_is_clip_active(actor, 0x590) == 0)) {
            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
        } else if (anim_primary_clip_userflags(actor, 0x58e) == 0) {
            if (anim_primary_clip_userflags(actor, 0x591) == 0) {
                if ((anim_primary_clip_userflags(actor, 0x58d) == 0) &&
                    (anim_primary_clip_userflags(actor, 0x590) == 0)) {
                    if ((anim_is_clip_active(actor, 0x58d) == 0) &&
                        (anim_is_clip_active(actor, 0x590) == 0) &&
                        (FLOAT_80619c50 < *(float *)(object + 0xb30))) {
                        anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
                    }
                } else if ((anim_is_clip_active(actor, 0x58d) == 0) ||
                           (FLOAT_80619d14 <= *(float *)(object + 0xaf4))) {
                    *(float *)(object + 0x9e0) = FLOAT_80619cb8 * ai_rand01() + FLOAT_80619cb8 + *(float *)(object + 0xb30);
                    ai_queue_mode(object, 3);
                    anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
                } else {
                    ai_queue_mode(object, 8);
                    anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
                }
            } else {
                anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c50);
            }
        } else {
            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c50);
        }
        break;
    case 7:
        if (aiactor_follow_zone_path() == 0) {
            if ((anim_primary_clip_userflags(actor, 0x589) == 0) &&
                (anim_primary_clip_userflags(actor, 0x58a) == 0)) {
                if (anim_is_clip_active(actor, 0x587) == 0)
                    anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
            } else {
                anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
            }
            if ((*(unsigned int *)(object + 0x28) & 1) == 0) {
                aiactor_movement_tick(aiactor_facing_update(), 0, 0, 0, 0, 0, 0, 0);
            } else {
                int codeA, codeB;
                if (DAT_805015bd == '\n') { codeA = *(int *)(object + 0x10); codeB = *(int *)(object + 0x14); }
                else                       { codeA = *(int *)(object + 0x18); codeB = *(int *)(object + 0x1c); }
                *(int *)(object + 0xabc) = codeB;
                *(int *)(object + 0xab8) = codeA;
                ai_queue_secondary(object, 0x20);
            }
        } else {
            worm_boss_leap_land(object, actor);
        }
        break;
    case 8:
        aiactor_movement_tick(aiactor_turn_toward_leader(), 0, 0, 0, 0, 0, 0, 0);
        if ((anim_is_clip_active(actor, 0x585) == 0) && (anim_is_clip_active(actor, 0x57c) == 0) &&
            (anim_is_clip_active(actor, 0x57b) == 0)) {
            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
        } else if (anim_primary_clip_userflags(actor, 0x585) == 0) {
            if (anim_primary_clip_userflags(actor, 0x57c) == 0) {
                if (anim_primary_clip_userflags(actor, 0x57b) != 0) {
                    *(float *)(object + 0x9e0) = FLOAT_80619cb8 * ai_rand01() + FLOAT_80619cb8 + *(float *)(object + 0xb30);
                    ai_queue_mode(object, 3);
                    anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
                }
            } else {
                anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
            }
        } else {
            *(unsigned int *)(object + 0x2c) = *(unsigned int *)(object + 0x2c) | 0x400;
            *(unsigned int *)(object + 0x28) = *(unsigned int *)(object + 0x28) | 2;
            *(float *)(object + 0x9fc) = FLOAT_80619c5c * ai_rand01();
            unsigned long long clip = anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c90);
            object_melee_thrown_fire(clip, (double)FLOAT_80619c90, actor, 0, 0, 0);
            int sp[24];
            sound_init_play_params((int)&sp[0]);
            sp[0] = 0xd30;
            *(float *)&sp[6] = FLOAT_80619d1c;
            sp[7] = *(int *)(actor + 100);
            sp[8] = *(int *)(actor + 0x68);
            sp[9] = *(int *)(actor + 0x6c);
            *(char *)&sp[12] = 1;
            sound_play_request(&sp[0]);
        }
        break;
    }
    }
done:
    return;
}

// =============================================================================
// 0x802c1520  worm_boss_leap_land
// Leap-landing handler for the worm boss: spawn a landing explosion, kill the
// trail particle, teleport to the next zone, clear flags, and trigger the prop
// explosion damage if a linked prop is resolved.
// =============================================================================
void worm_boss_leap_land(int object, int actor)
{
    float pos[3];  // local_78/74/70
    pos[0] = *(float *)&DAT_803e07f0;
    pos[1] = *(float *)&DAT_803e07f4;
    pos[2] = *(float *)&DAT_803e07f8;
    player_ai_explosion_spawn(0x2d, 0x17, (float *)(actor + 100), &pos[0], 0, actor, 1);
    particle_kill_by_entity();
    int variant = *(int *)(object + 8);
    int sign = variant >> 0x1f;
    int idx = ((sign * 4 | (unsigned int)(variant * 0x40000000 + sign) >> 0x1e) - sign);
    int z = soundpad_find_zone_by_id(*(int *)(&DAT_80473988 + idx * 4));
    player_ai_teleport_wrapper(object, z, 1);
    *(unsigned int *)(actor + 0x28) = *(unsigned int *)(actor + 0x28) & 0xfbffff9e;
    if (*(int *)(object + 0x20) != 1)
        *(int *)(object + 0x9d0) = 1;
    int prop = entity_handle_resolve(*(unsigned int *)(object + 0x9dc));
    if (prop != 0) {
        prop_explode_tick();
    }
}

// =============================================================================
// 0x802c163c  aiactor_find_nearby_special_actor
// Scan the special-actor array (DAT_805887d0) for a state-5 actor whose linked
// zone is active and which is within range of the frame actor. (Result is
// consumed by the caller as the matched actor entity (0 if none).)
// =============================================================================
int aiactor_find_nearby_special_actor(void)
{
    int actor = aiactor_current_frame_actor();
    int off = 0;
    int found = 0;
    for (int i = 0; i < DAT_806120ec; i = i + 1) {
        int other = *(int *)((int)&DAT_805887d0 + off);
        int rec = *(int *)(other + 0x7c);
        if ((rec != 0) && (*(int *)(rec + 0x1c0) != -1)) {
            int zoneEnt = soundpad_resolve_entity_to_zone(*(int *)(rec + 0x1c0));
            if (*(int *)(rec + 0x10) == 5) {
                int z = soundpad_find_zone_by_id((int)*(short *)(zoneEnt + 8));
                if ((*(unsigned int *)(DAT_80611b8c + z * 0x20 + 4) & 0x40000000) == 0) {
                    float d0 = *(float *)(other + 100) - *(float *)(actor + 100);
                    float d1 = *(float *)(other + 0x68) - *(float *)(actor + 0x68);
                    float d2 = *(float *)(other + 0x6c) - *(float *)(actor + 0x6c);
                    if (d2 * d2 + d0 * d0 + d1 * d1 < FLOAT_80619d14) {
                        found = other;
                        break;
                    }
                }
            }
        }
        off = off + 4;
    }
    return found;
}

} // extern "C"
