// Address: 0x80080380 - 0x80081968 | Name: weapon_muzzle_smoke | Date: 2026-08-10
// Weapon muzzle flash, smoke trail, and scale interpolation system.
// Handles muzzle flash entity lifecycle (init, update, fade, destroy),
// smoke particle emission with 3D positional audio, volume-based scaling,
// and delayed event scheduling for weapon effects.

#include "common.h"

// ============================================================================
// External dependencies
// ============================================================================

extern void entity_register_for_update(int entity, int type, int priority);
extern void entity_destroy(void* entity);
extern void entity_destroy_with_event(void* entity);
extern void entity_set_render_type(int entity, int type);
extern void collision_set_shape_params(int* render_inst, int mask, int a, int b, int c);
extern int  character_pool_alloc(void);
extern void character_pool_free(int block);
extern void sound_init_play_params(int params);
extern unsigned int sound_play_request(int* params);
extern void sound_stop_handle(void);
extern void sound_update_3d_source(/* ... */);
extern int  rng_next(void);  // FUN_8013104c

// ============================================================================
// Address: 0x80080380 | Name: muzzle_flash_fade_tick
// Per-frame fade-out for muzzle flash entity.
// Decreases alpha over time. When alpha reaches 0, destroys entity.
// If entity has flag 0x10, transitions to "dropped weapon" state (flag 0x400).
// Also triggers muzzle effect cleanup (FUN_800ea074).
// ============================================================================
void muzzle_flash_fade_tick(void) {
    int* entity = /* resolved from register state */;
    int block = entity[0x1f];  // +0x7c

    // Decrease alpha based on frame time
    float dt_divisor = /* frame time divisor from globals */;
    *(float*)(block + 0x28) -= g_frame_dt / dt_divisor;

    if (*(float*)(block + 0x28) < 1.0f) {
        int alpha = (int)(255.0f * *(float*)(block + 0x28));

        // Check if entity has "active muzzle" flag
        if ((entity[10] & 0x10) != 0) {
            entity[10] &= ~0x10;       // clear active flag
            entity[10] |= 0x400;       // set dropped flag
            entity[10] &= ~0x60;       // clear render mode bits
            FUN_800ea074();            // muzzle effect cleanup
        }

        if (alpha < 0) {
            // Fully faded - free pool slot and destroy
            character_pool_free(block);
            entity[0x1f] = 0;
            entity_destroy(entity);
        } else {
            // Set transparency
            collision_set_shape_params((int*)entity[1], 0xf, 0xffffffff, 0xff, (char)alpha);
        }
    }
}

// ============================================================================
// Address: 0x80080484 | Name: muzzle_flash_destroy
// Cleanup + destroy for muzzle flash entity.
// Spawns a muzzle flash exit effect (FUN_800b91e4), frees character pool,
// and destroys the entity with event notification.
// ============================================================================
void muzzle_flash_destroy(int* entity) {
    // Spawn shell casing batch at current position
    FUN_800922bc();  // shell_casing_spawn_batch_with_flags caller

    // Get bone matrix position for effect spawn
    float* bone_pos = (float*)FUN_80149504(entity[0x20]);
    float pos[3];
    pos[0] = *bone_pos;
    pos[1] = bone_pos[1];
    pos[2] = bone_pos[2];

    float* bone_mtx;
    if (entity[0x20] == 0) {
        bone_mtx = (float*)(entity + 0x9d);  // fallback position
    } else {
        bone_mtx = (float*)FUN_80149590(entity[0x20]);
    }

    float dir[3];
    dir[0] = bone_mtx[4];  // Y column of bone matrix (up direction)
    dir[1] = bone_mtx[5];
    dir[2] = bone_mtx[6];

    // Spawn exit muzzle flash effect
    FUN_800b91e4(0x20, 0x17, &pos, &dir, entity[0x84], (int)entity, 1);

    // Free character data and destroy
    character_pool_free(entity[0x1f]);
    entity[0x1f] = 0;
    entity_destroy_with_event(entity);
}

