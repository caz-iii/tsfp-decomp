// Address: 0x800531f8 - 0x80054ed8 | Batch 115 | Date: 2026-08-10
// Scene block loading, linking, and master geometry builder
// Part of the scene streaming/geometry system (continuation of scene_init.cpp / scene_transform.cpp)

#include "common.h"
#include "scene.h"
#include "memory.h"
#include "stream_queue.h"

// ============================================================================
// Address: 0x800531f8 | scene_node_flip_positions
// Swaps position vectors in scene node array entries.
// Type 3 nodes: swaps vec3 at +0x3C with vec3 at +0x54
// Type 4 nodes: swaps 3 vec3 pairs (+0x3C/+0x6C, +0x30/+0x48, +0x60/+0x78)
// Used for mirrored geometry variants.
// ============================================================================
void scene_node_flip_positions(int* node_array, int count) {
    int i;
    u32 tmp_x, tmp_y, tmp_z;

    for (i = 0; i < count; i++) {
        int* node = *(int**)(node_array + i);

        if (*node == 3) {
            // Swap position A (+0x3C) with position B (+0x54)
            tmp_x = *(u32*)(node + 0x3C);
            tmp_y = *(u32*)(node + 0x40);
            tmp_z = *(u32*)(node + 0x44);
            *(u32*)(node + 0x3C) = *(u32*)(node + 0x54);
            *(u32*)(node + 0x40) = *(u32*)(node + 0x58);
            *(u32*)(node + 0x44) = *(u32*)(node + 0x5C);
            *(u32*)(node + 0x54) = tmp_x;
            *(u32*)(node + 0x58) = tmp_y;
            *(u32*)(node + 0x5C) = tmp_z;
        }
        else if (*node == 4) {
            // Swap position pair 1: +0x3C <-> +0x6C
            tmp_x = *(u32*)(node + 0x3C);
            tmp_y = *(u32*)(node + 0x40);
            tmp_z = *(u32*)(node + 0x44);
            *(u32*)(node + 0x3C) = *(u32*)(node + 0x6C);
            *(u32*)(node + 0x40) = *(u32*)(node + 0x70);
            *(u32*)(node + 0x44) = *(u32*)(node + 0x74);
            *(u32*)(node + 0x6C) = tmp_x;
            *(u32*)(node + 0x70) = tmp_y;
            *(u32*)(node + 0x74) = tmp_z;

            // Swap position pair 2: +0x30 <-> +0x48
            tmp_x = *(u32*)(node + 0x30);
            tmp_y = *(u32*)(node + 0x34);
            tmp_z = *(u32*)(node + 0x38);
            *(u32*)(node + 0x30) = *(u32*)(node + 0x48);
            *(u32*)(node + 0x34) = *(u32*)(node + 0x4C);
            *(u32*)(node + 0x38) = *(u32*)(node + 0x50);
            *(u32*)(node + 0x48) = tmp_x;
            *(u32*)(node + 0x4C) = tmp_y;
            *(u32*)(node + 0x50) = tmp_z;

            // Swap position pair 3: +0x60 <-> +0x78
            tmp_x = *(u32*)(node + 0x60);
            tmp_y = *(u32*)(node + 0x64);
            tmp_z = *(u32*)(node + 0x68);
            *(u32*)(node + 0x60) = *(u32*)(node + 0x78);
            *(u32*)(node + 0x64) = *(u32*)(node + 0x7C);
            *(u32*)(node + 0x68) = *(u32*)(node + 0x80);
            *(u32*)(node + 0x78) = tmp_x;
            *(u32*)(node + 0x7C) = tmp_y;
            *(u32*)(node + 0x80) = tmp_z;
        }
    }
}

