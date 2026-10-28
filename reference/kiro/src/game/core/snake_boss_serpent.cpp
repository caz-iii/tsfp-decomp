// snake_boss_serpent.cpp - a second segmented "serpent"/spine boss actor built
// on the DAT_80612a38 control block. This is a separate actor from the snake
// boss (DAT_80612a58, snake_boss_*.cpp) but shares the segmented-body matrix
// helpers (snake_mtx3_multiply_upper / snake_mtx3_scale_init, batch_764).
//
// Control block layout (relative to DAT_80612a38, an int* base from mem_alloc):
//   +0x1cfc..+0x1d1c   head / target world position pair (sphere-sweep source)
//   +0x1d54 / +0x1d60  two projectile launch points (+ dir at +0x1d6c/+0x1d78)
//   +0x1d84/+0x1d88    projectile "fuse" thresholds; +0x1d8c/+0x1d90 timers
//   +0x1dac..+0x1dc0   two anim-event spit spawn points (bone-derived)
//   +0x2c00.. (0x5c)   segment/rope records: 3 primary sub-blocks + a 5-slot
//                      collision-sphere table (+0x2c1c count, +0x2c20 array ptr)
//   +0x2cec / +0x2d3c  per-segment scalar table (5 * 0x10) and emitter records
//   +0x2c0c/+0x2c68/+0x2cc4  spine-section rotation angles
//   +0x2de4/+0x2df4    spine deform amount / section-enable flag bits (1/2/4/8/0x20)
//   +0xaff / +0xb08..  GX rope render: point buffers + 3 material-id slots
//   +0xb7d / +0xb80    render-enable flag mask / active mask
//
// Functions:
//   snake3_seg_record_init       (0x8029608c): init one 8-float segment record.
//   snake3_boss_reset            (0x802960b8): full boss-block reset/init.
//   snake3_boss_alloc            (0x80296374): coop-scale, allocate, reset.
//   snake3_boss_render           (0x802963d8): GX rope/segment sub-material draw.
//   snake3_pose_spine_deform     (0x80296620): per-frame spine matrix deform.
//   snake3_dual_projectile_launch(0x802968e0): fire the two body projectiles.
//   snake3_anim_spit_spawn       (0x80296b64): anim-event driven spit spawns.
//   snake3_segment_sphere_sweep  (0x802970d0): segment collision-sphere sweep.
//   snake3_attractor_nudge       (0x802971cc): nudge one attractor scalar.
//   snake3_attractor_oscillate   (0x802971e8): sine-LUT attractor oscillation.
//
// Address range: 0x8029608c - 0x80297298
// Subsystem: game (AI actors / boss)

#include "common.h"

