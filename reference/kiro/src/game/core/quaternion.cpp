// Batch 349 | Date: 2026-09-04
// Quaternion math primitives (xyzw layout, float[4]).
//
//   quat_multiply       (0x8012e524) Hamilton product a = b * c
//   quat_rotate_vector  (0x8012e070) rotate a vector by a quaternion
//   quat_to_matrix      (0x8012e11c) quaternion -> 4x4 rotation matrix (16 floats)
//   quat_slerp          (0x8012e234) spherical linear interpolation
//
// Subsystem: game (core math). Compile-Clean Standard: faithful bodies + per-file
// externs for raw symbols.

#include "common.h"

// Local sqrt (the target hardware uses PPC fsqrts here; this TU includes only
// common.h so we avoid the C library. A bit-trick seed + two Newton steps is
// ample for the slerp weight, which is further refined below).
static inline float quat_sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    union { float f; unsigned int i; } u;
    u.f = x;
    u.i = 0x1fbd1df5u + (u.i >> 1);          // fast sqrt seed
    float y = u.f;
    y = 0.5f * (y + x / y);                    // Newton step 1
    y = 0.5f * (y + x / y);                    // Newton step 2
    return y;
}

// ---- math constants (raw floats) ----
extern float FLOAT_80615a20;  // 2.0
extern float FLOAT_80615a24;  // 1.0
extern float FLOAT_80615a28;  // 0.0
extern float FLOAT_80615a2c;  // slerp near-parallel threshold
extern float FLOAT_80615a30;  // rsqrt refine coeff
extern float FLOAT_80615a34;  // rsqrt refine coeff
extern float FLOAT_80615a38;  // angle/pi scale

// ---- transcendental helpers (raw) ----
extern double FUN_801313a4(double x);  // sin-like weight
extern double FUN_801314f4(double x);  // acos/angle-related

// ----------------------------------------------------------------------------
// quat_multiply (0x8012e524)  a = b * c   (vector part negated per engine convention)
// ----------------------------------------------------------------------------
void quat_multiply(float* param_1, float* param_2, float* param_3)
{
    float fVar1 = param_2[0];
    float fVar2 = param_3[3];
    float fVar3 = param_2[1];
    float fVar4 = param_3[0];
    float fVar5 = param_2[3];
    float fVar6 = param_2[2];
    float fVar7 = param_3[1];
    float fVar8 = param_3[2];
    param_1[0] = -(fVar6 * fVar7 - (fVar3 * fVar8 + fVar5 * fVar4 + fVar1 * fVar2));
    param_1[1] = -(fVar1 * fVar8 - (fVar6 * fVar4 + fVar5 * fVar7 + fVar3 * fVar2));
    param_1[2] = -(fVar3 * fVar4 - (fVar1 * fVar7 + fVar5 * fVar8 + fVar6 * fVar2));
    param_1[3] = -(fVar6 * fVar8 - -(fVar3 * fVar7 - (fVar5 * fVar2 - fVar1 * fVar4)));
}

// ----------------------------------------------------------------------------
// quat_rotate_vector (0x8012e070)  rotate vector param_1 by quaternion param_2 -> param_3
// ----------------------------------------------------------------------------
void quat_rotate_vector(float* param_1, float* param_2, float* param_3)
{
    float fVar1 = param_1[1];
    float fVar2 = param_2[1];
    float fVar3 = param_2[0];
    float fVar4 = param_1[2];
    float fVar5 = param_1[0];
    float fVar6 = param_2[2];
    float fVar7 = param_1[3];
    float fVar10 = fVar6 * fVar4 + fVar3 * fVar5 + fVar2 * fVar1;
    float fVar8 = fVar6 * fVar1 + (fVar3 * fVar7 - fVar2 * fVar4);
    float fVar9 = fVar2 * fVar5 + (fVar6 * fVar7 - fVar3 * fVar1);
    fVar2 = fVar3 * fVar4 + (fVar2 * fVar7 - fVar6 * fVar5);
    param_3[0] = -(fVar4 * fVar2 - (fVar1 * fVar9 + fVar7 * fVar8 + fVar5 * fVar10));
    param_3[1] = -(param_1[0] * fVar9 - (param_1[2] * fVar8 + param_1[3] * fVar2 + param_1[1] * fVar10));
    param_3[2] = -(param_1[1] * fVar8 - (param_1[0] * fVar2 + param_1[3] * fVar9 + param_1[2] * fVar10));
}

