// =============================================================================
// Sprite Grid & 2D Overlay Rendering
// Address Range: 0x800cdb1c - 0x800ce510 (batch 280)
// Date: 2026-08-25
// =============================================================================

#include "common.h"
#include "gx.h"
#include "particle.h"
#include "sprite.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

// Sprite grid cell data table (row stride 0x82, column stride 0xD)
static SpriteGridCell g_sprite_grid_cells[]; // DAT_8056a580

// Sprite grid vertex/corner data (row stride 0x39C = 0x54 * cols+1)
static SpriteGridVertex g_sprite_grid_vertices[]; // DAT_8056b7c8

// Grid working buffer (0xB70 bytes, cleared on init)
static u8 g_sprite_grid_buffer[0xB70]; // DAT_80569a10 region

// Global weather/overlay counter
static int g_sprite_overlay_count; // DAT_80611ddc

// Viewport rect (x, y, x2, y2, ...)
extern int* g_viewport_rect; // DAT_80611748

// Screen dimensions
extern u32 g_screen_width;  // DAT_806109c4
extern u32 g_screen_height; // DAT_806109c8

// Cell sizes (set by particle_set_pool_sizes)
extern u32 g_cell_width;    // DAT_80611de4 (0x40)
extern u32 g_cell_height;   // DAT_80611de0 (0x38 or 0x40)

// Scene room data
extern int g_scene_room_array; // DAT_80611d78
extern int g_scene_room_count; // via FUN_801c1714

// ---------------------------------------------------------------------------
// Address: 0x800cdb1c | Name: particle_get_current_player_emitter
// Returns emitter def for the current player's screen index.
// Central accessor called by 15+ functions.
// ---------------------------------------------------------------------------
ParticleEmitterDef* particle_get_current_player_emitter(void)
{
    int player_viewport_index = *(int*)(g_scene_state + 0x214);
    return particle_get_emitter_def(player_viewport_index);
}

// ---------------------------------------------------------------------------
// Address: 0x800cdb38 | Name: sprite_grid_get_cell
// Returns pointer to sprite grid cell at (col, row). Validates bounds
// against screen dimensions / cell sizes. Returns NULL if out of range.
// ---------------------------------------------------------------------------
SpriteGridCell* sprite_grid_get_cell(u32 col, u32 row)
{
    u32 max_cols = g_screen_width / g_cell_width;
    u32 max_rows = g_screen_height / g_cell_height;

    if (col < 0 || col >= max_cols || row < 0 || row >= max_rows) {
        return NULL;
    }

    // Cell table: row * 0x82 + col * 0xD
    return (SpriteGridCell*)((u8*)&g_sprite_grid_cells + row * 0x82 + col * 0xD);
}

// ---------------------------------------------------------------------------
// Address: 0x800cdb94 | Name: sprite_grid_init_wrapper
// Wrapper: calls sprite_grid_init.
// ---------------------------------------------------------------------------
void sprite_grid_init_wrapper(void)
{
    sprite_grid_init();
}

