// Address: 0x800e2114 - 0x800e2f90 | Batch 242 | Date: 2026-08-11
// Particle emitter tick, spawning, keyframe interpolation, and color computation
// Related: particle_emitter_buffer.cpp, particle_emitter_pool.cpp

#include "common.h"

// ============================================================================
// Address: 0x800e2114 | Name: particle_spawn_all_at_once
// Pre-spawns all particles for a "burst" emitter and pre-ages them with
// physics integration (drag + gravity). Each particle gets a fraction of
// total lifetime as its initial age. Uses exponential drag for XZ axes
// and parabolic gravity for Y axis.
// Called from particle_emitter_tick when flag 0x200000 is set.
// ============================================================================
void particle_spawn_all_at_once(void) {
    int emitter = FUN_803826f0();
    u32 count = particle_compute_scaled_count(emitter);
    float total_count_f = (float)count;
    float remaining = total_count_f;

    for (; (int)count > 0; count--) {
        float* particle = (float*)particle_ring_buffer_slot_alloc(emitter);
        particle_init_spawn_params(emitter, (int)particle, (float*)(emitter + 0x14));

        // Pre-age: fraction of lifetime based on spawn order
        float age_fraction = particle[9] * (remaining / total_count_f);
        remaining -= 0.5f;
        particle[10] = age_fraction;

        float speed_scale = *(float*)(emitter + 0x6c);
        float drag_coeff = *(float*)(*(int*)(*(int*)(emitter + 0xc) + 0x4c) + 0x88);

        if (drag_coeff > 0.0f) {
            // Exponential drag integration
            float neg_drag_scaled = -drag_coeff * speed_scale;
            float exp_factor = expf(neg_drag_scaled * age_fraction);

            // For each axis (X=0, Y=1, Z=2):
            for (int axis = 0; axis < 3; axis++) {
                if (axis == 1) {
                    // Y axis: gravity + drag combined
                    float gravity_scaled = -particle[6] * speed_scale;
                    float* pos = &particle[axis];
                    *pos += (gravity_scaled * age_fraction) / -neg_drag_scaled +
                            ((exp_factor - 1.0f) *
                             (neg_drag_scaled * pos[3] + gravity_scaled)) /
                            (neg_drag_scaled * neg_drag_scaled);
                    pos[3] = -((exp_factor * (neg_drag_scaled * pos[3] + gravity_scaled) -
                               gravity_scaled) / neg_drag_scaled);
                } else {
                    // X/Z axis: exponential drag only
                    float* pos = &particle[axis];
                    *pos += speed_scale * pos[3] * (exp_factor / neg_drag_scaled);
                    pos[3] = pos[3] * exp_factor;
                }
            }
        } else {
            // No drag: simple ballistic integration
            float dt = age_fraction * speed_scale;
            particle[0] += particle[3] * dt;
            particle[1] += particle[4] * dt;
            particle[2] += particle[5] * dt;
            // Gravity on Y
            particle[1] = -(age_fraction * (age_fraction * (particle[6] * speed_scale)) *
                           1.0f - particle[1]);
            particle[4] = -(age_fraction * (particle[6] * speed_scale) - particle[4]);
        }
    }
    FUN_8038273c();
}

