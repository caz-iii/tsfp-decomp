// =============================================================================
// Character Effects & Collision System
// Address: 0x800f36ec - 0x800f3de0 | Batch 304 | Date: 2026-08-25
// =============================================================================
// Manages character visual effects (particles, muzzle flash, smoke) and
// collision sub-object configuration. Characters have multiple collision
// sub-objects (body, limbs, weapon attachments) that can be individually
// configured with flags and shape parameters. Particle effects track bone
// positions and use opacity fade/spawn cycling.
// =============================================================================

#include "common.h"
#include "entity.h"
#include "collision.h"
#include "particle.h"
#include "math_util.h"

// --- Constants ---
#define CHARACTER_ENTITY_TYPE       0x35
#define MAX_COLLISION_OBJECTS       8
#define PARTICLE_SLOTS_PER_CHAR     4

// Collision object collection buffer
static int g_collision_object_list[MAX_COLLISION_OBJECTS]; // 0x805719a8

// Character FX callback handle
static u32 g_character_fx_callback_handle; // 0x80611f28

// Per-player particle effect type IDs
static int g_player_particle_type[4]; // 0x80610810

// --- Constants (rodata) ---
// CONST_EFFECT_INTERVAL   = 0x806153b0  (spawn interval)
// CONST_OPACITY_FULL      = 0x806153c0  (1.0 opacity)
// CONST_FADE_RATE         = 0x806153cc  (fade multiplier)
// CONST_RESPAWN_BASE      = 0x806153c4  (min respawn delay)
// CONST_RESPAWN_RANGE     = 0x806153c8  (random respawn range)
// CONST_SCALE_FACTOR      = 0x806153b4  (entity scale factor)
// CONST_SCALE_DIVISOR     = 0x806153b8  (entity scale divisor)
// CONST_PLAYER1_SCALE     = 0x806153bc  (player 1 color scale)

// Particle direction table (6 entries x 4 bytes per direction slot)
// 0x803cc980 - base position offsets for particle spawn

// =============================================================================
// Address: 0x800f36ec | Name: character_fx_stub_nop
// Empty stub. Placeholder callback that performs no operation.
// =============================================================================
void character_fx_stub_nop(void) {
    return;
}

// =============================================================================
// Address: 0x800f36f0 | Name: character_fx_preload_resources
// Preloads entity type 0x35 resources. Type 0x35 is the character weapon/held
// item model used by the effects system.
// =============================================================================
void character_fx_preload_resources(void) {
    entity_preload_resources(CHARACTER_ENTITY_TYPE);
}

// =============================================================================
// Address: 0x800f3714 | Name: character_fx_register_callback
// Registers a timed callback for the character FX system with the engine
// scheduler. Calls character_fx_init_weapon to get the callback pointer,
// then registers it. Stores the returned scheduler handle for later removal.
// =============================================================================
void character_fx_register_callback(void) {
    void* callback = character_fx_init_weapon();
    u16 handle = scheduler_register_callback(callback, -0x7ff0bd1c, 1);
    g_character_fx_callback_handle = (u32)handle;
}

// =============================================================================
// Address: 0x800f3748 | Name: character_fx_init_weapon
// Wrapper that initializes character weapon effects by calling the main
// weapon FX init function (0x800f61bc). Used as a callback getter for the
// scheduler registration system.
// =============================================================================
void character_fx_init_weapon(void) {
    character_weapon_fx_init(); // FUN_800f61bc
}

// =============================================================================
// Address: 0x800f3768 | Name: character_collect_collision_objects
// Collects collision sub-objects from a character entity into a flat array.
// Filters by bit flags in param_2:
//   bit 0: body root (offset 0x0) + weapon attachment (offset 0xbc4)
//   bit 1: limb sub-objects (offsets 0x18c, 0x2bc, 0x80)
//   bit 2: held weapon sub-objects (via entity+0x150 -> offsets 0x1d8, 0x538, 0xd1c)
// Returns the count of collected non-null objects.
// =============================================================================
int character_collect_collision_objects(int* char_data, u32 flags) {
    int count = 0;
    int entity = *(int*)*char_data;

    // Bit 0: body + weapon attachment
    if (flags & 1) {
        g_collision_object_list[count++] = entity;
        if (*(int*)(*char_data + 0xbc4) != 0) {
            g_collision_object_list[count++] = *(int*)(*char_data + 0xbc4);
        }
    }

    // Bit 1: limb sub-objects
    if (flags & 2) {
        if (*(int*)(*char_data + 0x18c) != 0) {
            g_collision_object_list[count++] = *(int*)(*char_data + 0x18c);
        }
        if (*(int*)(*char_data + 0x2bc) != 0) { // offset 700
            g_collision_object_list[count++] = *(int*)(*char_data + 0x2bc);
        }
        if (*(int*)(*char_data + 0x80) != 0) {
            g_collision_object_list[count++] = *(int*)(*char_data + 0x80);
        }
    }

    // Bit 2: held weapon sub-objects (via weapon entity pointer)
    if ((flags & 4) && *(int*)(entity + 0x150) != 0) {
        int weapon_entity = *(int*)(entity + 0x150);

        int sub_a = *(int*)(weapon_entity + 0x1d8);
        if (sub_a != 0) {
            g_collision_object_list[count++] = sub_a;
        }

        int sub_b = *(int*)(weapon_entity + 0x538);
        if (sub_b != 0) {
            g_collision_object_list[count++] = sub_b;
        }

        int sub_c = *(int*)(weapon_entity + 0xd1c);
        if (sub_c != 0) {
            g_collision_object_list[count++] = sub_c;
        }
    }

    return count;
}

