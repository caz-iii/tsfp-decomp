// Address: 0x8006135c-0x80061e30 | Name: render_instance | Date: 2026-08-10
// Render entity instance lifecycle: pool management, bone buffer allocation,
// reference relocation, material setup, creation and destruction.

#include "common.h"
#include "render_entity.h"

// ============================================================================
// Globals
// ============================================================================

static int   g_instance_pool_base;     // DAT_80611b34 - base address of instance pool
static int   g_instance_pool_count;    // DAT_80611b50 - number of allocated slots
static int   g_instance_pool_free;     // DAT_80611b4c - number of free slots
static int   g_instance_temp_page;     // DAT_80611b2c - temp page alloc for bone data
static int   g_render_instance_valid;  // DAT_80611970 - validity bitmask
static int   g_valid_flag;             // DAT_80610420 - computed valid flag

#define INSTANCE_STRIDE   0x1F8
#define SENTINEL_FLOAT    FLOAT_80613e5c  // "unset" marker

// ============================================================================
// render_instance_check_valid
// Address: 0x8006135c
// ============================================================================

void render_instance_check_valid(int param_1, int* pOutResult)
{
    if (pOutResult != NULL) {
        *pOutResult = 0;
    }
    // countLeadingZeros(x) >> 5 == 1 iff x == 0
    g_valid_flag = (countLeadingZeros(g_render_instance_valid) >> 5);
}

// ============================================================================
// render_instance_relocate_refs
// Address: 0x80061380
//
// RECURSIVE: Relocates model/texture references within a render instance.
// Walks 4 short-pointer tables (terminated by sign bit at byte+3),
// sub-objects (stride 0xB4), property arrays (stride 0x30), and material
// tables. Recurses into child nodes via linked indices.
//
// param_3 = model index delta
// param_4 = texture index delta
// ============================================================================

void render_instance_relocate_refs(int* pInstance, int nNodeIndex,
                                   int nModelDelta, int nTextureDelta)
{
    _savefpr_26();

    int nBase = pInstance[0]; // base node index
    int iOffset = nNodeIndex * 0xC0 + nBase * -0xC0;

    int iSubObjBase = pInstance[nBase * -0x30 + nNodeIndex * 0x30 + 0x18];
    int iPropBase   = pInstance[nBase * -0x30 + nNodeIndex * 0x30 + 0x2A];

    // Relocate 4 short-pointer tables (texture refs)
    for (int iTable = 0; iTable < 4; iTable++) {
        short* pTable = *(short**)((char*)pInstance + iTable * 4 + 0x54 + iOffset);
        if (pTable != NULL) {
            // Walk entries until sign bit set at byte+3
            while (*(char*)((char*)pTable + 3) >= 0) {
                if (*pTable >= 0) {
                    *pTable += (short)nTextureDelta;
                }
                pTable += 4; // 8 bytes per entry
            }
        }
    }

    // Relocate sub-objects (stride 0xB4, model refs at +8)
    int nSubObjCount = *(char*)((char*)pInstance + iOffset + 0x42);
    for (int i = 0; i < nSubObjCount; i++) {
        if (*(int*)(iSubObjBase + 8) >= 0) {
            *(int*)(iSubObjBase + 8) += nModelDelta;
        }
        iSubObjBase += 0xB4;
    }

    // Relocate property arrays (stride 0x30, value at +4)
    int nPropCount = *(short*)((char*)pInstance + iOffset + 0x4E);
    for (int i = 0; i < nPropCount; i++) {
        int iVal = *(int*)(iPropBase + i * 0x30 + 4);
        if (iVal >= 0) {
            *(int*)(iPropBase + i * 0x30 + 4) = iVal + nModelDelta;
        }
    }

    // Relocate material table entries (texture refs at +0x10)
    int nMatCount = *(short*)((char*)pInstance + iOffset + 0x4A);
    for (int i = 0; i < nMatCount; i++) {
        int pMat = *(int*)(pInstance[nBase * -0x30 + nNodeIndex * 0x30 + 0x1F] + i * 4);
        int iTexRef = *(int*)(pMat + 0x10);
        if (iTexRef >= 0) {
            *(int*)(pMat + 0x10) = iTexRef + nTextureDelta;
        }
    }

    // Recurse into child nodes
    char cChild = *(char*)((char*)pInstance + iOffset + 0x3F);
    if (cChild >= 0 && *(char*)(pInstance + nBase * -0x30 + nNodeIndex * 0x30 + 0xF) != 3) {
        render_instance_relocate_refs(pInstance, (int)cChild, nModelDelta, nTextureDelta);
    }
    if (*(char*)(pInstance + nBase * -0x30 + nNodeIndex * 0x30 + 0x10) >= 0) {
        render_instance_relocate_refs(
            pInstance,
            (int)*(char*)(pInstance + nBase * -0x30 + nNodeIndex * 0x30 + 0x10),
            nModelDelta, nTextureDelta);
    }

    _restfpr_26();
}

