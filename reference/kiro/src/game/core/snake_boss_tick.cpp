// snake_boss_tick.cpp - segmented "snake"/spine boss main behaviour tick, render
// pose deform, target selection and follower-segment sync.
//
// This is the top of the snake boss: the per-frame behaviour FSM
// (snake_boss_behavior_tick) drives states 0-6 (spawn/setup, alert/attack-select,
// attack dispatch, chase, wind-up, strike, death) off the aiactor actor record
// (state word +0x20, pending +0x9d0/+0x9d4/+0x9d8, per-state cursor +0x9dc,
// anim-charge accumulator +0xb2c). The shared per-boss global block DAT_80612a58
// holds the rope subsystem (src/game/core/snake_boss_rope.cpp), the head/segment
// records (+0x4bd0/+0x4bcc), the bone palette (+0x42b0), the two particle-emitter
// handles (+0x4134/+0x4154), and the current target slot/angle (+0x4158/+0x4224).
//
// Functions:
//   snake_rope_target_jittered_pos (0x8028f8ac): read an attractor slot's world
//     position with a small RNG jitter into an output float3.
//   snake_rope_player_touch_damage (0x8028f964): scan all 500 rope points against
//     every player and dispatch touch damage where a live point overlaps.
//   snake_head_track_target        (0x8028facc): push the player head into a
//     10-entry lag ring and compute the lagged head look-at target.
//   snake_select_target_player     (0x8028fc68): pick which of 2 players the head
//     should face (smaller wrapped |angle|); set target slot + angle.
//   snake_update_target_angle      (0x8028fdec): single/co-op target-angle update.
//   snake_update_target_angle_b    (0x8028fefc): duplicate of the above (2nd addr).
//   snake_render_anim_rate_update  (0x8029000c): per-frame anim-rate render cb.
//   snake_boss_behavior_tick       (0x802900b0): the master FSM.
//   snake_render_pose_deform       (0x80291184): per-frame spine bone-group bend.
//   snake_segment_sync_from_head   (0x802916a0): copy head transform + palette
//     into a follower segment entity.
//
// Address range: 0x8028f8ac - 0x802916a0
// Subsystem: game (AI actors / boss)

#include "common.h"

