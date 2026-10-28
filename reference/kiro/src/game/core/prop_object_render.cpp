// Module: prop_object_render
// TSFP GameCube main.dol non-matching decompilation.
//
// Per-frame prop / dynamic-object update, skeleton (bone-matrix) construction,
// and the prop "fire" path (projectile / muzzle-flash / trail emission). These
// are the prop-object counterparts to the weapon_* helpers: an object whose
// state flag +8 bit 0x40 marks it as weapon-driven dispatches to the weapon_*
// functions, otherwise the prop path here runs.
//
// Object layout used below (obj = param):
//   obj+0x28  : state flags (bit1 active, bit0x100 skinned-built,
//               bit0x200000 pose-ready)
//   obj+0x7c  : model/instance record pointer (rec)
//   rec+0x08  : model flags (bit2 static, bit0x40 weapon)
//   rec+0x0c  : prop type id (indexes prop_def_by_index)
//   rec+0x10  : model class id
//   rec+0xd8/0xdc : animation-active flag / animation timer
//
// Batch: batch_710  |  Addresses: 0x8023d2ec - 0x8023e88f (subset)
// NON-MATCHING decompilation.

#include "common.h"

// Ghidra float-sqrt intrinsic (common.h owns no math intrinsics). The original
// PowerPC code uses the hardware fsqrt(s) here; a plain sqrt is behaviourally
// equivalent for this non-matching build.
static inline float tsfp_sqrtf(float x) {
    float r = x;
    if (x > 0.0f) {
        // Two Newton-Raphson iterations from a rough seed keep this header-only
        // without pulling in <math.h> conventions across the tree.
        r = x;
        r = 0.5f * (r + x / r);
        r = 0.5f * (r + x / r);
        r = 0.5f * (r + x / r);
        r = 0.5f * (r + x / r);
    }
    return r;
}
#ifndef SQRT
#define SQRT(x) tsfp_sqrtf((float)(x))
#endif

// ---------------------------------------------------------------------------
// Shared timing / math constants (file-local externs to raw symbols).
// ---------------------------------------------------------------------------
extern float  FLOAT_806108ec;   // frame delta scale
extern float  FLOAT_806188f8;   // 1.0f
extern float  FLOAT_80618908;   // deg->rad numerator
extern float  FLOAT_8061890c;   // deg->rad denominator
extern float  FLOAT_80618910;   // 0.0f
extern float  FLOAT_80618914;   // anim divisor A
extern float  FLOAT_80618918;   // anim divisor B
extern float  FLOAT_8061891c;   // anim clamp / 0.5f
extern float  FLOAT_80618920;   // fire-origin blend
extern float  FLOAT_80618924;   // rng range
extern float  FLOAT_80618928;   // spread scale
extern float  FLOAT_8061892c;   // 1.5f (fast-inv-sqrt refine)
extern int    DAT_806108e4;     // global frame-advance enable
extern int    DAT_806109c0;     // rng/frame counter (int->double magic src)
extern double DOUBLE_80618900;  // 0x43300000_00000000 bias
extern double DOUBLE_80618930;  // rng bias

extern int    DAT_80610ba0;     // slot-record table base (0x29c stride)
extern float  DAT_803e01e8;     // muzzle offset template (7 floats)
extern float  DAT_803e01ec;
extern float  DAT_803e01f0;
extern float  DAT_803e01f4;
extern float  DAT_803e01f8;
extern float  DAT_803e01fc;
extern int    DAT_80611e2c;     // default particle-id fallback record

// ---------------------------------------------------------------------------
// Cross-module / register-spill helpers (file-local; common.h owns no raw syms).
// ---------------------------------------------------------------------------
extern int  prop_def_by_index(int idx);          // prop_table_loader.cpp (0x8023d2c0)

extern void weapon_equipped_tick(void);
extern void weapon_bone_matrix_builder(void);
extern int  weapon_get_def_by_index(int idx);
extern int  weapon_fire_tick(int obj, float *pos, int side, int a4, void *outDir);
extern void weapon_compute_fire_origin(int obj, unsigned int boneIdx, int side,
                                       float *pos, float *outDir);
extern void weapon_fire_stub(void);

extern int  match_slot_field_ptr(int base, int side);
extern void entity_build_local_matrix(float *out, int obj, int a3, int a4);
extern unsigned int rng_counter_tick(void);
extern int  object_projectile_spawn_dispatch(double d1, double d2, int obj, float *pos,
                                             int a5, int side, unsigned int *a7,
                                             unsigned int *a8, float *a9, unsigned int *a10,
                                             unsigned int *a11, unsigned int *a12,
                                             int a13, int *a14, int one);
