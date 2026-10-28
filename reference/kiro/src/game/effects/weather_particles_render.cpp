// Address: 0x800edb94 - 0x800edec4 | Weather Particle Rendering | Date: 2026-08-25
// Batch 295: Weather particle rendering, spatial grid, wind force, setup/teardown
//
// This module implements the rendering side of the precipitation system.
// It builds a spatial grid per scene room/portal for efficient particle
// culling, evaluates wind force harmonics, and renders particles as
// point sprites via the GX pipeline with portal-based visibility.
//
// Key globals:
//   g_weather_room_grid       (0x80611e88) - per-room grid data array (0x3c bytes each)
//   g_weather_particle_buffer (0x80611e84) - particle array (from weather_particles.cpp)
//   g_weather_wind_accel_x    (0x80570a5c) - computed wind X acceleration
//   g_weather_wind_accel_z    (0x80570a64) - computed wind Z acceleration
//   g_weather_wind_damping    (0x80570a60) - wind damping factor
//   g_scene_room_count        (0x80611b14) - total rooms in current level
//   g_camera_data             (0x80611d54) - active camera struct pointer
//   g_weather_lock_flag       (0x80611568) - prevents clearing during transitions
//   g_weather_render_scale    (0x80611ec0) - combined render scale from init
//
// Room grid struct layout (0x3c = 60 bytes per room):
//   +0x00..0x2F  various state
//   +0x30  int  grid_count_portals (at offset 0x34 = index 0xD)
//   +0x34  int  portal_count
//   +0x38  ptr  portal_grid_array (0x34 bytes per portal grid)
//
// Portal grid struct layout (0x34 = 52 bytes):
//   +0x18  int  cells_x
//   +0x1C  int  cells_y
//   +0x20  int  cells_z
//   +0x24  ptr  x_axis_data (float pairs: offset, extent)
//   +0x28  ptr  y_axis_data (float pairs: offset, extent)
//   +0x2C  ptr  z_axis_data (float pairs: offset, extent)
//   +0x30  ptr  cell_positions (float[3] per cell)

#include <math.h>

// Forward declarations
extern void* block_alloc_dispatch(int size, int pool, int align, int flags);
extern int   scene_node_get_portal_list_count(int room_id);
extern void  iterator_begin(void);                      // 0x803826f0
extern void  iterator_end(void);                        // 0x8038273c
extern float iterator_get_cell_size(void);              // 0x803826d4
extern void  iterator_begin_render(void);               // 0x803826b4
extern void  iterator_end_render(void);                 // 0x80382700
extern void  iterator_begin_submit(void);               // 0x803826e4
extern void  iterator_end_submit(void);                 // 0x80382730
extern int   particle_get_stride(void);                 // 0x801c1714
extern void  sprite_load_by_flag_mask(/*...*/);         // forwarded
extern int   sprite_get_texture_id(int id);
extern void* mem_advance_secondary(int size);

// Weather system functions from weather_particles.cpp
extern void  weather_build_camera_obb(int camera_data, float* out_obb);
extern void  weather_compute_camera_aabb(int camera_data, float* min_bounds, float* max_bounds);
extern float weather_frustum_test_point(float wx, float wy, float wz, float pd, float scale,
                                         int planes, float* cam_pos, float* point);

// GX render helpers
extern void  gx_set_blend_mode(int mode, int src, int dst);  // 0x803c7624
extern void  gx_set_zmode(int test, int func);                // 0x803c75e4
extern void  render_build_camera_matrix(float* out_mtx, float* cam_dir);  // 0x8012f680
extern void  render_set_matrix(float* mtx, float* params);                // 0x80130cd8
extern void  render_submit_particle_begin(/*...*/);   // 0x801740c8
extern void  render_submit_particle(/*...*/);         // 0x8016e1a8
extern void  render_submit_particle_end(void);        // 0x80172c54
extern void  render_finalize_batch(/*...*/);          // 0x80174144
extern void  render_set_particle_scale(/*...*/);      // 0x80172dc4
extern void  render_aabb_clip_setup(float* aabb);     // 0x8016e034
extern void  render_set_alpha_and_submit(int packed); // 0x8016e188
extern void  render_flush_vertex_cache(void*);        // 0x80174208

