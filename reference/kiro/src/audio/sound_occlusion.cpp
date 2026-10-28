// sound_occlusion.cpp
// Per-frame sound-occlusion / line-of-sight sweep for the audio system. Driven
// from session_tick_by_mode, this incrementally walks the (listener x source)
// matrix - every local player (listener) against every other player and every
// active sound-emitting event (source) - one pair per call cursor
// (DAT_80610fe8 = listener index, DAT_80610fec = source index) up to a budget of
// player_count * (teammate_count + reverb_state). For each pair it:
//   - resolves the source world position (player bone-space transform, or an
//     event's origin),
//   - computes the listener->source direction/distance
//     (sound_source_compute_dot_distance) into a per-source occlusion record,
//   - and if the source is within the audible speed/range, raycasts the scene
//     (scene_raycast) to decide the occlusion bits (1 = blocked LOS, 2 = partially
//     blocked) stored in that record.
// The sweep stops early once a pair has been fully processed this frame (bVar1).
//
// Address range: 0x802581ec - 0x802581ec  (batch_733)
//
// Self-contained translation unit. The player context/player-array symbols are
// declared file-locally; DAT_80611d78 (player array base) is an int address used
// with an explicit stride of 0x141c bytes.

typedef unsigned int       undefined4;
typedef unsigned int       uint;
typedef unsigned long long undefined8;
typedef unsigned long long ulonglong;

static inline unsigned long long CONCAT44(unsigned int hi, unsigned int lo) {
    return ((unsigned long long)hi << 32) | (unsigned long long)lo;
}

