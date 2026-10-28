// Address range: 0x8013e32c - 0x8013eb??  (subset) | batch_371 | 2026-09-05
//
// AI actor activation / deactivation, per-frame target reacquisition, behaviour-
// phase dispatch, zone-door detection and bot culling. Continuation of 364-370.
// Per-actor render object = *(actor + 0x7c). DAT_805015bd = game mode byte (0x0a
// coop). DAT_80588610 visible-node ptr table (count DAT_80612098). DAT_80611d90
// player state pool (0xea0). DAT_80612610 base count, DAT_806120fc extra-bot
// count. FUN_803826xx/__save_gpr_xx = fp/gpr scratch-frame helpers.
//
// Functions:
//   aiactor_get_scratch_slot         0x8013e32c  DAT_80588a9c[idx*2] accessor
//   aiactor_check_door_between_zones 0x8013e340  push zone hist via a door trigger
//   aiactor_scratch_list_push        0x8013e4a0  append to per-slot scratch list
//   aiactor_reacquire_targets_all    0x8013e4dc  per-frame bot target reacquire
//   aiactor_clear_leader_refs        0x8013e774  clear visible refs to an entity
//   aiactor_clear_targets_of_owner   0x8013e7cc  reacquire bots targeting owner
//   aiactor_cull_farthest_bots       0x8013e868  evict farthest idle bots
//   aiactor_run_behaviour_phase      0x8013e9cc  dispatch behaviour phase
//   aiactor_deactivate               0x8013eaa4  set inactive, drop from list
//   aiactor_activate                 0x8013eb10  clear inactive, add to list

#include "common.h"

// ---- named cross-module helpers (self-externed; common.h has no decls) -----
extern "C" {
extern int  aiactor_acquire_nearest_target(int actor, int preferred, int mode); // aiactor_combat.cpp
extern void aiactor_push_zone_history(int actor, int srcObj);                    // aiactor_death.cpp
extern void aiactor_visible_list_add(int actor);                                 // aiactor_relations.cpp
extern void aiactor_visible_list_remove(int actor);                              // aiactor_relations.cpp
extern int  trigger_is_moving(int trigger);
extern int  trigger_tick_update(void);
extern int  FUN_8027ea98(void);                                                  // squad leader entity
}

// ---- raw data symbols ------------------------------------------------------
extern char DAT_805015bd;    // game mode byte (0x0a coop)
extern int  DAT_80588a9c;    // per-actor scratch slot table (2 words / actor)
extern int  DAT_805885b8;    // scratch-list count array (0xb words / slot)
extern int  DAT_805885bc;    // scratch-list entry array (0x2c bytes / slot)
extern int  DAT_806120e4;    // current scratch-list slot index
extern int  DAT_80588610;    // visible-node ptr table
extern int  DAT_80612098;    // visible-node count
extern int  DAT_80612610;    // base player/actor count
extern int  DAT_806120fc;    // active extra-bot count
extern int  DAT_80611d78;    // actor table base (0x141c stride, +0x14 = player0)
extern int  DAT_80611d90;    // player state pool base (0xea0 stride)
extern int  DAT_80611b8c;    // zone table base (0x20 stride)
extern int  DAT_80611b90;    // door-trigger zone-pair table (0x14 stride)
extern int  DAT_80611c24;    // handle pool base (0x2e8 stride)
extern int  DAT_80611ca0;    // door-trigger table base (0x98 stride)
extern int  DAT_80611ca4;    // door-trigger count
extern int  DAT_804d34dc;    // weapon-class attribute table
extern short DAT_804d1cd0;   // weapon-class index table

extern float FLOAT_80615da0; // cull best-distance init
extern float FLOAT_80615da4; // cull no-target fallback distance

