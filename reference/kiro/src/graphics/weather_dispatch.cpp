// Address: 0x800d0a5c - 0x800d1774 | Weather dispatch & composite system | Date: 2026-08-25
// Batch 283: Weather rendering pipeline dispatchers, animated grid mesh, tiled texture
// overlays, sin-table math helper, and particle pool management.
//
// This file contains the high-level weather rendering control flow:
// - Post-process compositor: fog, color correction, distortion, overlays
// - Pre-render pass: heat shimmer + non-type-5 sub-particles
// - Post-render pass: full weather effect dispatcher (entity glow, rain, grid distortion)
// - Animated 10x10 grid mesh renderer (rain curtain / fog sheet)
// - Tiled texture overlay system (static noise, tile patterns)
// - Weather particle pool init/reset

#include "common.h"
#include "graphics.h"
#include "weather.h"
#include "sprite.h"

// ============================================================================
// 0x800d0a5c | weather_post_process_composite
// Main post-process compositor for weather effects. Reads from the active
// player's weather context (indexed by DAT_80611d54+0x214) and dispatches:
// - Viewport fog overlay
// - Color correction (mode 1 = darken with alpha clamp 0x7F, mode 2 = lighten 0xFF)
// - Blend passes A/B for overlay transitions
// - Particle overlay, heat distortion, viewport mask
// - Tiled background texture (texture 0x333)
// - Noise overlay (0x3B5) and tiled overlay (0x333)
// - Solid color fill
// ============================================================================
void weather_post_process_composite(void) {
    int has_color_a;
    int has_color_b;
    u32* emitter;
    int player_idx;
    int prev_depth_test;
    u32 tex_id;
    int color_mode;
    int r, g, b, a;

    FUN_803826e8(); // GX state push

    player_idx = *(int*)(DAT_80611d54 + 0x214);
    FUN_8016e000(0);  // disable texturing
    prev_depth_test = FUN_8016e020(0);
    FUN_8016e020((char)prev_depth_test);
    FUN_8016e8ac(0);  // blend off

    // Fog overlay
    if ((&DAT_80569a24)[player_idx * 0x7a] != 0) {
        sprite_draw_viewport_fog((&DAT_80569a24)[player_idx * 0x7a]);
    }

    // Check if color correction group A is active (RGB channels)
    int color_a_r = (&DAT_80569a2c)[player_idx * 0x7a];
    has_color_a = FALSE;
    if (color_a_r > 0 ||
        (int)(&DAT_80569a30)[player_idx * 0x7a] > 0 ||
        (int)(&DAT_80569a34)[player_idx * 0x7a] > 0) {
        has_color_a = TRUE;
    }

    // Check if color correction group B is active
    int color_b_r = (&DAT_80569a3c)[player_idx * 0x7a];
    has_color_b = FALSE;
    if (color_b_r > 0 ||
        (int)(&DAT_80569a40)[player_idx * 0x7a] > 0 ||
        (int)(&DAT_80569a44)[player_idx * 0x7a] > 0) {
        has_color_b = TRUE;
    }

    color_mode = (&DAT_80569a28)[player_idx * 0x7a];

    if (color_mode == 0) {
        // No color correction mode — just do blend passes
        if (has_color_a) {
            FUN_8016ee98();
            sprite_draw_viewport_blend_b();
        }
        if (has_color_b) {
            sprite_draw_viewport_blend_b();
        }
    }
    else if (has_color_a) {
        // Mode 1: darken — clamp channels to 0x7F
        a = 0x7f;
        if ((int)(&DAT_80569a38)[player_idx * 0x7a] < 0x7f) {
            a = (&DAT_80569a38)[player_idx * 0x7a];
        }
        b = 0x7f;
        if ((int)(&DAT_80569a34)[player_idx * 0x7a] < 0x7f) {
            b = (&DAT_80569a34)[player_idx * 0x7a];
        }
        g = 0x7f;
        if ((int)(&DAT_80569a30)[player_idx * 0x7a] < 0x7f) {
            g = (&DAT_80569a30)[player_idx * 0x7a];
        }
        r = 0x7f;
        if (color_a_r < 0x7f) {
            r = color_a_r;
        }
        sprite_apply_color_correction(color_mode, 1, r, g, b, a);

        if (has_color_b) {
            sprite_draw_viewport_blend_b();
        }
    }
    else if (has_color_b) {
        // Mode 2: lighten — clamp channels to 0xFF
        b = 0xff;
        if ((int)(&DAT_80569a44)[player_idx * 0x7a] < 0xff) {
            b = (&DAT_80569a44)[player_idx * 0x7a];
        }
        g = 0xff;
        if ((int)(&DAT_80569a40)[player_idx * 0x7a] < 0xff) {
            g = (&DAT_80569a40)[player_idx * 0x7a];
        }
        r = 0xff;
        if (color_b_r < 0xff) {
            r = color_b_r;
        }
        sprite_apply_color_correction(color_mode, 2, r, g, b, 0x7f);
    }
    else {
        // Fallback: simple blend pass A
        sprite_draw_viewport_blend_a();
    }

    // Particle overlay effect
    if ((&DAT_80569a90)[player_idx * 0x7a] != 0) {
        weather_render_particle_overlay();
    }

    // Heat distortion (only if intensity != 0.0)
    if (FLOAT_80614e04 != (f32)(&DAT_80569a1c)[player_idx * 0x7a]) {
        sprite_draw_heat_distortion();
    }

    // Viewport mask (splitscreen border)
    if ((int)(&DAT_80569a54)[player_idx * 0x7a] > 0) {
        sprite_draw_viewport_mask();
    }

    // Tiled background texture (e.g. snow/rain texture tile)
    if ((&DAT_80569a6c)[player_idx * 0x7a] != 0) {
        int prev_tex = FUN_8016f00c(1);
        u32* viewport = DAT_80611748;
        tex_id = sprite_get_texture_id(0x333);
        sprite_draw_tiled_background(tex_id, *viewport, viewport[5], viewport[1], viewport[6]);
        FUN_8016f00c(prev_tex);
    }

    // Noise overlay (static/interference)
    if ((&DAT_80569ab8)[player_idx * 0x7a] != 0) {
        weather_draw_noise_overlay();
    }

    // Tiled texture overlay
    if ((&DAT_80569abc)[player_idx * 0x7a] != 0) {
        weather_draw_tiled_overlay();
    }

    // Solid color fill (flash/fade)
    if ((&DAT_80569a70)[player_idx * 0x7a] != 0) {
        sprite_fill_viewport_color((&DAT_80569a70)[player_idx * 0x7a]);
    }

    FUN_8016e000(1);  // re-enable texturing
    FUN_8016e020((char)prev_depth_test);
    FUN_80382734();   // GX state pop
}

