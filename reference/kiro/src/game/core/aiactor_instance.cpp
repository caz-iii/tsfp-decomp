// Address range: 0x80135e58 - 0x80136a0f | batch_365 | 2026-09-05
//
// AI actor per-instance update: the primary per-frame tick invoked from
// player_ai_per_frame_tick for each live AI-controlled character, plus its
// support helpers (footstep sound, room/zone change sync, movement-flag reset,
// visible-node scratch clear).
//
// Conventions (shared with aiactor_system.cpp / aiactor_movement.cpp):
//   DAT_805015bd  game-mode byte (0x0a = coop story path)
//   render object = *(actor + 0x7c); most per-instance state lives there,
//   indexed as an int[] (word offsets below are element indices, e.g. rd[5]).
//   FUN_803826d0/FUN_8038271c = fp scratch-frame push/pop (returns actor ptr).
//
// Functions:
//   aiactor_footstep_sound_update       0x80135e58  3D footstep/move sound
//   aiactor_instance_tick               0x80135ecc  MASSIVE per-actor tick
//   aiactor_clear_movement_flags        0x801367a0  clear move/collision flags
//   aiactor_sync_room_change            0x80136810  commit room/zone change
//   aiactor_clear_visible_node_scratch  0x80136988  reset visible-node scratch

#include "common.h"

// --- Globals ---------------------------------------------------------------
extern char   DAT_805015bd;      // game mode byte
extern char   DAT_805015be;      // secondary mode selector
extern int    DAT_806120fc;      // story-extra actor count
extern int    DAT_806108e4;      // subsystem-active flag
extern int    DAT_806120f8;      // local/primary actor index
extern int    DAT_806120f0;      // (unused here; shared)
extern int    DAT_80612610;      // base active-actor count
extern int    DAT_806109c0;      // raw frame counter (int->double via magic)
extern int    DAT_806118dc;      // network/replay gate
extern int    DAT_80611c24;      // entity handle pool base (0x2e8 stride)
extern int    DAT_80612098;      // visible-node count
extern int    DAT_805885f0;      // visible-node scratch region (0x80 bytes)
extern float  FLOAT_806108ec;    // frame-time delta
extern float  FLOAT_80615ca8;    // shared 1.0 constant
extern float  FLOAT_80615cac;    // shared 0.0 constant
extern float  FLOAT_80615cb8;    // footstep default volume
extern float  FLOAT_80615cbc;    // footstep default pitch
extern float  FLOAT_80615cc0;    // type-0x609 volume
extern float  FLOAT_80615cc4;    // type-0x609 pitch
extern float  FLOAT_80615cc8;    // bag-tag scale factor
extern double DOUBLE_80615cb0;   // int->double bias (0x43300000 magic)

// --- Helpers ---------------------------------------------------------------
extern unsigned long long FUN_803826d0(void);   // fp frame enter (packed ptr)
extern void               FUN_8038271c(void);    // fp frame leave
extern void  memset_wrapper(int dst, int val, int len);

extern void  sound_update_3d_source(double vol, double pitch, double ref, int posPtr,
                                    int listenerPtr, int soundId, int flags);
extern int   character_visual_effects_tick(int *fxState);
extern int   character_hazard_zone_check(void);
extern int   character_death_is_ready_for_cleanup(int *fxState);
extern int   soundpad_resolve_zone_node(int zoneId);
extern int   soundpad_find_nearest_entity(double minDist, float *pos);
extern void  soundpad_portal_activate(int portal);
extern void  soundpad_portal_deactivate(int portal);
extern int   player_ai_compute_aim_position(int *rd);
extern int   player_ai_is_story_mode(int charType);
extern void  aiactor_update_passability_flag(int actor);         // aiactor_system.cpp
extern void  aiactor_footstep_sound_update(int actor);           // this file (fwd)

extern int   player_ai_drop_all_weapons(unsigned long long, double, double, double,
                          unsigned long long, unsigned long long, unsigned long long,
                          unsigned long long, int *rd);
extern void  FUN_80284ba4(unsigned long long, double, double, double, unsigned long long,
                          unsigned long long, unsigned long long, unsigned long long, int actor);
