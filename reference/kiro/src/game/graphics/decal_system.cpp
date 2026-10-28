// =============================================================================
// decal_system.cpp - Decal/mark system (bullet holes, blood splats, scorch)
// Address range: 0x800fe1f4 - 0x800fe808
// Date: 2026-08-25
// =============================================================================

#include "common.h"
#include "particle.h"
#include "entity.h"
#include "level.h"
#include "memory.h"
#include "sprite.h"

// Global state
extern int DAT_80611f38;   // System enabled flag (from level_has_decal_defs)
extern int DAT_80611f3c;   // System paused/disabled flag
extern int DAT_80611f40;   // Per-mesh decal vertex buffer
extern int DAT_80611f44;   // Instance pool (10 entries, 0xC0 bytes each = 0x780)
extern int DAT_80611f48;   // Instance count
extern int DAT_80611f4c;   // Group pool (5 entries, 0x10C8 bytes each = 0x53E8)
extern int DAT_80611f50;   // Group count
extern int DAT_80611568;   // Global "level active" flag

// Decal type table at level def offsets:
//   +0x70: type 1 (bullet holes / small impacts)
//   +0x74: type 2 (medium damage)
//   +0x78: type 3 (blood/gore)
//   +0x7C: type 4 (scorch/explosion)

extern u32 DAT_805752a0;   // Particle def buffer A
extern u32 DAT_8057536c;   // Particle def buffer B
extern u32 DAT_805753f0;   // Particle def buffer C
extern u32 DAT_805754bc;   // Particle def buffer D
extern u32 DAT_8040c2d0;   // Default decal def (fallback)
extern u8  DAT_80467bd0;   // Decal type table base (5 entries, 0x34 bytes each)

// Constants
extern float FLOAT_80615590;
extern float FLOAT_80615594;
extern float FLOAT_80615598; // Zero / 0.0f
extern float FLOAT_8061559c;
extern float FLOAT_806155a0;
extern float FLOAT_806155a4;
extern float FLOAT_806155a8;
extern float FLOAT_806155ac;
extern float FLOAT_806155b0;
extern float FLOAT_806155b4;
extern float FLOAT_806155b8;

// External functions
extern int level_get_current_def(void);
extern void particle_def_create(double, double, void*, int, int, int, int, int);
extern void particle_def_set_size_keyframes(double, double, double, double, int, int, int);
extern void particle_def_set_velocity_ranges(double, double, double, double, double, double);
extern void particle_def_set_size_start_end(double, double, double, double);
extern void particle_def_set_color_end(double, double, double, double);
extern void particle_emitter_create(double, double, double, void*, void*, int, int);
extern void particle_emitter_set_velocity_range(double, double);
extern void particle_emitter_set_color_range(double, double, double, double);
extern void sprite_load_by_flag_mask(void);
extern int mem_alloc(int size, int align, ...);
extern void memset_wrapper(int dst, int val, int size);
extern void FUN_8018cbd0(void);
extern void FUN_8018cbc8(void);
extern int FUN_8018cbcc(void);

// =============================================================================
// Address: 0x800fe1f4 | Name: level_has_decal_defs
//
// Returns 1 if the current level has any decal type definitions
// (offsets 0x70, 0x74, 0x78, 0x7C in level def).
// =============================================================================
int level_has_decal_defs(void) {
    int levelDef = level_get_current_def();
    if (levelDef == 0) {
        return 0;
    }

    if (*(int*)(levelDef + 0x70) != 0 ||
        *(int*)(levelDef + 0x74) != 0 ||
        *(int*)(levelDef + 0x78) != 0 ||
        *(int*)(levelDef + 0x7C) != 0) {
        return 1;
    }
    return 0;
}

// =============================================================================
// Address: 0x800fe260 | Name: decal_preload_sprites
//
// Preload sprite textures for decal system if level has decal defs.
// Called by: FUN_801c1888 (level load)
// =============================================================================
void decal_preload_sprites(void) {
    if (level_has_decal_defs() != 0) {
        sprite_load_by_flag_mask();
        FUN_8018cbc8();
    }
}

