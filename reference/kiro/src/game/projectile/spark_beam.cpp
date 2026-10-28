// Address: 0x80102bc0 - 0x80103460 | Spark Pool + Beam Trail System | Date: 2026-08-26
// Batch 321: Impact spark billboards (circular 200-entry pool) and beam weapon trails
//
// The spark pool is a lightweight billboard particle system for impact sparks,
// ricochet flashes, and weapon FX. It uses a circular buffer of 200 entries
// (0x34 bytes each) at DAT_80575d00, with DAT_80611f68 as the write index.
// Two spawn modes: instant (state=1, single frame) and fading (state=3, lifetime decay).
//
// The beam trail system generates multi-point weapon beam trails for energy weapons.
// Each trail has 10 control points with randomized scatter and per-weapon-type parameters.

#include "common.h"
#include "spark_beam.h"
#include "particle.h"
#include "weather.h"
#include "sprite.h"
#include "math_util.h"

// Pool constants
#define SPARK_POOL_SIZE     200
#define SPARK_ENTRY_SIZE    0x34
#define SPARK_STATE_NONE    0
#define SPARK_STATE_INSTANT 1
#define SPARK_STATE_FADING  3

// Pool globals
// DAT_80575d00: spark_entry_t pool[200]
// DAT_80611f68: int write_index (circular)

// ============================================================================
// Spark Pool - Tick / Lifecycle
// ============================================================================

// Address: 0x80102bc0
// Per-frame spark pool tick. State=1 (instant) entries are cleared immediately.
// State=3 (fading) entries have lifetime decremented and are cleared when expired.
// Called by particle_system_main_tick.
void spark_pool_tick(void) {
    u32 frame_time_raw = DAT_806109c0;
    u32 damage_scale = DAT_806108e4;

    int offset = 0;
    for (int i = 200; i != 0; i--) {
        int* entry = (int*)((int)&DAT_80575d00 + offset);

        // Instant sparks: clear after one frame
        if (*entry == SPARK_STATE_INSTANT) {
            *entry = SPARK_STATE_NONE;
        }

        // Fading sparks: decrement lifetime
        if (*entry == SPARK_STATE_FADING) {
            float* lifetime = (float*)((int)&DAT_80575d20 + offset);
            if (*lifetime <= FLOAT_80615670) {
                *entry = SPARK_STATE_NONE;
            }
            // Subtract frame-time-scaled decay rate
            float decay = (FLOAT_80615674 /
                (float)((double)(0x43300000 | (frame_time_raw ^ 0x80000000)) - DOUBLE_80615678)) *
                (float)((double)(0x43300000 | (damage_scale ^ 0x80000000)) - DOUBLE_80615678);
            *lifetime = -(*lifetime - decay);  // negate to decay towards zero
        }

        offset += SPARK_ENTRY_SIZE;
    }
    DAT_80611f68 = 0;
}

// ============================================================================
// Spark Pool - Rendering
// ============================================================================

// Address: 0x80102c80
// Renders a single spark billboard. Gets texture 0x33a, computes oriented
// billboard from position+direction with elapsed-time-based scale, submits quad.
// State=3 entries get alpha fade based on remaining lifetime / max lifetime.
void spark_render_single(int* entry) {
    int tex_id = sprite_get_texture_id(0x33a);

    // Compute elapsed time scale
    double elapsed = FUN_801310c0();
    float scale = (float)entry[0xb] * (float)elapsed;

    // Build oriented billboard from position (entry+2) and direction (entry+5)
    FUN_80134fac((double)scale, entry[1], (float*)(entry + 2), (float*)(entry + 5));

    // Submit billboard quad with alpha
    if (*entry == SPARK_STATE_FADING) {
        // Fade alpha: (remaining_lifetime / max_lifetime) * 255
        u32 color = entry[0xc] & 0xffffffu;
        int alpha = (int)(FLOAT_80615684 * ((float)entry[8] / (float)entry[9]));
        FUN_8001cb24(&DAT_80587d40, 0, 0x11, tex_id, color | (alpha << 24), 1);
    } else {
        FUN_8001cb24(&DAT_80587d40, 0, 0x11, tex_id, entry[0xc], 1);
    }
}

