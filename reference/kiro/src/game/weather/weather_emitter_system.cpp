// Address: 0x800f02fc | Name: weather_emitter_preload_resources | Date: 2026-08-25
// Address: 0x800f035c | Name: weather_emitter_spawn_entities | Date: 2026-08-25
// Address: 0x800f04c0 | Name: weather_trail_system_alloc | Date: 2026-08-25
// Address: 0x800f04f0 | Name: weather_compute_wind_intensity | Date: 2026-08-25
//
// Weather emitter system management: resource preloading, entity spawning,
// trail buffer allocation, and wind/intensity computation.
// These functions handle the lifecycle and physics of weather emitter entities.

#include "common.h"

// Forward declarations
extern void entity_preload_resources(int entity_type_id);
extern int  entity_spawn_by_type(int type_id, float* position, float* params, int flag1, int flag2);
extern void weather_trail_buffer_clear(void);
extern void weather_trail_buffer_alloc(void* buffer, int flag);
extern void* mem_alloc(int size, int alignment);

// Global sin table (2048 entries)
extern float g_sin_table[2048]; // DAT_80583228

// Weather emitter type table: up to 4 emitter slot definitions
// Stride = 0x44 bytes per slot
// +0x00: entity type ID (-1 = unused)
// +0x24: entity instance handle
// +0x14..0x1C: position XYZ
// +0x18..0x20: velocity XYZ
// +0x28: speed accumulator
// +0x2C: handle reference
// +0x30: total animation weight
extern int g_weather_emitter_table; // DAT_8040bfc8

// Animation node table
extern char* g_anim_node_table; // DAT_80611aa8

// Weather trail particle buffer pointer (150 entries, 0x24 bytes each)
extern void* g_weather_trail_buffer; // DAT_80611ebc

// Global frame time accumulator
extern float g_weather_time_accum; // DAT_80570a08

// Per-emitter counter array (6 integers)
extern int g_weather_emitter_counters[6]; // DAT_80570a14

// Trail buffer active count
extern int g_weather_trail_active_count; // DAT_80611eb8

// Current weather type ID (2=thunderstorm, 0x1a=blizzard)
extern int g_current_weather_type; // DAT_80612614

// ============================================================================
// weather_emitter_preload_resources
// Address: 0x800f02fc
//
// Preloads entity resources for up to 4 weather emitter types.
// Iterates the emitter type table (stride 0x44), skipping entries with ID -1.
// ============================================================================
void weather_emitter_preload_resources(void) {
    unsigned int i;
    int offset;

    weather_emitter_get_context(); // FUN_803826f0

    i = 0;
    offset = 0;
    do {
        int type_id = *(int*)((int)&g_weather_emitter_table + offset);
        if (type_id != -1) {
            entity_preload_resources(type_id);
        }
        i++;
        offset += 0x44;
    } while (i < 4);

    weather_emitter_end_context(); // FUN_8038273c
}

