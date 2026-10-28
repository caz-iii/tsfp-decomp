// Address range: 0x801369c0 - 0x80137ffb | batch_365 | 2026-09-05
//
// AI actor movement / physics tick. The per-frame locomotion for AI-controlled
// characters: ground movement with slope sliding and ledge-fall handling, an
// alternate flying/floating mode, kill-plane death, bone-attach snapping, and
// heading/turn interpolation toward the current target.
//
// Shared conventions with aiactor_instance.cpp:
//   render object = *(actor + 0x7c), indexed as int[] rd[]; actor origin at
//   actor+0x64/0x68/0x6c (x/y/z), heading at actor+0x70.
//   FUN_803826f0/FUN_8038273c = fp scratch-frame push/pop (returns actor ptr).
//   __save_gpr_28/__restore_gpr_28 = big-frame register save/restore.
//
// Functions:
//   aiactor_handle_kill_plane_death 0x801369c0  fall-out / kill-plane death
//   aiactor_snap_to_bone_position   0x80136bf8  snap origin to bone/attach pt
//   aiactor_movement_tick           0x80136ca8  MASSIVE ground movement
//   aiactor_flying_movement_tick    0x80137578  flying/floating movement
//   aiactor_facing_update           0x801379a0  yaw/turn interpolation

#include "common.h"

// --- Globals ---------------------------------------------------------------
extern char   DAT_805015bd;      // game mode byte (0x0a coop)
extern u32    DAT_805015c0;      // cheat/flag word
extern u32    DAT_805015c4;      // cheat/flag word 2
extern int    DAT_806108e4;      // subsystem-active flag
extern int    DAT_806109c0;      // raw frame counter
extern int    DAT_80611d90;      // per-player state pool base (0xea0 stride)
extern int    DAT_80611b04;      // room def table base
extern int    DAT_80611b8c;      // zone table base (0x20 stride)
extern int    DAT_80612614;      // current level id
extern int    DAT_80612be8;      // mission-script context
extern float  FLOAT_806108ec;    // frame-time delta
extern float  FLOAT_806118ec;    // slow-mo factor
extern float  FLOAT_80615ca8;    // shared 1.0
extern float  FLOAT_80615cac;    // shared 0.0
extern float  FLOAT_80615cc0;    // flying lateral gain
extern float  FLOAT_80615ccc;    // deg<->rad numerator
extern float  FLOAT_80615cd0;    // deg<->rad denominator (pi)
extern float  FLOAT_80615cd4;    // floor-clamp epsilon
extern float  FLOAT_80615cd8;    // slope-slide gain
extern float  FLOAT_80615cdc;    // slope descend step
extern float  FLOAT_80615ce0;    // min-speed threshold
extern float  FLOAT_80615ce4;    // ledge-drop threshold
extern float  FLOAT_80615ce8;    // ledge-drop step / proximity sq
extern float  FLOAT_80615cec;    // stumble amount
extern float  FLOAT_80615cf0;    // flying accel
extern float  FLOAT_80615cf4;    // flying floor speed
extern float  FLOAT_80615cf8;    // flying lateral base
extern float  FLOAT_80615cfc;    // flying descend scale
extern float  FLOAT_80615d00;    // 2*pi
extern float  FLOAT_80615d04;    // -pi
extern float  FLOAT_80615d08;    // default facing when no leader
extern float  FLOAT_80615d0c;    // weapon turn-rate scale
extern float  FLOAT_80615d10;    // backward turn-rate scale
extern float  FLOAT_80615d14;    // flagged turn-rate scale
extern double DOUBLE_80615cb0;   // int->double bias
extern int    DAT_803cca88;      // bag-tag explosion offset vec (x)
extern int    DAT_803cca8c;
extern int    DAT_803cca90;
extern float  DAT_803cca94;      // level-100 explosion offset vec
extern int    DAT_803cca98;
extern int    DAT_803cca9c;
extern int    DAT_804d1cd0;      // floor-material index table (short[])
extern int    DAT_804d34dc;      // floor-material flags table (0x8 stride)
extern int    DAT_8048ca94;      // per-char movement param table (x, 0x30 stride)
extern int    DAT_8048ca9c;      // per-char movement param table (z, 0x30 stride)

// --- Helpers ---------------------------------------------------------------
extern unsigned long long FUN_803826f0(void);    // fp frame enter (actor ptr)
extern void               FUN_8038273c(void);     // fp frame leave
extern int                __save_gpr_28(void);    // big frame enter (actor ptr)
extern void               __restore_gpr_28(void);

extern double math_sqrt(double x);
extern double math_atan2(double y, double x);
extern void   mtx4_set_rotation_y(double angle, float *outMtx);
extern void   mtx4_transform_vector_alias(float *mtx, float *vec);

