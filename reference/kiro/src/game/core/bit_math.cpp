// Batch 350 | Date: 2026-09-04
// Small bit / scalar-math utilities.
//
//   popcount32     (0x8012e97c) 32-bit population count (SWAR)
//   ceil_log2_u32  (0x8012e924) ceil(log2(n)) via float-exponent trick
//   max3f          (0x8012ea1c) max of three floats
//   min3f          (0x8012e9ec) min of three floats
//
// Subsystem: game (core math). Compile-Clean Standard.

#include "common.h"

// ----------------------------------------------------------------------------
// popcount32 (0x8012e97c)
// ----------------------------------------------------------------------------
int popcount32(unsigned int param_1)
{
    unsigned int uVar1;
    uVar1 = ((param_1 >> 1) & 0x55555555) + (param_1 & 0x55555555);
    uVar1 = ((uVar1 >> 2) & 0x33333333) + (uVar1 & 0x33333333);
    uVar1 = ((uVar1 >> 4) & 0x0f0f0f0f) + (uVar1 & 0x0f0f0f0f);
    uVar1 = ((uVar1 >> 8) & 0x00ff00ff) + (uVar1 & 0x00ff00ff);
    return (int)((uVar1 >> 0x10) + (uVar1 & 0xffff));
}

// ----------------------------------------------------------------------------
// ceil_log2_u32 (0x8012e924)
// Returns ceil(log2(n)) for n>0 via the IEEE-754 float exponent; 0 for n==0.
// ----------------------------------------------------------------------------
int ceil_log2_u32(int param_1)
{
    int iVar2 = 0;
    if (param_1 != 0) {
        // (float)param_1, then take exponent field (bits >> 23)
        float f = (float)((double)param_1);
        union { float ff; unsigned int ii; } u; u.ff = f;
        unsigned int uVar1 = u.ii >> 0x17;
        iVar2 = (int)uVar1 - 0x7f;
        if ((1 << iVar2) != param_1) {
            iVar2 = (int)uVar1 - 0x7e;
        }
    }
    return iVar2;
}

// ----------------------------------------------------------------------------
// max3f (0x8012ea1c)
// ----------------------------------------------------------------------------
double max3f(double param_1, double param_2, double param_3)
{
    if (param_1 <= param_2) {
        if (param_3 < param_2) {
            return param_2;
        }
        return param_3;
    }
    if (param_3 < param_1) {
        return param_1;
    }
    return param_3;
}

// ----------------------------------------------------------------------------
// min3f (0x8012e9ec)
// ----------------------------------------------------------------------------
double min3f(double param_1, double param_2, double param_3)
{
    if (param_2 <= param_1) {
        if (param_2 < param_3) {
            return param_2;
        }
        return param_3;
    }
    if (param_1 < param_3) {
        return param_1;
    }
    return param_3;
}
