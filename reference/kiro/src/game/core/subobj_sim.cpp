// Address range: 0x8014c2fc - 0x8014e6e4 | batch_419 | 2026-09-05
//
// Entity render sub-object PHYSICS SIMULATION STEP - the outer per-frame driver
// and its passes (companion to subobj_solver_iterate.cpp / subobj_collision.cpp
// / subobj_solver.cpp / subobj_dynamics.cpp / subobj_break.cpp).
//
// Each frame subobj_physics_step (0x8014e6e4, called from
// entity_post_transform_pass) runs a fixed-timestep loop. Per sub-step it:
//   1. subobj_collision_broadphase_pass (0x8014d4b8) - reset the constraint
//      pool and walk the active/dirty descriptor list, running collision
//      queries (and swept queries for fast movers) to (re)generate contacts,
//   2. subobj_area_effect_broadphase (batch_413) - area-effect scan,
//   3. subobj_constraint_resolve_events (0x8014d67c) - prune/merge the
//      constraint list and fire per-contact game events (damage, decals, AI
//      capsule notifications, break handlers),
//   4. subobj_integrate_pass (0x8014de4c) - projected Gauss-Seidel: shuffle the
//      constraint order, solve each row (subobj_solver_solve_row), integrate,
//      apply angular damping / renormalize,
//   5. subobj_save_impulse_state (0x8014dd64) - snapshot force/torque and add
//      gravity,
// then subobj_finalize_bodies (0x8014e1fc) applies impact damage, reassigns
// rooms/portals, rebuilds bounds and manages sleeping/dirty flags.
//
// The angular/twist limit path uses subobj_solver_twist_angle (0x8014c2fc) and
// subobj_solver_relative_twist (0x8014c3ec) to measure a joint's current twist,
// and subobj_solver_angular_frame_dispatch (0x8014c49c) to build+solve an
// angular limit island.
//
// Descriptor float-word indices used below (word = byte/4):
//   [9]/[10]   constraint record owner handles A/B (record[9], record[10])
//   [0x44..0x49] per-body velocity snapshot (linear+angular)
//   [0x4d..0x4f] angular velocity (x,y,z)
//   [0x50..0x52] world center offset
//   [0x53..0x58] contact/impact vectors
//   [0x6a..0x6d] previous-position / half-extent cache
//   [0x70]      flag word
//   [0x79]      descriptor handle (as float bit-pattern -> uint)
//   [0x7f]      next-in-active-list pointer

#include "common.h"
#include "generated_decls.h"

extern "C" {
// frame push/pop helpers - each returns a packed {ptr, handle<<32} in a u64.
extern unsigned long long FUN_803826f0(void);   extern void FUN_8038273c(void);
extern unsigned long long FUN_803826e8(void);    extern void FUN_80382734(void);
extern int                FUN_803826e4(void);     extern void FUN_80382730(void);
extern unsigned long long FUN_803826d0(void);     extern void FUN_8038271c(void);

// post-solve owner hooks for special descriptor types
// subobj_rotor_collision_broadphase (0x801570d0, batch_428) resolves via generated_decls
extern void FUN_80243d4c(void);   // type 0x10000000 post-collision hook

// material / bank lookup: returns a table pointer for a material index.
extern int  FUN_80252384(unsigned int material);   // -> table (+0x0c dt, +0x08 damp)

// break / event notification callbacks fired from the resolve pass
extern void FUN_8014ff80(float ownerHandle, unsigned int argBlock);   // break notify
extern void FUN_80284ce0(double relVel, double localVel, double rest,
                         double a, double b, double c, double d, double e,
                         float ownerB, float ownerA, float* impulseVec, float* record); // dmg dispatch
extern void FUN_8025a120(double relVel, double localVel, double rest,
                         double a, double b, double c, double d, double e,
                         float ownerB, float ownerA, float* impulseVec, float* record); // player dmg
extern void FUN_8014ee3c(void);   // per-frame island dispatch (calls subobj_solver_frame_dispatch)
extern void FUN_8014ef54(void);   // post-resolve fixup pass

// finalize-pass helpers
extern unsigned long long FUN_80268018(void);   // impact-damage arg build
extern void               FUN_80253544(void);   // entity contact bookkeeping
extern void               FUN_803654f0(void);   // type 0x20000000 destroy variant

// physics-step top helpers
extern void FUN_8014ff00(unsigned long long framed, unsigned long long p2, unsigned long long p3,
                         unsigned long long p4, double p5, double p6, double p7, double p8,
                         int mode, unsigned int r4, unsigned int p11, unsigned int p12,
                         unsigned int p13, unsigned int p14, unsigned int p15, unsigned int p16);
extern unsigned long long FUN_8014fff8(void);   // finalize-pass arg build

extern void __save_gpr_28(void);
extern void __restore_gpr_28(void);
}

// pool / list tables
extern int DAT_8061218c;   // descriptor pool base (stride 0x200)
extern int DAT_80612184;   // per-slot owner-entity pointer table (stride 4)
extern int DAT_80612180;   // per-slot aux table (stride 0x20)
extern int DAT_806121b4;   // constraint-island pool base (stride 0x80)
extern int DAT_80612168;   // active/dirty descriptor list head
extern int DAT_80611928;   // current frame stamp
extern int DAT_806109c0;   // frame / phase counter (signed via xor 0x80000000)
extern int DAT_806108e4;   // physics sub-step count (0 = disabled)
extern int DAT_80612164;   // "in physics step" reentrancy flag
extern int DAT_806121a8;   // pending-constraint flag
extern int DAT_806121b8;   // window-average ring cursor (mod 0xf)
extern int DAT_80612610;   // neighbor-scan start index
extern int DAT_80612098;   // neighbor-scan end index
extern char DAT_805015bd;  // active scene mode byte ('\n' == in-game)

// externally-populated scene node table (walked in the resolve pass)
extern int DAT_80588610;   // node pointer array base
extern float DAT_805893a0; // window-average ring buffer base

// conversion / constant globals
extern double DOUBLE_80615ed8; // 2^52 magic-double bias
extern float  FLOAT_806108ec;  // wind period divisor / dt scale

// shared float constants
extern float FLOAT_80615ea8;   // 0.0f
extern float FLOAT_80615eb0;   // 1.0f
extern float FLOAT_80615eb4;   // lambda-hi default
extern float FLOAT_80615eb8;   // lambda-lo (negative clamp)
extern float FLOAT_80615ebc;   // 0.5f (NR)
extern float FLOAT_80615ec4;   // impulse epsilon
extern float FLOAT_80615ec8;   // 3.0f (NR)
extern float FLOAT_80615ed0;   //  pi   (angle wrap upper)
extern float FLOAT_80615eec;   // age-fraction threshold
extern float FLOAT_80615ef8;   // penetration bias / bounds epsilon
extern float FLOAT_80615f14;   // impact speed threshold
extern float FLOAT_80615f28;   // twist->angle scale
extern float FLOAT_80615f34;   // separation-speed threshold
extern float FLOAT_80615f48;   // -1.0f
extern float FLOAT_80615f4c;   // length^2 epsilon
extern float FLOAT_80615f50;   // 2*pi (angle wrap step)
extern float FLOAT_80615f54;   // -pi (angle wrap lower)
extern float FLOAT_80615f58;   // rel-vel radius clamp
extern float FLOAT_80615f5c;   // node-AABB pad
extern float FLOAT_80615f60;   // type-3 friction seed
extern float FLOAT_80615f64;   // type-3 no-solve seed
extern float FLOAT_80615f68;   // gravity per-step
extern float FLOAT_80615f6c;   // angular-vel length^2 epsilon
extern float FLOAT_80615f70;   // angular-vel clamp magnitude
extern float FLOAT_80615f74;   // bounds-change epsilon (linear)
extern float FLOAT_80615f78;   // bounds-change epsilon (offset)
extern float FLOAT_80615f7c;   // wind damping scale

