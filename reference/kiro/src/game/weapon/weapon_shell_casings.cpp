// Address: 0x8007ea0c - 0x8007ff84 | Name: weapon_shell_casings | Date: 2026-08-10
// Shell casing / weapon debris particle system
// Handles spawning, physics simulation, angular spin, and pool management for
// ejected shell casings and weapon debris entities.

#include "common.h"

// ============================================================================
// Globals
// ============================================================================

static int g_shell_pool_count;           // DAT_80611c78 - active shell casing count
static int g_shell_pool_active;          // DAT_80611c7c - number of live shell entities
static int g_shell_pool_ring_index;      // DAT_80611c8c - ring buffer write index
static int g_shell_pool_ring_capacity;   // DAT_80611c88 - ring buffer max capacity
static int g_shell_pool_ring_base;       // DAT_80611c90 - ring buffer memory base
static int g_shell_pool_alloc_table;     // DAT_80611c94 - allocation table pointer
static int g_shell_entity_list[20];      // DAT_8055a330 - live entity pointer array
static int g_shell_ring_entities[20];    // DAT_8055a380 - ring buffer entity array

// ============================================================================
// Forward declarations
// ============================================================================

extern int entity_pool_validate_index(int index);
extern void entity_spawn_by_type(/* ... */);
extern void entity_register_for_update(int entity, int type, int priority);
extern void entity_destroy_with_event(void* entity);
extern void collision_set_shape_params(int* render_inst, int mask, int a, int b, int c);
extern void render_instance_set_material(float val, int* render_inst, int slot);
extern int  rng_next(void);              // FUN_8013104c
extern float math_acos(float dot);       // FUN_801314f4
extern int  particle_spawn(/* ... */);   // FUN_800cd838
extern void memset_wrapper(int dest, int val, int size);
extern int  mem_alloc(/* ... */);

// ============================================================================
// Address: 0x8007ea0c | Name: shell_casing_spin_update
// Per-frame angular velocity integration with clamping and wrap-around.
// Applies angular damping and enforces min/max angle constraints.
// Called from entity update callback (via 0x801cfb84).
// ============================================================================
void shell_casing_spin_update(int entity) {
    int data = *(int*)(entity + 0x7c);

    // Clamp angular velocity to [-1.0, 1.0]
    if (1.0f < *(float*)(data + 0xd8)) {
        *(float*)(data + 0xd8) = 1.0f;
    }
    if (*(float*)(data + 0xd8) < -1.0f) {
        *(float*)(data + 0xd8) = -1.0f;
    }

    // Integrate angular velocity into angle
    float prev_angle = *(float*)(data + 0xd4);
    *(float*)(data + 0xd4) = *(float*)(data + 0xd8) * *(float*)(data + 0xc8) + prev_angle;

    float angle = *(float*)(data + 0xd4);
    float correction;

    // Wrap angle to [0, 2*PI] range with smooth correction
    if (1.0f <= angle) {
        if (angle < 1.0f || 0.5f <= angle) {
            if (angle < 0.5f || 2.0f <= angle) {
                correction = 4.0f - angle;
            } else {
                correction = -2.0f * (angle - 0.5f) + 3.0f;
            }
        } else {
            correction = -2.0f * (angle - 1.0f) - 1.0f;
        }
    } else {
        correction = (1.0f - angle) - 1.0f;
    }

    // Apply correction scaled by damping factor
    *(float*)(data + 0xd4) += (correction / 1.0f) * *(float*)(data + 0xc4);

    // Handle wrap-around overflow
    if (4.0f <= *(float*)(data + 0xd4)) {
        prev_angle = prev_angle - 4.0f;
        *(float*)(data + 0xd4) -= 4.0f;
    }
    if (*(float*)(data + 0xd4) < 0.0f) {
        prev_angle = prev_angle + 4.0f;
        *(float*)(data + 0xd4) += 4.0f;
    }

    // Enforce min angle constraint
    float min_angle = *(float*)(data + 0xcc);
    if (0.0f != min_angle && min_angle < *(float*)(data + 0xd4) &&
        *(float*)(data + 0xd4) < 0.5f) {
        *(float*)(data + 0xd4) = min_angle;
    }

    // Enforce max angle constraint
    if (0.0f != *(float*)(data + 0xcc)) {
        float max_angle = 4.0f - *(float*)(data + 0xcc);
        if (*(float*)(data + 0xd4) < max_angle && 0.5f <= *(float*)(data + 0xd4)) {
            *(float*)(data + 0xd4) = max_angle;
        }
    }

    // Store delta as new velocity
    *(float*)(data + 0xd8) = *(float*)(data + 0xd4) - prev_angle;
}

