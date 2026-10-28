// boss3_combat.cpp - per-sub-unit attack update, projectile/particle spawning,
// aim solving and the boss3 damage/hit dispatch (control block DAT_80612a48; see
// boss3_tentacle.cpp / boss3_surface.cpp / boss3_control.cpp for the rest).
//
// This module drives the boss's offensive behaviour and how it takes damage:
//   * boss3_subunit_update runs each sub-unit's 8-state swing/attack FSM (state
//     at +0x4c) that ramps an attack angle (+0x40) and drives an inner
//     oscillation sub-state (the +0x50 record), then aim-tracks the boss centre.
//   * boss3_subunit_fire emits trail particles and, when firing, a projectile
//     (+ a sound) with rng-jittered spawn offsets.
//   * boss3_update_subunits gates the above by boss state (+0x58c).
//   * boss3_compute_segment_aim / boss3_turn_toward_aim / boss3_update_aim_angles
//     convert the active angle-segment into an aim point (+0xd8..+0xe0) and turn
//     the boss toward it.
//   * boss3_process_explosions fires queued part explosions.
//   * boss3_projectile_hit / boss3_projectile_contact are the weapon-contact
//     callbacks: absorb into the energy surface, damage a boss part, spawn an
//     explosion, reduce HP (+0x580), trigger hurt/death anim+sound, or reflect.
//
// NOTE on the first two functions: the original passes the current sub-unit
// context in registers (the decompiler models this via the frame-enter
// intrinsics FUN_803826f0 / __save_gpr_28 and an incoming r6). We keep those
// reads to stay faithful to the data flow; the 'mode' of boss3_subunit_fire is
// the incoming r6 selector, surfaced as a parameter (non-matching).
//
// Address range: 0x8029f3f0 - 0x802a0afb
// Subsystem: game (boss AI / combat)

#include "common.h"

extern "C" {

// --- data globals ----------------------------------------------------------
extern int   DAT_80612a48;            // boss3 control block base (byte-addressed)
extern int   DAT_806109c0;            // frame-delta ticks (xor bias)
extern const float DAT_803e079c, DAT_803e07a0, DAT_803e07a4;  // reflection basis vec

// --- float / double constants ----------------------------------------------
extern const float FLOAT_80619560, FLOAT_80619568, FLOAT_80619570, FLOAT_80619574;
extern const float FLOAT_80619578, FLOAT_80619580, FLOAT_80619584, FLOAT_80619588;
extern const float FLOAT_8061958c, FLOAT_8061959c, FLOAT_806195a0, FLOAT_806195a4;
extern const float FLOAT_806195a8, FLOAT_806195ac, FLOAT_806195b0, FLOAT_806195b8;
extern const float FLOAT_806195c4, FLOAT_806195e4, FLOAT_806195e8, FLOAT_806195f0;
extern const float FLOAT_806195f4, FLOAT_80619600, FLOAT_80619604, FLOAT_80619624;
extern const float FLOAT_8061963c, FLOAT_80619648, FLOAT_806196e4, FLOAT_80619700;
extern const float FLOAT_8061974c, FLOAT_80619758, FLOAT_80619858, FLOAT_8061985c;
extern const float FLOAT_80619860, FLOAT_8061986c, FLOAT_80619870, FLOAT_80619874;
extern const float FLOAT_80619878, FLOAT_806108e8;
extern const double DOUBLE_80619610;  // rng bias
extern const double DOUBLE_80619630;  // frame-delta bias

// --- raw frame-enter intrinsics (register save/restore) --------------------
extern int                FUN_803826f0(void);   // fp frame enter -> ctx ptr
extern void               FUN_8038273c(void);   // fp frame leave
extern unsigned long long __save_gpr_28(void);  // gpr frame enter (ctx pair)
extern void               __restore_gpr_28(void);
extern int                FUN_803826e8(void);    // fp frame enter (projectile ctx)
extern void               FUN_80382734(void);    // fp frame leave

// --- named cross-module helpers (call-site typed) --------------------------
extern double math_sqrt(double x);
extern double math_atan2(double y, double x);
extern void   angles_to_dir(double yaw, double pitch, float* outVec);
extern void   dir_to_angles(void* vec, void* outYaw, float* outPitch);
extern void   mtx4_transform_vector(float* mtx, float* vec);
extern void   snake3_attractor_nudge(float* rec, int center, int targetPos, int enabled);
extern unsigned int rng_counter_tick(void);
extern unsigned int player_ai_get_active_index(void);
extern void   player_ai_spawn_trail_particle(int a, int b, int c, float* pos, float* dir, int d);
extern void   player_ai_projectile_spawn_simple(double speed, double grav, int type, float* pos,
                                                int* dir, int entity, int* actorState, int a,
                                                int b, int room, int projId, int c);
extern void   player_ai_projectile_ricochet(int entity, int projPtr, float* p3, float* p4,
                                            int type, int flag);
extern int    player_ai_explosion_spawn(int a, int type, float* pos, float* dir, int owner,
                                        int source, int flag);
extern int    statlvl_class_is_state5(int classId);
extern int    anim_play_scaled(double startPhase, double rate);
extern void   sound_init_play_params(int params);
extern int    sound_play_request(int* params);

} // extern "C"