// math_sqrt/math_atan2/scene_query_nodes_in_frustum/mem_page_alloc/mem_page_free
// resolve through generated_decls with correct signatures. The helpers below are
// declared void() in the shared headers but return a value in the original
// (register EAX); recover the value through typed function-pointer aliases so we
// don't fight the shared declaration (rule 6 / the rng_counter_tick pattern).
typedef int          (*assign_room_fn)(int);
typedef int          (*ray_portals_fn)(double, double, float*, int, float*, int);
typedef int          (*ai_capsule_fn)(void);
typedef int          (*frustum_test_fn)(float*);
typedef int          (*neighbor_fn)(int, int, int);
typedef unsigned int (*rng_fn)(void);
typedef int          (*advance_sec_fn)(int);
#define ENTITY_ASSIGN_ROOM     ((assign_room_fn)&entity_assign_room)
#define SCENE_RAY_PORTALS      ((ray_portals_fn)&scene_ray_traverse_portals)
#define AI_CAPSULE_TEST        ((ai_capsule_fn)&player_ai_closest_capsule_test)
#define FRUSTUM_VS_WORLD       ((frustum_test_fn)&scene_test_frustum_vs_world)
#define NEIGHBOR_NODES         ((neighbor_fn)&scene_get_neighbor_nodes_temp)
#define RNG_TICK               ((rng_fn)&rng_counter_tick)
#define MEM_ADVANCE_SECONDARY  ((advance_sec_fn)&mem_advance_secondary)

// ===========================================================================
// 0x8014c2fc  subobj_solver_twist_angle
// Extract the signed swing/twist angle of a rotation delta quaternion: angle =
// atan2(|xyz|, w), sign-flipped if the axis opposes the reference direction,
// scaled and wrapped to (-pi, pi]. Returns the negated angle (solver sign).
// ===========================================================================
double subobj_solver_twist_angle(float* deltaQuat, float* refDir)
{
    double w = (double)deltaQuat[3];
    double sinLen = math_sqrt((double)(deltaQuat[2] * deltaQuat[2] +
                                       deltaQuat[0] * deltaQuat[0] +
                                       deltaQuat[1] * deltaQuat[1]));
    double angle;
    if (deltaQuat[2] * refDir[2] + deltaQuat[0] * refDir[0] + deltaQuat[1] * refDir[1] <
        FLOAT_80615ea8) {
        angle = (double)math_atan2(sinLen, -w);
    } else {
        angle = (double)math_atan2(sinLen, w);
    }
    angle = (double)((float)angle * FLOAT_80615f28);
    if ((double)FLOAT_80615ed0 < angle) {
        angle = (double)(float)(angle - (double)FLOAT_80615f50);
    }
    if (angle < (double)FLOAT_80615f54) {
        angle = (double)(float)(angle + (double)FLOAT_80615f50);
    }
    return -angle;
}

// ===========================================================================
// 0x8014c3ec  subobj_solver_relative_twist
// Compute the twist of body A's orientation relative to body B (or to the world
// when B is null): q_delta = conj(qA) * (qB) * conj(refQuat), then measure its
// twist angle about refDir. Frame-scoped (FUN_803826f0).
// ===========================================================================
double subobj_solver_relative_twist(float* bodyA, float* bodyB, float* refDir, float* refQuat)
{
    float conjA[4];
    float conjRef[4];
    float tmpA[4];
    float mulAB[4];
    float conjB[4];
    float deltaQuat[10];
    double result;

    unsigned long long framed = FUN_803826f0();
    int qA = (int)(framed >> 0x20);
    (void)bodyA;
    (void)bodyB;
    if ((int)framed == 0) {
        quat_conjugate(conjA, (float*)(qA + 0x100));
        quat_conjugate(conjB, refQuat);
        quat_multiply(deltaQuat, conjA, conjB);
    } else {
        quat_conjugate(tmpA, (float*)(qA + 0x100));
        quat_multiply(mulAB, tmpA, (float*)((int)framed + 0x100));
        quat_conjugate(conjRef, refQuat);
        quat_multiply(deltaQuat, mulAB, conjRef);
    }
    result = subobj_solver_twist_angle(deltaQuat, refDir);
    FUN_8038273c();
    return result;
}

// forward-declares (defined in subobj_solver_iterate.cpp / this file)
void subobj_solver_solve_island(double dt, float* massMtxScratch,
                                unsigned int biasA, unsigned int biasB,
                                int rhs, int loBounds, int hiBounds,
                                int bodyA, int bodyB, int nRows);

