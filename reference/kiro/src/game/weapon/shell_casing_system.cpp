// Address: 0x800cb744 - 0x800cc340 | Batch 277 | Date: 2026-08-25
// Shell casing system: physics-driven spent bullet casings ejected from weapons.
// Pool-based with 2 player pools x 10 ring buffer slots each (0x5C bytes per entry).
// Handles spawn, gravity, bounce, damping, rotation, and lifetime.

#include "common.h"
#include "entity.h"
#include "particle.h"

// Pool layout:
// - 2 pools (one per local player), indexed by param_1 (0 or 1)
// - Each pool: base at DAT_80569480 + pool_idx * 0x3A0
// - Ring buffer: head at [+0x00], count at [+0x04]
// - Entries at DAT_805690e8 + pool_idx * 0x3A0 + entry_idx * 0x5C
// - Per entry: [+0x40]=entity_ptr, [+0x44..0x4C]=angular_vel, [+0x50]=lifetime, [+0x54]=floor_y, [+0x58]=material

// ============================================================================
// 0x800cb744 | shell_casing_preload_models
// Preloads 4 shell casing model resources from table DAT_803cc748.
// ============================================================================
void shell_casing_preload_models(void) {
    FUN_803826f0();
    for (int i = 0; i < 4; i++) {
        int modelId = *(int*)((int)&DAT_803cc748 + i * 4);
        entity_preload_resources(modelId);
    }
    FUN_8038273c();
}

// ============================================================================
// 0x800cb798 | shell_casing_pool_clear
// Clears shell casing pool memory (memset 0x740 bytes to zero).
// ============================================================================
void shell_casing_pool_clear(void) {
    memset_wrapper((int)&DAT_80569480, 0, 0x740);
}

// ============================================================================
// 0x800cb7c8 | shell_casing_system_reset
// System reset wrapper: calls shell_casing_pool_clear.
// ============================================================================
void shell_casing_system_reset(void) {
    shell_casing_pool_clear();
}

// ============================================================================
// 0x800cb7e8 | shell_casing_destroy_all
// Destroys all entities in both shell casing pools (2 pools x 10 slots).
// ============================================================================
void shell_casing_destroy_all(void) {
    FUN_803826dc();
    for (int pool = 0; pool < 2; pool++) {
        int poolOffset = pool * 0x3A0;
        for (int slot = 0; slot < 10; slot++) {
            int entryOffset = slot * 0x5C;
            int* entityPtr = *(int**)(&DAT_805690e8 + entryOffset + 0x40 + poolOffset);
            if (entityPtr != NULL) {
                entity_destroy_with_event(entityPtr);
                *(int*)(&DAT_805690e8 + entryOffset + 0x40 + poolOffset) = 0;
            }
        }
    }
    FUN_80382728();
}

// ============================================================================
// 0x800cb86c | shell_casing_pool_alloc
// Allocates a slot in the shell casing ring buffer.
// Evicts oldest entry if pool is full (max 10 active).
// Returns pointer to 0x5C-byte slot data, or NULL if entity pool full.
// ============================================================================
void* shell_casing_pool_alloc(int poolIdx) {
    int poolOffset = poolIdx * 0x3A0;

    if (!entity_pool_validate_index(1)) {
        return NULL;
    }

    int* headPtr = (int*)((int)&DAT_80569480 + poolOffset);
    int* countPtr = (int*)((int)&DAT_80569484 + poolOffset);
    int head = *headPtr;
    int count = *countPtr;

    // Evict oldest if full (max 10)
    if (count > 9) {
        (*countPtr)--;
        int* oldEntity = *(int**)(&DAT_805690e8 + head * 0x5C + poolOffset + 0x40);
        if (oldEntity != NULL) {
            entity_destroy_with_event(oldEntity);
            *(int*)(&DAT_805690e8 + head * 0x5C + poolOffset + 0x40) = 0;
        }
        (*headPtr)++;
        if (*headPtr > 9) *headPtr = 0;
    }

    // Compute write index (head + count, wrapped)
    int writeIdx = *headPtr + *countPtr;
    if (writeIdx > 9) writeIdx -= 10;
    (*countPtr)++;

    return (void*)(&DAT_805690e8 + writeIdx * 0x5C + poolOffset);
}