// boss3 helpers defined elsewhere in this module (C++ linkage)
void boss3_tentacle_try_grab(int target);            // boss3_surface.cpp
int  boss3_surface_receive_hit(float* pt);           // boss3_surface.cpp
bool boss3_point_in_facing_cone(float* pt);          // boss3_tentacle.cpp

// normalized rng jitter in [0,1): (rng_tick - bias) / scale, matching the
// CONCAT44 magic-double reconstruction Ghidra emits at each spawn site.
static inline float rng_counter_tick_scaled(void) {
    unsigned int t = rng_counter_tick();
    return (float)((double)CONCAT44(0x43300000, t) - DOUBLE_80619610) / FLOAT_806195e4;
}

// same-module forward decls
static void boss3_subunit_update(void);
static void boss3_subunit_fire(int mode);
static bool boss3_points_within_range(float* a, float* b);
static void boss3_projectile_hit(void);

// ===========================================================================
// 0x8029f3f0  boss3_subunit_update
// Per-sub-unit attack update. Runs the 8-state swing FSM at ctx+0x4c: state 1..6
// ramp/hold the attack angle (ctx+0x40/+0x48) between limits then advance; state
// 7 measures a distance; the default state (0) aim-tracks the boss centre and
// runs an inner oscillation sub-state via the +0x50 sub-record (pfVar6[3..8]).
// Context ptr arrives in registers (frame-enter intrinsic).
// ===========================================================================
static void boss3_subunit_update(void) {
    int ctx = FUN_803826f0();
    float dtInv = FLOAT_80619580 /
                  (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80619630);
    double dt = (double)(FLOAT_80619584 * FLOAT_806108e8 * dtInv);
    float* sub = (float*)(ctx + *(int*)(ctx + 0x50) * 0x28 + 0xb4);

    if (FLOAT_80619568 < *(float*)(ctx + 0x84)) {
        snake3_attractor_nudge((float*)(ctx + 0x94), DAT_80612a48 + 0xcc, ctx + 0x78, 1);
    }

    float full = FLOAT_80619580;
    float zero = FLOAT_80619568;
    int state = *(int*)(ctx + 0x4c);
    if (state == 1) {
        *(float*)(ctx + 0x24) = (float)((double)FLOAT_80619560 * dt + (double)*(float*)(ctx + 0x24));
        if (full < *(float*)(ctx + 0x24)) {
            *(float*)(ctx + 0x24) = FLOAT_80619568;
            *(int*)(ctx + 0x4c) = 2;
        }
    } else if (state == 2) {
        if (*(float*)(ctx + 0x48) < FLOAT_806195f4)
            *(float*)(ctx + 0x48) = *(float*)(ctx + 0x48) + FLOAT_80619580;
        *(float*)(ctx + 0x40) = (float)((double)*(float*)(ctx + 0x48) * dt + (double)*(float*)(ctx + 0x40));
        if (FLOAT_806195c4 < *(float*)(ctx + 0x40))
            *(int*)(ctx + 0x4c) = 3;
    } else if (state == 3) {
        if (FLOAT_8061986c < *(float*)(ctx + 0x48))
            *(float*)(ctx + 0x48) = *(float*)(ctx + 0x48) - FLOAT_80619624;
        *(float*)(ctx + 0x40) = (float)((double)*(float*)(ctx + 0x48) * dt + (double)*(float*)(ctx + 0x40));
        if (*(float*)(ctx + 0x40) < FLOAT_80619584)
            *(int*)(ctx + 0x4c) = 4;
    } else if (state == 4) {
        if (*(float*)(ctx + 0x48) < FLOAT_806195b8)
            *(float*)(ctx + 0x48) = *(float*)(ctx + 0x48) + FLOAT_80619624;
        *(float*)(ctx + 0x40) = (float)((double)*(float*)(ctx + 0x48) * dt + (double)*(float*)(ctx + 0x40));
        if (FLOAT_80619870 < *(float*)(ctx + 0x40))
            *(int*)(ctx + 0x4c) = 5;
    } else if (state == 5) {
        if (FLOAT_806195f0 < *(float*)(ctx + 0x48))
            *(float*)(ctx + 0x48) = *(float*)(ctx + 0x48) - FLOAT_80619624;
        *(float*)(ctx + 0x40) = (float)((double)*(float*)(ctx + 0x48) * dt + (double)*(float*)(ctx + 0x40));
        if (*(float*)(ctx + 0x40) < FLOAT_80619568)
            *(int*)(ctx + 0x4c) = 6;
    } else if (state == 6) {
        *(float*)(ctx + 0x24) = (float)((double)FLOAT_80619700 * dt + (double)*(float*)(ctx + 0x24));
        if (full < *(float*)(ctx + 0x24)) {
            *(float*)(ctx + 0x40) = FLOAT_80619568;
            sub[5] = sub[6];
            *(int*)(ctx + 0x4c) = 0;
            *(float*)(ctx + 0x24) = FLOAT_80619568;
        }
    } else if (state == 7) {
        float dx = *(float*)(DAT_80612a48 + 0x18c) - *(float*)(DAT_80612a48 + 0x148);
        float dy = *(float*)(DAT_80612a48 + 0x188) - *(float*)(DAT_80612a48 + 0x144);
        float dz = *(float*)(DAT_80612a48 + 0x190) - *(float*)(DAT_80612a48 + 0x14c);
        math_sqrt((double)(dz * dz + dy * dy + dx * dx));
    } else {
        float local_angle;
        if ((0 < (int)sub[6]) && ((int)sub[5] < 1)) {
            *(float*)(ctx + 0x24) = FLOAT_80619568;
            *(int*)(ctx + 0x4c) = 1;
            *(float*)(ctx + 0x40) = zero;
        }
        if (FLOAT_806196e4 * *(float*)(DAT_80612a48 + 0x5a4) <= ABS(*(float*)(DAT_80612a48 + 0xe4))) {
            if (sub[8] == 1.4013e-45f) {   // sub-state flag == 1 (raw bits)
                *(float*)(ctx + 0x24) =
                    *(float*)(DAT_80612a48 + 0x5a4) * (float)((double)sub[0] * dt) + *(float*)(ctx + 0x24);
            }
            local_angle = FLOAT_80619568;
            *(int*)(DAT_80612a48 + 0x59c) = 0;
        } else {
            *(float*)(ctx + 0xc) = *(float*)(DAT_80612a48 + 0xcc) - *(float*)(ctx + 0x78);
            *(float*)(ctx + 0x10) = *(float*)(DAT_80612a48 + 0xd0) - *(float*)(ctx + 0x7c);
            *(float*)(ctx + 0x14) = *(float*)(DAT_80612a48 + 0xd4) - *(float*)(ctx + 0x80);
            double len = math_sqrt((double)(*(float*)(ctx + 0x14) * *(float*)(ctx + 0x14) +
                                            *(float*)(ctx + 0xc) * *(float*)(ctx + 0xc) +
                                            *(float*)(ctx + 0x10) * *(float*)(ctx + 0x10)));
            float inv = (float)((double)FLOAT_80619580 / len);
            *(float*)(ctx + 0xc) = *(float*)(ctx + 0xc) * inv;
            *(float*)(ctx + 0x10) = *(float*)(ctx + 0x10) * inv;
            *(float*)(ctx + 0x14) = *(float*)(ctx + 0x14) * inv;
            if (*(int*)(ctx + 0x4c) == 0) {
                dir_to_angles((void*)(ctx + 0xc), 0, &local_angle);
                if (local_angle < FLOAT_80619568)
                    local_angle = local_angle + FLOAT_806195ac;
                for (local_angle = local_angle - *(float*)(DAT_80612a48 + 0xf4);
                     local_angle <= FLOAT_806195b0; local_angle = local_angle + FLOAT_806195ac) {}
                for (; FLOAT_8061958c < local_angle; local_angle = local_angle - FLOAT_806195ac) {}
            } else {
                local_angle = FLOAT_80619568;
            }
            *(int*)(DAT_80612a48 + 0x59c) = 1;
        }

        // step ctx+0x18 toward local_angle at a fixed rate, clamped to ctx+0x20/+0x1c
        float cur = *(float*)(ctx + 0x18);
        if (local_angle <= cur) {
            if ((local_angle < cur) &&
                (*(float*)(ctx + 0x18) = cur - FLOAT_80619580, *(float*)(ctx + 0x18) < local_angle))
                *(float*)(ctx + 0x18) = local_angle;
        } else {
            *(float*)(ctx + 0x18) = cur + FLOAT_80619580;
            if (local_angle < *(float*)(ctx + 0x18))
                *(float*)(ctx + 0x18) = local_angle;
        }
        float lim = *(float*)(ctx + 0x20) * *(float*)(DAT_80612a48 + 0x5a4);
        if (lim <= *(float*)(ctx + 0x18)) {
            lim = *(float*)(ctx + 0x1c) * *(float*)(DAT_80612a48 + 0x5a4);
            if (lim < *(float*)(ctx + 0x18))
                *(float*)(ctx + 0x18) = lim;
        } else {
            *(float*)(ctx + 0x18) = lim;
        }

        // inner oscillation sub-state (sub[8]: 2==swing, 1==push, 0==idle) (raw bits)
        if (sub[8] == 2.8026e-45f) {
            if ((sub[4] == 0.0f) && (*(int*)(DAT_80612a48 + 0x59c) != 0) &&
                (ABS(*(float*)(ctx + 0x18) - local_angle) < FLOAT_806195b8)) {
                sub[4] = 1.4013e-45f;
            }
            if (sub[4] == 1.4013e-45f) {
                *(float*)(ctx + 0x24) =
                    *(float*)(DAT_80612a48 + 0x5a8) * sub[0] * FLOAT_806108e8 + *(float*)(ctx + 0x24);
                sub[3] = FLOAT_806195a4 * FLOAT_806108e8 + sub[3];
                if (sub[1] < sub[3]) {
                    sub[4] = 2.8026e-45f;
                    sub[3] = FLOAT_80619568;
                }
            } else {
                *(float*)(ctx + 0x24) =
                    -(float)((double)(FLOAT_80619758 * sub[0]) * dt - (double)*(float*)(ctx + 0x24));
                if (*(float*)(ctx + 0x24) < FLOAT_80619568)
                    *(float*)(ctx + 0x24) = FLOAT_80619568;
                if (sub[4] == 2.8026e-45f) {
                    sub[3] = FLOAT_806195a8 * FLOAT_806108e8 + sub[3];
                    if (sub[2] < sub[3]) {
                        sub[4] = 0.0f;
                        sub[3] = FLOAT_80619568;
                    }
                }
            }
        } else if (sub[8] == 1.4013e-45f) {
            *(float*)(ctx + 0x24) = sub[0] * FLOAT_806108e8 + *(float*)(ctx + 0x24);
        }
    }

    if ((*(int*)(ctx + 0x8c) == 0) &&
        (*(float*)(ctx + 0x24) = FLOAT_80619568, *(int*)(ctx + 0x90) == 0)) {
        boss3_tentacle_try_grab(ctx + 0x54);
    }
    FUN_8038273c();
}

