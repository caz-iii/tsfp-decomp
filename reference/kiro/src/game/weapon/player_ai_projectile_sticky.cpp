// Address: 0x800c8f38 - 0x800ca07c | Batch 275 | Date: 2026-08-25
// Sticky/bouncing projectile entity lifecycle: spawn, tick, trail, damage.
// Covers the full lifecycle of a "sticky grenade" style projectile that
// moves along a sine-wave path, sticks on contact, and damages nearby enemies.

#include "common.h"
#include "player_ai_projectile.h"
#include "particle.h"
#include "collision.h"
#include "entity.h"

// ============================================================================
// 0x800c8f38 | player_ai_projectile_spawn_burst_particle
// Spawns a burst particle with random direction offset.
// Generates 3 random components, adds upward bias to Y, calls spawn helper.
// ============================================================================
void player_ai_projectile_spawn_burst_particle(int pSlot) {
    float dirX = FLOAT_80614ae4 * (FUN_801310c0() - FLOAT_80614a78);
    float dirY = FLOAT_80614ae4 * (FUN_801310c0() - FLOAT_80614a78) + FLOAT_80614c8c;
    float dirZ = FLOAT_80614ae4 * (FUN_801310c0() - FLOAT_80614a78);

    FUN_800cd730(FLOAT_80614b48, 0.0f, FLOAT_80614bcc, 7, 0xD5,
                 (int*)(pSlot + 0x1C), &dirX);
}

// ============================================================================
// 0x800c8fcc | player_ai_projectile_spawn_pair
// Spawns a pair of projectile physics entities from a 2-entry table.
// Each entry at DAT_8040584c has 8-byte stride (model ID + param).
// ============================================================================
void player_ai_projectile_spawn_pair(void) {
    float* origin = (float*)FUN_803826e8();

    for (int i = 0; i < 2; i++) {
        int tableOffset = i * 8;
        int modelId = *(int*)((int)&DAT_8040584c + tableOffset);
        int param = *(int*)((int)&DAT_80405850 + tableOffset);
        player_ai_projectile_spawn_physics_entity(origin, modelId, param);
    }
    FUN_80382734();
}

// ============================================================================
// 0x800c9058 | player_ai_projectile_spawn_physics_entity
// Spawns a projectile entity with physics simulation.
// Creates entity, allocates render instance, sets random velocity/position,
// registers for per-frame updates, sets lifetime.
// ============================================================================
void player_ai_projectile_spawn_physics_entity(float* origin, int modelId, int param) {
    float spawnDir[3] = { DAT_803cc730, DAT_803cc734, DAT_803cc738 };

    int* entity = entity_spawn_with_activation(1, modelId, origin, spawnDir, 0, 0);
    entity[10] = entity[10] | 0x62; // Set flags

    int renderInst = FUN_80144864();
    entity[0x20] = renderInst;
    if (renderInst == 0) {
        entity_destroy_with_event(entity);
        return;
    }

    FUN_80145090(entity[0x20], entity, 9, param, 0.0f);

    // Random velocity (XZ scaled, Y different scale)
    float velX = FLOAT_80614a9c * (FUN_8013104c_norm() - FLOAT_80614a78) * FLOAT_80614d38;
    float velY = FLOAT_80614a9c * (FUN_8013104c_norm() - FLOAT_80614a78) * FLOAT_80614d38;
    float velZ = FLOAT_80614a9c * (FUN_8013104c_norm() - FLOAT_80614a78) * FLOAT_80614d3c;

    // Random position offset
    float posX = FLOAT_80614a9c * (FUN_8013104c_norm() - FLOAT_80614a78) * FLOAT_80614d40 + origin[0];
    float posY = FLOAT_80614a9c * (FUN_8013104c_norm() - FLOAT_80614a78) * FLOAT_80614d40 + origin[1];
    float posZ = FLOAT_80614a9c * (FUN_8013104c_norm() - FLOAT_80614a78) * FLOAT_80614d40 + origin[2];

    float vel[3] = { velX, velY, velZ };
    float pos[3] = { posX, posY, posZ };
    FUN_8014607c(entity[0x20], vel, pos);

    entity[0x73] = (int)FLOAT_80614bd8; // Gravity scale
    entity_register_for_update((int)entity, 3, -0x7FF36CC4);
    entity[0x68] = 1; // Active flag

    // Lifetime = FLOAT_80614b20 * frame_rate_factor
    int lifetime = (int)(FLOAT_80614b20 * (float)(DAT_806109c0));
    entity[0x69] = lifetime;

    FUN_8038273c();
}

