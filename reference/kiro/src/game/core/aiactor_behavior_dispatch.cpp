// =============================================================================
// aiactor_behavior_dispatch.cpp - AI actor high-level behaviour-state handlers
// driven by tracker_behavior_state_dispatch (the per-frame AI behaviour table).
//
// This is the sibling of aiactor_combat_fsm.cpp: where that file animates a
// single actor, these routines run the match-wide behaviour states - spawning
// the bot roster, choosing spawn zones, resolving target zones, running the
// death/respawn setup, and the top-level engage/combat behaviour ticks.
//
// Each AI actor carries a control record at (actor+0x7c) (here `rec`). Key
// fields used below (byte offsets unless noted):
//   rec+0x04   current target entity        rec+0x94   death-drop weapon slot
//   rec+0x678  resolved zone id             rec+0x684  fallback zone id
//   rec+0xaac / rec+0xaa8  pending action code + latch
//   rec+0xabc / rec+0xab8  secondary action code + latch
//   rec+0xaf0  per-zone action word         rec[0x19e] chosen spawn zone (int idx)
//
// The per-character navigation tuning table lives at DAT_8048ca94 (float pairs,
// stride 0x30, indexed by the actor's character id at rec+0x48).
//
// game/level mode selectors: DAT_805015bd (mode), DAT_805015be (sub-mode),
// DAT_805015c0 (co-op flags), DAT_805015d8/dc (variant).
//
// Address range: 0x802b7434 - 0x802b8ff4
// Subsystem: game (AI actor behaviour dispatch)
// NON-MATCHING: CONCAT44(0x43300000,x) int->double bias idioms reduced to plain
// float math; incoming paired-single f28-f31 spill temporaries expressed as
// parameters; the repeated "action-latch" idiom
//   if (!(rec.action==1 && rec.latch==0 && ((mode==10?rec.f14:rec.f1c)&1)==0)) ...
// is factored into ai_latch_action().
// =============================================================================

#include "common.h"

