// snake_boss_fsm.cpp - segmented "snake"/spine creature boss: body-pose deform,
// attack-pattern selection, event/damage handlers and small matrix helpers.
//
// This boss has a long multi-segment body (many bone matrices) that is bent
// along a spine curve each frame. A shared per-boss global block (DAT_80612a58
// + 0x41xx/0x42xx/0x4b/0x3e88 offsets) holds the boss's mode, wave counters and
// target-angle cache. The FSM shares the aiactor actor-record layout (state word
// +0x20, pending +0x9d0, per-state cursor +0x9dc) and the 0x806195xx / 0x806196xx
// rodata constant blocks.
//
//  snake_mtx3_multiply_upper   (0x8028cef8): multiply the upper-left 3x3 of two
//    row-major 4x4 matrices into a destination.
//  snake_mtx3_scale_init       (0x8028cf5c): initialize a 3x3 as a uniform scale
//    (diagonal) matrix from a single constant.
//  snake_anim_mode_toggle      (0x8028cf88): enter/leave the "coiled" anim mode,
//    setting the playback rate + a paired clip flag (event 1 vs 0).
//  snake_pose_spine_deform     (0x8028d010): the per-frame body-pose deform: bend
//    each bone group of the spine by a Y-rotation scaled by the wave phase, plus
//    a head look-at toward the target.
//  snake_event_bite_react      (0x8028d6e0): bite/grab event handler; on the wind
//    -up state, tests the mouth-bone distance to the target and plays the bite +
//    sound if in range.
//  snake_anim_pattern_index    (0x8028d900): map an anim-pattern id to its index
//    in the 8-entry pattern table (DAT_80473200).
//  snake_attack_pattern_select (0x8028d940): the attack-pattern selector FSM:
//    choose the next anim pattern from the target angle/distance + RNG, then drive
//    the pending state and play the chosen clip.
//  snake_clip_userflag_probe   (0x8028e078): level-specific probe of the active
//    clip's user flags (different clip id per campaign level).
//  snake_render_submaterial_hook(0x8028e0cc): per-boss render hook that swaps in a
//    sub-material and issues the extra draw passes (map-specific).
//  snake_tail_sweep_damage     (0x8028e1ac): tail/melee sweep - raycast against
//    each player mesh and dispatch damage where it connects.
//
// Address range: 0x8028cef8 - 0x8028e3xx
// Subsystem: game (AI actors)

#include "common.h"

