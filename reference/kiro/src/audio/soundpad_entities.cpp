// =============================================================================
// soundpad_entities.cpp - Sound entity spatial queries and management
// TimeSplitters: Future Perfect (GameCube) decompilation
// Batch 144 | Date: 2026-08-10
// =============================================================================

#include "soundpad.h"

// Forward declarations
extern void FUN_803826e0(void);
extern void FUN_8038272c(void);
extern void FUN_803826e4(void);  // returns pair: float* position, int* zone_list
extern void FUN_80382730(void);
extern int FUN_8013f2a8(double threshold, int entity_ptr, float* position);

extern void soundpad_direct_path_list_reset(void);
extern int soundpad_path_propagate_direct(void);
extern int soundpad_resolve_entity_to_zone(int zone_id);

// Globals
extern int   DAT_80611b8c;   // Entity array base pointer
extern int   DAT_80611b90;   // Node info array base
extern int   DAT_80611b98;   // Entity pool base (for registration)
extern float FLOAT_80611ba0; // Last computed path distance
extern int   DAT_80611bb4;   // Propagation state
extern int   DAT_80611bc0;   // Path list prev pointer
extern int   DAT_80611bc4;   // Path list current pointer
extern int   DAT_80611c14;   // Total active entity count
extern int   DAT_80611c1c;   // Entity pointer lookup table
extern float FLOAT_80613f58; // Minimum search distance threshold
extern float FLOAT_80613f88; // Distance squared bias
extern float FLOAT_80613f98; // Maximum distance (FLT_MAX-like sentinel)
extern float FLOAT_80613f9c; // Occlusion max distance squared
extern float FLOAT_80613fa0; // Occlusion angle threshold

extern char DAT_8053b468;    // Path node array base
// Path node struct (0x10 bytes per entry):
//   +0x00: short next_index
//   +0x02: short prev_index  (at DAT_8053b46a offset)
//   +0x06: short path_id     (at DAT_8053b46e offset)
//   +0x08: float distance    (at DAT_8053b470 offset)
//   +0x0C: float weight      (at psVar6 + 6)

// =============================================================================
// Address: 0x8006f3c0 | soundpad_find_path_to_listener
// Finds the shortest sound propagation path from source to listener.
// Populates path output array with node indices.
// Stores final distance in FLOAT_80611ba0.
// =============================================================================
void soundpad_find_path_to_listener(int param_1, int param_2, short* path_out, int max_path_len)
{
    int prev_node;
    int current_node;
    int propagate_result;
    int trace_node;
    short* node_ptr;

    FUN_803826e0();
    node_ptr = NULL;
    soundpad_direct_path_list_reset();
    // DAT_80611bb4 = extraout_r4;  // propagation state set by reset

    propagate_result = soundpad_path_propagate_direct();
    trace_node = -1;

    while (propagate_result < 0) {
        current_node = DAT_80611bc4;
        prev_node = DAT_80611bc0;

        node_ptr = (short*)(&DAT_8053b468 + DAT_80611bc4 * 0x10);
        DAT_80611bc0 = DAT_80611bc4;
        DAT_80611bc4 = (int)*node_ptr;
        *node_ptr = (short)prev_node;

        propagate_result = soundpad_path_propagate_direct();
        trace_node = current_node;
    }

    // Store first path node
    bool has_space = (0 < max_path_len);
    if (has_space) {
        *path_out = (short)propagate_result;
        max_path_len--;
    }

    // Trace back through path nodes
    int offset = (unsigned int)has_space << 1;
    while (trace_node >= 0 && max_path_len > 0) {
        *(short*)((int)path_out + offset) = *(short*)(&DAT_8053b468 + 6 + trace_node * 0x10);
        offset += 2;
        trace_node = (int)*(short*)(&DAT_8053b468 + 2 + trace_node * 0x10);
        max_path_len--;
    }

    // Store final path distance
    if (node_ptr == NULL) {
        FLOAT_80611ba0 = *(float*)(DAT_80611b90 + *path_out * 0x14 + 0x10);
    } else {
        FLOAT_80611ba0 = *(float*)(node_ptr + 6);
    }

    FUN_8038272c();
}

