// Address: 0x8008bbbc - 0x8008c0dc | Name: switch_event_system | Date: 2026-08-11
// Switch/button event system: pool management, action handlers for lights,
// triggers (lock/unlock, open/close), multi-target dispatch, sequence stepping,
// activation state, and item equip finalization.

#include "common.h"

// Switch event pool: 0x690 bytes at 0x8055f8b0, entries are 0x54 (84) bytes each
// Max entries = 0x690 / 0x54 = 30 slots
extern u8 g_switch_event_pool[];  // DAT_8055f8b0
extern int g_switch_event_count;  // DAT_80611cc0

#define SWITCH_SLOT_SIZE  0x54
#define SWITCH_SLOT_MAX   30

// ============================================================================
// 0x8008bbbc | switch_get_slot_by_index
// Returns pointer to switch event slot at given index.
// Each slot is 0x54 bytes in the pool at 0x8055f8b0.
// ============================================================================
void* switch_get_slot_by_index(int index) {
    return &g_switch_event_pool[index * SWITCH_SLOT_SIZE];
}

// ============================================================================
// 0x8008bbd0 | switch_find_slot_by_entity
// Linear search through active switch slots for one matching entity ID.
// Returns slot pointer or NULL if not found.
// ============================================================================
void* switch_find_slot_by_entity(int entity_id) {
    int offset = 0;
    for (int i = 0; i < g_switch_event_count; i++) {
        if (*(int*)(&g_switch_event_pool[offset]) == entity_id) {
            return switch_get_slot_by_index(i);
        }
        offset += SWITCH_SLOT_SIZE;
    }
    return NULL;
}

// ============================================================================
// 0x8008bc34 | switch_action_toggle_visibility
// Handles switch actions that toggle scene node visibility (lights/objects).
// Resolves target scene node via FUN_8025c8e8, then calls FUN_8025caf0
// to set visibility based on action type:
//   Type 0: Toggle (flip current state)
//   Type 1: Conditional on activation count (same as toggle)
//   Type 2: Set based on activation state (on if state==1, off otherwise)
//   Type 3: Toggle (same as type 0)
// ============================================================================
void switch_action_toggle_visibility(int slot) {
    int scene_node = FUN_8025c8e8(*(int*)(slot + 0x24));

    FUN_8008c6a4(slot); // update switch state

    int action_type = *(int*)(slot + 0xC);

    if (action_type == 2) {
        // Set visibility based on activation state
        if (*(int*)(slot + 0x18) == 1) {
            FUN_8025caf0(scene_node, 1, 0); // show
        }
        else {
            FUN_8025caf0(scene_node, 0, 0); // hide
        }
    }
    else if (action_type == 0) {
        // Toggle: flip current visibility
        // DAT_805cb94c is scene node visibility table, stride 8
        if ((&DAT_805cb94c)[scene_node * 8] == 1) {
            FUN_8025caf0(scene_node, 0, 0); // currently on -> off
        }
        else {
            FUN_8025caf0(scene_node, 1, 0); // currently off -> on
        }
    }
    else if (action_type == 1) {
        // Conditional: if not already on, turn on
        if ((&DAT_805cb94c)[scene_node * 8] != 1) {
            FUN_8025caf0(scene_node, 1, 0);
        }
    }
    else if (action_type == 3) {
        // Toggle (same as type 0)
        if ((&DAT_805cb94c)[scene_node * 8] == 1) {
            FUN_8025caf0(scene_node, 0, 0);
        }
        else {
            FUN_8025caf0(scene_node, 1, 0);
        }
    }
}

