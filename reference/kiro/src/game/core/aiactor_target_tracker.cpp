// ============================================================================
// aiactor_target_tracker.cpp
//
// Per-player AI target-tracking / line-of-sight subsystem. Owns two global
// state blocks allocated at level start (DAT_806120c4 = 0x550 bytes core state;
// DAT_806120c8 = 0x970 bytes per-map tracking arrays, only in the co-op story
// mode where DAT_80501572 is set). Each frame it refreshes the local player's
// tracked-entity list, tests target visibility with a scene raycast, and
// notifies related AI actors.
//
// Tracking record layout (offsets used here):
//   +0x08  bound source entity ptr
//   +0x0e  active member count (short)
//   +0x10  resolved tag/target count (short)
//   +0x14  member ptr array (stride 0x14)
//   +0xdc  per-target slot array (stride 8: [+0]=entity, [+4]=timestamp)
//
// Address range: 0x80272c44 - 0x80273283 | batch_747 | 2026-09-09
//
// Functions:
//   tracker_alloc_state          0x80272c44  allocate the two global blocks
//   tracker_populate_from_level  0x80272c94  fill per-map arrays from level info
//   tracker_update_visibility    0x80272de0  per-frame LOS/visibility refresh
//   tracker_splice_entries       0x8027317c  move tracking entries between recs
// ----------------------------------------------------------------------------

#include "common.h"

extern "C" {
// ---- frame prologue/epilogue helpers ---------------------------------------
extern long long FUN_803826e4(void);       // packed prologue (hi=entity, lo=aux)
extern long long FUN_803826e8(void);       // packed prologue (hi=entity, lo=aux)
extern int       FUN_803826dc(void);        // prologue (returns entity handle)
extern int       FUN_803826f0(void);        // prologue (entity handle)
extern void      FUN_80382730(void);        // epilogue
extern void      FUN_80382734(void);        // epilogue
extern void      FUN_80382728(void);        // epilogue
extern void      FUN_8038273c(void);        // epilogue
extern int       FUN_803826b8(void);        // packed prologue (batch_749)
extern void      FUN_80382704(void);        // epilogue (batch_749)
extern int       __save_gpr_25(void);       // frame save (returns entity handle)
extern void      __restore_gpr_25(void);    // frame restore
extern int       __save_gpr_28(void);       // frame save (returns entity handle)
extern void      __restore_gpr_28(void);    // frame restore
extern int       FUN_803826c8(void);        // prologue (returns entity handle)
extern void      FUN_80382714(void);        // epilogue (batch_750)
extern void      FUN_802b6d9c(void);        // player-facing aim/HUD update
extern void      aiactor_pick_best_weapon(void); // best-weapon selection pre-tick
extern void      FUN_80280c8c(int actor);   // combat strafe/aim tick

// ---- named AI/anim/match helpers used by the batch_750 FSMs ----------------
// (generated_decls.h is NOT included by common.h; each TU declares the raw
//  symbols it uses, so these match the call sites in this file)
extern int   aiactor_retarget_visible_teammate(void);
extern int   aiactor_follow_zone_path(void);       // returns arrived flag
extern int   aiactor_follow_zone_path_alt(void);   // returns arrived flag
extern bool  aiactor_aim_alignment_test(double threshold, int actor, float* targetPos);
extern void  aiactor_set_stance_flags(int actor, unsigned int flag);
extern void  aiactor_play_turn_animation(int actor);
extern void  aiactor_facing_update(void);
extern void  aiactor_turn_toward_leader(void);
extern bool  aiactor_turn_toward_angle(double targetYaw, int actor, int instant);
extern void  aiactor_turn_toward_entity(unsigned int actor, unsigned int target, int instant);
extern void  aiactor_leader_zone_update(unsigned int actor);
extern int   anim_is_clip_active(int actor, int clipId);
extern void  anim_play_scaled(double a, double b);
extern void  anim_set_playback_rate(double rate, int* animCtx, int which);
extern int   match_build_weapon_list(void);        // returns weapon-list ptr
extern void  switch_use_handler(void);
extern void  switch_hold_use_handler(void);
extern int*  worm_class_find_first_active(void);
extern int   FUN_8027f80c(void);                    // broad target acquire (returns handle)

// ---- allocation + level info -----------------------------------------------
extern int  mem_alloc(long long a, long long b, long long c, long long d, long long e,
                      long long f, long long g, long long h, int size, int align,
                      int i, int j, int k, int l, int m, int n);
extern int  level_get_info_cached(int levelId);
extern void memset_wrapper(int dst, int val, int len);
extern int  entity_tag_resolve_all(int tagSet, int outArray, int max);

// ---- soundpad zone helpers -------------------------------------------------
extern int* soundpad_resolve_zone_node(int zoneId);
extern int  soundpad_get_entity_zone_id(int node);
extern int  soundpad_find_zone_for_entity(int entityZone);

// ---- scene / raycast -------------------------------------------------------
extern int  scene_raycast(int scene, float* from, float* to, int flags0, float* hitPos,
                          float* hitNorm, int* hitFace, int mask, int arg8, int* arg9);

// ---- named cross-module helpers (batch_749) --------------------------------
extern unsigned int rng_counter_tick(void);
extern int   prop_can_target(int actor, int target, int flag, int arg3);
extern void  player_ai_projectile_count_in_radius(void); // fills 4 stack counters
extern int   soundpad_find_zone_by_id(int zoneId);
extern int   soundpad_get_entity_id_from_zone(int zoneId);
extern double FUN_8027919c(int zoneA, int zoneB);         // zone-path distance
extern int   worm_class_collect_members(int* out, int a, int* b, int max);
extern int   worm_class_find_first_active_index(void);
extern int   worm_class_find_nearest_member(void);

// ---- globals ---------------------------------------------------------------
extern int   DAT_806120c4;      // core tracking state block ptr
extern int   DAT_806120c8;      // per-map tracking arrays block ptr
extern char  DAT_80501572;      // co-op story mode enable flag
extern char  DAT_805015bd;      // active game-mode selector
extern int   DAT_80612614;      // current level id
extern int   DAT_80611d90;      // player-state array base
extern int   DAT_806120f8;      // local player index
extern int   DAT_806108e4;      // tracker-enabled flag
extern int   DAT_80588610;      // spawn-visible actor table base
extern int   DAT_80612610;      // spawn-vis iterate start index
extern int   DAT_80612098;      // spawn-vis iterate end index
extern int   DAT_80611928;      // global frame timestamp
extern int   DAT_806109c0;      // frame delta (int)
extern float FLOAT_80619190;    // visibility score threshold
extern float FLOAT_80619194;    // initial "nearest" distance sentinel (large)

// ---- batch_749 globals -----------------------------------------------------
extern int    DAT_806128ac;     // per-class event-timestamp table base
extern int    DAT_80611d78;     // active-player context ptr
extern int    DAT_80611b1c;     // per-zone "blocked/solid" flag byte table
extern float  FLOAT_80619198;   // zone-distance accept band
extern float  FLOAT_8061919c;   // zone-separation squared distance threshold
extern float  FLOAT_806191a0;   // random-index scale base
extern float  FLOAT_806191a4;   // random-index scale divisor
extern double DOUBLE_806191a8;  // int->double bias (rng)
extern double DOUBLE_806191b0;  // int->double bias (sign-flip)
extern float  FLOAT_806191b8;   // member-proximity squared radius
extern float  FLOAT_806191bc;   // replan idle-time threshold
extern float  FLOAT_806191c0;   // replan probability base
extern float  FLOAT_806191c4;   // replan probability scale
extern float  FLOAT_806191c8;   // member-threat hard squared radius
extern float  FLOAT_806191cc;   // member-threat soft squared radius
extern float  FLOAT_806191d0;   // member-threat relative-distance factor
extern float  FLOAT_806191d4;   // decision retarget band (mode 2/3)
extern float  FLOAT_806191d8;   // decision retarget band (mode 1)
extern float  FLOAT_806191dc;   // decision retarget band (mode <=0)
extern float  FLOAT_806191e0;   // snap-to-zone squared radius
extern float  FLOAT_806191e4;   // advance squared radius (mode 2/3)
extern float  FLOAT_806191e8;   // advance squared radius (mode <=1)

// ---- batch_750 globals -----------------------------------------------------
extern int    DAT_805889f0;     // co-op player-slot table (stride 2 words)
extern int    DAT_804d34dc;     // per-anim-descriptor flag table (stride 8)
extern short  DAT_804d1cd0;     // anim clip -> descriptor index table
extern float  FLOAT_806108ec;   // state-timer accumulation scale
extern float  FLOAT_806191ec;   // default animation playback rate
extern float  FLOAT_806191f0;   // leader re-lock squared radius
extern float  FLOAT_806191f4;   // GHZ close-combat squared radius
extern float  FLOAT_806191f8;   // regroup clip blend rate

// ---- batch_751 globals + helpers -------------------------------------------
extern int*  DAT_806120cc;      // per-team spawn stat block (stride 0x10)
extern int   DAT_80611934;      // active game-type id
extern int   DAT_806129d8;      // co-op players round-robin index
extern unsigned int FUN_80278ff0(void); // 1 when the match is running

// ---- batch_752 externs (target-selection FSM) ------------------------------
// combat-state block resolver: maps a source-entity handle to its combat-state
// record; [+0]=combat mode (0/1/2/3), [+1]=state timestamp.
extern int*  FUN_80278fbc(int sourceHandle);
extern int   FUN_803826c4(void);            // prologue (returns entity handle)
extern void  FUN_80382710(void);            // epilogue (batch_752 driver)
extern int   aiactor_relation_entry(int selfClass, int otherHandle); // -> char* relation flag
extern void  aiactor_turn_toward_position(int actor, float* worldPos, int instant);
extern void  mtx4_set_rotation_y(double yaw, float* outMtx);
extern void  mtx4_transform_vector(float* mtx, float* vec);

// batch_752 tuning constants (co-op AI target-selection distances/angles)
extern float  FLOAT_80619200;   // owner-repath time-gate scale
extern float  FLOAT_80619204;   // enemy-owner retarget squared radius
extern float  FLOAT_80619208;   // player-owner drop squared radius
extern float  FLOAT_8061920c;   // teammate retarget squared radius
extern float  FLOAT_80619210;   // "nearest" distance sentinel (large)
extern float  FLOAT_80619220;   // state-timer reset value
extern float  FLOAT_80619224;   // state-timer decay numerator
extern float  FLOAT_80619228;   // regroup close squared radius
extern float  FLOAT_8061922c;   // regroup clip blend rate
extern float  FLOAT_80619230;   // strafe rotation offset scale
extern float  FLOAT_80619234;   // strafe rotation offset divisor
extern float  FLOAT_80619238;   // follow-mode break squared radius
extern float  FLOAT_8061923c;   // owner-follow lock squared radius
extern float  FLOAT_80619240;   // zone-score bias base
extern double DOUBLE_80619218;  // int->double conversion bias
extern float  DAT_803e0558;     // identity offset vector .x
extern float  DAT_803e055c;     // identity offset vector .y
extern float  DAT_803e0560;     // identity offset vector .z
extern int    DAT_80472be0;     // per-team combat-role priority table (stride 0x10)
}

// ---- forward declarations for the batch_748 record-management helpers ------
// (defined at the bottom of this file; declared here so the header functions
//  above and the helpers below can call each other regardless of definition
//  order)
void tracker_bind_record(void);
void tracker_steal_nearest_visible(void);
int  tracker_get_group_bucket(int group, int* out);
int  tracker_register_entity(int entity);
int  tracker_get_bound_source(int entity);
int  tracker_find_record_for_entity(int entity);
int* tracker_find_level_entity(int handle);
int  tracker_find_record_by_owner(int handle);
int  tracker_find_member_slot(int entity, int rec);
void tracker_drop_target_slot(int rec, int entity);

// ---- forward declarations for the batch_749 selection/decision helpers -----
void tracker_add_target_slot(int rec, int entity);
void tracker_pick_random_visible_target(void);
int  tracker_select_best_target(void);
unsigned int tracker_no_incoming_projectiles(void);
int  tracker_query_mode4_event(int actor, int event);
int  tracker_event_steal_targets(int actor, int event);
void tracker_pick_destination_zone(int actorHi, int nodeLo, unsigned int flags);
int  tracker_group_replan_gate(void);
int  tracker_member_under_threat(int actor);
void tracker_actor_decision_tick(int entity, int levelDesc, int forceLocal);

// ---- forward declarations for the batch_750 bot-behavior helpers -----------
void tracker_bot_behavior_tick(void);
void tracker_bot_behavior_tick_variant(void);
unsigned int tracker_player_flag_bit4(int playerIdx);
unsigned int tracker_other_player_flag_bit4(int selfIdx);
unsigned int tracker_player_active_flag(int playerIdx);
unsigned int tracker_class_owner_active_flag(int actor);
int  tracker_player_self_owns_target(int playerIdx);
int  tracker_normalize_player_zone(int playerIdx);
int  tracker_normalize_other_player_zone(int selfIdx);
int  tracker_get_player_dest_zone(int playerIdx);

// ---- forward declarations for the batch_751 coop-tick helpers --------------
int  tracker_other_player_dest_zone(int selfIdx);
int  tracker_get_player_actor(int playerIdx);
int  tracker_other_player_actor(int selfIdx);
int  tracker_find_owned_actor(int team);
int  tracker_other_find_owned_actor(int selfIdx);
void tracker_count_actors_per_team(void);
void tracker_coop_players_tick(void);
void tracker_player_state_change_tick(int playerIdx);
void tracker_record_player_state(int playerIdx, int state);
int  tracker_stub_return_zero(void);

// ---- forward declarations for the batch_752 target-selection FSM -----------
int    tracker_is_owner_targeted_by_ally(void);
void   tracker_update_target_selection(void);
int    tracker_event_state_is_idle(int actor);
int    tracker_handle_target_lost_event(int actor, int event);
int    tracker_get_actor_zone(int unused, int actor);
void   tracker_combat_behavior_update(void);
void   tracker_ally_behavior_update(void);
void   tracker_owner_follow_update(void);
double tracker_compute_zone_score(int* rec, int mode);
void   tracker_assign_combat_role(void);

// ----------------------------------------------------------------------------
// tracker_alloc_state 0x80272c44
// Allocates the tracker's global state: the 0x550-byte core block always, and
// (only when the co-op story flag DAT_80501572 is set) the 0x970-byte per-map
// tracking-array block. All the pass-through params are the allocator's ABI
// context (heap/tag args), forwarded unchanged.
// ----------------------------------------------------------------------------
void tracker_alloc_state(long long param_1, long long param_2, long long param_3, long long param_4,
                         long long param_5, long long param_6, long long param_7, long long param_8,
                         int param_9, int param_10, int param_11, int param_12, int param_13,
                         int param_14, int param_15, int param_16)
{
    DAT_806120c4 = mem_alloc(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             0x550, 1, param_11, param_12, param_13, param_14, param_15, param_16);
    if ('\0' < DAT_80501572) {
        DAT_806120c8 = mem_alloc(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                                 param_8, 0x970, 1, param_11, param_12, param_13, param_14,
                                 param_15, param_16);
    }
}

// ----------------------------------------------------------------------------
// tracker_populate_from_level 0x80272c94
// In the co-op story mode (DAT_805015bd == 3), fills the per-map tracking arrays
// (DAT_806120c8) from the level info's entity list: type-1 entries go into the
// primary array (stride 300, count +0x960), type 0/-1 into the secondary array
// (base +0x4b0, count +0x964); +0x968 holds the total. Then registers every
// spawn-visible actor into the tracker (tracker_register_entity).
// ----------------------------------------------------------------------------
void tracker_populate_from_level(void)
{
    FUN_803826f0();
    int info = level_get_info_cached(DAT_80612614);
    if ((info != 0) && (0 < *(int*)(info + 0xf0)) && (DAT_805015bd == '\x03')) {
        int entOff = 0;
        for (int i = 0; i < *(int*)(info + 0xf0); i++) {
            int block = DAT_806120c8;
            int* ent = (int*)(*(int*)(info + 0xec) + entOff);
            short type = *(short*)((int)ent + 8);   // (ent + 2 words) low short
            int slot;
            bool store = false;
            if (type == 1) {
                slot = *(int*)(DAT_806120c8 + 0x960) * 300;
                *(int*)(DAT_806120c8 + 0x960) = *(int*)(DAT_806120c8 + 0x960) + 1;
                store = true;
            } else if ((type < 1) && (-1 < type)) {
                int idx = *(int*)(DAT_806120c8 + 0x964);
                *(int*)(DAT_806120c8 + 0x964) = idx + 1;
                slot = idx * 300 + 0x4b0;
                store = true;
            }
            if (store) {
                int* dst = (int*)(block + slot);
                dst[0] = ent[0];
                *(short*)(dst + 3) = *(short*)((int)ent + 10);
                *(short*)((int)dst + 0xe) = 0;
                dst[1] = ent[3];
                tracker_bind_record();
            }
            entOff += 0x10;
        }
        *(int*)(DAT_806120c8 + 0x968) =
            *(int*)(DAT_806120c8 + 0x960) + *(int*)(DAT_806120c8 + 0x964);
        int visOff = DAT_80612610 << 2;
        for (int i = DAT_80612610; i < DAT_80612098; i++) {
            tracker_register_entity(*(int*)((int)&DAT_80588610 + visOff));
            visOff += 4;
        }
    }
    FUN_8038273c();
}

