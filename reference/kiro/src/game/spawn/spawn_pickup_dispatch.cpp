// Address: 0x80095a80 - 0x80096bf8 | Name: spawn_pickup_dispatch | Date: 2026-08-11
// Spawn pickup dispatch, proximity detection, teleporter activation,
// weapon rack drop/re-equip, and main entity tick. This is the top-level
// pickup interaction layer that routes to type-specific handlers.

#include "common.h"

// ============================================================================
// spawn_pickup_team_swap - 0x80095a80
// Team-based weapon swap handler (type 0x20). Game mode 6 (CTF): updates
// team ownership of the spawn slot and resolves weapon entity references.
// Other modes: unequips current weapon from rack, resets render state
// (material, rotation, visibility), re-registers animation, awards score.
// ============================================================================
void spawn_pickup_team_swap(/* paired-single params */, int* result)
{
    u64 pair = get_pickup_pair_entities_v6();  // FUN_803826e0
    int player = (int)(pair >> 32);
    int entity = (int)pair;
    int* character = *(int**)(entity + 0x7c);
    int slot = *(int*)(player + 0x7c);

    if (g_game_mode == 6) {
        // CTF mode: transfer team ownership
        int prev_team = *(int*)(slot + 0x1c);
        *(int*)(slot + 0x1c) = character[0x21];
        *result = 1;
        *(int*)(*(int*)(player + 4) + 0x30) = 1 << character[0x21];

        if (prev_team != *(int*)(slot + 0x1c)) {
            spawn_update_team_visibility();  // FUN_800e4490
            // Resolve weapon entity reference by team
            if (character[0x21] == 1) {
                int* ref = (character[0x1e8] >= 1) ?
                           *(int**)(player + 0x1fc) : &DEFAULT_WEAPON_REF;
                int weapon_id = *ref;
                spawn_resolve_weapon_entity_alt(&weapon_id);
            } else {
                int* ref = (character[0x1e8] >= 2) ?
                           (int*)(*(int*)(player + 0x1fc) + 4) : &DEFAULT_WEAPON_REF;
                int weapon_id = *ref;
                spawn_resolve_weapon_entity_alt(&weapon_id);
            }
        }
    } else if (character[0x20] != 0 &&
               character[0x21] == *(int*)(slot + 0x1c)) {
        // Non-CTF: unequip weapon from rack if player owns it
        u32 can_unequip = check_weapon_unequip_allowed(*character);
        if (can_unequip != 0) {
            int weapon_ent = character[0x20];
            int weapon_slot = *(int*)(weapon_ent + 0x7c);

            // Call weapon rack drop handler
            spawn_weapon_rack_drop(character, player, weapon_ent, slot);

            // Reset weapon entity render state
            *(int*)(weapon_ent + 0x78) = 0;
            *(int*)(weapon_ent + 0x128) = 0;
            *(int*)(weapon_ent + 0x12c) = -1;
            *(int*)(weapon_ent + 0x130) = -1;
            *(float*)(weapon_ent + 0xc0) = 0.0f;
            *(float*)(weapon_ent + 0x70) = 0.0f;
            *(float*)(weapon_ent + 0xc8) = 0.0f;
            *(u8*)(weapon_ent + 0x32) = 2;
            *(u32*)(weapon_ent + 0x28) &= ~0x40000000;
            *(int*)(*(int*)(weapon_ent + 4) + 0x30) = 0;

            // Reset material on all submeshes
            render_instance_set_material(0.0, *(int**)(weapon_ent + 4), 0);
            int offset = 0;
            int submesh_count = *(int*)(*(int*)(*(int*)(weapon_ent + 4) + 4) + 4);
            for (int i = 0; i < submesh_count; i++) {
                submesh_reset_material(*(int*)(*(int*)(weapon_ent + 4) + 0x10) + offset);
                offset += 0x40;
            }

            // Update spawn slot state and unlink
            *(int*)(weapon_slot + 0xa0) = 1;
            character[0x20] = 0;
            weapon_detach_from_player(entity, weapon_ent, 0);
            *result = 1;
        }
    }
    restore_paired_single();
}