// ---- raw function symbols --------------------------------------------------
extern "C" {
extern int  FUN_803826e4(void);            // fp frame enter
extern void FUN_80382730(void);            // fp frame leave
extern int  __save_gpr_28(void);           // gpr frame enter
extern void __restore_gpr_28(void);
extern void __save_gpr_25(void);           // gpr frame enter (cull)
extern void __restore_gpr_25(void);
extern int  FUN_80278ff0(void);            // combat-hold predicate
extern int  FUN_80142b38(void);            // reserved-slot count
extern int  FUN_80280c84(void);            // bot-cull permitted?
extern void FUN_8028521c(int entity);      // deactivate/despawn bot
extern void FUN_80280f8c(void);            // pre-phase hook
extern int  FUN_802814a0(int a, int b, int* ctx);  // default phase handler
extern void FUN_80280e50(int a, int b);    // fallback phase handler
}

// FUN_803826e4 actually returns a packed 64-bit gpr frame; call via this cast.
typedef unsigned long long (*frame_enter_fn)(void);

// aiactor_get_scratch_slot 0x8013e32c
int* aiactor_get_scratch_slot(int idx)
{
    return &DAT_80588a9c + idx * 2;
}

// aiactor_clear_leader_refs 0x8013e774
// Clears any visible-list entry whose leader/target field (+4) points at the
// given entity (used when that entity is destroyed).
void aiactor_clear_leader_refs(int entity)
{
    int off = 0;
    for (int i = 0; i < DAT_80612098; i = i + 1) {
        int rd = *(int*)((int)&DAT_80588610 + off);
        if ((rd != 0) && (*(int*)(rd + 4) == entity)) {
            *(int*)(rd + 4) = 0;
            *(u32*)(rd + 0x2c) = *(u32*)(rd + 0x2c) & 0xffffff7f;
        }
        off = off + 4;
    }
}

// aiactor_clear_targets_of_owner 0x8013e7cc
// For every extra bot whose current target (+4) is the caller's frame owner,
// reacquire a fresh nearest target and clear the "target-locked" bit.
void aiactor_clear_targets_of_owner(void)
{
    int owner = __save_gpr_28();
    int poolOff = DAT_80612610 * 0xea0;
    int idx = DAT_80612610;
    while (idx < DAT_806120fc + DAT_80612610) {
        int* p = (int*)(DAT_80611d90 + poolOff);
        if (((p != (int*)0) && (*p != 0)) && (p[1] == owner)) {
            p[1] = aiactor_acquire_nearest_target(*p, *(int*)(DAT_80611d78 + 0x14), 0);
            p[0xb] = p[0xb] & 0xffffff7f;
        }
        idx = idx + 1;
        poolOff = poolOff + 0xea0;
    }
    __restore_gpr_28();
}

// aiactor_check_door_between_zones 0x8013e340
// If the actor is transitioning between two "door" zones, finds the matching
// door trigger and (unless it is already moving/ticking) records the zone hop.
void aiactor_check_door_between_zones(int actor)
{
    int  rd = *(int*)(actor + 0x7c);
    u32  curFlags = *(u32*)(DAT_80611b8c + 4 + *(int*)(rd + 0x678) * 0x20);
    if (((((curFlags & 0x1000000) != 0) &&
          ((*(u32*)(DAT_80611b8c + 4 + *(int*)(rd + 0x680) * 0x20) & 0x1000000) != 0)) &&
         ((curFlags & 0x800000) == 0)) && (*(int*)(rd + 0x678) != *(int*)(rd + 0x680))) {
        int off = 0;
        for (int i = 0; i < DAT_80611ca4; i = i + 1) {
            u32* pDoor = (u32*)(DAT_80611ca0 + off);
            u32  handleId = *pDoor;
            int  handle;
            if (handleId == 0) {
                handle = 0;
            }
            else {
                handle = DAT_80611c24 + (handleId >> 0x16) * 0x2e8;
                if (*(u32*)(handle + 0x2c) != handleId) { handle = 0; *pDoor = 0; }
            }
            if ((-1 < (int)pDoor[0x11]) && ((handle != 0))) {
                int pair = DAT_80611b90 + pDoor[0x11] * 0x14;
                if (((*(int*)(pair + 8) == *(int*)(rd + 0x680)) &&
                     (*(int*)(pair + 4) == *(int*)(rd + 0x678))) ||
                    ((*(int*)(pair + 4) == *(int*)(rd + 0x680)) &&
                     (*(int*)(pair + 8) == *(int*)(rd + 0x678)))) {
                    if (trigger_is_moving(handle) != 0) return;
                    if (trigger_tick_update() != 0) return;
                    aiactor_push_zone_history(actor, handle);
                    return;
                }
            }
            off = off + 0x98;
        }
    }
}

