// =============================================================================
// Character Death Effects - TSFP Decompilation
// Address Range: 0x800f82d0 - 0x800f8c9c
// Batch: 311 | Date: 2026-08-25
// =============================================================================

#include "common.h"
#include "character.h"
#include "particle.h"
#include "collision.h"
#include "entity.h"
#include "projectile.h"

// Forward declarations
extern float FLOAT_806153b0;   // Duration/max time constant
extern float FLOAT_806108ec;   // Delta time multiplier
extern float FLOAT_806153c0;   // Zero float constant
extern float FLOAT_80615400;   // Fadeout duration
extern float FLOAT_80615444;   // Collision scale base
extern float FLOAT_80615450;   // Radius growth factor
extern float FLOAT_806154d0;   // Particle count scale
extern float FLOAT_806154d4;   // Collision radius scale
extern float FLOAT_80615468;   // Particle Y scale
extern double DOUBLE_806153d0; // Double offset for int-to-float conversion
extern int DAT_806109c0;       // Frame count constant
extern int DAT_80612614;       // Difficulty/mode setting
extern int DAT_806108e4;       // Hazard zone active counter

// External functions
extern void* FUN_803826d0(void);
extern void FUN_8038271c(void);
extern unsigned long long FUN_803826e8(void);
extern unsigned long long FUN_803826ec(void);
extern void FUN_80382734(void);
extern void FUN_80382738(void);
extern int FUN_803825c0(double);
extern void FUN_800fd834(void* param);
extern void FUN_8012f62c(double, double, double);
extern void FUN_80130bd4(float*, float*, float*);
extern void FUN_80130b90(float*, float*);

extern void character_death_timer_tick(int entity);
extern void character_death_dissolve_tick(void);
extern void character_teleport_effect_tick(void* param);
extern void character_death_anim_check(void* param);
extern void character_bone_blend_tick(void);
extern void hazard_zone_damage_tick(void);
extern int projectile_spawn_impact_entity(double, double, double, double, double, double, double, double, int);
extern void entity_destroy_with_event(void* entity);
extern void* particle_get_current_player_emitter(void);
extern int particle_handle_is_valid(int* handle);
extern void particle_cached_set_position(int* handle, void* position);
extern void collision_set_radius_and_flags(double radius, int* shape, int a, int b, int c);
extern void collision_set_shape_params(int* shape, int type, int mask, int mode, char param);

// =============================================================================
// Address: 0x800f82d0 | Name: character_death_effect_dispatch
// Dispatches character death effect subtypes based on state index.
// States: 3=dissolve, 5=teleport, 6=timer, 7=anim_check, 8=FUN_800fd834
// =============================================================================
void character_death_effect_dispatch(u32* param_1) {
    int state;

    if ((param_1[1] & 1) == 0) {
        return;
    }

    // Accumulate time: timer += delta_time * (max_time / frame_count)
    param_1[0x17] = (u32)((float)param_1[0x17] +
        FLOAT_806108ec * (FLOAT_806153b0 / (float)((double)(0x43300000 | (DAT_806109c0 ^ 0x80000000)) - DOUBLE_806153d0)));

    state = (int)param_1[0x15];

    if (state == 6) {
        character_death_timer_tick((int)param_1);
    } else if (state < 6) {
        if (state == 4) {
            // State 4: no-op (fall through)
        } else if (state < 4) {
            if (state > 2) {
                character_death_dissolve_tick();
            }
        } else {
            // State 5: teleport effect
            character_teleport_effect_tick(param_1);
        }
    } else if (state == 8) {
        FUN_800fd834(param_1);
    } else if (state < 8) {
        // State 7: anim check
        character_death_anim_check(param_1);
    }
}

// =============================================================================
// Address: 0x800f838c | Name: character_death_effect_noop
// Empty stub for unused death effect subtype slot.
// =============================================================================
void character_death_effect_noop(void) {
    return;
}