// ============================================================================
// 0x800c933c | player_ai_projectile_trail_tick
// Per-frame tick for projectile trail particle entity.
// Decrements lifetime; when alive spawns directional particle.
// When lifetime expires, destroys entity.
// ============================================================================
void player_ai_projectile_trail_tick(int* pEntity) {
    pEntity[0x69] = pEntity[0x69] - DAT_806108e4;

    if ((float)(pEntity[0x69]) >= 0.0f) {
        // Generate random direction
        float dirX = FLOAT_80614a9c * (FUN_8013104c_norm() - FLOAT_80614a78) * FLOAT_80614ae4;
        float dirY = FLOAT_80614a9c * (FUN_8013104c_norm() - FLOAT_80614a78) * FLOAT_80614ae4 + 1.0f;
        float dirZ = FLOAT_80614a9c * (FUN_8013104c_norm() - FLOAT_80614a78) * FLOAT_80614ae4;

        // Normalize if length > threshold
        float lenSq = dirX * dirX + dirY * dirY + dirZ * dirZ;
        if (FLOAT_80614c00 < lenSq) {
            float invLen = fast_inv_sqrt(lenSq);
            invLen = invLen * -(FLOAT_80614a78 * lenSq * invLen * invLen - FLOAT_80614a7c);
            float scale = 1.0f * invLen * -(FLOAT_80614a78 * lenSq * invLen * invLen - FLOAT_80614a7c);
            dirX *= scale;
            dirY *= scale;
            dirZ *= scale;
        }

        // Compute intensity from remaining lifetime
        float maxLife = FLOAT_80614b20 * (float)(DAT_806109c0);
        float remaining = (float)(pEntity[0x69]);
        float ratio = FLOAT_80614a88 * maxLife;
        if (remaining < ratio) {
            float intensity = FLOAT_80614ae4 + (1.0f - remaining / ratio);
            FUN_800cd838(intensity, 0.0f, FLOAT_80614acc, 1, pEntity, pEntity + 0x19, &dirX);
        }
    } else {
        entity_destroy_with_event(pEntity);
    }
}

// ============================================================================
// 0x800c9580 | player_ai_projectile_register_callback
// Sets up entity callback: init helper + assigns update function pointer.
// ============================================================================
void player_ai_projectile_register_callback(int pEntity) {
    FUN_8013f920(pEntity);
    *(void (**)(int))(pEntity + 0x154) = player_ai_projectile_update_callback;
}

// ============================================================================
// 0x800c95b8 | player_ai_projectile_update_callback
// Simple per-frame update callback wrapper.
// ============================================================================
void player_ai_projectile_update_callback(int pEntity) {
    FUN_8013f948(pEntity);
}

// ============================================================================
// 0x800c95d8 | player_ai_projectile_spawn_simple_particle
// Minimal stub: calls particle_spawn_simple().
// ============================================================================
void player_ai_projectile_spawn_simple_particle(void) {
    particle_spawn_simple();
}

// ============================================================================
// 0x800c95f8 | player_ai_projectile_sticky_on_spawn
// On-spawn handler for sticky projectile.
// Allocates 0x188-byte tracking block (ring buffer for trail positions),
// initializes timing, spawns particle emitter, computes facing angle.
// ============================================================================
void player_ai_projectile_sticky_on_spawn(int pSlot) {
    int* trackBlock = block_alloc_dispatch(0x188, 1, pSlot, &LAB_800ca36c);
    *(int**)(pSlot + 0x208) = trackBlock;

    int track = *(int*)(pSlot + 0x208);

    // Initialize ring buffer state
    *(int*)(track + 0x17C) = 0;       // stuck flag
    *(float*)(track + 0x178) = 0.0f;  // fade timer
    *(int*)(track + 0x16C) = 0;       // write index
    // Max entries = (FLOAT_80614bc0 * frame_rate_factor) / FLOAT_80614bd4
    *(int*)(track + 0x170) = (int)((FLOAT_80614bc0 * (float)(DAT_806109c0)) / FLOAT_80614bd4);
    *(int*)(track + 0x174) = 0;       // wrapped flag
    *(int*)(track + 0x168) = DAT_80611928; // frame stamp

    particle_handle_clear((int*)(track + 0x180));
    particle_handle_clear((int*)(track + 0x184));

    // Spawn trail particle emitter
    int playerIdx = FUN_8037daf0(-0x7FF368FC, 0x55, pSlot, &LAB_800ca36c);
    FUN_801cacc0(FLOAT_80614b54, track + 0x180, (float*)(pSlot + 0x1C),
                 (char)((pSlot + playerIdx * 0x22C - DAT_80611dc0) / 0x22C));

    // Compute initial facing angle from direction XZ
    float angle = FUN_80390aa4(*(float*)(pSlot + 0x64), *(float*)(pSlot + 0x6C));
    *(float*)(pSlot + 0x1DC) = angle;
}

