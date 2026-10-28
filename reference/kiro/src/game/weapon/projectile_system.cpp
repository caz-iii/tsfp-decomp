// Address: 0x80086fa4 - 0x80087668 | Batch 182 | Date: 2026-08-11
// Projectile system: event registration, pool management, surface aim,
// tick update, and pool removal.

#include "projectile.h"
#include "entity.h"
#include "collision.h"
#include "sound.h"
#include "math_util.h"

// ============================================================================
// Globals
// ============================================================================
extern int DAT_8055f570;       // Projectile entity pool (8 slots, 4 bytes each)
extern int DAT_80611cac;       // Active projectile count
extern int DAT_80611ca8;       // Max projectile slots (8, divided by player count)
extern int DAT_80611d74;       // Camera/player data pointer

extern float FLOAT_806142e8;   // Initial aim constant (likely 0.0 or default angle)
extern float FLOAT_806142ec;   // Ray length for surface query
extern float FLOAT_806142f0;   // Epsilon for cross product validity
extern float FLOAT_806142f4;   // Newton-Raphson constant (0.5)
extern float FLOAT_806142f8;   // Rate limit delta / Newton-Raphson (1.5 or 3.0)
extern float FLOAT_806142fc;   // 1.0f
extern float FLOAT_80614300;   // Min clamp for dot product
extern float FLOAT_80614304;   // 180.0 (degrees)
extern float FLOAT_80614308;   // PI
extern float FLOAT_8061430c;   // Min pitch threshold
extern float FLOAT_80614310;   // 360.0 (degrees)
extern float FLOAT_80614314;   // -180.0 (degrees)
extern float FLOAT_80614318;   // Sine table scale factor
extern float FLOAT_8061431c;   // Fade multiplier (255.0)
extern float FLOAT_80614320;   // Random range divisor
extern float FLOAT_80614324;   // Probability scale
extern float FLOAT_80614328;   // Tick rate divisor
extern float FLOAT_8061432c;   // Volume min
extern float FLOAT_80614330;   // Radius scale

extern float FLOAT_806108ec;   // Delta time
extern int DAT_806109c0;       // Frame counter (int for double conversion trick)
extern int DAT_80583228;       // Sine lookup table (2048 entries)

extern float DAT_803cc370;     // Default light direction X
extern float DAT_803cc374;     // Default light direction Y
extern float DAT_803cc378;     // Default light direction Z

// Forward declarations
extern void FUN_800876a4(void);  // Projectile spawn event handler
extern void FUN_801c1714(void);  // Get player count

// ============================================================================
// Address: 0x80086fa4
// projectile_register_event_handlers - Registers projectile system events
// Event 0x1000 (type 1): projectile spawn handler (FUN_800876a4)
// Event 0x100000 (type 1): projectile impact handler (LAB_80087c14)
// Called by: game_tick_update (system init during frame loop setup)
// ============================================================================
void projectile_register_event_handlers(void) {
    event_set_handler(0x1000, 1, FUN_800876a4);
    event_set_handler(0x100000, 1, (void *)0x80087c14);  // LAB_80087c14
}

// ============================================================================
// Address: 0x80086fe8
// projectile_system_shutdown - Empty stub (no cleanup needed)
// ============================================================================
void projectile_system_shutdown(void) {
    return;
}

// ============================================================================
// Address: 0x80086fec
// projectile_system_reset - Resets projectile entity pool
// Clears 8 entity pointer slots, resets active count to 0.
// Max slots = 8 / player_count (for split-screen fairness).
// Called by: entity_system_reset
// ============================================================================
void projectile_system_reset(void) {
    int offset = 0;
    int count = 8;

    // Clear all 8 pool slots
    do {
        *(int *)((int)&DAT_8055f570 + offset) = 0;
        offset += 4;
        count--;
    } while (count != 0);

    DAT_80611cac = 0;
    DAT_80611ca8 = 8;

    // Divide max slots by player count for split-screen
    int playerCount = FUN_801c1714();
    if (playerCount > 1) {
        playerCount = FUN_801c1714();
        DAT_80611ca8 = DAT_80611ca8 / playerCount;
    }
}

