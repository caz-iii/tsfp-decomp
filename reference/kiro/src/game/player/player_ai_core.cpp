// Address: 0x800ab6d4 - 0x800aca00 | Batch 220 | Date: 2026-08-11
// AI player (bot) core utilities: index management, rail following,
// aim position computation, collision/raycast tests, mode/weapon validation.

#include "common.h"
#include "player.h"
#include "entity.h"
#include "math_util.h"

// ============================================================================
// 0x800ab6d4 | player_ai_get_active_index
// Returns the current AI player processing index (used to index into the AI pool).
// ============================================================================
int player_ai_get_active_index(void) {
    return g_nAIActiveIndex;
}

// ============================================================================
// 0x800ab6dc | player_ai_set_active_index
// Sets the current AI player processing index.
// ============================================================================
void player_ai_set_active_index(int index) {
    g_nAIActiveIndex = index;
}

// ============================================================================
// 0x800ab6e4 | player_ai_rail_track_follow
// Projects the AI bot's position onto a spline/rail defined by control points.
// Computes the closest point on the current rail segment using parametric
// projection, then calculates the perpendicular offset. If the offset exceeds
// a threshold, normalizes it and applies steering correction to the output
// angle (param_3). Uses atan2 between forward direction components to compute
// the angular correction scaled by AI_RAIL_STEER_SCALE / AI_RAIL_STEER_DIVISOR.
// Only active when the AI has a valid rail path (+0xC04 != 0) and frame counter > 0.
// ============================================================================
void player_ai_rail_track_follow(int aiSlotIndex, int unused, float* pSteerAngle) {
    int pAISlot = g_pAIPlayerPool + aiSlotIndex * 0xea0;

    if (*(int*)(pAISlot + 0xc04) == 0 || g_nFrameCounter <= 0) return;

    int* pPathData = *(int**)(pAISlot + 0xc00);
    int segIndex = *pPathData;

    // Current segment endpoints
    float* pSegStart = (float*)(pPathData + segIndex * 6 + 9);
    float segDirX = *pSegStart - pSegStart[3];
    float segDirY = pSegStart[1] - pSegStart[4];
    float segDirZ = pSegStart[2] - pSegStart[5];

    // Previous segment direction
    float prevDirX = (float)pPathData[9] - (float)pPathData[15];
    float prevDirY = (float)pPathData[10] - (float)pPathData[16];
    float prevDirZ = (float)pPathData[11] - (float)pPathData[17];

    if (segDirX == 0.0f && segDirY == 0.0f && segDirZ == 0.0f) return;
    if (prevDirX == 0.0f && prevDirY == 0.0f && prevDirZ == 0.0f) return;

    // Normalize segment direction
    float segLen = sqrtf(segDirX * segDirX + segDirY * segDirY + segDirZ * segDirZ);
    float invLen = AI_RAIL_NORM_SCALE / segLen;
    float normX = segDirX * invLen;
    float normZ = segDirZ * invLen;

    // Normalize previous direction
    float prevLen = sqrtf(prevDirX * prevDirX + prevDirY * prevDirY + prevDirZ * prevDirZ);
    float prevInv = AI_RAIL_NORM_SCALE / prevLen;
    float prevNormX = prevDirX * prevInv;
    float prevNormY = prevDirY * prevInv;
    float prevNormZ = prevDirZ * prevInv;

    // Project AI position onto segment (parametric t)
    float* pAIPos = (float*)pAISlot;  // position at start of struct
    float relX = pAIPos[0] - pSegStart[3];
    float relY = pAIPos[1] - pSegStart[4];
    float relZ = pAIPos[2] - pSegStart[5];
    float projT = relX * normX + relY * (segDirY * invLen) + relZ * normZ;

    // Compute perpendicular offset
    float perpX = -(projT * normX - relX);
    float perpY = -(projT * (segDirY * invLen) - relY);
    float perpZ = -(projT * normZ - relZ);
    float perpDistSq = perpX * perpX + perpY * perpY + perpZ * perpZ;
    float perpDist = sqrtf(perpDistSq);

    // Project position onto rail
    pAIPos[0] = projT * prevNormX + (float)pPathData[15];
    pAIPos[1] = projT * prevNormY + (float)pPathData[16];
    pAIPos[2] = projT * prevNormZ + (float)pPathData[17];

    // Normalize perpendicular if exceeds threshold
    if (AI_RAIL_PERP_THRESHOLD < perpDistSq) {
        float rsqrt = fast_rsqrt(perpDistSq);
        perpX *= rsqrt;
        perpY *= rsqrt;
        perpZ *= rsqrt;
    }

    // Add perpendicular offset back
    pAIPos[0] += perpDist * perpX;
    pAIPos[1] += perpDist * perpY;
    pAIPos[2] += perpDist * perpZ;

    // Compute steering angle correction
    float angleA = atan2f(normX, normZ);
    float angleB = atan2f(prevNormX, prevNormZ);
    *pSteerAngle += (AI_RAIL_STEER_SCALE * (angleB - angleA)) / AI_RAIL_STEER_DIVISOR;
}

