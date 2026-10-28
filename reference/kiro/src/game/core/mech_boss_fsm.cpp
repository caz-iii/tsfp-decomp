// mech_boss_fsm.cpp - the "MECH" mechanical boss (entity handle tag 0x4d454348
// = 'MECH'), built on the small 0x4c-byte control block DAT_80612a4c. It is a
// turret-style boss that tracks the target, fires missile volleys and a swept
// beam, and drives a 0-6 state machine with zone/soundpad transitions.
//
// The first function (snake3_finisher_react, 0x8029aae4) is physically adjacent
// in this address range but belongs to the serpent boss (DAT_80612a38, see
// snake_boss_serpent*.cpp); it is the serpent's grab-finisher render callback
// and is kept here only because it sits in this batch's contiguous range.
//
// DAT_80612a4c layout (int* base):
//   [0]              active flag
//   +0x04..+0x0c     target world position
//   +0x10..+0x18     self world position
//   +0x1c            previous facing snapshot
//   +0x20            current turret yaw     +0x24  turret pitch
//   +0x28            target yaw             +0x2c  beam oscillation timer
//   +0x30            spin direction         +0x34  beam-active flag
//   +0x38            "mode 1" flag          +0x3c  beam-burst timer
//   +0x40            turn rate              +0x44  fast-turn flag
//   +0x48            render-enable (==1)    +0x40(idx0x10) intro scale
//
// Functions:
//   snake3_finisher_react   (0x8029aae4)  [serpent boss - DAT_80612a38]
//   mech_boss_reset         (0x8029ac68)
//   mech_boss_alloc         (0x8029ad14)
//   mech_boss_render        (0x8029ad44)
//   mech_turret_aim_update  (0x8029adc4)
//   mech_fire_dual_missiles (0x8029b2f4)
//   mech_fire_single_missile(0x8029b6fc)
//   mech_fire_beam_pair     (0x8029b858)
//   mech_beam_burst_tick    (0x8029bdb0)
//   mech_boss_behavior_tick (0x8029be80)
//
// Address range: 0x8029aae4 - 0x8029be80
// Subsystem: game (AI actors / boss)

#include "common.h"

extern "C" {

// --- data globals ----------------------------------------------------------
extern int*  DAT_80612a4c;               // MECH boss control block base (0x4c bytes)
extern int   DAT_80612a38;               // serpent boss block (used by snake3_finisher_react)
extern int   DAT_80612a3c;               // beam-pair fire counter (fire every 3rd tick)
extern int   DAT_80611d78;               // player record table base
extern int   DAT_80611d74;               // active-actor global record base
extern int   DAT_80611b8c;               // zone parameter table base (stride 0x20)
extern char  DAT_805015bd;               // co-op mode flag ('\n')
extern int   DAT_80612610;               // player count
extern int   DAT_806109c0;               // frame-delta ticks (xor bias)
extern int   DAT_8048ca9c;               // per-character effect scale table base
extern char  LAB_8029be18;               // mech render callback (code label in this file's range)

// rodata muzzle / effect offset vectors
extern int   DAT_803e0718, DAT_803e071c, DAT_803e0720, DAT_803e0724, DAT_803e0728, DAT_803e072c;
extern int   DAT_803e0730, DAT_803e0734, DAT_803e0738;
extern int   DAT_803e073c, DAT_803e0740, DAT_803e0744, DAT_803e0748, DAT_803e074c, DAT_803e0750;
extern int   DAT_803e0754, DAT_803e0758, DAT_803e075c, DAT_803e0760, DAT_803e0764, DAT_803e0768;
extern int   DAT_803e076c, DAT_803e0770, DAT_803e0774, DAT_803e0778, DAT_803e077c, DAT_803e0780;
extern int   DAT_803e0784, DAT_803e0788, DAT_803e078c, DAT_803e0790, DAT_803e0794, DAT_803e0798;

// --- float / double constants ----------------------------------------------
extern float FLOAT_80612a40;             // missile-volley timer (state 4)
extern float FLOAT_80612a44;             // single-missile timer (state 5)
extern const float  FLOAT_80619560, FLOAT_80619564, FLOAT_80619568, FLOAT_80619574;
extern const float  FLOAT_80619578, FLOAT_80619580, FLOAT_80619588, FLOAT_8061958c;
extern const float  FLOAT_8061959c, FLOAT_806195a0, FLOAT_806195a4, FLOAT_806195a8;
extern const float  FLOAT_806195ac, FLOAT_806195b0, FLOAT_806195b4, FLOAT_806195c0;
extern const float  FLOAT_806195e4;
extern const float  FLOAT_806195e8, FLOAT_806195f4, FLOAT_806196bc, FLOAT_806196d8;
extern const float  FLOAT_8061963c, FLOAT_80619604, FLOAT_80619608, FLOAT_80619648;
extern const float  FLOAT_8061967c, FLOAT_80619704, FLOAT_80619758, FLOAT_80619770;
extern const float  FLOAT_806197b0, FLOAT_806197f0, FLOAT_806197f4, FLOAT_806197f8;
extern const float  FLOAT_806197fc, FLOAT_80619800, FLOAT_80619804, FLOAT_80619808;
extern const float  FLOAT_8061980c, FLOAT_80619810, FLOAT_80619818;
extern const double DOUBLE_80619610;     // rng bias
extern const double DOUBLE_80619630;     // frame-delta bias

// --- raw actor / render-rig helpers (still FUN_ in the IDB) ----------------
extern int  FUN_803826f0(void);          // rig begin -> actor context (hi=type, lo=ptr)
extern int  FUN_803826e8(void);          // rig begin (master FSM)
extern void FUN_8038273c(void);          // rig end
extern void FUN_80382734(void);          // rig end (master FSM)
extern void FUN_802aeee0(void);          // rope/sub-material render flush
extern void FUN_802adae8(void);          // fx setup
extern void FUN_802aeac4(int a1, int a2, int a3, int a4);  // beam/link effect

// --- named cross-module helpers (call-site typed) --------------------------
extern int  mem_alloc(unsigned long long a1, unsigned long long a2, unsigned long long a3,
    unsigned long long a4, unsigned long long a5, unsigned long long a6, unsigned long long a7,
    unsigned long long a8, int size, int a10, int a11, int a12, int a13, int a14, int a15, int a16);
extern int  entity_handle_resolve(int tag);
extern void leaf_31e8(unsigned int dst, int src, int len);
extern double math_sqrt(double x);
extern unsigned int rng_counter_tick(void);
extern void model_pose_eval(void);
extern void mtx4_set_rotation_x(double angle, int outMtx);
extern void mtx4_set_rotation_y(double angle, float* outMtx);
extern void mtx4_multiply(float* a, float* b, float* out);
extern void mtx4_transform_point(float* mtx, float* pt);
extern void dir_to_angles(float* dir, float* pitch, float* yaw);
extern void snake_mtx3_multiply_upper(int dstMtx, int srcMtx, int rotMtx);
extern void gx_set_sub_material_id(int id);
extern unsigned int sprite_get_texture_id(int id);
extern void entity_build_local_matrix(float* outMtx, int entity, int includePos, int mirror);
extern void entity_register_for_update(int entity, int phase, int cb);
extern unsigned long long entity_set_render_type(int entity, void* cb);

extern double anim_primary_clip_phase(int animCtx);
extern int  anim_primary_clip_userflags(int actor, int clipId);
extern int  anim_is_clip_active(int actor, int clipId);
extern int  anim_active_slot_userflags(int animCtx);
extern unsigned long long anim_play_scaled(double a, double b);

extern void aiactor_retarget_visible_teammate(void);
extern void aiactor_follow_zone_path(void);
extern void aiactor_facing_update(void);
extern void aiactor_leader_zone_update(int actor);

extern int  player_ai_projectile_spawn_simple(double a, double b, int type, float* posBlk,
    int* dirBlk, int owner, int* p7, int p8, int p9, int p10, int classId, int p12);
extern void player_ai_spawn_trail_particle(int a1, int a2, int a3, float* pos, float* dir, int a6);
extern void sound_init_play_params(int paramsPtr);
extern unsigned int sound_play_request(int* params);
extern void sound_set_voice_volume_scale(int v);
extern int  soundpad_find_zone_by_id(int id);
extern unsigned long long statlvl_class_complete(double a, double b, double c, double d,
    unsigned long long e, unsigned long long f, unsigned long long g, unsigned long long h);
extern int  damage_filter_invincible(void);   // used as a render-type callback pointer

} // extern "C"

