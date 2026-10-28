// Address: 0x800ee5d0 - 0x800ef890 | Weather Trail/Splash System | Date: 2026-08-25
// Batch 297: Weather trail emitters, ground splashes, and streak management
//
// This module implements the per-player trail particle system that creates
// rain streaks and snow trails relative to each player's camera. Each player
// gets a set of emitters (3 for rain, 4 for snow) that track camera-relative
// motion plus a pool of streak particles (50 for rain, 32 for blizzard).
//
// Trail emitter struct layout (0x30 = 48 bytes):
//   +0x00  float pos_x          (normalized 0..1 position)
//   +0x04  float pos_y
//   +0x08  float pos_z
//   +0x0C  float vel_x          (jitter velocity)
//   +0x10  float vel_y
//   +0x14  float vel_z
//   +0x18  float speed          (fall speed)
//   +0x1C  float lifetime       (countdown timer)
//   +0x20  float jitter_x
//   +0x24  float jitter_y
//   +0x28  float jitter_z
//   +0x2C  float opacity_phase  (animation phase for ground splash)
//   +0x30  float visibility     (0..1, final alpha multiplier)
//
// Streak particle struct layout (0x28 = 40 bytes):
//   +0x00  float pos_x
//   +0x04  float pos_y
//   +0x08  float age            (countdown to respawn)
//   +0x0C  float fall_speed
//   +0x10..+0x1C misc
//   +0x20  float delay          (initial delay before activation)
//   +0x24  int   flags
//
// Key globals:
//   g_weather_trail_buffer  (0x80570a0c) - emitter array (0x480 bytes total)
//   g_weather_streak_buffer (0x80570a10) - streak particle array
//   g_weather_state_init    (0x80570a14) - per-player init flags (6 entries)
//   g_weather_intensity     (0x80570a2c) - per-player intensity ramp (6 entries)
//   g_weather_indoor_factor (0x80570a44) - per-player indoor/outdoor factor
//   g_player_count          (0x80611d58) - active player count
//   g_player_data_base      (0x80611d5c) - player data array (0xFFC per player)
//   g_wind_direction_rain   (0x803cc894) - rain wind direction (3 floats)
//   g_wind_direction_snow   (0x803cc8a0) - snow wind direction (6 floats)
//   g_weather_effect_table  (0x8040bfb8) - 4-entry effect table (0x44 bytes each)
//   g_weather_blizzard_flag (0x80611eb0) - nonzero = blizzard mode

#include <math.h>

// Forward declarations
extern void* mem_alloc(/* 16 params */);
extern void  memset_wrapper(void* dst, int val, int size);
extern float rand_float(void);                    // 0x801310c0
extern float acos_float(float x);                 // 0x801314f4
extern void  matrix_transform_3x3(float* mtx, float* vec); // 0x8012fb90
extern void  matrix_build_identity_scaled(float scale, float* out); // 0x8012f05c
extern int   weather_get_type(void);              // 0x800f1ee8
extern int   weather_check_player_outdoors(float* pos, int mode, float* dir, int ref); // 0x800f1dbc
extern void  weather_streak_reset(void);          // 0x800ef8d8
extern void  weather_render_streaks_main(void);   // 0x800efa58
extern void* particle_get_current_player_emitter(void);
extern int   sprite_get_texture_id(int id);
extern void  render_set_fog_offset(float offset); // 0x803825c0
extern void  render_submit_splash_quad(/* params */); // 0x80171f7c

