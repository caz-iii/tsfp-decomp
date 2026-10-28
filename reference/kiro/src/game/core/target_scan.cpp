// target_scan.cpp
// Screen-space entity target scan used by the lock-on / soft-aim reticle and the
// HUD target picker. A caller first installs a set of scoring/predicate callbacks
// via target_scan_set_callbacks (stored in the DAT_805cb920..930 globals):
//   - filter(ctx, ent)                         -> keep candidate?          (920)
//   - scoreDistance(dist, dx, dy, ctx, ent)    -> distance score           (924)
//   - scoreAngle(dx, dy, ctx, ent)             -> angular score            (928)
//   - visibility(dir, param2, radius, ctx, ent)-> extra visibility gate    (92c)
//   - screenGate(sx, sy, radius, ctx, ent)     -> on-screen gate           (930)
// target_scan_test_entity does the cheap single-entity variant (used when a
// candidate is already known); target_scan_screen_entities does the full pass:
// walks the active entity list (DAT_80540a44), projects each to screen space,
// gathers up to 20 candidates inside the search box, scores + insertion-sorts
// them, then LOS-raycasts from the camera in score order and returns the first
// unobstructed hit (via the *piVar9 out-slot of the context pair).
//
// Address range: 0x8025a41c - 0x8025a584  (batch_734)
//
// Self-contained translation unit.

typedef unsigned char      undefined1;
typedef unsigned int       undefined4;
typedef unsigned int       uint;
typedef long long          longlong;
typedef unsigned long long ulonglong;
typedef unsigned long long undefined8;
typedef int (*code)(void);

static inline unsigned long long CONCAT44(unsigned int hi, unsigned int lo) {
    return ((unsigned long long)hi << 32) | (unsigned long long)lo;
}

