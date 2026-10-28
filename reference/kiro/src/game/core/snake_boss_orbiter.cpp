// snake_boss_orbiter.cpp - snake/spine boss "orbiter" minion subsystem, AI aim
// steering, lift/platform audio state machine and direct-GX ring rendering.
//
// The snake boss spawns up to 6 "orbiter" minions (team-tagged players 'RAG0'..
// 'RAG5') that circle the boss and then dive at the players. Each orbiter has a
// 0x44-byte slot in the boss global block starting at DAT_80612a58+0x4a30:
//   +0x00 entity   +0x04 nodeTarget  +0x0c/+0x10/+0x14 relPos
//   +0x16..        ... phase/spin/timer fields ...
//   +0x18 phase    +0x1c amp         +0x20 orbitR    +0x24 state
//   +0x28 spinDir  +0x2c relVel...   +0x30 seekYaw   +0x34 seekNode
//   +0x38 emitterA +0x3c emitterB    +0x40 trailTimer
// (offsets shown relative to the slot base; the code uses absolute +0x4a30..).
//
// Functions:
//   snake_orbiter_acquire_targets (0x802917a4): bind the 6 'RAG0'..'RAG5' team
//     players into the orbiter slots.
//   snake_orbiter_spawn_ring      (0x80291940): place up to 6 orbiters in a ring.
//   snake_rope_nearest_two_nodes  (0x80291b78): two nearest attractor nodes.
//   snake_orbiter_steer_seek      (0x80291c10): turn-rate-clamped seek with wall
//     raycast avoidance + RNG jitter.
//   snake_orbiter_avoid_angle     (0x8029200c): spread orbiters sharing a node.
//   snake_orbiter_update_all      (0x802920b0): the 6-slot orbiter FSM.
//   snake_boss_idle_tick          (0x80292cb4): light pre-spawn/idle tick.
//   snake_any_player_airborne     (0x80292e14): any-player-airborne predicate.
//   snake_lift_audio_state        (0x80292e5c): lift descent audio state machine.
//   snake_render_gx_rings         (0x80293390): direct-GX ring/aura quad render.
//
// Address range: 0x802917a4 - 0x80293390
// Subsystem: game (AI actors / boss)

#include "common.h"

extern "C" {

// --- GX CP write-gather FIFO port (physical 0xCC008000) --------------------
// The renderer streams vertex data by storing to this single volatile port.
static volatile float*        const GX_FIFO_F = (volatile float*)0xCC008000u;
static volatile unsigned int* const GX_FIFO_U = (volatile unsigned int*)0xCC008000u;
static inline void gx_fifo_f(float v)        { *GX_FIFO_F = v; }
static inline void gx_fifo_u(unsigned int v) { *GX_FIFO_U = v; }

// --- raw actor-context accessors (still FUN_ in the IDB) -------------------
extern int    FUN_803826e4(void);        // current actor entity record (returns hi=count,lo=ptr)
extern void   FUN_80382730(void);        // tick epilogue
extern int    __save_gpr_28(void);
extern void   __restore_gpr_28(void);
extern int    __save_gpr_25(void);
extern void   __restore_gpr_25(void);

// --- same-module snake helpers ---------------------------------------------
extern void snake_orbiter_steer_seek(double speed, double turnRate, float* dir, int* slotEntPtr);
extern float snake_orbiter_avoid_angle(double angle, int selfIdx);
extern int  snake_any_player_airborne(void);

// --- data globals ----------------------------------------------------------
extern int   DAT_80612a58;               // snake-boss global control block base
extern int   DAT_80611b8c;               // attractor-node record table base
extern int   DAT_80611b04;               // room/collision table base
extern int   DAT_80611d78;               // player record table base (stride 0x141c)
extern int   DAT_80612610;               // player count
extern int   DAT_806109c0;               // frame delta ticks (signed, xor bias)
extern char  DAT_805015bd;               // level id byte (coop gate)

// --- float / double constants (rodata, snake block) ------------------------
extern const float  FLOAT_80619560;      // 1.0
extern const float  FLOAT_8061956c;      // small offset / lerp
extern const float  FLOAT_80619568;      // 0.0
extern const float  FLOAT_80619570;      // amplitude
extern const float  FLOAT_80619574;      // 0.5
extern const float  FLOAT_80619578;      // spread / spawn height
extern const float  FLOAT_80619580;      // 1.0 (unit)
extern const float  FLOAT_80619584;      // lift stage-2 gate
extern const float  FLOAT_80619588;      // 180.0
extern const float  FLOAT_8061958c;      // PI
extern const float  FLOAT_806195a4;      // step / amp
extern const float  FLOAT_806195ac;      // 2*PI wrap
extern const float  FLOAT_806195b0;      // -PI wrap lower bound
extern const float  FLOAT_806195bc;      // sound param a
extern const float  FLOAT_806195c0;      // lift stage-1 gate
extern const float  FLOAT_806195d4;      // sound param b
extern const float  FLOAT_806195e4;      // rng denominator
extern const float  FLOAT_806195e8;      // avoidance / knockback scale
extern const float  FLOAT_806195f0;      // orbit radius z
extern const float  FLOAT_806195f4;      // 1.0 activation / big const
extern const float  FLOAT_806195f8;      // arrival radius^2
extern const float  FLOAT_80619624;      // trail drag value
extern const float  FLOAT_80619628;      // trail timer init
extern const float  FLOAT_8061964c;      // -1.0 sentinel
extern const float  FLOAT_80619654;      // small angle step
extern const float  FLOAT_8061967c;      // jitter scale
extern const float  FLOAT_806196d8;      // seek vertical bias
extern const float  FLOAT_806196e4;      // ring phase step
extern const float  FLOAT_806196e8;      // ring phase offset (PI/2)
extern const float  FLOAT_806196ec;      // orbit center x
extern const float  FLOAT_806196f0;      // orbit center z
extern const float  FLOAT_806196f4;      // orbit y base
extern const float  FLOAT_806196f8;      // wall-avoid radius^2
extern const float  FLOAT_806196fc;      // orbiter spread angle
extern const float  FLOAT_80619700;      // arc-out accel bias
extern const float  FLOAT_80619704;      // arc/seek accel
extern const float  FLOAT_80619708;      // rise accel bias
extern const float  FLOAT_8061970c;      // arc decel
extern const float  FLOAT_80619710;      // seek phase step
extern const float  FLOAT_80619714;      // seek pitch cap
extern const float  FLOAT_80619718;      // seek phase gate
extern const float  FLOAT_8061971c;      // rise y-gate (spin+)
extern const float  FLOAT_80619720;      // rise y-gate (spin-)
extern const float  FLOAT_80619724;      // seek turn rate
extern const float  FLOAT_80619728;      // attack turn rate
extern const float  FLOAT_8061972c;      // attack arrival radius^2
extern const float  FLOAT_80619730;      // reset pos x
extern const float  FLOAT_80619734;      // reset pos y
extern const float  FLOAT_80619738;      // reset pos z
extern const float  FLOAT_8061973c;      // idle reset y
extern const float  FLOAT_80619740;      // airborne y-vel threshold
extern const float  FLOAT_80619744;      // lift stage-1 target y
extern const float  FLOAT_80619748;      // lift stage-2 target y
extern const float  FLOAT_8061974c;      // lift stage-3 gate
extern const float  FLOAT_80619750;      // lift stage-3 target y
extern const float  FLOAT_80619758;      // ring render y offset
extern const float  FLOAT_80619648;      // ring render half-height
extern const float  FLOAT_806108e8;      // per-frame dt (orbiter)
extern const float  FLOAT_806108ec;      // per-frame charge accel
extern const double DOUBLE_80619610;     // rng bias
extern const double DOUBLE_80619630;     // frame-delta bias

// jump-target label used as a render callback pointer in snake_boss_idle_tick.
extern char LAB_80292ca4;

// --- named cross-module helpers (call-site typed) --------------------------
extern double math_sqrt(double x);
extern double math_poly_trig(double x);
extern unsigned int rng_counter_tick(void);

extern void dir_to_angles(float* dir, float* pitch, float* yaw);
extern void angles_to_dir(double pitch, double yaw, float* dir);

extern int  collision_entity_raycast(int room, int origin, float* dir, int mask,
                                     float* hitPos, float* hitNormal, int flags);
extern void gx_begin_textured_batch(int a, int b, int c, int d, int e);
extern void gx_end_batch(void);

extern unsigned int* entity_get_world_position(int entity);
extern int* soundpad_resolve_zone_node(int nodeId);

extern int  aiactor_find_player_by_team(int teamTag);
extern void player_ai_projectile_death_dispatch(void);
extern void player_ai_explosion_spawn(int a, int b, float* pos, float* p4, int p5, int p6, int p7);

extern unsigned long long anim_play_scaled(double a, double b);
extern int  anim_is_clip_active(int actor, int clipId);
extern int  anim_active_slot_userflags(int animCtx);

extern void aiactor_retarget_visible_teammate(void);
extern void entity_set_render_type(int entity, void* cb);

extern void sound_init_play_params(int paramsPtr);
extern unsigned int sound_play_request(int* params);
extern void damage_play_pain_sound(int id, int p2, unsigned int* pos);
extern void damage_dispatch_to_entity(double a, double b, double c, double d, double e,
                                      double f, double g, double h, int* eventData);
extern void switch_slot_reset(int slotPtr);
extern void particle_cached_set_drag(double drag, int* handleSlot);

} // extern "C"