// ============================================================================
// 0x800d0df0 | weather_render_pre_pass
// Weather pre-render pass. Renders heat shimmer effect, then iterates 5
// sub-particle layers for the active player context, rendering any that are
// NOT type 5 (type 5 is deferred to post-pass).
// ============================================================================
void weather_render_pre_pass(void) {
    int layer_type;
    int player_idx;
    int i;
    int sub_offset;

    FUN_803826f0(); // GX push

    player_idx = *(int*)(DAT_80611d54 + 0x214);

    weather_render_heat_shimmer();

    i = 0;
    sub_offset = 0;
    do {
        int ctx_offset = sub_offset + player_idx * 0x1e8;
        layer_type = *(int*)((int)&DAT_80569a10 + ctx_offset + 0xb0);

        if (layer_type != 0 && layer_type != 5) {
            FUN_800d1848((int*)((int)&DAT_80569a10 + ctx_offset + 0xb0));
        }

        i++;
        sub_offset += 0x34;
    } while (i < 5);

    FUN_8038273c(); // GX pop
}

// ============================================================================
// 0x800d0e70 | weather_render_post_pass
// Main weather post-render dispatcher. Renders all deferred weather effects:
// - Type-5 sub-particles (deferred from pre-pass)
// - Lightning/flash effect (via FUN_80217668 if weather param set)
// - Entity glow silhouettes
// - Rain strip rendering
// - Sprite grid distortion
// - Precipitation overlay
// - Viewport quad overlays
// - Rain streaks
// - Secondary heat distortion
// - Weather gate (level-specific rain gate rendering)
// ============================================================================
void weather_render_post_pass(void) {
    int player_idx;
    u32* weather_ctx;
    int prev_depth;
    int i;
    int sub_offset;

    FUN_803826ec(); // GX push

    player_idx = *(int*)(DAT_80611d54 + 0x214);
    weather_ctx = &DAT_80569a10 + player_idx * 0x7a;

    FUN_8016e000(0);  // disable texturing
    prev_depth = FUN_8016e020(0);
    FUN_8016e020((char)prev_depth);

    // Render type-5 sub-particles (deferred from pre-pass)
    i = 0;
    sub_offset = 0;
    do {
        if (*(int*)((int)weather_ctx + sub_offset + 0xb0) == 5) {
            FUN_800d1848((int*)((int)weather_ctx + sub_offset + 0xb0));
        }
        i++;
        sub_offset += 0x34;
    } while (i < 5);

    // Lightning/flash effect (only if weather param is set)
    if (*(int*)(&DAT_80569bcc + player_idx * 0x1e8) != 0) {
        FUN_80217668(/* pass-through params */);
    }

    // Entity glow outlines
    if ((&DAT_80569aac)[player_idx * 0x7a] != 0) {
        weather_render_entity_glow();
    }

    // Rain vertical strips
    if ((&DAT_80569ab0)[player_idx * 0x7a] != 0) {
        weather_render_rain_strips();
    }

    // Sprite grid distortion effect
    sprite_grid_render_distortion();

    // Precipitation overlay rendering
    if ((&DAT_80569bc4)[player_idx * 0x7a] != 0) {
        FUN_800d22b0((int)weather_ctx);
    }

    // Viewport quad overlay (first pass)
    if ((int)(&DAT_80569a20)[player_idx * 0x7a] > 0) {
        sprite_draw_viewport_quad();
    }

    // Rain streak lines
    if ((&DAT_80569a84)[player_idx * 0x7a] != 0) {
        weather_render_rain_streaks();
    }

    // Viewport quad overlay (second pass)
    if ((&DAT_80569ab4)[player_idx * 0x7a] != 0) {
        sprite_draw_viewport_quad();
    }

    // Secondary heat distortion (higher intensity threshold)
    if (FLOAT_80614e04 < (f32)(&DAT_80569bd0)[player_idx * 0x7a]) {
        sprite_draw_heat_distortion();
    }

    // Weather gate rendering (level-specific: levels 0xB, 0x11, 5, 0x1A)
    if (DAT_805015bd == '\n' &&
        (DAT_80612614 == 0xb || DAT_80612614 == 0x11 ||
         DAT_80612614 == 5 || DAT_80612614 == 0x1a)) {
        weather_render_gate((int)weather_ctx);
    }

    FUN_8016e000(1);  // re-enable texturing
    FUN_8016e020((char)prev_depth);
    FUN_80382738();   // GX pop
}

