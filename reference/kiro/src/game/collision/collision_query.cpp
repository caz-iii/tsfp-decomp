// =============================================================================
// collision_query.cpp - Collision system: raycasting, BVH traversal, mesh transforms
// Part of tsfp-decomp (TimeSplitters: Future Perfect decompilation)
// Batch: 149 | Date: 2026-08-10
// =============================================================================

#include "common.h"

// Global collision result buffer at 0x8053285c (32 bytes)
// Saved copy at 0x8053287c (32 bytes)
// Active hit result at 0x80532880 (entity ptr, bone index, material flags, etc.)

// =============================================================================
// Address: 0x80065440 | Name: mesh_collision_transform_wrapper | Batch: 149
// Date: 2026-08-10
// Description: Wrapper for mesh_collision_transform_vertices using model's own
//   position (offset 0x2c from model data). Simple convenience entry point.
// =============================================================================

void mesh_collision_transform_wrapper(int entity, u32 param_2, float* world_offset) {
    float* model_pos = (float*)(*(int*)(entity + 4) + 0x2c);
    mesh_collision_transform_vertices(entity, param_2, model_pos, world_offset);
}

// =============================================================================
// Address: 0x8006546c | Name: collision_result_save | Batch: 149
// Date: 2026-08-10
// Description: Copies saved collision result buffer (0x8053287c) into active
//   collision result buffer (0x8053285c). 32 bytes, unrolled 4x DWORD copy.
// =============================================================================

void collision_result_save(void) {
    u32* dst = (u32*)0x8053285c;
    u32* src = (u32*)0x8053287c;

    for (int i = 0; i < 8; i++) {
        dst[i] = src[i];
    }
}

// =============================================================================
// Address: 0x800654a4 | Name: collision_result_store | Batch: 149
// Date: 2026-08-10
// Description: Stores external data into the active collision result buffer
//   (32 bytes). Source address is param_1 - 4 (adjusts for header offset).
// =============================================================================

void collision_result_store(int src_addr) {
    u32* dst = (u32*)0x8053285c;
    u32* src = (u32*)(src_addr - 4);

    for (int i = 0; i < 8; i++) {
        dst[i] = src[i];
    }
}

// =============================================================================
// Address: 0x800654d4 | Name: collision_result_restore | Batch: 149
// Date: 2026-08-10
// Description: Restores collision result buffer from active (0x8053285c) to
//   external memory. Inverse of collision_result_store.
// =============================================================================

void collision_result_restore(int dst_addr) {
    u32* src = (u32*)0x8053285c;
    u32* dst = (u32*)(dst_addr - 4);

    for (int i = 0; i < 8; i++) {
        dst[i] = src[i];
    }
}

// =============================================================================
// Address: 0x80065504 | Name: collision_bvh_raycast | Batch: 149
// Date: 2026-08-10
// Description: Recursive BVH (Bounding Volume Hierarchy) traversal for ray/point
//   collision queries against entity mesh. Interior nodes descend by AABB-frustum
//   test; leaf nodes test individual triangles via FUN_80011fac. Stores hit result
//   in globals at 0x80532880 (entity ptr, bone index, face index, material data).
//   Supports quantized byte-encoded child AABBs for memory efficiency.
//
// Parameters:
//   entity_ptr  - Pointer to entity data
//   bone_index  - Sub-object/bone index for skinned meshes
//   mesh_data   - Mesh collision data (BVH tree + vertex buffers)
//   node        - Current BVH node (interior or leaf)
//   aabb_bounds - Parent AABB [min_x, min_y, min_z, max_x, max_y, max_z]
//   frustum     - Frustum/ray planes for intersection test
//   hit_pos_out - Output: hit position
//   hit_normal  - Output: hit normal
//   flags       - Collision flags (skip floor=8, skip walls=16, etc.)
// =============================================================================

