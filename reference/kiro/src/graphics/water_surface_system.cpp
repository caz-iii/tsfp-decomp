// =============================================================================
// Water Surface System - Update, Fog, Entity, and Dispatch
// Address: 0x800f2f40 - 0x800f363c | Batch 303 | Date: 2026-08-25
// =============================================================================
// Continuation of the water surface rendering module. This file covers the
// per-frame update loop (wind-driven wave animation), fog control for underwater
// rendering, the scene pipeline render callback, water entity spawn/positioning,
// and configuration accessors.
// =============================================================================

#include "common.h"
#include "gx.h"
#include "math_util.h"
#include "level.h"
#include "entity.h"
#include "collision.h"

// --- External references from water_surface_render.cpp ---
extern int   g_water_enabled;           // 0x80611f24
extern int   g_water_animated_mode;     // 0x80611f04
extern int   g_water_stream_queued;     // 0x80611f08
extern int   g_water_params_dirty;      // 0x80611f14
extern int   g_water_pending_type;      // 0x80611f18
extern int   g_water_camera_override;   // 0x80611d54

// --- Wind interpolation state (BSS) ---
static float g_wind_timer;              // 0x80611eec - accumulator for wind change
static float g_wind_interval;           // 0x80611ee8 - time between wind changes
static int   g_wind_current_slot;       // 0x80611ef0 - current wind direction (0-4)
static int   g_wind_prev_slot;          // 0x80611ef4 - previous wind direction

// Wind speed per-direction table (5 entries, indexed by slot)
// 0x803cc908[slot] = speed multiplier for direction
// 0x803cc91c[slot * 5] = cumulative probability thresholds for next direction

// Wind output: 4-channel speed vector applied to wave mesh
static float g_wind_speed[4];           // 0x80571988-0x80571994
static float g_wind_speed_total;        // 0x80571998

// Per-tile wave state (5 tiles tracked for wind direction transitions)
static float g_tile_wave_dir[5];        // 0x80570f30 - direction flag (0.0 or 360.0)
static float g_tile_wave_progress[5];   // 0x80570f44 - interpolation progress
static u32   g_tile_wave_target[5];     // 0x80570f58 - target random index
static u32   g_tile_wave_prev[5];       // 0x80570f6c - previous random index

// Level fog parameters
static float g_fog_near;                // 0x806104a0
static float g_fog_far;                 // 0x806104a4

// Pending water params buffer (set by water_surface_set_params)
static u8    g_water_params_pending[0x3c]; // 0x8057194c
// Level water config buffer (copied during level init)
static u8    g_water_level_config[0x3c];   // 0x80571910

// =============================================================================
// Address: 0x800f2f40 | Name: water_surface_clear_params_dirty
// Clears the params_dirty flag after pending parameters have been consumed.
// =============================================================================
void water_surface_clear_params_dirty(void) {
    g_water_params_dirty = 0;
}