// ===========================================================================
// 0x8029fa50  boss3_subunit_fire
// Emit a sub-unit's trail particles and, when its cooldown (ctx+0x24 slot 9) is
// up, spawn a projectile (+ a sound). 'mode' is the incoming r6 selector that
// picks between the two spawn-offset formulas. Context arrives in registers.
// ===========================================================================
static void boss3_subunit_fire(int mode) {
    unsigned long long pair = __save_gpr_28();
    float* p = (float*)(unsigned int)(pair >> 32);
    int ctx = (int)pair;
    float which = p[0x14];
    float spawn[3];
    int params[7];

    if (p[0x13] == 0.0f) {
        if (p[9] <= FLOAT_80619580) {
            if ((p[(int)which * 10 + 0x34] == 0.0f) && (p[(int)which * 10 + 0x35] == 0.0f)) {
                float jx = rng_counter_tick_scaled();
                float jy = rng_counter_tick_scaled();
                float jz = rng_counter_tick_scaled();
                spawn[0] = (FLOAT_806195a8 * jx + p[0]) - FLOAT_806195a4;
                spawn[1] = (FLOAT_806195a8 * jy + p[1]) - FLOAT_806195a4;
                spawn[2] = (FLOAT_806195a8 * jz + p[2]) - FLOAT_806195a4;
                player_ai_spawn_trail_particle(0, 0, 0xffffffff, p + 0x1e, spawn, -1);
            }
        } else {
            int* actorState = *(int**)(ctx + 0x7c);
            if (mode == 0) {
                spawn[0] = (FLOAT_806195a8 * rng_counter_tick_scaled() + p[0]) - FLOAT_806195a4;
                spawn[1] = (FLOAT_80619874 * rng_counter_tick_scaled() + p[1]) - FLOAT_80619560;
                spawn[2] = (FLOAT_806195a8 * rng_counter_tick_scaled() + p[2]) - FLOAT_806195a4;
            } else {
                spawn[0] = (FLOAT_80619574 * (p[0] + p[3]) +
                            FLOAT_806195a8 * rng_counter_tick_scaled()) - FLOAT_806195a4;
                spawn[1] = (FLOAT_80619574 * (p[1] + p[4]) +
                            FLOAT_80619874 * rng_counter_tick_scaled()) - FLOAT_80619560;
                spawn[2] = (FLOAT_80619574 * (p[2] + p[5]) +
                            FLOAT_806195a8 * rng_counter_tick_scaled()) - FLOAT_806195a4;
            }
            if (p[(int)which * 10 + 0x35] == p[(int)which * 10 + 0x34]) {
                player_ai_spawn_trail_particle(0, 0, 0xffffffff, p + 0x1e, spawn, -1);
            }
            sound_init_play_params((int)params);
            *(unsigned char*)&params[3] = 1;   // local_30 (offset into the param block)
            params[0] = 0x38e;
            *(float*)&params[4] = FLOAT_80619648;
            *(float*)&params[5] = p[0x1e];
            *(float*)&params[6] = p[0x1f];
            // NOTE: original writes local_34 = p[0x20] into the param tail before request
            sound_play_request(params);
            player_ai_projectile_spawn_simple((double)FLOAT_8061959c, (double)FLOAT_806195a8,
                (int)p[(int)which * 10 + 0x36], p + 0x1e, (int*)spawn, ctx, actorState, 6, 0,
                *(int*)(ctx + 0x34), 0x10000a, 2);
            p[9] = FLOAT_80619568;
            p[(int)which * 10 + 0x32] = (float)((int)p[(int)which * 10 + 0x32] - 1);
        }
    }
    __restore_gpr_28();
}