// =============================================================================
// Address: 0x8006f4ec | soundpad_find_path_with_distance_limit
// Variant of find_path_to_listener with a max distance cutoff.
// Aborts path search if a node exceeds the distance threshold.
// =============================================================================
void soundpad_find_path_with_distance_limit(int param_1, int param_2, short* path_out, int max_path_len)
{
    int prev_node;
    int current_node;
    int propagate_result;
    int trace_node;
    short* node_ptr;

    double max_distance = (double)FUN_803826e0();
    node_ptr = NULL;
    soundpad_direct_path_list_reset();

    propagate_result = soundpad_path_propagate_direct();
    trace_node = -1;

    while (propagate_result < 0) {
        current_node = DAT_80611bc4;
        prev_node = DAT_80611bc0;

        int node_offset = DAT_80611bc4 * 0x10;
        node_ptr = (short*)(&DAT_8053b468 + node_offset);
        DAT_80611bc0 = DAT_80611bc4;
        DAT_80611bc4 = (int)*node_ptr;
        *node_ptr = (short)prev_node;

        // Distance limit check
        if (max_distance < (double)*(float*)(&DAT_8053b468 + 8 + node_offset)) {
            goto cleanup;
        }

        propagate_result = soundpad_path_propagate_direct();
        trace_node = current_node;
    }

    {
        bool has_space = (0 < max_path_len);
        if (has_space) {
            *path_out = (short)propagate_result;
            max_path_len--;
        }

        int offset = (unsigned int)has_space << 1;
        while (trace_node >= 0 && max_path_len > 0) {
            *(short*)((int)path_out + offset) = *(short*)(&DAT_8053b468 + 6 + trace_node * 0x10);
            offset += 2;
            trace_node = (int)*(short*)(&DAT_8053b468 + 2 + trace_node * 0x10);
            max_path_len--;
        }

        if (node_ptr == NULL) {
            FLOAT_80611ba0 = *(float*)(DAT_80611b90 + *path_out * 0x14 + 0x10);
        } else {
            FLOAT_80611ba0 = *(float*)(node_ptr + 6);
        }
    }

cleanup:
    FUN_8038272c();
}

// =============================================================================
// Address: 0x8006f63c | soundpad_find_nearest_entity
// Finds the nearest active sound entity beyond a minimum distance threshold.
// Returns entity index or -1 if none found.
// =============================================================================
int soundpad_find_nearest_entity(double search_radius, float* position)
{
    float dx, dy, dz;
    double dist_sq;
    double min_distance = FLOAT_80613f58;
    int nearest_idx = -1;
    double best_dist_sq = (double)FLOAT_80613f98;

    if (min_distance < search_radius) {
        min_distance = (double)(float)(search_radius * search_radius + (double)FLOAT_80613f88);
    }

    int entity_count = DAT_80611c14;
    int offset = 0;
    for (int i = 0; i < entity_count; i++) {
        int entity_ptr = DAT_80611b8c + offset;

        if ((*(unsigned int*)(entity_ptr + 4) & 1) != 0) {
            dx = *(float*)(entity_ptr + 0xc) - position[0];
            dy = *(float*)(entity_ptr + 0x10) - position[1];
            dz = *(float*)(entity_ptr + 0x14) - position[2];
            dist_sq = (double)(dz * dz + dx * dx + dy * dy);

            if (dist_sq < best_dist_sq && min_distance <= dist_sq) {
                nearest_idx = i;
                best_dist_sq = dist_sq;
            }
        }

        offset += 0x20;
        entity_count--;  // counted down in original
    }

    return nearest_idx;
}

// =============================================================================
// Address: 0x8006f6f8 | soundpad_find_entities_in_radius
// Finds all active sound entities within a given radius from a position.
// Returns count of matched entities; indices stored in output array.
// =============================================================================
int soundpad_find_entities_in_radius(double radius, float* position, int max_results, int* out_indices)
{
    float dx, dy, dz;
    int count = 0;
    int out_offset = 0;
    int entity_offset = 0;

    for (int i = 0; ; i++) {
        if (DAT_80611c14 <= i) {
            return count;
        }

        int entity_ptr = DAT_80611b8c + entity_offset;

        if ((*(unsigned int*)(entity_ptr + 4) & 1) != 0) {
            dx = *(float*)(entity_ptr + 0xc) - position[0];
            dy = *(float*)(entity_ptr + 0x10) - position[1];
            dz = *(float*)(entity_ptr + 0x14) - position[2];

            if (dz * dz + dx * dx + dy * dy < (float)(radius * radius)) {
                count++;
                *(int*)((char*)out_indices + out_offset) = i;
                out_offset += 4;
                if (max_results <= count) {
                    return count;
                }
            }
        }

        entity_offset += 0x20;
    }
}