// ============================================================================
// 0x8008bd90 | switch_action_lock_trigger
// Handles switch actions that lock/unlock triggers (doors/gates).
// Resolves target trigger entity, then sets or clears flag 2 (locked)
// based on action type:
//   Type 0-2: Set/clear based on activation state (state 1 = unlock)
//   Type 3: Toggle lock state
// ============================================================================
void switch_action_lock_trigger(int slot) {
    FUN_8008c6a4(slot); // update switch state

    int action_type = *(int*)(slot + 0xC);

    if (action_type == 3) {
        // Toggle lock state
        int trigger = trigger_slot_resolve_entity(*(int*)(slot + 0x24));
        if (trigger != 0) {
            if ((*(u32*)(*(int*)(trigger + 0x7C) + 0xC) & 2) == 0) {
                trigger_set_flags(trigger, 2);   // lock
            }
            else {
                trigger_clear_flags(trigger, 2); // unlock
            }
        }
    }
    else if (action_type >= 0 && action_type < 3) {
        // Set based on activation state
        int trigger = trigger_slot_resolve_entity(*(int*)(slot + 0x24));
        if (*(int*)(slot + 0x18) == 1) {
            trigger_clear_flags(trigger, 2); // activated = unlock
        }
        else {
            trigger_set_flags(trigger, 2);   // deactivated = lock
        }
    }
}

// ============================================================================
// 0x8008be40 | switch_event_callback_open_close
// Event callback wrapper: looks up switch slot by entity ID from event data,
// then dispatches to switch_action_open_close_trigger.
// ============================================================================
void switch_event_callback_open_close(int* event_data) {
    int entity_id = *(int*)(*event_data + 0x44);
    void* slot = switch_find_slot_by_entity(entity_id);
    switch_action_open_close_trigger((int)slot);
}

// ============================================================================
// 0x8008be74 | switch_action_open_close_trigger
// Handles switch actions that open/close doors/triggers.
// Action types:
//   Type 0-2: Open if activation state==1, close otherwise
//   Type 3: Toggle (open if idle, close if moving)
// ============================================================================
void switch_action_open_close_trigger(int slot) {
    FUN_8008c6a4(slot); // update switch state

    int action_type = *(int*)(slot + 0xC);

    if (action_type == 3) {
        // Toggle: open if idle, close if moving
        int trigger = trigger_slot_resolve_entity(*(int*)(slot + 0x24));
        int moving = trigger_is_moving(trigger);
        if (moving == 0) {
            trigger_open_full(trigger, 1);
        }
        else {
            trigger_close_if_idle(trigger);
        }
    }
    else if (action_type >= 0 && action_type < 3) {
        // Based on activation state
        int trigger = trigger_slot_resolve_entity(*(int*)(slot + 0x24));
        if (*(int*)(slot + 0x18) == 1) {
            trigger_open_full(trigger, 1);
        }
        else {
            trigger_close_if_idle(trigger);
        }
    }
}

// ============================================================================
// 0x8008bf14 | switch_action_open_close_multi
// Multi-target variant of open/close. Iterates an array of target entity IDs
// (stored at slot+0x30, terminated by -1) and applies open/close to each.
// ============================================================================
void switch_action_open_close_multi(void) {
    int slot = FUN_803826f0();
    int* target_list = *(int**)(slot + 0x30);

    FUN_8008c6a4(slot); // update switch state

    for (; *target_list >= 0; target_list++) {
        int target_id = *target_list;
        int action_type = *(int*)(slot + 0xC);

        if (action_type == 3) {
            // Toggle
            int trigger = trigger_slot_resolve_entity(target_id);
            int moving = trigger_is_moving(trigger);
            if (moving == 0) {
                trigger_open_full(trigger, 1);
            }
            else {
                trigger_close_if_idle(trigger);
            }
        }
        else if (action_type >= 0 && action_type < 3) {
            int trigger = trigger_slot_resolve_entity(target_id);
            if (*(int*)(slot + 0x18) == 1) {
                trigger_open_full(trigger, 1);
            }
            else {
                trigger_close_if_idle(trigger);
            }
        }
    }

    FUN_8038273c(); // stack restore
}

