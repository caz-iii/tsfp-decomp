// Address: 0x800f70c4 - 0x800f764c | Batch 309 | Date: 2026-08-25
// Character opacity system: fade in/out interpolation, foot dust, blood effects
// Subsystem: game (character visual effects)

#include "common.h"
#include "entity.h"
#include "character.h"
#include "particle.h"
#include "math_utils.h"

// ============================================================================
// Character Opacity Data Layout (within character render data):
//   +0x04: flags (0x2=opacity active, 0x20000=hold, 0x100=foot particle)
//   +0x88: mode (0=instant set, 1=fade-in, 2=fade-out)
//   +0x8c: render pass mask (0x3f = all passes)
//   +0x90: timer (current progress in seconds)
//   +0x94: duration (total fade time)
//   +0x98: current opacity value (0.0 = transparent, 1.0 = opaque)
// ============================================================================

// ============================================================================
// Address: 0x800f70c4 | Name: character_opacity_tick
// Per-frame opacity interpolation. Advances timer, computes ratio.
// Supports fade-in (mode 1) and fade-out (mode 2, ratio inverted).
// Clears active flags when duration is reached.
// ============================================================================
void character_opacity_tick(int pCharData) {
    // Only process if opacity flag active and mode is set
    if (((*(u32*)(pCharData + 4) & 0x2) == 0) || (*(int*)(pCharData + 0x88) == 0)) {
        return;
    }

    // Advance timer by frame delta
    float frameDelta = g_frame_rate_scale *
                       (1.0f / (float)((double)frame_counter_combined - DOUBLE_OFFSET));
    *(float*)(pCharData + 0x90) += frameDelta;

    // Check if duration reached
    if (*(float*)(pCharData + 0x94) <= *(float*)(pCharData + 0x90)) {
        *(float*)(pCharData + 0x90) = *(float*)(pCharData + 0x94);
        *(u32*)(pCharData + 4) &= ~0x00020002; // Clear opacity + hold flags
    }

    // Compute ratio
    if (*(float*)(pCharData + 0x94) <= 0.0f) {
        *(float*)(pCharData + 0x98) = 0.0f;
    } else {
        *(float*)(pCharData + 0x98) =
            *(float*)(pCharData + 0x90) / *(float*)(pCharData + 0x94);
    }

    // Mode 2 (fade-out): invert ratio so 1.0 -> 0.0
    if (*(int*)(pCharData + 0x88) == 2) {
        *(float*)(pCharData + 0x98) = 1.0f - *(float*)(pCharData + 0x98);
    }
}

// ============================================================================
// Address: 0x800f718c | Name: character_set_opacity_instant
// Sets character opacity immediately with no animation.
// Wrapper for character_opacity_set_target with mask=0x3f (all passes).
// Called from game_phase_state_machine, match_reset_cameras_and_spawn.
// ============================================================================
void character_set_opacity_instant(float opacity, int pCharData) {
    character_opacity_set_target(opacity, pCharData, 0x3f);
}

// ============================================================================
// Address: 0x800f71b0 | Name: character_set_opacity_with_hold
// Sets opacity with hold flag to prevent auto-fade.
// Wrapper for character_opacity_set_target_hold with mask=0x3f.
// Called from camera_orbit_tick for character fade during orbit.
// ============================================================================
void character_set_opacity_with_hold(float opacity, int pCharData) {
    character_opacity_set_target_hold(opacity, pCharData, 0x3f);
}

// ============================================================================
// Address: 0x800f71d4 | Name: character_opacity_set_target
// Core opacity target setter. If value in (0.0, 1.0]: enables opacity
// flag (0x2), stores target, clears mode (instant). Otherwise clears all.
// ============================================================================
void character_opacity_set_target(float opacity, int pCharData, u32 mask) {
    if (opacity > 0.0f && opacity <= 1.0f) {
        *(u32*)(pCharData + 4) |= 0x2;        // Enable opacity
        *(int*)(pCharData + 0x88) = 0;         // Mode: instant/set
        *(float*)(pCharData + 0x98) = opacity; // Current opacity
        *(u32*)(pCharData + 0x8c) = mask;      // Render pass mask
    } else {
        *(u32*)(pCharData + 4) &= ~0x00020002; // Clear opacity + hold
        *(float*)(pCharData + 0x98) = 0.0f;    // Fully transparent
        *(u32*)(pCharData + 0x8c) = 0;         // Clear mask
    }
}