extern int  score_register_death(int *rd, int actorIndex, int flags);
extern int  player_ai_explosion_spawn(int type, int subtype, float *pos, void *offs,
                                      int a, int b, int c);
extern int  player_ai_combat_behavior_tick(void);
extern int  player_ai_drop_all_weapons(unsigned long long, double, double, double,
                          unsigned long long, unsigned long long, unsigned long long,
                          unsigned long long, int *rd);
extern int  player_ai_rail_track_follow(int actorIndex, float *destPos, float *heading);
extern int  player_ai_get_floor_room(int actor);
extern u32  nav_advanced_movement_dispatch(double paramX, double paramZ, int actorIndex,
                          int posPtr, float *destPos, int *outHit, int a, int flags);
extern int  floor_height_from_position(double x, double z);
extern int  nav_check_floor_edges(void);
extern int  soundpad_find_nearest_entity(double minDist, float *pos);
extern int *soundpad_find_active_portal(int *zoneA, int *zoneB);
extern int  sound_init_play_params(int reqBuf);
extern int  sound_play_request(int *reqBuf);

extern int  FUN_80339b90(int ctx, int key);
extern int  FUN_80025b30(int actor, u32 r4, int flagsA, int flagsB);
extern int  FUN_80025868(int actor);
extern int  FUN_80025950(int actor, u32 mat, int a, int b);
extern void FUN_80023514(double a, double b);
extern void FUN_80023280(double a, double b);
extern void FUN_8013c540(void);
extern void FUN_8013c8f8(void);
extern void FUN_80285120(int actor);
extern void FUN_80137578(void);   // aiactor_flying_movement_tick (this file)
extern void FUN_801389b0(int actor, int *zoneSlot);
extern void FUN_801390d8(void);
extern int  player_slot_get_pool_base(void);

// ---------------------------------------------------------------------------
// aiactor_handle_kill_plane_death 0x801369c0
// ---------------------------------------------------------------------------
void aiactor_handle_kill_plane_death(double param_1, double param_2, double param_3,
                                     double param_4, unsigned long long param_5,
                                     unsigned long long param_6, unsigned long long param_7,
                                     unsigned long long param_8, int *rd)
{
    int *pMission;
    u32 stateMask;
    int actor;
    unsigned long long ctx;
    float ex, ey, ez;     // explosion offset copy (local_38..)
    float px, py, pz;     // spawn position (local_2c..)
    (void)param_1;

    if ((rd[9] & 8U) == 0) {
        actor = *rd;
        score_register_death(rd, rd[2], 1);
        rd[10] = rd[10] | 0x100;
        if ((*(u16 *)(rd[0x300] + 8) & 0x800) == 0) {
            if ((*(u16 *)(rd[0x300] + 8) & 0x1000) == 0) {
                if (((DAT_80612614 == 100) &&
                     (pMission = (int *)FUN_80339b90(DAT_80612be8, 0xc77668),
                      *(short *)((int)pMission + 10) == 3)) &&
                    (*(int *)(*(int *)(DAT_80611b04 + *(int *)(actor + 0x34) * 4) + 0x8c) == 0x16)) {
                    ex = DAT_803cca94;
                    *(int *)&ey = DAT_803cca98;
                    *(int *)&ez = DAT_803cca9c;
                    px = *(float *)(actor + 100) + *(float *)(actor + 0x1a8);
                    py = *(float *)(actor + 0x68) + *(float *)(actor + 0x1ac);
                    pz = *(float *)(actor + 0x6c) + *(float *)(actor + 0x1b0);
                    player_ai_explosion_spawn(0xb, 0, (float *)(actor + 100), &ex, 0, actor, 1);
                    player_ai_explosion_spawn(0x2c, 0, &px, &ex, actor, actor, 1);
                    (void)py; (void)pz;
                }
            }
            else {
                rd[0xb] = rd[0xb] | 0x80000;
            }
        }
        else {
            ex = *(float *)&DAT_803cca88;
            *(int *)&ey = DAT_803cca8c;
            *(int *)&ez = DAT_803cca90;
            player_ai_explosion_spawn(0x19, 0x24, (float *)(actor + 100), &ex, 0, 0, 1);
            rd[0xb] = rd[0xb] | 0x40000;
        }
        (void)ey; (void)ez;

        ctx = (unsigned long long)(u32)player_ai_combat_behavior_tick();
        player_ai_drop_all_weapons(ctx, param_2, param_3, param_4, param_5, param_6,
                                   param_7, param_8, rd);
        if (rd[0x2ab] == 1 && rd[0x2aa] == 0) {
            stateMask = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
            if ((stateMask & 1) == 0) {
                return;
            }
        }
        rd[0x2ab] = 1;
        rd[0x2aa] = 0;
    }
    return;
}

