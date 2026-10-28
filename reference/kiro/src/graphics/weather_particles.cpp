// Address: 0x800d1794 - 0x800d24a0 | Weather particle sub-layers | Date: 2026-08-25
// Batch 284: Weather particle slot management, type dispatch, billboard and streak
// particle rendering via GX FIFO, precipitation fade, and flash trigger.
//
// Each weather context has 5 sub-particle slots (0x34 bytes each) at offset 0xB0.
// Slot layout:
//   [0] type (0=empty, 1=billboard, 3=streak, 4=type4, 5=type5)
//   [1] current_time
//   [2] max_lifetime
//   [3-5] position (x, y, z)
//   [6-8] direction/velocity (x, y, z)
//   [9] scale/size

#include "common.h"
#include "graphics.h"
#include "weather.h"
#include "particle.h"
#include "sprite.h"

// ============================================================================
// 0x800d1794 | weather_particle_find_free_slot
// Searches the 5 sub-particle slots in a weather context for an empty one
// (type == 0). Returns pointer to the slot or NULL if all are occupied.
// ============================================================================
int* weather_particle_find_free_slot(int player_idx) {
    u32* ctx;
    int offset;
    int* slot;
    int remaining;

    ctx = particle_get_emitter_def(player_idx);
    offset = 0;
    remaining = 5;

    do {
        slot = (int*)((int)ctx + offset + 0xb0);
        if (*slot == 0) {
            return slot;
        }
        offset += 0x34;
        remaining--;
    } while (remaining != 0);

    return (int*)0x0;
}

// ============================================================================
// 0x800d17e8 | weather_particle_lifetime_tick
// Advances a particle's elapsed time by the frame delta. If elapsed time
// exceeds the max lifetime, deactivates the particle (sets type to 0).
// Frame delta: FLOAT_806108ec * (1.0 / framerate)
// ============================================================================
void weather_particle_lifetime_tick(u32* particle) {
    // particle[1] = current_time, particle[2] = max_lifetime
    *(f32*)&particle[1] = *(f32*)&particle[1] +
        FLOAT_806108ec * (FLOAT_80614e00 /
        (f32)((f64)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80614df8));

    if (*(f32*)&particle[2] < *(f32*)&particle[1]) {
        *particle = 0; // deactivate
    }
}

// ============================================================================
// 0x800d1848 | weather_particle_render_dispatch
// Type-based dispatch for sub-particle rendering:
//   type 1 -> weather_particle_render_billboard (billboard quad)
//   type 3 -> weather_particle_render_streak (20x20 warped grid)
//   type 4 -> FUN_800d2afc (not yet decompiled)
//   type 5 -> FUN_800d2fa0 (not yet decompiled, deferred to post-pass)
// ============================================================================
void weather_particle_render_dispatch(int* particle) {
    int type = *particle;

    if (type == 1) {
        weather_particle_render_billboard();
    } else if (type == 3) {
        weather_particle_render_streak();
    } else if (type == 4) {
        FUN_800d2afc();
    } else if (type == 5) {
        FUN_800d2fa0();
    }
}

// ============================================================================
// 0x800d18a4 | weather_particle_spawn_billboard
// Spawns a type-1 billboard particle in the first available slot.
// Position = param_3 + FLOAT_80614e28 * param_4 (velocity offset).
// Direction = camera forward * FLOAT_80614e8c.
// Lifetime = FLOAT_80614e80. Scale = param_1.
// ============================================================================
void weather_particle_spawn_billboard(f64 scale, int player_idx, int* position, f32* velocity) {
    f32 vel_x, vel_y, vel_z;
    f32 offset_scale;
    f32 cam_scale;
    int* slot;

    slot = weather_particle_find_free_slot(player_idx);
    offset_scale = FLOAT_80614e28;

    if (slot != (int*)0x0) {
        *slot = 1; // type = billboard
        cam_scale = FLOAT_80614e8c;

        // Copy position with velocity offset
        slot[3] = *position;
        slot[4] = position[1];
        slot[5] = position[2];

        vel_x = velocity[0];
        vel_y = velocity[1];
        vel_z = velocity[2];

        slot[3] = (int)((f32)slot[3] + offset_scale * vel_x);
        slot[4] = (int)((f32)slot[4] + offset_scale * vel_y);
        slot[5] = (int)((f32)slot[5] + offset_scale * vel_z);

        // Direction from camera forward vector
        slot[6] = (int)(cam_scale * *(f32*)(DAT_80611d54 + 0x334));
        slot[7] = (int)(cam_scale * *(f32*)(DAT_80611d54 + 0x338));
        slot[8] = (int)(cam_scale * *(f32*)(DAT_80611d54 + 0x33c));

        // Lifetime and scale
        slot[1] = (int)FLOAT_80614e04; // current_time = 0.0
        slot[2] = (int)FLOAT_80614e80; // max_lifetime
        slot[9] = (int)(f32)scale;
    }
}