extern void particle_cached_restart(int *idHolder);
extern void player_ai_spawn_trail_particle(int a1, unsigned int a2, unsigned int a3,
                                           float *pos, float *dir, int a6);
extern void worm_stat_add(double value, int statIdx, int player);
extern void object_play_fire_sound(float *pos, int side, unsigned int *emitter, int a4);
extern void sound_get_emitter_position(int *emitter, unsigned int *outPos);

// Matrix helpers (matrix4.cpp / math_trig.cpp family; declared file-local).
extern void mtx4_set_translate_rotz(double x, double y, double z, double angle, float *out);
extern void mtx4_set_translation(double x, double y, double z);
extern void mtx4_multiply(float *a, float *b, float *out);
extern void mtx4_copy(float *dst, float *src);
extern void mtx4_set_identity(int out);
extern void mtx4_table_reset_identity(void);
extern void mtx4_set_transform_euler(double x, double y, double z,
                                     double rx, double ry, double rz, float *out);
extern void mtx4_set_rotation_y(double angle, float *out);
extern void mtx4_set_rotation_z(double angle, float *out);
extern void mtx4_transform_vector(float *mtx, float *v);
extern void mtx4_transform_vector_alias(float *mtx, float *v);

extern void prop_read_root_anchor(int obj, float *out);    // prop_object_util.cpp (0x80240324)
extern void FUN_8023fac0(int obj, int boneIdx, float *out); // read bone local pose
extern void FUN_8023fec0(int obj, float *pos);              // seed muzzle pos
extern void FUN_8023fd34(int obj, float *out);              // read aim frame
extern void FUN_8023fe0c(void);                             // aim-frame finalize
extern int  FUN_8023e890(void);                             // prop non-weapon anim update

extern unsigned long long FUN_803826c0(void); // fire-path work context
extern unsigned long long FUN_803826d8(void); // skeleton-build work context
extern void FUN_8038270c(void);               // release fire-path context
extern void FUN_80382724(void);               // release skeleton-build context

// Forward declarations (mutual calls between tick / matrix / skeleton paths).
static void prop_bone_matrix_apply(int obj, int a2, int slot, float *m);
static void prop_skeleton_build(void);

// 0x8023d2ec | prop_object_tick
// Per-frame prop update: advance the object animation timer when its model has
// an anim (rec+0xd8), and when the object is active (obj+0x28 bit1) either run
// the weapon tick (rec+8 bit0x40) or the prop non-weapon anim update, applying
// the shared frame-advance delta to the model's two counters at +0xf0/+0xf4.
void prop_object_tick(int obj)
{
    int rec = *(int *)(obj + 0x7c);

    if (*(int *)(rec + 0xd8) != 0) {
        *(float *)(rec + 0xdc) =
            FLOAT_806108ec *
                (FLOAT_806188f8 /
                 (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618900)) +
            *(float *)(rec + 0xdc);
    }

    if ((*(unsigned int *)(obj + 0x28) & 1) != 0) {
        if ((*(unsigned int *)(rec + 8) & 2) == 0) {
            *(int *)(*(int *)(rec + 0x6c) + 0xf0) =
                *(int *)(*(int *)(rec + 0x6c) + 0xf0) + DAT_806108e4;
            *(int *)(*(int *)(rec + 0x6c) + 0xf4) =
                *(int *)(*(int *)(rec + 0x6c) + 0xf4) + DAT_806108e4;
        }
        if ((*(unsigned int *)(rec + 8) & 0x40) == 0) {
            FUN_8023e890();
        } else {
            weapon_equipped_tick();
        }
    }
}

// 0x8023d3a8 | prop_build_pose
// Build the object pose/skeleton once per frame (guarded by obj+0x28 bit0x100).
// Dispatches to the weapon or prop skeleton builder, then marks the pose ready
// (obj+0x18 bit8, obj+0x28 bit0x200000).
void prop_build_pose(int obj)
{
    if ((*(unsigned int *)(obj + 0x28) & 0x100) == 0) {
        if ((*(unsigned int *)(*(int *)(obj + 0x7c) + 8) & 0x40) == 0) {
            prop_skeleton_build();
        } else {
            weapon_bone_matrix_builder();
        }
        *(unsigned int *)(obj + 0x18) = *(unsigned int *)(obj + 0x18) | 8;
        *(unsigned int *)(obj + 0x28) = *(unsigned int *)(obj + 0x28) | 0x200000;
    }
}