extern void  FUN_801c009c(int rd);
extern void  FUN_8038b4cc(int dst, int val, int count);
extern void  FUN_8002413c(double amount, int rd);
extern void  FUN_80237b50(double amount, int channel, int actorIndex);
extern void  FUN_802852c4(void);
extern unsigned long long FUN_80136810(void);   // aiactor_sync_room_change (this file)
extern void  FUN_80281c64(unsigned long long, double, double, double, double, double,
                          double, double, int actor);
extern int   FUN_80278ff0(void);
extern void  FUN_80278d6c(void);

// ---------------------------------------------------------------------------
// aiactor_footstep_sound_update 0x80135e58
// ---------------------------------------------------------------------------
void aiactor_footstep_sound_update(int actor)
{
    float vol;
    float pitch;
    int soundId;

    soundId = 0xbfb;
    vol = FLOAT_80615cb8;
    if ((*(u32 *)(*(int *)(actor + 0x7c) + 0xc6c) & 4) != 0) {
        vol = FLOAT_80615ca8;
    }
    pitch = FLOAT_80615cbc;
    if (*(short *)(actor + 0x94) == 0x609) {
        soundId = 0x365;
        vol = FLOAT_80615cc0;
        pitch = FLOAT_80615cc4;
    }
    sound_update_3d_source((double)vol, (double)pitch, (double)FLOAT_80615ca8,
                           actor + 0x228, actor + 100, soundId, 0);
    return;
}