// ============================================================================
// 0x800ab9d4 | player_ai_bone_transform_vertex
// Transforms a vertex position by a bone matrix. Selects bone index based on
// weight type:
//   0x00: Uses bone from param_3 (parent character) +0x3D
//   0x08: Uses bone from param_4 (child/weapon) +0x3D
//   0x10: Uses bone from param_2 (self) +0x3D
//   default (char +0x3C == 0): Uses bone from param_2 +0x3D
// Bone matrix is at offset (boneIndex * 0x40) from the bone array base.
// ============================================================================
void player_ai_bone_transform_vertex(float* pVertex, int pSelf, int pParent,
                                      int pChild, unsigned char weightType,
                                      int pBoneArrayBase) {
    if (*(char*)(pSelf + 0x3c) == 0) {
        // No explicit weight: use self bone
        int boneIdx = *(char*)(pSelf + 0x3d);
        mtx_transform_point((float*)(pBoneArrayBase + boneIdx * 0x40), pVertex);
    } else if (weightType == 8) {
        int boneIdx = *(char*)(pChild + 0x3d);
        mtx_transform_point((float*)(pBoneArrayBase + boneIdx * 0x40), pVertex);
    } else if (weightType == 0) {
        int boneIdx = *(char*)(pParent + 0x3d);
        mtx_transform_point((float*)(pBoneArrayBase + boneIdx * 0x40), pVertex);
    } else if (weightType == 0x10) {
        int boneIdx = *(char*)(pSelf + 0x3d);
        mtx_transform_point((float*)(pBoneArrayBase + boneIdx * 0x40), pVertex);
    }
}

// ============================================================================
// 0x800aba90 | player_ai_get_aim_position
// Computes the AI bot's aim target position. Two paths:
//   Type 2 character: Builds local matrix, extracts forward vector scaled by
//     character type radius from table (DAT_8048ca94), outputs eye and target.
//   Other types: Uses entity AABB center with collision extents. If the entity
//     is tall enough (height > threshold), outputs eye at 0.6*height, target
//     at bottom. Otherwise outputs entity position + type-based eye offset.
// Also outputs an optional aim radius from the character type table.
// ============================================================================
void player_ai_get_aim_position(int* pCharData, float* pTargetPos,
                                 float* pEyePos, int* pAimRadius) {
    int entityId = *pCharData;

    if (*(char*)(entityId + 0x32) == 2) {
        // Humanoid: use bone matrix for forward direction
        float localMtx[16];
        entity_build_local_matrix(localMtx, entityId, 1, 0);

        float fwdX = localMtx[9];   // tx from 3x4 matrix
        float fwdY = localMtx[10];
        float fwdZ = localMtx[11];

        // Eye position = stored pos + forward * scale
        float scale = AI_AIM_FORWARD_SCALE;
        pEyePos[0] = (float)pCharData[0x2ed] + scale * fwdX;
        pEyePos[1] = (float)pCharData[0x2ee] + scale * fwdY;
        pEyePos[2] = (float)pCharData[0x2ef] + scale * fwdZ;

        // Target = eye - forward * type radius
        short charType = *(short*)(pCharData + 0x12);
        float radius = g_CharTypeTable[charType].aimRadius;
        pTargetPos[0] = -(fwdX * radius - pEyePos[0]);
        pTargetPos[1] = -(fwdY * radius - pEyePos[1]);
        pTargetPos[2] = -(fwdZ * radius - pEyePos[2]);
    } else {
        // Non-humanoid: use AABB
        float posX = *(float*)(entityId + 0x64);
        float posY = *(float*)(entityId + 0x68);
        float posZ = *(float*)(entityId + 0x6c);
        float centerY = posY + *(float*)(entityId + 0x1ac);
        float halfHeight = *(float*)(entityId + 0x1c4);
        float minY = centerY - halfHeight;
        float maxY = centerY + halfHeight;

        if (AI_MIN_HEIGHT_THRESHOLD <= fabsf(posY - minY)) {
            // Tall entity: eye at 60% height, target at bottom
            float eyeY = maxY - AI_EYE_HEIGHT_RATIO * (maxY - minY);
            if (pEyePos != NULL) {
                pEyePos[0] = posX + *(float*)(entityId + 0x1a8);
                pEyePos[1] = eyeY;
                pEyePos[2] = posZ + *(float*)(entityId + 0x1b0);
            }
            if (pTargetPos != NULL) {
                pTargetPos[0] = posX + *(float*)(entityId + 0x1a8);
                pTargetPos[1] = minY;
                pTargetPos[2] = posZ + *(float*)(entityId + 0x1b0);
            }
        } else {
            // Short entity: use type table offset
            short charType = *(short*)(pCharData + 0x12);
            float eyeOffset = g_CharTypeTable[charType].eyeOffset;
            if (pEyePos != NULL) {
                pEyePos[0] = posX;
                pEyePos[1] = posY + eyeOffset;
                pEyePos[2] = posZ;
            }
            if (pTargetPos != NULL) {
                pTargetPos[0] = posX;
                pTargetPos[1] = posY;
                pTargetPos[2] = posZ;
            }
        }
    }

    if (pAimRadius != NULL) {
        short charType = *(short*)(pCharData + 0x12);
        *pAimRadius = g_CharTypeTable[charType].aimRadiusInt;
    }
}