void collision_bvh_raycast(
    void* entity_ptr,
    int bone_index,
    int mesh_data,
    int node,
    float* aabb_bounds,
    float* frustum,
    float* hit_pos_out,
    float* hit_normal,
    u32 flags
) {
    FUN_803826b8(); // perf counter begin

    if (*(short*)(node + 6) == 0) {
        // Interior node: has two children
        if ((*(u32*)(/*entity flags*/ + 0xc) & 0x80) == 0) {
            // Standard children: indices at node+8 and node+10
            int child_left  = *(int*)(mesh_data + 0x24) + (u32)*(u16*)(node + 8) * 12;
            int child_right = *(int*)(mesh_data + 0x24) + (u32)*(u16*)(node + 10) * 12;
            collision_bvh_raycast(entity_ptr, bone_index, mesh_data, child_left,
                                 aabb_bounds, frustum, hit_pos_out, hit_normal, flags);
            collision_bvh_raycast(entity_ptr, bone_index, mesh_data, child_right,
                                 aabb_bounds, frustum, hit_pos_out, hit_normal, flags);
        } else {
            // Quantized children: 6-byte AABB encoded as [0..255] within parent bounds
            float parent_size_x = (aabb_bounds[3] - aabb_bounds[0]) / 255.0f;
            float parent_size_y = (aabb_bounds[4] - aabb_bounds[1]) / 255.0f;
            float parent_size_z = (aabb_bounds[5] - aabb_bounds[2]) / 255.0f;

            // Left child
            u8* left_bytes = (u8*)(*(int*)(mesh_data + 0x24) + (u32)*(u16*)(node + 8) * 12);
            float child_aabb[6];
            child_aabb[0] = parent_size_x * (float)left_bytes[0] + aabb_bounds[0];
            child_aabb[1] = parent_size_y * (float)left_bytes[1] + aabb_bounds[1];
            child_aabb[2] = parent_size_z * (float)left_bytes[2] + aabb_bounds[2];
            child_aabb[3] = parent_size_x * (float)left_bytes[3] + aabb_bounds[0];
            child_aabb[4] = parent_size_y * (float)left_bytes[4] + aabb_bounds[1];
            child_aabb[5] = parent_size_z * (float)left_bytes[5] + aabb_bounds[2];

            if (FUN_80011d98(frustum, child_aabb) != 0) {
                collision_bvh_raycast(entity_ptr, bone_index, mesh_data, (int)left_bytes,
                                     child_aabb, frustum, hit_pos_out, hit_normal, flags);
            }

            // Right child
            u8* right_bytes = (u8*)(*(int*)(mesh_data + 0x24) + (u32)*(u16*)(node + 10) * 12);
            child_aabb[0] = parent_size_x * (float)right_bytes[0] + aabb_bounds[0];
            child_aabb[1] = parent_size_y * (float)right_bytes[1] + aabb_bounds[1];
            child_aabb[2] = parent_size_z * (float)right_bytes[2] + aabb_bounds[2];
            child_aabb[3] = parent_size_x * (float)right_bytes[3] + aabb_bounds[0];
            child_aabb[4] = parent_size_y * (float)right_bytes[4] + aabb_bounds[1];
            child_aabb[5] = parent_size_z * (float)right_bytes[5] + aabb_bounds[2];

            if (FUN_80011d98(frustum, child_aabb) != 0) {
                collision_bvh_raycast(entity_ptr, bone_index, mesh_data, (int)right_bytes,
                                     child_aabb, frustum, hit_pos_out, hit_normal, flags);
            }
        }
    } else {
        // Leaf node: iterate triangles
        int* entity_data = /* entity model data */;
        int face_count = (int)(u16)*(short*)(node + 10);

        for (int face_idx = 0; face_idx < face_count; face_idx++) {
            // Get face entry from face table
            char* face_entry = (char*)(*(int*)(mesh_data + 0x1c) +
                               ((u32)*(u16*)(node + 8) + face_idx) * 0xe);
            u8 sub_obj_type = face_entry[4] & 3;

            // Skip floor/wall based on flags
            if ((flags & 8) != 0 && sub_obj_type == 1) continue;
            if ((flags & 16) != 0 && sub_obj_type == 2) continue;

            // Material lookup
            short* material = (short*)(/* material table */ + (u32)*(u16*)(face_entry + 6) * 8);
            int* material_data = NULL;
            if (*material >= 0) {
                material_data = (int*)(/* material pool */ + *material * 0x30);
                u32 mat_flags = material_data[7];
                // Check material visibility flags
                bool visible = true;
                if ((mat_flags & 2) != 0) {
                    visible = ((mat_flags & 1) != 0);
                } else if ((mat_flags & 0x80000) != 0) {
                    visible = false;
                } else if (sub_obj_type != 1) {
                    visible = true;
                } else {
                    visible = false;
                }
                if (!visible) continue;
            }

            // Get vertex buffer pointer
            int vertex_base;
            if ((/* model flags */ & 0x10) == 0) {
                vertex_base = /* vertex buf */ + (u32)(u16)material[3] * 12;
            } else {
                vertex_base = /* vertex buf */ + (u32)(u16)material[3] * 6;
            }

            // Determine face flip
            int face_flip = 0;
            if (sub_obj_type == 0 || (sub_obj_type == 2 && /* entity scale */ != 0x10000)) {
                face_flip = 1;
            }

            // Triangle intersection test
            int hit = FUN_80011fac(frustum, vertex_base, mesh_data,
                                   (float*)(/* model pos */),
                                   /* quantized flag */, face_entry, face_flip,
                                   hit_pos_out, hit_normal);

            if (hit != 0) {
                // Store hit result in global buffer
                if (material_data == NULL) {
                    DAT_80532894 = 0xFFFFFFFF; // no material
                    DAT_80532898 = 0;
                    DAT_8053289c = 0;
                } else {
                    DAT_80532894 = (material_data[0] >= 0)
                        ? *(u32*)(/* texture table */ + material_data[0] * 0x10)
                        : 0xFFFFFFFF;
                    DAT_80532898 = (u32)(u8)(material_data[6]);
                    DAT_8053289c = material_data[7];
                }
                DAT_80532888 = sub_obj_type;
                DAT_8053288c = (u32)*(u16*)(face_entry + 6);
                DAT_80532890 = (u32)*(u16*)(node + 8) + face_idx;
                DAT_80532880 = entity_ptr;
                DAT_80532884 = bone_index;
            }
        }
    }

    FUN_80382704(); // perf counter end
}

// =============================================================================
// Address: 0x80065a74 | Name: collision_entity_raycast | Batch: 149
// Date: 2026-08-10
// Description: High-level ray cast against a single entity. Builds frustum from
//   ray endpoints, iterates sub-objects (bones), dispatches BVH queries per bone.
//   Supports skinned entities with per-bone rotation offsets. Outputs hit
//   position and flipped normal (ensures normal faces the ray origin).
//
// Parameters:
//   param_1          - Unused (padding/legacy)
//   param_2          - Unused (padding/legacy)
//   ray_origin       - [3] Ray origin point
//   include_backfaces - If nonzero, also test backfaces (doubles search radius)
//   hit_pos_out      - [3] Output: world-space hit position (NULL to skip)
//   hit_normal_out   - [3] Output: world-space hit normal (NULL to skip)
//   flags            - Collision flags passed to BVH traversal
// =============================================================================