// ============================================================================
// spawn_pickup_special_destroy - 0x80095c68
// Special type 0x400 pickup (explosive barrel, destructible). Collects the
// spawn, plays sound 0xd3a, awards score kill point, triggers HUD flash
// effect for the owning player, destroys the spawn entity via special cleanup.
// ============================================================================
void spawn_pickup_special_destroy(int attacker, int defender, int* result, int* destroy_flag)
{
    u64 pair = get_pickup_pair_entities();
    int player = (int)(pair >> 32);
    int entity = (int)pair;
    int* character = *(int**)(entity + 0x7c);

    float pos[3];
    sound_get_emitter_position(character, pos);

    // Process pickup collection
    spawn_pickup_collected(player);

    // Play destruction sound
    if (*(int*)(entity + 0x150) != 0) {
        SoundParams params;
        sound_init_play_params(&params);
        params.sound_id = 0xd3a;
        params.positional = 1;
        params.pos[0] = pos[0];
        params.pos[1] = pos[1];
        params.pos[2] = pos[2];
        sound_play_request(&params);
    }

    // Award kill point
    score_award_kill(character, 1);

    // Special type cleanup (explosion effect etc.)
    int cleanup_result = spawn_cleanup_special_type(player);

    // Trigger HUD flash for owning player
    if (character[2] < g_player_count) {
        hud_trigger_flash(*(int*)(g_hud_base + character[2] * 0x141c + 4),
                         g_frame_counter * 4 + 0x40);
    }

    *destroy_flag = 1;
    if (cleanup_result >= 0) {
        network_sync_destroy();  // FUN_803660d8
    }
    *result = 0;
    restore_paired_single();
}

// ============================================================================
// spawn_pickup_dispatch - 0x80095d70
// MASSIVE main pickup dispatcher. Called when a player overlaps a spawn entity.
// Routes to type-specific handler based on spawn_type:
//   0x01 = spawn_pickup_weapon_equip (weapon rack equip)
//   0x02 = spawn_pickup_award_weapon (weapon award)
//   0x08 = spawn_pickup_health
//   0x10 = spawn_pickup_armor
//   0x20 = spawn_pickup_team_swap
//   0x40 = spawn_pickup_weapon_ammo
//   0x80 = spawn_pickup_key
//   0x100 = spawn_teleport_pad_activate (via proximity check)
//   0x200 = spawn_pickup_item_generic
//   0x400 = spawn_pickup_special_destroy
//   0x800 = spawn_pickup_check_eligibility
// Post-pickup: network sync for ranked modes, entity destroy if flagged.
// 3 callers (proximity check, network handlers).
// ============================================================================
void spawn_pickup_dispatch(/* paired-single params */)
{
    u64 pair = get_pickup_pair_entities();
    Entity* spawn_ent = (Entity*)(pair >> 32);
    int entity = (int)pair;
    int slot = spawn_ent->spawn_slot;
    int* character = *(int**)(entity + 0x7c);
    u32* spawn_props = *(u32**)(slot + 0x18);
    int picked_up = 0;
    int destroy_flag = 0;

    // Pre-flight checks
    if (*(int*)(*(int*)(entity + 0x7c) + 8) >= g_player_count && g_game_mode == 10) {
        goto done;  // AI can't pickup in story mode
    }
    if (!network_pickup_allowed()) goto done;  // FUN_8036612c
    if (*(int*)(entity + 0x150) != 0 && (*spawn_props & 0x10) != 0) goto done;
    if ((*spawn_props & 4) != 0) {
        int trigger = trigger_slot_find_by_entity_id(spawn_props[8]);
        if (*(int*)(trigger + 0x10) != 2) goto done;  // trigger not in correct state
    }
    if (g_match_timer == 100 && (*spawn_props & 0x100) != 0) {
        if (!sudden_death_pickup_check(entity)) goto done;
    }

    // Type-based dispatch
    int spawn_type = *(int*)(slot + 0xc);
    switch (spawn_type) {
    case 0x01:
        picked_up = spawn_pickup_weapon_equip(spawn_ent, entity, &destroy_flag);
        break;
    case 0x02:
        picked_up = spawn_pickup_award_weapon(spawn_ent, entity, &destroy_flag);
        break;
    case 0x08:
        picked_up = spawn_pickup_health(spawn_ent, entity, &destroy_flag);
        break;
    case 0x10:
        picked_up = spawn_pickup_armor(spawn_ent, entity, &destroy_flag);
        break;
    case 0x20:
        picked_up = spawn_pickup_team_swap(spawn_ent, entity, &destroy_flag);
        break;
    case 0x40:
        picked_up = spawn_pickup_weapon_ammo(spawn_ent, entity, &destroy_flag);
        break;
    case 0x80:
        picked_up = spawn_pickup_key(spawn_ent, entity, &destroy_flag);
        break;
    case 0x200:
        picked_up = spawn_pickup_item_generic(spawn_ent, entity, &destroy_flag);
        break;
    case 0x400:
        picked_up = spawn_pickup_special_destroy(spawn_ent, entity, &destroy_flag);
        break;
    case 0x800:
        picked_up = spawn_pickup_check_eligibility(spawn_ent, entity, &destroy_flag);
        break;
    }

    if (picked_up != 0) {
        // Record which entity picked up this spawn
        *(int*)(slot + 4) = entity;

        // Network sync for ranked/online modes
        if (g_match_timer == 100 && (g_game_mode == 10 || g_game_mode == 3)) {
            network_sync_pickup();  // FUN_80349af4
        }

        // Cancel any pending interaction timer
        if (spawn_ent->entity_flags != 0) {
            timer_cancel_for_entity((int)spawn_ent);
        }
    }

    // Clear network lock if set
    if (destroy_flag != 0) {
        network_clear_pickup_lock();  // FUN_80366134
    }

    // Destroy spawn entity if both picked_up and destroy flagged
    if (picked_up != 0 && destroy_flag != 0 && spawn_ent != NULL) {
        entity_destroy_with_event(spawn_ent);
    }

done:
    restore_paired_single();
}

