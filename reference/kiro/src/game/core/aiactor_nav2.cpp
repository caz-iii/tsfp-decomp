// Address range: 0x8013ce5c - 0x8013cfa3, 0x8013d9e8 - 0x8013db0b | batch_369 | 2026-09-05
//
// AI actor navigation helpers (zone reselection, ally proximity, zone arrival).
// Continuation of batches 364-368. Per-actor render object = *(actor + 0x7c);
// nav state lives there: +0x30/+0x34/+0x38 move target, +0x678 current zone,
// +0x9b0 dist-to-target-sq, +0x19e target zone index, +0xacc cached node.
// DAT_805015bd = game mode byte (0x0a coop). DAT_80611b8c zone table (0x20),
// DAT_80588610 visible-node ptr table, DAT_80612610 base count,
// DAT_80612098 visible-node count. FUN_803826xx = fp scratch-frame helpers.
//
// Functions:
//   aiactor_pick_alternate_zone  0x8013ce5c  step out of a blacklisted zone
//   aiactor_check_ally_proximity 0x8013cfa4  yield to a closer crouched ally
//   aiactor_update_zone_arrival  0x8013d9e8  clear stale target when off-zone

#include "common.h"

// ---- named cross-module helpers (self-externed; common.h has no decls) -----
extern "C" {
extern void aiactor_set_stance_flags(int actor, u32 flag);                       // aiactor_combat.cpp
extern int  soundpad_query_adjacent_zones_simple(int zoneId, void* outList, int max,
                                                 u32 excludeFlags, int includeInternal);
}

// ---- raw data symbols ------------------------------------------------------
extern char DAT_805015bd;    // game mode byte (0x0a coop)
extern int  DAT_80611bec;    // blacklisted-zone count
extern int  DAT_8053f3d8;    // blacklisted-zone id array
extern int  DAT_80611b8c;    // zone table base (0x20 stride)
extern int  DAT_80588610;    // visible-node ptr table
extern int  DAT_80612610;    // base player/actor count
extern int  DAT_80612098;    // visible-node count

extern float FLOAT_80615ca8; // 1.0
extern float FLOAT_80615d1c; // arrival-recompute distance-sq
extern float FLOAT_80615d28; // mid proximity radius-sq
extern float FLOAT_80615d44; // zone-arrival distance-sq
extern float FLOAT_80615ce0; // zone-target drift tolerance
extern float FLOAT_80615d78; // ally-avoid weight (base)
extern float FLOAT_80615d7c; // ally-avoid weight (very near)
extern float FLOAT_80615d80; // ally-avoid weight (near)
extern float FLOAT_80615d84; // ally-avoid far threshold-sq
extern float FLOAT_80615d88; // ally-avoid weight (far)

// ---- raw function symbols --------------------------------------------------
extern "C" {
extern int  FUN_803826e4(void);                          // fp frame enter (ally)
extern void FUN_80382730(void);                          // fp frame leave
extern bool FUN_8013ee44(double range, int actor, float* targetPos); // proximity/LOS test
}

// aiactor_pick_alternate_zone 0x8013ce5c
// If the actor's current zone is on the blacklist, scan its simple-adjacent
// zones for the first non-blacklisted one and hop to it, then set the move
// target to that zone centre and request the "stand" stance if not yet arrived.
void aiactor_pick_alternate_zone(int actor)
{
    float dist;
    int   count;
    int   blOff;
    int   rd;
    int   adjIdx;
    int   adjEntry;
    int   blOff2;
    int   blRemaining;
    unsigned char adjList[76];

    blOff = 0;
    rd    = *(int*)(actor + 0x7c);
    count = DAT_80611bec;
    if (0 < DAT_80611bec) {
        do {
            if (*(int*)(rd + 0x678) == *(int*)((int)&DAT_8053f3d8 + blOff)) {
                count = soundpad_query_adjacent_zones_simple(
                            *(int*)(rd + 0x678), adjList, 0x14, 0x20000, 0);
                blOff = 0;
                for (adjIdx = 0; adjIdx < count; adjIdx = adjIdx + 1) {
                    blOff2 = 0;
                    blRemaining = DAT_80611bec;
                    if (0 < DAT_80611bec) {
                        do {
                            if (*(int*)(adjList + blOff + -4) != *(int*)((int)&DAT_8053f3d8 + blOff2)) {
                                *(int*)(rd + 0x678) = *(int*)(adjList + blOff + -4);
                                break;
                            }
                            blOff2 = blOff2 + 4;
                            blRemaining = blRemaining + -1;
                        } while (blRemaining != 0);
                    }
                    blOff = blOff + 4;
                }
                break;
            }
            blOff = blOff + 4;
            count = count + -1;
        } while (count != 0);
    }
    dist = FLOAT_80615d1c;
    *(int*)(rd + 0x30) = *(int*)(DAT_80611b8c + *(int*)(rd + 0x678) * 0x20 + 0xc);
    *(int*)(rd + 0x34) = *(int*)(DAT_80611b8c + *(int*)(rd + 0x678) * 0x20 + 0x10);
    *(int*)(rd + 0x38) = *(int*)(DAT_80611b8c + *(int*)(rd + 0x678) * 0x20 + 0x14);
    if (dist < *(float*)(rd + 0x9b0)) {
        aiactor_set_stance_flags(actor, 1);
        *(int*)(rd + 0xad0) = *(int*)(rd + 0xb14);
    }
}