// Globals
extern int   g_weather_trail_buffer;     // 0x80570a0c
extern int   g_weather_streak_buffer;    // 0x80570a10
extern int   g_weather_state_init[];     // 0x80570a14 (6 entries)
extern float g_weather_intensity[];      // 0x80570a2c (6 entries)
extern float g_weather_indoor_factor[];  // 0x80570a44 (6 entries)
extern int   g_player_count;             // 0x80611d58
extern int   g_player_data_base;         // 0x80611d5c
extern int   g_camera_data;              // 0x80611d54
extern int   g_weather_room_grid;        // 0x80611e88
extern int   g_weather_blizzard_flag;    // 0x80611eb0
extern float g_weather_wind_accel_x;     // 0x80570a5c
extern float g_weather_wind_accel_y;     // 0x80570a60
extern float g_weather_wind_accel_z;     // 0x80570a64
extern float g_frame_dt_scaled;          // 0x806108ec
extern int   g_frame_counter_raw;        // 0x806109c0
extern int   g_render_enabled;           // 0x806108e4

// ============================================================================
// Address: 0x800ee5d0
// Per-frame weather emitter opacity update. Adjusts the player particle
// emitter's alpha based on camera altitude and per-camera weather
// intensity/indoor factors. Accounts for indoor flag reducing opacity.
// ============================================================================
void weather_emitter_opacity_update(void)
{
    int camera_index = *(int*)(g_camera_data + 0x214);

    // If weather state active and render flag not set, render streaks
    if (g_weather_state_init[camera_index] != 0 && (*(unsigned int*)0x805015c0 & 0x20000000) == 0) {
        weather_render_streaks_main();
    }

    // Set depth mode
    // FUN_8016f00c(1)

    // Get current player particle emitter
    int* emitter = (int*)particle_get_current_player_emitter();
    emitter[0x12] = 1; // enable flag

    // Compute opacity from camera height * intensity - indoor offset
    float cam_height = *(float*)(g_camera_data + 0x3B4);
    float intensity = g_weather_intensity[camera_index];
    float indoor = g_weather_indoor_factor[camera_index];

    // opacity = -(height_scale * cam_height * intensity -
    //            -(indoor_scale * indoor * cam_height - (base * indoor + mid * intensity)))
    float opacity = -((0.5f * cam_height * intensity) -
                     -(0.25f * indoor * cam_height - (0.5f * indoor + 0.75f * intensity)));

    // If indoor render flag set, reduce further
    if ((*(unsigned int*)0x805015c0 & 0x20000000) != 0) {
        opacity *= 1.0f; // FLOAT_806151f4 multiplier
    }

    // Apply to emitter with lerp
    emitter = (int*)particle_get_current_player_emitter();
    float prev_alpha = *(float*)&emitter[4]; // current alpha as float
    emitter[4] = (int)(0.5f * opacity + prev_alpha); // FLOAT_80615250 * opacity + prev
}

// ============================================================================
// Address: 0x800ee700
// Clears the weather trail emitter buffer (0x480 = 1152 bytes).
// Called during init and reset.
// ============================================================================
void weather_trail_buffer_clear(void)
{
    memset_wrapper((void*)g_weather_trail_buffer, 0, 0x480);
}

// ============================================================================
// Address: 0x800ee734
// Allocates the weather trail emitter buffer (0x480 bytes) and clears it.
// Called during weather system initialization.
// ============================================================================
void weather_trail_buffer_alloc(/* 16 params */)
{
    g_weather_trail_buffer = (int)mem_alloc(/* params, */ 0x480, 1 /*, ...*/);
    weather_trail_buffer_clear();
}

