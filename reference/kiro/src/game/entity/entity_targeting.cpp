// Address: 0x80078fbc - 0x8007a194 | Name: entity_targeting | Date: 2026-08-10
// Player targeting, entity interaction queries, auto-aim, LOS checks
// Part of the entity/player interaction subsystem

#include "common.h"

// ============================================================================
// 0x8007931c | entity_check_vehicle_mount
// Checks if entity is type 4 (vehicle) with sub-state 3 (mountable).
// If so, triggers mount action via FUN_80086804.
// Returns 1 if mount triggered, 0 otherwise.
// ============================================================================
int entity_check_vehicle_mount(int entity) {
    if (*(int*)(entity + 0x20) == 4) {
        int sub_data = *(int*)(entity + 0x7C);
        if (*(int*)(sub_data + 0x10) == 3) {
            FUN_80086804(entity, 1);
            return 1;
        }
    }
    return 0;
}

// ============================================================================
// 0x80079368 | entity_wall_avoidance_resolve
// Complex polygon-based wall detection for AI entities.
// Iterates entity edge polygons, performs winding-number test to detect
// if position is inside polygon, then finds 3 closest edges sorted by
// perpendicular distance. Attempts collision resolution by pushing position
// along edge normals. Uses FUN_8009a2d0 for swept collision validation.
// Called only by entity_find_nearest_steering_target (0x80079880).
// ============================================================================
void entity_wall_avoidance_resolve(/* r3-r7 passed via register convention */) {
    // Paired-single register save/restore (f25-f31)
    // Get entity context via prologue helper
    // uVar23 = FUN_803826dc() -> returns {entity_ptr, player_index}
    int entity = (int)FUN_803826dc();
    int player_idx_ptr = (int)((uint64_t)FUN_803826dc() >> 32);

    // Store initial approach speed at output+0x20
    // *(float*)(output + 0x20) = extraout_f1;

    // Winding-number polygon containment test:
    // Iterates entity->edge_count (offset 0x1E4) edges from edge_list (offset 0x1F4)
    // Each edge: {y_min @ +0xC, y_max @ +0x18, x_start @ +0x14, x_end @ +0x08,
    //             z_start @ +0x10, z_end @ +0x1C}
    // Counts left/right crossings for point-in-polygon determination

    // If inside polygon (left < right crossings):
    //   Check vehicle mount state first
    //   If not mountable:
    //     Find 3 closest edges by perpendicular distance (sorted insertion)
    //     For each closest edge (up to 3):
    //       Compute push direction along edge normal
    //       Attempt swept collision via FUN_8009a2d0
    //       If clear (no collision bit 2): accept new position
    //       Compute normalized direction from old to new position
    //       Store direction + scaled speed in output struct

    FUN_80382728(); // epilogue restore
}