// ============================================================================
// Address: 0x80087058
// projectile_init_aim_state - Initializes projectile aim interpolation
// Sets horizontal (0xc0) and vertical (0xc8) aim offsets to default,
// marks state byte 0x32 = 1 (needs first-frame initialization)
// Called by: projectile spawn, character aim init
// ============================================================================
void projectile_init_aim_state(int pEntity) {
    float defaultAim = FLOAT_806142e8;
    *(float *)(pEntity + 0xc0) = defaultAim;
    *(float *)(pEntity + 0xc8) = defaultAim;
    *(unsigned char *)(pEntity + 0x32) = 1;
}

// ============================================================================
// Address: 0x80087070
// projectile_update_surface_aim - Computes aim offset from ground surface
// MASSIVE function: casts ray downward to find ground triangle, computes
// surface normal via cross product, derives pitch/yaw steering angles,
// applies sin/cos lookup for X/Z aim offsets.
// Rate-limited when param_2 != 0 (smooth interpolation).
//
// Algorithm:
//   1. On first frame (byte 0x32==1), snap aim to default
//   2. Store previous aim values for rate limiting
//   3. Cast ray from entity position downward (FUN_80099aa4)
//   4. If no hit: reset aim to zero
//   5. If hit: compute triangle edges, cross product for normal
//   6. Normalize normal via Newton-Raphson inverse sqrt
//   7. Dot normal with light direction for slope factor
//   8. Clamp slope to [min, 1.0], convert to angle via acos (FUN_801314f4)
//   9. Compute heading from atan2 of normal XZ (FUN_80390aa4)
//   10. Subtract entity rotation, wrap to [-180, 180]
//   11. Convert to sin/cos via lookup table for final aim X/Z
//   12. If rate limit enabled: clamp change per frame
//
// Called by: projectile_tick_update, character ground aim
// ============================================================================
void projectile_update_surface_aim(int pEntity, int enableRateLimit) {
    float prevAimX, prevAimZ;
    float defaultAim = FLOAT_806142e8;

    // First frame snap
    if (*(char *)(pEntity + 0x32) == 1) {
        *(float *)(pEntity + 0xc0) = defaultAim;
        *(float *)(pEntity + 0xc8) = defaultAim;
        *(unsigned char *)(pEntity + 0x32) = 2;
    }

    prevAimX = *(float *)(pEntity + 0xc0);
    prevAimZ = *(float *)(pEntity + 0xc8);

    // Cast downward ray from entity position (entity+100 = position)
    int hitResult = 0;
    FUN_80099aa4(FLOAT_806142ec, *(float *)(DAT_80611d74 + 0xa0),
                 pEntity + 100, &hitResult, NULL, 0);

    if (hitResult == 0) {
        // No ground contact - reset aim
        *(float *)(pEntity + 0xc0) = FLOAT_806142e8;
        *(float *)(pEntity + 0xc8) = FLOAT_806142e8;
    } else {
        // Compute triangle edges from hit result vertices
        // hitResult+0x24 = v0, hitResult+0x3c = v1, hitResult+0x54 = v2
        float edge1[3], edge2[3];
        for (int i = 0; i < 3; i++) {
            float v0 = *(float *)(hitResult + 0x24 + i * 4);
            float v1 = *(float *)(hitResult + 0x3c + i * 4);
            float v2 = *(float *)(hitResult + 0x54 + i * 4);
            edge2[i] = v1 - v0;
            edge1[i] = v2 - v1;
        }

        // Cross product for surface normal
        float nx = edge2[2] * edge1[0] - edge2[0] * edge1[2];
        float ny = edge2[1] * edge1[2] - edge2[2] * edge1[1];
        float nz = edge2[0] * edge1[1] - edge2[1] * edge1[0];

        // Normalize via Newton-Raphson rsqrt
        float lenSq = nx * nx + ny * ny + nz * nz;
        if (lenSq >= FLOAT_806142f0) {
            float invLen = rsqrt_newton_raphson(lenSq);
            nx *= invLen;
            ny *= invLen;
            nz *= invLen;
        }

        // Dot with default light/up direction for slope angle
        float dot = nx * DAT_803cc370 + ny * DAT_803cc374 + nz * DAT_803cc378;
        dot = clamp(dot, FLOAT_80614300, FLOAT_806142fc);

        float angle = acos_approx(dot);  // FUN_801314f4
        float pitch = (FLOAT_80614304 * angle) / FLOAT_80614308;

        if (pitch >= FLOAT_806142fc && pitch > FLOAT_8061430c) {
            // Compute heading from atan2(nx, nz)
            float heading = atan2_approx(nx, nz);  // FUN_80390aa4
            float relativeYaw = (FLOAT_80614304 * heading) / FLOAT_80614308
                                - *(float *)(pEntity + 0x70);

            // Wrap to [-180, 180]
            while (relativeYaw <= FLOAT_80614314) relativeYaw += FLOAT_80614310;
            while (relativeYaw > FLOAT_80614304) relativeYaw -= FLOAT_80614310;

            // Convert to sin/cos via lookup table
            unsigned int tableIndex = (unsigned int)(FLOAT_80614318 *
                ((FLOAT_80614308 * relativeYaw) / FLOAT_80614304) + FLOAT_806142f4);
            float sinVal = ((float *)&DAT_80583228)[tableIndex & 0x7FF];
            float cosVal = ((float *)&DAT_80583228)[(tableIndex + 0x200) & 0x7FF];

            *(float *)(pEntity + 0xc0) = pitch * cosVal;
            *(float *)(pEntity + 0xc8) = pitch * (-sinVal);
        } else {
            *(float *)(pEntity + 0xc0) = FLOAT_806142e8;
            *(float *)(pEntity + 0xc8) = FLOAT_806142e8;
        }
    }

    // Rate limiting (smooth interpolation)
    if (enableRateLimit != 0) {
        float maxDelta = FLOAT_806142f8;

        float diffX = *(float *)(pEntity + 0xc0) - prevAimX;
        if (fabs(diffX) > maxDelta) {
            if (*(float *)(pEntity + 0xc0) <= prevAimX) {
                *(float *)(pEntity + 0xc0) = prevAimX - maxDelta;
            } else {
                *(float *)(pEntity + 0xc0) = prevAimX + maxDelta;
            }
        }

        float diffZ = *(float *)(pEntity + 0xc8) - prevAimZ;
        if (fabs(diffZ) > maxDelta) {
            if (*(float *)(pEntity + 0xc8) <= prevAimZ) {
                *(float *)(pEntity + 0xc8) = prevAimZ - maxDelta;
            } else {
                *(float *)(pEntity + 0xc8) = prevAimZ + maxDelta;
            }
        }
    }
}

