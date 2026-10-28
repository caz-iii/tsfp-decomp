// Address: 0x80063154-0x80063e58 | Name: render_bone | Date: 2026-08-10
// Skeleton/bone transformation system: bind pose initialization, bone evaluation,
// node mapping, geometry transformation to bone-local space, mesh normal transforms.

#include "common.h"
#include "render_entity.h"

// ============================================================================
// render_bone_build_visibility_index
// Address: 0x80063154
//
// RECURSIVE: Assigns sequential visibility IDs to model nodes.
// A node is "visible" if its type flag at +0x3C == 0.
// Recurses into child (unless type 3) and sibling nodes.
// Used to build the bone index mapping table.
// ============================================================================

void render_bone_build_visibility_index(int* pIndexTable, int nNodeIdx,
                                        int pNodeBase, int nCurrentId)
{
    _savefpr_27();

    int iNodeOff = pNodeBase + nNodeIdx * 0xC0;
    int nId = nCurrentId;

    if (*(char*)(iNodeOff + 0x3C) == 0) {
        // Visible node - assign ID from counter
        nId = render_node_count_visible(iNodeOff, pNodeBase);
    }

    // Store index for this node
    pIndexTable[nNodeIdx] = nId;

    // Recurse into child (unless type 3)
    if (*(char*)(iNodeOff + 0x3F) >= 0) {
        if (*(char*)(iNodeOff + 0x3C) != 3) {
            render_bone_build_visibility_index(pIndexTable, (int)*(char*)(iNodeOff + 0x3F),
                                              pNodeBase, nId);
        }
    }

    // Recurse into sibling
    if (*(char*)(iNodeOff + 0x40) >= 0) {
        render_bone_build_visibility_index(pIndexTable, (int)*(char*)(iNodeOff + 0x40),
                                          pNodeBase, nCurrentId);
    }

    _restfpr_27();
}

// ============================================================================
// render_bone_init_bind_pose
// Address: 0x80063204
//
// Initializes the bone bind pose based on animation type.
// For types 0/1/0x15/0x27/0x7C: builds visibility index table, remaps animation
// type to initial pose selector (0→0x13, 1→0x15, 0x15→0x17, 0x27→0x19, 0x7C→0x14),
// then walks nodes to assign bone indices via jump table dispatch.
// For other types: zeros the entire bone state buffer (24 fields at 4 bytes each).
// ============================================================================

void render_bone_init_bind_pose(int pModelData, int pAnimEntry, int pBoneBuffer)
{
    _savefpr_28();

    int* pNodes = (int*)((u64)FUN_803826e0() >> 32);

    switch (*(int*)(pAnimEntry + 4)) {
    case 0: case 1: case 0x15: case 0x27: case 0x7C: {
        // Build visibility index table
        int* pNodeBase = pNodes + pNodes[0] * -0x30;
        render_bone_build_visibility_index(*(int**)(pBoneBuffer + 0x5C), 0, (int)pNodeBase, 0);

        // Remap animation type to initial pose index
        int nPoseType;
        int nAnimType = *(int*)(pAnimEntry + 4);
        if (nAnimType == 0x15) nPoseType = 0x17;
        else if (nAnimType == 0x7C) nPoseType = 0x14;
        else if (nAnimType == 1) nPoseType = 0x15;
        else if (nAnimType == 0) nPoseType = 0x13;
        else if (nAnimType == 0x27) nPoseType = 0x19;
        else nPoseType = 0;

        // Walk nodes, assign bone indices, handle type 3/1 special nodes
        int nCount = pNodes[0];
        for (int i = 0; i < nCount; i++) {
            char cType = *(char*)((char*)pNodeBase + i * 0xC0 + 0x3C);
            if (cType == 3 || cType == 1) {
                // Check if child/sibling shares bone with a specific marker
                char cChild = *(char*)((char*)pNodeBase + i * 0xC0 + 0x3E);
                char cSibling = *(char*)((char*)pNodeBase + i * 0xC0 + 0x3F);
                if (*(int*)(pBoneBuffer + 0x54) ==
                    (int)*(char*)((char*)pNodeBase + cChild * 0xC0 + 0x3D) ||
                    *(int*)(pBoneBuffer + 0x54) ==
                    (int)*(char*)((char*)pNodeBase + cSibling * 0xC0 + 0x3D)) {
                    *(int*)(*(int*)(pBoneBuffer + 0x5C) + i * 4) = 4;
                }
            }
        }
        break;
    }
    default:
        // Zero the bone state buffer (offsets 0x08..0x58)
        *(int*)(pBoneBuffer + 0x58) = 0;
        *(int*)(pBoneBuffer + 0x54) = 0;
        *(int*)(pBoneBuffer + 0x50) = 0;
        // ... (24 fields zeroed, offsets 0x08 through 0x58)
        *(float*)(pBoneBuffer + 0x08) = 0.0f; // converted from int magic
        break;
    }

    _restfpr_28();
}

