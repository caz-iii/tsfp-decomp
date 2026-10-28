// =============================================================================
// Water Surface Rendering System
// Address: 0x800f26dc - 0x800f2f14 | Batch 302 | Date: 2026-08-25
// =============================================================================
// Implements a 3x3 tiled water surface with UV scrolling animation, wave phase
// rotation, and GX quad submission. The system uses level-defined water config
// (color, position, scale) accessed via level_get_weapon_set_ptr() which appears
// to be a shared level property accessor (not actually weapon-related here).
// =============================================================================

#include "common.h"
#include "gx.h"
#include "math_util.h"
#include "level.h"
#include "entity.h"
#include "stream.h"

// --- Constants and data references ---

#define WATER_GRID_SIZE   3
#define WATER_TILE_STRIDE 0x110
#define WATER_ROW_STRIDE  0x330

// Global state (BSS)
static float g_water_uv_scroll_u;       // 0x80611ed8 - current U scroll accumulator
static float g_water_uv_scroll_v;       // 0x80611edc - current V scroll accumulator
static float g_water_wave_phase;        // 0x80611ee0 - wave animation phase
static float g_water_scroll_speed_u;    // 0x80611ed0 - U scroll speed (deg/frame)
static float g_water_scroll_speed_v;    // 0x80611ed4 - V scroll speed (deg/frame)
static int   g_water_enabled;           // 0x80611f24 - water surface active flag
static int   g_water_animated_mode;     // 0x80611f04 - 1 = vertex-animated, 0 = UV-only
static int   g_water_stream_queued;     // 0x80611f08 - animation stream loaded flag
static int   g_water_params_dirty;      // 0x80611f14 - set_params pending flag
static int   g_water_pending_type;      // 0x80611f18 - pending type after params
static u32   g_water_color_primary;     // 0x80611f20 - byte-swapped RGBA primary
static u32   g_water_color_secondary;   // 0x80611f1c - byte-swapped RGBA secondary
static int   g_water_material_current;  // 0x80611f0c - current material index
static int   g_water_material_prev;     // 0x80611ee4 - previous material index
static int   g_water_tile_materials[10]; // 0x80570e68 - per-tile material IDs
static int   g_water_camera_override;   // 0x80611d54 - camera entity override ptr
static int   g_water_render_index;      // 0x80612250 - current render tile index
static int   g_water_render_col;        // 0x80612258 - current render column

// Grid tile data buffer (3x3 tiles, 0x110 bytes each)
static u8    g_water_tile_data[WATER_GRID_SIZE * WATER_ROW_STRIDE]; // 0x80570f80

// Constants (rodata)
static const float CONST_360      = 360.0f;  // 0x80615340
static const float CONST_0        = 0.0f;    // 0x80615338
static const float CONST_1        = 1.0f;    // 0x80615378
static const float CONST_UV_SCALE = /* ... */; // 0x80615370
static const float CONST_UV_OFFSET = /* ... */; // 0x80615374

// =============================================================================
// Address: 0x800f28b0 | Name: water_surface_grid_to_uv
// Converts a grid index (0-2) to a UV coordinate with optional ripple offset.
// =============================================================================
static float water_surface_grid_to_uv(u32 index) {
    float uv = (float)index * CONST_UV_SCALE;

    if (g_water_animated_mode != 0) {
        if (index == 1) {
            uv += CONST_UV_OFFSET;
        } else if (index == 2) {
            uv -= CONST_UV_OFFSET;
        }
    }

    return uv;
}

