// Address: 0x8038b5e4 | Name: msl_strtod        | Date: 2026-09-13
// Address: 0x8038b670 | Name: msl_strtod_scan   | Date: 2026-09-13
// Address: 0x8038c67c | Name: msl_strtol        | Date: 2026-09-13
// Address: 0x8038c73c | Name: msl_strtol_r      | Date: 2026-09-13
// Address: 0x8038c82c | Name: msl_strtoll_scan  | Date: 2026-09-13
// Address: 0x8038cc38 | Name: msl_strtoul_scan  | Date: 2026-09-13
//
// Metrowerks Standard Library (MSL) string-to-number conversion core. These are
// the shared FSM scanners behind strtod / strtol / strtoul / strtoll and the
// scanf %f/%d/%i/%u conversions (msl_scan_engine at 0x8038a364 calls the *_scan
// cores directly). Each scanner pulls characters through a caller-supplied getch
// callback (getch(cookie, ungetc_ch, do_unget)) so the same code drives both
// nul-terminated strings and FILE* streams. On overflow they set errno (0x22 =
// ERANGE/EINVAL) via the global at 0x80612fe8 and clamp to the type limit.
//
// Non-matching decomp: control flow and constant table indices preserved; the
// packed digit-buffer / exponent state is reproduced as-is from the original.

#include "common.h"

// --- raw MSL runtime symbols referenced by this translation unit ------------
extern int          DAT_80612fe8;      // errno slot
extern double       DOUBLE_8061ba08;   // 0.0 constant
extern double       DOUBLE_8061ba10;   // parser sentinel
extern double       DAT_80611380;      // denorm low bound
extern double       DAT_80611388;      // denorm high bound
extern double       DAT_80611390;      // HUGE_VAL / clamp value
extern double       DAT_80611398;      // underflow threshold
extern double       DAT_806113a0;      // overflow threshold
extern double       DAT_80611378;      // NaN payload
extern double       DAT_8061137c;      // INFINITY payload
extern unsigned int DAT_803e12b8;      // strtod scratch template (packed)
extern unsigned int DAT_803e12bc;
extern unsigned int DAT_803e12c0;
extern unsigned int DAT_803e12c4;
extern unsigned int DAT_803e12c8;
extern unsigned int DAT_803e12cc;
extern unsigned int DAT_803e12d0;
extern unsigned int DAT_803e12d4;
extern unsigned int DAT_803e12d8;
extern unsigned int DAT_803e12dc;
extern unsigned short DAT_803e12e0;
extern unsigned int DAT_8061ba00;      // "NAN(" prefix bytes
extern unsigned char DAT_8061ba04;
extern char         s_INFINITY_803e12e4[]; // "INFINITY"
extern unsigned char DAT_80497890[];   // ctype class table
extern unsigned char DAT_80497a90[];   // upper-case fold table
extern char        *PTR_DAT_80497b90;  // locale decimal-point char ptr
extern unsigned char LAB_8038a2d4;     // string getch callback

extern double       msl_str_to_double(char *packed); // 0x8038c574-ish decimal->double

// Ghidra intrinsics used by the 64-bit accumulate path
extern "C" long long __div2u(unsigned int hi, unsigned int lo, int dhi, unsigned int dlo);