// ============================================================================
// spawn_teleport_pad_activate - 0x80096088
// MASSIVE teleporter pad activation. Searches all active spawn slots for a
// matching teleport pad (type 0x100/0x1000) with same category and compatible
// team. Resolves destination zone node, plays sound 0xdbe, teleports player
// to destination with rotation computed from zone facing angles. Updates
// camera position for HUD-owning players. Applies teleport cooldown.
// Called only from spawn_pickup_proximity_check when type==0x100.
// ============================================================================
void spawn_teleport_pad_activate(void)
{
    u64 pair = get_pickup_pair_entities_v2();
    int player = (int)(pair >> 32);
    int* this_ent = (int*)pair;
    int slot = *(int*)(player + 0x7c);
    int entity = *this_ent;
    int player_index = *(int*)(entity + 0x34);

    // Skip if no HUD and not in ranked match, or cooldown active
    if ((*(int*)(entity + 0x150) == 0 && g_match_timer != 100) ||
        (float)this_ent[0x2d0] > 0.0f) {
        goto done;
    }

    // Search for matching destination teleport pad
    int dest_slot = 0;
    int found = 0;
    for (int i = 0; i < g_spawn_active_entity_count; i++) {
        int candidate = g_spawn_active_list_base + found;
        if (*(int*)(candidate + 0x20) == 0x40) {
            int cand_slot = *(int*)(candidate + 0x7c);
            if (cand_slot != 0 && candidate != player &&
                (*(int*)(cand_slot + 0xc) == 0x100 || *(int*)(cand_slot + 0xc) == 0x1000) &&
                *(int*)(cand_slot + 0x14) == *(int*)(slot + 0x14) &&
                (*(int*)(cand_slot + 0x1c) < 1 || *(int*)(cand_slot + 0x1c) == this_ent[0x21])) {
                dest_slot = cand_slot;
                break;
            }
        }
        found += ENTITY_STRIDE;
    }
    if (dest_slot == 0) goto done;

    // Resolve destination zone node position
    int* zone_node = soundpad_resolve_zone_node(*(int*)(dest_slot + 0x38));

    // Story mode without HUD: check if same zone as target
    if (*(int*)(entity + 0x150) == 0) {
        if (this_ent[0x1a0] != *(int*)(dest_slot + 0x38)) goto done;
        this_ent[0x19e] = *(int*)(dest_slot + 0x38);
        if (this_ent[0x1a1] < 0) {
            this_ent[0x1a1] = *(int*)(*(int*)(*(int*)(g_hud_base + 0x14) + 0x7c) + 0x678);
        }
        this_ent[0x26b] = 0;
        this_ent[0x26a] = 0;
        camera_apply_teleport_state(entity, &this_ent[0x19e]);
    }

    // Set teleport cooldown
    this_ent[0x2d0] = (int)TELEPORT_COOLDOWN;

    // Play teleport sound at destination
    int* dest_zone = soundpad_resolve_zone_node(*(int*)(dest_slot + 0x38));
    SoundParams params;
    sound_init_play_params(&params);
    params.flags |= 0xa0;
    params.sound_id = 0xdbe;
    params.positional = 1;
    params.pos[0] = dest_zone[3];
    params.pos[1] = dest_zone[4];
    params.pos[2] = dest_zone[5];
    sound_play_request(&params);

    // Screen flash for teleporting player (if active and has HUD or MP)
    if ((*(int*)(entity + 0x150) != 0 || g_player_count > 1) &&
        g_player_active_flags[player_index] != 0) {
        screen_flash_effect(TELEPORT_FLASH_INTENSITY, &this_ent[0x31a], 0);
    }

    // Compute destination rotation from zone node facing
    float entity_rot_y = *(float*)(entity + 0x70);
    int* src_zone = soundpad_resolve_zone_node(*(int*)(slot + 0x38));
    float angle_diff = entity_rot_y - (float)src_zone[6];
    // Normalize to [0, 2pi)
    while (angle_diff <= -PI) angle_diff += TWO_PI;
    while (angle_diff > PI) angle_diff -= TWO_PI;

    // Apply destination facing
    int* final_zone = soundpad_resolve_zone_node(*(int*)(dest_slot + 0x38));
    float dest_rot = (angle_diff + (float)final_zone[6] - 0.0f) / TWO_PI;
    set_entity_facing_normalized(dest_rot);

    // Move player to destination position
    teleport_entity_to_zone(this_ent, *(int*)(dest_slot + 0x38), 2);

    // Post-teleport screen flash
    if (*(int*)(entity + 0x150) != 0 ||
        g_player_active_flags[*(int*)(entity + 0x34)] != 0) {
        screen_flash_effect(TELEPORT_FLASH_INTENSITY, &this_ent[0x31a], 1);
    }

    // Compute XZ position from rotation + offset using sine table
    u32 sine_idx = (u32)(SINE_TABLE_SCALE *
                   ((SINE_FREQ_TELEPORT * *(float*)(entity + 0x70)) / PI) + SINE_TABLE_OFFSET);
    float dest_x = TELEPORT_RADIUS * g_sine_table[sine_idx & 0x7ff] + *(float*)(entity + 0x64);
    float dest_y = *(float*)(entity + 0x68);
    float dest_z = TELEPORT_RADIUS * g_sine_table[(sine_idx + 0x200) & 0x7ff] +
                   *(float*)(entity + 0x6c);

    // Update entity/camera position
    int* hud = *(int**)(entity + 0x150);
    if (hud == NULL) {
        *(float*)(entity + 0x64) = dest_x;
        *(float*)(entity + 0x68) = dest_y;
        *(float*)(entity + 0x6c) = dest_z;
        *(float*)(entity + 0xec) = dest_x;
        *(float*)(entity + 0xf0) = dest_y;
        *(float*)(entity + 0xf4) = dest_z;
    } else {
        // Smooth camera transition for HUD-owning player
        camera_interpolate_teleport(hud, entity, &dest_x, NULL, NULL, 1, 0);
        *(float*)(entity + 0x64) = dest_x;
        *(float*)(entity + 0x68) = dest_y;
        *(float*)(entity + 0x6c) = dest_z;
        *(float*)(entity + 0xec) = dest_x;
        *(float*)(entity + 0xf0) = dest_y;
        *(float*)(entity + 0xf4) = dest_z;
        // Copy to camera state
        hud[0x2a] = *(int*)(entity + 0x64);
        hud[0x2b] = *(int*)(entity + 0x68);
        hud[0x2c] = *(int*)(entity + 0x6c);
        hud[0x2d] = hud[0x2a];
        hud[0x2e] = (int)((float)hud[0x2b] + (float)hud[0x24]);
        hud[0x2f] = hud[0x2c];
        hud[0x53] = hud[0x2a];
        hud[0x54] = hud[0x2b];
        hud[0x55] = hud[0x2c];
        hud[0x56] = hud[0x2d];
        hud[0x57] = hud[0x2e];
        hud[0x58] = hud[0x2f];
        hud[0x40] = *(int*)(entity + 0x70);
        hud[0x41] = 0;
    }

done:
    restore_paired_single();
}

