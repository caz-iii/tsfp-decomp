// =============================================================================
// damage_splash.cpp — Splash/AOE damage computation and area scanning
// TimeSplitters: Future Perfect (GameCube)
// Batch 193 | Date: 2026-08-11
// =============================================================================

#include "common.h"
#include "damage.h"
#include "entity.h"
#include "scene.h"
#include "collision.h"

// =============================================================================
// Address: 0x80090c78 | Name: damage_get_splash_radius
// Returns the effective splash damage radius. In multiplayer (flag 0x40 or
// 0x8000000), fetches from weapon/vehicle data. Otherwise uses a global base
// radius, scaled by difficulty in MP mode ('\n' == 10-player).
// =============================================================================
float damage_get_splash_radius(void)
{
    float radius;
    float scale;
    int vehicle_data;

    if ((g_game_flags & 0x40) != 0 || (g_game_flags & 0x8000000) != 0) {
        vehicle_data = get_current_vehicle_data();
        radius = *(float *)(*(int *)(vehicle_data + 0x6C) + 4);
    } else {
        radius = g_splash_base_radius;
    }

    scale = g_splash_distance_scale;
    if (g_game_mode == '\n') {
        scale = g_difficulty_radius_table[g_difficulty_level];
    }

    return radius * scale;
}

// =============================================================================
// Address: 0x80090cf8 | Name: damage_get_amount_for_entity
// Returns the base damage amount for a given entity. In arcade mode (100),
// uses the weapon stat table indexed by weapon type. In 10-player MP with
// >1 player and non-story entity, applies a multiplier. Otherwise reads
// from standard weapon damage table.
// =============================================================================
float damage_get_amount_for_entity(int entity_ptr)
{
    int char_data = *(int *)(entity_ptr + 0x7C);

    if (g_arcade_mode_id == 100) {
        unsigned short weapon_idx = *(unsigned short *)(*(int *)(char_data + 0xBF0) + 0x4E);
        return g_weapon_damage_arcade_table[weapon_idx];
    }

    if (g_game_mode == '\n' && g_player_count > 1 &&
        *(short *)(char_data + 0x48) != 0x0D71) {
        return g_mp_damage_multiplier * g_weapon_damage_table[*(int *)(char_data + 0xB04)];
    }

    return g_weapon_damage_table[*(int *)(char_data + 0xB04)];
}

// =============================================================================
// Address: 0x80090f14 | Name: damage_get_min_radius
// Returns the minimum splash radius (inner full-damage zone). In MP, uses
// difficulty-indexed table; in SP, uses the global constant.
// =============================================================================
float damage_get_min_radius(void)
{
    if (g_game_mode == '\n') {
        return g_splash_min_radius_table[g_difficulty_level];
    }
    return g_splash_min_radius_default;
}

// =============================================================================
// Address: 0x80090f48 | Name: damage_get_max_radius
// Returns the maximum splash radius (outer zero-damage boundary). In MP,
// uses difficulty-indexed table; in SP, uses the global constant.
// =============================================================================
float damage_get_max_radius(void)
{
    if (g_game_mode == '\n') {
        return g_splash_max_radius_table[g_difficulty_level];
    }
    return g_splash_max_radius_default;
}

// =============================================================================
// Address: 0x80090b90 | Name: damage_compute_knockback_force
// Computes the knockback force for a weapon hit. If weapon_index is valid,
// applies distance-based falloff to the base knockback value.
// =============================================================================
float damage_compute_knockback_force(float distance, int weapon_index, int attacker)
{
    float force = 0.0f;

    if (weapon_index != -1) {
        float falloff = damage_compute_falloff(distance, weapon_index, attacker);
        force = g_weapon_knockback_table[weapon_index] * falloff;
    }

    return force;
}

// =============================================================================
// Address: 0x80090be4 | Name: damage_compute_impulse
// Computes the damage impulse (actual damage value) for a weapon hit with
// distance falloff. Weapon type 1 (explosive) applies a player-count-based
// normalization factor to prevent excessive splash in crowded matches.
// =============================================================================
float damage_compute_impulse(float distance, int weapon_index, int attacker)
{
    float impulse = 0.0f;

    if (weapon_index != -1) {
        float falloff = damage_compute_falloff(distance, weapon_index, attacker);
        impulse = g_weapon_impulse_table[weapon_index] * falloff;

        if (g_weapon_type_table[weapon_index] == 1) {
            // Explosive: normalize by player count
            float player_norm = g_explosion_norm_factor *
                (g_splash_base_radius / (float)((double)g_player_count_dbl - g_player_count_offset));
            impulse = impulse * player_norm;
        }
    }

    return impulse;
}