// ----------------------------------------------------------------------------
// tracker_update_visibility 0x80272de0
// Per-frame refresh of the local player's tracked-target list. When the tracker
// is enabled (DAT_806108e4) and the descriptor is a "list" type (+6 == 1):
//   1. Add spawn-visible enemies in the same zone with a high enough score
//      (tracker_add_target_slot).
//   2. Drop stale targets (score below threshold, or timestamp too old).
//   3. Round-robin one candidate per frame through a scene raycast (from the
//      player eye +0x2ed..+0x2ef to the target center) and mark it visible or
//      occluded (+0x08 byte in the raycast ctx). On a visible hit it records the
//      best target (tracker_select_best_target) into player-state[1].
// ----------------------------------------------------------------------------
void tracker_update_visibility(void)
{
    FUN_803826dc();
    int* pstate = (int*)(DAT_80611d90 + DAT_806120f8 * 0xea0);
    int core = tracker_find_record_for_entity((int)pstate);
    int desc = tracker_get_bound_source((int)pstate);
    int rayCtx = tracker_find_member_slot((int)pstate, core);
    if (DAT_806108e4 == 0) {
        FUN_80382728();
        return;
    }

    // 1. add nearby same-zone visible enemies
    if (*(short*)(desc + 6) == 1) {
        int off = 0;
        for (int i = 0; i < DAT_80612098; i++) {
            int* other = *(int**)((int)&DAT_80588610 + off);
            if (pstate[0x21] != other[0x21]) {
                int* node = soundpad_resolve_zone_node(other[0x19e]);
                int z = soundpad_get_entity_zone_id((int)node);
                z = soundpad_find_zone_for_entity(z);
                if ((*(int*)(desc + 8) == z) && (FLOAT_80619190 <= *(float*)(*other + 0x1cc))) {
                    tracker_add_target_slot(core, *other);
                }
            }
            off += 4;
        }
    }

    // 2. drop stale targets
    if (*(short*)(desc + 6) == 1) {
        int slotOff = 0;
        for (int i = 0; (i < 10) && (0 < *(short*)(core + 0x10)); i++) {
            int tgt = *(int*)(core + slotOff + 0xdc);
            if (tgt != 0) {
                if (*(float*)(tgt + 0x1cc) <= FLOAT_80619190) {
                    tracker_drop_target_slot(core, tgt);
                } else if ((DAT_806109c0 << 1) < (DAT_80611928 - *(int*)(core + slotOff + 0xe0))) {
                    tracker_drop_target_slot(core, tgt);
                }
            }
            slotOff += 8;
        }
    }

    // 3. round-robin one raycast visibility test
    if (0 < *(short*)(core + 0x10)) {
        int idx = *(int*)(rayCtx + 4);
        bool tested = false;
        if (*(short*)(core + 0x10) < idx) {
            idx = 0;
        }
        for (int i = 0; (i < *(short*)(core + 0x10)) && (!tested); i++) {
            int tgt = *(int*)(core + idx * 8 + 0xdc);
            if ((tgt != 0) && (FLOAT_80619190 < *(float*)(tgt + 0x1cc))) {
                float from[3];
                float to[3];
                float hitPos[3];
                from[0] = (float)pstate[0x2ed];
                from[1] = (float)pstate[0x2ee];
                from[2] = (float)pstate[0x2ef];
                to[0] = (*(float*)(tgt + 100) + *(float*)(tgt + 0x1a8)) - from[0];
                to[1] = (*(float*)(tgt + 0x68) + *(float*)(tgt + 0x1ac)) - from[1];
                to[2] = (*(float*)(tgt + 0x6c) + *(float*)(tgt + 0x1b0)) - from[2];
                unsigned int savedFlags = *(unsigned int*)(*pstate + 0x28);
                *(unsigned int*)(*pstate + 0x28) = savedFlags & 0xffffff9f;
                int hit = scene_raycast(*(int*)(*pstate + 0x34), from, to, 0, hitPos,
                                        (float*)0, (int*)0, 0x2000070, 0, (int*)0);
                hitPos[0] = hitPos[0] - from[0];
                hitPos[1] = hitPos[1] - from[1];
                hitPos[2] = hitPos[2] - from[2];
                if (((hit == 0) || (hit == tgt)) ||
                    (to[2] * to[2] + to[0] * to[0] + to[1] * to[1] <
                     hitPos[2] * hitPos[2] + hitPos[0] * hitPos[0] + hitPos[1] * hitPos[1])) {
                    *(unsigned char*)(rayCtx + idx + 8) = 1;
                } else {
                    *(unsigned char*)(rayCtx + idx + 8) = 0;
                }
                tested = true;
                *(unsigned int*)(*pstate + 0x28) = savedFlags;
            }
            idx = (idx + 1) - ((idx + 1) / (int)*(short*)(core + 0x10)) *
                                  (int)*(short*)(core + 0x10);
        }
        *(int*)(rayCtx + 4) = idx;
        if (tested) {
            if (((pstate[7] & 0x200000U) == 0)) {
                int best = tracker_select_best_target();
                if (best != 0) {
                    pstate[1] = best;
                }
            }
        } else {
            *(short*)(core + 0x10) = 0;
        }
    }
    FUN_80382728();
}

// ----------------------------------------------------------------------------
// tracker_splice_entries 0x8027317c
// Moves up to `count` tracking-member entries from one record's member array
// (src +0x0e count, stride 0x14) into the tail of another record's array
// (dst +0x0e count). Clears each moved source slot. Used when re-parenting a
// tracked group between records.
// ----------------------------------------------------------------------------
void tracker_splice_entries(int param_1, int param_2, int param_3)
{
    long long packed = FUN_803826e4();
    int srcRec = tracker_find_record_by_owner((int)((unsigned long long)packed >> 0x20));
    int dstRec = tracker_find_record_by_owner(param_3);
    if ((srcRec != 0) && (dstRec != 0)) {
        int srcCount = (int)*(short*)(srcRec + 0xe);
        int room = (int)packed;
        if (srcCount < room) {
            room = srcCount;
        }
        int dstFree = (int)*(short*)(dstRec + 0xc) - (int)*(short*)(dstRec + 0xe);
        if (dstFree < room) {
            room = dstFree;
        }
        int s = srcCount - 1;
        int srcOff = s * 0x14;
        int lastKept;
        for (; (lastKept = *(short*)(srcRec + 0xe) - room), lastKept <= s; s--) {
            int entryOff = srcOff + 0x14;
            *(int*)(dstRec + *(short*)(dstRec + 0xe) * 0x14 + 0x14) = *(int*)(srcRec + entryOff);
            *(int*)(dstRec + *(short*)(dstRec + 0xe) * 0x14 + 0x18) = 0;
            memset_wrapper(dstRec + *(short*)(dstRec + 0xe) * 0x14 + 0x1c, 0, 10);
            srcOff -= 0x14;
            *(short*)(dstRec + 0xe) = *(short*)(dstRec + 0xe) + 1;
            *(int*)(srcRec + entryOff) = 0;
        }
        *(short*)(srcRec + 0xe) = (short)lastKept;
    }
    FUN_80382730();
}


// ============================================================================
// batch_748 : tracking-record bind + bucket management helpers
//
// This block is the record-management layer under tracker_populate_from_level /
// tracker_update_visibility. It binds a per-map tracking record to its level
// entity descriptor, resolves the per-group bucket arrays inside DAT_806120c8,
// registers/looks-up entities in those buckets, and steals the nearest visible
// enemy from a neighbouring record.
//
// Record layout (extends the header layout above):
//   +0x08  bound level-entity descriptor ptr
//   +0x0c  member capacity (short)
//   +0x0e  active member count (short)
//   +0x10  resolved tag/target count (short)
//   +0x14  member ptr array (stride 0x14: [+0]=entity, [+4]=zero, [+8..]=state)
//   +0xdc  per-target slot array (stride 8: [+0]=entity, [+4]=timestamp)
//
// Bucket block DAT_806120c8:
//   +0x000  primary group array   (stride 300, count at +0x960)
//   +0x4b0  secondary group array (stride 300, count at +0x964)
//   +0x968  total record count
//
// Address range: 0x80273284 - 0x8027395b | batch_748 | 2026-09-09
//
// Functions:
//   tracker_bind_record            0x80273284  bind record to level entity + tags
//   tracker_steal_nearest_visible  0x80273410  move nearest visible enemy to record
//   tracker_get_group_bucket       0x802735d8  bucket array + count by group index
//   tracker_register_entity        0x80273624  add entity to its bucket (alloc slot)
//   tracker_get_bound_source       0x802736c8  return a record's bound source (+8)
//   tracker_find_record_for_entity 0x802736ec  find bucket record containing entity
//   tracker_find_level_entity      0x80273784  level entity descriptor by handle
//   tracker_find_record_by_owner   0x802737f0  find record by owner across buckets
//   tracker_find_member_slot       0x80273864  member slot ptr by entity in record
//   tracker_drop_target_slot       0x802738a0  drop stale target from +0xdc array
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// tracker_bind_record 0x80273284
// Binds a tracking record (resolved from the prologue's owner handle) to its
// level-entity descriptor. On first bind it clears the +0xdc target slots and,
// when the descriptor is a "tag list" (+6 == 0), resolves its tag set into up to
// 10 target slots stamped with the current frame timestamp. Then, for each
// member with a spawnable flag set (and not filtered out), notifies the member
// that it has been re-bound (tracker_actor_decision_tick), respecting the co-op vs solo flag
// mask select (DAT_805015bd == '\n').
// ----------------------------------------------------------------------------
void tracker_bind_record(void)
{
    unsigned long long packed = (unsigned long long)FUN_803826e8();
    int rec = tracker_find_record_by_owner((int)(packed >> 0x20));
    int* levelDesc = (int*)tracker_find_level_entity((int)packed);
    if ((rec != 0) && (levelDesc != (int*)0) && (*(int**)(rec + 8) != levelDesc)) {
        *(int**)(rec + 8) = levelDesc;
        memset_wrapper(rec + 0xdc, 0, 0x28);
        if (*(short*)((int)levelDesc + 6) == 0) {
            int local_48[18];
            int n = entity_tag_resolve_all(levelDesc[2], (int)local_48, 10);
            *(short*)(rec + 0x10) = (short)n;
            int slotOff = 0;
            int srcOff = 0;
            for (int i = 0; i < *(short*)(rec + 0x10); i++) {
                int slot = rec + slotOff;
                slotOff += 8;
                *(int*)(slot + 0xdc) = *(int*)((int)local_48 + srcOff);
                srcOff += 4;
                *(int*)(slot + 0xe0) = DAT_80611928;
            }
        }
        int memOff = 0;
        for (int i = 0; i < *(short*)(rec + 0xe); i++) {
            int* member = *(int**)(rec + memOff + 0x14);
            if ((member[9] & 8) == 0) {
                unsigned int flags;
                if (DAT_805015bd == '\n') {
                    flags = member[5];
                } else {
                    flags = member[7];
                }
                if (((flags & 1) == 0) && ((member[7] & 0x100000) != 0)) {
                    tracker_actor_decision_tick(member[0], (int)(long long)levelDesc, 1);
                }
            }
            memOff += 0x14;
        }
    }
    FUN_80382734();
}

// ----------------------------------------------------------------------------
// tracker_steal_nearest_visible 0x80273410
// Scans every record in the owner's group bucket (skipping the owner's own
// record and any inactive ones) for the visible member entity nearest to the
// owner's zone node. If one is found it is unlinked from its current record's
// member array and re-appended to the owner's record (tracker_find_member_slot
// gives the destination slot). Distance is squared world distance to the zone
// node position (+0x0c..+0x14 of the resolved node).
// ----------------------------------------------------------------------------
void tracker_steal_nearest_visible(void)
{
    unsigned long long packed = (unsigned long long)FUN_803826e4();
    int owner = (int)(packed >> 0x20);
    int ownerHandle = (int)packed;
    int bucket[12];
    bucket[0] = 0;
    int bestRec = 0;
    int* node = soundpad_resolve_zone_node(*(int*)(owner + 0x684));
    double bestDist = (double)FLOAT_80619194;
    int bestSlot = -1;
    bool passedOwner = false;
    if (ownerHandle != 0) {
        int count = tracker_get_group_bucket(*(int*)(owner + 0x84), bucket);
        int recOff = 0;
        for (int r = 0; r < count; r++) {
            int rec = bucket[0] + recOff;
            if (rec == ownerHandle) {
                passedOwner = true;
            } else if ((passedOwner) || (rec == 0) || ((*(unsigned int*)(rec + 4) & 1) == 0)) {
                int members = (int)*(short*)(rec + 0xe);
                int slotIdx = 0;
                int memOff = 0;
                while (0 < members) {
                    int* member = *(int**)(rec + memOff + 0x14);
                    int ent = *member;
                    if ((FLOAT_80619190 < *(float*)(ent + 0x1cc)) && ((member[9] & 8U) == 0)) {
                        float dx = *(float*)(ent + 100) - (float)node[3];
                        float dy = *(float*)(ent + 0x68) - (float)node[4];
                        float dz = *(float*)(ent + 0x6c) - (float)node[5];
                        double d = (double)(dz * dz + dx * dx + dy * dy);
                        if (d < bestDist) {
                            bestSlot = slotIdx;
                            bestRec = rec;
                            bestDist = d;
                        }
                    }
                    slotIdx++;
                    memOff += 0x14;
                    members--;
                }
            }
            recOff += 300;
        }
        if (bestRec != 0) {
            int* dst = (int*)tracker_find_member_slot(owner, ownerHandle);
            int* src = (int*)(bestRec + bestSlot * 0x14 + 0x14);
            int ent = *src;
            *src = owner;
            src[1] = 0;
            memset_wrapper((int)(src + 2), 0, 10);
            *dst = ent;
            dst[1] = 0;
            memset_wrapper((int)(dst + 2), 0, 10);
        }
    }
    FUN_80382730();
}

// ----------------------------------------------------------------------------
// tracker_get_group_bucket 0x802735d8
// Given a group index, writes the base of that group's record array into *out
// and returns its record count. Group 2 -> secondary array (base +0x4b0, count
// +0x964); groups 1 (i.e. 0 < g < 2) -> primary array (base +0, count +0x960).
// Any other index returns 0.
// ----------------------------------------------------------------------------
int tracker_get_group_bucket(int group, int* out)
{
    int count = 0;
    if (group == 2) {
        *out = DAT_806120c8 + 0x4b0;
        count = *(int*)(DAT_806120c8 + 0x964);
    } else if ((group < 2) && (0 < group)) {
        *out = DAT_806120c8;
        count = *(int*)(DAT_806120c8 + 0x960);
    }
    return count;
}

// ----------------------------------------------------------------------------
// tracker_register_entity 0x80273624
// Adds an entity to its group bucket. If it is not already tracked
// (tracker_find_record_for_entity == 0) it scans that group's records for one
// with spare member capacity (+0x0e < +0x0c), appends the entity to that
// record's member array, bumps the count, and returns the record base. Returns
// the existing record if already tracked, or 0 if no room.
// ----------------------------------------------------------------------------
int tracker_register_entity(int entity)
{
    int bucket[5];
    bucket[0] = tracker_find_record_for_entity(entity);
    if (bucket[0] == 0) {
        int count = tracker_get_group_bucket(*(int*)(entity + 0x84), bucket);
        int recOff = 0;
        if (0 < count) {
            do {
                int rec = bucket[0] + recOff;
                short members = *(short*)(rec + 0xe);
                if ((int)members < (int)*(short*)(rec + 0xc)) {
                    *(short*)(rec + 0xe) = members + 1;
                    *(int*)(bucket[0] + recOff + members * 0x14 + 0x14) = entity;
                    return bucket[0] + recOff;
                }
                recOff += 300;
                count--;
            } while (count != 0);
        }
        bucket[0] = 0;
    }
    return bucket[0];
}

// ----------------------------------------------------------------------------
// tracker_get_bound_source 0x802736c8
// Returns the bound source entity (+8) of the record that currently tracks the
// given entity.
// ----------------------------------------------------------------------------
int tracker_get_bound_source(int entity)
{
    int rec = tracker_find_record_for_entity(entity);
    return *(int*)(rec + 8);
}

// ----------------------------------------------------------------------------
// tracker_find_record_for_entity 0x802736ec
// Searches every record in the entity's group bucket for one whose member array
// contains the entity, and returns that record's base (0 if not found).
// ----------------------------------------------------------------------------
int tracker_find_record_for_entity(int entity)
{
    int bucket[5];
    bucket[0] = 0;
    int count = tracker_get_group_bucket(*(int*)(entity + 0x84), bucket);
    int r = 0;
    int recOff = 0;
    do {
        if (count <= r) {
            return 0;
        }
        int memOff = 0;
        int members = (int)*(short*)(bucket[0] + recOff + 0xe);
        if (0 < members) {
            do {
                if (*(int*)(bucket[0] + recOff + memOff + 0x14) == entity) {
                    return bucket[0] + recOff;
                }
                memOff += 0x14;
                members--;
            } while (members != 0);
        }
        r++;
        recOff += 300;
    } while (true);
}

// ----------------------------------------------------------------------------
// tracker_find_level_entity 0x80273784
// Linear search of the current level's entity descriptor list (level info +0xf4
// base, +0xf8 count, stride 0x20) for the descriptor whose first word matches
// the given handle. Returns the descriptor ptr or 0.
// ----------------------------------------------------------------------------
int* tracker_find_level_entity(int handle)
{
    int info = level_get_info_cached(DAT_80612614);
    int count = *(int*)(info + 0xf8);
    int off = 0;
    if (0 < count) {
        do {
            int* desc = (int*)(*(int*)(info + 0xf4) + off);
            if (handle == *desc) {
                return desc;
            }
            off += 0x20;
            count--;
        } while (count != 0);
    }
    return (int*)0;
}

// ----------------------------------------------------------------------------
// tracker_find_record_by_owner 0x802737f0
// Finds the tracking record whose owner (first word) matches the given handle,
// searching first the primary group array (base +0, count +0x960) then the
// secondary array (base +0x4b0, count +0x964). Returns the record base or 0.
// ----------------------------------------------------------------------------
int tracker_find_record_by_owner(int handle)
{
    int off = 0;
    int count = *(int*)(DAT_806120c8 + 0x960);
    if (0 < count) {
        do {
            if (handle == *(int*)(DAT_806120c8 + off)) {
                return DAT_806120c8 + off;
            }
            off += 300;
            count--;
        } while (count != 0);
    }
    count = *(int*)(DAT_806120c8 + 0x964);
    off = 0;
    if (0 < count) {
        do {
            if (*(int*)(DAT_806120c8 + off + 0x4b0) == handle) {
                return DAT_806120c8 + off + 0x4b0;
            }
            off += 300;
            count--;
        } while (count != 0);
    }
    return 0;
}

// ----------------------------------------------------------------------------
// tracker_find_member_slot 0x80273864
// Returns the member-array slot ptr (+0x14 base, stride 0x14) inside a record
// whose first word equals the given entity, or 0 if not present.
// ----------------------------------------------------------------------------
int tracker_find_member_slot(int entity, int rec)
{
    int members = (int)*(short*)(rec + 0xe);
    int off = 0;
    if (0 < members) {
        do {
            if (*(int*)(rec + off + 0x14) == entity) {
                return rec + off + 0x14;
            }
            off += 0x14;
            members--;
        } while (members != 0);
    }
    return 0;
}

// ----------------------------------------------------------------------------
// tracker_drop_target_slot 0x802738a0
// Removes a target entity from a record's +0xdc target-slot array (stride 8,
// count at +0x10). Locates the matching slot, compacts the tail down over it,
// clears the vacated last slot ([+0]=0, [+4]=-0x3e8 sentinel timestamp) and
// decrements the count.
// ----------------------------------------------------------------------------
void tracker_drop_target_slot(int rec, int entity)
{
    int count = (int)*(short*)(rec + 0x10);
    bool found = false;
    if (*(short*)(rec + 0x10) == 0) {
        return;
    }
    int idx = 0;
    int off = 0;
    int remaining = count;
    if (0 < count) {
        do {
            if (*(int*)(rec + off + 0xdc) == entity) {
                found = true;
                count = idx;
                break;
            }
            idx++;
            off += 8;
            remaining--;
        } while (remaining != 0);
    }
    if (!found) {
        return;
    }
    int slotOff = count << 3;
    for (; count < *(short*)(rec + 0x10) + -1; count++) {
        int slot = rec + slotOff;
        slotOff += 8;
        *(int*)(slot + 0xdc) = *(int*)(slot + 0xe4);
        *(int*)(slot + 0xe0) = *(int*)(slot + 0xe8);
    }
    *(int*)(rec + *(short*)(rec + 0x10) * 8 + 0xd4) = 0;
    *(int*)(rec + *(short*)(rec + 0x10) * 8 + 0xd8) = -0x3e8;
    *(short*)(rec + 0x10) = *(short*)(rec + 0x10) + -1;
}

