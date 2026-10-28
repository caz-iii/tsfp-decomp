// Address: 0x800e6d88 - 0x800e6f90 | Particle Emitter Setup | Date: 2026-08-25
// Handles emitter creation, cloning, and spawn position/rotation computation.

#include "particle.h"
#include "math_util.h"

// ============================================================================
// Address: 0x800e6d88 | Name: particle_spawn_offset_along_dir
// Computes a spawn position offset along a random direction.
// Generates a random unit direction, scales by a factor derived from params,
// and adds to the base position.
// Called from particle_init_spawn_params.
// ============================================================================
void particle_spawn_offset_along_dir(double scale_x, double scale_y, double scale_z,
                                     float* base_pos, float* out_pos) {
    // Copy base position
    out_pos[0] = base_pos[0];
    out_pos[1] = base_pos[1];
    out_pos[2] = base_pos[2];

    // Generate random unit direction
    double random_angle = math_random_angle();

    // Apply scaled offset: magnitude = 2PI * random, direction from (scale_x * scale_y + scale_z)
    float offset_magnitude = (float)((double)MATH_TWO_PI * random_angle);
    float direction = (float)((double)(float)((double)MATH_HALF * scale_x) * scale_y + scale_z);

    math_offset_along_direction((double)offset_magnitude, (double)direction, out_pos);
}

// ============================================================================
// Address: 0x800e6e24 | Name: particle_spawn_build_rotation
// Builds a rotation matrix from euler angles (yaw, pitch, roll) for particle spawn.
// Constructs individual axis rotation matrices and multiplies them together,
// then extracts the final orientation into the output float array.
// Called from particle_spawn_position_generate.
// ============================================================================
void particle_spawn_build_rotation(double yaw, double pitch, double roll, float* out_matrix) {
    float mat_roll[16];
    float mat_pitch[16];
    float mat_combined[17]; // extra for alignment

    // Build individual axis rotation matrices
    mat_build_rotation_z(roll, mat_combined);
    mat_build_rotation_x(pitch, mat_pitch);
    mat_multiply_4x4(mat_pitch, mat_combined, mat_combined);
    mat_build_rotation_y(yaw, mat_pitch);
    mat_multiply_4x4(mat_pitch, mat_combined, mat_combined);

    // Extract rotation to output
    mat_extract_rotation(mat_combined, out_matrix);
}

// ============================================================================
// Address: 0x800e6eb4 | Name: particle_emitter_create
// Creates and initializes a new particle emitter instance.
// Allocates 0x84 bytes from the GX scratch allocator, zeros it, and sets
// initial parameters (position, scale, particle definition pointer).
// Called from many systems: weapons, entities, cutscenes, level loading, etc.
// Has 37+ callers - core particle system creation entry point.
// ============================================================================
void particle_emitter_create(uint64_t packed_params, double pos_y, double pos_z,
                             uint32_t particle_def_id, uint32_t flags,
                             uint32_t param_6, uint32_t param_7) {
    uint64_t alloc_result = gx_scratch_alloc_emitter();
    uint32_t* emitter = (uint32_t*)(alloc_result >> 32);
    float* def_ptr = (float*)(uint32_t)alloc_result;
    float extra_y = (float)alloc_result; // extracted from return pair

    // Clear emitter struct
    memset_wrapper((int)emitter, 0, 0x84);

    // Store particle definition pointer
    emitter[0x13] = (uint32_t)def_ptr; // particle def at offset 0x4C

    // Store creation flags
    emitter[0] = param_6;
    emitter[1] = param_7;

    // Store initial position (Y and Z from params, X from extra)
    ((float*)emitter)[8] = extra_y;   // position X (offset 0x20)
    ((float*)emitter)[9] = (float)pos_y;  // position Y (offset 0x24)
    ((float*)emitter)[0xc] = (float)pos_z; // position Z (offset 0x30)

    // Initialize default scale/alpha
    emitter[0x15] = 0xffffffff;   // color = white (offset 0x54)
    ((float*)emitter)[0x16] = 1.0f;  // scale X (offset 0x58)
    ((float*)emitter)[0x17] = 1.0f;  // scale Y (offset 0x5C)
    ((float*)emitter)[0x18] = 1.0f;  // scale Z (offset 0x60)

    gx_scratch_finalize_emitter();
}

