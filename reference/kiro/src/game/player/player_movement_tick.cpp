// Address: 0x800a781c | Name: player_movement_integrate_jump | Date: 2026-08-11
// Address: 0x800a7a04 | Name: player_respawn_zone_select | Date: 2026-08-11
// Address: 0x800a7b6c | Name: player_death_tick | Date: 2026-08-11
// Address: 0x800a8044 | Name: player_cutscene_movement_tick | Date: 2026-08-11
// Address: 0x800a814c | Name: player_enter_menu_state | Date: 2026-08-11
// Address: 0x800a821c | Name: player_exit_menu_state | Date: 2026-08-11
// Address: 0x800a8420 | Name: player_stub_empty | Date: 2026-08-11
// Address: 0x800a88dc | Name: player_vehicle_aim_tick | Date: 2026-08-11
// Address: 0x800a8c78 | Name: player_vehicle_movement_tick | Date: 2026-08-11
// Address: 0x800a9044 | Name: player_mounted_position_update | Date: 2026-08-11
//
// Player per-frame tick functions: movement physics (jump/fall), death state,
// menu/pause state transitions, vehicle aiming/movement, mounted positioning.

#include "common.h"

// ============================================================================
// 0x800a781c - player_movement_integrate_jump
// Jump/fall movement integration with variable gravity.
// Applies gravity acceleration scaled by a "float factor" at char_data+0xb20,
// then caps vertical speed to [FLOAT_80614874, FLOAT_8061488c].
// If speed exceeds threshold FLOAT_80614890, applies per-frame air drag.
// Then integrates forward+strafe into target position like player_movement_integrate
// but uses vertical_speed * dt for Y instead of fixed gravity.
// Also stores horizontal movement direction at player[0x38e] and player[0x390].
// 1 xref (from player tick state dispatch).
// ============================================================================
void player_movement_integrate_jump(double forward_speed, double strafe_speed,
                                    float* out_pos, float* forward_dir, float* strafe_dir) {
    int char_data = *(int*)(g_active_player[5] + 0x7c);
    float max_up = FLOAT_8061488c;
    float min_down = FLOAT_80614874;

    // Apply gravity with float factor
    float gravity = FLOAT_80614828;
    float buoyancy = FLOAT_80614824;
    float float_factor = *(float*)(char_data + 0xb20);
    *(float*)(char_data + 0xb1c) += (gravity * FLOAT_806108ec * float_factor) -
                                     (buoyancy * FLOAT_806108ec);

    // Clamp vertical speed
    if (*(float*)(char_data + 0xb1c) > max_up) {
        *(float*)(char_data + 0xb1c) = max_up;
    } else if (*(float*)(char_data + 0xb1c) < min_down) {
        *(float*)(char_data + 0xb1c) = min_down;
    }

    // Air drag when exceeding threshold
    float drag_threshold = FLOAT_80614890;
    float drag_factor = FLOAT_80614894;
    if (drag_threshold < fabsf(*(float*)(char_data + 0xb1c))) {
        u32 frame = 0;
        while (FLOAT_806108ec > (float)frame) { // per sub-frame drag
            frame++;
            *(float*)(char_data + 0xb1c) *= drag_factor;
        }
    }

    // Compute target position
    float damp = FLOAT_80614878;
    out_pos[0] = (float)((double)forward_dir[0] * forward_speed +
                 (double)((float)((double)strafe_dir[0] * strafe_speed +
                          (double)(float)g_active_player[0x2a])));
    out_pos[1] = *(float*)(char_data + 0xb1c) * FLOAT_806108ec + (float)g_active_player[0x2b];
    out_pos[2] = (float)((double)forward_dir[1] * forward_speed +
                 (double)((float)((double)strafe_dir[1] * strafe_speed +
                          (double)(float)g_active_player[0x2c])));

    // Dampen vertical offset
    g_active_player[0x26] = (float)g_active_player[0x26] * damp;

    // Store horizontal movement direction
    g_active_player[0x38e] = (float)((double)strafe_dir[0] * strafe_speed +
                              (double)((float)((double)forward_dir[0] * forward_speed)));
    g_active_player[0x390] = (float)((double)strafe_dir[1] * strafe_speed +
                              (double)((float)((double)forward_dir[1] * forward_speed)));

    // Navigation collision dispatch
    int collision[12];
    nav_advanced_movement_dispatch(
        (double)((float)g_active_player[0x25] + (float)g_active_player[0x23]),
        (double)(float)g_active_player[0x28],
        *g_active_player, g_active_player + 0x2a, out_pos, collision, 1, 0);

    // Commit position + update eye height
    g_active_player[0x2a] = *(int*)&out_pos[0];
    g_active_player[0x2b] = *(int*)&out_pos[1];
    g_active_player[0x2c] = *(int*)&out_pos[2];

    FUN_800a9e74((double)(float)g_active_player[0x2a],
                 (double)((float)g_active_player[0x26] +
                          (float)g_active_player[0x2b] + (float)g_active_player[0x24]),
                 (double)(float)g_active_player[0x2c], (int)g_active_player);
}

