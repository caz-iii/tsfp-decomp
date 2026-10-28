// =============================================================================
// Sprite Effects - Post-process, distortion grid, color correction
// Address Range: 0x800ce604 - 0x800cf180 (batch 281)
// Date: 2026-08-25
// =============================================================================

#include "common.h"
#include "gx.h"
#include "sprite.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#define GRID_VERTEX_COUNT    110  // 0x6E vertices in distortion grid
#define GRID_VERTEX_STRIDE   0x54 // bytes per vertex entry
#define GRID_CELL_STRIDE     0x34 // bytes per cell entry
#define GRID_ROW_STRIDE_VTX  0x39C // bytes per vertex row
#define GRID_ROW_STRIDE_CELL 0x208 // bytes per cell row

// ---------------------------------------------------------------------------
// Address: 0x800ce604 | Name: sprite_draw_viewport_mask
// Draws a viewport mask: gray solid fill (0x7F7F7F7F) then overlays textured
// quad with render mode 0xD (stencil/mask). Used for split-screen masking.
// ---------------------------------------------------------------------------
void sprite_draw_viewport_mask(void)
{
    // Set texture to solid color mode
    set_particle_texture(-6); // FUN_8016e77c

    int prev_render = set_render_mode(0); // FUN_8016f00c

    // Draw gray solid fill over viewport
    int vp_x1 = g_viewport_rect[0];
    int vp_y1 = g_viewport_rect[5];
    int vp_x2 = g_viewport_rect[1] + 1;
    int vp_y2 = g_viewport_rect[6] + 1;

    draw_solid_rect(vp_x1, vp_y1, vp_x2, vp_y2, 0x7F7F7F7F); // FUN_8016f4bc

    // Overlay with stencil blend mode
    set_render_mode(0xD);
    draw_textured_quad_rect(vp_x1, vp_y1, g_viewport_rect[1], g_viewport_rect[6],
                           vp_x1 << 4, vp_y1 << 4,
                           g_viewport_rect[1] << 4, g_viewport_rect[6] << 4); // FUN_80170410

    // Restore
    set_render_mode(prev_render);
}

// ---------------------------------------------------------------------------
// Address: 0x800ce6bc | Name: sprite_draw_heat_distortion
// Per-scanline horizontal heat distortion (heat shimmer). Iterates each row
// of the viewport, computes sin-table UV offset per row for wave effect.
// Intensity halved when >2 players (split-screen performance).
// ---------------------------------------------------------------------------
void sprite_draw_heat_distortion(void)
{
    // Get distortion intensity from context
    float intensity = get_distortion_context(); // FUN_803826ec (float)
    intensity *= HEAT_SCALE_FACTOR; // FLOAT_80614e08

    // Reduce intensity in split-screen (>2 players)
    int player_count = get_scene_room_count(); // FUN_801c1714
    if (player_count > 2) {
        intensity *= 0.5f; // FLOAT_80614de8
    }

    // Setup GX state
    set_texture_wrap(1);          // FUN_8016e000
    set_particle_texture(-6);     // FUN_8016e77c
    set_texture_wrap(0);          // FUN_8016e000
    set_render_mode(0);           // FUN_8016f00c
    set_texture_mode(0);          // FUN_8016e8ac
    gx_begin_particle_pass();     // FUN_8017c658

    // Per-row wave parameters
    float freq = HEAT_FREQUENCY;  // FLOAT_80614e0c
    float amp = HEAT_AMPLITUDE;   // FLOAT_80614e10
    float period = HEAT_PERIOD;   // FLOAT_80614e14
    float half = 0.5f;            // FLOAT_80614de8

    int vp_y1 = g_viewport_rect[5];
    int vp_y2 = g_viewport_rect[6] + 1;
    int vp_height = g_viewport_rect[8]; // total viewport height

    // Draw per-scanline distorted strips
    for (u32 row = vp_y1; (int)row < vp_y2; row++) {
        // Compute horizontal offset from sin table
        float t_normalized = (float)row / (float)vp_height;
        float wave_phase = amp * (period * t_normalized) / period;
        int sin_index = (int)(freq * wave_phase + half) & 0x7FF;
        float sin_val = g_sin_table[sin_index];

        // Compute per-row sqrt for intensity decay
        float row_intensity = sqrtf(sin_val); // FUN_80131538

        // UV offset for this row
        int uv_offset = (int)(intensity * row_intensity);

        // Draw single row strip with offset UVs
        int vp_x1 = g_viewport_rect[0];
        int vp_x2 = g_viewport_rect[1] + 1;
        draw_textured_quad_rect(vp_x1, row, vp_x2, row + 1,
                               uv_offset + vp_x1 * 16, row << 4,
                               vp_x2 * 16 - uv_offset, (row + 1) << 4); // FUN_80170410
    }

    gx_end_particle_pass(); // FUN_8017c670
    restore_distortion_context(); // FUN_80382738
}