// ============================================================================
// 0x800d19a0 | weather_particle_render_billboard
// MASSIVE billboard particle renderer. Projects the particle's 3D position
// to screen space, computes camera-facing orientation via cross product with
// up/right vectors, then draws a textured quad directly to GX FIFO (0xCC008000).
// Also draws a secondary glow/halo quad using additive blend.
//
// Algorithm:
// 1. Get current GX state, save texture/blend mode
// 2. Project particle position to screen (sprite_measure_text_region_int)
// 3. Compute time ratio -> alpha (quadratic falloff)
// 4. Load texture 0x363 (lens flare/glow)
// 5. Compute screen-space aspect ratio correction
// 6. Build billboard orientation from camera vectors via cross product
// 7. Offset position along camera forward for depth
// 8. Write 4 vertices to GX FIFO with UV coords
// 9. Draw secondary glow halo quad with additive blend
// 10. Restore GX state
// ============================================================================
void weather_particle_render_billboard(void) {
    int iVar6, iVar7, iVar8, iVar9;
    int screen_x, screen_y;
    u32 screen_depth;
    f32 time_ratio, alpha_scale;
    f32 size_world, size_screen;
    f32 right_x, right_y, right_z;
    f32 up_x, up_y, up_z;
    f32 pos_x, pos_y, pos_z;
    f32 corner_positions[12]; // 4 corners * 3 components
    u32 color_packed;

    iVar6 = FUN_803826e8(); // GX push, returns particle data pointer
    iVar7 = FUN_8016f004(); // save texture mode
    iVar8 = FUN_8016e8a4(); // save blend mode

    // Project 3D position to screen
    iVar9 = sprite_measure_text_region_int(
        iVar6 + 0xc, &screen_y, (int*)&screen_depth, NULL, 1);

    if (iVar9 != 2) { // not behind camera
        // Compute time ratio for alpha
        time_ratio = *(f32*)(iVar6 + 4) / *(f32*)(iVar6 + 8);
        f64 alpha_raw = FUN_80131538((f64)time_ratio);
        f64 size = (f64)(*(f32*)(iVar6 + 0x24) * (f32)alpha_raw + FLOAT_80614e38);

        // Get glow texture
        int tex_handle = sprite_get_texture_id(0x363);

        // Compute aspect ratio correction for screen projection
        f32 aspect = /* screen height / texture height ratio */;
        size_screen = (f32)((f64)FLOAT_80614e90 * size) * aspect;

        // Alpha from time ratio (quadratic falloff: 1 - t^2)
        color_packed = FUN_803825c0(
            (f64)(FLOAT_80614e74 * -(time_ratio * time_ratio - FLOAT_80614e00)));

        FUN_8016ee98();
        FUN_8016e77c(tex_handle);
        FUN_8016f00c(0xe);

        // Compute offset position along camera forward
        f64 forward_scale = (f64)FLOAT_80614e94 * size;
        f32 offset_pos[3];
        offset_pos[0] = *(f32*)(iVar6 + 0xc) +
            (f32)((f64)*(f32*)(DAT_80611d54 + 0x370) * forward_scale);
        offset_pos[1] = *(f32*)(iVar6 + 0x10) +
            (f32)((f64)*(f32*)(DAT_80611d54 + 0x374) * forward_scale);
        offset_pos[2] = *(f32*)(iVar6 + 0x14) +
            (f32)((f64)*(f32*)(DAT_80611d54 + 0x378) * forward_scale);

        // Project offset position for screen-space bounds
        sprite_measure_text_region_int(offset_pos, &screen_x, NULL, NULL, 1);

        // Build camera-facing billboard orientation (cross product)
        f32 cam_fwd[3] = { DAT_803cc778, DAT_803cc77c, DAT_803cc780 };
        f32 cam_up[3]  = { DAT_803cc784, DAT_803cc788, DAT_803cc78c };
        f32 view_dir[3];
        view_dir[0] = *(f32*)(iVar6 + 0x18);
        view_dir[1] = *(f32*)(iVar6 + 0x1c);
        view_dir[2] = *(f32*)(iVar6 + 0x20);

        // Cross product for billboard right vector
        if (ABS(view_dir[1]) <= FLOAT_80614e98) {
            // Near-horizontal: use cam_fwd cross
            right_x = view_dir[0] * cam_fwd[1] - view_dir[1] * cam_fwd[0];
            right_z = view_dir[2] * cam_fwd[0] - view_dir[0] * cam_fwd[2];
            right_y = view_dir[1] * cam_fwd[2] - view_dir[2] * cam_fwd[1];
        } else {
            // Use cam_up cross
            right_x = view_dir[0] * cam_up[1] - view_dir[1] * cam_up[0];
            right_z = view_dir[2] * cam_up[0] - view_dir[0] * cam_up[2];
            right_y = view_dir[1] * cam_up[2] - view_dir[2] * cam_up[1];
        }

        // Compute 4 billboard corners from right + up vectors scaled by size
        pos_x = *(f32*)(iVar6 + 0xc);
        pos_y = *(f32*)(iVar6 + 0x10);
        pos_z = *(f32*)(iVar6 + 0x14);

        // Corner computation (size * right +/- size * cross(right, view))
        // ... (complex paired-single math for 4 corner positions)

        // Setup GX draw state
        FUN_8016e84c();
        FUN_80130cd8(*(f32**)(DAT_80611d54 + 0x208), (f32*)&DAT_8058e3cc);
        DAT_80612250 = 1;
        DAT_80612258 = 0;
        FUN_8016e8ac(0);
        FUN_80172d88(0x91, 0, 4, tex_handle);

        // Write 4 vertices to GX FIFO
        color_packed = (color_packed & 0xff) | 0x80808000;

        // Vertex 0 (bottom-left)
        DAT_cc008000 = corner_positions[0]; // X
        DAT_cc008000 = corner_positions[1]; // Y
        DAT_cc008000 = corner_positions[2]; // Z
        DAT_cc008000 = color_packed;
        DAT_cc008000 = FLOAT_80614e04; // U = 0.0
        DAT_cc008000 = FLOAT_80614e04; // V = 0.0

        // Vertex 1 (bottom-right)
        DAT_cc008000 = corner_positions[3];
        DAT_cc008000 = corner_positions[4];
        DAT_cc008000 = corner_positions[5];
        DAT_cc008000 = color_packed;
        DAT_cc008000 = FLOAT_80614e00; // U = 1.0
        DAT_cc008000 = FLOAT_80614e04; // V = 0.0

        // Vertex 2 (top-right)
        DAT_cc008000 = corner_positions[6];
        DAT_cc008000 = corner_positions[7];
        DAT_cc008000 = corner_positions[8];
        DAT_cc008000 = color_packed;
        DAT_cc008000 = FLOAT_80614e00; // U = 1.0
        DAT_cc008000 = FLOAT_80614e00; // V = 1.0

        // Vertex 3 (top-left)
        DAT_cc008000 = corner_positions[9];
        DAT_cc008000 = corner_positions[10];
        DAT_cc008000 = corner_positions[11];
        DAT_cc008000 = color_packed;
        DAT_cc008000 = FLOAT_80614e04; // U = 0.0
        DAT_cc008000 = FLOAT_80614e00; // V = 1.0

        FUN_80172c54(); // end primitive
        FUN_8016ee98(); // flush

        // Draw secondary glow/halo quad (additive blend, screen-space)
        FUN_8016e77c(-6);
        FUN_8016f00c(6); // additive blend

        // Clamp screen bounds to viewport
        // ... (viewport clamping logic)

        // Compute texel coordinates
        // ... (FLOAT_80614e7c scaling)

        FUN_801708b4(/* screen quad coords + texel coords */);
        FUN_8016ee98();
    }

    FUN_8016f00c(iVar7); // restore texture
    FUN_8016e8ac(iVar8); // restore blend
    FUN_80382734();       // GX pop
}

