// Address: 0x8007a734 - 0x8007b3b0 | Name: entity_physics_lifecycle | Date: 2026-08-10
// Entity physics simulation, fall/gravity, post-spawn setup, callbacks, screen projection
// Part of the entity lifecycle subsystem

#include "common.h"

// ============================================================================
// 0x8007a734 | entity_spring_physics_tick
// Per-frame spring-mass simulation for bouncing/swaying entities (registered callback).
// No callers - registered via entity_register_for_update(entity, 6, ...).
// Simulates:
//   - Vertical displacement (+0x220) with velocity (+0x224)
//   - Gravity acceleration (FLOAT_8061406c) + spring damping (FLOAT_80614074)
//   - Rotation oscillation on 2 axes (+0xD4, +0xDC) with angular velocity (+0xE0, +0xE8)
//   - Bounce detection: when displacement crosses zero, plays impact sound
//     based on material type flags (+0x9C):
//       bit 6 (0x40): metal sound (ID 0xCDA)
//       bit 5 (0x20): wood sound (ID 0xCDB)
//       bit 9 (0x200): glass sound (ID 0xCDC)
//     Sound volume proportional to impact velocity
//   - Rotation clamped to [FLOAT_8061407c, FLOAT_80614078]
//   - When displacement < FLOAT_80614060: angular velocity damped by cos envelope
//   - Applies bone matrix transforms via FUN_8012f534 (translate) + FUN_8012f154 (rotate)
// Termination: when all motion below threshold, deregisters (entity_register_for_update(e,6,0))
//   and clears physics flag 0x4000000.
// ============================================================================
void entity_spring_physics_tick(void) {
    int entity = FUN_803826f0(); // prologue - get self

    // Check if still active: any of velocity, rotation, or displacement non-zero
    float velocity = *(float*)(entity + 0x224);
    float rot_a = *(float*)(entity + 0xD4);
    float rot_b = *(float*)(entity + 0xDC);
    float displacement = *(float*)(entity + 0x220);

    if (ABS(velocity) <= FLOAT_80614060 &&
        rot_a > FLOAT_80614064 && rot_a < FLOAT_80614068 &&
        rot_b > FLOAT_80614064 && rot_b < FLOAT_80614068 &&
        displacement <= 0.0f) {
        // Motion settled - deregister
        entity_register_for_update(entity, 6, 0);
        *(unsigned int*)(entity + 0x28) &= ~0x4000000;
        FUN_8038273c();
        return;
    }

    // Per-substep simulation (DAT_806108e4 substeps per frame)
    float dt = 1.0f / (float)DAT_806109c0; // frame time
    for (int i = 0; i < DAT_806108e4; i++) {
        // Update displacement: d += v*dt + 0.5*gravity*dt^2
        displacement += velocity * dt / FLOAT_80614040 + FLOAT_8061406c * dt * dt / FLOAT_80614040;

        // Update velocity: spring force + damping
        velocity = -(FLOAT_80614074 * velocity * dt - -(FLOAT_80614070 * dt - velocity));

        // Update rotation angles
        rot_a += *(float*)(entity + 0xE0);
        rot_b += *(float*)(entity + 0xE8);

        // Clamp rotations
        // ...

        // Bounce detection (displacement crosses zero from above)
        if (displacement <= 0.0f) {
            displacement = 0.0f;
            velocity = -(FLOAT_80614080 * velocity); // coefficient of restitution

            // Play impact sound if velocity high enough
            if (FLOAT_80614080 < ABS(velocity)) {
                float vol = velocity * 0.5f;
                unsigned int flags = *(unsigned int*)(entity + 0x9C);
                int sound_id;
                if (flags & 0x40) sound_id = 0xCDA;      // metal
                else if (flags & 0x20) sound_id = 0xCDB;  // wood
                else if (flags & 0x200) sound_id = 0xCDC; // glass
                else sound_id = 0; // no sound

                if (sound_id) {
                    // sound_init_play_params + sound_play_request at entity position
                }
            }

            // Damp rotation on bounce
            // ...
        }
    }

    // Apply transforms to bone matrix
    // FUN_8012f534: translate by (offset_x, displacement, offset_z)
    // FUN_8012f154: rotate by (rot_a, 0, rot_b)
    // FUN_80130bd4: matrix multiply

    FUN_8038273c(); // epilogue
}

