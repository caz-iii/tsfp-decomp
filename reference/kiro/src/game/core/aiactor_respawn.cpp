// Address range: 0x80140a2c - 0x8014143f | batch_376 | 2026-09-05
//
// AI actor respawn driver + spawn-point eligibility evaluators. Continuation of
// batches 364-375. Two structures dominate:
//
//   * Spawn record table: base = *(DAT_8061208c + 0x74), count = *(+0x78),
//     stride 0x98. Per record: +0x0c flags, +0x10 mode-mask, +0x16 char id,
//     +0x2c unlock flags, +0x38/+0x3c/+0x40/+0x44 zone ids, +0x50 linked
//     entity/trigger, +0x54 spawn flags, +0x5c/+0x60 per-zone caps,
//     +0x64 spawn id, +0x68 alt-height, +0x6c optional pre-spawn callback,
//     +0x96 team byte.
//   * Spawn-point visibility cache: DAT_80612108, 0x18 bytes/entry, parallel to
//     the spawn records: +0x00/+0x04 spawn counters, +0x0c cached id,
//     +0x0c(int[3]) respawn timer, +0x10 "occupied/eligible" flag, +0x10..
//     +0x10 timer state at [4].
//
// The evaluators (called per spawn point) set the cache entry's +0x10 flag to
// mark a spawn point eligible/occupied based on zone occupancy, trigger/switch
// state, or level load status. The driver walks all spawn points, and for each
// eligible one whose respawn timer has elapsed, respawns a bot there.
//
// DAT_805015bd = game mode byte (0x0a coop). DAT_80612610 = base player count.
// DAT_80612094 = live-bot count. DAT_80612098 = active actor count.
// DAT_80611d90 = player/actor state pool (0xea0 stride). DAT_80611b8c =
// zone-node table (0x20 stride). DAT_805015c0 = global game flags.

#include "common.h"

// ---- named cross-module helpers (self-externed; common.h has no decls) -----
extern "C" {
extern u32  spawn_check_flags_with_unlock(u32 flags, u32 unlockFlags);
extern int  spawn_try_find_respawn(u32 modeMask);
extern int* soundpad_resolve_entity_to_zone(int entityId);
extern int  soundpad_zone_node_to_index(int* node);
extern int  trigger_slot_resolve_entity(int entityId);
extern int  level_get_current_id(void);
extern int  level_actor_get_load_status(int levelId);

// respawn helpers (wide float ABI; pass-through scratch registers). Return
// value in EAX. Declared at call-site arity to match the original.
extern int  aiactor_respawn_bot(long long r, double a, double b, double c,
                                long long e, long long f, long long g, long long h,
                                int spawnRec, int zoneEntity, int weaponA, int weaponB,
                                int flags, int team);
extern void aiactor_cull_farthest_bots(void);
extern long long level_get_default_weapon(u32 charId, int* outWeaponA, int* outWeaponB);

// undecompiled predicates / accessors (self-externed at call-site arity).
extern int          __save_gpr_25(void);
extern void         __restore_gpr_25(void);
extern int          FUN_8025bc88(void);                 // respawn-enabled gate
extern unsigned int FUN_8027eba0(int spawnRec);         // coop spawn exclusion
extern int          FUN_80142b58(long long r, double a, double b, double c,
                                 long long e, long long f, long long g, long long h,
                                 int spawnRec);          // spawn-point distance/LOS test
extern int          FUN_80142b38(void);                 // current active-actor count
extern int          FUN_8025bd28(int zoneIndex);        // zone-occupied test
extern int          FUN_8022d4ac(int spawnRec);         // objective spawn gate
extern void         FUN_8025bc00(int stateId);          // register respawned actor
extern int          FUN_80232740(int entityId);         // trigger open predicate (bool)
extern int          FUN_80232760(int zoneEntityId);     // trigger present predicate (bool)
extern u32          FUN_80220f30(int linkedEntityId);   // linked-entity active flag
extern int          FUN_8025c8e8(int entityId);         // switch slot index lookup
}

// ---- raw data symbols ------------------------------------------------------
extern char  DAT_805015bd;    // game mode byte (0x0a coop)
extern u32   DAT_805015c0;    // global game flags
extern int   DAT_8061208c;    // spawn config object ptr (+0x74 records, +0x78 count)
extern int   DAT_80612108;    // spawn-point visibility cache base (0x18 stride)
extern int   DAT_80612610;    // base player/actor count
extern int   DAT_80612614;    // player-count-100 special sentinel
extern int   DAT_80612094;    // live bot count
extern int   DAT_80612098;    // active actor count
extern int   DAT_80611d90;    // player/actor state pool (0xea0 stride)
extern int   DAT_80611d78;    // coop leader ref object
extern int   DAT_80611b8c;    // zone-node table base (0x20 stride)
extern int   DAT_806109c0;    // rng raw word (respawn jitter)
extern char  DAT_805cb94c;    // switch state table (byte state, 8 stride)

