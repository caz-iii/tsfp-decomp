// =============================================================================
// Player AI Projectile Operations
// Address Range: 0x800ba2e0 - 0x800baa7c
// Date: 2026-08-11
// Batch: 263
//
// This module handles projectile slot operations including:
//   - Death/visibility dispatch (mode-dependent)
//   - Proximity detonation (LOS-gated)
//   - Type-based counting (owned, targeted, in-radius)
//   - Shockwave visual effects + camera shake
//   - Detonation sequence (sound stop + explosion + surface FX)
//   - Render highlight for owned projectile entities
// =============================================================================

#include "common.h"
#include "player_ai.h"
#include "particle.h"
#include "sound.h"
#include "scene.h"

// Pool globals
extern int DAT_80611dc0;  // projectile slot pool base
extern int DAT_80611d78;  // player data array base
extern char DAT_805015bd; // game mode flag ('\n' = story/single)

// =============================================================================
// Address: 0x800ba2e0 | player_ai_projectile_death_dispatch
// Dispatches projectile slot cleanup on entity death.
// Story mode: performs raycast visibility check before cleanup.
// Arcade mode: immediate death (zero velocity + deactivate).
// Called by 10+ callers: damage_entity_receive, entity_destroy,
// player_ai_on_death_cleanup, weapon_dropped_start_effects, etc.
// =============================================================================
void player_ai_projectile_death_dispatch(void)
{
    if (DAT_805015bd == '\n') {
        player_ai_projectile_raycast_visibility();
    } else {
        player_ai_projectile_on_entity_death();
    }
}

// =============================================================================
// Address: 0x800ba31c | player_ai_projectile_proximity_detonate
// Finds projectile slots within a given radius of an entity, with LOS check.
// If a slot is within range and has clear line-of-sight (no scene obstruction):
//   - For type 0x12 (missile): sets minimum countdown to 3 if less
//   - For all other types: immediately detonates via FUN_800ba964
// Uses scene_raycast with mask 0x2000070 for obstruction check.
// Checks player_ai_char_def_get_flags for flag 0x200 (proximity-enabled).
// =============================================================================
void player_ai_projectile_proximity_detonate(void)
{
    float* entity_pos = (float*)FUN_803826e4();
    float radius_sq; // passed via FPR - squared detection radius

    int start = player_ai_projectile_pool_start_index();
    int end = player_ai_projectile_pool_end_index();
    int offset = start * 0x22c;

    for (int i = start; i <= end; i++) {
        int* slot = (int*)(DAT_80611dc0 + offset);
        if (slot[0] != -1) {
            unsigned int flags = player_ai_char_def_get_flags(slot[0]);
            if (flags & 0x200) { // proximity-detonation enabled
                float dx = entity_pos[0] - (float)slot[7];
                float dy = entity_pos[1] - (float)slot[8];
                float dz = entity_pos[2] - (float)slot[9];
                float dist_sq = dx*dx + dy*dy + dz*dz;

                if (dist_sq < radius_sq) {
                    // LOS check: ray from slot to entity
                    float ray_dir[3] = {dx, dy, dz};
                    int hit = scene_raycast(slot[0x3c], slot + 7, ray_dir, 0,
                                            NULL, NULL, NULL, 0x2000070, 0, NULL);
                    if (hit == 0) { // clear LOS
                        if (slot[0] == 0x12) {
                            // Missile: set minimum countdown
                            if (slot[0x5e] < 3) {
                                slot[0x5e] = 3;
                            }
                        } else {
                            // Immediate detonation
                            FUN_800ba964(/* ... */ slot);
                            return; // early exit after first detonation
                        }
                    }
                }
            }
        }
        offset += 0x22c;
    }
}

// =============================================================================
// Address: 0x800ba46c | player_ai_projectile_count_by_type
// Increments per-type counter in a 7-int array based on slot type.
// Counter layout:
//   [0] = type 0x12 (homing missile)
//   [1] = type 0x10 (grenade)
//   [2] = type 0x11 (rocket)
//   [3] = type 0x13 (guided missile with sound)
//   [4] = type 0x0e (proximity mine)
//   [5] = type 0x20 (homing projectile)
//   [6] = type 0x21 (guided projectile)
// =============================================================================
void player_ai_projectile_count_by_type(int* counters, int slot_type)
{
    switch (slot_type) {
        case 0x12: counters[0]++; break;
        case 0x10: counters[1]++; break;
        case 0x11: counters[2]++; break;
        case 0x13: counters[3]++; break;
        case 0x0e: counters[4]++; break;
        case 0x20: counters[5]++; break;
        case 0x21: counters[6]++; break;
        default: break;
    }
}