// forward decls
static void mech_boss_reset(void);
static void mech_fire_dual_missiles(void);
static void mech_fire_single_missile(void);
static void mech_fire_beam_pair(int actor);
static bool mech_beam_burst_tick(int actor);

// Helper: unsigned->float biased idiom used pervasively.
static inline float u32_biased(unsigned int v) {
    return (float)((double)CONCAT44(0x43300000, (unsigned)(v ^ 0x80000000)) - DOUBLE_80619630);
}
static inline float rng_unit(void) {
    return (float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_80619610) / FLOAT_806195e4;
}

// ===========================================================================
// 0x8029aae4  snake3_finisher_react   [SERPENT boss - DAT_80612a38]
// Serpent-boss grab-finisher render callback: while in the grab state (2) and
// past the mid-animation phase, play the finisher clip, request the grab sound,
// set the finisher lean direction (+0x2df8) and force state 0x11. State 6 clears
// a render flag. This function is physically in the mech-boss range but operates
// on the serpent-boss control block.
// ===========================================================================
void snake3_finisher_react(void) {
    int type;
    int actorCtx;
    {
        unsigned long long r = (unsigned long long)(unsigned)FUN_803826f0();
        // FUN_803826f0 returns (hi=type, lo=ptr); re-read via the two halves.
        actorCtx = (int)r;
        type = (int)(r >> 0x20);
    }
    // The engine passes context in a register pair; model as two reads.
    actorCtx = FUN_803826f0();
    type = 2;  // this callback is only invoked in the grab path (type==2)

    if (type == 2) {
        int rec = *(int*)(actorCtx + 0x7c);
        int st = *(int*)(rec + 0x20);
        if ((st != 0x11) && (2 < st) && (st != 0x12)) {
            double phase = anim_primary_clip_phase(*(int*)(actorCtx + 4));
            if ((anim_is_clip_active(actorCtx, 0x812) == 0) || (phase <= (double)FLOAT_80619574)) {
                *(int*)(DAT_80612a38 + 0x2df8) = -1;
            } else if ((anim_is_clip_active(actorCtx, 0x814) != 0) && ((double)FLOAT_80619574 < phase)) {
                *(int*)(DAT_80612a38 + 0x2df8) = 1;
            }
            int blk = DAT_80612a38;
            int params[16];
            sound_init_play_params((int)params);
            *(unsigned char*)&params[10] = 1;
            params[0] = 0x4ff;
            *(float*)&params[9] = FLOAT_80619578;
            params[3] = *(int*)(blk + 0x1cfc);
            params[4] = *(int*)(blk + 0x1d00);
            params[5] = *(int*)(blk + 0x1d04);
            sound_play_request(params);
            anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
            if (*(int*)(rec + 0x20) != 0x11) {
                *(int*)(rec + 0x9d0) = 0x11;
            }
            *(int*)(DAT_80612a38 + 0x2df4) = 0;
        }
    }
    FUN_8038273c();
}

// ===========================================================================
// 0x8029ac68  mech_boss_reset
// Zero the 0x4c-byte MECH control block, then seed the running defaults
// (spin dir idx0xc = 1, intro scale idx0x10 = 1.0).
// ===========================================================================
static void mech_boss_reset(void) {
    float zero = FLOAT_80619568;
    float one  = FLOAT_80619580;
    *DAT_80612a4c = 0;
    for (int i = 1; i <= 0xb; i++) {
        if (i == 0xc) continue;
        *(float*)&DAT_80612a4c[i] = zero;
    }
    *(float*)&DAT_80612a4c[0x10] = one;
    DAT_80612a4c[0xc] = 1;
    DAT_80612a4c[0xd] = 0;
    *(float*)&DAT_80612a4c[0xf] = zero;
    DAT_80612a4c[0xe] = 0;
    DAT_80612a4c[0x11] = 0;
    DAT_80612a4c[0x12] = 0;
}

// ===========================================================================
// 0x8029ad14  mech_boss_alloc
// Allocate the 0x4c-byte control block and reset it. The 16-slot arg list is the
// register/stack image the game passes on; only the block size matters here.
// ===========================================================================
void mech_boss_alloc(unsigned long long param_1, unsigned long long param_2,
    unsigned long long param_3, unsigned long long param_4, unsigned long long param_5,
    unsigned long long param_6, unsigned long long param_7, unsigned long long param_8,
    int param_9, int param_10, int param_11, int param_12, int param_13, int param_14,
    int param_15, int param_16) {
    DAT_80612a4c = (int*)mem_alloc(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                                   param_8, 0x4c, 1, param_11, param_12, param_13, param_14,
                                   param_15, param_16);
    mech_boss_reset();
}