// =============================================================================
// Address: 0x800f2f4c | Name: water_surface_per_frame_update
// Main per-frame water surface update. Calls UV animation, then if in vertex-
// animated mode, runs a wind interpolation system with random directional
// transitions. The wind system uses 5 slots with probability-weighted random
// selection for the next wind direction, and interpolates wave speed between
// keyframes at a rate derived from a random timer.
// =============================================================================
void water_surface_per_frame_update(void) {
    // Prologue
    // FUN_803826dc();

    float dt = CONST_FRAME_TIME * (CONST_360 / (float)g_target_framerate);

    // Enable/disable water active state
    water_surface_set_active(1);

    // Always animate UVs
    water_surface_animate_uvs();

    // Vertex-animated mode: wind interpolation
    if (g_water_animated_mode == 0) {
        goto epilogue;
    }

    if (g_water_stream_queued != 0) {
        // Advance wind change timer
        g_wind_timer += dt;

        if (g_wind_timer >= g_wind_interval) {
            // Time to pick a new wind direction
            int new_slot = g_wind_current_slot;
            g_wind_prev_slot = g_wind_current_slot;

            // Weighted random selection from probability table
            u32 rng = random_u32();
            float normalized = (float)rng / CONST_RNG_DIVISOR;
            float cumulative = 0.0f;
            bool found = false;

            for (int i = 0; i < 5 && !found; i++) {
                float threshold = g_wind_prob_table[g_wind_current_slot * 5 + i];
                if (normalized / CONST_PROB_SCALE < cumulative + threshold) {
                    found = true;
                    g_wind_current_slot = i;
                }
                cumulative += threshold;
            }

            // Reset timer with randomized interval
            u32 rng2 = random_u32();
            g_wind_timer = 0.0f;
            g_wind_interval = CONST_WIND_INTERVAL_BASE +
                              CONST_WIND_INTERVAL_SCALE *
                              ((float)rng2 / CONST_RNG_DIVISOR);
        }
    }

    // Interpolate wind speed between prev and current direction
    float t = g_wind_timer / g_wind_interval;
    float speed = g_wind_speed_table[g_wind_prev_slot] * (CONST_360 - t) +
                  g_wind_speed_table[g_wind_current_slot] * t;
    float scaled_speed = speed * dt;

    // Apply directional multipliers to 4 wind channels
    g_wind_speed[0] = CONST_WIND_MULT_0 * scaled_speed;
    g_wind_speed[1] = CONST_WIND_MULT_1 * scaled_speed;
    g_wind_speed[2] = CONST_WIND_MULT_2 * scaled_speed;
    g_wind_speed[3] = g_wind_speed[0]; // mirror channel 0
    g_wind_speed_total = 0.0f;

    // Per-tile wave direction transitions (5 tiles)
    for (int i = 0; i < 5; i++) {
        if (g_tile_wave_dir[i] == 0.0f) {
            // Decrementing direction
            g_tile_wave_progress[i] -= g_wind_speed[i];
            if (g_tile_wave_progress[i] < 0.0f) {
                // Pick new random target (avoid repeats)
                u32 r;
                do {
                    do {
                        r = random_u32() % 10;
                    } while (r == g_tile_wave_target[i]);
                } while (r == g_tile_wave_prev[i]);
                g_tile_wave_prev[i] = r;
                g_tile_wave_progress[i] = 0.0f;
                g_tile_wave_dir[i] = 360.0f; // flip direction
            }
        } else {
            // Incrementing direction
            g_tile_wave_progress[i] += g_wind_speed[i];
            if (g_tile_wave_progress[i] > 360.0f) {
                // Pick new random target (avoid repeats)
                u32 r;
                do {
                    do {
                        r = random_u32() % 10;
                    } while (r == g_tile_wave_target[i]);
                } while (r == g_tile_wave_prev[i]);
                g_tile_wave_target[i] = r;
                g_tile_wave_progress[i] = 360.0f;
                g_tile_wave_dir[i] = 0.0f; // flip direction
            }
        }
    }

epilogue:
    // FUN_80382728();
    return;
}

// =============================================================================
// Address: 0x800f325c | Name: water_surface_fog_disable
// Disables fog for water surface rendering by setting all fog parameters to 0.
// Called before water render to prevent fog artifacts on the water plane.
// =============================================================================
void water_surface_fog_disable(void) {
    fog_set_params(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0);
}

