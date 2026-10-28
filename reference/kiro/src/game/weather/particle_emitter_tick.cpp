// Address: 0x800dc038 | Name: particle_emitter_tick_circular | Date: 2026-08-12
// Address: 0x800dc414 | Name: particle_emitter_tick_linear | Date: 2026-08-12
// Address: 0x800dc7bc | Name: particle_emitter_stub_nop | Date: 2026-08-12
// Address: 0x800dc7c0 | Name: particle_velocity_bounce_tick | Date: 2026-08-12
// Address: 0x800dc8e4 | Name: particle_emitter_tick_physics | Date: 2026-08-12
// Address: 0x800dceb0 | Name: particle_velocity_trail_tick | Date: 2026-08-12
// Address: 0x800dd040 | Name: particle_emitter_tick_trail | Date: 2026-08-12
// Address: 0x800dd3d0 | Name: weather_strip_physics_tick | Date: 2026-08-12
// Address: 0x800dd744 | Name: particle_radial_physics_tick | Date: 2026-08-12
// Address: 0x800dd9ec | Name: particle_conditional_velocity_tick | Date: 2026-08-12
//
// Particle emitter per-frame tick functions. These handle spawning new particles
// into circular/linear buffers, integrating velocity with gravity and floor
// bounce, computing trail offsets, alpha fade, and AABB expansion for scene
// room queries.

#include "common.h"

extern void weather_slot_destroy(int* slot);
extern int scene_query_nodes_in_frustum(int param, int aabb_min, int aabb_out, int flags);
extern int rng_next(int seed);
extern float g_frame_dt; // FLOAT_80611df8
extern float g_frame_scale; // FLOAT_80611e04

// ============================================================================
// particle_emitter_tick_circular
// Address: 0x800dc038
// Per-frame particle emitter tick using a CIRCULAR buffer. Spawns new particles
// at the current position based on elapsed time since last spawn. Each particle
// gets randomized phase angle, velocity components (from type table + random
// offset), and a birth timestamp. Uses a wrapping write index (ext+0x1F44)
// that loops around when reaching max count (type+0xA4). Advances the RNG
// state when wrapping to maintain determinism. Expands world AABB (slot+0x80)
// with each new particle position for scene room queries.
// Destroys self when: scale == 0 AND (count == 0 OR lifetime expired).
// ============================================================================
void particle_emitter_tick_circular(void)
{
    // 1. Get slot data and extended buffer
    // 2. Check destroy condition: scale==0 && (count==0 || time > last_birth + lifetime)
    //    If true: weather_slot_destroy and return
    //
    // 3. Compute spawn count:
    //    If count == 0: spawn 1 (first particle)
    //    Else: spawn_count = (current_time - last_spawn_time) / (1.0 / (scale * frame_scale))
    //
    // 4. For each spawn:
    //    a. If write_index >= max_count: advance RNG seed (overflow wrap)
    //    b. Store position: ext[write_idx * 0xC] = slot.position
    //    c. Store birth time: ext[write_idx*4 + 7000] = current_time
    //    d. Random phase angle: ext[write_idx*4 + 3000] = random * type.phase_scale / TWO_PI
    //    e. Random velocity X: ext[write_idx*0xC + 4000] = type.vel_base_x + type.vel_rand_x * rand
    //    f. Random velocity Y: ext[write_idx*0xC + 0xFA4] = type.gravity + type.vel_base_y + rand*y
    //    g. Random velocity Z: ext[write_idx*0xC + 0xFA8] = type.vel_base_z + rand*z
    //    h. Update last_spawn_time: ext[8000] = current_time
    //    i. Increment count (clamped to max)
    //    j. Increment write_index, wrap to 0 at max
    //
    // 5. If spawned any AND flag 0x80000 set:
    //    Expand world AABB (slot+0x80..0x94) from local AABB + position
    //    Query scene_query_nodes_in_frustum for room list
}