// ============================================================================
// Address: 0x8005347c | scene_data_byteswap
// Converts scene block data from big-endian (GameCube) to native format.
// Checks magic == 2 (already swapped); if not, byte-swaps:
//   - Header: 3 u32 fields (magic, node_count, material_count)
//   - Per-node entries (8 u32 + 2 u16 each, stride 0x20)
//   - Per-material entries (5 u32 each, stride 0x14)
//   - Optional trailing section (same format as nodes)
// ============================================================================
void scene_data_byteswap(u32* data, u32 total_size) {
    int i;
    u32* ptr;

    if (*data == 2) {
        return; // Already in native byte order
    }

    // Swap header
    *data = BSWAP32(*data);
    data[1] = BSWAP32(data[1]);
    data[2] = BSWAP32(data[2]);

    // Swap node entries
    ptr = data + 3;
    for (i = 0; i < (int)data[1]; i++) {
        ptr[0] = BSWAP32(ptr[0]);
        ptr[1] = BSWAP32(ptr[1]);
        *(u16*)(ptr + 2) = BSWAP16(*(u16*)(ptr + 2));
        *(u16*)((u8*)ptr + 10) = BSWAP16(*(u16*)((u8*)ptr + 10));
        ptr[3] = BSWAP32(ptr[3]);
        ptr[4] = BSWAP32(ptr[4]);
        ptr[5] = BSWAP32(ptr[5]);
        ptr[6] = BSWAP32(ptr[6]);
        ptr[7] = BSWAP32(ptr[7]);
        ptr += 8;
    }

    // Swap material entries
    for (i = 0; i < (int)data[2]; i++) {
        ptr[0] = BSWAP32(ptr[0]);
        ptr[1] = BSWAP32(ptr[1]);
        ptr[2] = BSWAP32(ptr[2]);
        ptr[3] = BSWAP32(ptr[3]);
        ptr[4] = BSWAP32(ptr[4]);
        ptr += 5;
    }

    // Optional trailing node section (if data remains)
    if ((u32)((u8*)ptr - (u8*)data) < total_size) {
        *ptr = BSWAP32(*ptr);
        u32* sub_ptr = ptr + 1;
        for (u32 j = 0; j < *ptr; j++) {
            sub_ptr[0] = BSWAP32(sub_ptr[0]);
            sub_ptr[1] = BSWAP32(sub_ptr[1]);
            *(u16*)(sub_ptr + 2) = BSWAP16(*(u16*)(sub_ptr + 2));
            *(u16*)((u8*)sub_ptr + 10) = BSWAP16(*(u16*)((u8*)sub_ptr + 10));
            sub_ptr[3] = BSWAP32(sub_ptr[3]);
            sub_ptr[4] = BSWAP32(sub_ptr[4]);
            sub_ptr[5] = BSWAP32(sub_ptr[5]);
            sub_ptr[6] = BSWAP32(sub_ptr[6]);
            sub_ptr[7] = BSWAP32(sub_ptr[7]);
            sub_ptr += 8;
        }
    }
}

// ============================================================================
// Address: 0x8005390c | scene_node_alloc_sub_bounds
// Allocates memory for a sub-node's bounding volume data and optionally
// invokes a transform callback to initialize it.
// Called during block linking for nodes that have bounding data (+0xC != 0).
// ============================================================================
void scene_node_alloc_sub_bounds(u32* slot_state, int node_index,
                                  void (*transform_fn)(void*, int),
                                  int transform_ctx) {
    int* base = *(int**)(/* scene state */);
    int slot_offset = node_index * 0xB4;

    int alloc = mem_alloc(0x10, 1);
    *(int*)(base[0] + slot_offset + 0xC) = alloc;

    if (*(int*)(transform_ctx + 0x178) != 0) {
        u32* sub_data = *(u32**)(base[0] + slot_offset + 0xC);
        int bounds_alloc = mem_alloc(0x20, 1);
        *sub_data = bounds_alloc;
        memcpy(*sub_data, base[0] + slot_offset + 0x10, 0x18);
        transform_fn(*sub_data, transform_ctx);
    }
}

// ============================================================================
// Address: 0x800539cc | scene_block_link_standard
// Links a streamed scene block into the active scene state (standard rotation).
// Resolves relative offsets to absolute pointers, transforms all bounding
// volumes using the standard (non-scaled) transform, processes orbit positions,
// counts sub-nodes/materials/portals/lights, and copies metadata to global arrays.
// Sets flag [0x19] = 0 (standard rotation modes 0-3).
// ============================================================================
void scene_block_link_standard(void) {
    // [MASSIVE function - see Ghidra decompilation]
    // Key operations:
    // 1. Finds model/pending entries via stream_find_model_by_name / stream_find_pending_by_name
    // 2. Calls scene_data_byteswap on loaded data
    // 3. Resolves base pointers from relative offsets (nodes, materials, orbits, vertex buffers)
    // 4. Builds rotation matrix from piVar7[4] (rotation index 0-3)
    // 5. For shared mode: directly patches pointers in-place
    //    For instanced mode: allocates copies of node/material arrays
    // 6. Transforms all 5 bounding boxes per sub-node via scene_transform_bounds
    // 7. Updates orbit positions
    // 8. Counts sub-nodes, vertex buffers, portals, lights
    // 9. Stores metadata: flags, position (xyz), rotation type, scale params
    // 10. Frees source data after linking
}