// =============================================================================
// Address: 0x8006f79c | soundpad_check_entity_occlusion
// Checks sound entity occlusion/visibility against a list of zone IDs.
// Updates entity flags (0x40000) if occluded within threshold.
// =============================================================================
void soundpad_check_entity_occlusion(int param_1, int param_2, int zone_count)
{
    float dx, dy, dz;
    float* listener_pos;
    int* zone_list;
    double best_dist_sq;

    // FUN_803826e4 returns listener position and zone list
    // listener_pos = high 32 bits, zone_list = low 32 bits (register pair)
    FUN_803826e4();
    // Approximation of the paired return:
    // listener_pos = ...; zone_list = ...;

    best_dist_sq = (double)FLOAT_80613f98;
    int entity_offset = 0;

    for (int i = 0; i < DAT_80611c14; i++) {
        int entity_ptr = DAT_80611b8c + entity_offset;
        bool in_zone = false;

        if ((*(unsigned int*)(entity_ptr + 4) & 1) != 0) {
            // Check if entity's zone matches any in the zone list
            int list_offset = 0;
            for (int j = 0; j < zone_count; j++) {
                if ((int)*(short*)(entity_ptr + 10) == *(int*)((int)zone_list + list_offset)) {
                    in_zone = true;
                    break;
                }
                list_offset += 4;
            }

            if (in_zone) {
                dx = *(float*)(entity_ptr + 0xc) - listener_pos[0];
                dy = *(float*)(entity_ptr + 0x10) - listener_pos[1];
                dz = *(float*)(entity_ptr + 0x14) - listener_pos[2];
                double dist_sq = (double)(dz * dz + dx * dx + dy * dy);

                if ((*(unsigned int*)(entity_ptr + 4) & 0x30000) == 0) {
                    // Normal entity: track nearest
                    if (dist_sq < best_dist_sq) {
                        best_dist_sq = dist_sq;
                    }
                } else {
                    // Occluder entity: check line of sight
                    if (dist_sq < (double)FLOAT_80613f9c) {
                        if (FUN_8013f2a8((double)FLOAT_80613fa0, entity_ptr, listener_pos)) {
                            // Mark as occluded
                            *(unsigned int*)(DAT_80611b8c + entity_offset + 4) |= 0x40000;
                            goto done;
                        }
                    }
                }
            }
        }

        entity_offset += 0x20;
    }

done:
    FUN_80382730();
}

// =============================================================================
// Address: 0x8006f8ec | soundpad_find_nearest_entity_in_zone
// Finds the nearest active sound entity in a specific zone.
// Filters by zone_id, then by distance beyond minimum threshold.
// Returns entity index or -1.
// =============================================================================
int soundpad_find_nearest_entity_in_zone(double search_radius, float* position, int zone_id)
{
    float dx, dy, dz;
    double dist_sq;
    double min_distance = (double)FLOAT_80613f58;
    int nearest_idx = -1;
    double best_dist_sq = (double)FLOAT_80613f98;

    if (min_distance < search_radius) {
        min_distance = (double)(float)(search_radius * search_radius + (double)FLOAT_80613f88);
    }

    int entity_count = DAT_80611c14;
    int offset = 0;

    if (0 < DAT_80611c14) {
        for (int i = 0; entity_count != 0; i++) {
            int entity_ptr = DAT_80611b8c + offset;

            if (zone_id == *(short*)(entity_ptr + 10) &&
                (*(unsigned int*)(entity_ptr + 4) & 1) != 0) {
                dx = *(float*)(entity_ptr + 0xc) - position[0];
                dy = *(float*)(entity_ptr + 0x10) - position[1];
                dz = *(float*)(entity_ptr + 0x14) - position[2];
                dist_sq = (double)(dz * dz + dx * dx + dy * dy);

                if (min_distance <= dist_sq && dist_sq < best_dist_sq) {
                    nearest_idx = i;
                    best_dist_sq = dist_sq;
                }
            }

            offset += 0x20;
            entity_count--;
        }
    }

    return nearest_idx;
}