// ============================================================================
// spawn_stub_empty - 0x800964cc
// Empty no-op stub. Possibly a removed/disabled handler placeholder.
// ============================================================================
void spawn_stub_empty(void)
{
    return;
}

// ============================================================================
// spawn_entity_main_tick - 0x800964d0
// Main per-frame tick for spawned entities. Called from entity update system.
// Checks respawn counter > 1 (entity is alive), then:
// - For type 0x80 (key): calls stub
// - Calls respawn animation/sound tick (FUN_80096cd4)
// - If visible+active: calls spawn_lifetime_tick for despawn timer
// - Decrements per-player pickup cooldown timers (32 slots, short at +0x50)
// - Calls spawn_entity_tick_update for spin/bob/fade
// - Manages collision decay timer
// - Calls spawn_pickup_proximity_check for player overlap detection
// ============================================================================
void spawn_entity_main_tick(/* paired-single params */, Entity* ent)
{
    int slot = ent->spawn_slot;

    // Only tick if respawn counter > 1 (entity is spawned/visible)
    if (*(int*)(slot + 0xc0) <= 1) return;

    // Type 0x80 (key): call stub (was probably debug/special logic)
    if (*(int*)(slot + 0xc) == 0x80) {
        spawn_stub_empty();
    }

    // Respawn animation/sound tick
    spawn_respawn_anim_tick();  // FUN_80096cd4

    // Check if entity is visible and either always-active or props say so
    if ((ent->vis_flags & 2) != 0 &&
        ((ent->vis_flags & 1) != 0 ||
         (*(u32**)(slot + 0x18) != NULL && (**(u32**)(slot + 0x18) & 8) != 0))) {

        // Despawn timer check
        int destroyed = spawn_lifetime_tick(ent);
        if (destroyed != 0) return;

        // Decrement per-player pickup cooldown timers
        int offset = 0;
        for (int i = 0; i < 32; i++) {
            short* timer = (short*)(slot + offset + 0x50);
            if (*timer > 0) {
                *timer = *timer - 1;
            }
            offset += 2;
        }

        // Spin/bob/fade update
        spawn_entity_tick_update();

        // Collision decay (approaching pickup fades collision)
        if ((*(u32*)(slot + 0x10) & 2) == 0) {
            if (*(float*)(slot + 0xd0) > 0.0f) {
                *(float*)(slot + 0xd0) -= g_physics_timestep *
                    (0.0f / (float)g_spawn_lifetime_base);
            }
            // Player proximity/overlap detection
            spawn_pickup_proximity_check();
        }
    }
}