// ============================================================================
// 0x80078fbc | entity_find_closest_autoaim_target
// Iterates all active entities in the global entity list (DAT_8055a154).
// For each entity with type 8 (character) and visibility flag 0x40 set:
//   - Computes 2D cylinder intersection distance (FUN_80011604) or
//     line-segment distance (FUN_800112ac) for polygon-hull entities
//   - Applies scaling based on entity type (vehicle=0x4D gets wider radius)
//   - Tracks closest hit with direction and entity pointer
// For polygon-hull entities (has collision mesh flag 0x200000):
//   - Tests against 4 edge segments of the collision hull
// Output struct: {type, direction_x, direction_z, distance, entity_ptr}
// Called by FUN_8009a2d0 (movement/collision resolution).
// ============================================================================
void entity_find_closest_autoaim_target(
    /* param_1, param_2 via r3/r4 (unused - passed through) */
    float* player_pos,      // param_3: player XZ position
    float* player_dir,      // param_4: player aim direction
    void* out_result        // param_5: output closest target info
) {
    int* player_ctx = (int*)FUN_803826c0(); // prologue - get player context
    unsigned int saved_flags = 0;
    int player_entity = 0;

    if (*player_ctx != -1) {
        player_entity = *(int*)(DAT_80611d90 + *player_ctx * 0xEA0);
    }

    float best_dist = 1.0f; // FLOAT_80613fbc (normalized max)
    // Clear output struct (0x2C bytes)
    memset_wrapper((int)/*&output*/ 0, 0, 0x2C);

    // Disable self-collision flag on player's attached entity
    if (*player_ctx != -1) {
        int attached = *(int*)(DAT_80611d90 + *player_ctx * 0xEA0 + 0xC14);
        if (attached != 0) {
            saved_flags = *(unsigned int*)(attached + 0x28);
            *(unsigned int*)(attached + 0x28) = saved_flags & ~0x40;
        }
    }

    // Iterate all entities (DAT_80611c38 count, DAT_8055a154 array)
    for (int i = 0; i < DAT_80611c38; i++) {
        int entity = *(int*)((int)&DAT_8055a154 + i * 4);
        if ((*(unsigned int*)(entity + 0x28) & 0x40) == 0) continue;

        if (*(int*)(entity + 0x20) == 8) {
            // Character entity - cylinder intersection test
            float scale = 1.0f;
            // Vehicle type 0x4D gets larger radius
            // Uses FUN_80011604 for cylinder-ray intersection
            // Updates best_dist, stores direction + entity
        } else if (*(unsigned int*)(entity + 0x80) != 0) {
            // Polygon hull entity - edge segment tests
            // Uses FUN_800112ac for line-segment distance
            // Tests 4 edges of collision hull
        }
    }

    // Restore collision flags
    // Write results to out_result
    FUN_8038270c(); // epilogue
}

// ============================================================================
// 0x80079880 | entity_find_nearest_steering_target
// Finds the nearest entity for AI steering/avoidance purposes.
// Iterates all active entities. For type-8 (character) entities:
//   - Computes 2D distance with type-specific scaling
//   - Vehicles (type 0x4D) and mounted entities get different scale factors
//   - Tracks closest with normalized direction
// For polygon-hull entities (has edge data at +0x1F4):
//   - Delegates to entity_wall_avoidance_resolve (0x80079368)
// Output written to param_6 struct at offsets:
//   +0x14: type, +0x18: dir_x, +0x1C: dir_z, +0x20: distance, +0x24: entity
// Called by FUN_80099eac (AI movement controller).
// ============================================================================
void entity_find_nearest_steering_target(
    /* param_1-2: unused r3/r4 */
    /* param_2: double - approach threshold */
    /* param_3-4: unused */
    float* target_pos,   // param_5: target position XYZ
    int out_result       // param_6: output struct pointer
) {
    int* player_ctx = (int*)FUN_803826d4();
    unsigned int saved_flags = 0;
    int player_entity = 0;

    if (*player_ctx != -1) {
        player_entity = *(int*)(DAT_80611d90 + *player_ctx * 0xEA0);
    }

    float best_dist = /* extraout_f1 - approach speed */0.0f;
    // Clear output (0x2C bytes)

    // Disable self-collision on player's attached entity
    // Iterate all entities
    for (int i = 0; i < DAT_80611c38; i++) {
        int entity = *(int*)((int)&DAT_8055a154 + i * 4);
        if ((*(unsigned int*)(entity + 0x28) & 0x40) == 0) continue;

        if (*(int*)(entity + 0x20) == 8) {
            // Character: 2D distance with type-specific scaling
            // If mounted or player is mounted: no scaling
            // If vehicle type: FLOAT_80614030 scale
            // Else: FLOAT_8061401c scale
            // Track closest, store normalized direction + random jitter for ties
        } else if (*(int*)(entity + 500) != 0) {
            // Polygon entity: delegate to wall avoidance resolver
            entity_wall_avoidance_resolve(/* ... */);
            // If wall result closer than current best, adopt it
        }
    }

    // Restore flags, write output struct
    FUN_80382720(); // epilogue
}