// ===========================================================================
// 0x802917a4  snake_orbiter_acquire_targets
// Bind the 6 orbiter-team players (team tags 'RAG0'..'RAG5' = 0x52414730..35)
// into the 6 orbiter slot base pointers; clear each found entity's active bit.
// ===========================================================================
void snake_orbiter_acquire_targets(void) {
    const int slotOff[6] = {0x4a30, 0x4a74, 0x4ab8, 0x4afc, 0x4b40, 0x4b84};
    for (int i = 0; i < 6; i++) {
        int off = slotOff[i];
        if (*(int*)(DAT_80612a58 + off) == 0) {
            int e = aiactor_find_player_by_team(0x52414730 + i);
            *(int*)(DAT_80612a58 + off) = e;
            if (e != 0) {
                *(unsigned int*)(e + 0x28) &= 0xfffffffe;
            }
        }
    }
}

// ===========================================================================
// 0x80291940  snake_orbiter_spawn_ring
// Place up to 6 orbiter minions in a ring around the boss: each gets a ring
// phase (uVar7 index), a poly-trig XZ position, an alternating spin direction,
// and its slot marked active (state 1). Stops after 3 have been placed.
// ===========================================================================
void snake_orbiter_spawn_ring(void) {
    int actorRec = __save_gpr_28();
    unsigned int placed = 0;
    int i = 0;
    int off = 0;
    while ((i < 6) && ((int)placed < 3)) {
        if (*(int*)(DAT_80612a58 + off + 0x4a54) == 0) {
            *(float*)(DAT_80612a58 + off + 0x4a48) =
                FLOAT_806196e4 * (float)((double)CONCAT44(0x43300000, placed ^ 0x80000000) - DOUBLE_80619630);
            double c = math_poly_trig((double)((FLOAT_80619588 *
                (*(float*)(DAT_80612a58 + off + 0x4a48) + *(float*)(actorRec + 0x70))) / FLOAT_8061958c - FLOAT_806196e8));
            double s = math_poly_trig((double)((FLOAT_80619588 *
                (*(float*)(DAT_80612a58 + off + 0x4a48) + *(float*)(actorRec + 0x70))) / FLOAT_8061958c));
            // Alternating spin direction by parity of 'placed'.
            if ((placed & 1) == 0) {
                *(float*)(DAT_80612a58 + off + 0x4a5c) = FLOAT_80619568;
                *(int*)(DAT_80612a58 + off + 0x4a58) = 1;
            } else {
                *(float*)(DAT_80612a58 + off + 0x4a5c) = FLOAT_80619578;
                *(int*)(DAT_80612a58 + off + 0x4a58) = -1;
            }
            int slot = *(int*)(DAT_80612a58 + off + 0x4a30);
            float yBase = FLOAT_806196f4 + *(float*)(DAT_80612a58 + off + 0x4a5c);
            *(float*)(slot + 100)   = (float)((double)FLOAT_806195f4 * s + (double)FLOAT_806196ec);
            *(float*)(slot + 0x68)  = yBase;
            *(float*)(slot + 0x6c)  = (float)((double)FLOAT_806195f0 * c + (double)FLOAT_806196f0);
            *(float*)(slot + 0x1cc) = FLOAT_80619578;
            *(unsigned int*)(slot + 0x28) |= 0x71;
            if ((*(unsigned int*)(slot + 0x28) & 0x10000) != 0) {
                player_ai_projectile_death_dispatch();
            }
            *(char*)(*(int*)(DAT_80612a58 + off + 0x4a30) + 0x33) = (char)i;
            placed++;
            *(float*)(DAT_80612a58 + off + 0x4a4c) = FLOAT_806195f4;
            *(int*)(DAT_80612a58 + off + 0x4a54) = 1;
            *(float*)(DAT_80612a58 + off + 0x4a50) = FLOAT_80619568;
            *(float*)(DAT_80612a58 + off + 0x4a70) = FLOAT_80619628;
        }
        i++;
        off += 0x44;
    }
    __restore_gpr_28();
}