// ============================================================================
// 0x800d2204 | weather_particle_trigger_flash
// Triggers a screen flash on a weather context. Sets flash active flag
// (offset 0x6D*4 = 0x1B4) and resets the flash timer (offset 0x6E*4 = 0x1B8)
// to 0.0. Only triggers if not already active or timer exceeds threshold.
// ============================================================================
void weather_particle_trigger_flash(int player_idx) {
    u32* ctx;

    ctx = particle_get_emitter_def(player_idx);

    // Trigger if not active, or if timer exceeded half-alpha threshold
    if (ctx[0x6d] == 0 || FLOAT_80614e20 < (f32)ctx[0x6e]) {
        ctx[0x6e] = (u32)FLOAT_80614e04; // reset timer to 0.0
        ctx[0x6d] = 1;                    // activate flash
    }
}

// ============================================================================
// 0x800d2250 | weather_precipitation_timer_tick
// Advances the precipitation effect timer (offset 0x1B8 in weather context)
// by the frame delta. When timer reaches FLOAT_80614e80, deactivates the
// precipitation effect (offset 0x1B4 = 0).
// ============================================================================
void weather_precipitation_timer_tick(int weather_ctx) {
    f32 max_time = FLOAT_80614e80;

    *(f32*)(weather_ctx + 0x1b8) +=
        FLOAT_806108ec * (FLOAT_80614e00 /
        (f32)((f64)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80614df8));

    if (max_time <= *(f32*)(weather_ctx + 0x1b8)) {
        *(u32*)(weather_ctx + 0x1b4) = 0; // deactivate
    }
}

// ============================================================================
// 0x800d22b0 | weather_precipitation_render_fade
// Renders a fading precipitation overlay. If timer < threshold (FLOAT_80614e78),
// draws a viewport blend pass with alpha proportional to remaining time.
// Always draws a fullscreen quad afterwards (for the rain/snow texture).
// ============================================================================
void weather_precipitation_render_fade(int weather_ctx) {
    if (*(f32*)(weather_ctx + 0x1b8) < FLOAT_80614e78) {
        // Compute fade alpha: full at t=0, fading to 0 at threshold
        FUN_803825c0((f64)(FLOAT_80614e40 *
            (FLOAT_80614e00 - *(f32*)(weather_ctx + 0x1b8) / FLOAT_80614e78)));
        sprite_draw_viewport_blend_a();
    }

    sprite_draw_fullscreen_quad(
        *DAT_80611748, DAT_80611748[5],
        DAT_80611748[1] + 1, DAT_80611748[6] + 1);
}

// ============================================================================
// 0x800d2364 | weather_particle_spawn_streak
// Spawns a type-3 streak particle. Limited to max 2 active type-3 particles
// per context. Position = source + offset*velocity. Direction stored directly
// from velocity parameter. Lifetime = FLOAT_80614e3c * param_2.
// ============================================================================
void weather_particle_spawn_streak(
    u64 param_1, f64 lifetime_scale, u32 param_3, u32 param_4, f32* velocity
) {
    int player_idx;
    int* source_pos;
    u32* ctx;
    int* slot;
    int type3_count;
    int i, offset;
    f32 vel_x, vel_y, vel_z;
    f32 offset_scale;

    // Extract from packed params
    player_idx = (int)(param_1 >> 32);
    source_pos = (int*)(u32)param_1;

    FUN_803826ec(); // GX push

    ctx = particle_get_emitter_def(player_idx);
    slot = weather_particle_find_free_slot(player_idx);

    // Count existing type-3 particles
    type3_count = 0;
    offset = 0;
    i = 5;
    do {
        if (*(int*)((int)ctx + offset + 0xb0) == 3) {
            type3_count++;
        }
        offset += 0x34;
        i--;
    } while (i != 0);

    // Only spawn if slot available and fewer than 2 type-3 active
    if (slot != (int*)0x0 && type3_count < 2) {
        offset_scale = FLOAT_80614e28;

        *slot = 3; // type = streak

        // Copy position with velocity offset
        slot[3] = *source_pos;
        slot[4] = source_pos[1];
        slot[5] = source_pos[2];

        vel_x = velocity[0];
        vel_y = velocity[1];
        vel_z = velocity[2];

        slot[3] = (int)((f32)slot[3] + offset_scale * vel_x);
        slot[4] = (int)((f32)slot[4] + offset_scale * vel_y);
        slot[5] = (int)((f32)slot[5] + offset_scale * vel_z);

        // Store velocity as direction
        slot[6] = (int)vel_x;
        slot[7] = (int)vel_y;
        slot[8] = (int)vel_z;

        // Lifetime and timer
        slot[1] = (int)FLOAT_80614e04;                    // current_time = 0.0
        slot[2] = (int)(f32)(FLOAT_80614e3c * (f32)lifetime_scale); // max_lifetime
        slot[9] = (int)(f32)/* extraout_f1 */;            // scale from return
    }

    FUN_80382738(); // GX pop
}

