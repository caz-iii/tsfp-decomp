// Sound Pad Zone Graph System
// Batch 139 | Date: 2026-08-10
// Handles zone adjacency graph construction, portal lookups, and adjacent zone queries
// for spatial sound propagation through level geometry.

#include "common.h"
#include "sound_pad.h"
#include "mem_alloc.h"
#include "level.h"

// ============================================================================
// Data structures:
//   Zone node table (DAT_80611b8c): stride 0x20, fields:
//     +0x00: flags (u32)
//     +0x04: flags2 (u32)
//     +0x08: zone_group_id (s16)
//     +0x0C: position X (float)
//     +0x10: position Y (float)
//     +0x14: position Z (float)
//
//   Portal table (DAT_80611b90): stride 0x14, fields:
//     +0x00: flags (u32) - bit 0x10000 = active, bit 0x20 = disabled, bit 2 = one-way
//     +0x04: zone_a_id (u32)
//     +0x08: zone_b_id (u32)
//     +0x0C: (unused?)
//     +0x10: (unused?)
//
//   Zone graph node (allocated per zone): variable size
//     +0x00: vertex_count (u32)
//     +0x04: inter_zone_portal_count (u32)
//     +0x08: intra_zone_portal_count (u32)
//     +0x0C: center X (float)
//     +0x10: center Y (float)
//     +0x14: center Z (float)
//     +0x18...: vertex IDs [vertex_count], then inter-portal indices [inter_count],
//               then intra-portal indices [intra_count]
// ============================================================================

// Globals
static void*  g_zone_node_table;       // DAT_80611b8c
static void*  g_portal_table;          // DAT_80611b90
static void*  g_trigger_table;         // DAT_80611b94
static void*  g_extra_buffer;          // DAT_80611b98
static void*  g_zone_graph;            // DAT_80611c1c - per-zone graph node pointers
static int    g_portal_count;          // DAT_80611c10
static int    g_zone_node_count;       // DAT_80611c14
static int    g_zone_group_count;      // DAT_80611c18 (number of zone groups)
static int    g_cross_portal_count;    // DAT_80611c00
static int    g_internal_portal_count; // DAT_80611c04
static int    g_trigger_count;         // DAT_80611c0c

// ============================================================================
// Address: 0x8006c490 | Name: soundpad_clear_zone_data
// Clears all zone data pointers. Guarded by zone system lock flag.
// ============================================================================
void soundpad_clear_zone_data(void) {
    if (DAT_80611568 != 0) {
        return;
    }
    g_zone_node_table = NULL;
    g_portal_table = NULL;
    g_trigger_table = NULL;
    g_extra_buffer = NULL;
}

// ============================================================================
// Address: 0x8006c4b4 | Name: soundpad_find_trigger_by_id
// Searches the trigger table (stride 0x20) for a trigger with matching ID at +8.
// Returns pointer to trigger entry, or NULL.
// ============================================================================
void* soundpad_find_trigger_by_id(int trigger_id) {
    int offset = 0;
    int remaining = g_trigger_count;

    if (remaining > 0) {
        do {
            if (trigger_id == *(s16*)((u8*)g_trigger_table + offset + 8)) {
                return (void*)((u8*)g_trigger_table + offset);
            }
            offset += 0x20;
            remaining--;
        } while (remaining != 0);
    }
    return NULL;
}

// ============================================================================
// Address: 0x8006c4f4 | Name: soundpad_find_portal_between_zones
// Finds a portal connecting two zone indices with flag filtering.
// Validates indices, checks for special zone pair ranges, then searches the
// zone graph node's inter-zone and intra-zone portal lists.
// ============================================================================
void* soundpad_find_portal_between_zones(int zone_a_idx, int zone_b_idx,
                                         u32 include_flags, u32 exclude_flags) {
    s64 pair = FUN_803826ec(); // packs zone_a_idx, zone_b_idx
    u32 a = (u32)(pair >> 32);
    u32 b = (u32)pair;

    if (pair < 0 || (int)a >= g_zone_node_count ||
        (int)b < 0 || (int)b >= g_zone_node_count) {
        goto done;
    }

    // Check if pair falls in special ranges (mapmaker zones 8000-8350, 8350-8370)
    bool special_range = false;
    if (pair > 0x1F3FFFFFFFFFLL && pair < 0x209E00000000LL) {
        special_range = true;
    }
    if (!special_range) {
        if (pair > 0x209DFFFFFFFFLL && pair < 0x20B200000000LL) {
            special_range = true;
        }
        if (!special_range) {
            // Also check single-index ranges
            bool single_special = false;
            if ((int)b > 7999 && (int)b < 0x209E) {
                single_special = true;
            }
            if (!single_special) {
                if ((int)b > 0x209D && (int)b < 0x20B2) {
                    single_special = true;
                }
                if (!single_special) {
                    // General case: look up zone graph
                    int* zone_data = FUN_8006d050(a);
                    int group_id = FUN_8006fb84((int)zone_data);

                    if (group_id >= 0 && group_id < g_zone_group_count && g_zone_graph != 0) {
                        int* node = *(int**)(g_zone_graph + group_id * 4);
                        int inter_count = node[1];

                        // Search inter-zone portals
                        int offset = 0;
                        int count = inter_count;
                        if (count > 0) {
                            do {
                                u32* portal = (u32*)(g_portal_table +
                                    *(int*)((u8*)node + offset + node[0] * 4 + 0x18) * 0x14);
                                if ((include_flags == (include_flags & portal[0])) &&
                                    ((portal[0] & exclude_flags) == 0) &&
                                    ((portal[1] == a && portal[2] == b) ||
                                     (portal[1] == b && portal[2] == a))) {
                                    goto done;
                                }
                                offset += 4;
                                count--;
                            } while (count != 0);
                        }

                        // Search intra-zone portals
                        int intra_count = node[2];
                        offset = 0;
                        if (intra_count > 0) {
                            do {
                                u32* portal = (u32*)(g_portal_table +
                                    *(int*)((u8*)node + offset + (node[0] + inter_count) * 4 + 0x18) * 0x14);
                                if ((include_flags == (include_flags & portal[0])) &&
                                    ((portal[0] & exclude_flags) == 0) &&
                                    ((portal[1] == a && portal[2] == b) ||
                                     (portal[1] == b && portal[2] == a))) {
                                    break;
                                }
                                offset += 4;
                                intra_count--;
                            } while (intra_count != 0);
                        }
                    }
                }
            }
        }
    }

done:
    FUN_80382738(); // release lock
    return; // portal result in register
}