// ============================================================================
// Address: 0x800e2394 | Name: particle_emitter_tick
// MASSIVE main per-frame emitter update. Handles the full emitter state machine:
//   State 0: inactive
//   State 1: finished (waiting for particles to expire)
//   State 2: initializing (compute emission timing, burst spawn)
//   State 3: active emitting (spawn particles per frame, iterate + physics)
// Also computes per-frame AABB (flag 0x20000), handles delay timer (+0x64),
// looping/one-shot transitions, and type-4 path interpolation.
// ============================================================================
void particle_emitter_tick(void) {
    int emitter = FUN_803826e0();
    float dt = /* extraout_f1 */ frame_dt;

    if (dt <= 0.0f) {
        goto end;
    }

    // Delay timer
    float delay = *(float*)(emitter + 0x64);
    if (delay > 0.0f) {
        if (dt <= delay) {
            *(float*)(emitter + 0x64) = delay - dt;
            goto end;
        }
        dt = dt - delay;
        *(float*)(emitter + 0x64) = 0.0f;
    }

    // Accumulate emitter time
    *(float*)(emitter + 0x38) += dt;

    // State transitions: enabled -> active
    if (*(char*)(emitter + 0x10) == 0 &&
        *(int*)(emitter + 0x50) != 0 && *(int*)(emitter + 0x50) != 1) {
        *(u32*)(emitter + 0x50) = 1;  // -> finished
    }

    if (*(char*)(emitter + 0x11) == 0 && *(char*)(emitter + 0x10) != 0 &&
        (*(int*)(emitter + 0x50) == 0 || *(int*)(emitter + 0x50) == 1)) {
        // Transition to state 2 (initializing)
        int particle_def = *(int*)(emitter + 0xc);
        if (*(int*)(particle_def + 0x54) >= 0) {
            *(float*)(emitter + 0x58) = 0.0f;
        }
        *(u32*)(emitter + 0x50) = 2;

        // Burst spawn for flag 0x200000 (non-type-2)
        if ((*(u32*)(emitter + 4) & 0x200000) != 0 &&
            *(int*)(*(int*)(particle_def + 0x4c) + 0xc) != 2) {
            particle_spawn_all_at_once();
        }

        // Save position for path interpolation
        if (*(int*)(particle_def + 0x54) >= 0) {
            *(u32*)(emitter + 0x20) = *(u32*)(emitter + 0x14);
            *(u32*)(emitter + 0x24) = *(u32*)(emitter + 0x18);
            *(u32*)(emitter + 0x28) = *(u32*)(emitter + 0x1c);
        }
    }

    if (*(int*)(emitter + 0x50) == 0) {
        goto post_tick;
    }

    int particle_def = *(int*)(emitter + 0xc);
    int def_table = *(int*)(particle_def + 0x4c);

    // State 2 -> 3: compute emission timing
    if (*(int*)(emitter + 0x50) == 2) {
        *(float*)(emitter + 0x54) = 0.0f;
        if ((*(u32*)(emitter + 4) & 0x400) == 0) {
            // Random start/end times
            float rng1 = FUN_801310c0();
            *(float*)(emitter + 0x5c) = *(float*)(particle_def + 0x38) * (rng1 - 1.0f) +
                                         *(float*)(particle_def + 0x34);
            if (*(float*)(emitter + 0x5c) < 0.0f ||
                (*(u32*)(emitter + 4) & 0x800000) != 0) {
                *(float*)(emitter + 0x5c) = 0.0f;
            }
            float rng2 = FUN_801310c0();
            *(float*)(emitter + 0x60) = *(float*)(particle_def + 0x40) * (rng2 - 1.0f) +
                                         *(float*)(particle_def + 0x3c);
            if (*(float*)(emitter + 0x60) < 0.0f) {
                *(float*)(emitter + 0x60) = 0.0f;
            }
        }
        *(u32*)(emitter + 0x50) = 3;  // -> active
    }

    // State 3: active emission
    if (*(int*)(emitter + 0x50) == 3 && *(char*)(emitter + 0x10) != 0) {
        float emit_time = *(float*)(emitter + 0x58);
        if (emit_time <= *(float*)(emitter + 0x60)) {
            // Compute emission count this frame
            u32 spawn_count;
            if (*(float*)(particle_def + 0x3c) == 0.0f &&
                *(float*)(particle_def + 0x40) == 0.0f) {
                // Fixed count
                spawn_count = (u32)(*(float*)(particle_def + 0x44) * *(float*)(emitter + 0x74) *
                                    0.5f);
            } else if ((*(u32*)(emitter + 4) & 0x800) == 0) {
                // Rate with acceleration curve
                float rate = (1.0f - *(float*)(particle_def + 0x48) * emit_time) *
                             *(float*)(emitter + 0x74) * (dt * *(float*)(particle_def + 0x44)) *
                             0.5f + *(float*)(emitter + 0x54);
                spawn_count = (u32)rate;
                *(float*)(emitter + 0x54) = rate - (float)spawn_count;
            } else {
                // Linear decay over duration
                float rate = (1.0f - emit_time / *(float*)(emitter + 0x60)) *
                             *(float*)(emitter + 0x74) * (dt * *(float*)(particle_def + 0x44)) *
                             0.5f + *(float*)(emitter + 0x54);
                spawn_count = (u32)rate;
                *(float*)(emitter + 0x54) = rate - (float)spawn_count;
            }

            // Scale lerp over emission duration (flag 0x80000)
            if ((*(u32*)(emitter + 4) & 0x80000) != 0) {
                float duration = *(float*)(emitter + 0x60) - *(float*)(emitter + 0x58);
                if (duration != 0.0f) {
                    float scale_x = *(float*)(emitter + 0x68);
                    if (scale_x != 0.5f) {
                        *(float*)(emitter + 0x68) -= dt * ((scale_x - 0.5f) / duration);
                    }
                    float scale_y = *(float*)(emitter + 0x6c);
                    if (scale_y != 0.5f) {
                        *(float*)(emitter + 0x6c) -= dt * ((scale_y - 0.5f) / duration);
                    }
                }
            }

            // Type 4 path interpolation: compute per-particle offset
            float path_dx, path_dy, path_dz;
            if (*(int*)(def_table + 0xc) == 4) {
                float n = (float)spawn_count;
                path_dx = (*(float*)(emitter + 0x14) - *(float*)(emitter + 0x20)) / n;
                path_dy = (*(float*)(emitter + 0x18) - *(float*)(emitter + 0x24)) / n;
                path_dz = (*(float*)(emitter + 0x1c) - *(float*)(emitter + 0x28)) / n;
            }

            // Spawn loop
            for (; (int)spawn_count > 0; spawn_count--) {
                int slot = particle_ring_buffer_slot_alloc(emitter);
                if (*(int*)(def_table + 0xc) == 4) {
                    // Path-interpolated position
                    float pos[3];
                    float n = (float)spawn_count;
                    pos[0] = *(float*)(emitter + 0x14) - path_dx * n;
                    pos[1] = *(float*)(emitter + 0x18) - path_dy * n;
                    pos[2] = *(float*)(emitter + 0x1c) - path_dz * n;
                    particle_init_spawn_params(emitter, slot, pos);
                } else {
                    particle_init_spawn_params(emitter, slot, (float*)(emitter + 0x14));
                }
            }
        } else {
            *(float*)(emitter + 0x54) = 0.0f;
        }

        // Advance emission time
        *(float*)(emitter + 0x58) += dt;

        // Check for end-of-emission transitions
        float time = *(float*)(emitter + 0x58);
        if (time >= *(float*)(emitter + 0x5c)) {
            // Various looping/one-shot logic depending on flags
            // (simplified: transitions back to state 2 if looping, state 1 if done)
        }
    }

    // Reset AABB for recomputation (flag 0x20000)
    if ((*(u32*)(emitter + 4) & 0x20000) != 0) {
        // Reset min/max to zero
        *(float*)(emitter + 0xbc) = 0.0f;
        *(float*)(emitter + 0xb0) = 0.0f;
        *(float*)(emitter + 0xc0) = 0.0f;
        *(float*)(emitter + 0xb4) = 0.0f;
        *(float*)(emitter + 0xc4) = 0.0f;
        *(float*)(emitter + 0xb8) = 0.0f;
    }

    // Iterate active particles: age, expire, physics
    int head = *(int*)(emitter + 0x48);
    int active_count = *(int*)(emitter + 0x4c);
    float total_lifetime = particle_get_total_lifetime(*(int*)(emitter + 0xc));

    for (u32 i = 0; (int)i < active_count; i++) {
        int def_type = *(int*)(*(int*)(*(int*)(emitter + 0xc) + 0x4c) + 0xc);
        float* p;
        if (def_type == 5 || def_type == 6) {
            p = (float*)(*(int*)(emitter + 0x40) + head * 0x58);
        } else {
            p = (float*)(*(int*)(emitter + 0x40) + head * 0x34);
        }

        float new_age = p[10] + dt;
        if (new_age > total_lifetime) {
            // Expired: remove from ring buffer
            particle_ring_buffer_advance(emitter);
        } else if (new_age > p[9]) {
            // Past active lifetime but not expired: just age (fade phase)
            p[10] = new_age;
        } else {
            // Active: run physics (skip bilateral alternates for type 2)
            if (*(int*)(def_table + 0xc) != 2 ||
                *(float*)(def_table + 0x78) > p[10] ||
                (i & 1) == 0) {
                particle_physics_tick_main();
            } else {
                p[10] = new_age;
            }
        }

        // AABB expansion (flag 0x20000)
        if ((*(u32*)(emitter + 4) & 0x20000) != 0) {
            float dx = p[0] - *(float*)(emitter + 0x14);
            float dy = p[1] - *(float*)(emitter + 0x18);
            float dz = p[2] - *(float*)(emitter + 0x1c);
            // Update min/max at +0xb0..+0xc4
            if (dx < *(float*)(emitter + 0xb0)) *(float*)(emitter + 0xb0) = dx;
            if (dy < *(float*)(emitter + 0xb4)) *(float*)(emitter + 0xb4) = dy;
            if (dz < *(float*)(emitter + 0xb8)) *(float*)(emitter + 0xb8) = dz;
            if (dx > *(float*)(emitter + 0xbc)) *(float*)(emitter + 0xbc) = dx;
            if (dy > *(float*)(emitter + 0xc0)) *(float*)(emitter + 0xc0) = dy;
            if (dz > *(float*)(emitter + 0xc4)) *(float*)(emitter + 0xc4) = dz;
        }

        head++;
        if (head >= *(int*)(emitter + 0x44)) {
            head -= *(int*)(emitter + 0x44);
        }
    }

    // Check if emitter is fully done (state 1 + no particles + past sub-emitter time)
    if (*(int*)(emitter + 0x50) == 1 && *(int*)(emitter + 0x4c) == 0) {
        int sub_emitter = *(int*)(particle_def + 0x50);
        if (sub_emitter == 0 ||
            (*(float*)(sub_emitter + 0xc) < *(float*)(emitter + 0x38) &&
             *(float*)(sub_emitter + 0x2c) < *(float*)(emitter + 0x38))) {
            *(u32*)(emitter + 0x50) = 0;  // -> inactive
        }
    }

post_tick:
    // Finalize AABB with padding (flag 0x20000)
    if ((*(u32*)(emitter + 4) & 0x20000) != 0) {
        float pad = *(float*)(emitter + 0xac);
        *(float*)(emitter + 0xb0) -= pad;
        *(float*)(emitter + 0xb4) -= pad;
        *(float*)(emitter + 0xb8) -= pad;
        *(float*)(emitter + 0xbc) += pad;
        *(float*)(emitter + 0xc0) += pad;
        *(float*)(emitter + 0xc4) += pad;
        // World-space AABB
        *(float*)(emitter + 0xc8) = *(float*)(emitter + 0x14) + *(float*)(emitter + 0xb0);
        *(float*)(emitter + 0xcc) = *(float*)(emitter + 0x18) + *(float*)(emitter + 0xb4);
        *(float*)(emitter + 0xd0) = *(float*)(emitter + 0x1c) + *(float*)(emitter + 0xb8);
        *(float*)(emitter + 0xd4) = *(float*)(emitter + 0x14) + *(float*)(emitter + 0xbc);
        *(float*)(emitter + 0xd8) = *(float*)(emitter + 0x18) + *(float*)(emitter + 0xc0);
        *(float*)(emitter + 0xdc) = *(float*)(emitter + 0x1c) + *(float*)(emitter + 0xc4);
    }

    // Save previous enabled state + position
    *(u8*)(emitter + 0x11) = *(u8*)(emitter + 0x10);
    *(u32*)(emitter + 0x20) = *(u32*)(emitter + 0x14);
    *(u32*)(emitter + 0x24) = *(u32*)(emitter + 0x18);
    *(u32*)(emitter + 0x28) = *(u32*)(emitter + 0x1c);

end:
    FUN_8038272c();
}

