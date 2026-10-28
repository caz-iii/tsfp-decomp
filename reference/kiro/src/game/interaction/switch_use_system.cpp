// =============================================================================
// switch_use_system.cpp - Switch/Button Use & Animation System
// Part of the switch/button interaction subsystem (continuation of switch_event_system)
// Handles: vertical/fall animations, weapon rack ownership checks, sound dispatch,
//          action invocation, delayed timers, main use handler (5 modes), event filtering
// Batch 189 | Date: 2026-08-11
// =============================================================================

#include "common.h"
#include "entity.h"
#include "sound.h"
#include "switch.h"

// External references
extern float g_delta_time;                       // 0x806108ec
extern float SWITCH_ANIM_MIN;                    // 0x80614400 (0.0f)
extern float SWITCH_CLOSE_SPEED;                 // 0x80614404
extern float SWITCH_ANIM_MAX;                    // 0x80614408
extern float SWITCH_RATE_SCALE;                  // 0x8061440c
extern float SWITCH_OPEN_SPEED;                  // 0x80614410
extern float SWITCH_FALL_DEFAULT_LIMIT;          // 0x806143f4
extern float SWITCH_SOUND_VOLUME;                // 0x80614420
extern float SWITCH_REUSE_DELAY;                 // 0x80614424

extern void set_entity_position(float x, float y, float z);         // 0x8012f534
extern void switch_toggle_state(int slot);
extern int  weapon_get_def_by_index(uint id);                        // 0x8023cc28
extern void switch_weapon_swap_handler(void);
extern void sound_init_play_params(int params);
extern void sound_play_request(int* params);
extern void sound_clear_bank_ref(int sound_id);
extern float* entity_get_world_position(int entity);
extern uint64_t event_get_entity_pair(void);                         // 0x803826f0
extern void event_end_pair(void);                                    // 0x8038273c
extern void entity_set_render_type(int entity, int type);
extern void switch_cooldown_fade_tick(int* event_data);              // 0x8008d19c

// =============================================================================
// Address: 0x8008ca28 | Name: switch_anim_tick_vertical
// Vertical open/close animation tick for switches/levers.
// State 2 = closing (moves toward 0), State 1 = opening (moves toward max).
// Uses speed ramp based on delta time and global constants.
// =============================================================================
void switch_anim_tick_vertical(int switch_entity) {
    int slot = *(int*)(switch_entity + 0x7c);

    if (*(int*)(slot + 0x3c) == 2) {
        // Closing: decrement Y offset toward 0
        if (SWITCH_ANIM_MIN < *(float*)(switch_entity + 0xd0)) {
            *(float*)(switch_entity + 0xd0) =
                -(SWITCH_CLOSE_SPEED *
                  SWITCH_ANIM_MAX *
                  (SWITCH_RATE_SCALE / (float)((double)g_frame_count_bias - DOUBLE_BIAS)) *
                  g_delta_time - *(float*)(switch_entity + 0xd0));
            *(uint*)(switch_entity + 0x18) |= 2; // mark dirty
        } else {
            // Reached closed position
            *(float*)(switch_entity + 0xd0) = SWITCH_ANIM_MIN;
            *(int*)(slot + 0x3c) = 1; // transition to opening state
            *(int*)(slot + 0x18) = 1 - *(int*)(slot + 0x18); // flip direction flag
        }
    } else if (*(int*)(slot + 0x3c) == 1) {
        // Opening: increment Y offset toward max
        if (*(float*)(switch_entity + 0xd0) < SWITCH_ANIM_MAX) {
            *(float*)(switch_entity + 0xd0) =
                SWITCH_OPEN_SPEED *
                SWITCH_ANIM_MAX *
                (SWITCH_RATE_SCALE / (float)((double)g_frame_count_bias - DOUBLE_BIAS)) *
                g_delta_time + *(float*)(switch_entity + 0xd0);
            *(uint*)(switch_entity + 0x18) |= 2; // mark dirty
        } else {
            // Reached open position
            *(float*)(switch_entity + 0xd0) = SWITCH_ANIM_MAX;
            *(int*)(slot + 0x3c) = 0; // idle
            *(uint*)(*(int*)(slot + 8) + 0x28) &= ~0x04000000; // clear active bit
        }
    }

    // Update position with current vertical offset
    set_entity_position(0.0f, *(float*)(switch_entity + 0xd0), 0.0f);
}