// ============================================================================
// Address: 0x8006c74c | Name: soundpad_find_active_portal
// Finds an active portal (flag 0x10000) between two zone entity pointers.
// ============================================================================
void* soundpad_find_active_portal(int* zone_a, int* zone_b) {
    if (zone_a == NULL || zone_b == NULL) {
        return NULL;
    }
    int id_a = FUN_8006d1c0(zone_a);
    int id_b = FUN_8006d1c0(zone_b);
    return soundpad_find_portal_between_zones(id_a, id_b, 0x10000, 0);
}

// ============================================================================
// Address: 0x8006c7b8 | Name: soundpad_find_portal_by_zone_pair
// Linear search of the portal table for a portal connecting zone_a_id and zone_b_id.
// Checks both orderings. Returns portal pointer or NULL.
// ============================================================================
void* soundpad_find_portal_by_zone_pair(int zone_a_id, int zone_b_id) {
    int offset = 0;
    int remaining = g_portal_count;

    if (remaining > 0) {
        do {
            int entry_addr = (int)g_portal_table + offset;
            if ((*(int*)(entry_addr + 4) == zone_a_id && *(int*)(entry_addr + 8) == zone_b_id) ||
                (*(int*)(entry_addr + 4) == zone_b_id && *(int*)(entry_addr + 8) == zone_a_id)) {
                return (void*)entry_addr;
            }
            offset += 0x14;
            remaining--;
        } while (remaining != 0);
    }
    return NULL;
}

// ============================================================================
// Address: 0x8006c818 | Name: soundpad_find_any_portal
// Finds any portal (no flag requirement) between two zone entity pointers.
// ============================================================================
void* soundpad_find_any_portal(int* zone_a, int* zone_b) {
    if (zone_a == NULL || zone_b == NULL) {
        return NULL;
    }
    int id_a = FUN_8006d1c0(zone_a);
    int id_b = FUN_8006d1c0(zone_b);
    return soundpad_find_portal_between_zones(id_a, id_b, 0, 0);
}

// ============================================================================
// Address: 0x8006c884 | Name: soundpad_get_portal_index
// Returns the linear index of a portal between two entities (by entity ID).
// Returns (portal_ptr - base) / 0x14, or -1 if not found.
// ============================================================================
int soundpad_get_portal_index(int entity_a_id, int entity_b_id) {
    int* zone_a = (int*)FUN_8006cfdc(entity_a_id);
    int* zone_b = (int*)FUN_8006cfdc(entity_b_id);
    int portal = (int)soundpad_find_any_portal(zone_a, zone_b);

    if (portal == 0) {
        return -1;
    }
    return (portal - (int)g_portal_table) / 0x14;
}