// ============================================================================
// Address: 0x80080550 | Name: muzzle_flash_update_tick
// Per-frame update for muzzle flash attached to weapon bone.
// Handles: lifetime countdown, bone-relative position tracking,
// 3D sound source update, smoke velocity push, and destruction on timeout.
// If entity has a callback at +0x158, invokes it on timeout.
// ============================================================================
void muzzle_flash_update_tick(void) {
    int entity = /* resolved from register state */;
    int* data = *(int**)(entity + 0x7c);
    unsigned int bone_handle = *(unsigned int*)(entity + 0x80);

    if (g_active_player_count <= 0) return;

    float lifetime = (float)data[2];
    if (0.0f < lifetime) {
        // Decrease lifetime with frame-scaled decay
        float decay = 2.0f / lifetime;
        data[2] = (int)(float)(lifetime - decay);
        if ((float)data[2] <= 0.0f) {
            data[10] = (int)2.0f;  // mark for cleanup
        }

        // Update smoke effect position/velocity via bone
        FUN_800d4cc4((double)(float)data[2], (int*)data[0]);
    } else {
        data[2] = (int)0.0f;
        FUN_800d4cc4(0.0, (int*)data[0]);
    }

    if ((float)data[2] <= 0.0f) {
        // Lifetime expired - check if bone is still valid
        unsigned int valid = FUN_801494e0(bone_handle);
        if (valid != 0) {
            muzzle_flash_fade_tick();
            return;
        }
    } else {
        // Update bone-relative position
        float* bone_pos = (float*)FUN_80149590(bone_handle);
        float* bone_mtx = (float*)FUN_80149504(bone_handle);

        if (data[0] != 0) {
            // Transform local offset by bone matrix
            float lx = (float)data[6], ly = (float)data[7], lz = (float)data[8];
            float world_x = bone_mtx[0]*lx + bone_mtx[4]*ly + bone_mtx[8]*lz + bone_pos[0];
            float world_y = bone_mtx[1]*lx + bone_mtx[5]*ly + bone_mtx[9]*lz + bone_pos[1];
            float world_z = bone_mtx[2]*lx + bone_mtx[6]*ly + bone_mtx[10]*lz + bone_pos[2];

            // Set smoke effect position and velocity
            FUN_800d4d4c(data[0], &world_x);

            float vx = (float)data[3], vy = (float)data[4], vz = (float)data[5];
            float vel_x = bone_mtx[0]*vx + bone_mtx[4]*vy + bone_mtx[8]*vz;
            float vel_y = bone_mtx[1]*vx + bone_mtx[5]*vy + bone_mtx[9]*vz;
            float vel_z = bone_mtx[2]*vx + bone_mtx[6]*vy + bone_mtx[10]*vz;

            // Scale velocity by negative lifetime (push away as it fades)
            float push = (float)data[2] * -(float)data[9];
            vel_x *= push;
            vel_y *= push;
            vel_z *= push;
            FUN_80146144(bone_handle, &world_x, &vel_x);
        }

        // Fade-in for attached sound
        if (0.0f < (float)data[10]) {
            float dt_divisor = /* frame time divisor */;
            data[10] = (int)(float)(-g_frame_dt * (1.0f / dt_divisor) - (float)data[10]);
            if ((float)data[10] < 0.0f) {
                // Trigger callback if present
                if (*(void**)(entity + 0x158) != NULL) {
                    (*(void(**)(int,int))(entity + 0x158))(entity, 0);
                    FUN_800d4cc4(0.0, (int*)data[0]);
                    data[2] = (int)0.0f;
                }
                data[10] = (int)0.0f;
            }
        }
    }

    // Update 3D sound position
    sound_update_3d_source((float)data[2] / 10.0f, 4.0f, 1.0f,
                           entity + 0x228, entity + 100, data[1], 0);
}