// ============================================================================
// Address: 0x800542d4 | scene_block_link_mirrored
// Links a streamed scene block with mirrored/scaled geometry (rotation modes 4-7).
// Nearly identical to scene_block_link_standard but:
//   - Uses rotation matrix offset (piVar7[4]+4)*0x40 (mirrored rotation set)
//   - Calls scene_transform_bounds_scaled instead of scene_transform_bounds
//   - Calls scene_orbit_pair_update instead of scene_orbit_position_update
//   - Calls scene_batch_transform_swap instead of scene_batch_transform_verts
//   - Sets flag [0x19] = 1 (mirrored mode)
// ============================================================================
void scene_block_link_mirrored(void) {
    // [MASSIVE function - mirrors scene_block_link_standard]
    // Same structure as standard but with scaled/mirrored transforms.
}

// ============================================================================
// Address: 0x80054bb8 | scene_block_finalize_indices
// Post-link fixup: adjusts material and vertex buffer indices with a base
// offset from the slot state, then stores per-slot metadata into global arrays:
//   - DAT_80527c08[]: slot flags
//   - DAT_80527fdc[]: rotation type
//   - DAT_805282fc[]: position (xyz, stride 0xC)
//   - DAT_80528c5c[]: scale params
//   - DAT_80528f7c[]: mirror flag
// Also patches material start/end indices in vertex buffer descriptors and
// orbit entry base indices.
// ============================================================================
void scene_block_finalize_indices(void) {
    int* slot_state = (int*)FUN_803826f0(); // Get current slot state from stack

    // Patch vertex buffer material references
    for (int i = 0; i < slot_state[6]; i++) {
        int* vbuf_desc = *(int**)(slot_state[1] + i * 4 + 4);
        if (*vbuf_desc >= 0) {
            *vbuf_desc += slot_state[10]; // Add base material index
        }
        if (vbuf_desc[1] >= 0) {
            vbuf_desc[1] += slot_state[10]; // Add base material index
        }
    }

    // Patch orbit entry base indices
    for (int i = 0; i < slot_state[9]; i++) {
        int entry = slot_state[4] + i * 0x2C;
        *(int*)(entry + 4) += slot_state[10];
    }

    // Store per-slot global metadata
    for (int i = 0; i < slot_state[5]; i++) {
        // FUN_8007089c: register sub-node with scene manager
        FUN_8007089c(slot_state[10] + i + 0x8FF,
                     *(u32*)(slot_state[0] + (i + 1) * 0xB4));
        // Global arrays indexed by (base_offset + i)
        int idx = slot_state[10] + i;
        g_scene_slot_flags[idx] = slot_state[0x11];
        g_scene_rotation_type[idx] = slot_state[0x14];
        g_scene_position[idx * 3 + 0] = slot_state[0x15]; // pos.x
        g_scene_position[idx * 3 + 1] = slot_state[0x16]; // pos.y
        g_scene_position[idx * 3 + 2] = slot_state[0x17]; // pos.z
        g_scene_scale_params[idx] = slot_state[0x18];
        g_scene_mirror_flag[idx] = slot_state[0x19];
    }
}

// ============================================================================
// Address: 0x80054d70 | scene_block_offset_material_refs
// Iterates sub-node material reference arrays and adds a base offset.
// Each sub-node at (base + (i+1)*0xB4 + 4) has a counted array of
// material indices that need rebasing after block linking.
// ============================================================================
void scene_block_offset_material_refs(int* slot_state) {
    for (int i = 0; i < slot_state[5]; i++) {
        int* mat_array = *(int**)(slot_state[0] + (i + 1) * 0xB4 + 4);
        int count = *mat_array;
        for (int j = 0; j < count; j++) {
            mat_array[j + 1] += slot_state[0xB]; // material base offset
        }
    }
}

// ============================================================================
// Address: 0x80054dd0 | scene_block_resolve_materials
// Resolves material/texture references for all sub-nodes in the slot.
// Calls FUN_8006151c (material resolver) with the consolidated portal/light
// buffers, then patches per-material vertex buffer pointers with base offset.
// ============================================================================
void scene_block_resolve_materials(void) {
    int* slot_state = (int*)FUN_803826f0();

    for (int i = 0; i < slot_state[5]; i++) {
        int* sub_node = (int*)(slot_state[0] + (i + 1) * 0xB4);
        int mesh_data = *sub_node;
        int header_offset = mesh_data + *(int*)mesh_data * -0xC0;

        // Resolve materials with portal/light context
        FUN_8006151c(mesh_data, DAT_80611aa0, slot_state[0xC],
                     DAT_80611aa4, slot_state[0xD]);

        // Patch vertex buffer pointers in material sub-entries
        if (*(int*)(header_offset + 0x78) != 0) {
            int vbuf_table = *(int*)(header_offset + 0x78);
            for (int j = 0; j < *(s16*)(header_offset + 0x48); j++) {
                int entry = *(int*)(vbuf_table + j * 4);
                *(int*)(entry + 4) += slot_state[0xC]; // base portal offset
            }
        }
    }
}

