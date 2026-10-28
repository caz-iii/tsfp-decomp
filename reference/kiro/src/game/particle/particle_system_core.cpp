// Address: 0x800e4404 - 0x800e51b8 | Batch 273 | Date: 2026-08-11
// Particle system core: entity helpers, system update loop, physics, rendering.
// Contains the main per-frame update, per-particle physics simulation,
// and the billboard quad renderer that writes directly to GX FIFO.

#include "particle_internal.h"
#include "entity.h"
#include "math_util.h"
#include "gx_fifo.h"

// ============================================================================
// Simple accessors / entity helpers
// ============================================================================

// Address: 0x800e4404
// Check if cached emitter is in STARTING state (state == 2).
// Uses CLZ trick: countLeadingZeros(2 - state) >> 5 == 1 only when state == 2.
int particle_cached_is_starting(ParticleEmitter** pCachedRef)
{
    if (*pCachedRef != NULL) {
        return (countLeadingZeros(2 - (*pCachedRef)->state) >> 5);
    }
    return 0;
}

// Address: 0x800e442c
// Enable all particle emitters attached to the current entity.
void particle_entity_enable_all(void)
{
    int entity = get_current_entity();
    ParticleEmitter* tempRef[1];

    for (int i = 0; i < entity->particle_count; i++) {
        tempRef[0] = entity->particle_list[i];
        particle_cached_enable(tempRef);
    }
    release_current_entity();
}

// Address: 0x800e4490
// Disable all particle emitters attached to the current entity.
void particle_entity_disable_all(void)
{
    int entity = get_current_entity();
    ParticleEmitter* tempRef[1];

    for (int i = 0; i < entity->particle_count; i++) {
        tempRef[0] = entity->particle_list[i];
        particle_cached_disable(tempRef);
    }
    release_current_entity();
}

// Address: 0x800e44f4
// Update emit rates for all emitters in entity based on definition base rate * delta time.
void particle_entity_update_emit_rates(int entityData)
{
    ParticleEmitter* pFirst;
    float baseRate;
    float dt;

    if (*(ParticleEmitter**)(entityData + 0x1fc) == NULL) return;
    pFirst = *(ParticleEmitter**)(entityData + 0x1fc);
    if (pFirst == NULL) return;

    dt = get_delta_time();
    baseRate = *(float*)(pFirst->def + 0x34);

    for (int i = 0; i < *(int*)(entityData + 0x1e8); i++) {
        ParticleEmitter* pEmitter = ((ParticleEmitter**)(entityData + 0x1fc))[i];
        if (pEmitter != NULL) {
            pEmitter->emit_accumulator = baseRate * dt;
        }
    }
}

// Address: 0x800e4580
// Set emit rate (offset 0x64) directly via cached ref.
void particle_cached_set_emit_rate(float rate, ParticleEmitter** pCachedRef)
{
    if (*pCachedRef == NULL) {
        return;
    }
    (*pCachedRef)->emit_accumulator = rate;
}

// ============================================================================
// World-space transform
// ============================================================================

