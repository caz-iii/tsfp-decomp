// Address: 0x800ec2f4 - 0x800ecdf8 | Weather Particle System | Date: 2026-08-25
// Batch 294: Rain/snow precipitation particle system
//
// This module implements a volumetric precipitation effect (rain/snow).
// Particles are spawned within a world-space volume around the camera,
// updated each frame with velocity + wind + gravity, and wrapped to stay
// within the precipitation bounds. A multi-harmonic sine-based wind
// oscillation drives horizontal drift.
//
// Key globals:
//   g_weather_particle_buffer (0x80611e84) - array of WeatherParticle (28 bytes each)
//   g_weather_wind_phase      (0x80570a08) - current wind wave phase
//   g_weather_wind_scale_x    (0x805709fc) - per-axis wind scale X
//   g_weather_wind_scale_y    (0x80570a00) - per-axis wind scale Y
//   g_weather_wind_scale_z    (0x80570a04) - per-axis wind scale Z
//   g_weather_particle_base   (0x805709ec) - particle buffer base pointer
//   g_weather_particle_stride (0x805709f0) - particle index multiplier
//
// Particle struct layout (0x1c = 28 bytes):
//   +0x00  float pos_x
//   +0x04  float pos_y
//   +0x08  float pos_z
//   +0x0C  float vel_x
//   +0x10  float vel_y
//   +0x14  float vel_z
//   +0x18  float size_scale

#include <math.h>

// Forward declarations
extern void* block_alloc_dispatch(int size, int pool, int align, int flags);
extern float rand_float(void);                    // 0x801310c0
extern void  matrix_build_axis_rotation(double angle, float* out_mtx, float* axis);  // 0x8013025c
extern void  matrix_transform_vector(float* mtx, float* vec);                        // 0x8012fbf4
extern double sinf_approx(double x);             // 0x8038fecc
extern int  iterator_begin(void);                 // 0x803826f0
extern void iterator_end(void);                   // 0x8038273c
extern int  particle_get_stride(void);            // 0x801c1714
extern void weather_init_wave_table(double x_extent, double y_extent, double z_extent); // 0x800ed378

// Sine lookup table (2048 entries)
extern float g_sine_table[];                      // 0x80583228

// Globals
static float* g_weather_particle_buffer;          // 0x80611e84
static float  g_weather_wind_phase;               // 0x80570a08
static float  g_weather_wind_scale_x;             // 0x805709fc
static float  g_weather_wind_scale_y;             // 0x80570a00
static float  g_weather_wind_scale_z;             // 0x80570a04
static int    g_weather_particle_base;            // 0x805709ec
static int    g_weather_particle_stride;          // 0x805709f0

// Wind wave parameters (read from ROM data at 0x806151xx)
static float WIND_FREQ_BASE;        // 0x806151e4
static float WIND_AMPLITUDE;        // 0x806151e0
static float WIND_BIAS;             // 0x806151d4
static float WIND_SCALE;            // 0x806151d8
static float WIND_OUTPUT_SCALE;     // 0x806151d0
static float WIND_CLAMP_ZERO;       // 0x806151e8
static float WIND_HARMONIC_0;       // 0x806151ec
static float WIND_HARMONIC_1;       // 0x806151dc
static float WIND_HARMONIC_2;       // 0x806151f0
static float WIND_HARMONIC_3;       // 0x806151f4
static float WIND_HARMONIC_4;       // 0x806151f8

// Timing / frame rate
extern float g_frame_dt_scaled;     // 0x806108ec
extern float g_frame_rate_target;   // 0x806151fc
extern int   g_frame_counter_raw;   // 0x806109c0

// ============================================================================
// Address: 0x800ec2f4
// Returns pointer to particle at current index in the buffer.
// ============================================================================
int weather_particle_get_current_ptr(void)
{
    return g_weather_particle_base + g_weather_particle_stride * 0x28;
}

