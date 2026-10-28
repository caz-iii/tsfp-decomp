// Address: 0x80098228-0x80098dd4 | Name: gib_system | Date: 2026-08-11
// Gib/debris entity system: spawning, trail effects, lifecycle, and pickup collection.
// 10-slot ring buffer for trails, 40-slot pool for type-5 gibs, per-weapon-slot block alloc for others.

#include "common.h"
#include "entity.h"
#include "event.h"
#include "memory.h"
#include "sound.h"
#include "spawn.h"
#include "collision.h"
#include "math_util.h"
#include "particle.h"

// ============================================================================
// Globals
// ============================================================================

static int g_gib_trail_ring_index;                  // DAT_80611ce0 — current write position in ring
static void* g_gib_trail_entities[10];              // DAT_80561a20 — 10-slot entity ring buffer
static float g_gib_trail_lifetimes[10];             // DAT_80561a48 — per-slot accumulated lifetime
static int g_gib_pool_active_count;                 // DAT_80611cec — active gib count in type-5 pool
static void* g_gib_pool_base;                       // DAT_80611ce8 — base pointer to 0x14A0-byte pool

#define GIB_TRAIL_RING_SIZE   10
#define GIB_POOL_SLOT_SIZE    0x84
#define GIB_POOL_MAX_SLOTS    40    // 0x28
#define GIB_POOL_TOTAL_SIZE   0x14A0

// ============================================================================
// 0x80098354 — gib_ring_buffer_reset
// Resets the 10-slot trail ring buffer. Called from spawn_system_reset.
// ============================================================================
void gib_ring_buffer_reset(void) {
    g_gib_trail_ring_index = 0;
    for (int i = 0; i < GIB_TRAIL_RING_SIZE; i++) {
        g_gib_trail_entities[i] = NULL;
    }
}

// ============================================================================
// 0x80098920 — gib_system_alloc
// Allocates gib system memory pool (0x14A0 bytes) and clears it.
// ============================================================================
void gib_system_alloc(void) {
    g_gib_pool_base = mem_alloc(GIB_POOL_TOTAL_SIZE, 1);
    gib_system_reset();
}

// ============================================================================
// 0x80098950 — gib_system_reset
// Clears gib pool: resets active count and zeroes pool memory.
// ============================================================================
void gib_system_reset(void) {
    g_gib_pool_active_count = 0;
    memset(g_gib_pool_base, 0, GIB_POOL_TOTAL_SIZE);
}

// ============================================================================
// 0x800988dc — gib_register_event_handlers
// Registers gib system event handlers on event type 0x800:
//   slot 1 = on_destroy handler
//   slot 4 = tick_update handler
// ============================================================================
void gib_register_event_handlers(void) {
    event_set_handler(0x800, 1, gib_on_destroy_handler);
    event_set_handler(0x800, 4, gib_tick_update);
}

// ============================================================================
// 0x80098984 — gib_on_destroy
// Destroy callback for gib entities. Used as block_alloc destructor.
// ============================================================================
void gib_on_destroy(void* entity) {
    if (entity != NULL) {
        entity_destroy(entity);
    }
}

// ============================================================================
// 0x80098228 — gib_find_nearest_pickup
// Finds nearest debris entity (state==2) within distance threshold, then
// triggers the pickup collection pipeline (score, sound, HUD notification).
// Called from story scripting contexts.
// ============================================================================
void gib_find_nearest_pickup(int player_entity) {
    int best_index = -1;
    float best_dist_sq = 99999.0f;  // large initial threshold
    int* player_data = *(int**)(player_entity + 0x7C);

    for (int i = 0; i < g_gib_pool_active_count; i++) {
        int slot = (int)g_gib_pool_base + i * 0xD8;

        float dx = *(float*)(slot + 0x20) - *(float*)(player_data + 0x30);
        float dy = *(float*)(slot + 0x24) - *(float*)(player_data + 0x34);
        float dz = *(float*)(slot + 0x28) - *(float*)(player_data + 0x38);
        float dist_sq = dx * dx + dy * dy + dz * dz;

        if (dist_sq < best_dist_sq && *(int*)(slot + 0x0C) == 2) {
            best_index = i;
            best_dist_sq = dist_sq;
        }
    }

    if (best_index != -1) {
        int slot = best_index * 0xD8;
        int slot_base = (int)g_gib_pool_base + slot;

        // Award sequence: camera effect, score, equip, collect
        FUN_801a9780();  // camera/screen effect
        FUN_801a94e0();  // score award
        FUN_801a95b8(player_data, *(int*)(slot_base + 0x14));  // equip item
        FUN_801a9384();  // finalize
        spawn_pickup_collected(*(int*)(slot_base));  // respawn timer + deactivation
        FUN_801a9a8c(player_data,
                     *(int*)(slot_base + 0x40),
                     (short)*(int*)(slot_base + 0x44));  // HUD notification
    }
}