// ============================================================================
// Address: 0x800873ec
// projectile_always_valid - Always returns 1 (validity stub)
// Used as a callback placeholder for projectile type validation
// ============================================================================
int projectile_always_valid(void) {
    return 1;
}

// ============================================================================
// Address: 0x800873f4
// projectile_tick_update - Per-frame projectile lifecycle update
// MASSIVE function handling:
//   - Velocity integration (position += velocity * dt via float at [puVar7+4])
//   - Lifetime management (compare elapsed vs duration at [puVar7+6])
//   - Fade phase: alpha decreases from 255 over fade duration [puVar7+7]
//   - Collision shape params update during fade (size 0xf, reduced alpha)
//   - 3D sound: volume fades with lifetime ratio, positioned at entity
//   - Collision radius scales with lifetime
//   - Random ricochet check: probability-based spark spawn (FUN_803591a8)
//   - Timer-based effect check (FUN_80364c40 on puVar7+0xf timer)
//   - Surface aim update (projectile_update_surface_aim with rate limit)
//   - Entity visibility toggle (flag 0x100 -> bit 0 flip)
//   - Destruction when lifetime exceeds duration + fade
//
// Called by: entity think callback system (registered at spawn)
// ============================================================================
void projectile_tick_update(int *pEntity) {
    int *puVar7 = (int *)pEntity[0x1f];  // Projectile-specific data pointer

    // Advance elapsed time
    puVar7[4] = (int)((float)puVar7[4] + FLOAT_806108ec * (FLOAT_806142fc / (float)/*dt*/0));

    float elapsed = (float)puVar7[4];

    if (elapsed < (float)puVar7[6]) {
        // Pre-fade phase: check for random ricochet spark
        if (elapsed < FLOAT_806142f8) {
            // Probability check for spark effect
            int rand = FUN_8013104c();
            float probability = (float)rand / FLOAT_80614320;
            if (probability < FLOAT_80614324 * FLOAT_806108ec * (FLOAT_80614328 / (float)/*dt*/0)) {
                if (puVar7[1] == 8 && ((*puVar7 & 1) == 0)) {
                    int target = FUN_800acc24(puVar7[2]);
                    if (target != 0) {
                        FUN_803591a8((int)pEntity);  // Spawn ricochet spark
                    }
                }
            }
        }
    } else {
        // Fade phase
        float fadeProgress = (elapsed - (float)puVar7[6]) / (float)puVar7[7];
        if (fadeProgress <= FLOAT_806142fc) {
            unsigned int alpha = (unsigned int)(-(FLOAT_8061431c * fadeProgress - FLOAT_8061431c));
            unsigned char collisionType = 4;
            if ((alpha & 0xFF) < 0x2C && puVar7[10] != 0) {
                collisionType = 2;
            }
            collision_set_shape_params((int *)pEntity[1], 0xf, 0xFFFFFFFF,
                                      collisionType, (char)alpha);
        }
    }

    // 3D sound update
    if (puVar7[0xb] != 0) {
        float volumeRatio = (float)puVar7[4] / (float)puVar7[5];
        float volume = FLOAT_806142fc - volumeRatio;
        sound_update_3d_source(volume, FLOAT_8061432c, FLOAT_806142fc,
                              &pEntity[0x8a], &pEntity[0x19], 0xBFB, 0);

        float radiusScale = (volumeRatio <= FLOAT_806142fc) ? volumeRatio : FLOAT_806142fc;
        collision_set_radius_and_flags(FLOAT_80614330 * radiusScale,
                                       (int *)pEntity[1], 10, 10, 10);
    }

    // Timer-based effect check
    int effectResult = FUN_80364c40((int *)(puVar7 + 0xf));
    if (effectResult != 0) {
        FUN_80087e3c();  // Apply timed effect
    }

    // Lifetime check
    if ((float)puVar7[4] < (float)puVar7[6] + (float)puVar7[7]) {
        // Still alive: toggle visibility and update surface aim
        unsigned int renderFlags = pEntity[10];
        if ((renderFlags & 0x100) == 0) {
            pEntity[10] = renderFlags | 1;
        } else {
            pEntity[10] = renderFlags & ~1u;
        }
        projectile_update_surface_aim((int)pEntity, 1);
    } else {
        // Expired: destroy
        entity_destroy_with_event(pEntity);
    }
}