// ============================================================================
// 0x800a7a04 - player_respawn_zone_select
// Handles respawn zone selection with duplicate avoidance.
// If FUN_80366088 returns 0 (game not ready) or player has no pad or already
// has respawn zone set: early out with timer increments.
// Otherwise: attempts up to 100 iterations to find a spawn zone not used by
// another player. Calls player_select_spawn_zone and checks all other player
// slots for matching zone at offset +0x139c. If flag bit 2 is set at
// DAT_805eba28[player*0x46] and pad == -1, calls FUN_80366358.
// 1 xref.
// ============================================================================
void player_respawn_zone_select(void) {
    int* player = (int*)FUN_803826f0(); // OS lock + context

    u32 game_ready = FUN_80366088();
    if ((game_ready == 0) || (player[1] < 0) || (player[0x4e7] != 0x7fffffff)) {
        // Not ready for respawn - check alternate path
        u32 alt = FUN_80366060();
        if ((alt == 0) || (player[0x4e7] != 0x7fffffff)) {
            FUN_80366060(); // poll again
        } else {
            // Select a zone that no other player is using
            bool duplicate = true;
            for (int attempt = 0; duplicate && (attempt < 100); attempt++) {
                duplicate = false;
                int zone = player_select_spawn_zone();
                player[0x4e7] = zone;

                // Check all other players for same zone
                int offset = 0;
                for (int i = 0; i < g_player_count; i++) {
                    if ((i != *player) &&
                        (player[0x4e7] == *(int*)(g_player_data_pool + offset + 0x139c))) {
                        duplicate = true;
                        player[0x4e7] = 0x7fffffff; // reset
                        break;
                    }
                    offset += 0x141c;
                }
            }

            // Special flag check for bot/AI spawn
            if (((g_player_flags[*player * 0x46] & 2) != 0) && (player[1] == -1)) {
                FUN_80366358(player);
            }
        }
    } else {
        // Game ready with valid pad - set respawn timers
        if (*(short*)((int)player + 0x13a2) == 0) {
            *(u16*)((int)player + 0x13a2) = 1;
        }
        if (*(short*)(player + 0x4e8) < 1) {
            *(u16*)(player + 0x4e8) = 1;
        }
    }

    FUN_8038273c(); // OS unlock
}