// ===========================================================================
// 0x8029ad44  mech_boss_render
// Bind the boss sub-material for the render pass, when the MECH boss exists, is
// active (+0x48 == 1) and we are not in split-screen co-op.
// ===========================================================================
void mech_boss_render(void) {
    if ((entity_handle_resolve(0x4d454348) != 0) && (DAT_80612a4c != (int*)0) &&
        (*(int*)((int)DAT_80612a4c + 0x48) == 1) &&
        ((DAT_805015bd != '\n') || (DAT_80612610 < 2))) {
        gx_set_sub_material_id(1);
        FUN_802aeee0();
    }
}

// ===========================================================================
// 0x8029adc4  mech_turret_aim_update
// Update the turret aim. When the boss is active (+0x48==1) it snaps toward the
// player yaw; otherwise it steers the yaw/pitch toward the target with a turn
// rate (or, in "spin" mode, sweeps freely). It then applies the resulting
// yaw/pitch rotation to the turret bone (0x80) and its 10 child bones.
// ===========================================================================
void mech_turret_aim_update(void) {
    int actorCtx = FUN_803826f0();
    char* blk = (char*)DAT_80612a4c;
    float pitchTarget = FLOAT_80619568;

    if (*(int*)(blk + 0x48) == 1) {
        *(int*)(blk + 0x28) = *(int*)(actorCtx + 0x70);
        float tgt = *(float*)(blk + 0x20);
        if (*(float*)(blk + 0x28) <= tgt) {
            if (*(float*)(blk + 0x28) < tgt) {
                *(float*)(blk + 0x20) = tgt - *(float*)(blk + 0x40);
                if (*(float*)(blk + 0x20) < *(float*)(blk + 0x28)) *(float*)(blk + 0x20) = *(float*)(blk + 0x28);
            }
        } else {
            *(float*)(blk + 0x20) = tgt + *(float*)(blk + 0x40);
            if (*(float*)(blk + 0x28) < *(float*)(blk + 0x20)) *(float*)(blk + 0x20) = *(float*)(blk + 0x28);
        }
    } else if ((*(int*)(blk + 0x34) == 0) || (*(int*)(blk + 0x38) == 1)) {
        float dir[3];
        float yaw;
        dir[0] = *(float*)(blk + 0x10) - *(float*)(blk + 4);
        dir[1] = *(float*)(blk + 0x14) - (*(float*)(blk + 8) - FLOAT_806195f4);
        dir[2] = *(float*)(blk + 0x18) - *(float*)(blk + 0xc);
        dir_to_angles(dir, &pitchTarget, &yaw);
        yaw = yaw + FLOAT_806195c0;   // (FLOAT_806195c0 declared via serpent set; see below)
        if (FLOAT_80619568 <= yaw) {
            if (FLOAT_806195ac < yaw) yaw -= FLOAT_806195ac;
        } else {
            yaw += FLOAT_806195ac;
        }
        *(int*)(blk + 0x1c) = *(int*)(actorCtx + 0x70);
        *(float*)(blk + 0x28) = yaw;
        float cur = *(float*)(blk + 0x20);
        float diff = cur - *(float*)(blk + 0x28);
        if (diff <= FLOAT_8061958c) {
            if (diff < FLOAT_806195b0) *(float*)(blk + 0x20) = cur + FLOAT_806195ac;
        } else {
            *(float*)(blk + 0x20) = cur - FLOAT_806195ac;
        }
        cur = *(float*)(blk + 0x20);
        if (*(float*)(blk + 0x28) <= cur) {
            if (*(float*)(blk + 0x28) < cur) {
                *(float*)(blk + 0x20) = cur - *(float*)(blk + 0x40);
                *(int*)(blk + 0x30) = -1;
                if (*(float*)(blk + 0x20) < *(float*)(blk + 0x28)) *(float*)(blk + 0x20) = *(float*)(blk + 0x28);
            }
        } else {
            *(float*)(blk + 0x20) = cur + *(float*)(blk + 0x40);
            *(int*)(blk + 0x30) = 1;
            if (*(float*)(blk + 0x28) < *(float*)(blk + 0x20)) *(float*)(blk + 0x20) = *(float*)(blk + 0x28);
        }
        if ((*(int*)(blk + 0x38) == 1) && (*(int*)(blk + 0x34) == 0)) {
            *(int*)(blk + 0x38) = 0;
        } else {
            pitchTarget = FLOAT_80619568;
        }
    } else {
        *(float*)(blk + 0x20) = FLOAT_80619604 * u32_biased((unsigned)*(int*)(blk + 0x30)) + *(float*)(blk + 0x20);
        float cur = *(float*)(blk + 0x20);
        if (FLOAT_80619568 <= cur) {
            if (FLOAT_806195ac < cur) *(float*)(blk + 0x20) = cur - FLOAT_806195ac;
        } else {
            *(float*)(blk + 0x20) = cur + FLOAT_806195ac;
        }
    }

    float yawDelta = *(float*)(blk + 0x20) - *(float*)(actorCtx + 0x70);
    model_pose_eval();
    float curPitch = *(float*)(blk + 0x24);
    if (curPitch <= pitchTarget) {
        if ((curPitch < pitchTarget) &&
            (*(float*)(blk + 0x24) = curPitch + FLOAT_80619574, pitchTarget < *(float*)(blk + 0x24))) {
            *(float*)(blk + 0x24) = pitchTarget;
        }
    } else {
        *(float*)(blk + 0x24) = curPitch - FLOAT_80619574;
        if (*(float*)(blk + 0x24) < pitchTarget) *(float*)(blk + 0x24) = pitchTarget;
    }

    float rotY[17];
    float rotX[16];
    unsigned char scratch[64];
    mtx4_set_rotation_y((double)((FLOAT_80619588 * yawDelta) / FLOAT_8061958c), rotY);
    mtx4_set_rotation_x((double)((FLOAT_80619588 * *(float*)(blk + 0x24)) / FLOAT_8061958c), (int)(uintptr_t)rotX);

    int base = *(int*)(*(int*)(actorCtx + 4) + 0x10);
    double px = (double)*(float*)(base + 0xb0);
    double py = (double)*(float*)(base + 0xb4);
    double pz = (double)*(float*)(base + 0xb8);
    *(float*)(base + 0xb0) = FLOAT_80619568;
    *(float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + 0xb4) = FLOAT_80619568;
    *(float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + 0xb8) = FLOAT_80619568;
    float* turret = (float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + 0x80);
    mtx4_multiply(turret, rotX, turret);
    *(float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + 0xb0) = (float)px;
    *(float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + 0xb4) = (float)py;
    *(float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + 0xb8) = (float)pz;
    leaf_31e8((unsigned int)(uintptr_t)scratch, *(int*)(*(int*)(actorCtx + 4) + 0x10) + 0x80, 0x40);
    snake_mtx3_multiply_upper(*(int*)(*(int*)(actorCtx + 4) + 0x10) + 0x80,
                              (int)(uintptr_t)scratch, (int)(uintptr_t)rotY);

    int child = 0xc0;
    for (int i = 3; i < 0xd; i++) {
        int b = *(int*)(*(int*)(actorCtx + 4) + 0x10);
        *(float*)(b + child + 0x30) = (float)((double)*(float*)(b + child + 0x30) - px);
        b = *(int*)(*(int*)(actorCtx + 4) + 0x10);
        *(float*)(b + child + 0x34) = (float)((double)*(float*)(b + child + 0x34) - py);
        b = *(int*)(*(int*)(actorCtx + 4) + 0x10);
        *(float*)(b + child + 0x38) = (float)((double)*(float*)(b + child + 0x38) - pz);
        float* cm = (float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + child);
        mtx4_multiply(cm, rotX, cm);
        b = *(int*)(*(int*)(actorCtx + 4) + 0x10);
        *(float*)(b + child + 0x30) = (float)((double)*(float*)(b + child + 0x30) + px);
        b = *(int*)(*(int*)(actorCtx + 4) + 0x10);
        *(float*)(b + child + 0x34) = (float)((double)*(float*)(b + child + 0x34) + py);
        b = *(int*)(*(int*)(actorCtx + 4) + 0x10);
        *(float*)(b + child + 0x38) = (float)((double)*(float*)(b + child + 0x38) + pz);
        cm = (float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + child);
        mtx4_multiply(cm, rotY, cm);
        child += 0x40;
    }
    FUN_8038273c();
}