// ============================================================================
// render_instance_link_props
// Address: 0x8006151c
//
// Links render instance properties after model data is loaded.
// Relocates sub-object indices by model delta, then calls
// render_instance_relocate_refs for full recursive fixup.
// ============================================================================

void render_instance_link_props(int pInstance, int param_2,
                                int nModelDelta, int pPropTable, int nTexDelta)
{
    _savefpr_25();

    // Relocate prop table entries (stride 0x30, value at +16)
    int* pEntry = *(int**)(pInstance + 0x14);
    while (pEntry[4] >= 0) {
        if (*pEntry >= 0) {
            *pEntry += nModelDelta;
        }
        pEntry += 0xC; // stride 0x30 bytes = 12 ints
    }

    // Recursive relocation from root node (index 0)
    render_instance_relocate_refs((int*)pInstance, 0, nModelDelta, nTexDelta);

    // Store new linkage
    *(int*)(pInstance + 0x10) = /* node count from uVar3 */;
    *(int*)(pInstance + 0x14) = pPropTable;

    _restfpr_25();
}

// ============================================================================
// render_instance_create
// Address: 0x80061598
//
// MASSIVE: Allocates a render entity instance from the pool (stride 0x1F8).
// - Acquires slot from free list or appends new entry
// - Initializes all fields with sentinel float values
// - Sets up bone buffers based on LOD mode:
//     -1 or 2: no bones
//     3: simple buffers (render_instance_alloc_simple_buffers)
//     default: full bone buffers (render_instance_alloc_bone_buffers)
// - Registers with light probe system
// - Configures visual state defaults (alpha 0x66, color -1, etc.)
// ============================================================================

