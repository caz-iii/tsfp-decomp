// Address: 0x80094888 - 0x80095904 | Name: spawn_pickup_handlers | Date: 2026-08-11
// Spawn pickup interaction system: callbacks triggered when a player collects
// items, weapons, health, armor, or keys from spawned pickup entities.
// Central handler (spawn_pickup_collected) is called by all pickup types.

#include "common.h"

// ============================================================================
// spawn_pickup_collected - 0x80094888
// Central pickup handler called by ALL pickup event functions (9 callers).
// Clears render table visibility bit, decrements global spawn count,
// deactivates sound entities for types 1/0x200, sets up respawn timer
// (calls spawn_on_spawn_position_setup via delayed callback). For non-type-1
// pickups: resolves respawn delay from spawn properties or mode-specific table.
// ============================================================================
void spawn_pickup_collected(int player_entity)
{
    int slot = *(int*)(player_entity + 0x7c);

    // Clear render table visibility bit for this spawn slot
    if (*(int*)(slot + 0x9c) >= 0) {
        int render_entry = g_render_table_base + *(int*)(slot + 0x9c) * 0x20;
        *(u32*)(render_entry + 4) &= ~0x200000;
    }

    // Decrement active spawn count
    g_spawn_total_count--;

    int spawn_type = *(int*)(slot + 0xc);

    // Non-type-1 and non-type-0x200: set up respawn with timer
    if (spawn_type != 1) {
        if (spawn_type != 0x200 && (*(u32*)(slot + 0x10) & 1) == 0 &&
            *(int*)(player_entity + 0x1a0) == 0) {

            // Clear visibility/collision flags
            if (*(u32**)(slot + 0x18) == NULL || (**(u32**)(slot + 0x18) & 0x20) == 0) {
                *(u32*)(player_entity + 0x28) &= 0xffffff9c;
                *(u32*)(slot + 0x10) &= ~8;
                *(u32*)(player_entity + 0x28) &= 0xffff3fff;
            }

            // Determine respawn delay
            double respawn_delay;
            if (g_game_mode == 3 && g_match_timer != 100) {
                respawn_delay = (double)RESPAWN_DELAY_DEATHMATCH;
            } else if (g_game_mode == 10 && (g_mode_flags & 0x40) == 0) {
                respawn_delay = (double)*(float*)(*(int*)(slot + 0x18) + 0x10);
            } else if (*(int*)(slot + 0x18) == 0) {
                if (*(int*)(slot + 0xc) == 0x80) {
                    respawn_delay = (double)RESPAWN_DELAY_SPECIAL;
                } else {
                    respawn_delay = (double)RESPAWN_DELAY_DEFAULT;
                }
            } else {
                respawn_delay = (double)*(float*)(*(int*)(slot + 0x18) + 0x10);
            }

            // Register delayed respawn callback
            if (respawn_delay > 0.0) {
                timer_register_callback(respawn_delay, 0, player_entity,
                                       spawn_on_spawn_position_setup);
            }
            goto done;
        }
    }

    // Type 1 (one-shot) or type 0x200 (quest item): permanent removal
    if (spawn_type == 1 || spawn_type == 0x200) {
        // Deactivate associated sound entities
        int sound_a = *(int*)(slot + 0x38);
        bool in_range = (sound_a > 0x209d && sound_a < 0x20b2);
        if (in_range) {
            soundpad_deactivate_entity(sound_a);
        } else {
            int sound_b = *(int*)(slot + 0x9c);
            in_range = (sound_b > 0x209d && sound_b < 0x20b2);
            if (in_range) {
                soundpad_deactivate_entity(sound_b);
            }
        }
        *(int*)(slot + 0x9c) = -1;
        *(int*)(slot + 0x38) = -1;

        // Mark as permanently collected
        *(u32*)(player_entity + 0x28) |= 0x80;
        *(int*)(slot + 0xa0) = 4;  // state: collected permanently
        *(u32*)(player_entity + 0x28) &= ~2;  // clear active bit
        *(u32*)(player_entity + 0x18) |= 2;   // set collected bit
    }

done:
    post_pickup_event_dispatch();  // FUN_8023347c
}