// =============================================================================
// Address: 0x800f26dc | Name: water_surface_submit_quads
// Submits water tile quad strips to GX FIFO (0xCC008000).
// Iterates rows/cols of a single tile, writing position (XYZ), color (RGBA
// byte-swapped), and texture coordinates for each vertex in GX_QUADS format.
// =============================================================================
static void water_surface_submit_quads(void) {
    int row, col;
    int tile_ptr; // extraout_r4 from calling convention
    int material_index;

    // Prologue: save volatile regs
    // FUN_803826cc();

    // Resolve material for this tile
    // FUN_8017fab4();
    material_index = *(int*)(tile_ptr + 0x104);
    if (material_index < 0 || material_index >= g_material_table_count ||
        material_index <= g_material_table_min) {
        // FUN_801720a8() - use default material
    } else {
        g_water_current_material_ptr = g_material_table_base + material_index * 0x58;
    }

    // Submit quads row by row
    int row_offset = 0;
    for (row = 0; row < *(int*)(tile_ptr + 0xdc); row++) {
        int col_offset = 0;
        int next_row_base = tile_ptr + (row + 1) * 0x48;

        for (col = 0; col < *(int*)(tile_ptr + 0xd8); col++) {
            u32* v0 = (u32*)(tile_ptr + row_offset + col_offset);
            u32* v1 = (u32*)(tile_ptr + row_offset + (col + 1) * 0x18);
            u32* v2 = (u32*)(next_row_base + col_offset);
            u32* v3 = (u32*)(next_row_base + (col + 1) * 0x18);

            // GX begin: GX_QUADS, 4 vertices
            GX_Begin(GX_QUADS, 0, 4);

            // Vertex 0: pos XYZ + color (byte-swap) + UV
            GX_FIFO = v0[0]; // X
            GX_FIFO = v0[1]; // Y
            GX_FIFO = v0[2]; // Z
            u32 c0 = v0[5];
            GX_FIFO = BSWAP32(c0); // RGBA byte-swapped
            GX_FIFO = v0[3]; // U
            GX_FIFO = v0[4]; // V

            // Vertex 1: pos XYZ + color + UV
            GX_FIFO = v1[0];
            GX_FIFO = v1[1];
            GX_FIFO = v1[2];
            u32 c1 = v1[5];
            GX_FIFO = BSWAP32(c1);
            GX_FIFO = v1[3];
            GX_FIFO = v1[4];

            // Vertex 2: pos XYZ + color + UV
            GX_FIFO = v2[0];
            GX_FIFO = v2[1];
            GX_FIFO = v2[2];
            u32 c2 = v2[5];
            GX_FIFO = BSWAP32(c2);
            GX_FIFO = v2[3];
            GX_FIFO = v2[4];

            // Vertex 3: pos XYZ + color + UV
            GX_FIFO = v3[0];
            GX_FIFO = v3[1];
            GX_FIFO = v3[2];
            u32 c3 = v3[5];
            GX_FIFO = BSWAP32(c3);
            GX_FIFO = v3[3];
            GX_FIFO = v3[4];

            col_offset += 0x18;
        }
        row_offset += 0x48;
    }

    // Epilogue: restore regs
    // FUN_80382718();
}

// =============================================================================
// Address: 0x800f2910 | Name: water_surface_build_grid
// Builds the 3x3 water surface grid mesh data. For each cell, computes UV
// coordinates, stores position/scale/color from level water config, then calls
// water_surface_generate_mesh() to tessellate.
// =============================================================================
static void water_surface_build_grid(void) {
    int row, col;
    int row_offset = 0;

    // Prologue
    // FUN_803826e0();

    // Read water position/scale from level config
    int water_set = level_get_weapon_set_ptr();
    float pos_x = *(float*)(water_set + 0x18);
    float pos_y = *(float*)(water_set + 0x1c);
    float pos_z = *(float*)(water_set + 0x20);

    for (row = 0; row < WATER_GRID_SIZE; row++) {
        int col_offset = 0;
        for (col = 0; col < WATER_GRID_SIZE; col++) {
            int tile_base = (int)&g_water_tile_data + col_offset + row_offset;

            // Store UV bounds for this cell (2x3 = 6 UV coords)
            *(float*)(tile_base + 0xe0) = water_surface_grid_to_uv(col);
            *(float*)(tile_base + 0xe4) = water_surface_grid_to_uv(row);
            *(float*)(tile_base + 0xe8) = water_surface_grid_to_uv(col + 1);
            *(float*)(tile_base + 0xec) = *(float*)(tile_base + 0xe4);
            *(float*)(tile_base + 0xf0) = *(float*)(tile_base + 0xe0);
            *(float*)(tile_base + 0xf4) = water_surface_grid_to_uv(row + 1);

            // Grid dimensions (2x2 quads per tile)
            *(int*)(tile_base + 0xd8) = 2;
            *(int*)(tile_base + 0xdc) = 2;

            // Position and scale
            *(float*)(tile_base + 0xf8) = pos_x;
            *(float*)(tile_base + 0xfc) = pos_y;
            *(float*)(tile_base + 0x100) = pos_z;

            // Colors
            *(u32*)(tile_base + 0x104) = g_water_anim_handle;
            *(u32*)(tile_base + 0x108) = g_water_color_primary;
            *(u32*)(tile_base + 0x10c) = g_water_color_secondary;

            // Generate tessellated mesh for this tile
            water_surface_generate_mesh();

            col_offset += WATER_TILE_STRIDE;
        }
        row_offset += WATER_ROW_STRIDE;
    }

    // Epilogue
    // FUN_8038272c();
}