// ============================================================================
// 0x800cb948 | shell_casing_spawn
// MASSIVE: spawns a shell casing entity with physics.
// Computes initial rotation from weapon bone, sets velocity from weapon
// direction + player velocity, configures per-player visibility mask.
// ============================================================================
void shell_casing_spawn(int playerIdx, int weaponType, int weaponEntity,
                        int attackerEntity, int weaponData, int* playerSlot) {
    FUN_803826dc();

    float spawnDir[3] = { DAT_803cc758, DAT_803cc75c, DAT_803cc760 };

    // Allocate from opposite pool (casings visible to other player)
    int otherPool = (playerIdx & 1) ^ 1;
    float* slotData = (float*)shell_casing_pool_alloc(otherPool);
    if (slotData == NULL) return;

    if ((playerIdx & 1) == 0) {
        // First-person casing: spawn at weapon bone position
        int modelId = ((int*)&DAT_803cc748)[weaponType];
        int* entity = entity_spawn_by_type(modelId, weaponEntity + 0x10, spawnDir, 0, 0);
        slotData[0x10] = (float)(int)entity;
        *(char*)((int)entity + 0x32) = 1;
        *(int*)((int)entity + 0x28) = 0;
        *(int*)((int)entity + 0x34) = *(int*)(*(int*)(DAT_80611d74 + 0x14) + 0x34);
        *(float**)((int)entity + 0x7C) = slotData;
        entity_register_for_update((int)entity, 2, -0x7FF34124);

        // Set initial rotation from weapon bone matrix (scaled to casing size)
        int boneEntity = *playerSlot;
        FUN_8012f154((FLOAT_80614d48 * *(float*)(boneEntity + 0xC0)) / FLOAT_80614d4c,
                     (FLOAT_80614d48 * (FLOAT_80614d4c + *(float*)(boneEntity + 0x70))) / FLOAT_80614d4c,
                     (FLOAT_80614d48 * *(float*)(boneEntity + 0xC8)) / FLOAT_80614d4c, slotData);
        slotData[0xC] = FLOAT_80614d70; // Scale
        slotData[0xD] = FLOAT_80614d70;
        slotData[0xE] = FLOAT_80614d70;

        // Velocity from weapon bone direction
        float boneScale = *(float*)(weaponData + 0x5C);
        float velX = boneScale * (*(float*)(weaponEntity + 0x20) - *(float*)(weaponEntity + 0x10));
        float velY = boneScale * (*(float*)(weaponEntity + 0x24) - *(float*)(weaponEntity + 0x14));
        float velZ = boneScale * (*(float*)(weaponEntity + 0x28) - *(float*)(weaponEntity + 0x18));
    } else {
        // Third-person casing: spawn at attacker bone position
        int modelId = ((int*)&DAT_803cc748)[weaponType];
        int* entity = entity_spawn_by_type(modelId, attackerEntity + 0x10, spawnDir, 0, 0);
        slotData[0x10] = (float)(int)entity;
        // ... similar setup with camera-space transform ...
        // Velocity from attacker position delta with random scatter
    }

    // Set entity velocity
    *(float*)((int)slotData[0x10] + 0xB4) = velX;
    *(float*)((int)slotData[0x10] + 0xB8) = velY;
    *(float*)((int)slotData[0x10] + 0xBC) = velZ;

    // Add player velocity contribution
    if (DAT_806108e4 > 0) {
        int boneEnt = *playerSlot;
        float pvX = *(float*)(boneEnt + 0xF8) / FLOAT_806108ec;
        float pvY = *(float*)(boneEnt + 0xFC) / FLOAT_806108ec;
        float pvZ = *(float*)(boneEnt + 0x100) / FLOAT_806108ec;
        *(float*)((int)slotData[0x10] + 0xB4) += pvX;
        *(float*)((int)slotData[0x10] + 0xB8) += pvY;
        *(float*)((int)slotData[0x10] + 0xBC) += pvZ;
    }

    // Integrate first frame
    int entity = (int)slotData[0x10];
    *(float*)(entity + 0x64) += *(float*)(entity + 0xB4) * FLOAT_806108ec;
    *(float*)(entity + 0x68) += *(float*)(entity + 0xB8) * FLOAT_806108ec;
    *(float*)(entity + 0x6C) += *(float*)(entity + 0xBC) * FLOAT_806108ec;

    // Set angular velocity (normalized XZ of linear velocity * scale)
    slotData[0x11] = FLOAT_80614d74 * *(float*)(entity + 0xB4);
    slotData[0x12] = FLOAT_80614d70; // No Y angular
    slotData[0x13] = FLOAT_80614d74 * *(float*)(entity + 0xBC);

    // Floor Y for bounce
    slotData[0x15] = *(float*)(*playerSlot + 0x68);
    slotData[0x14] = 0.0f; // Lifetime counter

    // Per-player visibility mask
    // ... (bit mask based on player index and team) ...

    FUN_80382728();
}

