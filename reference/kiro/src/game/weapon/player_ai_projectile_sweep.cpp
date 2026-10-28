// Address: 0x800ca09c - 0x800cb724 | Batch 276 | Date: 2026-08-25
// Sweeping/shockwave projectile system + plasma projectile tick + ricochet.
// Also includes sticky trail render and utility functions.

#include "common.h"
#include "player_ai_projectile.h"
#include "particle.h"
#include "collision.h"
#include "entity.h"
#include "scene.h"

// ============================================================================
// 0x800ca09c | player_ai_projectile_sticky_trail_render
// Renders sticky projectile trail as GX tristrip from ring buffer.
// Walks ring buffer backwards, submitting vertex pairs with fading color.
// ============================================================================
void player_ai_projectile_sticky_trail_render(void) {
    int slot = FUN_803826d8();
    int* trackBlock = *(int**)(slot + 0x208);

    int totalEntries;
    if (trackBlock[0x5D] == 0) {
        totalEntries = trackBlock[0x5B];
    } else {
        totalEntries = trackBlock[0x5C];
    }
    uint halfCount = (totalEntries - 1) / 2;

    // Lazy-alloc if needed (same pattern as sticky_tick)
    if (trackBlock == NULL) {
        trackBlock = block_alloc_dispatch(0x188, 1, slot, &LAB_800ca36c);
        *(int**)(slot + 0x208) = trackBlock;
        if (trackBlock == NULL) return;
        // ... init same as sticky_tick ...
    }

    if (halfCount > 1) {
        int prevBlend = FUN_8016f00c(2);
        int prevCull = FUN_8016e8ac(2);
        uint prevZMode = FUN_80172a04(0);

        // Walk ring buffer from newest to oldest, stepping by 2
        int readIdx = trackBlock[0x5B] - 1;
        if (readIdx < 0) readIdx += trackBlock[0x5C];

        int endIdx = readIdx + halfCount * -2;
        if (endIdx < 0) endIdx += trackBlock[0x5C];

        int texId = sprite_get_texture_id(0);
        FUN_80172a4c(2, 0x92, 0, (short)halfCount * 2, texId);

        uint vertIdx = 0;
        while (readIdx != endIdx) {
            // Compute fade: alpha ramps from 0 at ends to 1 at center
            float normalizedPos = (float)(vertIdx - 2) / (float)(halfCount - 4);
            if ((float)(vertIdx) > (float)(halfCount) * FLOAT_80614a78) {
                normalizedPos = 1.0f - normalizedPos;
            }
            float alpha = fmaxf(0.0f, normalizedPos);

            int* posData = (int*)((int)trackBlock + readIdx * 12);

            // Vertex 1: position from buffer
            GX_FIFO_F32 = *(float*)posData;
            GX_FIFO_F32 = *(float*)(posData + 1);
            GX_FIFO_F32 = *(float*)(posData + 2);
            GX_FIFO_U32 = ((int)(FLOAT_80614cdc * FLOAT_80614a9c * alpha) & 0xFF) | 0x7F7F4600;
            GX_FIFO_F32 = 0.0f;
            GX_FIFO_F32 = 0.0f;

            // Vertex 2: same XZ but Y+offset
            GX_FIFO_F32 = *(float*)posData;
            GX_FIFO_F32 = FLOAT_80614a78 + *(float*)(posData + 1);
            GX_FIFO_F32 = *(float*)(posData + 2);
            GX_FIFO_U32 = 0x7F7F4600; // No alpha at top
            GX_FIFO_F32 = 0.0f;
            GX_FIFO_F32 = 0.0f;

            readIdx -= 2;
            if (readIdx < 0) readIdx += trackBlock[0x5C];
            vertIdx++;
        }

        FUN_80172c54();
        FUN_8016f00c(prevBlend);
        FUN_8016e8ac(prevCull);
        FUN_80172a04(prevZMode);
    }
    FUN_80382724();
}

