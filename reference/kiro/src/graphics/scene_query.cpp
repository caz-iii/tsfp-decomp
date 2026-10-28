// Address: 0x80058e38 - 0x800594ac | Batch 118 | Date: 2026-08-11
// Scene spatial query system: frustum culling, portal traversal,
// point containment, and KD-tree queries.
// Part of the scene system (follows scene_render.cpp)

#include "common.h"
#include "scene.h"
#include "memory.h"

// ============================================================================
// Address: 0x80058e38 | scene_traverse_portals_recursive
// Recursive portal traversal for PVS (Potentially Visible Set).
// Starting from a node index, walks connected portals via the vertex buffer
// adjacency lists (node+0x04 → counted array of material indices).
// For each connected material: AABB-tests against frustum (param pfVar4).
// If intersection: determines the neighbor node from the vertex buffer's
// endpoint pair (picks the one != current), then recurses.
// Deduplicates via linear search of result list.
// Params: node_index, frustum_bounds[6], result_array, result_count_ptr, max
// ============================================================================
void scene_traverse_portals_recursive(int node_index, float* frustum,
                                       int result_array, int* result_count,
                                       int max_results) {
    // Check if already in result list
    for (int i = 0; i < *result_count; i++) {
        if (*(int*)(result_array + i * 4) == node_index) {
            return; // Already visited
        }
    }

    if (*result_count >= max_results) return;

    // Add to results
    *(int*)(result_array + *result_count * 4) = node_index;
    *result_count += 1;

    int node_entry = DAT_80611aa8 + node_index * 0xB4;
    if (*(int*)(node_entry + 4) == 0) return;

    // Walk portal adjacency list
    int adj_count = **(int**)(node_entry + 4);
    int adj_base = *(int*)(node_entry + 4) + 4;

    for (int i = 0; i < adj_count; i++) {
        int mat_idx = *(int*)(adj_base + i * 4);
        int mat_state = DAT_80611a98 + mat_idx * 0x28;

        // AABB-frustum intersection test
        if (*(float*)(mat_state + 0x08) <= frustum[3] &&
            frustum[0] <= *(float*)(mat_state + 0x14) &&
            *(float*)(mat_state + 0x0C) <= frustum[4] &&
            frustum[1] <= *(float*)(mat_state + 0x18) &&
            *(float*)(mat_state + 0x10) <= frustum[5] &&
            frustum[2] <= *(float*)(mat_state + 0x1C)) {

            // Get neighbor node from vertex buffer endpoint pair
            int* vbuf = *(int**)(DAT_80611aac + mat_idx * 4);
            int neighbor;
            if (vbuf[0] == node_index) {
                neighbor = vbuf[1];
            } else {
                neighbor = vbuf[0];
            }

            // Recurse into neighbor
            scene_traverse_portals_recursive(neighbor, frustum,
                                             result_array, result_count, max_results);
        }
    }
}

// ============================================================================
// Address: 0x80058fcc | scene_query_frustum_nodes
// Convenience wrapper: queries nodes visible from a frustum with a
// stack-allocated buffer of 50 entries (200 bytes).
// ============================================================================
void scene_query_frustum_nodes(int param_1, int frustum) {
    u8 buffer[200]; // max 50 results
    scene_query_nodes_in_frustum(param_1, frustum, (int)buffer, 0x32);
}

// ============================================================================
// Address: 0x80058ff4 | scene_find_nodes_containing_point
// Linear scan: finds all scene sub-nodes whose AABB (fields +0x70..+0x84)
// contains the given 3D point. Returns matching indices in output array.
// Capped by param_3 (max results). Returns count found.
// Note: Uses the OUTER bounds (+0x70..+0x84), not inner (+0x10..+0x24).
// ============================================================================
int scene_find_nodes_containing_point(float* point, int result_array, int max_results) {
    int count = 0;

    for (int i = 1; i <= DAT_80611b14; i++) {
        int node = DAT_80611aa8 + i * 0xB4;

        if (*(float*)(node + 0x70) <= point[0] && point[0] <= *(float*)(node + 0x7C) &&
            *(float*)(node + 0x74) <= point[1] && point[1] <= *(float*)(node + 0x80) &&
            *(float*)(node + 0x78) <= point[2] && point[2] <= *(float*)(node + 0x84)) {
            *(int*)(result_array + count * 4) = i;
            count++;
            if (count == max_results) {
                return count;
            }
        }
    }
    return count;
}