// ============================================================================
// 0x8008bfcc | switch_sequence_step
// Advances through a sequenced event list. The sequence is stored as an array
// of event IDs at the slot's target offset. Steps forward one entry per call.
// Calls FUN_8023cc28 to resolve/activate each event. Stops when one succeeds
// or hits the -1 terminator (resets sequence index to -1).
// ============================================================================
void switch_sequence_step(void) {
    u64 packed = FUN_803826ec();
    int slot = (int)(packed >> 32);
    int* sequence = (int*)(int)packed;

    bool done = false;
    *(int*)(slot + 0x34) += 1; // advance sequence index

    do {
        if (done) {
            FUN_80382738(); // stack restore
            return;
        }

        u32 event_id = *(u32*)((int)sequence + *(int*)(slot + 0x34) * 4);

        if (event_id == 0xFFFFFFFF) {
            // End of sequence
            *(int*)(slot + 0x34) = -1;
            FUN_80382738();
            return;
        }

        int result = FUN_8023cc28(event_id);
        if (result != 0) {
            done = true; // event succeeded, stop stepping
        }

        if (!done) {
            *(int*)(slot + 0x34) += 1; // try next
        }
    } while (true);
}

// ============================================================================
// 0x8008c070 | switch_set_activation_state
// Sets the activation state of a resolved event entity based on its flags:
//   Flag 0x100 set -> state = 1 (active)
//   Flag 0x8 set   -> state = 2 (pending)
// Also swaps flag bit 0x1 to 0x20 (marks as processed).
// ============================================================================
void switch_set_activation_state(int slot) {
    int event = FUN_8023cc28(*(u32*)(slot + 0x24));

    if ((*(u32*)(event + 0x8) & 0x100) != 0) {
        *(int*)(event + 0x10) = 1; // active
    }
    else if ((*(u32*)(event + 0x8) & 0x8) != 0) {
        *(int*)(event + 0x10) = 2; // pending
    }

    // Swap flag: clear bit 0x1, set bit 0x20
    if ((*(u32*)(event + 0x8) & 0x1) != 0) {
        *(u32*)(event + 0x8) &= ~0x1;
        *(u32*)(event + 0x8) |= 0x20;
    }
}

// ============================================================================
// 0x8008c0dc | switch_item_equip_finalize
// Finalizes weapon/item equip for a player. Sets movement flags, updates HUD,
// assigns weapon to player slot, initializes animation, and sets camera FOV.
// Called after item pickup is confirmed and player entity is resolved.
// ============================================================================
void switch_item_equip_finalize(void) {
    u64 packed = FUN_803826f0();
    int event_slot = (int)(packed >> 32);
    int event_id = (int)packed;

    int player_entity = *(int*)(*(int*)(event_slot + 0x1C) + 0x7C);
    int* weapon_data = (int*)FUN_8023cc28(event_id);
    int player_slot_ptr = *(int*)(*(int*)(event_slot + 0x1C) + 0x150);

    if (player_slot_ptr == 0) {
        FUN_8038273c();
        return;
    }

    // Set player movement flag (weapon-equipped mode)
    FUN_800ab2a4(player_slot_ptr, 0x80);

    // Reset fire timer
    *(int*)(*(int*)(*(int*)(event_slot + 0x1C) + 0x150) + 0xE1C) = 0;

    // Set event active flag
    *(u32*)(event_slot + 0x10) |= 0x100000;

    // Camera cut if event has flag 0x10
    if ((weapon_data[2] & 0x10) != 0) {
        FUN_80240710(weapon_data);
    }

    // HUD update
    int hud_handle = *(int*)(*(int*)(*(int*)(event_slot + 0x1C) + 0x150) + 8);
    if (hud_handle != 0) {
        FUN_8009d830(hud_handle, 2, *weapon_data);
    }

    // Weapon slot assignment
    if (*(int*)(player_entity + 0x94) == *(int*)(player_entity + 0x98)) {
        *(int*)(player_entity + 0x98) = *(int*)(player_entity + 0x94);
    }
    *(int*)(event_slot + 0x4C) = *(int*)(player_entity + 0x94);
    *(int*)(player_entity + 0x94) = 0;

    // Initialize weapon animation
    if (*(short*)(*weapon_data + 0x94) == 0) {
        FUN_801afd14(*(int*)(*(int*)(event_slot + 0x1C) + 0x150),
                     *(int*)(weapon_data[0x1B] + 0x30));
    }
    else {
        FUN_801afd14(*(int*)(*(int*)(event_slot + 0x1C) + 0x150), 0);
    }

    // Set camera FOV from weapon data
    int camera_handle = *(int*)(*(int*)(*(int*)(event_slot + 0x1C) + 0x150) + 8);
    if (camera_handle != 0) {
        int fov_data = FUN_8023d2c0(weapon_data[3]);
        float fov = *(float*)(fov_data + 0x2C);
        if (fov <= FLOAT_806143f0) {
            fov = (float)FUN_8009c278(camera_handle);
        }
        *(float*)(camera_handle + 0x388) = fov;
        *(float*)(camera_handle + 0x384) = fov;
    }

    // Link weapon to player
    *(int*)(player_entity + 0xC14) = *weapon_data;
    weapon_data[0x36] = *(int*)(event_slot + 0x1C);
    weapon_data[0x37] = (int)FLOAT_806143f0;
    *(u32*)(*weapon_data + 0x28) |= 0x4000000; // attached flag

    FUN_8038273c(); // stack restore
}