extern "C" {

// --- data globals ----------------------------------------------------------
extern int*  DAT_80612a38;               // serpent-boss control block base (mem_alloc'd)
extern char  DAT_805015bd;               // co-op mode flag ('\n' == active)
extern int   DAT_80612610;               // player count
extern int   DAT_80611d54;               // view/camera record base (+0x208 = active mtx)
extern int   DAT_80611d78;               // player record table base
extern int   DAT_80612020;               // rng / timer source (xor 0x80000000 bias)
extern int   DAT_806109c0;               // frame-delta ticks (xor 0x80000000 bias)
extern const float FLOAT_80583228[];     // 0x800-entry sine LUT (rodata)

// --- float / double constants (rodata, boss block) -------------------------
extern const float  FLOAT_80619568, FLOAT_8061956c, FLOAT_80619570, FLOAT_80619574;
extern const float  FLOAT_80619578, FLOAT_80619580, FLOAT_80619588, FLOAT_80619590;
extern const float  FLOAT_806195a0, FLOAT_806195a4, FLOAT_806195a8, FLOAT_806195e4;
extern const float  FLOAT_806195e8, FLOAT_80619624, FLOAT_80619678, FLOAT_80619700;
extern const float  FLOAT_80619754, FLOAT_80619758, FLOAT_80619790, FLOAT_806197a0;
extern const float  FLOAT_80619794, FLOAT_80619798;
extern const double DOUBLE_80619610;     // rng bias
extern const double DOUBLE_80619630;     // frame-delta bias

// --- raw actor / render-rig helpers (still FUN_ in the IDB) ----------------
extern int  FUN_803826f0(void);          // render rig begin -> actor context (hi=count, lo=ptr)
extern void FUN_8038273c(void);          // render rig end
// GX rope-render pipeline (boss trail segments):
extern void FUN_802b3370(void);          // rope render prologue
extern void FUN_802b3b9c(int a1, int a2, float* pts, float* mat);
extern void FUN_802b3fc8(double t, double bias, int a3, int a4, float* pts, float* mat);
extern void FUN_802b43e4(int a1, int a2, float** viewMtx);
extern void FUN_802b01e4(void);          // rope render flush
extern void FUN_802aeee0(void);          // rope render epilogue
// boss-block builders (reset):
extern void FUN_802b05e8(double a, double b, int* p3, int* p4, int p5, int p6, int* p7, int* p8);
extern void FUN_802b12cc(int a1, unsigned long long* a2);
extern void FUN_802b33e0(void);
extern void FUN_802af38c(void);
// coop-scale predicate helper (returns a value forwarded into mem_alloc):
extern unsigned long long FUN_802af304(unsigned long long a1, unsigned long long a2,
    unsigned long long a3, unsigned long long a4, unsigned long long a5, unsigned long long a6,
    unsigned long long a7, unsigned long long a8, int coop, int a10, int a11, int a12,
    int a13, int a14, int a15, int a16);

// --- named cross-module helpers (call-site typed) --------------------------
extern int  mem_alloc(unsigned long long a1, unsigned long long a2, unsigned long long a3,
    unsigned long long a4, unsigned long long a5, unsigned long long a6, unsigned long long a7,
    unsigned long long a8, int size, int a10, int a11, int a12, int a13, int a14, int a15, int a16);
extern void leaf_31e8(unsigned int dst, int src, int len);   // small block copy
extern double math_sqrt(double x);
extern unsigned int rng_counter_tick(void);
extern void model_pose_eval(void);
extern void mtx4_set_rotation_y(double angle, float* outMtx);
extern void mtx4_multiply(float* a, float* b, float* out);
extern void mtx4_transform_point(float* mtx, float* pt);
extern void snake_mtx3_multiply_upper(int dstMtx, int srcMtx, int rotMtx);
extern void snake_mtx3_scale_init(void* mtx3);
extern void entity_build_local_matrix(float* outMtx, int entity, int includePos, int mirror);
extern int  anim_event_scan_active(void);
extern void gx_set_sub_material_id(int id);
extern unsigned int sprite_get_texture_id(int id);
extern void sound_init_play_params(int paramsPtr);
extern unsigned int sound_play_request(int* params);
extern int  player_ai_projectile_spawn_simple(double a, double b, int type, int* posBlk,
    int* dirBlk, int owner, int* p7, int p8, int p9, int p10, int classId, int p12);
extern void player_ai_spawn_trail_particle(int a1, int a2, int a3, float* pos, float* dir, int a6);
extern void player_ai_explosion_spawn(int a1, int a2, float* pos, float* p4, int p5, int p6, int p7);

} // extern "C"

// forward decl: snake3_boss_reset is called by snake3_boss_alloc
static void snake3_boss_reset(void);

// ===========================================================================
// 0x8029608c  snake3_seg_record_init
// Initialise one 8-float segment/attractor record: fields [1][2] and [4..6] get
// the "one" constant, [0] a base scalar, [3] = param_2 and [7] = param_1.
// ===========================================================================
static void snake3_seg_record_init(double param_1, double param_2, float* rec) {
    float one  = FLOAT_80619580;
    float base = FLOAT_80619568;
    rec[1] = one;
    rec[2] = one;
    rec[3] = (float)param_2;
    rec[0] = base;
    rec[4] = base;
    rec[5] = base;
    rec[6] = base;
    rec[7] = (float)param_1;
}

