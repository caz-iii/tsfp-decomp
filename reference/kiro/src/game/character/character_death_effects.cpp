// =============================================================================
// character_death_effects.cpp
// Character death, teleport, and damage flash visual effects
// Address Range: 0x800f76bc - 0x800f826c
// Batch: 310 | Date: 2026-08-25
// =============================================================================

#include "common.h"
#include "character.h"
#include "particle.h"
#include "entity.h"
#include "collision.h"
#include "weather.h"

// =============================================================================
// Address: 0x800f76bc | character_set_damage_flash
// Sets damage flash effect on character. Stores duration and intensity,
// enables flag 0x200. Only overrides if new duration exceeds current
// (prevents weaker flashes from interrupting stronger ones).
// =============================================================================
void character_set_damage_flash(float duration, float intensity, int* char_data) {
    float zero = 0.0f;

    // Don't override a stronger/longer flash already in progress
    if ((*(u32*)(char_data + 1) & 0x200) != 0 && duration < *(float*)((u8*)char_data + 0xa0)) {
        return;
    }

    *(u32*)(char_data + 1) |= 0x200;  // Enable damage flash flag
    *(float*)((u8*)char_data + 0xa0) = duration;
    *(float*)((u8*)char_data + 0xa4) = intensity;
    *(float*)((u8*)char_data + 0x9c) = zero;  // Reset timer
}

// =============================================================================
// Address: 0x800f76f4 | character_damage_flash_tick
// Per-frame damage flash tick. Advances timer, computes fade ratio,
// scales particle emitter opacity on player's attached effect (+0x150).
// Clears flag 0x200 when duration expires.
// =============================================================================
void character_damage_flash_tick(u32* char_data) {
    float opacity;
    u32* emitter_def;
    int effect_ptr;
    float fade_ratio;

    if ((char_data[1] & 0x200) == 0) {
        return;
    }

    // Advance timer by frame delta
    char_data[0x27] = *(float*)&char_data[0x27] + g_frame_delta_scaled;

    // Check if duration expired
    if (*(float*)&char_data[0x27] >= *(float*)&char_data[0x29]) {
        *(float*)&char_data[0x27] = *(float*)&char_data[0x29];
        char_data[1] &= ~0x200u;  // Clear damage flash flag
    }

    // Update particle emitter opacity for local player
    if (((int*)char_data[0])[2] < g_local_player_index &&
        0.0f < *(float*)&char_data[0x29]) {
        effect_ptr = *(int*)(*(int*)char_data[0] + 0x150);
        fade_ratio = *(float*)&char_data[0x28] *
                     (1.0f - *(float*)&char_data[0x27] / *(float*)&char_data[0x29]);

        if (*(int*)(effect_ptr + 8) != 0) {
            emitter_def = particle_get_emitter_def(*(int*)(*(int*)(effect_ptr + 8) + 0x214));
            opacity = 255.0f * fade_ratio;
            // Clamp to emitter minimum
            if (opacity < (float)emitter_def[4]) {
                opacity = (float)emitter_def[4];
            }
            emitter_def = particle_get_emitter_def(*(int*)(*(int*)(effect_ptr + 8) + 0x214));
            emitter_def[4] = (int)opacity;
        }
    }
}

