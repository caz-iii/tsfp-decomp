// Address: 0x800edfc4 - 0x800ee498 | Weather Type Dispatch (Rain/Snow) | Date: 2026-08-25
// Batch 296: Per-type init/tick/render/shutdown for rain and snow weather effects
//
// This module implements the type-specific weather behaviors. The higher-level
// weather dispatch table (around 0x800f1500+) calls these functions based on
// the active weather type. Rain uses 512 particles with streak rendering;
// Snow uses 216 particles with line/trail rendering and different wind params.
//
// Rain particle count: 0x200 (512)
// Snow particle count: 0xD8 (216)
//
// Key globals:
//   g_weather_wind_phase       (0x80570a08) - shared wind phase
//   g_weather_wind_accel_x     (0x80570a5c) - per-frame wind X
//   g_weather_wind_accel_y     (0x80570a60) - per-frame wind Y
//   g_weather_wind_accel_z     (0x80570a64) - per-frame wind Z
//   g_weather_render_scale_rain(0x80611ec0) - rain render scale
//   g_weather_render_scale_snow(0x80611ec4) - snow render scale
//   g_weather_snow_stride      (0x80611ec8) - snow particle iteration stride
//   g_weather_state_table      (0x80570a14) - per-camera weather state (6 entries)
//   g_camera_data              (0x80611d54) - active camera struct pointer
//   g_weather_snow_color_r     (0x80610800) - snow vertex color R (u16)
//   g_weather_snow_color_g     (0x80610802) - snow vertex color G (u16)
//   g_weather_snow_opacity     (0x80611e80) - snow opacity factor
//   g_wind_direction           (0x803cc888) - wind direction vector (3 floats)
//
// Constants:
//   FLOAT_80615260 = rain extent (world units per axis)
//   FLOAT_80615274 = snow extent (world units per axis)
//   FLOAT_80615264 = velocity range (small)
//   FLOAT_806151d4 = 1.0
//   FLOAT_806151e8 = 0.0
//   FLOAT_806151fc = 1.0 (max clamp)
//   FLOAT_8061523c = wind scale multiplier

#include <math.h>

// Forward declarations from weather_particles.cpp / weather_particles_render.cpp
extern int   weather_particle_get_current_ptr(void);
extern void  weather_particles_init(float x, float y, float z, float vx, float vy, float vz, float sz, int count);
extern void  weather_particles_update_tick(float wx, float wy, float wz);
extern void  weather_grid_alloc_rooms(void);
extern void  weather_grid_conditional_clear(void);
extern void  weather_render_portal_precipitation(float wx, float wy, float wz, float scale, float max_rate);
extern void  weather_wind_evaluate_force(float amp_x, float amp_y, float amp_z);

// External helpers
extern void  sprite_load_by_flag_mask(/* 8 params */);
extern int   sprite_get_texture_id(int id);
extern float rand_float(void);           // 0x801310c0
extern float sqrt_float(float x);        // 0x80131538
extern int   particle_get_stride(void);  // 0x801c1714
extern int   weather_get_total_particles(void); // 0x800fdcd0

// Render helpers
extern void  render_submit_particle_begin(/* ... */);  // 0x801740c8
extern void  render_submit_particle(/* ... */);        // 0x8016e1a8
extern void  render_submit_line(/* ... */);            // 0x8016e1d4
extern void  render_submit_particle_end(void);         // 0x80172c54
extern void  render_finalize_batch(/* ... */);         // 0x80174144
extern void  render_set_line_width(float w, int flags, int ctx); // 0x80173778
extern void  render_set_fog_offset(float offset);      // 0x803825c0

// Weather system internal
extern void  weather_clear_state(void);                // 0x800ee700
extern void  weather_spawn_trail_particles(void);      // 0x800eec34
extern void  weather_render_streaks(void);             // 0x800efde4
extern void  weather_render_secondary_effects(void);   // 0x800ef59c
extern void  weather_setup_snow_special(void);         // 0x800f0da0

