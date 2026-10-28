// Address: 0x800cf61c - 0x800d0a20 | Weather rendering & post-process effects | Date: 2026-08-25
// Batch 282: Weather system update + individual render passes (heat shimmer, rain streaks,
// entity glow/silhouette, snow/rain strips, glow blur, lookup table init, setters)
//
// This file contains the weather visual effects rendering pipeline:
// - Per-frame weather context update driving 6 independent weather layers
// - Heat shimmer: sine-wave UV distortion on particle emitter quads
// - Particle overlay: alpha-blended particle texture overlay with screen mapping
// - Rain streak render: horizontal/vertical line pairs across viewport
// - Entity glow/silhouette: expanding outline passes per visible entity
// - Rain/snow strip render: GX FIFO vertical strip draw with sin-table positions
// - Glow blur pass: multi-iteration alpha glow expansion
// - Lookup table initialization and parameter setters

#include "common.h"
#include "graphics.h"
#include "particle.h"
#include "weather.h"

// ============================================================================
// 0x800cf61c | weather_render_heat_shimmer
// Renders per-particle heat shimmer distortion by offsetting quad UVs with a
// double-sine wave lookup. Uses particle emitter start/end range to define the
// vertical extent of the effect. Only renders if emitter has active range.
// ============================================================================
void weather_render_heat_shimmer(void) {
    int offset;
    u32* emitter;
    u32 idx;
    f64 intensity;
    f64 sin_a, sin_b;
    f64 amplitude, freq, phase_a, phase_b, table_scale, table_base;

    FUN_803826e8(); // GX state push

    emitter = particle_get_current_player_emitter();

    if ((int)emitter[0x19] < (int)emitter[0x1a]) {
        // Compute intensity from global frame counter ratio
        intensity = (f64)((f32)((f64)CONCAT44(0x43300000, DAT_80611928 ^ 0x80000000) -
                                DOUBLE_80614df8) *
                          (FLOAT_80614e00 /
                           (f32)((f64)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                                 DOUBLE_80614df8)));

        FUN_8016e8ac(0);        // set blend mode
        FUN_8016f00c(6);        // set texture format
        FUN_8016ee98();         // flush render state
        FUN_8017c658();         // begin quad batch
        FUN_8016e77c(-6);       // set z depth

        amplitude = (f64)(f32)((f64)FLOAT_80614e2c * intensity);
        phase_b   = (f64)(f32)((f64)FLOAT_80614e44 * intensity);
        freq      = (f64)FLOAT_80614e48;
        table_scale = (f64)FLOAT_80614e3c;
        table_base  = (f64)FLOAT_80614e0c;
        phase_a   = (f64)FLOAT_80614e4c;
        sin_a     = (f64)FLOAT_80614de8;
        sin_b     = (f64)FLOAT_80614e50;

        for (idx = emitter[0x1b]; (int)idx < (int)emitter[0x1c]; idx += 2) {
            f64 t = (f64)((f32)((f64)CONCAT44(0x43300000, idx ^ 0x80000000) - DOUBLE_80614df8) /
                          (f32)((f64)CONCAT44(0x43300000, DAT_80611748[8] ^ 0x80000000) -
                                DOUBLE_80614df8));

            // Double-sine wave: two sin table lookups combined and scaled
            offset = (int)(freq *
                (f64)(f32)(table_scale *
                    (f32)(&DAT_80583228)[(int)(table_base *
                        (f32)(phase_a * t + phase_b) + sin_a) & 0x7ff] +
                    (f64)(f32)(table_scale *
                        (f32)(&DAT_80583228)[(int)(table_base *
                            (f32)(sin_b * t + amplitude) + sin_a) & 0x7ff])));

            FUN_80170410(
                emitter[0x19] + offset, idx,
                emitter[0x1a] + offset, idx + 2,
                emitter[0x19] << 4, (idx + 1) * 0x10,
                emitter[0x1a] << 4, (idx + 3) * 0x10
            );
        }

        FUN_8017c670();  // end quad batch
        FUN_8016f24c(8, *DAT_80611748, DAT_80611748[5],
                     DAT_80611748[1] + 1, DAT_80611748[6] + 1, 0);
        FUN_8016e8ac(0);
    }

    FUN_80382734(); // GX state pop
}