// ============================================================================
// 0x80098380 — gib_trail_spawn
// Spawns a gib trail particle entity in the 10-slot ring buffer.
// Uses paired-single math for per-vertex color generation from sin table,
// randomized color channel offsets (R/G/B from random angles), and applies
// a 3D positioned sound entity. Destroys oldest trail if slot occupied.
// Ring advances and wraps at index 10.
// ============================================================================
void gib_trail_spawn(void) {
    float* camera_pos = get_camera_position();
    float spawn_pos[3] = { g_trail_spawn_x, g_trail_spawn_y, g_trail_spawn_z };

    // Destroy existing trail in current ring slot if occupied
    if (g_gib_trail_entities[g_gib_trail_ring_index] != NULL) {
        sound_entity_destroy(g_gib_trail_entities[g_gib_trail_ring_index]);
    }

    // Reset lifetime for this slot
    g_gib_trail_lifetimes[g_gib_trail_ring_index] = 0.0f;

    // Create new 3D sound entity (type 0x17)
    void* trail_entity = sound_entity_create(
        &g_gib_trail_entities[g_gib_trail_ring_index],
        0x17, 0, &spawn_pos);
    g_gib_trail_entities[g_gib_trail_ring_index] = trail_entity;

    if (trail_entity == NULL) goto done;

    float* render_data = *(float**)(trail_entity + 0x18);
    int vertex_count = *(int*)(trail_entity + 0x1C);

    // Determine color channel offsets based on random & 3
    int color_variant = random_int() & 3;
    float r_offset, g_offset, b_offset;
    switch (color_variant) {
        case 0: case 3:
            r_offset = TRAIL_COLOR_R_DEFAULT;
            g_offset = TRAIL_COLOR_G_HIGH;
            b_offset = TRAIL_COLOR_B_LOW;
            break;
        case 1:
            r_offset = TRAIL_COLOR_R_ALT;
            g_offset = TRAIL_COLOR_G_HIGH;
            b_offset = TRAIL_COLOR_R_ALT;
            break;
        case 2:
            r_offset = TRAIL_COLOR_R_MED;
            g_offset = TRAIL_COLOR_G_LOW;
            b_offset = TRAIL_COLOR_R_MED;
            break;
    }

    // Initialize per-vertex positions and colors from sin table
    render_data[0x204] = 0.0f;  // animation timer

    for (int v = 0; v < vertex_count; v++) {
        float t = (float)v / (float)vertex_count;
        float angle_r = random_float();
        float angle_g = random_float();
        float angle_b = random_float();

        // Position: radius from sin curve * trail_radius
        float sin_phase = sin_table_lookup((int)(t * t * t * TRAIL_CURVE_SCALE));
        int offset_pos = v * 0xC;
        int offset_color = v * 4;

        render_data[(0x40 + offset_pos) / 4]     = TRAIL_RADIUS * sin_table[(sin_phase + 0x200) & 0x7FF];
        render_data[(0x44 + offset_pos) / 4]     = 0.0f;
        render_data[(0x48 + offset_pos) / 4]     = TRAIL_RADIUS * sin_table[sin_phase & 0x7FF];

        // Scale factor per vertex
        render_data[(0x680 + offset_color) / 4]  = (TRAIL_SCALE_BASE * (1.0f - t)) / TRAIL_SCALE_DIV;

        // Packed RGBA color
        int r = clamp_byte(angle_b);
        int g = clamp_byte(angle_r);
        int b = clamp_byte(angle_g);
        *(int*)&render_data[(0x4F0 + offset_color) / 4] = (b << 16) | r | (g << 8) | 0x7F000000;
    }

    // Apply particle rendering setup
    particle_setup_render(render_data);

    // Set transform from camera position
    float trail_scale = TRAIL_ENTITY_SCALE;
    render_data[0x0C] = camera_pos[0];
    render_data[0x0D] = camera_pos[1];
    render_data[0x0E] = camera_pos[2];
    render_data[0x00] = trail_scale;
    render_data[0x05] = 1.0f;
    render_data[0x0A] = trail_scale;

done:
    // Advance ring buffer
    g_gib_trail_ring_index++;
    if (g_gib_trail_ring_index >= GIB_TRAIL_RING_SIZE) {
        g_gib_trail_ring_index = 0;
    }
    stack_restore();
}