// ============================================================================
// Address: 0x80087668
// projectile_pool_remove - Removes entity from projectile tracking pool
// Clears matching slot in the 8-entry projectile entity pool.
// Called by: projectile event handler (FUN_800876a4 -> on destroy)
// ============================================================================
void projectile_pool_remove(int pEntity) {
    int offset = 0;
    int remaining = DAT_80611ca8;

    if (remaining < 1) {
        return;
    }

    do {
        if (*(int *)((int)&DAT_8055f570 + offset) == pEntity) {
            *(int *)((int)&DAT_8055f570 + offset) = 0;
        }
        offset += 4;
        remaining--;
    } while (remaining != 0);
}


// ============================================================================
// Address: 0x800876a4
// projectile_on_destroy - Event handler for projectile entity destruction
// Cleans up particle effect timer, stops effect if active, removes from pool.
// Registered as handler for event type 0x1000 sub-type 1.
// ============================================================================
void projectile_on_destroy(int pEntity) {
    if (pEntity == 0) {
        return;
    }

    int pProjectileData = *(int *)(pEntity + 0x7c);

    // Stop any associated particle system
    FUN_8037ee2c();

    // Check timer-based effect and stop if active
    int effectActive = FUN_80364c40((int *)(pProjectileData + 0x3c));
    if (effectActive != 0) {
        FUN_803654f0();  // Kill particle effect
    }

    // Remove from projectile tracking pool
    projectile_pool_remove(pEntity);
}