extern "C" {

// --- raw actor-context accessors (still FUN_ in the IDB) -------------------
extern int    FUN_803826e8(void);   // current actor entity record
extern int    FUN_803826f0(void);   // current actor entity record (alt entry)
extern void   FUN_80382734(void);   // tick epilogue A
extern void   FUN_8038273c(void);   // tick epilogue B
extern void   FUN_802aeee0(void);   // boss sub-material draw pass A
extern void   FUN_80293390(void);   // boss sub-material draw pass B
extern int    __save_gpr_28(void);  // compiler save-regs stub
extern void   __restore_gpr_28(void);// compiler restore-regs stub

// --- scalar / data globals -------------------------------------------------
extern int   DAT_806109c0;           // frame delta ticks (signed, xor bias)
extern int   DAT_80611d78;           // player record table base
extern int   DAT_80612614;           // current game/level mode id
extern int   DAT_80612a58;           // snake-boss global control block base
extern float FLOAT_80612a54;         // cached vertical offset to target
extern int   DAT_80473200;           // 8-entry anim-pattern id table
extern int   DAT_803e05c4;           // tail-sweep ray dir.x (rodata)
extern int   DAT_803e05c8;           // tail-sweep ray dir.y
extern int   DAT_803e05cc;           // tail-sweep ray dir.z

// --- float / double constants (rodata, snake block) ------------------------
extern const float  FLOAT_80619560;  // 1.0 (scale-init diagonal)
extern const float  FLOAT_80619564;  // anim playback rate
extern const float  FLOAT_80619568;  // 0.0
extern const float  FLOAT_8061956c;  // wave decay coeff
extern const float  FLOAT_80619570;  // wave amplitude scale
extern const float  FLOAT_80619574;  // 0.5 / phase midpoint
extern const float  FLOAT_80619578;  // phase scale
extern const float  FLOAT_8061957c;  // segment bend coeff A
extern const float  FLOAT_80619580;  // 1.0 / distance-sq threshold
extern const float  FLOAT_80619584;  // segment bend coeff B
extern const float  FLOAT_80619588;  // 180.0 (deg<->rad numerator)
extern const float  FLOAT_8061958c;  // PI (deg<->rad denominator)
extern const float  FLOAT_80619590;  // per-group phase offset A
extern const float  FLOAT_80619594;  // per-group phase offset B
extern const float  FLOAT_80619598;  // rng gate / phase offset C
extern const float  FLOAT_8061959c;  // rng gate / phase offset D
extern const float  FLOAT_806195a0;  // rng gate / phase offset E
extern const float  FLOAT_806195a4;  // rng gate F
extern const float  FLOAT_806195a8;  // per-group phase offset G
extern const float  FLOAT_806195ac;  // 2*PI angle wrap
extern const float  FLOAT_806195b0;  // -PI wrap lower bound
extern const float  FLOAT_806195b4;  // head look bias / angle bound
extern const float  FLOAT_806195b8;  // damage/health scale
extern const float  FLOAT_806195bc;  // bite sound param a
extern const float  FLOAT_806195c0;  // bite sound param b / angle bound
extern const float  FLOAT_806195c4;  // |angle| gate A
extern const float  FLOAT_806195c8;  // angle bound B
extern const float  FLOAT_806195cc;  // angle bound C
extern const float  FLOAT_806195d0;  // angle bound D
extern const float  FLOAT_806195d4;  // vertical-offset gate
extern const float  FLOAT_806195d8;  // angle window hi
extern const float  FLOAT_806195dc;  // angle window lo
extern const float  FLOAT_806195e0;  // vertical-offset window
extern const float  FLOAT_806195e4;  // rng denominator
extern const float  FLOAT_806195e8;  // rng gate H
extern const float  FLOAT_806195ec;  // angle window hi 2
extern const float  FLOAT_806195f0;  // angle window lo 2
extern const float  FLOAT_806195f4;  // rng scale / big const
extern const float  FLOAT_806195f8;  // angle threshold
extern const float  FLOAT_806195fc;  // clip-select anim rate
extern const float  FLOAT_80619600;  // anim speed a
extern const float  FLOAT_80619604;  // anim speed b
extern const float  FLOAT_80619608;  // anim speed c
extern const float  FLOAT_8061960c;  // anim speed d
extern const float  FLOAT_80619620;  // tail-damage vertical gate
extern const float  FLOAT_80619624;  // tail-damage amount base
extern const double DOUBLE_80619610; // rng bias

// --- named cross-module helpers (call-site typed; still FUN_ in IDB) --------
extern double math_atan2(double y, double x);
extern double math_sqrt(double x);

extern unsigned long long anim_play_scaled(unsigned long long ctx, double speed);
extern int  anim_is_clip_active(int actor, int clipId);
extern int  anim_primary_clip_userflags(int actor, int clipId);
extern double anim_primary_clip_phase(int animCtx);
extern void anim_set_playback_rate(double rate, int* actorCtx, int mode);
extern void anim_set_pair_9498(int actorCtx, int a, int b);

extern void mtx4_set_rotation_y(double angle, float* mtx);
extern void mtx4_set_rotation_z(double angle, float* mtx);
extern void mtx4_multiply(float* a, float* b, float* out);
extern void mtx4_transform_point(float* mtx, float* pt);
extern void leaf_31e8(unsigned int dst, unsigned int src, int n);
extern void entity_build_local_matrix(float* outMtx, int entity, int includePos, int mirror);

extern void model_pose_eval(void);
extern unsigned int rng_counter_tick(void);

extern void sound_init_play_params(int paramsPtr);
extern unsigned int sound_play_request(int* params);

extern unsigned int gx_set_sub_material_id(int id);

extern int  session_get_state_flag(void);
extern int  player_ai_mesh_raycast(int entity, float* rayOrigin, float* rayDir,
                                   float* entPos, int p5, float* hitPos, float* hitNormal, int flags);
extern void switch_slot_reset(int slotPtr);
extern void damage_dispatch_to_entity(double a, double b, double c, double d, double e,
                                      double f, double g, double h, int* eventData);

} // extern "C"

// Reconstruct the "(double)CONCAT44(0x43300000, bits) - bias" rng idiom.
static inline float sb_unit(unsigned int bits, double bias) {
    return (float)((double)CONCAT44(0x43300000, bits) - bias);
}

