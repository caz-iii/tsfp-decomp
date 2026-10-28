// Address range: 0x8013ebb4 - 0x8013f74b (subset) | batch_372 | 2026-09-05
//
// AI actor aim/facing tests, bulk activation toggles, squad lookup and active-
// registration. Continuation of batches 364-371. Per-actor render object =
// *(actor + 0x7c). DAT_805015bd = game mode byte (0x0a coop). DAT_80588610
// visible-node ptr table (count DAT_80612098), DAT_80611d90 player state pool
// (0xea0), DAT_80612610 base count, DAT_806120fc extra-bot count. FUN_803826xx =
// fp scratch-frame helpers. Ghidra fast-inverse-sqrt idiom recurs throughout:
// 1/SQRT via one Newton step scaled by FLOAT_80615db0/b4/b8 constants.
//
// Functions:
//   aiactor_deactivate_all_idle  0x8013ebb4  deactivate idle visible actors
//   aiactor_activate_all_valid   0x8013ec9c  activate valid bots/players
//   aiactor_find_squad_index     0x8013ed54  squad slot by room id
//   aiactor_dir_alignment_test   0x8013eda8  horizontal dir dot >= threshold
//   aiactor_facing_dot_test      0x8013ee44  facing-matrix dot vs target dir
//   aiactor_aim_alignment_test   0x8013ef50  weapon-aware aim-vector alignment
//   aiactor_zone_facing_test     0x8013f2a8  zone-yaw rotated ref dot vs dir
//   aiactor_classify_aim_cone    0x8013f3ec  classify target dir into 0-4 cones
//   aiactor_aim_at_target        0x8013f5c0  compute + apply aim to current tgt
//   aiactor_register_active      0x8013f70c  append to active table (cap 0x36)

#include "common.h"

// Ghidra SQRT intrinsic (single-precision), self-contained Newton-Raphson.
static inline float SQRT_(float x) {
    if (x <= 0.0f) return 0.0f;
    float g = x;
    for (int i = 0; i < 6; i = i + 1) g = 0.5f * (g + x / g);
    return g;
}

// ---- named cross-module helpers (self-externed; common.h has no decls) -----
extern "C" {
extern u32  aiactor_activate(int actor);                       // aiactor_activation.cpp
extern u32  aiactor_deactivate(int actor);                     // aiactor_activation.cpp
extern int  player_ai_is_heavy_weapon(int weaponType);
extern void player_ai_transform_aim_to_world(double spin, float* origin, float* boneMtx,
                                             u32 boneCount, float* outDir);
extern void player_ai_get_aim_world_position(int actor, float* origin, float* outDir);
extern void entity_build_local_matrix(void* outMtx, int actor, int a, int b);
extern void mtx4_set_rotation_y(double yaw, float* outMtx);
extern void mtx4_transform_vector_alias(float* mtx, float* vecInOut);
extern double math_sqrt(double x);
}

// ---- raw data symbols ------------------------------------------------------
extern char  DAT_805015bd;    // game mode byte (0x0a coop)
extern int   DAT_80588610;    // visible-node ptr table
extern int   DAT_80612098;    // visible-node count
extern int   DAT_80611d90;    // player state pool base (0xea0 stride)
extern int   DAT_80612610;    // base player/actor count
extern int   DAT_806120fc;    // active extra-bot count
extern int   DAT_806120b0;    // squad-slot count
extern int   DAT_80588884;    // squad-slot value array (8 stride)
extern int   DAT_80588888;    // squad-slot entity array (8 stride)
extern int   DAT_80611b8c;    // zone table base (0x20 stride)
extern int   DAT_80612100;    // active-table count
extern int   DAT_80588a9c;    // active-table arg array (2 words / entry)
extern int   DAT_80588aa0;    // active-table value array (8 stride)
extern float DAT_8048ca94;    // per-weapon aim-height table (0x30 stride)
extern int   DAT_80583228;    // sine/cosine lookup table (0x800 entries)
extern float DAT_803ccab8;    // zone-facing ref vector x
extern float DAT_803ccabc;    // zone-facing ref vector y
extern float DAT_803ccac0;    // zone-facing ref vector z
extern float DAT_803ccac4;    // aim-cone ref vector x
extern float DAT_803ccac8;    // aim-cone ref vector y
extern float DAT_803ccacc;    // aim-cone ref vector z

