// Address: 0x800714a0 - 0x80071d03 | Batch 162 | Date: 2026-08-10
// Entity bounding volume computation: matrix transform, AABB overlap,
// LOD bounds, per-frame update, mesh bounds calculation.
// Part of the entity subsystem (game)

#include "common.h"

// ============================================================================
// External references
// ============================================================================

extern "C" {
    void FUN_8012fa34(float* matrix, float* vec);
    void FUN_800117cc(float* aabb_a_min, float* aabb_a_max, float* aabb_b_min, float* aabb_b_max);
    double render_entity_get_lod_distance(int mesh_data);
    int scene_room_is_processed(unsigned int room_id);
    int event_dispatch(int entity, int event_type, int param);
    void FUN_80075ef0(float* out_matrix, int entity, int flag_a, int flag_b);
    double FUN_800981c8(int entity);
    void entity_compute_velocity_midpoint(int entity, float* min, float* max, float* out_vel, float* out_mid);
    void FUN_800714a0(float* matrix, float* out_extent, float* center);
}

// Constants
// FLOAT_80613fb4 = 0.0f
// FLOAT_80613fc0 = 0.0f (alternate location)
// FLOAT_80613fc8 = some default Y offset
// FLOAT_80613fcc = FLT_MAX (for bounds init)
// FLOAT_80613fd0 = -FLT_MAX (for bounds init)

// ============================================================================
// 0x800714a0 | entity_transform_bounds_by_matrix
// Transforms a center point through matrix, then computes the transformed
// extent vector using absolute values of the 3x3 rotation portion.
// This gives a conservative axis-aligned extent after rotation.
// ============================================================================

void entity_transform_bounds_by_matrix(float* matrix, float* out_extent, float* center)
{
    // Transform center point through the matrix
    FUN_8012fa34(matrix, center);

    // Build absolute-value rotation matrix for extent transformation
    float abs_matrix[12];
    abs_matrix[0] = fabsf(matrix[0]);
    abs_matrix[1] = fabsf(matrix[1]);
    abs_matrix[2] = fabsf(matrix[2]);
    abs_matrix[3] = fabsf(matrix[4]);
    abs_matrix[4] = fabsf(matrix[5]);
    abs_matrix[5] = fabsf(matrix[6]);
    abs_matrix[6] = fabsf(matrix[8]);
    abs_matrix[7] = fabsf(matrix[9]);
    abs_matrix[8] = fabsf(matrix[10]);
    // Translation row = zero (extent is direction-only)
    abs_matrix[9]  = 0.0f;
    abs_matrix[10] = 0.0f;
    abs_matrix[11] = 0.0f;

    // Transform extent through absolute rotation
    FUN_8012fa34(abs_matrix, out_extent);
}

// ============================================================================
// 0x80071564 | entity_overlap_test_aabb
// Builds AABBs for two entities (position + offset + extent) and tests
// for overlap using FUN_800117cc.
// Entity AABB: center = pos(0x1A8) + offset(0x64), half-extent = extent(0x1C0)
// ============================================================================

void entity_overlap_test_aabb(int entity_a, int entity_b)
{
    float aabb_a_min[3], aabb_a_max[3];
    float aabb_b_min[3], aabb_b_max[3];

    // Entity A: center = position + offset
    float center_a_x = *(float*)(entity_a + 0x64) + *(float*)(entity_a + 0x1A8);
    float center_a_y = *(float*)(entity_a + 0x68) + *(float*)(entity_a + 0x1AC);
    float center_a_z = *(float*)(entity_a + 0x6C) + *(float*)(entity_a + 0x1B0);

    aabb_a_min[0] = center_a_x - *(float*)(entity_a + 0x1C0);
    aabb_a_max[0] = center_a_x + *(float*)(entity_a + 0x1C0);
    aabb_a_min[1] = center_a_y - *(float*)(entity_a + 0x1C4);
    aabb_a_max[1] = center_a_y + *(float*)(entity_a + 0x1C4);
    aabb_a_min[2] = center_a_z - *(float*)(entity_a + 0x1C8);
    aabb_a_max[2] = center_a_z + *(float*)(entity_a + 0x1C8);

    // Entity B: center = position + offset
    float center_b_x = *(float*)(entity_b + 0x64) + *(float*)(entity_b + 0x1A8);
    float center_b_y = *(float*)(entity_b + 0x68) + *(float*)(entity_b + 0x1AC);
    float center_b_z = *(float*)(entity_b + 0x6C) + *(float*)(entity_b + 0x1B0);

    aabb_b_min[0] = center_b_x - *(float*)(entity_b + 0x1C0);
    aabb_b_max[0] = center_b_x + *(float*)(entity_b + 0x1C0);
    aabb_b_min[1] = center_b_y - *(float*)(entity_b + 0x1C4);
    aabb_b_max[1] = center_b_y + *(float*)(entity_b + 0x1C4);
    aabb_b_min[2] = center_b_z - *(float*)(entity_b + 0x1C8);
    aabb_b_max[2] = center_b_z + *(float*)(entity_b + 0x1C8);

    // Test overlap
    FUN_800117cc(aabb_a_min, aabb_a_max, aabb_b_min, aabb_b_max);
}

