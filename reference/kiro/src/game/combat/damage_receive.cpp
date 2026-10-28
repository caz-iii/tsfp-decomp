// =============================================================================
// damage_receive.cpp — Entity damage receive handler and death timer
// TimeSplitters: Future Perfect (GameCube)
// Batch 193 | Date: 2026-08-11
// =============================================================================

#include "common.h"
#include "damage.h"
#include "entity.h"
#include "sound.h"

// =============================================================================
// Address: 0x80090f7c | Name: damage_entity_receive
// MASSIVE entity damage receive handler (10 xrefs — called as a callback from
// damage_dispatch_to_entity for every entity that takes damage).
//
// Flow:
// 1. Validates entity can receive damage (flag 0x100000 + weapon type check)
// 2. Records attacker ID at entity+0x210
// 3. Self-damage position check (flag 0x4, same position = skip)
// 4. Armor mode handling (flag 0x2):
//    - If armor flag active and entity has health: absorb and return
//    - If armor depleted: transition to vulnerable (flag 0x2->0x4), trigger
//      death stagger animation
// 5. Normal damage application:
//    - Weapon type 0x16 (fire): disable render bit, register fade-out
//    - Weapon type 0x17 (poison): if flag 0x4 and cooldown active, skip;
//      else apply reduced damage (divided by armor_divisor if flag set)
//    - Default: subtract damage from entity health (offset 0x1CC)
// 6. Post-damage checks:
//    - If health <= 0 and has physics: trigger entity_start_falling
//    - If health <= 0 and no death anim (flag 0x2000): hide entity
//    - Death callback: invoke entity+0x158 with death reason
//    - Respawn check: if entity has respawn data (flag 0x200 on type),
//      restore health from max_health and keep render active
// =============================================================================
void damage_entity_receive(void* unused1, float param_2, float param_3,
                           float param_4, float param_5, float param_6,
                           float param_7, void* param_8, int* damage_ctx)
{
    int entity = *damage_ctx;
    unsigned int entity_flags = *(unsigned int *)(entity + 0x9C);
    int weapon_type = damage_ctx[6];

    // Invulnerability check: flag 0x100000 blocks damage unless weapon is melee (0x17)
    if ((entity_flags & 0x100000) != 0 && weapon_type != 0x17) {
        unsigned int weapon_flags = get_weapon_class_flags(weapon_type);
        if ((weapon_flags & 0x800) == 0) {
            return;
        }
    }

    // Record attacker entity ID
    *(int *)(entity + 0x210) = damage_ctx[1];

    // Self-damage position check (flag 0x4 = has collision volume)
    if ((entity_flags & 0x4) != 0) {
        float* hit_pos = (float *)damage_ctx[10];
        if (hit_pos != NULL) {
            float ex = *(float *)(entity + 0x64) + *(float *)(entity + 0x1A8);
            float ey = *(float *)(entity + 0x68) + *(float *)(entity + 0x1AC);
            float ez = *(float *)(entity + 0x6C) + *(float *)(entity + 0x1B0);
            if (hit_pos[0] == ex && hit_pos[1] == ey && hit_pos[2] == ez) {
                return;  // Hit position matches entity center = self-damage, skip
            }
        }
    }

    // Armor mode handling (flag 0x2)
    if ((entity_flags & 0x2) != 0) {
        unsigned int type_flags = *(unsigned int *)(*(int *)(entity + 4) + 0x30);
        if ((type_flags & 0x2) != 0) {
            // Armor still active
            if (*(int *)(entity + 0x168) != 0) {
                // Has health remaining in armor mode
                if (*(float *)(entity + 0x1CC) > 0.0f) {
                    return;  // Absorb hit
                }
                // Armor depleted: transition to vulnerable
                *(unsigned int *)(*(int *)(entity + 4) + 0x30) = type_flags & ~0x2;
                *(unsigned int *)(*(int *)(entity + 4) + 0x30) |= 0x4;
                *(unsigned int *)(entity + 0x18) |= 0x2;
                if ((*(unsigned int *)(entity + 0x28) & 0x10000) != 0) {
                    trigger_armor_break_effect();
                }
                return;
            }
            // No armor health data: play death sound, register death callback
            if (*(int *)(entity + 0x228) != 0) {
                sound_stop_handle();
            }
            *(int *)(entity + 0x228) = 0;
            *(unsigned int *)(entity + 0x28) |= 0x4000000;
            entity_register_for_update(entity, 5, FUN_death_fade_callback);
            trigger_death_effect();
            goto post_damage;
        }
    }

    // Normal damage path
    if (weapon_type == 0x16 && *(int *)(entity + 0x168) != 0) {
        // Fire damage: disable render, register fade-out
        *(unsigned int *)(entity + 0x28) &= ~0x4000000;
        entity_register_for_update(entity, 5, 0);
    } else if (weapon_type == 0x17 && (entity_flags & 0x4) != 0) {
        // Poison damage with cooldown
        if (*(int *)(entity + 0x1A4) > 0) {
            return;  // Still in cooldown
        }
        if (*(int *)(entity + 0x160) == 0) {
            // Start cooldown timer
            *(int *)(entity + 0x1A4) = (int)g_frame_rate >> 3;
            entity_register_for_update(entity, 3, FUN_poison_tick_callback);
            *(unsigned int *)(entity + 0x28) |= 0x4000000;
        }
        // Apply reduced damage if armored type
        if ((*(unsigned int *)(*(int *)(entity + 4) + 0x30) & 0x4) == 0) {
            *(float *)(entity + 0x1CC) -= (float)damage_ctx[8];
        } else {
            *(float *)(entity + 0x1CC) -= (float)damage_ctx[8] / g_armor_divisor;
        }
    } else {
        // Standard damage subtraction
        *(float *)(entity + 0x1CC) -= (float)damage_ctx[8];
    }

post_damage:
    // Cooldown guard
    if (*(int *)(entity + 0x1A4) > 0) {
        return;
    }

    // Notify AI/script of damage
    if (*(int *)(entity + 0x80) != 0) {
        notify_ai_damaged(entity);
    }

    float health = *(float *)(entity + 0x1CC);

    // Death transitions
    if (health <= 0.0f) {
        unsigned int type_flags = *(unsigned int *)(*(int *)(entity + 4) + 0x30);
        if ((type_flags & 0x2) != 0) {
            // Armor-to-death transition
            *(unsigned int *)(*(int *)(entity + 4) + 0x30) = type_flags & ~0x2;
            *(unsigned int *)(*(int *)(entity + 4) + 0x30) |= 0x4;
            *(unsigned int *)(entity + 0x18) |= 0x2;
            if ((*(unsigned int *)(entity + 0x28) & 0x10000) != 0) {
                trigger_armor_break_effect();
            }
            trigger_death_sequence();
            dispatch_death_awards(entity, 1, (int)(float)damage_ctx[8]);
            trigger_death_particles();
            trigger_death_effect();
            *(float *)(entity + 0x1CC) = 0.0f;
            goto final_checks;
        }
    }

    // Physics-based death (entity_start_falling for ragdoll)
    if ((entity_flags & 0x1000) != 0 && health <= 0.0f) {
        *(float *)(entity + 0x1CC) = 0.0f;
        entity_start_falling(g_gravity_default, entity);
    }

    // Silent death (no death anim, flag 0x2000 absent)
    if ((entity_flags & 0x2000) == 0 && health <= 0.0f &&
        *(int *)(entity + 0x168) == 0) {
        *(float *)(entity + 0x1CC) = 0.0f;
        entity_set_render_type(entity, 0);
    }

final_checks:
    if (*(float *)(entity + 0x1CC) <= 0.0f) {
        // Check for respawn capability
        if (*(int *)(entity + 0x80) != 0) {
            unsigned int respawn_flags = get_entity_type_flags(*(int *)(entity + 0x80));
            if ((respawn_flags & 0x200) != 0) {
                // Respawn: restore to max health
                entity_set_render_type(entity, FUN_respawn_render);
                *(float *)(entity + 0x1CC) = *(float *)(entity + 0x1DC);
                return;
            }
        }
        // Death callback
        if (*(void **)(entity + 0x158) != NULL) {
            (*(void (**)(int, int))(entity + 0x158))(entity, 0);
        }
        entity_set_render_type(entity, 0);
    }
}

