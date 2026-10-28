// ============================================================================
// soundpad_pathfinding.cpp - Sound Pad Pathfinding System
// Address Range: 0x8006ea40 - 0x8006f3a0
// Date: 2026-08-10
// Batch: 143
//
// Implements A*-style pathfinding through sound pad portals to determine
// acoustic propagation paths. Two variants exist:
//   1. Group-based: finds paths between zone groups (used for room-level routing)
//   2. Direct: finds paths between individual zones (zone-to-zone routing)
//
// Each variant maintains its own open/closed lists as intrusive linked lists
// stored in a fixed-size node pool (DAT_8053b468 / DAT_805375e8).
// ============================================================================

#include "common.h"
#include "soundpad.h"

// ============================================================================
// External references
// ============================================================================

extern int DAT_80611b8c;    // Sound pad zone array base
extern int DAT_80611b90;    // Sound pad portal array base
extern int DAT_80611c1c;    // Zone group adjacency table (pointers per group)
extern int DAT_80610458;    // Portal exclusion flag mask

// Group-based pathfinding state (shared with soundpad_path_list.cpp)
extern int DAT_80611bdc;    // Group path open list head
extern int DAT_80611bd8;    // Group path closed list head
extern int DAT_80611bc8;    // Target group index
extern int DAT_80611bcc;    // Target zone index
extern int DAT_80611bac;    // One-way portal flag

// Direct pathfinding state
extern int DAT_80611bc4;    // Direct path open list head
extern int DAT_80611bc0;    // Direct path closed list head
extern int DAT_80611bbc;    // Direct path free list head
extern int DAT_80611bb8;    // Direct path node count (next alloc index)
extern int DAT_80611bb4;    // Direct path target zone

// Path list node pool for direct pathfinding (16 bytes per node)
// Layout: [next:s16][zone_from:s16][zone_to:s16][portal_id:s16][cost:f32][total_dist:f32]
extern char DAT_8053b468;   // Direct path node pool base
extern char DAT_8053b46c;   // +4: zone_to field
extern char DAT_8053b46e;   // +6: portal_id field
extern char DAT_8053b474;   // +12: total_dist field

// Group path list node pool (16 bytes per node)
extern char DAT_805375e8;   // Group path node pool base
extern char DAT_805375ea;   // +2: closed list link
extern char DAT_805375ec;   // +4: zone field
extern char DAT_805375ee;   // +6: result zone
extern char DAT_805375f0;   // +8: cost field
extern char DAT_805375f4;   // +12: total_dist field

extern float FLOAT_80613f94; // Constant distance for weighted portals

// ============================================================================
// Forward declarations (SDK / OS)
// ============================================================================

extern "C" {
    // OS interrupt enable/disable for thread safety
    void FUN_803826cc(void);  // OSDisableInterrupts (returns zone + group info)
    void FUN_803826d4(void);  // OSDisableInterrupts variant (direct path)
    void FUN_803826e4(void);  // OSDisableInterrupts variant (group path)
    void FUN_80382718(void);  // OSRestoreInterrupts (group propagate)
    void FUN_80382720(void);  // OSRestoreInterrupts (direct propagate)
    void FUN_80382730(void);  // OSRestoreInterrupts (find group/bounded)
    float FUN_80131538(double val); // sqrtf wrapper
}

// Previously decompiled (batch 142)
extern void soundpad_path_find_by_group(void* open_list, int target_group);
extern int  soundpad_path_find_by_portal(void* list, int portal_id);
extern void soundpad_path_list_insert(double cost, double total_dist, short from_zone, short to_zone, short portal_id);
extern void soundpad_path_list_remove(void* list, int index);
extern void soundpad_path_list_reset(void);
extern float soundpad_portal_weighted_distance(uint* portal);
extern float soundpad_zone_distance_squared(int zone_a, int zone_b);

// ============================================================================
// Group-Based Pathfinding
// ============================================================================

