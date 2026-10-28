// =============================================================================
// entity_system.cpp - Entity pool management, lifecycle, and registration
// TimeSplitters: Future Perfect (GameCube) decompilation
// Batch 160 | Date: 2026-08-10
//
// The entity system manages a fixed-size pool of game entities (max 0x226 = 550
// in normal mode, 20 in mapmaker mode). Each entity is 0x2E8 bytes (stride).
// Entities are categorized by type (byte at +0x30) into up to 214 category
// lists. Three tracked lists exist for different update categories:
//   - DAT_8054088c: type 0 (think/update entities)
//   - DAT_80540d2c: type 8 (render entities)
//   - DAT_8053f450: type 9 (special/scripted entities)
// =============================================================================

#include "common.h"
#include "entity/entity.h"

// Forward declarations
extern void event_invoke_type_handler(int type, int param);
extern void event_set_handler(int mask, int subtype, void* handler);

// =============================================================================
// Globals
// =============================================================================
// DAT_80611c20 - Max entity count (0x226 normal, 0x14 mapmaker)
// DAT_80611c24 - Entity pool base pointer (stride 0x2E8 per entity)
// DAT_80611c28 - Secondary entity buffer (400 bytes)
// DAT_80611c2c - Misc counter
// DAT_80611c40 - Event queue write index
// DAT_80611c44 - Event queue buffer pointer
// DAT_80611c4c - Misc state
// DAT_80611c50 - Misc state
// DAT_80611c54 - Entity system active flag (1 = active)
// DAT_80611c58 - Category list heap write cursor
// DAT_80611c5c - Category list heap base pointer (0x5340 bytes)
// DAT_80611c60 - Render entity list count (type 8)
// DAT_80611c64 - Update entity list count (type 0)
// DAT_80611c68 - Special entity list count (type 9)
// DAT_80611c6c - Misc counter
// DAT_80611c70 - Current active entity count
// DAT_80611b14 - Entity iteration start index
// DAT_80611928 - Current frame timestamp
// DAT_80612614 - Current level ID
// DAT_806125e8 - Entity type/class info table (stride 0x30)
// DAT_805c9858 - Entity type index lookup table (short, for type setter)
// DAT_8053f7d4 - Category counts (214 entries)
// DAT_8053fb2c - Category list pointers (214 entries)
// DAT_8053fe84 - Category capacity tracking (214 entries)
// DAT_805401dc - Category current counts (214 entries)
// DAT_8054088c - Update entity list (type 0)
// DAT_80540d2c - Render entity list (type 8)
// DAT_8053f450 - Special entity list (type 9)

// =============================================================================
// 0x800703d4 - entity_find_by_id
// Looks up an entity by its unique ID. First tries direct index access
// (param as index into pool), checking that the DEAD flag (bit 31 at +0x28)
// is not set and the ID at +0x88 matches. If direct access fails, performs
// a linear scan through all active entities.
// Returns entity pointer or 0 (not found / invalid).
// =============================================================================
int entity_find_by_id(int entity_id) {
    if (entity_id < 0) {
        return 0;
    }

    // Try direct index access
    int direct_ptr = g_entity_pool + entity_id * 0x2E8;  // DAT_80611c24
    if ((*(u32*)(direct_ptr + 0x28) & 0x80000000) == 0 &&
        entity_id == *(int*)(direct_ptr + 0x88)) {
        return direct_ptr;
    }

    // Linear scan fallback
    int offset = 0;
    int remaining = g_entity_count;  // DAT_80611c70
    if (remaining > 0) {
        do {
            int ent_ptr = g_entity_pool + offset;
            if ((*(u32*)(ent_ptr + 0x28) & 0x80000000) == 0) {
                if (entity_id == *(int*)(ent_ptr + 0x88)) {
                    return ent_ptr;
                }
            }
            offset += 0x2E8;
            remaining--;
        } while (remaining != 0);
    }

    return 0;
}