// ============================================================================
// Address: 0x800e6f90 | Name: particle_emitter_copy
// Copies/clones an existing emitter's state into a new slot.
// Performs a raw 0x84-byte memory copy then overwrites the particle def pointer.
// Called from player_ai_particle_defs_init and FUN_80358c48.
// ============================================================================
void particle_emitter_copy(uint32_t dest_emitter, uint32_t particle_def, uint32_t src_emitter) {
    g_current_emitter = dest_emitter;

    // Copy full emitter state (0x84 bytes)
    memcpy_fast(dest_emitter, src_emitter, 0x84);

    // Override particle definition pointer
    *(uint32_t*)(g_current_emitter + 0x4c) = particle_def;
}

// ============================================================================
// Address: 0x800e6fd4 | Name: particle_emitter_set_position
// Sets emitter world position (X, Y, Z) on the current emitter being configured.
// Offsets: 0x20 = pos_x, 0x24 = pos_y, 0x30 = pos_z
// Called immediately after particle_emitter_create by all emitter spawners.
// ============================================================================
void particle_emitter_set_position(float pos_x, float pos_y, float pos_z) {
    *(float*)(g_current_emitter + 0x20) = pos_x;
    *(float*)(g_current_emitter + 0x24) = pos_y;
    *(float*)(g_current_emitter + 0x30) = pos_z;
}

// ============================================================================
// Address: 0x800e6ff0 | Name: particle_emitter_set_scale
// Sets emitter scale vector (sx, sy, sz) from a float[3] array.
// Offsets: 0x58 = scale_x, 0x5c = scale_y, 0x60 = scale_z
// ============================================================================
void particle_emitter_set_scale(float* scale_vec) {
    *(float*)(g_current_emitter + 0x58) = scale_vec[0];
    *(float*)(g_current_emitter + 0x5c) = scale_vec[1];
    *(float*)(g_current_emitter + 0x60) = scale_vec[2];
}

// ============================================================================
// Address: 0x800e7018 | Name: particle_emitter_set_velocity_range
// Sets min/max velocity for particle emission.
// Offsets: 0x28 = velocity_min, 0x2c = velocity_max
// ============================================================================
void particle_emitter_set_velocity_range(float vel_min, float vel_max) {
    *(float*)(g_current_emitter + 0x28) = vel_min;
    *(float*)(g_current_emitter + 0x2c) = vel_max;
}

// ============================================================================
// Address: 0x800e702c | Name: particle_emitter_set_lifetime_range
// Sets min/max lifetime for spawned particles.
// Offsets: 0x34 = lifetime_min, 0x38 = lifetime_max
// ============================================================================
void particle_emitter_set_lifetime_range(float lifetime_min, float lifetime_max) {
    *(float*)(g_current_emitter + 0x34) = lifetime_min;
    *(float*)(g_current_emitter + 0x38) = lifetime_max;
}

// ============================================================================
// Address: 0x800e7040 | Name: particle_emitter_set_color_range
// Sets RGBA color range (start and end) for particle color interpolation.
// Offsets: 0x3c = color_r_start, 0x40 = color_g_start, 0x44 = color_b_start, 0x48 = color_a_start
// (Alternatively: 0x3c-0x44 = start color RGBA, 0x48 = end alpha or second color component)
// ============================================================================
void particle_emitter_set_color_range(float color_a, float color_b, float color_c, float color_d) {
    *(float*)(g_current_emitter + 0x3c) = color_a;
    *(float*)(g_current_emitter + 0x40) = color_b;
    *(float*)(g_current_emitter + 0x44) = color_c;
    *(float*)(g_current_emitter + 0x48) = color_d;
}