// ============================================================================
// Address: 0x8006c8f0 | Name: soundpad_query_adjacent_zones
// Queries all zones adjacent to a given zone node via portals.
// Fills out_zone_ids buffer, optionally out_portal_indices buffer.
// Filters by exclude_flags and include_internal flag.
// Searches inter-zone portals first, then intra-zone portals.
// ============================================================================
int soundpad_query_adjacent_zones(int zone_node_idx, int unused,
                                  int* out_portal_indices, int max_results,
                                  u32 exclude_flags, int include_internal) {
    s64 pair = FUN_803826d8();
    u32 node_idx = (u32)(pair >> 32);
    int out_zone_ids = (int)pair;
    int result_count = 0;
    int write_offset = 0;

    if (pair < 0 || (int)node_idx >= g_zone_node_count) {
        goto done;
    }

    int group_id = FUN_8006fb84((int)g_zone_node_table + node_idx * 0x20);
    FUN_8001d54c();

    if (group_id < 0 || group_id >= g_zone_group_count || max_results == 0) {
        goto done;
    }

    int* node = *(int**)(g_zone_graph + group_id * 4);
    int inter_offset = node[0] * 4 + 0x18;

    // Search inter-zone portals
    for (int i = 0; i < node[1]; i++) {
        u32* portal = (u32*)((u8*)g_portal_table +
            *(int*)((u8*)node + i * 4 + inter_offset) * 0x14);
        u32 flags = portal[0];

        if (((exclude_flags | 0x20) & flags) != 0) continue;
        if (!include_internal && (flags & 0x10000) == 0) continue;

        if (node_idx == portal[1]) {
            if (out_zone_ids != 0) {
                *(u32*)(out_zone_ids + write_offset) = portal[2];
            }
            if (out_portal_indices != 0) {
                *(int*)((u8*)out_portal_indices + write_offset) =
                    *(int*)((u8*)node + i * 4 + inter_offset);
            }
            result_count++;
            write_offset += 4;
        } else if (node_idx == portal[2] && (flags & 2) == 0) {
            if (out_zone_ids != 0) {
                *(u32*)(out_zone_ids + write_offset) = portal[1];
            }
            if (out_portal_indices != 0) {
                *(int*)((u8*)out_portal_indices + write_offset) =
                    *(int*)((u8*)node + i * 4 + inter_offset);
            }
            result_count++;
            write_offset += 4;
        }

        if (max_results <= result_count) goto done;
    }

    // Search intra-zone portals
    write_offset = result_count * 4;
    int intra_offset = (node[0] + node[1]) * 4 + 0x18;

    for (int i = 0; i < node[2]; i++) {
        u32* portal = (u32*)((u8*)g_portal_table +
            *(int*)((u8*)node + i * 4 + intra_offset) * 0x14);
        u32 flags = portal[0];

        if (((exclude_flags | 0x20) & flags) != 0) continue;
        if (!include_internal && (flags & 0x10000) == 0) continue;

        if (node_idx == portal[1]) {
            if (out_zone_ids != 0) {
                *(u32*)(out_zone_ids + write_offset) = portal[2];
            }
            if (out_portal_indices != 0) {
                *(int*)((u8*)out_portal_indices + write_offset) =
                    *(int*)((u8*)node + i * 4 + intra_offset);
            }
            result_count++;
            write_offset += 4;
        } else if (node_idx == portal[2] && (flags & 2) == 0) {
            if (out_zone_ids != 0) {
                *(u32*)(out_zone_ids + write_offset) = portal[1];
            }
            if (out_portal_indices != 0) {
                *(int*)((u8*)out_portal_indices + write_offset) =
                    *(int*)((u8*)node + i * 4 + intra_offset);
            }
            result_count++;
            write_offset += 4;
        }

        if (max_results <= result_count) break;
    }

done:
    FUN_80382724(); // release lock
    return result_count;
}

// ============================================================================
// Address: 0x8006cb64 | Name: soundpad_query_adjacent_zones_simple
// Wrapper: queries adjacent zones without returning portal indices.
// ============================================================================
int soundpad_query_adjacent_zones_simple(int zone_node_idx, int unused,
                                         int max_results, u32 exclude_flags,
                                         int include_internal) {
    return soundpad_query_adjacent_zones(zone_node_idx, unused, NULL,
                                         max_results, exclude_flags, include_internal);
}

