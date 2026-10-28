// Address: 0x800825e8 - 0x80082d3c | Name: mover_system | Date: 2026-08-10
// Moving platform / mover entity system.
// Handles level-placed movers (elevators, platforms, doors):
// spawning from level data, zone portal flag management,
// position updates for soundpad zones, and entity lookup.

#include "common.h"

// ============================================================================
// Globals
// ============================================================================

static int g_mover_count;                    // DAT_80611c98
static char g_mover_table[0x1e78];           // DAT_8055d6f8 - array of 0x30c-byte mover structs
// Each mover struct at offset:
//   +0x000: entity_id
//   +0x004: flags
//   +0x008: type (0 or 3)
//   +0x2f4: zone count (puVar9[0xbd])
//   +0x2f8: zone active flag (puVar9[0xbe])
//   +0x308: entity pointer (puVar9[0xc2])

// ============================================================================
// External dependencies
// ============================================================================

extern int* soundpad_resolve_zone_node(int zone_id);
extern int  soundpad_get_entity_zone_id(int zone_ptr);
extern int  soundpad_get_inter_portal_list(int zone_id, int* out_list);
extern int  soundpad_get_entity_data(int id, int* out_list);
extern void entity_preload_resources(int entity);
extern int  entity_spawn_by_type(/* ... */);
extern void entity_register_for_update(int entity, int type, int priority);
extern void render_instance_set_material(float val, int* render_inst, int slot);
extern unsigned int spawn_check_flags_or_cutscene(unsigned int flags);
extern int  level_get_info_cached(int level_id);
extern void memset_wrapper(int dest, int val, int size);

// ============================================================================
// Address: 0x800825e8 | Name: mover_flag_portals_by_height
// Sets portal flag 0x10000 on all portals in the zone containing this mover.
// Used to mark portals as "active" when a mover is in position.
// ============================================================================
void mover_flag_portals_by_height(int mover_data) {
    int* zone_node = soundpad_resolve_zone_node(*(int*)(mover_data + 0xb4));
    int zone_id = soundpad_get_entity_zone_id((int)zone_node);

    int portal_list[1];
    int count = soundpad_get_inter_portal_list(zone_id, portal_list);

    int offset = 0;
    for (int i = 0; i < count; i++) {
        int portal_index = *(int*)(portal_list[0] + offset);
        int portal_flags_addr = g_portal_table + portal_index * 0x14;
        *(unsigned int*)portal_flags_addr |= 0x10000;  // mark portal active
        offset += 4;
    }
}

// ============================================================================
// Address: 0x8008264c | Name: mover_update_portal_flags
// Conditionally sets/clears portal flag 0x10000 based on height difference
// between connected zones. If zone Y positions are within threshold (FLOAT_80614210),
// sets flag; otherwise clears it.
// Called per-frame for active movers to update portal passability.
// ============================================================================
void mover_update_portal_flags(void) {
    int entity = /* resolved from register state */;
    int* zone_node = soundpad_resolve_zone_node(*(int*)(entity + 0xb4));
    int zone_id = soundpad_get_entity_zone_id((int)zone_node);

    int portal_list[12];
    int count = soundpad_get_inter_portal_list(zone_id, portal_list);

    float height_threshold = FLOAT_80614210;
    for (int i = 0; i < count; i++) {
        unsigned int* portal = (unsigned int*)(g_portal_table + *(int*)(portal_list[0] + i * 4) * 0x14);
        int* zone_a = soundpad_resolve_zone_node(portal[1]);
        int* zone_b = soundpad_resolve_zone_node(portal[2]);

        float height_diff = (float)zone_a[4] - (float)zone_b[4];
        if (ABS(height_diff) <= height_threshold) {
            *portal |= 0x10000;    // zones aligned - portal passable
        } else {
            *portal &= ~0x10000;   // zones misaligned - portal blocked
        }
    }
}

// ============================================================================
// Address: 0x8008271c | Name: mover_system_init_wrapper
// Wrapper that calls mover_system_clear. Used as init entry point.
// ============================================================================
void mover_system_init_wrapper(void) {
    mover_system_clear();
}

// ============================================================================
// Address: 0x8008273c | Name: mover_system_clear
// Resets the mover system: zeroes the count and clears the entire
// mover table (0x1e78 bytes = 10 movers * 0x30c bytes each).
// Called from entity_system_reset.
// ============================================================================
void mover_system_clear(void) {
    g_mover_count = 0;
    memset_wrapper((int)&g_mover_table, 0, 0x1e78);
}