// ============================================================================
// 0x800d100c | weather_sin_table_lookup
// Pure math helper: computes an oscillating sine-based value for weather
// particle vertex animation. Uses the global sin table (DAT_80583228, 2048
// entries) with phase offset from frame counter ratio.
// Returns: amplitude * sin(frequency * frame_ratio + phase_offset)
// ============================================================================
f64 weather_sin_table_lookup(f64 phase_a, f64 phase_b, f64 frequency, u32 amplitude) {
    f32 frame_ratio = (f32)((f64)CONCAT44(0x43300000, DAT_80611928 ^ 0x80000000) - DOUBLE_80614df8) *
                      (FLOAT_80614e00 /
                       (f32)((f64)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80614df8));

    f32 phase = FLOAT_80614e0c * (frame_ratio * (f32)frequency + (f32)(phase_a - phase_b)) +
                FLOAT_80614de8;

    int table_idx = (int)phase & 0x7ff;

    return (f64)((f32)((f64)CONCAT44(0x43300000, amplitude ^ 0x80000000) - DOUBLE_80614df8) *
                 (&DAT_80583228)[table_idx]);
}

// ============================================================================
// 0x800d10a4 | weather_render_animated_grid
// Renders a 10x10 animated quad grid mesh with per-vertex sine-based offsets.
// Used for rain curtain or animated fog/mist sheets. Each vertex position is
// modulated by weather_sin_table_lookup. Draws with color from params (byte-swapped).
// param_3 controls depth mode (0=no depth write, else=depth+alpha blend 8).
// ============================================================================
void weather_render_animated_grid(u32 param_1, u32 param_2, int param_3) {
    f32 sin_val;
    u32 color_packed;
    u32 vp_x, vp_y;
    u32 amplitude;
    int width, height;
    int i, j;
    f64 phase;
    f64 cell_w, cell_h, tex_cell_w, tex_cell_h;

    u64 result = FUN_803826c8(); // returns color in high word, amplitude in low
    amplitude = (u32)(result >> 32);
    color_packed = (u32)result;
    // Byte-swap color (ABGR -> RGBA)
    color_packed = color_packed << 24 | (color_packed & 0xff00) << 8 |
                   color_packed >> 24 | (color_packed >> 8) & 0xff00;
    phase = /* extraout_f1 */;

    FUN_8016f00c(6); // additive blend texture mode

    if (param_3 == 0) {
        gx_bind_material_by_index_w(-6);  // no depth write
        FUN_8016e8ac(0);   // blend off
    } else {
        FUN_8016e77c(-6);  // depth write
        FUN_8016e8ac(8);   // alpha blend
    }

    vp_x = *DAT_80611748;
    vp_y = DAT_80611748[5];
    width = DAT_80611748[1] - vp_x;
    height = DAT_80611748[6] - vp_y;

    // Compute cell sizes (viewport / 10, with 16x texel scale)
    cell_w = (f64)((f32)width / FLOAT_80614e1c);       // width / 10.0
    cell_h = (f64)((f32)height / FLOAT_80614e1c);      // height / 10.0
    tex_cell_w = (f64)((f32)(width * 16) / FLOAT_80614e1c);
    tex_cell_h = (f64)((f32)(height * 16) / FLOAT_80614e1c);

    // Pre-compute first row of sin offsets
    u32 col = 0;
    int offset = 0;
    do {
        f64 sin_result = weather_sin_table_lookup(
            (f64)(f32)((f64)CONCAT44(0x43300000, col ^ 0x80000000) - DOUBLE_80614df8),
            (f64)FLOAT_80614e04, phase, amplitude);
        col++;
        *(f32*)((int)&DAT_8056dce4 + offset) = (f32)sin_result;
        offset += 4;
    } while ((int)col < 10);

    // Render 10x10 grid
    u32 row = 0;
    u32 vp_y_tex = vp_y << 4 ^ 0x80000000;
    do {
        f64 prev_sin = (f64)DAT_8056dce4;
        f64 next_row_sin = weather_sin_table_lookup(
            (f64)FLOAT_80614e04,
            (f64)(f32)((f64)CONCAT44(0x43300000, (row + 1) ^ 0x80000000) - DOUBLE_80614df8),
            phase, amplitude);

        DAT_8056dce4 = (f32)next_row_sin;

        // Compute row Y positions (screen + texture space)
        u32 y_next_tex = (u32)(tex_cell_h * (f64)(f32)((row + 1) - DOUBLE_80614df8) + vp_y_tex);
        int y_curr_tex = (int)(tex_cell_h * (f64)(f32)(row - DOUBLE_80614df8) + vp_y_tex);
        f64 y_next_screen = (f64)(f32)(cell_h * (f64)(f32)((row + 1) - DOUBLE_80614df8) + vp_y);
        f64 y_curr_screen = (f64)(f32)(cell_h * (f64)(f32)(row - DOUBLE_80614df8) + vp_y);

        u32 col_inner = 0;
        int sin_offset = 0;
        f64 curr_sin = prev_sin;

        do {
            f64 next_sin = (f64)*(f32*)((int)&DAT_8056dce8 + sin_offset);
            f64 new_col_sin = weather_sin_table_lookup(
                (f64)(f32)((col_inner + 1) - DOUBLE_80614df8),
                (f64)(f32)((row + 1) - DOUBLE_80614df8),
                phase, amplitude);

            *(f32*)((int)&DAT_8056dce8 + sin_offset) = (f32)new_col_sin;

            // Compute vertex positions with sin offset
            u32 vp_start_x = *DAT_80611748;
            f32 x_curr = cell_w * (f64)(f32)(col_inner) + vp_start_x;
            f32 x_next = cell_w * (f64)(f32)(col_inner + 1) + vp_start_x;
            int tx_curr = (int)(tex_cell_w * (f64)(f32)(col_inner) + (vp_start_x + amplitude) * 16);
            int tx_next = (int)(tex_cell_w * (f64)(f32)(col_inner + 1) + (vp_start_x + amplitude) * 16);

            // Draw quad with sin-offset vertices
            FUN_80170dbc(
                (f64)FLOAT_80614de8,
                (f64)(f32)(curr_sin + x_curr), y_curr_screen,
                (f64)(f32)(next_sin + x_next), y_curr_screen,
                (f64)(f32)(next_row_sin + x_curr), y_next_screen,
                (f64)(f32)(new_col_sin + x_next),
                y_curr_tex, y_curr_tex, tx_next, y_curr_tex,
                y_curr_tex, y_next_tex, tx_next, y_next_tex,
                y_next_screen, color_packed, color_packed, color_packed, color_packed
            );

            col_inner++;
            sin_offset += 4;
            curr_sin = next_sin;
        } while ((int)col_inner < 10);

        row++;
    } while ((int)row < 10);

    FUN_8016f00c(0);   // reset texture mode
    FUN_8016e8ac(4);   // restore blend
    FUN_80382714();    // GX pop
}