// =============================================================================
// Address: 0x800f38a4 | Name: character_set_collision_flags
// Sets collision flags on all collected sub-objects of a character.
// Calls character_collect_collision_objects to gather the list, then iterates
// calling collision_set_flags on each sub-object's collision data pointer (+4).
// Used to enable/disable collision categories (ragdoll, invincibility, etc).
// =============================================================================
void character_set_collision_flags(int* char_data, u32 flags, u16 collision_flags) {
    int count = character_collect_collision_objects(char_data, flags);

    for (int i = 0; i < count; i++) {
        int* collision_ptr = *(int**)(g_collision_object_list[i] + 4);
        collision_set_flags(collision_ptr, collision_flags);
    }
}

// =============================================================================
// Address: 0x800f3910 | Name: character_set_collision_shape
// Sets collision shape parameters on all sub-objects of a character.
// Calls character_collect_collision_objects, then iterates calling
// collision_set_shape_params with the provided material type, mask (-1 = all),
// and layer values. Used to configure damage types and hit detection layers.
// =============================================================================
void character_set_collision_shape(int* char_data, u32 flags,
                                   u8 material, u8 param_a, u8 param_b) {
    int count = character_collect_collision_objects(char_data, flags);

    for (int i = 0; i < count; i++) {
        int* collision_ptr = *(int**)(g_collision_object_list[i] + 4);
        collision_set_shape_params(collision_ptr, material, 0xFFFFFFFF, param_a, param_b);
    }
}

// =============================================================================
// Address: 0x800f3990 | Name: character_particle_effect_tick
// Per-frame particle effect update for a character effect slot. Manages the
// full particle lifecycle:
//
// Structure layout (param_1 is a 7-DWORD effect slot):
//   [0] = entity pointer (-> sub-entity at *[0])
//   [1] = flags (bit 6 = always-on mode)
//   [2] = state (0 = inactive, 1 = active/fading)
//   [3] = player index (selects particle type from table)
//   [4] = timer accumulator
//   [5] = interval/threshold
//   [6] = particle handle
//
// Two modes:
//   Always-on (flag 0x40): spawns immediately, fades opacity down over time,
//     respawns after random delay. Creates ambient effects (steam, smoke).
//   Match-based: only spawns when particle handle matches player's type ID,
//     sets opacity to full and marks active.
//
// Tracks entity position via bone matrix transform for 3D positioning.
// Applies per-player color scaling (player 1 gets reduced intensity).
// =============================================================================
void character_particle_effect_tick(u32* slot) {
    // Early out: check if effect should be active
    if ((slot[1] & 0x40) == 0) {
        // Match-based mode: check if particle handle matches player type
        if (slot[6] == 0 || *(int*)(slot[6] + 4) != g_player_particle_type[slot[3]]) {
            return;
        }
    }

    int entity = *(int*)*slot;

    // Compute time delta
    float dt = CONST_FRAME_TIME * (CONST_EFFECT_INTERVAL /
               (float)g_target_framerate);

    // Base position from direction table
    float pos[3];
    pos[0] = g_direction_table[0]; // 0x803cc980
    pos[1] = g_direction_table[1]; // 0x803cc984
    pos[2] = g_direction_table[2]; // 0x803cc988

    // Additional direction/scale data
    float dir[3];
    dir[0] = g_direction_table[3]; // 0x803cc98c
    dir[1] = g_direction_table[4]; // 0x803cc990
    dir[2] = g_direction_table[5]; // 0x803cc994

    // Build position from entity world transform
    float entity_pos[3] = {
        *(float*)(entity + 0x64),
        *(float*)(entity + 0x68),
        *(float*)(entity + 0x6c)
    };
    float scale = (CONST_SCALE_FACTOR * *(float*)(entity + 0x70)) / CONST_SCALE_DIVISOR;
    math_build_position_from_scale(entity_pos, scale, g_work_matrix);

    // Compute color from entity RGB channels, scaled by framerate
    float color[3];
    color[0] = *(float*)(entity + 0xf8) * (float)g_target_framerate;
    color[1] = *(float*)(entity + 0xfc) * (float)g_target_framerate;
    color[2] = *(float*)(entity + 0x100) * (float)g_target_framerate;

    // Player 1 gets reduced color intensity
    if (slot[3] == 1) {
        color[0] *= CONST_PLAYER1_SCALE;
        color[1] *= CONST_PLAYER1_SCALE;
        color[2] *= CONST_PLAYER1_SCALE;
    }

    // Apply bone matrix transform to position
    math_transform_point(g_work_matrix, dir);
    float* bone_mtx = get_bone_matrix(entity);
    math_concat_matrices(bone_mtx, g_work_matrix, g_result_matrix);

    // Offset position by bone result
    pos[0] += g_result_pos[0];
    pos[1] += g_result_pos[1];
    pos[2] += g_result_pos[2];

    // Direction from bone matrix
    dir[0] = g_result_dir[0];
    dir[1] = CONST_OPACITY_FULL;
    dir[2] = g_result_dir[2];

    // Advance timer
    slot[4] = (u32)((float)slot[4] + dt);

    if ((slot[1] & 0x40) == 0) {
        // --- Match-based mode ---
        int* particle = (int*)slot[6];
        if (particle != NULL && particle[1] == g_player_particle_type[slot[3]]) {
            particle_slot_set_opacity(CONST_OPACITY_FULL, particle);
            slot[4] = (u32)CONST_OPACITY_FULL;
            slot[2] = 1;
        }
        if (slot[6] == 0) {
            slot[2] = 0;
        }
    } else {
        // --- Always-on mode ---
        // Spawn if no active particle and timer exceeded threshold
        if (slot[6] == 0 && slot[2] == 0 && (float)slot[4] > (float)slot[5]) {
            u32 handle = particle_spawn_directed_full(
                &slot[6],
                g_player_particle_type[slot[3]],
                0x80000,    // flags
                pos,        // position
                NULL,       // velocity
                NULL,       // acceleration
                dir         // direction + scale
            );
            slot[6] = handle;
            slot[4] = (u32)CONST_OPACITY_FULL;
            slot[2] = 1;
            slot[5] = (u32)CONST_EFFECT_INTERVAL;
        }

        // Update active particle
        int* particle = (int*)slot[6];
        if (particle != NULL && particle[1] == g_player_particle_type[slot[3]]) {
            if (slot[2] == 0 || (float)slot[4] <= (float)slot[5]) {
                // Fading out
                if (slot[2] != 0) {
                    float fade = CONST_FADE_RATE * (CONST_EFFECT_INTERVAL - (float)slot[4]);
                    particle_slot_set_opacity(fade, particle);
                }
            } else {
                // Reached threshold: reset and pick new random interval
                particle_slot_set_opacity(CONST_OPACITY_FULL, particle);
                slot[4] = (u32)CONST_OPACITY_FULL;
                slot[2] = 0;
                float rand_val = random_float_01();
                slot[5] = (u32)(CONST_RESPAWN_BASE + CONST_RESPAWN_RANGE * rand_val);
            }
        }
    }

    // Update particle position and color if still valid
    int active_particle = slot[6];
    if (active_particle != 0 &&
        *(int*)(active_particle + 4) == g_player_particle_type[slot[3]]) {
        particle_slot_set_color(active_particle, color);
        particle_slot_set_position(slot[6], pos);
    }
}

