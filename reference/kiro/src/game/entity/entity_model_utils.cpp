// Address: 0x8007b6f8 - 0x8007c29c | Name: entity_model_utils | Date: 2026-08-10
// Entity sub-object raycasting, vertex color modification, model cache, world position
// Part of the entity model/collision utility subsystem

#include "common.h"

// ============================================================================
// 0x8007b6f8 | entity_raycast_sub_objects
// Single-hit raycast against entity collision sub-objects.
// Iterates all sub-objects in the entity's model data (stride 0xC0 each).
// For each sub-object with collision data (+0x54/58/5C non-zero):
//   - Checks visibility mask against entity class flags (+0x30)
//   - Gets bone index (+0x3D), applies bone matrix if skinned (flag 0x200000)
//   - Transforms sub-object local offset (+0x84/88/8C) to world via entity_build_local_matrix
//   - Calls FUN_80015c74 (ray-vs-OBB single intersection test)
//   - If hit: allocates hit result (FUN_80146610), fills via FUN_80146660, appends to list
// Called by FUN_80147590 (entity collision dispatcher).
// ============================================================================
void entity_raycast_sub_objects(
    /* param_1, param_2: unused */
    unsigned short hit_type,   // param_3: hit type classification
    int hit_flags,             // param_4: collision layer flags
    int* out_hit_list          // param_5: output hit result list
) {
    int entity_pair = (int)FUN_803826c4(); // prologue
    int entity = (int)((uint64_t)entity_pair >> 32);
    int model_data = *(int*)(entity + 4);
    int* sub_obj_header = *(int**)(model_data + 4);
    int sub_obj_count = *sub_obj_header;

    float local_matrix[16];
    entity_build_local_matrix(local_matrix, entity, 0, 0);

    // Check if entity has bones and is skinned
    bool has_bones = (sub_obj_header[1] > 0) && (*(unsigned int*)(entity + 0x28) & 8);
    bool is_skinned = has_bones && (*(unsigned int*)(entity + 0x28) & 0x200000);

    float best_t = 0.0f; // FLOAT_80613fb4

    for (int i = 0; i < sub_obj_count; i++) {
        int offset = i * 0xC0 + sub_obj_count * -0xC0;
        // Check sub-object has collision data
        if (*(int*)((int)sub_obj_header + offset + 0x54) == 0 &&
            *(int*)((int)sub_obj_header + offset + 0x58) == 0 &&
            *(int*)((int)sub_obj_header + offset + 0x5C) == 0) continue;

        // Check visibility mask
        unsigned int vis_mask = *(unsigned int*)((int)sub_obj_header + offset + 0x50);
        if (vis_mask != 0 && (*(unsigned int*)(model_data + 0x30) & vis_mask) == 0) continue;

        // Get bone-transformed matrix
        float work_matrix[16];
        int bone_idx = (int)*(char*)((int)sub_obj_header + offset + 0x3D);
        if (bone_idx >= 0 && is_skinned) {
            float* bone_mtx = (float*)(*(int*)(model_data + 0x10) + bone_idx * 0x40);
            FUN_80130bd4(bone_mtx, local_matrix, work_matrix);
        } else {
            FUN_80130b90(local_matrix, work_matrix);
        }

        // Transform sub-object center to world space
        float center[3];
        center[0] = *(float*)((int)sub_obj_header + offset + 0x84);
        center[1] = *(float*)((int)sub_obj_header + offset + 0x88);
        center[2] = *(float*)((int)sub_obj_header + offset + 0x8C);
        FUN_8012fa34(work_matrix, center);
        center[0] += *(float*)(entity + 0x64);
        center[1] += *(float*)(entity + 0x68);
        center[2] += *(float*)(entity + 0x6C);

        // Ray-vs-OBB test
        float hit_pos[3], hit_normal[3];
        int hit = FUN_80015c74(entity_pair, center,
            (float*)((int)sub_obj_header + offset + 0x90),
            work_matrix, hit_pos, hit_normal, &best_t);

        if (hit) {
            void* result = FUN_80146610(); // allocate hit result
            if (result != NULL) {
                FUN_80146660(0.0f, best_t, result, hit_pos, hit_normal,
                            hit_type, entity, hit_flags, -1);
                FUN_80146650(out_hit_list, (int)result);
            }
        }
    }

    FUN_80382710(); // epilogue
}

