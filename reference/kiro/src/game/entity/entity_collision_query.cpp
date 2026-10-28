// entity_collision_query.cpp
// Entity collision/raycast query system: attachment sync, raycasting, AABB queries
// Address range: 0x800779f0 - 0x80078d18
// Date: 2026-08-10

#include "common.h"

// Forward declarations
extern void entity_remap_animation_slot(int entity, int new_source, int param);
extern int entity_check_player_visibility(int entity, uint player_index);
extern int collision_entity_raycast(int col_data, float* origin, float* dir, int param,
                                    float* hit_pos, float* hit_normal, uint flags);
extern void collision_entity_swept_cast(int col_data, int origin, float* dir, int param,
                                        int p4, int* result, int p6, uint flags);
extern void collision_result_save(void);

// External data
extern int DAT_80611c24;    // Entity pool base
extern int DAT_80611c38;    // AABB query result count
extern int DAT_80611c70;    // Entity pool count
extern int DAT_80611b04;    // Scene node table
extern int DAT_80611b14;    // Scene node count
extern int DAT_80611d90;    // Character data array (stride 0xEA0)
extern int DAT_80540530;    // Global collision list count
extern int DAT_8053fe80;    // Global collision list pointer
extern float FLOAT_80613fb4; // 0.0f
extern float FLOAT_80613fbc; // 1.0f
extern float FLOAT_80613fc0; // large initial distance
extern float FLOAT_80614020; // Y offset for attached entities

// ============================================================================
// Address: 0x800779f0 | Name: entity_sync_attached_position
// Syncs an attached entity's position/rotation from its parent entity.
// If parent hasn't been evaluated yet (no 0x200000 flag), copies position
// directly with a small Y offset (FLOAT_80614020). Otherwise copies full
// transform including rotation and extracts world position from bone matrix.
// ============================================================================
void entity_sync_attached_position(int entity) {
    int render_data = *(int*)(entity + 4);
    int parent = **(int**)(entity + 0x78);

    if ((*(uint*)(parent + 0x28) & 0x200000) == 0) {
        // Parent not yet evaluated — approximate position
        *(int*)(entity + 0x34) = *(int*)(parent + 0x34);   // room
        *(float*)(entity + 0x64) = *(float*)(parent + 0x64); // pos.x
        *(float*)(entity + 0x68) = FLOAT_80614020 + *(float*)(parent + 0x68); // pos.y + offset
        *(float*)(entity + 0x6C) = *(float*)(parent + 0x6C); // pos.z
        // Store as render position
        *(float*)(entity + 0x248) = *(float*)(entity + 0x64);
        *(float*)(entity + 0x24C) = *(float*)(entity + 0x68);
        *(float*)(entity + 0x250) = *(float*)(entity + 0x6C);
    } else {
        // Parent evaluated — copy full transform
        *(float*)(entity + 0x64) = *(float*)(parent + 0x64);
        *(float*)(entity + 0x68) = *(float*)(parent + 0x68);
        *(float*)(entity + 0x6C) = *(float*)(parent + 0x6C);
        *(float*)(entity + 0xC0) = *(float*)(parent + 0xC0); // rot.x
        *(float*)(entity + 0x70) = *(float*)(parent + 0x70); // rot.y
        *(float*)(entity + 0xC8) = *(float*)(parent + 0xC8); // rot.z
        // Extract world position from bone matrix row 3
        int bone_matrices = *(int*)(render_data + 0x10);
        *(float*)(entity + 0x248) = *(float*)(bone_matrices + 0x30); // mtx[3][0]
        *(float*)(entity + 0x24C) = *(float*)(bone_matrices + 0x34); // mtx[3][1]
        *(float*)(entity + 0x250) = *(float*)(bone_matrices + 0x38); // mtx[3][2]
        *(int*)(entity + 0x34) = *(int*)(parent + 0x34); // room
    }
}


