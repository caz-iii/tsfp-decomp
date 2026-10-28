// Address: 0x800819d8 - 0x800824ac | Name: weapon_effects_system | Date: 2026-08-10
// Weapon effects subsystem: dropped weapon lifecycle, smoke trails,
// particle effect definitions, fire/impact dispatch, and system init/shutdown.

#include "common.h"

// ============================================================================
// External dependencies
// ============================================================================

extern void entity_register_for_update(int entity, int type, int priority);
extern void entity_destroy_with_event(void* entity);
extern void entity_set_render_type(int entity, int type);
extern void collision_set_shape_params(int* render_inst, int mask, int a, int b, int c);
extern void event_set_handler(unsigned int type, int slot, void* handler);
extern void weapon_smoke_tick(void);
extern void weapon_smoke_stop_all(void);
extern void weapon_start_delayed_event(double delay, int entity, unsigned int flags);
extern int  rng_next(void);

// Particle system
extern void particle_create(int* slot, void* def, int flags, float* pos, float* dir);
extern bool particle_is_alive(int* handle);
extern void particle_destroy(int* handle);

// ============================================================================
// Address: 0x800819d8 | Name: weapon_dropped_fade_tick
// Per-frame update for a dropped weapon entity fading out.
// Sets flag 0x40, handles bone detach (flag 2), transitions to dropped state
// (flag 0x400 + clears render bits), fades alpha, and destroys when done.
// Called from entity update callback registered by weapon drop system.
// ============================================================================
void weapon_dropped_fade_tick(int* entity) {
    entity[6] |= 0x40;  // mark as fading

    unsigned int flags = entity[0x68];
    if ((flags & 1) != 0) {
        // Check wait-for-animation flag
        if ((flags & 4) != 0) {
            if ((entity[4] & 0xffff) != 0) {
                return;  // animation still playing
            }
            entity[0x68] = flags & ~4;  // clear wait flag
        }

        // Check bone-attached flag
        if (entity[0x20] != 0 && (entity[0x68] & 2) != 0) {
            unsigned int valid = FUN_801494e0(entity[0x20]);
            if (valid == 0) {
                return;  // bone still valid, keep attached
            }
            entity[0x68] &= ~2;  // clear bone-attached flag
            FUN_80144a9c((int)entity);  // detach from bone
        }
    }

    // Transition to dropped render state if not already
    if ((entity[10] & 0x400) == 0) {
        entity[10] |= 0x400;   // set dropped flag
        FUN_800ea074();         // weapon render mode change
    }

    // Alpha fade countdown
    int fade_timer = entity[0x69];
    if (fade_timer > 0) {
        int dt = g_frame_counter;
        if (dt > fade_timer) dt = fade_timer;
        entity[0x69] -= dt;

        if ((int)entity[0x69] < 0x7f) {
            collision_set_shape_params((int*)entity[1], 0xf, 0xffffffff, 0xff, (char)entity[0x69]);
        }
    }

    // Destroy when fully faded or no longer flagged
    if ((entity[0x68] & 1) == 0 || (int)entity[0x69] < 1) {
        entity_destroy_with_event(entity);
    }
}