// ============================================================================
// 0x800cbedc | shell_casing_update_bone_transform
// Updates shell casing entity rotation/scale from weapon bone matrices.
// Applied per-frame to casings still attached to weapons (pre-eject state).
// ============================================================================
void shell_casing_update_bone_transform(int entity) {
    float* boneMatrix = *(float**)(entity + 0x7C);
    if (boneMatrix == NULL) return;
    if (*(int*)(*(int*)(entity + 4) + 0x10) == 0) return;

    // Build rotation matrix from scaled euler angles
    float rotX = (FLOAT_80614d48 * *(float*)(entity + 0xC0)) / FLOAT_80614d4c;
    float rotY = FLOAT_80614d70;
    float rotZ = (FLOAT_80614d48 * *(float*)(entity + 0xC8)) / FLOAT_80614d4c;
    float matrix[16];
    FUN_8012f154(rotX, rotY, rotZ, matrix);

    // Apply bone matrix * local rotation
    float* renderMatrix = *(float**)(*(int*)(entity + 4) + 0x10);
    FUN_80130bd4(boneMatrix, matrix, renderMatrix);

    // Set uniform scale
    renderMatrix[0x30/4] = FLOAT_80614d70;
    renderMatrix[0x34/4] = FLOAT_80614d70;
    renderMatrix[0x38/4] = FLOAT_80614d70;

    // Mark dirty
    *(uint*)(entity + 0x18) |= 2;
    *(uint*)(entity + 0x28) |= 0x200000;
}

