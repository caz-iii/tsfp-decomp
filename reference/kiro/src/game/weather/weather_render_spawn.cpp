// Address: 0x800d83e4 | Name: weather_strip_vertex_setup | Date: 2026-08-11
// Address: 0x800d907c | Name: weather_strip_spawn | Date: 2026-08-11
// Address: 0x800d9d58 | Name: weather_radial_particle_spawn | Date: 2026-08-11
// Address: 0x800da0bc | Name: weather_directed_particle_spawn | Date: 2026-08-11
// Address: 0x800da2bc | Name: weather_position_velocity_tick | Date: 2026-08-11
// Address: 0x800da300 | Name: weather_billboard_render | Date: 2026-08-11
// Address: 0x800da4f4 | Name: weather_beam_spawn | Date: 2026-08-11
// Address: 0x800daa38 | Name: weather_sway_oscillation_tick | Date: 2026-08-11
// Address: 0x800dad5c | Name: weather_fade_out_tick | Date: 2026-08-11
// Address: 0x800dadf0 | Name: weather_stub_nop | Date: 2026-08-11
//
// Weather/precipitation rendering and particle spawn system.
// These functions handle the visual rendering of weather effects (rain, snow,
// lightning) including vertex generation for strip-based precipitation,
// radial particle emission, beam/bolt effects, per-frame animation (sway,
// fade), and screen-space billboard rendering.

#include "common.h"

// Forward declarations
extern int particle_slot_alloc(void);
extern void weather_slot_destroy(int* slot);
extern int scene_query_nodes_in_frustum(int param, int aabb_min, int aabb_out, int flags);
extern int sprite_get_texture_id(int tex_ref);

// Global sin/cos lookup table (2048 entries)
extern float g_sin_table[2048]; // DAT_80583228

// Per-type weather definition table (0x90 bytes per entry)
extern char g_weather_type_table[]; // DAT_804070ac region

// Frame delta time
extern float g_frame_dt; // FLOAT_80611df8

// ============================================================================
// weather_strip_vertex_setup
// Address: 0x800d83e4
// Per-quad vertex setup for weather strip rendering. Computes 4 transformed
// vertex positions using bilinear interpolation across the patch corners,
// generates texture coordinates (linear or bilinear depending on mode flag at
// +0x618c), computes per-vertex lighting from directional + ambient with
// distance-based attenuation (cube falloff), and optional random flicker for
// background strips. Called from weather_strip_spawn for each of 80 quads.
// ============================================================================
void weather_strip_vertex_setup(int render_ctx, int quad_index, int strip_data,
                                int slot, float* corner_a, float* corner_b,
                                float* corner_c, float* corner_d, int is_outer_ring)
{
    // NOTE: Ghidra decompilation shows MASSIVE vertex math with:
    // - 4 corner positions bilinearly interpolated using UV from corner params
    // - Result stored at render_ctx + quad_index*0x40 + 0x1400 (positions)
    // - Texture coords at render_ctx + quad_index*0x40 + 0x2800
    //   Two modes: linear (corners[3..4] delta) or bilinear (4-corner lerp)
    // - Optional lightmap coords at +0x3C00 when texture ID != -1
    // - Per-vertex lighting at +0x5500 (diffuse) using cube-falloff
    //   from center distance, with random flicker on outer ring
    // - Wind sway offsets at +0x5C80 and +0x58C0
    // - Fade factor at +0x6040
    //
    // Key constants:
    //   FLOAT_80614f7c = margin offset for UV expansion
    //   FLOAT_80614f80 = diffuse intensity multiplier
    //   FLOAT_80614f84 = sway amplitude
    //   FLOAT_80614f30 = distance normalization (used for sqrt comparison)
    //   FLOAT_80614f88 = sway base offset
    //   FLOAT_80614f20 = random flicker amplitude

    // [Full implementation would require paired-single float reconstruction]
    // Stub for compilation:
    (void)render_ctx;
    (void)quad_index;
    (void)strip_data;
    (void)slot;
    (void)corner_a;
    (void)corner_b;
    (void)corner_c;
    (void)corner_d;
    (void)is_outer_ring;
}