// =============================================================================
// Address: 0x80090d90 | Name: damage_splash_level_gate
// Per-frame splash damage level/difficulty gate. Checks if the current
// game level allows splash damage based on difficulty thresholds and vehicle
// state. If permitted, dispatches to damage_get_amount_for_entity or the
// story progression handler.
// =============================================================================
void damage_splash_level_gate(void)
{
    int entity_ptr = get_current_entity_from_stack();
    int char_data = *(int *)(entity_ptr + 0x7C);

    if ((g_game_flags & 0x40) == 0) {
        if ((g_game_flags & 0x8000000) != 0) {
            int level_threshold = get_current_level_splash_threshold();
            if (*(int *)(char_data + 8) < level_threshold) {
                get_current_vehicle_data();
            }
        }
    } else {
        if (*(int *)(char_data + 0xBF0) != 0) {
            int level_threshold = get_current_level_splash_threshold();
            if ((int)(unsigned short)(*(int *)(char_data + 0xBF0) + 0x16) < level_threshold) {
                get_current_vehicle_data();
            }
        }
    }

    if (g_game_mode == '\n') {
        damage_get_amount_for_entity(entity_ptr);
    } else if (g_player_count <= *(int *)(char_data + 8)) {
        story_damage_progression(*(short *)(char_data + 0x48));
    }

    restore_stack_frame();
}

// =============================================================================
// Address: 0x80090360 | Name: damage_splash_area_scan
// MASSIVE function. The core splash/AOE damage dispatcher:
// 1. Gets explosion origin and determines damage tier (0/1/2/3 = full/2x/3x/0)
// 2. Computes effective radius scaled by time and distance factors
// 3. Builds AABB around explosion center, queries scene for nearby nodes
// 4. Iterates all entities in visible nodes:
//    - For character entities (type 8): checks weapon flags + team eligibility
//    - For prop entities: uses modular 5-frame scheduling
//    - Deduplicates multi-room entities via global seen-list
// 5. Per eligible entity:
//    - Computes direction vector from explosion to entity center
//    - Distance check against scaled radius
//    - Raycasts for line-of-sight (scene_raycast)
//    - On LOS success: computes quadratic falloff, builds damage context
//    - Dispatches via damage_dispatch_to_entity
// =============================================================================
void damage_splash_area_scan(void)
{
    // [Large function - see Ghidra decompilation at 0x80090360]
    // Key operations:
    // - save_stack_frame() / restore_stack_frame()
    // - Tier selection: tier 0 = base, tier 1 = 2x multiplier, tier 2 = 3x, else = zero
    // - Time factor via get_game_timer() -> normalized progression
    // - scene_query_nodes_in_frustum() for spatial query
    // - Per-entity iteration with type-specific eligibility:
    //   * Type 8 (character): weapon flag check, team flag, 5-frame skip for perf
    //   * Other: frame-modular scheduling (entity_id % 5 == current_sub_frame)
    // - Multi-room deduplication via DAT_8055ff40 seen-list (max 40 entries)
    // - For characters: special handling for type 0x46/0x64 (instant dispatch)
    //                   and type 0x42 (elevated target + reduced radius)
    // - LOS via scene_raycast with collision flags 0xFFFFFFF9
    // - Falloff: quadratic (1 - (dist/radius)^2) clamped to [0, max_damage]
    // - Optional collision_entity_raycast for hit-point determination
    // - Builds damage_context struct and calls damage_dispatch_to_entity

    // --- Pseudocode summary (cleaned) ---
    int* stack_ctx;
    int explosion_data;
    float damage_amount;
    float radius;
    float aabb[6];        // min xyz, max xyz
    int node_list[31];
    int node_count;

    // 1. Get explosion context from stack
    // stack_ctx = save_stack_frame()
    // explosion_data = stack_ctx[1]  (second word = explosion entity data)

    // 2. Determine tier and damage multiplier
    // tier 0: damage = base * 1.0,  is_direct = true
    // tier 1: damage = base * tier1_mult
    // tier 2: damage = base * tier2_mult
    // else:   damage = 0 (no splash)

    // 3. Time-based scaling
    // game_time = get_game_timer()
    // time_factor = (game_time - offset) / scale
    // damage *= (slope * time_factor + intercept)

    // 4. Build AABB from center + radius, query scene nodes
    // node_count = scene_query_nodes_in_frustum(0, &aabb, node_list, 30)

    // 5. For each node, iterate entities in that room
    //    Apply type-specific eligibility, dedup, distance check, LOS, dispatch

    // Full implementation deferred to matching pass
}