// ---------------------------------------------------------------------------
// msl_strtod_scan (0x8038b670) - the floating-point parser FSM. Reads through the
// getch callback `param_2` (cookie `param_3`), returns the parsed double, writes
// the consumed-character count to *param_4 and a range-error flag to *param_5.
// Handles decimal, hex-float (0x...p...), INFINITY and NAN(...) forms.
double msl_strtod_scan(int param_1, unsigned char *param_2, void *param_3,
                       int *param_4, int *param_5)
{
    typedef unsigned int (*getch_t)(void *, unsigned int, int);
    getch_t getch = (getch_t)param_2;

    bool bVar1;
    unsigned char bVar2;
    unsigned int uVar3;
    char cVar5;
    short sVar4;
    unsigned int uVar6;
    unsigned int uVar7;
    char *pcVar8;
    int iVar9;
    int iVar10;
    unsigned int uVar11;
    short sVar12;
    int iVar13;
    unsigned int unaff_r25;
    int iVar14;
    int iVar15;
    int iVar16;
    int iVar17;
    unsigned int uVar18;
    double dVar19;
    unsigned int local_a0;
    unsigned char local_9c;
    double local_98;
    char local_90;
    char acStack_8f[7];
    char local_88;
    unsigned int local_84;
    unsigned int local_80;
    unsigned int local_7c;
    unsigned int local_78;
    unsigned int local_74;
    unsigned int local_70;
    unsigned int local_6c;
    unsigned int local_68;
    unsigned int local_64;
    unsigned int local_60;
    unsigned short local_5c;
    int local_58;
    int local_54;
    int local_50;

    uVar7 = 1;
    uVar18 = 0;
    iVar16 = 0;
    iVar15 = 0;
    iVar14 = 0;
    iVar13 = 0;
    sVar12 = 0;
    local_84 = DAT_803e12b8;
    sVar4 = 0;
    bVar1 = false;
    local_80 = DAT_803e12bc;
    local_78 = DAT_803e12c4;
    local_74 = DAT_803e12c8;
    local_50 = 0;
    local_54 = 0;
    local_7c = DAT_803e12c0;
    local_70 = DAT_803e12cc;
    local_6c = DAT_803e12d0;
    local_68 = DAT_803e12d4;
    local_64 = DAT_803e12d8;
    local_60 = DAT_803e12dc;
    local_5c = DAT_803e12e0;
    uVar11 = (unsigned int)(unsigned char)*PTR_DAT_80497b90;
    local_58 = 0;
    *param_5 = 0;
    unaff_r25 = 0;
    local_98 = 0.0;
    iVar17 = 1;
    uVar6 = getch(param_3, 0, 0);
LAB_8038c3b4:
    do {
        while (true) {
            if (((param_1 < iVar17) || (uVar6 == 0xffffffff)) || ((uVar7 & 0x1800) != 0)) {
                if ((uVar7 & 0xe2c) == 0) {
                    *param_4 = 0;
                } else {
                    *param_4 = iVar17 + iVar16 + -1;
                }
                getch(param_3, uVar6, 1);
                dVar19 = DOUBLE_8061ba08;
                if (uVar18 == 0) {
                    if (local_50 != 0) {
                        iVar15 = -iVar15;
                    }
                    pcVar8 = (char *)((int)&local_80 + (local_80 >> 0x18) + 1);
                    uVar6 = local_80 >> 0x18;
                    while ((uVar6 != 0 && (pcVar8 = pcVar8 + -1, *pcVar8 == '0'))) {
                        iVar14 = iVar14 + 1;
                        uVar6 = uVar6 - 1;
                    }
                    cVar5 = (char)uVar6;
                    if ((uVar6 & 0xff) == 0) {
                        cVar5 = cVar5 + '\x01';
                    }
                    local_80 = (local_80 & 0x00ffffff) | ((unsigned int)(unsigned char)cVar5 << 24);
                    iVar15 = iVar15 + iVar14;
                    if ((iVar15 < -0x8000) || (0x7fff < iVar15)) {
                        *param_5 = 1;
                    }
                    if (*param_5 == 0) {
                        local_84 = (local_84 & 0xffff0000) | ((unsigned int)(unsigned short)(short)iVar15);
                        local_98 = msl_str_to_double((char *)&local_84);
                        if ((DOUBLE_8061ba08 == local_98) || (DAT_80611398 <= local_98)) {
                            if (DAT_806113a0 < local_98) {
                                *param_5 = 1;
                                local_98 = DAT_80611390;
                            }
                        } else {
                            *param_5 = 1;
                        }
                        dVar19 = local_98;
                        if ((bVar1) && ((uVar7 & 0xe2c) != 0)) {
                            dVar19 = -local_98;
                        }
                    } else {
                        dVar19 = DOUBLE_8061ba08;
                        if ((local_50 == 0) && (dVar19 = DAT_80611390, bVar1)) {
                            dVar19 = -DAT_80611390;
                        }
                    }
                } else if (local_98 != DOUBLE_8061ba10) {
                    unsigned int lo98 = (unsigned int)((unsigned long long)local_98 & 0xffffffff);
                    unsigned int hi98 = (unsigned int)((unsigned long long)local_98 >> 32);
                    unsigned short lo16 = (unsigned short)lo98;
                    if (local_58 != 0) {
                        sVar4 = -sVar4;
                    }
                    while ((lo16 & 0xf0) != 0x10) {
                        sVar4 = sVar4 + 1;
                        unsigned int nlo = (lo98 >> 1) | (hi98 << 31);
                        unsigned int nhi = hi98 >> 1;
                        lo98 = nlo; hi98 = nhi; lo16 = (unsigned short)lo98;
                    }
                    {
                        unsigned int expfield =
                            (unsigned int)(((sVar4 + (sVar12 + -1) * 4 + 0x3ff) * 0x10) | (lo16 & 0xf));
                        unsigned long long packed =
                            ((unsigned long long)expfield << 48) |
                            (((unsigned long long)hi98 << 16) | ((lo98 >> 16) & 0xffff)) & 0x0000ffffffffffffULL;
                        local_98 = (double)packed;
                    }
                    *param_4 = iVar16 + local_54 + (int)unaff_r25 + 1 + iVar13;
                    if ((dVar19 == local_98) || (DAT_80611398 <= local_98)) {
                        if (DAT_806113a0 < local_98) {
                            *param_5 = 1;
                            local_98 = DAT_80611390;
                        }
                    } else {
                        local_98 = dVar19;
                        *param_5 = 1;
                    }
                    dVar19 = local_98;
                    if (bVar1) {
                        unsigned long long b = (unsigned long long)local_98 | 0x8000000000000000ULL;
                        local_98 = (double)b;
                        dVar19 = local_98;
                    }
                }
                return dVar19;
            }
            if (uVar7 != 0x80) break;
            if (uVar6 == 0x2b) {
                iVar17 = iVar17 + 1;
                uVar6 = getch(param_3, 0, 0);
            } else if (uVar6 == 0x2d) {
                iVar17 = iVar17 + 1;
                uVar6 = getch(param_3, 0, 0);
                local_50 = 1;
            }
            uVar7 = 0x100;
        }
        if (0x7f < uVar7) {
            if (uVar7 == 0x2000) {
                pcVar8 = (char *)((int)&local_a0 + 1);
                local_a0 = DAT_8061ba00;
                iVar9 = 0;
                local_9c = DAT_8061ba04;
                for (iVar10 = 1; iVar10 < 4; iVar10 = iVar10 + 1) {
                    if (uVar6 == 0xffffffff) {
                        uVar7 = 0xffffffff;
                    } else {
                        uVar7 = (unsigned int)(unsigned char)DAT_80497a90[uVar6 & 0xff];
                    }
                    if ((int)*pcVar8 != (int)uVar7) break;
                    pcVar8 = pcVar8 + 1;
                    iVar17 = iVar17 + 1;
                    uVar6 = getch(param_3, 0, 0);
                }
                if ((iVar10 != 3) && (iVar10 != 4)) {
                    uVar7 = 0x1000;
                    goto LAB_8038c3b4;
                }
                if (iVar10 == 4) {
                    while ((iVar9 < 0x20 &&
                            (((DAT_80497890[uVar6 & 0xff] & 0x10) != 0 ||
                              ((DAT_80497890[uVar6 & 0xff] & 0xc0) != 0))))) {
                        iVar9 = iVar9 + 1;
                        iVar17 = iVar17 + 1;
                        uVar6 = getch(param_3, 0, 0);
                    }
                    if (uVar6 != 0x29) {
                        uVar7 = 0x1000;
                        goto LAB_8038c3b4;
                    }
                    iVar9 = iVar9 + 1;
                }
                if (bVar1) {
                    dVar19 = -DAT_80611378;
                } else {
                    dVar19 = DAT_80611378;
                }
                *param_4 = iVar16 + iVar10 + iVar9 + local_54;
                return dVar19;
            }
            if (uVar7 < 0x2000) {
                if (uVar7 == 0x200) {
                    if (uVar6 == 0x30) {
                        iVar17 = iVar17 + 1;
                        uVar6 = getch(param_3, 0, 0);
                    } else {
                        uVar7 = 0x400;
                    }
                } else if (uVar7 < 0x200) {
                    if (uVar7 == 0x100) {
                        if ((DAT_80497890[uVar6 & 0xff] & 0x10) == 0) {
                            uVar7 = 0x1000;
                        } else if (uVar6 == 0x30) {
                            uVar7 = 0x200;
                            iVar17 = iVar17 + 1;
                            uVar6 = getch(param_3, 0, 0);
                        } else {
                            uVar7 = 0x400;
                        }
                    }
                } else if (uVar7 == 0x400) {
                    if ((DAT_80497890[uVar6 & 0xff] & 0x10) == 0) {
                        uVar7 = 0x800;
                    } else {
                        iVar15 = uVar6 + iVar15 * 10 + -0x30;
                        if (0x7fff < iVar15) {
                            *param_5 = 1;
                        }
                        iVar17 = iVar17 + 1;
                        uVar6 = getch(param_3, 0, 0);
                    }
                }
            } else if (uVar7 == 0x8000) {
                if (uVar18 == 0x10) {
                    if (uVar6 == 0xffffffff) {
                        uVar3 = 0xffffffff;
                    } else {
                        uVar3 = (unsigned int)(unsigned char)DAT_80497a90[uVar6 & 0xff];
                    }
                    if (uVar3 == 0x50) {
                        uVar18 = 0x20;
                        iVar13 = iVar13 + 1;
                        iVar17 = iVar17 + 1;
                        uVar6 = getch(param_3, 0, 0);
                    } else {
                        uVar7 = 0x800;
                    }
                } else if (uVar18 < 0x10) {
                    if (uVar18 == 4) {
                        if ((DAT_80497890[uVar6 & 0xff] & 0x20) == 0) {
                            if (uVar6 == uVar11) {
                                uVar18 = 8;
                                iVar17 = iVar17 + 1;
                                uVar6 = getch(param_3, 0, 0);
                            } else {
                                uVar18 = 0x10;
                            }
                        } else if ((int)unaff_r25 < 0x11) {
                            sVar12 = sVar12 + 1;
                            if (uVar6 == 0xffffffff) {
                                uVar6 = 0xffffffff;
                            } else {
                                uVar6 = (unsigned int)(unsigned char)DAT_80497a90[uVar6 & 0xff];
                            }
                            if ((int)uVar6 < 0x41) {
                                iVar9 = uVar6 - 0x30;
                            } else {
                                iVar9 = uVar6 - 0x37;
                            }
                            bVar2 = (unsigned char)(iVar9 << 4);
                            if ((unaff_r25 & 1 ^ unaff_r25 >> 0x1f) != unaff_r25 >> 0x1f) {
                                bVar2 = (unsigned char)iVar9;
                            }
                            *(unsigned char *)((int)&local_98 + (int)unaff_r25 / 2) =
                                *(unsigned char *)((int)&local_98 + (int)unaff_r25 / 2) | bVar2;
                            unaff_r25 = unaff_r25 + 1;
                            iVar17 = iVar17 + 1;
                            uVar6 = getch(param_3, 0, 0);
                        } else {
                            iVar17 = iVar17 + 1;
                            uVar6 = getch(param_3, 0, 0);
                        }
                    } else if (uVar18 < 4) {
                        if (uVar18 == 2) {
                            if (uVar6 == 0x30) {
                                iVar17 = iVar17 + 1;
                                uVar6 = getch(param_3, 0, 0);
                            } else {
                                uVar18 = 4;
                            }
                        } else if ((uVar18 < 2) && (uVar18 != 0)) {
                            unaff_r25 = 2;
                            uVar18 = 2;
                            local_98 = 0.0;
                            iVar17 = iVar17 + 1;
                            uVar6 = getch(param_3, 0, 0);
                        }
                    } else if (uVar18 == 8) {
                        if ((DAT_80497890[uVar6 & 0xff] & 0x20) == 0) {
                            uVar18 = 0x10;
                        } else if ((int)unaff_r25 < 0x11) {
                            if (uVar6 == 0xffffffff) {
                                uVar6 = 0xffffffff;
                            } else {
                                uVar6 = (unsigned int)(unsigned char)DAT_80497a90[uVar6 & 0xff];
                            }
                            if ((int)uVar6 < 0x41) {
                                iVar9 = uVar6 - 0x30;
                            } else {
                                iVar9 = uVar6 - 0x37;
                            }
                            bVar2 = (unsigned char)(iVar9 << 4);
                            if ((unaff_r25 & 1 ^ unaff_r25 >> 0x1f) != unaff_r25 >> 0x1f) {
                                bVar2 = (unsigned char)iVar9;
                            }
                            *(unsigned char *)((int)&local_98 + (int)unaff_r25 / 2) =
                                *(unsigned char *)((int)&local_98 + (int)unaff_r25 / 2) | bVar2;
                            unaff_r25 = unaff_r25 + 1;
                            iVar17 = iVar17 + 1;
                            uVar6 = getch(param_3, 0, 0);
                        } else {
                            iVar17 = iVar17 + 1;
                            uVar6 = getch(param_3, 0, 0);
                        }
                    }
                } else if (uVar18 == 0x40) {
                    if ((DAT_80497890[uVar6 & 0xff] & 0x10) == 0) {
                        uVar7 = 0x1000;
                    } else if (uVar6 == 0x30) {
                        uVar18 = 0x80;
                        iVar13 = iVar13 + 1;
                        iVar17 = iVar17 + 1;
                        uVar6 = getch(param_3, 0, 0);
                    } else {
                        uVar18 = 0x100;
                    }
                } else if (uVar18 < 0x40) {
                    if (uVar18 == 0x20) {
                        iVar9 = iVar13 + 1;
                        if (uVar6 == 0x2d) {
                            local_58 = 1;
                        } else if (uVar6 != 0x2b) {
                            getch(param_3, uVar6, 1);
                            iVar9 = iVar13;
                        }
                        iVar13 = iVar9;
                        uVar18 = 0x40;
                        iVar17 = iVar17 + 1;
                        uVar6 = getch(param_3, 0, 0);
                    }
                } else if (uVar18 == 0x100) {
                    if ((DAT_80497890[uVar6 & 0xff] & 0x10) == 0) {
                        uVar7 = 0x800;
                    } else {
                        sVar4 = (short)uVar6 + sVar4 * 10 + -0x30;
                        if (0x7fff < iVar15) {
                            *param_5 = 1;
                        }
                        iVar13 = iVar13 + 1;
                        iVar17 = iVar17 + 1;
                        uVar6 = getch(param_3, 0, 0);
                    }
                }
            } else if ((uVar7 < 0x8000) && (uVar7 == 0x4000)) {
                pcVar8 = acStack_8f;
                local_90 = s_INFINITY_803e12e4[0];
                acStack_8f[0] = s_INFINITY_803e12e4[1];
                acStack_8f[1] = s_INFINITY_803e12e4[2];
                acStack_8f[2] = s_INFINITY_803e12e4[3];
                acStack_8f[3] = s_INFINITY_803e12e4[4];
                acStack_8f[4] = s_INFINITY_803e12e4[5];
                acStack_8f[5] = s_INFINITY_803e12e4[6];
                acStack_8f[6] = s_INFINITY_803e12e4[7];
                local_88 = s_INFINITY_803e12e4[8];
                for (iVar9 = 1; iVar9 < 8; iVar9 = iVar9 + 1) {
                    if (uVar6 == 0xffffffff) {
                        uVar7 = 0xffffffff;
                    } else {
                        uVar7 = (unsigned int)(unsigned char)DAT_80497a90[uVar6 & 0xff];
                    }
                    if ((int)*pcVar8 != (int)uVar7) break;
                    pcVar8 = pcVar8 + 1;
                    iVar17 = iVar17 + 1;
                    uVar6 = getch(param_3, 0, 0);
                }
                if ((iVar9 == 3) || (iVar9 == 8)) {
                    if (bVar1) {
                        dVar19 = -DAT_8061137c;
                    } else {
                        dVar19 = DAT_8061137c;
                    }
                    *param_4 = iVar16 + iVar9 + local_54;
                    return dVar19;
                }
                uVar7 = 0x1000;
            }
            goto LAB_8038c3b4;
        }
        if (uVar7 != 8) {
            if (uVar7 < 8) {
                if (uVar7 != 3) {
                    if (uVar7 < 3) {
                        if (uVar7 == 1) {
                            if ((DAT_80497890[uVar6 & 0xff] & 6) == 0) {
                                if (uVar6 == 0xffffffff) {
                                    uVar3 = 0xffffffff;
                                } else {
                                    uVar3 = (unsigned int)(unsigned char)DAT_80497a90[uVar6 & 0xff];
                                }
                                if (uVar3 == 0x49) {
                                    iVar17 = iVar17 + 1;
                                    uVar6 = getch(param_3, 0, 0);
                                    uVar7 = 0x4000;
                                    goto LAB_8038c3b4;
                                }
                                if ((int)uVar3 < 0x49) {
                                    if (uVar3 != 0x2c) {
                                        if ((int)uVar3 < 0x2c) {
                                            if (0x2a < (int)uVar3) {
LAB_8038b8bc:
                                                iVar17 = iVar17 + 1;
                                                uVar6 = getch(param_3, 0, 0);
                                                local_54 = 1;
                                                goto LAB_8038c3b4;
                                            }
                                        } else if ((int)uVar3 < 0x2e) {
                                            bVar1 = true;
                                            goto LAB_8038b8bc;
                                        }
                                    }
                                } else if (uVar3 == 0x4e) {
                                    iVar17 = iVar17 + 1;
                                    uVar6 = getch(param_3, 0, 0);
                                    uVar7 = 0x2000;
                                    goto LAB_8038c3b4;
                                }
                                uVar7 = 2;
                                goto LAB_8038c3b4;
                            }
                            uVar6 = getch(param_3, 0, 0);
                            iVar16 = iVar16 + 1;
                        } else if (uVar7 != 0) {
                            if (uVar6 == uVar11) {
                                uVar7 = 0x10;
                                iVar17 = iVar17 + 1;
                                uVar6 = getch(param_3, 0, 0);
                            } else if ((DAT_80497890[uVar6 & 0xff] & 0x10) == 0) {
                                uVar7 = 0x1000;
                            } else if (uVar6 == 0x30) {
                                iVar17 = iVar17 + 1;
                                uVar6 = getch(param_3, 0, 0);
                                if (uVar6 == 0xffffffff) {
                                    uVar7 = 0xffffffff;
                                } else {
                                    uVar7 = (unsigned int)(unsigned char)DAT_80497a90[uVar6 & 0xff];
                                }
                                if (uVar7 == 0x58) {
                                    uVar18 = 1;
                                    uVar7 = 0x8000;
                                } else {
                                    uVar7 = 4;
                                }
                            } else {
                                uVar7 = 8;
                            }
                        }
                    } else if (uVar7 < 5) {
                        if (uVar6 == 0x30) {
                            iVar17 = iVar17 + 1;
                            uVar6 = getch(param_3, 0, 0);
                        } else {
                            uVar7 = 8;
                        }
                    }
                }
            } else if (uVar7 == 0x20) {
                if ((DAT_80497890[uVar6 & 0xff] & 0x10) == 0) {
                    uVar7 = 0x40;
                } else {
                    if ((unsigned char)local_80 < 0x14) {
                        if ((uVar6 != 0x30) || ((unsigned char)local_80 != 0)) {
                            uVar3 = (unsigned int)(unsigned char)local_80;
                            local_80 = (local_80 & 0xffffff00) | (((unsigned char)local_80 + 1) & 0xff);
                            *(char *)((int)&local_80 + uVar3 + 1) = (char)uVar6;
                        }
                        iVar14 = iVar14 + -1;
                    }
                    iVar17 = iVar17 + 1;
                    uVar6 = getch(param_3, 0, 0);
                }
            } else if (uVar7 < 0x20) {
                if (uVar7 == 0x10) {
                    if ((DAT_80497890[uVar6 & 0xff] & 0x10) == 0) {
                        uVar7 = 0x1000;
                    } else {
                        uVar7 = 0x20;
                    }
                }
            } else if (uVar7 == 0x40) {
                if (uVar6 == 0xffffffff) {
                    uVar7 = 0xffffffff;
                } else {
                    uVar7 = (unsigned int)(unsigned char)DAT_80497a90[uVar6 & 0xff];
                }
                if (uVar7 == 0x45) {
                    uVar7 = 0x80;
                    iVar17 = iVar17 + 1;
                    uVar6 = getch(param_3, 0, 0);
                } else {
                    uVar7 = 0x800;
                }
            }
            goto LAB_8038c3b4;
        }
        if ((DAT_80497890[uVar6 & 0xff] & 0x10) == 0) {
            if (uVar6 == uVar11) {
                uVar7 = 0x20;
                iVar17 = iVar17 + 1;
                uVar6 = getch(param_3, 0, 0);
            } else {
                uVar7 = 0x40;
            }
        } else {
            uVar3 = (unsigned int)(unsigned char)local_80;
            if (uVar3 < 0x14) {
                local_80 = (local_80 & 0xffffff00) | (((unsigned char)local_80 + 1) & 0xff);
                *(char *)((int)&local_80 + uVar3 + 1) = (char)uVar6;
            } else {
                iVar14 = iVar14 + 1;
            }
            iVar17 = iVar17 + 1;
            uVar6 = getch(param_3, 0, 0);
        }
    } while (true);
}