// ============================================================================
// Address: 0x800e7064 | Name: particle_emitter_set_texture
// Sets the texture/sprite ID for this emitter's particles.
// Offset: 0x50 = texture_id
// ============================================================================
void particle_emitter_set_texture(int texture_id) {
    *(int*)(g_current_emitter + 0x50) = texture_id;
}

// ============================================================================
// Address: 0x800e7070 | Name: particle_emitter_set_emit_rate
// Sets particle emission rate (particles per second or per frame).
// Null-guarded: returns immediately if no emitter is active.
// Offset: 0x70 = emit_rate
// ============================================================================
void particle_emitter_set_emit_rate(float emit_rate) {
    if (g_current_emitter == 0) return;
    *(float*)(g_current_emitter + 0x70) = emit_rate;
}

// ============================================================================
// Address: 0x800e7084 | Name: particle_emitter_set_size_range
// Sets min/max particle size for this emitter.
// Null-guarded: returns immediately if no emitter is active.
// Offsets: 0x74 = size_min, 0x78 = size_max
// ============================================================================
void particle_emitter_set_size_range(float size_min, float size_max) {
    if (g_current_emitter == 0) return;
    *(float*)(g_current_emitter + 0x74) = size_min;
    *(float*)(g_current_emitter + 0x78) = size_max;
}

// ============================================================================
// Address: 0x800e70a0 | Name: particle_emitter_set_rotation_range
// Sets min/max rotation angle for spawned particles.
// Null-guarded: returns immediately if no emitter is active.
// Offsets: 0x7c = rotation_min, 0x80 = rotation_max
// ============================================================================
void particle_emitter_set_rotation_range(float rotation_min, float rotation_max) {
    if (g_current_emitter == 0) return;
    *(float*)(g_current_emitter + 0x7c) = rotation_min;
    *(float*)(g_current_emitter + 0x80) = rotation_max;
}

// ============================================================================
// Address: 0x800e70bc | Name: particle_emitter_set_flags
// Sets emitter behavior flags.
// Null-guarded: returns immediately if no emitter is active.
// Offset: 0x54 = flags
// ============================================================================
void particle_emitter_set_flags(uint32_t flags) {
    if (g_current_emitter == 0) return;
    *(uint32_t*)(g_current_emitter + 0x54) = flags;
}

// ============================================================================
// Particle Definition API
// These functions configure g_current_particle_def (DAT_80611e24), a 0xCC-byte
// struct that defines particle behavior (type, keyframes, velocity, color, etc.)
// within an emitter. Called after particle_def_create during emitter setup.
// ============================================================================

// ============================================================================
// Address: 0x800e70d0 | Name: particle_def_create
// Allocates and initializes a new particle definition (0xCC bytes).
// Sets render type, initial params. For types 5/6 (rotated billboard, triangle fan)
// sets a default scale of 1.0 at offset 0x28.
// Called alongside particle_emitter_create by all particle spawners.
// ============================================================================
void particle_def_create(uint64_t packed_params, float lifetime, uint32_t render_type,
                         uint32_t param_flags_a, uint32_t param_flags_b,
                         uint32_t color_start, uint32_t color_end, uint32_t blend_mode) {
    uint64_t alloc_result = gx_particle_begin_overlay(); // GX scratch alloc
    g_current_particle_def = (uint32_t*)(alloc_result >> 32);
    int type = (int)(uint32_t)alloc_result;
    float extra_f = (float)alloc_result; // extra return from alloc pair

    memset_wrapper((int)g_current_particle_def, 0, 0xCC);

    g_current_particle_def[3] = type;           // offset 0x0C: render type
    g_current_particle_def[5] = render_type;    // offset 0x14: render sub-type
    g_current_particle_def[0] = color_start;    // offset 0x00: color start packed
    ((float*)g_current_particle_def)[1] = extra_f; // offset 0x04
    g_current_particle_def[4] = blend_mode;     // offset 0x10: blend mode
    g_current_particle_def[6] = blend_mode;     // offset 0x18
    ((float*)g_current_particle_def)[7] = lifetime; // offset 0x1C: base lifetime

    // Types 5 (rotated billboard) and 6 (triangle fan) get default scale = 1.0
    if (type == 5 || type == 6) {
        ((float*)g_current_particle_def)[0x28] = 1.0f; // offset 0xA0: default scale
    }

    gx_particle_end_overlay();
}