// ============================================================================
// 0x80098794 — gib_trail_tick
// Per-frame update for gib trail entities. Iterates all 10 ring buffer slots,
// applies scale/rotation transforms, accumulates lifetime via delta_time,
// and destroys trails exceeding max lifetime.
// ============================================================================
void gib_trail_tick(void) {
    stack_save();

    float dt = g_frame_delta_time * (1.0f / (float)g_ticks_per_second);

    for (int i = 0; i < GIB_TRAIL_RING_SIZE; i++) {
        if (g_gib_trail_entities[i] == NULL) continue;

        float* render_data = *(float**)(g_gib_trail_entities[i] + 0x18);

        // Apply rotation/scale transforms
        float rot_scale = (TRAIL_ROT_SPEED * dt) / TRAIL_ROT_DIVISOR;
        matrix_rotate_y(rot_scale, render_data);  // FUN_8012f05c
        float expand = TRAIL_EXPAND_RATE * dt + 1.0f;
        matrix_scale_uniform(expand, 1.0f, expand);  // FUN_8012f62c
        matrix_multiply(render_data, render_data);    // FUN_80130bd4 x2

        // Accumulate lifetime
        g_gib_trail_lifetimes[i] += dt;

        // Destroy if exceeded max lifetime
        if (g_gib_trail_lifetimes[i] > TRAIL_MAX_LIFETIME) {
            sound_entity_destroy(g_gib_trail_entities[i]);
            g_gib_trail_entities[i] = NULL;
        }
    }

    stack_restore();
}