// Address: 0x80102d68
// Renders all active sparks. Sets GX blend mode (additive, depth read-only),
// iterates 200-entry pool, calls spark_render_single for each active entry.
// Called by particle_system_render_dispatch.
void spark_pool_render_all(void) {
    FUN_803826f0();

    // Set blend mode: additive, no depth write
    FUN_8016f00c(2);
    FUN_8016e8ac(2);

    int idx = 0;
    int offset = 0;
    do {
        if (*(int*)((int)&DAT_80575d00 + offset) != SPARK_STATE_NONE) {
            spark_render_single((int*)((int)&DAT_80575d00 + offset));
        }
        idx++;
        offset += SPARK_ENTRY_SIZE;
    } while (idx < SPARK_POOL_SIZE);

    FUN_8038273c();
}

// ============================================================================
// Spark Pool - Spawning
// ============================================================================

// Address: 0x80102dd8
// Spawns an instant (one-frame) spark at position+direction.
// Uses circular 200-entry pool. State=1 means it will be cleared next frame.
void spark_spawn_instant(void) {
    u64 ctx = FUN_803826f0();
    float* position = (float*)(ctx >> 32);
    float* direction = (float*)(u32)ctx;

    int slot_offset = DAT_80611f68 * SPARK_ENTRY_SIZE;

    // Set state = instant
    (&DAT_80575d00)[DAT_80611f68 * 0xd] = SPARK_STATE_INSTANT;

    // Get sprite handle
    u32 sprite_handle = FUN_80134f88();
    *(u32*)(&DAT_80575d04 + slot_offset) = sprite_handle;

    // Set default scale and lifetime
    *(float*)(&DAT_80575d2c + slot_offset) = FLOAT_80615674;  // max lifetime
    *(float*)(&DAT_80575d28 + slot_offset) = FLOAT_80615680;  // scale

    // Advance circular index
    DAT_80611f68++;

    // Set default color (grey-yellow: 0x7f7f6450)
    *(u32*)(&DAT_80575d30 + slot_offset) = 0x7f7f6450;

    // Copy position (3 floats)
    *(u32*)(&DAT_80575d08 + slot_offset) = *(u32*)&position[0];
    *(u32*)(&DAT_80575d0c + slot_offset) = *(u32*)&position[1];
    *(u32*)(&DAT_80575d10 + slot_offset) = *(u32*)&position[2];

    // Copy direction (3 floats)
    *(u32*)(&DAT_80575d14 + slot_offset) = *(u32*)&direction[0];
    *(u32*)(&DAT_80575d18 + slot_offset) = *(u32*)&direction[1];
    *(u32*)(&DAT_80575d1c + slot_offset) = *(u32*)&direction[2];

    // Wrap circular buffer
    if (DAT_80611f68 >= 199) {
        DAT_80611f68 = 0;
    }

    FUN_8038273c();
}

// Address: 0x80102e98
// Spawns a fading spark with initial lifetime from register.
// State=3 means it will fade over time and be cleared when expired.
// Called by 16+ functions (character damage, projectile sweep, weapon FX, etc).
void spark_spawn_fading(void) {
    u64 ctx = FUN_803826f0();
    float* position = (float*)(ctx >> 32);
    float* direction = (float*)(u32)ctx;
    float initial_lifetime = extraout_f1;  // passed via f1 register

    int slot_idx = DAT_80611f68;
    int slot_offset = DAT_80611f68 * SPARK_ENTRY_SIZE;

    // Set state = fading
    (&DAT_80575d00)[DAT_80611f68 * 0xd] = SPARK_STATE_FADING;

    // Get sprite handle
    u32 sprite_handle = FUN_80134f88();
    *(u32*)(&DAT_80575d04 + slot_offset) = sprite_handle;

    // Store lifetime (current and initial for alpha calculation)
    (&DAT_80575d20)[slot_idx * 0xd] = initial_lifetime;  // current lifetime

    // Set default scale and max lifetime
    *(float*)(&DAT_80575d2c + slot_offset) = FLOAT_80615674;
    *(float*)(&DAT_80575d28 + slot_offset) = FLOAT_80615680;

    // Advance circular index
    DAT_80611f68++;

    // Set default color and store initial lifetime for fade ratio
    *(u32*)(&DAT_80575d30 + slot_offset) = 0x7f7f6450;
    *(float*)(&DAT_80575d24 + slot_offset) = initial_lifetime;  // max lifetime for ratio

    // Copy position and direction
    *(u32*)(&DAT_80575d08 + slot_offset) = *(u32*)&position[0];
    *(u32*)(&DAT_80575d0c + slot_offset) = *(u32*)&position[1];
    *(u32*)(&DAT_80575d10 + slot_offset) = *(u32*)&position[2];
    *(u32*)(&DAT_80575d14 + slot_offset) = *(u32*)&direction[0];
    *(u32*)(&DAT_80575d18 + slot_offset) = *(u32*)&direction[1];
    *(u32*)(&DAT_80575d1c + slot_offset) = *(u32*)&direction[2];

    // Wrap circular buffer
    if (DAT_80611f68 >= 199) {
        DAT_80611f68 = 0;
    }

    FUN_8038273c();
}

