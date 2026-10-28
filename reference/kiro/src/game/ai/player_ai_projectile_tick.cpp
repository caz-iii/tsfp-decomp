// =============================================================================
// Player AI Projectile Tick & Effects
// Address Range: 0x800bd1cc - 0x800be4c8
// Date: 2026-08-11
// Batch: 266
//
// This module handles the core projectile per-frame update loop, including:
//   - Simple beam particle spawn (single beam at slot offset)
//   - Sound bank reference cleanup per projectile type
//   - Trail initialization (weather trail spawn with visibility split)
//   - Lightning projectile init (8 billboard ring + 0xB44 trail buffer)
//   - Lightning tick (ring motion with sin/cos + 120-entry trail buffer)
//   - Lightning render (billboards + trail spark rendering)
//   - Particle cleanup/reset (type-specific destroy or fade)
//   - Ambient 3D sound wrapper
//   - MAIN projectile tick (movement, raycast, impact, ricochet, damage, alerts)
//   - Homing/tracking target update (LOS, reflection, velocity-cross lead)
// =============================================================================
#include "common.h"
#include "player_ai.h"
#include "particle.h"
#include "sound.h"
#include "collision.h"
#include "scene.h"
#include "weather.h"

// Globals
extern int DAT_80611d54;    // player controller pointer
extern int DAT_80611d78;    // player data array base
extern int DAT_80612610;    // sound source threshold index
extern int DAT_80612614;    // game state index
extern int DAT_806109c0;    // frame counter
extern int DAT_806108e4;    // frame valid flag
extern float FLOAT_806108ec; // frame delta time
extern char DAT_805015c0;   // game mode flags
extern int DAT_805015d8;    // game mode type
extern float FLOAT_80614a5c; // 0.0f
extern float FLOAT_80614a60; // 1.0f
extern float FLOAT_80614a70; // small offset (bounce normal push)
extern float FLOAT_80614a78; // 0.5f (or 1.5f Newton-Raphson)
extern float FLOAT_80614a7c; // 1.5f (Newton-Raphson constant)
extern float FLOAT_80614a84; // threshold
extern float FLOAT_80614a9c; // 2.0f
extern float FLOAT_80614aa0; // random range scale
extern float FLOAT_80614abc; // fade-in scale
extern float FLOAT_80614acc; // height offset
extern float FLOAT_80614ae4; // particle scale
extern float FLOAT_80614b20; // alert radius
extern float FLOAT_80614b4c; // velocity bias
extern float FLOAT_80614b84; // ambient volume
extern float FLOAT_80614bac; // ricochet push distance
extern float FLOAT_80614bb4; // fade-out rate
extern float FLOAT_80614bc0; // 0.5f (half-offset scale)
extern float FLOAT_80614be0; // trail initial lifetime
extern float FLOAT_80614be8; // angular step
extern float FLOAT_80614c2c; // sin table scale
extern float FLOAT_80614c64; // direction threshold
extern float FLOAT_80614c68; // ring opacity
extern float FLOAT_80614c6c; // ring angular rate
extern float FLOAT_80614c70; // trail spawn rate
extern float FLOAT_80614c74; // trail alpha scale
extern float FLOAT_80614ac4; // velocity-cross scale
extern float FLOAT_80614ba8; // min velocity sq threshold
extern float FLOAT_80614c00; // norm threshold sq
extern float FLOAT_80611718; // ambient pitch
extern void* DAT_804045c0;  // projectile type vtable base
extern double DOUBLE_80614a68; // 2^52 int-to-float trick

// Sin/cos lookup table (2048 entries)
extern float DAT_80583228[];

// Vector constants for cross product reference
extern float DAT_803cc5f8;  // up_x
extern float DAT_803cc5fc;  // up_y
extern float DAT_803cc600;  // up_z
extern float DAT_803cc604;  // right_x
extern float DAT_803cc608;  // right_y
extern float DAT_803cc60c;  // right_z

// Forward declarations
extern int particle_spawn_beam(int handle_addr, int type, int* start_pos, int* end_pos, int mask);
extern void particle_slot_set_opacity(double opacity, int* particle);
extern void particle_slot_set_position(int particle, void* pos);
extern int particle_spawn_billboard(int handle_addr, int type, int subtype, int* pos, int mask);
extern int particle_spawn_oriented(void);
extern void* block_alloc_dispatch(int size, int align, int owner, void* callback);
extern int weather_trail_spawn(int* handle, int type, int variant, int* start, int* end, int mask);
extern void weather_trail_set_target(int trail, int* target);
extern void player_ai_particle_destroy(int slot, int index);
extern void player_ai_particle_stop_fade(int slot, int index);
extern void sound_clear_bank_ref(int ref_id);
extern void sound_play_ambient_3d(double volume, double pitch);
extern int player_ai_projectile_has_character_target(int slot);
extern int player_ai_fire_raycast(int* slot, float* velocity, float* hit_pos, float* hit_normal, int* results);
extern void player_ai_on_damage_received(int* slot, int* results);
extern void player_ai_impact_dispatch(int* slot, int type, int hit, float* pos, float* normal, float* reflect, int owner);
extern void player_ai_entity_raycast(int slot);
extern void player_ai_slot_deactivate(void);
extern void player_ai_alert_check_gated(void* pos, void* prev_pos, int* slot);
extern void player_ai_alert_fire_radius(double radius, int slot, int hit_type, float* pos);
extern void player_ai_voice_pitch_update(int* slot);
extern void player_ai_play_ambient_3d(unsigned int material, float* pos, int flags);
extern void player_ai_projectile_proximity_detonate(void);
extern void collision_result_restore(int* buf);
extern int collision_get_hit_entity_id(void);
extern unsigned char collision_get_surface_material_id(void);
extern void switch_slot_reset(int* buf);
extern double switch_slot_load_from_def(int* buf, int* slot);
extern void damage_resolve_and_dispatch(double d1, double d2, double d3, double d4, double d5, double d6, double d7, double d8, int* ctx);
extern int scene_raycast(int room, int* origin, float* dir, int a, float* b, float* c, int* d, int mask, int e, int* f);
extern int sound_source_find_reflection(int source, int entity, float* pos);
extern int sprite_get_texture_id(int id);
extern double FUN_80131538(double val);  // sqrtf wrapper
extern double FUN_801310c0(void);        // random float [0,1)
extern double FUN_80390324(double val);  // floor/trunc
extern int FUN_803825c0(double val);     // float-to-int with clamp