// ---------------------------------------------------------------------------
// msl_strtod (0x8038b5e4) - strtod on a nul-terminated string. Drives the FSM via
// the string getch callback and validates the result against the denormal window;
// out-of-range magnitudes set errno (0x22).
void msl_strtod(void *param_1)
{
    double dVar1;
    int iStack_18;
    int local_14;
    void *local_10;
    int local_c;

    local_c = 0;
    local_10 = param_1;
    dVar1 = msl_strtod_scan(0x7fffffff, &LAB_8038a2d4, &local_10, &iStack_18, &local_14);
    if ((local_14 != 0) ||
        ((DOUBLE_8061ba08 != dVar1 && ((ABS(dVar1) < DAT_80611380 || (DAT_80611388 < ABS(dVar1))))))) {
        DAT_80612fe8 = 0x22;
    }
    return;
}

// ---------------------------------------------------------------------------
// msl_strtoul_scan (0x8038cc38) - the unsigned-integer parser FSM. `param_1` is the
// radix (0 = auto), `param_2` the max char count. Accumulates into a 32-bit value,
// sets *param_7 on overflow, *param_6 on negative sign, *param_5 = chars consumed.
unsigned int msl_strtoul_scan(unsigned int param_1, unsigned int param_2, unsigned char *param_3,
                              void *param_4, int *param_5, int *param_6, int *param_7)
{
    typedef unsigned int (*getch_t)(void *, unsigned int, int);
    getch_t getch = (getch_t)param_3;

    bool bVar1;
    unsigned int uVar2;
    unsigned int uVar3;
    unsigned int uVar4;
    int iVar5;
    unsigned int uVar6;
    unsigned int uVar7;
    unsigned int uVar8;
    int iVar9;

    uVar6 = 1;
    iVar9 = 0;
    iVar5 = 0;
    uVar8 = 0;
    uVar7 = 0;
    *param_7 = 0;
    *param_6 = 0;
    if (((((int)param_1 < 0) || (param_1 == 1)) || (0x24 < (int)param_1)) || ((int)param_2 < 1)) {
        uVar6 = 0x40;
        uVar2 = param_2;
    } else {
        iVar9 = 1;
        uVar2 = getch(param_4, 0, 0);
    }
    if (param_1 != 0) {
        uVar7 = 0xffffffff / param_1;
    }
    do {
        if ((((int)param_2 < iVar9) || (uVar2 == 0xffffffff)) || ((uVar6 & 0x60) != 0)) {
            if ((uVar6 & 0x34) == 0) {
                uVar8 = 0;
                *param_5 = 0;
            } else {
                *param_5 = iVar9 + iVar5 + -1;
            }
            getch(param_4, uVar2, 1);
            return uVar8;
        }
        switch (uVar6) {
        case 1:
            if ((DAT_80497890[uVar2 & 0xff] & 6) == 0) {
                if (uVar2 == 0x2b) {
                    iVar9 = iVar9 + 1;
                    uVar2 = getch(param_4, 0, 0);
                } else if (uVar2 == 0x2d) {
                    iVar9 = iVar9 + 1;
                    uVar2 = getch(param_4, 0, 0);
                    *param_6 = 1;
                }
                uVar6 = 2;
            } else {
                uVar2 = getch(param_4, 0, 0);
                iVar5 = iVar5 + 1;
            }
            break;
        case 2:
            if (((param_1 == 0) || (param_1 == 0x10)) && (uVar2 == 0x30)) {
                uVar6 = 4;
                iVar9 = iVar9 + 1;
                uVar2 = getch(param_4, 0, 0);
            } else {
                uVar6 = 8;
            }
            break;
        case 4:
            if ((uVar2 == 0x58) || (uVar2 == 0x78)) {
                param_1 = 0x10;
                uVar6 = 8;
                iVar9 = iVar9 + 1;
                uVar2 = getch(param_4, 0, 0);
            } else {
                if (param_1 == 0) {
                    param_1 = 8;
                }
                uVar6 = 0x10;
            }
            break;
        case 8:
        case 0x10:
            if (param_1 == 0) {
                param_1 = 10;
            }
            if (uVar7 == 0) {
                uVar7 = 0xffffffff / param_1;
            }
            uVar4 = uVar2 & 0xff;
            if ((DAT_80497890[uVar4] & 0x10) == 0) {
                if ((DAT_80497890[uVar4] & 0xc0) != 0) {
                    if (uVar2 == 0xffffffff) {
                        uVar3 = 0xffffffff;
                    } else {
                        uVar3 = (unsigned int)(unsigned char)DAT_80497a90[uVar4];
                    }
                    if ((int)(uVar3 - 0x37) < (int)param_1) {
                        if (uVar2 == 0xffffffff) {
                            uVar4 = 0xffffffff;
                        } else {
                            uVar4 = (unsigned int)(unsigned char)DAT_80497a90[uVar4];
                        }
                        uVar4 = uVar4 - 0x37;
                        goto LAB_8038cef8;
                    }
                }
                if (uVar6 == 0x10) {
                    uVar6 = 0x20;
                } else {
                    uVar6 = 0x40;
                }
            } else {
                uVar4 = uVar2 - 0x30;
                if ((int)uVar4 < (int)param_1) {
LAB_8038cef8:
                    if (uVar7 < uVar8) {
                        *param_7 = 1;
                    }
                    if (-(uVar8 * param_1) - 1 < uVar4) {
                        *param_7 = 1;
                    }
                    uVar8 = uVar8 * param_1 + uVar4;
                    uVar6 = 0x10;
                    iVar9 = iVar9 + 1;
                    uVar2 = getch(param_4, 0, 0);
                } else {
                    bVar1 = uVar6 == 0x10;
                    uVar6 = 0x40;
                    if (bVar1) {
                        uVar6 = 0x20;
                    }
                }
            }
        }
    } while (true);
}

