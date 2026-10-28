// Address range: 0x80146384 - 0x801469cc | batch_412 | 2026-09-05
//
// Entity render sub-object CONSTRAINT SOLVER + integrator (companion to
// subobj_pool.cpp / subobj_build.cpp / subobj_dynamics.cpp).
//
// When a sub-object needs to react against another sub-object (or a static
// point) the engine builds a lightweight contact/velocity constraint record
// and threads it onto a per-frame priority-sorted list. Each frame the solver
// evaluates relative velocity along the constraint axis and the integrator
// advances each sub-object's position and orientation quaternion.
//
// Constraint pool: DAT_806121a4 holds up to 500 records of 0x38 bytes each;
// DAT_806109a0 is the free counter (records handed out from the top down),
// DAT_806121a0 is the pool base pointer. Records are singly linked via +0x34.
//
// Constraint record layout (word/byte offsets):
//   +0x00..+0x08  anchor point (world)                 (param_3 words 0..2)
//   +0x0c..+0x14  constraint axis / normal             (words 3..5)
//   +0x18         sort key (priority, lower = earlier) (word 6)
//   +0x1c         secondary sort key                   (word 7)  [list order]
//   +0x24         handle A owner entity                (word 9)
//   +0x28         handle B owner entity                (word 10)
//   +0x2c         flag halfword (+0x2c)                (halfword at +0x2c/0x2e)
//   +0x2e         constraint type (0..5)               (halfword)
//   +0x34         next-in-list pointer

#include "common.h"
#include "generated_decls.h"

extern "C" {
// spawn-frame push/pop
extern unsigned long long FUN_803826f0(void);   // push frame -> packed ptr|handle<<32
extern void               FUN_8038273c(void);    // pop frame (matches 803826f0)
extern unsigned long long FUN_803826e8(void);    // push frame (pair variant) -> packed handles
extern void               FUN_80382734(void);    // pop frame (matches 803826e8)

// per-object post-integrate hook for type-0x400000 owners
// subobj_rotor_drag_integrate (0x80156f58, batch_428) resolves via generated_decls
}

// pool tables (shared with subobj_pool.cpp)
extern int DAT_8061218c;   // descriptor pool base (stride 0x200)
extern int DAT_80612184;   // per-slot owner-entity pointer table (stride 4)

// constraint pool
extern int DAT_806109a0;   // free counter (records remaining)
extern int DAT_806121a0;   // current pool cursor (base of live block)
extern int DAT_806121a4;   // pool base (28000 bytes = 500 * 0x38)

// wind / time-scale globals used by the velocity-eval branch
extern int    DAT_806108e4;   // wind enabled flag
extern int    DAT_806109c0;   // wind phase counter (signed via xor 0x80000000)
extern float  FLOAT_806108ec; // wind period divisor
extern double DOUBLE_80615ed8;// magic double for int->double conversion bias

// shared float constants
extern float FLOAT_80615ea8;   // 0.0f
extern float FLOAT_80615eb0;   // 1.0f
extern float FLOAT_80615ebc;   // damping / pad factor
extern float FLOAT_80615ec4;   // normalize epsilon
extern float FLOAT_80615ec8;   // 1.5 (NR rsqrt refine const)
extern float FLOAT_80615ee0;   // angular decay factor

// ============================================================================
// 0x80146610  subobj_constraint_pool_alloc
// Hand out one 0x38-byte constraint record from the top of the pool, or 0 when
// exhausted.
// ============================================================================
int subobj_constraint_pool_alloc(void)
{
    if (0 < DAT_806109a0) {
        DAT_806109a0 = DAT_806109a0 - 1;
        return DAT_806121a0 + DAT_806109a0 * 0x38;
    }
    return 0;
}

// ============================================================================
// 0x8014663c  subobj_constraint_pool_reset
// Reset the constraint pool to 500 free records and rebase the cursor.
// ============================================================================
void subobj_constraint_pool_reset(void)
{
    DAT_806109a0 = 500;
    DAT_806121a0 = DAT_806121a4;
}

