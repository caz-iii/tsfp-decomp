// Address range: 0x8013db0c - 0x8013e32b (subset) | batch_370 | 2026-09-05
//
// AI actor visible-list / pairwise-relationship table management, plus a couple
// of per-frame tick helpers. Continuation of batches 364-369.
//
// The "visible list" DAT_80588610 is a packed array of render-object pointers
// (rd = *(actor+0x7c)); DAT_80612098 is its live count. The pairwise-relationship
// table DAT_80612088 is a triangular matrix of 0xc-byte entries indexed by two
// actor indices (see aiactor_relation_entry): [0]=visible flag, [4]/[8]=last-seen
// tick stamps. DAT_805015bd = game mode byte (0x0a coop). DAT_80611d90 player
// state pool (0xea0 stride). FUN_803826xx = fp scratch-frame helpers.
//
// Functions:
//   aiactor_visible_list_add          0x8013db0c  push rd onto visible list
//   aiactor_visible_list_remove       0x8013db30  swap-remove rd from list
//   aiactor_get_visible_capacity      0x8013dbac  list capacity (0x20 coop)
//   aiactor_relation_entry            0x8013dc70  triangular table accessor
//   aiactor_set_relation_visible      0x8013dca8  set pairwise flag + stamps
//   aiactor_alloc_relation_table      0x8013dbd0  zero the pairwise table
//   aiactor_retarget_visible_teammate 0x8013dd58  switch to a seen teammate
//   aiactor_idle_or_follow_tick       0x8013e0ec  idle anim vs follow-path
//   aiactor_slowdown_all_stopped      0x8013e214  damp speed for stopped actors
//   aiactor_get_active_index          0x8013e324  accessor for DAT_80612100

#include "common.h"

// ---- named cross-module helpers (self-externed; common.h has no decls) -----
extern "C" {
extern void  memset_wrapper(int dst, int val, int len);
extern int   FUN_8027ea98(void);                          // current squad leader entity
extern bool  FUN_8013ee44(double range, int actor, float* pos); // proximity/LOS test
extern int   FUN_8002588c(int actor, int animId);         // anim playing?
extern void  FUN_80023280(double a, double b);            // play idle/anim
extern void  aiactor_follow_zone_path(void);              // aiactor_leader.cpp (void def)
}

// ---- raw data symbols ------------------------------------------------------
extern char DAT_805015bd;    // game mode byte (0x0a coop)
extern u32  DAT_805015c0;    // cheat/flag word
extern int  DAT_80588610;    // visible-node ptr table (base)
extern int  DAT_8058860c;    // visible-node table[-1] (used for swap-remove tail)
extern int  DAT_80612098;    // visible-node count
extern int  DAT_80612088;    // pairwise-relationship table base (0xc entries)
extern int  DAT_80612100;    // active-index accessor value
extern int  DAT_80612610;    // base player/actor count
extern int  DAT_80611928;    // current tick stamp
extern int  DAT_80611d90;    // player state pool base (0xea0 stride)

extern float FLOAT_80615cac; // 0.0
extern float FLOAT_80615cdc; // reset speed / weight scale
extern float FLOAT_80615d68; // slowdown damp factor
extern float FLOAT_80615d8c; // retarget near-distance factor
extern float FLOAT_80615d90; // retarget aggression threshold A
extern float FLOAT_80615d94; // retarget aggression threshold B
extern float FLOAT_80615d98; // retarget far-distance factor
extern float FLOAT_80615d9c; // idle anim blend param

// ---- raw function symbols --------------------------------------------------
extern "C" {
extern int  FUN_803826f0(void);                           // fp frame enter (alloc table)
extern void FUN_8038273c(void);                           // fp frame leave
extern unsigned long long FUN_803826d0(void);             // fp frame enter (retarget)
extern void FUN_8038271c(void);                           // fp frame leave
}

// aiactor_visible_list_add 0x8013db0c
void aiactor_visible_list_add(int actor)
{
    (&DAT_80588610)[DAT_80612098] = *(int*)(actor + 0x7c);
    DAT_80612098 = DAT_80612098 + 1;
}

// aiactor_visible_list_remove 0x8013db30
// Swap-removes the actor's render object from the visible list (moves the tail
// element into its slot).
void aiactor_visible_list_remove(int actor)
{
    int off = 0;
    int idx = -1;
    int scan = 0;
    int remaining = DAT_80612098;

    if (0 < DAT_80612098) {
        do {
            idx = scan;
            if (*(int*)(actor + 0x7c) == *(int*)((int)&DAT_80588610 + off)) break;
            scan = idx + 1;
            off = off + 4;
            remaining = remaining + -1;
            idx = -1;
        } while (remaining != 0);
    }
    if (idx != -1) {
        int* tail = &DAT_8058860c + DAT_80612098;
        DAT_80612098 = DAT_80612098 + -1;
        (&DAT_80588610)[idx] = *tail;
    }
}

// aiactor_get_visible_capacity 0x8013dbac
int aiactor_get_visible_capacity(void)
{
    if (DAT_805015bd == '\n') return 0x20;
    return DAT_80612610 + 10;
}