// ===========================================================================
// 0x8029fe0c  boss3_update_subunits
// Unless the boss is in a locked state (+0x58c in {0x11,0x12,0x14}), run the
// update+fire pass for the two active sub-units.
// ===========================================================================
void boss3_update_subunits(void) {
    int st = *(int*)(DAT_80612a48 + 0x58c);
    if ((st != 0x11) && (st != 0x12) && (st != 0x14)) {
        boss3_subunit_update();
        boss3_subunit_fire(0);
        boss3_subunit_update();
        boss3_subunit_fire(0);
    }
}

// ===========================================================================
// 0x8029fe98  boss3_compute_segment_aim
// Turn the active angle-segment (+0x90 -> table +0x0) into a world aim point and
// store it at +0xd8/+0xdc/+0xe0 (radius = sqrt(FLOAT_80619878)).
// ===========================================================================
void boss3_compute_segment_aim(void) {
    int off = *(int*)(DAT_80612a48 + 0x90) * 8;
    unsigned int packed = *(unsigned int*)(DAT_80612a48 + off) +
                          *(unsigned int*)(DAT_80612a48 + off + 4);
    double yaw = (double)(FLOAT_80619574 *
                          (float)((double)CONCAT44(0x43300000, packed ^ 0x80000000) - DOUBLE_80619630));
    double radius = math_sqrt((double)FLOAT_80619878);
    float dir[3];
    angles_to_dir((double)FLOAT_80619568, yaw, dir);
    *(float*)(DAT_80612a48 + 0xd8) = -(float)(radius * (double)dir[0] - (double)FLOAT_80619858);
    *(float*)(DAT_80612a48 + 0xdc) = -(float)(radius * (double)dir[1] - (double)FLOAT_8061985c);
    *(float*)(DAT_80612a48 + 0xe0) = -(float)(radius * (double)dir[2] - (double)FLOAT_80619860);
}