// =============================================================================
// Address: 0x8008cb7c | Name: switch_anim_tick_fall
// Gravity-based fall animation for lever switches.
// Decrements X rotation by delta_time until reaching the configured limit,
// then snaps to limit and transitions to idle state.
// =============================================================================
void switch_anim_tick_fall(int switch_entity) {
    int slot = *(int*)(switch_entity + 0x7c);
    float custom_limit = *(float*)(slot + 0x28);

    // Use custom limit if set, else default
    float limit = (custom_limit != 0.0f) ? custom_limit : SWITCH_FALL_DEFAULT_LIMIT;

    if (-limit < *(float*)(switch_entity + 0xc0)) {
        // Still falling
        *(float*)(switch_entity + 0xc0) -= g_delta_time;
        *(uint*)(switch_entity + 0x18) |= 2; // mark dirty
        return;
    }

    // Reached fall limit - snap and go idle
    float snap_limit = (custom_limit != 0.0f) ? custom_limit : SWITCH_FALL_DEFAULT_LIMIT;
    *(float*)(switch_entity + 0xc0) = -snap_limit;
    *(int*)(slot + 0x3c) = 0; // idle state
    *(uint*)(*(int*)(slot + 8) + 0x28) &= ~0x04000000; // clear active bit
}

// =============================================================================
// Address: 0x8008cc04 | Name: switch_check_weapon_ownership
// Checks if the interacting player already owns the weapon on a weapon rack.
// Returns 1 if player already has this weapon equipped, 0 otherwise.
// Used to prevent picking up duplicate weapons from racks.
// =============================================================================
int switch_check_weapon_ownership(int switch_entity, int interacting_player) {
    int slot = *(int*)(switch_entity + 0x7c);

    // Must be an active weapon swap handler with a valid interactor
    if (*(int*)(slot + 0x18) == 0) return 0;
    if (*(code**)(slot + 0x14) != switch_weapon_swap_handler) return 0;
    if (interacting_player == 0) return 0;

    int last_user = *(int*)(slot + 0x1c);
    if (last_user == 0 || interacting_player == last_user) return 0;

    // Check if last_user is alive (health > 0) and has weapon slot filled
    if (!(0.0f < *(float*)(last_user + 0x1cc))) return 0;
    int last_user_slot = *(int*)(last_user + 0x7c);
    if (*(int*)(last_user_slot + 0xc14) == 0) return 0;

    // Resolve weapon definition from switch slot
    uint weapon_def_id = *(uint*)(slot + 0x24);
    if ((int)weapon_def_id < 1 && *(int*)(slot + 0x30) != 0) {
        if (*(int*)(slot + 0x34) < 0) return 0;
        weapon_def_id = *(uint*)(*(int*)(slot + 0x30) + *(int*)(slot + 0x34) * 4);
        if ((int)weapon_def_id < 1) return 0;
    }

    int* weapon_data = (int*)weapon_get_def_by_index(weapon_def_id);
    if (*weapon_data == *(int*)(last_user_slot + 0xc14)) {
        return 1; // same weapon - already owned
    }
    return 0;
}