// ============================================================================
// 0x800ca380 | player_ai_projectile_clear_entity_refs
// Clears all projectile slot references to a destroyed entity.
// Iterates 0x55 player slots (0x22C stride each).
// ============================================================================
void player_ai_projectile_clear_entity_refs(int entityId) {
    int offset = 0;
    for (int i = 0; i < 0x55; i++) {
        int* slotBase = (int*)(DAT_80611dc0 + offset);
        if (*slotBase != -1 && slotBase[0x41] == entityId) {
            slotBase[0x41] = 0;
        }
        offset += 0x22C;
    }
}

// ============================================================================
// 0x800ca3c0 | player_ai_projectile_sweep_init
// Initializes sweeping/shockwave projectile.
// Allocates 0x210 block with 33 particle entries (0x10 stride each),
// queries floor for ground plane, computes orthogonal basis from triangle.
// ============================================================================
void player_ai_projectile_sweep_init(void) {
    int slot = FUN_803826e8();

    // Advance time accumulator
    *(float*)(slot + 0x98) += FLOAT_806108ec * (1.0f / (float)(DAT_806109c0));

    // Allocate 0x210-byte particle tracking block
    int* block = block_alloc_dispatch(0x210, 1, slot, &LAB_800ca36c);
    *(int**)(slot + 0x208) = block;

    // Initialize 33 particles at spawn position with random angular offsets
    for (int i = 0; i < 0x21; i++) {
        float* entry = (float*)(block + i * 4); // 0x10 stride
        uint rng = FUN_8013104c();
        entry[3] = FLOAT_80614be8 * ((float)(rng) / FLOAT_80614bf4); // Random angle
        entry[0] = *(float*)(slot + 0x1C);
        entry[1] = *(float*)(slot + 0x20);
        entry[2] = *(float*)(slot + 0x24);
    }

    // Query floor to get ground plane
    int triData[2];
    float floorY = floor_query_with_interact(FLOAT_80614aa0, FLOAT_80614aa0,
                                             slot + 0x1C, triData, NULL, 1);
    if (floorY != FLOAT_80614c34) { // Valid floor found
        *(float*)(slot + 0x20) = floorY;

        // Compute tangent from triangle edge 1 (vertex[1] - vertex[3])
        *(float*)(slot + 0x1C4) = *(float*)(triData[0] + 0x3C) - *(float*)(triData[0] + 0x54);
        *(float*)(slot + 0x1C8) = *(float*)(triData[0] + 0x40) - *(float*)(triData[0] + 0x58);
        *(float*)(slot + 0x1CC) = *(float*)(triData[0] + 0x44) - *(float*)(triData[0] + 0x5C);

        // Compute binormal from triangle edge 2 (vertex[1] - vertex[0])
        *(float*)(slot + 0x1D0) = *(float*)(triData[0] + 0x3C) - *(float*)(triData[0] + 0x24);
        *(float*)(slot + 0x1D4) = *(float*)(triData[0] + 0x40) - *(float*)(triData[0] + 0x28);
        *(float*)(slot + 0x1D8) = *(float*)(triData[0] + 0x44) - *(float*)(triData[0] + 0x2C);

        // Normalize tangent
        vec3_normalize((float*)(slot + 0x1C4));
        // Normalize binormal
        vec3_normalize((float*)(slot + 0x1D0));

        // Compute normal = tangent x binormal
        *(float*)(slot + 0x180) = *(float*)(slot + 0x1C8) * *(float*)(slot + 0x1D8) -
                                  *(float*)(slot + 0x1CC) * *(float*)(slot + 0x1D4);
        *(float*)(slot + 0x184) = *(float*)(slot + 0x1CC) * *(float*)(slot + 0x1D0) -
                                  *(float*)(slot + 0x1C4) * *(float*)(slot + 0x1D8);
        *(float*)(slot + 0x188) = *(float*)(slot + 0x1C4) * *(float*)(slot + 0x1D4) -
                                  *(float*)(slot + 0x1C8) * *(float*)(slot + 0x1D0);

        // Re-orthogonalize if needed (cross product magnitude check)
        float normalLenSq = /* ... */;
        if (fabsf(normalLenSq - 1.0f) > FLOAT_80614aa0) {
            vec3_normalize((float*)(slot + 0x180));
            // Recompute binormal = normal x tangent
            *(float*)(slot + 0x1D0) = *(float*)(slot + 0x184) * *(float*)(slot + 0x1CC) -
                                      *(float*)(slot + 0x188) * *(float*)(slot + 0x1C8);
            *(float*)(slot + 0x1D4) = *(float*)(slot + 0x188) * *(float*)(slot + 0x1C4) -
                                      *(float*)(slot + 0x180) * *(float*)(slot + 0x1CC);
            *(float*)(slot + 0x1D8) = *(float*)(slot + 0x180) * *(float*)(slot + 0x1C8) -
                                      *(float*)(slot + 0x184) * *(float*)(slot + 0x1C4);
        }
    }
    FUN_80382734();
}