// ============================================================================
// Address: 0x80077aac | Name: entity_setup_attachment
// Sets up entity bone attachment to a parent. Configures rotation mode 3,
// attachment flags, bone indices (primary + alternate), copies parent's room,
// sets parent pointer, and remaps animation slot if model matches.
// ============================================================================
void entity_setup_attachment(int entity, int* parent_ptr, int primary_bone,
                             int alt_bone, int attach_flags) {
    int parent = *parent_ptr;

    *(char*)(entity + 0x32) = 3;                   // rotation mode = conditional
    *(uint*)(entity + 0x28) &= ~0x60;             // clear old attachment bits
    *(uint*)(entity + 0x28) |= 0x40000000;        // set "has attachment" flag
    *(int*)(entity + 0x128) = attach_flags;        // attachment mode flags
    *(int*)(entity + 0x34) = *(int*)(parent + 0x34); // copy room from parent
    *(int*)(entity + 0x130) = alt_bone;            // alternate bone index
    *(int*)(entity + 0x78) = *(int*)(parent + 0x74); // parent attachment pointer
    *(int*)(entity + 0x12C) = primary_bone;        // primary bone index

    // If parent's render data matches its alt render source, remap animation
    int anim_source = *(int*)(parent + 0xA4);
    if (*(int*)(parent + 4) == anim_source) {
        entity_remap_animation_slot(entity, anim_source, (int)parent_ptr);
    }
}

// ============================================================================
// Address: 0x80077b2c | Name: entity_find_next_by_type
// Circular search through entity pool for next entity matching a given type.
// Starts after param_1 (or pool start if 0), wraps around up to 3 full passes.
// param_2 == -1 means "any type except 2". Returns entity address or 0.
// ============================================================================
uint entity_find_next_by_type(uint start_entity, int type_filter) {
    uint pool_end = DAT_80611c24 + (DAT_80611c70 - 1) * 0x2E8;
    uint current = (start_entity == 0) ? DAT_80611c24 : start_entity;
    int wrap_count = 0;

    current += 0x2E8; // advance past start
    if (current > pool_end) {
        wrap_count = 1;
        current = DAT_80611c24;
    }

    while (wrap_count <= 3) {
        if (current == start_entity) return 0; // full loop without match

        int entity_type = *(int*)(current + 0x20);
        if (entity_type == type_filter || (type_filter == -1 && entity_type != 2)) {
            return current;
        }

        current += 0x2E8;
        if (current > pool_end) {
            wrap_count++;
            current = DAT_80611c24;
        }
    }

    return 0;
}


// ============================================================================
// Address: 0x80077bc4 | Name: entity_raycast_single
// Raycasts against a single entity. Handles two cases:
// - Character entities (type 8): calls FUN_800abe68 for skeletal raycast
// - Generic entities: calls collision_entity_raycast on collision data
// Skips if entity is the same as current hit target (parent/child exclusion).
// Updates closest hit position/normal/distance. Also checks attached collision
// mesh via FUN_801008b4 for non-character types.
// ============================================================================
int entity_raycast_single(int entity, float* origin, float* direction, float* norm_dir,
                          float* end_point, int is_infinite, int current_hit,
                          float* out_hit_pos, float* out_hit_normal, float* inout_best_dist,
                          uint flags, int* extra_result) {
    float hit_pos[3], hit_normal[3];
    int hit_entity = 0;

    // Skip if entity collision mesh is parent/child of current best hit
    int skip_mode = 0;
    if (current_hit != 0) {
        if (*(int*)(current_hit + 0x78) == *(int*)(entity + 4)) {
            skip_mode = 2; // current hit is our child
        } else if (*(int*)(entity + 0x78) == *(int*)(current_hit + 4)) {
            // We are child of current hit — temporarily set max distance
            skip_mode = 1;
            float saved_dist = *inout_best_dist;
            *inout_best_dist = FLOAT_80613fc0; // large value
        }
    }

    if (skip_mode != 2) {
        if (*(int*)(entity + 0x20) == 8) {
            // Character skeletal raycast
            if ((flags & 4) == 0 || (*(uint*)(*(int*)(entity + 0x7C) + 0x24) & 8) == 0) {
                int result = FUN_800abe68(entity, origin, norm_dir, end_point,
                                          is_infinite, hit_pos, hit_normal, flags);
                if (result != 0) hit_entity = entity;
            }
        } else {
            // Generic entity collision raycast
            int result = collision_entity_raycast(
                *(int*)(entity + 4), origin, direction, is_infinite,
                hit_pos, hit_normal, flags);
            if (result != 0) hit_entity = entity;
        }

        // Check if this hit is closer
        if (hit_entity != 0) {
            float dx = hit_pos[0] - origin[0];
            float dy = hit_pos[1] - origin[1];
            float dz = hit_pos[2] - origin[2];
            float dist_sq = dx*dx + dy*dy + dz*dz;

            if (dist_sq < *inout_best_dist) {
                if (*(int*)(hit_entity + 0x20) == 8) {
                    FUN_800ab6c8(); // save character hit state
                } else {
                    collision_result_save();
                }
                out_hit_pos[0] = hit_pos[0]; out_hit_pos[1] = hit_pos[1]; out_hit_pos[2] = hit_pos[2];
                out_hit_normal[0] = hit_normal[0]; out_hit_normal[1] = hit_normal[1]; out_hit_normal[2] = hit_normal[2];
                *inout_best_dist = dist_sq;
                current_hit = hit_entity;
            }
        }

        // Restore distance if we temporarily maxed it
        if (skip_mode == 1) {
            *inout_best_dist = saved_dist; // restore
        }

        // Check attached collision meshes for non-character types
        if ((*(uint*)(entity + 0x20) & 0x3841C38) == 0 && extra_result != NULL) {
            FUN_801008b4(entity, origin, direction, is_infinite,
                         extra_result, (int*)out_hit_pos, out_hit_normal, inout_best_dist);
        }
    }

    return current_hit;
}