// ============================================================================
// Address: 0x800ec310
// Normalizes a 3D position by per-axis wind scale (returns sum of ratios).
// Used to compute a phase offset based on world position.
// ============================================================================
float weather_wind_position_to_phase(float* pos)
{
    return (pos[0] / g_weather_wind_scale_x)
         + (pos[1] / g_weather_wind_scale_y)
         + (pos[2] / g_weather_wind_scale_z);
}

// ============================================================================
// Address: 0x800ec348
// Evaluates a multi-harmonic wind wave at a given phase.
// Uses 5 sine harmonics with clamped amplitude modulation.
// Output is a 3D wind vector written to out_wind[0..2].
// ============================================================================
void weather_wind_evaluate_wave(float phase, float* out_wind)
{
    float base_phase = WIND_AMPLITUDE * (WIND_FREQ_BASE * phase);

    // Compute 5 harmonic indices into sine table
    unsigned int idx0 = (unsigned int)(WIND_SCALE * WIND_HARMONIC_1 * base_phase + WIND_BIAS);
    float idx1_f = WIND_SCALE * WIND_HARMONIC_0 * base_phase + WIND_BIAS;
    float idx2_f = WIND_SCALE * WIND_HARMONIC_2 * base_phase + WIND_BIAS;
    float idx3_f = WIND_SCALE * WIND_HARMONIC_3 * base_phase + WIND_BIAS;
    float idx4_f = WIND_SCALE * WIND_HARMONIC_4 * base_phase + WIND_BIAS;

    // X component: sine of idx0, Y component: constant, Z component: cosine of idx0
    out_wind[0] = WIND_OUTPUT_SCALE * g_sine_table[idx0 & 0x7FF];
    out_wind[1] = WIND_CLAMP_ZERO;
    out_wind[2] = WIND_OUTPUT_SCALE * g_sine_table[(idx0 + 0x200) & 0x7FF]; // cos = sin + 512

    // Amplitude modulation from remaining harmonics (clamped to >= WIND_CLAMP_ZERO)
    float amp0 = g_sine_table[(int)idx1_f & 0x7FF];
    float amp2 = g_sine_table[((int)idx2_f + 0x200) & 0x7FF]; // cosine
    float amp3 = g_sine_table[(int)idx3_f & 0x7FF];
    float amp4 = g_sine_table[((int)idx4_f + 0x200) & 0x7FF]; // cosine

    // Modulate X
    float mod_x = WIND_CLAMP_ZERO;
    if (WIND_CLAMP_ZERO < amp0) mod_x = amp0;
    out_wind[0] *= mod_x;

    float mod_x2 = WIND_CLAMP_ZERO;
    if (WIND_CLAMP_ZERO < amp3) mod_x2 = amp3;
    out_wind[0] *= mod_x2;

    // Modulate Z
    float mod_z = WIND_CLAMP_ZERO;
    if (WIND_CLAMP_ZERO < amp2) mod_z = amp2;
    out_wind[2] *= mod_z;

    float mod_z2 = WIND_CLAMP_ZERO;
    if (WIND_CLAMP_ZERO < amp4) mod_z2 = amp4;
    out_wind[2] *= mod_z2;
}

// ============================================================================
// Address: 0x800ec4c0
// Computes wind vector for a given position. Adds global phase offset
// to the position-derived phase, then evaluates the wind wave.
// ============================================================================
void weather_wind_compute_at_position(float* pos, int unused, float* out_wind)
{
    float phase = weather_wind_position_to_phase(pos);
    weather_wind_evaluate_wave(g_weather_wind_phase + phase, out_wind);
}