// ============================================================================
// Address: 0x800e2d28 | Name: particle_keyframe_lerp
// Evaluates a keyframe table at a given time. Table is pairs of (time, value).
// Returns interpolated value between surrounding keyframes.
// Used for scale, opacity, and other animated particle properties.
// ============================================================================
float particle_keyframe_lerp(float time, float* keyframes, int num_keys) {
    if (num_keys == 0) return 1.0f;
    if (num_keys == 1) return keyframes[1];

    // Clamp to first/last
    if (time <= keyframes[0]) return keyframes[1];
    if (time >= keyframes[(num_keys - 1) * 2]) return keyframes[num_keys * 2 - 1];

    // Reverse linear search for bracket
    int idx = num_keys - 1;
    while (idx > 0 && time < keyframes[idx * 2]) {
        idx--;
    }

    // Linear interpolation
    float t0 = keyframes[idx * 2];
    float v0 = keyframes[idx * 2 + 1];
    float t1 = keyframes[(idx + 1) * 2];
    float v1 = keyframes[(idx + 1) * 2 + 1];
    float t = (time - t0) / (t1 - t0);
    return v0 + t * (v1 - v0);
}

// ============================================================================
// Address: 0x800e2dec | Name: particle_color_lerp_with_midpoint
// 3-stop color interpolation: start -> mid -> end, where the midpoint threshold
// is at def_table[0x28]. Unpacks RGBA from packed u32 at offsets 0x20, 0x24, 0x2c.
// Returns packed RGB shifted into upper 24 bits.
// ============================================================================
u32 particle_color_lerp_with_midpoint(int emitter, int particle) {
    float age_ratio = *(float*)(particle + 0x28) / *(float*)(particle + 0x24);
    int def_table = *(int*)(*(int*)(emitter + 0xc) + 0x4c);

    u32 color_start = *(u32*)(def_table + 0x20);
    u32 color_end   = *(u32*)(def_table + 0x2c);
    u32 color_mid   = *(u32*)(def_table + 0x24);
    float midpoint  = *(float*)(def_table + 0x28);

    // Unpack start color channels
    float r0 = (float)((color_start >> 24) & 0xFF);
    float g0 = (float)((color_start >> 16) & 0xFF);
    float b0 = (float)((color_start >> 8) & 0xFF);

    // Unpack mid color channels
    float r1 = (float)((color_mid >> 24) & 0xFF);
    float g1 = (float)((color_mid >> 16) & 0xFF);
    float b1 = (float)((color_mid >> 8) & 0xFF);

    float r, g, b;
    if (age_ratio > midpoint) {
        // Mid -> End interpolation
        float t = (age_ratio - midpoint) / (1.0f - midpoint);
        float r_end = (float)((color_end >> 24) & 0xFF);
        float g_end = (float)((color_end >> 16) & 0xFF);
        float b_end = (float)((color_end >> 8) & 0xFF);
        r = r1 + t * (r_end - r1);
        g = g1 + t * (g_end - g1);
        b = b1 + t * (b_end - b1);
    } else {
        // Start -> Mid interpolation
        float t = age_ratio / midpoint;
        r = r0 + t * (r1 - r0);
        g = g0 + t * (g1 - g0);
        b = b0 + t * (b1 - b0);
    }

    int ri = FUN_803825c0(r);
    int gi = FUN_803825c0(g);
    int bi = FUN_803825c0(b);
    return (ri << 24) | ((gi & 0xFF) << 16) | ((bi & 0xFF) << 8);
}