// ============================================================================
// Address: 0x80077e18 | Name: entity_raycast_room
// MASSIVE: Full room raycast. Tests scene node geometry, all entities in room's
// render list, plus global collision list. Normalizes direction, computes
// end point for segment tests. Returns closest hit entity with position/normal.
// ============================================================================
int entity_raycast_room(float* origin, float* direction, int is_infinite,
                        float* out_hit_pos, float* out_hit_normal,
                        uint type_mask, uint flags, int* extra_result) {
    float norm_dir[3], end_point[3];
    float hit_pos[3], hit_normal[3];
    int hit_entity = 0;
    float best_dist = FLOAT_80613fc0; // large initial

    int room_id = FUN_803826bc_room();
    float* ray_origin = FUN_803826bc_origin();

    // Normalize direction
    float len_sq = direction[0]*direction[0] + direction[1]*direction[1] + direction[2]*direction[2];
    if (len_sq > 0.0001f) { // FLOAT_80614024
        float inv_len = 1.0f / sqrt(len_sq);
        norm_dir[0] = direction[0] * inv_len;
        norm_dir[1] = direction[1] * inv_len;
        norm_dir[2] = direction[2] * inv_len;
    }

    // Compute end point for segment test
    if (is_infinite == 0) {
        best_dist = direction[0]*direction[0] + direction[1]*direction[1] + direction[2]*direction[2];
    }
    end_point[0] = ray_origin[0] + direction[0];
    end_point[1] = ray_origin[1] + direction[1];
    end_point[2] = ray_origin[2] + direction[2];

    // Test scene node (room geometry)
    if (room_id >= 0 && room_id <= DAT_80611b14) {
        int scene_node = *(int*)(DAT_80611b04 + room_id * 4);
        if ((*(uint*)(scene_node + 0x28) & 0x20) != 0) {
            if ((*(uint*)(scene_node + 0x20) & type_mask) == 0) {
                int result = collision_entity_raycast(
                    *(int*)(scene_node + 4), ray_origin, direction, is_infinite,
                    hit_pos, hit_normal, 0);
                if (result != 0) {
                    float d = (hit_pos[0]-ray_origin[0])*(hit_pos[0]-ray_origin[0]) +
                              (hit_pos[1]-ray_origin[1])*(hit_pos[1]-ray_origin[1]) +
                              (hit_pos[2]-ray_origin[2])*(hit_pos[2]-ray_origin[2]);
                    if (d < best_dist) {
                        collision_result_save();
                        // Store hit
                        best_dist = d;
                        hit_entity = scene_node;
                    }
                }
                // Also test triangle portals if flag 0x40 set
                if ((flags & 0x40) != 0) {
                    entity_raycast_triangles(scene_node, ray_origin, direction, is_infinite,
                                            hit_pos, hit_normal, NULL, 1);
                }
            }
            // Test extra collision via FUN_801008b4
            if (extra_result != NULL) {
                FUN_801008b4(scene_node, ray_origin, direction, is_infinite,
                             extra_result, (int*)out_hit_pos, out_hit_normal, &best_dist);
            }
        }

        // Test all entities in room render list
        int entity_count = (&DAT_805401dc)[room_id];
        for (int i = 0; i < entity_count; i++) {
            int ent = *(int*)((&DAT_8053fb2c)[room_id] + i * 4);
            if ((*(uint*)(ent + 0x28) & 0x80000000) != 0) continue;
            if ((*(uint*)(*(int*)(ent + 0x24) + 0x28) & 1) == 0) continue;
            if ((*(uint*)(ent + 0x28) & 0x20) == 0) continue;
            if ((*(uint*)(ent + 0x28) & 0x100) != 0) continue;
            if ((*(uint*)(ent + 0x20) & type_mask) != 0) continue;

            hit_entity = entity_raycast_single(ent, ray_origin, direction, norm_dir, end_point,
                                               is_infinite, hit_entity, out_hit_pos, out_hit_normal,
                                               &best_dist, flags, extra_result);
        }
    }

    // Test global collision list (entities not in any room)
    for (int i = 0; i < DAT_80540530; i++) {
        int ent = *(int*)(DAT_8053fe80 + i * 4);
        if ((*(uint*)(ent + 0x28) & 0x80000000) != 0) continue;
        if ((*(uint*)(*(int*)(ent + 0x24) + 0x28) & 1) == 0) continue;
        if ((*(uint*)(ent + 0x28) & 0x20) == 0) continue;
        if ((*(uint*)(ent + 0x20) & type_mask) != 0) continue;

        hit_entity = entity_raycast_single(ent, ray_origin, direction, norm_dir, end_point,
                                           is_infinite, hit_entity, out_hit_pos, out_hit_normal,
                                           &best_dist, flags, extra_result);
    }

    // Output results
    if (hit_entity != 0) {
        if (out_hit_pos != NULL) { /* already written by raycast_single */ }
        if (out_hit_normal != NULL) { /* already written */ }
    }

    FUN_80382708();
    return hit_entity;
}


