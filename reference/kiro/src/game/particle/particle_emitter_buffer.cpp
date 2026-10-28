// Address: 0x800e1094 - 0x800e2028 | Batch 241 | Date: 2026-08-11
// Particle emitter ring buffer management, initialization, and physics tick
// Related: particle_spawn_position_generate (0x800e0a68), particle_system_core.cpp

#include "common.h"

// ============================================================================
// Address: 0x800e1094 | Name: particle_buffer_free
// Free the particle ring buffer allocation and reset buffer state.
// Called from particle_buffer_alloc (re-alloc path) and externally on teardown.
// ============================================================================
void particle_buffer_free(int emitter) {
    if (emitter == 0) {
        return;
    }
    if (*(int*)(emitter + 0x40) != 0) {
        block_free_simple(*(int*)(emitter + 0x40));
        if ((*(u32*)(emitter + 4) & 1) != 0) {
            *(u32*)(emitter + 0x50) = 1;  // mark dirty flag
        }
    }
    *(u32*)(emitter + 0x40) = 0;  // buffer ptr
    *(u32*)(emitter + 0x44) = 0;  // capacity
    *(u32*)(emitter + 0x48) = 0;  // head index
    *(u32*)(emitter + 0x4c) = 0;  // count
}

// ============================================================================
// Address: 0x800e10f8 | Name: particle_get_total_lifetime
// Returns the sum of min_lifetime and lifetime_range from the particle def table.
// ============================================================================
float particle_get_total_lifetime(int particle_def) {
    int def_table = *(int*)(particle_def + 0x4c);
    return *(float*)(def_table + 4) + *(float*)(def_table + 8);
}

// ============================================================================
// Address: 0x800e110c | Name: particle_compute_max_count
// Computes the maximum number of active particles based on emission rate,
// lifetime, and speed scale. Doubles for type 2 (bilateral) particles.
// ============================================================================
u32 particle_compute_max_count(int emitter, int particle_def) {
    float lifetime = particle_get_total_lifetime(particle_def);
    float speed_scale = *(float*)(particle_def + 0x44) * *(float*)(emitter + 0x74);
    u32 count = (u32)(speed_scale * 0.5f * lifetime + 0.5f);

    // Round up on odd parity
    if ((count & 1 ^ count >> 31) != count >> 31) {
        count = count + 1;
    }

    // If both range values are zero, use raw emission rate
    if (0.0f == *(float*)(particle_def + 0x3c) &&
        0.0f == *(float*)(particle_def + 0x40)) {
        count = (u32)*(float*)(particle_def + 0x44);
    }

    // Type 2 = bilateral, double the count
    int def_type = *(int*)(*(int*)(particle_def + 0x4c) + 0xc);
    if (def_type == 2) {
        count = count << 1;
    }
    return count;
}

// ============================================================================
// Address: 0x800e1218 | Name: particle_buffer_alloc
// Allocates the particle ring buffer. Type 5/6 particles use 0x58 bytes per
// entry (oriented/trail), others use 0x34 bytes (billboard/point).
// Capacity is capped at 500 particles.
// ============================================================================
void particle_buffer_alloc(void) {
    int emitter = FUN_803826f0();  // get current emitter context
    if (emitter == 0) {
        goto end;
    }

    int particle_def = *(int*)(emitter + 0xc);
    if (particle_def == 0) {
        goto end;
    }

    particle_buffer_free(emitter);
    u32 count = particle_compute_max_count(emitter, particle_def);

    // Cap at 500
    u32 capacity = 500;
    if ((int)count < 500) {
        capacity = count;
    }

    int def_type = *(int*)(*(int*)(particle_def + 0x4c) + 0xc);
    if (def_type == 5 || def_type == 6) {
        // Oriented/trail particles: 0x58 bytes each
        u32* buf = block_alloc_dispatch(capacity * 0x58, 1, emitter, particle_buffer_free);
        *(u32**)(emitter + 0x40) = buf;
    } else {
        // Billboard/point particles: 0x34 bytes each
        u32* buf = block_alloc_dispatch(capacity * 0x34, 1, emitter, particle_buffer_free);
        *(u32**)(emitter + 0x40) = buf;
    }
    *(u32*)(emitter + 0x44) = capacity;

end:
    FUN_8038273c();  // release context
}