// ============================================================================
// Address: 0x800e719c | Name: particle_def_copy
// Copies/clones a particle definition (0xCC-byte memcpy) into a new slot.
// Sets g_current_particle_def to dest before copying.
// ============================================================================
void particle_def_copy(uint32_t dest_def, uint32_t src_def) {
    g_current_particle_def = dest_def;
    memcpy_fast(dest_def, src_def, 0xCC);
}

// ============================================================================
// Address: 0x800e71c4 | Name: particle_def_set_size_keyframes
// Builds an interleaved time/value keyframe table for particle size interpolation.
// Keyframes stored at offset 0x38+ as pairs (time, value). Count at offset 0x34.
// Also sets offset 0x20, 0x2c, 0x30 with additional params (texture, flags).
// Supports 2-4 keyframes depending on input values (auto-inserts start/end entries).
// ============================================================================
void particle_def_set_size_keyframes(float value_at_peak, float time_fade_in,
                                     float time_fade_out, float time_tail,
                                     uint32_t texture_id, uint32_t flags_a,
                                     uint32_t flags_b) {
    int count = 0;

    // If fade-in time > 0 or fade-out time > 0, insert initial zero keyframe
    if ((0.0f < time_fade_in) || (0.0f < time_fade_out)) {
        *(float*)(g_current_particle_def + 0x38) = time_fade_in;
        *(float*)(g_current_particle_def + 0x3c) = 0.0f;
        count = 1;
    }

    // Main keyframe at peak value
    int idx = count + 1;
    *(float*)(g_current_particle_def + count * 8 + 0x38) = time_fade_out;
    *(float*)(g_current_particle_def + count * 8 + 0x3c) = value_at_peak;

    // Optional tail keyframe (if tail time < 1.0)
    if (time_tail < 1.0f) {
        *(float*)(g_current_particle_def + idx * 8 + 0x38) = time_tail;
        *(float*)(g_current_particle_def + idx * 8 + 0x3c) = value_at_peak;
        idx = count + 2;
    }

    // Terminal keyframe: time=1.0, value=0.0
    *(float*)(g_current_particle_def + idx * 8 + 0x38) = 1.0f;
    *(float*)(g_current_particle_def + idx * 8 + 0x3c) = 0.0f;
    *(int*)(g_current_particle_def + 0x34) = idx + 1; // keyframe count

    // Additional params
    *(uint32_t*)(g_current_particle_def + 0x20) = texture_id;
    *(uint32_t*)(g_current_particle_def + 0x2c) = flags_a;
    *(uint32_t*)(g_current_particle_def + 0x30) = flags_b;
}

// ============================================================================
// Address: 0x800e728c | Name: particle_def_set_keyframes_raw
// Copies parallel time[] and value[] arrays into interleaved keyframe format.
// time_array[i] -> offset (0x38 + i*8), value_array[i] -> offset (0x3C + i*8).
// Sets keyframe count at offset 0x34.
// ============================================================================
void particle_def_set_keyframes_raw(int* time_array, int* value_array, int count) {
    int byte_offset = 0;
    int arr_offset = 0;
    int remaining = count;

    if (0 < count) {
        do {
            *(uint32_t*)(g_current_particle_def + byte_offset + 0x38) =
                *(uint32_t*)((char*)time_array + arr_offset);
            *(uint32_t*)(g_current_particle_def + byte_offset + 0x3c) =
                *(uint32_t*)((char*)value_array + arr_offset);
            byte_offset += 8;
            arr_offset += 4;
            remaining--;
        } while (remaining != 0);
    }
    *(int*)(g_current_particle_def + 0x34) = count;
}

