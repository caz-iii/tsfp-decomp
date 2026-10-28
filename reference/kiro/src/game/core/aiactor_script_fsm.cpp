// aiactor_script_fsm.cpp - AI actor scripted-behavior state machine + nav cost matrix.
//
// This is the core of the "scripted actor" AI: a large per-frame state machine
// (aiactor_script_behavior_tick, 0x80279374) that walks an actor's script node
// list and drives ~44 behavior states (idle, path-follow, combat approach,
// melee, cover, teleport, spawn triggers, carry/grab, etc.). It is supported by
// a small all-pairs zone navigation cost matrix used to score routes between
// the sound-pad zones, plus a handful of reset/allocation helpers.
//
//  tracker_zone_cost_row_ptr    (0x80278fbc): row pointer into the 12-byte-stride
//    cost record table for a zone index (only valid in nav mode 2).
//  tracker_costmatrix_mode_active(0x80278ff0): branchless test that the cost
//    matrix is in use (mode selector == 2).
//  tracker_costmatrix_build     (0x80279020): allocate an NxN float cost matrix
//    (N = active zone count) and fill each cell with the nav path cost between
//    the two zones (INF where unreachable).
//  tracker_costmatrix_get       (0x8027919c): read matrix[i][j] (INF if OOB/null).
//  tracker_costmatrix_set       (0x802791e0): write matrix[i][j].
//  tracker_worm_records_alloc   (0x80279210): zero the 4-slot worm-event record
//    array and allocate its 400-byte backing store.
//  tracker_clear_script_node_flags(0x80279264): clear the per-node 16-bit "fired"
//    flag (offset +0x3e, stride 0x48) across the script node list.
//  tracker_script_state_reset   (0x802792a8): reset all scripted-behavior global
//    state (mode, timers, zone gates, worm-record slots, cost matrix).
//  aiactor_script_behavior_tick (0x80279374): the main scripted-behavior FSM.
//  entity_handle_validate       (0x8027cd04): validate a packed entity handle,
//    zeroing it and returning 0 if the generation word no longer matches.
//
// Address range: 0x80278fbc - 0x8027cd4b

#include "common.h"

extern "C" {
// --- scalar globals -------------------------------------------------------
extern unsigned char DAT_805015bd;   // co-op / nav sub-mode selector (2 == cost-matrix mode)
extern unsigned char DAT_80501572;   // cost-matrix enabled flag
extern unsigned char DAT_805015bf;   // difficulty/aggression sub-mode flag

extern int   DAT_80611c18;   // active zone count (matrix dimension N)
extern int   DAT_80611c24;   // entity record table base
extern int   DAT_80611824;   // clip-class -> table base
extern int   DAT_80611b8c;   // entity pointer table base (stride 8)
extern int   DAT_80611d74;   // secondary player/actor record table base
extern int   DAT_80611d78;   // player record table base
extern int   DAT_8061208c;   // active script descriptor block
extern int   DAT_806120a4;   // (unused here) follow leader index
extern int   DAT_806120cc;   // zone descriptor table base (raw int)
extern int   DAT_806120d0;   // zone cost record table base
extern int   DAT_806120f8;   // "player is active target" sentinel
extern int   DAT_80612610;   // active player count
extern int   DAT_80612614;   // current game/level mode id
extern int   DAT_80610ba0;   // character class descriptor table base
extern int   DAT_806109c0;   // frame delta ticks (signed, xor bias)

extern int*  DAT_80612a18;   // cached leader actor record pointer
extern int   DAT_806129e0;   // cost matrix base (int* of row pointers)
extern int   DAT_805cbbd8;   // worm-event record backing store (400 bytes)
extern int   DAT_805cbb78;   // worm-event slot: record pointer (stride 8)
extern int   DAT_805cbb7c;   // worm-event slot: label id (stride 8)
extern int   DAT_805cbbb8;   // (cleared on reset)
extern int   DAT_805cbbc8;   // (cleared on reset)

extern int   DAT_80612a04;   // pending "A" zone-set: label
extern int   DAT_80612a00;   // pending "A" zone-set: zone 0
extern int   DAT_806129fc;   // pending "A" zone-set: zone 1
extern int   DAT_806129f8;   // pending "A" zone-set: zone 2
extern int   DAT_806129f4;   // pending "B" zone-set: label
extern int   DAT_806129f0;   // pending "B" zone-set: zone 0
extern int   DAT_806129ec;   // pending "B" zone-set: zone 1
extern int   DAT_806129e8;   // pending "B" zone-set: zone 2
extern int   DAT_80612a34;   // alert-group watch: active flag
extern int   DAT_80612a30;   // alert-group watch: group id
extern int   DAT_80612a2c;   // scripted look-at target A
extern int   DAT_80612a28;   // scripted look-at target B
extern int   DAT_80612a20;   // "entered state 0 / needs (re)spawn" flag
extern int   DAT_80612a14;   // saved script node index (trigger return)
extern int   DAT_80612a10;   // path-nav retry gate
extern int   DAT_80612a0c;   // scripted misc target
extern int   DAT_80612a08;   // scripted misc flag
extern float FLOAT_80612a24; // squared grab range

extern float FLOAT_806108ec; // per-frame time scale numerator

// --- float / double constants (rodata) ------------------------------------
extern const float  FLOAT_80619248; // 0.0 (matrix init / inf-seed)
extern const float  FLOAT_8061924c; // INF path-cost sentinel
extern const float  FLOAT_80619250; // INF matrix-read sentinel
extern const float  FLOAT_8061925c; // 0.0
extern const float  FLOAT_80619258; // default grab range
extern const float  FLOAT_80619260; // anim play rate 1.0
extern const float  FLOAT_80619264;
extern const float  FLOAT_80619268; // anim play rate
extern const float  FLOAT_8061926c;
extern const float  FLOAT_80619274; // zone-arrive radius sq
extern const float  FLOAT_80619278; // locomotion anim rate
extern const float  FLOAT_8061927c; // leader distance threshold
extern const float  FLOAT_80619280; // follow distance sq
extern const float  FLOAT_80619284; // repath timer threshold
extern const float  FLOAT_80619288; // crowd radius sq
extern const float  FLOAT_8061928c; // repath timer threshold B
extern const float  FLOAT_80619290; // near-leader radius sq
extern const float  FLOAT_80619294; // rng scale (bark chance)
extern const float  FLOAT_80619298; // rng denominator
extern const float  FLOAT_8061929c; // anim blend threshold
extern const float  FLOAT_806192a0; // rate/threshold
extern const float  FLOAT_806192a4; // cooldown base
extern const float  FLOAT_806192a8; // cooldown rng scale
extern const float  FLOAT_806192ac; // state timeout
extern const float  FLOAT_806192b0; // approach distance sq
extern const float  FLOAT_806192b4; // spring constant
extern const float  FLOAT_806192b8; // retarget hysteresis
extern const float  FLOAT_806192bc; // floor-miss sentinel
extern const float  FLOAT_806192c0; // floor step threshold
extern const float  FLOAT_806192c4; // combat distance sq
extern const double DOUBLE_806192c8; // frame-delta bias
extern const double DOUBLE_806192d0; // rng bias

// nav path helper output scratch (written by aiactor_nav_advance_path)
extern float FLOAT_80611ba0; // accumulated path length
extern float FLOAT_80611ba4; // accumulated path cost

// --- indirect-call jump tables (code pointers, raw bases) ------------------
extern int DAT_80472ed8;   // script "condition" handler table (stride 4)
extern int DAT_80472ef8;   // script "target select" handler table (stride 4)
extern int DAT_80472c10;   // clip-id table A (indexed by weapon posture)
extern int DAT_80472c30;   // clip-id table B
extern int DAT_804d34dc;   // class flags table
extern int DAT_804d1cd0;   // class -> flags-row index (short)
extern int DAT_8048ca94;   // floor probe X offset table (stride 0x30)
extern int DAT_8048ca9c;   // floor probe Z offset table (stride 0x30)
extern int DAT_80588610;   // active-player pointer array (stride 4)

// --- named cross-module helpers (still FUN_ in the IDB; typed to call site) -
extern int  FUN_803826c0(void);              // current actor record
extern void FUN_8038270c(void);

// batch_754 helpers defined in this file (used by the FSM above them)
void tracker_grab_attach_update(unsigned long long a, double b);
int  tracker_leader_pull_align(void);
int  tracker_relation_class_byte(int a, int b);
int  tracker_actor_flag_test(int rec, int p2, unsigned int mask3, unsigned int mask4);
int  tracker_debug_distance_probe(void);

// --- named subsystem helpers ----------------------------------------------
extern int  mem_alloc(int p1, int p2, int p3, int p4, int p5, int p6, int p7, int p8,
                      unsigned int size, int align, int p11, int p12, int p13, int p14,
                      int p15, int p16);
extern void memset_wrapper(int dst, int val, unsigned int n);
extern int  soundpad_get_entity_data(int zone, int* outList);
extern int  aiactor_nav_advance_path(int ctx, int* pathState);
extern int  aiactor_retarget_visible_teammate(void);
extern int  aiactor_alert_group_flagB_any(unsigned int group);
extern int  aiactor_follow_zone_path(void);
extern void aiactor_facing_update(void);
extern void aiactor_turn_toward_leader(void);
extern unsigned long long aiactor_turn_toward_entity(int actor, int target, int mode);
extern void aiactor_turn_toward_angle(double angle, int actor, int mode);
extern void aiactor_leader_zone_update(int actor);
extern unsigned long long aiactor_spawn_trigger_eval(void);
extern void aiactor_destroy_instance(double a, double b, double c, double d,
                                     unsigned long long e, unsigned long long f,
                                     unsigned long long g, unsigned long long h, int actor);
extern int  soundpad_find_zone_by_id(int id);
extern int  soundpad_find_nearest_entity(double a, float* pos);
extern int  soundpad_resolve_entity_to_zone(int id);
extern int  soundpad_zone_node_to_index(int* node);
extern int* soundpad_resolve_zone_node(int zoneIdx);
extern int  soundpad_get_entity_zone_id(int node);
extern int  worm_event_record_is_active(int rec);
extern int* worm_event_find_record_by_class(int cls);
extern void worm_set_pending_state_gated(int ctx, void* p2, int a, int b);
extern int  anim_primary_clip_userflags(int actor, int clipId);
extern int  anim_active_slot_userflags(int animCtx);
extern int  anim_is_clip_active(int actor, int clipId);
extern int  anim_clip_has_flags(int actor, void* clip, int a, int b);
extern int  anim_event_scan_active(void);
extern int  anim_play_scaled(double rate, double p2);
extern void anim_set_playback_rate(double rate, int* animCtx, int mode);
extern unsigned int rng_counter_tick(void);
extern double math_sqrt(double x);
extern int  entity_handle_resolve(unsigned int handle);
extern int  subobj_get_contact_origin(unsigned int sub);
extern void subobj_carry_nav_init(int actor, int a, int b, int c);
extern unsigned long long subobj_carry_nearest_grab_scan(void);
extern int  trigger_is_moving(int rec);
extern void soundpad_portal_activate(int portal);
extern unsigned int statlvl_class_is_state5(int cls);
extern void player_ai_teleport_wrapper(int* ctx, int zone, unsigned int flag);
extern void prop_hinge_apply_spring(double k, int actor, int propRec);
extern void object_melee_thrown_fire(unsigned long long f1, double d, int actor,
                                     int a, int b, int c);
extern double floor_query_with_interact(double x, double z, int posPtr, int a, int* out, int b);
extern unsigned int floor_height_from_position(double x, double z);
extern void level_get_current_id(void);
extern unsigned long long level_actor_check_active(void);

// --- batch_754 additional helpers/globals ---------------------------------
extern int  aiactor_relation_entry(int a, int b);          // -> ptr to relation byte record
extern void aiactor_dir_alignment_test(double thresh, float* posA, float* dir, float* posB);
extern int  worm_event_class_is_active(int cls);
extern int  subobj_carried_slot_value(int* leaderRec);
extern int  subobj_carry_nav_targets_valid(int subRec);
extern int  subobj_grab_slot_ring_advance(int subRec, unsigned int* cursor);
extern void subobj_grab_commit_attach(double a, double b, int subRec, int actor, unsigned int slot);
extern int  FUN_803826f0(void);   // current actor record (lock variant)
extern void FUN_8038273c(void);   // unlock/commit
extern int  FUN_803826dc(void);   // current actor record (lock variant)
extern void FUN_80382728(void);   // unlock/commit

extern int DAT_803e0568;  // carry offset template A (4 ints)
extern int DAT_803e056c;
extern int DAT_803e0570;
extern int DAT_803e0574;
extern float DAT_803e058c; // carry bbox min X
extern float DAT_803e0590; // carry bbox min Z
extern float DAT_803e0594; // carry bbox max X
extern float DAT_803e0598; // carry bbox max Z

extern const float FLOAT_806192e0; // min separation sq
extern const float FLOAT_806192e4; // alignment blend threshold
extern const float FLOAT_80619270; // pull spring bias

// --- batch_755 additional helpers/globals ---------------------------------
extern int  entity_tag_find_nearest(int tagId, float* pos, int mode);
extern int  entity_tag_pick_random(int tagId);
extern int  entity_tag_resolve_all(int tagId, int outBuf, int cap);
extern int  player_ai_is_valid_weapon_type(int weaponType);
extern void aiactor_set_stance_flags(int actor, int flags);
extern void anim_set_clip_speed_fields(double speed, int* animCtx);
extern double prop_hinge_wrap_angle_to_range(double angle, float* posA, float* posB);

extern unsigned long long FUN_803826d8(void); // current actor record (packed lock variant)
extern void FUN_80382724(void);               // unlock/commit
extern int  FUN_80364290(int actor, unsigned int mode); // weapon/posture validity probe

extern int   __save_gpr_28(void);    // context accessor: current actor record (r3)
extern void  __restore_gpr_28(void); // epilogue/commit counterpart
extern int   DAT_80611928;   // current frame timestamp (ticks)

extern int   DAT_80612098;   // active-player array element count
// per-clip-class anim clip id table, 0x24-byte rows; each column is its own symbol:
extern int   DAT_80472f10;   // col +0x00: idle clip
extern int   DAT_80472f14;   // col +0x04: walk clip
extern int   DAT_80472f18;   // col +0x08: run clip
extern int   DAT_80472f1c;   // col +0x0c: idle variant clip
extern int   DAT_80472f20;   // col +0x10: walk variant clip
extern int   DAT_80472f24;   // col +0x14: run variant clip

extern const float FLOAT_806192e8; // slow anim speed
extern const float FLOAT_806192ec; // turn-clip forward threshold
extern const float FLOAT_806192f0; // turn-clip deadzone threshold

// batch_755 helpers defined in this file (used by the FSM above + each other).
// Return types match how the (non-matching) FSM decompilation captured r3/r3:r4;
// the residual value is not semantically consumed, but keeping the type lets the
// existing call sites compile unchanged.
unsigned long long tracker_advance_zone_ring_cursor(int actor);
void tracker_resolve_current_zone_entity(int actor);
void tracker_find_nearest_tagged_in_zone(int actor);
void tracker_scan_nearest_hostile_actor(void);
void tracker_pick_random_tagged_in_zone(int actor);
int  tracker_collect_zone_targets(int actor, int selector, int tagId, int outBuf, int cap);
unsigned long long tracker_scan_nearest_relation_actor(void);
int  tracker_mode_to_clip_class(void);
void tracker_play_idle_variant_clip(void);
void tracker_play_locomotion_clip(void);
int  tracker_play_turn_clip(void);

// --- batch_756 additional helpers/globals ---------------------------------
extern int  FUN_803826e8(void);  // current actor record (lock variant, r3)
extern void FUN_80382734(void);  // unlock/commit

// per-clip-class table extra columns (0x24-byte rows, byte-addressed):
extern int  DAT_80472f28;   // col +0x18: crouch/special clip A
extern int  DAT_80472f2c;   // col +0x1c: crouch/special clip B
extern int  DAT_80472f30;   // col +0x20: crouch/special clip C

extern const float FLOAT_806192f4; // fallback anim scale divisor (when speed==0)

// batch_756 helpers defined in this file (forward decls)
void tracker_build_zone_index_list(void);                 // 0x8027e784
void tracker_play_crouch_special_clip(void);              // 0x8027e808
void tracker_bind_script_node_by_tagchar(int actor, char tagChar); // 0x8027e970
void tracker_seek_script_node_by_tag(int actor, int wantTag);      // 0x8027ea2c
int  tracker_coop_scan_partner_nodes(void);               // 0x8027ea98
unsigned int tracker_actor_coop_node_active(int actor);   // 0x8027eb3c
unsigned int tracker_level_node_is_coop_tag(int node);    // 0x8027eba0
double tracker_compute_anim_scale(int actor);             // 0x8027ecbc
int  tracker_handle_teleport_event(int actor, int event); // 0x8027edc4
int  tracker_count_crowd_neighbors(int actor, int count, int candArray); // 0x8027ef54

// --- batch_757 additional helpers/globals ---------------------------------
// packed context-lock intrinsics (return actor record in r3, packed count in
// r3:r4 for the packed variants); unlock/commit companions return void.
extern unsigned long long FUN_803826b8(void); // lock: (count<<0 | actor<<32)
extern unsigned long long FUN_803826cc(void);
extern unsigned long long FUN_803826d0(void);
extern void FUN_80382704(void);
extern void FUN_80382718(void);
extern void FUN_8038271c(void);

// named cross-module helpers used by batch_757 (file-local externs)
extern int  level_get_info_cached(int mode);
extern int  soundpad_collect_zone_entities(int zone, int* outList, int cap);
extern int  aiactor_run_behaviour_phase(int actor, int phase, int* p3, int p4);
extern int  anim_clip_flags_active_slot(int actor, int a, int b, int mask);
extern void player_ai_combat_behavior_tick(void);
extern void sound_init_play_params(int params);
extern void sound_play_request(int* params);
extern int  score_calc_player_total(int playerCtx);
extern int  player_ai_is_story_mode(int cls);
extern int  spawnpoint_count_active_for_zone(void);
extern void nav_advanced_movement_dispatch(double x, double z, int zoneWord, int posPtr,
                                           float* outPos, int* p6, int p7, int p8);
extern void aiactor_update_zone_anchor_position(void);
extern int  FUN_802b6d9c(void);   // combat/target refresh helper (used for its side effect)

// scalar globals
extern unsigned char DAT_805015c0;   // global gameplay flags (bit6 disables retarget)
extern unsigned char DAT_805015be;   // game mode id (0x13 == a story/coop variant)
extern int           DAT_805015dc;   // rank-based scoring enable flag

// rodata float/double constants used by the scorer
extern const float  FLOAT_806192f8;  // zone destination max range sq
extern const float  FLOAT_80619300;  // INF score sentinel
extern const float  FLOAT_80619304;  // skip-probability base
extern const float  FLOAT_80619308;  // skip-probability rng denom
extern const float  FLOAT_8061930c;  // low score weight
extern const float  FLOAT_80619310;  // current-target hysteresis mult
extern const float  FLOAT_80619314;  // recent-damage window (ticks)
extern const float  FLOAT_80619318;  // score weight base 1.0
extern const float  FLOAT_8061931c;  // rank-diff scale
extern const double DOUBLE_80619320; // rng bias
extern const double DOUBLE_80619328; // int->float conversion bias (0x43300000)
extern const float  FLOAT_80619330;  // stuck/arrival distance threshold sq
extern const float  FLOAT_80619334;  // combat-hold timer threshold

// batch_757 helpers defined in this file (forward decls)
unsigned long long tracker_pick_zone_destination(int a, void* b, int candArray); // 0x8027efe0
int  tracker_any_neighbor_crowding(int actor, int count, int candArray); // 0x8027f258
unsigned long long tracker_pick_nearest_party_target(int a, int b, int candArray); // 0x8027f2b4
unsigned int tracker_node_spawn_condition_met(int actor);             // 0x8027f468
void tracker_play_positional_sound(int a, int b, void* posv);        // 0x8027f4f8
double tracker_clamp(double v, double lo, double hi);                 // 0x8027f58c
void tracker_run_coop_behavior_phases(void);                          // 0x8027f5c0
void tracker_select_best_target_scored(void);                         // 0x8027f80c
void tracker_recover_to_zone_anchor(void);                            // 0x8027fd28
void tracker_arbitrate_behavior_mode(unsigned long long a, double b); // 0x8028001c

// --- batch_758 additional helpers/globals ---------------------------------
extern void character_death_begin_fadeout(int fxRec);
extern int  score_find_highest_player(void);
extern void character_death_effect_reset(unsigned int* fxRec);
extern int  aiactor_nav_advance_path_bounded(int leaderRec, int* io);
extern unsigned long long gx_present_and_advance(double a, double b, double c, unsigned long long d,
                                                 unsigned long long e, unsigned long long f,
                                                 unsigned long long g, unsigned long long h);
extern unsigned int FUN_8039e020(void);  // frame/zone-compute gate
extern int  player_ai_is_ranged_weapon(int weaponType);
extern void aiactor_set_relation_visible(int a, int b, int visible);

// cross-file target-tracker helpers (defined in aiactor_target_tracker.cpp)
extern int  tracker_handle_target_lost_event(int actor, int event);
extern int  tracker_event_steal_targets(int actor, int event);

// scalar globals + tables
extern int   DAT_80611910;   // current leader / spectated player record
extern int   DAT_80611d90;   // per-player death-fx record table base (stride 0xea0)
extern int   DAT_80611c14;   // active zone count (objective-table dimension)
extern int   DAT_80612374;   // amortized zone-compute in-progress flag
extern int   DAT_80612100;   // spawnpoint list count
extern int   DAT_80611b9c;   // per-zone nearest-objective table base (stride 0x2c)
extern int   DAT_806120b4;   // pickup list count
extern int   DAT_806120ac;   // objective list count
extern int   DAT_80588a9c;   // spawnpoint zone-id list (stride 8)
extern int   DAT_80588aa0;   // spawnpoint entity list (stride 8, +4 of DAT_80588a9c)
extern int   DAT_805887e4;   // pickup zone-id list (stride 8)
extern int   DAT_80588924;   // objective zone-id list (stride 8)
extern int   DAT_80588928;   // objective entity list (stride 8, +4 of DAT_80588924)

extern const float FLOAT_80619338; // stationary movement threshold sq
extern const float FLOAT_8061933c; // melee-fidget clip id (as float-cast constant)
extern const float FLOAT_80619340; // fidget replay rate base
extern const float FLOAT_80619344; // event-4 sound pitch scale
extern const float FLOAT_80619348; // floor-probe reference height

// batch_758 helpers defined in this file (forward decls)
void   tracker_reassign_leader_on_death(int* param_1);       // 0x80280594
double tracker_zone_path_distance(int fromZone, int toZone); // 0x80280684
void   tracker_precompute_zone_objectives(double p2, double p3, unsigned long long p4,
                                          unsigned long long p5, unsigned long long p6,
                                          unsigned long long p7, unsigned long long p8); // 0x802807bc
void   tracker_update_stationary_timer(int* param_1);        // 0x80280b94
int    tracker_stub_zero_a(void);                            // 0x80280c7c
int    tracker_stub_zero_b(void);                            // 0x80280c84
void   tracker_play_melee_fidget_clip(int actor);            // 0x80280c8c
int    tracker_coop_script_event_dispatch(int actor, int event); // 0x80280e50
void   tracker_clear_player_visibility_relations(int event, int actor); // 0x80280f8c
void   tracker_coop_ground_phase_handler(int phase, int actor); // 0x80281010

// forward references within this file
int tracker_costmatrix_set(double value, int i, int j);
int entity_handle_validate(unsigned int* pHandle);

// --- batch_759 additional helpers/globals ---------------------------------
// Co-op script-event handlers (0x80281150-0x80281427) + the two big co-op
// dispatch switches (event 0x802814a0, per-state tick 0x80281da4) and their
// supporting helpers. Types match the (non-matching) call sites.

// rodata float constants used by this batch
extern const float FLOAT_8061934c; // held-object throw field: low value
extern const float FLOAT_80619350; // held-object throw field: high value
extern const float FLOAT_80619354; // mount attack/turn pose wrap divisor

// data tables
extern int DAT_8043e23c;   // per-slot class-index table (stride 0x3c, 0x45 rows)

// named cross-module helpers (still FUN_ in the IDB; typed to call site)
extern void object_model_swap_in(int object);
extern int  aiactor_mount_attack_clip_active(void);
extern int  aiactor_mount_turn_clip_active(void);
extern double anim_active_curve_value(int actor);
extern int  player_ai_is_sniper_weapon(int weaponType);
extern unsigned int player_ai_char_def_get_flags(int defRec);
extern int  match_slot_field_ptr(int base, int field);
extern void aiactor_movement_tick(unsigned long long a, double b, double c, double d,
                                  double e, double f, double g, double h);
extern void aiactor_play_turn_animation(int actor);
extern unsigned long long projectile_init_aim_state(int actor);
extern void aiactor_behavior_alert_sound(void);
extern void aiactor_behavior_event_handler(int actor, int event, int* p3);
extern void aiactor_mount_death_dispatch(int actor, int event, int* p3);
extern void aiactor_behavior_reaction_gate(int actor, int event);
extern void aiactor_mount_event_handler(int actor, int event, int p3);
extern void tracker_query_mode4_event(int actor, int event);
extern void aiactor_behavior_follow_tick(void);
extern void aiactor_behavior_creature_tick(unsigned long long a, double b, double c, double d,
                                           double e, double f, double g, double h, int i);
extern void aiactor_behavior_combat_tick(void);
extern void aiactor_spawn_into_world(unsigned long long a, double b, double c, double d,
                                     double e, double f, double g, double h);
extern void aiactor_mount_follow_tick(void);
extern void aiactor_idle_or_follow_tick(int actor);
extern void aiactor_mounted_weapon_tick(unsigned long long a, double b, double c, double d,
                                        double e, double f, double g, double h);
extern void aiactor_follow_companion_tick(void);
extern void aiactor_behavior_guard_tick(void);
extern void aiactor_mount_clip_gate_tick(int actor);
extern void aiactor_follow_zone_nav_tick(unsigned long long a, double b, double c, double d,
                                         double e, double f, double g, double h, int i);
extern void aiactor_update_leader_and_state(int actor);

// raw FUN_ symbols referenced by this batch (typed to call site)
extern unsigned long long FUN_803826e4(void);   // lock: (count<<0 | actor<<32)
extern void FUN_80382730(void);                  // unlock/commit
extern double FUN_80390324(double x);            // fractional/round helper
extern unsigned long long FUN_8038b4cc(int p1, int p2, int p3); // matrix stack push
extern void FUN_802c4910(int actor, int event);
extern void FUN_8028a4c0(int actor, int event, int p3);
extern void FUN_802acf54(int actor, int event, int p3);
extern void FUN_80287e8c(int actor, int event);
extern void FUN_80382228(int actor, int event);
extern void FUN_8028ae00(int actor, int event, int p3);
extern void FUN_8029aae4(void);
extern void FUN_80296058(int actor, int event, int p3);
extern void FUN_802a44d0(int actor, int event);
extern void FUN_8029cd94(int actor, int event, int p3);
extern void FUN_802898d0(void);
extern void FUN_802b90a4(void);
extern void FUN_802b86dc(void);
extern void FUN_802b798c(unsigned long long a, double b, double c, double d, double e,
                         double f, double g, double h);
extern void FUN_802b4860(void);
extern void FUN_802b61cc(void);
extern void FUN_802bc414(void);
extern void aiactor_behavior_guard_tick(void);      // (was FUN_802c1730)
extern void FUN_802bc654(void);
extern void aiactor_behavior_investigate_tick(void); // (was FUN_802c22d0)
extern void aiactor_behavior_wander_tick(int actor); // (was FUN_802c1ec4)

// batch_759 helpers defined in this file (forward decls)
int  tracker_coop_ground_reground_event(int param_1, int param_2);   // 0x80281150
int  tracker_event_steal_or_mount_pose(int param_1, int param_2);    // 0x8028124c
int  tracker_event_force_ragdoll(int param_1, int param_2);          // 0x80281388
int  tracker_event_request_reground(int param_1, int param_2);       // 0x802813c0
int  tracker_event_steal_idle_clip(int param_1, int param_2);        // 0x80281428
void tracker_coop_event_dispatch(int p1, int p2, int* p3);           // 0x802814a0
int  tracker_should_retarget(int actor, int classIdx);               // 0x80281b50
void tracker_behavior_tick_router(unsigned long long a, double b, double c, double d,
                                  double e, double f, double g, double h, int actor,
                                  int inR8, int inR9, int inR10);     // 0x80281c64
int  tracker_scan_class_flag400(void);                               // 0x80281ce8
void tracker_behavior_state_dispatch(unsigned long long a, double b, double c, double d,
                                     double e, double f, double g, double h); // 0x80281da4

// --- batch_760 additional helpers/globals ---------------------------------
// The two big per-state behaviour tick dispatchers (single-player 0x80282570 /
// secondary-state 0x8028334c) that batch_759's tracker_behavior_tick_router
// routes to, plus the threat-scan / creature-melee / nearest-target /
// death-sequence / sub-object impact handlers (0x80283878-0x8028511f).

// packed context-lock intrinsics (return actor record; packed variants pack
// count<<0 | actor<<32). Unlock/commit companions return void.
extern unsigned long long FUN_803826d4(void);   // lock (packed): sub-object impact
extern void FUN_80382720(void);                  // unlock/commit

// rodata float constants used by this batch
extern const float FLOAT_80619358; // threat-scan acquire distance sq
extern const float FLOAT_8061935c; // creature turn/attack blend threshold
extern const float FLOAT_80619360; // creature reground distance threshold
extern const float FLOAT_80619364; // facing-gate distance sq
extern const float FLOAT_80619368; // death get-up reset field value
extern const float FLOAT_8061936c; // fall spring gravity scale
extern const float FLOAT_80619370; // fall velocity clamp
extern const float FLOAT_80619374; // death anim playback rate
extern const float FLOAT_80619378; // impact scale (grabbable)
extern const float FLOAT_8061937c; // impact scale (default)
extern const float FLOAT_80619380; // impact damage radius / param A
extern const float FLOAT_80619384; // impact damage amount / param B

// data tables
extern int DAT_80611b1c;   // clip-id -> ground-usable flag byte table

// named cross-module helpers (still FUN_ in the IDB; typed to call site)
extern int  aiactor_aim_alignment_test(double thresh, int target, float* selfPos);
extern int  aiactor_facing_dot_test(double thresh, int self, float* targetPos);
extern void aiactor_melee_swipe(int actor, int a, int b);
extern void aiactor_play_alert_voice(int entity, int mode);
extern void aiactor_alert_group_arm(unsigned int group);
extern int  aiactor_acquire_nearest_target(int actor, int targetList, int mode);
extern void aiactor_mount_retarget_zone_event(int target, int actor);
extern double anim_primary_clip_phase(int animCtx);
extern unsigned long long projectile_update_surface_aim(int actor, int mode);
extern int  aiactor_squad_contains_member(int memberZone, int mode);
extern int  anim_entity_has_data(int actor);
extern int  anim_event_has_passed(int actor, int eventMask, int count);
extern void player_ai_drop_all_weapons(unsigned long long a, double b, double c, double d,
                                       double e, double f, double g, double h, int* self);
extern int  game_is_standard_arcade(void);
extern void projectile_spawn(unsigned long long a, double b, double c, double d,
                             double e, double f, double g, double h);
extern int  anim_clip_field_b08(int actor);
extern unsigned long long anim_start_clip(double clipId, double rate);
extern void entity_register_for_update(int actor, int a, int b);
extern unsigned long long worm_map100_arcade_hook(int actor);
extern void player_ai_projectile_death_dispatch(void);
extern unsigned int subobj_get_flags(unsigned int sub);
extern unsigned int subobj_is_idle(unsigned int sub);
extern double subobj_get_inverse_mass(unsigned int sub);
extern unsigned int nav_movement_dispatch(double xoff, double zoff, int zoneWord, int posPtr,
                                          float* outPos, float* p6, int* scratch, int a, int b);
extern void character_on_death_handler(double a, double b, double c, double d,
                                       unsigned long long e, unsigned long long f,
                                       unsigned long long g, unsigned long long h);
extern void damage_dispatch_to_entity(double a, double b, double c, double d,
                                      unsigned long long e, unsigned long long f,
                                      unsigned long long g, unsigned long long h, int* dmgRec);
extern void switch_slot_reset(int slotPtr);
extern void subobj_find_active_grab(int sub, int* outHandle);

// raw FUN_ symbols referenced by this batch (typed to call site)
extern void FUN_802aa650(void);
extern void FUN_802a4744(unsigned long long a, double b, double c, double d,
                         double e, double f, double g, double h);
extern void FUN_802a59f0(unsigned long long a, double b, double c, double d,
                         double e, double f, double g, double h);
extern void FUN_802a8610(void);
extern void FUN_802a754c(int actor);
extern void aiactor_behavior_teleport_in_tick(unsigned long long a, double b, double c, double d, double e,
                         double f, double g, double h, int actor); // (was FUN_802c3b38)
extern void aiactor_behavior_alerted_tick(void); // (was FUN_802c35b0)
extern void FUN_802c3e10(void);
extern void FUN_8028cc34(int actor);
extern void FUN_80289574(int actor);
extern void FUN_80287f3c(void);
extern void FUN_802873bc(void);
extern void FUN_8028b08c(unsigned long long a, double b, double c, double d,
                         double e, double f, double g, double h);
extern void FUN_802882c4(unsigned long long a, double b, double c, double d,
                         double e, double f, double g, double h);
extern void FUN_80380e2c(void);
extern void FUN_802a14c8(unsigned long long a, double b, double c, double d,
                         double e, double f, double g, double h);
extern void FUN_80296024(void);
extern void FUN_8029be80(void);
extern void FUN_80298904(void);
extern void FUN_80292cb4(int actor);
extern void FUN_802bb580(int actor);
extern void aiactor_behavior_goto_marker_tick(void); // (was FUN_802be4d4)
extern void FUN_802bd884(unsigned long long a, double b, double c, double d, double e,
                         double f, double g, double h, int actor);
extern unsigned long long FUN_802bd198(void);
extern void FUN_802bc840(void);
extern void FUN_802bdaf8(unsigned long long a, double b, double c, double d, double e,
                         double f, double g, double h, int actor);
extern void FUN_802baa58(void);
extern void FUN_802bb87c(int actor);
extern void aiactor_behavior_boss_scripted_tick(void); // (was FUN_802be860)
extern void aiactor_behavior_worm_boss_tick(void);     // (was FUN_802c01d8)
extern void aiactor_behavior_patrol_pingpong_tick(int actor); // (was FUN_802c1c70)
extern int  FUN_8036424c(int self);
extern int  aiactor_all_zone_targets_inactive(void); // (was FUN_802bdf68)
extern int  FUN_80364c40(int* p);
extern void FUN_803652bc(int actor, int grab);
extern void FUN_80364cdc(void);
extern unsigned int FUN_80365914(int grab);
extern int  FUN_80365694(void);
extern int  FUN_80364808(unsigned long long a, double b, double c, double d, double e,
                         double f, double g, double h, int actor, int slot, unsigned int flags);
extern unsigned long long FUN_803656c4(int slot);
extern void FUN_8037ee2c(void);
extern void FUN_803654f0(void);
extern void FUN_8037ed6c(void);

// named handlers routed by the single-player state dispatcher (fn#1)
extern void aiactor_behavior_prop_ride_tick(int actor);
extern void aiactor_path_script_tick(unsigned long long a, double b, double c, double d,
                                     double e, double f, double g, double h);
extern void aiactor_scripted_update(unsigned long long a, double b, double c, double d,
                                    double e, double f, double g, double h);
extern void aiactor_death_anim_tick(unsigned long long a, double b, double c, double d,
                                    double e, double f, double g, double h, int actor);
extern void aiactor_behavior_fire_gate(int actor);
extern void aiactor_behavior_patrol_tick(void);
extern unsigned long long aiactor_facing_update_r(void);
extern void aiactor_mount_behavior_main_tick(unsigned long long a, double b, double c, double d,
                                             double e, double f, double g, double h);
extern void aiactor_behavior_search_tick(int actor);
extern void aiactor_behavior_engage_tick(void);
extern void aiactor_behavior_vehicle_state_tick(unsigned long long a, double b, double c, double d,
                                                double e, double f, double g, double h, int actor);
extern void aiactor_mount_spawn_tick(void);
extern void aiactor_behavior_mounted_gun_tick(unsigned long long a, double b, double c, double d,
                                              double e, double f, double g, double h);
extern void aiactor_behavior_vehicle_ride_tick(void);
extern void aiactor_mount_main_tick(unsigned long long a, double b, double c, double d, double e,
                                    double f, double g, double h, int actor, int r4, float* p11,
                                    int p12, int p13, int p14, int p15, int p16);
extern void aiactor_behavior_belt_ride_tick(void);
extern void aiactor_behavior_scripted_death_tick(unsigned long long a, double b, double c, double d,
                                                 double e, double f, double g, double h, int actor);
extern void aiactor_behavior_reaction_tick(int actor);
extern void aiactor_update_aim_height(void);
extern void aiactor_kamikaze_tick(int actor);
extern void aiactor_turret_creature_tick(void);
extern void aiactor_behavior_ride_idle_tick(int actor);
extern void aiactor_mount_patrol_zone_tick(unsigned long long a, double b, double c, double d,
                                           double e, double f, double g, double h);
extern void aiactor_behavior_investigate_tick(void);

// named handlers routed by the secondary-state dispatcher (fn#2)
extern void tracker_bot_behavior_tick_variant(void);
extern void tracker_bot_behavior_tick(void);
extern void tracker_owner_follow_update(void);
extern void tracker_ally_behavior_update(void);
extern void tracker_combat_behavior_update(void);

// batch_760 helpers defined in this file (forward decls)
void tracker_behavior_state_dispatch_sp(unsigned long long a, double b, double c, double d,
                                        double e, double f, double g, double h, int actor,
                                        int p10, int p11, int stateWord, int p13,
                                        int inR4, int p15, int p16);          // 0x80282570
void tracker_behavior_state_dispatch_alt(unsigned long long a, double b, double c, double d,
                                         double e, double f, double g, double h, int actor); // 0x8028334c
void tracker_scan_and_alert_threats(void);                                    // 0x80283878
void tracker_creature_melee_tick(void);                                       // 0x80283b90
double tracker_nearest_target_distance(int a, int b, int mode);               // 0x80283f34
double tracker_nearest_target_by_handle(int a, int b);                        // 0x80284220
double tracker_nearest_target_by_tag(int a, int b);                           // 0x80284244
void tracker_death_sequence_tick(unsigned long long a, double b, double c, double d,
                                 double e, double f, double g, double h);      // 0x80284268
void tracker_death_finalize(unsigned long long a, double b, double c, double d,
                            unsigned long long e, unsigned long long f,
                            unsigned long long g, unsigned long long h, int actor); // 0x80284ba4
void tracker_subobj_impact_push(unsigned long long a, double b, double c, double d,
                                unsigned long long e, unsigned long long f,
                                unsigned long long g, unsigned long long h,
                                int p9, int p10, float* dir, int p12);         // 0x80284ce0


// --- batch_761 additional helpers/globals ---------------------------------
// Low-level AI behaviour helpers (0x80285120-0x802873bb): turn-rate integrator,
// alert-state request, movement-progress probe, special-class predicate, idle /
// fidget anim selectors, and the melee / special-creature / boss-creature
// per-frame behaviour ticks (the leaf handlers routed to by the batch_760
// state dispatchers).

// rodata float constants (rodata block continues 0x80619388-0x80619424)
extern const float FLOAT_80619388; // move-progress sample gate
extern const float FLOAT_8061938c; // reach radius (crouch/special)
extern const float FLOAT_80619390; // reach radius (default)
extern const float FLOAT_80619398; // anim play clip-id base
extern const float FLOAT_8061939c; // 1.0 / unit weight
extern const float FLOAT_806193a0; // rng denominator
extern const float FLOAT_806193a4; // rng/blend scale A
extern const float FLOAT_806193a8; // rng/blend scale B (0.5-ish)
extern const float FLOAT_806193b8; // 0.0 (reset value)
extern const float FLOAT_806193bc; // story-mode range mult
extern const float FLOAT_806193c0; // anim rate lo threshold
extern const float FLOAT_806193c4; // anim rate hi threshold
extern const float FLOAT_806193c8; // retarget distance threshold
extern const float FLOAT_806193d8; // acquire cooldown base
extern const float FLOAT_806193dc; // approach cooldown base
extern const float FLOAT_806193e0; // rsqrt newton const (1.5)
extern const float FLOAT_806193e4; // attack anim phase gate
extern const float FLOAT_806193e8; // muzzle rotation numerator
extern const float FLOAT_806193ec; // muzzle rotation denominator
extern const float FLOAT_806193f0; // min-length sq (normalize guard)
extern const float FLOAT_806193f4; // beam step fraction
extern const float FLOAT_806193f8; // beam back-offset fraction
extern const float FLOAT_80619400; // spark scale
extern const float FLOAT_80619404; // beam damage amount
extern const float FLOAT_80619408; // boss anim rate / scale A
extern const float FLOAT_8061940c; // boss spawn cooldown base
extern const float FLOAT_80619410; // boss chase anim rate
extern const float FLOAT_80619414; // boss retreat distance gate
extern const float FLOAT_80619418; // boss attack cooldown base
extern const float FLOAT_8061941c; // boss recover cooldown base
extern const float FLOAT_80619420; // reaction anim rate
extern const float FLOAT_80619424; // boss retreat anim rate

extern const double DOUBLE_806193b0; // rng int->float bias (0x43300000)
extern const double DOUBLE_806193d0; // frame-delta int->float bias

// data tables
extern int DAT_8048caa0;   // floor-probe Z+4 offset column (stride 0x30, paired with DAT_8048ca9c)
extern int damage_filter_scaled_melee_immune; // render/damage-filter type constant (passed by value)

// named cross-module helpers (still FUN_ in the IDB; typed to call site)
extern int  anim_primary_clip_has_flags(int actor, int a, int b, int mask);
extern int  anim_secondary_clip_has_flags(int actor, int a, int b, int mask);
extern int  spawn_pack_speed_changed(int leader);
extern unsigned int aiactor_target_zone_reachable(int self);
extern int  object_resolve_group_base_slot(int cls);
extern int  anim_active_clip_slot(int self);
extern double anim_active_clip_phase(int self);
extern void mtx4_set_translate_rotz(double x, double y, double z, double angle, float* outMtx);
extern void mtx4_transform_point(float* mtx, float* pt);
extern int  scene_raycast(int space, float* from, float* dir, int a, float* hitOut,
                          float* b, int* c, int mask, int d, int* e);
extern void spark_spawn_fading(void);
extern void spark_set_last_scale(double s);
extern void character_set_damage_bone(int self, int bone);
extern void entity_set_render_type(int actor, int type);
extern int  screen_effect_alloc_slot(void);
extern void screen_effect_init_slot(int slot, int actor);

// batch_761 helpers defined in this file (forward decls)
void tracker_integrate_turn_rate(int actor);                                  // 0x80285120
void tracker_request_alert_state(int actor);                                  // 0x8028521c
void tracker_movement_progress_check(void);                                   // 0x802852c4
int  tracker_is_special_class_eligible(int actor);                            // 0x80285478
void tracker_special_anim_gate(int actor);                                    // 0x80285528
void tracker_play_idle_fidget_anim(int actor);                                // 0x802855a0
void tracker_melee_combat_tick(void);                                         // 0x80285788
void tracker_creature_special_tick(void);                                     // 0x80286040
void tracker_creature_boss_tick(unsigned long long a, double b, double c,
                                unsigned long long d, unsigned long long e,
                                unsigned long long f, unsigned long long g,
                                unsigned long long h, int actor);             // 0x80286a04
int  tracker_event_react_or_pass(int actor, int event);                       // 0x80287298

}

// ---------------------------------------------------------------------------
// 0x80278fbc - cost record row pointer for a zone (nav mode 2 only).
int tracker_zone_cost_row_ptr(int zoneIndex)
{
    if (DAT_805015bd != '\x02') {
        return 0;
    }
    return DAT_806120d0 + (zoneIndex - DAT_80612610) * 0xc;
}

// ---------------------------------------------------------------------------
// 0x80278ff0 - branchless "cost matrix in use" test (returns 0/1).
unsigned int tracker_costmatrix_mode_active(void)
{
    unsigned int lead;

    if (DAT_80501572 == '\0') {
        return 0;
    }
    lead = countLeadingZeros(2 - (unsigned int)DAT_805015bd);
    return lead >> 5;
}

// ---------------------------------------------------------------------------
// 0x80279020 - build the NxN zone navigation cost matrix.
void tracker_costmatrix_build(int p1, int p2, int p3, int p4, int p5, int p6, int p7, int p8,
                              int p11, int p12, int p13, int p14, int p15, int p16)
{
    unsigned int bytes;
    int base;
    int rowStart;
    int i;
    int j;
    int off;
    int* entA;
    int* entB;
    int idA;
    int idB;
    int path;

    bytes = (unsigned int)((DAT_80611c18 + DAT_80611c18 * DAT_80611c18) * 4);
    base = mem_alloc(p1, p2, p3, p4, p5, p6, p7, p8,
                     (bytes + 0xf) & 0xfffffff0, 1, p11, p12, p13, p14, p15, p16);
    memset_wrapper(base, 0, bytes);

    off = 0;
    rowStart = base + DAT_80611c18 * 4;
    DAT_806129e0 = base;
    for (i = 0; i < DAT_80611c18; i = i + 1) {
        *(int*)(DAT_806129e0 + off) = rowStart;
        off = off + 4;
        rowStart = rowStart + DAT_80611c18 * 4;
    }

    for (i = 0; i < DAT_80611c18; i = i + 1) {
        soundpad_get_entity_data(i, (int*)&entA);
        idA = *entA;
        tracker_costmatrix_set((double)FLOAT_80619248, i, i);
        for (j = 0; j < DAT_80611c18; j = j + 1) {
            if (i != j) {
                soundpad_get_entity_data(j, (int*)&entB);
                idB = *entB;
                (void)idA;
                (void)idB;
                FLOAT_80611ba0 = FLOAT_80619248;
                FLOAT_80611ba4 = FLOAT_80619248;
                path = aiactor_nav_advance_path(*(int*)(DAT_80611d78 + 0x14), &idA);
                if (path < 0) {
                    tracker_costmatrix_set((double)FLOAT_8061924c, i, j);
                }
                else {
                    tracker_costmatrix_set(
                        (double)(FLOAT_80611ba4 + FLOAT_80611ba0), i, j);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x8027919c - read cost matrix cell (INF sentinel if OOB or not built).
double tracker_costmatrix_get(int i, int j)
{
    if ((i < DAT_80611c18) && (j < DAT_80611c18)) {
        if (DAT_806129e0 != 0) {
            return (double)*(float*)(*(int*)(DAT_806129e0 + i * 4) + j * 4);
        }
        return (double)FLOAT_80619250;
    }
    return (double)FLOAT_80619250;
}

// ---------------------------------------------------------------------------
// 0x802791e0 - write cost matrix cell (no-op if OOB).
int tracker_costmatrix_set(double value, int i, int j)
{
    if (DAT_80611c18 <= i) {
        return 0;
    }
    if (DAT_80611c18 <= j) {
        return 0;
    }
    *(float*)(*(int*)(DAT_806129e0 + i * 4) + j * 4) = (float)value;
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80279210 - allocate + zero the 4-slot worm-event record array.
void tracker_worm_records_alloc(int p1, int p2, int p3, int p4, int p5, int p6, int p7, int p8)
{
    int off;
    int count;

    off = 0;
    count = 4;
    do {
        *(int*)((int)&DAT_805cbbd8 + off) = 0;
        off = off + 4;
        count = count - 1;
    } while (count != 0);

    DAT_805cbbd8 = mem_alloc(p1, p2, p3, p4, p5, p6, p7, p8, 400, 1, 0, 0, 0, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// 0x80279264 - clear the per-node 16-bit "fired" flag across script nodes.
void tracker_clear_script_node_flags(void)
{
    int off;
    int nodeOff;
    int i;

    if (DAT_8061208c == 0) {
        return;
    }
    off = 0;
    for (i = 0; i < *(int*)(DAT_8061208c + 0x40); i = i + 1) {
        nodeOff = off + 0x3e;
        off = off + 0x48;
        *(unsigned short*)(*(int*)(DAT_8061208c + 0x3c) + nodeOff) = 0;
    }
}

// ---------------------------------------------------------------------------
// 0x802792a8 - reset all scripted-behavior global state.
void tracker_script_state_reset(void)
{
    int off;
    int count;

    DAT_80612a20 = 1;
    DAT_80612a34 = 0;
    DAT_80612a30 = -1;
    DAT_80612a10 = 0;
    DAT_80612a2c = -1;
    DAT_80612a28 = -1;
    DAT_80612a0c = -1;
    FLOAT_80612a24 = FLOAT_80619258;
    DAT_80612a08 = 0;
    DAT_80612a04 = -1;
    DAT_80612a00 = -1;
    DAT_806129fc = -1;
    DAT_806129f8 = -1;
    DAT_806129f4 = -1;
    DAT_806129f0 = -1;
    DAT_806129ec = -1;
    DAT_806129e8 = -1;

    count = 8;
    off = 0;
    do {
        *(int*)((int)&DAT_805cbb78 + off) = 0;
        *(int*)((int)&DAT_805cbb7c + off) = -1;
        count = count - 1;
        off = off + 8;
    } while (count != 0);

    DAT_805cbbc8 = 0;
    DAT_805cbbb8 = 0;
    if (DAT_805cbbd8 != 0) {
        memset_wrapper(DAT_805cbbd8, 0, 400);
    }
}

// ---------------------------------------------------------------------------
// 0x8027cd04 - validate a packed entity handle (generation check).
int entity_handle_validate(unsigned int* pHandle)
{
    int rec;
    unsigned int handle;

    handle = *pHandle;
    if (handle == 0) {
        return 0;
    }
    rec = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
    if (*(unsigned int*)(rec + 0x2c) != handle) {
        *pHandle = 0;
        return 0;
    }
    return rec;
}

// ---------------------------------------------------------------------------
// 0x80279374 - the main scripted-behavior state machine.
//
// Runs once per frame for the "scripted actor" driver. Fetches the current
// actor record, resolves the active script node, then dispatches on the actor's
// behavior-state field (piVar23[8]) through ~44 states covering path following,
// combat approach, melee, cover, spawn triggers, teleport, carry/grab and the
// scripted look-at/zone-set opcodes. On each transition it records the previous
// state (piVar23[0x275]) and resets the transition timer (piVar23[0x276]).
//
// Register-carried scratch values from the original (extraout_r4 / in_fN) are
// modelled as explicit locals; this is a faithful non-matching translation.
void aiactor_script_behavior_tick(void)
{
    float fTmp1;
    float fTmp2;
    float fTmp3;
    float fTmp4;
    unsigned char postureSel;
    int actor;
    int leaderRec;
    int node;
    int i;
    int j;
    int k;
    int a;
    int b;
    int idx;
    bool cond;
    float* pf;
    int* pNode;
    void* r4scratch = 0;      // decompiler extraout_r4 register artifact
    unsigned int uTmp;
    int* self;
    unsigned long long ret64;
    double dRet;
    double d1;
    double d2;
    double d3;
    double distSq;

    // incoming FP register values (pass-through / stored); real values unknown.
    unsigned long long in_f5 = 0, in_f6 = 0, in_f7 = 0, in_f8 = 0;
    double in_f30 = 0.0, in_f31 = 0.0, in_ps30_1 = 0.0, in_ps31_1 = 0.0;

    double local_78;
    unsigned long long local_70;
    float local_stackA[11];      // auStack_a4 path buffer
    float local_cc[10];
    unsigned char pathBuf0[40];  // auStack_f4
    unsigned char pathBuf1[40];  // auStack_11c
    unsigned char pathBuf2[40];  // auStack_144
    unsigned char pathBuf3[40];  // auStack_16c
    unsigned char pathBuf4[40];  // auStack_194
    unsigned char pathBuf5[40];  // auStack_1bc
    unsigned char pathBuf6[40];  // auStack_1e4
    int local_1e8;

    float loc8  = (float)in_f31;
    float loc4  = (float)in_ps31_1;
    float loc18 = (float)in_f30;
    float loc14 = (float)in_ps30_1;
    (void)loc8; (void)loc4; (void)loc18; (void)loc14;

    actor = FUN_803826c0();
    self = *(int**)(actor + 0x7c);
    leaderRec = aiactor_retarget_visible_teammate();

    if (-1 < self[0x274]) {
        if (self[0x274] != self[8]) {
            self[0x276] = (int)FLOAT_8061925c;
            self[0x275] = self[8];
            self[8] = self[0x274];
        }
        self[0x274] = -1;
    }

    if ((self[0xb] & 0x80U) == 0) {
        self[0x2a4] = (int)FLOAT_8061925c;
    }
    else {
        local_78 = (double)(long long)DAT_806109c0;
        self[0x2a4] = (int)(FLOAT_806108ec *
            (FLOAT_80619260 / (float)(local_78 - DOUBLE_806192c8)) + (float)self[0x2a4]);
    }

    if ((self[0xb] & 4U) != 0) goto LAB_exit;

    DAT_80612a18 = *(int**)(*(int*)(DAT_80611d78 + 0x14) + 0x7c);
    {
        int other = *DAT_80612a18;
        d3 = (double)(*(float*)(actor + 100) - *(float*)(other + 100));
        d2 = (double)(*(float*)(actor + 0x68) - *(float*)(other + 0x68));
        fTmp1 = *(float*)(actor + 0x6c) - *(float*)(other + 0x6c);
        d1 = (double)(fTmp1 * fTmp1);
        distSq = (double)(float)(d1 + (double)((float)(d3 * d3) + (float)(d2 * d2)));
    }

    if (((DAT_80612a04 != -1) || (DAT_806129f4 != -1)) &&
        (i = 0, b = DAT_80612610, 0 < DAT_80612610)) {
        do {
            idx = *(int*)(*(int*)((int)&DAT_80588610 + i) + 0x678);
            if (((idx == DAT_80612a00) || (idx == DAT_806129fc)) || (idx == DAT_806129f8)) {
                self[0x27b] = DAT_80612a04;
                if (self[8] != 1) {
                    self[0x275] = self[8];
                    self[8] = 1;
                    self[0x276] = (int)FLOAT_8061925c;
                    self[0x274] = self[8];
                }
                DAT_80612a04 = -1; DAT_806129f8 = -1; DAT_806129fc = -1; DAT_80612a00 = -1;
                break;
            }
            if (((idx == DAT_806129f0) || (idx == DAT_806129ec)) || (idx == DAT_806129e8)) {
                self[0x27b] = DAT_806129f4;
                if (self[8] != 1) {
                    self[0x275] = self[8];
                    self[8] = 1;
                    self[0x276] = (int)FLOAT_8061925c;
                    self[0x274] = self[8];
                }
                DAT_806129f4 = -1; DAT_806129e8 = -1; DAT_806129ec = -1; DAT_806129f0 = -1;
                break;
            }
            i = i + 4;
            b = b - 1;
        } while (b != 0);
    }

    // worm-event record poll
    i = 0;
    b = 0;
    do {
        idx = *(int*)((int)&DAT_805cbb78 + b);
        if ((idx != 0) && (worm_event_record_is_active(idx))) {
            self[0x27b] = *(int*)((int)&DAT_805cbb7c + b);
            if (self[8] != 1) {
                self[0x275] = self[8];
                self[8] = 1;
                self[0x276] = (int)FLOAT_8061925c;
                self[0x274] = self[8];
            }
            *(int*)((int)&DAT_805cbb78 + b) = 0;
            *(int*)((int)&DAT_805cbb7c + b) = -1;
        }
        i = i + 1;
        b = b + 8;
    } while (i < 8);

    self[0x2cf] = (int)FLOAT_8061925c;
    ret64 = tracker_advance_zone_ring_cursor(actor);

    if (((DAT_80612a34 == 1) && (DAT_80612a30 != -1)) &&
        (aiactor_alert_group_flagB_any((unsigned int)DAT_80612a30) != 0)) {
        DAT_80612a34 = 0;
        DAT_80612a30 = -1;
    }

    tracker_grab_attach_update(ret64, d1);
    dRet = (double)(float)self[0x2d1];
    node = *(int*)(DAT_8061208c + 0x3c) + self[0x27b] * 0x48;

    if ((((double)FLOAT_80619264 < dRet) && (*(int*)(DAT_80611d74 + 0xe8c) != actor)) &&
        (DAT_80612614 != 0x1a)) {
        d1 = tracker_compute_anim_scale(actor);
        d2 = (double)FLOAT_8061925c;
        d3 = (double)*(float*)(actor + 0x1d8);
        d1 = tracker_clamp((double)(float)((double)*(float*)(actor + 0x1cc) + d1), d2, d3);
        *(float*)(actor + 0x1cc) = (float)d1;
    }

    switch (self[8]) {
    case 0:
        if (DAT_80612a20 != 0) {
            tracker_build_zone_index_list();
            self[0x21] = DAT_80612a18[0x21];
            ret64 = tracker_scan_nearest_relation_actor();
            r4scratch = (void*)ret64;
            self[1] = (int)(ret64 >> 0x20);
            self[0x2cc] = (int)FLOAT_8061925c;
            DAT_80612a20 = 0;
            DAT_80612a14 = -1;
        }
        self[0x2af] = 0x2000;
        self[0x2ae] = 0;
        if (self[8] != 1) {
            self[0x275] = self[8];
            self[8] = 1;
            self[0x276] = (int)FLOAT_8061925c;
            self[0x274] = self[8];
        }
        break;

    case 1:
        self[0x2cc] = (int)FLOAT_8061925c;
        self[10] = self[10] & 0xfffffffd;
        switch (*(int*)(node + 8)) {
        case 0:
            if (self[8] != 2) {
                self[0x275] = self[8]; self[8] = 2;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
            break;
        case 1:
            if (self[0x1a1] < 0) {
                if (self[0x19e] < 0)
                    self[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    self[0x1a1] = self[0x19e];
                self[0x26b] = 0; self[0x26a] = 0;
            }
            if (self[8] != 4) {
                self[0x275] = self[8]; self[8] = 4;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
            break;
        case 2:
            if (self[8] != 3) {
                self[0x275] = self[8]; self[8] = 3;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
            break;
        case 3:
            self[1] = 0;
            if (self[8] != 5) {
                self[0x275] = self[8]; self[8] = 5;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
            break;
        case 5:
            idx = (int)*(short*)(node + 0x3c);
            actor = anim_primary_clip_userflags(actor, idx);
            if (actor != 0) {
                if ((*(unsigned int*)(node + 0x34) & 4) == 0)
                    anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_80619260);
                else
                    anim_play_scaled((double)FLOAT_80619268, (double)FLOAT_80619260);
            }
            if (self[8] != 0xc) {
                self[0x275] = self[8]; self[8] = 0xc;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
            break;
        case 6:
            if (self[8] != 0xe) {
                self[0x275] = self[8]; self[8] = 0xe;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
            break;
        case 7:
            if (self[8] != 0xf) {
                self[0x275] = self[8]; self[8] = 0xf;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
            break;
        case 9:
            DAT_80612a34 = 0;
            self[0x27b] = self[0x27b] + 1;
            break;
        case 10:
            DAT_80612a34 = 1;
            DAT_80612a30 = *(int*)(node + 0x2c);
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0xb:
            if (self[8] != 0x13) {
                self[0x275] = self[8]; self[8] = 0x13;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
            break;
        case 0xc:
            aiactor_destroy_instance(dRet, d2, d3, d3, in_f5, in_f6, in_f7, in_f8, actor);
            break;
        case 0xd:
            DAT_80612a2c = (int)*(short*)(node + 0x3c);
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0xe:
            DAT_80612a28 = (int)*(short*)(node + 0x3c);
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0xf:
            DAT_80612a2c = -1;
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0x10:
            DAT_80612a28 = -1;
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0x11:
            self[0xb] = self[0xb] | 0x1000;
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0x12:
            self[0xb] = self[0xb] | 0x4000;
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0x13:
            self[0xb] = self[0xb] & 0xffffafff;
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0x14:
            FLOAT_80612a24 = *(float*)(node + 0x14) * *(float*)(node + 0x14);
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0x15:
            if ((*(short*)(node + 0x3c) == -1) ||
                (anim_is_clip_active(actor, (int)*(short*)(node + 0x3c)) != 0)) {
                j = soundpad_find_zone_by_id(*(int*)(node + 0x14));
                player_ai_teleport_wrapper(self, j, *(unsigned int*)(node + 0x34) >> 0x11 & 1);
                if (self[0x1a1] != j) {
                    if (j < 0)
                        self[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                    else
                        self[0x1a1] = j;
                    self[0x26b] = 0; self[0x26a] = 0;
                }
                if (*(short*)(node + 0x3c) != -1) {
                    *(float*)(actor + 0x70) = FLOAT_8061925c;
                    *(float*)(actor + 0xc4) = FLOAT_8061925c;
                }
                self[0x27b] = self[0x27b] + 1;
            }
            else {
                idx = (int)*(short*)(node + 0x3c);
                if (anim_is_clip_active(actor, idx) == 0)
                    anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_8061925c);
            }
            break;
        case 0x16:
            self[0x27b] = DAT_80612a14;
            break;
        case 0x17:
            if (self[8] != 0x10) {
                self[0x275] = self[8]; self[8] = 0x10;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
            break;
        case 0x18:
            if (*(int*)(node + 0x38) == -1) {
                DAT_80612a04 = -1; DAT_806129f8 = -1; DAT_806129fc = -1; DAT_80612a00 = -1;
            }
            else {
                DAT_80612a00 = soundpad_find_zone_by_id(*(int*)(node + 0x14));
                DAT_806129fc = (*(int*)(node + 0x18) == -1) ? -1
                             : soundpad_find_zone_by_id(*(int*)(node + 0x18));
                DAT_806129f8 = (*(int*)(node + 0x20) == -1) ? -1
                             : soundpad_find_zone_by_id(*(int*)(node + 0x20));
                DAT_80612a04 = *(int*)(node + 0x38);
            }
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0x19:
            if (*(int*)(node + 0x38) == -1) {
                DAT_806129f4 = -1; DAT_806129e8 = -1; DAT_806129ec = -1; DAT_806129f0 = -1;
            }
            else {
                DAT_806129f0 = soundpad_find_zone_by_id(*(int*)(node + 0x14));
                DAT_806129ec = (*(int*)(node + 0x18) == -1) ? -1
                             : soundpad_find_zone_by_id(*(int*)(node + 0x18));
                DAT_806129e8 = (*(int*)(node + 0x20) == -1) ? -1
                             : soundpad_find_zone_by_id(*(int*)(node + 0x20));
                DAT_806129f4 = *(int*)(node + 0x38);
            }
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0x1a:
            if (self[8] != 0x14) {
                self[0x275] = self[8]; self[8] = 0x14;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
            break;
        case 0x1b:
            ret64 = aiactor_spawn_trigger_eval();
            actor = (int)(ret64 >> 0x20);
            if (actor != 0) {
                if (actor == 1) {
                    self[0x27b] = self[0x27b] + 1;
                }
                else {
                    DAT_80612a14 = self[0x27b];
                    self[0x27b] = *(int*)(*(int*)(DAT_8061208c + 0x3c) + self[0x27b] * 0x48 + 0x38);
                }
                if (self[8] != 1) {
                    self[0x275] = self[8]; self[8] = 1;
                    self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                }
                goto LAB_exit;
            }
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0x1e:
            if (self[8] != 0x15) {
                self[0x275] = self[8]; self[8] = 0x15;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
            break;
        case 0x1f:
            actor = *(int*)(node + 0x18) - 1;
            if (actor == -2) actor = 0;
            pNode = worm_event_find_record_by_class(*(int*)(node + 0x14));
            (&DAT_805cbb78)[actor * 2] = (int)pNode;
            (&DAT_805cbb7c)[actor * 2] = *(int*)(node + 0x38);
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0x20:
            actor = *(int*)(node + 0x18) - 1;
            if (actor == -2) actor = 0;
            (&DAT_805cbb78)[actor * 2] = 0;
            (&DAT_805cbb7c)[actor * 2] = -1;
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0x21:
            DAT_80612a08 = 1;
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0x22:
            DAT_80612a08 = 0;
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0x23:
            DAT_80612a0c = *(int*)(node + 0x38);
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0x24:
            DAT_80612a0c = -1;
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0x25:
            DAT_80612a04 = -1; DAT_806129f8 = -1; DAT_806129fc = -1; DAT_80612a00 = -1;
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0x26:
            DAT_806129f4 = -1; DAT_806129e8 = -1; DAT_806129ec = -1; DAT_806129f0 = -1;
            self[0x27b] = self[0x27b] + 1;
            break;
        case 0x29:
            if (self[0x1a1] < 0) {
                if (self[0x19e] < 0)
                    self[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    self[0x1a1] = self[0x19e];
                self[0x26b] = 0; self[0x26a] = 0;
            }
            self[1] = 0;
            self[0x2cc] = (int)FLOAT_8061925c;
            self[0x2cd] = (int)FLOAT_8061925c;
            if (self[8] != 6) {
                self[0x275] = self[8]; self[8] = 6;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
            break;
        case 0x2a:
            actor = entity_handle_resolve(*(unsigned int*)(node + 0x14));
            if (actor == 0) {
                if (anim_is_clip_active(self[0], 0x25) == 0)
                    anim_play_scaled((double)FLOAT_8061926c, (double)FLOAT_80619260);
            }
            else {
                pf = (float*)subobj_get_contact_origin(*(unsigned int*)(actor + 0x80));
                j = soundpad_find_nearest_entity((double)FLOAT_8061925c, pf);
                subobj_carry_nav_init(actor, j, *(int*)(node + 0x18), *(int*)(node + 0x1c));
                self[0x27b] = self[0x27b] + 1;
            }
            break;
        case 0x2d:
            local_78 = (double)(long long)DAT_806109c0;
            j = (int)(*(float*)(node + 0x20) * (float)(local_78 - DOUBLE_806192c8));
            local_70 = (unsigned long long)(long long)j;
            (void)local_70;
            tracker_play_positional_sound(*(int*)(node + 0x14), j, (void*)(actor + 100));
            self[0x27b] = self[0x27b] + 1;
            break;
        default:
            break;
        }
        break;

    case 2:
        j = tracker_play_turn_clip();
        if (j == 0) {
            pNode = (int*)soundpad_resolve_entity_to_zone(*(int*)(node + 0x14));
            if ((pNode[1] & 0x800U) == 0)
                j = soundpad_zone_node_to_index(pNode);
            else
                j = soundpad_find_zone_by_id((int)*(short*)(pNode + 2));
            if (self[0x1a1] != j) {
                if (j < 0)
                    self[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    self[0x1a1] = j;
                self[0x26b] = 0; self[0x26a] = 0;
            }
            if ((tracker_leader_pull_align() != 0) && (self[8] != 0xd)) {
                self[0x275] = self[8]; self[8] = 0xd;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
            if (aiactor_follow_zone_path() != 0) {
                fTmp2 = *(float*)(actor + 100) - (float)pNode[3];
                fTmp3 = *(float*)(actor + 0x68) - (float)pNode[4];
                fTmp4 = *(float*)(actor + 0x6c) - (float)pNode[5];
                self[0xc] = pNode[3]; self[0xd] = pNode[4]; self[0xe] = pNode[5];
                if (fTmp4 * fTmp4 + fTmp2 * fTmp2 + fTmp3 * fTmp3 < FLOAT_80619274) {
                    self[0x27b] = self[0x27b] + 1;
                    if (self[8] != 1) {
                        self[0x275] = self[8]; self[8] = 1;
                        self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                    }
                }
            }
            tracker_play_locomotion_clip();
            aiactor_facing_update();
            self[10] = self[10] & 0xfffdffff;
            self[10] = self[10] & 0xfffbffff;
            anim_set_playback_rate((double)FLOAT_80619278, *(int**)(actor + 4), 1);
        }
        else {
            aiactor_turn_toward_leader();
        }
        break;

    case 3:
    {
        int* nodeA = soundpad_resolve_zone_node(self[0x1a1]);
        int* nodeB = soundpad_resolve_zone_node(self[0x19e]);
        int reach = tracker_relation_class_byte(self[2], DAT_80612a18[2]);
        ret64 = tracker_play_turn_clip();
        if ((int)(ret64 >> 0x20) == 0) {
            int other = *DAT_80612a18;
            fTmp1 = (float)nodeB[3] - *(float*)(other + 100);
            fTmp2 = (float)nodeB[4] - *(float*)(other + 0x68);
            fTmp3 = (float)nodeB[5] - *(float*)(other + 0x6c);
            d1 = math_sqrt((double)(fTmp3 * fTmp3 + fTmp1 * fTmp1 + fTmp2 * fTmp2));
            if (((double)FLOAT_8061927c <= d1) || (reach == 0)) {
                if (nodeA != 0) {
                    other = *DAT_80612a18;
                    fTmp1 = (float)nodeA[3] - *(float*)(other + 100);
                    fTmp2 = (float)nodeA[4] - *(float*)(other + 0x68);
                    fTmp3 = (float)nodeA[5] - *(float*)(other + 0x6c);
                    d1 = math_sqrt((double)(fTmp3 * fTmp3 + fTmp1 * fTmp1 + fTmp2 * fTmp2));
                    if ((d1 <= (double)FLOAT_8061927c) &&
                        ((self[0x19e] != self[0x1a1] || (reach != 0)))) goto LAB_case3_arrive;
                }
                d1 = (double)FLOAT_80619280;
                nodeA = soundpad_resolve_zone_node(DAT_80612a18[0x19e]);
                reach = -1;
                idx = soundpad_get_entity_zone_id((int)nodeA);
                idx = soundpad_get_entity_data(idx, &local_1e8);
                k = 0;
                if (0 < idx) {
                    do {
                        pNode = (int*)(DAT_80611b8c + *(int*)(local_1e8 + k) * 8);
                        fTmp1 = *(float*)(*(int*)(DAT_80611d74 + 0x14) + 100) - (float)pNode[3];
                        fTmp2 = *(float*)(*(int*)(DAT_80611d74 + 0x14) + 0x6c) - (float)pNode[5];
                        d2 = (double)(fTmp1 * fTmp1 + fTmp2 * fTmp2);
                        if ((pNode != nodeA) && ((d2 < d1 || (reach < 0)))) {
                            reach = *(int*)(local_1e8 + k);
                            d1 = d2;
                        }
                        k = k + 4;
                        idx = idx - 1;
                    } while (idx != 0);
                }
                if (reach < 0) reach = self[0x19e];
                if (reach < 0)
                    self[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    self[0x1a1] = reach;
                self[0x26b] = 0; self[0x26a] = 0;
            }
            else {
                if (self[0x19e] < 0)
                    self[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    self[0x1a1] = self[0x19e];
                self[0x26b] = 0; self[0x26a] = 0;
            }
LAB_case3_arrive:
            if (self[0x19e] == self[0x1a1]) {
                if ((((*(unsigned int*)(node + 0x34) & 1) == 0) || (self[1] == 0)) ||
                    (*(float*)(self[1] + 0x1cc) <= FLOAT_8061925c)) {
                    tracker_play_idle_variant_clip();
                    self[10] = self[10] & 0xfffdffff;
                    self[10] = self[10] & 0xfffbffff;
                    anim_set_playback_rate((double)FLOAT_80619278, *(int**)(actor + 4), 1);
                }
                else {
                    tracker_play_crouch_special_clip();
                    if (leaderRec != 0) self[10] = self[10] | 2;
                    self[10] = self[10] | 0x40000;
                    anim_set_playback_rate((double)FLOAT_80619278, *(int**)(actor + 4), 2);
                }
                aiactor_turn_toward_entity(actor, *(int*)(DAT_80611d78 + 0x14), 0);
            }
            else {
                aiactor_follow_zone_path();
                tracker_play_locomotion_clip();
                aiactor_facing_update();
            }
            if (*(int*)(node + 0xc) != 0) {
                ret64 = (**(unsigned long long(**)(int))(&DAT_80472ed8 + *(int*)(node + 0xc) * 4))(actor);
                if ((int)(ret64 >> 0x20) != 0) {
                    self[0x27b] = self[0x27b] + 1;
                    if (self[8] != 1) {
                        self[0x275] = self[8]; self[8] = 1;
                        self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                    }
                }
            }
        }
        break;
    }

    case 4:
    {
        int* zn = soundpad_resolve_zone_node(self[0x1a1]);
        d1 = (double)(*(float*)(actor + 0x68) - (float)zn[4]);
        zn = soundpad_resolve_zone_node(self[0x1a1]);
        d2 = (double)(float)((double)(*(float*)(actor + 0x68) - (float)zn[4]) * d1);
        zn = soundpad_resolve_zone_node(self[0x1a1]);
        d1 = (double)(*(float*)(actor + 100) - (float)zn[3]);
        zn = soundpad_resolve_zone_node(self[0x1a1]);
        d1 = (double)(float)((double)(*(float*)(actor + 100) - (float)zn[3]) * d1 + d2);
        zn = soundpad_resolve_zone_node(self[0x1a1]);
        d2 = (double)(*(float*)(actor + 0x6c) - (float)zn[5]);
        zn = soundpad_resolve_zone_node(self[0x1a1]);
        uTmp = *(unsigned int*)(node + 0x34) & 1;
        d1 = (double)(float)((double)(*(float*)(actor + 0x6c) - (float)zn[5]) * d2 + d1);
        if (tracker_play_turn_clip() == 0) {
            if ((*(unsigned int*)(node + 0x34) & 0x40000) != 0)
                prop_hinge_apply_spring((double)FLOAT_806192b4, actor, self[0x19f]);
            j = self[1];
            if (((j != 0) && (*(int*)(j + 0x20) == 8)) &&
                (tracker_actor_flag_test(*(int*)(j + 0x7c), 0x400000, 0, 0x400001) != 0)) {
                self[1] = 0;
            }
            if ((leaderRec == 0) ||
                (((uTmp != 0 && (self[2] == DAT_806120f8)) &&
                  (FLOAT_80619284 < (float)self[0x2cc])))) {
                j = self[1];
                d2 = (double)FLOAT_80619280;
                if ((j != 0) && (leaderRec != 0)) {
                    fTmp1 = *(float*)(actor + 100) - *(float*)(j + 100);
                    fTmp2 = *(float*)(actor + 0x6c) - *(float*)(j + 0x6c);
                    d2 = (double)(fTmp1 * fTmp1 + fTmp2 * fTmp2);
                }
                j = tracker_scan_nearest_relation_actor();
                if ((j != 0) &&
                    ((leaderRec == 0) ||
                     (fTmp1 = *(float*)(actor + 100) - *(float*)(j + 100),
                      fTmp2 = *(float*)(actor + 0x6c) - *(float*)(j + 0x6c),
                      fTmp1 * fTmp1 + fTmp2 * fTmp2 < (float)((double)FLOAT_806192b8 * d2)))) {
                    self[1] = j;
                    self[0x2cc] = (int)FLOAT_8061925c;
                }
            }
            if ((*(int*)(node + 0xc) != 0) &&
                ((**(unsigned long long(**)(int))(&DAT_80472ed8 + *(int*)(node + 0xc) * 4))(actor) != 0)) {
                self[0x27b] = self[0x27b] + 1;
                if (self[8] != 1) {
                    self[0x275] = self[8]; self[8] = 1;
                    self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                }
            }
            j = (int)aiactor_spawn_trigger_eval();
            if (j != 0) {
                if (j == 1) {
                    self[0x27b] = self[0x27b] + 1;
                }
                else {
                    DAT_80612a14 = self[0x27b];
                    self[0x27b] = *(int*)(*(int*)(DAT_8061208c + 0x3c) + self[0x27b] * 0x48 + 0x38);
                }
                if (self[8] != 1) {
                    self[0x275] = self[8]; self[8] = 1;
                    self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                }
                goto LAB_exit;
            }
            if ((((uTmp == 0) || (leaderRec == 0)) || (self[1] == 0)) &&
                (((double)FLOAT_806192b0 < d1 && (tracker_leader_pull_align() == 0)))) {
                aiactor_facing_update();
                tracker_play_locomotion_clip();
                if (aiactor_follow_zone_path() != 0) {
                    zn = soundpad_resolve_zone_node(self[0x1a1]); self[0xc] = zn[3];
                    zn = soundpad_resolve_zone_node(self[0x1a1]); self[0xd] = zn[4];
                    zn = soundpad_resolve_zone_node(self[0x1a1]); self[0xe] = zn[5];
                }
                goto LAB_exit;
            }
            if ((leaderRec == 0) || (uTmp == 0)) {
                if (d1 <= (double)FLOAT_806192b0) {
                    if ((*(unsigned int*)(node + 0x34) & 2) == 0) {
                        if (self[0x2b3] == 0)
                            aiactor_turn_toward_angle((double)*(float*)(self[0x19f] + 0x18), actor, 0);
                        else
                            aiactor_turn_toward_angle((double)*(float*)(self[0x2b3] + 0x18), actor, 0);
                        self[10] = self[10] & 0xfffdffff;
                        self[10] = self[10] & 0xfffbffff;
                        anim_set_playback_rate((double)FLOAT_80619278, *(int**)(actor + 4), 1);
                    }
                    else {
                        aiactor_turn_toward_entity(actor, *DAT_80612a18, 0);
                        self[10] = self[10] & 0xfffdffff;
                        self[10] = self[10] & 0xfffbffff;
                        anim_set_playback_rate((double)FLOAT_80619278, *(int**)(actor + 4), 1);
                    }
                }
                else {
                    aiactor_facing_update();
                    self[10] = self[10] & 0xfffdffff;
                    self[10] = self[10] & 0xfffbffff;
                    anim_set_playback_rate((double)FLOAT_80619278, *(int**)(actor + 4), 1);
                }
            }
            else {
                aiactor_turn_toward_leader();
                self[10] = self[10] | 0x20000;
                if ((*(unsigned int*)(&DAT_804d34dc +
                     (short)(&DAT_804d1cd0)[*(int*)(*(int*)(actor + 4) + 0xcc)] * 8) & 0x100) != 0) {
                    self[10] = self[10] & 0xfffbffff;
                    if (((*(int**)(actor + 4))[0x32] & 1U) != 0)
                        anim_set_playback_rate((double)FLOAT_80619278, *(int**)(actor + 4), 2);
                }
            }
            if ((uTmp == 0) || (leaderRec == 0)) {
                tracker_play_idle_variant_clip();
            }
            else {
                if (self[1] != 0) self[10] = self[10] | 2;
                tracker_play_crouch_special_clip();
            }
        }
        else if (d1 <= (double)FLOAT_806192b0) {
            aiactor_turn_toward_leader();
        }
        else {
            aiactor_facing_update();
        }
        break;
    }

    case 5:
        if (tracker_play_turn_clip() == 0) {
            if (*(int*)(node + 0x10) != 0) {
                j = (**(int(**)(int))(&DAT_80472ef8 + *(int*)(node + 0x10) * 4))(actor);
                self[1] = j;
            }
            if ((*(unsigned int*)(node + 0x34) & 0x40000) != 0)
                prop_hinge_apply_spring((double)FLOAT_806192b4, actor, self[0x19f]);
            j = self[1];
            if (((j != 0) && (*(float*)(j + 0x1cc) <= FLOAT_8061925c)) ||
                ((j == 0 ||
                  ((aiactor_retarget_visible_teammate() == 0) &&
                   ((*(unsigned int*)(node + 0x34) & 0x400000) != 0))))) {
                self[0x27b] = self[0x27b] + 1;
                if (self[8] != 1) {
                    self[0x275] = self[8]; self[8] = 1;
                    self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                }
            }
            if (leaderRec == 0) {
                self[10] = self[10] & 0xfffdffff;
                self[10] = self[10] & 0xfffbffff;
                anim_set_playback_rate((double)FLOAT_80619278, *(int**)(actor + 4), 1);
            }
            else {
                aiactor_turn_toward_leader();
                self[10] = self[10] | 2;
                if ((*(unsigned int*)(&DAT_804d34dc +
                     (short)(&DAT_804d1cd0)[*(int*)(*(int*)(actor + 4) + 0xcc)] * 8) & 0x100) != 0) {
                    self[10] = self[10] & 0xfffbffff;
                    if (((*(int**)(actor + 4))[0x32] & 1U) != 0)
                        anim_set_playback_rate((double)FLOAT_80619278, *(int**)(actor + 4), 2);
                }
                self[0xb] = self[0xb] | 0x400;
            }
            tracker_play_crouch_special_clip();
            ret64 = aiactor_spawn_trigger_eval();
            actor = (int)(ret64 >> 0x20);
            if (actor != 0) {
                if (actor == 1) {
                    self[0x27b] = self[0x27b] + 1;
                }
                else {
                    DAT_80612a14 = self[0x27b];
                    self[0x27b] = *(int*)(*(int*)(DAT_8061208c + 0x3c) + self[0x27b] * 0x48 + 0x38);
                }
                if (self[8] != 1) {
                    self[0x275] = self[8]; self[8] = 1;
                    self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                }
                goto LAB_exit;
            }
        }
        else {
            aiactor_turn_toward_leader();
        }
        break;

    case 6:
    {
        void* plan = (void*)(long long)tracker_collect_zone_targets(actor, *(int*)(node + 0x14),
                                                    *(int*)(node + 0x18), (int)local_stackA, 10);
        j = tracker_count_crowd_neighbors(actor, (int)plan, (int)local_stackA);
        if (j == 0) {
            ret64 = tracker_pick_zone_destination(actor, plan, (int)local_stackA);
            if ((int)(ret64 >> 0x20) == 0) {
                self[0x2cc] = (int)FLOAT_8061925c;
                if (leaderRec == 0) {
                    if (self[8] != 9) {
                        self[0x275] = self[8]; self[8] = 9;
                        self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                    }
                }
                else {
                    if (self[8] != 10) {
                        self[0x275] = self[8]; self[8] = 10;
                        self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                    }
                }
            }
            else {
                self[0x2cc] = (int)FLOAT_8061925c;
                if (self[8] != 7) {
                    self[0x275] = self[8]; self[8] = 7;
                    self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                }
            }
        }
        else {
            self[0x2cc] = (int)FLOAT_8061925c;
            if (self[8] != 8) {
                self[0x275] = self[8]; self[8] = 8;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
        }
        uTmp = tracker_node_spawn_condition_met(actor);
        if (uTmp == 0) {
            if (((self[1] == 0) &&
                 (tracker_pick_nearest_party_target(actor, (int)plan, (int)local_stackA), self[1] == 0)) &&
                (self[8] != 0xd)) {
                self[0x275] = self[8]; self[8] = 0xd;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
        }
        else {
            self[0x27b] = self[0x27b] + 1;
            if (self[8] != 1) {
                self[0x275] = self[8]; self[8] = 1;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
        }
        break;
    }

    case 7:
        tracker_play_locomotion_clip();
        aiactor_facing_update();
        ret64 = aiactor_follow_zone_path();
        if (((int)(ret64 >> 0x20) != 0) ||
            (((self[10] & 0x8000U) != 0 && (FLOAT_8061928c < (float)self[0x2cc])))) {
            j = tracker_collect_zone_targets(actor, *(int*)(node + 0x14), *(int*)(node + 0x18), (int)pathBuf0, 10);
            ret64 = tracker_pick_nearest_party_target(actor, j, (int)pathBuf0);
            self[10] = self[10] & 0xffff7fff;
            if ((int)(ret64 >> 0x20) == 0) {
                if (self[8] != 9) {
                    self[0x275] = self[8]; self[8] = 9;
                    self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                }
            }
            else {
                self[0x2cc] = (int)FLOAT_8061925c;
                if (self[8] != 10) {
                    self[0x275] = self[8]; self[8] = 10;
                    self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                }
            }
        }
        else if ((self[2] == DAT_806120f8) && (FLOAT_80619284 < (float)self[0x2cc])) {
            j = tracker_collect_zone_targets(actor, *(int*)(node + 0x14), *(int*)(node + 0x18), (int)pathBuf1, 10);
            ret64 = tracker_pick_nearest_party_target(actor, j, (int)pathBuf1);
            self[10] = self[10] & 0xffff7fff;
            if (((int)(ret64 >> 0x20) != 0) &&
                (fTmp2 = *(float*)(actor + 100) - *(float*)(self[1] + 100),
                 fTmp3 = *(float*)(actor + 0x6c) - *(float*)(self[1] + 0x6c),
                 fTmp2 * fTmp2 + fTmp3 * fTmp3 < FLOAT_80619290)) {
                self[0x2cc] = (int)FLOAT_8061925c;
                if (self[8] != 10) {
                    self[0x275] = self[8]; self[8] = 10;
                    self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                }
            }
        }
        uTmp = tracker_node_spawn_condition_met(actor);
        if (uTmp != 0) {
            self[0x27b] = self[0x27b] + 1;
            if (self[8] != 1) {
                self[0x275] = self[8]; self[8] = 1;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
        }
        break;

    case 8:
        aiactor_facing_update();
        if (anim_is_clip_active(actor, 0x1b1) == 0)
            anim_play_scaled((double)FLOAT_80619268, (double)FLOAT_80619260);
        if ((self[2] == DAT_806120f8) && (FLOAT_80619284 < (float)self[0x2cc])) {
            k = 0;
            j = tracker_collect_zone_targets(actor, *(int*)(node + 0x14), *(int*)(node + 0x18), (int)local_cc, 10);
            idx = 0;
            if (0 < j) {
                do {
                    fTmp1 = *(float*)(*(int*)((int)local_cc + idx) + 100) - *(float*)(actor + 100);
                    fTmp2 = *(float*)(*(int*)((int)local_cc + idx) + 0x6c) - *(float*)(actor + 0x6c);
                    if (fTmp1 * fTmp1 + fTmp2 * fTmp2 < FLOAT_80619288) k = k + 1;
                    idx = idx + 4;
                    j = j - 1;
                } while (j != 0);
            }
            if ((k == 0) && (self[8] != 6)) {
                self[0x275] = self[8]; self[8] = 6;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
        }
        uTmp = tracker_node_spawn_condition_met(actor);
        if (uTmp != 0) {
            self[0x27b] = self[0x27b] + 1;
            if (self[8] != 1) {
                self[0x275] = self[8]; self[8] = 1;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
        }
        break;

    case 9:
        if (self[1] == 0) {
            j = tracker_collect_zone_targets(actor, *(int*)(node + 0x14), *(int*)(node + 0x18), (int)pathBuf2, 10);
            tracker_pick_nearest_party_target(actor, j, (int)pathBuf2);
            if (self[8] != 6) {
                self[0x275] = self[8]; self[8] = 6;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
        }
        else {
            k = *(int*)(self[1] + 0x7c);
            tracker_play_locomotion_clip();
            aiactor_facing_update();
            k = *(int*)(k + 0x678);
            if (self[0x1a1] != k) {
                if (k < 0)
                    self[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    self[0x1a1] = k;
                self[0x26b] = 0; self[0x26a] = 0;
            }
            if (leaderRec == 0) {
                ret64 = aiactor_follow_zone_path();
                if (((int)(ret64 >> 0x20) != 0) && (self[8] != 6)) {
                    self[0x275] = self[8]; self[8] = 6;
                    self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                }
            }
            else {
                self[0x2cc] = (int)FLOAT_8061925c;
                if (self[8] != 10) {
                    self[0x275] = self[8]; self[8] = 10;
                    self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                }
            }
            if (self[2] == DAT_806120f8) {
                j = tracker_collect_zone_targets(actor, *(int*)(node + 0x14), *(int*)(node + 0x18), (int)pathBuf3, 10);
                ret64 = tracker_pick_nearest_party_target(actor, j, (int)pathBuf3);
                if ((int)(ret64 >> 0x20) != 0) {
                    self[0x2cc] = (int)FLOAT_8061925c;
                    if (self[8] != 10) {
                        self[0x275] = self[8]; self[8] = 10;
                        self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                    }
                }
            }
            uTmp = tracker_node_spawn_condition_met(actor);
            if (uTmp != 0) {
                self[0x27b] = self[0x27b] + 1;
                if (self[8] != 1) {
                    self[0x275] = self[8]; self[8] = 1;
                    self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                }
            }
        }
        break;

    case 10:
        if ((*(unsigned int*)(DAT_80610ba0 + self[0x81] * 0x29c + 0xc) & 1) == 0) {
            k = self[0x25];
            postureSel = 0;
            if (((k == 0xe) || (k == 0x35)) || (k == 0xf)) postureSel = 1;
            uTmp = ~(unsigned int)(-(int)postureSel) & 2;
        }
        else {
            uTmp = 1;
        }
        {
            int cidx = uTmp * 4;
            int cA = anim_is_clip_active(actor, (&DAT_80472c10)[uTmp]);
            int cB = anim_is_clip_active(actor, *(int*)(cidx + (int)&DAT_80472c10 - 16));
            int cC = anim_is_clip_active(actor, *(int*)(cidx + (int)&DAT_80472c10 - 32));
            int cD = anim_is_clip_active(actor, *(int*)(cidx + (int)&DAT_80472c10 - 48));
            cond = *(int*)(self[0x2fc] + 8) != 0x52313130;
            aiactor_leader_zone_update(actor);
            aiactor_turn_toward_leader();
            if (((cC == 0) && (cA == 0)) && ((cB == 0) && (cD == 0))) {
                if ((uTmp != 0) && (cond)) {
                    unsigned int rc = rng_counter_tick();
                    local_70 = (unsigned long long)rc;
                    k = (int)(FLOAT_80619294 + (float)((double)(long long)(unsigned int)local_70 - DOUBLE_806192d0) / FLOAT_80619298);
                    if (k != 1) {
                        if (self[8] != 0xb) {
                            self[0x275] = self[8]; self[8] = 0xb;
                            self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                        }
                        anim_play_scaled((double)FLOAT_80619268, (double)FLOAT_80619260);
                        goto LAB_case10_post;
                    }
                }
                self[0x2cc] = (int)FLOAT_8061925c;
                self[0x277] = 1;
                anim_play_scaled((double)FLOAT_80619268, (double)FLOAT_80619260);
            }
            else {
                k = anim_primary_clip_userflags(actor, *(int*)(cidx + (int)&DAT_80472c10 - 48));
                if (k == 0) {
                    if (((cA != 0) || (cB != 0)) || (cC != 0)) {
                        k = anim_active_slot_userflags(*(int*)(actor + 4));
                        if ((k == 0) || ((cA != 0 && ((float)self[0x2cc] <= FLOAT_8061929c)))) {
                            if ((uTmp == 1) &&
                                ((((cC != 0 && (self[0x277] < 7)) &&
                                   (FLOAT_806192a0 < (float)self[0x2cc])) && (self[1] != 0)))) {
                                self[0x277] = self[0x277] + 1;
                                anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_80619260);
                                fTmp1 = (DAT_805015bf == '\0') ? FLOAT_806192a0 : FLOAT_80619260;
                                unsigned long long rc = rng_counter_tick();
                                local_70 = rc;
                                self[0x2cc] = (int)(float)((double)(FLOAT_806192a8 *
                                    ((float)((double)(long long)(int)(rc >> 0x20) - DOUBLE_806192d0) / FLOAT_80619298) +
                                    FLOAT_806192a4) / (double)fTmp1);
                            }
                        }
                        else if (((uTmp == 1) && (self[0x277] < 7)) && ((cB == 0 && (self[1] != 0)))) {
                            self[0x277] = self[0x277] + 1;
                            anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_80619260);
                            fTmp1 = (DAT_805015bf == '\0') ? FLOAT_806192a0 : FLOAT_80619260;
                            unsigned long long rc = rng_counter_tick();
                            local_70 = rc;
                            self[0x2cc] = (int)(float)((double)(FLOAT_806192a8 *
                                ((float)((double)(long long)(int)(rc >> 0x20) - DOUBLE_806192d0) / FLOAT_80619298) +
                                FLOAT_806192a4) / (double)fTmp1);
                        }
                        else if (((uTmp == 0) && (self[0x277] < 2)) && (cB == 0)) {
                            self[0x277] = self[0x277] + 1;
                            anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_80619260);
                        }
                        else if (cC == 0) {
                            if ((uTmp != 0) && (cond)) {
                                unsigned int rc = rng_counter_tick();
                                local_70 = (unsigned long long)rc;
                                k = (int)(FLOAT_80619294 + (float)((double)(long long)(unsigned int)local_70 - DOUBLE_806192d0) / FLOAT_80619298);
                                if (k != 1) {
                                    self[0x277] = 0;
                                    anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_80619260);
                                    if (self[8] != 0xb) {
                                        self[0x275] = self[8]; self[8] = 0xb;
                                        self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                                    }
                                    goto LAB_case10_post;
                                }
                            }
                            self[0x277] = 0;
                            anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_80619260);
                            fTmp1 = (DAT_805015bf == '\0') ? FLOAT_806192a0 : FLOAT_80619260;
                            unsigned long long rc = rng_counter_tick();
                            local_70 = rc;
                            self[0x2cc] = (int)(float)((double)(FLOAT_806192a8 *
                                ((float)((double)(long long)(int)(rc >> 0x20) - DOUBLE_806192d0) / FLOAT_80619298) +
                                FLOAT_806192a4) / (double)fTmp1);
                        }
                        else {
                            self[0x277] = 0;
                            unsigned int rc = rng_counter_tick();
                            local_70 = (unsigned long long)rc;
                            self[0x2cc] = (int)(FLOAT_806192a4 * ((float)((double)(long long)(unsigned int)local_70 - DOUBLE_806192d0) / FLOAT_80619298));
                            anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_80619260);
                        }
                    }
                }
                else {
                    self[0x2cc] = (int)FLOAT_8061925c;
                    self[0x277] = 1;
                    anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_80619260);
                }
            }
LAB_case10_post:
            if (cC == 0) {
                self[10] = self[10] & 0xfffffffd;
                self[0xb] = self[0xb] & 0xfffffbff;
            }
            else {
                self[0xb] = self[0xb] | 0x400;
                self[10] = self[10] | 2;
            }
            if (((FLOAT_806192ac < (float)self[0x276]) || (self[1] == 0)) &&
                (self[2] == DAT_806120f8)) {
                j = tracker_collect_zone_targets(actor, *(int*)(node + 0x14), *(int*)(node + 0x18), (int)pathBuf4, 10);
                b = tracker_any_neighbor_crowding(actor, j, (int)pathBuf4);
                if (b == 0) {
                    if (leaderRec == 0) {
                        self[0x2cc] = (int)FLOAT_8061925c;
                        if (self[8] != 9) {
                            self[0x275] = self[8]; self[8] = 9;
                            self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                        }
                    }
                }
                else {
                    self[10] = self[10] & 0xfffffffd;
                    self[0xb] = self[0xb] & 0xfffffbff;
                    if (self[8] != 6) {
                        self[0x275] = self[8]; self[8] = 6;
                        self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                    }
                }
            }
            uTmp = tracker_node_spawn_condition_met(actor);
            if (uTmp != 0) {
                self[0x27b] = self[0x27b] + 1;
                if (self[8] != 1) {
                    self[0x275] = self[8]; self[8] = 1;
                    self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                }
            }
        }
        break;

    case 0xb:
        if ((*(unsigned int*)(DAT_80610ba0 + self[0x81] * 0x29c + 0xc) & 1) == 0) {
            k = self[0x25];
            postureSel = 0;
            if (((k == 0xe) || (k == 0x35)) || (k == 0xf)) postureSel = 1;
            uTmp = ~(unsigned int)(-(int)postureSel) & 2;
        }
        else {
            uTmp = 1;
        }
        {
            int cA = anim_is_clip_active(actor, (&DAT_80472c30)[uTmp]);
            int cB = anim_is_clip_active(actor, *(int*)(uTmp * 4 + (int)&DAT_80472c30 - 16));
            int* pClipC = (int*)(uTmp * 4 + (int)&DAT_80472c30 - 48);
            int cC = anim_is_clip_active(actor, *pClipC);
            aiactor_leader_zone_update(actor);
            aiactor_turn_toward_leader();
            if (((cB == 0) && (cA == 0)) && (cC == 0)) {
                unsigned int rc = rng_counter_tick();
                local_70 = (unsigned long long)rc;
                k = (int)(FLOAT_80619294 + (float)((double)(long long)(unsigned int)local_70 - DOUBLE_806192d0) / FLOAT_80619298);
                if (k == 1) {
                    self[0x2cc] = (int)FLOAT_8061925c;
                    self[0x277] = 1;
                    anim_play_scaled((double)FLOAT_80619268, (double)FLOAT_80619260);
                }
                else {
                    self[0x277] = 0;
                    if (self[8] != 10) {
                        self[0x275] = self[8]; self[8] = 10;
                        self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                    }
                    anim_play_scaled((double)FLOAT_80619268, (double)FLOAT_80619260);
                }
            }
            else {
                k = anim_primary_clip_userflags(actor, *pClipC);
                if (k == 0) {
                    if ((cA != 0) || (cB != 0)) {
                        k = anim_active_slot_userflags(*(int*)(actor + 4));
                        if (((k != 0) && (cA == 0)) ||
                            (((k = anim_active_slot_userflags(*(int*)(actor + 4)), k == 0 &&
                               (cA != 0)) && (FLOAT_8061929c < (float)self[0x2cc])))) {
                            if (uTmp == 1) {
                                if ((self[0x277] < 7) && (self[1] != 0)) {
                                    self[0x277] = self[0x277] + 1;
                                    anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_80619260);
                                    fTmp1 = (DAT_805015bf == '\0') ? FLOAT_806192a0 : FLOAT_80619260;
                                    unsigned long long rc = rng_counter_tick();
                                    local_70 = rc;
                                    self[0x2cc] = (int)(float)((double)(FLOAT_806192a8 *
                                        ((float)((double)(long long)(int)(rc >> 0x20) - DOUBLE_806192d0) / FLOAT_80619298) +
                                        FLOAT_806192a4) / (double)fTmp1);
                                }
                                else {
                                    self[0x2cc] = (int)FLOAT_8061925c;
                                    self[0x277] = 0;
                                    anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_80619260);
                                }
                            }
                            else if (uTmp == 2) {
                                unsigned long long rc = rng_counter_tick();
                                local_70 = rc;
                                k = (int)(FLOAT_8061928c * ((float)((double)(long long)(int)(rc >> 0x20) - DOUBLE_806192d0) / FLOAT_80619298) + FLOAT_80619294);
                                if ((self[0x277] < k + 1) && (self[1] != 0)) {
                                    self[0x277] = self[0x277] + 1;
                                    anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_80619260);
                                    fTmp1 = (DAT_805015bf == '\0') ? FLOAT_806192a0 : FLOAT_80619260;
                                    rc = rng_counter_tick();
                                    local_70 = rc;
                                    self[0x2cc] = (int)(float)((double)(FLOAT_806192a8 *
                                        ((float)((double)(long long)(int)(rc >> 0x20) - DOUBLE_806192d0) / FLOAT_80619298) +
                                        FLOAT_806192a4) / (double)fTmp1);
                                }
                                else {
                                    self[0x2cc] = (int)FLOAT_8061925c;
                                    self[0x277] = 0;
                                    anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_80619260);
                                }
                            }
                            else {
                                unsigned int rc = rng_counter_tick();
                                local_70 = (unsigned long long)rc;
                                k = (int)(FLOAT_80619294 + (float)((double)(long long)(unsigned int)local_70 - DOUBLE_806192d0) / FLOAT_80619298);
                                if (k == 1) {
                                    self[0x277] = 0;
                                    anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_80619260);
                                    fTmp1 = (DAT_805015bf == '\0') ? FLOAT_806192a0 : FLOAT_80619260;
                                    unsigned long long rc2 = rng_counter_tick();
                                    local_70 = rc2;
                                    self[0x2cc] = (int)(float)((double)(FLOAT_806192a8 *
                                        ((float)((double)(long long)(int)(rc2 >> 0x20) - DOUBLE_806192d0) / FLOAT_80619298) +
                                        FLOAT_806192a4) / (double)fTmp1);
                                }
                                else {
                                    self[0x277] = 0;
                                    if (self[8] != 10) {
                                        self[0x275] = self[8]; self[8] = 10;
                                        self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                                    }
                                    anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_80619260);
                                }
                            }
                        }
                        else if ((((uTmp == 1) && (cB != 0)) && (self[0x277] < 7)) &&
                                 ((FLOAT_806192a0 < (float)self[0x2cc] && (self[1] != 0)))) {
                            self[0x277] = self[0x277] + 1;
                            anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_80619260);
                            fTmp1 = (DAT_805015bf == '\0') ? FLOAT_806192a0 : FLOAT_80619260;
                            unsigned long long rc = rng_counter_tick();
                            local_70 = rc;
                            self[0x2cc] = (int)(float)((double)(FLOAT_806192a8 *
                                ((float)((double)(long long)(int)(rc >> 0x20) - DOUBLE_806192d0) / FLOAT_80619298) +
                                FLOAT_806192a4) / (double)fTmp1);
                        }
                    }
                }
                else {
                    self[0x2cc] = (int)FLOAT_8061925c;
                    self[0x277] = 0;
                    anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_80619260);
                }
            }
            if (cB == 0) {
                self[10] = self[10] & 0xfffffffd;
                self[0xb] = self[0xb] & 0xfffffbff;
            }
            else {
                self[0xb] = self[0xb] | 0x400;
                self[10] = self[10] | 2;
            }
            if (((FLOAT_806192ac < (float)self[0x276]) || (self[1] == 0)) &&
                (self[2] == DAT_806120f8)) {
                j = tracker_collect_zone_targets(actor, *(int*)(node + 0x14), *(int*)(node + 0x18), (int)pathBuf5, 10);
                b = tracker_any_neighbor_crowding(actor, j, (int)pathBuf5);
                if (b == 0) {
                    if (leaderRec == 0) {
                        self[0x2cc] = (int)FLOAT_8061925c;
                        if (self[8] != 9) {
                            self[0x275] = self[8]; self[8] = 9;
                            self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                        }
                    }
                }
                else {
                    self[10] = self[10] & 0xfffffffd;
                    self[0xb] = self[0xb] & 0xfffffbff;
                    if (self[8] != 6) {
                        self[0x275] = self[8]; self[8] = 6;
                        self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                    }
                }
            }
            uTmp = tracker_node_spawn_condition_met(actor);
            if (uTmp != 0) {
                self[0x27b] = self[0x27b] + 1;
                if (self[8] != 1) {
                    self[0x275] = self[8]; self[8] = 1;
                    self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                }
            }
        }
        break;

    case 0xc:
        if ((FLOAT_8061925c < *(float*)(*(int*)(actor + 4) + 400)) &&
            ((*(unsigned int*)(node + 0x34) & 4) != 0)) {
            *(float*)(*(int*)(actor + 4) + 0xf4) = FLOAT_8061925c;
            *(float*)(*(int*)(actor + 4) + 0x14c) = FLOAT_8061925c;
            *(float*)(*(int*)(actor + 4) + 0xfc) = FLOAT_8061925c;
            *(float*)(*(int*)(actor + 4) + 0x154) = FLOAT_8061925c;
        }
        if ((*(unsigned int*)(node + 0x34) & 2) == 0) {
            if ((*(unsigned int*)(node + 0x34) & 0x20000) != 0) {
                if (self[0x2b3] == 0)
                    aiactor_turn_toward_angle((double)*(float*)(self[0x19f] + 0x18), actor, 0);
                else
                    aiactor_turn_toward_angle((double)*(float*)(self[0x2b3] + 0x18), actor, 0);
            }
        }
        else {
            ret64 = aiactor_turn_toward_entity(actor, *DAT_80612a18, 0);
            if ((int)(ret64 >> 0x20) == 0) break;
        }
        if ((*(unsigned int*)(node + 0x34) & 0x40000) != 0)
            prop_hinge_apply_spring((double)FLOAT_806192b4, actor, self[0x19f]);
        j = anim_event_scan_active();
        if (j == 0) {
            self[10] = self[10] & 0xfffffffd;
        }
        else {
            if (*(int*)(node + 0x10) != 0) {
                j = (**(int(**)(int))(&DAT_80472ef8 + *(int*)(node + 0x10) * 4))(actor);
                self[1] = j;
            }
            if (self[1] == 0) self[1] = actor;
            self[10] = self[10] | 2;
            self[0xb] = self[0xb] | 0x400;
            object_melee_thrown_fire(ret64, d1, actor, 1, 0, 0);
            self[10] = self[10] & 0xfffffffd;
            self[0xb] = self[0xb] & 0xfffffbff;
        }
        j = (int)aiactor_spawn_trigger_eval();
        if (j != 0) {
            if (j == 1) {
                self[0x27b] = self[0x27b] + 1;
            }
            else {
                DAT_80612a14 = self[0x27b];
                self[0x27b] = *(int*)(*(int*)(DAT_8061208c + 0x3c) + self[0x27b] * 0x48 + 0x38);
            }
            if (self[8] != 1) {
                self[0x275] = self[8]; self[8] = 1;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
            goto LAB_exit;
        }
        idx = (int)*(short*)(node + 0x3c);
        if (*(int*)(DAT_80611824 + (short)(&DAT_804d1cd0)[idx] * 4) == 0) {
            level_get_current_id();
            ret64 = level_actor_check_active();
            if ((int)(ret64 >> 0x20) != 0) {
                if (anim_is_clip_active(actor, 0x25) == 0)
                    anim_play_scaled((double)FLOAT_80619268, (double)FLOAT_80619260);
            }
        }
        else {
            if (anim_is_clip_active(actor, idx) == 0) {
                if ((*(unsigned int*)(node + 0x34) & 4) == 0)
                    anim_play_scaled((double)FLOAT_80619268, (double)FLOAT_80619260);
                else
                    anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_80619260);
            }
            if (*(int*)(node + 0xc) == 0) {
                j = anim_primary_clip_userflags(actor, idx);
                if (j != 0) {
                    j = soundpad_find_nearest_entity((double)FLOAT_8061925c, (float*)(actor + 100));
                    if (j != self[0x19e]) {
                        self[0x19e] = j; self[0x26b] = 0; self[0x26a] = 0;
                    }
                    j = anim_clip_has_flags(actor, (void*)(long long)idx, 0, 0x800000);
                    if ((j != 0) && (anim_clip_has_flags(actor, (void*)(long long)idx, 0, 0x40) == 0)) {
                        d1 = (double)floor_query_with_interact(
                            (double)*(float*)(&DAT_8048ca94 + *(short*)(self + 0x12) * 0x30),
                            (double)*(float*)(&DAT_8048ca9c + *(short*)(self + 0x12) * 0x30),
                            actor + 100, 0, (int*)0, 0);
                        if ((double)FLOAT_806192bc == d1) {
                            tracker_play_idle_variant_clip();
                        }
                        else if (FLOAT_806192c0 <= ABS((float)((double)*(float*)(actor + 0x68) - d1))) {
                            self[0x2ff] = (int)(float)d1;
                            *(float*)(actor + 0x68) = (float)d1;
                            *(unsigned int*)(actor + 0x34) = floor_height_from_position(
                                (double)*(float*)(&DAT_8048ca94 + *(short*)(self + 0x12) * 0x30),
                                (double)*(float*)(&DAT_8048ca9c + *(short*)(self + 0x12) * 0x30));
                            tracker_play_idle_variant_clip();
                        }
                    }
                    self[0x27b] = self[0x27b] + 1;
                    if (self[8] != 1) {
                        self[0x275] = self[8]; self[8] = 1;
                        self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                    }
                }
            }
            else {
                ret64 = (**(unsigned long long(**)(int))(&DAT_80472ed8 + *(int*)(node + 0xc) * 4))(actor);
                if ((int)(ret64 >> 0x20) != 0) {
                    j = soundpad_find_nearest_entity((double)FLOAT_8061925c, (float*)(actor + 100));
                    if (j != self[0x19e]) {
                        self[0x19e] = j; self[0x26b] = 0; self[0x26a] = 0;
                    }
                    self[0x27b] = self[0x27b] + 1;
                    if (self[8] != 1) {
                        self[0x275] = self[8]; self[8] = 1;
                        self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                    }
                }
            }
            *(float*)(*(int*)(actor + 4) + 0x100) = FLOAT_80619260;
        }
        break;

    case 0xd:
        if (tracker_play_turn_clip() == 0) {
            tracker_play_idle_variant_clip();
        }
        aiactor_turn_toward_entity(actor, *(int*)(DAT_80611d78 + 0x14), 0);
        self[10] = self[10] & 0xfffdffff;
        self[10] = self[10] & 0xfffbffff;
        anim_set_playback_rate((double)FLOAT_80619278, *(int**)(actor + 4), 1);
        if ((self[10] & 0x10000U) != 0) {
            if (self[2] == DAT_806120f8) {
                cond = false;
                j = aiactor_nav_advance_path(actor, self + 0x19e);
                self[0x1a0] = j;
                if (self[0x1a0] == -2) {
                    b = 0;
                    j = 0;
                    do {
                        unsigned int* ph = (unsigned int*)((int)self + j + 0xe7c);
                        if (*(int*)((int)self + j + 0xe7c) != 0) {
                            k = entity_handle_validate(ph);
                            if (trigger_is_moving(k) != 0) {
                                soundpad_portal_activate(*(int*)(*(int*)(k + 0x7c) + 0x44));
                                *ph = 0;
                                cond = true;
                            }
                        }
                        b = b + 1;
                        j = j + 4;
                    } while (b < 3);
                }
                else {
                    cond = true;
                }
                if (cond) {
                    self[0x26b] = 0; self[0x26a] = 0;
                    j = aiactor_nav_advance_path(actor, self + 0x19e);
                    self[0x1a0] = j;
                    if (0 < self[0x1a0]) {
                        self[0x2cd] = (int)FLOAT_8061925c;
                        DAT_80612a10 = 0;
                        self[10] = self[10] & 0xfffeffff;
                        if (self[8] != 1) {
                            self[0x275] = self[8]; self[8] = 1;
                            self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                        }
                    }
                }
            }
            goto LAB_exit;
        }
        j = *(int*)(node + 8);
        if (j == 3) {
LAB_case_d_repath:
            if (((double)FLOAT_806192c4 < distSq) || (FLOAT_8061928c < (float)self[0x2cd])) {
                self[0x2cd] = (int)FLOAT_8061925c;
                DAT_80612a10 = 0;
                if (self[8] != 1) {
                    self[0x275] = self[8]; self[8] = 1;
                    self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                }
                self[0x26b] = 0; self[0x26a] = 0;
            }
        }
        else {
            if (j < 3) {
                if (j == 1) goto LAB_case_d_repath;
            }
            else if (j == 0x29) {
                b = tracker_collect_zone_targets(actor, *(int*)(node + 0x14), *(int*)(node + 0x18), (int)pathBuf6, 10);
                tracker_pick_nearest_party_target(actor, b, (int)pathBuf6);
                if (self[1] != 0) {
                    self[0x2cc] = (int)FLOAT_8061925c;
                    if (self[8] != 6) {
                        self[0x275] = self[8]; self[8] = 6;
                        self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                    }
                    self[0x26b] = 0; self[0x26a] = 0;
                }
                goto LAB_exit;
            }
            j = tracker_leader_pull_align();
            if (j == 0) {
                if ((*(unsigned int*)(node + 0x34) & 0x200000) == 0) {
                    self[0x2cd] = (int)FLOAT_8061925c;
                    DAT_80612a10 = 0;
                    if (self[8] != 1) {
                        self[0x275] = self[8]; self[8] = 1;
                        self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                    }
                    self[0x26b] = 0; self[0x26a] = 0;
                }
                else {
                    j = tracker_debug_distance_probe();
                    if ((j == 0) || (j == 3)) {
                        self[0x2cd] = (int)FLOAT_8061925c;
                        DAT_80612a10 = 0;
                        if (self[8] != 1) {
                            self[0x275] = self[8]; self[8] = 1;
                            self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
                        }
                        self[0x26b] = 0; self[0x26a] = 0;
                    }
                }
            }
        }
        goto LAB_exit;

    case 0xe:
        self[0x27b] = *(int*)(node + 0x38);
        if (self[8] != 1) {
            self[0x275] = self[8]; self[8] = 1;
            self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
        }
        break;

    case 0xf:
        j = aiactor_alert_group_flagB_any(*(unsigned int*)(node + 0x2c));
        if (j == 0) {
            self[0x27b] = self[0x27b] + 1;
        }
        else {
            self[0x27b] = *(int*)(node + 0x38);
        }
        if (self[8] != 1) {
            self[0x275] = self[8]; self[8] = 1;
            self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
        }
        break;

    case 0x10:
        uTmp = statlvl_class_is_state5(*(int*)(node + 0x14));
        if (uTmp == 0) {
            self[0x27b] = self[0x27b] + 1;
        }
        else {
            self[0x27b] = *(int*)(node + 0x38);
        }
        if (self[8] != 1) {
            self[0x275] = self[8]; self[8] = 1;
            self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
        }
        break;

    case 0x11:
    {
        int* tbl = *(int**)(DAT_8061208c + 0x34);
        int rec = tbl[self[0x277] * 2];
        if (*(int*)(rec + self[0x2a8] * 0xc) == 0x12) {
            self[0x27b] = self[0x27b] + 1;
            if (self[8] != 1) {
                self[0x275] = self[8]; self[8] = 1;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
        }
        else {
            j = soundpad_find_zone_by_id(*(int*)(rec + self[0x2a8] * 0xc + 8));
            if (j < 0)
                self[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            else
                self[0x1a1] = soundpad_find_zone_by_id(*(int*)(rec + self[0x2a8] * 0xc + 8));
            self[0x26b] = 0; self[0x26a] = 0;
            self[0x2a8] = self[0x2a8] + 1;
            if (self[8] != 0x12) {
                self[0x275] = self[8]; self[8] = 0x12;
                self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
            }
        }
        break;
    }

    case 0x12:
        ret64 = aiactor_follow_zone_path();
        if ((int)(ret64 >> 0x20) == 0) {
            tracker_play_locomotion_clip();
            aiactor_facing_update();
        }
        else if (self[8] != 0x11) {
            self[0x275] = self[8]; self[8] = 0x11;
            self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
        }
        break;

    case 0x13:
        j = DAT_80611b8c + DAT_80612a18[0x19e] * 8;
        j = *(int*)j;
        if (((j == *(int*)(node + 0x14)) || (j == *(int*)(node + 0x18))) ||
            (j == *(int*)(node + 0x1c))) {
            self[0x27b] = self[0x27b] + *(int*)(node + 0x20);
        }
        else {
            self[0x27b] = self[0x27b] + 1;
        }
        if (self[8] != 1) {
            self[0x275] = self[8]; self[8] = 1;
            self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
        }
        break;

    case 0x14:
        if (self[0x2b3] == 0)
            aiactor_turn_toward_angle((double)*(float*)(self[0x19f] + 0x18), actor, 0);
        else
            aiactor_turn_toward_angle((double)*(float*)(self[0x2b3] + 0x18), actor, 0);
        self[10] = self[10] & 0xfffdffff;
        self[10] = self[10] & 0xfffbffff;
        anim_set_playback_rate((double)FLOAT_80619278, *(int**)(actor + 4), 1);
        tracker_play_idle_variant_clip();
        {
            int other = *DAT_80612a18;
            fTmp1 = *(float*)(actor + 100) - *(float*)(other + 100);
            fTmp2 = *(float*)(actor + 0x68) - *(float*)(other + 0x68);
            fTmp3 = *(float*)(actor + 0x6c) - *(float*)(other + 0x6c);
        }
        if (fTmp3 * fTmp3 + fTmp1 * fTmp1 + fTmp2 * fTmp2 <= FLOAT_80612a24) {
            ret64 = subobj_carry_nearest_grab_scan();
            if ((int)(ret64 >> 0x20) != 0) {
                j = entity_handle_validate((unsigned int*)(DAT_80612a18 + 0x302));
                if (j != 0) {
                    worm_set_pending_state_gated((int)self, r4scratch, 0, 0x4000);
                    self[0x302] = *(int*)(j + 0x2c);
                    self[0x27b] = self[0x27b] + 1;
                }
                break;
            }
        }
        self[0x27b] = self[0x27b] + 1;
        if (self[8] != 1) {
            self[0x275] = self[8]; self[8] = 1;
            self[0x276] = (int)FLOAT_8061925c; self[0x274] = self[8];
        }
        break;

    case 0x15:
        j = entity_handle_resolve(*(unsigned int*)(node + 0x14));
        if (j != 0) {
            worm_set_pending_state_gated((int)self, r4scratch, 0, 0x4000);
            self[0x302] = *(int*)(j + 0x2c);
            self[0x27b] = self[0x27b] + 1;
        }
        break;

    default:
        break;
    }

    if ((DAT_80612a10 == 0) && ((self[10] & 1U) != 0)) {
        if (DAT_805015bd == '\n') {
            a = self[4];
            b = self[5];
        }
        else {
            a = self[6];
            b = self[7];
        }
        self[0x2af] = b;
        self[0x2ae] = a;
        worm_set_pending_state_gated((int)self, r4scratch, 0, 0x20);
    }

LAB_exit:
    FUN_8038270c();
}


// ===========================================================================
// batch_754 - scripted-behavior FSM helper functions (0x8027cd4c-0x8027d82c).
// Called from aiactor_script_behavior_tick and its sub-states above.
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x8027cd4c - test the actor's co-op flag words against two masks.
// Selects the flag pair at +0x10/+0x14 (co-op mode '\n') or +0x18/+0x1c
// (otherwise); returns 1 if either mask intersects its word.
int tracker_actor_flag_test(int rec, int p2, unsigned int mask3, unsigned int mask4)
{
    unsigned int wLo;
    unsigned int wHi;
    (void)p2;

    if (DAT_805015bd == '\n') {
        wLo = *(unsigned int*)(rec + 0x10);
        wHi = *(unsigned int*)(rec + 0x14);
    }
    else {
        wLo = *(unsigned int*)(rec + 0x18);
        wHi = *(unsigned int*)(rec + 0x1c);
    }
    if ((mask4 & wHi) != 0 || (mask3 & wLo) != 0) {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x8027cda0 - read the first byte of the (a,b) relation table entry.
int tracker_relation_class_byte(int a, int b)
{
    char* entry;
    entry = (char*)aiactor_relation_entry(a, b);
    return (int)*entry;
}

// ---------------------------------------------------------------------------
// 0x8027cdc8 - nav path cost between two zones.
// Returns 0 if the two are identical (path == -1), a fixed far cost
// (FLOAT_80619280) if unreachable (path < 0), else the larger of the two
// path accumulators (length vs cost).
double tracker_zone_path_cost(int zoneFrom, int zoneTo)
{
    int path;
    double cost;
    int req[3];
    int reqTo;
    int pad0;
    int pad1;

    pad0 = 0;
    pad1 = 0;
    (void)pad0; (void)pad1;
    FLOAT_80611ba0 = FLOAT_8061925c;
    FLOAT_80611ba4 = FLOAT_8061925c;
    req[0] = zoneFrom;
    reqTo = zoneTo;
    (void)reqTo;
    path = aiactor_nav_advance_path(*(int*)(DAT_80611d78 + 0x14), req);
    if (path == -1) {
        cost = (double)FLOAT_8061925c;
    }
    else if (path < 0) {
        cost = (double)FLOAT_80619280;
    }
    else {
        cost = (double)FLOAT_80611ba0;
        if (cost < (double)FLOAT_80611ba4) {
            cost = (double)FLOAT_80611ba4;
        }
    }
    return cost;
}

// ---------------------------------------------------------------------------
// 0x8027ce4c - telemetry/debug distance probe (results discarded).
// Computes several actor<->leader / actor<->zone-node distances (via the same
// path-cost and sqrt helpers the FSM uses) but the original discards the
// results; the removed unreachable blocks mean the classification return value
// is not recoverable from the decompiler output, so this returns 0.
int tracker_debug_distance_probe(void)
{
    float dx;
    float dz;
    int actor;
    int self;
    int leaderSelf;
    int* zn;
    double t;
    double acc;

    actor = FUN_803826f0();
    self = *(int*)(actor + 0x7c);
    leaderSelf = *(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c);

    tracker_zone_path_cost(*(int*)(self + 0x678), *(int*)(self + 0x684));
    tracker_zone_path_cost(*(int*)(leaderSelf + 0x678), *(int*)(self + 0x684));

    dx = *(float*)(*(int*)(DAT_80611d78 + 0x14) + 100) - *(float*)(actor + 100);
    dz = *(float*)(*(int*)(DAT_80611d78 + 0x14) + 0x6c) - *(float*)(actor + 0x6c);
    math_sqrt((double)(dx * dx + dz * dz));

    zn = soundpad_resolve_zone_node(*(int*)(leaderSelf + 0x678));
    t = (double)(*(float*)(*(int*)(DAT_80611d78 + 0x14) + 0x6c) - (float)zn[5]);
    zn = soundpad_resolve_zone_node(*(int*)(leaderSelf + 0x678));
    acc = (double)(float)((double)(*(float*)(*(int*)(DAT_80611d78 + 0x14) + 0x6c) - (float)zn[5]) * t);
    zn = soundpad_resolve_zone_node(*(int*)(leaderSelf + 0x678));
    t = (double)(*(float*)(*(int*)(DAT_80611d78 + 0x14) + 100) - (float)zn[3]);
    zn = soundpad_resolve_zone_node(*(int*)(leaderSelf + 0x678));
    math_sqrt((double)(float)((double)(*(float*)(*(int*)(DAT_80611d78 + 0x14) + 100) - (float)zn[3]) * t + acc));

    zn = soundpad_resolve_zone_node(*(int*)(leaderSelf + 0x678));
    t = (double)(*(float*)(actor + 0x6c) - (float)zn[5]);
    zn = soundpad_resolve_zone_node(*(int*)(leaderSelf + 0x678));
    t = (double)(float)((double)(*(float*)(actor + 0x6c) - (float)zn[5]) * t);
    zn = soundpad_resolve_zone_node(*(int*)(leaderSelf + 0x678));
    acc = (double)(*(float*)(actor + 100) - (float)zn[3]);
    zn = soundpad_resolve_zone_node(*(int*)(leaderSelf + 0x678));
    math_sqrt((double)(float)((double)(*(float*)(actor + 100) - (float)zn[3]) * acc + t));

    dx = *(float*)(actor + 100) - *(float*)(*(int*)(self + 0x67c) + 0xc);
    dz = *(float*)(actor + 0x6c) - *(float*)(*(int*)(self + 0x67c) + 0x14);
    math_sqrt((double)(dx * dx + dz * dz));

    FUN_8038273c();
    return 0;
}

// ---------------------------------------------------------------------------
// 0x8027d0c8 - pull the actor toward its leader with a soft spring, then run a
// direction-alignment test when within range. Returns a register value the
// caller tests as boolean; the decompiler dropped the concrete result, so this
// returns 0 (faithful non-matching).
int tracker_leader_pull_align(void)
{
    float invLen;
    float sepSq;
    float scale;
    int actor;
    int leader;
    int self;
    double dist;
    float delta[3];

    actor = FUN_803826f0();
    self = *(int*)(actor + 0x7c);
    leader = *(int*)(DAT_80611d78 + 0x14);

    delta[0] = *(float*)(self + 0x30) - *(float*)(actor + 100);
    delta[1] = *(float*)(self + 0x34) - *(float*)(actor + 0x68);
    delta[2] = *(float*)(self + 0x38) - *(float*)(actor + 0x6c);
    sepSq = delta[2] * delta[2] + delta[0] * delta[0] + delta[1] * delta[1];
    if (FLOAT_806192e0 < sepSq) {
        invLen = 1.0f / (float)math_sqrt((double)sepSq);
        invLen = invLen * -(FLOAT_80619294 * sepSq * invLen * invLen - FLOAT_80619270);
        scale = FLOAT_80619260 * invLen * -(FLOAT_80619294 * sepSq * invLen * invLen - FLOAT_80619270);
        delta[0] = delta[0] * scale;
        delta[1] = delta[1] * scale;
        delta[2] = delta[2] * scale;
    }
    {
        float ax = *(float*)(actor + 100) - *(float*)(leader + 100);
        float ay = *(float*)(actor + 0x68) - *(float*)(leader + 0x68);
        float az = *(float*)(actor + 0x6c) - *(float*)(leader + 0x6c);
        dist = math_sqrt((double)(az * az + ax * ax + ay * ay));
    }
    if (dist < (double)FLOAT_806192ac) {
        aiactor_dir_alignment_test((double)FLOAT_806192e4, (float*)(actor + 100),
                                   delta, (float*)(leader + 100));
    }
    FUN_8038273c();
    return 0;
}

// ---------------------------------------------------------------------------
// 0x8027d210 - carried sub-object grab/attach update.
// If the active script node is opcode 2, validates the carried entity handle,
// selects a set of attach offsets (either the fixed template DAT_803e0568.. or
// a bbox-derived set for game mode 4), then walks the grab slot ring to find a
// matching valid slot and commits the attachment.
void tracker_grab_attach_update(unsigned long long a, double b)
{
    float fx;
    float fz;
    bool doAttach;
    int* tmplSrc;
    int* tmplDst;
    int tmp;
    int actor;
    int node;
    unsigned int* slotPtr;
    unsigned int handle;
    int* offsets;
    int carriedRec;
    int slotVal;
    int i;
    int off;
    int iter;
    unsigned int matchSlot;
    unsigned int prevSlot;
    unsigned int gen;
    int rec2;
    double d2 = b;
    double param2 = b;
    (void)a;

    int local_44, local_40, local_3c, local_38;
    int local_34[13];

    actor = FUN_803826dc();
    int self = *(int*)(actor + 0x7c);
    node = *(int*)(DAT_8061208c + 0x3c) + *(int*)(self + 0x9ec) * 0x48;
    if (*(int*)(node + 8) != 2) {
        goto done;
    }

    handle = (unsigned int)DAT_80612a18[0x302];
    if (handle == 0) {
        carriedRec = 0;
    }
    else {
        carriedRec = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
        if (*(unsigned int*)(carriedRec + 0x2c) != handle) {
            carriedRec = 0;
            DAT_80612a18[0x302] = 0;
        }
    }

    slotVal = subobj_carried_slot_value(DAT_80612a18);
    offsets = (int*)0;
    doAttach = false;

    // copy the 4-int attach template DAT_803e0568..0574 into local_44..local_38
    local_44 = DAT_803e0568;
    local_40 = DAT_803e056c;
    local_3c = DAT_803e0570;
    local_38 = DAT_803e0574;
    iter = 2;
    tmplSrc = &DAT_803e0574;
    tmplDst = &local_38;
    do {
        int* dstCur = tmplDst;
        int* srcCur = tmplSrc;
        tmp = srcCur[2];
        dstCur[1] = srcCur[1];
        dstCur[2] = tmp;
        iter = iter - 1;
        tmplSrc = srcCur + 2;
        tmplDst = dstCur + 2;
    } while (iter != 0);
    tmplDst[3] = tmplSrc[3];

    if (carriedRec != 0) {
        unsigned int flags = *(unsigned int*)(node + 0x34);
        if ((flags & 0x8000000) == 0) {
            if (((flags & 0x4000000) != 0) && (-1 < slotVal)) {
                if ((slotVal == 0) ||
                    (tmp = subobj_carry_nav_targets_valid(carriedRec), tmp == 0)) {
                    doAttach = true;
                    offsets = &local_44;
                }
                else if (DAT_80612614 == 4) {
                    fx = *(float*)(carriedRec + 100) + *(float*)(carriedRec + 0x1a8);
                    float minX = DAT_803e058c;
                    fz = *(float*)(carriedRec + 0x6c) + *(float*)(carriedRec + 0x1b0);
                    param2 = (double)(fz + *(float*)(carriedRec + 0x1c8));
                    d2 = (double)(fz - *(float*)(carriedRec + 0x1c8));
                    float minZ = DAT_803e0590;
                    float maxX = DAT_803e0594;
                    float maxZ = DAT_803e0598;
                    (void)minZ; (void)maxZ;
                    if ((((fx - *(float*)(carriedRec + 0x1c0) <= minX) ||
                          (maxX <= fx + *(float*)(carriedRec + 0x1c0))) ||
                         (d2 <= (double)DAT_803e0590)) || ((double)DAT_803e0598 <= param2)) {
                        offsets = local_34;
                        doAttach = true;
                    }
                    else {
                        doAttach = false;
                        offsets = (int*)0;
                    }
                }
                else {
                    offsets = local_34;
                    doAttach = true;
                }
            }
        }
        else if (slotVal == 0) {
            offsets = &local_44;
            doAttach = true;
        }
    }

    if (doAttach && (offsets != (int*)0)) {
        i = 0;
        off = 0;
        prevSlot = 0xffffffff;
        gen = (unsigned int)DAT_80612a18[0x302];
        if (gen == 0) {
            rec2 = 0;
        }
        else {
            rec2 = DAT_80611c24 + (gen >> 0x16) * 0x2e8;
            if (*(unsigned int*)(rec2 + 0x2c) != gen) {
                DAT_80612a18[0x302] = 0;
                rec2 = 0;
            }
        }
        matchSlot = prevSlot;
        do {
            unsigned int cursor = 0;
            do {
                matchSlot = cursor;
                slotPtr = (unsigned int*)subobj_grab_slot_ring_advance(rec2, &cursor);
                if (slotPtr[0xc] == *(unsigned int*)((int)offsets + off)) {
                    unsigned int h2 = *slotPtr;
                    int rec3;
                    if (h2 == 0) {
                        rec3 = 0;
                    }
                    else {
                        rec3 = DAT_80611c24 + (h2 >> 0x16) * 0x2e8;
                        if (*(unsigned int*)(rec3 + 0x2c) != h2) {
                            rec3 = 0;
                            *slotPtr = 0;
                        }
                    }
                    if (rec3 == 0) break;
                }
                matchSlot = prevSlot;
            } while (cursor != 0);
            i = i + 1;
            off = off + 4;
        } while (((matchSlot == 0xffffffff) && (i < 3)) &&
                 (prevSlot = matchSlot, -1 < *(int*)((int)offsets + off)));

        subobj_grab_commit_attach(d2, param2, rec2, actor, matchSlot);

        if (*(int*)(self + 0xaac) == 1 && *(int*)(self + 0xaa8) == 0) {
            unsigned int w;
            if (DAT_805015bd == '\n')
                w = *(unsigned int*)(self + 0x14);
            else
                w = *(unsigned int*)(self + 0x1c);
            if ((w & 1) == 0) goto done;
        }
        *(int*)(self + 0xaac) = 0x8000;
        *(int*)(self + 0xaa8) = 0;
    }

done:
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// 0x8027d590 - is the current script node's worm-event class active?
int tracker_script_event_active(int actor)
{
    return worm_event_class_is_active(
        *(int*)(*(int*)(DAT_8061208c + 0x3c) +
                *(int*)(*(int*)(actor + 0x7c) + 0x9ec) * 0x48 + 0x14));
}

// ---------------------------------------------------------------------------
// 0x8027d640 - does the leader's current zone match the script node's
// zone-set A (fields +0x14/+0x18/+0x1c/+0x20)? Also returns 1 (pass) when the
// actor is beyond the grab range from the leader.
int tracker_zone_matches_nodeset_a(int actor)
{
    float dx;
    float dy;
    float dz;
    int result;
    int zoneId;
    int leaderZone;
    int self;
    int leader = *DAT_80612a18;

    dx = *(float*)(actor + 100) - *(float*)(leader + 100);
    dy = *(float*)(actor + 0x68) - *(float*)(leader + 0x68);
    dz = *(float*)(actor + 0x6c) - *(float*)(leader + 0x6c);
    self = *(int*)(actor + 0x7c);
    leaderZone = DAT_80612a18[0x19e];

    if (dz * dz + dx * dx + dy * dy <= FLOAT_80612a24) {
        zoneId = soundpad_find_zone_by_id(
            *(int*)(*(int*)(DAT_8061208c + 0x3c) + *(int*)(self + 0x9ec) * 0x48 + 0x14));
        if (leaderZone == zoneId) {
            result = 1;
        }
        else {
            zoneId = *(int*)(*(int*)(DAT_8061208c + 0x3c) + *(int*)(self + 0x9ec) * 0x48 + 0x18);
            if ((zoneId == -1) || (zoneId = soundpad_find_zone_by_id(zoneId), leaderZone != zoneId)) {
                zoneId = *(int*)(*(int*)(DAT_8061208c + 0x3c) + *(int*)(self + 0x9ec) * 0x48 + 0x1c);
                if ((zoneId == -1) || (zoneId = soundpad_find_zone_by_id(zoneId), leaderZone != zoneId)) {
                    self = *(int*)(*(int*)(DAT_8061208c + 0x3c) + *(int*)(self + 0x9ec) * 0x48 + 0x20);
                    if ((self == -1) || (self = soundpad_find_zone_by_id(self), leaderZone != self)) {
                        result = 0;
                    }
                    else {
                        result = 1;
                    }
                }
                else {
                    result = 1;
                }
            }
            else {
                result = 1;
            }
        }
    }
    else {
        result = 1;
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x8027d794 - thin wrapper: read the actor's active anim-slot user flags.
void tracker_actor_anim_userflags(int actor)
{
    anim_active_slot_userflags(*(int*)(actor + 4));
}

// ---------------------------------------------------------------------------
// 0x8027d82c - like nodeset_a but for zone-set B (fields +0x18/+0x1c/+0x20).
int tracker_zone_matches_nodeset_b(int actor)
{
    float dx;
    float dy;
    float dz;
    int result;
    int zoneId;
    int self;
    int leaderZone;
    int leader = *DAT_80612a18;

    dx = *(float*)(actor + 100) - *(float*)(leader + 100);
    dy = *(float*)(actor + 0x68) - *(float*)(leader + 0x68);
    dz = *(float*)(actor + 0x6c) - *(float*)(leader + 0x6c);
    self = *(int*)(actor + 0x7c);
    leaderZone = DAT_80612a18[0x19e];

    if (dz * dz + dx * dx + dy * dy <= FLOAT_80612a24) {
        zoneId = soundpad_find_zone_by_id(
            *(int*)(*(int*)(DAT_8061208c + 0x3c) + *(int*)(self + 0x9ec) * 0x48 + 0x18));
        if (leaderZone == zoneId) {
            result = 1;
        }
        else {
            zoneId = *(int*)(*(int*)(DAT_8061208c + 0x3c) + *(int*)(self + 0x9ec) * 0x48 + 0x1c);
            if ((zoneId == -1) || (zoneId = soundpad_find_zone_by_id(zoneId), leaderZone != zoneId)) {
                self = *(int*)(*(int*)(DAT_8061208c + 0x3c) + *(int*)(self + 0x9ec) * 0x48 + 0x20);
                if ((self == -1) || (self = soundpad_find_zone_by_id(self), leaderZone != self)) {
                    result = 0;
                }
                else {
                    result = 1;
                }
            }
            else {
                result = 1;
            }
        }
    }
    else {
        result = 1;
    }
    return result;
}

// ===========================================================================
// batch_755: scripted-actor zone target scan + clip playback layer
// Address range: 0x8027d94c - 0x8027e783
//
// This block sits directly below the main scripted-behavior FSM. It provides:
//  * the per-frame "current zone" ring cursor + zone-visible flag setter,
//  * a family of helpers that resolve the actor's current script-node zone
//    entity handle and pick targets from it (nearest tagged, random tagged,
//    resolve-all, active-player list),
//  * two brute-force nearest-actor scanners over the active-player pointer
//    array DAT_80588610 (one hostility-filtered, one relation/recent-damage
//    filtered), and
//  * the locomotion / idle / turn animation clip drivers that select a clip id
//    from the per-mode clip table (indexed by tracker_mode_to_clip_class) and
//    play it if not already active.
// ===========================================================================

// tracker_advance_zone_ring_cursor (0x8027d94c): walk the actor's active zone
// node list looking for the first node whose id exceeds the actor's current
// node index (+0x9ec); if found, advance the shared ring cursor DAT_805cbbb8
// (wrapping within +4 of that node, clamped to the node count) and mark the
// resolved zone descriptor (and up to 4 linked zones) "visible" (+0x3e = 1)
// when it matches the leader's current zone.
unsigned long long tracker_advance_zone_ring_cursor(int actor)
{
    int idx;
    int off;
    int leaderZone;
    int found;
    int remaining;
    int rec;
    int zoneId;

    idx = 0;
    off = 0;
    leaderZone = *(int*)(DAT_80612a18 + 0x678); // leader current zone
    found = -1;
    remaining = DAT_805cbbc8;
    if (0 < DAT_805cbbc8) {
        do {
            found = idx;
            if (*(int*)(*(int*)(actor + 0x7c) + 0x9ec) <= *(int*)(DAT_805cbbd8 + off)) break;
            idx = idx + 1;
            off = off + 4;
            remaining = remaining + -1;
            found = -1;
        } while (remaining != 0);
    }
    if (found != -1) {
        int cap = found + 4;
        if (DAT_805cbbc8 <= cap) {
            cap = DAT_805cbbc8 + -1;
        }
        DAT_805cbbb8 = DAT_805cbbb8 + 1;
        if (cap < DAT_805cbbb8) {
            DAT_805cbbb8 = found;
        }
        off = *(int*)(DAT_805cbbd8 + DAT_805cbbb8 * 4) * 0x48;
        rec = *(int*)(DAT_8061208c + 0x3c) + off;
        if (*(short*)(rec + 0x3e) == 0) {
            if ((*(int*)(rec + 0xc) == 3) ||
                (zoneId = soundpad_find_zone_by_id(*(int*)(rec + 0x14)), leaderZone != zoneId)) {
                zoneId = *(int*)(*(int*)(DAT_8061208c + 0x3c) + off + 0x18);
                if ((zoneId == -1) ||
                    (zoneId = soundpad_find_zone_by_id(zoneId), leaderZone != zoneId)) {
                    zoneId = *(int*)(*(int*)(DAT_8061208c + 0x3c) + off + 0x1c);
                    if ((zoneId == -1) ||
                        (zoneId = soundpad_find_zone_by_id(zoneId), leaderZone != zoneId)) {
                        zoneId = *(int*)(*(int*)(DAT_8061208c + 0x3c) + off + 0x20);
                        if ((zoneId != -1) &&
                            (zoneId = soundpad_find_zone_by_id(zoneId), leaderZone == zoneId)) {
                            *(short*)(*(int*)(DAT_8061208c + 0x3c) + off + 0x3e) = 1;
                        }
                    }
                    else {
                        *(short*)(*(int*)(DAT_8061208c + 0x3c) + off + 0x3e) = 1;
                    }
                }
                else {
                    *(short*)(*(int*)(DAT_8061208c + 0x3c) + off + 0x3e) = 1;
                }
            }
            else {
                *(short*)(*(int*)(DAT_8061208c + 0x3c) + off + 0x3e) = 1;
            }
        }
    }
    return 0;
}

// tracker_resolve_current_zone_entity (0x8027db24): resolve the packed entity
// handle stored in the actor's current script-node zone descriptor (+0x14).
void tracker_resolve_current_zone_entity(int actor)
{
    entity_handle_resolve(
        *(unsigned int*)(*(int*)(DAT_8061208c + 0x3c) +
                         *(int*)(*(int*)(actor + 0x7c) + 0x9ec) * 0x48 + 0x14));
    return;
}

// tracker_find_nearest_tagged_in_zone (0x8027db60): find the nearest tagged
// member of the actor's current-zone tag-set relative to the actor position
// (+0x64), mode 1 (nearest).
void tracker_find_nearest_tagged_in_zone(int actor)
{
    entity_tag_find_nearest(
        *(int*)(*(int*)(DAT_8061208c + 0x3c) +
                *(int*)(*(int*)(actor + 0x7c) + 0x9ec) * 0x48 + 0x14),
        (float*)(actor + 100), 1);
    return;
}

// tracker_scan_nearest_hostile_actor (0x8027dba4): brute-force scan of the
// active-player pointer array DAT_80588610 for the nearest actor that (a) is on
// a different team, (b) is beyond the local-player range (index >= DAT_80612610),
// (c) currently occupies this actor's target zone entity, (d) passes the weapon/
// posture validity gate, and (e) is hostile per the relation table. Tracks the
// nearest hostile (updates the scan record via the lock/commit intrinsics) and
// separately the nearest non-hostile squared distance.
void tracker_scan_nearest_hostile_actor(void)
{
    float dx, dy, dz;
    int actor;
    int rel;
    int self;
    int* cand;
    int* selfRec;
    int i;
    int off;
    double best;      // nearest non-hostile
    double bestHostile;
    double d2;

    actor = FUN_803826dc();
    best = (double)FLOAT_80619280;
    selfRec = *(int**)(actor + 0x7c);
    int chosen = -1;
    off = 0;
    bestHostile = best;
    for (i = 0; i < DAT_80612098; i = i + 1) {
        cand = *(int**)((int)&DAT_80588610 + off);
        if (((cand[2] != selfRec[2]) && (DAT_80612610 <= i)) &&
            (*(int*)(cand[0x2fc] + 0x28) ==
             *(int*)(*(int*)(DAT_8061208c + 0x3c) +
                     *(int*)(*(int*)(actor + 0x7c) + 0x9ec) * 0x48 + 0x14)) &&
            ((cand[9] & 8U) == 0 &&
             (((cand[0x31b] & 4U) == 0 ||
               (self = player_ai_is_valid_weapon_type((int)*(short*)(cand + 0x12)), self != 0)) ||
              ((self = FUN_80364290((int)cand, 2), self != 0 || ((cand[10] & 0x4000000U) != 0)))))) {
            char* relByte = (char*)aiactor_relation_entry(selfRec[2], cand[2]);
            if (*relByte == '\0') {
                if (chosen == -1) {
                    rel = *cand;
                    self = *selfRec;
                    dx = *(float*)(self + 100) - *(float*)(rel + 100);
                    dy = *(float*)(self + 0x68) - *(float*)(rel + 0x68);
                    dz = *(float*)(self + 0x6c) - *(float*)(rel + 0x6c);
                    d2 = (double)(dz * dz + dx * dx + dy * dy);
                    if (d2 < best) {
                        best = d2;
                    }
                }
            }
            else {
                rel = *cand;
                self = *selfRec;
                dx = *(float*)(self + 100) - *(float*)(rel + 100);
                dy = *(float*)(self + 0x68) - *(float*)(rel + 0x68);
                dz = *(float*)(self + 0x6c) - *(float*)(rel + 0x6c);
                d2 = (double)(dz * dz + dx * dx + dy * dy);
                if (selfRec[1] == rel) {
                    d2 = (double)(float)(d2 * (double)FLOAT_806192a0);
                }
                if (d2 < bestHostile) {
                    chosen = i;
                    bestHostile = d2;
                }
            }
        }
        off = off + 4;
    }
    FUN_80382728();
    return;
}

// tracker_pick_random_tagged_in_zone (0x8027dde0): pick a random tagged member
// of the actor's current-zone tag-set.
void tracker_pick_random_tagged_in_zone(int actor)
{
    entity_tag_pick_random(
        *(int*)(*(int*)(DAT_8061208c + 0x3c) +
                *(int*)(*(int*)(actor + 0x7c) + 0x9ec) * 0x48 + 0x14));
    return;
}

// tracker_collect_zone_targets (0x8027de1c): fill outBuf (cap entries) with
// candidate target handles for the actor.
//   selector == 1  : all active non-self players (from DAT_80588610).
//   0 <= selector<1: (== 0) delegate to entity_tag_resolve_all on the tag id.
//   otherwise      : none.
// Returns the number written.
int tracker_collect_zone_targets(int actor, int selector, int tagId, int outBuf, int cap)
{
    int count;
    int self;
    int off;
    int written;
    int i;

    if (selector == 1) {
        self = *(int*)(actor + 0x7c);
        off = DAT_80612610 << 2;
        count = 0;
        written = 0;
        i = DAT_80612610;
        while ((i < DAT_80612098 && (count < cap))) {
            if (*(int*)(self + 8) != (*(int**)((int)&DAT_80588610 + off))[2]) {
                count = count + 1;
                *(int*)(outBuf + written) = **(int**)((int)&DAT_80588610 + off);
                written = written + 4;
            }
            i = i + 1;
            off = off + 4;
        }
    }
    else if ((selector < 1) && (-1 < selector)) {
        count = entity_tag_resolve_all(tagId, outBuf, cap);
    }
    else {
        count = 0;
    }
    return count;
}

// tracker_scan_nearest_relation_actor (0x8027decc): variant nearest-actor scan
// over DAT_80588610. Selects actors that are alive, on a different class group
// (+0x21), not filtered out by loadout flags (fields depend on DAT_805015bd
// story mode), not flagged 0x80000 in their control block, and either the
// packed lock context flags it in-range OR the relation table shows a hostile
// relation with recent damage (< 30 ticks). Tracks the nearest squared distance
// only (no chosen index returned - result is stashed by the lock/commit pair).
unsigned long long tracker_scan_nearest_relation_actor(void)
{
    float dx, dy, dz;
    int actor;
    char* relByte;
    int relRec;
    unsigned int lo;
    unsigned int hi;
    int i;
    int off;
    int* cand;
    int* selfRec;
    double best;
    double d2;
    unsigned long long packed;

    packed = FUN_803826d8();
    actor = (int)((unsigned long long)packed >> 0x20);
    selfRec = *(int**)(actor + 0x7c);
    best = (double)FLOAT_80619280;
    off = 0;
    for (i = 0; i < DAT_80612098; i = i + 1) {
        cand = *(int**)((int)&DAT_80588610 + off);
        if ((((cand != selfRec) && (cand[0x21] != selfRec[0x21])) && (*cand != 0)) &&
            (*(int*)(*cand + 0x150) == 0)) {
            if (DAT_805015bd == '\n') {
                lo = cand[4];
                hi = cand[5];
            }
            else {
                lo = cand[6];
                hi = cand[7];
            }
            if ((((hi & 0x400001) == 0 && (lo & 1) == 0) &&
                 ((*(unsigned int*)(cand[0x2fc] + 0x58) & 0x80000) == 0)) &&
                (((int)packed != 0 ||
                  ((relByte = (char*)aiactor_relation_entry(cand[2], selfRec[2]), *relByte != '\0' &&
                    (relRec = aiactor_relation_entry(cand[2], selfRec[2]),
                     DAT_80611928 - *(int*)(relRec + 4) < 0x1e)))))) {
                relRec = *cand;
                dx = *(float*)(actor + 100) - *(float*)(relRec + 100);
                dy = *(float*)(actor + 0x68) - *(float*)(relRec + 0x68);
                dz = *(float*)(actor + 0x6c) - *(float*)(relRec + 0x6c);
                d2 = (double)(dz * dz + dx * dx + dy * dy);
                if (d2 < best) {
                    best = d2;
                }
            }
        }
        off = off + 4;
    }
    FUN_80382724();
    return 0;
}

// tracker_mode_to_clip_class (0x8027e4d4): map the current level/behavior mode
// id (DAT_80612614) to a clip-class table row index (0..0xc). Rows not listed
// fall through to 0.
int tracker_mode_to_clip_class(void)
{
    switch (DAT_80612614) {
    default:   return 0;
    case 1:    return 4;
    case 2:    return 10;
    case 4:    return 0;
    case 5:    return 8;
    case 6:    return 6;
    case 9:    return 1;
    case 0xb:  return 2;
    case 0xe:  return 9;
    case 0x11: return 5;
    case 0x12: return 3;
    case 0x16: return 7;
    case 0x1a: return 0xb;
    case 0x51: return 0xc;
    }
}

// tracker_play_idle_variant_clip (0x8027e068): play the mode-specific idle clip
// (column +0x00 normal / +0x0c variant, selected by DAT_80612a34) if it is not
// already active.
void tracker_play_idle_variant_clip(void)
{
    int actor;
    int cls;
    int active;

    actor = FUN_803826f0();
    cls = tracker_mode_to_clip_class();
    if (DAT_80612a34 == 0) {
        active = anim_is_clip_active(actor, *(int*)((char*)&DAT_80472f10 + cls * 0x24));
        if (active == 0) {
            anim_play_scaled((double)*(float*)(actor + 4), (double)FLOAT_80619260);
        }
    }
    else {
        active = anim_is_clip_active(actor, *(int*)((char*)&DAT_80472f1c + cls * 0x24));
        if (active == 0) {
            anim_play_scaled((double)*(float*)(actor + 4), (double)FLOAT_80619260);
        }
    }
    FUN_8038273c();
    return;
}

// tracker_play_locomotion_clip (0x8027e12c): main per-mode locomotion clip
// driver. Sets stance flag 1, picks the base clip from the per-mode clip table
// (columns +0x08/+0x14 by DAT_80612a34), and when no explicit look-at target is
// pending (DAT_80612a2c/DAT_80612a28 == -1) it consults the tracker distance
// probe to swap between run/walk/slow clips, forcing a state transition to 0xd
// (probe==2). If a look-at target is pending it plays that clip instead. Plays
// the chosen clip when not already active.
void tracker_play_locomotion_clip(void)
{
    int actor;
    int cls;
    int self;
    int active;
    int clip;
    int probe;

    actor = __save_gpr_28();
    cls = tracker_mode_to_clip_class();
    self = *(int*)(actor + 0x7c);
    aiactor_set_stance_flags(actor, 1);
    if (DAT_80612a2c == -1) {
        if (DAT_80612a28 == -1) {
            if (DAT_80612a34 == 0) {
                clip = *(int*)((char*)&DAT_80472f18 + cls * 0x24);
            }
            else {
                clip = *(int*)((char*)&DAT_80472f24 + cls * 0x24);
            }
            if (((*(unsigned int*)(*(int*)(DAT_8061208c + 0x3c) +
                                   *(int*)(self + 0x9ec) * 0x48 + 0x34) & 0x200000) != 0) &&
                ((*(unsigned int*)(self + 0x14) & 0x2000) != 0)) {
                probe = tracker_debug_distance_probe();
                if (probe == 2) {
                    if (*(int*)(self + 0x20) != 0xd) {
                        *(int*)(self + 0x9d0) = 0xd;
                    }
                    goto LAB_done;
                }
                if (probe < 2) {
                    if (probe == 0) {
                        anim_set_clip_speed_fields((double)FLOAT_80619260, *(int**)(actor + 4));
                    }
                    else if (-1 < probe) {
                        if (DAT_80612a34 == 0) {
                            clip = *(int*)((char*)&DAT_80472f14 + cls * 0x24);
                        }
                        else {
                            clip = *(int*)((char*)&DAT_80472f20 + cls * 0x24);
                        }
                    }
                }
                else if (probe < 4) {
                    anim_set_clip_speed_fields((double)FLOAT_806192e8, *(int**)(actor + 4));
                }
            }
            active = anim_is_clip_active(actor, clip);
            if (active == 0) {
                anim_play_scaled((double)FLOAT_80619268, (double)FLOAT_80619260);
            }
        }
        else {
            active = anim_is_clip_active(actor, DAT_80612a28);
            if ((((active == 0) &&
                  (active = anim_play_scaled((double)FLOAT_80619268, (double)FLOAT_80619260),
                   active != 1)) &&
                 (active != 0)) && (active = anim_is_clip_active(actor, 0x2a), active != 0)) {
                anim_play_scaled((double)FLOAT_80619268, (double)FLOAT_80619260);
            }
        }
    }
    else {
        active = anim_is_clip_active(actor, DAT_80612a2c);
        if (((active == 0) &&
             (active = anim_play_scaled((double)FLOAT_80619268, (double)FLOAT_80619260),
              active != 1)) &&
            ((active != 0 && (active = anim_is_clip_active(actor, 0x26), active != 0)))) {
            anim_play_scaled((double)FLOAT_80619268, (double)FLOAT_80619260);
        }
    }
LAB_done:
    __restore_gpr_28();
    return;
}

// tracker_play_turn_clip (0x8027e568): turn/facing clip driver. When the turn
// system is enabled (DAT_80612a08 == 0) it checks whether any of the four turn
// clips (0x2a-0x2d) are active; if so it updates the leader zone and may force a
// state transition to 0xd. It then, if the actor or the leader record's turn
// context is armed, computes the wrapped yaw error to the leader (via
// prop_hinge_wrap_angle_to_range) and selects the appropriate turn clip
// (in-place / left / right / large) to play if not already active.
int tracker_play_turn_clip(void)
{
    int actor;
    int self;
    int leaderCtx;
    int active;
    double yawErr;
    double fwdThresh;
    double bigThresh;

    actor = FUN_803826f0();
    self = *(int*)(actor + 0x7c);
    leaderCtx = *(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c);
    if (DAT_80612a08 == 0) {
        active = anim_is_clip_active(actor, 0x2b);
        if ((((active == 0) && (active = anim_is_clip_active(actor, 0x2a), active == 0)) &&
             (active = anim_is_clip_active(actor, 0x2d), active == 0)) &&
            (active = anim_is_clip_active(actor, 0x2c), active == 0)) {
            DAT_80612a10 = 0;
        }
        else {
            aiactor_leader_zone_update(actor);
            active = anim_active_slot_userflags(*(int*)(actor + 4));
            if (((active != 0) && (DAT_80612a10 != 0)) && (*(int*)(self + 0x20) != 0xd)) {
                *(int*)(self + 0x9d0) = 0xd;
            }
        }
        if (((*(int*)(self + 0xc1c) != 0) && (*(int*)(self + 0xc20) < DAT_80612610)) ||
            ((*(int*)(leaderCtx + 0xc1c) != 0 &&
              (*(int*)(leaderCtx + 0xc20) == *(int*)(self + 8))))) {
            yawErr = prop_hinge_wrap_angle_to_range(
                (double)*(float*)(actor + 0x70), (float*)(actor + 100),
                (float*)(DAT_80611d78 + 0xa8));
            fwdThresh = (double)FLOAT_8061925c;
            bigThresh = (double)FLOAT_806192ec;
            *(float*)(self + 0xb34) = FLOAT_8061925c;
            DAT_80612a10 = 1;
            if (ABS(yawErr) <= bigThresh) {
                if (ABS(yawErr) <= (double)FLOAT_806192f0) {
                    actor = anim_is_clip_active(actor, 0x2b);
                    if (actor == 0) {
                        anim_play_scaled((double)FLOAT_80619268, (double)FLOAT_80619260);
                    }
                }
                else if (yawErr <= fwdThresh) {
                    actor = anim_is_clip_active(actor, 0x2c);
                    if (actor == 0) {
                        anim_play_scaled((double)FLOAT_80619268, (double)FLOAT_80619260);
                    }
                }
                else {
                    actor = anim_is_clip_active(actor, 0x2d);
                    if (actor == 0) {
                        anim_play_scaled((double)FLOAT_80619268, (double)FLOAT_80619260);
                    }
                }
            }
            else {
                actor = anim_is_clip_active(actor, 0x2a);
                if (actor == 0) {
                    anim_play_scaled((double)FLOAT_80619268, (double)FLOAT_80619260);
                }
            }
        }
    }
    FUN_8038273c();
    return 0;
}

// ===========================================================================
// batch_756: scripted-actor node binding + co-op level-tag matching + more
//            clip drivers and script-event handlers.
// Address range: 0x8027e784 - 0x8027efdf
// ===========================================================================

// tracker_build_zone_index_list (0x8027e784): rebuild the compact zone-index
// list DAT_805cbbd8 (capped at 100 entries) from the active script descriptor
// block: for each 0x48-byte node record, if its type (+0xc) is 1 or 3 record
// its ordinal index. Terminates at the sentinel node type 0xc.
void tracker_build_zone_index_list(void)
{
    int off;
    int type;
    int ord;

    off = 0;
    DAT_805cbbc8 = 0;
    ord = 0;
    do {
        type = *(int*)(*(int*)(DAT_8061208c + 0x3c) + off + 0xc);
        if ((type == 1) || (type == 3)) {
            *(int*)(DAT_805cbbd8 + DAT_805cbbc8 * 4) = ord;
            if (DAT_805cbbc8 < 100) {
                DAT_805cbbc8 = DAT_805cbbc8 + 1;
            }
        }
        type = off + 0x50; // peek next node's type field (+0xc of next 0x48 rec == off+0x50 here? no: +0x48+0x8)
        ord = ord + 1;
        off = off + 0x48;
    } while (*(int*)(*(int*)(DAT_8061208c + 0x3c) + type) != 0xc);
    return;
}

// tracker_play_crouch_special_clip (0x8027e808): drive the mode-specific
// crouch/special clip set (columns +0x18/+0x1c/+0x20). If any of the three
// clips has its "userflag" set it counts as "wants to play"; if any is already
// active it counts as "playing". Starts a new (rng-jittered) clip if not
// already playing, or a light blend if playing while wanting.
void tracker_play_crouch_special_clip(void)
{
    bool wants;
    bool playing;
    int actor;
    int col;
    int r;

    actor = FUN_803826e8();
    playing = false;
    wants = false;
    col = tracker_mode_to_clip_class();
    col = col * 0x24;
    r = anim_primary_clip_userflags(actor, *(int*)((char*)&DAT_80472f28 + col));
    if (((r != 0) ||
         (r = anim_primary_clip_userflags(actor, *(int*)((char*)&DAT_80472f2c + col)), r != 0)) ||
        (r = anim_primary_clip_userflags(actor, *(int*)((char*)&DAT_80472f30 + col)), r != 0)) {
        wants = true;
    }
    r = anim_is_clip_active(actor, *(int*)((char*)&DAT_80472f28 + col));
    if (((r != 0) ||
         (r = anim_is_clip_active(actor, *(int*)((char*)&DAT_80472f2c + col)), r != 0)) ||
        (actor = anim_is_clip_active(actor, *(int*)((char*)&DAT_80472f30 + col)), actor != 0)) {
        playing = true;
    }
    if (playing) {
        if (wants) {
            rng_counter_tick();
            anim_play_scaled((double)FLOAT_8061925c, (double)FLOAT_80619260);
        }
    }
    else {
        rng_counter_tick();
        anim_play_scaled((double)FLOAT_80619268, (double)FLOAT_80619260);
    }
    FUN_80382734();
    return;
}

// tracker_bind_script_node_by_tagchar (0x8027e970): scan the script node list
// for the node whose 4-char tag (+0x30) equals 'CPC' with the low byte set to
// ('0' + tagChar); store its ordinal as the actor's current node index (+0x9ec),
// rebuild the zone list, cache the leader record + its team byte, seed the
// actor's target from the relation scan, and clear the "needs respawn" gate.
void tracker_bind_script_node_by_tagchar(int actor, char tagChar)
{
    int ord;
    int self;
    int off;
    unsigned int wantTag;
    int target;

    ord = 0;
    self = *(int*)(actor + 0x7c);
    wantTag = ((unsigned int)(char)(tagChar + '0') | 0x43504300U);
    for (off = 0;
         (*(int*)(*(int*)(DAT_8061208c + 0x3c) + off + 8) != 0xc &&
          (wantTag != *(unsigned int*)(*(int*)(DAT_8061208c + 0x3c) + off + 0x30)));
         off = off + 0x48) {
        ord = ord + 1;
    }
    *(int*)(self + 0x9ec) = ord;
    tracker_build_zone_index_list();
    DAT_80612a18 = (int*)*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c);
    *(int*)(self + 0x84) = *(int*)((int)DAT_80612a18 + 0x84);
    target = (int)tracker_scan_nearest_relation_actor();
    *(int*)(self + 4) = target;
    DAT_80612a20 = 0;
    return;
}

// tracker_seek_script_node_by_tag (0x8027ea2c): scan the script node list for
// the node whose tag word (+0x30) equals an exact target tag; store the ordinal
// as the actor's current node index (+0x9ec). If it changed and the actor is
// not already in state 1, force a transition to state 1.
void tracker_seek_script_node_by_tag(int actor, int wantTag)
{
    int ord;
    int self;
    int off;

    ord = 0;
    self = *(int*)(actor + 0x7c);
    for (off = 0;
         (*(int*)(*(int*)(DAT_8061208c + 0x3c) + off + 8) != 0xc &&
          (wantTag != *(int*)(*(int*)(DAT_8061208c + 0x3c) + off + 0x30)));
         off = off + 0x48) {
        ord = ord + 1;
    }
    if (*(int*)(self + 0x9ec) == ord) {
        return;
    }
    *(int*)(self + 0x9ec) = ord;
    if (*(int*)(self + 0x20) == 1) {
        return;
    }
    *(int*)(self + 0x9d0) = 1;
    return;
}

// tracker_coop_scan_partner_nodes (0x8027ea98): in single-player co-op story
// (DAT_805015bd=='\n', one active player), walk the non-local actors in the
// active-player array and test each partner's bound level-node (+0xbf0) for the
// current level's co-op tag; stops at the first match. Bracketed by the lock/
// commit context intrinsics.
int tracker_coop_scan_partner_nodes(void)
{
    unsigned int hit;
    int i;
    int off;
    int actor;

    actor = FUN_803826f0();
    if ((DAT_805015bd == '\n') && (DAT_80612610 == 1)) {
        off = 4;
        i = 1;
        while ((i < DAT_80612098 &&
                (hit = tracker_level_node_is_coop_tag(
                     *(int*)(*(int*)((int)&DAT_80588610 + off) + 0xbf0)), hit == 0))) {
            i = i + 1;
            off = off + 4;
        }
    }
    FUN_8038273c();
    return actor; // r3 residue: current actor record (FSM compares against self)
}

// tracker_actor_coop_node_active (0x8027eb3c): return non-zero if the given
// actor has a bound level-node (+0xbf0) that is the current level's co-op tag,
// but only in single-player co-op story mode.
unsigned int tracker_actor_coop_node_active(int actor)
{
    unsigned int result;

    if (*(int*)(actor + 0xbf0) == 0) {
        result = 0;
    }
    else if (DAT_805015bd == '\n') {
        if (DAT_80612610 == 1) {
            result = tracker_level_node_is_coop_tag(*(int*)(actor + 0xbf0));
        }
        else {
            result = 0;
        }
    }
    else {
        result = 0;
    }
    return result;
}

// tracker_level_node_is_coop_tag (0x8027eba0): return 1 if the level node's tag
// word (+8) matches the co-op node 4CC expected for the current level/behavior
// mode (DAT_80612614), else 0. Per-mode 4CCs: 'PITH'(0x48544950) for mode 0xb;
// 'ATPC'(0x43505441) modes 4/0x11+; 'HTJB'(0x4a425448) mode 1 & 0xe..0x10;
// 'NHCA'(0x4143484e) modes 6..8 & 0x16; 'R110'(0x52313130) modes 5/9/0xe/0x51.
unsigned int tracker_level_node_is_coop_tag(int node)
{
    unsigned int clz;

    if (DAT_80612614 != 0xb) {
        if (DAT_80612614 < 0xb) {
            if (DAT_80612614 != 5) {
                if (DAT_80612614 < 5) {
                    if (DAT_80612614 != 1) {
                        if (DAT_80612614 < 1) {
                            return 0;
                        }
                        if (DAT_80612614 < 4) {
                            return 0;
                        }
                        clz = countLeadingZeros(0x43505441 - *(int*)(node + 8));
                        return clz >> 5;
                    }
LAB_htjb:
                    clz = countLeadingZeros(0x4a425448 - *(int*)(node + 8));
                    return clz >> 5;
                }
                if (DAT_80612614 != 9) {
                    if (8 < DAT_80612614) {
                        return 0;
                    }
                    if (6 < DAT_80612614) {
                        return 0;
                    }
LAB_nhca:
                    clz = countLeadingZeros(0x4143484e - *(int*)(node + 8));
                    return clz >> 5;
                }
            }
            goto LAB_r110;
        }
        if (DAT_80612614 != 0x12) {
            if (DAT_80612614 < 0x12) {
                if (DAT_80612614 == 0xe) {
LAB_r110:
                    clz = countLeadingZeros(0x52313130 - *(int*)(node + 8));
                    return clz >> 5;
                }
                if ((0xd < DAT_80612614) && (0x10 < DAT_80612614)) goto LAB_htjb;
            }
            else {
                if (DAT_80612614 == 0x51) goto LAB_r110;
                if ((DAT_80612614 < 0x51) && (DAT_80612614 == 0x16)) goto LAB_nhca;
            }
            return 0;
        }
    }
    clz = countLeadingZeros(0x48544950 - *(int*)(node + 8));
    return clz >> 5;
}

// tracker_compute_anim_scale (0x8027ecbc): compute the per-mode locomotion anim
// playback scale for an actor. Selects a per-mode rate constant (FLOAT_806192ac
// for "story" modes, FLOAT_80619260 for the rest; FLOAT_806192f4 fallback when
// zero) then scales by the actor's speed field (+0x1d8), the frame-delta ratio
// (FLOAT_80619260 / frameDelta) and the global time scale FLOAT_806108ec.
double tracker_compute_anim_scale(int actor)
{
    float rate;

    rate = FLOAT_806192ac;
    if (DAT_80612614 == 0xb) goto LAB_scale;
    rate = FLOAT_806192ac;
    if (DAT_80612614 < 0xb) {
        if (DAT_80612614 == 5) goto LAB_scale;
        if (DAT_80612614 < 5) {
            if (DAT_80612614 != 1) {
                rate = FLOAT_80619260;
                if ((0 < DAT_80612614) && (rate = FLOAT_806192ac, DAT_80612614 < 4)) {
                    rate = FLOAT_80619260;
                }
                goto LAB_scale;
            }
LAB_story:
            rate = FLOAT_806192ac;
            goto LAB_scale;
        }
        if (((DAT_80612614 == 9) || (rate = FLOAT_80619260, 8 < DAT_80612614)) ||
            (6 < DAT_80612614)) goto LAB_scale;
    }
    else {
        if (DAT_80612614 == 0x12) goto LAB_scale;
        if (DAT_80612614 < 0x12) {
            if (((DAT_80612614 == 0xe) || (rate = FLOAT_80619260, DAT_80612614 < 0xe)) ||
                (DAT_80612614 < 0x11)) goto LAB_scale;
            goto LAB_story;
        }
        if (((DAT_80612614 == 0x51) || (rate = FLOAT_80619260, 0x50 < DAT_80612614)) ||
            (DAT_80612614 != 0x16)) goto LAB_scale;
    }
    rate = FLOAT_806192ac;
LAB_scale:
    if (FLOAT_8061925c == rate) {
        rate = FLOAT_806192f4;
    }
    return (double)((*(float*)(actor + 0x1d8) *
                     (FLOAT_80619260 /
                      (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                              DOUBLE_806192c8)) *
                     FLOAT_806108ec) / rate);
}

// tracker_handle_teleport_event (0x8027edc4): script-event handler.
//   event 7: set actor "teleporting" flag (+0x28 |0x10000) and force death
//            state 0xd; return -1.
//   event 1: if the current node allows warp (+0x34 & 0x100000): floor-probe at
//            the actor's stance-relative offset, snap Y to the floor, resolve the
//            nearest zone, advance the node index, and (unless in state 1) force
//            state 1; honor a pending explicit node override (DAT_80612a0c);
//            set flag +0x28 |0x8000; return -2 (or -1 if a held clip is still
//            playing for combat states 6..0xb).
int tracker_handle_teleport_event(int actor, int event)
{
    int result;
    int self;
    int probeOff;
    int state;
    double y;

    result = 0;
    if (event == 7) {
        self = *(int*)(actor + 0x7c);
        *(unsigned int*)(self + 0x28) = *(unsigned int*)(self + 0x28) | 0x10000;
        if (*(int*)(self + 0x20) != 0xd) {
            *(int*)(self + 0x9d0) = 0xd;
        }
        result = -1;
    }
    else if ((event < 7) && (event == 1)) {
        self = *(int*)(actor + 0x7c);
        if ((*(unsigned int*)(*(int*)(DAT_8061208c + 0x3c) +
                              *(int*)(self + 0x9ec) * 0x48 + 0x34) & 0x100000) != 0) {
            probeOff = *(short*)(self + 0x48) * 0x30;
            y = (double)floor_query_with_interact(
                (double)*(float*)((char*)&DAT_8048ca94 + probeOff),
                (double)*(float*)((char*)&DAT_8048ca9c + probeOff), actor + 100, 0, (int*)0, 0);
            *(float*)(self + 0xbfc) = (float)y;
            *(float*)(actor + 0x68) = (float)y;
            *(int*)(actor + 0x34) =
                (int)floor_height_from_position((double)FLOAT_806192c0, (double)FLOAT_806192c0);
            *(int*)(self + 0x678) =
                soundpad_find_nearest_entity((double)FLOAT_8061925c, (float*)(actor + 100));
            *(int*)(self + 0x9ec) = *(int*)(self + 0x9ec) + 1;
            if (*(int*)(self + 0x20) != 1) {
                *(int*)(self + 0x9d0) = 1;
            }
        }
        if ((-1 < DAT_80612a0c) &&
            (*(int*)(self + 0x9ec) = DAT_80612a0c, *(int*)(self + 0x20) != 1)) {
            *(int*)(self + 0x9d0) = 1;
        }
        result = -2;
        *(unsigned int*)(self + 0x28) = *(unsigned int*)(self + 0x28) | 0x8000;
        state = *(int*)(self + 0x20);
        if (state != 8) {
            if (state < 8) {
                if (state < 6) {
                    return -2;
                }
            }
            else if (0xb < state) {
                return -2;
            }
            if (FLOAT_806192ac <= *(float*)(self + 0xb34)) {
                *(float*)(self + 0xb34) = FLOAT_8061925c;
            }
            else {
                result = -1;
            }
        }
    }
    return result;
}

// tracker_count_crowd_neighbors (0x8027ef54): only for the 'HTJB'(0x4a425448)
// co-op node - count how many of the param2 candidate actors (handle array at
// param3) are within the XZ crowd radius FLOAT_80619288 of the actor; return 1
// if more than one is crowding, else 0 (negative handled by sign fold).
int tracker_count_crowd_neighbors(int actor, int count, int candArray)
{
    float dx, dz;
    int off;
    unsigned int crowded;

    crowded = 0;
    if (*(int*)(*(int*)(*(int*)(actor + 0x7c) + 0xbf0) + 8) != 0x4a425448) {
        return 0;
    }
    off = 0;
    if (0 < count) {
        do {
            dx = *(float*)(*(int*)(candArray + off) + 100) - *(float*)(actor + 100);
            dz = *(float*)(*(int*)(candArray + off) + 0x6c) - *(float*)(actor + 0x6c);
            if (dx * dx + dz * dz < FLOAT_80619288) {
                crowded = crowded + 1;
            }
            off = off + 4;
            count = count + -1;
        } while (count != 0);
    }
    return ((int)crowded >> 0x1f) + (unsigned int)(1 < crowded);
}

// ===========================================================================
// batch_757: co-op destination picking, target scoring, positional sound,
//            recovery + behavior-mode arbitration.
// Address range: 0x8027efe0 - 0x80280593
// ===========================================================================

// tracker_pick_zone_destination (0x8027efe0): pick the best zone-linked entity
// as the follower's destination (+0x684). Walks the linked-zone bitmask of the
// current script node (+0x1c), collects each zone's entities, resolves their
// nodes, and scores each by (a) being far enough from every party member (>=
// FLOAT_80619290, <= FLOAT_806192f8) and (b) nearest to the leader; also
// requires no party member is closer than the crowd radius. Falls back to the
// leader's current zone. On change, clears the path-progress fields (+0x9a8/ac).
unsigned long long tracker_pick_zone_destination(int a, void* b, int candArray)
{
    float dx, dz, memBest;
    int self;
    int levelInfo;
    int nodeTbl;
    int node;
    int best;
    int count;
    int i;
    int e, ei;
    int listCount, li;
    int* zoneNode;
    int m, mi;
    int cand;
    unsigned int flags;
    double bestScore;
    double score;
    unsigned long long packed;
    int members;
    int zoneBuf[31];
    int outCount;

    (void)a; (void)b;
    packed = FUN_803826b8();
    self = (int)((unsigned long long)packed >> 0x20);
    members = (int)packed;
    bestScore = (double)FLOAT_80619280;
    best = -1;
    levelInfo = level_get_info_cached(DAT_80612614);
    int selfState = *(int*)(self + 0x7c);
    nodeTbl = *(int*)(levelInfo + 0x3c);
    node = *(int*)(selfState + 0x9ec);
    if (0 < members) {
        for (i = 0; i < *(int*)(levelInfo + 0x68); i = i + 1) {
            if ((*(unsigned int*)(nodeTbl + node * 0x48 + 0x1c) & (1 << i)) != 0) {
                count = soundpad_collect_zone_entities(i, zoneBuf, 10);
                ei = 0;
                for (e = 0; e < count; e = e + 1) {
                    listCount = soundpad_get_entity_data(*(int*)((int)zoneBuf + ei), &outCount);
                    li = 0;
                    for (int lz = 0; lz < listCount; lz = lz + 1) {
                        zoneNode = soundpad_resolve_zone_node(*(int*)((int)&outCount + li));
                        mi = 0;
                        m = members;
                        memBest = FLOAT_80619280;
                        if (0 < members) {
                            do {
                                cand = *(int*)(candArray + mi);
                                if (DAT_805015bd == '\n') {
                                    flags = *(unsigned int*)(*(int*)(cand + 0x7c) + 0x14);
                                }
                                else {
                                    flags = *(unsigned int*)(*(int*)(cand + 0x7c) + 0x1c);
                                }
                                if (((flags & 0x400001) == 0) &&
                                    (dx = (float)zoneNode[3] - *(float*)(cand + 100),
                                     dz = (float)zoneNode[5] - *(float*)(cand + 0x6c),
                                     dx = dx * dx + dz * dz, dx < memBest)) {
                                    memBest = dx;
                                }
                                mi = mi + 4;
                                m = m + -1;
                            } while (m != 0);
                        }
                        score = bestScore;
                        if ((best < 0) ||
                            (((FLOAT_80619290 <= memBest && (memBest <= FLOAT_806192f8)) &&
                              (dx = (float)zoneNode[3] - *(float*)(self + 100),
                               dz = (float)zoneNode[5] - *(float*)(self + 0x6c),
                               score = (double)(dx * dx + dz * dz), score <= bestScore)))) {
                            best = *(int*)((int)&outCount + li);
                            bestScore = score;
                        }
                        li = li + 4;
                    }
                    ei = ei + 4;
                }
            }
        }
        if (best < 0) {
            best = *(int*)((int)DAT_80612a18 + 0x678);
        }
        if ((best != *(int*)(selfState + 0x684)) ||
            (tracker_any_neighbor_crowding(self, members, candArray) != 0)) {
            if (best < 0) {
                *(int*)(selfState + 0x684) =
                    *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            }
            else {
                *(int*)(selfState + 0x684) = best;
            }
            *(int*)(selfState + 0x9ac) = 0;
            *(int*)(selfState + 0x9a8) = 0;
        }
    }
    FUN_80382704();
    return 0;
}

// tracker_any_neighbor_crowding (0x8027f258): return 1 as soon as any of the
// count candidate actors is within the XZ crowd radius FLOAT_80619288 of the
// actor, else 0.
int tracker_any_neighbor_crowding(int actor, int count, int candArray)
{
    float dx, dz;
    int off;

    off = 0;
    if (0 < count) {
        do {
            dx = *(float*)(*(int*)(candArray + off) + 100) - *(float*)(actor + 100);
            dz = *(float*)(*(int*)(candArray + off) + 0x6c) - *(float*)(actor + 0x6c);
            if (dx * dx + dz * dz < FLOAT_80619288) {
                return 1;
            }
            off = off + 4;
            count = count + -1;
        } while (count != 0);
    }
    return 0;
}

// tracker_pick_nearest_party_target (0x8027f2b4): from the candidate list, pick
// the nearest valid party member (alive, not filtered by loadout flags, not
// flagged 0x80000) and store it as the actor's target (+4). Prefers a hostile
// with recent damage (< 30 ticks) over a merely-nearest friendly.
unsigned long long tracker_pick_nearest_party_target(int a, int b, int candArray)
{
    float dx, dz;
    unsigned int loFlags, hiFlags;
    int self;
    int selfState;
    char* relByte;
    int relRec;
    int i;
    int off;
    int cand;
    int candState;
    int best;
    double bestFriendly;
    double bestHostile;
    double d2;
    unsigned long long packed;
    int members;

    (void)a; (void)b;
    packed = FUN_803826cc();
    self = (int)((unsigned long long)packed >> 0x20);
    members = (int)packed;
    bestFriendly = (double)FLOAT_80619280;
    selfState = *(int*)(self + 0x7c);
    best = 0;
    off = 0;
    bestHostile = bestFriendly;
    for (i = 0; i < members; i = i + 1) {
        cand = *(int*)(candArray + off);
        candState = *(int*)(cand + 0x7c);
        if (DAT_805015bd == '\n') {
            loFlags = *(unsigned int*)(candState + 0x10);
            hiFlags = *(unsigned int*)(candState + 0x14);
        }
        else {
            loFlags = *(unsigned int*)(candState + 0x18);
            hiFlags = *(unsigned int*)(candState + 0x1c);
        }
        if (((hiFlags & 0x400001) == 0 && (loFlags & 1) == 0) &&
            ((*(unsigned int*)(*(int*)(candState + 0xbf0) + 0x58) & 0x80000) == 0)) {
            relByte = (char*)aiactor_relation_entry(*(int*)(candState + 8), *(int*)(selfState + 8));
            if ((*relByte == '\0') ||
                (relRec = aiactor_relation_entry(*(int*)(candState + 8), *(int*)(selfState + 8)),
                 0x1d < DAT_80611928 - *(int*)(relRec + 4))) {
                if ((best == 0) &&
                    (dx = *(float*)(self + 100) - *(float*)(cand + 100),
                     dz = *(float*)(self + 0x6c) - *(float*)(cand + 0x6c),
                     d2 = (double)(dx * dx + dz * dz), d2 < bestFriendly)) {
                    best = cand;
                    bestFriendly = d2;
                }
            }
            else {
                dx = *(float*)(self + 100) - *(float*)(cand + 100);
                dz = *(float*)(self + 0x6c) - *(float*)(cand + 0x6c);
                d2 = (double)(dx * dx + dz * dz);
                if (d2 < bestHostile) {
                    best = cand;
                    bestHostile = d2;
                }
            }
        }
        off = off + 4;
    }
    if (best != 0) {
        *(int*)(selfState + 4) = best;
    }
    FUN_80382718();
    return 0;
}

// tracker_node_spawn_condition_met (0x8027f468): test whether the current script
// node's spawn condition is satisfied. If the node is not flagged "no-spawn"
// (+0x34 & 0x10000000) and its spawn-type (+0x14) is a "count match" type, it
// compares the required count against the active spawnpoint count for the zone.
unsigned int tracker_node_spawn_condition_met(int actor)
{
    int node;
    int spawnType;
    unsigned int result;
    int activeCount;
    int wantCount; // required count from the node record (register residue in the IDB)

    node = *(int*)(DAT_8061208c + 0x3c) + *(int*)(*(int*)(actor + 0x7c) + 0x9ec) * 0x48;
    if ((*(unsigned int*)(node + 0x34) & 0x10000000) == 0) {
        spawnType = *(int*)(node + 0x14);
        if (spawnType == 1) {
            result = 0;
        }
        else if ((spawnType < 1) && (-1 < spawnType)) {
            wantCount = 0; // IDB reads an uninitialised local here; keep the CLZ==0 equality shape
            activeCount = spawnpoint_count_active_for_zone();
            result = countLeadingZeros((unsigned int)(wantCount - activeCount));
            result = result >> 5;
        }
        else {
            result = 0;
        }
    }
    else {
        result = 0;
    }
    return result;
}

// tracker_play_positional_sound (0x8027f4f8): fire a positional sound request at
// the supplied 3D position (pos = xyz), owner = current actor, with flags 0xa0.
void tracker_play_positional_sound(int a, int b, void* posv)
{
    int* pos = (int*)posv;
    int actor;
    float rate;
    long long params[2];
    unsigned int fields;
    float level;
    int px, py, pz;
    unsigned char one;

    (void)a; (void)b;
    actor = (int)FUN_803826f0();
    rate = 0.0f; // extraout_f1 (actor default sound rate) - non-matching residue
    sound_init_play_params((int)params);
    one = 1;
    fields = 0;
    fields = fields | 0xa0;
    level = rate;
    px = pos[0];
    py = pos[1];
    pz = pos[2];
    params[0] = (long long)actor;
    (void)one; (void)fields; (void)level; (void)px; (void)py; (void)pz;
    sound_play_request((int*)params);
    FUN_8038273c();
    return;
}

// tracker_clamp (0x8027f58c): return clamp(v, lo, hi) == min(max(v, lo), hi).
double tracker_clamp(double v, double lo, double hi)
{
    double r;

    r = v;
    if (hi < v) {
        r = hi;
    }
    if (lo <= r) {
        if (hi < v) {
            return hi;
        }
        return v;
    }
    return lo;
}

// tracker_run_coop_behavior_phases (0x8027f5c0): per-frame co-op behavior runner.
// Resets state to 0, temporarily swaps in the co-op override flags (+0xab0/ab4),
// runs behaviour phases 0x3ea then 0x3e9, restores the flags, and (if the node
// wants a stance and no combat clip is active) ticks the combat behavior and
// sets stance flag 8. Finally clears any stale zone-lock references (+0x9c0/9c4).
void tracker_run_coop_behavior_phases(void)
{
    int actor;
    int self;
    int lo, hi;
    int zone;

    actor = FUN_803826e8();
    self = *(int*)(actor + 0x7c);
    if (*(int*)(self + 0x20) != 0) {
        *(int*)(self + 0x9d0) = 0;
    }
    if (DAT_805015bd == '\n') {
        lo = *(int*)(self + 0x10);
        hi = *(int*)(self + 0x14);
        *(int*)(self + 0x14) = *(int*)(self + 0xab4);
        *(int*)(self + 0x10) = *(int*)(self + 0xab0);
    }
    else {
        lo = *(int*)(self + 0x18);
        hi = *(int*)(self + 0x1c);
        *(int*)(self + 0x1c) = *(int*)(self + 0xab4);
        *(int*)(self + 0x18) = *(int*)(self + 0xab0);
    }
    aiactor_run_behaviour_phase(actor, 0x3ea, (int*)0, 0);
    if (DAT_805015bd == '\n') {
        *(int*)(self + 0x14) = hi;
        *(int*)(self + 0x10) = lo;
    }
    else {
        *(int*)(self + 0x1c) = hi;
        *(int*)(self + 0x18) = lo;
    }
    aiactor_run_behaviour_phase(actor, 0x3e9, (int*)0, 0);
    if (DAT_805015bd == '\n') {
        lo = *(int*)(self + 0x10);
        hi = *(int*)(self + 0x14);
    }
    else {
        lo = *(int*)(self + 0x18);
        hi = *(int*)(self + 0x1c);
    }
    if (((hi == 1 && lo == 0) && (*(short*)(*(int*)(self + 0xbf0) + 0x94) != -1)) &&
        (anim_clip_flags_active_slot(actor, lo, 0, 0x20) == 0)) {
        player_ai_combat_behavior_tick();
        aiactor_set_stance_flags(actor, 8);
    }
    if (((DAT_805015bd == '\n') && ((*(unsigned int*)(self + 0xab4) & 0x70000) != 0)) &&
        ((*(unsigned int*)(self + 0x14) & 0x4070060) == 0)) {
        if (*(int*)(self + 0x9c0) != 0) {
            zone = soundpad_resolve_entity_to_zone((int)*(short*)(*(int*)(self + 0x9c0) + 8));
            *(unsigned int*)(zone + 4) = *(unsigned int*)(zone + 4) & 0xbfffffff;
            *(int*)(self + 0x9c0) = 0;
        }
        if (((*(unsigned int*)(self + 0xab4) & 0x60000) != 0) && (*(int*)(self + 0x9c4) != 0)) {
            zone = soundpad_resolve_entity_to_zone((int)*(short*)(*(int*)(self + 0x9c4) + 8));
            *(unsigned int*)(zone + 4) = *(unsigned int*)(zone + 4) & 0xbfffffff;
            *(int*)(self + 0x9c4) = 0;
        }
    }
    FUN_80382734();
    return;
}

// tracker_select_best_target_scored (0x8027f80c): the main target scorer. Builds
// a candidate list of visible non-teammate actors (skipping some at random when
// flag 0x10 is set), then scores each. In "leader" mode (packed==0) it uses a
// distance * score-rank * relation-recency * (rank-diff) cost; otherwise a
// simpler distance/relation cost with story-mode and current-target hysteresis.
// Keeps the minimum-cost candidate. Gated off when DAT_805015c0 bit6 is set.
void tracker_select_best_target_scored(void)
{
    float dx, dy, dz;
    int self;
    char* relByte;
    int relRec;
    unsigned int flags;
    int i, ii;
    int cand, candState;
    int* p;
    int listN;
    int selfState;
    int skip;
    double best;
    double d2;
    double rankBias;
    unsigned long long packed;
    int candBuf[32];

    packed = FUN_803826d0();
    self = (int)((unsigned long long)packed >> 0x20);
    selfState = *(int*)(self + 0x7c);
    best = (double)FLOAT_80619300;
    listN = 0;
    ii = 0;
    if ((((int)packed == 0) || (DAT_806120a4 == *(int*)(selfState + 8))) &&
        ((DAT_805015c0 & 0x40) == 0)) {
        int off = 0;
        for (i = 0; i < DAT_80612098; i = i + 1) {
            p = *(int**)((int)&DAT_80588610 + off);
            if ((((p[9] & 8) == 0) && (p[0x21] != *(int*)(selfState + 0x84))) &&
                (flags = p[7], (flags & 0x18000) == 0)) {
                if (DAT_805015bd == '\n') {
                    flags = p[5];
                }
                if (((flags & 0x1000) == 0) && ((p[0x31b] & 4) == 0)) {
                    if ((p[0x31b] & 0x10) != 0) {
                        unsigned int r = rng_counter_tick();
                        unsigned int denom =
                            (unsigned int)(((DAT_806109c0 / (DAT_80612098 - DAT_80612610)) * 3) ^ 0x80000000);
                        skip = (int)((float)((double)CONCAT44(0x43300000, denom) - DOUBLE_80619328) *
                                     ((float)((double)CONCAT44(0x43300000, r) - DOUBLE_80619320) /
                                      FLOAT_80619308) + FLOAT_80619304);
                        if (skip != 0) goto NEXT;
                    }
                    listN = listN + 1;
                    *(int*)((int)candBuf + ii) = *p;
                    ii = ii + 4;
                }
            }
NEXT:
            off = off + 4;
        }
        if ((int)packed == 0) {
            ii = 0;
            for (i = 0; i < listN; i = i + 1) {
                cand = *(int*)((int)candBuf + ii);
                dx = *(float*)(self + 100) - *(float*)(cand + 100);
                dy = *(float*)(self + 0x68) - *(float*)(cand + 0x68);
                candState = *(int*)(cand + 0x7c);
                dz = *(float*)(self + 0x6c) - *(float*)(cand + 0x6c);
                d2 = (double)(dz * dz + dx * dx + dy * dy);
                int sc = score_calc_player_total(candState);
                d2 = (double)(float)(d2 * (double)(float)((double)((sc + 1) * (sc + 1)) - DOUBLE_80619328));
                relByte = (char*)aiactor_relation_entry(*(int*)(candState + 8), *(int*)(selfState + 8));
                if (*relByte != '\0') {
                    relRec = aiactor_relation_entry(*(int*)(candState + 8), *(int*)(selfState + 8));
                    float recent = (FLOAT_80619314 -
                                    (float)((double)(DAT_80611928 - *(int*)(relRec + 4)))) /
                                   FLOAT_80619314;
                    float c = recent;
                    if (FLOAT_80619318 < recent) c = FLOAT_80619318;
                    float w = FLOAT_8061930c;
                    if ((FLOAT_8061930c <= c) && (w = recent, FLOAT_80619318 < recent)) {
                        w = FLOAT_80619318;
                    }
                    d2 = (double)(float)(d2 * (double)(FLOAT_80619318 + w));
                }
                if ((((DAT_805015be == '\x13') && (DAT_80612610 <= *(int*)(candState + 8))) &&
                     (player_ai_is_story_mode((int)*(short*)(candState + 0x48)) != 0)) &&
                    (*(int*)(candState + 4) != self)) {
                    d2 = (double)FLOAT_80619300;
                }
                if ((DAT_805015dc != 0) && (*(int*)(candState + 8) < DAT_80612610)) {
                    rankBias = (double)((int)*(short*)(candState + 0xa60) -
                                        (int)*(short*)(selfState + 0xa60));
                    float f = FLOAT_8061931c * (FLOAT_80619318 + ABS((float)rankBias));
                    d2 = (double)(float)(d2 / (double)(f * f));
                }
                if (d2 < best) {
                    best = d2;
                }
                ii = ii + 4;
            }
        }
        else {
            ii = 0;
            for (i = 0; i < listN; i = i + 1) {
                cand = *(int*)((int)candBuf + ii);
                candState = *(int*)(cand + 0x7c);
                relByte = (char*)aiactor_relation_entry(*(int*)(candState + 8), *(int*)(selfState + 8));
                if (*relByte != '\0') {
                    dx = *(float*)(self + 100) - *(float*)(cand + 100);
                    dy = *(float*)(self + 0x68) - *(float*)(cand + 0x68);
                    dz = *(float*)(self + 0x6c) - *(float*)(cand + 0x6c);
                    d2 = (double)(dz * dz + dx * dx + dy * dy);
                    if (*(int*)(candState + 0x80) == 0) {
                        if ((*(unsigned int*)(candState + 0x1c) & 0x10000) == 0) {
                            if (((DAT_805015be == '\x13') && (DAT_80612610 <= *(int*)(candState + 8))) &&
                                ((player_ai_is_story_mode((int)*(short*)(candState + 0x48)) != 0) &&
                                 (*(int*)(candState + 4) != self))) {
                                d2 = (double)FLOAT_80619300;
                            }
                            else if (*(int*)(selfState + 4) == cand) {
                                d2 = (double)(float)(d2 * (double)FLOAT_80619310);
                            }
                        }
                        else {
                            d2 = (double)FLOAT_80619300;
                        }
                    }
                    else {
                        d2 = (double)FLOAT_8061930c;
                    }
                    if (d2 < best) {
                        best = d2;
                    }
                }
                ii = ii + 4;
            }
        }
    }
    FUN_8038271c();
    return;
}

// tracker_recover_to_zone_anchor (0x8027fd28): "unstick" recovery. Sets stance
// flag 4. If not currently recovering it seeds the nearest zone node; if it has
// been stuck too long (+0x9c8 > 0x1d) it hard-teleports to the nearest zone node
// and queues mode 0x40; otherwise it path-tests toward the node (nav dispatch)
// and either advances/arrives (queue mode from node) or bumps the stuck counter.
void tracker_recover_to_zone_anchor(void)
{
    float dx, dy, dz;
    int actor;
    int self;
    int node;
    int nodeRec;
    unsigned int flags;
    double dist;
    float target[3];
    int probeOff;

    actor = __save_gpr_28();
    self = *(int*)(actor + 0x7c);
    aiactor_set_stance_flags(actor, 4);
    if (*(int*)(self + 0x9c8) == 0) {
        node = soundpad_find_nearest_entity((double)FLOAT_8061930c, (float*)(actor + 100));
        *(int*)(self + 0xbe0) = -1;
    }
    else {
        if (0x1d < *(int*)(self + 0x9c8)) {
            node = soundpad_find_nearest_entity((double)FLOAT_8061930c, (float*)(actor + 100));
            nodeRec = node * 0x20;
            *(int*)(actor + 100) = *(int*)(DAT_80611b8c + nodeRec + 0xc);
            *(int*)(actor + 0x68) = *(int*)(DAT_80611b8c + nodeRec + 0x10);
            *(int*)(actor + 0x6c) = *(int*)(DAT_80611b8c + nodeRec + 0x14);
            *(int*)(self + 0x678) = node;
            *(int*)(self + 0x9ac) = 0;
            *(int*)(self + 0x9a8) = 0;
            *(int*)(actor + 0x34) = (int)*(short*)(DAT_80611b8c + nodeRec + 10);
            *(int*)(self + 0x9c8) = 0;
            bool arm = true;
            if (*(int*)(self + 0xaac) == 1 && *(int*)(self + 0xaa8) == 0) {
                if (DAT_805015bd == '\n') {
                    flags = *(unsigned int*)(self + 0x14);
                }
                else {
                    flags = *(unsigned int*)(self + 0x1c);
                }
                if ((flags & 1) == 0) arm = false;
            }
            if (arm) {
                *(int*)(self + 0xaac) = 0x40;
                *(int*)(self + 0xaa8) = 0;
            }
            aiactor_update_zone_anchor_position();
            __restore_gpr_28();
            return;
        }
        nodeRec = DAT_80611b8c + *(int*)(self + 0xbe0) * 0x20;
        dx = *(float*)(actor + 100) - *(float*)(nodeRec + 0xc);
        dy = *(float*)(actor + 0x68) - *(float*)(nodeRec + 0x10);
        dz = *(float*)(actor + 0x6c) - *(float*)(nodeRec + 0x14);
        dist = math_sqrt((double)(dz * dz + dx * dx + dy * dy));
        node = soundpad_find_nearest_entity(dist, (float*)(actor + 100));
    }
    nodeRec = DAT_80611b8c + node * 0x20;
    target[0] = *(float*)(nodeRec + 0xc);
    target[1] = *(float*)(nodeRec + 0x10);
    target[2] = *(float*)(nodeRec + 0x14);
    probeOff = *(short*)(self + 0x48) * 0x30;
    nav_advanced_movement_dispatch(
        (double)*(float*)((char*)&DAT_8048ca94 + probeOff),
        (double)*(float*)((char*)&DAT_8048ca9c + probeOff),
        *(int*)(self + 8), actor + 100, &target[0], (int*)0, 0, 8);
    nodeRec = DAT_80611b8c + node * 0x20;
    dx = target[0] - *(float*)(nodeRec + 0xc);
    dy = target[1] - *(float*)(nodeRec + 0x10);
    dz = target[2] - *(float*)(nodeRec + 0x14);
    if (FLOAT_80619330 <= dz * dz + dx * dx + dy * dy) {
        *(int*)(self + 0x9c8) = *(int*)(self + 0x9c8) + 1;
        *(int*)(self + 0xbe0) = node;
    }
    else {
        *(int*)(self + 0x678) = node;
        *(int*)(self + 0x9ac) = 0;
        *(int*)(self + 0x9a8) = 0;
        *(int*)(self + 0x9c8) = 0;
        bool arm = true;
        if (*(int*)(self + 0xaac) == 1 && *(int*)(self + 0xaa8) == 0) {
            if (DAT_805015bd == '\n') {
                flags = *(unsigned int*)(self + 0x14);
            }
            else {
                flags = *(unsigned int*)(self + 0x1c);
            }
            if ((flags & 1) == 0) arm = false;
        }
        if (arm) {
            *(int*)(self + 0xaac) = 0x40;
            *(int*)(self + 0xaa8) = 0;
        }
    }
    __restore_gpr_28();
    return;
}

// tracker_arbitrate_behavior_mode (0x8028001c): pick the next queued behavior
// mode (+0xaac/+0xaa8) for the actor from its pending mode (+0xabc/+0xab8),
// target validity, game mode, and cost-matrix state. Copies the destination
// node position into the nav target (+0x30..+0x38) and, depending on whether the
// hold timer (+0x9b0) has elapsed, either promotes a pending queued mode or
// selects a fresh mode (regroup 0x10 / follow 8 / combat 4 / seek 0x20 / hold
// 0x40). Many branches share a "don't override an active locked mode 1" guard.
void tracker_arbitrate_behavior_mode(unsigned long long a, double b)
{
    int actor;
    int self;
    unsigned int flags;
    int target;
    int partner;

    actor = __save_gpr_28();
    self = *(int*)(actor + 0x7c);
    *(int*)(self + 0xad0) = *(int*)(self + 0xb18);
    *(unsigned int*)(self + 0x28) = *(unsigned int*)(self + 0x28) & 0xfffff7ff;
    aiactor_set_stance_flags(actor, 1);
    *(int*)(self + 0x30) = *(int*)(DAT_80611b8c + *(int*)(self + 0x678) * 0x20 + 0xc);
    *(int*)(self + 0x34) = *(int*)(DAT_80611b8c + *(int*)(self + 0x678) * 0x20 + 0x10);
    *(int*)(self + 0x38) = *(int*)(DAT_80611b8c + *(int*)(self + 0x678) * 0x20 + 0x14);
    if (FLOAT_80619330 <= *(float*)(self + 0x9b0)) {
        partner = tracker_coop_scan_partner_nodes();
        if (self == partner) {
            tracker_grab_attach_update(a, b);
        }
        if ((*(unsigned int*)(self + 0xabc) & 0x800) == 0) {
            if ((DAT_805015bd != '\x06') && (DAT_805015bd != '\n')) {
                int savedTarget = *(int*)(self + 4);
                FUN_802b6d9c();
                if (*(int*)(self + 4) == 0) {
                    *(int*)(self + 4) = savedTarget;
                }
            }
            if (((*(int*)(self + 4) != 0) && (aiactor_retarget_visible_teammate() != 0)) &&
                (tracker_coop_scan_partner_nodes() != self)) {
                target = *(int*)(*(int*)(self + 4) + 0x20);
                if ((target == 1) ||
                    ((target == 8) &&
                     (*(int*)(self + 0x84) != *(int*)(*(int*)(*(int*)(self + 4) + 0x7c) + 0x84)))) {
                    if ((DAT_805015bd == '\x06') || (DAT_805015bd == '\x05')) {
                        if (*(int*)(self + 0xabc) != 0 || *(int*)(self + 0xab8) != 0) {
                            if (!(*(int*)(self + 0xaac) == 1 && *(int*)(self + 0xaa8) == 0 &&
                                  (((DAT_805015bd == '\n') ? *(unsigned int*)(self + 0x14)
                                                           : *(unsigned int*)(self + 0x1c)) & 1) == 0)) {
                                *(int*)(self + 0xaac) = *(int*)(self + 0xabc);
                                *(int*)(self + 0xaa8) = *(int*)(self + 0xab8);
                            }
                        }
                        goto DONE;
                    }
                    if (tracker_costmatrix_mode_active() != 0) {
                        if (*(int*)(self + 0xabc) != 0 || *(int*)(self + 0xab8) != 0) {
                            if (!(*(int*)(self + 0xaac) == 1 && *(int*)(self + 0xaa8) == 0 &&
                                  (((DAT_805015bd == '\n') ? *(unsigned int*)(self + 0x14)
                                                           : *(unsigned int*)(self + 0x1c)) & 1) == 0)) {
                                *(int*)(self + 0xaac) = *(int*)(self + 0xabc);
                                *(int*)(self + 0xaa8) = *(int*)(self + 0xab8);
                            }
                            goto DONE;
                        }
                    }
                    if (((*(int*)(self + 700) != 0) || (*(int*)(self + 0x94) == 0x42)) ||
                        (*(int*)(self + 0x94) == 0x43)) {
                        if ((DAT_805015bd != '\x02') || (*(int*)(self + 0x80) == 0)) {
                            if (!(*(int*)(self + 0xaac) == 1 && *(int*)(self + 0xaa8) == 0 &&
                                  (((DAT_805015bd == '\n') ? *(unsigned int*)(self + 0x14)
                                                           : *(unsigned int*)(self + 0x1c)) & 1) == 0)) {
                                *(int*)(self + 0xaac) = 4;
                                *(int*)(self + 0xaa8) = 0;
                            }
                            goto DONE;
                        }
                    }
                }
            }
            if (FLOAT_80619334 < *(float*)(self + 0xb30)) {
                if ((*(unsigned int*)(self + 0x28) & 1) != 0) {
                    *(float*)(self + 0xb30) = FLOAT_8061930c;
                    if (!(*(int*)(self + 0xaac) == 1 && *(int*)(self + 0xaa8) == 0 &&
                          (((DAT_805015bd == '\n') ? *(unsigned int*)(self + 0x14)
                                                   : *(unsigned int*)(self + 0x1c)) & 1) == 0)) {
                        *(int*)(self + 0xaac) = 0x20;
                        *(int*)(self + 0xaa8) = 0;
                    }
                }
            }
        }
        goto DONE;
    }
    if (*(int*)(self + 0xabc) == 0 && *(int*)(self + 0xab8) == 0) {
        if ((DAT_805015bd != '\x01') || (*(int*)(self + 0x80) == 0)) {
            if (*(int*)(self + 4) == 0) {
                if (!(*(int*)(self + 0xaac) == 1 && *(int*)(self + 0xaa8) == 0 &&
                      (((DAT_805015bd == '\n') ? *(unsigned int*)(self + 0x14)
                                               : *(unsigned int*)(self + 0x1c)) & 1) == 0)) {
                    *(int*)(self + 0xaac) = 0x10;
                    *(int*)(self + 0xaa8) = 0;
                }
                goto DONE;
            }
        }
        if (!(*(int*)(self + 0xaac) == 1 && *(int*)(self + 0xaa8) == 0 &&
              (((DAT_805015bd == '\n') ? *(unsigned int*)(self + 0x14)
                                       : *(unsigned int*)(self + 0x1c)) & 1) == 0)) {
            *(int*)(self + 0xaac) = 8;
            *(int*)(self + 0xaa8) = 0;
        }
        goto DONE;
    }
    if (!(*(int*)(self + 0xaac) == 1 && *(int*)(self + 0xaa8) == 0 &&
          (((DAT_805015bd == '\n') ? *(unsigned int*)(self + 0x14)
                                   : *(unsigned int*)(self + 0x1c)) & 1) != 0)) {
        *(int*)(self + 0xaac) = *(int*)(self + 0xabc);
        *(int*)(self + 0xaa8) = *(int*)(self + 0xab8);
    }
    *(int*)(self + 0xabc) = 0;
    *(int*)(self + 0xab8) = 0;
DONE:
    __restore_gpr_28();
    return;
}

// ===========================================================================
// batch_758: co-op leader re-assignment, zone path distance, per-zone
//            objective precompute, stationary timer, fidget clip, script-event
//            dispatch, relation reset, ground-phase handler.
// Address range: 0x80280594 - 0x8028114f
// ===========================================================================

// tracker_reassign_leader_on_death (0x80280594): when the current leader/spectate
// player (DAT_80611910) has died, begin its death fadeout and pick a replacement:
// prefer a valid live secondary (param[1], state 8, not flagged +0x24&8), else the
// highest-scoring player, else player 0 record. Seeds the new leader's fields and
// resets its death effect.
void tracker_reassign_leader_on_death(int* param_1)
{
    int cur;
    int alt;

    cur = *param_1;
    if (cur == DAT_80611910) {
        if (cur != 0) {
            character_death_begin_fadeout(*(int*)(cur + 0x7c) + 0xc68);
        }
        alt = param_1[1];
        if ((((alt == 0) || (alt == *param_1)) || (*(int*)(alt + 0x20) != 8)) ||
            ((*(unsigned int*)(*(int*)(alt + 0x7c) + 0x24) & 8) != 0)) {
            if (*param_1 == 0) {
                alt = 0;
            }
            else {
                alt = score_find_highest_player();
            }
            if ((alt == 0) && (alt = DAT_80611910, DAT_80611910 == 0)) {
                alt = *(int*)(DAT_80611d78 + 0x14);
            }
        }
        DAT_80611910 = alt;
        *(int*)(alt + 0x1cc) = *(int*)(alt + 0x1d8);
        character_death_effect_reset(
            (unsigned int*)(DAT_80611d90 + *(int*)(*(int*)(DAT_80611910 + 0x7c) + 8) * 0xea0 + 0xc68));
    }
    return;
}

// tracker_zone_path_distance (0x80280684): compute the navigation path distance
// between two zones. Same zone -> 0 (FLOAT_8061930c). If the target is a linked
// sub-zone (7999 < to < 0x209e) that shares the source's parent zone, use the
// direct Euclidean distance between the two zone nodes. Otherwise run the bounded
// nav pathfinder (writing FLOAT_80611ba0/ba4) and return the total length+cost,
// or INF (FLOAT_80619300) if unreachable.
double tracker_zone_path_distance(int fromZone, int toZone)
{
    bool linked;
    int* toNode;
    int* fromNode;
    int rc;
    double d;
    int req[13];
    int reqTail;
    int scratchA;
    int scratchB;

    if (fromZone == toZone) {
        d = (double)FLOAT_8061930c;
    }
    else {
        linked = false;
        if ((7999 < toZone) && (toZone < 0x209e)) {
            linked = true;
        }
        req[0] = fromZone;
        reqTail = toZone;
        if (linked) {
            toNode = soundpad_resolve_zone_node(toZone);
            fromNode = soundpad_resolve_zone_node(fromZone);
            reqTail = soundpad_find_zone_by_id((int)*(short*)(toNode + 2));
            if ((int)*(short*)(toNode + 2) == *fromNode) {
                d = math_sqrt((double)(((float)toNode[5] - (float)fromNode[5]) *
                                       ((float)toNode[5] - (float)fromNode[5]) +
                                       ((float)toNode[3] - (float)fromNode[3]) *
                                       ((float)toNode[3] - (float)fromNode[3]) +
                                       ((float)toNode[4] - (float)fromNode[4]) *
                                       ((float)toNode[4] - (float)fromNode[4])));
                return d;
            }
        }
        scratchA = 0;
        scratchB = 0;
        (void)reqTail; (void)scratchA; (void)scratchB;
        FLOAT_80611ba0 = FLOAT_8061930c;
        FLOAT_80611ba4 = FLOAT_8061930c;
        rc = aiactor_nav_advance_path_bounded(*(int*)(DAT_80611d78 + 0x14), req);
        if (rc < 0) {
            d = (double)FLOAT_80619300;
        }
        else {
            d = (double)(FLOAT_80611ba0 + FLOAT_80611ba4);
        }
    }
    return d;
}

// tracker_precompute_zone_objectives (0x802807bc): per-frame amortized build of a
// per-zone "nearest objective" table (DAT_80611b9c, stride 0x2c). For each active
// zone it finds the nearest 3 spawn points, the nearest pickup, and the nearest
// objective (entity state 8 / 0x10) by nav path distance, calling gx_present
// between zones so the work spreads across frames. Skipped in co-op story mode.
void tracker_precompute_zone_objectives(double p2, double p3, unsigned long long p4,
                                        unsigned long long p5, unsigned long long p6,
                                        unsigned long long p7, unsigned long long p8)
{
    unsigned int gate;
    int recBase;
    int zoneRec;
    int zone;
    int spawnIdx, si;
    int found, fi;
    int d, di;
    float best3[4];
    int idx3[13];
    double dist;
    double bestPickup;
    double bestObjHi;
    double bestObjLo;

    (void)FUN_803826dc();
    if (DAT_805015bd != '\n') {
        recBase = 0;
        zoneRec = 0;
        for (zone = 0; zone < DAT_80611c14; zone = zone + 1) {
            found = 0;
            fi = 0;
            if ((*(unsigned int*)(DAT_80611b8c + zoneRec + 4) & 1) != 0) {
                gate = FUN_8039e020();
                if (gate == 0) {
                    if (DAT_80612374 != 0) {
                        gx_present_and_advance(0, p2, p3, p4, p5, p6, p7, p8);
                    }
                }
                else {
                    DAT_80612374 = 1;
                }
                best3[3] = FLOAT_80619300;
                si = 0;
                for (spawnIdx = 0; spawnIdx < DAT_80612100; spawnIdx = spawnIdx + 1) {
                    dist = tracker_zone_path_distance(zone, *(int*)((int)&DAT_80588a9c + si));
                    if (dist < (double)best3[3]) {
                        if ((found == 0) || ((double)*(float*)((int)best3 + fi) <= dist)) {
                            if (found < 3) {
                                *(int*)((int)idx3 + fi) = spawnIdx;
                                found = found + 1;
                                *(float*)((int)best3 + fi + 4) = (float)dist;
                                fi = fi + 4;
                            }
                        }
                        else {
                            // keep the 3 nearest sorted (insertion into best3/idx3)
                            int pos = 0;
                            int poff = 0;
                            int remain = found;
                            bool inserted = false;
                            while (remain > 0) {
                                if ((double)*(float*)((int)best3 + poff + 4) <= dist) {
                                    pos = pos + 1;
                                    poff = poff + 4;
                                    remain = remain - 1;
                                    continue;
                                }
                                int hi = found - 1;
                                int cnt = found - pos;
                                int hoff = hi * 4;
                                if (pos <= hi) {
                                    do {
                                        if (hi < 2) {
                                            *(int*)((int)idx3 + hoff + 4) = *(int*)((int)idx3 + hoff);
                                            *(float*)((int)best3 + hoff + 8) =
                                                *(float*)((int)best3 + hoff + 4);
                                        }
                                        hi = hi - 1;
                                        hoff = hoff - 4;
                                        cnt = cnt - 1;
                                    } while (cnt != 0);
                                }
                                *(int*)((int)idx3 + poff) = spawnIdx;
                                *(float*)((int)best3 + poff + 4) = (float)dist;
                                if (found < 3) {
                                    found = found + 1;
                                    fi = fi + 4;
                                }
                                inserted = true;
                                break;
                            }
                            (void)inserted;
                        }
                    }
                    si = si + 8;
                }
                // write the 3 nearest spawn-point entity handles into the zone record
                {
                    int w = 0;
                    int woff = 0;
                    int slot = 3;
                    do {
                        if (w < found) {
                            *(int*)(recBase + DAT_80611b9c + woff + 0x20) =
                                *(int*)((char*)&DAT_80588aa0 + *(int*)((int)idx3 + woff) * 8);
                        }
                        else {
                            *(int*)(recBase + DAT_80611b9c + woff + 0x20) = 0;
                        }
                        w = w + 1;
                        woff = woff + 4;
                        slot = slot - 1;
                    } while (slot != 0);
                }
                if (0 < DAT_806120b4) {
                    *(int*)(DAT_80611b9c + recBase + 8) = 0;
                    bestPickup = (double)FLOAT_80619300;
                    di = 0;
                    for (d = 0; d < DAT_806120b4; d = d + 1) {
                        dist = tracker_zone_path_distance(zone, *(int*)((int)&DAT_805887e4 + di));
                        if (dist < bestPickup) {
                            *(int**)(recBase + DAT_80611b9c + 8) = (int*)((int)&DAT_805887e4 + di);
                            *(float*)(recBase + DAT_80611b9c + 0x14) = (float)dist;
                            bestPickup = dist;
                        }
                        di = di + 8;
                    }
                }
                if (0 < DAT_806120ac) {
                    bestObjHi = (double)FLOAT_80619300;
                    *(int*)(DAT_80611b9c + recBase) = 0;
                    di = 0;
                    *(int*)(DAT_80611b9c + recBase + 4) = 0;
                    bestObjLo = bestObjHi;
                    for (d = 0; d < DAT_806120ac; d = d + 1) {
                        int objType =
                            *(int*)(*(int*)(*(int*)((int)&DAT_80588928 + di) + 0x7c) + 0xc);
                        if (objType == 0x10) {
                            dist = tracker_zone_path_distance(zone, *(int*)((int)&DAT_80588924 + di));
                            if (dist < bestObjHi) {
                                *(int*)(recBase + DAT_80611b9c + 4) =
                                    *(int*)((int)&DAT_80588928 + di);
                                *(float*)(recBase + DAT_80611b9c + 0x10) = (float)dist;
                                bestObjHi = dist;
                            }
                        }
                        else if (((objType < 0x10) && (objType == 8)) &&
                                 (dist = tracker_zone_path_distance(zone,
                                            *(int*)((int)&DAT_80588924 + di)), dist < bestObjLo)) {
                            *(int*)(recBase + DAT_80611b9c) = *(int*)((int)&DAT_80588928 + di);
                            *(float*)(recBase + DAT_80611b9c + 0xc) = (float)dist;
                            bestObjLo = dist;
                        }
                        di = di + 8;
                    }
                }
            }
            recBase = recBase + 0x2c;
            zoneRec = zoneRec + 0x20;
        }
    }
    FUN_80382728();
    return;
}

// tracker_update_stationary_timer (0x80280b94): accumulate a per-actor "stationary
// time" counter (+0x2a0). Once per frame, if the actor has moved less than
// FLOAT_80619338 (sq) since the last sample (+0x29d..29f) and within the timeout,
// the counter grows; otherwise it resets and the sample position is refreshed.
void tracker_update_stationary_timer(int* param_1)
{
    float dx, dy, dz;
    int self;
    unsigned int dt;

    self = *param_1;
    dt = DAT_80611928 - param_1[0x2a1];
    if (0 < (int)dt) {
        param_1[0x2a0] =
            (int)(FLOAT_806108ec *
                  (FLOAT_80619318 /
                   (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80619328)) +
                  (float)param_1[0x2a0]);
        dx = *(float*)(self + 100) - (float)param_1[0x29d];
        dy = *(float*)(self + 0x68) - (float)param_1[0x29e];
        dz = *(float*)(self + 0x6c) - (float)param_1[0x29f];
        if ((FLOAT_80619338 < dz * dz + dx * dx + dy * dy) ||
            (FLOAT_806108ec < (float)((double)CONCAT44(0x43300000, dt ^ 0x80000000) - DOUBLE_80619328))) {
            param_1[0x2a0] = (int)FLOAT_8061930c;
            param_1[0x29d] = *(int*)(self + 100);
            param_1[0x29e] = *(int*)(self + 0x68);
            param_1[0x29f] = *(int*)(self + 0x6c);
        }
        param_1[0x2a1] = DAT_80611928;
    }
    return;
}

// tracker_stub_zero_a (0x80280c7c): stub, returns 0.
int tracker_stub_zero_a(void)
{
    return 0;
}

// tracker_stub_zero_b (0x80280c84): stub, returns 0.
int tracker_stub_zero_b(void)
{
    return 0;
}

// tracker_play_melee_fidget_clip (0x80280c8c): play the co-op melee/idle-fidget
// animation. Ranged weapon -> clip 0x9ab. Melee -> if none of 0x25/0x37/0x38 is
// active, start clip 0x25; if 0x25 is active and its slot userflag is set, replay
// with rng-jittered rate. Non-story modes just (re)start clip 0x25.
void tracker_play_melee_fidget_clip(int actor)
{
    int r;
    unsigned int roll;

    if (DAT_805015bd == '\n') {
        r = player_ai_is_ranged_weapon((int)*(short*)(*(int*)(actor + 0x7c) + 0x4a));
        if (r == 0) {
            r = anim_is_clip_active(actor, 0x25);
            if (((r == 0) && (r = anim_is_clip_active(actor, 0x38), r == 0)) &&
                (r = anim_is_clip_active(actor, 0x37), r == 0)) {
                anim_play_scaled((double)FLOAT_8061933c, (double)FLOAT_80619318);
            }
            else {
                r = anim_is_clip_active(actor, 0x25);
                if ((r != 0) && (r = anim_active_slot_userflags(*(int*)(actor + 4)), r != 0)) {
                    roll = rng_counter_tick();
                    if ((roll & 3) < 2) {
                        anim_play_scaled((double)FLOAT_8061933c, (double)FLOAT_80619318);
                    }
                    else {
                        unsigned int r2 = rng_counter_tick();
                        anim_play_scaled((double)FLOAT_8061933c,
                                         (double)(FLOAT_80619340 *
                                                  ((float)((double)CONCAT44(0x43300000, r2) - DOUBLE_80619320) /
                                                   FLOAT_80619308) + FLOAT_80619340));
                    }
                }
            }
        }
        else {
            r = anim_is_clip_active(actor, 0x9ab);
            if (r == 0) {
                anim_play_scaled((double)FLOAT_8061933c, (double)FLOAT_80619318);
            }
        }
    }
    else {
        r = anim_is_clip_active(actor, 0x25);
        if (r == 0) {
            anim_play_scaled((double)FLOAT_8061933c, (double)FLOAT_80619318);
        }
    }
    return;
}

// tracker_coop_script_event_dispatch (0x80280e50): route a co-op script event.
// In modes 2 (target-lost) it calls tracker_handle_target_lost_event; in modes
// 3/7 (steal) tracker_event_steal_targets. Then, for event 4 on a node with a
// valid label (+0x94 != -1), plays a clip and (for label 0xb3f) fires sound 0x348
// at the actor's position with an rng pitch; returns -1.
int tracker_coop_script_event_dispatch(int actor, int event)
{
    int result;
    int self;
    int pitch;
    int params[13];
    int paramTail;
    int px, py, pz;
    unsigned char one;

    result = 0;
    if (DAT_805015bd != 3) {
        if (DAT_805015bd < 3) {
            if (1 < DAT_805015bd) {
                result = tracker_handle_target_lost_event(actor, event);
            }
            goto AFTER;
        }
        if (DAT_805015bd != 7) goto AFTER;
    }
    result = tracker_event_steal_targets(actor, event);
AFTER:
    if ((event == 4) &&
        (self = *(int*)(actor + 0x7c), *(short*)(*(int*)(self + 0xbf0) + 0x94) != -1)) {
        anim_play_scaled((double)FLOAT_8061930c, (double)FLOAT_80619318);
        if (*(short*)(*(int*)(self + 0xbf0) + 0x94) == 0xb3f) {
            pitch = (int)(FLOAT_80619344 *
                          (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80619328));
            sound_init_play_params((int)params);
            one = 1;
            params[0] = 0x348;
            px = *(int*)(actor + 100);
            py = *(int*)(actor + 0x68);
            pz = *(int*)(actor + 0x6c);
            paramTail = pitch;
            params[3] = px; params[4] = py; params[5] = pz;
            params[6] = paramTail;
            (void)one; (void)paramTail;
            sound_play_request(params);
        }
        result = -1;
    }
    return result;
}

// tracker_clear_player_visibility_relations (0x80280f8c): for co-op event 5, clear
// the "visible" relation bit between this actor and every other active player.
// Incoming registers modeled as parameters (see Compile-Clean rule #3): the
// original reads r3 = event and r4 = actor via the __save_gpr_28 prologue helper.
void tracker_clear_player_visibility_relations(int event, int actor)
{
    int self;
    int i;
    int off;
    int selfTeam;
    int otherTeam;

    if (event == 5) {
        self = *(int*)(actor + 0x7c);
        off = 0;
        for (i = 0; i < DAT_80612098; i = i + 1) {
            selfTeam = *(int*)(self + 8);
            otherTeam = *(int*)(*(int*)((int)&DAT_80588610 + off) + 8);
            if (selfTeam != otherTeam) {
                aiactor_set_relation_visible(selfTeam, otherTeam, 0);
            }
            off = off + 4;
        }
    }
    return;
}

// tracker_coop_ground_phase_handler (0x80281010): co-op behaviour-phase handler.
// Phases 3 and 0x3ea re-ground the actor: floor-probe at its stance offset, store
// the floor Y (+0xb70/+0xbfc), refresh the floor material (+0x34) and snap to the
// nearest zone (+0x678). Phase 0x3eb forces state 100.
// Incoming registers modeled as parameters (Compile-Clean rule #3): r3 = phase,
// r4 = actor, read via the FUN_803826f0 prologue helper in the original.
void tracker_coop_ground_phase_handler(int phase, int actor)
{
    int self;
    int probeOff;
    int mat;
    int zone;
    double y;

    if (phase == 0x3ea) {
        self = *(int*)(actor + 0x7c);
        probeOff = *(short*)(self + 0x48) * 0x30;
        y = (double)floor_query_with_interact(
            (double)*(float*)((char*)&DAT_8048ca94 + probeOff),
            (double)*(float*)((char*)&DAT_8048ca9c + probeOff), actor + 100, 0, (int*)0, 0);
        *(float*)(self + 0xb70) = (float)y;
        *(float*)(self + 0xbfc) = (float)y;
        mat = (int)floor_height_from_position((double)FLOAT_80619348, (double)FLOAT_80619348);
        *(int*)(actor + 0x34) = mat;
        zone = soundpad_find_nearest_entity((double)FLOAT_8061930c, (float*)(actor + 100));
        *(int*)(self + 0x678) = zone;
    }
    else if (phase < 0x3ea) {
        if (phase == 3) {
            self = *(int*)(actor + 0x7c);
            probeOff = *(short*)(self + 0x48) * 0x30;
            y = (double)floor_query_with_interact(
                (double)*(float*)((char*)&DAT_8048ca94 + probeOff),
                (double)*(float*)((char*)&DAT_8048ca9c + probeOff), actor + 100, 0, (int*)0, 0);
            *(float*)(self + 0xb70) = (float)y;
            *(float*)(self + 0xbfc) = (float)y;
            mat = (int)floor_height_from_position((double)FLOAT_80619348, (double)FLOAT_80619348);
            *(int*)(actor + 0x34) = mat;
            zone = soundpad_find_nearest_entity((double)FLOAT_8061930c, (float*)(actor + 100));
            *(int*)(self + 0x678) = zone;
        }
    }
    else if ((phase < 0x3ec) && (*(int*)(*(int*)(actor + 0x7c) + 0x20) != 100)) {
        *(int*)(*(int*)(actor + 0x7c) + 0x9d0) = 100;
    }
    return;
}

// ===========================================================================
// batch_759: co-op script-event handlers + the two behaviour dispatch switches
// Address range 0x80281150 - 0x8028256f. See per-function headers below.
// ===========================================================================

// Address: 0x80281150 | Name: tracker_coop_ground_reground_event | Date: 2026-09-10
// Co-op script event 0x3ea "reground / cleanup". Clears the held sound-pad
// zone node's visible flag (bit 0x40000000), swaps the actor's model back in,
// nudges a held object's throw field (+0x50) to the low/high preset, then
// drops the two carry-slot handles (+0xc14, +0x9c0). Returns 0xfffffffe.
int tracker_coop_ground_reground_event(int param_1, int param_2)
{
    unsigned int result;
    int object;
    int zoneRec;
    unsigned int stateWord;
    int held;

    result = 0;
    if (param_2 == 0x3ea) {
        object = *(int*)(param_1 + 0x7c);
        if (*(int*)(object + 0x9c0) != 0) {
            zoneRec = soundpad_find_zone_by_id((int)*(short*)(*(int*)(object + 0x9c0) + 8));
            zoneRec = DAT_80611b8c + zoneRec * 0x20;
            *(unsigned int*)(zoneRec + 4) = *(unsigned int*)(zoneRec + 4) & 0xbfffffff;
        }
        object_model_swap_in(object);
        if (*(int*)(object + 0xc14) != 0) {
            stateWord = (DAT_805015bd == '\n') ? *(unsigned int*)(object + 0x14)
                                               : *(unsigned int*)(object + 0x1c);
            if ((stateWord & 1) != 0) {
                held = *(int*)(*(int*)(object + 0xc14) + 0x7c);
                *(float*)(held + 0x50) =
                    (*(float*)(held + 0x50) <= FLOAT_8061930c) ? FLOAT_80619350 : FLOAT_8061934c;
            }
        }
        result = 0xfffffffe;
        *(int*)(object + 0xc14) = 0;
        *(int*)(object + 0x9c0) = 0;
    }
    return (int)result;
}

// Address: 0x8028124c | Name: tracker_event_steal_or_mount_pose | Date: 2026-09-10
// Handles two co-op script events. Event 7 (steal): seed the steal timer
// (+0xb30/+0xb34, rng jitter) and, for states 1/5/7, raise the "stealing" flag
// bit 0x10000, then request state 4; returns -1. Events 1/3 (mount pose): if a
// mount attack/turn clip is playing, correct the actor's yaw (+0xc4) by wrapping
// the anim curve value into the pose range; returns 0xfffffffe.
int tracker_event_steal_or_mount_pose(int param_1, int param_2)
{
    unsigned int roll;
    int state;
    int self;
    double curve;
    double yaw;
    int active;

    if (param_2 != 3) {
        if (2 < param_2) {
            if (param_2 != 7) {
                return 0;
            }
            self = *(int*)(param_1 + 0x7c);
            *(float*)(self + 0xb30) = FLOAT_8061930c;
            roll = rng_counter_tick();
            *(float*)(self + 0xb34) =
                (float)((double)CONCAT44(0x43300000, roll) - DOUBLE_80619320) / FLOAT_80619308;
            state = *(int*)(self + 0x20);
            if (state != 5) {
                if (state < 5) {
                    if (state != 1) goto AFTER_FLAG;
                }
                else if (state != 7) goto AFTER_FLAG;
                *(unsigned int*)(self + 0x28) = *(unsigned int*)(self + 0x28) | 0x10000;
            }
        AFTER_FLAG:
            if (*(int*)(self + 0x20) != 4) {
                *(int*)(self + 0x9d0) = 4;
            }
            return -1;
        }
        if (param_2 != 1) {
            return 0;
        }
    }
    active = aiactor_mount_attack_clip_active();
    if ((active != 0) || (active = aiactor_mount_turn_clip_active(), active != 0)) {
        curve = anim_active_curve_value(param_1);
        yaw = (double)(float)((double)*(float*)(param_1 + 0x70) + curve);
        curve = FUN_80390324((double)((float)(yaw - (double)FLOAT_8061930c) / FLOAT_80619354));
        *(float*)(param_1 + 0xc4) =
            -(float)((double)FLOAT_80619354 * (double)(float)curve - yaw);
    }
    return (int)0xfffffffe;
}

// Address: 0x80281388 | Name: tracker_event_force_ragdoll | Date: 2026-09-10
// Co-op script event 0x3eb: force the actor into the ragdoll/limp path by
// setting self+0x2c bit 0x100000 and clearing the actor's active bit (+0x28
// bit0). Returns 0xfffffffe on handle, 0 otherwise.
int tracker_event_force_ragdoll(int param_1, int param_2)
{
    unsigned int result;
    int self;

    result = 0;
    if (param_2 == 0x3eb) {
        result = 0xfffffffe;
        self = *(int*)(param_1 + 0x7c);
        *(unsigned int*)(self + 0x2c) = *(unsigned int*)(self + 0x2c) | 0x100000;
        *(unsigned int*)(param_1 + 0x28) = *(unsigned int*)(param_1 + 0x28) & 0xfffffffe;
    }
    return (int)result;
}

// Address: 0x802813c0 | Name: tracker_event_request_reground | Date: 2026-09-10
// Co-op script event 0x3ea guard: if the actor is not already mid-special
// (self+0xaac bit 0x80000, self+0xaa8 bit 0x200) and its script node is not
// gated (self+0xbf0->+0x58 bit 0x400), request activation (actor+0x28 bit0)
// and return -1. Returns 0 otherwise.
int tracker_event_request_reground(int param_1, int param_2)
{
    unsigned int result;
    int self;

    result = 0;
    if (param_2 == 0x3ea) {
        self = *(int*)(param_1 + 0x7c);
        if (((*(unsigned int*)(self + 0xaac) & 0x80000) == 0) &&
            ((*(unsigned int*)(self + 0xaa8) & 0x200) == 0) &&
            ((*(unsigned int*)(*(int*)(self + 0xbf0) + 0x58) & 0x400) == 0)) {
            result = 0xffffffff;
            *(unsigned int*)(param_1 + 0x28) = *(unsigned int*)(param_1 + 0x28) | 1;
        }
    }
    return (int)result;
}

// Address: 0x80281428 | Name: tracker_event_steal_idle_clip | Date: 2026-09-10
// Co-op script event 7: start the idle clip 0x25 (if not already playing) and
// mark the steal target slot (+0x684) as -1 (none). Returns -1 on handle.
int tracker_event_steal_idle_clip(int param_1, int param_2)
{
    int active;
    unsigned int result;
    int self;

    result = 0;
    if (param_2 == 7) {
        self = *(int*)(param_1 + 0x7c);
        active = anim_is_clip_active(param_1, 0x25);
        if (active == 0) {
            anim_play_scaled((double)FLOAT_8061933c, (double)FLOAT_80619304);
        }
        result = 0xffffffff;
        *(int*)(self + 0x684) = -1;
    }
    return (int)result;
}

// Address: 0x802814a0 | Name: tracker_coop_event_dispatch | Date: 2026-09-10
// Master co-op script-event dispatcher. Reads the actor's pending-event bitset
// (self+0x10/0x14 in story/co-op mode 0x0a, else self+0x18/0x1c) and, unless
// the "busy" bit 0x400 is set (-> steal-idle handler), routes exactly one set
// event bit to its handler: reground, ragdoll, steal, teleport, spawn, mount
// death/event, reaction, guard, etc. Wrapped by the FUN_803826e4 context lock.
void tracker_coop_event_dispatch(int p1, int p2, int* p3)
{
    unsigned int hi;
    unsigned int lo;
    int self;
    int actor;
    int event;
    unsigned long long packed;

    (void)p1; (void)p2;
    packed = FUN_803826e4();
    actor = (int)(packed >> 0x20);
    event = (int)packed;
    self = *(int*)(actor + 0x7c);

    lo = (DAT_805015bd == '\n') ? *(unsigned int*)(self + 0x14)
                                : *(unsigned int*)(self + 0x1c);

    if ((lo & 0x400) == 0) {
        if (DAT_805015bd == '\n') {
            hi = *(unsigned int*)(self + 0x10);
            lo = *(unsigned int*)(self + 0x14);
            if (lo == 0x40000000 && hi == 0) {
                FUN_802c4910(actor, event);
            }
            else if (hi < (unsigned int)(lo < 0x40000000)) {
                if (lo != 0x10000 || hi != 0) {
                    if (hi < (unsigned int)(lo < 0x10000)) {
                        if (lo == 4 && hi == 0) {
                            int r = player_ai_is_ranged_weapon((int)*(short*)(self + 0x48));
                            if (r == 0) {
                                r = player_ai_is_sniper_weapon((int)*(short*)(self + 0x48));
                                if (r != 0) {
                                    aiactor_behavior_alert_sound();
                                }
                            }
                            else {
                                FUN_8028a4c0(actor, event, (int)p3);
                            }
                        }
                        else if (hi < (unsigned int)(lo < 4)) {
                            if (lo == 2 && hi == 0) {
                                tracker_event_request_reground(actor, event);
                            }
                            else if ((hi < (unsigned int)(lo < 2)) &&
                                     ((unsigned int)(lo == 0) <= hi)) {
                                tracker_event_force_ragdoll(actor, event);
                            }
                        }
                        else if (lo == 0x2000 && hi == 0) {
                            tracker_handle_teleport_event(actor, event);
                        }
                        else if ((hi < (unsigned int)(lo < 0x2000)) &&
                                 (lo == 0x100 && hi == 0)) {
                            tracker_coop_ground_reground_event(actor, event);
                        }
                        goto DONE;
                    }
                    if (lo == 0x80000 && hi == 0) {
                        aiactor_behavior_event_handler(actor, event, p3);
                        goto DONE;
                    }
                    if ((unsigned int)(lo < 0x80000) <= hi) {
                        if (lo == 0x1000000 && hi == 0) {
                            tracker_event_steal_or_mount_pose(actor, event);
                        }
                        else if ((hi < (unsigned int)(lo < 0x1000000)) &&
                                 (lo == 0x100000 && hi == 0)) {
                            tracker_coop_ground_phase_handler(event, actor);
                        }
                        goto DONE;
                    }
                    if ((lo != 0x40000 || hi != 0) &&
                        (((unsigned int)(lo < 0x40000) <= hi) ||
                         (lo != 0x20000 || hi != 0))) goto DONE;
                }
                FUN_802acf54(actor, event, (int)p3);
            }
            else if (lo == 0 && hi == 0x4000) {
                aiactor_mount_death_dispatch(actor, event, p3);
            }
            else if (hi < 0x4000) {
                if (lo == 0 && hi == 0x200) {
                    FUN_80287e8c(actor, event);
                }
                else if (hi < 0x200) {
                    if (lo == 0 && hi == 0x80) {
                        tracker_event_react_or_pass(actor, event);
                    }
                    else if ((hi < 0x80) && (lo == 0x80000000 && hi == 0)) {
                        aiactor_behavior_reaction_gate(actor, event);
                    }
                }
                else if (lo == 0 && hi == 0x2000) {
                    FUN_80382228(actor, event);
                }
                else if ((hi < 0x2000) && (lo == 0 && hi == 0x1000)) {
                    FUN_8028ae00(actor, event, (int)p3);
                }
            }
            else if (lo == 0 && hi == 0x40000) {
                FUN_8029aae4();
            }
            else if (hi < 0x40000) {
                if (lo == 0 && hi == 0x20000) {
                    FUN_80296058(actor, event, (int)p3);
                }
                else if ((hi < 0x20000) && (lo == 0 && hi == 0x10000)) {
                    aiactor_mount_event_handler(actor, event, (int)p3);
                }
            }
            else if (lo == 0 && hi == 0x100000) {
                FUN_802a44d0(actor, event);
            }
            else if ((hi < 0x100000) && (lo == 0 && hi == 0x80000)) {
                FUN_8029cd94(actor, event, (int)p3);
            }
        }
        else if (*(int*)(self + 0x1c) == 0x200000 && *(int*)(self + 0x18) == 0) {
            tracker_query_mode4_event(actor, event);
        }
    }
    else {
        tracker_event_steal_idle_clip(actor, event);
    }
DONE:
    FUN_80382730();
    return;
}

// Address: 0x80281b50 | Name: tracker_should_retarget | Date: 2026-09-10
// Returns true if the actor should re-evaluate its combat target this frame:
// it must have a current target (self+0x2bc) or a script target (self+0x18c),
// not be in the locked node state 0x36, its class must allow retarget (class
// flags table bit 0x100), the actor bit 0x4000000 must be clear, and either a
// visible teammate is available or the "wants retarget" bit (self+0x28 bit
// 0x20000) is set.
int tracker_should_retarget(int actor, int classIdx)
{
    int found;
    unsigned int allow;
    int self;
    int result;

    self = *(int*)(actor + 0x7c);
    if (((*(int*)(self + 0x2bc) == 0) && (*(int*)(self + 0x18c) == 0)) ||
        (*(int*)(self + 0x94) == 0x36)) {
        result = 0;
    }
    else {
        allow = 0;
        if ((*(unsigned int*)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[classIdx] * 8) & 0x100) != 0) {
            allow = countLeadingZeros(*(unsigned int*)(self + 0x10) & 0x4000000);
            allow = allow >> 5;
        }
        result = 0;
        if (allow != 0 && *(int*)(self + 0x2bc) != 0) {
            found = aiactor_retarget_visible_teammate();
            allow = (unsigned int)(found != 0);
            if (allow == 0) {
                allow = *(unsigned int*)(self + 0x28) >> 0x11 & 1;
            }
            result = (allow != 0);
        }
    }
    return result;
}

// Address: 0x80281c64 | Name: tracker_behavior_tick_router | Date: 2026-09-10
// Dispatch the actor's per-frame behaviour tick. Reads the actor's active
// state bits (self+0x10/0x14 story-coop, else self+0x18/0x1c); if none of the
// low 13 state bits (0x1fff) are set, route to the "no scripted state" tick
// (co-op FUN_80282570 / non-coop FUN_8028334c), otherwise run the per-state
// dispatch (tracker_behavior_state_dispatch / FUN_80281da4). inR8/inR9/inR10
// are passthrough registers forwarded to FUN_80282570 (Compile-Clean rule #3).
void tracker_behavior_tick_router(unsigned long long a, double b, double c, double d,
                                  double e, double f, double g, double h, int actor,
                                  int inR8, int inR9, int inR10)
{
    unsigned int state;
    int self;
    int stateWord;

    self = *(int*)(actor + 0x7c);
    if (DAT_805015bd == '\n') {
        stateWord = *(int*)(self + 0x10);
        state = *(unsigned int*)(self + 0x14);
    }
    else {
        stateWord = *(int*)(self + 0x18);
        state = *(unsigned int*)(self + 0x1c);
    }
    if ((state & 0x1fff) == 0) {
        if (DAT_805015bd == '\n') {
            tracker_behavior_state_dispatch_sp(a, b, c, d, e, f, g, h, actor, 0, 0, stateWord, 10, inR8, inR9, inR10);
        }
        else {
            tracker_behavior_state_dispatch_alt(a, b, c, d, e, f, g, h, actor);
        }
    }
    else {
        tracker_behavior_state_dispatch(a, b, c, d, e, f, g, h);
    }
    return;
}

// Address: 0x80281ce8 | Name: tracker_scan_class_flag400 | Date: 2026-09-10
// Scan the 0x45-entry per-slot class table (DAT_8043e23c, stride 0x3c) for an
// active slot (actor+0x54c[i]) whose character class has flag 0x400 set and a
// positive per-class counter (actor+0x61e[classId]). Returns nonzero (the
// matched flag word) when such a slot exists, 0 otherwise.
int tracker_scan_class_flag400(void)
{
    int actor;
    int matchFlags;
    unsigned int flags;
    int base;
    int slotOff;
    int tblOff;
    int slotPtr;

    actor = __save_gpr_28();
    matchFlags = 0;
    slotOff = 0;
    tblOff = 0;
    do {
        if ((*(char*)(actor + slotOff + 0x54c) != '\0') &&
            (*(int*)((int)&DAT_8043e23c + tblOff) != -1)) {
            base = DAT_80610ba0 + *(int*)((int)&DAT_8043e23c + tblOff) * 0x29c;
            slotPtr = match_slot_field_ptr(base, 0);
            flags = player_ai_char_def_get_flags(*(int*)(slotPtr + 4));
            if (((flags & 0x400) != 0) &&
                (0 < *(short*)(actor + *(int*)(base + 0x14) * 2 + 0x61e))) {
                matchFlags = (int)flags;
                break;
            }
        }
        slotOff = slotOff + 1;
        tblOff = tblOff + 0x3c;
    } while (slotOff < 0x45);
    __restore_gpr_28();
    return matchFlags;
}

// Address: 0x80281da4 | Name: tracker_behavior_state_dispatch | Date: 2026-09-10
// The per-state behaviour tick. Reads the actor's active state bit (self+0x10/
// +0x14 in story-coop mode 0x0a, else self+0x18/+0x1c) and dispatches to the
// matching state handler: combat/aim (state 4), path-follow (8), teleport (1),
// spawn (0), recover-to-anchor (0x20), leader-follow (0x10), idle (0x400),
// mounted weapon (0x100), companion follow (0x80), guard (0x200), mount clip
// gate (0x1000), zone-nav follow (0x800). Each branch pushes the actor's local
// transform (FUN_8038b4cc) and advances movement/facing/turn animation. Story
// vs. arcade mode selects the co-op vs. legacy handler variant. Bracketed by
// the FUN_803826f0 context lock / FUN_8038273c commit.
void tracker_behavior_state_dispatch(unsigned long long a, double b, double c, double d,
                                     double e, double f, double g, double h)
{
    unsigned int hi;
    int active;
    unsigned int lo;
    int actor;
    int self;
    double extraout_f1 = 0.0;
    unsigned long long xform;

    actor = FUN_803826f0();
    self = *(int*)(actor + 0x7c);
    if (DAT_805015bd == '\n') {
        hi = *(unsigned int*)(self + 0x10);
        lo = *(unsigned int*)(self + 0x14);
    }
    else {
        hi = *(unsigned int*)(self + 0x18);
        lo = *(unsigned int*)(self + 0x1c);
    }
    if (lo == 0x40 && hi == 0) {
        xform = FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
        aiactor_movement_tick(xform, b, c, d, e, f, g, h);
        aiactor_facing_update();
        tracker_arbitrate_behavior_mode(a, b);
        aiactor_play_turn_animation(actor);
        goto DONE;
    }
    if (hi < (unsigned int)(lo < 0x40)) {
        if (lo == 8 && hi == 0) {
            xform = FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
            aiactor_movement_tick(xform, b, c, d, e, f, g, h);
            if (DAT_805015bd == '\n') {
                aiactor_behavior_follow_tick();
            }
            else {
                aiactor_facing_update();
                FUN_802b86dc();
                aiactor_play_turn_animation(actor);
            }
            goto DONE;
        }
        if (hi < (unsigned int)(lo < 8)) {
            if (lo != 3 || hi != 0) {
                if (hi < (unsigned int)(lo < 3)) {
                    if (lo == 1 && hi == 0) {
                        xform = FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        tracker_death_sequence_tick(xform, b, c, d, e, f, g, h);
                        goto DONE;
                    }
                    if ((unsigned int)(lo == 0) <= hi) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        xform = projectile_init_aim_state(actor);
                        if (DAT_805015bd == '\n') {
                            aiactor_spawn_into_world(xform, b, c, d, e, f, g, h);
                        }
                        else {
                            FUN_802b798c(xform, b, c, d, e, f, g, h);
                        }
                        goto DONE;
                    }
                }
                else if (hi < (unsigned int)(lo < 5)) {
                    aiactor_movement_tick(a, b, c, d, e, f, g, h);
                    (void)extraout_f1;
                    if (*(int*)(self + 4) == 0) {
                        if (*(int*)(self + 0xaac) == 1 && *(int*)(self + 0xaa8) == 0) {
                            lo = (DAT_805015bd == '\n') ? *(unsigned int*)(self + 0x14)
                                                        : *(unsigned int*)(self + 0x1c);
                            if ((lo & 1) == 0) goto DONE;
                        }
                        *(int*)(self + 0xaac) = 0x10;
                        *(int*)(self + 0xaa8) = 0;
                    }
                    else {
                        active = tracker_is_special_class_eligible(actor);
                        if (active == 0) {
                            if ((*(int*)(self + 4) == 0) ||
                                (*(int*)(*(int*)(self + 4) + 0x20) == 8)) {
                                if (DAT_805015bd == '\n') {
                                    active = player_ai_is_ranged_weapon((int)*(short*)(self + 0x4a));
                                    if (active == 0) {
                                        active = player_ai_is_sniper_weapon((int)*(short*)(self + 0x4a));
                                        if (active == 0) {
                                            active = player_ai_is_valid_weapon_type((int)*(short*)(self + 0x4a));
                                            if (active == 0) {
                                                if (*(short*)(self + 0x4a) == 0x51) {
                                                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                                                    aiactor_mount_follow_tick();
                                                }
                                                else {
                                                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                                                    FUN_802b4860();
                                                }
                                            }
                                            else {
                                                FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                                                aiactor_facing_update();
                                                tracker_melee_combat_tick();
                                            }
                                        }
                                        else {
                                            xform = FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                                            aiactor_behavior_creature_tick(xform, b, c, d, e, f, g, h, actor);
                                        }
                                    }
                                    else if ((DAT_80612614 == 100) && (*(int*)(self + 0x94) == 0) &&
                                             (*(int*)(self + 0x2bc) == 0) &&
                                             (active = tracker_scan_class_flag400(), active == 0)) {
                                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                                        aiactor_facing_update();
                                        tracker_creature_melee_tick();
                                    }
                                    else {
                                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                                        FUN_802898d0();
                                    }
                                }
                                else {
                                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                                    aiactor_facing_update();
                                    FUN_802b90a4();
                                }
                            }
                            else {
                                FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                                aiactor_facing_update();
                                aiactor_play_turn_animation(actor);
                                aiactor_behavior_combat_tick();
                            }
                        }
                        else {
                            FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                            aiactor_facing_update();
                            tracker_creature_melee_tick();
                        }
                    }
                    goto DONE;
                }
            }
        }
        else {
            if (lo == 0x20 && hi == 0) {
                xform = FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                aiactor_movement_tick(xform, b, c, d, e, f, g, h);
                aiactor_facing_update();
                tracker_recover_to_zone_anchor();
                aiactor_play_turn_animation(actor);
                goto DONE;
            }
            if ((hi < (unsigned int)(lo < 0x20)) && (lo == 0x10 && hi == 0)) {
                xform = FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                aiactor_movement_tick(xform, b, c, d, e, f, g, h);
                aiactor_facing_update();
                if (DAT_805015bd == '\n') {
                    aiactor_update_leader_and_state(actor);
                }
                else {
                    FUN_802b61cc();
                }
                aiactor_play_turn_animation(actor);
                goto DONE;
            }
        }
    }
    else {
        if (lo == 0x400 && hi == 0) {
            xform = FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
            aiactor_movement_tick(xform, b, c, d, e, f, g, h);
            aiactor_facing_update();
            aiactor_idle_or_follow_tick(actor);
            goto DONE;
        }
        if (hi < (unsigned int)(lo < 0x400)) {
            if (lo == 0x100 && hi == 0) {
                xform = FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                if (DAT_805015bd == '\n') {
                    aiactor_mounted_weapon_tick(xform, b, c, d, e, f, g, h);
                }
                else {
                    FUN_802bc414();
                }
                goto DONE;
            }
            if (hi < (unsigned int)(lo < 0x100)) {
                if (lo == 0x80 && hi == 0) {
                    xform = FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                    aiactor_movement_tick(xform, b, c, d, e, f, g, h);
                    if (DAT_805015bd == '\n') {
                        aiactor_follow_companion_tick();
                        aiactor_play_turn_animation(actor);
                    }
                    else {
                        aiactor_behavior_guard_tick();
                    }
                    goto DONE;
                }
            }
            else if (lo == 0x200 && hi == 0) {
                xform = FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                aiactor_movement_tick(xform, b, c, d, e, f, g, h);
                if (DAT_805015bd == '\n') {
                    aiactor_behavior_guard_tick();
                }
                else {
                    FUN_802bc654();
                }
                goto DONE;
            }
        }
        else {
            if (lo == 0x1000 && hi == 0) {
                xform = FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                aiactor_movement_tick(xform, b, c, d, e, f, g, h);
                aiactor_facing_update();
                if (DAT_805015bd == '\n') {
                    aiactor_mount_clip_gate_tick(actor);
                }
                else {
                    aiactor_behavior_investigate_tick();
                }
                goto DONE;
            }
            if ((hi < (unsigned int)(lo < 0x1000)) && (lo == 0x800 && hi == 0)) {
                xform = FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                aiactor_movement_tick(xform, b, c, d, e, f, g, h);
                aiactor_facing_update();
                if (DAT_805015bd == '\n') {
                    aiactor_follow_zone_nav_tick(a, b, c, d, e, f, g, h, actor);
                }
                else {
                    aiactor_behavior_wander_tick(actor);
                    aiactor_play_turn_animation(actor);
                }
                goto DONE;
            }
        }
    }
    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
DONE:
    FUN_8038273c();
    return;
}


// ===========================================================================
// batch_760 - AI actor per-state behaviour dispatch + threat/target/death tick
// Address range: 0x80282570 - 0x8028511f | aiactor_script_fsm.cpp
// ===========================================================================

// ---------------------------------------------------------------------------
// Address: 0x80282570 | Name: tracker_behavior_state_dispatch_sp
// Single-player per-state behaviour tick dispatcher. Reads the actor's active
// state word pair (self+0x10 == 0, self+0x14 == bit) where self = *(actor+0x7c),
// pushes a transform matrix (FUN_8038b4cc) for the branch, runs the shared
// locomotion tick (aiactor_movement_tick) and then the per-state behaviour
// handler selected by the single set state bit. Falls through to a bare matrix
// push when no recognised state is set. inR4 is a register-carried scratch
// forwarded to aiactor_mount_main_tick (Compile-Clean rule #3).
void tracker_behavior_state_dispatch_sp(unsigned long long a, double b, double c, double d,
                                        double e, double f, double g, double h, int actor,
                                        int p10, int p11, int stateWord, int p13,
                                        int inR4, int p15, int p16)
{
    unsigned int lo;
    unsigned int hi;
    int chk;
    int self;
    unsigned long long xform;
    float* rate = (float*)0x1e;

    self = *(int*)(actor + 0x7c);
    lo = *(unsigned int*)(self + 0x10);
    hi = *(unsigned int*)(self + 0x14);
    if (hi == 0 && lo == 8) {
        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
        aiactor_behavior_prop_ride_tick(actor);
        return;
    }
    if (lo < 8) {
        if (hi == 0x800000 && lo == 0) {
            xform = FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
            xform = (unsigned long long)0;
            aiactor_movement_tick(a, b, c, d, e, f, g, h);
            aiactor_path_script_tick(a, b, c, d, e, f, g, h);
            return;
        }
        if (lo < (unsigned int)(hi < 0x800000)) {
            if (hi == 0x40000 && lo == 0) {
                FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                aiactor_movement_tick(a, b, c, d, e, f, g, h);
                FUN_802aa650();
                return;
            }
            if (lo < (unsigned int)(hi < 0x40000)) {
                if (hi == 0x8000 && lo == 0) {
                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                    FUN_802a4744(a, b, c, d, e, f, g, h);
                    return;
                }
                if (lo < (unsigned int)(hi < 0x8000)) {
                    if (hi == 0x4000 && lo == 0) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        FUN_802a59f0(a, b, c, d, e, f, g, h);
                        return;
                    }
                    if ((lo < (unsigned int)(hi < 0x4000)) && (hi == 0x2000 && lo == 0)) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_script_behavior_tick();
                        if (*(int*)(actor + 4) == 0) {
                            return;
                        }
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        return;
                    }
                }
                else {
                    if (hi == 0x20000 && lo == 0) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        FUN_802a8610();
                        return;
                    }
                    if ((lo < (unsigned int)(hi < 0x20000)) && (hi == 0x10000 && lo == 0)) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        FUN_802a754c(actor);
                        return;
                    }
                }
            }
            else {
                if (hi == 0x200000 && lo == 0) {
                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                    aiactor_behavior_teleport_in_tick(a, b, c, d, e, f, g, h, actor);
                    return;
                }
                if (lo < (unsigned int)(hi < 0x200000)) {
                    if (hi == 0x100000 && lo == 0) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_behavior_alerted_tick();
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        return;
                    }
                    if ((lo < (unsigned int)(hi < 0x100000)) && (hi == 0x80000 && lo == 0)) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_scripted_update(a, b, c, d, e, f, g, h);
                        return;
                    }
                }
                else if (hi == 0x400000 && lo == 0) {
                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                    aiactor_movement_tick(a, b, c, d, e, f, g, h);
                    aiactor_death_anim_tick(a, b, c, d, e, f, g, h, actor);
                    return;
                }
            }
        }
        else {
            if (hi == 0x40000000 && lo == 0) {
                FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                FUN_802c3e10();
                return;
            }
            if (lo < (unsigned int)(hi < 0x40000000)) {
                if (hi == 0x4000000 && lo == 0) {
                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                    aiactor_behavior_fire_gate(actor);
                    return;
                }
                if (lo < (unsigned int)(hi < 0x4000000)) {
                    if (hi == 0x2000000 && lo == 0) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        aiactor_facing_update();
                        aiactor_behavior_patrol_tick();
                        return;
                    }
                    if ((lo < (unsigned int)(hi < 0x2000000)) && (hi == 0x1000000 && lo == 0)) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        aiactor_facing_update();
                        aiactor_mount_behavior_main_tick(a, b, c, d, e, f, g, h);
                        return;
                    }
                }
                else {
                    if (hi == 0x20000000 && lo == 0) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        aiactor_facing_update();
                        aiactor_behavior_search_tick(actor);
                        return;
                    }
                    if ((lo < (unsigned int)(hi < 0x20000000)) && (hi == 0x8000000 && lo == 0)) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        aiactor_facing_update();
                        aiactor_behavior_engage_tick();
                        return;
                    }
                }
            }
            else {
                if (hi == 0 && lo == 2) {
                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                    aiactor_movement_tick(a, b, c, d, e, f, g, h);
                    aiactor_behavior_vehicle_state_tick(a, b, c, d, e, f, g, h, actor);
                    return;
                }
                if (lo < 2) {
                    if (hi == 0 && lo == 1) {
                        chk = player_ai_is_ranged_weapon((int)*(short*)(self + 0x48));
                        if (chk != 0) {
                            FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                            aiactor_movement_tick(a, b, c, d, e, f, g, h);
                            FUN_8028cc34(actor);
                            return;
                        }
                        chk = player_ai_is_valid_weapon_type((int)*(short*)(self + 0x48));
                        if (chk != 0) {
                            FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                            aiactor_movement_tick(a, b, c, d, e, f, g, h);
                            FUN_80289574(actor);
                            return;
                        }
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        aiactor_mount_spawn_tick();
                        return;
                    }
                    if ((lo == 0) && (hi == 0x80000000)) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        aiactor_behavior_mounted_gun_tick(a, b, c, d, e, f, g, h);
                        return;
                    }
                }
                else if (hi == 0 && lo == 4) {
                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                    aiactor_behavior_vehicle_ride_tick();
                    return;
                }
            }
        }
    }
    else {
        if (hi == 0 && lo == 0x4000) {
            FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
            aiactor_movement_tick(a, b, c, d, e, f, g, h);
            aiactor_mount_main_tick(a, b, c, d, e, f, g, h, actor, inR4, rate,
                                    p13, stateWord, p11, p10, p16);
            return;
        }
        if (lo < 0x4000) {
            if (hi == 0 && lo == 0x200) {
                FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                aiactor_movement_tick(a, b, c, d, e, f, g, h);
                FUN_80287f3c();
                return;
            }
            if (lo < 0x200) {
                if (hi == 0 && lo == 0x40) {
                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                    aiactor_movement_tick(a, b, c, d, e, f, g, h);
                    tracker_creature_special_tick();
                    return;
                }
                if (lo < 0x40) {
                    if (hi == 0 && lo == 0x20) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_behavior_belt_ride_tick();
                        return;
                    }
                    if ((lo < 0x20) && (hi == 0 && lo == 0x10)) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        aiactor_behavior_scripted_death_tick(a, b, c, d, e, f, g, h, actor);
                        return;
                    }
                }
                else {
                    if (hi == 0 && lo == 0x100) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        FUN_802873bc();
                        return;
                    }
                    if ((lo < 0x100) && (hi == 0 && lo == 0x80)) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        tracker_creature_boss_tick(a, b, c, d, e, f, g, h, actor);
                        return;
                    }
                }
            }
            else {
                if (hi == 0 && lo == 0x1000) {
                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                    aiactor_movement_tick(a, b, c, d, e, f, g, h);
                    FUN_8028b08c(a, b, c, d, e, f, g, h);
                    return;
                }
                if (lo < 0x1000) {
                    if (hi == 0 && lo == 0x800) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        aiactor_behavior_reaction_tick(actor);
                        return;
                    }
                    if ((lo < 0x800) && (hi == 0 && lo == 0x400)) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        FUN_802882c4(a, b, c, d, e, f, g, h);
                        return;
                    }
                }
                else if (hi == 0 && lo == 0x2000) {
                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                    FUN_80380e2c();
                    aiactor_update_aim_height();
                    return;
                }
            }
        }
        else {
            if (hi == 0 && lo == 0x100000) {
                FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                aiactor_movement_tick(a, b, c, d, e, f, g, h);
                FUN_802a14c8(a, b, c, d, e, f, g, h);
                return;
            }
            if (lo < 0x100000) {
                if (hi == 0 && lo == 0x20000) {
                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                    aiactor_movement_tick(a, b, c, d, e, f, g, h);
                    FUN_80296024();
                    return;
                }
                if (lo < 0x20000) {
                    if (hi == 0 && lo == 0x10000) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_kamikaze_tick(actor);
                        return;
                    }
                    if ((lo < 0x10000) && (hi == 0 && lo == 0x8000)) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        aiactor_turret_creature_tick();
                        return;
                    }
                }
                else {
                    if (hi == 0 && lo == 0x80000) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        FUN_8029be80();
                        return;
                    }
                    if ((lo < 0x80000) && (hi == 0 && lo == 0x40000)) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        FUN_80298904();
                        return;
                    }
                }
            }
            else {
                if (hi == 0 && lo == 0x2000000) {
                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                    aiactor_behavior_ride_idle_tick(actor);
                    aiactor_update_aim_height();
                    return;
                }
                if (lo < 0x2000000) {
                    if (hi == 0 && lo == 0x400000) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        aiactor_mount_patrol_zone_tick(a, b, c, d, e, f, g, h);
                        return;
                    }
                    if ((lo < 0x400000) && (hi == 0 && lo == 0x200000)) {
                        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                        FUN_80292cb4(actor);
                        return;
                    }
                }
                else if (hi == 0 && lo == 0x4000000) {
                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                    aiactor_movement_tick(a, b, c, d, e, f, g, h);
                    aiactor_facing_update();
                    aiactor_behavior_investigate_tick();
                    return;
                }
            }
        }
    }
    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
    return;
}

// ---------------------------------------------------------------------------
// Address: 0x8028334c | Name: tracker_behavior_state_dispatch_alt
// Secondary-state behaviour tick dispatcher, keyed on the self+0x18/+0x1c state
// word pair (co-op/companion behaviours: follow, guard, ally, combat, owner).
void tracker_behavior_state_dispatch_alt(unsigned long long a, double b, double c, double d,
                                         double e, double f, double g, double h, int actor)
{
    unsigned int lo;
    unsigned int hi;
    int self;

    self = *(int*)(actor + 0x7c);
    lo = *(unsigned int*)(self + 0x18);
    hi = *(unsigned int*)(self + 0x1c);
    if (hi == 0x400000 && lo == 0) {
        FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
        aiactor_movement_tick(a, b, c, d, e, f, g, h);
        aiactor_facing_update();
        FUN_802bb580(actor);
        aiactor_play_turn_animation(actor);
        return;
    }
    if (lo < (unsigned int)(hi < 0x400000)) {
        if (hi == 0x20000 && lo == 0) {
            FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
            aiactor_movement_tick(a, b, c, d, e, f, g, h);
            aiactor_facing_update();
            aiactor_behavior_goto_marker_tick();
            aiactor_play_turn_animation(actor);
            return;
        }
        if (lo < (unsigned int)(hi < 0x20000)) {
            if (hi == 0x8000 && lo == 0) {
                FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                FUN_802bd884(a, b, c, d, e, f, g, h, actor);
                return;
            }
            if (lo < (unsigned int)(hi < 0x8000)) {
                if (hi == 0x4000 && lo == 0) {
                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                    FUN_802bd198();
                    aiactor_movement_tick(a, b, c, d, e, f, g, h);
                    return;
                }
                if ((lo < (unsigned int)(hi < 0x4000)) && (hi == 0x2000 && lo == 0)) {
                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                    aiactor_movement_tick(a, b, c, d, e, f, g, h);
                    aiactor_facing_update();
                    FUN_802bc840();
                    return;
                }
            }
            else if (hi == 0x10000 && lo == 0) {
                FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                FUN_802bdaf8(a, b, c, d, e, f, g, h, actor);
                return;
            }
        }
        else {
            if (hi == 0x100000 && lo == 0) {
                aiactor_movement_tick(a, b, c, d, e, f, g, h);
                FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                tracker_bot_behavior_tick_variant();
                return;
            }
            if (lo < (unsigned int)(hi < 0x100000)) {
                if (hi == 0x40000 && lo == 0) {
                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                    aiactor_movement_tick(a, b, c, d, e, f, g, h);
                    aiactor_facing_update();
                    FUN_802baa58();
                    aiactor_play_turn_animation(actor);
                    return;
                }
            }
            else if (hi == 0x200000 && lo == 0) {
                aiactor_movement_tick(a, b, c, d, e, f, g, h);
                FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                tracker_bot_behavior_tick();
                return;
            }
        }
    }
    else {
        if (hi == 0x4000000 && lo == 0) {
            FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
            aiactor_movement_tick(a, b, c, d, e, f, g, h);
            tracker_owner_follow_update();
            return;
        }
        if (lo < (unsigned int)(hi < 0x4000000)) {
            if (hi == 0x1000000 && lo == 0) {
                FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                aiactor_behavior_boss_scripted_tick();
                return;
            }
            if (lo < (unsigned int)(hi < 0x1000000)) {
                if (hi == 0x800000 && lo == 0) {
                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                    aiactor_movement_tick(a, b, c, d, e, f, g, h);
                    aiactor_facing_update();
                    FUN_802bb87c(actor);
                    aiactor_play_turn_animation(actor);
                    return;
                }
            }
            else if (hi == 0x2000000 && lo == 0) {
                FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                aiactor_movement_tick(a, b, c, d, e, f, g, h);
                tracker_ally_behavior_update();
                return;
            }
        }
        else {
            if (hi == 0x10000000 && lo == 0) {
                FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                aiactor_behavior_worm_boss_tick();
                return;
            }
            if (lo < (unsigned int)(hi < 0x10000000)) {
                if (hi == 0x8000000 && lo == 0) {
                    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                    aiactor_movement_tick(a, b, c, d, e, f, g, h);
                    tracker_combat_behavior_update();
                    return;
                }
            }
            else if (hi == 0 && lo == 0x40000000) {
                FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
                aiactor_movement_tick(a, b, c, d, e, f, g, h);
                aiactor_behavior_patrol_pingpong_tick(actor);
                return;
            }
        }
    }
    FUN_8038b4cc(self + 0x50, -0x7f9ef008, 0x1e);
    return;
}

// ---------------------------------------------------------------------------
// Address: 0x80283878 | Name: tracker_scan_and_alert_threats
// Walk the active-actor list; for each actor that is hostile to the current
// actor, within acquire range, and either aim-aligned or flagged, raise an
// alert (voice + group arm) and acquire the nearest target. Locks the actor
// context via FUN_803826dc / FUN_80382728.
void tracker_scan_and_alert_threats(void)
{
    float dx;
    float dy;
    float dz;
    int self;
    int selfRec;
    char* rel;
    int match;
    unsigned int flags;
    unsigned int flags2;
    int off;
    int other;
    int* rec;
    int i;

    self = FUN_803826dc();
    selfRec = *(int*)(self + 0x7c);
    off = DAT_80612610 << 2;
    i = DAT_80612610;
    while (true) {
        if (DAT_80612098 <= i) {
            FUN_80382728();
            return;
        }
        rec = *(int**)((int)&DAT_80588610 + off);
        match = *(int*)(selfRec + 8);
        other = *rec;
        if ((rec[2] != match) && ((*(unsigned int*)(rec[0x2fc] + 0x54) & 2) == 0)) {
            if (DAT_805015bd == '\n') {
                flags2 = (unsigned int)rec[5];
            }
            else {
                flags2 = (unsigned int)rec[7];
            }
            if ((((((flags2 & 0x1180200) != 0) || (((unsigned int)rec[0x2af] & 0x200) != 0)) &&
                 (rel = (char*)aiactor_relation_entry(match, rec[2]), *rel != '\0')) &&
                (dx = *(float*)(other + 100) - *(float*)(self + 100),
                 dy = *(float*)(other + 0x68) - *(float*)(self + 0x68),
                 dz = *(float*)(other + 0x6c) - *(float*)(self + 0x6c),
                 dz * dz + dx * dx + dy * dy <= FLOAT_80619358)) &&
                (match = aiactor_aim_alignment_test((double)*(float*)(selfRec + 0x9bc), *rec,
                                                    (float*)(self + 100)),
                 (match != 0) || (((unsigned int)rec[10] & 0x400) != 0))) {
                flags = (unsigned int)rec[5];
                if ((flags & 0x1000000) == 0) {
                    if (((unsigned int)rec[0x2af] & 0x200) == 0) {
                        unsigned int fx = flags;
                        if (DAT_805015bd != '\n') {
                            fx = (unsigned int)rec[7];
                        }
                        if ((fx & 0x200) != 0) {
                            goto do_alert;
                        }
                    }
                    else {
do_alert:
                        if (DAT_805015bd != '\n') {
                            flags = (unsigned int)rec[7];
                        }
                        if ((((flags & 1) == 0) && (((unsigned int)rec[9] & 8) == 0)) &&
                            (((unsigned int)rec[10] & 0x40000000) == 0)) {
                            aiactor_play_alert_voice(*rec, 1);
                            match = aiactor_acquire_nearest_target(
                                self, *(int*)(DAT_80611d78 + 0x14), 0);
                            rec[1] = match;
                            if (rec[0x2ab] == 1 && rec[0x2aa] == 0) {
                                if (DAT_805015bd == '\n') {
                                    flags = (unsigned int)rec[5];
                                }
                                else {
                                    flags = (unsigned int)rec[7];
                                }
                                if ((flags & 1) == 0) {
                                    goto next_actor;
                                }
                            }
                            rec[0x2ab] = 4;
                            rec[0x2aa] = 0;
                            goto next_actor;
                        }
                    }
                    aiactor_alert_group_arm(*(unsigned int*)(rec[0x2fc] + 0x70));
                }
                else {
                    aiactor_play_alert_voice(*rec, 1);
                    aiactor_mount_retarget_zone_event(other, self);
                }
            }
        }
next_actor:
        i = i + 1;
        off = off + 4;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x80283b90 | Name: tracker_creature_melee_tick
// Per-frame melee-creature behaviour: retarget a visible teammate, set stance,
// play a turn / lunge / bite clip (0x2a / 0x74 / 0x119) depending on leader
// alignment, run a melee swipe if a bite clip is active, follow the leader's
// zone path, and request a behaviour-state transition (reground 0x40000 /
// scripted-death 0x10 / belt-ride 0x20) as conditions dictate.
void tracker_creature_melee_tick(void)
{
    int self;
    int selfRec;
    int retarget;
    int active;
    unsigned int flags;
    unsigned int align;

    self = FUN_803826e8();
    selfRec = *(int*)(self + 0x7c);
    retarget = aiactor_retarget_visible_teammate();
    *(int*)(selfRec + 0xad0) = *(int*)(selfRec + 0xb14);
    aiactor_set_stance_flags(self, 1);
    if ((DAT_805015bd != '\n') && (*(int*)(selfRec + 8) == DAT_806120f8)) {
        active = aiactor_all_zone_targets_inactive();
        if (active == 0) {
            if (*(int*)(selfRec + 0xaac) == 1 && *(int*)(selfRec + 0xaa8) == 0) {
                if (DAT_805015bd == '\n') {
                    flags = *(unsigned int*)(selfRec + 0x14);
                }
                else {
                    flags = *(unsigned int*)(selfRec + 0x1c);
                }
                if ((flags & 1) == 0) {
                    goto done;
                }
            }
            *(int*)(selfRec + 0xaac) = 0x40000;
            *(int*)(selfRec + 0xaa8) = 0;
            goto done;
        }
    }
    if ((FLOAT_8061935c <= *(float*)(selfRec + 0xaf4)) || (retarget == 0)) {
        *(unsigned int*)(selfRec + 0x28) = *(unsigned int*)(selfRec + 0x28) & 0xfffff7ff;
        if (DAT_805015bd == '\n') {
            active = anim_is_clip_active(self, 0x2a);
            if (active == 0) {
                anim_play_scaled((double)FLOAT_8061933c, (double)FLOAT_80619318);
            }
        }
        else {
            aiactor_play_turn_animation(self);
        }
    }
    else {
        align = FUN_8036424c(selfRec);
        *(unsigned int*)(selfRec + 0x28) = *(unsigned int*)(selfRec + 0x28) | 0x800;
        aiactor_turn_toward_leader();
        if ((align & 0xc0) == 0) {
            active = anim_is_clip_active(self, 0x74);
            if (active == 0) {
                anim_play_scaled((double)FLOAT_8061933c, (double)FLOAT_80619318);
            }
        }
        else {
            active = anim_is_clip_active(self, 0x119);
            if (active == 0) {
                anim_play_scaled((double)FLOAT_8061933c, (double)FLOAT_80619318);
            }
        }
    }
    active = anim_is_clip_active(self, 0x74);
    if ((active != 0) || (active = anim_is_clip_active(self, 0x119), active != 0)) {
        aiactor_melee_swipe(self, 1, 0);
    }
    if (((selfRec != 0) && (*(int*)(selfRec + 4) != 0)) &&
        (self = aiactor_follow_zone_path(), self != 0)) {
        *(int*)(selfRec + 0x30) = *(int*)(*(int*)(selfRec + 4) + 100);
        *(int*)(selfRec + 0x34) = *(int*)(*(int*)(selfRec + 4) + 0x68);
        *(int*)(selfRec + 0x38) = *(int*)(*(int*)(selfRec + 4) + 0x6c);
    }
    if (selfRec != 0) {
        if (*(int*)(selfRec + 4) != 0) {
            if ((*(unsigned int*)(*(int*)(*(int*)(selfRec + 4) + 0x7c) + 0x24) & 8) != 0) {
                if (*(int*)(selfRec + 0xaac) == 1 && *(int*)(selfRec + 0xaa8) == 0) {
                    if (DAT_805015bd == '\n') {
                        flags = *(unsigned int*)(selfRec + 0x14);
                    }
                    else {
                        flags = *(unsigned int*)(selfRec + 0x1c);
                    }
                    if ((flags & 1) == 0) {
                        goto done;
                    }
                }
                *(int*)(selfRec + 0xaac) = 0x10;
                *(int*)(selfRec + 0xaa8) = 0;
                goto done;
            }
        }
    }
    if (FLOAT_80619360 < *(float*)(selfRec + 0xb30)) {
        if ((*(unsigned int*)(selfRec + 0x28) & 1) != 0) {
            *(int*)(selfRec + 0xabc) = 4;
            *(int*)(selfRec + 0xab8) = 0;
            if (*(int*)(selfRec + 0xaac) == 1 && *(int*)(selfRec + 0xaa8) == 0) {
                if (DAT_805015bd == '\n') {
                    flags = *(unsigned int*)(selfRec + 0x14);
                }
                else {
                    flags = *(unsigned int*)(selfRec + 0x1c);
                }
                if ((flags & 1) == 0) {
                    goto done;
                }
            }
            *(int*)(selfRec + 0xaac) = 0x20;
            *(int*)(selfRec + 0xaa8) = 0;
        }
    }
done:
    FUN_80382734();
    return;
}

// ---------------------------------------------------------------------------
// Address: 0x80283f34 | Name: tracker_nearest_target_distance
// Gather up to 10 candidate targets (co-op partner nodes plus the caller's own
// resolved handle or all tag-matched entities, selected by 'mode': 0 = single
// handle, 1 = resolve-all-by-tag) into a local list, then compute the minimum
// relation/facing-gated squared distance from the current actor to any of them.
double tracker_nearest_target_distance(int a, int b, int mode)
{
    float dx;
    float dy;
    float dz;
    int self;
    int* partner;
    int selfRec;
    unsigned int add;
    int other;
    char* rel;
    int aligned;
    int count;
    int i;
    double dist;
    double best;
    unsigned long long ctx;
    int list[20];

    (void)a;
    (void)b;
    ctx = FUN_803826dc();
    self = (int)((unsigned long long)ctx >> 0x20);
    selfRec = *(int*)(self + 0x7c);
    partner = (int*)tracker_coop_scan_partner_nodes();
    if (partner == (int*)0) {
        if (1 < DAT_80612610) {
            int lo;
            int hi;
            if (DAT_805015bd == '\n') {
                lo = (&DAT_80611d90)[0x3ac];
                hi = (&DAT_80611d90)[0x3ad];
            }
            else {
                lo = (&DAT_80611d90)[0x3ae];
                hi = (&DAT_80611d90)[0x3af];
            }
            if ((hi != 0 || lo != 0) && ((&DAT_80611d90)[0x21] == (&DAT_80611d90)[0x3c9])) {
                count = 2;
                list[0] = *(&DAT_80611d90);
                list[1] = (&DAT_80611d90)[0x3a8];
                goto scored;
            }
        }
        count = 1;
        list[0] = *(&DAT_80611d90);
    }
    else {
        count = 2;
        list[0] = *(&DAT_80611d90);
        list[1] = *partner;
    }
scored:
    {
        int leader = *(int*)(selfRec + 4);
        float thr = FLOAT_80619318;
        if (((leader != 0) && (*(int*)(leader + 0x20) == 8)) &&
            (*(int*)(*(int*)(leader + 0x7c) + 8) < DAT_80612610)) {
            thr = FLOAT_80619314;
        }
        if (thr < *(float*)(selfRec + 0xa94)) {
            if (mode == 0) {
                int h = entity_handle_resolve((unsigned int)ctx);
                list[count] = h;
                add = (unsigned int)(list[count] != 0);
            }
            else {
                add = entity_tag_resolve_all((unsigned int)ctx, (int)(list + count), 10 - count);
            }
            count = count + add;
            *(float*)(selfRec + 0xa94) = FLOAT_8061930c;
        }
    }
    best = (double)FLOAT_80619300;
    i = 0;
    for (int idx = 0; idx < count; idx = idx + 1) {
        other = list[idx];
        dx = *(float*)(self + 100) - *(float*)(other + 100);
        dy = *(float*)(self + 0x68) - *(float*)(other + 0x68);
        dz = *(float*)(self + 0x6c) - *(float*)(other + 0x6c);
        dist = (double)(dz * dz + dx * dx + dy * dy);
        if (*(int*)(other + 0x20) == 8) {
            rel = (char*)aiactor_relation_entry(*(int*)(*(int*)(other + 0x7c) + 8),
                                                *(int*)(*(int*)(self + 0x7c) + 8));
            if (*rel == '\0') {
                dist = (double)FLOAT_80619300;
            }
            other = list[idx];
            dx = *(float*)(self + 100) - *(float*)(other + 100);
            dy = *(float*)(self + 0x68) - *(float*)(other + 0x68);
            dz = *(float*)(self + 0x6c) - *(float*)(other + 0x6c);
            if ((FLOAT_80619364 < dz * dz + dx * dx + dy * dy) &&
                (aligned = aiactor_facing_dot_test((double)*(float*)(selfRec + 0x9bc), self,
                                                   (float*)(other + 100)),
                 aligned == 0)) {
                dist = (double)FLOAT_80619300;
            }
        }
        if (list[idx] == *(int*)(selfRec + 4)) {
            dist = (double)(float)(dist * (double)FLOAT_80619304);
        }
        if (dist < best) {
            best = dist;
        }
        i = i + 4;
    }
    (void)i;
    FUN_80382728();
    return best;
}

// ---------------------------------------------------------------------------
// Address: 0x80284220 | Name: tracker_nearest_target_by_handle
// Thin wrapper: nearest-target distance in single-handle mode.
double tracker_nearest_target_by_handle(int a, int b)
{
    return tracker_nearest_target_distance(a, b, 0);
}

// ---------------------------------------------------------------------------
// Address: 0x80284244 | Name: tracker_nearest_target_by_tag
// Thin wrapper: nearest-target distance in resolve-all-by-tag mode.
double tracker_nearest_target_by_tag(int a, int b)
{
    return tracker_nearest_target_distance(a, b, 1);
}

// ---------------------------------------------------------------------------
// Address: 0x80284268 | Name: tracker_death_sequence_tick
// Downed / dying actor state machine (state field piVar[8], values 0-5): drives
// fall physics, get-up recovery, weapon drop, ragdoll cleanup and the transition
// to the death finalizer. State transitions record the previous state and a
// timestamp at piVar[0x275]/[0x276]/[0x274]. Locks context via FUN_803826f0 /
// FUN_8038273c.
void tracker_death_sequence_tick(unsigned long long a, double b, double c, double d,
                                 double e, double f, double g, double h)
{
    float fv;
    float fv2;
    float fv3;
    int self;
    int slot;
    int handle;
    unsigned int flags;
    bool cond;
    int* piVar;
    double dv;
    unsigned long long uv;
    double extra = 0.0;
    int state;

    self = FUN_803826f0();
    piVar = *(int**)(self + 0x7c);
    dv = (double)aiactor_retarget_visible_teammate();
    if (-1 < piVar[0x274]) {
        if (piVar[0x274] != piVar[8]) {
            piVar[0x276] = (int)FLOAT_8061930c;
            piVar[0x275] = piVar[8];
            piVar[8] = piVar[0x274];
        }
        piVar[0x274] = -1;
    }
    if (((unsigned int)piVar[0xb] & 0x80) == 0) {
        piVar[0x2a4] = (int)FLOAT_8061930c;
    }
    else {
        double num = (double)FLOAT_80619318;
        double scale = (double)FLOAT_806108ec;
        dv = (double)(float)(num /
             (double)(float)((double)(long long)((unsigned int)DAT_806109c0 ^ 0x80000000) -
                             DOUBLE_80619328));
        piVar[0x2a4] = (int)(float)(scale * dv + (double)(float)piVar[0x2a4]);
    }
    if (DAT_805015bd == '\n') {
        tracker_scan_and_alert_threats();
    }
    state = piVar[8];
    if (state == 3) {
        slot = FUN_80364c40(piVar + 0x13);
        if (slot == 0) {
            if (piVar[8] != 5) {
                piVar[0x275] = piVar[8];
                fv = FLOAT_8061930c;
                piVar[8] = 5;
                piVar[0x276] = (int)fv;
                piVar[0x274] = piVar[8];
            }
        }
        else {
            FUN_803652bc(self, piVar[0x13]);
            FUN_80364cdc();
            if (((*(unsigned int*)(piVar[0x2fc] + 0x58) & 0x2000) == 0) &&
                (flags = FUN_80365914(piVar[0x13]), flags != 0)) {
                if (*(int**)(self + 0x188) != (int*)0) {
                    (*(void(**)(int, int))(self + 0x188))(self, 0);
                }
                if (piVar[8] != 5) {
                    piVar[0x275] = piVar[8];
                    fv = FLOAT_8061930c;
                    piVar[8] = 5;
                    piVar[0x276] = (int)fv;
                    piVar[0x274] = piVar[8];
                }
            }
        }
    }
    else if (state < 3) {
        if (state == 1) {
            fv2 = -(FLOAT_8061936c * FLOAT_806108ec - *(float*)(self + 0xb8));
            fv = fv2;
            if (FLOAT_8061930c < fv2) {
                fv = FLOAT_8061930c;
            }
            fv3 = FLOAT_80619370;
            if ((FLOAT_80619370 <= fv) && (fv3 = fv2, FLOAT_8061930c < fv2)) {
                fv3 = FLOAT_8061930c;
            }
            *(float*)(self + 0xb8) = fv3;
            fv = FLOAT_8061934c;
            *(float*)(self + 100) = *(float*)(self + 0xb4) * FLOAT_806108ec + *(float*)(self + 100);
            *(float*)(self + 0x68) = *(float*)(self + 0x68) + *(float*)(self + 0xb8);
            *(float*)(self + 0x6c) = *(float*)(self + 0xbc) * FLOAT_806108ec + *(float*)(self + 0x6c);
            if ((fv < (float)piVar[0x2cc]) && (piVar[8] != 5)) {
                piVar[0x275] = piVar[8];
                fv = FLOAT_8061930c;
                piVar[8] = 5;
                piVar[0x276] = (int)fv;
                piVar[0x274] = piVar[8];
            }
        }
        else if (state < 1) {
            if (-1 < state) {
                aiactor_set_stance_flags(self, 8);
                flags = (unsigned int)piVar[10] & 0xfffffffd;
                piVar[10] = flags;
                piVar[0x2eb] = 0;
                *(unsigned int*)(self + 0x28) = *(unsigned int*)(self + 0x28) & 0xffffffbf;
                slot = FUN_80364c40(piVar + 0x13);
                if (slot == 0) {
                    if (((unsigned int)piVar[0xb] & 0x80000000) == 0) {
                        if (*(short*)(piVar[0x2fc] + 0x94) != -1) {
                            unsigned long long ac = anim_clip_flags_active_slot(self, flags, 0, 0x20);
                            flags = (unsigned int)ac;
                            if ((int)(ac >> 0x20) == 0) {
                                player_ai_combat_behavior_tick();
                                uv = 0;
                                flags = (unsigned int)piVar[10];
                            }
                        }
                        if (((unsigned int)piVar[0xb] & 0x80000) == 0) {
                            if ((((unsigned int)piVar[0xb] & 0x40000000) == 0) ||
                                (dv = (double)FLOAT_8061930c,
                                 dv != (double)*(float*)(piVar[0x2fc] + 0x84))) {
                                slot = anim_clip_flags_active_slot(self, flags, 0x1800, 0x82);
                                if (slot == 0) {
                                    entity_register_for_update(self, 0xb, -0x7ffe03cc);
                                    fv2 = FLOAT_80619368;
                                    fv = FLOAT_8061930c;
                                    dv = (double)FLOAT_8061930c;
                                    *(unsigned int*)(self + 0x10) =
                                        *(unsigned int*)(self + 0x10) | 0xffff;
                                    piVar[0x2cf] = (int)fv;
                                    *(float*)(self + 100) = fv;
                                    *(float*)(self + 0x68) = fv;
                                    *(float*)(self + 0x6c) = fv;
                                    *(float*)(self + 0xec) = fv2;
                                    *(float*)(self + 0xf0) = fv2;
                                    *(float*)(self + 0xf4) = fv2;
                                    *(float*)(self + 0x70) = fv;
                                    *(float*)(self + 0xc0) = fv;
                                    *(float*)(self + 200) = fv;
                                    *(float*)(self + 0xc4) = fv;
                                    anim_set_playback_rate(dv, *(int**)(self + 4), 1);
                                }
                                aiactor_movement_tick(a, b, c, d, e, f, g, h);
                                if (piVar[8] != 2) {
                                    piVar[0x275] = piVar[8];
                                    fv = FLOAT_8061930c;
                                    piVar[8] = 2;
                                    piVar[0x276] = (int)fv;
                                    piVar[0x274] = piVar[8];
                                }
                            }
                            else {
                                slot = FUN_80365694();
                                if (slot == -1) {
                                    aiactor_movement_tick(a, b, c, d, e, f, g, h);
                                    if (piVar[8] != 2) {
                                        piVar[0x275] = piVar[8];
                                        fv = FLOAT_8061930c;
                                        piVar[8] = 2;
                                        piVar[0x276] = (int)fv;
                                        piVar[0x274] = piVar[8];
                                    }
                                }
                                else {
                                    handle = FUN_80364808(a, b, c, d, e, f, g, h, self, slot,
                                        *(unsigned int*)(piVar[0x2fc] + 0x80) &
                                        (0xffffffffU -
                                         ((int)*(unsigned int*)(piVar[0x2fc] + 0x80) >> 0x1f)));
                                    piVar[0x13] = handle;
                                    if (handle == 0) {
                                        FUN_803656c4(slot);
                                        aiactor_movement_tick(a, b, c, d, e, f, g, h);
                                        if (piVar[8] != 2) {
                                            piVar[0x275] = piVar[8];
                                            fv = FLOAT_8061930c;
                                            piVar[8] = 2;
                                            piVar[0x276] = (int)fv;
                                            piVar[0x274] = piVar[8];
                                        }
                                    }
                                    else {
                                        anim_clip_field_b08(self);
                                        anim_start_clip((double)FLOAT_80619348,
                                                        (double)FLOAT_80619318);
                                        if (piVar[8] != 3) {
                                            piVar[0x275] = piVar[8];
                                            fv = FLOAT_8061930c;
                                            piVar[8] = 3;
                                            piVar[0x276] = (int)fv;
                                            piVar[0x274] = piVar[8];
                                        }
                                    }
                                }
                            }
                        }
                        else {
                            aiactor_movement_tick(a, b, c, d, e, f, g, h);
                            fv = FLOAT_8061930c;
                            *(float*)(self + 0xb8) = FLOAT_8061930c;
                            if (piVar[8] != 1) {
                                piVar[0x275] = piVar[8];
                                piVar[8] = 1;
                                piVar[0x276] = (int)fv;
                                piVar[0x274] = piVar[8];
                            }
                        }
                    }
                    else if (piVar[8] != 5) {
                        piVar[0x275] = piVar[8];
                        fv = FLOAT_8061930c;
                        piVar[8] = 5;
                        piVar[0x276] = (int)fv;
                        piVar[0x274] = piVar[8];
                    }
                }
                else {
                    FUN_803652bc(self, piVar[0x13]);
                    FUN_80364cdc();
                    if (piVar[8] != 3) {
                        piVar[0x275] = piVar[8];
                        fv = FLOAT_8061930c;
                        piVar[8] = 3;
                        piVar[0x276] = (int)fv;
                        piVar[0x274] = piVar[8];
                    }
                }
            }
        }
        else {
            aiactor_movement_tick(a, b, c, d, e, f, g, h);
            if ((*(unsigned int*)(piVar[0x2fc] + 0x58) & 0x40) == 0) {
                if ((*(unsigned int*)(piVar[0x2fc] + 0x58) & 0x800) != 0) {
                    piVar[10] = piVar[10] | 0x4000;
                }
            }
            else {
                piVar[0x2cf] = (int)FLOAT_8061930c;
            }
            if (((((unsigned int)piVar[0xb] & 0x40000000) != 0) &&
                 (dv = (double)*(float*)(piVar[0x2fc] + 0x84), (double)FLOAT_8061930c < dv)) &&
                (dv = anim_primary_clip_phase(*(int*)(self + 4)),
                 (double)*(float*)(piVar[0x2fc] + 0x84) < dv) &&
                (slot = FUN_80365694(), slot != -1)) {
                handle = FUN_80364808(a, b, c, d, e, f, g, h, self, slot,
                    *(unsigned int*)(piVar[0x2fc] + 0x80) &
                    (0xffffffffU - ((int)*(unsigned int*)(piVar[0x2fc] + 0x80) >> 0x1f)));
                piVar[0x13] = handle;
                if (handle == 0) {
                    FUN_803656c4(slot);
                }
                else {
                    anim_clip_field_b08(self);
                    anim_start_clip((double)FLOAT_80619348, (double)FLOAT_80619318);
                    if (piVar[8] != 3) {
                        piVar[0x275] = piVar[8];
                        fv = FLOAT_8061930c;
                        piVar[8] = 3;
                        piVar[0x276] = (int)fv;
                        piVar[0x274] = piVar[8];
                    }
                }
            }
            slot = anim_active_slot_userflags(*(int*)(self + 4));
            if (slot == 0) {
                if ((*(unsigned int*)(piVar[0x2fc] + 0x58) & 0x200) != 0) {
                    unsigned int hgt = floor_height_from_position((double)FLOAT_80619348,
                                                                  (double)FLOAT_80619348);
                    *(unsigned int*)(self + 0x34) = hgt;
                    if (*(int*)(self + 0x34) == 0) {
                        *(int*)(self + 0x34) = 0xd5;
                    }
                }
                projectile_update_surface_aim(self, 1);
            }
            else if (((unsigned int)piVar[10] & 0x1000000) == 0) {
                if ((*(unsigned int*)(piVar[0x2fc] + 0x58) & 0x40) == 0) {
                    if (*(char*)(DAT_80611b1c + *(int*)(self + 0x34)) == '\0') {
                        if (piVar[8] != 5) {
                            piVar[0x275] = piVar[8];
                            fv = FLOAT_8061930c;
                            piVar[8] = 5;
                            piVar[0x276] = (int)fv;
                            piVar[0x274] = piVar[8];
                        }
                    }
                    else if (piVar[8] != 4) {
                        piVar[0x275] = piVar[8];
                        fv = FLOAT_8061930c;
                        piVar[8] = 4;
                        piVar[0x276] = (int)fv;
                        piVar[0x274] = piVar[8];
                    }
                }
                else {
                    slot = aiactor_squad_contains_member(*(int*)(self + 0x34), 1);
                    if ((slot == 0) && (piVar[8] != 5)) {
                        piVar[0x275] = piVar[8];
                        fv = FLOAT_8061930c;
                        piVar[8] = 5;
                        piVar[0x276] = (int)fv;
                        piVar[0x274] = piVar[8];
                    }
                }
            }
            else if (piVar[8] != 5) {
                piVar[0x275] = piVar[8];
                fv = FLOAT_8061930c;
                piVar[8] = 5;
                piVar[0x276] = (int)fv;
                piVar[0x274] = piVar[8];
            }
            cond = anim_entity_has_data(self);
            if (cond) {
                slot = anim_event_has_passed(self, 0x200, piVar[0x277] + 1);
                if (slot != 0) {
                    piVar[10] = piVar[10] | 2;
                    piVar[0x277] = piVar[0x277] + 1;
                }
                if ((((piVar[0xaf] != 0) || (piVar[0x25] == 0x42)) || (piVar[0x25] == 0x43)) &&
                    (self = anim_event_has_passed(self, 0x4000, 1), self != 0)) {
                    player_ai_drop_all_weapons(a, b, c, d, e, f, g, h, piVar);
                }
            }
        }
    }
    else if (state == 5) {
        aiactor_movement_tick(a, b, c, d, e, f, g, h);
        tracker_death_finalize(a, b, c, d, e, f, g, h, self);
    }
    else if (state < 5) {
        slot = game_is_standard_arcade();
        if ((slot != 0) ||
            (((*(unsigned int*)(self + 0x28) & 1) != 0) &&
             ((*(unsigned int*)(self + 0x10) & 0xffff) != 0))) {
            projectile_spawn(a, b, c, d, e, f, g, h);
            FUN_8037ed6c();
        }
        if (piVar[8] != 5) {
            piVar[0x275] = piVar[8];
            fv = FLOAT_8061930c;
            piVar[8] = 5;
            piVar[0x276] = (int)fv;
            piVar[0x274] = piVar[8];
        }
    }
    (void)dv;
    (void)uv;
    (void)cond;
    (void)extra;
    FUN_8038273c();
    return;
}

// ---------------------------------------------------------------------------
// Address: 0x80284ba4 | Name: tracker_death_finalize
// Death-state finalizer (called from state 5 of tracker_death_sequence_tick):
// in the arcade/map100 co-op variant, run the arcade hook then destroy the
// instance; otherwise clear the actor's active flags, request the death-drop
// behaviour state (0xaac == 2), play the death anim, dispatch the projectile
// death effect, and release the held grab handle.
void tracker_death_finalize(unsigned long long a, double b, double c, double d,
                            unsigned long long e, unsigned long long f,
                            unsigned long long g, unsigned long long h, int actor)
{
    unsigned int flags;
    int selfRec;
    unsigned long long uv;

    selfRec = *(int*)(actor + 0x7c);
    FUN_8037ee2c();
    if (DAT_805015bd == '\n') {
        uv = worm_map100_arcade_hook(actor);
        aiactor_destroy_instance((double)uv, b, c, d, e, f, g, h, actor);
        return;
    }
    *(unsigned int*)(selfRec + 0x2c) = *(unsigned int*)(selfRec + 0x2c) & 0xffefffff;
    *(unsigned int*)(actor + 0x10) = *(unsigned int*)(actor + 0x10) & 0xffff0000;
    *(unsigned int*)(actor + 0x28) = *(unsigned int*)(actor + 0x28) | 0x60;
    if (*(int*)(selfRec + 0xaac) == 1 && *(int*)(selfRec + 0xaa8) == 0) {
        if (DAT_805015bd == '\n') {
            flags = *(unsigned int*)(selfRec + 0x14);
        }
        else {
            flags = *(unsigned int*)(selfRec + 0x1c);
        }
        if ((flags & 1) == 0) {
            goto done;
        }
    }
    *(int*)(selfRec + 0xaac) = 2;
    *(int*)(selfRec + 0xaa8) = 0;
done:
    anim_play_scaled((double)FLOAT_8061930c, (double)FLOAT_80619374);
    player_ai_projectile_death_dispatch();
    *(unsigned int*)(*(int*)(actor + 4) + 0x19c) =
        *(unsigned int*)(*(int*)(actor + 4) + 0x19c) & 0xfffffffe;
    selfRec = FUN_80364c40((int*)(selfRec + 0x4c));
    if (selfRec != 0) {
        FUN_803654f0();
    }
    return;
}

// ---------------------------------------------------------------------------
// Address: 0x80284ce0 | Name: tracker_subobj_impact_push
// Physics push + impact-damage handler for a scripted sub-object (thrown /
// swept prop): resolves the sub-object flags, and if the object is active and
// not idle, computes a push along the direction vector 'dir', performs a
// nav-collision-checked move, and on impact (blocked or non-grabbable) applies
// damage or a death handler scaled by the object's mass. Locks context via
// FUN_803826d4 / FUN_80382720.
void tracker_subobj_impact_push(unsigned long long a, double b, double c, double d,
                                unsigned long long e, unsigned long long f,
                                unsigned long long g, unsigned long long h,
                                int p9, int p10, float* dir, int p12)
{
    float rate;
    bool bigImpact;
    bool notGetUp;
    bool blocked;
    int self;
    unsigned int flags;
    unsigned int stateWord;
    int slot;
    unsigned int gate;
    int selfRec;
    int idx;
    double dv = c;
    double amount = 0.0;
    unsigned long long ctx;
    float local_f0;
    float local_ec;
    float local_e8;
    int navScratch[11];
    int dmgRec[3];      // local_b8 / local_b4 / local_b0
    int dmgA0;
    float dmg98;
    int dmg94;
    int dmg90;
    float* dmg88;
    float dmg84;
    int dmg70;
    float dmg6c;
    float dmg68;

    (void)b;
    ctx = FUN_803826d4();
    self = (int)((unsigned long long)ctx >> 0x20);
    slot = (int)ctx;
    selfRec = *(int*)(self + 0x7c);
    flags = subobj_get_flags(*(unsigned int*)(slot + 0x80));
    amount = ABS(amount);
    if (((*(short*)(selfRec + 0x48) != 0x42) &&
         (((*(int*)(selfRec + 0xbf0) == 0 ||
            (gate = tracker_level_node_is_coop_tag(*(int*)(selfRec + 0xbf0)), gate == 0)) ||
           ((flags & 0x2000) != 0)))) &&
        ((slot = FUN_80364c40((int*)(selfRec + 0x4c)), slot == 0 &&
          ((*(unsigned int*)(selfRec + 0x24) & 8) == 0)))) {
        if (DAT_805015bd == '\n') {
            stateWord = *(unsigned int*)(selfRec + 0x14);
        }
        else {
            stateWord = *(unsigned int*)(selfRec + 0x1c);
        }
        if ((((stateWord & 1) == 0) && ((*(unsigned int*)(selfRec + 0xaac) & 1) == 0)) &&
            (((flags & 0x2800) != 0) &&
             (stateWord = subobj_is_idle(*(unsigned int*)((int)ctx + 0x80)), stateWord == 0))) {
            unsigned int grabbable = flags & 0x2000;
            bigImpact = false;
            if (((grabbable != 0) &&
                 (dv < -(double)*(float*)(&DAT_8048ca9c + *(short*)(selfRec + 0x48) * 0x30))) ||
                ((flags & 0x80000) != 0)) {
                bigImpact = true;
            }
            notGetUp = true;
            blocked = false;
            slot = anim_is_clip_active(self, 0x92);
            if (((slot != 0) || (slot = anim_is_clip_active(self, 0x95), slot != 0)) ||
                ((slot = anim_is_clip_active(self, 0x93), slot != 0 ||
                  ((slot = anim_is_clip_active(self, 0x96), slot != 0 ||
                    (slot = anim_is_clip_active(self, 0x94), slot != 0)))))) {
                blocked = true;
            }
            if ((!blocked) && (slot = anim_is_clip_active(self, 0x97), slot == 0)) {
                notGetUp = false;
            }
            if ((!bigImpact) && (grabbable != 0)) {
                double push = -dv;
                double zc = (double)(float)((double)dir[1] * push);
                local_f0 = (float)((double)*dir * push) + *(float*)(self + 100);
                local_ec = (float)(zc + (double)*(float*)(self + 0x68));
                local_e8 = (float)((double)dir[2] * push) + *(float*)(self + 0x6c);
                unsigned int keep = *(unsigned int*)(slot + 0x28) & 0x60;
                *(unsigned int*)(slot + 0x28) = *(unsigned int*)(slot + 0x28) & ~keep;
                idx = *(short*)(selfRec + 0x48) * 0x30;
                gate = nav_movement_dispatch((double)*(float*)(&DAT_8048ca94 + idx),
                                             (double)*(float*)(&DAT_8048ca9c + idx),
                                             *(int*)(selfRec + 8), self + 100, &local_f0,
                                             (float*)0, navScratch, 1, 0);
                blocked = (gate & 0xe) != 0;
                *(unsigned int*)(slot + 0x28) = *(unsigned int*)(slot + 0x28) | keep;
                if (!blocked) {
                    *(float*)(self + 100) = local_f0;
                    *(float*)(self + 0x68) = local_ec;
                    *(float*)(self + 0x6c) = local_e8;
                }
            }
            if (((blocked) || (!notGetUp)) && ((double)FLOAT_8061930c < dv)) {
                int partner = tracker_coop_scan_partner_nodes();
                rate = FLOAT_80619348;
                if (partner != selfRec) {
                    double invMass = subobj_get_inverse_mass(*(unsigned int*)(slot + 0x80));
                    rate = FLOAT_8061937c;
                    if (grabbable != 0) {
                        rate = FLOAT_80619378;
                    }
                    rate = (float)(ABS(amount) *
                           (double)(float)(((int)(float)invMass >> 1) + 0x1fc00000)) *
                           rate + FLOAT_80619348;
                }
                dv = (double)rate;
                switch_slot_reset((int)&dmgRec[0]);
                if (*(int*)(slot + 0x20) == 0x400000) {
                    subobj_find_active_grab(slot, &dmgRec[1]);
                }
                else if ((DAT_805015bd == '\n') &&
                         (flags = subobj_get_flags(*(unsigned int*)(slot + 0x80)),
                          (flags & 0x1000000) != 0)) {
                    dmgRec[1] = *(int*)(DAT_80611d78 + 0x14);
                }
                else {
                    dmgRec[1] = 0;
                }
                dmgA0 = 0;
                dmg70 = 0;
                dmg94 = 0;
                dmg84 = (float)amount;
                if (grabbable != 0) {
                    dmgA0 = 0x24;
                }
                dmg6c = FLOAT_80619380;
                dmg68 = FLOAT_80619384;
                double clamp = (double)FLOAT_80619314;
                if (dv < (double)FLOAT_80619314) {
                    clamp = dv;
                }
                dmg98 = (float)clamp;
                dmgRec[0] = self;
                dmgRec[2] = slot;
                dmg90 = p12;
                dmg88 = dir;
                if (blocked) {
                    character_on_death_handler((double)FLOAT_80619384, (double)FLOAT_80619380,
                                               c, d, e, f, g, h);
                }
                else {
                    damage_dispatch_to_entity((double)FLOAT_80619384, (double)FLOAT_80619380,
                                              c, d, e, f, g, h, &dmgRec[0]);
                }
                (void)dmgA0; (void)dmg98; (void)dmg94; (void)dmg90; (void)dmg88;
                (void)dmg84; (void)dmg70; (void)dmg6c; (void)dmg68;
            }
        }
    }
    (void)p9; (void)p10; (void)dv;
    FUN_80382720();
    return;
}


// ===========================================================================
// batch_761 - AI actor turn/alert/anim helpers + melee & creature behaviour ticks
// Address range: 0x80285120 - 0x802873bb | aiactor_script_fsm.cpp
// ===========================================================================

// ---------------------------------------------------------------------------
// Address: 0x80285120 | Name: tracker_integrate_turn_rate
// Integrate the actor's signed turn-rate field (+0xb28) by the per-frame step
// (+0xb24) toward or away from the clamp magnitude (+0xad0), selected by the
// state flags at +0x24: bit0 accelerate, bit4 decelerate (reset clamp from
// +0xb14 first), bit2 return-to-centre.
void tracker_integrate_turn_rate(int actor)
{
    float zero;
    float cur;
    unsigned int flags;
    int self;

    zero = FLOAT_8061930c;
    self = *(int*)(actor + 0x7c);
    flags = *(unsigned int*)(self + 0x24);
    if ((flags & 1) != 0) {
        *(float*)(self + 0xb28) = *(float*)(self + 0xb28) + *(float*)(self + 0xb24);
        if (*(float*)(self + 0xad0) < *(float*)(self + 0xb28)) {
            *(float*)(self + 0xb28) = *(float*)(self + 0xad0);
        }
        if (-*(float*)(self + 0xad0) <= *(float*)(self + 0xb28)) {
            return;
        }
        *(float*)(self + 0xb28) = -*(float*)(self + 0xad0);
        return;
    }
    if ((flags & 0x10) != 0) {
        *(int*)(self + 0xad0) = *(int*)(self + 0xb14);
        *(float*)(self + 0xb28) = *(float*)(self + 0xb28) - *(float*)(self + 0xb24);
        if (*(float*)(self + 0xad0) < *(float*)(self + 0xb28)) {
            *(float*)(self + 0xb28) = *(float*)(self + 0xad0);
        }
        if (-*(float*)(self + 0xad0) <= *(float*)(self + 0xb28)) {
            return;
        }
        *(float*)(self + 0xb28) = -*(float*)(self + 0xad0);
        return;
    }
    if ((flags & 4) == 0) {
        return;
    }
    cur = *(float*)(self + 0xb28);
    if (FLOAT_8061930c < cur) {
        *(float*)(self + 0xb28) = cur - *(float*)(self + 0xb24);
        if (zero <= *(float*)(self + 0xb28)) {
            return;
        }
        *(float*)(self + 0xb28) = zero;
        return;
    }
    if (FLOAT_8061930c <= cur) {
        return;
    }
    *(float*)(self + 0xb28) = cur + *(float*)(self + 0xb24);
    if (*(float*)(self + 0xb28) <= zero) {
        return;
    }
    *(float*)(self + 0xb28) = zero;
    return;
}

// ---------------------------------------------------------------------------
// Address: 0x8028521c | Name: tracker_request_alert_state
// Snapshot the actor's active behaviour-state word pair (self+0x10/+0x14 in the
// story-coop layout, else self+0x18/+0x1c) into the "previous state" fields
// (+0xab8/+0xabc) and request a transition to the alert/investigate behaviour
// state (0x20000000) via the pending-state fields (+0xaac/+0xaa8).
void tracker_request_alert_state(int actor)
{
    int lo;
    int hi;
    unsigned int flags;
    int self;

    self = *(int*)(actor + 0x7c);
    if (DAT_805015bd == '\n') {
        lo = *(int*)(self + 0x10);
        hi = *(int*)(self + 0x14);
    }
    else {
        lo = *(int*)(self + 0x18);
        hi = *(int*)(self + 0x1c);
    }
    *(int*)(self + 0xabc) = hi;
    *(int*)(self + 0xab8) = lo;
    if (*(int*)(self + 0xaac) == 1 && *(int*)(self + 0xaa8) == 0) {
        if (DAT_805015bd == '\n') {
            flags = *(unsigned int*)(self + 0x14);
        }
        else {
            flags = *(unsigned int*)(self + 0x1c);
        }
        if ((flags & 1) == 0) {
            return;
        }
    }
    *(int*)(self + 0xaac) = 0x20000000;
    *(int*)(self + 0xaa8) = 0;
    return;
}

// ---------------------------------------------------------------------------
// Address: 0x802852c4 | Name: tracker_movement_progress_check
// Timer-gated (+0xb38) progress probe: once the timer elapses, measure the
// squared displacement since the last sample (+0xba0/+0xba4/+0xba8); if the
// actor is playing a locomotion clip, not blocked, and either within reach or
// its target zone is reachable, set the "made progress" bit (+0x28 bit0), then
// re-sample. Locks the actor context via FUN_803826f0 / FUN_8038273c.
void tracker_movement_progress_check(void)
{
    float dx;
    float dy;
    float dz;
    bool progress;
    int self;
    int selfRec;
    int clip;
    unsigned int reach;
    int slot;
    double radius;
    double distSq;
    unsigned long long ctx;

    ctx = FUN_803826f0();
    self = (int)((unsigned long long)ctx >> 0x20);
    slot = (int)ctx;
    selfRec = *(int*)(self + 0x7c);
    progress = false;
    *(unsigned int*)(selfRec + 0x28) = *(unsigned int*)(selfRec + 0x28) & 0xfffffffe;
    if (FLOAT_80619388 <= *(float*)(selfRec + 0xb38)) {
        float rad = FLOAT_80619390;
        if ((*(unsigned int*)(selfRec + 0x2c) & 8) != 0) {
            rad = FLOAT_8061938c;
        }
        radius = (double)rad;
        dx = *(float*)(self + 0x68) - *(float*)(selfRec + 0xba4);
        dy = *(float*)(self + 100) - *(float*)(selfRec + 0xba0);
        dz = *(float*)(self + 0x6c) - *(float*)(selfRec + 0xba8);
        distSq = (double)(dz * dz + dy * dy + dx * dx);
        clip = anim_primary_clip_has_flags(self, slot, 0, 0x200);
        if (((clip != 0) &&
             (((spawn_pack_speed_changed(*(int*)(self + 4)) == 0) ||
               (clip = anim_secondary_clip_has_flags(self, slot, 0, 0x200), clip != 0)) &&
              ((*(unsigned int*)(selfRec + 0x2c) & 0x10000) == 0))) &&
            ((distSq < (double)(float)(radius * radius) ||
              (reach = aiactor_target_zone_reachable(self), reach != 0)))) {
            progress = true;
        }
        if (progress) {
            *(unsigned int*)(selfRec + 0x28) = *(unsigned int*)(selfRec + 0x28) | 1;
        }
        *(float*)(selfRec + 0xb38) = FLOAT_8061930c;
        *(int*)(selfRec + 0xba0) = *(int*)(self + 100);
        *(int*)(selfRec + 0xba4) = *(int*)(self + 0x68);
        *(int*)(selfRec + 0xba8) = *(int*)(self + 0x6c);
    }
    FUN_8038273c();
    return;
}

// ---------------------------------------------------------------------------
// Address: 0x80285478 | Name: tracker_is_special_class_eligible
// Predicate: returns 1 when the actor is eligible for the special-class handling
// path - either it is the level-100 boss class (0x36) with no pending target /
// class-flag, or it is a fresh (unassigned) class with no target/flag - else 0.
int tracker_is_special_class_eligible(int actor)
{
    int self;
    int found;
    int result;

    self = *(int*)(actor + 0x7c);
    if (((DAT_80612614 == 100) && (*(int*)(self + 0x94) == 0x36) && (*(int*)(self + 0x44) == 0)) &&
        (found = tracker_scan_class_flag400(), found == 0)) {
        result = 1;
    }
    else if (((*(int*)(self + 0x94) == 0) && (*(int*)(self + 0x44) == 0)) &&
             ((*(int*)(self + 700) == 0) && (found = tracker_scan_class_flag400(), found == 0))) {
        result = 1;
    }
    else {
        result = 0;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Address: 0x80285528 | Name: tracker_special_anim_gate
// If the actor's lock bit (+0xc6c bit0) is clear: when the "decelerate" state
// (+0x24 bit2) is set play the special clip 0x112; otherwise when the
// "accelerate" state (+0x24 bit0) is set delegate to the idle/fidget selector.
void tracker_special_anim_gate(int actor)
{
    int active;
    unsigned int flags;

    if ((*(unsigned int*)(*(int*)(actor + 0x7c) + 0xc6c) & 1) == 0) {
        flags = *(unsigned int*)(*(int*)(actor + 0x7c) + 0x24);
        if ((flags & 4) == 0) {
            if ((flags & 1) != 0) {
                tracker_play_idle_fidget_anim(actor);
            }
        }
        else {
            active = anim_is_clip_active(actor, 0x112);
            if (active == 0) {
                anim_play_scaled((double)FLOAT_80619398, (double)FLOAT_8061939c);
            }
        }
    }
    return;
}

// ---------------------------------------------------------------------------
// Address: 0x802855a0 | Name: tracker_play_idle_fidget_anim
// Idle / fidget animation selector. Gated by the actor lock bit (+0xc6c) and the
// level-node "no fidget" flag (+0xbf0 +0x58 bit 0x4000). Chooses among the idle
// (0x113/0x114), fidget (0x115), carry-idle (0x123) and locked (0x120) clips,
// with rng-gated variant selection.
void tracker_play_idle_fidget_anim(int actor)
{
    int active;
    unsigned int rng;
    int self;

    self = *(int*)(actor + 0x7c);
    if ((*(unsigned int*)(self + 0xc6c) & 1) == 0) {
        if ((*(unsigned int*)(*(int*)(self + 0xbf0) + 0x58) & 0x4000) == 0) {
            if ((*(int*)(self + 700) == 0) && (*(int*)(self + 0x18c) == 0)) {
                active = anim_is_clip_active(actor, 0x113);
                if ((active == 0) && (active = anim_is_clip_active(actor, 0x114), active == 0)) {
                    active = anim_primary_clip_userflags(actor, 0x115);
                    if ((active != 0) ||
                        (((active = anim_is_clip_active(actor, 0x115), active == 0 &&
                           (active = anim_is_clip_active(actor, 0x113), active == 0)) &&
                          (active = anim_is_clip_active(actor, 0x114), active == 0)))) {
                        rng = rng_counter_tick();
                        if (FLOAT_806193a8 <=
                            (float)((double)(long long)rng - DOUBLE_806193b0) / FLOAT_806193a0) {
                            anim_play_scaled((double)FLOAT_80619398, (double)*(float*)(self + 0xb18));
                        }
                        else {
                            anim_play_scaled((double)FLOAT_80619398, (double)*(float*)(self + 0xb18));
                        }
                    }
                }
                else {
                    active = anim_event_scan_active();
                    if (active != 0) {
                        rng = rng_counter_tick();
                        if ((float)((double)(long long)rng - DOUBLE_806193b0) / FLOAT_806193a0 <
                            FLOAT_806193a4) {
                            anim_play_scaled((double)FLOAT_80619398, (double)*(float*)(self + 0xb18));
                        }
                    }
                }
            }
            else {
                active = anim_is_clip_active(actor, 0x123);
                if (active == 0) {
                    anim_play_scaled((double)FLOAT_80619398, (double)*(float*)(self + 0xb18));
                }
            }
        }
        else {
            active = anim_is_clip_active(actor, 0x120);
            if (active == 0) {
                anim_play_scaled((double)FLOAT_80619398, (double)FLOAT_8061939c);
            }
        }
    }
    return;
}

// ---------------------------------------------------------------------------
// Address: 0x80285788 | Name: tracker_melee_combat_tick
// Per-frame melee-combatant behaviour: a 4-state sub-FSM (piVar[8]: 0 acquire,
// 1 approach, 2 attack, 3 special) driving combat clips (0x116-0x119 lunge/bite,
// 0x123-0x126 special), melee swipes, combat grunt SFX (0xd31/0xd32), leader
// zone update, and a target-lost state request (+0x2ab = 0x10). Locks context
// via FUN_803826e8 / FUN_80382734.
void tracker_melee_combat_tick(void)
{
    float fv;
    bool hasClip;
    int self;
    int retarget;
    int state;
    int hit;
    int slot;
    unsigned int rng;
    int* piVar;
    double weight;
    int soundParams[7];
    int sndPosX;
    int sndPosY;
    int sndPosZ;
    unsigned char sndFlag;

    self = FUN_803826e8();
    piVar = *(int**)(self + 0x7c);
    retarget = aiactor_retarget_visible_teammate();
    if (-1 < piVar[0x274]) {
        if (piVar[0x274] != piVar[8]) {
            piVar[0x276] = (int)FLOAT_806193b8;
            piVar[0x275] = piVar[8];
            piVar[8] = piVar[0x274];
        }
        piVar[0x274] = -1;
    }
    if (((unsigned int)piVar[0xb] & 0x80) == 0) {
        piVar[0x2a4] = (int)FLOAT_806193b8;
    }
    else {
        unsigned int biased = (unsigned int)DAT_806109c0 ^ 0x80000000;
        piVar[0x2a4] =
            (int)(FLOAT_806108ec *
                  (FLOAT_8061939c / (float)((double)(long long)((unsigned long long)biased | 0x4330000000000000ULL) - DOUBLE_806193d0))
                  + (float)piVar[0x2a4]);
    }
    state = piVar[1];
    if (state != 0) {
        if (*(int*)(state + 0x20) == 8) {
            piVar[0xc] = *(int*)(state + 100);
            piVar[0xd] = *(int*)(piVar[1] + 0x68);
            piVar[0xe] = *(int*)(piVar[1] + 0x6c);
            state = *(int*)(*(int*)(piVar[1] + 0x7c) + 0x678);
            if (state != piVar[0x1a1]) {
                if (state < 0) {
                    piVar[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                }
                else {
                    piVar[0x1a1] = state;
                }
                piVar[0x26b] = 0;
                piVar[0x26a] = 0;
            }
            state = piVar[8];
            if (state == 2) {
                if ((piVar[0xaf] == 0) && (piVar[99] == 0)) {
                    piVar[0x275] = 2;
                    piVar[8] = 0;
                    piVar[0x276] = (int)FLOAT_806193b8;
                    piVar[0x274] = piVar[8];
                }
                else {
                    slot = anim_is_clip_active(self, 0x123);
                    if ((slot == 0) && (slot = anim_is_clip_active(self, 0x124), slot == 0)) {
                        anim_play_scaled((double)FLOAT_80619398, (double)(float)piVar[0x2c6]);
                    }
                    else {
                        slot = anim_active_slot_userflags(*(int*)(self + 4));
                        if (slot != 0) {
                            anim_play_scaled((double)FLOAT_806193b8, (double)FLOAT_8061939c);
                            piVar[0xb] = piVar[0xb] | 0x400;
                        }
                    }
                }
            }
            else if (state < 2) {
                if (state == 0) {
                    if (DAT_80612614 == 100) {
                        piVar[0x25] = object_resolve_group_base_slot(piVar[0x25]);
                    }
                    piVar[1] = aiactor_acquire_nearest_target(self, *(int*)(DAT_80611d78 + 0x14), 0);
                    rng = rng_counter_tick();
                    piVar[0x278] =
                        (int)(FLOAT_8061939c +
                              (float)((double)(long long)rng - DOUBLE_806193b0) / FLOAT_806193a0);
                    if ((piVar[0xaf] == 0) && (piVar[99] == 0)) {
                        if (piVar[8] != 1) {
                            piVar[0x275] = piVar[8];
                            piVar[8] = 1;
                            piVar[0x276] = (int)FLOAT_806193b8;
                            piVar[0x274] = piVar[8];
                        }
                    }
                    else if (piVar[0x25] == 0x36) {
                        if (piVar[8] != 3) {
                            piVar[0x275] = piVar[8];
                            piVar[8] = 3;
                            piVar[0x276] = (int)FLOAT_806193b8;
                            piVar[0x274] = piVar[8];
                        }
                    }
                    else if (piVar[8] != 2) {
                        piVar[0x275] = piVar[8];
                        piVar[8] = 2;
                        piVar[0x276] = (int)FLOAT_806193b8;
                        piVar[0x274] = piVar[8];
                    }
                }
                else if (-1 < state) {
                    int sm = player_ai_is_story_mode((int)*(short*)((int)piVar + 0x4a));
                    fv = FLOAT_806193bc;
                    if (sm != 0) { fv = FLOAT_8061939c; }
                    weight = (double)fv;
                    sm = player_ai_is_story_mode((int)*(short*)((int)piVar + 0x4a));
                    fv = FLOAT_806193bc;
                    if (sm != 0) { fv = FLOAT_8061939c; }
                    if ((float)piVar[0x2bd] <= (float)((double)fv * weight)) {
                        slot = anim_is_clip_active(self, 0x116);
                        if ((((slot == 0) && (slot = anim_is_clip_active(self, 0x117), slot == 0)) &&
                             (slot = anim_is_clip_active(self, 0x119), slot == 0)) &&
                            (slot = anim_is_clip_active(self, 0x118), slot == 0)) {
                            hasClip = false;
                        }
                        else {
                            hasClip = true;
                        }
                        slot = anim_active_slot_userflags(*(int*)(self + 4));
                        if (slot != 0) {
                            anim_play_scaled((double)FLOAT_80619398, (double)FLOAT_8061939c);
                        }
                        if (hasClip) {
                            slot = anim_is_clip_active(self, 0x117);
                            if (slot == 0) {
                                aiactor_melee_swipe(self, 0, 0);
                            }
                            else {
                                aiactor_melee_swipe(self, 1, 0);
                            }
                            hit = anim_active_clip_slot(self);
                            if (((*(unsigned int*)(hit + 0x54) & 8) == 0) &&
                                (slot = anim_event_scan_active(), slot != 0)) {
                                rng = rng_counter_tick();
                                sound_init_play_params((int)soundParams);
                                sndFlag = 1;
                                sndPosX = *(int*)(self + 100);
                                sndPosY = *(int*)(self + 0x68);
                                sndPosZ = *(int*)(self + 0x6c);
                                soundParams[0] = (rng & 1) + 0xd31;
                                (void)sndPosX; (void)sndPosY; (void)sndPosZ; (void)sndFlag;
                                sound_play_request(soundParams);
                                *(unsigned int*)(hit + 0x54) = *(unsigned int*)(hit + 0x54) | 8;
                            }
                        }
                    }
                    else {
                        tracker_play_idle_fidget_anim(self);
                    }
                }
            }
            else if (state < 4) {
                int sm = player_ai_is_story_mode((int)*(short*)((int)piVar + 0x4a));
                fv = FLOAT_806193bc;
                if (sm != 0) { fv = FLOAT_8061939c; }
                weight = (double)fv;
                sm = player_ai_is_story_mode((int)*(short*)((int)piVar + 0x4a));
                fv = FLOAT_806193bc;
                if (sm != 0) { fv = FLOAT_8061939c; }
                if ((float)piVar[0x2bd] <= (float)((double)fv * weight)) {
                    slot = anim_is_clip_active(self, 0x125);
                    if (slot == 0) {
                        anim_play_scaled((double)FLOAT_80619398, (double)(float)piVar[0x2c6]);
                    }
                    else {
                        slot = anim_active_slot_userflags(*(int*)(self + 4));
                        if (slot != 0) {
                            anim_play_scaled((double)FLOAT_806193b8, (double)(float)piVar[0x2c6]);
                        }
                    }
                }
                else {
                    slot = anim_is_clip_active(self, 0x126);
                    if (slot == 0) {
                        anim_play_scaled((double)FLOAT_80619398, (double)(float)piVar[0x2c6]);
                    }
                }
                slot = anim_is_clip_active(self, 0x125);
                if (slot != 0) {
                    aiactor_melee_swipe(self, 1, 1);
                }
            }
            aiactor_leader_zone_update(self);
            if ((((piVar != (int*)0) && (state = *piVar, state != 0)) &&
                 ((DAT_80612610 <= piVar[2] &&
                   ((((*(unsigned int*)(state + 0x28) & 0x100) != 0 ||
                      ((*(unsigned int*)(state + 0x10) & 0xffff) == 0)) &&
                     (piVar[0x25] != 0)))))) && (piVar[0x25] != 0x36)) {
                piVar[0xc] = *(int*)(self + 100);
                piVar[0xd] = *(int*)(self + 0x68);
                piVar[0xe] = *(int*)(self + 0x6c);
            }
            if (piVar[0x2ab] == 1 && piVar[0x2aa] == 0) {
                if (DAT_805015bd == '\n') {
                    rng = (unsigned int)piVar[5];
                }
                else {
                    rng = (unsigned int)piVar[7];
                }
                if ((rng & 1) == 0) {
                    goto done;
                }
            }
            piVar[0x2ab] = 0x10;
            piVar[0x2aa] = 0;
        }
        else {
            aiactor_behavior_combat_tick();
        }
    }
done:
    FUN_80382734();
    return;
}

// ---------------------------------------------------------------------------
// Address: 0x80286040 | Name: tracker_creature_special_tick
// Per-frame "special creature" behaviour: a 4-state FSM (+0x20: 0 idle,
// 1 approach, 2 lunge-attack, 3 retreat). On the lunge attack it fires a beam:
// builds a muzzle transform (mtx4_set_translate_rotz), soft-spring aims the beam
// toward the target with an rsqrt normalise, raycasts the scene, spawns a burst
// of damage-scatter sparks plus a trailing spark stream, and dispatches AoE
// damage at the hit point. Locks context via FUN_803826e8 / FUN_80382734.
void tracker_creature_special_tick(void)
{
    float len;
    float inv;
    int self;
    int retarget;
    int state;
    int hit;
    int selfRec;
    unsigned int biased;
    int i;
    double dv;
    float mtx[16];
    float beamOrigin[3];   // local_1a4/1a0/19c
    float beamDir[3];      // local_1b0/1ac/1a8
    float hitPoint[3];     // local_1c8/1c4/1c0
    float scatter[3];      // local_1d4/1d0/1cc
    float beamEnd[3];      // local_1bc/1b8/1b4
    float rnd[3];          // local_1e0/1dc/1d8
    int dmgRec[3];         // local_128/124/120
    int dmgRec2;           // local_11c
    int dmgRec3;           // local_118
    unsigned int dmgFlagsA;
    unsigned int dmgFlagsB;
    float dmgAmount;       // local_108
    float* dmgHitPtr;      // local_100
    float* dmgDirPtr;      // local_f8
    int soundParams[7];
    int sndPosX, sndPosY, sndPosZ;
    unsigned char sndFlag;

    self = FUN_803826e8();
    selfRec = *(int*)(self + 0x7c);
    retarget = aiactor_retarget_visible_teammate();
    if (-1 < *(int*)(selfRec + 0x9d0)) {
        if (*(int*)(selfRec + 0x9d0) != *(int*)(selfRec + 0x20)) {
            *(float*)(selfRec + 0x9d8) = FLOAT_806193b8;
            *(int*)(selfRec + 0x9d4) = *(int*)(selfRec + 0x20);
            *(int*)(selfRec + 0x20) = *(int*)(selfRec + 0x9d0);
        }
        *(int*)(selfRec + 0x9d0) = -1;
    }
    if ((*(unsigned int*)(selfRec + 0x2c) & 0x80) == 0) {
        *(float*)(selfRec + 0xa90) = FLOAT_806193b8;
    }
    else {
        biased = (unsigned int)DAT_806109c0 ^ 0x80000000;
        *(float*)(selfRec + 0xa90) =
            FLOAT_806108ec *
            (FLOAT_8061939c / (float)((double)(long long)((unsigned long long)biased | 0x4330000000000000ULL) - DOUBLE_806193d0)) +
            *(float*)(selfRec + 0xa90);
    }
    *(int*)(selfRec + 0x30) = *(int*)(self + 100);
    *(int*)(selfRec + 0x34) = *(int*)(self + 0x68);
    *(int*)(selfRec + 0x38) = *(int*)(self + 0x6c);
    aiactor_turn_toward_leader();
    state = *(int*)(selfRec + 0x20);
    if (state == 2) {
        state = anim_is_clip_active(self, 0x121);
        if (state == 0) {
            anim_play_scaled((double)FLOAT_806193c0, (double)FLOAT_806193e0);
        }
        else {
            hit = anim_active_slot_userflags(*(int*)(self + 4));
            if ((hit != 0) && (*(float*)(selfRec + 0x9e0) < *(float*)(selfRec + 0xb30))) {
                if (retarget == 0) {
                    if (*(int*)(selfRec + 0x20) != 0) {
                        *(int*)(selfRec + 0x9d4) = *(int*)(selfRec + 0x20);
                        *(int*)(selfRec + 0x20) = 0;
                        *(float*)(selfRec + 0x9d8) = FLOAT_806193b8;
                        *(int*)(selfRec + 0x9d0) = *(int*)(selfRec + 0x20);
                    }
                }
                else if (*(int*)(selfRec + 0x20) != 3) {
                    *(int*)(selfRec + 0x9d4) = *(int*)(selfRec + 0x20);
                    *(int*)(selfRec + 0x20) = 3;
                    *(float*)(selfRec + 0x9d8) = FLOAT_806193b8;
                    *(int*)(selfRec + 0x9d0) = *(int*)(selfRec + 0x20);
                }
            }
        }
    }
    else if (state < 2) {
        if (state == 0) {
            if (*(int*)(selfRec + 0xbf0) != 0) {
                character_set_damage_bone(self, *(int*)(*(int*)(selfRec + 0xbf0) + 0x74));
            }
            if (*(int*)(selfRec + 0x20) != 1) {
                *(int*)(selfRec + 0x9d4) = *(int*)(selfRec + 0x20);
                *(int*)(selfRec + 0x20) = 1;
                *(float*)(selfRec + 0x9d8) = FLOAT_806193b8;
                *(int*)(selfRec + 0x9d0) = *(int*)(selfRec + 0x20);
            }
            biased = rng_counter_tick();
            *(float*)(selfRec + 0x9e0) =
                FLOAT_806193d8 *
                ((float)((double)(long long)biased - DOUBLE_806193b0) / FLOAT_806193a0) +
                FLOAT_806193d8 + *(float*)(selfRec + 0xb30);
            *(int*)(selfRec + 4) = aiactor_acquire_nearest_target(self, *(int*)(DAT_80611d78 + 0x14), 0);
        }
        else if (-1 < state) {
            if (retarget == 0) {
                hit = anim_is_clip_active(self, 0x112);
                if (hit == 0) {
                    anim_play_scaled((double)FLOAT_806193c0, (double)FLOAT_8061939c);
                }
            }
            else {
                hit = anim_is_clip_active(self, 0x113);
                if (hit == 0) {
                    anim_play_scaled((double)FLOAT_806193c0, (double)FLOAT_8061939c);
                }
            }
            if ((retarget != 0) && (*(float*)(selfRec + 0x9e0) < *(float*)(selfRec + 0xb30))) {
                if (*(int*)(selfRec + 0x20) != 2) {
                    *(int*)(selfRec + 0x9d4) = *(int*)(selfRec + 0x20);
                    *(int*)(selfRec + 0x20) = 2;
                    *(float*)(selfRec + 0x9d8) = FLOAT_806193b8;
                    *(int*)(selfRec + 0x9d0) = *(int*)(selfRec + 0x20);
                }
                biased = rng_counter_tick();
                *(float*)(selfRec + 0x9e0) =
                    FLOAT_806193dc *
                    ((float)((double)(long long)biased - DOUBLE_806193b0) / FLOAT_806193a0) +
                    FLOAT_806193dc + *(float*)(selfRec + 0xb30);
            }
        }
    }
    else if (state < 4) {
        hit = anim_is_clip_active(self, 0x122);
        if (hit == 0) {
            anim_play_scaled((double)FLOAT_806193c0, (double)FLOAT_8061939c);
            *(int*)(selfRec + 0xa74) = *(int*)(selfRec + 0xb88);
            *(int*)(selfRec + 0xa78) = *(int*)(selfRec + 0xb8c);
            *(int*)(selfRec + 0xa7c) = *(int*)(selfRec + 0xb90);
        }
        else {
            hit = anim_active_slot_userflags(*(int*)(self + 4));
            if (hit == 0) {
                dv = anim_active_clip_phase(self);
                if ((double)FLOAT_806193e4 < dv) {
                    sound_init_play_params((int)soundParams);
                    sndFlag = 1;
                    soundParams[0] = 0x55f;
                    sndPosX = *(int*)(self + 100);
                    sndPosY = *(int*)(self + 0x68);
                    sndPosZ = *(int*)(self + 0x6c);
                    (void)sndPosX; (void)sndPosY; (void)sndPosZ; (void)sndFlag;
                    sound_play_request(soundParams);
                    mtx4_set_translate_rotz(
                        (double)*(float*)(self + 100), (double)*(float*)(self + 0x68),
                        (double)*(float*)(self + 0x6c),
                        (double)((FLOAT_806193e8 * *(float*)(self + 0x70)) / FLOAT_806193ec), mtx);
                    hit = *(int*)(*(int*)(self + 4) + 0x10) +
                          *(int*)(*(int*)(*(int*)(self + 4) + 0x84) + 0x30) * 0x40;
                    beamOrigin[0] = *(float*)(hit + 0x30);
                    beamOrigin[1] = *(float*)(hit + 0x34);
                    beamOrigin[2] = *(float*)(hit + 0x38);
                    mtx4_transform_point(mtx, beamOrigin);
                    beamDir[0] = *(float*)(selfRec + 0xa74) - beamOrigin[0];
                    beamDir[1] = *(float*)(selfRec + 0xa78) - beamOrigin[1];
                    beamDir[2] = *(float*)(selfRec + 0xa7c) - beamOrigin[2];
                    len = beamDir[2] * beamDir[2] + beamDir[0] * beamDir[0] + beamDir[1] * beamDir[1];
                    if (FLOAT_806193f0 < len) {
                        inv = 1.0f / (float)math_sqrt((double)len);
                        inv = inv * -(FLOAT_806193a8 * len * inv * inv - FLOAT_806193e0);
                        inv = FLOAT_8061939c * inv * -(FLOAT_806193a8 * len * inv * inv - FLOAT_806193e0);
                        beamDir[0] = beamDir[0] * inv;
                        beamDir[1] = beamDir[1] * inv;
                        beamDir[2] = beamDir[2] * inv;
                    }
                    beamOrigin[0] = (float)((double)FLOAT_806193a4 * (double)beamDir[0] + (double)beamOrigin[0]);
                    beamOrigin[1] = (float)((double)FLOAT_806193a4 * (double)beamDir[1] + (double)beamOrigin[1]);
                    beamOrigin[2] = (float)((double)FLOAT_806193a4 * (double)beamDir[2] + (double)beamOrigin[2]);
                    beamDir[0] = (float)((double)beamDir[0] * (double)FLOAT_806193f4);
                    beamDir[1] = (float)((double)beamDir[1] * (double)FLOAT_806193f4);
                    beamDir[2] = (float)((double)beamDir[2] * (double)FLOAT_806193f4);
                    hit = scene_raycast(*(int*)(self + 0x34), beamOrigin, beamDir, 0, hitPoint,
                                        (float*)0, (int*)0, 0x10000, 0, (int*)0);
                    beamOrigin[0] = -(FLOAT_806193f8 * beamDir[0] - beamOrigin[0]);
                    beamOrigin[1] = -(FLOAT_806193f8 * beamDir[1] - beamOrigin[1]);
                    beamOrigin[2] = -(FLOAT_806193f8 * beamDir[2] - beamOrigin[2]);
                    if ((hit == 0) || (hit == self)) {
                        beamEnd[0] = beamOrigin[0] + beamDir[0];
                        beamEnd[1] = beamOrigin[1] + beamDir[1];
                        beamEnd[2] = beamOrigin[2] + beamDir[2];
                    }
                    else {
                        scatter[0] = beamDir[0];
                        scatter[1] = beamDir[1];
                        scatter[2] = beamDir[2];
                        len = beamDir[2] * beamDir[2] + beamDir[0] * beamDir[0] + beamDir[1] * beamDir[1];
                        if (FLOAT_806193f0 < len) {
                            inv = 1.0f / (float)math_sqrt((double)len);
                            inv = inv * -(FLOAT_806193a8 * len * inv * inv - FLOAT_806193e0);
                            inv = FLOAT_8061939c * inv * -(FLOAT_806193a8 * len * inv * inv - FLOAT_806193e0);
                            scatter[0] = beamDir[0] * inv;
                            scatter[1] = beamDir[1] * inv;
                            scatter[2] = beamDir[2] * inv;
                        }
                        beamEnd[0] = hitPoint[0];
                        beamEnd[1] = hitPoint[1];
                        beamEnd[2] = hitPoint[2];
                        i = 0;
                        do {
                            biased = rng_counter_tick();
                            rnd[0] = (float)((double)FLOAT_806193bc *
                                     (double)((float)((double)(long long)biased - DOUBLE_806193b0) / FLOAT_806193a0 - FLOAT_806193a8));
                            biased = rng_counter_tick();
                            rnd[1] = (float)((double)FLOAT_806193bc *
                                     (double)((float)((double)(long long)biased - DOUBLE_806193b0) / FLOAT_806193a0 - FLOAT_806193a8));
                            biased = rng_counter_tick();
                            rnd[2] = (float)((double)FLOAT_806193bc *
                                     (double)((float)((double)(long long)biased - DOUBLE_806193b0) / FLOAT_806193a0 - FLOAT_806193a8));
                            len = rnd[2] * rnd[2] + rnd[0] * rnd[0] + rnd[1] * rnd[1];
                            if (FLOAT_806193f0 < len) {
                                inv = 1.0f / (float)math_sqrt((double)len);
                                inv = inv * -(FLOAT_806193a8 * len * inv * inv - FLOAT_806193e0);
                                inv = FLOAT_8061939c * inv * -(FLOAT_806193a8 * len * inv * inv - FLOAT_806193e0);
                                rnd[0] = rnd[0] * inv;
                                rnd[1] = rnd[1] * inv;
                                rnd[2] = rnd[2] * inv;
                            }
                            spark_spawn_fading();
                            spark_set_last_scale((double)FLOAT_80619400);
                            i = i + 1;
                        } while (i < 4);
                        switch_slot_reset((int)&dmgRec[0]);
                        dmgHitPtr = hitPoint;
                        dmgDirPtr = scatter;
                        dmgAmount = FLOAT_80619404;
                        dmgFlagsA = 0x1a;
                        dmgFlagsB = 0x1a;
                        dmgRec[0] = hit;
                        dmgRec[1] = self;
                        dmgRec[2] = self;
                        dmgRec2 = self;
                        dmgRec3 = selfRec;
                        (void)dmgFlagsA; (void)dmgFlagsB; (void)dmgAmount;
                        (void)dmgHitPtr; (void)dmgDirPtr; (void)dmgRec2; (void)dmgRec3;
                        damage_dispatch_to_entity((double)beamEnd[0], (double)beamEnd[1],
                                                  (double)beamEnd[2], (double)beamDir[0],
                                                  (unsigned long long)0, (unsigned long long)0,
                                                  (unsigned long long)0, (unsigned long long)0,
                                                  &dmgRec[0]);
                    }
                    i = 0;
                    do {
                        spark_spawn_fading();
                        spark_set_last_scale((double)FLOAT_80619400);
                        i = i + 1;
                    } while (i < 10);
                }
            }
            else if (*(int*)(selfRec + 0x20) != 0) {
                *(int*)(selfRec + 0x9d4) = *(int*)(selfRec + 0x20);
                *(int*)(selfRec + 0x20) = 0;
                *(float*)(selfRec + 0x9d8) = FLOAT_806193b8;
                *(int*)(selfRec + 0x9d0) = *(int*)(selfRec + 0x20);
            }
        }
    }
    if (((*(int*)(selfRec + 4) != 0) &&
         (self = *(int*)(*(int*)(selfRec + 4) + 0x7c), self != 0)) &&
        ((*(unsigned int*)(self + 0x24) & 8) != 0)) {
        if (*(int*)(selfRec + 0xaac) == 1 && *(int*)(selfRec + 0xaa8) == 0) {
            unsigned int fl;
            if (DAT_805015bd == '\n') {
                fl = *(unsigned int*)(selfRec + 0x14);
            }
            else {
                fl = *(unsigned int*)(selfRec + 0x1c);
            }
            if ((fl & 1) == 0) {
                goto done2;
            }
        }
        *(int*)(selfRec + 0xaac) = 0x10;
        *(int*)(selfRec + 0xaa8) = 0;
    }
done2:
    FUN_80382734();
    return;
}

// ---------------------------------------------------------------------------
// Address: 0x80286a04 | Name: tracker_creature_boss_tick
// Per-frame boss-creature behaviour: a 5-state FSM (+0x20: 0 spawn/enrage,
// 1 chase, 2 attack, 3 retreat, 4 recover). Applies contact damage while its
// slam/stomp clips (0x127/0x12e) are active, drives per-state clips
// (0x127-0x12e, 0x12c), allocates a screen effect on spawn, and updates the
// floor-probe offset table (DAT_8048ca9c/DAT_8048caa0) for its footprint.
void tracker_creature_boss_tick(unsigned long long a, double b, double c,
                                unsigned long long d, unsigned long long e,
                                unsigned long long f, unsigned long long g,
                                unsigned long long h, int actor)
{
    float rate;
    int self;
    int state;
    unsigned int flags;
    unsigned int biased;
    int dmgRec[3];   // local_78/74/70
    unsigned int dmgFlagsA;
    unsigned int dmgFlagsB;
    float dmgScale;  // local_58

    self = *(int*)(actor + 0x7c);
    aiactor_retarget_visible_teammate();
    if (-1 < *(int*)(self + 0x9d0)) {
        if (*(int*)(self + 0x9d0) != *(int*)(self + 0x20)) {
            *(float*)(self + 0x9d8) = FLOAT_806193b8;
            *(int*)(self + 0x9d4) = *(int*)(self + 0x20);
            *(int*)(self + 0x20) = *(int*)(self + 0x9d0);
        }
        *(int*)(self + 0x9d0) = -1;
    }
    if ((*(unsigned int*)(self + 0x2c) & 0x80) == 0) {
        *(float*)(self + 0xa90) = FLOAT_806193b8;
    }
    else {
        biased = (unsigned int)DAT_806109c0 ^ 0x80000000;
        *(float*)(self + 0xa90) =
            (float)((double)FLOAT_806108ec *
                    (double)(float)((double)FLOAT_8061939c /
                                    (double)(float)((double)(long long)((unsigned long long)biased | 0x4330000000000000ULL) - DOUBLE_806193d0)) +
                    (double)*(float*)(self + 0xa90));
    }
    *(int*)(self + 0x30) = *(int*)(actor + 100);
    *(int*)(self + 0x34) = *(int*)(actor + 0x68);
    *(int*)(self + 0x38) = *(int*)(actor + 0x6c);
    *(int*)(self + 4) = aiactor_acquire_nearest_target(actor, *(int*)(DAT_80611d78 + 0x14), 0);
    state = anim_is_clip_active(actor, 0x127);
    if ((((state != 0) || (state = anim_is_clip_active(actor, 0x12e), state != 0)) &&
         (*(int*)(self + 0xc1c) != 0)) && (*(int*)(self + 0xc20) < DAT_80612610)) {
        switch_slot_reset((int)&dmgRec[0]);
        dmgRec[0] = *(int*)(DAT_80611d90 + *(int*)(self + 0xc20) * 0xea0);
        dmgScale = FLOAT_806193a8;
        dmgFlagsA = 0;
        dmgFlagsB = 0;
        dmgRec[1] = actor;
        dmgRec[2] = actor;
        (void)dmgScale; (void)dmgFlagsA; (void)dmgFlagsB;
        damage_dispatch_to_entity((double)(long long)a, b, c, (double)(long long)d,
                                  e, f, g, h, &dmgRec[0]);
    }
    rate = FLOAT_80619408;
    state = *(int*)(self + 0x20);
    if (state == 2) {
        aiactor_turn_toward_leader();
        state = anim_primary_clip_userflags(actor, 0x129);
        if (((state != 0) || (state = anim_primary_clip_userflags(actor, 0x128), state != 0)) ||
            (state = anim_primary_clip_userflags(actor, 0x127), state != 0)) {
            biased = rng_counter_tick();
            if (FLOAT_806193a8 <
                (float)((double)(long long)biased - DOUBLE_806193b0) / FLOAT_806193a0) {
                biased = rng_counter_tick();
                *(float*)(self + 0x9e0) =
                    FLOAT_80619418 *
                    ((float)((double)(long long)biased - DOUBLE_806193b0) / FLOAT_806193a0) +
                    FLOAT_80619418 + *(float*)(self + 0xb30);
                if (*(int*)(self + 0x20) != 1) {
                    *(int*)(self + 0x9d4) = *(int*)(self + 0x20);
                    *(int*)(self + 0x20) = 1;
                    *(float*)(self + 0x9d8) = FLOAT_806193b8;
                    *(int*)(self + 0x9d0) = *(int*)(self + 0x20);
                }
                goto tail;
            }
        }
        state = anim_active_slot_userflags(*(int*)(actor + 4));
        if (state != 0) {
            if (*(int*)(self + 0xc1c) != 0) {
                biased = rng_counter_tick();
                *(float*)(self + 0x9e0) =
                    FLOAT_8061941c *
                    ((float)((double)(long long)biased - DOUBLE_806193b0) / FLOAT_806193a0) +
                    FLOAT_8061941c + *(float*)(self + 0xb30);
                if (*(int*)(self + 0x20) != 3) {
                    *(int*)(self + 0x9d4) = *(int*)(self + 0x20);
                    *(int*)(self + 0x20) = 3;
                    *(float*)(self + 0x9d8) = FLOAT_806193b8;
                    *(int*)(self + 0x9d0) = *(int*)(self + 0x20);
                }
                goto tail;
            }
            biased = rng_counter_tick();
            rate = (float)((double)(long long)biased - DOUBLE_806193b0) / FLOAT_806193a0;
            if (FLOAT_806193c0 <= rate) {
                if (FLOAT_806193c4 <= rate) {
                    anim_play_scaled((double)FLOAT_80619398, (double)FLOAT_80619420);
                }
                else {
                    anim_play_scaled((double)FLOAT_80619398, (double)FLOAT_80619420);
                }
            }
            else {
                anim_play_scaled((double)FLOAT_80619398, (double)FLOAT_80619420);
            }
        }
        state = anim_is_clip_active(actor, 0x129);
        if (state == 0) {
            state = anim_is_clip_active(actor, 0x128);
            if (state != 0) {
                aiactor_melee_swipe(actor, 0, 0);
            }
        }
        else {
            aiactor_melee_swipe(actor, 1, 0);
        }
        goto tail;
    }
    if (1 < state) {
        if (state == 4) {
            state = anim_is_clip_active(actor, 0x12e);
            if (state == 0) {
                anim_play_scaled((double)FLOAT_806193c0, (double)FLOAT_80619408);
            }
            if (*(int*)(self + 0xc1c) == 0) {
                if (((*(unsigned int*)(self + 0x2c) & 0x40) != 0) ||
                    (*(float*)(self + 0x9e0) < *(float*)(self + 0xb30))) {
                    biased = rng_counter_tick();
                    *(float*)(self + 0x9e0) =
                        FLOAT_80619418 *
                        ((float)((double)(long long)biased - DOUBLE_806193b0) / FLOAT_806193a0) +
                        FLOAT_80619418 + *(float*)(self + 0xb30);
                    if (*(int*)(self + 0x20) != 1) {
                        *(int*)(self + 0x9d4) = *(int*)(self + 0x20);
                        *(int*)(self + 0x20) = 1;
                        *(float*)(self + 0x9d8) = FLOAT_806193b8;
                        *(int*)(self + 0x9d0) = *(int*)(self + 0x20);
                    }
                }
            }
            else {
                biased = rng_counter_tick();
                *(float*)(self + 0x9e0) =
                    FLOAT_8061941c *
                    ((float)((double)(long long)biased - DOUBLE_806193b0) / FLOAT_806193a0) +
                    FLOAT_8061941c + *(float*)(self + 0xb30);
                if (*(int*)(self + 0x20) != 3) {
                    *(int*)(self + 0x9d4) = *(int*)(self + 0x20);
                    *(int*)(self + 0x20) = 3;
                    *(float*)(self + 0x9d8) = FLOAT_806193b8;
                    *(int*)(self + 0x9d0) = *(int*)(self + 0x20);
                }
            }
        }
        else if (state < 4) {
            aiactor_turn_toward_leader();
            state = anim_is_clip_active(actor, 0x12d);
            if (state == 0) {
                anim_play_scaled((double)FLOAT_806193c0, (double)FLOAT_80619424);
            }
            else {
                state = anim_active_slot_userflags(*(int*)(actor + 4));
                if (state != 0) {
                    biased = rng_counter_tick();
                    *(float*)(self + 0x9e0) =
                        FLOAT_8061941c *
                        ((float)((double)(long long)biased - DOUBLE_806193b0) / FLOAT_806193a0) +
                        FLOAT_8061941c + *(float*)(self + 0xb30);
                    if (*(int*)(self + 0x20) != 4) {
                        *(int*)(self + 0x9d4) = *(int*)(self + 0x20);
                        *(int*)(self + 0x20) = 4;
                        *(float*)(self + 0x9d8) = FLOAT_806193b8;
                        *(int*)(self + 0x9d0) = *(int*)(self + 0x20);
                    }
                }
            }
        }
        goto tail;
    }
    if (state == 0) {
        *(float*)(&DAT_8048ca9c + *(short*)(self + 0x48) * 0x30) =
            FLOAT_80619408 * FLOAT_806193a8 * *(float*)(self + 0xb08);
        *(float*)(&DAT_8048caa0 + *(short*)(self + 0x48) * 0x30) = rate * *(float*)(self + 0xb08);
        if (*(int*)(self + 0x20) != 1) {
            *(int*)(self + 0x9d4) = *(int*)(self + 0x20);
            *(int*)(self + 0x20) = 1;
            *(float*)(self + 0x9d8) = FLOAT_806193b8;
            *(int*)(self + 0x9d0) = *(int*)(self + 0x20);
        }
        entity_set_render_type(actor, damage_filter_scaled_melee_immune);
        biased = rng_counter_tick();
        *(float*)(self + 0x9e0) =
            FLOAT_8061940c *
            ((float)((double)(long long)biased - DOUBLE_806193b0) / FLOAT_806193a0) +
            FLOAT_8061940c + *(float*)(self + 0xb30);
        aiactor_turn_toward_leader();
        *(int*)(actor + 0x268) = screen_effect_alloc_slot();
        screen_effect_init_slot(*(int*)(actor + 0x268), actor);
        *(int*)(DAT_80611d74 + 0xe8c) = actor;
        goto tail;
    }
    if (state < 0) {
        goto tail;
    }
    aiactor_turn_toward_leader();
    state = anim_is_clip_active(actor, 300);
    if (state == 0) {
        anim_play_scaled((double)FLOAT_806193c0, (double)FLOAT_80619410);
    }
    if (*(float*)(self + 0xaf4) < FLOAT_80619414) {
        if (*(int*)(self + 0x20) != 2) {
            *(int*)(self + 0x9d4) = *(int*)(self + 0x20);
            *(int*)(self + 0x20) = 2;
            *(float*)(self + 0x9d8) = FLOAT_806193b8;
            *(int*)(self + 0x9d0) = *(int*)(self + 0x20);
        }
        biased = rng_counter_tick();
        *(float*)(self + 0x9e0) =
            FLOAT_806193a8 *
            ((float)((double)(long long)biased - DOUBLE_806193b0) / FLOAT_806193a0) +
            FLOAT_806193a8 + *(float*)(self + 0xb30);
        goto tail;
    }
    state = anim_active_slot_userflags(*(int*)(actor + 4));
    if (state == 0) {
        if (*(float*)(self + 0xb30) <= *(float*)(self + 0x9e0)) {
            goto tail;
        }
    }
    else {
        biased = rng_counter_tick();
        if (FLOAT_80619418 <=
            (float)((double)(long long)biased - DOUBLE_806193b0) / FLOAT_806193a0) {
            if (*(float*)(self + 0xb30) <= *(float*)(self + 0x9e0)) {
                goto tail;
            }
        }
    }
    biased = rng_counter_tick();
    if ((float)((double)(long long)biased - DOUBLE_806193b0) / FLOAT_806193a0 <= FLOAT_806193a8) {
        if (*(int*)(self + 0x20) != 2) {
            *(int*)(self + 0x9d4) = *(int*)(self + 0x20);
            *(int*)(self + 0x20) = 2;
            *(float*)(self + 0x9d8) = FLOAT_806193b8;
            *(int*)(self + 0x9d0) = *(int*)(self + 0x20);
        }
    }
    else if (*(int*)(self + 0x20) != 3) {
        *(int*)(self + 0x9d4) = *(int*)(self + 0x20);
        *(int*)(self + 0x20) = 3;
        *(float*)(self + 0x9d8) = FLOAT_806193b8;
        *(int*)(self + 0x9d0) = *(int*)(self + 0x20);
    }
tail:
    if (((*(int*)(self + 4) != 0) && (state = *(int*)(*(int*)(self + 4) + 0x7c), state != 0)) &&
        ((*(unsigned int*)(state + 0x24) & 8) != 0)) {
        if (*(int*)(self + 0xaac) == 1 && *(int*)(self + 0xaa8) == 0) {
            if (DAT_805015bd == '\n') {
                flags = *(unsigned int*)(self + 0x14);
            }
            else {
                flags = *(unsigned int*)(self + 0x1c);
            }
            if ((flags & 1) == 0) {
                return;
            }
        }
        *(int*)(self + 0xaac) = 0x10;
        *(int*)(self + 0xaa8) = 0;
    }
    return;
}

// ---------------------------------------------------------------------------
// Address: 0x80287298 | Name: tracker_event_react_or_pass
// Script-event handler for event 1 (alerted): if the actor has the "can react"
// flag (+0x10 bit7), schedule a randomized react cooldown (+0x9e0), force the
// react substate (+0x9d0 = 4), and play a reaction clip (gated by the class
// flags in DAT_804d34dc); always returns -1 for event 1, 0 otherwise.
int tracker_event_react_or_pass(int actor, int event)
{
    int result;
    int self;
    unsigned int rng;

    result = 0;
    if (event == 1) {
        self = *(int*)(actor + 0x7c);
        if ((*(unsigned int*)(self + 0x10) & 0x80) == 0) {
            result = -1;
        }
        else {
            if (*(int*)(self + 0x20) != 4) {
                rng = rng_counter_tick();
                *(float*)(self + 0x9e0) =
                    FLOAT_8061941c *
                    ((float)((double)(long long)rng - DOUBLE_806193b0) / FLOAT_806193a0) +
                    FLOAT_8061941c + *(float*)(self + 0xb30);
                if (*(int*)(self + 0x20) != 4) {
                    *(int*)(self + 0x9d0) = 4;
                }
            }
            if (((&DAT_804d34dc)[*(int*)(*(int*)(actor + 4) + 0xd0) * 2] & 8) == 0) {
                anim_play_scaled((double)FLOAT_806193b8, (double)FLOAT_80619420);
            }
            result = -1;
        }
    }
    return result;
}