// ----------------------------------------------------------------------------
// quat_to_matrix (0x8012e11c)  quaternion param_2 -> 4x4 rotation matrix param_1
// ----------------------------------------------------------------------------
void quat_to_matrix(float* param_1, float* param_2)
{
    float fVar10 = FLOAT_80615a28;  // 0.0
    float fVar9 = FLOAT_80615a24;   // 1.0
    float fVar1 = param_2[0];
    float fVar2 = param_2[1];
    float fVar3 = param_2[2];
    float fVar4 = param_2[3];
    float fVar7 = FLOAT_80615a20 / (fVar4 * fVar4 + fVar3 * fVar3 + fVar1 * fVar1 + fVar2 * fVar2);
    float fVar8 = fVar3 * fVar7;
    float fVar6 = fVar2 * fVar7;
    float fVar5 = fVar1 * fVar1 * fVar7;
    param_1[0] = FLOAT_80615a24 - (fVar2 * fVar6 + fVar3 * fVar8);
    param_1[1] = fVar1 * fVar6 + fVar4 * fVar8;
    fVar7 = fVar4 * fVar1 * fVar7;
    param_1[2] = fVar1 * fVar8 - fVar4 * fVar6;
    param_1[4] = fVar1 * fVar6 - fVar4 * fVar8;
    param_1[5] = fVar9 - (fVar5 + fVar3 * fVar8);
    param_1[6] = fVar2 * fVar8 + fVar7;
    param_1[8] = fVar1 * fVar8 + fVar4 * fVar6;
    param_1[9] = fVar2 * fVar8 - fVar7;
    param_1[10] = fVar9 - (fVar5 + fVar2 * fVar6);
    param_1[11] = fVar10;
    param_1[7] = fVar10;
    param_1[3] = fVar10;
    param_1[14] = fVar10;
    param_1[13] = fVar10;
    param_1[12] = fVar10;
    param_1[15] = fVar9;
}

// ----------------------------------------------------------------------------
// quat_slerp (0x8012e234)  slerp(param_2, param_3, t=param_1) -> param_4
// ----------------------------------------------------------------------------
void quat_slerp(double param_1, float* param_2, float* param_3, float* param_4)
{
    if ((double)FLOAT_80615a28 == param_1) {          // t == 0 -> copy q0
        param_4[0] = param_2[0];
        param_4[1] = param_2[1];
        param_4[2] = param_2[2];
        param_4[3] = param_2[3];
    } else if ((double)FLOAT_80615a24 == param_1) {   // t == 1 -> copy q1
        param_4[0] = param_3[0];
        param_4[1] = param_3[1];
        param_4[2] = param_3[2];
        param_4[3] = param_3[3];
    } else {
        double dVar6 = (double)param_2[0];
        double dVar12 = (double)param_3[0];
        double dVar7 = (double)param_2[1];
        double dVar11 = (double)param_3[1];
        double dVar8 = (double)param_2[2];
        double dVar10 = (double)param_3[2];
        double dVar4 = (double)param_2[3];
        double dVar9 = (double)param_3[3];

        // choose shortest arc: flip q1 if the "sum" is longer than the "difference"
        if ((float)(dVar4 + dVar9) * (float)(dVar4 + dVar9) +
            (float)(dVar8 + dVar10) * (float)(dVar8 + dVar10) +
            (float)(dVar6 + dVar12) * (float)(dVar6 + dVar12) +
            (float)(dVar7 + dVar11) * (float)(dVar7 + dVar11) <
            (float)(dVar4 - dVar9) * (float)(dVar4 - dVar9) +
            (float)(dVar8 - dVar10) * (float)(dVar8 - dVar10) +
            (float)(dVar6 - dVar12) * (float)(dVar6 - dVar12) +
            (float)(dVar7 - dVar11) * (float)(dVar7 - dVar11)) {
            dVar12 = -dVar12;
            dVar11 = -dVar11;
            dVar10 = -dVar10;
            dVar9 = -dVar9;
        }

        double dVar5 = (double)FLOAT_80615a24;  // 1.0
        dVar4 = (double)(float)(dVar4 * dVar9 +
                (double)(float)(dVar8 * dVar10 +
                (double)(float)(dVar6 * dVar12 +
                (double)(float)(dVar7 * dVar11))));   // cos(theta)

        double wA, wB;
        if ((float)(dVar5 + dVar4) <= FLOAT_80615a2c) {
            // near-antiparallel: use the symmetric sin blend
            wA = FUN_801313a4((double)(FLOAT_80615a38 * (float)(dVar5 - param_1) - FLOAT_80615a38));
            wB = FUN_801313a4((double)(float)((double)FLOAT_80615a38 * param_1 - (double)FLOAT_80615a38));
            param_4[0] = (float)(wA * (double)param_2[0] - (double)(float)(wB * dVar12));
            param_4[1] = (float)(wA * (double)param_2[1] - (double)(float)(wB * dVar11));
            param_4[2] = (float)(wA * (double)param_2[2] - (double)(float)(wB * dVar10));
            param_4[3] = (float)(wA * (double)param_2[3] - (double)(float)(wB * dVar9));
            return;
        }

        if ((float)(dVar5 - dVar4) <= FLOAT_80615a2c) {
            // near-parallel: linear blend
            wA = (double)(float)(dVar5 - param_1);
            wB = param_1;
        } else {
            double invSin = (double)FUN_801314f4(dVar4);  // 1/theta-ish
            double one = (double)FLOAT_80615a24;
            float s2 = -(float)(dVar4 * dVar4 - one);      // 1 - cos^2 = sin^2
            float c = FLOAT_80615a30 * s2;
            float rs = 1.0f / quat_sqrtf(s2);              // rsqrt(sin^2)
            rs = rs * -(c * rs * rs - FLOAT_80615a34);     // Newton refine
            double sinScale = (double)(float)(one * (double)(rs * -(c * rs * rs - FLOAT_80615a34)));
            double a = FUN_801313a4((double)(float)((double)(float)(one - param_1) * invSin -
                                                    (double)FLOAT_80615a38));
            wA = (double)(float)(sinScale * a);
            double b = FUN_801313a4((double)(float)(param_1 * invSin - (double)FLOAT_80615a38));
            wB = (double)(float)(sinScale * b);
        }
        param_4[0] = (float)(wA * (double)param_2[0] + (double)(float)(wB * dVar12));
        param_4[1] = (float)(wA * (double)param_2[1] + (double)(float)(wB * dVar11));
        param_4[2] = (float)(wA * (double)param_2[2] + (double)(float)(wB * dVar10));
        param_4[3] = (float)(wA * (double)param_2[3] + (double)(float)(wB * dVar9));
    }
}