void collision_entity_raycast(
    u32 param_1,
    u32 param_2,
    float* ray_origin,
    int include_backfaces,
    float* hit_pos_out,
    float* hit_normal_out,
    u32 flags
) {
    FUN_803826cc(); // perf counter begin

    // Retrieve entity internal pointers
    int* entity_ptr = /* from stack context */;
    int* model_data = (int*)entity_ptr[1];
    int entity_base = *entity_ptr;
    int sub_obj_count = *model_data;

    // Initialize best-hit position/normal accumulators
    float best_hit_pos[3] = { DAT_803cc250, DAT_803cc254, DAT_803cc258 };
    float best_hit_normal[3] = { DAT_803cc25c, DAT_803cc260, DAT_803cc264 };

    // Skip if no sub-objects or entity flagged non-collidable
    if (model_data[1] < 1 || (*(u32*)(entity_base + 0x28) & 0x100) == 0) {
        // Set initial search distance based on backface mode
        float max_dist = include_backfaces ? FLOAT_80613ed0 : FLOAT_80613e48;
        float best_dist = max_dist;

        // Compute entity bounding box for early rejection
        float center_x = *(float*)(entity_base + 0x64) + *(float*)(entity_base + 0x1a8);
        float center_y = *(float*)(entity_base + 0x68) + *(float*)(entity_base + 0x1ac);
        float center_z = *(float*)(entity_base + 0x6c) + *(float*)(entity_base + 0x1b0);
        float extent_x = *(float*)(entity_base + 0x1c0);
        float extent_y = *(float*)(entity_base + 0x1c4);
        float extent_z = *(float*)(entity_base + 0x1c8);

        float entity_aabb[6];
        entity_aabb[0] = center_x - extent_x;
        entity_aabb[1] = center_y - extent_y;
        entity_aabb[2] = center_z - extent_z;
        entity_aabb[3] = center_x + extent_x;
        entity_aabb[4] = center_y + extent_y;
        entity_aabb[5] = center_z + extent_z;

        // Build frustum planes from ray
        float frustum_planes[9];
        FUN_80011d00((double)best_dist, frustum_planes, /*ray_start*/, ray_origin);

        // Early rejection: ray frustum vs entity AABB
        if (FUN_80011d98(frustum_planes, entity_aabb) == 0) {
            goto done;
        }

        // Additional flag check
        if ((flags & 0x80) != 0 && *(int*)(entity_base + 0x20) != 2) {
            goto done;
        }

        // Check if entity is skinned (multi-bone)
        bool is_skinned = false;
        if (model_data[1] > 0 &&
            (*(u32*)(entity_base + 0x28) & 8) != 0 &&
            (*(u32*)(entity_base + 0x28) & 0x200000) != 0) {
            is_skinned = true;
        }

        // Handle rotation offset if entity has one
        float ray_local[3], origin_local[3];
        float inv_matrix[16];
        if (*(char*)(entity_base + 0x32) != '\0') {
            float bone_matrix[12];
            FUN_80075ef0(bone_matrix, entity_base, 1, 0);
            // Transform ray into entity local space
            ray_local[0] = /*ray_start_x*/ - bone_matrix[9];
            ray_local[1] = /*ray_start_y*/ - bone_matrix[10];
            ray_local[2] = /*ray_start_z*/ - bone_matrix[11];
            origin_local[0] = ray_origin[0];
            origin_local[1] = ray_origin[1];
            origin_local[2] = ray_origin[2];
            FUN_80130cd8(bone_matrix, inv_matrix);
            FUN_8012fbf4(inv_matrix, ray_local);
            FUN_8012fbf4(inv_matrix, origin_local);
            if (!is_skinned) {
                FUN_80011d00((double)best_dist, frustum_planes, ray_local, origin_local);
            }
        }

        // Iterate sub-objects (bones)
        for (int bone = 0; bone < *model_data; bone++) {
            int bone_offset = bone * 0xC0 + sub_obj_count * -0xC0;
            u32 bone_mask = *(u32*)((int)model_data + bone_offset + 0x50);

            // Skip if bone mask doesn't match entity's active mask
            if (bone_mask != 0 && (entity_ptr[0xc] & bone_mask) == 0) continue;

            int collision_mesh = *(int*)((int)model_data + bone_offset + 0x9c);
            if (collision_mesh == 0) continue;
            if (*(int*)(collision_mesh + 0x24) == 0) continue;

            // For skinned entities, transform ray into bone-local space
            if (is_skinned) {
                float* bone_xform = (float*)(entity_ptr[4] +
                    *(char*)((int)model_data + bone_offset + 0x3d) * 0x40);
                // offset ray by bone position and apply inverse rotation
                float bone_ray[3], bone_origin[3];
                bone_ray[0] = ray_local[0] - bone_xform[12];
                bone_ray[1] = ray_local[1] - bone_xform[13];
                bone_ray[2] = ray_local[2] - bone_xform[14];
                bone_origin[0] = origin_local[0];
                bone_origin[1] = origin_local[1];
                bone_origin[2] = origin_local[2];
                float bone_inv[16];
                FUN_80130cd8(bone_xform, bone_inv);
                FUN_8012fbf4(bone_inv, bone_ray);
                FUN_8012fbf4(bone_inv, bone_origin);
                FUN_80011d00((double)best_dist, frustum_planes, bone_ray, bone_origin);
            }

            // Test ray vs bone collision mesh AABB
            if (FUN_80011d98(frustum_planes, (float*)collision_mesh) != 0) {
                int hit = collision_bvh_raycast(
                    entity_ptr, bone, collision_mesh,
                    *(int*)(collision_mesh + 0x24),
                    (float*)collision_mesh, frustum_planes,
                    best_hit_pos, best_hit_normal, flags);

                // If hit on a skinned bone, transform result back to entity space
                if (hit != 0 && is_skinned) {
                    float* bone_xform = (float*)(entity_ptr[4] +
                        *(char*)((int)model_data + bone_offset + 0x3d) * 0x40);
                    FUN_8012fa34(bone_xform, best_hit_pos);
                    FUN_8012fbf4(bone_xform, best_hit_normal);
                }
            }
        }

        // If we got a hit closer than initial distance
        if ((double)best_dist < (double)max_dist) {
            // Write hit position
            if (hit_pos_out != NULL) {
                // Transform back to world if entity had rotation
                if (*(char*)(entity_base + 0x32) != '\0') {
                    float bone_matrix[12];
                    FUN_8012fa34(bone_matrix, best_hit_pos);
                }
                hit_pos_out[0] = best_hit_pos[0];
                hit_pos_out[1] = best_hit_pos[1];
                hit_pos_out[2] = best_hit_pos[2];
            }

            // Write hit normal (flipped to face ray origin)
            if (hit_normal_out != NULL) {
                if (*(char*)(entity_base + 0x32) != '\0') {
                    float bone_matrix[12];
                    FUN_8012fbf4(bone_matrix, best_hit_normal);
                }
                // Flip normal if it faces away from ray direction
                float dot = ray_origin[2] * best_hit_normal[2] +
                            ray_origin[0] * best_hit_normal[0] +
                            ray_origin[1] * best_hit_normal[1];
                if (dot > 0.0f) {
                    hit_normal_out[0] = -best_hit_normal[0];
                    hit_normal_out[1] = -best_hit_normal[1];
                    hit_normal_out[2] = -best_hit_normal[2];
                } else {
                    hit_normal_out[0] = best_hit_normal[0];
                    hit_normal_out[1] = best_hit_normal[1];
                    hit_normal_out[2] = best_hit_normal[2];
                }
            }
        }
    }

done:
    FUN_80382718(); // perf counter end
}

