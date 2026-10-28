// Address: 0x800f5994 - 0x800f675c | Batch 307 | Date: 2026-08-25
// Character FX particle systems + pickup item visual setup
// Subsystem: game (character effects + pickup visual props)

#include "common.h"
#include "entity.h"
#include "particle.h"
#include "character.h"
#include "damage.h"
#include "weather.h"
#include "water.h"
#include "math_utils.h"

// ============================================================================
// Address: 0x800f5994 | Name: character_fx_bone_particle_tick
// Per-frame tick for character bone particle effects (fire/electrocution).
// Iterates all bones, applies random position jitter for entity type 0x5f2,
// spawns render lines between emitter and bone positions, fades over time.
// ============================================================================
void character_fx_bone_particle_tick(void) {
    int* pSlot = (int*)entity_script_get_slot();
    float fZero = 0.0f;

    if ((pSlot[1] & 0x400000) == 0) {
        entity_script_return();
        return;
    }

    // Integer-to-float conversion trick (PowerPC idiom)
    u32 frameSeed = g_frame_counter ^ 0x80000000;
    // ... double conversion for frame delta

    int pEntity = *(int*)pSlot[0];
    int pRenderData = *(int*)(pEntity + 0x7c);

    // Fade timer: decrease towards zero
    *(float*)(pRenderData + 0xd3c) =
        -(((1.0f / (float)((double)frameSeed_combined - DOUBLE_OFFSET)) *
           g_frame_rate_scale) - *(float*)(pRenderData + 0xd3c));

    if (*(float*)(pRenderData + 0xd3c) < 0.0f) {
        // Timer expired - clear effect flag
        pSlot[1] &= ~0x400000;
    }

    // Validate entity has valid skeleton data
    if (pEntity == 0 || *(int*)(pEntity + 4) == 0 ||
        (*(u32*)(pEntity + 0x10) & 0xFFFF) == 0) {
        entity_script_return();
        return;
    }

    float localMatrix[16];
    entity_build_local_matrix(localMatrix, pEntity, 1, 0);

    int boneSourceIdx;
    if (pSlot[0x34] == -1) {
        // No specific bone - use origin
        float emitterPos[3] = {0.0f, 0.0f, 0.0f};
    } else {
        // Get bone position from skeleton
        int boneData = *(int*)(*(int*)(pEntity + 4) + 0x10) + pSlot[0x34] * 0x40;
        float emitterPos[3];
        emitterPos[0] = *(float*)(boneData + 0x30);
        emitterPos[1] = *(float*)(boneData + 0x34);
        emitterPos[2] = *(float*)(boneData + 0x38);
    }

    // Transform emitter position to world space
    matrix_transform_point(localMatrix, emitterPos);

    // For special entity type 0x5f2: add random jitter to emitter
    if (*(s16*)(pEntity + 0x94) == 0x5f2) {
        float rx = 0.5f * ((float)(rng_next() - RNG_OFFSET) / RNG_RANGE - 1.0f);
        float ry = 0.5f * ((float)(rng_next() - RNG_OFFSET) / RNG_RANGE - 1.0f);
        float rz = 0.5f * ((float)(rng_next() - RNG_OFFSET) / RNG_RANGE - 1.0f);
        float jitterScale = 0.25f; // FLOAT_806153bc
        emitterPos[0] += jitterScale * rx;
        emitterPos[1] += jitterScale * ry;
        emitterPos[2] += jitterScale * rz;
        // Additional Y offset from RNG
        emitterPos[1] += (float)(rng_next() - RNG_OFFSET) / RNG_RANGE;
    }

    // Iterate all bones and spawn render lines
    int boneCount = *(int*)(*(int*)(*(int*)(pEntity + 4) + 4) + 4);
    int boneOffset = 0;
    for (int i = 0; i < boneCount; i++) {
        if (pSlot[0x34] != i) {
            float bonePos[3];
            float boneVel[3];

            if (*(s16*)(pEntity + 0x94) == 0x5f2 &&
                i == *(int*)(*(int*)(*(int*)(*(int*)pSlot[0] + 4) + 0x84) + 0x54)) {
                // Special bone: random velocity only
                bonePos[0] = 0.0f;
                bonePos[2] = 0.0f;
                bonePos[1] = 3.0f * ((float)(rng_next() - RNG_OFFSET) / RNG_RANGE - 1.0f);
                matrix_transform_point(*(int*)(*(int*)(pEntity + 4) + 0x10) + boneOffset, bonePos);
            } else {
                int boneBase = *(int*)(*(int*)(pEntity + 4) + 0x10) + boneOffset;
                bonePos[0] = *(float*)(boneBase + 0x30);
                bonePos[1] = *(float*)(boneBase + 0x34);
                bonePos[2] = *(float*)(boneBase + 0x38);
            }

            matrix_transform_point(localMatrix, bonePos);

            // Spawn render line from emitter to bone
            if (*(s16*)(pEntity + 0x94) == 0x5f2) {
                // Add per-bone jitter
                float rx = 0.5f * ((float)(rng_next() - RNG_OFFSET) / RNG_RANGE - 1.0f);
                float ry = 0.5f * ((float)(rng_next() - RNG_OFFSET) / RNG_RANGE - 1.0f);
                float rz = 0.5f * ((float)(rng_next() - RNG_OFFSET) / RNG_RANGE - 1.0f);
                float scale = 0.25f;
                bonePos[0] += scale * rx;
                bonePos[1] += scale * ry;
                bonePos[2] += scale * rz;
                render_line_spawn(); // FUN_80102e98
                render_line_set_width(0.02f); // FUN_80102f74
            } else {
                render_line_spawn();
                render_line_set_width(0.15f); // FLOAT_80615438
            }
        }
        boneOffset += 0x40;
    }

    entity_script_return();
}