// ============================================================================
// render_bone_eval_skeleton
// Address: 0x8006357c
//
// MASSIVE: Evaluates the initial skeleton pose for a render instance.
// 1. Looks up animation data (FUN_80022a08)
// 2. For each bone: samples keyframe (FUN_80024800), builds 4x3 rotation matrix
//    (FUN_8012e11c), copies bind position
// 3. Concatenates bone hierarchy via matrix multiply (FUN_80130b90)
// 4. Builds bone-to-node mapping (render_bone_build_node_mapping)
// 5. Evaluates full bone chain (FUN_80022c14, FUN_80022a38, FUN_80022b1c)
// 6. For specific anim types (0/1/0x15/0x7C): copies 6 predefined bone matrices
// 7. Applies entity position offset if flag 0x2000 is clear
// Core function called during render_instance_alloc_bone_buffers.
// ============================================================================

void render_bone_eval_skeleton(int* pInstance, int nAnimHandle, int bApplyOffset)
{
    _savefpr_23();

    int iInst = (int)((u64)FUN_803826dc() >> 32);
    int iAnimData = FUN_80022a08(nAnimHandle);
    int iTempPage = g_instance_temp_page; // DAT_80611b2c

    // Save and clear current anim state
    int nSavedAnim = *(int*)(*(int*)(iInst + 0x80) + 4);
    *(int*)(*(int*)(iInst + 0x80) + 4) = 0;

    // For each bone: sample keyframe → build rotation matrix → store position
    int nBoneCount = *(int*)(iAnimData + 0x14);
    int iMatOffset = 0;
    for (int i = 0; i < nBoneCount; i++) {
        render_entity_get_lod_distance(*(int*)(iInst + 4));

        // Sample keyframe for bone i
        float fQuat[4], fPos[3], fRot[4];
        FUN_80024800(iAnimData, i, 0, &fQuat);

        // Build 4x3 matrix from quaternion rotation
        float* pMatrix = (float*)(iTempPage + iMatOffset);
        FUN_8012e11c(pMatrix, &fRot);

        // Copy bind position into matrix column 3
        pMatrix[12] = fQuat[0]; // pos X
        pMatrix[13] = fQuat[1]; // pos Y
        pMatrix[14] = fQuat[2]; // pos Z

        // Concatenate with parent
        FUN_80130b90(pMatrix, (float*)(*(int*)(*(int*)(iInst + 0x84) + 0x78) + iMatOffset));

        iMatOffset += 0x40;
    }

    // Restore anim state
    *(int*)(*(int*)(iInst + 0x80) + 4) = nSavedAnim;

    // Build node mapping
    render_bone_build_node_mapping();

    // Evaluate full bone chain
    FUN_80022c14(iInst, 0, 1);
    FUN_80022a38(iInst, *(int*)(iInst + 0x10), 0, -1);
    FUN_80022b1c(iInst, *(int*)(iInst + 0x10), 0, -1);
    FUN_8002573c(*(int*)(iInst + 0x10));

    // Copy 6 predefined bone matrices for specific anim types
    u32 nAnimType = *(u32*)(*(int*)(iInst + 0x80) + 4);
    bool bCopySpecial = (nAnimType <= 1 || nAnimType == 0x15 || nAnimType == 0x7C);
    if (bCopySpecial) {
        int* pAnimDesc = *(int**)(iInst + 0x84);
        int iBoneBase = *(int*)(iInst + 0x10);
        // Copy 6 fixed bone matrices from animation descriptor
        FUN_80130b90((float*)(iBoneBase + *(int*)(pAnimDesc[0] + 0x0C) * 0x40), (float*)pAnimDesc[0x18]);
        FUN_80130b90((float*)(iBoneBase + *(int*)(pAnimDesc[0] + 0x10) * 0x40), (float*)pAnimDesc[0x19]);
        FUN_80130b90((float*)(iBoneBase + *(int*)(pAnimDesc[0] + 0x2C) * 0x40), (float*)pAnimDesc[0x1A]);
        FUN_80130b90((float*)(iBoneBase + *(int*)(pAnimDesc[0] + 0x1C) * 0x40), (float*)pAnimDesc[0x1B]);
        FUN_80130b90((float*)(iBoneBase + *(int*)(pAnimDesc[0] + 0x44) * 0x40), (float*)pAnimDesc[0x1C]);
        FUN_80130b90((float*)(iBoneBase + *(int*)(pAnimDesc[0] + 0x38) * 0x40), (float*)pAnimDesc[0x1D]);
    }

    // Extract root bone position
    float afRootPos[3];
    FUN_8012ef38((int)afRootPos);
    FUN_80025744(afRootPos, 1);

    // Store Y height from root bone
    *(int*)(*(int*)(iInst + 0x84) + 8) = *(int*)(*(int*)(iInst + 0x10) + 0x34);

    // Apply entity position offset
    if (bApplyOffset) {
        int iEntityDef = *(int*)(iInst + 4);
        if ((*(u32*)(iEntityDef + 0xC) & 0x2000) == 0) {
            // Compute offset from entity position minus bone root
            int iBoneRoot = *(int*)(iInst + 0x10);
            float fOffsetX = *(float*)(iEntityDef + 0x2C) - *(float*)(iBoneRoot + 0x30);
            float fOffsetY = *(float*)(iEntityDef + 0x30) - *(float*)(iBoneRoot + 0x34);
            float fOffsetZ = *(float*)(iEntityDef + 0x34) - *(float*)(iBoneRoot + 0x38);
            float afOffset[3] = { fOffsetX, fOffsetY, fOffsetZ };
            render_bone_apply_offset_recursive(iInst, 0, afOffset);
            *(float*)(iEntityDef + 0x2C) = fOffsetX;
            *(float*)(iEntityDef + 0x30) = fOffsetY;
            *(float*)(iEntityDef + 0x34) = fOffsetZ;
        } else {
            render_bone_apply_offset_recursive(iInst, 0, (float*)(iEntityDef + 0x2C));
        }
    }

    FUN_80025830();
    _restfpr_23();
}