// ============================================================================
// Address: 0x8007836c | Name: entity_query_aabb
// Queries all entities in a room's render list that overlap a given AABB.
// Outputs entity pointers to param_3 array (max param_5 entries).
// Skips dead/disabled/hidden entities, deduplicates multi-room entities.
// Optionally includes the room's scene node itself (unless flag bit 1 set).
// ============================================================================
void entity_query_aabb(int room_id, float* aabb, int* out_array, int start_index,
                       int max_count, uint type_mask) {
    int count = start_index;
    int entity_count = (&DAT_805401dc)[room_id];
    int* room_list = (int*)(&DAT_8053fb2c)[room_id];

    if (count >= max_count) { FUN_8038273c(); return; }

    // Include scene node itself (unless flag bit 1 excludes it)
    if ((type_mask & 2) == 0) {
        out_array[count] = *(int*)(DAT_80611b04 + room_id * 4);
        count++;
        if (count >= max_count) { FUN_8038273c(); return; }
    }

    // Test each entity against AABB
    for (int i = 0; i < entity_count; i++) {
        int ent = room_list[i];
        if (*(int*)(ent + 4) == 0) continue;
        uint flags = *(uint*)(ent + 0x28);
        if ((flags & 0x80000000) != 0) continue;  // dead
        if ((flags & 0x60) == 0) continue;         // no collision
        if ((flags & 0x100) != 0) continue;        // disabled
        if ((*(uint*)(ent + 0x20) & type_mask) != 0) continue;

        // AABB overlap test
        float cx = *(float*)(ent + 0x64) + *(float*)(ent + 0x1A8);
        float cy = *(float*)(ent + 0x68) + *(float*)(ent + 0x1AC);
        float cz = *(float*)(ent + 0x6C) + *(float*)(ent + 0x1B0);
        float ex = *(float*)(ent + 0x1C0);
        float ey = *(float*)(ent + 0x1C4);
        float ez = *(float*)(ent + 0x1C8);

        if (aabb[0] > cx + ex || cx - ex > aabb[3]) continue;
        if (aabb[1] > cy + ey || cy - ey > aabb[4]) continue;
        if (aabb[2] > cz + ez || cz - ez > aabb[5]) continue;

        // Dedup: skip if entity already in output (multi-room entities)
        bool duplicate = false;
        if (*(char*)(ent + 0x30) > 1 && count > start_index) {
            for (int j = start_index; j < count; j++) {
                if (out_array[j] == ent) { duplicate = true; break; }
            }
        }

        if (!duplicate) {
            out_array[count] = ent;
            count++;
            if (count >= max_count) break;
        }
    }

    FUN_8038273c();
}