// aiactor_relation_entry 0x8013dc70
// Returns the pairwise-relationship table entry (0xc bytes) for actor indices
// (a,b), addressed as a lower-triangular matrix.
int aiactor_relation_entry(int a, int b)
{
    int lo = a;
    if (b < a) { lo = b; b = a; }
    return DAT_80612088 + (lo + (b * (b + -1) >> 1)) * 0xc;
}

// aiactor_set_relation_visible 0x8013dca8
// Marks the (a,b) relationship visible=state, stamping last-seen ticks, and
// flags a bot (index < base count) as spotted (status bit 0x1000).
void aiactor_set_relation_visible(int a, int b, int state)
{
    char* e;
    int   row;

    if (a != b) {
        if ((a < DAT_80612610) && (state != 0)) {
            row = DAT_80611d90 + b * 0xea0;
            *(u32*)(row + 0x28) = *(u32*)(row + 0x28) | 0x1000;
        }
        else if ((b < DAT_80612610) && (state != 0)) {
            row = DAT_80611d90 + a * 0xea0;
            *(u32*)(row + 0x28) = *(u32*)(row + 0x28) | 0x1000;
        }
        e = (char*)aiactor_relation_entry(a, b);
        *(int*)(e + 4) = DAT_80611928;
        if (state != (int)*e) {
            *(int*)(e + 8) = DAT_80611928;
        }
        *e = (char)state;
    }
}

// aiactor_alloc_relation_table 0x8013dbd0
// Zeroes the whole triangular pairwise table for the current actor population.
void aiactor_alloc_relation_table(void)
{
    int n = FUN_803826f0();
    int base;
    int stride;

    if (n < 1) {
        base = aiactor_relation_entry(1, 0);
    }
    else {
        base = aiactor_relation_entry(n, 0);
        memset_wrapper(base, 0, n * 0xc);
        base = base + n * 0x18;
    }
    n = n + 1;
    stride = n * 0xc;
    for (; n < aiactor_get_visible_capacity(); n = n + 1) {
        memset_wrapper(base, 0, 0xc);
        base = base + stride;
        stride = stride + 0xc;
    }
    FUN_8038273c();
}

// aiactor_retarget_visible_teammate 0x8013dd58
// If the actor's current target is a player it can see, considers switching to
// the nearest teammate of that target that is also currently visible/engaged,
// picking the closest engageable one. Returns 1 if the target was changed.
int aiactor_retarget_visible_teammate(void)
{
    float fx, fy, fz;
    bool  changed;
    int*  self;
    int   target;
    int   leader;
    int   selfEnt;
    int   bestIdx;
    int   cand;
    int   candCount;
    int   scan;
    int   poolOff;
    char* rel;
    char  hadVisible;
    int*  targetObj;
    int*  candObj;
    double bestSq, candSq;
    int    result;
    unsigned long long fr;
    int    candList[16];

    result = 0;
    fr = FUN_803826d0();
    self = (int*)((unsigned long long)fr >> 0x20);
    targetObj = (int*)0;

    if ((((DAT_805015c0 & 0x40000000) == 0) && (target = self[1], target != 0)) &&
        (target != *self)) {
        if (*(int*)(target + 0x20) == 8) targetObj = *(int**)(target + 0x7c);
        if (targetObj != (int*)0) {
            leader = FUN_8027ea98();
            if ((((*(int*)(self[0x2fc] + 0x78) == 0) && ((int)fr != 0)) &&
                 ((DAT_805015bd == '\n' && ((leader == 0 || (self[2] != *(int*)(leader + 8))))))) &&
                ((cand = targetObj[2], cand < DAT_80612610 ||
                  ((leader != 0 && (cand == *(int*)(leader + 8))))))) {
                if ((1 < DAT_80612610) || (leader != 0)) {
                    rel = (char*)aiactor_relation_entry(cand, self[2]);
                    selfEnt = *self;
                    scan = 0;
                    int tEnt = *targetObj;
                    int listOff = 0;
                    candCount = 0;
                    poolOff = 0;
                    fx = *(float*)(selfEnt + 100)  - *(float*)(tEnt + 100);
                    fy = *(float*)(selfEnt + 0x68) - *(float*)(tEnt + 0x68);
                    hadVisible = *rel;
                    fz = *(float*)(selfEnt + 0x6c) - *(float*)(tEnt + 0x6c);
                    cand = targetObj[2];
                    bestSq = (double)(fz * fz + fx * fx + fy * fy);
                    while ((scan < DAT_80612610) && (listOff < 2)) {
                        int otherIdx = *(int*)(DAT_80611d90 + poolOff + 8);
                        if ((targetObj[2] != otherIdx) &&
                            (rel = (char*)aiactor_relation_entry(otherIdx, self[2]), *rel != '\0')) {
                            candList[listOff] = *(int*)(DAT_80611d90 + poolOff + 8);
                            listOff = listOff + 1;
                        }
                        scan = scan + 1;
                        poolOff = poolOff + 0xea0;
                    }
                    candCount = listOff;
                    if (((leader != 0) && (listOff < 2)) &&
                        (rel = (char*)aiactor_relation_entry(*(int*)(leader + 8), self[2]),
                         *rel != '\0')) {
                        candList[listOff] = *(int*)(leader + 8);
                        candCount = listOff + 1;
                    }
                    for (int i = 0; i < candCount; i = i + 1) {
                        changed = false;
                        candObj = (int*)(DAT_80611d90 + candList[i] * 0xea0);
                        int cEnt = *candObj;
                        fy = *(float*)(selfEnt + 0x68) - *(float*)(cEnt + 0x68);
                        fx = *(float*)(selfEnt + 100)  - *(float*)(cEnt + 100);
                        fz = *(float*)(selfEnt + 0x6c) - *(float*)(cEnt + 0x6c);
                        candSq = (double)(fz * fz + fx * fx + fy * fy);
                        if (candObj[2] < DAT_80612610) {
                            if ((candSq < (double)(float)((double)FLOAT_80615d8c * bestSq)) &&
                                (((FUN_8013ee44((double)FLOAT_80615cac, selfEnt, (float*)(cEnt + 100)) ||
                                   ((float)candObj[0x2d3] < FLOAT_80615d90)) ||
                                  ((float)candObj[0x2d1] < FLOAT_80615d94)))) {
                                changed = true;
                            }
                        }
                        else if ((candSq < (double)(float)((double)FLOAT_80615d98 * bestSq)) &&
                                 (FUN_8013ee44((double)FLOAT_80615cac, selfEnt, (float*)(cEnt + 100)))) {
                            changed = true;
                        }
                        if ((hadVisible == '\0') || (changed)) {
                            cand = candObj[2];
                            hadVisible = '\x01';
                            bestSq = candSq;
                        }
                    }
                    if (cand != targetObj[2]) {
                        self[1] = *(int*)(DAT_80611d90 + cand * 0xea0);
                        result = 1;
                    }
                }
            }
            else {
                aiactor_relation_entry(targetObj[2], self[2]);
            }
        }
    }
    FUN_8038271c();
    return result;
}