// ============================================================================
// Address: 0x80087704
// projectile_compute_lifetime_params - Determines lifetime and fade duration
// Based on player count (split-screen reduces lifetime for performance),
// and whether the source entity has an active attached weapon/vehicle.
//
// Output: *pLifetime written to pfVar3, *pFadeDuration written to param_3
//
// Player count adjustments:
//   1 player: default values (DAT_805015bd == 10 path)
//   2 players: FLOAT_806142fc lifetime, FLOAT_80614350 fade
//   3+ players: FLOAT_806142e8 lifetime (zero = instant fade)
// If source has active weapon (type 8 with attached entity or timer):
//   Override to zero lifetime (immediate destruction)
// ============================================================================
void projectile_compute_lifetime_params(int pSourceEntity, void *pUnused, float *pFadeDuration) {
    float lifetime;
    float fadeDuration = FLOAT_8061430c;

    // Get output pointer from coroutine-style return
    float *pLifetime; // resolved from FUN_803826f0

    *pLifetime = FLOAT_80614348;
    *pFadeDuration = fadeDuration;

    if (DAT_805015bd != 10) {
        *pLifetime = FLOAT_8061434c;
        *pFadeDuration = fadeDuration;

        int playerCount = FUN_801c1714();
        if (playerCount == 2) {
            *pLifetime = FLOAT_806142fc;
            *pFadeDuration = FLOAT_80614350;
        } else {
            playerCount = FUN_801c1714();
            if (playerCount > 2) {
                *pLifetime = FLOAT_806142e8;
                *pFadeDuration = FLOAT_806142f8;
            }
        }
    }

    // Check if source entity has active weapon attachment
    int pSourceEntityResolved; // from coroutine
    if (pSourceEntityResolved != 0 && *(int *)(pSourceEntityResolved + 0x20) == 8) {
        int pCharData = *(int *)(pSourceEntityResolved + 0x7c);
        if (pCharData != 0) {
            // Check packed entity reference at +0xc08
            unsigned int packedRef = *(unsigned int *)(pCharData + 0xc08);
            int attachedEntity = 0;
            if (packedRef != 0) {
                attachedEntity = DAT_80611c24 + (packedRef >> 22) * 0x2e8;
                if (*(unsigned int *)(attachedEntity + 0x2c) != packedRef) {
                    attachedEntity = 0;
                    *(int *)(pCharData + 0xc08) = 0;
                }
            }

            // Also check timer at +0x4c
            if (attachedEntity != 0 || FUN_80364c40((int *)(pCharData + 0x4c)) != 0) {
                *pLifetime = FLOAT_806142e8;
                *pFadeDuration = FLOAT_806142f8;
            }
        }
    }
}

// ============================================================================
// Address: 0x80087828
// projectile_spawn - MASSIVE: Spawns a projectile entity from source
// Allocates from circular pool (8 slots), destroys oldest if full.
// Spawns entity with type 0x1000, copies transform+bones from source.
// Sets up projectile-specific data block (0x60 bytes):
//   - Weapon type, model ID, sound handle, lifetime, fade params
//   - Registers for think (projectile_tick_update) and render callbacks
//   - If source is character (type 8): copies weapon params, sound handle,
//     detaches weapon from character, sets up ricochet spark check
//
// Called by: many weapon fire functions
// ============================================================================
void projectile_spawn(/* params via coroutine */) {
    // (Pseudocode summary - full decompilation in Ghidra)
    //
    // 1. Save source entity orientation (aim angles at +0xc0, +0x70, +0xc8)
    // 2. If pool slot [DAT_80611cac] is occupied: destroy existing, clear slot
    // 3. entity_spawn_with_activation(type=0x1000, model=source+0x94, pos=source+100, orient)
    // 4. Store in pool, assign data block from DAT_8055f590[slot*0x60]
    // 5. memset data block to 0
    // 6. Advance circular index: (index+1) % DAT_80611ca8
    // 7. Copy transform + bones from source
    // 8. Set entity type flags = 0x1000
    // 9. Call projectile_compute_lifetime_params for duration/fade
    // 10. If source is character (type 8):
    //     - Copy weapon params (model ID, weapon type, fire mode)
    //     - Transfer sound handle from source to projectile
    //     - Clear weapon fire flag on source
    //     - Set up 3D sound params (lifetime, volume, radius)
    //     - Check for weapon attachment and transfer
    //     - Register for update type 2 (render)
    // 11. Reset elapsed time to 0
    // 12. Test frustum visibility for initial state
    // 13. Clear render flags (invisible initially except for visibility test)
    // 14. Register for update type 3 (think = projectile_tick_update)
    // 15. Attempt initial ricochet spark
    // 16. If source is character: fire notification event (0x3ef)
}