// ============================================================================
// Address: 0x800ee76c
// Initializes a single trail emitter with randomized state.
// param_1 = emitter pointer (0x30 bytes)
// param_2 = emitter index (used for vertical stratification)
// ============================================================================
void weather_trail_particle_init(float* emitter, unsigned int index)
{
    emitter[4] = 0.0f; // opacity_phase = 0

    // Random normalized XY position
    emitter[0] = rand_float();
    emitter[1] = rand_float();

    // Z position: random within vertical band based on index (index/4 stratification)
    int band = (int)index / 4;
    emitter[2] = rand_float() * 0.5f + (float)band; // FLOAT_80615280 * rand + band

    // Speed based on weather type
    int weather_type = weather_get_type();
    if (weather_type == 3) {
        // Blizzard: slower, larger range
        emitter[6] = rand_float() * 0.75f + 0.75f; // FLOAT_80615294 * rand + FLOAT_80615294
    } else {
        // Normal rain/snow
        emitter[6] = rand_float() * 1.0f + 1.0f; // FLOAT_80615298 * rand + FLOAT_80615298
    }

    // Visibility = 1.0 (fully visible initially)
    emitter[0xB] = 1.0f;

    // Lifetime: random in range [base, base+range]
    emitter[5] = rand_float() * 0.5f + 0.5f; // FLOAT_806152a0 * rand + FLOAT_8061529c

    // Jitter velocities: random centered around 0
    emitter[7] = 0.25f * (rand_float() - 1.0f); // FLOAT_806152a4 * (rand - 1.0)
    emitter[8] = 0.25f * (rand_float() - 1.0f);
    emitter[9] = 0.25f * (rand_float() - 1.0f);

    // Phase counter = 0
    emitter[10] = 0.0f;
}

// ============================================================================
// Address: 0x800ee888
// Updates a single trail emitter per frame. Applies camera-relative
// velocity with wind, wraps position within [0,1] bounds, applies
// occlusion based on camera look direction, and edge-fading for
// particles near vertical/horizontal boundaries.
// ============================================================================
void weather_trail_particle_update(int player_index, int unused, float* velocity)
{
    // Begin render context
    // FUN_803826ec()
    int player_data = g_player_data_base + player_index * 0xFFC; // NOTE: actually from iterator
    float* emitter; // from iterator return

    // Get player's inverse view matrix and transform velocity
    float* inv_view = *(float**)(player_data + 0x208);
    // matrix_transform_3x3(inv_view, velocity)

    // Frame rate factor
    float dt_factor = 0.5f / (float)g_frame_counter_raw; // FLOAT_806152a8 / frame_rate
    float frame_dt = g_frame_dt_scaled;

    // Apply camera-relative velocity to position (with frame correction)
    emitter[0] = -(dt_factor * velocity[0] * frame_dt - emitter[0]);
    emitter[1] = -(dt_factor * velocity[1] * frame_dt - emitter[1]);
    emitter[2] = -(dt_factor * 1.0f * velocity[2] * frame_dt - emitter[2]);

    // Reset visibility
    emitter[0xB] = 1.0f;

    // Wrap opacity phase
    if (emitter[10] < 0.0f) emitter[10] += 1.0f;
    if (emitter[10] >= 1.0f) emitter[10] -= 1.0f;

    // Camera occlusion: if camera has look-direction flag (0x400)
    if ((*(unsigned int*)(player_data + 0x21C) & 0x400) != 0) {
        float look_dir[3];
        look_dir[0] = *(float*)(player_data + 0x334);
        look_dir[1] = *(float*)(player_data + 0x338);
        look_dir[2] = *(float*)(player_data + 0x33C);

        // Build rotation matrix and transform look direction
        float rot_mtx[16];
        // matrix_build_identity_scaled(FLOAT_806152ac, rot_mtx)
        // matrix_transform_3x3(rot_mtx, look_dir)

        // Compute dot product with camera forward
        float dot = *(float*)(player_data + 0x340) * *(float*)(player_data + 0x334) +
                    *(float*)(player_data + 0x344) * *(float*)(player_data + 0x338) +
                    *(float*)(player_data + 0x348) * *(float*)(player_data + 0x33C);

        // Clamp dot to [-1, 1]
        if (dot > 1.0f) dot = 1.0f;
        if (dot < -1.0f) dot = -1.0f;

        // Compute angle-based occlusion
        float angle = acos_float(dot);
        emitter[0xB] = 1.0f - (angle / 3.14159f); // FLOAT_806152b0

        if (emitter[0xB] < 0.0f) emitter[0xB] = 0.0f;

        // Apply signed direction offset to position
        float signed_dot = *(float*)(player_data + 0x348) * look_dir[2] +
                          *(float*)(player_data + 0x340) * look_dir[0] +
                          *(float*)(player_data + 0x344) * look_dir[1];
        if (signed_dot < 0.0f) angle = -angle;
        emitter[0] += angle;
    }

    // Apply per-player intensity squared to visibility
    emitter[0xB] *= g_weather_intensity[player_index] * g_weather_intensity[player_index];

    // Wrap positions to [0, 1)
    if (emitter[0] < 0.0f) emitter[0] += 1.0f;
    if (emitter[0] >= 1.0f) emitter[0] -= 1.0f;
    if (emitter[1] < 0.0f) emitter[1] += 1.0f;
    if (emitter[1] >= 1.0f) emitter[1] -= 1.0f;
    if (emitter[2] < 0.0f) emitter[2] += 1.0f;
    if (emitter[2] >= 1.0f) emitter[2] -= 1.0f;

    // Vertical edge fading (near bottom)
    if (emitter[2] < 0.25f) { // FLOAT_806151f0
        emitter[0xB] *= 4.0f * emitter[2]; // FLOAT_806152b4 * z
    }
    // Vertical edge fading (near top)
    if (emitter[2] > 0.75f) { // FLOAT_806152b8
        emitter[0xB] *= 4.0f * (1.0f - emitter[2]);
    }

    // Horizontal lifetime fading (near start)
    if (emitter[4] < 0.5f) { // FLOAT_80615280
        emitter[0xB] *= 2.0f * emitter[4]; // FLOAT_806152bc * phase
    }
    // Horizontal lifetime fading (near end)
    if (emitter[4] > 0.75f) { // FLOAT_806152c0
        emitter[0xB] *= 2.0f * (1.0f - emitter[4]);
    }

    // End render context
    // FUN_80382738()
}