// ============================================================================
// Address: 0x80080854 | Name: muzzle_flash_init
// Entity initialization callback for muzzle flash.
// Allocates character pool data, creates smoke particle effect based on
// weapon entity type (0x6ed=flamethrower, 0x34a=rocket, 0x33e=standard,
// 0x8b4-0x8b9=heavy). Configures bone-local position/velocity offsets
// and registers for per-frame update.
// ============================================================================
void muzzle_flash_init(/* entity callback params */) {
    int* init_data = /* resolved from register state */;
    int entity = init_data[0];
    unsigned int bone_handle = *(unsigned int*)(entity + 0x80);

    // Skip certain entity subtypes
    if (init_data[6] == 0x15 || init_data[6] == 0x16) return;

    // Allocate character pool data if not present
    if (*(int*)(entity + 0x7c) == 0) {
        int block = character_pool_alloc();
        *(int*)(entity + 0x7c) = block;
        if (block == 0) return;
    }

    int* data = *(int**)(entity + 0x7c);
    FUN_801493f4(bone_handle);  // bone system attach
    entity_register_for_update(entity, 0, 0);

    // If no recoil or weapon type is 0x17, use simple init
    if (*(float*)(entity + 0x1cc) <= 0.0f || init_data[10] == 0 || init_data[6] == 0x17) {
        FUN_8014a0f0(*(float*)(entity + 0x1cc));
        return;
    }

    // Get bone transform for velocity computation
    float* bone_pos = (float*)FUN_80149504(bone_handle);
    float* bone_mtx = (float*)FUN_80149590(bone_handle);

    // Reset weapon state
    *(float*)(entity + 0x1cc) = 0.0f;
    *(float*)(entity + 0x1d8) = 0.0f;
    *(int*)(entity + 0x210) = init_data[1];

    // Compute initial velocity direction from init data or random
    float vel[3];
    float* vel_src = (float*)init_data[0xb];
    if (vel_src == NULL) {
        vel_src = (float*)init_data[0xc];
        if (vel_src == NULL) {
            // Random direction scaled by 8.0
            vel[0] = 8.0f * ((float)rng_next() / (float)RNG_MAX - 0.5f) * 0.25f;
            vel[1] = 8.0f * ((float)rng_next() / (float)RNG_MAX - 0.5f) * 0.25f;
            vel[2] = 8.0f * ((float)rng_next() / (float)RNG_MAX - 0.5f) * 0.25f;
        } else {
            vel[0] = 5.0f * vel_src[0];
            vel[1] = 5.0f * vel_src[1];
            vel[2] = 5.0f * vel_src[2];
        }
    } else {
        vel[0] = 0.25f * vel_src[0];
        vel[1] = 0.25f * vel_src[1];
        vel[2] = 0.25f * vel_src[2];
    }

    // Kill existing smoke effect
    if ((int*)data[0] != NULL) {
        FUN_800d49d8((int*)data[0]);
    }

    // Select smoke effect based on weapon entity type
    int effect_id = -1;
    unsigned short etype = *(unsigned short*)(entity + 0x94);
    if (etype == 0x6ed) {
        effect_id = 0x22;
        data[1] = 0x38f;  // flamethrower sound ID
    } else if (etype == 0x33e) {
        effect_id = 0x22;
        data[1] = 0x38f;  // standard muzzle
    } else if (etype == 0x34a || (etype > 0x8b3 && etype < 0x8ba)) {
        effect_id = 0x23;
        data[1] = 0x51;   // rocket/heavy sound ID
        // Random lifetime multiplier
        float lifetime = 3.0f + (float)rng_next() / (float)RNG_MAX;
        // ... additional setup for rocket smoke
    }

    if (effect_id >= 0) {
        int is_split_screen = FUN_801c1714();
        int flags = (is_split_screen == 1) ? 0 : 0x80000;
        int effect = FUN_800d6300(0, effect_id, flags, (float*)init_data[10], &vel, NULL, NULL);
        data[0] = effect;
        data[2] = (int)10.0f;   // initial lifetime (max scale)
        data[9] = (int)0.25f;   // velocity push factor
        data[10] = (int)0.0f;   // fade timer
        FUN_800d4cc4((double)(float)data[2], (int*)data[0]);
    }

    // Transform position offset into bone-local space
    float* fire_pos = (float*)init_data[10];
    float dx = fire_pos[0] - bone_pos[0];
    float dy = fire_pos[1] - bone_pos[1];
    float dz = fire_pos[2] - bone_pos[2];
    // Multiply by inverse bone rotation (transpose)
    data[6] = (int)(bone_mtx[2]*dz + bone_mtx[0]*dx + bone_mtx[1]*dy);
    data[7] = (int)(bone_mtx[6]*dz + bone_mtx[4]*dx + bone_mtx[5]*dy);
    data[8] = (int)(bone_mtx[10]*dz + bone_mtx[8]*dx + bone_mtx[9]*dy);
    // Local velocity
    data[3] = (int)(bone_mtx[2]*vel[2] + bone_mtx[0]*vel[0] + bone_mtx[1]*vel[1]);
    data[4] = (int)(bone_mtx[6]*vel[2] + bone_mtx[4]*vel[0] + bone_mtx[5]*vel[1]);
    data[5] = (int)(bone_mtx[10]*vel[2] + bone_mtx[8]*vel[0] + bone_mtx[9]*vel[1]);

    entity_register_for_update(entity, 4, -0x7ff7fab0);
}

