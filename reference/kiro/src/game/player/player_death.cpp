// Address: 0x800a561c | Name: player_death_disable_entity | Date: 2026-08-11
// Address: 0x800a565c | Name: player_on_death_dispatch | Date: 2026-08-11
// Address: 0x800a583c | Name: player_death_reset_state | Date: 2026-08-11
// Address: 0x800a5990 | Name: player_death_set_camera_fly | Date: 2026-08-11
// Address: 0x800a5b14 | Name: player_death_standard | Date: 2026-08-11
// Address: 0x800a5bb0 | Name: player_death_cutscene | Date: 2026-08-11
//
// Player death pipeline: orchestration, state reset, camera modes, and entity disable.
// Called primarily from character_on_death_handler and internally between death modes.

#include "common.h"

// ============================================================================
// 0x800a561c - player_death_disable_entity
// Disables the player entity's active bit (bit 0 of flags at +0x28).
// Called from death animation system (FUN_800fd92c) after ragdoll completes.
// 1 xref.
// ============================================================================
void player_death_disable_entity(int entity) {
    FUN_8037ee2c(); // flush pending state
    *(u32*)(entity + 0x28) &= ~1u; // clear active bit
}

// ============================================================================
// 0x800a565c - player_on_death_dispatch
// Main death orchestrator called from character_on_death_handler.
// Resolves the killer entity, checks entity type (must be type 8 = character).
// If player state is 4 (already in cutscene death), updates portal visibility.
// Otherwise:
//   - Calls FUN_80234a04 to apply death animation / ragdoll on killer's data
//   - Plays random death sound (0x4fd or 0x4fe based on RNG threshold)
//   - Routes to either player_death_set_camera_fly (if flag 0x10000 set = teleport)
//     or player_death_standard (normal death)
//   - Calls player_death_reset_state to finalize
//   - In story mode with 2+ players: additional player_death_reset_state calls
//   - Story mode: calls FUN_802f3774 (story checkpoint logic)
// 2 xrefs.
// ============================================================================
void player_on_death_dispatch(void) {
    int* death_context;
    int* killer_data;
    int entity_ref;
    int* char_data;
    int player_state;
    int sound_params[6];
    float sound_volume;

    death_context = (int*)FUN_803826f0(); // OS lock + get context
    killer_data = NULL;
    entity_ref = death_context[1]; // killer entity
    player_state = *(int*)(*(int*)(*death_context + 0x150)); // player slot
    char_data = *(int**)(*(int*)(player_state + 0x14) + 0x7c);

    // Validate killer entity is a character (type 8)
    if ((entity_ref != 0) && (*(int*)(entity_ref + 0x20) == 8)) {
        killer_data = *(int**)(entity_ref + 0x7c);
    }

    if (*(int*)(player_state + 0x114) == 4) {
        // Already in cutscene death mode - just update portal visibility
        char_data[9] |= 8;
        FUN_80234a04(killer_data, *(int**)(*(int*)(player_state + 0x14) + 0x7c));
    } else {
        // Normal death path
        if ((char_data[9] & 8) == 0) {
            FUN_80234a04(killer_data, char_data); // apply death anim
        }

        // Play random death sound based on RNG
        entity_ref = death_context[1];
        if (entity_ref != 0) {
            if ((*(int*)(entity_ref + 0x20) == 8) &&
                ((*(short*)(entity_ref + 0x94) == 0x5e8) ||
                 (*(short*)(entity_ref + 0x94) == 0x637))) {
                u32 rng = FUN_8013104c(); // random number
                float threshold = (float)rng / FLOAT_8061480c;
                if (1.0f <= threshold) {
                    sound_init_play_params((int)sound_params);
                    sound_params[0] = 0x4fd; // death sound A
                    *(float*)&sound_params[4] = FLOAT_80614810; // volume
                    sound_play_request(sound_params);
                } else {
                    sound_init_play_params((int)sound_params);
                    sound_params[0] = 0x4fe; // death sound B
                    *(float*)&sound_params[4] = FLOAT_80614810;
                    sound_play_request(sound_params);
                }
            }
        }

        // Route to appropriate death handler
        if ((*(u32*)(player_state + 0xc) & 0x10000) != 0) {
            // Teleport death (flag 0x10000) - fly camera to velocity target
            player_death_set_camera_fly(player_state);
        } else {
            if (*(int*)(player_state + 0x114) != 4) {
                // Standard death
                player_death_standard();
            }
        }

        FUN_802595d0(); // post-death animation update
        player_death_reset_state();

        // Story mode coop: reset both players
        if ((g_game_mode == 0x0a) && (1 < g_player_count)) {
            if (char_data[2] == 0) {
                player_death_reset_state();
            } else {
                player_death_reset_state();
            }
        }

        // Story mode: checkpoint logic
        if (g_game_mode == 0x0a) {
            FUN_802f3774();
        }
    }

    FUN_8038273c(); // OS unlock
}