// ---------------------------------------------------------------------------
// msl_strtoll_scan (0x8038c82c) - the signed 64-bit integer parser FSM. Same shape
// as msl_strtoul_scan but accumulates a 64-bit value (returned packed hi:lo) using
// __div2u for the overflow guard.
unsigned long long msl_strtoll_scan(unsigned int param_1, int param_2, unsigned char *param_3,
                                    void *param_4, int *param_5, int *param_6, int *param_7)
{
    typedef unsigned int (*getch_t)(void *, unsigned int, int);
    getch_t getch = (getch_t)param_3;

    bool bVar1;
    unsigned int uVar2;
    int iVar3;
    unsigned int unaff_r20;
    unsigned int uVar4;
    int iVar5;
    unsigned int uVar6;
    int iVar7;
    unsigned int uVar8;
    long long lVar9;

    uVar8 = 1;
    iVar7 = 0;
    iVar3 = 0;
    uVar6 = 0;
    iVar5 = 0;
    lVar9 = 0;
    unaff_r20 = 0;
    *param_7 = 0;
    *param_6 = 0;
    if (((((int)param_1 < 0) || (param_1 == 1)) || (0x24 < (int)param_1)) || (param_2 < 1)) {
        uVar8 = 0x40;
    } else {
        iVar7 = 1;
        unaff_r20 = getch(param_4, 0, 0);
    }
    if (param_1 != 0) {
        lVar9 = __div2u(0xffffffff, 0xffffffff, (int)param_1 >> 0x1f, param_1);
    }
    do {
        if (((param_2 < iVar7) || (unaff_r20 == 0xffffffff)) || ((uVar8 & 0x60) != 0)) {
            if ((uVar8 & 0x34) == 0) {
                uVar6 = 0;
                *param_5 = 0;
                iVar5 = 0;
            } else {
                *param_5 = iVar7 + iVar3 + -1;
            }
            getch(param_4, unaff_r20, 1);
            return ((unsigned long long)(unsigned int)iVar5 << 32) | uVar6;
        }
        switch (uVar8) {
        case 1:
            if ((DAT_80497890[unaff_r20 & 0xff] & 6) == 0) {
                if (unaff_r20 == 0x2b) {
                    iVar7 = iVar7 + 1;
                    unaff_r20 = getch(param_4, 0, 0);
                } else if (unaff_r20 == 0x2d) {
                    iVar7 = iVar7 + 1;
                    unaff_r20 = getch(param_4, 0, 0);
                    *param_6 = 1;
                }
                uVar8 = 2;
            } else {
                unaff_r20 = getch(param_4, 0, 0);
                iVar3 = iVar3 + 1;
            }
            break;
        case 2:
            if (((param_1 == 0) || (param_1 == 0x10)) && (unaff_r20 == 0x30)) {
                uVar8 = 4;
                iVar7 = iVar7 + 1;
                unaff_r20 = getch(param_4, 0, 0);
            } else {
                uVar8 = 8;
            }
            break;
        case 4:
            if ((unaff_r20 == 0x58) || (unaff_r20 == 0x78)) {
                param_1 = 0x10;
                uVar8 = 8;
                iVar7 = iVar7 + 1;
                unaff_r20 = getch(param_4, 0, 0);
            } else {
                if (param_1 == 0) {
                    param_1 = 8;
                }
                uVar8 = 0x10;
            }
            break;
        case 8:
        case 0x10:
            if (param_1 == 0) {
                param_1 = 10;
            }
            if (lVar9 == 0) {
                lVar9 = __div2u(0xffffffff, 0xffffffff, (int)param_1 >> 0x1f, param_1);
            }
            uVar4 = unaff_r20 & 0xff;
            if ((DAT_80497890[uVar4] & 0x10) == 0) {
                if ((DAT_80497890[uVar4] & 0xc0) != 0) {
                    if (unaff_r20 == 0xffffffff) {
                        uVar2 = 0xffffffff;
                    } else {
                        uVar2 = (unsigned int)(unsigned char)DAT_80497a90[uVar4];
                    }
                    if ((int)(uVar2 - 0x37) < (int)param_1) {
                        if (unaff_r20 == 0xffffffff) {
                            uVar4 = 0xffffffff;
                        } else {
                            uVar4 = (unsigned int)(unsigned char)DAT_80497a90[uVar4];
                        }
                        uVar4 = uVar4 - 0x37;
                        goto LAB_8038cb2c;
                    }
                }
                if (uVar8 == 0x10) {
                    uVar8 = 0x20;
                } else {
                    uVar8 = 0x40;
                }
            } else {
                uVar4 = unaff_r20 - 0x30;
                if ((int)uVar4 < (int)param_1) {
LAB_8038cb2c:
                    {
                        unsigned int lhi = (unsigned int)((unsigned long long)lVar9 >> 0x20);
                        if (lhi < (unsigned int)(((unsigned int)lVar9 < uVar6) + (unsigned int)iVar5)) {
                            *param_7 = 1;
                        }
                    }
                    uVar8 = uVar6 * param_1;
                    iVar5 = (int)((unsigned long long)uVar6 * (unsigned long long)param_1 >> 0x20) +
                            iVar5 * (int)param_1 + (int)(uVar6 * ((int)param_1 >> 0x1f));
                    if ((unsigned int)(-iVar5 - 1) <
                        (unsigned int)((unsigned int)(0xffffffff - uVar8 < uVar4) + ((int)uVar4 >> 0x1f))) {
                        *param_7 = 1;
                    }
                    uVar6 = uVar8 + uVar4;
                    iVar5 = iVar5 + ((int)uVar4 >> 0x1f) + (unsigned int)CARRY4(uVar8, uVar4);
                    uVar8 = 0x10;
                    iVar7 = iVar7 + 1;
                    unaff_r20 = getch(param_4, 0, 0);
                } else {
                    bVar1 = uVar8 == 0x10;
                    uVar8 = 0x40;
                    if (bVar1) {
                        uVar8 = 0x20;
                    }
                }
            }
        }
    } while (true);
}

