// =============================================================================
// death_effects.cpp — Death effects dispatcher, ragdoll, gibs, melee area scan
// TimeSplitters: Future Perfect (GameCube)
// Batch 195 | Date: 2026-08-11
// =============================================================================

#include "common.h"
#include "damage.h"
#include "entity.h"
#include "sound.h"
#include "particle.h"

// =============================================================================
// Address: 0x800922bc | Name: death_effects_dispatch
// MASSIVE death effects dispatcher (10 xrefs — called from every death path).
// Based on entity flags at offset 0x9C, spawns various effects:
//
// Flag checks and effects:
// - 0x4: Blood splatter event + random death sound (0xC33 + rand%6)
// - 0x8: Screen shake / controller rumble
// - 0x10: Particle burst (6 type-0x36 + 4 type-0x35 with random velocity)
// - 0x20: Shell casings type 0 (6-12 random count)
// - 0x40: Shell casings type 1 (8-16 random count)
// - 0x80: Shell casings type 2 (4-8 random count)
// - 0x100: Shell casings type 3 (8-16 random count)
// - 0x200: Shell casings type 4 (8-16 random count)
// - 0x400: Shell casings type 5 (8-16 random count)
// - 0x4000: Shell casings type 9 (8-16 random count)
// - 0x8000: Shell casings type 10 (8-16 random count)
// - 0x10000: Shell casings type 11 (8-16 random count)
// - 0x20000: Shell casings type 12 (8-16 random count)
// - 0x80000: Shell casings type 8 (8-16 random count)
// - 0x1000000: Shell casings type 13 (10-20 random count)
// - 0x2000000: Gib spawner (death_spawn_gibs with 2-5 random count)
// - 0x400000 or has physics: forces "grounded" flag for debris
//
// Finally calls trigger_death_effect and damage_play_impact_sound.
// =============================================================================
void death_effects_dispatch(void)
{
    int entity_ptr = get_current_entity_from_stack();
    unsigned int flags = *(unsigned int *)(entity_ptr + 0x9C);
    unsigned int grounded = 0;

    // Compute entity world position
    float pos_x = *(float *)(entity_ptr + 0x1A8) + *(float *)(entity_ptr + 0x64);
    float pos_y = *(float *)(entity_ptr + 0x1AC) + *(float *)(entity_ptr + 0x68);
    float pos_z = *(float *)(entity_ptr + 0x1B0) + *(float *)(entity_ptr + 0x6C);
    float position[3] = { pos_x, pos_y, pos_z };
    float direction[3] = { 0.0f, 1.0f, 0.0f };

    // Blood splatter + death sound
    if ((flags & 0x4) != 0) {
        if ((flags & 0x200000) == 0 || *(float *)(entity_ptr + 0x1CC) > 0.0f) {
            spawn_blood_event(1, 0x17, position, NULL,
                              *(int *)(entity_ptr + 0x210), entity_ptr, 1);
            unsigned int timer = get_game_timer();
            int sound_params[7];
            sound_init_play_params((int)sound_params);
            sound_params[7] = 1;  // positional
            sound_params[4] = *(int *)&pos_x;
            sound_params[5] = *(int *)&pos_y;
            sound_params[6] = *(int *)&pos_z;
            sound_params[0] = (timer % 6) + 0xC33;  // Random death sound
            sound_play_request(sound_params);
        }
    }

    // Screen shake / rumble
    if ((flags & 0x8) != 0) {
        int player_count = get_active_player_count();
        unsigned int rumble_flags = (player_count == 1) ? 0 : 0x80000;
        spawn_rumble_event(0, 0x15, rumble_flags, position, NULL);
    }

    // Particle burst (10 particles: 6x type 0x36, 4x type 0x35)
    if ((flags & 0x10) != 0) {
        for (int i = 0; i < 10; i++) {
            int player_count = get_active_player_count();
            unsigned int p_flags = (player_count == 1) ? 0 : 0x80000;
            int particle_type = (i < 6) ? 0x36 : 0x35;
            int particle = spawn_particle(0, particle_type, p_flags, position, direction);
            unsigned int timer = get_game_timer();
            float time_factor = (float)((double)timer - g_timer_offset_dbl) / g_time_divisor;
            set_particle_velocity(time_factor, particle);
        }
    }

    // Grounded flag for debris (entity has physics or is flagged)
    if ((flags & 0x400000) != 0 || *(int *)(entity_ptr + 0x168) != 0) {
        grounded = 1;
    }

    // Shell casing debris spawning by entity flag
    float scale = *(float *)(entity_ptr + 0x218);

    if ((flags & 0x20) != 0) {
        unsigned int t = get_game_timer();
        shell_casing_spawn_batch_with_flags(scale, 0, position, direction, (t % 6) + 6, grounded);
    }
    if ((flags & 0x1000000) != 0) {
        unsigned int t = get_game_timer();
        shell_casing_spawn_batch_with_flags(scale, 0xD, position, direction, (t % 10) + 10, grounded);
    }
    if ((flags & 0x40) != 0) {
        unsigned int t = get_game_timer();
        shell_casing_spawn_batch_with_flags(scale, 1, position, direction, (t & 7) + 8, grounded);
    }
    if ((flags & 0x80000) != 0) {
        unsigned int t = get_game_timer();
        shell_casing_spawn_batch_with_flags(scale, 8, position, direction, (t & 7) + 8, grounded);
    }
    if ((flags & 0x80) != 0) {
        unsigned int t = get_game_timer();
        shell_casing_spawn_batch_with_flags(scale, 2, position, direction, (t & 3) + 4, grounded);
    }
    if ((flags & 0x100) != 0) {
        unsigned int t = get_game_timer();
        shell_casing_spawn_batch_with_flags(scale, 3, position, direction, (t & 7) + 8, grounded);
    }
    if ((flags & 0x200) != 0) {
        unsigned int t = get_game_timer();
        shell_casing_spawn_batch_with_flags(scale, 4, position, direction, (t & 7) + 8, grounded);
    }
    if ((flags & 0x400) != 0) {
        unsigned int t = get_game_timer();
        shell_casing_spawn_batch_with_flags(scale, 5, position, direction, (t & 7) + 8, grounded);
    }
    if ((flags & 0x4000) != 0) {
        unsigned int t = get_game_timer();
        shell_casing_spawn_batch_with_flags(scale, 9, position, direction, (t & 7) + 8, grounded);
    }
    if ((flags & 0x8000) != 0) {
        unsigned int t = get_game_timer();
        shell_casing_spawn_batch_with_flags(scale, 10, position, direction, (t & 7) + 8, grounded);
    }
    if ((flags & 0x10000) != 0) {
        unsigned int t = get_game_timer();
        shell_casing_spawn_batch_with_flags(scale, 11, position, direction, (t & 7) + 8, grounded);
    }
    if ((flags & 0x20000) != 0) {
        unsigned int t = get_game_timer();
        shell_casing_spawn_batch_with_flags(scale, 12, position, direction, (t & 7) + 8, grounded);
    }

    // Gib spawner (heavy explosion death)
    if ((flags & 0x2000000) != 0) {
        unsigned int t = get_game_timer();
        death_spawn_gibs(position, direction, 1, (t % 3) + 2);
    }

    // Final effects
    trigger_death_effect();
    damage_play_impact_sound(entity_ptr);

    restore_stack_frame();
}