// ============================================================================
// 0x80146650  subobj_constraint_list_push
// Prepend a constraint record onto the singly-linked list head (*listHead),
// linking via +0x34.
// ============================================================================
void subobj_constraint_list_push(int* listHead, int record)
{
    *(int*)(record + 0x34) = *listHead;
    *listHead = record;
}

// ============================================================================
// 0x801467ec  subobj_constraint_list_insert_sorted
// Insert a constraint record into the list ordered by ascending sort key
// (+0x1c). Records with a key <= the new record's key stay ahead of it.
// ============================================================================
void subobj_constraint_list_insert_sorted(int* listHead, int record)
{
    int cur = *listHead;
    int prev;

    if ((cur != 0) && (*(float*)(cur + 0x1c) <= *(float*)(record + 0x1c))) {
        do {
            prev = cur;
            if (prev == 0) {
                return;
            }
            cur = *(int*)(prev + 0x34);
        } while ((cur != 0) && (*(float*)(cur + 0x1c) <= *(float*)(record + 0x1c)));
        *(int*)(record + 0x34) = cur;
        *(int*)(prev + 0x34) = record;
        return;
    }
    *listHead = record;
    *(int*)(record + 0x34) = cur;
}

// ============================================================================
// 0x80146660  subobj_constraint_init
// Populate a freshly allocated constraint record from an anchor point, axis,
// two owner entities and a bitmask, then classify its type (halfword +0x2e).
// When the second owner is a type-8 entity the axis is negated and
// renormalised (NR rsqrt refine). Type codes: 5 = joint (bone flag 1/2),
// 4 = flagged (mask 0x800), 1 = has sub-object handle, 0 = plain, 3 = type-8.
// ============================================================================
void subobj_constraint_init(double keyA, double keyB, float* record, float* anchor,
                            float* axis, unsigned short mask, int ownerA, int ownerB, int boneIdx)
{
    float  eps, zero;
    float  len2, invLen;
    int    boneRec = 0;

    record[7] = (float)keyA;   // +0x1c sort key
    record[6] = (float)keyB;   // +0x18 priority
    record[0] = anchor[0];     // +0x00 anchor
    record[1] = anchor[1];
    record[2] = anchor[2];
    *(unsigned short*)(record + 0xb) = mask;         // +0x2c
    *(short*)(record + 0xc) = (short)boneIdx;        // +0x30

    eps  = FLOAT_80615ec4;
    zero = FLOAT_80615ea8;

    if (-1 < boneIdx) {
        boneRec = *(int*)(*(int*)(*(int*)(ownerA + 4) + 4) + 0x14) + boneIdx * 0x30;
    }

    if ((ownerB != 0) && (*(int*)(ownerB + 0x20) == 8)) {
        record[3] = -axis[0];      // +0x0c negated axis
        record[4] = zero;
        record[5] = -axis[2];
        record[9]  = *(float*)&ownerA;   // +0x24
        record[10] = *(float*)&ownerB;   // +0x28
        *(unsigned short*)((int)record + 0x2e) = 3;
        {
            float ax = record[3];
            len2 = record[5] * record[5] + ax * ax + record[4] * record[4];
            if (len2 <= eps) {
                return;
            }
            invLen = 1.0f / SQRT(len2);
            invLen = invLen * -(FLOAT_80615ebc * len2 * invLen * invLen - FLOAT_80615ec8);
            invLen = FLOAT_80615eb0 * invLen * -(FLOAT_80615ebc * len2 * invLen * invLen - FLOAT_80615ec8);
            record[3] = ax * invLen;
            record[4] = record[4] * invLen;
            record[5] = record[5] * invLen;
        }
        return;
    }

    record[3] = axis[0];
    record[4] = axis[1];
    record[5] = axis[2];
    record[9]  = *(float*)&ownerB;   // note: swapped vs the type-8 path
    record[10] = *(float*)&ownerA;

    if ((boneRec != 0) &&
        ((*(char*)(boneRec + 0x18) == '\x02') || (*(char*)(boneRec + 0x18) == '\x01'))) {
        *(unsigned short*)((int)record + 0x2e) = 5;
        return;
    }
    if ((mask & 0x800) != 0) {
        *(unsigned short*)((int)record + 0x2e) = 4;
        return;
    }
    if (*(int*)(ownerA + 0x80) != 0) {
        *(unsigned short*)((int)record + 0x2e) = 1;
        return;
    }
    *(unsigned short*)((int)record + 0x2e) = 0;
}