// ============================================================================
// 0x8008c26c | switch_item_unequip
// Detaches weapon from player. Resets weapon slot, clears HUD display,
// restores camera FOV to default, and returns the weapon slot ID to the
// player entity. Counterpart to switch_item_equip_finalize.
// ============================================================================
void switch_item_unequip(void) {
    u64 packed = FUN_803826f0();
    int event_slot = (int)(packed >> 32);
    int event_id = (int)packed;

    int player_entity_data = *(int*)(*(int*)(event_slot + 0x1C) + 0x7C);
    int weapon_data = FUN_8023cc28(event_id);

    // Only unequip if this player owns the weapon
    if (*(int*)(event_slot + 0x1C) != *(int*)(weapon_data + 0xD8)) {
        *(int*)(weapon_data + 0xD8) = 0;
        FUN_8038273c();
        return;
    }

    if (*(int*)(*(int*)(event_slot + 0x1C) + 0x150) == 0) {
        *(int*)(weapon_data + 0xD8) = 0;
        FUN_8038273c();
        return;
    }

    // Clear weapon slot on player
    *(int*)(player_entity_data + 0xC14) = 0;

    // Skip full teardown if player is in cutscene mode (0x10)
    int player_slot = *(int*)(*(int*)(event_slot + 0x1C) + 0x150);
    if (*(int*)(player_slot + 0x114) == 0x10) {
        *(int*)(weapon_data + 0xD8) = 0;
        FUN_8038273c();
        return;
    }

    // Set weapon state flag if applicable
    if ((*(u32*)(weapon_data + 0x8) & 0x100) != 0) {
        *(int*)(weapon_data + 0x10) = 1;
    }

    // Clear player movement flag
    FUN_800ab2a4(player_slot, 2);
    *(int*)(player_slot + 0xE1C) = 0;

    // Reset HUD weapon display
    int hud_handle = *(int*)(player_slot + 8);
    if (hud_handle != 0) {
        FUN_8009d830(hud_handle, 0, *(int*)(event_slot + 0x1C));
        // Restore default camera FOV
        float default_fov = (float)FUN_8009c278(*(int*)(player_slot + 8));
        *(float*)(*(int*)(player_slot + 8) + 0x388) = default_fov;
        *(float*)(*(int*)(player_slot + 8) + 0x384) = default_fov;
    }

    // Return weapon slot to player entity
    *(int*)(player_entity_data + 0x94) = *(int*)(event_slot + 0x4C);
    *(int*)(player_entity_data + 0x98) = 0;

    *(int*)(weapon_data + 0xD8) = 0;
    FUN_8038273c(); // stack restore
}

// ============================================================================
// 0x8008c380 | switch_event_callback_weapon_swap
// Event callback wrapper for weapon swap. Looks up switch slot by entity ID
// from event data, then dispatches to switch_weapon_swap_handler.
// ============================================================================
void switch_event_callback_weapon_swap(int event_data) {
    switch_find_slot_by_entity(*(int*)(event_data + 0x40));
    switch_weapon_swap_handler();
}