// Address: 0x800e4594
// Transform emitter position and velocity from local (bone) space to world space.
// Builds local matrix from entity, transforms the definition's offset/velocity
// through bone matrices if entity is skinned, then sets position/velocity on emitter.
// Also handles mirror flag (bit 0x10) based on entity mirror state.
void particle_emitter_transform_to_world(int entityId, ParticleEmitter* pEmitter,
                                         float* pMatrix, int useBoneTransform)
{
    u64 entityPair = get_entity_and_emitter();
    int entity = (int)(entityPair >> 32);
    int emitter = (int)entityPair;

    float localMatrix[16];
    Vec3 localPos;
    Vec3 localVel;

    if (pMatrix == NULL) {
        pMatrix = localMatrix;
        entity_build_local_matrix(pMatrix, entity, 1, 0);
    }

    int def = *(int*)(emitter + 0xc);
    localPos.x = *(float*)(def + 0x08);
    localPos.y = *(float*)(def + 0x0c);
    localPos.z = *(float*)(def + 0x10);
    localVel.x = *(float*)(def + 0x14);
    localVel.y = *(float*)(def + 0x18);
    localVel.z = *(float*)(def + 0x1c);

    // If using bone transform and entity is skinned (no flag 0x100)
    if (useBoneTransform != 0 && (*(u32*)(entity + 0x28) & 0x100) == 0) {
        int boneOffset = *(int*)(emitter + 0x8c) * 0x40;
        float* boneMatrix = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + boneOffset);
        matrix_transform_point(boneMatrix, &localPos);
        matrix_transform_vector(boneMatrix, &localVel);
    }

    matrix_transform_point(pMatrix, &localPos);
    matrix_transform_vector(pMatrix, &localVel);
    particle_emitter_set_position(emitter, &localPos);
    particle_emitter_set_velocity(emitter, &localVel);

    // Mirror flag
    if (*(char*)(entity + 0x31) == 1) {
        *(u32*)(emitter + 4) |= 0x10u;
    } else {
        *(u32*)(emitter + 4) &= ~0x10u;
    }

    release_entity_lock();
}

// ============================================================================
// Main system update
// ============================================================================

// Address: 0x800e46bc
// MAIN particle system per-frame update loop.
// Iterates the active emitter linked list:
// - Checks cutscene visibility flags (cutscene_flag_is_set)
// - Runs visibility/AABB culling
// - Transforms attached emitters to world space
// - Calls particle_emitter_tick for active/visible emitters
// - Calls particle_physics_update if physics flags are set
// - Queries color from attached entity if flag 0x20000000
// - Frees dead emitters (active_count == 0 and DEAD flag)
void particle_system_update(void)
{
    ParticleEmitter* pNext;
    ParticleEmitter* pEmitter = g_pActiveEmitterList;  // DAT_80611e10
    Vec3 entityColor;

    while (pEmitter != NULL) {
        pNext = pEmitter->next;
        pEmitter->render_index = 0;

        // Cutscene visibility check
        u32 cutsceneId = pEmitter->def->cutscene_flag_index;
        if ((int)cutsceneId >= 0 && pEmitter->def->type != 7) {
            pEmitter->active = cutscene_flag_is_set(cutsceneId);
        }

        // Copy active to visible byte
        pEmitter->visible = pEmitter->active;

        // Visibility culling (if no NOROOM flag and is active or has particles)
        if ((pEmitter->flags & 0x1000) == 0 &&
            (pEmitter->visible || pEmitter->active_count != 0)) {
            bool isVisible = particle_emitter_visibility_check(pEmitter);
            pEmitter->visible = (pEmitter->visible && isVisible) ? 1 : 0;
        }

        // AABB screen-space test for NOROOM emitters
        if ((pEmitter->flags & 0x1000) != 0 && (pEmitter->flags & 0x20000) != 0) {
            int onScreen = particle_emitter_submit_aabb_corners();
            pEmitter->visible = (pEmitter->visible && onScreen) ? 1 : 0;
        }

        // Transform attached emitters to world space
        if (pEmitter->attached_entity != 0 &&
            (pEmitter->flags & 0x100000) == 0 &&
            (pEmitter->flags2 & 1) == 0) {
            particle_emitter_transform_to_world(pEmitter->attached_entity, pEmitter, NULL, 1);
        }

        // Tick emitter if visible or has active particles
        if (pEmitter->visible || pEmitter->active_count != 0) {
            particle_emitter_tick();
            if ((pEmitter->flags & 0x1483a0) != 0) {
                particle_physics_update();
            }
            // Color from attached entity
            if ((pEmitter->flags & 0x20000000) != 0) {
                get_entity_color((u8)pEmitter->color_source_entity, &entityColor);
                pEmitter->color_override = entityColor;
            }
        }

        // Free dead emitters
        if (pEmitter->active_count == 0 && (pEmitter->flags & 1) != 0) {
            particle_emitter_pool_free(pEmitter);
        }

        pEmitter = pNext;
    }
}