// ===========================================================================
// 0x80291b78  snake_rope_nearest_two_nodes
// Find the two nearest of 13 attractor nodes to 'pt'; out[0]=nearest index,
// out[1]=second-nearest (the previous nearest as it is displaced).
// ===========================================================================
void snake_rope_nearest_two_nodes(float* pt, int* out) {
    out[0] = -1;
    out[1] = -1;
    float bestD = FLOAT_8061964c;   // -1.0; first candidate always wins
    int slotOff = 0;
    for (int i = 0; i < 13; i++) {
        int rec = DAT_80611b8c + *(int*)(DAT_80612a58 + slotOff + 0x4bf0) * 0x20;
        float dx = *(float*)(rec + 0xc)  - pt[0];
        float dy = *(float*)(rec + 0x10) - pt[1];
        float dz = *(float*)(rec + 0x14) - pt[2];
        float d = dz*dz + dx*dx + dy*dy;
        if (d < bestD) {
            out[1] = out[0];
            out[0] = i;
            bestD = d;
        }
        slotOff += 4;
    }
}

// ===========================================================================
// 0x80291c10  snake_orbiter_steer_seek
// Steer an orbiter entity (slot pointer in slotEntPtr[0]) toward 'dir': clamp
// its yaw (+0x70) and pitch (+0xc0) toward the desired angles by 'turnRate',
// rebuild the movement direction at length 'speed', push out of walls found by a
// short raycast, then advance the entity position with a small RNG jitter.
// ===========================================================================
void snake_orbiter_steer_seek(double speed, double turnRate, float* dir, int* slotEntPtr) {
    int ent = *slotEntPtr;
    float pitch, yaw;
    float hitPos[3];
    float hitNormal[3];
    dir_to_angles(dir, &pitch, &yaw);
    if (yaw < FLOAT_80619568) {
        yaw += FLOAT_806195ac;
    }

    // Yaw toward target, clamped by turnRate, wrapped to [0,2PI).
    double dyaw = (double)(yaw - *(float*)(ent + 0x70));
    for (; dyaw <= (double)FLOAT_806195b0; dyaw = (double)(float)(dyaw + (double)FLOAT_806195ac)) {}
    for (; (double)FLOAT_8061958c < dyaw; dyaw = (double)(float)(dyaw - (double)FLOAT_806195ac)) {}
    if (dyaw <= turnRate) {
        if (-turnRate <= dyaw) *(float*)(ent + 0x70) = (float)((double)*(float*)(ent + 0x70) + dyaw);
        else                   *(float*)(ent + 0x70) = (float)((double)*(float*)(ent + 0x70) - turnRate);
    } else {
        *(float*)(ent + 0x70) = (float)((double)*(float*)(ent + 0x70) + turnRate);
    }
    if (FLOAT_806195ac < *(float*)(ent + 0x70)) *(float*)(ent + 0x70) -= FLOAT_806195ac;
    if (*(float*)(ent + 0x70) < FLOAT_80619568) *(float*)(ent + 0x70) += FLOAT_806195ac;
    *(int*)(ent + 0xc4) = *(int*)(ent + 0x70);

    // Pitch toward target, clamped by turnRate.
    double dpitch = (double)(pitch - *(float*)(ent + 0xc0));
    for (; dpitch <= (double)FLOAT_806195b0; dpitch = (double)(float)(dpitch + (double)FLOAT_806195ac)) {}
    for (; (double)FLOAT_8061958c < dpitch; dpitch = (double)(float)(dpitch - (double)FLOAT_806195ac)) {}
    if (dpitch <= turnRate) {
        if (-turnRate <= dpitch) *(float*)(ent + 0xc0) = (float)((double)*(float*)(ent + 0xc0) + dpitch);
        else                     *(float*)(ent + 0xc0) = (float)((double)*(float*)(ent + 0xc0) - turnRate);
    } else {
        *(float*)(ent + 0xc0) = (float)((double)*(float*)(ent + 0xc0) + turnRate);
    }
    if (FLOAT_806195ac < *(float*)(ent + 0xc0)) *(float*)(ent + 0xc0) -= FLOAT_806195ac;
    if (*(float*)(ent + 0xc0) < FLOAT_80619568) *(float*)(ent + 0xc0) += FLOAT_806195ac;

    angles_to_dir((double)*(float*)(ent + 0xc0), (double)*(float*)(ent + 0xc4), dir);
    double len = math_sqrt((double)(dir[2]*dir[2] + dir[0]*dir[0] + dir[1]*dir[1]));
    float scale = (float)(speed / len);
    dir[0] *= scale; dir[1] *= scale; dir[2] *= scale;

    // Short wall raycast; if a near hit, blend the movement dir toward reflection.
    int hit = collision_entity_raycast(
        *(int*)(*(int*)(DAT_80611b04 + *(int*)(ent + 0x34) * 4) + 4),
        ent + 100, dir, 1, hitPos, hitNormal, 0);
    if (hit != 0) {
        hitPos[0] -= *(float*)(ent + 100);
        hitPos[1] -= *(float*)(ent + 0x68);
        hitPos[2] -= *(float*)(ent + 0x6c);
        float d2 = hitPos[2]*hitPos[2] + hitPos[0]*hitPos[0] + hitPos[1]*hitPos[1];
        if (d2 < FLOAT_806196f8) {
            float t = FLOAT_806195e8 * (FLOAT_80619580 - d2 / FLOAT_806196f8);
            float inv = FLOAT_80619580 - t;
            dir[0] = dir[0]*inv + hitNormal[0]*t;
            dir[1] = dir[1]*inv + hitNormal[1]*t;
            dir[2] = dir[2]*inv + hitNormal[2]*t;
        }
    }

    // Advance position with a per-axis RNG jitter around the moved direction.
    unsigned int r1 = rng_counter_tick();
    *(float*)(ent + 100) += (FLOAT_8061967c * ((float)((double)CONCAT44(0x43300000, r1) - DOUBLE_80619610) / FLOAT_806195e4) + dir[0]) - FLOAT_80619560;
    unsigned int r2 = rng_counter_tick();
    *(float*)(ent + 0x68) += (FLOAT_8061967c * ((float)((double)CONCAT44(0x43300000, r2) - DOUBLE_80619610) / FLOAT_806195e4) + dir[1]) - FLOAT_80619560;
    unsigned int r3 = rng_counter_tick();
    *(float*)(ent + 0x6c) += (FLOAT_8061967c * ((float)((double)CONCAT44(0x43300000, r3) - DOUBLE_80619610) / FLOAT_806195e4) + dir[2]) - FLOAT_80619560;
}