// ============================================================================
// Address: 0x80080cb8 | Name: weapon_get_scaled_volume
// Computes scaled bounding volume: 8^3 * scale_x * scale_y * scale_z.
// Used for smoke emission rate calculations.
// ============================================================================
double weapon_get_scaled_volume(int entity) {
    float sx = *(float*)(entity + 0x1c0);
    float sy = *(float*)(entity + 0x1c4);
    float sz = *(float*)(entity + 0x1c8);
    return (double)(8.0f * 8.0f * 8.0f * sz * sx * sy);
}

// ============================================================================
// Address: 0x80080ce0 | Name: weapon_compute_fill_delta
// Computes how much the entity's current scale differs from its target fill.
// Uses: entity Y position + height offset + collision offset vs param_1 (water level?),
// then computes partial volume and clamps to full volume.
// Returns delta between computed volume and current stored volume.
// ============================================================================
double weapon_compute_fill_delta(double fill_level, int entity) {
    int data = *(int*)(entity + 0x7c);

    // Compute effective height
    float height = *(float*)(entity + 0x1c4) + *(float*)(entity + 0x68) +
                   *(float*)(entity + 0x1ac);
    double full_vol = weapon_get_scaled_volume(entity);

    // Partial volume based on fill level
    double partial = (double)(8.0f * 8.0f * *(float*)(entity + 0x1c8) *
                             (float)(height - fill_level) * *(float*)(entity + 0x1c0));

    // Clamp to full volume
    if (full_vol < partial) {
        partial = full_vol;
    }

    // Update max if exceeded
    if ((double)*(float*)(data + 0x9c) < partial) {
        *(float*)(data + 0x9c) = (float)partial;
    }

    return (double)(float)(partial - (double)*(float*)(data + 0x98));
}

// ============================================================================
// Address: 0x80080d94 | Name: weapon_smoke_stop_all
// Stops all active smoke particles (up to 10 slots) and kills the looping sound.
// Iterates particle slot array at data+0x11c, destroys each alive particle.
// ============================================================================
void weapon_smoke_stop_all(void) {
    int entity = /* resolved from register state */;
    int data = *(int*)(entity + 0x7c);

    // Kill all 10 particle slots
    for (int i = 0; i < 10; i++) {
        int particle = *(int*)(data + 0x11c + i * 4);
        if (particle_is_alive(&particle)) {
            particle_destroy(&particle);
        }
    }

    // Stop looping smoke sound
    if (*(int*)(data + 0xf0) != -1) {
        sound_stop_handle();
        *(int*)(data + 0xf0) = -1;
    }
}