// ============================================================================
// 0x800cbfa8 | shell_casing_tick_all
// MASSIVE: per-frame tick for all shell casings in both pools.
// Gravity, position integration, floor bounce, velocity damping,
// angular velocity for rotation, material assignment, lifetime destroy.
// ============================================================================
void shell_casing_tick_all(void) {
    FUN_803826dc();
    float dt = FLOAT_806108ec;
    float gravityAccel = FLOAT_80614d84;
    float frameScale = 1.0f / (float)(DAT_806109c0);

    for (int pool = 0; pool < 2; pool++) {
        int poolOffset = pool * 0x3A0;
        int head = *(int*)((int)&DAT_80569480 + poolOffset);
        int count = *(int*)((int)&DAT_80569484 + poolOffset);

        for (int i = 0; i < count; i++) {
            int entryOffset = head * 0x5C + poolOffset;
            head++;
            if (head > 9) head = 0;

            int entity = *(int*)(&DAT_805690e8 + entryOffset + 0x40);
            int lifetime = *(int*)(&DAT_805690e8 + entryOffset + 0x50);

            // Check if entity is still alive
            if (*(int*)(entity + 0x34) == 0) {
                *(uint*)(entity + 0x28) &= ~1u; // Clear visible flag
            }

            if (lifetime < 100) {
                // Active: update physics
                // Set material from slot data
                *(uint*)(entity + 0x10) = (*(uint*)(entity + 0x10) & 0xFFFF0000) |
                                          *(uint*)(&DAT_805690e8 + entryOffset + 0x58);

                if ((*(uint*)(entity + 0x28) & 1) == 0) {
                    // First frame: set material, mark visible
                    render_instance_set_material(FLOAT_80614d70, *(int**)(entity + 4), 1);
                    *(uint*)(entity + 0x28) |= 1;
                } else {
                    // Position integration
                    *(float*)(entity + 0x64) += *(float*)(entity + 0xB4) * dt;
                    *(float*)(entity + 0x68) += *(float*)(entity + 0xB8) * dt;
                    *(float*)(entity + 0x6C) += *(float*)(entity + 0xBC) * dt;

                    // Angular velocity -> rotation
                    float angVelX = *(float*)(&DAT_805690e8 + entryOffset + 0x44);
                    float angVelZ = *(float*)(&DAT_805690e8 + entryOffset + 0x4C);
                    *(float*)(entity + 0xC0) += (FLOAT_80614d80 * frameScale) * angVelX * dt;
                    *(float*)(entity + 0xC8) += (FLOAT_80614d80 * frameScale) * angVelZ * dt;

                    // Gravity
                    *(float*)(entity + 0xB8) -= (gravityAccel * frameScale * frameScale) * dt;

                    // Velocity damping (per sub-frame)
                    float damping = FLOAT_80614d88;
                    for (int f = 0; f < DAT_806108e4; f++) {
                        *(float*)(entity + 0xB4) *= damping;
                        *(float*)(entity + 0xBC) *= damping;
                    }

                    // Floor bounce
                    float floorY = *(float*)(&DAT_805690e8 + entryOffset + 0x54);
                    if (*(float*)(entity + 0x68) <= floorY) {
                        *(float*)(entity + 0x68) = floorY;

                        // Bounce: reverse Y velocity with restitution
                        *(float*)(entity + 0xB8) *= FLOAT_80614d8c;
                        *(float*)(entity + 0xB4) *= FLOAT_80614d90;
                        *(float*)(entity + 0xBC) *= FLOAT_80614d90;

                        // Check if velocity is below rest threshold
                        if (*(float*)(entity + 0xB8) >= FLOAT_80614d94) {
                            // Update angular velocity from bounce
                            *(float*)(&DAT_805690e8 + entryOffset + 0x44) =
                                FLOAT_80614d74 * *(float*)(entity + 0xB4);
                            *(float*)(&DAT_805690e8 + entryOffset + 0x4C) =
                                FLOAT_80614d74 * *(float*)(entity + 0xBC);
                        } else {
                            // At rest: zero everything
                            *(float*)(entity + 0xB8) = 0.0f;
                            *(float*)(entity + 0xB4) = 0.0f;
                            *(float*)(entity + 0xBC) = 0.0f;
                            *(float*)(&DAT_805690e8 + entryOffset + 0x44) = 0.0f;
                            *(float*)(&DAT_805690e8 + entryOffset + 0x48) = 0.0f;
                            *(float*)(&DAT_805690e8 + entryOffset + 0x4C) = 0.0f;
                        }
                    }
                }

                // Advance lifetime
                *(int*)(&DAT_805690e8 + entryOffset + 0x50) += DAT_806108e4;
            } else {
                // Expired: destroy entity, free slot
                *(int*)((int)&DAT_80569484 + poolOffset) -= 1;
                int* oldEntity = *(int**)(&DAT_805690e8 +
                    *(int*)((int)&DAT_80569480 + poolOffset) * 0x5C + 0x40 + poolOffset);
                if (oldEntity != NULL) {
                    entity_destroy_with_event(oldEntity);
                    *(int*)(&DAT_805690e8 +
                        *(int*)((int)&DAT_80569480 + poolOffset) * 0x5C + 0x40 + poolOffset) = 0;
                }
                *(int*)((int)&DAT_80569480 + poolOffset) += 1;
                if (*(int*)((int)&DAT_80569480 + poolOffset) > 9) {
                    *(int*)((int)&DAT_80569480 + poolOffset) = 0;
                }
            }
        }
    }
    FUN_80382728();
}

// ============================================================================
// 0x800cc304 | shell_casing_stub
// Empty no-op stub.
// ============================================================================
void shell_casing_stub(void) {
    return;
}

// ============================================================================
// 0x800cc308 | shell_casing_system_alloc
// Allocates shell casing system pool memory.
// Sets max count = 200, allocates 0x5140 bytes, calls reset.
// ============================================================================
void shell_casing_system_alloc(void) {
    DAT_80611dd0 = 200;
    DAT_80611dd4 = mem_alloc(0x5140, 1);
    shell_casing_system_reset(); // FUN_800cc340
}