extern "C" {
// ---- occlusion-sweep context enter/leave ----
undefined8 FUN_803826d0(void);
void       FUN_8038271c(void);

// ---- helpers ----
int    soundpad_get_current_reverb_state(void);
int    event_get_type_data(int idx);
void   mtx4_set_translate_rotz(double x, double y, double z, double zAngle, float *mtxOut);
void   mtx4_multiply(float *a, float *b, float *out);
void   sound_source_compute_dot_distance(int srcVec, float *pos, int out);
double player_get_min_speed(int player);
double player_get_movement_speed(int player, int which);
int    scene_raycast(undefined4 world, int origin, float *dst, int a4, float *a5, float *a6,
                     int *a7, uint mask, int a9, int *a10);

// ---- raw globals ----
extern int  DAT_80611d78;   // player array base (stride 0x141c)
extern int  DAT_80612610;   // player (listener) count
extern int  DAT_80612098;   // teammate count
extern int  DAT_80610fe8;   // sweep cursor: current listener index
extern int  DAT_80610fec;   // sweep cursor: current source index
extern int  DAT_80588610;   // player/teammate pointer table
extern char DAT_805015bd;   // co-op mode
extern float FLOAT_80618e58, FLOAT_80618e64, FLOAT_80618e68, FLOAT_80618e70, FLOAT_80618e74;
extern char DAT_8043e22c;   // per-anim-state flags table (stride 0xf)

// ============================================================================
// 0x802581ec  sound_occlusion_sweep_pairs
// See file header. Incremental listener x source occlusion sweep, one pair per
// invocation cursor. Called from session_tick_by_mode.
// ============================================================================
void sound_occlusion_sweep_pairs(void)
{
    bool   bVar1;
    int    iVar2, iVar4, iVar8, iVar9, iVar10, iVar11, iVar12;
    int   *piVar3;
    uint   uVar5, uVar6;
    uint  *puVar7;
    double dVar13, dVar14;
    float  local_d0, local_cc, local_c8;
    float  afStack_c4[12];
    float  local_94, local_90, local_8c;
    float  afStack_84[31];

    FUN_803826d0();
    bVar1  = false;
    iVar11 = 0;
    iVar2  = soundpad_get_current_reverb_state();
    iVar2  = DAT_80612098 + iVar2;
    iVar10 = DAT_80612610 * iVar2;
LAB_80258624:
    do {
        do {
            while (true) {
                if ((bVar1) || (iVar10 <= iVar11)) {
                    FUN_8038271c();
                    return;
                }
                iVar11 = iVar11 + 1;
                DAT_80610fec = DAT_80610fec + 1;
                iVar4 = (&DAT_80588610)[DAT_80610fe8];
                if (DAT_80612098 < DAT_80610fec) {
                    piVar3 = (int *)0x0;
                } else {
                    piVar3 = (int *)(&DAT_80588610)[DAT_80610fec];
                }
                if (DAT_80610fec < iVar2) break;
                DAT_80610fec = 0;
                piVar3 = (int *)DAT_80588610;
                if (DAT_80612098 < 0) { piVar3 = (int *)0x0; }
                DAT_80610fe8 = DAT_80610fe8 + 1;
                if (DAT_80612610 <= DAT_80610fe8) { DAT_80610fe8 = 0; }
                iVar4 = (&DAT_80588610)[DAT_80610fe8];
                if (iVar4 != 0) break;
                iVar11 = iVar11 + iVar2;
                DAT_80610fec = iVar2;
            }
        } while (iVar4 == 0);
        iVar12 = DAT_80611d78 + *(int *)(iVar4 + 8) * 0x141c;
    } while ((((*(int *)(iVar12 + 0x114) == 0x400) || (*(int *)(iVar12 + 0x1390) == 0x27)) ||
              (piVar3 == (int *)0x0)) || (*(int *)(iVar4 + 8) == piVar3[2]));

    uVar6 = 0x2010278;
    iVar4 = 0;
    iVar9 = *(int *)(*(int *)(iVar12 + 0x14) + 0x7c);
    if (DAT_80610fec < DAT_80612098) {
        puVar7 = (uint *)(*(int *)(iVar12 + 0xe0c) + piVar3[2] * 0x10);
        *puVar7 = 0;
        if (((DAT_80612610 <= DAT_80610fec) && ((*(uint *)(*piVar3 + 0x10) & 0xffff) == 0)) ||
            (((piVar3[9] & 8U) != 0 || (*(int *)(iVar9 + 0x84) == piVar3[0x21])))) { goto LAB_80258624; }
        if (DAT_805015bd == '\n') {
            uVar5 = piVar3[5];
        } else {
            uVar5 = piVar3[7];
        }
        if ((uVar5 & 0x18000) != 0) { goto LAB_80258624; }
    } else {
        iVar8 = DAT_80610fec - DAT_80612098;
        iVar4 = event_get_type_data(iVar8);
        puVar7 = (uint *)(*(int *)(iVar12 + 0xe0c) + (iVar8 + 0x20) * 0x10);
        *puVar7 = 0;
        if (((*(uint *)(iVar4 + 0x28) & 0x80000000) != 0) ||
            (*(float *)(iVar4 + 0x1cc) <= FLOAT_80618e58)) { goto LAB_80258624; }
        uVar6 = 0x20502f8;
        piVar3 = (int *)0x0;
    }
    if ((*(uint *)(iVar9 + 0x24) & 8) == 0) {
        if (piVar3 == (int *)0x0) {
            local_d0 = *(float *)(iVar4 + 100)  + *(float *)(iVar4 + 0x1a8);
            local_cc = *(float *)(iVar4 + 0x68) + *(float *)(iVar4 + 0x1ac);
            local_c8 = *(float *)(iVar4 + 0x6c) + *(float *)(iVar4 + 0x1b0);
        } else {
            iVar4 = *piVar3;
            mtx4_set_translate_rotz(
                (double)*(float *)(iVar4 + 100), (double)*(float *)(iVar4 + 0x68),
                (double)*(float *)(iVar4 + 0x6c),
                (double)((FLOAT_80618e74 * *(float *)(iVar4 + 0x70)) / FLOAT_80618e70), afStack_84);
            mtx4_multiply((float *)(*(int *)(*(int *)(*(int *)(*piVar3 + 4) + 0x10)) +
                                    *(int *)(*(int *)(*(int *)(*piVar3 + 4) + 0x84) + 0x18) * 0x40),
                          afStack_84, afStack_c4);
            local_d0 = local_94;
            local_cc = local_90;
            local_c8 = local_8c;
            if (((&DAT_8043e22c)[*(int *)(iVar9 + 0x94) * 0xf] & 0x40) == 0) {
                local_cc = local_90 - FLOAT_80618e68;
            }
        }
        local_d0 = local_d0 - *(float *)(iVar12 + 0xb4);
        local_cc = local_cc - *(float *)(iVar12 + 0xb8);
        local_c8 = local_c8 - *(float *)(iVar12 + 0xbc);
        sound_source_compute_dot_distance(iVar12 + 0xc0, &local_d0, (int)puVar7);
        dVar13 = player_get_min_speed(iVar12);
        if (*(int *)(iVar12 + 0xec0) < 1) {
            dVar14 = (double)FLOAT_80618e64;
        } else {
            dVar14 = (double)*(float *)(iVar12 + 0xec4);
            if (dVar14 < dVar13) { dVar13 = dVar14; }
        }
        if (dVar13 < (double)(float)puVar7[2]) {
            iVar4 = scene_raycast(*(undefined4 *)(iVar12 + 0xdec), iVar12 + 0xb4, &local_d0, 0,
                                  (float *)0, (float *)0, (int *)0, uVar6, 1, (int *)0);
            if ((dVar14 < (double)(float)puVar7[2]) && (iVar4 == 0)) { *puVar7 = *puVar7 | 1; }
            dVar13 = player_get_movement_speed(iVar12, 2);
            if (dVar13 < (double)(float)puVar7[2]) { bVar1 = true; }
            dVar13 = player_get_movement_speed(iVar12, 0);
            if (((dVar13 < (double)(float)puVar7[2]) ||
                 (dVar13 = player_get_movement_speed(iVar12, 1), dVar13 < (double)(float)puVar7[2])) &&
                (bVar1 = true, iVar4 == 0)) {
                *puVar7 = *puVar7 | 2;
            }
        }
    }
    goto LAB_80258624;
}

} // extern "C"