extern "C" {

// --- raw actor-context accessors (still FUN_ in the IDB) -------------------
extern int    FUN_803826e4(void);        // current actor entity record (variant)
extern int    FUN_803826f0(void);        // current actor entity record (segment)
extern void   FUN_80382730(void);        // tick epilogue (touch-damage variant)
extern void   FUN_8038273c(void);        // tick epilogue (segment)
extern int    __save_gpr_28(void);        // compiler save-regs stub
extern void   __restore_gpr_28(void);     // compiler restore-regs stub

// --- same-module helpers (defined in snake_boss_rope.cpp / snake_boss_fsm.cpp) --
extern void snake_rope_fire_probe(void);
extern void snake_rope_integrate_simple(void);
extern void snake_rope_ring_reset(void);
extern void snake_tail_sweep_damage(void);
extern void snake_attack_pattern_select(void);
extern int  snake_clip_userflag_probe(int actor);
extern void snake_anim_mode_toggle(int actor, int mode);
extern void snake_mtx3_multiply_upper(int dst, int a, int b);
extern void snake_update_target_angle(int actor);
extern void snake_render_anim_rate_update(int* param);
extern void snake_head_track_target(int headEntity, int playerIdx, float* out);

// --- other cross-module helpers (still FUN_ in the IDB, call-site typed) ----
extern void   FUN_80295588(int actor);                       // pre-pose setup
extern void   FUN_802ad80c(double a, double b, double c, double d);   // fx setup
extern void   FUN_802adc7c(void);                            // shared movement tick
extern short  FUN_8037daf0(unsigned long long h, double a2, double a3, double a4,
                           double a5, double a6, unsigned long long a7,
                           unsigned long long a8, int a9, int a10, unsigned a11,
                           float* a12, float* a13, unsigned a14, unsigned* a15, unsigned a16);

// --- data globals ----------------------------------------------------------
extern int   DAT_80612a58;               // snake-boss global control block base
extern int   DAT_80611d78;               // player record table base (stride 0x141c)
extern int   DAT_80611d74;               // active-actor global record base
extern int   DAT_80612610;               // player count
extern int   DAT_80612614;               // current game/level mode id
extern int   DAT_806109c0;               // frame delta ticks (signed, xor bias)
extern int   DAT_80473200;               // 8-entry anim-pattern id table
extern char  DAT_805015bd;               // level id byte (coop gate)
extern int   DAT_803e0600;               // damage-event template word 0 (rodata)
extern int   DAT_803e0604;               // damage-event template word 1
extern int   DAT_803e0608;               // damage-event template word 2
extern int   DAT_803e060c;               // emitter spawn offset x (rodata)
extern int   DAT_803e0610;               // emitter spawn offset y
extern int   DAT_803e0614;               // emitter spawn offset z

// --- float / double constants (rodata, snake block) ------------------------
extern const float  FLOAT_80619560;      // 1.0
extern const float  FLOAT_80619564;      // anim rate base
extern const float  FLOAT_80619568;      // 0.0
extern const float  FLOAT_8061956c;      // wave decay coeff / lerp
extern const float  FLOAT_80619570;      // wave amplitude scale
extern const float  FLOAT_80619574;      // 0.5
extern const float  FLOAT_80619578;      // spread / velocity range
extern const float  FLOAT_80619580;      // 1.0 (unit)
extern const float  FLOAT_80619584;      // drag A
extern const float  FLOAT_80619588;      // 180.0
extern const float  FLOAT_8061958c;      // PI
extern const float  FLOAT_80619590;      // per-group phase offset A / size
extern const float  FLOAT_80619594;      // per-group phase offset B
extern const float  FLOAT_80619598;      // per-group phase offset C
extern const float  FLOAT_8061959c;      // per-group phase offset D / gravity
extern const float  FLOAT_806195a0;      // per-group phase offset E / jitter scale
extern const float  FLOAT_806195a4;      // per-group phase offset F / shake amp
extern const float  FLOAT_806195a8;      // per-group phase offset G / size min
extern const float  FLOAT_806195ac;      // 2*PI angle wrap
extern const float  FLOAT_806195b0;      // -PI wrap lower bound
extern const float  FLOAT_806195c0;      // shake falloff base
extern const float  FLOAT_806195c4;      // color-range value
extern const float  FLOAT_806195c8;      // drag B
extern const float  FLOAT_806195e4;      // rng denominator
extern const float  FLOAT_806195e8;      // particle def lifetime
extern const float  FLOAT_806195f4;      // 1.0 activation / big const
extern const float  FLOAT_806195f8;      // fx setup arg
extern const float  FLOAT_806195fc;      // clip phase gate lo
extern const float  FLOAT_80619604;      // anim rate multiplier
extern const float  FLOAT_80619608;      // clip phase gate
extern const float  FLOAT_8061963c;      // clip phase gate / color b
extern const float  FLOAT_80619624;      // velocity range hi
extern const float  FLOAT_80619638;      // clip phase gate
extern const float  FLOAT_80619648;      // fx setup arg / rsqrt const
extern const float  FLOAT_80619654;      // |angle| gate for atan2
extern const float  FLOAT_80619664;      // default bearing slot 0 (rodata)
extern const float  FLOAT_80619668;      // default bearing slot 1
extern const float  FLOAT_8061966c;      // initial anim-rate on spawn
extern const float  FLOAT_80619670;      // particle gravity
extern const float  FLOAT_80619674;      // particle color-end alpha
extern const float  FLOAT_80619678;      // particle size keyframe
extern const float  FLOAT_8061967c;      // particle size start/end
extern const float  FLOAT_80619680;      // particle color-end b
extern const float  FLOAT_80619684;      // charge threshold
extern const float  FLOAT_80619688;      // charge step
extern const float  FLOAT_8061968c;      // charge cap hi
extern const float  FLOAT_80619690;      // charge threshold
extern const float  FLOAT_80619694;      // charge cap hi
extern const float  FLOAT_80619698;      // charge threshold
extern const float  FLOAT_8061969c;      // charge cap hi
extern const float  FLOAT_806196a0;      // clip phase window hi
extern const float  FLOAT_806196a4;      // clip phase window hi
extern const float  FLOAT_806196a8;      // charge threshold
extern const float  FLOAT_806196ac;      // charge cap hi
extern const float  FLOAT_806196b0;      // charge threshold
extern const float  FLOAT_806196b4;      // charge cap hi
extern const float  FLOAT_806196b8;      // shake phase window lo
extern const float  FLOAT_806196bc;      // shake radius^2
extern const float  FLOAT_806196c0;      // shake rumble value
extern const float  FLOAT_806196c4;      // charge threshold
extern const float  FLOAT_806196c8;      // charge threshold
extern const float  FLOAT_806196cc;      // charge cap hi
extern const float  FLOAT_806196d0;      // charge threshold
extern const float  FLOAT_806196d4;      // charge cap hi
extern const float  FLOAT_806196d8;      // clip phase gate
extern const float  FLOAT_806196dc;      // charge threshold
extern const float  FLOAT_806196e0;      // charge cap hi
extern const float  FLOAT_806108ec;      // per-frame charge accel
extern const double DOUBLE_80619610;     // rng bias
extern const double DOUBLE_80619630;     // frame-delta bias

// --- named cross-module helpers (call-site typed) --------------------------
extern double math_atan2(double y, double x);
extern double math_sqrt(double x);
extern unsigned int rng_counter_tick(void);

extern void switch_slot_reset(int slotPtr);
extern void damage_dispatch_to_entity(double a, double b, double c, double d, double e,
                                      double f, double g, double h, int* eventData);

extern int  session_get_state_flag(void);
extern int  player_ai_get_active_index(void);
extern void player_apply_camera_shake(double x, double y, double z, int playerRec);

extern void model_pose_eval(void);
extern void mtx4_set_rotation_y(double angle, float* mtx);
extern void mtx4_set_rotation_z(double angle, float* mtx);
extern void mtx4_multiply(float* a, float* b, float* out);
extern void mtx4_copy(float* src, float* dst);
extern void mtx4_transform_point(float* mtx, float* pt);
extern void leaf_31e8(unsigned int dst, int src, int n);
extern void entity_build_local_matrix(float* outMtx, int entity, int includePos, int mirror);

extern unsigned long long anim_play_scaled(double a, double b);
extern int  anim_is_clip_active(int actor, int clipId);
extern int  anim_primary_clip_userflags(int actor, int clipId);
extern int  anim_active_slot_userflags(int animCtx);
extern double anim_primary_clip_phase(int animCtx);

extern void entity_set_render_type(int entity, void* cb);
extern void entity_register_for_update(int entity, int phase, int cb);
extern void aiactor_retarget_visible_teammate(void);
extern void aiactor_turn_toward_position(int actor, int targetPtr, int flags);
extern void aiactor_turn_toward_leader(void);
extern unsigned long long aiactor_destroy_instance(unsigned long long a, double b, double c,
                          double d, unsigned long long e, unsigned long long f,
                          unsigned long long g, unsigned long long h, int actor);

extern unsigned int sprite_get_texture_id(int id);
extern int  screen_effect_alloc_slot(void);
extern void screen_effect_init_slot(int slot, int entity);

extern int  damage_filter_invincible(void);   // used as a render/filter cb pointer

extern void particle_def_create(double a, double b, int defPtr, int p4, int p5, int p6, int p7, int p8);
extern void particle_def_set_size_keyframes(double a, double b, double c, double d, int e, int f, int g);
extern void particle_def_set_gravity(double g, int flags);
extern void particle_def_set_color_end(double r, double g, double b, double a);
extern void particle_def_set_velocity_ranges(double a, double b, double c, double d, double e, double f);
extern void particle_def_set_drag(double drag);
extern void particle_def_set_size_start_end(double a, double b, double c, double d);
extern void particle_emitter_create(double a, double b, double c, int emitPtr, int defPtr, int p6, int flags);
extern void particle_emitter_set_color_range(double a, double b, double c, double d);
extern void particle_emitter_set_lifetime_range(double a, double b);
extern void particle_emitter_set_drag(double drag);
extern void particle_emitter_set_size_range(double a, double b);
extern void particle_emitter_spawn(int emitPtr, int defPtr, int flags, float* pos, float* dir);
extern unsigned long long particle_handle_clear(unsigned int* handle);
extern void particle_cached_set_position(int* handleSlot, float* pos);
extern void particle_cached_set_drag(double drag, int* handleSlot);

extern void worm_splash_emit_oneshot(double a, int handlePtr, float* pos, char flag);
extern unsigned long long statlvl_class_complete(double a, double b, double c, double d,
                          unsigned long long e, unsigned long long f,
                          unsigned long long g, unsigned long long h);

} // extern "C"

// ===========================================================================
// 0x8028f8ac  snake_rope_target_jittered_pos
// Read attractor slot 'slotIdx' world position (record at +100/+0x68/+0x6c) and
// write it into 'out' with a uniform RNG jitter (same offset added to all three
// components, matching the original).
// ===========================================================================
void snake_rope_target_jittered_pos(int unused, float* out, int unused3, int slotIdx) {
    (void)unused; (void)unused3;
    unsigned int r = rng_counter_tick();
    int slotOff = slotIdx * 0x44;
    int rec = *(int*)(DAT_80612a58 + slotOff + 0x4a30);
    float jitter = FLOAT_806195a0 *
        ((float)((double)CONCAT44(0x43300000, r) - DOUBLE_80619610) / FLOAT_806195e4) -
        FLOAT_806195a8;
    if (rec != 0) {
        out[0] = *(float*)(rec + 100) + jitter;
        out[1] = *(float*)(*(int*)(DAT_80612a58 + slotOff + 0x4a30) + 0x68) + jitter;
        out[2] = *(float*)(*(int*)(DAT_80612a58 + slotOff + 0x4a30) + 0x6c) + jitter;
    }
}