// 0x8023ddac | prop_select_model_field
// Select a per-object model field: normally obj+8, but for a weapon-driven
// object (other+8 bit0x40) whose weapon-def flag (+0xac bit1) is set, use the
// weapon def's field +0x4c instead.
int prop_select_model_field(int obj, int other)
{
    int v = *(int *)(obj + 8);
    if ((*(unsigned int *)(other + 8) & 0x40) != 0) {
        int def = weapon_get_def_by_index(*(int *)(other + 0xc));
        if ((*(unsigned int *)(def + 0xac) & 1) != 0) {
            v = *(int *)(def + 0x4c);
        }
    }
    return v;
}

// 0x8023d410 | prop_bone_matrix_apply
// Build a single bone matrix for a prop bone attachment: rotate/translate about
// the bone's local anchor (from the prop instance array) by the object's Y
// rotation, then pre-multiply the caller matrix 'm' into the bone slot.
static void prop_bone_matrix_apply(int obj, int a2, int slot, float *m)
{
    (void)obj;
    (void)a2;
    unsigned long long ctx = FUN_803826d8();
    int self = (int)(ctx >> 0x20);
    unsigned int lo = (unsigned int)ctx;

    int *node = *(int **)(*(int *)(self + 4) + 4);
    float *anchor = (float *)(node[*node * -0x30 + ((int)lo >> 0x10) * 0x30 + 0x1b] +
                              (lo & 0xffff) * 0x18);
    int rec = *(int *)(self + 0x7c);
    float *dst = (float *)(*(int *)(*(int *)(self + 4) + 0x10) + slot * 0x40);

    mtx4_set_translate_rotz((double)anchor[0], (double)anchor[1], (double)anchor[2],
                            (double)((FLOAT_80618908 * *(float *)(rec + 0x24)) / FLOAT_8061890c),
                            dst);
    mtx4_set_translation(-(double)anchor[0], -(double)anchor[1], -(double)anchor[2]);
    dst = (float *)(*(int *)(*(int *)(self + 4) + 0x10) + slot * 0x40);
    mtx4_multiply(m, dst, dst);
}

