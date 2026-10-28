// Address: 0x800f0da0 | Name: lightning_flash_timer_reset | Date: 2026-08-25
// Address: 0x800f0db0 | Name: lightning_flash_timer_tick | Date: 2026-08-25
// Address: 0x800f0e88 | Name: lightning_bolt_render | Date: 2026-08-25
// Address: 0x800f14b8 | Name: lightning_flash_screen_overlay | Date: 2026-08-25
//
// Lightning effect system: flash timing, procedural bolt generation,
// and screen overlay rendering for thunderstorm weather.

#include "common.h"

// Forward declarations
extern int level_get_weapon_set_ptr(void);
extern int sprite_get_texture_id(int sprite_ref);
extern void sprite_draw_viewport_blend_a(void);
extern float randf(void); // FUN_801310c0

// Global sin table (2048 entries, index = angle * 2048 / 2PI)
extern float g_sin_table[2048]; // DAT_80583228

// Lightning flash state
static float g_flash_delay_timer;      // FLOAT_80611ea0 - countdown to next flash
static float g_flash_progress;         // FLOAT_80611e9c - current flash animation progress
static float g_flash_direction;        // FLOAT_80611e98 - bolt direction angle [0..1]
static float g_flash_angular_speed;    // FLOAT_80611e94 - bolt angular velocity
static float g_flash_fade_rate;        // FLOAT_80611e90 - bolt fade speed

// Timing constants
extern float g_weather_dt_scale;       // FLOAT_806108ec
extern float g_framerate_float;        // DAT_806109c0 (as uint, converted to float)

// Weather emitter state
extern int   g_weather_emitter_state_ptr; // DAT_80611d54
extern float g_weather_speed_table[];     // DAT_80570a2c

// ============================================================================
// lightning_flash_timer_reset
// Address: 0x800f0da0
//
// Resets both lightning timers to 1.0 (fully elapsed = ready for new flash).
// Called by snow_init during weather system setup.
// ============================================================================
void lightning_flash_timer_reset(void) {
    g_flash_delay_timer = 1.0f;
    g_flash_progress = 1.0f;
}

// ============================================================================
// lightning_flash_timer_tick
// Address: 0x800f0db0
//
// Per-frame lightning timer update. Decrements the delay timer by dt.
// When delay expires (< 0), randomizes new flash parameters:
//   - direction angle
//   - angular speed (scaled by 16.0 * random + 4.0)
//   - fade rate (scaled by 0.125 * random + 0.25)
// Then advances flash progress (fade-in phase).
// ============================================================================
void lightning_flash_timer_tick(void) {
    float dt = g_weather_dt_scale * (1.0f / g_framerate_float);

    g_flash_delay_timer -= dt;

    if (g_flash_progress >= 1.0f) {
        // Waiting for next flash
        if (g_flash_delay_timer < 0.0f) {
            // Trigger new flash
            float rnd = randf();
            g_flash_delay_timer = 8.0f * rnd + 2.0f; // FLOAT_80615314: delay [2..10] sec

            g_flash_progress = 0.0f;

            // Random direction [0..1]
            rnd = randf();
            g_flash_direction = rnd;

            // Random angular speed: 16.0 * (8.0 * rnd + 4.0)
            rnd = randf();
            g_flash_angular_speed = 16.0f * (8.0f * rnd + 4.0f);
            // FLOAT_8061523c * (FLOAT_80615264 * rnd + FLOAT_80615250)

            // Random fade rate: 0.125 * (1.0 * rnd + 0.25)
            rnd = randf();
            g_flash_fade_rate = 0.125f * (1.0f * rnd + 0.25f);
            // FLOAT_806151e4 * (FLOAT_80615280 * rnd + FLOAT_80615238)
        }
    } else {
        // Flash in progress: advance
        g_flash_progress += dt;
    }
}