// =============================================================================
// Address: 0x8009279c | Name: death_start_ragdoll
// Starts ragdoll/falling for a dying entity. Time-scales the fall speed based
// on weapon type (0x17 = melee uses different divisor). Handles attached
// entities (e.g., weapons held by the dying character):
// - If entity has no attacker (param_2==0 or no attached entity): ragdoll self
// - If attached entity exists and param_2 != 0 (fatal hit):
//   * Copies rotation to attached entity
//   * Checks bone count for death anim category selection (4+ = elevated, else ground)
//   * Starts attached entity falling (weapon drops)
// - If entity has flag 0x2000 (no-ragdoll) and param_2 == 0: ragdolls self
// =============================================================================
void death_start_ragdoll(int entity, int is_fatal, int weapon_type)
{
    if ((*(unsigned int *)(entity + 0x9C) & 0x1) == 0 &&
        (*(unsigned int *)(entity + 0x9C) & 0x1000) == 0) {
        return;
    }

    // Compute time-scaled fall speed
    float fall_speed;
    unsigned int timer = get_game_timer();
    float time_norm = (float)((double)timer - g_timer_offset_dbl) / g_time_divisor;

    if (weapon_type == 0x17) {
        // Melee: slower fall (larger divisor)
        fall_speed = 3.0f + time_norm / g_armor_divisor;
    } else {
        // Ranged: faster fall
        fall_speed = 3.0f + time_norm / g_ragdoll_speed_divisor;
    }

    int attached = *(int *)(entity + 0x20C);
    if (attached == 0) {
        // No attached entity: ragdoll self
        entity_start_falling(fall_speed, entity);
    } else if ((*(unsigned int *)(entity + 0x9C) & 0x2000) == 0 && is_fatal != 0) {
        // Has attached entity + fatal hit: drop the weapon
        if (*(short *)(attached + 0x94) != 4) {
            // Copy rotation from dying entity
            *(int *)(attached + 0xAC) = *(int *)(entity + 0xAC);

            // Determine death anim category by bone count
            int bone_count = **(int **)(*(int *)(*(int *)(entity + 0x20C) + 4) + 4);
            if (bone_count >= 4) {
                int is_elevated = entity_is_at_interact_height(entity);
                if (is_elevated != 0) {
                    // Elevated death: set flag 0x10
                    int type_data = *(int *)(*(int *)(entity + 0x20C) + 4);
                    *(unsigned int *)(type_data + 0x30) |= 0x10;
                } else {
                    int is_grounded = entity_is_at_interact_height(entity);
                    if (is_grounded != 0) {
                        int type_data = *(int *)(*(int *)(entity + 0x20C) + 4);
                        *(unsigned int *)(type_data + 0x30) |= 0x8;
                    }
                }
            } else {
                int is_grounded = entity_is_at_interact_height(entity);
                if (is_grounded != 0) {
                    int type_data = *(int *)(*(int *)(entity + 0x20C) + 4);
                    *(unsigned int *)(type_data + 0x30) |= 0x8;
                }
            }

            entity_start_falling(fall_speed, *(int *)(entity + 0x20C));
        }
    } else {
        // No-ragdoll flag or non-fatal: ragdoll self
        entity_start_falling(fall_speed, entity);
    }
}