// ============================================================================
// 0x8007accc | entity_start_falling
// Initiates gravity/fall physics on an entity. Recursive: propagates to
// owned entity at +0x20C (e.g. held items fall with holder).
// Classifies entity priority from visibility flags (+4 -> +0x30):
//   bit 1: priority 1, bit 2: priority 2, bit 3: priority 3
// First call (entity->fall_state (+0x16C) == 0):
//   - Sets fall speed (+0x224) from FLOAT_80614090 * dt
//   - Sets entity state byte (+0x32) = 2
//   - Randomizes rotation velocities (+0xE0, +0xE8) via FUN_8013104c
//   - Clears current rotation (+0xD4, +0xDC) to 0
//   - Registers for physics update: entity_register_for_update(entity, 6, callback)
// Subsequent calls (already falling):
//   - Accumulates fall speed: += dt * 0.5
//   - Propagates to owned entity with halved dt
// Sets flag 0x4000000 on entity flags (+0x28).
// Also adjusts height offset (+0x220) if visual Y != stored Y.
// Called by: self (recursive), FUN_80090f7c, FUN_80091628, FUN_8009279c
// ============================================================================
void entity_start_falling(double dt, int entity) {
    // Classify by visibility flags
    unsigned int vis_flags = *(unsigned int*)(*(int*)(entity + 4) + 0x30);
    if (vis_flags & 2) {
        *(int*)(entity + 300) = 1;
    } else if (vis_flags & 4) {
        *(int*)(entity + 300) = 2;
    } else if (vis_flags & 8) {
        *(int*)(entity + 300) = 3;
    } else {
        return; // not a fallable entity
    }

    // Sync height offset if visual Y changed
    if (*(float*)(entity + 0xAC) != *(float*)(entity + 0x68)) {
        *(float*)(entity + 0x220) += *(float*)(entity + 0x68) - *(float*)(entity + 0xAC);
        *(float*)(entity + 0x68) = *(float*)(entity + 0xAC);
    }

    if (*(int*)(entity + 0x16C) == 0) {
        // First call - initialize fall
        *(float*)(entity + 0x224) = (float)(FLOAT_80614090 * dt);
        *(unsigned char*)(entity + 0x32) = 2;
        // Random rotation velocities
        *(float*)(entity + 0xE0) = /* RNG-based */ 0.0f;
        *(float*)(entity + 0xE8) = /* RNG-based */ 0.0f;
        *(float*)(entity + 0xD4) = 0.0f;
        *(float*)(entity + 0xDC) = 0.0f;
        entity_register_for_update(entity, 6, /*callback*/ -0x7FF858CC);

        // Propagate to owned entity
        int owned = *(int*)(entity + 0x20C);
        if (owned != 0 && *(short*)(owned + 0x94) != 4) {
            entity_start_falling(dt, owned);
        }
    } else {
        // Already falling - accumulate
        *(float*)(entity + 0x224) += (float)(dt * 0.5);
        int owned = *(int*)(entity + 0x20C);
        if (owned != 0 && *(short*)(owned + 0x94) != 4) {
            entity_start_falling(dt * 0.5, owned);
        }
    }

    *(unsigned int*)(entity + 0x28) |= 0x4000000;
}

// ============================================================================
// 0x8007ae98 | entity_pool_validate_index
// Validates whether an entity pointer is within the active entity pool.
// Uses XOR with DAT_80611c20 and pool range arithmetic to produce a
// boolean: returns 1 if valid, 0 if out of range.
// The formula: ((hash >> 1) - (hash & mask)) >> 31
//   where hash = DAT_80611c20 ^ (param + (pool_count - pool_start) + 4)
// Effectively tests whether the pointer falls within the entity array bounds.
// ============================================================================
int entity_pool_validate_index(int entity_ptr) {
    unsigned int hash = DAT_80611c20 ^ (unsigned int)(entity_ptr + (DAT_80611c70 - DAT_80611c6c) + 4);
    return (int)((hash >> 1) - (hash & DAT_80611c20)) >> 31;
}

