// Address: 0x800dadf4 | Name: weather_trail_spawn | Date: 2026-08-11
// Address: 0x800daed8 | Name: weather_trail_compute_basis | Date: 2026-08-11
// Address: 0x800db03c | Name: weather_trail_advance_tick | Date: 2026-08-11
// Address: 0x800db1bc | Name: weather_trail_set_target | Date: 2026-08-11
// Address: 0x800db23c | Name: weather_flare_render | Date: 2026-08-11
// Address: 0x800db5dc | Name: rng_set_seed | Date: 2026-08-11
// Address: 0x800db5e4 | Name: rng_next | Date: 2026-08-11
// Address: 0x800db5fc | Name: rng_float | Date: 2026-08-11
// Address: 0x800db648 | Name: particle_type_spawn_with_aabb | Date: 2026-08-11
// Address: 0x800db90c | Name: weather_snowflake_render | Date: 2026-08-11
//
// Weather trail/streak effects, flare/glow rendering, RNG utilities,
// and snowflake/leaf billboard rendering.

#include "common.h"

// Forward declarations
extern int particle_slot_alloc(void);
extern void weather_slot_destroy(int* slot);
extern int sprite_get_texture_id(int ref);

// Global RNG state
static int g_rng_seed; // DAT_80611df0

// Global frame delta
extern float g_frame_dt; // FLOAT_80611df8

// Sin/cos lookup table
extern float g_sin_table[2048]; // DAT_80583228

// Weather type table (0x90 bytes per entry at DAT_804070ac region)
// Flare type table (0x24 bytes per entry at DAT_8040baac region)

// ============================================================================
// weather_trail_spawn
// Address: 0x800dadf4
// Spawns a weather trail/streak particle effect (e.g. rain streak, lightning
// trail). Allocates a particle slot, initializes position and direction,
// then calls weather_trail_compute_basis to build a local coordinate frame
// from the direction vector.
// ============================================================================
void weather_trail_spawn(int type, int ctx, int id,
                         float* position, float* direction, int user_data)
{
    int slot = particle_slot_alloc();
    float* extended = *(float**)(slot + 0x18);

    // Core slot init
    *(int*)(slot + 0x08) = 0;            // type flags
    *(float*)(slot + 0x0C) = 0.0f;       // scale (grows in)
    *(int*)(slot + 0x14) = ctx;          // render context
    *(float*)(slot + 0x10) = 0.0f;       // material
    *(int*)(slot + 0x04) = -1;           // no parent
    *(int*)(slot + 0x1C) = 1;            // active
    *(float*)(slot + 0x20) = position[0]; // position
    *(float*)(slot + 0x24) = position[1];
    *(float*)(slot + 0x28) = position[2];
    *(int*)(slot + 0x34) = user_data;
    *(int*)(slot + 0x98) = 0;            // room list count

    // Extended data init
    *(int*)&extended[0] = id;            // type index
    *(float*)&extended[5] = 0.0f;        // current length = 0
    *(int*)&extended[0xF] = 0;           // state = forward (0)
    *(float*)&extended[0xC] = position[0]; // origin position
    *(float*)&extended[0xD] = position[1];
    *(float*)&extended[0xE] = position[2];

    // Compute local frame from direction
    weather_trail_compute_basis(/* radius from extraout_f1 */(double)0.0, slot, direction);
}