// ============================================================================
// Address: 0x80080e20 | Name: weapon_smoke_tick
// MASSIVE per-frame smoke emission system.
// Computes fill delta from weapon_compute_fill_delta or volume difference.
// If fill delta positive (growing): spawns smoke particles from circular
// array of 10 slots, each with position randomized around entity center,
// scaled by entity dimensions. Plays looping smoke sound.
// If fill delta negative or stable: manages existing particle lifetime,
// spawns replacement particles at entity surface, handles sound crossfade.
// Also handles pre-fill callback invocation and scale lerp registration.
// ============================================================================
void weapon_smoke_tick(void) {
    int* context = /* resolved from register state */;
    int entity = context[0];
    int data = *(int*)(entity + 0x7c);

    double full_vol = weapon_get_scaled_volume(entity);
    int weapon_id = context[6];
    double fill_delta = 0.0;

    // Compute fill delta based on weapon config
    if ((weapon_id == -1) || ((*(unsigned int*)(weapon_table + weapon_id * 0x40 + 0x1c) & 0x80) == 0)
        || (context[10] == 0)) {
        // No fill source - use volume difference
        if ((weapon_id != -1) && ((*(unsigned int*)(weapon_table + weapon_id * 0x40 + 0x1c) & 2) != 0)) {
            fill_delta = (double)(float)(full_vol - *(float*)(data + 0x98));
        }
    } else {
        fill_delta = weapon_compute_fill_delta((double)*(float*)(context[10] + 4), entity);
    }

    // Handle first-fire sound
    if (weapon_id == 0) {
        // Play ignition sound at entity position
        sound_init_play_params(sound_params);
        sound_params[0] = 0xd9d;  // ignition SFX
        // ... position from entity
        sound_play_request(sound_params);
    }
    // Handle active fill (smoke rising)
    else if (fill_delta > 0.0f && weapon_id != -1 &&
             (*(unsigned int*)(weapon_table + weapon_id * 0x40 + 0x1c) & 0x80) != 0 &&
             *(float*)(entity + 0x1cc) > 0.0f && context[10] != 0 && context[0xb] != 0) {

        // Accumulate fill rate
        *(float*)(entity + 0x1cc) -= 0.3f * (float)context[8] - *(float*)(entity + 0x1cc);
        if (*(float*)(entity + 0x1cc) < 0.5f) {
            *(float*)(entity + 0x1cc) = 0.5f;
        }

        // Clamp delta to full volume
        if (full_vol < fill_delta) fill_delta = full_vol;
        float norm_delta = (float)(fill_delta / *(float*)(data + 0x10));
        if (norm_delta > 0.3f) {
            // Destroy old particles, spawn new batch
            for (int i = 0; i < 10; i++) {
                int p = *(int*)(data + 0x11c + i * 4);
                if (particle_is_alive(&p)) {
                    particle_destroy(&p);
                }
            }

            // Spawn particles based on fill amount
            int num_particles = (int)(0.015f * (float)(fill_delta / full_vol));
            if (num_particles < 8) num_particles = 8;

            for (unsigned int i = 0; i < (unsigned int)num_particles; i++) {
                float spawn_pos[3];
                spawn_pos[0] = 0.25f * ((float)rng_next() / (float)RNG_MAX - 0.5f)
                               + *(float*)(entity + 100);
                spawn_pos[1] = 0.7f * *(float*)(entity + 0x1c4) *
                               ((float)rng_next() / (float)RNG_MAX)
                               + *(float*)(entity + 0x68) + *(float*)(entity + 0x1ac);
                spawn_pos[2] = 0.25f * ((float)rng_next() / (float)RNG_MAX - 0.5f)
                               + *(float*)(entity + 0x6c);

                // Normalize spawn direction
                float dir[3] = {spawn_pos[0], spawn_pos[1], spawn_pos[2]};
                // ... fast inverse sqrt normalization ...

                // Create particle from circular effect table
                int slot = *(int*)(data + 0x94);
                int effect_base = *(int*)(data + 0xa0);
                particle_create((int*)(data + slot * 4 + 0x11c),
                                effect_table + (effect_base + (int)i % 3) * 0x84,
                                0x1c00, &spawn_pos, &dir);
                // Set particle color, scale, depth
                particle_set_color(*(int*)(data + slot * 4 + 0x11c), *(int*)(data + 0xbc));
                // ... additional particle config ...

                // Advance circular index (mod 10 via bit tricks)
                *(int*)(data + 0x94) = (*(int*)(data + 0x94) + 1) % 10;
            }
        }

        // Register for continued per-frame updates
        *(unsigned int*)(entity + 0x28) |= 0x4000000;
        entity_register_for_update(entity, 3, -0x7ff7e7c0);

        // Start looping smoke sound if not already playing
        if (*(int*)(data + 0xf0) == -1) {
            sound_init_play_params(sound_params);
            sound_params[0] = 0xbfc;  // default smoke loop
            if (*(int*)(data + 0xa0) == 5) {
                sound_params[0] = 0x349;  // fire crackle variant
            }
            // ... set 3D position, volume, flags ...
            unsigned int handle = sound_play_request(sound_params);
            *(unsigned int*)(data + 0xf0) = handle;
        }
    }
    // Handle shrinking/stable (no new fill)
    else {
        // Kill existing particles, stop sound, trigger callback
        // ... (particle cleanup, render flag changes, scale lerp setup) ...
        weapon_smoke_stop_all();
        *(unsigned int*)(*(int*)(entity + 4) + 0x30) &= ~0x6;
        *(unsigned int*)(*(int*)(entity + 4) + 0x30) |= 0x8;
        *(float*)(data + 0x9c) = (float)full_vol;
        entity_set_render_type(entity, 0);
        *(float*)(entity + 0x1cc) = 0.0f;
        FUN_800922bc();  // shell casing spawn
    }

    // Invoke pre-fill callback if present and fill complete
    if (*(void**)(entity + 0x158) != NULL && *(float*)(entity + 0x1cc) <= 0.0f) {
        (*(void(**)(int,int))(entity + 0x158))(entity, 0);
        entity_register_for_update(entity, 1, 0);
    }
}