extern float FLOAT_80615e20;  // respawn timer max / reset value
extern float FLOAT_80615e24;  // respawn timer trigger threshold
extern float FLOAT_806108ec;  // respawn timer decrement rate
extern double DOUBLE_80615e28; // rng->double bias

// ---------------------------------------------------------------------------
// aiactor_respawn_at_spawnpoints (0x80140a2c)
// Master respawn driver. If respawning is enabled (FUN_8025bc88), walk every
// spawn record whose cache counters are below its per-zone caps, pass the
// flag/unlock/coop-exclusion gates, run any pre-spawn callback (+0x6c), tick the
// respawn timer, and once elapsed pick a respawn zone and spawn a bot there
// (aiactor_respawn_bot). Culls the farthest idle bot if over the active budget.
// Wide float ABI: the double/long-long params are pass-through scratch regs.
void aiactor_respawn_at_spawnpoints(long long r0, double a, double b, double c,
                                    long long e, long long f, long long g, long long h)
{
    int gate;
    u32 flagOk;
    int botSlot;
    int* cache;
    int zoneEntity;
    int zoneIdx;
    int recBase;
    int* zoneNode;
    int recOff;
    int cacheOff;
    double scratch;
    double timerReset;
    int weaponA;
    int weaponB;
    int spawnRec;
    int i;
    u32 rngHi;

    scratch = (double)__save_gpr_25();
    gate = FUN_8025bc88();
    if (gate != 0) {
        cacheOff = 0;
        recOff = 0;
        for (i = 0; i < *(int*)(DAT_8061208c + 0x78); i = i + 1) {
            spawnRec = *(int*)(DAT_8061208c + 0x74) + recOff;
            cache = (int*)(DAT_80612108 + cacheOff);
            if ((cache[0] < *(int*)(spawnRec + 0x60)) && (cache[1] < *(int*)(spawnRec + 0x5c))) {
                flagOk = spawn_check_flags_with_unlock(*(u32*)(spawnRec + 0xc), *(u32*)(spawnRec + 0x2c));
                if (flagOk != 0) {
                    if ((((*(u32*)(spawnRec + 0x54) & 0x1000) == 0) || ((DAT_805015c0 & 0x40000000) == 0)) &&
                        ((DAT_805015bd != '\n') ||
                         ((DAT_80612610 < 2) || (FUN_8027eba0(spawnRec) == 0)))) {
                        if (*(void**)(spawnRec + 0x6c) != (void*)0) {
                            scratch = (double)((int(*)(int))*(void**)(spawnRec + 0x6c))(i);
                        }
                        if (((cache[4] != 0) && (cache[4] != 2)) &&
                            (FUN_80142b58((long long)scratch, a, b, c, e, f, g, h, spawnRec) != 0)) {
                            if ((DAT_80612098 < 0x14) || ((*(u32*)(spawnRec + 0xc) & 0x20000) != 0)) {
                                // Tick respawn timer using rng jitter.
                                rngHi = (u32)(DAT_806109c0 ^ 0x80000000);
                                double rngD = (double)FLOAT_80615e20 /
                                              (double)(float)((double)(*(double*)&rngHi) - DOUBLE_80615e28);
                                cache[3] = (int)-(float)((double)FLOAT_806108ec * rngD - (double)(float)cache[3]);
                                if (((double)(float)cache[3] <= (double)FLOAT_80615e24) &&
                                    (zoneEntity = spawn_try_find_respawn(*(u32*)(spawnRec + 0x10)),
                                     zoneEntity != 0)) {
                                    zoneNode = (int*)soundpad_resolve_entity_to_zone(zoneEntity);
                                    if (zoneNode != (int*)0) {
                                        if ((*(u32*)(spawnRec + 0x54) & 0x10) != 0) {
                                            zoneIdx = soundpad_zone_node_to_index(zoneNode);
                                            if (FUN_8025bd28(zoneIdx) != 0) {
                                                cache[3] = (int)FLOAT_80615e20;
                                                cache[4] = 2;
                                                goto next;
                                            }
                                        }
                                        if ((((DAT_805015c0 & 0x40) == 0) || (FUN_8022d4ac(spawnRec) != 0)) &&
                                            ((DAT_80612614 != 100) || (DAT_80612098 < 0x15))) {
                                            long long wpn = level_get_default_weapon(
                                                *(unsigned short*)(spawnRec + 0x16), &weaponA, &weaponB);
                                            botSlot = aiactor_respawn_bot(
                                                wpn, a, b, c, e, f, g, h,
                                                spawnRec, zoneEntity, weaponA, weaponB,
                                                -1, (int)*(char*)(spawnRec + 0x96));
                                            if (FUN_80142b38() < DAT_80612098) {
                                                aiactor_cull_farthest_bots();
                                            }
                                            if (botSlot != -1) {
                                                DAT_80612094 = DAT_80612094 + 1;
                                                int* botState = (int*)(DAT_80611d90 + botSlot * 0xea0);
                                                cache[0] = cache[0] + 1;
                                                cache[1] = cache[1] + 1;
                                                botState[0x2fd] = (int)cache;
                                                botState[0x2fc] = spawnRec;
                                                FUN_8025bc00(*botState);
                                                cache[3] = *(int*)(spawnRec + 100);
                                                if (((*(u32*)(spawnRec + 4) & 0x82000) != 0) &&
                                                    (botState[0x27b] = *(int*)(spawnRec + 0x68),
                                                     (*(u32*)(spawnRec + 4) & 0x2000) != 0)) {
                                                    botState[0x21] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x84);
                                                }
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        next:
            cacheOff = cacheOff + 0x18;
            recOff = recOff + 0x98;
        }
    }
    __restore_gpr_25();
    (void)r0;
}

// ---------------------------------------------------------------------------
// aiactor_spawnpoint_eval_zone (0x80140d4c)
// Eligibility evaluator: mark the spawn point eligible (+0x10 = 1) if any active
// actor currently occupies one of the record's four zone ids (+0x38..+0x44).
void aiactor_spawnpoint_eval_zone(int spawnIndex)
{
    int actorZone;
    int rec;
    int cache;
    int poolOff;
    int i;

    poolOff = 0;
    rec = *(int*)(DAT_8061208c + 0x74);
    cache = DAT_80612108 + spawnIndex * 0x18;
    *(int*)(cache + 0x10) = 0;
    rec = rec + spawnIndex * 0x98;
    for (i = 0; i < DAT_80612610; i = i + 1) {
        actorZone = *(int*)(DAT_80611b8c + *(int*)(DAT_80611d90 + poolOff + 0x678) * 0x20);
        if ((actorZone != -1) &&
            ((actorZone == *(int*)(rec + 0x38)) || (actorZone == *(int*)(rec + 0x3c)) ||
             (actorZone == *(int*)(rec + 0x40)) || (actorZone == *(int*)(rec + 0x44)))) {
            *(int*)(cache + 0x10) = 1;
        }
        poolOff = poolOff + 0xea0;
    }
}

// ---------------------------------------------------------------------------
// aiactor_spawnpoint_eval_trigger_open (0x80140de8)
// Eligible if the record's zone entity (+0x3c) trigger is open.
void aiactor_spawnpoint_eval_trigger_open(int spawnIndex)
{
    int rec;
    int cache;

    rec = *(int*)(DAT_8061208c + 0x74);
    cache = DAT_80612108 + spawnIndex * 0x18;
    *(int*)(cache + 0x10) = 0;
    if (FUN_80232740(*(int*)(rec + spawnIndex * 0x98 + 0x3c)) != 0) {
        *(int*)(cache + 0x10) = 1;
    }
}

// ---------------------------------------------------------------------------
// aiactor_spawnpoint_eval_trigger_free (0x80140e48)
// Eligible if the record's zone entity (+0x38) trigger exists AND its linked
// entity (+0x50) is not currently active.
void aiactor_spawnpoint_eval_trigger_free(int spawnIndex)
{
    u32 linkedActive;
    int rec;
    int cache;

    rec = *(int*)(DAT_8061208c + 0x74);
    cache = DAT_80612108 + spawnIndex * 0x18;
    *(int*)(cache + 0x10) = 0;
    rec = rec + spawnIndex * 0x98;
    if (FUN_80232760(*(int*)(rec + 0x38)) != 0) {
        linkedActive = FUN_80220f30(*(int*)(rec + 0x50));
        if (linkedActive == 0) {
            *(int*)(cache + 0x10) = 1;
        } else {
            *(int*)(cache + 0x10) = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// aiactor_spawnpoint_eval_zone_gated (0x80140ecc)
// Zone evaluator gated on live-bot count < 7.
void aiactor_spawnpoint_eval_zone_gated(int spawnIndex)
{
    if (DAT_80612094 < 7) {
        aiactor_spawnpoint_eval_zone(spawnIndex);
    }
}

// ---------------------------------------------------------------------------
// aiactor_spawnpoint_eval_zone2 (0x80140ef8)
// Same zone-occupancy test as eval_zone (distinct spawn-point set / call site).
void aiactor_spawnpoint_eval_zone2(int spawnIndex)
{
    int actorZone;
    int rec;
    int cache;
    int poolOff;
    int i;

    poolOff = 0;
    rec = *(int*)(DAT_8061208c + 0x74) + spawnIndex * 0x98;
    cache = DAT_80612108 + spawnIndex * 0x18;
    for (i = 0; i < DAT_80612610; i = i + 1) {
        actorZone = *(int*)(DAT_80611b8c + *(int*)(DAT_80611d90 + poolOff + 0x678) * 0x20);
        if ((actorZone != -1) &&
            ((actorZone == *(int*)(rec + 0x38)) || (actorZone == *(int*)(rec + 0x3c)) ||
             (actorZone == *(int*)(rec + 0x40)) || (actorZone == *(int*)(rec + 0x44)))) {
            *(int*)(cache + 0x10) = 1;
        }
        poolOff = poolOff + 0xea0;
    }
}

// ---------------------------------------------------------------------------
// aiactor_spawnpoint_eval_zone_loaded (0x80140f90)
// Zone-occupancy test, but only while the current level's actor set is fully
// loaded (level_actor_get_load_status == 0); otherwise mark ineligible.
void aiactor_spawnpoint_eval_zone_loaded(int spawnIndex)
{
    int actorZone;
    int cache;
    int rec;
    int loadStatus;
    int poolOff;
    int i;

    cache = DAT_80612108 + spawnIndex * 0x18;
    rec = *(int*)(DAT_8061208c + 0x74) + spawnIndex * 0x98;
    loadStatus = level_actor_get_load_status(level_get_current_id());
    if (loadStatus == 0) {
        poolOff = 0;
        for (i = 0; i < DAT_80612610; i = i + 1) {
            actorZone = *(int*)(DAT_80611b8c + *(int*)(DAT_80611d90 + poolOff + 0x678) * 0x20);
            if ((actorZone != -1) &&
                ((actorZone == *(int*)(rec + 0x38)) || (actorZone == *(int*)(rec + 0x3c)) ||
                 (actorZone == *(int*)(rec + 0x40)) || (actorZone == *(int*)(rec + 0x44)))) {
                *(int*)(cache + 0x10) = 1;
            }
            poolOff = poolOff + 0xea0;
        }
    } else {
        *(int*)(cache + 0x10) = 0;
    }
}

// ---------------------------------------------------------------------------
// aiactor_spawnpoint_eval_zone2_gated (0x8014106c)
// eval_zone2 gated on live-bot count < 5.
void aiactor_spawnpoint_eval_zone2_gated(int spawnIndex)
{
    if (DAT_80612094 < 5) {
        aiactor_spawnpoint_eval_zone2(spawnIndex);
    }
}

// ---------------------------------------------------------------------------
// aiactor_spawnpoint_eval_entity_state (0x801412c4)
// Eligibility driven by the linked trigger entity's render state (+0x10 == 1).
// If already eligible, clear it should the entity become invalid/inactive; else
// scan (per active actor) and set eligible when the entity is active.
void aiactor_spawnpoint_eval_entity_state(int spawnIndex)
{
    int cache;
    int entityObj;
    int i;

    cache = DAT_80612108 + spawnIndex * 0x18;
    entityObj = trigger_slot_resolve_entity(
        *(int*)(*(int*)(DAT_8061208c + 0x74) + spawnIndex * 0x98 + 0x38));
    entityObj = *(int*)(entityObj + 0x7c);
    if (*(int*)(cache + 0x10) == 1) {
        if (entityObj == 0) {
            *(int*)(cache + 0x10) = 0;
        } else if (*(int*)(entityObj + 0x10) != 1) {
            *(int*)(cache + 0x10) = 0;
        }
    } else {
        for (i = 0; i < DAT_80612610; i = i + 1) {
            if (entityObj == 0) {
                *(int*)(cache + 0x10) = 0;
            } else {
                if (*(int*)(entityObj + 0x10) == 1) {
                    *(int*)(cache + 0x10) = 1;
                    return;
                }
                *(int*)(cache + 0x10) = 0;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// aiactor_spawnpoint_eval_switch_state (0x80141390)
// Eligibility driven by the linked switch state (DAT_805cb94c table, 8 stride):
// eligible iff the switch's state == 1.
void aiactor_spawnpoint_eval_switch_state(int spawnIndex)
{
    int cache;
    int switchIdx;

    cache = DAT_80612108 + spawnIndex * 0x18;
    switchIdx = FUN_8025c8e8(*(int*)(*(int*)(DAT_8061208c + 0x74) + spawnIndex * 0x98 + 0x38));
    if (*(int*)(cache + 0x10) == 1) {
        if ((&DAT_805cb94c)[switchIdx * 8] != 1) {
            *(int*)(cache + 0x10) = 0;
        }
    } else if ((&DAT_805cb94c)[switchIdx * 8] == 1) {
        *(int*)(cache + 0x10) = 1;
    } else {
        *(int*)(cache + 0x10) = 0;
    }
}

// ===========================================================================
// Address range: 0x801414b8 - 0x801423f4 | batch_377 | 2026-09-05
//
// More spawn-point eligibility evaluators (same +0x10 visibility-cache flag
// pattern as batch_376) plus a shared "nearest visible enemy" target scan used
// by the AI retarget helpers. The evaluators are called once per spawn point;
// each writes the cache entry's +0x10 flag (1 = eligible/spotted, 0 = not).
//
//   * The zone-eval family compares a spawn record's four zone ids
//     (+0x38/+0x3c/+0x40/+0x44) against the zone the local/reference player is
//     currently in (resolved via the zone-node table DAT_80611b8c indexed by
//     player state +0x678). If the player is standing in one of the spawn
//     point's zones, the spawn point is "seen" and marked ineligible/occupied.
//   * The trigger/switch evals gate on a switch table byte (DAT_805cb94c) and a
//     door trigger's moving state.
//   * aiactor_scan_nearest_visible_enemy walks a caller-supplied actor pointer
//     list and finds, per the pairwise relation table, the nearest visible
//     hostile (relation flag set) or tracks the nearest not-yet-visible one.
//     The +0x678 pool field is the zone-node index; DAT_80588610 is the packed
//     visible-actor list (see aiactor_relations.cpp).
// ===========================================================================

extern "C" {
// zone / soundpad helpers (definitions in sound_pad_zones.cpp / *_zone_query.cpp)
extern int* soundpad_resolve_zone_node(int nodeId);
extern int  soundpad_get_entity_zone_id(int node);
extern int  soundpad_query_adjacent_zones(int zoneId, void* outA, int outB,
                                          int a, int b, int c);
extern int  soundpad_get_entity_id_from_zone(int zone);

// aiactor helpers (aiactor_death.cpp / aiactor_relations.cpp)
extern int  aiactor_find_player_by_team(int team);   // 0x8013d618
extern int  aiactor_relation_entry(int a, int b);    // 0x8013dc70 -> table ptr

// trigger predicates (trigger_ops.cpp)
extern int  trigger_is_moving(int pEntity);

// rng (random.cpp is void; used-with-return here -> file-local int decl OK)
extern int  rng_counter_tick(void);

// undecompiled scratch-frame / predicate helpers (self-externed at call arity)
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern int  FUN_803826e8(void);   // scratch: read spawn-point index (arg0)
extern int  FUN_803826f0(void);   // scratch: read spawn-point index (arg0)
extern long long FUN_803826d8(void); // scratch: packed (count<<32 | listPtr)
extern void FUN_80382734(void);   // scratch frame restore
extern void FUN_8038273c(void);   // scratch frame restore
extern void FUN_80382724(void);   // scratch frame restore
extern int  FUN_8027ea98(void);   // resolve secondary/reference player actor
}

// raw data symbols new to this cluster
extern int  DAT_80588610;      // packed visible-actor list base (aiactor_relations)
extern int* DAT_80611d90p;     // player/actor state pool as int* (alias below)
extern float  FLOAT_80615e30;  // rng scatter: base offset
extern float  FLOAT_80615e34;  // rng scatter: 15-bit divisor
extern float  FLOAT_80615e38;  // (unused here, kept for range parity)
extern double DOUBLE_80615e38; // rng->double bias (scatter)
extern float  FLOAT_80615e40;  // nearest-enemy scan: initial best distance
extern float  FLOAT_80615e44;  // nearest-enemy scan: same-team distance weight

// DAT_80611d90 is declared 'int' at file scope; index it as an int* array here.
#define AIRSPAWN_POOL ((int*)(long)DAT_80611d90)

// ---------------------------------------------------------------------------
// aiactor_spawnpoint_eval_door_open (0x801414b8)
// Mark the spawn point eligible if its second zone-door trigger (record +0x38)
// is open (FUN_80220f30 linked-entity active).
void aiactor_spawnpoint_eval_door_open(int spawnIndex)
{
    int recBase;
    int* cache;
    u32 open;

    recBase = *(int*)(DAT_8061208c + 0x74);
    cache = (int*)(DAT_80612108 + spawnIndex * 0x18);
    cache[4] = 0; // +0x10
    open = FUN_80220f30(*(int*)(recBase + spawnIndex * 0x98 + 0x38));
    if (open != 0) {
        cache[4] = 1;
    }
}

// ---------------------------------------------------------------------------
// aiactor_spawnpoint_eval_trigger_pair (0x80141518)
// Eligible if the first zone-door (+0x38) is open AND the second (+0x3c) is not
// (i.e. exactly one of the pair is open).
void aiactor_spawnpoint_eval_trigger_pair(int spawnIndex)
{
    int rec;
    int* cache;
    u32 firstOpen;
    u32 secondOpen;

    rec = *(int*)(DAT_8061208c + 0x74);
    cache = (int*)(DAT_80612108 + spawnIndex * 0x18);
    cache[4] = 0; // +0x10
    rec = rec + spawnIndex * 0x98;
    firstOpen = FUN_80220f30(*(int*)(rec + 0x38));
    if (firstOpen != 0) {
        secondOpen = FUN_80220f30(*(int*)(rec + 0x3c));
        if (secondOpen == 0) {
            cache[4] = 1;
        }
    }
}

// ---------------------------------------------------------------------------
// aiactor_spawnpoint_eval_zone_all_players (0x80141590)
// When the level's actor set is loaded, mark eligible only if no player stands
// in any of the spawn point's four zones (scans all players, 0xea0 stride).
// Spawn index comes from the scratch frame (FUN_803826e8).
void aiactor_spawnpoint_eval_zone_all_players(void)
{
    int spawnIndex;
    u32 excluded;
    int playerZone;
    int rec;
    int* cache;
    int poolOff;
    int loadStatus;
    int i;

    spawnIndex = FUN_803826e8();
    rec = *(int*)(DAT_8061208c + 0x74) + spawnIndex * 0x98;
    cache = (int*)(DAT_80612108 + spawnIndex * 0x18);
    loadStatus = level_actor_get_load_status(level_get_current_id());
    if (loadStatus == 0) {
        poolOff = 0;
        for (i = 0; i < DAT_80612610; i = i + 1) {
            playerZone = *(int*)(DAT_80611b8c +
                                 *(int*)(DAT_80611d90 + poolOff + 0x678) * 0x20);
            excluded = FUN_80220f30(8);
            if ((excluded == 0) &&
                ((playerZone == -1) ||
                 (((playerZone != *(int*)(rec + 0x38)) &&
                   (playerZone != *(int*)(rec + 0x3c)) &&
                   (playerZone != *(int*)(rec + 0x40)) &&
                   (playerZone != *(int*)(rec + 0x44)))))) {
                cache[4] = 0;
            } else {
                cache[4] = 1;
            }
            poolOff = poolOff + 0xea0;
        }
    } else {
        cache[4] = 0;
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// aiactor_spawnpoint_eval_flag_loaded (0x80141688)
// When the level's actor set is loaded, mark eligible if a game flag (0x11) is
// set for any player. Spawn index comes from the scratch frame (__save_gpr_28).
void aiactor_spawnpoint_eval_flag_loaded(void)
{
    int spawnIndex;
    u32 flagSet;
    int* cache;
    int loadStatus;
    int i;

    spawnIndex = __save_gpr_28();
    cache = (int*)(DAT_80612108 + spawnIndex * 0x18);
    loadStatus = level_actor_get_load_status(level_get_current_id());
    if (loadStatus == 0) {
        for (i = 0; i < DAT_80612610; i = i + 1) {
            flagSet = FUN_80220f30(0x11);
            if (flagSet == 0) {
                cache[4] = 0;
            } else {
                cache[4] = 1;
            }
        }
    } else {
        cache[4] = 0;
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// aiactor_spawnpoint_eval_switch_door (0x80141718)
// Eligible if switch slot 0x65's state byte (DAT_805cb94c table, 8 stride) == 1
// and the linked door entity 0x30303031 ("0001") is currently moving.
void aiactor_spawnpoint_eval_switch_door(int spawnIndex)
{
    int* cache;
    int switchIdx;
    int doorEntity;
    int moving;

    cache = (int*)(DAT_80612108 + spawnIndex * 0x18);
    switchIdx = FUN_8025c8e8(0x65);
    if ((&DAT_805cb94c)[switchIdx * 8] == 1) {
        doorEntity = trigger_slot_resolve_entity(0x30303031);
        moving = trigger_is_moving(doorEntity);
        if (moving != 0) {
            cache[4] = 1;
        }
    }
}

// ---------------------------------------------------------------------------
// aiactor_spawnpoint_eval_zone_coop (0x80141ef0)
// Coop-aware zone occupancy: mark eligible if either the primary player
// (state pool +0) OR the reference player (single-player: FUN_8027ea98; coop:
// state pool +0x1518) stands in one of the spawn point's four zones. Spawn
// index from scratch frame (__save_gpr_28).
void aiactor_spawnpoint_eval_zone_coop(void)
{
    int spawnIndex;
    int refPlayer;
    int refZone;
    int primaryZone;
    int rec;
    int* cache;

    spawnIndex = __save_gpr_28();
    refZone = -1;
    primaryZone = *(int*)(DAT_80611b8c + *(int*)(DAT_80611d90 + 0x678) * 0x20);
    rec = *(int*)(DAT_8061208c + 0x74) + spawnIndex * 0x98;
    cache = (int*)(DAT_80612108 + spawnIndex * 0x18);
    if (DAT_80612610 == 1) {
        refPlayer = FUN_8027ea98();
        if (refPlayer != 0) {
            refZone = *(int*)(DAT_80611b8c + *(int*)(refPlayer + 0x678) * 0x20);
        }
    } else {
        refZone = *(int*)(DAT_80611b8c + *(int*)(DAT_80611d90 + 0x1518) * 0x20);
    }
    if (((primaryZone != -1) &&
         (((primaryZone == *(int*)(rec + 0x38)) ||
           (primaryZone == *(int*)(rec + 0x3c)) ||
           (primaryZone == *(int*)(rec + 0x40)) ||
           (primaryZone == *(int*)(rec + 0x44))))) ||
        ((refZone != -1) &&
         (((refZone == *(int*)(rec + 0x38)) ||
           (refZone == *(int*)(rec + 0x3c)) ||
           (refZone == *(int*)(rec + 0x40)) ||
           (refZone == *(int*)(rec + 0x44)))))) {
        cache[4] = 1;
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// aiactor_spawnpoint_eval_zone_refplayer (0x80142000)
// Simpler variant: mark eligible if the reference player (FUN_8027ea98) stands
// in one of the spawn point's four zones.
void aiactor_spawnpoint_eval_zone_refplayer(int spawnIndex)
{
    int* cache;
    int rec;
    int refPlayer;
    int refZone;

    cache = (int*)(DAT_80612108 + spawnIndex * 0x18);
    rec = *(int*)(DAT_8061208c + 0x74) + spawnIndex * 0x98;
    refPlayer = FUN_8027ea98();
    refZone = -1;
    if (refPlayer != 0) {
        refZone = *(int*)(DAT_80611b8c + *(int*)(refPlayer + 0x678) * 0x20);
    }
    if ((refZone != -1) &&
        (((refZone == *(int*)(rec + 0x38)) ||
          (refZone == *(int*)(rec + 0x3c)) ||
          (refZone == *(int*)(rec + 0x40)) ||
          (refZone == *(int*)(rec + 0x44))))) {
        cache[4] = 1;
    }
}

// ---------------------------------------------------------------------------
// aiactor_spawnpoint_pick_teammate_zone (0x801420a8)
// If a live teammate exists (record +0x3c team) and the local player is not in
// zone 0x26, and the teammate is far enough away (squared XZ distance beyond the
// record's +0x38 threshold), pick a random adjacent zone around the teammate
// (or the local player when +0x40 == 0) as the respawn target zone. Stores the
// resolved zone entity id at record +0x10 and flags the cache entry eligible.
void aiactor_spawnpoint_pick_teammate_zone(void)
{
    float dx;
    float dz;
    int spawnIndex;
    int* originNode;
    int rec;
    int* cache;
    int teammate;
    int localZoneId;
    int adjCount;
    int pick;
    u32 rngRaw;
    int zoneEntity;
    int adjZones[20];

    spawnIndex = FUN_803826f0();
    rec = *(int*)(DAT_8061208c + 0x74) + spawnIndex * 0x98;
    cache = (int*)(DAT_80612108 + spawnIndex * 0x18);
    teammate = aiactor_find_player_by_team(*(int*)(rec + 0x3c));
    cache[4] = 0;
    if (teammate != 0) {
        originNode = soundpad_resolve_zone_node(AIRSPAWN_POOL[0x19e]);
        localZoneId = soundpad_get_entity_zone_id((int)originNode);
        if (localZoneId != 0x26) {
            dx = *(float*)(teammate + 100) - *(float*)(AIRSPAWN_POOL[0] + 100);
            dz = *(float*)(teammate + 0x6c) - *(float*)(AIRSPAWN_POOL[0] + 0x6c);
            // threshold: (record +0x38)^2 as float, biased
            {
                int thr = *(int*)(rec + 0x38);
                double thrSq = (double)((long long)thr * (long long)thr) - DOUBLE_80615e28;
                if ((float)thrSq < dx * dx + dz * dz) {
                    int* originPlayer = AIRSPAWN_POOL;
                    if (*(int*)(rec + 0x40) == 0) {
                        originPlayer = *(int**)(teammate + 0x7c);
                    }
                    adjCount = soundpad_query_adjacent_zones(originPlayer[0x19e],
                                                             adjZones, 0, 0x14, 0, 0);
                    rngRaw = (u32)rng_counter_tick();
                    // pick = (adjCount-1) * (rng15 / divisor) + baseOffset
                    {
                        double rngD = ((double)rngRaw - DOUBLE_80615e38) / (double)FLOAT_80615e34;
                        double span = (double)(adjCount - 1);
                        pick = (int)((float)(span * rngD) + FLOAT_80615e30);
                    }
                    zoneEntity = soundpad_get_entity_id_from_zone(adjZones[pick]);
                    *(int*)(rec + 0x10) = zoneEntity;
                    cache[4] = 1;
                }
            }
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// aiactor_scan_nearest_visible_enemy (0x80142218)
// Walk the caller-supplied actor pointer list (count/self packed by the scratch
// frame FUN_803826d8). For each candidate that is alive (flags +0x24 bit 3 clear)
// and not disabled (+0xc6c bit 2 clear), consult the pairwise relation table:
//   * if the pair's visible flag is set, track the nearest such hostile;
//   * otherwise track the nearest not-yet-visible candidate as a fallback.
// Same-team / distant candidates are distance-weighted (FLOAT_80615e44). The
// result index is consumed by the caller through the scratch frame.
void aiactor_scan_nearest_visible_enemy(u32 selfArg0, u32 selfArg1, int listBase)
{
    float dx;
    float dy;
    float dz;
    int* self;
    char* rel;
    int selfPos;
    int candPos;
    int* cand;
    int i;
    int off;
    int bestVisibleIdx;
    double distSq;
    double bestHidden;
    double bestVisible;
    long long packed;
    int count;

    packed = FUN_803826d8();
    self = (int*)(int)(packed >> 0x20);
    count = (int)packed;
    bestVisible = (double)FLOAT_80615e40;
    bestHidden = bestVisible;
    bestVisibleIdx = -1;
    off = 0;
    for (i = 0; i < count; i = i + 1) {
        cand = *(int**)(listBase + off);
        if (((cand[9] & 8U) == 0) && ((cand[0x31b] & 4U) == 0)) {
            rel = (char*)aiactor_relation_entry(self[2], cand[2]);
            candPos = *cand;
            selfPos = *self;
            dx = *(float*)(selfPos + 100) - *(float*)(candPos + 100);
            dy = *(float*)(selfPos + 0x68) - *(float*)(candPos + 0x68);
            dz = *(float*)(selfPos + 0x6c) - *(float*)(candPos + 0x6c);
            distSq = (double)(dz * dz + dx * dx + dy * dy);
            if ((i < DAT_80612610) || (self[1] == candPos)) {
                distSq = (double)(float)(distSq * (double)FLOAT_80615e44);
            }
            if (*rel == '\0') {
                // not yet visible: track nearest as fallback
                if (bestVisibleIdx == -1) {
                    if (distSq < bestHidden) {
                        bestHidden = distSq;
                    }
                }
            } else {
                // visible hostile: track nearest
                if (distSq < bestVisible) {
                    bestVisibleIdx = i;
                    bestVisible = distSq;
                }
            }
        }
        off = off + 4;
    }
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// aiactor_retarget_scan_visible_list (0x801423f4)
// Build a candidate list from the packed visible-actor table (DAT_80588610,
// DAT_80612610 entries), optionally append the reference player (single-player
// or non-coop), and hand it to aiactor_scan_nearest_visible_enemy using the
// actor's +0x7c self pointer as the scan origin.
void aiactor_retarget_scan_visible_list(int actor)
{
    int* visEntry;
    int selfPtr;
    int count;
    int refPlayer;
    int srcOff;
    int i;
    int list[32];

    selfPtr = *(int*)(actor + 0x7c);
    count = 0;
    srcOff = 0;
    for (i = 0; i < DAT_80612610; i = i + 1) {
        visEntry = (int*)((int)&DAT_80588610 + srcOff);
        srcOff = srcOff + 4;
        list[count] = *visEntry;
        count = count + 1;
    }
    if (((DAT_805015bd != '\n') || (DAT_80612610 < 2))) {
        refPlayer = FUN_8027ea98();
        if (refPlayer != 0) {
            list[count] = refPlayer;
            count = count + 1;
        }
    }
    aiactor_scan_nearest_visible_enemy((u32)selfPtr, (u32)count, (int)list);
}
