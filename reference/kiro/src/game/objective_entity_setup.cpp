// Address: 0x80351fe4 | Name: objective_setup_mirror_render | Date: 2026-09-12
// Batch: batch_1005
//
// Objective entity setup by class id. Continues the objective marker / wave /
// trigger / tick / visibility subsystem. objective_setup_entity_by_class is
// dispatched from a class table (data ref at 0x8044ec78) and configures each
// spawned objective entity (render type, timers, visibility, powerup spawn)
// according to its class id (short at entity +0x94).
//
// Entity fields used: +0x04 render node (mask at +0x30), +0x28 render flags,
// +0x7c owner/state record, +0x88 linked-object handle, +0x94 class id,
// +0x9c behaviour flags, +0x1cc/+0x1d8/+0x1dc lifetime timers.

#include "common.h"

// ---- raw symbols ----
extern char  DAT_805015bd;      // game-mode byte
extern int   DAT_80611c24;      // linked-object pool base (0x2e8 stride)
extern int   DAT_80612614;      // current level id
extern int   DAT_80612618;      // objective sub-mode selector
extern int   DAT_80612be4;      // secondary objective context
extern float FLOAT_8061ad00;    // 0.0f
extern float FLOAT_8061ad3c;    // pickup sound range
extern float FLOAT_8061ad40;    // pickup sound volume
extern float FLOAT_8061ad44;    // default lifetime
extern float FLOAT_8061ad48;    // secondary lifetime scale
extern float FLOAT_8061ad4c;    // effect param

// ---- render-type callback symbols (passed by address) ----
extern int damage_death_timer_tick;
extern int weapon_impact_smoke_dispatch;
extern int damage_character_receive;
extern int muzzle_flash_init;
extern int weapon_smoke_tick;

// ---- named cross-module helpers ----
extern int  mtx4_set_identity(int pMtx);
extern int * objective_find_visibility_desc(unsigned id);
extern u32 * objective_visibility_desc_at(int index);
extern void objective_build_mirror_matrices(int pSrc, int pDesc, int pState, unsigned char *pMtx);
extern void entity_force_bone_update(int pObj);
extern void objective_compute_visible_zone_mask(void);
extern int  entity_get_world_position(int pEntity);
extern int  sound_is_position_in_range(double range, float *pPos);
extern void sound_init_play_params(int pParams);
extern void sound_play_request(int *pParams);
extern void objective_update_task_visibility_mask(void);
extern void entity_set_render_type(int pEntity, int renderCb);
extern void entity_register_for_update(int pEntity, int slot, int cb);
extern void subobj_clear_flag_bits(unsigned handle, unsigned bits);
extern void subobj_set_flag_bits(unsigned handle, unsigned bits);
extern int  level_get_info_cached(int levelId);
extern int  objective_max_score(void);
extern int  objective_find_spawn_record(int index);
extern void spawn_apply_powerup(unsigned long long f1, unsigned long long a2, unsigned long long a3,
                                unsigned long long a4, unsigned long long a5, unsigned long long a6,
                                unsigned long long a7, unsigned long long a8, int entity, int rec);

// Register-pair context stubs.
extern "C" int  FUN_803826f0(void);
extern "C" void FUN_8038273c(void);
extern "C" int  __save_gpr_28(void);
extern "C" void __restore_gpr_28(void);
extern "C" int  __save_gpr_25(void);
extern "C" void __restore_gpr_25(void);