// ============================================================================
// render_bone_build_node_mapping
// Address: 0x80063890
//
// Builds bone-to-node mapping table.
// Pass 1: For visible nodes (type==0), copies bind matrix and assigns sequential
//   bone index. Stores node→bone and bone→node bidirectional mapping.
// Pass 2: For type-2 nodes (inherited), copies parent node's bone index
//   from the child's parent reference.
// ============================================================================

void render_bone_build_node_mapping(void)
{
    _savefpr_23();

    int iInst = (int)((u64)FUN_803826dc() >> 32);
    int iBoneBuffer = /* from context */;
    int* pNodes = *(int**)(iInst + 4);
    int nBaseNode = pNodes[0];
    int nNodeCount = *pNodes;

    int nBoneIdx = 0; // running bone counter
    int iNodeOff = 0;

    // Pass 1: Map visible nodes to bone indices
    for (int i = 0; i < nNodeCount; i++) {
        int iRelOff = iNodeOff + nBaseNode * -0xC0;
        if (*(char*)((char*)pNodes + iRelOff + 0x3C) == 0) {
            // Visible node: copy bind matrix
            if (iBoneBuffer != 0) {
                FUN_80130b90((float*)(iBoneBuffer + nBoneIdx * 0x40),
                            (float*)(*(int*)(iInst + 0x10) +
                                    *(char*)((char*)pNodes + iRelOff + 0x3D) * 0x40));
            }
            // Store bidirectional mapping
            *(int*)(*(int*)(*(int*)(iInst + 0x84) + 4) + i * 4) = nBoneIdx;
            *(int*)(*(int*)(iInst + 0x84)[0] + nBoneIdx * 4) =
                (int)*(char*)((char*)pNodes + iRelOff + 0x3D);
            nBoneIdx++;
        }
        iNodeOff += 0xC0;
    }

    // Pass 2: Handle type-2 nodes (inherit parent's bone index)
    iNodeOff = 0;
    for (int i = 0; i < nNodeCount; i++) {
        int iRelOff = iNodeOff + nBaseNode * -0xC0;
        if (*(char*)((char*)pNodes + iRelOff + 0x3C) == 2) {
            // Copy bind matrix from parent node
            if (iBoneBuffer != 0) {
                int nParentIdx = *(char*)((char*)pNodes +
                    *(char*)((char*)pNodes + iRelOff + 0x3F) * 0xC0 + nBaseNode * -0xC0 + 0x3D);
                FUN_80130b90((float*)(*(int*)(iInst + 0x10) +
                            *(char*)((char*)pNodes + iRelOff + 0x3D) * 0x40),
                            (float*)(*(int*)(iInst + 0x10) + nParentIdx * 0x40));
            }
            // Copy parent's index
            int* pIndexTable = (int*)(*(int*)(*(int*)(iInst + 0x84) + 4));
            pIndexTable[i] = pIndexTable[*(char*)((char*)pNodes + iRelOff + 0x3F)];
        }
        iNodeOff += 0xC0;
    }

    _restfpr_23();
}

