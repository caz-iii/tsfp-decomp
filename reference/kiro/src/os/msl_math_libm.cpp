// Address: 0x8038d584 | Name: msl_atan2      | Date: 2026-09-13
// Address: 0x8038d814 | Name: msl_exp        | Date: 2026-09-13
// Address: 0x8038da38 | Name: msl_fmod       | Date: 2026-09-13
// Address: 0x8038dd74 | Name: msl_log        | Date: 2026-09-13
// Address: 0x8038dff0 | Name: msl_log10      | Date: 2026-09-13
// Address: 0x8038e100 | Name: msl_pow        | Date: 2026-09-13
// Address: 0x8038e930 | Name: msl_rem_pio2   | Date: 2026-09-13
// Address: 0x8038ecd0 | Name: msl_kernel_sin | Date: 2026-09-13
// Address: 0x8038edc4 | Name: msl_kernel_rem_pio2 | Date: 2026-09-13
// Address: 0x8038fc18 | Name: msl_kernel_cos | Date: 2026-09-13
//
// Metrowerks Standard Library (MSL) libm core - the fdlibm-derived double-precision
// transcendental suite: atan2, exp, fmod, log, log10, pow, plus the trig argument-
// reduction chain (rem_pio2 -> kernel_rem_pio2) and the sin/cos polynomial kernels.
// All of these decompose the IEEE-754 double through its packed high/low 32-bit
// words to extract exponent/mantissa; that bit surgery is reproduced here through
// unsigned 64-bit reinterpretation so it compiles on the x64 host while preserving
// the exact branch structure. The DOUBLE_8061bb**..be** globals are the shared
// read-only coefficient/threshold tables and keep their raw symbol names, as do the
// DAT_803e1***-based lookup tables. Helper kernels FUN_8038fecc (atan),
// FUN_80390df4 (sqrt), FUN_803904f8 (scalbn/ldexp), FUN_80390324 (nearbyint) are
// declared extern.
//
// Non-matching decomp: the goal is a compiling, behaviourally faithful port, not a
// byte match. Register-pair temporaries the decompiler surfaced (SUB84 / CONCAT44
// halves) are expressed with explicit unsigned reinterpretation helpers.

#include "common.h"
#include <cstring>

// --- helpers to move between double and its 64-bit / 32-bit-word views --------
static inline unsigned long long d2u(double d)   { unsigned long long u; memcpy(&u, &d, 8); return u; }
static inline double            u2d(unsigned long long u) { double d; memcpy(&d, &u, 8); return d; }
static inline unsigned int      hiword(double d) { return (unsigned int)(d2u(d) >> 32); }
static inline unsigned int      loword(double d) { return (unsigned int)(d2u(d) & 0xffffffffu); }
static inline double            mkdouble(unsigned int hi, unsigned int lo) {
    return u2d(((unsigned long long)hi << 32) | lo);
}

// --- shared coefficient / threshold tables (read-only) ------------------------
extern double DOUBLE_8061bb28, DOUBLE_8061bb30, DOUBLE_8061bb38, DOUBLE_8061bb40;
extern double DOUBLE_8061bb48, DOUBLE_8061bb50, DOUBLE_8061bb58, DOUBLE_8061bb60;
extern double DOUBLE_8061bb68, DOUBLE_8061bb70, DOUBLE_8061bb78;
extern double DOUBLE_8061bb80, DOUBLE_8061bb88, DOUBLE_8061bb90, DOUBLE_8061bb98;
extern double DOUBLE_8061bba0, DOUBLE_8061bba8, DOUBLE_8061bbb0, DOUBLE_8061bbb8;
extern double DOUBLE_8061bbc0, DOUBLE_8061bbc8, DOUBLE_8061bbd0, DOUBLE_8061bbd8;
extern double DOUBLE_8061bbe0, DOUBLE_8061bbe8, DOUBLE_8061bbf0, DOUBLE_8061bbf8;
extern double DOUBLE_8061bc00, DOUBLE_8061bc08, DOUBLE_8061bc10, DOUBLE_8061bc18;
extern double DOUBLE_8061bc20, DOUBLE_8061bc28, DOUBLE_8061bc30, DOUBLE_8061bc38;
extern double DOUBLE_8061bc40, DOUBLE_8061bc48, DOUBLE_8061bc50, DOUBLE_8061bc58;
extern double DOUBLE_8061bc60, DOUBLE_8061bc68, DOUBLE_8061bc70, DOUBLE_8061bc78;
extern double DOUBLE_8061bc80, DOUBLE_8061bc88, DOUBLE_8061bc90, DOUBLE_8061bc98;
extern double DOUBLE_8061bca0, DOUBLE_8061bca8, DOUBLE_8061bcb0, DOUBLE_8061bcb8;
extern double DOUBLE_8061bcc0, DOUBLE_8061bcc8, DOUBLE_8061bcd0, DOUBLE_8061bcd8;
extern double DOUBLE_8061bce0, DOUBLE_8061bce8, DOUBLE_8061bcf0, DOUBLE_8061bcf8;
extern double DOUBLE_8061bd00, DOUBLE_8061bd08, DOUBLE_8061bd10, DOUBLE_8061bd18;
extern double DOUBLE_8061bd20, DOUBLE_8061bd28, DOUBLE_8061bd30, DOUBLE_8061bd38;
extern double DOUBLE_8061bd40, DOUBLE_8061bd48, DOUBLE_8061bd50, DOUBLE_8061bd58;
extern double DOUBLE_8061bd60, DOUBLE_8061bd68, DOUBLE_8061bd70, DOUBLE_8061bd78;
extern double DOUBLE_8061bd80, DOUBLE_8061bd88, DOUBLE_8061bd90, DOUBLE_8061bd98;
extern double DOUBLE_8061bda0, DOUBLE_8061bda8, DOUBLE_8061bdb0, DOUBLE_8061bdb8;
extern double DOUBLE_8061bdc0, DOUBLE_8061bdc8, DOUBLE_8061bdd0, DOUBLE_8061bdd8;
extern double DOUBLE_8061bde0, DOUBLE_8061bde8, DOUBLE_8061bdf0, DOUBLE_8061bdf8;
extern double DOUBLE_8061be00, DOUBLE_8061be08, DOUBLE_8061be10, DOUBLE_8061be18;
extern double DOUBLE_8061be20, DOUBLE_8061be28, DOUBLE_8061be30, DOUBLE_8061be38;
extern double DOUBLE_8061be40, DOUBLE_8061be48, DOUBLE_8061be50, DOUBLE_8061be58;
extern double DOUBLE_8061be60, DOUBLE_8061be68, DOUBLE_8061be70, DOUBLE_8061be78;
extern double DOUBLE_8061be80, DOUBLE_8061be88, DOUBLE_8061be90, DOUBLE_8061be98;
extern double DOUBLE_8061bea0, DOUBLE_8061bea8, DOUBLE_8061beb0, DOUBLE_8061beb8;
extern double DOUBLE_8061bec0, DOUBLE_8061bec8;
extern double DOUBLE_80612ff8, DOUBLE_80613000;
extern double DAT_80611378;         // NaN payload
extern int    DAT_80612fe8;         // errno slot

// small per-index lookup tables addressed by pointer arithmetic in the original
extern unsigned char DAT_803e12f0[]; // pow: 2^k reciprocal table base
extern unsigned char DAT_803e1300[]; // exp: hi/lo split constants
extern unsigned char DAT_803e1310[];
extern unsigned char DAT_803e1320[]; // fmod: signed-zero table
extern unsigned char DAT_803e1330[]; // pow: sqrt-region constants
extern unsigned char DAT_803e1340[];
extern unsigned char DAT_803e1350[];
extern unsigned char DAT_803e1464[]; // rem_pio2: exponent match table
extern double        DAT_803e14f8[]; // kernel_rem_pio2: PIo2[] array
extern unsigned char DAT_803e14e8[]; // kernel_rem_pio2: nbits-per-precision table

// helper kernels
extern double FUN_8038fecc(double x);            // atan
extern double FUN_80390df4(double x);            // sqrt
extern double FUN_803904f8(double x, int n);     // scalbn / ldexp
extern double FUN_80390324(double x);            // round-to-nearest-int (double)
extern "C" unsigned long long __save_fpr_25(void);
extern "C" void __restore_fpr_25(void);

// forward decl: msl_rem_pio2 (below) calls the multi-precision reduction
int msl_kernel_rem_pio2(void *param_1, void *param_2, int param_3, int param_4, int param_5,
                        int param_6);

// ---------------------------------------------------------------------------
// msl_atan2 (0x8038d584) - two-argument arctangent. Handles the NaN/Inf matrix and
// the four quadrants via the sign bits, delegating the finite case to atan(|y/x|)
// with the quadrant offset applied from the shared angle-constant table.
double msl_atan2(double param_1, double param_2)
{
    unsigned int uVar1, uVar2, uVar5;
    int iVar4;
    double dVar3;
    unsigned int local_28, uStack_24, local_20, uStack_1c;
    double local_18 = 0.0;

    uStack_1c = loword(param_2);
    local_20 = hiword(param_2);
    uVar1 = local_20 & 0x7fffffff;
    local_28 = hiword(param_1);
    uStack_24 = loword(param_1);
    uVar2 = local_28 & 0x7fffffff;
    if (((uVar1 | ((uStack_1c | (unsigned int)(-(int)uStack_1c)) >> 0x1f)) < 0x7ff00001) &&
        ((uVar2 | ((uStack_24 | (unsigned int)(-(int)uStack_24)) >> 0x1f)) < 0x7ff00001)) {
        if (param_2 == 1.0) {
            local_18 = FUN_8038fecc(param_1);
        } else {
            uVar5 = (local_28 >> 0x1f) | ((local_20 >> 0x1e) & 2);
            if (((d2u(param_1) & 0x7fffffff00000000ULL) != 0 || uStack_24 != 0) ||
                (((local_18 = DOUBLE_8061bb28, uVar5 != 2 && (local_18 = param_1, 1 < uVar5)) &&
                  (local_18 = DOUBLE_8061bb30, 3 < uVar5)))) {
                local_18 = DOUBLE_8061bb40;
                if ((d2u(param_2) & 0x7fffffff00000000ULL) == 0 && uStack_1c == 0) {
                    if ((long long)d2u(param_1) < 0) {
                        local_18 = DOUBLE_8061bb38;
                    }
                } else {
                    if (uVar1 == 0x7ff00000) {
                        if (uVar2 == 0x7ff00000) {
                            if (uVar5 == 2) return DOUBLE_8061bb58;
                            dVar3 = DOUBLE_8061bb60;
                            if (uVar5 < 2) {
                                if (uVar5 == 0) return DOUBLE_8061bb48;
                                return DOUBLE_8061bb50;
                            }
                        } else {
                            if (uVar5 == 2) return DOUBLE_8061bb28;
                            dVar3 = DOUBLE_8061bb30;
                            if (uVar5 < 2) {
                                if (uVar5 == 0) return DOUBLE_8061bb68;
                                return DOUBLE_8061bb70;
                            }
                        }
                        if (uVar5 < 4) return dVar3;
                    }
                    if (uVar2 == 0x7ff00000) {
                        if ((long long)d2u(param_1) < 0) {
                            local_18 = DOUBLE_8061bb38;
                        }
                    } else {
                        iVar4 = (int)(uVar2 - uVar1) >> 0x14;
                        if (iVar4 < 0x3d) {
                            if (((long long)d2u(param_2) < 0) && (iVar4 < -0x3c)) {
                                local_18 = DOUBLE_8061bb68;
                            } else {
                                local_18 = FUN_8038fecc(ABS(param_1 / param_2));
                            }
                        }
                        if (uVar5 == 1) {
                            local_18 = -local_18;
                        } else if (uVar5 != 0) {
                            if (uVar5 < 3) {
                                local_18 = DOUBLE_8061bb28 - (local_18 - DOUBLE_8061bb78);
                            } else {
                                local_18 = (local_18 - DOUBLE_8061bb78) - DOUBLE_8061bb28;
                            }
                        }
                    }
                }
            }
        }
    } else {
        local_18 = param_2 + param_1;
    }
    return local_18;
}