// Globals
static int    g_weather_room_grid;       // 0x80611e88 (pointer to room grid array)
static int    g_scene_room_count;        // 0x80611b14
static int    g_camera_data;             // 0x80611d54
static float* g_weather_particle_buffer; // 0x80611e84
static int    g_weather_lock_flag;       // 0x80611568
static float  g_weather_render_scale;    // 0x80611ec0
static float  g_weather_wind_accel_x;    // 0x80570a5c
static float  g_weather_wind_accel_z;    // 0x80570a64
static float  g_weather_wind_damping;    // 0x80570a60
static float  g_weather_wind_phase;      // 0x80570a08
static int    g_weather_render_active;   // 0x80612250
static int    g_weather_render_pass;     // 0x80612258

// Sine table
extern float g_sine_table[];             // 0x80583228

// Constants from ROM
static float WIND_SCALE_MULT;   // 0x8061523c
static float EXTENT_SIZE;       // 0x80615260

// ============================================================================
// Address: 0x800edb94
// Allocates per-room weather grid structures. Called during weather init
// to set up spatial subdivision for particle culling.
// ============================================================================
void weather_grid_alloc_rooms(void)
{
    iterator_begin();

    // Allocate room grid array: room_count * 0x3c bytes
    g_weather_room_grid = (int)block_alloc_dispatch(g_scene_room_count * 0x3C, 2, 0, 0);

    for (int room_id = 1; room_id < g_scene_room_count + 1; room_id++) {
        int room_idx = room_id - 1;
        int portal_list = scene_node_get_portal_list_count(room_id);

        // Count non-null portals
        int* room_data = (int*)(g_weather_room_grid + room_idx * 0x3C);
        room_data[0xD] = 0; // portal_count at offset 0x34
        while (*(int*)(portal_list + room_data[0xD] * 4) != 0) {
            room_data[0xD]++;
        }

        // Allocate portal grid array: portal_count * 0x34 bytes
        int* grid_array = (int*)block_alloc_dispatch(room_data[0xD] * 0x34, 2, 0, 0);
        room_data[0xE] = (int)grid_array; // portal_grid_array at offset 0x38

        // Clear the room's local state at offset 0x30
        *(int*)(g_weather_room_grid + room_idx * 0x3C + 0x30) = 0;
    }

    // Clear 6 state words at g_weather_state (0x80570a14)
    for (int i = 0; i < 6; i++) {
        *(int*)(0x80570a14 + i * 4) = 0;
    }

    iterator_end();
}