// ============================================================================
// Address: 0x80081af4 | Name: weapon_effect_particle_defs_init
// Initializes two particle effect definitions (smoke + fire) and two
// linked sub-effects. Uses particle definition builder functions:
// FUN_800e70d0 (create base def), FUN_800e71c4 (set color/alpha curves),
// FUN_800e7334 (set scale curves), FUN_800e73ec (set velocity params),
// FUN_800e72ec (set lifetime/emission params), FUN_800e6eb4 (create linked effect),
// FUN_800e7040 (set linked effect params).
// Called from weapon_system_preload_resources.
// ============================================================================
void weapon_effect_particle_defs_init(void) {
    // Smoke particle definition (at DAT_8055d208)
    FUN_800e70d0(1.5f, 1.0f, &g_smoke_particle_def, 0, 0, 1, 0x326, 0);
    FUN_800e71c4(6.0f, 0.0f, 0.0f, 0.25f,
                 0x1818181f, 0x10101020, 0x4040410);  // gray smoke colors
    FUN_800e7334(0.0f, 4.0f, 0.01f, 0.008f);         // scale: start small, grow
    FUN_800e73ec(0.15f, 0.7f, 3.0f, 0.0f);           // velocity: upward drift
    FUN_800e72ec(1.8f, 0.005f, 1.5f, 0.01f, 1.0f, 8.0f);  // lifetime/emission

    // Fire particle definition (at DAT_8055d2d4)
    FUN_800e70d0(0.8f, 1.0f, &g_fire_particle_def, 0, 1, 1, 0x331, 0);
    FUN_800e71c4(6.0f, 0.0f, 1.0f, 0.1f,
                 0x7346416e, 0x3c221e00, 0xa00000a);  // orange/red fire colors
    FUN_800e7334(0.0f, 4.0f, 0.01f, 0.008f);
    FUN_800e73ec(0.0f, 0.7f, 3.0f, 0.0f);
    FUN_800e72ec(0.8f, 0.01f, 0.25f, 0.5f, 1.0f, 1.0f);

    // Linked sub-effect: smoke trail (at DAT_8055d3a0, linked to smoke def)
    FUN_800e6eb4(8.0f, 0.01f, 0.35f, &g_smoke_trail_def, &g_smoke_particle_def, 0, 1);
    FUN_800e7040(0.25f, 0.5f, 2.5f, 0.0f);

    // Linked sub-effect: fire trail (at DAT_8055d424, linked to fire def)
    FUN_800e6eb4(1.0f, 0.5f, 0.6f, &g_fire_trail_def, &g_fire_particle_def, 0, 1);
    FUN_800e7040(0.25f, 0.5f, 3.0f, 0.01f);
}

// ============================================================================
// Address: 0x80081cb0 | Name: weapon_dropped_smoke_trail_tick
// Per-frame smoke trail update for a dropped weapon in flight.
// Updates 2 particle slots with entity position and bone-up direction.
// If volume delta goes negative (landed), schedules delayed destroy.
// Otherwise: randomizes velocity with bone-biased direction,
// pushes entity via bone handle, and accumulates fill volume over time.
// ============================================================================
void weapon_dropped_smoke_trail_tick(void) {
    int entity = /* resolved from register state */;
    int bone_mtx_ptr = FUN_80149590(*(unsigned int*)(entity + 0x80));
    int data = *(int*)(entity + 0x7c);

    // Get bone up-direction (negated Y column)
    float up_dir[3];
    up_dir[0] = -*(float*)(bone_mtx_ptr + 0x10);
    up_dir[1] = -*(float*)(bone_mtx_ptr + 0x14);
    up_dir[2] = -*(float*)(bone_mtx_ptr + 0x18);

    // Get entity scale for volume calc
    float* scale = (float*)FUN_80149568(*(unsigned int*)(entity + 0x80));
    double vol_delta = (double)(8.0f * 8.0f * 8.0f * scale[2] * scale[0] * scale[1]
                               - *(float*)(data + 0x98));

    // Update 2 particle slots with current position and direction
    for (int i = 0; i < 2; i++) {
        int particle = *(int*)(data + 0x11c + i * 4);
        if (particle_is_alive(&particle)) {
            FUN_800e3fb8(&particle, (float*)(entity + 100));  // set position
            FUN_800e3fdc(&particle, &up_dir);                 // set direction
        }
    }

    if (vol_delta <= 0.0f) {
        // Object has landed/stopped growing - schedule delayed destroy
        unsigned int valid = FUN_801494e0(*(unsigned int*)(entity + 0x80));
        if (valid != 0) {
            weapon_start_delayed_event(0.25, entity, 6);
        }
    } else {
        // Still in flight: randomize velocity with bone bias
        float vel[3];
        vel[0] = 8.0f * ((float)rng_next() / (float)RNG_MAX - 0.5f);
        vel[1] = 8.0f * ((float)rng_next() / (float)RNG_MAX - 0.5f);
        vel[2] = 8.0f * ((float)rng_next() / (float)RNG_MAX - 0.5f);

        // Blend random velocity with bone up-direction
        vel[0] = 0.75f * *(float*)(bone_mtx_ptr + 0x10) + vel[0] * 0.3f;
        vel[1] = 0.75f * *(float*)(bone_mtx_ptr + 0x14) + vel[1] * 0.3f;
        vel[2] = 0.75f * *(float*)(bone_mtx_ptr + 0x18) + vel[2] * 0.3f;

        // Push entity via bone handle
        FUN_80146144(*(unsigned int*)(entity + 0x80), (float*)(entity + 100), &vel);

        // Accumulate fill volume over time
        float dt_divisor = /* frame time divisor */;
        *(float*)(data + 0x98) += 3.0f * g_frame_dt * (1.0f / dt_divisor)
                                  + *(float*)(data + 0x98);
    }
}