// ---------------------------------------------------------------------------
// msl_exp (0x8038d814) - e^x. Overflow/underflow guarded against the packed
// exponent, argument reduced to r = x - k*ln2 (split hi/lo), evaluated with the
// standard rational R(r), then scaled by 2^k.
double msl_exp(double param_1)
{
    unsigned int uVar1;
    int iVar2;
    unsigned int in_r6 = 0;
    double dVar3;
    double in_f7 = 0.0;
    double in_f8 = 0.0;
    unsigned int hi28 = hiword(param_1);
    unsigned int lo28;
    double local_28, local_20;

    uVar1 = hi28 & 0x7fffffff;
    iVar2 = (int)hi28 >> 0x1f;
    if (0x40862e41 < uVar1) {
        if (0x7fefffff < uVar1) {
            lo28 = loword(param_1);
            if ((d2u(param_1) & 0xfffff00000000ULL) != 0 || lo28 != 0) {
                return param_1 + param_1;
            }
            if ((long long)d2u(param_1) < 0) {
                return DOUBLE_8061bb80;
            }
            return param_1;
        }
        if (DOUBLE_8061bb88 < param_1) return DOUBLE_8061bb90;
        if (param_1 < DOUBLE_8061bb98) return DOUBLE_8061bb80;
    }
    if (uVar1 < 0x3fd62e43) {
        local_28 = param_1;
        if (uVar1 < 0x3e300000) {
            if (DOUBLE_8061bbb0 < DOUBLE_8061bba8 + param_1) {
                return DOUBLE_8061bbb0 + param_1;
            }
        } else {
            in_r6 = 0;
        }
    } else {
        if (uVar1 < 0x3ff0a2b2) {
            in_r6 = (unsigned int)(iVar2 * 2 + 1);
            in_f8 = ((double *)DAT_803e1310)[-iVar2];
            in_f7 = param_1 - ((double *)DAT_803e1300)[-iVar2];
        } else {
            in_r6 = (unsigned int)(int)(DOUBLE_8061bba0 * param_1 +
                                        *(double *)(DAT_803e12f0 + iVar2 * -8));
            dVar3 = mkdouble(0x43300000, in_r6 ^ 0x80000000) - DOUBLE_8061bbf0;
            in_f7 = -(dVar3 * ((double *)DAT_803e1300)[0] - param_1);
            in_f8 = dVar3 * ((double *)DAT_803e1310)[0];
        }
        local_28 = in_f7 - in_f8;
    }
    dVar3 = local_28 * local_28;
    dVar3 = -(dVar3 * (dVar3 * (dVar3 * (dVar3 * (DOUBLE_8061bbd8 * dVar3 + DOUBLE_8061bbd0) +
                                         DOUBLE_8061bbc8) + DOUBLE_8061bbc0) + DOUBLE_8061bbb8) -
              local_28);
    if (in_r6 == 0) {
        local_20 = DOUBLE_8061bbb0 - ((local_28 * dVar3) / (dVar3 - DOUBLE_8061bbe0) - local_28);
    } else {
        dVar3 = DOUBLE_8061bbb0 - ((in_f8 - (local_28 * dVar3) / (DOUBLE_8061bbe0 - dVar3)) - in_f7);
        unsigned int lo = loword(dVar3);
        unsigned int hi = hiword(dVar3);
        if ((int)in_r6 < -0x3fd) {
            local_20 = mkdouble(hi + (in_r6 + 1000) * 0x100000, lo);
            local_20 = DOUBLE_8061bbe8 * local_20;
        } else {
            local_20 = mkdouble(hi + in_r6 * 0x100000, lo);
        }
    }
    return local_20;
}

// ---------------------------------------------------------------------------
// msl_fmod (0x8038da38) - floating-point remainder x - n*y (n truncated toward
// zero). Operates entirely on the integer bit patterns: aligns exponents, performs
// the shift-and-subtract remainder loop, then renormalises the result.
double msl_fmod(double param_1, double param_2)
{
    unsigned int uVar1, uVar3, uVar4, uVar6, uVar7;
    int iVar2, iVar5, iVar8;
    unsigned int lo18 = loword(param_1), hi18 = hiword(param_1);
    unsigned int local_10 = hiword(param_2), uStack_c = loword(param_2);

    uVar7 = local_10 & 0x7fffffff;
    uVar1 = hi18 & 0x80000000;
    uVar4 = hi18 ^ uVar1;
    if (((((d2u(param_2) & 0x7fffffff00000000ULL) == 0 && uStack_c == 0) ||
          (0x7fefffff < (int)uVar4)) ||
         (0x7ff00000 < (uVar7 | ((uStack_c | (unsigned int)(-(int)uStack_c)) >> 0x1f))))) {
        return (param_1 * param_2) / (param_1 * param_2);
    }
    if ((int)uVar4 <= (int)uVar7) {
        if ((int)uVar4 < (int)uVar7) return param_1;
        if (lo18 < uStack_c) return param_1;
        if (lo18 == uStack_c) return *(double *)(DAT_803e1320 + (uVar1 >> 0x1c));
    }
    if ((int)uVar4 < 0x100000) {
        if (uVar4 == 0) {
            iVar8 = -0x413;
            for (uVar6 = lo18; 0 < (int)uVar6; uVar6 = uVar6 << 1) iVar8 = iVar8 + -1;
        } else {
            iVar8 = -0x3fe;
            for (iVar2 = (int)(hi18 << 0xb); 0 < iVar2; iVar2 = iVar2 << 1) iVar8 = iVar8 + -1;
        }
    } else {
        iVar8 = ((int)uVar4 >> 0x14) + -0x3ff;
    }
    if (uVar7 < 0x100000) {
        if ((d2u(param_2) & 0x7fffffff00000000ULL) == 0) {
            iVar2 = -0x413;
            for (uVar6 = uStack_c; 0 < (int)uVar6; uVar6 = uVar6 << 1) iVar2 = iVar2 + -1;
        } else {
            iVar2 = -0x3fe;
            for (iVar5 = (int)(local_10 << 0xb); 0 < iVar5; iVar5 = iVar5 << 1) iVar2 = iVar2 + -1;
        }
    } else {
        iVar2 = ((int)uVar7 >> 0x14) + -0x3ff;
    }
    if (iVar8 < -0x3fe) {
        iVar5 = -iVar8 + -0x3fe;
        if (iVar5 < 0x20) {
            uVar6 = lo18 >> (0x20 - iVar5);
            lo18 = lo18 << iVar5;
            uVar6 = (uVar4 << iVar5) | uVar6;
        } else {
            uVar6 = lo18 << (-iVar8 + -0x41e);
            lo18 = 0;
        }
    } else {
        uVar6 = (uVar4 & 0xfffff) | 0x100000;
    }
    if (iVar2 < -0x3fe) {
        iVar5 = -iVar2 + -0x3fe;
        if (iVar5 < 0x20) {
            uVar4 = uStack_c >> (0x20 - iVar5);
            uStack_c = uStack_c << iVar5;
            uVar4 = (uVar7 << iVar5) | uVar4;
        } else {
            uVar4 = uStack_c << (-iVar2 + -0x41e);
            uStack_c = 0;
        }
    } else {
        uVar4 = (local_10 & 0xfffff) | 0x100000;
    }
    iVar8 = iVar8 - iVar2;
    unsigned long long result;
    while (true) {
        if (iVar8 == 0) {
            uVar4 = uVar6 - uVar4;
            if (lo18 < uStack_c) uVar4 = uVar4 - 1;
            if (-1 < (int)uVar4) {
                lo18 = lo18 - uStack_c;
                uVar6 = uVar4;
            }
            if (uVar6 == 0 && lo18 == 0) {
                return *(double *)(DAT_803e1320 + (uVar1 >> 0x1c));
            }
            for (; (int)uVar6 < 0x100000; uVar6 = uVar6 * 2 - iVar8) {
                iVar8 = (int)lo18 >> 0x1f;
                lo18 = lo18 * 2;
                iVar2 = iVar2 + -1;
            }
            if (iVar2 < -0x3fe) {
                uVar7 = (unsigned int)(-iVar2 - 0x3fe);
                if ((int)uVar7 < 0x15) {
                    uVar3 = (lo18 >> uVar7) | (uVar6 << (0x20 - uVar7));
                    uVar4 = (unsigned int)((int)uVar6 >> (uVar7 & 0x3f));
                } else {
                    uVar4 = uVar1;
                    if ((int)uVar7 < 0x20) {
                        uVar3 = (uVar6 << (0x20 - uVar7)) | (lo18 >> uVar7);
                    } else {
                        uVar3 = (unsigned int)((int)uVar6 >> ((unsigned int)(-iVar2 - 0x41e) & 0x3f));
                    }
                }
                result = ((unsigned long long)(uVar4 | uVar1) << 32) | uVar3;
            } else {
                result = ((unsigned long long)((uVar6 - 0x100000) | ((iVar2 + 0x3ff) * 0x100000) | uVar1) << 32) | lo18;
            }
            return u2d(result);
        }
        iVar5 = (int)(uVar6 - uVar4);
        uVar7 = lo18 - uStack_c;
        if (lo18 < uStack_c) iVar5 = iVar5 + -1;
        if (iVar5 < 0) {
            uVar6 = uVar6 * 2 - ((int)lo18 >> 0x1f);
            uVar7 = lo18;
        } else {
            if (iVar5 == 0 && uVar7 == 0) {
                return *(double *)(DAT_803e1320 + (uVar1 >> 0x1c));
            }
            uVar6 = (unsigned int)(iVar5 * 2 - ((int)uVar7 >> 0x1f));
        }
        lo18 = uVar7 * 2;
        iVar8 = iVar8 + -1;
    }
}

// ---------------------------------------------------------------------------
// msl_log (0x8038dd74) - natural log kernel. Normalises the mantissa to [sqrt(1/2),
// sqrt(2)), evaluates the odd rational in s = f/(2+f), and recombines with k*ln2.
// Called directly by msl_log10.
double msl_log(double param_1)
{
    unsigned int uVar1, uVar3;
    int iVar2 = 0;
    double dVar4, dVar5, dVar6, dVar7;
    unsigned int hi18 = hiword(param_1), lo18 = loword(param_1);
    uVar3 = hi18;
    dVar4 = param_1;

    if ((long long)d2u(param_1) < 0x10000000000000LL) {
        if ((d2u(param_1) & 0x7fffffff00000000ULL) == 0 && lo18 == 0) {
            return DOUBLE_8061bbf8 / DOUBLE_80612ff8;
        }
        if ((long long)d2u(param_1) < 0) {
            DAT_80612fe8 = 0x21;
            return (param_1 - param_1) / DOUBLE_80612ff8;
        }
        iVar2 = -0x36;
        dVar4 = param_1 * DOUBLE_8061bc00;
        uVar3 = hiword(dVar4);
    }
    double local_18 = dVar4;
    if ((int)uVar3 < 0x7ff00000) {
        uVar1 = uVar3 & 0xfffff;
        local_18 = mkdouble((uVar1 | ((uVar1 + 0x95f64) & 0x100000) ^ 0x3ff00000), loword(local_18));
        uVar3 = (unsigned int)(((int)uVar3 >> 0x14) + iVar2 + -0x3ff + (int)((uVar1 + 0x95f64) >> 0x14));
        local_18 = local_18 - DOUBLE_8061bc08;
        if (((uVar1 + 2) & 0xfffff) < 3) {
            if (local_18 == DOUBLE_80612ff8) {
                dVar4 = DOUBLE_80612ff8;
                if (uVar3 != 0) {
                    dVar4 = mkdouble(0x43300000, uVar3 ^ 0x80000000) - DOUBLE_8061bc70;
                    dVar4 = DOUBLE_8061bc10 * dVar4 + DOUBLE_8061bc18 * dVar4;
                }
            } else {
                dVar4 = -(DOUBLE_8061bc28 * local_18 - DOUBLE_8061bc20) * local_18 * local_18;
                if (uVar3 == 0) {
                    dVar4 = local_18 - dVar4;
                } else {
                    dVar5 = mkdouble(0x43300000, uVar3 ^ 0x80000000) - DOUBLE_8061bc70;
                    dVar4 = DOUBLE_8061bc10 * dVar5 - (-(DOUBLE_8061bc18 * dVar5 - dVar4) - local_18);
                }
            }
        } else {
            dVar4 = local_18 / (DOUBLE_8061bc30 + local_18);
            dVar5 = dVar4 * dVar4;
            dVar7 = dVar5 * dVar5;
            dVar6 = mkdouble(0x43300000, uVar3 ^ 0x80000000) - DOUBLE_8061bc70;
            dVar5 = dVar5 * (dVar7 * (dVar7 * (DOUBLE_8061bc50 * dVar7 + DOUBLE_8061bc48) +
                                      DOUBLE_8061bc40) + DOUBLE_8061bc38) +
                    dVar7 * (dVar7 * (DOUBLE_8061bc68 * dVar7 + DOUBLE_8061bc60) + DOUBLE_8061bc58);
            if ((int)((uVar1 - 0x6147a) | (0x6b851 - uVar1)) < 1) {
                if (uVar3 == 0) {
                    dVar4 = -(dVar4 * (local_18 - dVar5) - local_18);
                } else {
                    dVar4 = DOUBLE_8061bc10 * dVar6 -
                            ((dVar4 * (local_18 - dVar5) - DOUBLE_8061bc18 * dVar6) - local_18);
                }
            } else {
                dVar7 = DOUBLE_8061bc20 * local_18 * local_18;
                if (uVar3 == 0) {
                    dVar4 = local_18 - -(dVar4 * (dVar7 + dVar5) - dVar7);
                } else {
                    dVar4 = DOUBLE_8061bc10 * dVar6 -
                            ((dVar7 - (dVar4 * (dVar7 + dVar5) + DOUBLE_8061bc18 * dVar6)) - local_18);
                }
            }
        }
    } else {
        dVar4 = local_18 + local_18;
    }
    return dVar4;
}

// ---------------------------------------------------------------------------
// msl_log10 (0x8038dff0) - base-10 log. Splits x = 2^k * m, computes log(m) via
// msl_log, and recombines using log10(2) and log10(e) split constants.
double msl_log10(double param_1)
{
    int iVar1 = 0, iVar2;
    unsigned int uVar3;
    double dVar4, dVar5;
    unsigned int hi18 = hiword(param_1), lo18 = loword(param_1);
    uVar3 = hi18;
    dVar4 = param_1;

    if ((long long)d2u(param_1) < 0x10000000000000LL) {
        if ((d2u(param_1) & 0x7fffffff00000000ULL) == 0 && lo18 == 0) {
            DAT_80612fe8 = 0x21;
            return DOUBLE_8061bc78 / DOUBLE_80613000;
        }
        if ((long long)d2u(param_1) < 0) {
            DAT_80612fe8 = 0x21;
            return (param_1 - param_1) / DOUBLE_80613000;
        }
        iVar1 = -0x36;
        dVar4 = param_1 * DOUBLE_8061bc80;
        uVar3 = hiword(dVar4);
    }
    double local_18 = dVar4;
    if ((int)uVar3 < 0x7ff00000) {
        iVar2 = ((int)uVar3 >> 0x14) + iVar1 + -0x3ff;
        iVar1 = iVar2 >> 0x1f;
        local_18 = mkdouble((uVar3 & 0xfffff) | ((iVar1 + 0x3ff) * 0x100000), loword(local_18));
        dVar5 = mkdouble(0x43300000, (iVar2 - iVar1) ^ 0x80000000) - DOUBLE_8061bca0;
        dVar4 = msl_log(local_18);
        local_18 = DOUBLE_8061bc98 * dVar5 + DOUBLE_8061bc88 * dVar5 + DOUBLE_8061bc90 * dVar4;
    } else {
        local_18 = local_18 + local_18;
    }
    return local_18;
}

