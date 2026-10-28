// Batch 351 | Date: 2026-09-04
// Vector / geometry / interpolation utilities.
//
//   vec3_build_basis_from_dir  (0x8012ea88) orthonormal basis around a direction
//   point_in_quad_edge_mask    (0x8012ee40) per-edge side test for a 2D quad
//   scalar_step_toward_snap    (0x8012edfc) move a scalar toward a target with snap
//   angle_step_toward_clamped  (0x8012ec48) step an angle toward a clamped target
//
// Subsystem: game (core math). Compile-Clean Standard.

#include "common.h"

extern float FLOAT_80615a48;  // basis: axis-length epsilon
extern float FLOAT_80615a4c;  // rsqrt refine coeff
extern float FLOAT_80615a50;  // rsqrt refine coeff
extern float FLOAT_80615a54;  // rsqrt scale (0.5*3 etc.)
extern float FLOAT_80615a58;  // 0.0
extern float FLOAT_80615a5c;  // angle epsilon
extern float FLOAT_80615a60;  // 360.0 wrap
extern float FLOAT_80615a64;  // -180.0
extern float FLOAT_80615a68;  // +180.0

extern double FUN_80390324(double x);  // floor/round helper

static inline float vu_sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    union { float f; unsigned int i; } u; u.f = x;
    u.i = 0x1fbd1df5u + (u.i >> 1);
    float y = u.f;
    y = 0.5f * (y + x / y);
    y = 0.5f * (y + x / y);
    return y;
}

// ----------------------------------------------------------------------------
// vec3_build_basis_from_dir (0x8012ea88)
// param_3 = direction (optionally normalized when param_4==0); param_1 = a unit
// vector perpendicular to it; param_2 = param_3 x param_1.
// ----------------------------------------------------------------------------
void vec3_build_basis_from_dir(float* param_1, float* param_2, float* param_3, int param_4)
{
    if (param_4 == 0) {
        float fVar1 = param_3[0];
        float fVar2 = param_3[2] * param_3[2] + fVar1 * fVar1 + param_3[1] * param_3[1];
        if (FLOAT_80615a48 < fVar2) {
            float fVar3 = 1.0f / vu_sqrtf(fVar2);
            fVar3 = fVar3 * -(FLOAT_80615a4c * fVar2 * fVar3 * fVar3 - FLOAT_80615a50);
            float scale = FLOAT_80615a54 * fVar3 * -(FLOAT_80615a4c * fVar2 * fVar3 * fVar3 - FLOAT_80615a50);
            param_3[0] = fVar1 * scale;
            param_3[1] = param_3[1] * scale;
            param_3[2] = param_3[2] * scale;
        }
    }

    float fVar3 = FLOAT_80615a4c;
    float fx = param_3[0];
    float fy = param_3[1];
    if (ABS(fx) < ABS(fy)) {
        float fz = param_3[2];
        param_1[0] = FLOAT_80615a58;
        float len = fy * fy + fz * fz;
        float rs = 1.0f / vu_sqrtf(len);
        fVar3 = fVar3 * len;
        rs = rs * -(fVar3 * rs * rs - FLOAT_80615a50);
        float scale = FLOAT_80615a54 * rs * -(fVar3 * rs * rs - FLOAT_80615a50);
        param_1[1] = param_3[2] * scale;
        param_1[2] = -param_3[1] * scale;
    } else {
        float fz = param_3[2];
        float len = fx * fx + fz * fz;
        float rs = 1.0f / vu_sqrtf(len);
        len = FLOAT_80615a4c * len;
        rs = rs * -(len * rs * rs - FLOAT_80615a50);
        float scale = FLOAT_80615a54 * rs * -(len * rs * rs - FLOAT_80615a50);
        param_1[0] = -fz * scale;
        param_1[1] = FLOAT_80615a58;
        param_1[2] = param_3[0] * scale;
    }

    // param_2 = param_3 x param_1
    param_2[0] = param_3[1] * param_1[2] - param_3[2] * param_1[1];
    param_2[1] = param_3[2] * param_1[0] - param_3[0] * param_1[2];
    param_2[2] = param_3[0] * param_1[1] - param_3[1] * param_1[0];
}

