// Address: 0x800d4b1c - 0x800d4e70 | Name: particle_slot_ops | Date: 2026-08-11
// General particle effect system: slot allocation, property setters, visibility
// checks, and the primary spawn function. Shared pool (100 slots x 0xD0) used by
// weather, muzzle flashes, death effects, explosions, AI impacts, etc.
// Pool globals defined in weather_system.cpp (batch 233).

#include "common.h"
#include "memory.h"
#include "scene.h"

// ============================================================================
// External globals (from particle pool - batch 233)
// ============================================================================

extern int*   g_particle_free_head;         // DAT_80611dec
extern int*   g_particle_active_heads[40];  // DAT_8056e010
extern int    g_particle_active_counts[40]; // DAT_8056e0b0
extern int    g_particle_dirty;             // DAT_806107e4
extern float  g_particle_global_intensity;  // FLOAT_80611df4
extern float  g_particle_intensity_max;     // FLOAT_80611e04

// Per-type config table: [+0] max_count (or -1 = unlimited), [+4] alloc_size, ...
extern int    g_particle_type_config[];     // DAT_80406e24, stride 0x10
// Lifetime table per sub-type: at DAT_804070a4, stride 0x90
extern unsigned int g_particle_lifetime_table[]; // DAT_804070a4

extern int    g_player_slot;                // DAT_80611d54

// ============================================================================
// particle_slot_alloc
// Address: 0x800d4b1c
// Allocates a particle slot from the free pool. If pool is full, evicts the
// oldest slot from the requested type (or any type if none available).
// Links new slot into per-type active list at head. Sets up default fields.
// Optionally allocates extended data block via block_alloc_dispatch.
// 20+ callers across particle/muzzle/death/explosion/weather systems.
// ============================================================================
int* particle_slot_alloc(void) {
    // FUN_803826ec returns packed: type_index in upper 32, alloc_flag in lower 32
    unsigned long long packed = FUN_803826ec();
    int type_index = (int)(packed >> 32);
    int alloc_flag = (int)packed;

    int max_count = g_particle_type_config[type_index * 4]; // stride 0x10, offset +0

    // Check if we need to evict
    if (max_count == -1 || g_particle_active_counts[type_index] < max_count) {
        // Over limit for this type — evict oldest from this type
        int* oldest = particle_slot_list_traverse(type_index);
        particle_slot_destroy(oldest);
    } else {
        // Pool exhausted — find any type with slots and evict
        if (g_particle_free_head == NULL) {
            int search_type = 0;
            int* victim = NULL;
            while (victim == NULL) {
                victim = particle_slot_list_traverse(search_type);
                search_type++;
            }
            particle_slot_destroy(victim);
        }
    }

    // Pop from free list
    int* slot = g_particle_free_head;
    g_particle_free_head = (int*)g_particle_free_head[0x19]; // next in free list
    if (g_particle_free_head != NULL) {
        *(int*)((char*)g_particle_free_head + 0x60) = 0; // clear prev
    }

    // Insert at head of per-type active list
    slot[0x19] = (int)g_particle_active_heads[type_index]; // next = old head
    if (slot[0x19] != 0) {
        *(int**)(slot[0x19] + 0x60) = slot; // old_head->prev = slot
    }
    slot[0x18] = 0; // prev = NULL (we are new head)

    g_particle_active_heads[type_index] = slot;
    g_particle_active_counts[type_index]++;

    // Initialize slot fields
    slot[0] = type_index;            // type
    slot[5] = 0;                     // entity back-reference
    slot[0xd] = 0xffff;             // flags/sentinel
    float default_val = 1.0f;        // FLOAT_80614ee0
    slot[0xe] = *(int*)&default_val; // field_0x38 (scale default)
    slot[0xc] = *(int*)&default_val; // opacity
    slot[0xb] = *(int*)&default_val; // alpha
    slot[0x33] = 0;                  // extended state
    slot[0x26] = 0;                  // animation frame
    slot[6] = 0;                     // extended data ptr

    // Allocate extended data block if requested
    if (alloc_flag != 0) {
        int alloc_size = g_particle_type_config[type_index * 4 + 1]; // offset +4
        int* ext_data = block_alloc_dispatch(alloc_size, 1, slot, particle_slot_destroy);
        slot[6] = (int)ext_data;
    }

    g_particle_dirty = 1;
    FUN_80382738(); // release lock/context

    return slot;
}

// ============================================================================
// particle_slot_set_scale
// Address: 0x800d4c88
// Sets the scale/size of a particle slot (offset +0x38).
// 5 callers: death_effects, muzzle_flash, AI explosion, etc.
// ============================================================================
void particle_slot_set_scale(float scale, int* slot) {
    if (slot == NULL) {
        return;
    }
    *(float*)((char*)slot + 0x38) = scale;
}

// ============================================================================
// particle_slot_set_color
// Address: 0x800d4c98
// Sets RGB color of a particle slot (vec3 at offset +0x3c/0x40/0x44).
// 3 callers: muzzle_flash_update_tick, particle render functions.
// ============================================================================
void particle_slot_set_color(int* slot, float* color) {
    if (slot == NULL) {
        return;
    }
    *(float*)((char*)slot + 0x3c) = color[0];
    *(float*)((char*)slot + 0x40) = color[1];
    *(float*)((char*)slot + 0x44) = color[2];
}