// ============================================================================
// Address: 0x8007eb94 | Name: shell_casing_dot_angle
// Computes normalized angle between two 3D vectors via dot product + acos.
// Returns value in range [-1, 1] scaled by 180/PI, clamped and normalized.
// ============================================================================
double shell_casing_dot_angle(float* vec_a, float* vec_b) {
    float dot = vec_b[2] * vec_a[2] + vec_b[0] * vec_a[0] + vec_b[1] * vec_a[1];
    double acos_val = (double)math_acos(dot);

    // Convert radians to fraction of half-turn, scale by 180/PI constant
    float normalized = (float)((double)180.0f * acos_val) / 3.14159f;

    double result;
    if (1.0f <= normalized) {
        result = -((double)((normalized - 1.0f) / 1.0f));
    } else {
        result = (double)((1.0f - normalized) / 1.0f);
    }
    return result;
}

// ============================================================================
// Address: 0x8007ec0c | Name: shell_casing_spawn_entity
// Validates the pool slot and spawns a shell casing entity via entity_spawn_by_type.
// Checks if the slot's collision flags allow spawning (bit 3 of flags at +0x28).
// ============================================================================
void shell_casing_spawn_entity(int entity_type, int weapon_data, float* position,
                               int callback_a, int callback_b, int callback_c, int param_15) {
    int weapon_index = /* from register state */;
    int slot_index = /* from register state */;

    int valid = entity_pool_validate_index(1);
    if (valid != 0 ||
        (*(unsigned int*)(g_entity_data_base + *(short*)(g_weapon_table + weapon_index * 2) * 0x30 + 0x28) & 8) == 0) {
        int local_data[4];
        local_data[0] = callback_a;
        local_data[1] = callback_b;
        local_data[2] = callback_c;
        local_data[3] = param_15;
        entity_spawn_by_type(weapon_index, slot_index, position, local_data, param_15);
    }
}

// ============================================================================
// Address: 0x8007ecbc | Name: shell_casing_pool_pop
// Decrements the shell casing pool counter (frees the most recently allocated slot).
// ============================================================================
void shell_casing_pool_pop(void) {
    g_shell_pool_count--;
}

