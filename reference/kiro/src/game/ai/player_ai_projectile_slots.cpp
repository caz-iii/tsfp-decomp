// =============================================================================
// Player AI Projectile Slot System
// Address Range: 0x800b9c74 - 0x800ba124
// Date: 2026-08-11
// Batch: 262
//
// This module manages a pool of projectile tracking slots (0x22c bytes each,
// max 30 slots). Each slot represents an in-flight projectile (bullet, grenade,
// rocket, etc.) and tracks owner entity, position, velocity, lifetime, and
// visual effects. Functions include:
//   - Pool index accessors (start/end/max)
//   - Slot pointer computation from index
//   - Owner cleanup when entities are destroyed/dropped
//   - Per-entity slot iteration with update callback
//   - Impact debris spawning (shell casings + particles)
//   - Entity death handler (deactivate owned slots)
//   - Raycast visibility check for projectile-to-target LOS
// =============================================================================

#include "common.h"
#include "player_ai.h"
#include "particle.h"
#include "collision.h"
#include "scene.h"

// Projectile slot pool base address
extern int DAT_80611dc0;  // pool base pointer

// Pool config data
extern int DAT_80564300;  // pool start index
extern int DAT_80564310;  // pool end index

// Global timing/RNG
extern float FLOAT_806108ec;  // delta time
extern float FLOAT_80614c30;  // speed scale constant

// =============================================================================
// Address: 0x800b9c74 | player_ai_projectile_impact_debris
// Spawns impact debris on projectile hit. Three effect types controlled by
// flags bitmask:
//   bit 2 (0x4): directed blood/debris particle (type 0x6c)
//   bit 0 (0x1): directed smoke puff (type 0x11, half scale)
//   bit 1 (0x2): shell casings (4-11 pieces, floor polygon search for material)
// Called when a projectile hits a surface or character.
// =============================================================================
void player_ai_projectile_impact_debris(/* paired-single params */, double param_3,
                                         /* ... */, int param_9, int param_10, unsigned int flags)
{
    int slot_data; // from FPR resolution
    float* normal; // from FPR resolution

    // Bit 2: spawn directed debris particle
    if (flags & 4) {
        float scaled_normal[3];
        scaled_normal[0] = 3.0f * normal[0]; // FLOAT_80614a7c
        scaled_normal[1] = 3.0f * normal[1];
        scaled_normal[2] = 3.0f * normal[2];
        int mode = FUN_801c1714();
        unsigned int pflags = (mode == 1) ? 0 : 0x80000;
        particle_spawn_directed(0, 0x6c, pflags, (float*)(slot_data + 0x1c), scaled_normal);
    }

    // Bit 0: spawn smoke puff particle (half scale)
    if (flags & 1) {
        int mode = FUN_801c1714();
        unsigned int pflags = (mode == 1) ? 0 : 0x80000;
        int pslot = particle_spawn_directed(0, 0x11, pflags, (float*)(slot_data + 0x1c), NULL);
        particle_slot_set_scale(0.5f, pslot); // FLOAT_80614a78
    }

    // Bit 1: spawn shell casings from surface material
    if (flags & 2) {
        int room_node = *(int*)(slot_data + 0xf0);
        int tex_data = scene_node_get_texture_data(room_node, 1);
        int mat_count = scene_node_get_material_count(room_node, 1);
        int floor_poly = floor_polygon_search(mat_count, tex_data,
            *(unsigned int*)(*(int*)(*(int*)(DAT_80611b04 + room_node * 4) + 4) + 0x30),
            (float*)(slot_data + 0x1c), NULL);

        float casing_dir[3];
        casing_dir[0] = FLOAT_80614b50 * normal[0];
        casing_dir[1] = FLOAT_80614b50 * normal[1];
        casing_dir[2] = FLOAT_80614b50 * normal[2];

        unsigned int rng = FUN_8013104c();
        int count = (rng & 7) + 4; // 4-11 casings
        shell_casing_spawn_batch(floor_poly, /* ... */ 0xd,
                                  (int*)(slot_data + 0x1c), casing_dir, count);
    }
}

