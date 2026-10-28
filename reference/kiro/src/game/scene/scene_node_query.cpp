// =============================================================================
// scene_node_query.cpp - Scene node spatial queries and portal traversal
// TimeSplitters: Future Perfect (GameCube) decompilation
// Batch 145 | Date: 2026-08-10
// =============================================================================

#include "scene.h"

// Forward declarations
extern void FUN_803826dc(void);  // returns pair (buffer_ptr, node_id)
extern void FUN_80382728(void);
extern void scene_traverse_portals_recursive(int start_node, float* aabb, int result_buf, int* count, int max_results);
extern void scene_query_spatial_tree(int aabb_ptr, int result_buf, int max_results);
extern int scene_portal_is_open(int portal_index);

// Globals
extern int DAT_80611aa8;   // Scene node descriptor array base
extern int DAT_80611aac;   // Portal table base pointer
extern int DAT_80611b04;   // Render data pointer table
extern int DAT_80611b14;   // Total active scene node count
extern float FLOAT_80613dd4;  // Global floor-height constant for flat queries

// Scene node descriptor layout (stride = 0xB4):
//   +0x04: int* portal_list (first int = count, then portal indices)
//   +0x28: float outer_bounds_min_x
//   +0x2C: float outer_bounds_min_y
//   +0x30: float outer_bounds_min_z
//   +0x34: float outer_bounds_max_x
//   +0x38: float outer_bounds_max_y
//   +0x3C: float outer_bounds_max_z
//   +0x40: float inner_bounds_min_x
//   +0x44: float inner_bounds_min_y
//   +0x48: float inner_bounds_min_z
//   +0x4C: float inner_bounds_max_x
//   +0x50: float inner_bounds_max_y
//   +0x54: float inner_bounds_max_z
//   +0xB2: uint16 flags

// =============================================================================
// Address: 0x80059524 | scene_query_nodes_outer_bounds
// Queries scene nodes whose OUTER bounding box (+0x28..+0x3C) overlaps the
// given AABB. If start_node is valid (1-200), uses portal traversal;
// otherwise brute-forces all nodes. Returns count of matching node IDs.
// =============================================================================
int scene_query_nodes_outer_bounds(int start_node, float* aabb, int result_buf, int max_results)
{
    int count = 0;

    if (start_node < 1 || start_node > 200) {
        // Brute force: test all active nodes
        for (int i = 1; i <= DAT_80611b14; i++) {
            int node_ptr = DAT_80611aa8 + i * 0xB4;

            // AABB overlap test (6 separating axis checks)
            if (aabb[0] <= *(float*)(node_ptr + 0x34) &&
                *(float*)(node_ptr + 0x28) <= aabb[3] &&
                aabb[1] <= *(float*)(node_ptr + 0x38) &&
                *(float*)(node_ptr + 0x2C) <= aabb[4] &&
                aabb[2] <= *(float*)(node_ptr + 0x3C) &&
                *(float*)(node_ptr + 0x30) <= aabb[5]) {

                *(int*)(result_buf + count * 4) = i;
                count++;
                if (count == max_results) {
                    return count;
                }
            }
        }
    } else {
        // Portal-based traversal from start node
        scene_traverse_portals_recursive(start_node, aabb, result_buf, &count, max_results);
    }

    return count;
}

// =============================================================================
// Address: 0x80059630 | scene_query_nodes_flat_y
// Patches the Y min of the query AABB with a global floor-height constant
// before calling scene_query_spatial_tree. Used for 2D (plan-view) queries
// ignoring vertical extent.
// =============================================================================
void scene_query_nodes_flat_y(int param_1, float* aabb_in, int result_buf, int max_results)
{
    float local_aabb[6];

    local_aabb[0] = aabb_in[0];
    local_aabb[1] = FLOAT_80613dd4;  // Override Y min with floor height
    local_aabb[2] = aabb_in[2];
    local_aabb[3] = aabb_in[3];
    local_aabb[4] = aabb_in[4];
    local_aabb[5] = aabb_in[5];

    scene_query_spatial_tree((int)local_aabb, result_buf, max_results);
}

