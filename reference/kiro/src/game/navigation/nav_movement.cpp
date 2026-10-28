// Address: 0x8009a194-0x8009af60 | Name: nav_movement | Date: 2026-08-11
// Movement trace/slide collision resolution pipeline.
// Core system for player and AI movement: ray casting against walls,
// sliding along collision normals, floor height updates, and state management.

#include "common.h"
#include "entity.h"
#include "scene.h"
#include "math_util.h"
#include "navigation.h"
#include "collision.h"

// ============================================================================
// Globals
// ============================================================================

extern int g_wall_entity_list[];    // DAT_80561a70 — wall entity pointers (max 64)
extern int g_wall_data_list[];      // DAT_80561b70 — wall polygon data pointers
extern int g_wall_count;            // DAT_80611d00 — current wall count
extern void* g_player_data_pool;    // DAT_80611d90

static float g_last_hit_normal_x;  // FLOAT_80611cf8
static float g_last_hit_normal_z;  // fRam80611cfc

#define MAX_WALLS           64
#define MAX_SCENE_NODES     30
#define MOVEMENT_PAD_FACTOR /* FLOAT_80614658 */

// ============================================================================
// 0x8009a194 — nav_find_ray_wall_intersection
// Finds nearest wall segment intersection along movement ray.
// Iterates wall list, skips walls without flag 0x40, calls FUN_800112ac
// for ray-segment intersection. Outputs closest hit info.
// ============================================================================
void nav_find_ray_wall_intersection(float* start_pos, float* direction, int* out_result) {
    stack_save();

    float* query_pos = get_stack_position();
    float best_t = LARGE_T;  // FLOAT_80614648
    int best_wall_data = 0;
    int best_wall_entity = 0;
    float best_normal[2];

    for (int i = 0; i < g_wall_count; i++) {
        // Skip walls without active collision flag 0x40
        if ((*(unsigned int*)(g_wall_entity_list[i] + 0x28) & 0x40) == 0) continue;

        int wall_data = g_wall_data_list[i];
        float hit_normal[2];

        // Ray-segment intersection test
        float t = ray_segment_intersect_2d(
            query_pos[0], query_pos[2],           // ray origin
            start_pos[0], start_pos[1],           // ray direction (movement delta)
            *(float*)(wall_data + 8),             // segment start X
            *(float*)(wall_data + 0x10),          // segment start Z
            *(float*)(wall_data + 0x14),          // segment end X
            hit_normal);                          // output normal

        if (t < best_t) {
            best_wall_entity = g_wall_entity_list[i];
            best_normal[0] = hit_normal[0];
            best_normal[1] = hit_normal[1];
            best_wall_data = wall_data;
            best_t = t;
        }
    }

    // Write results
    if (out_result != NULL) {
        out_result[0] = best_wall_data;
        *(float*)(out_result + 1) = best_normal[0];
        *(float*)(out_result + 2) = best_normal[1];
        *(float*)(out_result + 3) = best_t;
        out_result[9] = best_wall_entity;
        out_result[4] = best_wall_entity;
    }

    stack_restore();
}