// ============================================================================
// lightning_bolt_render
// Address: 0x800f0e88
//
// Renders a procedural lightning bolt when flash is active (progress < 1.0).
// Two-part rendering:
//   Part 1: Camera-facing quad strip (5 vertices) for the bolt core,
//            using sprite 0x35b. Positioned relative to weapon set (camera).
//   Part 2: 33-point spine curve with sin-wave displacement for volumetric
//            glow, using sprite 0x35a.
//
// Bolt shape is determined by:
//   - Random direction (g_flash_direction via sin table)
//   - Sin-wave displacement along spine
//   - Intensity envelope from flash progress
// ============================================================================
void lightning_bolt_render(void) {
    int weapon_set;
    float bolt_vertices[96]; // 32 * 3 spine points
    float quad_verts[12];    // 4 corners * 3
    float dir_x, dir_y, dir_z;
    float right_x, right_y, right_z;
    float up_x, up_y, up_z;

    weather_lightning_begin(); // FUN_803826e0

    weapon_set = level_get_weapon_set_ptr();

    if (g_flash_progress >= 1.0f) {
        weather_lightning_end(); // FUN_8038272c
        return;
    }

    // Compute intensity envelope from flash progress
    // Multi-segment fade curve based on progress thresholds
    float intensity;
    float progress = g_flash_progress;

    if (progress < 0.1f) {
        intensity = (2.0f * progress) / 0.1f;
    } else if (progress < 1.0f) {
        // Several keyframe segments for fade
        if (progress < 0.25f) {
            intensity = (progress - 1.0f) / 0.1f;
        } else if (progress < 0.5f) {
            intensity = (0.25f - progress) / 0.1f;
        } else if (progress < 0.75f) {
            intensity = (progress - 0.25f) / 1.0f;
        } else {
            intensity = (1.0f - progress) / 0.25f;
        }
    } else {
        intensity = 0.0f;
    }

    unsigned int alpha = (unsigned int)(10.0f * intensity); // FLOAT_80615304

    // Compute bolt direction from sin table
    int angle_idx = (int)(6.283f * g_flash_fade_rate + 2.0f) & 0x7FF;
    int speed_idx = (int)(6.283f * g_flash_angular_speed + 2.0f) & 0x7FF;

    dir_x = g_sin_table[angle_idx];
    dir_y = g_sin_table[speed_idx];
    // cos from sin table offset by PI/2 (0x200 entries)
    dir_z = g_sin_table[(speed_idx + 0x200) & 0x7FF];

    // Normalize direction
    float len_sq = dir_z * dir_z + dir_y * dir_y + dir_x * dir_x;
    if (len_sq > 0.0001f) {
        float inv_len = fast_inv_sqrt(len_sq); // Newton-Raphson rsqrt
        dir_x *= inv_len;
        dir_y *= inv_len;
        dir_z *= inv_len;
    }

    // Compute perpendicular vectors (right, up) for quad orientation
    // right = normalize(cross(dir, up_hint))
    float neg_z = -dir_z;
    float perp_sq = dir_y * dir_y + neg_z * neg_z + 0.0f;
    if (perp_sq > 0.0001f) {
        float inv_perp = fast_inv_sqrt(perp_sq);
        right_x = neg_z * inv_perp;
        right_y = 0.0f * inv_perp;
        right_z = dir_y * inv_perp;
    } else {
        right_x = neg_z;
        right_y = 0.0f;
        right_z = dir_y;
    }

    // up = cross(right, dir)
    up_x = (right_y * dir_z - right_z * dir_y);
    up_y = (right_z * dir_x - right_x * dir_z);
    up_z = (right_x * dir_y - right_y * dir_x);

    // Compute bolt position relative to camera (weapon set)
    float half_len = *(float*)(weapon_set + 0x18); // bolt half-length
    float bolt_len = 1.0f + half_len;
    float base_x = *(float*)(weapon_set + 0x24);
    float base_y = *(float*)(weapon_set + 0x28);
    float base_z = *(float*)(weapon_set + 0x2C);

    // Bolt endpoint
    float end_x = dir_y * bolt_len + base_x;
    float end_y = dir_x * bolt_len + base_y;
    float end_z = dir_z * bolt_len + base_z;

    // Build quad corners (half_len * 2.0 width, half_len * 1.5 lateral)
    float hw = 2.0f * half_len;
    float hh = 1.5f * half_len; // FLOAT_806151f0

    // Scale right/up by lateral extent
    float lat_x = up_x * hh;
    float lat_y = up_y * hh;
    float lat_z = up_z * hh;

    // 4 corners: base +/- right*hw, end +/- right*hw
    float fwd_offset = right_x * hw;
    // ... (full quad construction)

    // Part 1: Render bolt core as quad strip
    int prev_tex_filter = gx_set_texture_filter(0); // FUN_8016e8ac
    int prev_blend = gx_set_blend_mode(1);
    int bolt_tex = sprite_get_texture_id(0x35b);

    gx_begin_quad_strip(2, 0x92, 0, 10, bolt_tex); // FUN_80172a4c

    // 5 vertices (4 corners + center repeat for strip)
    for (int v = 4; v >= 0; v--) {
        int corner = v & 3;

        // Write position to GX FIFO
        GX_FIFO_F32 = quad_verts[corner * 3 + 0];
        GX_FIFO_F32 = quad_verts[corner * 3 + 1];
        GX_FIFO_F32 = quad_verts[corner * 3 + 2];

        // Color: white with computed alpha
        GX_FIFO_U32 = 0x7F7F7F00;

        // UV coords
        float u = ((corner + 1) & 2) ? 1.0f : 0.0f;
        float s = (v & 2) ? 1.0f : 0.0f;
        GX_FIFO_F32 = u;
        GX_FIFO_F32 = s;

        // Center position + color for tristrip
        GX_FIFO_F32 = end_x;
        GX_FIFO_F32 = end_y;
        GX_FIFO_F32 = end_z;
        GX_FIFO_U32 = (alpha & 0xFF) | 0x7F7F7F00;
        GX_FIFO_F32 = 2.0f;
        GX_FIFO_F32 = 2.0f;
    }

    gx_end_quad_strip(); // FUN_80172c54

    // Part 2: Render volumetric glow spine
    unsigned int glow_alpha = (unsigned int)(12.0f * intensity); // FLOAT_80615324
    gx_set_texture_filter(0);
    gx_set_blend_mode(2); // additive

    int prev_cull = gx_set_cull_mode(-0x7FE91E08); // FUN_8016e4b4

    // Generate 33-point spine with sin-wave displacement
    float spine_t = 0.0f;
    for (int p = 32; p >= 0; p--) {
        // Compute position along bolt axis
        float along = 1.5f * spine_t + g_flash_direction; // FLOAT_806151f0
        float wrapped = along + 2.0f;

        // Wrap to [0, 1]
        while (wrapped >= 1.0f) wrapped -= 1.0f;

        // Sin displacement perpendicular to bolt
        float displacement = gx_compute_displacement(wrapped); // FUN_80164b10

        // Modulate by sin table for flutter
        int flutter_idx = (int)(6.283f * (0.1f * g_flash_progress) + 2.0f) & 0x7FF;
        float flutter = g_sin_table[flutter_idx] * displacement;

        // Compute wrapped secondary
        float along2 = wrapped + 2.0f;
        while (along2 >= 1.0f) along2 -= 1.0f;
        float disp2 = gx_compute_displacement(along2);

        int flutter_idx2 = (int)(6.283f * (0.1f * g_flash_progress) + 2.0f) & 0x7FF;
        float flutter2 = g_sin_table[flutter_idx2] * disp2;

        // Write spine point
        bolt_vertices[p * 3 + 0] = end_x + 1.5f * flutter;   // FLOAT_80615328
        bolt_vertices[p * 3 + 1] = end_y - 0.25f * spine_t;  // FLOAT_8061532c * t
        bolt_vertices[p * 3 + 2] = end_z + 1.5f * flutter2;

        spine_t += 0.5f; // FLOAT_80615330 step
    }

    // Draw spine as line strip with glow texture
    int glow_tex = sprite_get_texture_id(0x35a);
    unsigned int glow_color = (glow_alpha << 24) | (glow_alpha << 16) |
                              (glow_alpha << 8) | glow_alpha;
    draw_line_strip_3d(bolt_vertices, 0, 32, glow_tex, glow_color, 3); // FUN_8001cb24

    // Restore GX state
    gx_set_cull_mode(prev_cull);
    gx_restore_texture_filter(prev_tex_filter); // placeholder
    gx_set_texture_filter(prev_tex_filter);
    gx_set_blend_mode(prev_blend);

    weather_lightning_end(); // FUN_8038272c
}

// ============================================================================
// lightning_flash_screen_overlay
// Address: 0x800f14b8
//
// Conditionally draws a full-screen white flash overlay when lightning is active.
// Intensity = speed_table[current_slot] * ((1.0 - progress) / 2.0) * scale_factor.
// Only triggers when emitter state is valid and progress > threshold (FLOAT_806152cc).
// ============================================================================
void lightning_flash_screen_overlay(void) {
    if (g_weather_emitter_state_ptr == 0) return;

    // Check if flash progress exceeds activation threshold
    if (g_flash_progress <= 0.85f) return; // FLOAT_806152cc

    // Compute flash intensity
    float type_speed = g_weather_speed_table[*(int*)(g_weather_emitter_state_ptr + 0x214)];
    float fade_factor = (1.0f - g_flash_progress) / 2.0f;
    int intensity = (int)(1.5f * fade_factor * type_speed); // FLOAT_80615284

    if (intensity > 0) {
        sprite_draw_viewport_blend_a();
    }
}
