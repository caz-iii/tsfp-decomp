// Address range: 0x8013c540 - 0x8013cb1f | batch_368 | 2026-09-05
//
// AI actor aim / anchor-position helpers. Continuation of batches 364-367.
// Both run per-actor per-frame off the AI behaviour dispatcher.
//
//   aiactor_update_zone_anchor_position 0x8013c540  keeps a smoothed "home"
//       anchor point (+0xaf8/+0xafc/+0xb00 on the render object) tracking the
//       actor as it moves, re-projecting through portals so the anchor stays in
//       a reachable zone; snaps to a resolved zone node when floor query fails.
//   aiactor_update_aim_height          0x8013c8f8  computes the muzzle/aim world
//       point (+0xbb4/+0xbb8/+0xbbc) from weapon class and bone matrix, then
//       smooths the tracked aim Y (+0xbc0) toward it in coop.
//
// Per-actor render object = *(actor + 0x7c). Actor transform: +0x64 x, +0x68 y,
// +0x6c z, +0x34 current zone, +0x28 status flags, +0x10 mode flags.
// DAT_805015bd = game mode byte (0x0a coop). FUN_803826xx = fp scratch-frame
// helpers (actor handle in the high word, secondary index in the low word).

#include "common.h"

// ---- raw data symbols ------------------------------------------------------
extern char  DAT_805015bd;   // game mode byte (0x0a coop)
extern float DAT_8048ca94;   // per-weapon lateral offset table (0x30 stride)
extern float DAT_8048ca9c;   // per-weapon forward offset table (0x30 stride)

extern float FLOAT_80615cec; // story-mode aim Y drop
extern float FLOAT_80615ce8; // aim-Y smoothing step (coop)
extern float FLOAT_80615cf8; // (unused here)
extern float FLOAT_80615d14; // medium weapon eye height
extern float FLOAT_80615d64; // default eye-height base
extern float FLOAT_80615d68; // default eye-height + bone offset
extern float FLOAT_80615d6c; // floor-probe height offset
extern float FLOAT_80615d70; // tall weapon eye height

// ---- named cross-module helpers (self-externed; common.h has no decls) -----
extern "C" {
extern int   player_ai_is_heavy_weapon(int weaponType);
extern int   player_ai_is_sniper_weapon(int weaponType);
extern int   player_ai_is_story_mode(int weaponType);
extern int*  soundpad_resolve_zone_node(int zoneId);
extern void* soundpad_resolve_entity_to_zone(int entityId);
extern void  mtx4_multiply(float* a, float* b, float* out);
// wide/placeholder-ABI helpers invoked through typed casts below
extern void  scene_ray_portal_intersect(void);
extern void  floor_height_from_position(void);
extern void  floor_query_with_interact(void);
extern void  entity_build_local_matrix(void);
}

// ---- raw function symbols --------------------------------------------------
extern unsigned long long FUN_803826e8(void);   // fp frame enter (anchor)
extern void               FUN_80382734(void);    // fp frame leave
extern int                FUN_803826f0(void);    // fp frame enter (aim)
extern void               FUN_8038273c(void);    // fp frame leave
extern void               FUN_80380330(int frame, u32* outPos);  // heavy-weapon muzzle

// scene_ray_portal_intersect, floor_height_from_position, floor_query_with_interact,
// soundpad_resolve_zone_node, soundpad_resolve_entity_to_zone, entity_build_local_matrix
// and mtx4_multiply resolve through the shared header (generated_decls.h). Several of
// those shared decls are placeholder `(void)` stubs whose real arity differs from these
// call sites, so we invoke them through typed function-pointer casts to stay compile-clean
// without redeclaring (which would conflict, C2371).
typedef int   (*ray_portal_fn)(double, double, float*, float*, int, int, int, float*, float*);
typedef int   (*floor_h_fn)(double, double);
typedef float (*floor_q_fn)(double, double, void*, int, int*, int);
typedef void  (*build_mtx_fn)(float*, int, int, int);
#define RAY_PORTAL(...)  ((ray_portal_fn)(void*)&scene_ray_portal_intersect)(__VA_ARGS__)
#define FLOOR_H(...)     ((floor_h_fn)(void*)&floor_height_from_position)(__VA_ARGS__)
#define FLOOR_Q(...)     ((floor_q_fn)(void*)&floor_query_with_interact)(__VA_ARGS__)
#define BUILD_LOCAL_MTX(...) ((build_mtx_fn)(void*)&entity_build_local_matrix)(__VA_ARGS__)

