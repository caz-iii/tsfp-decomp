// Address: 0x80056574 - 0x800579d0 | Batch 116 | Date: 2026-08-11
// Scene initialization, bounds computation, entity creation, spatial tree
// Part of the scene system post-build phase (follows scene_block_link.cpp)

#include "common.h"
#include "scene.h"
#include "memory.h"
#include "stream_queue.h"

// ============================================================================
// Address: 0x80056574 | scene_node_portals_intersect
// Tests if all portal planes in a list intersect with a scene node's AABB.
// Iterates vertex buffers from scene node (via FUN_80059a9c), then for each
// portal entry (stride 0x18, positions at field[9..14]), checks if the portal
// position falls within the vertex buffer's AABB (+0x00 min, +0x0C max) with
// a tolerance of FLOAT_80613df8.
// Returns 1 if ALL portals intersect at least one vertex buffer (early-out
// via bitmask). Returns 0 if any portal has no intersection.
// ============================================================================
int scene_node_portals_intersect(int node_index, int* portal_list) {
    int vbuf_table = FUN_80059a9c(node_index);
    int i = 0;
    u32 hit_mask = 0;

    if (*portal_list == 0) {
        return 0;
    }

    while (*(int*)(vbuf_table + i * 4) != 0) {
        float* vbuf = *(float**)(vbuf_table + i * 4);
        int portal_count = *portal_list;

        for (int p = portal_count - 1; p >= 0; p--) {
            float* portal_pos = (float*)(portal_list + p * 6 + 9);

            // AABB containment test with tolerance
            if (vbuf[0] - TOLERANCE <= portal_pos[0] &&
                portal_pos[0] < TOLERANCE + vbuf[3] &&
                vbuf[1] - TOLERANCE <= portal_pos[1] &&
                portal_pos[1] < TOLERANCE + vbuf[4] &&
                vbuf[2] - TOLERANCE <= portal_pos[2] &&
                portal_pos[2] < TOLERANCE + vbuf[5]) {
                hit_mask |= (1 << p);
            }

            // Early out: all portals hit
            if (hit_mask == (1u << *portal_list) - 1) {
                return 1;
            }
        }
        i++;
    }
    return 0;
}

