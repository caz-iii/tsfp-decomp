// =============================================================================
// collision_volume.cpp - Entity collision volume system
// Address range: 0x800ffb88 - 0x801008b4
// Date: 2026-08-25
// =============================================================================

#include "common.h"
#include "entity.h"
#include "collision.h"
#include "math_utils.h"

// External functions
extern void entity_build_local_matrix(float* mtx, int entity, int a, int b);
extern void FUN_8012f9b8(float* mtx, float* vec);
extern double FUN_80131538(double val); // sqrt
extern int FUN_800150d8(int, float*, int, int, float*, float*, float*, float*, int);
extern int FUN_80018e10(int, int*, int, float*, int, int, int);
extern int FUN_8001a434(int*, float*, float*, int, int, int, float*);
extern int FUN_80013dfc(double, float*, float*, int, int, float*);
extern int FUN_80146610(void);
extern void FUN_801467ec(int*, int);
extern void FUN_80146660(double, double, void*, int*, float*, int, int, int, int);
extern void FUN_80146650(int*, int);
extern int scene_portal_get_ptr(int portalIdx);
extern void collision_result_store(int addr);
extern void memset_wrapper(int dst, int val, int size);

extern float FLOAT_806155f0; // 1.0f
extern float FLOAT_806155f4; // Scale factor
extern float FLOAT_8061560c; // Normal threshold low
extern float FLOAT_80615610; // Normal threshold high

extern int DAT_80575ce0; // Collision result mesh ref

// =============================================================================
// Address: 0x800ffb88 | Name: collision_volume_init_all
//
// Initialize all collision volume slots on an entity. For each volume:
//   - Sets first float to 1.0 (scale/size)
//   - Clears 4 padding dwords at +0x10..+0x1C
//   - Sets bytes at +0x0E and +0x0F to 0xFF (unassigned slot markers)
// Called by: entity_init_render_data
// =============================================================================
void collision_volume_init_all(int pEntity) {
    float fDefault = FLOAT_806155f0;
    int volumeTable = *(int*)(pEntity + 0x1F8);
    int offset = 0;

    for (int i = 0; i < *(int*)(pEntity + 0x1C); i++) {
        float* pVol = *(float**)(volumeTable + offset);

        // Set scale
        *pVol = fDefault;

        // Clear 4 dwords at +0x10
        int clearOffset = 0;
        for (int j = 0; j < 4; j++) {
            *(int*)((int)pVol + clearOffset + 0x10) = 0;
            clearOffset += 4;
        }

        // Mark slot assignments as unassigned
        *(u8*)((int)pVol + 0x0E) = 0xFF;
        *(u8*)((int)pVol + 0x0F) = 0xFF;

        offset += 4;
    }
}