// ============================================================================
// Spark Pool - Property Setters (modify most recently spawned spark)
// ============================================================================

// Address: 0x80102f74
// Sets the scale of the most recently spawned spark.
void spark_set_last_scale(float scale) {
    int idx = DAT_80611f68 - 1;
    if (idx < 0) {
        idx = DAT_80611f68 + 199;
    }
    *(float*)(&DAT_80575d28 + idx * SPARK_ENTRY_SIZE) = scale;
}

// Address: 0x80102f9c
// Sets the lifetime/duration of the most recently spawned spark.
void spark_set_last_lifetime(float lifetime) {
    int idx = DAT_80611f68 - 1;
    if (idx < 0) {
        idx = DAT_80611f68 + 199;
    }
    *(float*)(&DAT_80575d2c + idx * SPARK_ENTRY_SIZE) = lifetime;
}

// Address: 0x80102fc4
// Sets the RGBA color of the most recently spawned spark.
void spark_set_last_color(u32 color) {
    int idx = DAT_80611f68 - 1;
    if (idx < 0) {
        idx = DAT_80611f68 + 199;
    }
    *(u32*)(&DAT_80575d30 + idx * SPARK_ENTRY_SIZE) = color;
}

// ============================================================================
// Beam Trail System
// ============================================================================