// ============================================================================
// Address: 0x800566c4 | scene_node_compute_bounds_and_weights
// Per-node initialization after block linking:
// 1. Scans mesh LODs for transparency (bit 0x80) and alpha-test (bit 0x40)
//    flags, sets node flags at +0xB2 (bit 0=transparent, bit 1=alpha-test)
// 2. Computes AABB from bounds fields [0x1C..0x21]:
//    - Dimensions (extents) at [0x22..0x24]
//    - Volume at [0x29]
//    - Center at [0x25..0x27]
//    - Bounding sphere radius at [0x28] (via sqrt)
// 3. Updates world-space scene AABB extents (DAT_8052929c..DAT_805292b0)
// 4. Counts portals visible from this node at [0x2C]
// 5. Computes portal area weights:
//    - Total area at [0x2A]
//    - Intersecting portal area at [0x2B]
//    - Per-portal normalized weight at entry+0x10
// ============================================================================
void scene_node_compute_bounds_and_weights(void) {
    int node_index = FUN_803826ec(); // pop from param stack
    u32* node = (u32*)(DAT_80611aa8 + node_index * 0xB4);
    int vbuf_table = FUN_80059a5c(node_index, 0);
    int* mesh_data = (int*)*node;
    int lod_count = *mesh_data;

    // Scan for transparency/alpha-test flags in materials
    for (int lod = 0; lod < lod_count; lod++) {
        int mat_table = mesh_data[lod_count * -0x30 + lod * 0x30 + 0x1C];
        int tex_table = mesh_data[lod_count * -0x30 + lod * 0x30 + 0x1D];

        for (int m = 0; m < *(s16*)(mesh_data + lod_count * -0x30 + lod * 0x30 + 0x11); m++) {
            if ((*(u16*)(*(int*)(mat_table + m * 4) + 8) & 0x80) != 0) {
                *(u16*)((u8*)node + 0xB2) |= 1; // Has transparent materials
            }
        }
        for (int t = 0; t < *(s16*)((u8*)mesh_data + lod * 0xC0 + lod_count * -0xC0 + 0x46); t++) {
            if ((*(u32*)(*(int*)(tex_table + t * 4) + 4) & 0x40) != 0) {
                *(u16*)((u8*)node + 0xB2) |= 2; // Has alpha-tested materials
            }
        }
    }

    // Compute AABB if valid bounds
    if ((float)node[0x1C] < (float)node[0x1F]) {
        // Dimensions = max - min
        node[0x22] = (float)node[0x1F] - (float)node[0x1C]; // extent.x
        node[0x23] = (float)node[0x20] - (float)node[0x1D]; // extent.y
        node[0x24] = (float)node[0x21] - (float)node[0x1E]; // extent.z
        // Volume
        node[0x29] = (float)node[0x24] * (float)node[0x22] * (float)node[0x23];
        // Center = min + extent * 0.5
        node[0x25] = 0.5f * (float)node[0x22] + (float)node[0x1C];
        node[0x26] = 0.5f * (float)node[0x23] + (float)node[0x1D];
        node[0x27] = 0.5f * (float)node[0x24] + (float)node[0x1E];
        // Bounding sphere radius = length(half-extents)
        float hx = 0.5f * (float)node[0x22];
        float hy = 0.5f * (float)node[0x23];
        float hz = 0.5f * (float)node[0x24];
        node[0x28] = (float)sqrtf(hx*hx + hy*hy + hz*hz);
    }

    // Update world AABB extents
    // ... (min/max tracking against DAT_8052929c..DAT_805292b0)

    // Count portals and compute area weights
    *(u16*)(node + 0x2C) = 0;
    int portal_count = FUN_80059a1c(node_index, 0);
    for (int i = 0; i < portal_count; i++) {
        *(s16*)(node + 0x2C) += 1;
    }

    // Compute total and intersecting portal areas
    node[0x2A] = FLOAT_80613dfc; // initial total area
    node[0x2B] = FLOAT_80613dfc; // initial intersecting area
    // ... (area summation with scene_node_portals_intersect test)
}

// ============================================================================
// Address: 0x80056b64 | scene_compute_all_bounds
// Iterates all scene sub-nodes (1..DAT_80611b14) calling
// scene_node_compute_bounds_and_weights for each.
// Then iterates all vertex buffer descriptors (1..DAT_80611b10) and computes
// per-buffer AABB from vertex positions (stride 0xC at offset +0x18).
// Stores min at [+8,+0xC,+0x10] and max at [+0x14,+0x18,+0x1C] in the
// material render state (DAT_80611a98 + index * 0x28).
// ============================================================================
void scene_compute_all_bounds(void) {
    // Phase 1: Per-node bounds and weights
    for (int i = 1; i < DAT_80611b14 + 1; i++) {
        scene_node_compute_bounds_and_weights(/* push i */);
    }

    // Phase 2: Per-vertex-buffer AABB
    for (int i = 1; i <= DAT_80611b10; i++) {
        int vbuf = *(int*)(DAT_80611aac + i * 4);
        int mat_state = DAT_80611a98 + i * 0x28;

        // Initialize to extremes
        *(float*)(mat_state + 0x10) = FLT_MAX_NEG; // min.z
        *(float*)(mat_state + 0x0C) = FLT_MAX_NEG; // min.y
        *(float*)(mat_state + 0x08) = FLT_MAX_NEG; // min.x
        *(float*)(mat_state + 0x1C) = FLT_MIN_POS; // max.z
        *(float*)(mat_state + 0x18) = FLT_MIN_POS; // max.y
        *(float*)(mat_state + 0x14) = FLT_MIN_POS; // max.x

        int vert_count = *(s16*)(vbuf + 0x14);
        for (int v = 0; v < vert_count; v++) {
            float vx = *(float*)(vbuf + v * 0xC + 0x18);
            float vy = *(float*)(vbuf + v * 0xC + 0x1C);
            float vz = *(float*)(vbuf + v * 0xC + 0x20);
            // Update min/max...
        }
        *(u16*)(mat_state + 0x20) = 0; // Clear flags
    }
}