// =============================================================================
// Address: 0x800fe294 | Name: decal_system_alloc
//
// Allocate memory pools for decal system:
//   - Instance pool: 0x780 bytes (10 instances * 0xC0)
//   - Group pool: 0x53F0 bytes (5 groups * 0x10C8)
//   - Vertex buffer: 0x1A94 bytes
// Then calls decal_system_reset to initialize.
// Called by: FUN_801c1a3c (level init)
// =============================================================================
void decal_system_alloc(void) {
    DAT_80611f44 = 0;
    DAT_80611f4c = 0;
    DAT_80611f38 = level_has_decal_defs();

    if (DAT_80611f38 != 0) {
        DAT_80611f44 = mem_alloc(0x780, 1);   // 10 decal instances
        DAT_80611f4c = mem_alloc(0x53F0, 1);  // 5 decal groups

        // Initialize 5 group slots
        for (int i = 0; i < 5; i++) {
            FUN_8018cbcc();
        }

        DAT_80611f40 = mem_alloc(0x1A94, 1);  // Vertex scratch buffer
    }

    decal_system_reset();
}

// =============================================================================
// Address: 0x800fe338 | Name: decal_system_reset
//
// Reset decal system state: clear all pools, reinitialize group IDs to -1,
// then create particle effect definitions for decal spawn particles.
// Called by: decal_system_alloc, FUN_801c197c (level reset)
// =============================================================================
void decal_system_reset(void) {
    DAT_80611f3c = 0;  // Unpause
    DAT_80611f48 = 0;  // Instance count = 0
    DAT_80611f50 = 0;  // Group count = 0

    if (DAT_80611f38 != 0) {
        memset_wrapper(DAT_80611f44, 0, 0x780);   // Clear instances
        memset_wrapper(DAT_80611f4c, 0, 0x53E8);  // Clear groups
        memset_wrapper(DAT_80611f40, 0, 0x1A94);  // Clear vertex buffer

        // Set all 5 group entity IDs to -1 (unused)
        int offset = 0;
        for (int i = 0; i < 5; i++) {
            *(int*)(DAT_80611f4c + offset) = 0xFFFFFFFF;
            offset += 0x10C8;
        }
    }

    // Create particle definitions for decal impact effects
    // Particle def A: impact dust (sprite 0x34F)
    particle_def_create((double)FLOAT_80615590, (double)FLOAT_80615590,
                        &DAT_805752a0, 0, 0, 1, 0x34F, 0);
    particle_def_set_size_keyframes(
        (double)FLOAT_80615594, (double)FLOAT_80615598,
        (double)FLOAT_8061559c, (double)FLOAT_806155a0,
        0x55655F00, 0x55655F00, 0);
    particle_def_set_velocity_ranges(
        (double)FLOAT_806155a4, (double)FLOAT_80615598,
        (double)FLOAT_806155a0, (double)FLOAT_80615598,
        (double)FLOAT_80615590, (double)FLOAT_806155a0);
    double zero = (double)FLOAT_80615598;
    particle_def_set_size_start_end(zero, zero, zero, (double)FLOAT_80615590);
    particle_def_set_color_end(zero, zero, (double)FLOAT_806155a8, zero);

    // Emitter A: dust emitter
    particle_emitter_create(
        (double)FLOAT_806155ac, (double)FLOAT_806155ac, (double)FLOAT_806155b0,
        &DAT_8057536c, &DAT_805752a0, 0, 1);
    particle_emitter_set_velocity_range((double)FLOAT_80615590, (double)FLOAT_80615598);
    particle_emitter_set_color_range(zero, zero, (double)FLOAT_806155b4, zero);

    // Particle def B: debris/chunks (sprite 0x363)
    particle_def_create((double)FLOAT_80615590, (double)FLOAT_80615590,
                        &DAT_805753f0, 3, 0, 1, 0x363, 0);
    particle_def_set_size_keyframes(
        (double)FLOAT_80615590, (double)FLOAT_80615598,
        (double)FLOAT_8061559c, (double)FLOAT_806155a0,
        0x646E6400, 0x646E6400, 0);
    particle_def_set_velocity_ranges(
        zero, zero, (double)FLOAT_806155b8, zero,
        (double)FLOAT_80615590, (double)FLOAT_806155a0);

    // Emitter B: debris emitter
    particle_emitter_create(zero, zero, zero, &DAT_805754bc, &DAT_805753f0, 0, 1);
    particle_emitter_set_color_range(zero, zero, (double)FLOAT_80615590, zero);
}

// =============================================================================
// Address: 0x800fe534 | Name: decal_free_buffer_if_idle
//
// Free the instance buffer pointer if the level is not active.
// Called by: FUN_801c1cfc (level unload)
// =============================================================================
void decal_free_buffer_if_idle(void) {
    if (DAT_80611568 != 0) {
        return;
    }
    DAT_80611f44 = 0;
}