// ============================================================================
// weather_trail_compute_basis
// Address: 0x800daed8
// Computes a local coordinate frame (direction + tangent + bitangent) from
// a direction vector, for oriented trail rendering. Uses cross product with
// a world axis (up or side, depending on direction.y threshold).
// Same algorithm as weather_directed_particle_spawn's basis computation.
// ============================================================================
void weather_trail_compute_basis(double radius, int slot, float* direction)
{
    int ext = *(int*)(slot + 0x18);

    // Store radius
    *(float*)(ext + 0x10) = (float)radius;

    // Store direction
    *(float*)(ext + 0x04) = direction[0];
    *(float*)(ext + 0x08) = direction[1];
    *(float*)(ext + 0x0C) = direction[2];

    float dx = *(float*)(ext + 0x04);
    float dy = *(float*)(ext + 0x08);
    float dz = *(float*)(ext + 0x0C);

    // Compute tangent via cross product with world axis
    // If |dy| < threshold: use world up (0, 1, 0) variant
    // Else: use world side (1, 0, 0) variant
    float tx, ty, tz;
    if (fabsf(dy) < 0.9f) { // FLOAT_80614fa4
        // cross(dir, world_up_variant)
        // DAT_803cc7e4..803cc7f8 constants
        tx = dy * 0.0f - dz * 1.0f;  // simplified
        ty = dz * 0.0f - dx * 0.0f;
        tz = dx * 1.0f - dy * 0.0f;
    } else {
        // cross(dir, world_side_variant)
        tx = dy * 0.0f - dz * 0.0f;
        ty = dz * 1.0f - dx * 0.0f;
        tz = dx * 0.0f - dy * 1.0f;
    }

    *(float*)(ext + 0x18) = tx;
    *(float*)(ext + 0x1C) = ty;
    *(float*)(ext + 0x20) = tz;

    // Bitangent = cross(tangent, direction)
    *(float*)(ext + 0x24) = ty * dz - tz * dy;
    *(float*)(ext + 0x28) = tz * dx - tx * dz;
    *(float*)(ext + 0x2C) = tx * dy - ty * dx;
}

// ============================================================================
// weather_trail_advance_tick
// Address: 0x800db03c
// Per-frame trail extension/retraction tick. In forward mode (state 0),
// extends the trail along direction at speed from type table, clamping
// to max length. In reverse mode (state != 0), retracts the trail and
// destroys when fully retracted. Updates the trail tip position from
// origin + direction * current_length.
// ============================================================================
void weather_trail_advance_tick(int* slot)
{
    int* ext = (int*)slot[6]; // +0x18
    float dt = g_frame_dt; // scaled by frame counter

    if (ext[0xF] == 0) {
        // Forward mode: extend trail
        float speed = *(float*)&ext[4] * dt;
        // position += direction * speed
        *(float*)&slot[8]  += *(float*)&ext[1] * speed;
        *(float*)&slot[9]  += *(float*)&ext[2] * speed;
        *(float*)&slot[10] += *(float*)&ext[3] * speed;
        *(float*)&ext[5] += speed; // current length

        // Clamp to max length from flare type table (+0xB4 offset * 0x24)
        float max_len = /* DAT_8040bab4 + ext[0] * 0x24 */ 100.0f; // placeholder
        if (*(float*)&ext[5] > max_len) {
            *(float*)&ext[5] = max_len;
            // Compute tip: origin + direction * (-length)
            *(float*)&ext[0xC] = *(float*)&ext[1] * -*(float*)&ext[5] + *(float*)&slot[8];
            *(float*)&ext[0xD] = *(float*)&ext[2] * -*(float*)&ext[5] + *(float*)&slot[9];
            *(float*)&ext[0xE] = *(float*)&ext[3] * -*(float*)&ext[5] + *(float*)&slot[10];
        }
    } else {
        // Reverse mode: retract trail
        *(float*)&ext[5] -= *(float*)&ext[4] * dt;
        // Update tip position
        *(float*)&ext[0xC] = *(float*)&ext[1] * -*(float*)&ext[5] + *(float*)&slot[8];
        *(float*)&ext[0xD] = *(float*)&ext[2] * -*(float*)&ext[5] + *(float*)&slot[9];
        *(float*)&ext[0xE] = *(float*)&ext[3] * -*(float*)&ext[5] + *(float*)&slot[10];

        // Destroy when fully retracted
        if (*(float*)&ext[5] < 0.0f) {
            weather_slot_destroy(slot);
        }
    }
}