// ============================================================================
// 0x8007b91c | entity_raycast_sub_objects_multi
// Multi-hit raycast against entity collision sub-objects.
// Same structure as entity_raycast_sub_objects but uses FUN_800164f4
// (multi-hit ray intersection) with max 8 results per sub-object, 3 passes.
// Each hit allocates a result and appends to output list.
// Called by FUN_80147590 (entity collision dispatcher).
// ============================================================================
void entity_raycast_sub_objects_multi(
    /* param_1, param_2: unused */
    float* ray_origin,         // param_3: ray start position
    float* ray_dir,            // param_4: ray direction
    unsigned short hit_type,   // param_5: hit classification
    int hit_flags,             // param_6: collision layer flags
    int* out_hit_list          // param_7: output hit result list
) {
    // Same prologue: get entity, model data, sub-object array
    // Same matrix setup: entity_build_local_matrix + bone transform
    // For each sub-object with collision data:
    //   - Transform center to world
    //   - FUN_800164f4: multi-hit ray test (max 8 hits, 3 passes)
    //   - For each hit: allocate result, fill, append to list
    // Epilogue: FUN_80382700
}

// ============================================================================
// 0x8007bb74 | entity_swept_cast_sub_objects
// Swept sphere/capsule test against entity collision sub-objects.
// Same structure as raycast variants but uses FUN_8001b304
// (swept sphere intersection) with max 4 results per sub-object, 3 passes.
// Used for thick collision queries (character capsule vs environment).
// Called by FUN_80147590 (entity collision dispatcher).
// ============================================================================
void entity_swept_cast_sub_objects(
    /* param_1, param_2: unused */
    float* sweep_origin,       // param_3: sweep start position
    int sweep_radius,          // param_4: sweep sphere radius (as int)
    unsigned short hit_type,   // param_5: hit classification
    int hit_flags,             // param_6: collision layer flags
    int* out_hit_list          // param_7: output hit result list
) {
    // Same prologue and sub-object iteration as raycast variants
    // Uses FUN_8001b304 for swept sphere test (max 4 results, 3 passes)
    // Allocates and appends hit results
    // Epilogue: FUN_80382700
}

// ============================================================================
// 0x8007bdcc | entity_get_sub_object_vertex_world_pos
// Transforms a vertex from a sub-object's local space to world space.
// Given sub-object index (param_3) and vertex index (param_4):
//   - Looks up vertex position from sub-object's vertex list (+0x6C offset, stride 0x18)
//   - Builds entity local matrix (with mirror flag=1)
//   - If entity is skinned (flags 0x8 + 0x200000): multiplies by bone matrix
//   - Applies matrix transform to vertex (FUN_8012fa34)
// Outputs world-space position via return float* (r3 register pair).
// Called by 3 functions for attachment point queries.
// ============================================================================
void entity_get_sub_object_vertex_world_pos(
    /* param_1, param_2: unused */
    int sub_object_index,   // param_3
    int vertex_index        // param_4
) {
    float* out_pos = (float*)((uint64_t)FUN_803826f0() >> 32);
    int entity = (int)FUN_803826f0();

    // Get sub-object array
    int model_data = *(int*)(entity + 4);
    int* sub_obj_header = *(int**)(model_data + 4);
    int count = *sub_obj_header;

    // Bounds check
    int* sub_obj = NULL;
    if (sub_object_index >= 0 && sub_object_index < count) {
        sub_obj = sub_obj_header + count * -0x30 + sub_object_index * 0x30;
    }

    // Get vertex position from sub-object vertex list
    float* vertex_list = (float*)(sub_obj[0x1B] + vertex_index * 0x18);
    float pos[3] = { vertex_list[0], vertex_list[1], vertex_list[2] };

    // Build local matrix (mirror=1)
    float local_matrix[16];
    entity_build_local_matrix(local_matrix, entity, 1, 0);

    // Apply bone matrix if skinned
    if (*(int*)(*(int*)(*(int*)(entity + 4) + 4) + 4) > 0) {
        if ((*(unsigned int*)(entity + 0x28) & 8) && (*(unsigned int*)(entity + 0x28) & 0x200000)) {
            int bone_idx = *(char*)((int)sub_obj + 0x3D);
            float* bone_mtx = (float*)(*(int*)(model_data + 0x10) + bone_idx * 0x40);
            FUN_80130bd4(bone_mtx, local_matrix, local_matrix);
        }
    }

    // Transform to world space
    FUN_8012fa34(local_matrix, pos);
    out_pos[0] = pos[0];
    out_pos[1] = pos[1];
    out_pos[2] = pos[2];

    FUN_8038273c(); // epilogue
}