void render_instance_create(void)
{
    _savefpr_25();

    int iModelDef; // from register save context
    int* pSlot = NULL;

    g_instance_temp_page = mem_page_alloc();

    // Acquire a pool slot
    if (g_instance_pool_free == 0) {
        // No free slots - append new
        int iOffset = g_instance_pool_count * INSTANCE_STRIDE;
        g_instance_pool_count++;
        pSlot = (int*)(g_instance_pool_base + iOffset);
    } else {
        // Search free list
        for (int i = 0; pSlot == NULL && i < g_instance_pool_count; i++) {
            int* pCandidate = (int*)(g_instance_pool_base + i * INSTANCE_STRIDE);
            if (pCandidate[1] == 0) { // slot is free
                pSlot = pCandidate;
            }
        }
        if (pSlot == NULL) {
            // Fallback: append new, clear free flag
            int iOffset = g_instance_pool_count * INSTANCE_STRIDE;
            g_instance_pool_count++;
            g_instance_pool_free = 0;
            pSlot = (int*)(g_instance_pool_base + iOffset);
        } else {
            g_instance_pool_free--;
        }
    }

    // Initialize instance fields
    int nModelRef = *(int*)(iModelDef + 4);
    pSlot[3] = nModelRef;
    pSlot[1] = nModelRef;  // mark slot as active (non-zero)
    memset_wrapper((int)(pSlot + 2), 0, 4);
    pSlot[0] = /* pInstance base from context */;

    float fSentinel = SENTINEL_FLOAT;
    pSlot[7]    = 0;
    pSlot[0x24] = 0;
    pSlot[0x33] = 0;
    pSlot[0x34] = (int)DAT_804d1cd0; // frame count table
    pSlot[0x35] = *(int*)&fSentinel;
    pSlot[0x36] = *(int*)&fSentinel;
    pSlot[0x40] = *(int*)&fSentinel;
    // ... (remaining sentinel initializations for all transform channels)
    pSlot[0x47] = 0;
    pSlot[0x48] = 0;
    pSlot[0x49] = 0;
    // ... property B channels initialized similarly
    pSlot[0x61] = 0xFFFFFFFF;
    pSlot[0x62] = *(int*)&fSentinel;
    pSlot[0x63] = *(int*)&fSentinel;
    pSlot[0x64] = *(int*)&fSentinel;
    pSlot[0x65] = *(int*)&fSentinel;
    pSlot[0x66] = *(int*)&fSentinel;
    pSlot[0x32] = 1;

    memset_wrapper((int)(pSlot + 0x68), 0, 0x10);
    pSlot[0x60] = *(int*)&fSentinel;

    // Clear 4x3 arrays at end of struct
    for (int i = 0; i < 4; i++) {
        pSlot[0x6D + i]  = 0; // +0x1B4
        pSlot[0x70 + i]  = 0; // +0x1C0
        pSlot[0x73 + i]  = 0; // +0x1CC
        pSlot[0x76 + i]  = 0; // +0x1D8
    }

    *(u8*)((char*)pSlot + 0x1EC) = 0;
    *(u8*)((char*)pSlot + 0x1ED) = 0;
    *(u16*)((char*)pSlot + 0x1EE) = 0;

    // Clear visual state
    render_instance_clear_visual_state((int)pSlot);

    // Set up bone buffers based on LOD mode
    int nLodMode = *(int*)(iModelDef + 0x1C);
    if (*(int*)(pSlot[1] + 4) == 0 || nLodMode == -1) {
        pSlot[4] = 0; // no bone buffer needed
    } else {
        pSlot[4] = g_instance_temp_page + *(int*)(pSlot[1] + 4) * 0x80;
        pSlot[8] = 0;
        pSlot[9] = 0;
        pSlot[10] = 0;
        pSlot[11] = 0;
    }

    if (nLodMode == -1 || nLodMode == 2) {
        pSlot[0x20] = 0;
        pSlot[0x21] = 0;
    } else if (nLodMode == 3) {
        render_instance_alloc_simple_buffers();
    } else {
        // Full bone buffers (first time sets flag 0x4000000)
        int bFirstAlloc = (*(u32*)(iModelDef + 0x28) & 0x4000000) == 0;
        render_instance_alloc_bone_buffers(pSlot, iModelDef, bFirstAlloc ? 1 : 0);
        if (bFirstAlloc) {
            *(u32*)(iModelDef + 0x28) |= 0x4000000;
        }
    }

    // Register with lighting system
    light_probe_register_entities();

    // Final visual defaults
    pSlot[0x0C] = -(*(u32*)(pSlot[1] + 0xC) >> 8 & 1); // double-sided flag
    pSlot[0x6C] = 0;
    pSlot[0x0D] = 0xFFFFFFFF; // color override A
    pSlot[0x0E] = 0xFFFFFFFF; // color override B
    pSlot[0x1A] = *(int*)&fSentinel;
    pSlot[0x19] = *(int*)&fSentinel;
    pSlot[0x18] = *(int*)&fSentinel;
    pSlot[0x17] = *(int*)&fSentinel;
    pSlot[0x16] = *(int*)&fSentinel;
    pSlot[0x15] = *(int*)&fSentinel;
    *(u8*)((char*)pSlot + 0x7A) = 0x66; // default alpha A
    *(u8*)((char*)pSlot + 0x79) = 0x66; // default alpha B
    *(u8*)((char*)(pSlot + 0x1E)) = 0x66; // default alpha C

    float fBlend = FLOAT_80613eb0;
    pSlot[0x2E] = *(int*)&fBlend;
    pSlot[0x2F] = *(int*)&fBlend;

    mem_page_free(g_instance_temp_page);
    g_instance_temp_page = 0;

    _restfpr_25();
}

// ============================================================================
// render_instance_clear_visual_state
// Address: 0x800618ec
//
// Clears visual state flags at end of render instance struct.
// Resets color overrides, blend mode, visibility mask, alpha values.
// ============================================================================