// Address: 0x8006ea40 | Name: soundpad_path_propagate_group
// Expands one level of the group-based A* search. For each portal adjacent to
// the current zone's group, computes cumulative distance and inserts/updates
// nodes in the open list.
void soundpad_path_propagate_group(void) {
    // OSDisableInterrupts - returns packed zone/group info
    uint6 packed = FUN_803826cc();
    short from_zone = (short)(packed >> 32);
    int current_zone = (int)packed;

    int group_index = (int)*(short*)(DAT_80611b8c + current_zone * 0x20 + 8);
    int* adj_entry = *(int**)(DAT_80611c1c + group_index * 4);
    int portal_offset = adj_entry[0];  // Offset to portal indices
    int portal_count = adj_entry[1];   // Number of adjacent portals

    float accumulated_dist = /* extraout_f1 from register state */;

    for (int i = 0; i < portal_count; i++) {
        int portal_idx = *(int*)((int)adj_entry + i * 4 + portal_offset * 4 + 0x18);
        uint* portal = (uint*)(DAT_80611b90 + portal_idx * 0x14);
        uint flags = portal[0];

        // Skip disabled portals or portals matching exclusion mask
        if ((flags & 0x10000) == 0 || (flags & DAT_80610458) != 0)
            continue;

        uint zone_a = portal[1];
        uint zone_b = portal[2];
        uint target_zone;

        // Determine which side of the portal we're traversing to
        if (group_index == *(short*)(DAT_80611b8c + zone_a * 0x20 + 8)) {
            target_zone = zone_b;
            zone_a = zone_a; // zone_a stays as "from" side
        } else {
            // Check one-way portal restriction
            if ((flags & 2) != 0 && DAT_80611bac == 0)
                continue;
            target_zone = portal[2];
            // Swap: zone_b is "from" side
        }

        float portal_cost = soundpad_portal_weighted_distance(portal);
        float zone_dist = soundpad_zone_distance_squared(current_zone, target_zone);
        float cost_so_far = accumulated_dist + zone_dist + portal_cost;

        float dest_dist = soundpad_zone_distance_squared(zone_b, DAT_80611bcc);
        float total_dist = (float)portal[3] * dest_dist + cost_so_far;

        // Check if this portal is already in open list
        int existing = soundpad_path_find_by_portal(&DAT_80611bdc, portal_idx);
        short s_portal = (short)portal_idx;
        short s_target = (short)zone_b;

        if (existing < 0) {
            // Not in open list - check closed list
            int closed_idx = soundpad_path_find_by_portal(&DAT_80611bd8, portal_idx);
            if (closed_idx < 0) {
                // New node - insert into open list
                soundpad_path_list_insert(cost_so_far, total_dist, from_zone, s_target, s_portal);
            } else if (total_dist < *(float*)(&DAT_805375f4 + closed_idx * 0x10) &&
                       zone_b == (int)*(short*)(&DAT_805375ec + closed_idx * 0x10)) {
                // Better path found - move from closed to open
                soundpad_path_list_remove(&DAT_80611bd8, closed_idx);
                soundpad_path_list_insert(cost_so_far, total_dist, from_zone, s_target, s_portal);
            }
        } else if (total_dist < *(float*)(&DAT_805375f4 + existing * 0x10)) {
            // Better path - update in open list
            soundpad_path_list_remove(&DAT_80611bdc, existing);
            soundpad_path_list_insert(cost_so_far, total_dist, from_zone, s_target, s_portal);
        }
    }

    // Search complete for this node - move to closed and signal done
    soundpad_path_find_by_group(&DAT_80611bdc, DAT_80611bc8);
    FUN_80382718(); // OSRestoreInterrupts
}