// =============================================================================
// Address: 0x8008cd00 | Name: switch_get_sound_id_for_type
// Maps entity type IDs to activation sound bank IDs.
// Returns -1 (0xFFFFFFFF) if no sound is defined for the type.
// Large switch table covering doors, buttons, elevators, weapons, etc.
// =============================================================================
int switch_get_sound_id_for_type(int entity_type_id) {
    if (entity_type_id == 0x3a7) return 0x62c;

    if (entity_type_id < 0x3a7) {
        if (entity_type_id == 0x9a)  return 0x623;
        if (entity_type_id > 0x8e && entity_type_id < 0x91) return 0x652;
        if (entity_type_id > 0xbe && entity_type_id <= 0xc2) return 0x686;
        if (entity_type_id == 0x2c6) return 0x36e;
        if (entity_type_id > 0x366 && entity_type_id < 0x36b) return 0x3b5;
    } else {
        if (entity_type_id == 0x7a3) return 0xdc3;
        if (entity_type_id == 0x4d5) return 0x1574;
        if (entity_type_id == 0x465) return 0x607;
        if (entity_type_id == 0x7a4) return 0xdc4;
        if (entity_type_id == 0x7a5) return 0xdc5;
        if (entity_type_id == 0x7a6) return 0xdc6;
        if (entity_type_id == 0x7a7) return 0xdc7;
    }

    return -1; // no sound for this type
}

// =============================================================================
// Address: 0x8008ce28 | Name: switch_play_activation_sound
// Plays a 3D positional activation sound for the switch entity.
// Looks up sound ID from entity type, gets world position, submits play request.
// =============================================================================
void switch_play_activation_sound(int slot) {
    int entity = *(int*)(slot + 8);
    int sound_id = switch_get_sound_id_for_type((uint)*(unsigned short*)(entity + 0x94));
    if (sound_id == -1) return;

    float* pos = entity_get_world_position(entity);

    int play_params[10];
    sound_init_play_params((int)play_params);
    play_params[0] = sound_id;

    // Set 3D positional flag
    *(unsigned char*)((char*)play_params + 0x28) = 1;
    // Volume
    *(float*)((char*)play_params + 0x18) = SWITCH_SOUND_VOLUME;
    // Position XYZ
    *(float*)((char*)play_params + 0x1c) = pos[0];
    *(float*)((char*)play_params + 0x20) = pos[1];
    *(float*)((char*)play_params + 0x24) = pos[2];

    sound_play_request(play_params);
}

// =============================================================================
// Address: 0x8008ceb8 | Name: switch_clear_activation_sound
// Clears any looping/banked sound reference for a switch entity type.
// =============================================================================
void switch_clear_activation_sound(int entity_type_id) {
    int sound_id = switch_get_sound_id_for_type(entity_type_id);
    sound_clear_bank_ref(sound_id);
}

// =============================================================================
// Address: 0x8008cedc | Name: switch_invoke_action
// Core dispatch: if a custom callback is set, invoke it; else toggle state.
// After action, if state changed, plays the activation sound.
// =============================================================================
void switch_invoke_action(int slot) {
    int prev_state = *(int*)(slot + 0x18);

    if (*(void**)(slot + 0x14) == NULL) {
        switch_toggle_state(slot);
    } else {
        // Invoke custom handler (e.g. weapon swap, sequence step)
        (*(void(**)(void))(slot + 0x14))();
    }

    // Play sound if state actually changed
    if (prev_state != *(int*)(slot + 0x18)) {
        switch_play_activation_sound(slot);
    }
}

// =============================================================================
// Address: 0x8008cf40 | Name: switch_delayed_action_tick
// Timer-based delayed switch activation.
// When timer at +0x20 expires (<= 0), clears pending flag and invokes action.
// =============================================================================
void switch_delayed_action_tick(int slot) {
    float delay = SWITCH_REUSE_DELAY;

    if (*(float*)(slot + 0x20) <= 0.0f) {
        *(int*)(slot + 0x24) = 0;       // clear pending flag
        *(float*)(slot + 0x20) = delay;  // reset timer
        switch_invoke_action(slot);
    }
}