// ============================================================================
// spawn_pickup_health - 0x80094aa4
// Health pickup handler. Checks if player health < max (scaled by type),
// increments health, plays sound 0xd39 at entity position, triggers
// HUD event 0x22 (health pickup notification). Sets flag bit 4 on slot.
// ============================================================================
void spawn_pickup_health(int attacker, int defender, int* result)
{
    int player_ent = get_pickup_pair_entities();  // returns pair
    int player = (int)(pair >> 32);
    int entity = (int)pair;
    int* character = *(int**)(entity + 0x7c);
    int slot = *(int*)(player + 0x7c);

    // Check health < max (scaled by type)
    if (*(float*)(entity + 0x1cc) < *(float*)(entity + 0x1d8) ||
        (*(u32*)(slot + 0x10) & 0x10) != 0) {

        float pos[3];
        sound_get_emitter_position(character, pos);

        // Process pickup
        spawn_pickup_collected(player);
        character[10] |= 4;  // set health-picked-up flag

        // Increment health (capped at max)
        float increment = *(float*)(entity + 0x1d8) *
                         g_health_increment_table[*(int*)(slot + 0x14)];
        *(float*)(entity + 0x1cc) += increment;
        if (*(float*)(entity + 0x1cc) > *(float*)(entity + 0x1d8)) {
            *(float*)(entity + 0x1cc) = *(float*)(entity + 0x1d8);
        }

        // Play pickup sound if player has HUD
        if (*(int*)(entity + 0x150) != 0) {
            SoundParams params;
            sound_init_play_params(&params);
            params.sound_id = 0xd39;
            params.positional = 1;
            params.pos[0] = pos[0];
            params.pos[1] = pos[1];
            params.pos[2] = pos[2];
            sound_play_request(&params);
        }

        // Trigger HUD pickup event
        hud_trigger_event(0.0f, 0x22, character[2]);
        *result = 1;
    }
    restore_paired_single();
}

// ============================================================================
// spawn_pickup_armor - 0x80094bc0
// Armor/shield pickup handler. Checks if armor < max (type-scaled threshold),
// increments armor, plays sound 0xd3a at entity position, triggers HUD
// event 0x23 (armor pickup). Sets flag bit 0x10 on slot.
// ============================================================================
void spawn_pickup_armor(int attacker, int defender, int* result)
{
    u64 pair = get_pickup_pair_entities();
    int player = (int)(pair >> 32);
    int entity = (int)pair;
    int* character = *(int**)(entity + 0x7c);
    int slot = *(int*)(player + 0x7c);

    float pos[3];
    sound_get_emitter_position(character, pos);

    // Check armor below threshold (scaled by type)
    float max_armor = (float)character[0x1f] *
                     g_armor_scale_table[*(int*)(slot + 0x14)];
    float current_armor = (float)character[0x1e];

    if (ARMOR_PICKUP_THRESHOLD < (max_armor - current_armor) ||
        (*(u32*)(slot + 0x10) & 0x10) != 0) {

        spawn_pickup_collected(player);
        character[10] |= 0x10;  // set armor-picked-up flag

        // Increment armor (capped at scaled max)
        float new_armor = max_armor;
        if (new_armor < current_armor) {
            new_armor = current_armor;
        }
        character[0x1e] = (int)new_armor;

        // Play pickup sound
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

        // Trigger HUD armor event
        hud_trigger_event(0.0f, 0x23, character[2]);
        *result = 1;
    } else {
        *result = 0;
    }
    restore_paired_single();
}