// ---------------------------------------------------------------------------
// aiactor_snap_to_bone_position 0x80136bf8
// ---------------------------------------------------------------------------
u32 aiactor_snap_to_bone_position(int actor, u32 param_2)
{
    int bone;
    u32 result;
    int rd;

    result = 0;
    if (FUN_80025b30(actor, param_2, 0x20000, 0) != 0) {
        bone = FUN_80025868(actor);
        rd = *(int *)(actor + 0x7c);
        *(int *)(actor + 100)   = *(int *)(bone + 0x10);
        *(int *)(actor + 0x68)  = *(int *)(bone + 0x14);
        *(int *)(actor + 0x6c)  = *(int *)(bone + 0x18);
        if ((*(u32 *)(rd + 0x28) & 0x40) == 0) {
            FUN_8013c540();
        }
        else {
            FUN_8013c540();
            *(u32 *)(rd + 0x28) = *(u32 *)(rd + 0x28) & 0xffffffbf;
        }
        FUN_8013c8f8();
        result = 1;
    }
    return result;
}

// ---------------------------------------------------------------------------
// aiactor_movement_tick 0x80136ca8
// MASSIVE ground movement/physics for a walking AI actor.
// ---------------------------------------------------------------------------
void aiactor_movement_tick(unsigned long long param_1, double param_2, double param_3,
                           double param_4, double param_5, double param_6,
                           double param_7, unsigned long long param_8)
{
    float f1, f2;
    bool okA, okB;
    int actor;
    unsigned char navResult;
    u32 mat;
    int volDef;
    int i;
    int step;
    int actorIndex;
    int *rd;
    double dx, dy, dz, len;
    float local_b8, local_b4, local_b0;   // model velocity vec
    float local_c0, local_bc;             // clamped target y / z
    float local_dc[7];                    // desired position + slope work
    float local_e8, local_e4, local_e0;   // anchor-pull target
    float afStack_ac[17];                 // yaw matrix scratch
    (void)param_1;

    actor = __save_gpr_28();
    rd = *(int **)(actor + 0x7c);
    i = 0;   // mount-query flag
    actorIndex = ((int)rd - DAT_80611d90) / 0xea0;

    {
        int mount = rd[0x301];
        if ((mount != 0) && (((*(u32 *)(mount + 0x18) | *(u32 *)(mount + 8)) & 2) != 0)) {
            i = 1;
        }
    }

    if (aiactor_snap_to_bone_position(actor, (u32)i) != 0) {
        __restore_gpr_28();
        return;
    }

    volDef = *(int *)(actor + 4);
    local_b8 = *(float *)(volDef + 0xf4);
    local_b4 = *(float *)(volDef + 0xf8);
    local_b0 = *(float *)(volDef + 0xfc);

    if ((rd[0x300] != 0) &&
        ((((*(u16 *)(rd[0x300] + 8) & 0x1004) != 0) || (nav_check_floor_edges() != 0)) &&
         ((double)*(float *)(actor + 0x68) <= (double)(float)rd[0x2ff]))) {
        aiactor_handle_kill_plane_death((double)*(float *)(actor + 0x68), param_2, param_3,
                                        param_4, param_5, param_6, param_7, param_8, rd);
        __restore_gpr_28();
        return;
    }

    if ((rd[9] & 0x40U) == 0) {
        if (((FLOAT_80615cac != local_b8) || (FLOAT_80615cac != local_b4)) ||
            ((FLOAT_80615cac != local_b0) || (i != 0))) {

            if ((rd[0xb] & 0x10000U) == 0) {
                // --- normal grounded movement ---
                if ((rd[10] & 0x40U) != 0) {
                    FUN_8013c540();
                }
                mtx4_set_rotation_y((double)((FLOAT_80615ccc * *(float *)(actor + 0x70)) /
                                             FLOAT_80615cd0), afStack_ac);
                mtx4_transform_vector_alias(afStack_ac, &local_b8);
                local_dc[6] = *(float *)(actor + 100) + local_b8;
                local_bc = *(float *)(actor + 0x6c) + local_b0;

                int matFlagsIdx = (short)(&DAT_804d1cd0)[*(int *)(*(int *)(actor + 4) + 0xcc)] * 8;
                if (((*(u32 *)(&DAT_804d34dc + matFlagsIdx) & 0x20) == 0) ||
                    ((*(u32 *)(&DAT_804d34dc + matFlagsIdx) & 0x800000) != 0)) {
                    local_c0 = (float)rd[0x2ff];
                }
                else {
                    if (*(float *)(actor + 0x68) - FLOAT_80615ca8 < (float)rd[0x2ff]) {
                        *(float *)(actor + 0x68) = (float)rd[0x2ff] - FLOAT_80615cd4;
                    }
                    local_c0 = *(float *)(actor + 0x68) + local_b4;
                    if (local_c0 < (float)rd[0x2ff]) {
                        local_c0 = (float)rd[0x2ff];
                    }
                    if (FLOAT_80615ca8 < local_c0 - (float)rd[0x2ff]) {
                        local_c0 = (float)rd[0x2ff];
                    }
                }

                // Slope tangent slide for triangular floor volumes.
                int floorVol = rd[0x300];
                if ((floorVol != 0) && ((*(u16 *)(floorVol + 8) & 2) != 0)) {
                    int o = 0;
                    int c = 3;
                    do {
                        int v = floorVol + o;
                        f1 = *(float *)(v + 0x3c);
                        f2 = *(float *)(v + 0x24);
                        *(float *)((int)local_dc + o + 0xc) = f1 - *(float *)(v + 0x54);
                        *(float *)((int)local_dc + o) = f1 - f2;
                        o = o + 4;
                        c = c + -1;
                    } while (c != 0);
                    // cross product of the two edge vectors -> slope normal
                    double e0 = (double)local_dc[0], e1 = (double)local_dc[1], e2 = (double)local_dc[2];
                    double g3 = (double)local_dc[3], g4 = (double)local_dc[4], g5 = (double)local_dc[5];
                    double nX = (double)(float)(g5 * e0 - (double)(float)(e2 * g3));
                    double nY = (double)(float)(g4 * e2 - (double)(float)(e1 * g5));
                    double nZ = (double)(float)(g3 * e1 - (double)(float)(e0 * g4));
                    len = math_sqrt((double)(float)(nZ * nZ +
                                                    (double)(float)(nY * nY +
                                                                    (double)(float)(nX * nX))));
                    if (nX < len) {
                        double invFrame = (double)(float)((double)DAT_806109c0);
                        double gain = (double)FLOAT_80615cd8;
                        local_dc[6] = (float)((double)*(float *)(actor + 100) +
                                              (double)(float)((double)(float)(gain *
                                                  (double)(FLOAT_806108ec * (float)(nY / len))) /
                                                  invFrame));
                        local_c0 = *(float *)(actor + 0x68) - FLOAT_80615cdc;
                        local_bc = *(float *)(actor + 0x6c) +
                                   (float)((double)(float)(gain *
                                       (double)(FLOAT_806108ec * (float)(nZ / len))) / invFrame);
                    }
                }

                player_ai_rail_track_follow(actorIndex, local_dc + 6, (float *)(actor + 0x70));
                if ((rd[10] & 0x40U) != 0) {
                    local_dc[6] = local_dc[6] + FLOAT_80615ce0;
                }
                rd[0x2dc] = rd[0x2ff];

                if ((rd[0xb] & 0x20U) == 0) {
                    if ((int)((unsigned long long)FUN_80025b30(actor, 0, 0, 0x800000)) != 0) {
                        goto ground_finish;   // collision-blocked
                    }
                    int flyFlag = (rd[9] & 8U) ? 8 : 0;
                    navResult = (unsigned char)nav_advanced_movement_dispatch(
                        (double)*(float *)(&DAT_8048ca94 + *(short *)(rd + 0x12) * 0x30),
                        (double)*(float *)(&DAT_8048ca9c + *(short *)(rd + 0x12) * 0x30),
                        actorIndex, actor + 100, local_dc + 6, (int *)0, 1, flyFlag);
                    if ((navResult & 0x10) == 0) {
                        if (*(int *)(actor + 0x34) == 0xd5) {
                            *(int *)(actor + 0x34) = floor_height_from_position(
                                (double)*(float *)(&DAT_8048ca94 + *(short *)(rd + 0x12) * 0x30),
                                (double)*(float *)(&DAT_8048ca9c + *(short *)(rd + 0x12) * 0x30));
                        }
                    }
                    else {
                        *(int *)(actor + 0x34) = 0xd5;
                    }
                }
                else {
ground_finish:
                    if (((rd[0xb] & 0x20U) == 0) &&
                        (FUN_80025b30(actor, 0, 0, 0x40) == 0)) {
                        local_c0 = *(float *)(actor + 0x68);
                        navResult = 1;
                    }
                    else {
                        player_ai_get_floor_room(actor);
                        local_c0 = (float)rd[0x2ff];
                        navResult = (*(float *)(actor + 0x68) <= local_c0);
                    }
                }

                if ((navResult & 0xe) != 0) {
                    rd[0xb] = rd[0xb] | 0x40;
                }

                // Ledge-drop / stumble transition.
                if ((((navResult & 1) == 0) && ((rd[10] & 0x40U) == 0)) &&
                    (FLOAT_80615ce4 < (float)((double)(float)rd[0x2dc] - (double)(float)rd[0x2ff])) &&
                    ((rd[10] & 0x100U) == 0)) {
                    okB = true;
                    *(float *)(actor + 100) = local_dc[6];
                    okA = true;
                    *(float *)(actor + 0x68) = *(float *)(actor + 0x68) - FLOAT_80615ce8;
                    *(float *)(actor + 0x6c) = local_bc;
                    rd[9] = rd[9] | 0x40;
                    mat = *(u32 *)(*(int *)(*(int *)(actor + 4) + 0x80) + 4);
                    if ((1 < mat) && (mat != 0x15)) okA = false;
                    if ((!okA) && (mat != 0x7c)) okB = false;
                    if (okB && (FUN_80025950(actor, mat, 0, 0x20) == 0)) {
                        FUN_80023514((double)FLOAT_80615cec, (double)FLOAT_80615ca8);
                    }
                    rd[0x2c7] = (int)FLOAT_80615cac;
                    if (((rd[9] & 8U) == 0) && (rd[0x300] == 0)) {
                        unsigned long long ctx =
                            (unsigned long long)(u32)score_register_death(rd, rd[2], 1);
                        player_ai_drop_all_weapons(ctx, param_2, param_3, param_4, param_5,
                                                   param_6, param_7, param_8, rd);
                        rd[0xb] = rd[0xb] | 0x100000;
                    }
                    __restore_gpr_28();
                    return;
                }

                *(float *)(actor + 100) = local_dc[6];
                if ((rd[9] & 0x80U) == 0) {
                    *(float *)(actor + 0x68) = local_c0;
                }
                *(float *)(actor + 0x6c) = local_bc;
                rd[10] = rd[10] & 0xffffffbf;
                FUN_8013c540();
            }
            else {
                // --- special-locomotion / anchor-pull branch (flag 0x10000) ---
                bool doPull = false;
                rd[10] = rd[10] | 0x40;
                mat = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
                if ((mat & 0x20) == 0) {
                    int zoneA = rd[0x19e] * 0x20;
                    u32 flagsA = *(u32 *)(DAT_80611b8c + 4 + zoneA);
                    if ((((flagsA & 0x1000000) == 0) || (rd[0x1a0] == 0)) ||
                        ((*(u32 *)(DAT_80611b8c + 4 + rd[0x1a0] * 0x20) & 0x1000000) == 0 ||
                         ((flagsA & 0x800000) != 0))) {
                        if (-1 < rd[0x19e]) doPull = true;
                    }
                    else {
                        u32 *portal = (u32 *)soundpad_find_active_portal(
                            (int *)(DAT_80611b8c + zoneA),
                            (int *)(DAT_80611b8c + rd[0x1a0] * 0x20));
                        doPull = (portal == (u32 *)0) || ((*portal & 4) == 0);
                    }
                }
                else {
                    rd[0x2ff] = *(int *)(actor + 0x68);
                    FUN_8013c540();
                }
                if (doPull) {
                    dx = (double)((float)rd[0xc] - *(float *)(actor + 100));
                    dy = (double)((float)rd[0xd] - *(float *)(actor + 0x68));
                    dz = (double)((float)rd[0xe] - *(float *)(actor + 0x6c));
                    double distSq = (double)(float)(dz * dz +
                                                    (double)(float)(dy * dy + (double)(float)(dx * dx)));
                    double scale = (double)FLOAT_80615cac;
                    if ((double)FLOAT_80615cac < distSq) {
                        len = math_sqrt(distSq);
                        double inv = (double)(float)((double)FLOAT_80615ca8 / len);
                        double vmag = math_sqrt((double)(local_b0 * local_b0 +
                                                         local_b8 * local_b8 + local_b4 * local_b4));
                        scale = (double)(float)(inv * vmag);
                        if (scale < (double)FLOAT_80615ce0) {
                            scale = (double)FLOAT_80615cac;
                        }
                    }
                    local_e8 = (float)(dx * scale + (double)*(float *)(actor + 100));
                    local_e4 = (float)(dy * scale + (double)*(float *)(actor + 0x68));
                    local_e0 = (float)(dz * scale + (double)*(float *)(actor + 0x6c));
                    player_ai_rail_track_follow(actorIndex, &local_e8, (float *)(actor + 0x70));
                    *(float *)(actor + 100)  = local_e8;
                    *(float *)(actor + 0x68) = local_e4;
                    *(float *)(actor + 0x6c) = local_e0;
                    rd[0x2dc] = *(int *)(actor + 0x68);
                    rd[0x2ff] = *(int *)(actor + 0x68);
                    FUN_8013c540();
                }
            }
        }
    }
    else {
        FUN_80137578();   // flying/floating movement
    }

    FUN_8013c8f8();
    FUN_80285120(actor);
    __restore_gpr_28();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_flying_movement_tick 0x80137578
// ---------------------------------------------------------------------------
void aiactor_flying_movement_tick(void)
{
    bool okA, okB;
    float floorSpeed;
    int actor;
    int actorIndex;
    u32 mat;
    int *rd;
    double invFrame, dt;
    float lateral;
    float local_a8, local_a4, local_a0;   // desired position
    float local_9c, local_98, local_94;   // step vector (transformed)
    int reqBuf[7];
    int reqType;
    int reqX, reqY, reqZ;
    unsigned char reqOne;
    float afStack_60[16];

    actor = (int)(u32)FUN_803826f0();
    floorSpeed = FLOAT_80615cf4;
    rd = *(int **)(actor + 0x7c);
    actorIndex = (int)rd - DAT_80611d90;
    invFrame = (double)(float)((double)DAT_806109c0);

    // Decay vertical speed (+0x2c7) toward the floor speed.
    dt = (double)(float)((double)FLOAT_80615ce8 *
                         (double)(FLOAT_80615cf0 / (float)invFrame));
    rd[0x2c7] = (int)-(float)(dt * (double)FLOAT_806108ec - (double)(float)rd[0x2c7]);
    if ((float)rd[0x2c7] < floorSpeed) {
        rd[0x2c7] = (int)floorSpeed;
    }

    local_9c = FLOAT_80615cac;
    local_98 = FLOAT_80615cac;
    if ((rd[7] & 0x8000U) == 0) {
        lateral = (float)((double)FLOAT_806108ec *
                          (double)(FLOAT_80615cc0 * (FLOAT_80615cf8 + ABS((float)rd[0x2ca])))) /
                   (float)invFrame;
        local_94 = lateral;
    }
    else {
        local_94 = FLOAT_80615cac;
    }

    mtx4_set_rotation_y((double)((FLOAT_80615ccc * *(float *)(actor + 0x70)) / FLOAT_80615cd0),
                        afStack_60);
    mtx4_transform_vector_alias(afStack_60, &local_9c);
    local_a8 = *(float *)(actor + 100) + local_9c;
    if ((rd[7] & 0x8000U) == 0) {
        local_a4 = (float)rd[0x2c7] * FLOAT_806108ec + *(float *)(actor + 0x68);
    }
    else {
        local_a4 = FLOAT_80615cfc * (float)rd[0x2c7] * FLOAT_806108ec + *(float *)(actor + 0x68);
    }
    local_a0 = *(float *)(actor + 0x6c) + local_94;

    mat = nav_advanced_movement_dispatch(
        (double)*(float *)(&DAT_8048ca94 + *(short *)(rd + 0x12) * 0x30),
        (double)*(float *)(&DAT_8048ca9c + *(short *)(rd + 0x12) * 0x30),
        actorIndex / 0xea0, actor + 100, &local_a8, (int *)0, 1, 0);

    if ((mat & 1) == 0) {
        FUN_8013c540();
        rd[10] = rd[10] & 0xffffffbf;
        *(float *)(actor + 100)  = local_a8;
        *(float *)(actor + 0x68) = local_a4;
        *(float *)(actor + 0x6c) = local_a0;
        FUN_8038273c();
        return;
    }

    if (((rd[0x300] != 0) && ((*(u16 *)(rd[0x300] + 8) & 0x1804) != 0)) &&
        (((double)*(float *)(actor + 0x68) <= (double)(float)rd[0x2ff]) || ((mat & 1) != 0))) {
        aiactor_handle_kill_plane_death((double)*(float *)(actor + 0x68), 0, 0, 0, 0, 0, 0, 0, rd);
        FUN_8038273c();
        return;
    }

    rd[9] = rd[9] & 0xffffffbf;
    if ((rd[9] & 8U) == 0) {
        rd[0x19e] = soundpad_find_nearest_entity((double)FLOAT_80615cac, (float *)(actor + 100));
        rd[0x26b] = 0;
        rd[0x26a] = 0;
        FUN_801389b0(actor, rd + 0x19e);
        FUN_801390d8();
        FUN_8013c540();
        okB = true;
        okA = true;
        rd[10] = rd[10] & 0xffffffbf;
        mat = *(u32 *)(*(int *)(*(int *)(actor + 4) + 0x80) + 4);
        if ((1 < mat) && (mat != 0x15)) okA = false;
        if ((!okA) && (mat != 0x7c)) okB = false;
        if (okB) {
            FUN_80023280((double)FLOAT_80615cec, (double)FLOAT_80615ca8);
        }
        *(float *)(actor + 100)  = local_a8;
        *(float *)(actor + 0x68) = local_a4;
        *(float *)(actor + 0x6c) = local_a0;
        FUN_8038273c();
        return;
    }

    if (0 < DAT_806108e4) {
        sound_init_play_params((int)reqBuf);
        reqOne = 1;
        reqBuf[0] = 0xd52;
        reqType = *(int *)(actor + 100);
        reqX = *(int *)(actor + 0x68);
        reqY = *(int *)(actor + 0x6c);
        (void)reqType; (void)reqX; (void)reqY; (void)reqZ; (void)reqOne;
        // Fill the request position fields (reqBuf +0x10/+0x14/+0x18).
        *(int *)((int)reqBuf + 0x10) = *(int *)(actor + 100);
        *(int *)((int)reqBuf + 0x14) = *(int *)(actor + 0x68);
        *(int *)((int)reqBuf + 0x18) = *(int *)(actor + 0x6c);
        *(unsigned char *)((int)reqBuf + 0x24) = 1;
        sound_play_request(reqBuf);
    }

    player_ai_combat_behavior_tick();
    {
        bool skip = false;
        if (rd[0x2ab] == 1 && rd[0x2aa] == 0) {
            mat = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
            if ((mat & 1) != 0) skip = true;
        }
        if (!skip) {
            rd[0x2ab] = 1;
            rd[0x2aa] = 0;
        }
    }
    rd[10] = rd[10] | 0x100;
    FUN_8038273c();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_facing_update 0x801379a0
// ---------------------------------------------------------------------------
void aiactor_facing_update(void)
{
    float yaw;
    int actor;
    u32 stateFlags;
    int leader;
    int rd;
    double dx, dz, ang;

    actor = (int)(u32)FUN_803826f0();
    rd = *(int *)(actor + 0x7c);

    // Wrap current heading into [0, 2pi).
    while ((double)*(float *)(actor + 0x70) < (double)FLOAT_80615cac) {
        *(float *)(actor + 0x70) = *(float *)(actor + 0x70) + FLOAT_80615d00;
    }

    leader = *(int *)(rd + 4);
    if (leader == 0) {
        *(float *)(rd + 0xae0) = FLOAT_80615d08;
    }
    else {
        dx = (double)(*(float *)(leader + 100) - *(float *)(actor + 100));
        dz = (double)(*(float *)(leader + 0x6c) - *(float *)(actor + 0x6c));
        stateFlags = (DAT_805015bd == '\n') ? *(u32 *)(rd + 0x14) : *(u32 *)(rd + 0x1c);
        if ((stateFlags & 0x800) == 0) {
            ang = math_atan2(dx, dz);
        }
        else {
            ang = math_atan2(-dx, -dz);
        }
        yaw = (FLOAT_80615cd0 * (float)ang) / FLOAT_80615ccc;
        if (yaw < FLOAT_80615cac) {
            yaw = yaw + FLOAT_80615d00;
        }
        for (yaw = yaw - *(float *)(actor + 0x70); yaw <= FLOAT_80615d04; yaw = yaw + FLOAT_80615d00) { }
        for (; FLOAT_80615cd0 < yaw; yaw = yaw - FLOAT_80615d00) { }
        *(float *)(rd + 0xae0) = yaw;
    }

    // Secondary aim yaw + body-yaw integration, gated by state/def flags.
    if ((((*(u32 *)(rd + 0x28) & 0x800) == 0) && ((*(u32 *)(rd + 0x24) & 0x40) == 0)) &&
        ((((*(u32 *)(rd + 0x14) & 0x1000000) == 0 || ((*(u32 *)(rd + 0x28) & 0x800000) != 0)) ||
          ((FLOAT_80615cac == *(float *)(*(int *)(actor + 4) + 400) &&
            ((*(u32 *)(&DAT_804d34dc +
                       (short)(&DAT_804d1cd0)[*(int *)(*(int *)(actor + 4) + 0xcc)] * 8) & 0x2000) !=
             0)))))) {
        int volDef = *(int *)(actor + 4);
        u32 matFlags = *(u32 *)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[*(int *)(volDef + 0xcc)] * 8);
        if (((matFlags & 0x4000) == 0) &&
            ((((*(float *)(volDef + 400) <= FLOAT_80615cac ||
                ((*(u32 *)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[*(int *)(volDef + 0x124)] * 8) & 0x4000)
                 == 0)) && (((matFlags & 0x1c00) == 0) || ((*(u32 *)(volDef + 0x120) & 4) != 0)))) &&
             ((((*(float *)(volDef + 400) <= FLOAT_80615cac ||
                 ((*(u32 *)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[*(int *)(volDef + 0x124)] * 8) & 0x1c00)
                  == 0)) || ((*(u32 *)(volDef + 0x178) & 4) != 0)) && ((matFlags & 0x20) == 0))))) {

            dx = (double)(*(float *)(rd + 0x30) - *(float *)(actor + 100));
            dz = (double)(*(float *)(rd + 0x38) - *(float *)(actor + 0x6c));
            if ((*(u32 *)(rd + 0x14) & 0x8000000) == 0) {
                if ((float)(dx * dx + (double)(float)(dz * dz)) < FLOAT_80615ce8) {
                    *(float *)(rd + 0xae4) = FLOAT_80615cac;
                }
                else {
                    if ((*(u32 *)(rd + 0x24) & 0x10) == 0) {
                        ang = math_atan2(dx, dz);
                    }
                    else {
                        ang = math_atan2(-dx, -dz);
                    }
                    yaw = (FLOAT_80615cd0 * (float)ang) / FLOAT_80615ccc;
                    if (yaw < FLOAT_80615cac) {
                        yaw = yaw + FLOAT_80615d00;
                    }
                    for (yaw = yaw - *(float *)(actor + 0x70); yaw <= FLOAT_80615d04; yaw = yaw + FLOAT_80615d00) { }
                    for (; FLOAT_80615cd0 < yaw; yaw = yaw - FLOAT_80615d00) { }
                    *(float *)(rd + 0xae4) = yaw;
                }
            }
            else {
                for (yaw = *(float *)(*(int *)(rd + 0x67c) + 0x18) - *(float *)(actor + 0x70);
                     yaw <= FLOAT_80615d04; yaw = yaw + FLOAT_80615d00) { }
                for (; FLOAT_80615cd0 < yaw; yaw = yaw - FLOAT_80615d00) { }
                *(float *)(rd + 0xae0) = yaw;
                *(int *)(rd + 0xae4) = *(int *)(rd + 0xae0);
            }

            if ((*(u32 *)(rd + 0x24) & 0x35) != 0) {
                double rate = (double)*(float *)(rd + 0xb2c);
                double targetYaw = (double)*(float *)(rd + 0xae4);
                if ((((DAT_805015c0 & 0x800) != 0) && ((DAT_805015c4 & 0x2000000) == 0)) &&
                    ((player_slot_get_pool_base() != 0) &&
                     (*(int *)(player_slot_get_pool_base() + 0x218) != 0x1b))) {
                    rate = (double)(float)(rate * (double)FLOAT_806118ec);
                }
                if (targetYaw <= (double)FLOAT_80615cac) {
                    rate = (double)(float)(-rate * (double)FLOAT_806108ec);
                }
                else {
                    rate = (double)(float)(rate * (double)FLOAT_806108ec);
                }

                stateFlags = *(u32 *)(rd + 0x14);
                if ((stateFlags & 0x1000000) != 0) {
                    int wtype = *(int *)(rd + 0x20);
                    if ((wtype != 6) && ((5 < wtype || (wtype != 2)))) {
                        rate = (double)(float)(rate * (double)FLOAT_80615d0c);
                    }
                }
                if (DAT_805015bd != '\n') {
                    stateFlags = *(u32 *)(rd + 0x1c);
                }
                if ((stateFlags & 0x40) != 0) {
                    rate = (double)(float)(rate * (double)FLOAT_80615d10);
                }
                if ((*(u32 *)(rd + 0x2c) & 1) != 0) {
                    rate = (double)(float)(rate * (double)FLOAT_80615d14);
                }

                if (ABS(targetYaw) < ABS(rate)) {
                    *(float *)(actor + 0xc4) = (float)((double)*(float *)(actor + 0xc4) + targetYaw);
                }
                else {
                    *(float *)(actor + 0xc4) = (float)((double)*(float *)(actor + 0xc4) + rate);
                }
                if (FLOAT_80615d00 < *(float *)(actor + 0xc4)) {
                    *(float *)(actor + 0xc4) = *(float *)(actor + 0xc4) - FLOAT_80615d00;
                }
                if (*(float *)(actor + 0xc4) < FLOAT_80615cac) {
                    *(float *)(actor + 0xc4) = *(float *)(actor + 0xc4) + FLOAT_80615d00;
                }
            }
        }
    }
    FUN_8038273c();
    return;
}