// ============================================================================
// spawn_pickup_proximity_check - 0x800965f4
// MASSIVE proximity detection. Iterates all active player entities, tests
// overlap against the spawn entity's collision volume. Two modes:
// - Flag 8 set: uses spawn def AABB with Y-height check (flag-based mode)
// - Flag 8 clear: cylindrical radius + Y-height band check
// On overlap: dispatches to spawn_teleport_pad_activate (type 0x100) or
// spawn_pickup_dispatch (all other types). Breaks on first successful pickup.
// 3 callers (spawn_entity_main_tick, network handlers).
// ============================================================================
void spawn_pickup_proximity_check(void)
{
    Entity* spawn_ent = get_current_update_entity();  // FUN_803826dc
    int slot = spawn_ent->spawn_slot;

    // Skip if not visible+active
    if ((spawn_ent->vis_flags & 2) == 0) goto done;
    if ((spawn_ent->vis_flags & 1) == 0 &&
        !(*(u32**)(slot + 0x18) != NULL && (**(u32**)(slot + 0x18) & 8) != 0)) {
        goto done;
    }

    // Despawn check
    if (spawn_lifetime_tick(spawn_ent) != 0) goto done;

    // Iterate all active player entities
    for (int i = 0; i < g_active_player_entity_count; i++) {
        int* player_ptr = *(int**)(&g_player_entity_table + i * 4);
        if (player_ptr == NULL) continue;

        int player_ent = *player_ptr;

        // Check if player can interact with this spawn
        if (!spawn_pickup_can_interact(player_ent, (int)spawn_ent)) continue;

        // Compute XZ distance squared
        float dx = (spawn_ent->pos_x + spawn_ent->offset_x) -
                   (*(float*)(player_ent + 0x64) + *(float*)(player_ent + 0x1a8));
        float dz = (spawn_ent->pos_z + spawn_ent->offset_z) -
                   (*(float*)(player_ent + 0x6c) + *(float*)(player_ent + 0x1b0));
        float dist_xz_sq = dx * dx + dz * dz;

        int* props = *(int**)(slot + 0x18);
        bool overlap = false;

        if ((*(u32*)(slot + 0x10) & 8) != 0) {
            // Flag-based AABB mode: uses character height from table
            int hud = *(int*)(player_ent + 0x150);
            float char_height;
            if (hud == 0) {
                // From character def table
                short char_type = *(short*)(player_ptr + 0x12);
                char_height = (player_ptr[9] & 2) ?
                    g_char_height_table_crouch[char_type] :
                    g_char_height_table_stand[char_type];
            } else {
                char_height = ((*(u32*)(hud + 0xc) & 0x10) == 0) ?
                    *(float*)(hud + 0x80) : *(float*)(hud + 0x84);
            }

            // Y-height check + XZ radius check
            float spawn_y = spawn_ent->pos_y + spawn_ent->offset_y;
            float player_y = *(float*)(player_ent + 0x68) + *(float*)(player_ent + 0x1ac);
            float y_diff = fabsf(spawn_y - player_y);

            if (y_diff < (0.5f * char_height + spawn_ent->collision_height) &&
                dist_xz_sq < *(float*)(props + 4)) {
                overlap = true;
            }
        } else {
            // Cylinder mode: Y band + radius
            float spawn_y;
            if ((*(u32*)(slot + 0x10) & 1) == 0 &&
                *(int*)(slot + 0xc) != 1 && *(int*)(slot + 0xc) != 0x200) {
                float rot_offset = get_entity_rotation_offset((int)spawn_ent);
                spawn_y = (*(float*)(slot + 0x24) + spawn_ent->offset_y) - rot_offset;
            } else {
                float rot_offset = get_entity_rotation_offset((int)spawn_ent);
                spawn_y = (spawn_ent->pos_y + spawn_ent->offset_y) - rot_offset;
            }

            float y_extent = spawn_ent->collision_height + *(float*)(props + 8);
            float player_y = *(float*)(player_ent + 0x68);

            float y_low = spawn_y - *(float*)(props + 0xc);
            float y_high = spawn_y + y_extent;

            if (y_low <= player_y && player_y <= y_high &&
                dist_xz_sq < *(float*)(props + 4)) {
                overlap = true;
            }
        }

        if (overlap) {
            if (*(int*)(slot + 0xc) == 0x100) {
                // Teleporter pad
                spawn_teleport_pad_activate();
                break;
            }
            if (*(int*)(slot + 0xc) == 0x1000 ||
                spawn_pickup_dispatch() == 1) {
                break;
            }
        }
    }

done:
    restore_paired_single();
}