// ============================================================================
// spawn_pickup_key - 0x80094cfc
// Key/objective pickup handler (MASSIVE). 4-type switch for different key
// categories (0=red key 0x10, 1=blue key 0x1000, 2=yellow key 0x400,
// 3=green key 0x800). Checks already-collected flag bitmask, displays
// HUD message with key name, calls key-specific unlock function, plays
// type-specific sound (0xd67/0xd68/0xd3a). Volume scaled by HUD presence.
// ============================================================================
void spawn_pickup_key(/* paired-single params */, int* result)
{
    u64 pair = get_pickup_pair_entities_v2();  // FUN_803826e4
    int player = (int)(pair >> 32);
    int entity = (int)pair;
    int* character = *(int**)(entity + 0x7c);
    int slot = *(int*)(player + 0x7c);
    int key_type = *(int*)(slot + 0x14);
    int sound_id = 0xd3a;

    float pos[3];
    sound_get_emitter_position(character, pos);

    switch (key_type) {
    case 0:  // Red key
        if ((character[0x31b] & 0x10) != 0) {
            *result = 0;
            goto done;
        }
        if (*(int*)(entity + 0x150) != 0) {
            int key_name = get_string_by_id(0xa14);
            int fmt = get_string_by_id(0x1606);
            char buf[292];
            format_string_varargs(buf, fmt, &g_key_format_data, key_name);
            hud_display_message(*(int*)(*(int*)(entity + 0x150) + 4), 2, buf);
        }
        key_unlock_red((int)(character + 0x31a));
        sound_id = 0xd67;
        break;

    case 1:  // Blue key
        if ((character[0x31b] & 0x1000) != 0) {
            *result = 0;
            goto done;
        }
        if (*(int*)(entity + 0x150) != 0) {
            int key_name = get_string_by_id(0xa15);
            int fmt = get_string_by_id(0x1606);
            char buf[292];
            format_string_varargs(buf, fmt, &g_key_format_data, key_name);
            hud_display_message(*(int*)(*(int*)(entity + 0x150) + 4), 2, buf);
        }
        key_unlock_blue((int)(character + 0x31a));
        sound_id = 0xd68;
        break;

    case 2:  // Yellow key (index 2 maps to flag 0x400)
        if ((character[0x31b] & 0x400) != 0) {
            *result = 0;
            goto done;
        }
        if (*(int*)(entity + 0x150) != 0) {
            int key_name = get_string_by_id(0xa17);
            int fmt = get_string_by_id(0x1606);
            char buf[292];
            format_string_varargs(buf, fmt, &g_key_format_data, key_name);
            hud_display_message(*(int*)(*(int*)(entity + 0x150) + 4), 2, buf);
        }
        key_unlock_yellow((int)(character + 0x31a));
        break;

    case 3:  // Green key
        if ((character[0x31b] & 0x800) != 0) {
            *result = 0;
            goto done;
        }
        if (*(int*)(entity + 0x150) != 0) {
            int key_name = get_string_by_id(0xa16);
            int fmt = get_string_by_id(0x1606);
            char buf[292];
            format_string_varargs(buf, fmt, &g_key_format_data, key_name);
            hud_display_message(*(int*)(*(int*)(entity + 0x150) + 4), 2, buf);
        }
        key_unlock_green((int)(character + 0x31a));
        break;
    }

    // Process the pickup
    spawn_pickup_collected(player);

    // Play sound with volume based on HUD presence
    float volume = (*(int*)(entity + 0x150) != 0) ? KEY_VOLUME_WITH_HUD : KEY_VOLUME_DEFAULT;
    SoundParams params;
    sound_init_play_params(&params);
    params.sound_id = sound_id;
    params.positional = 1;
    params.volume = volume;
    params.pos[0] = pos[0];
    params.pos[1] = pos[1];
    params.pos[2] = pos[2];
    sound_play_request(&params);

    *result = 1;
done:
    restore_paired_single();  // FUN_80382730
}

// ============================================================================
// spawn_pickup_item_generic - 0x80094fd0
// Generic quest/story item pickup. Searches character inventory for
// type 0x200 matching slot's item ID. If item not in inventory: adds it,
// displays item name on HUD, plays sound 0xd39, hides entity.
// ============================================================================
void spawn_pickup_item_generic(/* paired-single params */, int* result)
{
    u64 pair = get_pickup_pair_entities();
    int player = (int)(pair >> 32);
    int entity = (int)pair;
    int* character = *(int**)(entity + 0x7c);
    int slot = *(int*)(player + 0x7c);

    float pos[3];
    sound_get_emitter_position(character, pos);

    // Check if player already has this item
    int* existing = inventory_find_item((int)(character + 0xc1), 0x200, *(int*)(slot + 0x14));

    if (existing == NULL) {
        // Add item to inventory
        int item_entry = inventory_add_item((int)(character + 0xc1), player);

        // Display item name on HUD
        if (*(int*)(entity + 0x150) != 0) {
            int priority = 3;
            if (g_game_mode == 10) priority = 2;

            int item_name = get_string_by_id(**(int**)(item_entry + 0xc));
            int fmt = get_string_by_id(0x18a1);
            char* msg = format_pickup_message("%s  %s", fmt, item_name);
            hud_display_message(*(int*)(*(int*)(entity + 0x150) + 4), priority, msg);

            // Process pickup and play sound
            spawn_pickup_collected(player);

            SoundParams params;
            sound_init_play_params(&params);
            params.sound_id = 0xd39;
            params.positional = 1;
            params.pos[0] = pos[0];
            params.pos[1] = pos[1];
            params.pos[2] = pos[2];
            sound_play_request(&params);

            // Hide entity
            *(u32*)(player + 0x28) &= ~1;
        }
        *result = 1;
    }
    restore_paired_single();
}