// ---------------------------------------------------------------------------
// msl_pow (0x8038e100) - x^y. Full fdlibm pow: handles the special-value matrix,
// determines whether y is an odd/even integer for sign handling, computes
// log2(|x|) in extended precision, multiplies by y, and exponentiates 2^(that),
// scaling with scalbn (FUN_803904f8) and sqrt (FUN_80390df4).
double msl_pow(double param_1, double param_2)
{
    unsigned int uVar1w;
    int iVar2;
    unsigned int uVar3, uVar4, uVar6;
    int iVar5, iVar7;
    double dVar8, dVar9, dVar10, dVar11;
    unsigned int local_a8, local_a0, uStack_9c;
    int iStack_a4;
    double local_98, local_90, local_88, local_80, local_78, local_70, local_68;
    double local_60;

    local_a0 = hiword(param_2);
    uStack_9c = loword(param_2);
    uVar3 = local_a0 & 0x7fffffff;
    local_a8 = hiword(param_1);
    iStack_a4 = (int)loword(param_1);
    local_a8 = local_a8 & 0x7fffffff;
    local_60 = DOUBLE_8061bca8;
    if ((d2u(param_2) & 0x7fffffff00000000ULL) != 0 || uStack_9c != 0) {
        if (((local_a8 < 0x7ff00001) &&
             (((local_a8 != 0x7ff00000 || (iStack_a4 == 0)) && (uVar3 < 0x7ff00001)))) &&
            ((uVar3 != 0x7ff00000 || (uStack_9c == 0)))) {
            iVar2 = 0;
            if ((long long)d2u(param_1) < 0) {
                if (uVar3 < 0x43400000) {
                    if (0x3fefffff < uVar3) {
                        iVar5 = ((int)uVar3 >> 0x14) + -0x3ff;
                        if (iVar5 < 0x15) {
                            if ((uStack_9c == 0) &&
                                (uVar4 = (int)uVar3 >> (0x14U - iVar5 & 0x3f),
                                 uVar3 == uVar4 << (0x14U - iVar5))) {
                                iVar2 = 2 - (uVar4 & 1);
                            }
                        } else {
                            uVar4 = uStack_9c >> (0x34 - iVar5);
                            if (uStack_9c == uVar4 << (0x34 - iVar5)) {
                                iVar2 = 2 - (uVar4 & 1);
                            }
                        }
                    }
                } else {
                    iVar2 = 2;
                }
            }
            if (uStack_9c == 0) {
                if (uVar3 == 0x7ff00000) {
                    if (local_a8 == 0x3ff00000 && iStack_a4 == 0) return param_2 - param_2;
                    if (0x3fefffff < local_a8) {
                        if ((long long)d2u(param_2) < 0) return DOUBLE_8061bcb0;
                        return param_2;
                    }
                    if (-1 < (long long)d2u(param_2)) return DOUBLE_8061bcb0;
                    return -param_2;
                }
                if (uVar3 == 0x3ff00000) {
                    if (-1 < (long long)d2u(param_2)) return param_1;
                    return DOUBLE_8061bca8 / param_1;
                }
                if (local_a0 == 0x40000000) return param_1 * param_1;
                if ((local_a0 == 0x3fe00000) && (-1 < (long long)d2u(param_1))) {
                    return FUN_80390df4(param_1);
                }
            }
            local_60 = ABS(param_1);
            if ((iStack_a4 == 0) &&
                (((local_a8 == 0x7ff00000 || ((d2u(param_1) & 0x7fffffff00000000ULL) == 0)) ||
                  (local_a8 == 0x3ff00000)))) {
                if ((long long)d2u(param_2) < 0) local_60 = DOUBLE_8061bca8 / local_60;
                if ((long long)d2u(param_1) < 0) {
                    if (local_a8 == 0x3ff00000 && iVar2 == 0) {
                        local_60 = (local_60 - local_60) / (local_60 - local_60);
                    } else if (iVar2 == 1) {
                        local_60 = -local_60;
                    }
                }
            } else if ((long long)d2u(param_1) < 0 && iVar2 == 0) {
                DAT_80612fe8 = 0x21;
                local_60 = DAT_80611378;
            } else {
                unsigned int uVar1 = loword(DOUBLE_8061bcb0);
                if (uVar3 < 0x41e00001) {
                    iVar5 = 0;
                    local_68 = local_60;
                    if (local_a8 < 0x100000) {
                        iVar5 = -0x35;
                        local_68 = local_60 * DOUBLE_8061bcf0;
                        local_a8 = hiword(local_68);
                    }
                    uVar3 = local_a8 & 0xfffff;
                    uVar4 = uVar3 | 0x3ff00000;
                    iVar5 = ((int)local_a8 >> 0x14) + iVar5;
                    uVar6 = (unsigned int)(iVar5 - 0x3ff);
                    if (uVar3 < 0x3988f) {
                        iVar7 = 0;
                    } else if (uVar3 < 0xbb67a) {
                        iVar7 = 1;
                    } else {
                        uVar4 = uVar4 - 0x100000;
                        iVar7 = 0;
                        uVar6 = (unsigned int)(iVar5 - 0x3fe);
                    }
                    local_68 = mkdouble(uVar4, loword(local_68));
                    dVar9 = ((double *)DAT_803e1330)[iVar7];
                    dVar10 = DOUBLE_8061bca8 / (local_68 + dVar9);
                    dVar8 = (local_68 - dVar9) * dVar10;
                    local_98 = mkdouble((((int)uVar4 >> 1) | 0x20000000U) + 0x80000 + iVar7 * 0x40000,
                                        loword(DOUBLE_8061bcb0));
                    dVar11 = dVar8 * dVar8;
                    local_90 = u2d(d2u(dVar8) & 0xffffffff00000000ULL);
                    dVar10 = dVar10 * -(local_90 * (local_68 - (local_98 - dVar9)) -
                                        -(local_90 * local_98 - (local_68 - dVar9)));
                    dVar9 = mkdouble(0x43300000, uVar6 ^ 0x80000000) - DOUBLE_8061bdb0;
                    dVar11 = dVar10 * (local_90 + dVar8) +
                             dVar11 * dVar11 *
                             (dVar11 * (dVar11 * (dVar11 * (dVar11 * (DOUBLE_8061bd20 * dVar11 +
                                                                      DOUBLE_8061bd18) + DOUBLE_8061bd10) +
                                                  DOUBLE_8061bd08) + DOUBLE_8061bd00) + DOUBLE_8061bcf8);
                    local_98 = u2d(d2u(DOUBLE_8061bd28 + local_90 * local_90 + dVar11) &
                                   0xffffffff00000000ULL);
                    dVar8 = dVar10 * local_98 +
                            (dVar11 - ((local_98 - DOUBLE_8061bd28) - local_90 * local_90)) * dVar8;
                    local_70 = u2d(d2u(local_90 * local_98 + dVar8) & 0xffffffff00000000ULL);
                    dVar8 = ((double *)DAT_803e1350)[iVar7] +
                            DOUBLE_8061bd38 * local_70 +
                            DOUBLE_8061bd40 * (dVar8 - (local_70 - local_90 * local_98));
                    local_80 = u2d(d2u(dVar9 + DOUBLE_8061bd30 * local_70 + dVar8 +
                                       ((double *)DAT_803e1340)[iVar7]) & 0xffffffff00000000ULL);
                    dVar8 = dVar8 - (((local_80 - dVar9) - ((double *)DAT_803e1340)[iVar7]) -
                                     DOUBLE_8061bd30 * local_70);
                } else {
                    if (0x43f00000 < uVar3) {
                        if (local_a8 < 0x3ff00000) {
                            if (-1 < (long long)d2u(param_2)) return DOUBLE_8061bcb0;
                            return DOUBLE_8061bcb8;
                        }
                        if (0x3fefffff < local_a8) {
                            if ((long long)d2u(param_2) < 0x100000000LL) return DOUBLE_8061bcb0;
                            return DOUBLE_8061bcb8;
                        }
                    }
                    if (local_a8 < 0x3fefffff) {
                        if (-1 < (long long)d2u(param_2)) return DOUBLE_8061bcb0;
                        return DOUBLE_8061bcb8;
                    }
                    if (0x3ff00000 < local_a8) {
                        if ((long long)d2u(param_2) < 0x100000000LL) return DOUBLE_8061bcb0;
                        return DOUBLE_8061bcb8;
                    }
                    dVar9 = param_1 - DOUBLE_8061bca8;
                    dVar8 = DOUBLE_8061bce0 * dVar9 -
                            DOUBLE_8061bce8 * dVar9 * dVar9 *
                            -(dVar9 * -(DOUBLE_8061bcd0 * dVar9 - DOUBLE_8061bcc8) - DOUBLE_8061bcc0);
                    local_80 = u2d(d2u(DOUBLE_8061bcd8 * dVar9 + dVar8) & 0xffffffff00000000ULL);
                    dVar8 = dVar8 - (local_80 - DOUBLE_8061bcd8 * dVar9);
                }
                dVar9 = DOUBLE_8061bca8;
                if ((long long)d2u(param_1) < 0 && iVar2 == 1) dVar9 = DOUBLE_8061bd48;
                local_78 = u2d(d2u(param_2) & 0xffffffff00000000ULL);
                local_70 = local_78 * local_80;
                dVar11 = local_80 * (param_2 - local_78) + param_2 * dVar8;
                dVar8 = dVar11 + local_70;
                unsigned int hi60 = hiword(dVar8);
                if ((long long)d2u(dVar8) < 0x4090000000000000LL) {
                    if (0x4090cbff < (hi60 & 0x7fffffff)) {
                        if (dVar8 != -1075.0) return DOUBLE_8061bd60 * DOUBLE_8061bd60 * dVar9;
                        if (dVar11 <= -1075.0 - local_70) return DOUBLE_8061bd60 * DOUBLE_8061bd60 * dVar9;
                    }
                } else {
                    if (dVar8 != 1024.0) return DOUBLE_8061bd50 * DOUBLE_8061bd50 * dVar9;
                    if (1024.0 - local_70 < DOUBLE_8061bd58 + dVar11) return DOUBLE_8061bd50 * DOUBLE_8061bd50 * dVar9;
                }
                iVar2 = 0;
                if (0x3fe00000 < (hi60 & 0x7fffffff)) {
                    hi60 = hi60 + (0x100000 >> (((hi60 >> 0x14 & 0x7ff) - 0x3fe) & 0x3f));
                    uVar3 = (unsigned int)(((int)(hi60 & 0x7fffffff) >> 0x14) - 0x3ff);
                    local_88 = mkdouble(hi60 & ~(0xfffff >> (uVar3 & 0x3f)), uVar1);
                    iVar2 = (int)(hi60 & 0xfffff | 0x100000) >> ((0x14 - uVar3) & 0x3f);
                    if ((long long)d2u(dVar8) < 0) iVar2 = -iVar2;
                    local_70 = local_70 - local_88;
                }
                local_88 = u2d(d2u(dVar11 + local_70) & 0xffffffff00000000ULL);
                dVar11 = DOUBLE_8061bd70 * (dVar11 - (local_88 - local_70)) + DOUBLE_8061bd78 * local_88;
                dVar10 = DOUBLE_8061bd68 * local_88 + dVar11;
                dVar8 = dVar10 * dVar10;
                dVar11 = dVar11 - (dVar10 - DOUBLE_8061bd68 * local_88);
                dVar8 = dVar10 - dVar8 * (dVar8 * (dVar8 * (dVar8 * (DOUBLE_8061bda0 * dVar8 +
                                                                     DOUBLE_8061bd98) + DOUBLE_8061bd90) +
                                                   DOUBLE_8061bd88) + DOUBLE_8061bd80);
                dVar8 = DOUBLE_8061bca8 -
                        (((dVar10 * dVar8) / (dVar8 - DOUBLE_8061bda8) - (dVar10 * dVar11 + dVar11)) -
                         dVar10);
                unsigned int hi8 = hiword(dVar8);
                if ((int)(hi8 + iVar2 * 0x100000) >> 0x14 < 1) {
                    local_60 = FUN_803904f8(dVar8, iVar2);
                } else {
                    local_60 = mkdouble(hi8 + iVar2 * 0x100000, loword(dVar8));
                }
                local_60 = dVar9 * local_60;
            }
        } else {
            local_60 = param_1 + param_2;
        }
    }
    return local_60;
}