// ---------------------------------------------------------------------------
// Address: 0x800ce880 | Name: sprite_draw_viewport_fog
// Semi-transparent white fog overlay. Render mode 2 (additive), alpha from
// param low byte blended with 0x7F7F7F gray base color.
// ---------------------------------------------------------------------------
void sprite_draw_viewport_fog(u32 fog_alpha)
{
    set_render_mode(2);  // FUN_8016f00c — additive
    set_texture_mode(0); // FUN_8016e8ac

    int vp_x1 = g_viewport_rect[0];
    int vp_y1 = g_viewport_rect[5];
    int vp_x2 = g_viewport_rect[1] + 1;
    int vp_y2 = g_viewport_rect[6] + 1;

    // Pack color: 0x7F7F7F with alpha from param
    u32 color = 0x7F7F7F00 | (fog_alpha & 0xFF);

    draw_solid_rect(vp_x1, vp_y1, vp_x2, vp_y2, color); // FUN_8016f4bc
}

// ---------------------------------------------------------------------------
// Address: 0x800ce8e4 | Name: sprite_draw_viewport_blend_a
// Textured viewport quad with additive blend (mode 2). Texture -6 (effect
// buffer). First pass of color grading/tint system.
// ---------------------------------------------------------------------------
void sprite_draw_viewport_blend_a(void)
{
    get_blend_context(); // FUN_803826e8

    set_particle_texture(-6); // FUN_8016e77c

    int prev_render = set_render_mode(2); // additive
    int prev_tex = set_texture_mode(0);

    // Draw viewport quad with effect buffer
    float vp_x1 = (float)g_viewport_rect[0];
    float vp_y1 = (float)g_viewport_rect[5];
    float vp_x2 = (float)g_viewport_rect[1];
    float vp_y2 = (float)g_viewport_rect[6];
    float alpha = VIEWPORT_QUAD_ALPHA; // FLOAT_80614e00

    draw_textured_quad_2d(vp_x1, vp_y1, vp_x2, vp_y2, alpha,
                         g_viewport_rect[0] << 4, g_viewport_rect[5] << 4,
                         g_viewport_rect[1] << 4, g_viewport_rect[6] << 4); // FUN_80171f7c

    // Restore
    set_texture_mode(prev_tex);
    set_render_mode(prev_render);
    commit_blend(); // FUN_80382734
}

// ---------------------------------------------------------------------------
// Address: 0x800ce9e4 | Name: sprite_draw_viewport_blend_b
// Viewport quad with variable blend mode from register context.
// Second pass of color grading. Texture -6.
// ---------------------------------------------------------------------------
void sprite_draw_viewport_blend_b(void)
{
    int blend_mode = get_blend_context(); // FUN_803826e8 — mode from register

    set_particle_texture(-6); // FUN_8016e77c

    int prev_tex = set_texture_mode(0);
    int prev_render = set_render_mode(blend_mode);

    // Draw viewport quad
    float vp_x1 = (float)g_viewport_rect[0];
    float vp_y1 = (float)g_viewport_rect[5];
    float vp_x2 = (float)g_viewport_rect[1];
    float vp_y2 = (float)g_viewport_rect[6];
    float alpha = VIEWPORT_QUAD_ALPHA;

    draw_textured_quad_2d(vp_x1, vp_y1, vp_x2, vp_y2, alpha,
                         g_viewport_rect[0] << 4, g_viewport_rect[5] << 4,
                         g_viewport_rect[1] << 4, g_viewport_rect[6] << 4); // FUN_80171f7c

    // Restore
    set_texture_mode(prev_tex);
    set_render_mode(prev_render);
    commit_blend(); // FUN_80382734
}