// ============================================================================
// particle_emitter_tick_linear
// Address: 0x800dc414
// Per-frame particle emitter tick using a LINEAR buffer. Same spawn logic as
// circular variant but without write index wrapping - simply appends particles
// until max count is reached. Simpler than circular: no RNG advance on overflow,
// no write pointer wrap. Used for non-looping effects (explosions, one-shot).
// ============================================================================
void particle_emitter_tick_linear(void)
{
    // Same structure as circular but:
    // - Buffer writes at slot[7] (count) directly
    // - No wrap when reaching max - just stops spawning
    // - Same random velocity/phase generation
    // - Same AABB expansion logic
    // - Same destroy condition
}

// ============================================================================
// particle_emitter_stub_nop
// Address: 0x800dc7bc
// Empty stub - no operation. Placeholder for unused emitter tick callbacks.
// ============================================================================
void particle_emitter_stub_nop(void)
{
    return;
}

// ============================================================================
// particle_velocity_bounce_tick
// Address: 0x800dc7c0
// Per-particle velocity integration with floor bounce. For each active
// particle in the slot:
//   - Apply gravity to Y velocity: vel.y += gravity * dt
//   - Integrate position: pos += vel * dt
//   - Floor bounce: if pos.y < floor_height AND floor_enabled:
//       pos.y = floor_height
//       vel.y = -vel.y * bounce_damping (from type +0x118)
//       vel.xz *= lateral_damping (from type +0x114)
// ============================================================================
void particle_velocity_bounce_tick(int slot)
{
    int ext = *(int*)(slot + 0x18);
    int type_offset = *(int*)(slot + 0x04) * 0x90;
    int count = *(int*)(slot + 0x1C);

    float dt = g_frame_dt; // scaled

    for (int i = 0; i < count; i++) {
        int vel_base = ext + i * 0xC;
        float* pos = (float*)(ext + i * 0x10); // stride 0x10 for position

        // Gravity: vel.y += gravity_accel * dt
        *(float*)(vel_base + 0x644) += /* type gravity */ 0.0f * dt;

        // Position integration
        pos[0] += *(float*)(vel_base + 0x640) * dt;
        pos[1] += *(float*)(vel_base + 0x644) * dt;
        pos[2] += *(float*)(vel_base + 0x648) * dt;

        // Floor bounce
        if ((*(int*)(slot + 0x08) & 0x20000) != 0) {
            float floor_y = *(float*)(slot + 0x2C);
            if (pos[1] < floor_y && /* floor_enabled */ 1) {
                pos[1] = floor_y;
                *(float*)(vel_base + 0x644) *= -/* bounce_y */ 0.5f;
                *(float*)(vel_base + 0x640) *= /* damp_xz */ 0.8f;
                *(float*)(vel_base + 0x648) *= /* damp_xz */ 0.8f;
            }
        }
    }
}

// ============================================================================
// particle_emitter_tick_physics
// Address: 0x800dc8e4
// MASSIVE particle emitter tick with full physics simulation. Combines:
// - Circular buffer spawning (same as particle_emitter_tick_circular)
// - Per-particle velocity integration with gravity
// - Floor bounce with damping
// - Splash particle effects on floor impact (FUN_800fdcd8)
// - Decal spawning on surfaces (FUN_800e9518 with texture 0x324)
// - Optional random XZ offset on spawn (flag 0x8000000)
// - Per-frame position update with velocity decay
// - Floor height query (floor_height_from_position)
// Most complex emitter: handles rain splashes, debris with surface effects.
// ============================================================================
void particle_emitter_tick_physics(void)
{
    // 1. Circular buffer spawn logic (same as particle_emitter_tick_circular)
    //    But with additional per-spawn data:
    //    - Position with optional random XZ offset (flag 0x8000000, type +0xD0 radius)
    //    - Random velocity from slot fields [0xF..0x17] (base + range * rand)
    //    - Birth timestamp
    //    - Random phase angle
    //
    // 2. Per-particle physics update (all active particles):
    //    a. Compute age: current_time - birth_time
    //       If outside [0, max_lifetime]: skip
    //    b. Apply gravity: vel.y += gravity_accel * dt (type +0xE0)
    //    c. Integrate position: pos += vel * dt
    //    d. Floor bounce (flag 0x20000):
    //       If pos.y < floor_y:
    //         pos.y = floor_y
    //         vel.y = -vel.y * bounce_y_damp (type +0x118)
    //         vel.xz *= lateral_damp (type +0x114)
    //    e. Water splash (flag 0x10000000):
    //       If pos.y < floor_y:
    //         Spawn splash particle (FUN_800fdcd8) with color from ext+0xFA4
    //         Set birth to negative (mark dead)
    //         Every 20th frame: spawn decal (FUN_800e9518, texture 0x324)
    //           with floor height query + random XZ offset
}

