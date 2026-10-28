// =============================================================================
// switch_tick_spawn.cpp - Switch Tick Update, Spawn, and Supporting Systems
// Part of the switch/button interaction subsystem
// Handles: cooldown fade, proximity detection, sliding movement, hold-use,
//          ambient sound, per-frame tick dispatch, spawn/init, slot management
// Batch 190 | Date: 2026-08-11
// =============================================================================

#include "common.h"
#include "entity.h"
#include "sound.h"
#include "switch.h"

// External references
extern float g_delta_time;                       // 0x806108ec
extern int   g_player_count;                     // 0x80612098
extern int   g_player_table[];                   // 0x80588610
extern int   g_level_type;                       // 0x80612614
extern int   g_switch_pool_count;                // 0x80611cc0

extern void switch_use_handler(void);
extern void switch_toggle_state(int slot);
extern void switch_invoke_action(int slot);
extern void switch_anim_tick_vertical(int entity);
extern void switch_anim_tick_fall(int entity);
extern void switch_anim_tick_bidirectional(int entity);
extern void switch_update_visual_state(int slot);
extern void entity_set_render_type(int entity, int type);
extern void entity_register_for_update(int entity, int list, int callback);
extern void entity_interaction_execute(void);
extern int  entity_spawn_by_type(/* variadic */);
extern int  soundpad_find_zone_by_id(int zone_id);
extern void sound_update_3d_source(double vol, double p2, double p3,
                                   int handle_ptr, int pos_ptr, int sound_id, int flags);

// =============================================================================
// Address: 0x8008d19c | Name: switch_cooldown_fade_tick
// Decrements entity health/opacity each frame by a configured rate.
// When health reaches zero: auto-toggles if state==1, clears render bits,
// sets render type to invisible. Used for timed-use switches that fade out.
// =============================================================================
void switch_cooldown_fade_tick(int* event_data) {
    int entity = *event_data;

    *(float*)(entity + 0x1cc) -= (float)event_data[8]; // subtract fade rate

    if (*(float*)(entity + 0x1cc) < 0.0f) {
        *(float*)(entity + 0x1cc) = 0.0f;

        // If currently active (state==1), auto-toggle back
        if (*(int*)(*(int*)(entity + 0x7c) + 0x18) == 1) {
            switch_use_handler();
        }

        // Clear render bits and set invisible
        *(uint*)(*(int*)(entity + 4) + 0x30) &= ~0x6; // clear bits 1,2
        *(uint*)(*(int*)(entity + 4) + 0x30) |= 0x8;  // set bit 3 (hidden)
        entity_set_render_type(entity, 0);
    }
}

// =============================================================================
// Address: 0x8008d234 | Name: switch_find_player_in_proximity
// Iterates the player pool looking for an alive player in the same room
// and within AABB proximity of the switch entity.
// Returns player entity pointer, or 0 if none found.
// =============================================================================
int switch_find_player_in_proximity(int switch_entity) {
    int count = g_player_count;

    for (int i = 0; i < count; i++) {
        int* player_ptr = *(int**)((int)&g_player_table + i * 4);
        if (player_ptr == NULL) continue;

        int player = *player_ptr;

        // Must be alive (health > 0)
        if (!(0.0f < *(float*)(player + 0x1cc))) continue;

        // Must be in same room
        if (*(int*)(switch_entity + 0x34) != *(int*)(player + 0x34)) continue;

        // AABB proximity check on X axis
        float dx = *(float*)(player + 0x64) -
                   (*(float*)(switch_entity + 0x64) + *(float*)(switch_entity + 0x1a8));
        if (!(ABS(dx) < SWITCH_PROXIMITY_RADIUS + *(float*)(switch_entity + 0x1c0)))
            continue;

        // AABB proximity check on Z axis
        float dz = *(float*)(player + 0x6c) -
                   (*(float*)(switch_entity + 0x6c) + *(float*)(switch_entity + 0x1b0));
        if (!(ABS(dz) < SWITCH_PROXIMITY_RADIUS + *(float*)(switch_entity + 0x1c8)))
            continue;

        return player; // found
    }
    return 0;
}

