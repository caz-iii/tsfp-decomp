// Address: 0x800ebad8-0x800ec2e4 | Batch 293 | Date: 2026-08-25
// Zone lighting evaluation, animation, entity propagation, utility accessors
// Subsystem: game/particle

#include "common.h"
#include "particle.h"
#include "collision.h"
#include "entity.h"
#include "math_util.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

extern int   g_zone_light_data_ptr;    // DAT_80611e68 — per-zone state (0x28 per zone)
extern int   g_zone_light_def_ptr;     // DAT_80611e78 — zone definitions (0x1C per zone)
extern int   g_zone_light_dirty;       // DAT_80611e6c
extern int   g_special_mode;           // DAT_80611568

extern int   g_particle_env_mode;      // DAT_80612614 — 100=indoor
extern int   g_zone_count;             // DAT_80611b14

extern float g_zone_light_r[];         // DAT_8056ffe0
extern float g_zone_light_g[];         // DAT_8056ffe4
extern float g_zone_light_b[];         // DAT_8056ffe8

extern float g_zone_phase_a;           // FLOAT_80611e70 — oscillator A
extern float g_zone_phase_b;           // FLOAT_80611e74 — oscillator B

extern int   g_entity_table;           // DAT_80611c24 — entity table base (0x2E8 stride)
extern int   g_entity_count;           // DAT_80611c70
extern int   g_entity_zone_table;      // DAT_80611aa8 — per-entity zone data (0xB4 stride)
extern int   g_player_data;            // DAT_80611d78 — player character data (0x141C stride)

extern u32   g_frame_counter;          // DAT_806109c0
extern int   g_frame_time_raw;         // DAT_806108e4
extern int   g_frame_accum;            // DAT_80611928
extern float g_frame_dt_scaled;        // FLOAT_806108ec

extern int   g_no_split_screen;        // DAT_8060faf0

// ---------------------------------------------------------------------------
// Address: 0x800ebad8 | particle_zone_light_free
// Free zone light data: clears pointer unless special mode is active.
// ---------------------------------------------------------------------------
void particle_zone_light_free(void) {
    if (g_special_mode != 0) {
        return;
    }
    g_zone_light_data_ptr = 0;
}

// ---------------------------------------------------------------------------
// Address: 0x800ebaf0 | particle_zone_light_get_shadow_color
// Get zone shadow color for shadow projection. Extracts packed RGB (each
// channel halved) from zone def, returns flag byte from zone state.
// Returns 0 if no zone data or level mode == 0x66.
// ---------------------------------------------------------------------------
int particle_zone_light_get_shadow_color(int zone_id, u32* out_color, u32* out_flags) {
    if (g_zone_light_def_ptr == 0 || g_particle_env_mode == 0x66) {
        return 0;
    }

    u32 packed = *(u32*)(g_zone_light_def_ptr + zone_id * 0x1C - 8);

    // Halve each RGB channel (shift right 1, masking per byte)
    *out_color = (packed >> 1) & 0x7F000000 |
                 (packed >> 1) & 0x00FF0000 |
                 (packed >> 1) & 0x0000FF00;

    // Flag byte from zone state
    *out_flags = *(u32*)(g_zone_light_data_ptr + zone_id * 0x28 + 0x1C) & 0xFF;

    return 1;
}

// ---------------------------------------------------------------------------
// Address: 0x800ebb4c | particle_zone_light_stub
// Empty stub — no-op.
// ---------------------------------------------------------------------------
void particle_zone_light_stub(void) {
    // intentionally empty
}

// ---------------------------------------------------------------------------
// Address: 0x800ebb50 | particle_resolve_zone_id
// Resolve entity zone ID: if entity ID is in player range [0xCA..0xCD],
// reads actual zone from player data. Otherwise returns raw ID.
// ---------------------------------------------------------------------------
int particle_resolve_zone_id(int entity_id) {
    if (entity_id < 0xCA || entity_id > 0xCD) {
        return entity_id;
    }
    // Player entity — resolve zone from player data
    return *(int*)(*(int*)(g_player_data + (entity_id - 0xCA) * 0x141C + 0x14) + 0x34);
}

// ---------------------------------------------------------------------------
// Address: 0x800ebb7c | particle_apply_zone_color_blend
// Apply zone light color blending to a particle/entity color. Only active
// in indoor mode (100) when entity has zone-lit flag (bit 7 at +0xB2).
// Gets zone state, blends color by zone factor.
// ---------------------------------------------------------------------------
u32 particle_apply_zone_color_blend(u32 color, int entity_zone_id) {
    if (g_particle_env_mode == 100) {
        u16 zone_flags = *(u16*)(g_entity_zone_table + entity_zone_id * 0xB4 + 0xB2);
        if ((zone_flags & 0x80) != 0) {
            int zone_state = particle_zone_light_get_state(entity_zone_id);
            color = color_lerp_by_factor(0.8f, *(u32*)(zone_state + 0x1C), color);
        }
    }
    return color;
}