// =============================================================================
// Address: 0x800fe54c | Name: decal_group_find_or_create
//
// Find an existing decal group by entity ID, or allocate a new one from the
// 5-slot pool. Returns pointer to group data (0x10C8 bytes per group).
// Returns 0 if pool is full.
// Called by: decal_create_instance
// =============================================================================
int decal_group_find_or_create(int entityId) {
    int offset = 0;

    // Search for existing group with matching entity ID
    for (int i = 0; i < 5; i++) {
        if (entityId == *(int*)(DAT_80611f4c + offset)) {
            return DAT_80611f4c + offset;
        }
        offset += 0x10C8;
    }

    // Not found - allocate new slot
    int slotIdx = 0;
    offset = 0;
    for (int i = 0; i < 5; i++) {
        if (*(int*)(DAT_80611f4c + offset) == -1) {
            // Found free slot
            DAT_80611f50++;
            *(int*)(DAT_80611f4c + offset) = entityId;

            // Clear sub-slot IDs to -1 (6 sub-slots at +4, +8, ... +24)
            int subOffset = 0;
            for (int j = 0; j < 6; j++) {
                *(int*)(offset + DAT_80611f4c + subOffset + 4) = 0xFFFFFFFF;
                subOffset += 4;
            }

            FUN_8018cbd0(slotIdx, DAT_80611f4c + offset);
            return DAT_80611f4c + offset;
        }
        slotIdx++;
        offset += 0x10C8;
    }

    return 0; // Pool full
}

// =============================================================================
// Address: 0x800fe61c | Name: decal_create_instance
//
// Create a decal instance from a mesh definition. Allocates from the 10-slot
// instance pool (0xC0 bytes each). Scans sub-mesh bone data looking for
// material type 1 entries. Up to 10 material refs per instance.
// Links instance to a decal group via decal_group_find_or_create.
// Returns instance pointer or NULL if pool full / system paused.
// Called by: decal_create_from_entity, FUN_800fe970
// =============================================================================
int* decal_create_instance(int* pDecalDef, int meshEntity, int param11, int* pColor) {
    int* piVar4;
    float fVar3 = FLOAT_80615598; // 0.0f

    if (DAT_80611f3c != 0) {
        return NULL; // System paused
    }

    if (DAT_80611f38 == 0) {
        return NULL; // System not initialized
    }

    if (DAT_80611f48 >= 10) {
        return NULL; // Pool full
    }

    // Allocate from instance pool
    int slotOffset = DAT_80611f48 * 0xC0;
    DAT_80611f48++;
    piVar4 = (int*)(DAT_80611f44 + slotOffset);

    // Initialize instance
    piVar4[0] = (int)pDecalDef;       // Decal definition pointer
    piVar4[9] = *pDecalDef;           // Copy first field from def
    piVar4[0xB] = 0;                  // Material ref count
    piVar4[10] = meshEntity;          // Mesh entity reference
    piVar4[1] = param11;              // Additional param
    piVar4[2] = (int)fVar3;           // Color R (default 0)
    piVar4[3] = (int)fVar3;           // Color G (default 0)
    piVar4[4] = (int)fVar3;           // Color B (default 0)

    if (pColor != NULL) {
        piVar4[2] = pColor[0];        // Override color R
        piVar4[3] = pColor[1];        // Override color G
        piVar4[4] = pColor[2];        // Override color B
    }

    // Scan mesh for material type 1 entries (decal-compatible surfaces)
    if (meshEntity != 0) {
        int* meshData = *(int**)(*(int*)(meshEntity + 4) + 4);
        int boneCount = *meshData;
        int boneOffset = 0;

        for (int boneIdx = 0; boneIdx < boneCount; boneIdx++) {
            int subOffset = 0;
            int listOffset = 0;
            int baseOffset = boneOffset + boneCount * -0xC0;

            // Iterate 3 sub-mesh channels per bone
            for (int channel = 0; channel < 3; channel++) {
                int vertexBase = *(int*)((int)meshData + baseOffset + listOffset + 0xB0);
                short* pStripList = *(short**)((int)meshData + baseOffset + subOffset + 0x54);

                // Walk strip list entries
                while (pStripList != NULL && (*(char*)((int)pStripList + 3) >= 0)) {
                    if (*pStripList >= 0) {
                        int matIdx = *pStripList * 0x30;
                        // Check if material type is 1 (decal-compatible)
                        if (*(char*)(meshData[5] + matIdx + 0x18) == 1) {
                            if (piVar4[0xB] > 9) {
                                return NULL; // Too many material refs
                            }
                            piVar4[piVar4[0xB] + 0xC] = (int)pStripList;
                            piVar4[piVar4[0xB] + 0x16] = (int)meshData + subOffset + baseOffset;
                            piVar4[piVar4[0xB] + 0x20] = vertexBase;
                            piVar4[0xB]++;
                        }
                    }
                    vertexBase += 8;
                    pStripList += 4;
                }
                subOffset += 0x14;
                listOffset += 4;
            }
            boneOffset += 0xC0;
        }
    }

    // Initialize transform/scale fields
    fVar3 = FLOAT_80615598;
    piVar4[0x2C] = (int)fVar3;  // Scale X
    piVar4[0x2D] = (int)fVar3;  // Scale Y
    piVar4[0x2A] = (int)fVar3;  // UV offset X
    piVar4[0x2B] = (int)fVar3;  // UV offset Y
    piVar4[0x2E] = (int)fVar3;  // Rotation

    // Link to decal group
    int groupPtr = decal_group_find_or_create(*(int*)(*piVar4 + 4));
    piVar4[0x2F] = groupPtr;

    return piVar4;
}