// ============================================================================
// spawn_pickup_can_interact - 0x800968dc
// Eligibility filter for spawn pickup interaction. Returns 0 (cannot pickup) if:
// - Player is in cutscene state (HUD field == 0x400)
// - Player has stun/incapacitated flag (bit 8 in character flags)
// - Non-story mode AND player is dead/spectating (state 2 or 3) AND
//   spawn type is not teleport (0x100)
// Otherwise returns 1 (can pickup).
// ============================================================================
int spawn_pickup_can_interact(int player_entity, int spawn_entity)
{
    // Check cutscene state
    if (*(int*)(player_entity + 0x150) != 0) {
        if (*(int*)(*(int*)(player_entity + 0x150) + 0x114) == 0x400) {
            return 0;  // in cutscene
        }
    }

    // Check stun/incapacitated flag
    if ((*(u32*)(*(int*)(player_entity + 0x7c) + 0x24) & 8) != 0) {
        return 0;
    }

    // Non-story mode: dead/spectating players can't pickup non-teleport items
    if (g_game_mode != 10) {
        int player_state = *(int*)(*(int*)(player_entity + 0x7c) + 0x44);
        if ((player_state == 3 || player_state == 2) &&
            *(int*)(*(int*)(spawn_entity + 0x7c) + 0xc) != 0x100) {
            return 0;
        }
    }

    return 1;
}

