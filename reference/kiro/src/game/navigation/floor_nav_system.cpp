// Address: 0x800994f0-0x80099eac | Name: floor_nav_system | Date: 2026-08-11
// Floor height queries, polygon search, wall avoidance, and AI navigation target resolution.
// Core utilities used by 42+ callers including entity placement, weapon spawn, AI pathing.

#include "common.h"
#include "entity.h"
#include "scene.h"
#include "math_util.h"
#include "navigation.h"

// ============================================================================
// Globals
// ============================================================================

static float g_floor_height_best;       // FLOAT_80610490 — best floor height from last query
static float g_floor_height_secondary;  // FLOAT_80610494 — secondary floor (portal-traced)
static int g_floor_best_room_index;     // DAT_80611cf4 — room index of best floor hit
static int g_nav_wall_disable_flag;     // DAT_80611cf0 — when nonzero, skips wall flag 0x800 check

// Player data pool (0xEA0 bytes per slot)
extern void* g_player_data_pool;        // DAT_80611d90
// Wall list
extern void* g_wall_entity_list[64];    // DAT_80561a70
extern void* g_wall_data_list[64];      // DAT_80561b70
extern int g_wall_count;                // DAT_80611d00

#define FLOOR_INVALID_HEIGHT  (-99999.0f)
#define MAX_SCENE_QUERY_NODES 30  // 0x1E

// ============================================================================
// 0x800994f0 — player_copy_team_flag_data
// Copies team flag data between two player slots. If source has flag bit 4
// set and dest does not (or vice versa), propagates the flag reference.
// Used for CTF flag transfer on player interaction.
// ============================================================================
void player_copy_team_flag_data(int player_a, int player_b) {
    int* slot_a = (int*)((int)g_player_data_pool + player_a * 0xEA0);
    int* slot_b = (int*)((int)g_player_data_pool + player_b * 0xEA0);

    if ((slot_a[0x31B] & 4) == 0 && (slot_b[0x31B] & 4) != 0) {
        // Player B has flag, A does not — transfer B's flag ref to A
        FUN_800f3e84(&slot_a[0x31A], slot_b[0]);
    } else if ((slot_b[0x31B] & 4) != 0 && (slot_a[0x31B] & 4) == 0) {
        // Player A has flag, B does not — transfer A's flag ref to B
        FUN_800f3e84(&slot_b[0x31A], slot_a[0]);
    }
}

// ============================================================================
// 0x8009956c — floor_triangle_height_at_xz
// Computes Y height on a triangle surface at given X/Z coordinates.
// Uses cross product to derive plane normal, solves for Y.
// Returns negative Y (world-up convention).
//
// params:
//   param_1 = query X position
//   param_2 = query Z position
//   param_3 = triangle vertex A (float[3])
//   param_4 = triangle vertex B ptr offset
//   param_5 = triangle vertex C ptr offset
// ============================================================================
float floor_triangle_height_at_xz(float query_x, float query_z,
                                   float* vert_a, int vert_b_ptr, int vert_c_ptr) {
    float edge_ab[3];
    float edge_ac[3];

    // Compute edge vectors: AB and AC
    for (int i = 0; i < 3; i++) {
        edge_ab[i] = *(float*)(vert_b_ptr + i * 4) - vert_a[i];
        edge_ac[i] = *(float*)(vert_c_ptr + i * 4) - *(float*)(vert_b_ptr + i * 4);
    }

    // Cross product: normal = AB x AC
    float nx = edge_ab[2] * edge_ac[0] - edge_ab[0] * edge_ac[2];  // actually ny component
    float ny = edge_ab[1] * edge_ac[2] - edge_ab[2] * edge_ac[1];  // nz
    float nz = edge_ab[0] * edge_ac[1] - edge_ab[1] * edge_ac[0];  // nx

    // Plane equation: dot(normal, point - vert_a) = 0
    // Solve for Y: Y = -(nz*Z + nx*X + d) / ny  where d = dot(normal, vert_a)
    float d = ny * (float)query_x + nx * vert_a[1] + nz * (float)vert_a[2];
    float result = -(nz * query_z - (-(ny * query_x - d))) / nx;

    return result;
}