// ============================================================================
// 0x800d24a0 | weather_particle_render_streak
// MASSIVE streak particle renderer using a 20x20 vertex grid.
// Projects the particle 3D position to screen, then builds a warped grid where
// each vertex is displaced by sin-wave oscillation proportional to distance
// from center. Draws as GX triangle strips with per-vertex color (alpha from
// distance falloff). Uses world_to_screen_project for 3D->2D conversion.
//
// Algorithm:
// 1. Project particle position; bail if behind camera or out of depth range
// 2. Compute screen-space size from time ratio + aspect correction
// 3. Clamp size to FLOAT_80614e34 maximum
// 4. Build 20x20 grid: each vertex offset by sin(distance) * falloff
// 5. Convert grid to screen coords via FUN_8001ca5c/FUN_8001cac0
// 6. Draw 19 triangle strips of 21 vertices each via GX FIFO
// ============================================================================
void weather_particle_render_streak(void) {
    int iVar9, iVar10, iVar11;
    f32 screen_x, screen_y, screen_depth;
    f32 grid[20 * 20 * 4]; // x, y, clip_x, clip_y per vertex
    u32 color_packed;
    f64 size, aspect_size;

    iVar9 = FUN_803826cc(); // GX push, returns particle data
    iVar10 = FUN_8016e8a4(); // save blend

    // Project 3D position to screen
    iVar11 = world_to_screen_project(iVar9 + 0xc, &screen_x, 1);

    // Only render if visible and within depth range (0.0 < depth < 1.0)
    if (iVar11 != 2 && FLOAT_80614e04 < screen_depth && screen_depth < FLOAT_80614e00) {

        // Compute size from time ratio with linear growth
        f64 time_factor = (f64)(FLOAT_80614ea4 *
            (*(f32*)(iVar9 + 4) / *(f32*)(iVar9 + 8)) + FLOAT_80614ea0);
        f32 world_size = *(f32*)(iVar9 + 0x24) *
            (f32)((f64)FLOAT_80614e90 * time_factor) * screen_depth;

        // Clamp to max
        if (FLOAT_80614e34 < world_size) {
            world_size = FLOAT_80614e34;
        }

        // Aspect ratio correction for screen projection
        f32 aspect_correction = /* viewport step * world_size */;
        f32 screen_size = aspect_correction / /* aspect ratio */;

        // Alpha from depth (closer = more opaque)
        color_packed = FUN_803825c0(
            (f64)(FLOAT_80614e40 * (FLOAT_80614e00 - screen_depth)));

        // Build 20x20 vertex grid with sin-wave displacement
        f64 wave_freq = (f64)FLOAT_80614e88;
        f64 max_dist = (f64)FLOAT_80614e34 * time_factor;
        int row = 0;
        int grid_offset = 0;

        do {
            u32 col = 0;
            // Compute row-specific sin values for wave animation
            f64 row_t = (f64)(f32)((f32)row / (f64)FLOAT_80614e90);
            u32 sin_idx_a = (u32)(FLOAT_80614e0c * (f32)(wave_freq * row_t) + FLOAT_80614de8);
            u32 sin_idx_b = (u32)(FLOAT_80614e0c * (f32)(wave_freq * row_t + FLOAT_80614e04) +
                                  FLOAT_80614de8);

            do {
                int cell = col + grid_offset;
                f64 col_t = (f64)(f32)((f32)col / (f64)FLOAT_80614e90);

                // Distance-based falloff
                f64 dist = (f64)(f32)(max_dist - col_t);
                f32 falloff = FLOAT_80614e04; // default 0.0
                if (FLOAT_80614e04 <= (f32)dist && (f32)dist <= FLOAT_80614e00) {
                    f32 d = FLOAT_80614e00 - (f32)dist;
                    // Sin-based displacement scaled by falloff^2
                    u32 sin_idx = (u32)(FLOAT_80614e0c * FLOAT_80614e34 *
                        (f32)((f64)FLOAT_80614e88 * dist) + FLOAT_80614de8);
                    falloff = d * d * (&DAT_80583228)[sin_idx & 0x7ff];
                }

                // Compute world-space vertex positions with wave displacement
                f32 sin_a = (&DAT_80583228)[sin_idx_a & 0x7ff];
                f32 cos_a = (&DAT_80583228)[(sin_idx_a + 0x200) & 0x7ff]; // cos = sin+512
                f32 sin_b = (&DAT_80583228)[sin_idx_b & 0x7ff];

                grid[cell * 4 + 0] = screen_x +
                    (f32)(col_t * (f64)(aspect_correction * cos_a));
                grid[cell * 4 + 1] = screen_y +
                    (f32)(col_t * (f64)(screen_size * sin_a));

                // With falloff-based additional displacement
                f32 disp_x = cos_a * (FLOAT_80614e00 + falloff) * (f32)(col_t * aspect_correction);
                f32 disp_y = sin_b * (FLOAT_80614e00 + falloff) * (f32)(col_t * screen_size);

                // Project to clip space
                f64 clip_x = FUN_8001ca5c((f64)(disp_x + screen_x), -6);
                f64 clip_y = FUN_8001cac0((f64)(disp_y + screen_y), -6);

                grid[cell * 4 + 2] = (f32)clip_x;
                grid[cell * 4 + 3] = (f32)clip_y;

                col++;
            } while ((int)col < 0x14); // 20 columns

            row++;
            grid_offset += 0x14; // 20 per row
        } while (row < 0x14); // 20 rows

        // Setup GX draw state
        FUN_8016e8ac(0);
        FUN_8016f00c(0xe);
        FUN_8016ee98();
        FUN_8016f00c(1);
        FUN_8001d4b4(); // invalidate vertex cache
        gx_bind_material_fallback(); // setup texture state

        // Draw 19 triangle strips (adjacent row pairs)
        int strip_row = 0;
        do {
            FUN_80172a4c(2, 0x92, 0, 0x2a, -6); // begin strip (42 vertices)

            int vert = 0;
            int remaining = 0x15; // 21 vertices per strip
            do {
                int col_idx = (vert % 0x14) * 0x14; // wrap column
                int v0 = strip_row + col_idx;
                int v1 = strip_row + 1 + col_idx;

                // Vertex from current row
                DAT_cc008000 = grid[v0 * 4 + 0]; // X
                DAT_cc008000 = grid[v0 * 4 + 1]; // Y
                DAT_cc008000 = FLOAT_80614e04;    // Z = 0.0
                DAT_cc008000 = (color_packed & 0xff) | 0x7f7f7f00; // color
                DAT_cc008000 = grid[v0 * 4 + 2]; // tex U
                DAT_cc008000 = grid[v0 * 4 + 3]; // tex V

                // Vertex from next row
                DAT_cc008000 = grid[v1 * 4 + 0];
                DAT_cc008000 = grid[v1 * 4 + 1];
                DAT_cc008000 = FLOAT_80614e04;
                DAT_cc008000 = (color_packed & 0xff) | 0x7f7f7f00;
                DAT_cc008000 = grid[v1 * 4 + 2];
                DAT_cc008000 = grid[v1 * 4 + 3];

                vert++;
                remaining--;
            } while (remaining != 0);

            FUN_80172c54(); // end strip
            strip_row++;
        } while (strip_row < 0x13); // 19 strips

        FUN_8001d4d4(); // restore vertex cache
        FUN_8016f00c(0xe);
        FUN_8016ee98();
    }

    FUN_8016e8ac(iVar10); // restore blend
    FUN_80382718();        // GX pop
}