// =============================================================================
// Address: 0x800bd1cc | player_ai_projectile_spawn_beam_single
// Spawns a single beam particle at slot offset 0xB0.
// Used for simpler projectile types that only need one tracer beam.
// =============================================================================
void player_ai_projectile_spawn_beam_single(int slot) {
    int beam_handle;

    beam_handle = particle_spawn_beam(
        slot + 0xb0, 0,
        (int*)(slot + 0x34),   // start position
        (int*)(slot + 0x7c),   // end position
        0xffff                  // all players see
    );
    *(int*)(slot + 0xb0) = beam_handle;
}

// =============================================================================
// Address: 0x800bd218 | player_ai_projectile_clear_sound_banks
// Clears sound bank references when a projectile type is being unloaded.
// Checks vtable callbacks to identify which type, then clears 4 bank refs
// for trail-based projectiles (0xCD6-0xCD9) and 4 for another type (0xD34-0xD37).
// =============================================================================
void player_ai_projectile_clear_sound_banks(int type_id) {
    typedef void (*TypeCallback)(void*);

    // Check if type's init callback is the trail init (FUN_800bd2c0)
    if (*(TypeCallback*)((char*)&DAT_804045c0 + type_id * 0x40) ==
        (TypeCallback)player_ai_projectile_trail_init) {
        for (int i = 0; i < 4; i++) {
            sound_clear_bank_ref(i + 0xcd6);
        }
    }

    // Check if type's tick callback is FUN_800c230c
    if (*(TypeCallback*)((char*)&DAT_804045c0 + 4 + type_id * 0x40) ==
        (TypeCallback)0x800c230c) {
        sound_clear_bank_ref(0xd34);
        sound_clear_bank_ref(0xd35);
        sound_clear_bank_ref(0xd36);
        sound_clear_bank_ref(0xd37);
    }
}

// =============================================================================
// Address: 0x800bd2c0 | player_ai_projectile_trail_init
// Initializes weather trail effects for trail-based projectiles (types 0x1B-0x2E).
// Maps projectile type to trail variant index, then spawns trail with
// character-target visibility split (same pattern as beam particles).
// Sets flag 0x10000 on slot flags to mark trail as active.
// =============================================================================
void player_ai_projectile_trail_init(int* slot) {
    int has_target;
    int trail_handle;

    // Map projectile type to trail variant
    switch (*slot) {
        case 0x1b: slot[1] = 0; break;
        case 0x1c: slot[1] = 2; break;
        case 0x1d: slot[1] = 3; break;
        case 0x1e: slot[1] = 1; break;
        case 0x1f: slot[1] = 4; break;
        case 0x2d: slot[1] = 5; break;
        case 0x2e: slot[1] = 6; break;
    }

    has_target = player_ai_projectile_has_character_target((int)slot);
    if (has_target == 0) {
        // Non-targeting: single trail, full visibility
        trail_handle = weather_trail_spawn(
            slot + 0x2e, 0, slot[1],
            slot + 0xd,    // start position (dest)
            slot + 0x1f,   // direction (dest)
            0xffff
        );
        slot[0x2e] = trail_handle;
    } else {
        // Targeting: dual trail split by player visibility
        int player_bit = 1 << *(int*)(DAT_80611d54 + 0x214);

        trail_handle = weather_trail_spawn(
            slot + 0x2d, 0, slot[1],
            slot + 10,     // start position (curr)
            slot + 0x1c,   // direction (curr)
            player_bit
        );
        slot[0x2d] = trail_handle;

        trail_handle = weather_trail_spawn(
            slot + 0x2e, 0, slot[1],
            slot + 0xd,    // start position (dest)
            slot + 0x1f,   // direction (dest)
            ~player_bit & 0xffff
        );
        slot[0x2e] = trail_handle;
    }

    // Mark trail as active
    slot[3] = slot[3] | 0x10000;
}

