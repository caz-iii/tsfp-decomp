// Address: 0x8035098c | Name: objective_pick_zone_in_bounds | Date: 2026-09-12
// Batch: batch_1002
//
// Objective / spawn trigger evaluation. Continues the objective marker / wave
// subsystem (objective_markers.cpp, objective_waves.cpp, spawn_bag.cpp).
//
// Per game mode the spawn variant (spawn_mode_variant) selects a condition
// table: DAT_805c7c24[variant] is an array of 0x18-byte trigger descriptors:
//   +0x00 activated flag      +0x08 team0 cond ptr   +0x0c team1 cond ptr
//   +0x10 mode flag
// Entity pool: DAT_805c7c10 base (0xb4 stride), DAT_805c7c0c count; each entry
// has a flag byte at +0xb0 and, via entity_pool_find_type1, a vtable at +0x10.

#include "common.h"

// ---- raw symbols ----
extern char DAT_805015bd;          // game-mode byte
extern unsigned int DAT_805015c4;  // objective result/state bitmask
extern char DAT_805015c8;
extern int  DAT_80403738;          // entity-class descriptor table base
extern int  DAT_80611aa8;          // zone descriptor table base (0xb4 stride)
extern int  DAT_80612db0;          // active-object list context
extern int  DAT_805c7c08;          // zone record table base (0x8c stride)
extern int  DAT_805c7c0c;          // entity pool count
extern int  DAT_805c7c10;          // entity pool base (0xb4 stride)
extern int  DAT_805c7c24;          // per-variant trigger table array
extern int  DAT_8061208c;          // match/objective context
extern int  PTR_FUN_804821b8;      // fallback condition callback table (8-byte entries)

// ---- named cross-module helpers ----
extern u32  objective_entity_in_bounds(int pEntity, float *bounds);
extern int  spawn_mode_variant(void);
extern int  player_ai_resolve_entity_callback(int pDesc);
extern int  entity_pool_find_type1(int index);
extern int  match_desc_field5(int key);
extern u32  match_desc_field6(int key);

// Pointer-returning lookups.
extern "C" int * FUN_80351c50(int id);   // team-0 objective descriptor by id
extern "C" u32 * FUN_80351c90(int id);   // team-1 objective descriptor by id

// Register-pair context stubs.
extern "C" unsigned long long FUN_803826d8(void);
extern "C" void               FUN_80382724(void);
extern "C" unsigned long long FUN_803826e4(void);
extern "C" void               FUN_80382730(void);
extern "C" unsigned long long FUN_803826e8(void);
extern "C" void               FUN_80382734(void);
extern "C" unsigned long long FUN_803826f0(void);
extern "C" void               FUN_8038273c(void);
extern "C" int                __save_gpr_28(void);
extern "C" void               __restore_gpr_28(void);