// Address: 0x80102fec
// Initializes a beam weapon trail segment. Generates 10 control points with
// randomized angular scatter (sin/cos), per-weapon-type parameters for scale,
// lifetime, and scatter amplitude. 7 weapon types (0-6) with paired variants.
// Resolves texture 0x33a for rendering.
// Called by player_ai_projectile_beam_trail_render.
void beam_trail_init_segment(u32 param_1, u32 param_2, u32 param_3) {
    u64 ctx = FUN_803826e4();
    int context = (int)(ctx >> 32);
    int weapon_type = (int)ctx;

    int trail_data = *(int*)(context + 0x18);
    *(u32*)(trail_data + 0x4c8) = param_3;  // store weapon sub-type
    float speed = extraout_f1;

    // Generate random angular offset for scatter
    u32 rng = FUN_8013104c();
    float angle = FLOAT_80615688 * FLOAT_8061568c *
        ((float)((double)(0x43300000 | rng) - DOUBLE_806156d0) / FLOAT_80615690);
    *(float*)(trail_data + 0xa44) = angle;

    // Compute sin/cos for scatter direction
    u32 angle_idx = (u32)(FLOAT_80615698 * angle + FLOAT_80615694);
    float sin_val = (&DAT_80583228)[angle_idx & 0x7ff];
    float cos_val = *(float*)((int)&DAT_80583228 + ((angle_idx + 0x200) * 4 & 0x1ffc));

    *(int*)(trail_data + 0xa58) = weapon_type;

    // Per-weapon-type parameters: scale multiplier and lifetime
    float type_scale, type_lifetime;
    switch (weapon_type) {
        case 0: case 4:  // Standard beam
            rng = FUN_8013104c();
            *(float*)(trail_data + 0xa4c) =
                FLOAT_8061568c * ((float)rng_to_float(rng)) + FLOAT_8061569c;
            break;
        case 1: case 5:  // Wide beam
            rng = FUN_8013104c();
            *(float*)(trail_data + 0xa4c) =
                FLOAT_80615694 * ((float)rng_to_float(rng)) + FLOAT_806156a0;
            break;
        case 2: case 6:  // Thin beam
            rng = FUN_8013104c();
            *(float*)(trail_data + 0xa4c) =
                FLOAT_806156a8 * ((float)rng_to_float(rng)) + FLOAT_806156a4;
            break;
    }

    // Per-type scatter amplitude and height parameters
    float scatter_xy = FLOAT_80615670;  // default zero
    float scatter_z = FLOAT_80615670;
    float height_offset = FLOAT_80615670;

    if (weapon_type == 0 || weapon_type == 1 || weapon_type == 3 ||
        weapon_type == 4 || weapon_type == 5) {
        scatter_xy = FLOAT_806156ac;
        scatter_z = FLOAT_806156b0;
        rng = FUN_8013104c();
        height_offset = FLOAT_80615680 * (rng_to_float(rng) - FLOAT_80615694);
    } else if (weapon_type == 2 || weapon_type == 6) {
        scatter_xy = FLOAT_806156b4;
        scatter_z = FLOAT_806156b8;
        rng = FUN_8013104c();
        height_offset = FLOAT_806156c0 * -(rng_to_float(rng)) + FLOAT_806156bc;
    }

    // Initialize control points
    *(u32*)(context + 0x1c) = 0;  // clear state
    u32 tex_id = sprite_get_texture_id(0x33a);
    *(u32*)(trail_data + 0x554) = tex_id;

    *(float*)(trail_data + 0xa54) = speed;
    *(float*)(trail_data + 0x604) = FLOAT_80615670;  // first control point at origin

    // Initialize vertex positions (Y, Z components)
    *(float*)(trail_data + 0x714) = FLOAT_80615670;
    *(float*)(trail_data + 0x718) = FLOAT_80615670;
    *(float*)(trail_data + 0x71c) = FLOAT_80615670;

    // Generate 10 scale values along trail (decreasing from head to tail)
    for (int i = 0; i < 10; i++) {
        float t = (float)((double)(0x43300000 | (i ^ 0x80000000)) - DOUBLE_80615678);
        float envelope = (FLOAT_80615674 - t / FLOAT_806156c4) * scatter_xy;
        *(float*)(trail_data + 0x558 + i * 4) = envelope;
    }

    // Generate 10 control point offsets with random scatter
    for (int i = 1; i < 10; i++) {
        float t = (float)((double)(0x43300000 | (i ^ 0x80000000)) - DOUBLE_80615678) / FLOAT_806156c4;

        rng = FUN_8013104c();
        float rand_offset = FLOAT_806156cc * (FLOAT_806156c8 * (rng_to_float(rng) - FLOAT_80615694));
        *(float*)(trail_data + 0x604 + i * 4) = rand_offset + t;

        float radial = speed * *(float*)(trail_data + 0x604 + i * 4);

        rng = FUN_8013104c();
        // Y offset: cos * radial + scatter_z * random
        *(float*)(trail_data + 0x714 + i * 0xc) =
            cos_val * radial + scatter_z * (rng_to_float(rng) - FLOAT_80615694);

        rng = FUN_8013104c();
        // X offset: sin * radial + scatter_z * random
        *(float*)(trail_data + 0x718 + i * 0xc) =
            sin_val * radial + scatter_z * (rng_to_float(rng) - FLOAT_80615694);

        // Z offset: t * height_offset
        *(float*)(trail_data + 0x71c + i * 0xc) = t * height_offset;
    }

    FUN_80382730();
}