// ============================================================================
// 0x80079be0 | entity_find_best_interact_target
// Searches room entity lists for best interactive target (use-key targeting).
// First searches the player's own room entity list (DAT_8053fb2c[player]),
// then falls back to the global interactive entity list (DAT_8053fe80).
// For each candidate:
//   - Must have interaction data (+0x1F0 != 0)
//   - Must have visibility flags (bit 2 or bit 1 in class flags at +0x24)
//   - Must be active (flag 0x40) and not dead (flag 0x80000000)
//   - Calls FUN_80099604 to compute interaction score (angle+distance)
//   - Tracks highest-scoring entity
// Output: best entity pointer via param_4, interaction data via param_3.
// Called by 3 functions in the player interaction system (0x8009xxxx).
// ============================================================================
void entity_find_best_interact_target(
    /* param_1, param_2: unused */
    unsigned int* out_interact_data,  // param_3: receives interaction info
    int* out_entity                   // param_4: receives best entity ptr
) {
    int player_ctx_pair = (int)FUN_803826d0();
    int player_idx = (int)((uint64_t)player_ctx_pair >> 32);

    float best_score = FLOAT_80613fc4; // minimum threshold
    int best_entity = 0;
    unsigned int best_data = 0;

    // Search player's room entity list first
    int room_count = (&DAT_805401dc)[player_idx];
    int room_list = (&DAT_8053fb2c)[player_idx];
    for (int i = 0; i < room_count; i++) {
        int entity = *(int*)(room_list + i * 4);
        if (*(int*)(entity + 0x1F0) == 0) continue;
        unsigned int class_flags = *(unsigned int*)(*(int*)(entity + 0x24) + 0x28);
        if ((class_flags & 4) == 0 && (class_flags & 2) == 0) continue;
        if ((*(unsigned int*)(entity + 0x28) & 0x40) == 0) continue;
        if ((*(unsigned int*)(entity + 0x28) & 0x80000000) != 0) continue;

        float score = FUN_80099604(
            *(unsigned int*)(entity + 0x1E0),
            *(int*)(entity + 0x1F0),
            *(unsigned int*)(*(int*)(entity + 4) + 0x30),
            (float*)player_ctx_pair,
            /*out*/NULL
        );
        if (score > best_score) {
            best_entity = entity;
            best_data = /*local interaction data*/0;
            best_score = score;
        }
    }

    // If nothing found in room, search global interactive list
    if (best_entity == 0) {
        for (int i = 0; i < DAT_80540530; i++) {
            int entity = *(int*)(DAT_8053fe80 + i * 4);
            // Same filtering + scoring as above
        }
    }

    if (out_interact_data != NULL) *out_interact_data = best_data;
    if (out_entity != NULL) *out_entity = best_entity;
    FUN_8038271c(); // epilogue
}