// ============================================================================
// 0x8009a2d0 — nav_trace_and_slide
// MASSIVE movement trace and slide (9 xrefs, RECURSIVE).
// Casts movement ray from start to end, checks wall intersection and entity
// collision. On hit: computes slide vector along wall normal using remaining
// distance, recursively calls self for slide resolution.
// Handles CTF flag transfer on player-player collision.
//
// params:
//   param_2 = search radius
//   param_5 = target position (XZ movement goal)
//   param_6 = output resolved position (optional, enables slide)
//   param_7 = output collision info (optional)
//   param_8 = enable scene query flag
// ============================================================================
void nav_trace_and_slide(float height, float radius, int* nav_state,
                         float* start_pos, float* target_pos, float* out_pos,
                         int* out_collision, int enable_scene_query, int param_9) {
    stack_save();

    int* player_data = get_stack_player();
    float* from_pos = get_stack_from();

    // Early out: no movement (same XZ position)
    if (target_pos[0] == from_pos[0] && target_pos[2] == from_pos[2]) {
        if (out_collision != NULL) {
            out_collision[0] = 0;
            *(float*)(out_collision + 3) = LARGE_T;
            out_collision[4] = 0;
        }
        if (enable_scene_query) player_data[6] = 0;
        if (out_pos != NULL) {
            out_pos[0] = target_pos[0];
            out_pos[1] = target_pos[1];
            out_pos[2] = target_pos[2];
        }
        stack_restore();
        return;
    }

    // Compute movement delta
    float delta_x = target_pos[0] - from_pos[0];
    float delta_z = target_pos[2] - from_pos[2];

    // Build AABB for movement sweep
    float min_x = fminf(from_pos[0], target_pos[0]) - radius;
    float max_x = fmaxf(from_pos[0], target_pos[0]) + radius;
    float min_z = fminf(from_pos[2], target_pos[2]) - radius;
    float max_z = fmaxf(from_pos[2], target_pos[2]) + radius;

    // Find nearest wall intersection along ray
    int wall_hit_info[10];
    nav_find_ray_wall_intersection(from_pos, &delta_x, wall_hit_info);

    float best_t = LARGE_T;
    float hit_normal_x, hit_normal_z;
    int hit_entity = 0;
    int hit_wall_data = 0;

    if (wall_hit_info[3] < LARGE_T) {  // wall_t < max
        hit_normal_x = *(float*)(wall_hit_info + 1);
        hit_normal_z = *(float*)(wall_hit_info + 2);
        hit_entity = wall_hit_info[4];
        hit_wall_data = wall_hit_info[0];
        best_t = *(float*)(wall_hit_info + 3);
    }

    // Scene query for entity collisions (if enabled)
    if (enable_scene_query && *player_data >= 0) {
        int player_idx = *player_data;
        int player_struct = (int)g_player_data_pool + player_idx * 0xEA0;
        *(float*)(player_struct + 0x3C) = delta_x;
        *(float*)(player_struct + 0x40) = delta_z;

        // Query scene nodes in movement AABB
        float aabb[6] = { min_x, max_x, from_pos[1], from_pos[1] + height, min_z, max_z };
        int room_list[MAX_SCENE_NODES];
        int room_count = scene_query_nodes_flat_y(player_data[1], aabb, room_list, MAX_SCENE_NODES);

        for (int i = 0; i < room_count; i++) {
            // Check entity collisions per room
            FUN_80101094(height, radius, player_idx, room_list[i], from_pos, target_pos);
        }
    }

    // Check entity (auto-aim target) collision
    int entity_hit = entity_find_closest_autoaim_target(
        player_data, &min_x, from_pos, &delta_x, wall_hit_info);

    int hit_player_idx = -1;
    if (wall_hit_info[3] < best_t) {
        hit_normal_x = *(float*)(wall_hit_info + 1);
        hit_normal_z = *(float*)(wall_hit_info + 2);
        hit_player_idx = entity_hit;
        hit_entity = wall_hit_info[4];
        hit_wall_data = wall_hit_info[0];
        best_t = *(float*)(wall_hit_info + 3);
    }

    // No collision: move to target directly
    if (best_t >= LARGE_T) {
        if (enable_scene_query) player_data[6] = 0;
        if (out_pos != NULL) {
            out_pos[0] = target_pos[0];
            out_pos[1] = target_pos[1];
            out_pos[2] = target_pos[2];
        }
    } else {
        // Collision occurred
        if (enable_scene_query) {
            player_data[6] = 1;
            if (hit_entity != 0) {
                // Invoke wall callback if present
                int player_struct = (int)g_player_data_pool + (*player_data) * 0xEA0;
                // Pack collision info and call callback at offset 0x170
                if (*(void**)(hit_entity + 0x170) != NULL) {
                    (*(void(*)(int, void*))(hit_entity + 0x170))(hit_entity, &collision_info);
                }
            }
        }

        if (out_pos == NULL) {
            // No output buffer: modify target in place
            target_pos[0] = from_pos[0] + best_t * delta_x;
            target_pos[2] = from_pos[2] + best_t * delta_z;
        } else {
            // Compute slide vector along wall normal
            float tangent_dot = delta_x * hit_normal_z + delta_z * (-hit_normal_x);
            if (tangent_dot < 0.0f) {
                tangent_dot = -tangent_dot;
                hit_normal_z = -hit_normal_z;
                hit_normal_x = -(-hit_normal_x);
            }

            // Slide: project remaining distance onto tangent
            float slide_x = SLIDE_FACTOR * hit_normal_z + (-hit_normal_x);
            float slide_z = SLIDE_FACTOR * hit_normal_x + hit_normal_z;
            float slide_len_sq = slide_x * slide_x + slide_z * slide_z;

            if (slide_len_sq > 0.0f) {
                float remaining = (LARGE_T - best_t) * tangent_dot;
                float slide_scale = remaining / slide_len_sq;
                slide_x *= slide_scale;
                slide_z *= slide_scale;
            }

            // Move to collision point
            target_pos[0] = from_pos[0] + best_t * delta_x;
            target_pos[2] = from_pos[2] + best_t * delta_z;

            // Recursive slide resolution (no output, just moves target_pos)
            nav_trace_and_slide(height, radius, player_data, from_pos,
                               target_pos, NULL, NULL, 0, param_9);

            // Apply slide offset
            out_pos[0] = target_pos[0] + slide_z;
            out_pos[1] = target_pos[1];
            out_pos[2] = target_pos[2] + slide_x;
        }
    }

    // Store last hit normal globally
    g_last_hit_normal_x = hit_normal_x;
    g_last_hit_normal_z = hit_normal_z;

    // CTF flag transfer on player-player collision
    if (hit_player_idx >= 0) {
        player_data[4] = 1;
        player_data[5] = hit_player_idx;
        if (*player_data >= 0) {
            player_copy_team_flag_data(*player_data, hit_player_idx);
        }
    } else {
        player_data[4] = 0;
    }

    // Write collision output
    if (out_collision != NULL) {
        out_collision[0] = hit_wall_data;
        *(float*)(out_collision + 1) = hit_normal_x;
        *(float*)(out_collision + 2) = hit_normal_z;
        *(float*)(out_collision + 3) = best_t;
        out_collision[4] = hit_entity;
    }

    stack_restore();
}