// ============================================================================
// weather_emitter_spawn_entities
// Address: 0x800f035c
//
// Spawns up to 4 weather emitter entities from the type table.
// For each valid entry: spawns entity, computes cumulative animation weight.
// Then clears 150 trail particle slots (0x24 bytes each) and 6 counters.
// Called during level weather initialization.
// ============================================================================
void weather_emitter_spawn_entities(void) {
    unsigned int i;
    int offset;
    int node_count;
    float default_pos[3];

    float total_time = (float)weather_emitter_begin_spawn(); // FUN_803826e0

    i = 0;
    offset = 0;

    do {
        int node_offset = 0;
        *(int*)((int)&g_weather_emitter_table + 0x24 + offset) = 0; // clear handle

        int type_count = 0;
        default_pos[0] = 0.0f; // DAT_803cc8b8
        default_pos[1] = 0.0f; // DAT_803cc8bc
        default_pos[2] = 0.0f; // DAT_803cc8c0

        int type_id = *(int*)((int)&g_weather_emitter_table + offset);
        if (type_id != -1) {
            int entity_handle = entity_spawn_by_type(type_id, default_pos, default_pos, 0, 0);
            *(int*)((int)&g_weather_emitter_table + 0x24 + offset) = entity_handle;
        }

        // Initialize per-slot fields
        *(float*)((int)&g_weather_emitter_table + 0x28 + offset) = 0.0f; // speed accum
        *(int*)((int)&g_weather_emitter_table + 0x2C + offset) = -1;     // ref handle
        *(float*)((int)&g_weather_emitter_table + 0x14 + offset) = 0.0f; // pos x
        *(float*)((int)&g_weather_emitter_table + 0x18 + offset) = 0.0f; // pos y
        *(float*)((int)&g_weather_emitter_table + 0x1C + offset) = 0.0f; // pos z
        *(float*)((int)&g_weather_emitter_table + 0x30 + offset) = 0.0f; // total weight

        // Accumulate animation weights from linked nodes (max 4 per slot)
        while (1) {
            int node_id = *(int*)((int)&g_weather_emitter_table - 0x10 + node_offset + offset);
            if (node_id == 0 || type_count >= 4) break;

            float node_weight = *(float*)(g_anim_node_table + node_id * 0xB4 + 0xAC);
            *(float*)((int)&g_weather_emitter_table + 0x30 + offset) += node_weight;

            type_count++;
            node_offset += 4;
        }

        i++;
        offset += 0x44;
    } while (i < 4);

    // Clear trail buffer
    weather_trail_buffer_clear();

    // Clear 6 emitter counters
    for (int c = 0; c < 6; c++) {
        g_weather_emitter_counters[c] = 0;
    }

    // Clear 150 trail particle active flags
    float clear_val = 300.0f; // FLOAT_8061521c - deactivation sentinel
    for (int p = 0; p < 150; p++) {
        *(float*)((char*)g_weather_trail_buffer + p * 0x24 + 0x20) = clear_val;
    }

    g_weather_trail_active_count = 0;
    weather_emitter_end_spawn(); // FUN_8038272c
}

// ============================================================================
// weather_trail_system_alloc
// Address: 0x800f04c0
//
// Allocates 0x1518 bytes (150 particles * 0x24 = 5400 bytes) for the weather
// trail particle buffer, then initializes it via weather_trail_buffer_alloc.
// ============================================================================
void weather_trail_system_alloc(void) {
    g_weather_trail_buffer = mem_alloc(0x1518, 1);
    weather_trail_buffer_alloc(g_weather_trail_buffer, 1);
}

// ============================================================================
// weather_compute_wind_intensity
// Address: 0x800f04f0
//
// Computes wind-influenced weather intensity using a 3-frequency sine wave
// blend (turbulence simulation). Writes directional velocity to param+0x24..0x2C
// and raw intensity to param+0x30.
//
// For weather types 2 (thunderstorm) and 0x1a (blizzard):
//   Adds time-based oscillation and overrides velocity with fixed storm direction.
//
// param_1: pointer to weather emitter state structure
//   +0x18: base_vel_x
//   +0x1C: base_vel_y
//   +0x20: base_vel_z
//   +0x24: output_vel_x (written)
//   +0x28: output_vel_y (written)
//   +0x2C: output_vel_z (written)
//   +0x30: output_intensity (written)
// ============================================================================
void weather_compute_wind_intensity(int param_1) {
    float intensity;
    float time_phase;

    // Compute time-scaled phase
    time_phase = 16.0f * g_weather_time_accum; // FLOAT_8061523c

    // 3-frequency sine blend for turbulence
    // freq1: 2*PI * 2.0 * phase
    // freq2: 2*PI * 5.0 * phase  (offset by PI/2 = +0x200 in table)
    // freq3: 2*PI * 8.0 * phase
    int idx1 = (int)(6.283f * 2.0f * time_phase + 2.0f) & 0x7FF;
    int idx2 = ((int)(6.283f * 5.0f * time_phase + 2.0f) + 0x200) & 0x7FF; // +PI/2 offset
    int idx3 = (int)(6.283f * 8.0f * time_phase + 2.0f) & 0x7FF;

    intensity = 0.4f * g_sin_table[idx1]   // FLOAT_80615264 (amplitude 1)
              + 0.3f * g_sin_table[idx2]    // FLOAT_806151f4 (amplitude 2)
              + 0.2f * g_sin_table[idx3]    // FLOAT_806151ec (amplitude 3)
              + 2.0f;                        // FLOAT_806151d4 (base offset)

    // Clamp to [0.0, 1.0]
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;

    *(float*)(param_1 + 0x30) = intensity;

    // Check for storm weather types (thunderstorm=2, blizzard=0x1a)
    float storm_factor = 0.0f;
    if (g_current_weather_type == 2 || g_current_weather_type == 0x1a) {
        storm_factor = get_storm_time_factor(); // FUN_8020e788
    }

    if (storm_factor <= 0.0f) {
        // Normal weather: velocity = base_vel * intensity
        *(float*)(param_1 + 0x24) = *(float*)(param_1 + 0x18) * intensity;
        *(float*)(param_1 + 0x28) = *(float*)(param_1 + 0x1C) * intensity;
        *(float*)(param_1 + 0x2C) = *(float*)(param_1 + 0x20) * intensity;
    } else {
        // Storm: add oscillation to intensity, override with fixed storm direction
        float storm_phase = 6.283f * (0.125f * storm_factor); // FLOAT_806151e4
        int storm_idx = (int)(6.283f * storm_phase + 2.0f) & 0x7FF;
        intensity += 0.5f * g_sin_table[storm_idx]; // FLOAT_806151e0 (storm amplitude)

        *(float*)(param_1 + 0x30) = intensity;

        // Fixed storm direction constants
        *(float*)(param_1 + 0x24) = 0.7f * intensity;  // FLOAT_806152f8
        *(float*)(param_1 + 0x28) = 0.8f * intensity;  // FLOAT_806152fc
        *(float*)(param_1 + 0x2C) = 0.9f * intensity;  // FLOAT_80615300
    }
}