// ============================================================================
// Address: 0x800590b8 | scene_build_node_spatial_tree
// Builds a SECONDARY spatial tree (stored at DAT_805293a8) from scene node
// AABBs (fields +0x10..+0x24). Uses depth params [4, 6].
// Differs from the primary tree (DAT_80610408, depth [6,6]) which uses
// entity positions ± radius. This tree is for frustum-based scene queries.
// ============================================================================
void scene_build_node_spatial_tree(void) {
    int node_count = DAT_80611b14;
    int temp_buf = mem_page_alloc();

    for (int i = 1; i < node_count + 1; i++) {
        int idx = i - 1;
        *(s16*)(temp_buf + idx * 0x1C) = (s16)i;
        // Copy AABB min (fields +0x10..+0x18)
        *(u32*)(temp_buf + idx * 0x1C + 0x04) = *(u32*)(DAT_80611aa8 + i * 0xB4 + 0x10);
        *(u32*)(temp_buf + idx * 0x1C + 0x08) = *(u32*)(DAT_80611aa8 + i * 0xB4 + 0x14);
        *(u32*)(temp_buf + idx * 0x1C + 0x0C) = *(u32*)(DAT_80611aa8 + i * 0xB4 + 0x18);
        // Copy AABB max (fields +0x1C..+0x24)
        *(u32*)(temp_buf + idx * 0x1C + 0x10) = *(u32*)(DAT_80611aa8 + i * 0xB4 + 0x1C);
        *(u32*)(temp_buf + idx * 0x1C + 0x14) = *(u32*)(DAT_80611aa8 + i * 0xB4 + 0x20);
        *(u32*)(temp_buf + idx * 0x1C + 0x18) = *(u32*)(DAT_80611aa8 + i * 0xB4 + 0x24);
    }

    int depth_params[2] = {4, 6};
    FUN_80134914(&DAT_805293a8, &depth_params, temp_buf, node_count,
                 scene_spatial_alloc_callback);
    mem_page_free(temp_buf);
}

// ============================================================================
// Address: 0x800591fc | scene_spatial_frustum_test_callback
// KD-tree leaf callback for frustum queries.
// For each candidate node in the leaf:
//   - Skips if already visited (flag 0x10 at +0xB2)
//   - Sets visited flag
//   - Calls FUN_800117cc (frustum-AABB test) with node's AABB (+0x10..+0x24)
//   - Encodes result as: (node_index << 1) | inside_flag
//   - Appends to result list (param_3 = counted u16 array)
// ============================================================================
void scene_spatial_frustum_test_callback(float* frustum_min, float* frustum_max,
                                          u16* result_list, int candidate_count,
                                          int candidate_indices) {
    u32 current_count = (u32)*result_list;

    for (int i = 0; i < candidate_count; i++) {
        u16 node_idx = *(u16*)(candidate_indices + i * 2);
        u32 idx = (u32)node_idx;

        // Skip if already tested this frame
        if ((*(u16*)(DAT_80611aa8 + idx * 0xB4 + 0xB2) & 0x10) != 0) {
            continue;
        }

        int node_entry = DAT_80611aa8 + idx * 0xB4;
        *(u16*)(node_entry + 0xB2) |= 0x10; // Mark visited

        // Frustum-AABB intersection test
        u32 inside = FUN_800117cc(frustum_min, frustum_max,
                                   (float*)(node_entry + 0x10),
                                   (float*)(node_entry + 0x1C));

        // Encode: (index << 1) | inside_flag
        result_list[current_count + 1] = (u16)((node_idx << 1) | (u16)inside);
        current_count++;
    }
    *result_list = (u16)current_count;
}