// ---------------------------------------------------------------------------
// aiactor_instance_tick 0x80135ecc
// MASSIVE per-frame update for one AI actor.
// ---------------------------------------------------------------------------
void aiactor_instance_tick(unsigned long long param_1, double param_2, double param_3,
                           double param_4, double param_5, double param_6,
                           double param_7, double param_8)
{
    float fdx;
    int actor;
    int a;
    int b;
    int *pZone;
    u32 mask;
    int reactivateCount;
    int deactivateStoreOff;
    int slotOff;
    int idx;
    int handle;
    int hEnt;
    unsigned long long ctx;
    double dtStep;
    int pendingReactivate[5];   // aiStack_6c
    int *rd;

    ctx = FUN_803826d0();
    actor = (int)(u32)ctx;
    rd = *(int **)(actor + 0x7c);
    reactivateCount = 0;
    deactivateStoreOff = 0;

    if ((DAT_806120fc == 0) ||
        (character_visual_effects_tick(rd + 0x31a), DAT_806108e4 < 1)) {
        FUN_8038271c();
        return;
    }

    if (DAT_805015bd == '\n') { a = rd[4]; b = rd[5]; }
    else                      { a = rd[6]; b = rd[7]; }
    if (b == 0 && a == 0) {
        *(u32 *)(*rd + 0x10) = *(u32 *)(*rd + 0x10) & 0xffff0000;
        *(u32 *)(*rd + 0x28) = *(u32 *)(*rd + 0x28) & 0xffffff9f;
        FUN_8038b4cc((int)(rd + 0x14), (int)0x80610990, 0x1e);  // -0x7f9ef670
        FUN_8038271c();
        return;
    }

    if ((rd[0xb] & 0x100000U) != 0) {
        if (DAT_805015bd != '\n') {
            player_ai_drop_all_weapons(0, param_2, param_3, param_4, param_5, param_6,
                                       param_7, param_8, rd);
        }
        FUN_80284ba4(0, param_2, param_3, param_4, param_5, param_6, param_7, param_8, actor);
        FUN_8038271c();
        return;
    }

    aiactor_footstep_sound_update(actor);
    FUN_801c009c((int)rd);

    // dt = frameDelta * (1.0 / frameCount)  (approximate per-frame timer step)
    dtStep = (double)(float)((double)FLOAT_806108ec *
                             (double)(float)((double)FLOAT_80615ca8 /
                                             (double)(float)((double)DAT_806109c0)));

    aiactor_update_passability_flag(actor);

    if (((rd[5] & 0x4000000U) == 0) && (rd[2] == DAT_806120f8)) {
        player_ai_compute_aim_position(rd);
    }

    if ((DAT_805015be == '\b') && (rd[2] == DAT_806120f8)) {
        // bag-tag radius scaling for the local actor
        float base = (float)rd[0x2c3];
        float span = (float)(DAT_80612610 + DAT_806120fc);
        float frac = (float)(rd[0x29a] - 1) / span;
        FUN_8002413c(-(double)(float)((double)(FLOAT_80615cc8 * base) * (double)frac - (double)base),
                     (int)rd);
    }

    // distance-to-target squared -> +0x2bd
    a = rd[1];
    if (a == 0) {
        rd[0x2bd] = (int)FLOAT_80615cac;
    }
    else {
        float dx = *(float *)(actor + 100) - *(float *)(a + 100);
        float dy = *(float *)(actor + 0x68) - *(float *)(a + 0x68);
        float dz = *(float *)(actor + 0x6c) - *(float *)(a + 0x6c);
        rd[0x2bd] = (int)(float)(dz * dz + (dx * dx + dy * dy));
    }

    // zone-node resolution / distance-to-zone-node squared -> +0x26c
    if (rd[0x19e] < 0) {
        rd[0x19f] = 0;
    }
    else {
        pZone = (int *)soundpad_resolve_zone_node(rd[0x19e]);
        rd[0x19f] = (int)pZone;
        a = rd[0x19f];
        if (a == 0) {
            rd[0x19e] = -1;
        }
        else {
            float dx = *(float *)(actor + 100) - *(float *)(a + 0xc);
            float dy = *(float *)(actor + 0x68) - *(float *)(a + 0x10);
            float dz = *(float *)(actor + 0x6c) - *(float *)(a + 0x14);
            rd[0x26c] = (int)(float)(dz * dz + (dx * dx + dy * dy));
        }
    }
    if (rd[0x19e] == -1) {
        rd[0x19e] = soundpad_find_nearest_entity((double)FLOAT_80615cac, (float *)(actor + 100));
    }

    // Advance the per-actor timer bank by dt.
    rd[0x273] = (int)(float)((double)(float)rd[0x273] + dtStep);
    rd[0x276] = (int)(float)((double)(float)rd[0x276] + dtStep);
    rd[0x2cc] = (int)(float)((double)(float)rd[0x2cc] + dtStep);
    rd[0x2cd] = (int)(float)((double)(float)rd[0x2cd] + dtStep);
    rd[0x2ce] = (int)(float)((double)(float)rd[0x2ce] + dtStep);
    rd[0x2d1] = (int)(float)((double)(float)rd[0x2d1] + dtStep);
    rd[0x2d3] = (int)(float)((double)(float)rd[0x2d3] + dtStep);
    rd[0x2a5] = (int)(float)((double)(float)rd[0x2a5] + dtStep);
    if (rd[0x2b7] == 0) {
        rd[0x2a4] = (int)(float)((double)(float)rd[0x2a4] + dtStep);
    }
    else {
        rd[0x2a4] = (int)FLOAT_80615cac;
    }

    if (DAT_805015bd == '\n') { a = rd[4]; b = rd[5]; }
    else                      { a = rd[6]; b = rd[7]; }
    if ((b != 0 || a != 0) && ((rd[5] & 0xc000000U) == 0)) {
        rd[0x2b6] = (int)(float)((double)(float)rd[0x2b6] + dtStep);
    }

    mask = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
    if ((mask & 1) == 0) {
        if ((rd[0x31b] & 4U) != 0) {
            FUN_80237b50((double)(float)((double)FLOAT_806108ec *
                                         (double)(float)((double)FLOAT_80615ca8 /
                                                         (double)(float)((double)DAT_806109c0))),
                         0x49, rd[2]);
        }
        FUN_80237b50((double)(float)((double)FLOAT_806108ec *
                                     (double)(float)((double)FLOAT_80615ca8 /
                                                     (double)(float)((double)DAT_806109c0))),
                     0x45, rd[2]);
        FUN_802852c4();
    }

    // Clear transient state bits when the corresponding state flags cleared.
    if ((rd[7] & 0x40000U)  == 0) rd[10] = rd[10] & 0xfffffff7;
    if ((rd[7] & 0x400000U) == 0) rd[10] = rd[10] & 0xfffffffb;
    if ((rd[7] & 0x800000U) == 0) rd[10] = rd[10] & 0xffffffef;

    // Portal activate/deactivate as the actor's zone membership changes.
    if (rd[0x19e] == rd[0x1a1]) {
        idx = 0;
        slotOff = 0;
        do {
            mask = *(u32 *)((int)rd + slotOff + 0xe7c);
            if (mask != 0) {
                hEnt = DAT_80611c24 + (mask >> 0x16) * 0x2e8;
                if (*(u32 *)(hEnt + 0x2c) != mask) {
                    hEnt = 0;
                    *(int *)((int)rd + slotOff + 0xe7c) = 0;
                }
                soundpad_portal_activate(*(int *)(*(int *)(hEnt + 0x7c) + 0x44));
                *(int *)((int)rd + slotOff + 0xe7c) = 0;
            }
            idx = idx + 1;
            slotOff = slotOff + 4;
        } while (idx < 3);
    }
    else {
        idx = 0;
        slotOff = 0;
        do {
            mask = *(u32 *)((int)rd + slotOff + 0xe7c);
            if (mask != 0) {
                hEnt = DAT_80611c24 + (mask >> 0x16) * 0x2e8;
                if (*(u32 *)(hEnt + 0x2c) != mask) {
                    hEnt = 0;
                    *(int *)((int)rd + slotOff + 0xe7c) = 0;
                }
                hEnt = *(int *)(hEnt + 0x7c);
                soundpad_portal_deactivate(*(int *)(hEnt + 0x44));
                pendingReactivate[reactivateCount] = *(int *)(hEnt + 0x44);
                reactivateCount = reactivateCount + 1;
            }
            idx = idx + 1;
            slotOff = slotOff + 4;
        } while (idx < 3);
    }

    rd[0xb] = rd[0xb] & 0xfffff7b7;
    character_hazard_zone_check();

    if (character_death_is_ready_for_cleanup(rd + 0x31a) == 0) {
        // Not ready to clean up: re-activate the portals we deactivated.
        for (idx = reactivateCount; idx != 0; idx = idx + -1) {
            soundpad_portal_activate(pendingReactivate[idx - 1]);
        }
        FUN_8038271c();
        return;
    }

    // Commit the queued behavior transition (+0x2ab/+0x2aa).
    if (rd[0x283] != 0 || rd[0x282] != 0) {
        bool keep = false;
        if (rd[0x2ab] == 1 && rd[0x2aa] == 0) {
            mask = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
            if ((mask & 1) != 0) keep = true;
        }
        if (!keep) {
            rd[0x2ab] = rd[0x283];
            rd[0x2aa] = rd[0x282];
        }
        rd[0x283] = 0;
        rd[0x282] = 0;
    }

    if (rd[1] != rd[0x2a3]) {
        rd[0xb] = rd[0xb] & 0xffffff7f;
        rd[0x2a3] = rd[1];
    }

    // Story-mode auto-behavior arming (special mode 0x13).
    if (((DAT_806118dc == 0) && (DAT_805015be == '\x13')) && (DAT_80612610 <= rd[2]) &&
        ((player_ai_is_story_mode((int)*(short *)(rd + 0x12)) != 0) && ((rd[9] & 8U) == 0))) {
        mask = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
        if ((mask & 0x10003) == 0) {
            bool skip = false;
            if (rd[0x2ab] == 1 && rd[0x2aa] == 0) {
                mask = (DAT_805015bd == '\n') ? (u32)rd[5] : (u32)rd[7];
                if ((mask & 1) == 0) skip = true;
            }
            if (!skip) {
                rd[0x2ab] = 0x10000;
                rd[0x2aa] = 0;
            }
        }
    }

    // Movement dispatch + optional replay record.
    ctx = FUN_80136810();
    FUN_80281c64(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8, actor);
    if ((FUN_80278ff0() != 0) && (rd[2] == DAT_806120f8)) {
        FUN_80278d6c();
    }

    for (idx = reactivateCount; idx != 0; idx = idx + -1) {
        soundpad_portal_activate(pendingReactivate[idx - 1]);
    }
    FUN_8038271c();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_clear_movement_flags 0x801367a0
// ---------------------------------------------------------------------------
void aiactor_clear_movement_flags(int actor, u32 param_2, int param_3, u32 prevState,
                                  u32 param_5, u32 newState)
{
    int rd;
    (void)param_2; (void)param_3; (void)param_5;

    rd = *(int *)(actor + 0x7c);
    *(u32 *)(rd + 0x28) = *(u32 *)(rd + 0x28) & 0xfffbb7fc;
    *(u32 *)(rd + 0x2c) = *(u32 *)(rd + 0x2c) & 0xfffffbde;
    if ((newState & 0xc000000) == 0) {
        return;
    }
    if ((prevState & 0xc000000) != 0) {
        return;
    }
    *(u32 *)(rd + 0x28) = *(u32 *)(rd + 0x28) & 0xffffff7f;
    return;
}

// ---------------------------------------------------------------------------
// aiactor_sync_room_change 0x80136810
// ---------------------------------------------------------------------------
unsigned long long aiactor_sync_room_change(void)
{
    float floorReset;
    int cur;
    u32 curFlags;
    u32 mode;
    int rd;
    int actor;
    extern unsigned long long FUN_803826f0(void);   // fp frame enter
    extern void FUN_8038273c(void);                  // fp frame leave
    extern int  FUN_8013db0c(int actor);
    extern int  FUN_8013db30(int actor);
    extern int  FUN_8027f5c0(void);

    actor = (int)(u32)FUN_803826f0();
    rd = *(int *)(actor + 0x7c);
    mode = (u32)(unsigned char)DAT_805015bd;
    if (mode == 10) { cur = *(int *)(rd + 0x10); curFlags = *(u32 *)(rd + 0x14); }
    else            { cur = *(int *)(rd + 0x18); curFlags = *(u32 *)(rd + 0x1c); }

    if (*(u32 *)(rd + 0xaac) != curFlags || *(int *)(rd + 0xaa8) != cur) {
        int prevA = *(int *)(rd + 0xaa8);
        u32 prevB = *(u32 *)(rd + 0xaac);
        int newA; u32 newB;
        if (mode == 10) { newA = *(int *)(rd + 0x10); newB = *(u32 *)(rd + 0x14); }
        else            { newA = *(int *)(rd + 0x18); newB = *(u32 *)(rd + 0x1c); }
        aiactor_clear_movement_flags(actor, mode, prevA, prevB, (u32)newA, newB);

        if (DAT_805015bd == 10) { newA = *(int *)(rd + 0x10); newB = *(u32 *)(rd + 0x14); }
        else                    { newA = *(int *)(rd + 0x18); newB = *(u32 *)(rd + 0x1c); }
        *(u32 *)(rd + 0xab4) = newB;
        *(int *)(rd + 0xab0) = newA;

        if (DAT_805015bd == 10) {
            *(u32 *)(rd + 0x14) = *(u32 *)(rd + 0xaac);
            *(int *)(rd + 0x10) = *(int *)(rd + 0xaa8);
        }
        else {
            *(u32 *)(rd + 0x1c) = *(u32 *)(rd + 0xaac);
            *(int *)(rd + 0x18) = *(int *)(rd + 0xaa8);
        }
        floorReset = FLOAT_80615cac;
        *(float *)(rd + 0x9cc) = FLOAT_80615cac;
        *(float *)(rd + 0xb30) = floorReset;
        *(float *)(rd + 0x9e0) = floorReset;
        *(int *)(rd + 0x9dc) = 0;

        if (DAT_805015bd == 10) {
            if ((*(u32 *)(rd + 0x14) & 0x4000000) == 0) {
                if (*(int *)(rd + 0xab4) == 0x4000000 && *(int *)(rd + 0xab0) == 0) {
                    FUN_8013db0c(actor);
                }
            }
            else {
                *(u32 *)(rd + 0x28) = *(u32 *)(rd + 0x28) | 0x80;
                FUN_8013db30(actor);
            }
        }
        FUN_8027f5c0();
    }
    FUN_8038273c();
    return (unsigned long long)(u32)actor;
}

// ---------------------------------------------------------------------------
// aiactor_clear_visible_node_scratch 0x80136988
// ---------------------------------------------------------------------------
void aiactor_clear_visible_node_scratch(void)
{
    DAT_80612098 = 0;
    memset_wrapper((int)&DAT_805885f0, 0, 0x80);
    return;
}