// ============================================================================
// Batch 350 additions: conjugate, normalize, axis-angle construction.
// ============================================================================
extern float FLOAT_80615a3c;  // axis-length epsilon

// ----------------------------------------------------------------------------
// quat_conjugate (0x8012e598)  param_1 = conjugate(param_2): negate x/y/z, keep w
// ----------------------------------------------------------------------------
void quat_conjugate(float* param_1, float* param_2)
{
    param_1[0] = -param_2[0];
    param_1[1] = -param_2[1];
    param_1[2] = -param_2[2];
    param_1[3] = param_2[3];
}

// ----------------------------------------------------------------------------
// quat_normalize (0x8012e5c8)  normalize in place (rsqrt + Newton refine)
// ----------------------------------------------------------------------------
void quat_normalize(float* param_1)
{
    float fVar1 = param_1[0];
    float fVar2 = param_1[3] * param_1[3] + param_1[2] * param_1[2] +
                  fVar1 * fVar1 + param_1[1] * param_1[1];
    float fVar3 = 1.0f / quat_sqrtf(fVar2);
    fVar2 = FLOAT_80615a30 * fVar2;
    fVar3 = fVar3 * -(fVar2 * fVar3 * fVar3 - FLOAT_80615a34);
    fVar2 = FLOAT_80615a24 * fVar3 * -(fVar2 * fVar3 * fVar3 - FLOAT_80615a34);
    param_1[0] = fVar1 * fVar2;
    param_1[1] = param_1[1] * fVar2;
    param_1[2] = param_1[2] * fVar2;
    param_1[3] = param_1[3] * fVar2;
}

// ----------------------------------------------------------------------------
// quat_from_axis_angle (0x8012e654)
// param_2 = quaternion for rotation of angle param_1 about axis param_3.
// ----------------------------------------------------------------------------
void quat_from_axis_angle(double param_1, float* param_2, float* param_3)
{
    double half = (double)(float)((double)FLOAT_80615a30 * param_1);  // angle/2
    double c = FUN_801313a4((double)(float)(half - (double)FLOAT_80615a38));  // cos(angle/2)
    double s = FUN_801313a4(half);                                            // sin(angle/2)

    float fVar1 = param_3[0];
    float lenSq = param_3[2] * param_3[2] + fVar1 * fVar1 + param_3[1] * param_3[1];
    if (FLOAT_80615a3c < lenSq) {
        float rs = 1.0f / quat_sqrtf(lenSq);
        rs = rs * -(FLOAT_80615a30 * lenSq * rs * rs - FLOAT_80615a34);
        float scale = FLOAT_80615a24 * rs * -(FLOAT_80615a30 * lenSq * rs * rs - FLOAT_80615a34);
        param_3[0] = fVar1 * scale;
        param_3[1] = param_3[1] * scale;
        param_3[2] = param_3[2] * scale;
    }
    param_2[0] = (float)((double)param_3[0] * c);
    param_2[1] = (float)((double)param_3[1] * c);
    param_2[2] = (float)((double)param_3[2] * c);
    param_2[3] = (float)s;
}