// ============================================================================
// 0x8009a79c — nav_update_floor_at_dest
// Updates floor height at movement destination (3 xrefs).
// Queries scene nodes, calls floor_room_query per node, falls back to
// entity_find_best_interact_target. Clamps output Y to best floor height.
// ============================================================================
void nav_update_floor_at_dest(int nav_state, float* from_pos,
                              float* dest_pos, int out_ptr, int write_to_state) {
    stack_save();

    int state = get_stack_state();
    float* pos = get_stack_pos();
    float query_y = pos[1];
    float target_y = dest_pos[1];
    float best_height = FLOOR_INVALID_HEIGHT;
    int best_entity = 0;
    int best_interact = 0;

    // Build query position from dest X/Z + padded Y
    float query_pos[3] = { dest_pos[0], FLOOR_Y_PAD + pos[1], dest_pos[2] };

    // Compute AABB from start+dest (min/max + padding)
    float aabb[6];
    float min_x = fminf(pos[0], dest_pos[0]) - NAV_FLOOR_PAD;
    float max_x = fmaxf(pos[0], dest_pos[0]) + NAV_FLOOR_PAD;
    float min_z = fminf(pos[2], dest_pos[2]) - NAV_FLOOR_PAD;
    float max_z = fmaxf(pos[2], dest_pos[2]) + NAV_FLOOR_PAD;
    aabb[0] = min_x; aabb[1] = max_x;
    aabb[2] = pos[1]; aabb[3] = query_pos[1];
    aabb[4] = min_z; aabb[5] = max_z;

    // Query scene nodes
    int room_list[MAX_SCENE_NODES + 17];
    int room_count = scene_query_nodes_flat_y(*(int*)(state + 4), aabb, room_list, MAX_SCENE_NODES);

    for (int i = 0; i < room_count; i++) {
        int interact_entity = 0;
        int interact_type = 0;
        float h = floor_room_query(room_list[i], query_pos, &interact_entity, &interact_type, 0);
        if (h > best_height) {
            best_interact = interact_type;
            best_entity = interact_entity;
            best_height = h;
        }
    }

    // Fallback: no rooms found
    if (room_count == 0) {
        int fb_entity = 0;
        int fb_type = 0;
        float h = entity_find_best_interact_target(0xD5, query_pos, &fb_entity, &fb_type);
        if (h > best_height) {
            best_interact = fb_type;
            best_entity = fb_entity;
        }
    }

    // Write results to nav state
    if (write_to_state || room_count == 0) {
        *(float*)(state + 8) = best_height;
        *(int*)(state + 0xC) = best_entity;
        *(int*)(state + 0x1C) = best_interact;
    }
    if (out_ptr != 0) {
        *(int*)(out_ptr + 0x28) = best_entity;
    }

    // Clamp Y: use best of current target_y vs floor height
    dest_pos[1] = (target_y > best_height) ? target_y : best_height;

    stack_restore();
}

// ============================================================================
// 0x8009a9ac — nav_collect_wall_polygons
// Collects wall polygons in AABB into global wall list (max 64).
// Iterates room polygon array, applies flag/AABB filtering.
// Checks player state for movement-blocking conditions.
// Populates g_wall_entity_list and g_wall_data_list.
// ============================================================================
void nav_collect_wall_polygons(int room_index, int room_data_ptr, float* aabb) {
    stack_save();

    int nav_state = get_stack_state();
    int polygon_count = *(int*)(nav_state + 0x1E4);
    int polygon_array = *(int*)(nav_state + 500);  // 0x1F4
    unsigned int filter_flags = *(unsigned int*)(*(int*)(nav_state + 4) + 0x30);

    for (int i = 0; i < polygon_count; i++) {
        unsigned int* poly = *(unsigned int**)(polygon_array + i * 4);

        // Skip dead/invalid polygons
        if ((poly[1] & 0x80000000) != 0) continue;

        // Flag 0x4000 check with player state validation
        if ((poly[1] & 0x4000) != 0 && g_nav_wall_disable_flag == 0) {
            int player_struct = (int)g_player_data_pool + *(int*)(nav_state + ???) * 0xEA0;
            if (player_struct != 0) {
                int player_level = *(int*)(player_struct + 8);
                if (player_level >= g_current_level_threshold) {
                    // Check player state flags for blocking conditions
                    unsigned int flags;
                    if (g_mode_flag == '\n') {
                        flags = *(unsigned int*)(player_struct + 0x14);
                    } else {
                        flags = *(unsigned int*)(player_struct + 0x1C);
                    }
                    if ((flags & 0x18E8) != 0) continue;
                    if ((*(unsigned int*)(player_struct + 0x1C) & 0xEFFE000) != 0) continue;
                    if ((*(unsigned int*)(player_struct + 0x14) & 0x83C86000) != 0) continue;
                    if ((*(unsigned int*)(player_struct + 0x10) & 0x8000) != 0) continue;
                }
            }
        }

        // Filter by collision flags
        if (poly[0] != 0 && (poly[0] & filter_flags) == 0) continue;

        // AABB test: polygon bounds vs query AABB
        if ((float)poly[8] >= aabb[3] && aabb[0] < (float)poly[10] &&
            (float)poly[3] < aabb[4] && aabb[1] < (float)poly[6] &&
            (float)poly[9] < aabb[5] && aabb[2] < (float)poly[11]) {

            // Add to wall list
            if (g_wall_count < MAX_WALLS) {
                g_wall_data_list[g_wall_count] = (int)poly;
                g_wall_entity_list[g_wall_count] = nav_state;
                g_wall_count++;
            } else {
                stub_61b8();  // overflow warning
            }
        }
    }

    stack_restore();
}