extern "C" {

// --- level / region / global state (raw symbols this file reads) -------------
extern char DAT_805015bd;          // game/level mode selector
extern char DAT_805015be;          // sub-mode
extern unsigned char DAT_805015c0; // co-op flags
extern int  DAT_805015d8;          // co-op variant id
extern int  DAT_805015dc;          // spectator/observer flag
extern char DAT_80501572;          // secondary-tracker enable
extern unsigned char DAT_80501570[]; // per-slot team/character config table

extern int  DAT_80610ba0;          // actor/character definition table base
extern short DAT_8043e23c;         // per-weapon character-def index table (stride 0xf)
extern int  DAT_806118dc;          // story-mode override gate
extern int  DAT_80611910;          // designated leader/special actor
extern int  DAT_80611918;          // reference actor for spawn scoring
extern int  DAT_80611934;          // spawn-request flag
extern int  DAT_80611568;          // zone-objective init flag
extern int  DAT_80611b94;          // spawn descriptor table base
extern int  DAT_80611b98;          // action/zone flag table base
extern int  DAT_80611b9c;          // per-zone action word table base
extern int  DAT_80611c0c;          // spawn descriptor count
extern int  DAT_80611d78;          // world/level context base
extern int  DAT_80611d90;          // actor record array base (stride 0xea0)
extern int  DAT_80612098;          // active actor count
extern int  DAT_806120c4;          // tracker state block A
extern int  DAT_806120c8;          // tracker state block B
extern int  DAT_806120d8;          // per-state spawn mask
extern int  DAT_806120f8;          // frame stamp
extern int  DAT_806120fc;          // bot roster count
extern int  DAT_80612610;          // human-player count / frame threshold
extern int  DAT_80612a98;          // tracker generation counter
extern int  DAT_80588610;          // actor pointer list base
extern int  DAT_80588a1c;          // spawned-bot pointer list base
extern float FLOAT_806120d4;       // last spawn timestamp

// per-character navigation tuning table (float pairs, stride 0x30 by char id)
extern float DAT_8048ca94;
extern float DAT_8048ca9c;

// float / double tuning constants unique to this file
extern const float  FLOAT_80619c30;
extern const float  FLOAT_80619c34;
extern const float  FLOAT_80619c48;  // 1.0 / y-offset
extern const float  FLOAT_80619c4c;  // 0.0
extern const float  FLOAT_80619c50;
extern const float  FLOAT_80619c54;
extern const float  FLOAT_80619c58;
extern const float  FLOAT_80619c5c;
extern const float  FLOAT_80619c60;
extern const float  FLOAT_80619c64;
extern const float  FLOAT_80619c70;
extern const float  FLOAT_80619c74;
extern const double DOUBLE_80619c40;

// --- frame-enter / frame-leave intrinsics ------------------------------------
extern unsigned long long __save_gpr_25(void);
extern void __restore_gpr_25(void);
extern unsigned long long __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern int  FUN_803826d8(void);
extern int  FUN_803826e4(void);
extern void FUN_80382724(void);
extern void FUN_80382730(void);

// --- named helpers resolved through generated_decls.h ------------------------
extern unsigned int rng_counter_tick(void);
extern int  soundpad_find_zone_by_id(int id);
extern int  aiactor_find_squad_index(int actor);
extern int  tracker_find_owned_actor(int a);
extern int  match_build_weapon_list(void);
extern int  match_slot_field_ptr(int base, int a);
extern unsigned int player_ai_char_def_get_flags(int def);
extern void object_write_sound_slots_all(void);
extern int  nav_advanced_movement_dispatch(double px, double pz, int a, void *from,
                                           void *to, int *out, int b, int c);
extern void nav_movement_dispatch(double px, double pz, int a, int pos, void *v0,
                                  void *v1, int *out, int b, int c);
extern float floor_query_with_interact(double px, double pz, int pos, int *hit,
                                       int *out, int b);
extern int  soundpad_resolve_entity_to_zone(int id);
extern int  player_ai_respawn_state_reset(void);
extern float damage_splash_level_gate(void);
extern float damage_get_max_radius(void);
extern int *aiactor_resolve_zone_or_node(int actor);
extern void aiactor_update_zone_anchor_position(void);
extern void aiactor_turn_toward_leader(void);
extern void entity_assign_room(int actor);
extern void character_death_start(unsigned long long ctx, double a, double b, double c,
                                  unsigned long long d, unsigned long long e,
                                  unsigned long long f, unsigned long long g);
extern void character_damage_effect_wrapper(void);
extern void anim_clear_render_scale(int rec);
extern void anim_play_scaled(double a, double b);
extern int  player_ai_is_story_mode(int level);
extern int  aiactor_retarget_visible_teammate(void);
extern unsigned int tracker_player_flag_bit4(int a);
extern int  aiactor_retarget_scan(void);
extern int  aiactor_follow_zone_path(void);
extern void aiactor_set_stance_flags(int actor, int flags);
extern void anim_set_playback_rate(double r, int *slot, int a);
extern void soundpad_query_two_hop_zones(int zone, int *out, int *count, int a,
                                         int *b, int cap, int flags);
extern int  soundpad_get_entity_id_from_zone(int zone);
extern int *soundpad_resolve_zone_node(int zone);
extern int  spawn_select_character_wrapper2(void);
extern int  tracker_stub_zero_a(void);
extern void *memset_wrapper(int dst, int val, int len);
extern unsigned int tracker_costmatrix_mode_active(void);
extern unsigned long long tracker_alloc_state(unsigned long long a, unsigned long long b,
        unsigned long long c, unsigned long long d, unsigned long long e,
        unsigned long long f, unsigned long long g, unsigned long long h,
        void *cfg, int i, int j, int k, int l, int m, int n, int o);
extern int  player_ai_spawn_bot(double a, double b, double c, double d,
        unsigned long long e, unsigned long long f, unsigned long long g,
        unsigned long long h, int team, int chr, int idx, int i, int j, int k);
extern void player_ai_assign_team(int rec);
extern unsigned long long aiactor_visible_list_add(int actor);
extern unsigned long long tracker_populate_from_level(void);
extern unsigned long long tracker_count_actors_per_team(void);
extern void tracker_precompute_zone_objectives(unsigned long long ctx, double a, double b,
        double c, unsigned long long d, unsigned long long e, unsigned long long f,
        unsigned long long g);
extern void character_death_effect_reset(unsigned int *rec);
extern int  tracker_scan_class_flag400(void);

// --- unnamed callees (leave as raw externs) ----------------------------------
extern int  aiactor_seek_rally_marker(int actor); // (was FUN_802be2a4)
extern void aiactor_pick_best_weapon(void);
extern void FUN_802768f0(void);
extern void FUN_80276930(unsigned long long a, unsigned long long b, unsigned long long c,
        unsigned long long d, unsigned long long e, unsigned long long f,
        unsigned long long g, unsigned long long h, unsigned int i, int j, int k,
        int l, int m, int n, int o, int p);
extern void FUN_802bc1a4(void);

// forward decls (mutual references inside this file)
int  aiactor_resolve_target_zone(int actor);
void aiactor_pick_death_weapon(int actor);
void aiactor_compute_zone_anchor_pos(int actor, int *zoneNode, float *outPos);
int *aiactor_select_spawn_zone(void);
unsigned int aiactor_roll_attack_chance(int rec);

// Shared "commit an action code unless a matching one is already latched" idiom.
// Returns 1 when the caller should skip the write (already latched), else 0.
static int ai_action_already_latched(int *rec)
{
    if (*(int *)((char *)rec + 0xaac) == 1 && *(int *)((char *)rec + 0xaa8) == 0) {
        unsigned int f = (DAT_805015bd == 10)
                             ? *(unsigned int *)((char *)rec + 0x14)
                             : *(unsigned int *)((char *)rec + 0x1c);
        if ((f & 1) == 0)
            return 1;
    }
    return 0;
}

// -----------------------------------------------------------------------------
// aiactor_resolve_target_zone (0x802b7434) - resolve the zone/node id the actor
// should navigate to, based on its tracked target entity's type and the mode.
// -----------------------------------------------------------------------------
int aiactor_resolve_target_zone(int actor)
{
    int rec = *(int *)(actor + 0x7c);

    if (DAT_805015bd == 1 && *(int *)(rec + 0x80) != 0) {
        int z = *(int *)(rec + 0x684);
        if (z > 0x209d && z < 0x20b2)
            return (int)*(short *)(DAT_80611b98 + z - 0x413d8);
        return z;
    }

    if (DAT_805015bd == 6 && *(int *)(rec + 4) != 0 &&
        *(int *)(*(int *)(rec + 4) + 0x20) != 8) {
        *(int *)(rec + 4) = 0;
    }

    int tgt = *(int *)(rec + 4);
    if (tgt == 0)
        return *(int *)(rec + 0x684);

    int type = *(int *)(tgt + 0x20);
    if (type != 0x40) {
        if (type < 0x40) {
            if (type != 8)
                return -1;
            return *(int *)(*(int *)(tgt + 0x7c) + 0x678);
        }
        if (type == 0x2000)
            return *(int *)(*(int *)(tgt + 0x7c) + 4);
        if (type > 0x1fff)
            return -1;
        if (type != 0x200)
            return -1;
        int z = *(int *)(*(int *)(tgt + 0x7c) + 0x1c0);
        if (z != -1)
            return soundpad_find_zone_by_id(z);
        return *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
    }

    if (*(int *)(*(int *)(tgt + 0x7c) + 0xc) == 0x80)
        return *(int *)(*(int *)(tgt + 0x7c) + 0x38);

    if (DAT_805015bd != 2 && DAT_805015bd != 1)
        return -1;

    if (*(int *)(rec + 0x80) != 0)
        return aiactor_find_squad_index(actor);

    int owner = 1;
    if (DAT_805015bd == 2 && *(int *)(rec + 0x84) == 1)
        owner = 2;
    int owned = tracker_find_owned_actor(owner);
    if (owned != 0) {
        *(int *)(rec + 4) = owned;
        return *(int *)(*(int *)(*(int *)(rec + 4) + 0x7c) + 0x678);
    }

    int fz = *(int *)(*(int *)(*(int *)(rec + 4) + 0x7c) + 0x38);
    if (fz >= 0) {
        if (!(fz > 0x209d && fz < 0x20b2))
            return fz;
        if ((*(unsigned int *)(DAT_80611b98 + fz * 0x20 - 0x413bc) & 1) != 0)
            return fz;
    }

    *(int *)(rec + 0xaf0) =
        *(int *)(DAT_80611b9c + *(int *)(rec + 0x678) * 0x2c);
    if (*(int *)(rec + 0xaac) == 1 && *(int *)(rec + 0xaa8) == 0) {
        unsigned int f = (DAT_805015bd == 10) ? *(unsigned int *)(rec + 0x14)
                                              : *(unsigned int *)(rec + 0x1c);
        if ((f & 1) == 0)
            return *(int *)(rec + 0x678);
    }
    *(int *)(rec + 0xaac) = 0x400000;
    *(int *)(rec + 0xaa8) = 0;
    return *(int *)(rec + 0x678);
}

// -----------------------------------------------------------------------------
// aiactor_pick_death_weapon (0x802b76e0) - choose the weapon slot the corpse
// drops, honouring the match weapon list and the character-definition flags.
// -----------------------------------------------------------------------------
void aiactor_pick_death_weapon(int actor)
{
    int rec = *(int *)(actor + 0x7c);

    if (*(int *)(rec + 0x94) == 0 && (DAT_805015c0 & 0x400) != 0) {
        int list = match_build_weapon_list();
        if (*(int *)(list + 4) > 0 && *(int *)(list + 8) > 0)
            *(int *)(rec + 0x94) = *(int *)(list + 8);
    }

    int slot = *(int *)(rec + 0x94);
    if (slot > 0 && slot < 0x46) {
        *(unsigned char *)(rec + slot + 0x54c) = 1;
        object_write_sound_slots_all();
    }

    int def = match_slot_field_ptr(
        DAT_80610ba0 + (&DAT_8043e23c)[*(int *)(rec + 0x94) * 0xf] * 0x29c, 0);
    unsigned int flags = player_ai_char_def_get_flags(*(int *)(def + 4));
    if ((flags & 0x400) != 0)
        *(int *)(rec + 0x94) = 0;
}

// -----------------------------------------------------------------------------
// aiactor_compute_zone_anchor_pos (0x802b77b8) - compute the movement anchor
// position (world xyz) the actor should aim for given its target zone node.
// -----------------------------------------------------------------------------
void aiactor_compute_zone_anchor_pos(int actor, int *target, float *outPos)
{
    (void)actor;
    unsigned long long fr = FUN_803826e4();
    int self = (int)(fr >> 0x20);
    int tgt = (int)fr;
    int rec = *(int *)(self + 0x7c);
    int floorHit = 0;
    float from[3], probe[3], zonePos[3];

    if ((*(unsigned int *)(tgt + 4) & 0x800) == 0) {
        outPos[0] = *(float *)(tgt + 0xc);
        outPos[1] = *(float *)(tgt + 0x10);
        outPos[2] = *(float *)(tgt + 0x14);
        FUN_80382730();
        return;
    }

    int zone = soundpad_resolve_entity_to_zone((int)*(short *)(tgt + 8));
    if (zone == 0) {
        outPos[0] = *(float *)(tgt + 0xc);
        outPos[1] = *(float *)(tgt + 0x10);
        outPos[2] = *(float *)(tgt + 0x14);
        FUN_80382730();
        return;
    }

    probe[0] = *(float *)(tgt + 0xc);
    probe[1] = *(float *)(tgt + 0x10) + FLOAT_80619c48;
    probe[2] = *(float *)(tgt + 0x14);
    from[0] = *(float *)(zone + 0xc);
    from[1] = *(float *)(zone + 0x10) + FLOAT_80619c48;
    from[2] = *(float *)(zone + 0x14);
    *(int *)(self + 0x34) = (int)*(short *)(zone + 10);

    int ci = *(short *)(rec + 0x48) * 0x30;
    unsigned int hit = (unsigned int)nav_advanced_movement_dispatch(
        (double)(&DAT_8048ca94)[ci / 4], (double)(&DAT_8048ca9c)[ci / 4],
        *(int *)(rec + 8), from, probe, (int *)0, 0, 8);
    probe[1] -= FLOAT_80619c48;
    from[1] -= FLOAT_80619c48;

    ci = *(short *)(rec + 0x48) * 0x30;
    floor_query_with_interact((double)(&DAT_8048ca94)[ci / 4],
                              (double)(&DAT_8048ca9c)[ci / 4],
                              (int)(intptr_t)probe, &floorHit, (int *)0, 1);

    if (floorHit == 0 || (*(unsigned short *)(floorHit + 8) & 0x1804) == 0) {
        if ((hit & 2) == 0) {
            outPos[0] = *(float *)(tgt + 0xc);
            outPos[1] = *(float *)(tgt + 0x10);
            outPos[2] = *(float *)(tgt + 0x14);
        } else {
            outPos[0] = probe[0];
            outPos[1] = probe[1];
            outPos[2] = zonePos[2] = probe[2];
        }
    } else {
        outPos[0] = from[0];
        outPos[1] = from[1];
        outPos[2] = from[2];
    }
    FUN_80382730();
}

// -----------------------------------------------------------------------------
// aiactor_select_spawn_zone (0x802b83d4) - choose the best spawn zone for the
// actor. Story mode scores a two-hop soundpad zone set by distance to every
// active actor and keeps the farthest; otherwise it random-picks a valid zone.
// Returns the resolved zone-node pointer (via r3).
// -----------------------------------------------------------------------------
int *aiactor_select_spawn_zone(void)
{
    int self = FUN_803826d8();
    int *rec = *(int **)(self + 0x7c);
    int chosen = -1;
    int zones[35];
    int zoneCount = 0;

    if (DAT_805015be == 0x13 && DAT_80612610 <= rec[2] &&
        player_ai_is_story_mode((int)*(short *)((char *)rec + 0x24)) != 0) {
        if (DAT_80611918 != 0) {
            float best = FLOAT_80619c54;
            soundpad_query_two_hop_zones(
                *(int *)(*(int *)(DAT_80611918 + 0x7c) + 0x678), zones, &zoneCount,
                0, (int *)0, 0x14, 0x1c0020);
            if (zoneCount == 0) {
                zoneCount = 1;
                zones[0] = *(int *)(*(int *)(DAT_80611918 + 0x7c) + 0x678);
            }
            int seed = zoneCount;
            int descIdx = 0;
            for (int d = 0; d < DAT_80611c0c && zoneCount < 0x14; d++) {
                int desc = DAT_80611b94 + descIdx;
                for (int s = 0; s < seed; s++) {
                    int eid = soundpad_get_entity_id_from_zone(zones[s]);
                    int did = (int)*(short *)(desc + 8);
                    if (did == eid) {
                        zones[zoneCount++] = soundpad_find_zone_by_id(did);
                    }
                }
                descIdx += 0x20;
            }
            for (int zi = 0; zi < zoneCount; zi++) {
                float nearest = FLOAT_80619c30;
                int ai = 0;
                for (int a = 0; a < DAT_80612098; a++) {
                    int *actor = *(int **)((int)&DAT_80588610 + ai);
                    if (actor != rec) {
                        int *node = soundpad_resolve_zone_node(zones[zi]);
                        float dz = *(float *)(*actor + 0x6c) - (float)node[5];
                        float dx = *(float *)(*actor + 0x64) - (float)node[3];
                        float d2 = dx * dx + dz * dz;
                        if (d2 < nearest)
                            nearest = d2;
                    }
                    ai += 4;
                }
                if (best < nearest) {
                    chosen = zones[zi];
                    best = nearest;
                }
            }
        }
        if (chosen == -1)
            chosen = 0;
        int *node = soundpad_resolve_zone_node(chosen);
        if ((node[1] & 0x800) == 0)
            rec[0x19e] = chosen;
        else
            rec[0x19e] = soundpad_find_zone_by_id((int)*(short *)((char *)node + 8));
    } else {
        int tries = 0;
        int again;
        do {
            int pick = spawn_select_character_wrapper2();
            int *node = soundpad_resolve_zone_node(pick);
            if ((node[1] & 0x800) == 0)
                rec[0x19e] = pick;
            else
                rec[0x19e] = soundpad_find_zone_by_id((int)*(short *)((char *)node + 8));
            again = tracker_stub_zero_a();
        } while (again != 0 && ++tries < 10);
    }

    FUN_80382724();
    return soundpad_resolve_zone_node(rec[0x19e]);
}

// -----------------------------------------------------------------------------
// aiactor_behavior_death_setup (0x802b798c) - behaviour state 9: register the
// actor's death, reset respawn state, choose a respawn zone + anchor position,
// and queue the correct death/respawn animation action by mode.
// -----------------------------------------------------------------------------
void aiactor_behavior_death_setup(unsigned long long param_1, double param_2, double param_3,
                                  double param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8)
{
    int self = (int)__save_gpr_25();
    int rec = *(int *)(self + 0x7c);
    int recIdx = (rec - DAT_80611d90) / 0xea0;

    if (DAT_805015dc != 0 && *(short *)(rec + 0xa60) == 0) {
        *(unsigned int *)(self + 0x28) &= 0xffffff9f;
        __restore_gpr_25();
        return;
    }

    player_ai_respawn_state_reset();
    aiactor_pick_death_weapon(self);
    *(int *)(rec + 4) = 0;
    *(int *)(rec + 0x24) = 1;
    *(float *)(self + 0x1d8) = damage_splash_level_gate();
    *(int *)(self + 0x1cc) = *(int *)(self + 0x1d8);
    *(int *)(self + 0x1d0) = *(int *)(self + 0x1d8);
    *(float *)(rec + 0x7c) = damage_get_max_radius();
    *(float *)(rec + 0x78) = FLOAT_80619c4c;
    *(int *)(rec + 0x9c0) = 0;
    *(int *)(rec + 0xad0) = *(int *)(rec + 0xb14);

    int storyGate = (DAT_805015be == 0x13 && *(int *)(rec + 8) >= DAT_80612610 &&
                     player_ai_is_story_mode((int)*(short *)(rec + 0x48)) != 0 &&
                     DAT_806118dc == 0);
    if (!storyGate) {
        float local_pos[3];
        *(float *)(rec + 0xb70) = FLOAT_80619c30;
        int *zone = aiactor_select_spawn_zone();
        if (zone == (int *)0)
            zone = aiactor_resolve_zone_or_node(rec);
        aiactor_compute_zone_anchor_pos(self, zone, local_pos);
        *(float *)(self + 100) = local_pos[0];
        *(int *)(self + 0x68) = *(int *)&local_pos[1];
        *(int *)(self + 0x6c) = *(int *)&local_pos[2];
        *(int *)(self + 0x70) = zone[6];
        *(int *)(self + 0xc4) = *(int *)(self + 0x70);
        aiactor_update_zone_anchor_position();

        float p0 = *(float *)(self + 100), p1, p2;
        int ci = *(short *)(rec + 0x48) * 0x30;
        nav_movement_dispatch((double)(&DAT_8048ca94)[ci / 4], (double)(&DAT_8048ca9c)[ci / 4],
                              recIdx, self + 100, &p0, &p1, (int *)0, 1, 0);
        ci = *(short *)(rec + 0x48) * 0x30;
        nav_movement_dispatch((double)(&DAT_8048ca94)[ci / 4], (double)(&DAT_8048ca9c)[ci / 4],
                              recIdx, (int)(intptr_t)&p0, &p1, (float *)0, (int *)0, 1, 0);
        *(float *)(self + 100) = p1;

        ci = *(short *)(rec + 0x48) * 0x30;
        float floorY = floor_query_with_interact((double)(&DAT_8048ca94)[ci / 4],
                                                 (double)(&DAT_8048ca9c)[ci / 4],
                                                 self + 100, (int *)0, (int *)0, 0);
        *(float *)(rec + 0xb70) = floorY;
        *(int *)(rec + 0xbc0) = *(int *)(rec + 0xb70);
        if ((zone[1] & 0x800) == 0)
            zone = (int *)0;
        *(int **)(rec + 0xacc) = zone;
    }

    *(int *)(self + 0xec) = *(int *)(self + 100);
    *(int *)(self + 0xf0) = *(int *)(self + 0x68);
    *(int *)(self + 0xf4) = *(int *)(self + 0x6c);
    *(int *)(rec + 0xaf8) = *(int *)(self + 100);
    *(int *)(rec + 0xafc) = *(int *)(self + 0x68);
    *(int *)(rec + 0xb00) = *(int *)(self + 0x6c);
    if (*(int *)(rec + 0xbf0) != 0 &&
        (*(unsigned int *)(*(int *)(rec + 0xbf0) + 0x54) & 0x100) != 0 &&
        *(int *)(rec + 4) != 0) {
        aiactor_turn_toward_leader();
    }
    *(int *)(rec + 0xba0) = *(int *)(self + 100);
    *(int *)(rec + 0xba4) = *(int *)(self + 0x68);
    *(int *)(rec + 0xba8) = *(int *)(self + 0x6c);
    *(int *)(rec + 0xab4) = 0;
    *(int *)(rec + 0xab0) = 0;
    *(int *)(rec + 0x9d0) = 0;
    *(int *)(rec + 0x20) = 0;
    *(int *)(rec + 0xadc) = 0;
    *(int *)(rec + 0xbac) = 0;
    *(float *)(rec + 0xb38) = FLOAT_80619c4c;
    entity_assign_room(self);
    aiactor_update_zone_anchor_position();
    *(unsigned int *)(rec + 0x28) &= 0xfffffeff;

    int coopKill = ((DAT_805015c0 & 0x40) != 0 &&
                    (DAT_805015d8 == 0xf || (unsigned int)(DAT_805015d8 - 10) <= 1 ||
                     DAT_805015d8 == 0x13 || (unsigned int)(DAT_805015d8 - 0x10) <= 1));
    if (!coopKill)
        character_death_start(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);

    if (DAT_805015bd == 5) {
        if ((DAT_806120d8 & (1 << *(int *)(rec + 8))) == 0) {
            if (*(int *)(rec + 0xaac) != 1 || *(int *)(rec + 0xaa8) != 0 ||
                (*(unsigned int *)(rec + 0x1c) & 1) != 0) {
                *(int *)(rec + 0xaac) = 0x800;
                *(int *)(rec + 0xaa8) = 0;
            }
        } else {
            character_damage_effect_wrapper();
        }
    } else if ((*(unsigned int *)(rec + 0x28) & 0x10000000) == 0) {
        *(unsigned int *)(rec + 0xc6c) &= 0xfffffffb;
    } else {
        character_damage_effect_wrapper();
    }

    if (DAT_805015be != 8)
        anim_clear_render_scale(rec);

    // Mode-dependent death/respawn animation action selection.
    int action = 0x40000;   // default
    if (DAT_805015bd == 9) {
        anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c50);
        action = 0x1000000;
        if ((DAT_805015c0 & 0x40) != 0 &&
            (DAT_805015d8 == 0xf || DAT_805015d8 == 0x10 ||
             (unsigned int)(DAT_805015d8 - 10) <= 1 || DAT_805015d8 == 0x11)) {
            action = 0x1000000;
        } else if ((DAT_805015c0 & 0x40) != 0 && DAT_805015d8 == 0x13) {
            *(unsigned int *)(self + 0x28) &= 0xffffff9e;
            action = 0x10000000;
        } else if (DAT_806118dc != 0) {
            int list = match_build_weapon_list();
            if (*(int *)(list + 4) > 0 && *(int *)(list + 8) > 0) {
                unsigned int r = rng_counter_tick();
                *(int *)(rec + 0x94) =
                    *(int *)(list + (r - (r / *(unsigned int *)(list + 4)) *
                                             *(unsigned int *)(list + 4)) * 4 + 8);
                *(unsigned char *)(rec + *(int *)(rec + 0x94) + 0x54c) = 1;
                object_write_sound_slots_all();
            }
            action = 0x8000;
        } else {
            action = 0x10000;
        }
    } else if (DAT_805015bd == 3) {
        anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c50);
        action = (*(int *)(rec + 0x94) == 0) ? 0x40000 : 0x100000;
    } else {
        int sub = *(int *)(rec + 0x44);
        anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c50);
        action = (sub >= 1 && sub <= 2) ? 0x10 : 0x40000;
    }

    if (!ai_action_already_latched((int *)rec)) {
        *(int *)(rec + 0xaac) = action;
        *(int *)(rec + 0xaa8) = 0;
    }

    if (DAT_80611934 != 7)
        *(unsigned int *)(rec + 0x2c) |= 4;

    __restore_gpr_25();
}