// ============================================================================
// Address: 0x800e12d4 | Name: particle_emitter_init_from_def
// Initializes an emitter instance from a particle definition table entry.
// Copies position offsets, velocity ranges, and resets state flags.
// ============================================================================
void particle_emitter_init_from_def(int emitter, int def) {
    *(int*)(emitter + 0xc) = def;
    *(u32*)(emitter + 0x40) = 0;  // clear buffer
    *(u32*)(emitter + 4) = *(u32*)(def + 4);  // flags

    // Position offset (accumulate from def)
    *(float*)(emitter + 0x14) += *(float*)(def + 0x08);
    *(float*)(emitter + 0x18) += *(float*)(def + 0x0c);
    *(float*)(emitter + 0x1c) += *(float*)(def + 0x10);

    // Velocity range
    *(float*)(emitter + 0x2c) += *(float*)(def + 0x14);
    *(float*)(emitter + 0x30) += *(float*)(def + 0x18);
    *(float*)(emitter + 0x34) += *(float*)(def + 0x1c);

    // Copy position to "current position" fields
    *(u32*)(emitter + 0x20) = *(u32*)(emitter + 0x14);
    *(u32*)(emitter + 0x24) = *(u32*)(emitter + 0x18);
    *(u32*)(emitter + 0x28) = *(u32*)(emitter + 0x1c);

    // State flags
    *(u8*)(emitter + 0x12) = 1;  // active
    *(u8*)(emitter + 0x10) = 1;  // enabled
    *(u8*)(emitter + 0x11) = 0;  // not paused
    *(u32*)(emitter + 0x50) = 2; // state = ready

    // Scale factors (all 0.5)
    float half = 0.5f;
    *(float*)(emitter + 0x68) = half;
    *(float*)(emitter + 0x6c) = half;
    *(float*)(emitter + 0x70) = half;
    *(float*)(emitter + 0x74) = half;
}

// ============================================================================
// Address: 0x800e1390 | Name: particle_ring_buffer_advance
// Advances the ring buffer head by one slot, wrapping at capacity.
// Decrements the active particle count.
// ============================================================================
void particle_ring_buffer_advance(int emitter) {
    *(int*)(emitter + 0x48) += 1;  // head++
    if (*(int*)(emitter + 0x44) <= *(int*)(emitter + 0x48)) {
        *(int*)(emitter + 0x48) -= *(int*)(emitter + 0x44);  // wrap
    }
    *(int*)(emitter + 0x4c) -= 1;  // count--
}

// ============================================================================
// Address: 0x800e13c4 | Name: particle_ring_buffer_slot_alloc
// Returns a pointer to the next available particle slot in the ring buffer.
// If buffer is not allocated, triggers allocation. Returns 0 if alloc fails.
// When buffer is full, overwrites oldest (head) and advances head.
// ============================================================================
int particle_ring_buffer_slot_alloc(int emitter) {
    // Lazy allocation
    if (*(int*)(emitter + 0x40) == 0) {
        particle_buffer_alloc();
        if (*(int*)(emitter + 0x40) == 0) {
            return 0;
        }
    }

    int capacity = *(int*)(emitter + 0x44);
    int slot_ptr;

    if (*(int*)(emitter + 0x4c) < capacity) {
        // Buffer not full: compute tail index
        int tail = *(int*)(emitter + 0x48) + *(int*)(emitter + 0x4c);
        while (tail >= capacity) {
            tail -= capacity;
        }

        int def_type = *(int*)(*(int*)(*(int*)(emitter + 0xc) + 0x4c) + 0xc);
        if (def_type == 5 || def_type == 6) {
            slot_ptr = *(int*)(emitter + 0x40) + tail * 0x58;
        } else {
            slot_ptr = *(int*)(emitter + 0x40) + tail * 0x34;
        }
        *(int*)(emitter + 0x4c) += 1;  // count++
    } else {
        // Buffer full: overwrite head slot
        *(int*)(emitter + 0x4c) = capacity;

        int def_type = *(int*)(*(int*)(*(int*)(emitter + 0xc) + 0x4c) + 0xc);
        if (def_type == 5 || def_type == 6) {
            slot_ptr = *(int*)(emitter + 0x40) + *(int*)(emitter + 0x48) * 0x58;
        } else {
            slot_ptr = *(int*)(emitter + 0x40) + *(int*)(emitter + 0x48) * 0x34;
        }

        // Advance head
        *(int*)(emitter + 0x48) += 1;
        if (*(int*)(emitter + 0x44) <= *(int*)(emitter + 0x48)) {
            *(u32*)(emitter + 0x48) = 0;
        }
    }
    return slot_ptr;
}