// ---------------------------------------------------------------------------
// Address: 0x800cdbb4 | Name: sprite_grid_init
// MASSIVE: Initializes the 2D sprite grid system.
// 1. Clears 0xB70 bytes of grid buffer
// 2. Initializes 6 emitter defs with pool sizes (extended/base)
// 3. Builds cell grid: for each cell, determines which scene room it overlaps
//    by testing cell center against room bounding boxes
// 4. Builds corner-vertex grid with boundary flags for edge detection
// 5. Calls 3 follow-up init functions (weather, overlay, effects)
// ---------------------------------------------------------------------------
void sprite_grid_init(void)
{
    begin_grid_init(); // FUN_803826c0

    // Clear working buffer
    memset_wrapper(&g_sprite_grid_buffer, 0, 0xB70);

    // Initialize 6 emitter defs with pool sizes
    g_sprite_overlay_count = 0;
    for (int i = 0; i < 6; i++) {
        ParticleEmitterDef* def = particle_get_emitter_def(i);
        def->field_13 = 0;  // active count
        def->field_14 = 0;  // allocated count

        def = particle_get_emitter_def(i);
        def->field_15 = g_cell_width;  // cell width for this emitter

        def = particle_get_emitter_def(i);
        def->field_16 = g_cell_height; // cell height for this emitter
    }

    u32 max_cols = g_screen_width / g_cell_width;
    u32 max_rows = g_screen_height / g_cell_height;

    float half_cell_w = (float)g_cell_width * 0.5f;  // FLOAT_80614de8
    float half_cell_h = (float)g_cell_height * 0.5f;

    // Phase 1: Map each cell to a scene room
    for (u32 row = 0; row < max_rows; row++) {
        for (u32 col = 0; col < max_cols; col++) {
            SpriteGridCell* cell = sprite_grid_get_cell(col, row);

            // Compute cell center in screen coordinates
            float center_x = half_cell_w + (float)(col * g_cell_width);
            float center_y = half_cell_h + (float)(row * g_cell_height);

            // Store corner vertex pointers (4 corners)
            cell->corner_tl = &g_sprite_grid_vertices[row][col];
            cell->corner_tr = &g_sprite_grid_vertices[row][col + 1];
            cell->corner_bl = &g_sprite_grid_vertices[row + 1][col];
            cell->corner_br = &g_sprite_grid_vertices[row + 1][col + 1];

            // Find which scene room contains this cell center
            cell->room_id = -1;
            int room_count = get_scene_room_count(); // FUN_801c1714
            for (int room = 0; room < room_count; room++) {
                // Skip if global flag set (single-room mode)
                if ((g_global_flags & 0x20000000) != 0) {
                    cell->room_id = 0;
                    break;
                }

                int room_ptr = g_scene_room_array + room * 0x141C;
                if (room_ptr == 0) continue;

                int room_state = *(int*)(room_ptr + 4);
                if (room_state < 0) continue;

                int room_bounds = *(int*)(room_ptr + 8);
                if (room_bounds == 0) continue;

                // Test cell center against room 2D bounding box
                float room_min_x = (float)(*(int*)(room_bounds + 0x224));
                float room_max_x = (float)(*(int*)(room_bounds + 0x228));
                float room_min_y = (float)(*(int*)(room_bounds + 0x238));
                float room_max_y = (float)(*(int*)(room_bounds + 0x23C));

                if (center_x > room_min_x && center_x < room_max_x &&
                    center_y > room_min_y && center_y < room_max_y) {
                    cell->room_id = room;
                    break;
                }
            }
        }
    }

    // Phase 2: Build corner-vertex grid with boundary flags
    for (u32 row = 0; row <= max_rows; row++) {
        for (u32 col = 0; col <= max_cols; col++) {
            SpriteGridVertex* vtx = &g_sprite_grid_vertices[row][col];

            // Store pixel position
            vtx->pixel_x = col * g_cell_width;
            vtx->pixel_y = row * g_cell_height;

            // Store float position
            vtx->pos_x = (float)vtx->pixel_x;
            vtx->pos_y = (float)vtx->pixel_y;

            // Initialize flags
            vtx->is_initialized = 1;
            vtx->boundary_flags = 0;

            // Get adjacent cells for boundary detection
            SpriteGridCell* cell_tl = sprite_grid_get_cell(col - 1, row - 1);
            SpriteGridCell* cell_tr = sprite_grid_get_cell(col, row - 1);
            SpriteGridCell* cell_bl = sprite_grid_get_cell(col - 1, row);
            SpriteGridCell* cell_br = sprite_grid_get_cell(col, row);

            // Horizontal boundary: if left/right cells differ in room
            vtx->boundary_x = 0;
            if (col == 0 || col == max_cols + 1) {
                vtx->boundary_x = 1;
            } else if (cell_tl != NULL && cell_tr != NULL &&
                       cell_tl->room_id != cell_tr->room_id) {
                vtx->boundary_x = 1;
            } else if (cell_bl != NULL && cell_br != NULL &&
                       cell_bl->room_id != cell_br->room_id) {
                vtx->boundary_x = 1;
            }

            // Vertical boundary: if top/bottom cells differ in room
            vtx->boundary_y = 0;
            if (row == 0 || row == max_rows + 1) {
                vtx->boundary_y = 1;
            } else if (cell_tl != NULL && cell_bl != NULL &&
                       cell_tl->room_id != cell_bl->room_id) {
                vtx->boundary_y = 1;
            } else if (cell_tr != NULL && cell_br != NULL &&
                       cell_tr->room_id != cell_br->room_id) {
                vtx->boundary_y = 1;
            }
        }
    }

    // Follow-up subsystem inits
    sprite_grid_init_weather();  // FUN_800d1774
    sprite_grid_init_effects();  // FUN_800d0400
    sprite_grid_init_overlay();  // FUN_800d3548

    end_grid_init(); // FUN_8038270c
}