// ============================================================================
// Address: 0x800ec504
// Advances the global wind phase each frame. Updates and clamps the
// per-axis wind scale factors.
// ============================================================================
void weather_wind_advance_phase(void)
{
    static float WIND_PHASE_SPEED;      // 0x806151fc
    static float WIND_SCALE_DAMPEN;     // 0x80615200
    static float WIND_SCALE_MIN;        // 0x80615204
    static float WIND_SCALE_MAX_NEG;    // 0x80615208

    // Advance phase by frame-rate-normalized increment
    float dt_normalized = g_frame_dt_scaled * (WIND_PHASE_SPEED / (float)((double)(g_frame_counter_raw ^ 0x80000000) - 4503599627370496.0));
    g_weather_wind_phase += dt_normalized;

    // Evaluate wind at current global phase -> updates wind scale
    weather_wind_evaluate_wave(g_weather_wind_phase, &g_weather_wind_scale_x);

    // Apply dampening
    g_weather_wind_scale_x *= WIND_SCALE_DAMPEN;
    g_weather_wind_scale_z *= WIND_SCALE_DAMPEN;

    // Clamp X axis
    if (g_weather_wind_scale_x < WIND_CLAMP_ZERO) {
        // Negative range
        if (WIND_SCALE_MAX_NEG < g_weather_wind_scale_x)
            g_weather_wind_scale_x = WIND_SCALE_MAX_NEG;
    } else {
        // Positive range
        if (g_weather_wind_scale_x < WIND_SCALE_MIN)
            g_weather_wind_scale_x = WIND_SCALE_MIN;
    }

    // Clamp Y axis minimum
    if (g_weather_wind_scale_y < WIND_SCALE_MIN)
        g_weather_wind_scale_y = WIND_SCALE_MIN;

    // Clamp Z axis (same logic as X)
    if (g_weather_wind_scale_x < WIND_CLAMP_ZERO) {
        if (WIND_SCALE_MAX_NEG < g_weather_wind_scale_z)
            g_weather_wind_scale_z = WIND_SCALE_MAX_NEG;
    } else {
        if (g_weather_wind_scale_z < WIND_SCALE_MIN)
            g_weather_wind_scale_z = WIND_SCALE_MIN;
    }
}

// ============================================================================
// Address: 0x800ec650
// Builds a camera-oriented bounding box (OBB) for the precipitation volume.
// Takes camera data (param_1) at offsets for right/up/forward vectors and
// position, rotates them by FOV-derived angle, and outputs 15 floats:
//   [0..2]  = -rotated_up
//   [3..5]  = -rotated_position
//   [6..8]  = +rotated_up (original)
//   [9..11] = +rotated_position (original)
//   [12..14]= camera world position
// ============================================================================
void weather_build_camera_obb(int camera_data, float* out_obb)
{
    float up[3], neg_up[3];
    float pos[3], neg_pos[3];
    float rot_matrix[16];

    // Read camera up vector (+0x370)
    up[0] = *(float*)(camera_data + 0x370);
    up[1] = *(float*)(camera_data + 0x374);
    up[2] = *(float*)(camera_data + 0x378);
    neg_up[0] = -up[0];
    neg_up[1] = -up[1];
    neg_up[2] = -up[2];

    // Compute rotation angle from FOV: -(PI * fov) / (2 * ref_fov)
    float fov = *(float*)(camera_data + 900); // +0x384
    static float HALF_PI;       // 0x806151d4
    static float PI;            // 0x806151e4
    static float REF_FOV;       // 0x80615218
    double angle = (double)(HALF_PI * ((PI * fov) / REF_FOV));

    // Read camera right vector (+0x364)
    pos[0] = *(float*)(camera_data + 0x364);
    pos[1] = *(float*)(camera_data + 0x368);
    pos[2] = *(float*)(camera_data + 0x36c);
    neg_pos[0] = -pos[0];
    neg_pos[1] = -pos[1];
    neg_pos[2] = -pos[2];

    // Rotate up vector by -angle around right vector
    matrix_build_axis_rotation(-angle, rot_matrix, (float*)(camera_data + 0x364));
    matrix_transform_vector(rot_matrix, up);

    // Rotate up vector by +angle around right vector
    matrix_build_axis_rotation(angle, rot_matrix, (float*)(camera_data + 0x364));
    matrix_transform_vector(rot_matrix, neg_up);

    // Rotate right vector by -angle around up vector
    matrix_build_axis_rotation(-angle, rot_matrix, (float*)(camera_data + 0x370));
    matrix_transform_vector(rot_matrix, neg_pos);

    // Rotate right vector by +angle around up vector
    matrix_build_axis_rotation(angle, rot_matrix, (float*)(camera_data + 0x370));
    matrix_transform_vector(rot_matrix, pos);

    // Write output OBB
    out_obb[0]  = neg_up[0];
    out_obb[1]  = neg_up[1];
    out_obb[2]  = neg_up[2];
    out_obb[3]  = neg_pos[0];
    out_obb[4]  = neg_pos[1];
    out_obb[5]  = neg_pos[2];
    out_obb[6]  = up[0];
    out_obb[7]  = up[1];
    out_obb[8]  = up[2];
    out_obb[9]  = pos[0];
    out_obb[10] = pos[1];
    out_obb[11] = pos[2];
    // Camera world position
    out_obb[12] = *(float*)(camera_data + 0x334);
    out_obb[13] = *(float*)(camera_data + 0x338);
    out_obb[14] = *(float*)(camera_data + 0x33c);
}