// =============================================================================
// Address: 0x8005968c | scene_node_set_flags
// Sets (ORs) flag bits on a scene node descriptor at offset +0xB2.
// Node descriptor stride is 0xB4, base at DAT_80611aa8.
// =============================================================================
void scene_node_set_flags(int node_id, unsigned short flags)
{
    int node_ptr = DAT_80611aa8 + node_id * 0xB4;
    *(unsigned short*)(node_ptr + 0xB2) |= flags;
}

// =============================================================================
// Address: 0x800596ac | scene_query_nodes_inner_bounds
// Queries scene nodes whose INNER bounding box (+0x40..+0x54) overlaps the
// given AABB. Same logic as outer bounds but uses tighter inner bounds.
// =============================================================================
int scene_query_nodes_inner_bounds(int start_node, float* aabb, int result_buf, int max_results)
{
    int count = 0;

    if (start_node < 1 || start_node > 200) {
        // Brute force: test all active nodes
        for (int i = 1; i <= DAT_80611b14; i++) {
            int node_ptr = DAT_80611aa8 + i * 0xB4;

            // AABB overlap test against inner bounds
            if (aabb[0] <= *(float*)(node_ptr + 0x4C) &&
                *(float*)(node_ptr + 0x40) <= aabb[3] &&
                aabb[1] <= *(float*)(node_ptr + 0x50) &&
                *(float*)(node_ptr + 0x44) <= aabb[4] &&
                aabb[2] <= *(float*)(node_ptr + 0x54) &&
                *(float*)(node_ptr + 0x48) <= aabb[5]) {

                *(int*)(result_buf + count * 4) = i;
                count++;
                if (count == max_results) {
                    return count;
                }
            }
        }
    } else {
        scene_traverse_portals_recursive(start_node, aabb, result_buf, &count, max_results);
    }

    return count;
}

// =============================================================================
// Address: 0x800597b8 | scene_get_neighbor_nodes_temp
// Gets unique neighbor node IDs through portal connectivity.
// Uses a temporary buffer allocated via FUN_803826dc.
// Deduplicates results. Portal validity checked via scene_portal_is_open.
// =============================================================================
void scene_get_neighbor_nodes_temp(int param_1, int param_2, int max_count)
{
    int node_id;
    int* temp_buffer;

    // FUN_803826dc returns pair: temp_buffer (low), node_id (high)
    FUN_803826dc();
    // node_id = ...; temp_buffer = ...;

    int* portal_list = *(int**)(DAT_80611aa8 + node_id * 0xB4 + 4);
    if (portal_list == NULL) return;

    int num_portals = *portal_list;
    if (max_count < num_portals) {
        num_portals = max_count;
    }

    int result_count = 0;

    for (int i = 0; i < num_portals; i++) {
        int* portal = *(int**)(DAT_80611aac + portal_list[i + 1] * 4);

        // Get the other side of the portal
        int neighbor;
        if (*portal == node_id) {
            neighbor = portal[1];
        } else {
            neighbor = *portal;
        }

        // Only include if portal is open
        if (!scene_portal_is_open(portal_list[i + 1])) continue;

        // Deduplicate
        bool duplicate = false;
        for (int j = 0; j < result_count; j++) {
            if (neighbor == temp_buffer[j]) {
                duplicate = true;
                break;
            }
        }

        if (!duplicate) {
            temp_buffer[result_count] = neighbor;
            result_count++;
        }
    }

    FUN_80382728();
}