// =============================================================================
// Address: 0x800bd410 | player_ai_projectile_lightning_init
// Initializes a lightning-type projectile (type 0x31).
// Spawns 8 billboard particles in a ring around the projectile origin
// with initial opacity, then allocates a 0xB44-byte trail buffer for
// 120 position entries (used for electrical arc trail rendering).
// Buffer layout: [0x000-0x1DF] angles, [0x1E0-0x77F] positions (vec3 * 120),
// [0x780-0x95F] lifetimes, [0x960-0xB3F] sizes, [0xB40] write index.
// =============================================================================
void player_ai_projectile_lightning_init(void) {
    int slot;
    int i;
    int offset;
    int billboard;
    int* trail_buf;

    slot = FUN_803826e8();

    // Spawn 8 ring particles
    i = 0;
    offset = 0;
    do {
        billboard = particle_spawn_billboard(
            slot + offset + 0xac, 0x81, 0,
            (int*)(slot + 0x28),   // position
            0xffff
        );
        *(int*)(slot + offset + 0xac) = billboard;
        particle_slot_set_opacity((double)FLOAT_80614c68, (int*)*(int*)(slot + offset + 0xac));
        i++;
        offset += 4;
    } while (i < 8);

    // Allocate trail buffer (0xB44 bytes)
    trail_buf = (int*)block_alloc_dispatch(0xb44, 1, slot, (void*)0x800ca36c);
    *(int**)(slot + 0x208) = trail_buf;

    // Initialize buffer if allocation succeeded
    int buf = *(int*)(slot + 0x208);
    if (buf != 0) {
        *(int*)(buf + 0xb40) = 0;  // write index = 0
        float initial_lifetime = FLOAT_80614be0;

        // Initialize all 120 lifetimes to initial value
        int count = 0x78;  // 120
        int idx = 0;
        do {
            *(float*)(buf + idx + 0x780) = initial_lifetime;
            idx += 4;
            count--;
        } while (count != 0);
    }

    FUN_80382734();
}

// =============================================================================
// Address: 0x800bd4d8 | player_ai_projectile_lightning_tick
// MASSIVE per-frame update for lightning-type projectiles (type 0x31).
// Updates 8 ring particle positions using sin/cos rotation around the
// projectile's direction vector (cross-product for perpendicular axes).
// Also manages a 120-entry circular trail buffer: spawns new trail entries
// at a frame-rate-scaled rate, then advances existing entries along the
// ring with lifetime-based orbital decay.
// Uses paired-single float registers extensively (GameCube optimization).
// =============================================================================
void player_ai_projectile_lightning_tick(void) {
    int slot;
    float dir_x, dir_y, dir_z;
    float perp1_x, perp1_y, perp1_z;  // first perpendicular axis
    float perp2_x, perp2_y, perp2_z;  // second perpendicular axis
    float sin_val, cos_val;
    unsigned int angle_idx;
    int i;

    slot = FUN_803826e8();
    dir_x = *(float*)(slot + 0x68);  // direction X

    // Compute perpendicular axes via cross product with world up/right
    // Choose reference vector based on direction magnitude
    float ref_x, ref_y, ref_z;
    if (fabsf(dir_x) <= FLOAT_80614c64) {
        // Use world up vector
        ref_x = DAT_803cc5f8; ref_y = DAT_803cc5fc; ref_z = DAT_803cc600;
    } else {
        // Use world right vector
        ref_x = DAT_803cc604; ref_y = DAT_803cc608; ref_z = DAT_803cc60c;
    }

    // Cross product: perp1 = direction x reference
    float dy = *(float*)(slot + 0x64);
    float dz = *(float*)(slot + 0x6c);
    perp1_x = dy * ref_z - dir_x * ref_y;
    perp1_y = dz * ref_x - dy * ref_z;  // note: partial
    perp1_z = dir_x * ref_z - dz * ref_y;  // note: partial

    // Cross product: perp2 = direction x perp1
    perp2_x = perp1_x * dy - perp1_z * dz;
    perp2_y = perp1_y * dz - perp1_x * dir_x;
    perp2_z = perp1_z * dir_x - perp1_y * dy;

    float ring_radius = FLOAT_80614acc;
    float angular_step = FLOAT_80614c2c;
    float angular_rate = FLOAT_80614c6c;
    float random_scale = FLOAT_80614aa0;

    // Update 8 ring particles
    i = 0;
    int particle_offset = 0;
    do {
        // Compute angle for this particle: angular_step * (i * angular_rate)
        angle_idx = (unsigned int)(angular_step * (float)(FLOAT_80614be8 * (float)i * angular_rate)
                    + FLOAT_80614a78);

        // Lookup sin/cos from table
        sin_val = ring_radius * DAT_80583228[angle_idx & 0x7ff];
        cos_val = ring_radius * DAT_80583228[((angle_idx + 0x200) * 4 & 0x1ffc) / 4];

        // Random offset
        float rand_x = random_scale * (float)(FUN_801310c0() - (double)FLOAT_80614a78);
        float rand_y = random_scale * (float)(FUN_801310c0() - (double)FLOAT_80614a78);
        float rand_z = random_scale * (float)(FUN_801310c0() - (double)FLOAT_80614a78);

        // Position = origin + perp1*cos + perp2*sin + random
        float pos[3];
        pos[0] = *(float*)(slot + 0x28) + rand_x
                 + (perp1_z * cos_val + perp2_y * sin_val);
        pos[1] = *(float*)(slot + 0x2c) + rand_y
                 + (perp1_y * cos_val + perp2_x * sin_val);
        pos[2] = *(float*)(slot + 0x30) + rand_z
                 + (perp1_x * cos_val + perp2_z * sin_val);

        particle_slot_set_position(*(int*)(slot + particle_offset + 0xac), pos);
        i++;
        particle_offset += 4;
    } while (i < 8);

    // Trail buffer management
    int trail_buf = *(int*)(slot + 0x208);
    if (trail_buf == 0) {
        FUN_80382734();
        return;
    }

    // Compute number of new trail entries to spawn this frame
    float frame_time = (float)((double)(DAT_806109c0 ^ 0x80000000) + 4503599627370496.0
                       - 4503599627370496.0);
    int spawn_count = (int)FUN_80390324(
        (double)(FLOAT_80614c70 * FLOAT_806108ec / frame_time));

    // Spawn new trail entries
    for (int s = 0; s < spawn_count; s++) {
        // Random angle
        float rand_angle = (float)FUN_801310c0();
        angle_idx = (unsigned int)(angular_step * (FLOAT_80614be8 * rand_angle) + FLOAT_80614a78);

        cos_val = DAT_80583228[((angle_idx + 0x200) * 4 & 0x1ffc) / 4];
        sin_val = FLOAT_80614ae4 * DAT_80583228[angle_idx & 0x7ff];
        float cos_scaled = FLOAT_80614ae4 * cos_val;

        int write_idx = *(int*)(trail_buf + 0xb40);

        // Store angle
        *(float*)(trail_buf + write_idx * 4) = FLOAT_80614be8 * rand_angle;

        // Store position = origin + perp axes * sin/cos + velocity bias
        int pos_offset = write_idx * 12 + 0x1e0;
        *(float*)(trail_buf + pos_offset + 0) = *(float*)(slot + 0x28)
            + (cos_scaled * perp1_z + sin_val * perp2_y)
            + FLOAT_80614b4c * *(float*)(slot + 0x70);
        *(float*)(trail_buf + pos_offset + 4) = *(float*)(slot + 0x2c)
            + (cos_scaled * perp1_y + sin_val * perp2_x)
            + FLOAT_80614b4c * *(float*)(slot + 0x74);
        *(float*)(trail_buf + pos_offset + 8) = *(float*)(slot + 0x30)
            + (cos_scaled * perp1_x + sin_val * perp2_z)
            + FLOAT_80614b4c * *(float*)(slot + 0x78);

        // Reset lifetime and random size
        *(float*)(trail_buf + write_idx * 4 + 0x780) = FLOAT_80614a5c;
        float rsize = (float)(FLOAT_80614a78 * FUN_801310c0() + FLOAT_80614a78);
        *(float*)(trail_buf + write_idx * 4 + 0x960) = rsize;

        // Advance circular index
        *(int*)(trail_buf + 0xb40) = write_idx + 1;
        if (*(int*)(trail_buf + 0xb40) > 0x77) {
            *(int*)(trail_buf + 0xb40) = 0;
        }
    }

    // Advance existing trail entries (orbital motion + lifetime)
    float zero = FLOAT_80614a5c;
    for (int t = 0; t < 120; t++) {
        float* lifetime = (float*)(trail_buf + t * 4 + 0x780);
        if (zero <= *lifetime) {
            // Advance lifetime
            float dt_scaled = FLOAT_80614a78 * FLOAT_806108ec * (1.0f / frame_time);

            angle_idx = (unsigned int)(FLOAT_80614c2c * *(float*)(trail_buf + t * 4)
                        + FLOAT_80614a78);
            cos_val = DAT_80583228[((angle_idx + 0x200) * 4 & 0x1ffc) / 4];
            sin_val = dt_scaled * DAT_80583228[angle_idx & 0x7ff];
            float cos_dt = dt_scaled * cos_val;

            *lifetime += FLOAT_806108ec * (1.0f / frame_time);

            if (*lifetime <= FLOAT_80614a78) {
                // Apply orbital displacement
                int p_off = trail_buf + t * 12 + 0x1e0;
                *(float*)(p_off + 0) += (float)(perp1_z * (double)cos_dt + perp2_y * (double)sin_val);
                *(float*)(p_off + 4) += (float)(perp1_y * (double)cos_dt + perp2_x * (double)sin_val);
                *(float*)(p_off + 8) += (float)(perp1_x * (double)cos_dt + perp2_z * (double)sin_val);
            } else {
                // Expired - mark as dead
                *lifetime = FLOAT_80614be0;
            }
        }
    }

    FUN_80382734();
}

