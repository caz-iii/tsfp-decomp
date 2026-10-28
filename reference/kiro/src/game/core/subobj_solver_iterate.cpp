// Address range: 0x8014b058 - 0x8014c130 | batch_418 | 2026-09-05
//
// Entity render sub-object CONSTRAINT SOLVER - iterative LCP / effective-mass
// kernel (companion to subobj_solver.cpp / subobj_collision.cpp /
// subobj_dynamics.cpp / subobj_break.cpp).
//
// Once per frame each active sub-object "island" (one body, or a linked pair)
// runs a projected Gauss-Seidel style velocity solve. The kernel:
//   1. builds a per-body Jacobian matrix J (rows = constraints, each row a
//      6-wide {linear.xyz, angular.xyz} block, stride 0x20 bytes),
//   2. forms the effective-mass matrix  A = J * M^-1 * J^T  (subobj_solver_*_mac
//      helpers below), mirrors its lower triangle, adds a CFM/softness term to
//      the diagonal,
//   3. builds the right-hand side (bias + drift + current relative velocity),
//   4. calls the shared iterative solver solver_iterative_constrained() to get
//      the constraint impulses (lambda), and
//   5. scatters lambda back onto each body's linear/angular accumulators
//      (+0x110 linear force, +0x11c torque).
//
// Descriptor rigid-body layout referenced here (byte offsets into the 0x200
// descriptor at DAT_8061218c + slot*0x200):
//   +0x40   object->world 3x3 basis (used by mtx4_transform_vector_alias)
//   +0x110  linear force accumulator (xyz)
//   +0x11c  torque accumulator (xyz, 6 words 0x11c..0x130)
//   +0x134  world inverse-inertia row 0
//   +0x138  world inverse-inertia row 1
//   +0x13c  world inverse-inertia row 2
//   +0x140  center-of-mass world position (xyz)
//   +0x14c  linear impulse cache (xyz, 0x14c..0x154)
//   +0x1a8  inverse mass (scalar)
//   +0x1c0  flags
//
// Constraint island record (0x80-stride block at DAT_806121b4 + idx*0x80):
//   +0x10   3 axis rows (per-axis limit vectors, 3 x 0x10)
//   +0x5c   drift/baumgarte gain
//   +0x60   dt for this island
//   +0x68   restitution slop enable
//   +0x70   body-A descriptor handle
//   +0x74   body-B descriptor handle (0 = single-body island)

#include "common.h"
#include "generated_decls.h"

extern "C" {
// spawn / pair frame push-pop (packed pointer|handle<<32 in the returned u64)
extern unsigned long long FUN_803826f0(void);   // push single-body frame
extern void               FUN_8038273c(void);    // pop  (matches 803826f0)
extern unsigned long long FUN_803826e8(void);    // push pair frame
extern void               FUN_80382734(void);    // pop  (matches 803826e8)
extern unsigned long long FUN_803826d4(void);    // push solve frame -> {J_A ptr, J_B ptr}
extern void               FUN_80382720(void);    // pop  (matches 803826d4)

// sub-object bank / material lookup used by the impact-sound branch.
// returns non-zero when the material has an associated sound bank; fills the
// [lo, hi, extra] range for random sound selection.
extern int FUN_802532b0(unsigned int material, int* outLo, int* outHi, int* outExtra);
}

// pool tables (shared with subobj_pool.cpp)
extern int DAT_8061218c;   // descriptor pool base (stride 0x200)
extern int DAT_80612184;   // per-slot owner-entity pointer table (stride 4)
extern int DAT_806121b4;   // constraint-island pool base (stride 0x80)

// time / conversion globals
extern int    DAT_806109c0;   // frame / phase counter (signed via xor 0x80000000)
extern double DOUBLE_80615ed8;// 2^52 magic-double bias for int->double

// shared float constants
extern float FLOAT_80615ea8;   // 0.0f
extern float FLOAT_80615eb0;   // 1.0f
extern float FLOAT_80615eb4;   // small default lambda-hi bound
extern float FLOAT_80615eb8;   // lambda-lo bound (negative clamp)
extern float FLOAT_80615ebc;   // 0.5f (NR rsqrt refine)
extern float FLOAT_80615ec8;   // 3.0f (NR rsqrt refine)
extern float FLOAT_80615ee8;   // relative-velocity clamp
extern float FLOAT_80615eec;   // sound pitch bias
extern float FLOAT_80615ef0;   // sound pitch scale
extern float FLOAT_80615ef8;   // penetration bias threshold
extern float FLOAT_80615f10;   // baumgarte gain scale
extern float FLOAT_80615f48;   // axis-2 selector constant (1.0f alt)
extern float FLOAT_80615f4c;   // length^2 epsilon

// raw fracture / rng helpers (declared in generated_decls with these arities)
// subobj_fracture_spawn_default(...) and rng_counter_tick() come from shared decls.

