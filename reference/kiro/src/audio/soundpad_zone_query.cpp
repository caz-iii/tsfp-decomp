// =============================================================================
// soundpad_zone_query.cpp - Sound pad zone lookup, entity collection, and
// spatial queries for the sound zone system.
// TimeSplitters: Future Perfect (GameCube) decompilation
// Batch 158 | Date: 2026-08-10
// =============================================================================

#include "soundpad.h"

// Forward declarations
extern int soundpad_resolve_entity_to_zone(int zone_id);
extern int* soundpad_resolve_zone_node(int node_id);
extern int soundpad_get_entity_data(int zone_id, int* out_data_ptr);
extern int level_get_info_cached(int level_id);
extern double FUN_80131538(double val);  // sqrt

// Globals
// DAT_80611b8c - Primary sound pad entity buffer (stride 0x20)
// DAT_80611bb0 - Recursion depth counter (max 10)
// DAT_80611c68 - Current reverb/zone state value
// DAT_80612614 - Current level ID
// DAT_803feea0 - Reverb level float table (20 entries)
// FLOAT_80613f58 - 0.0f
// FLOAT_80613f5c - 1.0f
// FLOAT_80613f98 - 0.0f (alternate)
// FLOAT_80613fa8 - Default reverb level (fallback)

// =============================================================================
// 0x8006fb84 - soundpad_get_entity_zone_id
// Resolves the zone ID for a given sound pad entry. If the entity has the
// INDIRECT or DYNAMIC flags set (bit 11 or bit 22 in flags at offset +4),
// resolves through soundpad_resolve_entity_to_zone and reads the resolved
// zone's ID at offset +8. Otherwise reads the zone ID directly from +8.
// Returns -1 if the entry pointer is NULL.
// =============================================================================
int soundpad_get_entity_zone_id(int pad_entry) {
    if (pad_entry == 0) {
        return -1;
    }

    u32 flags = *(u32*)(pad_entry + 4);
    if (((flags >> 11) & 1) == 0 && (flags & 0x400000) == 0) {
        // Direct zone ID
        return (int)*(s16*)(pad_entry + 8);
    } else {
        // Indirect: resolve through zone entity table
        int resolved = soundpad_resolve_entity_to_zone((int)*(s16*)(pad_entry + 8));
        return (int)*(s16*)(resolved + 8);
    }
}

// =============================================================================
// 0x8006fbd8 - soundpad_find_zone_for_entity
// Linear search through level zone definitions to find which zone contains
// a given entity ID. Each zone (stride 0x58) has a count at +4 and entity
// ID list starting at +8. Returns the zone index (0-based) or -1 if not found.
// =============================================================================
int soundpad_find_zone_for_entity(int entity_id) {
    int level_info = level_get_info_cached(g_current_level_id);
    int zone_count = *(int*)(level_info + 0x68);
    int zone_defs = *(int*)(level_info + 0x64);

    int zone_offset = 0;
    for (int i = 0; i < zone_count; i++) {
        int entity_count = *(int*)(zone_defs + zone_offset + 4);
        int entity_offset = 0;

        for (int j = 0; j < entity_count; j++) {
            if (entity_id == *(int*)(zone_defs + zone_offset + entity_offset + 8)) {
                return i;
            }
            entity_offset += 4;
        }
        zone_offset += 0x58;
    }
    return -1;
}