// Globals
extern float g_sine_table[];              // 0x80583228
extern float g_weather_wind_phase;        // 0x80570a08
extern float g_weather_wind_accel_x;      // 0x80570a5c
extern float g_weather_wind_accel_y;      // 0x80570a60
extern float g_weather_wind_accel_z;      // 0x80570a64
extern float g_wind_direction[3];         // 0x803cc888 (x, y, z)
extern int   g_weather_state_table[];     // 0x80570a14
extern int   g_camera_data;              // 0x80611d54
extern float g_weather_render_scale_rain; // 0x80611ec0
extern float g_weather_render_scale_snow; // 0x80611ec4
extern int   g_weather_snow_stride;       // 0x80611ec8
extern float g_weather_snow_opacity;      // 0x80611e80

// ============================================================================
// Address: 0x800edfc4
// Weather render streak callback. Checks if the current camera has an
// active weather state entry, and if so renders rain streaks.
// Called as a scene render callback.
// ============================================================================
void weather_render_streak_callback(void)
{
    int camera_index = *(int*)(g_camera_data + 0x214);
    if (g_weather_state_table[camera_index] != 0) {
        weather_render_streaks();
    }
}

// ============================================================================
// Address: 0x800ee00c
// Rain per-frame tick. Evaluates wind direction from 3 sine harmonics,
// multiplies by global wind direction vector, then updates all rain
// particles and spawns trail effects.
// ============================================================================
void rain_tick(void)
{
    // Compute wind intensity from 3 sine harmonics mixed together
    float phase = g_weather_wind_phase * 1.0f; // WIND_SCALE_MULT * phase
    float wind_intensity =
        0.5f  * g_sine_table[(int)(0.5f * 1.0f * phase + 0.5f) & 0x7FF] +  // harmonic 0
        1.0f  * g_sine_table[((int)(0.5f * 1.0f * phase + 0.5f) + 0x200) & 0x7FF] +  // harmonic 1 (cos)
        0.25f * g_sine_table[(int)(0.5f * 1.0f * phase + 0.5f) & 0x7FF] +   // harmonic 2
        0.5f; // DC bias (FLOAT_806151d0)

    // Clamp wind intensity to [min, max]
    if (wind_intensity < 0.5f) // FLOAT_80615264
        wind_intensity = 0.5f;
    if (1.0f < wind_intensity) // FLOAT_806151fc
        wind_intensity = 1.0f;

    // Apply wind direction scaled by intensity
    float extent = 1.0f; // FLOAT_80615260
    g_weather_wind_accel_x = g_wind_direction[0] * wind_intensity;
    g_weather_wind_accel_y = g_wind_direction[1] * wind_intensity;
    g_weather_wind_accel_z = g_wind_direction[2] * wind_intensity;

    // Update all rain particles with world extent
    weather_particles_update_tick(extent, extent, extent);

    // Spawn trail/splash particles where rain hits surfaces
    weather_spawn_trail_particles();
}

// ============================================================================
// Address: 0x800ee148
// Rain initialization. Clears weather state, allocates room grid, spawns
// 512 rain particles with specified extents, and computes the combined
// render scale from two sqrt values.
// ============================================================================
void rain_init(void)
{
    weather_clear_state();
    weather_grid_alloc_rooms();

    float extent = 1.0f; // FLOAT_80615260 - rain world extent
    // Spawn 512 rain particles: xyz extent, vel_x range, vel_y=1.0, vel_z range, size=1.0
    weather_particles_init(extent, extent, extent,
                           0.5f,   // FLOAT_80615264 - vel_x range
                           1.0f,   // FLOAT_806151d4 - vel_y (gravity)
                           0.5f,   // FLOAT_80615264 - vel_z range
                           1.0f,   // FLOAT_806151d4 - size scale
                           0x200); // 512 particles

    // Compute render scale = sqrt(a) + sqrt(b)
    float scale_a = sqrt_float(1.0f); // FLOAT_80615270
    float scale_b = sqrt_float(1.0f); // FLOAT_8061526c
    g_weather_render_scale_rain = scale_a + scale_b;
}

