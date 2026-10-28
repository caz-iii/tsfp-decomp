// Address range: 0x80138988 - 0x8013941f | batch_366 | 2026-09-05
//
// AI actor sound-zone navigation / pathfinding. Steps an actor's path through
// the sound-pad zone graph toward its current target zone, keeps every actor's
// zone target refreshed each frame, and re-acquires a reachable target zone via
// line-of-sight raycasts when the current one becomes blocked.
//
// The per-actor nav/path state (param_2 in the advance functions) is an int[]:
//   [0]     current zone id           [3]     target zone id
//   [4..]   short path buffer A       [0x36..] short path buffer B (group path)
//   [0xcc]  path-A cursor/length      [0xcd]  path-B cursor/length
// Zone table DAT_80611b8c (0x20 stride); portal/link table DAT_80611b90
// (0x14 stride); per-char move params DAT_8048ca94/9c (0x30 stride).
//
// Functions:
//   aiactor_is_not_coop              0x80138988  mode != coop predicate
//   aiactor_nav_advance_path         0x801389b0  step zone path (unbounded)
//   aiactor_nav_advance_path_bounded 0x80138cd0  step zone path (dist-limited)
//   aiactor_update_all_zone_targets  0x80138e74  per-frame all-actor refresh
//   aiactor_reacquire_sound_target   0x801390d8  LOS re-acquire target zone

#include "common.h"

extern char   DAT_805015bd;      // game mode byte (0x0a coop)
extern u32    DAT_805015c0;      // cheat/flag word
extern u32    DAT_805015c4;      // cheat/flag word 2
extern int    DAT_806108e4;      // subsystem-active flag (unused here)
extern int    DAT_80612610;      // base actor count
extern int    DAT_806120b8;      // 10-frame counter
extern int    DAT_806120bc;      // round-robin actor index
extern int    DAT_80611d78;      // actor table base (0x141c stride)
extern int    DAT_80611d90;      // per-player state pool (0xea0 stride)
extern int    DAT_80611b8c;      // zone table base (0x20 stride)
extern int    DAT_80611b90;      // portal/link table base (0x14 stride)
extern int    DAT_80611b94;      // trigger-zone table A base (0x20 stride)
extern int    DAT_80611b98;      // trigger-zone table B base (0x20 stride)
extern int    DAT_80611bec;      // blocked-zone list count
extern int    DAT_8053f3d8;      // blocked-zone id list (int[])
extern float  FLOAT_80615cac;    // 0.0
extern float  FLOAT_80615d1c;    // arrival-distance-sq threshold
extern float  FLOAT_80615d20;    // vertical step threshold
extern float  FLOAT_80615d24;    // reacquire best-distance init
extern float  FLOAT_80615d28;    // reacquire radius (near)
extern float  FLOAT_80615d2c;    // reacquire radius (far)
extern int    DAT_8048ca94;      // per-char move param table (x)
extern int    DAT_8048ca9c;      // per-char move param table (z)

extern unsigned long long FUN_803826dc(void);   // fp frame enter (actor index)
extern void               FUN_80382728(void);    // fp frame leave

extern int  *soundpad_resolve_zone_node(int zoneId);
extern int   soundpad_find_zone_by_id(int id);
extern int   soundpad_find_nearest_entity(double minDist, float *pos);
extern int   soundpad_find_path_to_listener(int from, int to, short *outPath, int cap);
extern int   soundpad_path_find_group(int from, int to, unsigned short *outPath, int cap);
extern int   soundpad_find_path_with_distance_limit(int from, int to, short *outPath, int cap);
extern int   soundpad_path_find_group_bounded(int from, int to, unsigned short *outPath, int cap);
extern int   soundpad_query_adjacent_zones(int zoneId, void *outA, int outB, int a, int b, int c);
extern int   soundpad_find_closest_from_list(float *pos, int count, int listPtr);
extern int   soundpad_find_entities_in_radius(double radius, float *pos, int maxCount, int outList);
extern u32   nav_advanced_movement_dispatch(double paramX, double paramZ, int actorIndex,
                          int posPtr, float *destPos, int *outHit, int a, int flags);