// =============================================================================
// 0x8006fc6c - soundpad_collect_zone_entities
// Recursively collects entity IDs from a zone definition into an output
// buffer. Walks the zone's entity list: positive IDs are direct entities,
// negative IDs indicate sub-zones (recursion with ID = -1 - value).
// Max recursion depth = 10 (tracked by g_zone_recursion_depth).
// Returns the number of entities collected.
// Called by: soundpad_compute_zone_bounds (0x8006fef8)
// =============================================================================
int soundpad_collect_zone_entities(int zone_index, int* out_buf, int max_count) {
    // Context push (paired-single register save)
    void* ctx = os_get_context_pair();  // FUN_803826e4

    int collected = 0;
    int level_info = level_get_info_cached(g_current_level_id);

    // Guard: max depth, valid zone index
    if (g_zone_recursion_depth >= 10) goto done;       // DAT_80611bb0
    if (zone_index == -1) goto done;
    if (zone_index < 0) goto done;
    if (zone_index >= *(int*)(level_info + 0x68)) goto done;

    g_zone_recursion_depth++;

    int zone_def = *(int*)(level_info + 0x64) + zone_index * 0x58;
    int entity_count = *(int*)(zone_def + 4);

    int entry_offset = 0;
    for (int i = 0; i < entity_count; i++) {
        int entry = *(int*)(zone_def + entry_offset + 8);

        if (entry < 0) {
            // Sub-zone reference: recurse
            int sub_count = soundpad_collect_zone_entities(
                -1 - entry, out_buf + collected, max_count - collected);
            collected += sub_count;
        } else {
            // Direct entity ID
            if (collected < max_count) {
                out_buf[collected] = entry;
                collected++;
            }
            if (collected >= max_count) break;
        }
        entry_offset += 4;
    }

    g_zone_recursion_depth--;

done:
    os_restore_context_pair();  // FUN_80382730
    return collected;
}

// =============================================================================
// 0x8006fd7c - soundpad_compute_lateral_distance
// Computes the minimum lateral (perpendicular) distance from a reference
// pad entity to a set of entities. Used for determining sound occlusion
// width or portal effective aperture.
//
// Algorithm:
//   For each entity in the list, compute the forward dot product with the
//   reference direction. If positive (entity is in front), compute the
//   cross-product component (lateral offset) and normalize by distance.
//   Track the minimum absolute lateral distance.
//
// Params:
//   param_3: entity ID list pointer
//   param_4: entity ID list pointer (secondary, used as validity check)
//   param_5: count of entities
// =============================================================================
void soundpad_compute_lateral_distance(int unused1, int unused2,
                                        int entity_list, int entity_list2, int count) {
    void* ctx = os_get_context_pair();  // FUN_803826e4
    float* ref_pos = (float*)((u64)ctx >> 32);  // paired return: position ptr

    if (entity_list == 0 || entity_list2 == 0 || (int)ctx == -1) goto done;

    int ref_slot = g_soundpad_primary_buf + (int)ctx * 0x20;  // DAT_80611b8c
    float dx = *ref_pos - *(float*)(ref_slot + 0x0C);
    float dz = ref_pos[2] - *(float*)(ref_slot + 0x14);
    float min_lateral = 0.0f;  // FLOAT_80613f98

    for (int i = 0; i < count; i++) {
        int ent_slot = g_soundpad_primary_buf + *(int*)(entity_list + i * 4) * 0x20;
        float ex = *(float*)(ent_slot + 0x0C) - *(float*)(ref_slot + 0x0C);
        float ez = *(float*)(ent_slot + 0x14) - *(float*)(ref_slot + 0x14);

        // Forward dot product (entity must be ahead of reference)
        float forward_dot = ex * dx + ez * dz;
        if (forward_dot > 0.0f) {
            // Cross product component = lateral offset
            float lateral = (-ex) * dz + ez * dx;  // note: negated ex
            float dist = sqrt(ez * ez + ex * ex);  // FUN_80131538

            if (dist > 0.0f) {
                lateral = lateral / dist;
            }

            float abs_lateral = (lateral < 0.0f) ? -lateral : lateral;
            if (abs_lateral < min_lateral) {
                min_lateral = abs_lateral;
            }
        }
    }

done:
    os_restore_context_pair();  // FUN_80382730
}