// ============================================================================
// batch_749 : target selection + per-actor decision tick
//
// The selection/decision layer above the batch_748 record-management helpers.
// It chooses which visible enemy a tracked actor should engage, picks a
// destination zone from the actor's adjacent-zone list, gates group-wide
// re-planning, and runs the main per-actor decision tick that batch_748's
// tracker_bind_record drives.
//
// Frame-scratch note: several of these run under the game's spill-frame
// prologues (FUN_803826b8 / FUN_803826dc / FUN_803826e8 / __save_gpr_25) which
// return the actor entity handle (and, for the packed variants, an aux node
// handle in the high word). The matching epilogue must be called on every exit.
//
// Address range: 0x8027395c - 0x802744f8 | batch_749 | 2026-09-09
//
// Functions:
//   tracker_add_target_slot            0x8027395c  add/refresh a target slot (cap 10)
//   tracker_pick_random_visible_target 0x802739d0  random visible target across bucket
//   tracker_select_best_target         0x80273b7c  best raycast-confirmed target
//   tracker_no_incoming_projectiles    0x80273cb4  1 if no projectiles in radius
//   tracker_query_mode4_event          0x80273d00  query: mode-4 handled? (-1)
//   tracker_event_steal_targets        0x80273d2c  event 3: steal nearest visible
//   tracker_pick_destination_zone      0x80273d90  choose nav/retreat zone
//   tracker_group_replan_gate          0x802740d8  periodic group re-plan gate
//   tracker_member_under_threat        0x80274370  1 if a class member is threatened
//   tracker_actor_decision_tick        0x802744f8  main per-actor decision tick
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// tracker_add_target_slot 0x8027395c
// Adds an entity to a record's +0xdc target-slot array (stride 8: [+0]=entity,
// [+4]=timestamp), or refreshes its timestamp if already present. Capped at 10
// slots; the count lives at +0x10. Counterpart to tracker_drop_target_slot.
// ----------------------------------------------------------------------------
void tracker_add_target_slot(int rec, int entity)
{
    int count = (int)*(short*)(rec + 0x10);
    if (count == 10) {
        return;
    }
    int off = 0;
    int remaining = count;
    if (0 < count) {
        do {
            if (*(int*)(rec + off + 0xdc) == entity) {
                *(int*)(rec + off + 0xe0) = DAT_80611928;
                return;
            }
            off += 8;
            remaining--;
        } while (remaining != 0);
    }
    *(int*)(rec + count * 8 + 0xe0) = DAT_80611928;
    *(int*)(rec + *(short*)(rec + 0x10) * 8 + 0xdc) = entity;
    *(short*)(rec + 0x10) = *(short*)(rec + 0x10) + 1;
}

// ----------------------------------------------------------------------------
// tracker_pick_random_visible_target 0x802739d0
// Collects every visible (score > FLOAT_80619190) target across all records in
// the actor's group bucket into a scratch array, then picks one at random. If
// none are found it falls back to scanning the spawn-visible actor table for a
// non-allied, targetable enemy (prop_can_target). The chosen candidate is used
// by the caller; this routine's own result is dropped (frame-only side-effects).
// ----------------------------------------------------------------------------
void tracker_pick_random_visible_target(void)
{
    int actor = FUN_803826dc();
    int desc = *(int*)(actor + 0x7c);
    unsigned int visCount = 0;
    int outOff = 0;
    int bucketBase = 0;
    int chosen = 0;
    int recCount = tracker_get_group_bucket(*(int*)(desc + 0x18), &bucketBase);
    int visible[49];
    int recOff = 0;
    for (int r = 0; r < recCount; r++) {
        int slotOff = 0;
        int slots = 10;
        do {
            int tgt = *(int*)(bucketBase + recOff + slotOff + 0xdc);
            if ((tgt != 0) && (FLOAT_80619190 < *(float*)(tgt + 0x1cc))) {
                *(int*)((int)visible + outOff) = tgt;
                visCount++;
                outOff += 4;
            }
            slotOff += 8;
            slots--;
        } while (slots != 0);
        recOff += 300;
    }
    if (visCount != 0) {
        unsigned int roll = rng_counter_tick();
        chosen = visible[roll - (roll / visCount) * visCount];
    }
    int idx = 0;
    int off = 0;
    while ((idx < DAT_80612098) && (chosen == 0)) {
        int* cand = *(int**)((int)&DAT_80588610 + off);
        if ((cand != (int*)0) && ((cand[9] & 8) == 0)) {
            unsigned int flags;
            if (DAT_805015bd == '\n') {
                flags = cand[5];
            } else {
                flags = cand[7];
            }
            if (((flags & 1) == 0) && (cand[0x21] != *(int*)(desc + 0x18)) &&
                (prop_can_target(actor, *cand, 1, 0) != 0)) {
                break;
            }
        }
        idx++;
        off += 4;
    }
    FUN_80382728();
}

// ----------------------------------------------------------------------------
// tracker_select_best_target 0x80273b7c
// Picks the actor's current best target from its own record's target slots,
// restricted to those the round-robin raycast marked visible (member-slot
// +8 byte). Keeps the currently-locked target (+4) if it is still valid;
// otherwise picks one of the visible candidates at random. If none qualify it
// falls back to FUN_8027f80c (broad acquire). Result stored by the caller.
// ----------------------------------------------------------------------------
int tracker_select_best_target(void)
{
    int actor = FUN_803826e8();
    int entity = *(int*)(actor + 0x7c);
    int rec = tracker_find_record_for_entity(entity);
    int memberSlot = tracker_find_member_slot(entity, rec);
    int locked = *(int*)(entity + 4);
    int chosen = 0;
    if (rec != 0) {
        int count = (int)*(short*)(rec + 0x10);
        unsigned int candCount = 0;
        int outOff = 0;
        int visIdx = 0;
        int slotOff = 0;
        int candidates[18];
        if (0 < count) {
            do {
                int tgt = *(int*)(rec + slotOff + 0xdc);
                if (((tgt != 0) && (FLOAT_80619190 < *(float*)(tgt + 0x1cc))) &&
                    (*(char*)(memberSlot + visIdx + 8) != '\0')) {
                    if (locked == tgt) {
                        goto done;
                    }
                    *(int*)((int)candidates + outOff) = tgt;
                    candCount++;
                    outOff += 4;
                }
                visIdx++;
                slotOff += 8;
                count--;
            } while (count != 0);
        }
        if (candCount != 0) {
            unsigned int roll = rng_counter_tick();
            chosen = candidates[roll - (roll / candCount) * candCount];
        }
        if (chosen == 0) {
            FUN_8027f80c();
        }
    }
done:
    FUN_80382734();
    return chosen;
}

// ----------------------------------------------------------------------------
// tracker_no_incoming_projectiles 0x80273cb4
// Returns 1 when the sum of the four projectile counters filled by
// player_ai_projectile_count_in_radius is zero (no incoming threat), else 0.
// ----------------------------------------------------------------------------
unsigned int tracker_no_incoming_projectiles(void)
{
    unsigned int c0 = 0, c1 = 0, c2 = 0, c3 = 0;
    player_ai_projectile_count_in_radius();
    unsigned int sum = c0 + c1 + c2 + c3;
    return ((unsigned int)(-(int)sum) & ~sum) >> 0x1f;
}

// ----------------------------------------------------------------------------
// tracker_query_mode4_event 0x80273d00
// Query handler: returns -1 when event == 1 and the actor's tracked-descriptor
// mode (+0x20) is 4 (already in the "hold/guard" mode), else 0.
// ----------------------------------------------------------------------------
int tracker_query_mode4_event(int actor, int event)
{
    int result = 0;
    if ((event == 1) && (*(int*)(*(int*)(actor + 0x7c) + 0x20) == 4)) {
        result = -1;
    }
    return result;
}

// ----------------------------------------------------------------------------
// tracker_event_steal_targets 0x80273d2c
// Event handler: on event == 3, if the actor's record is active (+4 bit0),
// steals the nearest visible enemy into it (tracker_steal_nearest_visible).
// Always returns -2 for event 3 (handled), else 0.
// ----------------------------------------------------------------------------
int tracker_event_steal_targets(int actor, int event)
{
    int result = 0;
    if (event == 3) {
        int rec = tracker_find_record_for_entity(*(int*)(actor + 0x7c));
        if ((rec != 0) && ((*(unsigned int*)(rec + 4) & 1) != 0)) {
            tracker_steal_nearest_visible();
        }
        result = -2;
    }
    return result;
}

// ----------------------------------------------------------------------------
// tracker_pick_destination_zone 0x80273d90
// Chooses a destination zone for the actor from its adjacent-zone list (the
// up-to-10 shorts at nodeLo +0xc). Each candidate is scored by zone-path
// distance (FUN_8027919c) from the actor's current zone (+0x678). Occupied
// zones (a group member already in/adjacent) are rejected. flags bit1 adds a
// straight-line separation filter; flags bit0 adds a class-member-distance
// filter. If nothing scores and flags==0, a random adjacent zone is chosen.
// The result is written to actor +0x684 (or the local player's zone if none),
// and the pending sub-state (+0x9a8/+0x9ac) is cleared.
//
// The leading float params are the incoming FP-register spill slots (unused on
// the reachable path) kept only to preserve the ABI signature.
// ----------------------------------------------------------------------------
void tracker_pick_destination_zone(int actorHi, int nodeLo, unsigned int flags)
{
    (void)actorHi;
    long long packed = (long long)FUN_803826b8();
    int actor = *(int*)((int)((unsigned long long)packed >> 0x20) + 0x7c);
    short* adj = (short*)((int)packed + 0xc);
    int bestZone = -1;
    int* selfNode = soundpad_resolve_zone_node(*(int*)(actor + 0x678));
    double sentinel = (double)FLOAT_80619194;
    double band = (double)FLOAT_80619198;
    double best = sentinel;
    int i = 0;
    for (i = 0; (-1 < *adj) && (i < 10); i++) {
        int zoneId = soundpad_find_zone_by_id((int)*adj);
        int* candNode = soundpad_resolve_zone_node(zoneId);
        int candZone = soundpad_get_entity_zone_id((int)candNode);
        int selfZone = soundpad_get_entity_zone_id((int)selfNode);
        double dist = FUN_8027919c(selfZone, candZone);
        double savedBand = band;
        if (band <= dist) {
            candZone = soundpad_get_entity_zone_id((int)candNode);
            selfZone = soundpad_get_entity_zone_id((int)selfNode);
            savedBand = band;
            band = FUN_8027919c(selfZone, candZone);
        }

        // reject zones already occupied by a same-group member
        int visOff = DAT_80612610 << 2;
        int visN = DAT_80612098 - DAT_80612610;
        if (DAT_80612610 < DAT_80612098) {
            do {
                int other = *(int*)((int)&DAT_80588610 + visOff);
                if ((actor != other) && (*(int*)(other + 0x84) == *(int*)(actor + 0x84)) &&
                    ((zoneId == *(int*)(other + 0x678)) || (zoneId == *(int*)(other + 0x684)))) {
                    band = (double)FLOAT_80619194;
                    break;
                }
                visOff += 4;
                visN--;
            } while (visN != 0);
        }

        // flags bit1: straight-line separation filter
        if ((sentinel != band) && ((flags & 2) != 0)) {
            int cz = soundpad_get_entity_zone_id((int)candNode);
            int sz = soundpad_get_entity_zone_id((int)selfNode);
            if ((sz != cz) &&
                (FLOAT_8061919c <
                 ((float)selfNode[3] - (float)candNode[3]) *
                     ((float)selfNode[3] - (float)candNode[3]) +
                 ((float)selfNode[5] - (float)candNode[5]) *
                     ((float)selfNode[5] - (float)candNode[5]))) {
                band = (double)FLOAT_80619194;
            }
        }

        // flags bit0: class-member proximity filter
        if ((best != band) && ((flags & 1) != 0)) {
            int members[10];
            int mCount = worm_class_collect_members(members, 0, (int*)0, 10);
            double mBest = (double)FLOAT_80619194;
            int mOff = 0;
            for (int m = 0; m < mCount; m++) {
                int cz = soundpad_get_entity_zone_id((int)candNode);
                int* mNode = soundpad_resolve_zone_node(*(int*)((int)members + mOff));
                int mz = soundpad_get_entity_zone_id((int)mNode);
                double md = FUN_8027919c(mz, cz);
                if (md < mBest) {
                    band = md;
                }
                mOff += 4;
            }
        }

        if (band < best) {
            bestZone = zoneId;
            best = band;
        }
        adj++;
        band = savedBand;
    }

    if (bestZone == -1) {
        if (flags != 0) {
            FUN_80382704();
            return;
        }
        unsigned int roll = rng_counter_tick();
        int pick = (int)((float)((double)(unsigned int)((i - 1) ^ 0x80000000) - DOUBLE_806191b0) *
                             ((float)((double)roll - DOUBLE_806191a8) / FLOAT_806191a4) +
                         FLOAT_806191a0);
        bestZone = soundpad_find_zone_by_id((int)*(short*)((int)packed + pick * 2 + 0xc));
    }

    if (bestZone < 0) {
        *(int*)(actor + 0x684) =
            *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
    } else {
        *(int*)(actor + 0x684) = bestZone;
    }
    *(int*)(actor + 0x9ac) = 0;
    *(int*)(actor + 0x9a8) = 0;
    FUN_80382704();
}

// ----------------------------------------------------------------------------
// tracker_group_replan_gate 0x802740d8
// Periodic gate deciding whether the tracked group should aggressively re-plan
// / advance. Considers: time since the last active-class event, presence of a
// class-1 member in view, distance to the nearest member vs threshold, and a
// probabilistic term. If gated in and no member is already threatened
// (tracker_member_under_threat), scans the spawn-visible table for a candidate.
// ----------------------------------------------------------------------------
int tracker_group_replan_gate(void)
{
    int actor = __save_gpr_25();
    int desc = *(int*)(actor + 0x7c);
    int result = 0;
    bool hasClass1 = false;
    bool nearMember = false;
    if (*(int*)(desc + 0x84) != 2) {
        int activeIdx = worm_class_find_first_active_index();
        int elapsed = DAT_80611928;
        if (activeIdx != 0) {
            elapsed = DAT_80611928 -
                      *(int*)(DAT_806128ac +
                              *(int*)(DAT_806120c4 + activeIdx * 0xa8 + -0xa4) * 0x4c + 0x34);
        }
        int visIdx = 0;
        int off = 0;
        while ((visIdx < DAT_80612610) && (!hasClass1)) {
            if (*(int*)(*(int*)((int)&DAT_80588610 + off) + 0x84) == 1) {
                hasClass1 = true;
            }
            visIdx++;
            off += 4;
        }
        int nearest = worm_class_find_nearest_member();
        if (-1 < nearest) {
            int* node = soundpad_resolve_zone_node(nearest);
            float dz = *(float*)(actor + 0x6c) - (float)node[5];
            node = soundpad_resolve_zone_node(nearest);
            float dx = *(float*)(actor + 100) - (float)node[3];
            nearMember = (dx * dx + dz * dz) < FLOAT_806191b8;
        }
        float idleTime = (float)((double)(unsigned int)((elapsed / DAT_806109c0) ^ 0x80000000) -
                                 DOUBLE_806191b0);
        float replanRoll = FLOAT_806191c4 *
                               (float)((double)(unsigned int)(*(unsigned int*)(desc + 0xb04) ^
                                                              0x80000000) - DOUBLE_806191b0) +
                           FLOAT_806191c0;
        if ((((!hasClass1) && (FLOAT_806191bc < idleTime)) || (nearMember) ||
             (replanRoll < idleTime)) &&
            (tracker_member_under_threat(actor) == 0)) {
            int off2 = DAT_80612610 << 2;
            int n = DAT_80612098 - DAT_80612610;
            if (DAT_80612610 < DAT_80612098) {
                do {
                    int cand = *(int*)((int)&DAT_80588610 + off2);
                    if ((*(int*)(cand + 0x84) != 2) && ((*(unsigned int*)(cand + 0x24) & 8) == 0) &&
                        ((*(unsigned int*)(cand + 0x1c) & 0x200000) != 0)) {
                        result = cand;
                        break;
                    }
                    off2 += 4;
                    n--;
                } while (n != 0);
            }
        }
    }
    __restore_gpr_25();
    return result;
}

// ----------------------------------------------------------------------------
// tracker_member_under_threat 0x80274370
// Returns 1 if any enemy is dangerously close to the nearest class member's
// zone position: either a spawn-visible enemy inside a hard radius
// (FLOAT_806191c8), or a flagged enemy inside a larger radius (FLOAT_806191cc)
// that is also nearer the member than the actor is (relative-distance factor
// FLOAT_806191d0). Returns 0 otherwise.
// ----------------------------------------------------------------------------
int tracker_member_under_threat(int actor)
{
    int nearest = worm_class_find_nearest_member();
    if (-1 < nearest) {
        int* node = soundpad_resolve_zone_node(nearest);
        double memberX = (double)(float)node[3];
        soundpad_resolve_zone_node(nearest);
        node = soundpad_resolve_zone_node(nearest);
        float memberZ = (float)node[5];
        int off = 0;
        int n = DAT_80612098;
        if (0 < DAT_80612098) {
            do {
                int* cand = *(int**)((int)&DAT_80588610 + off);
                if ((cand[0x21] != 2) && ((cand[9] & 8U) == 0)) {
                    if (cand[2] < DAT_80612610) {
                        float dx = (float)((double)*(float*)(*cand + 100) - memberX);
                        float dz = *(float*)(*cand + 0x6c) - memberZ;
                        if (dx * dx + dz * dz < FLOAT_806191c8) {
                            return 1;
                        }
                    } else if (((cand[7] & 0x200000U) != 0)) {
                        float dx = (float)((double)*(float*)(*cand + 100) - memberX);
                        float dz = *(float*)(*cand + 0x6c) - memberZ;
                        float candDist = dx * dx + dz * dz;
                        if (candDist <= FLOAT_806191cc) {
                            float ax = (float)((double)*(float*)(actor + 100) - memberX);
                            float az = *(float*)(actor + 0x6c) - memberZ;
                            if (candDist < FLOAT_806191d0 * (ax * ax + az * az)) {
                                return 1;
                            }
                        }
                    }
                }
                off += 4;
                n--;
            } while (n != 0);
        }
    }
    return 0;
}