// ===========================================================================
// 0x802960b8  snake3_boss_reset
// Full reset of the boss control block: clear the 5 segment scalar records
// (+0x2cec), initialise the 3 primary spine sub-blocks (+0x2c08), seed 5 rope
// emitter records (+0x2d3c) via snake3_seg_record_init, wire the render slot
// pointers (+0xb08/+0xb1f/+0xb36), then hand off to the boss-block builders and
// bind the boss sprite (texture 0x300).
// ===========================================================================
static void snake3_boss_reset(void) {
    char* blk = (char*)DAT_80612a38;
    float one   = FLOAT_80619580;
    float half  = FLOAT_80619570;
    float base  = FLOAT_80619568;
    float ninety= FLOAT_80619790;

    *DAT_80612a38 = 0;

    // 5 segment scalar records at +0x2cec, stride 0x10.
    int off = 0;
    for (int i = 0; i < 5; i++) {
        *(float*)(blk + off + 0x2cec) = base;
        *(float*)(blk + off + 0x2cf0) = base;
        *(float*)(blk + off + 0x2cf4) = base;
        *(float*)(blk + off + 0x2cf8) = base;
        off += 0x10;
    }

    DAT_80612a38[0x748] = 0;
    DAT_80612a38[0x749] = 0;
    *(float*)&DAT_80612a38[0x74b] = base;
    *(float*)&DAT_80612a38[0x74c] = base;
    *(float*)&DAT_80612a38[0x74d] = base;
    *(float*)&DAT_80612a38[0x74e] = base;
    DAT_80612a38[0x763] = 0;
    DAT_80612a38[0x764] = 0;
    *(float*)&DAT_80612a38[0xb03] = base;
    *(float*)&DAT_80612a38[0xb1a] = one;
    *(float*)&DAT_80612a38[0xb31] = half;
    *(float*)&DAT_80612a38[0xb79] = base;
    DAT_80612a38[0xb7e] = 0;
    *(float*)&DAT_80612a38[0xb7f] = base;
    DAT_80612a38[0xb80] = 0;

    // 3 primary spine sub-blocks at +0x2c08, stride 0x5c.
    off = 0;
    for (int i = 0; i < 3; i++) {
        *(float*)(blk + off + 0x2c08) = base;
        *(float*)(blk + off + 0x2c28) = *(float*)(blk + off + 0x2c0c) - one;
        *(float*)(blk + off + 0x2c24) = one + *(float*)(blk + off + 0x2c0c);
        *(int*)  (blk + off + 0x2c18) = 1;
        *(float*)(blk + off + 0x2c10) = base;
        *(float*)(blk + off + 0x2c14) = ninety;
        *(int*)  (blk + off + 0x2c30) = 0;
        off += 0x5c;
    }

    *(float*)&DAT_80612a38[0xb0b] = base;
    *(float*)&DAT_80612a38[0xb22] = base;
    *(float*)&DAT_80612a38[0xb39] = base;

    // 5 rope emitter records at +0x2d3c, stride 0x20.
    off = 0;
    for (int i = 0; i < 5; i++) {
        snake3_seg_record_init((double)FLOAT_806195a8, (double)FLOAT_806195e8,
                               (float*)(blk + off + 0x2d3c));
        off += 0x20;
    }

    *(float*)&DAT_80612a38[0x761] = base;
    *(float*)&DAT_80612a38[0x762] = base;
    *(float*)&DAT_80612a38[0x771] = base;
    DAT_80612a38[0xb77] = 0;
    DAT_80612a38[0xb78] = 0;
    DAT_80612a38[0xb07] = 3;
    DAT_80612a38[0xb1e] = 1;
    DAT_80612a38[0xb35] = 1;
    DAT_80612a38[0xb08] = (int)(DAT_80612a38 + 0xb3b);
    DAT_80612a38[0xb1f] = (int)(DAT_80612a38 + 0xb47);
    DAT_80612a38[0xb36] = (int)(DAT_80612a38 + 0xb4b);
    DAT_80612a38[0xb7d] = 0x2f;
    DAT_80612a38[0xb7b] = 0;
    *(float*)&DAT_80612a38[0xb7c] = base;

    FUN_802b05e8((double)FLOAT_80619794, (double)FLOAT_80619798,
                 DAT_80612a38 + 0x25c, DAT_80612a38 + 0x3ba,
                 (int)(DAT_80612a38 + 0x3c4), (int)(DAT_80612a38 + 0x3d8),
                 DAT_80612a38 + 0x464, DAT_80612a38 + 0x466);
    FUN_802b12cc((int)(DAT_80612a38 + 0x468), (unsigned long long*)(DAT_80612a38 + 1));
    DAT_80612a38[0x738] = 0;
    FUN_802b33e0();
    sprite_get_texture_id(0x300);
    FUN_802af38c();
}

// ===========================================================================
// 0x80296374  snake3_boss_alloc
// Allocate the boss control block. In co-op with >1 player the coop flag is set
// on the FUN_802af304 helper; the result feeds mem_alloc (size 0x2e04) whose
// return becomes the DAT_80612a38 base, then the block is fully reset.
// The 16-slot argument list mirrors the register/stack image the game passes on;
// only the coop selector and the size matter to this routine.
// ===========================================================================
void snake3_boss_alloc(unsigned long long param_1, unsigned long long param_2,
    unsigned long long param_3, unsigned long long param_4, unsigned long long param_5,
    unsigned long long param_6, unsigned long long param_7, unsigned long long param_8,
    int param_9, int param_10, int param_11, int param_12, int param_13, int param_14,
    int param_15, int param_16) {
    unsigned long long scaled;
    if ((DAT_805015bd == '\n') && (1 < DAT_80612610)) {
        scaled = FUN_802af304(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                              param_8, 1, param_10, param_11, param_12, param_13, param_14,
                              param_15, param_16);
    } else {
        scaled = FUN_802af304(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                              param_8, 0, param_10, param_11, param_12, param_13, param_14,
                              param_15, param_16);
    }
    DAT_80612a38 = (int*)mem_alloc(scaled, param_2, param_3, param_4, param_5, param_6, param_7,
                                   param_8, 0x2e04, 1, param_11, param_12, param_13, param_14,
                                   param_15, param_16);
    snake3_boss_reset();
}