// ============================================================================
// Address: 0x800e14e8 | Name: particle_init_spawn_params
// MASSIVE particle spawn initialization. Sets up position (from emitter shape),
// velocity (with RNG spread), color (with channel randomization), lifetime,
// scale, and for types 5/6: direction vectors + cross products for oriented
// rendering. Called from particle_spawn_position_generate callers.
// ============================================================================
void particle_init_spawn_params(u32 param_1, u32 param_2, float* position) {
    // Uses paired internal context: emitter + particle slot
    u64 ctx = FUN_803826e8();  // get emitter + slot pair
    int emitter = (int)(ctx >> 32);
    u32 slot = (u32)ctx;
    int particle_def = *(int*)(emitter + 0xc);
    u32* def_table = *(u32**)(particle_def + 0x4c);

    // Generate spawn position from emitter shape
    particle_spawn_position_generate(emitter, slot, position);

    // Lifetime: base + random * range
    float rng = FUN_801310c0();  // random [0,1)
    *(float*)(slot + 0x24) = (float)def_table[2] * 0.5f * (rng - 1.0f) + (float)def_table[1];

    // Flag-based lifetime scaling
    if ((*(u32*)(emitter + 4) & 0x1000000) != 0) {
        *(float*)(slot + 0x24) *= 1.0f;  // identity (multiplayer scale placeholder)
    }
    if ((*(u32*)(emitter + 4) & 0x2000000) != 0) {
        *(float*)(slot + 0x24) *= FLOAT_8061504c;  // difficulty scale
    }

    // Age = 0
    *(float*)(slot + 0x28) = 0.0f;

    // Velocity direction: spherical from RNG
    float rng1 = FUN_801310c0();
    float rng2 = FUN_801310c0();
    float rng3 = FUN_801310c0();
    float cone_angle = *(float*)(particle_def + 0x28);
    float vertical_bias = 1.0f - *(float*)(particle_def + 0x2c);
    *(float*)(slot + 0x30) = 1.0f - (rng3 * vertical_bias + rng2 * cone_angle);

    // Convert spherical direction to velocity vector via FUN_800e6d88
    float elevation = *(float*)(*(int*)(emitter + 0xc) + 0x30);
    float floor_bias = 0.0f;
    if ((*(u32*)(emitter + 4) & 0x4000000) != 0) {
        floor_bias = FLOAT_80615050 - elevation;
    }
    FUN_800e6d88(elevation,
                 (1.0f - (rng1 * (1.0f - cone_angle) + rng2 * cone_angle)),
                 floor_bias,
                 (float*)(emitter + 0x2c),
                 (float*)(slot + 0xc));

    // Speed: base + random * range, scaled by emitter speed
    float speed = ((rng2 - 1.0f) * *(float*)(particle_def + 0x24) +
                   *(float*)(particle_def + 0x20)) * *(float*)(emitter + 0x6c);
    *(float*)(slot + 0x0c) *= speed;
    *(float*)(slot + 0x10) *= speed;
    *(float*)(slot + 0x14) *= speed;

    // Scale: random range for types != 2
    if (def_table[3] != 2) {
        float rng4 = FUN_801310c0();
        *(float*)(slot + 0x1c) = (float)def_table[0x1f] * (rng4 - 1.0f) + (float)def_table[0x1e];
        float rng5 = FUN_801310c0();
        *(float*)(slot + 0x20) = (float)def_table[0x21] * (rng5 - 1.0f) + (float)def_table[0x20];
    }

    // Color: RGBA channel randomization
    // (complex bit unpacking from def_table[0xc] with random modulation)
    u32 color_packed = def_table[0xc];
    if (color_packed == 0 || (def_table[0] & 0x20) != 0) {
        // No color or forced random
        if (color_packed == 0) {
            *(u32*)(slot + 0x2c) = 0;
        } else {
            // Per-channel random scale
            float rng_r = FUN_801310c0();
            int r = FUN_803825c0((float)((color_packed >> 24) & 0xFF) * rng_r);
            float rng_g = FUN_801310c0();
            int g = FUN_803825c0((float)((color_packed >> 16) & 0xFF) * rng_g);
            float rng_b = FUN_801310c0();
            int b = FUN_803825c0((float)((color_packed >> 8) & 0xFF) * rng_b);
            *(int*)(slot + 0x2c) = (r << 24) | (g << 16) | (b << 8);
        }
    } else {
        // Uniform random intensity applied to all channels
        float rng_uniform = FUN_801310c0();
        int g = FUN_803825c0((float)((color_packed >> 16) & 0xFF) * rng_uniform);
        int r = FUN_803825c0((float)((color_packed >> 24) & 0xFF) * rng_uniform);
        int b = FUN_803825c0((float)((color_packed >> 8) & 0xFF) * rng_uniform);
        *(int*)(slot + 0x2c) = (r << 24) | (g << 16) | (b << 8);
    }

    // Type 2: copy from ring buffer slot (bilateral mirror)
    if (def_table[3] == 2) {
        u32 mirror_slot = particle_ring_buffer_slot_alloc(emitter);
        leaf_31e8(mirror_slot, slot, 0x34);  // memcpy 0x34 bytes
    }

    FUN_80382734();  // release context
}

