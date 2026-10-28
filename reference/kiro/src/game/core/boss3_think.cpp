// boss3_think.cpp - the third segmented boss's per-frame AI brain, skeleton pose
// and animation matrices (control block DAT_80612a48; see boss3_tentacle.cpp /
// boss3_surface.cpp / boss3_control.cpp / boss3_combat.cpp for the rest).
//
//   * boss3_animate_segment_matrix builds and applies an X/Y rotation to a pair
//     of bone matrices around a pivot (used for the writhing segments).
//   * boss3_apply_hit_shake drives a decaying sine-table screen/model distortion
//     while the boss is in an active hurt/dying state.
//   * boss3_pose_skeleton evaluates the base pose then twists the bone-matrix
//     palette by the boss's animated swing angles.
//   * boss3_think is the master AI update: acquire target, rebuild part world
//     positions from the model bones, compute aim, drive the sub-systems, apply
//     camera shake, and run the ~0x15-case behaviour state machine (spawn/intro/
//     attacks/hurt/death) at actor+0x20.
//   * boss3_message_handler is the entity event callback (death on message 2).
//
// NOTE: several of these take their context in registers; the decompiler models
// that via the frame-enter intrinsics (FUN_803826e8/FUN_803826e4/__save_gpr_28).
// We keep those reads. boss3_animate_segment_matrix's second Y-rotation uses a
// prior fp value (Ghidra 'extraout_f1') that isn't recoverable from the listing;
// it is modelled as 0 (documented, non-matching).
//
// Address range: 0x802a0afc - 0x802a45b7
// Subsystem: game (boss AI)

#include "common.h"

extern "C" {

// --- data globals ----------------------------------------------------------
extern int   DAT_80612a48;            // boss3 control block base (byte-addressed)
extern int   DAT_80611d78;            // player record table base
extern int   DAT_80611d74;            // level/session state base
extern int   DAT_80612610;            // participant count
extern int   DAT_80612020;            // effect timer
extern int   DAT_806109c0;            // frame-delta ticks (xor bias)
extern char  DAT_805015bd;            // level/mode id
extern char  DAT_805015bf;            // difficulty/region variant flag
extern const float DAT_80583228;      // sine LUT (0x800 entries, &DAT_80583228)
extern int   DAT_8048ca9c;            // per-actor render table base
extern const float DAT_803e07a8, DAT_803e07ac, DAT_803e07b0;  // emitter pos range lo
extern const float DAT_803e07b4, DAT_803e07b8, DAT_803e07bc;  // emitter pos range hi

// --- float / double constants (declared as used) ---------------------------
extern const float FLOAT_80619560, FLOAT_80619564, FLOAT_80619568, FLOAT_8061956c;
extern const float FLOAT_80619570, FLOAT_80619574, FLOAT_80619578, FLOAT_8061957c;
extern const float FLOAT_80619580, FLOAT_80619584, FLOAT_80619588, FLOAT_8061958c;
extern const float FLOAT_80619590, FLOAT_8061959c, FLOAT_806195a0, FLOAT_806195a4;
extern const float FLOAT_806195a8, FLOAT_806195ac, FLOAT_806195b0, FLOAT_806195b4;
extern const float FLOAT_806195b8, FLOAT_806195bc, FLOAT_806195c4, FLOAT_806195e4;
extern const float FLOAT_806195e8, FLOAT_806195f4, FLOAT_806195f8, FLOAT_806195fc;
extern const float FLOAT_80619604, FLOAT_80619608, FLOAT_8061963c, FLOAT_80619648;
extern const float FLOAT_80619624, FLOAT_80619784;
extern const float FLOAT_8061965c, FLOAT_8061966c, FLOAT_80619670, FLOAT_8061967c;
extern const float FLOAT_80619678, FLOAT_80619688, FLOAT_806196c4, FLOAT_806196c8;
extern const float FLOAT_806196cc, FLOAT_806196d8, FLOAT_806196e4, FLOAT_80619700;
extern const float FLOAT_80619724, FLOAT_8061974c, FLOAT_80619754, FLOAT_80619758;
extern const float FLOAT_80619760, FLOAT_80619764, FLOAT_8061976c, FLOAT_80619770;
extern const float FLOAT_8061977c, FLOAT_8061978c, FLOAT_806197a0, FLOAT_806197a8;
extern const float FLOAT_806197b0, FLOAT_806197d0, FLOAT_806197dc, FLOAT_8061981c;
extern const float FLOAT_8061982c, FLOAT_8061983c, FLOAT_80619858, FLOAT_8061985c;
extern const float FLOAT_80619860, FLOAT_8061987c, FLOAT_80619880, FLOAT_80619884;
extern const float FLOAT_80619888, FLOAT_8061988c, FLOAT_80619890, FLOAT_80619894;
extern const float FLOAT_80619898, FLOAT_8061989c, FLOAT_806198a0, FLOAT_806198a4;
extern const float FLOAT_806198a8, FLOAT_806198ac, FLOAT_806198b0, FLOAT_806198b4;
extern const float FLOAT_806198b8, FLOAT_806198bc, FLOAT_806198c0, FLOAT_806198c4;
extern const float FLOAT_806198c8, FLOAT_80619618, FLOAT_806108e8, FLOAT_806108ec;
extern const double DOUBLE_80619610;  // rng bias
extern const double DOUBLE_80619630;  // frame-delta bias

// --- raw helpers (still FUN_ in the IDB) -----------------------------------
extern unsigned long long FUN_803826e8(void);  // frame enter -> ctx pair (fp+gpr)
extern int  FUN_803826e4(void);                 // frame enter -> actor ptr
extern void FUN_80382730(void);                 // frame leave
extern void FUN_80382734(void);                 // frame leave
extern unsigned long long __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern int* FUN_802a472c(int* skeleton, int boneId);  // bone-record lookup
extern void FUN_802af384(unsigned int tex);     // material setup
extern void FUN_802ad804(unsigned int tex);     // render-state setup
extern void FUN_802af834(void);                 // boss3 post-update hook

// --- named cross-module helpers (call-site typed) --------------------------
extern void mtx4_set_rotation_x(double angle, int outMtx);
extern void mtx4_set_rotation_y(double yaw, float* outMtx);
extern void mtx4_multiply(float* a, float* b, float* out);
extern void mtx4_transform_point(float* mtx, float* pt);
extern void mtx4_transform_vector(float* mtx, float* vec);
extern void mtx4_copy(float* dst, float* src);
extern void snake_mtx3_multiply_upper(int dstMtx, int srcMtx, int rotMtx);
extern void leaf_31e8(unsigned int dst, unsigned int src, int n);
extern void memset_wrapper(int dst, int val, int len);
extern double math_sqrt(double x);
extern unsigned int rng_counter_tick(void);
extern void model_pose_eval(void);
extern int  aiactor_retarget_visible_teammate(void);
extern int  aiactor_acquire_nearest_target(int actor, int preferred, int mode);
extern double aiactor_wrap_angle_delta(double a, double b);
extern void aiactor_turn_toward_leader(void);
extern void dir_to_angles(void* vec, void* outYaw, float* outPitch);
extern void entity_build_local_matrix(void* outMtx, int actor, int a, int b);
extern int  anim_event_scan_active(void);
extern int  anim_is_clip_active(int entity, int clipId);
extern int  anim_active_slot_userflags(int ctrl);
extern int  anim_primary_clip_userflags(int entity, int clipId);
extern int  anim_play_scaled(double startPhase, double rate);
extern void player_apply_camera_shake(double x, double y, double z, int playerRec);
extern void snake3_attractor_oscillate(float* rec, int center, int targetPos, int enabled, unsigned int lit);
extern unsigned long long statlvl_class_complete(double a, double b, double c, double d,
                                                 unsigned long long e, unsigned long long f,
                                                 unsigned long long g, unsigned long long h);
extern int  statlvl_class_is_state5(int classId);
extern int  aiactor_find_player_by_team(int teamTag);
extern int  entity_handle_resolve(unsigned int handle);
extern int  screen_effect_alloc_slot(void);
extern void screen_effect_init_slot(int slot, int actor);
extern void entity_register_for_update(int entity, int phase, int key);
extern void entity_set_render_type(int entity, void* fn);
extern void particle_def_create(double a, double b, int def, int c, int d, int e, int id, int f);
extern void particle_def_set_size_keyframes(double a, double b, double c, double d,
                                            unsigned int k0, unsigned int k1, unsigned int k2);
extern void particle_def_set_gravity(double g, int tag);
extern void particle_def_set_color_end(double a, double b, double c, double d);
extern void particle_def_set_velocity_ranges(double a, double b, double c, double d, double e, double f);
extern void particle_def_set_drag(double a);
extern void particle_emitter_create(double a, double b, double c, int src, int def, int d, int flags);
extern void particle_emitter_set_color_range(double a, double b, double c, double d);
extern void particle_emitter_set_lifetime_range(double a, double b);
extern unsigned long long particle_handle_clear(unsigned int* handle);
extern void particle_emitter_spawn(int slot, int def, int flags, float* pos0, float* pos1);
extern void particle_emitter_set_drag(double drag);
extern void particle_emitter_set_size_range(double a, double b);
extern unsigned int sprite_get_texture_id(int slot);
extern void sound_init_play_params(int params);
extern int  sound_play_request(int* params);
extern void projectile_strip_pool_reset(void);
extern void particle_cached_set_drag(double drag, int* pCachedRef);
extern int* worm_event_find_record_by_class(int cls);
extern int  worm_event_record_is_active(int rec);
extern void FUN_802aeccc(int a1, int a2, int a3, int a4, int a5, unsigned int flags);

} // extern "C"