// ============================================================================
// Address: 0x80081f28 | Name: weapon_dropped_start_effects
// MASSIVE: Initiates visual effects when a weapon is dropped/destroyed.
// Clears render flags, stops smoke, spawns shell casings, creates bone handle,
// spawns 2 particle effects (fire trail + smoke trail) from bone direction,
// initializes volume tracking, spawns muzzle flash exit effect,
// and registers for per-frame update (priority -0x7ff7e350).
// If bone allocation fails, schedules delayed event instead.
// Called from weapon_fire_smoke_dispatch and weapon_impact_smoke_dispatch.
// ============================================================================
void weapon_dropped_start_effects(void) {
    int entity = /* resolved from register state */;
    int data = *(int*)(entity + 0x7c);

    // Clear render mode to transparent
    *(unsigned int*)(*(int*)(entity + 4) + 0x30) &= ~1;
    *(unsigned int*)(*(int*)(entity + 4) + 0x30) |= 2;
    *(float*)(entity + 0x1cc) = 0.0f;
    entity_set_render_type(entity, 0);
    *(unsigned int*)(entity + 0x28) &= ~0x10;

    // Stop any existing smoke effects
    weapon_smoke_stop_all();

    // Check for special weapon flag
    if ((*(unsigned int*)(entity + 0x28) & 0x10000) != 0) {
        FUN_800ba2e0();  // special weapon cleanup
    }

    // Spawn shell casings
    FUN_800922bc();

    // Reset bone/physics state
    FUN_80149c50();

    // Try to allocate physics body
    unsigned int phys_valid = FUN_8014f9f8(1);
    if (phys_valid == 0) {
        // No physics available - schedule delayed destroy
        weapon_start_delayed_event(8.0, entity, 8);
        return;
    }

    // Create bone handle if not present
    if (*(int*)(entity + 0x80) == 0) {
        int handle = FUN_80144864();
        *(int*)(entity + 0x80) = handle;
        FUN_80145090(/* params: bone handle, entity, mode=1, joint=-1, mass */);
    }

    // Get bone up-direction (negated)
    int bone_mtx = FUN_80149590(*(unsigned int*)(entity + 0x80));
    float up_dir[3];
    up_dir[0] = -*(float*)(bone_mtx + 0x10);
    up_dir[1] = -*(float*)(bone_mtx + 0x14);
    up_dir[2] = -*(float*)(bone_mtx + 0x18);

    // Spawn fire trail particle (slot 0x11c)
    particle_create((int*)(data + 0x11c), &g_fire_trail_def, 0x1006,
                    (float*)(entity + 100), &up_dir);
    // Spawn smoke trail particle (slot 0x120)
    particle_create((int*)(data + 0x120), &g_smoke_trail_def, 0x1006,
                    (float*)(entity + 100), &up_dir);

    // Enable both particles
    FUN_800e3ec0((int*)(data + 0x11c));
    FUN_800e3ec0((int*)(data + 0x120));

    // Initialize volume tracking from entity scale
    float* scale = (float*)FUN_80149568(*(unsigned int*)(entity + 0x80));
    double full_vol = (double)(8.0f * 8.0f * 8.0f * scale[2] * scale[0] * scale[1]);
    float rng_offset = 0.3f * ((float)rng_next() / (float)RNG_MAX);
    *(float*)(data + 0x98) = (float)(0.3 * full_vol + (double)rng_offset);

    // Set particle scale based on volume delta
    float vol_delta = (float)(full_vol - (double)*(float*)(data + 0x98));
    FUN_800e42d4((double)(6.0f * vol_delta), (int*)(data + 0x11c));
    FUN_800e42d4((double)(6.0f * vol_delta), (int*)(data + 0x120));

    // Spawn muzzle flash exit effect at entity center + offset
    float effect_pos[3];
    effect_pos[0] = *(float*)(entity + 100) + *(float*)(entity + 0x1a8);
    effect_pos[1] = *(float*)(entity + 0x68) + *(float*)(entity + 0x1ac);
    effect_pos[2] = *(float*)(entity + 0x6c) + *(float*)(entity + 0x1b0);
    FUN_800b91e4(10, 0x17, &effect_pos, &up_dir, *(int*)(entity + 0x210), entity, 1);

    // Register for per-frame smoke trail update
    entity_register_for_update(entity, 3, -0x7ff7e350);
}