// ============================================================================
// 0x800ca7e4 | player_ai_projectile_sweep_damage_check
// Queries scene nodes in frustum, tests entities in annular distance band,
// dispatches damage type 0x30 to valid targets.
// ============================================================================
void player_ai_projectile_sweep_damage_check(void) {
    int slot = FUN_803826d4();
    float time = *(float*)(slot + 0x98);

    // Compute inner/outer radii of the expanding annulus
    float outerRadius = FLOAT_80614b9c * time;
    float innerRadius = FLOAT_80614b9c * (time - FLOAT_80614aa0);
    float outerRadiusSq = (outerRadius * *(float*)(slot + 0x184)) *
                          (outerRadius * *(float*)(slot + 0x184));
    float innerRadiusSq = (innerRadius * *(float*)(slot + 0x184)) *
                          (innerRadius * *(float*)(slot + 0x184));

    // Compute frustum endpoints for query
    float endA[3], endB[3];
    // endA = origin + tangent * (-outerRadius)
    // endB = origin + tangent * outerRadius
    endA[0] = DAT_803cc73c * (-outerRadius) + *(float*)(slot + 0x1C);
    endA[1] = DAT_803cc740 * (-outerRadius) + *(float*)(slot + 0x20);
    endA[2] = DAT_803cc744 * (-outerRadius) + *(float*)(slot + 0x24);
    endB[0] = DAT_803cc73c * outerRadius + *(float*)(slot + 0x1C);
    endB[1] = DAT_803cc740 * outerRadius + *(float*)(slot + 0x20);
    endB[2] = DAT_803cc744 * outerRadius + *(float*)(slot + 0x24);

    // Query scene nodes in frustum region
    int nodeList[10];
    int nodeCount = scene_query_nodes_in_frustum(*(int*)(slot + 0xF0), (int)endA, (int)nodeList, 10);

    // Iterate entities in matching nodes
    for (int n = 0; n < nodeCount; n++) {
        int nodeIdx = nodeList[n];
        int entityCount = ((int*)&DAT_805401dc)[nodeIdx];
        int* entityList = (int*)(((int*)&DAT_8053fb2c)[nodeIdx]);

        for (int e = 0; e < entityCount; e++) {
            int entity = entityList[e];
            if (entity == 0) continue;
            if ((*(uint*)(entity + 0x28) & 0x80000000) != 0) continue; // Dead
            if (entity == *(int*)(slot + 0x10C)) continue; // Self
            if (*(int*)(entity + 0x20) == 8 && *(int*)(entity + 0x7C) != 0) {
                if (player_ai_is_valid_weapon_type(*(short*)(*(int*)(entity + 0x7C) + 0x4A))) continue;
            }

            // Compute XZ distance to entity center
            float ex = *(float*)(entity + 0x64) + *(float*)(entity + 0x1A8);
            float ez = *(float*)(entity + 0x6C) + *(float*)(entity + 0x1B0);
            float dx = ex - *(float*)(slot + 0x1C);
            float dz = ez - *(float*)(slot + 0x24);
            float distSq = dx * dx + dz * dz;

            // Check if within annular band
            if (distSq >= innerRadiusSq && distSq <= outerRadiusSq) {
                // Compute direction to entity
                float dirX = ex - *(float*)(entity + 0x64);
                float dirY = (*(float*)(entity + 0x68) + *(float*)(entity + 0x1AC)) - *(float*)(entity + 0x68);
                float dirZ = ez - *(float*)(entity + 0x6C);

                // Check facing (dot with normal)
                float dot = dirX * *(float*)(slot + 0x180) +
                            dirY * *(float*)(slot + 0x184) +
                            dirZ * *(float*)(slot + 0x188);
                if (fabsf(dot) != 0.0f) {
                    // Dispatch damage
                    switch_slot_reset((int)local_b0);
                    local_b0[0] = entity;
                    damage_dispatch_to_entity(FLOAT_80614acc, local_b0);
                }
            }
        }
    }
    FUN_80382720();
}