// ---------------------------------------------------------------------------
// Address: 0x800ce078 | Name: sprite_overlay_stub_nop
// Empty stub (no-op).
// ---------------------------------------------------------------------------
void sprite_overlay_stub_nop(void)
{
    // intentionally empty
}

// ---------------------------------------------------------------------------
// Address: 0x800ce07c | Name: sprite_draw_streak
// Draws a screen-space streak/trail effect (rain, speed lines, bullet tracers).
// Iterates N segments computing interpolated positions and tapering widths.
// Uses sin/cos for angle, submits each segment quad via textured quad renderer.
// Called from cutscene_apply_visual_effects.
// ---------------------------------------------------------------------------
void sprite_draw_streak(/* register params */
                        float vel_x, float vel_y, float vel_z, float vel_w,
                        int texture_id, u32 color_start_x, u32 color_start_y,
                        u32 color_end_x, u32 color_end_y)
{
    // Get context (color byte + render mode)
    u64 context = get_streak_context(); // FUN_803826c4
    int render_mode = (int)(context >> 32);
    u8 alpha = (u8)(context & 0xFF);
    float alpha_f = (float)alpha;

    // Compute angle sin/cos for streak direction
    float sin_angle = sinf(vel_w);    // FUN_803900e4
    float cos_angle = atan2f(vel_w);  // FUN_80390324 (angle computation)

    // Set texture and render state
    set_particle_texture(texture_id); // FUN_8016e77c
    set_render_mode(render_mode);     // FUN_8016f00c
    set_texture_mode(0);              // FUN_8016e8ac

    float taper_scale = STREAK_TAPER_SCALE; // FLOAT_80614de8
    float base_width = STREAK_BASE_WIDTH;   // FLOAT_80614e00

    // Compute partial segment factor for last segment
    float partial = vel_w - (float)((int)cos_angle);

    // Draw N-1 segments (N = total length in segments)
    for (u32 seg = 1; seg < (u32)sin_angle; seg++) {
        float t = (float)seg / vel_w;

        // Width tapers along streak
        float width = -(taper_scale * t - base_width);

        // If last segment, apply partial factor
        if (seg == (u32)sin_angle - 1 && (float)((int)cos_angle) < vel_w) {
            width *= partial;
        }

        // Scale by alpha
        set_streak_width(width * alpha_f); // FUN_803825c0

        // Compute interpolated position
        float px = vel_x * t + (float)color_start_x;  // lerp start -> end
        float py = vel_y * t + (float)color_start_y;
        float qx = vel_z * t + (float)color_end_x;
        float qy = vel_w * t + (float)color_end_y;

        // Submit quad segment
        draw_textured_quad_2d(px, py, qx, qy, base_width,
                             color_start_x << 4, color_start_y << 4,
                             color_end_x << 4, color_end_y << 4); // FUN_80171f7c
    }

    end_streak_draw(); // FUN_80382710
}

// ---------------------------------------------------------------------------
// Address: 0x800ce330 | Name: sprite_draw_fullscreen_quad
// Draws a full-screen colored quad. Sets render mode 1 (opaque), texture -7
// (solid color), and submits the quad with pixel coords and UV coords.
// Used for screen fades and mapmaker overlay.
// ---------------------------------------------------------------------------
void sprite_draw_fullscreen_quad(u32 param_1, u32 param_2, u32 x2, u32 y2)
{
    // Get context (screen coords)
    u64 context = get_quad_context(); // FUN_803826e0
    int x1 = (int)(context >> 32);
    int y1 = (int)context;

    // Save and set render state
    int prev_render = set_render_mode(1);   // opaque
    int prev_tex = set_texture_mode(0);     // no filtering

    // Set solid color texture (negative = special mode)
    set_particle_texture(-7); // FUN_8016e77c

    // Draw quad with pixel coords and UV (shifted << 4 for fixed-point)
    draw_textured_quad_rect(x1, y1, x2, y2,
                           x1 << 4, y1 << 4, x2 << 4, y2 << 4); // FUN_80170410

    // Restore render state
    set_render_mode(prev_render);
    set_texture_mode(prev_tex);

    end_quad_draw(); // FUN_8038272c
}

