// Address: 0x800e3e60 - 0x800e4000 | Batch 270 | Date: 2026-08-11
// Particle emitter property setter functions.
// These are small accessor/setter utilities used widely by weapon effects,
// spawn systems, and AI trail particles to control emitter properties.
//
// The "cached" variants dereference a pointer-to-pointer (cached ref) before
// operating on the underlying emitter struct. This allows callers to hold a
// stable handle that can be invalidated (set to NULL) when the emitter dies.

#include "particle_internal.h"

// ============================================================================
// Global cached emitter reference (used by a subset of callers)
// ============================================================================

extern ParticleEmitter* g_pCachedEmitter; // DAT_80611e30

// ============================================================================
// Direct emitter setters
// ============================================================================

// Address: 0x800e3e60
// Set uniform scale on the global cached emitter.
// If emitter lacks NOROOM flag (0x1000), triggers a cull check.
void particle_emitter_set_scale(float scale)
{
    if (g_pCachedEmitter != NULL) {
        g_pCachedEmitter->scale_x = scale;
        g_pCachedEmitter->scale_y = scale;
        if ((g_pCachedEmitter->flags & 0x1000) == 0) {
            particle_emitter_cull_check(g_pCachedEmitter);
        }
    }
}

// Address: 0x800e3f48
// Set world position (vec3) on a particle emitter.
// If emitter lacks NOROOM flag (0x1000), triggers room update.
void particle_emitter_set_position(ParticleEmitter* pEmitter, Vec3* pPos)
{
    if (pEmitter != NULL) {
        pEmitter->pos.x = pPos->x;
        pEmitter->pos.y = pPos->y;
        pEmitter->pos.z = pPos->z;
        if ((pEmitter->flags & 0x1000) == 0) {
            particle_emitter_update_rooms(pEmitter);
        }
    }
}

// Address: 0x800e3f94
// Set velocity (vec3) on a particle emitter.
void particle_emitter_set_velocity(ParticleEmitter* pEmitter, Vec3* pVel)
{
    if (pEmitter == NULL) {
        return;
    }
    pEmitter->vel.x = pVel->x;
    pEmitter->vel.y = pVel->y;
    pEmitter->vel.z = pVel->z;
}

// ============================================================================
// Cached-reference wrappers
// These take a pointer-to-emitter-pointer (a "cached ref") that can be NULL
// or point to a NULL emitter if the emitter has been destroyed.
// ============================================================================

// Address: 0x800e3ea8
// Set uniform scale on emitter via cached reference.
void particle_cached_set_scale(ParticleEmitter** pCachedRef, float scale)
{
    ParticleEmitter* pEmitter = *pCachedRef;
    if (pEmitter == NULL) {
        return;
    }
    pEmitter->scale_x = scale;
    pEmitter->scale_y = scale;
}

// Address: 0x800e3ec0
// Enable a cached particle emitter (set active byte = 1).
void particle_cached_enable(ParticleEmitter** pCachedRef)
{
    if (*pCachedRef == NULL) {
        return;
    }
    (*pCachedRef)->active = 1;
}

// Address: 0x800e3ed8
// Disable a cached particle emitter (set active byte = 0).
void particle_cached_disable(ParticleEmitter** pCachedRef)
{
    if (*pCachedRef == NULL) {
        return;
    }
    (*pCachedRef)->active = 0;
}

// Address: 0x800e3ef0
// Restart a cached particle emitter: enables it, resets speed to 0,
// and transitions state to STARTING (2) unless already in DYING (3).
void particle_cached_restart(ParticleEmitter** pCachedRef)
{
    ParticleEmitter* pEmitter = *pCachedRef;
    ParticleEmitter* tempRef[1];

    tempRef[0] = pEmitter;
    particle_cached_enable(tempRef);

    if (pEmitter != NULL) {
        pEmitter->speed = 0.0f;
        if (pEmitter->state != PARTICLE_STATE_DYING) {
            pEmitter->state = PARTICLE_STATE_STARTING;
        }
    }
}

// Address: 0x800e3fb8
// Set position (vec3) on emitter via cached reference.
void particle_cached_set_position(ParticleEmitter** pCachedRef, Vec3* pPos)
{
    particle_emitter_set_position(*pCachedRef, pPos);
}

// Address: 0x800e3fdc
// Set velocity (vec3) on emitter via cached reference.
void particle_cached_set_velocity(ParticleEmitter** pCachedRef, Vec3* pVel)
{
    particle_emitter_set_velocity(*pCachedRef, pVel);
}

// Address: 0x800e4000
// Set render layer byte on emitter via cached reference.
// Used by weapon_smoke_tick to control draw order.
void particle_cached_set_render_layer(ParticleEmitter** pCachedRef, u8 layer)
{
    if (*pCachedRef == NULL) {
        return;
    }
    (*pCachedRef)->render_layer = layer;
}

// ============================================================================
// Batch 271: Advanced emitter property setters
// Address: 0x800e4014 - 0x800e42d4 | Date: 2026-08-11
// Color keys, bounding volumes, emit rate, target position, lifetime
// ============================================================================

