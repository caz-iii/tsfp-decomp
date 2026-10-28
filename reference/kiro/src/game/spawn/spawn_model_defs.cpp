// Address: 0x80096cd4 - 0x800981c8 | Name: spawn_model_defs | Date: 2026-08-11
// Spawn system utilities: respawn animation, zone selection for respawning,
// dropped weapon creation, and model definition table lookups.
// These are the low-level support functions used by the spawn lifecycle.

#include "common.h"

// ============================================================================
// spawn_respawn_anim_tick - 0x80096cd4
// Respawn animation interpolation. When spawn slot state==2 (respawning):
// decrements countdown timer, interpolates position toward target via
// velocity fields, rotates toward target angles. On timer expire: plays
// sound 0xd44 for type 2 (timed), snaps to final position, transitions to
// state 1. For type 1 (weapon rack): calls spawn_weapon_rack_drop to
// return weapon. Called by spawn_entity_main_tick.
// ============================================================================
void spawn_respawn_anim_tick(/* paired-single params */)
{
    int entity = get_current_update_entity();  // FUN_803826f0
    int slot = *(int*)(entity + 0x7c);

    // Only active when state==2, no attachment, has submesh
    if (*(int*)(entity + 0x78) != 0 || *(int*)(entity + 0x128) != 0) return;
    if (slot == 0) return;
    if (*(int*)(slot + 0xa0) != 2) return;
    if (*(int*)(*(int*)(entity + 4) + 0x10) == 0) return;

    // Decrement animation timer
    *(int*)(slot + 0xa8) -= g_frame_delta_ticks;

    if (*(int*)(slot + 0xa8) < 1) {
        // Timer expired: snap to final position
        SpawnModelDef* def = spawn_get_model_def(*(int*)(slot + 0xc), *(int*)(slot + 0x14));

        // Type 2 (timed pickup): play respawn sound
        if (*(int*)(slot + 0xc) == 2) {
            SoundParams params;
            sound_init_play_params(&params);
            params.sound_id = 0xd44;
            params.positional = 1;
            params.pos[0] = *(float*)(entity + 0x64);
            params.pos[1] = *(float*)(entity + 0x68);
            params.pos[2] = *(float*)(entity + 0x6c);
            sound_play_request(&params);

            // Non-story mode: set despawn flag + timer
            if (g_game_mode != 10) {
                *(int*)(entity + 0x1a0) = 1;
                *(int*)(entity + 0x1a4) = g_frame_counter * 12;
            }
        }

        // Set final rotation from def
        *(float*)(entity + 0xc0) = (float)def[10];
        *(float*)(entity + 0xc8) = (float)def[12];
        *(int*)(slot + 0xa0) = 1;  // transition to idle state
        *(int*)(slot + 0x38) = *(int*)(slot + 0x9c);

        // Type 2: additional cleanup
        if (*(int*)(slot + 0xc) == 2) {
            spawn_type2_init();  // FUN_802bc194
        }

        // Snap position to target
        *(float*)(entity + 0x64) = *(float*)(slot + 0x90);
        *(float*)(entity + 0x68) = *(float*)(slot + 0x94);
        *(float*)(entity + 0x6c) = *(float*)(slot + 0x98);

        // Type 1 (weapon rack): drop weapon back to rack
        if (*(int*)(slot + 0xc) == 1) {
            int* zone_node = find_zone_for_respawn(*(int*)(slot + 0x38));
            if (zone_node != NULL) {
                // Clear render table entry
                if (*(int*)(slot + 0x9c) >= 0) {
                    int render_entry = g_render_table_base + *(int*)(slot + 0x9c) * 0x20;
                    *(u32*)(render_entry + 4) &= ~0x200000;
                }
                *(int*)(slot + 0x9c) = -1;
                // Return weapon to rack owner
                spawn_weapon_rack_drop(g_player_data + *(int*)(slot + 0xa4) * 0xea0,
                                      zone_node[1], entity, g_player_data);
            }
        }
    } else {
        // Interpolate position toward target
        *(float*)(entity + 0x64) += *(float*)(entity + 0xb4) * g_physics_timestep;
        *(float*)(entity + 0x68) += *(float*)(entity + 0xb8) * g_physics_timestep;
        *(float*)(entity + 0x6c) += *(float*)(entity + 0xbc) * g_physics_timestep;

        // Interpolate rotation toward target
        *(float*)(entity + 0xc0) += *(float*)(slot + 0xac) * g_physics_timestep;
        *(float*)(entity + 0x70) += *(float*)(slot + 0xb0) * g_physics_timestep;
        *(float*)(entity + 0xc8) += *(float*)(slot + 0xb4) * g_physics_timestep;
    }

    restore_paired_single();
}