// ---------------------------------------------------------------------------
// msl_rem_pio2 (0x8038e930) - reduce x modulo pi/2, returning n mod 4 and the two-
// word remainder in param_2[0..1]. Fast paths for |x| < 3pi/4 and medium values;
// large arguments defer to msl_kernel_rem_pio2.
int msl_rem_pio2(double param_1, double *param_2)
{
    int iVar1, iVar2, iVar4;
    double *pdVar3;
    double dVar5, dVar7;
    unsigned int local_58;
    double local_48, local_40;
    double local_38[3];
    long long local_20;
    unsigned int uStack_14;

    dVar5 = DOUBLE_8061bdd8;
    dVar7 = DOUBLE_8061bdc8;
    local_58 = hiword(param_1) & 0x7fffffff;
    if (local_58 < 0x3fe921fc) {
        *param_2 = param_1;
        iVar1 = 0;
        param_2[1] = DOUBLE_8061bdb8;
    } else if (local_58 < 0x4002d97c) {
        if ((long long)d2u(param_1) < 0x100000000LL) {
            double dVar6 = DOUBLE_8061bdc0 + param_1;
            if (local_58 == 0x3ff921fb) {
                dVar6 = dVar6 + DOUBLE_8061bdd0;
                *param_2 = DOUBLE_8061bdd8 + dVar6;
                param_2[1] = dVar5 + (dVar6 - *param_2);
            } else {
                *param_2 = DOUBLE_8061bdc8 + dVar6;
                param_2[1] = dVar7 + (dVar6 - *param_2);
            }
            iVar1 = -1;
        } else {
            double dVar6 = param_1 - DOUBLE_8061bdc0;
            if (local_58 == 0x3ff921fb) {
                dVar6 = dVar6 - DOUBLE_8061bdd0;
                *param_2 = dVar6 - DOUBLE_8061bdd8;
                param_2[1] = (dVar6 - *param_2) - dVar5;
            } else {
                *param_2 = dVar6 - DOUBLE_8061bdc8;
                param_2[1] = (dVar6 - *param_2) - dVar7;
            }
            iVar1 = 1;
        }
    } else {
        iVar2 = (int)local_58 >> 0x14;
        if (local_58 < 0x413921fc) {
            iVar1 = (int)(DOUBLE_8061bde8 * ABS(param_1) + DOUBLE_8061bde0);
            local_38[2] = (double)iVar1;  // original int->double via CONCAT44 magic-number trick
            dVar7 = -(DOUBLE_8061bdc0 * local_38[2] - ABS(param_1));
            dVar5 = DOUBLE_8061bdc8 * local_38[2];
            if ((iVar1 < 0x20) && (local_58 != *(unsigned int *)(DAT_803e1464 + iVar1 * 4))) {
                *param_2 = dVar7 - dVar5;
            } else {
                *param_2 = dVar7 - dVar5;
                if (0x10 < (int)(iVar2 - (int)((hiword(*param_2) >> 0x14) & 0x7ff))) {
                    double dVar6 = dVar7 - DOUBLE_8061bdd0 * local_38[2];
                    dVar5 = DOUBLE_8061bdd8 * local_38[2] - ((dVar7 - dVar6) - DOUBLE_8061bdd0 * local_38[2]);
                    *param_2 = dVar6 - dVar5;
                    dVar7 = dVar6;
                    if (0x31 < (int)(iVar2 - (int)((hiword(*param_2) >> 0x14) & 0x7ff))) {
                        dVar7 = dVar6 - DOUBLE_8061bdf0 * local_38[2];
                        dVar5 = DOUBLE_8061bdf8 * local_38[2] - ((dVar6 - dVar7) - DOUBLE_8061bdf0 * local_38[2]);
                        *param_2 = dVar7 - dVar5;
                    }
                }
            }
            param_2[1] = (dVar7 - *param_2) - dVar5;
            if ((long long)d2u(param_1) < 0) {
                iVar1 = -iVar1;
                *param_2 = -*param_2;
                param_2[1] = -param_2[1];
            }
        } else if (local_58 < 0x7ff00000) {
            iVar4 = 3;
            dVar7 = mkdouble(local_58 + (iVar2 + -0x416) * -0x100000, loword(param_1));
            iVar1 = (int)dVar7;
            local_38[2] = (double)(long long)iVar1;
            local_48 = (double)iVar1;
            dVar7 = DOUBLE_8061be00 * (dVar7 - local_48);
            uStack_14 = (unsigned int)dVar7;
            local_20 = (long long)(int)uStack_14;
            local_40 = (double)(int)uStack_14;
            local_38[0] = DOUBLE_8061be00 * (dVar7 - local_40);
            for (pdVar3 = local_38 + 1; DOUBLE_8061bdb8 == pdVar3[-1]; pdVar3 = pdVar3 + -1) {
                iVar4 = iVar4 + -1;
            }
            msl_kernel_rem_pio2(&local_48, param_2, iVar2 + -0x416, iVar4, 2, -0x7fc1eca0);
            iVar1 = (int)local_20;
            if ((long long)d2u(param_1) < 0) {
                iVar1 = -iVar1;
                *param_2 = -*param_2;
                param_2[1] = -param_2[1];
            }
        } else {
            iVar1 = 0;
            param_2[1] = param_1 - param_1;
            *param_2 = param_1 - param_1;
        }
    }
    return iVar1;
}

// ---------------------------------------------------------------------------
// msl_kernel_sin (0x8038ecd0) - sin(x + y) on the reduced interval [-pi/4, pi/4],
// with y the tail term and the polynomial S1..S6 evaluated in x^2.
double msl_kernel_sin(double param_1, double param_2)
{
    double dVar1, dVar2;
    unsigned int local_18;
    double local_10;

    local_18 = hiword(param_1) & 0x7fffffff;
    if ((0x3e3fffff < local_18) || ((int)param_1 != 0)) {
        dVar2 = param_1 * param_1;
        dVar1 = dVar2 * (dVar2 * (dVar2 * (dVar2 * (dVar2 * (DOUBLE_8061be40 * dVar2 + DOUBLE_8061be38) +
                                                    DOUBLE_8061be30) + DOUBLE_8061be28) +
                                  DOUBLE_8061be20) + DOUBLE_8061be18);
        if (local_18 < 0x3fd33333) {
            dVar1 = DOUBLE_8061be10 - (DOUBLE_8061be48 * dVar2 - (dVar2 * dVar1 - param_1 * param_2));
        } else {
            if (local_18 < 0x3fe90001) {
                local_10 = u2d((unsigned long long)(local_18 - 0x200000) << 0x20);
            } else {
                local_10 = DOUBLE_8061be50;
            }
            dVar1 = (DOUBLE_8061be10 - local_10) -
                    ((DOUBLE_8061be48 * dVar2 - local_10) - (dVar2 * dVar1 - param_1 * param_2));
        }
    } else {
        dVar1 = DOUBLE_8061be10;
    }
    return dVar1;
}

// ---------------------------------------------------------------------------
// msl_kernel_cos (0x8038fc18) - cos(x + y) on [-pi/4, pi/4]. param_3 selects the
// tail-inclusive form. Evaluates the C1..C6 polynomial in x^2.
double msl_kernel_cos(double param_1, double param_2, int param_3)
{
    double dVar1, dVar2, dVar3;
    unsigned int local_18 = hiword(param_1);

    if ((0x3e3fffff < (local_18 & 0x7fffffff)) || ((int)param_1 != 0)) {
        dVar3 = param_1 * param_1;
        dVar2 = dVar3 * param_1;
        dVar1 = dVar3 * (dVar3 * (dVar3 * (DOUBLE_8061beb8 * dVar3 + DOUBLE_8061beb0) + DOUBLE_8061bea8) +
                         DOUBLE_8061bea0) + DOUBLE_8061be98;
        if (param_3 == 0) {
            param_1 = dVar2 * (dVar3 * dVar1 + DOUBLE_8061bec0) + param_1;
        } else {
            param_1 = param_1 - -(DOUBLE_8061bec0 * dVar2 -
                                  (dVar3 * (DOUBLE_8061bec8 * param_2 - dVar2 * dVar1) - param_2));
        }
    }
    return param_1;
}