// ===========================================================================
// 0x802963d8  snake3_boss_render
// Render the boss rope/segment trails. Each of the 5 render segments is gated by
// a bit in the enable mask (+0xb7d) AND the active mask (+0xb80). Segments 0-2
// build a 3-point tapered strip (FUN_802b3b9c + two FUN_802b3fc8 taper calls),
// segments 3-4 are single strips; then the whole batch is drawn (FUN_802b01e4)
// and, when not in co-op, the sub-material is bound and flushed.
// ===========================================================================
void snake3_boss_render(void) {
    if ((DAT_80612a38 == (int*)0) || (*DAT_80612a38 == 0)) {
        return;
    }
    int viewMtx = *(int*)(DAT_80611d54 + 0x208);
    FUN_802b3370();

    if (((DAT_80612a38[0xb7d] & 1U) != 0) && ((DAT_80612a38[0xb80] & 1U) != 0)) {
        int seg = DAT_80612a38[0xb08];
        FUN_802b3b9c((int)(DAT_80612a38 + 0x772), viewMtx,
                     (float*)(DAT_80612a38 + 0xaff), (float*)seg);
        FUN_802b3fc8((double)*(float*)(seg + 0x1c), (double)FLOAT_80619678,
                     (int)(DAT_80612a38 + 0x772), viewMtx,
                     (float*)(DAT_80612a38 + 0xaff), (float*)(seg + 0x10));
        FUN_802b3fc8((double)*(float*)(seg + 0x2c), (double)FLOAT_8061956c,
                     (int)(DAT_80612a38 + 0x772), viewMtx,
                     (float*)(DAT_80612a38 + 0xaff), (float*)(seg + 0x20));
        FUN_802b43e4((int)(DAT_80612a38 + 0xb4f), (int)(DAT_80612a38 + 0xaff),
                     (float**)(DAT_80611d54 + 0x208));
    }
    if (((DAT_80612a38[0xb7d] & 2U) != 0) && ((DAT_80612a38[0xb80] & 2U) != 0)) {
        FUN_802b3b9c((int)(DAT_80612a38 + 0x772), viewMtx,
                     (float*)(DAT_80612a38 + 0xb16), (float*)DAT_80612a38[0xb1f]);
        FUN_802b43e4((int)(DAT_80612a38 + 0xb57), (int)(DAT_80612a38 + 0xb16),
                     (float**)(DAT_80611d54 + 0x208));
    }
    if (((DAT_80612a38[0xb7d] & 4U) != 0) && ((DAT_80612a38[0xb80] & 4U) != 0)) {
        FUN_802b3b9c((int)(DAT_80612a38 + 0x772), viewMtx,
                     (float*)(DAT_80612a38 + 0xb2d), (float*)DAT_80612a38[0xb36]);
        FUN_802b43e4((int)(DAT_80612a38 + 0xb5f), (int)(DAT_80612a38 + 0xb2d),
                     (float**)(DAT_80611d54 + 0x208));
    }
    if ((DAT_80612a38[0xb7d] & 8U) != 0) {
        FUN_802b43e4((int)(DAT_80612a38 + 0xb67), (int)(DAT_80612a38 + 0x74f),
                     (float**)(DAT_80611d54 + 0x208));
    }
    if ((DAT_80612a38[0xb7d] & 0x20U) != 0) {
        FUN_802b43e4((int)(DAT_80612a38 + 0xb6f), (int)(DAT_80612a38 + 0x752),
                     (float**)(DAT_80611d54 + 0x208));
    }
    FUN_802b01e4();
    if ((DAT_805015bd != '\n') || (DAT_80612610 < 2)) {
        gx_set_sub_material_id(1);
        FUN_802aeee0();
    }
}