// =============================================================================
// Address: 0x80064328 | Name: mesh_collision_transform_vertices | Batch: 149
// Date: 2026-08-10
// Description: MASSIVE function. Transforms skinned mesh vertices from model-
//   space to world-space for collision detection. Handles:
//   - 3 sub-objects per entity (typically: body, arms, head)
//   - Float and quantized short vertex formats
//   - Multi-bone skinning with per-vertex bone assignment
//   - AABB bounds recomputation after transform
//   - Normal transform (packed byte normals rotated by bone matrix)
//   - Face normal recomputation from transformed vertex positions
//   - Dominant axis classification for face normals (used by raycaster)
//
// Parameters:
//   param_1      - Unused
//   param_2      - Unused
//   model_pos    - Optional new origin for quantized vertex re-encoding (NULL = use model data)
//   world_offset - World-space bone matrix (NULL = use entity's own bone)
// =============================================================================

void mesh_collision_transform_vertices(
    u32 param_1,
    u32 param_2,
    float* model_pos,
    float* world_offset
) {
    // This function is extremely large (500+ lines of decompiled output).
    // Core algorithm:
    //
    // 1. For each of 3 sub-objects:
    //    a. Determine skinning mode (1-bone, 2-bone, or 3-bone)
    //    b. Fetch bone matrices for all relevant bones
    //    c. Compute inverse matrices for each bone
    //    d. For each vertex run in the bone-weight table:
    //       - Read vertex position (float or quantized short)
    //       - Subtract bone origin
    //       - Multiply by inverse bone matrix (rotate to bone-local space)
    //       - Write back (re-quantize if short format)
    //       - Track min/max bounds for AABB update
    //       - If normals present: rotate packed normal by bone inverse
    //    e. Flush transformed vertex data
    //
    // 2. After all sub-objects:
    //    - Call render_bone_transform_geometry_to_local() for final fixup
    //    - Recompute entity AABB from tracked bounds (with 0.5 padding)
    //    - If face/BVH data exists:
    //       * Update BVH root AABB
    //       * For each face: recompute normal from 3 transformed vertices
    //       * Classify face normal dominant axis (for SAT optimization)
    //       * Pack normal into signed bytes
    //
    // 3. Optional: Transform attachment points (e.g. weapon mount positions)
    //    by their associated bone matrices.

    // [Full implementation is ~400 lines of vertex iteration - see raw decomp]
    // Key called functions:
    //   FUN_8002570c(bone_id)              - Select bone by ID
    //   FUN_80025848()                     - Get current bone matrix (4x3)
    //   FUN_80130b90(src, dst)             - Copy 4x3 matrix
    //   FUN_80130cd8(matrix, inverse_out)  - Compute 4x4 inverse of 4x3 matrix
    //   FUN_8012fbf4(matrix, vec3)         - Rotate vec3 by 3x3 part of matrix
    //   FUN_8012fa34(matrix, vec3)         - Transform vec3 by full 4x3 matrix
    //   render_bone_transform_packed_normal(matrix, packed_normal) - Rotate packed normal
    //   render_mesh_transform_normals(...)  - Batch normal transform for compressed format
    //   render_mesh_flush_vertex_data(...)  - Flush/sync vertex buffer after transform
    //   render_bone_transform_geometry_to_local(...) - Final local-space fixup
}

// =============================================================================
// Address: 0x80065f74 | Name: collision_bvh_swept_query | Batch: 150
// Date: 2026-08-10
// Description: Recursive BVH traversal for swept volume queries. Interior nodes
//   use quantized byte AABBs, tested against swept frustum via FUN_800144c0.
//   Leaf nodes test individual triangles via FUN_80015044 (swept tri intersection).
//   Records single closest hit per triangle (distance + position + normal).
//   Transforms results back through bone/entity matrices for skinned meshes.
//   Allocates result nodes from pool (FUN_80146610) and inserts into sorted list.
// =============================================================================

void collision_bvh_swept_query(
    void* entity_ptr,
    int bone_index,
    int mesh_data,
    int node,
    float* aabb_bounds,
    int* swept_frustum,
    u32 sweep_id,
    int sweep_param,
    int* result_list,
    float* entity_matrix,
    u32 flags
) {
    // BVH traversal structure identical to collision_bvh_raycast:
    // - Interior nodes (node+6 == 0): decode quantized child AABBs,
    //   test vs swept frustum, recurse on hits
    // - Leaf nodes: iterate face table, check material visibility,
    //   call FUN_80015044 for swept-triangle intersection
    //   On hit: transform through bone matrix if skinned,
    //   transform through entity matrix if rotated,
    //   allocate result node, insert into sorted result list
}