extern float FLOAT_80615da8;  // 0.0 (y term placeholder)
extern float FLOAT_80615dac;  // min length-sq for normalise
extern float FLOAT_80615db0;  // 1.5 (fast-invsqrt) / half-step scale
extern float FLOAT_80615db4;  // 1.5 (fast-invsqrt)
extern float FLOAT_80615db8;  // 1.0
extern float FLOAT_80615dbc;  // yaw->radians scale numerator
extern float FLOAT_80615dc0;  // yaw->radians scale denominator
extern float FLOAT_80615dc4;  // lut index scale
extern float FLOAT_80615dc8;  // aim-cone upper bound
extern float FLOAT_80615dcc;  // aim-cone lower bound

// ---- raw function symbols --------------------------------------------------
extern "C" {
extern int  FUN_803826f0(void);            // fp frame enter
extern void FUN_8038273c(void);            // fp frame leave
extern void FUN_8037efc8(int actor, float* outDir);       // heavy-weapon aim dir
extern void FUN_801af0a8(int actor, float* dir, int target, int flags);  // apply aim
}

// FUN_803826f0 returns the packed 64-bit gpr scratch frame (actor in high word,
// a local/remote flag in the low word); call via this cast where both are needed.
typedef unsigned long long (*frame_enter_ull)(void);

// aiactor_register_active 0x8013f70c
void aiactor_register_active(u32 value, u32 arg)
{
    if (0x36 < DAT_80612100) return;
    int vOff = DAT_80612100 * 8;
    int aOff = DAT_80612100 * 2;
    DAT_80612100 = DAT_80612100 + 1;
    (&DAT_80588a9c)[aOff] = (int)arg;
    *(int*)(&DAT_80588aa0 + vOff) = (int)value;
}

// aiactor_find_squad_index 0x8013ed54
// Returns the squad-slot value whose slot entity's room id (+0x1c) matches the
// actor's room id (+0x84), or -1.
int aiactor_find_squad_index(int actor)
{
    int off = 0;
    int remaining = DAT_806120b0;
    if (0 < DAT_806120b0) {
        do {
            if (*(int*)(*(int*)(actor + 0x7c) + 0x84) ==
                *(int*)(*(int*)(*(int*)((int)&DAT_80588888 + off) + 0x7c) + 0x1c)) {
                return *(int*)((int)&DAT_80588884 + off);
            }
            off = off + 8;
            remaining = remaining + -1;
        } while (remaining != 0);
    }
    return -1;
}

// aiactor_deactivate_all_idle 0x8013ebb4
// Deactivates every visible actor that is not pinned (rd[0xb]&2), not in a
// special state (rd[5]&0x200000) and not receiving input; compensates the loop
// index when a deactivation swap-removes the current entry.
void aiactor_deactivate_all_idle(void)
{
    FUN_803826f0();
    for (int i = 0; i < DAT_80612098; i = i + 1) {
        int* rd = (int*)(&DAT_80588610)[i];
        if (((rd[0xb] & 2U) == 0) && (((u32)rd[5] & 0x200000) == 0)) {
            u32 mask = (DAT_805015bd != '\n') ? (u32)rd[7] : (u32)rd[5];
            if (((mask & 1) == 0) && (aiactor_deactivate(*rd) != 0)) {
                i = i + -1;
            }
        }
    }
    FUN_8038273c();
}

// aiactor_activate_all_valid 0x8013ec9c
// Activates every actor/bot that has input pending or is a base player.
void aiactor_activate_all_valid(void)
{
    FUN_803826f0();
    int idx = 0;
    int poolOff = 0;
    while (idx < DAT_80612610 + DAT_806120fc) {
        int* rd = (int*)(DAT_80611d90 + poolOff);
        if ((rd[0xb] & 2U) == 0) {
            int inA, inB;
            if (DAT_805015bd == '\n') { inA = rd[4]; inB = rd[5]; }
            else                      { inA = rd[6]; inB = rd[7]; }
            if ((inB != 0 || inA != 0) || (rd[2] < DAT_80612610)) {
                aiactor_activate(*rd);
            }
        }
        idx = idx + 1;
        poolOff = poolOff + 0xea0;
    }
    FUN_8038273c();
}

// aiactor_dir_alignment_test 0x8013eda8
// Returns 1 if the normalised horizontal direction (p2->p4) dotted with p3
// exceeds the threshold param_1.
u32 aiactor_dir_alignment_test(double threshold, float* p2, float* p3, float* p4)
{
    float dx = p4[0] - p2[0];
    float dz = p4[2] - p2[2];
    float lenSq = dz * dz + dx * dx + FLOAT_80615da8;
    float dy = FLOAT_80615da8;
    if (FLOAT_80615dac < lenSq) {
        float inv = 1.0f / SQRT_(lenSq);
        inv = inv * -(FLOAT_80615db0 * lenSq * inv * inv - FLOAT_80615db4);
        lenSq = FLOAT_80615db8 * inv * -(FLOAT_80615db0 * lenSq * inv * inv - FLOAT_80615db4);
        dx = dx * lenSq;
        dy = FLOAT_80615da8 * lenSq;
        dz = dz * lenSq;
    }
    if (threshold < (double)(dz * p3[2] + dx * p3[0] + dy * p3[1])) {
        return 1;
    }
    return 0;
}