// ===========================================================================
// 0x80296620  snake3_pose_spine_deform
// Per-frame spine deform. Evaluates the pose, builds a Y-rotation matrix for the
// head section, and for each of the 3 spine sections either resets the section's
// 3x3 to a scale-identity (section disabled) or applies the rotation about the
// pivot bone via snake_mtx3_multiply_upper. Section 0 additionally shears its 6
// child bones by the +0x2de4 deform amount around the pivot.
// The incoming f29/f30/f31 register pair values are the head pivot vector; they
// are read through the actor's bone table below and are not otherwise consumed,
// so they are represented as locals seeded from the bone data.
// ===========================================================================
void snake3_pose_spine_deform(void) {
    char* blk = (char*)DAT_80612a38;
    unsigned char scratch[64];
    float rotMtx[24];

    int actorCtx = FUN_803826f0();
    model_pose_eval();
    mtx4_set_rotation_y((double)*(float*)(blk + 0x2c0c), rotMtx);

    int bone = *(int*)(*(int*)(actorCtx + 4) + 0x10);
    double pivotX = (double)*(float*)(bone + 0xf0);
    double pivotY = (double)*(float*)(bone + 0xf4);
    double pivotZ = (double)*(float*)(bone + 0xf8);
    leaf_31e8((unsigned int)(uintptr_t)scratch, bone + 0xc0, 0x40);
    snake_mtx3_multiply_upper(*(int*)(*(int*)(actorCtx + 4) + 0x10) + 0xc0,
                              (int)(uintptr_t)scratch, (int)(uintptr_t)rotMtx);

    if ((*(unsigned int*)(blk + 0x2df4) & 1) == 0) {
        snake_mtx3_scale_init((void*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + 0x100));
        snake_mtx3_scale_init((void*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + 0x140));
    } else {
        int base = *(int*)(*(int*)(actorCtx + 4) + 0x10);
        *(float*)(base + 0x170) = *(float*)(blk + 0x2de4) * *(float*)(base + 0x140) + *(float*)(base + 0x170);
        base = *(int*)(*(int*)(actorCtx + 4) + 0x10);
        *(float*)(base + 0x174) = *(float*)(blk + 0x2de4) * *(float*)(base + 0x144) + *(float*)(base + 0x174);
        base = *(int*)(*(int*)(actorCtx + 4) + 0x10);
        *(float*)(base + 0x178) = *(float*)(blk + 0x2de4) * *(float*)(base + 0x148) + *(float*)(base + 0x178);
        int child = 0x100;
        for (int i = 4; i < 6; i++) {
            base = *(int*)(*(int*)(actorCtx + 4) + 0x10);
            *(float*)(base + child + 0x30) = (float)((double)*(float*)(base + child + 0x30) - pivotX);
            base = *(int*)(*(int*)(actorCtx + 4) + 0x10);
            *(float*)(base + child + 0x34) = (float)((double)*(float*)(base + child + 0x34) - pivotY);
            base = *(int*)(*(int*)(actorCtx + 4) + 0x10);
            *(float*)(base + child + 0x38) = (float)((double)*(float*)(base + child + 0x38) - pivotZ);
            float* childMtx = (float*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + child);
            mtx4_multiply(childMtx, rotMtx, childMtx);
            base = *(int*)(*(int*)(actorCtx + 4) + 0x10);
            *(float*)(base + child + 0x30) = (float)((double)*(float*)(base + child + 0x30) + pivotX);
            base = *(int*)(*(int*)(actorCtx + 4) + 0x10);
            *(float*)(base + child + 0x34) = (float)((double)*(float*)(base + child + 0x34) + pivotY);
            base = *(int*)(*(int*)(actorCtx + 4) + 0x10);
            *(float*)(base + child + 0x38) = (float)((double)*(float*)(base + child + 0x38) + pivotZ);
            child += 0x40;
        }
    }

    if ((*(unsigned int*)(blk + 0x2df4) & 2) == 0) {
        snake_mtx3_scale_init((void*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + 0x180));
    } else {
        mtx4_set_rotation_y((double)*(float*)(blk + 0x2c68), rotMtx);
        leaf_31e8((unsigned int)(uintptr_t)scratch, *(int*)(*(int*)(actorCtx + 4) + 0x10) + 0x180, 0x40);
        snake_mtx3_multiply_upper(*(int*)(*(int*)(actorCtx + 4) + 0x10) + 0x180,
                                  (int)(uintptr_t)scratch, (int)(uintptr_t)rotMtx);
    }

    if ((*(unsigned int*)(blk + 0x2df4) & 4) == 0) {
        snake_mtx3_scale_init((void*)(*(int*)(*(int*)(actorCtx + 4) + 0x10) + 0x200));
    } else {
        mtx4_set_rotation_y((double)*(float*)(blk + 0x2cc4), rotMtx);
        leaf_31e8((unsigned int)(uintptr_t)scratch, *(int*)(*(int*)(actorCtx + 4) + 0x10) + 0x200, 0x40);
        snake_mtx3_multiply_upper(*(int*)(*(int*)(actorCtx + 4) + 0x10) + 0x200,
                                  (int)(uintptr_t)scratch, (int)(uintptr_t)rotMtx);
    }
    FUN_8038273c();
}