// ===========================================================================
// 0x8029b2f4  mech_fire_dual_missiles
// Fire two homing missiles from the boss's two shoulder muzzle bones with an
// RNG-jittered spread (projectile class 0x10000e, type 0xb). Damage scale is
// halved when the "fast" flag (+0x44) is set.
// ===========================================================================
static void mech_fire_dual_missiles(void) {
    int actorCtx = FUN_803826f0();
    float muzzleA[3] = {*(float*)&DAT_803e0718, *(float*)&DAT_803e071c, *(float*)&DAT_803e0720};
    float muzzleB[3] = {*(float*)&DAT_803e0724, *(float*)&DAT_803e0728, *(float*)&DAT_803e072c};
    int material = *(int*)(DAT_80611d78 + 0x14);
    double dmg = (double)((*(int*)((int)DAT_80612a4c + 0x44) == 1) ? FLOAT_80619564 : FLOAT_80619574);
    float localMtx[16];
    float pos[3], dir[3], out[3];

    int* skel = *(int**)(*(int*)(actorCtx + 4) + 4);
    int n = *skel;
    entity_build_local_matrix(localMtx, actorCtx, 1, 0);

    // Muzzle A.
    mtx4_transform_point((float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + *(char*)((int)skel + n * -0xc0 + 0x7bd) * 0x40), muzzleA);
    mtx4_transform_point(localMtx, muzzleA);
    pos[0] = muzzleA[0]; pos[1] = muzzleA[1]; pos[2] = muzzleA[2];
    mtx4_multiply((float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + *(char*)((int)skel + n * -0xc0 + 0x7bd) * 0x40), localMtx, out);
    dir[0] = (float)((double)FLOAT_806195a8 * (double)(rng_unit()) + (double)out[0]) - FLOAT_806195a4;
    dir[1] = (float)((double)FLOAT_806195a8 * (double)(rng_unit()) + (double)out[1]) - FLOAT_806195a4;
    dir[2] = (float)((double)FLOAT_806195a8 * (double)(rng_unit()) + (double)out[2]) - FLOAT_806195a4;
    player_ai_spawn_trail_particle(0, 0, 0xffffffff, pos, dir, -1);
    int proj = player_ai_projectile_spawn_simple(dmg, (double)FLOAT_806195a8, 0xb, pos, (int*)dir,
        actorCtx, (int*)0, 6, 0, *(int*)(actorCtx + 0x34), 0x10000e, 0x16);
    *(int*)(proj + 0x19c) = material;

    // Muzzle B.
    skel = *(int**)(*(int*)(actorCtx + 4) + 4);
    n = *skel;
    mtx4_transform_point((float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + *(char*)((int)skel + n * -0xc0 + 0xc3d) * 0x40), muzzleB);
    mtx4_transform_point(localMtx, muzzleB);
    pos[0] = muzzleB[0]; pos[1] = muzzleB[1]; pos[2] = muzzleB[2];
    mtx4_multiply((float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + *(char*)((int)skel + n * -0xc0 + 0xc3d) * 0x40), localMtx, out);
    dir[0] = (float)((double)FLOAT_806195a8 * (double)(rng_unit()) + (double)out[0]) - FLOAT_806195a4;
    dir[1] = (float)((double)FLOAT_806195a8 * (double)(rng_unit()) + (double)out[1]) - FLOAT_806195a4;
    dir[2] = (float)((double)FLOAT_806195a8 * (double)(rng_unit()) + (double)out[2]) - FLOAT_806195a4;
    player_ai_spawn_trail_particle(0, 0, 0xffffffff, pos, dir, -1);
    proj = player_ai_projectile_spawn_simple(dmg, (double)FLOAT_806195a8, 0xb, pos, (int*)dir,
        actorCtx, (int*)0, 6, 0, *(int*)(actorCtx + 0x34), 0x10000e, 0x16);
    *(int*)(proj + 0x19c) = material;
    FUN_8038273c();
}

// ===========================================================================
// 0x8029b6fc  mech_fire_single_missile
// Fire a single downward-angled missile from the boss's central muzzle bone
// (projectile type 9, class 0x10000e).
// ===========================================================================
static void mech_fire_single_missile(void) {
    int actorCtx = FUN_803826f0();
    float muzzle[3] = {*(float*)&DAT_803e0730, *(float*)&DAT_803e0734, *(float*)&DAT_803e0738};
    int material = *(int*)(DAT_80611d78 + 0x14);
    float localMtx[22];
    float basis[8];

    entity_build_local_matrix(localMtx, actorCtx, 1, 0);
    int* skel = *(int**)(*(int*)(actorCtx + 4) + 4);
    int n = *skel;
    mtx4_transform_point((float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + *(char*)((int)skel + n * -0xc0 + 0x33d) * 0x40), muzzle);
    mtx4_transform_point(localMtx, muzzle);
    mtx4_multiply((float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + *(char*)((int)skel + n * -0xc0 + 0x33d) * 0x40), localMtx, basis);
    float dir[3];
    dir[0] = FLOAT_806195a0 * basis[4];
    dir[2] = FLOAT_806195a0 * basis[6];
    dir[1] = FLOAT_806195a0 * basis[5] - FLOAT_80619580;
    player_ai_spawn_trail_particle(0, 0, 0xffffffff, muzzle, dir, -1);
    int proj = player_ai_projectile_spawn_simple((double)FLOAT_80619580, (double)FLOAT_806195a8, 9,
        muzzle, (int*)dir, actorCtx, (int*)0, 6, 0, *(int*)(actorCtx + 0x34), 0x10000e, 0x16);
    *(int*)(proj + 0x19c) = material;
    FUN_8038273c();
}