// =============================================================================
// Address: 0x8006f9b0 | soundpad_find_closest_from_list
// Finds the closest entity from a pre-filtered list of entity indices.
// Computes squared distance to each, returns the nearest index.
// =============================================================================
int soundpad_find_closest_from_list(float* position, int list_count, int* index_list)
{
    float dx, dy, dz, dist_sq;
    int closest_idx = 0;
    float best_dist = FLOAT_80613f98;
    int list_offset = 0;

    if (0 < list_count) {
        do {
            int entity_ptr = DAT_80611b8c + *(int*)((char*)index_list + list_offset) * 0x20;
            dx = *(float*)(entity_ptr + 0xc) - position[0];
            dy = *(float*)(entity_ptr + 0x10) - position[1];
            dz = *(float*)(entity_ptr + 0x14) - position[2];
            dist_sq = dz * dz + dx * dx + dy * dy;

            if (dist_sq < best_dist) {
                closest_idx = *(int*)((char*)index_list + list_offset);
                best_dist = dist_sq;
            }

            list_offset += 4;
            list_count--;
        } while (list_count != 0);
    }

    return closest_idx;
}

// =============================================================================
// Address: 0x8006fa30 | soundpad_get_entity_position
// Retrieves the 3D position of a sound entity by its index.
// Copies XYZ from internal entity array to output vector.
// =============================================================================
void soundpad_get_entity_position(int entity_index, float* out_position)
{
    int entity_ptr = *(int*)(DAT_80611c1c + entity_index * 4);
    out_position[0] = *(float*)(entity_ptr + 0xc);
    out_position[1] = *(float*)(entity_ptr + 0x10);
    out_position[2] = *(float*)(entity_ptr + 0x14);
}

// =============================================================================
// Address: 0x8006fa58 | soundpad_get_entity_data
// Gets a pointer to entity extended data and returns entity type.
// out_data receives pointer to entity data at offset 0x18.
// Returns the entity type value.
// =============================================================================
int soundpad_get_entity_data(int entity_index, int* out_data)
{
    int* entity_ptr = *(int**)(DAT_80611c1c + entity_index * 4);
    *out_data = (int)(entity_ptr + 6);  // offset 0x18
    return *entity_ptr;                  // entity type at offset 0
}

// =============================================================================
// Address: 0x8006fa74 | soundpad_register_entity
// Registers a new sound entity in the entity pool.
// Finds first free slot (max 20), sets position, flags, and zone.
// If zone_id is -1, resolves zone via soundpad_find_nearest_entity.
// Returns entity handle or -1 if pool full.
// =============================================================================
int soundpad_register_entity(float* position, int zone_id, unsigned int flags)
{
    bool found_slot = false;
    int slot_offset = 0;
    int slot_index = 0;
    int* slot_ptr = NULL;

    for (int remaining = 0x14; remaining > 0; remaining--) {
        slot_ptr = (int*)(DAT_80611b98 + slot_offset);
        if (slot_ptr == NULL || (slot_ptr[1] & 1) == 0) {
            found_slot = true;
            break;
        }
        slot_index++;
        slot_offset += 0x20;
    }

    if (found_slot) {
        slot_ptr[1] = flags | 0x400001;
        slot_ptr[3] = *(int*)&position[0];
        slot_ptr[4] = *(int*)&position[1];
        slot_ptr[5] = *(int*)&position[2];
        *slot_ptr = 0xfffffffe;  // type = unresolved

        if (zone_id == -1) {
            int nearest = soundpad_find_nearest_entity((double)FLOAT_80613f58, (float*)(slot_ptr + 3));
            *(short*)(slot_ptr + 2) = (short)*(int*)(DAT_80611b8c + nearest * 0x20);
        } else {
            *(short*)(slot_ptr + 2) = (short)zone_id;
        }

        int zone_data = soundpad_resolve_entity_to_zone((int)*(short*)(slot_ptr + 2));
        slot_index += 0x209e;  // handle offset (entity pool handle base)
        slot_ptr[6] = *(int*)(zone_data + 0x18);
        slot_ptr[7] = 0;
    } else {
        slot_index = -1;
    }

    return slot_index;
}

// =============================================================================
// 0x8006fb64 - soundpad_deactivate_entity
// Deactivates a registered sound pad entity by handle. Clears the ACTIVE bit
// (bit 0) in the entity's flags field (offset +4). The handle is offset by
// 0x209E (entity pool base), so the actual slot index = (handle - 0x209E).
// Each slot is 0x20 bytes in the zone lookup table (DAT_80611b98).
// Called by: FUN_80093eac, FUN_80094888 (entity cleanup paths)
// =============================================================================
void soundpad_deactivate_entity(int handle) {
    int slot_offset = (handle - 0x209E) * 0x20;
    int entry_ptr = g_soundpad_zone_table + slot_offset;  // DAT_80611b98
    *(u32*)(entry_ptr + 4) &= ~0x1u;  // clear ACTIVE bit
}