// subobj_solver_solve_island is called cross-file (subobj_solver_angular_frame_dispatch
// in subobj_sim.cpp at 0x8014c49c), so it has external linkage. Forward-declare
// subobj_solver_apply_axis_impulse (defined below) for the frame dispatcher.
void subobj_solver_solve_island(double dt, float* massMtxScratch,
                                unsigned int biasA, unsigned int biasB,
                                int rhs, int loBounds, int hiBounds,
                                int bodyA, int bodyB, int nRows);
void subobj_solver_apply_axis_impulse(unsigned int islandHandle, float* pointWorld,
                                      float* axis);

// ===========================================================================
// 0x8014b058  subobj_fracture_spawn_impact
// Break-off callback fired from subobj_break_tick_impl: emit the material
// impact sound (pitch scaled by impact speed) for the struck sub-object, then
// spawn its fracture debris via subobj_fracture_spawn_default.
// (Called as FUN_8014b058 from subobj_break.cpp.)
// ===========================================================================
int subobj_fracture_spawn_impact(double impactSpeed, double p2, double p3, double p4,
                                 double p5, double p6, double p7,
                                 unsigned long long p8, unsigned int p9,
                                 unsigned int p10, float* param11)
{
    int   soundLo;
    int   soundHi;
    int   soundExtra;
    int   playParams[6];
    float pitch;
    unsigned char oneShot;
    float baseGain;
    unsigned int matWord0;
    unsigned int matWord1;
    unsigned int matWord2;

    unsigned long long framed = FUN_803826e8();
    unsigned int slot = (unsigned int)(framed >> 0x20);
    int desc = *(int*)(DAT_80612184 + (slot & 0xffff) * 4);
    int island = DAT_8061218c + (slot & 0xffff) * 0x200;

    if (FUN_802532b0((unsigned int)*(unsigned short*)(desc + 0x94),
                     &soundLo, &soundHi, &soundExtra) != 0) {
        unsigned int range = (unsigned int)((soundHi - soundLo) + 1);
        unsigned int r = ((unsigned int(*)(void))&rng_counter_tick)();
        sound_init_play_params((int)(unsigned long long)(playParams));

        pitch = (float)((double)FLOAT_80615ef0 * impactSpeed + (double)FLOAT_80615eec);
        oneShot = 1;
        baseGain = FLOAT_80615eb0;
        (void)oneShot;
        (void)baseGain;
        (void)matWord0; (void)matWord1; (void)matWord2;

        playParams[3] = *(int*)(desc + 100);   // world pos x (+0x64)
        playParams[4] = *(int*)(desc + 0x68);  // world pos y
        playParams[5] = *(int*)(desc + 0x6c);  // world pos z
        *(float*)&playParams[2] = pitch;
        playParams[0] = soundHi + (int)(r - (r / range) * range);
        sound_play_request(playParams);
    }

    subobj_fracture_spawn_default(
        (unsigned long long)(long long)impactSpeed, p2, p3, p4, p5, p6, p7, p8,
        (unsigned int)desc, *(unsigned int*)(island + 0x1c4),
        (unsigned int)*(unsigned char*)(island + 0x1cc),
        *(unsigned int*)(island + 0x1c0) & 0xff5dd0bf,
        (float*)(unsigned long long)framed, param11);

    FUN_80382734();
    return 1;
}