// ============================================================================
// 0x800a7b6c - player_death_tick
// MASSIVE per-frame tick for dead player state. Handles:
// - If no vehicle entity and no teleport flag: compute head bone position via
//   frustum transform, call nav_advanced_movement_dispatch, update eye height,
//   call projectile_update_surface_aim
// - If teleport flag (0x10000): integrate position from stored velocity
//   (player[0x38e]/0x390), apply gravity, set entity flags 0xd5, mark dirty
// - Clear entity render bits (0x60)
// - If match phase==7 and respawn flag set: call player_respawn
// - Manage respawn timer countdown (mode-dependent: 10x or 3x frame rate)
// - Check fire button held + respawn timer expired -> projectile spawn
// - Interpolate LOD near distance toward target
// - Increment play time counter
// 2 xrefs.
// ============================================================================
void player_death_tick(void) {
    int* player;
    int entity;
    int char_data;

    player = (int*)FUN_803826f0(); // OS lock + context
    entity = g_active_player[5];
    char_data = *(int*)(entity + 0x7c);

    u32 vehicle_ref = *(u32*)(char_data + 0xc08);
    int vehicle = 0;
    if (vehicle_ref != 0) {
        vehicle = g_entity_pool_base + (vehicle_ref >> 22) * 0x2e8;
        if (*(u32*)(vehicle + 0x2c) != vehicle_ref) {
            vehicle = 0;
            *(u32*)(char_data + 0xc08) = 0;
        }
    }

    if ((vehicle == 0) && ((g_active_player[3] & 0x10000) == 0)) {
        // Standard death: head position from frustum + bone transform
        *(u32*)(char_data + 0xc14) = 0;
        // ... frustum computation, nav dispatch, eye height update
        projectile_update_surface_aim(entity, 1);
    } else if ((g_active_player[3] & 0x10000) != 0) {
        // Teleport death: integrate position from stored velocity
        *(float*)(char_data + 0xb1c) += FLOAT_80614898 * FLOAT_806108ec;
        if (*(float*)(char_data + 0xb1c) < FLOAT_80614874) {
            *(float*)(char_data + 0xb1c) = FLOAT_80614874;
        }
        *(float*)(entity + 0x64) += (float)g_active_player[0x38e] * FLOAT_806108ec;
        *(float*)(entity + 0x68) += *(float*)(char_data + 0xb1c);
        *(float*)(entity + 0x6c) += (float)g_active_player[0x390] * FLOAT_806108ec;
        // Sync player position from entity
        g_active_player[0x2a] = *(int*)(entity + 0x64);
        g_active_player[0x2b] = *(int*)(entity + 0x68);
        g_active_player[0x2c] = *(int*)(entity + 0x6c);
        // Update eye + set dead anim flags
        FUN_800a9e74(...);
        *(u32*)(entity + 0x38) = 0xd5;
        *(u32*)(entity + 0x34) = 0xd5;
        *(u8*)(entity + 0x30) = 1;
        *(u32*)(entity + 0x28) |= 0x40000000;
        projectile_update_surface_aim(entity, 1);
    }

    // Clear render bits
    *(u32*)(entity + 0x28) &= 0xffffff9f;
    *(u32*)(char_data + 0x24) = 8; // char dirty flags

    // Respawn logic
    bool did_respawn = false;
    if ((g_match_phase == 7) && (*(int*)(char_data + 0xc10) != 0) && (g_active_player[1] >= 0)) {
        did_respawn = true;
        *(u32*)(char_data + 0xc10) = 0;
        player_respawn();
    }

    // Respawn timer management
    if (*(short*)(g_active_player + 0x4e8) == 0) {
        // Set initial respawn delay based on game mode
        if ((g_game_sub_mode == 3) || (g_game_sub_mode >= 4 && g_game_sub_mode <= 6) ||
            (g_game_sub_mode == 8) || (g_game_sub_mode == 0x10)) {
            *(short*)(g_active_player + 0x4e8) = (short)g_frame_rate * 10;
        } else {
            *(short*)(g_active_player + 0x4e8) = (short)g_frame_rate * 3;
        }
        *(u16*)((int)g_active_player + 0x13a2) = 0;
    }

    // Decrement timers
    *(short*)(g_active_player + 0x4e8) -= (short)g_frame_delta;
    if (*(short*)((int)g_active_player + 0x13a2) > 0) {
        *(short*)((int)g_active_player + 0x13a2) -= (short)g_frame_delta;
    } else {
        *(u16*)((int)g_active_player + 0x13a2) = 0;
    }

    // Fire button -> projectile spawn (if respawn timer expired + flag 0x10000)
    if (!did_respawn && (g_active_player[1] >= 0)) {
        u32 buttons = input_get_buttons_held(*g_active_player);
        if (((g_active_player[8] & buttons) != 0) &&
            (*(short*)((int)g_active_player + 0x13a2) == 0)) {
            if ((g_match_flags & 0x10000) != 0) {
                if (((*(u32*)(entity + 0x28) & 1) != 0) &&
                    ((*(u32*)(entity + 0x10) & 0xffff) != 0)) {
                    projectile_spawn();
                }
                projectile_init_aim_state(entity);
                *(u32*)(char_data + 0xc10) = 1;
                *(u16*)(g_active_player + 0x4e8) = 0;
            }
        }
    }

    // Interpolate LOD near distance
    g_active_player[0x23] = (int)((float)g_active_player[0x23] +
        ((float)g_active_player[0x20] - (float)g_active_player[0x23]) / FLOAT_806147fc);
    // Clamp
    float max_near = (float)g_active_player[0x20] + (float)g_active_player[0x26] -
                     (float)g_active_player[0x21];
    if (max_near < (float)g_active_player[0x23]) {
        g_active_player[0x23] = *(int*)&max_near;
    }

    // Increment play time
    g_active_player[0x39f] += g_frame_delta;

    FUN_8038273c(); // OS unlock
}