// ============================================================================
// 0x80099604 — floor_polygon_search
// MASSIVE floor polygon search (12 xrefs). Iterates polygon list from a room,
// tests AABB bounds, then half-plane containment for each polygon edge.
// For non-flat polygons (flag 0x10 not set), computes exact height via
// floor_triangle_height_at_xz. Returns highest floor polygon below query pos.
//
// params:
//   param_1, param_2 = unused/forwarded
//   param_3 = polygon filter flags (bitmask AND)
//   param_4 = query position float[3] (X, max_Y, Z)
//   param_5 = output: best polygon pointer (optional)
// returns: best floor height (via f1)
// ============================================================================
float floor_polygon_search(int unused1, int unused2, unsigned int flags_mask,
                           float* query_pos, void** out_polygon) {
    stack_save();

    int poly_count = stack_get_count();  // from room data
    float* poly_list = (float*)stack_get_data();
    float best_height = FLOOR_INVALID_HEIGHT;
    void* best_poly = NULL;
    float query_x = query_pos[0];
    float query_z = query_pos[2];
    float search_radius = /* from stack */;

    for (int i = 0; i < poly_count; i++) {
        int* poly = (int*)poly_list[i];

        // Filter: skip if flags don't match
        if (poly[1] != 0 && (poly[1] & flags_mask) == 0) continue;

        // Skip wall-flagged polygons when disabled
        unsigned short poly_flags = *(unsigned short*)(poly + 2);
        if ((poly_flags & 0x800) != 0 && g_nav_wall_disable_flag == 0) continue;

        // AABB test: check X/Z bounds with search radius
        if ((poly_flags & 0x10) == 0 || (float)poly[10] <= query_pos[1]) {
            if ((float)poly[5] > query_x + search_radius) continue;
            if (query_x - search_radius > (float)poly[7]) continue;
            if ((float)poly[6] > query_z + search_radius) continue;
            if (query_z - search_radius > (float)poly[8]) continue;
        }

        // Half-plane containment test for all edges
        bool inside = true;
        int edge_count = poly[0];
        for (int e = 0; e < edge_count; e++) {
            float* edge = (float*)(poly + 9 + e * 6);  // 0x18 bytes per edge (6 floats)
            // Cross product sign test: (X - edge_origin) x edge_normal < 0 => outside
            float cross = edge[4] * (query_x - edge[0]) - edge[3] * (query_z - edge[2]);
            if (cross < -EPSILON) {
                inside = false;
                break;
            }
        }

        if (!inside) continue;

        // Compute height at query X/Z
        float height;
        if ((poly_flags & 0x10) == 0) {
            // Non-flat polygon: interpolate via triangle
            height = floor_triangle_height_at_xz(query_x, query_z,
                        (float*)(poly + 9), (int)(poly + 15), (int)(poly + 21));
        } else {
            // Flat polygon: constant height
            height = (float)poly[10];
        }

        // Keep best (highest) that is still below query max_Y
        if (height > best_height && height <= query_pos[1]) {
            best_poly = poly;
            best_height = height;
        }
    }

    if (out_polygon != NULL) {
        *out_polygon = best_poly;
    }

    stack_restore();
    return best_height;
}