// ---------------------------------------------------------------------------
// Address: 0x800ceaf0 | Name: sprite_apply_color_correction
// MASSIVE: Per-channel (RGB) color correction with two modes:
// - Mode 2: additive brightness (R/G/B + intensity * exposure)
// - Mode 1: multiply + offset (R/G/B * contrast + bias)
// Falls back to dual-pass (blend_a + blend_b) if channel values overflow 0x80.
// ---------------------------------------------------------------------------
void sprite_apply_color_correction(u32 param_1, u32 param_2,
                                   int color_r, int color_g, int color_b, int exposure)
{
    u64 context = get_blend_context(); // FUN_803826e8
    int brightness = (int)(context >> 32);
    int mode = (int)context;

    if (mode == 2) {
        // Additive brightness mode
        // Compute per-channel: (color * exposure) >> 7
        u32 r_adj = (u32)(color_r * exposure) >> 7;
        u32 g_adj = (u32)(color_g * exposure) >> 7;
        u32 b_adj = (u32)(color_b * exposure) >> 7;

        // Check if all channels fit in 0x80 range
        if ((int)(r_adj + brightness + (r_adj * brightness >> 7)) < 0x81 &&
            (int)(g_adj + brightness + (g_adj * brightness >> 7)) < 0x81 &&
            (int)(b_adj + brightness + (b_adj * brightness >> 7)) < 0x81) {
            // Single-pass additive
            set_particle_texture(-6);
            set_texture_mode(0);
            set_render_mode(2);
            draw_textured_quad_2d(/* viewport bounds + alpha */);
        } else {
            // Overflow: fall back to dual-pass
            goto fallback;
        }
    } else if (mode == 1) {
        // Multiply + offset mode
        int adjusted_brightness = brightness + 0x80;
        u32 r_val = adjusted_brightness * (((u32)(color_r * exposure) >> 7) - exposure + 0x80) >> 7;
        u32 g_val = adjusted_brightness * (((u32)(color_g * exposure) >> 7) - exposure + 0x80) >> 7;
        u32 b_val = adjusted_brightness * (((u32)(color_b * exposure) >> 7) - exposure + 0x80) >> 7;

        if (r_val < 0x81 && g_val < 0x81 && b_val < 0x81) {
            // Single-pass multiply
            set_particle_texture(-6);
            set_texture_mode(0);
            set_render_mode(1);
            draw_textured_quad_2d(/* viewport bounds + alpha */);
        } else if ((r_val - 0x80) >= 0 && (r_val - 0x80) < 0x81 &&
                   (g_val - 0x80) >= 0 && (g_val - 0x80) < 0x81 &&
                   (b_val - 0x80) >= 0 && (b_val - 0x80) < 0x81) {
            // Single-pass additive with offset values
            set_particle_texture(-6);
            set_texture_mode(0);
            set_render_mode(2);
            draw_textured_quad_2d(/* viewport bounds + alpha */);
        } else {
            goto fallback;
        }
    } else {
        goto fallback;
    }

    goto done;

fallback:
    // Dual-pass: first additive, then variable blend
    sprite_draw_viewport_blend_a();
    sprite_draw_viewport_blend_b();

done:
    commit_blend(); // FUN_80382734
}