// Address: 0x80103460
// Evaluates and builds beam trail geometry for rendering. For each control point:
// - Computes alpha fade (brighter at head, fading towards tail)
// - Builds vertex positions using basis vectors (right/up/forward) from trail data
// - Applies rotation matrix around forward axis for twist animation
// Destroys trail via weather_slot_destroy if lifetime exceeded.
// Called by player_ai_projectile_beam_trail_render.
void beam_trail_evaluate_render(u32 param_1, u32 param_2, u32* basis_origin,
                                float* basis_right, float* basis_up, float* basis_forward) {
    u64 ctx = FUN_803826d0();
    int* slot = (int*)(ctx >> 32);
    u32* slot_params = (u32*)(u32)ctx;

    int trail_data = slot[6];

    // Compute progress along trail (time * speed)
    float progress = (float)slot[3] * *(float*)(trail_data + 0xa4c);
    float progress_sq = progress * progress;

    // Compute base color with blending
    u32 base_color = FUN_80132228(extraout_f1, 0x2f3fff, *(u32*)(trail_data + 0x4c8));

    if (progress < FLOAT_80615674) {
        // Trail still active — build geometry

        // Set origin based on weapon type (2/6 use basis_origin, others use slot_params)
        if (*(int*)(trail_data + 0xa58) == 2 || *(int*)(trail_data + 0xa58) == 6) {
            *(u32*)(trail_data + 0x5e0) = basis_origin[0];
            *(u32*)(trail_data + 0x5e4) = basis_origin[1];
            *(u32*)(trail_data + 0x5e8) = basis_origin[2];
        } else {
            *(u32*)(trail_data + 0x5e0) = slot_params[0];
            *(u32*)(trail_data + 0x5e4) = slot_params[1];
            *(u32*)(trail_data + 0x5e8) = slot_params[2];
        }

        // Set vertex count (clamped 3-34)
        slot[7] = 10;
        if (slot[7] < 3) slot[7] = 3;
        if (slot[7] > 0x22) slot[7] = 0x22;

        // Compute alpha envelope from progress
        float head_offset = FLOAT_806156d8 + progress;
        u32 alpha_base = FUN_803825c0(-(FLOAT_80615684 * progress_sq - FLOAT_80615684));

        // Build per-vertex position and color
        int pos_offset = 0;
        int color_offset = 0;
        for (u32 i = 0; (int)i < slot[7]; i++) {
            // Compute parametric t along trail
            float t = (float)((double)(0x43300000 | (i ^ 0x80000000)) - DOUBLE_80615678) /
                      FLOAT_806156dc;

            // Direction-dependent t (type 2/6 goes backward from progress)
            float sample_t;
            if (*(int*)(trail_data + 0xa58) == 2 || *(int*)(trail_data + 0xa58) == 6) {
                sample_t = progress - t;
            } else {
                sample_t = head_offset + t;
            }

            // Compute per-vertex alpha
            int vertex_alpha = 0;
            if (sample_t > FLOAT_80615670 && sample_t < FLOAT_80615674) {
                float alpha_scale;
                if (sample_t >= FLOAT_80615680) {
                    if (sample_t > FLOAT_806156e0) {
                        alpha_scale = FLOAT_806156e4 * (FLOAT_80615674 - sample_t);
                    } else {
                        alpha_scale = 1.0f;  // full brightness in middle
                    }
                } else {
                    alpha_scale = FLOAT_806156dc * sample_t;  // fade in at head
                }
                vertex_alpha = FUN_803825c0(
                    (float)((double)(0x43300000 | alpha_base) - DOUBLE_806156d0) * alpha_scale);
            }

            // Store color with per-vertex alpha
            *(u32*)(trail_data + color_offset + 0x4cc) = base_color | (vertex_alpha << 24);

            // Build world position from control point offsets + basis vectors
            float* cp = (float*)(trail_data + pos_offset);
            float cx = cp[0x1c5];  // control point X scatter
            float cy = cp[0x1c6];  // control point Y scatter
            float cz = cp[0x1c7];  // control point Z scatter

            cp[0] = cz * basis_right[0] + cy * basis_forward[0] +
                    cx * basis_up[0] + *(float*)(trail_data + 0x5e0);
            cp[1] = cz * basis_right[1] + cy * basis_forward[1] +
                    cx * basis_up[1] + *(float*)(trail_data + 0x5e4);
            cp[2] = cz * basis_right[2] + cy * basis_forward[2] +
                    cx * basis_up[2] + *(float*)(trail_data + 0x5e8);

            pos_offset += 0xc;
            color_offset += 4;
        }

        // Apply rotation matrix around forward axis (twist animation)
        float rot_matrix[16];
        float right_copy[3] = { basis_up[0], basis_up[1], basis_up[2] };
        float forward_copy[3] = { basis_forward[0], basis_forward[1], basis_forward[2] };

        FUN_8012f154((double)*(float*)(trail_data + 0xa44),
                     (double)FLOAT_80615670, (double)FLOAT_80615670, rot_matrix);
        FUN_8012fb90(rot_matrix, right_copy);
        FUN_8012fb90(rot_matrix, forward_copy);

        // Store rotated basis for width computation
        int basis_offset = 0;
        for (int i = 0; i < slot[7]; i++) {
            int base = trail_data + basis_offset;
            *(float*)(base + 0x198) = right_copy[0];
            *(float*)(base + 0x19c) = right_copy[1];
            *(float*)(base + 0x1a0) = right_copy[2];
            *(float*)(base + 0x330) = forward_copy[0];
            *(float*)(base + 0x334) = forward_copy[1];
            *(float*)(base + 0x338) = forward_copy[2];
            basis_offset += 0xc;
        }
    } else {
        // Trail expired — destroy
        weather_slot_destroy(slot);
    }

    FUN_8038271c();
}