// ============================================================================
// 0x800cf8a0 | weather_render_particle_overlay
// Renders a screen-space particle overlay texture with alpha scaling based on
// the emitter's intensity parameter. Creates a texture-mapped quad covering
// the viewport area defined by emitter screen coords.
// ============================================================================
void weather_render_particle_overlay(void) {
    u32* emitter;
    int prev_blend, prev_tex;
    int prev_shader;
    int local_68[2];
    u32 local_60, uStack_5c, local_58, uStack_54;
    u32 local_50, uStack_4c, local_48, uStack_44;
    u32 local_40, uStack_3c, local_38, uStack_34;
    u32 local_30, uStack_2c, local_28, uStack_24;

    FUN_803826e8(); // GX state push

    emitter = particle_get_current_player_emitter();
    FUN_803825c0((f64)(FLOAT_80614e40 * (f32)emitter[0x25])); // set alpha

    particle_get_current_player_emitter();
    emitter = particle_get_current_player_emitter();

    if (FLOAT_80614e54 <= (f32)emitter[0x25] &&
        (emitter[0x26] & 0xffffff00) != 0) {

        prev_blend = FUN_8016e8ac(0);
        FUN_8016ee98();
        emitter = particle_get_current_player_emitter();
        FUN_8016e010((f64)(f32)emitter[0x22]); // set alpha scale
        FUN_8016e77c(-7);                       // z depth
        prev_tex = FUN_8016f00c(0xe);
        prev_shader = FUN_8016e490(-0x7fe70fdc); // texture handle

        FUN_801706cc(DOUBLE_80614e58, DOUBLE_80614e60,
                     DOUBLE_80614e60, DOUBLE_80614e58, 0, 0, 0xa0, 0xa0);

        FUN_8016e57c();
        FUN_8016ee98();
        FUN_8017bc4c(0xa0, 0xa0, local_68);
        FUN_8017bbe4(local_68[0], 3);
        FUN_8016e77c(local_68[0]);
        FUN_8016e490(-0x7fe70eec);
        FUN_8016f00c(2);

        // Compute normalized screen UV coordinates from viewport bounds
        uStack_44 = DAT_806109c8 ^ 0x80000000;
        uStack_4c = DAT_80611748[5] ^ 0x80000000;
        uStack_2c = DAT_80611748[6] ^ 0x80000000;
        uStack_3c = DAT_80611748[1] ^ 0x80000000;

        FUN_801706cc(
            (f64)((f32)((f64)CONCAT44(0x43300000, *DAT_80611748 ^ 0x80000000) - DOUBLE_80614df8) /
                  (f32)((f64)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) - DOUBLE_80614df8)),
            (f64)(FLOAT_80614e00 -
                  (f32)((f64)CONCAT44(0x43300000, DAT_80611748[5] ^ 0x80000000) - DOUBLE_80614df8) /
                  (f32)((f64)CONCAT44(0x43300000, DAT_806109c8 ^ 0x80000000) - DOUBLE_80614df8)),
            (f64)((f32)((f64)CONCAT44(0x43300000, DAT_80611748[1] ^ 0x80000000) - DOUBLE_80614df8) /
                  (f32)((f64)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) - DOUBLE_80614df8)),
            (f64)(FLOAT_80614e00 -
                  (f32)((f64)CONCAT44(0x43300000, DAT_80611748[6] ^ 0x80000000) - DOUBLE_80614df8) /
                  (f32)((f64)CONCAT44(0x43300000, DAT_806109c8 ^ 0x80000000) - DOUBLE_80614df8)),
            *DAT_80611748, DAT_80611748[5], DAT_80611748[1], DAT_80611748[6]
        );

        FUN_8017c454(local_68[0]);
        FUN_8016e490(prev_shader);
        FUN_8018ec78();
        FUN_8016e010((f64)FLOAT_80614e04);
        FUN_8016e8ac(prev_blend);
        FUN_8016f00c(prev_tex);
        FUN_8016ee98();
    }

    FUN_80382734(); // GX state pop
}

