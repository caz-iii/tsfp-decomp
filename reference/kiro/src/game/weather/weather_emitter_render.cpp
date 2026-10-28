// Address: 0x800ef8d8 | Name: rain_trail_particle_init_random | Date: 2026-08-25
// Address: 0x800efa58 | Name: weather_streak_render_quads | Date: 2026-08-25
// Address: 0x800efde4 | Name: weather_debris_render_sprites | Date: 2026-08-25
// Address: 0x800f0000 | Name: weather_material_select_weighted | Date: 2026-08-25
// Address: 0x800f007c | Name: weather_emitter_select_node | Date: 2026-08-25
// Address: 0x800f011c | Name: weather_emitter_sample_color | Date: 2026-08-25
//
// Weather emitter rendering: particle initialization, streak/debris quad
// rendering, and material/color sampling from scene node gradient textures.
// Part of the weather precipitation visual pipeline.

#include "common.h"

// Forward declarations
extern void* weather_particle_get_current_ptr(void);
extern int sprite_get_texture_id(int sprite_ref);
extern int scene_node_get_texture_data(int node_id, int channel);
extern int scene_node_get_material_count(int node_id, int channel);
extern float randf(void); // FUN_801310c0 - returns [0.0, 1.0)

// Rain/snow streak particle pool (50 particles per emitter slot, 0x28 bytes each)
// Layout per particle:
//   +0x00: pos_x (normalized)
//   +0x04: pos_y (normalized)
//   +0x08: alpha (squared fade)
//   +0x0C: scale_y (height)
//   +0x10: vel_x (screen-space)
//   +0x14: vel_y (screen-space)
//   +0x18: vel2_x (secondary offset)
//   +0x1C: vel2_y (secondary offset)
//   +0x20: spin (rotation rate)
//   +0x24: active (0 = dead, nonzero = alive)
extern char* g_weather_streak_pool;        // DAT_80570a10
extern int*  g_weather_emitter_state_ptr;  // DAT_80611d54
extern int   g_viewport_data[];            // DAT_80611748

// Debris/leaf particle pool (32 particles: 4 types x 8 each, 0x28 bytes each)
// Same layout as streak particles, different texture and count
extern float g_weather_type_table[];       // DAT_80570a2c (per-type speed table)

// ============================================================================
// rain_trail_particle_init_random
// Address: 0x800ef8d8
//
// Initializes a rain trail particle with randomized position, velocity, alpha,
// and scale. Uses weather type to modify velocity multiplier (rain vs snow).
// Called by rain_trail_spawner.
// ============================================================================
void rain_trail_particle_init_random(void) {
    float* particle;
    int* weather_ptr;
    float vel_scale;
    bool is_rain_or_snow;

    // Get next free particle slot from iterator
    particle = (float*)weather_particle_iterator_next(); // FUN_803826f0 returns slot ptr

    // Get current weather type state
    weather_ptr = (int*)weather_particle_get_current_ptr();

    // Set initial alpha (squared for fade-in curve)
    particle[2] = 0.5f; // FLOAT_806151e8

    // Set initial scale based on random
    float rnd_scale = randf();
    particle[3] = 2.0f * rnd_scale + 2.0f; // scale in [2.0, 4.0]

    // Set particle as inactive initially
    particle[9] = 0.0f;

    // Check activation conditions:
    // - speed matches the per-type speed table for current emitter slot
    // - random position threshold vs emitter field at +0x338
    // - either random > 2.0 or secondary value < 0.5
    float type_speed = g_weather_type_table[*(int*)(g_weather_emitter_state_ptr + 0x214)];
    float rnd_pos = randf();

    if (type_speed == type_speed && // speed matches (always true for valid entries)
        (rnd_pos - 0.25f) < *(float*)(g_weather_emitter_state_ptr + 0x338) &&
        (randf() > 2.0f || randf() < 0.5f)) {

        // Determine velocity scale based on weather type
        is_rain_or_snow = (*weather_ptr == 1) || (*weather_ptr == 2);
        vel_scale = is_rain_or_snow ? 0.6f : 0.8f; // FLOAT_806152dc / FLOAT_806152e0

        // Activate particle
        particle[9] = 1.4013e-45f; // FLT_MIN-ish flag (nonzero = active)

        // Random XY position in [0,1]
        particle[0] = randf();
        particle[1] = randf();

        // Random rotation rate
        particle[8] = 1.5f * randf(); // FLOAT_806151f0

        // Random velocity components with type-scaled magnitude
        particle[4] = vel_scale * (3.0f * randf() + 1.0f);  // FLOAT_806152e8, FLOAT_806152e4
        particle[5] = vel_scale * (0.25f * randf() + 1.0f); // FLOAT_806152d8, FLOAT_806151fc
        particle[6] = vel_scale * (4.0f * randf() + 0.5f);  // FLOAT_806151d0, FLOAT_806152ec
        particle[7] = vel_scale * (0.125f * randf() + 0.75f); // FLOAT_80615250, FLOAT_8061527c
    }

    // End particle iterator
    weather_particle_iterator_end(); // FUN_8038273c
}