// =============================================================================
// Address: 0x8008d2ec | Name: switch_slide_movement_tick
// 4-state sliding door/platform system:
//   State 0: Idle - checks for player proximity to start extending
//   State 1: Extending - interpolates position along direction vector
//   State 2: Waiting - countdown timer, checks if player left proximity
//   State 3: Retracting - interpolates position back to origin
// Position = base_pos + direction * (distance * (1 - timer/duration))
// =============================================================================
void switch_slide_movement_tick(int entity) {
    int slot = *(int*)(entity + 0x7c);
    int state = *(int*)(slot + 0x3c);
    float* direction = *(float**)(slot + 0x30);

    // Delta time normalized
    double dt = (double)(g_delta_time *
                (SWITCH_RATE_SCALE /
                (float)((double)g_frame_count_bias - DOUBLE_BIAS)));

    if (state == 2) {
        // State 2: Waiting - check if player left
        int player = switch_find_player_in_proximity(entity);
        if (player == 0) {
            // Player gone - decrement wait timer
            *(float*)(slot + 0x38) = (float)((double)*(float*)(slot + 0x38) - dt);
        }
        if (*(float*)(slot + 0x38) <= 0.0f) {
            // Timer expired - start retracting
            *(float*)(slot + 0x44) = SWITCH_SLIDE_RETRACT_FACTOR * *(float*)(slot + 0x28);
            *(float*)(slot + 0x38) = *(float*)(slot + 0x44);
            *(int*)(slot + 0x3c) = 3; // retract state
        }
    } else if (state == 0) {
        // State 0: Idle - detect player proximity
        int player = switch_find_player_in_proximity(entity);
        if (player != 0) {
            *(int*)(slot + 0x1c) = player;
            *(int*)(slot + 0x3c) = 1; // start extending
            *(float*)(slot + 0x44) = SWITCH_SLIDE_RETRACT_FACTOR * *(float*)(slot + 0x28);
            *(float*)(slot + 0x38) = *(float*)(slot + 0x44);
        }
    } else if (state == 1) {
        // State 1: Extending - move outward along direction
        float progress = *(float*)(slot + 0x40) *
                        (1.0f - *(float*)(slot + 0x38) / *(float*)(slot + 0x44));
        *(float*)(entity + 0x64) = direction[0] * progress + *(float*)(entity + 0xa8);
        *(float*)(entity + 0x68) = direction[1] * progress + *(float*)(entity + 0xac);
        *(float*)(entity + 0x6c) = direction[2] * progress + *(float*)(entity + 0xb0);

        *(float*)(slot + 0x38) = (float)((double)*(float*)(slot + 0x38) - dt);
        if (*(float*)(slot + 0x38) <= 0.0f) {
            // Fully extended - start waiting
            *(float*)(slot + 0x44) = *(float*)(slot + 0x28); // wait duration
            *(float*)(slot + 0x38) = *(float*)(slot + 0x44);
            *(int*)(slot + 0x3c) = 2; // wait state
            switch_use_handler(); // fire activation
        }
    } else if (state == 3) {
        // State 3: Retracting - move back toward origin
        float progress = *(float*)(slot + 0x40) *
                        (*(float*)(slot + 0x38) / *(float*)(slot + 0x44));
        *(float*)(entity + 0x64) = direction[0] * progress + *(float*)(entity + 0xa8);
        *(float*)(entity + 0x68) = direction[1] * progress + *(float*)(entity + 0xac);
        *(float*)(entity + 0x6c) = direction[2] * progress + *(float*)(entity + 0xb0);

        *(float*)(slot + 0x38) = (float)((double)*(float*)(slot + 0x38) - dt);
        if (*(float*)(slot + 0x38) <= 0.0f) {
            // Fully retracted - snap to origin, go idle
            *(float*)(slot + 0x38) = 0.0f;
            *(int*)(slot + 0x3c) = 0;
            *(float*)(entity + 0x64) = *(float*)(entity + 0xa8);
            *(float*)(entity + 0x68) = *(float*)(entity + 0xac);
            *(float*)(entity + 0x6c) = *(float*)(entity + 0xb0);
            switch_use_handler(); // fire deactivation
        }
    }
}

// =============================================================================
// Address: 0x8008d53c | Name: switch_get_use_duration
// Returns the required hold-time for a switch based on its current state,
// flags, and interaction mode.
// - State off + flag 0x10000: return field +0x28 (off->on duration)
// - State on + flag 0x20000: return field +0x2c (on->off duration)
// - Mode 4: return field +0x28
// - Default: return global default duration
// =============================================================================
float switch_get_use_duration(int slot) {
    if (*(int*)(slot + 0x18) == 0 && (*(uint*)(slot + 0x10) & 0x10000) != 0) {
        return *(float*)(slot + 0x28);
    }
    if (*(int*)(slot + 0x18) != 0 && (*(uint*)(slot + 0x10) & 0x20000) != 0) {
        return *(float*)(slot + 0x2c);
    }
    if (*(int*)(slot + 0xc) == 4) {
        return *(float*)(slot + 0x28);
    }
    return SWITCH_DEFAULT_USE_DURATION; // 0x80614430
}