// ===========================================================================
// 0x8029200c  snake_orbiter_avoid_angle
// Given a desired bearing 'angle' for the orbiter with index 'selfIdx', nudge it
// away from other orbiters that are in the attack state (5) targeting the same
// node, so multiple orbiters don't stack. Returns the adjusted bearing.
// ===========================================================================
float snake_orbiter_avoid_angle(double angle, int selfIdx) {
    int off = 0;
    for (int i = 0; i < 6; i++) {
        int slot = DAT_80612a58 + off;
        if ((*(int*)(slot + 0x4a54) == 5) && (*(int*)(slot + 0x4a64) == selfIdx)) {
            float rel;
            for (rel = (float)((double)*(float*)(slot + 0x4a60) - angle); rel <= FLOAT_806195b0; rel += FLOAT_806195ac) {}
            for (; FLOAT_8061958c < rel; rel -= FLOAT_806195ac) {}
            if (ABS(rel) < FLOAT_806196fc) {
                if (rel <= FLOAT_80619568) angle = (double)(float)(angle + (double)(FLOAT_806196fc + rel));
                else                       angle = (double)(float)(angle - (double)(FLOAT_806196fc - rel));
            }
        }
        off += 0x44;
    }
    return (float)angle;   // adjusted bearing (FP return register in the original)
}