// ===========================================================================
// 0x802968e0  snake3_dual_projectile_launch
// The boss's two body projectile slots (bits 8 and 0x20 of +0x2df4). When the
// head is close enough to the target and a slot's cooldown (+0x1d8c / +0x1d90)
// has elapsed, play the launch sound and spawn the projectile. Each slot then
// advances its timer and, once its fuse (+0x1d84 / +0x1d88) drops below zero,
// spawns a detonation explosion and clears the slot's active bit.
// ===========================================================================
void snake3_dual_projectile_launch(void) {
    int actorCtx = FUN_803826f0();
    int blk = (int)DAT_80612a38;
    float dx = *(float*)(blk + 0x1d04) - *(float*)(blk + 0x1d1c);
    float dy = *(float*)(blk + 0x1cfc) - *(float*)(blk + 0x1d14);

    int params[16];
    unsigned char* pFlag;

    if (dy * dy + dx * dx < FLOAT_806197a0) {
        if ((*(unsigned int*)(blk + 0x2df4) & 8) != 0) {
            if (0x32 < *(int*)(blk + 0x1d8c)) {
                int* owner = *(int**)(actorCtx + 0x7c);
                int material = *(int*)(DAT_80611d78 + 0x14);
                sound_init_play_params((int)params);
                pFlag = (unsigned char*)&params[10]; *pFlag = 1;   // local_18
                params[0] = 0xc67;
                params[3] = *(int*)(blk + 0x1d54);
                params[4] = *(int*)(blk + 0x1d58);
                params[5] = *(int*)(blk + 0x1d5c);
                sound_play_request(params);
                int proj = player_ai_projectile_spawn_simple(
                    (double)FLOAT_806195a0, (double)FLOAT_806195a8, 0xe,
                    (int*)((int)DAT_80612a38 + 0x1d54), (int*)((int)DAT_80612a38 + 0x1d6c),
                    actorCtx, owner, 2, 0, *(int*)(actorCtx + 0x34), 0x2000000e, 0xc);
                *(int*)(proj + 0x19c) = material;
                *(int*)((int)DAT_80612a38 + 0x1d8c) = 0;
            }
        }
        blk = (int)DAT_80612a38;
        if ((*(unsigned int*)(blk + 0x2df4) & 0x20) != 0) {
            if (0x32 < *(int*)(blk + 0x1d90)) {
                int* owner = *(int**)(actorCtx + 0x7c);
                int material = *(int*)(DAT_80611d78 + 0x14);
                sound_init_play_params((int)params);
                pFlag = (unsigned char*)&params[10]; *pFlag = 1;
                params[0] = 0xc67;
                params[3] = *(int*)(blk + 0x1d60);
                params[4] = *(int*)(blk + 0x1d64);
                params[5] = *(int*)(blk + 0x1d68);
                sound_play_request(params);
                int proj = player_ai_projectile_spawn_simple(
                    (double)FLOAT_806195a0, (double)FLOAT_806195a8, 0xe,
                    (int*)((int)DAT_80612a38 + 0x1d60), (int*)((int)DAT_80612a38 + 0x1d78),
                    actorCtx, owner, 2, 0, *(int*)(actorCtx + 0x34), 0x2000000e, 0xc);
                *(int*)(proj + 0x19c) = material;
                *(int*)((int)DAT_80612a38 + 0x1d90) = 0;
            }
        }
    }

    float zero = FLOAT_80619568;
    if ((*(unsigned int*)((int)DAT_80612a38 + 0x2df4) & 8) != 0) {
        *(int*)((int)DAT_80612a38 + 0x1d8c) = *(int*)((int)DAT_80612a38 + 0x1d8c) + 1;
        if (*(float*)((int)DAT_80612a38 + 0x1d84) < zero) {
            player_ai_explosion_spawn(0x20, 0x17, (float*)((int)DAT_80612a38 + 0x1d54), (float*)0, 0, 0, 1);
            *(unsigned int*)((int)DAT_80612a38 + 0x2df4) &= 0xfffffff7;
        }
    }
    zero = FLOAT_80619568;
    if ((*(unsigned int*)((int)DAT_80612a38 + 0x2df4) & 0x20) != 0) {
        *(int*)((int)DAT_80612a38 + 0x1d90) = *(int*)((int)DAT_80612a38 + 0x1d90) + 1;
        if (*(float*)((int)DAT_80612a38 + 0x1d88) < zero) {
            player_ai_explosion_spawn(0x20, 0x17, (float*)((int)DAT_80612a38 + 0x1d60), (float*)0, 0, 0, 1);
            *(unsigned int*)((int)DAT_80612a38 + 0x2df4) &= 0xffffffdf;
        }
    }
}