// Address: 0x800e4014
// Set render layer on the global cached emitter.
void particle_emitter_set_render_layer(u8 layer)
{
    if (g_pCachedEmitter == NULL) {
        return;
    }
    g_pCachedEmitter->render_layer = layer;
}

// Address: 0x800e4028
// Set color gradient keys (up to 12) on an emitter.
// Copies byte-sized color values from an int array into the key table.
void particle_emitter_set_color_keys(ParticleEmitter* pEmitter, int numKeys, int* pColorArray)
{
    int count;
    int i;

    if (pEmitter == NULL) {
        return;
    }

    // Clamp to max 12 keys
    count = 12;
    if (numKeys < 12) {
        count = numKeys;
    }
    if (count < 1) {
        count = 0;
    } else {
        count = 12;
        if (numKeys < 12) {
            count = numKeys;
        }
    }

    pEmitter->color_key_count = count;

    for (i = 0; i < pEmitter->color_key_count; i++) {
        pEmitter->color_keys[i] = (char)pColorArray[i];
    }
}

// Address: 0x800e4094
// Set a single color key value on an emitter (simplified variant).
// If value < 1, clears all keys. Otherwise sets count=1 with that value.
void particle_emitter_set_single_color_key(ParticleEmitter* pEmitter, int colorValue)
{
    if (pEmitter == NULL) {
        return;
    }

    pEmitter->color_key_count = 0;

    if (colorValue < 1) {
        return;
    }

    pEmitter->color_key_count = 1;
    pEmitter->color_keys[0] = (char)colorValue;
}

// Address: 0x800e40bc
// Set AABB bounds on emitter via cached reference.
// If pBounds is NULL and nodeId==0: clears HAS_BOUNDS flag (0x100).
// If pBounds is NULL but nodeId!=0: queries scene node AABB.
// If pBounds is provided: copies 6 floats (min xyz, max xyz) directly.
void particle_cached_set_bounds(ParticleEmitter** pCachedRef, int nodeId, float* pBounds)
{
    ParticleEmitter* pEmitter = *pCachedRef;

    if (pEmitter != NULL) {
        if (pBounds == NULL && nodeId == 0) {
            pEmitter->flags &= ~0x100u;  // clear HAS_BOUNDS
        } else {
            if (pBounds == NULL) {
                scene_get_node_aabb(nodeId, &pEmitter->bounds_min, &pEmitter->bounds_max);
            } else {
                pEmitter->bounds_min.x = pBounds[0];
                pEmitter->bounds_min.y = pBounds[1];
                pEmitter->bounds_min.z = pBounds[2];
                pEmitter->bounds_max.x = pBounds[3];
                pEmitter->bounds_max.y = pBounds[4];
                pEmitter->bounds_max.z = pBounds[5];
            }
            pEmitter->flags |= 0x100u;  // set HAS_BOUNDS
        }
    }
}

// Address: 0x800e4164
// Set AABB bounds on the global cached emitter. Same logic as cached variant.
void particle_emitter_set_bounds(int nodeId, float* pBounds)
{
    if (g_pCachedEmitter != NULL) {
        if (pBounds == NULL && nodeId == 0) {
            g_pCachedEmitter->flags &= ~0x100u;
        } else {
            if (pBounds == NULL) {
                scene_get_node_aabb(nodeId, &g_pCachedEmitter->bounds_min,
                                    &g_pCachedEmitter->bounds_max);
            } else {
                g_pCachedEmitter->bounds_min.x = pBounds[0];
                g_pCachedEmitter->bounds_min.y = pBounds[1];
                g_pCachedEmitter->bounds_min.z = pBounds[2];
                g_pCachedEmitter->bounds_max.x = pBounds[3];
                g_pCachedEmitter->bounds_max.y = pBounds[4];
                g_pCachedEmitter->bounds_max.z = pBounds[5];
            }
            g_pCachedEmitter->flags |= 0x100u;
        }
    }
}

// Address: 0x800e4218
// Set emission rate on the global cached emitter.
// Sets EMIT_RATE_OVERRIDE flag (0x20000).
void particle_emitter_set_emit_rate(float rate)
{
    if (g_pCachedEmitter == NULL) {
        return;
    }
    g_pCachedEmitter->emit_rate = rate;
    g_pCachedEmitter->flags |= 0x20000u;
}

// Address: 0x800e423c
// Set target/attractor position (vec3) via cached reference (offset 0x98).
// Sets HAS_TARGET flag (0x4000).
void particle_cached_set_target(ParticleEmitter** pCachedRef, Vec3* pTarget)
{
    ParticleEmitter* pEmitter = *pCachedRef;
    if (pEmitter == NULL) {
        return;
    }
    pEmitter->target.x = pTarget->x;
    pEmitter->target.y = pTarget->y;
    pEmitter->target.z = pTarget->z;
    pEmitter->flags |= 0x4000u;
}