// Address: 0x8006ec80 | Name: soundpad_path_find_group
// Main entry for group-based pathfinding. Iteratively expands the best node
// until the target group is reached or list is exhausted. Returns zone path
// through param_3 array.
void soundpad_path_find_group(int param_1, int param_2, short* out_zones, int max_zones) {
    FUN_803826e4(); // OSDisableInterrupts
    soundpad_path_list_reset();

    int start_zone = /* extraout_r4 from register */;
    DAT_80611bc8 = (int)*(short*)(DAT_80611b8c + start_zone * 0x20 + 8);
    DAT_80611bcc = start_zone;

    int found = soundpad_path_propagate_group();
    int last_closed = -1;
    int open_head = DAT_80611bdc;

    while (open_head >= 0) {
        if (found >= 0) break;

        // Pop from open list, push to closed
        DAT_80611bdc = (int)*(short*)(&DAT_805375e8 + open_head * 0x10);
        short closed_link = (short)DAT_80611bd8;
        DAT_80611bd8 = open_head;
        *(short*)(&DAT_805375e8 + open_head * 0x10) = closed_link;

        found = soundpad_path_propagate_group();
        last_closed = open_head;
        open_head = DAT_80611bdc;
    }

    // Collect results into output array
    if (found >= 0 && max_zones > 0) {
        out_zones[0] = (short)found;
        max_zones--;
    }

    int out_idx = (found >= 0) ? 2 : 0;
    while (last_closed >= 0 && max_zones > 0) {
        *(short*)((int)out_zones + out_idx) = *(short*)(&DAT_805375ee + last_closed * 0x10);
        out_idx += 2;
        max_zones--;
        last_closed = (int)*(short*)(&DAT_805375ea + last_closed * 0x10);
    }

    FUN_80382730(); // OSRestoreInterrupts
}

// Address: 0x8006edb4 | Name: soundpad_path_find_group_bounded
// Same as soundpad_path_find_group but with an early-termination distance bound.
// If the accumulated cost of a node exceeds the threshold, search is abandoned.
void soundpad_path_find_group_bounded(int param_1, int param_2, short* out_zones, int max_zones) {
    float max_distance = (float)FUN_803826e4();
    soundpad_path_list_reset();

    int start_zone = /* extraout_r4 */;
    DAT_80611bc8 = (int)*(short*)(DAT_80611b8c + start_zone * 0x20 + 8);
    DAT_80611bcc = start_zone;

    int found = soundpad_path_propagate_group();
    int last_closed = -1;
    int open_head = DAT_80611bdc;

    while (open_head >= 0) {
        if (found >= 0) break;

        DAT_80611bdc = (int)*(short*)(&DAT_805375e8 + open_head * 0x10);
        short closed_link = (short)DAT_80611bd8;
        DAT_80611bd8 = open_head;
        *(short*)(&DAT_805375e8 + open_head * 0x10) = closed_link;

        // Distance bound check - terminate if exceeded
        if (max_distance < *(float*)(&DAT_805375f0 + open_head * 0x10))
            break;

        found = soundpad_path_propagate_group();
        last_closed = open_head;
        open_head = DAT_80611bdc;
    }

    // Collect results
    if (found >= 0 && max_zones > 0) {
        out_zones[0] = (short)found;
        max_zones--;
    }

    int out_idx = (found >= 0) ? 2 : 0;
    while (last_closed >= 0 && max_zones > 0) {
        *(short*)((int)out_zones + out_idx) = *(short*)(&DAT_805375ee + last_closed * 0x10);
        out_idx += 2;
        max_zones--;
        last_closed = (int)*(short*)(&DAT_805375ea + last_closed * 0x10);
    }

    FUN_80382730(); // OSRestoreInterrupts
}

// ============================================================================
// Direct (Zone-to-Zone) Pathfinding
// ============================================================================

// Address: 0x8006ef0c | Name: soundpad_direct_path_list_reset
// Initializes direct pathfinding state. Must be called before starting a
// new direct path search.
void soundpad_direct_path_list_reset(void) {
    DAT_80611bc4 = -1;  // Open list head = empty
    DAT_80611bc0 = -1;  // Closed list head = empty
    DAT_80611bbc = -1;  // Free list head = empty
    DAT_80611bb8 = 0;   // Next alloc index = 0
}