// ===========================================================================
// 0x80296b64  snake3_anim_spit_spawn
// Fired from two animation events. For each of two body bones (indices resolved
// through the skeleton table) it lerps the bone segment endpoint, transforms it
// into world space, adds a small RNG-jittered spread, plays a launch sound, and
// spawns a trailing projectile (class 0x2010000e / 0x2010000a).
// ===========================================================================
void snake3_anim_spit_spawn(int actor) {
    int blk = (int)DAT_80612a38;
    float spread0[3];       // local_194/190/18c
    float spread1[3];       // local_1a0/19c/198
    float localMtxA[16];    // afStack_68 + tail
    float localMtxB[16];    // afStack_e8 + tail
    float outA[3];          // local_a8/a4/a0
    float outB[3];          // local_128/124/120
    int   params[16];

    if (anim_event_scan_active() != 0) {
        entity_build_local_matrix(localMtxA, actor, 1, 0);
        float k = FLOAT_80619574;
        int* skel = *(int**)(*(int*)(actor + 4) + 4);
        int n = *skel;
        float* bp = (float*)skel[n * -0x30 + 0x658];
        *(float*)(blk + 0x1dac) = bp[3] - bp[0];
        *(float*)(blk + 0x1db0) = bp[4] - bp[1];
        *(float*)(blk + 0x1db4) = bp[5] - bp[2];
        *(float*)(blk + 0x1dac) = k * *(float*)(blk + 0x1dac) + bp[0];
        *(float*)(blk + 0x1db0) = k * *(float*)(blk + 0x1db0) + bp[1];
        *(float*)(blk + 0x1db4) = k * *(float*)(blk + 0x1db4) + bp[2];
        int boneMtx = *(int*)(*(int*)(actor + 4) + 0x10) + *(char*)((int)skel + n * -0xc0 + 0x18fd) * 0x40;
        mtx4_transform_point((float*)boneMtx, (float*)(blk + 0x1dac));
        mtx4_transform_point(localMtxA, (float*)(blk + 0x1dac));
        mtx4_multiply((float*)boneMtx, localMtxA, outA);
        *(float*)(blk + 0x1dac) = *(float*)(blk + 0x1dac) + outA[0];
        *(float*)(blk + 0x1db0) = *(float*)(blk + 0x1db0) + outA[1];
        *(float*)(blk + 0x1db4) = *(float*)(blk + 0x1db4) + outA[2];

        spread0[0] = (FLOAT_806195a8 * ((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_80619610) / FLOAT_806195e4) + outA[0]) - FLOAT_806195a4;
        spread0[1] = FLOAT_806195a8 * ((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_80619610) / FLOAT_806195e4) - FLOAT_806195a4;
        spread0[2] = (FLOAT_806195a8 * ((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_80619610) / FLOAT_806195e4) + outA[2]) - FLOAT_806195a4;

        blk = (int)DAT_80612a38;
        sound_init_play_params((int)params);
        *(unsigned char*)&params[10] = 1;
        params[0] = 0xc67;
        params[3] = *(int*)(blk + 0x1dac);
        params[4] = *(int*)(blk + 0x1db0);
        params[5] = *(int*)(blk + 0x1db4);
        sound_play_request(params);
        player_ai_spawn_trail_particle(0, 0, 0xffffffff, (float*)((int)DAT_80612a38 + 0x1dac), spread0, -1);
        player_ai_projectile_spawn_simple((double)FLOAT_80619580, (double)FLOAT_806195a8, 9,
            (int*)((int)DAT_80612a38 + 0x1dac), (int*)spread0, actor, (int*)0, 6, 0,
            *(int*)(actor + 0x34), 0x2010000e, 0x16);
    }

    if (anim_event_scan_active() != 0) {
        entity_build_local_matrix(localMtxB, actor, 1, 0);
        float k = FLOAT_80619574;
        int* skel = *(int**)(*(int*)(actor + 4) + 4);
        int n = *skel;
        float* bp = (float*)skel[n * -0x30 + 0x598];
        *(float*)(blk + 0x1db8) = bp[3] - bp[0];
        *(float*)(blk + 0x1dbc) = bp[4] - bp[1];
        *(float*)(blk + 0x1dc0) = bp[5] - bp[2];
        *(float*)(blk + 0x1db8) = k * *(float*)(blk + 0x1db8) + bp[0];
        *(float*)(blk + 0x1dbc) = k * *(float*)(blk + 0x1dbc) + bp[1];
        *(float*)(blk + 0x1dc0) = k * *(float*)(blk + 0x1dc0) + bp[2];
        int boneMtx = *(int*)(*(int*)(actor + 4) + 0x10) + *(char*)((int)skel + n * -0xc0 + 0x15fd) * 0x40;
        mtx4_transform_point((float*)boneMtx, (float*)(blk + 0x1db8));
        mtx4_transform_point(localMtxB, (float*)(blk + 0x1db8));
        mtx4_multiply((float*)boneMtx, localMtxB, outB);
        *(float*)(blk + 0x1db8) = *(float*)(blk + 0x1db8) + outB[0];
        *(float*)(blk + 0x1dbc) = *(float*)(blk + 0x1dbc) + outB[1];
        *(float*)(blk + 0x1dc0) = *(float*)(blk + 0x1dc0) + outB[2];

        spread1[0] = (FLOAT_806195a8 * ((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_80619610) / FLOAT_806195e4) + outB[0]) - FLOAT_806195a4;
        spread1[1] = FLOAT_806195a8 * ((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_80619610) / FLOAT_806195e4) - FLOAT_806195a4;
        spread1[2] = (FLOAT_806195a8 * ((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_80619610) / FLOAT_806195e4) + outB[2]) - FLOAT_806195a4;

        blk = (int)DAT_80612a38;
        sound_init_play_params((int)params);
        *(unsigned char*)&params[10] = 1;
        params[0] = 0xc67;
        params[3] = *(int*)(blk + 0x1db8);
        params[4] = *(int*)(blk + 0x1dbc);
        params[5] = *(int*)(blk + 0x1dc0);
        sound_play_request(params);
        player_ai_spawn_trail_particle(0, 0, 0xffffffff, (float*)((int)DAT_80612a38 + 0x1db8), spread1, -1);
        player_ai_projectile_spawn_simple((double)FLOAT_80619580, (double)FLOAT_806195a8, 9,
            (int*)((int)DAT_80612a38 + 0x1db8), (int*)spread1, actor, (int*)0, 6, 0,
            *(int*)(actor + 0x34), 0x2010000a, 0x16);
    }
}