// ============================================================================
// Address: 0x800e1a90 | Name: particle_physics_tick_main
// MASSIVE per-particle physics update. Handles:
// - Sin-table oscillation (flag 0x8 on def)
// - External force accumulation (flag 0x20004000)
// - Position integration (pos += vel * dt)
// - Drag/damping (vel *= 1 - drag*dt)
// - Random jitter per axis
// - Age advancement
// - Angular velocity rotation for types 5/6 (quaternion via matrix)
// - Floor bounce with restitution (flag 0x4, Y < floor_height)
//   - Resets orientation on bounce for types 5/6
//   - Optionally resets age on bounce (flag 0x10)
//   - Calls FUN_800e512c on bounce with bounce sound flag (0x8)
// ============================================================================
void particle_physics_tick_main(void) {
    u64 ctx = FUN_803826f0();  // get emitter + particle pair
    int emitter = (int)(ctx >> 32);
    float* particle = (float*)(u32)ctx;
    u32* def_table = *(u32**)(*(int*)(emitter + 0xc) + 0x4c);
    float dt = /* extraout_f1 */ particle_frame_dt * *(float*)(emitter + 0x6c);

    // Sin-table oscillation (flag 0x8)
    if ((def_table[0] & 8) != 0) {
        float freq = particle[0xc] * (float)def_table[0x19] + (float)def_table[0x18];
        float phase_speed = FLOAT_806107ec *
            (particle[7] + particle[10] / ((float)def_table[1] + (float)def_table[2]));
        u32 phase_x = (u32)(FLOAT_80615060 * (particle[8] * particle[10] + particle[7]) + 1.0f);
        u32 phase_y = (u32)(FLOAT_80615060 * phase_speed + 1.0f);
        u32 phase_z = (u32)(FLOAT_80615060 * 0.5f * phase_speed + 1.0f);

        float amplitude = FLOAT_806107e8 * freq * freq * particle[6] *
                          sin_table[phase_y & 0x7ff];
        particle[3] += amplitude * sin_table[phase_x & 0x7ff];
        particle[4] += FLOAT_806107f0 * (1.0f * sin_table[phase_z & 0x7ff] + 1.0f);
        particle[5] += amplitude * cos_table[(phase_x + 0x200) & 0x7ff];
    }

    // External force (gravity/wind from emitter global)
    if ((*(u32*)(emitter + 4) & 0x20004000) != 0) {
        particle[3] += *(float*)(emitter + 0x98);
        particle[4] += *(float*)(emitter + 0x9c);
        particle[5] += *(float*)(emitter + 0xa0);
    }

    // Position integration: pos += vel * dt
    particle[0] += particle[3] * dt;
    particle[1] += particle[4] * dt;
    particle[2] += particle[5] * dt;

    // Random jitter per axis
    float rng1 = FUN_801310c0();
    particle[3] += (float)def_table[0x26] * dt * (rng1 - 1.0f);
    // Gravity: vel_y = -(gravity * dt - vel_y)
    particle[4] = -(particle[6] * dt - particle[4]);

    float rng2 = FUN_801310c0();
    particle[4] += dt * ((float)def_table[0x26] * (rng2 - 1.0f));
    particle[4] *= -(((float)def_table[0x22] * dt) - 1.0f);  // drag

    float rng3 = FUN_801310c0();
    particle[5] += dt * ((float)def_table[0x26] * (rng3 - 1.0f));
    particle[5] *= -(((float)def_table[0x22] * dt) - 1.0f);  // drag

    // Age advancement
    particle[10] += dt;

    // Remove external force after integration
    if ((*(u32*)(emitter + 4) & 0x4000) != 0) {
        particle[3] -= *(float*)(emitter + 0x98);
        particle[4] -= *(float*)(emitter + 0x9c);
        particle[5] -= *(float*)(emitter + 0xa0);
    }

    // Angular velocity rotation for types 5/6 (oriented particles)
    if ((def_table[3] == 5 || def_table[3] == 6) &&
        (*(u32*)(emitter + 4) & 0x8000) == 0) {
        float ang_x = particle[0x13];
        float ang_y = particle[0x14];
        float ang_z = particle[0x15];
        float mag_sq = ang_x * ang_x + ang_y * ang_y + ang_z * ang_z;
        if (mag_sq > 0.0f) {
            float mag = FUN_80131538(mag_sq);  // sqrt
            float inv_mag = 1.0f / mag;
            float axis_x = ang_x * inv_mag;
            float axis_y = ang_y * inv_mag;
            float axis_z = ang_z * inv_mag;
            // Build rotation matrix from axis-angle
            float angle = mag * dt;
            float rot_matrix[17];
            FUN_8013025c(angle, rot_matrix, &axis_x);
            // Apply rotation to direction vectors
            FUN_8012fb90(rot_matrix, particle + 0xd);   // forward
            FUN_8012fb90(rot_matrix, particle + 0x10);  // right
        }
    }

    // Floor bounce
    if ((*(u32*)(emitter + 4) & 4) != 0) {
        float floor_y = FLOAT_80615064 + *(float*)(emitter + 0x84);
        if (particle[1] < floor_y && dt > 0.0f) {
            particle[1] = floor_y;
            float restitution = 1.0f - (float)def_table[0x23];
            particle[4] = -particle[4] * restitution;
            particle[3] *= restitution;
            particle[5] *= restitution;

            // Reset orientation on bounce for types 5/6
            if ((def_table[3] == 5 || def_table[3] == 6) &&
                fabsf(particle[4]) < FLOAT_80615064) {
                // Reset to identity-ish orientation
                particle[0xd] = 0.0f;
                particle[0x11] = 0.0f;
                particle[0xf] = 0.0f;
                particle[0x12] = 0.0f;
                // Re-normalize right vector
                float ry = particle[0x10];
                float rmag = ry * ry + particle[0x11] * particle[0x11] +
                             particle[0x12] * particle[0x12];
                if (rmag > FLOAT_8061503c) {
                    // Fast inverse sqrt (Newton-Raphson)
                    float inv = 1.0f / sqrtf(rmag);
                    particle[0x10] = ry * inv;
                    particle[0x11] *= inv;
                    particle[0x12] *= inv;
                }
                // Re-normalize forward vector
                float fy = particle[0xd];
                float fmag = fy * fy + particle[0xe] * particle[0xe] +
                             particle[0xf] * particle[0xf];
                if (fmag > FLOAT_8061503c) {
                    float inv2 = 1.0f / sqrtf(fmag);
                    particle[0xd] = fy * inv2;
                    particle[0xe] *= inv2;
                    particle[0xf] *= inv2;
                }
                // Kill angular velocity and Y velocity
                particle[0x13] = 0.0f;
                particle[0x14] = 0.0f;
                particle[0x15] = 0.0f;
                particle[4] = 0.0f;
            }

            // Partial age reset on bounce (flag 0x10)
            if ((def_table[0] & 0x10) != 0) {
                particle[10] = 1.0f * (particle[9] - particle[10]) + particle[10];
            }

            // Bounce sound/effect callback (flag 0x8 on emitter)
            if ((*(u32*)(emitter + 4) & 8) != 0) {
                FUN_800e512c(emitter, particle);
            }
        }
    }

    FUN_8038273c();  // release context
}