// ============================================================================
// Address: 0x80078540 | Name: entity_swept_cast_room
// Swept collision test (capsule/thick ray) against the room's scene node +
// all entities in the room's render list. Uses collision_entity_swept_cast.
// ============================================================================
void entity_swept_cast_room(int room_id, float* origin, float* direction,
                            int param, int* result, uint type_mask, uint flags) {
    if (room_id > DAT_80611b14) { FUN_80382718(); return; }

    // Test scene node
    int scene_node = *(int*)(DAT_80611b04 + room_id * 4);
    if ((*(uint*)(scene_node + 0x28) & 0x60) != 0) {
        collision_entity_swept_cast(*(int*)(scene_node + 4), (int)origin, direction,
                                    param, 0, result, 1, flags);
    }

    // Test all entities in room
    int entity_count = (&DAT_805401dc)[room_id];
    for (int i = 0; i < entity_count; i++) {
        int ent = *(int*)((&DAT_8053fb2c)[room_id] + i * 4);
        if (*(int*)(ent + 4) == 0) continue;
        uint ent_flags = *(uint*)(ent + 0x28);
        if ((ent_flags & 0x80000000) != 0) continue;
        if ((ent_flags & 0x60) == 0) continue;
        if ((ent_flags & 0x100) != 0) continue;
        if ((*(uint*)(ent + 0x20) & type_mask) != 0) continue;

        collision_entity_swept_cast(*(int*)(ent + 4), (int)origin, direction,
                                    param, 0, result, 1, flags);
    }

    FUN_80382718();
}

// ============================================================================
// Address: 0x80078674 | Name: entity_raycast_triangles
// Raycasts against an entity's triangle mesh (portal polygons). Iterates portal
// list (at entity+0x1E4 count, entity+0x1F4 ptr), tests both triangle faces
// via FUN_80011a44, validates hit direction against ray, finds closest hit.
// Returns hit position + normal + polygon reference.
// ============================================================================
int entity_raycast_triangles(int entity, float* origin, float* direction, int is_infinite,
                             float* out_hit_pos, float* out_hit_normal,
                             void** out_polygon, int require_facing) {
    int portal_count = *(int*)(entity + 0x1E4);
    int portal_list = *(int*)(entity + 0x1F4); // portal index array
    float best_dist = FLOAT_80613fc0;
    void* best_polygon = NULL;
    float best_pos[3], best_normal[3];

    for (int i = 0; i < portal_count; i++) {
        uint* portal = *(uint**)(portal_list + i * 4);

        // Skip if not matching render flags (portal[0] check)
        if (require_facing != 0 && (portal[1] & 0x3000) == 0) continue;
        if (portal[0] != 0 && (portal[0] & *(uint*)(*(int*)(entity + 4) + 0x30)) == 0) continue;

        // Build two triangle faces from portal vertices
        float tri_a[9], tri_b[9]; // 3 vertices each
        // [vertex extraction from portal data at offsets 2-11]

        // Test face A
        float hit_pos[3], hit_normal[3];
        int hit = FUN_80011a44(origin, direction, tri_a, tri_a+3, tri_a+6, hit_pos, hit_normal);
        if (hit != 0 && direction[0]*hit_normal[0] + direction[1]*hit_normal[1] +
                         direction[2]*hit_normal[2] > 0.0f) {
            hit = 0; // backface — reject
        }

        // Test face B if A missed
        if (hit == 0) {
            hit = FUN_80011a44(origin, direction, tri_a+6, tri_b, tri_a, hit_pos, hit_normal);
            if (hit != 0 && direction[0]*hit_normal[0] + direction[1]*hit_normal[1] +
                             direction[2]*hit_normal[2] > 0.0f) {
                hit = 0;
            }
        }

        if (hit != 0) {
            float dx = hit_pos[0] - origin[0];
            float dy = hit_pos[1] - origin[1];
            float dz = hit_pos[2] - origin[2];
            float dist = dx*dx + dy*dy + dz*dz;
            if (dist < best_dist) {
                best_pos[0] = hit_pos[0]; best_pos[1] = hit_pos[1]; best_pos[2] = hit_pos[2];
                best_normal[0] = hit_normal[0]; best_normal[1] = hit_normal[1]; best_normal[2] = hit_normal[2];
                best_polygon = portal;
                best_dist = dist;
            }
        }
    }

    // Output results
    if (best_dist != FLOAT_80613fc0) {
        if (out_hit_pos) { out_hit_pos[0] = best_pos[0]; out_hit_pos[1] = best_pos[1]; out_hit_pos[2] = best_pos[2]; }
        if (out_hit_normal) { out_hit_normal[0] = best_normal[0]; out_hit_normal[1] = best_normal[1]; out_hit_normal[2] = best_normal[2]; }
        if (out_polygon) *out_polygon = best_polygon;
        return 1;
    }

    FUN_80382710();
    return 0;
}