// ============================================================================
// spawn_find_respawn_zone - 0x80096f08
// Finds best respawn zone node for a spawn entity. Queries adjacent zones
// from the entity's current zone, checks visibility (line of sight) for each,
// picks nearest visible zone by weighted distance (XZ + Y-scaled factor).
// Arcade mode with weapon racks: excludes the owning team's base zone.
// Fallback: nearest zone by raw distance if no visible zones found.
// Returns zone node ID or -1.
// ============================================================================
int spawn_find_respawn_zone(void)
{
    u64 pair = get_pickup_pair_entities_v3();
    int player = (int)(pair >> 32);
    int* this_ent = (int*)pair;
    int slot = *(int*)(player + 0x7c);

    float best_dist = MAX_RESPAWN_DISTANCE;
    int visible_count = 0;
    int best_zone = -1;
    int adjacent_zones[32];
    int visible_zones[20];

    // Get adjacent zones from current zone
    int zone_count = soundpad_query_adjacent_zones_simple(
        this_ent[0x19e], adjacent_zones, 19, 0, 0);
    adjacent_zones[zone_count] = this_ent[0x19e];  // include self

    // Arcade weapon rack mode: exclude team base zone
    if (g_game_mode == 2 && slot != 0 && *(int*)(slot + 0xc) == 1) {
        u32 can_unequip = check_weapon_unequip_allowed(*this_ent);
        if (can_unequip == 0) {
            for (int i = 0; i <= zone_count; i++) {
                if (zone_count != 0 &&
                    adjacent_zones[i] == g_team_base_zones[this_ent[0x21]]) {
                    adjacent_zones[i] = -1;  // exclude
                }
            }
        }
    }

    // Filter to visible zones (LOS check)
    for (int i = 0; i <= zone_count; i++) {
        int render_entry = g_render_table_base + adjacent_zones[i] * 0x20;
        if (adjacent_zones[i] != -1 &&
            *(float*)(render_entry + 0x10) - *(float*)(player + 0x68) <= MAX_Y_OFFSET &&
            check_zone_visibility(0.0f, *this_ent, (float*)(render_entry + 0xc))) {
            visible_zones[visible_count] = adjacent_zones[i];
            visible_count++;
        }
    }

    // Find nearest visible zone by weighted distance
    for (int i = 0; i < visible_count; i++) {
        int re = g_render_table_base + visible_zones[i] * 0x20;
        float dy = *(float*)(re + 0x10) - *(float*)(player + 0x68);
        float dx = *(float*)(re + 0xc) - *(float*)(player + 0x64);
        if (dy < 0.0f) dy = 0.0f;
        float dz = *(float*)(re + 0x14) - *(float*)(player + 0x6c);
        float dist = dz * dz + dx * dx + Y_WEIGHT_FACTOR * dy * dy;

        if (dist < best_dist && dist < MAX_ZONE_DIST_SQ &&
            (*(u32*)(re + 4) & 0x200000) == 0) {
            best_zone = visible_zones[i];
            best_dist = dist;
        }
    }

    // Fallback: nearest by raw distance (no visibility)
    if (best_zone == -1) {
        float fallback_dist = MAX_RESPAWN_DISTANCE;
        for (int i = 0; i <= zone_count; i++) {
            int re = g_render_table_base + adjacent_zones[i] * 0x20;
            float dy = *(float*)(re + 0x10) - *(float*)(player + 0x68);
            float dx = *(float*)(re + 0xc) - *(float*)(player + 0x64);
            if (dy < 0.0f) dy = 0.0f;
            float dz = *(float*)(re + 0x14) - *(float*)(player + 0x6c);
            float dist = dz * dz + dx * dx + Y_WEIGHT_FACTOR * dy * dy;
            if (dist < fallback_dist) {
                fallback_dist = dist;
            }
        }
    }

    restore_paired_single();
    return best_zone;
}