// ============================================================================
// spawn_play_pickup_sound - 0x80095438
// Simple helper: plays positional sound 0xd6d (generic pickup chime)
// at the given position coordinates.
// ============================================================================
void spawn_play_pickup_sound(int unused, float* position)
{
    SoundParams params;
    sound_init_play_params(&params);
    params.sound_id = 0xd6d;
    params.positional = 1;
    params.pos[0] = position[0];
    params.pos[1] = position[1];
    params.pos[2] = position[2];
    sound_play_request(&params);
}

// ============================================================================
// spawn_pickup_weapon_equip - 0x80095498
// Weapon equip from rack (MASSIVE). Checks no weapon currently equipped in
// that slot, handles arcade mode weapon swap HUD message, calls
// spawn_pickup_collected, attaches weapon entity to player bone, copies
// position for interpolation, displays weapon name HUD messages (type-specific
// string IDs for different weapon categories), triggers event 0x26.
// 1 caller (spawn tick dispatcher).
// ============================================================================
void spawn_pickup_weapon_equip(/* paired-single params */, int* result)
{
    u64 pair = get_pickup_pair_entities_v3();  // FUN_803826d4
    int player = (int)(pair >> 32);
    int entity = (int)pair;
    int slot = *(int*)(entity + 0x7c);
    int spawn_slot = *(int*)(player + 0x7c);

    // Guard: already holding weapon or state 4 (collected)
    if ((*(u32*)(slot + 0x24) & 0x48) != 0 || *(int*)(spawn_slot + 0xa0) == 4) {
        goto done;
    }

    // Check weapon slot is free
    if (*(int*)(slot + 0x80) == 0 &&
        (g_game_mode == 1 || *(int*)(slot + 0x84) != *(int*)(spawn_slot + 0x1c))) {

        // Arcade mode: display "swap weapon" message to other players
        if (g_game_mode == 2 && (*(u32*)(player + 0x28) & 0x80) == 0) {
            char* swap_msg = get_string_by_id(0x189f);
            hud_clear_weapon_display(*(int*)(spawn_slot + 0x1c) + 8);
            for (int i = 0; i < g_player_count; i++) {
                if (*(int*)(spawn_slot + 0x1c) == *(int*)(g_player_data + i * 0xea0 + 0x84)) {
                    hud_show_player_message(g_hud_base + i * 0x141c, swap_msg);
                }
            }
        }

        // Process pickup
        spawn_pickup_collected(player);

        // Link weapon to player slot
        *(int*)(slot + 0x80) = player;
        weapon_rack_setup_link(slot);

        // Attach weapon entity to player bone
        entity_setup_attachment(player, *(int**)(entity + 4),
                              *(int*)(*(int*)(*(int*)(entity + 0x74) + 0x84) + 0x2c),
                              -1, 2);

        // Play pickup chime at entity position
        spawn_play_pickup_sound(player, (float*)(entity + 0x64));

        // Link animation state
        weapon_attach_to_player(entity, *(int*)(slot + 0x80), 1);

        // Store pickup position for interpolation
        *(float*)(player + 0x248) = *(float*)(entity + 0x64);
        *(float*)(player + 0x24c) = *(float*)(entity + 0x68);
        *(float*)(player + 0x250) = *(float*)(entity + 0x6c);

        // Display weapon name HUD message
        if (*(int*)(entity + 0x150) != 0) {
            if (g_game_mode == 2) {
                char* equipped_msg = get_string_by_id(0x18a0);
                hud_display_message(*(int*)(*(int*)(entity + 0x150) + 4), 3, equipped_msg);
            } else if ((g_mode_flags & 0x40) != 0) {
                // Story mode: show weapon name by category
                int weapon_name;
                u16 weapon_cat = *(u16*)(player + 0x94);
                if (weapon_cat == 0xfe) {
                    weapon_name = get_string_by_id(0x689);
                } else if (weapon_cat == 0x2c) {
                    weapon_name = get_string_by_id(0x687);
                } else if (weapon_cat == 0xff) {
                    weapon_name = get_string_by_id(0x688);
                } else {
                    weapon_name = get_string_by_id(0x980);
                }
                int fmt = get_string_by_id(0x18a1);
                char* msg = format_pickup_message("%s  %s", fmt, weapon_name);
                hud_display_message(*(int*)(*(int*)(entity + 0x150) + 4), 3, msg);
            }
        }

        // Trigger weapon equip HUD event
        hud_trigger_event(0.0f, 0x26, *(int*)(slot + 8));
        *result = 1;
    }
    // Arcade mode: re-equip from weapon rack (same player owns rack)
    else if (g_game_mode == 2 &&
             *(int*)(slot + 0x84) == *(int*)(spawn_slot + 0x1c) &&
             (*(u32*)(player + 0x28) & 0x80) != 0) {

        spawn_pickup_collected(player);
        spawn_play_pickup_sound(player, (float*)(player + 0x64));
        spawn_weapon_rack_reequip();  // FUN_80096bf8

        // Store previous position
        *(float*)(player + 0xec) = *(float*)(player + 0x64);
        *(float*)(player + 0xf0) = *(float*)(player + 0x68);
        *(float*)(player + 0xf4) = *(float*)(player + 0x6c);

        // Copy rack position to entity
        *(float*)(player + 0x64) = *(float*)(spawn_slot + 0x20);
        *(float*)(player + 0x68) = *(float*)(spawn_slot + 0x24);
        *(float*)(player + 0x6c) = *(float*)(spawn_slot + 0x28);
        *(float*)(player + 0xc0) = *(float*)(spawn_slot + 0x2c);
        *(float*)(player + 0x70) = *(float*)(spawn_slot + 0x30);
        *(float*)(player + 0xc8) = *(float*)(spawn_slot + 0x34);

        // Update slot state
        *(int*)(spawn_slot + 0x38) = *(int*)(spawn_slot + 0x3c);
        *(u32*)(player + 0x28) &= ~0x80;
        *(u32*)(player + 0x28) |= 1;
        *(int*)(spawn_slot + 0xa0) = 1;

        spawn_on_spawn_position_setup(player);
        *result = 1;
    }

done:
    restore_paired_single();  // FUN_80382720
}