// =============================================================================
// Address: 0x8009292c | Name: death_spawn_gibs
// Spawns N gib entities at the death position with randomized velocity and
// rotation. Each gib:
// 1. Spawns entity type 0x295 (gib) at position with provided direction
// 2. Allocates animation/physics instance
// 3. Sets randomized angular velocity (scaled by entity bounding box)
// 4. Sets randomized linear velocity (biased outward from death center)
// 5. Registers delayed destroy event (weapon_start_delayed_event type 8)
// =============================================================================
void death_spawn_gibs(float* position, float* direction, int param_3, int gib_count)
{
    // [Large function with paired-single register saves]
    // Key operations:
    // - save_stack_frame (7 PS register pairs)
    // - get_death_context() for entity scale
    // - Compute normalized direction from entity center -> outward
    // - Scale direction by 0.5 * distance factor
    // - Loop gib_count times:
    //   * entity_spawn_with_activation(type=0x295, position, direction)
    //   * Allocate anim instance (FUN_80144864)
    //   * If alloc fails: entity_destroy_with_event and return
    //   * Set anim params (FUN_80145090, mode=9, type=0x16)
    //   * Compute random angular velocity (6x get_game_timer -> normalized rand)
    //   * Scale angular velocity by entity bounding box dimensions
    //   * Compute random linear velocity (3x get_game_timer -> normalized rand)
    //   * Bias linear velocity toward normalized death direction
    //   * Apply via FUN_8014607c
    //   * weapon_start_delayed_event for auto-destroy timer

    // Full implementation deferred to matching pass
}