// ============================================================================
// 0x80079d9c | entity_los_check_with_scene_query
// Performs a line-of-sight check between player and target position.
// 1. Gets camera/eye position (from vehicle mount or character anim data)
// 2. Computes ray direction from eye to target
// 3. Determines which portal/room the ray starts in via scene_ray_traverse_portals
// 4. Builds AABB encompassing player, start point, and end point (param_3/param_4)
// 5. Queries scene nodes in that AABB (max 30 results)
// 6. For each node, calls FUN_80100de0 (entity collision check)
// 7. Performs scene_raycast from player to target with given collision mask
// 8. Outputs hit position (param_5) and hit normal (param_6) if ray hits
// Temporarily clears entity flag 0x20 during raycast (ignore self).
// ============================================================================
void entity_los_check_with_scene_query(
    /* param_1, param_2: unused */
    int start_pos,          // param_3: ray start position
    float* end_pos,         // param_4: ray end position
    float* out_hit_pos,     // param_5: output hit position (or NULL)
    float* out_hit_normal,  // param_6: output hit normal (or NULL)
    unsigned int col_mask   // param_7: collision layer mask
) {
    int ctx = (int)FUN_803826cc();
    int entity = (int)((uint64_t)ctx >> 32);
    float* player_pos = (float*)ctx;

    // Get eye position from vehicle mount (+0x150) or character anim (+0x7C)
    float eye_x, eye_y, eye_z;
    int vehicle = *(int*)(entity + 0x150);
    if (vehicle == 0) {
        int anim_data = *(int*)(entity + 0x7C);
        eye_x = *(float*)(anim_data + 0xAF8);
        eye_y = *(float*)(anim_data + 0xAFC);
        eye_z = *(float*)(anim_data + 0xB00);
    } else {
        eye_x = *(float*)(vehicle + 0xB4);
        eye_y = *(float*)(vehicle + 0xB8);
        eye_z = *(float*)(vehicle + 0xBC);
    }

    // Compute ray direction
    float dir_x = *player_pos - eye_x;
    float dir_y = player_pos[1] - eye_y;
    float dir_z = player_pos[2] - eye_z;

    // Find starting room via portal traversal
    int room = scene_ray_traverse_portals(&eye_x, *(int*)(entity + 0x34), &dir_x, 0);

    // Build AABB from player pos, start_pos, end_pos (min/max per axis)
    // Query scene nodes in frustum (max 30)
    int node_count = scene_query_nodes_in_frustum(room, /*aabb_min*/ 0, /*aabb_max*/ 0, 30);

    // For each node: FUN_80100de0 (entity vs scene collision prep)

    // Raycast from player_pos to end_pos
    float ray_dir[3] = { end_pos[0] - player_pos[0],
                         end_pos[1] - player_pos[1],
                         end_pos[2] - player_pos[2] };

    *(unsigned int*)(entity + 0x28) &= ~0x20; // disable self-hit
    int hit = scene_raycast(room, player_pos, ray_dir, 0,
                            out_hit_pos, out_hit_normal, NULL, col_mask, 0, NULL);
    *(unsigned int*)(entity + 0x28) |= 0x20;  // restore

    // Copy results to output params if hit and non-null

    FUN_80382718(); // epilogue
}

// ============================================================================
// 0x8007a030 | entity_los_check_default
// Wrapper for entity_los_check_with_scene_query with collision mask = 0
// (all layers). Used for general-purpose line-of-sight queries.
// Called by FUN_8013ff70.
// ============================================================================
void entity_los_check_default(
    unsigned int p1, unsigned int p2,
    int start_pos, float* end_pos, float* out_hit_pos, float* out_hit_normal
) {
    entity_los_check_with_scene_query(p1, p2, start_pos, end_pos, out_hit_pos, out_hit_normal, 0);
}

// ============================================================================
// 0x8007a054 | entity_targeting_stub
// Empty no-op function. Likely a removed/disabled targeting callback.
// ============================================================================
void entity_targeting_stub(void) {
    return;
}

// ============================================================================
// 0x8007a058 | entity_find_visible_players_in_radius
// Finds all players visible from a position within a given radius.
// Iterates all active players (DAT_80612098 count, DAT_80588610 table).
// For each valid player:
//   - Computes 3D distance (with +1.0 Y offset for head height)
//   - If within radius^2: performs scene_raycast LOS check
//   - If no obstruction (raycast returns 0): adds to output array
// Returns count of visible players. Output capped at param_3 entries.
// Called by FUN_800bab94 (AI awareness/alert system).
// ============================================================================
int entity_find_visible_players_in_radius(
    /* param_1, param_2: unused */
    int max_results,     // param_3: max output array size
    int* out_entities    // param_4: output entity pointer array
) {
    float* origin = (float*)((uint64_t)FUN_803826d4() >> 32);
    float radius_sq = /* extraout_f1^2 */ 0.0f;

    int count = 0;
    for (int i = 0; i < DAT_80612098; i++) {
        int* player_slot = *(int**)((int)&DAT_80588610 + i * 4);
        if (player_slot == NULL) continue;

        int entity = *player_slot;
        float dx = *(float*)(entity + 0x64) - origin[0];
        float dy = (1.0f + *(float*)(entity + 0x68)) - origin[1];
        float dz = *(float*)(entity + 0x6C) - origin[2];
        float dist_sq = dx*dx + dy*dy + dz*dz;

        if (dist_sq < radius_sq) {
            float dir[3] = {dx, dy, dz};
            int blocked = scene_raycast(/*room*/ 0, origin, dir, 0,
                                        NULL, NULL, NULL, 0xFFFFFFFD, 0, NULL);
            if (blocked == 0) {
                if (count < max_results && out_entities != NULL) {
                    out_entities[count] = entity;
                }
                count++;
            }
        }
    }

    FUN_80382720(); // epilogue
    return count;
}