// ============================================================================
// 0x8009ab9c — nav_collision_setup
// Movement collision setup. Queries scene nodes via
// scene_query_nodes_inner_bounds, calls nav_collect_wall_polygons per node
// (unless flag 2 set), then entity_query_aabb_characters.
// ============================================================================
void nav_collision_setup(int nav_state, float* aabb, int has_movement,
                         unsigned int flags, int param_5) {
    stack_save();

    int state = get_stack_state();
    float* bounds = get_stack_bounds();

    // Query scene nodes in inner bounds
    int room_list[MAX_SCENE_NODES + 14];
    int room_count = scene_query_nodes_inner_bounds(*(int*)(state + 4), bounds,
                                                    room_list, MAX_SCENE_NODES);

    // Reset wall list
    g_wall_count = 0;

    // Override movement flag if nav state has pending target
    if (*(int*)(state + 0x18) != 0) {
        has_movement = 1;
    }

    // Collect wall polygons from each room (unless flag 2 skips walls)
    for (int i = 0; i < room_count; i++) {
        if ((flags & 2) == 0) {
            int room_data = *(int*)(g_room_data_base + room_list[i] * 4);
            nav_collect_wall_polygons(state, room_data, bounds);
        }
    }

    // Query character entities for collision
    entity_query_aabb_characters(state, bounds, room_list, room_count,
                                 has_movement, flags, param_5);

    stack_restore();
}

// ============================================================================
// 0x8009ac84 — nav_build_movement_aabb
// Builds movement AABB from start and end positions.
// Pads by radius * MOVEMENT_PAD_FACTOR. Calls nav_collision_setup.
// ============================================================================
void nav_build_movement_aabb(float height, float radius, int nav_state,
                             float* start_pos, float* end_pos, unsigned int flags) {
    float pad = radius * MOVEMENT_PAD_FACTOR;

    // Compute padded AABB enclosing both positions
    float aabb[6];
    float min_x = fminf(start_pos[0], end_pos[0]);
    float max_x = fmaxf(start_pos[0], end_pos[0]);
    float min_z = fminf(start_pos[2], end_pos[2]);
    float max_z = fmaxf(start_pos[2], end_pos[2]);

    aabb[0] = min_x - pad;
    aabb[1] = max_x + pad;
    aabb[2] = start_pos[1];
    aabb[3] = start_pos[1] + height;
    aabb[4] = min_z - pad;
    aabb[5] = max_z + pad;

    // Determine if movement is non-zero
    int has_movement = 0;
    if (end_pos[0] != start_pos[0] || end_pos[2] != start_pos[2]) {
        has_movement = 1;
    }

    nav_collision_setup(nav_state, aabb, has_movement, flags, 1);
}

// ============================================================================
// 0x8009ad64 — nav_full_movement_step
// Full movement step combining all nav systems.
// Sequence: build AABB → resolve steering → trace/slide → floor update.
// Preserves Y values across the XZ trace operations.
// ============================================================================
void nav_full_movement_step(float height, float radius, int nav_state, int from_state,
                            float* target_pos, float* out_pos, int* out_collision,
                            int enable_scene, unsigned int flags) {
    stack_save();

    int state = get_stack_state();
    float* from_pos = get_stack_from();

    // Preserve Y values (movement is XZ only for trace)
    float saved_from_y = from_pos[1];
    float saved_target_y = target_pos[1];

    // Step 1: Build AABB and collect walls/entities
    nav_build_movement_aabb(height, radius, state, from_pos, target_pos, flags);

    // Step 2: AI steering/wall avoidance resolution
    nav_resolve_movement_target(height, radius);

    // Step 3: Trace and slide collision resolution
    float trace_buffer[64];
    nav_trace_and_slide(height, radius, state, trace_buffer, target_pos,
                        out_pos, out_collision, enable_scene, flags);

    // Restore Y values
    from_pos[1] = saved_from_y;
    target_pos[1] = saved_target_y;

    // Step 4: Update floor height at destination
    float* final_pos = (out_pos != NULL) ? out_pos : target_pos;
    nav_update_floor_at_dest(state, from_pos, final_pos, (int)out_collision, enable_scene);

    stack_restore();
}

// ============================================================================
// 0x8009ae9c — nav_save_player_state
// Saves player movement state snapshot to 0x20-byte buffer.
// If player_index == -1, zeroes the buffer and sets sentinel.
// Otherwise copies: room ID, position data, and navigation flags.
// ============================================================================
void nav_save_player_state(int player_index, int* out_buffer) {
    if (player_index == -1) {
        memset(out_buffer, 0, 0x20);
        out_buffer[0] = -1;
        return;
    }

    int* player_struct = (int*)((int)g_player_data_pool + player_index * 0xEA0);

    out_buffer[0] = player_index;
    out_buffer[1] = *(int*)(player_struct[0] + 0x34);  // room ID
    out_buffer[2] = player_struct[0x2FF];               // nav data A
    out_buffer[3] = player_struct[0x300];               // nav data B
    out_buffer[4] = player_struct[0x307];               // nav data C
    out_buffer[5] = player_struct[0x308];               // nav data D
    out_buffer[6] = player_struct[0x2FE];               // nav flags
    out_buffer[7] = player_struct[0x301];               // nav state
}

// ============================================================================
// 0x8009af34 — nav_movement_dispatch
// Movement dispatch wrapper (11 xrefs). Thin indirection that forwards all
// params to nav_movement_entry_point. Called by teleport, AI, and gameplay.
// ============================================================================
void nav_movement_dispatch(float height, float radius, int nav_state, int from_state,
                           float* target_pos, float* out_pos, int* out_collision,
                           int enable_scene, unsigned int flags) {
    nav_movement_entry_point(height, radius, nav_state, from_state,
                             target_pos, out_pos, out_collision,
                             enable_scene, flags);
}