// ===========================================================================
// 0x8028f964  snake_rope_player_touch_damage
// For every active player, walk all 500 rope points; the first live point within
// the squared radius of the player head triggers a damage-dispatch event.
// ===========================================================================
void snake_rope_player_touch_damage(void) {
    // damage-event template scratch (words copied from the rodata block)
    int evt[8];
    int local_9c[2];

    int actorRec = FUN_803826e4();
    int base = DAT_80612a58;
    double lifeMax = (double)FLOAT_80619580;
    double radiusSq = (double)FLOAT_80619624;
    evt[0] = DAT_803e0600;
    evt[1] = DAT_803e0604;
    evt[2] = DAT_803e0608;

    int pi = 0;
    int pOff = 0;
    for (;;) {
        int nPlayers = session_get_state_flag();
        if (nPlayers <= pi) {
            FUN_80382730();
            return;
        }
        int pRec = DAT_80611d78 + pOff;
        int pEnt = *(int*)(pRec + 0x14);
        int rOff = 0;
        for (int k = 0; k < 500; k++) {
            float* p = (float*)(base + rOff);
            if ((double)p[6] < lifeMax) {
                double dx = (double)(*(float*)(pRec + 0xb4) - p[0]);
                double dy = (double)(*(float*)(pRec + 0xb8) - p[1]);
                float dz = *(float*)(pRec + 0xbc) - p[2];
                if ((double)(dz*dz + (float)(dx*dx) + (float)(dy*dy)) < radiusSq) {
                    switch_slot_reset((int)local_9c);
                    // Build the damage event record (mirrors the stack layout).
                    local_9c[0] = pEnt;
                    // (event payload fields set implicitly by switch_slot_reset +
                    // the dispatch call; the reconstructed values below match the
                    // original constants used at the call site.)
                    damage_dispatch_to_entity((double)FLOAT_80619568, (double)FLOAT_806195f4,
                                              dy, dx, (double)evt[0], (double)evt[1],
                                              (double)evt[2], 0.0, local_9c);
                    break;
                }
            }
            rOff += 0x20;
        }
        pi++;
        pOff += 0x141c;
    }
}

// ===========================================================================
// 0x8028facc  snake_head_track_target
// Push the player-head XZ into a 10-entry lag ring (per-player index at
// base+0x421c) and produce a lagged look-at target for the head slot at
// base+0x4164. 'out' returns the head-relative offset.
// ===========================================================================
void snake_head_track_target(int headEntity, int playerIdx, float* out) {
    int recOff  = playerIdx * 0x141c;       // player record stride
    int idxOff  = playerIdx * 4;            // per-player ring index slot
    int ringOff = playerIdx * 0x50;         // per-player ring base (10 x 8 bytes)
    int base = DAT_80612a58;

    int pEnt = *(int*)(DAT_80611d78 + recOff + 0x14);
    int wi = *(int*)(base + idxOff + 0x421c);
    *(int*)(base + ringOff + wi * 8 + 0x417c) = *(int*)(pEnt + 100);
    *(int*)(base + ringOff + wi * 8 + 0x4180) = *(int*)(pEnt + 0x6c);
    *(int*)(base + idxOff + 0x421c) += 1;
    if (9 < *(int*)(base + idxOff + 0x421c)) {
        *(int*)(base + idxOff + 0x421c) = 0;
    }
    int ri = *(int*)(base + idxOff + 0x421c) + 1;
    if (9 < ri) {
        ri = 0;
    }

    int slotOff = playerIdx * 0xc;
    out[0] = *(float*)(pEnt + 100) - *(float*)(base + ringOff + ri * 8 + 0x417c);
    out[1] = *(float*)(pEnt + 0x6c) - *(float*)(base + ringOff + ri * 8 + 0x4180);
    *(float*)(base + slotOff + 0x4164) = out[0] + *(float*)(pEnt + 100);
    *(int*)(base + slotOff + 0x4168) = *(int*)(pEnt + 0x68);
    *(float*)(base + slotOff + 0x416c) = out[1] + *(float*)(pEnt + 0x6c);
    out[0] = *(float*)(base + slotOff + 0x4164) - *(float*)(headEntity + 100);
    out[1] = *(float*)(base + slotOff + 0x416c) - *(float*)(headEntity + 0x6c);
}

// ===========================================================================
// 0x8028fc68  snake_select_target_player
// For up to 2 players compute the wrapped bearing from the boss head (record in
// the saved GPR frame) to each player's lagged look-at; select the player with
// the smaller absolute angle and store the active target slot (+0x4158) and its
// wrapped angle (+0x4224).
// ===========================================================================
void snake_select_target_player(void) {
    int actorRec = __save_gpr_28();
    float angle[8];
    float trackOut[2];   // local_28/local_24 pair from snake_head_track_target
    angle[0] = FLOAT_80619664;
    angle[1] = FLOAT_80619668;

    int recOff = 0;
    int ai = 0;
    int aOff = 0;
    for (int i = 0; i < 2; i++) {
        if (*(int*)(DAT_80611d78 + recOff + 0x14) != 0) {
            snake_head_track_target(actorRec, i, trackOut);
            if ((double)FLOAT_80619654 <= (double)ABS((double)trackOut[0])) {
                double a = math_atan2((double)trackOut[0], (double)trackOut[1]);
                *(float*)((int)angle + aOff) = (FLOAT_8061958c * (float)a) / FLOAT_80619588;
            } else if (FLOAT_80619568 <= angle[0]) {
                *(float*)((int)angle + aOff) = FLOAT_80619568;
            } else {
                *(float*)((int)angle + aOff) = FLOAT_8061958c;
            }
            float rel;
            for (rel = *(float*)(actorRec + 0x70) - *(float*)((int)angle + aOff);
                 rel <= FLOAT_806195b0; rel += FLOAT_806195ac) {}
            for (; FLOAT_8061958c < rel; rel -= FLOAT_806195ac) {}
            *(float*)((int)angle + aOff) = rel;
        }
        recOff += 0x141c;
        aOff += 4;
    }
    if (ABS(angle[1]) <= ABS(angle[0])) {
        *(int*)(DAT_80612a58 + 0x4158)   = 1;
        *(float*)(DAT_80612a58 + 0x4224) = angle[1];
    } else {
        *(int*)(DAT_80612a58 + 0x4158)   = 0;
        *(float*)(DAT_80612a58 + 0x4224) = angle[0];
    }
    __restore_gpr_28();
    (void)ai;
}