// ===========================================================================
// 0x8029b858  mech_fire_beam_pair
// Fire the swept-beam projectile pair from the two shoulder bones, aimed toward
// the current target with a jittered spread. Fired only every 3rd tick
// (DAT_80612a3c counter). The oscillation timer (+0x2c) sweeps the aim.
// ===========================================================================
static void mech_fire_beam_pair(int actor) {
    char* blk = (char*)DAT_80612a4c;
    float muzzleA[3] = {*(float*)&DAT_803e073c, *(float*)&DAT_803e0740, *(float*)&DAT_803e0744};
    float muzzleB[3] = {*(float*)&DAT_803e0748, *(float*)&DAT_803e074c, *(float*)&DAT_803e0750};

    *(float*)(blk + 0x2c) = *(float*)(blk + 0x2c) + FLOAT_80619648;
    if (FLOAT_806195b4 < *(float*)(blk + 0x2c)) {
        *(int*)(blk + 0x30) = -*(int*)(blk + 0x30);
        *(float*)(blk + 0x2c) = FLOAT_80619568;
    }
    DAT_80612a3c = DAT_80612a3c + 1;
    if (2 < DAT_80612a3c) {
        float localMtx[16];
        float pos[3], dir[3], out[3];
        int params[16];
        int* skel = *(int**)(*(int*)(actor + 4) + 4);
        int n = *skel;
        entity_build_local_matrix(localMtx, actor, 1, 0);

        // Beam A: aim from muzzle toward the target.
        mtx4_transform_point((float*)(*(int*)(*(int*)(actor + 4) + 0x10) + *(char*)((int)skel + n * -0xc0 + 0x7bd) * 0x40), muzzleA);
        mtx4_transform_point(localMtx, muzzleA);
        pos[0] = muzzleA[0]; pos[1] = muzzleA[1]; pos[2] = muzzleA[2];
        double dy = (double)(*(float*)(blk + 0x14) - muzzleA[1]);
        double dx = (double)(*(float*)(blk + 0x10) - muzzleA[0]);
        double dz = (double)(*(float*)(blk + 0x18) - muzzleA[2]);
        double inv = (double)(float)((double)FLOAT_80619580 / math_sqrt((double)(float)(dz*dz + (double)(float)(dx*dx + (double)(float)(dy*dy)))));
        double ax = (double)(float)(dx*inv), ay = (double)(float)(dy*inv), az = (double)(float)(dz*inv);
        mtx4_multiply((float*)(*(int*)(*(int*)(actor + 4) + 0x10) + *(char*)((int)skel + n * -0xc0 + 0x7bd) * 0x40), localMtx, out);
        dir[0] = (FLOAT_806196d8 * rng_unit() + (float)((double)FLOAT_806195e8 * (double)out[0] + (double)(float)((double)FLOAT_8061963c * ax))) - FLOAT_806197f0;
        dir[1] = (FLOAT_806196d8 * rng_unit() + (float)((double)FLOAT_806195e8 * (double)out[1] + (double)(float)((double)FLOAT_8061963c * ay))) - FLOAT_806197f0;
        dir[2] = (FLOAT_806196d8 * rng_unit() + (float)((double)FLOAT_806195e8 * (double)out[2] + (double)(float)((double)FLOAT_8061963c * az))) - FLOAT_806197f0;
        sound_init_play_params((int)params);
        *(unsigned char*)&params[10] = 1;
        params[0] = 0x4e7;
        params[3] = *(int*)(actor + 100);
        params[4] = *(int*)(actor + 0x68);
        params[5] = *(int*)(actor + 0x6c);
        sound_play_request(params);
        player_ai_spawn_trail_particle(0, 0, 0xffffffff, pos, dir, -1);
        player_ai_projectile_spawn_simple((double)FLOAT_8061959c, (double)FLOAT_806195a8, 4,
            pos, (int*)dir, actor, (int*)0, 6, 0, *(int*)(actor + 0x34), 0x10000e, 2);

        // Beam B.
        skel = *(int**)(*(int*)(actor + 4) + 4);
        n = *skel;
        mtx4_transform_point((float*)(*(int*)(*(int*)(actor + 4) + 0x10) + *(char*)((int)skel + n * -0xc0 + 0xc3d) * 0x40), muzzleB);
        mtx4_transform_point(localMtx, muzzleB);
        pos[0] = muzzleB[0]; pos[1] = muzzleB[1]; pos[2] = muzzleB[2];
        float ddx = *(float*)(blk + 0x10) - muzzleB[0];
        float ddy = *(float*)(blk + 0x14) - muzzleB[1];
        float ddz = *(float*)(blk + 0x18) - muzzleB[2];
        double invB = (double)(float)((double)FLOAT_80619580 / math_sqrt((double)(ddz*ddz + (float)(ddx*(double)ddx + (double)(ddy*ddy)))));
        double aimB = (double)(float)((double)FLOAT_8061963c * (double)(float)(ddx * invB));
        mtx4_multiply((float*)(*(int*)(*(int*)(actor + 4) + 0x10) + *(char*)((int)skel + n * -0xc0 + 0xc3d) * 0x40), localMtx, out);
        dir[0] = (FLOAT_806196d8 * rng_unit() + (float)((double)FLOAT_806195e8 * (double)out[0] + aimB)) - FLOAT_806197f0;
        dir[1] = (FLOAT_806196d8 * rng_unit() + (float)((double)FLOAT_806195e8 * (double)out[1] + aimB)) - FLOAT_806197f0;
        dir[2] = (FLOAT_806196d8 * rng_unit() + (float)((double)FLOAT_806195e8 * (double)out[2] + aimB)) - FLOAT_806197f0;
        player_ai_spawn_trail_particle(0, 0, 0xffffffff, pos, dir, -1);
        player_ai_projectile_spawn_simple((double)FLOAT_8061959c, (double)FLOAT_806195a8, 4,
            pos, (int*)dir, actor, (int*)0, 6, 0, *(int*)(actor + 0x34), 0x10000e, 2);
        DAT_80612a3c = 0;
    }
}

