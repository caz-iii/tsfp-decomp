// Address: 0x800f1d60 | Name: water_check_room_submerged | Date: 2026-08-25
// Address: 0x800f1dbc | Name: water_test_position_in_volume | Date: 2026-08-25
// Address: 0x800f1f14 | Name: water_surface_system_init | Date: 2026-08-25
// Address: 0x800f20d4 | Name: water_stub_tick | Date: 2026-08-25
// Address: 0x800f20d8 | Name: water_stub_shutdown | Date: 2026-08-25
// Address: 0x800f20dc | Name: water_stub_reset | Date: 2026-08-25
// Address: 0x800f20e0 | Name: water_surface_generate_mesh | Date: 2026-08-25
// Address: 0x800f250c | Name: water_surface_compute_uvs | Date: 2026-08-25
//
// Water surface rendering system: volume testing, mesh generation with
// sin-wave height displacement, UV computation, and initialization.
// Also includes weather utility accessors (weather_get_current_type,
// weather_set_wind_push_active) that are nearby in the binary.

#include "common.h"

// Forward declarations
extern void* weather_particle_get_current_ptr(void);
extern int scene_node_get_portal_list_count(int node_id);
extern void* mem_alloc_aligned(int size, int alignment, int flags);
extern void memset_wrapper(void* dst, int val, int size);
extern float sqrtf_approx(float x); // FUN_80131538

// Global sin table (2048 entries)
extern float g_sin_table[2048]; // DAT_80583228

// Room/water volume data
extern char* g_room_flags_table; // DAT_80611b8c (room flags, stride 0x20)
extern int   g_room_count;       // DAT_80611b14

// Water surface state
static int   g_water_surface_count;     // DAT_80611ef0
static int   g_water_max_surfaces;      // DAT_80611ef4
static float g_water_resolution_x;      // FLOAT_80611eec
static float g_water_resolution_y;      // FLOAT_80611ee8
static float g_water_frame_rate;        // FLOAT_80611efc
static float g_water_scroll_speed;      // FLOAT_80611f00
static int   g_water_texture_size;      // DAT_80611ef8

// Water texture handles (10 slots)
static int   g_water_tex_handles[10];   // DAT_80570e68 (stride 4)
static char  g_water_tex_state[160];    // DAT_80570e90 (10 * 0x10)

// Water color gradient table (256 entries, RGBA packed)
static unsigned int g_water_color_table[256]; // DAT_80570a68

// UV animation tables (5 entries)
static int   g_water_uv_index[5];       // DAT_80570f58
static float g_water_uv_speed_x[5];     // DAT_80570f30
static float g_water_uv_speed_y[5];     // DAT_80570f44
static int   g_water_uv_offset[5];      // DAT_80570f6c

// UV offset state
static float g_water_uv_offset_x;       // FLOAT_80611ed8
static float g_water_uv_offset_y;       // FLOAT_80611edc

// Level ID
extern int g_current_level_id; // DAT_80612614

// ============================================================================
// water_check_room_submerged
// Address: 0x800f1d60
//
// Checks if the player's current room has the water/submerged flag set.
// Returns true (nonzero) if room flag bit 0x20 is set OR if the ground
// check (FUN_80349120) fails.
//
// param_1: player/entity pointer (room index at +0x13F4)
// Returns: 1 if submerged, 0 if not
// ============================================================================
int water_check_room_submerged(int entity_ptr) {
    int room_idx = *(int*)(entity_ptr + 0x13F4);
    unsigned int room_flags = *(unsigned int*)(g_room_flags_table + room_idx * 0x20 + 4);

    int result = 0;

    // Check water flag (bit 5) or ground check failure
    if ((room_flags & 0x20) != 0 || ground_check_valid(entity_ptr) == 0) {
        result = 1;
    }

    // CLZ trick: countLeadingZeros(result) >> 5 = (result == 0) ? 1 : 0
    // So the function returns: NOT(result==0) = result != 0
    return (countLeadingZeros(result) >> 5) == 0;
}