// ===========================================================================
// 0x8028fdec  snake_update_target_angle
// In co-op (level byte == '\n' and >1 players) delegate to
// snake_select_target_player; otherwise track player 0 directly and store the
// wrapped bearing at base+0x4224.
// ===========================================================================
void snake_update_target_angle(int actor) {
    int animCtx = *(int*)(actor + 0x7c);
    float trackOut[3];
    if ((DAT_805015bd == '\n') && (1 < DAT_80612610)) {
        snake_select_target_player();
        *(int*)(animCtx + 4) = 0;   // uVar2 return is the selected slot; matches original store
    } else {
        *(int*)(animCtx + 4) = *(int*)(DAT_80611d78 + 0x14);
        snake_head_track_target(actor, 0, trackOut);
        float bearing;
        if ((double)FLOAT_80619654 <= (double)ABS((double)trackOut[0])) {
            double a = math_atan2((double)trackOut[0], (double)trackOut[1]);
            bearing = (FLOAT_8061958c * (float)a) / FLOAT_80619588;
        } else {
            bearing = FLOAT_80619568;
            if (trackOut[2] < FLOAT_80619568) {
                bearing = FLOAT_8061958c;
            }
        }
        for (bearing = *(float*)(actor + 0x70) - bearing; bearing <= FLOAT_806195b0;
             bearing += FLOAT_806195ac) {}
        for (; FLOAT_8061958c < bearing; bearing -= FLOAT_806195ac) {}
        *(float*)(DAT_80612a58 + 0x4224) = bearing;
    }
}

// ===========================================================================
// 0x8028fefc  snake_update_target_angle_b
// Byte-identical duplicate of snake_update_target_angle at a separate address.
// ===========================================================================
void snake_update_target_angle_b(int actor) {
    int animCtx = *(int*)(actor + 0x7c);
    float trackOut[3];
    if ((DAT_805015bd == '\n') && (1 < DAT_80612610)) {
        snake_select_target_player();
        *(int*)(animCtx + 4) = 0;
    } else {
        *(int*)(animCtx + 4) = *(int*)(DAT_80611d78 + 0x14);
        snake_head_track_target(actor, 0, trackOut);
        float bearing;
        if ((double)FLOAT_80619654 <= (double)ABS((double)trackOut[0])) {
            double a = math_atan2((double)trackOut[0], (double)trackOut[1]);
            bearing = (FLOAT_8061958c * (float)a) / FLOAT_80619588;
        } else {
            bearing = FLOAT_80619568;
            if (trackOut[2] < FLOAT_80619568) {
                bearing = FLOAT_8061958c;
            }
        }
        for (bearing = *(float*)(actor + 0x70) - bearing; bearing <= FLOAT_806195b0;
             bearing += FLOAT_806195ac) {}
        for (; FLOAT_8061958c < bearing; bearing -= FLOAT_806195ac) {}
        *(float*)(DAT_80612a58 + 0x4224) = bearing;
    }
}

// ===========================================================================
// 0x8029000c  snake_render_anim_rate_update
// Per-frame render callback driving the model's anim-rate field (+0x1cc): choose
// a base rate by active-index, scale it up when a threshold field is exceeded,
// integrate toward a target and clamp to >= 0.
// ===========================================================================
void snake_render_anim_rate_update(int* param) {
    int idx = player_ai_get_active_index();
    float zero = FLOAT_80619568;
    float rate = FLOAT_80619564;
    if ((idx != 0xf) && (idx != 0x11) && (idx != 0xd)) {
        rate = FLOAT_806195a8;
        if (idx == 0xb) {
            rate = FLOAT_806195a0;
        }
    }
    if (FLOAT_806195f4 < *(float*)(param[10] + 4)) {
        rate = rate * FLOAT_80619604;
    }
    *(float*)(*param + 0x1cc) = -((float)param[8] * rate - *(float*)(*param + 0x1cc));
    if (*(float*)(*param + 0x1cc) < zero) {
        *(float*)(*param + 0x1cc) = zero;
    }
}