// =============================================================================
// Address: 0x800f8390 | Name: character_death_effect_update_particles
// Updates projectile impact entities and particle emitter parameters during
// character death. Handles both growing (flag 0x4000 clear) and shrinking
// (flag 0x4000 set) phases. Spawns impact entities if not yet created.
// =============================================================================
void character_death_effect_update_particles(void) {
    u32* puVar2;
    int iVar3;
    int iVar12;
    int iVar13;
    float fVar1;
    double dVar14, dVar15, dVar16, dVar17, dVar18;
    u32* puVar4;
    u32 uVar5, uVar6, uVar7, uVar8, uVar9, uVar10, uVar11;

    puVar2 = (u32*)FUN_803826d0();
    iVar12 = 0;
    dVar15 = (double)FLOAT_806153b0;
    dVar16 = (double)FLOAT_806108ec;
    dVar14 = (double)(float)puVar2[0x33];
    iVar13 = *(int*)*puVar2;

    // Accumulate death timer
    puVar2[0x33] = (u32)(float)(dVar14 + dVar16 * (dVar15 / (double)(float)(
        (double)(0x43300000 | (DAT_806109c0 ^ 0x80000000)) - DOUBLE_806153d0)));

    // Spawn impact entity for primary if not yet existing
    if (*(int*)(iVar13 + 0x208) == 0) {
        iVar3 = projectile_spawn_impact_entity(dVar14, dVar15, dVar16, 0, 0, 0, 0, 0, iVar13);
        *(int*)(iVar13 + 0x208) = iVar3;
    }

    // Spawn impact entity for linked secondary character
    if (*(int*)(iVar13 + 0x150) != 0) {
        iVar12 = *(int*)(*(int*)(iVar13 + 0x150) + 0xd1c);
        if (iVar12 != 0 && *(int*)(iVar12 + 4) != 0 && *(int*)(iVar12 + 0x208) == 0) {
            iVar3 = projectile_spawn_impact_entity(dVar14, dVar15, dVar16, 0, 0, 0, 0, 0, iVar12);
            *(int*)(iVar12 + 0x208) = iVar3;
        }
    }

    // Update particle emitter if character has valid link
    if (*(int*)(iVar13 + 0x150) != 0 && *(int*)(*(int*)(iVar13 + 0x150) + 4) >= 0) {
        // Default particle counts
        uVar5 = 0x6a;
        uVar11 = 0x50;
        uVar10 = 0;
        uVar9 = 0;
        uVar8 = 0;
        uVar7 = 0;
        uVar6 = 0;

        // Override for difficulty/mode 0x35
        if (DAT_80612614 == 0x35) {
            uVar5 = 0x14;
            uVar11 = 0x14;
            uVar10 = 100;
            uVar9 = 0x3c;
            uVar8 = 0x1e;
            uVar7 = 0x40;
            uVar6 = 0x1e;
        }

        if ((puVar2[1] & 0x4000) == 0) {
            // Growing phase: interpolate from current timer to max
            fVar1 = (float)puVar2[0x33];
            if (FLOAT_806153b0 < fVar1) {
                fVar1 = FLOAT_806153b0;
            }
            dVar18 = (double)(FLOAT_806153b0 - fVar1);

            puVar4 = (u32*)particle_get_current_player_emitter();
            puVar4[0xb] = (u32)(float)(FLOAT_806154d0 * dVar18 + (float)uVar5);

            puVar4 = (u32*)particle_get_current_player_emitter();
            puVar4[0xc] = (u32)(float)(FLOAT_80615468 * dVar18 + (float)uVar11);

            puVar4 = (u32*)particle_get_current_player_emitter();
            puVar4[0xd] = (u32)(float)(FLOAT_806153c0 * dVar18 + 0.0f);

            puVar4 = (u32*)particle_get_current_player_emitter();
            puVar4[7] = uVar10;

            puVar4 = (u32*)particle_get_current_player_emitter();
            puVar4[8] = uVar9;

            puVar4 = (u32*)particle_get_current_player_emitter();
            puVar4[9] = uVar8;

            puVar4 = (u32*)particle_get_current_player_emitter();
            puVar4[10] = (u32)(float)((float)uVar6 * dVar18 + (float)uVar7);
        } else {
            // Shrinking/fadeout phase: blend between current and target
            dVar16 = (double)(FLOAT_806153b0 - (float)puVar2[0x33] / FLOAT_80615400);
            dVar17 = (double)(float)(FLOAT_806153b0 - (float)dVar16);

            puVar4 = (u32*)particle_get_current_player_emitter();
            puVar4[0xb] = (u32)(float)((float)uVar5 * (float)dVar16 + (float)puVar4[0xb] * (float)dVar17);

            puVar4 = (u32*)particle_get_current_player_emitter();
            puVar4[0xc] = (u32)(float)((float)uVar11 * (float)dVar16 + (float)puVar4[0xc] * (float)dVar17);

            puVar4 = (u32*)particle_get_current_player_emitter();
            puVar4[0xd] = (u32)(float)(0.0f * (float)dVar16 + (float)puVar4[0xd] * (float)dVar17);

            puVar4 = (u32*)particle_get_current_player_emitter();
            puVar4[7] = uVar10;

            puVar4 = (u32*)particle_get_current_player_emitter();
            puVar4[8] = uVar9;

            puVar4 = (u32*)particle_get_current_player_emitter();
            puVar4[9] = uVar8;

            puVar4 = (u32*)particle_get_current_player_emitter();
            puVar4[10] = (u32)(float)((float)uVar7 * (float)dVar16 + (float)puVar4[10] * (float)dVar17);
        }
    }

    // If fadeout complete, clean up
    if ((puVar2[1] & 0x4000) != 0 && (float)puVar2[0x33] >= FLOAT_80615400) {
        puVar2[1] = puVar2[1] & 0xffff9fff;

        if (*(void**)(iVar13 + 0x208) != NULL) {
            entity_destroy_with_event(*(void**)(iVar13 + 0x208));
            *(int*)(iVar13 + 0x208) = 0;
        }
        if (iVar12 != 0 && *(void**)(iVar12 + 0x208) != NULL) {
            entity_destroy_with_event(*(void**)(iVar12 + 0x208));
            *(int*)(iVar12 + 0x208) = 0;
        }
    }

    FUN_8038271c();
}