// ============================================================================
// weather_effect_get_velocity
// Address: 0x800f06a8
//
// Retrieves velocity XYZ from a weather effect table entry.
// Returns zero vector if entry not found in the table.
// Called by particle_system_update.
//
// param_1: effect ID to look up
// param_2: output float[3] for velocity XYZ
// ============================================================================
void weather_effect_get_velocity(int effect_id, float* out_velocity) {
    int entry = weather_effect_table_lookup(effect_id);

    out_velocity[0] = 0.0f;
    out_velocity[1] = 0.0f;
    out_velocity[2] = 0.0f;

    if (entry != 0) {
        out_velocity[0] = *(float*)(entry + 0x24);
        out_velocity[1] = *(float*)(entry + 0x28);
        out_velocity[2] = *(float*)(entry + 0x2C);
    }
}

// ============================================================================
// weather_emitter_tick
// Address: 0x800f0704
//
// Main per-frame weather emitter update. Iterates active emitter slots:
// - Computes wind intensity via weather_compute_wind_intensity
// - Spawns new trail particles with color sampled from gradient
// - Manages collision shapes for rain puddles (rotation, alpha, layer mask)
// Then calls snow_trail_spawner.
// Finally updates all 150 trail particles: position += velocity * dt,
// age += dt, deactivates particles that exceed max age.
//
// Called by FUN_800f186c (weather system frame dispatcher).
// ============================================================================
void weather_emitter_tick(void) {
    int i, j, k;
    int emitter_offset;
    int emitter_ptr;
    int effect_entry;
    float color_out[3];
    float* particle;
    float dt;

    weather_emitter_begin_tick(); // FUN_803826dc

    // dt = FLOAT_806108ec * (1.0 / framerate)
    // framerate from DAT_806109c0

    // Phase 1: iterate active emitter slots, spawn new trail particles
    emitter_offset = 0;
    for (i = 0; i < g_num_active_emitters; i++) { // DAT_80611d58
        emitter_ptr = g_emitter_slot_base + emitter_offset; // DAT_80611d5c

        // Check if emitter slot is active (flag bit 31 set at +0x21C)
        if ((*(unsigned int*)(emitter_ptr + 0x21C) & 0x80000000) != 0) {
            int node_offset = 0;

            // Iterate linked effect nodes (short array at +0xE0C, count at +0xE0A)
            for (j = 0; j < *(short*)(emitter_ptr + 0xE0A); j++) {
                short effect_id = *(short*)(emitter_ptr + 0xE0C + node_offset);
                effect_entry = weather_effect_table_lookup((int)effect_id);

                if (effect_entry != 0 && *(int*)(effect_entry + 0x3C) < g_frame_counter) {
                    // Mark as processed this frame
                    *(int*)(effect_entry + 0x3C) = g_frame_counter; // DAT_80612020

                    // Compute wind intensity for this effect
                    weather_compute_wind_intensity(effect_entry);

                    // Only spawn if intensity exceeds threshold (FLOAT_806152c4)
                    if (0.15f < *(float*)(effect_entry + 0x30)) {
                        // Spawn N particles per tick (count = rate * speed_scale)
                        int spawn_count = *(int*)(effect_entry + 0x14) * g_weather_speed_scale;

                        for (k = 0; k < spawn_count; k++) {
                            // Sample color from emitter gradient
                            int color_ok = weather_emitter_sample_color(i, effect_entry, color_out);

                            if (color_ok != 0) {
                                // Write new particle into circular buffer
                                particle = (float*)(g_weather_trail_buffer +
                                           g_weather_trail_active_count * 0x24);

                                // RGB color
                                particle[0] = color_out[0];
                                particle[1] = color_out[1];
                                particle[2] = color_out[2];
                                particle[1] += 0.3f; // Y bias (FLOAT_806151f4)

                                // Velocity X: random offset + directional component
                                float rnd = randf();
                                float offset = 1.5f * (rnd - 2.0f); // FLOAT_806151f0
                                rnd = randf();
                                particle[3] = 0.75f * *(float*)(effect_entry + 0x24) *
                                              (2.0f * rnd + 2.0f) + offset;

                                // Velocity Y: small random
                                rnd = randf();
                                particle[4] = 0.75f * rnd; // FLOAT_8061527c

                                // Velocity Z: random offset + directional component
                                rnd = randf();
                                offset = 1.5f * (rnd - 2.0f);
                                rnd = randf();
                                particle[5] = 0.75f * *(float*)(effect_entry + 0x2C) *
                                              (2.0f * rnd + 2.0f) + offset;

                                // Age = 0
                                particle[8] = 0.0f;

                                // Random rotation
                                rnd = randf();
                                particle[6] = 16.0f * (rnd - 2.0f); // FLOAT_8061523c

                                // Random size
                                rnd = randf();
                                particle[7] = 2.0f * (0.125f * (rnd - 2.0f));

                                // Advance circular buffer index (mod 150)
                                g_weather_trail_active_count =
                                    (g_weather_trail_active_count + 1) % 150;
                            }
                        }
                    }

                    // Update collision shape for rain puddles (if entity exists)
                    if (*(int*)(effect_entry + 0x34) != 0) {
                        int mtx = build_identity_matrix(); // FUN_8012ef14
                        float intensity = *(float*)(effect_entry + 0x30);

                        // Accumulate rotation phase
                        *(float*)(effect_entry + 0x38) +=
                            intensity * (0.5f * g_weather_dt_scale * (1.0f / g_framerate_float));

                        if (1.0f <= *(float*)(effect_entry + 0x38)) {
                            *(float*)(effect_entry + 0x38) -= 1.0f;
                        }

                        // Set rotation matrix
                        set_rotation_matrix(*(float*)(effect_entry + 0x38), 0.0f, 0.0f);

                        // Apply to collision entity
                        int entity_ptr = *(int*)(*(int*)(effect_entry + 0x34) + 4);
                        collision_set_face_entry(entity_ptr, 0, mtx);
                        collision_set_layer_mask(entity_ptr, 5);

                        // Set alpha based on intensity
                        unsigned char alpha = (unsigned char)(int)(10.0f * intensity);
                        collision_set_shape_params(entity_ptr, 0xFF, -1, 0xFF, alpha);
                        collision_set_flags(entity_ptr, 0x20);
                    }
                }
                node_offset += 2;
            }
        }
        emitter_offset += 0xFFC;
    }

    // Phase 2: spawn snow trails
    snow_trail_spawner();

    // Phase 3: update all 150 trail particles
    int particle_offset = 0;
    for (i = 0; i < 150; i++) {
        particle = (float*)(g_weather_trail_buffer + particle_offset);

        if (particle[8] >= 0.0f) { // active particle (age >= 0)
            // Advance age
            particle[8] += g_weather_dt_scale * (1.0f / g_framerate_float);

            // Update position: pos += vel * dt
            float frame_dt = g_weather_dt_scale * (1.0f / g_framerate_float);

            // Random perturbation to rotation
            float rnd = randf();
            particle[6] += 0.125f * (g_weather_dt_scale * rnd * (1.0f / g_framerate_float));

            // Position integration
            particle[0] += particle[3] * frame_dt;
            particle[1] += particle[4] * frame_dt;
            particle[2] += particle[5] * frame_dt;

            // Kill particle if age exceeds max (FLOAT_80615254)
            if (4.0f < particle[8]) {
                particle[8] = 300.0f; // FLOAT_8061521c sentinel = dead
            }
        }

        particle_offset += 0x24;
    }

    weather_emitter_end_tick(); // FUN_80382728
}