// =============================================================================
// Address: 0x800bdb0c | player_ai_projectile_lightning_render
// Renders lightning projectile: 8 ring particles as billboards using sprite
// texture 0x28D, plus 120 trail spark points with lifetime-based alpha fade.
// Trail sparks use color 0x7F1414 (dark red) with alpha from lifetime curve.
// =============================================================================
void player_ai_projectile_lightning_render(void) {
    int slot;
    int i;
    int particle_offset;

    slot = FUN_803826e8();

    // Setup sprite rendering mode
    FUN_8016f00c(2);
    FUN_8016e8ac(2);

    int tex_id = sprite_get_texture_id(0x28d);
    FUN_80172dc4((double)FLOAT_80614a60, (double)FLOAT_80614a84,
                 (double)FLOAT_80614a5c, (double)FLOAT_80614a5c, 0xffffffff, tex_id);

    // Render 8 ring particles
    i = 0;
    particle_offset = 0;
    do {
        int particle = *(int*)(slot + particle_offset + 0xac);
        if (particle != 0) {
            gx_projtex_add_point(
                (double)*(float*)(particle + 0x20),
                (double)*(float*)(particle + 0x24),
                (double)*(float*)(particle + 0x28),
                (double)FLOAT_80614ae4,
                (double)FLOAT_80614a5c,
                (double)FLOAT_80614a5c,
                3.3911994e+38f  // max depth (always render)
            );
        }
        i++;
        particle_offset += 4;
    } while (i < 8);

    // Render trail sparks from buffer
    int trail_buf = *(int*)(slot + 0x208);
    if (trail_buf != 0) {
        float zero = FLOAT_80614a5c;
        int pos_offset = 0;
        int lifetime_offset = 0;

        for (int t = 0; t < 120; t++) {
            float lifetime = *(float*)(trail_buf + lifetime_offset + 0x780);
            if (zero <= lifetime) {
                // Compute alpha from lifetime
                float alpha_f = FLOAT_80614a9c * lifetime;  // 2x lifetime
                float alpha;
                if (FLOAT_80614a84 <= alpha_f) {
                    // Fade out phase
                    alpha = -(FLOAT_80614bb4 * (alpha_f - FLOAT_80614a84) - FLOAT_80614a60);
                } else {
                    // Fade in phase
                    alpha = FLOAT_80614abc * alpha_f;
                }

                // Compute RGBA color with alpha
                int alpha_byte = FUN_803825c0((double)(FLOAT_80614c74 * alpha));
                int color = (alpha_byte << 24) | 0x7f1414;  // dark red + alpha

                // Render spark billboard
                float* pos = (float*)(trail_buf + pos_offset + 0x1e0);
                gx_projtex_add_point(
                    (double)pos[0], (double)pos[1], (double)pos[2],
                    (double)FLOAT_80614a70,
                    (double)FLOAT_80614a5c, (double)FLOAT_80614a5c,
                    *(float*)&color
                );
            }
            lifetime_offset += 4;
            pos_offset += 12;
        }
    }

    FUN_80172c54();  // End sprite batch
    FUN_80382734();
}