// =============================================================================
// Address: 0x800669f4 | Name: collision_bvh_multi_hit_query | Batch: 150
// Date: 2026-08-10
// Description: Recursive BVH traversal for multi-hit swept queries. Uses function
//   pointer at DAT_8060fac8 for triangle intersection, allowing different sweep
//   shapes (capsule, OBB) to share the same BVH traversal logic. Returns multiple
//   hit points per triangle (up to 8 positions + distances). Allocates result
//   nodes from pool and inserts into result list via FUN_80146650.
// =============================================================================

void collision_bvh_multi_hit_query(
    void* entity_ptr,
    int bone_index,
    int mesh_data,
    int node,
    float* aabb_bounds,
    int swept_volume,
    u32 sweep_id,
    int sweep_param,
    int* result_list,
    float* entity_matrix,
    u32 flags
) {
    // Same BVH traversal as collision_bvh_swept_query but:
    // - Uses function pointer DAT_8060fac8 for intersection (pluggable shape)
    // - Can return multiple hits per leaf (up to 8)
    // - Each hit allocated from pool and inserted into list
    // - Supports bone-local transform for skinned meshes
}

// =============================================================================
// Address: 0x80066a68 | Name: collision_bvh_obb_multi_hit | Batch: 150
// Date: 2026-08-10
// Description: Recursive BVH traversal for OBB (Oriented Bounding Box) multi-hit
//   queries. Uses FUN_80019950 for OBB-triangle intersection. Tests child AABBs
//   via FUN_800198e0 (OBB-AABB overlap). Returns multiple contact points per
//   triangle. Same pool allocation and result list pattern as other BVH variants.
// =============================================================================

void collision_bvh_obb_multi_hit(
    void* entity_ptr,
    int bone_index,
    int mesh_data,
    int node,
    float* aabb_bounds,
    int obb_data,
    u32 sweep_id,
    int sweep_param,
    int* result_list,
    float* entity_matrix,
    u32 flags
) {
    // BVH traversal with OBB-specific tests:
    // - Interior: FUN_800198e0 for OBB vs quantized AABB
    // - Leaf: FUN_80019950 for OBB vs triangle (returns up to 8 contacts)
    // - Material visibility check same as other variants
    // - Results transformed through bone/entity matrices
}

// =============================================================================
// Address: 0x80066ff4 | Name: collision_entity_capsule_sweep | Batch: 150
// Date: 2026-08-10
// Description: Entity collision cast using capsule/swept frustum. For each bone:
//   tests AABB overlap with swept volume, then dispatches to either
//   collision_bvh_multi_hit_query (for mesh BVH) or geometric primitive tests
//   (sphere via FUN_80015c74, capsule via FUN_800164f4, box via FUN_8001b304).
//   Also supports terrain collision via FUN_80100558. Builds swept frustum from
//   ray origin + direction via FUN_800179f0.
// =============================================================================

void collision_entity_capsule_sweep(
    u32 param_1,
    u32 param_2,
    float* ray_origin,
    float* ray_dir,
    u32 sweep_id,
    int sweep_param,
    int* result_list,
    int include_terrain,
    u32 flags
) {
    // High-level entity sweep cast (capsule shape):
    // 1. Compute entity AABB, test vs swept frustum for early reject
    // 2. Handle skinned entity: transform ray into bone-local space per bone
    // 3. For each bone sub-object:
    //    a. If has BVH mesh: call collision_bvh_multi_hit_query
    //    b. If has primitive shapes (sphere/capsule/box):
    //       - Type 0 (sphere): FUN_80015c74
    //       - Type 1 (capsule): FUN_800164f4
    //       - Type 2 (box): FUN_8001b304
    //    c. Allocate result nodes, insert into list
    // 4. If include_terrain: call FUN_80100558 for heightmap collision
}

// =============================================================================
// Address: 0x8006763c | Name: collision_entity_obb_sweep | Batch: 150
// Date: 2026-08-10
// Description: Entity collision cast using OBB (Oriented Bounding Box) sweep.
//   For each bone: tests AABB overlap via FUN_800198e0, then dispatches to
//   collision_bvh_obb_multi_hit (for mesh BVH) or geometric primitive tests
//   (sphere-OBB via FUN_8001ab5c, box-OBB via FUN_8001b304, OBB-OBB via
//   FUN_8001ad4c). Terrain via FUN_8010070c. Builds OBB from origin +
//   orientation via FUN_80019790.
// =============================================================================

void collision_entity_obb_sweep(
    u32 param_1,
    u32 param_2,
    float* ray_origin,
    float* orientation,
    u32 sweep_id,
    int sweep_param,
    int* result_list,
    int include_terrain,
    u32 flags
) {
    // High-level entity sweep cast (OBB shape):
    // 1. Compute entity AABB, test vs OBB for early reject
    // 2. Handle skinned: transform OBB into bone-local space per bone
    // 3. For each bone sub-object:
    //    a. If has BVH mesh: call collision_bvh_obb_multi_hit
    //    b. If has primitive shapes:
    //       - Type 0 (sphere): FUN_8001ab5c (sphere vs OBB)
    //       - Type 1 (capsule): FUN_8001b304 (capsule vs OBB)
    //       - Type 2 (box): FUN_8001ad4c (OBB vs OBB)
    //    c. Allocate result nodes, insert into list
    // 4. If include_terrain: FUN_8010070c for heightmap-OBB collision
}

// =============================================================================
// Address: 0x8006649c | Name: collision_entity_swept_cast | Batch: 150
// Date: 2026-08-10
// Description: High-level swept volume cast against entity. Generic entry point
//   that dispatches to collision_bvh_swept_query for BVH mesh, or geometric
//   primitive intersection (sphere/capsule/box). Builds swept volume from
//   position + direction via FUN_80014300. Terrain via FUN_80100364.
//   Used for physics movement: character controller ground detection, bullet
//   traces, etc.
// =============================================================================

