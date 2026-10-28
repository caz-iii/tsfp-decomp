// Address: 0x8038d110 | Name: msl_atan_kernel | Date: 2026-09-13
// Address: 0x8038d34c | Name: msl_asin_kernel | Date: 2026-09-13
//
// Metrowerks Standard Library (MSL) transcendental kernels. Both evaluate a
// rational (P/Q) minimax polynomial in the argument's square, using the packed
// IEEE-754 exponent field of the input to branch between the small-argument
// Taylor region, the mid region, and the near-1 / >1 tail. The DOUBLE_8061ba**
// and DOUBLE_8061bab**..bb** globals are the shared coefficient tables; they are
// read-only and keep their raw symbol names. FUN_80390df4 is the sqrt helper.
//
// Non-matching decomp: bit-manipulation on the double is reproduced through
// unsigned 64-bit reinterpretation; branch structure preserved verbatim.

#include "common.h"

extern double DAT_80611378;   // NaN payload

// atan/asinh coefficient + control constants
extern double DOUBLE_8061ba08, DOUBLE_8061ba10, DOUBLE_8061ba18, DOUBLE_8061ba20;
extern double DOUBLE_8061ba28, DOUBLE_8061ba30, DOUBLE_8061ba38, DOUBLE_8061ba40;
extern double DOUBLE_8061ba48, DOUBLE_8061ba50, DOUBLE_8061ba58, DOUBLE_8061ba60;
extern double DOUBLE_8061ba68, DOUBLE_8061ba70, DOUBLE_8061ba78, DOUBLE_8061ba80;
extern double DOUBLE_8061ba88, DOUBLE_8061ba90, DOUBLE_8061ba98;

// asin coefficient + control constants
extern double DOUBLE_8061baa0, DOUBLE_8061baa8, DOUBLE_8061bab0, DOUBLE_8061bab8;
extern double DOUBLE_8061bac0, DOUBLE_8061bac8, DOUBLE_8061bad0, DOUBLE_8061bad8;
extern double DOUBLE_8061bae0, DOUBLE_8061bae8, DOUBLE_8061baf0, DOUBLE_8061baf8;
extern double DOUBLE_8061bb00, DOUBLE_8061bb08, DOUBLE_8061bb10, DOUBLE_8061bb18, DOUBLE_8061bb20;

extern double FUN_80390df4(double x); // sqrt kernel

// ---------------------------------------------------------------------------
// msl_atan_kernel (0x8038d110) - rational-polynomial atan/asinh-family kernel.
// Splits on the high 31 bits of the argument's exponent: tiny args return the
// leading Taylor term, mid args evaluate P/Q, and the near/over-1 tail uses the
// sqrt helper with a Newton correction.
double msl_atan_kernel(double param_1)
{
    double dVar1;
    double dVar2;
    unsigned int local_28;
    int iStack_24;
    double local_20;

    unsigned long long bits = (unsigned long long)param_1;
    local_28 = (unsigned int)(bits >> 0x20);
    local_28 = local_28 & 0x7fffffff;
    if (local_28 < 0x3ff00000) {
        if (local_28 < 0x3fe00000) {
            dVar1 = DOUBLE_8061ba28;
            if (0x3c600000 < local_28) {
                dVar1 = param_1 * param_1;
                dVar1 = DOUBLE_8061ba28 -
                        (param_1 -
                         -(param_1 * ((dVar1 * (dVar1 * (dVar1 * (dVar1 * (dVar1 * (DOUBLE_8061ba60 * dVar1 +
                                                                                   DOUBLE_8061ba58) +
                                                                          DOUBLE_8061ba50) + DOUBLE_8061ba48) +
                                                        DOUBLE_8061ba40) + DOUBLE_8061ba38)) /
                                      (dVar1 * (dVar1 * (dVar1 * (DOUBLE_8061ba88 * dVar1 + DOUBLE_8061ba80) +
                                                         DOUBLE_8061ba78) + DOUBLE_8061ba70) + DOUBLE_8061ba68)) -
                          DOUBLE_8061ba30));
            }
        } else if ((long long)bits < 0) {
            dVar2 = DOUBLE_8061ba90 * (DOUBLE_8061ba68 + param_1);
            dVar1 = FUN_80390df4(dVar2);
            dVar1 = -(DOUBLE_8061ba98 *
                      (dVar1 + (((dVar2 * (dVar2 * (dVar2 * (dVar2 * (dVar2 * (DOUBLE_8061ba60 * dVar2 +
                                                                              DOUBLE_8061ba58) +
                                                                     DOUBLE_8061ba50) + DOUBLE_8061ba48) +
                                          DOUBLE_8061ba40) + DOUBLE_8061ba38)) /
                                (dVar2 * (dVar2 * (dVar2 * (DOUBLE_8061ba88 * dVar2 + DOUBLE_8061ba80) +
                                                   DOUBLE_8061ba78) + DOUBLE_8061ba70) + DOUBLE_8061ba68)) *
                           dVar1 - DOUBLE_8061ba30)) - DOUBLE_8061ba20);
        } else {
            dVar2 = DOUBLE_8061ba90 * (DOUBLE_8061ba68 - param_1);
            dVar1 = FUN_80390df4(dVar2);
            local_20 = (double)((unsigned long long)dVar1 & 0xffffffff00000000ULL);
            dVar1 = DOUBLE_8061ba98 *
                    (local_20 +
                     ((dVar2 * (dVar2 * (dVar2 * (dVar2 * (dVar2 * (DOUBLE_8061ba60 * dVar2 +
                                                                   DOUBLE_8061ba58) + DOUBLE_8061ba50) +
                                                 DOUBLE_8061ba48) + DOUBLE_8061ba40) + DOUBLE_8061ba38)) /
                      (dVar2 * (dVar2 * (dVar2 * (DOUBLE_8061ba88 * dVar2 + DOUBLE_8061ba80) +
                                         DOUBLE_8061ba78) + DOUBLE_8061ba70) + DOUBLE_8061ba68)) * dVar1 +
                     -(local_20 * local_20 - dVar2) / (dVar1 + local_20));
        }
    } else {
        iStack_24 = (int)bits;
        if (local_28 == 0x3ff00000 && iStack_24 == 0) {
            dVar1 = DOUBLE_8061ba20;
            if (0xffffffff < (long long)bits) {
                dVar1 = DOUBLE_8061ba18;
            }
        } else {
            dVar1 = DAT_80611378;
        }
    }
    return dVar1;
}