// ============================================================================
// Address: 0x8006c02c | Name: soundpad_build_zone_graph
// Builds the zone adjacency graph for sound propagation.
// For mapmaker levels (ID 100): counts cross-zone portals, rebalances counts,
//   then allocates graph nodes with centroid computation.
// For normal levels: loads zone data from file path + zone loader.
// Each graph node stores: vertices (zone nodes in group), inter-zone portals,
//   intra-zone portals, and centroid position.
// ============================================================================
void soundpad_build_zone_graph(void) {
    int level_info = level_get_info_cached(DAT_80612614);

    if (DAT_80612614 == 100) {
        // Mapmaker level: count cross-zone portals
        u32 cross_count = 0;
        FUN_8006d288();

        for (u32 group = 0; (int)group < (int)g_zone_group_count; group++) {
            int offset = 0;
            int remaining = g_portal_count;
            if (remaining > 0) {
                do {
                    u32* portal = (u32*)((u8*)g_portal_table + offset);
                    if ((portal[0] & 0x10000) != 0) {
                        s16 group_a = *(s16*)((u8*)g_zone_node_table + portal[1] * 8 + 2);
                        s16 group_b = *(s16*)((u8*)g_zone_node_table + portal[2] * 8 + 2);
                        if (group_a != group_b &&
                            (group_a == (s16)group || group_b == (s16)group)) {
                            cross_count++;
                        }
                    }
                    offset += 0x14;
                    remaining--;
                } while (remaining != 0);
            }
        }

        // Rebalance cross/internal portal counts
        int delta = ((int)cross_count / 2 +
                     (cross_count < 0 && (cross_count & 1) != 0 ? 1 : 0))
                    - g_cross_portal_count;
        g_internal_portal_count -= delta;
        g_cross_portal_count += delta;
    } else {
        // Normal level: load zone data from file
        soundpad_build_data_path();
        FUN_8006e018();
    }

    if (g_zone_node_count <= 0) {
        return;
    }

    // Determine number of zone groups
    void* heap = FUN_8006e6d8();
    DAT_80611b88 = 0;
    g_zone_group_count = 0;

    int scan_offset = 0;
    int remaining = g_zone_node_count;
    if (remaining > 0) {
        do {
            s16 group_id = *(s16*)((u8*)g_zone_node_table + scan_offset + 8);
            if ((int)g_zone_group_count <= group_id) {
                g_zone_group_count = (u32*)(group_id + 1);
            }
            scan_offset += 0x20;
            remaining--;
        } while (remaining != 0);
    }

    // Allocate zone graph: pointer array + variable-size nodes
    int alloc_size = (int)g_zone_group_count * 0x1C +
                     g_trigger_count * 4 + // DAT_80611c08
                     g_cross_portal_count * 8 +
                     g_internal_portal_count * 4;
    alloc_size = (alloc_size + 0xF) & ~0xF; // align to 16
    g_zone_graph = mem_alloc(heap, alloc_size, 1);

    // Build per-group graph nodes
    float init_val = FLOAT_80613f58; // large float sentinel
    u32* write_ptr = (u32*)(g_zone_graph + (int)g_zone_group_count * 4);

    for (int group = 0; group < (int)g_zone_group_count; group++) {
        // Store pointer to this node
        *(u32**)(g_zone_graph + group * 4) = write_ptr;

        // Initialize node header
        write_ptr[0] = 0; // vertex_count
        write_ptr[1] = 0; // inter_portal_count
        write_ptr[2] = 0; // intra_portal_count
        write_ptr[3] = *(u32*)&init_val; // center X
        write_ptr[4] = *(u32*)&init_val; // center Y
        write_ptr[5] = *(u32*)&init_val; // center Z

        // Collect vertices (zone nodes in this group)
        for (u32 node_idx = 0; (int)node_idx < g_zone_node_count; node_idx++) {
            u32 flags = *(u32*)((u8*)g_zone_node_table + node_idx * 0x20 + 4);
            if ((flags & 1) != 0 && group == *(s16*)((u8*)g_zone_node_table + node_idx * 0x20 + 8)) {
                u32 vert_count = write_ptr[0];
                write_ptr[0] = vert_count + 1;
                write_ptr[vert_count + 6] = node_idx;
                // Accumulate centroid
                *(float*)&write_ptr[3] += *(float*)((u8*)g_zone_node_table + node_idx * 0x20 + 0xC);
                *(float*)&write_ptr[4] += *(float*)((u8*)g_zone_node_table + node_idx * 0x20 + 0x10);
                *(float*)&write_ptr[5] += *(float*)((u8*)g_zone_node_table + node_idx * 0x20 + 0x14);
            }
        }

        // Compute centroid average
        float inv_count = 1.0f / (float)write_ptr[0];
        *(float*)&write_ptr[3] *= inv_count;
        *(float*)&write_ptr[4] *= inv_count;
        *(float*)&write_ptr[5] *= inv_count;

        // Collect inter-zone portals (portals crossing between groups)
        for (u32 portal_idx = 0; (int)portal_idx < g_portal_count; portal_idx++) {
            u32* portal = (u32*)((u8*)g_portal_table + portal_idx * 0x14);
            if ((portal[0] & 0x10000) != 0) {
                s16 group_a = *(s16*)((u8*)g_zone_node_table + portal[1] * 8 + 2);
                s16 group_b = *(s16*)((u8*)g_zone_node_table + portal[2] * 8 + 2);
                if (group_a != group_b &&
                    (group_a == group || group_b == group)) {
                    u32 inter_count = write_ptr[1];
                    write_ptr[1] = inter_count + 1;
                    write_ptr[write_ptr[0] + inter_count + 6] = portal_idx;
                }
            }
        }

        // Collect intra-zone portals (portals within the same group)
        for (u32 portal_idx = 0; (int)portal_idx < g_portal_count; portal_idx++) {
            u32* portal = (u32*)((u8*)g_portal_table + portal_idx * 0x14);
            if ((portal[0] & 0x10000) != 0) {
                s16 group_a = *(s16*)((u8*)g_zone_node_table + portal[1] * 8 + 2);
                s16 group_b = *(s16*)((u8*)g_zone_node_table + portal[2] * 8 + 2);
                if (group_a == group && group_b == group) {
                    u32 intra_count = write_ptr[2];
                    write_ptr[2] = intra_count + 1;
                    write_ptr[write_ptr[0] + write_ptr[1] + intra_count + 6] = portal_idx;
                }
            }
        }

        // Advance write pointer past this node's data
        int total_entries = write_ptr[0] + write_ptr[1] + write_ptr[2];
        write_ptr = write_ptr + total_entries + 6;
    }

    // Allocate level-specific sound data if available
    void* heap2 = FUN_8006e590();
    DAT_80611ba8 = 0;
    if (level_info != 0 && *(int*)(level_info + 0x68) > 0) {
        DAT_80611ba8 = mem_alloc(heap2, *(int*)(level_info + 0x68) * 0x30, 1);
    }

    soundpad_update();
    FUN_800700fc();
}

// ============================================================================
// Address: 0x8006cb9c | Name: soundpad_check_player_zone_changes
// Per-frame: checks if any player's zone group has changed. If so, triggers
// reverb/visibility updates via soundpad_on_zone_changed.
// ============================================================================
void soundpad_check_player_zone_changes(void) {
    FUN_803826e0(); // acquire lock
    bool changed = false;

    if ((DAT_805015c0 & 0x20000000) == 0) {
        int player_offset = 0;
        int data_offset = 0;

        for (int i = 0; i < DAT_80612610; i++) {
            int player_data = DAT_80611d78 + data_offset;
            int* zone_node = soundpad_resolve_zone_node(
                *(int*)(DAT_80611d90 + player_offset + 0x678));
            int group_id = FUN_8006fb84((int)zone_node);
            int zone_group = FUN_8006fbd8(group_id);

            if (zone_group != *(int*)(player_data + 0x1400)) {
                changed = true;
                *(int*)(player_data + 0x1400) = zone_group;
            }
            player_offset += 0xEA0;
            data_offset += 0x141C;
        }

        if (changed) {
            soundpad_on_zone_changed();
        }
    }
    FUN_8038272c(); // release lock
}