// ===========================================================================
// 0x8014c49c  subobj_solver_angular_frame_dispatch
// Build and solve an angular/twist-limit constraint island. Resolves body A/B,
// builds three world limit axes (like subobj_solver_frame_dispatch) plus a
// twist row derived from subobj_solver_relative_twist against the joint's stored
// low/high limit (words +0x40/+0x44). When the twist is inside the limit an
// optional joint callback (+0x7c) fires; otherwise a corrective bias row is
// added. Falls through to subobj_solver_solve_island.
// ===========================================================================
void subobj_solver_angular_frame_dispatch(void)
{
    float basisA[4];
    float basisB[4];
    float jacScratchA[8];
    float lambdaLo[8];
    float lambdaHi[8];
    float twistAxis[4];
    float refCross[4];
    float rowLo[8];
    float rowHi[8];
    float rowBias[8];
    float axisWorkA[16];
    float axisWorkB[16];

    int islandIdx = FUN_803826e4();
    unsigned int island = (unsigned int)(DAT_806121b4 + islandIdx * 0x80);
    int nRows = 3;

    float* bodyA = (float*)(DAT_8061218c + (*(unsigned int*)(island + 0x70) & 0xffff) * 0x200);
    float* bodyB;
    if (*(unsigned int*)(island + 0x74) == 0) {
        bodyB = (float*)0;
    } else {
        bodyB = (float*)(DAT_8061218c + (*(unsigned int*)(island + 0x74) & 0xffff) * 0x200);
    }

    // limit-axis selector columns (unit basis)
    float selX = FLOAT_80615eb0, selY = FLOAT_80615ea8, selZ = FLOAT_80615ea8;
    (void)selX; (void)selY; (void)selZ;

    mtx4_transform_vector_copy(bodyA, island, jacScratchA);
    (void)basisA; (void)basisB; (void)axisWorkA; (void)axisWorkB;
    (void)refCross;

    if (bodyB != (float*)0) {
        mtx4_transform_vector_copy(bodyB, island + 0x10, basisB);
    }

    double drift = (double)(*(float*)(island + 0x5c) *
        (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80615ed8));

    // linear positional rows (3)
    if (bodyB == (float*)0) {
        int col = 0;
        for (int i = 3; i != 0; i = i - 1) {
            float target = *(float*)(island + col + 0x10);
            float comA   = *(float*)((int)bodyA + col + 0x140);
            *(float*)((int)rowLo + col)  = FLOAT_80615eb8;
            *(float*)((int)lambdaHi + col) = FLOAT_80615eb4;
            *(float*)((int)rowBias + col) =
                (float)(drift * (double)((target - *(float*)((int)jacScratchA + col)) - comA));
            col = col + 4;
        }
    } else {
        int col = 0;
        for (int i = 3; i != 0; i = i - 1) {
            float comB = *(float*)((int)bodyB + col + 0x140);
            float comA = *(float*)((int)bodyA + col + 0x140);
            *(float*)((int)rowLo + col)  = FLOAT_80615eb8;
            *(float*)((int)lambdaHi + col) = FLOAT_80615eb4;
            *(float*)((int)rowBias + col) =
                (float)(drift * (double)(((comB + *(float*)((int)basisB + col)) -
                                          *(float*)((int)jacScratchA + col)) - comA));
            col = col + 4;
        }
    }

    // twist row: axis = transform of joint axis (word +0x20), reference basis
    mtx4_transform_vector_copy(bodyA, island + 0x20, twistAxis);
    vec3_build_basis_from_dir(&refCross[0], &rowHi[0], twistAxis, 1);

    int solvedType = 0;   // 0 = free / callback, 1 = low-limit hit, 2 = high-limit hit
    double restitution = (double)*(float*)(island + 0x68);

    if ((FLOAT_80615eb8 < *(float*)(island + 0x40)) &&
        (*(float*)(island + 0x44) < FLOAT_80615eb4) &&
        (*(float*)(island + 0x40) < *(float*)(island + 0x44))) {
        double twist = subobj_solver_relative_twist(bodyA, bodyB,
                                                     (float*)(island + 0x20),
                                                     (float*)(island + 0x48));
        double excess = (double)FLOAT_80615ea8;
        if ((double)*(float*)(island + 0x40) <= twist) {
            if ((double)*(float*)(island + 0x44) < twist) {
                solvedType = 2;
                excess = (double)(float)(twist - (double)*(float*)(island + 0x44));
            }
        } else {
            excess = (double)(float)(twist - (double)*(float*)(island + 0x40));
            solvedType = 1;
        }
        // spring word +0x64 must agree in sign with the excess
        if ((FLOAT_80615ea8 != *(float*)(island + 100)) &&
            ((float)(((unsigned int)*(float*)(island + 100) & 0x80000000) | 0x3f800000) !=
             (float)(((unsigned int)(float)excess & 0x80000000) | 0x3f800000))) {
            solvedType = 0;
        }

        if (solvedType != 0) {
            // limit hit: corrective bias row
            float biasVel = (float)(-drift * excess);
            (void)biasVel;
            if (*(float*)(island + 0x40) == *(float*)(island + 0x44)) {
                rowLo[6]  = FLOAT_80615eb8; rowHi[6] = FLOAT_80615eb4;
            } else if (solvedType == 1) {
                rowLo[6]  = FLOAT_80615ea8; rowHi[6] = FLOAT_80615eb4;
            } else {
                rowLo[6]  = FLOAT_80615eb8; rowHi[6] = FLOAT_80615ea8;
            }
            nRows = 6;
        } else if (FLOAT_80615ea8 != *(float*)(island + 100)) {
            // free within a spring joint: relaxed limit row scaled by frame counter
            rowLo[6] = FLOAT_80615eb8;
            rowHi[6] = FLOAT_80615eb4;
            nRows = 6;
        }

        // joint callback on limit hit
        if (solvedType != 0 && *(int*)(island + 0x7c) != 0) {
            typedef void (*joint_cb)(int, unsigned int, unsigned int);
            ((joint_cb)*(void**)(island + 0x7c))(islandIdx,
                *(unsigned int*)(island + 0x70), *(unsigned int*)(island + 0x74));
        }
    }

    if ((solvedType == 0) && ((double)FLOAT_80615ea8 < restitution)) {
        float kickPoint[4];
        if (*(int*)(island + 0x74) != 0) {
            kickPoint[0] = basisB[0] + bodyB[0x50];
            kickPoint[1] = basisB[1] + bodyB[0x51];
            kickPoint[2] = basisB[2] + bodyB[0x52];
            subobj_solver_apply_axis_impulse(*(unsigned int*)(island + 0x74), kickPoint, twistAxis);
        }
        kickPoint[0] = jacScratchA[0] + bodyA[0x50];
        kickPoint[1] = jacScratchA[1] + bodyA[0x51];
        kickPoint[2] = jacScratchA[2] + bodyA[0x52];
        subobj_solver_apply_axis_impulse(*(unsigned int*)(island + 0x70), kickPoint, twistAxis);
    }

    subobj_solver_solve_island(0.0, (double)*(float*)(island + 0x60) != 0.0 ? (float*)lambdaHi : (float*)lambdaHi,
                               0, 0, (int)rowBias, (int)rowLo, (int)lambdaHi,
                               (int)bodyA, (int)bodyB, nRows);
    FUN_80382730();
}