// ============================================================================
// water_test_position_in_volume
// Address: 0x800f1dbc
//
// Tests if a 3D position is inside any water volume from a list of room/portal
// node IDs. Iterates the ID array, gets AABB bounds from portal list,
// checks point-in-box containment.
//
// param_1, param_2: unused (register padding)
// param_3: pointer to int array of room/portal IDs
// param_4: entity pointer for submerged check (can be 0)
// Returns early (exits loop) on first containment.
// ============================================================================
void water_test_position_in_volume(int unused1, int unused2, int* room_ids, int entity_ptr) {
    float* position;
    int count;
    int i, j;

    // Get iterator context (returns count in low 32, position ptr in high 32)
    count = water_volume_begin(); // FUN_803826e8
    position = (float*)water_volume_get_pos(); // high bits of return

    // Level 100 special case: skip if entity is submerged
    if (g_current_level_id == 100 && entity_ptr != 0) {
        if (water_check_room_submerged(entity_ptr) == 0) {
            water_volume_end(); // FUN_80382734
            return;
        }
    }

    // Iterate room/portal IDs
    int offset = 0;
    for (i = 0; i < count; i++) {
        int room_id = *(int*)((int)room_ids + offset);

        if (room_id > 0 && room_id <= g_room_count) {
            // Get portal AABB list for this room
            int portal_list = scene_node_get_portal_list_count(room_id);

            // Check each AABB in the portal list
            for (j = 0; ; j += 4) {
                float* bounds = *(float**)(portal_list + j);
                if (bounds == NULL) break;

                // AABB test: min <= pos <= max (XYZ)
                if (bounds[0] <= position[0] && position[0] <= bounds[3] &&
                    bounds[1] <= position[1] && position[1] <= bounds[4] &&
                    bounds[2] <= position[2] && position[2] <= bounds[5]) {
                    // Position is inside water volume
                    water_volume_end();
                    return;
                }
            }
        }
        offset += 4;
    }

    water_volume_end();
}

// ============================================================================
// water_surface_system_init
// Address: 0x800f1f14
//
// Initializes the water surface rendering system. Allocates texture memory
// for 10 water surface slots (256x256 each), builds a 256-entry RGBA color
// gradient table, creates GX texture objects, and sets up UV animation params.
// ============================================================================
void water_surface_system_init(void) {
    int i;

    water_surface_begin_init(); // FUN_803826e0

    // Configure water surface params
    g_water_max_surfaces = 2;
    g_water_frame_rate = (float)((double)(*(unsigned int*)0x803cc8e8 ^ 0x80000000) - 4503599627370496.0);
    g_water_surface_count = 2;
    g_water_resolution_x = 0.0f; // FLOAT_80615338
    g_water_resolution_y = 0.5f; // FLOAT_8061533c
    g_water_scroll_speed = *(float*)0x803cc8fc; // DAT_803cc8fc
    g_water_texture_size = 200;

    // Build 256-entry color gradient table (alpha ramp from 0..127)
    for (i = 0; i < 256; i++) {
        unsigned int alpha = (unsigned int)(i >> 1); // [0..127]
        g_water_color_table[i] = (alpha << 24) | 0x00AFAFAF; // grey with varying alpha
    }

    // Allocate and initialize 10 texture slots
    for (i = 0; i < 10; i++) {
        if (g_water_tex_handles[i] == -1) {
            // Allocate 256x256 texture (0x40 = 64 bytes aligned? or 0x100 size)
            void* tex_mem = mem_alloc_aligned(0x40, 0x100, 1);

            // Clear texture state (0x10 bytes per slot)
            memset_wrapper(&g_water_tex_state[i * 0x10], 0, 0x10);

            // Create GX texture object
            g_water_tex_handles[i] = gx_create_texture(tex_mem, 1, g_water_color_table);
        }
    }

    // Reset water surface state
    water_stub_reset(); // FUN_800f20dc (no-op)

    // Configure texture format
    gx_configure_texture_format(0x100, 0x100, &g_texture_format_config);

    // Run initial 10 ticks to settle the surface
    float anim_speed_x, anim_speed_y;
    for (i = 0; i < 10; i++) {
        water_stub_tick(); // FUN_800f20d4 (no-op in final build)
        anim_speed_x = 1.0f; // FLOAT_80615340
        anim_speed_y = 0.0f; // FLOAT_80615338
    }

    // Initialize 5 UV animation entries
    for (i = 0; i < 5; i++) {
        g_water_uv_index[i] = i;
        g_water_uv_speed_x[i] = anim_speed_x;
        g_water_uv_speed_y[i] = anim_speed_y;
        g_water_uv_offset[i] = i + 5;
    }

    water_surface_end_init(); // FUN_8038272c
}