// ----------------------------------------------------------------------------
// tracker_actor_decision_tick 0x802744f8
// Main per-actor decision tick (driven by tracker_bind_record). If the group is
// re-planning (tracker_group_replan_gate) it queues a fallback pursue action.
// Otherwise, for a "list"-flagged descriptor (+0x1c bit0x100000), and when this
// is the local player's record (or forceLocal), it may pick a fresh destination
// zone (tracker_pick_destination_zone). Then, per the descriptor's relation
// short (nodeLo +4) and mode (+0x20), it retargets (tracker_select_best_target),
// requests a pending state (+0x9d0 = 1 or 4), or snaps the actor to the zone
// node position.
// ----------------------------------------------------------------------------
void tracker_actor_decision_tick(int entity, int levelDesc, int forceLocal)
{
    (void)levelDesc;
    unsigned long long packed = (unsigned long long)FUN_803826e8();
    int actor = (int)(packed >> 0x20);
    int nodeLo = (int)packed;
    int desc = *(int*)(actor + 0x7c);
    int replanning = tracker_group_replan_gate();
    if (replanning == 0) {
        if ((*(unsigned int*)(desc + 0x1c) & 0x100000) != 0) {
            if ((*(int*)(desc + 8) == DAT_806120f8) || (forceLocal != 0)) {
                short* adj = (short*)(nodeLo + 0xc);
                int i = 0;
                int selfId = soundpad_get_entity_id_from_zone(*(int*)(desc + 0x684));
                bool notAdjacent = true;
                while (((-1 < *adj) && (i < 10)) && (notAdjacent)) {
                    if (*adj == selfId) {
                        notAdjacent = false;
                    }
                    adj++;
                    i++;
                }
                if (notAdjacent) {
                    tracker_pick_destination_zone(actor, nodeLo, 0);
                    if (*(int*)(desc + 0x20) != 1) {
                        *(int*)(desc + 0x9d0) = 1;
                    }
                    goto done;
                }
            }
            if ((*(int*)(desc + 0x20) != 1) && (*(int*)(desc + 0x20) != 4)) {
                int* node = soundpad_resolve_zone_node(*(int*)(desc + 0x684));
                if (*(int*)(desc + 8) == DAT_806120f8) {
                    short relation = *(short*)(nodeLo + 4);
                    if (relation == 1) {
                        if ((tracker_member_under_threat(actor) != 0)) {
                            int best = tracker_select_best_target();
                            if (best != 0) {
                                *(int*)(desc + 4) = best;
                                if ((*(int*)(desc + 0xaac) == 1) && (*(int*)(desc + 0xaa8) == 0)) {
                                    unsigned int f = (DAT_805015bd == '\n')
                                                         ? *(unsigned int*)(desc + 0x14)
                                                         : *(unsigned int*)(desc + 0x1c);
                                    if ((f & 1) == 0) {
                                        goto done;
                                    }
                                }
                                *(int*)(desc + 0xaac) = 8;
                                *(int*)(desc + 0xaa8) = 0;
                                goto done;
                            }
                        }
                        if ((FLOAT_806191d8 < *(float*)(desc + 0xb30)) &&
                            (tracker_pick_destination_zone(actor, nodeLo, 1),
                             *(int*)(desc + 0x684) != 0)) {
                            if (*(int*)(desc + 0x20) != 1) {
                                *(int*)(desc + 0x9d0) = 1;
                            }
                            goto done;
                        }
                    } else if (relation < 1) {
                        if ((-1 < relation) && (FLOAT_806191dc < *(float*)(desc + 0xb30))) {
                            int best = tracker_select_best_target();
                            if (best != 0) {
                                *(int*)(desc + 4) = best;
                                if ((*(int*)(desc + 0xaac) == 1) && (*(int*)(desc + 0xaa8) == 0)) {
                                    unsigned int f = (DAT_805015bd == '\n')
                                                         ? *(unsigned int*)(desc + 0x14)
                                                         : *(unsigned int*)(desc + 0x1c);
                                    if ((f & 1) == 0) {
                                        goto done;
                                    }
                                }
                                *(int*)(desc + 0xaac) = 8;
                                *(int*)(desc + 0xaa8) = 0;
                                goto done;
                            }
                        }
                    } else if ((relation < 4) && (FLOAT_806191d4 < *(float*)(desc + 0xb30))) {
                        tracker_pick_destination_zone(actor, nodeLo, 2);
                        if (*(int*)(desc + 0x20) != 4) {
                            *(int*)(desc + 0x9d0) = 4;
                        }
                        goto done;
                    }
                }
                float dx = *(float*)(actor + 100) - (float)node[3];
                short relation = *(short*)(nodeLo + 4);
                float dz = *(float*)(actor + 0x6c) - (float)node[5];
                float dist = dx * dx + dz * dz;
                if (relation < 2) {
                    if ((-1 < relation) && (FLOAT_806191e8 < dist) &&
                        (*(int*)(desc + 0x20) != 1)) {
                        *(int*)(desc + 0x9d0) = 1;
                    }
                } else if (relation < 4) {
                    if ((dist <= FLOAT_806191e0) ||
                        (*(char*)(DAT_80611b1c + *(int*)(actor + 0x34)) != '\0') ||
                        (*(char*)(DAT_80611b1c + *(short*)((int)node + 10)) != '\0')) {
                        if ((FLOAT_806191e4 < dist) && (*(int*)(desc + 0x20) != 1)) {
                            *(int*)(desc + 0x9d0) = 1;
                        }
                    } else {
                        *(int*)(actor + 100) = node[3];
                        *(int*)(actor + 0x68) = node[4];
                        *(int*)(actor + 0x6c) = node[5];
                    }
                }
            }
        }
    } else {
        if ((*(int*)(desc + 0xaac) == 1) && (*(int*)(desc + 0xaa8) == 0)) {
            unsigned int f = (DAT_805015bd == '\n') ? *(unsigned int*)(desc + 0x14)
                                                    : *(unsigned int*)(desc + 0x1c);
            if ((f & 1) == 0) {
                goto done;
            }
        }
        *(int*)(desc + 0xaac) = 0x200000;
        *(int*)(desc + 0xaa8) = 0;
    }
done:
    FUN_80382734();
}

// ============================================================================
// batch_750 : co-op squad-bot behavior ticks + player-slot query helpers
//
// Two large per-actor behavior FSMs for the co-op "buddy" bots that the tracker
// drives, plus a set of small query/normalize helpers keyed on the co-op
// player-slot table (DAT_805889f0, two words per slot). Both FSMs branch on the
// actor descriptor mode (+0x20): 0 = regroup, 1 = idle/advance, 2 = follow-path,
// 3 = engage, 4 = switch-use. They share a common per-frame preamble (promote a
// queued mode swap via +0x9d0/+0x9d4/+0x9d8/+0x20, advance the state timer
// +0xa90) and epilogue.
//
// Address range: 0x80274964 - 0x802766b3 | batch_750 | 2026-09-09
//
// Functions:
//   tracker_bot_behavior_tick          0x80274964  squad-bot behavior FSM (variant A)
//   tracker_bot_behavior_tick_variant  0x80275928  squad-bot behavior FSM (variant B, tracker-driven)
//   tracker_player_flag_bit4           0x802764f4  player descriptor flag bit2 (0x4)
//   tracker_other_player_flag_bit4     0x80276518  same for the other co-op player
//   tracker_player_active_flag         0x80276548  player actor +0x28 bit7 clear
//   tracker_class_owner_active_flag    0x8027656c  class-owner actor +0x28 bit7 clear
//   tracker_player_self_owns_target    0x8027659c  player self-owns its target descriptor
//   tracker_normalize_player_zone      0x802765dc  re-normalize a player's zone id
//   tracker_normalize_other_player_zone 0x80276664  same for the other player
//   tracker_get_player_dest_zone       0x80276694  player descriptor destination zone (+0x3c)
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// tracker_bot_behavior_tick 0x80274964
// Full behavior FSM for a co-op squad bot. Preamble promotes any queued mode
// swap (+0x9d0) and advances the state timer (+0xa90, scaled by dt when +0x2c
// bit7 is set). Then, keyed on mode (+0x20):
//   2  follow-path combat: follow the zone path; on arrival pick a preferred
//      weapon from the match weapon list, retarget/aim, play turn/facing.
//   0  regroup: gather to the active class member (or nearest by zone-path);
//      on level 0x1d also select a weapon; aim/turn; may transition to follow.
//   1..3 engage/attack: manage the +0xaf0 objective entity (switch, target),
//      run follow/aim/turn, and switch-use handling.
//   4  switch-use.
// Finally, if the fire bit (+0x28 bit0) is set, commit the fire stance
// (+0xaac = 0x20). The leading FP locals are incoming register spill slots.
// ----------------------------------------------------------------------------
void tracker_bot_behavior_tick(void)
{
    int actor = FUN_803826c8();
    int desc = *(int*)(actor + 0x7c);
    int retarget = aiactor_retarget_visible_teammate();

    // preamble: promote queued mode swap
    if (-1 < *(int*)(desc + 0x9d0)) {
        if (*(int*)(desc + 0x9d0) != *(int*)(desc + 0x20)) {
            *(float*)(desc + 0x9d8) = FLOAT_80619190;
            *(int*)(desc + 0x9d4) = *(int*)(desc + 0x20);
            *(int*)(desc + 0x20) = *(int*)(desc + 0x9d0);
        }
        *(int*)(desc + 0x9d0) = -1;
    }
    if ((*(unsigned int*)(desc + 0x2c) & 0x80) == 0) {
        *(float*)(desc + 0xa90) = FLOAT_80619190;
    } else {
        *(float*)(desc + 0xa90) =
            FLOAT_806108ec *
                (FLOAT_80619198 /
                 (float)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_806191b0)) +
            *(float*)(desc + 0xa90);
    }
    if (*(int*)(desc + 8) == DAT_806120f8) {
        aiactor_pick_best_weapon();
    }

    int mode = *(int*)(desc + 0x20);
    if (mode == 2) {
        int arrived = aiactor_follow_zone_path();
        if (arrived != 0) {
            int wl = match_build_weapon_list();
            int best = 0;
            bool done = false;
            int off = 0;
            for (int i = 0; (i < *(int*)(wl + 4)) && (!done); i++) {
                int w = *(int*)(wl + off + 8);
                if ((*(char*)(desc + w + 0x54c) != '\0') && (w != 0x2d)) {
                    if (w < 0x2d) {
                        if (0x2a < w) { done = true; best = w; }
                    } else if (w < 0x30) { done = true; best = w; }
                }
                off += 4;
            }
            *(int*)(desc + 0x94) = best;
            int zone = *(int*)(*(int*)(desc + 0xaf0) + 0x110);
            if (zone < 0) {
                *(int*)(desc + 0x684) =
                    *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            } else {
                *(int*)(desc + 0x684) = zone;
            }
            *(int*)(desc + 0x9ac) = 0;
            *(int*)(desc + 0x9a8) = 0;
            if (*(int*)(desc + 0x20) != 1) {
                *(int*)(desc + 0x9d4) = *(int*)(desc + 0x20);
                *(int*)(desc + 0x20) = 1;
                *(float*)(desc + 0x9d8) = FLOAT_80619190;
                *(int*)(desc + 0x9d0) = *(int*)(desc + 0x20);
            }
        }
        if ((*(int*)(desc + 4) == 0) || (*(int*)(*(int*)(desc + 4) + 0x20) != 8)) {
            *(unsigned int*)(desc + 0x28) &= 0xfffdffff;
            *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
            anim_set_playback_rate((double)FLOAT_806191ec, *(int**)(actor + 4), 1);
        } else {
            if (*(int*)(desc + 8) == DAT_806120f8) {
                FUN_802b6d9c();
            }
            if ((*(int*)(desc + 4) == 0) ||
                (!aiactor_aim_alignment_test((double)*(float*)(desc + 0x9bc), actor,
                                             (float*)(*(int*)(desc + 4) + 100)))) {
                *(unsigned int*)(desc + 0x28) |= 0x40000;
            } else if (retarget != 0) {
                *(unsigned int*)(desc + 0x28) |= 2;
                *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
            }
            if ((*(unsigned int*)((int)&DAT_804d34dc +
                                  (short)(&DAT_804d1cd0)[*(int*)(*(int*)(actor + 4) + 0xcc)] * 8) &
                 0x100) != 0) {
                *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
                if (((*(int**)(actor + 4))[0x32] & 1U) != 0) {
                    anim_set_playback_rate((double)FLOAT_806191ec, *(int**)(actor + 4), 2);
                }
            }
        }
        aiactor_set_stance_flags(actor, 1);
        aiactor_play_turn_animation(actor);
        aiactor_facing_update();
    } else if (mode < 2) {
        if (mode == 0) {
            bool goFollow = false;
            int* active = worm_class_find_first_active();
            if (active[1] == 1) {
                int members0[10];
                worm_class_collect_members(members0, 0, (int*)0, 10);
                if (members0[0] < 0) {
                    *(int*)(desc + 0x684) =
                        *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                } else {
                    *(int*)(desc + 0x684) = members0[0];
                }
                *(int*)(desc + 0x9ac) = 0;
                *(int*)(desc + 0x9a8) = 0;
                *(int*)(desc + 0xaf0) = 0;
            } else {
                double best = (double)FLOAT_80619194;
                int bestZone = -1;
                int bestObj = 0;
                int members[10];
                int objs[10];
                int count = worm_class_collect_members(members, 0, objs, 10);
                int off = 0;
                for (int i = 0; i < count; i++) {
                    int* mNode = soundpad_resolve_zone_node(*(int*)((int)members + off));
                    int mz = soundpad_get_entity_zone_id((int)mNode);
                    int* selfNode = soundpad_resolve_zone_node(*(int*)(desc + 0x678));
                    int sz = soundpad_get_entity_zone_id((int)selfNode);
                    double d = FUN_8027919c(sz, mz);
                    if (d < best) {
                        bestObj = *(int*)((int)objs + off);
                        bestZone = *(int*)((int)members + off);
                        best = d;
                    }
                    off += 4;
                }
                if (bestZone < 0) {
                    if ((*(int*)(desc + 0xaac) == 1) && (*(int*)(desc + 0xaa8) == 0)) {
                        unsigned int f = (DAT_805015bd == '\n') ? *(unsigned int*)(desc + 0x14)
                                                                : *(unsigned int*)(desc + 0x1c);
                        if ((f & 1) == 0) {
                            goto fire_commit;
                        }
                    }
                    *(int*)(desc + 0xaac) = 0x10;
                    *(int*)(desc + 0xaa8) = 0;
                    goto fire_commit;
                }
                if (bestObj != 0) {
                    *(int*)(desc + 0xaf0) = bestObj;
                }
                *(int*)(desc + 0x684) = bestZone;
                *(int*)(desc + 0x9ac) = 0;
                *(int*)(desc + 0x9a8) = 0;
            }

            if ((DAT_80612614 == 0x1d) && (*active == 0)) {
                int wl = match_build_weapon_list();
                int best = 0;
                bool searching = true;
                int off = 0;
                for (int i = 0; (i < *(int*)(wl + 4)) && (searching); i++) {
                    int w = *(int*)(wl + off + 8);
                    if ((*(char*)(desc + w + 0x54c) != '\0') && (w != 0x2d)) {
                        if (w < 0x2d) {
                            if (0x2a < w) { searching = false; best = w; }
                        } else if (w < 0x30) { searching = false; best = w; }
                    }
                    off += 4;
                }
                if (!searching) {
                    *(int*)(desc + 0x94) = best;
                }
            }
            if (*(int*)(desc + 8) == DAT_806120f8) {
                FUN_802b6d9c();
            }
            if ((*(int*)(desc + 4) == 0) ||
                (!aiactor_aim_alignment_test((double)*(float*)(desc + 0x9bc), actor,
                                             (float*)(*(int*)(desc + 4) + 100)))) {
                *(unsigned int*)(desc + 0x28) |= 0x40000;
            } else if (retarget != 0) {
                *(unsigned int*)(desc + 0x28) |= 2;
                *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
            }
            if ((*(unsigned int*)((int)&DAT_804d34dc +
                                  (short)(&DAT_804d1cd0)[*(int*)(*(int*)(actor + 4) + 0xcc)] * 8) &
                 0x100) != 0) {
                *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
                if (((*(int**)(actor + 4))[0x32] & 1U) != 0) {
                    anim_set_playback_rate((double)FLOAT_806191ec, *(int**)(actor + 4), 2);
                }
            }
            aiactor_set_stance_flags(actor, 1);
            aiactor_play_turn_animation(actor);
            aiactor_facing_update();
            if (goFollow) {
                int z = soundpad_find_zone_by_id(0x3f0);
                if (z < 0) {
                    *(int*)(desc + 0x684) =
                        *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                } else {
                    *(int*)(desc + 0x684) = soundpad_find_zone_by_id(0x3f0);
                }
                *(int*)(desc + 0x9ac) = 0;
                *(int*)(desc + 0x9a8) = 0;
                if (*(int*)(desc + 0x20) != 2) {
                    *(int*)(desc + 0x9d4) = *(int*)(desc + 0x20);
                    *(int*)(desc + 0x20) = 2;
                    *(float*)(desc + 0x9d8) = FLOAT_80619190;
                    *(int*)(desc + 0x9d0) = *(int*)(desc + 0x20);
                }
            } else if (*(int*)(desc + 0x20) != 1) {
                *(int*)(desc + 0x9d4) = *(int*)(desc + 0x20);
                *(int*)(desc + 0x20) = 1;
                *(float*)(desc + 0x9d8) = FLOAT_80619190;
                *(int*)(desc + 0x9d0) = *(int*)(desc + 0x20);
            }
        } else if (-1 < mode) {
            int obj = *(int*)(desc + 0xaf0);
            if ((obj != 0) && (*(int*)(obj + 0x20) != 0x20000)) {
                if (*(int*)(desc + 4) == obj) {
                    if ((retarget != 0) && (mode != 3)) {
                        *(int*)(desc + 0x9d4) = mode;
                        *(int*)(desc + 0x20) = 3;
                        *(float*)(desc + 0x9d8) = FLOAT_80619190;
                        *(int*)(desc + 0x9d0) = *(int*)(desc + 0x20);
                    }
                } else if ((*(int*)(desc + 8) == DAT_806120f8) &&
                           (((*(float*)(actor + 100) - *(float*)(obj + 100)) *
                                 (*(float*)(actor + 100) - *(float*)(obj + 100)) +
                             (*(float*)(actor + 0x6c) - *(float*)(obj + 0x6c)) *
                                 (*(float*)(actor + 0x6c) - *(float*)(obj + 0x6c))) <
                            FLOAT_806191f0)) {
                    *(int*)(desc + 4) = obj;
                }
            }
            int arrived = aiactor_follow_zone_path();
            if (arrived != 0) {
                int obj2 = *(int*)(desc + 0xaf0);
                if (obj2 == 0) {
                    if ((*(int*)(desc + 0xaac) == 1) && (*(int*)(desc + 0xaa8) == 0)) {
                        unsigned int f = (DAT_805015bd == '\n') ? *(unsigned int*)(desc + 0x14)
                                                                : *(unsigned int*)(desc + 0x1c);
                        if ((f & 1) == 0) {
                            goto after_engage;
                        }
                    }
                    *(int*)(desc + 0xaac) = 0x100000;
                    *(int*)(desc + 0xaa8) = 0;
                } else if (*(int*)(obj2 + 0x20) == 0x20000) {
                    int sw = *(int*)(obj2 + 0x7c);
                    aiactor_set_stance_flags(actor, 4);
                    if ((*(int*)(sw + 0xc) == 4) ||
                        ((*(int*)(sw + 0x18) != 0) && ((*(unsigned int*)(sw + 0x10) & 0x20000) != 0)) ||
                        ((*(int*)(sw + 0x18) == 0) && ((*(unsigned int*)(sw + 0x10) & 0x10000) != 0))) {
                        *(float*)(desc + 0x9e0) = FLOAT_80619190;
                        if (*(int*)(desc + 0x20) != 4) {
                            *(int*)(desc + 0x9d4) = *(int*)(desc + 0x20);
                            *(int*)(desc + 0x20) = 4;
                            *(float*)(desc + 0x9d8) = FLOAT_80619190;
                            *(int*)(desc + 0x9d0) = *(int*)(desc + 0x20);
                        }
                    } else {
                        switch_use_handler();
                        if ((*(int*)(desc + 0xaac) == 1) && (*(int*)(desc + 0xaa8) == 0)) {
                            unsigned int f = (DAT_805015bd == '\n') ? *(unsigned int*)(desc + 0x14)
                                                                    : *(unsigned int*)(desc + 0x1c);
                            if ((f & 1) == 0) {
                                goto after_engage;
                            }
                        }
                        *(int*)(desc + 0xaac) = 4;
                        *(int*)(desc + 0xaa8) = 0;
                    }
                } else {
                    *(int*)(desc + 4) = obj2;
                    if (*(int*)(desc + 0x20) != 3) {
                        *(int*)(desc + 0x9d4) = *(int*)(desc + 0x20);
                        *(int*)(desc + 0x20) = 3;
                        *(float*)(desc + 0x9d8) = FLOAT_80619190;
                        *(int*)(desc + 0x9d0) = *(int*)(desc + 0x20);
                    }
                }
            }
        after_engage:
            if ((*(int*)(desc + 4) != *(int*)(desc + 0xaf0)) &&
                (*(int*)(desc + 8) == DAT_806120f8)) {
                FUN_802b6d9c();
            }
            if ((*(int*)(desc + 4) == 0) || (*(int*)(*(int*)(desc + 4) + 0x20) != 8)) {
                *(unsigned int*)(desc + 0x28) &= 0xfffdffff;
                *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
                anim_set_playback_rate((double)FLOAT_806191ec, *(int**)(actor + 4), 1);
            } else {
                if (*(int*)(desc + 8) == DAT_806120f8) {
                    FUN_802b6d9c();
                }
                if ((*(int*)(desc + 4) == 0) ||
                    (!aiactor_aim_alignment_test((double)*(float*)(desc + 0x9bc), actor,
                                                 (float*)(*(int*)(desc + 4) + 100)))) {
                    *(unsigned int*)(desc + 0x28) |= 0x40000;
                } else if (retarget != 0) {
                    *(unsigned int*)(desc + 0x28) |= 2;
                    *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
                }
                if ((*(unsigned int*)((int)&DAT_804d34dc +
                                      (short)(&DAT_804d1cd0)[*(int*)(*(int*)(actor + 4) + 0xcc)] *
                                          8) &
                     0x100) != 0) {
                    *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
                    if (((*(int**)(actor + 4))[0x32] & 1U) != 0) {
                        anim_set_playback_rate((double)FLOAT_806191ec, *(int**)(actor + 4), 2);
                    }
                }
            }
            aiactor_set_stance_flags(actor, 1);
            aiactor_play_turn_animation(actor);
            aiactor_facing_update();
        }
    } else if (mode == 4) {
        int sw = *(int*)(*(int*)(desc + 0xaf0) + 0x7c);
        switch_hold_use_handler();
        if (*(int*)(sw + 0x18) != 0) {
            if ((*(int*)(desc + 0xaac) == 1) && (*(int*)(desc + 0xaa8) == 0)) {
                unsigned int f = (DAT_805015bd == '\n') ? *(unsigned int*)(desc + 0x14)
                                                        : *(unsigned int*)(desc + 0x1c);
                if ((f & 1) == 0) {
                    goto switch_aim;
                }
            }
            *(int*)(desc + 0xaac) = 0x10;
            *(int*)(desc + 0xaa8) = 0;
        }
    switch_aim:
        if (*(int*)(desc + 8) == DAT_806120f8) {
            FUN_802b6d9c();
        }
        if ((*(int*)(desc + 4) == 0) ||
            (!aiactor_aim_alignment_test((double)*(float*)(desc + 0x9bc), actor,
                                         (float*)(*(int*)(desc + 4) + 100)))) {
            *(unsigned int*)(desc + 0x28) |= 0x40000;
        } else if (retarget != 0) {
            *(unsigned int*)(desc + 0x28) |= 2;
            *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
        }
        FUN_80280c8c(actor);
        if ((*(unsigned int*)((int)&DAT_804d34dc +
                              (short)(&DAT_804d1cd0)[*(int*)(*(int*)(actor + 4) + 0xcc)] * 8) &
             0x100) != 0) {
            *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
            if (((*(int**)(actor + 4))[0x32] & 1U) != 0) {
                anim_set_playback_rate((double)FLOAT_806191ec, *(int**)(actor + 4), 2);
            }
        }
        aiactor_turn_toward_entity(actor, *(int*)(desc + 0xaf0), 0);
    } else if (mode < 4) {
        int tgt = *(int*)(desc + 4);
        if ((tgt == 0) || (*(float*)(tgt + 0x1cc) <= FLOAT_80619190)) {
            *(int*)(desc + 4) = FUN_8027f80c();
            if ((*(int*)(desc + 0xaac) == 1) && (*(int*)(desc + 0xaa8) == 0)) {
                unsigned int f = (DAT_805015bd == '\n') ? *(unsigned int*)(desc + 0x14)
                                                        : *(unsigned int*)(desc + 0x1c);
                if ((f & 1) == 0) {
                    goto fire_commit;
                }
            }
            *(int*)(desc + 0xaac) = 4;
            *(int*)(desc + 0xaa8) = 0;
            goto fire_commit;
        }
        if ((retarget == 0) ||
            ((DAT_80612614 == 0x1d) &&
             (FLOAT_806191f4 < (*(float*)(actor + 100) - *(float*)(tgt + 100)) *
                                       (*(float*)(actor + 100) - *(float*)(tgt + 100)) +
                                   (*(float*)(actor + 0x6c) - *(float*)(tgt + 0x6c)) *
                                       (*(float*)(actor + 0x6c) - *(float*)(tgt + 0x6c))))) {
            aiactor_follow_zone_path();
            aiactor_set_stance_flags(actor, 1);
            aiactor_play_turn_animation(actor);
            aiactor_facing_update();
        } else {
            FUN_80280c8c(actor);
            aiactor_turn_toward_leader();
            if (((*(float*)(desc + 0xae0)) < 0 ? -(*(float*)(desc + 0xae0)) : (*(float*)(desc + 0xae0))) <
                FLOAT_806191dc) {
                *(unsigned int*)(desc + 0x28) |= 2;
            }
        }
        if (tracker_no_incoming_projectiles() != 0) {
            *(int*)(desc + 4) = FUN_8027f80c();
            if ((*(int*)(desc + 0xaac) == 1) && (*(int*)(desc + 0xaa8) == 0)) {
                unsigned int f = (DAT_805015bd == '\n') ? *(unsigned int*)(desc + 0x14)
                                                        : *(unsigned int*)(desc + 0x1c);
                if ((f & 1) == 0) {
                    goto after_attack;
                }
            }
            *(int*)(desc + 0xaac) = 4;
            *(int*)(desc + 0xaa8) = 0;
        }
    after_attack:
        if ((*(unsigned int*)((int)&DAT_804d34dc +
                              (short)(&DAT_804d1cd0)[*(int*)(*(int*)(actor + 4) + 0xcc)] * 8) &
             0x100) != 0) {
            *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
            if (((*(int**)(actor + 4))[0x32] & 1U) != 0) {
                anim_set_playback_rate((double)FLOAT_806191ec, *(int**)(actor + 4), 2);
            }
        }
    }