// ============================================================================
// Address: 0x800ed378
// Builds the spatial grid for all rooms. Subdivides each portal's bounding
// volume into cells based on a fixed cell size. Each cell stores its world
// position and per-axis offset/extent for particle density calculation.
// Called from weather_particles_init.
// ============================================================================
void weather_grid_build_cells(float x_extent, float y_extent, float z_extent)
{
    float cell_size = iterator_get_cell_size();

    for (int room_id = 1; room_id < g_scene_room_count + 1; room_id++) {
        int room_offset = g_weather_room_grid + (room_id - 1) * 0x3C;
        int portal_list = scene_node_get_portal_list_count(room_id);

        for (int p = 0; p < *(int*)(room_offset + 0x34); p++) {
            float* portal_bounds = *(float**)(portal_list + p * 4);
            int grid = *(int*)(room_offset + 0x38) + p * 0x34;

            // Clear grid
            *(int*)(grid + 0x30) = 0;
            *(int*)(grid + 0x24) = 0;
            *(int*)(grid + 0x28) = 0;
            *(int*)(grid + 0x2C) = 0;
            *(int*)(grid + 0x18) = 0;
            *(int*)(grid + 0x1C) = 0;
            *(int*)(grid + 0x20) = 0;

            // Compute cell counts per axis by dividing bounds extent by cell_size
            // X axis: portal_bounds[0]=min_x, portal_bounds[3]=max_x
            int cells_x_min = (int)(portal_bounds[0] / cell_size);
            if (portal_bounds[0] < 0.0f && (portal_bounds[0] / cell_size) != (float)cells_x_min)
                cells_x_min--;
            int cells_x_max = (int)(portal_bounds[3] / cell_size);
            if (portal_bounds[3] > 0.0f && (portal_bounds[3] / cell_size) != (float)cells_x_max)
                cells_x_max++;
            *(int*)(grid + 0x18) = cells_x_max - cells_x_min;

            // Y axis: portal_bounds[1]=min_y, portal_bounds[4]=max_y
            int cells_y_min = (int)(portal_bounds[1] / y_extent);
            if (portal_bounds[1] < 0.0f && (portal_bounds[1] / y_extent) != (float)cells_y_min)
                cells_y_min--;
            int cells_y_max = (int)(portal_bounds[4] / y_extent);
            if (portal_bounds[4] > 0.0f && (portal_bounds[4] / y_extent) != (float)cells_y_max)
                cells_y_max++;
            *(int*)(grid + 0x1C) = cells_y_max - cells_y_min;

            // Z axis: portal_bounds[2]=min_z, portal_bounds[5]=max_z
            int cells_z_min = (int)(portal_bounds[2] / z_extent);
            if (portal_bounds[2] < 0.0f && (portal_bounds[2] / z_extent) != (float)cells_z_min)
                cells_z_min--;
            int cells_z_max = (int)(portal_bounds[5] / z_extent);
            if (portal_bounds[5] > 0.0f && (portal_bounds[5] / z_extent) != (float)cells_z_max)
                cells_z_max++;
            *(int*)(grid + 0x20) = cells_z_max - cells_z_min;

            // Total cells
            int total_cells = *(int*)(grid + 0x18) * *(int*)(grid + 0x1C) * *(int*)(grid + 0x20);
            if (total_cells > 0) {
                // Allocate axis data arrays: (cells_x + cells_y + cells_z) * 8 bytes
                int axis_alloc_size = (*(int*)(grid + 0x18) + *(int*)(grid + 0x1C) + *(int*)(grid + 0x20)) * 8;
                axis_alloc_size = (axis_alloc_size + 0xF) & ~0xF; // 16-byte align
                int* axis_data = (int*)block_alloc_dispatch(axis_alloc_size, 2, 0, 0);
                *(int*)(grid + 0x24) = (int)axis_data;
                *(int*)(grid + 0x28) = *(int*)(grid + 0x24) + *(int*)(grid + 0x18) * 8;
                *(int*)(grid + 0x2C) = *(int*)(grid + 0x28) + *(int*)(grid + 0x1C) * 8;

                // Allocate cell position array: total_cells * 12 bytes (float[3])
                int cell_alloc_size = (total_cells * 0xC + 0xF) & ~0xF;
                int* cell_data = (int*)block_alloc_dispatch(cell_alloc_size, 2, 0, 0);
                *(int*)(grid + 0x30) = (int)cell_data;
            }

            // Fill cell positions and axis offset/extent data
            for (int iz = 0; iz < *(int*)(grid + 0x20); iz++) {
                float z_base = portal_bounds[2] + (float)iz * z_extent;
                int z_cell_idx = (int)(z_base / z_extent);
                if (z_base < 0.0f && (z_base / z_extent) != (float)z_cell_idx)
                    z_cell_idx--;
                float z_world = (float)z_cell_idx * z_extent;

                // Z axis offset (clamped to portal min)
                float z_offset = 0.0f;
                if (z_world < portal_bounds[2])
                    z_offset = portal_bounds[2] - z_world;
                // Z axis extent (clamped to portal max)
                float z_cell_extent = z_extent;
                if (portal_bounds[5] < z_world + z_extent)
                    z_cell_extent = portal_bounds[5] - z_world;

                for (int ix = 0; ix < *(int*)(grid + 0x18); ix++) {
                    float x_base = portal_bounds[0] + (float)ix * cell_size;
                    int x_cell_idx = (int)(x_base / cell_size);
                    if (x_base < 0.0f && (x_base / cell_size) != (float)x_cell_idx)
                        x_cell_idx--;
                    float x_world = (float)x_cell_idx * cell_size;

                    float x_offset = 0.0f;
                    if (x_world < portal_bounds[0])
                        x_offset = portal_bounds[0] - x_world;
                    float x_cell_extent = cell_size;
                    if (portal_bounds[3] < x_world + cell_size)
                        x_cell_extent = portal_bounds[3] - x_world;

                    for (int iy = 0; iy < *(int*)(grid + 0x1C); iy++) {
                        // Compute cell index in 3D array (x + y*cells_x + z*cells_x*cells_y)
                        int cell_idx = ix + iy * *(int*)(grid + 0x18)
                                     + *(int*)(grid + 0x1C) * iz * *(int*)(grid + 0x18);
                        float* cell = (float*)(*(int*)(grid + 0x30) + cell_idx * 0xC);

                        float y_base = portal_bounds[1] + (float)iy * y_extent;
                        int y_cell_idx = (int)(y_base / y_extent);
                        if (y_base < 0.0f && (y_base / y_extent) != (float)y_cell_idx)
                            y_cell_idx--;
                        float y_world = (float)y_cell_idx * y_extent;

                        float y_offset = 0.0f;
                        if (y_world < portal_bounds[1])
                            y_offset = portal_bounds[1] - y_world;
                        float y_cell_extent = y_extent;
                        if (portal_bounds[4] < y_world + y_extent)
                            y_cell_extent = portal_bounds[4] - y_world;

                        // Store cell world position
                        cell[0] = x_world;
                        cell[1] = y_world;
                        cell[2] = z_world;

                        // Store per-axis offset and extent into axis arrays
                        *(float*)(*(int*)(grid + 0x24) + ix * 8)     = x_offset;
                        *(float*)(*(int*)(grid + 0x28) + iy * 8)     = y_offset;
                        *(float*)(*(int*)(grid + 0x2C) + iz * 8)     = z_offset;
                        *(float*)(*(int*)(grid + 0x24) + ix * 8 + 4) = x_cell_extent;
                        *(float*)(*(int*)(grid + 0x28) + iy * 8 + 4) = y_cell_extent;
                        *(float*)(*(int*)(grid + 0x2C) + iz * 8 + 4) = z_cell_extent;
                    }
                }
            }
        }
    }

    // End grid builder iterator
    // FUN_80382720
}