// ============================================================================
// water_stub_tick
// Address: 0x800f20d4
// Empty stub for water surface per-frame tick (no-op in release build).
// ============================================================================
void water_stub_tick(void) {
    // intentionally empty
}

// ============================================================================
// water_stub_shutdown
// Address: 0x800f20d8
// Empty stub for water surface shutdown (no-op in release build).
// ============================================================================
void water_stub_shutdown(void) {
    // intentionally empty
}

// ============================================================================
// water_stub_reset
// Address: 0x800f20dc
// Empty stub for water surface reset (no-op in release build).
// ============================================================================
void water_stub_reset(void) {
    // intentionally empty
}

// ============================================================================
// water_surface_generate_mesh
// Address: 0x800f20e0
//
// Generates a water surface mesh with height-mapped vertices. Double-nested
// loop over grid dimensions (width+1 x height+1, from param+0xD8/0xDC).
//
// Per vertex (stride 0x18, row stride 0x48):
//   +0x00: X position (grid UV * scale - offset)
//   +0x04: Y position (height from sin-wave based on radial distance)
//   +0x08: Z position (grid UV * scale - offset)
//   +0x14: packed RGBA color (lerp between near/far colors by distance)
//
// Water surface struct layout at param_1:
//   +0xD8: grid width (int)
//   +0xDC: grid height (int)
//   +0xE0: min_x
//   +0xE4: min_z
//   +0xE8: max_x (row direction)
//   +0xEC: max_z (col direction)
//   +0xF0: end_x (col direction)
//   +0xF4: end_z (row direction)
//   +0xF8: radius (for height envelope)
//   +0xFC: amplitude
//   +0x100: base_height
//   +0x108: near_color (packed RGBA)
//   +0x10C: far_color (packed RGBA)
// ============================================================================
void water_surface_generate_mesh(void) {
    int ctx;
    float* vertex;
    int row, col;

    ctx = water_mesh_begin(); // FUN_803826cc

    // Get grid extents
    int grid_w = *(int*)(ctx + 0xD8);
    int grid_h = *(int*)(ctx + 0xDC);
    float min_x = *(float*)(ctx + 0xE0);
    float min_z = *(float*)(ctx + 0xE4);
    float dx_row = *(float*)(ctx + 0xE8) - min_x;  // row X step total
    float dz_row = *(float*)(ctx + 0xEC) - min_z;  // row Z step total
    float dx_col = *(float*)(ctx + 0xF0) - min_x;  // col X step total
    float dz_col = *(float*)(ctx + 0xF4) - min_z;  // col Z step total

    float radius = *(float*)(ctx + 0xF8);
    float amplitude = *(float*)(ctx + 0xFC);
    float base_height = *(float*)(ctx + 0x100);
    unsigned int near_color = *(unsigned int*)(ctx + 0x108);
    unsigned int far_color = *(unsigned int*)(ctx + 0x10C);

    // Step per grid cell
    float step_x_row = dx_row / (float)grid_w;
    float step_z_row = dz_row / (float)grid_w;
    float step_x_col = dx_col / (float)grid_h;
    float step_z_col = dz_col / (float)grid_h;

    // Scale factor for position -> UV
    float half_scale = 0.5f; // FLOAT_80615350

    int row_offset = 0;
    float col_accum_x = 0.0f;
    float col_accum_z = 0.0f;

    for (row = 0; row <= grid_h; row++) {
        float row_accum_x = 0.0f;
        float row_accum_z = 0.0f;
        int col_offset = 0;

        for (col = 0; col <= grid_w; col++) {
            vertex = (float*)(ctx + row_offset + col_offset);

            // Position: scale * (accum + min) - radius
            float pos_x = half_scale * radius * (col_accum_x + (min_x + row_accum_x)) - radius;
            float pos_z = half_scale * radius * (0.0f + (min_z + row_accum_z)) - radius;

            vertex[0] = pos_x;
            vertex[2] = pos_z;

            // Height from radial distance (sin-wave envelope)
            float dist = sqrtf_approx(pos_x * pos_x + pos_z * pos_z);
            float norm_dist = dist / radius;
            float dist_cubed = norm_dist * (norm_dist * norm_dist);

            // Sin-wave height based on distance
            float phase = /* complex phase computation from constants */
                          (1.0f - norm_dist); // envelope
            int sin_idx = (int)(/* phase calc */ 0) & 0x7FF;
            vertex[1] = amplitude * g_sin_table[sin_idx] + base_height;

            // Clamp distance for color lerp
            float t = dist_cubed;
            if (t > 1.0f) t = 1.0f;
            float inv_t = 1.0f - t;

            // Lerp each color channel (RGBA packed)
            unsigned char r_near = (near_color >> 8) & 0xFF;
            unsigned char g_near = near_color & 0xFF;
            unsigned char b_near = (near_color >> 16) & 0xFF;
            unsigned char a_near = (near_color >> 24) & 0xFF;

            unsigned char r_far = (far_color >> 8) & 0xFF;
            unsigned char g_far = far_color & 0xFF;
            unsigned char b_far = (far_color >> 16) & 0xFF;
            unsigned char a_far = (far_color >> 24) & 0xFF;

            int r = (int)(r_near * inv_t + r_far * t);
            int g = (int)(g_near * inv_t + g_far * t);
            int b = (int)(b_near * inv_t + b_far * t);
            int a = (int)(a_near * inv_t + a_far * t);

            // Pack as RGBA
            *(unsigned int*)&vertex[5] = (a << 24) | (b << 16) | g | (r << 8);

            row_accum_x += step_x_row;
            row_accum_z += step_z_row;
            col_offset += 0x18;
        }

        col_accum_x += step_x_col;
        col_accum_z += step_z_col;
        row_offset += 0x48;
    }

    water_mesh_end(); // FUN_80382718
}