// ===========================================================================
// 0x802970d0  snake3_segment_sphere_sweep
// Sweep the given segment's collision-sphere list (record idx * 0x5c: +0x2c1c
// count, +0x2c20 array of 0x10-byte spheres) against the head point at +0x1d14.
// Returns 1 and writes the penetration depth into the sphere's +0xc field on the
// first hit (inside radius, in front, within depth), else 0.
// ===========================================================================
int snake3_segment_sphere_sweep(int idx) {
    int blk = (int)DAT_80612a38;
    int rec = blk + idx * 0x5c;
    int count = *(int*)(rec + 0x2c1c);
    int off = 0;
    if (0 < count) {
        do {
            float ddx = *(float*)(blk + 0x1d18) - *(float*)(rec + 0x2c00);
            float* sphere = (float*)(*(int*)(rec + 0x2c20) + off);
            float ddy = *(float*)(blk + 0x1d14) - *(float*)(rec + 0x2bfc);
            float ddz = *(float*)(blk + 0x1d1c) - *(float*)(rec + 0x2c04);
            double dist2 = (double)(ddz * ddz + ddy * ddy + ddx * ddx);
            double proj;
            if ((dist2 < (double)(sphere[3] * sphere[3])) &&
                (proj = (double)(ddz * sphere[2] + ddy * sphere[0] + ddx * sphere[1]),
                 (double)FLOAT_80619568 < proj) &&
                (-(float)(proj * proj - dist2) < FLOAT_80619624)) {
                double d = math_sqrt(dist2);
                *(float*)(*(int*)((int)DAT_80612a38 + idx * 0x5c + 0x2c20) + off + 0xc) =
                    (float)(d - (double)FLOAT_806195a8);
                return 1;
            }
            off += 0x10;
            count += -1;
        } while (count != 0);
    }
    return 0;
}

// ===========================================================================
// 0x802971cc  snake3_attractor_nudge
// Rope-attractor callback: when enabled, advance the attractor's leading scalar
// by the per-frame step (FLOAT_80619700). p2/p3 are unused callback params.
// ===========================================================================
void snake3_attractor_nudge(float* rec, int p2, int p3, int enabled) {
    (void)p2; (void)p3;
    if (enabled == 0) {
        return;
    }
    *rec = *rec + FLOAT_80619700;
}

// ===========================================================================
// 0x802971e8  snake3_attractor_oscillate
// Rope-attractor callback: set the attractor's +0xc (rec[3]) field. When p5 is
// zero it snaps to the rest constant; otherwise it oscillates via a sine-LUT
// index built from the frame-delta and rng sources. Optionally also advances the
// leading scalar when p4 is set.
// ===========================================================================
void snake3_attractor_oscillate(float* rec, int p2, int p3, int p4, int p5) {
    (void)p2; (void)p3;
    if (p5 == 0) {
        rec[3] = FLOAT_80619590;
    } else {
        int lutIdx = (int)(FLOAT_80619754 * FLOAT_80619578 * FLOAT_80619588 *
            (float)((double)CONCAT44(0x43300000, (unsigned)(DAT_80612020 ^ 0x80000000)) - DOUBLE_80619630) *
            (FLOAT_80619580 / (float)((double)CONCAT44(0x43300000, (unsigned)(DAT_806109c0 ^ 0x80000000)) - DOUBLE_80619630)) +
            FLOAT_80619574) & 0x7ff;
        rec[3] = FLOAT_80619758 * (FLOAT_80619580 + FLOAT_80583228[lutIdx]) + FLOAT_80619590;
    }
    if (p4 != 0) {
        *rec = *rec + FLOAT_80619700;
    }
}