// ============================================================================
// weather_trail_render_billboards
// Address: 0x800f0b64
//
// Renders active weather trail particles as 3D camera-facing billboards.
// First draws the ground splash/fog layer (weather_render_ground_splashes +
// full-screen sprite 0x300), then iterates 150 trail particles.
// Per particle: computes fade from age, rotation from sin-wave wobble,
// size from age curve, and renders as colored billboard.
//
// Called by FUN_800f1948 (weather render dispatcher).
// ============================================================================
void weather_trail_render_billboards(void) {
    float* particle;
    int i;

    weather_trail_render_begin(); // FUN_803826ec

    // Only render if emitter counter for current slot is nonzero
    if (g_weather_emitter_counters[*(int*)(g_weather_emitter_state_ptr + 0x214)] == 0) {
        weather_trail_render_end(); // FUN_80382738
        return;
    }

    gx_set_blend_mode(1); // normal alpha blend
    weather_render_ground_splashes();

    // Render fog/mist layer using sprite 0x300
    int fog_tex = sprite_get_texture_id(0x300);
    // Compute aspect ratio from viewport dimensions
    float aspect = (g_viewport_height_f / g_viewport_height_half) /
                   (g_viewport_width_f / g_viewport_width_half);
    gx_draw_fullscreen_fog(aspect, 1.0f, 0.0f, 0.0f, 0xFFFFFFFF, fog_tex);

    // Render individual trail particles as billboards
    int particle_offset = 0;
    for (i = 0; i < 150; i++) {
        particle = (float*)(g_weather_trail_buffer + particle_offset);

        if (particle[8] >= 0.0f) { // active
            // Compute normalized age [0..1]
            float t = particle[8] / 4.0f; // FLOAT_80615254
            float fade = 1.0f - t;

            // Compute alpha from fade curve: sin-based * fade
            float phase = 6.283f * (0.5f * 0.125f *
                          (1.0f - fade * fade) - 0.1f); // complex envelope
            int sin_idx = (int)(6.283f * phase + 2.0f) & 0x7FF;
            float alpha_f = 2.0f * g_sin_table[sin_idx] + 2.0f;
            unsigned int color_packed = pack_rgba_swapped(alpha_f); // FUN_80132348

            // Compute rotation angle with fmod wrapping
            float angle = fmodf(particle[6] * t + particle[6], 1.0f); // FUN_80390ae4

            // Clamp rotation to valid range
            if (angle >= 0.75f) {
                // do nothing
            } else if (angle >= 0.125f) {
                angle -= 16.0f; // FLOAT_8061523c wrap
            } else {
                angle += 16.0f;
            }

            // Compute size from age
            float size = 0.6f + t; // FLOAT_80615278

            // Draw billboard at particle world position
            gx_draw_billboard_3d(
                particle[0], particle[1], particle[2],
                size, angle, 0.0f,
                color_packed
            ); // FUN_801735cc
        }

        particle_offset += 0x24;
    }

    gx_end_billboard_batch(); // FUN_80172c54
    weather_trail_render_end(); // FUN_80382738
}

// ============================================================================
// weather_stub_destroy
// Address: 0x800f0d9c
//
// Empty stub for weather destroy callback (no-op).
// ============================================================================
void weather_stub_destroy(void) {
    // intentionally empty
}

// ============================================================================
// weather_stub_render
// Address: 0x800f153c
//
// Empty stub for weather render callback (no-op).
// ============================================================================
void weather_stub_render(void) {
    // intentionally empty
}

// ============================================================================
// weather_system_init_active
// Address: 0x800f1540
//
// Weather system initialization entry point. Sets the weather active flag
// and preloads rain/weather sprite assets.
// ============================================================================
void weather_system_init_active(void) {
    g_weather_active_flag = 1; // DAT_80611eb4
    rain_preload_sprites();
}