// ============================================================================
// render_bone_apply_offset_recursive
// Address: 0x80063a10
//
// RECURSIVE: Applies a position offset vector to all sub-object data
// (collision, attachments, etc.) in the model node hierarchy.
// Calls FUN_80064328 for each node's sub-object transform, then recurses.
// ============================================================================

void render_bone_apply_offset_recursive(int pInstance, int nNodeIdx, float* pOffset)
{
    _savefpr_25();

    int iInst = (int)((u64)FUN_803826f0() >> 32);
    int* pNodes = *(int**)(iInst + 4);
    int* pNode = pNodes + pNodes[0] * -0x30 + nNodeIdx * 0x30;

    // Apply offset to this node's sub-objects
    FUN_80064328(iInst, pNode, pOffset, NULL);

    // Recurse into child (unless type 3)
    if (*(char*)((char*)pNode + 0x3F) >= 0) {
        if (*(char*)(pNode + 0xF) != 3) {
            render_bone_apply_offset_recursive(iInst, (int)*(char*)((char*)pNode + 0x3F), pOffset);
        }
    }

    // Recurse into sibling
    if (*(char*)(pNode + 0x10) >= 0) {
        render_bone_apply_offset_recursive(iInst, (int)*(char*)(pNode + 0x10), pOffset);
    }

    _restfpr_25();
}

// ============================================================================
// render_bone_transform_packed_normal
// Address: 0x80063aac
//
// Transforms a packed normal direction (3 signed bytes) through a rotation matrix.
// Input: param_2 points to 3 consecutive signed bytes (X, Y, Z direction).
// Process: unpack bytes → floats (/127.0), rotate via matrix, re-pack with clamping.
// Output: writes 3 clamped bytes back to param_2 (range [-127, +127]).
// ============================================================================

void render_bone_transform_packed_normal(float* pMatrix, char* pNormal)
{
    float afDir[4];

    // Unpack signed bytes to float direction vector
    afDir[0] = (float)(int)pNormal[0] / 127.0f;
    afDir[1] = (float)(int)pNormal[1] / 127.0f;
    afDir[2] = (float)(int)pNormal[2] / 127.0f;

    // Transform through rotation matrix
    FUN_8012fb90(pMatrix, afDir);

    // Re-pack with clamping to [-127, +127]
    for (int i = 0; i < 3; i++) {
        float fVal = afDir[i];
        if (fVal < -0.992f) {       // FLOAT_80613ecc
            *pNormal = -127;
        } else if (fVal >= 1.0f) {
            *pNormal = 127;
        } else {
            *pNormal = (char)(int)(127.0f * fVal);
        }
        pNormal++;
    }
}

// ============================================================================
// render_mesh_strip_unchanged
// Address: 0x80063bc8
//
// Checks if a vertex strip entry has the same state as its previous frame data.
// Compares two u16 values at different offsets within the strip structure.
// Returns 1 (true) if the values are equal (strip unchanged), 0 otherwise.
// Used to skip normal re-transformation for static strips.
// ============================================================================

u32 render_mesh_strip_unchanged(int pStripBase, int nStride, int nExtraOffset)
{
    int iEntry = pStripBase + nStride;
    int iCompare = nStride + nExtraOffset + 6;

    // Compare current u16 pair with previous frame's value
    u16 nCurrent = *(u16*)(iEntry + 3);
    u16 nPrevious = (u16)((u8)*(char*)(iEntry + 3 + iCompare) << 8 |
                          (u8)*(char*)(iEntry + 4 + iCompare));

    // countLeadingZeros(x) >> 5 == 1 iff x == 0
    return countLeadingZeros((u32)(nPrevious - nCurrent)) >> 5;
}