// ---------------------------------------------------------------------------
// msl_kernel_rem_pio2 (0x8038edc4) - the multi-precision Payne-Hanek argument
// reduction (fdlibm __kernel_rem_pio2). Given the input broken into base-2^24
// doubles (param_1 -> x[]), the exponent e0, the count nx and precision `prec`,
// it produces the reduced value in y[] (param_2) and returns n mod 8. This is a
// large fixed-array routine reproduced verbatim; the per-precision bit table and
// the two-over-pi digit table come from DAT_803e14e8 / DAT_803e14f8.
int msl_kernel_rem_pio2(void *param_1, void *param_2, int param_3, int param_4, int param_5,
                        int param_6)
{
    double *pdVar1, *pdVar2, *pdVar3, *pdVar4, *pdVar5, *pdVar6, *pdVar7;
    unsigned int *puVar8, *puVar9, *puVar10, *puVar11, *puVar12, *puVar13, *puVar14;
    bool bVar15;
    int iVar16, iVar17, iVar19, iVar20, iVar21, iVar22;
    unsigned int uVar18, uVar23;
    unsigned int *puVar24, *puVar27;
    double *pdVar25, *pdVar28, *pdVar29, *pdVar30;
    unsigned int *puVar26;
    int iVar31, iVar32, iVar33, iVar34, iVar35, iVar36, iVar37, iVar38, iVar40;
    unsigned int uVar39, uVar41, uVar42, uVar43;
    double dVar44, dVar45, dVar46, dVar47, dVar48, dVar49, dVar50, dVar51, dVar52, dVar53;
    unsigned int auStack_2cc[21];
    double local_278[20];
    double local_1d8[20];
    double local_138[20];
    double local_98, local_88, local_80;
    unsigned int uStack_8c;

    double *fbase = (double *)param_1;   // f[]/q[] scratch view of caller storage
    double *pdVar25_out = (double *)param_2;
    (void)__save_fpr_25();
    pdVar29 = fbase;
    pdVar25 = pdVar25_out;
    uVar39 = *(unsigned int *)(DAT_803e14e8 + param_5 * 4);
    iVar16 = (param_3 + -3) / 0x18 + ((param_3 + -3) >> 0x1f);
    iVar40 = param_4 + -1;
    iVar16 = iVar16 - (iVar16 >> 0x1f);
    if (iVar16 < 0) iVar16 = 0;
    iVar31 = iVar16 - iVar40;
    iVar38 = param_3 + (iVar16 + 1) * -0x18;
    iVar17 = iVar40 + uVar39 + 1;
    puVar26 = (unsigned int *)(param_6 + iVar31 * 4);
    pdVar30 = local_138;
    if (-1 < (int)(iVar40 + uVar39)) {
        do {
            dVar47 = DOUBLE_8061be58;
            if (-1 < iVar31) {
                local_98 = mkdouble(0x43300000, *puVar26 ^ 0x80000000);
                dVar47 = local_98 - DOUBLE_8061be90;
            }
            *pdVar30 = dVar47;
            pdVar30 = pdVar30 + 1;
            puVar26 = puVar26 + 1;
            iVar31 = iVar31 + 1;
            iVar17 = iVar17 + -1;
        } while (iVar17 != 0);
    }
    pdVar30 = local_278;
    for (iVar17 = 0; iVar17 <= (int)uVar39; iVar17 = iVar17 + 1) {
        iVar31 = 0;
        dVar47 = DOUBLE_8061be58;
        if (-1 < iVar40) {
            if (8 < param_4) {
                uVar41 = (param_4 - 1U) >> 3;
                iVar32 = iVar40 + iVar17;
                pdVar28 = pdVar29;
                if (-1 < param_4 + -9) {
                    do {
                        iVar33 = iVar32 - iVar31;
                        dVar48 = *pdVar28;
                        iVar19 = iVar31 + 2; iVar20 = iVar31 + 3;
                        pdVar1 = pdVar28 + 1; pdVar2 = pdVar28 + 2;
                        iVar21 = iVar31 + 4; pdVar3 = pdVar28 + 3;
                        iVar22 = iVar31 + 5; pdVar4 = pdVar28 + 4;
                        iVar36 = iVar31 + 6; pdVar5 = pdVar28 + 5;
                        iVar34 = iVar31 + 7; pdVar6 = pdVar28 + 6;
                        iVar35 = iVar31 + 1;
                        iVar31 = iVar31 + 8;
                        pdVar7 = pdVar28 + 7;
                        pdVar28 = pdVar28 + 8;
                        dVar47 = *pdVar7 * local_138[iVar32 - iVar34] +
                                 *pdVar6 * local_138[iVar32 - iVar36] +
                                 *pdVar5 * local_138[iVar32 - iVar22] +
                                 *pdVar4 * local_138[iVar32 - iVar21] +
                                 *pdVar3 * local_138[iVar32 - iVar20] +
                                 *pdVar2 * local_138[iVar32 - iVar19] +
                                 *pdVar1 * local_138[iVar32 - iVar35] + dVar48 * local_138[iVar33] + dVar47;
                        uVar41 = uVar41 - 1;
                    } while (uVar41 != 0);
                }
            }
            iVar32 = param_4 - iVar31;
            pdVar28 = pdVar29 + iVar31;
            if (iVar31 <= iVar40) {
                do {
                    iVar33 = (iVar40 + iVar17) - iVar31;
                    dVar48 = *pdVar28;
                    pdVar28 = pdVar28 + 1;
                    iVar31 = iVar31 + 1;
                    dVar47 = dVar48 * local_138[iVar33] + dVar47;
                    iVar32 = iVar32 + -1;
                } while (iVar32 != 0);
            }
        }
        *pdVar30 = dVar47;
        pdVar30 = pdVar30 + 1;
    }
    puVar24 = auStack_2cc + 1;
    uVar41 = uVar39;
    dVar47 = DOUBLE_8061be60;
    dVar48 = DOUBLE_8061be90;
    dVar52 = DOUBLE_8061be68;
    dVar50 = DOUBLE_8061be78;
    dVar51 = DOUBLE_8061be70;
    dVar53 = DOUBLE_8061be58;
    dVar45 = 0.0;
    do {
        pdVar30 = local_278 + uVar41;
        dVar45 = *pdVar30;
        if (0 < (int)uVar41) {
            uVar42 = uVar41 >> 1;
            puVar27 = puVar24;
            uVar23 = uVar41;
            if (uVar42 != 0) {
                do {
                    local_98 = (double)(long long)(int)(dVar47 * dVar45);
                    uStack_8c = (int)(dVar47 * dVar45) ^ 0x80000000;
                    dVar49 = mkdouble(0x43300000, uStack_8c) - dVar48;
                    dVar46 = dVar49 + pdVar30[-1];
                    pdVar30 = pdVar30 + -2;
                    dVar44 = *pdVar30;
                    uVar23 = (unsigned int)(dVar47 * dVar46);
                    *puVar27 = (int)-(dVar52 * dVar49 - dVar45);
                    dVar49 = u2d(d2u(mkdouble(0x43300000, uVar23)) ^ 0x8000000000000000ULL) - dVar48;
                    dVar45 = dVar49 + dVar44;
                    uVar23 = (unsigned int)-(dVar52 * dVar49 - dVar46);
                    puVar27[1] = uVar23;
                    puVar27 = puVar27 + 2;
                    uVar42 = uVar42 - 1;
                } while (uVar42 != 0);
                uVar23 = uVar41 & 1;
                if (uVar23 == 0) goto LAB_8038f138;
            }
            do {
                pdVar30 = pdVar30 + -1;
                local_98 = (double)(long long)(int)(dVar47 * dVar45);
                uStack_8c = (int)(dVar47 * dVar45) ^ 0x80000000;
                dVar46 = mkdouble(0x43300000, uStack_8c) - dVar48;
                dVar44 = dVar52 * dVar46 - dVar45;
                dVar45 = dVar46 + *pdVar30;
                uVar42 = (unsigned int)-dVar44;
                *puVar27 = uVar42;
                uVar23 = uVar23 - 1;
                puVar27 = puVar27 + 1;
            } while (uVar23 != 0);
        }
    LAB_8038f138:
        dVar45 = FUN_803904f8(dVar45, iVar38);
        dVar44 = FUN_80390324(dVar50 * dVar45);
        dVar45 = -(dVar51 * dVar44 - dVar45);
        iVar17 = 0;
        uStack_8c = (unsigned int)dVar45;
        uStack_8c = uStack_8c ^ 0x80000000;
        dVar45 = dVar45 - (mkdouble(0x43300000, uStack_8c) - dVar48);
        if (iVar38 < 1) {
            if (iVar38 == 0) {
                iVar17 = (int)auStack_2cc[uVar41] >> 0x17;
            } else if (DOUBLE_8061be80 <= dVar45) {
                iVar17 = 2;
            }
        } else {
            auStack_2cc[uVar41] =
                auStack_2cc[uVar41] -
                (((int)auStack_2cc[uVar41] >> ((0x18U - iVar38) & 0x3f)) << (0x18U - iVar38));
            iVar17 = (int)auStack_2cc[uVar41] >> ((0x17U - iVar38) & 0x3f);
        }
        if (0 < iVar17) {
            bVar15 = false;
            puVar27 = puVar24;
            uVar42 = uVar41;
            if (0 < (int)uVar41) {
                do {
                    uVar23 = *puVar27;
                    if (bVar15) {
                        *puVar27 = 0xffffff - uVar23;
                    } else if (uVar23 != 0) {
                        bVar15 = true;
                        *puVar27 = 0x1000000 - uVar23;
                    }
                    puVar27 = puVar27 + 1;
                    uVar42 = uVar42 - 1;
                } while (uVar42 != 0);
            }
            if (0 < iVar38) {
                if (iVar38 == 2) {
                    auStack_2cc[uVar41] = auStack_2cc[uVar41] & 0x3fffff;
                } else if ((iVar38 < 2) && (0 < iVar38)) {
                    auStack_2cc[uVar41] = auStack_2cc[uVar41] & 0x7fffff;
                }
            }
            if ((iVar17 == 2) && (dVar45 = DOUBLE_8061be88 - dVar45, bVar15)) {
                dVar44 = FUN_803904f8(DOUBLE_8061be88, iVar38);
                dVar45 = dVar45 - dVar44;
            }
        }
        if (dVar53 != dVar45) break;
        uVar23 = 0;
        puVar27 = auStack_2cc + uVar41;
        uVar42 = uVar41 - uVar39;
        if ((int)uVar39 <= (int)(uVar41 - 1)) {
            uVar43 = uVar42 >> 3;
            if (uVar43 != 0) {
                do {
                    uVar18 = *puVar27;
                    puVar8 = puVar27 + -1; puVar9 = puVar27 + -2; puVar10 = puVar27 + -3;
                    puVar11 = puVar27 + -4; puVar12 = puVar27 + -5; puVar13 = puVar27 + -6;
                    puVar14 = puVar27 + -7;
                    puVar27 = puVar27 + -8;
                    uVar23 = uVar23 | uVar18 | *puVar8 | *puVar9 | *puVar10 | *puVar11 | *puVar12 | *puVar13 | *puVar14;
                    uVar43 = uVar43 - 1;
                } while (uVar43 != 0);
                uVar42 = uVar42 & 7;
                if (uVar42 == 0) goto LAB_8038f350;
            }
            do {
                uVar43 = *puVar27;
                puVar27 = puVar27 + -1;
                uVar23 = uVar23 | uVar43;
                uVar42 = uVar42 - 1;
            } while (uVar42 != 0);
        }
    LAB_8038f350:
        if (uVar23 != 0) break;
        for (iVar17 = 1; puVar24[uVar39 - iVar17] == 0; iVar17 = iVar17 + 1) { }
        iVar31 = uVar41 + 1;
        pdVar30 = local_278 + iVar31;
        for (; iVar31 <= (int)(uVar41 + iVar17); iVar31 = iVar31 + 1) {
            iVar32 = iVar40 + iVar31;
            iVar33 = 0;
            local_88 = mkdouble(0x43300000, *(unsigned int *)(param_6 + (iVar16 + iVar31) * 4) ^ 0x80000000);
            local_138[iVar32] = local_88 - dVar48;
            dVar45 = DOUBLE_8061be58;
            if (-1 < iVar40) {
                if (8 < param_4) {
                    uVar42 = (param_4 - 1U) >> 3;
                    pdVar28 = pdVar29;
                    if (-1 < param_4 + -9) {
                        do {
                            iVar34 = iVar32 - iVar33;
                            dVar44 = *pdVar28;
                            iVar19 = iVar33 + 1; iVar35 = iVar33 + 2;
                            pdVar1 = pdVar28 + 4; iVar36 = iVar33 + 3;
                            pdVar2 = pdVar28 + 1; iVar20 = iVar33 + 4;
                            pdVar3 = pdVar28 + 2; iVar21 = iVar33 + 5;
                            pdVar4 = pdVar28 + 6; pdVar5 = pdVar28 + 3;
                            iVar37 = iVar33 + 6; pdVar6 = pdVar28 + 5;
                            iVar22 = iVar33 + 7; pdVar7 = pdVar28 + 7;
                            pdVar28 = pdVar28 + 8;
                            iVar33 = iVar33 + 8;
                            dVar45 = *pdVar7 * local_138[iVar32 - iVar22] +
                                     *pdVar4 * local_138[iVar32 - iVar37] +
                                     *pdVar6 * local_138[iVar32 - iVar21] +
                                     *pdVar1 * local_138[iVar32 - iVar20] +
                                     *pdVar5 * local_138[iVar32 - iVar36] +
                                     *pdVar3 * local_138[iVar32 - iVar35] +
                                     *pdVar2 * local_138[iVar32 - iVar19] + dVar44 * local_138[iVar34] + dVar45;
                            uVar42 = uVar42 - 1;
                        } while (uVar42 != 0);
                    }
                }
                iVar19 = param_4 - iVar33;
                pdVar28 = pdVar29 + iVar33;
                if (iVar33 <= iVar40) {
                    do {
                        iVar20 = iVar32 - iVar33;
                        dVar44 = *pdVar28;
                        pdVar28 = pdVar28 + 1;
                        iVar33 = iVar33 + 1;
                        dVar45 = dVar44 * local_138[iVar20] + dVar45;
                        iVar19 = iVar19 + -1;
                    } while (iVar19 != 0);
                }
            }
            *pdVar30 = dVar45;
            pdVar30 = pdVar30 + 1;
        }
        uVar41 = uVar41 + iVar17;
    } while (true);

    if (DOUBLE_8061be58 == dVar45) {
        for (puVar24 = auStack_2cc + uVar41; uVar41 = uVar41 - 1, iVar38 = iVar38 + -0x18, *puVar24 == 0;
             puVar24 = puVar24 + -1) { }
    } else {
        dVar47 = FUN_803904f8(dVar45, -iVar38);
        if (dVar47 < DOUBLE_8061be68) {
            auStack_2cc[uVar41 + 1] = (int)dVar47;
        } else {
            iVar38 = iVar38 + 0x18;
            local_88 = (double)(long long)(int)(DOUBLE_8061be60 * dVar47);
            uStack_8c = (int)(DOUBLE_8061be60 * dVar47) ^ 0x80000000;
            dVar48 = mkdouble(0x43300000, uStack_8c) - DOUBLE_8061be90;
            uVar42 = (unsigned int)dVar48;
            uVar23 = (unsigned int)-(DOUBLE_8061be68 * dVar48 - dVar47);
            auStack_2cc[uVar41 + 1] = uVar23;
            auStack_2cc[uVar41 + 2] = uVar42;
            uVar41 = uVar41 + 1;
        }
    }
    dVar47 = FUN_803904f8(DOUBLE_8061be88, iVar38);
    puVar24 = auStack_2cc + uVar41 + 1;
    pdVar29 = local_278 + uVar41;
    uVar42 = uVar41 + 1;
    if (-1 < (int)uVar41) {
        uVar23 = uVar42 >> 2;
        if (uVar23 != 0) {
            do {
                double db = mkdouble(0x43300000, *puVar24 ^ 0x80000000);
                puVar27 = puVar24 + -2;
                double d1 = mkdouble(0x43300000, puVar24[-1] ^ 0x80000000);
                uVar43 = puVar24[-3];
                puVar24 = puVar24 + -4;
                double dVar50l = d1 - DOUBLE_8061be90;
                double d52 = mkdouble(0x43300000, *puVar27 ^ 0x80000000);
                *pdVar29 = dVar47 * (db - DOUBLE_8061be90);
                dVar48 = dVar47 * DOUBLE_8061be60 * DOUBLE_8061be60;
                double d43 = mkdouble(0x43300000, uVar43 ^ 0x80000000);
                pdVar29[-1] = dVar47 * DOUBLE_8061be60 * dVar50l;
                double dVar50b = dVar48 * DOUBLE_8061be60;
                double dVar51b = d43 - DOUBLE_8061be90;
                pdVar29[-2] = dVar48 * (d52 - DOUBLE_8061be90);
                dVar47 = dVar50b * DOUBLE_8061be60;
                pdVar29[-3] = dVar50b * dVar51b;
                pdVar29 = pdVar29 + -4;
                uVar23 = uVar23 - 1;
            } while (uVar23 != 0);
            uVar42 = uVar42 & 3;
            if (uVar42 == 0) goto LAB_8038f71c;
        }
        do {
            uVar23 = *puVar24;
            puVar24 = puVar24 + -1;
            double dl = mkdouble(0x43300000, uVar23 ^ 0x80000000);
            dVar48 = dVar47 * (dl - DOUBLE_8061be90);
            dVar47 = dVar47 * DOUBLE_8061be60;
            *pdVar29 = dVar48;
            pdVar29 = pdVar29 + -1;
            uVar42 = uVar42 - 1;
        } while (uVar42 != 0);
    }
LAB_8038f71c:
    iVar16 = uVar41 + 1;
    uVar42 = uVar41;
    if (-1 < (int)uVar41) {
        do {
            iVar40 = uVar41 - uVar42;
            pdVar29 = DAT_803e14f8;
            dVar47 = DOUBLE_8061be58;
            for (iVar31 = 0; (iVar31 <= (int)uVar39 && (iVar31 <= iVar40)); iVar31 = iVar31 + 1) {
                dVar48 = *pdVar29;
                pdVar29 = pdVar29 + 1;
                dVar47 = dVar48 * local_278[uVar42 + iVar31] + dVar47;
            }
            uVar42 = uVar42 - 1;
            local_1d8[iVar40] = dVar47;
            iVar16 = iVar16 + -1;
        } while (iVar16 != 0);
    }
    int nret = iVar17;
    if (param_5 != 3) {
        if (2 < param_5) goto LAB_8038fbf8;
        if (param_5 != 0) {
            if (param_5 < 0) goto LAB_8038fbf8;
            pdVar29 = local_1d8 + uVar41;
            uVar39 = uVar41 + 1;
            dVar47 = DOUBLE_8061be58;
            if (-1 < (int)uVar41) {
                uVar42 = uVar39 >> 3;
                if (uVar42 != 0) {
                    do {
                        dVar48 = *pdVar29;
                        pdVar30 = pdVar29 + -1; pdVar28 = pdVar29 + -2; pdVar1 = pdVar29 + -3;
                        pdVar2 = pdVar29 + -4; pdVar3 = pdVar29 + -5; pdVar4 = pdVar29 + -6;
                        pdVar5 = pdVar29 + -7;
                        pdVar29 = pdVar29 + -8;
                        dVar47 = dVar47 + dVar48 + *pdVar30 + *pdVar28 + *pdVar1 + *pdVar2 + *pdVar3 + *pdVar4 + *pdVar5;
                        uVar42 = uVar42 - 1;
                    } while (uVar42 != 0);
                    uVar39 = uVar39 & 7;
                    if (uVar39 == 0) goto LAB_8038f8d4;
                }
                do {
                    dVar48 = *pdVar29;
                    pdVar29 = pdVar29 + -1;
                    dVar47 = dVar47 + dVar48;
                    uVar39 = uVar39 - 1;
                } while (uVar39 != 0);
            }
        LAB_8038f8d4:
            dVar48 = dVar47;
            if (nret != 0) dVar48 = -dVar47;
            *pdVar25 = dVar48;
            iVar16 = 1;
            local_1d8[0] = local_1d8[0] - dVar47;
            if (0 < (int)uVar41) {
                if (8 < (int)uVar41) {
                    pdVar29 = local_1d8 + 1;
                    uVar39 = (uVar41 - 1) >> 3;
                    if (0 < (int)(uVar41 - 8)) {
                        do {
                            dVar47 = *pdVar29;
                            iVar16 = iVar16 + 8;
                            pdVar30 = pdVar29 + 1; pdVar28 = pdVar29 + 2; pdVar1 = pdVar29 + 3;
                            pdVar2 = pdVar29 + 4; pdVar3 = pdVar29 + 5; pdVar4 = pdVar29 + 6;
                            pdVar5 = pdVar29 + 7;
                            pdVar29 = pdVar29 + 8;
                            local_1d8[0] = local_1d8[0] + dVar47 + *pdVar30 + *pdVar28 + *pdVar1 + *pdVar2 + *pdVar3 + *pdVar4 + *pdVar5;
                            uVar39 = uVar39 - 1;
                        } while (uVar39 != 0);
                    }
                }
                iVar40 = (uVar41 + 1) - iVar16;
                pdVar29 = local_1d8 + iVar16;
                if (iVar16 <= (int)uVar41) {
                    do {
                        dVar47 = *pdVar29;
                        pdVar29 = pdVar29 + 1;
                        local_1d8[0] = local_1d8[0] + dVar47;
                        iVar40 = iVar40 + -1;
                    } while (iVar40 != 0);
                }
            }
            if (nret != 0) local_1d8[0] = -local_1d8[0];
            pdVar25[1] = local_1d8[0];
            goto LAB_8038fbf8;
        }
        pdVar29 = local_1d8 + uVar41;
        uVar39 = uVar41 + 1;
        dVar47 = DOUBLE_8061be58;
        if (-1 < (int)uVar41) {
            uVar41 = uVar39 >> 3;
            if (uVar41 != 0) {
                do {
                    dVar48 = *pdVar29;
                    pdVar30 = pdVar29 + -1; pdVar28 = pdVar29 + -2; pdVar1 = pdVar29 + -3;
                    pdVar2 = pdVar29 + -4; pdVar3 = pdVar29 + -5; pdVar4 = pdVar29 + -6;
                    pdVar5 = pdVar29 + -7;
                    pdVar29 = pdVar29 + -8;
                    dVar47 = dVar47 + dVar48 + *pdVar30 + *pdVar28 + *pdVar1 + *pdVar2 + *pdVar3 + *pdVar4 + *pdVar5;
                    uVar41 = uVar41 - 1;
                } while (uVar41 != 0);
                uVar39 = uVar39 & 7;
                if (uVar39 == 0) goto LAB_8038f834;
            }
            do {
                dVar48 = *pdVar29;
                pdVar29 = pdVar29 + -1;
                dVar47 = dVar47 + dVar48;
                uVar39 = uVar39 - 1;
            } while (uVar39 != 0);
        }
    LAB_8038f834:
        if (nret != 0) dVar47 = -dVar47;
        *pdVar25 = dVar47;
        goto LAB_8038fbf8;
    }
    pdVar29 = local_1d8 + uVar41;
    if (0 < (int)uVar41) {
        uVar39 = uVar41 >> 2;
        pdVar30 = pdVar29;
        uVar42 = uVar41;
        if (uVar39 != 0) {
            do {
                dVar47 = pdVar30[-1] + *pdVar30;
                *pdVar30 = *pdVar30 + (pdVar30[-1] - dVar47);
                pdVar30[-1] = dVar47;
                dVar47 = pdVar30[-2] + pdVar30[-1];
                pdVar30[-1] = pdVar30[-1] + (pdVar30[-2] - dVar47);
                pdVar30[-2] = dVar47;
                dVar47 = pdVar30[-3] + pdVar30[-2];
                pdVar30[-2] = pdVar30[-2] + (pdVar30[-3] - dVar47);
                pdVar30[-3] = dVar47;
                dVar47 = pdVar30[-4] + pdVar30[-3];
                pdVar30[-3] = pdVar30[-3] + (pdVar30[-4] - dVar47);
                pdVar30 = pdVar30 + -4;
                *pdVar30 = dVar47;
                uVar39 = uVar39 - 1;
            } while (uVar39 != 0);
            uVar42 = uVar41 & 3;
            if (uVar42 == 0) goto LAB_8038fa7c;
        }
        do {
            dVar47 = pdVar30[-1] + *pdVar30;
            *pdVar30 = *pdVar30 + (pdVar30[-1] - dVar47);
            pdVar30[-1] = dVar47;
            uVar42 = uVar42 - 1;
            pdVar30 = pdVar30 + -1;
        } while (uVar42 != 0);
    }
LAB_8038fa7c:
    uVar39 = uVar41 - 1;
    if (1 < (int)uVar41) {
        uVar42 = uVar39 >> 2;
        pdVar30 = pdVar29;
        if (uVar42 != 0) {
            do {
                dVar47 = pdVar30[-1] + *pdVar30;
                *pdVar30 = *pdVar30 + (pdVar30[-1] - dVar47);
                pdVar30[-1] = dVar47;
                dVar47 = pdVar30[-2] + pdVar30[-1];
                pdVar30[-1] = pdVar30[-1] + (pdVar30[-2] - dVar47);
                pdVar30[-2] = dVar47;
                dVar47 = pdVar30[-3] + pdVar30[-2];
                pdVar30[-2] = pdVar30[-2] + (pdVar30[-3] - dVar47);
                pdVar30[-3] = dVar47;
                dVar47 = pdVar30[-4] + pdVar30[-3];
                pdVar30[-3] = pdVar30[-3] + (pdVar30[-4] - dVar47);
                pdVar30 = pdVar30 + -4;
                *pdVar30 = dVar47;
                uVar42 = uVar42 - 1;
            } while (uVar42 != 0);
            uVar39 = uVar39 & 3;
            if (uVar39 == 0) goto LAB_8038fb38;
        }
        do {
            dVar47 = pdVar30[-1] + *pdVar30;
            *pdVar30 = *pdVar30 + (pdVar30[-1] - dVar47);
            pdVar30[-1] = dVar47;
            uVar39 = uVar39 - 1;
            pdVar30 = pdVar30 + -1;
        } while (uVar39 != 0);
    }
LAB_8038fb38:
    uVar39 = uVar41 - 1;
    dVar47 = DOUBLE_8061be58;
    if (1 < (int)uVar41) {
        uVar41 = uVar39 >> 3;
        if (uVar41 != 0) {
            do {
                dVar48 = *pdVar29;
                pdVar30 = pdVar29 + -1; pdVar28 = pdVar29 + -2; pdVar1 = pdVar29 + -3;
                pdVar2 = pdVar29 + -4; pdVar3 = pdVar29 + -5; pdVar4 = pdVar29 + -6;
                pdVar5 = pdVar29 + -7;
                pdVar29 = pdVar29 + -8;
                dVar47 = dVar47 + dVar48 + *pdVar30 + *pdVar28 + *pdVar1 + *pdVar2 + *pdVar3 + *pdVar4 + *pdVar5;
                uVar41 = uVar41 - 1;
            } while (uVar41 != 0);
            uVar39 = uVar39 & 7;
            if (uVar39 == 0) goto LAB_8038fbb8;
        }
        do {
            dVar48 = *pdVar29;
            pdVar29 = pdVar29 + -1;
            dVar47 = dVar47 + dVar48;
            uVar39 = uVar39 - 1;
        } while (uVar39 != 0);
    }
LAB_8038fbb8:
    if (nret == 0) {
        pdVar25[0] = local_1d8[0];
        pdVar25[1] = local_1d8[1];
        pdVar25[2] = dVar47;
    } else {
        pdVar25[0] = -local_1d8[0];
        pdVar25[1] = -local_1d8[1];
        pdVar25[2] = -dVar47;
    }
LAB_8038fbf8:
    __restore_fpr_25();
    return nret;
}