// =============================================================================
// Address: 0x800fffdc | Name: collision_volume_transform
//
// MASSIVE function. Transforms all collision volumes from model space to world
// space using the entity's local matrix. For each volume:
//   1. On first call (mode==0): copies base data from source to runtime buffer
//   2. Transforms vertex positions through bone matrix then entity matrix
//   3. Computes face normal via cross product of edges
//   4. Normalizes normal vector (using sqrt)
//   5. Classifies dominant normal axis for fast rejection:
//      - Determines which axis (X, Y, Z) the normal is most aligned with
//      - Stores axis classification bytes at +0x3C, +0x3D, +0x3E
//
// Called by: entity_init_render_data, entity_prepare_render_volumes,
//            entity_update_transform
// =============================================================================
void collision_volume_transform(void) {
    float localMatrix[32]; // 4x4 matrix + workspace
    int entity;
    int mode;

    // Unpack args from callee-save register pair
    u64 packed = FUN_803826c8();
    entity = (int)(packed >> 32);
    mode = (int)packed;

    if (*(int*)(entity + 0x1C) == 0) {
        FUN_80382714();
        return;
    }

    int volOffset = 0;
    entity_build_local_matrix(localMatrix, entity, 1, 0);

    int* meshData = *(int**)(*(int*)(entity + 4) + 4);
    int boneCount = *meshData;
    int boneOffset = 0;

    for (int boneIdx = 0; boneIdx < boneCount; boneIdx++) {
        int baseOffset = boneOffset + boneCount * -0xC0;
        int subIdx = 0;
        int numSubs = *(short*)((int)meshData + baseOffset + 0x48);

        for (int s = 0; s < numSubs; s++) {
            u32* pSrc = *(u32**)(*(int*)((int)meshData + baseOffset + 0x78) + subIdx);
            u32* pDst = *(u32**)(*(int*)(entity + 0x1F8) + volOffset);
            volOffset += 4;

            // First frame: copy base volume data
            if (mode == 0) {
                // Copy header (3 dwords)
                pDst[0] = pSrc[0];
                pDst[1] = pSrc[1];
                pDst[2] = pSrc[2];
                // Clear transform area
                for (int k = 0; k < 4; k++) {
                    *(int*)((int)pDst + k * 4 + 0x10) = 0;
                }
                *(u8*)((int)pDst + 0x0E) = 0xFF;
                *(u8*)((int)pDst + 0x0F) = 0xFF;
                // Copy tail data
                pDst[8] = pSrc[8];
                pDst[9] = pSrc[9];
                *(u16*)(pDst + 10) = *(u16*)(pSrc + 10);
                pDst[0xB] = pSrc[0xB];
                *(u16*)((int)pDst + 0x2A) = *(u16*)((int)pSrc + 0x2A);
            }

            // Transform each vertex
            int numVerts = (int)*(u16*)((int)pDst + 0x2A);
            int vtxOffset = 0;
            for (int v = 0; v < numVerts; v++) {
                float* pSrcVtx = (float*)((int)pSrc + vtxOffset + 0x40);
                float* pDstVtx = (float*)((int)pDst + vtxOffset + 0x40);

                // Copy position
                pDstVtx[0] = pSrcVtx[0];
                pDstVtx[1] = pSrcVtx[1];
                pDstVtx[2] = pSrcVtx[2];

                if (mode == 0) {
                    // First frame: copy all vertex data
                    pDstVtx[3] = pSrcVtx[3];
                    pDstVtx[4] = pSrcVtx[4];
                    pDstVtx[5] = pSrcVtx[5];
                    pDstVtx[6] = pSrcVtx[6];
                } else {
                    // Transform through bone matrix
                    int boneIdx2 = *(char*)((int)meshData + baseOffset + 0x3D);
                    float* boneMtx = (float*)(*(int*)(*(int*)(entity + 4) + 0x10) + boneIdx2 * 0x40);
                    FUN_8012f9b8(boneMtx, pDstVtx);
                }

                // Transform through entity world matrix
                FUN_8012f9b8(localMatrix, pDstVtx);
                vtxOffset += 0x1C;
            }

            // Compute face normal from first triangle (edges v1-v0, v2-v0)
            float e1x = *(float*)(pDst + 0x1F) - *(float*)(pDst + 0x11);
            float e1y = *(float*)(pDst + 0x19) - *(float*)(pDst + 0x12);
            float e2x = *(float*)(pDst + 0x20) - *(float*)(pDst + 0x12);
            float e2y = *(float*)(pDst + 0x18) - *(float*)(pDst + 0x11);
            float e1z = *(float*)(pDst + 0x17) - *(float*)(pDst + 0x10);
            float e2z = *(float*)(pDst + 0x1E) - *(float*)(pDst + 0x10);

            // Cross product
            *(float*)(pDst + 0xC) = e2y * e2x - e1y * e1x;
            *(float*)(pDst + 0xD) = e1y * e2z - e1z * e2x;
            *(float*)(pDst + 0xE) = e1z * e1x - e2y * e2z;

            // Normalize
            float nx = *(float*)(pDst + 0xC);
            float ny = *(float*)(pDst + 0xD);
            float nz = *(float*)(pDst + 0xE);
            double len = FUN_80131538((double)(nx*nx + ny*ny + nz*nz));
            float invLen = (float)(FLOAT_806155f4 / len);
            *(float*)(pDst + 0xC) *= invLen;
            *(float*)(pDst + 0xD) *= invLen;
            *(float*)(pDst + 0xE) *= invLen;

            // Classify dominant axis for fast culling
            // Sorts normal components by absolute value to determine primary/secondary/tertiary
            float absX = *(float*)(pDst + 0xC);
            float absZ = *(float*)(pDst + 0xE);
            float absY = *(float*)(pDst + 0xD);

            if (FLOAT_8061560c <= absX) {
                if (absX <= FLOAT_80615610) {
                    if (FLOAT_8061560c <= absZ) {
                        if (absZ <= FLOAT_80615610) {
                            if (FLOAT_8061560c <= absY) {
                                *(u8*)(pDst + 0xF) = 2; // Primary=Z
                                *(u8*)((int)pDst + 0x3D) = 0;
                                *(u8*)((int)pDst + 0x3E) = 1;
                            } else {
                                *(u8*)(pDst + 0xF) = 0; // Primary=X
                                *(u8*)((int)pDst + 0x3D) = 2;
                                *(u8*)((int)pDst + 0x3E) = 1;
                            }
                        } else {
                            *(u8*)(pDst + 0xF) = 0;
                            *(u8*)((int)pDst + 0x3D) = 1;
                            *(u8*)((int)pDst + 0x3E) = 2;
                        }
                    } else {
                        *(u8*)(pDst + 0xF) = 1;
                        *(u8*)((int)pDst + 0x3D) = 0;
                        *(u8*)((int)pDst + 0x3E) = 2;
                    }
                } else {
                    *(u8*)(pDst + 0xF) = 1;
                    *(u8*)((int)pDst + 0x3D) = 2;
                    *(u8*)((int)pDst + 0x3E) = 0;
                }
            } else {
                *(u8*)(pDst + 0xF) = 2;
                *(u8*)((int)pDst + 0x3D) = 1;
                *(u8*)((int)pDst + 0x3E) = 0;
            }

            subIdx += 4;
        }
        boneOffset += 0xC0;
    }

    FUN_80382714();
}