// ============================================================================
// spawn_pickup_weapon_ammo - 0x8009510c
// Weapon/ammo pickup (MASSIVE). Processes both primary (slot 0x40-0x44) and
// secondary (slot 0x48-0x4c) weapon/ammo awards. Checks ammo capacity via
// FUN_801a9a8c, detaches linked entity for weapon rack types (0x20/0x21),
// plays sound 0xd38, displays weapon/ammo name from type table on HUD,
// triggers event 0x24.
// ============================================================================
void spawn_pickup_weapon_ammo(/* paired-single params */, int* result, int* detach_flag)
{
    u64 pair = get_pickup_pair_entities_v4();  // FUN_803826d8
    int player = (int)(pair >> 32);
    int entity = (int)pair;
    int slot = *(int*)(player + 0x7c);
    int* character = *(int**)(entity + 0x7c);

    // Process secondary weapon slot (0x48)
    int weapon_b = *(int*)(slot + 0x48);
    u32 total_awarded = (*(u32*)(slot + 0x10) >> 4) & 1;
    int ammo_awarded_b = 0;

    if (weapon_b > 0 && weapon_b < 0x2c && *(int*)(slot + 0x4c) > 0) {
        short prev_ammo = *(short*)((int)character + weapon_b * 2 + 0x61e);
        int awarded = check_ammo_capacity(character, weapon_b, (short)*(int*)(slot + 0x4c));
        total_awarded += awarded;
        if (awarded != 0 || (*(u32*)(slot + 0x10) & 0x10) != 0) {
            ammo_awarded_b = award_weapon_ammo(character, weapon_b, (int)prev_ammo);
        }
    }

    // Process primary weapon slot (0x40)
    int weapon_a = *(int*)(slot + 0x40);
    if (weapon_a > 0 && weapon_a < 0x2c && *(int*)(slot + 0x44) > 0) {
        short prev_ammo = *(short*)((int)character + weapon_a * 2 + 0x61e);
        int awarded = check_ammo_capacity(character, weapon_a, (short)*(int*)(slot + 0x44));
        total_awarded += awarded;
        if (awarded != 0 || (*(u32*)(slot + 0x10) & 0x10) != 0) {
            int a = award_weapon_ammo(character, weapon_a, (int)prev_ammo);
            ammo_awarded_b += a;
        }
    }

    if (total_awarded != 0) {
        // Detach linked entity for weapon rack types
        if (*(int*)(slot + 0x40) == 0x21 || *(int*)(slot + 0x40) == 0x20) {
            spawn_detach_linked_entity(slot);
            *detach_flag = 1;
        }
        spawn_pickup_collected(player);
    }

    // Display HUD messages and play sound
    if (total_awarded != 0 && *(int*)(entity + 0x150) != 0 &&
        ((*(int*)(slot + 0x40) > 0 && *(int*)(slot + 0x40) < 0x2c) ||
         (*(int*)(slot + 0x48) > 0 && *(int*)(slot + 0x48) < 0x2c)) &&
        ammo_awarded_b == 0) {

        float pos[3];
        sound_get_emitter_position(character, pos);

        SoundParams params;
        sound_init_play_params(&params);
        params.sound_id = 0xd38;
        params.positional = 1;
        params.pos[0] = pos[0];
        params.pos[1] = pos[1];
        params.pos[2] = pos[2];
        sound_play_request(&params);

        // Display primary weapon name
        int wp_a = *(int*)(slot + 0x40);
        if (wp_a > 0 && wp_a < 0x2c && *(int*)(slot + 0x44) > 0) {
            int name_id = get_string_by_id(g_weapon_name_table[wp_a]);
            int fmt;
            if (wp_a == 0x21 || wp_a == 0x20) {
                fmt = get_string_by_id(0x18a2);  // weapon rack format
            } else {
                fmt = get_string_by_id(0x18a1);  // standard format
            }
            char* msg = format_pickup_message("%s  %s", fmt, name_id);
            hud_display_message(*(int*)(*(int*)(entity + 0x150) + 4), 2, msg);
        }

        // Display secondary weapon name
        int wp_b = *(int*)(slot + 0x48);
        if (wp_b > 0 && wp_b < 0x2c && *(int*)(slot + 0x4c) > 0) {
            int name_id = get_string_by_id(g_weapon_name_table[wp_b]);
            int fmt = get_string_by_id(0x18a1);
            char* msg = format_pickup_message("%s  %s", fmt, name_id);
            hud_display_message(*(int*)(*(int*)(entity + 0x150) + 4), 2, msg);
        }
    }

    if (total_awarded != 0) {
        hud_trigger_event(0.0f, 0x24, character[2]);
        *result = 1;
    }
    restore_paired_single();
}

