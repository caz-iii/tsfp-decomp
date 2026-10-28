// Address: 0x800280d0-0x80028eeb | Name: skel_pose | Date: 2026-08-05
// Skeleton pose evaluation: keyframe interpolation, IK constraints,
// bone hierarchy concatenation, AABB bounds computation.

#include "common.h"
#include "animation.h"
#include "math_mtx.h"
#include "mem_alloc.h"

// ============================================================================
// Forward declarations (internal)
// ============================================================================
static void skel_ik_constraint_apply(void);
static void skel_base_pose_blend(void);
static void skel_bone_hierarchy_concat(int* pSkelCtx, int pBoneMatrices, int nBoneIndex);

// ============================================================================
// Address: 0x80028b10 | anim_time_wrap
// Wraps animation playback time to valid range within duration.
// ============================================================================
void anim_time_wrap(int pEntity, int nAnimSlot, int pAnimState) {
    float fDuration;
    float fTime;

    fDuration = *(float*)(*(int*)(g_AnimDataTable + *(int*)(pAnimState + 4) * 4) + 0x18);
    if (0.0f < fDuration) {
        *(float*)(pAnimState + 8) = *(float*)(pEntity + 8) -
            *(float*)(pEntity + nAnimSlot * 0x2c + 0x34);
        if (fDuration < *(float*)(pAnimState + 8)) {
            *(float*)(pAnimState + 8) = fDuration;
        }
        while (*(float*)(pAnimState + 8) < 0.0f) {
            *(float*)(pAnimState + 8) = *(float*)(pAnimState + 8) + fDuration;
        }
    }
}

// ============================================================================
// Address: 0x80028b7c | anim_keyframe_lookup
// Looks up keyframe data at a given frame index.
// Handles both streamed and non-streamed animation formats.
// ============================================================================
int* anim_keyframe_lookup(int nFrame) {
    int* pKeyframe;
    u32 uOffset;

    if (nFrame < 0) {
        nFrame = 0;
    }
    if (g_AnimTotalFrames <= nFrame) {
        nFrame = g_AnimTotalFrames - 1;
    }

    if (g_AnimStreamFlag == 0) {
        // Non-streamed: direct index into packed keyframe array
        uOffset = *(u32*)(g_AnimCurrentData + 8);
        pKeyframe = (int*)(g_AnimCurrentData +
            ((uOffset >> 2) + (u32)((int)uOffset < 0 && (uOffset & 3) != 0)) * 4);
        pKeyframe = pKeyframe + pKeyframe[nFrame + *pKeyframe * 0x10 + 1] * 0x10 + 1;
    } else {
        // Streamed: use streaming lookup function
        pKeyframe = (int*)FUN_80027f9c(nFrame, 0);
    }
    return pKeyframe;
}

// ============================================================================
// Address: 0x80028c14 | anim_keyframe_pose_query
// Queries full pose data (rotation quat, position, scale) from a keyframe.
// ============================================================================
void anim_keyframe_pose_query(void* pUnused1, void* pUnused2,
                              int* pPositionOut, int* pScaleOut) {
    int* pKeyframe;
    int* pRotationOut;

    // Prologue fetches context from PPC link register pair
    // pRotationOut and frame index come from register convention
    pRotationOut = (int*)FUN_803826f0_hi();
    int nFrame = FUN_803826f0_lo();

    pKeyframe = anim_keyframe_lookup(nFrame);
    if (pKeyframe != NULL) {
        if (pRotationOut != NULL) {
            pRotationOut[0] = pKeyframe[0];
            pRotationOut[1] = pKeyframe[1];
            pRotationOut[2] = pKeyframe[2];
            pRotationOut[3] = pKeyframe[3];
        }
        if (pPositionOut != NULL) {
            pPositionOut[0] = pKeyframe[4];
            pPositionOut[1] = pKeyframe[5];
            pPositionOut[2] = pKeyframe[6];
        }
        if (pScaleOut != NULL) {
            *pScaleOut = pKeyframe[7];
        }
    }
    FUN_8038273c(); // epilogue
}