// ============================================================================
// 0x800997f4 — floor_room_query
// Room-level floor query (8 xrefs). Resolves room index, calls
// floor_polygon_search, then optionally combines with
// entity_find_best_interact_target for interactable object detection.
//
// params:
//   param_3 = output: best entity (optional)
//   param_4 = output: interact type (optional)
//   param_5 = skip_interact flag (1 = floor only, 0 = also check interact)
// returns: best height
// ============================================================================
float floor_room_query(int room_index, float* query_pos,
                       void** out_entity, int* out_interact_type, int skip_interact) {
    stack_save();

    int resolved_room = stack_resolve();
    float best_height = FLOOR_INVALID_HEIGHT;
    void* best_entity = NULL;
    int best_interact = 0;

    if (resolved_room == 0 || resolved_room > g_max_room_index) {
        if (out_entity != NULL) *out_entity = NULL;
        if (out_interact_type != NULL) *out_interact_type = 0;
        stack_restore();
        return best_height;
    }

    // Get room polygon data
    int room_data = g_room_data_base + resolved_room * 4;
    int polygon_set_a = *(int*)(room_data + 0x1E0);
    int polygon_set_b = *(int*)(room_data + 0x1F0);
    unsigned int filter_flags = *(unsigned int*)(*(int*)(room_data + 4) + 0x30);

    // Query floor polygons
    float floor_h = floor_polygon_search(polygon_set_a, polygon_set_b, filter_flags,
                                          query_pos, &best_entity);

    // Track global best
    if (floor_h > g_floor_height_best) {
        g_floor_height_best = floor_h;
        g_floor_best_room_index = resolved_room;
    }

    // Optionally query interact targets
    float interact_h = FLOOR_INVALID_HEIGHT;
    if (skip_interact == 0) {
        interact_h = entity_find_best_interact_target(resolved_room, query_pos,
                                                      &best_entity, &best_interact);
    }

    // Return better of floor vs interact
    if (interact_h > floor_h) {
        best_entity = best_entity;  // interact target wins
        if (out_interact_type != NULL) *out_interact_type = best_interact;
    } else {
        if (out_interact_type != NULL) *out_interact_type = 0;
    }

    if (out_entity != NULL) *out_entity = best_entity;

    stack_restore();
    return (interact_h > floor_h) ? interact_h : floor_h;
}

// ============================================================================
// 0x8009994c — floor_height_from_position
// Primary floor height query (42 xrefs!). Given world position + search radius,
// queries up to 30 scene nodes via scene_query_nodes_flat_y, calls
// floor_room_query on each, picks highest valid floor. Then traces a portal
// ray (scene_ray_traverse_portals) for cross-room resolution.
// Core utility for entity placement, weapon spawn, AI pathing.
// ============================================================================
float floor_height_from_position(float* position, float search_radius) {
    stack_save();

    float* pos = get_stack_position();
    float best_height = FLOOR_INVALID_HEIGHT;
    int best_room = 0;

    // Build AABB from position + radius
    float aabb[6];
    aabb[0] = pos[0] - search_radius;  // min X
    aabb[1] = pos[0] + search_radius;  // max X
    aabb[2] = pos[1];                   // Y (floor level)
    aabb[3] = pos[1] + search_radius;   // max Y
    aabb[4] = pos[2] - search_radius;  // min Z
    aabb[5] = pos[2] + search_radius;  // max Z

    // Query overlapping scene nodes (rooms)
    int room_list[MAX_SCENE_QUERY_NODES];
    int room_count = scene_query_nodes_flat_y(0, aabb, room_list, MAX_SCENE_QUERY_NODES);

    // Find best floor across all rooms
    for (int i = 0; i < room_count; i++) {
        float h = floor_room_query(room_list[i], pos, NULL, NULL, 1);
        if (h > best_height) {
            best_room = room_list[i];
            best_height = h;
        }
    }

    // Portal ray trace for cross-room verification
    if (best_height != FLOOR_INVALID_HEIGHT) {
        float ray_origin[3] = { pos[0], best_height, pos[2] };
        float ray_dir[3] = { 0.0f, pos[1] - best_height, 0.0f };
        int portal_result = scene_ray_traverse_portals(
            ray_dir[1], pos[1], ray_origin, best_room, ray_dir, 0);

        if (portal_result > 0) {
            g_floor_height_secondary = best_height;
        }
    }

    stack_restore();
    return best_height;
}