// ============================================================================
// Address: 0x80056d0c | scene_load_tileset_data
// Loads tileset geometry and collision/pad data for the current level.
// For level ID 100 (mapmaker): parses scene chunk stream to count per-tile
// usage, then queues tile models ("tileset_%d_tile_%d_%d") and collision
// data ("pad_data_tile_%d_%d.war") for streaming.
// For regular levels: queues the level model directly from
// "assets/ts/models/level%d/level%d".
// Up to 0x2D (45) unique tilesets supported.
// DAT_80527f24: per-rotation-type reference counters
// DAT_805292b0: per-tile collision data counters
// ============================================================================
void scene_load_tileset_data(void) {
    if (DAT_80612614 == 100) {
        // Mapmaker mode: parse chunk stream for tile usage
        int* chunk_ptr = (int*)(DAT_80612be8 + 8);
        int block_count = 0;
        u32 world_id = 0;
        int arena = heap_arena_lookup();

        if (arena == 0) {
            // Zero out tileset tracking arrays
            memset(g_tileset_node_array, 0, 0xB4);
            memset(g_tileset_vbuf_array, 0, 0xB4);
            memset(g_tileset_mat_array, 0, 0xB4);

            // Parse chunks to count tile usage per rotation type
            // ... (iterates magic 0xC77668/69/6A)

            // Queue tileset models and collision data
            for (int tile = 0; tile < 0x2D; tile++) {
                if (g_tile_model_counts[tile] != 0) {
                    // Format: "tileset_%d_tile_%d_%d"
                    char* path = format_path("tileset_%d_tile_%d_%d", world_id + 1, world_id + 1, tile + 1);
                    stream_queue_model(path);
                }
                if (g_tile_collision_counts[tile] != 0) {
                    // Format: "pad_data_tile_%d_%d.war"
                    char* path = format_path("pad_data_tile_%d_%d.war", world_id + 1, tile + 1);
                    int size = asset_get_size(path);
                    if (size < 1) {
                        // Fallback to world 1
                        path = format_path("pad_data_tile_%d_%d.war", 1, tile + 1);
                    }
                    stream_queue_raw_asset(path);
                }
            }
        }
    } else {
        // Regular level: queue level model directly
        // Format: "assets/ts/models/level%d/level%d"
        char path[64];
        format_path(path, "assets/ts/models/level%d/level%d", DAT_80612614, DAT_80612614);
        stream_queue_raw_asset(path);
    }
}