// ============================================================================
// spawn_dropped_weapon_create - 0x800971c4
// MASSIVE function creating a dropped weapon entity when a player dies or
// drops. Two paths:
// - param_11==NULL: simple drop (weapon unequip, resets to spawn idle)
// - param_11!=NULL: weapon throw with ammo (resolves ammo from player data,
//   calls spawn_create_instance, handles physics throw with bone-based velocity)
// Story mode: reads ammo from weapon slot data with random scaling.
// Non-story: reads ammo from weapon ammo functions.
// After spawn: assigns zone node for 3D sound, computes velocity toward
// destination zone over N frames, sets up respawn interpolation.
// Called by weapon equip/death systems.
// ============================================================================
void spawn_dropped_weapon_create(/* paired-single params */, int* weapon_data)
{
    u64 pair = get_pickup_pair_entities_v8();  // FUN_803826cc
    int player = (int)(pair >> 32);
    int* this_ent = (int*)pair;
    int spawned_entity = 0;

    // If player has bone attachment: sync position from parent
    if (*(int**)(player + 0x78) != NULL) {
        int parent = **(int**)(player + 0x78);
        int bone_index = *(int*)(player + 0x12c);
        if ((*(u32*)(parent + 0x10) & 0xffff) != 0) {
            *(float*)(player + 0x64) = *(float*)(parent + 0x64) + *(float*)(player + 0x248);
            *(float*)(player + 0x68) = *(float*)(**(int**)(player + 0x78) + 0x68) +
                                       *(float*)(player + 0x24c);
            *(float*)(player + 0x6c) = *(float*)(**(int**)(player + 0x78) + 0x6c) +
                                       *(float*)(player + 0x250);
        }
    }

    if (weapon_data == NULL) {
        // Simple weapon drop: reset to spawn idle state
        SpawnModelDef* def = spawn_get_model_def(
            *(int*)(*(int*)(player + 0x7c) + 0xc),
            *(int*)(*(int*)(player + 0x7c) + 0x14));
        *(int*)(player + 0x78) = 0;
        *(int*)(player + 0x128) = 0;
        *(int*)(player + 0x12c) = -1;
        *(float*)(player + 0xc0) = (float)def[10];
        *(float*)(player + 0x70) = 0.0f;
        *(float*)(player + 0xc8) = (float)def[12];
        spawned_entity = player;
    } else {
        // Weapon throw: resolve ammo and create new spawn entity
        int weapon_id = this_ent[0x25];
        if (weapon_id <= 0 || weapon_id >= 0x46) goto done;
        if (!weapon_is_in_match_list(weapon_id)) goto done;

        int weapon_entry = g_weapon_table_base + weapon_data[0xc] * 0x29c;
        if (weapon_data[0xc] == -1) goto done;
        if ((*(u32*)(weapon_entry + 0xc) & 0x10) != 0) goto done;

        int primary_weapon = *(int*)(weapon_entry + 0x14);
        int secondary_weapon = *(int*)(weapon_entry + 0x1c);
        int ammo_primary = 0;
        int ammo_secondary = 0;

        if (g_game_mode == 10) {
            // Story mode: read ammo from weapon slot with random scaling
            short* ammo_ptr;
            int slot_data = this_ent[0x2fc];
            int slot_offset = slot_data + (u32)g_weapon_slot_index * 4;

            if (*(short*)(slot_offset + 0x1c) == -1 || *(short*)(slot_offset + 0x1e) == -1) {
                ammo_ptr = weapon_get_ammo_data_ptr(this_ent[0x25],
                    *(short*)(slot_data + 0x1a));
                if (*(short*)(slot_offset + 0x1c) != -1) *ammo_ptr = *(short*)(slot_offset + 0x1c);
                if (*(short*)(slot_offset + 0x1e) != -1) ammo_ptr[1] = *(short*)(slot_offset + 0x1e);
            } else {
                ammo_ptr = (short*)(slot_offset + 0x1c);
            }

            if (g_match_timer == 100) {
                ammo_ptr = weapon_get_ammo_data_ptr(this_ent[0x25],
                    *(short*)(this_ent[0x2fc] + 0x1a));
            }

            // Scale ammo with random factor
            if (primary_weapon > 0 && primary_weapon < 0x2c && ammo_ptr != NULL) {
                float scale = AMMO_SCALE_BASE * (get_random_float() / RAND_MAX_F) + AMMO_SCALE_MIN;
                ammo_primary = (int)((float)*ammo_ptr * scale);
            }
            if (secondary_weapon > 0 && secondary_weapon < 0x2c && ammo_ptr != NULL) {
                float scale = AMMO_SCALE_BASE * (get_random_float() / RAND_MAX_F) + AMMO_SCALE_MIN;
                ammo_secondary = (int)((float)ammo_ptr[1] * scale);
            }
        } else {
            // Non-story: read from weapon ammo functions
            ammo_primary = get_weapon_primary_ammo(this_ent, this_ent[0x25]);
            ammo_secondary = get_weapon_secondary_ammo(this_ent, this_ent[0x25]);

            // AI ammo cap from weapon table
            if (g_player_count < this_ent[2] && g_game_mode != 10) {
                int ai_entry = g_ai_weapon_table[this_ent[0x25]];
                if (ai_entry >= 0 && ai_entry < 0x42) {
                    short* caps = (short*)(g_weapon_table_base + ai_entry * 0x29c + 0x28);
                    if ((int)caps[0] < ammo_primary) ammo_primary = (int)caps[0];
                    if ((int)caps[1] < ammo_secondary) ammo_secondary = (int)caps[1];
                }
            }
        }

        // Create the spawn entity (or special case for weapon 0x3c)
        if (ammo_primary == 0 && ammo_secondary == 0) {
            if (this_ent[0x25] == 0x3c) {
                spawned_entity = spawn_create_instance(1, 2, 0x3c, 0,
                    (float*)(player + 0x64), &g_spawn_props_default,
                    primary_weapon, 0, secondary_weapon, 0);
                *(u32*)(spawned_entity + 0x28) &= ~2;
            }
        } else {
            spawned_entity = spawn_create_instance(1, 2, this_ent[0x25], 0,
                (float*)(player + 0x64), &g_spawn_props_default,
                primary_weapon, ammo_primary, secondary_weapon, ammo_secondary);
        }
    }

    if (spawned_entity == 0) goto done;

    // Configure spawned entity
    int sp_slot = *(int*)(spawned_entity + 0x7c);
    *(int*)(sp_slot + 0xa4) = this_ent[2];  // owner player index
    *(u8*)(spawned_entity + 0x32) = 2;
    *(u32*)(spawned_entity + 0x28) &= ~0x40000000;

    // [Story mode with weapon_data: physics throw with bone velocity]
    // ... (complex bone matrix extraction, velocity computation, physics setup)

    // Non-physics path: standard respawn interpolation
    *(int*)(*(int*)(spawned_entity + 4) + 0x30) = 0;
    render_instance_set_material(0.0, *(int**)(spawned_entity + 4), 0);
    *(int*)(sp_slot + 0xa0) = 2;  // state: respawning
    *(u32*)(spawned_entity + 0x28) |= 99;

    // Find destination zone for respawn
    int dest_zone = spawn_find_respawn_zone();
    int* zone_node = soundpad_resolve_zone_node(dest_zone);

    // Assign sound zone node
    if (zone_node == NULL) {
        if (this_ent == NULL || this_ent[0x19e] < 0) {
            *(int*)(sp_slot + 0x9c) = 0;
        } else {
            *(int*)(sp_slot + 0x9c) = this_ent[0x19e];
        }
    } else {
        *(int*)(sp_slot + 0x9c) = dest_zone;
    }

    // ... (zone node sound entity registration for large zones)

    // Mark destination zone as occupied
    int* dest_node = soundpad_resolve_zone_node(*(int*)(sp_slot + 0x9c));

    // For type 1 (weapon rack): set sound handle visibility
    if (weapon_data == NULL && *(int*)(sp_slot + 0xc) == 1) {
        if (*(int*)(sp_slot + 0x9c) == *(int*)(sp_slot + 0x3c)) {
            *(u32*)(spawned_entity + 0x28) &= ~0x80;
        } else {
            *(u32*)(spawned_entity + 0x28) |= 0x80;
        }
        *(int*)(sp_slot + 0x38) = *(int*)(sp_slot + 0x9c);
    }
    dest_node[1] |= 0x200000;  // mark zone as occupied

    // Store target position from zone node
    *(int*)(sp_slot + 0x90) = dest_node[3];
    float rot_offset = spawn_get_rotation_offset(spawned_entity);
    *(float*)(sp_slot + 0x94) = (float)dest_node[4] + (float)rot_offset;
    *(int*)(sp_slot + 0x98) = dest_node[5];

    // Compute velocity: (target - current) / N frames
    int frame_count = (weapon_data == NULL) ? 10 : 20;
    *(int*)(sp_slot + 0xa8) = frame_count;

    float speed_scale = RESPAWN_SPEED_SCALE;
    *(float*)(spawned_entity + 0xb4) =
        (*(float*)(sp_slot + 0x90) - *(float*)(spawned_entity + 0x64)) / (float)frame_count;
    *(float*)(spawned_entity + 0xbc) =
        (*(float*)(sp_slot + 0x98) - *(float*)(spawned_entity + 0x6c)) / (float)frame_count;
    *(float*)(spawned_entity + 0xb8) =
        (*(float*)(sp_slot + 0x94) - *(float*)(spawned_entity + 0x68)) / (float)frame_count;

    // Angular velocity for rotation interpolation
    *(float*)(sp_slot + 0xac) = speed_scale * *(float*)(spawned_entity + 0xb4);
    *(float*)(sp_slot + 0xb0) = speed_scale * *(float*)(spawned_entity + 0xb8);
    *(float*)(sp_slot + 0xb4) = speed_scale * *(float*)(spawned_entity + 0xbc);

    network_notify_spawn();  // FUN_8036613c

done:
    restore_paired_single();
}