// ============================================================================
// Address: 0x80081840 | Name: weapon_scale_lerp_tick
// Per-frame linear interpolation of entity scale toward target.
// Increments stored scale (data+0x98) by rate (data+0x10) * dt each frame.
// When scale reaches target (data+0x9c), clears update flag and stops sound.
// Also checks pre-fill callback threshold for early invocation.
// ============================================================================
void weapon_scale_lerp_tick(int entity) {
    int data = *(int*)(entity + 0x7c);

    // Integrate scale: current += rate * dt
    float rate = *(float*)(data + 0x10);
    float dt_divisor = /* frame time divisor */;
    *(float*)(data + 0x98) += (1.0f / dt_divisor) * rate * g_frame_dt + *(float*)(data + 0x98);

    // Check if reached target
    if (*(float*)(data + 0x9c) < *(float*)(data + 0x98)) {
        *(float*)(data + 0x98) = *(float*)(data + 0x9c);
        *(unsigned int*)(entity + 0x28) &= ~0x4000000;  // clear update flag
        entity_register_for_update(entity, 3, 0);

        // Stop sound if playing
        if (*(int*)(data + 0xf0) >= 0) {
            sound_stop_handle();
            *(int*)(data + 0xf0) = -1;
        }
    }

    // Check pre-fill callback threshold
    if (*(int*)(entity + 0x158) != 0) {
        double full_vol = weapon_get_scaled_volume(entity);
        float threshold = (*(float*)(entity + 0x1dc) / *(float*)(entity + 0x1d8)) * (float)full_vol;
        if (threshold < *(float*)(data + 0x98)) {
            (*(void(**)(int,int))(entity + 0x158))(entity, 0);
            entity_register_for_update(entity, 1, 0);
            *(unsigned int*)(entity + 0x28) &= ~0x4000000;
            sound_stop_handle();
            *(int*)(data + 0xf0) = -1;
        }
    }
}

// ============================================================================
// Address: 0x80081968 | Name: weapon_start_delayed_event
// Registers a delayed event on an entity.
// Stores flags and frame-count timer at entity+0x1a0/0x1a4,
// then registers for per-frame update with priority -0x7ff7e628.
// param_1: delay in seconds (converted to frames via dt divisor)
// param_2: entity pointer
// param_3: event flags (must be non-zero to activate)
// ============================================================================
void weapon_start_delayed_event(double delay_seconds, int entity, unsigned int flags) {
    if (flags != 0) {
        *(unsigned int*)(entity + 0x1a0) = flags | 1;  // set active bit
        *(int*)(entity + 0x1a4) = (int)(delay_seconds * /* frame time divisor */);
        entity_register_for_update(entity, 3, -0x7ff7e628);
    }
}