// ============================================================================
// spawn_weapon_rack_drop - 0x8009695c
// Weapon rack drop/return handler. Called when a player drops a weapon back
// onto a rack. If same team as rack owner: calls spawn_weapon_rack_reequip
// (weapon returned). If different team: awards score kill, displays team
// messages ("weapon stolen" / "weapon captured"), plays sound 0xd6e,
// teleports entity to rack position, updates spawn state.
// 2 callers (spawn_pickup_team_swap, spawn_pickup_weapon_equip re-equip path).
// ============================================================================
void spawn_weapon_rack_drop(/* paired-single params */, int* character, int player,
                           int weapon_entity, int slot)
{
    u64 pair = get_pickup_pair_entities_v7();  // FUN_803826dc
    int* this_char = (int*)(pair >> 32);
    int entity = (int)pair;
    int this_slot = *(int*)(entity + 0x7c);
    int weapon_slot = *(int*)(weapon_entity + 0x7c);

    float pos[3];
    sound_get_emitter_position(this_char, pos);

    // Mark weapon as active + clear collected flag
    *(u32*)(weapon_entity + 0x28) |= 2;
    *(u32*)(weapon_entity + 0x28) &= ~0x80;
    *(u32*)(weapon_entity + 0x18) |= 2;

    int rack_team = *(int*)(this_slot + 0x1c);

    if (this_char[0x21] == rack_team) {
        // Same team: weapon returned to rack
        if (*(int*)(weapon_slot + 0x1c) == rack_team) {
            // Own team's weapon returned
            spawn_weapon_rack_reequip();
        } else {
            // Captured enemy weapon delivered to own rack
            hud_trigger_event(0.0f, 0x28, this_char[2]);
            score_award_kill(this_char, 1);

            // Display "weapon captured" to capturing team
            int offset = 0;
            for (int i = 0; i < g_player_count; i++) {
                if (this_char[0x21] == *(int*)(*(int*)(*(int*)(g_hud_base + offset + 0x14) + 0x7c) + 0x84)) {
                    hud_trigger_flash(*(int*)(g_hud_base + offset + 4), g_frame_counter * 4 + 0x40);
                }
                if (*(int*)(weapon_slot + 0x1c) == *(int*)(*(int*)(*(int*)(g_hud_base + offset + 0x14) + 0x7c) + 0x84)) {
                    int team_name = get_string_by_id(g_team_name_ids[this_char[0x21]]);
                    int fmt = get_string_by_id(0x18a7);
                    char* msg = format_pickup_message("%s  %s", fmt, team_name);
                    hud_show_player_message(g_hud_base + offset, msg);
                }
                offset += 0x141c;
            }

            // Display "weapon lost" to losing team
            offset = 0;
            for (int i = 0; i < g_player_count; i++) {
                if (this_char[0x21] == *(int*)(*(int*)(*(int*)(g_hud_base + offset + 0x14) + 0x7c) + 0x84)) {
                    hud_trigger_flash(*(int*)(g_hud_base + offset + 4), g_frame_counter * 4 + 0x40);
                    char* lost_msg = get_string_by_id(0x18a8);
                    hud_display_message(*(int*)(g_hud_base + offset + 4), 3, lost_msg);
                }
                offset += 0x141c;
            }
            hud_clear_weapon_display(*(int*)(this_slot + 0x1c) + 0x10);
        }
    }

    // Play weapon drop sound at position
    SoundParams params;
    sound_init_play_params(&params);
    params.sound_id = 0xd6e;
    params.positional = 1;
    params.pos[0] = pos[0];
    params.pos[1] = pos[1];
    params.pos[2] = pos[2];
    sound_play_request(&params);

    // Store previous position and teleport to rack position
    *(float*)(weapon_entity + 0xec) = *(float*)(weapon_entity + 0x64);
    *(float*)(weapon_entity + 0xf0) = *(float*)(weapon_entity + 0x68);
    *(float*)(weapon_entity + 0xf4) = *(float*)(weapon_entity + 0x6c);
    *(float*)(weapon_entity + 0x64) = *(float*)(weapon_slot + 0x20);
    *(float*)(weapon_entity + 0x68) = *(float*)(weapon_slot + 0x24);
    *(float*)(weapon_entity + 0x6c) = *(float*)(weapon_slot + 0x28);
    *(float*)(weapon_entity + 0xc0) = *(float*)(weapon_slot + 0x2c);
    *(float*)(weapon_entity + 0x70) = *(float*)(weapon_slot + 0x30);
    *(float*)(weapon_entity + 0xc8) = *(float*)(weapon_slot + 0x34);
    *(int*)(weapon_slot + 0x38) = *(int*)(weapon_slot + 0x3c);

    // Apply position setup unless same-team return
    if (this_char[0x21] != rack_team || *(int*)(weapon_slot + 0x1c) != rack_team) {
        spawn_on_spawn_position_setup(weapon_entity);
    }
    restore_paired_single();
}