// ===========================================================================
// 0x8014b174  subobj_solver_build_constraint_row
// Build one constraint row for a contact/limit: fill the Jacobian (linear +
// angular {r x n}) for body A (and body B when the record is a two-body pair,
// type 1), evaluate the current relative velocity along the axis, compute the
// bias/target velocity with a penetration-restitution term, and write the
// lambda lo/hi bounds. Returns 1 (row valid).
// ===========================================================================
unsigned int subobj_solver_build_constraint_row(float* jacobianA, float* jacobianB,
                                                float* outTargetVel, unsigned int* outLoBound,
                                                unsigned int* outHiBound, float* record)
{
    float rx, ry, rz;
    float relVel;
    float clampVel;
    int   descA = DAT_8061218c + (*(unsigned int*)((int)record[9] + 0x80) & 0xffff) * 0x200;
    unsigned int handleB = *(unsigned int*)((int)record[10] + 0x80);

    float drift = FLOAT_80615f10 *
        (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80615ed8);

    clampVel = FLOAT_80615ea8;
    if (((*(unsigned int*)(descA + 0x1c0) & 0x10) == 0) &&
        (*(short*)((int)record + 0x2e) != 3)) {
        float neg = -record[6];
        float lo = neg;
        if (FLOAT_80615ee8 < neg) {
            lo = FLOAT_80615ee8;
        }
        clampVel = FLOAT_80615ea8;
        if ((FLOAT_80615ea8 <= lo) && (FLOAT_80615ee8 < neg)) {
            clampVel = FLOAT_80615ee8;
        } else if (FLOAT_80615ea8 <= lo) {
            clampVel = neg;
        }
    }

    rx = record[0] - *(float*)(descA + 0x140);
    ry = record[1] - *(float*)(descA + 0x144);
    rz = record[2] - *(float*)(descA + 0x148);
    jacobianA[0] = record[3];
    jacobianA[1] = record[4];
    jacobianA[2] = record[5];
    jacobianA[4] = ry * jacobianA[2] - rz * jacobianA[1];
    jacobianA[5] = rz * jacobianA[0] - rx * jacobianA[2];
    jacobianA[6] = rx * jacobianA[1] - ry * jacobianA[0];

    relVel =
        jacobianA[2] * *(float*)(descA + 0x154) +
        jacobianA[0] * *(float*)(descA + 0x14c) + jacobianA[1] * *(float*)(descA + 0x150) +
        jacobianA[6] * *(float*)(descA + 0x13c) +
        jacobianA[4] * *(float*)(descA + 0x134) + jacobianA[5] * *(float*)(descA + 0x138);

    if (*(short*)((int)record + 0x2e) == 1) {
        int descB = DAT_8061218c + (handleB & 0xffff) * 0x200;
        float rbx = record[0] - *(float*)(descB + 0x140);
        float rbz = record[2];
        float bz = *(float*)(descB + 0x148);
        float rby = record[1] - *(float*)(descB + 0x144);
        jacobianB[0] = -record[3];
        rbz = rbz - bz;
        jacobianB[1] = -record[4];
        jacobianB[2] = -record[5];
        jacobianB[4] = rby * jacobianB[2] - rbz * jacobianB[1];
        jacobianB[5] = rbz * jacobianB[0] - rbx * jacobianB[2];
        jacobianB[6] = rbx * jacobianB[1] - rby * jacobianB[0];
        relVel = relVel +
            jacobianB[2] * *(float*)(descB + 0x154) +
            jacobianB[0] * *(float*)(descB + 0x14c) + jacobianB[1] * *(float*)(descB + 0x150) +
            jacobianB[6] * *(float*)(descB + 0x13c) +
            jacobianB[4] * *(float*)(descB + 0x134) + jacobianB[5] * *(float*)(descB + 0x138);
    }

    bool penetrating = FLOAT_80615ef8 < -relVel;
    *outTargetVel = clampVel * drift;
    if (penetrating && (*outTargetVel < -record[8] * relVel)) {
        *outTargetVel = -record[8] * relVel;
    }
    *(float*)outLoBound = FLOAT_80615ea8;
    *(float*)outHiBound = FLOAT_80615eb4;
    return 1;
}

// ===========================================================================
// 0x8014b424  subobj_solver_jacobian_mac
// Effective-mass block build (overwrite): out[i][j] = dot6(J_A[i], MJ[j]) for
// the lower triangle, where each row is the 6-wide {linear.xyz, angular.xyz}
// laid out with the linear part at words 0..2 and angular at words 4..6 in a
// 0x20-byte stride. n = number of constraint rows.
// ===========================================================================
void subobj_solver_jacobian_mac(int outMtx, int jacobian, int massJacobian, int n)
{
    int rowByteBase = 0;
    for (int i = 0; i < n; i = i + 1) {
        int remaining = n - i;
        float* jRow = (float*)(jacobian + rowByteBase);
        int mjByte = i << 5;
        int outByte = i << 2;
        if (i < n) {
            do {
                float* mj = (float*)(massJacobian + mjByte);
                mjByte = mjByte + 0x20;
                *(float*)(outMtx + rowByteBase + outByte) =
                    jRow[6] * mj[6] + jRow[5] * mj[5] + jRow[4] * mj[4] +
                    jRow[2] * mj[2] + jRow[0] * mj[0] + jRow[1] * mj[1];
                outByte = outByte + 4;
                remaining = remaining - 1;
            } while (remaining != 0);
        }
        rowByteBase = rowByteBase + 0x20;
    }
}

// ===========================================================================
// 0x8014b4e0  subobj_solver_jacobian_mac_accum
// Same block build as subobj_solver_jacobian_mac but accumulates (+=) - used
// to add body-B's J*M^-1*J^T contribution onto the matrix already holding
// body-A's contribution.
// ===========================================================================
void subobj_solver_jacobian_mac_accum(int outMtx, int jacobian, int massJacobian, int n)
{
    int rowByteBase = 0;
    for (int i = 0; i < n; i = i + 1) {
        int remaining = n - i;
        float* jRow = (float*)(jacobian + rowByteBase);
        int mjByte = i << 5;
        int outByte = i << 2;
        if (i < n) {
            do {
                float* mj = (float*)(massJacobian + mjByte);
                mjByte = mjByte + 0x20;
                *(float*)(outMtx + rowByteBase + outByte) =
                    *(float*)(outMtx + rowByteBase + outByte) +
                    jRow[6] * mj[6] + jRow[5] * mj[5] + jRow[4] * mj[4] +
                    jRow[2] * mj[2] + jRow[0] * mj[0] + jRow[1] * mj[1];
                outByte = outByte + 4;
                remaining = remaining - 1;
            } while (remaining != 0);
        }
        rowByteBase = rowByteBase + 0x20;
    }
}