// ============================================================================
// weather_streak_render_quads
// Address: 0x800efa58
//
// Renders weather streak particles as screen-space quads in two passes:
// Pass 1: main particle position (velocity offsets applied)
// Pass 2: secondary offset (motion blur / trail effect)
// Uses sprite texture 0x353, iterates 50 particles per emitter slot.
// Called by weather_emitter_opacity_update.
// ============================================================================
void weather_streak_render_quads(void) {
    int tex_id;
    int tex_info_ptr;
    unsigned short tex_w, tex_h;
    float* particle;
    int i;

    unsigned int alpha_raw = weather_render_begin_alpha(); // FUN_803826e4

    tex_id = sprite_get_texture_id(0x353);
    tex_info_ptr = g_texture_table + tex_id * 0x58;
    tex_w = *(unsigned short*)(tex_info_ptr + 0x4E);
    tex_h = *(unsigned short*)(tex_info_ptr + 0x52);

    gx_bind_texture(tex_id);    // FUN_8016e77c
    gx_set_blend_mode(10);      // FUN_8016f00c - additive blend

    unsigned int uv_w = (unsigned int)tex_w << 4;
    unsigned int uv_h = (unsigned int)tex_h << 4;

    // Pass 1: main particle positions
    for (i = 0; i < 50; i++) {
        particle = (float*)(g_weather_streak_pool +
                   (i + *(int*)(g_weather_emitter_state_ptr + 0x214) * 50) * 0x28);

        if (particle[9] != 0.0f) {
            // Convert normalized position to screen space using viewport
            int vp_x = g_viewport_data[3] ^ 0x80000000; // signed viewport origin
            int vp_y = g_viewport_data[8] ^ 0x80000000;

            float screen_x = particle[0] * (float)((double)(unsigned int)vp_x - 4503599627370496.0);
            float screen_y = particle[1] * (float)((double)(unsigned int)vp_y - 4503599627370496.0);
            float half_w = particle[5] * (float)((double)(unsigned int)vp_x - 4503599627370496.0);
            float half_h = particle[4] * (float)((double)(unsigned int)vp_x - 4503599627370496.0);

            // Set alpha from squared fade
            float alpha = (1.0f - particle[2] * particle[2]) *
                          (float)((double)(alpha_raw ^ 0x80000000) - 4503599627370496.0);
            gx_set_alpha(alpha); // FUN_803825c0

            // Draw quad
            gx_draw_sprite_quad(
                (int)(screen_x - half_h), (int)(screen_y - half_w),
                (int)(screen_x + half_h), (int)(screen_y + half_w),
                0, 0, uv_w, uv_h
            ); // FUN_80170410
        }
    }

    // Pass 2: secondary motion trail offset
    gx_set_blend_mode(11); // FUN_8016f00c - different blend
    for (i = 0; i < 50; i++) {
        particle = (float*)(g_weather_streak_pool +
                   (i + *(int*)(g_weather_emitter_state_ptr + 0x214) * 50) * 0x28);

        if (particle[9] != 0.0f) {
            // Offset position by velocity for motion blur
            float rnd = randf();
            float offset_x = 0.6f * 3.0f * (rnd - 2.0f) + particle[0]; // velocity-biased
            rnd = randf();
            float offset_y = 0.6f * 3.0f * (rnd - 2.0f) + particle[1];

            int vp_x = g_viewport_data[3] ^ 0x80000000;
            int vp_y = g_viewport_data[8] ^ 0x80000000;

            float sx = offset_x * (float)((double)(unsigned int)vp_x - 4503599627370496.0);
            float sy = offset_y * (float)((double)(unsigned int)vp_y - 4503599627370496.0);
            float hw = particle[7] * (float)((double)(unsigned int)vp_x - 4503599627370496.0);
            float hh = particle[6] * (float)((double)(unsigned int)vp_x - 4503599627370496.0);

            float alpha = (1.0f - particle[2] * particle[2]) *
                          (float)((double)(alpha_raw ^ 0x80000000) - 4503599627370496.0);
            gx_set_alpha(alpha);

            gx_draw_sprite_quad(
                (int)(sx - hh), (int)(sy + hw),
                (int)(sx + hh), (int)(sy - hw),
                0, 0, uv_w, uv_h
            );
        }
    }

    weather_render_end(); // FUN_80382730
}