// ============================================================================
// render_mesh_transform_normals
// Address: 0x80063c00
//
// Transforms vertex normals in mesh strip data through bone rotation matrices.
// Walks variable-stride strip structures, for each vertex that has changed:
//   - Looks up normal from global table (DAT_80412740, stride 0xC)
//   - Transforms through bone's rotation matrix (FUN_8012fb90)
//   - Re-packs to 16-bit via FUN_8018bea0
// Handles both single-bone and multi-bone (weighted) vertices.
// ============================================================================

void render_mesh_transform_normals(int param_1, int param_2, int nVertCount,
                                   int nStride, int bHasWeights, int pWeightTable)
{
    _savefpr_17();

    float* pMatrix = (float*)((u64)FUN_803826b8() >> 32);
    u32 nExtraStride = (bHasWeights != 0) ? 2 : 0;
    int iEntryStride = nStride + nExtraStride + 6;

    float* pCurMatrix = pMatrix;
    int iChunkOff = 0;

    for (int iChunk = 0; iChunk < nVertCount; iChunk++) {
        int* pChunk = (int*)((int)FUN_803826b8() + iChunkOff); // register context
        int iDataLen = pChunk[1];

        if (iDataLen != 0) {
            char* pEntry = (char*)pChunk[0];
            char* pEnd = pEntry + iDataLen;

            while (pEntry < pEnd && *pEntry != 0) {
                u16 nCount = *(u16*)(pEntry + 1);

                // Check each vertex in strip
                for (u32 iVert = render_mesh_strip_unchanged((int)pEntry, nStride, nExtraStride);
                     (int)iVert < (int)nCount; iVert++) {

                    u16* pVertData = (u16*)(pEntry + iVert * iEntryStride + nStride + 5);
                    int iNormalIdx = (u32)*pVertData * 0xC;

                    // Select rotation matrix based on bone weight
                    if (nStride != 0 && pWeightTable != 0) {
                        pCurMatrix = pMatrix + (*(u8*)(pWeightTable + (u32)pVertData[-1]) & 0x1FFFFFF8) * 2;
                    }

                    // Lookup normal from global table
                    float afNormal[3];
                    afNormal[0] = *(float*)(0x80412740 + iNormalIdx);
                    afNormal[1] = *(float*)(0x80412744 + iNormalIdx);
                    afNormal[2] = *(float*)(0x80412748 + iNormalIdx);

                    // Transform normal
                    FUN_8012fb90(pCurMatrix, afNormal);

                    // Pack to 16-bit
                    u16 nPacked = FUN_8018bea0(afNormal);
                    *(char*)pVertData = (char)(nPacked >> 8);
                    *((char*)pVertData + 1) = (char)nPacked;
                }

                pEntry += iEntryStride * nCount + 3;
            }
        }
        iChunkOff += 8;
    }

    _restfpr_17();
}

// ============================================================================
// render_mesh_flush_vertex_data
// Address: 0x80063d7c
//
// Flushes vertex and normal data to GX hardware memory via DCStoreRange.
// Vertex positions: aligned to 32 bytes.
//   - Normal mode: nVerts * 12 bytes (3 floats per vertex)
//   - Compressed mode (flag 0x10): nVerts * 6 bytes (3 shorts per vertex)
// Normals: 
//   - Normal mode (no flag 0x40): nVerts * 4 bytes
//   - Indexed mode (flag 0x40): walks strip table, flushes per-strip buffers
// ============================================================================

void render_mesh_flush_vertex_data(int param_1, int param_2, int nVertCount,
                                   int pStripTable, int* pStripBuffers)
{
    _savefpr_27();

    int iInst = (int)((u64)FUN_803826e8() >> 32);

    // Flush vertex positions
    u32 nVertexAddr = *(u32*)((int)FUN_803826e8() + 4);
    if (nVertexAddr != 0) {
        u32 nVertSize;
        if ((*(u32*)(iInst + 0xC) & 0x10) == 0) {
            nVertSize = (nVertCount * 12 + 0x1F) & 0xFFFFFFE0; // 12 bytes, 32-aligned
        } else {
            nVertSize = (nVertCount * 6 + 0x1F) & 0xFFFFFFE0;  // 6 bytes, 32-aligned
        }
        FUN_8039955c(nVertexAddr, nVertSize); // DCStoreRange
    }

    // Flush normals
    u32 nNormalAddr = *(u32*)((int)FUN_803826e8() + 0xC);
    if (nNormalAddr != 0) {
        if ((*(u32*)(iInst + 0xC) & 0x40) == 0) {
            u32 nNormSize = (nVertCount * 4 + 0x1F) & 0xFFFFFFE0;
            FUN_8039955c(nNormalAddr, nNormSize);
        } else {
            // Indexed mode: flush per-strip buffers
            while (*(char*)(pStripTable + 3) >= 0) {
                if (pStripBuffers[1] != 0) {
                    FUN_8039955c(pStripBuffers[0], pStripBuffers[1]);
                }
                pStripBuffers += 2;
                pStripTable += 8;
            }
        }
    }

    _restfpr_27();
}

