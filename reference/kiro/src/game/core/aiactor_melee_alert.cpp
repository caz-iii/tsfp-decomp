// Address range: 0x8013ff28 - 0x80140a2b | batch_375 | 2026-09-05
//
// AI actor melee/vehicle checks, melee-swipe damage, coop alert-broadcast
// sweeps, zone-door trigger tick, and a spawn-point visibility cache. Tail of
// the AI-actor block (batches 364-374) rolling into adjacent helpers.
//
// Conventions carried from prior batches:
//   * Per-actor render object = *(actor + 0x7c); +0xbf0 = alert/state object.
//   * FUN_803826e8 / FUN_803826e4 return a packed 64-bit (hi:lo) gpr frame:
//     lo = index/flag, hi = actor pointer. Read here via a typed helper struct.
//   * FUN_80382730 / FUN_80382734 pop that scratch frame (void).
//   * DAT_805015bd = game mode byte (0x0a coop). DAT_80612610 = base player
//     count; DAT_80588610 = visible-node ptr table, count DAT_80612098.
//   * DAT_80611928 = frame counter; DAT_806108e4 = melee-swipe cooldown frames.
//   * damage_dispatch_to_entity takes the wide float ABI + a stack hit-record.
//
// Functions:
//   aiactor_target_is_vehicle       0x8013ff28  is current alert target a VICE/VICO vehicle?
//   aiactor_melee_swipe             0x8013ff70  melee LOS swipe -> damage dispatch
//   aiactor_compute_flee_target     0x80140424  jittered flee/steer destination point
//   aiactor_alert_zone_broadcast    0x80140594  coop: flag same-zone actors (variant A)
//   aiactor_alert_zone_broadcast_b  0x801406e8  coop: flag same-zone actors (variant B, +flag8 gate)
//   aiactor_zone_door_tick          0x80140844  open pending zone-door portals (3-slot history)
//   aiactor_spawnvis_reset_globals  0x80140910  zero 0x780-byte spawn-vis scratch block
//   aiactor_spawnvis_clear_table    0x80140940  clear 8-entry pointer table
//   aiactor_spawnvis_reset_cache    0x80140968  reset spawn-point cache pointer
//   aiactor_spawnvis_build_cache    0x80140974  alloc + populate spawn-point visibility cache

#include "common.h"

// ---- named cross-module helpers (self-externed; common.h has no decls) -----
extern "C" {
extern int  player_ai_is_story_mode(int charId);
extern int  player_ai_is_explosive_weapon(int weaponType);
extern void mtx4_set_translate_rotz(double x, double y, double z, double yaw, float* outMtx);
extern void mtx4_set_rotation_y(double yaw, float* outMtx);
extern void mtx4_multiply(float* a, float* b, float* out);
extern void mtx4_transform_vector_alias(float* mtx, float* vecInOut);
extern int  entity_los_check_default(int actor, float* from, int flags, float* to,
                                     float* outHit, float* unused);
extern void switch_slot_reset(int hitRecord);
extern int  rng_counter_tick(void);
extern int* soundpad_resolve_zone_node(int zoneId);
extern int  soundpad_get_entity_zone_id(int node);
extern unsigned int FUN_8027eb3c(int actorState);   // actor cull/visibility predicate
extern int  soundpad_find_zone_for_entity(int zoneId);
extern int  trigger_check_activation(void);
extern void soundpad_portal_activate(int portalId);
extern void memset_wrapper(int dst, int val, int len);

// mem_alloc: wide-float-ABI allocator. Only the integer args (size, tag) matter
// here; the leading register-pair args are pass-through scratch.
extern int  mem_alloc(long long a, double b, double c, double d, long long e,
                      long long f, long long g, long long h,
                      int size, int tag, int i, int j, int k, int l, int m, int n);

// Combat helpers (wide-float ABI; return value in EAX/f1). Declared to match
// call-site arity; the double args are the scratch-frame float registers.
extern int  FUN_80364290(int actorState, unsigned int flag);       // stance/behaviour query
extern void damage_dispatch_to_entity(double a, double b, double c, double d,
                                       double e, double f, double g, double h,
                                       void* hitRecord);
extern long long player_ai_drop_all_weapons(long long r, double a, double b, double c,
                                            long long e, long long f, long long g,
                                            long long h, int* actor);

// Packed-64 gpr scratch-frame helpers. FUN_803826e8/e4 return hi:lo where
// lo = flag/index (EAX) and hi = actor pointer.
extern unsigned long long FUN_803826e8(void);
extern int  FUN_803826e4(void);
extern void FUN_80382730(void);
extern void FUN_80382734(void);

// libc-style frame save/restore emitted by the compiler for FUN_80140844.
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);
}