// ============================================================================
// 0x800989ac — gib_spawn_entity
// MASSIVE gib/debris entity spawner. Called from damage_death_anim_dispatch
// and weapon effect callbacks.
//
// For type-5 gibs: uses 40-slot pool (g_gib_pool_base, 0x84 bytes per slot).
// For per-weapon-slot gibs (slot 0-4): uses block_alloc_dispatch with
//   gib_on_destroy as destructor.
//
// Assigns randomized velocity direction, initial scale/position (optional
// override from param_11), plays pickup sound 0xd57 for slot 0,
// and sets room/visibility/render flags on the spawned entity.
// ============================================================================
void gib_spawn_entity(int* position_override, int owner_entity, int weapon_slot) {
    stack_save();

    int owner = owner_entity;
    int* owner_data = *(int**)(owner + 0x7C);

    float spawn_pos[3];
    float spawn_dir[3];

    // Generate randomized direction
    float dir_x, dir_y, dir_z;
    if (position_override == NULL) {
        // Random direction from RNG
        dir_x = (random_float() / RAND_SCALE - RAND_OFFSET) * GIB_DIR_SCALE;
        dir_y = (random_float() / RAND_SCALE - RAND_OFFSET) * GIB_DIR_SCALE;
        dir_z = (random_float() / RAND_SCALE - RAND_OFFSET) * GIB_DIR_SCALE;
    } else {
        dir_x = position_override[0];
        dir_y = position_override[1];
        dir_z = position_override[2];
    }

    // Check if this slot is already active (bitmask check)
    if (weapon_slot == 5) {
        if ((*(unsigned int*)(owner_data + 0xBE8) & 0x20) != 0) goto done;
        int spawn_type_id = *(int*)(owner_data + 0xBDC);
        if (spawn_type_id == -1) goto done;

        // Allocate from 40-slot pool
        int slot_index;
        if (g_gib_pool_active_count < GIB_POOL_MAX_SLOTS) {
            slot_index = g_gib_pool_active_count;
            g_gib_pool_active_count++;
        } else {
            // Find first inactive slot
            slot_index = -1;
            for (int s = 0; s < GIB_POOL_MAX_SLOTS; s++) {
                if (*(int*)((int)g_gib_pool_base + s * GIB_POOL_SLOT_SIZE + 0x44) == 0) {
                    slot_index = s;
                    break;
                }
            }
            if (slot_index == -1) goto done;
        }

        int entity = entity_spawn_by_type(spawn_type_id, owner + 100, &spawn_pos, 0, 0);
        *(int*)(entity + 0x7C) = (int)g_gib_pool_base + slot_index * GIB_POOL_SLOT_SIZE;
    } else {
        // Per-weapon-slot allocation
        if ((*(unsigned short*)(owner + 0x10) == 0) ||
            (*(unsigned int*)(owner_data + 0xBE8) & (1 << weapon_slot)) != 0) {
            goto done;
        }
        int spawn_type_id = *(int*)(owner_data + weapon_slot * 4 + 0xBC8);
        if (spawn_type_id == -1) goto done;

        int entity = entity_spawn_by_type(spawn_type_id, owner + 100, &spawn_pos, 0, 0);
        void* alloc_data = block_alloc_dispatch(GIB_POOL_SLOT_SIZE, 1, entity, gib_on_destroy);
        *(void**)(entity + 0x7C) = alloc_data;
    }

    // Mark slot as active in owner's bitmask
    *(unsigned int*)(owner_data + 0xBE8) |= (1 << weapon_slot);

    // Initialize gib state data
    int* gib_data = *(int**)(entity + 0x7C);
    *(int*)(gib_data + 0x40) = 0;          // timer
    *(float*)(gib_data + 0x4C) = GIB_INITIAL_SCALE;
    *(int*)(gib_data + 0x48) = 0;          // state flags
    *(int*)(gib_data + 0x44) = weapon_slot; // slot index
    *(int*)(gib_data + 0x68) = owner;       // owner entity reference
    *(float*)(gib_data + 0x6C) = dir_x * GIB_VELOCITY_SCALE;
    *(float*)(gib_data + 0x70) = dir_y * GIB_VELOCITY_SCALE;
    *(float*)(gib_data + 0x74) = dir_z * GIB_VELOCITY_SCALE;

    // Set initial world position
    if (position_override == NULL) {
        *(float*)(gib_data + 0x78) = *(float*)(entity + 100) + *(float*)(entity + 0x1A8);
        *(float*)(gib_data + 0x7C) = *(float*)(entity + 0x68) + *(float*)(entity + 0x1AC);
        *(float*)(gib_data + 0x80) = *(float*)(entity + 0x6C) + *(float*)(entity + 0x1B0);
    } else {
        *(float*)(gib_data + 0x78) = *(float*)position_override;
        *(float*)(gib_data + 0x7C) = *(float*)(position_override + 1);
        *(float*)(gib_data + 0x80) = *(float*)(position_override + 2);
    }

    // Initial scale values (all same)
    float init_scale = GIB_RENDER_SCALE;
    *(float*)(gib_data + 0x5C) = init_scale;
    *(float*)(gib_data + 0x60) = init_scale;
    *(float*)(gib_data + 0x64) = init_scale;
    *(float*)(gib_data + 0x50) = init_scale;
    *(float*)(gib_data + 0x54) = init_scale;
    *(float*)(gib_data + 0x58) = init_scale;

    // Set render flags: clear bit 0, set bit 30 (non-collidable, force-render)
    *(unsigned int*)(entity + 0x28) &= ~0x1;
    *(unsigned int*)(entity + 0x28) |= 0x40000000;

    // Play pickup sound for slot 0
    if (weapon_slot == 0) {
        int sound_params[13];
        sound_init_play_params(sound_params);
        sound_params[0] = 0xD57;  // gib spawn sound
        sound_params[1] = 5;      // priority
        sound_play_request(sound_params);
    }

    // Assign room from owner (special case for level 0x65)
    if (g_current_level_id == 0x65) {
        *(int*)(entity + 0x34) = 0xD5;
    } else {
        *(int*)(entity + 0x34) = *(int*)(*(int*)(gib_data + 0x68) + 0x34);
    }

done:
    stack_restore();
}