// ============================================================================
// Address: 0x8006cc68 | Name: soundpad_on_zone_changed
// Called when a player's sound zone changes. Updates reverb environment
// and refreshes entity visibility masks.
// ============================================================================
void soundpad_on_zone_changed(void) {
    FUN_8023411c(); // update reverb environment
    entity_visibility_update();
}

// ============================================================================
// Address: 0x8006cc9c | Name: soundpad_zone_node_is_valid
// Validates a zone node pointer. Returns 1 if the node represents a valid,
// active zone entity; 0 otherwise.
// Flag checks:
//   bit 0x1: node is active
//   bit 0x800: trigger zone (ID must be > 9000)
//   bit 0x400000: special marker (ID must be -2)
//   default: primary zone (ID must be 1001..9000)
// ============================================================================
int soundpad_zone_node_is_valid(int* node) {
    if (node == NULL) return 0;

    u32 flags = (u32)node[1];
    if ((flags & 1) == 0) return 0;

    if ((flags & 0x800) != 0) {
        // Trigger zone: ID must be > 9000
        if (node[0] != -1 && node[0] > 9000) {
            return 1;
        }
    } else if ((flags & 0x400000) != 0) {
        // Special marker: ID must be -2
        if (node[0] == -2) {
            return 1;
        }
    } else {
        // Primary zone: ID in range 1001..9000
        int id = node[0];
        if (id != -1 && id > 1000 && id < 9001) {
            return 1;
        }
    }
    return 0;
}

// ============================================================================
// Address: 0x8006cd1c | Name: soundpad_get_inter_portal_list
// Returns the inter-zone portal count for a zone group and writes the
// portal index array start to *out_ptr.
// ============================================================================
int soundpad_get_inter_portal_list(int group_idx, u32** out_ptr) {
    int* node = *(int**)(g_zone_graph + group_idx * 4);
    *out_ptr = (u32*)(node + node[0] + 6); // skip header + vertex list
    return node[1]; // inter-zone portal count
}

// ============================================================================
// Address: 0x8006cd4c | Name: soundpad_find_index_in_array
// Linear search: finds 'value' in an int array of 'count' elements.
// Returns the index, or -1 if not found.
// ============================================================================
int soundpad_find_index_in_array(int value, int* array, int count) {
    for (int i = 0; i < count; i++) {
        if (value == array[i]) {
            return i;
        }
    }
    return -1;
}

// ============================================================================
// Address: 0x8006cd88 | Name: soundpad_query_two_hop_zones
// BFS-style query: finds all zones reachable within 2 portal hops from
// the source zone. De-duplicates results using soundpad_find_index_in_array.
// Fills both zone ID and portal index output buffers.
// ============================================================================
void soundpad_query_two_hop_zones(int src_zone, int unused,
                                   int* out_zone_count, int out_portal_buf,
                                   int* out_portal_count, int max_results,
                                   u32 exclude_flags) {
    int local_portals[20];
    int local_zones[38];

    u64 pair = FUN_803826b8();
    int out_zone_buf = (int)pair;

    // First hop: direct neighbors
    int hop1_count = soundpad_query_adjacent_zones(
        (int)(pair >> 32), out_zone_buf, out_portal_buf, max_results, exclude_flags, 0);

    int zone_total = hop1_count;
    int portal_total = hop1_count;
    int hop1_offset = 0;

    // Second hop: neighbors of neighbors
    for (int i = 0; i < hop1_count && (zone_total < max_results || portal_total < max_results); i++) {
        int hop2_count = soundpad_query_adjacent_zones(
            *(int*)(out_zone_buf + hop1_offset), local_zones, (int)local_portals, 20, exclude_flags, 0);

        int zone_write = zone_total * 4;
        int portal_write = portal_total * 4;

        for (int j = 0; j < hop2_count && (zone_total < max_results || portal_total < max_results); j++) {
            // Add zone if not already in results
            if (out_zone_buf != 0 && zone_total < max_results) {
                if (soundpad_find_index_in_array(local_zones[j], (int*)out_zone_buf, zone_total) == -1) {
                    *(int*)(out_zone_buf + zone_write) = local_zones[j];
                    zone_total++;
                    zone_write += 4;
                }
            }
            // Add portal if not already in results
            if (out_portal_buf != 0 && portal_total < max_results) {
                if (soundpad_find_index_in_array(local_portals[j], (int*)out_portal_buf, portal_total) == -1) {
                    *(int*)(out_portal_buf + portal_write) = local_portals[j];
                    portal_total++;
                    portal_write += 4;
                }
            }
        }
        hop1_offset += 4;
    }

    if (out_portal_count != NULL) *out_portal_count = portal_total;
    if (out_zone_count != NULL) *out_zone_count = zone_total;

    FUN_80382704(); // release lock
}

// ============================================================================
// Address: 0x8006cefc | Name: soundpad_find_zone_by_id
// Binary search for a zone node index by entity ID.
// Primary table (DAT_80611b8c, stride 0x20): IDs < 9001.
// For IDs >= 9001: delegates to soundpad_find_trigger_zone_by_id.
// Returns index into table, or -1 if not found.
// ============================================================================
int soundpad_find_zone_by_id(int entity_id) {
    if (entity_id < 9001) {
        // Binary search in primary zone table
        int lo = 0;
        int hi = g_zone_node_count;
        int last_mid = -1;

        do {
            int mid = lo + (hi - lo) / 2;
            if (mid == last_mid) return -1;

            int table_id = *(int*)((u8*)g_zone_node_table + mid * 0x20);
            last_mid = mid;

            if (entity_id < table_id) {
                hi = mid;
            } else if (entity_id > table_id) {
                lo = mid;
            } else {
                return mid; // found
            }
        } while (true);
    } else {
        return soundpad_find_trigger_zone_by_id(entity_id);
    }
}