// ---------------------------------------------------------------------------
// msl_strtol (0x8038c67c) - strtol base 10 on a nul-terminated string. Clamps to
// INT_MIN/INT_MAX and sets errno on overflow.
unsigned int msl_strtol(void *param_1)
{
    unsigned int uVar1;
    int iStack_18;
    unsigned int local_14;
    int local_10;
    void *local_c;
    int local_8;

    local_8 = 0;
    local_c = param_1;
    uVar1 = msl_strtoul_scan(10, 0x7fffffff, &LAB_8038a2d4, &local_c, &iStack_18, (int *)&local_14, &local_10);
    if (((local_10 == 0) && ((local_14 != 0 || (uVar1 < 0x80000000)))) &&
        ((local_14 == 0 || (uVar1 < 0x80000001)))) {
        if (local_14 != 0) {
            uVar1 = -uVar1;
        }
    } else {
        DAT_80612fe8 = 0x22;
        uVar1 = 0x7fffffff - ((int)(-(int)local_14 | local_14) >> 0x1f);
    }
    return uVar1;
}

// ---------------------------------------------------------------------------
// msl_strtol_r (0x8038c73c) - strtol with explicit base `param_3` and an endptr
// out-parameter `param_2` (byte offset of the first unconsumed char).
unsigned int msl_strtol_r(int param_1, int *param_2, unsigned int param_3)
{
    unsigned int uVar1;
    int local_28;
    unsigned int local_24;
    int local_20;
    int local_1c[5];

    local_1c[1] = 0;
    local_1c[0] = param_1;
    uVar1 = msl_strtoul_scan(param_3, 0x7fffffff, &LAB_8038a2d4, local_1c, &local_20, (int *)&local_24, &local_28);
    if (param_2 != (int *)0x0) {
        *param_2 = param_1 + local_20;
    }
    if (((local_28 == 0) && ((local_24 != 0 || (uVar1 < 0x80000000)))) &&
        ((local_24 == 0 || (uVar1 < 0x80000001)))) {
        if (local_24 != 0) {
            uVar1 = -uVar1;
        }
    } else {
        DAT_80612fe8 = 0x22;
        uVar1 = 0x7fffffff - ((int)(-(int)local_24 | local_24) >> 0x1f);
    }
    return uVar1;
}