// ============================================================================
// 0x8014684c  subobj_constraint_eval_rel_velocity
// Compute the relative velocity of the constraint's two ends in owner-A local
// space, apply the type-specific correction (type 1 subtracts owner-B local
// velocity; types 2/3 apply the wind offset), optionally output the local
// vector, and return its dot product with the constraint axis (+0x0c).
// ============================================================================
double subobj_constraint_eval_rel_velocity(float* outLocal, float* record)
{
    unsigned short type;
    int   ownerBBone;
    float axisX, axisY, axisZ;
    float wind;
    float velA[3];   // local_1c/18/14
    float velB[3];   // local_28/24/20

    subobj_world_to_local_dir(*(unsigned int*)((int)*(int*)(record + 9) + 0x80), record, velA);
    type = *(unsigned short*)((int)record + 0x2e);
    if (type != 2) {
        if (type < 2) {
            if ((type != 0) && ((*(unsigned int*)((int)*(int*)(record + 10) + 0x80) & 0x80000000) == 0)) {
                subobj_world_to_local_dir(*(unsigned int*)((int)*(int*)(record + 10) + 0x80), record, velB);
                velA[0] -= velB[0];
                velA[1] -= velB[1];   // local_18 - local_24
                velA[2] -= velB[2];   // local_14 - local_20
            }
        } else if ((type < 6) && (type < 4)) {   // type == 2 or 3 handled here (3)
            ownerBBone = *(int*)((int)*(int*)(record + 10) + 0x7c);
            if (DAT_806108e4 != 0) {
                wind = (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80615ed8)
                       / FLOAT_806108ec;
                velA[0] -= *(float*)(ownerBBone + 0x3c) * wind;
                velA[1] -= *(float*)((int)*(int*)(record + 10) + 0xfc) * wind;
                velA[2] -= *(float*)(ownerBBone + 0x40) * wind;
            }
        }
    }

    axisY = record[4];
    axisX = record[3];
    axisZ = record[5];
    if (outLocal != (float*)0) {
        outLocal[0] = velA[0];
        outLocal[1] = velA[1];
        outLocal[2] = velA[2];
    }
    return (double)(velA[2] * axisZ + velA[0] * axisX + velA[1] * axisY);
}

// ============================================================================
// 0x80146384  subobj_constraint_apply_torque_impulse
// Spawn-frame entry: cross the frame's local vector with a supplied axis and
// transform the result by the sub-object rotation submatrix (+0x40) to apply a
// torque impulse.
// ============================================================================
void subobj_constraint_apply_torque_impulse(unsigned int dummy0, unsigned int dummy1, float* axis)
{
    unsigned long long frame;
    float* v;
    float  torque[3];   // local_38/34/30

    (void)dummy0; (void)dummy1;
    frame = FUN_803826f0();
    v = (float*)(unsigned int)frame;
    torque[0] = v[1] * axis[2] - v[2] * axis[1];
    torque[2] = v[0] * axis[1] - v[1] * axis[0];
    torque[1] = v[2] * axis[0] - v[0] * axis[2];
    mtx4_transform_vector_alias(
        (float*)(DAT_8061218c + ((unsigned int)(frame >> 0x20) & 0xffff) * 0x200 + 0x40),
        torque);
    FUN_8038273c();
}