// ============================================================================
// 0x80099aa4 — floor_query_with_interact
// Floor query with interact target output (36 xrefs). Similar to
// floor_height_from_position but also returns best interact target entity
// and interaction type. Falls back to entity_find_best_interact_target if
// no scene nodes found.
// ============================================================================
void floor_query_with_interact(float* position, float search_radius,
                                int param_3, int param_4,
                                int* out_interact_type, int skip_interact) {
    stack_save();

    float* pos = get_stack_position();
    void* best_entity = NULL;
    int best_interact = 0;
    float best_height = FLOOR_INVALID_HEIGHT;

    // Build AABB
    float aabb[7];
    aabb[0] = pos[0] - search_radius;
    aabb[1] = pos[0] + search_radius;
    aabb[2] = pos[1];
    aabb[3] = pos[1] + search_radius;
    aabb[4] = pos[2] - search_radius;
    aabb[5] = pos[2] + search_radius;

    // Query scene nodes
    int room_list[MAX_SCENE_QUERY_NODES + 17];  // extra space for interact results
    int room_count = scene_query_nodes_flat_y(0, aabb, room_list, MAX_SCENE_QUERY_NODES);

    // Prepare floor query position
    float query_pos[3] = { pos[0], pos[1] + search_radius, pos[2] };
    g_floor_height_best = FLOOR_INVALID_HEIGHT;

    // Search all rooms
    for (int i = 0; i < room_count; i++) {
        int interact_type = 0;
        int interact_entity = 0;
        float h = floor_room_query(room_list[i], query_pos, &interact_entity,
                                    &interact_type, skip_interact);
        if (h > best_height) {
            best_interact = interact_type;
            best_entity = (void*)interact_entity;
            best_height = h;
        }
    }

    // Fallback: if no rooms found, try interact target at default room 0xD5
    if (room_count == 0) {
        int fallback_entity = 0;
        int fallback_type = 0;
        float h = entity_find_best_interact_target(0xD5, pos, &fallback_entity, &fallback_type);
        if (h > best_height) {
            best_interact = fallback_type;
            best_entity = (void*)fallback_entity;
        }
    }

    // Output results
    void** out_entity_ptr = (void**)get_output_ptr();
    if (out_entity_ptr != NULL) {
        *out_entity_ptr = best_entity;
    }
    if (out_interact_type != NULL) {
        *out_interact_type = best_interact;
    }

    // Debug: if no entity found, call heap_arena_lookup (debug/assert?)
    if (best_entity == NULL) {
        heap_arena_lookup();
    }

    stack_restore();
}

// ============================================================================
// 0x80099c64 — nav_line_segment_distance_2d
// 2D perpendicular distance from point to a line segment.
// Computes closest point on segment (clamped to [0,1] parametric range),
// returns squared distance. If below threshold, outputs segment normal.
//
// params:
//   param_1 = current best distance squared (threshold)
//   param_2 = query position (float[3], uses X and Z)
//   param_3 = wall/segment data pointer (has start XZ at +8, end XZ at +0x14/+0x1C)
//   param_4 = output normal float[2] (optional)
// returns: squared distance to segment (or large value if wrong side)
// ============================================================================
float nav_line_segment_distance_2d(float best_dist_sq, float* query_pos,
                                    int segment_ptr, float* out_normal) {
    float seg_start_x = *(float*)(segment_ptr + 8);
    float seg_start_z = *(float*)(segment_ptr + 0x10);
    float seg_end_x = *(float*)(segment_ptr + 0x14);
    float seg_end_z = *(float*)(segment_ptr + 0x1C);

    // Segment direction vector
    float dx = seg_end_x - seg_start_x;
    float dz = seg_end_z - seg_start_z;

    // Vector from start to query point
    float px = query_pos[0] - seg_start_x;
    float pz = query_pos[2] - seg_start_z;

    // Check which side of segment the point is on (cross product sign)
    float cross = dz * px - dx * pz;
    if (cross < SMALL_EPSILON) {
        // Wrong side of wall — return large distance
        return LARGE_DISTANCE;
    }

    // Project query point onto segment (parametric t)
    float dot = px * dx + pz * dz;
    float seg_len_sq = dx * dx + dz * dz;

    float closest_x, closest_z;
    if (dot <= SMALL_EPSILON) {
        // Before segment start
        closest_x = px;
        closest_z = pz;
    } else if (dot < seg_len_sq) {
        // On segment: project
        float t = dot / seg_len_sq;
        closest_x = -(t * dx - px);
        closest_z = -(t * dz - pz);
    } else {
        // After segment end
        closest_x = px - dx;
        closest_z = pz - dz;
    }

    float dist_sq = closest_x * closest_x + closest_z * closest_z;

    // If this is a new closest hit, compute and output the normal
    if (out_normal != NULL && dist_sq < best_dist_sq) {
        float seg_len = sqrtf(seg_len_sq);
        out_normal[0] = dz / seg_len;    // perpendicular normal X
        out_normal[1] = -dx / seg_len;   // perpendicular normal Z
    }

    return dist_sq;
}

