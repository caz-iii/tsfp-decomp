// Address range: 0x8013943c - 0x8013a3ff | batch_367 | 2026-09-05
//
// AI actor leader/owner following through the sound-pad zone graph. Keeps the
// actor's target zone tracking its leader, steers it along the resolved zone
// path, handles blocked paths and arrival, and resolves the target zone id
// from the leader entity's type. Continuation of batches 364-366.
//
// Per-actor render object = *(actor + 0x7c); zone/nav state lives there:
//   +0x678 current zone     +0x67c leader node     +0x680 next/lateral zone
//   +0x684 stored target    +0x9a8/+0x9ac path cursors   +0xacc cached node
//   +0x30/+0x34/+0x38 current move target point
// Zone table DAT_80611b8c (0x20 stride, center at +0xc/+0x10/+0x14).
// FUN_803826xx/__save_gpr_28 = fp/gpr scratch frame helpers (actor in the high
// word, a leader/entity pointer or flag in the low word).
//
// Functions:
//   aiactor_leader_zone_update           0x8013943c  wrapper
//   aiactor_leader_zone_advance          0x80139464  step leader zone target
//   aiactor_deactivate_stuck_projectiles 0x80139534  cull stopped projectiles
//   aiactor_apply_zone_steering_force    0x8013967c  curve toward next zone
//   aiactor_handle_blocked_path          0x801397d0  set stuck-state markers
//   aiactor_zone_transition_is_clear     0x80139934  direct-move clearance test
//   aiactor_follow_zone_path             0x80139a3c  MASSIVE follow-path update
//   aiactor_follow_zone_path_alt         0x80139dec  byte-identical duplicate
//   aiactor_find_nearest_visible_ally    0x8013a19c  nearest visible teammate
//   aiactor_resolve_target_zone_id       0x8013a364  target zone from leader

#include "common.h"

// Ghidra SQRT intrinsic (single-precision). Self-contained Newton-Raphson to
// avoid a <cmath> include that conflicts with the project's own math headers.
static inline float SQRT(float x) {
    if (x <= 0.0f) return 0.0f;
    float g = x;
    for (int i = 0; i < 6; i = i + 1) g = 0.5f * (g + x / g);
    return g;
}

extern char   DAT_805015bd;      // game mode byte (0x0a coop)
extern u32    DAT_805015c0;      // cheat/flag word
extern int    DAT_806109c0;      // raw frame counter
extern int    DAT_80612098;      // visible-node count
extern int    DAT_80612610;      // base actor count
extern int    DAT_80611928;      // ally-recency window (frames)
extern int    DAT_80611d78;      // actor table base (0x141c stride)
extern int    DAT_80611d90;      // per-player state pool base
extern int    DAT_80611b8c;      // zone table base (0x20 stride)
extern int    DAT_80588610;      // visible-node pointer table
extern float  FLOAT_80615ca8;    // 1.0
extern float  FLOAT_80615cac;    // 0.0
extern float  FLOAT_80615cdc;    // steering gain
extern float  FLOAT_80615cf8;    // near proximity radius (crouched)
extern float  FLOAT_80615d14;    // steering base term
extern float  FLOAT_80615d24;    // nearest-ally init distance
extern float  FLOAT_80615d28;    // proximity radius (type 0x5f2)
extern float  FLOAT_80615d30;    // projectile min lifetime
extern float  FLOAT_80615d34;    // projectile max travel-sq
extern float  FLOAT_80615d38;    // projectile max lifetime
extern float  FLOAT_80615d3c;    // steering min distance-sq
extern float  FLOAT_80615d40;    // proximity radius (default)
extern float  FLOAT_80615d44;    // arrival distance-sq
extern float  DAT_803ccaa0;      // steering reference axis x
extern float  DAT_803ccaa4;      // steering reference axis y
extern float  DAT_803ccaa8;      // steering reference axis z

extern unsigned long long FUN_803826e4(void);   // fp frame enter (leader adv)
extern void               FUN_80382730(void);    // fp frame leave
extern int                FUN_803826f0(void);    // fp frame enter (projectiles)
extern void               FUN_8038273c(void);    // fp frame leave
extern int                FUN_803826d4(void);    // fp frame enter (ally scan)
extern void               FUN_80382720(void);    // fp frame leave
extern unsigned long long __save_gpr_28(void);   // gpr frame enter
extern void               __restore_gpr_28(void);