void render_instance_clear_visual_state(int pInst)
{
    *(int*)(pInst + 0x1B0) = 0;     // visual flags
    *(int*)(pInst + 0x1E4) = 0;     // blend state A
    *(int*)(pInst + 0x1E8) = 0;     // blend state B
    *(u16*)(pInst + 0x1EE) = 0;     // visibility mask
    *(u8*)(pInst + 0x1F0) = 0xFF;   // alpha A
    *(u8*)(pInst + 0x1F1) = 0xFF;   // alpha B
    *(u8*)(pInst + 0x1F2) = 0;      // override flag
    *(u8*)(pInst + 0x7F)  = 0;      // LOD override
    *(u8*)(pInst + 0x1F3) = 0;      // reserved A
    *(u8*)(pInst + 0x1F4) = 0;      // reserved B
    *(u8*)(pInst + 0x1F5) = 0;      // reserved C
}

// ============================================================================
// render_instance_alloc_bone_buffers
// Address: 0x80061924
//
// Allocates bone transform buffers for a fully-animated render instance.
// Buffer layout:
//   Header: 0x80 bytes (8 sub-pointers at +0x60..+0x78)
//   Sub-matrices: (nBones+6) * 0x40 bytes (8 matrices at 0x40 stride each)
//   Bone indices: nBones * 4 bytes
//   Joint refs:   nJoints * 8 bytes (4 per joint * 2)
//   Total: 16-byte aligned
//
// For type-8 entities (mirrored), allocates a second identical buffer.
// Looks up animation table entry from DAT_804e5f20 (stride 0xC).
// ============================================================================

void render_instance_alloc_bone_buffers(int* pSlot, int iModelDef, int bFirstAlloc)
{
    _savefpr_26();

    if (*(int*)(iModelDef + 0x20) == 0) {
        int* pModelData = *(int**)(iModelDef + 4);
        bool bHasExisting = (pModelData != NULL && pModelData[0xE] != 0);

        int nBones  = pModelData[1];
        int nJoints = pModelData[0];
        int nMatrixSize = (nBones + 6) * 0x40;
        u32 nTotalSize = (nBones * 4 + nJoints * 8 + nMatrixSize + 0x8F) & 0xFFFFFFF0;

        u32 nExistingSize = bHasExisting ? *(u32*)pModelData[0xE] : 0;

        int iBuf;
        if ((int)nExistingSize < (int)nTotalSize) {
            iBuf = mem_alloc(nTotalSize, 1, nMatrixSize);
        } else {
            iBuf = pModelData[0xE]; // reuse existing buffer
        }

        // Notify if buffer resized with active sound
        if (*(int*)(iModelDef + 4) != 0 &&
            *(int*)(*(int*)(iModelDef + 4) + 0x38) != 0 &&
            nExistingSize != nTotalSize) {
            soundpad_stub_nop();
        }

        // Set up sub-buffer pointers
        *(int*)(iModelDef + 0x20) = iBuf;
        int iBase = iBuf;
        *(int*)(iBase + 0x60) = iBase + 0x80;
        *(int*)(iBase + 0x64) = iBase + 0xC0;
        *(int*)(iBase + 0x68) = iBase + 0x100;
        *(int*)(iBase + 0x6C) = iBase + 0x140;
        *(int*)(iBase + 0x70) = iBase + 0x180;
        *(int*)(iBase + 0x74) = iBase + 0x1C0;
        *(int*)(iBase + 0x78) = iBase + 0x200;

        // Bone transform array starts after 8 sub-matrices
        int iBoneArray = iBase + 0x200 + nBones * 0x40;
        *(int*)iBase = iBoneArray;
        iBoneArray += nBones * 4;
        *(int*)(iBase + 4) = iBoneArray;
        *(int*)(iBase + 0x5C) = iBoneArray + nJoints * 4;

        // Secondary buffer for type-8 entities (mirrored)
        if (*(int*)(pSlot[0] + 0x20) == 8) {
            int iBuf2 = mem_alloc(nTotalSize, 1, nMatrixSize);
            pSlot[0x22] = iBuf2;
            *(int*)(iBuf2 + 0x60) = iBuf2 + 0x80;
            *(int*)(iBuf2 + 0x64) = iBuf2 + 0xC0;
            *(int*)(iBuf2 + 0x68) = iBuf2 + 0x100;
            *(int*)(iBuf2 + 0x6C) = iBuf2 + 0x140;
            *(int*)(iBuf2 + 0x70) = iBuf2 + 0x180;
            *(int*)(iBuf2 + 0x74) = iBuf2 + 0x1C0;
            *(int*)(iBuf2 + 0x78) = iBuf2 + 0x200;
            int iBone2 = iBuf2 + 0x200 + nBones * 0x40;
            *(int*)iBuf2 = iBone2;
            iBone2 += nBones * 4;
            *(int*)(iBuf2 + 4) = iBone2;
            *(int*)(iBuf2 + 0x5C) = iBone2 + nJoints * 4;
        } else {
            pSlot[0x22] = 0;
        }
    }

    // Look up animation table entry (stride 0xC from DAT_804e5f20)
    int nAnimIdx = *(int*)(iModelDef + 0x1C);
    u8* pAnimEntry = (nAnimIdx == -1) ? NULL : &DAT_804e5f20 + nAnimIdx * 0xC;
    pSlot[0x20] = (int)pAnimEntry;

    int iBoneBuffer = *(int*)(iModelDef + 0x20);
    pSlot[0x23] = iBoneBuffer;
    pSlot[0x21] = iBoneBuffer;

    // Initialize bone state
    FUN_8006357c(pSlot, *(int*)(pSlot[0x20] + 8), bFirstAlloc);
    FUN_80063204(*(int*)(iModelDef + 4), pSlot[0x20], *(int*)(iModelDef + 0x20));

    _restfpr_26();
}