// ===========================================================================
// 0x8029ff6c  boss3_turn_toward_aim
// Turn angle *param_2 toward the boss's aim target (+0xd8/+0xe0 vs +0x9c/+0xa4)
// using atan2, stepping at a fixed rate. Returns true once the remaining delta
// is within 'tolerance'.
// ===========================================================================
bool boss3_turn_toward_aim(double tolerance, float* pAngle) {
    double a = math_atan2((double)(*(float*)(DAT_80612a48 + 0xd8) - *(float*)(DAT_80612a48 + 0x9c)),
                          (double)(*(float*)(DAT_80612a48 + 0xe0) - *(float*)(DAT_80612a48 + 0xa4)));
    float deg = (FLOAT_8061958c * (float)a) / FLOAT_80619588;
    if (deg < FLOAT_80619568)
        deg = deg + FLOAT_806195ac;
    deg = deg - *pAngle;
    double d = (double)deg;
    while (d <= (double)FLOAT_806195b0)
        d = (double)(float)(d + (double)FLOAT_806195ac);
    for (; (double)FLOAT_8061958c < d; d = (double)(float)(d - (double)FLOAT_806195ac)) {}
    double step = (double)FLOAT_80619580;
    if (d <= step) {
        if ((double)FLOAT_80619570 <= d)
            *pAngle = (float)((double)*pAngle + d);
        else
            *pAngle = (float)((double)*pAngle - step);
    } else {
        *pAngle = (float)((double)*pAngle + step);
    }
    return ABS(d) < tolerance;
}

// ===========================================================================
// 0x802a0080  boss3_update_aim_angles
// From the pattern base point (+0x84/+0x88/+0x8c) relative to the facing basis,
// compute squared distances (+0x104/+0x108/+0x10c) and two derived aim angles
// (+0xe8/+0xec, +0xf0). param_1+0xaf4 supplies an external radius term.
// ===========================================================================
void boss3_update_aim_angles(int param_1) {
    float v[3];   // v[0..2] = local_18/local_14/local_10 (primary delta)
    float w[3];   // w[0..2] = local_24/local_20/local_1c (secondary delta)
    float ay = *(float*)(DAT_80612a48 + 0xa0) - *(float*)(DAT_80612a48 + 0xc4);
    v[0] = *(float*)(DAT_80612a48 + 0x84) - *(float*)(DAT_80612a48 + 0x9c);
    v[1] = *(float*)(DAT_80612a48 + 0x88) - *(float*)(DAT_80612a48 + 0xa0);
    v[2] = *(float*)(DAT_80612a48 + 0x8c) - *(float*)(DAT_80612a48 + 0xa4);
    w[0] = *(float*)(DAT_80612a48 + 0x84) - *(float*)(DAT_80612a48 + 0xc0);
    w[1] = *(float*)(DAT_80612a48 + 0x88) - *(float*)(DAT_80612a48 + 0xc4);
    w[2] = *(float*)(DAT_80612a48 + 0x8c) - *(float*)(DAT_80612a48 + 0xc8);

    *(float*)(DAT_80612a48 + 0x108) = v[0] * v[0] + v[2] * v[2];
    *(float*)(DAT_80612a48 + 0x10c) = w[0] * w[0] + w[2] * w[2];
    *(float*)(DAT_80612a48 + 0x104) = -(ay * ay - *(float*)(param_1 + 0xaf4));

    float ang;
    dir_to_angles(v, 0, &ang);
    if (ang < FLOAT_80619568)
        ang = ang + FLOAT_806195ac;
    *(float*)(DAT_80612a48 + 0xec) = ang;
    float wrapped;
    for (wrapped = ang - *(float*)(DAT_80612a48 + 0xf4); wrapped <= FLOAT_806195b0;
         wrapped = wrapped + FLOAT_806195ac) {}
    for (; FLOAT_8061958c < wrapped; wrapped = wrapped - FLOAT_806195ac) {}
    *(float*)(DAT_80612a48 + 0xe8) = wrapped;

    dir_to_angles(w, 0, &ang);
    if (ang < FLOAT_80619568)
        ang = ang + FLOAT_806195ac;
    *(float*)(DAT_80612a48 + 0xf0) = ang;
}

