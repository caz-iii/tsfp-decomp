// Address: 0x800f1568 | Name: weather_rain_init_active | Date: 2026-08-25
// Address: 0x800f1590 | Name: weather_system_preload | Date: 2026-08-25
// Address: 0x800f16c8 | Name: weather_system_spawn | Date: 2026-08-25
// Address: 0x800f1768 | Name: weather_system_alloc_and_init | Date: 2026-08-25
// Address: 0x800f17cc | Name: weather_system_shutdown | Date: 2026-08-25
// Address: 0x800f1808 | Name: weather_config_reset | Date: 2026-08-25
// Address: 0x800f186c | Name: weather_system_tick | Date: 2026-08-25
// Address: 0x800f190c | Name: weather_lightning_render_dispatch | Date: 2026-08-25
// Address: 0x800f1948 | Name: weather_system_render | Date: 2026-08-25
// Address: 0x800f19ec | Name: weather_post_render_and_camera | Date: 2026-08-25
//
// Weather system top-level dispatchers. This is the public API the rest of the
// engine uses to interact with the weather system. Handles initialization,
// per-frame updates, rendering, and shutdown for all weather types:
//   Type 1: Snow
//   Type 2: Thunderstorm (snow + lightning)
//   Type 3: Rain
//   Type 5: Particle emitter (leaves, debris)

#include "common.h"

// Forward declarations - subsystem init/shutdown
extern void snow_init(void);
extern void snow_tick(void);
extern void snow_shutdown(void);
extern void snow_setup_and_apply(void);
extern void snow_render_main(void);
extern void rain_init(void);
extern void rain_tick(void);
extern void weather_shutdown(void);
extern void weather_setup_and_apply(void);
extern void weather_render_main(void);
extern void rain_preload_sprites(void);
extern void weather_preload_sprites(void);
extern int  level_get_info_cached(int level_id);
extern void weather_wind_advance_phase(void);
extern void* weather_particle_get_current_ptr(void);

// Forward declarations - emitter subsystem (from weather_emitter_system.cpp)
extern void weather_emitter_preload_resources(void);
extern void weather_emitter_spawn_entities(void);
extern void weather_emitter_tick(void);
extern void weather_trail_system_alloc(void);
extern void weather_trail_render_billboards(void);
extern void weather_stub_destroy(void);
extern void weather_stub_render(void);

// Forward declarations - lightning (from weather_lightning.cpp)
extern void lightning_flash_timer_tick(void);
extern void lightning_bolt_render(void);
extern void lightning_flash_screen_overlay(void);

// Forward declarations - effects
extern void weather_emitter_opacity_update(void);
extern void weather_render_streak_callback(void);
extern void player_apply_camera_shake(float base, float scaled, float lateral, int player);
extern void player_set_eye_position_and_update(float x, float y, float z, int player);
extern void nav_advanced_movement_dispatch(float height, float radius, int entity,
                                           float* pos, float* target, int* result, int a, int b);

// Weather type active flags
static int g_snow_active;       // DAT_80611eb4 (types 1, 2)
static int g_rain_active;       // DAT_80611eb0 (type 3)
static int g_emitter_active;    // DAT_80611ea4 (type 5)
static int g_wind_push_active;  // DAT_80611ea8 (camera shake / wind push)
static int g_weather_flag_ac;   // DAT_80611eac (unused counter)

// Weather config
static int   g_weather_type_count;  // DAT_805709e8
static void* g_weather_config_ptr;  // DAT_805709ec
static int   g_weather_state_a;     // DAT_805709f0
static int   g_weather_state_b;     // DAT_805709f4
static float g_weather_state_f;     // DAT_805709f8
static float g_weather_fog_r;       // DAT_805709fc
static float g_weather_fog_g;       // DAT_80570a00
static float g_weather_fog_b;       // DAT_80570a04
static float g_weather_time_accum;  // DAT_80570a08

// Per-emitter slot data
extern int   g_weather_emitter_counters[];  // DAT_80570a14
extern float g_weather_speed_table[];       // DAT_80570a2c
extern float g_weather_opacity_table[];     // DAT_80570a44

// Player/camera state
extern int* g_player_data_ptr; // DAT_80611d74

// Level ID
extern int g_current_level_id; // DAT_80612614

// ============================================================================
// weather_rain_init_active
// Address: 0x800f1568
//
// Sets rain-type weather active flag and preloads rain sprites.
// Registered as callback for rain weather activation.
// ============================================================================
void weather_rain_init_active(void) {
    g_rain_active = 1;
    weather_preload_sprites();
}