// ===========================================================================
// 0x8014b5a4  subobj_solver_mirror_matrix
// Mirror the effective-mass matrix's computed lower triangle into the upper
// triangle (the *_mac helpers only fill the lower half). 0x20-byte row stride,
// word columns; copies the 4x4 (up to n=4) symmetric block.
// ===========================================================================
void subobj_solver_mirror_matrix(int mtx)
{
    *(unsigned int*)(mtx + 0x20) = *(unsigned int*)(mtx + 4);
    *(unsigned int*)(mtx + 0x40) = *(unsigned int*)(mtx + 8);
    *(unsigned int*)(mtx + 0x44) = *(unsigned int*)(mtx + 0x28);
    *(unsigned int*)(mtx + 0x60) = *(unsigned int*)(mtx + 0xc);
    *(unsigned int*)(mtx + 100)  = *(unsigned int*)(mtx + 0x2c);
    *(unsigned int*)(mtx + 0x68) = *(unsigned int*)(mtx + 0x4c);
    *(unsigned int*)(mtx + 0x80) = *(unsigned int*)(mtx + 0x10);
    *(unsigned int*)(mtx + 0x84) = *(unsigned int*)(mtx + 0x30);
    *(unsigned int*)(mtx + 0x88) = *(unsigned int*)(mtx + 0x50);
    *(unsigned int*)(mtx + 0x8c) = *(unsigned int*)(mtx + 0x70);
    *(unsigned int*)(mtx + 0xa0) = *(unsigned int*)(mtx + 0x14);
    *(unsigned int*)(mtx + 0xa4) = *(unsigned int*)(mtx + 0x34);
    *(unsigned int*)(mtx + 0xa8) = *(unsigned int*)(mtx + 0x54);
    *(unsigned int*)(mtx + 0xac) = *(unsigned int*)(mtx + 0x74);
    *(unsigned int*)(mtx + 0xb0) = *(unsigned int*)(mtx + 0x94);
}

// ===========================================================================
// 0x8014b620  subobj_solver_vec_jacobian_mac
// Vector * Jacobian multiply-accumulate (overwrite): out[i] = dot6(vec, J[i]).
// Used to project a body velocity/force vector through the Jacobian into
// constraint space (RHS build). Row stride 0x20.
// ===========================================================================
void subobj_solver_vec_jacobian_mac(int outVec, float* vec, int jacobian, int n)
{
    int outByte = 0;
    int rowByte = 0;
    if (n < 1) {
        return;
    }
    do {
        float* jRow = (float*)(jacobian + rowByte);
        rowByte = rowByte + 0x20;
        *(float*)(outVec + outByte) =
            vec[6] * jRow[6] + vec[5] * jRow[5] + vec[4] * jRow[4] +
            vec[2] * jRow[2] + vec[0] * jRow[0] + vec[1] * jRow[1];
        outByte = outByte + 4;
        n = n - 1;
    } while (n != 0);
}

// ===========================================================================
// 0x8014b694  subobj_solver_vec_jacobian_mac_accum
// Same as subobj_solver_vec_jacobian_mac but accumulates (+=) - adds body-B's
// projected velocity onto the constraint-space RHS already holding body-A's.
// ===========================================================================
void subobj_solver_vec_jacobian_mac_accum(int outVec, float* vec, int jacobian, int n)
{
    int outByte = 0;
    int rowByte = 0;
    if (n < 1) {
        return;
    }
    do {
        float* jRow = (float*)(jacobian + rowByte);
        rowByte = rowByte + 0x20;
        *(float*)(outVec + outByte) =
            *(float*)(outVec + outByte) +
            vec[6] * jRow[6] + vec[5] * jRow[5] + vec[4] * jRow[4] +
            vec[2] * jRow[2] + vec[0] * jRow[0] + vec[1] * jRow[1];
        outByte = outByte + 4;
        n = n - 1;
    } while (n != 0);
}