// ============================================================================
// spawn_weapon_rack_reequip - 0x80096bf8
// Weapon rack re-equip notification. Triggered when a weapon is returned to
// its own team's rack. Triggers HUD event 0x27 (weapon available), displays
// "weapon returned" message to all same-team players except the returner,
// "weapon available" message to the returner, clears weapon display timer.
// 2 callers (spawn_weapon_rack_drop same-team, spawn_pickup_weapon_equip re-equip).
// ============================================================================
void spawn_weapon_rack_reequip(void)
{
    int player = get_current_update_entity();  // FUN_803826f0

    // Trigger "weapon available" HUD event
    hud_trigger_event(0.0f, 0x27, *(int*)(player + 8));

    // Display messages to team
    int offset = 0;
    for (int i = 0; i < g_player_count; i++) {
        // Same team, different player: "weapon returned"
        if (*(int*)(player + 0x84) ==
            *(int*)(*(int*)(*(int*)(g_hud_base + offset + 0x14) + 0x7c) + 0x84) &&
            i != *(int*)(player + 8)) {
            char* returned_msg = get_string_by_id(0x18a9);
            hud_show_player_message(g_hud_base + offset, returned_msg);
        }
        offset += 0x141c;
    }

    // Display "weapon available" to the player who returned it
    if (*(int*)(player + 8) < g_player_count) {
        char* available_msg = get_string_by_id(0x18aa);
        hud_show_player_message(g_hud_base + *(int*)(player + 8) * 0x141c, available_msg);
    }

    // Clear weapon HUD display timer
    hud_clear_weapon_display(*(int*)(player + 0x84) + 0xc);
    restore_paired_single();
}