// ============================================================================
// weather_system_preload
// Address: 0x800f1590
//
// Main weather preload dispatcher. Reads level definition to determine which
// weather types are present. Sets per-type active flags and preloads resources.
//
// Weather type mapping from level data:
//   1 -> snow (g_snow_active)
//   2 -> thunderstorm/snow (g_snow_active)
//   3 -> rain (g_rain_active)
//   5 -> particle emitter (g_emitter_active)
//
// Split-screen override: disables snow on certain levels when >1 player.
// ============================================================================
void weather_system_preload(void) {
    int level_info;
    int i;
    int offset;

    level_info = level_get_info_cached(g_current_level_id);

    // Clear all flags
    g_snow_active = 0;
    g_rain_active = 0;
    g_wind_push_active = 0;
    g_emitter_active = 0;

    if (level_info == 0) return;

    // Iterate weather definitions in level data (stride 0x28, count at +0x98)
    offset = 0;
    int count = *(int*)(level_info + 0x98);
    int* type_array = *(int**)(level_info + 0x94);

    for (i = 0; i < count; i++) {
        int weather_type = *(int*)((int)type_array + offset);

        switch (weather_type) {
            case 1: // snow
            case 2: // thunderstorm
                g_snow_active = 1;
                break;
            case 3: // rain
                g_rain_active = 1;
                break;
            case 5: // particle emitter
                g_emitter_active = 1;
                break;
        }
        offset += 0x28;
    }

    // Split-screen override: disable snow on specific level/mode combos
    if (((*(unsigned int*)0x805015c0 & 0x100000) == 0) &&
        (*(char*)0x805015bd == '\n') &&
        (*(int*)0x80612610 > 1)) {
        g_snow_active = 0;
    }

    g_weather_flag_ac = 0;

    // Preload resources for each active type
    if (g_snow_active != 0) {
        rain_preload_sprites();
    }
    if (g_rain_active != 0) {
        weather_preload_sprites();
    }
    if (g_emitter_active != 0) {
        weather_emitter_preload_resources();
    }
}

// ============================================================================
// weather_system_spawn
// Address: 0x800f16c8
//
// Initializes all active weather subsystems. Resets config to defaults,
// clears per-slot counters/speed/opacity tables (6 slots), then calls
// subsystem init for each active type.
// ============================================================================
void weather_system_spawn(void) {
    weather_config_reset(0, NULL);

    // Clear per-slot state (6 emitter slots)
    float zero = 0.0f;
    g_weather_fog_extra[0] = zero; // DAT_80570a5c
    g_weather_fog_extra[1] = zero; // DAT_80570a60
    g_weather_fog_extra[2] = zero; // DAT_80570a64

    for (int i = 0; i < 6; i++) {
        g_weather_emitter_counters[i] = 0;
        g_weather_speed_table[i] = zero;
        g_weather_opacity_table[i] = zero;
    }

    // Initialize active subsystems
    if (g_snow_active != 0) {
        snow_init();
    }
    if (g_rain_active != 0) {
        rain_init();
    }
    if (g_emitter_active != 0) {
        weather_emitter_spawn_entities();
    }
}

// ============================================================================
// weather_system_alloc_and_init
// Address: 0x800f1768
//
// Full weather system allocation and initialization. Allocates scratch memory,
// calls setup/apply for each active type, then spawns everything.
// Called during level loading.
// ============================================================================
void weather_system_alloc_and_init(void) {
    // Allocate and clear 0x74-byte scratch buffer
    char scratch[0x74];
    memset(scratch, 0, 0x74); // memset_wrapper

    // Setup/allocate per-type
    if (g_snow_active != 0) {
        snow_setup_and_apply();
    }
    if (g_rain_active != 0) {
        weather_setup_and_apply();
    }
    if (g_emitter_active != 0) {
        weather_trail_system_alloc();
    }

    // Spawn all weather entities
    weather_system_spawn();
}

// ============================================================================
// weather_system_shutdown
// Address: 0x800f17cc
//
// Shuts down active weather subsystems. Called during level unload.
// ============================================================================
void weather_system_shutdown(void) {
    if (g_snow_active != 0) {
        snow_shutdown();
    }
    if (g_rain_active != 0) {
        weather_shutdown();
    }
}

// ============================================================================
// weather_config_reset
// Address: 0x800f1808
//
// Resets weather configuration. Sets type count and config pointer.
// If param_1 == 0, uses default config table (DAT_8040c0c8) with count=1.
// Initializes fog color to default, clears time accumulator and state flags.
//
// param_1: number of weather types (0 = use default)
// param_2: pointer to weather config data (NULL = use default)
// ============================================================================
void weather_config_reset(int type_count, void* config_ptr) {
    g_weather_type_count = type_count;
    g_weather_config_ptr = config_ptr;

    if (type_count == 0) {
        g_weather_type_count = 1;
        g_weather_config_ptr = (void*)0x8040c0c8; // default config table
    }

    // Initialize fog color to default
    float default_fog = 0.75f; // FLOAT_80615204
    g_weather_fog_r = default_fog;
    g_weather_fog_g = default_fog;
    g_weather_fog_b = default_fog;

    // Clear state
    g_weather_time_accum = 0.0f;
    g_weather_state_a = 0;
    g_weather_state_b = 0;
    g_weather_state_f = 0.0f;
}