// ============================================================================
// 0x800caa90 | player_ai_projectile_sweep_tick
// Per-frame tick for sweeping projectile.
// Phase 1: fade-out timer. Phase 2: moves 33 particles along orbiting paths.
// ============================================================================
void player_ai_projectile_sweep_tick(void) {
    int slot = FUN_803826e0();

    if (*(float*)(slot + 0xF8) >= 0.0f) {
        // Phase 1: fade-out (pre-activation)
        *(float*)(slot + 0x98) = 0.0f;
        *(float*)(slot + 0xF8) -= (1.0f / (float)(DAT_806109c0)) * FLOAT_806108ec;
    } else {
        // Phase 2: active - move particles
        int* block = *(int**)(slot + 0x208);
        float dt = FLOAT_806108ec * (1.0f / (float)(DAT_806109c0));

        for (int i = 0; i < 0x21; i++) {
            float* entry = (float*)(block + i * 4);

            // Compute orbital velocity from angle
            uint angleIdx = (uint)(FLOAT_80614c2c * entry[3] + FLOAT_80614a78);
            float sinVal = FLOAT_80614b9c * ((float*)&DAT_80583228)[angleIdx & 0x7FF];
            float cosVal = FLOAT_80614b9c * *(float*)((int)&DAT_80583228 + ((angleIdx + 0x200) * 4 & 0x1FFC));

            // Velocity = tangent*cos + binormal*sin (orbit in local plane)
            float velX = *(float*)(slot + 0x1D0) * sinVal + *(float*)(slot + 0x1C4) * cosVal;
            float velY = *(float*)(slot + 0x1D4) * sinVal + *(float*)(slot + 0x1C8) * cosVal;
            float velZ = *(float*)(slot + 0x1D8) * sinVal + *(float*)(slot + 0x1CC) * cosVal;

            // Add random angular perturbation
            uint rng = FUN_8013104c();
            entry[3] += FLOAT_80614c28 * dt * ((float)(rng) / FLOAT_80614bf4 - FLOAT_80614a78);

            // Integrate position
            entry[0] += velX * dt;
            entry[1] += velY * dt;
            entry[2] += velZ * dt;

            // Per-particle effects
            FUN_80102e98();
            FUN_80102f74(1.0f);
        }

        // Call damage check
        player_ai_projectile_sweep_damage_check();

        // Deactivate when fully expanded
        if (*(float*)(slot + 0x98) > FLOAT_80614a84) {
            player_ai_slot_deactivate();
        }
    }
    FUN_8038272c();
}

// ============================================================================
// 0x800cae04 | player_ai_projectile_sweep_render
// Renders expanding shockwave quads (4 textured quads using local basis).
// Texture 0x3B9. Expands outward from center.
// ============================================================================
void player_ai_projectile_sweep_render(int pSlot) {
    if (*(float*)(pSlot + 0xF8) >= 0.0f) return; // Not yet active

    int texId = sprite_get_texture_id(0x3B9);
    FUN_8016e77c(texId);
    FUN_8016f00c(2);
    FUN_8016e8ac(2);

    float outerDist = FLOAT_80614b9c * *(float*)(pSlot + 0x98);
    float innerDist = FLOAT_80614b9c * -*(float*)(pSlot + 0x98);

    // Compute 4 corner positions using tangent and binormal
    float* tangent = (float*)(pSlot + 0x1C4);
    float* binormal = (float*)(pSlot + 0x1D0);
    float* origin = (float*)(pSlot + 0x1C);

    // Quad 1: inner corners
    float cornerA[3] = {
        tangent[0] * innerDist + origin[0],
        tangent[1] * innerDist + origin[1],
        tangent[2] * innerDist + origin[2]
    };
    float cornerB[3] = {
        binormal[0] * outerDist + cornerA[0],
        binormal[1] * outerDist + cornerA[1],
        binormal[2] * outerDist + cornerA[2]
    };
    float cornerC[3] = {
        binormal[0] * outerDist + origin[0],
        binormal[1] * outerDist + origin[1],
        binormal[2] * outerDist + origin[2]
    };

    FUN_80171130(1.0f, 0.0f, 0.0f, 1.0f, texId, cornerB, cornerA, cornerC, (int*)origin);

    // Repeat with shifts along tangent and binormal for full cross pattern
    float shift = FLOAT_80614a9c * FLOAT_80614b9c * *(float*)(pSlot + 0x98);
    // ... 3 more quads shifted along tangent/binormal axes ...

    // (4 total FUN_80171130 calls form an expanding cross/diamond pattern)
}