// ============================================================================
// particle_set_global_intensity
// Address: 0x800d4cbc
// Sets the global particle intensity multiplier (g_particle_global_intensity).
// 1 caller (menu/settings system).
// ============================================================================
void particle_set_global_intensity(float intensity) {
    g_particle_global_intensity = intensity;
}

// ============================================================================
// particle_slot_set_opacity
// Address: 0x800d4cc4
// Sets opacity on a particle slot with clamping to [1.0, max].
// For specific types (0x27, 0x1c-0x1d, 0x23, 0x24, 0xd, 0xe), also writes
// opacity into the extended data block at type-specific offsets.
// 19+ callers across all particle subsystems.
// ============================================================================
void particle_slot_set_opacity(float opacity, int* slot) {
    if (slot == NULL) {
        return;
    }

    float min_val = 1.0f;  // FLOAT_80614ee0
    float max_val = 2.0f;  // FLOAT_80614eec (example max)

    if (opacity > min_val) {
        // Clamp to max
        float clamped = (opacity < max_val) ? opacity : max_val;
        slot[0xc] = *(int*)&clamped;

        int type = slot[0];
        // Types that store opacity in extended data at +0x2ac
        if (type == 0x27 || (type >= 0x1c && type <= 0x1d) || type == 0x23 || type == 0x24) {
            *(float*)(slot[6] + 0x2ac) = clamped;
        }
        // Types that store opacity in extended data at +0x138
        if (type == 0xd || type == 0xe) {
            *(float*)(slot[6] + 0x138) = clamped;
        }
        return;
    }

    // Below minimum — set to min
    slot[0xc] = *(int*)&min_val;
}

// ============================================================================
// particle_slot_set_position
// Address: 0x800d4d4c
// Sets world position of a particle slot (vec3 at offset +0x20/0x24/0x28).
// 12+ callers: muzzle_flash, AI particles, spawn trail, etc.
// ============================================================================
void particle_slot_set_position(int* slot, float* position) {
    if (slot == NULL) {
        return;
    }
    *(float*)((char*)slot + 0x20) = position[0];
    *(float*)((char*)slot + 0x24) = position[1];
    *(float*)((char*)slot + 0x28) = position[2];
}

// ============================================================================
// particle_slot_is_alive
// Address: 0x800d4d70
// Checks if a particle slot is still alive based on its frame counter vs the
// max lifetime from the type's configuration table. Only applies to types
// 0x13, 0x14, 0x26 (animated/timed particles). Returns 1 if alive or N/A type.
// 2 callers.
// ============================================================================
int particle_slot_is_alive(int* slot) {
    if (slot != NULL) {
        int type = slot[0];
        if (type == 0x13 || type == 0x14 || type == 0x26) {
            // Compare current frame (slot[7]) against max lifetime from table
            unsigned int max_life = g_particle_lifetime_table[slot[1] * 0x24]; // stride 0x90, offset +0x4 => index * 0x24 words
            int current = slot[7];
            // Returns: sign(current) + (current >= max_life) - sign(max_life)
            // Effectively: 1 if alive (current < max_life), 0 if expired
            return (current >> 31) + ((unsigned int)current >= max_life) - ((int)max_life >> 31);
        }
    }
    return 1; // alive by default for other types
}

// ============================================================================
// particle_slot_is_visible
// Address: 0x800d4dd4
// Visibility check for a particle slot. Checks:
//   - Bit 0x10000 set (active/enabled)
//   - Scale (offset +0x38) equals 1.0 (FLOAT_80614ee0 — i.e. not hidden)
//   - Not marked 0x200000 (force-hidden)
//   - Player visibility bit matches current player
//   - Optional 0x80000 flag with group mask check
// 3 callers.
// ============================================================================
int particle_slot_is_visible(int slot_addr, unsigned int group_mask) {
    unsigned int flags = *(unsigned int*)(slot_addr + 8);

    if ((flags & 0x10000) == 0) {
        return 0; // not active
    }
    if (*(float*)(slot_addr + 0x38) != 1.0f) {
        return 0; // scale != default (hidden)
    }
    if ((flags & 0x200000) != 0) {
        return 0; // force-hidden
    }
    // Check per-player visibility bit
    int player_index = *(int*)(g_player_slot + 0x214);
    if ((*(unsigned int*)(slot_addr + 0x34) & (1 << player_index)) == 0) {
        return 0; // not visible to this player
    }
    // Optional group mask check
    if ((flags & 0x80000) != 0) {
        if ((flags & group_mask) == 0) {
            return 0;
        }
    }

    return 1;
}