// =============================================================================
// Address: 0x800f7860 | character_teleport_particles_spawn
// Spawns teleport arrival/departure particle effects. Creates:
// - 3 radial particle systems (expanding rings at different heights)
// - 2 directed particle sprays (upward beams)
// All positioned at entity world position with color tinting.
// =============================================================================
void character_teleport_particles_spawn(u32* char_data) {
    int entity_ptr;
    int particle_handle;
    float color[3];
    float position[3];
    float velocity[3];
    float scale[3];

    entity_ptr = *(int*)char_data[0];

    // Ring 1: ground level, green tint
    color[0] = 0.0f; color[1] = 1.0f; color[2] = 0.0f;
    velocity[0] = 0.0f; velocity[1] = 0.0f; velocity[2] = 0.5f;
    position[0] = *(float*)(entity_ptr + 0x64);
    position[1] = *(float*)(entity_ptr + 0x68) + 2.0f;
    position[2] = *(float*)(entity_ptr + 0x6c);
    scale[0] = 0.25f; scale[1] = 1.0f; scale[2] = 0.25f;

    particle_handle = weather_radial_particle_spawn(
        2.0f, 0.0f, 0, 0x5c, 0x80000, position, velocity, scale, 0xffff);
    particle_slot_set_color(particle_handle, color);

    // Ring 2: elevated, blue-green tint
    color[0] = 0.0f; color[1] = 0.75f; color[2] = 0.0f;
    velocity[0] = 0.0f; velocity[1] = 0.0f; velocity[2] = 0.0f;
    position[0] = *(float*)(entity_ptr + 0x64);
    position[1] = *(float*)(entity_ptr + 0x68) + 0.75f;
    position[2] = *(float*)(entity_ptr + 0x6c);
    scale[0] = 0.25f; scale[1] = 1.0f; scale[2] = 0.25f;

    particle_handle = weather_radial_particle_spawn(
        3.0f, 0.0f, 0, 0x5c, 0x80000, position, velocity, scale, 0xffff);
    particle_slot_set_color(particle_handle, color);

    // Ring 3: high position, different type
    velocity[0] = 0.0f; velocity[1] = 0.0f; velocity[2] = 0.0f;
    position[0] = *(float*)(entity_ptr + 0x64);
    position[1] = *(float*)(entity_ptr + 0x68) + 1.5f;
    position[2] = *(float*)(entity_ptr + 0x6c);
    scale[0] = 0.125f; scale[1] = 0.1f; scale[2] = 0.125f;

    weather_radial_particle_spawn(
        4.0f, 1.0f, 0, 0x5d, 0x80000, position, velocity, scale, 0xffff);

    // Directed beam 1: green
    color[0] = 0.0f; color[1] = 1.0f; color[2] = 0.0f;
    particle_handle = weather_directed_particle_spawn(0, 0x5e, 0x880000, position, NULL);
    particle_slot_set_color(particle_handle, color);

    // Directed beam 2: blue-green
    color[0] = 0.0f; color[1] = 0.75f; color[2] = 0.0f;
    particle_handle = weather_directed_particle_spawn(0, 0x5e, 0x880000, position, NULL);
    particle_slot_set_color(particle_handle, color);
}

// =============================================================================
// Address: 0x800f7a68 | character_teleport_effect_start
// Initiates teleport visual effect. Spawns particles, sets duration,
// resets timer, configures mode (in/out). Enables flag 0x8000.
// =============================================================================
void character_teleport_effect_start(float duration, u32* char_data, u32 mode) {
    character_teleport_particles_spawn(char_data);

    *(float*)&char_data[0x21] = duration;   // Total duration
    *(float*)&char_data[0x20] = 0.0f;       // Reset timer
    char_data[0x1f] = mode;                 // 0=teleport-in, 1=teleport-out

    if (*(int*)(*(int*)char_data[0] + 0x150) != 0) {
        char_data[1] |= 0x8000;  // Enable teleport effect flag
    }

    // Clear flag 0x1 (active) if effect attached and was set
    if (*(int*)(*(int*)char_data[0] + 0x150) != 0 && (char_data[1] & 1) != 0) {
        char_data[1] &= ~1u;
    }
}