fire_commit:
    if ((*(unsigned int*)(desc + 0x28) & 1) != 0) {
        int fA, fB;
        if (DAT_805015bd == '\n') {
            fA = *(int*)(desc + 0x10);
            fB = *(int*)(desc + 0x14);
        } else {
            fA = *(int*)(desc + 0x18);
            fB = *(int*)(desc + 0x1c);
        }
        *(int*)(desc + 0xabc) = fB;
        *(int*)(desc + 0xab8) = fA;
        if ((*(int*)(desc + 0xaac) == 1) && (*(int*)(desc + 0xaa8) == 0)) {
            unsigned int f = (DAT_805015bd == '\n') ? *(unsigned int*)(desc + 0x14)
                                                    : *(unsigned int*)(desc + 0x1c);
            if ((f & 1) == 0) {
                goto tick_done;
            }
        }
        *(int*)(desc + 0xaac) = 0x20;
        *(int*)(desc + 0xaa8) = 0;
    }
tick_done:
    FUN_80382714();
}

// ----------------------------------------------------------------------------
// tracker_bot_behavior_tick_variant 0x80275928
// Second squad-bot behavior FSM, driven directly by the tracker (each state
// ends with tracker_actor_decision_tick / tracker_group_replan_gate /
// tracker_pick_destination_zone). Same preamble/epilogue and mode set as
// tracker_bot_behavior_tick, but the movement/animation choices are simpler
// (plays the shared regroup/idle clip 0x1c3, turns toward leader or a fixed
// facing, and defers targeting to the tracker decision tick).
// ----------------------------------------------------------------------------
void tracker_bot_behavior_tick_variant(void)
{
    int actor = __save_gpr_28();
    int desc = *(int*)(actor + 0x7c);
    int retarget = aiactor_retarget_visible_teammate();

    if (-1 < *(int*)(desc + 0x9d0)) {
        if (*(int*)(desc + 0x9d0) != *(int*)(desc + 0x20)) {
            *(float*)(desc + 0x9d8) = FLOAT_80619190;
            *(int*)(desc + 0x9d4) = *(int*)(desc + 0x20);
            *(int*)(desc + 0x20) = *(int*)(desc + 0x9d0);
        }
        *(int*)(desc + 0x9d0) = -1;
    }
    if ((*(unsigned int*)(desc + 0x2c) & 0x80) == 0) {
        *(float*)(desc + 0xa90) = FLOAT_80619190;
    } else {
        *(float*)(desc + 0xa90) =
            FLOAT_806108ec *
                (FLOAT_80619198 /
                 (float)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_806191b0)) +
            *(float*)(desc + 0xa90);
    }
    int rec = tracker_find_record_for_entity(desc);
    rec = *(int*)(rec + 8);
    if (*(int*)(desc + 8) == DAT_806120f8) {
        aiactor_pick_best_weapon();
    }

    int mode = *(int*)(desc + 0x20);
    if (mode == 2) {
        if (anim_is_clip_active(actor, 0x1c3) == 0) {
            anim_play_scaled((double)FLOAT_806191f8, (double)FLOAT_80619198);
        }
        if ((*(int*)(desc + 4) == 0) || (*(float*)(*(int*)(desc + 4) + 0x1cc) <= FLOAT_80619190) ||
            (*(int*)(desc + 8) == DAT_806120f8)) {
            FUN_802b6d9c();
        } else if ((retarget != 0) && (*(short*)(rec + 4) == 1)) {
            if ((*(int*)(desc + 0xaac) == 1) && (*(int*)(desc + 0xaa8) == 0)) {
                unsigned int f = (DAT_805015bd == '\n') ? *(unsigned int*)(desc + 0x14)
                                                        : *(unsigned int*)(desc + 0x1c);
                if ((f & 1) == 0) {
                    goto turn_a;
                }
            }
            *(int*)(desc + 0xaac) = 4;
            *(int*)(desc + 0xaa8) = 0;
        }
    turn_a:
        if (retarget == 0) {
            if (*(int*)(desc + 0xacc) == 0) {
                aiactor_turn_toward_angle((double)*(float*)(*(int*)(desc + 0x67c) + 0x18), actor, 0);
            } else {
                aiactor_turn_toward_angle((double)*(float*)(*(int*)(desc + 0xacc) + 0x18), actor, 0);
            }
        } else {
            aiactor_turn_toward_leader();
            *(float*)(desc + 0xb30) = FLOAT_80619190;
        }
        if (*(int*)(desc + 8) == DAT_806120f8) {
            FUN_802b6d9c();
        }
        if ((*(int*)(desc + 4) == 0) ||
            (!aiactor_aim_alignment_test((double)*(float*)(desc + 0x9bc), actor,
                                         (float*)(*(int*)(desc + 4) + 100)))) {
            *(unsigned int*)(desc + 0x28) |= 0x40000;
        } else if (retarget != 0) {
            *(unsigned int*)(desc + 0x28) |= 2;
            *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
        }
        if ((*(unsigned int*)((int)&DAT_804d34dc +
                              (short)(&DAT_804d1cd0)[*(int*)(*(int*)(actor + 4) + 0xcc)] * 8) &
             0x100) != 0) {
            *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
            if (((*(int**)(actor + 4))[0x32] & 1U) != 0) {
                anim_set_playback_rate((double)FLOAT_806191ec, *(int**)(actor + 4), 2);
            }
        }
        if (*(int*)(desc + 8) == DAT_806120f8) {
            aiactor_leader_zone_update(actor);
        }
        tracker_actor_decision_tick(actor, rec, 0);
    } else if (mode < 2) {
        if (mode == 0) {
            int gate = tracker_group_replan_gate();
            if (gate == 0) {
                tracker_pick_destination_zone(actor, rec, 0);
                if (*(int*)(desc + 0x20) != 1) {
                    *(int*)(desc + 0x9d4) = *(int*)(desc + 0x20);
                    *(int*)(desc + 0x20) = 1;
                    *(float*)(desc + 0x9d8) = FLOAT_80619190;
                    *(int*)(desc + 0x9d0) = *(int*)(desc + 0x20);
                }
            } else {
                if ((*(int*)(desc + 0xaac) == 1) && (*(int*)(desc + 0xaa8) == 0)) {
                    unsigned int f = (DAT_805015bd == '\n') ? *(unsigned int*)(desc + 0x14)
                                                            : *(unsigned int*)(desc + 0x1c);
                    if ((f & 1) == 0) {
                        goto move_a;
                    }
                }
                *(int*)(desc + 0xaac) = 0x200000;
                *(int*)(desc + 0xaa8) = 0;
            }
        move_a:
            aiactor_set_stance_flags(actor, 1);
            aiactor_play_turn_animation(actor);
            aiactor_facing_update();
        } else {
            if (mode < 0) {
                goto tick_done2;
            }
            if (*(int*)(desc + 8) == DAT_806120f8) {
                FUN_802b6d9c();
            }
            if ((*(int*)(desc + 4) == 0) ||
                (!aiactor_aim_alignment_test((double)*(float*)(desc + 0x9bc), actor,
                                             (float*)(*(int*)(desc + 4) + 100)))) {
                *(unsigned int*)(desc + 0x28) |= 0x40000;
            } else if (retarget != 0) {
                *(unsigned int*)(desc + 0x28) |= 2;
                *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
            }
            if ((*(unsigned int*)((int)&DAT_804d34dc +
                                  (short)(&DAT_804d1cd0)[*(int*)(*(int*)(actor + 4) + 0xcc)] * 8) &
                 0x100) != 0) {
                *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
                if (((*(int**)(actor + 4))[0x32] & 1U) != 0) {
                    anim_set_playback_rate((double)FLOAT_806191ec, *(int**)(actor + 4), 2);
                }
            }
            aiactor_set_stance_flags(actor, 1);
            aiactor_play_turn_animation(actor);
            aiactor_facing_update();
            if ((*(int*)(desc + 4) == 0) ||
                (*(float*)(*(int*)(desc + 4) + 0x1cc) <= FLOAT_80619190) ||
                (*(int*)(desc + 8) == DAT_806120f8)) {
                FUN_802b6d9c();
            } else if ((retarget != 0) && (*(short*)(rec + 4) < 2) && (-1 < *(short*)(rec + 4))) {
                if ((*(int*)(desc + 0xaac) == 1) && (*(int*)(desc + 0xaa8) == 0)) {
                    unsigned int f = (DAT_805015bd == '\n') ? *(unsigned int*)(desc + 0x14)
                                                            : *(unsigned int*)(desc + 0x1c);
                    if ((f & 1) == 0) {
                        goto tick_done2;
                    }
                }
                *(int*)(desc + 0xaac) = 4;
                *(int*)(desc + 0xaa8) = 0;
                goto tick_done2;
            }
            int arrived = aiactor_follow_zone_path_alt();
            if (arrived != 0) {
                *(float*)(desc + 0xb30) = FLOAT_80619190;
                aiactor_set_stance_flags(actor, 4);
                short rel = *(short*)(rec + 4);
                bool toFollow = false;
                if (rel != 2) {
                    if (rel < 2) {
                        if (0 < rel) toFollow = true;
                    } else if (rel < 4) toFollow = true;
                }
                if (toFollow) {
                    if (*(int*)(desc + 0x20) != 2) {
                        *(int*)(desc + 0x9d4) = *(int*)(desc + 0x20);
                        *(int*)(desc + 0x20) = 2;
                        *(float*)(desc + 0x9d8) = FLOAT_80619190;
                        *(int*)(desc + 0x9d0) = *(int*)(desc + 0x20);
                    }
                } else if (*(int*)(desc + 0x20) != 3) {
                    *(int*)(desc + 0x9d4) = *(int*)(desc + 0x20);
                    *(int*)(desc + 0x20) = 3;
                    *(float*)(desc + 0x9d8) = FLOAT_80619190;
                    *(int*)(desc + 0x9d0) = *(int*)(desc + 0x20);
                }
            }
            if (*(int*)(desc + 8) == DAT_806120f8) {
                tracker_actor_decision_tick(actor, rec, 0);
            }
        }
    } else if (mode == 4) {
        if (*(int*)(desc + 8) == DAT_806120f8) {
            FUN_802b6d9c();
        }
        if ((*(int*)(desc + 4) == 0) ||
            (!aiactor_aim_alignment_test((double)*(float*)(desc + 0x9bc), actor,
                                         (float*)(*(int*)(desc + 4) + 100)))) {
            *(unsigned int*)(desc + 0x28) |= 0x40000;
        } else if (retarget != 0) {
            *(unsigned int*)(desc + 0x28) |= 2;
            *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
        }
        if ((*(unsigned int*)((int)&DAT_804d34dc +
                              (short)(&DAT_804d1cd0)[*(int*)(*(int*)(actor + 4) + 0xcc)] * 8) &
             0x100) != 0) {
            *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
            if (((*(int**)(actor + 4))[0x32] & 1U) != 0) {
                anim_set_playback_rate((double)FLOAT_806191ec, *(int**)(actor + 4), 2);
            }
        }
        aiactor_set_stance_flags(actor, 1);
        aiactor_play_turn_animation(actor);
        aiactor_facing_update();
        if ((*(int*)(desc + 4) == 0) || (*(float*)(*(int*)(desc + 4) + 0x1cc) <= FLOAT_80619190) ||
            (*(int*)(desc + 8) == DAT_806120f8)) {
            FUN_802b6d9c();
        }
        if ((retarget != 0) || (aiactor_follow_zone_path_alt() != 0)) {
            *(float*)(desc + 0xb30) = FLOAT_80619190;
            aiactor_set_stance_flags(actor, 4);
            if (*(short*)(rec + 4) == 2) {
                if (*(int*)(desc + 0x20) != 3) {
                    *(int*)(desc + 0x9d4) = *(int*)(desc + 0x20);
                    *(int*)(desc + 0x20) = 3;
                    *(float*)(desc + 0x9d8) = FLOAT_80619190;
                    *(int*)(desc + 0x9d0) = *(int*)(desc + 0x20);
                }
            } else if (*(int*)(desc + 0x20) != 2) {
                *(int*)(desc + 0x9d4) = *(int*)(desc + 0x20);
                *(int*)(desc + 0x20) = 2;
                *(float*)(desc + 0x9d8) = FLOAT_80619190;
                *(int*)(desc + 0x9d0) = *(int*)(desc + 0x20);
            }
        }
        if (*(int*)(desc + 8) == DAT_806120f8) {
            tracker_actor_decision_tick(actor, rec, 0);
        }
    } else if (mode < 4) {
        if ((*(int*)(desc + 4) == 0) || (*(float*)(*(int*)(desc + 4) + 0x1cc) <= FLOAT_80619190) ||
            (*(int*)(desc + 8) == DAT_806120f8)) {
            FUN_802b6d9c();
        } else if ((retarget != 0) && (*(short*)(rec + 4) < 2) && (-1 < *(short*)(rec + 4))) {
            if ((*(int*)(desc + 0xaac) == 1) && (*(int*)(desc + 0xaa8) == 0)) {
                unsigned int f = (DAT_805015bd == '\n') ? *(unsigned int*)(desc + 0x14)
                                                        : *(unsigned int*)(desc + 0x1c);
                if ((f & 1) == 0) {
                    goto turn_b;
                }
            }
            *(int*)(desc + 0xaac) = 4;
            *(int*)(desc + 0xaa8) = 0;
        }
    turn_b:
        if (retarget == 0) {
            if (*(int*)(desc + 0xacc) == 0) {
                aiactor_turn_toward_angle((double)*(float*)(*(int*)(desc + 0x67c) + 0x18), actor, 0);
            } else {
                aiactor_turn_toward_angle((double)*(float*)(*(int*)(desc + 0xacc) + 0x18), actor, 0);
            }
        } else {
            aiactor_turn_toward_leader();
            *(float*)(desc + 0xb30) = FLOAT_80619190;
        }
        if (*(int*)(desc + 8) == DAT_806120f8) {
            FUN_802b6d9c();
        }
        if ((*(int*)(desc + 4) == 0) ||
            (!aiactor_aim_alignment_test((double)*(float*)(desc + 0x9bc), actor,
                                         (float*)(*(int*)(desc + 4) + 100)))) {
            *(unsigned int*)(desc + 0x28) |= 0x40000;
        } else if (retarget != 0) {
            *(unsigned int*)(desc + 0x28) |= 2;
            *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
        }
        if ((*(unsigned int*)((int)&DAT_804d34dc +
                              (short)(&DAT_804d1cd0)[*(int*)(*(int*)(actor + 4) + 0xcc)] * 8) &
             0x100) != 0) {
            *(unsigned int*)(desc + 0x28) &= 0xfffbffff;
            if (((*(int**)(actor + 4))[0x32] & 1U) != 0) {
                anim_set_playback_rate((double)FLOAT_806191ec, *(int**)(actor + 4), 2);
            }
        }
        FUN_80280c8c(actor);
        if (*(int*)(desc + 8) == DAT_806120f8) {
            aiactor_leader_zone_update(actor);
        }
        tracker_actor_decision_tick(actor, rec, 0);
    }