// ============================================================================
// 0x800d2a00 | weather_particle_spawn_type4
// Spawns a type-4 particle (screen-space explosion/flash). Projects the
// spawn position (with velocity offset * FLOAT_80614ea8) to screen coordinates,
// then stores screen X/Y/depth in slots [10-12]. Only spawns if the projected
// position is visible (not behind camera, depth in 0..1 range).
// ============================================================================
void weather_particle_spawn_type4(f64 scale, int player_idx, f32* position, f32* velocity) {
    int* slot;
    int proj_result;
    f32 projected_pos[3]; // screen x, y, depth
    int screen_coords[3];

    // Offset position by velocity
    f32 spawn_pos[3];
    spawn_pos[0] = position[0] + FLOAT_80614ea8 * velocity[0];
    spawn_pos[1] = position[1] + FLOAT_80614ea8 * velocity[1];
    spawn_pos[2] = position[2] + FLOAT_80614ea8 * velocity[2];

    proj_result = world_to_screen_project(spawn_pos, screen_coords, 1);

    if (proj_result != 2 && FLOAT_80614e04 < projected_pos[2] &&
        projected_pos[2] < FLOAT_80614e00) {
        slot = weather_particle_find_free_slot(player_idx);
        if (slot != (int*)0x0) {
            *slot = 4;  // type = 4 (screen flash)
            slot[10] = screen_coords[0]; // screen X
            slot[11] = screen_coords[1]; // screen Y
            slot[12] = screen_coords[2]; // screen depth
            slot[1] = (int)FLOAT_80614e04;  // current_time = 0.0
            slot[2] = (int)(f32)(FLOAT_80614ea8 * (f32)scale + FLOAT_80614de8); // lifetime
            slot[9] = (int)(f32)scale;
        }
    }
}