// ============================================================================
// 0x800cfaf8 | weather_system_update
// Per-frame update for 6 weather layer contexts. Each context is 0x1E8 bytes.
// Handles weather intensity tick, 5 sub-particle layers per context, and
// precipitation overlay rendering. Resets sprite grid distortion at end.
// Guarded by level ID check (DAT_80612614 <= 0x65).
// ============================================================================
void weather_system_update(void) {
    char level_flags;
    u32* viewport;
    int i, j;
    int layer_offset;
    f32* weather_ctx;

    FUN_803826e0(); // GX state push (variant)

    if (DAT_80612614 == 0x65 || DAT_80612614 < 0x65) {
        f64 one_f = (f64)FLOAT_80614e00;
        f64 zero_f = (f64)FLOAT_80614e04;
        int ctx_index = 0;
        layer_offset = 0;

        do {
            int weather_data_flag = DAT_80611ddc;
            weather_ctx = (f32*)((int)&DAT_80569a10 + layer_offset);

            // Initialize context defaults
            *weather_ctx = (f32)one_f;
            *(u32*)((int)&DAT_80569a14 + layer_offset) = 1;
            *(u32*)((int)&DAT_80569a18 + layer_offset) = 0x3f;
            *(f32*)((int)&DAT_80569a1c + layer_offset) = (f32)zero_f;
            *(u32*)((int)&DAT_80569a58 + layer_offset) = 0;
            *(u32*)((int)&DAT_80569a84 + layer_offset) = 0;
            *(u32*)((int)&DAT_80569a88 + layer_offset) = 0;
            *(f32*)((int)&DAT_80569a8c + layer_offset) = (f32)zero_f;
            *(u32*)((int)&DAT_80569a20 + layer_offset) = 0;

            if (weather_data_flag == 0) {
                *(u32*)((int)&DAT_80569a24 + layer_offset) = 0;
            }

            level_flags = DAT_805015bd;
            viewport = DAT_80611748;

            // Clear remaining fields
            *(u32*)((int)&DAT_80569a28 + layer_offset) = 0;
            *(u32*)((int)&DAT_80569a2c + layer_offset) = 0;
            *(u32*)((int)&DAT_80569a30 + layer_offset) = 0;
            *(u32*)((int)&DAT_80569a34 + layer_offset) = 0;
            *(u32*)((int)&DAT_80569a38 + layer_offset) = 0;
            *(u32*)((int)&DAT_80569a3c + layer_offset) = 0;
            *(u32*)((int)&DAT_80569a40 + layer_offset) = 0;
            *(u32*)((int)&DAT_80569a44 + layer_offset) = 0;
            *(u32*)((int)&DAT_80569a48 + layer_offset) = 0;
            *(u32*)((int)&DAT_80569a4c + layer_offset) = 0;
            *(u32*)((int)&DAT_80569a50 + layer_offset) = 0;
            *(u32*)((int)&DAT_80569a54 + layer_offset) = 0;

            // Store viewport bounds into context
            *(u32*)((int)&DAT_80569a74 + layer_offset) = viewport[1];
            *(u32*)((int)&DAT_80569a78 + layer_offset) = *viewport;
            *(u32*)((int)&DAT_80569a7c + layer_offset) = viewport[6];
            *(u32*)((int)&DAT_80569a80 + layer_offset) = viewport[5];

            *(u32*)((int)&DAT_80569a6c + layer_offset) = 0;
            *(u32*)((int)&DAT_80569a70 + layer_offset) = 0;
            *(u32*)((int)&DAT_80569a90 + layer_offset) = 0;
            *(u32*)((int)&DAT_80569aac + layer_offset) = 0;
            *(u32*)((int)&DAT_80569ab4 + layer_offset) = 0;
            *(u32*)((int)&DAT_80569ab8 + layer_offset) = 0;
            *(u32*)((int)&DAT_80569abc + layer_offset) = 0;
            *(u32*)((int)&DAT_80569ab0 + layer_offset) = 0;
            *(f32*)((int)&DAT_80569bd0 + layer_offset) = (f32)zero_f;

            // Tick weather intensity for specific level IDs when flag set
            if (level_flags == '\n' &&
                (DAT_80612614 == 0xb || DAT_80612614 == 0x11 ||
                 DAT_80612614 == 5 || DAT_80612614 == 0x1a)) {
                weather_intensity_tick((int)weather_ctx);
            }

            FUN_800d354c(); // weather particle spawn/update

            // Update 5 sub-particle layers (0x34 bytes each)
            j = 0;
            int sub_offset = 0;
            do {
                if (*(int*)((int)weather_ctx + sub_offset + 0xb0) != 0) {
                    FUN_800d17e8((u32*)((int)weather_ctx + sub_offset + 0xb0));
                }
                j++;
                sub_offset += 0x34;
            } while (j < 5);

            // Render precipitation overlay if active
            if (*(int*)((int)&DAT_80569bc4 + layer_offset) != 0) {
                FUN_800d2250((int)weather_ctx);
            }

            ctx_index++;
            layer_offset += 0x1e8;
        } while (ctx_index < 6);

        sprite_grid_reset_distortion();

        if (DAT_80611ddc != 0) {
            DAT_80611ddc = 0;
        }
    }

    FUN_8038272c(); // GX state pop (variant)
}