// =============================================================================
// Address: 0x80100364 | Name: collision_volume_swept_ray
//
// Swept ray intersection test against entity collision volumes. For each volume
// with scale >= threshold, performs ray-polygon intersection (FUN_800150d8).
// Records up to 5 hits in param_5 result array. Also creates hit markers via
// FUN_80146610/FUN_801467ec.
// Called by: collision_entity_swept_cast
// =============================================================================
void collision_volume_swept_ray(int param3, float* rayDir, int* hitResults, int* hitList) {
    int entity;
    u64 packed = FUN_803826d8();
    entity = (int)((u64)packed >> 32);

    int volumeTable = *(int*)(entity + 0x1F8);
    if (volumeTable == 0) {
        FUN_80382724();
        return;
    }

    int offset = 0;
    for (int i = 0; i < *(int*)(entity + 0x1C); i++) {
        float* pVol = *(float**)(volumeTable + offset);

        if (FLOAT_806155f0 <= *pVol) {
            // Volume is active (scale >= 1.0)
            float hitDist, hitDistMax;
            hitDistMax = FLOAT_806155f4;
            hitDist = FLOAT_806155f4;
            float hitPos[3], hitNormal[3];

            int hit = FUN_800150d8(param3, rayDir, (int)pVol, 0,
                                   hitPos, hitNormal, &hitDist, &hitDistMax, 0);
            if (hit != 0) {
                // Create hit marker
                float* marker = (float*)FUN_80146610();
                if (marker != NULL) {
                    marker[0] = hitPos[0];
                    marker[1] = hitPos[1];
                    marker[2] = hitPos[2];
                    marker[3] = hitNormal[0];
                    marker[4] = hitNormal[1];
                    marker[5] = hitNormal[2];
                    marker[9] = (float)(int)packed;
                    marker[10] = (float)entity;
                    *(u16*)((int)marker + 0x2E) = 2;
                    marker[7] = hitDistMax;
                    marker[6] = hitDist;
                    *(u16*)(marker + 0xB) = 0;
                    FUN_801467ec(hitList, (int)marker);
                }

                // Record in results array (max 5)
                if (*hitResults < 5) {
                    float* pResult = (float*)(hitResults + *hitResults * 0xD + 2);
                    pResult[0] = (float)entity;
                    pResult[0xB] = *(float*)(entity + 0x34);
                    pResult[0xC] = *pVol;
                    pResult[1] = hitPos[0];
                    pResult[2] = hitPos[1];
                    pResult[3] = hitPos[2];
                    pResult[4] = rayDir[0];
                    pResult[5] = rayDir[1];
                    pResult[6] = rayDir[2];
                    pResult[7] = *(float*)((int)pVol + 0x30);
                    pResult[8] = *(float*)((int)pVol + 0x34);
                    pResult[9] = *(float*)((int)pVol + 0x38);
                    pResult[10] = FLOAT_806155f0;
                    (*hitResults)++;
                }
            }
        }
        offset += 4;
    }

    FUN_80382724();
}