// =============================================================================
// Address: 0x800bdc84 | player_ai_projectile_cleanup_particles
// Cleans up projectile visual effects on deactivation or impact.
// Type 0x31 (lightning): destroys all 8 ring particles.
// Other types: updates trail target positions, then stops/fades particles 2+3.
// =============================================================================
void player_ai_projectile_cleanup_particles(int* slot) {
    if (*slot == 0x31) {
        // Lightning: destroy all 8 ring billboard particles
        for (int i = 0; i < 8; i++) {
            player_ai_particle_destroy((int)slot, i);
        }
    } else {
        // Trail-based: update trail endpoint targets before stopping
        int* trail_a = (int*)slot[0x2d];
        if (trail_a != NULL && *trail_a == 0xc) {
            weather_trail_set_target((int)trail_a, slot + 10);  // curr position
        }

        int* trail_b = (int*)slot[0x2e];
        if (trail_b != NULL && *trail_b == 0xc) {
            weather_trail_set_target((int)trail_b, slot + 0xd);  // dest position
        }

        // Stop and fade particles at indices 2 and 3
        player_ai_particle_stop_fade((int)slot, 2);
        player_ai_particle_stop_fade((int)slot, 3);
    }
}

// =============================================================================
// Address: 0x800bdd38 | player_ai_projectile_play_impact_sound
// Plays a positioned ambient 3D sound at the impact point.
// Uses fixed volume (FLOAT_80614b84) and pitch (FLOAT_80611718).
// =============================================================================
void player_ai_projectile_play_impact_sound(void) {
    sound_play_ambient_3d((double)FLOAT_80614b84, (double)FLOAT_80611718);
}