// =============================================================================
// Address: 0x800f32a0 | Name: water_surface_fog_apply
// Applies water-specific fog settings for underwater rendering. Uses either
// the pending params (if params_dirty is set) or the level config data
// (if pending_type is nonzero). Passes position, distance, near/far planes,
// type ID, and two config values to the fog setup function.
// =============================================================================
void water_surface_fog_apply(void) {
    if (g_water_params_dirty != 0) {
        // Use pending params from water_surface_set_params
        float pos_x = *(float*)(&g_water_params_pending[0x10]); // 0x80571960
        float pos_y = *(float*)(&g_water_params_pending[0x14]); // 0x80571964
        float pos_z = *(float*)(&g_water_params_pending[0x18]); // 0x80571968
        int   dist  = *(int*)(&g_water_params_pending[0x24]);   // 0x80571974
        int   type  = g_water_param_scale;                      // 0x80611f10
        int   cfg_a = *(int*)(&g_water_params_pending[0x00]);   // 0x80571950
        int   cfg_b = *(int*)(&g_water_params_pending[0x08]);   // 0x80571958

        fog_set_params(pos_x, pos_y, pos_z, (float)(-dist), g_fog_near, g_fog_far,
                       type, cfg_a, cfg_b);
    } else if (g_water_pending_type != 0) {
        // Use level config
        float pos_x = *(float*)(&g_water_level_config[0x14]); // 0x80571924
        float pos_y = *(float*)(&g_water_level_config[0x18]); // 0x80571928
        float pos_z = *(float*)(&g_water_level_config[0x1c]); // 0x8057192c
        int   dist  = *(int*)(&g_water_level_config[0x28]);   // 0x80571938
        int   type  = g_water_pending_type;                   // 0x80611f18
        int   cfg_a = *(int*)(&g_water_level_config[0x04]);   // 0x80571914
        int   cfg_b = *(int*)(&g_water_level_config[0x0c]);   // 0x8057191c

        fog_set_params(pos_x, pos_y, pos_z, (float)(-dist), g_fog_near, g_fog_far,
                       type, cfg_a, cfg_b);
    }
}

// =============================================================================
// Address: 0x800f336c | Name: water_surface_render_dispatch
// Scene render pipeline callback. If water is enabled, saves the current blend
// mode and depth write state, calls water_surface_render(), then restores both.
// Ensures water renders with correct transparency without affecting other draws.
// =============================================================================
void water_surface_render_dispatch(void) {
    if (g_water_enabled != 0) {
        int prev_blend = gx_set_blend_mode(1);
        int prev_depth = gx_set_depth_write(0);

        water_surface_render();

        gx_set_blend_mode(prev_blend);
        gx_set_depth_write(prev_depth);
    }
}

// =============================================================================
// Address: 0x800f33d4 | Name: water_surface_spawn_entity
// Spawns the water surface collision/render entity during player slot init.
// Uses entity type ID from level water config (+0x04). Positions at level
// config offset (+0x24, +0x28, +0x2c). Sets render flags: 0x40000000 (no shadow
// cast), assigns per-player visibility bit, then syncs position to camera.
// =============================================================================
void water_surface_spawn_entity(int player_slot) {
    int water_set = level_get_weapon_set_ptr();

    int entity_type = *(int*)(water_set + 4);
    if (entity_type < 0) {
        // No water entity type defined
        *(int*)(player_slot + 0xe04) = 0;
        return;
    }

    // Spawn position from level config
    float spawn_pos[3];
    spawn_pos[0] = *(float*)(water_set + 0x24);
    spawn_pos[1] = *(float*)(water_set + 0x28);
    spawn_pos[2] = *(float*)(water_set + 0x2c);

    // Spawn rotation (identity with Y from config)
    float spawn_rot[3];
    spawn_rot[0] = 0.0f;
    spawn_rot[1] = *(float*)(water_set + 0x30);
    spawn_rot[2] = 0.0f;

    int entity = entity_spawn_by_type(entity_type, spawn_pos, spawn_rot, 0, 0);
    *(int*)(player_slot + 0xe04) = entity;

    // Set render order (camera render order + 0xcf)
    *(int*)(entity + 0x34) = *(int*)(player_slot + 0x214) + 0xcf;

    // Clear player visibility mask initially
    *(int*)(entity + 0x10) = 0;

    // Set no-shadow flag
    *(u32*)(entity + 0x28) |= 0x40000000;

    // Set per-player visibility bit
    int player_index = *(int*)(player_slot + 0x214);
    if (player_index < 4) {
        *(u32*)(entity + 0x10) |= (1 << player_index);
    }

    // Sync entity position to camera
    water_surface_update_entity_pos(player_slot);
}