// =============================================================================
// Address: 0x800ba500 | player_ai_projectile_count_owned
// Counts all active projectile slots owned by a specific entity, grouped
// by type. Zeroes the 7-int counter array, then iterates pool.
// =============================================================================
void player_ai_projectile_count_owned(void)
{
    int entity; // passed via register
    int* counters; // output array

    memset(counters, 0, 0x1c); // 7 ints = 28 bytes

    int start = player_ai_projectile_pool_start_index();
    int end = player_ai_projectile_pool_end_index();
    int offset = start * 0x22c;

    for (int i = start; i <= end; i++) {
        int* slot = (int*)(DAT_80611dc0 + offset);
        if (slot[0] != -1 && slot[0x56] == entity) {
            player_ai_projectile_count_by_type(counters, slot[0]);
        }
        offset += 0x22c;
    }
}

// =============================================================================
// Address: 0x800ba594 | player_ai_projectile_count_targeted
// Counts all active projectile slots targeting a specific entity pair
// (owner + target fields at [0x56] and [0x57]), grouped by type.
// =============================================================================
void player_ai_projectile_count_targeted(int param_1, int param_2, int* counters)
{
    int owner;  // from register
    int target; // from register

    memset(counters, 0, 0x1c);

    int start = player_ai_projectile_pool_start_index();
    int end = player_ai_projectile_pool_end_index();
    int offset = start * 0x22c;

    for (int i = start; i <= end; i++) {
        int* slot = (int*)(DAT_80611dc0 + offset);
        if (slot[0] != -1 && slot[0x56] == owner && slot[0x57] == target) {
            player_ai_projectile_count_by_type(counters, slot[0]);
        }
        offset += 0x22c;
    }
}

// =============================================================================
// Address: 0x800ba638 | player_ai_projectile_count_in_radius
// Counts all active projectile slots within a distance of an entity's
// position (using entity fields at +0x64/0x68/0x6c as XYZ position).
// Squared-distance comparison for efficiency. Output to 7-int counter array.
// =============================================================================
void player_ai_projectile_count_in_radius(void)
{
    int entity;     // passed via register
    int* counters;  // output array
    float radius_sq; // passed via FPR (squared)

    // Zero all 7 counters
    for (int j = 0; j < 7; j++) counters[j] = 0;

    int start = player_ai_projectile_pool_start_index();
    int end = player_ai_projectile_pool_end_index();
    int offset = start * 0x22c;

    for (int i = start; i <= end; i++) {
        int* slot = (int*)(DAT_80611dc0 + offset);
        if (slot[0] != -1) {
            float dx = *(float*)(entity + 0x64) - (float)slot[7];
            float dy = *(float*)(entity + 0x68) - (float)slot[8];
            float dz = *(float*)(entity + 0x6c) - (float)slot[9];
            if (dx*dx + dy*dy + dz*dz < radius_sq) {
                player_ai_projectile_count_by_type(counters, slot[0]);
            }
        }
        offset += 0x22c;
    }
}

// =============================================================================
// Address: 0x800ba728 | player_ai_projectile_shockwave_effect
// Spawns shockwave visual effect when a proximity mine (type 0x0e) detonates:
//   1. Directional debris particle (type 0x69) along normal
//   2. Radial particle ring (type 0x6a) using euler decomposition
//   3. Camera shake for all players within range (FLOAT_80614a94 radius)
//      with distance-based intensity falloff
// =============================================================================
void player_ai_projectile_shockwave_effect(void)
{
    int* slot; // from register
    float* position; // from register

    // 1. Directed debris particle along slot normal
    float scaled_normal[3];
    scaled_normal[0] = 0.5f * *(float*)(slot + 0x60);
    scaled_normal[1] = 0.5f * *(float*)(slot + 0x61);
    scaled_normal[2] = 0.5f * *(float*)(slot + 0x62);

    int mode = FUN_801c1714();
    unsigned int pflags = (mode == 1) ? 0 : 0x80000;
    particle_spawn_directed(0, 0x69, pflags, position, scaled_normal);

    // 2. Radial particle ring from euler decomposition of normal
    float euler[3];
    FUN_80130730(/* temp_matrix */, (float*)(slot + 0x60));
    FUN_80130438(/* temp_matrix */, &euler[0], &euler[1], &euler[2]);
    // Scale euler angles for ring emission
    euler[0] = (FLOAT_80614b6c * euler[0]) / FLOAT_80614c28;
    euler[1] = (FLOAT_80614b6c * euler[1]) / FLOAT_80614c28;
    euler[2] = (FLOAT_80614b6c * euler[2]) / FLOAT_80614c28;

    float ring_pos[3];
    ring_pos[0] = position[0] + scaled_normal[0];
    ring_pos[1] = position[1] + scaled_normal[1];
    ring_pos[2] = position[2] + scaled_normal[2];

    mode = FUN_801c1714();
    pflags = (mode == 1) ? 0 : 0x80000;
    weather_radial_particle_spawn(3.0f, 1.0f, 0, 0x6a, pflags,
                                   ring_pos, euler, /* size_params */, 0xffff);

    // 3. Camera shake for nearby players
    int player_count = FUN_801c1714();
    int player_offset = 0;
    for (int i = 0; i < player_count; i++) {
        int player = DAT_80611d78 + player_offset;
        if (player != 0) {
            float dx = position[0] - *(float*)(player + 0xb4);
            float dy = position[1] - *(float*)(player + 0xb8);
            float dz = position[2] - *(float*)(player + 0xbc);
            float dist_sq = dx*dx + dy*dy + dz*dz;

            if (dist_sq < FLOAT_80614a94) { // shockwave radius
                player_apply_camera_shake(0.5f, FLOAT_80614af8, FLOAT_80614aa0, player);
                // Set screen-shake parameters
                *(float*)(player + 0xe54) = FLOAT_80614c14;
                float intensity = FLOAT_80614bcc - (dist_sq / FLOAT_80614a94);
                if (intensity > FLOAT_80614b1c) intensity = FLOAT_80614b1c;
                *(float*)(player + 0xe58) = intensity;
            }
        }
        player_offset += 0x141c;
    }
}