// aiactor_scratch_list_push 0x8013e4a0
// Appends an entry to the current scratch-list slot (DAT_806120e4), capped at 10.
void aiactor_scratch_list_push(int value)
{
    int slot = DAT_806120e4;
    int count = (&DAT_805885b8)[slot * 0xb];
    *(int*)(&DAT_805885bc + slot * 0x2c + count * 4) = value;
    if (9 < (&DAT_805885b8)[slot * 0xb]) return;
    (&DAT_805885b8)[slot * 0xb] = (&DAT_805885b8)[slot * 0xb] + 1;
}

// aiactor_reacquire_targets_all 0x8013e4dc
// Per-frame pass over the extra-bot pool: for bots targeting the caller's frame
// owner that aren't locked / immune, reacquire the nearest target and pick the
// appropriate combat stance (8 engage or 0x10 hold).
void aiactor_reacquire_targets_all(void)
{
    int owner = FUN_803826e4();
    int poolOff = DAT_80612610 * 0xea0;
    int idx = DAT_80612610;
    do {
        if (DAT_806120fc + DAT_80612610 <= idx) { FUN_80382730(); return; }
        int* p = (int*)(DAT_80611d90 + poolOff);
        if (((p != (int*)0) && (*p != 0)) && (p[1] == owner)) {
            u32 mask = (DAT_805015bd == '\n') ? (u32)p[5] : (u32)p[7];
            if (((mask & 1) == 0) &&
                ((*(u32*)(&DAT_804d34dc +
                          (short)(&DAT_804d1cd0)[*(int*)(*(int*)(*p + 4) + 0xcc)] * 8) & 0x20) == 0)) {
                int leader = FUN_8027ea98();
                p[1] = aiactor_acquire_nearest_target(*p, *(int*)(DAT_80611d78 + 0x14), 0);
                if ((leader == 0) || ((int)(intptr_t)p != leader)) {
                    if (FUN_80278ff0() == 0) {
                        u32 s = (u32)p[5];
                        if ((s & 0x4488000) == 0) {
                            u32 s2 = (DAT_805015bd == '\n') ? s : (u32)p[7];
                            if ((s2 & 0x100) == 0) {
                                if (p[0x2ab] == 1 && p[0x2aa] == 0) {
                                    u32 s3 = (DAT_805015bd == '\n') ? (u32)p[5] : (u32)p[7];
                                    if ((s3 & 1) == 0) goto next;
                                }
                                p[0x2ab] = 8;
                                p[0x2aa] = 0;
                            }
                        }
                    }
                    else {
                        if (p[0x2ab] == 1 && p[0x2aa] == 0) {
                            u32 s3 = (DAT_805015bd == '\n') ? (u32)p[5] : (u32)p[7];
                            if ((s3 & 1) == 0) goto next;
                        }
                        p[0x2ab] = 0x10;
                        p[0x2aa] = 0;
                    }
                }
            }
        }
next:
        idx = idx + 1;
        poolOff = poolOff + 0xea0;
    } while (true);
}