// ============================================================================
// spawn_get_model_def_extended - 0x80097e7c
// Extended model def lookup for type 0x40 (weapon ammo pickups).
// If params are valid (type==0x40, level_id in range, primary_weapon==0,
// secondary_weapon 1-0x2b): returns weapon table entry directly.
// Otherwise falls through to spawn_get_model_def for standard lookup.
// ============================================================================
SpawnModelDef* spawn_get_model_def_extended(int spawn_type, int level_id,
                                           int primary_weapon, int secondary_weapon_count,
                                           int secondary_weapon_id)
{
    if (level_id < 0 || spawn_type != 0x40 || level_id < 0 ||
        level_id > 0x45 || secondary_weapon_count != 0 ||
        secondary_weapon_id < 1 || secondary_weapon_id > 0x2b) {
        return spawn_get_model_def(spawn_type, level_id);
    }
    return (SpawnModelDef*)(&g_weapon_def_table + secondary_weapon_id * 0xe0);
}

// ============================================================================
// spawn_get_model_def_for_weapon - 0x80097eec
// Resolves spawn model def for weapon type 0x40 by looking up ammo data
// pointer and weapon properties. Calls spawn_get_model_def_extended with
// resolved parameters. Wrapper for weapon-specific def resolution.
// ============================================================================
void spawn_get_model_def_for_weapon(int a, int b, int weapon_id)
{
    u64 pair = get_pickup_pair_entities_v5();
    int type = (int)(pair >> 32);
    int id = (int)pair;

    if (id < 0 || type != 0x40 || id < 0 || id > 0x45) {
        spawn_get_model_def(type, id);
    } else {
        short* ammo_ptr = weapon_get_ammo_data_ptr(0x40, weapon_id);
        if (ammo_ptr == NULL) {
            spawn_get_model_def(type, id);
        } else {
            int secondary_id = get_weapon_secondary_id(id);
            int primary_count = get_weapon_primary_count(id);
            spawn_get_model_def_extended(0x40, id, primary_count, (int)*ammo_ptr, secondary_id);
        }
    }
    restore_paired_single();
}