// ============================================================================
// 0x8007b148 | entity_is_at_interact_height
// Checks if entity's current Y position (+0xAC) is within interaction range
// of its computed interaction height (+0x218).
// Range: [interact_height, interact_height + FLOAT_80614084)
// Returns 1 if entity is at correct height for interaction, 0 otherwise.
// Called by entity_post_spawn_setup and FUN_8009279c.
// ============================================================================
int entity_is_at_interact_height(int entity) {
    float current_y = *(float*)(entity + 0xAC);
    float interact_y = *(float*)(entity + 0x218);
    if (current_y >= interact_y && current_y < interact_y + FLOAT_80614084) {
        return 1;
    }
    return 0;
}

// ============================================================================
// 0x8007aec8 | entity_post_spawn_setup
// Called by level_spawn_all_entities after all entities are placed.
// Per-entity initialization pass:
//   1. Assigns each entity to its scene room via entity_assign_room
//   2. Rebuilds category lists (entity_rebuild_category_lists)
//   3. For each entity with valid room assignment:
//      a. Gets material data (scene_node_get_texture_data, scene_node_get_material_count)
//      b. Computes interaction score via FUN_80099604 (distance+angle from spawn pos)
//      c. Stores score at +0x218, interaction data at +0x21C
//      d. If entity has "needs owner" flag (bit 0 of +0x9C):
//         - Calls class callback (+0x24 -> +0x2C)
//         - If not at interact height: searches room entities for closest
//           entity directly above (within movement range), assigns as owner (+0x20C)
// This establishes the owner hierarchy (e.g. items on tables, lamps on shelves).
// ============================================================================
void entity_post_spawn_setup(void) {
    FUN_803826d4(); // prologue
    int node_count = DAT_80611b14;

    // Phase 1: assign rooms to all entities
    int offset = 0;
    for (int i = 0; i < DAT_80611c70; i++) {
        entity_assign_room(DAT_80611c24 + offset);
        offset += 0x2E8;
    }

    // Phase 2: rebuild sorted category lists
    entity_rebuild_category_lists();

    // Phase 3: per-entity interaction setup + owner finding
    offset = 0;
    for (int i = 0; i < DAT_80611c70; i++) {
        int entity = DAT_80611c24 + offset;
        if (entity == 0) { offset += 0x2E8; continue; }

        int room_id = *(int*)(entity + 0x34);
        if (room_id <= 0 || room_id > node_count) { offset += 0x2E8; continue; }

        // Compute interaction score from entity spawn position
        int mat_count = scene_node_get_material_count(room_id, 1);
        int tex_data = scene_node_get_texture_data(room_id, 1);
        float spawn_pos[3] = {
            *(float*)(entity + 0xA8) + *(float*)(entity + 0x1A8),
            *(float*)(entity + 0xAC) + *(float*)(entity + 0x1AC),
            *(float*)(entity + 0xB0) + *(float*)(entity + 0x1B0)
        };
        float score = FUN_80099604(mat_count, tex_data,
            *(unsigned int*)(*(int*)(DAT_80611b04 + room_id * 4) + 4 + 0x30),
            spawn_pos, /*out*/ NULL);
        *(float*)(entity + 0x218) = score;

        // If "needs owner" flag set
        if (*(unsigned int*)(entity + 0x9C) & 1) {
            // Call class init callback
            // Check if at interact height
            if (!entity_is_at_interact_height(entity)) {
                // Find closest entity in room that is directly above
                float best_dist = FLOAT_80613fc0; // large value
                int best_entity = 0;
                // Iterate room entity list, find closest above within range
                // ...
                if (best_entity != 0 && *(int*)(best_entity + 0x20C) == 0) {
                    *(int*)(best_entity + 0x20C) = entity; // assign owner
                }
            }
        }
        offset += 0x2E8;
    }

    FUN_80382720(); // epilogue
}