// 0x8023d4e0 | prop_skeleton_build
// Non-weapon prop skeleton builder. Reads the prop-definition record for the
// object type, computes the root transform, walks the bone list applying the
// per-bone animation channels, then runs a large per-type switch that fixes up
// special bones (turrets, wheels, hinged/animated attachments, etc.).
static void prop_skeleton_build(void)
{
    int self = (int)FUN_803826d8();
    int rec = *(int *)(self + 0x7c);
    int def = prop_def_by_index(*(int *)(rec + 0xc));

    unsigned int m88 = *(unsigned int *)(def + 0x88);
    unsigned int m90 = *(unsigned int *)(def + 0x90);
    unsigned int m7c = *(unsigned int *)(def + 0x7c);
    unsigned int m80 = *(unsigned int *)(def + 0x80);
    unsigned int m84 = *(unsigned int *)(def + 0x84);
    unsigned int m8c = *(unsigned int *)(def + 0x8c);

    float rootPos[3];      // local_bc / local_b8 / local_b4
    float mA[16];          // afStack_b0
    float mB[28];          // afStack_70

    prop_read_root_anchor(self, rootPos);
    mtx4_set_transform_euler((double)rootPos[0], (double)rootPos[1], (double)rootPos[2],
                             (double)((FLOAT_80618908 * *(float *)(rec + 0x1c)) / FLOAT_8061890c),
                             (double)((FLOAT_80618908 * *(float *)(rec + 0x24)) / FLOAT_8061890c),
                             (double)((FLOAT_80618908 * *(float *)(rec + 0x2c)) / FLOAT_8061890c),
                             mA);
    mtx4_set_translation(-(double)rootPos[0], -(double)rootPos[1], -(double)rootPos[2]);
    mtx4_multiply(mB, mA, mA);
    mtx4_table_reset_identity();

    int boneIdx = 1;
    int boneMtxOff = 0x40;
    int boneNodeOff = 0xc0;
    while (true) {
        int frame = *(int *)(self + 4);
        int *node = *(int **)(frame + 4);
        if (node[1] <= boneIdx) {
            break;
        }
        bool handled = false;
        unsigned int mask = *(unsigned int *)((int)node + boneNodeOff + 0x50 + *node * -0xc0);
        if ((mask == 0) || ((*(unsigned int *)(frame + 0x30) & mask) != 0)) {
            int chanCount = *(int *)(def + 0x30);
            if (chanCount != 0) {
                int ci = 0;
                int co = 0;
                if (0 < chanCount) {
                    do {
                        if (*(int *)(def + co + 0x34) == boneIdx) {
                            float fade = -(*(float *)(def + 8) * FLOAT_806108ec - FLOAT_806188f8);
                            float clamped = FLOAT_80618910;
                            if (FLOAT_80618910 < fade) {
                                clamped = fade;
                            }
                            int scaleOff = ci * 4 + 0x198;
                            *(float *)(rec + scaleOff) = *(float *)(rec + scaleOff) * clamped;

                            if (*(int *)(def + co + 0x3c) < 0) {
                                mtx4_copy(mA, (float *)(*(int *)(*(int *)(self + 4) + 0x10) + boneMtxOff));
                            } else {
                                float bonePose[3]; // local_c8/c4/c0
                                FUN_8023fac0(self, ci, bonePose);
                                mtx4_set_transform_euler(
                                    (double)bonePose[0], (double)bonePose[1], (double)bonePose[2],
                                    (double)FLOAT_80618910, (double)FLOAT_80618910,
                                    (double)((FLOAT_80618908 * *(float *)(rec + 0xb8)) / FLOAT_8061890c),
                                    mB);
                                mtx4_multiply(mB, mA,
                                              (float *)(*(int *)(*(int *)(self + 4) + 0x10) + boneMtxOff));
                                mtx4_set_translation(-(double)bonePose[0], -(double)bonePose[1],
                                                     (double)(*(float *)(rec + scaleOff) - bonePose[2]));
                                float *bm = (float *)(*(int *)(*(int *)(self + 4) + 0x10) + boneMtxOff);
                                mtx4_multiply(mB, bm, bm);
                            }
                            handled = true;
                            break;
                        }
                        ci = ci + 1;
                        co = co + 0xc;
                        chanCount = chanCount + -1;
                    } while (chanCount != 0);
                }
            }
            if (!handled) {
                if (((boneIdx == *(int *)(def + 0x20)) || (boneIdx == *(int *)(def + 0x24))) ||
                    ((mask & (m8c | m84 | m80 | m7c | m88 | m90)) != 0)) {
                    mtx4_copy(mA, (float *)(*(int *)(*(int *)(self + 4) + 0x10) + boneMtxOff));
                } else {
                    mtx4_set_identity(*(int *)(*(int *)(self + 4) + 0x10) + boneMtxOff);
                }
            }
        }
        boneIdx = boneIdx + 1;
        boneMtxOff = boneMtxOff + 0x40;
        boneNodeOff = boneNodeOff + 0xc0;
    }

    // Per prop-type special-bone fixups.
    int frame = *(int *)(self + 4);
    int *node = *(int **)(frame + 4);
    switch (*(int *)(rec + 0xc)) {
    case 7: {
        int b = node[*node * -0x30 + 0x1b];
        mtx4_set_translate_rotz((double)*(float *)(b + 0x30), (double)*(float *)(b + 0x34),
                                (double)*(float *)(b + 0x38),
                                (double)((FLOAT_80618908 * *(float *)(rec + 0x24)) / FLOAT_8061890c),
                                (float *)(*(int *)(frame + 0x10) + 0x40));
        mtx4_set_translation(-(double)*(float *)(b + 0x30), -(double)*(float *)(b + 0x34),
                             -(double)*(float *)(b + 0x38));
        float *bm = (float *)(*(int *)(*(int *)(self + 4) + 0x10) + 0x40);
        mtx4_multiply(mB, bm, bm);
        break;
    }
    case 9: {
        float *b = (float *)node[*node * -0x30 + 0x1b];
        mtx4_set_translate_rotz((double)b[0], (double)b[1], (double)b[2],
                                (double)((FLOAT_80618908 * *(float *)(rec + 0x24)) / FLOAT_8061890c),
                                (float *)(*(int *)(frame + 0x10) + 0x40));
        mtx4_set_translation(-(double)b[0], -(double)b[1], -(double)b[2]);
        float *bm = (float *)(*(int *)(*(int *)(self + 4) + 0x10) + 0x40);
        mtx4_multiply(mB, bm, bm);
        mtx4_set_transform_euler((double)rootPos[0], (double)rootPos[1], (double)rootPos[2],
                                 (double)((FLOAT_80618908 * *(float *)(rec + 0x1c)) / FLOAT_8061890c),
                                 (double)FLOAT_80618910,
                                 (double)((FLOAT_80618908 * *(float *)(rec + 0x2c)) / FLOAT_8061890c),
                                 mA);
        mtx4_set_translation(-(double)rootPos[0], -(double)rootPos[1], -(double)rootPos[2]);
        mtx4_multiply(mB, mA, mA);
        int table = *(int *)(*(int *)(self + 4) + 0x10);
        mtx4_multiply(mA, (float *)(table + 0x40),
                      (float *)(table + *(int *)((int)b + 0x20) * 0x40));
        break;
    }
    case 0xb:
    case 0xc: {
        float *b = (float *)node[*node * -0x30 + 0x1b];
        mtx4_set_translate_rotz((double)b[0], (double)b[1], (double)b[2],
                                (double)((FLOAT_80618908 * *(float *)(rec + 0x24)) / FLOAT_8061890c),
                                (float *)(*(int *)(frame + 0x10) + 0x40));
        mtx4_set_translation(-(double)b[0], -(double)b[1], -(double)b[2]);
        float *bm = (float *)(*(int *)(*(int *)(self + 4) + 0x10) + 0x40);
        mtx4_multiply(mB, bm, bm);
        break;
    }
    case 0x12:
    case 0x13:
    case 0x16:
    case 0x17:
        prop_bone_matrix_apply(self, 0, 1, mB);
        break;
    case 0x14:
        prop_bone_matrix_apply(self, 3, 1, mB);
        break;
    case 0x18: {
        int slot = -1;
        int cls = *(int *)(*(int *)(rec + 0x6c) + 0x30);
        bool alt = *(float *)(self + 0x1cc) <= FLOAT_80618910;
        if (cls == 0x2a) {
            slot = alt ? 0xb : 0xf;
        } else if (cls == 0x10 || cls == 0xb || cls == 4 || cls == 0x26 ||
                   cls == 0x34 || cls == 0x3a || cls == 0x40) {
            slot = alt ? 0xc : 0x10;
        }
        mtx4_copy(mA, (float *)(*(int *)(frame + 0x10) + 0x140));
        if (-1 < slot) {
            mtx4_copy(mA, (float *)(*(int *)(*(int *)(self + 4) + 0x10) + slot * 0x40));
        }
        break;
    }
    case 0x19: {
        int slot = -1;
        int cls = *(int *)(*(int *)(rec + 0x6c) + 0x30);
        if (cls == 0x2a) {
            slot = 10;
        } else if (cls == 0x10 || cls == 0xb || cls == 4 || cls == 0x26 ||
                   cls == 0x34 || cls == 0x3a || cls == 0x40) {
            slot = 0xb;
        }
        mtx4_copy(mA, (float *)(*(int *)(frame + 0x10) + 0x100));
        if (-1 < slot) {
            mtx4_copy(mA, (float *)(*(int *)(*(int *)(self + 4) + 0x10) + slot * 0x40));
        }
        break;
    }
    case 0x1b: {
        int slot = 0;
        bool alt = *(float *)(self + 0x1cc) <= FLOAT_80618910;
        int cls = *(int *)(*(int *)(rec + 0x6c) + 0x30);
        if (cls == 0x2a) {
            slot = alt ? 0x11 : 7;
        } else if (cls == 0x10 || cls == 0xb || cls == 4 || cls == 0x26 ||
                   cls == 0x34 || cls == 0x3a || cls == 0x40) {
            slot = alt ? 0x12 : 0xd;
        }
        mtx4_copy(mA, (float *)(*(int *)(frame + 0x10) + slot * 0x40));
        mtx4_copy(mA, (float *)(*(int *)(*(int *)(self + 4) + 0x10) + 0x100));
        break;
    }
    case 0x1c: {
        int slot = 0;
        bool alt = *(float *)(self + 0x1cc) <= FLOAT_80618910;
        int cls = *(int *)(*(int *)(rec + 0x6c) + 0x30);
        if (cls == 0x2a) {
            slot = alt ? 0x11 : 7;
        } else if (cls == 0x10 || cls == 0xb || cls == 4 || cls == 0x26 ||
                   cls == 0x34 || cls == 0x3a || cls == 0x40) {
            slot = alt ? 0x12 : 8;
        }
        mtx4_copy(mA, (float *)(*(int *)(frame + 0x10) + slot * 0x40));
        mtx4_copy(mA, (float *)(*(int *)(*(int *)(self + 4) + 0x10) + 0x4c0));
        prop_bone_matrix_apply(self, 0x50000, 4, mB);
        break;
    }
    case 0x1d:
        prop_bone_matrix_apply(self, 0, 1, mB);
        break;
    default:
        break;
    }
    FUN_80382724();
}