// ============================================================================
// Address: 0x80057000 | scene_load_level_model
// Loads the level's scene model data (non-mapmaker levels only).
// Skips levels 100-106 (mapmaker, frontend, etc.).
// Pipeline:
// 1. Builds path "assets/ts/models/level%d/level%d"
// 2. Resolves portal table (offset[0] -> DAT_80611aa0) and light table
//    (offset[10] -> DAT_80611aa4) from model header
// 3. Calls FUN_8005ff6c (material setup with portal/light context)
// 4. Resolves material texture IDs via FUN_80060b5c
// 5. Queues textures for streaming:
//    - Normal textures: stream_queue_texture
//    - Special textures (flag 0x10000000): FUN_8017bffc (animated/multi-frame)
// 6. Calls FUN_8017c72c with model params (likely lightmap/shadow setup)
// Sets DAT_80611afc=1 if model has field[7] (vertex colors present).
// ============================================================================
void scene_load_level_model(void) {
    // Skip mapmaker/frontend levels (100-107)
    if (DAT_80612614 == 100 || DAT_80612614 == 0x65 ||
        DAT_80612614 == 0x66 || DAT_80612614 == 0x67 ||
        DAT_80612614 == 0x68 || DAT_80612614 == 0x69 ||
        DAT_80612614 == 0x6B || DAT_80612614 == 0x6A) {
        return;
    }

    char path[64];
    format_path(path, "assets/ts/models/level%d/level%d",
                DAT_80612614, DAT_80612614);

    int pending = stream_find_pending_by_name(path);
    int* model_header = *(int**)(pending + 0x10);

    // Check for vertex colors
    if (model_header[7] != 0) {
        DAT_80611afc = 1;
    }

    // Resolve portal table
    if (model_header[0] == 0) {
        DAT_80611aa0 = 0;
    } else {
        DAT_80611aa0 = model_header[0] + (int)model_header;
    }

    // Resolve light table
    if (model_header[10] == 0) {
        DAT_80611aa4 = 0;
    } else {
        DAT_80611aa4 = model_header[10] + (int)model_header;
    }

    // Material setup
    FUN_8005ff6c(DAT_80611aa4, model_header, DAT_80611aa0);

    // Queue textures for streaming
    if (DAT_80611aa0 != 0) {
        FUN_80060b5c(DAT_80611aa0, (int)model_header);
        for (int i = 0; *(int*)(DAT_80611aa0 + i * 0x10) != -1; i++) {
            if (*(int*)(DAT_80611aa0 + i * 0x10) != 0x40000000 &&
                (*(u32*)(DAT_80611aa0 + i * 0x10 + 0xC) & 0x200000) != 0) {
                if ((*(u32*)(DAT_80611aa0 + i * 0x10 + 0xC) & 0x10000000) == 0) {
                    stream_queue_texture(/* normal texture */);
                } else {
                    FUN_8017bffc(/* animated/multi-frame texture */);
                }
            }
        }
    }

    // Lightmap/shadow setup
    FUN_8017c72c(model_header[5], model_header[6]);
}

// ============================================================================
// Address: 0x80057204 | scene_create_render_entities
// Creates scene render entities (via FUN_80072d70) for all sub-nodes.
// Two modes based on param (from stack):
//   Mode 0 (fresh load):
//     - Computes AABB center and half-extents from node bounds [+0x70..+0x84]
//     - Sets render entity fields: slot index, flags, center, half-extents
//     - Marks collision ref as -1
//   Mode 1 (hot-reload):
//     - Uses pre-stored globals (position from DAT_805282fc, scale from DAT_80528c5c)
//     - Restores slot flags from DAT_80527c08, rotation from DAT_80527fdc
//     - Handles mirrored blocks: flips axes via scene_batch_swap_axes and
//       scene_node_flip_positions, negates appropriate center axis based on
//       scale parameter
// ============================================================================
void scene_create_render_entities(void) {
    int reload_mode = FUN_803826e8(); // 0=fresh, 1=reload
    *DAT_80611b04 = 0;

    if (reload_mode != 0) {
        // Clear entity tracking array (0xD6 entries)
        for (int i = 0; i < 0xD6; i++) {
            g_scene_entity_table[i] = 0;
        }
    }

    for (int i = 0; i < DAT_80611b14; i++) {
        // ... (see full decompilation for entity creation logic)
        // Creates entity with FUN_80072d70(id=i+0x8FF, pos, scale, 0, 0)
        // Sets DAT_80611b04[i+1] = entity handle
        // Configures AABB center at +0x1A8/1AC/1B0
        // Configures half-extents at +0x1C0/1C4/1C8
    }
}