// ============================================================================
// Address: 0x80028cb0 | anim_interpolate_pose
// Interpolates pose parameters (height, width, flags) between two keyframes.
// Used for animation-driven gameplay parameters.
// ============================================================================
void anim_interpolate_pose(void* pUnused1, void* pUnused2,
                           float* pHeightOut, float* pWidthOut,
                           int* pFlags1Out, int* pFlags2Out, int* pFlags3Out) {
    int* pKeyA;
    int* pKeyB;
    u32 uFrameInt;
    float fTime;
    float fFrameBase;
    double dLerp;

    // Context from PPC register convention
    fTime = *(float*)(FUN_803826e0_hi() + 8);
    uFrameInt = (u32)(fTime * FRAMES_PER_SECOND);
    fFrameBase = (float)((double)(u32)(uFrameInt ^ 0x80000000) - DOUBLE_BIAS) / FRAMES_PER_SECOND;

    dLerp = (double)((fTime - fFrameBase) /
        ((float)((double)((int)FUN_803826e0_lo() + 1U ^ 0x80000000) - DOUBLE_BIAS) /
         FRAMES_PER_SECOND - fFrameBase));

    if (dLerp < 0.0) dLerp = 0.0;
    if (dLerp > 1.0) dLerp = 1.0;

    pKeyA = anim_keyframe_lookup((int)FUN_803826e0_lo());
    pKeyB = anim_keyframe_lookup(uFrameInt);

    if (pHeightOut != NULL) {
        *pHeightOut = (float)(dLerp * (double)((float)pKeyB[8] - (float)pKeyA[8])
                     + (double)(float)pKeyA[8]);
    }
    if (pWidthOut != NULL) {
        *pWidthOut = (float)(dLerp * (double)((float)pKeyB[9] - (float)pKeyA[9])
                    + (double)(float)pKeyA[9]);
    }
    if (pFlags1Out != NULL) *pFlags1Out = pKeyA[0xb];
    if (pFlags2Out != NULL) *pFlags2Out = pKeyA[0xc];
    if (pFlags3Out != NULL) *pFlags3Out = pKeyA[10];

    FUN_8038272c(); // epilogue
}

// ============================================================================
// Address: 0x80028dfc | anim_bone_channel_lookup
// Looks up per-bone animation channel data for a given frame.
// Returns TRUE if channel data exists, FALSE otherwise.
// ============================================================================
BOOL anim_bone_channel_lookup(int pEntity, int nAnimSlot, int nFrame,
                              int nChannelIdx, int* pOutPtr) {
    int iAnimData;
    u32 uOffset;

    iAnimData = *(int*)(pEntity + nAnimSlot * 0x2c + 0x44);

    if (nFrame < 0) nFrame = 0;
    if (g_AnimTotalFrames <= nFrame) {
        nFrame = g_AnimTotalFrames - 1;
    }

    if (*(int*)(pEntity + 0x28) == 0) {
        // Non-streamed format
        uOffset = *(u32*)(iAnimData + nChannelIdx * 4 + 0x14);
        if (uOffset == 0) return FALSE;
        iAnimData = iAnimData +
            ((uOffset >> 2) + (u32)((int)uOffset < 0 && (uOffset & 3) != 0)) * 4 +
            nFrame * 0x18;
    } else {
        // Streamed format
        if (nChannelIdx < 1 || *(int*)(iAnimData + 0x18) < nChannelIdx) {
            return FALSE;
        }
        iAnimData = FUN_80027f9c(nFrame, **(int**)(iAnimData + *(int*)(iAnimData + 4) + 0x20));
        iAnimData = iAnimData + (nChannelIdx - 1) * 0x18;
    }
    *pOutPtr = iAnimData;
    return TRUE;
}