// ============================================================================
// 0x8008c3b8 | switch_get_current_event_id
// Returns the current weapon/event ID for the switch slot.
// If slot+0x24 > 0: direct ID return.
// If sequence index == -1: advance via switch_sequence_step.
// Otherwise: read from sequence array at current index.
// ============================================================================
int switch_get_current_event_id(int entity) {
    int switch_data = *(int*)(entity + 0x7C);
    int direct_id = *(int*)(switch_data + 0x24);

    if (direct_id >= 1) {
        return direct_id;
    }

    if (*(int*)(switch_data + 0x34) == -1) {
        return switch_sequence_step();
    }
    else {
        int* sequence = *(int**)(switch_data + 0x30);
        return sequence[*(int*)(switch_data + 0x34)];
    }
}

// ============================================================================
// 0x8008c410 | switch_weapon_swap_handler
// MASSIVE handler for weapon rack/swap interaction. Core logic:
// 1. Resolves current weapon event ID via switch_get_current_event_id
// 2. Checks if another player already owns the weapon (busy check)
// 3. If sequence mode: also gets next weapon in sequence
// 4. If player has no weapon equipped (state==0 & not activated):
//    - Plays pickup sound, calls switch_item_equip_finalize
// 5. If player already has weapon (state==1, activated):
//    - Calls switch_item_unequip, then equips next if available
// Handles edge cases: cutscene mode (0x10), multi-player ownership conflicts.
// ============================================================================
void switch_weapon_swap_handler(void) {
    int slot = FUN_803826e4();
    int pickup_sound_id = FUN_80241854(); // get pickup sound

    u32 next_event_id = 0xFFFFFFFF;
    u32 current_id = switch_get_current_event_id(*(int*)(slot + 0x8));

    // Sequence mode: get next weapon ID if applicable
    if (*(int*)(slot + 0x24) < 1 && *(int*)(slot + 0x30) != 0 &&
        *(int*)(slot + 0x18) == 1) {
        next_event_id = switch_sequence_step();
        if (next_event_id == current_id) {
            next_event_id = 0xFFFFFFFF; // same weapon, treat as none
        }
    }

    if ((int)current_id < 1) goto end;

    int weapon_entity = FUN_8023cc28(current_id);

    // Check if weapon is already owned by another player
    if (*(int*)(slot + 0x24) < 1 && *(int*)(slot + 0x30) != 0) {
        // Sequence mode: check all entries for ownership conflicts
        int* sequence = *(int**)(slot + 0x30);
        for (int i = 0; sequence[i] != -1; i += 1) {
            int id = sequence[i];
            if (id < 1) continue;
            int ent = FUN_8023cc28(id);
            if (ent == 0) continue;
            int owner = *(int*)(ent + 0xD8);
            if (owner == 0) continue;
            if (owner == *(int*)(slot + 0x1C)) continue;
            // Another player owns a weapon in this rack: abort
            if (FLOAT_806143f0 < *(float*)(owner + 0x1CC)) goto end;
        }
    }
    else {
        // Single mode: check ownership
        int owner = *(int*)(weapon_entity + 0xD8);
        if (owner != 0 && owner != *(int*)(slot + 0x1C) &&
            FLOAT_806143f0 < *(float*)(owner + 0x1CC)) goto end;
    }

    // Execute swap based on current action state
    if (*(int*)(slot + 0xC) == 0) {
        if (*(int*)(slot + 0x18) == 0 &&
            *(int*)(*(int*)(*(int*)(slot + 0x1C) + 0x150) + 0x114) != 0x10) {
            // Not activated, not in cutscene: pick up weapon
            switch_toggle_state(slot);
            // Play pickup sound at entity position
            int sound_params[7];
            int entity = *(int*)(slot + 0x8);
            sound_init_play_params((int)sound_params);
            // ... set 3D position from entity
            sound_params[0] = pickup_sound_id;
            DAT_80610468 = sound_play_request(sound_params);
            switch_item_equip_finalize();
        }
        else {
            // Already activated: unequip current
            switch_item_unequip();
            if ((int)next_event_id < 1 ||
                *(int*)(*(int*)(*(int*)(slot + 0x1C) + 0x150) + 0x114) == 0x10) {
                // No next weapon or in cutscene: just toggle off
                switch_toggle_state(slot);
                sound_stop_handle();
            }
            else {
                // Swap to next weapon
                switch_item_equip_finalize();
            }
        }
    }

end:
    FUN_80382730(); // stack restore
}