// ============================================================================
// 0x800cb114 | player_ai_projectile_spawn_emitter_a
// Spawns particle emitter type A at projectile position/direction.
// ============================================================================
void player_ai_projectile_spawn_emitter_a(int pSlot) {
    particle_emitter_spawn(pSlot + 0xE8, &DAT_805688d0, 0,
                           (float*)(pSlot + 0x28), (float*)(pSlot + 0x70));
}

// ============================================================================
// 0x800cb150 | player_ai_projectile_spawn_emitter_b
// Spawns particle emitter type B at projectile position/direction.
// ============================================================================
void player_ai_projectile_spawn_emitter_b(int pSlot) {
    particle_emitter_spawn(pSlot + 0xE8, &DAT_80568a20, 0,
                           (float*)(pSlot + 0x28), (float*)(pSlot + 0x70));
}

// ============================================================================
// 0x800cb18c | player_ai_projectile_plasma_tick
// Plasma/energy projectile per-frame tick.
// Moves by direction*speed*dt, raycasts, checks player proximity sphere,
// dispatches damage type 0x24 on hit with splash params.
// ============================================================================
void player_ai_projectile_plasma_tick(void) {
    int* slot = (int*)FUN_803826f0();

    // Compute movement delta = direction * speed * dt
    float dt = FLOAT_806108ec * (float)slot[0x22] * (1.0f / (float)(DAT_806109c0));
    float moveX = (float)slot[0x19] * dt;
    float moveY = (float)slot[0x1A] * dt;
    float moveZ = (float)slot[0x1B] * dt;

    int damageInfo[2] = { 0, 0 };
    float hitPos[3], hitNormal[3];
    int hitEntity = player_ai_fire_raycast(slot, &moveX, hitPos, hitNormal, damageInfo);

    if (damageInfo[0] > 0) {
        player_ai_on_damage_received(slot, (int)damageInfo);
    }

    if (hitEntity == 0) {
        // Check player proximity sphere
        for (int p = 0; p < DAT_80612610; p++) {
            int playerBase = DAT_80611d78 + p * 0x141C;
            float dx = (float)slot[7] - *(float*)(playerBase + 0xB4);
            float dy = (float)slot[8] - *(float*)(playerBase + 0xB8);
            float dz = (float)slot[9] - *(float*)(playerBase + 0xBC);
            float radius = FLOAT_80614a78 + *(float*)(playerBase + 0xA0);
            if (dx*dx + dy*dy + dz*dz < radius * radius) {
                hitEntity = *(int*)(playerBase + 0x14);
                // Set hit position to midpoint
                hitPos[0] = FLOAT_80614a78 * ((float)slot[7] + *(float*)(playerBase + 0xB4));
                hitPos[1] = FLOAT_80614a78 * ((float)slot[8] + *(float*)(playerBase + 0xB8));
                hitPos[2] = FLOAT_80614a78 * ((float)slot[9] + *(float*)(playerBase + 0xBC));
                break;
            }
        }
    } else if (slot[0x42] == hitEntity && (float)slot[0x26] < FLOAT_80614aa0) {
        hitEntity = 0; // Ignore self-hit in early frames
    }

    // Check lifetime
    if ((float)slot[0x26] <= FLOAT_80614ad8) {
        if (hitEntity == 0) {
            // No hit: advance position + secondary trail positions
            slot[7]  = (int)((float)slot[7] + moveX);
            slot[8]  = (int)((float)slot[8] + moveY);
            slot[9]  = (int)((float)slot[9] + moveZ);
            slot[10] = (int)((float)slot[0x1C] * dt + (float)slot[10]);
            slot[11] = (int)((float)slot[0x1D] * dt + (float)slot[11]);
            slot[12] = (int)((float)slot[0x1E] * dt + (float)slot[12]);
            slot[13] = (int)((float)slot[0x1F] * dt + (float)slot[13]);
            slot[14] = (int)((float)slot[0x20] * dt + (float)slot[14]);
            slot[15] = (int)((float)slot[0x21] * dt + (float)slot[15]);
            slot[0x3D] = (int)((float)slot[0x3D] + dt * dt);

            // Update attached particle position
            int handle = slot[0x3A];
            particle_cached_set_position(&handle, slot + 10);
        } else {
            // Hit: dispatch damage
            int entityType = *(int*)(hitEntity + 0x20);
            slot[0x3D] = (int)((float)slot[0x3D] + /* dist to hit */);

            player_ai_impact_dispatch(slot, *slot, hitEntity, hitPos, hitNormal, hitNormal, slot[0x42]);
            if (entityType == 8) {
                player_ai_voice_pitch_update(slot);
            }

            switch_slot_reset((int)local_190);
            switch_slot_load_from_def((int)local_190, slot);
            local_190[0] = hitEntity;
            // Type 0x24, splash params
            damage_resolve_and_dispatch(FLOAT_80614abc, local_190);

            player_ai_sound_stop_and_clear((int)slot, 0);
            player_ai_slot_deactivate();
        }
    } else {
        // Expired: deactivate
        player_ai_sound_stop_and_clear((int)slot, 0);
        player_ai_slot_deactivate();
    }
    FUN_8038273c();
}