// ============================================================================
// render_instance_alloc_simple_buffers
// Address: 0x80061bd8
//
// Simplified bone buffer allocation for type-3 (static/simple) entities.
// No sub-matrix pointers (offsets 0x60-0x78 zeroed).
// Size = nBones*4 + nJoints*4 (16-byte aligned).
// Used for props, static objects without skeletal animation.
// ============================================================================

void render_instance_alloc_simple_buffers(void)
{
    _savefpr_27();

    int iSlot;   // from register context
    int iModelDef;

    if (*(int*)(iModelDef + 0x20) == 0) {
        int* pModelData = *(int**)(iModelDef + 4);
        bool bHasExisting = (pModelData != NULL && pModelData[0xE] != 0);

        int nBones  = pModelData[1];
        int nJoints = pModelData[0];
        u32 nTotalSize = (nBones * 4 + nJoints * 8 + 0x8F) & 0xFFFFFFF0;

        u32 nExistingSize = bHasExisting ? *(u32*)pModelData[0xE] : 0;

        int iBuf;
        if ((int)nExistingSize < (int)nTotalSize) {
            iBuf = mem_alloc(nTotalSize, 1);
        } else {
            iBuf = pModelData[0xE];
        }

        if (*(int*)(iModelDef + 4) != 0 &&
            *(int*)(*(int*)(iModelDef + 4) + 0x38) != 0 &&
            nExistingSize != nTotalSize) {
            soundpad_stub_nop();
        }

        *(int*)(iModelDef + 0x20) = iBuf;
        // Zero all sub-matrix pointers (no skeletal transforms)
        *(int*)(iBuf + 0x60) = 0;
        *(int*)(iBuf + 0x64) = 0;
        *(int*)(iBuf + 0x68) = 0;
        *(int*)(iBuf + 0x6C) = 0;
        *(int*)(iBuf + 0x70) = 0;
        *(int*)(iBuf + 0x74) = 0;
        *(int*)(iBuf + 0x78) = 0;

        // Bone array starts right after header (0x80)
        *(int*)iBuf = iBuf + 0x80;
        int iBoneEnd = iBuf + 0x80 + nBones * 4;
        *(int*)(iBuf + 4) = iBoneEnd;
        *(int*)(iBuf + 0x5C) = iBoneEnd + nJoints * 4;
    }

    *(int*)(iSlot + 0x80) = 0;
    *(int*)(iSlot + 0x84) = *(int*)(iModelDef + 0x20);
    FUN_80063890();

    _restfpr_27();
}