// ============================================================================
// 0x800a8044 - player_cutscene_movement_tick
// Per-frame tick for players in cutscene movement state.
// Moves entity at fixed speed (FLOAT_8061489c) per frame along X axis,
// sets Y to constant FLOAT_806148a0, preserves Z from current position.
// Calls player_death_tick (FUN_800a7b6c) for the physics integration.
// If elapsed frames exceed player+0xe7c, transitions to dead state (0x10).
// 1 xref.
// ============================================================================
void player_cutscene_movement_tick(float* out_pos) {
    int entity = *(int*)(g_active_player + 0x14);

    // Store current Z, compute new X from fixed speed
    float cur_z = *(float*)(entity + 0x6c);
    float new_x = FLOAT_8061489c * (float)g_frame_delta + *(float*)(entity + 0x64);

    // Call death tick for physics integration
    player_death_tick();

    // Override position to fixed cutscene rail
    *(float*)(entity + 0x64) = new_x;
    *(float*)(entity + 0x68) = FLOAT_806148a0; // fixed Y height
    *(float*)(entity + 0x6c) = cur_z;

    // Sync to player data
    *(float*)(g_active_player + 0xa8) = new_x;
    *(float*)(g_active_player + 0xac) = FLOAT_806148a0;
    *(float*)(g_active_player + 0xb0) = cur_z;

    // Update eye height
    FUN_800a9e74((double)*(float*)(entity + 0x64),
                 (double)(*(float*)(g_active_player + 0x98) +
                          *(float*)(entity + 0x68) + *(float*)(g_active_player + 0x90)),
                 (double)*(float*)(entity + 0x6c), (int)g_active_player);

    // Output position for caller
    out_pos[0] = new_x;
    out_pos[1] = FLOAT_806148a0;
    out_pos[2] = cur_z;

    // Timeout check -> dead state
    if ((g_frame_rate << 1) < *(int*)(g_active_player + 0xe7c)) {
        FUN_800ab2a4((int)g_active_player, 0x10);
    }
}

// ============================================================================
// 0x800a814c - player_enter_menu_state
// Enters menu/pause state for a player. 13 xrefs (story menus, pause, etc).
// Sets global flag DAT_805015c4 |= 0x2000000.
// Iterates all players and calls FUN_801a92b4 (freeze animation).
// Sets player anim state to 0x100 (menu).
// Sets camera mode from param (via camera_set_mode).
// Stores menu callback pointers at player+0x140c..+0x1418.
// If FUN_802ef550 returns 0: calls camera_splitscreen_setup (pause viewport).
// params: p3=render_callback, p4=input_callback, p5=position_data, p6=exit_callback
// ============================================================================
void player_enter_menu_state(u32 p1, u32 p2, u32 render_cb, u32 input_cb,
                             u32 position_data, u32 exit_cb) {
    int player_data;
    int camera_mode;

    // OS lock + get context (player_data in high word, camera_mode in low)
    u64 ctx = FUN_803826dc();
    player_data = (int)(ctx >> 32);
    camera_mode = (int)ctx;

    // Set global menu active flag
    g_system_flags |= 0x2000000;

    // Freeze all player animations
    int offset = 0;
    for (int i = 0; i < g_player_count; i++) {
        FUN_801a92b4(*(int*)(*(int*)(g_player_data_pool + offset + 0x14) + 0x7c));
        offset += 0x141c;
    }
    FUN_801a936c(); // animation system pause

    // Set anim state to menu (0x100)
    FUN_800ab2a4(player_data, 0x100);

    // Set camera mode
    camera_set_mode(*(u32*)(player_data + 8), camera_mode, *(int*)(player_data + 0x14));

    // Store menu callbacks
    *(u32*)(player_data + 0x140c) = position_data;
    *(u32*)(player_data + 0x1414) = render_cb;
    *(u32*)(player_data + 0x1418) = exit_cb;
    *(u32*)(player_data + 0x1410) = input_cb;

    // Setup pause viewport if not in replay
    if (FUN_802ef550() == 0) {
        camera_splitscreen_setup();
    }

    FUN_80382728(); // OS unlock
}