// ============================================================================
// Address: 0x800ec80c
// Computes a world-space AABB for the precipitation volume by expanding
// the camera position by the scaled right/up vectors in all 8 combinations.
// Writes min_bounds to an internal buffer and max_bounds to param_3.
// ============================================================================
void weather_compute_camera_aabb(int unused1, int unused2, float* max_bounds)
{
    // Get camera data via iterator
    int cam_data;
    float* min_bounds;
    {
        // iterator_begin returns [min_ptr, cam_ptr]
        long long result = iterator_begin();
        cam_data = (int)(result >> 32);
        min_bounds = (float*)(int)result;
    }

    float interp_factor; // extraout_f1 from iterator
    // Camera world position interpolated
    float cam_x = *(float*)(cam_data + 0x334) * interp_factor + *(float*)(cam_data + 0x27c);
    float cam_y = *(float*)(cam_data + 0x338) * interp_factor + *(float*)(cam_data + 0x280);
    float cam_z = *(float*)(cam_data + 0x33c) * interp_factor + *(float*)(cam_data + 0x284);

    // Compute extent from FOV
    float fov = *(float*)(cam_data + 900);
    static float HALF_PI_2;     // 0x806151d4
    static float PI_2;          // 0x806151e4
    static float REF_FOV_2;     // 0x80615218
    float sin_val = (float)(interp_factor * (float)sinf_approx((double)(HALF_PI_2 * ((PI_2 * fov) / REF_FOV_2))));

    // Camera right vector scaled
    float right_x = *(float*)(cam_data + 0x370) * sin_val;
    float right_y = *(float*)(cam_data + 0x374) * sin_val;
    float right_z = *(float*)(cam_data + 0x378) * sin_val;

    // Camera up vector scaled
    float up_x = *(float*)(cam_data + 0x364) * sin_val;
    float up_y = *(float*)(cam_data + 0x368) * sin_val;
    float up_z = *(float*)(cam_data + 0x36c) * sin_val;

    // Expand AABB by all 8 corner combinations of (cam +/- right +/- up)
    // Corner 1: cam - right - up
    float cx = cam_x - right_x - up_x;
    float cy = cam_y - right_y - up_y;
    float cz = cam_z - right_z - up_z;

    // Initialize min/max
    min_bounds[0] = (cx < *(float*)(cam_data + 0x27c)) ? cx : *(float*)(cam_data + 0x27c);
    min_bounds[1] = (cy < *(float*)(cam_data + 0x280)) ? cy : *(float*)(cam_data + 0x280);
    min_bounds[2] = (cz < *(float*)(cam_data + 0x284)) ? cz : *(float*)(cam_data + 0x284);
    max_bounds[0] = (cx > *(float*)(cam_data + 0x27c)) ? cx : *(float*)(cam_data + 0x27c);
    max_bounds[1] = (cy > *(float*)(cam_data + 0x280)) ? cy : *(float*)(cam_data + 0x280);
    max_bounds[2] = (cz > *(float*)(cam_data + 0x284)) ? cz : *(float*)(cam_data + 0x284);

    // Corner 2: cam + right - up
    cx = cam_x + right_x - up_x;
    cy = cam_y + right_y - up_y;
    cz = cam_z + right_z - up_z;
    if (cx < min_bounds[0]) min_bounds[0] = cx;
    if (cy < min_bounds[1]) min_bounds[1] = cy;
    if (cz < min_bounds[2]) min_bounds[2] = cz;
    if (cx > max_bounds[0]) max_bounds[0] = cx;
    if (cy > max_bounds[1]) max_bounds[1] = cy;
    if (cz > max_bounds[2]) max_bounds[2] = cz;

    // Corner 3: cam - right + up
    cx = cam_x - right_x + up_x;
    cy = cam_y - right_y + up_y;
    cz = cam_z - right_z + up_z;
    if (cx < min_bounds[0]) min_bounds[0] = cx;
    if (cy < min_bounds[1]) min_bounds[1] = cy;
    if (cz < min_bounds[2]) min_bounds[2] = cz;
    if (cx > max_bounds[0]) max_bounds[0] = cx;
    if (cy > max_bounds[1]) max_bounds[1] = cy;
    if (cz > max_bounds[2]) max_bounds[2] = cz;

    // Corner 4: cam + right + up
    cx = cam_x + right_x + up_x;
    cy = cam_y + right_y + up_y;
    cz = cam_z + right_z + up_z;
    if (cx < min_bounds[0]) min_bounds[0] = cx;
    if (cy < min_bounds[1]) min_bounds[1] = cy;
    if (cz < min_bounds[2]) min_bounds[2] = cz;
    if (cx > max_bounds[0]) max_bounds[0] = cx;
    if (cy > max_bounds[1]) max_bounds[1] = cy;
    if (cz > max_bounds[2]) max_bounds[2] = cz;

    iterator_end();
}