// ===========================================================================
// 0x802a01ec  boss3_points_within_range
// True when two 3D points are within FLOAT_80619604 (distance squared).
// ===========================================================================
static bool boss3_points_within_range(float* a, float* b) {
    return (b[2] - a[2]) * (b[2] - a[2]) +
           (b[0] - a[0]) * (b[0] - a[0]) +
           (b[1] - a[1]) * (b[1] - a[1]) < FLOAT_80619604;
}

// ===========================================================================
// 0x802a0230  boss3_process_explosions
// Fire and clear queued part-explosion flags for the two sub-units (+0x19c, and
// a second array +0x4cc).
// ===========================================================================
void boss3_process_explosions(void) {
    FUN_803826f0();
    int a = 0;   // stride 300 into the +0x188/+0x19c array
    int b = 0;   // stride 0x60 into the +0x4b8/+0x4cc array
    for (int i = 0; i < 2; i++) {
        if (*(int*)(DAT_80612a48 + a + 0x19c) == 1) {
            *(float*)(DAT_80612a48 + a + 0x194) = FLOAT_80619568;
            *(int*)(DAT_80612a48 + a + 0x19c) = 0;
            player_ai_explosion_spawn(1, 0x17, (float*)(DAT_80612a48 + a + 0x188), (float*)0, 0, 0, 1);
        }
        if (*(int*)(DAT_80612a48 + b + 0x4cc) == 1) {
            *(float*)(DAT_80612a48 + b + 0x4c4) = FLOAT_80619568;
            *(int*)(DAT_80612a48 + b + 0x4cc) = 0;
            player_ai_explosion_spawn(0x20, 0x17, (float*)(DAT_80612a48 + b + 0x4b8), (float*)0, 0, 0, 1);
        }
        a += 300;
        b += 0x60;
    }
    FUN_8038273c();
}