// ============================================================================
// weather_strip_spawn
// Address: 0x800d907c
// MASSIVE weather strip spawner. Creates a full precipitation volume with
// 16 angular sectors around the viewer, 5 concentric layers per sector.
// Each layer generates a quad via weather_strip_vertex_setup with positions
// derived from sin/cos table lookups and randomized radial offsets.
// Computes AABB from all 4 corner positions and queries scene for visible
// rooms. Also initializes per-strip rendering state including texture mode,
// opacity flags, and material references.
// ============================================================================
void weather_strip_spawn(void)
{
    // Allocate particle slot
    int slot_ptr = particle_slot_alloc();

    // Setup from weather type definition table:
    // - Flags at +0x8 (bit 0 = short lifetime mode, adds flag 0x20000)
    // - Material refs from type table at +0xC, +0x10
    // - Texture ID from sprite subsystem
    // - Rendering mode at +0x618c (0 = linear UVs, 1 = bilinear)
    // - Texture ID at +0x6184 (or -1 for untextured)

    // Compute 4 primary corner positions from definition:
    // - pfVar15 = corner0 (param offset +0x14)
    // - pfVar14 = corner1 (param offset +0x18)
    // - pfVar13 = corner2 (param offset +0x1C)
    // - pfVar12 = corner3 (param offset +0x20)
    // Edge vectors: (corner1 - corner0) stored at +0x44..+0x4C
    //               (corner2 - corner0) stored at +0x50..+0x58

    // Center position = bilinear center of patch at UV (0.5, 0.5)
    // stored at slot[8..10]

    // Generate 16 radial control points using randomized angle offsets
    // from the strip definition's base UV coordinates (+0x5C, +0x60).
    // Points are stored in a local array of 16 (x,y) pairs.

    // Subdivide into 8 interpolated pairs for smoother angular coverage.
    // Convert to polar form via atan2.

    // For each of 16 sectors, generate 5 layers:
    //   Layer distances use random scale factors (dVar29, dVar28, etc.)
    //   Each layer calls weather_strip_vertex_setup with 4 corner positions
    //   computed from sector angles * layer distance.
    //   Final (5th) layer uses full edge positions (outer ring, param is_outer=1).

    // After all 80 quads generated:
    // - Set all vertex normals to a single value from corner data (+0x18)
    // - Compute padded AABB from all 4 corner positions (min-3, max+3)
    // - Query scene_query_nodes_in_frustum for visible room list
    // - Store room count at slot[0x26]

    // [Full implementation omitted - requires paired-single float math]
    (void)slot_ptr;
}

// ============================================================================
// weather_radial_particle_spawn
// Address: 0x800d9d58
// Spawns a radial weather particle (rain splash, snowflake burst) with
// 8 evenly-spaced direction vectors around a center point. Positions are
// computed from sin/cos table, velocity is derived from a normalized direction
// vector, and an AABB is computed for scene room queries.
// ============================================================================
void weather_radial_particle_spawn(/* params from registers */)
{
    // 1. Call particle_slot_alloc to get a new slot
    // 2. Initialize slot fields:
    //    - +0x08: type ID (param_5)
    //    - +0x0C: scale = 0.0 (grows in)
    //    - +0x14: render context index
    //    - +0x10: material from type table (+0xAC offset)
    //    - +0x34: user data (param_9)
    //    - +0x38: scale = 0.0
    //    - +0x1C: sub-type from type table (+0xA4)
    //    - +0x2C: alpha = 0.0
    //    - +0x20..0x28: position = param_6 (vec3)
    //    - +0x3C..0x44: velocity from type table (+0xE4..+0xEC)
    //
    // 3. Extended data (pfVar14 = slot+0x18 pointer):
    //    - +0x46..0x48: start position (copy of param_6)
    //    - +0x43..0x45: direction (param_8)
    //    - +0x40..0x42: normal (param_7)
    //    - +0x49: scale = 0.0
    //    - +0x4A, +0x4B: timing params (extraout_f1, param_2)
    //
    // 4. Generate 8 radial direction vectors:
    //    For i in 0..7:
    //      angle = i * (sector_size / 8) + base_angle_offset
    //      dir[i].x = sin(angle)
    //      dir[i].y = 0.0 (planar)
    //      dir[i].z = cos(angle)
    //      velocity[i] = dir[i] * radius_from_type_table(+0xD0)
    //
    // 5. Normalize param_7 direction and scale by (1/3):
    //    FUN_8012f154(nx/3, ny/3, nz/3, extended_data)
    //
    // 6. Compute half-extents from type table radius (+0xD0), clamped min 1.0:
    //    half_extent = max(1.0, direction_component * radius)
    //
    // 7. Compute AABB = position +/- half_extents
    //    Store at slot +0x68..+0x94
    //
    // 8. Query scene for visible rooms: scene_query_nodes_in_frustum
    //    Store room count at slot +0x98
}