// ============================================================================
// Address: 0x8006cf80 | Name: soundpad_find_trigger_zone_by_id
// Binary search for a trigger zone by entity ID in the trigger table
// (DAT_80611b94, stride 0x20). Returns (index + 8000) as remapped ID, or -1.
// ============================================================================
int soundpad_find_trigger_zone_by_id(int entity_id) {
    int lo = 0;
    int hi = g_trigger_count;
    int last_mid = -1;

    do {
        int mid = lo + (hi - lo) / 2;
        if (mid == last_mid) return -1;

        int table_id = *(int*)((u8*)g_trigger_table + mid * 0x20);
        last_mid = mid;

        if (entity_id < table_id) {
            hi = mid;
        } else if (entity_id > table_id) {
            lo = mid;
        } else {
            return mid + 8000; // offset into trigger ID space
        }
    } while (true);
}

// ============================================================================
// Address: 0x8006cfdc | Name: soundpad_resolve_entity_to_zone
// Resolves an entity ID to a zone node pointer.
// ID ranges:
//   1001..9000: primary zone table (binary search via soundpad_find_zone_by_id)
//   > 9000: trigger zone table (binary search, returns at trigger_table + (idx-8000)*0x20)
// Returns zone node address, or 0 if not found.
// ============================================================================
void* soundpad_resolve_entity_to_zone(int entity_id) {
    if (entity_id >= 1001 && entity_id <= 9000) {
        int idx = soundpad_find_zone_by_id(entity_id);
        if (idx != -1) {
            return (void*)((u8*)g_zone_node_table + idx * 0x20);
        }
    } else if (entity_id > 9000) {
        int idx = soundpad_find_trigger_zone_by_id(entity_id);
        if (idx != -1) {
            return (void*)((u8*)g_trigger_table + (idx - 8000) * 0x20);
        }
    }
    return NULL;
}

// ============================================================================
// Address: 0x8006d050 | Name: soundpad_resolve_zone_node
// Resolves a zone ID to a validated zone node pointer.
// ID ranges:
//   8000..8350 (0x1F40..0x209E): trigger zones at g_trigger_table + (id-8000)*0x20
//   8350..8370 (0x209E..0x20B2): extra buffer at g_extra_buffer + (id-0x209E)*0x20
//   Mapmaker (level 100) extends trigger range to 8000..9280 (0x1F40..0x2440)
//   Default: primary zones at g_zone_node_table + id*0x20
// Validates result with soundpad_zone_node_is_valid; returns NULL if invalid.
// ============================================================================
int* soundpad_resolve_zone_node(int zone_id) {
    if (zone_id == -1) {
        return NULL;
    }

    int* result;

    if (zone_id >= 8000 && zone_id < 0x209E) {
        // Trigger zone range
        result = (int*)((u8*)g_trigger_table + (zone_id - 8000) * 0x20);
    } else {
        // Mapmaker extends trigger range
        if (DAT_80612614 == 100 && zone_id >= 8000 && zone_id < 0x2440) {
            result = (int*)((u8*)g_trigger_table + (zone_id - 8000) * 0x20);
        } else if (zone_id > 0x209D && zone_id < 0x20B2) {
            // Extra zone buffer
            result = (int*)((u8*)g_extra_buffer + (zone_id - 0x209E) * 0x20);
        } else {
            // Primary zone table
            result = (int*)((u8*)g_zone_node_table + zone_id * 0x20);
        }
    }

    // Validate before returning
    if (!soundpad_zone_node_is_valid(result)) {
        return NULL;
    }
    return result;
}

// ============================================================================
// Address: 0x8006d140 | Name: soundpad_get_entity_id_from_zone
// Gets the entity ID (field[0]) of a zone node by zone ID.
// Handles 3 ranges: -1 -> -1, extra (0x209E..0x20B2) -> -2,
// triggers (8000..0x209E) -> trigger table, default -> primary table.
// ============================================================================
int soundpad_get_entity_id_from_zone(int zone_id) {
    if (zone_id == -1) return -1;

    if (zone_id > 0x209D && zone_id < 0x20B2) {
        return -2; // extra zone marker
    }

    if (zone_id >= 8000 && zone_id < 0x209E) {
        // Trigger zone: read from trigger table
        return *(int*)((u8*)g_trigger_table + zone_id * 0x20 - 0x3E800);
    }

    // Primary zone table
    return *(int*)((u8*)g_zone_node_table + zone_id * 0x20);
}

// ============================================================================
// Address: 0x8006d1c0 | Name: soundpad_zone_node_to_index
// Converts a zone node pointer back to its linear index.
// Determines table membership by flags:
//   0x400000 -> extra buffer (offset +0x209E)
//   0x800 -> trigger table (offset +8000)
//   default -> primary table (raw index)
// Returns -1 if node is invalid.
// ============================================================================
int soundpad_zone_node_to_index(int* node) {
    if (!soundpad_zone_node_is_valid(node)) {
        return -1;
    }

    u32 flags = (u32)node[1];

    if ((flags >> 22) & 1) {
        // Extra buffer zone
        u32 offset = (u32)((u8*)node - (u8*)g_extra_buffer);
        return (int)(offset >> 5) + 0x209E;
    } else if ((flags & 0x800) == 0) {
        // Primary zone table
        u32 offset = (u32)((u8*)node - (u8*)g_zone_node_table);
        return (int)(offset >> 5);
    } else {
        // Trigger zone table
        u32 offset = (u32)((u8*)node - (u8*)g_trigger_table);
        return (int)(offset >> 5) + 8000;
    }
}