// aiactor_cull_farthest_bots 0x8013e868
// Repeatedly evicts the farthest idle/holding extra bot until the active count
// is back within budget (DAT_80612098 - reserved).
void aiactor_cull_farthest_bots(void)
{
    float fx, fy, fz;
    int   reserved;
    int*  farthest;
    int*  p;
    int   listOff;
    double bestSq;

    __save_gpr_25();
    for (int iter = 0; reserved = FUN_80142b38(), iter < DAT_80612098 - reserved; iter = iter + 1) {
        farthest = (int*)0;
        bestSq = (double)FLOAT_80615da0;
        listOff = DAT_80612610 << 2;
        for (int i = DAT_80612610; i < DAT_80612098; i = i + 1) {
            p = *(int**)((int)&DAT_80588610 + listOff);
            if ((p[9] & 8U) == 0) {
                int inA, inB;
                if (DAT_805015bd == '\n') { inA = p[4]; inB = p[5]; }
                else                      { inA = p[6]; inB = p[7]; }
                if ((p[0x2ab] == inB && p[0x2aa] == inA) && (FUN_80280c84() != 0)) {
                    int target = p[1];
                    float distSq = FLOAT_80615da4;
                    if (target != 0) {
                        int ent = *p;
                        fx = *(float*)(ent + 100)  - *(float*)(target + 100);
                        fy = *(float*)(ent + 0x68) - *(float*)(target + 0x68);
                        fz = *(float*)(ent + 0x6c) - *(float*)(target + 0x6c);
                        distSq = fz * fz + fx * fx + fy * fy;
                    }
                    if (bestSq < (double)distSq) {
                        farthest = p;
                        bestSq = (double)distSq;
                    }
                }
            }
            listOff = listOff + 4;
        }
        if (farthest == (int*)0) break;
        FUN_8028521c(*farthest);
    }
    __restore_gpr_25();
}

// aiactor_run_behaviour_phase 0x8013e9cc
// Dispatches a behaviour phase: run the actor's optional custom callback first,
// then the default handler, then the fallback, honouring "handled / retry" codes.
void aiactor_run_behaviour_phase(u32 param_1, u32 param_2, int* ctx, u32 param_4)
{
    int actor;
    int lowArg;
    int rd;
    int result;
    void (*cb)(int, int, int*, u32);
    unsigned long long fr;
    (void)param_1; (void)param_2;

    // FUN_803826e4 returns the gpr scratch frame packed as a 64-bit value
    // (actor in the high word, the phase argument in the low word).
    fr    = ((frame_enter_fn)(void*)&FUN_803826e4)();
    actor  = (int)(fr >> 0x20);
    lowArg = (int)fr;
    rd     = *(int*)(actor + 0x7c);
    result = 0;
    FUN_80280f8c();
    cb = *(void(**)(int, int, int*, u32))(rd + 0x74);
    if (cb != (void(*)(int, int, int*, u32))0) {
        result = ((int(*)(int, int, int*, u32))cb)(actor, lowArg, ctx, param_4);
    }
    if ((result == 0) || (result == -2)) {
        result = FUN_802814a0(actor, lowArg, ctx);
    }
    if ((result == 0) || (result == -2)) {
        FUN_80280e50(actor, lowArg);
    }
    FUN_80382730();
}

// aiactor_deactivate 0x8013eaa4
// Marks the actor inactive (state bit 4), clears its movement flags and removes
// it from the visible list if it is an extra bot. Returns 1 if it was removed.
u32 aiactor_deactivate(int actor)
{
    int* rd = *(int**)(actor + 0x7c);
    if ((rd[0xb] & 4U) == 0) {
        rd[0xb] = rd[0xb] | 4;
        *(u32*)(*rd + 0x28) = *(u32*)(*rd + 0x28) & 0xffffff9e;
        if (DAT_80612610 <= rd[2]) {
            aiactor_visible_list_remove(*rd);
            return 1;
        }
    }
    return 0;
}

// aiactor_activate 0x8013eb10
// Clears the inactive bit, restores movement flags and re-adds the actor to the
// visible list (extra bots only, unless suppressed). Returns 1 if it was added.
u32 aiactor_activate(int actor)
{
    int* rd = *(int**)(actor + 0x7c);
    if ((rd[0xb] & 4U) != 0) {
        rd[0xb] = rd[0xb] & 0xfffffffb;
        *(u32*)(*rd + 0x28) = *(u32*)(*rd + 0x28) | 0x61;
        if (((rd[10] & 0x80U) != 0) || ((rd[5] & 0x4000000U) != 0)) {
            return 0;
        }
        if (DAT_80612610 <= rd[2]) {
            aiactor_visible_list_add(*rd);
            return 1;
        }
    }
    return 0;
}