// ============================================================================
// weather_debris_render_sprites
// Address: 0x800efde4
//
// Renders weather debris (leaves, paper, etc.) as screen-space sprites.
// 4 texture variants (sprite IDs 0x355-0x358), 8 particles each (32 total).
// Only draws particles within viewport bounds.
// Called by weather_render_streak_callback.
// ============================================================================
void weather_debris_render_sprites(void) {
    int tex_id, tex_info_ptr;
    unsigned short tex_w, tex_h;
    float* particle;
    int i, j;

    unsigned int alpha_raw = weather_render_begin_debris(); // FUN_803826cc

    gx_set_blend_mode(1); // FUN_8016f00c - normal alpha blend

    for (j = 0; j < 4; j++) {
        tex_id = sprite_get_texture_id(j + 0x355);
        tex_info_ptr = g_texture_table + tex_id * 0x58;
        tex_w = *(unsigned short*)(tex_info_ptr + 0x4E);
        tex_h = *(unsigned short*)(tex_info_ptr + 0x52);

        gx_bind_texture(tex_id);

        // 8 particles per texture variant
        for (i = j * 8; i < (j + 1) * 8; i++) {
            particle = (float*)(g_weather_streak_pool +
                       (i + *(int*)(g_weather_emitter_state_ptr + 0x214) * 32) * 0x28);

            if (particle[9] != 0.0f) {
                unsigned int vp_x = g_viewport_data[3] ^ 0x80000000;

                // Compute screen position and half-extents
                float half_w = 2.0f * particle[4] *
                               (float)((double)vp_x - 4503599627370496.0);
                float half_h = 2.0f * particle[5] *
                               (float)((double)vp_x - 4503599627370496.0);
                float sx = particle[0] * (float)((double)vp_x - 4503599627370496.0);
                float sy = particle[1] *
                           (float)((double)(g_viewport_data[8] ^ 0x80000000) - 4503599627370496.0);

                int left   = (int)(sx - half_w);
                int right  = (unsigned int)(sx + half_w);
                int top    = (int)(sy - half_h);
                int bottom = (unsigned int)(sy + half_h);

                // Alpha from squared fade (halved for debris)
                float alpha = (1.0f - particle[2] * particle[2]) *
                              (float)((double)(alpha_raw >> 1) - 1073741824.0); // DOUBLE_80615230
                gx_set_alpha(alpha);

                // Viewport clip check
                if (g_viewport_data[0] <= (int)right &&
                    left < g_viewport_data[1] &&
                    g_viewport_data[5] <= (int)bottom &&
                    top < g_viewport_data[6]) {
                    gx_draw_sprite_quad(
                        left, top, right, bottom,
                        0, 0, (unsigned int)tex_w << 4, (unsigned int)tex_h << 4
                    );
                }
            }
        }
    }

    weather_render_end_debris(); // FUN_80382718
}

// ============================================================================
// weather_material_select_weighted
// Address: 0x800f0000
//
// Selects an entry from a weighted probability list. Walks entries in reverse,
// accumulating weight from field +0x10. Returns first entry whose cumulative
// weight exceeds a random threshold, or defaults to entry[0].
//
// param_1: pointer to array of entry pointers
// param_2: number of entries
// Returns: selected entry pointer
// ============================================================================
int weather_material_select_weighted(int* entry_list, int count) {
    float cumulative = 0.0f;
    float threshold = randf();

    // Walk entries from last to first
    for (int i = count - 1; i >= 0; i--) {
        cumulative += *(float*)(entry_list[i] + 0x10);
        if (cumulative >= threshold) {
            return entry_list[i];
        }
    }

    // Default to first entry
    return entry_list[0];
}