// ---------------------------------------------------------------------------
// Address: 0x800ebbe8 | particle_set_collision_params
// Set collision radius (0.5) and layer mask for an entity's collision shape.
// If shape has no sub-object data, sets layer mask to 7.
// ---------------------------------------------------------------------------
void particle_set_collision_params(int entity, u8 r, u8 g, u8 b) {
    if (entity == 0 || *(int**)(entity + 4) == NULL) {
        return;
    }

    int* shape = *(int**)(entity + 4);
    collision_set_radius_and_flags(0.5f, shape, r, g, b);

    if (*(int*)(shape[1] + 8) == 0) {
        collision_set_layer_mask(shape, 7);
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800ebc48 | particle_zone_apply_to_entities
// Apply zone lighting params to all entities in the scene. For entities in
// the same zone: updates collision params. For entities with type==2: marks
// zone-lit flag (bit 7). For player characters (IDs 0xCA-0xCD): updates
// weapon collision shapes at 3 offsets (+0x1D8, +0x538, +0xD1C).
// ---------------------------------------------------------------------------
void particle_zone_apply_to_entities(u32 zone_id, u32 param_2, u8 g_val, u8 b_val) {
    int current_zone = particle_zone_context_begin();

    for (int i = 0; i < g_entity_count; i++) {
        int entity = g_entity_table + i * 0x2E8;
        int entity_zone = *(int*)(entity + 0x34);

        if (entity_zone == current_zone && *(int*)(entity + 0x20) != 2) {
            // Same zone, non-type-2: update collision params
            particle_set_collision_params(entity, 0, g_val, b_val);
        } else if (entity_zone < 0xCA || entity_zone > 0xCD) {
            // Not a player entity
            if (*(int*)(entity + 0x20) == 2) {
                // Type 2 entity: check zone match and set zone-lit flag
                BOOL matches;
                if (g_no_split_screen == 0) {
                    matches = TRUE;
                } else {
                    matches = (current_zone == entity_zone) ? TRUE : FALSE;
                }
                if (matches) {
                    int zone_data = g_entity_zone_table + entity_zone * 0xB4;
                    u16 flags = *(u16*)(zone_data + 0xB2);
                    if ((flags & 0x80) == 0) {
                        *(u16*)(zone_data + 0xB2) = flags | 0x80;
                    }
                }
            }
        } else {
            // Player character (0xCA-0xCD)
            int player_offset = g_player_data + (entity_zone - 0xCA) * 0x141C;
            if (*(int*)(player_offset + 0x14) != 0 &&
                *(int*)(*(int*)(player_offset + 0x14) + 0x34) == current_zone) {
                // Update weapon collision shapes (3 weapon slots)
                particle_set_collision_params(*(int*)(player_offset + 0x1D8), 0, g_val, b_val);
                particle_set_collision_params(*(int*)(player_offset + 0x538), 0, g_val, b_val);
                particle_set_collision_params(*(int*)(player_offset + 0xD1C), 0, g_val, b_val);
            }
        }
    }

    particle_zone_context_end();
}

// ---------------------------------------------------------------------------
// Address: 0x800ebda4 | particle_zone_light_evaluate
// MASSIVE per-frame zone light evaluation. Evaluates light type:
//   0 = static (constant color from zone def)
//   1 = oscillating (sine wave modulation)
//   2 = strobing (on/off by phase)
//   3 = flickering (random threshold with timed trigger)
//   default = constant 0.5
// Computes interpolated color between two packed RGBs from zone definition.
// Writes result to g_zone_light_r/g/b arrays. Calls
// particle_zone_apply_to_entities. Called from entity_force_bone_update.
// ---------------------------------------------------------------------------
void particle_zone_light_evaluate(void) {
    int ctx = particle_zone_eval_context_begin();
    int zone_id = *(int*)(ctx + 0x34);

    int* zone_state = (int*)(g_zone_light_data_ptr + zone_id * 0x28);
    int light_type = *zone_state;

    float brightness;
    float half = 0.5f; // FLOAT_80615184

    switch (light_type) {
        case 0: {
            // Static: use stored brightness
            brightness = (float)zone_state[3];
            break;
        }
        case 1: {
            // Oscillating: sine wave
            float period = (float)zone_state[2];
            float freq = (float)g_frame_counter / period;
            int phase_offset = g_frame_accum - (g_frame_accum / (int)freq) * (int)freq;
            float t = (float)zone_state[3] + (float)phase_offset / freq;

            // Wrap to [0, 0.5)
            while (t >= half) t -= half;

            // Sine evaluation: sin(2*PI*t - PI) * 0.5 + 0.5
            float sin_val = sinf(6.283f * t - 3.14159f); // FLOAT_806151AC, FLOAT_806151B0
            brightness = 0.5f * sin_val + 0.5f; // FLOAT_80615180
            break;
        }
        case 2: {
            // Strobing: binary on/off by phase
            float period = (float)zone_state[2];
            float freq = (float)g_frame_counter / period;
            int phase_offset = g_frame_accum - (g_frame_accum / (int)freq) * (int)freq;
            float t = (float)zone_state[3] + (float)phase_offset / freq;

            while (t >= half) t -= half;

            brightness = half; // default: on
            if (t < 0.5f) {   // FLOAT_80615180
                brightness = 0.25f; // FLOAT_806151a0 — dimmed
            }
            break;
        }
        case 3: {
            // Flickering: random threshold with timed trigger
            brightness = 0.25f; // FLOAT_806151a0 — default dim

            if (g_frame_time_raw != 0) {
                float period = (float)zone_state[2];
                float freq = (float)g_frame_counter / period;
                int phase_offset = g_frame_accum - (g_frame_accum / (int)freq) * (int)freq;

                // Compute time since last trigger
                float dt_since_trigger = (float)(g_frame_time_raw *
                    (int)(0.75f * (half - period))); // FLOAT_806151B4

                float t = (float)zone_state[3] + (float)phase_offset / freq;
                while (t >= half) t -= half;

                if (g_frame_accum < zone_state[1]) {
                    // Timer expired: use strobe logic
                    brightness = half;
                    if (t < 0.5f) {
                        brightness = 0.25f;
                    }
                } else {
                    // Random trigger check
                    u32 rng = random_seeded();
                    float threshold = (float)zone_state[3];
                    if ((float)rng / 65536.0f < threshold) { // FLOAT_806151B8
                        brightness = half;
                    } else {
                        brightness = 0.25f;
                    }
                    // Set next trigger time
                    zone_state[1] = (int)((float)g_frame_accum + dt_since_trigger);
                }
            }
            break;
        }
        default: {
            brightness = half;
            break;
        }
    }

    // Interpolate between two packed colors from zone definition
    float inv_brightness = half - brightness;
    int zone_def_offset = g_zone_light_def_ptr + (zone_id - 1) * 0x1C;

    u32 color_a = *(u32*)(zone_def_offset + 0x14); // primary color
    u32 color_b = *(u32*)(zone_def_offset + 0x18); // secondary color

    // Extract and interpolate R, G, B channels
    u32 r = (u32)(brightness * (float)((color_a >> 24) & 0xFF) +
                  inv_brightness * (float)((color_b >> 24) & 0xFF));
    u32 g = (u32)(brightness * (float)((color_a >> 16) & 0xFF) +
                  inv_brightness * (float)((color_b >> 16) & 0xFF));
    u32 b = (u32)(brightness * (float)((color_a >> 8) & 0xFF) +
                  inv_brightness * (float)((color_b >> 8) & 0xFF));

    // Pack result (0xFFRRGGBB format)
    zone_state[7] = r | (b << 16) | 0xFF000000 | (g << 8);

    // Write to per-zone float arrays for particle lighting
    if (*zone_state != 0) {
        float scale = 1.0f / 255.0f; // FLOAT_80615190
        g_zone_light_r[zone_id * 3] = (float)r * scale;
        g_zone_light_g[zone_id * 3] = (float)g * scale;
        g_zone_light_b[zone_id * 3] = (float)b * scale;
    }

    // Propagate to scene entities
    particle_zone_apply_to_entities(zone_id, r, (u8)g, (u8)b);

    particle_zone_eval_context_end();
}

// ---------------------------------------------------------------------------
// Address: 0x800ec24c | particle_zone_light_animate
// Advance zone light animation timers. Two independent phase oscillators
// driven by dt * speed, wrapping at 0.5. Only active in indoor mode (100).
// ---------------------------------------------------------------------------
void particle_zone_light_animate(void) {
    if (g_particle_env_mode != 100) {
        return;
    }

    float dt = g_frame_dt_scaled *
               (0.5f / (float)g_frame_counter); // half / frame_counter

    // Oscillator A: speed FLOAT_806151C8
    g_zone_phase_a += 1.5f * dt; // FLOAT_806151C8
    // Oscillator B: speed FLOAT_806151CC
    g_zone_phase_b += 2.0f * dt; // FLOAT_806151CC

    // Wrap both to [0, 0.5)
    while (g_zone_phase_b >= 0.5f) {
        g_zone_phase_b -= 0.5f;
    }
    while (g_zone_phase_a >= 0.5f) {
        g_zone_phase_a -= 0.5f;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800ec2e4 | particle_zone_light_get_state
// Get zone state pointer by zone ID. Simple index into allocated array.
// Returns (g_zone_light_data_ptr + zone_id * 0x28).
// ---------------------------------------------------------------------------
int particle_zone_light_get_state(int zone_id) {
    return g_zone_light_data_ptr + zone_id * 0x28;
}