extern "C" {
// ---- scan context enter/leave ----
undefined8 FUN_803826c0(void);
void       FUN_8038270c(void);

// ---- helpers ----
int    sprite_measure_text_width(float *worldPos, int *outX, int *outY);
double math_sqrt(double x);
int    scene_raycast(undefined4 world, int origin, float *dst, int a4, float *a5, float *a6,
                     int *a7, uint mask, int a9, int *a10);

// ---- callback globals (installed by target_scan_set_callbacks) ----
extern void *DAT_805cb920;   // filter(ctx, ent)
extern void *DAT_805cb924;   // scoreDistance
extern void *DAT_805cb928;   // scoreAngle
extern void *DAT_805cb92c;   // visibility / LOS override
extern void *DAT_805cb930;   // screenGate

// ---- raw globals ----
extern int  DAT_80611d54;    // active camera object
extern int  DAT_80611d74;    // local player context base
extern int  DAT_80611748;    // screen viewport params (center x/y at +0x10/+0x24, half-extents +8/+0x1c)
extern void *DAT_80540a44;   // active entity list head
extern void *DAT_80611c50;   // entity-list iterator scratch
extern float FLOAT_80618f60;
extern double DOUBLE_80618e90, DOUBLE_80618f80;

// callback typedefs
typedef int    (*FilterFn)(undefined4 ctx, void *ent);
typedef double (*ScoreDistFn)(double dist, double dx, double dy, undefined4 ctx, void *ent);
typedef double (*ScoreAngleFn)(double dx, double dy, undefined4 ctx, void *ent);
typedef int    (*VisFn)(double a1, double a2, double radius, undefined4 ctx, void *ent);
typedef int    (*ScreenGateFn)(double sx, double sy, double radius, undefined4 ctx, void *ent);

// ============================================================================
// 0x8025a41c  target_scan_set_callbacks
// Installs the five target-scan callbacks (filter / scoreDistance / scoreAngle /
// visibility / screenGate) into the DAT_805cb920..930 globals for the next
// target_scan_screen_entities pass. Note the parameter->global mapping: param_2
// is stored last (into +0x10 = screenGate).
// ============================================================================
void target_scan_set_callbacks(undefined4 param_1, undefined4 param_2, undefined4 param_3,
                               undefined4 param_4, undefined4 param_5)
{
    DAT_805cb920 = (void *)param_1;
    DAT_805cb924 = (void *)param_3;
    DAT_805cb928 = (void *)param_4;
    DAT_805cb92c = (void *)param_5;
    DAT_805cb930 = (void *)param_2;
}

// ============================================================================
// 0x8025a438  target_scan_test_entity
// Cheap single-entity variant: returns 1 if the given entity (param_4) is a live,
// selectable candidate whose (up to 2, for type-8 anchored entities) sample
// points are within radius param_3 of the camera and successfully project to
// screen space via sprite_measure_text_width. Returns 0 otherwise.
// ============================================================================
undefined4 target_scan_test_entity(undefined8 param_1, undefined8 param_2, double param_3, int param_4)
{
    bool   bVar1;
    float  fVar2, fVar3, fVar4;
    undefined4 uVar5;
    int    iVar6, iVar8, iVar9;
    float *pfVar7;
    double dVar10;
    float  afStack_3c[6];
    undefined4 local_24, local_20, local_1c;
    (void)param_1; (void)param_2;

    if (param_4 == 0) {
        uVar5 = 0;
    } else if ((((*(uint *)(param_4 + 0x10) & 0xffff) == 0) ||
                ((*(uint *)(param_4 + 0x28) & 0x80000000) != 0)) ||
               ((*(uint *)(param_4 + 0x28) & 1) == 0)) {
LAB_8025a560:
        uVar5 = 0;
    } else {
        iVar8 = 1;
        afStack_3c[3] = *(float *)(param_4 + 100)  + *(float *)(param_4 + 0x1a8);
        afStack_3c[4] = *(float *)(param_4 + 0x68) + *(float *)(param_4 + 0x1ac);
        afStack_3c[5] = *(float *)(param_4 + 0x6c) + *(float *)(param_4 + 0x1b0);
        if (*(int *)(param_4 + 0x20) == 8) {
            iVar6 = *(int *)(param_4 + 0x7c);
            iVar8 = 2;
            local_24 = *(undefined4 *)(iVar6 + 0xbb4);
            local_20 = *(undefined4 *)(iVar6 + 3000);
            local_1c = *(undefined4 *)(iVar6 + 0xbbc);
        }
        dVar10 = (double)(float)(param_3 * param_3);
        iVar6 = iVar8 * 0xc;
        do {
            bVar1 = iVar8 == 0;
            iVar8 = iVar8 + -1;
            iVar9 = iVar6 + -0xc;
            if (bVar1) goto LAB_8025a560;
            pfVar7 = (float *)((int)afStack_3c + iVar6);
            fVar2 = *(float *)((int)afStack_3c + iVar6 + 4) - *(float *)(DAT_80611d54 + 0x280);
            fVar3 = *pfVar7 - *(float *)(DAT_80611d54 + 0x27c);
            fVar4 = *(float *)((int)afStack_3c + iVar6 + 8) - *(float *)(DAT_80611d54 + 0x284);
            iVar6 = iVar9;
        } while ((dVar10 <= (double)(fVar4 * fVar4 + fVar3 * fVar3 + fVar2 * fVar2)) ||
                 (iVar9 = sprite_measure_text_width(pfVar7, (int *)(afStack_3c + 2), (int *)(afStack_3c + 1)),
                  iVar9 != 0));
        uVar5 = 1;
    }
    return uVar5;
}

// ============================================================================
// 0x8025a584  target_scan_screen_entities
// Full screen-space target scan. Walks the active entity list (DAT_80540a44),
// filtering with DAT_805cb920, and for each live candidate projects its sample
// point(s) to screen space (sprite_measure_text_width) relative to the search
// center (screen params DAT_80611748 offset by the param_2 aim bias). Candidates
// inside the |dx|<param_3, |dy|<param_4 box and within radius param_5 are scored
// (DAT_805cb924/928/92c) and collected (up to 20). The list is insertion-sorted
// by score, then LOS-raycast from the camera in best-first order (or via the
// DAT_805cb92c override); the first unobstructed entity is written to *piVar9 and
// its camera distance to the out-float (uVar27 lo word).
// ============================================================================
void target_scan_screen_entities(undefined8 param_1, double param_2, double param_3, double param_4,
                                 double param_5, undefined4 param_6, undefined4 param_7, undefined4 param_8)
{
    bool   bVar1;
    float  fVar2, fVar3;
    undefined4 uVar4, uVar5, uVar6, uVar7, uVar13;
    undefined4 *puVar8;
    int   *piVar9;
    int    iVar10, iVar12, iVar17, iVar19, iVar20, iVar21, iVar22;
    uint   uVar11, uVar14, uVar15, uVar16;
    float *pfVar18;
    double dVar23, dVar24, dVar25, dVar26;
    undefined8 uVar27;
    int    local_2c0, local_2bc;
    float  local_2b8[6];
    undefined4 local_2a0, local_29c, local_298;
    float  local_294[20];
    int    local_244[20];
    float  local_1f4[61];
    (void)param_1; (void)param_6; (void)param_7;

    uVar27 = FUN_803826c0();
    piVar9 = (int *)(uint)((ulonglong)uVar27 >> 0x20);
    *piVar9 = 0;
    dVar26 = (double)(float)(param_5 * param_5);
    iVar22 = 0;
    iVar21 = 0;
    iVar20 = 0;
    /* search-box center in screen space: viewport center +/- aim bias */
    int centerX = *(int *)(DAT_80611748 + 8) +
        (int)(param_2 * (double)(float)(FLOAT_80618f60 * (float)*(int *)(DAT_80611748 + 0x10)));
    int centerY = *(int *)(DAT_80611748 + 0x1c) +
        (int)(param_2 * (double)(float)(FLOAT_80618f60 * (float)*(int *)(DAT_80611748 + 0x24)));
    uVar15 = (uint)centerX;
    uVar14 = (uint)centerY;
    puVar8 = (undefined4 *)DAT_80540a44;
    dVar24 = param_2;
    while (DAT_80611c50 = puVar8, puVar8 != (undefined4 *)0x0) {
        uVar11 = puVar8[10];
        if ((((((uVar11 & 0x60) != 0) && ((puVar8[4] & 0xffff) != 0)) && ((uVar11 & 0x80000000) == 0)) &&
             ((uVar11 & 1) != 0)) &&
            ((DAT_805cb920 == (void *)0 || (iVar12 = ((FilterFn)DAT_805cb920)(param_8, puVar8), iVar12 != 0)))) {
            iVar12 = 1;
            local_2b8[3] = (float)puVar8[0x19] + (float)puVar8[0x6a];
            local_2b8[4] = (float)puVar8[0x1a] + (float)puVar8[0x6b];
            local_2b8[5] = (float)puVar8[0x1b] + (float)puVar8[0x6c];
            if (puVar8[8] == 8) {
                iVar17 = puVar8[0x1f];
                iVar12 = 2;
                local_2a0 = *(undefined4 *)(iVar17 + 0xbb4);
                local_29c = *(undefined4 *)(iVar17 + 3000);
                local_298 = *(undefined4 *)(iVar17 + 0xbbc);
            }
            iVar17 = iVar12 * 0xc;
LAB_8025aa10:
            iVar19 = iVar17;
            bVar1 = iVar12 != 0;
            iVar12 = iVar12 + -1;
            iVar17 = iVar19 + -0xc;
            if (bVar1) {
                pfVar18 = (float *)((int)local_2b8 + iVar19);
                local_2b8[0] = *pfVar18 - *(float *)(DAT_80611d54 + 0x27c);
                local_2b8[1] = *(float *)((int)local_2b8 + iVar19 + 4) - *(float *)(DAT_80611d54 + 0x280);
                local_2b8[2] = *(float *)((int)local_2b8 + iVar19 + 8) - *(float *)(DAT_80611d54 + 0x284);
                dVar25 = (double)(local_2b8[2] * local_2b8[2] +
                                  local_2b8[0] * local_2b8[0] + local_2b8[1] * local_2b8[1]);
                if ((dVar25 < dVar26) &&
                    (iVar10 = sprite_measure_text_width(pfVar18, &local_2bc, &local_2c0), iVar10 == 0)) {
                    uVar16 = uVar15 - (uint)local_2bc;
                    uVar11 = uVar14 - (uint)local_2c0;
                    int adx = (int)(((int)uVar16 >> 0x1f ^ uVar16) - ((int)uVar16 >> 0x1f));
                    int ady = (int)(((int)uVar11 >> 0x1f ^ uVar11) - ((int)uVar11 >> 0x1f));
                    if (((double)(float)adx < param_3) && ((double)(float)ady < param_4) && (iVar22 < 0x14)) {
                        if (DAT_805cb930 != (void *)0) {
                            iVar10 = ((ScreenGateFn)DAT_805cb930)(
                                (double)(float)(int)uVar15, (double)(float)(int)uVar14,
                                param_5, param_8, puVar8);
                            if (iVar10 == 0) goto LAB_8025aa10;
                        }
                        if (DAT_805cb924 == (void *)0) {
                            *(float *)((int)local_294 + iVar20) = (float)dVar25;
                        } else {
                            dVar23 = ((ScoreDistFn)DAT_805cb924)(dVar25, (double)(float)(int)uVar16,
                                                                 (double)(float)(int)uVar11, param_8, puVar8);
                            dVar25 = ((ScoreDistFn)DAT_805cb924)(dVar25, (double)(float)(int)uVar16,
                                                                 (double)(float)(int)uVar11, param_8, puVar8);
                            *(float *)((int)local_294 + iVar20) = (float)(dVar25 * dVar23);
                        }
                        if (DAT_805cb928 != (void *)0) {
                            dVar25 = ((ScoreAngleFn)DAT_805cb928)((double)(float)(int)uVar16,
                                                                  (double)(float)(int)uVar11, param_8, puVar8);
                            *(float *)((int)local_294 + iVar20) =
                                -(float)(dVar25 * dVar25 - (double)*(float *)((int)local_294 + iVar20));
                        }
                        *(float *)((int)local_1f4 + iVar21) = *pfVar18;
                        iVar22 = iVar22 + 1;
                        uVar4 = *(undefined4 *)((int)local_2b8 + iVar19 + 8);
                        *(undefined4 *)((int)local_1f4 + iVar21 + 4) = *(undefined4 *)((int)local_2b8 + iVar19 + 4);
                        *(undefined4 **)((int)local_244 + iVar20) = puVar8;
                        iVar20 = iVar20 + 4;
                        *(undefined4 *)((int)local_1f4 + iVar21 + 8) = uVar4;
                        iVar21 = iVar21 + 0xc;
                    }
                }
                goto LAB_8025aa10;
            }
            if (0x13 < iVar22) break;
        }
        puVar8 = (undefined4 *)*(int *)DAT_80611c50;
    }
    if (iVar22 != 0) {
        for (iVar12 = iVar22 + -2; -1 < iVar12; iVar12 = iVar12 + -1) {
            iVar17 = iVar12 + 1;
            bVar1  = true;
            iVar21 = 0;
            iVar20 = 0;
            if (-1 < iVar12) {
                do {
                    fVar2 = *(float *)((int)local_294 + iVar20 + 4);
                    fVar3 = *(float *)((int)local_294 + iVar20);
                    if (fVar2 < fVar3) {
                        *(float *)((int)local_294 + iVar20) = fVar2;
                        uVar4 = *(undefined4 *)((int)local_1f4 + iVar21);
                        uVar5 = *(undefined4 *)((int)local_1f4 + iVar21 + 4);
                        bVar1 = false;
                        uVar6 = *(undefined4 *)((int)local_1f4 + iVar21 + 8);
                        uVar13 = *(undefined4 *)((int)local_244 + iVar20);
                        *(undefined4 *)((int)local_1f4 + iVar21) = *(undefined4 *)((int)local_1f4 + iVar21 + 0xc);
                        uVar7 = *(undefined4 *)((int)local_1f4 + iVar21 + 0x10);
                        *(undefined4 *)((int)local_244 + iVar20) = *(undefined4 *)((int)local_244 + iVar20 + 4);
                        *(undefined4 *)((int)local_1f4 + iVar21 + 4) = uVar7;
                        uVar7 = *(undefined4 *)((int)local_1f4 + iVar21 + 0x14);
                        *(undefined4 *)((int)local_244 + iVar20 + 4) = uVar13;
                        *(undefined4 *)((int)local_1f4 + iVar21 + 8) = uVar7;
                        *(undefined4 *)((int)local_1f4 + iVar21 + 0xc) = uVar4;
                        *(undefined4 *)((int)local_1f4 + iVar21 + 0x10) = uVar5;
                        *(float *)((int)local_294 + iVar20 + 4) = fVar3;
                        *(undefined4 *)((int)local_1f4 + iVar21 + 0x14) = uVar6;
                    }
                    iVar21 = iVar21 + 0xc;
                    iVar20 = iVar20 + 4;
                    iVar17 = iVar17 + -1;
                } while (iVar17 != 0);
            }
            if (bVar1) break;
        }
        iVar17 = 0;
        iVar21 = 0;
        uVar15 = *(uint *)(*(int *)(DAT_80611d74 + 0x14) + 0x28);
        uVar14 = uVar15 & 0x60;
        *(uint *)(*(int *)(DAT_80611d74 + 0x14) + 0x28) = uVar15 & ~uVar14;
        for (iVar12 = 0; iVar12 < iVar22; iVar12 = iVar12 + 1) {
            if (DAT_805cb92c == (void *)0) {
                local_2b8[0] = *(float *)((int)local_1f4 + iVar17);
                uVar11 = *(uint *)(*(int *)((int)local_244 + iVar21) + 0x28);
                local_2b8[1] = *(float *)((int)local_1f4 + iVar17 + 4);
                uVar15 = uVar11 & 0x60;
                local_2b8[2] = *(float *)((int)local_1f4 + iVar17 + 8);
                *(uint *)(*(int *)((int)local_244 + iVar21) + 0x28) = uVar11 & ~uVar15;
                local_2b8[0] = local_2b8[0] - *(float *)(DAT_80611d54 + 0x27c);
                local_2b8[1] = local_2b8[1] - *(float *)(DAT_80611d54 + 0x280);
                local_2b8[2] = local_2b8[2] - *(float *)(DAT_80611d54 + 0x284);
                iVar20 = scene_raycast(*(undefined4 *)(DAT_80611d54 + 0x2a0), DAT_80611d54 + 0x27c,
                                       local_2b8, 0, (float *)0, (float *)0, (int *)0, 0x2010070, 0x18, (int *)0);
                if (iVar20 == 0) {
                    *(uint *)(*(int *)((int)local_244 + iVar21) + 0x28) =
                        *(uint *)(*(int *)((int)local_244 + iVar21) + 0x28) | uVar15;
                    goto LAB_8025ac28;
                }
                *(uint *)(*(int *)((int)local_244 + iVar21) + 0x28) =
                    *(uint *)(*(int *)((int)local_244 + iVar21) + 0x28) | uVar15;
            } else {
                iVar20 = ((VisFn)DAT_805cb92c)(dVar24, param_2, param_5, param_8,
                                               *(void **)((int)local_244 + iVar21));
                if (iVar20 != 0) {
LAB_8025ac28:
                    iVar12 = local_244[iVar12];
                    *piVar9 = iVar12;
                    if ((float *)(uint)uVar27 != (float *)0) {
                        local_2b8[0] = *(float *)(iVar12 + 100)  - *(float *)(DAT_80611d54 + 0x27c);
                        local_2b8[1] = *(float *)(iVar12 + 0x68) - *(float *)(DAT_80611d54 + 0x280);
                        local_2b8[2] = *(float *)(iVar12 + 0x6c) - *(float *)(DAT_80611d54 + 0x284);
                        dVar24 = math_sqrt((double)(local_2b8[2] * local_2b8[2] +
                                                    local_2b8[0] * local_2b8[0] + local_2b8[1] * local_2b8[1]));
                        *(float *)(uint)uVar27 = (float)dVar24;
                    }
                    break;
                }
            }
            iVar17 = iVar17 + 0xc;
            iVar21 = iVar21 + 4;
        }
        *(uint *)(*(int *)(DAT_80611d74 + 0x14) + 0x28) =
            *(uint *)(*(int *)(DAT_80611d74 + 0x14) + 0x28) | uVar14;
    }
    FUN_8038270c();
}

} // extern "C"