// ---------------------------------------------------------------------------
// Set up the mirrored render matrices for an objective task and, if it has a
// linked object, force its bone update and build its mirror matrices too.
void objective_setup_mirror_render(void)
{
    unsigned char scratch[88];
    int ctx = FUN_803826f0();
    int state = *(int *)(ctx + 0x7c);
    int *desc = objective_find_visibility_desc((unsigned)*(unsigned short *)(ctx + 0x94));
    mtx4_set_identity((int)scratch);
    *(u32 *)(ctx + 0x28) = *(u32 *)(ctx + 0x28) | 0x200000;
    objective_build_mirror_matrices(ctx, (int)desc, state, scratch);

    u32 handle = *(u32 *)(state + 0x88);
    if (handle != 0) {
        int obj = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
        if (*(u32 *)(obj + 0x2c) != handle) {
            obj = 0;
            *(u32 *)(state + 0x88) = 0;
        }
        if (obj != 0) {
            u32 *desc2 = objective_visibility_desc_at(desc[0x12]);
            entity_force_bone_update(obj);
            *(u32 *)(obj + 0x28) = *(u32 *)(obj + 0x28) | 0x200000;
            objective_build_mirror_matrices(obj, (int)desc2, state, scratch);
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// Mark an objective entity (and its linked object) as visible/dirty; recompute
// the visible-zone mask for the linked object.
void objective_mark_visible_pair(int pEntity)
{
    *(u32 *)(pEntity + 0x18) = *(u32 *)(pEntity + 0x18) | 2;
    *(u32 *)(pEntity + 0x28) = *(u32 *)(pEntity + 0x28) | 8;
    u32 handle = *(u32 *)(*(int *)(pEntity + 0x7c) + 0x88);
    if (handle != 0) {
        int obj = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
        if (*(u32 *)(obj + 0x2c) != handle) {
            obj = 0;
            *(u32 *)(*(int *)(pEntity + 0x7c) + 0x88) = 0;
        }
        if (obj != 0) {
            objective_compute_visible_zone_mask();
            *(u32 *)(obj + 0x18) = *(u32 *)(obj + 0x18) | 2;
            *(u32 *)(obj + 0x28) = *(u32 *)(obj + 0x28) | 8;
        }
    }
}

// ---------------------------------------------------------------------------
// Play the objective pickup sound at the entity's position, choosing the sound
// id by the sub-mode selector, when the position is within range.
void objective_play_pickup_sound(int pEntity)
{
    int params[6];
    float volume;
    float rangeParam;
    float posX;
    float posY;
    float posZ;
    unsigned char one;

    float *pPos = (float *)entity_get_world_position(pEntity);
    if (sound_is_position_in_range((double)FLOAT_8061ad3c, pPos)) {
        int soundId = 0xdc2;
        if (DAT_80612618 == 4) {
            soundId = 0xdc0;
        } else if (DAT_80612618 < 4) {
            if (DAT_80612618 == 2) soundId = 0xdbf;
        } else if (DAT_80612618 < 6) {
            soundId = 0xdc1;
        }
        sound_init_play_params((int)params);
        one = 1;
        rangeParam = FLOAT_8061ad3c;
        volume = FLOAT_8061ad40;
        posX = *pPos;
        posY = pPos[1];
        posZ = pPos[2];
        params[0] = soundId;
        (void)one; (void)rangeParam; (void)volume; (void)posX; (void)posY; (void)posZ;
        sound_play_request(params);
    }
}

// ---------------------------------------------------------------------------
// Configure an objective entity for the "death fade" lifetime: set render/
// behaviour flags, seed the fade timer and register the death tick handler.
void objective_setup_death_fade(int pEntity)
{
    objective_update_task_visibility_mask();
    float life = FLOAT_8061ad44;
    *(u32 *)(pEntity + 0x28) = *(u32 *)(pEntity + 0x28) | 0x14;
    *(u32 *)(pEntity + 0x9c) = *(u32 *)(pEntity + 0x9c) | 0x2028;
    *(float *)(pEntity + 0x1d8) = life;
    *(u32 *)(pEntity + 0x1cc) = *(u32 *)(pEntity + 0x1d8);
    entity_set_render_type(pEntity, damage_death_timer_tick);
    entity_register_for_update(pEntity, 2, -0x7fcae5f8);
}

// ---------------------------------------------------------------------------
// Set up dual-team visibility for a special objective entity (level sub-mode
// 0x66) or, otherwise, register it for three deferred update passes.
void objective_setup_dual_visibility(void)
{
    int ctx = __save_gpr_25();
    if (DAT_80612614 == 0x66) {
        u32 maskA = 0;
        u32 maskB = 0;
        int ctxB = *(int *)(DAT_80612be4 + 0x4150);
        int *zonesA = *(int **)(*(int *)(ctx + 4) + 4);
        int *zonesB = *(int **)(*(int *)(ctxB + 4) + 4);
        int *descA = objective_find_visibility_desc((unsigned)*(unsigned short *)(ctx + 0x94));
        u32 *descB = objective_visibility_desc_at(descA[0x12]);
        if (descA[1] != -1) {
            maskA = (u32)zonesA[*zonesA * -0x30 + descA[1] * 0x30 + 0x14];
        }
        if (descB[1] != -1) {
            maskB = (u32)zonesB[*zonesB * -0x30 + descB[1] * 0x30 + 0x14];
        }
        u32 extraB = (u32)zonesB[*zonesB * -0x30 + descB[2] * 0x30 + 0x14];
        *(u32 *)(*(int *)(ctx + 4) + 0x30) =
            maskA | (u32)zonesA[*zonesA * -0x30 + descA[2] * 0x30 + 0x14];
        *(u32 *)(*(int *)(ctxB + 4) + 0x30) = maskB | extraB;
    } else {
        entity_register_for_update(ctx, 3, -0x7fcadf34);
        entity_register_for_update(ctx, 0xd, -0x7fcade90);
        entity_register_for_update(ctx, 0xe, -0x7fcade90);
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// Toggle the objective sub-object display flags (clear 0x800100, set 0x40400000).
void objective_set_subobj_flags(int pEntity)
{
    subobj_clear_flag_bits(*(u32 *)(pEntity + 0x80), 0x800100);
    subobj_set_flag_bits(*(u32 *)(pEntity + 0x80), 0x40400000);
}

// ---------------------------------------------------------------------------
// Return non-zero if the active objective (matching the caller's r4 index-1)
// has a spawn zone whose zone descriptor id matches, across all objective
// spawn records for the current level.
int objective_find_active_spawn_zone(void)
{
    __save_gpr_28();
    int info = level_get_info_cached(DAT_80612614);
    int zoneTable = *(int *)(info + 0x130);
    int maxScore = objective_max_score();
    // r4 arrives holding a pointer to the target zone index (index+1 in [0]).
    int *pTarget = (int *)0;   // supplied via r4 by caller convention
    int target = pTarget ? *pTarget : 0;
    int found = 0;
    if (-1 < target + -1) {
        for (int obj = 0; obj < maxScore; obj = obj + 1) {
            int rec = objective_find_spawn_record(obj + 1);
            int count = *(int *)(rec + 0x50);
            int off = 0;
            if (0 < count) {
                do {
                    if ((int)*(short *)(zoneTable + *(int *)(rec + off + 0x54) * 0xb4 + 0xc) == target + -1) {
                        found = 1;
                        goto done;
                    }
                    off = off + 4;
                    count = count + -1;
                } while (count != 0);
            }
        }
    }
done:
    __restore_gpr_28();
    return found;
}

// ---------------------------------------------------------------------------
// In mode 3, if the entity is neither of two special classes and an active
// spawn zone exists, apply the objective sub-object flags.
void objective_maybe_flag_subobj(int pEntity)
{
    if ((*(int *)(pEntity + 0x20) != 0x400000) && (*(int *)(pEntity + 0x20) != 0x10000000) &&
        (DAT_805015bd == '\x03') && (objective_find_active_spawn_zone() != 0)) {
        objective_set_subobj_flags(pEntity);
    }
}

// ---------------------------------------------------------------------------
// Class-id dispatch entry (referenced from a data table at 0x8044ec78). Sets up
// an objective entity based on its class id (+0x94): render type, lifetime
// timers, visibility mask, and, when it carries a spawn record, a powerup spawn.
void objective_setup_entity_by_class(unsigned long long a2, unsigned long long a3,
                                     unsigned long long a4, unsigned long long a5,
                                     unsigned long long a6, unsigned long long a7,
                                     unsigned long long a8)
{
    unsigned long long pair = FUN_803826f0();
    float life = FLOAT_8061ad44;
    int entity = (int)(unsigned)(pair >> 0x20);
    int rec = (int)pair;
    unsigned long long f1 = 0;
    unsigned short cls = *(unsigned short *)(entity + 0x94);

    if (cls < 0x8ae) {
        if ((0x88b < cls) && (0x8a7 < cls)) {
            *(u32 *)(entity + 0x28) = *(u32 *)(entity + 0x28) | 0x10;
            *(float *)(entity + 0x1d8) = life;
            *(u32 *)(entity + 0x1cc) = *(u32 *)(entity + 0x1d8);
            *(u32 *)(*(int *)(entity + 4) + 0x30) = 1;
            if (rec != 0) {
                *(u32 *)(rec + 0x50) = 8;
                *(u32 *)(rec + 0x54) = 0xffffffff;
                *(u32 *)(rec + 0x58) = 0x200f00;
                spawn_apply_powerup(f1, a2, a3, a4, a5, a6, a7, a8, entity, rec);
            }
            entity_set_render_type(entity, weapon_impact_smoke_dispatch);
        }
    } else if (cls < 0x8ba) {
        if (cls < 0x8b4) {
            u32 extra = 0;
            *(u32 *)(entity + 0x28) = *(u32 *)(entity + 0x28) | 0x10;
            *(float *)(entity + 0x1d8) = life;
            *(u32 *)(entity + 0x1cc) = *(u32 *)(entity + 0x1d8);
            float scale = FLOAT_8061ad48;
            if ((cls == 0x8b0) || (cls == 0x8b1)) {
                *(u32 *)(*(int *)(entity + 4) + 0x30) = 2;
                *(float *)(entity + 0x1dc) = scale * *(float *)(entity + 0x1d8);
                entity_set_render_type(entity, damage_character_receive);
            } else {
                extra = 0x200;
                *(u32 *)(*(int *)(entity + 4) + 0x30) = 1;
                *(u32 *)(entity + 0x1dc) = *(u32 *)(entity + 0x1d8);
            }
            *(u32 *)(entity + 0x1dc) = *(u32 *)(entity + 0x1d8);
            *(u32 *)(entity + 0x9c) = *(u32 *)(entity + 0x9c) | 0x40;
            if (rec != 0) {
                if (cls == 0x8af) *(u32 *)(rec + 0x50) = 1;
                if (cls == 0x8b2) *(u32 *)(rec + 0x50) = 7;
                else *(u32 *)(rec + 0x50) = 7;
                *(u32 *)(rec + 0x54) = 0xffffffff;
                *(u32 *)(rec + 0x58) = extra | 0x200d00;
                spawn_apply_powerup(f1, a2, a3, a4, a5, a6, a7, a8, entity, rec);
            }
        } else {
            if ((cls == 0x8b6) || (cls == 0x8b7)) {
                *(u32 *)(*(int *)(entity + 4) + 0x30) = 2;
            }
            life = FLOAT_8061ad44;
            *(u32 *)(entity + 0x28) = *(u32 *)(entity + 0x28) | 0x10;
            *(u32 *)(entity + 0x9c) = *(u32 *)(entity + 0x9c) | 0x1000000;
            *(float *)(entity + 0x1d8) = life;
            *(u32 *)(entity + 0x1cc) = *(u32 *)(entity + 0x1d8);
            if (rec != 0) {
                *(u32 *)(rec + 0x50) = 2;
                *(u32 *)(rec + 0x54) = 0xffffffff;
                *(u32 *)(rec + 0x58) = 0x200d00;
                spawn_apply_powerup(f1, a2, a3, a4, a5, a6, a7, a8, entity, rec);
            }
            entity_set_render_type(entity, muzzle_flash_init);
            entity_register_for_update(entity, 1, -0x7ff7fb7c);
        }
    } else if (cls < 0x8c0) {
        u32 extra = 0;
        *(u32 *)(entity + 0x28) = *(u32 *)(entity + 0x28) | 0x10;
        *(float *)(entity + 0x1d8) = life;
        *(u32 *)(entity + 0x1cc) = *(u32 *)(entity + 0x1d8);
        *(u32 *)(entity + 0x9c) = *(u32 *)(entity + 0x9c) | 0x400;
        float scale = FLOAT_8061ad48;
        if ((cls == 0x8bc) || (cls == 0x8bd)) {
            *(u32 *)(*(int *)(entity + 4) + 0x30) = 2;
            *(float *)(entity + 0x1dc) = scale * *(float *)(entity + 0x1d8);
            entity_set_render_type(entity, damage_character_receive);
        } else {
            extra = 0x200;
            *(u32 *)(*(int *)(entity + 4) + 0x30) = 1;
            *(u32 *)(entity + 0x1dc) = *(u32 *)(entity + 0x1d8);
        }
        if (rec != 0) {
            *(u32 *)(rec + 0x50) = 4;
            *(u32 *)(rec + 0x54) = 0xffffffff;
            *(u32 *)(rec + 0x58) = extra | 0x200d00;
            spawn_apply_powerup(f1, a2, a3, a4, a5, a6, a7, a8, entity, rec);
        }
    }
    if (DAT_80612614 == 100) {
        objective_maybe_flag_subobj(entity);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// Set up a "smoke" objective entity (classes 0x8d2..0x8d7): render type, fade
// timers, visibility, and effect parameters seeded on the linked state record.
void objective_setup_smoke_entity(int pEntity)
{
    float life = FLOAT_8061ad44;
    int state = *(int *)(pEntity + 0x7c);
    *(u32 *)(*(int *)(pEntity + 4) + 0x30) = 1;
    *(u32 *)(pEntity + 0x28) = *(u32 *)(pEntity + 0x28) | 0x10;
    *(float *)(pEntity + 0x1d8) = life;
    *(u32 *)(pEntity + 0x1cc) = *(u32 *)(pEntity + 0x1d8);
    *(u32 *)(pEntity + 0x1dc) = *(u32 *)(pEntity + 0x1d8);
    entity_set_render_type(pEntity, weapon_smoke_tick);
    float zero = FLOAT_8061ad00;
    unsigned short cls = *(unsigned short *)(pEntity + 0x94);
    if (cls == 0x8d6) {
        if (state != 0) {
            *(float *)(state + 0x10) = FLOAT_8061ad4c;
            *(float *)(state + 0x9c) = zero;
            *(u32 *)(state + 0xa0) = 0x19;
            *(u32 *)(state + 0xbc) = 0;
        }
    } else {
        if (cls < 0x8d6) {
            if (cls < 0x8d2) return;
        } else if (0x8d7 < cls) {
            return;
        }
        if (state != 0) {
            *(float *)(state + 0x10) = FLOAT_8061ad4c;
            *(float *)(state + 0x9c) = zero;
            *(u32 *)(state + 0xa0) = 0x1e;
            *(u32 *)(state + 0xbc) = 0;
        }
    }
}