// ============================================================================
// Address: 0x800f7238 | Name: character_opacity_set_target_hold
// Sets opacity target and additionally enables hold flag (0x20000).
// Prevents automatic fade-out after reaching the target value.
// ============================================================================
void character_opacity_set_target_hold(float opacity, int pCharData, u32 mask) {
    character_opacity_set_target(opacity, pCharData, mask);

    if ((*(u32*)(pCharData + 4) & 0x2) != 0) {
        *(u32*)(pCharData + 4) |= 0x20000; // Set hold flag
    }
}

// ============================================================================
// Address: 0x800f7278 | Name: character_opacity_fade_out
// Starts opacity fade-out over specified duration.
// Mode=2, timer reset, duration stored, mask=0x3f.
// ============================================================================
void character_opacity_fade_out(float duration, int pCharData) {
    *(u32*)(pCharData + 4) |= 0x2;         // Enable opacity
    *(float*)(pCharData + 0x90) = 0.0f;    // Reset timer
    *(int*)(pCharData + 0x88) = 2;         // Mode: fade-out
    *(float*)(pCharData + 0x94) = duration; // Duration
    *(u32*)(pCharData + 0x8c) = 0x3f;      // All render passes
}

// ============================================================================
// Address: 0x800f72a4 | Name: character_opacity_fade_in
// Starts opacity fade-in over specified duration.
// Mode=1, timer reset, duration stored, mask=0x3f.
// ============================================================================
void character_opacity_fade_in(float duration, int pCharData) {
    *(u32*)(pCharData + 4) |= 0x2;         // Enable opacity
    *(int*)(pCharData + 0x88) = 1;         // Mode: fade-in
    *(float*)(pCharData + 0x94) = duration; // Duration
    *(float*)(pCharData + 0x90) = 0.0f;    // Reset timer
    *(u32*)(pCharData + 0x8c) = 0x3f;      // All render passes
}

// ============================================================================
// Address: 0x800f72d0 | Name: character_foot_particle_tick
// Per-frame foot dust/trail particle tick. Builds rotation matrix from entity
// orientation angles, transforms foot bone offset to world space, spawns
// animated particle at feet. Adjusts particle opacity based on entity
// velocity magnitude (faster movement = more visible dust).
// Destroys particle when entity stops or opacity drops to zero.
// ============================================================================
void character_foot_particle_tick(u32* pCharSlot) {
    if ((pCharSlot[1] & 0x100) == 0) {
        return; // Foot particle not enabled
    }

    int pEntity = *(int*)pCharSlot[0];

    // Advance time counter
    float frameDelta = g_frame_rate_scale *
                       (1.0f / (float)((double)frame_counter_combined - DOUBLE_OFFSET));
    pCharSlot[4] = (int)((float)pCharSlot[4] + frameDelta);

    // Compute foot position: rotation matrix from entity angles
    float scale = 0.1f; // FLOAT_806153b8
    float invFull = 0.05f; // FLOAT_806153b4 (1/20 scale factor)
    float rotX = invFull * *(float*)(pEntity + 0xc0) / scale;
    float rotY = invFull * *(float*)(pEntity + 0x70) / scale;
    float rotZ = invFull * *(float*)(pEntity + 0xc8) / scale;

    float rotMatrix[17];
    build_rotation_matrix(rotX, rotY, rotZ, rotMatrix); // FUN_8012f154

    // Base foot offset: Y=some height, X/Z=0
    float footPos[3] = {0.0f, FOOT_Y_OFFSET, 0.0f}; // FLOAT_80615490
    matrix_transform_point(rotMatrix, footPos);

    // Add entity world position + velocity offset
    footPos[0] += *(float*)(pEntity + 0x64) + *(float*)(pEntity + 0x1a8);
    footPos[1] += *(float*)(pEntity + 0x68) + *(float*)(pEntity + 0x1ac);
    footPos[2] += *(float*)(pEntity + 0x6c) + *(float*)(pEntity + 0x1b0);

    // Check if entity is grounded (flag 0x80000000 in +0x10)
    if ((*(u32*)(pEntity + 0x10) & 0x80000000) != 0) {
        // Spawn or respawn particle if needed
        if (pCharSlot[6] == 0 || particle_slot_is_alive((int*)pCharSlot[6]) != 0) {
            if (pCharSlot[6] != 0) {
                *(int*)(pCharSlot[6] + 0x14) = 0; // Kill old
            }
            pCharSlot[6] = particle_spawn_animated(pCharSlot + 6, 0x40, 0, footPos);
            pCharSlot[4] = 0; // Reset timer
        }
    }

    // Update existing particle opacity based on velocity
    if (pCharSlot[6] != 0) {
        float opacity = 0.0f;

        if ((float)pCharSlot[4] < FOOT_FADE_TIME) { // FLOAT_80615494
            // Opacity scales with movement speed
            float velMag = fabsf(*(float*)(pEntity + 0x100)) +
                          fabsf(*(float*)(pEntity + 0xf8)) +
                          fabsf(*(float*)(pEntity + 0xfc));
            opacity = FOOT_VEL_SCALE * velMag + 0.5f; // FLOAT_80615498
            if (opacity > FOOT_MAX_OPACITY) {         // FLOAT_8061546c
                opacity = FOOT_MAX_OPACITY;
            }
        }

        particle_slot_set_opacity(opacity, (int*)pCharSlot[6]);

        if (opacity <= 0.0f) {
            // Kill particle
            *(int*)(pCharSlot[6] + 0x14) = 0;
            pCharSlot[6] = 0;
        } else {
            particle_slot_set_position(pCharSlot[6], footPos);
        }
    }
}