// ===========================================================================
// 0x8014ce18  subobj_solver_solve_row
// One projected Gauss-Seidel iteration for a single constraint record: build
// the row (subobj_solver_build_constraint_row), form the effective mass by
// projecting each body's inverse-mass-scaled Jacobian, compute the constraint
// impulse lambda = (biasVel/dt - Jv) / effMass, clamp to [lo,hi], apply to both
// bodies' accumulators, and record the deepest contact into the aux table. dt
// arrives in f1; param_2 is the accumulated solve time used for contact stamps.
// ===========================================================================
void subobj_solver_solve_row(double dtInvArg, double solveTime)
{
    float jacA[8];
    float jacB[8];
    float targetVel;
    unsigned int loBound;
    unsigned int hiBound;
    float mjA[8];
    float mjB[8];
    float biasA[8];
    float biasB[8];

    (void)dtInvArg;
    float* record = (float*)(unsigned long long)(unsigned int)FUN_803826e4();
    double invDt = (double)(float)((double)FLOAT_80615eb0 / dtInvArg);
    unsigned int handleA = *(unsigned int*)((int)record[9] + 0x80);
    unsigned int handleB = *(unsigned int*)((int)record[10] + 0x80);
    int descA = DAT_8061218c + (handleA & 0xffff) * 0x200;
    int descB = 0;

    subobj_solver_build_constraint_row(jacA, jacB, &targetVel, &loBound, &hiBound, record);

    float invMassA = *(float*)(descA + 0x1a8);
    mjA[0] = jacA[0] * invMassA;
    mjA[1] = jacA[1] * invMassA;
    mjA[2] = jacA[2] * invMassA;
    mjA[4] = jacA[4];
    mjA[5] = jacA[5];
    mjA[6] = jacA[6];
    mtx4_transform_vector_alias((float*)(descA + 0x40), &mjA[4]);

    if (*(short*)((int)record + 0x2e) == 1) {
        descB = DAT_8061218c + (handleB & 0xffff) * 0x200;
        float invMassB = *(float*)(descB + 0x1a8);
        mjB[0] = jacB[0] * invMassB;
        mjB[1] = jacB[1] * invMassB;
        mjB[2] = jacB[2] * invMassB;
        mjB[4] = jacB[4];
        mjB[5] = jacB[5];
        mjB[6] = jacB[6];
        mtx4_transform_vector_alias((float*)(descB + 0x40), &mjB[4]);
    }

    double effMass = (double)(mjA[6] * jacA[6] + mjA[5] * jacA[5] + mjA[4] * jacA[4] +
                              mjA[2] * jacA[2] + mjA[0] * jacA[0] + mjA[1] * jacA[1]);
    if (*(short*)((int)record + 0x2e) == 1) {
        effMass = (double)(float)(effMass + (double)(mjB[6] * jacB[6] + mjB[5] * jacB[5] +
                                                     mjB[4] * jacB[4] + mjB[2] * jacB[2] +
                                                     mjB[0] * jacB[0] + mjB[1] * jacB[1]));
    }

    // current relative velocity in constraint space (Jv) for body A
    float im = *(float*)(descA + 0x1a8);
    biasA[0] = *(float*)(descA + 0x110) * im + (float)((double)*(float*)(descA + 0x14c) * invDt);
    biasA[1] = *(float*)(descA + 0x114) * im + (float)((double)*(float*)(descA + 0x150) * invDt);
    biasA[2] = *(float*)(descA + 0x118) * im + (float)((double)*(float*)(descA + 0x154) * invDt);
    biasA[4] = *(float*)(descA + 0x11c);
    biasA[5] = *(float*)(descA + 0x120);
    biasA[6] = *(float*)(descA + 0x124);
    mtx4_transform_vector_alias((float*)(descA + 0x40), &biasA[4]);
    biasA[4] = (float)((double)*(float*)(descA + 0x134) * invDt + (double)biasA[4]);
    biasA[5] = (float)((double)*(float*)(descA + 0x138) * invDt + (double)biasA[5]);
    biasA[6] = (float)((double)*(float*)(descA + 0x13c) * invDt + (double)biasA[6]);
    double jv = (double)(biasA[6] * jacA[6] + biasA[5] * jacA[5] + biasA[4] * jacA[4] +
                         biasA[2] * jacA[2] + biasA[0] * jacA[0] + biasA[1] * jacA[1]);

    if (*(short*)((int)record + 0x2e) == 1) {
        float imB = *(float*)(descB + 0x1a8);
        biasB[0] = *(float*)(descB + 0x110) * imB + (float)((double)*(float*)(descB + 0x14c) * invDt);
        biasB[1] = *(float*)(descB + 0x114) * imB + (float)((double)*(float*)(descB + 0x150) * invDt);
        biasB[2] = *(float*)(descB + 0x118) * imB + (float)((double)*(float*)(descB + 0x154) * invDt);
        biasB[4] = *(float*)(descB + 0x11c);
        biasB[5] = *(float*)(descB + 0x120);
        biasB[6] = *(float*)(descB + 0x124);
        mtx4_transform_vector_alias((float*)(descB + 0x40), &biasB[4]);
        biasB[4] = (float)((double)*(float*)(descB + 0x134) * invDt + (double)biasB[4]);
        biasB[5] = (float)((double)*(float*)(descB + 0x138) * invDt + (double)biasB[5]);
        biasB[6] = (float)((double)*(float*)(descB + 0x13c) * invDt + (double)biasB[6]);
        jv = (double)(float)(jv + (double)(biasB[6] * jacB[6] + biasB[5] * jacB[5] +
                                           biasB[4] * jacB[4] + biasB[2] * jacB[2] +
                                           biasB[0] * jacB[0] + biasB[1] * jacB[1]));
    } else if (*(short*)((int)record + 0x2e) == 3) {
        // wind/ambient offset for one-body drag constraints
        int windTbl = *(int*)((int)record[10] + 0x7c);
        float windProj[4];
        if (FLOAT_80615f4c <= ABS(FLOAT_806108ec)) {
            float phase = (float)((double)(float)((double)CONCAT44(0x43300000,
                            DAT_806109c0 ^ 0x80000000) - DOUBLE_80615ed8) * invDt) / FLOAT_806108ec;
            windProj[1] = *(float*)(windTbl + 0x3c) * phase;
            windProj[2] = *(float*)((int)record[10] + 0xfc) * phase;
            windProj[0] = *(float*)(windTbl + 0x40) * phase;
        } else {
            windProj[1] = FLOAT_80615ea8;
            windProj[2] = FLOAT_80615ea8;
            windProj[0] = FLOAT_80615ea8;
        }
        jv = (double)(float)(jv - (double)(windProj[0] * jacA[2] +
                                           windProj[1] * jacA[0] + windProj[2] * jacA[1]));
    }

    double lo = (double)*(float*)&loBound;
    double hi = (double)*(float*)&hiBound;
    double lambda = (double)(float)((double)(float)((double)targetVel * invDt - jv) / effMass);
    double clamped = lambda;
    if (lo < lambda) {
        clamped = lo;
    }
    if ((hi <= clamped) && (lo < lambda)) {
        clamped = lo;
    } else if (hi <= clamped) {
        // keep clamped == lambda path handled below
    }
    // final projection to [lo, hi]
    if (!(lo < lambda)) {
        clamped = lambda;
    }
    if (hi <= clamped && lo < lambda) {
        clamped = lo;
    }
    double applied = clamped;

    if ((double)FLOAT_80615ec4 < ABS(applied)) {
        *(float*)(descA + 0x110) = (float)(applied * (double)jacA[0] + (double)*(float*)(descA + 0x110));
        *(float*)(descA + 0x114) = (float)(applied * (double)jacA[1] + (double)*(float*)(descA + 0x114));
        *(float*)(descA + 0x118) = (float)(applied * (double)jacA[2] + (double)*(float*)(descA + 0x118));
        *(float*)(descA + 0x11c) = (float)(applied * (double)jacA[4] + (double)*(float*)(descA + 0x11c));
        *(float*)(descA + 0x120) = (float)(applied * (double)jacA[5] + (double)*(float*)(descA + 0x120));
        *(float*)(descA + 0x124) = (float)(applied * (double)jacA[6] + (double)*(float*)(descA + 0x124));
        if (*(short*)((int)record + 0x2e) == 1) {
            *(float*)(descB + 0x110) = (float)(applied * (double)jacB[0] + (double)*(float*)(descB + 0x110));
            *(float*)(descB + 0x114) = (float)(applied * (double)jacB[1] + (double)*(float*)(descB + 0x114));
            *(float*)(descB + 0x118) = (float)(applied * (double)jacB[2] + (double)*(float*)(descB + 0x118));
            *(float*)(descB + 0x11c) = (float)(applied * (double)jacB[4] + (double)*(float*)(descB + 0x11c));
            *(float*)(descB + 0x120) = (float)(applied * (double)jacB[5] + (double)*(float*)(descB + 0x120));
            *(float*)(descB + 0x124) = (float)(applied * (double)jacB[6] + (double)*(float*)(descB + 0x124));
        }
        subobj_solver_resolve_constraint((unsigned long long)(unsigned int)record,
                                         (double)(float)(applied));
        float* auxA = (float*)(DAT_80612180 + (handleA & 0xffff) * 0x20);
        *(int*)(descA + 0x1d0) = DAT_80611928;
        if (solveTime < (double)*auxA) {
            *auxA = (float)solveTime;
            auxA[1] = record[0];
            auxA[2] = record[1];
            auxA[3] = record[2];
            auxA[7] = record[10];
            auxA[4] = FLOAT_80615f48 * record[3];
            auxA[5] = FLOAT_80615f48 * record[4];
            auxA[6] = FLOAT_80615f48 * record[5];
        }
        if (*(short*)((int)record + 0x2e) == 1) {
            float* auxB = (float*)(DAT_80612180 + (handleB & 0xffff) * 0x20);
            *(int*)(descB + 0x1d0) = DAT_80611928;
            if (-solveTime < (double)*auxB) {
                *auxB = (float)-solveTime;
                auxB[1] = record[0];
                auxB[2] = record[1];
                auxB[3] = record[2];
                auxB[7] = record[10];
                auxB[4] = record[3];
                auxB[5] = record[4];
                auxB[6] = record[5];
            }
        }
    }
    FUN_80382730();
}