// ============================================================================
// 0x8009af60 — nav_movement_entry_point
// Movement entry point. Saves player state, calls nav_full_movement_step,
// then writes back navigation results to player struct.
// ============================================================================
void nav_movement_entry_point(float height, float radius, int nav_state, int from_state,
                              float* target_pos, float* out_pos, int* out_collision,
                              int enable_scene, unsigned int flags) {
    stack_save();

    int player_index = get_stack_player_index();
    int player_struct = (int)g_player_data_pool + player_index * 0xEA0;

    // Save pre-movement state
    int state_buffer[8];
    nav_save_player_state(player_index, state_buffer);

    // Execute full movement
    nav_full_movement_step(height, radius, state_buffer, from_state,
                           target_pos, out_pos, out_collision, enable_scene, flags);

    // Write back navigation results to player struct
    if (player_index != -1) {
        *(int*)(player_struct + 0xBFC) = state_buffer[2];  // local_68
        *(int*)(player_struct + 0xC00) = state_buffer[3];  // local_64
        *(int*)(player_struct + 0xC1C) = state_buffer[4];  // local_60
        *(int*)(player_struct + 0xC20) = state_buffer[5];  // local_5c
        *(int*)(player_struct + 0xBF8) = state_buffer[6];  // local_58
        *(int*)(player_struct + 0xC04) = state_buffer[7];  // local_54
    }

    stack_restore();
}


// ============================================================================
// 0x8009b054 — nav_advanced_movement_dispatch
// Advanced movement dispatch (15 xrefs). Thin wrapper forwarding to
// nav_advanced_movement_entry. Used by AI, camera, vehicle, and gameplay.
// ============================================================================
void nav_advanced_movement_dispatch(float height, float radius, int nav_state, int from_state,
                                     float* target_pos, int* out_collision,
                                     int enable_scene, unsigned int flags) {
    nav_advanced_movement_entry(height, radius, nav_state, from_state,
                                target_pos, out_collision, enable_scene, flags);
}

// ============================================================================
// 0x8009b07c — nav_advanced_movement_entry
// Advanced movement entry point. Saves player state, calls nav_advanced_trace
// for multi-pass collision, writes back nav results, sets flag 0x800
// for high-index players.
// ============================================================================
void nav_advanced_movement_entry(float height, float radius, int nav_state, int from_state,
                                  float* target_pos, int* out_collision,
                                  int enable_scene, unsigned int flags) {
    stack_save();

    int player_index = get_stack_player_index();
    int player_struct = (int)g_player_data_pool + player_index * 0xEA0;

    // Save pre-movement state
    int state_buffer[8];
    nav_save_player_state(player_index, state_buffer);

    // Execute advanced multi-pass trace
    nav_advanced_trace(height, radius, state_buffer, from_state,
                       target_pos, out_collision, enable_scene, flags);

    // Write back navigation results
    if (player_index != -1 && enable_scene != 0) {
        *(int*)(player_struct + 0xBFC) = state_buffer[2];
        *(int*)(player_struct + 0xC00) = state_buffer[3];
        *(int*)(player_struct + 0xC1C) = state_buffer[4];
        *(int*)(player_struct + 0xC20) = state_buffer[5];
        *(int*)(player_struct + 0xBF8) = state_buffer[6];
        *(int*)(player_struct + 0xC04) = state_buffer[7];

        // High-index players get extra flag
        if (player_index >= g_level_player_threshold) {
            *(unsigned int*)(player_struct + 0x2C) |= 0x800;
        }
    }

    stack_restore();
}