// ============================================================================
// 0x800d1578 | weather_draw_tiled_texture
// Tiles a texture sprite across a rectangular region with randomized UV offsets.
// Each tile is texture_size x texture_size pixels. UV origin is randomized per
// tile to create visual noise/variety.
// ============================================================================
void weather_draw_tiled_texture(u32 x_start, u32 y_start, int x_end, int y_end, int texture_id) {
    int tex_handle;
    u32 tile_size;
    u32 rand_u, rand_v;
    int x, y;

    FUN_803826d0(); // GX push (returns start coords in result)

    tex_handle = sprite_get_texture_id(texture_id);
    tile_size = (u32)*(u16*)(*DAT_80610a20 + tex_handle * 0x58 + 0x4e);

    FUN_8016e77c(tex_handle); // set texture

    for (x = (int)x_start; x < x_end; x += tile_size) {
        for (y = (int)y_start; y < y_end; y += tile_size) {
            // Random UV offset within tile (aligned to tile boundary)
            rand_u = FUN_8013109c();
            rand_u = ((tile_size - 1) & rand_u) * 0x10;
            rand_v = FUN_8013109c();
            rand_v = ((tile_size - 1) & rand_v) * 0x10;

            FUN_80170410(x, y, x + tile_size, y + tile_size,
                         rand_u, rand_v,
                         rand_u + tile_size * 0x10,
                         rand_v + tile_size * 0x10);
        }
    }

    FUN_8038271c(); // GX pop
}