tick_done2:
    if ((*(unsigned int*)(desc + 0x28) & 1) != 0) {
        int fA, fB;
        if (DAT_805015bd == '\n') {
            fA = *(int*)(desc + 0x10);
            fB = *(int*)(desc + 0x14);
        } else {
            fA = *(int*)(desc + 0x18);
            fB = *(int*)(desc + 0x1c);
        }
        *(int*)(desc + 0xabc) = fB;
        *(int*)(desc + 0xab8) = fA;
        if ((*(int*)(desc + 0xaac) == 1) && (*(int*)(desc + 0xaa8) == 0)) {
            unsigned int f = (DAT_805015bd == '\n') ? *(unsigned int*)(desc + 0x14)
                                                    : *(unsigned int*)(desc + 0x1c);
            if ((f & 1) == 0) {
                goto done2;
            }
        }
        *(int*)(desc + 0xaac) = 0x20;
        *(int*)(desc + 0xaa8) = 0;
    }
done2:
    __restore_gpr_28();
}

// ----------------------------------------------------------------------------
// tracker_player_flag_bit4 0x802764f4
// Returns bit 0x4 of the tracked-descriptor flag word (+0xa0) for the co-op
// player at slot index playerIdx.
// ----------------------------------------------------------------------------
unsigned int tracker_player_flag_bit4(int playerIdx)
{
    return *(unsigned int*)(*(int*)((&DAT_805889f0)[playerIdx * 2] + 0x7c) + 0xa0) & 4;
}

// ----------------------------------------------------------------------------
// tracker_other_player_flag_bit4 0x80276518
// Queries tracker_player_flag_bit4 for the OTHER co-op player (slot 1 if self is
// 1, else slot 2). (The upstream code discards the return.)
// ----------------------------------------------------------------------------
unsigned int tracker_other_player_flag_bit4(int selfIdx)
{
    int other = 1;
    if (selfIdx == 1) {
        other = 2;
    }
    return tracker_player_flag_bit4(other);
}

// ----------------------------------------------------------------------------
// tracker_player_active_flag 0x80276548
// Returns 1 when the player's actor +0x28 bit7 is CLEAR (i.e. not in the paused
// / suppressed state), else 0.
// ----------------------------------------------------------------------------
unsigned int tracker_player_active_flag(int playerIdx)
{
    return (*(unsigned int*)((&DAT_805889f0)[playerIdx * 2] + 0x28) >> 7 & 1) ^ 1;
}

// ----------------------------------------------------------------------------
// tracker_class_owner_active_flag 0x8027656c
// Same test as tracker_player_active_flag, but resolves the player via the
// actor's class-owner index (+0x7c descriptor, +0x84 class index).
// ----------------------------------------------------------------------------
unsigned int tracker_class_owner_active_flag(int actor)
{
    unsigned int clz = countLeadingZeros(
        *(unsigned int*)((&DAT_805889f0)[*(int*)(*(int*)(actor + 0x7c) + 0x84) * 2] + 0x28) & 0x80);
    return clz >> 5;
}