// ===========================================================================
// 0x8014d4b8  subobj_collision_broadphase_pass
// Reset the constraint pool, then walk the active descriptor list (DAT_80612168
// via +0x1fc link). For each awake descriptor whose owner entity is in a valid
// room, run the narrow-phase collision query, dispatch owner-type post hooks,
// and (unless flagged) also run a swept query when the frame displacement
// exceeds the object's smallest half-extent. Frame-scoped (FUN_803826e8).
// ===========================================================================
void subobj_collision_broadphase_pass(void)
{
    unsigned int* frame = (unsigned int*)(unsigned long long)FUN_803826e8();
    *frame = 0;
    double dt = 0.0;   // dt arrives in f1; only used to scale velocity below
    subobj_constraint_pool_reset();

    int node = DAT_80612168;
    while (node != 0) {
        int next = *(int*)(node + 0x1fc);
        if (0 < *(char*)(node + 0x1c8)) {
            int entity = *(int*)(DAT_80612184 + (*(unsigned int*)(node + 0x1e4) & 0xffff) * 4);
            if ((0 < *(int*)(entity + 0x34)) && (*(int*)(entity + 0x34) < 0xc9)) {
                unsigned int savedFlags = *(unsigned int*)(entity + 0x28);
                *(unsigned char*)((int)frame + 4) = (unsigned char)((*(unsigned int*)(node + 0x1c0) >> 0xc) & 1);
                *(unsigned char*)((int)frame + 5) = (unsigned char)((*(unsigned int*)(node + 0x1c0) >> 3) & 1);
                *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) & 0xffffff9f;
                subobj_collision_query_dispatch((unsigned int)(unsigned long long)frame, node, entity);
                if (*(int*)(entity + 0x20) == 0x400000) {
                    subobj_rotor_collision_broadphase();
                } else if (*(int*)(entity + 0x20) == 0x10000000) {
                    FUN_80243d4c();
                }
                if ((*(unsigned int*)(node + 0x1c0) & 0x20000000) == 0) {
                    double vx = (double)(float)((double)*(float*)(node + 0x14c) * dt);
                    double vy = (double)(float)((double)*(float*)(node + 0x150) * dt);
                    double vz = (double)(float)((double)*(float*)(node + 0x154) * dt);
                    double minExt = min3f((double)*(float*)(node + 0x17c),
                                          (double)*(float*)(node + 0x180),
                                          (double)*(float*)(node + 0x184));
                    if ((float)(minExt * minExt) <
                        (float)(vz * vz + (double)(float)(vx * vx + (double)(float)(vy * vy)))) {
                        subobj_collision_query_swept((unsigned int)(unsigned long long)frame, node, entity);
                    }
                }
                *(unsigned int*)(entity + 0x28) = savedFlags;
            }
        }
        node = next;
    }
    FUN_80382734();
}