// ---------------------------------------------------------------------------
// Address: 0x800ce3d0 | Name: sprite_draw_tiled_background
// Draws a tiled texture background. Looks up texture dimensions from sprite
// table, iterates in grid steps filling the rect with randomly UV-offset tiles.
// Used for HUD/menu backgrounds.
// ---------------------------------------------------------------------------
void sprite_draw_tiled_background(u32 param_1, u32 param_2,
                                  int rect_top, int rect_left, int rect_bottom)
{
    // Get context (texture index + start Y)
    u64 context = get_tile_context(); // FUN_803826cc
    int tex_index = (int)(context >> 32);
    int start_y = (int)context;

    // Look up texture info from sprite table (0x58 bytes per entry)
    int* sprite_entry = g_sprite_table + tex_index * 0x58;
    u32 tex_width = (u32)*(u16*)(sprite_entry + 0x4E / 4);   // pixel width
    u32 tex_height = (u32)*(u16*)(sprite_entry + 0x52 / 4);  // pixel height

    // Set texture
    set_particle_texture(tex_index); // FUN_8016e77c

    // Tile dimensions (in pixels, scaled by 4 for step, *0x40 for UV range)
    u32 uv_range_x = tex_width * 0x40;
    u32 uv_range_y = tex_height * 0x40;
    int step_x = tex_height * 4;
    int step_y = tex_width * 4;

    // Fill rect with tiles
    for (int y = start_y; y < rect_left; y += step_y) {
        for (int x = rect_top; x < rect_bottom; x += step_x) {
            // Random UV offset for variety
            u32 rng_u = get_random_u32() % uv_range_x; // FUN_8013104c
            u32 rng_v = get_random_u32() % uv_range_y;

            // Draw tile quad
            draw_textured_quad_rect(y, x, y + step_y, x + step_x,
                                   rng_u, rng_v,
                                   rng_u + uv_range_x, rng_v + uv_range_y); // FUN_80170410
        }
    }

    end_tile_draw(); // FUN_80382718
}

// ---------------------------------------------------------------------------
// Address: 0x800ce4b4 | Name: sprite_fill_viewport_color
// Fills the current viewport rect with a solid color (alpha from param low byte).
// Sets render mode 1, texture mode 0, then draws solid rect over viewport bounds.
// ---------------------------------------------------------------------------
void sprite_fill_viewport_color(u32 rgba_packed)
{
    set_render_mode(1);    // FUN_8016f00c — opaque
    set_texture_mode(0);   // FUN_8016e8ac — no filtering

    // Get viewport bounds from global rect
    int vp_x1 = g_viewport_rect[0];
    int vp_y1 = g_viewport_rect[5];
    int vp_x2 = g_viewport_rect[1] + 1;
    int vp_y2 = g_viewport_rect[6] + 1;

    // Draw solid color fill (alpha = low byte)
    u8 alpha = (u8)(rgba_packed & 0xFF);
    draw_solid_rect(vp_x1, vp_y1, vp_x2, vp_y2, alpha); // FUN_8016f4bc
}

// ---------------------------------------------------------------------------
// Address: 0x800ce510 | Name: sprite_draw_viewport_quad
// Draws a single textured quad covering the viewport. Sets texture -5 (screen
// buffer/effect texture), uses viewport bounds from DAT_80611748 with alpha
// scaling. Called from cutscene effects and scene transitions.
// ---------------------------------------------------------------------------
void sprite_draw_viewport_quad(void)
{
    // Get context (render mode)
    u64 context = get_spawn_context(); // FUN_803826ec
    int render_mode = (int)(context >> 32);

    int prev_render = set_render_mode(render_mode); // FUN_8016f00c

    // Set special texture -5 (screen capture / post-process buffer)
    set_particle_texture(-5); // FUN_8016e77c
    set_texture_mode(0);      // FUN_8016e8ac

    // Get viewport bounds
    int vp_x1 = g_viewport_rect[0];
    int vp_y1 = g_viewport_rect[5];
    int vp_x2 = g_viewport_rect[1];
    int vp_y2 = g_viewport_rect[6];

    float alpha = VIEWPORT_QUAD_ALPHA; // FLOAT_80614e00

    // Draw textured quad over full viewport
    draw_textured_quad_2d(
        (float)vp_x1, (float)vp_y1, (float)vp_x2, (float)vp_y2,
        alpha,
        vp_x1 << 4, vp_y1 << 4, vp_x2 << 4, vp_y2 << 4); // FUN_80171f7c

    // Restore render mode
    set_render_mode(prev_render);

    restore_spawn_context(); // FUN_80382738
}