// ============================================================================
// Address: 0x8007eccc | Name: shell_casing_init_entity
// MASSIVE: Initializes a shell casing entity after spawn.
// Allocates per-casing data (0x154 bytes), sets up physics params
// (velocity, rotation, damping, scale) based on weapon type.
// Weapon types: 0x17-0x18 (pistol), 0x19-0x1a (rifle), 0x1b-0x22 (heavy),
// 0x1d-0x1e (shotgun), 0x1f-0x20 (SMG), 0x29-0x2b (special), 0x35 (grenade pin).
// Registers entity for per-frame update with shell_casing_physics_tick.
// ============================================================================
void shell_casing_init_entity(/* entity callback params */) {
    int entity;      // resolved from register state
    int* init_data;  // optional init data pointer (can be NULL)

    float* emit_pos = NULL;
    int emit_source = 0;
    int emit_id = 0;

    if (init_data != NULL) {
        emit_pos = (float*)init_data[0];
        emit_source = init_data[1];
        emit_id = init_data[2];
    }

    // Check if entity's render instance has flag 8 (ring buffer mode)
    if ((*(unsigned int*)(*(int*)(entity + 0x24) + 0x28) & 8) == 0) {
        // Linear allocation mode
        if (*(int*)(g_shell_pool_alloc_table + g_shell_pool_count * 4) == 0) {
            int mem = mem_alloc(0x154, 1);
            *(int*)(g_shell_pool_alloc_table + g_shell_pool_count * 4) = mem;
            memset_wrapper(mem, 0, 0x154);
        }
        int slot_offset = g_shell_pool_count * 4;
        g_shell_pool_count++;
        *(int*)(entity + 0x7c) = *(int*)(g_shell_pool_alloc_table + slot_offset);
    } else {
        // Ring buffer mode - evict old entry if present
        if (g_shell_ring_entities[g_shell_pool_ring_index] != 0) {
            entity_destroy_with_event((void*)g_shell_ring_entities[g_shell_pool_ring_index]);
        }
        int offset = g_shell_pool_ring_index * 0x154;
        g_shell_ring_entities[g_shell_pool_ring_index] = entity;
        *(int*)(entity + 0x7c) = g_shell_pool_ring_base + offset;
        g_shell_pool_ring_index =
            (g_shell_pool_ring_index + 1) % g_shell_pool_ring_capacity;
    }

    // Initialize casing data
    int* casing = *(int**)(entity + 0x7c);
    memset_wrapper((int)casing, 0, 0x154);
    casing[0] = emit_source;
    casing[1] = emit_id;
    casing[2] = (int)emit_pos;
    casing[0x3c] = -1;
    casing[0x53] = -1;
    casing[4] = *(int*)&(1.0f);  // scale factors initialized to 1.0
    casing[5] = *(int*)&(1.0f);
    casing[6] = *(int*)&(1.0f);
    casing[7] = *(int*)&(1.0f);

    int extra_data = (init_data != NULL) ? init_data[3] : 0;
    casing[0x54] = extra_data;

    // Configure based on weapon/entity type
    unsigned short entity_type = *(unsigned short*)(entity + 0x94);
    switch (entity_type) {
    case 0x17: case 0x18: case 0x24:
        // Pistol casings - small, fast spin
        // ... (velocity, rotation, damping configured from RNG + emit_pos)
        *(unsigned int*)(*(int*)(entity + 4) + 0x30) |= 4;  // enable shadow
        entity_register_for_update(entity, 3, -0x7ff8007c);
        break;

    case 0x19: case 0x1a:
        // Rifle casings - medium size
        entity_register_for_update(entity, 3, -0x7ff8007c);
        break;

    case 0x1b: case 0x1c: case 0x21: case 0x22:
    case 0x29: case 0x2a: case 0x2b:
        // Heavy weapon / special casings
        entity_register_for_update(entity, 3, -0x7ff8007c);
        break;

    case 0x1d: case 0x1e:
        // Shotgun shells - larger, slower
        entity_register_for_update(entity, 3, -0x7ff8007c);
        break;

    case 0x1f: case 0x20:
        // SMG casings - small, high volume
        entity_register_for_update(entity, 3, -0x7ff8007c);
        break;

    case 0x35:
        // Grenade pin - no shadow, special material, different priority
        *(unsigned int*)(entity + 0x28) |= 0x400;
        render_instance_set_material(0.0f, *(int**)(entity + 4), 1);
        casing[4] = *(int*)&(0.0f);
        collision_set_shape_params(*(int**)(entity + 4), 2, 0, 0xff, 0);
        entity_register_for_update(entity, 3, -0x7ff7dc94);
        break;
    }
}

// ============================================================================
// Address: 0x8007f9f4 | Name: shell_casing_spawn_batch
// Convenience wrapper: spawns a batch of casings with flags = 0.
// ============================================================================
void shell_casing_spawn_batch(float* position, int count) {
    shell_casing_spawn_batch_with_flags(position, count, 0);
}