// ----------------------------------------------------------------------------
// tracker_player_self_owns_target 0x8027659c
// Returns 1 when the player's tracked descriptor self-owns (+0x1c == slot index)
// and its flag word (+0xa0) bit0 is set, else 0.
// ----------------------------------------------------------------------------
int tracker_player_self_owns_target(int playerIdx)
{
    if ((*(int*)(*(int*)((&DAT_805889f0)[playerIdx * 2] + 0x7c) + 0x1c) == playerIdx) &&
        ((*(unsigned int*)(*(int*)((&DAT_805889f0)[playerIdx * 2] + 0x7c) + 0xa0) & 1) != 0)) {
        return 1;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// tracker_normalize_player_zone 0x802765dc
// If the player's current zone id (+0x38) falls in one of the special ranges
// (0x1f40..0x209d or 0x209e..0x20b1), resolves the zone node and re-normalizes
// the id via soundpad_find_zone_by_id on the node's canonical id (+2 short).
// ----------------------------------------------------------------------------
int tracker_normalize_player_zone(int playerIdx)
{
    int zone = *(int*)(*(int*)((&DAT_805889f0)[playerIdx * 2] + 0x7c) + 0x38);
    bool inRange = (7999 < zone) && (zone < 0x209e);
    if (!inRange) {
        inRange = (0x209d < zone) && (zone < 0x20b2);
        if (!inRange) {
            return zone;
        }
    }
    int* node = soundpad_resolve_zone_node(zone);
    return soundpad_find_zone_by_id((int)*(short*)((int)node + 8));
}

// ----------------------------------------------------------------------------
// tracker_normalize_other_player_zone 0x80276664
// Runs tracker_normalize_player_zone for the OTHER co-op player.
// ----------------------------------------------------------------------------
int tracker_normalize_other_player_zone(int selfIdx)
{
    int other = 1;
    if (selfIdx == 1) {
        other = 2;
    }
    return tracker_normalize_player_zone(other);
}

// ----------------------------------------------------------------------------
// tracker_get_player_dest_zone 0x80276694
// Returns the player's tracked-descriptor destination-zone field (+0x3c).
// ----------------------------------------------------------------------------
int tracker_get_player_dest_zone(int playerIdx)
{
    return *(int*)(*(int*)((&DAT_805889f0)[playerIdx * 2] + 0x7c) + 0x3c);
}

// ============================================================================
// batch_751 : co-op player-slot getters + per-frame tracker driver
//
// Small accessors over the co-op player-slot table (DAT_805889f0, two words per
// slot) plus the per-frame tracker driver that ticks both co-op players in the
// story game-type. Also builds the per-team spawn-count stat block DAT_806120cc.
//
// Per-team stat block DAT_806120cc (stride 0x10 bytes = 4 ints per team, and the
// negative offsets in the decompile are just base-relative: [team*4 - 3] etc.):
//   [-4] extra-range actor count      (players/bots outside the spawned window)
//   [-3] spawned-range actor count
//   [-2] last cached active-flag state
//   [-1] timestamp of the last state change
//
// Address range: 0x802766b4 - 0x80276a13 | batch_751 | 2026-09-09
//
// Functions:
//   tracker_other_player_dest_zone     0x802766b4  dest zone of the other player
//   tracker_get_player_actor           0x802766e4  actor ptr for a player slot
//   tracker_other_player_actor         0x802766fc  actor ptr of the other player
//   tracker_find_owned_actor           0x8027672c  first owned spawn-vis actor
//   tracker_other_find_owned_actor     0x802767ac  same for the other player's team
//   tracker_count_actors_per_team      0x802767dc  build per-team spawn counts
//   tracker_coop_players_tick          0x8027688c  per-frame both-players driver
//   tracker_player_state_change_tick   0x80276984  detect+dispatch state change
//   tracker_record_player_state        0x802769e8  store new state + timestamp
//   tracker_stub_return_zero           0x80276a0c  stub (returns 0)
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// tracker_other_player_dest_zone 0x802766b4
// Reads tracker_get_player_dest_zone for the OTHER co-op player (slot 1 if self
// is 1, else 2). (Upstream discards the return.)
// ----------------------------------------------------------------------------
int tracker_other_player_dest_zone(int selfIdx)
{
    int other = 1;
    if (selfIdx == 1) {
        other = 2;
    }
    return tracker_get_player_dest_zone(other);
}

// ----------------------------------------------------------------------------
// tracker_get_player_actor 0x802766e4
// Returns the actor pointer stored in the co-op player-slot table for the given
// slot index (two words per slot, actor at [idx*2]).
// ----------------------------------------------------------------------------
int tracker_get_player_actor(int playerIdx)
{
    return (&DAT_805889f0)[playerIdx * 2];
}

// ----------------------------------------------------------------------------
// tracker_other_player_actor 0x802766fc
// tracker_get_player_actor for the OTHER co-op player.
// ----------------------------------------------------------------------------
int tracker_other_player_actor(int selfIdx)
{
    int other = 1;
    if (selfIdx == 1) {
        other = 2;
    }
    return tracker_get_player_actor(other);
}

// ----------------------------------------------------------------------------
// tracker_find_owned_actor 0x8027672c
// Scans the spawn-visible actor table for the first actor with an owner
// (+0x80 word non-zero). In game-mode 1 any owned actor matches; in mode 2 the
// actor's team (+0x84) must also equal `team`. Returns the actor handle or 0.
// ----------------------------------------------------------------------------
int tracker_find_owned_actor(int team)
{
    int off = 0;
    int n = DAT_80612098;
    if (0 < DAT_80612098) {
        do {
            int* actor = *(int**)((int)&DAT_80588610 + off);
            if ((DAT_805015bd == '\x01') && (actor[0x20] != 0)) {
                return *actor;
            }
            if ((DAT_805015bd == '\x02') && (actor[0x20] != 0) && (actor[0x21] == team)) {
                return *actor;
            }
            off += 4;
            n--;
        } while (n != 0);
    }
    return 0;
}

// ----------------------------------------------------------------------------
// tracker_other_find_owned_actor 0x802767ac
// tracker_find_owned_actor for the OTHER co-op player's team.
// ----------------------------------------------------------------------------
int tracker_other_find_owned_actor(int selfIdx)
{
    int other = 1;
    if (selfIdx == 1) {
        other = 2;
    }
    return tracker_find_owned_actor(other);
}

// ----------------------------------------------------------------------------
// tracker_count_actors_per_team 0x802767dc
// Rebuilds the per-team spawn-count fields of the stat block DAT_806120cc.
// Clears the two count fields for teams 1 and 2, then tallies: the spawned-range
// actors [0,DAT_80612610) into the [-3] field, and the extra-range actors
// [DAT_80612610,DAT_80612098) into the [-4] field, keyed on each actor's team
// (+0x84).
// ----------------------------------------------------------------------------
void tracker_count_actors_per_team(void)
{
    DAT_806120cc[0] = 0;
    DAT_806120cc[1] = 0;
    DAT_806120cc[4] = 0;
    DAT_806120cc[5] = 0;
    int off = 0;
    for (int i = 0; i < DAT_80612610; i++) {
        int* actor = (int*)((int)&DAT_80588610 + off);
        off += 4;
        int team = *(int*)(*actor + 0x84);
        DAT_806120cc[team * 4 + -3] = DAT_806120cc[team * 4 + -3] + 1;
    }
    off = DAT_80612610 << 2;
    for (int i = DAT_80612610; i < DAT_80612098; i++) {
        int* actor = (int*)((int)&DAT_80588610 + off);
        off += 4;
        int team = *(int*)(*actor + 0x84);
        DAT_806120cc[team * 4 + -4] = DAT_806120cc[team * 4 + -4] + 1;
    }
}

// ----------------------------------------------------------------------------
// tracker_coop_players_tick 0x8027688c
// Per-frame driver: in the co-op story game-type (DAT_80611934 == 7) with the
// tracker enabled (DAT_806108e4 > 0), ticks both co-op players and advances the
// round-robin index DAT_806129d8 (wrapping at DAT_80612610).
// ----------------------------------------------------------------------------
void tracker_coop_players_tick(void)
{
    if ((DAT_80611934 == 7) && (0 < DAT_806108e4)) {
        tracker_player_state_change_tick(1);
        tracker_player_state_change_tick(2);
        DAT_806129d8 = DAT_806129d8 + 1;
        if (DAT_806129d8 == DAT_80612610) {
            DAT_806129d8 = 0;
        }
    }
}

// ----------------------------------------------------------------------------
// tracker_player_state_change_tick 0x80276984
// When the match is running (FUN_80278ff0), computes the player's active-flag
// state (tracker_player_active_flag -> 0/1 via countLeadingZeros) and, if it
// differs from the cached state in the stat block ([-2] field), records the
// transition (tracker_record_player_state).
// ----------------------------------------------------------------------------
void tracker_player_state_change_tick(int playerIdx)
{
    if (FUN_80278ff0() != 0) {
        unsigned int state = tracker_player_active_flag(playerIdx);
        state = countLeadingZeros(state) >> 5;
        if (state != *(unsigned int*)((char*)DAT_806120cc + playerIdx * 0x10 + -8)) {
            tracker_record_player_state(playerIdx, (int)state);
        }
    }
}

// ----------------------------------------------------------------------------
// tracker_record_player_state 0x802769e8
// Stores the player's new active-flag state ([-2] field) and stamps the change
// time ([-1] field = current frame timestamp) in the per-team stat block.
// ----------------------------------------------------------------------------
void tracker_record_player_state(int playerIdx, int state)
{
    *(int*)((char*)DAT_806120cc + playerIdx * 0x10 + -8) = state;
    *(int*)((char*)DAT_806120cc + playerIdx * 0x10 + -4) = DAT_80611928;
}

// ----------------------------------------------------------------------------
// tracker_stub_return_zero 0x80276a0c
// Stub; always returns 0.
// ----------------------------------------------------------------------------
int tracker_stub_return_zero(void)
{
    return 0;
}

// ============================================================================
// batch_752 : co-op AI target-selection finite-state machine
//
// The heart of the co-op "buddy AI" targeting. Each spawned actor carries a
// combat-state record (resolved by FUN_80278fbc from its source-entity handle)
// whose first word is a combat MODE (0 = idle/regroup, 1 = follow-enemy-owner,
// 2 = engage, 3 = advance) and whose second word is a state-change timestamp.
// The three big driver functions below are per-mode variants of the same
// behavior tick (chosen by which co-op slot / prologue is active); each reads
// the actor's tracked descriptor (actor+0x7c), updates its movement zone
// (+0x1a1), aim flags (+0x28 / offset 10), and combat-request fields
// (+0x2ab request / +0x2aa arg) then plays the appropriate turn/aim animation.
//
// Tracked-descriptor offsets used here (word indices into actor descriptor):
//   [1]      current follow/aim target actor
//   [2]      owning class index
//   [8]      current movement mode (-1 unset, 1 follow, 2 engage)
//   [0x21]   team / player-slot index
//   [0x1a1]  active path zone id
//   [0x274]  queued movement mode  [0x275] previous  [0x276] mode-change time
//   [0x2a4]  aim-hold timer
//   [0x2a6]  locked target actor
//   [0x2aa]  pending combat-request arg   [0x2ab] pending combat-request flag
//   [0x2ae/0x2af] cached input state words
//   [0x2bd]  last-known target distance
//
// Address range: 0x80276a14 - 0x80278fbb | batch_752 | 2026-09-10
//
// Functions:
//   tracker_is_owner_targeted_by_ally   0x80276a14  ally already covers owner?
//   tracker_update_target_selection     0x80276ae4  per-actor target pick FSM
//   tracker_event_state_is_idle         0x802773b4  event 0x3ea idle predicate
//   tracker_handle_target_lost_event    0x80277414  event 0x3ea state handler
//   tracker_get_actor_zone              0x80277630  actor descriptor zone (+0x678)
//   tracker_combat_behavior_update      0x80277638  behavior tick (variant A)
//   tracker_ally_behavior_update        0x80277d94  behavior tick (variant B)
//   tracker_owner_follow_update         0x802784dc  behavior tick (owner follow)
//   tracker_compute_zone_score          0x80278c4c  zone-distance target score
//   tracker_assign_combat_role          0x80278d6c  team-census role assignment
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// tracker_is_owner_targeted_by_ally 0x80276a14
// Returns non-zero (via break-found index math; here a bool-ish 0/1 is not
// produced by the original, it just scans) - actually the upstream discards the
// scan result and only relies on the side-effect-free loop. We preserve the
// scan: walk the extra-range spawn-visible actors and stop at the first ally on
// the same team that is engaged (mode 2) and is targeting the same owner. The
// original returns void; callers that read a result treat "found" as the loop
// having hit the break. We expose it as int (1 = an ally already covers this
// owner, 0 = none) which matches how tracker_combat_behavior_update consumes it.
// ----------------------------------------------------------------------------
int tracker_is_owner_targeted_by_ally(void)
{
    long long packed = FUN_803826e4();
    int self = (int)((unsigned long long)packed >> 0x20);
    int* selfState = FUN_80278fbc(*(int*)(self + 8));
    int found = 0;
    if (*selfState != 1) {
        int off = DAT_80612610 << 2;
        for (int i = DAT_80612610; i < DAT_80612098; i = i + 1) {
            int other = *(int*)((int)&DAT_80588610 + off);
            int* otherState = FUN_80278fbc(*(int*)(other + 8));
            if ((other != self) && (*(int*)(other + 0x84) == *(int*)(self + 0x84)) &&
                ((*(unsigned int*)(other + 0x24) & 8) == 0) &&
                (*otherState == 2) && (*(int*)(other + 0xa98) == *(int*)packed)) {
                found = 1;
                break;
            }
            off = off + 4;
        }
    }
    FUN_80382730();
    return found;
}

// ----------------------------------------------------------------------------
// tracker_update_target_selection 0x80276ae4
// The per-actor target-selection FSM. Reads the actor's combat mode and, gated
// by aim-state flags and repath timers, picks a new locked target (+0x2a6) or a
// pending combat request (+0x2ab / +0x2aa). Mode 2 (engage) does a nearest-
// enemy scan over the active-player context table (DAT_80611d78, stride 0x141c),
// preferring hostiles that pass tracker_is_owner_targeted_by_ally. Mode 0 / 1
// resolve owner/other-owner actors and set the matching request flag.
// ----------------------------------------------------------------------------
void tracker_update_target_selection(void)
{
    long long packed = FUN_803826e4();
    int* self = (int*)((unsigned long long)packed >> 0x20);
    int aux = (int)packed;
    int* state = FUN_80278fbc(self[2]);

    if ((self[9] & 8U) != 0) { FUN_80382730(); return; }

    unsigned int flags = (DAT_805015bd == '\n') ? (unsigned int)self[5] : (unsigned int)self[7];
    if ((flags & 0xe2) != 0) { FUN_80382730(); return; }

    int mode = *state;
    if (mode == 2) {
        float timeGate = FLOAT_80619200 *
            (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80619218);
        float sinceRepath =
            (float)((double)CONCAT44(0x43300000, DAT_80611928 - state[1] ^ 0x80000000) - DOUBLE_80619218);
        if ((aux != 0) || (timeGate < sinceRepath)) {
            flags = (DAT_805015bd == '\n') ? (unsigned int)self[5] : (unsigned int)self[7];
            if ((flags & 0x8000004) == 0) {
                int locked = self[0x2a6];
                bool clearToScan = (locked == 0) ||
                    ((*(unsigned int*)(*(int*)(locked + 0x7c) + 0x24) & 8) != 0);
                if (!clearToScan) {
                    int a = *self;
                    float dx = *(float*)(a + 100) - *(float*)(locked + 100);
                    float dy = *(float*)(a + 0x68) - *(float*)(locked + 0x68);
                    float dz = *(float*)(a + 0x6c) - *(float*)(locked + 0x6c);
                    clearToScan = FLOAT_8061920c < dz * dz + dx * dx + dy * dy;
                }
                if (clearToScan) {
                    double bestNoAlly = (double)FLOAT_80619210;
                    double bestAlly = bestNoAlly;
                    int pickAlly = -1;
                    int pickNoAlly = -1;
                    state[1] = DAT_80611928;
                    int ctxOff = 0;
                    for (int i = 0; i < DAT_80612610; i = i + 1) {
                        int* cand = *(int**)(*(int*)(DAT_80611d78 + ctxOff + 0x14) + 0x7c);
                        if ((self[0x21] == cand[0x21]) && ((cand[9] & 8U) == 0)) {
                            int ca = *cand;
                            int sa = *self;
                            float dx = *(float*)(sa + 100) - *(float*)(ca + 100);
                            float dy = *(float*)(sa + 0x68) - *(float*)(ca + 0x68);
                            float dz = *(float*)(sa + 0x6c) - *(float*)(ca + 0x6c);
                            double dist = (double)(dz * dz + dx * dx + dy * dy);
                            int allyCovers = tracker_is_owner_targeted_by_ally();
                            if (allyCovers == 0) {
                                if ((pickAlly == -1) && (dist < bestNoAlly)) {
                                    pickNoAlly = i;
                                    bestNoAlly = dist;
                                }
                            } else if (dist < bestAlly) {
                                pickAlly = i;
                                bestAlly = dist;
                            }
                        }
                        ctxOff = ctxOff + 0x141c;
                    }
                    if (pickAlly == -1) {
                        pickAlly = pickNoAlly;
                    }
                    if (-1 < pickAlly) {
                        self[0x2a6] = *(int*)(DAT_80611d78 + pickAlly * 0x141c + 0x14);
                        bool skip = false;
                        if (self[0x2ab] == 1 && self[0x2aa] == 0) {
                            unsigned int f2 = (DAT_805015bd == '\n')
                                ? (unsigned int)self[5] : (unsigned int)self[7];
                            skip = ((f2 & 1) == 0);
                        }
                        if (!skip) {
                            self[0x2ab] = 0x8000000;
                            self[0x2aa] = 0;
                        }
                    }
                }
            }
        }
    } else if (mode == 0) {
        unsigned int active = tracker_player_active_flag(self[0x21]);
        if (active == 0) {
            float timeGate = FLOAT_80619200 *
                (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80619218);
            float sinceRepath = (float)((double)CONCAT44(0x43300000,
                DAT_80611928 - state[1] ^ 0x80000000) - DOUBLE_80619218);
            if (((aux != 0) || (timeGate < sinceRepath)) && ((self[7] & 0x4000000U) == 0)) {
                int owner = tracker_other_find_owned_actor(self[0x21]);
                state[1] = DAT_80611928;
                unsigned int f = (DAT_805015bd == '\n')
                    ? (unsigned int)self[5] : (unsigned int)self[7];
                bool commit = ((f & 4) == 0);
                if (!commit && owner != 0) {
                    int a = *self;
                    float dx = *(float*)(a + 100) - *(float*)(owner + 100);
                    float dy = *(float*)(a + 0x68) - *(float*)(owner + 0x68);
                    float dz = *(float*)(a + 0x6c) - *(float*)(owner + 0x6c);
                    commit = FLOAT_80619204 < dz * dz + dx * dx + dy * dy;
                }
                if (commit) {
                    bool skip = false;
                    if (self[0x2ab] == 1 && self[0x2aa] == 0) {
                        unsigned int f2 = (DAT_805015bd == '\n')
                            ? (unsigned int)self[5] : (unsigned int)self[7];
                        skip = ((f2 & 1) == 0);
                    }
                    if (!skip) {
                        self[0x2ab] = 0x4000000;
                        self[0x2aa] = 0;
                    }
                }
            }
        } else {
            bool skip = false;
            if (self[0x2ab] == 1 && self[0x2aa] == 0) {
                unsigned int f2 = (DAT_805015bd == '\n')
                    ? (unsigned int)self[5] : (unsigned int)self[7];
                skip = ((f2 & 1) == 0);
            }
            if (!skip) {
                self[0x2ab] = 0x10;
                self[0x2aa] = 0;
            }
        }
    } else if ((-1 < mode) && (mode < 2)) {
        // mode == 1 (follow enemy owner)
        float timeGate = FLOAT_80619200 *
            (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80619218);
        float sinceRepath = (float)((double)CONCAT44(0x43300000,
            DAT_80611928 - state[1] ^ 0x80000000) - DOUBLE_80619218);
        if (((aux != 0) || (timeGate < sinceRepath)) && ((self[7] & 0x2000000U) == 0)) {
            int owned = tracker_find_owned_actor(self[0x21]);
            int otherActor = tracker_other_player_actor(self[0x21]);
            state[1] = DAT_80611928;
            if ((owned == 0) || (*self != owned)) {
                if (owned == 0) {
                    int a = *self;
                    float dx = *(float*)(a + 100) - *(float*)(otherActor + 100);
                    float dy = *(float*)(a + 0x68) - *(float*)(otherActor + 0x68);
                    float dz = *(float*)(a + 0x6c) - *(float*)(otherActor + 0x6c);
                    bool farEnough = FLOAT_80619208 <= dz * dz + dx * dx + dy * dy;
                    bool skip = false;
                    if (farEnough) {
                        unsigned int f = (DAT_805015bd == '\n')
                            ? (unsigned int)self[5] : (unsigned int)self[7];
                        if ((f & 0xc40004) != 0) {
                            skip = true;
                        }
                    }
                    if (!skip) {
                        if (self[0x2ab] == 1 && self[0x2aa] == 0) {
                            unsigned int f2 = (DAT_805015bd == '\n')
                                ? (unsigned int)self[5] : (unsigned int)self[7];
                            if ((f2 & 1) == 0) { skip = true; }
                        }
                    }
                    if (!skip) {
                        self[0x2ab] = 0x2000000;
                        self[0x2aa] = 0;
                    }
                } else {
                    unsigned int f = (DAT_805015bd == '\n')
                        ? (unsigned int)self[5] : (unsigned int)self[7];
                    if ((f & 0x8000004) == 0) {
                        self[0x2a6] = owned;
                        bool skip = false;
                        if (self[0x2ab] == 1 && self[0x2aa] == 0) {
                            unsigned int f2 = (DAT_805015bd == '\n')
                                ? (unsigned int)self[5] : (unsigned int)self[7];
                            skip = ((f2 & 1) == 0);
                        }
                        if (!skip) {
                            self[0x2ab] = 0x8000000;
                            self[0x2aa] = 0;
                        }
                    }
                }
            } else {
                bool skip = false;
                if (self[0x2ab] == 1 && self[0x2aa] == 0) {
                    unsigned int f2 = (DAT_805015bd == '\n')
                        ? (unsigned int)self[5] : (unsigned int)self[7];
                    skip = ((f2 & 1) == 0);
                }
                if (!skip) {
                    self[0x2ab] = 0x2000000;
                    self[0x2aa] = 0;
                }
            }
        }
    }
    FUN_80382730();
}

// ----------------------------------------------------------------------------
// tracker_event_state_is_idle 0x802773b4
// Event predicate: in game-mode 2 (co-op) returns 0 when the event source's
// combat state is idle (mode 0), otherwise 1. Used to decide whether an event
// should be suppressed while the actor is idle.
// ----------------------------------------------------------------------------
int tracker_event_state_is_idle(int actor)
{
    int result = 1;
    if (DAT_805015bd == '\x02') {
        int* state = FUN_80278fbc(*(int*)(actor + 8));
        if (*state == 0) {
            result = 0;
        }
    }
    return result;
}

// ----------------------------------------------------------------------------
// tracker_handle_target_lost_event 0x80277414
// Handles event id 0x3ea (target-lost). Validates the actor's pending combat
// request (+0xaa8 arg / +0xaac flag) against the allowed transitions, checks aim
// and hidden flags, and if the combat state is idle promotes the request to the
// "player-owner" flag (0x4000000), returning 0xfffffffe to signal the caller to
// re-dispatch. Any non-0x3ea event returns 0.
// ----------------------------------------------------------------------------
int tracker_handle_target_lost_event(int actor, int event)
{
    if (event != 0x3ea) {
        return 0;
    }
    int desc = *(int*)(actor + 0x7c);
    int* state = FUN_80278fbc(*(int*)(desc + 8));
    unsigned int arg = *(unsigned int*)(desc + 0xaa8);
    unsigned int flag = *(unsigned int*)(desc + 0xaac);
    if (flag == 0x2000000 && arg == 0) {
        return 0;
    }
    if (arg < (unsigned int)(flag < 0x2000000)) {
        if (flag != 3 || arg != 0) {
            if (arg < (unsigned int)(flag < 3)) {
                if ((unsigned int)(flag == 0) <= arg) {
                    return 0;
                }
            } else if (arg < (unsigned int)(flag < 5)) {
                return 0;
            }
        }
    } else if (flag == 0x4000000 && arg == 0) {
        return 0;
    }
    unsigned int aim = (DAT_805015bd == '\n')
        ? *(unsigned int*)(desc + 0x14) : *(unsigned int*)(desc + 0x1c);
    if ((aim & 2) != 0) {
        return 0;
    }
    if ((*(unsigned int*)(desc + 0x24) & 8) != 0) {
        return 0;
    }
    if (*state == 0) {
        state[1] = DAT_80611928;
        if (*(int*)(desc + 0xaac) == 1 && *(int*)(desc + 0xaa8) == 0) {
            unsigned int f2 = (DAT_805015bd == '\n')
                ? *(unsigned int*)(desc + 0x14) : *(unsigned int*)(desc + 0x1c);
            if ((f2 & 1) == 0) {
                return 0xfffffffe;
            }
        }
        *(int*)(desc + 0xaac) = 0x4000000;
        *(int*)(desc + 0xaa8) = 0;
        return 0xfffffffe;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// tracker_get_actor_zone 0x80277630
// Returns the actor descriptor's current zone id (+0x678). The first argument is
// the ignored caller context (present in the original ABI); the actor
// descriptor pointer is the second argument.
// ----------------------------------------------------------------------------
int tracker_get_actor_zone(int unused, int actor)
{
    (void)unused;
    return *(int*)(actor + 0x678);
}

// ----------------------------------------------------------------------------
// tracker_combat_behavior_update 0x80277638
// The main co-op AI behavior tick (variant A, driven by the FUN_803826e4
// prologue). Handles queued mode swaps, the aim-hold timer, and the three
// movement modes: engage (mode 2 - path to target zone, close-combat regroup),
// retreat/idle (mode <= 0), and advance (mode 1 - strafe around the target with
// a rotated offset). Sets aim/turn animation flags and the pending combat
// request each frame.
// ----------------------------------------------------------------------------
void tracker_combat_behavior_update(void)
{
    int self = FUN_803826e4();
    int* d = *(int**)(self + 0x7c);
    int teammate = aiactor_retarget_visible_teammate();

    if (-1 < d[0x274]) {
        if (d[0x274] != d[8]) {
            d[0x276] = (int)FLOAT_80619220;
            d[0x275] = d[8];
            d[8] = d[0x274];
        }
        d[0x274] = -1;
    }
    if ((d[0xb] & 0x80U) == 0) {
        d[0x2a4] = (int)FLOAT_80619220;
    } else {
        double dt = (double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80619218;
        d[0x2a4] = (int)(FLOAT_806108ec * (FLOAT_80619224 / (float)dt) + (float)d[0x2a4]);
    }

    if (d[0x2a6] == 0) {
        bool skip = false;
        if (d[0x2ab] == 1 && d[0x2aa] == 0) {
            unsigned int f = (DAT_805015bd == '\n') ? (unsigned int)d[5] : (unsigned int)d[7];
            skip = ((f & 1) == 0);
        }
        if (!skip) {
            d[0x2ab] = 0x10;
            d[0x2aa] = 0;
        }
        FUN_80382730();
        return;
    }

    int targetDesc = *(int*)(d[0x2a6] + 0x7c);
    char* relation = (char*)aiactor_relation_entry(d[2], *(int*)(targetDesc + 8));
    char relClose = *relation;

    bool teammateBlocks = (d[1] != 0) && (teammate != 0) &&
        ((*(unsigned int*)(*(int*)(d[1] + 0x7c) + 0x24) & 8) == 0);
    if (teammateBlocks) {
        bool skip = false;
        if (d[0x2ab] == 1 && d[0x2aa] == 0) {
            unsigned int f = (DAT_805015bd == '\n') ? (unsigned int)d[5] : (unsigned int)d[7];
            skip = ((f & 1) == 0);
        }
        if (!skip) {
            d[0x2ab] = 4;
            d[0x2aa] = 0;
        }
        FUN_80382730();
        return;
    }

    if (d[2] == DAT_806120f8) {
        aiactor_pick_best_weapon();
    }
    int mode = d[8];
    bool didAimBlock = false;
    if (mode == 1) {
        int* zoneNode = soundpad_resolve_zone_node(*(int*)(targetDesc + 0x678));
        int targetZone = soundpad_get_entity_zone_id((int)zoneNode);
        zoneNode = soundpad_resolve_zone_node(d[0x1a1]);
        int curZone = soundpad_get_entity_zone_id((int)zoneNode);
        if (curZone != targetZone) {
            int z = tracker_get_actor_zone(self, targetDesc);
            if (z < 0) {
                d[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            } else {
                d[0x1a1] = z;
            }
            d[0x26b] = 0;
            d[0x26a] = 0;
        }
        int arrived = aiactor_follow_zone_path();
        bool closeCombat = false;
        if (relClose != '\0') {
            int t = d[0x2a6];
            float dx = *(float*)(self + 100) - *(float*)(t + 100);
            float dy = *(float*)(self + 0x68) - *(float*)(t + 0x68);
            float dz = *(float*)(self + 0x6c) - *(float*)(t + 0x6c);
            closeCombat = dz * dz + dx * dx + dy * dy < FLOAT_80619228;
        }
        if ((arrived != 0 || closeCombat) && (d[8] != 2)) {
            d[0x275] = d[8];
            d[8] = 2;
            d[0x276] = (int)FLOAT_80619220;
            d[0x274] = d[8];
        }
        if (d[2] == DAT_806120f8) {
            FUN_802b6d9c();
        }
        if ((d[1] == 0) ||
            (!aiactor_aim_alignment_test((double)(float)d[0x26f], self, (float*)(d[1] + 100)))) {
            d[10] = d[10] | 0x40000;
        } else if (teammate != 0) {
            d[10] = d[10] | 2;
            d[10] = d[10] & 0xfffbffff;
        }
        aiactor_set_stance_flags(self, 1);
        aiactor_play_turn_animation(self);
        aiactor_facing_update();
        d[10] = d[10] | 0x40000;
        anim_set_playback_rate((double)FLOAT_8061922c, *(int**)(self + 4), 2);
    } else if (mode < 1) {
        if (-1 < mode) {
            if (d[0x2a6] == 0) {
                bool skip = false;
                if (d[0x2ab] == 1 && d[0x2aa] == 0) {
                    unsigned int f = (DAT_805015bd == '\n')
                        ? (unsigned int)d[5] : (unsigned int)d[7];
                    skip = ((f & 1) == 0);
                }
                if (!skip) {
                    d[0x2ab] = 0x10;
                    d[0x2aa] = 0;
                }
                FUN_80382730();
                return;
            }
            int z = tracker_get_actor_zone(self, targetDesc);
            if (z < 0) {
                d[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            } else {
                d[0x1a1] = z;
            }
            d[0x26b] = 0;
            d[0x26a] = 0;
            if (d[8] != 1) {
                d[0x275] = d[8];
                d[8] = 1;
                d[0x276] = (int)FLOAT_80619220;
                d[0x274] = d[8];
            }
        }
    } else if (mode < 3) {
        int t = d[0x2a6];
        int a = *d;
        float dx = *(float*)(a + 100) - *(float*)(t + 100);
        float dy = *(float*)(a + 0x68) - *(float*)(t + 0x68);
        float dz = *(float*)(a + 0x6c) - *(float*)(t + 0x6c);
        if ((FLOAT_8061920c < dz * dz + dx * dx + dy * dy) || (relClose == '\0')) {
            int z = tracker_get_actor_zone(self, targetDesc);
            if (z < 0) {
                d[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            } else {
                d[0x1a1] = z;
            }
            d[0x26b] = 0;
            d[0x26a] = 0;
            if (d[8] != 1) {
                d[0x275] = d[8];
                d[8] = 1;
                d[0x276] = (int)FLOAT_80619220;
                d[0x274] = d[8];
            }
        }
        if (d[2] == DAT_806120f8) {
            FUN_802b6d9c();
        }
        if ((d[1] == 0) ||
            (!aiactor_aim_alignment_test((double)(float)d[0x26f], self, (float*)(d[1] + 100)))) {
            d[10] = d[10] | 0x40000;
        } else if (teammate != 0) {
            d[10] = d[10] | 2;
            d[10] = d[10] & 0xfffbffff;
        }
        FUN_80280c8c(self);
        d[10] = d[10] | 0x40000;
        anim_set_playback_rate((double)FLOAT_8061922c, *(int**)(self + 4), 2);
        if (d[0x2a6] != 0) {
            float offset[18];
            offset[0] = DAT_803e0558;
            offset[1] = DAT_803e055c;
            offset[2] = DAT_803e0560;
            mtx4_set_rotation_y(
                (double)((FLOAT_80619230 * *(float*)(d[0x2a6] + 0x70)) / FLOAT_80619234),
                offset + 3);
            mtx4_transform_vector(offset + 3, offset);
            int t2 = d[0x2a6];
            offset[0] = offset[0] + *(float*)(t2 + 100);
            offset[1] = offset[1] + *(float*)(t2 + 0x68);
            offset[2] = offset[2] + *(float*)(t2 + 0x6c);
            aiactor_turn_toward_position(self, offset, 0);
        }
    }

    if ((d[10] & 1U) != 0) {
        int in0, in1;
        if (DAT_805015bd == '\n') {
            in0 = d[4]; in1 = d[5];
        } else {
            in0 = d[6]; in1 = d[7];
        }
        d[0x2af] = in1;
        d[0x2ae] = in0;
        bool skip = false;
        if (d[0x2ab] == 1 && d[0x2aa] == 0) {
            unsigned int f = (DAT_805015bd == '\n') ? (unsigned int)d[5] : (unsigned int)d[7];
            skip = ((f & 1) == 0);
        }
        if (!skip) {
            d[0x2ab] = 0x20;
            d[0x2aa] = 0;
        }
    }
    (void)didAimBlock;
    FUN_80382730();
}

// ----------------------------------------------------------------------------
// tracker_ally_behavior_update 0x80277d94
// Behavior tick variant B (driven by the FUN_803826e8 prologue / restore-28
// epilogue). Same structure as tracker_combat_behavior_update but keyed on the
// co-op ownership helpers: if this actor is owned by the other player it locks
// onto that owner; otherwise it follows/normalizes the player zone and engages
// once close. Modes: engage (1), idle (<=0), advance (mode < 3).
// ----------------------------------------------------------------------------
void tracker_ally_behavior_update(void)
{
    int self = FUN_803826e8();
    int* d = *(int**)(self + 0x7c);
    int teammate = aiactor_retarget_visible_teammate();

    if (-1 < d[0x274]) {
        if (d[0x274] != d[8]) {
            d[0x276] = (int)FLOAT_80619220;
            d[0x275] = d[8];
            d[8] = d[0x274];
        }
        d[0x274] = -1;
    }
    if ((d[0xb] & 0x80U) == 0) {
        d[0x2a4] = (int)FLOAT_80619220;
    } else {
        double dt = (double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80619218;
        d[0x2a4] = (int)(FLOAT_806108ec * (FLOAT_80619224 / (float)dt) + (float)d[0x2a4]);
    }

    int owned = tracker_find_owned_actor(d[0x21]);
    if ((owned != 0) && (self != owned)) {
        d[0x2a6] = owned;
        bool skip = false;
        if (d[0x2ab] == 1 && d[0x2aa] == 0) {
            unsigned int f = (DAT_805015bd == '\n') ? (unsigned int)d[5] : (unsigned int)d[7];
            skip = ((f & 1) == 0);
        }
        if (!skip) {
            d[0x2ab] = 0x8000000;
            d[0x2aa] = 0;
        }
        FUN_80382734();
        return;
    }

    if (d[2] == DAT_806120f8) {
        aiactor_pick_best_weapon();
    }
    int mode = d[8];
    if (mode == 1) {
        int otherActor = tracker_other_player_actor(d[0x21]);
        int destZone;
        if (owned == 0) {
            int nz = tracker_normalize_other_player_zone(d[0x21]);
            if (nz != d[0x1a1]) {
                if (nz < 0) {
                    d[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                } else {
                    d[0x1a1] = nz;
                }
                d[0x26b] = 0;
                d[0x26a] = 0;
            }
        }
        int arrived = aiactor_follow_zone_path();
        if ((arrived == 0) && ((owned == 0) || (self != owned))) {
            int a = *d;
            float dx = *(float*)(a + 100) - *(float*)(otherActor + 100);
            float dy = *(float*)(a + 0x68) - *(float*)(otherActor + 0x68);
            float dz = *(float*)(a + 0x6c) - *(float*)(otherActor + 0x6c);
            if ((FLOAT_80619238 < dz * dz + dx * dx + dy * dy) && (d[1] != 0) &&
                (teammate != 0) && ((float)d[0x2bd] < FLOAT_80619238) &&
                ((*(unsigned int*)(*(int*)(d[1] + 0x7c) + 0x24) & 8) == 0)) {
                bool skip = false;
                if (d[0x2ab] == 1 && d[0x2aa] == 0) {
                    unsigned int f = (DAT_805015bd == '\n')
                        ? (unsigned int)d[5] : (unsigned int)d[7];
                    skip = ((f & 1) == 0);
                }
                if (!skip) {
                    d[0x2ab] = 4;
                    d[0x2aa] = 0;
                }
            }
        } else {
            destZone = tracker_get_player_dest_zone(d[0x21]);
            if (destZone < 0) {
                d[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            } else {
                d[0x1a1] = destZone;
            }
            d[0x26b] = 0;
            d[0x26a] = 0;
            if (d[8] != 2) {
                d[0x275] = d[8];
                d[8] = 2;
                d[0x276] = (int)FLOAT_80619220;
                d[0x274] = d[8];
            }
        }
        if (d[2] == DAT_806120f8) {
            FUN_802b6d9c();
        }
        if ((d[1] == 0) ||
            (!aiactor_aim_alignment_test((double)(float)d[0x26f], self, (float*)(d[1] + 100)))) {
            d[10] = d[10] | 0x40000;
        } else if (teammate != 0) {
            d[10] = d[10] | 2;
            d[10] = d[10] & 0xfffbffff;
        }
        aiactor_set_stance_flags(self, 1);
        aiactor_play_turn_animation(self);
        aiactor_facing_update();
        d[10] = d[10] | 0x40000;
        anim_set_playback_rate((double)FLOAT_8061922c, *(int**)(self + 4), 2);
    } else if (mode < 1) {
        if (-1 < mode) {
            if ((owned == 0) || (self != owned)) {
                int nz = tracker_normalize_other_player_zone(d[0x21]);
                if (nz < 0) {
                    d[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                } else {
                    d[0x1a1] = nz;
                }
                d[0x26b] = 0;
                d[0x26a] = 0;
                if (d[8] != 1) {
                    d[0x275] = d[8];
                    d[8] = 1;
                    d[0x276] = (int)FLOAT_80619220;
                    d[0x274] = d[8];
                }
            } else {
                int dz2 = tracker_get_player_dest_zone(d[0x21]);
                if (dz2 < 0) {
                    d[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                } else {
                    d[0x1a1] = dz2;
                }
                d[0x26b] = 0;
                d[0x26a] = 0;
                if (d[8] != 2) {
                    d[0x275] = d[8];
                    d[8] = 2;
                    d[0x276] = (int)FLOAT_80619220;
                    d[0x274] = d[8];
                }
            }
        }
    } else if (mode < 3) {
        if ((owned == 0) || (self == owned)) {
            int arrived = aiactor_follow_zone_path();
            if (arrived != 0) {
                int oz = tracker_other_player_dest_zone(d[0x21]);
                if (oz < 0) {
                    d[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                } else {
                    d[0x1a1] = oz;
                }
                d[0x26b] = 0;
                d[0x26a] = 0;
                if (d[8] != 1) {
                    d[0x275] = d[8];
                    d[8] = 1;
                    d[0x276] = (int)FLOAT_80619220;
                    d[0x274] = d[8];
                }
            }
        } else {
            d[0x2a6] = owned;
            bool skip = false;
            if (d[0x2ab] == 1 && d[0x2aa] == 0) {
                unsigned int f = (DAT_805015bd == '\n') ? (unsigned int)d[5] : (unsigned int)d[7];
                skip = ((f & 1) == 0);
            }
            if (!skip) {
                d[0x2ab] = 0x8000000;
                d[0x2aa] = 0;
            }
        }
        if (d[2] == DAT_806120f8) {
            FUN_802b6d9c();
        }
        if ((d[1] == 0) ||
            (!aiactor_aim_alignment_test((double)(float)d[0x26f], self, (float*)(d[1] + 100)))) {
            d[10] = d[10] | 0x40000;
        } else if (teammate != 0) {
            d[10] = d[10] | 2;
            d[10] = d[10] & 0xfffbffff;
        }
        aiactor_set_stance_flags(self, 1);
        aiactor_play_turn_animation(self);
        aiactor_facing_update();
        d[10] = d[10] | 0x40000;
        anim_set_playback_rate((double)FLOAT_8061922c, *(int**)(self + 4), 2);
    }

    if ((d[10] & 1U) != 0) {
        int in0, in1;
        if (DAT_805015bd == '\n') {
            in0 = d[4]; in1 = d[5];
        } else {
            in0 = d[6]; in1 = d[7];
        }
        d[0x2af] = in1;
        d[0x2ae] = in0;
        bool skip = false;
        if (d[0x2ab] == 1 && d[0x2aa] == 0) {
            unsigned int f = (DAT_805015bd == '\n') ? (unsigned int)d[5] : (unsigned int)d[7];
            skip = ((f & 1) == 0);
        }
        if (!skip) {
            d[0x2ab] = 0x20;
            d[0x2aa] = 0;
        }
    }
    FUN_80382734();
}

// ----------------------------------------------------------------------------
// tracker_owner_follow_update 0x802784dc
// Behavior tick variant (driven by the __save_gpr_28 prologue). Keyed on the
// "other owned actor" helper: resolves the owner actor for this team and, per
// movement mode, either follows the owner's zone directly (locking onto the
// owner as target and issuing a follow request) or normalizes the local player
// zone. Sets aim/turn animation the same way as the other variants.
// ----------------------------------------------------------------------------
void tracker_owner_follow_update(void)
{
    int self = __save_gpr_28();
    int* d = *(int**)(self + 0x7c);
    int teammate = aiactor_retarget_visible_teammate();

    if (-1 < d[0x274]) {
        if (d[0x274] != d[8]) {
            d[0x276] = (int)FLOAT_80619220;
            d[0x275] = d[8];
            d[8] = d[0x274];
        }
        d[0x274] = -1;
    }
    if ((d[0xb] & 0x80U) == 0) {
        d[0x2a4] = (int)FLOAT_80619220;
    } else {
        double dt = (double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80619218;
        d[0x2a4] = (int)(FLOAT_806108ec * (FLOAT_80619224 / (float)dt) + (float)d[0x2a4]);
    }

    int owner = tracker_other_find_owned_actor(d[0x21]);
    if (d[2] == DAT_806120f8) {
        aiactor_pick_best_weapon();
    }
    int mode = d[8];
    if (mode == 1) {
        if (owner == 0) {
            d[700] = tracker_get_player_actor(d[0x21]);
            int nz = tracker_normalize_player_zone(d[0x21]);
            if (nz < 0) {
                d[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            } else {
                d[0x1a1] = nz;
            }
            d[0x26b] = 0;
            d[0x26a] = 0;
            if (d[8] != 2) {
                d[0x275] = d[8];
                d[8] = 2;
                d[0x276] = (int)FLOAT_80619220;
                d[0x274] = d[8];
            }
        } else {
            int oz = *(int*)(*(int*)(owner + 0x7c) + 0x678);
            if (d[0x1a1] != oz) {
                if (oz < 0) {
                    d[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                } else {
                    d[0x1a1] = oz;
                }
                d[0x26b] = 0;
                d[0x26a] = 0;
            }
        }
        bool engage = false;
        if (owner != 0) {
            int arrived = aiactor_follow_zone_path();
            if (arrived != 0) {
                engage = true;
            } else {
                int a = *d;
                float dx = *(float*)(a + 100) - *(float*)(owner + 100);
                float dy = *(float*)(a + 0x68) - *(float*)(owner + 0x68);
                float dz = *(float*)(a + 0x6c) - *(float*)(owner + 0x6c);
                engage = dz * dz + dx * dx + dy * dy < FLOAT_8061923c;
            }
        }
        if (engage) {
            d[1] = owner;
            bool skip = false;
            if (d[0x2ab] == 1 && d[0x2aa] == 0) {
                unsigned int f = (DAT_805015bd == '\n') ? (unsigned int)d[5] : (unsigned int)d[7];
                skip = ((f & 1) == 0);
            }
            if (!skip) {
                d[0x2ab] = 4;
                d[0x2aa] = 0;
            }
        }
        if (d[2] == DAT_806120f8) {
            FUN_802b6d9c();
        }
        if ((d[1] == 0) ||
            (!aiactor_aim_alignment_test((double)(float)d[0x26f], self, (float*)(d[1] + 100)))) {
            d[10] = d[10] | 0x40000;
        } else if (teammate != 0) {
            d[10] = d[10] | 2;
            d[10] = d[10] & 0xfffbffff;
        }
        aiactor_set_stance_flags(self, 1);
        aiactor_play_turn_animation(self);
        aiactor_facing_update();
        d[10] = d[10] | 0x40000;
        anim_set_playback_rate((double)FLOAT_8061922c, *(int**)(self + 4), 2);
    } else if (mode < 1) {
        if (-1 < mode) {
            if (owner == 0) {
                d[700] = tracker_get_player_actor(d[0x21]);
                int nz = tracker_normalize_player_zone(d[0x21]);
                if (nz < 0) {
                    d[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                } else {
                    d[0x1a1] = nz;
                }
                d[0x26b] = 0;
                d[0x26a] = 0;
                if (d[8] != 2) {
                    d[0x275] = d[8];
                    d[8] = 2;
                    d[0x276] = (int)FLOAT_80619220;
                    d[0x274] = d[8];
                }
            } else {
                d[1] = owner;
                int oz = *(int*)(*(int*)(owner + 0x7c) + 0x678);
                if (oz < 0) {
                    d[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                } else {
                    d[0x1a1] = oz;
                }
                d[0x26b] = 0;
                d[0x26a] = 0;
                if (d[8] != 1) {
                    d[0x275] = d[8];
                    d[8] = 1;
                    d[0x276] = (int)FLOAT_80619220;
                    d[0x274] = d[8];
                }
            }
            if (d[2] == DAT_806120f8) {
                FUN_802b6d9c();
            }
            if ((d[1] == 0) ||
                (!aiactor_aim_alignment_test((double)(float)d[0x26f], self, (float*)(d[1] + 100)))) {
                d[10] = d[10] | 0x40000;
            } else if (teammate != 0) {
                d[10] = d[10] | 2;
                d[10] = d[10] & 0xfffbffff;
            }
            aiactor_set_stance_flags(self, 1);
            aiactor_play_turn_animation(self);
            aiactor_facing_update();
            d[10] = d[10] | 0x40000;
            anim_set_playback_rate((double)FLOAT_8061922c, *(int**)(self + 4), 2);
        }
    } else if (mode < 3) {
        if (owner == 0) {
            int nz = tracker_normalize_player_zone(d[0x21]);
            if (d[0x1a1] != nz) {
                if (nz < 0) {
                    d[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                } else {
                    d[0x1a1] = nz;
                }
                d[0x26b] = 0;
                d[0x26a] = 0;
            }
        } else {
            d[1] = owner;
            int oz = *(int*)(*(int*)(owner + 0x7c) + 0x678);
            if (oz < 0) {
                d[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            } else {
                d[0x1a1] = oz;
            }
            d[0x26b] = 0;
            d[0x26a] = 0;
            if (d[8] != 1) {
                d[0x275] = d[8];
                d[8] = 1;
                d[0x276] = (int)FLOAT_80619220;
                d[0x274] = d[8];
            }
        }
        int arrived = aiactor_follow_zone_path();
        if (arrived != 0) {
            bool skip = false;
            if (d[0x2ab] == 1 && d[0x2aa] == 0) {
                unsigned int f = (DAT_805015bd == '\n') ? (unsigned int)d[5] : (unsigned int)d[7];
                skip = ((f & 1) == 0);
            }
            if (!skip) {
                d[0x2ab] = 0x10;
                d[0x2aa] = 0;
            }
        }
        if (d[2] == DAT_806120f8) {
            FUN_802b6d9c();
        }
        if ((d[1] == 0) ||
            (!aiactor_aim_alignment_test((double)(float)d[0x26f], self, (float*)(d[1] + 100)))) {
            d[10] = d[10] | 0x40000;
        } else if (teammate != 0) {
            d[10] = d[10] | 2;
            d[10] = d[10] & 0xfffbffff;
        }
        aiactor_set_stance_flags(self, 1);
        aiactor_play_turn_animation(self);
        aiactor_facing_update();
        d[10] = d[10] | 0x40000;
        anim_set_playback_rate((double)FLOAT_8061922c, *(int**)(self + 4), 2);
    }

    if ((d[10] & 1U) != 0) {
        int in0, in1;
        if (DAT_805015bd == '\n') {
            in0 = d[4]; in1 = d[5];
        } else {
            in0 = d[6]; in1 = d[7];
        }
        d[0x2af] = in1;
        d[0x2ae] = in0;
        bool skip = false;
        if (d[0x2ab] == 1 && d[0x2aa] == 0) {
            unsigned int f = (DAT_805015bd == '\n') ? (unsigned int)d[5] : (unsigned int)d[7];
            skip = ((f & 1) == 0);
        }
        if (!skip) {
            d[0x2ab] = 0x20;
            d[0x2aa] = 0;
        }
    }
    __restore_gpr_28();
}

// ----------------------------------------------------------------------------
// tracker_compute_zone_score 0x80278c4c
// Scores a candidate record's zone against the actor's current tracking zone
// (+0x19e) for target selection. For mode 0 it resolves the player's zone (or
// the owned-actor's zone when the player has the bit4 flag); for mode 1 it does
// the same for the other player, returning a large sentinel if the candidate is
// this same actor. Score = (bias - zone_path_distance). Modes 2/3 return the
// reset sentinel FLOAT_80619220.
// ----------------------------------------------------------------------------
double tracker_compute_zone_score(int* rec, int mode)
{
    double score = (double)FLOAT_80619220;
    int slot = rec[0x21];
    if ((mode != 2) && (mode < 2)) {
        if (mode == 0) {
            unsigned int flagged = tracker_player_flag_bit4(slot);
            int zone;
            if (flagged == 0) {
                zone = tracker_normalize_player_zone(slot);
            } else {
                int owner = tracker_other_find_owned_actor(slot);
                zone = *(int*)(*(int*)(owner + 0x7c) + 0x678);
            }
            int* node = soundpad_resolve_zone_node(zone);
            int candZone = soundpad_get_entity_zone_id((int)node);
            node = soundpad_resolve_zone_node(rec[0x19e]);
            int curZone = soundpad_get_entity_zone_id((int)node);
            double dist = FUN_8027919c(curZone, candZone);
            score = (double)(float)((double)FLOAT_80619240 - dist);
        } else if (-1 < mode) {
            int flagged = tracker_other_player_flag_bit4(slot);
            int zone;
            if (flagged == 0) {
                zone = tracker_normalize_other_player_zone(slot);
            } else {
                int owner = tracker_find_owned_actor(slot);
                if (*rec == owner) {
                    return (double)FLOAT_80619210;
                }
                zone = *(int*)(*(int*)(owner + 0x7c) + 0x678);
            }
            int* node = soundpad_resolve_zone_node(zone);
            int candZone = soundpad_get_entity_zone_id((int)node);
            node = soundpad_resolve_zone_node(rec[0x19e]);
            int curZone = soundpad_get_entity_zone_id((int)node);
            double dist = FUN_8027919c(curZone, candZone);
            score = (double)(float)((double)FLOAT_80619240 - dist);
        }
    }
    return score;
}

// ----------------------------------------------------------------------------
// tracker_assign_combat_role 0x80278d6c
// Rebuilds this actor's combat-role slot. Tallies how many same-team actors are
// currently in each combat mode (local_58[mode]++), then walks the desired-role
// priority table (DAT_80472be0, keyed by team stat block DAT_806120cc) to find
// the highest-priority role whose quota (from the team's stat fields) is not yet
// filled by better-scoring teammates (compared via tracker_compute_zone_score).
// Writes the chosen mode back into the actor's combat state and re-runs the
// per-actor target-selection FSM.
// ----------------------------------------------------------------------------
void tracker_assign_combat_role(void)
{
    int* self = (int*)FUN_803826c4();
    int teamIdx = DAT_806120cc[self[0x21] * 4 + -2];
    int* selfState = FUN_80278fbc(self[2]);
    int census[22];
    memset_wrapper((int)census, 0, 0x10);

    int off = DAT_80612610 << 2;
    for (int i = DAT_80612610; i < DAT_80612098; i = i + 1) {
        int actor = *(int*)((int)&DAT_80588610 + off);
        if ((self[0x21] == *(int*)(actor + 0x84)) && (self[2] != *(int*)(actor + 8)) &&
            ((*(unsigned int*)(actor + 0x24) & 8) == 0)) {
            int* aState = FUN_80278fbc(*(int*)(actor + 8));
            census[*aState] = census[*aState] + 1;
        }
        off = off + 4;
    }

    int chosen;
    if (self[0x20] == 0) {
        int roleOff = 0;
        int quota = 0;
        chosen = 0;
        do {
            int prevChosen = chosen;
            chosen = *(int*)((int)&DAT_80472be0 + roleOff + teamIdx * 0x10);
            if (prevChosen == 2) {
                chosen = DAT_806120cc[self[0x21] * 4 + -3];
            } else if (prevChosen == 3) {
                chosen = DAT_806120cc[self[0x21] * 4 + -4];
            }
            quota = chosen;
            if (*(int*)((int)census + roleOff) < quota) {
                chosen = prevChosen;
                break;
            }
            if (prevChosen <= *selfState) {
                double selfScore = tracker_compute_zone_score(self, prevChosen);
                int better = 0;
                int scanOff = DAT_80612610 << 2;
                int idx = DAT_80612610;
                while ((idx < DAT_80612098) && (better < quota)) {
                    int* cand = *(int**)((int)&DAT_80588610 + scanOff);
                    if ((cand[0x21] == self[0x21]) && (self[2] != cand[2])) {
                        int* cState = FUN_80278fbc(cand[2]);
                        if (prevChosen == *cState) {
                            double candScore = tracker_compute_zone_score(cand, prevChosen);
                            if (selfScore <= candScore) {
                                better = better + 1;
                            }
                        }
                    }
                    idx = idx + 1;
                    scanOff = scanOff + 4;
                }
                if (better < quota) {
                    chosen = prevChosen;
                    break;
                }
            }
            chosen = prevChosen + 1;
            roleOff = roleOff + 4;
        } while (chosen < 4);
    } else {
        chosen = 1;
    }

    if (*selfState != chosen) {
        selfState[1] = DAT_80611928;
        *selfState = chosen;
    }
    tracker_update_target_selection();
    FUN_80382710();
}