// ============================================================================
// Address: 0x800e72d8 | Name: particle_def_set_gravity
// Sets gravity type and strength on the particle definition.
// Offset 0x24 = gravity_type (int), offset 0x28 = gravity_strength (float).
// ============================================================================
void particle_def_set_gravity(float gravity_strength, uint32_t gravity_type) {
    *(uint32_t*)(g_current_particle_def + 0x24) = gravity_type;
    *(float*)(g_current_particle_def + 0x28) = gravity_strength;
}

// ============================================================================
// Address: 0x800e72ec | Name: particle_def_set_velocity_ranges
// Sets min/max velocity for each axis (6 floats).
// Offsets: 0x60=vel_x_min, 0x64=vel_x_max, 0x68=vel_y_min,
//          0x6C=vel_y_max, 0x70=vel_z_min, 0x74=vel_z_max
// ============================================================================
void particle_def_set_velocity_ranges(float vel_x_min, float vel_x_max,
                                      float vel_y_min, float vel_y_max,
                                      float vel_z_min, float vel_z_max) {
    *(float*)(g_current_particle_def + 0x60) = vel_x_min;
    *(float*)(g_current_particle_def + 0x64) = vel_x_max;
    *(float*)(g_current_particle_def + 0x68) = vel_y_min;
    *(float*)(g_current_particle_def + 0x6c) = vel_y_max;
    *(float*)(g_current_particle_def + 0x70) = vel_z_min;
    *(float*)(g_current_particle_def + 0x74) = vel_z_max;
}

// ============================================================================
// Address: 0x800e7320 | Name: particle_def_set_spin_range
// Sets particle spin speed range.
// Offset 0x78 = spin_min, offset 0x70 = spin_max.
// ============================================================================
void particle_def_set_spin_range(float spin_min, float spin_max) {
    *(float*)(g_current_particle_def + 0x78) = spin_min;
    *(float*)(g_current_particle_def + 0x70) = spin_max;
}

// ============================================================================
// Address: 0x800e7334 | Name: particle_def_set_size_start_end
// Sets particle width/height start and end values for size interpolation.
// Offsets: 0x78=width_start, 0x7C=width_end, 0x80=height_start, 0x84=height_end
// ============================================================================
void particle_def_set_size_start_end(float width_start, float width_end,
                                     float height_start, float height_end) {
    *(float*)(g_current_particle_def + 0x78) = width_start;
    *(float*)(g_current_particle_def + 0x7c) = width_end;
    *(float*)(g_current_particle_def + 0x80) = height_start;
    *(float*)(g_current_particle_def + 0x84) = height_end;
}

// ============================================================================
// Address: 0x800e7358 | Name: particle_def_set_spawn_volume
// Sets 4 corner positions (vec3 each = 12 floats total) defining the
// spawn volume / bounding region for particle emission.
// Offsets: 0x9C-0xA4=corner0, 0xA8-0xB0=corner1, 0xB4-0xBC=corner2, 0xC0-0xC8=corner3
// ============================================================================
void particle_def_set_spawn_volume(float* corner0, float* corner1,
                                   float* corner2, float* corner3) {
    *(float*)(g_current_particle_def + 0x9c) = corner0[0];
    *(float*)(g_current_particle_def + 0xa0) = corner0[1];
    *(float*)(g_current_particle_def + 0xa4) = corner0[2];
    *(float*)(g_current_particle_def + 0xa8) = corner1[0];
    *(float*)(g_current_particle_def + 0xac) = corner1[1];
    *(float*)(g_current_particle_def + 0xb0) = corner1[2];
    *(float*)(g_current_particle_def + 0xb4) = corner2[0];
    *(float*)(g_current_particle_def + 0xb8) = corner2[1];
    *(float*)(g_current_particle_def + 0xbc) = corner2[2];
    *(float*)(g_current_particle_def + 0xc0) = corner3[0];
    *(float*)(g_current_particle_def + 0xc4) = corner3[1];
    *(float*)(g_current_particle_def + 0xc8) = corner3[2];
}