// ============================================================================
// Address: 0x8002888c | skel_bone_hierarchy_concat
// Recursive bone hierarchy matrix concatenation.
// Multiplies each bone's local transform by its parent's world transform.
// Traverses child/sibling tree structure from bone hierarchy table.
// ============================================================================
static void skel_bone_hierarchy_concat(int* pSkelCtx, int pBoneMatrices, int nBoneIndex) {
    int iParentIdx;
    int* pHierarchy;
    float* pBoneMtx;
    int iChildIdx, iSiblingIdx;
    int nOffset;

    // Prologue
    // FUN_803826f0 returns context pair

    nOffset = nBoneIndex * 0x14;
    pHierarchy = (int*)(*(int*)(*(int*)(*pSkelCtx + 0x14)) + nOffset);

    // Parent index at offset +4
    iParentIdx = pHierarchy[1]; // offset 0x04
    if (iParentIdx >= 0) {
        pBoneMtx = (float*)(pBoneMatrices + nBoneIndex * 0x40);
        // Multiply local by parent world matrix
        mtx_concat(pBoneMtx, (float*)(pBoneMatrices + iParentIdx * 0x40), pBoneMtx);
    }

    // First child at offset +8
    iChildIdx = pHierarchy[2]; // offset 0x08
    if (iChildIdx >= 0) {
        skel_bone_hierarchy_concat(pSkelCtx, pBoneMatrices, iChildIdx);
    }

    // Next sibling at offset +0xC
    iSiblingIdx = pHierarchy[3]; // offset 0x0C
    if (iSiblingIdx >= 0) {
        skel_bone_hierarchy_concat(pSkelCtx, pBoneMatrices, iSiblingIdx);
    }

    FUN_8038273c(); // epilogue
}

// ============================================================================
// Address: 0x8002893c | skel_ik_constraint_apply
// Applies IK constraint targets to skeleton bones.
// For bones with non-1.0 weight, multiplies base pose by IK-solved matrix.
// ============================================================================
static void skel_ik_constraint_apply(void) {
    int* pSkelCtx;
    int* pConstraintList;
    float* pWeight;
    int nCount;
    int i;
    int iMatOffset;

    // Context from register convention
    pSkelCtx = (int*)FUN_803826e4_hi();

    pConstraintList = *(int**)(*(int*)(*pSkelCtx + 0x10) + 0xc);
    pWeight = (float*)(pConstraintList + 1);

    nCount = *(int*)(*pSkelCtx + 0x20);
    iMatOffset = 0;

    for (i = 0; i < nCount; i++) {
        if (1.0 != (double)*pWeight) {
            // Blend IK result with base pose
            mtx_mul_blend(
                *(int*)(*pSkelCtx + 0x2c) + *pConstraintList * 0x40,
                pSkelCtx[1] + *pConstraintList * 0x40,
                (float*)(pSkelCtx[2] + iMatOffset)
            );
        }
        pConstraintList += 2;
        pWeight += 2;
        iMatOffset += 0x40;
    }
    FUN_80382730(); // epilogue
}

// ============================================================================
// Address: 0x80028a28 | skel_base_pose_blend
// Copies or blends the base (bind) pose into working bone matrices.
// For bones with weight 1.0, copies directly from base pose.
// For blended bones, multiplies working matrix by parent's world matrix.
// ============================================================================
static void skel_base_pose_blend(void) {
    int* pSkelCtx;
    int* pConstraintList;
    float* pWeight;
    int nCount;
    int i;
    int iMatOffset;
    int iParentIdx;

    pSkelCtx = (int*)FUN_803826e8();

    pConstraintList = *(int**)(*(int*)(*pSkelCtx + 0x10) + 0xc);
    pWeight = (float*)(pConstraintList + 1);
    nCount = *(int*)(*pSkelCtx + 0x20);
    iMatOffset = 0;

    for (i = 0; i < nCount; i++) {
        if (1.0 == (double)*pWeight) {
            // Direct copy from base pose
            mtx_copy(
                (float*)(pSkelCtx[1] + *pConstraintList * 0x40),
                (float*)(pSkelCtx[2] + iMatOffset)
            );
        } else {
            // Multiply by parent world matrix
            iParentIdx = *(int*)(*(int*)(*pSkelCtx + 0x14) +
                         *pConstraintList * 0x14 + 4);
            if (iParentIdx != -1) {
                mtx_concat(
                    (float*)(pSkelCtx[2] + iMatOffset),
                    (float*)(pSkelCtx[1] + iParentIdx * 0x40),
                    (float*)(pSkelCtx[2] + iMatOffset)
                );
            }
        }
        pConstraintList += 2;
        pWeight += 2;
        iMatOffset += 0x40;
    }
    FUN_80382734(); // epilogue
}