// ============================================================================
// 0x800d2afc | weather_particle_render_type4
// Renders type-4 particles as 2-pass expanding textured quads with glow halo.
// Pass 1: full alpha quad, Pass 2: reduced alpha (FLOAT_80614e24) quad.
// Uses texture 0x3B9. Size expands with cubic time falloff.
// Each pass: draw 4 textured sub-quads + additive glow halo.
// ============================================================================
void weather_particle_render_type4(void) {
    int particle_data;
    int tex_handle, tex_info;
    u32 tex_w, tex_h;
    f64 time_ratio, alpha_curve;
    f32 size;
    f64 half_w, half_h;
    f64 center_x, center_y;
    int pass;

    particle_data = FUN_803826d0(); // GX push, returns particle ptr

    pass = 0;
    do {
        // Compute time ratio and cubic alpha curve
        time_ratio = (f64)(*(f32*)(particle_data + 4) / *(f32*)(particle_data + 8));

        f32 scale_factor = (pass == 0) ? FLOAT_80614e00 : FLOAT_80614e24;
        alpha_curve = FUN_80131538(time_ratio);

        // Size from alpha curve * scale * random factor
        size = scale_factor * (f32)(FLOAT_80614e20 * (f32)alpha_curve + FLOAT_80614e84) *
               (FLOAT_80614de8 * *(f32*)(particle_data + 0x24) + FLOAT_80614de8);

        // Compute screen extent
        half_w = (f64)(size * (f32)((f64)CONCAT44(0x43300000, DAT_80611748[3] ^ 0x80000000) -
                 DOUBLE_80614df8));
        half_h = (f64)(size * (f32)((f64)CONCAT44(0x43300000, DAT_80611748[8] ^ 0x80000000) -
                 DOUBLE_80614df8));

        // Alpha from cubic falloff: -(t * t^2 - 1)
        FUN_803825c0((f64)(FLOAT_80614e40 *
            -(f32)(time_ratio * (f32)(time_ratio * time_ratio) - (f64)FLOAT_80614e00)));

        center_x = (f64)*(f32*)(particle_data + 0x28);
        center_y = (f64)*(f32*)(particle_data + 0x2c);

        f64 x0 = (f64)(f32)(center_x - half_w);
        f64 y0 = (f64)(f32)(center_y - half_h);
        f64 x1 = (f64)(f32)(center_x + half_w);
        f64 y1 = (f64)(f32)(center_y + half_h);

        // Load texture 0x3B9 (spark/flash texture)
        tex_handle = sprite_get_texture_id(0x3b9);
        tex_info = *DAT_80610a20 + tex_handle * 0x58;
        tex_w = (u32)*(u16*)(tex_info + 0x4e) << 4;
        tex_h = (u32)*(u16*)(tex_info + 0x52) << 4;

        FUN_8016ee98();
        FUN_8016e77c(tex_handle);
        FUN_8016f00c(2);

        // Draw 4 sub-quads (mirrored UV) centered on particle position
        FUN_80170410((int)x0, (int)y0, (u32)center_x, (u32)center_y, tex_w, 0, 0, tex_h);
        FUN_80170410((u32)center_x, (int)y0, (u32)x1, (u32)center_y, 0, 0, tex_w, tex_h);
        FUN_80170410((int)x0, (u32)center_y, (u32)center_x, (u32)y1, tex_w, tex_h, 0, 0);
        FUN_80170410((u32)center_x, (u32)center_y, (u32)x1, (u32)y1, 0, tex_h, tex_w, 0);

        FUN_8016ee98();
        FUN_8016ee98();

        // Draw glow halo (additive blend, clamped to viewport)
        FUN_8016e77c(-6);
        FUN_8016f00c(6);

        // Clamp to viewport bounds
        if (x0 < (f64)*DAT_80611748) x0 = (f64)*DAT_80611748;
        if (x1 > (f64)(DAT_80611748[1] - 1)) x1 = (f64)(DAT_80611748[1] - 1);
        if (y0 < (f64)DAT_80611748[5]) y0 = (f64)DAT_80611748[5];
        if (y1 > (f64)(DAT_80611748[6] - 1)) y1 = (f64)(DAT_80611748[6] - 1);

        FUN_801708b4(x0, y0, x1, y1,
                     (int)(FLOAT_80614e7c * (f32)(x0 + (f64)FLOAT_80614e90)),
                     (int)(FLOAT_80614e7c * (f32)(y0 + (f64)FLOAT_80614e90)),
                     (int)(FLOAT_80614e7c * (f32)(x1 - (f64)FLOAT_80614e90)),
                     (int)(FLOAT_80614e7c * (f32)(y1 - (f64)FLOAT_80614e90)));
        FUN_8016ee98();

        pass++;
    } while (pass < 2);

    FUN_8038271c(); // GX pop
}