// ===========================================================================
// 0x802a0324  boss3_projectile_hit
// Weapon-contact handler: match the incoming projectile (ctx from FUN_803826e8)
// against the boss parts, apply damage timers, spawn explosions, and on a valid
// hit reduce boss HP (+0x580), trigger the hurt/death anim (states 0x12/0x14) +
// sound, then ricochet the projectile. Gated by boss state (+0x98, +0x58c).
// ===========================================================================
static void boss3_projectile_hit(void) {
    int* proj = (int*)FUN_803826e8();
    int who = (int)player_ai_get_active_index();
    float dmg = (float)proj[8];
    double dmgD = (double)dmg;
    bool hit = false;

    if ((*(char*)(DAT_80612a48 + 0x98) != '\x02') && (*(int*)(DAT_80612a48 + 0x58c) != 0x11)) {
        if (proj[6] == 0x17) {
            double radSq = (double)((float)proj[0x15] * (float)proj[0x15]);
            if (FLOAT_80619568 == *(float*)(DAT_80612a48 + 0x1434)) {
                float* pp = (float*)proj[10];
                float dy = *(float*)(DAT_80612a48 + 0x578) - pp[1];
                float dx = *(float*)(DAT_80612a48 + 0x574) - pp[0];
                float dz = *(float*)(DAT_80612a48 + 0x57c) - pp[2];
                double dd = (double)(dz * dz + dx * dx + dy * dy);
                if (dd < radSq) {
                    dd = math_sqrt(dd);
                    hit = true;
                    dmgD = (double)(float)(dmgD * (double)(FLOAT_80619600 *
                                          (float)(dd / (double)(float)proj[0x15])));
                }
            }
            int a = 0, b = 0;
            for (int i = 0; i < 2; i++) {
                int rec = DAT_80612a48 + a;
                if (*(int*)(rec + 0x19c) == 1) {
                    float* pp = (float*)proj[10];
                    float dy = *(float*)(rec + 0x18c) - pp[1];
                    float dx = *(float*)(rec + 0x188) - pp[0];
                    float dz = *(float*)(rec + 0x190) - pp[2];
                    double dd = (double)(dz * dz + dx * dx + dy * dy);
                    if (dd < radSq) {
                        dd = math_sqrt(dd);
                        int t = a + 0x194;
                        *(float*)(DAT_80612a48 + t) =
                            -(FLOAT_806195e8 * (float)proj[8] * (float)(dd / (double)(float)proj[0x15])
                              - *(float*)(DAT_80612a48 + t));
                        if (*(float*)(DAT_80612a48 + t) < FLOAT_80619568) {
                            *(float*)(DAT_80612a48 + t) = FLOAT_80619568;
                            *(int*)(DAT_80612a48 + a + 0x19c) = 0;
                            player_ai_explosion_spawn(1, 0x17, (float*)(DAT_80612a48 + a + 0x188),
                                                      (float*)0, *proj, 0, 1);
                        }
                    }
                }
                int rec2 = DAT_80612a48 + b;
                if (*(int*)(rec2 + 0x4cc) == 1) {
                    float* pp = (float*)proj[10];
                    float dy = *(float*)(rec2 + 0x4bc) - pp[1];
                    float dx = *(float*)(rec2 + 0x4b8) - pp[0];
                    float dz = *(float*)(rec2 + 0x4c0) - pp[2];
                    double dd = (double)(dz * dz + dx * dx + dy * dy);
                    if (dd < radSq) {
                        dd = math_sqrt(dd);
                        int t = b + 0x4c4;
                        *(float*)(DAT_80612a48 + t) =
                            -(FLOAT_806195e8 * (float)proj[8] * (float)(dd / (double)(float)proj[0x15])
                              - *(float*)(DAT_80612a48 + t));
                        if (*(float*)(DAT_80612a48 + t) < FLOAT_80619568) {
                            *(float*)(DAT_80612a48 + t) = FLOAT_80619568;
                            *(int*)(DAT_80612a48 + b + 0x4cc) = 0;
                            player_ai_explosion_spawn(0x20, 0x17, (float*)(DAT_80612a48 + b + 0x4b8),
                                                      (float*)0, *proj, 0, 1);
                        }
                    }
                }
                a += 300;
                b += 0x60;
            }
        } else if ((who == 0x16) && (*(int*)(DAT_80612a48 + 0x19c) == 1)) {
            *(float*)(DAT_80612a48 + 0x194) = (float)((double)*(float*)(DAT_80612a48 + 0x194) - dmgD);
            if (*(float*)(DAT_80612a48 + 0x194) < dmg) {
                *(float*)(DAT_80612a48 + 0x194) = FLOAT_80619568;
                *(int*)(DAT_80612a48 + 0x19c) = 0;
                player_ai_explosion_spawn(1, -1, (float*)(DAT_80612a48 + 0x188), (float*)0, *proj, 0, 1);
            }
        } else if ((who == 0x12) && (*(int*)(DAT_80612a48 + 0x2c8) == 1)) {
            *(float*)(DAT_80612a48 + 0x2c0) = (float)((double)*(float*)(DAT_80612a48 + 0x2c0) - dmgD);
            if (*(float*)(DAT_80612a48 + 0x2c0) < dmg) {
                *(float*)(DAT_80612a48 + 0x2c0) = FLOAT_80619568;
                *(int*)(DAT_80612a48 + 0x2c8) = 0;
                player_ai_explosion_spawn(1, -1, (float*)(DAT_80612a48 + 0x2b4), (float*)0, *proj, 0, 1);
            }
        } else if ((*(int*)(DAT_80612a48 + 0x4cc) == 1) &&
                   ((who == 7) ||
                    boss3_points_within_range((float*)proj[10], (float*)(DAT_80612a48 + 0x4b8)))) {
            *(float*)(DAT_80612a48 + 0x4c4) = *(float*)(DAT_80612a48 + 0x4c4) - (float)proj[8];
            if (*(float*)(DAT_80612a48 + 0x4c4) < FLOAT_80619568) {
                *(float*)(DAT_80612a48 + 0x4c4) = FLOAT_80619568;
                *(int*)(DAT_80612a48 + 0x4cc) = 0;
                player_ai_explosion_spawn(0x20, -1, (float*)(DAT_80612a48 + 0x4b8), (float*)0, *proj, 0, 1);
            }
        } else if ((*(int*)(DAT_80612a48 + 0x52c) == 1) &&
                   ((who == 0xe) ||
                    boss3_points_within_range((float*)proj[10], (float*)(DAT_80612a48 + 0x518)))) {
            *(float*)(DAT_80612a48 + 0x524) = *(float*)(DAT_80612a48 + 0x524) - (float)proj[8];
            if (*(float*)(DAT_80612a48 + 0x524) < FLOAT_80619568) {
                *(float*)(DAT_80612a48 + 0x524) = FLOAT_80619568;
                *(int*)(DAT_80612a48 + 0x52c) = 0;
                player_ai_explosion_spawn(0x20, -1, (float*)(DAT_80612a48 + 0x518), (float*)0, *proj, 0, 1);
            }
        } else if ((who == 0) && (FLOAT_80619568 == *(float*)(DAT_80612a48 + 0x1434))) {
            hit = boss3_point_in_facing_cone((float*)proj[10]);
        }

        if (hit) {
            int actor = *(int*)(*proj + 0x7c);
            *(float*)(DAT_80612a48 + 0x580) = (float)((double)*(float*)(DAT_80612a48 + 0x580) - dmgD);
            *(int*)(*proj + 0x1cc) = *(int*)(DAT_80612a48 + 0x580);
            *(float*)(DAT_80612a48 + 0x6494) = FLOAT_80619580;
            unsigned int alive = statlvl_class_is_state5(1);
            int base = DAT_80612a48;
            if (alive == 0) {
                if (*(float*)(DAT_80612a48 + 0x580) <= *(float*)(DAT_80612a48 + 0x584)) {
                    if (*(int*)(actor + 0x20) != 0x12)
                        *(int*)(actor + 0x9d0) = 0x12;
                    anim_play_scaled((double)FLOAT_8061963c, (double)FLOAT_80619580);
                    *(unsigned char*)(DAT_80612a48 + 0x98) = 2;
                    *(float*)(DAT_80612a48 + 0x5b0) = FLOAT_806195f4;
                }
            } else if (*(float*)(DAT_80612a48 + 0x580) < FLOAT_80619568) {
                actor = *(int*)(*proj + 0x7c);
                if (*(int*)(actor + 0x20) != 0x14) {
                    int params[7];
                    sound_init_play_params((int)params);
                    *(unsigned char*)&params[3] = 1;
                    params[0] = 0x13e4;
                    params[4] = *(int*)(base + 0x9c);
                    params[5] = *(int*)(base + 0xa0);
                    params[6] = *(int*)(base + 0xa4);
                    sound_play_request(params);
                    anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
                    if (*(int*)(actor + 0x20) != 0x14)
                        *(int*)(actor + 0x9d0) = 0x14;
                }
                *(float*)(DAT_80612a48 + 0x580) = FLOAT_80619568;
            }
            player_ai_projectile_ricochet(*proj, proj[10], (float*)proj[0xb], (float*)proj[0xc],
                                          proj[6], 1);
        }
    }
    FUN_80382734();
}