// ============================================================================
// Address: 0x800280d0 | skel_pose_update
// Main skeleton pose evaluation. Interpolates between keyframes for all bones,
// applies IK constraints, concatenates hierarchy, computes root motion delta.
// Called per-entity per-frame.
// ============================================================================
void skel_pose_update(void* pUnused1, void* pUnused2, int* pSkelState, int bCopyBasePose) {
    int* pSkelCtx;
    int iAnimData;
    int nBoneCount;
    int i;
    int iMatOffset;
    u32 uAnimFlags1, uAnimFlags2;
    u32 uFrameA, uFrameB;
    float fLerp;
    double dLerp;
    int bDiscontinuity;
    float afQuatA[4], afQuatB[4], afQuatOut[4];
    float afPosA[3], afPosB[3];

    // Prologue: get context from PPC registers
    pSkelCtx = (int*)FUN_803826e0_hi();

    // Check for animation discontinuity (clip change)
    bDiscontinuity = anim_motion_continuity_check(pSkelCtx);

    // Wrap animation time
    anim_time_wrap((int)pSkelCtx, (int)FUN_803826e0_lo(), (int)(pSkelState + 4));

    // Get animation data and flags
    int nAnimIdx = pSkelState[5];
    int nAnimOffset = nAnimIdx * 8;
    iAnimData = *(int*)(g_AnimDataTable + nAnimIdx * 4);
    uAnimFlags1 = *(u32*)(g_AnimFlagsTable + nAnimOffset);
    uAnimFlags2 = *(u32*)(g_AnimFlagsTable + nAnimOffset + 4);

    // Get surrounding keyframe indices
    FUN_80022ea4((int)(pSkelState + 4), &uFrameB, &uFrameA);

    // Compute interpolation factor
    fLerp = 1.0f;
    if (uFrameB != uFrameA) {
        float fTimeA = (float)((double)(uFrameB ^ 0x80000000) - DOUBLE_BIAS) / FRAMES_PER_SECOND;
        float fTimeB = (float)((double)(uFrameA ^ 0x80000000) - DOUBLE_BIAS) / FRAMES_PER_SECOND;
        fLerp = ((float)pSkelState[6] - fTimeA) / (fTimeB - fTimeA);
    }

    dLerp = (double)fLerp;
    if (bDiscontinuity != 0) {
        dLerp = (dLerp >= 0.5) ? 1.0 : 0.0;
    }

    // Interpolate all bone transforms
    iMatOffset = 0;
    nBoneCount = *(int*)(iAnimData + 0x14);
    for (i = 0; i < nBoneCount; i++) {
        // Extract keyframe rotation quaternions
        FUN_80024800(iAnimData, i, uFrameA, afPosA);  // frame A
        FUN_80024800(iAnimData, i, uFrameB, afPosB);  // frame B

        // SLERP between quaternions
        FUN_8012e234(dLerp, afQuatA, afQuatB, afQuatOut);
        // Convert quaternion to matrix
        FUN_8012e11c((float*)(pSkelState[1] + iMatOffset), afQuatOut);

        // Interpolate translation
        if (i == 0) {
            // Root bone: special handling for root motion
            if ((uAnimFlags2 & 0x82) == 0 && (uAnimFlags1 & 0x1800) == 0) {
                // No root motion: only Y translation
                *(float*)(pSkelState[1] + 0x30) = 0.0f;
                *(float*)(pSkelState[1] + 0x34) = (float)(dLerp *
                    (double)(afPosB[1] - afPosA[1]) + (double)afPosA[1]);
                *(float*)(pSkelState[1] + 0x38) = 0.0f;
            } else {
                // Full root motion: XYZ
                pSkelState[8]  = (int)(float)(dLerp * (double)(afPosB[0] - afPosA[0]) + (double)afPosA[0]);
                pSkelState[9]  = (int)(float)(dLerp * (double)(afPosB[1] - afPosA[1]) + (double)afPosA[1]);
                pSkelState[10] = (int)(float)(dLerp * (double)(afPosB[2] - afPosA[2]) + (double)afPosA[2]);
                *(int*)(pSkelState[1] + 0x30) = pSkelState[8];
                *(int*)(pSkelState[1] + 0x34) = pSkelState[9];
                *(int*)(pSkelState[1] + 0x38) = pSkelState[10];
            }
        } else {
            // Non-root bones: standard XYZ lerp
            *(float*)(pSkelState[1] + iMatOffset + 0x30) = (float)(dLerp *
                (double)(afPosB[0] - afPosA[0]) + (double)afPosA[0]);
            *(float*)(pSkelState[1] + iMatOffset + 0x34) = (float)(dLerp *
                (double)(afPosB[1] - afPosA[1]) + (double)afPosA[1]);
            *(float*)(pSkelState[1] + iMatOffset + 0x38) = (float)(dLerp *
                (double)(afPosB[2] - afPosA[2]) + (double)afPosA[2]);
        }
        iMatOffset += 0x40;
    }

    // Copy base pose if requested (additive blending setup)
    if (bCopyBasePose != 0) {
        iMatOffset = 0;
        int nTotalBones = *(int*)(*pSkelState + 0x1c);
        for (i = 0; i < nTotalBones; i++) {
            mtx_copy((float*)(pSkelState[1] + iMatOffset),
                     (float*)(*(int*)(*pSkelState + 0x2c) + iMatOffset));
            iMatOffset += 0x40;
        }
    }

    // Apply IK, hierarchy concatenation, base pose blend
    skel_ik_constraint_apply();
    skel_bone_hierarchy_concat(pSkelState, pSkelState[1], 0);
    skel_base_pose_blend();

    // Compute root motion delta
    if ((uAnimFlags2 & 0x82) == 0 && (uAnimFlags1 & 0x1800) == 0) {
        // No root motion
        pSkelState[0xe] = 0;
        pSkelState[0xf] = 0;
        pSkelState[0x10] = 0;
    } else {
        // Delta = current - previous
        pSkelState[0xe] = (int)((float)pSkelState[8]  - (float)pSkelState[0xb]);
        pSkelState[0xf] = (int)((float)pSkelState[9]  - (float)pSkelState[0xc]);
        pSkelState[0x10] = (int)((float)pSkelState[10] - (float)pSkelState[0xd]);
        // Store current as previous for next frame
        pSkelState[0xb] = pSkelState[8];
        pSkelState[0xc] = pSkelState[9];
        pSkelState[0xd] = pSkelState[10];
    }

    FUN_8038272c(); // epilogue
}