// boss3 helpers defined elsewhere in this module (C++ linkage)
void boss3_spawn(void);                         // boss3_control.cpp
void boss3_update(void);                        // boss3_surface.cpp
void boss3_reset(void);                         // boss3_surface.cpp
void boss3_tentacles_reset(void);               // boss3_tentacle.cpp
void boss3_update_aim_angles(int param_1);      // boss3_combat.cpp
void boss3_angle_segment_lookup(void);          // boss3_control.cpp
void boss3_update_subunits(void);               // boss3_combat.cpp
void boss3_process_targets(void);               // boss3_control.cpp
void boss3_compute_segment_aim(void);           // boss3_combat.cpp
bool boss3_turn_toward_aim(double tolerance, float* pAngle);   // boss3_combat.cpp
void boss3_process_explosions(void);            // boss3_combat.cpp
void boss3_projectile_contact(int* proj);       // boss3_combat.cpp

// same-module forward decls
static void boss3_animate_segment_matrix(int palette, int bossBlock, int boneA, int boneB);
// boss3_think split into helpers to keep it readable (all inlined into the
// original single 0x802a14c8 function).
static void boss3_think_coop_links(int actor, int rec);
static void boss3_think_state_machine(int actor, int rec);

// helper: switch the boss actor to a new AI state (records the previous state
// and the "requested" state, matching the +0x9d0/+0x9d4/+0x9d8/+0x20 dance).
static inline void boss3_set_state(int rec, int newState) {
    if (*(int*)(rec + 0x20) != newState) {
        *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
        *(float*)(rec + 0x9d8) = FLOAT_80619568;
        *(int*)(rec + 0x20) = newState;
        *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
    }
}

// normalized rng jitter in [0,1)
static inline float rng_jitter(void) {
    unsigned int t = rng_counter_tick();
    return (float)((double)CONCAT44(0x43300000, t) - DOUBLE_80619610) / FLOAT_806195e4;
}

// ===========================================================================
// 0x802a0afc  boss3_animate_segment_matrix
// Build an X rotation (from an animated angle at ctx+0x24, +rng jitter) and a Y
// rotation (from ctx+0x18), compose, and apply the transform to two bone-matrix
// slots (boneA, boneB of 0x40 bytes) around the palette pivot. Context pair
// arrives via FUN_803826e8.
// ===========================================================================
static void boss3_animate_segment_matrix(int palette, int bossBlock, int boneA, int boneB) {
    (void)bossBlock;
    unsigned long long pair = FUN_803826e8();
    int ctx = (int)pair;                    // animation context (angles)
    // second Y-rotation reuses a prior fp result (Ghidra extraout_f1) that the
    // listing does not expose; modelled as 0 (non-matching).
    double prevAngle = 0.0;

    float rotX[16];
    float rotY[16];
    unsigned char scratch[64];

    double swing = (double)(FLOAT_806195b8 * *(float*)(ctx + 0x24));
    if ((double)FLOAT_80619568 < swing) {
        swing = (double)(float)((double)FLOAT_8061959c * (double)rng_jitter() + swing);
    }
    double xAngle = (double)((FLOAT_80619588 * (float)swing) / FLOAT_8061958c);
    mtx4_set_rotation_x(xAngle, (int)rotX);
    mtx4_set_rotation_y((double)((FLOAT_80619588 * FLOAT_80619564 * *(float*)(ctx + 0x18)) / FLOAT_8061958c),
                        rotY);
    mtx4_multiply(rotY, rotX, rotY);

    int bmA = palette + boneA * 0x40;
    leaf_31e8((unsigned int)scratch, (unsigned int)bmA, 0x40);
    snake_mtx3_multiply_upper(bmA, (int)scratch, (int)rotY);

    double px = (double)*(float*)(bmA + 0x30);
    double py = (double)*(float*)(bmA + 0x34);
    double pz = (double)*(float*)(bmA + 0x38);
    float* bmB = (float*)(palette + boneB * 0x40);
    bmB[0xc] = (float)((double)bmB[0xc] - px);
    bmB[0xd] = (float)((double)bmB[0xd] - py);
    bmB[0xe] = (float)((double)bmB[0xe] - pz);
    mtx4_multiply(bmB, rotX, bmB);
    bmB[0xc] = (float)((double)bmB[0xc] + px);
    bmB[0xd] = (float)((double)bmB[0xd] + py);
    bmB[0xe] = (float)((double)bmB[0xe] + pz);

    mtx4_set_rotation_y((double)((FLOAT_80619588 * (float)((double)*(float*)(ctx + 0x18) - prevAngle)) / FLOAT_8061958c),
                        rotY);
    mtx4_set_rotation_x(-xAngle, (int)rotX);
    mtx4_multiply(rotY, rotX, rotY);
    leaf_31e8((unsigned int)scratch, (unsigned int)bmB, 0x40);
    snake_mtx3_multiply_upper((int)bmB, (int)scratch, (int)rotY);
    FUN_80382734();
}

// ===========================================================================
// 0x802a0d10  boss3_apply_hit_shake
// While the boss surface is dark and it is in an active hurt/dying state, build
// a decaying sine-table distortion transform at +0x6440, link it to the actor
// (+0x254) and set render flag 0x10; otherwise clear them.
// ===========================================================================
void boss3_apply_hit_shake(int actor) {
    if (!(((double)FLOAT_80619568 == (double)*(float*)(DAT_80612a48 + 0x1434)))) {
        goto clear;
    }
    {
        int st = *(int*)(DAT_80612a48 + 0x58c);
        if (!((2 < st) && (st != 0x14) && (1 < (unsigned int)(st - 0x11)))) {
            goto clear;
        }
    }
    {
        double cap = (double)FLOAT_80619608;
        float phase = FLOAT_80619754 * FLOAT_80619578 * FLOAT_80619588 *
                      (float)((double)CONCAT44(0x43300000, DAT_80612020 ^ 0x80000000) - DOUBLE_80619630) *
                      (FLOAT_80619580 /
                       (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80619630)) +
                      FLOAT_80619574;
        double amt = (double)(FLOAT_806195a0 * (FLOAT_80619580 + (&DAT_80583228)[(int)phase & 0x7ff]) + FLOAT_80619574);
        if ((amt <= cap) && (cap = amt, amt < (double)FLOAT_80619568)) {
            cap = (double)FLOAT_80619568;
        }
        memset_wrapper(DAT_80612a48 + 0x6440, 0, 0x54);
        double one = (double)FLOAT_80619580;
        *(float*)(DAT_80612a48 + 0x6440) =
            -(float)(cap * (double)(float)(one - (double)*(float*)(DAT_80612a48 + 0x6494)) - one);
        *(float*)(DAT_80612a48 + 0x6444) =
            -(float)(cap * -(double)(float)((double)FLOAT_806195a0 * (double)*(float*)(DAT_80612a48 + 0x6494) - one) - one);
        *(float*)(DAT_80612a48 + 0x6448) = (float)(one - cap);
        *(float*)(DAT_80612a48 + 0x6490) = FLOAT_806195a8;
        *(float*)(DAT_80612a48 + 0x6484) = FLOAT_80619764;
        *(float*)(DAT_80612a48 + 0x6478) = FLOAT_80619764;
        *(int*)(actor + 0x254) = DAT_80612a48 + 0x6440;
        *(unsigned short*)(*(int*)(actor + 4) + 0x1ee) |= 0x10;
        if ((FLOAT_80619568 < *(float*)(DAT_80612a48 + 0x6494)) &&
            (*(float*)(DAT_80612a48 + 0x6494) = *(float*)(DAT_80612a48 + 0x6494) - FLOAT_806195a4,
             *(float*)(DAT_80612a48 + 0x6494) < FLOAT_80619568)) {
            *(float*)(DAT_80612a48 + 0x6494) = FLOAT_80619568;
        }
        return;
    }
clear:
    *(int*)(actor + 0x254) = 0;
    *(unsigned short*)(*(int*)(actor + 4) + 0x1ee) &= 0xffef;
}