// ============================================================================
// 0x8008c61c | switch_update_visual_state
// Updates render flags on the switch entity to reflect activation state.
// Toggles between render bits 2 (off model) and 4 (on model).
// Level 100 has a special handler (FUN_80351b9c).
// ============================================================================
void switch_update_visual_state(int slot) {
    int entity = *(int*)(slot + 0x8);

    if (DAT_80612614 == 100) {
        FUN_80351b9c(); // level 100 special
        return;
    }

    int render_data = *(int*)(entity + 0x4);

    if (*(int*)(slot + 0x18) == 0) {
        // Deactivated: show "off" model (bit 2), hide "on" model (bit 4)
        *(u32*)(render_data + 0x30) &= ~0x4;
        *(u32*)(render_data + 0x30) |= 0x2;
    }
    else {
        // Activated: show "on" model (bit 4), hide "off" model (bit 2)
        *(u32*)(render_data + 0x30) &= ~0x2;
        *(u32*)(render_data + 0x30) |= 0x4;
    }
}

// ============================================================================
// 0x8008c6a4 | switch_toggle_state
// Core state toggle helper called by all switch action handlers.
// If flag 4 is NOT set: flips activation (0↔1) and calls visual update.
// If flag 4 IS set: sets entity attached flag and starts animation.
// Flag 0x4000 suppresses visual update (used for silent toggles).
// ============================================================================
void switch_toggle_state(int slot) {
    if ((*(u32*)(slot + 0x10) & 4) == 0) {
        // Simple toggle
        if (*(int*)(slot + 0x18) == 0) {
            *(int*)(slot + 0x18) = 1; // activate
            if ((*(u32*)(slot + 0x10) & 0x4000) == 0) {
                switch_update_visual_state(slot);
            }
        }
        else {
            *(int*)(slot + 0x18) = 0; // deactivate
            if ((*(u32*)(slot + 0x10) & 0x4000) == 0) {
                switch_update_visual_state(slot);
            }
        }
    }
    else {
        // Animated switch: set attached flag and start animation
        int entity = *(int*)(slot + 0x8);
        *(u32*)(entity + 0x28) |= 0x4000000;
        switch_start_animation(slot);
    }
}

// ============================================================================
// 0x8008c724 | switch_start_animation
// Sets animation direction based on current state:
//   state 0 → direction 2 (closing/deactivating)
//   state 1 → direction 1 (opening/activating)
// Then flips the state (0↔1).
// ============================================================================
void switch_start_animation(int slot) {
    if (*(int*)(slot + 0x18) == 0) {
        *(int*)(slot + 0x3C) = 2; // close direction
    }
    else {
        *(int*)(slot + 0x3C) = 1; // open direction
    }
    *(int*)(slot + 0x18) = 1 - *(int*)(slot + 0x18); // flip state
}

// ============================================================================
// 0x8008c754 | switch_anim_tick_open
// Animation tick for opening switches. Increments entity+0xC0 toward
// target angle (from switch_data+0x28 or default FLOAT_806143f4).
// When target reached: clears animation state and attached flag.
// ============================================================================
void switch_anim_tick_open(int entity) {
    int switch_data = *(int*)(entity + 0x7C);
    float angle_limit = *(float*)(switch_data + 0x28);
    float target = FLOAT_806143f4; // default max angle
    if (angle_limit != FLOAT_806143f0) {
        target = angle_limit;
    }

    if (*(float*)(entity + 0xC0) < target) {
        // Still animating: increment by frame time
        *(float*)(entity + 0xC0) += FLOAT_806108ec;
        *(u32*)(entity + 0x18) |= 2; // mark dirty
    }
    else {
        // Animation complete
        float final_target = FLOAT_806143f4;
        if (angle_limit != FLOAT_806143f0) {
            final_target = angle_limit;
        }
        *(float*)(entity + 0xC0) = final_target;
        *(int*)(switch_data + 0x3C) = 0; // clear animation state
        // Clear attached flag
        *(u32*)(*(int*)(switch_data + 0x8) + 0x28) &= ~0x4000000;
    }
}