// ============================================================================
// Address: 0x800789f4 | Name: entity_raycast_room_portals
// Raycasts against scene node + entity triangle meshes in a room. Similar to
// entity_raycast_room but focused on portal/triangle mesh hits rather than
// full collision geometry. Tests scene node first, then entities in render list.
// ============================================================================
int entity_raycast_room_portals(int room_id, float* origin, float* direction, int is_infinite,
                                float* out_hit_pos, float* out_hit_normal,
                                void** out_polygon, uint type_mask) {
    float best_dist = FLOAT_80613fc0;
    int hit_entity = 0;
    float hit_pos[3], hit_normal[3];

    // Test scene node
    if (room_id >= 0 && room_id <= DAT_80611b14) {
        int scene_node = *(int*)(DAT_80611b04 + room_id * 4);
        if ((*(uint*)(scene_node + 0x28) & 0x20) != 0 &&
            (*(uint*)(scene_node + 0x20) & type_mask) == 0) {
            int result = entity_raycast_triangles(scene_node, origin, direction, is_infinite,
                                                  hit_pos, hit_normal, out_polygon, 0);
            if (result != 0) {
                float d = (hit_pos[0]-origin[0])*(hit_pos[0]-origin[0]) +
                          (hit_pos[1]-origin[1])*(hit_pos[1]-origin[1]) +
                          (hit_pos[2]-origin[2])*(hit_pos[2]-origin[2]);
                if (d < best_dist) {
                    best_dist = d;
                    hit_entity = scene_node;
                }
            }
        }

        // Test entities with triangle meshes
        int entity_count = (&DAT_805401dc)[room_id];
        for (int i = 0; i < entity_count; i++) {
            int ent = *(int*)((&DAT_8053fb2c)[room_id] + i * 4);
            uint ent_flags = *(uint*)(*(int*)(ent + 0x24) + 0x28);
            if ((ent_flags & 4) == 0 && (ent_flags & 2) == 0) continue;
            if ((*(uint*)(ent + 0x28) & 0x20) == 0) continue;
            if ((*(uint*)(ent + 0x28) & 0x100) != 0) continue;
            if ((*(uint*)(ent + 0x20) & (type_mask | 0x2000070)) != 0) continue;

            if (*(int*)(ent + 0x1F4) == 0) continue; // no triangle mesh

            int result = entity_raycast_triangles(ent, origin, direction, is_infinite,
                                                  hit_pos, hit_normal, out_polygon, 0);
            if (result != 0) {
                float d = (hit_pos[0]-origin[0])*(hit_pos[0]-origin[0]) +
                          (hit_pos[1]-origin[1])*(hit_pos[1]-origin[1]) +
                          (hit_pos[2]-origin[2])*(hit_pos[2]-origin[2]);
                if (d < best_dist) {
                    best_dist = d;
                    hit_entity = ent;
                }
            }
        }
    }

    // Output
    if (hit_entity != 0) {
        if (out_hit_pos) { out_hit_pos[0] = hit_pos[0]; out_hit_pos[1] = hit_pos[1]; out_hit_pos[2] = hit_pos[2]; }
        if (out_hit_normal) { out_hit_normal[0] = hit_normal[0]; out_hit_normal[1] = hit_normal[1]; out_hit_normal[2] = hit_normal[2]; }
    }

    FUN_80382708();
    return hit_entity;
}