// ============================================================================
// spawn_get_item_def_direct - 0x80097f8c
// Direct accessor for item definition table (type 0x200 items).
// Returns pointer to 0x54-byte entry at index param_1 in the item def table.
// No bounds checking.
// ============================================================================
SpawnModelDef* spawn_get_item_def_direct(int item_index)
{
    return (SpawnModelDef*)(&g_item_def_table + item_index * 0x54);
}

// ============================================================================
// spawn_get_model_def - 0x80097fa0
// Core spawn model definition table lookup. Type-switch returning pointer
// to per-type definition tables. Entry sizes:
//   Most types: 0x54 bytes per entry (indexed by level_id)
//   Type 0x40 (weapon): 0xe0 bytes via weapon_get_secondary_id lookup
//   Type 0x800: single fixed entry (no level_id indexing)
//   Type 0x100/0x1000: share same table
// Returns NULL if type/id invalid.
// 14 spawn types supported. Called extensively throughout spawn system.
// ============================================================================
SpawnModelDef* spawn_get_model_def(int spawn_type, int level_id)
{
    if (level_id < 0) return NULL;

    switch (spawn_type) {
    case 0x01:   return (SpawnModelDef*)(&g_def_table_weapon_rack + level_id * 0x54);
    case 0x02:   return (SpawnModelDef*)(&g_def_table_weapon_drop + level_id * 0x15);
    case 0x08:   return (SpawnModelDef*)(&g_def_table_health + level_id * 0x54);
    case 0x10:   return (SpawnModelDef*)(&g_def_table_armor + level_id * 0x54);
    case 0x20:   return (SpawnModelDef*)(&g_def_table_team_swap + level_id * 0x54);
    case 0x40: {
        // Weapon ammo: resolve via secondary weapon ID
        int weapon_id = get_weapon_secondary_id(level_id);
        if (weapon_id > 0 && weapon_id < 0x2c) {
            return (SpawnModelDef*)(&g_weapon_def_table + weapon_id * 0xe0);
        }
        return NULL;
    }
    case 0x80:   return (SpawnModelDef*)(&g_def_table_key + level_id * 0x54);
    case 0x100:  // fall through
    case 0x1000: return (SpawnModelDef*)(&g_def_table_teleport + level_id * 0x54);
    case 0x200:  return (SpawnModelDef*)(&g_def_table_item + level_id * 0x54);
    case 0x400:  return (SpawnModelDef*)(&g_def_table_special + level_id * 0x54);
    case 0x800:  return (SpawnModelDef*)(&g_def_table_eligibility);  // single entry
    default:     return NULL;
    }
}

