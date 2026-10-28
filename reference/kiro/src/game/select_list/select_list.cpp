// =============================================================================
// select_list.cpp - Weighted selection list for character/entity selection
// TimeSplitters: Future Perfect (GameCube) Decompilation
// Batch 061 | Date: 2026-08-05
// =============================================================================
//
// A selection list is a fixed-capacity array used for character selection,
// weapon pools, and entity spawn tables. Layout:
//
//   Offset 0x00: int count          - number of entries currently in list
//   Offset 0x04: int entries[60]    - entity/character IDs (max 60 entries)
//   Offset 0xF4: int mode           - selection mode (0=weapon/prop, 1=character)
//   Offset 0xF8: float weights[60]  - probability weights for random selection
//
// An external reference table (at obj+0x90) provides additional per-entry
// metadata with stride 8: [id, flags] pairs, plus a weight table at +0x28.
//
// =============================================================================

#include "common.h"

// Forward declarations
extern float FLOAT_80613bb0;  // default weight value (likely 0.0f)

// -----------------------------------------------------------------------------
// Address: 0x8003c770 | Name: select_list_init_empty
// -----------------------------------------------------------------------------
// Initializes a selection list to empty. Sets count to 0 and stores the
// mode/type value at offset 0xF4.
// -----------------------------------------------------------------------------
void select_list_init_empty(u32* list, u32 mode) {
    list[0] = 0;          // count = 0
    list[0x3D] = mode;    // mode at offset 0xF4
}

// -----------------------------------------------------------------------------
// Address: 0x8003c780 | Name: select_list_init_with_data
// -----------------------------------------------------------------------------
// Initializes a selection list with pre-existing data. Copies count entries
// from source array into the list body, stores mode, then resets all weights.
// -----------------------------------------------------------------------------
void select_list_init_with_data(int* list, int mode, int* source, int count) {
    list[0] = count;      // set entry count
    list[0x3D] = mode;    // set mode at offset 0xF4

    // Copy entries from source to list[1..count]
    for (int i = 0; i < count; i++) {
        list[i + 1] = source[i];
    }

    select_list_reset_weights(list);
}

// -----------------------------------------------------------------------------
// Address: 0x8003c7cc | Name: select_list_reset_weights
// -----------------------------------------------------------------------------
// Resets all weight/probability values to 0. Iterates count entries and
// clears the weight array starting at offset 0xF8.
// -----------------------------------------------------------------------------
void select_list_reset_weights(int* list) {
    float default_weight = FLOAT_80613bb0;  // likely 0.0f
    int count = list[0];

    for (int i = 0; i < count; i++) {
        // Offset 0xF8 = index 0x3E in int terms
        // Weight at offset 0xF8 + i*4
        *(float*)((char*)list + 0xF8 + i * 4) = default_weight;
        // Actually zeroed out immediately after assignment
        *(int*)((char*)list + 0xF8 + i * 4) = 0;
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003c804 | Name: select_list_find_value
// -----------------------------------------------------------------------------
// Linear search through the selection list for a matching value.
// Returns the 0-based index if found, or -1 if not found.
// List format: [count, entry0, entry1, ...] with 4-byte stride.
// -----------------------------------------------------------------------------
int select_list_find_value(int* list, int value) {
    if (list == NULL) {
        return -1;
    }

    int count = list[0];

    for (int i = 0; i < count; i++) {
        if (list[i + 1] == value) {
            return i;
        }
    }

    return -1;
}

// -----------------------------------------------------------------------------
// Address: 0x8003c850 | Name: select_list_nop
// -----------------------------------------------------------------------------
// Empty function. Placeholder in selection list function table.
// -----------------------------------------------------------------------------
void select_list_nop(void) {
    // intentionally empty
}

// -----------------------------------------------------------------------------
// Address: 0x8003c854 | Name: select_list_append_entry
// -----------------------------------------------------------------------------
// Appends an entity to the selection list. Reads the entity ID from the
// source object based on the list's mode:
//   Mode 1: reads from source + 0x84 (character ID)
//   Mode 0: reads from source + 0x180 (weapon/entity ID)
// Stores the ID and increments the count.
// -----------------------------------------------------------------------------
void select_list_append_entry(int* list, int* source) {
    int mode = list[0x3D];  // mode at offset 0xF4
    int count = list[0];
    int value;

    // Read value based on mode
    if (mode == 1) {
        value = *(int*)((char*)source + 0x84);   // character ID
    } else if (mode == 0) {
        value = *(int*)((char*)source + 0x180);  // weapon/entity ID
    }

    // Store in entry array at list[count+1]
    list[count + 1] = value;

    // Also store in secondary array at offset 0xF8 (list[0x3E + count])
    if (mode == 1) {
        list[count + 0x3E] = *(int*)((char*)source + 0x84);
    } else if (mode == 0) {
        list[count + 0x3E] = *(int*)((char*)source + 0x180);
    }

    list[0] = count + 1;  // increment count
}

// -----------------------------------------------------------------------------
// Address: 0x8003c8c0 | Name: select_list_get_id
// -----------------------------------------------------------------------------
// Gets the ID value at a given index from the selection list's external
// metadata table (stored at obj+0x90). Table has stride 8, returns first
// element of the pair.
// -----------------------------------------------------------------------------
u32 select_list_get_id(int obj, int index) {
    int* table = *(int**)(obj + 0x90);
    return (u32)table[index * 2];  // stride 8, first element
}

// -----------------------------------------------------------------------------
// Address: 0x8003c8d0 | Name: select_list_get_flags
// -----------------------------------------------------------------------------
// Gets the flags value at a given index from the same external table.
// Stride 8, returns the second element (offset +4) of the pair.
// -----------------------------------------------------------------------------
u32 select_list_get_flags(int obj, int index) {
    int* table = *(int**)(obj + 0x90);
    return (u32)table[index * 2 + 1];  // stride 8, second element
}

// -----------------------------------------------------------------------------
// Address: 0x8003c8e4 | Name: select_list_get_weight
// -----------------------------------------------------------------------------
// Gets the weight value at a given index from the weight sub-table
// (at table base + 0x28). Stride 4.
// -----------------------------------------------------------------------------
u32 select_list_get_weight(int obj, int index) {
    int* table = *(int**)(obj + 0x90);
    return (u32)*(int*)((char*)table + 0x28 + index * 4);
}