// ============================================================================
// Address: 0x80082774 | Name: mover_system_update_zones
// Per-frame update: iterates all active movers and updates their
// associated soundpad zone node positions from the mover data.
// Only processes movers that have zone references and are active.
// Called from entity_system_shutdown (cleanup pass).
// ============================================================================
void mover_system_update_zones(void) {
    if (g_level_active == 0) return;

    for (int i = 0; i < g_mover_count; ) {
        int* mover = (int*)(&g_mover_table + i * 0xc3);  // 0x30c / 4 = 0xc3 dwords

        if (mover[0x2f4/4] > 0 && mover[0x2f8/4] != 0) {
            // Update zone positions from mover data
            int offset = 0;
            for (int j = 0; j < mover[0x2f4/4]; j++) {
                int zone_id = *(int*)((int)mover + offset + 0xb4);
                int* zone_node = soundpad_resolve_zone_node(zone_id);
                zone_node[3] = *(int*)((int)mover + offset + 0xb8);  // pos X
                zone_node[4] = *(int*)((int)mover + offset + 0xbc);  // pos Y
                zone_node[5] = *(int*)((int)mover + offset + 0xc0);  // pos Z
                offset += 0x10;
            }
            mover_flag_portals_by_height((int)mover);
        }
        i++;
    }
}

// ============================================================================
// Address: 0x80082838 | Name: mover_preload_resources
// Event handler wrapper: calls entity_preload_resources for the entity
// referenced in param_9[0]. Used as a preload callback.
// ============================================================================
void mover_preload_resources(int* param_9) {
    entity_preload_resources(param_9[0]);
}