// ============================================================================
// 0x800cfce4 | weather_render_rain_streaks
// Renders rain streak lines as expanding horizontal/vertical pairs.
// Iterates N times (count from FUN_803826d8) drawing line quads at increasing
// offsets from viewport edges. Used for rain/snow streak overlay.
// ============================================================================
void weather_render_rain_streaks(void) {
    int count;
    int prev_blend, prev_tex;
    u32 i;
    f64 extraout_f1;
    f64 streak_width;

    count = FUN_803826d8();
    streak_width = extraout_f1; // returned in f1 from above call

    prev_blend = FUN_8016e8ac(0);
    prev_tex = FUN_8016f00c(1);
    FUN_8016e000(1);      // enable alpha test
    FUN_8016e77c(-6);     // z depth
    FUN_8016e000(0);      // disable texturing

    f64 base = DOUBLE_80614df8;

    for (i = 1; (int)i < count; i++) {
        f64 offset = (f64)(f32)((f64)(f32)((f64)CONCAT44(0x43300000, i ^ 0x80000000) - base) *
                                streak_width);

        // Horizontal streak (left edge inward)
        FUN_80170410(
            (int)((f32)((f64)CONCAT44(0x43300000, *DAT_80611748 ^ 0x80000000) - base) - (f32)offset),
            DAT_80611748[5], DAT_80611748[1], DAT_80611748[6],
            *DAT_80611748 << 4, DAT_80611748[5] << 4,
            DAT_80611748[1] << 4, DAT_80611748[6] << 4
        );

        // Vertical streak (top edge inward)
        FUN_80170410(
            *DAT_80611748, DAT_80611748[5],
            (int)((f32)((f64)CONCAT44(0x43300000, DAT_80611748[1] ^ 0x80000000) - base) + (f32)offset),
            DAT_80611748[6],
            *DAT_80611748 << 4, DAT_80611748[5] << 4,
            DAT_80611748[1] << 4, DAT_80611748[6] << 4
        );
    }

    FUN_8016e8ac(prev_blend);
    FUN_8016f00c(prev_tex);
    FUN_80382724(); // GX state pop
}