// =============================================================================
// Address: 0x80100558 | Name: collision_volume_capsule_sweep
//
// Capsule sweep intersection test against entity collision volumes. Uses
// FUN_80018e10 for multi-hit capsule test. Records hits similarly to swept ray.
// Called by: collision_entity_capsule_sweep
// =============================================================================
void collision_volume_capsule_sweep(int param3, int* hitResults, int* hitList) {
    int entity;
    u64 packed = FUN_803826c4();
    entity = (int)((u64)packed >> 32);

    int volumeTable = *(int*)(entity + 0x1F8);
    if (volumeTable == 0) {
        FUN_80382710();
        return;
    }

    int offset = 0;
    for (int i = 0; i < *(int*)(entity + 0x1C); i++) {
        float* pVol = *(float**)(volumeTable + offset);

        if (FLOAT_806155f0 <= *pVol) {
            float hitDist;
            float hitPositions[8]; // Up to 8 intersection points
            int hitNormals[49];    // Hit normal data

            int numHits = FUN_80018e10(param3, hitNormals, (int)hitPositions, &hitDist, 8, 4, (int)pVol);

            int normOffset = 0;
            int posOffset = 0;
            for (int h = 0; h < numHits; h++) {
                u32* marker = (u32*)FUN_80146610();
                if (marker != NULL) {
                    FUN_80146660((double)FLOAT_806155f0,
                                (double)*(float*)((int)hitPositions + posOffset),
                                marker, (int*)((int)hitNormals + normOffset),
                                &hitDist, 0xFFFF, entity, (int)packed, -1);
                    FUN_80146650(hitList, (int)marker);

                    // Record first hit in results
                    if (h == 0 && *hitResults < 5) {
                        int* pResult = hitResults + *hitResults * 0xD + 2;
                        pResult[0] = entity;
                        pResult[0xB] = *(int*)(entity + 0x34);
                        pResult[0xC] = (int)pVol;
                        pResult[1] = *(int*)((int)hitNormals + normOffset);
                        pResult[2] = *(int*)((int)hitNormals + normOffset + 4);
                        pResult[3] = *(int*)((int)hitNormals + normOffset + 8);
                        pResult[4] = (int)hitDist;
                        pResult[5] = *(int*)((int)&hitDist + 4); // local_f4
                        pResult[6] = *(int*)((int)&hitDist + 8); // local_f0
                        pResult[7] = (int)hitDist;
                        pResult[8] = *(int*)((int)&hitDist + 4);
                        pResult[9] = *(int*)((int)&hitDist + 8);
                        pResult[10] = (int)FLOAT_806155f0;
                        (*hitResults)++;
                    }
                }
                posOffset += 4;
                normOffset += 0x10;
            }
        }
        offset += 4;
    }

    FUN_80382710();
}

// =============================================================================
// Address: 0x8010070c | Name: collision_volume_swept_sphere
//
// Sphere sweep intersection test against entity collision volumes. Uses
// FUN_8001a434 for sphere-polygon intersection. Records hits similarly.
// Called by: collision_entity_swept_cast_default_dir
// =============================================================================
void collision_volume_swept_sphere(float* sweepDir, int* hitResults, int* hitList) {
    int entity;
    u64 packed = FUN_803826c8();
    entity = (int)((u64)packed >> 32);

    int volumeTable = *(int*)(entity + 0x1F8);
    if (volumeTable == 0) {
        FUN_80382714();
        return;
    }

    int offset = 0;
    for (int i = 0; i < *(int*)(entity + 0x1C); i++) {
        float* pVol = *(float**)(volumeTable + offset);

        if (FLOAT_806155f0 <= *pVol) {
            float hitDist, hitDistY;
            int hitPositions[32];

            int numHits = FUN_8001a434(hitPositions, &hitDist, &hitDistY, 4, 4, (int)pVol, sweepDir);

            int normOffset = 0;
            for (int h = 0; h < numHits; h++) {
                u32* marker = (u32*)FUN_80146610();
                if (marker != NULL) {
                    FUN_80146660((double)FLOAT_806155f0, (double)hitDist,
                                marker, (int*)((int)hitPositions + normOffset),
                                &hitDistY, 0xFFFF, entity, (int)packed, -1);
                    FUN_80146650(hitList, (int)marker);

                    // Record first hit
                    if (h == 0 && *hitResults < 5) {
                        int* pResult = hitResults + *hitResults * 0xD + 2;
                        pResult[0] = entity;
                        pResult[0xB] = *(int*)(entity + 0x34);
                        pResult[0xC] = (int)pVol;
                        pResult[1] = *(int*)((int)hitPositions + normOffset);
                        pResult[2] = *(int*)((int)hitPositions + normOffset + 4);
                        pResult[3] = *(int*)((int)hitPositions + normOffset + 8);
                        pResult[4] = (int)hitDistY;
                        pResult[5] = *(int*)((int)&hitDistY + 4);
                        pResult[6] = *(int*)((int)&hitDistY + 8);
                        pResult[7] = (int)hitDistY;
                        pResult[8] = *(int*)((int)&hitDistY + 4);
                        pResult[9] = *(int*)((int)&hitDistY + 8);
                        pResult[10] = (int)FLOAT_806155f0;
                        (*hitResults)++;
                    }
                }
                normOffset += 0x10;
            }
        }
        offset += 4;
    }

    FUN_80382714();
}