extern double math_sqrt(double x);
extern int   *soundpad_resolve_zone_node(int zoneId);
extern int    soundpad_find_zone_by_id(int id);
extern int    soundpad_query_adjacent_zones(int zoneId, void *outA, int outB, int a, int b, int c);
extern int    soundpad_find_closest_from_list(float *pos, int count, int listPtr);
extern int    soundpad_compute_lateral_distance(int leader, int zone, int outA, int outB, int n);
extern int    soundpad_get_entity_id_from_zone(int zone);
extern int    player_ai_projectile_pool_max_count(void);
extern int    player_ai_projectile_get_slot(int idx);
extern int    player_ai_projectile_deactivate_with_sound(double, double, double, double, double,
                          unsigned long long, unsigned long long, unsigned long long, int *proj);
extern int    aiactor_is_not_coop(void);                            // aiactor_nav.cpp
extern int    aiactor_nav_advance_path(int actor, int *nav);        // aiactor_nav.cpp
extern int    FUN_800135e8(float *p, float *segA, float *segB, float *out, float *outT);
extern int    FUN_8013e9cc(int actor, int a, int *b, int c);
extern int    FUN_8013dc70(int zoneA, int zoneB);
extern void   FUN_8013fa88(int flag);
extern void   FUN_8013cb20(int actor, int flag);
extern void   FUN_8013e340(int actor);

// forward decls for the two follow-path variants (mutual helpers)
extern void   aiactor_apply_zone_steering_force(int actor);
extern void   aiactor_handle_blocked_path(int actor);
extern u32    aiactor_zone_transition_is_clear(int actor, int fromZone, int toZone);

// ---------------------------------------------------------------------------
// aiactor_leader_zone_update 0x8013943c
// ---------------------------------------------------------------------------
void aiactor_leader_zone_update(u32 actor)
{
    extern void aiactor_leader_zone_advance(u32, u32, int);
    aiactor_leader_zone_advance(actor, 0, 0);
    return;
}