// ----------------------------------------------------------------------------
// point_in_quad_edge_mask (0x8012ee40)
// 2D point (px,py) vs quad corners (v0..v3). Returns a 4-bit mask: bit N set if
// the point is on the positive (CCW) side of edge N. The point (v4) arrives on
// the stack in the original; here it is passed explicitly as px/py.
// Corners: (param_1,param_2)=v0? -- faithful to the raw arg packing below.
// ----------------------------------------------------------------------------
unsigned char point_in_quad_edge_mask(double param_1, double param_2, double param_3, double param_4,
                                      double param_5, double param_6, double param_7, double param_8,
                                      float px, float py)
{
    double dVar2 = (double)px;
    double dVar1 = (double)py;

    unsigned char e0 = (FLOAT_80615a58 <
            (float)(param_1 - dVar2) * -(float)(param_4 - dVar1) +
            (float)(param_2 - dVar1) * (float)(param_3 - dVar2)) ? 1 : 0;
    unsigned char e1 = (FLOAT_80615a58 <
            (float)(param_1 - param_7) * -(float)(dVar1 - param_8) +
            (float)(param_2 - param_8) * (float)(dVar2 - param_7)) ? 1 : 0;
    unsigned char e2 = (FLOAT_80615a58 <
            (float)(param_1 - param_5) * -(float)(param_8 - param_6) +
            (float)(param_2 - param_6) * (float)(param_7 - param_5)) ? 1 : 0;
    unsigned char e3 = (FLOAT_80615a58 <
            (float)(param_1 - param_3) * -(float)(param_6 - param_4) +
            (float)(param_2 - param_4) * (float)(param_5 - param_3)) ? 1 : 0;

    return (unsigned char)(e0 | (e1 << 1) | (e2 << 2) | (e3 << 3));
}

// ----------------------------------------------------------------------------
// scalar_step_toward_snap (0x8012edfc)
// Move *param_3 toward target param_1 by at most step param_2; snap + return 1
// when within a step, else step and return 0.
// ----------------------------------------------------------------------------
int scalar_step_toward_snap(double param_1, double param_2, float* param_3)
{
    double dVar1 = (double)*param_3;
    if (ABS((double)(float)(param_1 - dVar1)) < param_2) {
        *param_3 = (float)param_1;
        return 1;
    }
    if (param_1 <= dVar1) {
        *param_3 = (float)(dVar1 - param_2);
    } else {
        *param_3 = (float)(dVar1 + param_2);
    }
    return 0;
}

// ----------------------------------------------------------------------------
// angle_step_toward_clamped (0x8012ec48)
// Returns the wrapped delta to move current angle param_1 toward target param_2,
// clamped so the result stays within [param_3, param_4]. Returns 0 when the move
// is negligible or the endpoints are outside the bounds.
// ----------------------------------------------------------------------------
double angle_step_toward_clamped(double param_1, double param_2, double param_3, double param_4)
{
    double dVar3 = (double)(float)(param_2 - param_1);
    if (((ABS(dVar3) < (double)FLOAT_80615a5c) || (param_1 < param_3)) || (param_4 < param_1) ||
        (param_2 < param_3) || (param_4 < param_2)) {
        return (double)FLOAT_80615a58;
    }

    // wrap delta into (-180, 180]
    for (; dVar3 <= (double)FLOAT_80615a64; dVar3 = (double)(float)(dVar3 + (double)FLOAT_80615a60)) {}
    for (; (double)FLOAT_80615a68 < dVar3; dVar3 = (double)(float)(dVar3 - (double)FLOAT_80615a60)) {}

    double dVar2 = (double)(float)((double)FLOAT_80615a60 + dVar3);
    double dVar1 = FUN_80390324((double)(float)((double)(float)(dVar2 - (double)FLOAT_80615a64) /
                                                (double)FLOAT_80615a60));
    dVar1 = -(double)(float)((double)FLOAT_80615a60 * (double)(float)dVar1 - dVar2);

    if ((double)FLOAT_80615a58 <= dVar3) {
        if ((param_4 < (double)(float)(param_1 + dVar3)) &&
            (((double)(float)(param_1 + dVar1) <= param_3) ||
             (dVar3 = dVar1, param_4 <= (double)(float)(param_1 + dVar1)))) {
            dVar1 = (double)(float)(param_4 - param_1);
            dVar3 = FUN_80390324((double)((float)(dVar1 - (double)FLOAT_80615a64) / FLOAT_80615a60));
            dVar3 = -(double)(float)((double)FLOAT_80615a60 * (double)(float)dVar3 - dVar1);
        }
    } else if (((double)(float)(param_1 + dVar3) < param_3) &&
               (((double)(float)(param_1 + dVar1) <= param_3) ||
                (dVar3 = dVar1, param_4 <= (double)(float)(param_1 + dVar1)))) {
        dVar1 = (double)(float)(param_3 - param_1);
        dVar3 = FUN_80390324((double)((float)(dVar1 - (double)FLOAT_80615a64) / FLOAT_80615a60));
        dVar3 = -(double)(float)((double)FLOAT_80615a60 * (double)(float)dVar3 - dVar1);
    }
    return dVar3;
}