// ============================================================================
// 0x8008c7d4 | switch_bone_matrix_update
// Builds rotation matrix for animated switch/lever model.
// Gets bone position from skeleton, builds entity local matrix,
// applies rotation transform, and writes to entity render bone table.
// Flag 0x800 applies additional offset from data constants.
// ============================================================================
void switch_bone_matrix_update(void) {
    int entity = FUN_803826f0();
    int switch_data = *(int*)(entity + 0x7C);

    // Load rotation axis from data constants
    float axis[3] = { DAT_803cc398, DAT_803cc39c, DAT_803cc3a0 };

    // Get bone position from skeleton
    int* bone_table = *(int**)(*(int*)(entity + 0x4) + 0x4);
    float* bone_pos = (float*)bone_table[*bone_table * -0x30 + 0x1B];

    // Build entity local matrix
    float local_mtx[16];
    entity_build_local_matrix(local_mtx, entity, 0, 0);

    // Get pivot position (from bone or default)
    float pivot[3];
    if (bone_pos == NULL) {
        pivot[0] = FLOAT_806143f0;
        pivot[1] = FLOAT_806143f0;
        pivot[2] = FLOAT_806143f0;
    }
    else {
        pivot[0] = bone_pos[0];
        pivot[1] = bone_pos[1];
        pivot[2] = bone_pos[2];
    }

    // Transform pivot by local matrix
    FUN_8012fb90(local_mtx, pivot);

    // Apply additional offset if flag 0x800 set
    if ((*(u32*)(switch_data + 0x10) & 0x800) != 0) {
        FUN_8012fb90(local_mtx, axis);
    }

    // Write to render bone table
    int bone_mtx_base = *(int*)(*(int*)(entity + 0x4) + 0x10);
    FUN_8012ef38(bone_mtx_base); // identity init
    FUN_80130374(bone_mtx_base + 0x40, axis, pivot);

    // Mark entity as bone-dirty
    *(u32*)(entity + 0x28) |= 0x200000;

    FUN_8038273c(); // stack restore
}

// ============================================================================
// 0x8008c8f0 | switch_anim_tick_bidirectional
// Animation tick for switches with both open and close directions.
// Direction 2 (closing): decrements entity+0xD0 toward -target.
// Direction 1 (opening): increments entity+0xD0 toward +target.
// When complete: clears animation state and attached flag.
// Registers self for continued per-frame update via entity_register_for_update.
// ============================================================================
void switch_anim_tick_bidirectional(int entity) {
    int switch_data = *(int*)(entity + 0x7C);
    int direction = *(int*)(switch_data + 0x3C);

    float angle_limit = *(float*)(switch_data + 0x28);
    float target = FLOAT_806143f4; // default
    if (angle_limit != FLOAT_806143f0) {
        target = angle_limit;
    }

    if (direction == 2) {
        // Closing: decrement toward -target
        if (*(float*)(entity + 0xD0) > -target) {
            *(float*)(entity + 0xD0) -= FLOAT_806108ec;
            *(u32*)(entity + 0x18) |= 2; // mark dirty
        }
        else {
            // Close complete
            float final_val = FLOAT_806143f4;
            if (angle_limit != FLOAT_806143f0) final_val = angle_limit;
            *(float*)(entity + 0xD0) = -final_val;
            *(u32*)(*(int*)(switch_data + 0x8) + 0x28) &= ~0x4000000;
        }
    }
    else if (direction == 1) {
        // Opening: increment toward +target
        if (*(float*)(entity + 0xD0) < target) {
            *(float*)(entity + 0xD0) += FLOAT_806108ec;
            *(u32*)(entity + 0x18) |= 2; // mark dirty
        }
        else {
            // Open complete
            float final_val = FLOAT_806143f4;
            if (angle_limit != FLOAT_806143f0) final_val = angle_limit;
            *(float*)(entity + 0xD0) = final_val;
            *(int*)(switch_data + 0x3C) = 0; // clear animation
            *(u32*)(*(int*)(switch_data + 0x8) + 0x28) &= ~0x4000000;
        }
    }

    // Re-register for next frame update
    entity_register_for_update(entity, 2, -0x7FF7382C);
}