// ---------------------------------------------------------------------------
// Address: 0x800cee9c | Name: sprite_grid_reset_distortion
// Resets all 110 grid vertex distortion values to 1.0 (identity).
// Clears 5 floats per vertex at offset 0xC (fields 0xC..0x10), stride 0x54.
// ---------------------------------------------------------------------------
void sprite_grid_reset_distortion(void)
{
    float identity = DISTORTION_IDENTITY; // FLOAT_80614e04 = 1.0

    SpriteGridVertex* vtx = &g_sprite_grid_vertices[0][0];

    for (int i = 0; i < GRID_VERTEX_COUNT; i++) {
        vtx->distort_0 = identity;
        vtx->distort_1 = identity;
        vtx->distort_2 = identity;
        vtx->distort_3 = identity;
        vtx->distort_4 = identity;
        vtx = (SpriteGridVertex*)((u8*)vtx + GRID_VERTEX_STRIDE);
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800ceed0 | Name: sprite_grid_vertex_apply_force
// Applies force to a single grid vertex. Updates spring value (+0x28),
// calls lighting if active, then clamps displacement (+0x34) and intensity
// (+0x30) to maximum values.
// ---------------------------------------------------------------------------
void sprite_grid_vertex_apply_force(float spring_value, float displacement_delta,
                                    float intensity_delta, int vertex_ptr, int room_id)
{
    // Set spring target
    *(float*)(vertex_ptr + 0x28) = spring_value;

    // Call lighting setup if vertex has geometry data
    int result = setup_vertex_lighting(room_id, vertex_ptr, 0); // FUN_8017cd98
    if (result != 0) {
        // Update light direction from vertex state
        set_light_direction(*(float*)(vertex_ptr + 0x20),
                          *(float*)(vertex_ptr + 0x24),
                          spring_value - 1.0f); // FUN_8017ce5c
        commit_lighting(); // FUN_8017ce28
    }

    // Accumulate displacement (per-player slot)
    float max_displacement = DISTORTION_MAX_DISPLACEMENT; // FLOAT_80614e18
    if (*(int*)(vertex_ptr + 0x18) != 0) {
        int slot_offset = vertex_ptr + room_id * 4;
        *(float*)(slot_offset + 0x34) += displacement_delta;
        if (*(float*)(slot_offset + 0x34) > max_displacement) {
            *(float*)(slot_offset + 0x34) = max_displacement;
        }

        // Accumulate intensity
        float max_intensity = 1.0f; // FLOAT_80614e00
        *(float*)(vertex_ptr + 0x30) += intensity_delta;
        if (*(float*)(vertex_ptr + 0x30) > max_intensity) {
            *(float*)(vertex_ptr + 0x30) = max_intensity;
        }
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800cefcc | Name: sprite_grid_apply_particle_force
// Applies a particle's explosion force to the distortion grid. Computes grid
// cell from particle world position, applies distance-based falloff (squared),
// randomizes cell at large distances, then applies force to all 4 corners.
// ---------------------------------------------------------------------------
void sprite_grid_apply_particle_force(/* register */ float distance)
{
    // Get particle context (position, room)
    u64 context = get_scene_context(); // FUN_803826f0
    float* particle_pos = (float*)context;
    int room_id = (int)(context >> 32);

    // Compute falloff from distance
    float normalized_dist = distance - FORCE_DISTANCE_OFFSET; // FLOAT_80614e20
    if (normalized_dist < DISTORTION_IDENTITY) { // FLOAT_80614e04
        normalized_dist = DISTORTION_IDENTITY;
    }

    // Intensity = -(scale * dist - 1.0)
    float intensity = -(FORCE_FALLOFF_SCALE * normalized_dist - 1.0f); // FLOAT_80614e1c

    // Convert world position to grid cell coordinates
    u32 cell_x = (u32)(particle_pos[0] / (float)g_cell_width);
    u32 cell_y = (u32)(particle_pos[1] / (float)g_cell_height);

    // Displacement and intensity deltas (squared falloff)
    float disp_delta = intensity * (distance * intensity);
    float int_delta = FORCE_INTENSITY_SCALE * (intensity * intensity); // FLOAT_80614e28

    // Randomize cell selection at large distances (scatter effect)
    if (intensity > FORCE_SCATTER_THRESHOLD) { // FLOAT_80614e24
        u32 rng = get_random_u32();
        cell_x = (rng % 3 + cell_x) - 1;
        rng = get_random_u32();
        cell_y = cell_y - rng % 3;
    }

    // Look up grid cell
    SpriteGridCell* cell = sprite_grid_get_cell(cell_x, cell_y);

    // Only apply if cell exists and matches the particle's room
    if (cell != NULL && cell->room_id == room_id) {
        // Apply force to all 4 corner vertices
        sprite_grid_vertex_apply_force(distance, disp_delta, int_delta,
                                       cell->corner_tl, cell->room_id);
        sprite_grid_vertex_apply_force(distance, disp_delta, int_delta,
                                       cell->corner_tr, cell->room_id);
        sprite_grid_vertex_apply_force(distance, disp_delta, int_delta,
                                       cell->corner_bl, cell->room_id);
        sprite_grid_vertex_apply_force(distance, disp_delta, int_delta,
                                       cell->corner_br, cell->room_id);
    }

    commit_force(); // FUN_8038273c
}

// ---------------------------------------------------------------------------
// Address: 0x800cf180 | Name: sprite_grid_render_distortion
// MASSIVE: Renders the distortion grid as screen-space displacement.
// Phase 1: Computes displaced vertex positions using dual sin-table waves.
// Phase 2: If any displacement detected, renders grid cells as textured quads
// with per-vertex alpha (displacement intensity -> alpha channel).
// ---------------------------------------------------------------------------
void sprite_grid_render_distortion(void)
{
    begin_distortion_render(); // FUN_803826d0

    float threshold = DISTORTION_THRESHOLD;   // FLOAT_80614e38
    float freq_base = DISTORTION_FREQ_BASE;   // FLOAT_80614e34

    u32 cols = g_screen_width / g_cell_width;
    u32 rows = g_screen_height / g_cell_height;

    // Time-based wave parameters
    float time_norm = (float)g_frame_counter *
                      (1.0f / (float)g_screen_height); // normalized time
    float wave_a_speed = DISTORTION_WAVE_A_SPEED * time_norm; // FLOAT_80614e2c
    float wave_b_speed = DISTORTION_WAVE_B_SPEED * time_norm; // FLOAT_80614e30

    bool has_displacement = true;

    // Phase 1: Compute displaced positions for all vertices
    for (u32 row = 0; row <= rows; row++) {
        for (u32 col = 0; col <= cols; col++) {
            SpriteGridVertex* vtx = get_grid_vertex(row, col);

            // Check if any vertex exceeds threshold
            if (fabsf(vtx->distort_0) > threshold) {
                has_displacement = false; // flag that we need to render
            }

            if (vtx->boundary_x == 0) {
                // Compute dual-wave displacement
                float phase_a = freq_base * (float)row + freq_base * (float)col + wave_b_speed;
                int sin_idx_a = (int)(HEAT_FREQUENCY * phase_a + 0.5f) & 0x7FF;

                float phase_b = (float)(row / 2) + (wave_a_speed - (float)col);
                int sin_idx_b = (int)(HEAT_FREQUENCY * phase_b + 0.5f) & 0x7FF;

                // Displaced X = distortion * (wave_a * scale_a + wave_b * scale_b) + base_x
                vtx->displaced_x = (int)(vtx->distort_0 *
                    (DISTORTION_AMP_A * g_sin_table[sin_idx_a] +
                     freq_base * g_sin_table[sin_idx_b]) + (float)vtx->pixel_x);
            } else {
                // Boundary vertex: no displacement
                vtx->displaced_x = vtx->pixel_x;
            }

            // Y stays at base position (no vertical displacement in this mode)
            vtx->displaced_y = vtx->pixel_y;
        }
    }

    // Phase 2: Render distorted grid if any displacement exists
    if (!has_displacement) {
        // Early out: no visible distortion
    } else {
        set_particle_texture(-5); // screen capture texture
        set_render_mode(1);       // opaque
        set_texture_mode(0);

        // Iterate grid cells
        for (u32 row = 0; row < rows; row++) {
            for (u32 col = 0; col < cols; col++) {
                SpriteGridCell* cell = get_grid_cell(row, col);

                // Only render cells matching current player viewport
                if (cell->room_id != g_current_player_viewport_index) {
                    continue;
                }

                // Get 4 corner vertices
                SpriteGridVertex* vtx_tl = cell->corner_tl;
                SpriteGridVertex* vtx_tr = cell->corner_tr;
                SpriteGridVertex* vtx_bl = cell->corner_bl;
                SpriteGridVertex* vtx_br = cell->corner_br;

                // Per-player displacement slot
                int slot_offset = cell->room_id * 4 + 0x34;

                // Compute per-vertex alpha from displacement intensity
                int alpha_tl = (int)(DISTORTION_ALPHA_SCALE * *(float*)(vtx_tl + slot_offset));
                int alpha_tr = (int)(DISTORTION_ALPHA_SCALE * *(float*)(vtx_tr + slot_offset));
                int alpha_bl = (int)(DISTORTION_ALPHA_SCALE * *(float*)(vtx_bl + slot_offset));
                int alpha_br = (int)(DISTORTION_ALPHA_SCALE * *(float*)(vtx_br + slot_offset));

                // Skip if all alphas are zero
                if (alpha_tl <= 0 && alpha_tr <= 0 && alpha_bl <= 0 && alpha_br <= 0) {
                    continue;
                }

                // Draw quad with per-vertex color (0x7F7F7F | alpha << 24)
                draw_quad_4vertex(
                    DISTORTION_IDENTITY,
                    (float)vtx_tl->displaced_x, (float)vtx_tl->displaced_y,
                    (float)vtx_tr->displaced_x, (float)vtx_tr->displaced_y,
                    (float)vtx_bl->displaced_x, (float)vtx_bl->displaced_y,
                    (float)vtx_br->displaced_x, (float)vtx_br->displaced_y,
                    vtx_tl->pixel_x << 4, vtx_tl->pixel_y << 4,
                    vtx_tr->pixel_x << 4, vtx_tr->pixel_y << 4,
                    vtx_bl->pixel_x << 4, vtx_bl->pixel_y << 4,
                    vtx_br->pixel_x << 4, vtx_br->pixel_y << 4,
                    (float)vtx_br->displaced_y,
                    alpha_tl << 24 | 0x7F7F7F,
                    alpha_tr << 24 | 0x7F7F7F,
                    alpha_bl << 24 | 0x7F7F7F,
                    alpha_br << 24 | 0x7F7F7F); // FUN_80170dbc
            }
        }
    }

    end_distortion_render(); // FUN_8038271c
}