// ===========================================================================
// 0x802920b0  snake_orbiter_update_all
// The 6-slot orbiter FSM. For each active orbiter, dispatch on its state field
// (+0x4a54):
//   1: rise - accelerate upward, spin, until y-gate -> state 3.
//   2: arc-out - accelerate outward with a decel curve -> state 4.
//   3: seek-node - phase toward the two nearest attractor nodes, pick one -> 4.
//   4: node-traverse - steer toward the chosen node; on arrival step to the next
//      node; if a player is close, snapshot bearing + play a bark -> state 5.
//   5: attack-player - dive at the target player; on contact/overlap set b2 and
//      snap to the player -> triggers a damage dispatch below.
//   6: reset - park the orbiter back at the spawn point, clear its active bit.
// After the per-state work, non-4/5 states re-place the orbiter on its poly-trig
// orbit, emit an explosion when its render-rate hits 1.0, drive its attack anim,
// and update the two trail emitters' drag by the trail timer.
// ===========================================================================
void snake_orbiter_update_all(void) {
    int uVar6 = __save_gpr_25();
    bool attackContact = false;
    int slotIdx = 0;
    int off = 0;
    do {
        float resetX = FLOAT_80619730;
        int slot = DAT_80612a58 + off;
        if (*(int*)(slot + 0x4a54) < 1) {
            int hA = *(int*)(slot + 0x4a68);
            particle_cached_set_drag((double)FLOAT_80619568, &hA);
            int hB = *(int*)(DAT_80612a58 + off + 0x4a6c);
            particle_cached_set_drag((double)FLOAT_80619568, &hB);
        } else {
            int state = *(int*)(slot + 0x4a54);
            int ent = *(int*)(slot + 0x4a30);
            double px = (double)*(float*)(ent + 100);
            double py = (double)*(float*)(ent + 0x68);
            double pz = (double)*(float*)(ent + 0x6c);
            float local_100[3];         // scratch dir/target
            int nn[2];                  // nearest-two node indices (local_108/104)

            if (state == 1) {
                *(float*)(slot + 0x4a50) = FLOAT_80619560 * FLOAT_806108e8 + *(float*)(slot + 0x4a50);
                double phase2 = (double)(*(float*)(slot + 0x4a50) * *(float*)(slot + 0x4a50));
                *(float*)(slot + 0x4a48) =
                    (float)((double)CONCAT44(0x43300000, (unsigned)(*(unsigned int*)(slot + 0x4a58) ^ 0x80000000)) - DOUBLE_80619630)
                    * (float)((double)FLOAT_806108e8 + phase2) + *(float*)(slot + 0x4a48);
                if (FLOAT_806195ac < *(float*)(slot + 0x4a48)) *(float*)(slot + 0x4a48) -= FLOAT_806195ac;
                if (*(float*)(slot + 0x4a50) <= FLOAT_80619580) {
                    *(float*)(slot + 0x4a4c) = (float)((double)FLOAT_80619704 * phase2 + (double)*(float*)(slot + 0x4a4c));
                } else {
                    *(float*)(slot + 0x4a4c) += (float)((double)FLOAT_806195a4 * phase2 + (double)FLOAT_80619700);
                    *(float*)(slot + 0x4a50) = FLOAT_80619568;
                    *(int*)(slot + 0x4a54) = 3;
                }
                *(float*)(slot + 0x4a44) = -(float)((double)FLOAT_806196d8 * phase2 - (double)FLOAT_80619708);
                *(float*)(ent + 0x68) += *(float*)(slot + 0x4a44);
                int hA = *(int*)(DAT_80612a58 + off + 0x4a68);
                particle_cached_set_drag((double)FLOAT_806195f4, &hA);
                int hB = *(int*)(DAT_80612a58 + off + 0x4a6c);
                particle_cached_set_drag((double)FLOAT_806195f4, &hB);
            } else if (state == 2) {
                *(float*)(slot + 0x4a50) = FLOAT_8061967c * FLOAT_806108e8 + *(float*)(slot + 0x4a50);
                float dt = FLOAT_80619578 * FLOAT_806108e8;
                *(float*)(slot + 0x4a48) =
                    (float)((double)dt * (double)(float)((double)CONCAT44(0x43300000, (unsigned)(*(unsigned int*)(slot + 0x4a58) ^ 0x80000000)) - DOUBLE_80619630)
                            + (double)*(float*)(slot + 0x4a48));
                if (FLOAT_806195ac < *(float*)(slot + 0x4a48)) *(float*)(slot + 0x4a48) -= FLOAT_806195ac;
                float v = *(float*)(slot + 0x4a50);
                if (v <= FLOAT_80619580) {
                    *(float*)(slot + 0x4a4c) = -(FLOAT_8061970c * v * v - *(float*)(slot + 0x4a4c));
                } else {
                    *(float*)(slot + 0x4a4c) -= FLOAT_8061970c;
                    *(float*)(slot + 0x4a50) = FLOAT_80619568;
                    *(int*)(slot + 0x4a54) = 4;
                }
            } else if (state == 3) {
                *(float*)(slot + 0x4a48) =
                    (float)((double)FLOAT_80619574 * (double)(FLOAT_806108e8 *
                        (float)((double)CONCAT44(0x43300000, (unsigned)(*(unsigned int*)(slot + 0x4a58) ^ 0x80000000)) - DOUBLE_80619630))
                        + (double)*(float*)(slot + 0x4a48));
                if (FLOAT_806195ac < *(float*)(slot + 0x4a48)) *(float*)(slot + 0x4a48) -= FLOAT_806195ac;
                *(float*)(slot + 0x4a4c) += FLOAT_80619710;
                if (FLOAT_80619714 < *(float*)(slot + 0x4a44)) *(float*)(slot + 0x4a44) -= FLOAT_80619654;
                int hA = *(int*)(DAT_80612a58 + off + 0x4a68);
                particle_cached_set_drag((double)FLOAT_806195f4, &hA);
                int hB = *(int*)(DAT_80612a58 + off + 0x4a6c);
                particle_cached_set_drag((double)FLOAT_806195f4, &hB);
                if (*(float*)(slot + 0x4a4c) <= FLOAT_80619718) {
                    if (*(int*)(slot + 0x4a58) == 1) {
                        if (FLOAT_8061971c < *(float*)(ent + 0x68))
                            *(float*)(ent + 0x68) += *(float*)(slot + 0x4a44) * FLOAT_806108e8;
                    } else if (FLOAT_80619720 < *(float*)(ent + 0x68)) {
                        *(float*)(ent + 0x68) += *(float*)(slot + 0x4a44) * FLOAT_806108e8;
                    }
                } else {
                    snake_rope_nearest_two_nodes((float*)(ent + 100), nn);
                    if ((nn[0] < 0) || (nn[1] < 0)) {
                        if (nn[0] < 0) {
                            if (nn[1] < 0) *(int*)(slot + 0x4a34) = 0;
                            else           *(int*)(slot + 0x4a34) = nn[1];
                        } else {
                            *(int*)(slot + 0x4a34) = nn[0];
                        }
                    } else {
                        int nA = DAT_80611b8c + *(int*)(DAT_80612a58 + nn[0]*4 + 0x4bf0) * 0x20;
                        int nB = DAT_80611b8c + *(int*)(DAT_80612a58 + nn[1]*4 + 0x4bf0) * 0x20;
                        // Pick the node whose weighted planar distance is smaller.
                        float ex = *(float*)(ent + 100), ey = *(float*)(ent + 0x68), ez = *(float*)(ent + 0x6c);
                        float w0 = *(float*)(slot + 19000), w1 = *(float*)(slot + 0x4a40), w2 = *(float*)(slot + 0x4a3c);
                        float scoreB = w1*(ez - *(float*)(nB + 0x14)) + (w0*(ex - *(float*)(nB + 0xc)) + w2*(ey - *(float*)(nB + 0x10)));
                        float scoreA = w1*(ez - *(float*)(nA + 0x14)) + (w0*(ex - *(float*)(nA + 0xc)) + w2*(ey - *(float*)(nA + 0x10)));
                        if (scoreB <= scoreA) *(int*)(slot + 0x4a34) = nn[1];
                        else                  *(int*)(slot + 0x4a34) = nn[0];
                    }
                    *(int*)(slot + 0x4a54) = 4;
                }
            } else if (state == 4) {
                int* node = soundpad_resolve_zone_node(*(int*)(DAT_80612a58 + *(int*)(slot + 0x4a34) * 4 + 0x4bf0));
                unsigned int r1 = rng_counter_tick();
                local_100[0] = FLOAT_80619574 * (float)(((double)CONCAT44(0x43300000, r1) - DOUBLE_80619610) / (double)FLOAT_806195e4)
                               + ((float)node[3] - *(float*)(ent + 100));
                float local_fc = (FLOAT_80619578 + (float)node[4] + *(float*)(slot + 0x4a5c)) - *(float*)(ent + 0x68);
                unsigned int r2 = rng_counter_tick();
                local_100[1] = local_fc;
                float local_f8 = (float)((double)FLOAT_80619574 * (double)((float)((double)CONCAT44(0x43300000, r2) - DOUBLE_80619610) / FLOAT_806195e4)
                                 + (double)((float)node[5] - *(float*)(ent + 0x6c)));
                local_100[2] = local_f8;
                snake_orbiter_steer_seek((double)FLOAT_80619704, (double)FLOAT_80619724, local_100, (int*)(slot + 0x4a30));
                float ax = (float)node[3] - *(float*)(ent + 100);
                float az = (float)node[5] - *(float*)(ent + 0x6c);
                if (ax*ax + az*az < FLOAT_806195f8) {
                    *(int*)(slot + 0x4a34) -= *(int*)(slot + 0x4a58);
                    if (*(int*)(slot + 0x4a34) < 0xd) {
                        if (*(int*)(slot + 0x4a34) < 0) *(int*)(slot + 0x4a34) = 0xc;
                    } else {
                        *(int*)(slot + 0x4a34) = 0;
                    }
                }
                // If a player is in range, snapshot bearing + bark, enter attack.
                int pOff = 0;
                for (int pi = 0; pi < DAT_80612610; pi++) {
                    float dz2 = *(float*)(DAT_80611d78 + pOff + 0xbc) - *(float*)(ent + 0x6c);
                    float dx2 = *(float*)(DAT_80611d78 + pOff + 0xb4) - *(float*)(ent + 100);
                    if (dx2*dx2 + dz2*dz2 < FLOAT_806195f4) {
                        float pit, yw;
                        dir_to_angles(local_100, &pit, &yw);
                        if (yw < FLOAT_80619568) yw += FLOAT_806195ac;
                        *(int*)(slot + 0x4a64) = pi;
                        *(float*)(slot + 0x4a60) = snake_orbiter_avoid_angle((double)yw, pi);
                        *(int*)(slot + 0x4a54) = 5;
                        unsigned int* wp = entity_get_world_position(ent);
                        damage_play_pain_sound(0xb0, 0, wp);
                        break;
                    }
                    pOff += 0x141c;
                }
            } else if (state == 5) {
                int pIdx = *(int*)(slot + 0x4a64);
                angles_to_dir((double)FLOAT_80619568, (double)*(float*)(slot + 0x4a60), local_100);
                int pOff = pIdx * 0x141c;
                int pRec = DAT_80611d78 + pOff;
                local_100[0] = (float)((double)(*(float*)(pRec + 0xb4) - local_100[0] * FLOAT_806195e8) - (double)*(float*)(ent + 100));
                local_100[1] = (float)((double)*(float*)(pRec + 0xb8) - (double)(local_100[1] * FLOAT_806195e8)) - *(float*)(ent + 0x68);
                local_100[2] = (*(float*)(pRec + 0xbc) - local_100[2] * FLOAT_806195e8) - *(float*)(ent + 0x6c);
                snake_orbiter_steer_seek((double)FLOAT_80619728, (double)FLOAT_806195f8, local_100, (int*)(slot + 0x4a30));
                pRec = DAT_80611d78 + pOff;
                double ddx = (double)(float)((double)*(float*)(ent + 100) - (double)*(float*)(pRec + 0xb4));
                double ddy = (double)(*(float*)(ent + 0x68) - *(float*)(pRec + 0xb8));
                double ddz = (double)(*(float*)(ent + 0x6c) - *(float*)(pRec + 0xbc));
                double d2 = (double)(float)(ddz*ddz + (double)(float)(ddx*ddx + (double)(float)(ddy*ddy)));
                if ((double)FLOAT_8061972c <= d2) {
                    attackContact = false;
                } else {
                    if ((double)FLOAT_80619560 < d2) {
                        double inv = (double)(float)((double)FLOAT_806195e8 / math_sqrt(d2));
                        *(float*)(ent + 100)  = (float)(ddx*inv) + *(float*)(DAT_80611d78 + pOff + 0xb4);
                        *(float*)(ent + 0x68) = (float)(ddy*inv) + *(float*)(DAT_80611d78 + pOff + 0xb8);
                        *(float*)(ent + 0x6c) = (float)(ddz*inv) + *(float*)(DAT_80611d78 + pOff + 0xbc);
                    }
                    attackContact = true;
                }
            } else if (state == 6) {
                *(int*)(slot + 0x4a54) = 0;
                *(float*)(ent + 100)  = resetX;
                *(float*)(ent + 0x68) = FLOAT_80619734;
                *(float*)(ent + 0x6c) = FLOAT_80619738;
                *(float*)(ent + 0x1cc) = FLOAT_80619578;
                int e2 = *(int*)(DAT_80612a58 + off + 0x4a30);
                *(unsigned int*)(e2 + 0x28) &= 0xfffffffe;
                anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
                slotIdx++; off += 0x44;
                if (!(slotIdx < 6)) break;
                continue;
            }

            // Common post-state work (skipped for the state-6 reset which broke).
            int st2 = *(int*)(slot + 0x4a54);
            if (st2 != 6) {
                if ((st2 != 4) && (st2 != 5)) {
                    double c = math_poly_trig((double)((FLOAT_80619588 * *(float*)(slot + 0x4a48)) / FLOAT_8061958c - FLOAT_806196e8));
                    double s = math_poly_trig((double)((FLOAT_80619588 * *(float*)(slot + 0x4a48)) / FLOAT_8061958c));
                    *(float*)(ent + 100)  = (float)((double)*(float*)(slot + 0x4a4c) * s + (double)FLOAT_806196ec);
                    *(float*)(ent + 0x6c) = (float)(-(double)*(float*)(slot + 0x4a4c) * c + (double)FLOAT_806196f0);
                    *(float*)(slot + 19000)  = (float)((double)*(float*)(ent + 100) - px);
                    *(float*)(slot + 0x4a3c) = (float)((double)*(float*)(ent + 0x68) - py);
                    *(float*)(slot + 0x4a40) = (float)((double)*(float*)(ent + 0x6c) - pz);
                    float pit, yw;
                    dir_to_angles((float*)(slot + 19000), &pit, &yw);
                    *(float*)(ent + 0x70) = yw;
                    *(float*)(ent + 0xc4) = yw;
                    *(float*)(ent + 0xc0) = pit;
                }
                if (FLOAT_80619580 == *(float*)(ent + 0x1cc)) {
                    player_ai_explosion_spawn(0x1b, 0x17, (float*)(ent + 100), (float*)0, 0, 0, 1);
                    *(int*)(slot + 0x4a54) = 6;
                }
                int uf = anim_active_slot_userflags(*(int*)(ent + 4));
                if (uf != 0) {
                    if ((*(int*)(slot + 0x4a54) == 5) && attackContact) {
                        anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
                        int evt[6];
                        switch_slot_reset((int)evt);
                        evt[0] = *(int*)(DAT_80611d78 + *(int*)(slot + 0x4a64) * 0x141c + 0x14);
                        damage_dispatch_to_entity((double)FLOAT_80619578, (double)FLOAT_80619574,
                                                  (double)FLOAT_806195f4, 0.0, 0.0, 0.0, 0.0, 0.0, evt);
                    } else {
                        if (anim_is_clip_active(ent, 0x243) != 0) {
                            unsigned int rr = rng_counter_tick();
                            if ((float)((double)CONCAT44(0x43300000, rr) - DOUBLE_80619610) / FLOAT_806195e4 < FLOAT_80619574) {
                                (void)rng_counter_tick();
                                anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
                                goto after_anim;
                            }
                        }
                        anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
                    }
                }
            }
        after_anim:
            // Trail-emitter drag driven by the per-slot trail timer (+0x4a70).
            double timer = (double)*(float*)(slot + 0x4a70);
            if ((double)FLOAT_80619568 < timer) {
                float dt2 = FLOAT_80619580 /
                    (float)((double)CONCAT44(0x43300000, (unsigned)(DAT_806109c0 ^ 0x80000000)) - DOUBLE_80619630);
                *(float*)(slot + 0x4a70) = -(float)((double)FLOAT_806108e8 * (double)dt2 - timer);
                if ((double)*(float*)(slot + 0x4a70) < (double)FLOAT_80619568) *(float*)(slot + 0x4a70) = FLOAT_80619568;
            }
            if (*(float*)(slot + 0x4a70) <= FLOAT_80619580) {
                int hA = *(int*)(DAT_80612a58 + off + 0x4a68);
                particle_cached_set_drag((double)*(float*)(slot + 0x4a70), &hA);
                int hB = *(int*)(DAT_80612a58 + off + 0x4a6c);
                particle_cached_set_drag((double)*(float*)(slot + 0x4a70), &hB);
            } else {
                int hA = *(int*)(DAT_80612a58 + off + 0x4a68);
                particle_cached_set_drag((double)FLOAT_80619624, &hA);
                int hB = *(int*)(DAT_80612a58 + off + 0x4a6c);
                particle_cached_set_drag((double)FLOAT_80619624, &hB);
            }
        }
        slotIdx++;
        off += 0x44;
    } while (slotIdx < 6);
    __restore_gpr_25();
    (void)uVar6;
}