// ============================================================================
// Address: 0x800ecaf0
// Tests a point (param_8 - param_7) against up to 5 frustum planes.
// Returns a penetration depth if inside, or a sentinel (FLOAT_8061521c)
// if outside (i.e., behind any plane). Accounts for wrap-around offsets
// (param_1..3) when plane normal components are negative.
// ============================================================================
float weather_frustum_test_point(float wrap_x, float wrap_y, float wrap_z,
                                 float plane_d, float scale,
                                 int planes_ptr, float* point_a, float* point_b)
{
    float zero = 0.0f;

    for (int i = 4; i >= 0; i--) {
        float* plane = (float*)(planes_ptr + i * 0xC);
        float dx = point_b[0] - point_a[0];
        float dy = point_b[1] - point_a[1];
        float dz = point_b[2] - point_a[2];

        // Add wrap offsets where plane normal is negative
        if (plane[0] < zero) dx += wrap_x;
        if (plane[1] < zero) dy += wrap_y;
        if (plane[2] < zero) dz += wrap_z;

        // Dot product with plane normal
        float dot = dx * plane[0] + dy * plane[1] + dz * plane[2];

        if (i < 0) {
            // Final plane: compute penetration depth
            // (This branch handles the last plane at offset +0x30)
            float* last_plane = (float*)(planes_ptr + 0x30);
            dx = point_b[0] - point_a[0];
            dy = point_b[1] - point_a[1];
            dz = point_b[2] - point_a[2];
            if (last_plane[0] < zero) dx += wrap_x;
            if (last_plane[1] < zero) dy += wrap_y;
            if (last_plane[2] < zero) dz += wrap_z;
            float final_dot = last_plane[0] * dx + last_plane[1] * dy + last_plane[2] * dz;
            return scale * (plane_d - final_dot);
        }

        if (dot > zero) {
            // Point is outside this plane
            return -1.0f; // FLOAT_8061521c sentinel
        }
    }

    return -1.0f;
}