// ===========================================================================
// 0x8014d67c  subobj_constraint_resolve_events
// Prune/merge the constraint list then dispatch per-constraint game events. For
// each surviving constraint (up to maxConstraints): eval relative velocity, run
// the neighbor-node "loud impact" propagation (scene node wake), then branch on
// constraint type: type 3 -> break/damage callbacks + AI capsule notify; type 1
// -> paired friction seeding + callback; type 5 -> decal impact effect; else
// default friction seed. Frame-scoped (FUN_803826d0). param_3 = maxConstraints.
// ===========================================================================
void subobj_constraint_resolve_events(unsigned int dummy0, unsigned int dummy1, int maxConstraints)
{
    (void)dummy0; (void)dummy1;
    float local_e0, local_dc, local_d8, local_d4, local_d0, local_cc;
    float local_c8, local_c4, local_c0; float* local_bc;
    float local_b8, local_b4, local_b0; float* local_ac;
    int neighborNodes[10];
    long long stamp64;

    unsigned long long framed = FUN_803826d0();
    int* outArr = (int*)(framed >> 0x20);
    int count = 0;
    int outByte = 0;

    float* c = subobj_constraint_list_prune_merge((float*)*outArr);
    *outArr = (int)c;
    double one = (double)FLOAT_80615eb0;
    c = (float*)*outArr;

    while ((c != (float*)0) && (count < maxConstraints)) {
        float* descA = (float*)(DAT_8061218c + (*(unsigned int*)((int)c[9] + 0x80) & 0xffff) * 0x200);
        int matTblA = FUN_80252384((unsigned int)*(unsigned char*)(descA + 0x73));
        double relVel = subobj_constraint_eval_rel_velocity((float*)0, c);

        if ((one < ABS(relVel)) && (DAT_805015bd == '\n') &&
            ((((unsigned int)descA[0x70] & 0x1000000) != 0) ||
             (*(int*)((int)c[10] + 0x150) != 0))) {
            double invM = subobj_get_inverse_mass((unsigned int)descA[0x79]);
            double radius2 = (double)(float)((double)(float)(relVel * relVel) * invM);
            double clampR = radius2;
            if ((double)FLOAT_80615f58 < radius2) { clampR = (double)FLOAT_80615f58; }
            if ((double)FLOAT_80615ea8 <= clampR) {
                if ((double)FLOAT_80615f58 < radius2) { radius2 = (double)FLOAT_80615f58; }
            } else { radius2 = (double)FLOAT_80615ea8; }

            int nCount = NEIGHBOR_NODES(*(int*)((int)c[9] + 0x34), (int)neighborNodes, 10);
            int idxByte = DAT_80612610 << 2;
            for (int i = DAT_80612610; i < DAT_80612098; i = i + 1) {
                int* nodePtr = *(int**)((int)&DAT_80588610 + idxByte);
                unsigned int mask;
                if (DAT_805015bd == '\n') { mask = (unsigned int)nodePtr[5]; }
                else { mask = (unsigned int)nodePtr[7]; }
                if (((mask & 0x9180200) != 0) &&
                    (((nodePtr[0xaf] != 0 || nodePtr[99] != 0) ||
                      ((((unsigned int)descA[0x70] & 0x1000000) != 0) &&
                       ((double)FLOAT_80615f34 < ABS(relVel)))) &&
                     ((*(unsigned char*)((int)descA + 0x1c9) & 8) == 0))) {
                    int other = *nodePtr;
                    int seen = 0;
                    int scanByte = 0;
                    unsigned int diff = countLeadingZeros(*(int*)((int)c[9] + 0x34) - *(int*)(other + 0x34));
                    float dz = (*(float*)(other + 0x68) - c[1]) + FLOAT_80615f5c;
                    float dx = *(float*)(other + 100) - c[0];
                    diff = diff >> 5;
                    float dy = *(float*)(other + 0x6c) - c[2];
                    while ((seen < nCount) && (diff == 0)) {
                        if (*(int*)(other + 0x34) == neighborNodes[scanByte >> 2]) { diff = 1; }
                        seen = seen + 1;
                        scanByte = scanByte + 4;
                    }
                    if ((diff != 0) && ((double)(dy * dy + dx * dx + dz * dz) < radius2)) {
                        nodePtr[0x2d8] = DAT_80611928;
                        nodePtr[0x2d9] = -1;
                        nodePtr[10] = nodePtr[10] & 0xf7ffffff;
                    }
                }
                idxByte = idxByte + 4;
            }
        }

        unsigned short type = *(unsigned short*)((int)c + 0x2e);
        if (type == 3) {
            c[8] = FLOAT_80615f60;
            double seedVel;
            if (((unsigned int)descA[0x70] & 0x2000) == 0) {
                seedVel = (double)FLOAT_80615f14;
                subobj_dirty_list_add(*(unsigned int*)((int)c[9] + 0x80));
            } else {
                seedVel = (double)FLOAT_80615f64;
            }
            float ownerB = c[9];
            if (*(int*)((int)ownerB + 0x80) == 0) {
                bool notify = false;
                if ((*(int*)((int)ownerB + 0x170) != 0) ||
                    event_has_handler(*(int*)((int)ownerB + 0x20), 7)) {
                    notify = true;
                }
                if (notify) {
                    local_c8 = c[10];
                    local_c4 = -c[3];
                    local_c0 = -c[5];
                    local_bc = c;
                    (void)local_bc;
                    FUN_8014ff80(c[9], (unsigned int)(unsigned long long)&local_c8);
                }
            }
            if ((((unsigned int)descA[0x70] & 0x4000) == 0) && (relVel < seedVel)) {
                int aiIdx = AI_CAPSULE_TEST();
                player_ai_set_active_index(aiIdx);
                local_e0 = FLOAT_80615f48 * c[3];
                local_dc = FLOAT_80615f48 * c[4];
                local_d8 = FLOAT_80615f48 * c[5];
                subobj_world_to_local_dir(*(unsigned int*)((int)c[9] + 0x80), c, &local_d4);
                float ownerBv = c[10];
                double localVel = -(double)(local_cc * c[5] + local_d4 * c[3] + local_d0 * c[4]);
                if (*(int*)((int)ownerBv + 0x150) == 0) {
                    FUN_80284ce0(relVel, localVel, (double)c[6], 0, 0, 0, 0, 0,
                                 ownerBv, c[9], &local_e0, c);
                } else {
                    FUN_8025a120(relVel, localVel, (double)c[6], 0, 0, 0, 0, 0,
                                 ownerBv, c[9], &local_e0, c);
                }
            }
        } else if (type == 1) {
            int descB = DAT_8061218c + (*(unsigned int*)((int)c[10] + 0x80) & 0xffff) * 0x200;
            int matTblB = FUN_80252384((unsigned int)*(unsigned char*)(descB + 0x1cc));
            char act = *(char*)(descB + 0x1c8);
            subobj_dirty_list_add(*(unsigned int*)((int)c[10] + 0x80));
            char newAct = 1;
            if (0 < act) { newAct = act; }
            *(char*)(descB + 0x1c8) = newAct;
            float fr = *(float*)(matTblB + 0xc);
            if (*(float*)(matTblA + 0xc) < fr) { fr = *(float*)(matTblA + 0xc); }
            c[8] = fr;
            if (*(void**)(descB + 0x1ec) != (void*)0) {
                typedef void (*pair_cb)(double, int, float*);
                ((pair_cb)*(void**)(descB + 0x1ec))(relVel,
                    *(int*)(DAT_80612184 + (*(unsigned int*)(descB + 0x1e4) & 0xffff) * 4), c);
            }
        } else if (type == 0) {
            c[8] = *(float*)(matTblA + 0xc);
            float ownerB = c[10];
            if (*(int*)((int)ownerB + 0x80) == 0) {
                bool notify = false;
                if ((*(int*)((int)ownerB + 0x170) != 0) ||
                    event_has_handler(*(int*)((int)ownerB + 0x20), 7)) {
                    notify = true;
                }
                if (notify) {
                    local_b8 = c[9];
                    local_b4 = -c[3];
                    local_b0 = -c[5];
                    local_ac = c;
                    (void)local_ac;
                    FUN_8014ff80(c[10], (unsigned int)(unsigned long long)&local_b8);
                }
            }
        } else if (type == 5) {
            c[8] = FLOAT_80615ea8;
            if ((int)descA[0x77] < DAT_80611928) {
                float ay = c[4];
                float ax = c[3];
                double az = (double)c[5];
                double one2 = (double)FLOAT_80615eb0;
                double d0 = ABS((double)(float)(az * (double)descA[2] +
                            (double)(ax * descA[0] + ay * descA[1])));
                double sc0 = (double)descA[0x5f];
                double comp1 = (double)(descA[0x60] *
                    (float)(one2 - ABS((double)(float)(az * (double)descA[6] +
                            (double)(ax * descA[4] + ay * descA[5])))));
                float v0 = (float)(sc0 * (double)(float)(one2 - d0));
                float v2 = descA[0x61] *
                    (float)(one2 - ABS((double)(float)(az * (double)descA[10] +
                            (double)(ax * descA[8] + ay * descA[9]))));
                decal_impact_effect(
                    (float)(((int)(v2 * v2 + v0 * v0 + (float)(comp1 * comp1)) >> 1) + 0x1fc00000),
                    (int)c[10], c, descA + 0x53);
                unsigned int fx = DAT_80611928 ^ 0x80000000;
                unsigned int fx2 = (unsigned int)(DAT_806109c0 << 1) ^ 0x80000000;
                float stampF = (float)(int)(
                    (float)((double)CONCAT44(0x43300000, fx) - DOUBLE_80615ed8) +
                    (float)((double)CONCAT44(0x43300000, fx2) - DOUBLE_80615ed8));
                stamp64 = (long long)(int)stampF;
                (void)stamp64;
                descA[0x77] = stampF;
            }
        } else {
            c[8] = *(float*)(matTblA + 0xc);
        }

        if (*(void**)&descA[0x7b] != (void*)0) {
            typedef void (*contact_cb)(double, int, float*);
            ((contact_cb)*(void**)&descA[0x7b])(relVel,
                *(int*)(DAT_80612184 + ((unsigned int)descA[0x79] & 0xffff) * 4), c);
        }
        *(float**)((int)(framed >> 0x20) + outByte) = c;
        count = count + 1;
        outByte = outByte + 4;
        c = *(float**)(c + 0xd);
    }
    FUN_8038271c();
}

// ===========================================================================
// 0x8014dd64  subobj_save_impulse_state
// For every active descriptor: snapshot its 6-word linear+angular accumulator
// into a freshly allocated 0x18-byte scratch block (+0x1b8), add the per-step
// gravity term to the linear-Y accumulator (unless flag 0x40000 set), and clear
// the aux-table contact depth. Runs before the solve iterations.
// ===========================================================================
void subobj_save_impulse_state(void)
{
    double zero = (double)FLOAT_80615ea8;
    for (int node = DAT_80612168; node != 0; node = *(int*)(node + 0x1fc)) {
        unsigned int handle = *(unsigned int*)(node + 0x1e4);
        int block = MEM_ADVANCE_SECONDARY(0x18);
        *(int*)(node + 0x1b8) = block;
        if (*(int**)(node + 0x1b8) != (int*)0) {
            int b = *(int*)(node + 0x1b8);
            *(int*)(b + 0x00) = *(int*)(node + 0x110);
            *(int*)(b + 0x04) = *(int*)(node + 0x114);
            *(int*)(b + 0x08) = *(int*)(node + 0x118);
            *(int*)(b + 0x0c) = *(int*)(node + 0x11c);
            *(int*)(b + 0x10) = *(int*)(node + 0x120);
            *(int*)(b + 0x14) = *(int*)(node + 0x124);
        }
        if ((*(unsigned int*)(node + 0x1c0) & 0x40000) == 0) {
            *(float*)(node + 0x114) =
                *(float*)(node + 0x114) + FLOAT_80615f68 / *(float*)(node + 0x1a8);
        }
        *(float*)(DAT_80612180 + (handle & 0xffff) * 0x20) = (float)zero;
    }
}