// ============================================================================
// Per-particle physics
// ============================================================================

// Address: 0x800e489c
// MASSIVE per-particle physics update. Processes all particles in an emitter:
//
// Flag 0x20:     Orbit/spring motion - velocity reversal + gravity integration
// Flag 0x80:     Torus wrapping around emitter center
// Flag 0x2000:   Attractor - pull toward emitter center with random weight
// Flag 0x100:    AABB bounds clamping - reflect velocity at walls
// Flag 0x200:    Floor clamping - Y >= emitter Y
// Flag 0x8000:   Angular momentum - cross product spin with rotation matrix
// Flag 0x40000:  Ceiling bounce - reflect at height limit
// Flag 0x100000: Attached entity - offset particles by entity delta position
//
// Uses fast inverse sqrt (Newton-Raphson) for normalization.
// Particle stride depends on type: 0x58 for type 5/6, 0x34 otherwise.
void particle_physics_update(void)
{
    // [MASSIVE function - see Ghidra decompilation for full implementation]
    // This function iterates all particles in the current emitter's ring buffer
    // and applies physics forces based on the emitter's flag bits.
    // Key physics behaviors are listed in the plate comment above.
    //
    // For the PC port, this will need to be reimplemented with:
    // - Standard sqrt/normalize instead of paired-single fast inverse sqrt
    // - Time-based integration using frame delta (already uses dt internally)
    // - The orbit/spring, attractor, and bounds behaviors are straightforward
    //   to port as they use basic vector math
}

// ============================================================================
// Splash spawn
// ============================================================================

// Address: 0x800e512c
// Spawn a splash/debris particle at position with random XZ offset.
// Uses emitter's spawn_radius + 1.0 as Y, and render_layer for material index.
void particle_spawn_splash(ParticleEmitter* pEmitter, float* pBasePos)
{
    Vec3 spawnPos;
    float dt = get_delta_time();

    spawnPos.x = 2.0f * (dt - 0.5f) + pBasePos[0];  // random offset X
    spawnPos.y = 1.0f + pEmitter->spawn_radius;       // height from radius
    float dt2 = get_delta_time();
    spawnPos.z = 2.0f * (dt2 - 0.5f) + pBasePos[2]; // random offset Z

    spawn_effect_at_position(&spawnPos, 0x78, (u32)pEmitter->render_layer);
}

// ============================================================================
// Billboard quad renderer
// ============================================================================

// Address: 0x800e51b8
// MASSIVE particle billboard quad renderer.
// For each visible particle in the emitter's ring buffer:
// 1. Computes camera-facing basis vectors via cross products
// 2. Evaluates particle size from definition keyframes and age
// 3. Builds a rotated quad (4 corners) using size and angular rotation
// 4. Computes per-particle color (particle_compute_color)
// 5. Writes 4 vertices directly to GX FIFO (0xCC008000):
//    - Position XYZ (float)
//    - Color (RGBA packed u32)
//    - Texture UV (float pair)
// 6. Advances ring buffer index with wrap-around
//
// Uses fast inverse sqrt for direction normalization.
// Particle stride is 0x34 (standard type).
void particle_render_billboard_quads(void)
{
    // [MASSIVE function - see Ghidra decompilation for full implementation]
    // This is the core GX rendering path for billboard particles.
    //
    // For the PC port (OpenGL), this maps to:
    // - Compute camera-facing quad corners (same math)
    // - Evaluate color + size keyframes (same logic)
    // - Submit via glBegin(GL_QUADS) or a vertex buffer
    // - Color: RGBA from particle_compute_color
    // - UV: 4 corners (0,0), (1,0), (1,1), (0,1)
    //
    // GX FIFO writes (DAT_cc008000) become vertex attribute submissions.
}