// =============================================================================
// Address: 0x80092cb0 | Name: damage_melee_area_scan
// Melee damage area scan. Finds nearby entities within melee range and applies
// directional knockback damage (weapon type 0x17):
// 1. Gets attacker context from stack
// 2. Calls spatial query (FUN_8023c59c) to find up to 10 nearby entities
// 3. For each found entity:
//    a. Allocates anim instance if needed (FUN_80144864 + FUN_80145090)
//    b. Sets "being hit" flag (0x10 in entity+0x28)
//    c. Computes direction vector from attacker to victim
//    d. Normalizes direction (fast inverse sqrt)
//    e. Negates direction for knockback
//    f. Computes hit position offset by entity collision radius (offset 0x1C0)
//    g. Builds damage context with weapon type 0x17, time-scaled damage
//    h. Calls damage_dispatch_to_entity
// =============================================================================
void damage_melee_area_scan(void* p1, float p2, float p3, float p4,
                            float p5, float p6, float p7, void* p8,
                            int param_9, int param_10, unsigned int param_11)
{
    // [Large function with paired-single register saves]
    float* attacker_pos = get_attacker_position_from_stack();

    int entity_list[10];
    int count = find_entities_in_melee_range(attacker_pos, entity_list, 10);

    if (count == 0) {
        restore_stack_frame();
        return;
    }

    for (int i = count - 1; i >= 0; i--) {
        int target = entity_list[i];
        if (target == 0) continue;

        // Ensure target has anim instance
        if (*(int *)(target + 0x80) == 0) {
            int has_bones = check_has_bone_data(*(int **)(*(int *)(target + 4) + 4), -1);
            if (has_bones != 0) {
                int anim = alloc_anim_instance();
                *(int *)(target + 0x80) = anim;
                init_anim_instance(anim, target, param_11 & 0xFF, -1, 0.0f);
            }
        }

        // Set "being hit" flag
        *(unsigned int *)(target + 0x28) |= 0x10;

        // Direction: victim - attacker (for knockback)
        float dx = (*(float *)(target + 0x64) + *(float *)(target + 0x1A8)) - attacker_pos[0];
        float dy = (*(float *)(target + 0x68) + *(float *)(target + 0x1AC)) - attacker_pos[1];
        float dz = (*(float *)(target + 0x6C) + *(float *)(target + 0x1B0)) - attacker_pos[2];

        float dist_sq = dx*dx + dy*dy + dz*dz;
        float min_dist = g_melee_min_dist;

        // Normalize if beyond minimum distance
        if (dist_sq > min_dist) {
            float inv_len = fast_inv_sqrt(dist_sq);
            // Newton-Raphson refinement (2 iterations in original)
            dx *= inv_len;
            dy *= inv_len;
            dz *= inv_len;
        }

        // Negate for knockback direction (push away from attacker)
        dx = -dx;
        dy = -dy;
        dz = -dz;

        // Hit position: entity center + direction * collision_radius
        float radius = *(float *)(target + 0x1C0);
        float hit_x = *(float *)(target + 0x64) + dx * radius + *(float *)(target + 0x1A8);
        float hit_y = *(float *)(target + 0x68) + dy * radius + *(float *)(target + 0x1AC);
        float hit_z = *(float *)(target + 0x6C) + dz * radius + *(float *)(target + 0x1B0);

        // Build damage context
        int damage_context[20];
        switch_slot_reset((int)damage_context);
        damage_context[0] = target;
        damage_context[6] = 0x17;   // weapon type: melee
        damage_context[7] = 0x17;   // source category: melee
        damage_context[10] = (int)&hit_x;  // hit position ptr
        damage_context[11] = (int)&dx;     // knockback direction ptr

        // Time-scaled damage amount
        unsigned int timer = get_game_timer();
        float time_norm = (float)((double)timer - g_timer_offset_dbl) / g_time_divisor;
        damage_context[12] = (int)(p2 * time_norm + p3);  // damage amount

        damage_dispatch_to_entity(damage_context);
    }

    restore_stack_frame();
}