// =============================================================================
// Address: 0x8009137c | Name: damage_death_timer_tick
// Per-frame death timer countdown for entities in dying state (flag 0x10 in
// entity+0x28). Subtracts frame damage, and when health reaches zero:
// 1. Clears animation state bits (death anim phases) from type flags
// 2. Sets the "dead" completion bit
// 3. If entity had a weapon equipped (switch system), unequips it
// 4. Triggers death sequence via FUN_800922bc
// 5. Deregisters from update lists, hides entity, calls death effect
// 6. Invokes death callback (entity+0x158) if present
// =============================================================================
void damage_death_timer_tick(void)
{
    int* stack_ctx = get_stack_context();
    int entity = *stack_ctx;
    int char_data = *(int *)(entity + 0x7C);
    float health;

    if ((*(unsigned int *)(entity + 0x28) & 0x10) == 0) {
        goto cleanup;
    }

    // Decrement health by per-frame damage amount
    *(float *)(entity + 0x1CC) -= (float)stack_ctx[8];

    if (*(float *)(entity + 0x1CC) <= 0.0f) {
        unsigned int clear_bits = 0;
        unsigned int set_bits = 0;

        // Get animation phase table for this entity's type
        int* anim_phases = get_anim_phase_table((unsigned short)(*(short *)(entity + 0x94)));
        int* type_data = *(int **)(*(int *)(entity + 4) + 4);
        int base_idx = *type_data;

        // Compute flag bits to clear (death anim phases 1,2,3) and set (phase 4 = dead)
        if (anim_phases[3] >= 0) {
            clear_bits = type_data[base_idx * -0x30 + anim_phases[3] * 0x30 + 0x14];
        }
        if (anim_phases[4] >= 0) {
            set_bits = type_data[base_idx * -0x30 + anim_phases[4] * 0x30 + 0x14];
        }

        // Clear death-in-progress bits, set death-complete bit
        unsigned int all_clear = type_data[base_idx * -0x30 + anim_phases[2] * 0x30 + 0x14] |
                                 clear_bits |
                                 type_data[base_idx * -0x30 + anim_phases[1] * 0x30 + 0x14];
        *(unsigned int *)(*(int *)(entity + 4) + 0x30) &= ~all_clear;
        *(unsigned int *)(*(int *)(entity + 4) + 0x30) |= set_bits;

        // Clear "dying" flag
        *(unsigned int *)(entity + 0x28) &= ~0x4;

        // If entity had equipped weapon via switch system, unequip
        if (*(int *)(char_data + 0x24) != 0 &&
            *(int *)(char_data + 0x1C) != 0 &&
            *(int *)(*(int *)(*(int *)(char_data + 0x1C) + 0x150) + 0x114) == 0x80) {
            switch_item_unequip();
            switch_set_activation_state(char_data);
        }

        // Finalize death
        trigger_death_sequence();  // FUN_800922bc
        entity_register_for_update(entity, 0, 0);
        entity_set_render_type(entity, 0);
        *(float *)(entity + 0x1CC) = 0.0f;
        trigger_death_effect();  // FUN_800ea074
    }

    // Death callback (even if timer not expired, check post-death)
    if (*(void **)(entity + 0x158) != NULL) {
        if (*(float *)(entity + 0x1CC) <= 0.0f) {
            (*(void (**)(int, int))(entity + 0x158))(entity, 0);
        }
    }

cleanup:
    restore_stack_frame();
}