// aiactor_idle_or_follow_tick 0x8013e0ec
// When the actor is idle and not mid-animation, either follows its zone path
// (if it has a valid pending zone target) or plays an idle-shuffle animation.
void aiactor_idle_or_follow_tick(int actor)
{
    bool  isFollowAnim;
    bool  canIdle;
    int   rd;
    u32   animType;

    canIdle      = true;
    isFollowAnim = true;
    *(float*)(*(int*)(actor + 4) + 0x100) = FLOAT_80615cdc;
    animType = *(u32*)(*(int*)(*(int*)(actor + 4) + 0x80) + 4);
    if ((1 < animType) && (animType != 0x15)) isFollowAnim = false;
    if ((!isFollowAnim) && (animType != 0x7c)) canIdle = false;

    if (((canIdle) && (FUN_8002588c(actor, 0x25) == 0)) &&
        (rd = *(int*)(actor + 0x7c), (*(u32*)(rd + 0x24) & 8) == 0) &&
        (isFollowAnim = false, (*(u32*)(rd + 0x10) & 1) == 0)) {
        int zone = *(int*)(rd + 0x684);
        if (-1 < zone) {
            if ((7999 < zone) && (zone < 0x209e)) isFollowAnim = true;
            if (!isFollowAnim) {
                int followed = ((int(*)(void))(void*)&aiactor_follow_zone_path)();
                if (followed == 0) return;
            }
        }
        FUN_80023280((double)FLOAT_80615d9c, (double)FLOAT_80615cdc);
    }
}

// aiactor_slowdown_all_stopped 0x8013e214
// Per-frame pass over the visible list: for actors that are idle/holding, set
// the "arrived" stance (0x400) and damp their linear/angular speed.
void aiactor_slowdown_all_stopped(void)
{
    float damp;
    int   rd;
    int   i = 0;
    int   off = 0;
    u32   mask;

    do {
        if (DAT_80612098 <= i) return;
        rd = *(int*)((int)&DAT_80588610 + off);
        if ((rd != 0) && (mask = *(u32*)(rd + 0x14), (*(u32*)(rd + 0x10) & 0x108) == 0)) {
            if (*(int*)(rd + 0xaac) == 1 && *(int*)(rd + 0xaa8) == 0) {
                if (DAT_805015bd != '\n') mask = *(u32*)(rd + 0x1c);
                if ((mask & 1) != 0) goto set_arrived;
            }
            else {
set_arrived:
                *(int*)(rd + 0xaac) = 0x400;
                *(int*)(rd + 0xaa8) = 0;
            }
            damp = FLOAT_80615d68;
            int obj = *(int*)(*(int**)((int)&DAT_80588610 + off) + 1);
            *(float*)(obj + 0x100) = *(float*)(obj + 0x100) * FLOAT_80615d68;
            obj = *(int*)(*(int**)((int)&DAT_80588610 + off) + 1);
            *(float*)(obj + 0x158) = *(float*)(obj + 0x158) * damp;
        }
        i = i + 1;
        off = off + 4;
    } while (true);
}

// aiactor_get_active_index 0x8013e324
int aiactor_get_active_index(void)
{
    return DAT_80612100;
}