// ============================================================================
// spawn_pickup_check_eligibility - 0x800957e0
// Pre-check for weapon/ammo pickup eligibility. Validates weapon is in
// match list (IDs 1-0x45), checks player slot capacity and per-player
// pickup cooldown timer (stored at slot+0x50 as short per player_index,
// set to frame_counter*5). Calls the actual award function if eligible.
// ============================================================================
void spawn_pickup_check_eligibility(/* paired-single params */, int* result)
{
    u64 pair = get_pickup_pair_entities_v5();  // FUN_803826ec
    int player = (int)(pair >> 32);
    int spawn_slot = *(int*)(player + 0x7c);
    int* character = *(int**)(entity + 0x7c);

    // Validate weapon ID in match list (1-0x45)
    int weapon_id = *(int*)(spawn_slot + 0x14);
    if (weapon_id > 0 && weapon_id < 0x46) {
        weapon_is_in_match_list(weapon_id);
    }

    // Check slot restriction: team mode requires flag bit 1 in weapon table
    if ((*(int*)(character + 0x44) == 1 &&
         (*(u32*)(g_weapon_table + *(int*)(spawn_slot + 0x14) * 0x29c + 0xc) & 2) != 0) ||
        *(int*)(character + 0x44) == 0) {

        // Check per-player pickup cooldown
        u32 player_index = *(u32*)(character + 8);
        if (*(short*)(spawn_slot + player_index * 2 + 0x50) == 0) {

            // Check ammo capacity for both weapon slots
            int can_pickup_a = check_ammo_capacity(character,
                *(int*)(spawn_slot + 0x40), (short)*(int*)(spawn_slot + 0x44));
            int can_pickup_b = check_ammo_capacity(character,
                *(int*)(spawn_slot + 0x48), (short)*(int*)(spawn_slot + 0x4c));

            // Check inventory full flag
            if (*(char*)(character + *(int*)(spawn_slot + 0x14) + 0x54c) == 0 ||
                (((int)player_index >> 31) + ((u32)(g_player_count <= player_index) - 
                 ((int)g_player_count >> 31)) | (*(u32*)(spawn_slot + 0x10) >> 4 & 1))
                 + can_pickup_a + can_pickup_b != 0) {

                // Execute actual pickup award
                weapon_pickup_award_dispatch(character);
                *result = 1;

                // Set cooldown timer: frame_counter * 5
                *(short*)(spawn_slot + *(int*)(character + 8) * 2 + 0x50) =
                    (short)g_frame_counter * 5;
            }
        }
    }
    restore_paired_single();
}