// ============================================================================
// weather_system_tick
// Address: 0x800f186c
//
// Per-frame weather system update dispatcher. Advances wind phase, then
// dispatches to type-specific tick functions based on current weather type.
//
// Weather type dispatch:
//   1 (snow): snow_tick
//   2 (thunderstorm): snow_tick + lightning_flash_timer_tick
//   3 (rain): rain_tick
//   5 (emitter): weather_emitter_tick
// ============================================================================
void weather_system_tick(void) {
    int* weather_type = (int*)weather_particle_get_current_ptr();

    weather_wind_advance_phase();

    if (g_snow_active != 0) {
        if (*weather_type == 1 || *weather_type == 2) {
            snow_tick();
        }
        if (*weather_type == 2) {
            lightning_flash_timer_tick();
        }
    }

    if (g_rain_active != 0 && *weather_type == 3) {
        rain_tick();
    }

    if (g_emitter_active != 0 && *weather_type == 5) {
        weather_emitter_tick();
    }

    weather_stub_render();
}

// ============================================================================
// weather_lightning_render_dispatch
// Address: 0x800f190c
//
// Lightning bolt render dispatcher. Only renders if weather type is 2
// (thunderstorm) and the snow/storm system is active.
// Called by scene_render_pipeline.
// ============================================================================
void weather_lightning_render_dispatch(void) {
    int* weather_type = (int*)weather_particle_get_current_ptr();

    if (*weather_type == 2 && g_snow_active != 0) {
        lightning_bolt_render();
    }
}

// ============================================================================
// weather_system_render
// Address: 0x800f1948
//
// Main weather render dispatcher. Sets GX texture filter and dispatches
// to type-specific renderers.
//
// Weather type dispatch:
//   1/2 (snow/storm): snow_render_main
//   3 (rain): weather_render_main
//   5 (emitter): weather_trail_render_billboards
// ============================================================================
void weather_system_render(void) {
    int* weather_type = (int*)weather_particle_get_current_ptr();

    int prev_filter = gx_set_texture_filter(2); // FUN_8016e8ac

    if (g_snow_active != 0 && (*weather_type == 1 || *weather_type == 2)) {
        snow_render_main();
    }

    if (g_rain_active != 0 && *weather_type == 3) {
        weather_render_main();
    }

    if (g_emitter_active != 0 && *weather_type == 5) {
        weather_trail_render_billboards();
    }

    gx_set_texture_filter(prev_filter);
}