// ============================================================================
// 0x800cb524 | player_ai_projectile_ricochet
// Projectile ricochet/bounce handler.
// Tests surface bounce eligibility, attempts primary bounce ray,
// falls back to random scatter direction on failure.
// ============================================================================
void player_ai_projectile_ricochet(int param1, int param2, float* hitPos, float* hitNormal,
                                    int surfaceFlags, int param6) {
    float* origin = (float*)FUN_803826ec();

    FUN_80358fc4(0, origin, hitPos);

    // Check if surface supports bounce (flag 0x8000000)
    if ((*(uint*)(&DAT_804045bc + surfaceFlags * 0x40) & 0x8000000) == 0 ||
        DAT_80611db8 < DAT_80611928 - 8) {

        DAT_80611db8 = DAT_80611928;

        // Compute bounce direction = normal * FLOAT_80614b30
        float bounceDir[3] = {
            FLOAT_80614b30 * hitNormal[0],
            FLOAT_80614b30 * hitNormal[1],
            FLOAT_80614b30 * hitNormal[2]
        };

        // Attempt primary bounce raycast
        int bounceResult = FUN_803590f8(origin, bounceDir, param6, 0);
        if (bounceResult == 0) {
            // Bounce failed: generate random scatter
            float rngA = FUN_8013104c_norm() - FLOAT_80614a78;
            float scatterX = FLOAT_80614a9c * (FLOAT_80614ae4 * rngA +
                             FLOAT_80614b54 * hitNormal[0] * rngA);

            float scatterY = FLOAT_80614be0; // Upward bias

            float rngB = FUN_8013104c_norm() - FLOAT_80614a78;
            float scatterZ = FLOAT_80614a9c * (FLOAT_80614ae4 * rngB +
                             FLOAT_80614b54 * hitNormal[2] * rngB);

            // Scale by FLOAT_80614ad8
            scatterX *= FLOAT_80614ad8;
            scatterY *= FLOAT_80614ad8;
            scatterZ *= FLOAT_80614ad8;

            FUN_803590f8(origin, &scatterX, param6, 2);
        }
    }
    FUN_80382738();
}