// =============================================================================
// Address: 0x800bdd68 | player_ai_projectile_main_tick
// MASSIVE core per-frame projectile update function (750+ bytes).
// Handles the full lifecycle of an in-flight projectile:
//   1. Lifetime/direction validation (deactivate if expired or zero velocity)
//   2. Velocity computation from direction * speed * dt
//   3. Raycast for collision detection (player_ai_fire_raycast)
//   4. On miss: advance all position slots along velocity
//   5. On hit:
//      a. Restore collision state, compute reflection vector
//      b. Dispatch impact effects (player_ai_impact_dispatch)
//      c. Handle special cases: type-7 ricochet with beam respawn
//      d. Surface material sound, voice pitch update for character hits
//      e. Alert AI in radius, load damage switch slot
//      f. Apply damage via damage_resolve_and_dispatch
//      g. Check proximity detonation
//      h. Cleanup particles
//      i. Entity ID 0x13: bounce (reflect direction, re-raycast)
//      j. Other entities: ricochet (offset pos, reflect, re-create trails)
//   6. Lightning type (0x31): call lightning_tick
//   7. Other types: update trail particle positions
//   8. Final alert propagation check
// =============================================================================
void player_ai_projectile_main_tick(void) {
    int* slot;
    float velocity[3];
    float hit_pos[3];       // local_1d4, local_1d0, local_1cc
    float hit_normal[3];    // local_1e0, local_1dc, local_1d8
    float reflect[3];       // local_1f8, local_1f4, local_1f0
    int prev_pos[3];        // local_1c8, local_1c4, local_1c0
    int results[2];         // hit count + data
    unsigned char collision_buf[32];
    int switch_ctx[10];
    double dt_scaled;

    slot = (int*)FUN_803826f0();

    // Compute frame-scaled speed
    float frame_time = (float)((double)(DAT_806109c0 ^ 0x80000000) + 4503599627370496.0
                       - 4503599627370496.0);
    dt_scaled = (double)(FLOAT_80614a60 / frame_time * (float)slot[0x22] * FLOAT_806108ec);

    // Skip if frame not valid
    if (DAT_806108e4 == 0) goto done;

    // Deactivate if lifetime expired (>= 2.0) or zero velocity
    if ((FLOAT_80614a9c <= *(float*)&slot[0x26]) ||
        ((FLOAT_80614a5c == *(float*)&slot[0x19] &&
          FLOAT_80614a5c == *(float*)&slot[0x1a] &&
          FLOAT_80614a5c == *(float*)&slot[0x1b]))) {
        player_ai_projectile_cleanup_particles(slot);
        player_ai_slot_deactivate();
        goto done;
    }

    // Save previous position
    prev_pos[0] = slot[7];
    prev_pos[1] = slot[8];
    prev_pos[2] = slot[9];

    // Compute velocity = direction * dt_scaled
    velocity[0] = (float)((double)*(float*)&slot[0x19] * dt_scaled);
    velocity[1] = (float)((double)*(float*)&slot[0x1a] * dt_scaled);
    velocity[2] = (float)((double)*(float*)&slot[0x1b] * dt_scaled);

    // Raycast along velocity vector
    results[0] = 0;
    results[1] = 0;
    int hit = player_ai_fire_raycast(slot, velocity, hit_pos, hit_normal, results);

    // Apply damage if hit count > 0
    if (results[0] > 0) {
        player_ai_on_damage_received(slot, results);
    }

    // Multiplayer network sync
    if (((DAT_805015c0 & 0x40) != 0) && (FUN_8024786c() != 0)) {
        FUN_80247154(slot[0x41], slot + 7, velocity);
    }

    if (hit == 0) {
        // No collision: advance all position slots
        *(float*)&slot[7]  += velocity[0];
        *(float*)&slot[8]  += velocity[1];
        *(float*)&slot[9]  += velocity[2];
        *(float*)&slot[10] += (float)((double)*(float*)&slot[0x1c] * dt_scaled);
        *(float*)&slot[0xb] += (float)((double)*(float*)&slot[0x1d] * dt_scaled);
        *(float*)&slot[0xc] += (float)((double)*(float*)&slot[0x1e] * dt_scaled);
        *(float*)&slot[0xd] += (float)((double)*(float*)&slot[0x1f] * dt_scaled);
        *(float*)&slot[0xe] += (float)((double)*(float*)&slot[0x20] * dt_scaled);
        *(float*)&slot[0xf] += (float)((double)*(float*)&slot[0x21] * dt_scaled);
        // Accumulate distance squared
        *(float*)&slot[0x3d] += (float)(dt_scaled * dt_scaled);
    } else {
        // Collision occurred
        collision_result_restore((int*)collision_buf);

        // Compute reflection vector: R = D - 2*(D.N)*N
        float dot = *(float*)&slot[0x19] * hit_normal[0]
                  + *(float*)&slot[0x1a] * hit_normal[1]
                  + *(float*)&slot[0x1b] * hit_normal[2];
        float dot2 = dot + dot;
        reflect[0] = -(hit_normal[0] * dot2 - *(float*)&slot[0x19]);
        reflect[1] = -(hit_normal[1] * dot2 - *(float*)&slot[0x1a]);
        reflect[2] = -(hit_normal[2] * dot2 - *(float*)&slot[0x1b]);

        // Accumulate distance to hit point
        float dx = hit_pos[0] - *(float*)&slot[7];
        float dy = hit_pos[1] - *(float*)&slot[8];
        float dz = hit_pos[2] - *(float*)&slot[9];
        *(float*)&slot[0x3d] += (dx*dx + dy*dy + dz*dz);

        unsigned int entity_flag = *(unsigned int*)(hit + 0x9c) & 0x4000000;

        if (entity_flag == 0) {
            // Normal surface impact
            player_ai_impact_dispatch(slot, *slot, hit, hit_pos, hit_normal, reflect, slot[0x42]);

            // Type 7 + mode 1 + has sound def: spawn decal at impact
            if (*slot == 7 && (slot[3] & 0x40u) == 0 && slot[0x40] == 1
                && (int*)slot[0x41] != NULL) {
                int sound_entity = *(int*)slot[0x41];
                if (sound_entity != 0 && *(int*)(sound_entity + 0x150) != 0) {
                    int decal_id = *(int*)(*(int*)(sound_entity + 0x150) + 4);
                    if (decal_id >= 0) {
                        FUN_800d18a4((double)FLOAT_80614a60, decal_id, (int*)hit_pos, hit_normal);
                    }
                }
            }
        } else if (DAT_80612614 == 0xb && slot[0x41] != 0
                   && *(int*)(slot[0x41] + 8) < DAT_80612610) {
            // Special entity (shield) - dispatch shield effect
            FUN_80221074((double)FLOAT_80614a5c, 0, 0, 0, 0, 0, 0, 0);
        }

        // Sound on hit
        if (*(int*)(hit + 0x20) == 8) {
            // Character hit
            if (entity_flag == 0) {
                player_ai_voice_pitch_update(slot);
                player_ai_projectile_play_impact_sound();
            }
        } else {
            // Surface hit - play material-specific sound
            unsigned char material = collision_get_surface_material_id();
            if ((slot[3] & 0x40u) == 0) {
                player_ai_play_ambient_3d((unsigned int)material, hit_pos, slot[3]);
            }
        }

        // Alert AI unless character hit
        if (*(int*)(hit + 0x20) != 8) {
            player_ai_alert_check_gated(hit_pos, prev_pos, slot);
        }
        player_ai_alert_fire_radius((double)FLOAT_80614b20, (int)slot, *(int*)(hit + 0x20), hit_pos);

        // Damage resolution
        switch_slot_reset(switch_ctx);
        if (entity_flag == 0) {
            switch_slot_load_from_def(switch_ctx, slot);
            // ... setup damage context and dispatch
            switch_ctx[0] = hit;
            damage_resolve_and_dispatch(0, 0, 0, 0, 0, 0, 0, 0, switch_ctx);
        }

        player_ai_projectile_proximity_detonate();
        player_ai_projectile_cleanup_particles(slot);

        // Post-impact behavior based on hit entity type
        int hit_entity_id = collision_get_hit_entity_id();
        float push = FLOAT_80614a70;

        if (hit_entity_id == 0x13) {
            // Entity 0x13: perfect bounce (mirror reflect)
            *(float*)&slot[7]  = -(push * hit_normal[0] - hit_pos[0]);
            *(float*)&slot[8]  = -(push * hit_normal[1] - hit_pos[1]);
            *(float*)&slot[9]  = -(push * hit_normal[2] - hit_pos[2]);
            slot[10] = slot[7]; slot[0xb] = slot[8]; slot[0xc] = slot[9];
            slot[0xd] = slot[7]; slot[0xe] = slot[8]; slot[0xf] = slot[9];
            player_ai_entity_raycast((int)slot);
            *(float*)&slot[0x26] = FLOAT_80614a5c;  // reset lifetime
        } else {
            // Ricochet or terminate
            if (entity_flag == 0) {
                int can_ricochet = player_ai_projectile_random_event_tick();
                if (can_ricochet == 0) {
                    player_ai_slot_deactivate();
                    goto done;
                }
            }

            // Ricochet: reduce accuracy, offset position along normal
            float ricochet_push = FLOAT_80614bac;
            *(float*)&slot[0x3e] *= *(float*)&slot[0x3e];  // accuracy squared (degrades)
            *(float*)&slot[7]  = ricochet_push * hit_normal[0] + hit_pos[0];
            *(float*)&slot[8]  = ricochet_push * hit_normal[1] + hit_pos[1];
            *(float*)&slot[9]  = ricochet_push * hit_normal[2] + hit_pos[2];
            slot[10] = slot[7]; slot[0xb] = slot[8]; slot[0xc] = slot[9];
            slot[0xd] = slot[7]; slot[0xe] = slot[8]; slot[0xf] = slot[9];

            // Set reflected direction
            *(float*)&slot[0x19] = reflect[0];
            *(float*)&slot[0x1a] = reflect[1];
            *(float*)&slot[0x1b] = reflect[2];

            player_ai_entity_raycast((int)slot);
            *(float*)&slot[0x26] = FLOAT_80614a5c;  // reset lifetime
            slot[3] |= 0x100;     // mark as ricocheted
            slot[3] &= ~0x40;     // clear first-shot flag

            // Re-create trail/beam for ricocheted projectile
            int type = *slot;
            if (type != 0x28) {
                if (type >= 3 && type < 5) {
                    // Beam types: re-spawn beam + oriented glow
                    int beam = particle_spawn_beam(slot + 0x2b, 0, slot + 7, slot + 0x19, 0xffff);
                    slot[0x2b] = beam;
                    int glow = particle_spawn_oriented();
                    slot[0x2e] = glow;
                    particle_slot_set_opacity((double)FLOAT_80614a9c, (int*)slot[0x2e]);
                } else if ((type >= 0x1b && type < 0x20) ||
                           (type >= 0x2d && type < 0x2f)) {
                    // Trail types: re-spawn weather trail
                    int trail = weather_trail_spawn(
                        slot + 0x2e, 0, slot[1], slot + 7, slot + 0x19, 0xffff);
                    slot[0x2e] = trail;
                }
            }
        }
    }

    // Per-frame visual update
    if (*slot == 0x31) {
        // Lightning: call dedicated tick
        player_ai_projectile_lightning_tick();
    } else {
        // Update trail particle positions
        int* trail_a = (int*)slot[0x2d];
        if (trail_a != NULL && *trail_a != 0xc) {
            particle_slot_set_position((int)trail_a, slot + 10);
        }
        int* trail_b = (int*)slot[0x2e];
        if (trail_b != NULL && *trail_b != 0xc) {
            particle_slot_set_position((int)trail_b, slot + 0xd);
        }
    }

    // Final alert propagation
    player_ai_alert_check_gated(slot + 7, prev_pos, slot);

done:
    FUN_8038273c();
}