// ============================================================================
// Address: 0x800eec34
// Rain trail spawner. Per-player main loop: manages intensity ramp-up/down
// based on whether player is outdoors and has portals visible, initializes
// 3 trail emitters on first frame, updates emitters with wind velocity,
// and manages 50 rain streak particles (respawn, wind-push, age).
// Called every frame from rain_tick.
// ============================================================================
void rain_trail_spawner(void)
{
    // FUN_803826c8() - begin iterator

    int* weather_type = (int*)weather_particle_get_current_ptr();
    int streaks_per_player = 50; // 0x32
    if (*weather_type == 3) streaks_per_player = 32; // 0x20 for blizzard

    float dt = g_frame_dt_scaled * (1.0f / (float)g_frame_counter_raw);
    float dt_scaled = 1.0f * dt;

    for (int p = 0; p < g_player_count; p++) {
        int player_data = g_player_data_base + p * 0xFFC;

        // Only process players with active + outdoor flags
        if (!((*(unsigned int*)(player_data + 0x21C) & 0x80000000) &&
              (*(unsigned int*)(player_data + 0x21C) & 0x400)))
            continue;

        // Check if player is outdoors
        int outdoors = weather_check_player_outdoors(
            (float*)(player_data + 0x27C), 1,
            (float*)(player_data + 0x2A0), *(int*)0x80611d74
        );

        if (outdoors == 0) {
            // Indoors: check if any visible room has portals
            int has_portals = 0;
            int visible_count = (int)*(short*)(player_data + 0xE0A);
            for (int r = 0; r < visible_count; r++) {
                int room_idx = *(short*)(player_data + r * 2 + 0xE0C);
                if (*(int*)(g_weather_room_grid + (room_idx - 1) * 0x3C + 0x34) > 0) {
                    has_portals = 1;
                }
            }

            // Ramp down intensity
            g_weather_intensity[p] -= dt_scaled;
            if (g_weather_intensity[p] < 0.0f) g_weather_intensity[p] = 0.0f;

            // Indoor factor ramps up if portals visible, down otherwise
            if (has_portals) {
                g_weather_indoor_factor[p] += dt_scaled;
                if (g_weather_indoor_factor[p] > 1.0f) g_weather_indoor_factor[p] = 1.0f;
            } else {
                g_weather_indoor_factor[p] -= dt_scaled;
                if (g_weather_indoor_factor[p] < 0.0f) g_weather_indoor_factor[p] = 0.0f;
            }
        } else {
            // Outdoors: full intensity
            g_weather_intensity[p] = 1.0f;
            g_weather_indoor_factor[p] = 1.0f;
        }

        // Transform wind direction by player's inverse view
        float wind_local[3] = { g_weather_wind_accel_x, g_weather_wind_accel_y, g_weather_wind_accel_z };
        float* inv_view = *(float**)(player_data + 0x20C);
        // matrix_transform_3x3(inv_view, wind_local)

        // Initialize emitters on first frame
        if (g_weather_state_init[p] == 0) {
            for (int e = 0; e < 3; e++) {
                float* em = (float*)(g_weather_trail_buffer + (e + p * 4) * 0x30);
                weather_trail_particle_init(em, e);
                em[4] = rand_float(); // random starting phase
            }
            // Initialize streak particles
            for (int s = 0; s < streaks_per_player; s++) {
                int streak = g_weather_streak_buffer + (s + p * streaks_per_player) * 0x28;
                weather_streak_reset();
                *(float*)(streak + 8) = rand_float(); // random starting age
            }
            g_weather_state_init[p] = 1;
        }

        // Update 3 trail emitters
        for (int e = 0; e < 3; e++) {
            float* em = (float*)(g_weather_trail_buffer + (e + p * 4) * 0x30);
            em[4] += dt * em[6]; // advance phase by speed

            if (em[4] <= 1.0f) {
                // Still alive: compute velocity and update
                if (g_render_enabled > 0) {
                    float vel[3];
                    // Compute velocity from wind + jitter
                    vel[0] = -(0.5f * wind_local[0] - (g_weather_wind_accel_x * em[6] + em[7]));
                    vel[1] = -(0.5f * wind_local[1] - (g_weather_wind_accel_y * em[6] + em[8]));
                    vel[2] = -(0.5f * wind_local[2] - (g_weather_wind_accel_z * em[6] + em[9]));

                    // Advance phase counter
                    em[10] = -(1.0f * dt - em[10]);

                    weather_trail_particle_update(p, (int)em, vel);
                }
            } else {
                // Dead: respawn
                weather_trail_particle_init(em, e);
                em[0xB] *= g_weather_intensity[p] * g_weather_intensity[p];
            }
        }

        // Update streak particles
        for (int s = 0; s < streaks_per_player; s++) {
            float* streak = (float*)(g_weather_streak_buffer + (s + p * streaks_per_player) * 0x28);
            streak[2] += dt * streak[3]; // advance age by fall speed

            if (streak[2] < 1.0f) {
                // Alive: apply wind push and age
                if (g_render_enabled > 0 && *(int*)&streak[9] == 1 &&
                    (weather_get_type() == 1 || weather_get_type() == 2)) {
                    streak[8] -= dt;
                    if (streak[8] < 0.0f) {
                        // Apply wind push
                        float push = dt * (streak[3] * (1.0f + streak[8]));
                        streak[0] += 1.0f * wind_local[0] * push;
                        streak[1] += push;
                        if (streak[8] < -1.0f) {
                            streak[8] = 1.0f * rand_float(); // respawn delay
                        }
                    }
                }
            } else {
                // Dead: respawn streak
                weather_streak_reset();
            }
        }
    }

    // FUN_80382714() - end iterator
}