// =============================================================================
// Address: 0x800b9df8 | player_ai_projectile_pool_start_index
// Returns the starting index of the projectile slot pool.
// =============================================================================
int player_ai_projectile_pool_start_index(void)
{
    return DAT_80564300;
}

// =============================================================================
// Address: 0x800b9e08 | player_ai_projectile_pool_end_index
// Returns the ending index (inclusive) of the projectile slot pool.
// =============================================================================
int player_ai_projectile_pool_end_index(void)
{
    return DAT_80564310;
}

// =============================================================================
// Address: 0x800b9e18 | player_ai_projectile_pool_max_count
// Returns the maximum number of projectile slots (constant 30).
// =============================================================================
int player_ai_projectile_pool_max_count(void)
{
    return 0x1e; // 30
}

// =============================================================================
// Address: 0x800b9e20 | player_ai_projectile_get_slot
// Computes slot pointer from relative index.
// Formula: pool_base + (start_index + rel_index) * 0x22c
// =============================================================================
int* player_ai_projectile_get_slot(int rel_index)
{
    int start = player_ai_projectile_pool_start_index();
    return (int*)(DAT_80611dc0 + (start + rel_index) * 0x22c);
}

// =============================================================================
// Address: 0x800b9e5c | player_ai_projectile_clear_owner
// Clears ownership reference when an entity is destroyed/dropped.
// Iterates all active slots (type == 0x10), zeroes owner field if matching.
// Called by player_ai_drop_all_weapons.
// =============================================================================
void player_ai_projectile_clear_owner(int entity_ptr)
{
    int start = player_ai_projectile_pool_start_index();
    int end = player_ai_projectile_pool_end_index();
    int offset = start * 0x22c;

    for (int i = start; i <= end; i++) {
        int* slot = (int*)(DAT_80611dc0 + offset);
        // Type 0x10 = active projectile, field 0x41 = owner entity
        if (slot[0] == 0x10 && slot[0x41] != 0 && slot[0x41] == entity_ptr) {
            slot[0x41] = 0;
        }
        offset += 0x22c;
    }
}

// =============================================================================
// Address: 0x800b9ee8 | player_ai_projectile_update_owned
// Iterates all projectile slots owned by a specific entity, calling the
// per-slot update function (FUN_800ba964) on each matching slot.
// =============================================================================
void player_ai_projectile_update_owned(void)
{
    int owner_entity = FUN_803826e8(); // entity passed via register convention
    int start = player_ai_projectile_pool_start_index();
    int end = player_ai_projectile_pool_end_index();
    int offset = start * 0x22c;

    for (int i = start; i <= end; i++) {
        int* slot = (int*)(DAT_80611dc0 + offset);
        if (slot[0] == 0x10 && slot[0x41] != 0 && slot[0x41] == owner_entity) {
            FUN_800ba964(/* params */, slot); // per-slot update
        }
        offset += 0x22c;
    }
}

// =============================================================================
// Address: 0x800b9f7c | player_ai_projectile_iterate_all_stub
// Iterates all projectile slots but performs no action (empty loop body). The
// slot count remains in the result register (r3) on return, so callers reuse
// this as a projectile/slot-availability probe (see object_base.cpp,
// object_weapon_dispatch_93ec). Returns the projectile-pool slot count.
// =============================================================================
int player_ai_projectile_iterate_all_stub(void)
{
    int start = player_ai_projectile_pool_start_index();
    int end = player_ai_projectile_pool_end_index();
    int count = (end + 1) - start;

    // Empty iteration - no-op; slot count is returned via r3.
    for (int i = 0; i < count; i++) {
        // no-op
    }
    return count;
}