// -----------------------------------------------------------------------------
// aiactor_behavior_engage_tick (0x802b86dc) - engage/target behaviour state:
// resolve target zone, run retargeting, follow the zone path, and choose the
// stance/movement action for the current game mode.
// -----------------------------------------------------------------------------
void aiactor_behavior_engage_tick(void)
{
    int self = (int)__save_gpr_28();
    int rec = *(int *)(self + 0x7c);

    aiactor_resolve_target_zone(self);
    if (aiactor_seek_rally_marker(self) != 0) {
        __restore_gpr_28();
        return;
    }
    if (*(int *)(rec + 8) == DAT_806120f8)
        aiactor_pick_best_weapon();
    if (*(int *)(rec + 700) != 0) {
        *(unsigned int *)(rec + 0x28) |= 0x40000;
        anim_set_playback_rate((double)FLOAT_80619c58, *(int **)(self + 4), 2);
    }

    if (aiactor_retarget_visible_teammate() != 0) {
        if (DAT_805015bd == 1) {
            if (*(int *)(rec + 0x80) != 0) {
                if (*(int *)(rec + 0xaac) != 1 || *(int *)(rec + 0xaa8) != 0 ||
                    (*(unsigned int *)(rec + 0x1c) & 1) != 0) {
                    *(int *)(rec + 0xaac) = 0x800;
                    *(int *)(rec + 0xaa8) = 0;
                }
                __restore_gpr_28();
                return;
            }
            if (*(int *)(rec + 4) != 0 && *(int *)(*(int *)(rec + 4) + 0x20) == 0x40 &&
                tracker_player_flag_bit4(5) != 0) {
                int t = *(int *)(rec + 4);
                if (DAT_805015bd == 10 || t == 0 ||
                    *(int *)(t + 0x20) != 0x40 ||
                    *(int *)(*(int *)(t + 0x7c) + 0xc) != 0x80) {
                    *(int *)(rec + 4) = tracker_find_owned_actor(5);
                }
            }
            if (*(int *)(rec + 4) != 0 && *(int *)(*(int *)(rec + 4) + 0x20) == 8) {
                if (!ai_action_already_latched((int *)rec)) {
                    *(int *)(rec + 0xaac) = 4;
                    *(int *)(rec + 0xaa8) = 0;
                }
                __restore_gpr_28();
                return;
            }
        } else {
            int t = *(int *)(*(int *)(rec + 4) + 0x20);
            if (t == 8 || t == 0x200) {
                if (!ai_action_already_latched((int *)rec)) {
                    *(int *)(rec + 0xaac) = 4;
                    *(int *)(rec + 0xaa8) = 0;
                }
                __restore_gpr_28();
                return;
            }
        }
    }

    if (aiactor_retarget_scan() != 0) {
        __restore_gpr_28();
        return;
    }

    if (aiactor_follow_zone_path() == 0) {
        if (FLOAT_80619c64 < *(float *)(rec + 0xb30) &&
            (*(unsigned int *)(rec + 0x28) & 1) != 0) {
            *(int *)(rec + 0xabc) = 4;
            *(int *)(rec + 0xab8) = 0;
            if (!ai_action_already_latched((int *)rec)) {
                *(int *)(rec + 0xaac) = 0x20;
                *(int *)(rec + 0xaa8) = 0;
            }
        }
        __restore_gpr_28();
        return;
    }

    float range = ((*(unsigned int *)(rec + 0x2c) & 0x100) != 0) ? FLOAT_80619c5c
                                                                 : FLOAT_80619c60;
    if (*(float *)(rec + 0x9b0) <= range * range) {
        switch (DAT_805015bd) {
        case 1:
            if (*(int *)(rec + 0x80) == 0) {
                if (!ai_action_already_latched((int *)rec)) {
                    *(int *)(rec + 0xaac) = 0x10;
                    *(int *)(rec + 0xaa8) = 0;
                }
            } else {
                if (!ai_action_already_latched((int *)rec)) {
                    *(int *)(rec + 0xaac) = 0x800;
                    *(int *)(rec + 0xaa8) = 0;
                }
            }
            break;
        case 2:
        case 9:
            *(int *)(rec + 0xaf0) = 0;
            if (!ai_action_already_latched((int *)rec)) {
                *(int *)(rec + 0xaac) = 0x40000;
                *(int *)(rec + 0xaa8) = 0;
            }
            break;
        case 3:
            if (!ai_action_already_latched((int *)rec)) {
                *(int *)(rec + 0xaac) = 0x100000;
                *(int *)(rec + 0xaa8) = 0;
            }
            break;
        default:
            break;
        }
    } else {
        *(int *)(rec + 0x30) = *(int *)(*(int *)(rec + 0x67c) + 0xc);
        *(int *)(rec + 0x34) = *(int *)(*(int *)(rec + 0x67c) + 0x10);
        *(int *)(rec + 0x38) = *(int *)(*(int *)(rec + 0x67c) + 0x14);
        aiactor_set_stance_flags(self, 1);
    }
    __restore_gpr_28();
}