// ============================================================================
// particle_slot_is_visible_ext
// Address: 0x800d4e34
// Extended visibility check using flags from a secondary structure (offset +0x44
// of param_2). Simplified version without player bit or force-hidden checks.
// 1 caller.
// ============================================================================
int particle_slot_is_visible_ext(int slot_addr, int ext_struct, unsigned int group_mask) {
    unsigned int flags = *(unsigned int*)(ext_struct + 0x44);

    if ((flags & 0x10000) == 0) {
        return 0; // not active
    }
    if (*(float*)(slot_addr + 0x38) != 1.0f) {
        return 0; // hidden scale
    }
    // Group mask check
    if ((flags & 0x80000) != 0) {
        if ((flags & group_mask) == 0) {
            return 0;
        }
    }

    return 1;
}

// ============================================================================
// particle_slot_spawn
// Address: 0x800d4e70
// MASSIVE primary particle spawner. Allocates a slot via particle_slot_alloc,
// populates initial position from source + random offsets (RNG function
// FUN_801310c0), computes bounding box for multi-player scene query, and
// queries scene nodes in the particle's AABB for room assignment.
// 2 callers: player_ai_impact_dispatch, FUN_8029e770.
// ============================================================================
void particle_slot_spawn(int param_unused1, int param_unused2, float* source_position) {
    unsigned long long packed = FUN_803826e0();
    float* spawn_origin = (float*)packed;
    int type_sub_index = (int)(packed >> 32);

    // Allocate a slot
    int* slot = particle_slot_alloc();
    float* slot_data = *(float**)(slot + 0x18); // extended position array ptr

    slot[0x14] = type_sub_index; // sub-type index

    int player_count = FUN_801c1714();
    float spread = FLOAT_80614ef0;

    // Set flag: multiplayer particles get 0x80000 bit
    *(unsigned int*)((char*)slot + 8) = (~(unsigned int)(-(player_count == 1))) & 0x80000;

    float base_opacity = 1.0f;   // FLOAT_80614ee0
    *(float*)((char*)slot + 0xc) = base_opacity;  // opacity
    *(float*)((char*)slot + 0x10) = spread;       // spread radius
    slot[1] = -1;                                  // sub-type sentinel
    float intensity_scale = FLOAT_80614ef4;
    *(int*)((char*)slot + 0x1c) = (int)(intensity_scale * g_particle_intensity_max); // particle count

    // Copy source position to slot data
    slot_data[0] = spawn_origin[0];
    slot_data[1] = spawn_origin[1];
    slot_data[2] = spawn_origin[2];

    // Generate randomized particle positions
    float rng_scale = FLOAT_80614ef8;
    float rng_offset = FLOAT_80614efc;
    float height_offset = FLOAT_80614ee4;

    int byte_offset = 0;
    int max_particles = *(int*)((char*)slot + 0x1c);

    for (int i = 0; i < max_particles; i++) {
        float rng_x = FUN_801310c0(); // random [0..1]
        *(float*)((char*)slot_data + byte_offset + 0xc) =
            rng_scale * (source_position[0] + rng_x) - rng_offset;

        float rng_y = FUN_801310c0();
        *(float*)((char*)slot_data + byte_offset + 0x10) =
            rng_scale * (source_position[1] + rng_y) - height_offset;

        float rng_z = FUN_801310c0();
        *(float*)((char*)slot_data + byte_offset + 0x14) =
            rng_scale * (source_position[2] + rng_z) - rng_offset;

        byte_offset += 0xc;
    }

    // Multi-player: compute AABB and query scene nodes for room assignment
    if ((*(unsigned int*)((char*)slot + 8) & 0x80000) != 0) {
        float base_offset = FLOAT_80614ed8;

        // Compute min/max bounds from position offsets
        float y_low  = rng_scale * source_position[1] - height_offset;
        float y_high = rng_scale * (base_offset + source_position[1]) - height_offset;
        float vel_low  = spread * y_low + FLOAT_80614f04;
        float vel_high = spread * y_high + FLOAT_80614f04;

        // AABB min
        *(float*)((char*)slot + 0x80) = spread * (rng_scale * source_position[0] - rng_offset) + spawn_origin[0];
        float min_y = (vel_low < vel_high) ? vel_low : vel_high;
        *(float*)((char*)slot + 0x84) = spawn_origin[1] + ((min_y < 1.0f) ? min_y : (vel_low < vel_high ? vel_low : vel_high));
        *(float*)((char*)slot + 0x88) = spread * (rng_scale * source_position[2] - rng_offset) + spawn_origin[2];

        // AABB max
        *(float*)((char*)slot + 0x8c) = spread * (rng_scale * (base_offset + source_position[0]) - rng_offset) + spawn_origin[0];
        float max_y = (vel_high > vel_low) ? vel_high : vel_low; // squared comparison
        *(float*)((char*)slot + 0x90) = spawn_origin[1] + max_y;
        *(float*)((char*)slot + 0x94) = spread * (rng_scale * (base_offset + source_position[2]) - rng_offset) + spawn_origin[2];

        // Query scene for nodes within this AABB
        int node_count = scene_query_nodes_in_frustum(0, (float*)((char*)slot + 0x80),
                                                      (int*)((char*)slot + 0x9c), 0xc);
        *(int*)((char*)slot + 0x98) = node_count;
    }

    FUN_8038272c(); // release context
}