// Address: 0x800e4270
// Set target/attractor position (vec3) on the global cached emitter.
// Sets HAS_TARGET flag (0x4000).
void particle_emitter_set_target(Vec3* pTarget)
{
    if (g_pCachedEmitter == NULL) {
        return;
    }
    g_pCachedEmitter->target.x = pTarget->x;
    g_pCachedEmitter->target.y = pTarget->y;
    g_pCachedEmitter->target.z = pTarget->z;
    g_pCachedEmitter->flags |= 0x4000u;
}

// Address: 0x800e42b0
// Set particle lifetime on the global cached emitter (offset 0x60).
// Sets LIFETIME_OVERRIDE flag (0x400).
void particle_emitter_set_lifetime(float lifetime)
{
    if (g_pCachedEmitter == NULL) {
        return;
    }
    g_pCachedEmitter->lifetime = lifetime;
    g_pCachedEmitter->flags |= 0x400u;
}

// Address: 0x800e42d4
// Set particle lifetime via cached reference.
// Sets LIFETIME_OVERRIDE flag (0x400).
void particle_cached_set_lifetime(float lifetime, ParticleEmitter** pCachedRef)
{
    ParticleEmitter* pEmitter = *pCachedRef;
    if (pEmitter == NULL) {
        return;
    }
    pEmitter->lifetime = lifetime;
    pEmitter->flags |= 0x400u;
}

// ============================================================================
// Batch 272: Getters + physics/size/delay setters
// Address: 0x800e42f4 - 0x800e43e8 | Date: 2026-08-11
// Progress ratio getter, delay, spawn radius, size range, gravity, drag, speed
// ============================================================================

// Address: 0x800e42f4
// Get normalized progress (speed / lifetime) of emitter via cached ref.
// Returns 1.0f if emitter is NULL (treat as complete).
float particle_cached_get_progress(ParticleEmitter** pCachedRef)
{
    ParticleEmitter* pEmitter = *pCachedRef;
    if (pEmitter != NULL) {
        return pEmitter->speed / pEmitter->lifetime;
    }
    return 1.0f;
}

// Address: 0x800e4318
// Set start delay (offset 0x5c) via cached ref.
// Sets LIFETIME_OVERRIDE flag (0x400).
void particle_cached_set_delay(float delay, ParticleEmitter** pCachedRef)
{
    ParticleEmitter* pEmitter = *pCachedRef;
    if (pEmitter == NULL) {
        return;
    }
    pEmitter->delay = delay;
    pEmitter->flags |= 0x400u;
}

// Address: 0x800e4338
// Set spawn radius (offset 0x84) via cached ref.
// Sets flag 0x4 (HAS_RADIUS), clears flag 0x2 (POINT_SPAWN).
void particle_cached_set_spawn_radius(float radius, ParticleEmitter** pCachedRef)
{
    ParticleEmitter* pEmitter = *pCachedRef;
    if (pEmitter == NULL) {
        return;
    }
    pEmitter->spawn_radius = radius;
    pEmitter->flags |= 0x4u;    // HAS_RADIUS
    pEmitter->flags &= ~0x2u;   // clear POINT_SPAWN
}

// Address: 0x800e4364
// Set particle size range (min/max) on the global cached emitter.
void particle_emitter_set_size_range(float sizeMin, float sizeMax)
{
    if (g_pCachedEmitter == NULL) {
        return;
    }
    g_pCachedEmitter->size_min = sizeMin;
    g_pCachedEmitter->size_max = sizeMax;
}

// Address: 0x800e4380
// Set particle size range (min/max) via cached ref.
void particle_cached_set_size_range(float sizeMin, float sizeMax, ParticleEmitter** pCachedRef)
{
    ParticleEmitter* pEmitter = *pCachedRef;
    if (pEmitter == NULL) {
        return;
    }
    pEmitter->size_min = sizeMin;
    pEmitter->size_max = sizeMax;
}

// Address: 0x800e4398
// Set gravity/weight on the global cached emitter (offset 0x70).
void particle_emitter_set_gravity(float gravity)
{
    if (g_pCachedEmitter == NULL) {
        return;
    }
    g_pCachedEmitter->gravity = gravity;
}

// Address: 0x800e43ac
// Set gravity/weight via cached ref.
void particle_cached_set_gravity(float gravity, ParticleEmitter** pCachedRef)
{
    if (*pCachedRef == NULL) {
        return;
    }
    (*pCachedRef)->gravity = gravity;
}

// Address: 0x800e43c0
// Set drag/friction on the global cached emitter (offset 0x74).
void particle_emitter_set_drag(float drag)
{
    if (g_pCachedEmitter == NULL) {
        return;
    }
    g_pCachedEmitter->drag = drag;
}

// Address: 0x800e43d4
// Set drag/friction via cached ref.
void particle_cached_set_drag(float drag, ParticleEmitter** pCachedRef)
{
    if (*pCachedRef == NULL) {
        return;
    }
    (*pCachedRef)->drag = drag;
}

// Address: 0x800e43e8
// Get current speed/age (offset 0x58) from emitter via cached ref.
// Returns 0.0f if emitter is NULL.
float particle_cached_get_speed(ParticleEmitter** pCachedRef)
{
    if (*pCachedRef != NULL) {
        return (*pCachedRef)->speed;
    }
    return 0.0f;
}