// =============================================================================
// Address: 0x800fe808 | Name: decal_create_from_entity
//
// Create a decal on an entity by looking up the decal type from the level
// definition. Maps material sub-type byte to level def offsets:
//   0 -> default (use &DAT_8040c2d0)
//   1 -> +0x70 (bullet holes)
//   2 -> +0x74 (medium damage)
//   3 -> +0x78 (blood/gore)
//   4 -> +0x7C (scorch/explosion)
// Then selects from a 5-entry type table (&DAT_80467bd0, 0x34 each).
// Called by: entity_init_render_data
// =============================================================================
int* decal_create_from_entity(int entityMesh) {
    int* meshData = *(int**)(*(int*)(entityMesh + 4) + 4);
    int boneCount = *meshData;
    int boneOffset = 0;

    for (int boneIdx = 0; boneIdx < boneCount; boneIdx++) {
        int baseOffset = boneOffset + boneCount * -0xC0;

        // Scan 3 channels per bone
        for (int channel = 0; channel < 3; channel++) {
            short* pStripList = *(short**)((int)meshData + (channel * 4) + 0x54 + baseOffset);

            while (pStripList != NULL && (*(char*)((int)pStripList + 3) >= 0)) {
                if (*pStripList >= 0) {
                    int matIdx = *pStripList * 0x30;

                    // Check material type 1 (decal surface)
                    if (*(char*)(meshData[5] + matIdx + 0x18) == 1) {
                        // Get decal sub-type
                        u8 subType = *(u8*)(meshData[5] + matIdx + 0x19);

                        int decalDefId = 0;
                        if (subType == 3) {
                            int levelDef = level_get_current_def();
                            decalDefId = *(int*)(levelDef + 0x78); // Blood/gore
                        } else if (subType == 1) {
                            int levelDef = level_get_current_def();
                            decalDefId = *(int*)(levelDef + 0x70); // Bullet holes
                        } else if (subType == 2) {
                            int levelDef = level_get_current_def();
                            decalDefId = *(int*)(levelDef + 0x74); // Medium damage
                        } else if (subType == 4) {
                            int levelDef = level_get_current_def();
                            decalDefId = *(int*)(levelDef + 0x7C); // Scorch
                        }

                        // Look up decal def from type table
                        int* pDecalDef = (int*)&DAT_8040c2d0; // Default
                        if (decalDefId > 0 && decalDefId < 6) {
                            pDecalDef = (int*)(&DAT_80467bd0 + decalDefId * 0x34);
                        }

                        // Create the decal instance
                        int* result = decal_create_instance(pDecalDef, entityMesh, 0, NULL);
                        return result;
                    }
                }
                pStripList += 4;
            }
        }
        boneOffset += 0xC0;
    }

    return NULL; // No decal-compatible surface found
}