// ============================================================================
// Address: 0x800ed988
// Evaluates wind force from multiple sine harmonics. Computes the global
// wind acceleration values (g_weather_wind_accel_x, g_weather_wind_accel_z)
// that drive particle horizontal drift per frame.
// ============================================================================
void weather_wind_evaluate_force(float amplitude_x, float amplitude_y, float amplitude_z)
{
    float phase = g_weather_wind_phase * 1.0f; // FLOAT_806151f0 * wind_phase
    g_weather_wind_damping = 0.0f;

    // Z-axis harmonic (cosine via +512 offset in sine table)
    float cos_harm_y = amplitude_y * g_sine_table[
        ((int)(0.5f * 1.0f * (1.0f + phase) * WIND_SCALE_MULT + 0.5f) + 0x200) & 0x7FF
    ]; // WIND_HARMONIC_4 variant

    // X-axis sine harmonic clamped
    float sin_harm_z = 0.0f;
    float raw_z = amplitude_z * g_sine_table[
        (int)(0.5f * 1.0f * (1.0f + phase) * WIND_SCALE_MULT + 0.5f) & 0x7FF
    ];
    if (0.0f < raw_z) sin_harm_z = raw_z;

    // X-axis second harmonic clamped
    float sin_harm_x2 = 0.0f;
    float raw_x2 = amplitude_y * g_sine_table[
        (int)(0.5f * 1.0f * (1.0f + phase) * WIND_SCALE_MULT + 0.5f) & 0x7FF
    ];
    if (0.0f < raw_x2) sin_harm_x2 = raw_x2;

    // Compute X acceleration: base harmonic + second harmonic - damping + bias
    g_weather_wind_accel_x = 1.0f * (
        (amplitude_x * g_sine_table[(int)(0.5f * 1.0f * phase * WIND_SCALE_MULT + 0.5f) & 0x7FF]
         + sin_harm_x2) - sin_harm_z
    ) + 0.0f; // FLOAT_80615258 bias

    // Z-axis harmonics
    float cos_harm_z2 = 0.0f;
    float raw_cz = amplitude_z * g_sine_table[
        (int)(0.5f * 1.0f * (1.0f + phase) * WIND_SCALE_MULT + 0.5f) & 0x7FF
    ];
    if (0.0f < raw_cz) cos_harm_z2 = raw_cz;

    float sin_harm_cos = 0.0f;
    if (0.0f < cos_harm_y) sin_harm_cos = cos_harm_y;

    // Compute Z acceleration
    g_weather_wind_accel_z = 1.0f * (
        (amplitude_x * g_sine_table[
            ((int)(0.5f * 1.0f * phase * WIND_SCALE_MULT + 0.5f) + 0x200) & 0x7FF
        ] + sin_harm_cos) - cos_harm_z2
    ) + 1.0f; // FLOAT_806151f0 bias
}

// ============================================================================
// Address: 0x800edc94
// Conditionally clears the weather room grid pointer. Only clears if
// the lock flag is not set (prevents clearing during level transitions).
// ============================================================================
void weather_grid_conditional_clear(void)
{
    if (g_weather_lock_flag != 0)
        return;
    g_weather_room_grid = 0;
}

// ============================================================================
// Address: 0x800edcac
// Preloads weather particle sprite textures via the sprite system.
// Thin wrapper around sprite_load_by_flag_mask.
// ============================================================================
void weather_preload_sprites(/* 8 params forwarded */)
{
    sprite_load_by_flag_mask(/* all params forwarded */);
}