// ===========================================================================
// 0x802900b0  snake_boss_behavior_tick
// The snake boss master FSM. Runs the rope subsystem, tail-sweep damage and
// target update every frame, then dispatches on the actor state word (+0x20):
//   0: spawn/setup - build the two particle emitters + screen effect, enter
//      attack-ready, flip to state 1.
//   1: alert - probe the active clip's user flags, kick attack-pattern select.
//   2: attack dispatch - per anim-clip (0x19d..0x1a2) advance the charge field
//      (+0xb2c) and drive the muzzle/aim particle emitter; on the sweep clip
//      (0x19e) also apply proximity camera-shake + rumble to nearby players.
//   3: chase - turn toward leader, on clip 0x1a3->0x1a4 transition go to state 4.
//   4: wind-up - advance charge, on clip 0x1a4 end re-select attack pattern.
//   5: strike - drive clip 0x1a5, re-select on clip end.
//   6: death - on clip 0x1a6 complete the mission class and destroy the boss.
// ===========================================================================
void snake_boss_behavior_tick(void) {
    int actorRec = __save_gpr_28();
    int rec = *(int*)(actorRec + 0x7c);
    float emitPos[3];               // local_e0/dc/d8 spawn point
    float palette[16];              // afStack_c8 local matrix
    int hdrag;

    aiactor_retarget_visible_teammate();
    if (-1 < *(int*)(rec + 0x9d0)) {
        if (*(int*)(rec + 0x9d0) != *(int*)(rec + 0x20)) {
            *(float*)(rec + 0x9d8) = FLOAT_80619568;
            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
            *(int*)(rec + 0x20)  = *(int*)(rec + 0x9d0);
        }
        *(int*)(rec + 0x9d0) = -1;
    }
    if ((*(unsigned int*)(rec + 0x2c) & 0x80) == 0) {
        *(float*)(rec + 0xa90) = FLOAT_80619568;
    } else {
        float dt = FLOAT_80619580 /
            (float)((double)CONCAT44(0x43300000, (unsigned)(DAT_806109c0 ^ 0x80000000)) - DOUBLE_80619630);
        *(float*)(rec + 0xa90) = FLOAT_806108ec * dt + *(float*)(rec + 0xa90);
    }

    // Compute the emitter spawn point from the head bone's mid-segment.
    float spawnOff[3];
    spawnOff[0] = *(float*)&DAT_803e060c;
    spawnOff[1] = *(float*)&DAT_803e0610;
    spawnOff[2] = *(float*)&DAT_803e0614;
    entity_build_local_matrix(palette, actorRec, 1, 0);
    {
        double half = (double)FLOAT_80619574;
        int* skel = *(int**)(*(int*)(actorRec + 4) + 4);
        float* bp = (float*)skel[*skel * -0x30 + 0x7d8];
        emitPos[0] = (float)(half * (double)(float)((double)bp[3] - (double)bp[0]) + (double)bp[0]);
        emitPos[1] = (float)(half * (double)(float)((double)bp[4] - (double)bp[1]) + (double)bp[1]);
        emitPos[2] = (float)(half * (double)(float)((double)bp[5] - (double)bp[2]) + (double)bp[2]);
        mtx4_transform_point(
            (float*)(*(int*)(*(int*)(actorRec + 4) + 0x10) +
                     *(char*)((int)skel + *skel * -0xc0 + 0x1efd) * 0x40), emitPos);
        mtx4_transform_point(palette, emitPos);
    }

    snake_rope_fire_probe();
    FUN_80295588(actorRec);
    snake_rope_integrate_simple();
    snake_tail_sweep_damage();
    *(float*)(actorRec + 0x68) = FLOAT_80619568;
    snake_update_target_angle(actorRec);

    switch (*(int*)(rec + 0x20)) {
    case 0: {
        snake_rope_ring_reset();
        *(float*)(DAT_80612a58 + 0x415c) = FLOAT_80619568;
        *(int*)(DAT_80612a58 + 0x4160)  = 2;
        *(int*)(DAT_80612a58 + 0x3e88)  = 1;
        sprite_get_texture_id(0x300);
        FUN_802ad80c((double)FLOAT_806195f8, (double)FLOAT_80619648,
                     (double)FLOAT_80619568, (double)FLOAT_80619580);
        float initRate = FLOAT_8061966c;
        *(float*)(actorRec + 0x1cc) = FLOAT_8061966c;
        *(float*)(actorRec + 0x1d8) = initRate;
        *(int*)(DAT_80611d74 + 0xe8c) = actorRec;
        int slot = screen_effect_alloc_slot();
        *(int*)(actorRec + 0x268) = slot;
        screen_effect_init_slot(*(int*)(actorRec + 0x268), actorRec);

        // Emitter A (0x3e94 def / 0x3f60 emitter -> handle at 0x4154).
        particle_def_create((double)FLOAT_806195e8, (double)FLOAT_80619580, DAT_80612a58 + 0x3e94, 0, 1, 1, 0x28d, 0);
        particle_def_set_size_keyframes((double)FLOAT_80619580, (double)FLOAT_80619568, (double)FLOAT_80619568,
                                        (double)FLOAT_80619590, 0x64000000, 0, 0x28140a00);
        particle_def_set_gravity((double)FLOAT_80619670, 0x32140a00);
        particle_def_set_color_end((double)FLOAT_80619560, (double)FLOAT_8061956c, (double)FLOAT_8061963c, (double)FLOAT_80619674);
        particle_def_set_velocity_ranges((double)FLOAT_80619578, (double)FLOAT_80619578, (double)FLOAT_80619574,
                                         (double)FLOAT_806195a8, (double)FLOAT_80619580, (double)FLOAT_80619624);
        particle_def_set_drag((double)FLOAT_80619584);
        particle_emitter_create((double)FLOAT_80619580, (double)FLOAT_80619568, (double)FLOAT_80619574,
                                DAT_80612a58 + 0x3f60, DAT_80612a58 + 0x3e94, 0, 0x4000);
        particle_emitter_set_color_range((double)FLOAT_806195f4, (double)FLOAT_80619568, (double)FLOAT_806195c4, (double)FLOAT_80619568);
        particle_emitter_set_lifetime_range((double)FLOAT_806195f4, (double)FLOAT_80619568);
        particle_handle_clear((unsigned int*)(DAT_80612a58 + 0x4154));
        particle_emitter_spawn(DAT_80612a58 + 0x4154, DAT_80612a58 + 0x3f60, 0x1000, (float*)(actorRec + 100), spawnOff);
        particle_emitter_set_drag((double)FLOAT_80619580);
        particle_emitter_set_size_range((double)FLOAT_80619580, (double)FLOAT_80619580);

        // Emitter B (0x3fe4 def / 0x40b0 emitter -> handle at 0x4134).
        particle_def_create((double)FLOAT_806195e8, (double)FLOAT_80619580, DAT_80612a58 + 0x3fe4, 0, 0, 1, 0x300, 0);
        particle_def_set_size_keyframes((double)FLOAT_80619678, (double)FLOAT_806195a8, (double)FLOAT_806195a8,
                                        (double)FLOAT_80619590, 0xc0c0c00, 0x40404000, 0x30303000);
        particle_def_set_gravity((double)FLOAT_80619670, 0x20202000);
        particle_def_set_size_start_end((double)FLOAT_8061958c, (double)FLOAT_806195ac, (double)FLOAT_8061967c, (double)FLOAT_8061967c);
        particle_def_set_color_end((double)FLOAT_80619560, (double)FLOAT_8061956c, (double)FLOAT_80619680, (double)FLOAT_806195a8);
        particle_def_set_velocity_ranges((double)FLOAT_80619580, (double)FLOAT_80619580, (double)FLOAT_80619578,
                                         (double)FLOAT_806195a8, (double)FLOAT_80619580, (double)FLOAT_80619624);
        particle_def_set_drag((double)FLOAT_806195c8);
        particle_emitter_create((double)FLOAT_80619580, (double)FLOAT_80619568, (double)FLOAT_80619574,
                                DAT_80612a58 + 0x40b0, DAT_80612a58 + 0x3fe4, 0, 0x20000000);
        particle_emitter_set_color_range((double)FLOAT_806195f4, (double)FLOAT_80619568, (double)FLOAT_806195c4, (double)FLOAT_80619568);
        particle_emitter_set_lifetime_range((double)FLOAT_806195f4, (double)FLOAT_80619568);
        particle_handle_clear((unsigned int*)(DAT_80612a58 + 0x4134));
        particle_emitter_spawn(DAT_80612a58 + 0x4134, DAT_80612a58 + 0x40b0, 0x1000, (float*)(actorRec + 100), spawnOff);
        int hb = *(int*)(DAT_80612a58 + 0x4134);
        particle_cached_set_drag((double)FLOAT_80619568, &hb);

        particle_handle_clear((unsigned int*)(DAT_80612a58 + 0x3e8c));
        unsigned long long h = particle_handle_clear((unsigned int*)(DAT_80612a58 + 0x3e90));
        short f = FUN_8037daf0(h, (double)FLOAT_80619568, (double)FLOAT_806195c4, (double)FLOAT_80619568,
                               (double)FLOAT_80619580, (double)FLOAT_80619624, 0, 0,
                               -0x7fd70cdc, 1, 0x1000, (float*)(actorRec + 100), spawnOff, 0, (unsigned*)&DAT_803e060c, 0);
        worm_splash_emit_oneshot((double)FLOAT_80619578, DAT_80612a58 + 0x3e8c, (float*)(actorRec + 100), (char)f);
        snake_anim_mode_toggle(actorRec, 1);
        *(int*)(rec + 0x9dc) = -1;
        *(float*)(rec + 0xb2c) = FLOAT_80619568;
        entity_set_render_type(actorRec, (void*)&damage_filter_invincible);
        entity_register_for_update(actorRec, 2, -0x7fd72ff0);
        *(unsigned int*)(rec + 0x28) |= 0x4000000;
        anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
        if (*(int*)(rec + 0x20) != 1) {
            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
            *(int*)(rec + 0x20)  = 1;
            *(float*)(rec + 0x9d8) = FLOAT_80619568;
            *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
        }
        break;
    }
    case 1: {
        *(unsigned int*)(actorRec + 0x28) |= 1;
        int flags = snake_clip_userflag_probe(actorRec);
        if (flags != 0) {
            *(int*)(DAT_80612a58 + 0x4234) = 1;
            snake_attack_pattern_select();
            entity_set_render_type(actorRec, (void*)&snake_render_anim_rate_update);
        }
        break;
    }
    case 2: {
        int clipA = *(int*)(*(int*)(actorRec + 4) + 0xcc);
        int clipB = *(int*)(*(int*)(actorRec + 4) + 0x124);
        FUN_802adc7c();
        if ((clipA == 0x19d) || (clipB == 0x19d)) {
            float c = *(float*)(rec + 0xb2c);
            if (FLOAT_80619684 <= c) { if (FLOAT_8061968c < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
            else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
        } else if ((clipA == 0x1a2) || (clipB == 0x1a2)) {
            double phase = anim_primary_clip_phase(*(int*)(actorRec + 4));
            if ((double)FLOAT_8061963c <= phase) {
                float c = *(float*)(rec + 0xb2c);
                if (FLOAT_80619698 <= c) { if (FLOAT_8061969c < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
                else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
                double half = (double)FLOAT_80619574;
                if ((phase <= half) || ((double)FLOAT_806196a0 <= phase)) {
                    int h = *(int*)(DAT_80612a58 + 0x4134);
                    particle_cached_set_drag((double)FLOAT_80619568, &h);
                } else {
                    int* skel = *(int**)(*(int*)(actorRec + 4) + 4);
                    float* bp = (float*)skel[*skel * -0x30 + 0x5c8];
                    float mp[3];
                    mp[0] = (float)(half * (double)(float)((double)bp[3] - (double)bp[0]) + (double)bp[0]);
                    mp[1] = (float)(half * (double)(float)((double)bp[4] - (double)bp[1]) + (double)bp[1]);
                    mp[2] = (float)(half * (double)(float)((double)bp[5] - (double)bp[2]) + (double)bp[2]);
                    mtx4_transform_point(
                        (float*)(*(int*)(*(int*)(actorRec + 4) + 0x10) +
                                 *(char*)((int)skel + *skel * -0xc0 + 0x16bd) * 0x40), mp);
                    mtx4_transform_point(palette, mp);
                    int hp = *(int*)(DAT_80612a58 + 0x4134);
                    particle_cached_set_position(&hp, mp);
                    int hd = *(int*)(DAT_80612a58 + 0x4134);
                    particle_cached_set_drag((double)FLOAT_8061956c, &hd);
                }
            } else {
                float c = *(float*)(rec + 0xb2c);
                if (FLOAT_80619690 <= c) { if (FLOAT_80619694 < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
                else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
            }
        } else if ((clipA == 0x1a1) || (clipB == 0x1a1)) {
            double phase = anim_primary_clip_phase(*(int*)(actorRec + 4));
            if ((double)FLOAT_80619608 <= phase) {
                float c = *(float*)(rec + 0xb2c);
                if (FLOAT_806196b0 <= c) { if (FLOAT_806196b4 < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
                else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
            } else {
                if ((phase <= (double)FLOAT_806195fc) || ((double)FLOAT_806196a4 <= phase)) {
                    int h = *(int*)(DAT_80612a58 + 0x4134);
                    particle_cached_set_drag((double)FLOAT_80619568, &h);
                } else {
                    int* skel = *(int**)(*(int*)(actorRec + 4) + 4);
                    float* bp = (float*)skel[*skel * -0x30 + 0x5c8];
                    float mp[3];
                    mp[0] = FLOAT_80619574 * (bp[3] - bp[0]) + bp[0];
                    mp[1] = FLOAT_80619574 * (bp[4] - bp[1]) + bp[1];
                    mp[2] = FLOAT_80619574 * (bp[5] - bp[2]) + bp[2];
                    mtx4_transform_point(
                        (float*)(*(int*)(*(int*)(actorRec + 4) + 0x10) +
                                 *(char*)((int)skel + *skel * -0xc0 + 0x16bd) * 0x40), mp);
                    mtx4_transform_point(palette, mp);
                    int hp = *(int*)(DAT_80612a58 + 0x4134);
                    particle_cached_set_position(&hp, mp);
                    int hd = *(int*)(DAT_80612a58 + 0x4134);
                    particle_cached_set_drag((double)FLOAT_8061956c, &hd);
                }
                float c = *(float*)(rec + 0xb2c);
                if (FLOAT_806196a8 <= c) { if (FLOAT_806196ac < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
                else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
            }
        } else if ((clipA == 0x19e) || (clipB == 0x19e)) {
            double phase = anim_primary_clip_phase(*(int*)(actorRec + 4));
            float c = *(float*)(rec + 0xb2c);
            if (FLOAT_806196a8 <= c) { if (FLOAT_806196ac < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
            else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
            if ((phase <= (double)FLOAT_80619638) || ((double)FLOAT_806196a0 <= phase)) {
                int h = *(int*)(DAT_80612a58 + 0x4134);
                particle_cached_set_drag((double)FLOAT_80619568, &h);
            } else {
                int* skel = *(int**)(*(int*)(actorRec + 4) + 4);
                float* bp = (float*)skel[*skel * -0x30 + 0x5c8];
                float mp[3];
                mp[0] = FLOAT_80619574 * (bp[3] - bp[0]) + bp[0];
                mp[1] = FLOAT_80619574 * (bp[4] - bp[1]) + bp[1];
                mp[2] = FLOAT_80619574 * (bp[5] - bp[2]) + bp[2];
                mtx4_transform_point(
                    (float*)(*(int*)(*(int*)(actorRec + 4) + 0x10) +
                             *(char*)((int)skel + *skel * -0xc0 + 0x16bd) * 0x40), mp);
                mtx4_transform_point(palette, mp);
                unsigned int r1 = rng_counter_tick();
                float jx = (mp[0] + (float)((double)CONCAT44(0x43300000, r1) - DOUBLE_80619610) / FLOAT_806195e4) - FLOAT_80619574;
                unsigned int r2 = rng_counter_tick();
                float jz = (mp[2] + (float)((double)CONCAT44(0x43300000, r2) - DOUBLE_80619610) / FLOAT_806195e4) - FLOAT_80619574;
                if (FLOAT_80619568 <= jx) jx += FLOAT_80619580; else jx -= FLOAT_80619580;
                if (FLOAT_80619568 <= jz) jz += FLOAT_80619580; else jz -= FLOAT_80619580;
                float jitPos[3]; jitPos[0] = jx; jitPos[1] = mp[1]; jitPos[2] = jz;
                int hp = *(int*)(DAT_80612a58 + 0x4134);
                particle_cached_set_position(&hp, jitPos);
                int hd = *(int*)(DAT_80612a58 + 0x4134);
                particle_cached_set_drag((double)FLOAT_80619580, &hd);
                if (((double)FLOAT_806196b8 < phase) && (phase < (double)FLOAT_80619590)) {
                    double baseShake = (double)FLOAT_806195a8;
                    double radiusSq = (double)FLOAT_806196bc;
                    double half2 = (double)FLOAT_80619574;
                    double amp = (double)FLOAT_806195a0;
                    double rumble = (double)FLOAT_806196c0;
                    double cap = (double)FLOAT_80619608;
                    int pOff = 0;
                    for (int pIdx = 0; pIdx < session_get_state_flag(); pIdx++) {
                        int pEnt = *(int*)(DAT_80611d78 + pOff + 0x14);
                        float dz = *(float*)(pEnt + 0x6c) - mp[2];
                        float dx = *(float*)(pEnt + 100) - mp[0];
                        double d2 = (double)(dx*dx + dz*dz);
                        double shake = baseShake;
                        if (d2 < radiusSq) {
                            double d = math_sqrt(d2);
                            shake = (double)(float)((double)FLOAT_806195a4 *
                                     (double)(float)((double)FLOAT_806195c0 - d) + shake);
                        }
                        player_apply_camera_shake((double)(float)(half2 * shake), shake,
                                                  (double)(float)(amp * shake), DAT_80611d78 + pOff);
                        *(float*)(DAT_80611d78 + pOff + 0xe54) = (float)rumble;
                        double clamped = shake;
                        if (cap < shake) clamped = cap;
                        *(float*)(DAT_80611d78 + pOff + 0xe58) = (float)clamped;
                        pOff += 0x141c;
                    }
                }
            }
        } else if ((&DAT_80473200)[*(int*)(rec + 0x9dc)] == 3) {
            float c = *(float*)(rec + 0xb2c);
            if (FLOAT_806196c4 <= c) { if (FLOAT_80619560 < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
            else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
        } else if ((&DAT_80473200)[*(int*)(rec + 0x9dc)] == 4) {
            float c = *(float*)(rec + 0xb2c);
            if (FLOAT_80619684 <= c) { if (FLOAT_8061968c < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
            else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
        } else if ((clipA == 0x19f) || (clipB == 0x19f)) {
            float c = *(float*)(rec + 0xb2c);
            if (FLOAT_806196c8 <= c) { if (FLOAT_806196cc < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
            else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
        } else {
            *(float*)(rec + 0xb2c) = FLOAT_80619580;
        }
        aiactor_turn_toward_position(actorRec, DAT_80612a58 + *(int*)(DAT_80612a58 + 0x4158) * 0xc + 0x4164, 0);
        int slotFlags = anim_active_slot_userflags(*(int*)(actorRec + 4));
        if (slotFlags != 0) {
            snake_attack_pattern_select();
        }
        break;
    }
    case 3: {
        FUN_802adc7c();
        float c = *(float*)(rec + 0xb2c);
        if (FLOAT_806196d0 <= c) { if (FLOAT_806196d4 < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
        else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
        aiactor_turn_toward_leader();
        int f = anim_primary_clip_userflags(actorRec, 0x1a3);
        if (f != 0) {
            if (anim_is_clip_active(actorRec, 0x1a4) == 0) {
                anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
            }
            if (*(int*)(rec + 0x20) != 4) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20)  = 4;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        }
        break;
    }
    case 4: {
        double phase = anim_primary_clip_phase(*(int*)(actorRec + 4));
        FUN_802adc7c();
        if ((double)FLOAT_806196d8 <= phase) {
            float c = *(float*)(rec + 0xb2c);
            if (FLOAT_806196dc <= c) { if (FLOAT_806196e0 < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
            else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
            *(int*)(DAT_80612a58 + 0x4234) = 0;
            int f = anim_primary_clip_userflags(actorRec, 0x1a4);
            if ((f != 0) && (anim_is_clip_active(actorRec, 0x1a3) == 0)) {
                snake_attack_pattern_select();
                *(float*)(rec + 0xb2c) = FLOAT_80619568;
            }
        } else {
            float c = *(float*)(rec + 0xb2c);
            if (FLOAT_806196d0 <= c) { if (FLOAT_806196d4 < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
            else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
        }
        aiactor_turn_toward_leader();
        break;
    }
    case 5: {
        (void)anim_primary_clip_phase(*(int*)(actorRec + 4));
        float c = *(float*)(rec + 0xb2c);
        if (FLOAT_806196c4 <= c) { if (FLOAT_80619560 < c) *(float*)(rec + 0xb2c) = c - FLOAT_80619688; }
        else { *(float*)(rec + 0xb2c) = c + FLOAT_80619688; }
        (void)anim_is_clip_active(actorRec, 0x1a5);
        FUN_802adc7c();
        int f = anim_primary_clip_userflags(actorRec, 0x1a5);
        if ((f != 0) || (anim_primary_clip_userflags(actorRec, 0x1a3) != 0)) {
            snake_attack_pattern_select();
        }
        break;
    }
    case 6: {
        FUN_802adc7c();
        int f = anim_primary_clip_userflags(actorRec, 0x1a6);
        if (f != 0) {
            *(int*)(DAT_80611d74 + 0xe8c) = 0;
            unsigned long long h = statlvl_class_complete((double)FLOAT_80619568, 0.0, 0.0, 0.0, 0, 0, 0, 0);
            aiactor_destroy_instance(h, 0.0, 0.0, 0.0, 0, 0, 0, 0, actorRec);
        }
        break;
    }
    }
    __restore_gpr_28();
    (void)hdrag;
}

// ===========================================================================
// 0x80291184  snake_render_pose_deform
// Per-frame body deform: evaluate the model pose, then bend contiguous bone
// groups by a Y-rotation whose magnitude is the smoothed target angle (+0x4224)
// scaled by per-group phase constants (with an optional Z twist when the strike
// flag +0x4234 is set). The upper-3x3 of three joints are re-composed via
// snake_mtx3_multiply_upper, and finally the head bone palette is copied into
// the shared block at +0x42b0 for the follower segments.
// ===========================================================================
void snake_render_pose_deform(void) {
    int actorRec = __save_gpr_28();
    int rec = *(int*)(actorRec + 0x7c);
    float rotZ[16];    // afStack_b8
    float rotY[24];    // afStack_78

    model_pose_eval();

    double amt = 0.0;
    double tgt = (double)*(float*)(DAT_80612a58 + 0x4224);
    if (0.0 != tgt) {
        float charge = *(float*)(rec + 0xb2c);
        if (tgt <= 0.0) {
            if ((tgt < 0.0) &&
                (amt = (double)(charge * (float)((double)FLOAT_8061956c * tgt) + -charge), amt < tgt)) {
                amt = tgt;
            }
        } else {
            amt = (double)(charge * (float)((double)FLOAT_8061956c * tgt) + charge);
            if (tgt < amt) amt = tgt;
        }
        amt = (double)(float)(amt * (double)FLOAT_80619570);
    }

    int skelBase = *(int*)(*(int*)(actorRec + 4) + 0x10);

    // Head group
    mtx4_set_rotation_y((double)((FLOAT_80619588 * (float)((double)FLOAT_80619564 * amt)) / FLOAT_8061958c), rotY);
    { float* m = (float*)(skelBase + 0x80); mtx4_multiply(m, rotY, m); }

    double baseRotArg = (double)((FLOAT_80619588 * (float)((double)FLOAT_80619574 * amt)) / FLOAT_8061958c);
    mtx4_set_rotation_y(baseRotArg, rotY);
    for (int i = 3, o = 0xc0; i < 6; i++, o += 0x40) { float* m = (float*)(skelBase + o); mtx4_multiply(m, rotY, m); }

    mtx4_set_rotation_y((double)((FLOAT_80619588 * (float)((double)FLOAT_80619590 * amt)) / FLOAT_8061958c), rotY);
    { float* m = (float*)(skelBase + 0x180); mtx4_multiply(m, rotY, m); }
    { float* m = (float*)(skelBase + 0x1c0); mtx4_multiply(m, rotY, m); }
    { float* m = (float*)(skelBase + 0x500); mtx4_multiply(m, rotY, m); }
    for (int i = 0x1c, o = 0x700; i < 0x23; i++, o += 0x40) { float* m = (float*)(skelBase + o); mtx4_multiply(m, rotY, m); }

    mtx4_set_rotation_y((double)((FLOAT_80619588 * (float)((double)FLOAT_80619594 * amt)) / FLOAT_8061958c), rotY);
    for (int i = 0x15, o = 0x540; i < 0x17; i++, o += 0x40) { float* m = (float*)(skelBase + o); mtx4_multiply(m, rotY, m); }
    for (int i = 0xc,  o = 0x300; i < 0xe;  i++, o += 0x40) { float* m = (float*)(skelBase + o); mtx4_multiply(m, rotY, m); }

    mtx4_set_rotation_y((double)((FLOAT_80619588 * (float)((double)FLOAT_80619598 * amt)) / FLOAT_8061958c), rotY);
    for (int i = 0x17, o = 0x5c0; i < 0x1c; i++, o += 0x40) { float* m = (float*)(skelBase + o); mtx4_multiply(m, rotY, m); }
    for (int i = 0xe,  o = 0x380; i < 0x14; i++, o += 0x40) { float* m = (float*)(skelBase + o); mtx4_multiply(m, rotY, m); }

    mtx4_set_rotation_y((double)((FLOAT_80619588 * (float)((double)FLOAT_8061959c * amt)) / FLOAT_8061958c), rotY);
    { float* m = (float*)(skelBase + 0x200); mtx4_multiply(m, rotY, m); }
    { float* m = (float*)(skelBase + 0x240); mtx4_multiply(m, rotY, m); }
    for (int i = 10, o = 0x280; i < 0xc; i++, o += 0x40) { float* m = (float*)(skelBase + o); mtx4_multiply(m, rotY, m); }

    // Last three joints: rebuild upper-3x3 via snake_mtx3_multiply_upper, with an
    // optional Z twist when the strike flag is set.
    mtx4_set_rotation_y((double)((FLOAT_80619588 * (float)((double)FLOAT_806195a0 * amt)) / FLOAT_8061958c), rotY);
    if (*(int*)(DAT_80612a58 + 0x4234) == 1) {
        mtx4_set_rotation_z((double)((FLOAT_80619588 * (float)((double)FLOAT_806195a4 * amt)) / FLOAT_8061958c), rotZ);
        mtx4_multiply(rotY, rotZ, rotY);
    }
    leaf_31e8((unsigned int)rotZ, skelBase + 0x240, 0x40);
    snake_mtx3_multiply_upper(skelBase + 0x240, (int)rotZ, (int)rotY);

    mtx4_set_rotation_y(baseRotArg, rotY);
    if (*(int*)(DAT_80612a58 + 0x4234) == 1) {
        mtx4_set_rotation_z((double)((FLOAT_80619588 * (float)((double)FLOAT_806195a8 * amt)) / FLOAT_8061958c), rotZ);
        mtx4_multiply(rotY, rotZ, rotY);
    }
    leaf_31e8((unsigned int)rotZ, skelBase + 0x280, 0x40);
    snake_mtx3_multiply_upper(skelBase + 0x280, (int)rotZ, (int)rotY);
    leaf_31e8((unsigned int)rotZ, skelBase + 0x2c0, 0x40);
    snake_mtx3_multiply_upper(skelBase + 0x2c0, (int)rotZ, (int)rotY);

    // Copy the head bone palette into the shared block for the follower segments.
    if ((DAT_80612a58 != 0) && (*(int*)(DAT_80612a58 + 0x4bcc) != 0)) {
        int dstOff = 0;
        int srcOff = 0;
        int count = *(int*)(*(int*)(*(int*)(*(int*)(DAT_80612a58 + 0x4bcc) + 4) + 4) + 4);
        for (int i = 0; i < count; i++) {
            mtx4_copy((float*)(skelBase + *(int*)(**(int**)(*(int*)(actorRec + 4) + 0x84) + srcOff) * 0x40),
                      (float*)(DAT_80612a58 + dstOff + 0x42b0));
            dstOff += 0x40;
            srcOff += 4;
        }
    }
    __restore_gpr_28();
}

// ===========================================================================
// 0x802916a0  snake_segment_sync_from_head
// Copy the head record's transform (pos +100/+0x68/+0x6c, yaw +0x70, matrices
// +0xc0/+200) and bone palette (+0x42b0 -> segment skeleton) into this follower
// segment entity, then OR in its render/visibility flags.
// ===========================================================================
void snake_segment_sync_from_head(void) {
    int seg = FUN_803826f0();
    if (*(int*)(DAT_80612a58 + 0x4bd0) != 0) {
        int head = *(int*)(DAT_80612a58 + 0x4bd0);
        int off = 0;
        *(int*)(seg + 100)   = *(int*)(head + 100);
        *(int*)(seg + 0x68)  = *(int*)(head + 0x68);
        *(int*)(seg + 0x6c)  = *(int*)(head + 0x6c);
        *(int*)(seg + 0x70)  = *(int*)(head + 0x70);
        *(int*)(seg + 0xc4)  = *(int*)(seg + 0x70);
        *(int*)(seg + 0xc0)  = *(int*)(head + 0xc0);
        *(int*)(seg + 200)   = *(int*)(head + 200);
        int count = *(int*)(*(int*)(*(int*)(seg + 4) + 4) + 4);
        for (int i = 0; i < count; i++) {
            mtx4_copy((float*)(DAT_80612a58 + off + 0x42b0),
                      (float*)(*(int*)(*(int*)(seg + 4) + 0x10) + off));
            off += 0x40;
        }
        mtx4_copy((float*)(*(int*)(DAT_80612a58 + 0x4bd0) + 0x274), (float*)(seg + 0x274));
        *(unsigned int*)(seg + 0x28) |= 0x200061;
    }
    FUN_8038273c();
}