// =============================================================================
// 0x80070458 - entity_rebuild_category_lists
// Rebuilds all entity category lists from scratch. Iterates the entity pool
// starting at g_entity_iter_start (DAT_80611b14), classifying each active
// entity by its type byte at +0x30 into category buckets.
//
// Skips entities with DEAD flag (bit 31 at +0x28), certain state flags
// (0x2101010 at +0x20), or specific condition bits on the entity's parent
// component (+0x24 -> +0x28). Dynamically grows category lists when they
// overflow using the list heap (DAT_80611c5c), max total 0x14CD entries.
//
// Category infrastructure:
//   DAT_8053f7d4[i] = desired capacity for category i
//   DAT_8053fb2c[i] = pointer to category i's entity list
//   DAT_805401dc[i] = current count for category i
// =============================================================================
void entity_rebuild_category_lists(void) {
    os_disable_interrupts();  // FUN_803826d8

    int heap_cursor = g_category_heap_cursor;  // DAT_80611c5c (saved)
    g_category_heap_cursor = 0;  // DAT_80611c58

    // Phase 1: Compute list offsets from capacities
    for (int i = 0; i < 214; i++) {  // 0xD6 categories
        if (g_category_capacities[i] == 0) {  // DAT_8053f7d4
            g_category_ptrs[i] = 0;           // DAT_8053fb2c
        } else {
            int offset = g_category_heap_cursor * 4;
            g_category_heap_cursor += g_category_capacities[i];
            g_category_ptrs[i] = heap_cursor + offset;
        }
    }

    // Save capacities backup, clear working counts
    memcpy(g_category_backup, g_category_capacities, 0x358);  // leaf_31e8
    memset(g_category_counts, 0, 0x358);  // DAT_805401dc

    // Phase 2: Classify entities
    int ent_offset = g_entity_iter_start * 0x2E8;  // DAT_80611b14
    int idx = g_entity_iter_start;

    while (idx < g_entity_count) {  // DAT_80611c70
        int ent = g_entity_pool + ent_offset;

        // Skip dead entities
        if ((*(u32*)(ent + 0x28) & 0x80000000) != 0) goto next;

        // Skip entities with certain state flags
        if ((*(u32*)(ent + 0x20) & 0x2101010) != 0) goto next;

        // Check parent component flags
        u32 parent_flags = *(u32*)(*(int*)(ent + 0x24) + 0x28);
        if ((parent_flags & 1) == 0) {
            if ((parent_flags & 4) == 0 && (parent_flags & 2) == 0 &&
                *(int*)(ent + 0x80) == 0) {
                goto next;
            }
        }

        // Classify by type categories (byte at +0x30 is category count)
        int num_categories = *(char*)(ent + 0x30);
        for (int c = 0; c < num_categories; c++) {
            int cat_id = *(int*)(ent + 0x38 + c * 4);
            int cur_count = g_category_counts[cat_id];  // DAT_805401dc

            if (cur_count < g_category_backup[cat_id]) {
                // Fits in current allocation
                *(int*)(g_category_ptrs[cat_id] + cur_count * 4) = ent;
                g_category_counts[cat_id]++;
            } else {
                // Need to grow — reallocate from heap
                // ... (grows by aligned-8 + 8, copies old data)
                *(int*)(g_category_ptrs[cat_id] + cur_count * 4) = ent;
                g_category_counts[cat_id]++;
            }
        }

    next:
        idx++;
        ent_offset += 0x2E8;
    }

    // Restore capacities from backup
    memcpy(g_category_capacities, g_category_backup, 0x358);
    os_restore_interrupts();  // FUN_80382724
}