// ============================================================================
// 0x800cff24 | weather_render_entity_glow
// Renders glow/silhouette outlines around visible entities. For each entity in
// the active render list, computes screen-space bounding rect, draws the entity
// in a stencil-like pass, then draws 10 expanding outline passes for the glow.
// ============================================================================
void weather_render_entity_glow(void) {
    u32 x0, y0, x1, y1;
    u32 expand_x0, expand_y0, expand_x1, expand_y1;
    int entity_ptr;
    int iter_offset;
    int prev_viewport[4]; // x, y, w, h stored
    u32 local_104, local_108, local_10c, local_110;
    f32 screen_rect[4]; // x0, y0, x1, y1
    f64 glow_scale;
    f32 expand_factor;
    int pass;

    // Save paired-single registers (compiler artifact)
    FUN_803826b4(); // GX push (large state)

    FUN_8016f0a0(&local_104, &local_108, &local_10c, &local_110); // save viewport
    FUN_8016e8ac(0);
    FUN_8016f00c(0xe);
    FUN_8016efe4();      // flush
    FUN_8016f24c(0xf, *DAT_80611748, DAT_80611748[5],
                 DAT_80611748[1] + 1, DAT_80611748[6] + 1, 0x7f000000);
    FUN_8016ee98();
    FUN_8016efe4();

    iter_offset = 0;
    FUN_803825c0((f64)FLOAT_80614e40); // set glow alpha

    for (int i = 0; i < DAT_80612098; i++) {
        entity_ptr = **(int**)((int)&DAT_80588610 + iter_offset);

        if (entity_ptr != 0 && entity_ptr != *(int*)(DAT_80611d74 + 0x14) &&
            (*(u32*)(entity_ptr + 0x28) & 0x800000) == 0) {

            glow_scale = (f64)entity_compute_screen_rect(); // returns scale

            // Clamp screen rect to viewport bounds
            x0 = (u32)screen_rect[0];
            y0 = (u32)screen_rect[1];
            x1 = (u32)screen_rect[2];
            y1 = (u32)screen_rect[3];

            // Clamp min
            if (screen_rect[0] < (f32)(*DAT_80611748)) x0 = *DAT_80611748;
            if (screen_rect[1] < (f32)(DAT_80611748[5])) y0 = DAT_80611748[5];
            // Clamp max
            if ((f32)(DAT_80611748[1]) < screen_rect[2]) x1 = DAT_80611748[1];
            if ((f32)(DAT_80611748[6]) < screen_rect[3]) y1 = DAT_80611748[6];

            FUN_8016f0e4(x0, y0, x1, y1); // set scissor

            // Draw entity silhouette pass
            collision_set_flags(*(int**)(entity_ptr + 4), 1);
            FUN_8016f00c(1);
            FUN_8016e8ac(0);
            render_instance_draw_multipass(/* ... */ *(u32*)(entity_ptr + 4));
            gx_bind_material_by_index_w(-6);
            FUN_8016ee98();
            FUN_8016efe4();
            FUN_8016f00c(2);

            // 10 expanding outline passes
            f64 glow_rate = (f64)FLOAT_80614e68;
            f64 glow_divisor = (f64)FLOAT_80614e1c;
            pass = 10;

            do {
                expand_factor = (f32)((f64)(f32)(glow_rate *
                    (f64)(f32)((f64)(f32)(pass) / (f32)glow_divisor)) * glow_scale);

                expand_x0 = (u32)((f32)x0 - expand_factor);
                expand_y0 = (u32)((f32)y0 - expand_factor);
                expand_x1 = (u32)((f32)x1 + expand_factor);
                expand_y1 = (u32)((f32)y1 + expand_factor);

                FUN_80170410(expand_x0, expand_y0, expand_x1, expand_y1,
                             x0 << 4, y0 << 4, x1 << 4, y1 << 4);
                pass--;
            } while (pass > 0);

            // Final pass: copy back with blend
            FUN_8016efe4();
            FUN_8016f00c(0xe);
            FUN_8016f24c(8, x0, y0, x1, y1, 0);
        }
        iter_offset += 4;
    }

    // Restore viewport and cleanup
    FUN_8016f0e4(local_104, local_108, local_10c, local_110);
    FUN_8016efe8(0x7f);
    FUN_8016f00c(0x10);
    gx_bind_material_by_index_w(-2);
    FUN_8016efe4();
    FUN_80170410(*DAT_80611748, DAT_80611748[5],
                 DAT_80611748[1] + 1, DAT_80611748[6] + 1,
                 *DAT_80611748 << 4, DAT_80611748[5] << 4,
                 (DAT_80611748[1] + 1) * 0x10, (DAT_80611748[6] + 1) * 0x10);
    FUN_8016ee98();
    FUN_8016f00c(0);
    FUN_8016efe4();
    FUN_8016f4bc(*DAT_80611748, DAT_80611748[5],
                 DAT_80611748[1] + 1, DAT_80611748[6] + 1, 0);
    FUN_8016ee98();
    FUN_8016efe4();

    FUN_80382700(); // GX state pop (large)
}