// =============================================================================
// Address: 0x800f87f8 | Name: character_death_effect_reset
// Resets death effect state: clears flags, resets timer, destroys any active
// impact entities for both primary and linked secondary character.
// =============================================================================
void character_death_effect_reset(u32* param_1) {
    int iVar2;
    float fVar1 = FLOAT_806153c0;  // 0.0f

    if ((param_1[1] & 0x2000) == 0 || (param_1[1] & 0x4000) != 0) {
        iVar2 = *(int*)*param_1;

        // Clear fadeout flag (0x4000), set death-active flag (0x2000)
        param_1[1] = param_1[1] & ~0x4000;
        param_1[1] = param_1[1] | 0x2000;
        param_1[0x33] = (u32)fVar1;  // Reset timer to 0

        // Destroy primary impact entity
        if (*(void**)(iVar2 + 0x208) != NULL) {
            entity_destroy_with_event(*(void**)(iVar2 + 0x208));
            *(int*)(iVar2 + 0x208) = 0;
        }

        // Destroy secondary (linked character weapon) impact entity
        if (*(int*)(iVar2 + 0x150) != 0) {
            int secondary = *(int*)(*(int*)(iVar2 + 0x150) + 0xd1c);
            if (secondary != 0 && *(void**)(secondary + 0x208) != NULL) {
                entity_destroy_with_event(*(void**)(secondary + 0x208));
                *(int*)(secondary + 0x208) = 0;
            }
        }
    }
}

// =============================================================================
// Address: 0x800f88a0 | Name: character_death_begin_fadeout
// Initiates fadeout phase: sets flag 0x4000 and resets timer to 0.
// Only activates if death-active flag (0x2000) is already set.
// =============================================================================
void character_death_begin_fadeout(int param_1) {
    float fVar1 = FLOAT_806153c0;  // 0.0f

    if ((*(u32*)(param_1 + 4) & 0x2000) == 0) {
        return;
    }

    *(u32*)(param_1 + 4) = *(u32*)(param_1 + 4) | 0x4000;
    *(float*)(param_1 + 0xcc) = fVar1;
}