// ---- raw data symbols ------------------------------------------------------
extern char  DAT_805015bd;    // game mode byte (0x0a coop)
extern int   DAT_80612610;    // base player/actor count
extern int   DAT_80588610;    // visible-node ptr table
extern int   DAT_80612098;    // visible-node count
extern int   DAT_80611928;    // frame counter
extern int   DAT_806108e4;    // melee-swipe cooldown frames
extern int   DAT_80611c24;    // trigger record pool base (0x2e8 stride)
extern int   DAT_80588c58;    // spawn-vis 8-entry pointer table
extern int   DAT_80612108;    // spawn-point visibility cache ptr
extern int   DAT_8061208c;    // level/spawn config object ptr (+0x74 list, +0x78 count)

// melee-swipe / flee tuning constants.
extern float FLOAT_80615da8;  // 0.0
extern float FLOAT_80615db0;  // base melee damage
extern float FLOAT_80615db4;  // aim lead factor (normal)
extern float FLOAT_80615dbc;  // deg->rad numerator (180.0)
extern float FLOAT_80615dc0;  // deg->rad denominator / angle wrap (pi or 180)
extern float FLOAT_80615dd8;  // angle wrap step (2pi)
extern float FLOAT_80615ddc;  // angle wrap low bound (-pi)
extern float FLOAT_80615de0;  // rear-arc threshold
extern float FLOAT_80615de4;  // rear-arc damage
extern float FLOAT_80615de8;  // story-mode / crouch scale
extern float FLOAT_80615dec;  // explosive vs actor scale
extern float FLOAT_80615df0;  // explosive vs player scale
extern float FLOAT_80615df4;  // hit-record field a
extern float FLOAT_80615df8;  // hit-record field b
extern float FLOAT_80615dfc;  // flee angle variant 0
extern float FLOAT_80615e00;  // flee angle variant 1
extern float FLOAT_80615e04;  // flee angle variant 2
extern float FLOAT_80615e08;  // flee jitter amplitude
extern float FLOAT_80615e0c;  // rng normalize divisor
extern float FLOAT_80615e10;  // flee radius scale
extern double DOUBLE_80615e18; // rng->double bias (0x43300000 pair)
extern float DAT_803ccad0;    // flee base offset x
extern float DAT_803ccad4;    // flee base offset y
extern float DAT_803ccad8;    // flee base offset z