// ============================================================================
// 0x8009b188 — nav_advanced_trace
// MASSIVE advanced movement trace with multi-pass slide resolution.
// Performs initial trace, double-slide if hit (up to 3 passes), stall
// detection (snaps to start if moved less than threshold * dt^2),
// and floor height update. Two code paths: simplified for high-index
// players, full multi-pass for normal players.
// ============================================================================
void nav_advanced_trace(float height, float radius, int* nav_state, int from_state,
                        float* target_pos, int* out_collision,
                        int enable_scene, unsigned int flags) {
    stack_save();

    int* state = get_stack_state();
    float* from_pos = get_stack_from();

    float floor_height = (float)state[2];
    int floor_entity = state[3];
    float target_y = target_pos[1];
    bool did_double_slide = false;
    bool did_triple_slide = false;

    // If player index is -1, query floor first
    if (state[0] == -1) {
        floor_height = floor_query_with_interact(height, radius, from_pos, &floor_entity, NULL, 0);
    }

    // Save original movement direction
    float orig_dx = target_pos[0] - from_pos[0];
    float orig_dz = target_pos[2] - from_pos[2];
    float saved_from[3] = { from_pos[0], from_pos[1], from_pos[2] };

    // Step 1: Build AABB and collect walls
    nav_build_movement_aabb(height, radius, state, from_pos, target_pos, flags);

    // Step 2: Resolve steering
    nav_resolve_movement_target(height, radius);

    // Step 3: Initial trace
    float slide_pos[3];
    unsigned int trace_flags = nav_trace_and_slide(height, radius, state, /* buffer */,
                                                    target_pos, slide_pos, out_collision,
                                                    enable_scene, flags);

    int saved_hit_player = state[4];
    int saved_hit_index = state[5];

    // Normal player path (below level threshold)
    if (state[0] < g_level_player_threshold) {
        // Double-slide: if we hit something and slid to a different position
        if ((trace_flags & 2) != 0 &&
            (target_pos[0] != slide_pos[0] || target_pos[2] != slide_pos[2])) {

            did_double_slide = (trace_flags & 4) != 0;
            if (did_double_slide) {
                // Save hit normals for stall detection
                float saved_normal_x = g_last_hit_normal_x;
                float saved_normal_z = g_last_hit_normal_z;
            }

            // Check if slide direction aligns with original movement
            float align = orig_dx * (slide_pos[0] - target_pos[0]) +
                         orig_dz * (slide_pos[2] - target_pos[2]);

            if (align > SMALL_EPSILON) {
                // Second trace: from collision point toward slide target
                float slide_target[3] = { slide_pos[0], target_y, slide_pos[2] };
                float second_result[3];
                unsigned int flags2 = nav_trace_and_slide(height, radius, state,
                    target_pos, slide_target, second_result, NULL, enable_scene, flags);
                trace_flags |= flags2;

                // Triple-slide if second trace also hit and produced different result
                if ((trace_flags & 2) != 0 &&
                    (slide_pos[0] != second_result[0] || slide_pos[2] != second_result[2])) {

                    float dx2 = second_result[0] - slide_pos[0];
                    float dz2 = second_result[2] - slide_pos[2];
                    float align2 = orig_dx * dx2 + orig_dz * dz2;

                    if (align2 > SMALL_EPSILON) {
                        // Normalize slide direction, project remaining distance
                        float len = sqrtf(dx2 * dx2 + dz2 * dz2);
                        float inv_len = 1.0f / len;
                        float norm_x = dz2 * inv_len;
                        float norm_z = dx2 * inv_len;

                        float proj = (target_pos[0] - slide_pos[0]) * norm_z +
                                    (target_pos[2] - slide_pos[2]) * norm_x;

                        if (proj > SMALL_EPSILON) {
                            float third_target[3] = {
                                slide_pos[0] + proj * norm_z,
                                target_y,
                                slide_pos[2] + proj * norm_x
                            };
                            unsigned int flags3 = nav_trace_and_slide(height, radius, state,
                                slide_pos, third_target, NULL, NULL, enable_scene, flags);
                            trace_flags |= flags3;
                            did_triple_slide = true;
                            slide_pos[0] = third_target[0];
                            slide_pos[2] = third_target[2];
                        }
                    }
                }
                slide_pos[1] = second_result[1];
            }
        }

        // Apply final position
        target_pos[0] = slide_pos[0];
        target_pos[1] = slide_pos[1];
        target_pos[2] = slide_pos[2];

        // Floor update at final position
        unsigned int floor_flags = nav_update_floor_at_dest(state, saved_from, target_pos,
                                                            (int)out_collision, enable_scene);

        // Stall detection: if moved very little, snap back to start
        float move_dx = from_pos[0] - target_pos[0];
        float move_dy = from_pos[1] - target_pos[1];
        float move_dz = from_pos[2] - target_pos[2];
        float move_dist_sq = move_dx * move_dx + move_dy * move_dy + move_dz * move_dz;

        // Select stall threshold based on slide type
        float stall_threshold;
        if (((trace_flags | floor_flags) & 4) == 0 || !did_double_slide ||
            (saved_normal_x * g_last_hit_normal_x + saved_normal_z * g_last_hit_normal_z >= STALL_DOT_THRESHOLD)) {
            if (did_triple_slide) {
                stall_threshold = STALL_THRESHOLD_TRIPLE;
            } else {
                stall_threshold = STALL_THRESHOLD_DOUBLE;
            }
        } else {
            stall_threshold = STALL_THRESHOLD_NORMAL;
        }

        float dt_sq = g_frame_delta_time * g_frame_delta_time;
        if (move_dist_sq < stall_threshold * dt_sq &&
            from_pos[1] > floor_height - MOVEMENT_PAD_FACTOR) {
            // Stalled: snap back to original position
            target_pos[0] = from_pos[0];
            target_pos[1] = from_pos[1];
            target_pos[2] = from_pos[2];
            if (floor_entity != 0) {
                state[2] = (int)floor_height;
                state[3] = floor_entity;
                target_pos[1] = floor_height;
                if (out_collision != NULL) {
                    out_collision[10] = floor_entity;
                }
                if (target_pos[1] < target_y) {
                    target_pos[1] = target_y;
                }
            }
        }

        // Restore saved hit state if lost
        if (saved_hit_player != 0 && state[4] == 0) {
            state[4] = saved_hit_player;
            state[5] = saved_hit_index;
        }
    } else {
        // Simplified path for high-index players
        if ((trace_flags & 2) != 0 &&
            (target_pos[0] != slide_pos[0] || target_pos[2] != slide_pos[2])) {
            slide_pos[1] = target_y;
            nav_trace_and_slide(height, radius, state, target_pos, slide_pos,
                               NULL, NULL, enable_scene, flags);
        }
        target_pos[0] = slide_pos[0];
        target_pos[1] = slide_pos[1];
        target_pos[2] = slide_pos[2];
        nav_update_floor_at_dest(state, saved_from, target_pos, (int)out_collision, enable_scene);

        if (saved_hit_player != 0 && state[4] == 0) {
            state[4] = saved_hit_player;
            state[5] = saved_hit_index;
        }
    }

    stack_restore();
}