// ============================================================================
// Address: 0x800f74e4 | Name: character_spawn_blood_particle
// Spawns blood/debris particle on character hit. Computes random positions
// around the character (scaled by distance from damage source), random
// velocities, and spawns entity-attached particle (type 0xd).
// Sets collision shape for debris physical interaction.
// ============================================================================
void character_spawn_blood_particle(int* pCharSlot) {
    if (g_frame_counter_raw <= 0) {
        return; // Skip on frame 0
    }

    int* pCharData = (int*)pCharSlot[0];
    int pEntity = *pCharData;

    // Compute distance from damage source
    float dx = (float)pCharData[0x2ed] - *(float*)(pEntity + 0x64);
    float dy = (float)pCharData[0x2ee] - *(float*)(pEntity + 0x68);
    float dz = (float)pCharData[0x2ef] - *(float*)(pEntity + 0x6c);
    float dist = sqrtf(dx * dx + dz * dz); // FUN_80131538

    // Random spawn position around character
    float spawnPos[3];
    float rnd1 = rng_float(); // FUN_801310c0
    float radiusScale = 0.5f * dist + 0.25f; // FLOAT_80615404, FLOAT_806153e0
    spawnPos[0] = radiusScale * (0.5f * rnd1 - 1.0f) + *(float*)(pEntity + 0x64);

    float rnd2 = rng_float();
    spawnPos[1] = dy * rnd2 + *(float*)(pEntity + 0x68);

    float rnd3 = rng_float();
    spawnPos[2] = radiusScale * (0.5f * rnd3 - 1.0f) + *(float*)(pEntity + 0x6c);

    // Random velocity
    float velocity[3];
    float rnd4 = rng_float();
    velocity[0] = BLOOD_VEL_XZ * (rnd4 - 2.0f); // FLOAT_8061549c

    float rnd5 = rng_float();
    velocity[1] = BLOOD_VEL_Y * (rnd5 - 0.75f); // FLOAT_806154a0

    float rnd6 = rng_float();
    velocity[2] = BLOOD_VEL_XZ * (rnd6 - 2.0f);

    // Spawn entity-attached particle (type 0xd = blood splatter)
    particle_spawn_entity_attached(1.0f, 0.0f, 2.0f, 0xd,
                                   pCharSlot[0], spawnPos, velocity);

    // Set collision shape for splatter interaction
    collision_set_shape_params(*(int**)(pEntity + 4), 0x13, -1, 4, 0x4d);
}

// ============================================================================
// Address: 0x800f764c | Name: character_blood_effect_tick
// Per-frame blood/wound effect tick. Advances timer while flag 0x8 is active.
// When wound state reaches 0 (fresh wound), triggers blood particle spawn.
// ============================================================================
void character_blood_effect_tick(int* pCharSlot) {
    if ((pCharSlot[1] & 0x8) == 0) {
        return; // Blood effect not active
    }

    // Advance blood timer
    float frameDelta = g_frame_rate_scale *
                       (1.0f / (float)((double)frame_counter_combined - DOUBLE_OFFSET));
    pCharSlot[0x1e] = (int)((float)pCharSlot[0x1e] + frameDelta);

    // Check wound state - spawn blood on fresh hit
    if (pCharSlot[0x1d] == 0) {
        character_spawn_blood_particle(pCharSlot);
    }
}