// =============================================================================
// Address: 0x800f88c0 | Name: character_death_collision_update_secondary
// Updates collision shape radius and bone transforms for a secondary death
// entity (e.g. weapon/attached object). Handles both growing and fadeout phases.
// =============================================================================
void character_death_collision_update_secondary(u32 param_1, u32 param_2, int param_3) {
    float fVar1;
    int iVar2, iVar3, iVar4;
    double dVar5, dVar6, dVar7;
    unsigned long long uVar9;
    float afStack_f8[16];
    float afStack_b8[16];
    float afStack_78[24];

    uVar9 = FUN_803826ec();
    iVar2 = (int)(uVar9 >> 32);
    iVar3 = (int)uVar9;
    dVar7 = (double)FLOAT_806153b0;

    if ((*(u32*)(iVar2 + 4) & 0x4000) == 0) {
        // Growing phase: clamp timer to max
        dVar5 = (double)*(float*)(iVar2 + 0xcc);
        if (dVar7 < dVar5) {
            dVar5 = dVar7;
        }
    } else {
        // Fadeout phase: compute squared falloff
        dVar5 = (double)(*(float*)(iVar2 + 0xcc) / FLOAT_80615400);
        fVar1 = (float)(FLOAT_806153b0 - (float)dVar5);
        dVar7 = (double)(float)(FLOAT_80615450 * (float)dVar5 + FLOAT_806153b0);
        dVar5 = (double)(fVar1 * fVar1);
    }

    // Update collision radius
    collision_set_radius_and_flags(dVar5, *(int**)(iVar3 + 4), 0x7f, 0x46, 0x1e);
    iVar2 = FUN_803825c0((double)(float)(FLOAT_806154d4 * (float)dVar5));
    collision_set_shape_params(*(int**)(param_3 + 4), 0x10, 0xffffffff, 2, (char)iVar2);

    // Copy flags
    *(u8*)(param_3 + 0x31) = *(u8*)(iVar3 + 0x31);

    // Interpolate transforms
    FUN_8012f62c((double)FLOAT_806153b0, (double)FLOAT_80615444, (double)FLOAT_80615444);
    if ((double)FLOAT_806153b0 != dVar7) {
        FUN_8012f62c((double)FLOAT_806153b0, dVar7, (double)FLOAT_806153b0);
    }

    // Update bone matrices if not frozen
    if ((*(u32*)(param_3 + 0x28) & 0x100) == 0) {
        iVar4 = 0;
        int bone_count = *(int*)(*(int*)(*(int*)(param_3 + 4) + 4) + 4);
        for (iVar2 = 0; iVar2 < bone_count; iVar2++) {
            FUN_80130bd4(afStack_78,
                (float*)(*(int*)(*(int*)(iVar3 + 4) + 0x10) + iVar4),
                (float*)(*(int*)(*(int*)(param_3 + 4) + 0x10) + iVar4));
            if ((double)FLOAT_806153b0 != dVar7) {
                FUN_80130bd4((float*)(*(int*)(*(int*)(param_3 + 4) + 0x10) + iVar4),
                    afStack_b8, afStack_f8);
                FUN_80130b90(afStack_f8,
                    (float*)(*(int*)(*(int*)(param_3 + 4) + 0x10) + iVar4));
            }
            iVar4 += 0x40;
        }
    }

    FUN_80382738();
}