// aiactor_facing_dot_test 0x8013ee44
// Builds the actor's local matrix and returns whether the normalised direction
// to param_3 dotted with the matrix forward row exceeds param_1.
bool aiactor_facing_dot_test(double threshold, int actor, float* targetPos)
{
    double dy = (double)FLOAT_80615da8;
    double dx = (double)(targetPos[0] - *(float*)(actor + 100));
    double dz = (double)(targetPos[2] - *(float*)(actor + 0x6c));
    float  mtx[32];

    float lenSq = (float)(dz * dz + (double)(float)(dx * dx + dy));
    if ((dy != threshold) && (FLOAT_80615dac < lenSq)) {
        float inv = 1.0f / SQRT_(lenSq);
        inv = inv * -(FLOAT_80615db0 * lenSq * inv * inv - FLOAT_80615db4);
        double scale = (double)(FLOAT_80615db8 * inv *
                       -(FLOAT_80615db0 * lenSq * inv * inv - FLOAT_80615db4));
        dx = (double)(float)(dx * scale);
        dy = (double)(float)(dy * scale);
        dz = (double)(float)(dz * scale);
    }
    entity_build_local_matrix(mtx, actor, 0, 0);
    // matrix forward row is at float offsets 8/9/10 (local_68/64/60 in Ghidra)
    return threshold < (double)(float)(dz * (double)mtx[10] +
                                       (double)(float)(dx * (double)mtx[8] +
                                                       (double)(float)(dy * (double)mtx[9])));
}