// ============================================================================
// 0x80099d74 — nav_find_nearest_wall
// Finds nearest wall segment to a position. Iterates wall entity list
// (g_wall_entity_list, up to g_wall_count entries), skips walls without
// flag 0x40, calls nav_line_segment_distance_2d on each. Outputs closest
// wall entity, distance (sqrt), and normal direction.
// Called by nav_resolve_movement_target.
// ============================================================================
void nav_find_nearest_wall(void) {
    stack_save();

    int result_ptr = get_stack_result();
    float* query_pos = get_stack_position();

    float best_dist_sq = query_pos[1] * query_pos[1] + WALL_SEARCH_BIAS;  // initial threshold
    void* best_entity = NULL;
    float best_normal[2] = { 0.0f, 0.0f };
    bool found = false;

    for (int i = 0; i < g_wall_count; i++) {
        // Skip walls without flag 0x40 (active/collidable)
        if ((*(unsigned int*)(g_wall_entity_list[i] + 0x28) & 0x40) == 0) continue;

        int wall_data = (int)g_wall_data_list[i];
        float normal[2];
        float dist_sq = nav_line_segment_distance_2d(best_dist_sq, query_pos, wall_data, normal);

        if (dist_sq < best_dist_sq) {
            best_normal[0] = normal[0];
            best_normal[1] = normal[1];
            best_entity = g_wall_entity_list[i];
            found = true;
            best_dist_sq = dist_sq;
        }
    }

    // Write results to output structure
    if (result_ptr != 0) {
        *(int*)(result_ptr + 0x14) = (int)best_entity;  // wall data ptr
        *(float*)(result_ptr + 0x18) = best_normal[0];
        *(float*)(result_ptr + 0x1C) = best_normal[1];
        if (found) {
            *(float*)(result_ptr + 0x20) = sqrtf(best_dist_sq);
        } else {
            *(float*)(result_ptr + 0x20) = query_pos[1];  // fallback
        }
        *(void**)(result_ptr + 0x24) = best_entity;
    }

    stack_restore();
}