// =============================================================================
// Address: 0x800f8a54 | Name: character_death_collision_update_primary
// Updates collision shape and bone transforms for the primary death entity.
// Also propagates collision update to linked secondary entity.
// =============================================================================
void character_death_collision_update_primary(void) {
    float fVar1;
    u32* puVar2;
    int iVar3, iVar4, iVar5, iVar6;
    double dVar7, dVar8;
    unsigned long long uVar9;
    float afStack_f8[16];
    float afStack_b8[16];
    float afStack_78[24];

    uVar9 = FUN_803826e8();
    puVar2 = (u32*)(uVar9 >> 32);
    iVar4 = (int)uVar9;
    dVar8 = (double)FLOAT_806153b0;
    iVar5 = *(int*)*puVar2;

    if ((puVar2[1] & 0x4000) == 0) {
        // Growing phase: clamp timer
        dVar7 = (double)(float)puVar2[0x33];
        if (dVar8 < (double)(float)puVar2[0x33]) {
            dVar7 = dVar8;
        }
    } else {
        // Fadeout: squared falloff
        fVar1 = (float)(FLOAT_806153b0 - (float)puVar2[0x33] / FLOAT_80615400);
        dVar8 = (double)(float)(FLOAT_80615450 * (float)puVar2[0x33] / FLOAT_80615400 + FLOAT_806153b0);
        dVar7 = (double)(fVar1 * fVar1);
    }

    // Update collision
    collision_set_radius_and_flags(dVar7, *(int**)(iVar5 + 4), 0x7f, 0x46, 0x1e);
    iVar3 = FUN_803825c0((double)(float)(FLOAT_806154d4 * (float)dVar7));
    collision_set_shape_params(*(int**)(iVar4 + 4), 0x10, 0xffffffff, 2, (char)iVar3);

    // Interpolate transforms
    FUN_8012f62c((double)FLOAT_806153b0, (double)FLOAT_80615444, (double)FLOAT_80615444);
    if ((double)FLOAT_806153b0 != dVar8) {
        FUN_8012f62c((double)FLOAT_806153b0, dVar8, (double)FLOAT_806153b0);
    }

    // Update bone matrices
    if ((*(u32*)(iVar4 + 0x28) & 0x100) == 0) {
        iVar6 = 0;
        int bone_count = *(int*)(*(int*)(*(int*)(iVar4 + 4) + 4) + 4);
        for (iVar3 = 0; iVar3 < bone_count; iVar3++) {
            FUN_80130bd4(afStack_78,
                (float*)(*(int*)(*(int*)(iVar5 + 4) + 0x10) + iVar6),
                (float*)(*(int*)(*(int*)(iVar4 + 4) + 0x10) + iVar6));
            if ((double)FLOAT_806153b0 != dVar8) {
                FUN_80130bd4((float*)(*(int*)(*(int*)(iVar4 + 4) + 0x10) + iVar6),
                    afStack_b8, afStack_f8);
                FUN_80130b90(afStack_f8,
                    (float*)(*(int*)(*(int*)(iVar4 + 4) + 0x10) + iVar6));
            }
            iVar6 += 0x40;
        }
    }

    // Propagate to linked secondary entity
    if (*(int*)(iVar5 + 0x150) != 0) {
        iVar4 = *(int*)(*(int*)(iVar5 + 0x150) + 0xd1c);
        if (iVar4 != 0 && *(int*)(iVar4 + 0x208) != 0) {
            character_death_collision_update_secondary(
                (u32)puVar2, (u32)iVar4, *(int*)(iVar4 + 0x208));
        }
    }

    FUN_80382734();
}

// =============================================================================
// Address: 0x800f8c14 | Name: character_death_update_check
// Determines which death update to execute: either collision update (if fading)
// or bone blend tick (if in death animation state 5).
// Returns 1 if an update was performed, 0 otherwise.
// =============================================================================
u32 character_death_update_check(int param_1) {
    if ((*(u32*)(param_1 + 4) & 0x2000) != 0) {
        // Death-active: run collision update
        character_death_collision_update_primary();
        return 1;
    }

    if ((*(u32*)(param_1 + 4) & 1) == 0 || *(int*)(param_1 + 0x54) != 5) {
        return 0;
    }

    // In death anim state 5: blend bones
    character_bone_blend_tick();
    return 1;
}

// =============================================================================
// Address: 0x800f8c70 | Name: character_hazard_zone_check
// Checks if hazard zone damage counter is positive, and if so calls the
// hazard zone damage tick handler.
// =============================================================================
void character_hazard_zone_check(void) {
    if (DAT_806108e4 > 0) {
        hazard_zone_damage_tick();
    }
}

// =============================================================================
// Address: 0x800f8c9c | Name: character_update_attached_particles
// Updates positions of up to 4 attached particle handles to match the
// entity's current world position (offset 0x64 in entity struct).
// =============================================================================
void character_update_attached_particles(void) {
    u32* puVar1;
    int iVar3, iVar5;
    int local_28;
    int local_24[9];

    puVar1 = (u32*)FUN_803826e8();
    iVar3 = 0;
    iVar5 = 0;
    int iVar4 = *(int*)*puVar1;

    do {
        local_24[0] = *(int*)((int)puVar1 + iVar5 + 0xbc);
        if (particle_handle_is_valid(local_24)) {
            local_28 = *(int*)((int)puVar1 + iVar5 + 0xbc);
            particle_cached_set_position(&local_28, (void*)(iVar4 + 100));
        }
        iVar3++;
        iVar5 += 4;
    } while (iVar3 < 4);

    FUN_80382734();
}