// ============================================================================
// Address: 0x80087b58
// projectile_spawn_impact_entity - Spawns impact/explosion effect entity
// Creates a child entity of type 0x100000 (impact) that follows the source.
// Registers for render (type 4) and think (type 2) callbacks.
// Stores reference to parent at entity+0x208.
//
// Called by: weapon impact handlers (FUN_800f7b0c, FUN_800f8390)
// Returns: spawned entity pointer
// ============================================================================
int projectile_spawn_impact_entity(/* coroutine params */, int pSourceEntity) {
    float orient[3];
    orient[0] = *(float *)(pSourceEntity + 0xc0);
    orient[1] = *(float *)(pSourceEntity + 0x70);
    orient[2] = *(float *)(pSourceEntity + 0xc8);

    int pImpactEntity = entity_spawn_with_activation(
        /* ... */ 0x100000,
        (unsigned int)*(unsigned short *)(pSourceEntity + 0x94),
        pSourceEntity + 100, orient, 0, 0);

    entity_copy_transform_state(pImpactEntity, pSourceEntity);

    *(int *)(pImpactEntity + 0x20) = 0x100000;
    *(unsigned int *)(pImpactEntity + 0x28) &= ~0x60u;  // Clear bits 5,6
    *(unsigned int *)(pImpactEntity + 0x28) &= ~1u;     // Clear bit 0

    // Store parent reference
    *(int *)(pImpactEntity + 0x208) = pSourceEntity;

    // Register for render callback (type 4) and think (type 2)
    entity_register_for_update(pImpactEntity, 4, -0x7ff783d4);  // projectile_impact_sync_position
    entity_register_for_update(pImpactEntity, 2, 0);

    return pImpactEntity;
}

// ============================================================================
// Address: 0x80087c2c
// projectile_impact_sync_position - Per-frame impact entity position sync
// Copies position, rotation, aim state, and room from parent entity.
// Then calls projectile_impact_copy_bones to sync bone matrices.
// Registered as type 4 (render) callback on impact entities.
// ============================================================================
void projectile_impact_sync_position(int pImpactEntity) {
    int pParent = *(int *)(pImpactEntity + 0x208);

    // Copy room assignment
    *(int *)(pImpactEntity + 0x34) = *(int *)(pParent + 0x34);

    // Copy position (3 floats at +0x64)
    *(int *)(pImpactEntity + 0x64) = *(int *)(pParent + 0x64);
    *(int *)(pImpactEntity + 0x68) = *(int *)(pParent + 0x68);
    *(int *)(pImpactEntity + 0x6c) = *(int *)(pParent + 0x6c);

    // Copy aim/rotation state
    *(unsigned char *)(pImpactEntity + 0x32) = *(unsigned char *)(pParent + 0x32);
    *(int *)(pImpactEntity + 0xc0) = *(int *)(pParent + 0xc0);
    *(int *)(pImpactEntity + 0x70) = *(int *)(pParent + 0x70);
    *(int *)(pImpactEntity + 0xc8) = *(int *)(pParent + 0xc8);

    // Sync room in render data
    int parentRoom = *(int *)(*(int *)(pParent + 4) + 0x30);
    if (*(int *)(*(int *)(pImpactEntity + 4) + 0x30) != parentRoom) {
        *(int *)(*(int *)(pImpactEntity + 4) + 0x30) = parentRoom;
    }

    // Copy bone matrices
    projectile_impact_copy_bones();
}

// ============================================================================
// Address: 0x80087cac
// projectile_impact_copy_bones - Copies bone matrices from parent to impact
// Uses coroutine-style iterator (FUN_803826ec/FUN_80382738).
// If parent is character (type 8): checks death animation first.
// If both entities are visible (flag 0x100 clear): copies all bone matrices.
// ============================================================================
void projectile_impact_copy_bones(void) {
    int pImpactEntity = FUN_803826ec();  // Current entity from iterator
    int deathAnimState = 0;
    int pParent = *(int *)(pImpactEntity + 0x208);

    // Check if parent is playing death animation
    if (*(int *)(pParent + 0x20) == 8) {
        deathAnimState = FUN_800f8c14(*(int *)(pParent + 0x7c) + 0xc68);
    }

    if (deathAnimState == 0) {
        // Only copy if both entities are visible (bit 8 clear)
        if ((*(unsigned int *)(pImpactEntity + 0x28) & 0x100) == 0 &&
            (*(unsigned int *)(pParent + 0x28) & 0x100) == 0) {

            // Copy all bone matrices (0x40 bytes each)
            int boneCount = *(int *)(*(int *)(*(int *)(pImpactEntity + 4) + 4) + 4);
            int offset = 0;
            for (int i = 0; i < boneCount; i++) {
                FUN_80130b90(
                    (float *)(*(int *)(*(int *)(pParent + 4) + 0x10) + offset),
                    (float *)(*(int *)(*(int *)(pImpactEntity + 4) + 0x10) + offset));
                offset += 0x40;
            }
        }
    }

    FUN_80382738();  // End iterator
}