// ============================================================================
// 0x8007bef4 | entity_apply_vertex_color_offset
// Simplified wrapper for entity_apply_vertex_color_offset_full.
// Applies RGB color offset to entity vertex colors without alpha or
// channel overrides (param_6=0, param_10=0).
// Called by FUN_80127180, FUN_801f7308.
// ============================================================================
void entity_apply_vertex_color_offset(
    /* param_1, param_2: unused */
    char r_offset,            // param_3
    char g_offset,            // param_4
    char b_offset,            // param_5
    unsigned int r_override,  // param_6
    unsigned int g_override,  // param_7
    unsigned char b_override  // param_8
) {
    entity_apply_vertex_color_offset_full(
        0, 0, r_offset, g_offset, b_offset, 0,
        r_override, g_override, b_override, 0);
}

// ============================================================================
// 0x8007bf2c | entity_apply_vertex_color_offset_full
// Modifies per-vertex colors on up to 3 sub-meshes of an entity.
// For each sub-mesh with vertex color data:
//   1. Calls collision_get_mesh_stats to get vertex count
//   2. Allocates write-through vertex color buffer (collision_alloc_vertex_buffers)
//   3. For each vertex: reads packed RGBA (8 bits each), applies:
//      - Add offset per channel (params 3-6), clamp to [0, 127]
//      - If override channel is non-zero: replace with override value
//   4. Writes modified packed RGBA back
//   5. Flushes data cache (FUN_80399500) for DMA coherence
// Vertex color format: [A:8][R:8][G:8][B:8] packed into uint32
// Clamped to 7-bit range [0, 0x7F] per channel (GX hardware vertex color format).
// Called by entity_apply_vertex_color_offset wrapper + 2 direct callers.
// ============================================================================
void entity_apply_vertex_color_offset_full(
    /* param_1, param_2: unused */
    char r_offset,             // param_3: red additive offset
    char g_offset,             // param_4: green additive offset
    char b_offset,             // param_5: blue additive offset
    char a_offset,             // param_6: alpha additive offset
    unsigned int r_override,   // param_7: red override (0=no override)
    unsigned int g_override,   // param_8: green override (0=no override)
    unsigned char b_override,  // param_9: blue override (0=no override)
    unsigned char a_override   // param_10: alpha override (0=no override)
) {
    int pair = (int)FUN_803826b4(); // prologue
    int entity = (int)((uint64_t)pair >> 32);
    int sub_idx = (int)pair;

    int model_data = *(int*)(entity + 4);
    int* sub_obj_header = *(int**)(model_data + 4);
    int base_offset = sub_idx * 0xC0 + *sub_obj_header * -0xC0;

    for (int mesh = 0; mesh < 3; mesh++) {
        // Check if this mesh has vertex color data
        if (*(int*)((int)sub_obj_header + base_offset + 0x10 + mesh * 0x14) == 0) continue;

        int vertex_count[22]; // local_58 (index 0 = count)
        collision_get_mesh_stats(*(int*)(entity + 4), sub_idx, mesh, 4);
        unsigned int* color_buf = (unsigned int*)collision_alloc_vertex_buffers(
            *(int*)(entity + 4), sub_idx, mesh, 4, NULL, NULL, vertex_count);

        if (color_buf == NULL) continue;

        unsigned int* src = *(unsigned int**)((int)sub_obj_header + base_offset + 0x10 + mesh * 0x14);
        unsigned int* dst = color_buf;

        for (int v = 0; v < vertex_count[0]; v++) {
            unsigned int packed = *src++;

            // Extract channels and apply offsets (clamp to [0, 0x7F])
            unsigned int a = ((packed >> 24) + (int)r_offset);
            a = (a > 0x7F) ? 0x7F : ((int)a < 0 ? 0 : a);

            unsigned int r = (((packed >> 16) & 0xFF) + (int)g_offset);
            r = (r > 0x7F) ? 0x7F : ((int)r < 0 ? 0 : r);

            unsigned int g = (((packed >> 8) & 0xFF) + (int)b_offset);
            g = (g > 0x7F) ? 0x7F : ((int)g < 0 ? 0 : g);

            unsigned int b = ((packed & 0xFF) + (int)a_offset);
            b = (b > 0x7F) ? 0x7F : ((int)b < 0 ? 0 : b);

            // Apply overrides if non-zero
            if (r_override & 0xFF) a = r_override & 0xFF;
            if (g_override & 0xFF) r = g_override & 0xFF;
            if (b_override) g = (unsigned int)b_override;
            if (a_override) b = (unsigned int)a_override;

            *dst++ = (a << 24) | (r << 16) | (g << 8) | b;
        }

        // Flush cache for DMA coherence
        FUN_80399500((unsigned int)color_buf, vertex_count[0] * 4);
    }

    FUN_80382700(); // epilogue
}