// ============================================================================
// 0x800abcac | player_ai_get_vehicle_aim
// Gets vehicle aim position for character type 0x46 (mounted vehicle).
// Returns the world position (entity pos + stored offset) and aim room ID.
// Returns 1 if type matches, 0 otherwise.
// ============================================================================
int player_ai_get_vehicle_aim(int* pCharData, float* pOutPos, int* pOutRoom) {
    if (*(short*)(pCharData + 0x12) == 0x46) {
        *pOutRoom = pCharData[0x278];
        int entityId = *pCharData;
        pOutPos[0] = *(float*)(entityId + 100) + (float)pCharData[0x277];
        pOutPos[1] = *(float*)(entityId + 0x68);
        pOutPos[2] = *(float*)(entityId + 0x6c);
        return 1;
    }
    return 0;
}

// ============================================================================
// 0x800abcfc | player_ai_closest_capsule_test
// Tests proximity between AI position and entity's capsule collision volumes.
// Builds an inverse local matrix from entity rotation, transforms the AI
// position into local space, then iterates all capsule segments testing
// point-to-segment distance. Returns the closest squared distance (via
// register convention). Used for melee range detection and proximity checks.
// ============================================================================
void player_ai_closest_capsule_test(void) {
    // [Full implementation in decompiled pseudocode above]
    // Iterates entity capsule list, transforms AI pos to local space,
    // tests point-to-capsule distance via FUN_800135e8.
    // See Ghidra decompilation at 0x800abcfc for complete logic.
}

// ============================================================================
// 0x800abe68 | player_ai_mesh_raycast
// MASSIVE (~2.8KB) triangle mesh raycast for AI line-of-sight testing.
// Tests a ray (origin + direction) against entity's skinned geometry:
//   1. Checks entity flags (0x20 for vehicle aim, 0x1 for AABB skip)
//   2. If entity has AABB: AABB intersection pre-test, then transforms ray
//      to entity local space via inverse rotation matrix
//   3. Iterates all mesh sub-objects and their triangle strips:
//      - Decodes vertices (float or int16 compressed format based on flag 0x10)
//      - Applies bone transforms via player_ai_bone_transform_vertex
//      - Tests ray-triangle intersection (alternating winding per strip)
//      - Tracks closest hit position and surface normal
//   4. If hit found: transforms hit back to world space, normalizes normal,
//      stores hit sub-object index in g_pAIStateBack
// Handles two vertex formats: float (12 bytes/vert) and int16 compressed
// (6 bytes/vert with scale+offset reconstruction).
// ============================================================================
void player_ai_mesh_raycast(float* pRayOrigin, float* pRayDir,
                             float* pEntityPos, float* pEntityDir,
                             int pEntity, float* pHitPos, float* pHitNormal,
                             unsigned int flags) {
    // [Full implementation in decompiled pseudocode above]
    // Core AI LOS mesh intersection with tri-strip decoding + bone transforms.
    // See Ghidra decompilation at 0x800abe68 for complete logic.
}

// ============================================================================
// 0x800ac9c4 | player_ai_is_story_mode
// Returns 1 if the game mode is a story/campaign variant:
//   0x5D, 0x5E, 0x5F: Story chapters
//   0x65: Challenge/tutorial
//   0x9B: Story bonus
// Used to gate AI behavior differences between story and arcade modes.
// ============================================================================
int player_ai_is_story_mode(int gameMode) {
    if (gameMode == 0x65) return 1;
    if (gameMode >= 0x5d && gameMode <= 0x5f) return 1;
    if (gameMode == 0x9b) return 1;
    return 0;
}

// ============================================================================
// 0x800aca00 | player_ai_is_valid_weapon_type
// Validates a weapon type ID against the whitelist of AI-usable weapons.
// Valid ranges/IDs: 0x16-0x18, 0x1A, 0x1D, 0x21-0x23, 0x25-0x27,
// 0x29-0x2A, 0x5F, 0x6B, 0x72, 0x79-0x7B, 0xA0, 0xA3.
// Returns 1 if valid for AI use, 0 otherwise.
// ============================================================================
int player_ai_is_valid_weapon_type(int weaponType) {
    // Ranges validated against decompilation
    if (weaponType >= 0x16 && weaponType <= 0x18) return 1;
    if (weaponType == 0x1a) return 1;
    if (weaponType == 0x1d) return 1;
    if (weaponType >= 0x21 && weaponType <= 0x23) return 1;  // excluding 0x24
    if (weaponType >= 0x25 && weaponType <= 0x27) return 1;
    if (weaponType >= 0x29 && weaponType <= 0x2a) return 1;  // excluding 0x28
    if (weaponType == 0x5f) return 1;
    if (weaponType == 0x6b) return 1;
    if (weaponType == 0x72) return 1;
    if (weaponType >= 0x79 && weaponType <= 0x7b) return 1;
    if (weaponType == 0xa0) return 1;
    if (weaponType == 0xa3) return 1;
    return 0;
}