// =============================================================================
// Address: 0x800be4c8 | player_ai_projectile_homing_update
// Updates a homing/tracking projectile's target position.
// Every 10 frames, recomputes target position from tracked entity,
// performs LOS raycast to validate visibility, applies velocity-cross
// perpendicular bias (flag 0x400000), and handles sound reflection paths.
// Returns distance to target (or 0 if target not visible).
// =============================================================================
double player_ai_projectile_homing_update(int* slot, float* out_dir) {
    int target_entity;
    float target_pos[3];

    // Decrement refresh counter
    slot[0x6b] -= 1;

    // Only refresh every 10 frames
    if (slot[0x6b] >= 0) goto compute_output;

    target_entity = slot[0x67];
    if (target_entity == 0) goto compute_output;
    if ((*(unsigned int*)(target_entity + 0x28) & 0x80000000) != 0) goto compute_output;

    // Compute target position from entity (position + aim offset)
    int should_check = 1;
    target_pos[0] = *(float*)(target_entity + 100) + *(float*)(target_entity + 0x1a8);
    target_pos[1] = *(float*)(target_entity + 0x68) + *(float*)(target_entity + 0x1ac);
    target_pos[2] = *(float*)(target_entity + 0x6c) + *(float*)(target_entity + 0x1b0);

    // Type 8 (character): use bone position with height offset
    if (*(int*)(target_entity + 0x20) == 8) {
        target_pos[1] = *(float*)(*(int*)(target_entity + 0x7c) + 3000) - FLOAT_80614acc;
    }

    slot[0x6b] = 10;  // reset counter

    // Initial direction to target
    out_dir[0] = target_pos[0] - *(float*)&slot[7];
    out_dir[1] = target_pos[1] - *(float*)&slot[8];
    out_dir[2] = target_pos[2] - *(float*)&slot[9];

    // Sound reflection path check (flag 0x1000000)
    if (slot[0x41] != 0) {
        int sound_idx = *(int*)(slot[0x41] + 8);
        if (sound_idx < DAT_80612610 && (slot[3] & 0x1000000u) != 0) {
            should_check = sound_source_find_reflection(
                DAT_80611d78 + sound_idx * 0x141c, slot[0x67], target_pos);
        }
    }

    // LOS check via scene raycast
    if (should_check != 0) {
        // Temporarily disable target entity collision
        int target_ref = slot[0x2a];
        if (target_ref != 0) {
            *(unsigned int*)(target_ref + 0x28) &= 0xffffff9f;
        }

        int raycast_hit = scene_raycast(
            slot[0x3c], slot + 7, out_dir, 0,
            NULL, NULL, NULL, 0x2000070, 1, NULL);

        if (raycast_hit == slot[0x67] || (slot[3] & 0x10000000u) != 0) {
            // Target is visible - update stored target position
            *(float*)&slot[0x68] = target_pos[0];
            *(float*)&slot[0x69] = target_pos[1];
            *(float*)&slot[0x6a] = target_pos[2];
        }

        // Velocity-cross perpendicular bias (flag 0x400000)
        if ((slot[3] & 0x400000u) != 0) {
            int ent = slot[0x67];
            float vx = *(float*)(ent + 0x68) - *(float*)(ent + 0xf0);
            float vy = *(float*)(ent + 100) - *(float*)(ent + 0xec);
            float vz = *(float*)(ent + 0x6c) - *(float*)(ent + 0xf4);
            float vel_sq = vx*vx + vy*vy + vz*vz;

            float norm_vx = FLOAT_80614a5c;
            float norm_vy = FLOAT_80614a60;
            if (FLOAT_80614ba8 < vel_sq && FLOAT_80614c00 < vel_sq) {
                // Normalize velocity (Newton-Raphson rsqrt)
                float inv_len = 1.0f / __builtin_sqrtf(vel_sq);
                inv_len = inv_len * -(FLOAT_80614a78 * vel_sq * inv_len * inv_len - FLOAT_80614a7c);
                float final_scale = FLOAT_80614a60 * inv_len
                    * -(FLOAT_80614a78 * vel_sq * inv_len * inv_len - FLOAT_80614a7c);
                vz = vz * final_scale;
                norm_vx = vx * final_scale;
                norm_vy = vy * final_scale;
            }

            // Cross product: velocity x direction = perpendicular offset
            float cross_bias = FLOAT_80614ac4;
            *(float*)&slot[0x68] += cross_bias *
                (norm_vx * *(float*)&slot[0x1b] - vz * *(float*)&slot[0x1a]);
            *(float*)&slot[0x69] += cross_bias *
                (vz * *(float*)&slot[0x19] - norm_vy * *(float*)&slot[0x1b]);
            *(float*)&slot[0x6a] += cross_bias *
                (norm_vy * *(float*)&slot[0x1a] - norm_vx * *(float*)&slot[0x19]);
        }

        // Re-enable target collision
        target_ref = slot[0x2a];
        if (target_ref != 0 && (slot[3] & 0x400000u) == 0) {
            *(unsigned int*)(target_ref + 0x28) |= 0x60;
        }
    }

compute_output:
    // Compute direction to stored target
    float dx = *(float*)&slot[0x68] - *(float*)&slot[7];

    // If target is very close (XZ distance < 0.5), use direction offset instead
    float dz = *(float*)&slot[0x6a] - *(float*)&slot[9];
    if (dx*dx + dz*dz < FLOAT_80614a78) {
        float scale = FLOAT_80614a60;
        if (*slot == 0x2a) scale = FLOAT_80614bc0;  // half for type 0x2a
        *(float*)&slot[0x68] = *(float*)&slot[0x19] * scale + *(float*)&slot[7];
        *(float*)&slot[0x69] = *(float*)&slot[0x1a] * scale + *(float*)&slot[8];
        *(float*)&slot[0x6a] = *(float*)&slot[0x1b] * scale + *(float*)&slot[9];
    }

    // Output direction vector
    double zero = (double)FLOAT_80614a5c;
    out_dir[0] = *(float*)&slot[0x68] - *(float*)&slot[7];
    out_dir[1] = *(float*)&slot[0x69] - *(float*)&slot[8];
    out_dir[2] = *(float*)&slot[0x6a] - *(float*)&slot[9];

    // Return distance (sqrt of length squared)
    double len_sq = (double)(out_dir[0]*out_dir[0] + out_dir[1]*out_dir[1] + out_dir[2]*out_dir[2]);
    if (zero < len_sq) {
        return FUN_80131538(len_sq);  // sqrtf
    }
    return zero;
}