// ===========================================================================
// 0x8028cef8  snake_mtx3_multiply_upper
// dst[i][j] = sum_k a[i][k] * b[k][j] for the upper-left 3x3 of two row-major
// 4x4 matrices (stride 0x10 rows, 4 bytes per element).
// ===========================================================================
void snake_mtx3_multiply_upper(int dst, int a, int b) {
    int rowOff = 0;
    for (int i = 0; i < 3; i++) {
        float* ra = (float*)(a + rowOff);
        int colOff = 0;
        for (int j = 0; j < 3; j++) {
            float* cb = (float*)(b + colOff);
            *(float*)(dst + rowOff + colOff) = cb[8] * ra[2] + cb[4] * ra[1] + *ra * *cb;
            colOff += 4;
        }
        rowOff += 0x10;
    }
}

// ===========================================================================
// 0x8028cf5c  snake_mtx3_scale_init
// Fill the diagonal of a 3x3 (row-major 4x4 storage) with a constant scale.
// ===========================================================================
void snake_mtx3_scale_init(float* mtx) {
    float s = FLOAT_80619560;
    mtx[0] = s;
    mtx[1] = s;
    mtx[2] = s;
    mtx[4] = s;
    mtx[8] = s;
}

// ===========================================================================
// 0x8028cf88  snake_anim_mode_toggle
// Enter (event 1) or leave (event 0) the coiled anim mode.
// ===========================================================================
void snake_anim_mode_toggle(int entity, int event) {
    if (event == 1) {
        *(int*)(*(int*)(entity + 0x7c) + 0x9e0) = 1;
        anim_set_playback_rate((double)FLOAT_80619564, *(int**)(entity + 4), 2);
        anim_set_pair_9498(*(int*)(entity + 4), 0x3c000000, 0x1a9);
    } else if (event < 1 && -1 < event) {
        *(int*)(*(int*)(entity + 0x7c) + 0x9e0) = 0;
        anim_set_playback_rate((double)FLOAT_80619564, *(int**)(entity + 4), 1);
    }
}

// helper: rotate a contiguous run of bone matrices about Y by `angle`.
static void snake_rotate_bone_range(int entity, float* rotMtx, int startOff, int endOff) {
    for (int off = startOff; off < endOff; off += 0x40) {
        float* m = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + off);
        mtx4_multiply(m, rotMtx, m);
    }
}