// ============================================================================
// weather_trail_set_target
// Address: 0x800db1bc
// Sets a new target position for a weather trail and computes the distance
// from the stored origin to the new position. Activates tracking mode
// (extended[0x3C] = 1).
// ============================================================================
void weather_trail_set_target(int slot, float* target_pos)
{
    int ext = *(int*)(slot + 0x18);

    // Enable tracking
    *(int*)(ext + 0x3C) = 1;

    // Update position
    *(float*)(slot + 0x20) = target_pos[0];
    *(float*)(slot + 0x24) = target_pos[1];
    *(float*)(slot + 0x28) = target_pos[2];

    // Compute distance from origin to new position
    float dx = *(float*)(slot + 0x20) - *(float*)(ext + 0x30);
    float dy = *(float*)(slot + 0x24) - *(float*)(ext + 0x34);
    float dz = *(float*)(slot + 0x28) - *(float*)(ext + 0x38);
    *(float*)(ext + 0x14) = sqrtf(dx*dx + dy*dy + dz*dz);
}

// ============================================================================
// weather_flare_render
// Address: 0x800db23c
// Renders a weather flare/glow effect with multiple layers:
// 1. Base glow sprite (additive blended, texture 0x28F)
// 2. Inner core sprite (additive, texture 0x28D with random rotation)
// 3. Rotating light shafts (N quads from type table count, each a billboard
//    strip using sin/cos for rotation, at varying distances along the
//    direction vector). Written directly to GX FIFO (DAT_cc008000).
// ============================================================================
void weather_flare_render(void)
{
    // 1. Get type def from flare table (slot extended[0] * 0x24)
    // 2. Get texture ID from type table material ref
    // 3. Read color (at +0xB8), inner color (+0xC4), shaft color (+0xCC)
    //    Colors are byte-swapped for GX format (RGBA -> ABGR)
    //
    // 4. Set blend mode additive (FUN_8016e8ac(2), FUN_8016f00c(2))
    //
    // 5. Render base glow: FUN_800de07c with scale from type +0xC0
    //    at particle position (slot+0x20)
    //
    // 6. Render inner core: FUN_800de07c with scale from type +0xC8
    //    Random rotation from FUN_801310c0
    //
    // 7. If extended[5] (trail length) > 0:
    //    For each shaft (count from type +0xBC):
    //      Two passes (offset and non-offset):
    //        angle = time_scale * slot.time + offset
    //        sin/cos from lookup table
    //        radius = frame_scale * sin * type_radius(+0xB0)
    //        For 4 vertices along direction:
    //          t = [0.0, 0.5, 0.75, 1.0] (parameterized distance)
    //          pos = position - direction * length * t
    //          offset by tangent/bitangent * radius
    //          Write to GX FIFO: position, color, UV
    //
    // Uses GX direct FIFO writes (DAT_cc008000)
}

// ============================================================================
// rng_set_seed
// Address: 0x800db5dc
// Sets the global RNG seed.
// ============================================================================
void rng_set_seed(int seed)
{
    g_rng_seed = seed;
}

// ============================================================================
// rng_next
// Address: 0x800db5e4
// Linear congruential generator (LCG) step.
// Standard MINSTD variant: next = current * 214013 + 2531011
// (0x343FD and 0x269EC3 — matches MSVC's rand() constants)
// ============================================================================
int rng_next(int current)
{
    return current * 0x343FD + 0x269EC3;
}

// ============================================================================
// rng_float
// Address: 0x800db5fc
// Returns a random float in [0, 1). Advances the global RNG seed,
// extracts bits 16..30 (15 bits), and scales to [0, 1) via
// multiplication by (1.0 / 32768.0).
// ============================================================================
float rng_float(void)
{
    g_rng_seed = g_rng_seed * 0x343FD + 0x269EC3;
    int bits = (g_rng_seed >> 16) & 0x7FFF;
    return (float)bits * (1.0f / 32768.0f); // FLOAT_80614fbc
}