// =============================================================================
// Address: 0x800ba964 | player_ai_projectile_deactivate_with_sound
// Pre-detonation cleanup: if slot is type 0x13 (guided missile with looping
// sound), stops the sound handle at field [0x6c]. Then calls the main
// detonation function.
// =============================================================================
void player_ai_projectile_deactivate_with_sound(/* ... */ int* slot)
{
    if (slot[0] == 0x13 && slot[0x6c] >= 0) {
        sound_stop_handle(slot[0x6c]);
        slot[0x6c] = -1;
    }
    player_ai_projectile_detonate(/* ... */);
}

// =============================================================================
// Address: 0x800ba9bc | player_ai_projectile_detonate
// Main detonation sequence for a projectile slot:
//   1. Selects explosion scale by type (0x13→4, 0x0e→8, others→6)
//   2. For type 0x0e (prox mine): spawns shockwave visual effect
//   3. Calls player_ai_explosion_spawn with position/normal/entity/room
//   4. Spawns surface impact effect at detonation point
//   5. In story mode: calls FUN_80222780 (mission event trigger)
//   6. Deactivates the projectile slot
// =============================================================================
void player_ai_projectile_detonate(void)
{
    int* slot; // from register

    int scale;
    if (slot[0] == 0x13) {
        scale = 4;
    } else if (slot[0] == 0x0e) {
        scale = 8;
        player_ai_projectile_shockwave_effect();
    } else {
        scale = 6;
    }

    // Spawn explosion at slot position
    player_ai_explosion_spawn(scale, slot[0], (float*)slot /* position */,
                               (float*)(slot + 0x5b) /* normal */,
                               slot[0x43] /* entity */, 0, 1);

    // Surface impact effect
    FUN_800e95fc(slot[0], 0, slot[0x3c], 0, (float*)slot, NULL);

    // Story mode: trigger mission event
    if (DAT_805015bd == '\n') {
        FUN_80222780(/* ... */ slot);
    }

    player_ai_slot_deactivate();
}

// =============================================================================
// Address: 0x800baa7c | player_ai_projectile_highlight_ownership
// Sets or clears render highlight flag (bit 30 = 0x40000000) on entities
// associated with projectile slots (fields [0x2a], [99], [100]).
//
// For slots owned by param_1: sets the highlight flag (visible to that player).
// For all other slots: clears the highlight flag.
// If param_1 == 0: clears highlight on ALL slots.
//
// Called by camera_render_dispatch — used to highlight projectiles belonging
// to the currently rendering player in split-screen.
// =============================================================================
void player_ai_projectile_highlight_ownership(int owner_entity)
{
    int start = player_ai_projectile_pool_start_index();
    int end = player_ai_projectile_pool_end_index();
    int count = (end + 1) - start;
    int offset = start * 0x22c;

    for (int i = 0; i < count; i++) {
        int* slot = (int*)(DAT_80611dc0 + offset);
        if (slot[0] != -1) {
            if (owner_entity == 0 || slot[0x56] != owner_entity) {
                // Clear highlight on associated entities
                int ent_a = slot[0x2a];
                if (ent_a != 0) *(unsigned int*)(ent_a + 0x2b4) &= ~0x40000000;
                int ent_b = slot[99];
                if (ent_b != 0) *(unsigned int*)(ent_b + 0x2b4) &= ~0x40000000;
                int ent_c = slot[100];
                if (ent_c != 0) *(unsigned int*)(ent_c + 0x2b4) &= ~0x40000000;
            } else {
                // Set highlight on owned slot's entities
                int ent_a = slot[0x2a];
                if (ent_a != 0) *(unsigned int*)(ent_a + 0x2b4) |= 0x40000000;
                int ent_b = slot[99];
                if (ent_b != 0) *(unsigned int*)(ent_b + 0x2b4) |= 0x40000000;
                int ent_c = slot[100];
                if (ent_c != 0) *(unsigned int*)(ent_c + 0x2b4) |= 0x40000000;
            }
        }
        offset += 0x22c;
    }
}