// =============================================================================
// Address: 0x8008d590 | Name: switch_hold_use_handler
// Accumulates hold-time per frame while player holds use button on a switch.
// When threshold reached: fires switch_use_handler or entity_interaction_execute.
// Uses progress bar fill (field +0xe8) for visual feedback.
// =============================================================================
void switch_hold_use_handler(void) {
    uint64_t event_pair = event_get_entity_pair();
    int switch_entity = (int)(event_pair >> 32);
    int player_entity = (int)event_pair;

    int player_slot = *(int*)(player_entity + 0x7c);
    int switch_slot = *(int*)(switch_entity + 0x7c);

    // Accumulate normalized delta time
    *(float*)(player_slot + 0x9e0) +=
        g_delta_time * (SWITCH_RATE_SCALE /
        (float)((double)g_frame_count_bias - DOUBLE_BIAS));

    // Store weapon def reference
    int weapon_ref = DAT_80611928;
    *(int*)(switch_slot + 0x24) = weapon_ref;
    *(int*)(player_slot + 0x9dc) = weapon_ref;

    // Check if switch allows activation in current state
    if ((*(int*)(switch_slot + 0x18) == 0 || (*(uint*)(switch_slot + 0x10) & 0x20000) != 0) ||
        (*(int*)(switch_slot + 0x18) == 0 && (*(uint*)(switch_slot + 0x10) & 0x10000) != 0)) {

        // Link player to switch for interaction tracking
        if (*(int*)(player_entity + 0x150) != 0) {
            *(int*)(*(int*)(player_entity + 0x150) + 0xe8c) = switch_entity;
        }

        // Get required duration and check if met
        float duration = switch_get_use_duration(switch_slot);
        if ((double)*(float*)(player_slot + 0x9e0) < duration) {
            // Still holding - show progress
            *(float*)(switch_entity + 0xe8) = 1.0f; // progress indicator
        } else {
            // Duration met - fire activation
            if (*(int*)(player_entity + 0x150) == 0) {
                switch_use_handler();
            } else {
                entity_interaction_execute();
            }
        }
    }

    event_end_pair();
}

// =============================================================================
// Address: 0x8008d6a0 | Name: switch_ambient_sound_tick
// Per-frame ambient sound emission for switches based on level type.
// Decrements timer at +0x20, then emits different sounds per level:
//   Level 6 + entity type 0x507: calls special callback FUN_801ffd14
//   Level 0x1b: sound 0x685 (mechanical hum)
//   Level 0x1d: sound 0x651 (electrical buzz)
//   Level 0x30: sound 0x1573 (industrial) with volume scaling
// Also calls FUN_8012edfc to interpolate fade on +0xe8 field.
// =============================================================================
void switch_ambient_sound_tick(int entity) {
    float timer = *(float*)(*(int*)(entity + 0x7c) + 0x20);

    // Decrement timer
    if (0.0f < timer) {
        *(float*)(*(int*)(entity + 0x7c) + 0x20) =
            -(g_delta_time *
              (SWITCH_RATE_SCALE /
              (float)((double)g_frame_count_bias - DOUBLE_BIAS)) - timer);
    }

    // Level-type-based ambient sound dispatch
    if (g_level_type == 6) {
        if (*(short*)(entity + 0x94) == 0x507) {
            FUN_801ffd14(entity); // special entity callback
        }
    } else if (g_level_type > 6 && g_level_type < 0x1c && g_level_type > 0x1a) {
        // Level 0x1b: mechanical hum
        sound_update_3d_source(
            (double)*(float*)(entity + 0xe8), (double)SWITCH_SOUND_FALLOFF_A,
            (double)1.0f, entity + 0x228, entity + 0x64, 0x685, 0);
    } else if (g_level_type == 0x30) {
        // Level 0x30: industrial with volume scale
        sound_update_3d_source(
            (double)(SWITCH_INDUSTRIAL_VOL_SCALE * *(float*)(entity + 0xe8)),
            (double)SWITCH_SOUND_FALLOFF_B, (double)1.0f,
            entity + 0x228, entity + 0x64, 0x1573, 0);
    } else if (g_level_type > 0x1c && g_level_type < 0x1e) {
        // Level 0x1d: electrical buzz
        sound_update_3d_source(
            (double)*(float*)(entity + 0xe8), (double)SWITCH_SOUND_FALLOFF_C,
            (double)1.0f, entity + 0x228, entity + 0x64, 0x651, 0);
    }

    // Interpolate fade value
    interpolate_fade(0.0f, SWITCH_SLIDE_RETRACT_FACTOR, (float*)(entity + 0xe8));
}