// ============================================================================
// particle_type_spawn_with_aabb
// Address: 0x800db648
// Spawns a particle from the weather type table with optional AABB computation.
// Allocates a slot, sets lifetime from type+subtype offsets, positions at
// given coordinates, and when flag 0x80000 is set, computes a local-space
// AABB from the type's velocity/acceleration bounds for scene room queries.
// ============================================================================
void particle_type_spawn_with_aabb(int type, int ctx, int id,
                                   float* position)
{
    int type_offset = /* from uVar17 */ 0; // type table index * 0x90
    int slot = particle_slot_alloc();

    // Slot init
    *(int*)(slot + 0x08) = id;
    *(int*)(slot + 0x04) = type;          // type index
    *(float*)(slot + 0x0C) = 0.0f;        // scale
    // Lifetime = type_table[+0xAC] + type_table[+0xB4]
    *(float*)(slot + 0x10) = /* lifetime sum */ 1.0f;
    *(int*)(slot + 0x1C) = 0;             // sub-type
    *(float*)(slot + 0x20) = position[0];
    *(float*)(slot + 0x24) = position[1];
    *(float*)(slot + 0x28) = position[2];
    *(int*)(slot + 0x98) = 0;             // room count

    // Extended data: clear frame counter
    int ext = *(int*)(slot + 0x18);
    *(int*)(ext + 0x1F44) = 0;

    // If flag 0x80000 set: compute AABB from velocity/acceleration bounds
    if ((id & 0x80000) != 0) {
        // Read type velocity bounds (+0xE4..+0xF8)
        // Compute parabolic extent: v*t + 0.5*a*t^2 for each axis
        // min/max from both endpoints of velocity range
        // Store AABB at slot +0x68..+0x7C (min xyz, max xyz)
        // [Detailed computation omitted - uses type table constants]
    }
}

// ============================================================================
// weather_snowflake_render
// Address: 0x800db90c
// MASSIVE per-particle snowflake/leaf renderer. Iterates all active particles
// in the slot's particle list, performing:
// 1. Distance-based culling (skip if beyond type max range)
// 2. View-space billboard computation (position relative to camera)
// 3. Random phase rotation for natural tumbling
// 4. Perspective-correct quad corners using camera right/up vectors
// 5. View-angle culling (skip particles nearly edge-on to camera)
// 6. Distance-based LOD skip (every Nth particle based on angle)
// 7. Near-clip fade (alpha ramp when close to camera)
// 8. Per-particle color with distance-based alpha falloff (4th power)
// 9. Direct GX FIFO quad submission with position, color, UV
//
// Uses the particle system's internal RNG (rng_set_seed/rng_float) for
// deterministic per-particle rotation across frames.
// ============================================================================
void weather_snowflake_render(void)
{
    // 1. Get particle slot data and type table entry
    // 2. Get texture from type, read base color
    // 3. Set blend mode (FUN_8016f00c(1))
    // 4. Seed RNG from stored per-slot seed (for deterministic rotation)
    //
    // 5. Determine render order: start from stored index, wrap around
    //    Skip the starting particle index itself
    //
    // 6. Setup texture (FUN_8016e77c)
    //    Save blend state (FUN_8016e8ac(2))
    //
    // 7. For each particle in circular list:
    //    a. Compute distance from camera: slot.time - particle.birth_time
    //       If outside [0, max_range]: skip
    //
    //    b. Random phase from rng_float (deterministic per particle)
    //
    //    c. Compute world position:
    //       pos = base_position + direction * distance + gravity_offset
    //       where gravity uses type constants (+0x108, +0x10C, +0x110)
    //
    //    d. Project to clip space via camera matrix (FUN_8012fb70)
    //       Frustum reject if outside clip bounds
    //
    //    e. Compute billboard scale from type (+0xC4) * perspective factor
    //
    //    f. Normalize segment direction between this and next particle
    //       Compute dot with camera forward vector
    //       If nearly parallel (dot > threshold): skip (edge-on)
    //       If moderately parallel: skip every Nth (LOD)
    //
    //    g. Near-clip alpha fade: if w < threshold, scale alpha
    //
    //    h. Compute 4 quad corners using camera right/up * scale
    //       rotated by sin/cos of random phase angle
    //
    //    i. Compute per-particle color:
    //       - Alpha = base_alpha * distance_factor^4 (4th power falloff)
    //       - RGB channels scaled by distance factor
    //
    //    j. Submit quad to GX FIFO (FUN_80172a4c + DAT_cc008000 writes):
    //       4 vertices with position, color (RGBA), UV (0,0 / 0,1 / 1,0 / 1,1)
    //
    // 8. Restore blend state, cleanup
}