// =============================================================================
// Address: 0x800f7b0c | character_teleport_effect_tick
// Per-frame teleport shimmer/dissolve tick. Advances timer, computes
// opacity via sine function, updates collision shape transparency,
// spawns impact entity for visual, modifies player particle emitter colors.
// Destroys entities and clears flags when duration expires.
// =============================================================================
void character_teleport_effect_tick(u32* char_data) {
    u32 sin_val;
    u32 clamped;
    u32* emitter;
    int entity_ptr;
    int impact_entity;
    float ratio;
    float timer_advance;

    entity_ptr = *(int*)char_data[0];

    // Advance timer
    timer_advance = g_frame_delta_scaled * (1.0f / g_target_framerate);
    *(float*)&char_data[0x20] += timer_advance;

    float current_time = *(float*)&char_data[0x20];
    float total_duration = *(float*)&char_data[0x21];

    if (current_time < total_duration) {
        if (*(int*)(entity_ptr + 0x150) == 0 || *(int*)(*(int*)(entity_ptr + 0x150) + 4) < 0) {
            // No particle attached - use collision shape transparency
            *(u32*)(entity_ptr + 0x28) |= 1;

            // Spawn impact entity if not yet created
            if (*(int*)(entity_ptr + 0x208) == 0) {
                impact_entity = projectile_spawn_impact_entity(entity_ptr);
                *(int*)(entity_ptr + 0x208) = impact_entity;
            }

            // Compute sine-based opacity
            if (char_data[0x1f] == 1) {
                // Teleport out: fade from opaque to transparent
                sin_val = math_sin_u8(255.0f * (current_time / total_duration));
            } else {
                // Teleport in: fade from transparent to opaque
                sin_val = math_sin_u8(255.0f * (1.0f - current_time / total_duration));
            }

            // Apply transparency to impact entity
            if (*(int*)(entity_ptr + 0x208) != 0) {
                clamped = sin_val;
                if (sin_val > 0x3f) {
                    clamped = 0x7f - sin_val;
                }
                collision_set_shape_params(
                    *(int**)(*(int*)(entity_ptr + 0x208) + 4),
                    0x13, 0xffffffff, 2, (char)(clamped << 1));
            }

            // Apply to main entity collision
            collision_set_shape_params(
                *(int**)(entity_ptr + 4),
                0xf, 0xffffffff, 2,
                (char)((sin_val << 1) + 0x81u) & -(sin_val > 0x3f));
        } else {
            // Has particle attached - modify emitter colors directly
            sin_val = math_sin_u8(255.0f * (current_time / total_duration));

            u32 color_add;
            if (sin_val < 0x20) {
                color_add = sin_val << 1;
            } else {
                int secondary = math_sin_u8(4.0f * (float)(sin_val - 0x20));
                color_add = 0x40 - secondary;
            }

            emitter = particle_get_current_player_emitter();
            emitter[0xd] += color_add;
            emitter = particle_get_current_player_emitter();
            emitter[5] += (color_add >> 1);
            emitter = particle_get_current_player_emitter();
            emitter[10] += color_add;
            emitter = particle_get_current_player_emitter();
            emitter[4] += color_add;
        }
    } else {
        // Effect complete - cleanup
        *(float*)&char_data[0x20] = 0.0f;
        char_data[1] &= ~0x8001u;  // Clear teleport + active flags

        if (*(u32**)(entity_ptr + 0x208) != NULL) {
            entity_destroy_with_event(*(u32**)(entity_ptr + 0x208));
        }
    }
}

// =============================================================================
// Address: 0x800f7d44 | character_bone_blend_tick
// Per-frame bone interpolation/blend tick. Builds identity scale matrix,
// iterates all skeleton bones blending between source and destination
// transforms via matrix lerp. Used for death collapse animation.
// =============================================================================
void character_bone_blend_tick(void) {
    int entity_ptr;
    int bone_count;
    int offset;
    float blend_matrix[22];

    u32* char_data = character_get_current();
    entity_ptr = *(int*)*(u32*)char_data;

    // Build identity scale matrix
    matrix_set_identity_scale(1.0f);

    if ((*(u32*)((u8*)char_data + 0x28) & 0x100) == 0) {
        offset = 0;
        bone_count = *(int*)(*(int*)(*(int*)(char_data[1]) + 4) + 4);

        for (int i = 0; i < bone_count; i++) {
            // Blend source bones toward destination bones
            matrix_lerp(
                blend_matrix,
                (float*)(*(int*)(*(int*)(entity_ptr + 4) + 0x10) + offset),  // src bones
                (float*)(*(int*)(*(int*)(char_data[1]) + 0x10) + offset));   // dst bones
            offset += 0x40;  // 4x4 matrix stride
        }
    }

    character_release_current();
}