// ============================================================================
// Address: 0x800edcd0
// Weather setup entry point. Computes weather parameters then applies them.
// Calls weather_compute_params (0x800ef890) then weather_apply (0x800ee734).
// ============================================================================
void weather_setup_and_apply(/* 16 params */)
{
    // result = weather_compute_params(all params)
    // weather_apply(result, ...)
}

// ============================================================================
// Address: 0x800edcf4
// Weather shutdown wrapper. Calls weather_grid_conditional_clear.
// ============================================================================
void weather_shutdown(void)
{
    weather_grid_conditional_clear();
}

// ============================================================================
// Address: 0x800edd14
// Submits weather particles to the GX rendering pipeline as point sprites.
// Iterates the 512-particle buffer, tests each against world bounds, and
// submits visible particles with per-particle size scaling. Uses the
// particle iterator pattern for thread safety.
// ============================================================================
void weather_submit_particles_to_gx(/* params including texture, camera, scale */)
{
    float particle_scale = 1.0f * *(float*)(g_camera_data + 0x268); // FLOAT_8061525c * cam zoom

    // Begin particle rendering setup
    // render_submit_particle_begin(FLOAT_8061525c, ..., camera_data, ...)
    // gx_set_blend_mode(0, 1, 1) - additive blending
    // gx_set_zmode(0x18, 5) - depth test less-equal, no write

    float y_scale = 1.0f * *(float*)(g_camera_data + 0x268); // FLOAT_8061524c * cam zoom

    // Compute frame-rate correction for particle density
    float frame_ratio = (float)g_frame_counter_raw / (float)g_target_fps;
    // render_set_particle_scale(frame_ratio / target_ratio, y_scale, ...)

    float extent = EXTENT_SIZE;

    // Iterate all 512 particles
    for (int i = 0; i < 0x200; i += particle_get_stride()) {
        float* p = (float*)((char*)g_weather_particle_buffer + i * 0x1C);

        // Bounds check: particle within [0, extent) on all axes
        if (p[0] >= 0.0f && p[0] < extent &&
            p[1] >= 0.0f && p[1] < extent &&
            p[2] >= 0.0f && p[2] < extent)
        {
            // render_submit_particle(pos_x, pos_y, pos_z, particle_scale, 0.0f)
        }
    }

    // Finalize particle batch
    // render_submit_particle_end() -> render_finalize_batch()
}