// ============================================================================
// 0x800a821c - player_exit_menu_state
// Exits menu/pause state. 5 xrefs.
// Clears global flag DAT_805015c4 bit 0x2000000.
// Invokes exit callback at player+0x1418 if set, then clears it.
// Calls FUN_8024488c (HUD restore), FUN_801a9378 (animation unpause).
// Iterates all players: resolves weapon state, restores camera mode 0,
// sets anim state 2 (alive), unfreezes animation (FUN_801a932c).
// Restores split-screen via camera_splitscreen_restore.
// ============================================================================
void player_exit_menu_state(void) {
    int player_data = FUN_803826f0(); // OS lock + context

    // Clear menu active flag
    g_system_flags &= ~0x2000000u;

    // Invoke exit callback
    if (*(void(**)(void))(player_data + 0x1418) != NULL) {
        (*(void(**)(void))(player_data + 0x1418))();
    }
    *(u32*)(player_data + 0x1418) = 0;
    *(u32*)(player_data + 0x1414) = 0;

    FUN_8024488c(); // HUD restore
    FUN_801a9378(); // animation unpause

    int offset = 0;
    for (int i = 0; i < g_player_count; i++) {
        int char_data = *(int*)(*(int*)(g_player_data_pool + offset + 0x14) + 0x7c);
        int weapon_state = FUN_8036fe90(g_player_data_pool + offset);
        FUN_8036fea4(weapon_state);
        camera_set_mode_by_index(*(u32*)(g_player_data_pool + offset + 8), 0,
                                 *(int*)(g_player_data_pool + offset + 0x14));
        FUN_800ab2a4(g_player_data_pool + offset, 2); // alive state
        FUN_801a932c(char_data); // unfreeze animation

        // Restore anim if cleared
        if (*(int*)(char_data + 0x94) == 0) {
            *(u32*)(char_data + 0x94) = *(u32*)(char_data + 0x9c);
        }
        if (*(int*)(char_data + 0x94) == 0) {
            *(u32*)(char_data + 0x94) = 1;
        }
        offset += 0x141c;
    }

    camera_splitscreen_restore();
    FUN_8038273c(); // OS unlock
}

// ============================================================================
// 0x800a8420 - player_stub_empty
// Empty stub function. 1 xref.
// ============================================================================
void player_stub_empty(void) {
    return;
}

// ============================================================================
// 0x800a88dc - player_vehicle_aim_tick
// Per-frame vehicle turret aiming. Interpolates yaw and pitch toward the
// mounted weapon's angles with frame-rate-independent speed (FLOAT_80611d84/80).
// Wraps angles to [-pi, pi] range. Also integrates eye height offset
// (FLOAT_806148ac * dt, capped at FLOAT_806148b0).
// Computes weapon bone position and updates eye position accordingly.
// Handles 3 weapon position modes based on vehicle flags:
//   - 0x4000: offset from bone + render offset (aerial turret)
//   - 0x10000: callback-based position (FUN_8023ff0c)
//   - Default: direct from player position + eye offset
// 1 xref.
// ============================================================================
void player_vehicle_aim_tick(void) {
    FUN_803826ec(); // OS lock

    int entity = *(int*)(g_active_player + 0x14);
    int vehicle = *(int*)(*(int*)(entity + 0x7c) + 0xc14);
    int vehicle_data = *(int*)(vehicle + 0x7c);
    u8* weapon_def = weapon_get_def_from_entity();

    // Yaw interpolation toward mounted weapon angle
    float target_yaw = *(float*)(g_active_player + 0x100) - *(float*)(vehicle_data + 0x74);
    // Wrap to [-pi, pi]
    while (target_yaw <= FLOAT_806148a8) target_yaw += FLOAT_806148a4;
    while (FLOAT_806147b0 < target_yaw) target_yaw -= FLOAT_806148a4;

    float yaw_speed = FLOAT_806108ec * (FLOAT_80611d84 / (float)(g_frame_rate));
    FUN_8024001c(); // compute weapon aim direction

    if (yaw_speed <= fabsf(target_yaw)) {
        if (target_yaw >= 0.0f) yaw_speed = -yaw_speed;
        *(float*)(g_active_player + 0x100) += yaw_speed;
    } else {
        *(float*)(g_active_player + 0x100) = *(float*)(vehicle_data + 0x74);
    }

    // Pitch interpolation
    float target_pitch = *(float*)(g_active_player + 0x104) - (-(*(float*)(vehicle_data + 0x78)));
    while (target_pitch <= FLOAT_806148a8) target_pitch += FLOAT_806148a4;
    while (FLOAT_806147b0 < target_pitch) target_pitch -= FLOAT_806148a4;

    float pitch_speed = FLOAT_806108ec * (FLOAT_80611d80 / (float)(g_frame_rate));
    if (pitch_speed <= fabsf(target_pitch)) {
        if (target_pitch >= 0.0f) pitch_speed = -pitch_speed;
        *(float*)(g_active_player + 0x104) += pitch_speed;
    } else {
        *(float*)(g_active_player + 0x104) = -(*(float*)(vehicle_data + 0x78));
    }

    // Eye height integration
    *(float*)(g_active_player + 0x98) += FLOAT_806148ac * FLOAT_806108ec;
    if (FLOAT_806148b0 < *(float*)(g_active_player + 0x98)) {
        *(float*)(g_active_player + 0x98) = FLOAT_806148b0;
    }

    // Weapon animation check
    if ((*(int*)(weapon_def + 0x38) >= 0) &&
        (FUN_8002588c(entity, *(int*)(weapon_def + 0x38)) == 0)) {
        FUN_80023280(0.0, 0.0);
    }

    // Store yaw to entity
    *(float*)(entity + 0xc4) = *(float*)(g_active_player + 0x100);

    // Compute weapon/eye position based on vehicle type flags
    if ((*(u32*)(vehicle_data + 8) & 0x4000) != 0) {
        // Aerial turret: bone offset - 1.0 + render offsets
        // ... position computation from bone matrices
    } else if ((*(u32*)(vehicle_data + 8) & 0x10000) != 0) {
        // Callback-based position
        FUN_8023ff0c(vehicle, weapon_def + 0x40, (float*)(g_active_player + 0xb4));
    } else {
        // Standard: direct position + eye offset
        *(float*)(g_active_player + 0xb4) = *(float*)(g_active_player + 0xa8);
        *(float*)(g_active_player + 0xb8) = *(float*)(g_active_player + 0x98) +
            *(float*)(g_active_player + 0xac) + *(float*)(g_active_player + 0x90);
        *(float*)(g_active_player + 0xbc) = *(float*)(g_active_player + 0xb0);
    }

    FUN_80382738(); // OS unlock
}