// ============================================================================
// 0x8007b17c | entity_death_particle_tick
// Per-frame tick for entity death/destroy particle effect (registered callback).
// If countdown timer (+0x1A4) has expired (<= 0):
//   - Spawns death particle at entity position + Y offset (FLOAT_80614054)
//   - Uses particle params: color from DAT_803cc364/68/6c, scale = FLOAT_8061400c
//   - Deregisters from update list: entity_register_for_update(entity, 3, 0)
// If timer still active:
//   - Decrements by DAT_806108e4 (frame ticks elapsed)
// No callers found - registered via entity_register_for_update callback system.
// ============================================================================
void entity_death_particle_tick(
    /* params 1-8 passed through for FUN_80090180 */
    int entity  // param_9
) {
    if (*(int*)(entity + 0x1A4) < 1) {
        // Timer expired - spawn death particle and deregister
        float pos[3];
        pos[0] = *(float*)(entity + 0x64) + *(float*)(entity + 0x1A8);
        pos[1] = FLOAT_80614054 + *(float*)(entity + 0x68) + *(float*)(entity + 0x1AC);
        pos[2] = *(float*)(entity + 0x6C) + *(float*)(entity + 0x1B0);

        entity_register_for_update(entity, 3, 0); // deregister

        // Spawn particle effect
        // FUN_8008dc3c + FUN_80090180 with color/scale params
    } else {
        // Decrement countdown
        *(int*)(entity + 0x1A4) -= DAT_806108e4;
    }
}

// ============================================================================
// 0x8007b268 | entity_dispatch_think_callbacks
// Iterates the "think" entity list (DAT_80611c60 count, DAT_80540d2c array).
// For each entity that is:
//   - Not dead (flag 0x80000000 clear)
//   - Has non-zero type bits (lower 16 bits of +0x10)
//   - Has a think callback (+0x174 != NULL)
// Calls the think callback: callback(entity, 0)
// Called by FUN_801c252c (main game loop think pass).
// ============================================================================
void entity_dispatch_think_callbacks(void) {
    FUN_803826f0(); // prologue

    for (int i = 0; i < DAT_80611c60; i++) {
        int entity = *(int*)((int)&DAT_80540d2c + i * 4);
        if ((*(unsigned int*)(entity + 0x28) & 0x80000000) != 0) continue;
        if ((*(unsigned int*)(entity + 0x10) & 0xFFFF) == 0) continue;
        void (*callback)(int, int) = *(void(**)(int, int))(entity + 0x174);
        if (callback != NULL) {
            callback(entity, 0);
        }
    }

    FUN_8038273c(); // epilogue
}

// ============================================================================
// 0x8007b2f0 | entity_prepare_render_volumes
// Pre-render pass: transforms all collision/decal volumes and attached effects.
// Called by scene_create_render_entities as part of the render setup pipeline.
// Calls in order:
//   1. entity_transform_collision_volumes() - update collision AABBs
//   2. entity_transform_decal_volumes() - update decal projection volumes
//   3. FUN_800fffdc() - (likely shadow volume update)
//   4. entity_render_attached_effects() - transform particle/effect attachments
//   5. FUN_8010386c() - (likely light probe update)
// ============================================================================
void entity_prepare_render_volumes(void) {
    entity_transform_collision_volumes();
    entity_transform_decal_volumes();
    FUN_800fffdc();  // shadow/volume update
    entity_render_attached_effects();
    FUN_8010386c();  // light probe update
}