void collision_entity_swept_cast(
    u32 param_1,
    u32 param_2,
    float* sweep_dir,
    u32 sweep_id,
    int sweep_param,
    int* result_list,
    int include_terrain,
    u32 flags
) {
    // High-level entity swept cast (generic swept volume):
    // 1. Compute entity AABB, test vs swept volume for early reject
    // 2. Handle skinned: transform sweep into bone-local space per bone
    // 3. For each bone sub-object:
    //    a. If has BVH mesh: call collision_bvh_swept_query
    //    b. If has primitive shapes:
    //       - Type 0 (sphere): FUN_80015844 (sphere sweep)
    //       - Type 1 (capsule): FUN_80015c74 (capsule sweep)
    //       - Type 2 (box): FUN_8001ab5c (box sweep)
    //    c. Allocate result node on hit, insert into list
    // 4. If include_terrain: FUN_80100364 for heightmap swept collision
}

// =============================================================================
// Address: 0x80066a68 duplicate — see above
// =============================================================================

// =============================================================================
// Address: 0x80068140 | Name: collision_set_shape_params | Batch: 150
// Date: 2026-08-10
// Description: Sets collision shape type byte at entity+0x1F0 and associated
//   flag bytes at +0x1F1 and +0x1F2. Marks collision as dirty (sets bit 2 in
//   parent entity flags at offset 0x18). Used during entity initialization.
// =============================================================================

void collision_set_shape_params(int* entity, u8 shape_type, u32 unused, u8 flag1, u8 flag2) {
    *(u8*)(entity + 0x7c) = shape_type;         // +0x1F0 (byte within int at +0x7C*4)
    *(u8*)((int)entity + 0x1f1) = flag1;
    *(u8*)((int)entity + 0x1f2) = flag2;
    *(u32*)(*entity + 0x18) |= 4;               // mark dirty
}

// =============================================================================
// Address: 0x80068160 | Name: collision_set_radius_and_flags | Batch: 150
// Date: 2026-08-10
// Description: Sets collision radius (float quantized to byte at +0x7F via
//   multiply by 255.0) and flag bytes at +0x1F3, +0x1F4, +0x1F5. Marks dirty.
// =============================================================================

void collision_set_radius_and_flags(double radius, int* entity, u8 flag_a, u8 flag_b, u8 flag_c) {
    *(char*)((int)entity + 0x7f) = (char)(int)(255.0 * radius);  // quantized radius
    *(u8*)((int)entity + 0x1f3) = flag_a;
    *(u8*)(entity + 0x7d) = flag_b;
    *(u8*)((int)entity + 0x1f5) = flag_c;
    *(u32*)(*entity + 0x18) |= 4;               // mark dirty
}

// =============================================================================
// Address: 0x800681a0 | Name: collision_set_layer_mask | Batch: 150
// Date: 2026-08-10
// Description: Sets collision layer mask (u16 at entity+0x76). Marks dirty.
//   Used to assign entities to collision layers for selective queries.
// =============================================================================

void collision_set_layer_mask(int* entity, u16 layer_mask) {
    *(u16*)((int)entity + 0x76) = layer_mask;
    *(u32*)(*entity + 0x18) |= 4;               // mark dirty
}

// =============================================================================
// Address: 0x80067bc0 | Name: collision_entity_swept_cast_default_dir | Batch: 150
// Date: 2026-08-10
// Description: Convenience wrapper for collision_entity_swept_cast using default
//   sweep direction from DAT_803cc268 (gravity/down vector). Used for ground
//   detection and downward traces.
// =============================================================================

void collision_entity_swept_cast_default_dir(
    u32 param_1,
    u32 param_2,
    u32 sweep_id,
    int sweep_param,
    int* result_list,
    int include_terrain,
    u32 flags
) {
    float default_dir[3];
    default_dir[0] = DAT_803cc268;  // typically (0, -1, 0) for gravity
    default_dir[1] = DAT_803cc26c;
    default_dir[2] = DAT_803cc270;

    collision_entity_swept_cast(
        /* entity context */, /* bone */,
        default_dir, sweep_id, sweep_param,
        result_list, include_terrain, flags
    );
}

// =============================================================================
// Address: 0x800681b8 | Name: collision_forward_shape_data | Batch: 151
// Date: 2026-08-10
// Description: Copies 48 bytes of shape data from source (param_2 - 4) into
//   local buffer, then forwards to FUN_8018d610 for shape registration.
// =============================================================================

void collision_forward_shape_data(short shape_id, int src_data) {
    u32 local_buf[12];
    u32* src = (u32*)(src_data - 4);

    for (int i = 0; i < 12; i++) {
        local_buf[i] = src[i];
    }

    FUN_8018d610(shape_id, (int)local_buf);
}

// =============================================================================
// Address: 0x80068200 | Name: collision_set_flags | Batch: 151
// Date: 2026-08-10
// Description: Sets collision flag bits at entity+0x1EE (OR operation). Marks
//   entity collision as dirty (bit 2 in parent flags at +0x18).
// =============================================================================

void collision_set_flags(int* entity, u16 flags) {
    *(u16*)((int)entity + 0x1ee) |= flags;
    *(u32*)(*entity + 0x18) |= 4;  // mark dirty
}

// =============================================================================
// Address: 0x80068220 | Name: collision_clear_flags | Batch: 151
// Date: 2026-08-10
// Description: Clears collision flag bits at entity+0x1EE (AND NOT). Does NOT
//   mark dirty (clearing flags is non-destructive).
// =============================================================================

void collision_clear_flags(int entity, u16 flags) {
    *(u16*)(entity + 0x1ee) &= ~flags;
}

// =============================================================================
// Address: 0x80068230 | Name: collision_count_strips | Batch: 151
// Date: 2026-08-10
// Description: Counts collision strip entries in table at model+0x10. Iterates
//   stride-0x10 entries until negative terminator at offset 0 of each entry.
// =============================================================================

int collision_count_strips(int model_data) {
    int count = 0;
    int offset = 0;
    int* table = *(int**)(model_data + 0x10);

    while (*(int*)((int)table + offset) >= 0) {
        count++;
        offset += 0x10;
    }
    return count;
}