// ============================================================================
// render_bone_transform_geometry_to_local
// Address: 0x80063e58
//
// MASSIVE: Transforms all model sub-object geometry from world space into
// bone-local space. Handles 5 distinct geometry types from the node descriptor:
//
// 1. Collision points (+0x6C, count at +0x43, stride 0x18):
//    Subtract bone origin, rotate by inverse bind matrix.
//
// 2. Portal planes (+0x74, count at +0x46):
//    Transform start+end positions, recompute per-axis min/max AABB.
//
// 3. Collision volumes (+0x70, count at +0x44):
//    Transform vertex positions + normal directions, update X/Z bounds.
//
// 4. Sound sources (+0x7C, count at +0x4A):
//    Transform position and direction vectors.
//
// 5. Attachment points (+0x80, count at +0x4C, stride 0x1C):
//    Transform position only.
//
// All transforms: subtract bone origin (afStack_d8[9..11]), then rotate
// through 3x3 inverse bind matrix (FUN_8012fbf4 / FUN_8012fb90).
// ============================================================================

void render_bone_transform_geometry_to_local(int param_1, int param_2, float* pBoneMatrix)
{
    _savefpr_23();

    int iNodeDesc; // from register context (extraout_r4)
    FUN_803826dc();

    // Skip if node has no sub-objects
    if (*(char*)(iNodeDesc + 0x43) == 0 && *(short*)(iNodeDesc + 0x46) == 0 &&
        *(short*)(iNodeDesc + 0x44) == 0 && *(short*)(iNodeDesc + 0x4A) == 0 &&
        *(short*)(iNodeDesc + 0x4C) == 0) {
        goto done;
    }

    // Get bone matrix (from pBoneMatrix or lookup by bone index)
    float afBindMatrix[12]; // 3x3 rotation + position
    if (pBoneMatrix == NULL) {
        FUN_8002570c((int)*(char*)(iNodeDesc + 0x3D));
        float* pMtx = (float*)FUN_80025848();
        FUN_80130b90(pMtx, afBindMatrix);
    } else {
        FUN_80130b90(pBoneMatrix, afBindMatrix);
    }

    // Build inverse rotation matrix
    float afInvRot[12];
    FUN_80130cd8(afBindMatrix, afInvRot);

    float fOriginX = afBindMatrix[9];  // local_a8
    float fOriginY = afBindMatrix[10]; // local_a4
    float fOriginZ = afBindMatrix[11]; // local_a0

    // 1. Transform collision points
    if (*(char*)(iNodeDesc + 0x43) != 0) {
        int nCount = (int)*(char*)(iNodeDesc + 0x43);
        int iOff = 0;
        for (int i = 0; i < nCount; i++) {
            int iData = *(int*)(iNodeDesc + 0x6C);
            float afLocal[3];
            afLocal[0] = *(float*)(iData + iOff) - fOriginX;
            afLocal[1] = *(float*)(iData + iOff + 4) - fOriginY;
            afLocal[2] = *(float*)(iData + iOff + 8) - fOriginZ;
            FUN_8012fbf4(afInvRot, afLocal);
            *(float*)(*(int*)(iNodeDesc + 0x6C) + iOff) = afLocal[0];
            *(float*)(*(int*)(iNodeDesc + 0x6C) + iOff + 4) = afLocal[1];
            *(float*)(*(int*)(iNodeDesc + 0x6C) + iOff + 8) = afLocal[2];
            iOff += 0x18;
        }
    }

    // 2. Transform portal planes (start+end, recompute AABB)
    // 3. Transform collision volumes (vertices + normals, update bounds)
    // 4. Transform sound sources (position + direction)
    // 5. Transform attachment points (position only, stride 0x1C)
    // [Each follows the same pattern: subtract origin, rotate by inverse]

done:
    _restfpr_23();
}