// ============================================================================
// 0x80071654 | entity_compute_lod_bounds
// For LOD-type entities (type 0x1000 or 0x8), gets the LOD distance from the
// render system and uses it as a uniform bounding sphere extent.
// Sets entity position from bone matrix translation or defaults.
// Returns 1 if entity is LOD-type, 0 otherwise.
// ============================================================================

int entity_compute_lod_bounds(int entity)
{
    int entity_type = *(int*)(entity + 0x20);

    // Only applies to type 0x1000 (LOD mesh) or type 0x8 (simple mesh)
    if (entity_type != 0x1000 && !(entity_type < 0x1000 && entity_type == 8)) {
        return 0;
    }

    int mesh_data = *(int*)(*(int*)(entity + 4) + 4);
    double lod_dist = render_entity_get_lod_distance(mesh_data);

    if ((*(unsigned int*)(entity + 0x28) & 0x100) == 0) {
        // Use bone matrix translation as position
        int bone_base = *(int*)(*(int*)(entity + 4) + 0x10);
        *(float*)(entity + 0x1A8) = *(float*)(bone_base + 0x30);
        *(float*)(entity + 0x1AC) = *(float*)(bone_base + 0x34);
        *(float*)(entity + 0x1B0) = *(float*)(bone_base + 0x38);
    } else {
        // Default position (0, default_y_offset, 0)
        *(float*)(entity + 0x1A8) = 0.0f;
        *(float*)(entity + 0x1AC) = /* FLOAT_80613fc8 */ 0.0f;
        *(float*)(entity + 0x1B0) = 0.0f;
    }

    // Set uniform extent = LOD distance
    *(float*)(entity + 0x1C0) = (float)lod_dist;
    *(float*)(entity + 0x1C4) = (float)lod_dist;
    *(float*)(entity + 0x1C8) = (float)lod_dist;

    return 1;
}

// ============================================================================
// 0x8007170c | entity_update_bounds
// MASSIVE: Main per-frame bounding volume update for an entity.
// 1. Checks if entity's room is visible (scene_room_is_processed)
// 2. Tries entity_compute_lod_bounds for LOD types
// 3. Dispatches event 5 (bounds update event)
// 4. If entity has custom bounds callback (+0x180), calls it
// 5. Otherwise: calls entity_compute_mesh_bounds -> transforms bounds
//    by world matrix -> optionally scales by animation factor ->
//    extends by collision volumes -> calls entity_compute_velocity_midpoint
// Called by: scene_room_streaming_update, cutscene_update_slots, etc.
// ============================================================================