// ============================================================================
// 0x800d2fa0 | weather_particle_render_type5
// Renders type-5 particles as a 10x10 warped vertex grid with cos-based
// displacement and random per-frame vertex color. Projects particle position
// to screen, computes vertex positions with time-based shrinkage and wave
// displacement. Draws 9 triangle strips of 11 vertices each via GX FIFO.
// Uses random color per strip row via FUN_80132348.
// ============================================================================
void weather_particle_render_type5(void) {
    int particle_data;
    int proj_result;
    f32 screen_x, screen_y, screen_depth;
    f32 grid[10 * 10 * 4]; // x, y, clip_x, clip_y per vertex
    f64 time_factor, size_factor;

    particle_data = FUN_803826dc(); // GX push, returns particle ptr

    proj_result = world_to_screen_project(particle_data + 0xc, &screen_x, 1);

    // Compute viewport-normalized center
    screen_x = (f32)((f64)CONCAT44(0x43300000, *(u32*)(DAT_80611748 + 8) ^ 0x80000000) -
               DOUBLE_80614df8);
    screen_y = (f32)((f64)CONCAT44(0x43300000, *(u32*)(DAT_80611748 + 0x1c) ^ 0x80000000) -
               DOUBLE_80614df8);

    if (proj_result != 2 && FLOAT_80614e04 < screen_depth && screen_depth < FLOAT_80614e00) {
        // Time-based shrinkage: starts at 1.0, shrinks as t increases
        time_factor = (f64)(FLOAT_80614de8 * (FLOAT_80614e00 -
            (*(f32*)(particle_data + 4) / *(f32*)(particle_data + 8))));
        f64 size_x = (f64)(FLOAT_80614eac *
            (f32)((f64)CONCAT44(0x43300000, *(u32*)(DAT_80611748 + 0xc) ^ 0x80000000) -
            DOUBLE_80614df8));
        f64 size_y = (f64)(FLOAT_80614eac *
            (f32)((f64)CONCAT44(0x43300000, *(u32*)(DAT_80611748 + 0x20) ^ 0x80000000) -
            DOUBLE_80614df8));

        // Build 10x10 grid with wave displacement
        int row = 0;
        int grid_offset = 0;
        do {
            u32 col = 0;
            f64 row_wave = (f64)(FLOAT_80614e88 *
                (f32)((f32)row / FLOAT_80614e1c)); // row normalized to wave freq
            u32 sin_idx = (u32)(FLOAT_80614e0c * (f32)row_wave + FLOAT_80614de8);

            do {
                int cell = col + grid_offset;
                f64 col_t = (f64)(f32)((f32)col / FLOAT_80614eb0);

                // Wave displacement based on distance from time-factor
                f64 dist = (f64)(f32)(time_factor - col_t);
                f32 displacement = FLOAT_80614e04;
                if (FLOAT_80614e04 <= (f32)dist && (f32)dist <= FLOAT_80614e00) {
                    f32 d = FLOAT_80614e00 - (f32)dist;
                    u32 wave_idx = (u32)(FLOAT_80614e0c * FLOAT_80614e34 *
                        (FLOAT_80614e88 * (f32)dist) + FLOAT_80614de8);
                    displacement = d * d * (&DAT_80583228)[wave_idx & 0x7ff];
                }

                f32 sin_val = (&DAT_80583228)[sin_idx & 0x7ff];
                f32 cos_val = *(f32*)((int)&DAT_80583228 + ((sin_idx + 0x200) * 4 & 0x1ffc));

                // Vertex position with displacement
                grid[cell * 4 + 0] = cos_val * (f32)(FLOAT_80614e00 + displacement) *
                    (f32)(size_x * col_t) + screen_x;
                grid[cell * 4 + 1] = sin_val * (f32)(size_y * col_t) + screen_y;

                // Project to clip space
                f64 clip_x = FUN_8001ca5c((f64)(cos_val * (f32)(size_x * col_t) + screen_x), -6);
                f64 clip_y = FUN_8001cac0((f64)(sin_val * (f32)(size_y * col_t) + screen_y), -6);

                grid[cell * 4 + 2] = (f32)clip_x;
                grid[cell * 4 + 3] = (f32)clip_y;

                col++;
            } while ((int)col < 10);

            row++;
            grid_offset += 10;
        } while (row < 10);

        // Setup GX state
        FUN_8016e8ac(0);
        FUN_8016f00c(0xe);
        FUN_8016ee98();
        FUN_8016e000(1);
        gx_bind_material_fallback();
        FUN_8016e000(/* restore */);
        FUN_8016efe4();
        FUN_8016ee98();
        FUN_8016f00c(1);
        FUN_8001d4b4(); // invalidate vertex cache

        // Draw 9 triangle strips with random per-strip colors
        int strip = 0;
        do {
            // Generate random color components for this strip
            u32 r0 = FUN_80132348((f64)FLOAT_80614e00, 0x80808080);
            u32 g0 = FUN_80132348((f64)FLOAT_80614e00, 0x80808080);
            u32 b0 = FUN_80132348((f64)FLOAT_80614e00, 0x80808080);
            u32 a0 = FUN_80132348((f64)FLOAT_80614e00, 0x80808080);
            u32 r1 = FUN_80132348((f64)FLOAT_80614e00, 0x80808080);
            u32 g1 = FUN_80132348((f64)FLOAT_80614e00, 0x80808080);
            u32 b1 = FUN_80132348((f64)FLOAT_80614e00, 0x80808080);
            u32 a1 = FUN_80132348((f64)FLOAT_80614e00, 0x80808080);

            u32 color_0 = (g0 & 0xff000000) | ((r0 >> 8) & 0xff00) << 8 |
                          (b0 & 0xff00) | (a0 & 0xff);
            u32 color_1 = (g1 & 0xff000000) | ((r1 >> 8) & 0xff00) << 8 |
                          (b1 & 0xff00) | (a1 & 0xff);

            FUN_80172a4c(2, 0x92, 0, 0x16, -8); // begin strip (22 verts)

            int vert = 0;
            int remaining = 11;
            do {
                int col_idx = (vert % 10) * 10;
                int v0 = strip + col_idx;
                int v1 = strip + 1 + col_idx;

                // Row 0 vertex
                DAT_cc008000 = grid[v0 * 4 + 0];
                DAT_cc008000 = grid[v0 * 4 + 1];
                DAT_cc008000 = FLOAT_80614e04;
                DAT_cc008000 = color_0;
                DAT_cc008000 = grid[v0 * 4 + 2];
                DAT_cc008000 = grid[v0 * 4 + 3];

                // Row 1 vertex
                DAT_cc008000 = grid[v1 * 4 + 0];
                DAT_cc008000 = grid[v1 * 4 + 1];
                DAT_cc008000 = FLOAT_80614e04;
                DAT_cc008000 = color_1;
                DAT_cc008000 = grid[v1 * 4 + 2];
                DAT_cc008000 = grid[v1 * 4 + 3];

                vert++;
                remaining--;
            } while (remaining != 0);

            FUN_80172c54(); // end strip
            strip++;
        } while (strip < 9);

        FUN_8001d4d4(); // restore vertex cache
        FUN_8016f00c(1);
    }

    FUN_80382728(); // GX pop
}

// ============================================================================
// 0x800d3498 | weather_particle_spawn_type5
// Spawns a type-5 particle (deferred energy ring). Finds free slot, sets
// type=5, copies position, lifetime from FLOAT_80614eb4.
// ============================================================================
void weather_particle_spawn_type5(int player_idx, int* position) {
    int* slot;

    slot = weather_particle_find_free_slot(player_idx);
    if (slot != (int*)0x0) {
        *slot = 5;                         // type = 5 (energy ring)
        slot[1] = (int)FLOAT_80614e04;    // current_time = 0.0
        slot[2] = (int)FLOAT_80614eb4;    // max_lifetime
        slot[3] = *position;
        slot[4] = position[1];
        slot[5] = position[2];
    }
}

// ============================================================================
// 0x800d34fc | weather_get_time_of_day
// Returns the current time-of-day blending factor for weather color
// interpolation. Returns 0.0 if the current level has no dynamic weather,
// otherwise queries FUN_80253e74 for the actual time value.
// ============================================================================
f64 weather_get_time_of_day(void) {
    f64 result = (f64)FLOAT_80614e04; // default 0.0

    int has_weather = FUN_801c2dcc(DAT_80612614);
    if (has_weather != 0) {
        result = FUN_80253e74(); // query actual time-of-day
    }
    return result;
}

// ============================================================================
// 0x800d3548 | weather_stub_empty
// Empty stub function (no-op). Placeholder in dispatch table.
// ============================================================================
void weather_stub_empty(void) {
    return;
}