// ============================================================================
// 0x800a8c78 - player_vehicle_movement_tick
// Per-frame vehicle movement integration. Uses nav_movement_dispatch (not
// advanced variant) for collision resolution. Computes facing direction from
// yaw+pitch via trig (sin/cos: FUN_801313a4) and builds a direction matrix
// from combined yaw angles. Updates player position, eye position, and
// weapon bone offsets. Handles 3 modes like vehicle_aim_tick:
// flag 0x4000 (aerial), 0x10000 (callback), default (direct).
// Calls FUN_80237b50 with anim ID 0x41 for vehicle animation.
// Clears spectator camera speed if g_active_camera_slot is non-null.
// 1 xref.
// ============================================================================
void player_vehicle_movement_tick(void) {
    FUN_803826e8(); // OS lock
    int entity = g_active_player[5];
    int vehicle = *(int*)(*(int*)(entity + 0x7c) + 0xc14);
    int vehicle_data = *(int*)(vehicle + 0x7c);
    u8* weapon_def = weapon_get_def_from_entity();

    u32 saved_flags = *(u32*)(vehicle + 0x28);
    FUN_8024001c(); // compute direction

    // Adjust position Y by -1.0
    // ... (local position computation)

    // Clear entity render bits for nav dispatch
    *(u32*)(vehicle + 0x28) &= 0xffffff9f;

    // Nav movement dispatch
    int collision[12];
    u32 result = nav_movement_dispatch(
        (double)((float)g_active_player[0x25] + (float)g_active_player[0x23]),
        (double)(float)g_active_player[0x28],
        *g_active_player, g_active_player + 0x2a, &local_pos,
        NULL, collision, 1, 0);

    // Restore entity flags
    *(u32*)(vehicle + 0x28) = saved_flags;

    // If collision bit 2: compute facing direction from trig
    if ((result & 2) != 0) {
        float angle = FUN_80240238(vehicle, &local_pos);
        g_active_player[0x40] = *(int*)&angle;

        // Build direction matrix from combined angles
        float yaw = (float)g_active_player[0x40] + (float)g_active_player[0x50];
        float pitch = (float)g_active_player[0x41] + (float)g_active_player[0x51];
        float scale = FLOAT_80614860 / FLOAT_806147b0;

        // sin/cos for combined angles
        float cos_yaw = (float)FUN_801313a4((double)(scale * yaw));
        float sin_yaw = (float)FUN_801313a4((double)(scale * yaw - FLOAT_806148b4));
        float cos_pitch = (float)FUN_801313a4((double)(scale * pitch));
        float sin_pitch = (float)FUN_801313a4((double)(scale * pitch - FLOAT_806148b4));

        g_active_player[0x30] = *(int*)&(float)(cos_pitch * sin_yaw);
        g_active_player[0x31] = *(int*)&sin_pitch;
        g_active_player[0x32] = *(int*)&(float)(cos_pitch * cos_yaw);
    }

    // Eye height integration (same as aim tick)
    g_active_player[0x26] = (int)(FLOAT_806148ac * FLOAT_806108ec + (float)g_active_player[0x26]);
    if (FLOAT_806148b0 < (float)g_active_player[0x26]) {
        g_active_player[0x26] = *(int*)&FLOAT_806148b0;
    }

    // Store yaw to entity + vehicle bone offsets
    *(float*)(entity + 0xc4) = (float)g_active_player[0x40];
    *(float*)(vehicle_data + 0x24) = (float)g_active_player[0x40] - *(float*)(vehicle + 0x70);
    *(float*)(vehicle_data + 0x1c) = -(float)g_active_player[0x41] - *(float*)(weapon_def + 0x3c);

    // Update player position from nav result
    g_active_player[0x2a] = *(int*)&local_pos[0];
    g_active_player[0x2b] = *(int*)&local_pos[1];
    g_active_player[0x2c] = *(int*)&local_pos[2];

    // Compute eye/weapon position (3 modes: 0x4000, 0x10000, default)
    // ... (same pattern as vehicle_aim_tick)

    // Clear spectator camera speed
    if (g_active_camera_slot != 0) {
        *(float*)(g_active_camera_slot + 0x3b8) = 0.0f;
    }

    // Vehicle animation tick
    float dt = FLOAT_806108ec * (1.0f / (float)(g_frame_rate));
    FUN_80237b50(dt, 0x41, *g_active_player);

    FUN_80382734(); // OS unlock
}