// ============================================================================
// Address: 0x800f5dd4 | Name: character_fx_bone_particle_spawn
// Spawns up to 6 bone-attached particle emitters on a character entity.
// Picks random bone indices (no duplicates), transforms to world space,
// creates particle emitters with specified lifetime.
// ============================================================================
void character_fx_bone_particle_spawn(void) {
    // f31 = lifetime parameter passed in FPR
    float lifetime; // in_f31

    int pEntity = entity_script_get_entity(); // FUN_803826e0
    int pRenderData = *(int*)(pEntity + 0x7c);

    // Set effect active flag
    *(u32*)(pRenderData + 0xc6c) |= 0x4000000;

    // Store lifetime
    *(float*)(pRenderData + 0xd58) = lifetime; // extraout_f1

    float localMatrix[29]; // includes extra workspace
    entity_build_local_matrix(localMatrix, pEntity, 1, 0);

    int spawnedCount = 0;
    int handleOffset = 0;

    do {
        // Pick random bone index (no duplicates)
        u32 boneCount = *(u32*)(*(int*)(*(int*)(pEntity + 4) + 4) + 4);
        u32 boneIdx = rng_next() % boneCount; // FUN_8013104c

        // Check for duplicate - reroll if found
        for (int j = 0; j < spawnedCount; j++) {
            if (boneIdx == *(u8*)(pRenderData + j + 0xd50)) {
                boneIdx = rng_next() % boneCount;
                break;
            }
        }

        // Get bone world-space position and direction
        int boneData = *(int*)(*(int*)(pEntity + 4) + 0x10) + boneIdx * 0x40;
        float position[3];
        position[0] = *(float*)(boneData + 0x30);
        position[1] = *(float*)(boneData + 0x34);
        position[2] = *(float*)(boneData + 0x38);

        float velocity[3];
        velocity[0] = *(float*)(boneData + 0x20);
        velocity[1] = *(float*)(boneData + 0x24);
        velocity[2] = *(float*)(boneData + 0x28);

        // Transform to world space
        matrix_transform_point(localMatrix, position);  // FUN_8012f9b8
        matrix_transform_vector(localMatrix, velocity); // FUN_8012fb90

        // Spawn particle emitter at bone position
        particle_emitter_spawn(pRenderData + handleOffset + 0xd38,
                              &g_bone_particle_def, 1, position, velocity);

        // Set particle lifetime
        int handle = *(int*)(pRenderData + handleOffset + 0xd38);
        particle_cached_set_lifetime(lifetime, &handle);

        // Record bone index
        *(u8*)(pRenderData + spawnedCount + 0xd50) = (u8)boneIdx;

        spawnedCount++;
        handleOffset += 4;

        if (spawnedCount > 5) {
            entity_script_return_b(); // FUN_8038272c
            return;
        }
    } while (1);
}