// ============================================================================
// 0x8009b710 — nav_check_floor_edges
// Floor edge detection via 2x2 grid probe. Tests 4 positions offset by
// radius from entity center with floor_query_with_interact. If any probe
// returns INVALID_HEIGHT, the entity is near a floor edge (ledge).
// Used for preventing AI from walking off ledges.
// ============================================================================
void nav_check_floor_edges(void) {
    stack_save();

    int* player_data = get_stack_player();

    // Only check if player has the specific status flag
    if (player_data[0x300] == 0 || (*(unsigned short*)(player_data[0x300] + 8) & 0x800) == 0) {
        stack_restore();
        return;
    }

    float probe_radius = EDGE_PROBE_RADIUS;
    float* entity_pos = (float*)*player_data;

    // 2x2 grid: test in 4 directions (+X, -X, +Z, -Z combined)
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 2; col++) {
            float probe_x = entity_pos[0x19] + probe_radius * ((float)(row - 1) - 0.5f);
            float probe_y = entity_pos[0x1A];  // Y preserved
            float probe_z = entity_pos[0x1B] + probe_radius * ((float)(col - 1) - 0.5f);

            float probe_pos[3] = { probe_x, probe_y, probe_z };

            // Temporarily enable wall-through flag for probe
            g_nav_wall_disable_flag = 1;
            float height = floor_query_with_interact(
                *(float*)(&g_height_table + *(short*)(player_data + 0x12) * 0x30),
                SMALL_EPSILON, probe_pos, 0, NULL, 0);
            g_nav_wall_disable_flag = 0;

            // If any probe finds valid floor, entity is NOT at edge — done
            if (height != FLOOR_INVALID_HEIGHT) {
                goto done;
            }
        }
    }

    // All 4 probes returned invalid: entity IS at a floor edge
    // (fall through to done — edge flag already set by caller)

done:
    stack_restore();
}

// ============================================================================
// 0x8009b894 — nav_accumulate_force_a
// Accumulates a force vector into global force accumulator A (DAT_80561c70).
// Simple vec3 addition.
// ============================================================================
void nav_accumulate_force_a(float* force) {
    g_force_accumulator_a[0] += force[0];
    g_force_accumulator_a[1] += force[1];
    g_force_accumulator_a[2] += force[2];
}

// ============================================================================
// 0x8009b8cc — nav_accumulate_force_b
// Accumulates a force vector into global force accumulator B (DAT_80561c7c).
// Simple vec3 addition.
// ============================================================================
void nav_accumulate_force_b(float* force) {
    g_force_accumulator_b[0] += force[0];
    g_force_accumulator_b[1] += force[1];
    g_force_accumulator_b[2] += force[2];
}

// ============================================================================
// 0x8009b904 — nav_get_force_b_ptr
// Returns pointer to global force accumulator B.
// ============================================================================
float* nav_get_force_b_ptr(void) {
    return g_force_accumulator_b;  // &DAT_80561c7c
}

// ============================================================================
// 0x8009b910 — player_init_spawn_position
// Initializes player spawn position (9 xrefs). Sets position at multiple
// offsets in player struct, resets velocity scales to 1.0, and queries
// floor_height_from_position. Called during spawn and teleport.
// ============================================================================
void player_init_spawn_position(int player_struct, float* position, int floor_param) {
    float scale = 1.0f;

    // Set position at 3 struct locations (current, previous, target)
    *(float*)(player_struct + 0x304) = position[0];
    *(float*)(player_struct + 0x288) = position[0];
    *(float*)(player_struct + 0x27C) = position[0];

    *(float*)(player_struct + 0x308) = position[1];
    *(float*)(player_struct + 0x28C) = position[1];
    *(float*)(player_struct + 0x280) = position[1];

    *(float*)(player_struct + 0x30C) = position[2];
    *(float*)(player_struct + 0x290) = position[2];
    *(float*)(player_struct + 0x284) = position[2];

    // Reset velocity/scale factors
    *(float*)(player_struct + 0x2E8) = scale;
    *(float*)(player_struct + 0x2EC) = scale;
    *(float*)(player_struct + 0x2F0) = scale;
    *(float*)(player_struct + 0xFD4) = scale;
    *(float*)(player_struct + 0xFD8) = scale;

    // Query or set floor height
    if (floor_param == 0) {
        int floor_h = floor_height_from_position(SPAWN_FLOOR_RADIUS, SPAWN_FLOOR_RADIUS);
        *(int*)(player_struct + 0x2A4) = floor_h;
        *(int*)(player_struct + 0x2A0) = floor_h;
    } else {
        *(int*)(player_struct + 0x2A4) = floor_param;
        *(int*)(player_struct + 0x2A0) = floor_param;
    }

    // Debug check: if floor is zero, call stub
    if (*(int*)(player_struct + 0x2A0) == 0) {
        nav_stub_empty();
    }
}

// ============================================================================
// 0x8009b9c0 — nav_stub_empty
// Empty stub (no-op). Debug/assert placeholder.
// ============================================================================
void nav_stub_empty(void) {
    return;
}