// ============================================================================
// 0x800d0400 | weather_init_position_tables
// Initializes random position lookup tables for weather particles.
// Called from sprite_grid_init. Fills 5 random normalized values and 30
// screen-space Y positions for rain/snow rendering.
// ============================================================================
void weather_init_position_tables(void) {
    f32 rand_val;
    f32 screen_height_inv;
    f32 one_f;
    u32 i;
    int offset;

    FUN_803826e8(); // GX push

    f64 rand_scale = (f64)FLOAT_80614e6c;
    int count = 0;
    offset = 0;
    f64 rand_base = DOUBLE_80614df0;

    // Fill 5 random normalized values
    do {
        u32 raw_rand = FUN_8013104c(); // random number generator
        count++;
        *(f32*)((int)&DAT_8056dbe0 + offset) =
            (f32)((f64)CONCAT44(0x43300000, raw_rand) - rand_base) / (f32)rand_scale;
        screen_height_inv = FLOAT_80614e70;
        one_f = FLOAT_80614e00;
        offset += 4;
    } while (count < 5);

    // Fill 30 screen-space Y position entries (mirrored into second table)
    u32 y_idx = 0;
    offset = 0;
    int remaining = 0x1e; // 30
    do {
        u32 y_raw = y_idx ^ 0x80000000;
        y_idx++;
        *(f32*)((int)&DAT_8056dbf4 + offset) =
            one_f - (f32)((f64)CONCAT44(0x43300000, y_raw) - DOUBLE_80614df8) / screen_height_inv;
        // Mirror into second table
        *(u32*)((int)&DAT_8056dc6c + offset) = *(u32*)((int)&DAT_8056dbf4 + offset);
        offset += 4;
        remaining--;
    } while (remaining != 0);

    FUN_80382734(); // GX pop
}

// ============================================================================
// 0x800d04f4 | weather_render_rain_strips
// Renders rain/snow as vertical GX strips using paired sin-table lookups for
// horizontal position variation. Draws in 2 passes (left-to-right, right-to-left)
// with direct writes to GX FIFO register (0xCC008000).
// ============================================================================
void weather_render_rain_strips(void) {
    u32 alpha_byte;
    f32 x_pos, y_top, y_bot;
    u32 strip_step;
    u8 prev_cull;
    u32 color_packed;
    int pass;

    FUN_803826d4(); // GX push
    prev_cull = FUN_8016e000(1);  // enable culling
    FUN_8016e77c(-6);              // z depth
    FUN_8016e8ac(0);               // blend off
    FUN_8001d4b4();                // GX invalidate vertex cache
    FUN_8016e84c();                // additional GX state
    FUN_8016f00c(0xe);             // texture mode
    FUN_8016ee98();                // flush
    FUN_8016f0e4(*DAT_80611748, DAT_80611748[5], DAT_80611748[1], DAT_80611748[6]);
    FUN_8016efe4();
    FUN_8016f00c(2);               // line draw mode
    FUN_8016ee98();

    f64 freq = (f64)FLOAT_80614e48;
    f64 x_scale = (f64)FLOAT_80614e74;
    f64 y_scale = (f64)FLOAT_80614e78;
    f64 one_f = (f64)FLOAT_80614e00;
    pass = 0;
    f64 base = DOUBLE_80614df8;

    do {
        strip_step = DAT_80611748[0xc] + 1;
        f64 inv_step = (f64)(f32)(one_f /
            (f32)((f64)CONCAT44(0x43300000, strip_step ^ 0x80000000) - base));

        FUN_80172a4c(2, 0x92, 0, (s16)(2 / (int)strip_step) * 0x1e, -6);

        u32 col = 0;
        while ((int)col < 0x1e) {
            u32 lookup_idx = col;
            if (pass != 0) {
                lookup_idx = 0x1d - col;  // reverse direction for pass 2
            }

            alpha_byte = (u32)(x_scale * (f64)(f32)(&DAT_8056dbf4)[lookup_idx]);
            x_pos = (f32)((f64)(f32)(freq *
                (f64)(f32)((f64)CONCAT44(0x43300000, lookup_idx ^ 0x80000000) - base)) * inv_step);
            y_top = (f32)(y_scale * (f64)((f32)((f64)CONCAT44(0x43300000,
                DAT_80611748[8] ^ 0x80000000) - base) * (f32)(&DAT_8056dc6c)[lookup_idx]));

            f32 x_left = (f32)((f64)CONCAT44(0x43300000, *DAT_80611748 ^ 0x80000000) - base) + x_pos;
            f64 y_top_pos = (f64)((f32)((f64)CONCAT44(0x43300000, DAT_80611748[5] ^ 0x80000000) - base) + y_top);
            f64 y_bot_pos = (f64)((f32)((f64)CONCAT44(0x43300000, DAT_80611748[6] ^ 0x80000000) - base) - y_top);

            if (pass != 0) {
                x_left = (f32)((f64)CONCAT44(0x43300000, DAT_80611748[1] ^ 0x80000000) - base) - x_pos;
            }

            // Screen-space to GX clip coords
            f64 clip_x = FUN_8001ca5c((f64)x_left, -6);
            f64 clip_y_top = FUN_8001cac0(y_top_pos, -6);
            f64 clip_y_bot = FUN_8001cac0(y_bot_pos, -6);

            // Write 2 vertices directly to GX FIFO
            color_packed = alpha_byte << 24 | (alpha_byte & 0xff) << 16 |
                           (alpha_byte & 0xff) << 8 | 0x7f;

            // Vertex 1 (top)
            DAT_cc008000 = (f32)clip_x;      // X
            DAT_cc008000 = (f32)y_top_pos;   // Y (screen)
            DAT_cc008000 = FLOAT_80614e04;   // Z (constant)
            DAT_cc008000 = color_packed;     // color

            DAT_cc008000 = (f32)clip_x;      // tex U
            DAT_cc008000 = (f32)clip_y_top;  // tex V

            // Vertex 2 (bottom)
            DAT_cc008000 = (f32)clip_x;
            DAT_cc008000 = (f32)y_bot_pos;
            DAT_cc008000 = FLOAT_80614e04;
            DAT_cc008000 = color_packed;

            col += strip_step;
            DAT_cc008000 = (f32)clip_x;
            DAT_cc008000 = (f32)clip_y_bot;
        }

        FUN_80172c54(); // end draw
        pass++;
    } while (pass < 2);

    FUN_8001d4d4();         // GX restore vertex cache
    FUN_8016e000(prev_cull); // restore cull mode
    FUN_80382720();         // GX pop
}