// =============================================================================
// Address: 0x800f34b8 | Name: water_surface_update_entity_pos
// Updates the water entity position to follow the camera. Reads position offset
// from level water config and adds to camera world position. Handles Y axis
// specially: if config flag bit 0 is clear, calls a height resolver for certain
// levels. Also called per-frame from camera_per_frame_update.
// =============================================================================
void water_surface_update_entity_pos(int player_slot) {
    int water_set = level_get_weapon_set_ptr();
    int entity = *(int*)(player_slot + 0xe04);

    if (entity == 0) return;

    int flags = *(u32*)(water_set + 8);
    if ((flags & 3) == 0) return;

    // X: camera pos + config offset
    *(float*)(entity + 0x64) = *(float*)(player_slot + 0x27c) +
                                *(float*)(water_set + 0x24);

    // Y: depends on flag bit 0
    if ((flags & 1) == 0) {
        // Special height calculation for level 0x34
        if (g_current_level_id == 0x34) {
            compute_water_height_for_level();
        }
    } else {
        // Camera Y + config offset
        *(float*)(entity + 0x68) = *(float*)(player_slot + 0x280) +
                                    *(float*)(water_set + 0x28);
    }

    // Z: camera pos + config offset
    *(float*)(entity + 0x6c) = *(float*)(player_slot + 0x284) +
                                *(float*)(water_set + 0x2c);
}

// =============================================================================
// Address: 0x800f3560 | Name: water_surface_should_apply_fog
// Returns whether water fog should be applied this frame. Checks multiple
// conditions: camera must exist, must not be in certain game states (menu
// transitions, specific sub-modes), pending_type must be nonzero, camera
// must not have flag bit 3 set, and current level must not be type 0x11/0x0f.
// =============================================================================
int water_surface_should_apply_fog(void) {
    int current_level_type = -1;

    if (g_water_camera_override == 0) {
        return 0;
    }

    // Check if camera has a particular flag
    if ((*(u32*)(g_water_camera_override + 0x21c) & 2) != 0) {
        int player_data = *(int*)(g_player_slot_ptr + 4);
        current_level_type = g_level_type_table[player_data * 0x5df];
    }

    // Don't apply during certain game mode transitions
    if (g_game_mode == 0x11 && g_player_slot_ptr != 0) {
        int sub_state = *(int*)(g_player_slot_ptr + 0xdec);
        if (sub_state == 0x1b || sub_state == 0x1d || sub_state == 0x17 ||
            sub_state == 0x19 || sub_state == 0x1a || sub_state == 0x18) {
            return 0;
        }
    }

    if (g_water_pending_type == 0) return 0;
    if ((*(u32*)(g_water_camera_override + 0x21c) & 8) != 0) return 0;
    if (current_level_type == 0x11) return 0;
    if (current_level_type == 0x0f) return 0;

    return 1;
}

// =============================================================================
// Address: 0x800f361c | Name: water_surface_get_config_ptr
// Returns pointer to the active water surface config data. If params_dirty
// is set (from water_surface_set_params), returns pending params buffer.
// Otherwise returns the level config buffer copied during level init.
// =============================================================================
void* water_surface_get_config_ptr(void) {
    if (g_water_params_dirty != 0) {
        return &g_water_params_pending;
    }
    return &g_water_level_config;
}

// =============================================================================
// Address: 0x800f363c | Name: water_surface_set_active
// Sets the water surface active state and configures collision detection.
// Selects config pointer based on params state. If a water entity exists with
// a collision sub-object, sets layer mask to 6 (collidable) when active or
// 0 (non-collidable) when inactive, controlling whether physics queries
// interact with the water plane.
// =============================================================================
void water_surface_set_active(int active) {
    void* config = NULL;

    if (g_water_params_dirty == 0 || g_water_param_scale == 0) {
        if (active != 0) {
            config = &g_water_level_config;
        }
    } else {
        config = &g_water_params_pending;
    }

    g_water_pending_type = active;

    // Update collision layer mask on water entity
    if (config != NULL && g_water_camera_override != 0) {
        int entity = *(int*)(g_water_camera_override + 0xe04);
        if (entity != 0) {
            int* collision_obj = *(int**)(entity + 4);
            if (collision_obj != NULL) {
                if (active == 0) {
                    collision_set_layer_mask(collision_obj, 0);
                } else {
                    collision_set_layer_mask(collision_obj, 6);
                }
            }
        }
    }
}