// ============================================================================
// Address: 0x800592e8 | scene_test_frustum_vs_world
// Tests a frustum against the world AABB (DAT_8052929c = min, DAT_805292a8 = max).
// Simple wrapper around FUN_800117cc (generic frustum-AABB test).
// Used for early-out: if frustum doesn't intersect world, skip all queries.
// ============================================================================
void scene_test_frustum_vs_world(float* frustum) {
    FUN_800117cc(frustum, frustum + 3, &DAT_8052929c, &DAT_805292a8);
}

// ============================================================================
// Address: 0x80059320 | scene_query_spatial_tree
// Queries the secondary spatial tree (DAT_805293a8) with a frustum.
// 1. Calls FUN_801341d8 (tree traversal) with frustum test callback
// 2. Unpacks encoded results: node_index = result >> 1, inside = result & 1
// 3. Clears visited flag (0x10) on all tested nodes
// 4. Copies passing node indices (inside=1) to output array
// Returns count of fully-inside nodes (capped by param_3).
// ============================================================================
int scene_query_spatial_tree(int frustum, int result_array, int max_results) {
    u16 local_results[216]; // [0]=count, [1..215]=encoded results
    local_results[0] = 0;

    FUN_801341d8(&DAT_805293a8, frustum, (float*)(frustum + 0xC),
                 scene_spatial_frustum_test_callback, local_results);

    int out_count = 0;
    u32 allow_add = 1;

    for (int i = 0; i < (int)(u32)local_results[0]; i++) {
        u16 encoded = local_results[i + 1]; // renamed from local_1b6
        int node_idx = (int)(u32)encoded >> 1;
        int node_entry = DAT_80611aa8 + node_idx * 0xB4;

        // Clear visited flag
        *(u16*)(node_entry + 0xB2) &= ~0x10;

        // If inside and still have room
        if ((encoded & allow_add) != 0) {
            *(int*)(result_array + out_count * 4) = node_idx;
            out_count++;
            if (out_count == max_results) {
                allow_add = 0; // Stop adding but keep clearing flags
            }
        }
    }
    return out_count;
}

// ============================================================================
// Address: 0x800593f0 | scene_query_nodes_in_frustum
// Safe wrapper around scene_query_spatial_tree.
// Returns 0 if scene is not loaded (DAT_80611aa8 == 0).
// Otherwise delegates to scene_query_spatial_tree.
// ============================================================================
int scene_query_nodes_in_frustum(int param_1, int frustum, int result_array, int max_results) {
    if (DAT_80611aa8 == 0) {
        return 0;
    }
    return scene_query_spatial_tree(frustum, result_array, max_results);
}

// ============================================================================
// Address: 0x80059430 | scene_get_node_aabb
// Getter: copies node's AABB min and max vectors to output parameters.
// Min = fields +0x10, +0x14, +0x18 (vec3)
// Max = fields +0x1C, +0x20, +0x24 (vec3)
// ============================================================================
void scene_get_node_aabb(int node_index, float* out_min, float* out_max) {
    int base = DAT_80611aa8 + node_index * 0xB4;
    out_min[0] = *(float*)(base + 0x10);
    out_min[1] = *(float*)(base + 0x14);
    out_min[2] = *(float*)(base + 0x18);
    out_max[0] = *(float*)(base + 0x1C);
    out_max[1] = *(float*)(base + 0x20);
    out_max[2] = *(float*)(base + 0x24);
}

// ============================================================================
// Address: 0x800594ac | scene_point_in_node_aabb
// Point-in-AABB test for a single node. Tests position (vec3) against
// node's inner AABB (fields +0x10..+0x24). Uses strict less-than.
// Returns 1 if point is inside, 0 otherwise.
// ============================================================================
int scene_point_in_node_aabb(int node_index, float* point) {
    int base = DAT_80611aa8 + node_index * 0xB4;

    if (*(float*)(base + 0x10) < point[0] && point[0] < *(float*)(base + 0x1C) &&
        *(float*)(base + 0x14) < point[1] && point[1] < *(float*)(base + 0x20) &&
        *(float*)(base + 0x18) < point[2] && point[2] < *(float*)(base + 0x24)) {
        return 1;
    }
    return 0;
}