// ---------------------------------------------------------------------------
// aiactor_target_is_vehicle (0x8013ff28)
// True if the entity is type 8 (character) and its alert object's tag (+8) is
// the 'VICE'/'VICO' vehicle magic ("ECIV"/"OCIV" little-endian words).
u32 aiactor_target_is_vehicle(int entity)
{
    int tag;

    if (*(int*)(entity + 0x20) != 8) {
        return 0;
    }
    tag = *(int*)(*(int*)(*(int*)(entity + 0x7c) + 0xbf0) + 8);
    if ((tag != 0x45434956) && (tag != 0x4f434956)) {
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// aiactor_melee_swipe (0x8013ff70)
// Per-frame melee swipe. Reads the packed gpr scratch frame (lo = left/right
// side flag, hi = attacker actor). Builds a swing matrix from a bone transform,
// interpolates the swing arc endpoint (shortened in story mode), LOS-checks it,
// and on hitting a type-8 character dispatches melee damage. The `else` branch
// handles a continuous-contact re-hit gated by a per-target hit stamp (+0xb6e).
void aiactor_melee_swipe(u32 unused1, u32 unused2, u32 param3)
{
    float leadScale;
    int hitEntity;
    int attacker;
    int targetObj;
    int* attackerState;
    double dvArc;
    double dvTargetYaw;
    double dvArcYaw;
    unsigned long long frame;
    int side;

    float swingEnd[3];
    float arcStart[3];
    float swingMtx0;
    float swingMtx1;
    float swingMtx2;
    float boneMtxA[16];
    float boneMtxB[16];
    int hitRecA[4];   // { hitEntity, attacker, attacker, ... }
    u32 hitRecA_pad0;
    u32 hitRecA_pad1;
    float hitRecA_dmg;
    float* hitRecA_pos;
    float hitRecA_fa;
    float hitRecA_fb;
    int hitRecB[4];
    u32 hitRecB_pad0;
    u32 hitRecB_pad1;
    float hitRecB_dmg;

    // NOTE: the original packs these locals into two switch_slot_reset-managed
    // hit records; we keep them as explicit fields to stay compile-clean.
    (void)hitRecA; (void)hitRecA_pad0; (void)hitRecA_pad1; (void)hitRecA_dmg;
    (void)hitRecA_pos; (void)hitRecA_fa; (void)hitRecA_fb;
    (void)hitRecB; (void)hitRecB_pad0; (void)hitRecB_pad1; (void)hitRecB_dmg;
    (void)swingMtx0; (void)swingMtx1; (void)swingMtx2;

    frame = FUN_803826e8();
    side = (int)frame;
    attacker = (int)(frame >> 0x20);
    attackerState = *(int**)(attacker + 0x7c);

    if (attackerState[0x2da] + DAT_806108e4 < DAT_80611928) {
        *(unsigned short*)(attackerState + 0x2db) = 0;
    }
    attackerState[0x2da] = DAT_80611928;

    int targetActor = attackerState[1];
    int targetState = 0;
    if (targetActor != 0) {
        targetState = *(int*)(targetActor + 0x7c);
    }

    // Gate: attacker/target must be live, past base count, and engageable.
    bool blocked =
        (attackerState == (int*)0) ||
        (attackerState[0] == 0) ||
        (attackerState[2] < DAT_80612610) ||
        (((*(uint*)(attackerState[0] + 0x28) & 0x100) == 0) &&
         ((*(uint*)(attackerState[0] + 0x10) & 0xffff) != 0)) ||
        (targetActor == 0) ||
        (targetState == 0) ||
        (*(int*)(targetState + 8) < DAT_80612610) ||
        (((*(uint*)(targetActor + 0x28) & 0x100) == 0) &&
         ((*(uint*)(targetActor + 0x10) & 0xffff) != 0));

    if (!blocked) {
        // Build swing matrix at attacker position + yaw, concat with weapon bone.
        mtx4_set_translate_rotz((double)*(float*)(attacker + 100),
                                (double)*(float*)(attacker + 0x68),
                                (double)*(float*)(attacker + 0x6c),
                                (double)((FLOAT_80615dbc * *(float*)(attacker + 0x70)) / FLOAT_80615dc0),
                                boneMtxA);
        int model = *(int*)(attacker + 4);
        int boneBase = *(int*)(model + 0x10);
        int boneTbl = *(int*)(model + 0x84);
        mtx4_multiply((float*)(boneBase + *(int*)(boneTbl + 0x18) * 0x40), boneMtxA, boneMtxB);
        arcStart[0] = boneMtxB[12];
        arcStart[1] = boneMtxB[13];
        arcStart[2] = boneMtxB[14];
        if (side == 0) {
            mtx4_multiply((float*)(boneBase + *(int*)(boneTbl + 0x2c) * 0x40), boneMtxA, boneMtxB);
        } else {
            mtx4_multiply((float*)(boneBase + *(int*)(boneTbl + 0x30) * 0x40), boneMtxA, boneMtxB);
        }
        float endX = boneMtxB[12] - arcStart[0];
        float endY = boneMtxB[13] - arcStart[1];
        float endZ = boneMtxB[14] - arcStart[2];

        leadScale = FLOAT_80615db4;
        if (player_ai_is_story_mode((int)*(short*)((int)attackerState + 0x4a)) != 0) {
            leadScale = FLOAT_80615de8;
        }
        endZ = endZ * leadScale + arcStart[2];
        endY = endY * leadScale + arcStart[1];
        endX = endX * leadScale + arcStart[0];
        dvArcYaw = (double)endX;

        // arcStart/&swing endpoint stored back into attacker state at +0x399.
        float swingPt[3];
        swingPt[0] = endX; swingPt[1] = endY; swingPt[2] = endZ;

        hitEntity = entity_los_check_default(attacker, arcStart,
                                             (int)(attackerState + 0x399), swingPt,
                                             swingEnd, (float*)0);
        if (hitEntity != 0) {
            if (*(int*)(hitEntity + 0x20) == 8) {
                targetObj = *(int*)(hitEntity + 0x7c);
            } else {
                targetObj = 0;
            }
            if (targetObj != 0) {
                struct HitRecord {
                    int   entity;
                    int   attackerA;
                    int   attackerB;
                    u32   pad0;
                    u32   pad1;
                    float dmg;
                    float* pos;
                    float fa;
                    float fb;
                } rec;
                switch_slot_reset((int)&rec);

                dvTargetYaw = (double)*(float*)(hitEntity + 0x70);
                for (dvArc = dvTargetYaw - (double)*(float*)(attacker + 0x70);
                     dvArc = (double)(float)dvArc, dvArc <= (double)FLOAT_80615ddc;
                     dvArc = dvArc + (double)FLOAT_80615dd8) {}
                for (; (double)FLOAT_80615dc0 < dvArc;
                     dvArc = (double)(float)(dvArc - (double)FLOAT_80615dd8)) {}

                if ((double)FLOAT_80615de0 <= ABS(dvArc)) {
                    rec.dmg = FLOAT_80615de4;
                } else {
                    rec.dmg = FLOAT_80615db0;
                }
                rec.entity = hitEntity;
                rec.attackerA = attacker;
                rec.attackerB = attacker;
                if (FUN_80364290((int)attackerState, 0x80) != 0) {
                    rec.dmg = rec.dmg * FLOAT_80615de8;
                }
                if (player_ai_is_explosive_weapon((int)*(short*)((int)attackerState + 0x4a)) != 0) {
                    if ((targetObj == 0) || (DAT_80612610 <= *(int*)(targetObj + 8))) {
                        rec.dmg = rec.dmg * FLOAT_80615df0;
                    } else {
                        rec.dmg = rec.dmg * FLOAT_80615dec;
                    }
                }
                rec.pos = swingEnd;
                rec.fa = FLOAT_80615df4;
                rec.fb = FLOAT_80615df8;
                rec.pad0 = param3;
                rec.pad1 = param3;
                damage_dispatch_to_entity((double)FLOAT_80615df4, dvArc, dvTargetYaw, dvArcYaw,
                                          0.0, 0.0, 0.0, 0.0, &rec);
            }
        }
        attackerState[0x399] = (int)swingPt[0];
        attackerState[0x39a] = (int)swingPt[1];
        attackerState[0x39b] = (int)swingPt[2];
    } else if (targetState != 0 &&
               *(unsigned short*)(targetState + 0xb6e) < *(unsigned short*)(attackerState + 0x2db)) {
        *(unsigned short*)(targetState + 0xb6e) = *(unsigned short*)(attackerState + 0x2db);
        struct HitRecordB {
            int   entity;
            int   attackerA;
            int   attackerB;
            u32   pad0;
            u32   pad1;
            float dmg;
        } recB;
        switch_slot_reset((int)&recB);

        dvArc = (double)*(float*)(targetActor + 0x70);
        for (dvTargetYaw = dvArc - (double)*(float*)(attacker + 0x70);
             dvTargetYaw = (double)(float)dvTargetYaw, dvTargetYaw <= (double)FLOAT_80615ddc;
             dvTargetYaw = dvTargetYaw + (double)FLOAT_80615dd8) {}
        for (; (double)FLOAT_80615dc0 < dvTargetYaw;
             dvTargetYaw = (double)(float)(dvTargetYaw - (double)FLOAT_80615dd8)) {}
        if ((double)FLOAT_80615de0 <= ABS(dvTargetYaw)) {
            recB.dmg = FLOAT_80615de4;
        } else {
            recB.dmg = FLOAT_80615db0;
        }
        recB.entity = targetActor;
        recB.attackerA = attacker;
        recB.attackerB = attacker;
        if (FUN_80364290((int)attackerState, 0x80) != 0) {
            recB.dmg = (float)((double)recB.dmg * (double)FLOAT_80615de8);
        }
        if (player_ai_is_explosive_weapon((int)*(short*)((int)attackerState + 0x4a)) != 0) {
            recB.dmg = (float)((double)recB.dmg * (double)FLOAT_80615dec);
        }
        recB.pad0 = 0;
        recB.pad1 = 0;
        damage_dispatch_to_entity((double)recB.dmg, dvTargetYaw, dvArc,
                                  0.0, 0.0, 0.0, 0.0, 0.0, &recB);
    }
    FUN_80382734();
    (void)unused1; (void)unused2;
}

// ---------------------------------------------------------------------------
// aiactor_compute_flee_target (0x80140424)
// Compute a jittered flee/steer destination in world space: rotate a base
// offset vector by a per-actor yaw plus an angle variant (index %3) plus an
// rng jitter term, scale, and add to the actor's parent position.
void aiactor_compute_flee_target(int actor, float* outPos)
{
    float variantAngle;
    int parent;
    int variantSel;
    int renderObj;
    double baseYaw;
    double rngJitter;
    float offset[3];
    float rotMtx[17];
    unsigned int rngRaw;
    u32 dblHi;

    renderObj = *(int*)(actor + 0x7c);
    baseYaw = (double)FLOAT_80615da8;
    offset[0] = DAT_803ccad0;
    offset[1] = DAT_803ccad4;
    offset[2] = DAT_803ccad8;
    if (*(int*)(renderObj + 4) != 0) {
        baseYaw = (double)(float)(baseYaw + (double)*(float*)(*(int*)(renderObj + 4) + 0x70));
    }
    variantSel = *(int*)(renderObj + 8) % 3;
    variantAngle = FLOAT_80615dfc;
    if (variantSel == 1) {
        variantAngle = FLOAT_80615e00;
    }
    if (variantSel == 2) {
        variantAngle = FLOAT_80615e04;
    }
    rngRaw = (unsigned int)rng_counter_tick();
    dblHi = 0x43300000;
    rngJitter = (double)((float)((double)(*(double*)&dblHi) - DOUBLE_80615e18) / FLOAT_80615e0c);
    (void)rngRaw;
    double yaw = (double)((FLOAT_80615dbc *
                          (float)(baseYaw - (double)(float)((double)FLOAT_80615e08 * rngJitter
                                                            - (double)variantAngle))) / FLOAT_80615dc0);
    mtx4_set_rotation_y(yaw, rotMtx);
    mtx4_transform_vector_alias(rotMtx, offset);
    offset[1] = offset[1] * FLOAT_80615e10;
    offset[2] = offset[2] * FLOAT_80615e10;
    *outPos      = *(float*)(*(int*)(renderObj + 4) + 100) + offset[0] * FLOAT_80615e10;
    outPos[1]    = *(float*)(*(int*)(renderObj + 4) + 0x68) + offset[1];
    outPos[2]    = *(float*)(*(int*)(renderObj + 4) + 0x6c) + offset[2];
}

// ---------------------------------------------------------------------------
// aiactor_alert_zone_broadcast (0x80140594)
// Coop alert broadcast (variant A): iterate active visible actors; for each not
// culled (FUN_8027eb3c==0), whose flag word (coop:+5, else:+7) lacks bits
// 0x600001, resolve its zone and if it matches the caller's zone, drop the
// actor's weapons and set its alert/kill state (+0x2ab/+0x2aa) and bit31 at +0xb.
void aiactor_alert_zone_broadcast(long long r0, double a, double b, double c,
                                  long long e, long long f, long long g, long long h)
{
    int callerZone;
    u32 flags;
    int* node;
    int zoneId;
    int byteOff;
    int idx;
    int* actorState;
    long long scratch;

    callerZone = FUN_803826e4();
    byteOff = DAT_80612610 << 2;
    idx = DAT_80612610;
    scratch = r0;
    do {
        if (DAT_80612098 <= idx) {
            FUN_80382730();
            return;
        }
        actorState = *(int**)((int)&DAT_80588610 + byteOff);
        if (FUN_8027eb3c((int)actorState) == 0) {
            if (DAT_805015bd == '\n') {
                flags = actorState[5];
            } else {
                flags = actorState[7];
            }
            if ((flags & 0x600001) == 0) {
                node = soundpad_resolve_zone_node(actorState[0x19e]);
                zoneId = soundpad_get_entity_zone_id((int)node);
                zoneId = soundpad_find_zone_for_entity(zoneId);
                if (callerZone == zoneId) {
                    scratch = player_ai_drop_all_weapons(scratch, a, b, c, e, f, g, h, actorState);
                    bool doSet = true;
                    if (actorState[0x2ab] == 1 && actorState[0x2aa] == 0) {
                        u32 f2 = (DAT_805015bd == '\n') ? actorState[5] : actorState[7];
                        if ((f2 & 1) != 0) doSet = false;
                    }
                    if (doSet) {
                        actorState[0x2ab] = 0x400000;
                        actorState[0x2aa] = 0;
                    }
                    actorState[0xb] = actorState[0xb] | 0x80000000;
                }
            }
        }
        idx = idx + 1;
        byteOff = byteOff + 4;
    } while (true);
}

// ---------------------------------------------------------------------------
// aiactor_alert_zone_broadcast_b (0x801406e8)
// Variant B: same as above but additionally gated on the +9 word bit 8 being
// clear, and the zone check stops one resolve earlier (uses the node's own
// entity zone id directly rather than re-resolving to a group).
void aiactor_alert_zone_broadcast_b(long long r0, double a, double b, double c,
                                    long long e, long long f, long long g, long long h)
{
    int callerZone;
    u32 flags;
    int* node;
    int zoneId;
    int byteOff;
    int idx;
    int* actorState;
    long long scratch;

    callerZone = FUN_803826e4();
    byteOff = DAT_80612610 << 2;
    idx = DAT_80612610;
    scratch = r0;
    do {
        if (DAT_80612098 <= idx) {
            FUN_80382730();
            return;
        }
        actorState = *(int**)((int)&DAT_80588610 + byteOff);
        if (FUN_8027eb3c((int)actorState) == 0) {
            if (DAT_805015bd == '\n') {
                flags = actorState[5];
            } else {
                flags = actorState[7];
            }
            if (((flags & 0x600001) == 0) && ((actorState[9] & 8U) == 0)) {
                node = soundpad_resolve_zone_node(actorState[0x19e]);
                zoneId = soundpad_get_entity_zone_id((int)node);
                if (callerZone == zoneId) {
                    scratch = player_ai_drop_all_weapons(scratch, a, b, c, e, f, g, h, actorState);
                    bool doSet = true;
                    if (actorState[0x2ab] == 1 && actorState[0x2aa] == 0) {
                        u32 f2 = (DAT_805015bd == '\n') ? actorState[5] : actorState[7];
                        if ((f2 & 1) != 0) doSet = false;
                    }
                    if (doSet) {
                        actorState[0x2ab] = 0x400000;
                        actorState[0x2aa] = 0;
                    }
                    actorState[0xb] = actorState[0xb] | 0x80000000;
                }
            }
        }
        idx = idx + 1;
        byteOff = byteOff + 4;
    } while (true);
}

// ---------------------------------------------------------------------------
// aiactor_zone_door_tick (0x80140844)
// Walk the caller actor's 3-slot zone-door history (+0xe7c). For each valid,
// non-stale door trigger record (pool DAT_80611c24, 0x2e8 stride, generation
// at +0x2c), if it is not currently activating, open its portal and clear the
// history slot. The actor pointer arrives via the compiler's __save_gpr_28.
void aiactor_zone_door_tick(void)
{
    int renderObj;
    u32 handle;
    int rec;
    int slotIdx;
    int byteOff;

    renderObj = *(int*)(__save_gpr_28() + 0x7c);
    slotIdx = 0;
    byteOff = 0;
    do {
        int slot = renderObj + byteOff;
        handle = *(u32*)(slot + 0xe7c);
        if (handle != 0) {
            rec = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
            if (*(u32*)(rec + 0x2c) != handle) {
                rec = 0;
                *(int*)(slot + 0xe7c) = 0;
            }
            if ((rec != 0) && (trigger_check_activation() == 0)) {
                soundpad_portal_activate(*(int*)(*(int*)(rec + 0x7c) + 0x44));
                *(int*)(renderObj + byteOff + 0xe7c) = 0;
                __restore_gpr_28();
                return;
            }
        }
        slotIdx = slotIdx + 1;
        byteOff = byteOff + 4;
        if (2 < slotIdx) {
            __restore_gpr_28();
            return;
        }
    } while (true);
}

// ---------------------------------------------------------------------------
// aiactor_spawnvis_reset_globals (0x80140910)
// Zero the 0x780-byte spawn-visibility scratch block (fixed address 0x8058...).
void aiactor_spawnvis_reset_globals(void)
{
    memset_wrapper(-0x7fa781f0, 0, 0x780);
}

// ---------------------------------------------------------------------------
// aiactor_spawnvis_clear_table (0x80140940)
// Clear the 8-entry spawn-vis pointer table.
void aiactor_spawnvis_clear_table(void)
{
    int byteOff;
    int remaining;

    byteOff = 0;
    remaining = 8;
    do {
        *(int*)((int)&DAT_80588c58 + byteOff) = 0;
        byteOff = byteOff + 4;
        remaining = remaining + -1;
    } while (remaining != 0);
}

// ---------------------------------------------------------------------------
// aiactor_spawnvis_reset_cache (0x80140968)
// Reset the spawn-point visibility cache pointer.
void aiactor_spawnvis_reset_cache(void)
{
    DAT_80612108 = 0;
}

// ---------------------------------------------------------------------------
// aiactor_spawnvis_build_cache (0x80140974)
// Lazily allocate (0x18 bytes/spawn, 16-byte aligned) and populate the spawn
// point visibility cache: for every spawn record (base +0x74, count +0x78,
// stride 0x98) whose flag word (+0x54) has bit 0x8000, copy its id (+0x64) into
// the cache entry at +0xc. Leading register-pair args pass through to mem_alloc.
void aiactor_spawnvis_build_cache(long long p1, double p2, double p3, double p4,
                                  long long p5, long long p6, long long p7, long long p8)
{
    int spawnRec;
    int cacheOff;
    int recOff;
    int idx;

    if (*(int*)(DAT_8061208c + 0x78) != 0) {
        if (DAT_80612108 == 0) {
            DAT_80612108 = mem_alloc(p1, p2, p3, p4, p5, p6, p7, p8,
                                     (*(int*)(DAT_8061208c + 0x78) * 0x18 + 0xf) & 0xfffffff0,
                                     1, 0, 0, 0, 0, 0, 0);
        }
        memset_wrapper(DAT_80612108, 0, *(int*)(DAT_8061208c + 0x78) * 0x18);
        idx = 0;
        cacheOff = 0;
        recOff = 0;
        while (true) {
            if (*(int*)(DAT_8061208c + 0x78) <= idx) break;
            spawnRec = *(int*)(DAT_8061208c + 0x74) + recOff;
            if ((*(uint*)(spawnRec + 0x54) & 0x8000) != 0) {
                *(int*)(DAT_80612108 + cacheOff + 0xc) = *(int*)(spawnRec + 100);
            }
            idx = idx + 1;
            cacheOff = cacheOff + 0x18;
            recOff = recOff + 0x98;
        }
    }
}