// 0x8023ddfc | prop_fire_tick
// The prop / weapon fire path. Resolves the object's per-side fire descriptor,
// gates on cooldown / ammo / pose-ready, and for each pending shot computes the
// fire origin and direction (with rng spread), spawns the projectile, restarts
// the muzzle and trail particle emitters, records weapon stats, and plays the
// fire sound.
void prop_fire_tick(int self_unused, int owner_unused, int side, int a4, int param5, int *param6)
{
    // param_1/param_2 are consumed only as the register-spill work-context pair
    // in the original (re-fetched below via FUN_803826c0); ignored here.
    (void)self_unused;
    (void)owner_unused;
    float dir[3];          // local_11c / local_118 / local_114 (also fire origin dir)
    float originA[3];      // local_158 / local_154 / local_150
    float originB[3];      // local_140 / local_13c / local_138
    float tmpB[3];         // local_128 / local_124 / local_120
    float tmpC[3];         // local_134 / local_130 / local_12c
    float tmpD[3];         // local_14c / local_148 / local_144
    float basis[6];        // local_17c..local_168 (muzzle-offset template)
    unsigned int emitterPos[3]; // auStack_188
    int idHolderA;         // local_18c
    int idHolderB;         // local_190
    float mLocal[16];      // afStack_110
    float mAim[16];        // afStack_d0

    unsigned long long ctx = FUN_803826c0();
    int self = (int)(ctx >> 0x20);
    float *state = (float *)ctx;
    int rec = *(int *)(self + 0x7c);
    int base = DAT_80610ba0 + (int)state[0xc] * 0x29c;
    int desc = match_slot_field_ptr(base, side);
    int trailBone = 0;
    int shots = prop_select_model_field(desc, rec);

    int rounds = 1;
    if (*(int *)(base + 0x18) != 0) {
        rounds = *(int *)(base + 0x18);
    }
    *(short *)((int)state + *(int *)(base + 0x14) * 2 + 0x38) = (short)rounds;

    unsigned int recFlags = *(unsigned int *)(rec + 8);
    if (((DAT_806108e4 == 0) || ((recFlags & 2) != 0)) ||
        (((recFlags & 1) != 0) && ((*(int *)(rec + 0x10) == 9) || (*(int *)(rec + 0x10) == 1)))) {
        FUN_8038270c();
        return;
    }
    if ((*(unsigned int *)(self + 0x28) & 0x200000) == 0) {
        FUN_8038270c();
        return;
    }

    float maxShots = (side == 0) ? state[0x3c] : state[0x3d];

    int propInst = 0;
    if ((recFlags & 0x40) == 0) {
        propInst = prop_def_by_index(*(int *)(rec + 0xc));
    }

    // Cooldown / rate advance. The (float)(magic - bias) idiom converts the
    // integer rng/frame counter to a float; kept as a plain frame-scaled value.
    float frameScale = FLOAT_806108ec *
        (FLOAT_806188f8 / (float)((double)CONCAT44(0x43300000, DAT_806109c0) - DOUBLE_80618900));

    if (side == 1 || (side < 1 && (-1 <= side))) {
        int shotBudget = (side == 1)
            ? (int)(*(float *)(desc + 0x14) * frameScale)
            : (int)(*(float *)(desc + 0x10) * frameScale);
        if ((int)maxShots < shotBudget) {
            FUN_8038270c();
            return;
        }
    }

    state[0x41] = frameScale + state[0x41];

    if ((*(unsigned int *)(base + 0xc) & 0x200) != 0) {
        if (state[0xc] == 7.42688e-44f) {
            state[0x42] = state[0x42] + (frameScale / FLOAT_80618914);
        } else {
            state[0x42] = state[0x42] + (frameScale / FLOAT_80618918);
        }
        if (FLOAT_8061891c < state[0x42]) {
            state[0x42] = FLOAT_80618910;
        }
        if (state[0x42] < FLOAT_806188f8) {
            FUN_8038270c();
            return;
        }
    }

    for (int s = 0; s < shots; s = s + 1) {
        if ((recFlags & 0x40) == 0) {
            int *node = *(int **)(*(int *)(self + 4) + 4);
            int base0 = *node;
            unsigned int packed = *(unsigned int *)(propInst + *(int *)(rec + 0x70) * 0xc + 0x38);
            int hi = (int)packed >> 0x10;
            int lo = (packed & 0xffff) * 0x18;
            float *anchor = (float *)(node[base0 * -0x30 + hi * 0x30 + 0x1b] + lo);
            dir[0] = anchor[0];
            dir[1] = anchor[1];
            dir[2] = anchor[2];
            *(int *)(rec + *(int *)(rec + 0x70) * 4 + 0x198) = *(int *)(propInst + 4);
            int b = *(int *)(propInst + *(int *)(rec + 0x70) * 0xc + 0x34);
            if (-1 < b) {
                trailBone = b;
            }
            int cursor = *(int *)(rec + 0x70) + 1;
            *(int *)(rec + 0x70) = cursor;
            if (*(int *)(propInst + 0x30) <= cursor) {
                *(int *)(rec + 0x70) = 0;
            }
        } else {
            int cont = weapon_fire_tick(self, state, side, param5, dir);
            if (cont == 0) {
                break;
            }
        }

        state[0x3c] = 0.0f;
        if ((side == 1) || ((*(unsigned int *)(base + 0xc) & 0x400) == 0)) {
            state[0x3d] = 0.0f;
        }

        if ((recFlags & 0x40) == 0) {
            if ((recFlags & 1) == 0) {
                FUN_8023fec0(self, state);
                entity_build_local_matrix(mLocal, self, 0, 0);
                int t = trailBone * 0x40;
                int table = *(int *)(*(int *)(self + 4) + 0x10);
                dir[0] = *(float *)(table + t + 0x20);
                dir[1] = *(float *)(table + t + 0x24);
                dir[2] = *(float *)(table + t + 0x28);
                mtx4_transform_vector_alias(mLocal, dir);
                dir[0] = *(float *)(rec + 0xb4) *
                             (FLOAT_8061891c *
                              ((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_80618930) /
                               FLOAT_80618924) - FLOAT_806188f8) + dir[0];
                dir[1] = *(float *)(rec + 0xb4) *
                             (FLOAT_8061891c *
                              ((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_80618930) /
                               FLOAT_80618924) - FLOAT_806188f8) + dir[1];
                dir[2] = *(float *)(rec + 0xb4) *
                             (FLOAT_8061891c *
                              ((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_80618930) /
                               FLOAT_80618924) - FLOAT_806188f8) + dir[2];
                originA[0] = state[0];
                originA[1] = state[1];
                originA[2] = state[2];
            } else {
                FUN_8023fec0(self, state);
                FUN_8023fd34(self, originA);
                FUN_8023fe0c();
                double sx = (double)(FLOAT_80618928 * *(float *)(desc + 0x20) *
                    (FLOAT_8061891c *
                     ((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_80618930) /
                      FLOAT_80618924) - FLOAT_806188f8));
                double sy = (double)(FLOAT_80618928 * *(float *)(desc + 0x20) *
                    (FLOAT_8061891c *
                     ((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_80618930) /
                      FLOAT_80618924) - FLOAT_806188f8));
                basis[3] = DAT_803e01f4;   // local_17c
                basis[2] = DAT_803e01f0;   // local_170..168 reordered below
                float aimUp[3];
                float aimRight[3];
                aimUp[0]   = DAT_803e01e8; // local_170
                aimUp[1]   = DAT_803e01ec; // local_16c
                aimUp[2]   = DAT_803e01f0; // local_168
                aimRight[0] = DAT_803e01f4; // local_17c
                aimRight[1] = DAT_803e01f8; // local_178
                aimRight[2] = DAT_803e01fc; // local_174
                mtx4_set_rotation_y((double)*(float *)(rec + 0x24), mAim);
                mtx4_transform_vector(mAim, aimRight);
                mtx4_set_rotation_z((double)*(float *)(rec + 0x1c), mAim);
                mtx4_transform_vector(mAim, aimUp);
                float len2 = dir[2] * dir[2] + dir[0] * dir[0] + dir[1] * dir[1];
                float inv = 1.0f / SQRT(len2);
                float scaled = FLOAT_80618928 * len2;
                inv = inv * -(scaled * inv * inv - FLOAT_8061892c);
                float fwd = FLOAT_806188f8 * inv * -(scaled * inv * inv - FLOAT_8061892c);
                dir[0] = (float)(sy * (double)aimUp[0] +
                                 (double)(dir[0] * fwd + (float)(sx * (double)aimRight[0])));
                dir[1] = (float)(sy * (double)aimUp[1] +
                                 (double)(dir[1] * fwd + (float)(sx * (double)aimRight[1])));
                dir[2] = (float)(sy * (double)aimUp[2] +
                                 (double)(dir[2] * fwd + (float)(sx * (double)aimRight[2])));
            }
            originB[0] = state[0];
            originB[1] = state[1];
            originB[2] = state[2];
        } else {
            weapon_compute_fire_origin(self, *(unsigned int *)(rec + 0x70), side, state, dir);
            state[0] = FLOAT_80618920 * dir[0] + state[0];
            state[1] = FLOAT_80618920 * dir[1] + state[1];
            state[2] = FLOAT_80618920 * dir[2] + state[2];
            dir[0] = *(float *)(rec + 0xb4) *
                         (FLOAT_8061891c *
                          ((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_80618930) /
                           FLOAT_80618924) - FLOAT_806188f8) + dir[0];
            dir[1] = *(float *)(rec + 0xb4) *
                         (FLOAT_8061891c *
                          ((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_80618930) /
                           FLOAT_80618924) - FLOAT_806188f8) + dir[1];
            dir[2] = *(float *)(rec + 0xb4) *
                         (FLOAT_8061891c *
                          ((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_80618930) /
                           FLOAT_80618924) - FLOAT_806188f8) + dir[2];
            originA[0] = state[0];
            originA[1] = state[1];
            originA[2] = state[2];
            originB[0] = originA[0];
            originB[1] = originA[1];
            originB[2] = originA[2];
        }

        tmpB[0] = dir[0];  tmpB[1] = dir[1];  tmpB[2] = dir[2];
        tmpC[0] = dir[0];  tmpC[1] = dir[1];  tmpC[2] = dir[2];
        tmpD[0] = originB[0]; tmpD[1] = originB[1]; tmpD[2] = originB[2];

        if (*(int *)(rec + 0xb0) != 0) {
            int spawned = object_projectile_spawn_dispatch(
                (double)dir[1], (double)dir[0], self, state, 0, side,
                (unsigned int *)originA, (unsigned int *)tmpD, originB,
                (unsigned int *)dir, (unsigned int *)tmpB, (unsigned int *)tmpC,
                param5, param6, 1);

            if ((*(int *)(desc + 0x70) != 0) &&
                (((unsigned int)*(int *)(rec + 0x70) & 1) ==
                 ((unsigned int)*(int *)(rec + 0x70) >> 0x1f))) {
                for (int i = 0; ; i = i + 4) {
                    int pid = *(int *)(*(int *)(desc + 0x70) + i);
                    if (pid < 0) break;
                    if (pid < *(int *)(self + 0x1e8)) {
                        idHolderA = *(int *)(*(int *)(self + 0x1fc) + pid * 4);
                    } else {
                        idHolderA = *(int *)&DAT_80611e2c;
                    }
                    particle_cached_restart(&idHolderA);
                }
            }
            if ((*(int *)(desc + 0x74) != 0) &&
                ((((unsigned int)*(int *)(rec + 0x70) & 1) ^
                  (unsigned int)((int)*(unsigned int *)(rec + 0x70) >> 0x1f)) +
                     ((int)*(unsigned int *)(rec + 0x70) >> 0x1f) == 1)) {
                for (int i = 0; ; i = i + 4) {
                    int pid = *(int *)(*(int *)(desc + 0x74) + i);
                    if (pid < 0) break;
                    if (pid < *(int *)(self + 0x1e8)) {
                        idHolderB = *(int *)(*(int *)(self + 0x1fc) + pid * 4);
                    } else {
                        idHolderB = *(int *)&DAT_80611e2c;
                    }
                    particle_cached_restart(&idHolderB);
                }
            }
            player_ai_spawn_trail_particle(0, *(unsigned int *)(desc + 0x68), 0xffffffff,
                                           state, dir, -1);
            if ((recFlags & 0x40) != 0) {
                weapon_fire_stub();
            }

            if ((spawned != 0) && ((recFlags & 0x40) != 0)) {
                worm_stat_add((double)FLOAT_806188f8, 8, param6[2]);
                worm_stat_add((double)FLOAT_806188f8, 0x40, param6[2]);
                *(int *)(spawned + 0x19c) = *(int *)(rec + 0x68);
            }
        }

        if (param6 == (int *)0) {
            object_play_fire_sound(state, side, (unsigned int *)(self + 100), a4);
        } else {
            sound_get_emitter_position(param6, emitterPos);
            object_play_fire_sound(state, side, emitterPos, a4);
        }
    }
    FUN_8038270c();
}