// ============================================================================
// 0x800d0870 | weather_render_glow_blur_pass
// Renders multi-iteration glow expansion. Each iteration draws at half-alpha
// with increasing bounds offset. Used for bloom/glow on weather effects or
// screen flash. param_11 controls blend mode (0=additive, else=alpha).
// ============================================================================
void weather_render_glow_blur_pass(
    f64 param_1, f64 x_min, f64 y_min, f64 x_max,
    f64 y_max, f64 x_offset, f64 y_offset, f64 unused,
    u32 param_9, u32 param_10, int blend_mode
) {
    int iterations;
    u8 prev_cull;
    f64 alpha;
    f64 x0, y0, x1, y1;

    iterations = FUN_803826d8(); // returns iteration count + alpha in f1
    alpha = (f64)FLOAT_80614e7c;

    // Compute expanded bounds
    x1 = (f64)(f32)(x_max + unused);
    y1 = (f64)(f32)(y_min + y_offset);
    x0 = (f64)(f32)(x_min + x_offset);
    y0 = (f64)(f32)(param_1 + y_max); // extraout_f1 + param

    for (int i = 0; i < iterations; i++) {
        FUN_8016f00c(0xe);
        prev_cull = FUN_8016e000(1);
        FUN_8016e010((f64)FLOAT_80614e20); // half alpha
        gx_bind_material_fallback();                     // set up texture state
        FUN_8016e010((f64)FLOAT_80614e04); // restore alpha
        FUN_8016e000(prev_cull);

        if (blend_mode == 0) {
            FUN_8016f00c(6);  // additive blend
        } else {
            FUN_8016f00c(8);  // alpha blend
        }

        FUN_8016e8ac(0);
        FUN_80171cbc(y0, x0, y1, x1, (f64)FLOAT_80614e00,
                     (int)(alpha * param_1),
                     (int)(alpha * x_min),
                     (int)(alpha * y_min),
                     (int)(alpha * x_max));
    }

    FUN_80382724(); // GX pop
}

// ============================================================================
// 0x800d0a08 | weather_set_layer_param
// Sets a single parameter value on a weather layer context.
// Layer index selects which of the 6 contexts (each 0x1E8 bytes).
// Offset 0x1BC within the context structure.
// ============================================================================
void weather_set_layer_param(int layer_index, u32 value) {
    *(u32*)(&DAT_80569bcc + layer_index * 0x1e8) = value;
}

// ============================================================================
// 0x800d0a20 | weather_set_intensity
// Sets the weather intensity for a given layer and flags the system for
// recalculation on next frame (DAT_80611ddc = 1). Intensity is scaled by
// FLOAT_80614e40 before storing into the context.
// ============================================================================
void weather_set_intensity(f64 intensity, int layer_index) {
    DAT_80611ddc = 1;
    (&DAT_80569a24)[layer_index * 0x7a] = (int)((f64)FLOAT_80614e40 * intensity);
}