// ===========================================================================
// 0x80292cb4  snake_boss_idle_tick
// Light pre-spawn / idle tick: handle the pending-state swap and charge accel
// like the main FSM, and when the state word is a low non-1 value reset the
// boss position and enter state 1 (spawn).
// ===========================================================================
void snake_boss_idle_tick(int actor) {
    int rec = *(int*)(actor + 0x7c);
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
        *(float*)(rec + 0xa90) = FLOAT_806108ec *
            (FLOAT_80619580 / (float)((double)CONCAT44(0x43300000, (unsigned)(DAT_806109c0 ^ 0x80000000)) - DOUBLE_80619630))
            + *(float*)(rec + 0xa90);
    }
    int state = *(int*)(rec + 0x20);
    if ((state != 1) && (state < 1) && (-1 < state)) {
        entity_set_render_type(actor, (void*)&LAB_80292ca4);
        *(float*)(actor + 100)  = FLOAT_80619730;
        *(float*)(actor + 0x68) = FLOAT_8061973c;
        *(float*)(actor + 0x6c) = FLOAT_80619738;
        *(float*)(actor + 0x1cc) = FLOAT_80619578;
        *(unsigned int*)(actor + 0x28) &= 0xffffbf9f;
        anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
        if (*(int*)(rec + 0x20) != 1) {
            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
            *(int*)(rec + 0x20)  = 1;
            *(float*)(rec + 0x9d8) = FLOAT_80619568;
            *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
        }
    }
}

