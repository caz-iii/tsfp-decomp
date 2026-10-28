// Sound Pad Propagation Path System
// Batch 142 | Date: 2026-08-11
// Sorted linked-list for sound propagation pathfinding through zone portals.
// Entry pool at DAT_805375e8, stride 0x10 per entry.
// Entry layout: next (s16 +0), zone_idx (s16 +2), portal_idx (s16 +4),
//               prev_entry (s16 +6), distance (float +8), weight (float +C).

#include "common.h"
#include "sound_pad.h"

// Globals for the propagation path linked list
static int g_path_head;      // DAT_80611bdc - head of sorted list
static int g_path_tail;      // DAT_80611bd8 - tail of sorted list
static int g_path_free;      // DAT_80611bd4 - free list head
static int g_path_count;     // DAT_80611bd0 - total allocated entries

// ============================================================================
// Address: 0x8006e73c | Name: soundpad_path_list_reset
// Resets the propagation path linked list to empty state.
// ============================================================================
void soundpad_path_list_reset(void) {
    g_path_head = -1;
    g_path_tail = -1;
    g_path_free = -1;
    g_path_count = 0;
}

// ============================================================================
// Address: 0x8006e758 | Name: soundpad_path_list_insert
// Inserts a new entry into the sorted propagation path list.
// Sorted by weight (field at +0xC) in ascending order.
// Allocates from free list if available, otherwise appends to pool.
// ============================================================================
void soundpad_path_list_insert(float distance, float weight,
                                s16 zone_idx, s16 portal_idx, s16 prev_entry) {
    s16* entry;
    int slot;

    // Allocate a slot
    if (g_path_free < 0) {
        entry = (s16*)(0x805375E8 + g_path_count * 0x10);
        slot = g_path_count;
        g_path_count++;
    } else {
        entry = (s16*)(0x805375E8 + g_path_free * 0x10);
        slot = g_path_free;
        g_path_free = (int)*entry; // next free
    }

    // Insert into sorted position by weight
    if (g_path_head < 0) {
        // Empty list
        g_path_head = slot;
        entry[0] = -1; // next = null
    } else if (weight <= *(float*)(0x805375F4 + g_path_head * 0x10)) {
        // Insert before head (new smallest)
        s16 old_head = (s16)g_path_head;
        g_path_head = slot;
        entry[0] = old_head;
    } else {
        // Walk list to find insertion point
        s16* cursor = (s16*)(0x805375E8 + g_path_head * 0x10);
        bool inserted = false;
        while (!inserted) {
            s16 next = *cursor;
            if (next < 0) {
                // End of list: append
                *cursor = (s16)slot;
                entry[0] = next;
                inserted = true;
            } else {
                float next_weight = *(float*)(0x805375F4 + next * 0x10);
                if (weight < next_weight) {
                    // Insert before 'next'
                    *cursor = (s16)slot;
                    entry[0] = next;
                    inserted = true;
                } else {
                    cursor = (s16*)(0x805375E8 + next * 0x10);
                }
            }
        }
    }

    // Fill entry fields
    entry[1] = zone_idx;
    entry[2] = portal_idx;
    entry[3] = prev_entry;
    *(float*)(entry + 4) = distance;
    *(float*)(entry + 6) = weight;
}

// ============================================================================
// Address: 0x8006e84c | Name: soundpad_path_find_by_portal
// Walks the path linked list starting from *head_ptr looking for an entry
// whose portal_idx (field at +6, offset from DAT_805375ee) matches param_2.
// Returns entry index, or -1 if not found.
// ============================================================================
int soundpad_path_find_by_portal(int* head_ptr, int portal_idx) {
    int cur = *head_ptr;
    while (cur >= 0) {
        if (portal_idx == *(s16*)(0x805375EE + cur * 0x10)) {
            return cur;
        }
        cur = (int)*(s16*)(0x805375E8 + cur * 0x10);
    }
    return -1;
}

// ============================================================================
// Address: 0x8006e884 | Name: soundpad_path_find_by_group
// Walks the path list looking for an entry whose zone node belongs to
// the specified group ID. Stores the matching entry's weight in
// FLOAT_80611ba4 (global output). Returns the portal_idx from that entry.
// ============================================================================
int soundpad_path_find_by_group(int* head_ptr, int group_id) {
    int cur = *head_ptr;
    while (cur >= 0) {
        int off = cur * 0x10;
        s16 zone_idx = *(s16*)(0x805375EC + off);
        if (group_id == *(s16*)((u8*)g_zone_node_table + zone_idx * 0x20 + 8)) {
            FLOAT_80611ba4 = *(float*)(0x805375F4 + off);
            return (int)*(s16*)(0x805375EE + off);
        }
        cur = (int)*(s16*)(0x805375E8 + off);
    }
    return -1;
}

// ============================================================================
// Address: 0x8006e8e8 | Name: soundpad_path_list_remove
// Removes entry 'idx' from the linked list rooted at *head_ptr.
// Unlinks from the chain and returns the slot to the free list.
// ============================================================================
void soundpad_path_list_remove(int* head_ptr, int idx) {
    int cur = *head_ptr;

    if (cur == idx) {
        // Removing head
        *head_ptr = (int)*(s16*)(0x805375E8 + cur * 0x10);
    } else {
        // Walk to find predecessor
        bool found = false;
        while (cur >= 0 && !found) {
            int prev = cur;
            cur = (int)*(s16*)(0x805375E8 + prev * 0x10);
            if (*(s16*)(0x805375E8 + prev * 0x10) == idx) {
                // Unlink: predecessor.next = target.next
                *(s16*)(0x805375E8 + prev * 0x10) = *(s16*)(0x805375E8 + idx * 0x10);
                found = true;
            }
        }
    }

    // Return to free list
    *(s16*)(0x805375E8 + idx * 0x10) = (s16)g_path_free;
    g_path_free = idx;
}

// ============================================================================
// Address: 0x8006e974 | Name: soundpad_zone_distance_squared
// Computes 3D distance between two zone nodes (by index).
// Reads positions from g_zone_node_table at offsets +0xC, +0x10, +0x14.
// Calls sqrt on the squared sum (so returns actual distance, not squared).
// ============================================================================
float soundpad_zone_distance_squared(int zone_a, int zone_b) {
    int addr_a = (int)g_zone_node_table + zone_a * 0x20;
    int addr_b = (int)g_zone_node_table + zone_b * 0x20;

    float dx = *(float*)(addr_b + 0x0C) - *(float*)(addr_a + 0x0C);
    float dy = *(float*)(addr_b + 0x10) - *(float*)(addr_a + 0x10);
    float dz = *(float*)(addr_b + 0x14) - *(float*)(addr_a + 0x14);

    float dist_sq = dx * dx + dy * dy + dz * dz;
    return FUN_80131538(dist_sq); // sqrt
}

// ============================================================================
// Address: 0x8006e9d8 | Name: soundpad_portal_weighted_distance
// Computes effective distance through a portal for sound propagation.
// Formula: distance_between_zones * weight^2
// Flag 0x20000: portal is blocked -> returns constant large value.
// Flag 0x40: narrow portal penalty -> multiply by additional factor.
// ============================================================================
float soundpad_portal_weighted_distance(u32* portal) {
    if ((portal[0] & 0x20000) != 0) {
        return FLOAT_80613f8c; // blocked: return large constant
    }

    float dist = soundpad_zone_distance_squared(portal[1], portal[2]);
    float weight = *(float*)(portal + 3);
    float result = weight * weight * dist;

    if ((portal[0] & 0x40) != 0) {
        result *= FLOAT_80613f90; // narrow portal multiplier
    }

    return result;
}