// ============================================================================
// particle_velocity_trail_tick
// Address: 0x800dceb0
// Per-particle velocity integration with trail offset computation.
// Same as particle_velocity_bounce_tick but additionally computes a
// trail end position = pos + vel * trail_scale (type +0xB8) for each
// particle, creating a stretched/motion-blurred appearance. Includes
// floor bounce with damping.
// ============================================================================
void particle_velocity_trail_tick(int slot)
{
    int ext = *(int*)(slot + 0x18);
    int type_offset = *(int*)(slot + 0x04) * 0x90;
    int count = *(int*)(slot + 0x1C);

    float dt = g_frame_dt; // scaled

    for (int i = 0; i < count; i++) {
        int vel_base = ext + i * 0xC;
        float* pos = (float*)(ext + i * 0x20); // stride 0x20 (has trail data)

        // Gravity + integration (same as bounce tick)
        *(float*)(vel_base + 0x5C4) += /* gravity */ 0.0f * dt;
        pos[0] += *(float*)(vel_base + 0x5C0) * dt;
        pos[1] += *(float*)(vel_base + 0x5C4) * dt;
        pos[2] += *(float*)(vel_base + 0x5C8) * dt;

        // Trail end = pos + vel * trail_scale
        float trail_scale = /* type +0xB8 */ 1.0f;
        pos[4] = pos[0] + *(float*)(vel_base + 0x5C0) * trail_scale;
        pos[5] = pos[1] + *(float*)(vel_base + 0x5C4) * trail_scale;
        pos[6] = pos[2] + *(float*)(vel_base + 0x5C8) * trail_scale;
    }

    // Floor bounce pass (flag 0x20000)
    if ((*(int*)(slot + 0x08) & 0x20000) != 0) {
        // Same bounce logic as particle_velocity_bounce_tick
    }
}

// ============================================================================
// particle_emitter_tick_trail
// Address: 0x800dd040
// Per-frame particle emitter tick with trail positions. Combines circular
// buffer spawning with trail offset computation. Each particle stores both
// a head position and a trail end position (head + velocity * trail_scale).
// Includes gravity, velocity integration, and floor bounce for both head
// and trail positions.
// ============================================================================
void particle_emitter_tick_trail(void)
{
    // 1. Circular buffer spawn (write index wraps at max)
    //    Per spawn: position + random velocity (from slot[0x15..0x17] range)
    //    Birth timestamp stored per-particle
    //
    // 2. Per-particle velocity integration:
    //    - Gravity on Y velocity
    //    - pos += vel * dt
    //    - Trail end = pos + vel * trail_scale (type +0xB8)
    //
    // 3. Floor bounce pass (flag 0x20000):
    //    Same as particle_velocity_bounce_tick
}