// ============================================================================
// 0x800d1648 | weather_draw_noise_overlay
// Draws a noise/static overlay using texture 0x3B5 (TV static pattern).
// Tiles across the full viewport with blend mode 3 (modulate).
// ============================================================================
void weather_draw_noise_overlay(void) {
    int prev_blend, prev_tex;

    FUN_803826f0(); // GX push

    prev_blend = FUN_8016e8ac(0);
    prev_tex = FUN_8016f00c(3); // modulate blend

    weather_draw_tiled_texture(
        *DAT_80611748, DAT_80611748[5],
        DAT_80611748[1], DAT_80611748[6],
        0x3b5  // noise texture ID
    );

    FUN_8016e8ac(prev_blend);
    FUN_8016f00c(prev_tex);
    FUN_8038273c(); // GX pop
}

// ============================================================================
// 0x800d16c4 | weather_draw_tiled_overlay
// Draws a tiled texture overlay using texture 0x333 (generic tile pattern).
// Same structure as noise overlay but with a different texture.
// ============================================================================
void weather_draw_tiled_overlay(void) {
    int prev_blend, prev_tex;

    FUN_803826f0(); // GX push

    prev_blend = FUN_8016e8ac(0);
    prev_tex = FUN_8016f00c(3); // modulate blend

    weather_draw_tiled_texture(
        *DAT_80611748, DAT_80611748[5],
        DAT_80611748[1], DAT_80611748[6],
        0x333  // tile texture ID
    );

    FUN_8016e8ac(prev_blend);
    FUN_8016f00c(prev_tex);
    FUN_8038273c(); // GX pop
}

// ============================================================================
// 0x800d1740 | weather_particle_pool_reset
// Clears the weather particle pool: 32 entries of 0x18 bytes each.
// Sets the first field (active flag) of each entry to 0.
// Also resets global particle count (DAT_80611dd8).
// Base address: 0x8056dd24 (computed as -0x7FA922DC)
// ============================================================================
void weather_particle_pool_reset(void) {
    int i = 32;
    while (TRUE) {
        i--;
        if (i < 0) break;
        *(u32*)(i * 0x18 + 0x8056dd24) = 0; // clear active flag
    }
    DAT_80611dd8 = 0; // reset active particle count
}

// ============================================================================
// 0x800d1774 | weather_particle_pool_init
// Initializes the weather particle pool by calling reset.
// Called from sprite_grid_init during level load.
// ============================================================================
void weather_particle_pool_init(void) {
    weather_particle_pool_reset();
}