// ============================================================================
// Address: 0x800821d8 | Name: weapon_fire_smoke_dispatch
// Dispatches weapon fire effects based on entity state.
// If weapon type has 0x100000 flag set (and not type 0x17) and no 0x800 weapon
// flag: calls weapon_smoke_tick for active smoke, else weapon_dropped_start_effects.
// Decrements recoil value (entity+0x1cc) by param_9[8] each call.
// ============================================================================
void weapon_fire_smoke_dispatch(int* param_9) {
    int entity = param_9[0];

    // Skip if entity has certain conditions
    if (((*(unsigned int*)(entity + 0x9c) & 0x100000) == 0) ||
        (param_9[6] == 0x17) ||
        (FUN_800b187c(param_9[6]) & 0x800) != 0) {
        if (param_9[6] == 0x17) return;
    }

    // Store weapon source ID
    *(int*)(entity + 0x210) = param_9[1];

    // Decrement recoil/fill timer
    float prev = *(float*)(entity + 0x1cc);
    *(float*)(entity + 0x1cc) = prev - (float)param_9[8];

    if (0.0f < *(float*)(entity + 0x1cc) ||
        (*(unsigned int*)(*(int*)(entity + 4) + 0x30) & 1) == 0) {
        // Still has recoil - trigger smoke emission
        *(float*)(entity + 0x1cc) = SPECIAL_MARKER;  // 0x806141ec
        weapon_smoke_tick();
        *(float*)(entity + 0x1cc) = prev - (float)param_9[8];  // restore
    } else {
        // Recoil done - start dropped weapon effects
        weapon_dropped_start_effects();
    }
}

// ============================================================================
// Address: 0x800822ac | Name: weapon_impact_smoke_dispatch
// Dispatches weapon impact effects.
// If weapon has 0x100000 flag (and not type 0x17, no 0x800 flag): returns early
// (impact handled by another system). Otherwise checks if weapon type changed
// or delayed event pending - resets fill. If fill <= 0 and render flag set,
// calls weapon_dropped_start_effects.
// ============================================================================
void weapon_impact_smoke_dispatch(int* param_9) {
    int entity = param_9[0];

    // Check if this weapon type handles its own impact
    if ((*(unsigned int*)(entity + 0x9c) & 0x100000) != 0 &&
        param_9[6] != 0x17 &&
        (FUN_800b187c(param_9[6]) & 0x800) == 0) {
        return;  // handled by fire dispatch
    }

    *(int*)(entity + 0x210) = param_9[1];

    // Reset fill if weapon type changed or delayed event active
    if (param_9[2] == 0 ||
        *(short*)(param_9[2] + 0x94) != *(short*)(entity + 0x94) ||
        *(int*)(entity + 0x1a0) != 0) {
        *(float*)(entity + 0x1cc) = 0.0f;
    }

    // If fill depleted and render flag active, start dropped effects
    if (*(float*)(entity + 0x1cc) <= 0.0f &&
        (*(unsigned int*)(*(int*)(entity + 4) + 0x30) & 1) != 0) {
        weapon_dropped_start_effects();
    }
}