// =============================================================================
// Address: 0x800ba00c | player_ai_projectile_on_entity_death
// Handles entity death: finds all projectile slots owned by the dying entity,
// zeroes their velocity, applies random speed decay, clears ownership, and
// optionally deactivates AI slot or clears character state.
//
// For slot types 0x20/0x21 (homing/guided): clears target character state,
// or in non-arcade mode for type 0x20, deactivates the AI slot entirely.
// =============================================================================
void player_ai_projectile_on_entity_death(void)
{
    int entity = FUN_803826e8();
    int start = player_ai_projectile_pool_start_index();
    int end = player_ai_projectile_pool_end_index();
    int offset = start * 0x22c;

    for (int i = start; i <= end; i++) {
        int* slot = (int*)(DAT_80611dc0 + offset);
        if (slot[0] != -1 && slot[0x56] == entity) {
            slot[0x29] = 0; // zero velocity
            // Apply randomized speed decay: dt * scale * (1.0 / random)
            slot[0x1a] = (int)(FLOAT_806108ec * FLOAT_80614c30 *
                              (1.0f / (float)((double)(0x43300000 | (DAT_806109c0 ^ 0x80000000))
                                              - DOUBLE_80614a68)));
            slot[0x56] = 0; // clear owner reference

            if (slot[0] == 0x20 || slot[0] == 0x21) {
                int is_arcade = game_is_standard_arcade();
                if (is_arcade == 0 && slot[0] != 0x21) {
                    player_ai_slot_deactivate();
                } else {
                    // Clear character tracking state
                    *(int*)(*(int*)(slot[0x2a] + 0x7c) + 0xc0) = 0;
                }
            }
        }
        offset += 0x22c;
    }

    // Clear entity's projectile-active flag (bit 16)
    *(unsigned int*)(entity + 0x28) &= ~0x10000;
}

// =============================================================================
// Address: 0x800ba124 | player_ai_projectile_raycast_visibility
// Performs downward raycast from each projectile slot owned by the given entity
// to check if the projectile has line-of-sight to the ground/target.
//
// For character entities (type 8): only counts as visible if ray hits AND
// hit point is below the entity's Y position.
// For non-character entities: any ray hit counts as visible.
//
// On visibility confirmed: applies speed decay and clears character target.
// On no visibility: sets character tracking to mode 2 (lost target).
// =============================================================================
void player_ai_projectile_raycast_visibility(void)
{
    int entity = FUN_803826e4();
    int start = player_ai_projectile_pool_start_index();
    int end = player_ai_projectile_pool_end_index();
    int offset = start * 0x22c;

    for (int i = start; i <= end; i++) {
        int* slot = (int*)(DAT_80611dc0 + offset);
        if (slot[0] != -1 && slot[0x56] == entity) {
            float ray_dir[3]; // downward ray
            ray_dir[0] = DAT_803cc5bc;
            ray_dir[1] = DAT_803cc5c0;
            ray_dir[2] = DAT_803cc5c4;

            slot[0x56] = 0; // clear owner before check
            bool visible = false;

            int hit = scene_raycast(slot[0x3c], slot + 7, ray_dir, 1,
                                     NULL, NULL, NULL, 0x2000078, 0, NULL);

            if (*(int*)(entity + 0x20) == 8) {
                // Character: visible only if hit AND below entity Y
                if (hit != 0 && *(float*)(entity + 0x68) < (float)slot[8]) {
                    visible = true;
                }
            } else {
                // Non-character: any hit = visible
                if (hit != 0) visible = true;
            }

            if (visible) {
                // Apply speed decay
                slot[0x1a] = (int)(FLOAT_806108ec * FLOAT_80614c30 *
                                  (1.0f / (float)((double)(0x43300000 | (DAT_806109c0 ^ 0x80000000))
                                                  - DOUBLE_80614a68)));
                if ((slot[0] == 0x20 || slot[0] == 0x21) && slot[0x2a] != 0) {
                    *(int*)(*(int*)(slot[0x2a] + 0x7c) + 0xc0) = 0;
                }
            } else {
                // Lost visibility - set tracking mode 2
                if ((slot[0] == 0x20 || slot[0] == 0x21) && slot[0x2a] != 0) {
                    *(int*)(*(int*)(slot[0x2a] + 0x7c) + 0xc0) = 2;
                }
            }
        }
        offset += 0x22c;
    }

    // Clear entity's projectile-active flag
    *(unsigned int*)(entity + 0x28) &= ~0x10000;
}