// ============================================================================
// 0x8007b350 | entity_dispatch_render_callbacks
// Iterates the render linked list starting at DAT_80540a44.
// For each entity in the list:
//   - Reads render callback at offset +0x17C (index 0x5F * 4)
//   - If non-NULL: calls callback(entity, 1)
// The flag=1 parameter distinguishes render-pass calls from think-pass (flag=0).
// Called by scene_render_pipeline as part of the per-frame render sequence.
// ============================================================================
void entity_dispatch_render_callbacks(void) {
    unsigned int* current = DAT_80540a44; // linked list head
    DAT_80611c50 = current;

    while (current != NULL) {
        void (*callback)(unsigned int*, int) = (void(*)(unsigned int*, int))current[0x5F];
        if (callback != NULL) {
            callback(current, 1);
        }
        current = (unsigned int*)*current; // next = first field (linked list)
        DAT_80611c50 = current;
    }
}

// ============================================================================
// 0x8007b3b0 | entity_compute_screen_bounds
// Projects entity's 3D AABB (defined by position +/- movement_range) to 2D
// screen-space bounds. Tests all 8 corners of the AABB.
// Uses sprite_measure_text_region_int for each corner to get screen coords.
// Accumulates min/max X and Y across all valid projections.
// If param_7 != 0: clamps output to viewport bounds (DAT_80611748).
// If all 8 corners fail projection: outputs zeros (entity fully off-screen).
// param_6 (optional): receives average depth/distance for sorting.
// Output: param_3=min_z, param_4=max_x, param_5=max_z, piVar7(return)=min_x
// Called by 8+ functions for UI targeting, occlusion, screen-space effects.
// ============================================================================
void entity_compute_screen_bounds(
    /* param_1, param_2: unused */
    int* out_min_z,      // param_3
    int* out_max_x,      // param_4
    int* out_max_z,      // param_5
    float* out_depth,    // param_6: optional average depth
    int clamp_to_viewport // param_7: if non-zero, clamp to screen bounds
) {
    int entity_pair = (int)FUN_803826c8();
    int entity = (int)((uint64_t)entity_pair >> 32);
    int* out_min_x = (int*)entity_pair; // return via r3 slot

    // Compute AABB corners from position + movement_range
    float cx = *(float*)(entity + 0x64) + *(float*)(entity + 0x1A8);
    float cy = *(float*)(entity + 0x68) + *(float*)(entity + 0x1AC);
    float cz = *(float*)(entity + 0x6C) + *(float*)(entity + 0x1B0);
    float rx = *(float*)(entity + 0x1C0);
    float ry = *(float*)(entity + 0x1C4);
    float rz = *(float*)(entity + 0x1C8);

    int min_x = DAT_80611748[1], max_x = DAT_80611748[0];
    int min_z = DAT_80611748[6], max_z = DAT_80611748[5];
    unsigned int valid_count = 8;

    if (out_depth != NULL) *out_depth = 0.0f;

    unsigned int clz_mask = countLeadingZeros(clamp_to_viewport);

    // Test all 8 AABB corners
    for (unsigned int corner = 0; corner < 8; corner++) {
        float px = (corner & 1) ? (cx - rx) : (cx + rx);
        float py = (corner & 2) ? (cy - ry) : (cy + ry);
        float pz = (corner & 4) ? (cz - rz) : (cz + rz);

        float depth;
        int screen_x, screen_y;
        unsigned int result = sprite_measure_text_region_int(
            &px, &screen_x, &screen_y, &depth, clz_mask >> 5);

        if (result & 2) {
            valid_count--;
        } else {
            if (out_depth != NULL) *out_depth += depth;
        }

        // Track min/max screen coords
        if (screen_x < min_x) min_x = screen_x;
        if (screen_x > max_x) max_x = screen_x;
        if (screen_y < min_z) min_z = screen_y;
        if (screen_y > max_z) max_z = screen_y;
    }

    if (valid_count == 0) {
        // All corners off-screen
        *out_max_z = 0;
        *out_min_z = 0;
        *out_max_x = 0;
        *out_min_x = 0;
    } else {
        if (out_depth != NULL) {
            *out_depth /= (float)valid_count;
        }
        // Clamp to viewport if requested, otherwise use raw values
        // ... (viewport clamping logic)
        *out_min_x = min_x;
        *out_max_x = max_x;
        *out_min_z = min_z;
        *out_max_z = max_z;
    }

    FUN_80382714(); // epilogue
}