// ============================================================================
// weather_emitter_select_node
// Address: 0x800f007c
//
// Selects a scene node index from a weighted node list (max 4 entries).
// Accumulates (1.0 / total_weight) * per_node_weight from DAT_80611aa8 table,
// picks node where cumulative weight exceeds random threshold.
// Called by weather_emitter_sample_color.
// ============================================================================
void weather_emitter_select_node(void) {
    int node_base;      // FUN_803826f0 returns emitter struct pointer
    float cumulative;
    int idx;
    int offset;

    node_base = weather_emitter_get_context(); // FUN_803826f0

    cumulative = 0.0f;
    idx = 0;
    offset = 0;
    float threshold = randf();

    while (*(int*)(node_base + offset) != 0 && idx < 4) {
        int node_id = *(int*)(node_base + offset);
        float node_weight = *(float*)(g_anim_node_table + node_id * 0xB4 + 0xAC);
        cumulative += (1.0f / *(float*)(node_base + 0x40)) * node_weight;

        if (cumulative > threshold) {
            break;
        }
        idx++;
        offset += 4;
    }

    weather_emitter_end_context(); // FUN_8038273c
}

// ============================================================================
// weather_emitter_sample_color
// Address: 0x800f011c
//
// Samples an RGB color from a weather emitter's scene node material gradient.
// Uses weather_emitter_select_node to pick a node, retrieves its material list,
// then selects a material via weather_material_select_weighted.
// Supports bilinear interpolation (4 stops) or N-stop weighted blend.
//
// param_1, param_2: unused register padding
// param_3: output float[3] for RGB color
// ============================================================================
void weather_emitter_sample_color(int param_1, int param_2, float* out_color) {
    int node_id;
    int* material_list;
    int mat_count;

    weather_emitter_begin_sample(); // FUN_803826e8

    node_id = weather_emitter_select_node(); // picks node by weight
    material_list = (int*)scene_node_get_texture_data(node_id, 1);

    if (material_list == NULL) {
        weather_emitter_end_sample(); // FUN_80382734
        return;
    }

    mat_count = scene_node_get_material_count(node_id, 1);
    if (mat_count <= 0) {
        weather_emitter_end_sample();
        return;
    }

    int total_mats = scene_node_get_material_count(node_id, 1);
    unsigned int* material = (unsigned int*)weather_material_select_weighted(material_list, total_mats);

    // Only process materials with weight > 0 and more than 2 color stops
    if (*(float*)&material[4] <= 0.0f || material[0] <= 2) {
        weather_emitter_end_sample();
        return;
    }

    unsigned int num_stops = material[0];

    if (num_stops == 4) {
        // Bilinear interpolation between 4 gradient stops
        float t0 = randf(); // position along first axis
        float t1 = randf(); // position along second axis

        // Each stop has RGB at offsets [9,10,11], [15,16,17], [27,28,29]
        float base_r = *(float*)&material[9];
        float base_g = *(float*)&material[10];
        float base_b = *(float*)&material[11];

        float mid_r = *(float*)&material[15];
        float mid_g = *(float*)&material[16];
        float mid_b = *(float*)&material[17];

        float end_r = *(float*)&material[27];
        float end_g = *(float*)&material[28];
        float end_b = *(float*)&material[29];

        // Interpolate: base -> mid (by t0), then result -> end (by t1)
        out_color[0] = (t0 * (mid_r - base_r) + base_r) + t1 * (end_r - (t0 * (mid_r - base_r) + base_r));
        out_color[1] = (t0 * (mid_g - base_g) + base_g) + t1 * (end_g - (t0 * (mid_g - base_g) + base_g));
        out_color[2] = (t0 * (mid_b - base_b) + base_b) + t1 * (end_b - (t0 * (mid_b - base_b) + base_b));
    } else {
        // N-stop weighted blend (up to 20 stops)
        unsigned int stop_count = (num_stops < 20) ? num_stops : 20;
        float weights[20];
        float total_weight = 0.0f;

        // Gather random weights for each stop
        for (int i = stop_count - 1; i >= 0; i--) {
            weights[i] = randf();
            total_weight += weights[i];
        }

        // Normalize and blend
        out_color[0] = 0.0f;
        out_color[1] = 0.0f;
        out_color[2] = 0.0f;

        for (int i = stop_count - 1; i >= 0; i--) {
            float normalized = (1.0f / total_weight) * weights[i];
            out_color[0] += normalized * *(float*)&material[i * 6 + 9];
            out_color[1] += normalized * *(float*)&material[i * 6 + 10];
            out_color[2] += normalized * *(float*)&material[i * 6 + 11];
        }
    }

    weather_emitter_end_sample();
}
