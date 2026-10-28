// TSFP GameCube VEC 3-vector math (from main.dol, PPC/Gekko).
//
// Address: 0x803a160c | Name: vec_normalize | Date: 2026-09-15
//
// VECNormalize: normalize a 3-component vector src -> dst.
//
// The original computes the reciprocal square root with the PPC frsqrte hardware
// estimate refined by one Newton-Raphson iteration:
//   e0 = frsqrte(len2)                 (hardware ~12-bit estimate of 1/sqrt)
//   e  = -(e0*e0*len2 - 3.0) * e0 * 0.5   (one NR step; 0.5=8061c010, 3.0=8061c014)
// then scales each component by e.
//
// NON-MATCHING: frsqrte has no portable equivalent, so the initial estimate is
// produced by the classic fast-inverse-sqrt bit hack and then refined with the
// SAME Newton-Raphson step the original uses. The per-component scaling and the
// len2 computation are transcribed verbatim.

#include "common.h"

extern "C" {

// Portable stand-in for the PPC frsqrte reciprocal-sqrt seed.
static float rsqrt_estimate(float x)
{
    union { float f; unsigned int i; } u;
    u.f = x;
    u.i = 0x5f3759dfu - (u.i >> 1);   // fast inverse sqrt seed
    return u.f;
}

void vec_normalize(float *src, float *dst)
{
    float x = src[0];
    float y = src[1];
    float z = src[2];
    float len2 = x * x + y * y + z * z;

    float e = rsqrt_estimate(len2);
    // One Newton-Raphson refinement, matching the original's constants:
    //   e = -(e*e*len2 - 3.0) * e * 0.5
    e = -(e * e * len2 - 3.0f) * e * 0.5f;

    dst[0] = x * e;
    dst[1] = y * e;
    dst[2] = z * e;
}

} // extern "C"