// ---------------------------------------------------------------------------
// From a small zone-index list on the objective record (count +0x30, ids at
// +0x38), select the zone whose bounds contain the record's own position and
// store it at +0x34.
void objective_pick_zone_in_bounds(void)
{
    int rec = __save_gpr_28();
    int off = 0;
    for (int i = 0; i < *(char *)(rec + 0x30); i = i + 1) {
        int zoneIdx = *(int *)(rec + off + 0x38);
        int hit = objective_entity_in_bounds(
            rec, (float *)(DAT_80611aa8 + zoneIdx * 0xb4 + 0x10));
        if (hit != 0) {
            *(u32 *)(rec + 0x34) = *(u32 *)(rec + off + 0x38);
        }
        off = off + 4;
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// Compute the visible-zone bitmask for both teams and store it on their state
// records (+0x30). Combines base zone visibility, an AI-role adjustment table
// (selected by the class descriptor value 0x8dd..0x8f5), and the "not seen"
// fallback zones depending on state flag bit 2.
void objective_compute_visible_zone_mask(void)
{
    unsigned long long ctxPair = FUN_803826d8();
    int ctxA = (int)(unsigned)(ctxPair >> 0x20);
    int ctxB = (int)ctxPair;
    u32 maskA = 0;
    u32 maskB = 0;

    int stateCtx = *(int *)(ctxA + 0x7c);
    int *zonesA = *(int **)(*(int *)(ctxA + 4) + 4);
    int *zonesB = *(int **)(*(int *)(ctxB + 4) + 4);
    int *descA = FUN_80351c50((int)*(unsigned short *)(ctxA + 0x94));
    u32 *descB = FUN_80351c90(descA[0x12]);

    if (descA[1] != -1) {
        maskA = (u32)zonesA[*zonesA * -0x30 + descA[1] * 0x30 + 0x14];
    }
    if (descB[1] != -1) {
        maskB = (u32)zonesB[*zonesB * -0x30 + descB[1] * 0x30 + 0x14];
    }

    int classIdx = *(int *)(stateCtx + 0x78);
    if ((0 < classIdx) && (DAT_805015bd == '\n')) {
        int role = player_ai_resolve_entity_callback((int)(&DAT_80403738 + classIdx * 0x54));
        int fieldOff = -1;
        if (role < 0x8ea) {
            if (role < 0x8e4) {
                if (0x8dd < role) fieldOff = 0x28;
            } else {
                fieldOff = 0x2c;
            }
        } else if (role < 0x8f6) {
            if (role < 0x8f0) fieldOff = 0x30;
            else fieldOff = 0x24;
        }
        if (fieldOff != -1) {
            int n = descA[8];
            int p = 0;
            while (0 < n) {
                maskA |= (u32)zonesA[*zonesA * -0x30 + *(int *)((int)descA + p + fieldOff) * 0x30 + 0x14];
                p = p + 0x10;
                n = n + -1;
            }
            n = (int)descB[8];
            p = 0;
            while (0 < n) {
                maskB |= (u32)zonesB[*zonesB * -0x30 + *(int *)((int)descB + p + fieldOff) * 0x30 + 0x14];
                p = p + 0x10;
                n = n + -1;
            }
        }
    }

    if ((*(u32 *)(stateCtx + 0xc) & 2) == 0) {
        maskA |= (u32)zonesA[*zonesA * -0x30 + descA[2] * 0x30 + 0x14];
        maskB |= (u32)zonesB[*zonesB * -0x30 + descB[2] * 0x30 + 0x14];
    } else {
        if ((maskA == 0) || (descA[1] != -1)) {
            maskA |= (u32)zonesA[*zonesA * -0x30 + descA[3] * 0x30 + 0x14];
        }
        if ((maskB == 0) || (descB[1] != -1)) {
            maskB |= (u32)zonesB[*zonesB * -0x30 + descB[3] * 0x30 + 0x14];
        }
    }

    *(u32 *)(*(int *)(ctxA + 4) + 0x30) = maskA;
    *(u32 *)(*(int *)(ctxB + 4) + 0x30) = maskB;
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// True unless some active object owns a type-7 zone record matching the given
// team (i.e. the objective is still "clear" for that team).
bool objective_no_active_type7_owner(int team)
{
    int count = *(int *)(DAT_80612db0 + 0x1950);
    int off = 0;
    int remaining = count;
    if (0 < count) {
        do {
            int zone = DAT_805c7c08 + *(int *)(DAT_80612db0 + off + 0x13c) * 0x8c;
            if ((*(int *)(DAT_80612db0 + off + 0xb4) != 3) &&
                (*(int *)(zone + 0x80) == 7) &&
                (*(int *)(zone + 0x7c) == team)) {
                return false;
            }
            off = off + 0x8c;
            remaining = remaining + -1;
        } while (remaining != 0);
    }
    return (bool)('\x01' - (count == 0));
}

// ---------------------------------------------------------------------------
// Team-0 trigger condition: descriptor slot +8 non-zero and no active type-7
// owner for team 0.
bool objective_cond_team0_clear(int /*unused*/, int slot)
{
    int variant = spawn_mode_variant();
    bool ok;
    if (*(int *)(*(int *)(DAT_805c7c24 + variant * 4) + slot * 0x18 + 8) == 0) {
        ok = false;
    } else {
        ok = objective_no_active_type7_owner(0);
    }
    return ok;
}

// Team-1 trigger condition: descriptor slot +0xc non-zero and no active type-7
// owner for team 1.
bool objective_cond_team1_clear(int /*unused*/, int slot)
{
    int variant = spawn_mode_variant();
    bool ok;
    if (*(int *)(*(int *)(DAT_805c7c24 + variant * 4) + slot * 0x18 + 0xc) == 0) {
        ok = false;
    } else {
        ok = objective_no_active_type7_owner(1);
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Scan the entity pool for one whose flag byte matches the current key's
// bitmask; stop at the first (or first missing) entity depending on the
// descriptor mode flag.
void objective_scan_entities_flag(void)
{
    unsigned long long keyPair = FUN_803826e4();
    int variant = spawn_mode_variant();
    int poolOff = 0;
    int modeFlag = *(int *)(*(int *)(DAT_805c7c24 + variant * 4) + (int)keyPair * 0x18 + 0x10);
    u32 keyMask = (u32)(unsigned)(keyPair >> 0x20);

    for (int i = 0; i < DAT_805c7c0c; i = i + 1) {
        if (((u32)*(byte *)(DAT_805c7c10 + poolOff + 0xb0) & keyMask) != 0) {
            int ent = entity_pool_find_type1(i);
            bool present = false;
            int (**vtbl)() = *(int (***)())(ent + 0x10);
            if ((vtbl != nullptr) && ((*vtbl)() != 0)) {
                present = true;
            }
            if (modeFlag == 0) {
                if (present) break;
            } else if (!present) {
                break;
            }
        }
        poolOff = poolOff + 0xb4;
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// Evaluate a wave's team conditions; if both pass, run the entity scan.
// (a1/a2 are the pass-through desc/key words from the caller; the descriptor is
// re-fetched from the register-pair stub.)
void objective_eval_wave_conditions(int /*a1*/, int /*a2*/, int slot)
{
    unsigned long long pair = FUN_803826f0();
    int desc = (int)(unsigned)(pair >> 0x20);
    if (((*(int *)(desc + 8) == 0) || objective_cond_team0_clear((int)pair, slot)) &&
        ((*(int *)(desc + 0xc) == 0) || objective_cond_team1_clear((int)pair, slot))) {
        objective_scan_entities_flag();
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// Evaluate one trigger descriptor. If its mode flag is 0, try up to 3 fallback
// condition callbacks (PTR_FUN_804821b8) and return the first non-zero result;
// otherwise run the full wave-condition evaluation.
int objective_eval_trigger(void)
{
    unsigned long long pair = FUN_803826e8();
    u32 key = (u32)(unsigned)(pair >> 0x20);
    int slot = (int)pair;
    int variant = spawn_mode_variant();
    int desc = *(int *)(*(int *)(DAT_8061208c + 0x144) + variant * 4) + slot * 0x18;
    int result = 0;
    if (*(int *)(desc + 0x10) == 0) {
        int i = 0;
        int cbOff = 0;
        do {
            int r = (*(int (**)(u32,int))((int)&PTR_FUN_804821b8 + cbOff))(key, slot);
            if (r != 0) { result = r; break; }
            i = i + 1;
            cbOff = cbOff + 8;
        } while (i < 3);
    } else {
        objective_eval_wave_conditions(desc, (int)key, slot);
    }
    FUN_80382734();
    return result;
}

// ---------------------------------------------------------------------------
// Activate a trigger: mark its descriptor active, raise the objective score
// floor from the key's field5, and OR the result bitmask (DAT_805015c4) with
// the activation bit plus the key's field6 flags.
void objective_activate_trigger(void)
{
    unsigned long long pair = FUN_803826e8();
    int key = (int)(unsigned)(pair >> 0x20);
    int field5 = match_desc_field5(key);
    int scoreFloor = *(int *)(*(int *)(DAT_8061208c + 0x14c) + 0xc);
    int variant = spawn_mode_variant();
    *(u32 *)(*(int *)(*(int *)(DAT_8061208c + 0x144) + variant * 4) + (int)pair * 0x18) = 1;
    if (scoreFloor < field5) {
        scoreFloor = field5;
    }
    *(int *)(*(int *)(DAT_8061208c + 0x14c) + 0xc) = scoreFloor;
    DAT_805015c4 = DAT_805015c4 | 0x1000000;
    u32 field6 = match_desc_field6(key);
    DAT_805015c4 = DAT_805015c4 | field6;
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// Per-frame trigger tick: run "reset" vtable calls on pool entities carrying
// flag bits 0x3c and 0x20, then evaluate triggers 0..3 and activate any that
// are pending (+0x00 clear, +0x04 set) and pass evaluation.
void objective_tick_triggers(void)
{
    __save_gpr_28();
    int i = 0;
    int poolOff = 0;
    while (i < DAT_805c7c0c) {
        if (((*(byte *)(DAT_805c7c10 + poolOff + 0xb0) & 0x3c) != 0)) {
            int ent = entity_pool_find_type1(i);
            if (((*(byte *)(ent + 0xc) & 0x20) != 0) &&
                (*(int (**)())(ent + 0x10) != nullptr)) {
                (*(int (**)())(ent + 0x10))();
            }
        }
        i = i + 1;
        poolOff = poolOff + 0xb4;
    }
    for (int t = 2; t < 6; t = t + 1) {
        int variant = spawn_mode_variant();
        int *pDesc = (int *)(*(int *)(DAT_805c7c24 + variant * 4) + (t + -2) * 0x18);
        if ((*pDesc == 0) && (pDesc[1] != 0) && (objective_eval_trigger() != 0)) {
            objective_activate_trigger();
        }
    }
    __restore_gpr_28();
}