// ============================================================================
// weather_directed_particle_spawn
// Address: 0x800da0bc
// Spawns a directional weather particle with optional orientation basis.
// If a direction vector is provided, constructs a local coordinate frame
// (tangent + bitangent via cross products) for oriented rendering.
// ============================================================================
void weather_directed_particle_spawn(/* type, ctx, id, position, direction */)
{
    // 1. Call particle_slot_alloc
    // 2. Initialize core slot fields (type, scale, material, position)
    // 3. Extended data[0] = has_direction flag
    //    Extended data[1] = 0.0 (lifetime accumulator)
    //
    // 4. If direction != NULL:
    //    - Store direction at extended[2..4]
    //    - Compute tangent via cross product with world up (0,1,0):
    //      tangent = cross(direction, (0,1,0))
    //    - If |direction.y| <= threshold (nearly horizontal):
    //      Use alternative up vector (world constants from DAT_803cc7b4..c8)
    //    - Compute bitangent = cross(tangent, direction)
    //    - Store tangent at extended[5..7], bitangent at extended[8..10]
}

// ============================================================================
// weather_position_velocity_tick
// Address: 0x800da2bc
// Simple per-frame position integration: position += velocity * dt.
// Applied to weather particles for gravity/wind drift.
// ============================================================================
void weather_position_velocity_tick(int slot_ptr)
{
    float* velocity = *(float**)(slot_ptr + 0x18);

    // position.x += velocity.x * g_frame_dt
    *(float*)(slot_ptr + 0x20) += velocity[0] * g_frame_dt;
    // position.y += velocity.y * g_frame_dt
    *(float*)(slot_ptr + 0x24) += velocity[1] * g_frame_dt;
    // position.z += velocity.z * g_frame_dt
    *(float*)(slot_ptr + 0x28) += velocity[2] * g_frame_dt;
}

// ============================================================================
// weather_billboard_render
// Address: 0x800da300
// Renders a weather particle as a screen-space billboard sprite.
// Projects 3D position to screen coordinates using the camera view matrix,
// applies sprite scale from the weather type table, clips against viewport
// bounds, then submits a textured quad via the sprite renderer.
// ============================================================================
void weather_billboard_render(void)
{
    // 1. Get weather type definition (slot[1] * 0x90 into type table)
    // 2. Get texture ID via sprite_get_texture_id from type material
    // 3. Read texture dimensions (width at +0x4E, height at +0x52)
    //
    // 4. Transform position (slot+0x20) to clip space via view-projection matrix:
    //    FUN_8012fb70(camera_matrix, position, out_clip)
    //    Result: local_48 = x_clip, local_44 = y_clip, local_40 = w_clip, local_3c = w
    //
    // 5. Frustum rejection: if any clip coord outside [-w, +w], skip render
    //
    // 6. Perspective divide: ndc = clip / w
    //    Screen position = ndc * viewport_scale + viewport_offset
    //
    // 7. Compute billboard corners from type table scale:
    //    half_w = type_scale_x * perspective_scale
    //    half_h = type_scale_y * perspective_scale
    //    corners = center +/- half extents
    //
    // 8. Z-depth = 1.0 + ndc.z (for depth buffer)
    //
    // 9. Setup texture (FUN_8016e77c)
    // 10. Submit quad (FUN_8017a62c) with texture dims and color from type table
}