// ============================================================================
// 0x800a583c - player_death_reset_state
// Resets the player to death state:
// - Clears animation event (FUN_8013e774)
// - Clears render visual state
// - Sets char_data flags to 8 (dead)
// - Resets weapon layers (FUN_800aecf8 if entity exists)
// - Resets weapon state (FUN_801af9f4, FUN_800adc48)
// - Sets anim state to 0x10 (FUN_800ab2a4)
// - Shows respawn location HUD
// - Sets camera to mode 1 (first-person spectator) if not already death cam
// - Moves player position slightly upward for death cam clearance
// - Clears entity active/render bits
// 12 xrefs (4 internal callers from death handlers).
// ============================================================================
void player_death_reset_state(void) {
    int player_data;
    int* char_data;
    int camera_slot;
    float offset_pos[3];

    player_data = (int)FUN_803826f0(); // OS lock + get player context

    char_data = *(int**)(*(int*)(player_data + 0x14) + 0x7c);

    FUN_8013e774(*(int*)(player_data + 0x14)); // clear anim events
    render_instance_clear_visual_state(*(int*)(*(int*)(player_data + 0x14) + 4));

    char_data[9] = 8; // dead state flags

    // Reset weapon state if entity valid
    if (*(int*)player_data != 0) {
        FUN_800aecf8(char_data); // weapon layer clear
    }

    FUN_801af9f4(player_data);    // weapon layer reset
    FUN_800adc48((int)char_data); // weapon visual clear
    FUN_800ab2a4(player_data, 0x10); // set anim state dead

    player_show_respawn_location(player_data);

    // Set camera to spectator mode if not already in death camera
    camera_slot = *(int*)(player_data + 8);
    if ((camera_slot != 0) &&
        (*(int*)(camera_slot + 0x26c) != 0x80402640) && // not death cam A
        (*(int*)(camera_slot + 0x26c) != 0x80402ac0)) { // not death cam B
        camera_set_mode_by_index(camera_slot, 1, *(int*)(player_data + 0x14));
    }

    // Move position up slightly for death camera clearance
    float y_offset = FLOAT_80614824;
    *(float*)(player_data + 0xa0) = FLOAT_80614820;

    offset_pos[0] = y_offset + *(float*)&g_active_player[0x2a];
    offset_pos[1] = *(float*)&g_active_player[0x2b];
    offset_pos[2] = y_offset + *(float*)&g_active_player[0x2c];

    // Move with navigation to ensure valid position
    nav_advanced_movement_dispatch(
        (double)(*(float*)&g_active_player[0x25] + *(float*)&g_active_player[0x23]),
        (double)*(float*)&g_active_player[0x28],
        *g_active_player,
        g_active_player + 0x2a,
        &offset_pos,
        NULL, 1, -3);

    g_active_player[0x2a] = *(int*)&offset_pos[0];
    g_active_player[0x2b] = *(int*)&offset_pos[1];
    g_active_player[0x2c] = *(int*)&offset_pos[2];

    // Clear entity active/render bits
    *(u32*)(*(int*)(player_data + 0x14) + 0x28) &= 0xffffff9f;

    FUN_8038273c(); // OS unlock
}