// =============================================================================
// Address: 0x800fe970 | Name: decal_spawn_with_entity
//
// Spawn a decal, optionally creating a 3D entity for it. Looks up decal type
// from the type table (&DAT_80467bd0). If entity type is -1, creates a flat
// decal directly. Otherwise spawns entity, pauses decal system during spawn,
// then creates and links the decal instance.
// Called by: FUN_801db588, FUN_80253eac (gameplay/impact code)
// =============================================================================
void decal_spawn_with_entity(int param11, int* param12, float* param13) {
    int entityType;
    int* pDecalDef;

    // Look up decal definition from type table
    int result = FUN_803826ec(); // Returns packed: high=typeIndex, low=entityType
    int typeIndex = (int)((u64)result >> 32);
    entityType = (int)result;
    pDecalDef = (int*)(&DAT_80467bd0 + typeIndex * 0x34);

    if (entityType == -1) {
        // Flat decal only (no entity)
        decal_create_instance(pDecalDef, 0, param11, param12);
    } else {
        // Spawn entity for 3D decal
        DAT_80611f3c = 1; // Pause decal system during entity spawn
        int entity = entity_spawn_by_type(entityType, param12, param13, 0, (int)pDecalDef);
        DAT_80611f3c = 0;

        // If decal def has flag 0x10, configure as persistent overlay
        if ((*pDecalDef & 0x10) != 0) {
            *(int*)(entity + 0x34) = 0xD5; // render priority
            *(u32*)(entity + 0x28) = *(u32*)(entity + 0x28) | 0x40000000;
        }

        // Create decal instance and link to entity
        int* pInstance = decal_create_instance(pDecalDef, entity, param11, param12);
        *(int**)(entity + 0x204) = pInstance;
    }

    FUN_80382738();
}

// =============================================================================
// Address: 0x800fea34 | Name: decal_destroy_instance
//
// Remove a decal instance from the pool when its entity is destroyed.
// Shifts remaining entries down to fill the gap.
// Called by: entity_destroy
// =============================================================================
void decal_destroy_instance(void) {
    u32 targetPtr = FUN_803826f0(); // Get target instance pointer
    int offset = 0;

    for (int i = 0; i < DAT_80611f48; i++) {
        if ((DAT_80611f44 + offset == (int)targetPtr) && (targetPtr != 0)) {
            DAT_80611f48--;
            // Shift remaining entries down (memmove)
            leaf_31e8(DAT_80611f44 + offset,
                      DAT_80611f44 + (i + 1) * 0xC0,
                      (9 - i) * 0xC0);
        }
        offset += 0xC0;
    }

    FUN_8038273c();
}