// ============================================================================
// weather_beam_spawn
// Address: 0x800da4f4
// MASSIVE beam/lightning bolt spawner. Creates a multi-segment beam effect
// from a start position to a target position, with per-segment vertex
// placement along the beam direction. Each segment has randomized vertex
// counts (8 or 16 based on random check), angular offsets for natural
// curvature, and distance-based opacity falloff.
// ============================================================================
void weather_beam_spawn(/* type, ctx, position, target, color, intensity, seg_count, user */)
{
    // 1. Call particle_slot_alloc
    // 2. Initialize slot: type, scale=0.13, lifetime, target position
    //    slot[4] = -1 (no parent), slot[7] = 1 (active)
    //    user_data = param_8
    //
    // 3. Compute beam direction:
    //    dir = normalize(target - position)
    //    Store length at extended[0x25C]
    //
    // 4. Compute local frame (tangent/bitangent) via cross product:
    //    Same algorithm as weather_directed_particle_spawn
    //    using world up or side vector based on direction.y threshold
    //    Store at extended[0x25D..0x262]
    //
    // 5. Clamp beam visual length to max (FLOAT_80614fa8)
    //    Compute direction-scaled positions
    //
    // 6. For each segment (1..segment_count):
    //    - Random vertex count per segment (8 or 16 from FUN_801c1714)
    //    - Random angular phase offset (for twist)
    //    - Random brightness factor per segment
    //    - Random radius variation
    //    - Per-vertex positions: position + direction * (vertex_index / total)
    //    - Stored at segment * 0x1E0 + vertex * 0x1C + 0x10
    //
    // 7. Set control points for start/mid/end rendering:
    //    extended[1..7]: start info (state=3)
    //    extended[8..0x16]: end info (state=5, alpha from color>>24)
    //    extended[0x13..0x16]: position at full length (state=5, alpha)
    //
    // 8. Segments create a tube-like lightning bolt with per-vertex
    //    randomization for organic appearance
}

// ============================================================================
// weather_sway_oscillation_tick
// Address: 0x800daa38
// Per-frame weather sway/oscillation tick. Applies sin-based wind sway to
// per-vertex positions of strip-based weather effects. Uses a rotation matrix
// derived from the strip's angular phase, with distance-based attenuation
// (closer segments sway less). Each vertex is offset by the sway vector
// scaled by amplitude, wind direction, and frame delta.
// ============================================================================
void weather_sway_oscillation_tick(void)
{
    // 1. Get extended data pointer (slot+0x18)
    // 2. For each segment (1..segment_count):
    //    a. Compute sin/cos of segment phase angle (+0x71)
    //       Store at segment+0x72, +0x73
    //    b. For each vertex in segment:
    //       - Compute distance falloff: (slot+0x0C - segment_dist) / max_dist
    //         Clamped to [0, 1]
    //       - Compute sway magnitude = base_amplitude * sqrt(distance) *
    //         sin(phase + vertex_offset) * frame_dt
    //       - Apply sway in wind direction:
    //         vertex.xyz += sway * (wind_x * cos + wind_z * sin) * dt
    //       - Per-vertex alpha (vertex 0 = 0, vertex 1 = color>>25, else color>>24)
    //
    // Uses constants:
    //   FLOAT_80614fac = sway amplitude base
    //   FLOAT_80614fb0 = sway multiplier
    //   FLOAT_80614f54 = distance normalization
    //   g_frame_dt for time integration
}

// ============================================================================
// weather_fade_out_tick
// Address: 0x800dad5c
// Per-frame weather fade-out tick. Decrements opacity and shrinks scale
// until the particle is dead, then destroys it. Only operates on particles
// in state 2 (dying/fading).
// ============================================================================
void weather_fade_out_tick(int* slot)
{
    float decay_rate = 0.96f; // FLOAT_80614fb4 (approximate)
    int* extended = (int*)slot[6];

    // Compute dt from global frame counter
    // float dt = g_frame_dt * (1.0 / (float)g_frame_counter)
    float dt = g_frame_dt; // simplified

    if (dt > 0.0f && extended[0] == 2) {
        // Decrement opacity
        *(float*)&extended[8] -= dt;
        // Shrink scale
        *(float*)&extended[9] *= decay_rate;

        // If opacity <= 0, destroy
        if (*(float*)&extended[8] < 0.0f) {
            weather_slot_destroy(slot);
        }
    }
}

// ============================================================================
// weather_stub_nop
// Address: 0x800dadf0
// Empty stub - no operation. Placeholder callback for unused weather states.
// ============================================================================
void weather_stub_nop(void)
{
    return;
}