// ============================================================================
// weather_post_render_and_camera
// Address: 0x800f19ec
//
// Post-render pass for weather overlays and camera effects.
// Dispatches overlay effects per type:
//   1/2: weather_emitter_opacity_update + lightning_flash_screen_overlay
//   3: weather_render_streak_callback
//   5: weather_stub_destroy (no-op)
//
// Then handles wind-driven camera shake and player push movement for
// thunderstorm (type 2): applies directional camera shake based on wind
// direction dot player facing, and uses nav_advanced_movement_dispatch to
// physically push the player in the wind direction.
// ============================================================================
void weather_post_render_and_camera(void) {
    int* weather_type = (int*)weather_particle_get_current_ptr();

    int prev_filter = gx_set_texture_filter(2);

    // Overlay effects
    if (g_snow_active != 0) {
        if (*weather_type == 1 || *weather_type == 2) {
            weather_emitter_opacity_update();
        }
        if (*weather_type == 2) {
            lightning_flash_screen_overlay();
        }
    }

    if (g_rain_active != 0 && *weather_type == 3) {
        weather_render_streak_callback();
    }

    if (g_emitter_active != 0 && *weather_type == 5) {
        weather_stub_destroy();
    }

    // Wind push + camera shake (when active and player visible)
    if (g_wind_push_active != 0 &&
        (*(unsigned int*)(g_player_data_ptr[2] + 0x21C) & 0x80) == 0) {

        // Compute wind direction from config table
        float wind_dir[3];
        wind_dir[0] = *(float*)0x803cc8c4; // DAT_803cc8c4
        wind_dir[1] = *(float*)0x803cc8c8; // DAT_803cc8c8
        wind_dir[2] = *(float*)0x803cc8cc; // DAT_803cc8cc

        // Dot product: wind direction vs player facing
        float dot = *(float*)(g_weather_emitter_state_ptr + 0x334) * wind_dir[0] +
                    *(float*)(g_weather_emitter_state_ptr + 0x338) * wind_dir[1] +
                    *(float*)(g_weather_emitter_state_ptr + 0x33C) * wind_dir[2];

        // Clamp to [0, inf)
        float strength = (dot > 0.0f) ? dot : 0.0f;

        // Apply camera shake
        if (g_player_data_ptr != NULL) {
            float shake_base = 0.25f;    // FLOAT_80615238
            float shake_scaled = shake_base + strength * 1.5f; // FLOAT_806151f0
            player_apply_camera_shake(
                shake_base,
                2.0f * shake_scaled, // FLOAT_806151d4
                shake_base * shake_scaled,
                (int)g_player_data_ptr
            );
        }

        // Wind push player movement (thunderstorm type 2 with speed > 0)
        if (g_player_data_ptr[0x45] == 2 && g_weather_speed_scale != 0) {
            float push_dir[3];
            push_dir[0] = *(float*)0x803cc8d0;
            push_dir[1] = *(float*)0x803cc8d4;
            push_dir[2] = *(float*)0x803cc8d8;

            // Accumulate rotation phase
            float rnd = randf();
            float frame_dt = (1.0f / g_framerate_float) * rnd;
            g_wind_push_phase += 2.0f * (float)g_weather_speed_scale *
                                 (1.0f / g_framerate_float) * frame_dt;

            // Build rotation matrix from phase
            float phase_frac = g_wind_push_phase - (float)(int)g_wind_push_phase;
            float rot_matrix[17]; // 4x4 + extra
            build_rotation_y_matrix(0.125f * (phase_frac - 2.0f) * 2.0f, rot_matrix);
            transform_direction(rot_matrix, push_dir);

            // Compute push target position
            float player_pos[3];
            player_pos[0] = *(float*)&g_player_data_ptr[0x2A];
            player_pos[1] = *(float*)&g_player_data_ptr[0x2B];
            player_pos[2] = *(float*)&g_player_data_ptr[0x2C];

            float push_strength = fabsf(0.2f * // FLOAT_806151ec
                (push_dir[2] * (player_pos[2] - *(float*)&g_player_data_ptr[0x55]) +
                 push_dir[0] * (player_pos[0] - *(float*)&g_player_data_ptr[0x53]) +
                 push_dir[1] * (player_pos[1] - *(float*)&g_player_data_ptr[0x54])));

            float target[3];
            target[0] = push_dir[0] * push_strength + player_pos[0];
            target[1] = push_dir[1] * push_strength + player_pos[1];
            target[2] = push_dir[2] * push_strength + player_pos[2];

            // Move player via nav system
            float nav_height = *(float*)&g_player_data_ptr[0x25] +
                               *(float*)&g_player_data_ptr[0x23];
            float nav_radius = *(float*)&g_player_data_ptr[0x28];

            nav_advanced_movement_dispatch(
                nav_height, nav_radius,
                *g_player_data_ptr, // entity ID
                (float*)&g_player_data_ptr[0x2A], // current pos
                target, NULL, 0, -3
            );

            // Accept position if Y displacement is small
            if (fabsf(target[1] - player_pos[1]) < 0.25f) {
                // Save previous position
                g_player_data_ptr[0x53] = g_player_data_ptr[0x2A];
                g_player_data_ptr[0x54] = g_player_data_ptr[0x2B];
                g_player_data_ptr[0x55] = g_player_data_ptr[0x2C];
                g_player_data_ptr[0x56] = g_player_data_ptr[0x2D];
                g_player_data_ptr[0x57] = g_player_data_ptr[0x2E];
                g_player_data_ptr[0x58] = g_player_data_ptr[0x2F];

                // Apply new position
                *(float*)&g_player_data_ptr[0x2A] = target[0];
                *(float*)&g_player_data_ptr[0x2B] = target[1];
                *(float*)&g_player_data_ptr[0x2C] = target[2];

                // Update eye position
                player_set_eye_position_and_update(
                    *(float*)&g_player_data_ptr[0x2A],
                    *(float*)&g_player_data_ptr[0x26] +
                    *(float*)&g_player_data_ptr[0x2B] +
                    *(float*)&g_player_data_ptr[0x24],
                    *(float*)&g_player_data_ptr[0x2C],
                    (int)g_player_data_ptr
                );
            }
        }
    }

    gx_set_texture_filter(prev_filter);
}

// ============================================================================
// weather_get_current_type
// Address: 0x800f1ee8
//
// Returns the current weather type ID from the particle state pointer.
// Simple accessor wrapper.
// ============================================================================
int weather_get_current_type(void) {
    int* type_ptr = (int*)weather_particle_get_current_ptr();
    return *type_ptr;
}

// ============================================================================
// weather_set_wind_push_active
// Address: 0x800f1f0c
//
// Sets the wind push active flag. When nonzero, weather_post_render_and_camera
// applies camera shake and player push movement.
// ============================================================================
void weather_set_wind_push_active(int active) {
    g_wind_push_active = active; // DAT_80611ea8
}