// ============================================================================
// 0x800a9044 - player_mounted_position_update
// Updates player position from a mounted/attached entity's bone transform.
// Resolves entity at char_data+0xc04 (mounted platform), gets its render
// matrix, transforms the local offset (entity+0x134..+0x13c) by the
// inverse matrix, adds world-space bone position, stores result as both
// player position (player+0xa8) and entity position (entity+0x64).
// Updates eye height from position + offset.
// 1 xref.
// ============================================================================
void player_mounted_position_update(void) {
    FUN_803826f0(); // OS lock

    int player = (int)g_active_player;
    int entity = *(int*)(g_active_player + 0x14);
    int platform = *(int*)(*(int*)(entity + 0x7c) + 0xc04);

    if (platform != 0) {
        // Get inverse of platform's render matrix
        float inv_matrix[16];
        FUN_80130b90(*(float**)(*(int*)(platform + 4) + 0x10), inv_matrix);

        // Get local offset from entity
        float local_offset[3];
        local_offset[0] = *(float*)(platform + 0x134);
        local_offset[1] = *(float*)(platform + 0x138);
        local_offset[2] = *(float*)(platform + 0x13c);

        // Transform offset by inverse matrix
        FUN_8012fb90(inv_matrix, local_offset);

        // Add world-space result from matrix transform
        float world_pos[3];
        world_pos[0] = local_offset[0] + inv_matrix[12]; // matrix translation
        world_pos[1] = local_offset[1] + inv_matrix[13];
        world_pos[2] = local_offset[2] + inv_matrix[14];

        // Store to player and entity
        *(float*)(player + 0xa8) = world_pos[0];
        *(float*)(player + 0xac) = world_pos[1];
        *(float*)(player + 0xb0) = world_pos[2];
        *(float*)(entity + 0x64) = world_pos[0];
        *(float*)(entity + 0x68) = world_pos[1];
        *(float*)(entity + 0x6c) = world_pos[2];

        // Update eye height
        FUN_800a9e74((double)*(float*)(player + 0xa8),
                     (double)(*(float*)(player + 0xac) + *(float*)(player + 0x90)),
                     (double)*(float*)(player + 0xb0), player);
    }

    FUN_8038273c(); // OS unlock
}