// ===========================================================================
// 0x8028d010  snake_pose_spine_deform
// Bend the body segments along the wave phase and apply a head look-at.
// ===========================================================================
void snake_pose_spine_deform(void) {
    int entity = FUN_803826e8();
    int rec = *(int*)(entity + 0x7c);
    int* skel = *(int**)(*(int*)(entity + 4) + 4);
    int skelBase = *skel;
    float rot[16], rotZ[16];

    model_pose_eval();
    int mode = *(int*)(DAT_80612a58 + 0x4234);
    double wave = (double)FLOAT_80619568;
    if (mode != 0) {
        double target = (double)*(float*)(DAT_80612a58 + 0x4224);
        double zero = (double)FLOAT_80619568;
        if (zero != target) {
            if (target <= zero) {
                if (target < zero) {
                    wave = (double)(*(float*)(rec + 0xb2c) * (float)((double)FLOAT_8061956c * target) +
                                    -*(float*)(rec + 0xb2c));
                    if (wave < target) { wave = target; }
                }
            } else {
                wave = (double)(*(float*)(rec + 0xb2c) * (float)((double)FLOAT_8061956c * target) +
                                *(float*)(rec + 0xb2c));
                if (target < wave) { wave = target; }
            }
            wave = (double)(float)(wave * (double)FLOAT_80619570);
        }
        if (mode == 2) {
            double ph = anim_primary_clip_phase(*(int*)(entity + 4));
            if ((double)FLOAT_80619574 <= ph) {
                double q = math_sqrt((double)((float)(ph - (double)FLOAT_80619574) * FLOAT_80619578));
                wave = (double)(float)((double)FLOAT_8061957c * (double)(float)((double)FLOAT_80619580 - q) + wave);
            } else {
                float p = (float)(ph * (double)FLOAT_80619578);
                wave = (double)(float)((double)FLOAT_8061957c * (double)(p * p) + wave);
            }
        } else if (mode == 3) {
            double ph = anim_primary_clip_phase(*(int*)(entity + 4));
            if ((double)FLOAT_80619574 <= ph) {
                double q = math_sqrt((double)((float)(ph - (double)FLOAT_80619574) * FLOAT_80619578));
                wave = (double)(float)((double)FLOAT_80619584 * (double)(float)((double)FLOAT_80619580 - q) + wave);
            } else {
                float p = (float)(ph * (double)FLOAT_80619578);
                wave = (double)(float)((double)FLOAT_80619584 * (double)(p * p) + wave);
            }
        }

        // apply per-group Y bends
        mtx4_set_rotation_y((double)((FLOAT_80619588 * (float)((double)FLOAT_80619564 * wave)) / FLOAT_8061958c), rot);
        { float* m = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + 0x80); mtx4_multiply(m, rot, m); }

        double headAngle = (double)((FLOAT_80619588 * (float)((double)FLOAT_80619574 * wave)) / FLOAT_8061958c);
        mtx4_set_rotation_y(headAngle, rot);
        snake_rotate_bone_range(entity, rot, 0xc0, 0x180);

        mtx4_set_rotation_y((double)((FLOAT_80619588 * (float)((double)FLOAT_80619590 * wave)) / FLOAT_8061958c), rot);
        { float* m; m = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + 0x180); mtx4_multiply(m, rot, m);
          m = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + 0x1c0); mtx4_multiply(m, rot, m);
          m = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + 0x500); mtx4_multiply(m, rot, m); }
        snake_rotate_bone_range(entity, rot, 0x700, 0x8c0);

        mtx4_set_rotation_y((double)((FLOAT_80619588 * (float)((double)FLOAT_80619594 * wave)) / FLOAT_8061958c), rot);
        snake_rotate_bone_range(entity, rot, 0x540, 0x5c0);
        snake_rotate_bone_range(entity, rot, 0x300, 0x380);

        mtx4_set_rotation_y((double)((FLOAT_80619588 * (float)((double)FLOAT_80619598 * wave)) / FLOAT_8061958c), rot);
        snake_rotate_bone_range(entity, rot, 0x5c0, 0x700);
        snake_rotate_bone_range(entity, rot, 0x380, 0x500);

        mtx4_set_rotation_y((double)((FLOAT_80619588 * (float)((double)FLOAT_8061959c * wave)) / FLOAT_8061958c), rot);
        { float* m; m = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + 0x200); mtx4_multiply(m, rot, m);
          m = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + 0x240); mtx4_multiply(m, rot, m); }
        snake_rotate_bone_range(entity, rot, 0x280, 0x300);

        mtx4_set_rotation_y((double)((FLOAT_80619588 * (float)((double)FLOAT_806195a0 * wave)) / FLOAT_8061958c), rot);
        if (*(int*)(DAT_80612a58 + 0x4234) == 1) {
            mtx4_set_rotation_z((double)((FLOAT_80619588 * (float)((double)FLOAT_806195a4 * wave)) / FLOAT_8061958c), rotZ);
            mtx4_multiply(rot, rotZ, rot);
        }
        leaf_31e8((unsigned int)(size_t)rotZ, *(int*)(*(int*)(entity + 4) + 0x10) + 0x240, 0x40);
        snake_mtx3_multiply_upper(*(int*)(*(int*)(entity + 4) + 0x10) + 0x240, (int)(size_t)rotZ, (int)(size_t)rot);

        mtx4_set_rotation_y(headAngle, rot);
        if (*(int*)(DAT_80612a58 + 0x4234) == 1) {
            mtx4_set_rotation_z((double)((FLOAT_80619588 * (float)((double)FLOAT_806195a8 * wave)) / FLOAT_8061958c), rotZ);
            mtx4_multiply(rot, rotZ, rot);
        }
        leaf_31e8((unsigned int)(size_t)rotZ, *(int*)(*(int*)(entity + 4) + 0x10) + 0x280, 0x40);
        snake_mtx3_multiply_upper(*(int*)(*(int*)(entity + 4) + 0x10) + 0x280, (int)(size_t)rotZ, (int)(size_t)rot);
        leaf_31e8((unsigned int)(size_t)rotZ, *(int*)(*(int*)(entity + 4) + 0x10) + 0x2c0, 0x40);
        snake_mtx3_multiply_upper(*(int*)(*(int*)(entity + 4) + 0x10) + 0x2c0, (int)(size_t)rotZ, (int)(size_t)rot);
    }

    // head look-at toward the leader
    int leader = *(int*)(rec + 4);
    double dy2, dx2;
    if (leader == 0) {
        dy2 = (double)FLOAT_80619568;
        dx2 = (double)FLOAT_80619568;
    } else {
        dy2 = (double)(*(float*)(leader + 0x64) - *(float*)(entity + 0x64));
        dx2 = (double)(*(float*)(leader + 0x6c) - *(float*)(entity + 0x6c));
    }
    float ang = (FLOAT_8061958c * (float)math_atan2(dy2, dx2)) / FLOAT_80619588;
    if (ang < FLOAT_80619568) { ang = ang + FLOAT_806195ac; }
    for (ang = ang - *(float*)(entity + 0x70); ang <= FLOAT_806195b0; ang = ang + FLOAT_806195ac) {}
    for (; FLOAT_8061958c < ang; ang = ang - FLOAT_806195ac) {}
    int headBone = *(int*)(*(int*)(entity + 4) + 0x10) +
                   *(char*)((int)skel + skelBase * -0xc0 + 0x2bbd) * 0x40;
    float px = *(float*)(headBone + 0x30);
    float py = *(float*)(headBone + 0x34);
    float pz = *(float*)(headBone + 0x38);
    mtx4_set_rotation_y((double)((FLOAT_80619588 * (ang - FLOAT_806195b4)) / FLOAT_8061958c), rot);
    leaf_31e8(*(int*)(*(int*)(entity + 4) + 0x10) + *(char*)((int)skel + skelBase * -0xc0 + 0x2bbd) * 0x40,
              (unsigned int)(size_t)rot, 0x40);
    *(float*)(headBone + 0x30) = px;
    *(float*)(headBone + 0x34) = py;
    *(float*)(headBone + 0x38) = pz;
    FUN_80382734();
}