// ============================================================================
// Address: 0x8007fa18 | Name: shell_casing_spawn_batch_with_flags
// Spawns N shell casing entities for a given weapon type.
// Selects casing model pair based on weapon type (switch on 14 weapon classes).
// For type 0xd (dual-wield): randomly selects between two model variants.
// Each casing gets randomized position offset and optional flip flag.
// ============================================================================
void shell_casing_spawn_batch_with_flags(float* position, int count, unsigned int flags) {
    int weapon_type = /* from register state: weapon lookup */;
    int weapon_data = /* weapon data pointer */;

    float spawn_pos[3];
    spawn_pos[0] = position[0];
    spawn_pos[1] = position[1];
    spawn_pos[2] = position[2];
    float offset_y = 1.5f + /* extraout_f1 */;

    float zero_vel[3] = {0.0f, 0.0f, 0.0f};

    // Select casing model pair based on weapon type
    int model_a, model_b;
    switch (weapon_type) {
    case 0: case 0xd:  model_a = 0x19; model_b = 0x1a; break;
    case 1:            model_a = 0x17; model_b = 0x18; break;
    case 2:            model_a = 0x21; model_b = 0x22; break;
    case 3:            model_a = 0x1f; model_b = 0x20; break;
    case 4:            model_a = 0x1d; model_b = 0x1e; break;
    case 5:            model_a = 0x1b; model_b = 0x1c; break;
    case 6: case 7: case 8:
        return;  // no casings for these weapon types
    case 9:            model_a = 0x29; model_b = 0x29; break;
    case 10:           model_a = 0x2a; model_b = 0x2a; break;
    case 0xb:          model_a = 0x2b; model_b = 0x2b; break;
    case 0xc:          model_a = 0x24; model_b = 0x24; break;
    default:
        return;
    }

    float rng_scale = 1.0f / (float)RNG_MAX;  // FLOAT_80614180

    if (weapon_type == 0xd) {
        // Dual-wield: randomize between model_a and model_b each iteration
        for (int i = 0; i < count; i++) {
            shell_casing_evict_if_full();
            unsigned int r = rng_next();
            float chance = (float)r * rng_scale;
            int model = (0.5f <= chance) ? model_b : model_a;

            int ent = shell_casing_spawn_entity(model, weapon_data,
                                                &zero_vel, &spawn_pos, 0, /*callback*/ 0);
            if (ent != 0) {
                g_shell_entity_list[g_shell_pool_active] = ent;
                g_shell_pool_active++;

                // Randomize spawn position offset
                spawn_pos[0] = 2.0f * ((float)rng_next() * rng_scale) - 1.0f;
                spawn_pos[1] = 3.0f * ((float)rng_next() * rng_scale);
                spawn_pos[2] = 2.0f * ((float)rng_next() * rng_scale) - 1.0f;

                int casing_data = *(int*)(ent + 0x7c);
                *(float*)(casing_data + 0x98) =
                    1.0f + (float)rng_next() * rng_scale;
                *(unsigned int*)(casing_data + 0x148) = flags;

                // Random flip: 50% chance to clear bit 0 of flags
                if ((flags & 1) != 0) {
                    unsigned int r2 = rng_next();
                    if ((r2 & 1) != 0) {
                        *(unsigned int*)(casing_data + 0x148) &= ~1u;
                    }
                }
            }
        }
    } else {
        // Standard: spawn count casings with same model selection logic
        for (int i = 0; i < count; i++) {
            shell_casing_evict_if_full();
            unsigned int r = rng_next();
            float chance = (float)r * rng_scale;
            int model = (0.5f <= chance) ? model_b : model_a;

            int ent = shell_casing_spawn_entity(model, weapon_data,
                                                &zero_vel, &spawn_pos, 0, /*callback*/ 0);
            if (ent != 0) {
                g_shell_entity_list[g_shell_pool_active] = ent;
                g_shell_pool_active++;

                // Randomize spawn offset
                spawn_pos[0] = 2.0f * ((float)rng_next() * rng_scale) - 1.0f;
                spawn_pos[1] = 3.0f * ((float)rng_next() * rng_scale);
                spawn_pos[2] = 2.0f * ((float)rng_next() * rng_scale) - 1.0f;

                int casing_data = *(int*)(ent + 0x7c);
                *(unsigned int*)(casing_data + 0x148) = flags;

                // Random flip
                if ((flags & 1) != 0) {
                    unsigned int r2 = rng_next();
                    if ((r2 & 1) != 0) {
                        *(unsigned int*)(casing_data + 0x148) &= ~1u;
                    }
                }
            }
        }
    }
}

// ============================================================================
// Address: 0x8007fe94 | Name: shell_casing_destroy_oldest
// Destroys the oldest (first) shell casing entity in the active list.
// ============================================================================
void shell_casing_destroy_oldest(void) {
    entity_destroy_with_event((void*)g_shell_entity_list[0]);
}

// ============================================================================
// Address: 0x8007fec0 | Name: shell_casing_evict_if_full
// If active shell count exceeds max (20), evicts the oldest casing.
// ============================================================================
void shell_casing_evict_if_full(void) {
    if (g_shell_pool_active > 0x13) {  // max 20 casings
        shell_casing_destroy_oldest();
    }
}