// ============================================================================
// Address: 0x8008236c | Name: weapon_dropped_spin_tick
// Per-frame rotation for a dropped spinning weapon entity.
// Iterates over all sub-meshes and scales them uniformly by data[0x10].
// Applies angular velocity (FLOAT_80614194 * dt) to yaw and pitch.
// Sets collision to transparent mode (mask=2, layer=2).
// ============================================================================
void weapon_dropped_spin_tick(void) {
    int entity = /* resolved from register state */;
    int data = *(int*)(entity + 0x7c);

    // Scale all sub-meshes uniformly
    int sub_mesh_count = **(int**)(*(int*)(entity + 4) + 4);
    float scale = *(float*)(data + 0x10);
    for (int i = 0; i < sub_mesh_count; i++) {
        FUN_8012f62c(scale, scale, scale);  // set sub-mesh scale
    }

    // Apply angular velocity to rotation
    float angular_vel = 0.02f;  // FLOAT_80614194
    *(float*)(entity + 0xc0) += angular_vel * g_frame_dt;
    *(float*)(entity + 0x70) += angular_vel * g_frame_dt;

    // Set collision to transparent/pickup mode
    collision_set_shape_params(*(int**)(entity + 4), 2, 0, 2, 0);
}

// ============================================================================
// Address: 0x8008241c | Name: weapon_effects_register_events
// Registers 3 event handlers for weapon effects (event type 0x4000000).
// Slot 0: FUN_800824d4 (weapon create/spawn handler)
// Slot 3: LAB_80082568 (weapon think/update handler)
// Slot 1: FUN_80082524 (weapon destroy handler)
// Called from game_tick_update during initialization.
// ============================================================================
void weapon_effects_register_events(void) {
    event_set_handler(0x4000000, 0, FUN_800824d4);  // spawn
    event_set_handler(0x4000000, 3, &LAB_80082568);  // think
    event_set_handler(0x4000000, 1, FUN_80082524);  // destroy
}

// ============================================================================
// Address: 0x80082474 | Name: weapon_effects_system_reset
// Resets the weapon effects subsystem.
// Initializes particle definition tables (0x24 bytes at DAT_8055d4a8)
// and secondary table (0x10 bytes at DAT_8055d4b8).
// Called from entity_system_reset.
// ============================================================================
void weapon_effects_system_reset(void) {
    FUN_80133b18((int*)&g_weapon_effects_state, 0x24,
                 (int*)&g_weapon_effects_secondary, 0x10);
}

// ============================================================================
// Address: 0x800824ac | Name: weapon_effects_system_shutdown
// Shuts down the weapon effects subsystem.
// Frees resources tracked in g_weapon_effects_state.
// Called from entity_system_shutdown.
// ============================================================================
void weapon_effects_system_shutdown(void) {
    FUN_80133b50((int*)&g_weapon_effects_state);
}

// ============================================================================
// Address: 0x800824d4 | Name: weapon_effects_on_spawn
// Event handler (slot 0) for weapon effects entity type 0x4000000.
// Allocates a 0x24-byte data block from the weapon effects pool
// and attaches it to entity+0x7c. Zeroes the block on success.
// ============================================================================
void weapon_effects_on_spawn(int entity) {
    int* data = FUN_80133b88(WEAPON_EFFECTS_POOL_ID);
    *(int**)(entity + 0x7c) = data;
    if (*(int*)(entity + 0x7c) != 0) {
        memset_wrapper(*(int*)(entity + 0x7c), 0, 0x24);
    }
}

// ============================================================================
// Address: 0x80082524 | Name: weapon_effects_on_destroy
// Event handler (slot 1) for weapon effects entity type 0x4000000.
// Frees the 0x24-byte data block back to the weapon effects pool.
// ============================================================================
void weapon_effects_on_destroy(int entity) {
    if (entity != 0) {
        FUN_80133bb0(WEAPON_EFFECTS_POOL_ID, *(int**)(entity + 0x7c));
        *(int*)(entity + 0x7c) = 0;
    }
}