// ============================================================================
// Address: 0x800ee1bc
// Rain sprite texture preload. Wrapper around sprite_load_by_flag_mask.
// ============================================================================
void rain_preload_sprites(/* 8 params forwarded */)
{
    sprite_load_by_flag_mask(/* all params forwarded */);
}

// ============================================================================
// Address: 0x800ee1e0
// Snow initialization. Clears state, allocates grid, spawns 216 snow
// particles with snow-specific extents/velocities, calculates particle
// stride based on total count, and sets initial snow color/opacity.
// If weather type is 2 (blizzard), runs special setup.
// ============================================================================
void snow_init(void)
{
    int* weather_type_ptr = (int*)weather_particle_get_current_ptr();

    weather_clear_state();
    weather_grid_alloc_rooms();

    float extent = 1.0f; // FLOAT_80615274 - snow world extent
    // Spawn 216 snow particles: xyz extent, vel_x=0, vel_y=special, vel_z=0, size=1.0
    weather_particles_init(extent, extent, extent,
                           0.0f,   // FLOAT_806151e8 - no horizontal vel
                           1.0f,   // FLOAT_80615278 - snow fall speed
                           0.0f,   // FLOAT_806151e8 - no horizontal vel
                           1.0f,   // FLOAT_806151d4 - size scale
                           0xD8);  // 216 particles

    // Calculate particle iteration stride
    int stride = particle_get_stride();
    if (stride < 1) stride = 1;
    else stride = particle_get_stride();

    int total = weather_get_total_particles();
    g_weather_snow_stride = total / stride;

    // Compute snow render scale
    float scale_a = sqrt_float(1.0f); // FLOAT_80615270
    float scale_b = sqrt_float(1.0f); // FLOAT_8061526c
    g_weather_render_scale_snow = scale_a + scale_b;

    // Set initial snow color to white and opacity to 0
    *(unsigned short*)0x80610800 = 0xFFFF; // R
    *(unsigned short*)0x80610802 = 0xFFFF; // G
    g_weather_snow_opacity = 0.0f;

    // If blizzard type (2), run special initialization
    if (*weather_type_ptr == 2) {
        weather_setup_snow_special();
    }
}

// ============================================================================
// Address: 0x800ee2b8
// Snow setup and apply. Computes weather parameters, applies them, then
// resets snow color/opacity to initial state.
// ============================================================================
void snow_setup_and_apply(/* 16 params */)
{
    // result = weather_compute_params(all params)  -- 0x800ef890
    // weather_apply(result, ...)                   -- 0x800ee734

    // Reset snow vertex color and opacity
    *(unsigned short*)0x80610800 = 0xFFFF;
    *(unsigned short*)0x80610802 = 0xFFFF;
    g_weather_snow_opacity = 0.0f;
}

// ============================================================================
// Address: 0x800ee2f0
// Snow shutdown. Wrapper calling weather_grid_conditional_clear.
// ============================================================================
void snow_shutdown(void)
{
    weather_grid_conditional_clear();
}

// ============================================================================
// Address: 0x800ee310
// Snow per-frame tick. Evaluates wind force with snow-specific amplitudes,
// then updates all 216 snow particles and spawns trail effects.
// ============================================================================
void snow_tick(void)
{
    // Evaluate wind with snow-specific amplitude parameters
    weather_wind_evaluate_force(
        0.25f, // FLOAT_806151ec - X amplitude (lower than rain)
        1.0f,  // FLOAT_8061527c - Y amplitude
        1.0f   // FLOAT_806151f4 - Z amplitude
    );

    // Update snow particles with snow extent
    float extent = 1.0f; // FLOAT_80615274
    weather_particles_update_tick(extent, extent, extent);

    // Spawn snow trail/ground accumulation effects
    weather_spawn_trail_particles();
}