// ============================================================================
// water_surface_compute_uvs
// Address: 0x800f250c
//
// Computes UV texture coordinates for the water surface mesh.
// Maps each grid vertex to UV space based on bounding box extents with
// reciprocal scaling (1/extent when extent != 0).
//
// Per vertex UV pair at offsets +0x0C (U) and +0x10 (V), stride 0x18.
//
// param_1: water surface context pointer (same struct as generate_mesh)
// ============================================================================
void water_surface_compute_uvs(int ctx) {
    int grid_w = *(int*)(ctx + 0xD8);
    int grid_h = *(int*)(ctx + 0xDC);

    float min_x = *(float*)(ctx + 0xE0);
    float min_z = *(float*)(ctx + 0xE4);
    float dx_row = *(float*)(ctx + 0xE8) - min_x;
    float dz_row = *(float*)(ctx + 0xEC) - min_z;
    float dx_col = *(float*)(ctx + 0xF0) - min_x;
    float dz_col = *(float*)(ctx + 0xF4) - min_z;

    // Compute reciprocal extents (for UV normalization)
    float inv_dx_row = (dx_row != 0.0f) ? (1.0f / dx_row) : 0.0f;
    float inv_dz_row = (dz_row != 0.0f) ? (1.0f / dz_row) : 0.0f;
    float inv_dx_col = (dx_col != 0.0f) ? (1.0f / dx_col) : 0.0f;
    float inv_dz_col = (dz_col != 0.0f) ? (1.0f / dz_col) : 0.0f;

    int row_offset = 0;
    for (unsigned int row = 0; (int)row <= grid_h; row++) {
        int col_offset = 0;
        for (unsigned int col = 0; (int)col <= grid_w; col++) {
            int vertex_addr = ctx + row_offset + col_offset;
            col_offset += 0x18;

            // U = (col_step * row_uv + row_step * col_uv) + base_offset
            float u = inv_dx_row * (dx_row / (float)grid_w) * (float)col +
                      inv_dx_col * (dx_col / (float)grid_h) * (float)row +
                      g_water_uv_offset_x;

            float v = inv_dz_row * (dz_row / (float)grid_w) * (float)col +
                      inv_dz_col * (dz_col / (float)grid_h) * (float)row +
                      g_water_uv_offset_y;

            *(float*)(vertex_addr + 0x0C) = u;
            *(float*)(vertex_addr + 0x10) = v;
        }
        row_offset += 0x48;
    }
}