// ============================================================================
// 0x800d354c | weather_compute_ambient_color
// Computes per-frame ambient lighting color by interpolating between two level
// color palettes based on time-of-day. Writes RGBA channels + particle overlay
// flag + ambient parameters into the active player's weather context.
// Called from weather_system_update each frame.
// ============================================================================
void weather_compute_ambient_color(void) {
    int player_idx;
    int level_def;
    int color_idx_a, color_idx_b;
    u8* palette_a;
    u8* palette_b;
    f64 time_of_day;
    f64 inv_time;
    int ctx_offset;

    player_idx = FUN_803826ec(); // returns player index from GX state

    level_def = level_get_current_def();
    color_idx_a = *(int*)(level_def + 0x1c);
    level_def = level_get_current_def();
    color_idx_b = *(int*)(level_def + 0x20);

    palette_a = FUN_8023902c(color_idx_a);
    palette_b = FUN_8023902c(color_idx_b);

    time_of_day = weather_get_time_of_day();
    inv_time = (f64)(FLOAT_80614e00 - (f32)time_of_day);
    ctx_offset = player_idx * 0x1e8;

    // Interpolate RGB channels (group B: lighten)
    (&DAT_80569a3c)[player_idx * 0x7a] =
        (int)((f64)(f32)palette_a[0] * inv_time + (f64)((f32)palette_b[0] * (f32)time_of_day));
    (&DAT_80569a40)[player_idx * 0x7a] =
        (int)((f64)(f32)palette_a[1] * inv_time + (f64)((f32)palette_b[1] * (f32)time_of_day));
    (&DAT_80569a44)[player_idx * 0x7a] =
        (int)((f64)(f32)palette_a[2] * inv_time + (f64)((f32)palette_b[2] * (f32)time_of_day));

    // Interpolate color correction mode (alpha channel)
    (&DAT_80569a28)[player_idx * 0x7a] =
        (int)((f64)(f32)palette_a[3] * inv_time + (f64)((f32)palette_b[3] * (f32)time_of_day));

    // Particle overlay flag (max of both palettes' byte 4)
    u32 overlay_flag = (u32)palette_b[4];
    if ((u32)palette_b[4] < (u32)palette_a[4]) {
        overlay_flag = (u32)palette_a[4];
    }
    (&DAT_80569a90)[player_idx * 0x7a] = overlay_flag;

    // Interpolate ambient parameters (float fields at byte offsets 8, 12, 16)
    *(f32*)(&DAT_80569aa4 + ctx_offset) =
        (f32)((f64)*(f32*)(palette_a + 8) * inv_time +
              (f64)(*(f32*)(palette_b + 8) * (f32)time_of_day));

    // Color lerp at byte offset 0x14
    u32 blended_color = FUN_80132228(inv_time, *(u32*)(palette_a + 0x14), *(u32*)(palette_b + 0x14));
    *(u32*)(&DAT_80569aa8 + ctx_offset) = blended_color;

    // Additional ambient floats
    *(f32*)(&DAT_80569a9c + ctx_offset) =
        (f32)((f64)*(f32*)(palette_a + 0xc) * inv_time +
              (f64)(*(f32*)(palette_b + 0xc) * (f32)time_of_day));
    *(f32*)(&DAT_80569aa0 + ctx_offset) =
        (f32)((f64)*(f32*)(palette_a + 0x10) * inv_time +
              (f64)(*(f32*)(palette_b + 0x10) * (f32)time_of_day));
    *(f32*)(&DAT_80569a98 + ctx_offset) = FLOAT_80614e00; // always 1.0

    FUN_80382738(); // GX pop
}

// ============================================================================
// 0x800d3764 | weather_render_gate
// Renders a weather boundary "gate" effect (visible rain/snow edge).
// Renders to an offscreen buffer at half/quarter resolution (256 or 128 for
// splitscreen), applies a distortion texture, then composites back to the
// main framebuffer. Used on specific rain levels.
// ============================================================================
void weather_render_gate(u32 param_1, u32 param_2, u32 param_3, u32 param_4) {
    int prev_shader;
    u32 resolution;
    int render_target[2];
    BOOL is_splitscreen;

    FUN_803826e4(); // GX push

    FUN_8016e000(1);
    FUN_8016f00c(0x18);
    FUN_8016e010((f64)FLOAT_80614e00);
    gx_bind_material_by_index_w(-6);
    FUN_8016e000(/* restore */);
    FUN_8016ee98();
    FUN_8016f00c(1);

    // Determine resolution based on splitscreen mode
    is_splitscreen = FALSE;
    if (DAT_805015bd == '\n' && 1 < DAT_80612610) {
        is_splitscreen = TRUE;
    }
    resolution = is_splitscreen ? 0x80 : 0x100; // 128 or 256

    // Render to offscreen target
    FUN_8016f24c(8, 0, param_2, resolution, param_2 + resolution, 0x80000000);
    prev_shader = FUN_8016e490(-0x7fe70b3c); // distortion shader

    FUN_80170410(0, param_2, resolution, param_2 + resolution,
                 0, 0, DAT_806109c4 << 4, DAT_806109c8 << 4);

    // Create render target
    FUN_8017bc4c(resolution, resolution, render_target);
    FUN_8017bf04(render_target[0]);
    FUN_8016e010((f64)FLOAT_80614e00);
    FUN_8017bdb4(render_target[0], 0, param_2, resolution, resolution, 3);
    FUN_8018f32c(render_target[0]);
    FUN_8016f00c(1);
    FUN_8016ee98();

    // Apply composite shader
    FUN_8016e490(-0x7fe70ccc);
    FUN_803cadac(1, 0);

    // Compute UV mapping from params to screen
    f32 alpha = (f32)((f64)FLOAT_80614e40 * /* extraout_f1 */);

    FUN_801706cc(
        (f64)((f32)param_1 / (f32)DAT_806109c4),
        (f64)((f32)param_2 / (f32)DAT_806109c8),
        (f64)((f32)param_3 / (f32)DAT_806109c4),
        (f64)((f32)param_4 / (f32)DAT_806109c8),
        (int)param_1, param_2, param_3, param_4);

    // Restore and cleanup
    FUN_8016e490(prev_shader);
    FUN_803cadac(0, 0xff);
    FUN_8017bf24();
    FUN_8017c454(render_target[0]);

    FUN_80382730(); // GX pop
}