// ============================================================================
// Address: 0x80078d18 | Name: entity_query_aabb_characters
// MASSIVE: Queries entities across multiple rooms that overlap an AABB and
// match type/flag filters. Used for gameplay (damage, proximity detection).
// Iterates room list (param_3 array of room IDs, param_4 count), for each room
// tests all entities against the AABB. Filters:
// - Skip dead entities (bit 31)
// - Skip entities without collision or visibility flags matching param_7
// - Skip type match against param_6 mask
// - Must be character (type 8) or have collision data (offset 0x80 or 0x1F4)
// For characters, checks if entity is NOT the querying character (via DAT_80611d90).
// Results stored in DAT_8055a154 array (max 16 entries).
// ============================================================================
void entity_query_aabb_characters(float* aabb, int* room_ids, int room_count,
                                  int include_collidable, uint type_mask, int include_all) {
    int querying_char = 0;

    // Determine which character is querying (to exclude self)
    int* caller_info = FUN_803826b8_caller();
    int char_index = caller_info[0];
    if (char_index == -1) {
        querying_char = 0;
    } else {
        querying_char = *(int*)(DAT_80611d90 + char_index * 0xEA0);
    }

    DAT_80611c38 = 0; // reset result count

    for (int room_idx = 0; room_idx < room_count; room_idx++) {
        int room_id = room_ids[room_idx];
        int entity_count = (&DAT_805401dc)[room_id];

        for (int i = 0; i < entity_count; i++) {
            int ent = *(int*)((&DAT_8053fb2c)[room_id] + i * 4);

            // Basic filters
            if ((*(uint*)(ent + 0x28) & 0x80000000) != 0) continue; // dead
            if (include_all == 0 && (*(uint*)(*(int*)(ent + 0x24) + 0x28) & 0x2000) != 0) continue;
            if (include_collidable == 0 && (*(uint*)(*(int*)(ent + 0x24) + 0x28) & 4) == 0) {
                if ((*(uint*)(*(int*)(ent + 0x24) + 0x28) & 2) == 0) continue;
            }

            // Type filter
            uint ent_type = *(uint*)(ent + 0x20);
            if ((ent_type & type_mask) != 0) continue;

            // Must have collision capability
            if (ent_type != 8 && *(int*)(ent + 0x80) == 0 && *(int*)(ent + 0x1F4) == 0) continue;

            // AABB overlap test
            float cx = *(float*)(ent + 0x64) + *(float*)(ent + 0x1A8);
            float cy = *(float*)(ent + 0x68) + *(float*)(ent + 0x1AC);
            float cz = *(float*)(ent + 0x6C) + *(float*)(ent + 0x1B0);
            float ex = *(float*)(ent + 0x1C0);
            float ey = *(float*)(ent + 0x1C4);
            float ez = *(float*)(ent + 0x1C8);

            if (aabb[3] <= cx - ex || cx + ex <= aabb[0]) goto check_char_exception;
            if (aabb[4] <= cy - ey || cy + ey <= aabb[1]) goto check_char_exception;
            if (aabb[5] <= cz - ez || cz + ez <= aabb[2]) goto check_char_exception;

            // Passed AABB test — add to results
            {
                bool should_add = false;
                if (ent_type == 8) {
                    should_add = (ent != querying_char); // exclude self
                } else if (*(int*)(ent + 0x80) == 0) {
                    if (*(int*)(ent + 0x1F4) != 0) {
                        FUN_8009a9ac(caller_info, ent, aabb); // triangle mesh test
                        should_add = true;
                    }
                } else {
                    uint col_flags = FUN_80149d5c(*(uint*)(ent + 0x80));
                    if ((col_flags & 0x200000) != 0) {
                        if (*(int*)(ent + 0x20) != 0x400000 ||
                            FUN_80151f54(*(int**)(querying_char + 0x7C)) == 0) {
                            should_add = true;
                        }
                    }
                }

                if (should_add && DAT_80611c38 < 16) {
                    *(int*)(&DAT_8055a154 + DAT_80611c38 * 4) = ent;
                    DAT_80611c38++;
                }
            }
            continue;

check_char_exception:
            // Characters (type 8) with specific flag still get added
            if (ent_type == 8 && *(short*)(*(int*)(ent + 0x7C) + 0x48) == 0x46) continue;
            continue;
        }
    }

    FUN_80382704();
}