// ===========================================================================
// 0x8028d6e0  snake_event_bite_react
// ===========================================================================
void snake_event_bite_react(int event, int otherHit) {
    int entity = FUN_803826f0();
    int rec = *(int*)(entity + 0x7c);
    (void)rec;
    if (event == 2) {
        if (*(int*)(*(int*)(entity + 0x7c) + 0x20) != 6) {
            *(int*)(*(int*)(entity + 0x7c) + 0x9d0) = 6;
            anim_play_scaled((unsigned long long)(double)FLOAT_80619568, (double)FLOAT_80619580);
        }
    } else if (event < 2) {
        if (0 < event && *(int*)(*(int*)(entity + 0x7c) + 0x20) == 5 &&
            anim_is_clip_active(entity, 0x1a5) == 0) {
            int* skel = *(int**)(*(int*)(entity + 4) + 4);
            float* mouth = (float*)skel[*skel * -0x30 + 0xb08];
            float pt[3];
            pt[0] = FLOAT_80619574 * (mouth[3] - mouth[0]) + mouth[0];
            pt[1] = FLOAT_80619574 * (mouth[4] - mouth[1]) + mouth[1];
            pt[2] = FLOAT_80619574 * (mouth[5] - mouth[2]) + mouth[2];
            mtx4_transform_point((float*)(*(int*)(*(int*)(entity + 4) + 0x10) +
                                          *(char*)((int)skel + *skel * -0xc0 + 0x2bbd) * 0x40), pt);
            float local[24];
            entity_build_local_matrix(local, entity, 1, 0);
            mtx4_transform_point(local, pt);
            float* tgt = *(float**)(otherHit + 0x28);
            if ((double)((pt[2] - tgt[2]) * (pt[2] - tgt[2]) +
                         (pt[0] - tgt[0]) * (pt[0] - tgt[0]) +
                         (pt[1] - tgt[1]) * (pt[1] - tgt[1])) < (double)FLOAT_80619580) {
                anim_play_scaled((unsigned long long)(double)FLOAT_80619568, (double)FLOAT_80619580);
                *(float*)(entity + 0x1cc) = *(float*)(entity + 0x1cc) - FLOAT_806195b8;
                int sp[7];
                sound_init_play_params((int)sp);
                *(unsigned char*)((int)sp + 0x24) = 1;
                sp[0] = 0x56a;
                *(float*)((int)sp + 0x18) = FLOAT_806195bc;
                *(float*)((int)sp + 0x0c) = FLOAT_806195c0;
                *(float*)((int)sp + 0x10) = pt[0];
                *(float*)((int)sp + 0x14) = pt[1];
                *(float*)((int)sp + 0x18) = pt[2];
                sound_play_request(sp);
            }
        }
    } else if (event == 6) {
        *(unsigned int*)(entity + 0x28) &= 0xfffffffe;
    }
    FUN_8038273c();
}