// ===========================================================================
// 0x8029bdb0  mech_beam_burst_tick
// Advance the beam-burst timer (+0x3c), fire the beam pair each tick, and report
// completion once the burst duration elapses (clearing the beam-active flag).
// ===========================================================================
static bool mech_beam_burst_tick(int actor) {
    *(float*)((int)DAT_80612a4c + 0x3c) = *(float*)((int)DAT_80612a4c + 0x3c) + FLOAT_806197f4;
    mech_fire_beam_pair(actor);
    bool done = FLOAT_80619580 < *(float*)((int)DAT_80612a4c + 0x3c);
    if (done) {
        *(float*)((int)DAT_80612a4c + 0x3c) = FLOAT_80619568;
        *(int*)((int)DAT_80612a4c + 0x34) = 0;
    }
    return done;
}

// ===========================================================================
// 0x8029be80  mech_boss_behavior_tick
// The MECH boss master per-frame FSM (state = actor +0x20, range 0..6):
//   0  spawn: mark invincible, set up fx/scale, go state 1.
//   1  intro: play spawn clip; once it finishes register the aim-update phase,
//      bind the mech render callback, resolve the initial audio zone, go state 2.
//   2  combat: pick attack sub-state from target distance; run the beam burst;
//      update the audio zone from the zone parameter table (DAT_80611b8c); drive
//      the follow/facing/leader updates.
//   3  approach dwell; 4 missile volley (timer FLOAT_80612a40 -> mech_fire_dual);
//   5  single-missile pass (timer FLOAT_80612a44 -> mech_fire_single);
//   6  death: spawn the two/three bone finisher effects and complete the class.
// ===========================================================================
void mech_boss_behavior_tick(void) {
    int actorCtx = FUN_803826e8();
    int rec = *(int*)(actorCtx + 0x7c);
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
        *(float*)(rec + 0xa90) = FLOAT_806195f4 * (FLOAT_80619580 / u32_biased((unsigned)DAT_806109c0)) + *(float*)(rec + 0xa90);
    }

    int viewRec = *(int*)(DAT_80611d78 + 0x14);
    *(float*)(rec + 0xb2c) = FLOAT_80619770;
    DAT_80612a4c[4] = *(int*)(viewRec + 100);
    DAT_80612a4c[5] = *(int*)(viewRec + 0x68);
    DAT_80612a4c[6] = *(int*)(viewRec + 0x6c);
    DAT_80612a4c[1] = *(int*)(actorCtx + 100);
    DAT_80612a4c[2] = *(int*)(actorCtx + 0x68);
    DAT_80612a4c[3] = *(int*)(actorCtx + 0x6c);
    *(unsigned int*)(rec + 0x28) |= 0x4000;
    *(float*)(rec + 0xb3c) = FLOAT_80619568;

    switch (*(int*)(rec + 0x20)) {
    case 0:
        *DAT_80612a4c = 1;
        *(unsigned int*)(rec + 0x28) |= 0x4000000;
        entity_set_render_type(actorCtx, (void*)&damage_filter_invincible);
        sprite_get_texture_id(0x300);
        FUN_802adae8();
        *(float*)(actorCtx + 0x1cc) = FLOAT_806197f8;
        *(float*)(actorCtx + 0x1d8) = FLOAT_806197f8;
        *(int*)(DAT_80611d74 + 0xe8c) = actorCtx;
        if (*(int*)(rec + 0x20) != 1) {
            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
            *(int*)(rec + 0x20) = 1;
            *(float*)(rec + 0x9d8) = FLOAT_80619568;
            *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
        }
        break;
    case 1:
        if (anim_is_clip_active(actorCtx, 0x807) == 0) {
            anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_80619580);
        }
        if (anim_active_slot_userflags(*(int*)(actorCtx + 4)) != 0) {
            entity_register_for_update(actorCtx, 2, -0x7fd6523c);
            if (*(int*)(rec + 0x20) != 2) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20) = 2;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
            entity_set_render_type(actorCtx, (void*)&LAB_8029be18);
            anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_806197fc);
            int zone = soundpad_find_zone_by_id(0x46e);
            if (zone < 0) {
                *(int*)(rec + 0x684) = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            } else {
                *(int*)(rec + 0x684) = soundpad_find_zone_by_id(0x46e);
            }
            *(int*)(rec + 0x9ac) = 0;
            *(int*)(rec + 0x9a8) = 0;
        }
        break;
    case 2: {
        if (anim_primary_clip_userflags(actorCtx, 0x808) != 0) {
            float d2 = (*(float*)&DAT_80612a4c[1] - *(float*)&DAT_80612a4c[4]) * (*(float*)&DAT_80612a4c[1] - *(float*)&DAT_80612a4c[4]) +
                       (*(float*)&DAT_80612a4c[3] - *(float*)&DAT_80612a4c[6]) * (*(float*)&DAT_80612a4c[3] - *(float*)&DAT_80612a4c[6]);
            if ((FLOAT_80619800 <= d2) || (DAT_80612a4c[0xd] != 0)) {
                bool early = false;
                if (DAT_80612a4c[0x11] == 0) {
                    if (FLOAT_80619758 <= rng_unit()) {
                        anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_806197fc);
                        early = true;
                    }
                }
                if (!early) {
                    *(float*)&DAT_80612a4c[0x10] = FLOAT_80619648;
                    if (*(int*)(rec + 0x20) != 4) {
                        *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                        *(int*)(rec + 0x20) = 4;
                        *(float*)(rec + 0x9d8) = FLOAT_80619568;
                        *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                    }
                }
            } else if (FLOAT_806197b0 <= d2) {
                if (FLOAT_80619804 <= d2) {
                    bool retreat = false;
                    if (d2 < FLOAT_806196bc) {
                        if (rng_unit() < FLOAT_80619574) {
                            *(float*)&DAT_80612a4c[0x10] = FLOAT_80619648;
                            if (*(int*)(rec + 0x20) != 4) {
                                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                                *(int*)(rec + 0x20) = 4;
                                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                            }
                            retreat = true;
                        }
                    }
                    if (!retreat) {
                        DAT_80612a4c[0xd] = 1;
                        anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_806197fc);
                    }
                } else {
                    DAT_80612a4c[0xd] = 1;
                    *(float*)&DAT_80612a4c[0x10] = FLOAT_80619648;
                    if (*(int*)(rec + 0x20) != 3) {
                        *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                        *(int*)(rec + 0x20) = 3;
                        *(float*)(rec + 0x9d8) = FLOAT_80619568;
                        *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                    }
                }
            } else {
                *(float*)&DAT_80612a4c[0x10] = FLOAT_80619648;
                if (*(int*)(rec + 0x20) != 5) {
                    *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                    *(int*)(rec + 0x20) = 5;
                    *(float*)(rec + 0x9d8) = FLOAT_80619568;
                    *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                }
            }
        }

        // Audio-zone transition based on the zone parameter table.
        if ((*(int*)(rec + 0x678) == *(int*)(rec + 0x684)) && (*(int*)(rec + 0x9b8) == 0) &&
            (*(int*)(rec + 4) != 0)) {
            int srcZone = *(int*)(*(int*)(*(int*)(rec + 4) + 0x7c) + 0x678);
            if (*(int*)(rec + 0x684) != srcZone) {
                int zoneOff = srcZone * 0x20;
                float p14 = *(float*)(DAT_80611b8c + zoneOff + 0x14);
                if (p14 <= FLOAT_80619808) {
                    DAT_80612a4c[0x11] = 1;
                    float p0c = *(float*)(DAT_80611b8c + zoneOff + 0xc);
                    int zid;
                    if (FLOAT_80619578 <= p0c) {
                        zid = (p0c <= FLOAT_80619810) ? 0x690 : 0x468;
                    } else {
                        zid = 0x473;
                    }
                    int z = soundpad_find_zone_by_id(zid);
                    if (z < 0) *(int*)(rec + 0x684) = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                    else *(int*)(rec + 0x684) = soundpad_find_zone_by_id(zid);
                    *(int*)(rec + 0x9ac) = 0;
                    *(int*)(rec + 0x9a8) = 0;
                } else if (FLOAT_8061980c <= p14) {
                    DAT_80612a4c[0x11] = 1;
                    float p0c = *(float*)(DAT_80611b8c + zoneOff + 0xc);
                    int zid;
                    if (FLOAT_80619578 <= p0c) {
                        zid = (p0c <= FLOAT_80619810) ? 0x466 : 0x465;
                    } else {
                        zid = 0x6a4;
                    }
                    int z = soundpad_find_zone_by_id(zid);
                    if (z < 0) *(int*)(rec + 0x684) = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                    else *(int*)(rec + 0x684) = soundpad_find_zone_by_id(zid);
                    *(int*)(rec + 0x9ac) = 0;
                    *(int*)(rec + 0x9a8) = 0;
                } else {
                    DAT_80612a4c[0x11] = 0;
                    int z = *(int*)(*(int*)(*(int*)(rec + 4) + 0x7c) + 0x678);
                    if (z < 0) *(int*)(rec + 0x684) = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                    else *(int*)(rec + 0x684) = z;
                    *(int*)(rec + 0x9ac) = 0;
                    *(int*)(rec + 0x9a8) = 0;
                }
            }
        }
        if ((DAT_80612a4c[0xd] == 1) && mech_beam_burst_tick(actorCtx)) {
            DAT_80612a4c[0xd] = 0;
        }
        aiactor_follow_zone_path();
        aiactor_facing_update();
        aiactor_leader_zone_update(actorCtx);
        break;
    }
    case 3:
        if (DAT_80612a4c[0xd] == 1) {
            if (mech_beam_burst_tick(actorCtx)) {
                float d2 = (*(float*)&DAT_80612a4c[1] - *(float*)&DAT_80612a4c[4]) * (*(float*)&DAT_80612a4c[1] - *(float*)&DAT_80612a4c[4]) +
                           (*(float*)&DAT_80612a4c[3] - *(float*)&DAT_80612a4c[6]) * (*(float*)&DAT_80612a4c[3] - *(float*)&DAT_80612a4c[6]);
                if (d2 <= FLOAT_80619804) {
                    if (d2 < FLOAT_806197b0) {
                        *(float*)&DAT_80612a4c[0x10] = FLOAT_80619648;
                        if (*(int*)(rec + 0x20) != 5) {
                            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                            *(int*)(rec + 0x20) = 5;
                            *(float*)(rec + 0x9d8) = FLOAT_80619568;
                            *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                        }
                    }
                } else {
                    anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_806197fc);
                    *(float*)&DAT_80612a4c[0x10] = FLOAT_80619580;
                    if (*(int*)(rec + 0x20) != 2) {
                        *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                        *(int*)(rec + 0x20) = 2;
                        *(float*)(rec + 0x9d8) = FLOAT_80619568;
                        *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                    }
                }
            }
        } else {
            *(float*)&DAT_80612a4c[0xf] = *(float*)&DAT_80612a4c[0xf] + FLOAT_80619560;
            if (FLOAT_80619580 < *(float*)&DAT_80612a4c[0xf]) {
                DAT_80612a4c[0xd] = 1;
                *(float*)&DAT_80612a4c[0xf] = FLOAT_80619568;
            }
        }
        break;
    case 4:
        FLOAT_80612a40 = FLOAT_80612a40 + FLOAT_80619704;
        if (FLOAT_80619580 < FLOAT_80612a40) {
            mech_fire_dual_missiles();
            FLOAT_80612a40 = FLOAT_80619568;
            *(float*)&DAT_80612a4c[0x10] = FLOAT_80619580;
            anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_806197fc);
            if (*(int*)(rec + 0x20) != 2) {
                *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                *(int*)(rec + 0x20) = 2;
                *(float*)(rec + 0x9d8) = FLOAT_80619568;
                *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
            }
        }
        break;
    case 5:
        FLOAT_80612a44 = FLOAT_80612a44 + FLOAT_8061967c;
        if (FLOAT_80619580 < FLOAT_80612a44) {
            mech_fire_single_missile();
            FLOAT_80612a44 = FLOAT_80619568;
            *(float*)&DAT_80612a4c[0x10] = FLOAT_80619580;
            float d2 = (*(float*)&DAT_80612a4c[1] - *(float*)&DAT_80612a4c[4]) * (*(float*)&DAT_80612a4c[1] - *(float*)&DAT_80612a4c[4]) +
                       (*(float*)&DAT_80612a4c[3] - *(float*)&DAT_80612a4c[6]) * (*(float*)&DAT_80612a4c[3] - *(float*)&DAT_80612a4c[6]);
            if (FLOAT_806197b0 < d2) {
                if (FLOAT_80619804 <= d2) {
                    anim_play_scaled((double)FLOAT_80619568, (double)FLOAT_806197fc);
                    if (*(int*)(rec + 0x20) != 2) {
                        *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                        *(int*)(rec + 0x20) = 2;
                        *(float*)(rec + 0x9d8) = FLOAT_80619568;
                        *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                    }
                } else {
                    DAT_80612a4c[0xd] = 1;
                    *(float*)&DAT_80612a4c[0x10] = FLOAT_80619648;
                    if (*(int*)(rec + 0x20) != 3) {
                        *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                        *(int*)(rec + 0x20) = 3;
                        *(float*)(rec + 0x9d8) = FLOAT_80619568;
                        *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                    }
                }
            }
        }
        break;
    case 6: {
        double phase = anim_primary_clip_phase(*(int*)(actorCtx + 4));
        float localMtx[16];
        if (phase <= (double)FLOAT_80619608) {
            if (phase <= (double)FLOAT_80619574) {
                float a[3] = {*(float*)&DAT_803e0778, *(float*)&DAT_803e077c, *(float*)&DAT_803e0780};
                float b[3] = {*(float*)&DAT_803e0784, *(float*)&DAT_803e0788, *(float*)&DAT_803e078c};
                float c[3] = {*(float*)&DAT_803e0790, *(float*)&DAT_803e0794, *(float*)&DAT_803e0798};
                entity_build_local_matrix(localMtx, actorCtx, 1, 0);
                int* skel = *(int**)(*(int*)(actorCtx + 4) + 4);
                int n = *skel;
                float* bp = (float*)skel[n * -0x30 + 0x28];
                a[0] = FLOAT_80619574 * (bp[3] - bp[0]) + bp[0];
                a[1] = FLOAT_80619574 * (bp[4] - bp[1]) + bp[1];
                a[2] = FLOAT_80619574 * (bp[5] - bp[2]) + bp[2];
                mtx4_transform_point((float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + *(char*)((int)skel + n * -0xc0 + 0x3d) * 0x40), a);
                mtx4_transform_point(localMtx, a);
                FUN_802aeac4((int)(uintptr_t)b, (int)(uintptr_t)a, (int)(uintptr_t)c, (int)(uintptr_t)a);
            } else {
                float a[3] = {*(float*)&DAT_803e0760, *(float*)&DAT_803e0764, *(float*)&DAT_803e0768};
                float b[3] = {*(float*)&DAT_803e076c, *(float*)&DAT_803e0770, *(float*)&DAT_803e0774};
                entity_build_local_matrix(localMtx, actorCtx, 1, 0);
                int* skel = *(int**)(*(int*)(actorCtx + 4) + 4);
                int n = *skel;
                float* bp = (float*)skel[n * -0x30 + 0x1a8];
                a[0] = FLOAT_80619574 * (bp[3] - bp[0]) + bp[0];
                a[1] = FLOAT_80619574 * (bp[4] - bp[1]) + bp[1];
                a[2] = FLOAT_80619574 * (bp[5] - bp[2]) + bp[2];
                mtx4_transform_point((float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + *(char*)((int)skel + n * -0xc0 + 0x63d) * 0x40), a);
                mtx4_transform_point(localMtx, a);
                skel = *(int**)(*(int*)(actorCtx + 4) + 4);
                n = *skel;
                bp = (float*)skel[n * -0x30 + 0x28];
                float mid[3];
                mid[0] = FLOAT_80619574 * (bp[3] - bp[0]) + bp[0];
                mid[1] = FLOAT_80619574 * (bp[4] - bp[1]) + bp[1];
                mid[2] = FLOAT_80619574 * (bp[5] - bp[2]) + bp[2];
                mtx4_transform_point((float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + *(char*)((int)skel + n * -0xc0 + 0x3d) * 0x40), mid);
                mtx4_transform_point(localMtx, mid);
                FUN_802aeac4((int)(uintptr_t)a, (int)(uintptr_t)mid, (int)(uintptr_t)b, (int)(uintptr_t)a);
            }
        } else {
            float a[3] = {*(float*)&DAT_803e0754, *(float*)&DAT_803e0758, *(float*)&DAT_803e075c};
            entity_build_local_matrix(localMtx, actorCtx, 1, 0);
            int* skel = *(int**)(*(int*)(actorCtx + 4) + 4);
            int n = *skel;
            float* bp = (float*)skel[n * -0x30 + 0x1a8];
            float p0[3], p1[3], p2[3];
            p0[0] = FLOAT_80619574 * (bp[3] - bp[0]) + bp[0];
            p0[1] = FLOAT_80619574 * (bp[4] - bp[1]) + bp[1];
            p0[2] = FLOAT_80619574 * (bp[5] - bp[2]) + bp[2];
            mtx4_transform_point((float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + *(char*)((int)skel + n * -0xc0 + 0x63d) * 0x40), p0);
            mtx4_transform_point(localMtx, p0);
            skel = *(int**)(*(int*)(actorCtx + 4) + 4); n = *skel;
            bp = (float*)skel[n * -0x30 + 0x28];
            p1[0] = FLOAT_80619574 * (bp[3] - bp[0]) + bp[0];
            p1[1] = FLOAT_80619574 * (bp[4] - bp[1]) + bp[1];
            p1[2] = FLOAT_80619574 * (bp[5] - bp[2]) + bp[2];
            mtx4_transform_point((float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + *(char*)((int)skel + n * -0xc0 + 0x3d) * 0x40), p1);
            mtx4_transform_point(localMtx, p1);
            skel = *(int**)(*(int*)(actorCtx + 4) + 4); n = *skel;
            bp = (float*)skel[n * -0x30 + 0x418];
            p2[0] = FLOAT_80619574 * (bp[3] - bp[0]) + bp[0];
            p2[1] = FLOAT_80619574 * (bp[4] - bp[1]) + bp[1];
            p2[2] = FLOAT_80619574 * (bp[5] - bp[2]) + bp[2];
            mtx4_transform_point((float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + *(char*)((int)skel + n * -0xc0 + 0xffd) * 0x40), p2);
            mtx4_transform_point(localMtx, p2);
            FUN_802aeac4((int)(uintptr_t)p0, (int)(uintptr_t)p1, (int)(uintptr_t)p2, (int)(uintptr_t)a);
        }
        float* scale = (float*)(&DAT_8048ca9c + *(short*)(rec + 0x48) * 0x30);
        if (*scale < FLOAT_80619818) *scale = *scale + FLOAT_806197f0;
        if (anim_active_slot_userflags(*(int*)(actorCtx + 4)) != 0) {
            sound_set_voice_volume_scale(0);
            statlvl_class_complete((double)FLOAT_80619574, 0.0, 0.0, 0.0, 0, 0, 0, 0);
            *(int*)(DAT_80611d74 + 0xe8c) = 0;
        }
        break;
    }
    }
    FUN_80382734();
}