// =============================================================================
// Address: 0x8008d7e4 | Name: switch_tick_update
// MASSIVE per-frame switch update dispatcher.
// First checks flag 0x10 (sliding type) and delegates to slide_movement_tick.
// Otherwise dispatches by interaction mode:
//   Mode 0/1: ambient sound tick if directional flags match state
//   Mode 2: timer increment, auto-invoke when expired
//   Mode 3: timer increment, auto-toggle when expired (state==1)
//   Mode 4: ambient sound tick
// Then handles animation type dispatch (flag 0x4):
//   Flag 0x1800: bidirectional bone rotation
//   Flag 0x2000: vertical open/close
//   Neither: fall (state 1) or open (state 2)
// =============================================================================
void switch_tick_update(int entity) {
    int slot = *(int*)(entity + 0x7c);
    uint flags = *(uint*)(slot + 0x10);
    float dt_normalized = g_delta_time /
        (float)((double)g_frame_count_bias - DOUBLE_BIAS);

    if ((flags & 0x10) != 0) {
        // Sliding type - use movement state machine
        switch_slide_movement_tick(entity);
        return;
    }

    int mode = *(int*)(slot + 0xc);

    if (mode == 3) {
        // Mode 3: auto-return timer
        *(float*)(slot + 0x20) += dt_normalized;
        if (SWITCH_AUTO_RETURN_TIME < *(float*)(slot + 0x20) &&
            *(int*)(slot + 0x18) == 1) {
            switch_toggle_state(slot);
        }
    } else if (mode < 3) {
        if (mode < 2) {
            // Mode 0/1: ambient sound if directional flags match
            if (mode >= 0 &&
                ((*(int*)(slot + 0x18) != 0 && (flags & 0x20000) != 0) ||
                 (*(int*)(slot + 0x18) == 0 && (flags & 0x10000) != 0))) {
                switch_ambient_sound_tick(entity);
            }
        } else {
            // Mode 2: timed auto-invoke
            if (*(int*)(slot + 0x18) == 1) {
                *(float*)(slot + 0x20) += dt_normalized;
                if (*(float*)(slot + 0x28) < *(float*)(slot + 0x20)) {
                    *(uint*)(entity + 0x28) &= ~0x04000000;
                    switch_invoke_action(slot);
                }
            }
        }
    } else if (mode < 5) {
        // Mode 4: ambient sound always
        switch_ambient_sound_tick(entity);
    }

    // Animation dispatch (flag 0x4 = has animation)
    flags = *(uint*)(slot + 0x10); // re-read after possible changes
    if ((flags & 4) != 0) {
        if ((flags & 0x1800) != 0) {
            // Bidirectional bone rotation
            switch_anim_tick_bidirectional(entity);
        } else if ((flags & 0x2000) != 0) {
            // Vertical open/close
            switch_anim_tick_vertical(entity);
        } else {
            // Fall or open based on state
            if (*(int*)(slot + 0x3c) == 1) {
                switch_anim_tick_fall(entity);
            } else if (*(int*)(slot + 0x3c) == 2) {
                switch_anim_tick_open(entity);
            }
        }
    }
}