// ===========================================================================
// 0x8014de4c  subobj_integrate_pass
// The Gauss-Seidel sub-step loop. Advances an internal accumulator by dt
// (param_2) until it reaches the frame time (f1). Each iteration: Fisher-Yates
// shuffle the per-frame constraint index array (rng_counter_tick), dispatch the
// island builders (FUN_8014ee3c), solve every non-sleeping constraint row
// (subobj_solver_solve_row), integrate each body (subobj_integrate), apply
// per-axis angular damping scaled by material + frame counter, renormalize
// angular velocity, restore the pre-step velocity snapshot, and re-add gravity.
// Frame-scoped (FUN_803826e4).
// ===========================================================================
void subobj_integrate_pass(unsigned long long frameArg, double dt)
{
    (void)frameArg;
    float relLocal[4];
    unsigned long long framed = FUN_803826e4();
    int arr = (int)((unsigned long long)framed >> 0x20);
    int count = (int)framed;
    double acc = (double)FLOAT_80615ea8;
    double frameTime = dt;   // f1 carries the frame time; param_2 carries the sub-dt
    double magic = DOUBLE_80615ed8;

    while (acc < frameTime) {
        // Fisher-Yates shuffle of the constraint order
        int slot = 4;
        for (unsigned int i = 1; (int)i < count; i = i + 1) {
            unsigned int r = RNG_TICK();
            int tmp = *(int*)(arr + slot);
            int j = (int)(r - (r / i) * i) * 4;
            *(int*)(arr + slot) = *(int*)(arr + j);
            slot = slot + 4;
            *(int*)(arr + j) = tmp;
        }

        FUN_8014ee3c();

        int itByte = 0;
        for (int k = 0; k < count; k = k + 1) {
            float* rec = *(float**)(arr + itByte);
            if ((*(short*)((int)rec + 0x2e) != 5) &&
                ((*(short*)((int)rec + 0x2e) != 3) ||
                 ((*(unsigned int*)(DAT_8061218c + (*(unsigned int*)((int)rec[9] + 0x80) & 0xffff) * 0x200 + 0x1c0) & 0x2000) == 0))) {
                double rv = subobj_constraint_eval_rel_velocity(relLocal, rec);
                subobj_solver_solve_row(dt, rv);
            }
            itByte = itByte + 4;
        }

        acc = (double)(float)(acc + dt);

        for (float* body = (float*)DAT_80612168; body != (float*)0; body = *(float**)(body + 0x7f)) {
            subobj_integrate(dt, body);
            int matTbl = FUN_80252384((unsigned int)*(unsigned char*)(body + 0x73));
            // per-axis angular damping: v -= dt * frameCounter * damp * v^2 * sign(v)
            for (int axis = 0; axis < 3; axis = axis + 1) {
                double v = (double)body[0x4d + axis];
                unsigned int fx = DAT_806109c0 ^ 0x80000000;
                body[0x4d + axis] =
                    -(float)(dt * (double)((float)((double)CONCAT44(0x43300000, fx) - magic) *
                             *(float*)(matTbl + 8) *
                             (float)(v * v) *
                             (float)(((unsigned int)body[0x4d + axis] & 0x80000000) | 0x3f800000)) - v);
            }
            if (((unsigned int)body[0x70] & 0x20) == 0) {
                float ax = body[0x4d];
                float len = body[0x4f] * body[0x4f] + ax * ax + body[0x4e] * body[0x4e];
                if (FLOAT_80615f6c < len) {
                    float rInv = 1.0f / SQRT(len);
                    rInv = rInv * -(FLOAT_80615ebc * len * rInv * rInv - FLOAT_80615ec8);
                    float scale = FLOAT_80615f70 *
                                  rInv * -(FLOAT_80615ebc * len * rInv * rInv - FLOAT_80615ec8);
                    body[0x4d] = ax * scale;
                    body[0x4e] = body[0x4e] * scale;
                    body[0x4f] = body[0x4f] * scale;
                }
            }
            subobj_apply_angular_impulse_local();
            int snap = *(int*)(body + 0x6e);
            if (snap == 0) {
                float z = FLOAT_80615ea8;
                body[0x44] = z; body[0x45] = z; body[0x46] = z;
                body[0x47] = z; body[0x48] = z; body[0x49] = z;
            } else {
                body[0x44] = *(float*)(snap + 0x00);
                body[0x45] = *(float*)(snap + 0x04);
                body[0x46] = *(float*)(snap + 0x08);
                body[0x47] = *(float*)(snap + 0x0c);
                body[0x48] = *(float*)(snap + 0x10);
                body[0x49] = *(float*)(snap + 0x14);
            }
            if ((((unsigned int)body[0x70] & 0x40000) == 0) && (acc < frameTime)) {
                body[0x45] = body[0x45] + FLOAT_80615f68 / body[0x6a];
            }
        }
    }
    FUN_80382730();
}