// ============================================================================
// Address: 0x800f5f70 | Name: character_fx_bone_particle_update
// Updates existing bone-attached particles each frame.
// Syncs position/velocity to current bone transforms.
// Scales particle size based on remaining lifetime fade.
// Destroys all particles when timer expires.
// ============================================================================
void character_fx_bone_particle_update(void) {
    int* pSlot = (int*)entity_script_get_slot(); // FUN_803826ec

    if ((pSlot[1] & 0x4000000) == 0) {
        entity_script_return(); // FUN_80382738
        return;
    }

    // Frame delta conversion
    u32 frameSeed = g_frame_counter ^ 0x80000000;

    int pEntity = *(int*)pSlot[0];
    int pRenderData = *(int*)(pEntity + 0x7c);

    // Decay timer
    float fadeRate = 1.0f / (float)((double)frameSeed_combined - DOUBLE_OFFSET);
    *(float*)(pRenderData + 0xd58) =
        -(fadeRate * g_frame_rate_scale - *(float*)(pRenderData + 0xd58));

    if (*(float*)(pRenderData + 0xd58) >= 0.0f) {
        // Still active - compute fade factor
        float rawScale = *(float*)(pRenderData + 0xd58) * 2.0f;
        float scale = (rawScale < 1.0f) ? rawScale : 1.0f;
        if (scale < 0.0f) scale = 0.0f;

        // Size also clamped
        float sizeScale = (rawScale < 1.0f) ? rawScale : 1.0f;
        if (sizeScale < 0.0f) sizeScale = 0.0f;

        float localMatrix[16];
        entity_build_local_matrix(localMatrix, pEntity, 1, 0);

        // Update each of 6 particle emitters
        int pEmitters = pRenderData + 0xd38;
        for (int i = 0; i < 6; i++) {
            int boneData = *(int*)(*(int*)(pEntity + 4) + 0x10);
            int boneIdx = *(u8*)(pEmitters + i + 0x18);
            int bone = boneData + boneIdx * 0x40;

            float position[3];
            position[0] = *(float*)(bone + 0x30);
            position[1] = *(float*)(bone + 0x34);
            position[2] = *(float*)(bone + 0x38);

            float velocity[3];
            velocity[0] = *(float*)(bone + 0x20);
            velocity[1] = *(float*)(bone + 0x24);
            velocity[2] = *(float*)(bone + 0x28);

            matrix_transform_point(localMatrix, position);
            matrix_transform_vector(localMatrix, velocity);

            int handle = *(int*)(pEmitters + i * 4);
            particle_cached_set_position(&handle, position);
            particle_cached_set_velocity(&handle, velocity);
            particle_cached_set_size_range(sizeScale, sizeScale, &handle);
        }
    } else {
        // Timer expired - destroy all particles
        int pEmitters = pRenderData + 0xd38;
        pSlot[1] &= ~0x4000000; // Clear active flag

        for (int i = 0; i < 6; i++) {
            int handle = *(int*)(pEmitters + i * 4);
            particle_handle_destroy(&handle);
        }
    }

    entity_script_return();
}

// ============================================================================
// Address: 0x800f6164 | Name: fx_spawn_explosion_particle
// Spawns a single explosion particle effect at the given world position.
// Uses pre-defined velocity direction from global constant.
// ============================================================================
void fx_spawn_explosion_particle(float* pPosition) {
    float velocity[3];
    velocity[0] = g_explosion_velocity_x; // DAT_803cc9c0
    velocity[1] = g_explosion_velocity_y; // DAT_803cc9c4
    velocity[2] = g_explosion_velocity_z; // DAT_803cc9c8

    particle_emitter_spawn(0, &g_explosion_particle_def, 0, pPosition, velocity);
}