// ============================================================================
// Address: 0x800ecbf4
// Updates all weather particles each frame. Applies velocity (with wind
// and gravity from globals), then wraps positions to stay within world bounds.
// Uses iterator_begin/end pattern for thread safety.
// ============================================================================
void weather_particles_update_tick(float world_x_extent, float world_y_extent, float world_z_extent)
{
    int particle_count = iterator_begin();
    float zero = 0.0f;
    static float GRAVITY_SCALE; // 0x80615220

    // Frame delta
    float dt = g_frame_dt_scaled * (g_frame_rate_target / (float)((double)(g_frame_counter_raw ^ 0x80000000) - 4503599627370496.0));

    // Global wind acceleration from globals
    static float g_weather_wind_accel_x; // 0x80570a5c
    static float g_weather_wind_accel_z; // 0x80570a64

    for (int i = 0; i < particle_count; i += particle_get_stride()) {
        float* p = (float*)((char*)g_weather_particle_buffer + i * 0x1C);

        // Integrate velocity with wind + gravity
        p[0] += dt * (GRAVITY_SCALE * (g_weather_wind_accel_x * p[6]) + p[3]); // pos_x += dt * (wind_x + vel_x)
        p[1] += dt * p[4];                                                       // pos_y += dt * vel_y (gravity)
        p[2] += dt * (GRAVITY_SCALE * (g_weather_wind_accel_z * p[6]) + p[5]); // pos_z += dt * (wind_z + vel_z)

        // Wrap positions into [0, extent) range
        while (p[0] < zero)           p[0] += world_x_extent;
        while (p[1] < zero)           p[1] += world_y_extent;
        while (p[2] < zero)           p[2] += world_z_extent;
        while (p[0] >= world_x_extent) p[0] -= world_x_extent;
        while (p[1] >= world_y_extent) p[1] -= world_y_extent;
        while (p[2] >= world_z_extent) p[2] -= world_z_extent;
    }

    iterator_end();
}

// ============================================================================
// Address: 0x800ecdf8
// Initializes the weather particle system. Allocates particle buffer,
// randomizes initial positions and velocities within given extents.
// param_8 = particle count (typically 0x200 = 512)
// ============================================================================
void weather_particles_init(float x_extent, float y_extent, float z_extent,
                            float vel_x_range, float vel_y_range,
                            float vel_z_range, float size_range,
                            int particle_count)
{
    // Initialize wave table / global state
    weather_init_wave_table(x_extent, y_extent, z_extent);

    // Allocate particle buffer: particle_count * 28 bytes
    g_weather_particle_buffer = (float*)block_alloc_dispatch(particle_count * 0x1C, 2, 0, 0);

    static float HALF;  // 0x80615224 (0.5)
    static float ONE;   // 0x806151d4 (1.0)

    while (--particle_count >= 0) {
        float* p = &g_weather_particle_buffer[particle_count * 7];

        // Random position within extents
        p[0] = x_extent * rand_float();
        p[1] = y_extent * rand_float();
        p[2] = z_extent * rand_float();

        // Random velocity
        p[3] = vel_x_range * rand_float();
        p[4] = vel_y_range * (HALF - rand_float()); // centered around 0
        p[5] = vel_z_range * rand_float();

        // Random size: range * (1.0 * rand + 1.0) = range * [1..2]
        p[6] = size_range * (ONE * rand_float() + ONE);
    }
}