// ============================================================================
// 0x80098dd4 — gib_spawn_particles
// Spawns visual particle effects for a gib entity.
// Applies rotation matrix from entity angle, spawns 3 randomized offset
// particles using transform helpers, and creates a positioned particle burst.
// Called from gib_tick_update during the launch state.
// ============================================================================
void gib_spawn_particles(void) {
    stack_save();

    int entity = get_current_entity();
    float* entity_pos = (float*)entity;
    float* gib_data = *(float**)(entity + 0x7C);
    float angle = gib_data[0x1A];  // current rotation angle

    // Setup rotation axis vectors
    float axis_a[3] = { PARTICLE_AXIS_A };
    float axis_b[3] = { PARTICLE_AXIS_B };
    matrix_from_angle_axis(angle, *(int*)(entity + 300), &axis_a, &axis_b);

    // Base direction
    float dir[3] = { GIB_RENDER_SCALE, PARTICLE_Y_OFFSET, GIB_RENDER_SCALE };
    matrix_transform_vector(gib_data, &dir);
    particle_emit_positioned(entity + 100, &dir, *(int*)(entity + 0x34), 2);

    // Spawn 3 randomized offset particles
    for (int i = 0; i < 3; i++) {
        dir[0] = (random_float() / RAND_SCALE - RAND_OFFSET) * PARTICLE_SCATTER;
        dir[1] = (random_float() / RAND_SCALE - RAND_OFFSET) * PARTICLE_SCATTER;
        dir[2] = (random_float() / RAND_SCALE - RAND_OFFSET) * PARTICLE_SCATTER;
        particle_emit_positioned(entity_pos, &dir, *(int*)(entity + 0x34), 2);
    }

    // Create positioned particle burst at entity location
    float burst_pos[3] = { entity_pos[0], entity_pos[1], entity_pos[2] };
    float burst_params[3] = { BURST_PARAM_A, BURST_PARAM_B, BURST_PARAM_C };
    float burst_colors[3] = { BURST_PARAM_A, BURST_PARAM_B, BURST_PARAM_C };
    particle_burst_create(1, (int)angle, 0, &burst_pos, &burst_colors);

    stack_restore();
}

// ============================================================================
// 0x80098d70 — gib_on_destroy_handler (event handler for slot 1)
// Handles gib entity destruction event. If gib is in state 5 (attached),
// clears the owner's attachment reference. Otherwise frees the block.
// ============================================================================
void gib_on_destroy_handler(int event_data) {
    if (event_data == 0) return;

    int* gib_data = *(int**)(event_data + 0x7C);
    if (gib_data == NULL) return;

    if (*(int*)(gib_data + 0x44) == 5) {
        // Clear owner's attachment slot
        int owner = *(int*)(gib_data + 0x68);
        int* owner_data = *(int**)(owner + 0x7C);
        if (owner_data != NULL) {
            *(int*)(owner_data + 0xBC4) = 0;
        }
        *(int*)(gib_data + 0x44) = 0;
    } else {
        block_free_simple(gib_data);
    }
}