// ============================================================================
// Address: 0x800f61bc | Name: fx_weapon_fire_particle_init
// Initializes weapon fire particle system with 3 particle definitions:
//   1. Primary smoke (DAT_80571aec) - alpha fade, drag-based dispersal
//   2. Secondary smoke (DAT_80571bb8) - similar but different timing
//   3. Additive fire glow (DAT_80571d08) - brighter, less drag
// Each def has full keyframe size, color fade, velocity, and emitter config.
// Called from character_fx_init_weapon.
// ============================================================================
void fx_weapon_fire_particle_init(void) {
    float emitterScale[3];
    emitterScale[0] = g_fire_scale_x; // DAT_803cc9cc
    emitterScale[1] = g_fire_scale_y; // DAT_803cc9d0
    emitterScale[2] = g_fire_scale_z; // DAT_803cc9d4

    // --- Definition 1: Primary smoke ---
    particle_def_create(0.02f, 0.0f, &g_smoke_def_a, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes(0.0f, 1.0f, 1.0f, 0.5f,
                                    0x1e1d0b00, 0x1f1d0b00, 0x0a090300);
    particle_def_set_size_start_end(0.1f, /*start*/ 0.0f, /*end*/ 0.0f, 0.25f);
    particle_def_set_color_end(/*r*/ 0.0f, /*g*/ 0.0f, /*b*/ 0.25f, /*a*/ 0.0f);
    particle_def_set_velocity_ranges(0.5f, /*max*/ 0.0f, 2.0f,
                                     0.25f, 0.0f, 2.0f);
    particle_def_set_drag(/*drag*/ 0.0f);

    particle_emitter_create(1.0f, 2.0f, 0.0f,
                           &g_explosion_particle_def, &g_smoke_def_a, 1, 0x201);
    particle_emitter_set_color_range(0.02f, 1.0f, /*range*/ 0.0f, 1.0f);
    particle_emitter_set_scale(emitterScale);

    // --- Definition 2: Secondary smoke ---
    particle_def_create(0.02f, 0.0f, &g_smoke_def_b, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes(0.0f, 1.0f, 1.0f, 0.5f,
                                    0x3c434500, 0x3c434500, 0x14161700);
    particle_def_set_size_start_end(0.1f, 0.0f, 0.0f, 0.25f);
    particle_def_set_color_end(0.0f, 0.0f, 0.25f, 0.0f);
    particle_def_set_velocity_ranges(0.5f, 0.0f, 2.0f, 0.25f, 0.0f, 2.0f);
    particle_def_set_drag(0.0f);

    particle_emitter_create(1.0f, 2.0f, 0.0f,
                           &g_smoke_def_b_emitter, &g_smoke_def_b, 1, 0x201);
    particle_emitter_set_color_range(0.02f, 1.0f, 0.0f, 1.0f);
    particle_emitter_set_scale(emitterScale);

    // --- Definition 3: Additive fire glow ---
    particle_def_create(0.5f, 0.0f, &g_fire_def, 0, 1, 1, 0x326, 0);
    particle_def_set_size_keyframes(0.0f, 1.0f, 1.0f, 2.0f,
                                    0x70707f00, 0x7f7f7f00, 0x0f0f0000);
    particle_def_set_size_start_end(0.1f, 0.0f, 0.25f, 0.25f);
    particle_def_set_color_end(/*r*/ 0.0f, /*g*/ 0.0f, /*b*/ 0.0f, /*a*/ 1.0f);
    particle_def_set_velocity_ranges(1.0f, 1.0f, /*upward*/ 0.0f,
                                     0.25f, 0.0f, /*spread*/ 0.0f);
    particle_def_set_drag(0.0f);

    particle_emitter_create(0.0f, 1.0f, 0.25f,
                           &g_bone_particle_def, &g_fire_def, 0, 0);
    particle_emitter_set_color_range(0.0f, 1.0f, /*range*/ 0.0f, 1.0f);
}

// ============================================================================
// Address: 0x800f6480 | Name: hazard_zone_damage_tick
// Environmental hazard zone damage tick (fire/acid/lava).
// Computes distance from hazard source, applies damage per frame scaled by
// difficulty mode and frame delta. Checks weather (rain reduces fire damage)
// and water volume. Triggers death effect when health depletes.
// ============================================================================
void hazard_zone_damage_tick(void) {
    int* pSlot = (int*)entity_script_get_slot_b(); // FUN_803826f0

    if ((pSlot[1] & 4) == 0) {
        entity_script_return_c(); // FUN_8038273c
        return;
    }

    int* pCharData = (int*)pSlot[0];
    int pEntity = *pCharData;

    // Frame delta computation
    u32 frameSeed = g_frame_counter ^ 0x80000000;
    float frameDelta = 1.0f / (float)((double)frameSeed_combined - DOUBLE_OFFSET);
    float scaledDelta = g_frame_rate_scale * frameDelta;

    // Distance from hazard center to entity
    float dx = (float)pCharData[0x2ed] - *(float*)(pEntity + 0x64);
    float dz = (float)pCharData[0x2ef] - *(float*)(pEntity + 0x6c);
    float distSq = dx * dx + dz * dz;
    float dist = sqrtf(distSq); // FUN_80131538

    // Random factors for damage variation
    rng_float(); // FUN_801310c0 x7
    rng_float();
    rng_float();
    rng_float();
    rng_float();
    rng_float();
    rng_float();

    // Only apply damage if entity has health remaining
    if (*(float*)(pEntity + 0x1cc) > 0.0f) {
        int pRenderData = *(int*)(pEntity + 0x7c);
        DamageInfo dmgInfo;
        switch_slot_reset(&dmgInfo);

        dmgInfo.target = pEntity;
        dmgInfo.position_ptr = pEntity + 0x64;
        dmgInfo.source = pSlot[0x2e];

        // Difficulty-based damage scaling
        if (*(int*)(pRenderData + 0x44) == 1) {
            // Easy mode
            dmgInfo.amount = 0.0f * scaledDelta; // FLOAT_80615440
        } else if (g_level_mode == '\n') {
            // Arcade/special mode
            dmgInfo.amount = 0.0f * scaledDelta; // FLOAT_80615470
        } else {
            // Normal/hard mode
            dmgInfo.amount = 0.0f * scaledDelta; // FLOAT_80615474
        }

        dmgInfo.source_entity = dmgInfo.source;

        if (dmgInfo.amount > 0.0f) {
            damage_dispatch_to_entity(&dmgInfo);
        }
    }

    // Position slightly above entity for water/weather test
    float testPos[3];
    testPos[0] = *(float*)(pEntity + 0x64);
    testPos[1] = 0.5f + *(float*)(pEntity + 0x68); // FLOAT_80615404
    testPos[2] = *(float*)(pEntity + 0x6c);

    // Check if protected by weather or water
    if (g_level_mode == '\n' || (g_level_flags & 0x40) != 0 ||
        weather_get_current_type() != 1 ||
        water_test_position_in_volume(testPos, *(s8*)(pEntity + 0x30),
                                      pEntity + 0x38, *(int*)(pEntity + 0x150)) == 0) {
        // Not protected - apply health decay
        if (*(int*)(pEntity + 8) >= g_hazard_immunity_threshold ||
            FUN_8027eb3c(pSlot[0]) != 0) {
            if (g_level_mode == '\n') {
                // Linear drain
                pSlot[0x2d] = (int)((float)pSlot[0x2d] - scaledDelta);
            } else {
                // Exponential drain
                pSlot[0x2d] = (int)-((0.5f * scaledDelta) - (float)pSlot[0x2d]);
            }

            if ((float)pSlot[0x2d] <= 0.0f) {
                character_death_effect_trigger(pSlot);
            }
        }
    } else {
        // In water - reduced damage
        pSlot[0x2d] = (int)((float)pSlot[0x2d] - scaledDelta);
        if ((float)pSlot[0x2d] <= 0.0f) {
            character_death_effect_trigger(pSlot);
        }
    }

    entity_script_return_c();
}

// ============================================================================
// Address: 0x800f66f0 | Name: pickup_set_fade_params
// Configures fade-in/out parameters for a pickup item.
// Sets fade duration (1.0) and target opacity, initializes state machine.
// ============================================================================
void pickup_set_fade_params(int pPickupData) {
    // FUN_800f6774 sets up fade state: duration=1.0, finalAlpha=0.25
    pickup_fade_state_init(1.0f, 0.25f, pPickupData); // FUN_800f6774
    *(float*)(pPickupData + 0x28) = PICKUP_GLOW_DEFAULT; // FLOAT_8061546c
}

// ============================================================================
// Address: 0x800f672c | Name: pickup_set_bob_amplitude
// Sets vertical bob amplitude for a pickup item floating animation.
// Enables bob flag (0x400) and stores amplitude value.
// ============================================================================
void pickup_set_bob_amplitude(int pPickupData) {
    float amplitude = PICKUP_GLOW_DEFAULT; // FLOAT_8061546c
    *(u32*)(pPickupData + 4) |= 0x400;    // Enable bob
    *(float*)(pPickupData + 0xa8) = amplitude;
}

// ============================================================================
// Address: 0x800f6744 | Name: pickup_set_spin_speed
// Sets rotation speed for a pickup item spin animation.
// Enables spin flag (0x1000) and stores angular speed.
// ============================================================================
void pickup_set_spin_speed(int pPickupData) {
    float speed = PICKUP_SPIN_SPEED; // FLOAT_80615414
    *(u32*)(pPickupData + 4) |= 0x1000;  // Enable spin
    *(float*)(pPickupData + 0xac) = speed;
}

// ============================================================================
// Address: 0x800f675c | Name: pickup_set_glow_intensity
// Sets glow/pulse intensity for a pickup item visual effect.
// Enables glow flag (0x800) and stores intensity value.
// ============================================================================
void pickup_set_glow_intensity(int pPickupData) {
    float intensity = PICKUP_GLOW_DEFAULT; // FLOAT_8061546c
    *(u32*)(pPickupData + 4) |= 0x800;    // Enable glow
    *(float*)(pPickupData + 0xb0) = intensity;
}