// Address: 0x8006ef28 | Name: soundpad_direct_path_list_insert
// Inserts a node into the direct path open list, maintaining sorted order
// by total_distance (ascending). Allocates from free list or bump allocator.
void soundpad_direct_path_list_insert(double cost, double total_dist,
                                       short from_zone, short to_zone, short portal_id) {
    short* node;
    int node_idx;

    // Allocate node: try free list first, then bump allocator
    if (DAT_80611bbc < 0) {
        node = (short*)(&DAT_8053b468 + DAT_80611bb8 * 0x10);
        node_idx = DAT_80611bb8;
        DAT_80611bb8++;
    } else {
        node = (short*)(&DAT_8053b468 + DAT_80611bbc * 0x10);
        node_idx = DAT_80611bbc;
        DAT_80611bbc = (int)*node;  // Pop from free list
    }

    // Insert into sorted position in open list
    if (DAT_80611bc4 < 0) {
        // Empty list
        DAT_80611bc4 = node_idx;
        *node = -1;
    } else if (*(float*)(&DAT_8053b474 + DAT_80611bc4 * 0x10) <= (float)total_dist) {
        // Insert after head - walk to find position
        short* cursor = (short*)(&DAT_8053b468 + DAT_80611bc4 * 0x10);
        bool inserted = false;
        while (!inserted) {
            short next = *cursor;
            if (next < 0) {
                *cursor = (short)node_idx;
                inserted = true;
                *node = next;
            } else {
                int next_offset = next * 0x10;
                if ((float)total_dist < *(float*)(&DAT_8053b474 + next_offset)) {
                    *cursor = (short)node_idx;
                    inserted = true;
                    *node = next;
                }
                cursor = (short*)(&DAT_8053b468 + next_offset);
            }
        }
    } else {
        // New head (lowest distance)
        short old_head = (short)DAT_80611bc4;
        DAT_80611bc4 = node_idx;
        *node = old_head;
    }

    // Fill node data
    node[1] = from_zone;
    node[2] = to_zone;
    node[3] = portal_id;
    *(float*)(node + 4) = (float)cost;
    *(float*)(node + 6) = (float)total_dist;
}

// Address: 0x8006f01c | Name: soundpad_direct_path_find_by_portal
// Searches a path list for a node with a matching portal ID.
// Returns the node index if found, -1 otherwise.
int soundpad_direct_path_find_by_portal(int* list_head, int portal_id) {
    int idx = *list_head;
    while (idx >= 0) {
        if (portal_id == *(short*)(&DAT_8053b46e + idx * 0x10))
            return idx;
        idx = (int)*(short*)(&DAT_8053b468 + idx * 0x10);
    }
    return -1;
}

// Address: 0x8006f054 | Name: soundpad_direct_path_list_remove
// Removes a node from a path list by index. Unlinks from the chain and
// returns the node to the free list for reuse.
void soundpad_direct_path_list_remove(int* list_head, int target_idx) {
    int head = *list_head;

    if (head == target_idx) {
        // Removing head
        *list_head = (int)*(short*)(&DAT_8053b468 + head * 0x10);
    } else {
        // Walk chain to find predecessor
        int prev = head;
        while (prev >= 0) {
            int next = (int)*(short*)(&DAT_8053b468 + prev * 0x10);
            if (*(short*)(&DAT_8053b468 + prev * 0x10) == target_idx) {
                *(short*)(&DAT_8053b468 + prev * 0x10) =
                    *(short*)(&DAT_8053b468 + target_idx * 0x10);
                break;
            }
            prev = next;
        }
    }

    // Return to free list
    *(short*)(&DAT_8053b468 + target_idx * 0x10) = (short)DAT_80611bbc;
    DAT_80611bbc = target_idx;
}

// Address: 0x8006f0e0 | Name: soundpad_zone_distance
// Computes 3D Euclidean distance between two zone center points.
// Zone positions stored at DAT_80611b8c + zone_id * 0x20 + {0xC, 0x10, 0x14}.
float soundpad_zone_distance(int zone_a, int zone_b) {
    int addr_b = DAT_80611b8c + zone_b * 0x20;
    int addr_a = DAT_80611b8c + zone_a * 0x20;

    float dx = *(float*)(addr_b + 0x10) - *(float*)(addr_a + 0x10);
    float dy = *(float*)(addr_b + 0x0C) - *(float*)(addr_a + 0x0C);
    float dz = *(float*)(addr_b + 0x14) - *(float*)(addr_a + 0x14);

    return FUN_80131538((double)(dx * dx + dy * dy + dz * dz)); // sqrtf
}