// =============================================================================
// Address: 0x8008d988 | Name: switch_on_spawn
// MASSIVE switch entity spawn handler. Called from event system when a switch
// definition is instantiated into the world.
// Creates entity, initializes 0x54-byte slot from definition, sets up:
// - Animation state (bidirectional/vertical initial positions)
// - Event handlers (use dispatch, cooldown fade)
// - Sound zone assignment
// - Visual state and flags
// - Interaction mode-specific initial conditions
// Pool limited to 20 (0x14) simultaneous switches.
// =============================================================================
void switch_on_spawn(void) {
    int* def = (int*)event_get_entity_pair(); // actually gets spawn def pointer

    if (g_switch_pool_count >= 0x14) {
        event_end_pair();
        return;
    }

    int slot_index = g_switch_pool_count;
    int slot_offset = slot_index * 0x54;
    int* slot_ptr = (int*)(&SWITCH_SLOT_POOL + slot_index * 0x15);

    // Spawn the entity
    int entity = entity_spawn_by_type(
        def[2], def + 3, (float*)(def + 6), 0, (int)def);

    // Basic entity setup
    *(int*)(entity + 0x60) = def[0]; // entity type ID
    *(int*)(&SWITCH_SLOT_FIELD_50 + slot_offset) = def[0x12]; // extra data
    *(uint*)(entity + 0x28) |= 5; // flags: interactable + visible

    // Register for think update (list 0)
    entity_register_for_update(entity, 0, (int)switch_tick_update);
    *(int*)(entity + 0x7c) = (int)slot_ptr;

    // Link entity to slot
    *(int*)(&SWITCH_SLOT_ENTITY + slot_offset) = entity;
    *slot_ptr = def[0]; // store type ID in slot

    // Sound zone assignment
    if (def[0xd] == 0) {
        *(int*)(&SWITCH_SLOT_ZONE + slot_offset) = -1;
    } else {
        *(int*)(&SWITCH_SLOT_ZONE + slot_offset) = soundpad_find_zone_by_id(def[0xd]);
    }

    // Copy definition fields to slot
    *(int*)(&SWITCH_SLOT_MODE + slot_offset) = def[9];     // interaction mode
    *(int*)(&SWITCH_SLOT_FLAGS + slot_offset) = def[10];   // flags
    *(int*)(&SWITCH_SLOT_EXTRA1 + slot_offset) = def[11];  // extra field
    *(int*)(&SWITCH_SLOT_DIR_BIT + slot_offset) = ((uint)def[10] >> 15) & 1;
    *(int*)(&SWITCH_SLOT_ANIM_STATE + slot_offset) = 0;
    *(float*)(&SWITCH_SLOT_TIMER + slot_offset) = 0.0f;
    *(int*)(&SWITCH_SLOT_DURATION_A + slot_offset) = def[0xe];
    *(int*)(&SWITCH_SLOT_DURATION_B + slot_offset) = def[0xf];
    *(int*)(&SWITCH_SLOT_SLIDE_DIR + slot_offset) = def[0x10];
    *(int*)(&SWITCH_SLOT_SLIDE_DIST + slot_offset) = def[0x11];
    *(int*)(entity + 0x25c) = def[0xc];
    *(int*)(&SWITCH_SLOT_CALLBACK_ID + slot_offset) = def[0xc];

    g_switch_pool_count++;
    *(int*)(&SWITCH_SLOT_SOUND_HANDLE + slot_offset) = -1;

    // Register for render update (list 3)
    entity_register_for_update(entity, 3, (int)switch_use_event_dispatch);
    switch_update_visual_state((int)slot_ptr);
    *(uint*)(entity + 0x28) |= 0x400;

    // Animation setup (flag 0x4)
    if ((*(uint*)(&SWITCH_SLOT_FLAGS + slot_offset) & 4) != 0) {
        *(int*)(&SWITCH_SLOT_ANIM_STATE + slot_offset + 0x3c) = 0;
        *(unsigned char*)(entity + 0x32) = 2;

        if ((*(uint*)(&SWITCH_SLOT_FLAGS + slot_offset) & 0x1800) == 0) {
            if ((*(uint*)(&SWITCH_SLOT_FLAGS + slot_offset) & 0x2000) != 0) {
                // Vertical: start at max height
                *(float*)(entity + 0xd0) = SWITCH_ANIM_MAX;
            }
        } else {
            // Bidirectional: use custom or default angle
            float angle = (*(float*)(&SWITCH_SLOT_SLIDE_DIR + slot_offset) != 0.0f)
                ? *(float*)(&SWITCH_SLOT_SLIDE_DIR + slot_offset)
                : SWITCH_FALL_DEFAULT_LIMIT;
            *(float*)(entity + 0xd0) = angle;
        }
    }

    // Event handler setup based on flags
    if ((*(uint*)(&SWITCH_SLOT_FLAGS + slot_offset) & 8) != 0) {
        // Flag 0x8: use-button triggered
        *(uint*)(entity + 0x28) |= 0x10;
        entity_set_render_type(entity, (int)switch_use_event_dispatch);
    } else if ((*(uint*)(&SWITCH_SLOT_FLAGS + slot_offset) & 0x20) != 0) {
        // Flag 0x20: cooldown/fade switch
        *(uint*)(entity + 0x28) |= 0x10;
        *(float*)(entity + 0x1cc) = 1.0f; // full health
        entity_set_render_type(entity, (int)switch_cooldown_fade_tick);
    }

    // Sliding switch setup (flag 0x10)
    if ((*(uint*)(&SWITCH_SLOT_FLAGS + slot_offset) & 0x10) != 0) {
        *(int*)(&SWITCH_SLOT_ANIM_STATE + slot_offset + 0x3c) = 0;
        float scale = SWITCH_SLIDE_SCALE * *(float*)(entity + 0x1c4);
        *(float*)(&SWITCH_SLOT_SLIDE_SPEED + slot_offset) = 0.0f;
        *(float*)(&SWITCH_SLOT_SLIDE_RANGE + slot_offset) = 0.0f;
        *(float*)(&SWITCH_SLOT_SLIDE_MAX + slot_offset) = scale;
    }

    // Auto-activate if mode 1 with toggle flag
    if (*(int*)(&SWITCH_SLOT_MODE + slot_offset) == 1) {
        *(uint*)(&SWITCH_SLOT_FLAGS + slot_offset) |= 0x40000;
    }

    // Initial state: start already active if conditions met
    if (*(int*)(&SWITCH_SLOT_MODE + slot_offset) == 4 ||
        (*(int*)(&SWITCH_SLOT_DIR_BIT + slot_offset) != 0 &&
         (*(uint*)(&SWITCH_SLOT_FLAGS + slot_offset) & 0x20000) != 0) ||
        (*(int*)(&SWITCH_SLOT_DIR_BIT + slot_offset) == 0 &&
         (*(uint*)(&SWITCH_SLOT_FLAGS + slot_offset) & 0x10000) != 0)) {
        *(int*)(&SWITCH_SLOT_ANIM_STATE + slot_offset + 0x3c) = 0;
        *(float*)(&SWITCH_SLOT_TIMER + slot_offset) = 0.0f;
        *(float*)(&SWITCH_SLOT_SLIDE_RANGE + slot_offset) = 0.0f;
    }

    event_end_pair();
}