// ===========================================================================
// batch_1077: remaining MSL libm entry points that live contiguously after the
// kernels above - tan kernel, atan, ceil, copysign, sin, floor, frexp, ldexp,
// modf, rint. Same fdlibm lineage and the same bit-surgery approach.
// ===========================================================================

// Additional shared coefficient / threshold tables for this block.
extern double DOUBLE_8061bed0, DOUBLE_8061bed8, DOUBLE_8061bee0, DOUBLE_8061bee8;
extern double DOUBLE_8061bef0, DOUBLE_8061bef8, DOUBLE_8061bf00, DOUBLE_8061bf08;
extern double DOUBLE_8061bf10, DOUBLE_8061bf18, DOUBLE_8061bf20, DOUBLE_8061bf28;
extern double DOUBLE_8061bf30, DOUBLE_8061bf38, DOUBLE_8061bf40, DOUBLE_8061bf48;
extern double DOUBLE_8061bf50, DOUBLE_8061bf58, DOUBLE_8061bf60, DOUBLE_8061bf68;
extern double DOUBLE_8061bf70, DOUBLE_8061bf78, DOUBLE_8061bf80;
extern double DAT_803e1538, DAT_803e1540, DAT_803e1548, DAT_803e1550, DAT_803e1558;
extern double DAT_803e1560, DAT_803e1568, DAT_803e1570, DAT_803e1578, DAT_803e1580;
extern double DAT_803e1588, DAT_803e1590, DAT_803e1598;
extern unsigned char DAT_803e15a0[]; // atan: hi angle table (indexed *8)
extern unsigned char DAT_803e15c0[]; // atan: lo angle table (indexed *8)
extern double DAT_803e15b8, DAT_803e15d8; // atan: pi/2 hi/lo for the |x|>2^66 tail
extern double DAT_803e15e0, DAT_803e15e8, DAT_803e15f0, DAT_803e15f8, DAT_803e1600;
extern double DAT_803e1608, DAT_803e1610, DAT_803e1618, DAT_803e1620, DAT_803e1628, DAT_803e1630;
extern unsigned char DAT_803e1638[]; // rint: 2^52 rounding magic, signed via index

// copysign is used by ldexp below; forward-declare
double msl_copysign(double param_1, double param_2);

// ---------------------------------------------------------------------------
// msl_kernel_tan (0x8038fcb8) - tan(x+y) on the reduced quarter-pi interval
// (fdlibm __kernel_tan). param_3 == 1 returns tan, otherwise -1/tan (used by the
// cotangent path). Evaluates the odd T0..T12 rational in x^2 and, for the large-arg
// branch, refines with the reciprocal correction.
double msl_kernel_tan(double param_1, double param_2, unsigned int param_3)
{
    unsigned int uVar1, uVar2;
    double dVar3, dVar4;
    double local_38, local_30, local_28, local_20;

    uVar2 = hiword(param_1);
    uVar1 = uVar2 & 0x7fffffff;
    if ((uVar1 < 0x3e300000) && ((int)param_1 == 0)) {
        unsigned int lo = loword(param_1);
        if (param_3 == 0xffffffff &&
            ((d2u(param_1) & 0x7fffffff00000000ULL) == 0 && lo == 0)) {
            param_1 = DOUBLE_8061bed0 / ABS(param_1);
        } else if (param_3 != 1) {
            param_1 = DOUBLE_8061bed8 / param_1;
        }
    } else {
        local_38 = param_1;
        if (0x3fe59427 < uVar1) {
            if ((long long)d2u(param_1) < 0) {
                param_2 = -param_2;
                local_38 = -param_1;
            }
            local_38 = (DOUBLE_8061bee0 - local_38) + (DOUBLE_8061bee8 - param_2);
            param_2 = DOUBLE_8061bef0;
        }
        dVar3 = local_38 * local_38;
        dVar4 = dVar3 * dVar3;
        dVar3 = DAT_803e1538 * dVar3 * local_38 +
                dVar3 * (dVar3 * local_38 *
                         (dVar4 * (dVar4 * (dVar4 * (dVar4 * (dVar4 * DAT_803e1590 + DAT_803e1580) +
                                                     DAT_803e1570) + DAT_803e1560) + DAT_803e1550) +
                          DAT_803e1540 +
                         dVar3 * (dVar4 * (dVar4 * (dVar4 * (dVar4 * (dVar4 * DAT_803e1598 +
                                                                     DAT_803e1588) + DAT_803e1578) +
                                                    DAT_803e1568) + DAT_803e1558) + DAT_803e1548)) +
                        param_2) + param_2;
        param_1 = local_38 + dVar3;
        if (uVar1 < 0x3fe59428) {
            if (param_3 != 1) {
                local_28 = u2d(d2u(param_1) & 0xffffffff00000000ULL);
                local_30 = u2d(d2u(DOUBLE_8061bed8 / param_1) & 0xffffffff00000000ULL);
                param_1 = (DOUBLE_8061bed8 / param_1) *
                          (local_30 * (dVar3 - (local_28 - local_38)) +
                           local_30 * local_28 + DOUBLE_8061bed0) + local_30;
            }
        } else {
            local_20 = mkdouble(0x43300000, param_3 ^ 0x80000000);
            param_1 = (mkdouble(0x43300000, (1 - ((uVar2 >> 0x1e) & 2)) ^ 0x80000000) - DOUBLE_8061bf00) *
                      -(DOUBLE_8061bef8 *
                        (local_38 -
                         ((param_1 * param_1) / (param_1 + (local_20 - DOUBLE_8061bf00)) - dVar3)) -
                        (local_20 - DOUBLE_8061bf00));
        }
    }
    return param_1;
}