// aiactor_check_ally_proximity 0x8013cfa4
// Walks the visible-node list looking for a crouched/covering ally (flag 0x204)
// at similar height that is closer to the actor's current target than the actor
// itself; used to make the actor defer/hold instead of crowding the ally.
void aiactor_check_ally_proximity(void)
{
    float fx, fz;
    int   frame;
    int   target;
    int   listOff;
    int*  node;
    int*  rd;
    u32   inMask;
    double avoidW;
    double distToTargetSq;
    double distToAllySq;
    float local_48;
    (void)local_48;

    frame = FUN_803826e4();
    rd    = *(int**)(frame + 0x7c);
    target = rd[1];
    if (target != 0) {
        fz = *(float*)(frame + 0x6c) - *(float*)(target + 0x6c);
        listOff = DAT_80612610 << 2;
        fx = *(float*)(frame + 100) - *(float*)(target + 100);
        distToTargetSq = (double)(fx * fx + fz * fz);
        for (int i = DAT_80612610; i < DAT_80612098; i = i + 1) {
            node = *(int**)((int)&DAT_80588610 + listOff);
            if ((node != rd) && (*node != rd[1])) {
                inMask = (DAT_805015bd == '\n') ? (u32)node[5] : (u32)node[7];
                if (((inMask & 0x204) != 0) &&
                    (ABS((double)(*(float*)(frame + 0x68) - *(float*)(*node + 0x68))) <=
                     (double)FLOAT_80615ca8)) {
                    fz = *(float*)(frame + 0x6c) - *(float*)(*node + 0x6c);
                    local_48 = *(float*)(*node + 100);
                    fx = *(float*)(frame + 100) - local_48;
                    distToAllySq = (double)(fx * fx + fz * fz);
                    if ((double)FLOAT_80615ca8 <= distToAllySq) {
                        if ((double)FLOAT_80615d28 <= distToAllySq) {
                            avoidW = (distToAllySq < (double)FLOAT_80615d84)
                                         ? (double)FLOAT_80615d88 : (double)FLOAT_80615d78;
                        }
                        else {
                            avoidW = (double)FLOAT_80615d80;
                        }
                    }
                    else {
                        avoidW = (double)FLOAT_80615d7c;
                    }
                    float probe[3];
                    probe[0] = local_48;
                    probe[1] = *(float*)(frame + 0x68);
                    probe[2] = *(float*)(*node + 0x6c);
                    if (FUN_8013ee44(avoidW, frame, probe) && (distToAllySq < distToTargetSq)) {
                        break;
                    }
                }
            }
            listOff = listOff + 4;
        }
    }
    FUN_80382730();
}

// aiactor_update_zone_arrival 0x8013d9e8
// Per-frame pass over visible actors: clears the "moving" status bit when the
// actor has no pending nav target, and drops a stale reserved-target (+0xacc)
// once the actor's target zone centre has drifted away from where it started.
void aiactor_update_zone_arrival(void)
{
    float fx, fy, fz;
    int   listOff;
    int   i;
    int   entity;
    int   reserved;
    int   zoneRow;
    int*  node;

    listOff = DAT_80612610 << 2;
    i = DAT_80612610;
    do {
        if (DAT_80612098 <= i) return;
        node = *(int**)((int)&DAT_80588610 + listOff);
        entity = *node;
        if ((*(u32*)(entity + 0xa0) & 0xffff) == 0) {
            *(u32*)(entity + 0x28) = *(u32*)(entity + 0x28) & 0xfdffffff;
        }
        reserved = node[0x2b3];
        if (reserved != 0) {
            fx = *(float*)(reserved + 0xc) - *(float*)(entity + 100);
            fy = *(float*)(reserved + 0x10) - *(float*)(entity + 0x68);
            fz = *(float*)(reserved + 0x14) - *(float*)(entity + 0x6c);
            zoneRow = DAT_80611b8c + node[0x19e] * 0x20;
            if (FLOAT_80615d44 < fz * fz + fx * fx + fy * fy) {
                if ((ABS(*(float*)(zoneRow + 0xc)  - (float)node[0xc]) < FLOAT_80615ce0) &&
                    (ABS(*(float*)(zoneRow + 0x10) - (float)node[0xd]) < FLOAT_80615ce0) &&
                    (ABS(*(float*)(zoneRow + 0x14) - (float)node[0xe]) < FLOAT_80615ce0)) {
                    goto next;
                }
                if ((node[5] & 0x60000U) == 0) {
                    node[0x2b3] = 0;
                }
            }
        }
next:
        i = i + 1;
        listOff = listOff + 4;
    } while (true);
}