// ===========================================================================
// 0x8014b710  subobj_solver_solve_island
// Core island solve. For each of nRows constraint rows: scale the incoming
// Jacobian by each body's inverse mass and transform the angular part by the
// object->world basis to form M^-1 * J^T (massJacobian). Then build the
// effective-mass matrix A = J * M^-1 * J^T (mirror + diagonal softness),
// assemble the RHS (bias velocity minus current constraint-space velocity),
// run the shared iterative solver, and scatter the resulting impulses back
// onto each body's linear (+0x110) and angular (+0x11c) accumulators.
// dt arrives in f1/param_2; the reciprocal invDt drives the bias term.
// ===========================================================================
void subobj_solver_solve_island(double dt, float* massMtxScratch,
                                unsigned int biasA, unsigned int biasB,
                                int rhs, int loBounds, int hiBounds,
                                int bodyA, int bodyB, int nRows)
{
    float massMatrix[64];
    float rhsVec[8];
    float massJacobianA[64];
    float massJacobianB[48];
    float jacobianScaledA[60];
    float driftBiasA[6];
    float driftBiasB[6];
    float lambda[8];

    (void)biasA;
    (void)biasB;
    (void)massMtxScratch;

    unsigned long long framed = FUN_803826d4();
    int jA = (int)(framed >> 0x20);
    int jB = (int)framed;

    double invDt = (double)(float)((double)FLOAT_80615eb0 / dt);
    double biasScale = (double)(float)(dt * invDt);

    int byteA = 0;
    for (int i = 0; i < nRows; i = i + 1) {
        float* srcRow = (float*)(jA + byteA);
        *(float*)((int)jacobianScaledA + byteA)        = srcRow[0] * *(float*)(bodyA + 0x1a8);
        *(float*)((int)jacobianScaledA + byteA + 4)    = srcRow[1] * *(float*)(bodyA + 0x1a8);
        *(float*)((int)jacobianScaledA + byteA + 8)    = srcRow[2] * *(float*)(bodyA + 0x1a8);
        *(float*)((int)jacobianScaledA + byteA + 0x10) = srcRow[4];
        *(float*)((int)jacobianScaledA + byteA + 0x14) = srcRow[5];
        *(float*)((int)jacobianScaledA + byteA + 0x18) = srcRow[6];
        mtx4_transform_vector_alias((float*)(bodyA + 0x40),
                                    (float*)((int)jacobianScaledA + byteA + 0x10));
        byteA = byteA + 0x20;
    }
    subobj_solver_jacobian_mac((int)massMatrix, (int)jacobianScaledA, jA, nRows);

    if (bodyB != 0) {
        int byteB = 0;
        for (int i = 0; i < nRows; i = i + 1) {
            float* srcRow = (float*)(jB + byteB);
            *(float*)((int)massJacobianB + byteB)        = srcRow[0] * *(float*)(bodyB + 0x1a8);
            *(float*)((int)massJacobianB + byteB + 4)    = srcRow[1] * *(float*)(bodyB + 0x1a8);
            *(float*)((int)massJacobianB + byteB + 8)    = srcRow[2] * *(float*)(bodyB + 0x1a8);
            *(float*)((int)massJacobianB + byteB + 0x10) = srcRow[4];
            *(float*)((int)massJacobianB + byteB + 0x14) = srcRow[5];
            *(float*)((int)massJacobianB + byteB + 0x18) = srcRow[6];
            mtx4_transform_vector_alias((float*)(bodyB + 0x40),
                                        (float*)((int)massJacobianB + byteB + 0x10));
            byteB = byteB + 0x20;
        }
        subobj_solver_jacobian_mac_accum((int)massMatrix, (int)massJacobianB, jB, nRows);
    }
    subobj_solver_mirror_matrix((int)massMatrix);

    float invMassA = *(float*)(bodyA + 0x1a8);
    driftBiasA[0] = *(float*)(bodyA + 0x110) * invMassA +
                    (float)((double)*(float*)(bodyA + 0x14c) * invDt);
    driftBiasA[1] = *(float*)(bodyA + 0x114) * invMassA +
                    (float)((double)*(float*)(bodyA + 0x150) * invDt);
    driftBiasA[2] = *(float*)(bodyA + 0x118) * invMassA +
                    (float)((double)*(float*)(bodyA + 0x154) * invDt);
    driftBiasA[3] = *(float*)(bodyA + 0x11c);
    driftBiasA[4] = *(float*)(bodyA + 0x120);
    driftBiasA[5] = *(float*)(bodyA + 0x124);
    mtx4_transform_vector_alias((float*)(bodyA + 0x40), &driftBiasA[3]);
    driftBiasA[3] = (float)((double)*(float*)(bodyA + 0x134) * invDt + (double)driftBiasA[3]);
    driftBiasA[4] = (float)((double)*(float*)(bodyA + 0x138) * invDt + (double)driftBiasA[4]);
    driftBiasA[5] = (float)((double)*(float*)(bodyA + 0x13c) * invDt + (double)driftBiasA[5]);
    subobj_solver_vec_jacobian_mac((int)rhsVec, driftBiasA, jA, nRows);

    if (bodyB != 0) {
        float invMassB = *(float*)(bodyB + 0x1a8);
        driftBiasB[0] = *(float*)(bodyB + 0x110) * invMassB +
                        (float)((double)*(float*)(bodyB + 0x14c) * invDt);
        driftBiasB[1] = *(float*)(bodyB + 0x114) * invMassB +
                        (float)((double)*(float*)(bodyB + 0x150) * invDt);
        driftBiasB[2] = *(float*)(bodyB + 0x118) * invMassB +
                        (float)((double)*(float*)(bodyB + 0x154) * invDt);
        driftBiasB[3] = *(float*)(bodyB + 0x11c);
        driftBiasB[4] = *(float*)(bodyB + 0x120);
        driftBiasB[5] = *(float*)(bodyB + 0x124);
        mtx4_transform_vector_alias((float*)(bodyB + 0x40), &driftBiasB[3]);
        driftBiasB[3] = (float)((double)*(float*)(bodyB + 0x134) * invDt + (double)driftBiasB[3]);
        driftBiasB[4] = (float)((double)*(float*)(bodyB + 0x138) * invDt + (double)driftBiasB[4]);
        driftBiasB[5] = (float)((double)*(float*)(bodyB + 0x13c) * invDt + (double)driftBiasB[5]);
        subobj_solver_vec_jacobian_mac_accum((int)rhsVec, driftBiasB, jB, nRows);
    }

    {
        int rowByte = 0;
        int diagByte = 0;
        int rowsLeft = nRows;
        do {
            int diagIdx = diagByte + rowByte;
            float cur = *(float*)((int)rhsVec + rowByte);
            diagByte = diagByte + 0x20;
            *(float*)((int)massMatrix + diagIdx) =
                (float)((double)*(float*)((int)massMatrix + diagIdx) + biasScale);
            *(float*)((int)rhsVec + rowByte) =
                -(float)(invDt * (double)*(float*)(rhs + rowByte) - (double)cur);
            rowByte = rowByte + 4;
            rowsLeft = rowsLeft - 1;
        } while (rowsLeft != 0);
    }

    solver_iterative_constrained((u32)(unsigned int)(unsigned long long)massMatrix,
                                 (u32)(unsigned int)(unsigned long long)lambda,
                                 rhsVec, hiBounds, loBounds, nRows, nRows << 2);

    if (bodyB == 0) {
        int jByte = 0;
        int lByte = 0;
        for (int rowsLeft = nRows; rowsLeft > 0; rowsLeft = rowsLeft - 1) {
            float* jRow = (float*)(jA + jByte);
            float li = *(float*)((int)lambda + lByte);
            jByte = jByte + 0x20;
            lByte = lByte + 4;
            *(float*)(bodyA + 0x110) = li * jRow[0] + *(float*)(bodyA + 0x110);
            *(float*)(bodyA + 0x114) = li * jRow[1] + *(float*)(bodyA + 0x114);
            *(float*)(bodyA + 0x118) = li * jRow[2] + *(float*)(bodyA + 0x118);
            *(float*)(bodyA + 0x11c) = li * jRow[4] + *(float*)(bodyA + 0x11c);
            *(float*)(bodyA + 0x120) = li * jRow[5] + *(float*)(bodyA + 0x120);
            *(float*)(bodyA + 0x124) = li * jRow[6] + *(float*)(bodyA + 0x124);
        }
    } else {
        int jByte = 0;
        int lByte = 0;
        for (int rowsLeft = nRows; rowsLeft > 0; rowsLeft = rowsLeft - 1) {
            float* jRowA = (float*)(jA + jByte);
            float li = *(float*)((int)lambda + lByte);
            float* jRowB = (float*)(jB + jByte);
            jByte = jByte + 0x20;
            lByte = lByte + 4;
            *(float*)(bodyA + 0x110) = li * jRowA[0] + *(float*)(bodyA + 0x110);
            *(float*)(bodyA + 0x114) = li * jRowA[1] + *(float*)(bodyA + 0x114);
            *(float*)(bodyA + 0x118) = li * jRowA[2] + *(float*)(bodyA + 0x118);
            *(float*)(bodyA + 0x11c) = li * jRowA[4] + *(float*)(bodyA + 0x11c);
            *(float*)(bodyA + 0x120) = li * jRowA[5] + *(float*)(bodyA + 0x120);
            *(float*)(bodyA + 0x124) = li * jRowA[6] + *(float*)(bodyA + 0x124);
            *(float*)(bodyB + 0x110) = li * jRowB[0] + *(float*)(bodyB + 0x110);
            *(float*)(bodyB + 0x114) = li * jRowB[1] + *(float*)(bodyB + 0x114);
            *(float*)(bodyB + 0x118) = li * jRowB[2] + *(float*)(bodyB + 0x118);
            *(float*)(bodyB + 0x11c) = li * jRowB[4] + *(float*)(bodyB + 0x11c);
            *(float*)(bodyB + 0x120) = li * jRowB[5] + *(float*)(bodyB + 0x120);
            *(float*)(bodyB + 0x124) = li * jRowB[6] + *(float*)(bodyB + 0x124);
        }
    }

    (void)massJacobianA;
    FUN_80382720();
}