// =============================================================================
// Address: 0x800f7de0 | character_spawn_death_dissolve
// Spawns death dissolve effect: 4 shell casing entities at different scales
// (representing body chunks/fragments) plus 1 timed particle system with
// randomized velocities. Positions everything at entity center.
// =============================================================================
void character_spawn_death_dissolve(void) {
    float scale_vec[3];
    float position[3];
    u32 particle_def[3];
    u32* char_data;
    int entity_ptr;
    float* particle_data;
    int i;

    char_data = character_get_current_raw();
    entity_ptr = *(int*)char_data[0];

    // Particle definition from ROM data
    particle_def[0] = g_death_particle_def[0];
    particle_def[1] = g_death_particle_def[1];
    particle_def[2] = g_death_particle_def[2];

    // Position at entity center, slightly elevated
    position[0] = *(float*)(entity_ptr + 0x64);
    position[1] = *(float*)(entity_ptr + 0x68) + 1.0f;
    position[2] = *(float*)(entity_ptr + 0x6c);

    // Fragment 1: full scale
    scale_vec[0] = 0.0f; scale_vec[1] = 0.0f; scale_vec[2] = 0.0f;
    char_data[0x19] = shell_casing_spawn_entity(
        0x35, position, scale_vec, 0, &char_data[0x19], 0, 0);

    // Fragment 2: 0.8x scale
    scale_vec[0] = 0.8f; scale_vec[1] = 0.8f;
    char_data[0x1a] = shell_casing_spawn_entity(
        0x35, position, scale_vec, 0, &char_data[0x1a], 0, 0);

    // Fragment 3: 0.5x scale
    scale_vec[0] = 0.5f; scale_vec[1] = 0.5f;
    char_data[0x1b] = shell_casing_spawn_entity(
        0x35, position, scale_vec, 0, &char_data[0x1b], 0, 0);

    // Fragment 4: 0.7x scale
    scale_vec[0] = 0.7f; scale_vec[1] = 0.7f;
    char_data[0x1c] = shell_casing_spawn_entity(
        0x35, position, scale_vec, 0, &char_data[0x1c], 0, 0);

    // Spawn timed particle system
    char_data[0x18] = particle_spawn_timed(&char_data[0x18], 0x10, 0, particle_def);

    if (char_data[0x18] != 0) {
        particle_data = *(float**)(char_data[0x18] + 0x18);
        int count = *(int*)(char_data[0x18] + 0x1c);

        particle_data[0x204] = 5.0f;  // Lifetime

        // Randomize per-particle velocities
        int vel_offset = 0;
        int color_offset = 0;
        for (i = 0; i < count; i++) {
            float rng = random_float();
            *(float*)((u8*)particle_data + vel_offset + 0x40) = 6.0f * (rng - 2.0f);
            rng = random_float();
            *(float*)((u8*)particle_data + vel_offset + 0x44) = 0.25f * rng;
            rng = random_float();
            *(float*)((u8*)particle_data + vel_offset + 0x48) = 6.0f * (rng - 2.0f);

            // Set initial alpha to zero (fade in)
            *(float*)((u8*)particle_data + color_offset + 0x680) = 0.0f;
            // Set color (brownish death particle)
            *(u32*)((u8*)particle_data + color_offset + 0x4f0) = 0x7f7f4f2f;

            vel_offset += 0xc;
            color_offset += 4;
        }

        particle_finalize_init((int)particle_data);

        // Position particle system
        float offset_y = 0.5f;
        particle_data[0xc] = position[0];
        particle_data[0xd] = position[1] - offset_y;
        particle_data[0xe] = position[2];
        particle_data[0] = offset_y;
        particle_data[5] = offset_y;
        particle_data[10] = offset_y;
    }

    character_release_current_raw();
}