// ===========================================================================
// 0x8014e1fc  subobj_finalize_bodies
// Post-solve finalize over every active descriptor: clear velocity snapshot,
// apply accumulated impact damage (subobj_process_impact_damage), reassign the
// owner entity's room/portal via ray traversal, rebuild world/screen bounds,
// and manage the sleep/dirty lifecycle (destroy off-screen debris, decrement
// the settle timer, remove from the dirty list when settled). Frame-scoped
// (__save_gpr_28 / __restore_gpr_28 spill).
// ===========================================================================
void subobj_finalize_bodies(unsigned long long selfArg, double p2, double p3, double p4,
                            double p5, double p6, double p7, double p8)
{
    __save_gpr_28();
    float local_58, local_54, local_50, local_4c, local_48, local_44;
    float local_40, local_3c, local_38, local_34, local_30, local_2c;
    double zero = (double)FLOAT_80615ea8;
    (void)selfArg;

    for (float* body = (float*)DAT_80612168; body != (float*)0; body = *(float**)(body + 0x7f)) {
        body[0x44] = (float)zero; body[0x45] = (float)zero; body[0x46] = (float)zero;
        body[0x47] = (float)zero; body[0x48] = (float)zero; body[0x49] = (float)zero;

        if (*(int*)(DAT_80612184 + ((unsigned int)body[0x79] & 0xffff) * 4) == 0) { continue; }

        unsigned long long dmgArg = FUN_80268018();
        subobj_process_impact_damage(dmgArg, p2, p3, p4, p5, p6, p7, p8);
        int* entity = *(int**)(DAT_80612184 + ((unsigned int)body[0x79] & 0xffff) * 4);
        if (entity == (int*)0) { continue; }

        entity[6] = entity[6] | 0x40;
        if (((entity[4] | entity[3]) & 0xffff) != 0) {
            body[0x6f] = (float)(int)DAT_80611928;
        }
        FUN_80253544();

        if (((unsigned int)body[0x70] & 0x10000) != 0) { continue; }

        entity[0x2d] = (int)body[0x53];
        entity[0x2e] = (int)body[0x54];
        entity[0x2f] = (int)body[0x55];
        local_4c = body[0x50] - body[0x56];
        local_48 = body[0x51] - body[0x57];
        local_44 = (float)((double)body[0x52] - (double)body[0x58]);

        if ((entity[0xd] == 0) || (200 < entity[0xd])) {
            entity[0xd] = 0;
            entity[0xd] = ENTITY_ASSIGN_ROOM((int)entity);
        }
        if (*(char*)(entity + 0xc) == '\0') {
            entity[0xe] = 0xd5;
            entity[0xd] = 0xd5;
            *(char*)(entity + 0xc) = 1;
        } else if (((entity[10] & 0x40000000) == 0) && (0 < entity[0xd]) && (entity[0xd] < 0xc9)) {
            entity[0xd] = SCENE_RAY_PORTALS((double)zero, p2, body + 0x56, entity[0xd], &local_4c, 0);
        }

        float fz = FLOAT_80615ea8;
        body[0x56] = body[0x50];
        body[0x57] = body[0x51];
        body[0x58] = body[0x52];
        entity[0x30] = (int)fz;
        entity[0x1c] = (int)fz;
        entity[0x32] = (int)fz;

        if ((entity[2] & 2) != 0) {
            subobj_rebuild_after_lod_swap();
            subobj_compute_vertex_bounds((unsigned int)body[0x79]);
        }
        subobj_apply_world_bounds((unsigned int)body[0x79]);
        if (((unsigned int)body[0x70] & 0x200000) != 0) {
            subobj_compute_screen_quad((unsigned int)body[0x79]);
        }

        bool needQuery = false;
        if (((entity[10] & 0x40000000) == 0) && (entity[0xd] != 0xd5) &&
            ((entity[0xd] < 1) || (200 < entity[0xd]))) {
            needQuery = true;
        }
        double age = subobj_get_age_fraction((unsigned int)body[0x79]);
        if (needQuery || ((double)FLOAT_80615eec < age)) {
            local_34 = (float)entity[0x19] + (float)entity[0x6a] + (float)entity[0x70];
            local_40 = ((float)entity[0x19] + (float)entity[0x6a]) - (float)entity[0x70];
            local_30 = (float)entity[0x1a] + (float)entity[0x6b] + (float)entity[0x71];
            local_3c = ((float)entity[0x1a] + (float)entity[0x6b]) - (float)entity[0x71];
            float cz = (float)entity[0x1b] + (float)entity[0x6c];
            local_2c = cz + (float)entity[0x72];
            local_38 = cz - (float)entity[0x72];
            (void)local_58; (void)local_54; (void)local_50;
            int inFrustum = FRUSTUM_VS_WORLD(&local_40);
            if ((inFrustum == 0) &&
                (*(short*)(entity + 0x25) != 0x385) && (*(short*)(entity + 0x25) != 0x88f)) {
                if (entity[8] == 0x20000000) {
                    FUN_803654f0();
                } else {
                    entity_destroy_with_event(entity);
                }
            } else if (needQuery) {
                int hit = scene_query_nodes_in_frustum(0, (int)&local_40, (int)(entity + 0xe), 10);
                *(char*)(entity + 0xc) = (char)hit;
                if ((char)hit != '\0') { entity[0xd] = entity[0xe]; }
            }
        }

        unsigned int flags = (unsigned int)body[0x70];
        if ((flags & 4) != 0) { continue; }

        bool settle = false;
        if (((unsigned int)(DAT_806109c0 * 10) < (unsigned int)((int)DAT_80611928 - (int)body[0x6f])) &&
            ((flags & 0x10000000) == 0)) {
            settle = false;
        } else if ((flags & 0x4000) == 0) {
            float cx = (float)((double)body[0x4f] * (double)body[0x60] -
                        (double)(float)((double)body[0x4d] * (double)body[0x61]));
            float cy = (float)((double)body[0x4e] * (double)body[0x61] -
                        (double)(float)((double)body[0x4f] * (double)body[0x60]));
            float angMag2 = (float)((double)cx * cx +
                            (double)(float)((double)cy * cy +
                            (double)(float)((double)body[0x4d] * (double)body[0x60] -
                                            (double)(float)((double)body[0x4e] * (double)body[0x5f]))));
            float linMag2 = body[0x55] * body[0x55] + body[0x53] * body[0x53] + body[0x54] * body[0x54];
            (void)angMag2;
            if ((FLOAT_80615ef8 < linMag2) || ((double)FLOAT_80615f74 < (double)angMag2)) {
                settle = true;
            } else {
                local_58 = body[0x5f]; local_54 = body[0x60]; local_50 = body[0x61];
                mtx4_transform_vector_alias(body, &local_58);
                local_58 = local_58 + body[0x50];
                local_54 = local_54 + body[0x51];
                local_50 = local_50 + body[0x52];
                float dx = (float)((double)local_58 - (double)body[0x6b]);
                float dy = local_54 - body[0x6c];
                float dz = local_50 - body[0x6d];
                body[0x6b] = local_58;
                body[0x6c] = local_54;
                body[0x6d] = local_50;
                if (FLOAT_80615f78 <= (float)((double)dz * dz +
                                       (double)(float)((double)dx * dx + (double)(float)((double)dy * dy)))) {
                    settle = true;
                }
            }
        } else {
            settle = true;
        }

        if (settle) {
            *(char*)(body + 0x72) = (char)(DAT_806109c0 / 6);
        } else {
            *(char*)(body + 0x72) = *(char*)(body + 0x72) - 1;
            if (*(char*)(body + 0x72) == '\0') {
                subobj_dirty_list_remove((unsigned int)body[0x79]);
            }
        }
    }
    __restore_gpr_28();
}

// ===========================================================================
// 0x8014e6e4  subobj_physics_step
// Top-level per-frame physics entry (called from entity_post_transform_pass).
// When physics is enabled (DAT_806108e4 sub-step count > 0): seed the wind/dt
// window average, allocate a scratch page for the constraint index array, then
// for each sub-step run broadphase -> area-effect -> resolve-events -> island
// fixup -> save-state, choosing the adaptive dt, and integrate. Finally free the
// page, reset the constraint pool and finalize all bodies.
// ===========================================================================
void subobj_physics_step(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                         unsigned long long p4, double p5, double p6, double p7, double p8,
                         unsigned int p9, unsigned int p10, unsigned int p11, unsigned int p12,
                         unsigned int p13, unsigned int p14, unsigned int p15, unsigned int p16)
{
    (void)p9;
    int constraintFlag[2];
    unsigned long long framed = FUN_803826f0();

    if (DAT_806108e4 != 0) {
        FUN_8014ff00(framed, p2, p3, p4, p5, p6, p7, p8, 0x10, 0,
                     p11, p12, p13, p14, p15, p16);
        DAT_80612164 = 1;
        int page = mem_page_alloc();
        *(float*)(&DAT_805893a0 + DAT_806121b8 * 4) = FLOAT_806108ec;
        DAT_806121b8 = (DAT_806121b8 + 1) % 0xf;
        double avg = subobj_window_average();
        double stepCount = (double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000);
        double frameDt = (double)(float)((double)(FLOAT_80615eb0 /
            (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80615ed8)) * avg);
        double subDt = (double)(float)(frameDt / (double)(float)(stepCount - DOUBLE_80615ed8));
        double adaptiveDt = (double)(float)((double)FLOAT_80615f7c * subDt);
        double lastDt = adaptiveDt;

        for (int step = 0; step < DAT_806108e4; step = step + 1) {
            subobj_collision_broadphase_pass();
            subobj_area_effect_broadphase();
            subobj_constraint_resolve_events((unsigned int)(unsigned long long)constraintFlag, page, 0xdb);
            FUN_8014ef54();
            subobj_save_impulse_state();
            double useDt = lastDt;
            if ((constraintFlag[0] == 0) && (DAT_806121a8 == 0)) {
                useDt = subDt;
            }
            lastDt = useDt;
            subobj_integrate_pass((unsigned long long)(long long)subDt, useDt);
        }

        mem_page_free(page);
        subobj_constraint_pool_reset();
        DAT_80612164 = 0;
        unsigned long long finArg = FUN_8014fff8();
        subobj_finalize_bodies(finArg, lastDt, frameDt, DOUBLE_80615ed8, p5, p6, p7, p8);
    }
    FUN_8038273c();
}