// ============================================================================
// Address: 0x800284ac | skel_pose_sample_bounds
// Samples skeleton pose at arbitrary time and computes AABB bounds from all
// bone positions. Used for visibility culling and collision broadphase.
// Allocates temporary bone buffer via mem_alloc.
// ============================================================================
void skel_pose_sample_bounds(int pSkelState, float* pMinOut, float* pMaxOut) {
    int* pSkelCtx;
    int iAnimData;
    int nBoneCount;
    int i;
    int iMatOffset;
    u32 uAnimFlags1, uAnimFlags2;
    u32 uFrameA, uFrameB;
    float fLerp;
    double dLerp;
    int bDiscontinuity;
    float fSavedTime;
    float afQuatA[4], afQuatB[4], afQuatOut[4];
    float afPosA[3], afPosB[3];
    float fBoneX, fBoneY, fBoneZ;

    // Prologue
    pSkelCtx = (int*)FUN_803826d8_hi();

    bDiscontinuity = anim_motion_continuity_check(pSkelCtx);
    fSavedTime = *(float*)(pSkelState + 0x18);
    anim_time_wrap((int)pSkelCtx, (int)FUN_803826d8_lo(), pSkelState + 0x10);

    int nAnimIdx = *(int*)(pSkelState + 0x14);
    int nAnimOffset = nAnimIdx * 8;
    iAnimData = *(int*)(g_AnimDataTable + nAnimIdx * 4);
    uAnimFlags1 = *(u32*)(g_AnimFlagsTable + nAnimOffset);
    uAnimFlags2 = *(u32*)(g_AnimFlagsTable + nAnimOffset + 4);

    FUN_80022ea4(pSkelState + 0x10, &uFrameB, &uFrameA);

    fLerp = 1.0f;
    if (uFrameB != uFrameA) {
        float fTimeA = (float)((double)(uFrameB ^ 0x80000000) - DOUBLE_BIAS) / FRAMES_PER_SECOND;
        float fTimeB = (float)((double)(uFrameA ^ 0x80000000) - DOUBLE_BIAS) / FRAMES_PER_SECOND;
        fLerp = (*(float*)(pSkelState + 0x18) - fTimeA) / (fTimeB - fTimeA);
    }

    dLerp = (double)fLerp;
    if (bDiscontinuity != 0) {
        dLerp = (dLerp >= 0.5) ? 1.0 : 0.0;
    }

    // Allocate temporary bone matrix buffer
    *(int*)(pSkelState + 4) = mem_alloc(16000, 0);

    // Evaluate pose into temp buffer
    nBoneCount = *(int*)(iAnimData + 0x14);
    iMatOffset = 0;
    for (i = 0; i < nBoneCount; i++) {
        FUN_80024800(iAnimData, i, uFrameA, afPosA);
        FUN_80024800(iAnimData, i, uFrameB, afPosB);

        FUN_8012e234(dLerp, afQuatA, afQuatB, afQuatOut);
        FUN_8012e11c((float*)(*(int*)(pSkelState + 4) + iMatOffset), afQuatOut);

        // Translation
        if (i < 1 && (uAnimFlags2 & 0x82) == 0 && (uAnimFlags1 & 0x1800) == 0) {
            *(float*)(*(int*)(pSkelState + 4) + iMatOffset + 0x30) = 0.0f;
            *(float*)(*(int*)(pSkelState + 4) + iMatOffset + 0x34) = (float)(dLerp *
                (double)(afPosB[1] - afPosA[1]) + (double)afPosA[1]);
            *(float*)(*(int*)(pSkelState + 4) + iMatOffset + 0x38) = 0.0f;
        } else {
            *(float*)(*(int*)(pSkelState + 4) + iMatOffset + 0x30) = (float)(dLerp *
                (double)(afPosB[0] - afPosA[0]) + (double)afPosA[0]);
            *(float*)(*(int*)(pSkelState + 4) + iMatOffset + 0x34) = (float)(dLerp *
                (double)(afPosB[1] - afPosA[1]) + (double)afPosA[1]);
            *(float*)(*(int*)(pSkelState + 4) + iMatOffset + 0x38) = (float)(dLerp *
                (double)(afPosB[2] - afPosA[2]) + (double)afPosA[2]);
        }
        iMatOffset += 0x40;
    }

    // Concatenate bone hierarchy
    skel_bone_hierarchy_concat((int*)pSkelState, *(int*)(pSkelState + 4), 0);

    // Compute AABB from all bone world positions
    *pMinOut = FLT_MAX;
    pMinOut[1] = FLT_MAX;
    pMinOut[2] = FLT_MAX;
    *pMaxOut = -FLT_MAX;
    pMaxOut[1] = -FLT_MAX;
    pMaxOut[2] = -FLT_MAX;

    iMatOffset = 0;
    for (i = 0; i < nBoneCount; i++) {
        fBoneX = *(float*)(*(int*)(pSkelState + 4) + iMatOffset + 0x30);
        fBoneY = *(float*)(*(int*)(pSkelState + 4) + iMatOffset + 0x34);
        fBoneZ = *(float*)(*(int*)(pSkelState + 4) + iMatOffset + 0x38);

        if (fBoneX < *pMinOut)    *pMinOut = fBoneX;
        if (fBoneX > *pMaxOut)    *pMaxOut = fBoneX;
        if (fBoneY < pMinOut[1])  pMinOut[1] = fBoneY;
        if (fBoneY > pMaxOut[1])  pMaxOut[1] = fBoneY;
        if (fBoneZ < pMinOut[2])  pMinOut[2] = fBoneZ;
        if (fBoneZ > pMaxOut[2])  pMaxOut[2] = fBoneZ;

        iMatOffset += 0x40;
    }

    // Free temp buffer and restore time
    mem_free_top(*(int*)(pSkelState + 4));
    *(int*)(pSkelState + 4) = 0;
    *(float*)(pSkelState + 0x18) = fSavedTime;

    FUN_80382724(); // epilogue
}