// ===========================================================================
// 0x8014bc04  subobj_solver_frame_dispatch
// Per-frame entry for one constraint island (DAT_806121b4 + idx*0x80). Resolve
// body-A (and optional body-B) descriptors, build three limit axes from the
// island's stored basis (transformed into world), compute a drift term scaled
// by the frame counter, optionally apply a rotational restitution kick per
// body (subobj_solver_apply_axis_impulse), then run the island solve.
// ===========================================================================
void subobj_solver_frame_dispatch(void)
{
    float axisA[4];
    float axisB[4];
    float basisA[8];
    float basisB[8];
    float jacobianRowsA[8];
    float massJacobianRows[8];
    float rhsScratch[8];
    float kickPoint[4];
    float kickAxis[4];
    // limit-axis selectors (unit basis columns) for body A
    float selA0x, selA0y, selA0z;
    float selA1x, selA1y, selA1z;
    float selA2x, selA2y, selA2z;

    int islandIdx = (int)FUN_803826f0();
    unsigned int island = (unsigned int)(DAT_806121b4 + islandIdx * 0x80);

    float* bodyA = (float*)(DAT_8061218c + (*(unsigned int*)(island + 0x70) & 0xffff) * 0x200);
    float* bodyB;
    if (*(unsigned int*)(island + 0x74) == 0) {
        bodyB = (float*)0;
    } else {
        bodyB = (float*)(DAT_8061218c + (*(unsigned int*)(island + 0x74) & 0xffff) * 0x200);
    }

    selA0y = FLOAT_80615ea8; selA0x = FLOAT_80615eb0; selA0z = FLOAT_80615ea8;
    selA1y = FLOAT_80615ea8; selA1x = FLOAT_80615eb0; selA1z = FLOAT_80615ea8;
    selA2y = FLOAT_80615ea8; selA2x = FLOAT_80615ea8; selA2z = FLOAT_80615eb0;

    mtx4_transform_vector_copy((float*)bodyA, island, basisA);
    basisA[6] = basisA[0] * selA0y - basisA[1] * selA0x; // cross-column 0
    basisA[7] = basisA[1] * selA0z - basisA[2] * selA0y;
    (void)basisA[7];
    // build the three world-space limit axes for A (r x sel)
    axisA[0] = basisA[1] * selA1z - basisA[2] * selA1y;
    axisA[1] = basisA[2] * selA1x - basisA[0] * selA1z;
    axisA[2] = basisA[0] * selA1y - basisA[1] * selA1x;
    (void)axisA;

    if (bodyB != (float*)0) {
        float selB0x = FLOAT_80615ea8, selB0y = FLOAT_80615f48, selB0z = FLOAT_80615ea8;
        (void)selB0x; (void)selB0y; (void)selB0z;
        mtx4_transform_vector_copy((float*)bodyB, island + 0x10, basisB);
        axisB[0] = basisB[1] * FLOAT_80615ea8 - basisB[2] * FLOAT_80615f48;
        axisB[1] = basisB[2] * FLOAT_80615ea8 - basisB[0] * FLOAT_80615ea8;
        axisB[2] = basisB[0] * FLOAT_80615f48 - basisB[1] * FLOAT_80615ea8;
        (void)axisB;
    }

    float drift = *(float*)(island + 0x5c) *
        (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80615ed8);

    if (bodyB == (float*)0) {
        int col = 0;
        int rowsLeft = 3;
        do {
            float target = *(float*)(island + col + 0x10);
            float comA    = *(float*)((int)bodyA + col + 0x140);
            *(float*)((int)jacobianRowsA + col) = FLOAT_80615eb8;
            *(float*)((int)massJacobianRows + col) = FLOAT_80615eb4;
            *(float*)((int)rhsScratch + col) =
                drift * ((target - *(float*)((int)basisA + col)) - comA);
            col = col + 4;
            rowsLeft = rowsLeft - 1;
        } while (rowsLeft != 0);
    } else {
        int col = 0;
        int rowsLeft = 3;
        do {
            float comB = *(float*)((int)bodyB + col + 0x140);
            float comA = *(float*)((int)bodyA + col + 0x140);
            *(float*)((int)jacobianRowsA + col) = FLOAT_80615eb8;
            *(float*)((int)massJacobianRows + col) = FLOAT_80615eb4;
            *(float*)((int)rhsScratch + col) =
                drift * (((comB + *(float*)((int)basisB + col)) -
                          *(float*)((int)basisA + col)) - comA);
            col = col + 4;
            rowsLeft = rowsLeft - 1;
        } while (rowsLeft != 0);
    }

    if (FLOAT_80615ea8 < *(float*)(island + 0x68)) {
        if (*(int*)(island + 0x74) != 0) {
            kickPoint[0] = basisB[0] + bodyB[0x50];
            kickPoint[1] = basisB[1] + bodyB[0x51];
            kickPoint[2] = basisB[2] + bodyB[0x52];
            float ax = bodyB[0x4d];
            float ay = bodyB[0x4e];
            float az = bodyB[0x4f];
            float len = az * az + ax * ax + ay * ay;
            if (FLOAT_80615f4c < len) {
                float rInv = 1.0f / SQRT(len);
                rInv = rInv * -(FLOAT_80615ebc * len * rInv * rInv - FLOAT_80615ec8);
                float scale = FLOAT_80615eb0 *
                              rInv * -(FLOAT_80615ebc * len * rInv * rInv - FLOAT_80615ec8);
                kickAxis[0] = ax * scale;
                kickAxis[1] = ay * scale;
                kickAxis[2] = az * scale;
                subobj_solver_apply_axis_impulse(*(unsigned int*)(island + 0x74),
                                                 kickPoint, kickAxis);
            }
        }
        kickPoint[0] = basisA[0] + bodyA[0x50];
        kickPoint[1] = basisA[1] + bodyA[0x51];
        kickPoint[2] = basisA[2] + bodyA[0x52];
        float ax = bodyA[0x4d];
        float ay = bodyA[0x4e];
        float az = bodyA[0x4f];
        float len = az * az + ax * ax + ay * ay;
        if (FLOAT_80615f4c < len) {
            float rInv = 1.0f / SQRT(len);
            rInv = rInv * -(FLOAT_80615ebc * len * rInv * rInv - FLOAT_80615ec8);
            float scale = FLOAT_80615eb0 *
                          rInv * -(FLOAT_80615ebc * len * rInv * rInv - FLOAT_80615ec8);
            kickAxis[0] = ax * scale;
            kickAxis[1] = ay * scale;
            kickAxis[2] = az * scale;
            subobj_solver_apply_axis_impulse(*(unsigned int*)(island + 0x70),
                                             kickPoint, kickAxis);
        }
    }

    subobj_solver_solve_island((double)*(float*)(island + 0x60),
                               massJacobianRows, 0, 0,
                               (int)rhsScratch, (int)jacobianRowsA, (int)massJacobianRows,
                               (int)bodyA, (int)bodyB, 3);
    FUN_8038273c();
}