// =============================================================================
// Address: 0x80068258 | Name: collision_count_faces | Batch: 151
// Date: 2026-08-10
// Description: Counts face entries in collision face table at model+0x14. Iterates
//   stride-0x30 entries until negative terminator at entry+0x10.
// =============================================================================

int collision_count_faces(int model_data) {
    if (*(int*)(model_data + 0x14) == 0) {
        return 0;
    }

    int count = 0;
    int offset = 0;
    int table_base = *(int*)(model_data + 0x14);

    while (*(int*)(table_base + offset + 0x10) >= 0) {
        count++;
        offset += 0x30;
    }
    return count;
}

// =============================================================================
// Address: 0x80068294 | Name: collision_alloc_strip_table | Batch: 151
// Date: 2026-08-10
// Description: Allocates mutable copy of collision strip table into secondary
//   memory pool. Stores pointer at entity[0x79]. Size = (count+1)*0x10.
//   Copies original table data. Marks dirty.
// =============================================================================

int collision_alloc_strip_table(int* entity) {
    int model = entity[1];
    if (*(int*)(model + 0x10) == 0) {
        return 0;
    }

    int count = collision_count_strips(model);
    int size = (count + 1) * 0x10;
    int buf = mem_advance_secondary(size);
    entity[0x79] = buf;
    leaf_31e8(entity[0x79], *(u32*)(model + 0x10), size);  // memcpy
    *(u32*)(*entity + 0x18) |= 4;  // mark dirty

    return entity[0x79];
}

// =============================================================================
// Address: 0x80068318 | Name: collision_alloc_strip_table_extended | Batch: 151
// Date: 2026-08-10
// Description: Like collision_alloc_strip_table but allocates extra_count
//   additional entries beyond the existing count. Used when runtime needs to
//   append new collision strips dynamically.
// =============================================================================

int collision_alloc_strip_table_extended(int* entity, int extra_count) {
    int model = entity[1];
    if (*(int*)(model + 0x10) == 0) {
        return 0;
    }

    int count = collision_count_strips(model);
    int size = (count + extra_count + 1) * 0x10;
    int buf = mem_advance_secondary(size);
    entity[0x79] = buf;
    leaf_31e8(entity[0x79], *(u32*)(model + 0x10), size);  // memcpy
    *(u32*)(*entity + 0x18) |= 4;  // mark dirty

    return entity[0x79];
}

// =============================================================================
// Address: 0x800683a4 | Name: collision_alloc_face_table | Batch: 151
// Date: 2026-08-10
// Description: Allocates mutable copy of collision face table into secondary
//   memory pool. Stores pointer at entity[0x7A]. Size = (count+1)*0x30.
//   Copies original table data. Marks dirty.
// =============================================================================

int collision_alloc_face_table(int* entity) {
    int model = entity[1];
    if (*(int*)(model + 0x14) == 0) {
        return 0;
    }

    int count = collision_count_faces(model);
    int size = (count + 1) * 0x30;
    int buf = mem_advance_secondary(size);
    entity[0x7a] = buf;
    leaf_31e8(entity[0x7a], *(u32*)(model + 0x14), size);  // memcpy
    *(u32*)(*entity + 0x18) |= 4;  // mark dirty

    return entity[0x7a];
}

// =============================================================================
// Address: 0x80068428 | Name: collision_set_strip_entry | Batch: 151
// Date: 2026-08-10
// Description: Sets value at strip_table[sub_index] (stride 0x10). Lazy-inits
//   strip table via collision_alloc_strip_table if not yet allocated.
// =============================================================================

void collision_set_strip_entry(u32 unused1, u32 unused2, u32 value) {
    // Context provides entity_ptr and sub_index from stack
    int* entity = /* from perf context */;
    int sub_index = /* from perf context */;

    if (entity[0x79] == 0) {
        if (collision_alloc_strip_table(entity) == 0) {
            return;
        }
    }
    *(u32*)(entity[0x79] + sub_index * 0x10) = value;
}

// =============================================================================
// Address: 0x80068484 | Name: collision_set_face_entry | Batch: 151
// Date: 2026-08-10
// Description: Sets values at face_table[sub_index]+0x14 and +0x10. Lazy-inits
//   face table via collision_alloc_face_table if not yet allocated. Writes
//   param_3 to +0x14 and global DAT_80612024 to +0x10.
// =============================================================================

void collision_set_face_entry(u32 unused1, u32 unused2, u32 value) {
    // Context provides entity_ptr and sub_index from stack
    int* entity = /* from perf context */;
    int sub_index = /* from perf context */;

    if (entity[0x7a] == 0) {
        if (collision_alloc_face_table(entity) == 0) {
            return;
        }
    }
    int entry = entity[0x7a] + sub_index * 0x30;
    *(u32*)(entry + 0x14) = value;
    *(u32*)(entry + 0x10) = DAT_80612024;
}

// =============================================================================
// Address: 0x80068924 | Name: collision_stub_empty | Batch: 152
// Date: 2026-08-10
// Description: Empty stub (no-op). Placeholder for removed/disabled collision
//   callback.
// =============================================================================

void collision_stub_empty(void) {
    return;
}

// =============================================================================
// Address: 0x800684ec | Name: collision_alloc_sub_object_buffers | Batch: 152
// Date: 2026-08-10
// Description: Allocates per-sub-object vertex buffer array at entity[0x6C].
//   Size = bone_count * 0x3C, aligned to 16 bytes. Zeroes memory. Marks dirty.
// =============================================================================

int collision_alloc_sub_object_buffers(int* entity) {
    int model = entity[1];
    int bone_count = *(int*)model;
    u32 size = (bone_count * 0x3c + 0xf) & 0xfffffff0;  // align 16

    int buf = mem_advance_secondary(size);
    entity[0x6c] = buf;

    if (entity[0x6c] != 0) {
        memset_wrapper(entity[0x6c], 0, size);
    }

    *(u32*)(*entity + 0x18) |= 4;  // mark dirty
    return entity[0x6c];
}