// =============================================================================
// Address: 0x8008dc3c | Name: switch_slot_reset
// Clears a 0x5c-byte switch slot to zero, then sets sentinel values (-1)
// for fields that use -1 as "unset": zone, sound handle, callback ID, etc.
// =============================================================================
void switch_slot_reset(int slot) {
    memset_wrapper(slot, 0, 0x5c);
    *(int*)(slot + 0x14) = -1;  // zone ID
    *(int*)(slot + 0x18) = -1;  // sound handle A
    *(int*)(slot + 0x24) = -1;  // callback ID
    *(int*)(slot + 0x44) = -1;  // linked entity
    *(int*)(slot + 0x48) = 0;   // extra clear
    *(int*)(slot + 0x1c) = -1;  // sound handle B
}

// =============================================================================
// Address: 0x8008dc90 | Name: switch_slot_load_from_def
// Copies fields from a switch definition table entry into a runtime slot.
// Definition is a 0x44+ word array, slot is the 0x5c-byte runtime struct.
// Maps: def[0]->type, def[0x43]->sizes, def[2]->target, def[0x42]->mode,
//       def[0x41]->flags, def[3]->extra, def[0x3d]->callback
// =============================================================================
void switch_slot_load_from_def(int slot, int* def) {
    int size_field = def[0x43];
    *(int*)(slot + 0x04) = size_field;  // size A
    *(int*)(slot + 0x08) = size_field;  // size B (copy)

    int type_id = def[0];
    *(int*)(slot + 0x14) = type_id;     // type ID A
    *(int*)(slot + 0x18) = type_id;     // type ID B (copy)

    *(int*)(slot + 0x24) = def[2];      // target entity ref
    *(int*)(slot + 0x0c) = def[0x42];   // interaction mode
    *(int*)(slot + 0x10) = def[0x41];   // flags
    *(int*)(slot + 0x3c) = def[3];      // initial state
    *(int*)(slot + 0x54) = def[0x3d];   // callback/event ID
}