// ===========================================================================
// 0x80292e14  snake_any_player_airborne
// Return 1 if any active player's Y velocity (+0x68) exceeds a threshold.
// ===========================================================================
int snake_any_player_airborne(void) {
    int off = 0;
    for (int i = 0; i < DAT_80612610; i++) {
        if (FLOAT_80619740 < *(float*)(*(int*)(DAT_80611d78 + off + 0x14) + 0x68)) {
            return 1;
        }
        off += 0x141c;
    }
    return 0;
}

// ===========================================================================
// 0x80292e5c  snake_lift_audio_state
// Lift/platform descent audio state machine (state at base+0x4be4). Stages 1-3
// each lower the linked entity's Y toward a target while playing start/stop loop
// sounds (0x5e3 stop / 0x5e4 start); a sub-branch plays a player-relative bark
// (0x977 co-op / 0x956 solo) and requests state 0xc; stage 5 waits for the fill
// meter to reach 1.0 or any player airborne before advancing to stage 6.
// ===========================================================================
void snake_lift_audio_state(int actor) {
    int stage = *(int*)(DAT_80612a58 + 0x4be4);
    if (stage <= 0) return;

    if (*(int*)(actor + 0x20) < 0xe) {
        // Three descent stages sharing the same structure (target y varies).
        const struct { float gate; float targetY; int nextStage; } S3[3] = {
            {FLOAT_806195c0, FLOAT_80619744, 2},
            {FLOAT_80619584, FLOAT_80619748, 3},
            {FLOAT_8061974c, FLOAT_80619750, 4},
        };
        int params[6];
        if (stage >= 1 && stage <= 3) {
            const float gate = S3[stage-1].gate;
            const float targetY = S3[stage-1].targetY;
            const int nextStage = S3[stage-1].nextStage;
            int linked = *(int*)(DAT_80612a58 + 0x4bd8);
            if (*(float*)(DAT_80612a58 + 0x423c) <= gate) {
                if (*(int*)(DAT_80612a58 + 0x4be8) != 0) {
                    unsigned int* wp = entity_get_world_position(linked);
                    sound_init_play_params((int)params);
                    params[0] = 0x5e3;
                    *(float*)&params[1] = FLOAT_80619578;   // volume
                    *(float*)&params[5] = FLOAT_806195d4;   // range (local_3c-style tail field)
                    params[2] = (int)wp[0];
                    params[3] = (int)wp[1];
                    params[4] = (int)wp[2];
                    sound_play_request(params);
                    *(int*)(DAT_80612a58 + 0x4be8) = 0;
                }
            } else {
                if (*(int*)(DAT_80612a58 + 0x4be8) == 0) {
                    unsigned int* wp = entity_get_world_position(linked);
                    sound_init_play_params((int)params);
                    params[0] = 0x5e4;
                    *(float*)&params[1] = FLOAT_80619578;
                    *(float*)&params[5] = FLOAT_806195d4;
                    params[2] = (int)wp[0];
                    params[3] = (int)wp[1];
                    params[4] = (int)wp[2];
                    sound_play_request(params);
                    *(int*)(DAT_80612a58 + 0x4be8) = 1;
                }
                float y = *(float*)(linked + 0x68);
                if (y <= targetY) {
                    *(int*)(DAT_80612a58 + 0x4be4) = nextStage;
                } else {
                    *(float*)(linked + 0x68) = y - FLOAT_806195a4;
                    if (*(float*)(linked + 0x68) < targetY) *(float*)(linked + 0x68) = targetY;
                }
            }
        } else if (*(int*)(actor + 0x20) < 0xc) {
            int spk = *(int*)(DAT_80612a58 + 0x4bd4);
            if (spk != 0) {
                if ((DAT_805015bd == '\n') && (1 < DAT_80612610)) {
                    sound_init_play_params((int)params);
                    params[0] = 0x977;
                    *(float*)&params[1] = FLOAT_80619578;
                    *(float*)&params[5] = FLOAT_806195bc;
                    params[2] = *(int*)(spk + 100);
                    params[3] = *(int*)(spk + 0x68);
                    params[4] = *(int*)(spk + 0x6c);
                    sound_play_request(params);
                } else {
                    sound_init_play_params((int)params);
                    params[0] = 0x956;
                    *(float*)&params[1] = FLOAT_80619578;
                    *(float*)&params[5] = FLOAT_806195bc;
                    params[2] = *(int*)(spk + 100);
                    params[3] = *(int*)(spk + 0x68);
                    params[4] = *(int*)(spk + 0x6c);
                    sound_play_request(params);
                }
            }
            if (*(int*)(actor + 0x20) != 0xc) {
                *(int*)(actor + 0x9d0) = 0xc;
            }
        }
    } else if (stage == 5) {
        if ((FLOAT_80619580 <= *(float*)(DAT_80612a58 + 0x4bec)) || (snake_any_player_airborne() != 0)) {
            *(int*)(DAT_80612a58 + 0x4be4) = 6;
        } else {
            *(float*)(DAT_80612a58 + 0x4bec) += FLOAT_80619654;
        }
    }
}