// ============================================================================
// Address: 0x800e73ec | Name: particle_def_set_color_end
// Sets the end color RGBA for particle color interpolation over lifetime.
// Offsets: 0x88=r_end, 0x8C=g_end, 0x90=b_end, 0x94=a_end
// ============================================================================
void particle_def_set_color_end(float r_end, float g_end, float b_end, float a_end) {
    *(float*)(g_current_particle_def + 0x88) = r_end;
    *(float*)(g_current_particle_def + 0x8c) = g_end;
    *(float*)(g_current_particle_def + 0x90) = b_end;
    *(float*)(g_current_particle_def + 0x94) = a_end;
}

// ============================================================================
// Address: 0x800e7410 | Name: particle_def_set_drag
// Sets particle drag/friction coefficient. Offset 0x98.
// ============================================================================
void particle_def_set_drag(float drag) {
    *(float*)(g_current_particle_def + 0x98) = drag;
}

// ============================================================================
// Address: 0x800e741c | Name: particle_def_set_blend_mode
// Sets particle blend mode (additive, alpha, etc.). Offset 0x10.
// ============================================================================
void particle_def_set_blend_mode(uint32_t blend_mode) {
    *(uint32_t*)(g_current_particle_def + 0x10) = blend_mode;
}

// ============================================================================
// Particle Overlay API
// These configure g_current_particle_overlay (DAT_80611e28), a 0x38-byte struct
// defining soft particle overlay effects (lens flares, glows) attached to emitters.
// ============================================================================

// ============================================================================
// Address: 0x800e7428 | Name: particle_overlay_create
// Allocates and initializes a particle overlay definition (0x38 bytes).
// Stores position offset, color, size, fade distance, and render params.
// Called from particle def init functions.
// ============================================================================
void particle_overlay_create(uint64_t packed_params, float fade_distance,
                             uint32_t param3, uint32_t param4, uint32_t param5,
                             uint32_t color_start, uint32_t color_end, uint32_t texture_id) {
    uint64_t alloc_result = gx_particle_begin_overlay_alloc();
    g_current_particle_overlay = (uint32_t*)(alloc_result >> 32);
    uint32_t* pos_ptr = (uint32_t*)(uint32_t)alloc_result;
    float extra_f = (float)alloc_result; // paired return

    memset_wrapper((int)g_current_particle_overlay, 0, 0x38);

    // Copy position (3 floats from alloc result)
    g_current_particle_overlay[0] = pos_ptr[0];
    g_current_particle_overlay[1] = pos_ptr[1];
    g_current_particle_overlay[2] = pos_ptr[2];

    // Set params
    ((float*)g_current_particle_overlay)[3] = extra_f;     // offset 0x0C: near fade start
    ((float*)g_current_particle_overlay)[4] = (float)((double)(texture_id ^ 0x80000000) - 4503601774854144.0); // offset 0x10: converted uint
    g_current_particle_overlay[5] = param4;                // offset 0x14
    g_current_particle_overlay[6] = param5;                // offset 0x18
    ((float*)g_current_particle_overlay)[7] = fade_distance; // offset 0x1C: size start
    ((float*)g_current_particle_overlay)[8] = (float)packed_params; // offset 0x20: size end
    ((float*)g_current_particle_overlay)[9] = (float)packed_params; // offset 0x24: width start
    ((float*)g_current_particle_overlay)[10] = (float)packed_params; // offset 0x28: width end

    gx_particle_end_overlay_alloc();
}

// ============================================================================
// Address: 0x800e7558 | Name: particle_overlay_set_fade_params
// Sets fade distance and color params on the current overlay.
// Offsets: 0x2C = far_fade_distance, 0x30 = color_a, 0x34 = color_b
// ============================================================================
void particle_overlay_set_fade_params(float far_fade_distance, uint32_t color_a, uint32_t color_b) {
    *(float*)(g_current_particle_overlay + 0x2c) = far_fade_distance;
    *(uint32_t*)(g_current_particle_overlay + 0x30) = color_a;
    *(uint32_t*)(g_current_particle_overlay + 0x34) = color_b;
}

// ============================================================================
// Address: 0x800e7574 | Name: particle_overlay_stub_nop
// Empty stub / no-op.
// ============================================================================
void particle_overlay_stub_nop(void) {
    return;
}