// =============================================================================
// Address: 0x800f2a3c | Name: water_surface_animate_uvs
// Per-frame UV animation tick. Advances two UV scroll accumulators and a wave
// phase timer, wrapping to valid ranges. Then recomputes UV coordinates for all
// 9 grid cells via water_surface_compute_uvs().
// =============================================================================
static void water_surface_animate_uvs(void) {
    int row, col;

    // Prologue
    // FUN_803826e4();

    // Compute time-scaled delta
    float dt_scale = CONST_FRAME_TIME * (CONST_360 / (float)g_target_framerate);

    // Advance U scroll
    g_water_uv_scroll_u += g_water_scroll_speed_u * dt_scale;
    if (g_water_uv_scroll_u >= CONST_360) {
        g_water_uv_scroll_u -= CONST_360;
    }
    if (g_water_uv_scroll_u < CONST_0) {
        g_water_uv_scroll_u += CONST_360;
    }

    // Advance V scroll
    g_water_uv_scroll_v += g_water_scroll_speed_v * dt_scale;
    if (g_water_uv_scroll_v >= CONST_360) {
        g_water_uv_scroll_v -= CONST_360;
    }
    if (g_water_uv_scroll_v < CONST_0) {
        g_water_uv_scroll_v += CONST_360;
    }

    // Advance wave phase [0, 1)
    g_water_wave_phase += dt_scale;
    if (g_water_wave_phase >= CONST_1) {
        g_water_wave_phase -= CONST_1;
    }

    // Recompute UVs for all tiles
    int row_offset = 0;
    for (row = 0; row < WATER_GRID_SIZE; row++) {
        int col_offset = 0;
        for (col = 0; col < WATER_GRID_SIZE; col++) {
            water_surface_compute_uvs((int)&g_water_tile_data + col_offset + row_offset);
            col_offset += WATER_TILE_STRIDE;
        }
        row_offset += WATER_ROW_STRIDE;
    }

    // Epilogue
    // FUN_80382730();
}

// =============================================================================
// Address: 0x800f2ba4 | Name: water_surface_render
// Main water surface render entry point. Sets up camera position, builds a
// rotation matrix from the wave phase, concatenates with the view matrix, and
// loads the result as the texture coordinate matrix. Then iterates the 3x3
// grid calling water_surface_submit_quads() for each tile.
// =============================================================================
static void water_surface_render(void) {
    float mtx_a[16];
    float mtx_b[16];
    float mtx_c[16];
    float mtx_result[26]; // padded for matrix ops

    // Prologue
    // FUN_803826e0();

    // Get camera position (override or player camera)
    float cam_x, cam_y, cam_z;
    int cam_override = *(int*)(g_water_camera_override + 0xe04);
    if (cam_override == 0) {
        cam_x = *(float*)(g_water_camera_override + 0x27c);
        cam_y = *(float*)(g_water_camera_override + 0x280);
        cam_z = *(float*)(g_water_camera_override + 0x284);
    } else {
        cam_x = *(float*)(cam_override + 0x64);
        cam_y = *(float*)(cam_override + 0x68);
        cam_z = *(float*)(cam_override + 0x6c);
    }

    // Set camera position for water rendering
    // FUN_8012f534(cam_x, cam_y, cam_z);

    // Build rotation matrix from wave phase
    float angle = (CONST_TEXCOORD_SCALE * g_water_wave_phase) / CONST_TEXCOORD_DIVISOR;
    // FUN_8012f05c(angle, mtx_a);

    // Concatenate rotation with intermediate
    // FUN_80130bd4(mtx_a, mtx_b, mtx_c);

    // Concatenate with view matrix
    float* view_mtx = *(float**)(g_water_camera_override + 0x208);
    // FUN_80130bd4(mtx_c, view_mtx, mtx_result);

    // Load as GX texture matrix
    // FUN_80130cd8(mtx_result, &g_texmtx_slot);

    // Render all 3x3 tiles
    g_water_render_index = 1;
    g_water_render_col = 0;
    int row, col;
    for (row = 0; row < WATER_GRID_SIZE; row++) {
        for (col = 0; col < WATER_GRID_SIZE; col++) {
            water_surface_submit_quads();
        }
    }

    // Epilogue
    // FUN_8038272c();
}

// =============================================================================
// Address: 0x800f2cb0 | Name: water_surface_preload_resources
// Preloads resources needed for water surface rendering. Gets the water config
// entity type ID and calls entity_preload_resources(). If water is enabled and
// using UV-only mode (not animated), queues the water animation stream asset.
// =============================================================================
static void water_surface_preload_resources(void) {
    int entity_type_id = level_get_weapon_set_id();
    if (entity_type_id >= 0) {
        entity_preload_resources(entity_type_id);
    }

    if (g_water_enabled != 0 && g_water_animated_mode == 0) {
        stream_queue_animation();
    }
}