// =============================================================================
// Address: 0x800f803c | character_death_dissolve_tick
// Per-frame death dissolve effect tick. Iterates 4 attached entities,
// updates color from lookup table based on timer. Applies rotation and
// scale matrix to weather particle slot. Destroys all entities and
// weather slot when timer exceeds threshold. Clears flag 0x1.
// =============================================================================
void character_death_dissolve_tick(void) {
    u32 color_index;
    int fragment_ptr;
    int material_ptr;
    float* particle_data;
    float rot_matrix[16];
    float scale_matrix[16];

    int char_data = character_get_current_ext();
    float frame_delta = g_frame_delta_scaled * (1.0f / g_target_framerate);

    // Update 4 fragment entities
    int slot_offset = 0;
    for (int i = 0; i < 4; i++) {
        int slot_addr = char_data + slot_offset + 100;
        if (*(int*)slot_addr != 0) {
            material_ptr = *(int*)(*(int*)slot_addr + 0x7c);

            if (0.0f <= *(float*)(char_data + 0x5c)) {
                // Compute color from LUT based on timer
                color_index = (u32)(64.0f * (4.0f * *(float*)(char_data + 0x5c)) / 6.0f + 2.0f);
                *(u32*)(material_ptr + 0x10) = g_death_color_lut[color_index & 0x7ff];
            } else {
                *(float*)(material_ptr + 0x10) = 0.0f;
            }

            // Destroy fragment when timer exceeds threshold
            if (6.0f < *(float*)(char_data + 0x5c)) {
                entity_destroy_with_event(*(u32**)slot_addr);
                *(u32*)slot_addr = 0;
            }
        }
        slot_offset += 4;
    }

    // Update weather particle slot (rotation + scale over time)
    if (*(int*)(char_data + 0x60) != 0) {
        particle_data = *(float**)(*(int*)(char_data + 0x60) + 0x18);

        // Build rotation matrix scaled by time
        matrix_build_rotation(4.0f * (7.0f * frame_delta) / 0.5f, rot_matrix);
        // Build scale matrix
        matrix_set_identity_scale(
            8.0f * frame_delta + 1.0f,
            12.0f * frame_delta + 1.0f,
            8.0f * frame_delta + 1.0f);

        matrix_lerp(scale_matrix, particle_data, particle_data);
        matrix_lerp(rot_matrix, particle_data, particle_data);
    }

    // Final cleanup when effect fully complete
    if (1.5f < *(float*)(char_data + 0x5c)) {
        if (*(int**)(char_data + 0x60) != NULL) {
            weather_slot_destroy(*(int**)(char_data + 0x60));
            *(u32*)(char_data + 0x60) = 0;
        }
        *(u32*)(char_data + 4) &= ~1u;  // Clear active flag
    }

    character_release_current_ext();
}

// =============================================================================
// Address: 0x800f81f0 | character_death_timer_tick
// Death sequence timer. Advances timer (+0x5c) at frame rate scaled by
// global frame counter. Clears flag 0x1 when timer reaches 1.0.
// =============================================================================
void character_death_timer_tick(int char_data) {
    float one = 1.0f;

    // Advance timer: dt * speed_factor * (1/target_framerate)
    *(float*)(char_data + 0x5c) +=
        2.0f * g_frame_speed_factor * (1.0f / g_target_framerate);

    if (one <= *(float*)(char_data + 0x5c)) {
        *(u32*)(char_data + 4) &= ~1u;  // Clear active flag - death complete
    }
}

// =============================================================================
// Address: 0x800f826c | character_death_anim_check
// Polls death animation completion. Checks if animation 0x201 (death)
// has finished or is not playing. Clears flag 0x1 when death anim is done.
// =============================================================================
void character_death_anim_check(u32* char_data) {
    int result;

    // Check if death anim (0x201) has completed
    result = anim_is_finished(*(int*)char_data[0], 0x201);
    if (result != 0 || anim_is_playing(*(int*)char_data[0], 0x201) == 0) {
        char_data[1] &= ~1u;  // Clear active flag - death anim done
    }
}