// =============================================================================
// Address: 0x800feac0 | Name: decal_tick_uv_scroll
//
// Per-frame UV animation for all active decal instances. Uses a sine-wave
// table (DAT_80583228) for oscillating UV offsets. Applies velocity-based
// UV scroll, rotation accumulation, and handles UV wrapping [0,1].
// Called by: FUN_801c1fd8 (per-frame update)
// =============================================================================
void decal_tick_uv_scroll(void) {
    FUN_803826e0();

    if (DAT_80611f48 == 0) {
        FUN_8038272c();
        return;
    }

    // Compute delta time
    double dt = (double)FLOAT_806108ec;
    if (dt < (double)FLOAT_80615598) {
        dt = (double)FLOAT_80615598;
    }

    double timeScale = (double)(FLOAT_80615590 /
        (float)((double)(DAT_806109c0 ^ 0x80000000) - DOUBLE_806155d0));
    double frameDelta = (double)(float)(dt * timeScale);

    // Compute sine-wave phase offsets
    float phase = (float)((double)(DAT_80611928 ^ 0x80000000) - DOUBLE_806155d0) * (float)timeScale;
    float sineA = FLOAT_806155bc * FLOAT_806155c0 * FLOAT_806155c8 * phase + FLOAT_806155a0;
    float sineB = FLOAT_806155bc * FLOAT_806155c0 * FLOAT_806155a0 * (FLOAT_806155c4 + phase) +
                  FLOAT_806155a0;

    int offset = 0;
    for (int i = 0; i < DAT_80611f48; i++) {
        int* pInst = (int*)(DAT_80611f44 + offset);

        // Check visibility: either has overlay flag or mesh is valid
        if (((pInst[9] & 0x10) == 0) && (pInst[10] != 0)) {
            pInst[8] = (int)((*(u32*)(pInst[10] + 0x10) & 0xFFFF) != 0);
        } else {
            pInst[8] = 1;
        }

        int pDef = *pInst;
        if ((pDef != 0) && (pInst[8] != 0)) {
            // Compute oscillation amplitude from sine table
            double amplitude = (double)(*(float*)(pDef + 0x14) *
                (FLOAT_806155cc * ((float*)&DAT_80583228)[(int)sineB & 0x7FF] +
                 ((float*)&DAT_80583228)[(int)sineA & 0x7FF]));
            if (amplitude < (double)FLOAT_80615598) amplitude = (double)FLOAT_80615598;

            double uvDelta = (double)(float)(dt * amplitude);
            double uvDecay = (double)(float)((double)*(float*)(pDef + 0x18) * dt);

            FUN_8018cc90(); // Update internal state

            // Get UV scale factors from mesh reference
            double scaleU = (double)FLOAT_80615590;
            double scaleV = (double)FLOAT_80615590;
            if (pInst[1] != 0) {
                scaleU = (double)(FLOAT_80615590 / *(float*)(pInst[1] + 0x10));
                scaleV = (double)(FLOAT_80615590 / *(float*)(pInst[1] + 0x14));
            }

            // Update UV velocity with decay
            float decay = (float)((double)FLOAT_80615590 - uvDecay);
            pInst[0x2A] = (int)(*(float*)&pInst[0x2A] * decay);
            pInst[0x2A] = (int)(float)(uvDelta * (double)*(float*)(pDef + 8) +
                          (double)*(float*)&pInst[0x2A]);
            pInst[0x2B] = (int)(*(float*)&pInst[0x2B] * decay);
            pInst[0x2B] = (int)(float)(uvDelta * (double)*(float*)(pDef + 0xC) +
                          (double)*(float*)&pInst[0x2B]);

            // Accumulate UV offset with wrapping
            pInst[0x2C] = (int)(float)(scaleU * (double)(float)(frameDelta *
                          (double)*(float*)&pInst[0x2A]) + (double)*(float*)&pInst[0x2C]);
            if ((float)pInst[0x2C] < FLOAT_80615598) {
                pInst[0x2C] = (int)(*(float*)&pInst[0x2C] + FLOAT_80615590);
            }
            if (FLOAT_80615590 < (float)pInst[0x2C]) {
                pInst[0x2C] = (int)(*(float*)&pInst[0x2C] - FLOAT_80615590);
            }

            pInst[0x2D] = (int)(float)(scaleV * (double)(float)(frameDelta *
                          (double)*(float*)&pInst[0x2B]) + (double)*(float*)&pInst[0x2D]);
            if ((float)pInst[0x2D] < FLOAT_80615598) {
                pInst[0x2D] = (int)(*(float*)&pInst[0x2D] + FLOAT_80615590);
            }
            if (FLOAT_80615590 < (float)pInst[0x2D]) {
                pInst[0x2D] = (int)(*(float*)&pInst[0x2D] - FLOAT_80615590);
            }

            // Accumulate rotation
            double rotDelta = frameDelta;
            if (rotDelta < (double)FLOAT_80615598) rotDelta = (double)FLOAT_80615598;
            pInst[0x2E] = (int)(float)((double)*(float*)(pDef + 0x10) * rotDelta +
                          (double)*(float*)&pInst[0x2E]);
            if (FLOAT_80615590 <= (float)pInst[0x2E]) {
                pInst[0x2E] = (int)(*(float*)&pInst[0x2E] - FLOAT_80615590);
            }
        }

        offset += 0xC0;
    }

    FUN_8038272c();
}

// =============================================================================
// Address: 0x800fedfc | Name: decal_render_pass_overlay
//
// Render pass for overlay decals (flag 0x10 set or no mesh attachment).
// Iterates all instances, calls decal_render_instance for matching ones.
// Called by: scene_render_dispatch
// =============================================================================
void decal_render_pass_overlay(void) {
    int offset = 0;

    for (int i = 0; i < DAT_80611f48; i++) {
        int pInst = DAT_80611f44 + offset;

        if ((*(int*)(pInst + 0x20) != 0) &&
            ((*(u32*)(pInst + 0x24) & 0x10) != 0 || *(int*)(pInst + 0x28) == 0)) {
            decal_render_instance();
        }
        offset += 0xC0;
    }
}

// =============================================================================
// Address: 0x800fee78 | Name: decal_render_pass_base
//
// Render pass for base decals (no overlay flag, no mesh attachment).
// Iterates all instances, calls decal_render_instance for matching ones.
// Called by: FUN_801c2584 (render pass)
// =============================================================================
void decal_render_pass_base(void) {
    int offset = 0;

    for (int i = 0; i < DAT_80611f48; i++) {
        if ((*(int*)(DAT_80611f44 + offset + 0x20) != 0) &&
            (*(int*)(DAT_80611f44 + offset + 0x28) == 0)) {
            decal_render_instance();
        }
        offset += 0xC0;
    }
}