// -----------------------------------------------------------------------------
// aiactor_behavior_reset_state (0x802b8cc4) - reset the tracker / cost-matrix
// state blocks at the start of a match (mode 3 clears the objective tables).
// -----------------------------------------------------------------------------
void aiactor_behavior_reset_state(void)
{
    if (DAT_805015bd == 3) {
        memset_wrapper(DAT_806120c4, 0, 0x548);
        if (DAT_80501572 > 0)
            memset_wrapper(DAT_806120c8, 0, 0x96c);
    }
    if (tracker_costmatrix_mode_active() != 0)
        FUN_802768f0();
    DAT_806120d8 = 0;
    FLOAT_806120d4 = FLOAT_80619c4c;
    memset_wrapper((int)0x805bccd0, 0, 0x78);  // r13 - 0x7fa34330
    DAT_80612a98 = 0;
}

// -----------------------------------------------------------------------------
// aiactor_behavior_alloc_state (0x802b8d64) - allocate the tracker state block
// for the match. Wide (16-arg) PPC ABI passthrough to the allocator + optional
// cost-matrix init.
// -----------------------------------------------------------------------------
void aiactor_behavior_alloc_state(unsigned long long param_1, unsigned long long param_2,
        unsigned long long param_3, unsigned long long param_4, unsigned long long param_5,
        unsigned long long param_6, unsigned long long param_7, unsigned long long param_8,
        int param_9, int param_10, int param_11, int param_12, int param_13, int param_14,
        int param_15, int param_16)
{
    (void)param_9;
    if (DAT_805015bd == 3) {
        param_1 = tracker_alloc_state(param_1, param_2, param_3, param_4, param_5, param_6,
                                      param_7, param_8, (void *)&DAT_80501570, param_10,
                                      param_11, param_12, param_13, param_14, param_15, param_16);
    }
    unsigned int cm = tracker_costmatrix_mode_active();
    if (cm != 0) {
        FUN_80276930(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     cm, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
    }
}

// -----------------------------------------------------------------------------
// aiactor_behavior_spawn_all (0x802b8da8) - spawn the full bot roster for the
// match, assign teams, register them in the visibility list, then run the
// tracker/objective precompute and mode-specific bookkeeping.
// -----------------------------------------------------------------------------
void aiactor_behavior_spawn_all(unsigned long long param_1, double param_2, double param_3,
                                double param_4, unsigned long long param_5,
                                unsigned long long param_6, unsigned long long param_7,
                                unsigned long long param_8)
{
    (void)param_1;
    __save_gpr_28();

    for (int i = 0, off = 0; i < DAT_806120fc; i++, off += 4) {
        unsigned int chr = (unsigned int)DAT_80501570[DAT_80612610 + i + 0x14];
        int bot = player_ai_spawn_bot((double)FLOAT_80619c4c, (double)FLOAT_80619c4c,
                                      (double)FLOAT_80619c4c, (double)FLOAT_80619c4c,
                                      param_5, param_6, param_7, param_8,
                                      chr, i + DAT_80612610, -1, 1,
                                      (int)(char)DAT_80501570[DAT_80612610 + i + 0x3e], 0);
        *(int *)((int)&DAT_80588a1c + off) = bot;
        player_ai_assign_team(*(int *)(*(int *)((int)&DAT_80588a1c + off) + 0x7c));
        aiactor_visible_list_add(*(int *)((int)&DAT_80588a1c + off));
    }

    if (DAT_806120fc > 0) {
        unsigned long long ctx;
        if (tracker_costmatrix_mode_active() == 0) {
            if (DAT_805015bd == 3)
                ctx = tracker_populate_from_level();
            else
                ctx = 0;
        } else {
            FUN_802bc1a4();
            ctx = tracker_count_actors_per_team();
        }
        if (DAT_80611568 == 0)
            tracker_precompute_zone_objectives(ctx, param_2, param_3, param_4,
                                               param_5, param_6, param_7, param_8);
    }

    unsigned int base = (unsigned int)DAT_80612610;
    if (DAT_805015bd == 5) {
        int idx;
        if ((int)DAT_80612610 < DAT_80612098) {
            unsigned int r = rng_counter_tick();
            idx = base + (int)((float)((DAT_80612098 - 1) - (int)base) *
                                   ((float)(double)(r) / FLOAT_80619c34) + FLOAT_80619c48);
        } else {
            unsigned int r = rng_counter_tick();
            idx = r - (r / DAT_80612610) * DAT_80612610;
            character_damage_effect_wrapper();
        }
        DAT_806120d8 = 1 << idx;
    }

    if (DAT_805015be == 0x12 && DAT_80611910 == 0) {
        if ((int)DAT_80612610 < DAT_80612098) {
            DAT_80611910 = *(int *)(DAT_80611d90 + DAT_80612610 * 0xea0);
        } else {
            unsigned int r = rng_counter_tick();
            DAT_80611910 = *(int *)(DAT_80611d78 +
                                    (r - (r / DAT_80612610) * DAT_80612610) * 0x141c + 0x14);
        }
        character_death_effect_reset(
            (unsigned int *)(DAT_80611d90 +
                             *(int *)(*(int *)(DAT_80611910 + 0x7c) + 8) * 0xea0 + 0xc68));
    }

    __restore_gpr_28();
}

// -----------------------------------------------------------------------------
// aiactor_roll_attack_chance (0x802b8ff4) - RNG-driven decision helper: returns
// 1 when the actor should commit to an attack this frame, else 0. Gated on
// energy/fatigue, an existing target, and a class-flag scan.
// -----------------------------------------------------------------------------
unsigned int aiactor_roll_attack_chance(int rec)
{
    if (*(float *)(rec + 0x9e0) > *(float *)(rec + 0xb34))
        return 0;
    if (*(int *)(rec + 4) == 0 || *(float *)(rec + 0xaf4) < FLOAT_80619c70)
        return 0;
    if (tracker_scan_class_flag400() == 0)
        return 0;

    unsigned int r = rng_counter_tick();
    unsigned int clz = countLeadingZeros(
        (int)(FLOAT_80619c74 * ((float)((double)r - DOUBLE_80619c40) / FLOAT_80619c34) +
              FLOAT_80619c48));
    return clz >> 5;
}

} // extern "C"