// =============================================================================
// Address: 0x8008cf84 | Name: switch_use_handler
// Main switch/button interaction dispatcher. Called when a player uses a switch.
// Handles 5 interaction modes:
//   Mode 0: Instant toggle
//   Mode 1: Conditional (flag-based: one-shot on/off/toggle with reuse logic)
//   Mode 2: Timed (countdown before toggle, sets active bit on entity)
//   Mode 3: One-shot off-only (only fires when currently on)
//   Mode 4: One-shot on-only (only fires when currently off)
// First checks weapon ownership to prevent duplicate pickups on weapon racks.
// =============================================================================
void switch_use_handler(void) {
    // Get event entity pair (switch entity + interacting player)
    uint64_t event_pair = event_get_entity_pair();
    int switch_entity = (int)(event_pair >> 32);
    int interacting_player = (int)event_pair;

    int slot = *(int*)(switch_entity + 0x7c);

    // Check if player already owns this weapon (weapon rack guard)
    int already_owned = switch_check_weapon_ownership(switch_entity, interacting_player);
    if (already_owned != 0) {
        event_end_pair();
        return;
    }

    // Store interacting player reference
    *(int*)(slot + 0x1c) = interacting_player;

    int mode = *(int*)(slot + 0xc);

    if (mode == 2) {
        // Timed activation
        *(float*)(slot + 0x20) = 0.0f;
        switch_invoke_action(slot);
        *(uint*)(switch_entity + 0x28) |= 0x4000000; // mark entity active
    } else if (mode < 2) {
        if (mode == 0) {
            // Instant toggle
            switch_invoke_action(slot);
        } else {
            // Mode 1: Conditional flag-based
            uint flags = *(uint*)(slot + 0x10);
            if ((flags & 0x40000) == 0) {
                // Single-use mode with player check
                if ((flags & 0x100000) != 0 &&
                    interacting_player == *(int*)(slot + 0x1c)) {
                    switch_invoke_action(slot);
                    *(uint*)(slot + 0x10) &= ~0x100000;
                }
            } else {
                // Flag-gated toggle with direction checks
                if ((*(int*)(slot + 0x18) == 0 || (flags & 0x20000) == 0) &&
                    (*(int*)(slot + 0x18) != 0 || (flags & 0x10000) == 0)) {
                    switch_invoke_action(slot);
                } else {
                    switch_delayed_action_tick(slot);
                }
                *(uint*)(slot + 0x10) &= ~0x70000; // clear processed flags
            }
        }
    } else if (mode == 4) {
        // One-shot on-only
        *(int*)(slot + 0x24) = 0;
        if (*(int*)(slot + 0x18) == 0) {
            switch_invoke_action(slot);
        }
    } else if (mode == 3) {
        // One-shot off-only
        if (*(int*)(slot + 0x18) == 0) {
            *(float*)(slot + 0x20) = 0.0f;
            switch_invoke_action(slot);
        }
    }

    event_end_pair();
}

// =============================================================================
// Address: 0x8008d100 | Name: switch_use_event_dispatch
// Filters use events before dispatching to the switch handler.
// Checks flag 0x200 (coop switch requiring both players alive).
// Skips dispatch for event type 0x17 (system reset).
// Also handles cooldown/reuse timer if flag 0x20 is set.
// =============================================================================
void switch_use_event_dispatch(int* event_data) {
    int entity = *event_data;
    int slot = *(int*)(entity + 0x7c);

    // Flag 0x200: requires both players to be alive (coop switch)
    if ((*(uint*)(slot + 0x10) & 0x200) != 0) {
        if ((event_data[2] == 0 || *(int*)(event_data[2] + 0x150) == 0)) {
            if (event_data[1] == 0 || *(int*)(event_data[1] + 0x150) == 0) {
                goto handle_cooldown; // neither player valid - skip use
            }
        }
    }

    // Skip for system reset events (type 0x17)
    if (event_data[6] != 0x17) {
        switch_use_handler();
    }

handle_cooldown:
    // Handle cooldown/reuse timer if flag 0x20 set
    if ((*(uint*)(slot + 0x10) & 0x20) != 0) {
        switch_cooldown_fade_tick(event_data);
    }
}