// ---------------------------------------------------------------------------
// msl_atan (0x8038fecc) - arctangent (fdlibm). Reduces |x| into one of five
// segments (indexed by iVar2) via the atan-addition identity, evaluates the odd
// rational in x^2, and recombines with the segment's precomputed atan constant.
double msl_atan(double param_1)
{
    unsigned int uVar1;
    int iVar2;
    double dVar3, dVar4, dVar5;
    double local_8;

    uVar1 = hiword(param_1) & 0x7fffffff;
    if (uVar1 < 0x44100000) {
        if (uVar1 < 0x3fdc0000) {
            if ((uVar1 < 0x3e200000) && (DOUBLE_8061bf10 < DOUBLE_8061bf08 + param_1)) {
                return param_1;
            }
            iVar2 = -1;
            local_8 = param_1;
        } else {
            local_8 = ABS(param_1);
            if (uVar1 < 0x3ff30000) {
                if (uVar1 < 0x3fe60000) {
                    iVar2 = 0;
                    local_8 = (DOUBLE_8061bf18 * local_8 - DOUBLE_8061bf10) / (DOUBLE_8061bf18 + local_8);
                } else {
                    iVar2 = 1;
                    local_8 = (local_8 - DOUBLE_8061bf10) / (DOUBLE_8061bf10 + local_8);
                }
            } else if (uVar1 < 0x40038000) {
                iVar2 = 2;
                local_8 = (local_8 - DOUBLE_8061bf20) / (DOUBLE_8061bf20 * local_8 + DOUBLE_8061bf10);
            } else {
                iVar2 = 3;
                local_8 = DOUBLE_8061bf28 / local_8;
            }
        }
        dVar3 = local_8 * local_8;
        dVar5 = dVar3 * dVar3;
        dVar4 = dVar5 * (dVar5 * (dVar5 * (dVar5 * (dVar5 * DAT_803e1628 + DAT_803e1618) + DAT_803e1608) +
                                  DAT_803e15f8) + DAT_803e15e8);
        dVar3 = dVar3 * (dVar5 * (dVar5 * (dVar5 * (dVar5 * (dVar5 * DAT_803e1630 + DAT_803e1620) +
                                                    DAT_803e1610) + DAT_803e1600) + DAT_803e15f0) +
                         DAT_803e15e0);
        if (iVar2 < 0) {
            dVar3 = -(local_8 * (dVar3 + dVar4) - local_8);
        } else {
            dVar3 = *(double *)(DAT_803e15a0 + iVar2 * 8) -
                    ((local_8 * (dVar3 + dVar4) - *(double *)(DAT_803e15c0 + iVar2 * 8)) - local_8);
            if ((long long)d2u(param_1) < 0) {
                dVar3 = -dVar3;
            }
        }
    } else if ((uVar1 < 0x7ff00001) &&
               ((uVar1 != 0x7ff00000 || (loword(param_1) == 0)))) {
        if ((long long)d2u(param_1) < 0x100000000LL) {
            dVar3 = -DAT_803e15b8 - DAT_803e15d8;
        } else {
            dVar3 = DAT_803e15b8 + DAT_803e15d8;
        }
    } else {
        dVar3 = param_1 + param_1;
    }
    return dVar3;
}

// ---------------------------------------------------------------------------
// msl_ceil (0x803900e4) - round toward +infinity (fdlibm). Masks off the fractional
// mantissa bits according to the unbiased exponent and, for positive non-integers,
// adds one ULP at the integer position first.
double msl_ceil(double param_1)
{
    unsigned int uVar1, uVar2;
    unsigned int local_8 = hiword(param_1), uStack_4 = loword(param_1);

    uVar1 = (local_8 >> 0x14) & 0x7ff;
    uVar2 = uVar1 - 0x3ff;
    if ((int)uVar2 < 0x14) {
        if ((int)uVar2 < 0) {
            if (DOUBLE_8061bf38 < DOUBLE_8061bf30 + param_1) {
                if ((long long)d2u(param_1) < 0) {
                    param_1 = -0.0;
                } else if (param_1 != 0.0) {
                    param_1 = 1.0;
                }
            }
        } else {
            uVar1 = 0xfffff >> (uVar2 & 0x3f);
            if ((uStack_4 != 0 || (local_8 & uVar1) != 0) && (DOUBLE_8061bf38 < DOUBLE_8061bf30 + param_1)) {
                if (0xffffffff < (long long)d2u(param_1)) {
                    local_8 = local_8 + (0x100000 >> (uVar2 & 0x3f));
                }
                param_1 = u2d((unsigned long long)(local_8 & ~uVar1) << 0x20);
            }
        }
    } else if ((int)uVar2 < 0x34) {
        uVar1 = 0xffffffff >> (uVar1 - 0x413);
        if (((uStack_4 & uVar1) != 0) && (DOUBLE_8061bf38 < DOUBLE_8061bf30 + param_1)) {
            if (0xffffffff < (long long)d2u(param_1)) {
                if (uVar2 == 0x14) {
                    param_1 = mkdouble(local_8 + 1, uStack_4);
                } else {
                    uVar2 = uStack_4 + (1 << (0x34 - uVar2));
                    if (uVar2 < uStack_4) local_8 = local_8 + 1;
                    param_1 = mkdouble(local_8, uVar2);
                }
            }
            param_1 = mkdouble(hiword(param_1), loword(param_1) & ~uVar1);
        }
    } else if (uVar2 == 0x400) {
        param_1 = param_1 + param_1;
    }
    return param_1;
}

// ---------------------------------------------------------------------------
// msl_copysign (0x80390228) - return |param_1| with the sign bit of param_2.
double msl_copysign(double param_1, double param_2)
{
    unsigned int hi1 = hiword(param_1);
    unsigned int hi2 = hiword(param_2);
    return mkdouble((hi1 & 0x7fffffff) | (hi2 & 0x80000000), loword(param_1));
}

// ---------------------------------------------------------------------------
// msl_sin (0x80390250) - sine (fdlibm __ieee754 sin). Small args go straight to the
// sin kernel; otherwise reduce mod pi/2 (msl_rem_pio2) and dispatch to the sin/cos
// kernel per the resulting quadrant.
double msl_sin(double param_1)
{
    unsigned int uVar1;
    double dVar2;
    unsigned int local_18;
    double local_10, local_8;

    local_18 = hiword(param_1);
    if ((local_18 & 0x7fffffff) < 0x3fe921fc) {
        dVar2 = msl_kernel_sin(param_1, DOUBLE_8061bf40);
    } else if ((local_18 & 0x7fffffff) < 0x7ff00000) {
        double y[2];
        uVar1 = (unsigned int)msl_rem_pio2(param_1, y);
        local_10 = y[0];
        local_8 = y[1];
        uVar1 = uVar1 & 3;
        if (uVar1 == 1) {
            dVar2 = -msl_kernel_cos(local_10, local_8, 1);
        } else if (uVar1 == 0) {
            dVar2 = msl_kernel_sin(local_10, local_8);
        } else if (uVar1 < 3) {
            dVar2 = -msl_kernel_sin(local_10, local_8);
        } else {
            dVar2 = msl_kernel_cos(local_10, local_8, 1);
        }
    } else {
        dVar2 = param_1 - param_1;
    }
    return dVar2;
}

// ---------------------------------------------------------------------------
// msl_floor (0x80390324) - round toward -infinity (fdlibm). Mirror of msl_ceil with
// the sign test inverted for the +1-ULP step.
double msl_floor(double param_1)
{
    unsigned int uVar1, uVar2;
    unsigned int local_8 = hiword(param_1), uStack_4 = loword(param_1);

    uVar1 = (local_8 >> 0x14) & 0x7ff;
    uVar2 = uVar1 - 0x3ff;
    if ((int)uVar2 < 0x14) {
        if ((int)uVar2 < 0) {
            if (DOUBLE_8061bf50 < DOUBLE_8061bf48 + param_1) {
                if ((long long)d2u(param_1) < 0) {
                    if ((d2u(param_1) & 0x7fffffff00000000ULL) != 0 || uStack_4 != 0) {
                        param_1 = -1.0;
                    }
                } else {
                    param_1 = 0.0;
                }
            }
        } else {
            uVar1 = 0xfffff >> (uVar2 & 0x3f);
            if ((uStack_4 != 0 || (local_8 & uVar1) != 0) && (DOUBLE_8061bf50 < DOUBLE_8061bf48 + param_1)) {
                if ((long long)d2u(param_1) < 0) {
                    local_8 = local_8 + (0x100000 >> (uVar2 & 0x3f));
                }
                param_1 = u2d((unsigned long long)(local_8 & ~uVar1) << 0x20);
            }
        }
    } else if ((int)uVar2 < 0x34) {
        uVar1 = 0xffffffff >> (uVar1 - 0x413);
        if (((uStack_4 & uVar1) != 0) && (DOUBLE_8061bf50 < DOUBLE_8061bf48 + param_1)) {
            if ((long long)d2u(param_1) < 0) {
                if (uVar2 == 0x14) {
                    param_1 = mkdouble(local_8 + 1, uStack_4);
                } else {
                    uVar2 = uStack_4 + (1 << (0x34 - uVar2));
                    if (uVar2 < uStack_4) local_8 = local_8 + 1;
                    param_1 = mkdouble(local_8, uVar2);
                }
            }
            param_1 = mkdouble(hiword(param_1), loword(param_1) & ~uVar1);
        }
    } else if (uVar2 == 0x400) {
        param_1 = param_1 + param_1;
    }
    return param_1;
}

// ---------------------------------------------------------------------------
// msl_frexp (0x8039046c) - split x into a normalised fraction in [0.5,1) and a
// power-of-two exponent written to *param_2. Denormals are pre-scaled by 2^54.
double msl_frexp(double param_1, int *param_2)
{
    double dVar1;
    unsigned int uVar2, uVar3;
    unsigned int hi = hiword(param_1), lo = loword(param_1);

    *param_2 = 0;
    dVar1 = DOUBLE_8061bf58;
    uVar2 = hi & 0x7fffffff;
    if ((uVar2 < 0x7ff00000) && ((d2u(param_1) & 0x7fffffff00000000ULL) != 0 || lo != 0)) {
        uVar3 = hi;
        double v = param_1;
        if (uVar2 < 0x100000) {
            *param_2 = -0x36;
            v = param_1 * dVar1;
            uVar3 = hiword(v);
            uVar2 = uVar3 & 0x7fffffff;
        }
        unsigned long long bits = (d2u(v) & 0x800fffffffffffffULL) | 0x3fe0000000000000ULL;
        *param_2 = ((int)uVar2 >> 0x14) + *param_2 + -0x3fe;
        param_1 = u2d(bits);
    }
    return param_1;
}

// ---------------------------------------------------------------------------
// msl_ldexp (0x803904f8) - x * 2^n (fdlibm scalbn). Classifies the input, handles
// denormal pre/post scaling, and clamps to signed HUGE/tiny on overflow/underflow.
double msl_ldexp(double param_1, int param_2)
{
    unsigned int uVar1;
    int iVar2;
    double dVar3;
    double local_18;
    unsigned int local_10 = hiword(param_1);
    int iStack_c = (int)loword(param_1);

    if ((local_10 & 0x7ff00000) == 0x7ff00000) {
        if (((d2u(param_1) & 0xfffff00000000ULL) == 0) && (iStack_c == 0)) uVar1 = 2;
        else uVar1 = 1;
    } else if (((local_10 & 0x7ff00000) < 0x7ff00000) &&
               ((d2u(param_1) & 0x7ff0000000000000ULL) == 0)) {
        if (((d2u(param_1) & 0xfffff00000000ULL) == 0) && (iStack_c == 0)) uVar1 = 3;
        else uVar1 = 5;
    } else {
        uVar1 = 4;
    }
    if (uVar1 < 3) return param_1;
    if (DOUBLE_8061bf60 == param_1) return param_1;
    uVar1 = (local_10 >> 0x14) & 0x7ff;
    local_18 = param_1;
    if (uVar1 == 0) {
        if (iStack_c == 0 && (d2u(param_1) & 0x7fffffff00000000ULL) == 0) return param_1;
        dVar3 = param_1 * DOUBLE_8061bf68;
        uVar1 = ((hiword(dVar3) >> 0x14) & 0x7ff) - 0x36;
        local_10 = hiword(dVar3);
        local_18 = dVar3;
        if (param_2 < -50000) return DOUBLE_8061bf70 * dVar3;
    }
    if (uVar1 == 0x7ff) {
        local_18 = local_18 + local_18;
    } else {
        iVar2 = uVar1 + param_2;
        if (iVar2 < 0x7ff) {
            if (iVar2 < 1) {
                if (iVar2 < -0x35) {
                    if (param_2 < 0xc351) {
                        local_18 = msl_copysign(DOUBLE_8061bf70, local_18);
                        local_18 = DOUBLE_8061bf70 * local_18;
                    } else {
                        local_18 = msl_copysign(DOUBLE_8061bf78, local_18);
                        local_18 = DOUBLE_8061bf78 * local_18;
                    }
                } else {
                    local_18 = mkdouble((local_10 & 0x800fffff) | ((iVar2 + 0x36) * 0x100000), loword(local_18));
                    local_18 = DOUBLE_8061bf80 * local_18;
                }
            } else {
                local_18 = mkdouble((local_10 & 0x800fffff) | (iVar2 * 0x100000), loword(local_18));
            }
        } else {
            local_18 = msl_copysign(DOUBLE_8061bf78, local_18);
            local_18 = DOUBLE_8061bf78 * local_18;
        }
    }
    return local_18;
}