// =============================================================================
// 0x80070648 - entity_system_reset
// Resets the entity system to initial state. Zeros all entity counters and
// list counts, clears the entity pool and category tables, then calls
// subsystem reset functions. Registers the entity damage handler on event
// mask 8, subtype 2. Fires event_invoke_type_handler(0, 0) to notify
// listeners of the reset.
// =============================================================================
void entity_system_reset(void) {
    g_entity_count = 0;          // DAT_80611c70
    g_update_list_count = 0;     // DAT_80611c64
    g_render_list_count = 0;     // DAT_80611c60
    g_special_list_count = 0;    // DAT_80611c68
    g_misc_counter = 0;          // DAT_80611c6c
    g_entity_system_active = 1;  // DAT_80611c54

    memset(g_category_capacities, 0, 0x358);   // DAT_8053f7d4
    memset(g_category_ptrs, 0, 0x358);         // DAT_8053fb2c
    memset(g_entity_pool, 0, (g_max_entity_count * 0x2E8 + 0xF) & ~0xF);
    memset(g_entity_type_table, 0, 0x1C0);     // DAT_8054088c area
    memset(g_secondary_buffer, 0, 400);        // DAT_80611c28

    // Subsystem resets
    FUN_80071e74();   // entity component reset
    FUN_80084268();   // AI reset
    FUN_8008273c();   // physics reset
    FUN_80087fd0();   // animation reset
    FUN_80242da8();   // script reset
    FUN_80098950();   // weapon reset
    FUN_8015cf24();   // effect reset

    DAT_80540a44 = 0;
    DAT_80611c50 = 0;
    DAT_80611c4c = 0;
    DAT_80611c2c = 0;

    event_invoke_type_handler(0, 0);  // notify: system reset
    FUN_80086fec();   // collision reset
    FUN_800ff9a8();   // nav reset
    FUN_80082474();   // spawn reset

    event_set_handler(8, 2, FUN_8001d9ec);  // register damage handler
    FUN_800774d8();   // final init
}

// =============================================================================
// 0x80070750 - entity_system_alloc
// Allocates all memory for the entity system. Normal levels use 0x226 (550)
// entity slots; mapmaker (level 0x65 = 101) uses only 0x14 (20).
// Allocations:
//   - Main pool: max_entities * 0x2E8 bytes
//   - Secondary buffer: 400 bytes
//   - Category list heap: 0x5340 bytes
// Then allocates subsystem memory and calls entity_system_reset.
// =============================================================================
void entity_system_alloc(void) {
    g_max_entity_count = 0x226;  // DAT_80611c20
    if (g_current_level_id == 0x65) {  // DAT_80612614 - mapmaker
        g_max_entity_count = 0x14;
    }

    g_entity_pool = mem_alloc(g_max_entity_count * 0x2E8, 1);     // DAT_80611c24
    g_secondary_buffer = mem_alloc(400, 1);                        // DAT_80611c28
    g_category_heap = mem_alloc(0x5340, 1);                        // DAT_80611c5c

    memset(g_category_backup, 0, 0x358);      // DAT_8053fe84
    memset(g_category_capacities, 0, 0x358);  // DAT_8053f7d4
    memset(g_category_ptrs, 0, 0x358);        // DAT_8053fb2c

    // Subsystem allocations
    FUN_8008429c();   // AI alloc
    FUN_80087f90();   // animation alloc
    FUN_8024293c();   // script alloc
    FUN_80098920();   // weapon alloc
    FUN_8015cec4();   // effect alloc
    FUN_80086fe8();   // collision alloc
    FUN_800ff9a4();   // nav alloc
    FUN_8008271c();   // physics alloc
    FUN_80071e98();   // component alloc
    FUN_8007c184();   // behavior alloc

    event_invoke_type_handler(1, 0);  // notify: system allocated
    entity_system_reset();            // FUN_80070648
}

// =============================================================================
// 0x80070834 - entity_system_shutdown
// Shuts down the entity system. If not in persistent mode (DAT_80611568),
// clears all pool pointers. Calls subsystem shutdowns and fires
// event_invoke_type_handler(2, 0) to notify listeners.
// =============================================================================
void entity_system_shutdown(void) {
    if (g_persistent_mode == 0) {  // DAT_80611568
        g_entity_pool = 0;      // DAT_80611c24
        DAT_80611ca0 = 0;
        DAT_80611cb0 = 0;
        g_secondary_buffer = 0; // DAT_80611c28
        DAT_80611cb4 = 0;
    }

    FUN_80082774();  // subsystem shutdown
    event_invoke_type_handler(2, 0);  // notify: system shutdown
    FUN_800824ac();  // final cleanup
}

// =============================================================================
// 0x80070888 - entity_type_get_class_info
// Returns the class info pointer (field at +0x10) for a given entity type
// index from the entity type table (DAT_806125e8, stride 0x30).
// =============================================================================
int entity_type_get_class_info(int type_index) {
    return *(int*)(g_entity_type_table + type_index * 0x30 + 0x10);  // DAT_806125e8
}