// ============================================================================
// 0x800a5990 - player_death_set_camera_fly
// Sets up a "fly" death camera that tracks the player's last velocity vector.
// Used when player dies while teleporting (flag 0x10000).
// Computes a target position from last velocity, normalized and scaled by 0.5.
// Sets camera to mode 7 (death fly).
// Calls player_death_reset_state internally, plus coop variant for story mode.
// 1 xref.
// ============================================================================
void player_death_set_camera_fly(int player_state) {
    int char_data_ptr;
    float vel_x, vel_y, vel_z;
    float target[3];

    char_data_ptr = *(int*)(*(int*)(player_state + 0x14) + 0x7c);

    // Call standard death reset first
    player_death_reset_state();

    // Story mode coop: additional resets
    if ((g_game_mode == 0x0a) && (1 < g_player_count)) {
        if (*(int*)(char_data_ptr + 8) == 0) {
            player_death_reset_state();
        } else {
            player_death_reset_state();
        }
    }

    // Clear velocity tracking
    *(float*)(char_data_ptr + 0xb1c) = 0.0f;

    // Get last movement velocity
    vel_x = *(float*)(player_state + 0xe38);
    vel_y = *(float*)(player_state + 0xe3c);
    vel_z = *(float*)(player_state + 0xe40);

    // Apply gravity offset
    float gravity = FLOAT_80614828;
    *(float*)(player_state + 0xe38) = vel_x + gravity;

    // Normalize velocity if above threshold
    float mag_sq = vel_z * vel_z + vel_x * vel_x + vel_y * vel_y;
    float max_sq = FLOAT_8061482c;
    if (max_sq < mag_sq) {
        float inv_mag = 1.0f / sqrtf(mag_sq);
        // Newton-Raphson refinement
        inv_mag = inv_mag * -(1.0f * mag_sq * inv_mag * inv_mag - 3.0f);
        float scale = 1.0f * inv_mag * -(1.0f * mag_sq * inv_mag * inv_mag - 3.0f);
        vel_x *= scale;
        vel_y *= scale;
        vel_z *= scale;
    }

    // Compute target from velocity * -0.5 + entity position
    float half = FLOAT_806147c4; // 0.5
    target[0] = -(half * vel_x - *(float*)(player_state + 0xb4));
    target[1] = -(half * vel_y - *(float*)(player_state + 0xb8));
    target[2] = -(half * vel_z - *(float*)(player_state + 0xbc));

    camera_death_set_target(target);
    FUN_80023280(0.0, 1.0); // reset blend

    // Set camera to death fly mode (7)
    if (*(int*)(player_state + 8) != 0) {
        camera_set_mode_by_index(*(int*)(player_state + 8), 7, *(int*)(player_state + 0x14));
    }
}

// ============================================================================
// 0x800a5b14 - player_death_standard
// Standard death path (no teleport, no cutscene).
// Calls player_death_reset_state, handles story coop resets,
// then calls FUN_800aee44 (weapon drop / death effects).
// 2 xrefs (from player_on_death_dispatch).
// ============================================================================
void player_death_standard(void) {
    int player_data;
    int* char_data;

    player_data = FUN_803826f0(); // OS lock + context
    char_data = *(int**)(*(int*)(player_data + 0x14) + 0x7c);

    player_death_reset_state();

    // Story mode coop: additional resets
    if ((g_game_mode == 0x0a) && (1 < g_player_count)) {
        if (*(int*)(char_data + 8) == 0) {
            player_death_reset_state();
        } else {
            player_death_reset_state();
        }
    }

    FUN_800aee44(); // weapon drop / death effects
    FUN_8038273c(); // OS unlock
}

// ============================================================================
// 0x800a5bb0 - player_death_cutscene
// Cutscene death handler - sets player state to 0xd5 (cutscene death anim).
// Only triggers if not already in state 0x40000 or not in specific cutscene.
// Sets death camera target from entity position, calls player_death_reset_state,
// sets camera mode 7, updates active camera state to 0xd5.
// param_11: if 0 uses high Y offset, else uses lower Y offset for target.
// 1 xref.
// ============================================================================
void player_death_cutscene(int param_11) {
    int player_state;
    int entity;
    int char_data_ptr;
    float target[3];

    player_state = FUN_803826ec(); // OS lock + context
    entity = *(int*)(player_state + 0x14);
    char_data_ptr = *(int*)(entity + 0x7c);

    // Skip if already in cutscene death or specific anim state
    if ((*(int*)(player_state + 0x114) == 0x40000) ||
        ((*(int*)(player_state + 0x114) == 0x10) &&
         (*(int*)(g_active_camera_slot + 0x2a0) == 0xd5))) {
        FUN_80382738(); // OS unlock
        return;
    }

    // Set death target from entity position with Y offset
    float y_offset;
    if (param_11 == 0) {
        y_offset = FLOAT_80614830; // high offset
    } else {
        y_offset = FLOAT_806147e4; // low offset
    }

    target[0] = *(float*)(entity + 0x64); // entity pos X
    target[1] = FLOAT_80614834;           // fixed Y
    target[2] = y_offset;                 // Z from param

    camera_death_set_target(target);
    player_death_reset_state();

    // Story coop: additional resets
    if ((g_game_mode == 0x0a) && (1 < g_player_count)) {
        if (*(int*)(char_data_ptr + 8) == 0) {
            player_death_reset_state();
        } else {
            player_death_reset_state();
        }
    }

    // Set cutscene death state
    *(int*)(player_state + 0x114) = 0x40000;
    *(int*)(entity + 0x34) = 0xd5;
    *(int*)(g_active_player + 0xdec) = 0xd5;
    *(int*)(g_active_camera_slot + 0x2a0) = 0xd5;

    // Set camera to death fly mode
    if (*(int*)(player_state + 8) != 0) {
        camera_set_mode_by_index(*(int*)(player_state + 8), 7, *(int*)(player_state + 0x14));
    }

    FUN_800aee44(); // weapon drop / death effects
    FUN_80382738(); // OS unlock
}