// ===========================================================================
// 0x80293390  snake_render_gx_rings
// Direct-GX render of the boss ring/aura: streams textured triangle-strip quads
// to the CP write-gather FIFO (0xCC008000). A central column strip is emitted
// first, then (when the actor count > 0) four surrounding strips offset in X/Z.
// Vertex format is pos.x/pos.y/pos.z + a packed color/uv word (0x06060664).
// ===========================================================================
void snake_render_gx_rings(void) {
    unsigned long long uVar13 = (unsigned long long)FUN_803826e4();
    float* p = (float*)(unsigned)(uVar13 >> 0x20);
    float y0 = FLOAT_80619580 + p[1];
    float z0 = FLOAT_80619580 + p[2];
    float y1 = y0;   // (double)(dVar9 + extraout_f1) collapses to y0 in practice
    float half = FLOAT_806195a4;

    gx_begin_textured_batch(2, 0x81, 0, 10, 0);
    gx_fifo_f(half + p[0]); gx_fifo_f(y0); gx_fifo_f(z0); gx_fifo_u(0x6060664);
    gx_fifo_f(half + p[0]); gx_fifo_f(y1); gx_fifo_f(z0); gx_fifo_u(0x6060664);
    float zc = half + z0;
    gx_fifo_f(p[0]); gx_fifo_f(y0); gx_fifo_f(zc); gx_fifo_u(0x6060664);
    gx_fifo_f(p[0]); gx_fifo_f(y1); gx_fifo_f(zc); gx_fifo_u(0x6060664);
    gx_fifo_f(p[0] - half); gx_fifo_f(y0); gx_fifo_f(z0); gx_fifo_u(0x6060664);
    gx_fifo_f(p[0] - half); gx_fifo_f(y1); gx_fifo_f(z0); gx_fifo_u(0x6060664);
    gx_fifo_f(p[0]); gx_fifo_f(y0); gx_fifo_f(z0 - half); gx_fifo_u(0x6060664);
    gx_fifo_f(p[0]); gx_fifo_f(y1); gx_fifo_f(z0 - half); gx_fifo_u(0x6060664);
    gx_fifo_f(half + p[0]); gx_fifo_f(y0); gx_fifo_f(z0); gx_fifo_u(0x6060664);
    gx_fifo_f(half + p[0]); gx_fifo_f(y1); gx_fifo_f(z0); gx_fifo_u(0x6060664);
    gx_end_batch();

    if (0 < (int)uVar13) {
        float ring[5];
        ring[0] = FLOAT_8061956c + p[0];
        ring[1] = y0 + FLOAT_80619648;
        ring[2] = ring[0];
        ring[3] = z0 - FLOAT_80619648;   // note: index reuse below matches original
        ring[4] = p[0] - FLOAT_8061956c;
        float local_84 = ring[3];
        float local_80 = ring[4];
        float local_7c = ring[1];
        float z7 = z0 - half;
        float y10 = FLOAT_80619758 + p[1];
        (void)local_84; (void)local_80; (void)local_7c;

        int idx = 0;
        int o = 0;
        do {
            gx_begin_textured_batch(2, 0x81, 0, 10, 0);
            float fA = *(float*)((int)ring + o);
            float fB = *(float*)((int)ring + o + 4);
            float fAplus = half + fA;
            gx_fifo_f(fAplus); gx_fifo_f(y10); gx_fifo_f(fB); gx_fifo_u(0x6060664);
            gx_fifo_f(half + p[0]); gx_fifo_f(y0); gx_fifo_f(z0); gx_fifo_u(0x6060664);
            gx_fifo_f(fA); gx_fifo_f(y10); gx_fifo_f(half + fB); gx_fifo_u(0x6060664);
            gx_fifo_f(p[0]); gx_fifo_f(y0); gx_fifo_f(z7); gx_fifo_u(0x6060664);
            gx_fifo_f(fA - half); gx_fifo_f(y10); gx_fifo_f(fB); gx_fifo_u(0x6060664);
            gx_fifo_f(p[0] - half); gx_fifo_f(y0); gx_fifo_f(z0); gx_fifo_u(0x6060664);
            gx_fifo_f(fA); gx_fifo_f(y10); gx_fifo_f(fB - half); gx_fifo_u(0x6060664);
            gx_fifo_f(p[0]); gx_fifo_f(y0); gx_fifo_f(z0 - half); gx_fifo_u(0x6060664);
            gx_fifo_f(fAplus); gx_fifo_f(y10); gx_fifo_f(fB); gx_fifo_u(0x6060664);
            gx_fifo_f(half + p[0]); gx_fifo_f(y0); gx_fifo_f(z0); gx_fifo_u(0x6060664);
            gx_end_batch();
            idx++;
            o += 8;
        } while (idx < 4);
    }
    FUN_80382730();
}