// ============================================================================
// Address: 0x80087d60
// projectile_attach_effect - Attaches/starts a particle effect on projectile
// If no existing effect (piVar3 == null): creates new timer-based effect
// If existing: reattaches to new slot index, restarts particle system.
// Sets timer base to 0.0 at effect+0x48.
//
// Called by: FUN_80365e48 (particle system manager)
// ============================================================================
void projectile_attach_effect(/* many params */, int enableEffect) {
    int pEntity = FUN_803826ec();  // From iterator
    int pProjectileData = *(int *)(pEntity + 0x7c);

    // Compute effect slot index from data offset
    int effectSlotIndex = (pProjectileData + 0x7faa0a70) / 0x60 + 0xe8;

    int *pExistingEffect; // From iterator second return

    if (pExistingEffect == NULL) {
        if (enableEffect != 0) {
            // Create new timer-based effect
            int effectHandle = FUN_80364808(/* params */, pEntity, effectSlotIndex, 1);
            *(int *)(pProjectileData + 0x3c) = effectHandle;
            FUN_80023280(FLOAT_806142ec, FLOAT_806142fc);
        }
    } else {
        if (enableEffect != 0) {
            // Reuse existing effect handle
            *(int **)(pProjectileData + 0x3c) = pExistingEffect;
            *(int *)(*(int *)(pProjectileData + 0x3c) + 0x44) = *(int *)(pEntity + 0x2c);
        }
        // Reattach and restart
        FUN_803656c4(*pExistingEffect);
        FUN_8036568c(pExistingEffect, effectSlotIndex);
        FUN_8036530c();
        FUN_80023280(FLOAT_806142ec, FLOAT_806142fc);
    }

    // Reset effect timer
    *(float *)(pProjectileData + 0x48) = FLOAT_806142e8;

    FUN_80382738();  // End iterator
}

// ============================================================================
// Address: 0x80087e3c
// projectile_effect_tick - Per-frame particle effect update
// Advances effect timer, checks threshold for spawning trail particles.
// Uses coroutine-style iterator. Advances elapsed at data+0x48 by dt.
// If elapsed > FLOAT_80614354 and effect still has particles: spawn more.
// If trail effect lookup fails (0x746b5f6a = "tk_j"): accumulate time.
//
// Called by: projectile_tick_update
// ============================================================================
void projectile_effect_tick(void) {
    int pEntity = FUN_803826f0();  // Get current entity from stack
    int pProjectileData = *(int *)(pEntity + 0x7c);

    // Look up trail effect by magic ID "tk_j" (0x746b5f6a)
    int trailEffectId = FUN_8014f0d4(0x746b5f6a);

    // Compute delta time
    float dt = FLOAT_806108ec * (FLOAT_806142fc / (float)/*frame time*/0);

    // Update effect source position
    FUN_803652bc(pEntity, *(int *)(pProjectileData + 0x3c));
    FUN_80364cdc();  // Tick effect system

    if (trailEffectId == -1) {
        // No trail effect found: accumulate time, check spawn threshold
        if (*(float *)(pProjectileData + 0x48) > FLOAT_80614354) {
            unsigned int hasParticles = FUN_80365914(*(int *)(pProjectileData + 0x3c));
            if (hasParticles != 0) {
                FUN_803654f0();  // Spawn trail particle burst
            }
        }
        *(float *)(pProjectileData + 0x48) += dt;
    } else {
        // Trail effect exists: reset timer
        *(float *)(pProjectileData + 0x48) = FLOAT_806142e8;
    }

    FUN_8038273c();  // End iterator
}