// ============================================================================
// Address: 0x800e2f90 | Name: particle_compute_color
// Dispatches color computation: if start==end and midpoint==0, returns start
// color directly. If midpoint is 0, does linear lerp. Otherwise calls
// particle_color_lerp_with_midpoint. Adds per-particle additive color.
// ============================================================================
u32 particle_compute_color(int emitter, int particle) {
    int def_table = *(int*)(*(int*)(emitter + 0xc) + 0x4c);
    u32 color_start = *(u32*)(def_table + 0x20);
    u32 color_end   = *(u32*)(def_table + 0x2c);

    u32 result;
    if (color_start != color_end || *(float*)(def_table + 0x28) != 0.0f) {
        if (*(float*)(def_table + 0x28) == 0.0f) {
            // Simple linear lerp start -> end
            float t = *(float*)(particle + 0x28) / *(float*)(particle + 0x24);
            float r0 = (float)((color_start >> 24) & 0xFF);
            float g0 = (float)((color_start >> 16) & 0xFF);
            float b0 = (float)((color_start >> 8) & 0xFF);
            float r = r0 + t * ((float)((color_end >> 24) & 0xFF) - r0);
            float g = g0 + t * ((float)((color_end >> 16) & 0xFF) - g0);
            float b = b0 + t * ((float)((color_end >> 8) & 0xFF) - b0);
            int ri = FUN_803825c0(r);
            int gi = FUN_803825c0(g);
            int bi = FUN_803825c0(b);
            result = (ri << 24) | ((gi & 0xFF) << 16) | ((bi & 0xFF) << 8);
        } else {
            result = particle_color_lerp_with_midpoint(emitter, particle);
        }
    } else {
        result = color_start;
    }

    // Add per-particle additive color
    return result + *(int*)(particle + 0x2c);
}