// =============================================================================
// Address: 0x801008b4 | Name: collision_volume_raycast
//
// Raycast against entity collision volumes with portal-aware visibility check.
// For each volume with scale >= threshold, performs ray intersection via
// FUN_80013dfc. Records up to 5 hits. Additionally checks portal flags (0x40C)
// and validates hit side against portal plane normal for correct room assignment.
// On valid hit with portal flags, stores collision result globally.
// Called by: entity_raycast_room, entity_raycast_single
// =============================================================================
void collision_volume_raycast(float* rayOrigin, int* hitResults, int* hitPos, u32* hitNormal,
                              float* hitDist) {
    int entity;
    u64 packed = FUN_803826d0();
    entity = (int)((u64)packed >> 32);

    int volumeTable = *(int*)(entity + 0x1F8);
    if (volumeTable == 0) {
        FUN_8038271c();
        return;
    }

    int offset = 0;
    for (int i = 0; i < *(int*)(entity + 0x1C); i++) {
        float* pVol = *(float**)(volumeTable + offset);

        if (FLOAT_806155f0 <= *pVol) {
            float localHitDist;
            int localHitPos[3];

            int hit = FUN_80013dfc((double)*hitDist, (float*)(int)packed, rayOrigin,
                                   (int)pVol, (int)localHitPos, &localHitDist);
            if (hit != 0) {
                // Record in results (max 5)
                if (*hitResults < 5) {
                    int* pResult = hitResults + *hitResults * 0xD + 2;
                    pResult[0] = entity;
                    pResult[0xB] = *(int*)(entity + 0x34);
                    pResult[0xC] = (int)pVol;
                    pResult[1] = localHitPos[0];
                    pResult[2] = localHitPos[1];
                    pResult[3] = localHitPos[2];
                    pResult[4] = (int)*rayOrigin;
                    pResult[5] = (int)rayOrigin[1];
                    pResult[6] = (int)rayOrigin[2];
                    pResult[7] = *(int*)((int)pVol + 0x30);
                    pResult[8] = *(int*)((int)pVol + 0x34);
                    pResult[9] = *(int*)((int)pVol + 0x38);
                    pResult[10] = (int)localHitDist;
                    (*hitResults)++;
                }

                // Check portal flags for hit validation
                u16 flags = *(u16*)((int)pVol + 0x28);
                if ((flags & 0x100) != 0) {
                    hitResults[1] = 1; // Mark as portal hit
                }

                if (((flags & 0x40C) != 0) && ((flags & 0x4000) == 0)) {
                    int bValid = 1;

                    // Portal plane side check
                    if ((flags & 2) != 0) {
                        int* portal = (int*)scene_portal_get_ptr((int)*(short*)((int)pVol + 0xC));
                        bValid = 0;
                        float dot = rayOrigin[2] * *(float*)(portal + 4) +
                                    rayOrigin[0] * *(float*)(portal + 2) +
                                    rayOrigin[1] * *(float*)(portal + 3);
                        if ((FLOAT_806155f0 <= dot && *(int*)(entity + 0x34) == *portal) ||
                            (dot < FLOAT_806155f0 && *(int*)(entity + 0x34) != *portal)) {
                            bValid = 1;
                        }
                    }

                    if (bValid) {
                        if (hitPos != NULL) {
                            hitPos[0] = localHitPos[0];
                            hitPos[1] = localHitPos[1];
                            hitPos[2] = localHitPos[2];
                        }
                        if (hitNormal != NULL) {
                            hitNormal[0] = *(u32*)((int)pVol + 0x30);
                            hitNormal[1] = *(u32*)((int)pVol + 0x34);
                            hitNormal[2] = *(u32*)((int)pVol + 0x38);
                        }
                        if (hitDist != NULL) {
                            *hitDist = localHitDist;
                        }

                        // Store global collision result
                        memset_wrapper(0x80575CE0, 0xFF, 0x20);
                        DAT_80575ce0 = *(int*)(entity + 4);
                        collision_result_store(0x80575CE0);
                    }
                }
            }
        }
        offset += 4;
    }

    FUN_8038271c();
}