// ---------------------------------------------------------------------------
// msl_asin_kernel (0x8038d34c) - rational-polynomial asin-family kernel. Tiny args
// short-circuit to the identity, mid args evaluate the P/Q ratio times the
// argument, and near-1 args reduce with sqrt(1-|x|) plus a Newton refinement.
double msl_asin_kernel(double param_1)
{
    double dVar1;
    double dVar2;
    double dVar3;
    double in_f31;
    double dVar4;
    unsigned int local_48;
    int iStack_44;
    double local_40;

    unsigned long long bits = (unsigned long long)param_1;
    local_48 = (unsigned int)(bits >> 0x20);
    local_48 = local_48 & 0x7fffffff;
    in_f31 = 0.0;
    if (local_48 < 0x3ff00000) {
        if (local_48 < 0x3fe00000) {
            if (local_48 < 0x3e400000) {
                if (DOUBLE_8061bab8 < DOUBLE_8061bab0 + param_1) {
                    return param_1;
                }
            } else {
                in_f31 = param_1 * param_1;
            }
            dVar1 = param_1 * ((in_f31 * (in_f31 * (in_f31 * (in_f31 * (in_f31 * (DOUBLE_8061bae8 * in_f31 +
                                                                                 DOUBLE_8061bae0) +
                                                                       DOUBLE_8061bad8) + DOUBLE_8061bad0) +
                                          DOUBLE_8061bac8) + DOUBLE_8061bac0)) /
                              (in_f31 * (in_f31 * (in_f31 * (DOUBLE_8061bb08 * in_f31 + DOUBLE_8061bb00) +
                                                   DOUBLE_8061baf8) + DOUBLE_8061baf0) + DOUBLE_8061bab8)) +
                    param_1;
        } else {
            dVar4 = DOUBLE_8061bb10 * (DOUBLE_8061bab8 - ABS(param_1));
            dVar2 = dVar4 * (dVar4 * (dVar4 * (DOUBLE_8061bb08 * dVar4 + DOUBLE_8061bb00) +
                                      DOUBLE_8061baf8) + DOUBLE_8061baf0) + DOUBLE_8061bab8;
            dVar3 = dVar4 * (dVar4 * (dVar4 * (dVar4 * (dVar4 * (DOUBLE_8061bae8 * dVar4 + DOUBLE_8061bae0) +
                                                        DOUBLE_8061bad8) + DOUBLE_8061bad0) +
                                      DOUBLE_8061bac8) + DOUBLE_8061bac0);
            dVar1 = FUN_80390df4(dVar4);
            if (local_48 < 0x3fef3333) {
                local_40 = (double)((unsigned long long)dVar1 & 0xffffffff00000000ULL);
                dVar1 = DOUBLE_8061bb20 -
                        ((DOUBLE_8061bb18 * dVar1 * (dVar3 / dVar2) -
                          -(DOUBLE_8061bb18 * (-(local_40 * local_40 - dVar4) / (dVar1 + local_40)) -
                            DOUBLE_8061baa8)) - -(DOUBLE_8061bb18 * local_40 - DOUBLE_8061bb20));
            } else {
                dVar1 = DOUBLE_8061baa0 -
                        (DOUBLE_8061bb18 * (dVar1 * (dVar3 / dVar2) + dVar1) - DOUBLE_8061baa8);
            }
            if ((long long)bits < 0x100000000LL) {
                dVar1 = -dVar1;
            }
        }
    } else {
        iStack_44 = (int)bits;
        if (local_48 == 0x3ff00000 && iStack_44 == 0) {
            dVar1 = DOUBLE_8061baa0 * param_1 + DOUBLE_8061baa8 * param_1;
        } else {
            dVar1 = DAT_80611378;
        }
    }
    return dVar1;
}