// Address: 0x8006f144 | Name: soundpad_portal_distance_weighted
// Returns the effective traversal cost for a portal. If the "pre-weighted"
// flag (0x20000) is set, returns a constant; otherwise computes
// weight_factor * zone_distance(zone_a, zone_b).
float soundpad_portal_distance_weighted(uint* portal) {
    if ((portal[0] & 0x20000) != 0) {
        return FLOAT_80613f94; // Pre-computed constant distance
    }

    float dist = soundpad_zone_distance(portal[1], portal[2]);
    return (float)portal[3] * dist;
}

// Address: 0x8006f194 | Name: soundpad_path_propagate_direct
// Direct zone-to-zone path propagation. Same algorithm as group-based but
// operates on individual zones and uses the "exit portals" of the adjacency
// table (offset by portal_count from the entry portals).
void soundpad_path_propagate_direct(void) {
    uint6 packed = FUN_803826d4();
    short from_zone = (short)(packed >> 32);
    uint current_zone = (uint)packed;

    int group_index = *(short*)(DAT_80611b8c + current_zone * 0x20 + 8);
    int* adj_entry = *(int**)(DAT_80611c1c + group_index * 4);
    int entry_count = adj_entry[0];
    int portal_count = adj_entry[1];
    int exit_count = adj_entry[2];

    float accumulated_dist = /* extraout_f1 */;

    for (int i = 0; i < exit_count; i++) {
        int portal_idx = *(int*)((int)adj_entry + i * 4 + (entry_count + portal_count) * 4 + 0x18);
        uint* portal = (uint*)(DAT_80611b90 + portal_idx * 0x14);
        uint flags = portal[0];

        if ((flags & 0x10000) == 0 || (flags & DAT_80610458) != 0)
            continue;

        uint target_zone = portal[1];
        if (target_zone == current_zone) {
            target_zone = portal[2];
        } else if (portal[2] != current_zone || (flags & 2) != 0 || DAT_80611bac != 0) {
            continue;
        }

        // Check if we reached the destination
        if (target_zone == (uint)DAT_80611bb4) {
            FUN_80382720(); // Found path - restore interrupts and return
            return;
        }

        float portal_cost = soundpad_portal_distance_weighted(portal);
        float cost_so_far = accumulated_dist + portal_cost;
        float dest_dist = soundpad_zone_distance(target_zone, DAT_80611bb4);
        float total_dist = cost_so_far + dest_dist;

        int existing = soundpad_direct_path_find_by_portal(&DAT_80611bc4, portal_idx);
        short s_portal = (short)portal_idx;
        short s_target = (short)target_zone;

        if (existing < 0) {
            int closed_idx = soundpad_direct_path_find_by_portal(&DAT_80611bc0, portal_idx);
            if (closed_idx < 0) {
                soundpad_direct_path_list_insert(cost_so_far, total_dist, from_zone, s_target, s_portal);
            } else if (total_dist < *(float*)(&DAT_8053b474 + closed_idx * 0x10) &&
                       target_zone == (int)*(short*)(&DAT_8053b46c + closed_idx * 0x10)) {
                soundpad_direct_path_list_remove(&DAT_80611bc0, closed_idx);
                soundpad_direct_path_list_insert(cost_so_far, total_dist, from_zone, s_target, s_portal);
            }
        } else if (total_dist < *(float*)(&DAT_8053b474 + existing * 0x10)) {
            soundpad_direct_path_list_remove(&DAT_80611bc4, existing);
            soundpad_direct_path_list_insert(cost_so_far, total_dist, from_zone, s_target, s_portal);
        }
    }

    FUN_80382720(); // OSRestoreInterrupts
}