// =============================================================================
// Address: 0x80068ec0 | Name: collision_count_total_vertices | Batch: 152
// Date: 2026-08-10
// Description: Counts total vertices across all strips in a strip table. Per
//   strip: vertex count at byte +2. If strip continues (byte +0xB >= 0), aligns
//   count to 4. Iterates stride-8 entries until terminator (byte +3 < 0).
// =============================================================================

int collision_count_total_vertices(int strip_table) {
    int total = 0;

    if (strip_table == 0) return 0;

    while (*(char*)(strip_table + 3) >= 0) {
        u32 vert_count;
        if (*(char*)(strip_table + 0xb) < 0) {
            // Last strip in chain: exact count
            vert_count = (u32)*(u8*)(strip_table + 2);
        } else {
            // Continuing strip: align count to 4
            vert_count = (*(u8*)(strip_table + 2) + 3) & 0xfffffffc;
        }
        total += vert_count;
        strip_table += 8;
    }
    return total;
}

// =============================================================================
// Address: 0x80068568 | Name: collision_alloc_vertex_buffers | Batch: 152
// Date: 2026-08-10
// Description: Allocates per-vertex data buffers by flag mask for a sub-object.
//   Flags: 0x01=positions (12B/vert), 0x02=normals (8B/vert), 0x04=weights
//   (4B/vert), 0x08=strips (16B/strip), 0x10=UVs (16B/vert), 0x20=indices
//   (4B/vert +4, aligned). Lazy-inits entity[0x6C] array first.
// =============================================================================

void collision_alloc_vertex_buffers(u32 unused1, u32 unused2, int sub_obj_index, u32 alloc_flags) {
    // Entity/bone context from perf counter stack
    int* entity = /* from context */;
    int bone_index = /* from context */;

    if (entity == NULL) return;

    int* model_data = (int*)entity[1];
    int bone_count = *model_data;
    int strip_table = model_data[bone_count * -0x30 + bone_index * 0x30 + sub_obj_index + 0x15];

    if (entity[0x6c] == 0) {
        if (collision_alloc_sub_object_buffers(entity) == 0) return;
    }

    if (strip_table == 0) return;

    // Count strips
    int strip_count = 0;
    if (*(char*)(strip_table + 3) >= 0) {
        strip_count = 1;
        int ptr = strip_table;
        while (*(char*)(ptr + 0xb) >= 0) {
            strip_count++;
            ptr += 8;
        }
    }

    int vert_count = collision_count_total_vertices(strip_table);
    if (vert_count == 0) return;

    int slot_offset = (sub_obj_index + bone_index * 3) * 0x14;

    if (alloc_flags & 0x01) {  // positions: 12 bytes/vert
        int buf = mem_advance_secondary((vert_count * 12 + 0xf) & 0xfffffff0);
        *(int*)(entity[0x6c] + slot_offset + 4) = buf;
    }
    if (alloc_flags & 0x04) {  // weights: 4 bytes/vert
        int buf = mem_advance_secondary((vert_count * 4 + 0x1f) & 0xffffffe0);
        *(int*)(entity[0x6c] + slot_offset + 0x10) = buf;
    }
    if (alloc_flags & 0x02) {  // normals: 8 bytes/vert
        int buf = mem_advance_secondary((vert_count * 8 + 0xf) & 0xfffffff0);
        *(int*)(entity[0x6c] + slot_offset + 8) = buf;
    }
    if (alloc_flags & 0x08) {  // strips: 16 bytes/strip
        int buf = mem_advance_secondary(strip_count * 16);
        *(int*)(entity[0x6c] + slot_offset + 0) = buf;
    }
    if (alloc_flags & 0x10) {  // UVs: 16 bytes/vert
        int buf = mem_advance_secondary(vert_count * 16);
        *(int*)(entity[0x6c] + slot_offset + 0xc) = buf;
    }
    if (alloc_flags & 0x20) {  // indices: 4 bytes/vert + header
        int buf = mem_advance_secondary(((vert_count + 1) * 4 + 0xf) & 0xfffffff0);
        *(int*)(entity[0x6c] + slot_offset + 8) = buf;
        *(int*)(entity[0x6c] + slot_offset + 8) += 2;  // skip 2-byte header
    }
}

// =============================================================================
// Address: 0x8006872c | Name: collision_get_mesh_stats | Batch: 152
// Date: 2026-08-10
// Description: Queries collision mesh stats for a sub-object: strip count,
//   face/strip count (same value), and total vertex count. Output params can
//   be NULL to skip.
// =============================================================================

void collision_get_mesh_stats(
    u32 unused1, u32 unused2, int sub_obj_index, u32 query_type,
    int* out_strip_count, int* out_face_count, int* out_vertex_count
) {
    // Entity context from perf counter
    int entity_ptr = /* from context */;
    if (entity_ptr == 0) return;

    int* model_data = *(int**)(entity_ptr + 4);
    int bone_count = *model_data;
    int bone_index = /* from context */;
    int strip_table = model_data[bone_count * -0x30 + bone_index * 0x30 + sub_obj_index + 0x15];

    int local_strip_count = 0;
    if (out_strip_count == NULL) out_strip_count = &local_strip_count;
    *out_strip_count = 0;
    if (out_face_count != NULL) *out_face_count = 0;
    if (out_vertex_count != NULL) *out_vertex_count = 0;

    if (strip_table == 0) return;

    // Count strips
    if (*(char*)(strip_table + 3) >= 0) {
        *out_strip_count += 1;
        int ptr = strip_table;
        while (*(char*)(ptr + 0xb) >= 0) {
            *out_strip_count += 1;
            ptr += 8;
        }
    }

    if (out_face_count != NULL) {
        *out_face_count = *out_strip_count;
    }

    if (out_vertex_count != NULL) {
        *out_vertex_count = collision_count_total_vertices(strip_table);
    }
}