// ===========================================================================
// 0x8014c130  subobj_solver_apply_axis_impulse
// Apply a rotational + linear restitution impulse to one body along a supplied
// world axis about a world point. Computes the torque arm (point - COM) x axis,
// normalizes, scales by the frame-counter drift, and accumulates into the
// body's torque (+0x110 group) and its inverse-inertia-projected linear part
// (+0x11c group).
// ===========================================================================
void subobj_solver_apply_axis_impulse(unsigned int islandHandle, float* pointWorld, float* axis)
{
    float outAxis[4];
    unsigned int frameXor;

    unsigned long long framed = FUN_803826f0();
    unsigned int slot = (unsigned int)(framed >> 0x20);
    float* pt = (float*)(unsigned long long)framed;
    frameXor = DAT_806109c0 ^ 0x80000000;
    int desc = DAT_8061218c + (slot & 0xffff) * 0x200;
    double drift = (double)(float)((double)FLOAT_80615eb0 *
        (double)(float)((double)CONCAT44(0x43300000, frameXor) - DOUBLE_80615ed8));

    (void)islandHandle;
    (void)pointWorld;
    subobj_project_point_torque_axis(slot, pt, axis, outAxis);

    float dotAxis = outAxis[2] * axis[2] + outAxis[0] * axis[0] + outAxis[1] * axis[1];
    if (FLOAT_80615f4c < ABS(dotAxis)) {
        float rz = pt[2] - *(float*)(desc + 0x148);
        float rx = pt[0] - *(float*)(desc + 0x140);
        float ry = pt[1] - *(float*)(desc + 0x144);
        float cx = outAxis[2] * rx - outAxis[0] * rz;
        float cy = outAxis[0] * ry - outAxis[1] * rx;
        float cz = outAxis[1] * rz - outAxis[2] * ry;
        float len = cy * cy + cz * cz + cx * cx;
        if (FLOAT_80615f4c < len) {
            float rInv = 1.0f / SQRT(len);
            rInv = rInv * -(FLOAT_80615ebc * len * rInv * rInv - FLOAT_80615ec8);
            float mag = (float)(drift * (double)(dotAxis * FLOAT_80615eb0 *
                        rInv * -(FLOAT_80615ebc * len * rInv * rInv - FLOAT_80615ec8)));
            *(float*)(desc + 0x110) = *(float*)(desc + 0x110) + cz * mag;
            *(float*)(desc + 0x114) = *(float*)(desc + 0x114) + cx * mag;
            *(float*)(desc + 0x118) = *(float*)(desc + 0x118) + cy * mag;
        }
    }

    float linMag = (float)(drift * -(double)(axis[2] * *(float*)(desc + 0x13c) +
                                             axis[0] * *(float*)(desc + 0x134) +
                                             axis[1] * *(float*)(desc + 0x138)));
    *(float*)(desc + 0x11c) = axis[0] * linMag + *(float*)(desc + 0x11c);
    *(float*)(desc + 0x120) = axis[1] * linMag + *(float*)(desc + 0x120);
    *(float*)(desc + 0x124) = axis[2] * linMag + *(float*)(desc + 0x124);
    FUN_8038273c();
}