// ============================================================================
// spawn_pickup_award_weapon - 0x80095904
// Awards weapon to player entity. Validates weapon in match list (1-0x45),
// checks team slot restrictions and existing inventory, calls
// spawn_pickup_collected + weapon award dispatch. Handles flag bit 0x40
// for special weapon types. Sets detach flag if needed.
// ============================================================================
void spawn_pickup_award_weapon(/* paired-single params */, int* result, int* detach_flag)
{
    u64 pair = get_pickup_pair_entities_v2();  // FUN_803826e4
    int player = (int)(pair >> 32);
    int entity = (int)pair;
    int spawn_slot = *(int*)(entity + 0x7c);
    int player_slot = *(int*)(player + 0x7c);

    // Validate weapon in match list
    int weapon_id = *(int*)(player_slot + 0x14);
    if (weapon_id > 0 && weapon_id < 0x46) {
        weapon_is_in_match_list(weapon_id);
    }

    // Check restrictions: game mode != virus OR player is not "it"
    if ((g_game_sub_mode != 0x11 || *(u32*)(spawn_slot + 8) != (u32)g_virus_player_id) &&
        // Team mode weapon restriction check
        ((*(int*)(spawn_slot + 0x44) == 1 &&
          (*(u32*)(g_weapon_table + *(int*)(player_slot + 0x14) * 0x29c + 0xc) & 2) != 0) ||
         *(int*)(spawn_slot + 0x44) == 0)) {

        u32 player_index = *(u32*)(spawn_slot + 8);

        // Check ammo capacity
        int shift = (int)g_player_count >> 31;
        bool over_limit = (g_player_count <= player_index);
        int can_a = check_ammo_capacity(spawn_slot,
            *(int*)(player_slot + 0x40), (short)*(int*)(player_slot + 0x44));
        int can_b = check_ammo_capacity(spawn_slot,
            *(int*)(player_slot + 0x48), (short)*(int*)(player_slot + 0x4c));

        // Check if player can accept this weapon
        if (*(char*)(spawn_slot + *(int*)(player_slot + 0x14) + 0x54c) == 0 ||
            (((int)player_index >> 31) + ((u32)over_limit - shift) |
             (*(u32*)(player_slot + 0x10) >> 4 & 1)) + can_a + can_b != 0) {

            // Process pickup
            spawn_pickup_collected(player);
            weapon_award_to_player(spawn_slot);

            // Special weapon type: apply loadout flag
            if (*(u32**)(player_slot + 0x18) != NULL &&
                (**(u32**)(player_slot + 0x18) & 0x40) != 0) {
                weapon_apply_loadout_flag(spawn_slot, *(int*)(player_slot + 0x14));
            }

            // Set detach flag if flag bit 0 set
            if ((*(u32*)(player_slot + 0x10) & 1) != 0) {
                *detach_flag = 1;
            }
            *result = 1;
        }
    }
    restore_paired_single();
}