// ===========================================================================
// 0x8028d900  snake_anim_pattern_index
// ===========================================================================
int snake_anim_pattern_index(int patternId) {
    int off = 0;
    for (int i = 0; i < 8; i++) {
        if (patternId == *(int*)((int)&DAT_80473200 + off)) { return i; }
        off += 4;
    }
    return 0;
}

// ===========================================================================
// 0x8028d940  snake_attack_pattern_select
// Choose the next anim pattern from target angle/distance + RNG, then drive the
// pending state and play the selected clip.
// ===========================================================================
void snake_attack_pattern_select(void) {
    int entity = FUN_803826f0();
    int rec = *(int*)(entity + 0x7c);
    int sel;

    if (*(int*)(rec + 4) == 0) {
        *(int*)(rec + 0x9dc) = *(int*)(rec + 0x9dc) + 1;
        if (7 < *(int*)(rec + 0x9dc)) { *(int*)(rec + 0x9dc) = 0; }
        sel = (&DAT_80473200)[*(int*)(rec + 0x9dc)];
    } else {
        *(int*)(DAT_80612a58 + 0x4234) = 1;
        if (*(int*)(DAT_80612a58 + 0x4228) < *(int*)(DAT_80612a58 + 0x422c)) {
            FLOAT_80612a54 = *(float*)(rec + 0xaf4) -
                             (*(float*)(entity + 0x68) - *(float*)(*(int*)(rec + 4) + 0x68));
            float ang = *(float*)(DAT_80612a58 + 0x4224);
            if (ABS(ang) <= FLOAT_806195c4) {
                if (FLOAT_806195d4 <= FLOAT_80612a54) {
                    if (FLOAT_806195f4 <= ang ||
                        FLOAT_80619574 <= sb_unit(rng_counter_tick(), DOUBLE_80619610) / FLOAT_806195e4) {
                        if (FLOAT_8061959c <= sb_unit(rng_counter_tick(), DOUBLE_80619610) / FLOAT_806195e4) {
                            *(int*)(rec + 0x9dc) = *(int*)(rec + 0x9dc) + 1;
                            if (7 < *(int*)(rec + 0x9dc)) { *(int*)(rec + 0x9dc) = 0; }
                            sel = (&DAT_80473200)[*(int*)(rec + 0x9dc)];
                        } else {
                            *(int*)(rec + 0x9dc) = snake_anim_pattern_index(5);
                            sel = 5;
                        }
                    } else {
                        *(int*)(rec + 0x9dc) = snake_anim_pattern_index(6);
                        sel = 6;
                    }
                } else if ((((ang < FLOAT_806195d8 && FLOAT_806195dc < ang) || FLOAT_80612a54 < FLOAT_806195e0) &&
                            sb_unit(rng_counter_tick(), DOUBLE_80619610) / FLOAT_806195e4 < FLOAT_806195e8) ||
                           sb_unit(rng_counter_tick(), DOUBLE_80619610) / FLOAT_806195e4 < FLOAT_806195a4) {
                    *(int*)(rec + 0x9dc) = snake_anim_pattern_index(0);
                    sel = 0;
                } else if (((FLOAT_806195ec < *(float*)(DAT_80612a58 + 0x4224) &&
                             *(float*)(DAT_80612a58 + 0x4224) < FLOAT_806195f0) &&
                            sb_unit(rng_counter_tick(), DOUBLE_80619610) / FLOAT_806195e4 < FLOAT_8061959c) ||
                           ((*(float*)(DAT_80612a58 + 0x4224) < FLOAT_806195c0 &&
                             FLOAT_806195f4 < *(float*)(DAT_80612a58 + 0x4224)) ||
                            sb_unit(rng_counter_tick(), DOUBLE_80619610) / FLOAT_806195e4 < FLOAT_806195a4)) {
                    *(int*)(rec + 0x9dc) = snake_anim_pattern_index(3);
                    sel = 3;
                } else if ((FLOAT_806195f8 < *(float*)(DAT_80612a58 + 0x4224) &&
                            sb_unit(rng_counter_tick(), DOUBLE_80619610) / FLOAT_806195e4 < FLOAT_8061959c) ||
                           sb_unit(rng_counter_tick(), DOUBLE_80619610) / FLOAT_806195e4 < FLOAT_806195a0) {
                    *(int*)(rec + 0x9dc) = snake_anim_pattern_index(2);
                    sel = 2;
                } else if (FLOAT_806195f4 <= *(float*)(DAT_80612a58 + 0x4224) ||
                           FLOAT_80619574 <= sb_unit(rng_counter_tick(), DOUBLE_80619610) / FLOAT_806195e4) {
                    if (FLOAT_806195e8 <= sb_unit(rng_counter_tick(), DOUBLE_80619610) / FLOAT_806195e4) {
                        *(int*)(rec + 0x9dc) = *(int*)(rec + 0x9dc) + 1;
                        if (6 < *(int*)(rec + 0x9dc)) { *(int*)(rec + 0x9dc) = 0; }
                        sel = (&DAT_80473200)[*(int*)(rec + 0x9dc)];
                    } else {
                        *(int*)(rec + 0x9dc) = snake_anim_pattern_index(5);
                        sel = 5;
                    }
                } else {
                    *(int*)(rec + 0x9dc) = snake_anim_pattern_index(6);
                    sel = 6;
                }
            } else if (ang <= FLOAT_806195b4 || FLOAT_806195c8 <= ang) {
                if (FLOAT_806195cc <= ang || ang <= FLOAT_806195d0) {
                    *(int*)(rec + 0x9dc) = snake_anim_pattern_index(7);
                    sel = 7;
                    *(int*)(DAT_80612a58 + 0x4228) = *(int*)(DAT_80612a58 + 0x4228) - 1;
                } else {
                    *(int*)(rec + 0x9dc) = snake_anim_pattern_index(4);
                    sel = 4;
                    *(int*)(DAT_80612a58 + 0x4234) = 3;
                }
            } else {
                *(int*)(rec + 0x9dc) = snake_anim_pattern_index(4);
                sel = 4;
                *(int*)(DAT_80612a58 + 0x4234) = 2;
            }
            *(int*)(DAT_80612a58 + 0x4228) = *(int*)(DAT_80612a58 + 0x4228) + 1;
        } else if (*(int*)(rec + 0x20) == 5) {
            *(int*)(rec + 0x9dc) = snake_anim_pattern_index(8);
            sel = 8;
            *(int*)(DAT_80612a58 + 0x422c) =
                (int)(FLOAT_806195f4 * (sb_unit(rng_counter_tick(), DOUBLE_80619610) / FLOAT_806195e4)) + 5;
            *(int*)(DAT_80612a58 + 0x4228) = 0;
        } else {
            *(int*)(rec + 0x9dc) = snake_anim_pattern_index(9);
            sel = 9;
        }
    }

    switch (sel) {
    case 0:
    case 1:
        anim_play_scaled((unsigned long long)(double)FLOAT_806195fc, (double)FLOAT_80619600);
        if (*(int*)(rec + 0x20) != 2) { *(int*)(rec + 0x9d0) = 2; }
        break;
    case 2:
        anim_play_scaled((unsigned long long)(double)FLOAT_806195fc, (double)FLOAT_80619604);
        if (*(int*)(rec + 0x20) != 2) { *(int*)(rec + 0x9d0) = 2; }
        break;
    case 3:
    case 4:
        anim_play_scaled((unsigned long long)(double)FLOAT_806195fc, (double)FLOAT_80619608);
        if (*(int*)(rec + 0x20) != 2) { *(int*)(rec + 0x9d0) = 2; }
        break;
    case 5:
        anim_play_scaled((unsigned long long)(double)FLOAT_806195fc, (double)FLOAT_80619580);
        if (*(int*)(rec + 0x20) != 4) { *(int*)(rec + 0x9d0) = 4; }
        break;
    case 6:
        *(int*)(DAT_80612a58 + 0x4160) = 0;
        anim_play_scaled((unsigned long long)(double)FLOAT_80619568, (double)FLOAT_80619600);
        if (*(int*)(rec + 0x20) != 2) { *(int*)(rec + 0x9d0) = 2; }
        break;
    case 7:
        anim_play_scaled((unsigned long long)(double)FLOAT_806195fc, (double)FLOAT_8061960c);
        if (*(int*)(rec + 0x20) != 2) { *(int*)(rec + 0x9d0) = 2; }
        break;
    case 8:
        anim_play_scaled((unsigned long long)(double)FLOAT_806195fc, (double)FLOAT_80619600);
        if (*(int*)(rec + 0x20) != 2) { *(int*)(rec + 0x9d0) = 2; }
        break;
    case 9:
        *(int*)(DAT_80612a58 + 0x4234) = 0;
        snake_anim_mode_toggle(entity, 0);
        anim_play_scaled((unsigned long long)(double)FLOAT_806195fc, (double)FLOAT_80619580);
        if (*(int*)(rec + 0x20) != 5) { *(int*)(rec + 0x9d0) = 5; }
        break;
    default:
        break;
    }
    FUN_8038273c();
}