// ===========================================================================
// 0x802a0948  boss3_projectile_contact
// Top-level projectile-contact callback: track per-shooter hit counters
// (+0x5b0/+0x5b8), let the energy surface absorb the hit (boss3_surface_receive_hit);
// if not absorbed run the hit handler, or for reflected shots compute a reflection
// off the surface normal (+0x649c) and spawn a bounced projectile.
// ===========================================================================
void boss3_projectile_contact(int* param_1) {
    if ((*(int*)(DAT_80612a48 + 0x5ac) == 0) || (param_1[1] != *(int*)(DAT_80612a48 + 0x5ac))) {
        if ((*(int*)(DAT_80612a48 + 0x5b4) != 0) && (param_1[1] == *(int*)(DAT_80612a48 + 0x5b4))) {
            *(float*)(DAT_80612a48 + 0x5b8) = *(float*)(DAT_80612a48 + 0x5b8) + FLOAT_806195a0;
        }
    } else {
        *(float*)(DAT_80612a48 + 0x5b0) = *(float*)(DAT_80612a48 + 0x5b0) + FLOAT_806195a0;
    }

    if (param_1[1] == *param_1)
        return;

    if (param_1[6] == 0x17) {
        if (boss3_surface_receive_hit((float*)param_1[10]) == 0)
            boss3_projectile_hit();
    } else {
        if (boss3_surface_receive_hit((float*)param_1[10]) == 0) {
            boss3_projectile_hit();
        } else {
            float n[3];
            n[0] = DAT_803e079c;
            n[1] = DAT_803e07a0;
            n[2] = DAT_803e07a4;
            mtx4_transform_vector((float*)(DAT_80612a48 + 0x649c), n);
            float* dir = (float*)param_1[0xc];
            float d = (float)((double)FLOAT_80619578 *
                              (double)(n[2] * dir[2] + n[0] * dir[0] + n[1] * dir[1]));
            float refl[3];
            refl[0] = d * n[0] - dir[0];
            refl[1] = d * n[1] + dir[1];
            refl[2] = d * n[2] - dir[2];
            float* org = (float*)param_1[10];
            float pos[3];
            pos[0] = org[0] + refl[0];
            pos[1] = org[1] + refl[1];
            pos[2] = org[2] + refl[2];
            player_ai_projectile_spawn_simple((double)FLOAT_80619578, (double)FLOAT_806195a8,
                param_1[6], pos, (int*)refl, param_1[1], (int*)0, 6, 0, 1, param_1[0xf], param_1[9]);
        }
    }
}