// ============================================================================
// 0x80099eac — nav_resolve_movement_target
// AI navigation movement target resolver. Combines:
//   1. nav_find_nearest_wall — wall avoidance direction
//   2. entity_find_nearest_steering_target — AI steering toward targets
//   3. entity_check_vehicle_mount — vehicle mount check
//   4. FUN_8009a2d0 — trace/slide movement along floor
//
// Computes desired movement position by projecting toward nearest wall
// normal, clamped by frame-rate-scaled max speed. If wall is very close,
// projects full distance. Handles vehicle mount exclusion.
//
// Output: resolved target position + navigation state in output structs.
// ============================================================================
void nav_resolve_movement_target(float* position, float search_radius) {
    stack_save();

    int nav_state = get_stack_state();
    float* target_pos = get_stack_position();

    // Build AABB for spatial queries
    float aabb[6];
    aabb[0] = target_pos[0] - search_radius;
    aabb[1] = target_pos[0] + search_radius;
    aabb[2] = target_pos[1];
    aabb[3] = target_pos[1] + search_radius;
    aabb[4] = target_pos[2] - search_radius;
    aabb[5] = target_pos[2] + search_radius;

    // Find nearest wall
    int wall_entity = 0;
    float wall_normal[2] = { 0.0f, 0.0f };
    float wall_dist = search_radius;
    void* wall_result = NULL;

    nav_find_nearest_wall();  // writes to local state

    // Check wall result and vehicle mount
    if (local_result.entity != 0 && local_result.distance < search_radius) {
        if (entity_check_vehicle_mount(local_result.entity) == 0) {
            wall_normal[0] = local_result.normal_x;
            wall_normal[1] = local_result.normal_z;
            wall_dist = local_result.distance;
            wall_entity = local_result.entity;
            wall_result = local_result.raw;
        }
    }

    // Find nearest steering target
    entity_find_nearest_steering_target(search_radius, search_radius,
                                         nav_state, aabb, target_pos, local_buffer);

    if (local_result2.entity != 0 && local_result2.distance < wall_dist) {
        if (entity_check_vehicle_mount(local_result2.entity) == 0) {
            wall_normal[0] = local_result2.normal_x;
            wall_normal[1] = local_result2.normal_z;
            wall_dist = local_result2.distance;
            wall_entity = local_result2.entity;
            wall_result = local_result2.raw;
        }
    }

    // Resolve movement
    if (wall_dist >= search_radius) {
        // No nearby wall — stay at current position
        *(int*)(nav_state + 0x18) = 0;  // no wall contact
        if (out_pos != NULL) {
            out_pos[0] = target_pos[0];
            out_pos[1] = target_pos[1];
            out_pos[2] = target_pos[2];
        }
    } else {
        // Move toward wall
        *(int*)(nav_state + 0x18) = 1;  // wall contact active
        float remaining = search_radius - wall_dist;
        float max_step = NAV_MAX_SPEED * g_frame_delta_time;
        float step = (remaining < max_step) ? remaining : max_step;

        // Check if wall entity has flag 2 (force full projection)
        if (wall_entity != 0 && (*(unsigned int*)(*(int*)(wall_entity + 0x24) + 0x28) & 2) != 0) {
            if (fabsf(step * wall_normal[0]) < fabsf(remaining * wall_normal[0]) ||
                fabsf(step * wall_normal[1]) < fabsf(remaining * wall_normal[1])) {
                step = remaining;  // force full distance
            }
        }

        // Compute target position
        float move_target[3];
        move_target[0] = target_pos[0] + step * wall_normal[0];
        move_target[1] = target_pos[1];
        move_target[2] = target_pos[2] + step * wall_normal[1];

        // Trace and slide
        unsigned int trace_flags = FUN_8009a2d0(search_radius, search_radius,
                                                 nav_state, target_pos, move_target,
                                                 out_pos, NULL, 0, 0);
        if ((trace_flags & 4) != 0) {
            // Hit something during slide — re-trace from slide result
            FUN_8009a2d0(search_radius, search_radius,
                         nav_state, move_target, out_pos, NULL, NULL, 0, 0);
        }
    }

    // Clear pending flag
    *(int*)(nav_state + 0x10) = 0;

    // Write navigation output
    if (out_nav != NULL) {
        *(void**)(out_nav + 0x14) = wall_result;
        *(float*)(out_nav + 0x18) = wall_normal[0];
        *(float*)(out_nav + 0x1C) = wall_normal[1];
        *(float*)(out_nav + 0x20) = wall_dist;
        *(int*)(out_nav + 0x24) = wall_entity;
    }

    stack_restore();
}