// ============================================================================
// Address: 0x8008285c | Name: mover_spawn_all_from_level
// MASSIVE: Spawns all mover entities from the current level's definition table.
// Iterates level_info->mover_array (entries are 0xac bytes each).
// For each valid entry (passes spawn flag check, not cutscene-only):
//   1. Spawns entity via entity_spawn_by_type (type 0 or type 3)
//   2. Calls OSCreateThread for mover logic thread
//   3. Allocates 0x30c-byte mover data from g_mover_table
//   4. Copies spawn definition fields into mover data
//   5. Computes normalized direction vector between start/end positions
//   6. Resolves soundpad zone associations if present
//   7. Registers for multiple update types (think, render, special)
//   8. Sets render material and flags
// Called from level_spawn_all_entities during level load.
// ============================================================================
void mover_spawn_all_from_level(void) {
    int level_info = level_get_info_cached(g_current_level_id);
    int mover_def_count = *(int*)(level_info + 0xa8);
    int mover_def_base = *(int*)(level_info + 0xa4);

    int def_offset = 0;
    for (int i = 0; i < mover_def_count; i++) {
        int* def = (int*)(mover_def_base + def_offset);
        int entity = 0;

        // Check spawn flags
        if ((def[2] & 0x10000000) != 0) goto next;
        unsigned int valid = spawn_check_flags_or_cutscene(def[2]);
        if (valid == 0) goto next;

        // Spawn entity based on mover type
        if (def[5] == 0) {
            entity = entity_spawn_by_type(def[0], &def[8], (float*)&def[0xe], 0, 0);
        } else if (def[5] == 3) {
            entity = entity_spawn_by_type(def[0], &def[0xb], (float*)&def[0xe], 0, 0);
        }

        // Initialize mover thread
        OSCreateThread(entity, def[2], -1);
        FUN_8023c8a0(def[1], entity);

        // Allocate mover data slot
        int slot = g_mover_count * 0xc3;
        g_mover_count++;
        *(int*)(entity + 0x7c) = (int)(&g_mover_table + slot);
        memset_wrapper(*(int*)(entity + 0x7c), 0, 0x30c);

        int* mover = *(int**)(entity + 0x7c);

        // Copy definition data to mover struct
        mover[0x19] = def[0x17];  // end position X
        mover[0x1a] = def[0x18];  // end position Y
        mover[0x1b] = def[0x19];  // end position Z
        mover[0x0f] = def[0x0b];  // waypoint 1
        mover[0x10] = def[0x0c];
        mover[0x11] = def[0x0d];
        mover[0x0c] = def[0x08];  // start position
        mover[0x0d] = def[0x09];
        mover[0x0e] = def[0x0a];
        mover[0x02] = def[0x05];  // type
        mover[0x01] = def[0x04];  // flags

        // Set directional flag
        mover[0x1d] = ((mover[1] & 2) != 0) ? 1 : 0;

        // Speed and entity ID
        mover[0x00] = def[1];     // entity ID
        mover[0x05] = *(int*)&FLOAT_80614214;  // default speed
        mover[0x0b] = def[7];     // sound ID
        mover[0x0a] = def[6];     // anim ID

        // Movement params
        mover[0x12] = def[0x11];  // accel
        mover[0x14] = def[0x13];  // decel
        mover[0x13] = def[0x12];  // max speed
        mover[0x15] = def[0x14];  // wait time
        mover[0x16] = def[0x15];  // delay
        mover[0x17] = def[0x16];  // trigger flags
        mover[0x1c] = def[0x1a];  // linked mover
        mover[0x18] = def[3];     // path ID

        // Extra data
        mover[0xbf] = def[0x28];
        mover[0xc0] = def[0x29];
        mover[0xc2] = entity;     // back-reference
        mover[0xc1] = def[0x2a];
        mover[0x2a] = *(int*)&FLOAT_80614214;
        mover[0x2b] = *(int*)&FLOAT_80614214;

        // Compute direction vector (start -> end)
        float dx = (float)mover[0x0c] - (float)mover[0x0f];
        float dy = (float)mover[0x0d] - (float)mover[0x10];
        float dz = (float)mover[0x0e] - (float)mover[0x11];
        float dist_sq = dx*dx + dy*dy + dz*dz;
        float dist = sqrt(dist_sq);
        mover[6] = *(int*)&dist;

        // Normalize direction
        if (dist_sq > EPSILON) {
            float inv_dist = fast_inv_sqrt(dist_sq) * 0.5f;
            mover[7] = *(int*)&(dx * inv_dist);
            mover[8] = *(int*)&(dy * inv_dist);
            mover[9] = *(int*)&(dz * inv_dist);
        }

        // Copy path waypoints (4 entries of 4 bytes each, x2)
        for (int j = 0; j < 4; j++) {
            *(int*)((int)mover + j*4 + 0x78) = *(int*)((int)def + j*4 + 0x6c);
            *(int*)((int)mover + j*4 + 0x88) = *(int*)((int)def + j*4 + 0x7c);
        }
        for (int j = 0; j < 4; j++) {
            *(int*)((int)mover + j*4 + 0x98) = *(int*)((int)def + j*4 + 0x8c);
        }

        // Resolve soundpad zone associations
        if ((int)def[0x27] >= 0) {
            int zone_list[12];
            int zone_count = soundpad_get_entity_data(def[0x27], zone_list);
            for (int j = 0; j < zone_count; j++) {
                mover[0xbd]++;  // increment zone count
                int zone_id = *(int*)(zone_list[0] + j * 4);
                *(int*)((int)mover + j*0x10 + 0xb4) = zone_id;
                // Store zone positions
                *(int*)((int)mover + j*0x10 + 0xb8) = *(int*)(g_zone_data + zone_id * 0x20 + 0x0c);
                *(int*)((int)mover + j*0x10 + 0xbc) = *(int*)(g_zone_data + zone_id * 0x20 + 0x10);
                *(int*)((int)mover + j*0x10 + 0xc0) = *(int*)(g_zone_data + zone_id * 0x20 + 0x14);
                mover[0xbe] = 1;  // mark zones active
            }
            mover[0x2c] = def[0x27];
            FUN_80082e44(FLOAT_80614214, FLOAT_80614214, FLOAT_80614214);
        }

        // Set render flags and material
        if (mover[2] == 3) {
            *(int*)(*(int*)(entity + 4) + 0x30) = mover[0x13];
        } else {
            *(int*)(*(int*)(entity + 4) + 0x30) = mover[0x14];
        }
        FUN_800839f8();  // mover render setup

        // Register for entity updates
        if ((mover[1] & 0x10) != 0) {
            *(unsigned int*)(entity + 0x28) |= 4;
            *(float*)(entity + 0x260) = FLOAT_80614224;
            entity_register_for_update(entity, 0, -0x7ff7bf54);
        }
        render_instance_set_material(FLOAT_80614214, *(int**)(entity + 4), 1);
        entity_register_for_update(entity, 3, -0x7ff7cf98);
        if ((mover[1] & 0x10) != 0 || (mover[1] & 0x40) != 0) {
            entity_register_for_update(entity, 2, -0x7ff7c464);
        }

        *(unsigned int*)(entity + 0x28) &= ~0x4020000;

        // Start path if defined
        if (mover[0x18] != -1) {
            FUN_80082f14();  // mover_start_path
        }

    next:
        def_offset += 0xac;
    }
}

// ============================================================================
// Address: 0x80082d3c | Name: mover_find_by_entity_id
// Linear search through the mover table for a mover matching the given entity ID.
// Returns the mover's secondary data field (at +0x308 offset = 0x30c-4),
// or 0 if not found.
// Each mover entry is 0x30c bytes; searches DAT_8055d6f8 base.
// ============================================================================
int mover_find_by_entity_id(int entity_id) {
    int offset = 0;
    int remaining = g_mover_count;

    if (remaining > 0) {
        do {
            if (entity_id == *(int*)((int)&g_mover_table + offset)) {
                return *(int*)((int)&g_mover_table + offset + 0x308);
            }
            offset += 0x30c;
            remaining--;
        } while (remaining != 0);
    }
    return 0;
}