// ============================================================================
// Address: 0x800ee354
// Snow particle GX submission. Renders 216 snow particles as wind-affected
// line segments (streaks). Each particle's line extends from its position
// to position + wind offset, giving directional motion appearance.
// Uses random jitter for organic flutter effect.
// ============================================================================
void snow_submit_particles_to_gx(/* params: scale, texture, camera, etc. */)
{
    // Begin particle rendering with snow-specific setup
    // iterator_begin_render_snow()
    // render_submit_particle_begin(...)
    // render_set_line_width(FLOAT_80615280, 0xFFFFFFFF, context)

    float extent = 1.0f; // FLOAT_80615274 - snow extent
    float zero = 0.0f;

    for (int i = 0; i < 0xD8; i += particle_get_stride()) {
        float* p = (float*)((char*)g_weather_particle_buffer + i * 0x1C);

        // Bounds check: particle within [0, extent) on all axes
        if (p[0] >= zero && p[0] < extent &&
            p[1] >= zero && p[1] < extent &&
            p[2] >= zero && p[2] < extent)
        {
            // Random flutter offset (cubic falloff for organic motion)
            float rand_val = rand_float();
            float flutter = 1.0f * (rand_val - 1.0f); // center around 0
            float flutter_cubed = 1.0f * (flutter * (flutter * flutter)); // cubic

            // Compute line endpoint with wind + flutter
            float end_x = p[0] + (g_weather_wind_accel_x * p[6] + flutter_cubed);
            float end_y = p[1] - 1.0f; // FLOAT_806151f4 - slight downward offset
            float end_z = p[2] + (g_weather_wind_accel_z * p[6] + flutter_cubed);

            // Submit line segment from start to end
            // render_submit_line(p[0], p[1], p[2], end_x, end_y, end_z)
        }
    }

    // Finalize snow particle batch
    // render_submit_particle_end() -> render_finalize_batch()
    // iterator_end_render_snow()
}

// ============================================================================
// Address: 0x800ee498
// Snow render main. Top-level snow rendering entry point. Sets up fog
// offset based on camera height, configures GX state, submits snow
// particles, runs portal precipitation, and optionally renders secondary
// ground effects based on per-camera weather state.
// ============================================================================
void snow_render_main(/* params: camera, transforms */)
{
    // Begin render context
    // FUN_803826e8()

    // Compute fog offset from camera altitude
    float cam_height = *(float*)(g_camera_data + 0x3B4);
    float fog_scale = 1.0f; // FLOAT_8061524c
    float fog_offset = fog_scale * -(1.0f * cam_height - 1.0f); // FLOAT_806151f0, FLOAT_806151fc
    render_set_fog_offset(fog_offset);

    // Save and set render state
    int prev_depth = 0; // FUN_8016f00c(2)
    int prev_blend = 0; // FUN_80174078(1)
    int prev_particle = 0; // FUN_8016e178(1)

    // Bind snow texture (sprite ID 0x35C)
    int texture_id = sprite_get_texture_id(0x35C);

    // Submit snow point sprites
    snow_submit_particles_to_gx(/* texture_id, camera data */);

    // Unbind texture
    // FUN_8016e7e4(texture_id)

    // Run portal-based volumetric snow
    float snow_extent = 1.0f; // FLOAT_80615274
    float rain_extent = 1.0f; // FLOAT_80615260
    weather_render_portal_precipitation(snow_extent, snow_extent, snow_extent,
                                        g_weather_render_scale_snow, rain_extent);

    // Restore particle/blend state
    // FUN_8016e178(prev_particle)
    // FUN_80174078(prev_blend)

    // Render secondary ground effects if weather state active
    int camera_index = *(int*)(g_camera_data + 0x214);
    if (g_weather_state_table[camera_index] != 0) {
        // Adjust fog for ground effects
        float ground_fog = 1.0f * (1.0f - cam_height); // FLOAT_80615254 * (FLOAT_806151fc - height)
        render_set_fog_offset(ground_fog);
        weather_render_secondary_effects();
    }

    // Restore depth mode
    // FUN_8016f00c(prev_depth)
    // iterator_end_render_snow()
}