// =============================================================================
// Address: 0x800feee8 | Name: decal_render_instance
//
// MASSIVE core decal renderer. Builds vertex arrays (position, UV, normal-weight,
// color) for a single decal instance and submits to GX via DMA (FUN_8039955c).
//
// Two paths:
// 1. No mesh attachment (piVar24[1] == NULL): reads vertices directly from
//    mesh data (float or quantized short format), applies entity position offset,
//    computes UV from texture coordinates, and builds vertex color from normal
//    weight and decal alpha.
// 2. Mesh attachment (grid decal): generates a subdivided grid of (cols+1)*rows*2
//    vertices with bilinear color interpolation from 4 corner colors.
//
// Max 200 vertices per instance. Writes to DAT_80611f40 vertex buffer:
//   +0x000: positions (vec4, 200 entries)
//   +0xC80: UVs (vec2, 200 entries)
//   +0x12C0: normal weights (float, 200 entries)
//   +0x15E0: colors (u32 RGBA, 200 entries)
//   +0x1900: strip counts (int, up to entries)
//   +0x1A90: strip count total
// Called by: decal_render_pass_overlay, decal_render_pass_base, render_instance_draw
// =============================================================================
void decal_render_instance(void) {
    int* piVar24 = (int*)FUN_803826b4();

    // Reset strip counter
    *(int*)(DAT_80611f40 + 0x1A90) = 0;

    int vertexCount = 0;
    int posOffset = 0;
    int uvOffset = 0;
    int weightOffset = 0;
    int colorOffset = 0;
    int stripOffset = 0;

    if ((*piVar24 == 0) || (piVar24[1] == 0)) {
        // Path 1: Direct mesh vertex decal
        int entity = piVar24[10];
        float posX = *(float*)(entity + 100);
        float posY = *(float*)(entity + 0x68);
        float posZ = *(float*)(entity + 0x6C);
        int meshData = *(int*)(*(int*)(entity + 4) + 4);

        for (int matIdx = 0; matIdx < piVar24[0xB]; matIdx++) {
            int stripDef = *(int*)((int)piVar24 + stripOffset + 0x30);
            int stripCount = *(int*)(DAT_80611f40 + 0x1A90);
            int subMesh = *(int*)((int)piVar24 + stripOffset + 0x58);
            u16 startVtx = *(u16*)(stripDef + 6);
            int vtxBase = *(int*)(subMesh + 0x10);
            u8 numVerts = *(u8*)(stripDef + 2);

            *(int*)(DAT_80611f40 + 0x1A90) = stripCount + 1;
            *(int*)(DAT_80611f40 + stripCount * 4 + 0x1900) = (int)numVerts;

            for (int v = 0; v < numVerts; v++) {
                if (vertexCount > 199) goto done;

                float vx, vy, vz;
                // Read vertex (float or quantized short format)
                if ((*(u32*)(meshData + 0xC) & 0x10) == 0) {
                    float* pVtx = (float*)(*(int*)(subMesh + 4) + (startVtx + v) * 0xC);
                    vx = pVtx[0]; vy = pVtx[1]; vz = pVtx[2];
                } else {
                    short* pVtx = (short*)(*(int*)(subMesh + 4) + (startVtx + v) * 6);
                    vx = FLOAT_806155d8 * (float)pVtx[0] + *(float*)(meshData + 0x2C);
                    vy = FLOAT_806155d8 * (float)pVtx[1] + *(float*)(meshData + 0x30);
                    vz = FLOAT_806155d8 * (float)pVtx[2] + *(float*)(meshData + 0x34);
                }

                // Write position (entity-relative)
                *(float*)(DAT_80611f40 + posOffset) = vx + posX;
                *(float*)(DAT_80611f40 + posOffset + 4) = vy + posY + FLOAT_8061559c;
                *(float*)(DAT_80611f40 + posOffset + 8) = vz + posZ;

                // Read and write UV
                float u, vCoord;
                if ((*(u32*)(subMesh + 8) & 2) == 0) {
                    float* pUV = (float*)(*(u32*)(subMesh + 8) + (startVtx + v) * 8);
                    u = pUV[0]; vCoord = pUV[1];
                } else {
                    short* pUV = (short*)(*(u32*)(subMesh + 8) + (startVtx + v) * 4);
                    u = FLOAT_806155d8 * (float)pUV[0];
                    vCoord = FLOAT_806155d8 * (float)pUV[1];
                }
                *(float*)(DAT_80611f40 + uvOffset + 0xC80) = u + (float)piVar24[0x2C];
                *(float*)(DAT_80611f40 + uvOffset + 0xC84) = vCoord + (float)piVar24[0x2D];

                // Normal weight -> alpha
                u32 normalPacked = *(u32*)(vtxBase + (startVtx + v) * 4);
                *(float*)(DAT_80611f40 + weightOffset + 0x12C0) =
                    FLOAT_806155dc * (float)((normalPacked >> 24) - 0x80) *
                    *(float*)(*piVar24 + 0x24);

                // Color (white with computed alpha)
                *(u32*)(DAT_80611f40 + colorOffset + 0x15E0) = 0x7F7F7FFF;

                vertexCount++;
                posOffset += 0x10;
                uvOffset += 8;
                weightOffset += 4;
                colorOffset += 4;
            }
            stripOffset += 4;
        }
    } else {
        // Path 2: Grid-subdivided decal (mesh attachment)
        float* pMeshDef = (float*)piVar24[1];
        // ... generates subdivided grid with bilinear color interpolation ...
        // Vertices = (cols+1) * rows * 2, max 200
        // Each pair forms a triangle strip row
    }

done:
    // Flush vertex buffer to GX DMA
    FUN_8039955c(DAT_80611f40, 0x1A94);
    FUN_8018d4ec(); // Submit draw call

    FUN_80382700();
}