// ============================================================================
// 0x8007c184 | entity_model_cache_reset
// Resets the entity model allocation cache counter to zero.
// Called at level load/unload to invalidate cached model pointers.
// The cache table (DAT_8055a194) stores {key, ptr} pairs up to DAT_80611c34 entries.
// ============================================================================
void entity_model_cache_reset(void) {
    DAT_80611c34 = 0;
}

// ============================================================================
// 0x8007c190 | entity_model_cache_alloc
// Cached allocation for entity model/material data. Avoids duplicate allocations
// by maintaining a lookup table of {model_id, allocation_ptr} pairs.
// If param_10 (model ID key) already exists in cache: returns cached pointer.
// Otherwise: allocates 64-byte aligned memory via mem_alloc_aligned,
// clears it to zero, stores in cache, increments counter.
// Cache: DAT_8055a194 array of {int key, void* ptr} pairs, max DAT_80611c34.
// Called by 10 functions across the model/material loading system.
// ============================================================================
void* entity_model_cache_alloc(
    /* params 1-8: passed through to mem_alloc_aligned */
    int model_id,    // param_9 (actually param_10 in decompiler numbering)
    int alloc_size,  // param_11
    /* params 12-16: passed through */
) {
    // Search cache for existing allocation
    for (int i = 0; i < DAT_80611c34; i++) {
        if ((&DAT_8055a194)[i * 2] == model_id && model_id != 0) {
            return (void*)(&DAT_8055a198)[i * 2];
        }
    }

    // Not found - allocate new
    (&DAT_8055a194)[DAT_80611c34 * 2] = model_id;
    void* ptr = mem_alloc_aligned(/* params */, 0x40, alloc_size, 1, /* ... */);
    (&DAT_8055a198)[DAT_80611c34 * 2] = (int)ptr;
    memset_wrapper((int)ptr, 0, 4);
    DAT_80611c34++;

    return ptr;
}

// ============================================================================
// 0x8007c260 | entity_get_world_position
// Computes and returns entity's world-space position (base pos + offset).
// Stores result in static global buffer DAT_8055a324 (3 floats).
// Returns pointer to the static buffer.
// Position = {entity+0x64 + entity+0x1A8, entity+0x68 + entity+0x1AC,
//             entity+0x6C + entity+0x1B0}
// EXTREMELY widely used (51+ callers) - the primary position accessor.
// ============================================================================
float* entity_get_world_position(int entity) {
    DAT_8055a324 = *(float*)(entity + 0x64) + *(float*)(entity + 0x1A8);
    DAT_8055a328 = *(float*)(entity + 0x68) + *(float*)(entity + 0x1AC);
    DAT_8055a32c = *(float*)(entity + 0x6C) + *(float*)(entity + 0x1B0);
    return &DAT_8055a324;
}

// ============================================================================
// 0x8007c29c | entity_is_valid_ai_target
// Checks if an entity is a valid target for the current AI controller.
// Returns 1 if:
//   - Entity's target field (+0x10) matches the AI controller's entity (+0x214)
//   - OR entity's target field is 0 (unassigned/any target)
// If AI system is not active or AI mode != 0: treats as no-target (iVar4=0).
// Called by FUN_80202da0 (AI targeting logic).
// ============================================================================
int entity_is_valid_ai_target(int entity) {
    int ai_entity = 0;

    bool ai_active = FUN_800a21b8(); // check if AI system active
    if (ai_active) {
        int ai_mode = FUN_800a21c8(); // get AI mode
        if (ai_mode == 0) {
            int ai_controller = FUN_800a26cc(); // get AI controller
            ai_entity = *(int*)(ai_controller + 0x214);
        }
    }

    int target = *(int*)(entity + 0x10);
    if (target == ai_entity || target == 0) {
        return 1;
    }
    return 0;
}