// ===========================================================================
// 0x802a0efc  boss3_pose_skeleton
// Evaluate the base pose then twist the bone-matrix palette by the boss's
// animated swing angles (+0xfc pitch, +0x25c/+0x12c swing amounts, clamped by
// the +0x254/+0x128 limits), calling boss3_animate_segment_matrix for two segment
// pairs and composing the upper 3x3s over bones 9..29. Palette pointer arrives in
// registers (__save_gpr_28).
// ===========================================================================
void boss3_pose_skeleton(void) {
    int actor = __save_gpr_28();
    int dst = *(int*)(*(int*)(actor + 4) + 0x10);   // bone-matrix palette
    float afRot[16];
    float afTmp[16];
    unsigned char scratch[96];

    double amountA = (double)(FLOAT_80619574 * *(float*)(DAT_80612a48 + 0x25c) * *(float*)(DAT_80612a48 + 0x5a4));
    double amountB = (double)(FLOAT_80619574 * *(float*)(DAT_80612a48 + 0x12c) * *(float*)(DAT_80612a48 + 0x5a4));

    model_pose_eval();

    float dxL = *(float*)(DAT_80612a48 + 0x4c0) - FLOAT_80619860;
    float dxR = *(float*)(DAT_80612a48 + 0x4b8) - FLOAT_80619858;
    double pivX = (double)*(float*)(dst + 0x30);
    double pivY = (double)*(float*)(dst + 0x34);
    double pivZ = (double)*(float*)(dst + 0x38);
    double liftL = (double)FLOAT_80619568;
    if (dxR * dxR + dxL * dxL < FLOAT_80619724) {
        liftL = (double)FLOAT_80619678;
    }

    mtx4_set_rotation_x((double)((FLOAT_80619588 * -*(float*)(DAT_80612a48 + 0xfc)) / FLOAT_8061958c), (int)afRot);

    double reach = (double)FLOAT_80619568;
    if (amountA <= (double)*(float*)(DAT_80612a48 + 0x254)) {
        if (amountB < (double)*(float*)(DAT_80612a48 + 0x128)) {
            reach = (double)(float)((double)*(float*)(DAT_80612a48 + 0x128) - amountB);
        }
    } else {
        reach = (double)(float)((double)*(float*)(DAT_80612a48 + 0x254) - amountA);
    }

    double yA = (double)((FLOAT_80619588 * (float)((double)FLOAT_8061987c * reach)) / FLOAT_8061958c);
    mtx4_set_rotation_y(yA, afTmp);
    // segment 1 (arm chain L): matrices +0x80/+0xc0/+0x100
    *(float*)(dst + 0xb0) = (float)((double)*(float*)(dst + 0xb0) - pivX);
    *(float*)(dst + 0xb4) = (float)((double)*(float*)(dst + 0xb4) - pivY);
    *(float*)(dst + 0xb8) = (float)((double)*(float*)(dst + 0xb8) - pivZ);
    mtx4_multiply((float*)(dst + 0x80), afTmp, (float*)(dst + 0x80));
    double yB = (double)((FLOAT_80619588 * (float)((double)FLOAT_8061959c * reach)) / FLOAT_8061958c);
    *(float*)(dst + 0xb0) = (float)((double)*(float*)(dst + 0xb0) + pivX);
    *(float*)(dst + 0xb4) = *(float*)(dst + 0xb4) + (float)((double)FLOAT_806195fc * liftL + pivY);
    *(float*)(dst + 0xb8) = (float)((double)*(float*)(dst + 0xb8) + pivZ);
    mtx4_set_rotation_y(yB, afTmp);
    *(float*)(dst + 0xf0) = (float)((double)*(float*)(dst + 0xf0) - pivX);
    *(float*)(dst + 0xf4) = (float)((double)*(float*)(dst + 0xf4) - pivY);
    *(float*)(dst + 0xf8) = (float)((double)*(float*)(dst + 0xf8) - pivZ);
    mtx4_multiply((float*)(dst + 0xc0), afTmp, (float*)(dst + 0xc0));
    *(float*)(dst + 0xf0) = (float)((double)*(float*)(dst + 0xf0) + pivX);
    *(float*)(dst + 0xf4) = *(float*)(dst + 0xf4) + (float)((double)FLOAT_80619880 * liftL + pivY);
    *(float*)(dst + 0xf8) = (float)((double)*(float*)(dst + 0xf8) + pivZ);
    *(float*)(dst + 0x130) = (float)((double)*(float*)(dst + 0x130) - pivX);
    *(float*)(dst + 0x134) = (float)((double)*(float*)(dst + 0x134) - pivY);
    *(float*)(dst + 0x138) = (float)((double)*(float*)(dst + 0x138) - pivZ);
    mtx4_multiply((float*)(dst + 0x100), afTmp, (float*)(dst + 0x100));
    *(float*)(dst + 0x130) = (float)((double)*(float*)(dst + 0x130) + pivX);
    *(float*)(dst + 0x134) = *(float*)(dst + 0x134) + (float)(pivY + liftL);
    *(float*)(dst + 0x138) = (float)((double)*(float*)(dst + 0x138) + pivZ);
    if ((double)*(float*)(DAT_80612a48 + 0x254) < amountA) {
        mtx4_set_rotation_y((double)((float)((double)FLOAT_80619588 * reach) / FLOAT_8061958c), afTmp);
        mtx4_multiply(afRot, afTmp, afRot);
    }

    float dxL2 = *(float*)(DAT_80612a48 + 0x520) - FLOAT_80619860;
    float dxR2 = *(float*)(DAT_80612a48 + 0x518) - FLOAT_80619858;
    double liftR = (double)FLOAT_80619568;
    if (dxR2 * dxR2 + dxL2 * dxL2 < FLOAT_80619724) {
        liftR = (double)FLOAT_80619678;
    }
    mtx4_set_rotation_y(yA, afTmp);
    // segment 2 (arm chain R): matrices +0x180/+0x1c0/+0x200
    *(float*)(dst + 0x1b0) = (float)((double)*(float*)(dst + 0x1b0) - pivX);
    *(float*)(dst + 0x1b4) = (float)((double)*(float*)(dst + 0x1b4) - pivY);
    *(float*)(dst + 0x1b8) = (float)((double)*(float*)(dst + 0x1b8) - pivZ);
    mtx4_multiply((float*)(dst + 0x180), afTmp, (float*)(dst + 0x180));
    *(float*)(dst + 0x1b0) = (float)((double)*(float*)(dst + 0x1b0) + pivX);
    *(float*)(dst + 0x1b4) = *(float*)(dst + 0x1b4) + (float)((double)FLOAT_806195fc * liftR + pivY);
    *(float*)(dst + 0x1b8) = (float)((double)*(float*)(dst + 0x1b8) + pivZ);
    mtx4_set_rotation_y(yB, afTmp);
    *(float*)(dst + 0x1f0) = (float)((double)*(float*)(dst + 0x1f0) - pivX);
    *(float*)(dst + 0x1f4) = (float)((double)*(float*)(dst + 0x1f4) - pivY);
    *(float*)(dst + 0x1f8) = (float)((double)*(float*)(dst + 0x1f8) - pivZ);
    mtx4_multiply((float*)(dst + 0x1c0), afTmp, (float*)(dst + 0x1c0));
    *(float*)(dst + 0x1f0) = (float)((double)*(float*)(dst + 0x1f0) + pivX);
    *(float*)(dst + 0x1f4) = *(float*)(dst + 0x1f4) + (float)((double)FLOAT_80619880 * liftR + pivY);
    *(float*)(dst + 0x1f8) = (float)((double)*(float*)(dst + 0x1f8) + pivZ);
    *(float*)(dst + 0x230) = (float)((double)*(float*)(dst + 0x230) - pivX);
    *(float*)(dst + 0x234) = (float)((double)*(float*)(dst + 0x234) - pivY);
    *(float*)(dst + 0x238) = (float)((double)*(float*)(dst + 0x238) - pivZ);
    mtx4_multiply((float*)(dst + 0x200), afTmp, (float*)(dst + 0x200));
    *(float*)(dst + 0x230) = (float)((double)*(float*)(dst + 0x230) + pivX);
    *(float*)(dst + 0x234) = *(float*)(dst + 0x234) + (float)(pivY + liftR);
    *(float*)(dst + 0x238) = (float)((double)*(float*)(dst + 0x238) + pivZ);
    if (amountB < (double)*(float*)(DAT_80612a48 + 0x128)) {
        mtx4_set_rotation_y((double)((float)((double)FLOAT_80619588 * reach) / FLOAT_8061958c), afTmp);
        mtx4_multiply(afRot, afTmp, afRot);
    }

    boss3_animate_segment_matrix(dst, DAT_80612a48 + 0x23c, 9, 0xb);
    boss3_animate_segment_matrix(dst, DAT_80612a48 + 0x110, 0xc, 0xe);

    leaf_31e8((unsigned int)scratch, dst + 0x40, 0x40);
    snake_mtx3_multiply_upper(dst + 0x40, (int)scratch, (int)afRot);
    leaf_31e8((unsigned int)scratch, dst + 0x140, 0x40);
    snake_mtx3_multiply_upper(dst + 0x140, (int)scratch, (int)afRot);
    leaf_31e8((unsigned int)scratch, dst, 0x40);
    snake_mtx3_multiply_upper(dst, (int)scratch, (int)afRot);

    int off = 0x240;
    for (int b = 9; b < 0x1e; b++) {
        float* bm = (float*)(dst + off);
        bm[0xc] = (float)((double)bm[0xc] - pivX);
        bm[0xd] = (float)((double)bm[0xd] - pivY);
        bm[0xe] = (float)((double)bm[0xe] - pivZ);
        mtx4_multiply(bm, afRot, bm);
        bm[0xc] = (float)((double)bm[0xc] + pivX);
        bm[0xd] = (float)((double)bm[0xd] + pivY);
        bm[0xe] = (float)((double)bm[0xe] + pivZ);
        off += 0x40;
    }
    __restore_gpr_28();
}