// ============================================================================
// Address: 0x800ecf98
// Portal-based precipitation render pass. For each visible room/portal,
// iterates the spatial grid cells, tests each against the camera frustum,
// and spawns render quads for cells that pass the visibility test.
// This creates the volumetric precipitation density effect.
// ============================================================================
void weather_render_portal_precipitation(float world_extent_x, float world_extent_y,
                                          float world_extent_z, float render_scale,
                                          float max_frame_rate)
{
    // Clamp frame rate
    float frame_rate = max_frame_rate;
    static float MAX_RATE; // 0x80615228
    if (MAX_RATE < max_frame_rate)
        frame_rate = MAX_RATE;
    float density_scale = 1.0f / frame_rate; // FLOAT_806151fc / frame_rate

    // Get render context (packed alpha + room info)
    // unsigned int render_ctx = iterator_begin_render();
    int visible_room_count = *(short*)(g_camera_data + 0xE0A);

    // Build camera OBB and AABB for frustum culling
    float obb[16];
    weather_build_camera_obb(g_camera_data, obb);
    float aabb_min[3], aabb_max[3];
    weather_compute_camera_aabb(g_camera_data, aabb_min, aabb_max);

    // Iterate visible rooms
    for (int r = visible_room_count - 1; r >= 0; r--) {
        int room_idx = (int)*(short*)(g_camera_data + r * 2 + 0xE0C);
        int room_data = g_weather_room_grid + (room_idx - 1) * 0x3C;
        int portal_list = scene_node_get_portal_list_count(room_idx);
        int portal_count = *(int*)(room_data + 0x34);

        // Iterate portals in this room
        for (int p = portal_count - 1; p >= 0; p--) {
            float* portal_bounds = *(float**)(portal_list + p * 4);
            int grid = *(int*)(room_data + 0x38) + p * 0x34;

            // Quick AABB rejection against camera AABB
            if (aabb_min[0] > portal_bounds[3] || portal_bounds[0] > aabb_max[0]) continue;
            if (aabb_min[1] > portal_bounds[4] || portal_bounds[1] > aabb_max[1]) continue;
            if (aabb_min[2] > portal_bounds[5] || portal_bounds[2] > aabb_max[2]) continue;

            // Iterate grid cells (z * y * x)
            int cells_z = *(int*)(grid + 0x20);
            int cells_y = *(int*)(grid + 0x18);
            int cells_x = *(int*)(grid + 0x1C);

            for (int iz = cells_z - 1; iz >= 0; iz--) {
                for (int iy = cells_y - 1; iy >= 0; iy--) {
                    for (int ix = cells_x - 1; ix >= 0; ix--) {
                        int cell_idx = iy + ix * cells_y + cells_x * iz * cells_y;
                        float* cell_pos = (float*)(*(int*)(grid + 0x30) + cell_idx * 0xC);

                        // Quick AABB test for this cell
                        if (aabb_min[0] > cell_pos[0] + world_extent_x) continue;
                        if (cell_pos[0] > aabb_max[0]) continue;
                        if (aabb_min[2] > cell_pos[2] + world_extent_z) continue;
                        if (cell_pos[2] > aabb_max[2]) continue;

                        // Full frustum test
                        float depth = weather_frustum_test_point(
                            world_extent_x, world_extent_y, world_extent_z,
                            max_frame_rate, density_scale,
                            (int)obb, (float*)(g_camera_data + 0x27C), cell_pos
                        );

                        if (depth >= 0.0f) {
                            if (depth > 1.0f) depth = 1.0f;

                            // Compute cell local AABB for clipping
                            float cell_aabb[6];
                            cell_aabb[0] = portal_bounds[0] - cell_pos[0];
                            cell_aabb[1] = portal_bounds[1] - cell_pos[1];
                            cell_aabb[2] = portal_bounds[2] - cell_pos[2];
                            cell_aabb[3] = portal_bounds[3] - cell_pos[0];
                            cell_aabb[4] = portal_bounds[4] - cell_pos[1];
                            cell_aabb[5] = portal_bounds[5] - cell_pos[2];
                            // render_aabb_clip_setup(cell_aabb);

                            // Build camera-facing matrix for this cell
                            float* mtx = (float*)mem_advance_secondary(0x40);
                            // render_build_camera_matrix(cam_pos - cell_pos, cam_dir, cam_right, mtx)
                            // render_set_matrix(mtx, rain_params)

                            // Submit with alpha based on depth
                            g_weather_render_active = 1;
                            g_weather_render_pass = 0;
                            int alpha = (int)(density_scale * depth * 255.0f);
                            // render_set_alpha_and_submit(packed_context | (alpha << 24))
                            // render_flush_vertex_cache(...)
                        }
                    }
                }
            }
        }
    }

    // Restore default matrix and finalize
    // render_set_matrix(camera_default_mtx, rain_params)
    g_weather_render_active = 1;
    g_weather_render_pass = 0;
    // iterator_end_render()
}

// ============================================================================
// Address: 0x800edec4
// Top-level weather render function. Sets up GX render state (blending,
// depth, texture), submits point-sprite particles, then runs the portal-
// based volumetric precipitation pass. Restores all render state on exit.
// ============================================================================
void weather_render_main(/* params including camera, transforms */)
{
    // Save render state
    // iterator_begin_submit()
    int prev_blend = 0; // FUN_80174078(1) - enable alpha blend
    int prev_depth = 0; // FUN_8016f00c(2) - set depth mode
    // FUN_8016ee98() - flush
    int prev_particle_mode = 0; // FUN_8016e178(1)
    int prev_fog = 0;           // FUN_80172f18(1)

    // Bind weather texture (sprite ID 0x354 = rain/snow texture)
    int texture_id = sprite_get_texture_id(0x354);

    // Submit point-sprite particles
    weather_submit_particles_to_gx(/* texture_id, camera, etc. */);

    // Unbind texture
    // FUN_8016e7e4(texture_id)

    // Run portal precipitation pass
    float extent = EXTENT_SIZE;
    weather_render_portal_precipitation(extent, extent, extent, g_weather_render_scale, extent);

    // Restore render state
    // FUN_80172f18(prev_fog)
    // FUN_8016e178(prev_particle_mode)
    // FUN_80174078(prev_blend)
    // FUN_8016f00c(prev_depth)
    // FUN_8016ee98() - flush

    // Run secondary rain effect (streaks/splashes)
    // FUN_8016f00c(1)
    // FUN_800ef59c() - secondary weather effects
    // iterator_end_submit()
}