// =============================================================================
// Address: 0x800f3d50 | Name: character_release_particles
// Releases all 4 particle effect handles from a character. For each slot at
// offset +0xbc (stride 4): disables the cached particle, detaches from its
// entity owner, sets emitter stop flag (0x100000), and releases the handle.
// Called during character destroy/death cleanup.
// =============================================================================
void character_release_particles(int char_data) {
    for (int i = 0; i < PARTICLE_SLOTS_PER_CHAR; i++) {
        int offset = char_data + 0xbc + i * 4;

        // Disable particle rendering
        int handle = *(int*)offset;
        particle_cached_disable(&handle);

        // Detach from entity
        int h2 = *(int*)offset;
        particle_handle_set_entity(&h2, 0);

        // Set emitter stop flag
        int h3 = *(int*)offset;
        particle_emitter_set_flags(&h3, 1, 0x100000);

        // Release the handle back to pool
        particle_handle_release((u32*)offset);
    }
}

// =============================================================================
// Address: 0x800f3de0 | Name: character_death_effect_trigger
// Triggers death/damage visual effect on a character. Checks game state:
//   - Level 0x65: always allowed
//   - Mode byte 0x05 with flag clear: early return (no effect)
//   - Other modes without flag bit 2: early return
// When allowed: sets opacity to 0, clears flag bit 2, clears animation field
// at entity+0x8c, then releases all active particle effects.
// Called from: bullet impact, AI projectile hit, and various combat systems.
// =============================================================================
void character_death_effect_trigger(int* char_data) {
    // Game state check
    if (g_current_level_id != 0x65) {
        if (g_game_mode_byte == 0x05 && g_death_effect_enabled == 0) {
            return;
        }
        if (g_game_mode_byte != 0x05 && (char_data[1] & 4) == 0) {
            return;
        }
    }

    // Clear opacity to zero
    char_data[0x2d] = (int)0.0f; // CONST_OPACITY_FULL = 0.0 here (float bits)

    // Clear "has active effect" flag
    char_data[1] &= ~4;

    // Clear animation reference
    *(int*)(*char_data + 0x8c) = 0;

    // Release all particle effects
    character_release_particles();
}