// aiactor_aim_alignment_test 0x8013ef50
// Computes the actor's current aim direction (weapon-class specific: sniper LUT,
// heavy pass-through, or bone/aim-position derived) and returns whether the
// normalised direction to param_3 dotted with it exceeds param_1.
bool aiactor_aim_alignment_test(double threshold, int actor, float* targetPos)
{
    u32   lutIdx;
    float fVar2, fVar3;
    int   rd;
    float ax, ay, az;         // local_ec/e8/e4 aim vector
    float origin[3];          // local_f8/f4/f0
    float scratch[4];
    float sn0, sn1;           // afStack_a0 outputs (local_90/88)
    float bn0, bn1;           // afStack_e0 outputs (local_d0/c8)
    double dx, dy, dz;

    rd = *(int*)(actor + 0x7c);
    origin[0] = *(float*)(actor + 100);
    float ey = *(float*)(actor + 0x68);
    origin[2] = *(float*)(actor + 0x6c);
    origin[1] = targetPos[1];
    if (((origin[1] < ey) ||
         (ey + *(float*)(&DAT_8048ca94 + *(short*)(rd + 0x48) * 0x30) < origin[1]))) {
        bool clampHi = ey + *(float*)(&DAT_8048ca94 + *(short*)(rd + 0x48) * 0x30) < origin[1];
        origin[1] = ey;
        if (clampHi) origin[1] = ey + *(float*)(&DAT_8048ca94 + *(short*)(rd + 0x48) * 0x30);
    }

    if (player_ai_is_heavy_weapon((int)*(short*)(rd + 0x4a)) == 0) {
        if (*(short*)(actor + 0x94) == 0x607) {
            player_ai_transform_aim_to_world(
                (double)((FLOAT_80615dbc * *(float*)(actor + 0x70)) / FLOAT_80615dc0), origin,
                (float*)(*(int*)(*(int*)(actor + 4) + 0x10) + 0x80),
                *(u32*)(*(int*)(*(int*)(actor + 4) + 0x80) + 4), scratch);
            sn0 = scratch[2]; sn1 = scratch[3];   // afStack_a0[2]/[3] -> local_90/88
            ax = sn0; ay = FLOAT_80615da8; az = sn1;
            fVar2 = sn1 * sn1 + sn0 * sn0 + FLOAT_80615da8 * FLOAT_80615da8;
            if (FLOAT_80615dac < fVar2) {
                fVar3 = 1.0f / SQRT_(fVar2);
                fVar3 = fVar3 * -(FLOAT_80615db0 * fVar2 * fVar3 * fVar3 - FLOAT_80615db4);
                float s = FLOAT_80615db8 * fVar3 *
                          -(FLOAT_80615db0 * fVar2 * fVar3 * fVar3 - FLOAT_80615db4);
                ax = sn0 * s; ay = FLOAT_80615da8 * s; az = sn1 * s;
            }
        }
        else if ((*(u32*)(actor + 0x10) & 0xffff) == 0) {
            ay = FLOAT_80615da8;
            lutIdx = (u32)(FLOAT_80615dc4 *
                     ((FLOAT_80615dbc * *(float*)(actor + 0x70)) / FLOAT_80615dc0) + FLOAT_80615db0);
            ax = *(float*)((int)&DAT_80583228 + (lutIdx & 0x7ff) * 4);
            az = *(float*)((int)&DAT_80583228 + ((lutIdx + 0x200) * 4 & 0x1ffc));
        }
        else {
            player_ai_get_aim_world_position(actor, origin, scratch);
            bn0 = scratch[2]; bn1 = scratch[3];   // afStack_e0[2]/[3] -> local_d0/c8
            ax = bn0; ay = FLOAT_80615da8; az = bn1;
            fVar2 = bn1 * bn1 + bn0 * bn0 + FLOAT_80615da8 * FLOAT_80615da8;
            if (FLOAT_80615dac < fVar2) {
                fVar3 = 1.0f / SQRT_(fVar2);
                fVar3 = fVar3 * -(FLOAT_80615db0 * fVar2 * fVar3 * fVar3 - FLOAT_80615db4);
                float s = FLOAT_80615db8 * fVar3 *
                          -(FLOAT_80615db0 * fVar2 * fVar3 * fVar3 - FLOAT_80615db4);
                ax = bn0 * s; ay = FLOAT_80615da8 * s; az = bn1 * s;
            }
        }
    }
    else {
        float heavy[3];
        FUN_8037efc8(actor, heavy);
        ax = heavy[0]; ay = heavy[1]; az = heavy[2];
    }

    dy = (double)(targetPos[1] - origin[1]);
    dx = (double)(targetPos[0] - origin[0]);
    dz = (double)(targetPos[2] - origin[2]);
    double len = math_sqrt((double)(float)(dz * dz +
                 (double)(float)(dx * dx + (double)(float)(dy * dy))));
    double invLen = (double)FLOAT_80615da8;
    if (invLen < len) invLen = (double)(float)((double)FLOAT_80615db8 / len);
    return threshold < (double)((float)(dz * invLen) * az +
                                (float)(dx * invLen) * ax + (float)(dy * invLen) * ay);
}

// aiactor_zone_facing_test 0x8013f2a8
// Rotates a reference vector by the target zone's yaw (+0x18) and returns whether
// the normalised direction from that zone centre to param_3 dotted with it > p1.
bool aiactor_zone_facing_test(double threshold, int zoneRow, float* targetPos)
{
    double dy = (double)FLOAT_80615da8;
    float  ref[3];
    float  mtx[24];
    ref[0] = DAT_803ccab8; ref[1] = DAT_803ccabc; ref[2] = DAT_803ccac0;
    double dx = (double)(targetPos[0] - *(float*)(zoneRow + 0xc));
    double dz = (double)(targetPos[2] - *(float*)(zoneRow + 0x14));
    float lenSq = (float)(dz * dz + (double)(float)(dx * dx + dy));
    if (FLOAT_80615dac < lenSq) {
        float inv = 1.0f / SQRT_(lenSq);
        inv = inv * -(FLOAT_80615db0 * lenSq * inv * inv - FLOAT_80615db4);
        double s = (double)(FLOAT_80615db8 * inv *
                   -(FLOAT_80615db0 * lenSq * inv * inv - FLOAT_80615db4));
        dx = (double)(float)(dx * s);
        dy = (double)(float)(dy * s);
        dz = (double)(float)(dz * s);
    }
    mtx4_set_rotation_y((double)((FLOAT_80615dbc * *(float*)(zoneRow + 0x18)) / FLOAT_80615dc0), mtx);
    mtx4_transform_vector_alias(mtx, ref);
    return threshold < (double)(float)(dz * (double)ref[2] +
                                       (double)(float)(dx * (double)ref[0] +
                                                       (double)(float)(dy * (double)ref[1])));
}