// ============================================================================
// 0x80146474  subobj_constraint_apply_pair_impulse
// Pair-frame entry: cross two supplied vectors with a shared axis and transform
// each cross product by the respective sub-object rotation submatrix (+0x40) of
// the two handles packed in the frame. Applies equal/opposite angular impulses.
// ============================================================================
void subobj_constraint_apply_pair_impulse(unsigned int dummy0, unsigned int dummy1,
                                          float* vecA, float* vecB, float* axis)
{
    unsigned long long frame;
    int   descB;
    float cross[3];   // local_48/44/40

    (void)dummy0; (void)dummy1;
    frame = FUN_803826e8();
    cross[0] = vecA[1] * axis[2] - vecA[2] * axis[1];
    descB = DAT_8061218c + (int)(frame & 0xffff0000ffffULL) * 0x200;
    cross[2] = vecA[0] * axis[1] - vecA[1] * axis[0];
    cross[1] = vecA[2] * axis[0] - vecA[0] * axis[2];
    mtx4_transform_vector_alias(
        (float*)(DAT_8061218c + (int)((frame & 0xffff0000ffffULL) >> 0x20) * 0x200 + 0x40),
        cross);

    cross[0] = vecB[1] * axis[2] - vecB[2] * axis[1];
    cross[2] = vecB[0] * axis[1] - vecB[1] * axis[0];
    cross[1] = vecB[2] * axis[0] - vecB[0] * axis[2];
    mtx4_transform_vector_alias((float*)(descB + 0x40), cross);
    FUN_80382734();
}

// ============================================================================
// 0x801469cc  subobj_integrate
// Advance one sub-object descriptor by dt: integrate world position from its
// velocity (+0x50/+0x53), apply angular decay to the velocity, integrate the
// orientation quaternion (+0x40) from angular velocity (+0x4d) and renormalise,
// rebuild the object matrix (quat_to_matrix) plus its transpose products, and
// integrate the auxiliary vector (+0x4a). Type-0x400000 owners get FUN_80156f58.
// ============================================================================
void subobj_integrate(double dt, float* desc)
{
    float  scaledDt;
    int    owner;
    float  decay;
    float  angScale;   // local_a4
    float  dq[4];      // local_bc/b8/b4/b0
    float  dqIn[3];    // local_ac/a8/a4 (scaled angular velocity, w=0)
    float  qw;         // local_a0
    float  aux[3];     // local_c8/c4/c0
    float  transp[16]; // afStack_5c
    float  tmp[16];    // afStack_9c

    scaledDt = (float)((double)desc[0x6a] * dt);
    owner = *(int*)(DAT_80612184 + ((unsigned int)desc[0x79] & 0xffff) * 4);
    angScale = (float)((double)FLOAT_80615ebc * dt);
    desc[0x50] = (float)((double)desc[0x53] * dt + (double)desc[0x50]);
    decay = FLOAT_80615ee0;
    qw = FLOAT_80615ea8;
    desc[0x51] = (float)((double)desc[0x54] * dt + (double)desc[0x51]);
    desc[0x52] = (float)((double)desc[0x55] * dt + (double)desc[0x52]);

    desc[0x53] = (decay * desc[0x53] + desc[0x44]) * scaledDt + desc[0x53];
    desc[0x54] = (decay * desc[0x54] + desc[0x45]) * scaledDt + desc[0x54];
    desc[0x55] = (decay * desc[0x55] + desc[0x46]) * scaledDt + desc[0x55];

    dqIn[0] = desc[0x4d] * angScale;
    dqIn[1] = desc[0x4e] * angScale;
    dqIn[2] = desc[0x4f] * angScale;
    (void)qw;
    quat_multiply(dq, dqIn, desc + 0x40);
    desc[0x40] += dq[0];
    desc[0x41] += dq[1];
    desc[0x42] += dq[2];
    desc[0x43] += dq[3];
    quat_normalize(desc + 0x40);
    quat_to_matrix(desc, desc + 0x40);
    mtx4_transpose(desc, transp);
    mtx4_multiply(desc + 0x20, desc, tmp);
    mtx4_multiply(transp, tmp, desc + 0x10);

    desc[0x4a] = (float)((double)desc[0x47] * dt + (double)desc[0x4a]);
    desc[0x4b] = (float)((double)desc[0x48] * dt + (double)desc[0x4b]);
    desc[0x4c] = (float)((double)desc[0x49] * dt + (double)desc[0x4c]);
    aux[0] = desc[0x4a];
    aux[1] = desc[0x4b];
    aux[2] = desc[0x4c];
    mtx4_transform_vector_alias(desc + 0x10, aux);
    desc[0x4d] = aux[0];
    desc[0x4e] = aux[1];
    desc[0x4f] = aux[2];

    if ((owner != 0) && (*(int*)(owner + 0x20) == 0x400000)) {
        subobj_rotor_drag_integrate(dt, owner);
    }
}