// ============================================================================
// spawn_is_extended_pool_mode - 0x80098138
// Returns 1 if story mode (g_game_mode==10) OR if spawn slot has extended
// pool flag (bit 0 set in slot+0x10). Used to select between two sets of
// model def fields (extended vs standard mode).
// ============================================================================
int spawn_is_extended_pool_mode(int entity)
{
    if (g_game_mode != 10 && (*(u32*)(*(int*)(entity + 0x7c) + 0x10) & 1) == 0) {
        return 0;
    }
    return 1;
}

// ============================================================================
// spawn_get_rotation_offset - 0x80098168
// Returns the Y rotation offset from the model definition.
// Extended pool mode: returns field[9] (story/MP offset).
// Standard mode: returns field[0xe] (arcade offset).
// ============================================================================
float spawn_get_rotation_offset(int entity)
{
    SpawnModelDef* def = spawn_get_model_def(
        *(int*)(*(int*)(entity + 0x7c) + 0xc),
        *(int*)(*(int*)(entity + 0x7c) + 0x14));

    if (spawn_is_extended_pool_mode(entity)) {
        return (float)def[9];   // extended mode offset
    } else {
        return (float)def[14];  // standard mode offset (0xe)
    }
}

// ============================================================================
// spawn_get_alpha - 0x800981c8
// Returns the alpha/opacity value from the model definition.
// Extended pool mode: returns field[8] (story/MP alpha).
// Standard mode: returns field[0xd] (arcade alpha).
// ============================================================================
float spawn_get_alpha(int entity)
{
    SpawnModelDef* def = spawn_get_model_def(
        *(int*)(*(int*)(entity + 0x7c) + 0xc),
        *(int*)(*(int*)(entity + 0x7c) + 0x14));

    if (spawn_is_extended_pool_mode(entity)) {
        return (float)def[8];   // extended mode alpha
    } else {
        return (float)def[13];  // standard mode alpha (0xd)
    }
}