// ============================================================================
// Address: 0x800ef198
// Searches the 4-entry weather wind effect table for a matching entity ID.
// Table at 0x8040bfb8: 4 entries of 0x44 bytes, first 4 DWORDs are entity IDs.
// Returns pointer to matching entry or 0 if not found.
// ============================================================================
int weather_effect_table_lookup(int entity_id)
{
    for (int entry = 0; entry < 4; entry++) {
        int entry_base = 0x8040bfb8 + entry * 0x44;
        for (int slot = 0; slot < 4; slot++) {
            int stored_id = *(int*)(entry_base + slot * 4);
            if (stored_id != 0 && entity_id == stored_id) {
                return entry_base;
            }
        }
    }
    return 0;
}

// ============================================================================
// Address: 0x800ef1f4
// Snow trail spawner. Similar to rain_trail_spawner but uses 4 emitters
// per player and references the wind effect table for directional wind
// from nearby fan/vent entities. Also applies effect-based visibility
// multiplier from the matched table entry.
// Called every frame from snow_tick (indirectly via FUN_800f0704).
// ============================================================================
void snow_trail_spawner(void)
{
    // FUN_803826d0() - begin iterator

    float dt = g_frame_dt_scaled * (1.0f / (float)g_frame_counter_raw);
    float dt_scaled = 1.0f * dt;

    for (int p = 0; p < g_player_count; p++) {
        int player_data = g_player_data_base + p * 0xFFC;

        // Look up wind effect table entry for this player's entity
        int effect_entry = weather_effect_table_lookup(*(int*)(player_data + 0x2A0));

        // Only process active + outdoor players
        if (!((*(unsigned int*)(player_data + 0x21C) & 0x80000000) &&
              (*(unsigned int*)(player_data + 0x21C) & 0x400)))
            continue;

        // Outdoor check and intensity ramp (simpler than rain: no indoor_factor)
        int outdoors = weather_check_player_outdoors(
            (float*)(player_data + 0x27C), 1,
            (float*)(player_data + 0x2A0), *(int*)0x80611d74
        );

        if (outdoors == 0) {
            g_weather_intensity[p] -= dt_scaled;
            if (g_weather_intensity[p] < 0.0f) g_weather_intensity[p] = 0.0f;
        } else {
            g_weather_intensity[p] = 1.0f;
        }

        // Initialize 4 emitters on first frame
        if (g_weather_state_init[p] == 0) {
            for (int e = 0; e < 4; e++) {
                float* em = (float*)(g_weather_trail_buffer + (e + p * 4) * 0x30);
                weather_trail_particle_init(em, e);
                em[4] = rand_float();
            }
            g_weather_state_init[p] = 1;
        }

        // Update 4 trail emitters
        for (int e = 0; e < 4; e++) {
            float* em = (float*)(g_weather_trail_buffer + (e + p * 4) * 0x30);
            em[4] += dt * em[6]; // advance phase

            if (em[4] <= 1.0f) {
                if (g_render_enabled > 0) {
                    // Compute velocity: default from snow wind globals
                    float vel[3];
                    vel[0] = *(float*)0x803cc8a0; // snow wind X
                    vel[1] = *(float*)0x803cc8a4; // snow wind Y
                    vel[2] = *(float*)0x803cc8a8; // snow wind Z

                    float dir[3];
                    dir[0] = *(float*)0x803cc8ac; // snow direction X
                    dir[1] = *(float*)0x803cc8b0;
                    dir[2] = *(float*)0x803cc8b4;

                    // Override direction from camera if outdoor flag set
                    if ((*(unsigned int*)(player_data + 0x21C) & 0x400) != 0) {
                        dir[0] = *(float*)(player_data + 0x27C) - *(float*)(player_data + 0x288);
                        dir[1] = *(float*)(player_data + 0x280) - *(float*)(player_data + 0x28C);
                        dir[2] = *(float*)(player_data + 0x284) - *(float*)(player_data + 0x290);
                    }

                    // If wind effect table entry found, use its directional wind
                    if (effect_entry != 0) {
                        float effect_speed = em[6];
                        vel[0] = -(1.0f * dir[0] - (*(float*)(effect_entry + 0x24) * effect_speed + em[7]));
                        vel[1] = -(1.0f * dir[1] - (*(float*)(effect_entry + 0x28) * effect_speed + em[8]));
                        vel[2] = -(1.0f * dir[2] - (*(float*)(effect_entry + 0x2C) * effect_speed + em[9]));
                    }

                    // Advance phase counter
                    em[10] += 1.0f * dt;

                    weather_trail_particle_update(p, (int)em, vel);

                    // Apply effect table visibility multiplier
                    float vis_mult = 0.0f;
                    if (effect_entry != 0) vis_mult = *(float*)(effect_entry + 0x30);

                    if (vis_mult >= 0.5f) { // FLOAT_806152c4
                        // Use effect visibility directly
                        float final_vis = 0.0f;
                        if (effect_entry != 0) final_vis = *(float*)(effect_entry + 0x30);
                        em[0xB] *= final_vis;
                    } else {
                        em[0xB] *= vis_mult;
                    }
                }
            } else {
                // Dead: respawn
                weather_trail_particle_init(em, e);
                em[0xB] *= g_weather_intensity[p] * g_weather_intensity[p];
            }
        }
    }

    // FUN_8038271c() - end iterator
}