// ============================================================================
// 0x80099008 — gib_tick_update (event handler for slot 4)
// Gib entity tick update state machine.
// States:
//   0 = attach to owner (setup_attachment → state 1)
//   1 = launch (animation play + velocity + particles via gib_spawn_particles)
//   4/5 = fade (alpha decrement per frame + collision shape opacity)
//   4(end) = destroy when alpha <= 0
//
// Handles bone attachment, animation playback lifecycle, collision fade-out,
// and owner bitmask management.
// ============================================================================
void gib_tick_update(void) {
    stack_save();

    int* entity = get_current_entity();
    float* gib_data = (float*)entity[0x1F];  // entity + 0x7C
    float state = gib_data[0x10];
    float owner_entity = gib_data[0x1A];

    // State 4 (terminal): destroy
    if (state == 4.0f) {
        entity_destroy_with_event(entity);
        stack_restore();
        return;
    }

    // State 1: attach to owner bone
    if (state == 1.0f) {
        // Validate owner is alive and has model data
        bool owner_invalid = false;
        if (owner_entity == 0.0f ||
            (*(unsigned int*)((int)owner_entity + 0x28) & 0x80000000) != 0 ||
            (*(unsigned int*)((int)owner_entity + 0x28) & 0x200000) == 0 ||
            *(int*)((int)owner_entity + 4) == 0 ||
            *(int*)(*(int*)((int)owner_entity + 4) + 0x10) == 0) {
            owner_invalid = true;
        }

        if (owner_invalid) {
            gib_data[0x10] = 4.0f;  // skip to destroy
        } else {
            // Sync position to parent if parent exists
            if (entity[0x1E] != 0 &&
                (*(unsigned int*)(*(int*)entity[0x1E] + 0x28) & 0x80000000) == 0) {
                entity_sync_attached_position((int)entity);
                entity_compute_bone_attachment();
                entity[10] |= 8;
            }

            // Copy transform from owner bone matrix
            matrix_copy(*(float**)(entity[1] + 0x10), gib_data);

            // Set owner's gib bitmask for this slot
            int* owner_data = *(int**)((int)owner_entity + 0x7C);
            *(unsigned int*)(owner_data + 0xBE8) |= (0x40 << (int)gib_data[0x11]);

            // Enable rendering and collision
            entity[6] |= 1;
            entity[10] |= 0x1071;

            // Start animation playback
            int anim_handle = animation_create_instance();
            entity[0x20] = anim_handle;

            if (anim_handle == 0) {
                gib_data[0x10] = 4.0f;  // no anim = destroy
            } else {
                // Configure animation: weapon slot -> anim ID, play at speed
                int weapon_slot = (int)gib_data[0x11];
                animation_play(anim_handle, entity, 0x11,
                              g_weapon_anim_ids[weapon_slot], ANIM_SPEED);

                // Compute velocity from animation duration
                float duration = animation_get_duration(anim_handle);
                float inv_duration = 1.0f / duration;
                float vel_x = GIB_LAUNCH_SPEED * gib_data[0x1B] * inv_duration;
                float vel_y = GIB_LAUNCH_SPEED * gib_data[0x1C] * inv_duration;
                float vel_z = GIB_LAUNCH_SPEED * gib_data[0x1D] * inv_duration;

                // Bone offset for velocity direction
                if (*(int*)(*(int*)((int)owner_entity + 4) + 0x90) != 0) {
                    int bone_idx = g_weapon_bone_ids[weapon_slot];
                    if (bone_idx >= 0) {
                        // Get bone position delta and add to velocity
                        int bone_matrices = *(int*)(*(int*)((int)owner_entity + 4) + 0x10)
                                           + entity[0x4B] * 0x40;
                        int rest_pose = *(int*)(*(int*)((int)owner_entity + 4) + 0x90);
                        float bone_offset[3];
                        bone_offset[0] = *(float*)(bone_matrices + 0x30) - *(float*)(rest_pose + bone_idx * 0xC);
                        bone_offset[1] = *(float*)(bone_matrices + 0x34) - *(float*)(rest_pose + bone_idx * 0xC + 4);
                        bone_offset[2] = *(float*)(bone_matrices + 0x38) - *(float*)(rest_pose + bone_idx * 0xC + 8);

                        // Transform bone offset by entity local matrix
                        float local_matrix[16];
                        entity_build_local_matrix(local_matrix, owner_entity, 0, 0);
                        matrix_transform_point(local_matrix, bone_offset);

                        vel_x += bone_offset[0];
                        vel_y += bone_offset[1];
                        vel_z += bone_offset[2];
                    }
                }

                // Apply velocity
                animation_set_velocity(anim_handle, vel_x, vel_y, vel_z);

                // Compute angular velocity (cross product for spin axis)
                float* anim_pos = animation_get_position(anim_handle);
                float cross_x = (gib_data[0x20] - anim_pos[2]) * vel_x - (gib_data[0x1E] - anim_pos[0]) * vel_z;
                float cross_y = (gib_data[0x1E] - anim_pos[0]) * vel_y - (gib_data[0x1F] - anim_pos[1]) * vel_x;
                float cross_z = (gib_data[0x1F] - anim_pos[1]) * vel_z - (gib_data[0x20] - anim_pos[2]) * vel_y;
                animation_set_angular_velocity(cross_x, cross_y, cross_z);

                gib_data[0x10] = 5.0f;  // transition to active state

                // Spawn initial particles
                gib_spawn_particles();
            }
        }
    }
    // State 0: initial setup (bone attachment)
    else if (state == 0.0f) {
        bool owner_invalid = false;
        if (owner_entity == 0.0f ||
            (*(unsigned int*)((int)owner_entity + 0x28) & 0x80000000) != 0 ||
            (*(unsigned int*)((int)owner_entity + 0x28) & 0x200000) == 0 ||
            *(int*)((int)owner_entity + 4) == 0 ||
            *(int*)(*(int*)((int)owner_entity + 4) + 0x10) == 0) {
            owner_invalid = true;
        }

        if (owner_invalid) {
            gib_data[0x10] = 4.0f;
        } else {
            // Determine bone attachment based on weapon slot
            int weapon_slot = (int)gib_data[0x11];
            int bone_id = -1;
            if (weapon_slot == 2) {
                bone_id = *(int*)(*(int*)(*(int*)((int)owner_entity + 4) + 0x84) + 0x38);
            } else if (weapon_slot == 0) {
                bone_id = *(int*)(*(int*)(*(int*)((int)owner_entity + 4) + 0x84) + 0x18);
            } else if (weapon_slot == 1) {
                bone_id = *(int*)(*(int*)(*(int*)((int)owner_entity + 4) + 0x84) + 0x34);
            } else if (weapon_slot == 5) {
                bone_id = *(int*)(*(int*)(*(int*)((int)owner_entity + 4) + 0x84) + 0x54);
            }

            entity_setup_attachment((int)entity, *(int**)((int)owner_entity + 4), bone_id, -1, 0);
            gib_data[0x10] = 1.0f;  // transition to launch state
        }
    }
    // State 5 (fade): alpha decrement + collision fade
    else if (state >= 4.0f && state < 6.0f) {
        unsigned int anim_flags = animation_get_flags(entity[0x20]);

        if ((anim_flags & 0x4000) == 0 && (anim_flags & 0x4000000) == 0) {
            // Animation still playing: decrement alpha per frame
            float dt = g_frame_delta_time / g_ticks_per_second_float;
            gib_data[0x13] -= dt;

            if (gib_data[0x13] >= GIB_RENDER_SCALE) {
                // Still visible: update collision shape opacity
                if (gib_data[0x13] < GIB_FADE_THRESHOLD && gib_data[0x10] != 1.0f) {
                    int alpha = (int)(GIB_ALPHA_SCALE * gib_data[0x13]);
                    collision_set_shape_params(
                        (int*)entity[1], 0xF, -1, 0xFF,
                        (unsigned char)(alpha & ~(alpha >> 31)));
                }
            } else {
                // Fully faded: disable rendering, mark for destroy
                entity[10] &= ~1;
                gib_data[0x10] = 4.0f;
            }
        } else {
            // Animation completed: reset alpha to full
            gib_data[0x13] = GIB_INITIAL_SCALE;
        }
    }

    stack_restore();
}