// ===========================================================================
// 0x8028e078  snake_clip_userflag_probe
// ===========================================================================
int snake_clip_userflag_probe(int entity) {
    if (DAT_80612614 == 0x11) {
        return anim_primary_clip_userflags(entity, 0x23b);
    } else if (DAT_80612614 < 0x11 && DAT_80612614 == 1) {
        return anim_primary_clip_userflags(entity, 0x19c);
    }
    return 0;
}

// ===========================================================================
// 0x8028e0cc  snake_render_submaterial_hook
// ===========================================================================
void snake_render_submaterial_hook(void) {
    if (DAT_80612a58 != 0 && *(int*)(DAT_80612a58 + 0x3e88) == 1) {
        if (DAT_80612614 == 1) {
            gx_set_sub_material_id(1);
            FUN_802aeee0();
        } else if (*(int*)(DAT_80612a58 + 0x4bd0) != 0) {
            gx_set_sub_material_id(1);
            FUN_802aeee0();
            if (*(int*)(DAT_80612a58 + 0x4bd8) != 0) {
                FUN_80293390(); // both branches call the same pass in the IDB
            }
        }
    }
}

// ===========================================================================
// 0x8028e1ac  snake_tail_sweep_damage
// Raycast the tail sweep against each player mesh and dispatch damage on hit.
// ===========================================================================
void snake_tail_sweep_damage(void) {
    int entity = __save_gpr_28();
    float rayDir[3];
    rayDir[0] = *(float*)&DAT_803e05c4;
    rayDir[1] = *(float*)&DAT_803e05c8;
    rayDir[2] = *(float*)&DAT_803e05cc;

    int off = 0;
    for (int i = 0; i < session_get_state_flag(); i++) {
        int pRec = *(int*)(DAT_80611d78 + off + 0x14);
        float rayOrigin[3];
        float entPos[3];
        rayOrigin[0] = *(float*)(pRec + 0x64);
        float py = *(float*)(pRec + 0x68);
        rayOrigin[2] = *(float*)(pRec + 0x6c);
        entPos[0] = rayOrigin[0];
        entPos[1] = py + *(float*)(DAT_80611d78 + off + 0x80);
        entPos[2] = rayOrigin[2];
        rayOrigin[1] = py;

        float hitPos[3];
        float hitNormal[3];
        if (*(int*)(entity + 4) != 0 &&
            player_ai_mesh_raycast(entity, rayOrigin, rayDir, entPos, 0, hitPos, hitNormal, 0) != 0) {
            hitNormal[0] = hitNormal[0] * FLOAT_806195b8;
            hitNormal[1] = hitNormal[1] * FLOAT_806195b8;
            hitNormal[2] = hitNormal[2] * FLOAT_806195b8;
            int ev[16];
            switch_slot_reset((int)ev);
            if (hitNormal[1] < FLOAT_80619620) {
                hitNormal[1] = hitNormal[1] * FLOAT_80619574;
                hitNormal[0] = hitNormal[0] * FLOAT_80619578;
                hitNormal[2] = hitNormal[2] * FLOAT_80619578;
            }
            float dmg = FLOAT_8061959c;
            if (*(int*)(*(int*)(entity + 4) + 0xcc) == 0x19e ||
                *(int*)(*(int*)(entity + 4) + 0x124) == 0x19e) {
                dmg = (float)((double)FLOAT_8061959c * (double)FLOAT_80619574);
            }
            ev[0] = pRec;
            ev[3] = entity;
            (void)dmg;
            damage_dispatch_to_entity((double)FLOAT_80619624, (double)FLOAT_80619598,
                                      (double)FLOAT_806195f4, 0, 0, 0, 0, 0, ev);
        }
        off += 0x141c;
    }
    __restore_gpr_28();
}