// ============================================================================
// Address: 0x80057780 | scene_level_load_finalize
// Main orchestrator for scene level loading completion.
// 1. Unloads all previous scene rooms and resets slot state
// 2. Calls FUN_801c2ec0 (likely camera/viewport reset)
// 3. For non-mapmaker levels (< 0x65):
//    - Creates render entities via scene_create_render_entities
//    - Zeroes scene render state (DAT_80611b1c, 0xD6 bytes)
//    - Sets final byte [+0xD5] = 1
//    - Clears viewport visibility arrays (5 + 6 entries)
// 4. Calls FUN_8005c5b0 (visibility system init)
// 5. Resets vertex buffer visibility flags (field +0x16 = 0 for all buffers)
// 6. Calls scene_reset_visibility_state
// ============================================================================
void scene_level_load_finalize(void) {
    scene_room_unload_all();
    scene_slot_reset_all();
    FUN_801c2ec0(); // camera/viewport reset

    if (DAT_80612614 < 0x65) {
        // Regular gameplay level
        scene_create_render_entities(/* mode depends on context */);
        memset(DAT_80611b1c, 0, 0xD6);
        *(u8*)(DAT_80611b1c + 0xD5) = 1;

        // Clear viewport visibility
        for (int i = 0; i < 5; i++) {
            *(u8*)(DAT_80611b1c + 0xCA + i) = 0;
        }
        for (int i = 0; i < 6; i++) {
            *(u8*)(DAT_80611b1c + 0xCF + i) = 0;
        }
    }

    FUN_8005c5b0(); // visibility system init
    DAT_80611b00 = 0;

    // Reset vertex buffer visibility flags
    for (int i = 0; i < DAT_80611b10; i++) {
        *(u16*)(*(int*)(DAT_80611aac + i * 4 + 4) + 0x16) = 0;
    }

    scene_reset_visibility_state();
}

// ============================================================================
// Address: 0x80057870 | scene_spatial_alloc_callback
// Thin memory allocation wrapper used as callback by the spatial tree builder.
// Simply forwards to mem_alloc with alignment=1.
// ============================================================================
void scene_spatial_alloc_callback(int size) {
    mem_alloc(size, 1);
}

// ============================================================================
// Address: 0x80057894 | scene_build_spatial_tree
// Builds the spatial partitioning tree (BSP/KD-tree) for scene visibility.
// 1. Allocates temp buffer via mem_page_alloc
// 2. For each node (0..DAT_80611b38):
//    - Stores node index at entry+0 (stride 0x1C)
//    - Computes AABB: min = position - radius, max = position + radius
//      (position from DAT_8052f940[i*9+0..2], radius from entity+0xA8)
// 3. Calls FUN_80134914 (tree builder) with:
//    - Root = DAT_80610408
//    - Max depth = [6, 6] (X and Z axis splits)
//    - Node array + count
//    - Alloc callback = scene_spatial_alloc_callback
// 4. Frees temp buffer
// 5. Records final node count: DAT_80611b3c = DAT_80611b38
// ============================================================================
void scene_build_spatial_tree(void) {
    int temp_buf = mem_page_alloc();

    for (int i = 0; i < DAT_80611b38; i++) {
        *(s16*)(temp_buf + i * 0x1C) = (s16)i;
        for (int axis = 0; axis < 3; axis++) {
            float pos = g_node_positions[i * 9 + axis];
            float radius = *(float*)(g_node_entities[i * 9] + 0xA8);
            *(float*)(temp_buf + i * 0x1C + axis * 4 + 4) = pos - radius;  // min
            *(float*)(temp_buf + i * 0x1C + axis * 4 + 0x10) = pos + radius; // max
        }
    }

    int depth_params[2] = {6, 6};
    FUN_80134914(DAT_80610408, &depth_params, temp_buf, DAT_80611b38,
                 scene_spatial_alloc_callback);
    mem_page_free(temp_buf);
    DAT_80611b3c = DAT_80611b38;
}

// ============================================================================
// Address: 0x800579d0 | scene_reset_visibility_state
// Saves the spatial node count, calls FUN_8005c7ec (visibility/culling reset),
// then restores the count. Ensures the visibility system is re-initialized
// without losing the spatial tree node count.
// ============================================================================
void scene_reset_visibility_state(void) {
    u32 saved_count = DAT_80611b3c;
    DAT_80611b3c = 0;
    FUN_8005c7ec(); // visibility/culling reset
    DAT_80611b3c = saved_count;
}