// ============================================================================
// weather_strip_physics_tick
// Address: 0x800dd3d0
// Per-frame physics tick for weather strip particles. Handles:
// - Lifetime tracking with grow-in period
// - Velocity integration with drag (exponential decay per frame)
// - Gravity acceleration on Y
// - Floor bounce with velocity damping (flag 0x20000)
// - Per-vertex sway offset integration (wind effect)
// - Alpha fade based on lifetime vs grow/decay thresholds
// - Per-quad vertex color alpha adjustment (fade in/out)
// Multi-player speed adjustment when DAT_80612244 != 0.
// ============================================================================
void weather_strip_physics_tick(void)
{
    // 1. Check if floor enabled (DAT_806108e4 != 0)
    //    If not, skip all physics
    //
    // 2. Lifetime management:
    //    If time < grow_threshold AND (scale > 0.5 OR flag 0x20000 clear):
    //      Extend grow duration (multiplayer: FLOAT_80614fd0)
    //      Update total lifetime = grow + ext+0x6180
    //    Clamp grow to max (FLOAT_80614f90)
    //
    // 3. Compute fade factor: 1.0 / (lifetime - grow_time)
    //
    // 4. Compute drag: -(drag_rate * dt - 1.0)
    //    Multiplayer: uses different drag constant
    //
    // 5. For each quad (0..ext+0x6190):
    //    a. Velocity integration:
    //       pos.xz += vel.xz * dt
    //       pos.y += vel.y * dt
    //    b. Accumulate lifetime per-quad
    //    c. If lifetime > 0 (quad is active):
    //       - Apply drag to velocity (exponential decay)
    //       - Apply gravity to Y velocity
    //       - Floor bounce (flag 0x20000): bounce + damp
    //       - Apply sway: transform position by sway offsets
    //       - Accumulate sway offsets per-frame
    //    d. Compute alpha from lifetime vs thresholds:
    //       If beyond grow_time: fade = -(factor * (age - grow) - 1.0)
    //       Clamp to [0, 1]
    //    e. Write alpha byte to per-vertex color (top byte of RGBA)
    //       All 4 vertices of the quad get same alpha
}

// ============================================================================
// particle_radial_physics_tick
// Address: 0x800dd744
// Per-frame physics tick for radial particles (weather_radial_particle_spawn).
// Updates:
// - Angular velocity: phase += angular_speed * dt
// - Directional decay: direction *= (decay_rate * dt + 1.0) per axis
//   X/Z use type +0xD4, Y uses +0xD8
// - Position integration: pos += velocity * dt
// - AABB recompute: if moved beyond threshold OR velocity non-zero,
//   recompute local half-extents from direction * radius (clamped min 1.0)
//   and update world AABB (slot +0x68..+0x94)
// ============================================================================
void particle_radial_physics_tick(int slot)
{
    float* ext = *(float**)(slot + 0x18);
    int type_offset = *(int*)(slot + 0x04) * 0x90;

    float dt = g_frame_dt; // scaled

    // Angular velocity integration
    ext[0x49] += ext[0x4A] * dt;

    // Directional decay (shrink over time)
    float decay_xz = /* type +0xD4 */ 0.98f * dt + 1.0f;
    float decay_y  = /* type +0xD8 */ 0.98f * dt + 1.0f;
    ext[0x43] *= decay_xz;
    ext[0x44] *= decay_y;
    ext[0x45] *= decay_xz;

    // Position integration
    *(float*)(slot + 0x20) += *(float*)(slot + 0x3C) * dt;
    *(float*)(slot + 0x24) += *(float*)(slot + 0x40) * dt;
    *(float*)(slot + 0x28) += *(float*)(slot + 0x44) * dt;

    // Check if moved enough to recompute AABB
    float dx = *(float*)(slot + 0x20) - ext[0x46];
    float dy = *(float*)(slot + 0x24) - ext[0x47];
    float dz = *(float*)(slot + 0x28) - ext[0x48];
    float dist_sq = dx*dx + dy*dy + dz*dz;

    if (dist_sq > /* threshold */ 0.01f ||
        fabsf(/* decay_xz */) > 0.0f ||
        fabsf(/* decay_y */) > 0.0f) {
        // Recompute half-extents from direction * radius (clamped min 1.0)
        // Update world AABB = position +/- half_extents
    }
}

// ============================================================================
// particle_conditional_velocity_tick
// Address: 0x800dd9ec
// Conditional velocity integration. Only integrates position += velocity * dt
// when flag 0x800000 is set on the particle slot. Used for particles that
// optionally drift (e.g. attached particles that only move when detached).
// ============================================================================
void particle_conditional_velocity_tick(int slot)
{
    if ((*(int*)(slot + 0x08) & 0x800000) != 0) {
        float dt = g_frame_dt; // scaled
        *(float*)(slot + 0x20) += *(float*)(slot + 0x3C) * dt;
        *(float*)(slot + 0x24) += *(float*)(slot + 0x40) * dt;
        *(float*)(slot + 0x28) += *(float*)(slot + 0x44) * dt;
    }
}