// ============================================================================
// 0x8007a194 | entity_idle_ambient_tick
// Per-frame update for ambient/idle entities (no callers - registered callback).
// Generates random movement offsets using FUN_8013104c (RNG) and oscillation.
// Features:
//   - Random position jitter (within entity->movement_range XYZ at +0x1C0/1C4/1C8)
//   - Random rotation oscillation (roll/pitch)
//   - Calls FUN_800cd838 to apply movement + rotation to entity
//   - If entity has an "owner" (+0x20C), spawns a particle effect on it
//   - Every N frames (DAT_806109c0/5): calls FUN_8016193c (footstep/splash effect)
//   - If entity->velocity (+0x1CC) > 0: spawns particle + plays looping sound
//   - If velocity == 0: spawns idle particle, deregisters from update list,
//     stops any looping sound (sound_stop_handle)
//   - Proximity check: for each player in same room, if within FLOAT_8061405c
//     distance, spawns awareness particle (FUN_80090180)
// Uses: entity_register_for_update, sound_init_play_params, sound_play_request,
//       sound_stop_handle
// ============================================================================
void entity_idle_ambient_tick(void) {
    int entity = FUN_803826e8(); // prologue - get self entity

    if (DAT_806108e4 <= 0) {
        FUN_80382734(); // epilogue
        return;
    }

    // Compute frame-rate-independent scale
    // float dt_scale = FLOAT_806108ec * (1.0f / (float)(DAT_806109c0 ^ 0x80000000));
    // ... (RNG-based position jitter, rotation oscillation)

    // Apply movement to entity position
    float new_pos[3];
    new_pos[0] = *(float*)(entity + 0x64) + *(float*)(entity + 0x1A8);
    // + random offsets scaled by movement_range
    // ...

    FUN_800cd838(/* params for entity movement application */);

    // Owner particle effect
    int owner = *(int*)(entity + 0x20C);
    if (owner != 0 && *(int*)(owner + 0x168) == 0) {
        // Spawn particle at owner position via FUN_80090180
    }

    // Periodic footstep/splash effect
    // if (frame_counter % (tick_rate/5) == 0) FUN_8016193c(...)

    // Velocity-based behavior
    if (*(float*)(entity + 0x1CC) > 0.0f) {
        // Moving: spawn movement particle + start looping sound
        // sound_init_play_params, sound_play_request (ID 0xBFB)
    } else {
        // Stopped: spawn idle particle, deregister update, stop sound
        entity_register_for_update(entity, 5, 0);
        if (*(int*)(entity + 0x228) != 0) {
            sound_stop_handle(/* handle at +0x228 */);
            *(int*)(entity + 0x228) = -1;
        }
    }

    // Player proximity awareness check
    for (int i = 0; i < DAT_80612610; i++) {
        int* slot = *(int**)((int)&DAT_80588610 + i * 4);
        if (slot == NULL) continue;
        int player = *slot;
        if (*(int*)(player + 0x34) != *(int*)(entity + 0x34)) continue; // same room?

        float dx = *(float*)(player + 0x64) - *(float*)(entity + 0x64)
                   - *(float*)(entity + 0x1A8);
        float dy = *(float*)(player + 0x68) - *(float*)(entity + 0x68);
        float dz = *(float*)(player + 0x6C) - *(float*)(entity + 0x6C)
                   - *(float*)(entity + 0x1B0);
        if (dx*dx + dy*dy + dz*dz < FLOAT_8061405c) {
            // Spawn awareness/alert particle via FUN_80090180
        }
    }

    FUN_80382734(); // epilogue
}