// ---------------------------------------------------------------------------
// msl_modf (0x803906bc) - split x into integral part (written to *param_2) and the
// signed fractional part (returned). Pure exponent/mantissa masking.
double msl_modf(double param_1, double *param_2)
{
    unsigned int uVar1, uVar2;
    unsigned int hi = hiword(param_1), lo = loword(param_1);

    uVar1 = (hi >> 0x14) & 0x7ff;
    uVar2 = uVar1 - 0x3ff;
    if ((int)uVar2 < 0x14) {
        if ((int)uVar2 < 0) {
            *(unsigned int *)param_2 = hi & 0x80000000;
            *((unsigned int *)param_2 + 1) = 0;
        } else {
            uVar1 = 0xfffff >> (uVar2 & 0x3f);
            if (lo == 0 && (hi & uVar1) == 0) {
                *param_2 = param_1;
                param_1 = u2d((unsigned long long)(hi & 0x80000000) << 0x20);
            } else {
                *(unsigned int *)param_2 = hi & ~uVar1;
                *((unsigned int *)param_2 + 1) = 0;
                param_1 = param_1 - *param_2;
            }
        }
    } else if ((int)uVar2 < 0x34) {
        uVar1 = 0xffffffff >> (uVar1 - 0x413);
        if ((lo & uVar1) == 0) {
            *param_2 = param_1;
            param_1 = u2d((unsigned long long)(hi & 0x80000000) << 0x20);
        } else {
            *(unsigned int *)param_2 = hi;
            *((unsigned int *)param_2 + 1) = lo & ~uVar1;
            param_1 = param_1 - *param_2;
        }
    } else {
        *param_2 = param_1;
        param_1 = u2d((unsigned long long)(hi & 0x80000000) << 0x20);
    }
    return param_1;
}

// ---------------------------------------------------------------------------
// msl_rint (0x803907b8) - round to nearest integer in the current (nearest-even)
// mode (fdlibm rint). Uses the classic add/subtract of 2^52 (sign-selected from
// DAT_803e1638) to force rounding, with explicit round-half-to-even mantissa fixup.
double msl_rint(double param_1)
{
    int iVar1;
    unsigned int uVar2, uVar3;
    double dVar4;
    unsigned int hi = hiword(param_1), lo = loword(param_1);

    uVar2 = (hi >> 0x14) & 0x7ff;
    iVar1 = (int)hi >> 0x1f;
    uVar3 = uVar2 - 0x3ff;
    if ((int)uVar3 < 0x14) {
        if ((int)uVar3 < 0) {
            if ((d2u(param_1) & 0x7fffffff00000000ULL) == 0 && lo == 0) {
                return param_1;
            }
            uVar2 = lo | (hi & 0xfffff);
            double t = mkdouble((hi & 0xfffe0000) | (((uVar2 | (unsigned int)(-(int)uVar2)) >> 0xc) & 0x80000), lo);
            dVar4 = (t + *(double *)(DAT_803e1638 + iVar1 * -8)) - *(double *)(DAT_803e1638 + iVar1 * -8);
            unsigned int rhi = hiword(dVar4);
            return mkdouble((rhi & 0x7fffffff) | (unsigned int)(iVar1 * -0x80000000), loword(dVar4));
        }
        uVar2 = 0xfffff >> (uVar3 & 0x3f);
        if (lo == 0 && (hi & uVar2) == 0) {
            return param_1;
        }
        uVar2 = uVar2 >> 1;
        if (lo != 0 || (hi & uVar2) != 0) {
            if (uVar3 == 0x13) {
                param_1 = mkdouble(hi, 0x40000000);
            } else {
                param_1 = mkdouble((0x20000 >> (uVar3 & 0x3f)) | (hi & ~uVar2), lo);
            }
        }
    } else {
        if (0x33 < (int)uVar3) {
            if (uVar3 != 0x400) return param_1;
            return param_1 + param_1;
        }
        uVar2 = uVar2 - 0x413;
        uVar3 = 0xffffffff >> uVar2;
        if ((lo & uVar3) == 0) return param_1;
        uVar3 = uVar3 >> 1;
        if ((lo & uVar3) != 0) {
            param_1 = mkdouble(hi, (0x40000000 >> (uVar2 & 0x3f)) | (lo & ~uVar3));
        }
    }
    return (param_1 + *(double *)(DAT_803e1638 + iVar1 * -8)) - *(double *)(DAT_803e1638 + iVar1 * -8);
}


// ===========================================================================
// batch_1078: the cos/tan reduction wrappers plus the public <math.h> C-linkage
// entry points. The latter are thin forwarders that tail-call the corresponding
// __ieee754 kernel (the decompiler renders the tail return as void because the
// value is left in f1); they are the ANSI names the game links against.
// ===========================================================================

extern double DOUBLE_8061bf88, DOUBLE_8061bf90;

// defined in msl_math_trig.cpp (batch_1075)
extern double msl_atan_kernel(double x);
extern double msl_asin_kernel(double x);

// ---------------------------------------------------------------------------
// msl_cos (0x80390914) - cosine (fdlibm __ieee754 cos). Mirror of msl_sin: small
// args go to the cos kernel directly, otherwise reduce mod pi/2 and dispatch to the
// sin/cos kernel with the cosine quadrant mapping.
double msl_cos(double param_1)
{
    unsigned int uVar1;
    double dVar2;
    unsigned int local_18 = hiword(param_1);
    double local_10, local_8;

    if ((local_18 & 0x7fffffff) < 0x3fe921fc) {
        dVar2 = msl_kernel_cos(param_1, DOUBLE_8061bf88, 0);
    } else if ((local_18 & 0x7fffffff) < 0x7ff00000) {
        double y[2];
        uVar1 = (unsigned int)msl_rem_pio2(param_1, y);
        local_10 = y[0];
        local_8 = y[1];
        uVar1 = uVar1 & 3;
        if (uVar1 == 1) {
            dVar2 = msl_kernel_sin(local_10, local_8);
        } else if (uVar1 == 0) {
            dVar2 = msl_kernel_cos(local_10, local_8, 1);
        } else if (uVar1 < 3) {
            dVar2 = -msl_kernel_cos(local_10, local_8, 1);
        } else {
            dVar2 = -msl_kernel_sin(local_10, local_8);
        }
    } else {
        dVar2 = param_1 - param_1;
    }
    return dVar2;
}

// ---------------------------------------------------------------------------
// msl_tan (0x803909ec) - tangent (fdlibm __ieee754 tan). Small args go to the tan
// kernel; otherwise reduce mod pi/2 and pass the odd/even quadrant parity as the
// kernel's tan/cot selector.
double msl_tan(double param_1)
{
    unsigned int uVar1;
    double dVar2;
    unsigned int local_18 = hiword(param_1);
    double local_10, local_8;

    if ((local_18 & 0x7fffffff) < 0x3fe921fc) {
        dVar2 = msl_kernel_tan(param_1, DOUBLE_8061bf90, 1);
    } else if ((local_18 & 0x7fffffff) < 0x7ff00000) {
        double y[2];
        uVar1 = (unsigned int)msl_rem_pio2(param_1, y);
        local_10 = y[0];
        local_8 = y[1];
        dVar2 = msl_kernel_tan(local_10, local_8, (uVar1 & 1) * -2 + 1);
    } else {
        dVar2 = param_1 - param_1;
    }
    return dVar2;
}

// ---------------------------------------------------------------------------
// Public <math.h> C-linkage entry points (0x80390a64..0x80390b44). Each is a thin
// forwarder to the corresponding __ieee754 kernel. Kept as their ANSI names.
double math_atan_kernel_entry(double x) { return msl_atan_kernel(x); } // 0x80390a64 (public wrapper over the atan/asinh-family kernel)
double math_asin_kernel_entry(double x) { return msl_asin_kernel(x); } // 0x80390a84 (public wrapper over the asin-family kernel)
double math_atan2(double y, double x) { return msl_atan2(y, x); } // 0x80390aa4
double math_exp(double x) { return msl_exp(x); }             // 0x80390ac4
double math_fmod(double x, double y) { return msl_fmod(x, y); } // 0x80390ae4
double math_log(double x) { return msl_log(x); }             // 0x80390b04
double math_log10(double x) { return msl_log10(x); }         // 0x80390b24
double math_pow(double x, double y) { return msl_pow(x, y); }   // 0x80390b44

// ===========================================================================
// batch_1079: the double-precision sqrt core and its float-narrowing companions
// (tanf/asinf/atanf/sqrtf). msl_sqrt is the fdlibm bit-restoring integer square
// root; the *_f entries call the double kernel and narrow the result to float,
// matching the game's single-precision math.h forwarders.
// ===========================================================================
// Address: 0x80390b64 | Name: msl_sqrt  | Date: 2026-09-13
// Address: 0x80390d88 | Name: math_tanf | Date: 2026-09-13
// Address: 0x80390dac | Name: math_asinf| Date: 2026-09-13
// Address: 0x80390dd0 | Name: math_atanf| Date: 2026-09-13
// Address: 0x80390df4 | Name: msl_sqrt_entry | Date: 2026-09-13

// ---------------------------------------------------------------------------
// msl_sqrt (0x80390b64) - IEEE-754 double square root by the classic fdlibm
// bit-by-bit restoring algorithm. NaN/Inf pass through (errno EDOM=0x21 flagged);
// +/-0 returns itself; negatives return NaN. Denormals are normalised first, then
// the mantissa is doubled to an even exponent and the root extracted one bit at a
// time across the high and low 32-bit halves, with a final round-to-nearest and
// exponent recombination.
double msl_sqrt(double param_1)
{
    unsigned int uVar1;
    int iVar2, iVar3, iVar6, iVar9;
    unsigned int uVar4, uVar5, uVar7, uVar8, uVar10;
    unsigned int local_18;
    int iStack_14;
    double local_10;

    local_18 = hiword(param_1);
    iStack_14 = (int)loword(param_1);
    if ((local_18 & 0x7ff00000) == 0x7ff00000) {
        DAT_80612fe8 = 0x21;
        return param_1 * param_1 + param_1;
    }
    if ((long long)d2u(param_1) < 0x100000000LL) {
        if (iStack_14 == 0 && (d2u(param_1) & 0x7fffffff00000000ULL) == 0) {
            return param_1;
        }
        if ((long long)d2u(param_1) < 0) {
            DAT_80612fe8 = 0x21;
            return (double)DAT_80611378;
        }
    }
    iVar2 = (int)local_18 >> 0x14;
    if (iVar2 == 0) {
        iVar2 = 0;
        while (true) {
            uVar5 = hiword(param_1);
            uVar4 = loword(param_1);
            if (uVar5 != 0) break;
            param_1 = mkdouble(uVar4 >> 0xb, uVar4 << 0x15);
            iVar2 = iVar2 + -0x15;
        }
        iVar6 = 0;
        for (; (uVar5 & 0x100000) == 0; uVar5 = uVar5 << 1) {
            iVar6 = iVar6 + 1;
        }
        iVar2 = iVar2 - (iVar6 + -1);
        param_1 = mkdouble(uVar5 | uVar4 >> (0x20 - iVar6), uVar4 << iVar6);
    }
    uVar5 = loword(param_1);
    uVar4 = (hiword(param_1) & 0xfffff) | 0x100000;
    if (((unsigned int)(iVar2 - 0x3ff) & 1) != 0) {
        iVar6 = (int)uVar5 >> 0x1f;
        uVar5 = uVar5 * 2;
        uVar4 = uVar4 * 2 - iVar6;
    }
    iVar3 = uVar4 * 2 - ((int)uVar5 >> 0x1f);
    uVar7 = 0;
    iVar6 = 0;
    uVar8 = 0;
    uVar10 = 0;
    for (uVar4 = 0x200000; uVar1 = uVar5 * 2, uVar4 != 0; uVar4 = uVar4 >> 1) {
        iVar9 = iVar6 + uVar4;
        if (iVar9 <= iVar3) {
            iVar6 = iVar9 + uVar4;
            iVar3 = iVar3 - iVar9;
            uVar10 = uVar10 + uVar4;
        }
        iVar3 = ((uVar5 & 0x7fffffff) >> 0x1e) + iVar3 * 2;
        uVar5 = uVar1;
    }
    for (uVar5 = 0x80000000; uVar5 != 0; uVar5 = uVar5 >> 1) {
        uVar4 = uVar7 + uVar5;
        iVar9 = iVar6;
        if ((iVar6 < iVar3) || ((iVar6 == iVar3) && (uVar4 <= uVar1))) {
            uVar7 = uVar4 + uVar5;
            if (((uVar4 & 0x80000000) == 0x80000000) && ((uVar7 & 0x80000000) == 0)) {
                iVar9 = iVar6 + 1;
            }
            iVar3 = iVar3 - iVar6;
            if (uVar1 < uVar4) {
                iVar3 = iVar3 + -1;
            }
            uVar1 = uVar1 - uVar4;
            uVar8 = uVar8 + uVar5;
        }
        iVar6 = (int)uVar1 >> 0x1f;
        uVar1 = uVar1 * 2;
        iVar3 = iVar3 * 2 - iVar6;
        iVar6 = iVar9;
    }
    if (iVar3 != 0 || uVar1 != 0) {
        if (uVar8 == 0xffffffff) {
            uVar8 = 0;
            uVar10 = uVar10 + 1;
        } else {
            uVar8 = uVar8 + (uVar8 & 1);
        }
    }
    uVar8 = uVar8 >> 1;
    if ((uVar10 & 1) == 1) {
        uVar8 = uVar8 | 0x80000000;
    }
    local_10 = mkdouble(((int)uVar10 >> 1) + 0x3fe00000 + ((iVar2 + -0x3ff) >> 1) * 0x100000, uVar8);
    return local_10;
}

// ---------------------------------------------------------------------------
// msl_sqrt_entry (0x80390df4) - public sqrt() forwarder. The original computes the
// double root and leaves it in f1; the decompiler surfaced it as a void tail-call.
double msl_sqrt_entry(double x) { return msl_sqrt(x); }

// ---------------------------------------------------------------------------
// math_tanf (0x80390d88) - single-precision tangent: double tan kernel, narrowed.
float math_tanf(double x) { return (float)msl_tan(x); }

// ---------------------------------------------------------------------------
// math_asinf (0x80390dac) - single-precision arcsine: double asin kernel, narrowed.
float math_asinf(double x) { return (float)msl_asin_kernel(x); }

// ---------------------------------------------------------------------------
// math_atanf (0x80390dd0) - single-precision arctangent: double atan kernel, narrowed.
float math_atanf(double x) { return (float)msl_atan_kernel(x); }