// ============================================================================
// Address: 0x8007ff84 | Name: shell_casing_physics_tick
// Per-frame physics update for a shell casing entity.
// Handles: lifetime fade, gravity, ground bounce with restitution,
// velocity damping, angular velocity integration, and trail particle emission.
// Called as entity update callback (priority -0x7ff8007c).
// ============================================================================
void shell_casing_physics_tick(void) {
    int* entity = /* resolved from register state */;
    int casing_data = entity[0x1f];  // +0x7c

    // Lifetime fade: decrease alpha over time
    float dt_scale = g_frame_dt;  // FLOAT_806108ec / frame_time_divisor
    *(float*)(casing_data + 0x98) -= dt_scale / /* frame time divisor */;

    if (*(float*)(casing_data + 0x98) < 1.0f) {
        int alpha = (int)(255.0f * *(float*)(casing_data + 0x98));
        if (alpha < 0) {
            // Fully faded - destroy
            entity_destroy_with_event(entity);
            return;
        }
        collision_set_shape_params((int*)entity[1], 0xf, 0xffffffff, 0xff, (char)alpha);
    }

    // Ground bounce detection
    float ground_y = *(float*)&entity[0x1a];
    float vel_y = *(float*)&entity[0x2e];
    if (*(float*)(casing_data + 0x10) <= ground_y && vel_y <= 0.0f) {
        // Bounce: invert Y velocity with restitution coefficient
        entity[0x2e] = *(int*)&(-((float)entity[0x2e] * *(float*)(casing_data + 0xbc)));
    }

    // Check if velocity is below threshold (at rest)
    float vel_threshold = 0.01f;
    if (ground_y <= *(float*)(casing_data + 0x10)) {
        if ((float)entity[0x2e] <= vel_threshold && -0.005f <= (float)entity[0x2e]) {
            // Come to rest
            entity[0x35] = 0;  // zero angular velocity
            entity[0x36] = 0;
            entity[0x37] = 0;
            entity[0x30] = *(int*)&(2.0f);  // lock rotation
            entity[0x32] = 0;
            entity[0x1a] = *(int*)(casing_data + 0x10);  // snap to ground
            return;
        }
        // Apply horizontal damping on bounce
        float restitution = *(float*)(casing_data + 0xc8);
        entity[0x2d] = *(int*)&(-((float)entity[0x2d] * restitution - (float)entity[0x2d]));
        entity[0x2f] = *(int*)&(-((float)entity[0x2f] * restitution - (float)entity[0x2f]));
    }

    // Apply gravity
    float gravity = 0.5f;  // FLOAT_806140b0
    float gravity_accel = dt_scale * (gravity / /* frame_divisor */);
    entity[0x2e] = *(int*)&(-((float)gravity_accel - (float)entity[0x2e]));

    // Integrate position
    entity[0x19] = *(int*)&((float)entity[0x2d] * dt_scale + (float)entity[0x19]);
    entity[0x1a] = *(int*)&((float)entity[0x2e] * dt_scale + (float)entity[0x1a]);
    entity[0x1b] = *(int*)&((float)entity[0x2f] * dt_scale + (float)entity[0x1b]);

    // Apply air damping to velocity
    float air_damp = *(float*)(casing_data + 0xc4);
    entity[0x2d] = *(int*)&((float)entity[0x2d] * air_damp);
    entity[0x2e] = *(int*)&((float)entity[0x2e] * air_damp);
    entity[0x2f] = *(int*)&((float)entity[0x2f] * air_damp);

    // Integrate angular position from angular velocity
    entity[0x30] = *(int*)&((float)entity[0x35] * dt_scale + (float)entity[0x30]);
    entity[0x1c] = *(int*)&((float)entity[0x36] * dt_scale + (float)entity[0x1c]);
    entity[0x32] = *(int*)&((float)entity[0x37] * dt_scale + (float)entity[0x32]);

    // Trail particle emission (if flag bit 0 set and lifetime > threshold)
    if ((*(unsigned int*)(casing_data + 0x148) & 1) != 0 &&
        0.25f < *(float*)(casing_data + 0x98) &&
        *(char*)(entity + 0x30) > 0) {  // has active particles

        int num_particles = (int)*(float*)(casing_data + 0x98);
        for (int i = 0; i < num_particles; i++) {
            float trail_pos[3];
            trail_pos[0] = (float)entity[0x19] + (float)entity[0x6a];
            trail_pos[1] = (float)entity[0x1a] + (float)entity[0x6b];
            trail_pos[2] = (float)entity[0x1b] + (float)entity[0x6c];

            // Randomize trail offset
            float rng_scale = 1.0f / (float)RNG_MAX;
            float trail_vel[3];
            trail_vel[0] = 0.1f * ((float)rng_next() * rng_scale - 0.5f);
            trail_vel[1] = 0.3f * ((float)rng_next() * rng_scale) + 0.1f;
            trail_vel[2] = 0.1f * ((float)rng_next() * rng_scale - 0.5f);

            int particle = particle_spawn(1.0f, 0.0f, 0.25f, 1,
                                          entity, &trail_pos, &trail_vel);
            *(float*)(particle + 0x38) = 0.02f;
            *(float*)(particle + 0x3c) = 0.04f;
            *(float*)(particle + 0x44) = 0.015f;
        }
    }
}