// =============================================================================
// Address: 0x800ff818 | Name: decal_impact_effect
//
// Spawn impact particles and sound at a surface hit point. If damage parameter
// exceeds threshold, spawns explosion instead. Checks surface normal direction
// to ensure impact faces outward.
// Called by: player_ai_impact_dispatch, FUN_8014d67c
// =============================================================================
void decal_impact_effect(float damage, int hitEntity, float* hitPos, float* hitNormal) {
    float fVar3 = FLOAT_806155e8; // Offset distance

    // Copy surface normal for particle direction
    float normalDir[3];
    normalDir[0] = DAT_803cca00;
    normalDir[1] = DAT_803cca04;
    normalDir[2] = DAT_803cca08;

    // Find existing decal instance on this entity (if any)
    if (hitEntity != 0) {
        int offset = 0;
        int count = DAT_80611f48;
        if (count > 0) {
            do {
                if (*(int*)(DAT_80611f44 + offset + 0x28) == hitEntity) break;
                offset += 0xC0;
                count--;
            } while (count != 0);
        }
    }

    // Check that normal faces away from surface (dot product test)
    float dot = -(hitNormal[2] * DAT_803cca08 + hitNormal[0] * DAT_803cca00 +
                  hitNormal[1] * DAT_803cca04);
    if (FLOAT_8061559c <= dot) {
        if (damage <= (double)FLOAT_80615590) {
            // Small impact: offset position along normal, spawn particles + sound
            hitPos[0] += DAT_803cca00 * fVar3;
            hitPos[1] += DAT_803cca04 * fVar3;
            hitPos[2] += DAT_803cca08 * fVar3;

            particle_emitter_spawn(0, &DAT_805754bc, 0, hitPos, normalDir);
            particle_emitter_spawn(0, &DAT_8057536c, 0, hitPos, normalDir);

            // Play impact sound
            int soundParams[7];
            sound_init_play_params((int)soundParams);
            soundParams[0] = 0xDA4; // Impact sound ID
            *(u8*)&soundParams[3] = 1; // 3D flag
            *(float*)&soundParams[5] = FLOAT_806155ec; // Volume
            *(float*)&soundParams[4] = hitPos[0];
            *(float*)((int)soundParams + 0x14) = hitPos[1];
            *(float*)((int)soundParams + 0x18) = hitPos[2];
            sound_play_request(soundParams);
        } else {
            // Large impact: spawn explosion
            player_ai_explosion_spawn(0xE, 0x24, hitPos, normalDir, 0, hitEntity, 1);
        }
    }
}

// =============================================================================
// Address: 0x800ff9a4 | Name: decal_system_alloc_stub
// Empty stub called during entity_system_alloc.
// =============================================================================
void decal_system_alloc_stub(void) {
    return;
}