// =============================================================================
// Address: 0x800598c4 | scene_get_neighbor_nodes
// Gets unique neighbor node IDs through portal connectivity for a given node.
// Writes neighbor IDs into caller-provided buffer.
// Each portal connects two nodes; picks the other side.
// Returns count of unique neighbors written.
// =============================================================================
int scene_get_neighbor_nodes(int node_id, int out_buffer, int max_count)
{
    int* portal_list = *(int**)(DAT_80611aa8 + node_id * 0xB4 + 4);

    if (portal_list == NULL) {
        return 0;
    }

    int num_portals = *portal_list;
    if (max_count < num_portals) {
        num_portals = max_count;
    }

    int result_count = 0;

    for (int i = 0; i < num_portals; i++) {
        int* portal = *(int**)(DAT_80611aac + portal_list[i + 1] * 4);

        // Get the other side of the portal
        int neighbor;
        if (*portal == node_id) {
            neighbor = portal[1];
        } else {
            neighbor = *portal;
        }

        // Deduplicate
        bool duplicate = false;
        for (int j = 0; j < result_count; j++) {
            if (neighbor == *(int*)(out_buffer + j * 4)) {
                duplicate = true;
                break;
            }
        }

        if (!duplicate) {
            *(int*)(out_buffer + result_count * 4) = neighbor;
            result_count++;
        }
    }

    return result_count;
}

// =============================================================================
// Address: 0x80059988 | scene_node_get_render_data
// Returns render data pointer for a scene node from DAT_80611b04[node_id].
// Simple table lookup.
// =============================================================================
void* scene_node_get_render_data(int node_id)
{
    return *(void**)(DAT_80611b04 + node_id * 4);
}

// =============================================================================
// Batch 146 additions | Date: 2026-08-10
// =============================================================================

// =============================================================================
// Address: 0x80059998 | scene_node_get_submesh
// Resolves a packed submesh reference (high 16 = chunk index, low 16 = entry
// offset) into a pointer to the actual submesh/display-list data.
// Stride per chunk entry is 0x30, submesh entry stride is 0x18.
// =============================================================================
int scene_node_get_submesh(int node_id, unsigned int packed_ref)
{
    int render_data = scene_node_get_render_data(node_id);
    int* chunk_table = *(int**)(*(int*)(render_data + 4) + 4);

    // Navigate to the correct chunk entry, then offset within it
    int chunk_index = (int)packed_ref >> 16;
    int entry_offset = packed_ref & 0xFFFF;

    return chunk_table[*chunk_table * -0x30 + chunk_index * 0x30 + 0x1B] + entry_offset * 0x18;
}

// =============================================================================
// Address: 0x800599f4 | scene_node_get_base_data
// Gets the base (first entry) pointer for a node's render data.
// Computes: node_data_ptr + node_data_ptr[0] * -0xC0 (walks backward).
// =============================================================================
int scene_node_get_base_data(int node_id)
{
    int node_data_ptr = *(int*)(DAT_80611aa8 + node_id * 0xB4);
    return node_data_ptr + (*(int*)node_data_ptr) * -0xC0;
}

// =============================================================================
// Address: 0x80059a1c | scene_node_get_material_count
// Gets material/texture count for a scene node.
// mode==0: reads from base render data field at +0x44 (short).
// mode!=0: reads from render data table at +0x1E0.
// =============================================================================
int scene_node_get_material_count(int node_id, int mode)
{
    if (mode == 0) {
        int base = scene_node_get_base_data(node_id);
        return (int)*(short*)(base + 0x44);
    } else {
        return *(int*)(*(int*)(DAT_80611b04 + node_id * 4) + 0x1E0);
    }
}

// =============================================================================
// Address: 0x80059a5c | scene_node_get_texture_data
// Gets texture/material data pointer for a scene node.
// mode==0: reads from base render data at offset +0x70.
// mode!=0: reads from render data table entry at +0x1F0.
// =============================================================================
void* scene_node_get_texture_data(int node_id, int mode)
{
    if (mode == 0) {
        int base = scene_node_get_base_data(node_id);
        return *(void**)(base + 0x70);
    } else {
        return *(void**)(*(int*)(DAT_80611b04 + node_id * 4) + 0x1F0);
    }
}

// =============================================================================
// Address: 0x80059a9c | scene_node_get_portal_list_count
// Gets the portal list count (value at node offset +0xC) for a scene node.
// =============================================================================
int scene_node_get_portal_list_count(int node_id)
{
    return *(int*)(DAT_80611aa8 + node_id * 0xB4 + 0xC);
}