// ============================================================================
// 0x8009b9c4 — spawn_point_system_init
// MASSIVE spawn point system initialization. Loads level spawn point
// definitions, builds per-group spawn tables (0xC0 bytes per group),
// resolves linked path data, and allocates path segment arrays.
// Handles deduplication via group ID and mode flag filtering.
// ============================================================================
void spawn_point_system_init(void) {
    stack_save();

    int level_info = level_get_info_cached(g_current_level_id);
    int spawn_def_base = *(int*)(level_info + 0x44);
    int spawn_def_count = *(int*)(level_info + 0x48);
    int path_def_base = *(int*)(level_info + 0x4C);
    int path_def_count = *(int*)(level_info + 0x50);

    // Phase 1: Count unique groups
    int group_ids[15];
    memset(group_ids, 0xFF, 0x3C);  // sentinel -1
    int unique_count = 0;

    for (int i = 0; i < spawn_def_count; i++) {
        int* def = (int*)(spawn_def_base + i * 0x34);

        // Check mode compatibility
        if (!spawn_flags_match_mode(def[2])) continue;

        // Resolve group ID (use spawn ID if group is -1)
        if (def[1] == -1) def[1] = def[0];

        // Dedup check against known groups
        bool found = false;
        for (int g = 0; g < 15; g++) {
            if (group_ids[g] == -1) break;
            if (group_ids[g] == def[1]) { found = true; break; }
        }

        if (!found) {
            group_ids[unique_count] = def[1];
            unique_count++;
        }
    }

    // Allocate group table: 0xC0 bytes per group
    g_spawn_point_table = mem_alloc(unique_count * 0xC0, 1);
    g_spawn_point_count = unique_count;

    // Phase 2: Build per-group data
    memset(group_ids, 0xFF, 0x3C);
    int group_offset = 0;
    int group_id_idx = 0;

    for (int i = 0; i < spawn_def_count; i++) {
        int* def = (int*)(spawn_def_base + i * 0x34);

        if (!spawn_flags_match_mode(def[2])) continue;

        // Dedup
        bool found = false;
        for (int g = 0; g < 15; g++) {
            if (group_ids[g] == -1) break;
            if (group_ids[g] == def[1]) { found = true; break; }
        }
        if (found) continue;

        // Count members in this group
        int member_count = 1;
        int member_list[7];
        member_list[1] = 0;  // padding
        for (int j = 0; j < spawn_def_count; j++) {
            int* other = (int*)(spawn_def_base + j * 0x34);
            if (other[1] == -1) other[1] = other[0];
            if (j != i && other[1] == def[1]) {
                member_list[member_count + 1] = (int)other;
                member_count++;
            }
        }
        member_list[0] = (int)def;

        // Allocate per-member spawn point array (0x14 per member)
        int member_array = mem_alloc(member_count * 0x14, 1);
        *(int*)(g_spawn_point_table + group_offset + 0xA8) = member_array;
        *(int*)(g_spawn_point_table + group_offset + 0xBC) = def[1];  // group ID

        // Copy base properties
        *(int*)(g_spawn_point_table + group_offset) = def[9];       // position offset
        *(int*)(g_spawn_point_table + group_offset + 4) = def[12];  // rotation
        *(int*)(g_spawn_point_table + group_offset + 0xAC) = member_count;

        // Process each member's path data
        for (int m = 0; m < member_count; m++) {
            int* member_def = (int*)member_list[m];
            int* member_entry = (int*)(member_array + m * 0x14);

            // Find linked path segments
            int segment_count = 0;
            int segment_list[32];
            int spawn_id = member_def[0];

            if (spawn_id != -1) {
                for (int p = 0; p < path_def_count; p++) {
                    int* path = (int*)(path_def_base + p * 0x2C);
                    if (path[0] == spawn_id) {
                        segment_list[segment_count] = (int)path;
                        segment_count++;
                    }
                }
            }

            member_entry[2] = member_def[10];  // flags A
            member_entry[3] = member_def[11];  // flags B
            member_entry[4] = 0;               // runtime state
            member_entry[1] = segment_count;

            // Special case: single segment with aligned direction
            if ((member_def[9] == 3 || member_def[9] == 4) && segment_count == 1) {
                float* seg = (float*)segment_list[0];
                float dir_x = seg[4], dir_y = seg[5], dir_z = seg[6];

                if (dir_x == 1.0f && dir_y == 1.0f && dir_z == 1.0f) {
                    // Axis-aligned: compute extent from bounds
                    *(float*)(g_spawn_point_table + group_offset + 0x38) = seg[7] - seg[1];
                    *(float*)(g_spawn_point_table + group_offset + 0x3C) = seg[8] - seg[2];
                    *(float*)(g_spawn_point_table + group_offset + 0x40) = seg[9] - seg[3];
                } else {
                    // Non-aligned: copy direction directly
                    *(float*)(g_spawn_point_table + group_offset + 0x38) = dir_x;
                    *(float*)(g_spawn_point_table + group_offset + 0x3C) = dir_y;
                    *(float*)(g_spawn_point_table + group_offset + 0x40) = dir_z;
                }

                // Copy origin
                *(int*)(g_spawn_point_table + group_offset + 8) = *(int*)(segment_list[0] + 4);
                *(int*)(g_spawn_point_table + group_offset + 0xC) = *(int*)(segment_list[0] + 8);
                *(int*)(g_spawn_point_table + group_offset + 0x10) = *(int*)(segment_list[0] + 0xC);
                *(int*)(g_spawn_point_table + group_offset + 0xAC) = 0;  // mark as simple
            } else {
                // General case: allocate segment array (0x1C per segment)
                int seg_array = mem_alloc(segment_count * 0x1C, 1);
                for (int s = 0; s < segment_count; s++) {
                    int* src_seg = (int*)segment_list[s];
                    int* dst_seg = (int*)(seg_array + s * 0x1C);
                    dst_seg[0] = src_seg[1];  // start pos
                    dst_seg[1] = src_seg[2];
                    dst_seg[2] = src_seg[3];
                    dst_seg[3] = src_seg[4];  // end pos
                    dst_seg[4] = src_seg[5];
                    dst_seg[5] = src_seg[6];
                    dst_seg[6] = src_seg[10]; // flags
                }
                member_entry[0] = seg_array;
            }
        }

        group_offset += 0xC0;
        group_ids[group_id_idx] = def[1];
        group_id_idx++;
    }

    stack_restore();
}