// =============================================================================
// Address: 0x800f2d04 | Name: water_surface_system_reset
// Resets all water surface system state. Initializes UV scroll speeds from
// constants, clears wave phase, reads water color from level config (with
// byte-swap for GX color format), and clears per-tile material tracking.
// Called during level transitions.
// =============================================================================
static void water_surface_system_reset(void) {
    g_water_scroll_speed_u = CONST_DEFAULT_SCROLL_U; // 0x80615390
    g_water_scroll_speed_v = CONST_DEFAULT_SCROLL_V; // 0x80615394
    g_water_wave_phase = 0.0f;
    g_water_enabled = 1;

    // Check if level has valid water config
    int water_set = level_get_weapon_set_ptr();
    if (water_set == 0) {
        g_water_enabled = 0;
    } else {
        int tex_a = *(int*)(level_get_weapon_set_ptr() + 0x10);
        int tex_b = *(int*)(level_get_weapon_set_ptr() + 0x14);
        if (tex_a == 0 && tex_b == 0) {
            g_water_enabled = 0;
        }
    }

    g_water_animated_mode = 0;
    g_water_stream_queued = 0;

    // Byte-swap water colors from level config for GX RGBA format
    int wset = level_get_weapon_set_ptr();
    if (wset != 0) {
        u32 color_a = *(u32*)(wset + 0x10);
        g_water_color_primary = BSWAP32(color_a);

        u32 color_b = *(u32*)(wset + 0x14);
        g_water_color_secondary = BSWAP32(color_b);
    }

    // Clear material tracking
    g_water_material_current = -1;
    g_water_material_prev = -1;
    for (int i = 0; i < 10; i++) {
        g_water_tile_materials[i] = -1;
    }
}

// =============================================================================
// Address: 0x800f2e40 | Name: water_surface_level_load
// Called during level load. Clears the camera entity override (so the player
// camera is used for water rendering), then delegates to water_surface_level_init.
// =============================================================================
static void water_surface_level_load(void) {
    if (g_water_camera_override != 0 && (g_level_flags & 0x20000000) == 0) {
        *(int*)(g_water_camera_override + 0xe04) = 0;
    }
    water_surface_level_init();
}

// =============================================================================
// Address: 0x800f2e88 | Name: water_surface_level_init
// Level initialization for the water surface. If water is enabled: resolves the
// animation asset handle (UV-only mode) or initializes vertex animation system.
// Builds the initial grid mesh, copies level bot set data, and initializes
// water flow direction state.
// =============================================================================
static void water_surface_level_init(void) {
    if (g_water_enabled != 0) {
        if (g_water_animated_mode == 0) {
            // UV-only mode: find animation asset for texture scrolling
            int anim = stream_find_anim_by_name();
            g_water_anim_handle = *(u32*)(anim + 0x14);
        } else {
            // Vertex-animated mode: full system init
            water_surface_system_init();
        }
        water_surface_build_grid();
    }

    // Clear pending state
    g_water_pending_type = 0;
    g_water_params_dirty = 0;

    // Copy level bot set data to local buffer
    u32 bot_set = level_get_bot_set_ptr();
    memcpy(&g_water_level_config, bot_set, 0x3c);

    // Initialize water flow direction based on level def
    int level_def = level_get_current_def();
    int has_flow = (*(int*)(level_def + 0x14) != 0) ? 1 : 0;
    water_surface_set_flow_direction(has_flow);
}

// =============================================================================
// Address: 0x800f2f14 | Name: water_surface_set_params
// Sets water surface rendering parameters from an external caller (likely a
// scripting/event system). Stores position XYZ, scale, and config values.
// Sets the dirty flag to trigger re-initialization on next frame.
// =============================================================================
static void water_surface_set_params(float pos_x, float pos_y, float pos_z,
                                     int scale, int config_a, int config_b) {
    g_water_param_scale = scale;       // 0x80611f10
    g_water_param_pos_y = pos_x;       // 0x80571964 (note: register ordering)
    g_water_param_pos_x = pos_y;       // 0x80571960
    g_water_param_pos_z = pos_z;       // 0x80571968
    g_water_param_config_a = config_a; // 0x80571974
    g_water_param_config_b = config_b; // 0x80571950
    g_water_params_dirty = 1;
}