// ============================================================================
// Address: 0x8006d250 | Name: soundpad_portal_activate
// Activates a portal by setting the 0x10000 flag.
// ============================================================================
void soundpad_portal_activate(int portal_index) {
    *(u32*)((u8*)g_portal_table + portal_index * 0x14) |= 0x10000;
}

// ============================================================================
// Address: 0x8006d268 | Name: soundpad_portal_deactivate
// Deactivates a portal by clearing the 0x10000 flag.
// ============================================================================
void soundpad_portal_deactivate(int portal_index) {
    if (portal_index < 0) return;
    *(u32*)((u8*)g_portal_table + portal_index * 0x14) &= ~0x10000;
}

// ============================================================================
// Address: 0x8006d288 | Name: soundpad_build_portals_from_defs
// Builds portal entries from the portal definition table (DAT_80611be4, stride 6).
// Each def: zone_a (u16), zone_b (u16), flags (byte).
// Validates both zones are active, creates 0x14-byte portal with:
//   flags = (bit1_from_def & 1) * 2 | 0x10000
//   zone IDs adjusted by -0x3E9
//   distance fields set to sentinel floats
// Then calls auto_create_group_portals + FUN_8006bc14 (portal sort).
// ============================================================================
void soundpad_build_portals_from_defs(void) {
    for (int i = 0; i < DAT_80611be0; i++) {
        u16* def = (u16*)(DAT_80611be4 + i * 6);
        u16 zone_a = def[0];
        u16 zone_b = def[1];

        bool both_active = false;
        if ((*(u32*)((u8*)g_zone_node_table + (zone_a - 0x3E9) * 0x20 + 4) & 1) != 0 &&
            (*(u32*)((u8*)g_zone_node_table + (zone_b - 0x3E9) * 0x20 + 4) & 1) != 0) {
            both_active = true;
        }

        if (both_active) {
            u32* portal = (u32*)((u8*)g_portal_table + g_portal_count * 0x14);
            g_portal_count++;

            u8 flags_byte = *(u8*)(def + 2);
            portal[0] = (u32)(-(flags_byte & 1)) & 2 | 0x10000;
            portal[1] = zone_a - 0x3E9;
            portal[2] = zone_b - 0x3E9;
            portal[3] = *(u32*)&FLOAT_80613f5c; // distance sentinel
            portal[4] = *(u32*)&FLOAT_80613f58; // weight sentinel
            g_cross_portal_count++;
        }
    }
    soundpad_auto_create_group_portals();
    FUN_8006bc14(); // portal sort
}

// ============================================================================
// Address: 0x8006d378 | Name: soundpad_auto_create_group_portals
// Uses lookup tables at 0x803cc274/0x803cc29c (10 entries each) to find
// zone pairs that should be connected within the same group.
// For each matching pair without an existing portal, calls soundpad_create_portal.
// ============================================================================
void soundpad_auto_create_group_portals(void) {
    int zone_ids[10];
    int group_ids[10];

    FUN_803826e4(); // acquire lock

    // Copy lookup tables from ROM
    memcpy(group_ids, (void*)0x803cc274, 40);
    memcpy(zone_ids, (void*)0x803cc29c, 40);

    int count = 0; // populated by FUN_8023c090
    FUN_8023c090(&count, zone_ids, group_ids);

    for (int i = 0; i < count; i++) {
        int zone_a = soundpad_find_zone_by_id(zone_ids[i]);
        // Check subsequent entries with same group
        for (int j = i + 1; j < count; j++) {
            if (group_ids[i] == group_ids[j]) {
                int zone_b = soundpad_find_zone_by_id(zone_ids[j]);
                int portal = soundpad_find_portal_by_zone_pair(zone_a, zone_b);
                if (portal == 0) {
                    soundpad_create_portal();
                }
                break;
            }
        }
    }
    FUN_80382730(); // release lock
}

// ============================================================================
// Address: 0x8006d4b8 | Name: soundpad_create_portal
// Creates a new portal between two zone IDs (passed via register pair).
// Resolves both to zone nodes, validates them, appends a 0x14-byte portal
// with flags 0x10010 (active + auto-created). Increments cross or internal
// counter based on whether zones share a group.
// ============================================================================
void soundpad_create_portal(void) {
    u64 pair = FUN_803826f0();
    int zone_a_id = (int)(pair >> 32);
    int zone_b_id = (int)pair;

    int* node_a = soundpad_resolve_zone_node(zone_a_id);
    int* node_b = soundpad_resolve_zone_node(zone_b_id);

    if (node_a == NULL || node_b == NULL) return;
    if ((node_a[1] & 1) == 0 || (node_b[1] & 1) == 0) return;

    int offset = g_portal_count * 0x14;
    g_portal_count++;

    u32* portal = (u32*)((u8*)g_portal_table + offset);
    portal[0] = 0x10010; // active + auto-created
    portal[1] = zone_a_id;
    portal[2] = zone_b_id;
    portal[3] = *(u32*)&FLOAT_80613f68; // default distance
    portal[4] = *(u32*)&FLOAT_80613f58; // default weight

    // Count as cross-zone or internal based on group
    if (*(s16*)(node_a + 2) == *(s16*)(node_b + 2)) {
        g_internal_portal_count++;
    } else {
        g_cross_portal_count++;
    }

    FUN_8038273c(); // release lock
}