// ---------------------------------------------------------------------------
// aiactor_update_zone_anchor_position 0x8013c540
// ---------------------------------------------------------------------------
void aiactor_update_zone_anchor_position(void)
{
    float fVar1;
    int   pZone;
    int   fromZone;
    int*  node;
    int   nextZone;
    int   anchorZoneOut;
    int   frame;
    int   rd;
    double dz;
    double extraout_f1;
    double extraout_f1_00;
    double dcur;
    unsigned long long fr;
    int   flagIdx;
    float snapPos[3];        // local_48/44/40
    float anchor[3];         // local_30/2c/28  (previous anchor)
    float delta[3];          // local_3c/38/34  (movement delta)

    fr    = FUN_803826e8();
    frame = (int)((unsigned long long)fr >> 0x20);
    rd    = *(int*)(frame + 0x7c);

    if (((*(u32*)(rd + 0x2c) & 0x10000) == 0) || (*(int*)(rd + 0x678) == -1)) {
        if ((int)fr == 0) {
            anchor[0] = *(float*)(rd + 0xaf8);
            anchor[1] = *(float*)(rd + 0xafc);
            anchor[2] = *(float*)(rd + 0xb00);
            delta[0]  = *(float*)(frame + 100) - anchor[0];
            fVar1 = FLOAT_80615d64;
            if ((*(u32*)(frame + 0x28) & 0x100) == 0) {
                fVar1 = FLOAT_80615d68 + *(float*)(*(int*)(*(int*)(frame + 4) + 0x10) + 0x34);
            }
            nextZone  = 0;
            delta[1]  = (*(float*)(frame + 0x68) + fVar1) - anchor[1];
            dcur      = (double)*(float*)(frame + 0x6c);
            delta[2]  = (float)(dcur - (double)anchor[2]);
            *(float*)(rd + 0xaf8) = anchor[0] + delta[0];
            *(float*)(rd + 0xafc) = anchor[1] + delta[1];
            dz = (double)anchor[2];
            *(float*)(rd + 0xb00) = (float)(dz + (double)delta[2]);
            fromZone = 0;
            pZone    = *(int*)(frame + 0x34);
            while (pZone != fromZone) {
                nextZone = RAY_PORTAL(dz, dcur, anchor, delta, pZone, fromZone, 0,
                                                      anchor, delta);
                fromZone = pZone;
                dz       = extraout_f1_00;
                pZone    = nextZone;
            }
            *(int*)(frame + 0x34) = nextZone;
        }
        else {
            *(u32*)(rd + 0xaf8) = *(u32*)(frame + 100);
            fVar1 = FLOAT_80615d64;
            if ((*(u32*)(frame + 0x28) & 0x100) == 0) {
                fVar1 = FLOAT_80615d68 + *(float*)(*(int*)(*(int*)(frame + 4) + 0x10) + 0x34);
            }
            dz = (double)FLOAT_80615cf8;
            *(float*)(rd + 0xafc) = *(float*)(frame + 0x68) + fVar1;
            *(u32*)(rd + 0xb00) = *(u32*)(frame + 0x6c);
            *(int*)(frame + 0x34) = FLOOR_H(dz, dz);
        }

        if ((*(int*)(frame + 0x34) == 0) && ((*(u32*)(frame + 0x10) & 0xffff) != 0)) {
            node = soundpad_resolve_zone_node(*(int*)(rd + 0x678));
            if ((node[1] & 0x800U) != 0) {
                node = (int*)soundpad_resolve_entity_to_zone((int)*(short*)(node + 2));
            }
            snapPos[0] = *(float*)(frame + 100);
            snapPos[1] = FLOAT_80615d6c + *(float*)(frame + 0x68);
            snapPos[2] = *(float*)(frame + 0x6c);
            flagIdx = *(short*)(rd + 0x48) * 0x30;
            dz = (double)FLOOR_Q(
                (double)*(float*)(&DAT_8048ca94 + flagIdx),
                (double)*(float*)(&DAT_8048ca9c + flagIdx), snapPos, 0, (int*)0, 0);
            snapPos[1] = (float)dz;
            *(float*)(frame + 0x68) = (float)dz;
            flagIdx = *(short*)(rd + 0x48) * 0x30;
            *(int*)(frame + 0x34) = FLOOR_H(
                (double)*(float*)(&DAT_8048ca94 + flagIdx),
                (double)*(float*)(&DAT_8048ca9c + flagIdx));
            if (*(int*)(frame + 0x34) == 0) {
                *(int*)(frame + 100)  = node[3];
                *(int*)(frame + 0x68) = node[4];
                *(int*)(frame + 0x6c) = node[5];
                *(int*)(frame + 0x34) = (int)*(short*)((int)node + 10);
            }
            *(u32*)(rd + 0xaf8) = *(u32*)(frame + 100);
            *(u32*)(rd + 0xafc) = *(u32*)(frame + 0x68);
            *(u32*)(rd + 0xb00) = *(u32*)(frame + 0x6c);
        }
    }
    else {
        node = soundpad_resolve_zone_node(*(int*)(rd + 0x678));
        if ((node[1] & 0x800U) != 0) {
            node = (int*)soundpad_resolve_entity_to_zone((int)*(short*)(node + 2));
        }
        *(int*)(frame + 0x34) = (int)*(short*)((int)node + 10);
        *(u32*)(rd + 0xaf8) = *(u32*)(frame + 100);
        fVar1 = FLOAT_80615d64;
        if ((*(u32*)(frame + 0x28) & 0x100) == 0) {
            fVar1 = FLOAT_80615d68 + *(float*)(*(int*)(*(int*)(frame + 4) + 0x10) + 0x34);
        }
        *(float*)(rd + 0xafc) = *(float*)(frame + 0x68) + fVar1;
        *(u32*)(rd + 0xb00) = *(u32*)(frame + 0x6c);
        anchor[0] = (float)node[3];
        fVar1 = FLOAT_80615d64;
        if ((*(u32*)(frame + 0x28) & 0x100) == 0) {
            fVar1 = FLOAT_80615d68 + *(float*)(*(int*)(*(int*)(frame + 4) + 0x10) + 0x34);
        }
        anchor[1] = (float)node[4] + fVar1;
        anchor[2] = (float)node[5];
        dcur = (double)anchor[2];
        dz   = (double)*(float*)(rd + 0xaf8);
        delta[0] = (float)(dz - (double)anchor[0]);
        delta[1] = *(float*)(rd + 0xafc) - anchor[1];
        delta[2] = (float)((double)*(float*)(rd + 0xb00) - dcur);
        fromZone = 0;
        pZone    = *(int*)(frame + 0x34);
        nextZone = 0;
        while (pZone != fromZone) {
            nextZone = RAY_PORTAL(dz, dcur, anchor, delta, pZone, fromZone, 0,
                                                  anchor, delta);
            fromZone = pZone;
            dz       = extraout_f1;
            pZone    = nextZone;
        }
        *(int*)(frame + 0x34) = nextZone;
        if (*(int*)(frame + 0x34) == 0) {
            *(int*)(frame + 0x34) = (int)*(short*)((int)node + 10);
        }
    }
    FUN_80382734();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_update_aim_height 0x8013c8f8
// Computes the aim/muzzle world point per weapon class, then in coop smooths
// the tracked aim Y toward it by a fixed step.
// ---------------------------------------------------------------------------
void aiactor_update_aim_height(void)
{
    float fHi;
    float fMed;
    int   frame;
    int   weap;
    int   rd;
    // Original stack layout: afStack_98[12], then local_68/64/60 (the multiply
    // result's translation row at float offsets 12/13/14), then afStack_58[22].
    // Keep them contiguous so mtx4_multiply's write lands where the reads expect.
    float mtxA[15];   // afStack_98[0..11] + translation row [12..14] (local_68/64/60)
    float mtxLocal[22]; // afStack_58 (local transform build)

    frame = FUN_803826f0();
    fHi   = FLOAT_80615d70;
    fMed  = FLOAT_80615d14;
    rd    = *(int*)(frame + 0x7c);
    weap  = (int)*(short*)(rd + 0x48);

    if (weap == 0x45) {
        *(u32*)(rd + 0xbb4)  = *(u32*)(frame + 100);
        *(float*)(rd + 3000) = fHi + *(float*)(frame + 0x68);
        *(u32*)(rd + 0xbbc)  = *(u32*)(frame + 0x6c);
    }
    else if (weap == 0x4d) {
        *(u32*)(rd + 0xbb4)  = *(u32*)(frame + 100);
        *(float*)(rd + 3000) = fMed + *(float*)(frame + 0x68);
        *(u32*)(rd + 0xbbc)  = *(u32*)(frame + 0x6c);
    }
    else if (player_ai_is_heavy_weapon(weap) != 0) {
        FUN_80380330(frame, (u32*)(rd + 0xbb4));
    }
    else if (player_ai_is_sniper_weapon((int)*(short*)(rd + 0x4a)) != 0) {
        *(u32*)(rd + 0xbb4)  = *(u32*)(frame + 100);
        *(float*)(rd + 3000) = FLOAT_80615d14 + *(float*)(frame + 0x68);
        *(u32*)(rd + 0xbbc)  = *(u32*)(frame + 0x6c);
    }
    else if (((*(u32*)(frame + 0x28) & 0x100) == 0) && ((*(u32*)(frame + 0x10) & 0xffff) != 0)) {
        int boneIdx;
        if ((*(u32*)(frame + 0x28) & 0x200000) == 0) {
            boneIdx = 0;
        }
        else {
            boneIdx = *(int*)(*(int*)(*(int*)(frame + 4) + 0x84) + 0x18);
        }
        BUILD_LOCAL_MTX(mtxLocal, frame, 1, 0);
        mtx4_multiply((float*)(*(int*)(*(int*)(frame + 4) + 0x10) + boneIdx * 0x40), mtxLocal, mtxA);
        *(float*)(rd + 0xbb4) = mtxA[12];   // local_68
        *(float*)(rd + 3000)  = mtxA[13];   // local_64
        *(float*)(rd + 0xbbc) = mtxA[14];   // local_60
        if (player_ai_is_story_mode((int)*(short*)(rd + 0x4a)) == 0) {
            *(float*)(rd + 3000) = *(float*)(rd + 3000) - FLOAT_80615cec;
        }
    }
    else {
        *(u32*)(rd + 0xbb4) = *(u32*)(frame + 100);
        fMed = FLOAT_80615d64;
        if ((*(u32*)(frame + 0x28) & 0x100) == 0) {
            fMed = FLOAT_80615d68 + *(float*)(*(int*)(*(int*)(frame + 4) + 0x10) + 0x34);
        }
        *(float*)(rd + 3000) = *(float*)(frame + 0x68) + fMed;
        *(u32*)(rd + 0xbbc)  = *(u32*)(frame + 0x6c);
    }

    if (DAT_805015bd == '\n') {
        float aimY  = *(float*)(rd + 3000);
        float trkY  = *(float*)(rd + 0xbc0);
        if (FLOAT_80615ce8 < ABS(aimY - trkY)) {
            if (aimY <= trkY) {
                *(float*)(rd + 0xbc0) = trkY - FLOAT_80615ce8;
            }
            else {
                *(float*)(rd + 0xbc0) = trkY + FLOAT_80615ce8;
            }
        }
        else {
            *(float*)(rd + 0xbc0) = aimY;
        }
    }
    else {
        *(u32*)(rd + 0xbc0) = *(u32*)(rd + 3000);
    }
    FUN_8038273c();
    return;
}