// ============================================================================
// Address: 0x800e2028 | Name: particle_compute_scaled_count
// Computes particle count scaled by aspect ratio. Adjusts for narrow/wide
// aspect by clamping emission when width < height.
// ============================================================================
u32 particle_compute_scaled_count(int emitter) {
    int particle_def = *(int*)(emitter + 0xc);
    float base_rate = *(float*)(particle_def + 0x44) * *(float*)(emitter + 0x74) * 0.5f;

    // Aspect ratio: (range_mid_x, range_mid_y)
    float mid_x = *(float*)(particle_def + 0x38) * 1.0f + *(float*)(particle_def + 0x34);
    float mid_y = *(float*)(particle_def + 0x40) * 1.0f + *(float*)(particle_def + 0x3c);

    // If Y < X and Y < 1.0 and X > 0: scale down by ratio
    if (mid_y < mid_x && mid_y < 1.0f && mid_x > 0.0f) {
        base_rate *= (mid_y / mid_x);
    }

    // Multiply by total lifetime
    float lifetime = *(float*)(*(int*)(particle_def + 0x4c) + 8) * 1.0f +
                     *(float*)(*(int*)(particle_def + 0x4c) + 4);
    u32 count = (u32)(base_rate * lifetime + 0.5f);

    // Round up on odd parity
    if ((count & 1 ^ count >> 31) != count >> 31) {
        count = count + 1;
    }
    return count;
}