// ===========================================================================
// 0x802a44d0  boss3_message_handler
// Entity event callback. On message type 2 (finish/kill) start the death sound
// (0x13e4) + anim and set the death state (0x14). Returns -1 (handled) for
// message types 1, 2 and 6.
// ===========================================================================
int boss3_message_handler(int actor, int msg) {
    int ret = 0;
    if (msg == 2) {
        int rec = *(int*)(actor + 0x7c);
        if (*(int*)(rec + 0x20) != 0x14) {
            int params[7];
            sound_init_play_params((int)params);
            *(unsigned char*)&params[3] = 1;
            params[0] = 0x13e4;
            params[4] = *(int*)(actor + 100);
            params[5] = *(int*)(actor + 0x68);
            params[6] = *(int*)(actor + 0x6c);
            sound_play_request(params);
            anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
            if (*(int*)(rec + 0x20) != 0x14) {
                *(int*)(rec + 0x9d0) = 0x14;
            }
            *(float*)(DAT_80612a48 + 0x5a0) = FLOAT_80619568;
        }
        ret = -1;
    } else if (msg < 2) {
        if (0 < msg) ret = -1;
    } else if (msg == 6) {
        ret = -1;
    }
    return ret;
}

// ===========================================================================
// 0x802a14c8  boss3_think
// The boss3 master per-frame AI update. Acquires/refreshes the target, rebuilds
// the boss's part world positions from the model's bone matrices (FUN_802a472c +
// mtx4 transforms), computes aim angles, drives the sub-systems (aim/segment/
// subunits/targets/update), applies camera shake, updates the co-op link timers,
// and runs the ~0x15-case behaviour state machine at actor(+0x7c)+0x20.
// Actor arrives via FUN_803826e4.
// ===========================================================================
void boss3_think(void) {
    int actor = FUN_803826e4();
    int rec = *(int*)(actor + 0x7c);
    float scratch[17];
    float local_c4, local_c0, local_bc;           // last mtx4_multiply out (row0.x/y/z used)
    float mtxOut[12];                              // full local_c4.. block (a4/a0/9c/b4/b0/ac + c4/c0/bc)
    (void)mtxOut;

    aiactor_retarget_visible_teammate();
    if (-1 < *(int*)(rec + 0x9d0)) {
        if (*(int*)(rec + 0x9d0) != *(int*)(rec + 0x20)) {
            *(float*)(rec + 0x9d8) = FLOAT_80619568;
            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
            *(int*)(rec + 0x20) = *(int*)(rec + 0x9d0);
        }
        *(int*)(rec + 0x9d0) = -1;
    }
    if ((*(unsigned int*)(rec + 0x2c) & 0x80) == 0) {
        *(float*)(rec + 0xa90) = FLOAT_80619568;
    } else {
        *(float*)(rec + 0xa90) =
            (float)((double)FLOAT_806108ec *
                    (double)(FLOAT_80619580 /
                             (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80619630)) +
                    (double)*(float*)(rec + 0xa90));
    }
    *(unsigned int*)(rec + 0x28) |= 0x4000;
    *(float*)(rec + 0xb3c) = FLOAT_80619568;
    *(int*)(DAT_80612a48 + 0x58c) = *(int*)(rec + 0x20);
    *(int*)(DAT_80612a48 + 0x9c) = *(int*)(actor + 100);
    *(int*)(DAT_80612a48 + 0xa0) = *(int*)(actor + 0x68);
    *(int*)(DAT_80612a48 + 0xa4) = *(int*)(actor + 0x6c);
    *(float*)(DAT_80612a48 + 0xb4) = *(float*)(actor + 100) + *(float*)(actor + 0x1a8);
    *(float*)(DAT_80612a48 + 0xb8) = *(float*)(actor + 0x68) + *(float*)(actor + 0x1ac);
    *(float*)(DAT_80612a48 + 0xbc) = *(float*)(actor + 0x6c) + *(float*)(actor + 0x1b0);

    if (*(int*)(rec + 4) == 0) {
        *(int*)(rec + 4) = aiactor_acquire_nearest_target(actor, *(int*)(DAT_80611d78 + 0x14), 0);
        if (*(int*)(rec + 4) == 0) {
            FUN_80382730();
            return;
        }
    } else {
        int tgt = *(int*)(rec + 4);
        int tctx = *(int*)(tgt + 0x7c);
        *(int*)(DAT_80612a48 + 0xc0) = *(int*)(tgt + 100);
        *(int*)(DAT_80612a48 + 0xc4) = *(int*)(tgt + 0x68);
        *(int*)(DAT_80612a48 + 0xc8) = *(int*)(tgt + 0x6c);
        *(int*)(DAT_80612a48 + 0xcc) = *(int*)(tctx + 0xbb4);
        *(int*)(DAT_80612a48 + 0xd0) = *(int*)(tctx + 0xbb8);
        *(int*)(DAT_80612a48 + 0xd4) = *(int*)(tctx + 0xbbc);
    }
    double heightBias = (double)(float)((double)FLOAT_80619884 *
        (double)(*(float*)(DAT_80612a48 + 0xc4) - *(float*)(DAT_80612a48 + 0xa0)));

    // --- rebuild part world positions from bone matrices --------------------
    if ((*(int*)(DAT_80612a48 + 0x590) == 0) || (*(int*)(DAT_80612a48 + 0x4cc) == 0) ||
        (*(int*)(DAT_80612a48 + 0x52c) == 0)) {
        int skel = *(int*)(actor + 4);
        int palette = *(int*)(skel + 0x10);
        entity_build_local_matrix(scratch, actor, 1, 0);

        // reads local_c4/c0/bc (row0 of the multiply) + local_a4..ac (upper 3x3)
        struct M { int a4, a0, s9c, b4, b0, ac; float c4, c0, bc; };
        M m; // populated by mtx4_multiply into &local_c4 ordering below

        auto build_part = [&](int boneId, int posOff, int fwdOff, float fwdScale, bool storeBasis,
                              int basisOff) {
            int* bone = FUN_802a472c(*(int**)(skel + 4), boneId);
            float* bp = (float*)bone[0x28];
            *(float*)(DAT_80612a48 + posOff) = bp[3] - bp[0];
            *(float*)(DAT_80612a48 + posOff + 4) = bp[4] - bp[1];
            *(float*)(DAT_80612a48 + posOff + 8) = bp[5] - bp[2];
            *(float*)(DAT_80612a48 + posOff) = FLOAT_80619574 * *(float*)(DAT_80612a48 + posOff) + bp[0];
            *(float*)(DAT_80612a48 + posOff + 4) = FLOAT_80619574 * *(float*)(DAT_80612a48 + posOff + 4) + bp[1];
            *(float*)(DAT_80612a48 + posOff + 8) = FLOAT_80619574 * *(float*)(DAT_80612a48 + posOff + 8) + bp[2];
            mtx4_transform_point((float*)(palette + *(char*)((int)bone + 0x3d) * 0x40),
                                 (float*)(DAT_80612a48 + posOff));
            mtx4_transform_point(scratch, (float*)(DAT_80612a48 + posOff));
            mtx4_multiply((float*)(palette + *(char*)((int)bone + 0x3d) * 0x40), scratch, &m.c4);
            *(float*)(DAT_80612a48 + posOff) += m.c4;
            *(float*)(DAT_80612a48 + posOff + 4) += m.c0;
            *(float*)(DAT_80612a48 + posOff + 8) += m.bc;
            (void)fwdOff; (void)fwdScale; (void)storeBasis; (void)basisOff;
        };
        // Left tentacle hitbox (bone 0x16) at +0x188, basis at +0x164..+0x184
        {
            int* bone = FUN_802a472c(*(int**)(skel + 4), 0x16);
            float* bp = (float*)bone[0x28];
            *(float*)(DAT_80612a48 + 0x188) = FLOAT_80619574 * (bp[3] - bp[0]) + bp[0];
            *(float*)(DAT_80612a48 + 0x18c) = FLOAT_80619574 * (bp[4] - bp[1]) + bp[1];
            *(float*)(DAT_80612a48 + 0x190) = FLOAT_80619574 * (bp[5] - bp[2]) + bp[2];
            mtx4_transform_point((float*)(palette + *(char*)((int)bone + 0x3d) * 0x40), (float*)(DAT_80612a48 + 0x188));
            mtx4_transform_point(scratch, (float*)(DAT_80612a48 + 0x188));
            mtx4_multiply((float*)(palette + *(char*)((int)bone + 0x3d) * 0x40), scratch, &m.c4);
            *(float*)(DAT_80612a48 + 0x188) += m.c4;
            *(float*)(DAT_80612a48 + 0x18c) += m.c0;
            *(float*)(DAT_80612a48 + 0x190) += m.bc;
            *(int*)(DAT_80612a48 + 0x164) = m.a4; *(int*)(DAT_80612a48 + 0x168) = m.a0; *(int*)(DAT_80612a48 + 0x16c) = m.s9c;
            *(int*)(DAT_80612a48 + 0x170) = m.b4; *(int*)(DAT_80612a48 + 0x174) = m.b0; *(int*)(DAT_80612a48 + 0x178) = m.ac;
            *(float*)(DAT_80612a48 + 0x17c) = m.c4; *(float*)(DAT_80612a48 + 0x180) = m.c0; *(float*)(DAT_80612a48 + 0x184) = m.bc;
            *(float*)(DAT_80612a48 + 0x110) = m.c4;
            *(float*)(DAT_80612a48 + 0x114) = (float)((double)m.c0 + heightBias);
            *(float*)(DAT_80612a48 + 0x118) = m.bc;
            *(float*)(DAT_80612a48 + 0x144) = -(FLOAT_80619648 * m.c4 - *(float*)(DAT_80612a48 + 0x188));
            *(float*)(DAT_80612a48 + 0x148) = -(FLOAT_80619648 * m.c0 - *(float*)(DAT_80612a48 + 0x18c));
            *(float*)(DAT_80612a48 + 0x14c) = -(FLOAT_80619648 * m.bc - *(float*)(DAT_80612a48 + 0x190));
        }
        // Head hitbox (bone 0x12) at +0x2b4, basis at +0x290
        {
            int* bone = FUN_802a472c(*(int**)(skel + 4), 0x12);
            float* bp = (float*)bone[0x28];
            *(float*)(DAT_80612a48 + 0x2b4) = FLOAT_80619574 * (bp[3] - bp[0]) + bp[0];
            *(float*)(DAT_80612a48 + 0x2b8) = FLOAT_80619574 * (bp[4] - bp[1]) + bp[1];
            *(float*)(DAT_80612a48 + 0x2bc) = FLOAT_80619574 * (bp[5] - bp[2]) + bp[2];
            mtx4_transform_point((float*)(palette + *(char*)((int)bone + 0x3d) * 0x40), (float*)(DAT_80612a48 + 0x2b4));
            mtx4_transform_point(scratch, (float*)(DAT_80612a48 + 0x2b4));
            mtx4_multiply((float*)(palette + *(char*)((int)bone + 0x3d) * 0x40), scratch, &m.c4);
            *(float*)(DAT_80612a48 + 0x2b4) += m.c4;
            *(float*)(DAT_80612a48 + 0x2b8) += m.c0;
            *(float*)(DAT_80612a48 + 0x2bc) += m.bc;
            *(int*)(DAT_80612a48 + 0x290) = m.a4; *(int*)(DAT_80612a48 + 0x294) = m.a0; *(int*)(DAT_80612a48 + 0x298) = m.s9c;
            *(int*)(DAT_80612a48 + 0x29c) = m.b4; *(int*)(DAT_80612a48 + 0x2a0) = m.b0; *(int*)(DAT_80612a48 + 0x2a4) = m.ac;
            *(float*)(DAT_80612a48 + 0x2a8) = m.c4; *(float*)(DAT_80612a48 + 0x2ac) = m.c0; *(float*)(DAT_80612a48 + 0x2b0) = m.bc;
            *(float*)(DAT_80612a48 + 0x23c) = m.c4;
            *(float*)(DAT_80612a48 + 0x240) = (float)((double)m.c0 + heightBias);
            *(float*)(DAT_80612a48 + 0x244) = m.bc;
            *(float*)(DAT_80612a48 + 0x270) = -(FLOAT_80619648 * m.c4 - *(float*)(DAT_80612a48 + 0x2b4));
            *(float*)(DAT_80612a48 + 0x274) = -(FLOAT_80619648 * m.c0 - *(float*)(DAT_80612a48 + 0x2b8));
            *(float*)(DAT_80612a48 + 0x278) = -(FLOAT_80619648 * m.bc - *(float*)(DAT_80612a48 + 0x2bc));
        }
        // Tail point (bone 0x1d) at +0x3e0
        build_part(0x1d, 0x3e0, 0, 0.0f, false, 0);
        // Left claw (bone 7, basis palette bone 5) at +0x4b8, basis +0x494
        {
            int* bone = FUN_802a472c(*(int**)(skel + 4), 7);
            float* bp = (float*)bone[0x28];
            *(float*)(DAT_80612a48 + 0x4b8) = FLOAT_80619574 * (bp[3] - bp[0]) + bp[0];
            *(float*)(DAT_80612a48 + 0x4bc) = FLOAT_80619574 * (bp[4] - bp[1]) + bp[1];
            *(float*)(DAT_80612a48 + 0x4c0) = FLOAT_80619574 * (bp[5] - bp[2]) + bp[2];
            mtx4_transform_point((float*)(palette + *(char*)((int)bone + 0x3d) * 0x40), (float*)(DAT_80612a48 + 0x4b8));
            mtx4_transform_point(scratch, (float*)(DAT_80612a48 + 0x4b8));
            int* basis = FUN_802a472c(*(int**)(skel + 4), 5);
            mtx4_multiply((float*)(palette + *(char*)((int)basis + 0x3d) * 0x40), scratch, &m.c4);
            *(int*)(DAT_80612a48 + 0x494) = m.a4; *(int*)(DAT_80612a48 + 0x498) = m.a0; *(int*)(DAT_80612a48 + 0x49c) = m.s9c;
            *(int*)(DAT_80612a48 + 0x4a0) = m.b4; *(int*)(DAT_80612a48 + 0x4a4) = m.b0; *(int*)(DAT_80612a48 + 0x4a8) = m.ac;
            *(float*)(DAT_80612a48 + 0x4ac) = m.c4; *(float*)(DAT_80612a48 + 0x4b0) = m.c0; *(float*)(DAT_80612a48 + 0x4b4) = m.bc;
            *(float*)(DAT_80612a48 + 0x4bc) += FLOAT_806195a0;
        }
        // Right claw (bone 0xe, basis palette bone 0xc) at +0x518, basis +0x4f4
        {
            int* bone = FUN_802a472c(*(int**)(skel + 4), 0xe);
            float* bp = (float*)bone[0x28];
            *(float*)(DAT_80612a48 + 0x518) = FLOAT_80619574 * (bp[3] - bp[0]) + bp[0];
            *(float*)(DAT_80612a48 + 0x51c) = FLOAT_80619574 * (bp[4] - bp[1]) + bp[1];
            *(float*)(DAT_80612a48 + 0x520) = FLOAT_80619574 * (bp[5] - bp[2]) + bp[2];
            mtx4_transform_point((float*)(palette + *(char*)((int)bone + 0x3d) * 0x40), (float*)(DAT_80612a48 + 0x518));
            mtx4_transform_point(scratch, (float*)(DAT_80612a48 + 0x518));
            int* basis = FUN_802a472c(*(int**)(skel + 4), 0xc);
            mtx4_multiply((float*)(palette + *(char*)((int)basis + 0x3d) * 0x40), scratch, &m.c4);
            *(int*)(DAT_80612a48 + 0x4f4) = m.a4; *(int*)(DAT_80612a48 + 0x4f8) = m.a0; *(int*)(DAT_80612a48 + 0x4fc) = m.s9c;
            *(int*)(DAT_80612a48 + 0x500) = m.b4; *(int*)(DAT_80612a48 + 0x504) = m.b0; *(int*)(DAT_80612a48 + 0x508) = m.ac;
            *(float*)(DAT_80612a48 + 0x50c) = m.c4; *(float*)(DAT_80612a48 + 0x510) = m.c0; *(float*)(DAT_80612a48 + 0x514) = m.bc;
            *(float*)(DAT_80612a48 + 0x51c) += FLOAT_806195a0;
        }
        // Body/surface orientation (bone 0) at +0xa8 + surface normal +0x649c
        {
            int* bone = FUN_802a472c(*(int**)(skel + 4), 0);
            mtx4_multiply((float*)(palette + *(char*)((int)bone + 0x3d) * 0x40), scratch, &m.c4);
            *(float*)(DAT_80612a48 + 0xa8) = m.c4;
            *(float*)(DAT_80612a48 + 0xac) = m.c0;
            *(float*)(DAT_80612a48 + 0xb0) = m.bc;
            *(float*)(DAT_80612a48 + 0x574) = FLOAT_80619888;
            *(float*)(DAT_80612a48 + 0x578) = FLOAT_8061982c;
            *(float*)(DAT_80612a48 + 0x57c) = FLOAT_80619568;
            mtx4_copy(&m.c4, (float*)(DAT_80612a48 + 0x649c));
            mtx4_transform_vector(&m.c4, (float*)(DAT_80612a48 + 0x574));
            *(float*)(DAT_80612a48 + 0x574) += *(float*)(actor + 100) + *(float*)(actor + 0x1a8);
            *(float*)(DAT_80612a48 + 0x578) += *(float*)(actor + 0x68) + *(float*)(actor + 0x1ac);
            *(float*)(DAT_80612a48 + 0x57c) += *(float*)(actor + 0x6c) + *(float*)(actor + 0x1b0);
        }
    }

    // --- aim angles ---------------------------------------------------------
    float toTgt[3];
    toTgt[0] = *(float*)(DAT_80612a48 + 0xc0) - *(float*)(DAT_80612a48 + 0x9c);
    toTgt[1] = *(float*)(DAT_80612a48 + 0xc4) - *(float*)(DAT_80612a48 + 0xa0);
    toTgt[2] = *(float*)(DAT_80612a48 + 0xc8) - *(float*)(DAT_80612a48 + 0xa4);
    dir_to_angles((void*)(DAT_80612a48 + 0xa8), 0, (float*)(DAT_80612a48 + 0xf4));
    float tgtYaw, tgtPitch;
    dir_to_angles(toTgt, &tgtYaw, &tgtPitch);
    if (*(float*)(DAT_80612a48 + 0xf4) < FLOAT_80619568)
        *(float*)(DAT_80612a48 + 0xf4) += FLOAT_806195ac;

    int st = *(int*)(DAT_80612a48 + 0x58c);
    if ((st == 0x11) || (st == 0x12) || (st == 0x14)) {
        float f = *(float*)(DAT_80612a48 + 0xfc);
        if (f <= FLOAT_80619580) {
            if (FLOAT_80619570 <= f) *(float*)(DAT_80612a48 + 0xfc) = FLOAT_80619568;
            else *(float*)(DAT_80612a48 + 0xfc) = f + FLOAT_80619580;
        } else {
            *(float*)(DAT_80612a48 + 0xfc) = f - FLOAT_80619580;
        }
    } else if (FLOAT_80619568 == *(float*)(DAT_80612a48 + 0x100)) {
        double d = (double)(float)aiactor_wrap_angle_delta((double)*(float*)(DAT_80612a48 + 0xfc), (double)tgtYaw);
        double one = (double)FLOAT_80619580;
        if (d <= one) {
            if ((double)FLOAT_80619570 <= d) *(float*)(DAT_80612a48 + 0xfc) = (float)((double)*(float*)(DAT_80612a48 + 0xfc) + d);
            else *(float*)(DAT_80612a48 + 0xfc) = (float)((double)*(float*)(DAT_80612a48 + 0xfc) - one);
        } else {
            *(float*)(DAT_80612a48 + 0xfc) = (float)((double)*(float*)(DAT_80612a48 + 0xfc) + one);
        }
    } else if (*(float*)(DAT_80612a48 + 0xfc) < *(float*)(DAT_80612a48 + 0x100)) {
        *(float*)(DAT_80612a48 + 0xfc) += FLOAT_80619580;
    }
    if (tgtPitch < FLOAT_80619568) tgtPitch += FLOAT_806195ac;
    *(float*)(DAT_80612a48 + 0xe4) = (float)aiactor_wrap_angle_delta((double)*(float*)(DAT_80612a48 + 0xf4), (double)tgtPitch);
    aiactor_turn_toward_leader();
    *(int*)(DAT_80612a48 + 0xf8) = *(int*)(actor + 0x70);

    // --- drive the sub-systems ---------------------------------------------
    if (*(char*)(DAT_80612a48 + 0x98) != '\x03') {
        if (2 < *(int*)(rec + 0x20)) {
            double s = (double)*(float*)(DAT_80612a48 + 0x5a0);
            if ((s < (double)FLOAT_80619580) &&
                (*(float*)(DAT_80612a48 + 0x5a0) = (float)(s + (double)FLOAT_80619560),
                 (double)FLOAT_80619580 < (double)*(float*)(DAT_80612a48 + 0x5a0))) {
                *(float*)(DAT_80612a48 + 0x5a0) = FLOAT_80619580;
            }
            boss3_update_aim_angles(rec);
            boss3_angle_segment_lookup();
            boss3_update_subunits();
            boss3_process_targets();
            FUN_802af834();
            snake3_attractor_oscillate((float*)(DAT_80612a48 + 0x554), DAT_80612a48 + 0xcc, DAT_80612a48 + 0x574, 1,
                                       (unsigned int)(FLOAT_80619568 == *(float*)(DAT_80612a48 + 0x1434)));
        }
        boss3_update();
    }
    *(unsigned int*)(actor + 0x28) |= 0x4000001;

    // --- proximity camera shake --------------------------------------------
    {
        int evt = anim_event_scan_active();
        float dz = *(float*)(DAT_80612a48 + 0xa4) - *(float*)(DAT_80611d78 + 0xb0);
        float dx = *(float*)(DAT_80612a48 + 0x9c) - *(float*)(DAT_80611d78 + 0xa8);
        float dy = *(float*)(DAT_80612a48 + 0xa0) - *(float*)(DAT_80611d78 + 0xac);
        if (evt != 0) {
            double dist = math_sqrt((double)(float)((double)FLOAT_806195f4 * (double)(dy * dy) +
                                                    (double)(float)((double)(dx * dx) + (double)(dz * dz))));
            if (dist < (double)FLOAT_80619584) {
                double amt = (double)(FLOAT_8061967c * (float)((double)FLOAT_80619584 - dist));
                if ((anim_is_clip_active(actor, 0x6f3) != 0) || (anim_is_clip_active(actor, 0x6f2) != 0))
                    amt = (double)(float)(amt * (double)FLOAT_80619648);
                player_apply_camera_shake((double)(float)((double)FLOAT_8061963c * amt),
                                          (double)(float)((double)FLOAT_806195e8 * amt),
                                          (double)(float)((double)FLOAT_806196d8 * amt), DAT_80611d78);
                *(float*)(DAT_80611d78 + 0xe54) = FLOAT_80619618;
                double cap = (double)FLOAT_8061959c;
                if (cap < amt) amt = cap;
                *(float*)(DAT_80611d78 + 0xe58) = (float)amt;
            }
        }
    }

    // --- intro lock release -------------------------------------------------
    if ((*(int*)(DAT_80612a48 + 0x594) == 1) && (*(float*)(DAT_80612a48 + 0x580) < FLOAT_80619784) &&
        (anim_active_slot_userflags(*(int*)(actor + 4)) != 0)) {
        anim_play_scaled((double)FLOAT_8061963c, (double)FLOAT_80619580);
        boss3_set_state(rec, 0x11);
        *(int*)(DAT_80612a48 + 0x594) = 0;
    }

    // --- part-alive mask + external hook -----------------------------------
    {
        unsigned int mask = (unsigned int)(*(int*)(DAT_80612a48 + 0x19c) == 0);
        if (*(int*)(DAT_80612a48 + 0x2c8) == 0) mask |= 2;
        if (*(int*)(DAT_80612a48 + 0x4cc) == 0) mask |= 4;
        if (*(int*)(DAT_80612a48 + 0x52c) == 0) mask |= 8;
        FUN_802aeccc(DAT_80612a48 + 0x188, DAT_80612a48 + 0x2b4, DAT_80612a48 + 0x4b8, DAT_80612a48 + 0x518,
                     DAT_80612a48 + 0x2b4, mask);
    }

    // --- co-op / retaliation link timers -----------------------------------
    boss3_think_coop_links(actor, rec);

    // --- captured-entity pull (+0x5bc) -------------------------------------
    {
        int cap = *(int*)(DAT_80612a48 + 0x5bc);
        if (cap != 0) {
            int state = *(int*)(rec + 0x20);
            if (state == 0x14) {
                if (*(float*)(cap + 0x68) < *(float*)(DAT_80612a48 + 0x5c0)) {
                    *(float*)(cap + 0x68) += FLOAT_80619890;
                    if (*(float*)(DAT_80612a48 + 0x5c0) < *(float*)(*(int*)(DAT_80612a48 + 0x5bc) + 0x68))
                        *(float*)(*(int*)(DAT_80612a48 + 0x5bc) + 0x68) = *(float*)(DAT_80612a48 + 0x5c0);
                }
            } else {
                double h = (double)*(float*)(cap + 0x68);
                if (((double)FLOAT_80619894 < h) && (1 < state)) {
                    if (state == 2) {
                        if (FLOAT_80619898 < *(float*)(rec + 0x9cc))
                            *(float*)(cap + 0x68) = (float)(h - (double)FLOAT_80619890);
                    } else {
                        *(float*)(cap + 0x68) = (float)(h - (double)FLOAT_80619890);
                    }
                }
            }
        }
    }

    // --- global speed scale (+0x5a4) ---------------------------------------
    if (*(int*)(DAT_80612a48 + 0x58c) == 0x13) {
        *(float*)(DAT_80612a48 + 0x5a4) = FLOAT_80619578;
    } else {
        double s = (double)*(float*)(DAT_80612a48 + 0x5a4);
        if (((double)FLOAT_80619580 < s) &&
            (*(float*)(DAT_80612a48 + 0x5a4) = (float)(s - (double)FLOAT_8061967c),
             (double)FLOAT_80619580 < (double)*(float*)(DAT_80612a48 + 0x5a4))) {
            *(float*)(DAT_80612a48 + 0x5a4) = FLOAT_80619580;
        }
    }

    // --- behaviour state machine -------------------------------------------
    boss3_think_state_machine(actor, rec);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// boss3_think_coop_links: the co-op / two-player "escort" link handling. Tracks
// which player currently owns the boss (+0x5ac primary, +0x5b4 secondary), ramps
// the per-owner grip timers (+0x5b0/+0x5b8), and on expiry completes the class
// objective or hands the boss to another player. Inlined in the original.
// ---------------------------------------------------------------------------
static void boss3_think_coop_links(int actor, int rec) {
    (void)actor;
    if (*(int*)(DAT_80612a48 + 0x5ac) == 0) {
        if ((DAT_805015bd == '\n') && (1 < DAT_80612610) && (*(char*)(DAT_80612a48 + 0x98) == '\x02')) {
            projectile_strip_pool_reset();
            int off = 0;
            for (int i = 0; i < 3; i++) {
                int h = *(int*)(DAT_80612a48 + off + 0xf7c);
                particle_cached_set_drag((double)FLOAT_80619568, &h);
                off += 0x1a0;
            }
            statlvl_class_complete((double)FLOAT_80619568, 0, 0, 0, 0, 0, 0, 0);
            *(char*)(DAT_80612a48 + 0x98) = 3;
        }
        return;
    }

    if (*(int*)(rec + 4) == *(int*)(DAT_80612a48 + 0x5ac)) {
        float dtInv = FLOAT_80619580 /
                      (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80619630);
        *(float*)(DAT_80612a48 + 0x5b0) =
            -(float)((double)FLOAT_806108e8 * (double)dtInv - (double)*(float*)(DAT_80612a48 + 0x5b0));
        if (*(float*)(DAT_80612a48 + 0x5b0) < FLOAT_80619568) {
            if (*(char*)(DAT_80612a48 + 0x98) == '\x02') {
                projectile_strip_pool_reset();
                int off = 0;
                for (int i = 0; i < 3; i++) {
                    int h = *(int*)(DAT_80612a48 + off + 0xf7c);
                    particle_cached_set_drag((double)FLOAT_80619568, &h);
                    off += 0x1a0;
                }
                statlvl_class_complete((double)FLOAT_80619568, 0, 0, 0, 0, 0, 0, 0);
                *(char*)(DAT_80612a48 + 0x98) = 3;
                *(float*)(DAT_80612a48 + 0x5b0) = FLOAT_80619568;
            }
            *(float*)(DAT_80612a48 + 0x5a8) = FLOAT_80619580;
            *(int*)(rec + 4) = *(int*)(DAT_80611d78 + 0x14);
        }
    } else if ((*(int*)(DAT_80612a48 + 0x5b4) == 0) || (*(int*)(rec + 4) != *(int*)(DAT_80612a48 + 0x5b4))) {
        if (*(int*)(rec + 0x20) == 4) {
            if ((FLOAT_8061988c < *(float*)(DAT_80612a48 + 0x5b0)) && (rng_jitter() < FLOAT_806195a0)) {
                *(int*)(rec + 4) = *(int*)(DAT_80612a48 + 0x5ac);
                *(float*)(DAT_80612a48 + 0x5a8) = FLOAT_806195e8;
                *(float*)(DAT_80612a48 + 0x5b8) = FLOAT_80619568;
                return;
            }
            if ((FLOAT_8061988c < *(float*)(DAT_80612a48 + 0x5b8)) && (rng_jitter() < FLOAT_806195a0)) {
                *(float*)(DAT_80612a48 + 0x5a8) = FLOAT_806195e8;
                *(int*)(rec + 4) = *(int*)(DAT_80612a48 + 0x5b4);
            }
        }
    } else {
        float dtInv = FLOAT_80619580 /
                      (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80619630);
        *(float*)(DAT_80612a48 + 0x5b8) =
            -(float)((double)FLOAT_80619578 * (double)(float)((double)FLOAT_806108e8 * (double)dtInv) -
                     (double)*(float*)(DAT_80612a48 + 0x5b8));
        if (*(float*)(DAT_80612a48 + 0x5b8) < FLOAT_80619568) {
            if ((FLOAT_806195f8 < *(float*)(DAT_80612a48 + 0x5b0)) && (rng_jitter() < FLOAT_80619564)) {
                *(int*)(rec + 4) = *(int*)(DAT_80612a48 + 0x5ac);
                return;
            }
            *(float*)(DAT_80612a48 + 0x5a8) = FLOAT_80619580;
            *(int*)(rec + 4) = *(int*)(DAT_80611d78 + 0x14);
        }
    }
}

// helper: the yaw-tweak that many attack states run (nudge +0x70 by +0x590 sign).
static void boss3_apply_yaw_nudge(int actor) {
    if (*(int*)(DAT_80612a48 + 0x590) == -1) {
        *(float*)(actor + 0x70) -= FLOAT_806198a8;
        if (*(float*)(actor + 0x70) < FLOAT_80619568)
            *(float*)(actor + 0x70) += FLOAT_806195ac;
        *(int*)(actor + 0xc4) = *(int*)(actor + 0x70);
        *(int*)(DAT_80612a48 + 0x590) = 0;
    } else if (*(int*)(DAT_80612a48 + 0x590) == 1) {
        *(float*)(actor + 0x70) += FLOAT_806198a8;
        if (FLOAT_806195ac < *(float*)(actor + 0x70))
            *(float*)(actor + 0x70) -= FLOAT_806195ac;
        *(int*)(actor + 0xc4) = *(int*)(actor + 0x70);
        *(int*)(DAT_80612a48 + 0x590) = 0;
    }
}

// ---------------------------------------------------------------------------
// boss3_think_state_machine: the boss's ~0x15-case behaviour switch on the AI
// state at rec+0x20 (0 spawn, 1 intro, 2 rise, 4 idle/decide, 5 turn, 6/7 swing,
// 8/9/10 attack windups, 0xd/0xf/0x10 aim, 0x11/0x12 stun, 0x13 wait, 0x14 die).
// Faithful but condensed; anim transitions use boss3_set_state. Inlined in the
// original 0x802a14c8.
// ---------------------------------------------------------------------------
static void boss3_think_state_machine(int actor, int rec) {
    int state = *(int*)(rec + 0x20);
    switch (state) {
    case 0: {
        // spawn: build the boss, register update/render callbacks, create the
        // tentacle particle emitters, resolve co-op players.
        float emitLo[3] = { DAT_803e07a8, DAT_803e07ac, DAT_803e07b0 };
        float emitHi[3] = { DAT_803e07b4, DAT_803e07b8, DAT_803e07bc };
        boss3_spawn();
        *(float*)(rec + 0xb2c) = FLOAT_80619568;
        *(int*)(DAT_80611d74 + 0xe8c) = actor;
        *(int*)(actor + 0x268) = screen_effect_alloc_slot();
        screen_effect_init_slot(*(int*)(actor + 0x268), actor);
        entity_register_for_update(actor, 2, -0x7fd5f104);
        *(unsigned int*)(rec + 0x28) |= 0x4000000;
        entity_set_render_type(actor, (void*)boss3_projectile_contact);
        entity_register_for_update(actor, 3, -0x7fd5f2f0);
        *(int*)(actor + 0x1cc) = *(int*)(DAT_80612a48 + 0x580);
        *(float*)(actor + 0x1d8) = (DAT_805015bf == '\0') ? FLOAT_80619584 : FLOAT_806195c4;
        if (*(char*)(DAT_80612a48 + 0x98) == '\0') {
            int capent = entity_handle_resolve(0x57454c4c);
            *(int*)(DAT_80612a48 + 0x5bc) = capent;
            *(unsigned int*)(capent + 0x28) &= 0xffffffbf;
            *(int*)(DAT_80612a48 + 0x5c0) = *(int*)(capent + 0x68);
            if (DAT_805015bf == '\0')      *(float*)(DAT_80612a48 + 0x6498) = FLOAT_80619590;
            else if (DAT_805015bf == '\x01') *(float*)(DAT_80612a48 + 0x6498) = FLOAT_80619608;
            else                            *(float*)(DAT_80612a48 + 0x6498) = FLOAT_80619770;
            int off = 0;
            for (int i = 0; i < 3; i++) {
                int e = DAT_80612a48 + off + 0xf30;
                particle_def_create((double)FLOAT_8061963c, (double)FLOAT_80619580, e + 0x50, 0, 1, 1, 0x322, 0);
                particle_def_set_size_keyframes((double)FLOAT_80619580, (double)FLOAT_80619568, (double)FLOAT_80619568,
                                                (double)FLOAT_80619590, 0x640000, 0, 0xa281400);
                particle_def_set_gravity((double)FLOAT_80619670, 0xa321400);
                particle_def_set_color_end((double)FLOAT_80619560, (double)FLOAT_8061956c, (double)FLOAT_806195a0,
                                           (double)FLOAT_8061977c);
                particle_def_set_velocity_ranges((double)FLOAT_80619758, (double)FLOAT_80619758, (double)FLOAT_80619760,
                                                 (double)FLOAT_8061983c, (double)FLOAT_80619580, (double)FLOAT_80619580);
                particle_def_set_drag((double)FLOAT_80619584);
                particle_emitter_create((double)FLOAT_80619574, (double)FLOAT_80619568, (double)FLOAT_80619574,
                                        e + 0x11c, e + 0x50, 0, 0x4000);
                particle_emitter_set_color_range((double)FLOAT_80619624, (double)FLOAT_80619568, (double)FLOAT_8061966c,
                                                 (double)FLOAT_80619568);
                particle_emitter_set_lifetime_range((double)FLOAT_80619624, (double)FLOAT_80619568);
                particle_handle_clear((unsigned int*)(e + 0x4c));
                particle_emitter_spawn(e + 0x4c, e + 0x11c, 0x1000, emitLo, emitHi);
                particle_emitter_set_drag((double)FLOAT_80619580);
                particle_emitter_set_size_range((double)FLOAT_80619580, (double)FLOAT_80619580);
                off += 0x1a0;
            }
            FUN_802af384(sprite_get_texture_id(0x300));
            FUN_802ad804(sprite_get_texture_id(0x300));
            *(int*)(DAT_80612a48 + 0x5ac) = aiactor_find_player_by_team(0x52313141);
            boss3_set_state(rec, 1);
        } else {
            int off = 0;
            for (int i = 0; i < 3; i++) {
                *(int*)(DAT_80612a48 + off + 0xf5c) = 2;
                off += 0x1a0;
            }
            *(int*)(DAT_80612a48 + 0x643c) = 0;
            *(int*)(DAT_80612a48 + 0x5ac) = aiactor_find_player_by_team(0x52313142);
            *(int*)(DAT_80612a48 + 0x5b4) = aiactor_find_player_by_team(0x4352545a);
            boss3_set_state(rec, 4);
        }
        *(float*)(&DAT_8048ca9c + *(short*)(rec + 0x48) * 0x30) = FLOAT_80619618;
        *(char*)(DAT_80612a48 + 0x98) = 1;
        break;
    }
    case 1:
        if (anim_active_slot_userflags(*(int*)(actor + 4)) != 0) {
            int* r1 = worm_event_find_record_by_class(0x53504332);
            int* r2 = worm_event_find_record_by_class(0x53504331);
            if (worm_event_record_is_active((int)r1) || worm_event_record_is_active((int)r2)) {
                anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
                boss3_set_state(rec, 2);
            }
        }
        break;
    case 2:
        if (anim_active_slot_userflags(*(int*)(actor + 4)) != 0) {
            if (anim_is_clip_active(actor, 0x6f7) == 0)
                anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
            else
                boss3_set_state(rec, 4);
        }
        break;
    case 3:
        *(float*)(rec + 0xb2c) = FLOAT_80619568;
        anim_active_slot_userflags(*(int*)(actor + 4));
        break;
    case 5:
        *(float*)(rec + 0xb2c) = FLOAT_80619568;
        if ((*(int*)(DAT_80612a48 + 0x4cc) == 0) || (*(int*)(DAT_80612a48 + 0x52c) == 0)) {
            boss3_set_state(rec, 0x13);
        } else if (ABS(*(float*)(DAT_80612a48 + 0xe4)) <= FLOAT_80619584) {
            if ((FLOAT_80619580 < *(float*)(rec + 0x9cc)))
                boss3_set_state(rec, 4);
        } else {
            anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
            boss3_set_state(rec, 10);
            *(float*)(rec + 0xb2c) = FLOAT_8061956c;
        }
        break;
    case 6:
    case 7:
    case 9:
    case 10:
        // swing/attack windups: nudge yaw, advance the anim; on the anim's user
        // flag return to the decide state (4).
        boss3_apply_yaw_nudge(actor);
        if (anim_active_slot_userflags(*(int*)(actor + 4)) != 0) {
            boss3_set_state(rec, 4);
        }
        break;
    case 8:
        if (ABS((double)*(float*)(DAT_80612a48 + 0xe4)) <= (double)FLOAT_8061966c) {
            boss3_set_state(rec, 4);
        } else {
            anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619604);
            boss3_set_state(rec, 9);
        }
        break;
    case 4:
        // decide: pick swing / aim / turn based on distances and angle deltas.
        if ((*(int*)(DAT_80612a48 + 0x4cc) == 0) || (*(int*)(DAT_80612a48 + 0x52c) == 0)) {
            boss3_set_state(rec, 0x13);
        } else if (*(float*)(DAT_80612a48 + 0x10c) <= FLOAT_8061989c) {
            if (rng_jitter() < FLOAT_806195a8) {
                boss3_compute_segment_aim();
                float dx = *(float*)(DAT_80612a48 + 0xd8) - *(float*)(DAT_80612a48 + 0x9c);
                float dz = *(float*)(DAT_80612a48 + 0xe0) - *(float*)(DAT_80612a48 + 0xa4);
                if (dx * dx + dz * dz <= FLOAT_806198ac) {
                    bool aligned = boss3_turn_toward_aim((double)FLOAT_8061965c, (float*)(actor + 0x70));
                    boss3_set_state(rec, aligned ? 0xd : 0x10);
                } else {
                    anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
                    boss3_set_state(rec, 0xf);
                }
            }
        } else if (ABS(*(float*)(DAT_80612a48 + 0xe4)) <= FLOAT_806195bc) {
            boss3_set_state(rec, 5);
        } else {
            anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
            boss3_set_state(rec, 6);
        }
        break;
    case 0xd:
        *(float*)(DAT_80612a48 + 0x100) = FLOAT_8061966c;
        *(int*)(DAT_80612a48 + 0x160) = 2;
        *(int*)(DAT_80612a48 + 0x28c) = 2;
        if (anim_active_slot_userflags(*(int*)(actor + 4)) != 0) {
            if (anim_is_clip_active(actor, 0x6f8) == 0)
                anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619590);
            else {
                boss3_set_state(rec, 4);
                *(float*)(DAT_80612a48 + 0x100) = FLOAT_80619568;
                *(int*)(DAT_80612a48 + 0x160) = 0;
                *(int*)(DAT_80612a48 + 0x28c) = 0;
            }
        }
        break;
    case 0xf:
    case 0x10: {
        *(float*)(rec + 0xb2c) = FLOAT_80619568;
        bool aligned = boss3_turn_toward_aim((double)FLOAT_8061965c, (float*)(actor + 0x70));
        *(int*)(actor + 0xc4) = *(int*)(actor + 0x70);
        *(int*)(DAT_80612a48 + 0x160) = 2;
        *(int*)(DAT_80612a48 + 0x28c) = 2;
        if (aligned)
            boss3_set_state(rec, 0xd);
        break;
    }
    case 0x11:
    case 0x12:
        *(float*)(rec + 0xb2c) = FLOAT_80619568;
        boss3_apply_yaw_nudge(actor);
        if (anim_active_slot_userflags(*(int*)(actor + 4)) != 0) {
            if ((state == 0x12) && (*(int*)(DAT_80612a48 + 0x5ac) != 0))
                *(int*)(rec + 4) = *(int*)(DAT_80612a48 + 0x5ac);
            boss3_tentacles_reset();
            boss3_set_state(rec, 4);
        }
        break;
    case 0x13:
        *(float*)(rec + 0xb2c) = FLOAT_80619568;
        if ((*(int*)(DAT_80612a48 + 0x4cc) == 1) && (*(int*)(DAT_80612a48 + 0x52c) == 1))
            boss3_set_state(rec, 4);
        break;
    case 0x14:
        boss3_process_explosions();
        *(unsigned int*)(*(int*)(DAT_80612a48 + 0x5bc) + 0x28) |= 0x40;
        *(float*)(rec + 0xb2c) = FLOAT_80619568;
        *(int*)(DAT_80612a48 + 0x1e4) = 0;
        *(int*)(DAT_80612a48 + 0x310) = 0;
        if (anim_active_slot_userflags(*(int*)(actor + 4)) != 0) {
            statlvl_class_complete((double)FLOAT_80619568, 0, 0, 0, 0, 0, 0, 0);
        }
        break;
    default:
        break;
    }
}