// ============================================================================
// 0x800c9704 | player_ai_projectile_get_random_spawn_pos
// Gets a random spawn position from player bone data or camera fallback.
// ============================================================================
void player_ai_projectile_get_random_spawn_pos(float* outPos, int playerIdx) {
    FUN_803826f0();

    int playerBase = DAT_80611dc0 + playerIdx * 0x22C;
    int boneData = *(int*)(playerBase + 0x208);

    if (boneData == 0) {
        // No bone data: use player camera position
        outPos[0] = *(float*)(playerBase + 0x1C);
        outPos[1] = *(float*)(playerBase + 0x20);
        outPos[2] = *(float*)(playerBase + 0x24);
    } else {
        int boneCount;
        if (*(int*)(boneData + 0x174) == 0) {
            boneCount = *(int*)(boneData + 0x16C);
        } else {
            boneCount = *(int*)(boneData + 0x170);
        }

        if (boneCount < 1) {
            // No bones: use camera with Y offset
            outPos[0] = *(float*)(playerBase + 0x1C);
            outPos[1] = *(float*)(playerBase + 0x20) - 1.0f;
            outPos[2] = *(float*)(playerBase + 0x24);
        } else {
            // Pick random bone position
            uint rng = FUN_8013104c();
            int boneIdx = (rng % boneCount) * 0xC;
            outPos[0] = *(float*)(boneData + boneIdx);
            outPos[1] = *(float*)(boneData + boneIdx + 4);
            outPos[2] = *(float*)(boneData + boneIdx + 8);
        }
    }
    FUN_8038273c();
}