// aiactor_classify_aim_cone 0x8013f3ec
// Rotates a reference cone vector by the actor's (or its zone's) yaw and returns
// which cone the normalised target direction falls in: 0 in-cone, 1..4 outside.
u32 aiactor_classify_aim_cone(int actor, float* dirIn, int useZoneYaw)
{
    double dy = (double)FLOAT_80615da8;
    float  ref[3];
    float  mtx[24];
    u32    result;

    ref[0] = DAT_803ccac4; ref[1] = DAT_803ccac8; ref[2] = DAT_803ccacc;
    double dx = (double)dirIn[0];
    double dz = (double)dirIn[2];
    float lenSq = (float)(dz * dz + (double)(float)(dx * dx + dy));
    if (FLOAT_80615dac < lenSq) {
        float inv = 1.0f / SQRT_(lenSq);
        inv = inv * -(FLOAT_80615db0 * lenSq * inv * inv - FLOAT_80615db4);
        double s = (double)(FLOAT_80615db8 * inv *
                   -(FLOAT_80615db0 * lenSq * inv * inv - FLOAT_80615db4));
        dx = (double)(float)(dx * s);
        dy = (double)(float)(dy * s);
        dz = (double)(float)(dz * s);
    }
    if (useZoneYaw == 0) {
        mtx4_set_rotation_y((double)((FLOAT_80615dbc * *(float*)(actor + 0x70)) / FLOAT_80615dc0), mtx);
    }
    else {
        mtx4_set_rotation_y((double)((FLOAT_80615dbc *
            *(float*)(DAT_80611b8c + *(int*)(*(int*)(actor + 0x7c) + 0x678) * 0x20 + 0x18)) /
            FLOAT_80615dc0), mtx);
    }
    mtx4_transform_vector_alias(mtx, ref);
    float d1 = (float)(dz * (double)ref[2] +
               (double)(float)(dx * (double)ref[0] + (double)(float)(dy * (double)ref[1])));
    if (d1 <= FLOAT_80615dc8) {
        if (FLOAT_80615dcc <= d1) {
            ref[1] = FLOAT_80615da8; ref[0] = FLOAT_80615db8; ref[2] = FLOAT_80615da8;
            mtx4_transform_vector_alias(mtx, ref);
            float d2 = (float)(dz * (double)ref[2] +
                       (double)(float)(dx * (double)ref[0] + (double)(float)(dy * (double)ref[1])));
            if (d2 <= FLOAT_80615dc8) {
                result = (FLOAT_80615dcc <= d2) ? 0 : 4;
            }
            else {
                result = 3;
            }
        }
        else {
            result = 2;
        }
    }
    else {
        result = 1;
    }
    return result;
}

// aiactor_aim_at_target 0x8013f5c0
// Computes the aim direction toward the actor's current target (with a height
// bias that differs for the local player vs remote), normalises it and applies
// it via the aim-solver.
void aiactor_aim_at_target(u32 param_1, u32 param_2, int flags)
{
    float dir[3];
    int   actor;
    int   rd;
    int   target;
    double biasedY;
    unsigned long long fr;
    (void)param_1; (void)param_2;

    fr    = ((frame_enter_ull)(void*)&FUN_803826f0)();
    actor = (int)(fr >> 0x20);
    rd    = *(int*)(actor + 0x7c);
    target = *(int*)(rd + 4);
    dir[0] = *(float*)(target + 100)  - *(float*)(actor + 100);
    dir[1] = *(float*)(target + 0x68) - *(float*)(actor + 0x68);
    dir[2] = *(float*)(target + 0x6c) - *(float*)(actor + 0x6c);
    if ((int)fr == 0) {
        biasedY = (double)dir[1] + math_sqrt((double)(dir[0] * dir[0] + dir[2] * dir[2]));
    }
    else {
        biasedY = (double)FLOAT_80615db0 * math_sqrt((double)(dir[0] * dir[0] + dir[2] * dir[2]))
                  + (double)dir[1];
    }
    dir[1] = (float)biasedY;
    float lenSq = dir[2] * dir[2] + dir[0] * dir[0] + dir[1] * dir[1];
    if (FLOAT_80615dac < lenSq) {
        float inv = 1.0f / SQRT_(lenSq);
        inv = inv * -(FLOAT_80615db0 * lenSq * inv * inv - FLOAT_80615db4);
        float s = FLOAT_80615db8 * inv * -(FLOAT_80615db0 * lenSq * inv * inv - FLOAT_80615db4);
        dir[0] = dir[0] * s;
        dir[1] = dir[1] * s;
        dir[2] = dir[2] * s;
    }
    FUN_801af0a8(actor, dir, *(int*)(rd + 4), flags);
    FUN_8038273c();
}