// =============================================================================
// 0x8006fef8 - soundpad_compute_zone_bounds
// Computes the bounding sphere for a sound zone. Collects all entities in
// the zone (via recursive collector), computes their weighted centroid
// (average XYZ position), then finds the maximum squared distance from any
// entity to the centroid. This gives a bounding radius for the zone.
//
// Uses paired-single float registers for position accumulation.
// Returns via paired registers (centroid + radius).
// =============================================================================
void soundpad_compute_zone_bounds(void) {
    // Paired-single register save area (f28-f31)
    // ...register spills to stack for paired-single ops...

    int zone_index = os_get_context_int();  // FUN_803826e8
    int level_info = level_get_info_cached(g_current_level_id);

    if (zone_index != -1 && zone_index >= 0 &&
        zone_index < *(int*)(level_info + 0x68)) {
        // Valid zone - proceed
    } else if (zone_index == -1) {
        // Special case: all zones (global query)
    } else {
        goto done;
    }

    float sum_x = 0.0f, sum_y = 0.0f, sum_z = 0.0f;
    u32 entity_count = 0;

    // Collect entities in zone
    int zone_entities[10];
    int num_zones = soundpad_collect_zone_entities(zone_index, zone_entities, 10);

    // Phase 1: Sum all entity positions
    for (int i = 0; i < num_zones; i++) {
        int data_ptr;
        int count = soundpad_get_entity_data(zone_entities[i], &data_ptr);

        for (int j = 0; j < count; j++) {
            int ent_slot = g_soundpad_primary_buf + *(int*)(data_ptr + j * 4) * 0x20;
            sum_x += *(float*)(ent_slot + 0x0C);
            sum_y += *(float*)(ent_slot + 0x10);
            sum_z += *(float*)(ent_slot + 0x14);
            entity_count++;
        }
    }

    // Phase 2: Compute centroid
    float inv_count = 1.0f / (float)entity_count;
    float cx = sum_x * inv_count;
    float cy = sum_y * inv_count;
    float cz = sum_z * inv_count;

    // Phase 3: Find max squared distance from centroid
    float max_dist_sq = 0.0f;

    for (int i = 0; i < num_zones; i++) {
        int data_ptr;
        int count = soundpad_get_entity_data(zone_entities[i], &data_ptr);

        for (int j = 0; j < count; j++) {
            int ent_slot = g_soundpad_primary_buf + *(int*)(data_ptr + j * 4) * 0x20;
            float dx = *(float*)(ent_slot + 0x0C) - cx;
            float dy = *(float*)(ent_slot + 0x10) - cy;
            float dz = *(float*)(ent_slot + 0x14) - cz;
            float dist_sq = dx * dx + dy * dy + dz * dz;

            if (dist_sq < max_dist_sq) {
                max_dist_sq = dist_sq;
            }
        }
    }

done:
    os_restore_context();  // FUN_80382734
}

// =============================================================================
// 0x800700fc - soundpad_zone_stub_a
// Empty stub. Reserved for future zone system extension.
// =============================================================================
void soundpad_zone_stub_a(void) {
    // intentionally empty
}

// =============================================================================
// 0x80070100 - soundpad_zone_stub_b
// Empty stub. Reserved for future zone system extension.
// =============================================================================
void soundpad_zone_stub_b(void) {
    // intentionally empty
}

// =============================================================================
// 0x80070104 - soundpad_get_zone_reverb_by_id
// Resolves a zone node by ID and returns its reverb level.
// Wrapper around soundpad_resolve_zone_node + soundpad_get_reverb_level.
// =============================================================================
float soundpad_get_zone_reverb_by_id(int zone_node_id) {
    int* zone_node = soundpad_resolve_zone_node(zone_node_id);
    return soundpad_get_reverb_level((int)zone_node);
}

// =============================================================================
// 0x80070128 - soundpad_get_reverb_level
// Returns the reverb intensity for a zone node. Reads the reverb index
// from offset +0x1C in the zone node struct. Valid indices are 0-19,
// which index into a 20-entry float table at 0x803feea0.
// Returns a default value (FLOAT_80613fa8) if node is NULL or index invalid.
// =============================================================================
float soundpad_get_reverb_level(int zone_node_ptr) {
    if (zone_node_ptr == 0) {
        return DEFAULT_REVERB_LEVEL;  // FLOAT_80613fa8
    }

    int reverb_idx = *(int*)(zone_node_ptr + 0x1C);
    if (reverb_idx >= 0 && reverb_idx < 20) {
        return g_reverb_table[reverb_idx];  // DAT_803feea0 + idx*4
    }

    return DEFAULT_REVERB_LEVEL;  // FLOAT_80613fa8
}

// =============================================================================
// 0x80070168 - soundpad_get_current_reverb_state
// Returns the current global reverb/zone state value. Likely the active
// reverb mix level or zone transition state.
// =============================================================================
int soundpad_get_current_reverb_state(void) {
    return g_soundpad_reverb_state;  // DAT_80611c68
}