// =============================================================================
// 0x8007089c - entity_type_set_property
// Sets a property value (field at +4) for an entity type. The type index
// is resolved through a lookup table (DAT_805c9858, short entries) to
// get the actual row in the entity type table (DAT_806125e8, stride 0x30).
// =============================================================================
void entity_type_set_property(int lookup_index, int value) {
    int type_idx = *(s16*)(g_entity_type_lookup + lookup_index * 2);  // DAT_805c9858
    *(int*)(g_entity_type_table + type_idx * 0x30 + 4) = value;      // DAT_806125e8
}

// =============================================================================
// 0x800708c0 - entity_register_for_update
// Registers or unregisters an entity for a specific update list. Sets the
// frame timestamp at entity+0x90 and the flag at entity+(list_type*4+0x154).
//
// List types:
//   0 = think/update list (DAT_8054088c, count at DAT_80611c64)
//   8 = render list (DAT_80540d2c, count at DAT_80611c60)
//   9 = special list (DAT_8053f450, count at DAT_80611c68)
//
// If flag (param_3) is non-zero: adds to list (with duplicate check).
// If flag is zero: removes from list (shifts remaining entries).
// =============================================================================
void entity_register_for_update(int entity_ptr, int list_type, int flag) {
    *(int*)(entity_ptr + 0x90) = g_frame_timestamp;  // DAT_80611928
    *(int*)(entity_ptr + list_type * 4 + 0x154) = flag;

    if (list_type == 0) {
        // Update list management
        for (int i = 0; i < g_update_list_count; i++) {
            if (g_update_list[i] == entity_ptr) {
                if (flag != 0) return;  // already registered
                // Remove: shift remaining
                memcpy(&g_update_list[i], &g_update_list[i + 1],
                       (g_update_list_count - (i + 1)) * 4);
                g_update_list_count--;
                return;
            }
        }
        if (flag != 0) {
            g_update_list[g_update_list_count] = entity_ptr;
            g_update_list_count++;
        }
    }

    if (list_type == 8) {
        // Render list management (same pattern)
        for (int i = 0; i < g_render_list_count; i++) {
            if (g_render_list[i] == entity_ptr) {
                if (flag != 0) return;
                memcpy(&g_render_list[i], &g_render_list[i + 1],
                       (g_render_list_count - (i + 1)) * 4);
                g_render_list_count--;
                return;
            }
        }
        if (flag != 0) {
            g_render_list[g_render_list_count] = entity_ptr;
            g_render_list_count++;
        }
    }

    if (list_type == 9) {
        // Special list: delegate to add/remove helpers
        if (flag == 0) {
            entity_special_list_remove(entity_ptr);  // FUN_80070ab4
        } else {
            entity_special_list_add(entity_ptr);     // FUN_80070a54
        }
    }
}

// =============================================================================
// 0x80070a54 - entity_special_list_add
// Adds an entity to the special entity list (DAT_8053f450). Checks for
// duplicates before adding. DAT_80611c68 tracks the count.
// =============================================================================
void entity_special_list_add(int entity_ptr) {
    // Duplicate check
    for (int i = 0; i < g_special_list_count; i++) {
        if (g_special_list[i] == entity_ptr) {
            return;  // already in list
        }
    }

    // Append
    g_special_list[g_special_list_count] = entity_ptr;
    g_special_list_count++;  // DAT_80611c68
}

// =============================================================================
// 0x80070ab4 - entity_special_list_remove
// Removes an entity from the special entity list (DAT_8053f450). Shifts
// remaining entries to fill the gap. DAT_80611c68 tracks the count.
// =============================================================================
void entity_special_list_remove(int entity_ptr) {
    for (int i = 0; i < g_special_list_count; i++) {
        if (g_special_list[i] == entity_ptr) {
            // Shift remaining entries down
            memcpy(&g_special_list[i], &g_special_list[i + 1],
                   (g_special_list_count - (i + 1)) * 4);
            g_special_list_count--;
            return;
        }
    }
}