// ============================================================================
// Address: 0x8006d58c | Name: soundpad_flood_fill_connected
// Recursive flood-fill marking all zones reachable from 'start_zone' via
// active portals. Uses DAT_805375c8 as visited array.
// Only traverses within same group and index range [base_idx..max_idx].
// Used for zone group splitting after portal changes.
// ============================================================================
void soundpad_flood_fill_connected(int start_zone, int base_idx,
                                    int max_idx, int zone_table,
                                    int portal_start, int portal_end,
                                    int portal_table) {
    u64 pair = FUN_803826d0();
    u32 zone_idx = (u32)(pair >> 32);
    int visited_base = (int)pair;

    if (((u8*)0x805375C8)[zone_idx - visited_base] != 0) {
        FUN_8038271c();
        return;
    }

    ((u8*)0x805375C8)[zone_idx - visited_base] = 1;
    s16 my_group = *(s16*)(zone_table + zone_idx * 0x20 + 8);

    // Walk all portals looking for connections to this zone
    for (int i = portal_start; i < portal_end; i++) {
        u32* portal = (u32*)(portal_table + i * 0x14);
        if ((portal[0] & 0x10000) == 0 || (portal[0] & 2) != 0) continue;

        // If zone_idx is on side A, check side B
        if (portal[1] == zone_idx) {
            u32 other = portal[2];
            if (*(s16*)(zone_table + other * 0x20 + 8) == my_group &&
                (int)other <= max_idx && visited_base <= (int)other &&
                ((u8*)0x805375C8)[other - visited_base] == 0) {
                soundpad_flood_fill_connected(other, visited_base, max_idx,
                    zone_table, portal_start, portal_end, portal_table);
            }
        }
        // If zone_idx is on side B, check side A
        if (portal[2] == zone_idx) {
            u32 other = portal[1];
            if (*(s16*)(zone_table + other * 0x20 + 8) == my_group &&
                (int)other <= max_idx && visited_base <= (int)other &&
                ((u8*)0x805375C8)[other - visited_base] == 0) {
                soundpad_flood_fill_connected(other, visited_base, max_idx,
                    zone_table, portal_start, portal_end, portal_table);
            }
        }
    }
    FUN_8038271c(); // release lock
}

// ============================================================================
// Address: 0x8006d708 | Name: soundpad_swap_zone_positions
// Swaps position data (X/Y/Z at +0xC..+0x14) between two zone nodes.
// Refuses if either has the 0x1000 lock flag.
// ============================================================================
void soundpad_swap_zone_positions(int node_a, int node_b) {
    if ((*(u32*)(node_a + 4) & 0x1000) != 0) return;
    if ((*(u32*)(node_b + 4) & 0x1000) != 0) return;

    // Swap X, Y, Z
    float tmp_x = *(float*)(node_a + 0x0C);
    float tmp_y = *(float*)(node_a + 0x10);
    float tmp_z = *(float*)(node_a + 0x14);

    *(float*)(node_a + 0x0C) = *(float*)(node_b + 0x0C);
    *(float*)(node_a + 0x10) = *(float*)(node_b + 0x10);
    *(float*)(node_a + 0x14) = *(float*)(node_b + 0x14);

    *(float*)(node_b + 0x0C) = tmp_x;
    *(float*)(node_b + 0x10) = tmp_y;
    *(float*)(node_b + 0x14) = tmp_z;
}

// ============================================================================
// Address: 0x8006d754 | Name: soundpad_load_zone_data
// MASSIVE: Imports zone nodes, portals, and triggers from a level descriptor
// into the runtime tables. Handles:
// - Zone node copy with position transform (rotation modes 1-3)
// - Portal creation with ID remapping to global indices
// - Trigger zone creation with group assignment
// - Group splitting via flood-fill when portals separate subgraphs
// - Portal definition de-duplication
// - Zone disabling based on exclude/include lists
// ============================================================================
void soundpad_load_zone_data(int param_1, int param_2, int* level_desc) {
    // [Implementation is ~300 lines of complex level loading logic]
    // Key operations:
    // 1. Copy zone nodes from level_desc (+0xC, stride 0x20) into g_zone_node_table
    //    at offset DAT_80611c14, adjusting IDs and group numbers
    // 2. Apply position rotation based on level_desc[4] (modes 1-3 subtract constants)
    // 3. Transform positions via FUN_8012f9b8
    // 4. Disable zones matching exclude lists at level_desc[0x3C] and level_desc[0x4D]
    // 5. Create portal entries from level_desc portal section, remap zone indices
    // 6. Handle one-way portal flags based on level_desc[5] configuration
    // 7. Copy trigger zones into g_trigger_table at offset DAT_80611c0c
    // 8. For zones with exclude/include lists, run flood-fill to detect disconnected
    //    subgraphs and assign new group IDs to split components
    // 9. Append portal definitions to DAT_80611be4, de-duplicating
    // 10. Update global counters: DAT_80611c14, DAT_80611c10, DAT_80611c18, DAT_80611c0c

    // See Ghidra decompilation for full implementation details.
    // PC port note: this function's complexity comes from the mapmaker tile system
    // where zones from multiple tiles must be merged into a single global graph.
}