// ============================================================================
// Address: 0x800ef59c
// Renders weather ground splashes as textured quads. Iterates per-camera
// emitters, computes quad size/position from emitter state, and submits
// sprite 0x325 (splash texture) with alpha based on visibility.
// Uses a quartic falloff (1 - age^4) for splash intensity.
// ============================================================================
void weather_render_ground_splashes(void)
{
    // FUN_803826d4() - get emitter count
    int emitter_count; // from return

    // Bind splash texture (sprite ID 0x325)
    int texture_id = sprite_get_texture_id(0x325);
    int tex_data = *(int*)0x80610a20 + texture_id * 0x58;
    int tex_width = *(unsigned short*)(tex_data + 0x4E);
    int tex_height = *(unsigned short*)(tex_data + 0x52);

    // FUN_8016e77c(texture_id) - bind
    // FUN_8016e8ac(2) - set tex mode

    int camera_index = *(int*)(g_camera_data + 0x214);

    for (int i = 0; i < emitter_count; i++) {
        float* em = (float*)(g_weather_trail_buffer + (i + camera_index * 4) * 0x30);
        float age = 1.0f - em[2]; // distance from ground

        // Compute splash size: grows with age, offset by constants
        float size = 1.0f * (1.0f * em[2] + 0.5f); // FLOAT_806152cc * (FLOAT_806152d0 * age + FLOAT_80615264)

        // Quartic intensity falloff: -(fade * (age^4) - base)
        float age4 = age * age * age * age;
        float intensity = -(1.0f * age4 - 0.5f); // FLOAT_806151f8 * age^4 - FLOAT_806152c8

        // Compute alpha from visibility
        float alpha = (float)((int)(255.0f * em[0xB]) & 0xFF);
        int fog_result = render_set_fog_offset(alpha);

        if (fog_result != 0) {
            // Submit splash quad
            // render_submit_splash_quad(screen coords + tex coords + size)
        }
    }

    // FUN_80382720() - end iterator
}

// ============================================================================
// Address: 0x800ef890
// Allocates weather streak particle buffer. Size depends on whether
// blizzard mode is active: 50 streaks * players * 0xF0 normally,
// or 32 streaks * players * 0xF0 in blizzard mode.
// ============================================================================
void weather_streak_buffer_alloc(/* 16 params */)
{
    int streaks_per_player = 50; // 0x32
    if (g_weather_blizzard_flag != 0) {
        streaks_per_player = 32; // 0x20
    }

    // Allocate: streaks_per_player * total_players * 0xF0 bytes
    g_weather_streak_buffer = (int)mem_alloc(/* params, */ streaks_per_player * 0xF0, 1 /*, ...*/);
}