// ============================================================================
// 0x800c97e4 | player_ai_projectile_sticky_tick
// MASSIVE per-frame tick for sticky/bouncing projectile.
// Moves along sine-wave path, floor-snaps, checks ring buffer proximity
// damage, raycasts for collision, dispatches damage on hit,
// manages particle trail fade-out when stuck.
// ============================================================================
void player_ai_projectile_sticky_tick(void) {
    int* slot = (int*)FUN_803826e8();
    int* trackBlock = (int*)slot[0x82];

    // Lazy-allocate tracking block on first tick
    if (trackBlock == NULL) {
        trackBlock = block_alloc_dispatch(0x188, 1, slot, &LAB_800ca36c);
        slot[0x82] = (int)trackBlock;
        if (trackBlock == NULL) return;

        trackBlock[0x5F] = 0;       // stuck flag
        trackBlock[0x5E] = 0.0f;    // fade timer
        trackBlock[0x5B] = 0;       // write index
        int maxEntries = (int)((FLOAT_80614bc0 * (float)(DAT_806109c0)) / FLOAT_80614bd4);
        trackBlock[0x5C] = maxEntries;
        trackBlock[0x5D] = 0;       // wrapped flag
        trackBlock[0x5A] = DAT_80611928;
    }

    // Get AABB for scene node
    float aabbMin[3], aabbMax[3];
    scene_get_node_aabb(slot[0x3C], aabbMin, aabbMax);

    // Floor query to snap Y position
    float floorY = floor_query_with_interact(FLOAT_80614aa0, FLOAT_80614aa0, slot + 7, 0, NULL, 1);
    if (fabsf(floorY - (float)slot[8]) <= FLOAT_80614a9c) {
        // Smooth snap to floor
        float blend = FLOAT_80614b1c;
        float offset = FLOAT_80614a70;
        for (int i = 0; i < (int)DAT_806108e4; i++) {
            slot[8] = (int)(offset + blend * ((float)slot[8] - (offset + floorY)) + floorY);
        }
    } else {
        // Reverse X direction (bounce off wall)
        slot[0x19] = (int)(-(float)slot[0x19]);
    }

    // Update movement angle with random perturbation
    float rngNorm = (FUN_8013104c_norm() - FLOAT_80614a78);
    float angularAccel = FLOAT_80614ac4 * rngNorm;
    slot[0x77] = (int)((1.0f / (float)(DAT_806109c0)) *
                       ((float)(DAT_806108e4) * angularAccel) + (float)slot[0x77]);

    // Compute velocity from angle (sine wave path)
    uint angleIdx = (uint)(FLOAT_80614c2c * (float)slot[0x77] + FLOAT_80614a78);
    slot[0x19] = (int)((float)slot[0x22] * ((float*)&DAT_80583228)[angleIdx & 0x7FF]);
    slot[0x1A] = (int)0.0f;
    int cosIdx = (int)(FLOAT_80614c2c * (float)slot[0x77] + FLOAT_80614a78);
    slot[0x1B] = (int)((float)slot[0x22] *
                       *(float*)((int)&DAT_80583228 + ((cosIdx + 0x200) * 4 & 0x1FFC)));

    // Check ring buffer positions for proximity damage to player
    int startIdx = (int)trackBlock[0x5B] % 3;
    int maxIdx = (trackBlock[0x5D] == 0) ? trackBlock[0x5B] : trackBlock[0x5C];

    for (int i = startIdx; i < maxIdx; i += 3) {
        float* bufPos = (float*)((int)trackBlock + i * 0xC);
        float dx = *(float*)(DAT_80611d74 + 0xA8) - bufPos[0];
        float dy = *(float*)(DAT_80611d74 + 0xAC) - bufPos[1];
        float dz = *(float*)(DAT_80611d74 + 0xB0) - bufPos[2];
        float distSq = dx * dx + dy * dy + dz * dz;

        if (distSq < FLOAT_80614aa0) {
            float dist = sqrtf(distSq);
            if (dist == 0.0f) dist = 1.0f;
            float invDist = 1.0f / dist;
            float normX = dx * invDist;
            float normY = dy * invDist;
            float normZ = dz * invDist;

            // Dispatch proximity damage
            switch_slot_reset((int)local_1c4);
            local_1c4[0] = *(int*)(DAT_80611d74 + 0x14);
            damage_dispatch_to_entity(local_1c4);
        }
    }

    // Record position in ring buffer (every 2+ frames)
    if (trackBlock[0x5A] + 2 <= DAT_80611928) {
        int writeIdx = trackBlock[0x5B];
        trackBlock[writeIdx * 3] = slot[7];
        trackBlock[writeIdx * 3 + 1] = slot[8];
        trackBlock[writeIdx * 3 + 2] = slot[9];
        trackBlock[0x5A] = DAT_80611928;
        trackBlock[0x5B] = trackBlock[0x5B] + 1;
        if (trackBlock[0x5B] >= trackBlock[0x5C]) {
            trackBlock[0x5B] = 0;
            trackBlock[0x5D] = 1; // wrapped
        }
    }

    // Raycast for collision
    float dt = FLOAT_806108ec * (1.0f / (float)(DAT_806109c0));
    float moveX = (float)slot[0x19] * dt;
    float moveY = (float)slot[0x1A] * dt;
    float moveZ = (float)slot[0x1B] * dt;
    int damageInfo[2] = { 0, 0 };
    float hitPos[3], hitNormal[3];
    int hitEntity = player_ai_fire_raycast_simple(slot, &moveX, hitPos, hitNormal, damageInfo);

    if (damageInfo[0] > 0) {
        player_ai_on_damage_received(slot, (int)damageInfo);
    }

    // Ignore hits against own shooter in early frames
    if (slot[0x42] == hitEntity && (float)slot[0x26] < FLOAT_80614aa0) {
        hitEntity = 0;
    }

    // Check proximity to player (close-range damage)
    float px = *(float*)(DAT_80611d74 + 0xA8) - (float)slot[7];
    float py = *(float*)(DAT_80611d74 + 0xAC) - (float)slot[8];
    float pz = *(float*)(DAT_80611d74 + 0xB0) - (float)slot[9];
    if (px * px + py * py + pz * pz < FLOAT_80614acc) {
        hitEntity = *(int*)(DAT_80611d74 + 0x14);
        // Override hit position to player center
        hitPos[0] = *(float*)(DAT_80611d74 + 0xA8);
        hitPos[1] = *(float*)(DAT_80611d74 + 0xAC);
        hitPos[2] = *(float*)(DAT_80611d74 + 0xB0);
        hitNormal[0] = 0.0f;
        hitNormal[1] = 1.0f;
        hitNormal[2] = 0.0f;
    }

    // Skip certain entity types (type 0x1B9 with flag bit 2 clear)
    if (hitEntity != 0 && *(short*)(hitEntity + 0x94) == 0x1B9 &&
        (*(uint*)(*(int*)(hitEntity + 4) + 0x30) & 2) == 0) {
        hitEntity = 0;
    }

    // Process hit (first time only, tracked by stuck flag)
    if (hitEntity != 0 && trackBlock[0x5F] == 0) {
        int entityType = *(int*)(hitEntity + 0x20);

        // Normalize direction
        float speed = sqrtf((float)slot[0x1B] * (float)slot[0x1B] +
                            (float)slot[0x19] * (float)slot[0x19] +
                            (float)slot[0x1A] * (float)slot[0x1A]);
        float invSpeed = 1.0f / speed;
        float dirX = (float)slot[0x19] * invSpeed;
        float dirY = (float)slot[0x1A] * invSpeed;
        float dirZ = (float)slot[0x1B] * invSpeed;

        // Compute reflection
        float dot = dirX * hitNormal[0] + dirY * hitNormal[1] + dirZ * hitNormal[2];
        float reflX = -(FLOAT_80614ae4 * (hitNormal[0] * (dot + dot)) - dirX);
        float reflY = -(FLOAT_80614ae4 * (hitNormal[1] * (dot + dot)) - dirY);
        float reflZ = -(FLOAT_80614ae4 * (hitNormal[2] * (dot + dot)) - dirZ);

        // Distance traveled
        float dx2 = hitPos[0] - (float)slot[7];
        float dy2 = hitPos[1] - (float)slot[8];
        float dz2 = hitPos[2] - (float)slot[9];
        slot[0x3D] = (int)((float)slot[0x3D] + dx2*dx2 + dy2*dy2 + dz2*dz2);

        if (entityType == 8) {
            player_ai_impact_dispatch(slot, *slot, hitEntity, hitPos, hitNormal, &reflX, slot[0x42]);
            if (*(int*)(hitEntity + 0x150) == 0) {
                FUN_800f3e60(); // Kill confirm
            }
            player_ai_voice_pitch_update(slot);
        }

        player_ai_alert_fire_radius(FLOAT_80614b20, (int)slot, *(int*)(hitEntity + 0x20), hitPos);

        int validSurface = collision_hit_is_valid_surface();
        if (validSurface != 0) {
            FUN_800e95fc(*slot, 0, slot[0x3C], hitEntity, hitPos, hitNormal);
        }

        // Dispatch damage
        switch_slot_reset((int)local_220);
        float dmg = switch_slot_load_from_def((int)local_220, slot);
        local_220[0] = hitEntity;
        if (*(short*)(hitEntity + 0x94) == 0x1B9) {
            damage_dispatch_to_entity(dmg, local_220);
        } else {
            damage_resolve_and_dispatch(dmg, local_220);
        }

        // Mark as stuck
        trackBlock[0x5E] = (int)1.0f;
        trackBlock[0x5F] = 1;
        FUN_801cc6dc((float*)(slot + 7)); // Stick sound/effect
    }

    // Position update or fade-out
    if (trackBlock[0x5F] == 0) {
        // Still flying: advance position
        slot[7] = (int)((float)slot[7] + moveX);
        slot[8] = (int)((float)slot[8] + moveY);
        slot[9] = (int)((float)slot[9] + moveZ);
    } else {
        // Stuck: fade out particle trail then deactivate
        float fadeRate = (float)(DAT_806108e4) * (1.0f / (float)(DAT_806109c0));
        trackBlock[0x5E] = (int)(-(fadeRate - (float)trackBlock[0x5E]));
        float fadeProgress = (float)trackBlock[0x5E];

        if (fadeProgress >= 0.0f) {
            // Compute normalized fade for particle drag
            float maxLife = FLOAT_80614a9c * (1.0f / (float)(DAT_806109c0)) * (float)trackBlock[0x5C];
            float threshold = 1.0f - maxLife;
            float dragFade = 0.0f;
            if (fadeProgress < threshold) {
                dragFade = (threshold - fadeProgress) / maxLife;
            }

            // Apply drag to trail particles
            if (particle_handle_is_valid(&trackBlock[0x60])) {
                particle_cached_set_drag(FLOAT_80614b18 * dragFade, &trackBlock[0x60]);
            }
            if (particle_handle_is_valid(&trackBlock[0x61])) {
                particle_cached_set_drag(FLOAT_80614b18 * dragFade, &trackBlock[0x61]);
            }
        } else {
            // Fade complete: destroy particles and deactivate
            particle_handle_destroy(&trackBlock[0x60]);
            particle_handle_destroy(&trackBlock[0x61]);
            block_free_simple(slot[0x82]);
            player_ai_slot_deactivate();
        }
    }

    FUN_80382734();
}