extern int   scene_raycast(u32 room, float *origin, float *dir, int a, float *b, float *c,
                          int *d, int mask, int e, int *f);
extern void  aiactor_reacquire_sound_target(void);   // this file (fwd)

// ---------------------------------------------------------------------------
// aiactor_is_not_coop 0x80138988
// ---------------------------------------------------------------------------
u32 aiactor_is_not_coop(void)
{
    if (DAT_805015bd != '\n') {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// aiactor_nav_advance_path 0x801389b0
// ---------------------------------------------------------------------------
int aiactor_nav_advance_path(int actor, int *nav)
{
    short zoneGroup;
    bool inRange;
    int targetZone;
    int *pNode;
    int cur;
    int nextZone;
    int rd;

    rd = 0;
    if (actor != 0) {
        rd = *(int *)(actor + 0x7c);
    }

    targetZone = nav[3];
    inRange = false;
    if ((7999 < targetZone) && (targetZone < 0x209e)) inRange = true;
    if (!inRange) {
        inRange = false;
        if ((0x209d < targetZone) && (targetZone < 0x20b2)) inRange = true;
        if (!inRange) goto have_target;
    }

    pNode = soundpad_resolve_zone_node(targetZone);
    if (pNode == (int *)0) {
        int rowBase;
        targetZone = nav[3];
        inRange = false;
        if ((7999 < targetZone) && (targetZone < 0x209e)) inRange = true;
        if (inRange) rowBase = DAT_80611b94 + (targetZone + -8000) * 0x20;
        else         rowBase = DAT_80611b98 + (targetZone + -0x209e) * 0x20;
        if (rowBase == 0) {
            targetZone = *nav;
        }
        else {
            targetZone = soundpad_find_zone_by_id((int)*(short *)(rowBase + 8));
        }
        if (targetZone < 0) {
            *(int *)(rd + 0x684) =
                *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        }
        else {
            *(int *)(rd + 0x684) = targetZone;
        }
        *(int *)(rd + 0x9ac) = 0;
        *(int *)(rd + 0x9a8) = 0;
    }
    else {
        pNode = soundpad_resolve_zone_node(nav[3]);
        targetZone = soundpad_find_zone_by_id((int)*(short *)(pNode + 2));
    }

have_target:
    cur = *nav;
    if ((cur == targetZone) || (targetZone < 0)) {
        nextZone = -1;
        nav[0xcc] = 0;
        nav[0xcd] = 0;
        return nextZone;
    }
    if (cur == -1) {
        nav[0xcc] = 0;
        nav[0xcd] = 0;
        return -2;
    }

    if ((nav[0xcc] == 0) && (nav[0xcd] == 0)) {
        if (*(short *)(DAT_80611b8c + 8 + cur * 0x20) ==
            *(short *)(DAT_80611b8c + 8 + targetZone * 0x20)) {
            nav[0xcc] = soundpad_find_path_to_listener(cur, targetZone, (short *)(nav + 4), 100);
        }
        else {
            nav[0xcd] = soundpad_path_find_group(cur, targetZone,
                                                 (unsigned short *)(nav + 0x36), 300);
            if (nav[0xcd] == 0) {
                return -2;
            }
        }
    }

    if (nav[0xcc] == 0) {
        int idx = nav[0xcd];
        zoneGroup = *(short *)(DAT_80611b8c + *nav * 0x20 + 8);
        nav[0xcd] = idx + -1;
        *(short *)(nav + 4) = *(short *)((int)nav + (idx + -1) * 2 + 0xd8);
        int link = DAT_80611b90 + *(short *)(nav + 4) * 0x14;
        int far = *(int *)(link + 4);
        if (zoneGroup != *(short *)(DAT_80611b8c + far * 0x20 + 8)) {
            far = *(int *)(link + 8);
        }
        if (*nav == far) {
            nav[0xcc] = 1;
        }
        else {
            nav[0xcc] = soundpad_find_path_to_listener(*nav, far, (short *)((int)nav + 0x12), 99) + 1;
        }
    }

    {
        int link = DAT_80611b90 + *(short *)((int)nav + nav[0xcc] * 2 + 0xe) * 0x14;
        nextZone = *(int *)(link + 4);
        if (*nav == nextZone) {
            nextZone = *(int *)(link + 8);
        }
        int off = 0;
        int n = DAT_80611bec;
        if (0 < DAT_80611bec) {
            do {
                if (nextZone == *(int *)((int)&DAT_8053f3d8 + off)) {
                    if (rd != 0) {
                        *(u32 *)(rd + 0x28) = *(u32 *)(rd + 0x28) | 0x200;
                    }
                    return -2;
                }
                off = off + 4;
                n = n + -1;
            } while (n != 0);
        }
    }
    return nextZone;
}

// ---------------------------------------------------------------------------
// aiactor_nav_advance_path_bounded 0x80138cd0
// ---------------------------------------------------------------------------
u32 aiactor_nav_advance_path_bounded(u32 param_1, int *nav)
{
    short zoneGroup;
    int cur;
    u32 result;
    int targetZone;
    (void)param_1;

    cur = *nav;
    targetZone = nav[3];
    if ((cur == targetZone) || (targetZone < 0)) {
        result = 0xffffffff;
        nav[0xcc] = 0;
        nav[0xcd] = 0;
    }
    else if (cur == -1) {
        result = 0xfffffffe;
        nav[0xcc] = 0;
        nav[0xcd] = 0;
    }
    else {
        if ((nav[0xcc] == 0) && (nav[0xcd] == 0)) {
            if (*(short *)(DAT_80611b8c + 8 + cur * 0x20) ==
                *(short *)(DAT_80611b8c + 8 + targetZone * 0x20)) {
                nav[0xcc] = soundpad_find_path_with_distance_limit(cur, targetZone,
                                                                   (short *)(nav + 4), 100);
                if (nav[0xcc] == 0) {
                    return 0xfffffffe;
                }
            }
            else {
                nav[0xcd] = soundpad_path_find_group_bounded(cur, targetZone,
                                                             (unsigned short *)(nav + 0x36), 100);
                if (nav[0xcd] == 0) {
                    return 0xfffffffe;
                }
            }
        }
        if (nav[0xcc] == 0) {
            int idx = nav[0xcd];
            zoneGroup = *(short *)(DAT_80611b8c + *nav * 0x20 + 8);
            nav[0xcd] = idx + -1;
            *(short *)(nav + 4) = *(short *)((int)nav + (idx + -1) * 2 + 0xd8);
            int link = DAT_80611b90 + *(short *)(nav + 4) * 0x14;
            int far = *(int *)(link + 4);
            if (zoneGroup != *(short *)(DAT_80611b8c + far * 0x20 + 8)) {
                far = *(int *)(link + 8);
            }
            if (*nav == far) {
                nav[0xcc] = 1;
            }
            else {
                nav[0xcc] = soundpad_find_path_with_distance_limit(*nav, far,
                                                                   (short *)((int)nav + 0x12), 99) + 1;
            }
        }
        result = 1;
    }
    return result;
}

// ---------------------------------------------------------------------------
// aiactor_update_all_zone_targets 0x80138e74
// ---------------------------------------------------------------------------
void aiactor_update_all_zone_targets(void)
{
    float dx, dy, dz;
    int *pNode;
    int zone;
    int actorObj;
    int nextZone;
    int slotOff;
    int i;
    int *rd;
    float local_d8, local_d4, local_d0;   // destination zone position
    unsigned char adjScratchA[76];        // auStack_c8 (query out A)
    int curZone;                          // local_7c
    unsigned char adjScratchB[120];       // auStack_78 (query out B)

    extern void __save_gpr_25(void);
    extern void __restore_gpr_25(void);

    __save_gpr_25();
    slotOff = 0;
    for (i = 0; i < DAT_80612610; i = i + 1) {
        rd = *(int **)(*(int *)(DAT_80611d78 + slotOff + 0x14) + 0x7c);

        // refresh current zone node + distance-sq to it (+0x26c)
        if (rd[0x19e] < 0) {
            rd[0x19f] = 0;
        }
        else {
            pNode = soundpad_resolve_zone_node(rd[0x19e]);
            rd[0x19f] = (int)pNode;
            zone = rd[0x19f];
            if (zone == 0) {
                rd[0x19e] = -1;
            }
            else {
                actorObj = *rd;
                dx = *(float *)(actorObj + 100)  - *(float *)(zone + 0xc);
                dy = *(float *)(actorObj + 0x68) - *(float *)(zone + 0x10);
                dz = *(float *)(actorObj + 0x6c) - *(float *)(zone + 0x14);
                rd[0x26c] = (int)(dz * dz + dx * dx + dy * dy);
            }
        }
        if (rd[0x19e] == -1) {
            rd[0x19e] = soundpad_find_nearest_entity((double)FLOAT_80615cac,
                                                     (float *)(*rd + 100));
        }
        if (rd[0x19e] == -1) goto done;

        // pick closest reachable adjacent zone and step toward it
        zone = soundpad_query_adjacent_zones(rd[0x19e], adjScratchB, (int)adjScratchA, 0x13, 0x80000, 0);
        curZone = rd[0x19e];
        zone = soundpad_find_closest_from_list((float *)(DAT_80611d78 + slotOff + 0xa8),
                                               zone + 1, (int)&curZone);
        if (zone != rd[0x19e]) {
            if (*(int *)(DAT_80611d78 + slotOff + 0x114) == 0x4000) {
                rd[0x19e] = zone;
            }
            else {
                int zoneRow = zone * 0x20;
                local_d8 = *(float *)(DAT_80611b8c + zoneRow + 0xc);
                local_d4 = *(float *)(DAT_80611b8c + zoneRow + 0x10);
                local_d0 = *(float *)(DAT_80611b8c + zoneRow + 0x14);
                nav_advanced_movement_dispatch(
                    (double)*(float *)(&DAT_8048ca94 + *(short *)(rd + 0x12) * 0x30),
                    (double)*(float *)(&DAT_8048ca9c + *(short *)(rd + 0x12) * 0x30),
                    rd[2], DAT_80611d78 + slotOff + 0xa8, &local_d8, (int *)0, 0, 8);
                int zoneBase = DAT_80611b8c + zoneRow;
                dx = local_d8 - *(float *)(zoneBase + 0xc);
                dy = local_d4 - *(float *)(zoneBase + 0x10);
                dz = local_d0 - *(float *)(zoneBase + 0x14);
                if (dz * dz + dx * dx + dy * dy < FLOAT_80615d1c) {
                    rd[0x19e] = zone;
                }
            }
        }
        slotOff = slotOff + 0x141c;
    }
    (void)nextZone;

    DAT_806120b8 = DAT_806120b8 + 1;
    if (10 < DAT_806120b8) {
        aiactor_reacquire_sound_target();
        DAT_806120bc = DAT_806120bc + 1;
        DAT_806120b8 = 0;
        if (DAT_80612610 <= DAT_806120bc) {
            DAT_806120bc = 0;
        }
    }
done:
    __restore_gpr_25();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_reacquire_sound_target 0x801390d8
// ---------------------------------------------------------------------------
void aiactor_reacquire_sound_target(void)
{
    float dx, dy, dz;
    int actorObj;
    int zone;
    int found;
    u32 stateMask;
    int best;
    int listOff;
    int i;
    int *rd;
    double bestDistSq, distSq;
    float local_b0, local_ac, local_a8;   // ray direction
    float local_a4, local_a0, local_9c;   // ray origin
    float local_98, local_94, local_90;   // target zone position
    int zoneList[33];                     // local_8c

    rd = (int *)(DAT_80611d90 + FUN_803826dc() * 0xea0);
    actorObj = *rd;

    if ((rd[0x19f] != 0) && (stateMask = (u32)rd[5], (stateMask & 0x1000000) == 0)) {
        if (DAT_805015bd != '\n') {
            stateMask = (u32)rd[7];
        }
        if ((stateMask & 0x201) == 0) {
            local_a4 = *(float *)(actorObj + 100);
            local_a0 = *(float *)(actorObj + 0x68);
            local_9c = *(float *)(actorObj + 0x6c);
            zone = rd[0x19f];
            local_98 = *(float *)(zone + 0xc);
            local_94 = *(float *)(zone + 0x10);
            local_90 = *(float *)(zone + 0x14);

            if (local_94 - local_a0 <= FLOAT_80615d20) {
                // reachable via nav step -> keep current target
                nav_advanced_movement_dispatch(
                    (double)*(float *)(&DAT_8048ca94 + *(short *)(rd + 0x12) * 0x30),
                    (double)*(float *)(&DAT_8048ca9c + *(short *)(rd + 0x12) * 0x30),
                    rd[2], (int)&local_a4, &local_98, (int *)0, 0, 0x200008);
                zone = rd[0x19f];
                dx = local_98 - *(float *)(zone + 0xc);
                dy = local_94 - *(float *)(zone + 0x10);
                dz = local_90 - *(float *)(zone + 0x14);
                if (dz * dz + dx * dx + dy * dy < FLOAT_80615d1c) goto done;
            }
            else {
                // reachable via straight LOS raycast -> keep current target
                local_a0 = local_a0 + FLOAT_80615d20;
                local_b0 = local_98 - local_a4;
                local_ac = local_94 - local_a0;
                local_a8 = local_90 - local_9c;
                if (scene_raycast(*(u32 *)(actorObj + 0x34), &local_a4, &local_b0, 0,
                                  (float *)0, (float *)0, (int *)0, 0x2000078, 0, (int *)0) == 0) {
                    goto done;
                }
            }

            // current target unreachable: gather nearby zones, LOS-test each,
            // pick the closest visible one.
            bestDistSq = (double)FLOAT_80615d24;
            best = -1;
            found = soundpad_find_entities_in_radius((double)FLOAT_80615d28,
                                                     (float *)(actorObj + 100), 0x14, (int)zoneList);
            if (found == 0) {
                found = soundpad_find_entities_in_radius((double)FLOAT_80615d2c,
                                                         (float *)(actorObj + 100), 0x14, (int)zoneList);
            }
            local_a4 = *(float *)(actorObj + 100);
            local_a0 = FLOAT_80615d20 + *(float *)(actorObj + 0x68);
            local_9c = *(float *)(actorObj + 0x6c);
            listOff = 0;
            for (i = 0; i < found; i = i + 1) {
                int zoneRow = *(int *)((int)zoneList + listOff) * 0x20;
                int zoneBase = DAT_80611b8c + zoneRow;
                local_b0 = *(float *)(zoneBase + 0xc) - *(float *)(actorObj + 100);
                local_ac = *(float *)(zoneBase + 0x10) - *(float *)(actorObj + 0x68);
                local_a8 = *(float *)(DAT_80611b8c + zoneRow + 0x14) - *(float *)(actorObj + 0x6c);
                if (scene_raycast(*(u32 *)(actorObj + 0x34), &local_a4, &local_b0, 0,
                                  (float *)0, (float *)0, (int *)0, 0x2000078, 0, (int *)0) == 0) {
                    int zb = DAT_80611b8c + *(int *)((int)zoneList + listOff) * 0x20;
                    dx = *(float *)(actorObj + 100)  - *(float *)(zb + 0xc);
                    dy = *(float *)(actorObj + 0x68) - *(float *)(zb + 0x10);
                    dz = *(float *)(actorObj + 0x6c) - *(float *)(zb + 0x14);
                    distSq = (double)(dz * dz + dx * dx + dy * dy);
                    if (distSq < bestDistSq) {
                        best = i;
                        bestDistSq = distSq;
                    }
                }
                listOff = listOff + 4;
            }
            if (best != -1) {
                rd[0x19e] = zoneList[best];
                rd[0x26b] = 0;
                rd[0x26a] = 0;
            }
        }
    }
done:
    FUN_80382728();
    return;
}