// ============================================================================
// Address: 0x80054e94 | scene_block_is_unique_material_set
// Checks if block entry at index param_2 has a unique material set ID
// (field at +0x50 in stride-0x68 entries). Returns 1 if no earlier entry
// shares the same ID, 0 if a duplicate is found.
// Used to avoid redundant portal/light data duplication.
// ============================================================================
int scene_block_is_unique_material_set(int block_array, int check_index) {
    for (int i = 0; i < check_index; i++) {
        if (*(int*)(block_array + i * 0x68 + 0x50) ==
            *(int*)(block_array + check_index * 0x68 + 0x50)) {
            return 0; // Duplicate found
        }
    }
    return 1; // Unique
}

// ============================================================================
// Address: 0x80054ed8 | scene_system_build_all_blocks
// MASTER scene geometry builder. Parses the scene chunk stream and constructs
// the complete scene from all block entries.
//
// Chunk format (magic IDs):
//   0xC77668 - Block header: contains block count, allocates tracking buffer
//   0xC77669 - Block data: per-block geometry entries with position/rotation
//   0xC7766A - End marker
//
// Per-block entry (variable size, parsed inline):
//   - Character/model ID, position offsets (x/y/z), rotation mode (0-7)
//   - Material references, animation keyframe data
//   - Vertex group entries, optional secondary vertex groups
//
// Pipeline:
//   1. Parse all block entries, determine totals (nodes, materials, portals, lights)
//   2. For rotation modes 0-3: call scene_block_link_standard
//      For rotation modes 4-7: call scene_block_link_mirrored
//   3. Call scene_block_finalize_indices for each block
//   4. Call scene_block_offset_material_refs for each block
//   5. If unique material set: call scene_block_resolve_materials
//   6. Consolidate all sub-arrays into master buffers:
//      - DAT_80611aa8: all scene sub-nodes (stride 0xB4)
//      - DAT_80611aac: all vertex buffer descriptors (stride 4 ptr array)
//      - DAT_80611aa0: all portals (stride 0x10)
//      - DAT_80611aa4: all lights (stride 0x30)
//   7. Portal plane merging pass:
//      - Compares portal normals via dot product
//      - Compares portal vertex AABB extents and centroids
//      - Merges duplicates by patching vertex buffer references
//   8. Portal plane validation pass (removes orphaned portals from sub-nodes)
//   9. Coplanar portal grouping pass:
//      - Groups portals with matching normals (up to 24 per group)
//      - Computes combined AABB and recalculates normal from vertices
//      - Validates normal orientation and flips if needed
//      - Removes grouped portals from individual sub-node references
//  10. Allocates final scene state buffers:
//      - DAT_80611b1c: scene render state (0xE0 bytes)
//      - DAT_80611b18: per-node render flags
//      - DAT_80611a98: per-material render state (stride 0x28)
//      - DAT_80611b04: per-node visibility flags
//      - DAT_80611e78: per-node lighting params (stride 0x1C)
//  11. Initializes per-node lighting from block entry data:
//      - Type 0: default (ambient only)
//      - Type 1: directional (single light)
//      - Type 2: point light (distance + falloff)
//      - Type 3: spot light (angle + falloff)
//  12. Frees temporary block tracking buffer
// ============================================================================
void scene_system_build_all_blocks(void) {
    // [MASSIVE function ~1700 lines of decompiled output]
    // See Ghidra for full implementation.
    // Key globals written:
    //   DAT_80611aa8 - master sub-node array
    //   DAT_80611aac - master vertex buffer descriptor array
    //   DAT_80611aa0 - master portal array
    //   DAT_80611aa4 - master light array
    //   DAT_80611b14 - total sub-node count
    //   DAT_80611b10 - total vertex buffer count
    //   DAT_80611b08 - total orbit entry count
    //   DAT_80611b0c - reserved
    //   DAT_80611b1c - scene render state
    //   DAT_80611b18 - per-node render flags
    //   DAT_80611a98 - per-material render state
    //   DAT_80611b04 - per-node visibility
    //   DAT_80611e78 - per-node lighting params
}