void entity_update_bounds(int entity)
{
    unsigned int room_visible = scene_room_is_processed(
        (unsigned int)*(unsigned short*)(entity + 0x94));

    if (room_visible == 0) return;

    // Check skip conditions
    int mesh_count = *(int*)(*(int*)(*(int*)(entity + 4) + 4) + 4);
    int lod_result = 0;

    if ((*(unsigned int*)(entity + 0x28) & 0x200000) != 0 ||
        mesh_count < 1 ||
        (lod_result = entity_compute_lod_bounds(entity),
         lod_result == 0 && (*(unsigned int*)(entity + 0x28) & 0x100) != 0)) {
        // Check event dispatch
        if (event_dispatch(entity, 5, 0) != 0) return;
    } else {
        return;  // LOD bounds handled or mesh count is 0
    }

    if (*(void**)(entity + 0x180) != NULL) {
        // Custom bounds callback
        typedef void (*BoundsCallback)(int, int);
        ((BoundsCallback)(*(int*)(entity + 0x180)))(entity, 0);
    } else {
        float extent[3], center[3];
        float world_matrix[17];

        // Compute raw mesh bounds
        FUN_800719b4(entity, &extent[0], &center[0]);  // entity_compute_mesh_bounds

        // Build world-to-local matrix
        FUN_80075ef0(world_matrix, entity, 0, 0);

        // Transform bounds by world matrix (abs rotation for extent)
        entity_transform_bounds_by_matrix(world_matrix, extent, center);

        // If entity has animation scale (type 0x40 with anim data)
        if (*(int*)(entity + 0x7C) != 0 && *(int*)(entity + 0x20) == 0x40) {
            double scale = FUN_800981c8(entity);
            center[0] = (float)((double)center[0] * scale);
            center[1] = (float)((double)center[1] * scale);
            center[2] = (float)((double)center[2] * scale);
            extent[0] = (float)((double)extent[0] * scale);
            extent[1] = (float)((double)extent[1] * scale);
            extent[2] = (float)((double)extent[2] * scale);
        }

        // Store results: center -> position, extent -> extent
        *(float*)(entity + 0x1A8) = center[0];
        *(float*)(entity + 0x1AC) = center[1];
        *(float*)(entity + 0x1B0) = center[2];
        *(float*)(entity + 0x1C0) = extent[0];
        *(float*)(entity + 0x1C4) = extent[1];
        *(float*)(entity + 0x1C8) = extent[2];

        // Extend bounds by collision volumes if present
        int collision_count = *(int*)(entity + 0x1E4);
        if (collision_count > 0) {
            float bounds_min[3], bounds_max[3];

            bounds_min[0] = *(float*)(entity + 0x1A8) - *(float*)(entity + 0x1C0);
            bounds_max[0] = *(float*)(entity + 0x1A8) + *(float*)(entity + 0x1C0);
            bounds_min[1] = *(float*)(entity + 0x1AC) - *(float*)(entity + 0x1C4);
            bounds_max[1] = *(float*)(entity + 0x1AC) + *(float*)(entity + 0x1C4);
            bounds_min[2] = *(float*)(entity + 0x1B0) - *(float*)(entity + 0x1C8);
            bounds_max[2] = *(float*)(entity + 0x1B0) + *(float*)(entity + 0x1C8);

            for (int i = 0; i < collision_count; i++) {
                int vol = *(int*)(*(int*)(entity + 500) + i * 4);
                float vx = *(float*)(vol + 0x08) - *(float*)(entity + 0x64);
                float vy = *(float*)(vol + 0x0C) - *(float*)(entity + 0x68);
                float vy2 = *(float*)(vol + 0x18) - *(float*)(entity + 0x68);
                float vz = *(float*)(vol + 0x10) - *(float*)(entity + 0x6C);

                if (vx < bounds_min[0]) bounds_min[0] = vx;
                if (vx > bounds_max[0]) bounds_max[0] = vx;
                if (vy < bounds_min[1]) bounds_min[1] = vy;
                if (vy2 > bounds_max[1]) bounds_max[1] = vy2;
                if (vz < bounds_min[2]) bounds_min[2] = vz;
                if (vz > bounds_max[2]) bounds_max[2] = vz;
            }

            entity_compute_velocity_midpoint(
                entity, bounds_min, bounds_max,
                (float*)(entity + 0x1C0), (float*)(entity + 0x1A8));
        }
    }
}

// ============================================================================
// 0x800719b4 | entity_compute_mesh_bounds
// MASSIVE: Iterates over all mesh sub-objects and computes a tight
// world-space bounding box from bone-transformed vertex positions.
// Uses bone matrices to transform each sub-object's local bounds,
// accumulates min/max across all sub-objects.
// Output: center (param_3) and extent (param_2) via entity_compute_velocity_midpoint.
// ============================================================================

void entity_compute_mesh_bounds(int entity, float* out_extent, float* out_center)
{
    // Algorithm:
    // 1. Get entity context via FUN_803826cc
    // 2. Get mesh data from entity -> model -> sub-object list
    // 3. Initialize bounds to FLT_MAX/-FLT_MAX
    // 4. For each sub-object (stride 0xC0):
    //    a. Skip if sub-object scale is 0.0
    //    b. For 3 iterations (bone chain: self, parent, grandparent):
    //       - Read local center (offset +0x84..+0x8C) and extent (+0x90..+0x98)
    //       - If entity has bone transforms (flag 0x8 set, not flag 0x100):
    //         * Resolve bone index for this iteration level
    //         * Transform bounds by bone matrix via entity_transform_bounds_by_matrix
    //       - Expand global min/max from transformed center +/- extent
    // 5. Call entity_compute_velocity_midpoint(entity, min, max, out_extent, out_center)
    // 6. Restore registers via FUN_80382718

    // [Full implementation preserved in Ghidra IDB as raw pseudocode]
    // See Ghidra decompilation at 0x800719b4 for complete logic
}