// ============================================================================
// render_instance_destroy
// Address: 0x80061d54
//
// Destroys a render instance: unregisters from light probe system,
// marks slot as free (clears active flag), increments pool free counter,
// calls entity cleanup callback.
// ============================================================================

void render_instance_destroy(int* pInst)
{
    light_probe_unregister_by_entity((int)pInst);
    pInst[1] = 0;        // mark slot as inactive
    pInst[0x6C] = 0;     // clear render flags
    g_instance_pool_free++;
    FUN_8001f92c(pInst);  // entity cleanup callback
}

// ============================================================================
// render_instance_set_material
// Address: 0x80061da0
//
// Changes a render instance's material/animation variant.
// Re-initializes animation properties, resets LOD blend state.
// For mode 1: if the entity has specific animation callbacks
// (FUN_8001d9ec or FUN_8001d73c), triggers animation state notification.
// ============================================================================

void render_instance_set_material(float fParam, int* pInst, int nMaterialIdx)
{
    render_instance_init_anim_props(fParam, pInst + 0x33, (int)(pInst + 0x49), nMaterialIdx);
    pInst[0x30] = *(int*)&SENTINEL_FLOAT; // reset LOD blend
    pInst[0x67] = 0;                       // clear anim state

    if (nMaterialIdx == 1) {
        void* pfnCallback = *(void**)(*pInst + 0x15C);
        if (pfnCallback == FUN_8001d9ec || pfnCallback == FUN_8001d73c) {
            FUN_800708c0(*pInst, 2, 0);
        }
    }
}

// ============================================================================
// render_instance_init_anim_props
// Address: 0x80061e30
//
// Initializes animation properties for a render instance material slot.
// Sets material index, frame count from lookup table (DAT_804d1cd0),
// fills all transform channels (pos XYZ, rot XYZ, scale XYZ) with sentinel
// float values. Also clears secondary state block.
// ============================================================================

void render_instance_init_anim_props(float fDefaultTime, int* pAnimBlock,
                                     int pSecondary, int nMaterialIdx)
{
    float fSentinel = SENTINEL_FLOAT;

    pAnimBlock[0]  = nMaterialIdx;
    pAnimBlock[1]  = (int)(short)DAT_804d1cd0[nMaterialIdx]; // frame count
    pAnimBlock[2]  = *(int*)&fSentinel;
    pAnimBlock[3]  = *(int*)&fSentinel;
    pAnimBlock[0xD] = *(int*)&fDefaultTime; // default time scale
    pAnimBlock[0x15] = 0;                   // anim state flags

    // Clear position XYZ (channels 10-12)
    pAnimBlock[0xA] = *(int*)&fSentinel;
    pAnimBlock[0xB] = *(int*)&fSentinel;
    pAnimBlock[0xC] = *(int*)&fSentinel;

    // Clear rotation XYZ (channels 4-6)
    pAnimBlock[4] = *(int*)&fSentinel;
    pAnimBlock[5] = *(int*)&fSentinel;
    pAnimBlock[6] = *(int*)&fSentinel;

    // Clear scale XYZ (channels 7-9)
    pAnimBlock[7] = *(int*)&fSentinel;
    pAnimBlock[8] = *(int*)&fSentinel;
    pAnimBlock[9] = *(int*)&fSentinel;

    pAnimBlock[0x14] = 0; // anim flags

    // Clear secondary state block
    *(int*)(pSecondary + 0x54) = 0;
    *(float*)(pSecondary + 0x28) = fSentinel;
    *(float*)(pSecondary + 0x2C) = fSentinel;
    *(float*)(pSecondary + 0x30) = fSentinel;
    *(float*)(pSecondary + 0x10) = fSentinel;
    *(float*)(pSecondary + 0x14) = fSentinel;
    *(float*)(pSecondary + 0x18) = fSentinel;
    *(float*)(pSecondary + 0x1C) = fSentinel;
    *(float*)(pSecondary + 0x20) = fSentinel;
    *(float*)(pSecondary + 0x24) = fSentinel;
    *(int*)(pSecondary + 0x50) = 0;
}