// ---------------------------------------------------------------------------
// aiactor_leader_zone_advance 0x80139464
// ---------------------------------------------------------------------------
void aiactor_leader_zone_advance(u32 param_1, u32 param_2, int queryFlags)
{
    int adjCount;
    int actor;
    int rd;
    int closest;
    unsigned long long frame;
    unsigned char adjA[76];      // auStack_b4
    int curZone;                 // local_68
    unsigned char adjB[100];     // auStack_64
    (void)param_1; (void)param_2;

    frame = FUN_803826e4();
    actor = (int)((unsigned long long)frame >> 0x20);
    rd = *(int *)(actor + 0x7c);
    if ((*(int *)(rd + 0x67c) != 0) && (*(int *)(rd + 0x678) != -1)) {
        adjCount = soundpad_query_adjacent_zones(*(int *)(rd + 0x678), adjB, (int)adjA,
                                                 0x13, 0x80000, queryFlags);
        curZone = *(int *)(rd + 0x678);
        closest = soundpad_find_closest_from_list((float *)(actor + 100), adjCount + 1, (int)&curZone);
        if (closest != *(int *)(rd + 0x678)) {
            *(int *)(rd + 0x678) = closest;
            *(int *)(rd + 0x9ac) = 0;
            *(int *)(rd + 0x9a8) = 0;
        }
        if ((int)frame != 0) {
            *(int *)(rd + 0x680) = soundpad_compute_lateral_distance(
                (int)frame, *(int *)(rd + 0x678), (int)adjB, (int)adjA, adjCount);
        }
    }
    FUN_80382730();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_deactivate_stuck_projectiles 0x80139534
// ---------------------------------------------------------------------------
void aiactor_deactivate_stuck_projectiles(unsigned long long param_1, double param_2, double param_3,
                                          double param_4, double param_5, unsigned long long param_6,
                                          unsigned long long param_7, unsigned long long param_8)
{
    float dx;
    int actor;
    int *proj;
    int owner;
    int i;
    int rd;
    double distSq;
    (void)param_1;

    actor = FUN_803826f0();
    i = 0;
    rd = *(int *)(actor + 0x7c);
    distSq = 0.0;
    while (true) {
        if (player_ai_projectile_pool_max_count() <= i) break;
        proj = (int *)player_ai_projectile_get_slot(i);
        if (((proj != (int *)0) && (*proj == 0x10)) && (proj[0x41] != 0) && (proj[0x41] == rd)) {
            owner = proj[0x56];
            if (((owner == 0) || (*(int *)(owner + 0x20) != 8)) ||
                (*(int *)(*(int *)(owner + 0x7c) + 0x84) == *(int *)(rd + 0x84))) {
                float life = (float)proj[0x26];
                if ((((double)FLOAT_80615d30 < (double)life) &&
                     (FLOAT_80615cac == (float)proj[0x19])) &&
                    ((FLOAT_80615cac == (float)proj[0x1a]) && (FLOAT_80615cac == (float)proj[0x1b]))) {
                    dx = (float)proj[7] - *(float *)(actor + 100);
                    float dy = (float)proj[8] - *(float *)(actor + 0x68);
                    float dz = (float)proj[9] - *(float *)(actor + 0x6c);
                    distSq = (double)(float)(dz * dz + (dx * dx + dy * dy));
                    if (((double)FLOAT_80615d34 < distSq) || ((double)FLOAT_80615d38 < (double)life)) {
                        player_ai_projectile_deactivate_with_sound(
                            distSq, param_2, param_3, param_4, param_5, param_6, param_7, param_8, proj);
                    }
                }
            }
            else {
                player_ai_projectile_deactivate_with_sound(
                    distSq, param_2, param_3, param_4, param_5, param_6, param_7, param_8, proj);
            }
        }
        i = i + 1;
    }
    FUN_8038273c();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_apply_zone_steering_force 0x8013967c
// ---------------------------------------------------------------------------
void aiactor_apply_zone_steering_force(int actor)
{
    int rd;
    float dx, dy, dz, distSq, gain;

    rd = *(int *)(actor + 0x7c);
    if (aiactor_is_not_coop() != 0) {
        int nextRow = *(int *)(rd + 0x680) * 0x20;
        int curRow  = *(int *)(rd + 0x678) * 0x20;
        dy = *(float *)(DAT_80611b8c + 0x10 + nextRow) - *(float *)(DAT_80611b8c + 0x10 + curRow);
        dx = *(float *)(DAT_80611b8c + 0xc + nextRow)  - *(float *)(DAT_80611b8c + 0xc + curRow);
        dz = *(float *)(DAT_80611b8c + 0x14 + nextRow) - *(float *)(DAT_80611b8c + 0x14 + curRow);
        distSq = dz * dz + dx * dx + dy * dy;
        if (FLOAT_80615d3c < distSq) {
            float inv = 1.0f / SQRT(distSq);
            float k = inv * -(FLOAT_80615cdc * distSq * inv * inv - FLOAT_80615d14);
            float scale = FLOAT_80615ca8 * k * -(FLOAT_80615cdc * distSq * k * k - FLOAT_80615d14);
            dx = dx * scale;
            dy = dy * scale;
            dz = dz * scale;
        }
        float sy = (dz * DAT_803ccaa0 - dx * DAT_803ccaa8) * FLOAT_80615cdc;
        float sz = (dx * DAT_803ccaa4 - dy * DAT_803ccaa0) * FLOAT_80615cdc;
        *(float *)(rd + 0x30) = *(float *)(rd + 0x30) + (dy * DAT_803ccaa8 - dz * DAT_803ccaa4) * FLOAT_80615cdc;
        *(float *)(rd + 0x34) = *(float *)(rd + 0x34) + sy;
        *(float *)(rd + 0x38) = *(float *)(rd + 0x38) + sz;
    }
    return;
}

// ---------------------------------------------------------------------------
// aiactor_handle_blocked_path 0x801397d0
// ---------------------------------------------------------------------------
void aiactor_handle_blocked_path(int actor)
{
    int rd;
    u32 mask;

    rd = *(int *)(actor + 0x7c);
    if (FUN_8013e9cc(actor, 7, (int *)0, 0) != -1) {
        if (DAT_805015bd == '\n') {
            *(u32 *)(rd + 0x28) = *(u32 *)(rd + 0x28) | 0x10000;
            if (*(int *)(rd + 0xaac) == 1 && *(int *)(rd + 0xaa8) == 0) {
                mask = (DAT_805015bd == '\n') ? *(u32 *)(rd + 0x14) : *(u32 *)(rd + 0x1c);
                if ((mask & 1) == 0) {
                    return;
                }
            }
            *(int *)(rd + 0xaac) = 0x200;
            *(int *)(rd + 0xaa8) = 0;
        }
        else {
            *(int *)(rd + 0xabc) = *(int *)(rd + 0x1c);
            *(int *)(rd + 0xab8) = *(int *)(rd + 0x18);
            if (*(int *)(rd + 0xaac) == 1 && *(int *)(rd + 0xaa8) == 0) {
                mask = (DAT_805015bd == '\n') ? *(u32 *)(rd + 0x14) : *(u32 *)(rd + 0x1c);
                if ((mask & 1) == 0) {
                    return;
                }
            }
            *(int *)(rd + 0xaac) = 0x80;
            *(int *)(rd + 0xaa8) = 0;
        }
    }
    return;
}

// ---------------------------------------------------------------------------
// aiactor_zone_transition_is_clear 0x80139934
// ---------------------------------------------------------------------------
u32 aiactor_zone_transition_is_clear(int actor, int fromZone, int toZone)
{
    float radius, dx, dy, dz;
    u32 result;
    int toRow;
    float outT[2];

    outT[0] = FLOAT_80615cac;
    if (toZone < 0) {
        result = 0;
    }
    else {
        toRow = DAT_80611b8c + toZone * 0x20;
        radius = FLOAT_80615d40;
        if ((*(u32 *)(*(int *)(actor + 0x7c) + 0x2c) & 0x100) != 0) {
            radius = FLOAT_80615cf8;
        }
        if (*(short *)(actor + 0x94) == 0x5f2) {
            radius = FLOAT_80615d28;
        }
        dx = *(float *)(toRow + 0xc)  - *(float *)(actor + 100);
        dy = *(float *)(toRow + 0x10) - *(float *)(actor + 0x68);
        dz = *(float *)(toRow + 0x14) - *(float *)(actor + 0x6c);
        if (radius * radius <= dz * dz + dx * dx + dy * dy) {
            if (((fromZone < 0) || (fromZone == toZone)) ||
                (FUN_800135e8((float *)(actor + 100),
                              (float *)(DAT_80611b8c + fromZone * 0x20 + 0xc),
                              (float *)(toRow + 0xc), (float *)0, outT),
                 outT[0] < FLOAT_80615ca8)) {
                result = 0;
            }
            else {
                result = 1;
            }
        }
        else {
            result = 1;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Shared body for the two byte-identical follow-path variants.
// ---------------------------------------------------------------------------
static void aiactor_follow_zone_path_body(void)
{
    float f1, f2, f3, f4;
    bool inRange;
    int actor;
    int *pNode;
    int targetZone;
    int *pOther;
    int rd;
    unsigned long long frame;

    frame = __save_gpr_28();
    actor = (int)((unsigned long long)frame >> 0x20);
    targetZone = (int)frame;
    rd = *(int *)(actor + 0x7c);
    pNode = (int *)0;
    inRange = false;
    *(u32 *)(rd + 0x28) = *(u32 *)(rd + 0x28) & 0xfffff7ff;

    if ((7999 < targetZone) && (targetZone < 0x209e)) inRange = true;
    if (!inRange) {
        inRange = false;
        if ((0x209d < targetZone) && (targetZone < 0x20b2)) inRange = true;
        if (!inRange) goto have_target;
    }
    pNode = soundpad_resolve_zone_node(targetZone);
    if (pNode == (int *)0) {
        targetZone = *(int *)(DAT_80611d90 + 0x678);
    }
    else {
        targetZone = soundpad_find_zone_by_id((int)*(short *)(pNode + 2));
    }

have_target:
    if (((*(int **)(rd + 0xacc) == (int *)0) || (*(int **)(rd + 0xacc) == pNode)) ||
        ((*(int *)(rd + 0x684) == targetZone) &&
         ((*(int *)(rd + 0x9a8) != 0) || (*(int *)(rd + 0x9ac) != 0)))) {

        if ((*(int *)(rd + 0x684) != targetZone) &&
            ((pNode == (int *)0) ||
             (pOther = soundpad_resolve_zone_node(*(int *)(rd + 0x684)), pOther != pNode))) {
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

        int next = aiactor_nav_advance_path(actor, (int *)(rd + 0x678));
        *(int *)(rd + 0x680) = next;
        next = *(int *)(rd + 0x680);
        if (next == -2) {
            FUN_8013fa88(0);
            *(int *)(rd + 0x9ac) = 0;
            *(int *)(rd + 0x9a8) = 0;
            *(int *)(rd + 0x680) = aiactor_nav_advance_path(actor, (int *)(rd + 0x678));
            FUN_8013fa88(1);
            if (*(int *)(rd + 0x680) < -1) {
                aiactor_handle_blocked_path(actor);
            }
        }
        else {
            if (-1 < next) {
                *(int *)(rd + 0x30) = *(int *)(DAT_80611b8c + next * 0x20 + 0xc);
                *(int *)(rd + 0x34) = *(int *)(DAT_80611b8c + *(int *)(rd + 0x680) * 0x20 + 0x10);
                *(int *)(rd + 0x38) = *(int *)(DAT_80611b8c + *(int *)(rd + 0x680) * 0x20 + 0x14);
                aiactor_apply_zone_steering_force(actor);
            }
            if ((*(int *)(rd + 0x680) == -1) &&
                ((*(int *)(rd + 0x678) == *(int *)(rd + 0x684)) ||
                 ((pNode != (int *)0) &&
                  (soundpad_get_entity_id_from_zone(*(int *)(rd + 0x678)) == (int)*(short *)(pNode + 2))))) {
                f1 = FLOAT_80615d44;
                if ((pNode == (int *)0) ||
                    ((int)*(short *)(pNode + 2) != *(int *)(DAT_80611b8c + *(int *)(rd + 0x678) * 0x20))) {
                    *(int *)(rd + 0xacc) = 0;
                }
                else {
                    *(int *)(rd + 0x30) = pNode[3];
                    *(int *)(rd + 0x34) = pNode[4];
                    *(int *)(rd + 0x38) = pNode[5];
                    f2 = *(float *)(actor + 100)  - *(float *)(rd + 0x30);
                    f3 = *(float *)(actor + 0x68) - *(float *)(rd + 0x34);
                    f4 = *(float *)(actor + 0x6c) - *(float *)(rd + 0x38);
                    if (f4 * f4 + f2 * f2 + f3 * f3 <= f1) {
                        *(int **)(rd + 0xacc) = pNode;
                    }
                    else {
                        FUN_8013cb20(actor, 1);
                        *(int *)(rd + 0xad0) = *(int *)(rd + 0xb14);
                    }
                }
            }
            else {
                FUN_8013cb20(actor, 1);
                *(int *)(rd + 0xad0) = *(int *)(rd + 0xb14);
                if (aiactor_zone_transition_is_clear(actor, *(int *)(rd + 0x678), *(int *)(rd + 0x680)) != 0) {
                    *(int *)(rd + 0xacc) = 0;
                    *(int *)(rd + 0x678) = *(int *)(rd + 0x680);
                    *(int *)(rd + 0x9a8) = *(int *)(rd + 0x9a8) + -1;
                }
                FUN_8013e340(actor);
            }
        }
    }
    else {
        int curRow = DAT_80611b8c + *(int *)(rd + 0x678) * 0x20;
        f1 = FLOAT_80615d40;
        if ((*(u32 *)(rd + 0x2c) & 0x100) != 0) {
            f1 = FLOAT_80615cf8;
        }
        if (*(float *)(rd + 0x9b0) <= f1 * f1) {
            *(int *)(rd + 0xacc) = 0;
        }
        else {
            *(int *)(rd + 0x30) = *(int *)(curRow + 0xc);
            *(int *)(rd + 0x34) = *(int *)(curRow + 0x10);
            *(int *)(rd + 0x38) = *(int *)(curRow + 0x14);
            FUN_8013cb20(actor, 1);
            *(int *)(rd + 0xad0) = *(int *)(rd + 0xb14);
        }
    }
    __restore_gpr_28();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_follow_zone_path 0x80139a3c  (primary, ~40 callers)
// ---------------------------------------------------------------------------
void aiactor_follow_zone_path(void)
{
    aiactor_follow_zone_path_body();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_follow_zone_path_alt 0x80139dec  (byte-identical duplicate)
// ---------------------------------------------------------------------------
void aiactor_follow_zone_path_alt(void)
{
    aiactor_follow_zone_path_body();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_find_nearest_visible_ally 0x8013a19c
// ---------------------------------------------------------------------------
void aiactor_find_nearest_visible_ally(void)
{
    float dx, dy, dz;
    int actor;
    int frameCount;
    int rd;
    int leaderObj;
    int nodeZone;
    int listOff;
    int i;
    int *pNode;
    double best, dist;

    actor = FUN_803826d4();
    frameCount = DAT_806109c0;
    best = (double)FLOAT_80615d24;
    rd = *(int *)(actor + 0x7c);
    leaderObj = 0;
    {
        int leader = *(int *)(rd + 4);
        if ((leader != 0) && (*(int *)(leader + 0x20) == 8)) {
            leaderObj = *(int *)(leader + 0x7c);
        }
    }

    listOff = 0;
    for (i = 0; i < DAT_80612098; i = i + 1) {
        pNode = *(int **)((int)&DAT_80588610 + listOff);
        nodeZone = pNode[2];
        if (((nodeZone != *(int *)(rd + 8)) && ((leaderObj == 0) || (nodeZone != *(int *)(leaderObj + 8)))) &&
            (((DAT_805015c0 & 1) == 0) || (pNode[0x21] != *(int *)(rd + 0x84)))) {
            u32 mask = (DAT_805015bd == '\n') ? *(u32 *)(rd + 0x14) : *(u32 *)(rd + 0x1c);
            if ((((mask & 1) == 0) &&
                 (((DAT_80612610 <= nodeZone) || ((pNode[9] & 8U) == 0)) &&
                  (nodeZone = FUN_8013dc70(nodeZone, *(int *)(rd + 8)),
                   DAT_80611928 - *(int *)(nodeZone + 4) < frameCount))) &&
                (*(char *)(uintptr_t)FUN_8013dc70(pNode[2], *(int *)(rd + 8)) != '\0')) {
                int obj = *pNode;
                dy = *(float *)(actor + 0x68) - *(float *)(obj + 0x68);
                dx = *(float *)(actor + 100)  - *(float *)(obj + 100);
                dz = *(float *)(actor + 0x6c) - *(float *)(obj + 0x6c);
                dist = math_sqrt((double)(dz * dz + dx * dx + dy * dy));
                if (dist < best) {
                    best = dist;
                }
            }
        }
        listOff = listOff + 4;
    }
    FUN_80382720();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_resolve_target_zone_id 0x8013a364
// ---------------------------------------------------------------------------
u32 aiactor_resolve_target_zone_id(int actor)
{
    int leader;
    int type;

    leader = *(int *)(*(int *)(actor + 0x7c) + 4);
    if (leader == 0) {
        return *(u32 *)(*(int *)(actor + 0x7c) + 0x684);
    }
    type = *(int *)(leader + 0x20);
    if (type == 0x200) {
        return *(u32 *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
    }
    if (0x1ff < type) {
        if (type != 0x2000) {
            return 0xffffffff;
        }
        return *(u32 *)(*(int *)(leader + 0x7c) + 4);
    }
    if (type != 8) {
        if (7 < type) {
            return 0xffffffff;
        }
        if (type != 1) {
            return 0xffffffff;
        }
        return *(u32 *)(leader + 0x110);
    }
    return *(u32 *)(*(int *)(leader + 0x7c) + 0x678);
}
