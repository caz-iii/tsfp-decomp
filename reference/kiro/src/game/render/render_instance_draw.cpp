// Address: 0x80061eb8-0x80063128 | Name: render_instance_draw | Date: 2026-08-10
// Render instance drawing pipeline: shadow projection, material dispatch,
// multi-pass rendering, node submission, GX state management.

#include "common.h"
#include "render_entity.h"

// ============================================================================
// Globals
// ============================================================================

static int   g_material_override_active; // DAT_80611b40 - material override flag
static int   g_wireframe_active;         // DAT_80611b44 - wireframe mode flag
static int   g_saved_material_mode;      // DAT_80610444
static int   g_saved_blend_mode;         // DAT_80610448
static int   g_blend_mode_default;       // DAT_80610440
static int*  g_material_table;           // DAT_80610a20 - material slot table
static int   g_active_material_ptr;      // DAT_80612254
static int*  g_current_draw_instance;    // DAT_80611b24
static int   g_current_draw_flags;       // DAT_80611b28
static int   g_viewport_context;         // DAT_80611d54
static int   g_debug_draw_mode;          // DAT_80612614

// ============================================================================
// render_instance_project_shadows
// Address: 0x80061eb8
//
// MASSIVE: Projects shadow/decal geometry for a render instance.
// Iterates sub-objects (stride 0xB4, count at param_3+0x42), for each:
//   - If type==1: simple copy of shadow params
//   - Otherwise: frustum visibility test, screen-space projection,
//     LOD-scaled size computation, light probe color with interpolation,
//     vertex position offsetting, and shadow geometry submission
// Uses paired-single FP registers for efficient vertex math.
// Up to 9 sub-objects processed per instance.
// ============================================================================

void render_instance_project_shadows(int param_1, int param_2, int param_3,
                                     float* pViewMatrix, float* pProjMatrix,
                                     int param_6)
{
    // [Full implementation omitted - 200+ lines of projection math]
    // Key operations:
    // 1. Get viewport count via FUN_801c1714() for LOD scaling
    // 2. Compute distance-based size: FUN_8009c278() / field[900] * lodScale
    // 3. Per sub-object (stride 0xB4, max 9 iterations):
    //    a. Frustum test via FUN_8017cd98()
    //    b. Copy 5 vertex positions with velocity offset (piVar13[0x22..0x24])
    //    c. Compute 4 midpoints with 0.5 interpolation
    //    d. Transform all vertices through view matrix (FUN_8012fb70)
    //    e. Screen-space bounds check (W-clip)
    //    f. Project to normalized device coordinates
    //    g. If sub-object type 0 or 3: compute light direction dot product,
    //       evaluate shadow intensity, query light probe color, submit decal
    //    h. If sub-object type 0 (non-blob): compute size from distance,
    //       submit shadow quad via FUN_8017cfc8
    //    i. If sub-object type 3 (blob): transform to bone space,
    //       offset by entity position, submit blob shadow via FUN_80103f20
}

// ============================================================================
// render_state_reset_overrides
// Address: 0x800626a8
//
// Restores material mode and blend mode to saved values, clears override flag.
// ============================================================================

void render_state_reset_overrides(int nBlendMode, int nMaterialMode)
{
    FUN_8016efe8(nMaterialMode); // restore material mode
    FUN_8016f00c(nBlendMode);    // restore blend mode
    g_material_override_active = 0;
}

// ============================================================================
// render_state_enable_wireframe
// Address: 0x800626e8
//
// Enables wireframe rendering mode (blend mode 4).
// ============================================================================

void render_state_enable_wireframe(void)
{
    FUN_8016e8ac(4); // set blend mode to wireframe
    g_wireframe_active = 1;
}

// ============================================================================
// render_state_set_blend_mode
// Address: 0x80062714
//
// Sets render blend mode and clears wireframe flag.
// ============================================================================

void render_state_set_blend_mode(int nMode)
{
    FUN_8016e8ac(nMode);
    g_wireframe_active = 0;
}

// ============================================================================
// render_instance_apply_material
// Address: 0x8006273c
//
// Material/effect dispatch for a render instance. Evaluates animated property
// to select material slot from table (stride 0x58). Applies visual effects:
//   0x0080 = environment map (material mode 0xE)
//   0x0800 = refraction (material mode 2)
//   0x0400 = reflection (material mode 0xD)
//   0x1000 = glow (material mode 0x15, alpha offset -6)
//   0x10000 = wireframe toggle
// Falls back to default material if override inactive.
// ============================================================================

void render_instance_apply_material(int param_1, int param_2, int param_3)
{
    _savefpr_25();

    int* pProp = NULL;
    int iMaterialSlot = -1;

    // Evaluate animated property to get material index
    long long lVal = FUN_803826f0();
    if (lVal >= 0) {
        pProp = (int*)((int)lVal + (int)(lVal >> 32) * 0x30);
        iMaterialSlot = render_prop_get_value(pProp);
    }

    // Resolve material slot to table entry
    if (iMaterialSlot >= 0) {
        iMaterialSlot = *(int*)(param_3 + iMaterialSlot * 0x10);
    }

    // Check effect flags
    if (pProp != NULL) {
        u32 uFlags = pProp[7];
        if ((uFlags & 0x1C80) != 0) {
            // Apply effect based on priority
            if ((uFlags & 0x80) != 0) {
                FUN_8016f00c(0xE); // env map
            } else if ((uFlags & 0x800) != 0) {
                FUN_8016f00c(2);   // refraction
            } else if ((uFlags & 0x400) != 0) {
                FUN_8016f00c(0xD); // reflection
            } else if ((uFlags & 0x1000) != 0) {
                FUN_8016efe8(0xFF);
                FUN_8016f00c(0x15); // glow
                gx_bind_material_by_index(-6);   // alpha offset
            }

            // Resolve material table entry (stride 0x58)
            if (iMaterialSlot >= 0 && iMaterialSlot < g_material_table[3] &&
                iMaterialSlot > g_material_table[2]) {
                g_active_material_ptr = g_material_table[0] + iMaterialSlot * 0x58;
            } else {
                gx_bind_material_fallback(); // fallback material
            }

            g_material_override_active = 1;
            goto check_wireframe;
        }
    }

    // No effect flags - reset override if active
    if (g_material_override_active != 0) {
        render_state_reset_overrides(g_saved_blend_mode, g_saved_material_mode);
    }

    // Resolve material slot
    if (iMaterialSlot >= 0 && iMaterialSlot < g_material_table[3] &&
        iMaterialSlot > g_material_table[2]) {
        g_active_material_ptr = g_material_table[0] + iMaterialSlot * 0x58;
    } else {
        gx_bind_material_fallback();
    }

check_wireframe:
    // Wireframe toggle (flag 0x10000)
    if (pProp == NULL || (pProp[7] & 0x10000) == 0) {
        if (g_wireframe_active != 0) {
            render_state_set_blend_mode(g_blend_mode_default);
        }
    } else {
        render_state_enable_wireframe();
    }

    _restfpr_25();
}

// ============================================================================
// render_instance_submit_node
// Address: 0x800629bc
//
// RECURSIVE: Submits a model hierarchy node to the GX rendering pipeline.
// - Saves current GX cull mode
// - Checks entity flags for backface culling override (flag 0x20000000)
// - Validates node index bounds
// - Calls mesh submission (FUN_80183b18) with visibility test
// - If visible and LOD mask passes: calls FUN_80182934 for actual draw
// - Recurses into child nodes (index at +0x3F, unless type==3)
// - Recurses into sibling nodes (index at +0x40)
// - Restores cull mode
// ============================================================================

void render_instance_submit_node(int* pInstance, int nNodeIndex,
                                 int nPassIndex, u32 nBufferIndex)
{
    _savefpr_26();

    int* pModelData = (int*)pInstance[1];
    int nBaseNode = *pModelData;

    // Save current cull mode
    u32 savedCull[1];
    GXGetCullMode(savedCull);

    // Override cull mode if entity has flag 0x20000000
    if ((*(u32*)(*pInstance + 0x28) & 0x20000000) == 0) {
        GXSetCullMode(savedCull[0]);
    } else {
        GXSetCullMode(2); // cull front
    }

    // Validate node index
    if (nNodeIndex >= 0 && nNodeIndex < *pModelData) {
        // Visibility test + mesh submission
        int iResult = FUN_80183b18(
            pModelData[nBaseNode * -0x30 + nNodeIndex * 0x30 + 0x2F],
            pInstance, nPassIndex, nBufferIndex);

        if (iResult != 0) {
            // Check LOD visibility mask
            if (pModelData[nBaseNode * -0x30 + nNodeIndex * 0x30 + 0x14] == 0 ||
                (pInstance[0xC] & pModelData[nBaseNode * -0x30 + nNodeIndex * 0x30 + 0x14]) != 0) {
                FUN_80182934(pInstance, nNodeIndex, nPassIndex, nBufferIndex);
            }

            // Recurse into child (index at +0x3F in node data, stride 0xC0)
            char cChild = *(char*)((char*)pModelData + nNodeIndex * 0xC0 + nBaseNode * -0xC0 + 0x3F);
            if (cChild >= 0 &&
                *(char*)(pModelData + nBaseNode * -0x30 + nNodeIndex * 0x30 + 0xF) != 3) {
                render_instance_submit_node(pInstance, (int)cChild, nPassIndex, nBufferIndex);
            }

            // Recurse into sibling (index at +0x40)
            if (*(char*)(pModelData + nBaseNode * -0x30 + nNodeIndex * 0x30 + 0x10) >= 0) {
                render_instance_submit_node(
                    pInstance,
                    (int)*(char*)(pModelData + nBaseNode * -0x30 + nNodeIndex * 0x30 + 0x10),
                    nPassIndex, nBufferIndex);
            }

            // Restore cull mode
            GXSetCullMode(savedCull[0]);
        }
    }

    _restfpr_26();
}

// ============================================================================
// render_instance_draw_multipass
// Address: 0x80062af0
//
// Multi-pass render dispatcher. Renders entity in 3 passes:
//   Pass 0: Opaque geometry (material mode 0)
//   Pass 2: Transparent geometry (blend mode 5)
//   Pass 1: Overlay/additive (blend mode 2)
// ============================================================================

void render_instance_draw_multipass(int* pInstance, float fParam, int nParam)
{
    FUN_8016f00c(0);    // material mode: opaque
    render_instance_draw_pass(pInstance, fParam, nParam, 0, 0);

    FUN_8016f00c(1);    // material mode: transparent setup
    FUN_8016e8ac(5);    // blend mode: alpha blend
    render_instance_draw_pass(pInstance, fParam, nParam, 2, 0);

    FUN_8016e8ac(2);    // blend mode: additive
    render_instance_draw_pass(pInstance, fParam, nParam, 1, 0);
}

// ============================================================================
// render_instance_draw
// Address: 0x80062b68
//
// MASSIVE: Main render instance draw entry point. Full GX state setup:
// 1. Per-viewport visibility (bit mask by viewport index from g_viewport_context)
// 2. If entity has flag 0x200000: check per-viewport visibility bits
// 3. Material alpha/blend overrides from instance visual state (+0x1EE flags)
// 4. Bone transform matrix upload: GXLoadPosMtxImm (nBones * 0x40 stride)
// 5. Hardware lighting matrix setup (GXLoadNrmMtx)
// 6. Atmosphere/fog disable for flag 0x20000/0x40000 entities
// 7. Submit all mesh nodes via render_instance_submit_node (root node 0)
//    - Standard: single pass with current pass index
//    - Type-specific: 3 passes (0,2,1) for certain material types
// 8. Reset hardware state (matrices, fog, effects)
// 9. Handle attached entity at +0x208 (recursive draw)
// 10. Debug draw mode (DAT_80612614==100): trigger debug visualization
// ============================================================================

void render_instance_draw(int* pInstance, float fParam, int nParam,
                          int nPassIndex, u32 nBufferIndex)
{
    _savefpr_24();

    int iEntityDef = *pInstance;

    // Per-viewport visibility
    if ((*(u32*)(iEntityDef + 0x28) & 0x200000) == 0) {
        *(u32*)(iEntityDef + 0xC) |= (1 << *(int*)(g_viewport_context + 0x214));
    } else {
        // Check if this viewport should see the entity
        if (((u16)*(u16*)(iEntityDef + 0x14) & (1 << *(int*)(g_viewport_context + 0x214))) == 0) {
            FUN_8007666c(); // visibility callback
        }
        *(u32*)(iEntityDef + 0xC) |= (1 << *(int*)(g_viewport_context + 0x214));

        if ((*(u32*)(iEntityDef + 0x28) & 0x100) != 0) {
            goto done; // hidden - skip draw
        }

        // ... [extensive state setup: alpha, material, bones, fog]
    }

    // Upload bone transforms
    if (*(int*)(pInstance[1] + 8) != 0) {
        for (int i = 0; i < *(short*)(pInstance + 0x1D); i++) {
            GXLoadPosMtxImm(pInstance[0x1C] + i * 0x40, 1 << i);
        }
        u32 nrmMtxCount = pInstance[0x1B];
        GXLoadNrmMtx(4, &nrmMtxCount);
    }

    // Atmosphere/fog override
    if (nPassIndex == 0) {
        if ((*(u32*)(*(int*)(iEntityDef + 0x24) + 0x28) & 0x20000) != 0 ||
            (*(u32*)(iEntityDef + 0x28) & 0x40000) != 0) {
            GXSetFog(0); // disable fog
        }
    }

    // Submit bone matrices to GX
    FUN_8039955c(pInstance[nBufferIndex + 8], *(int*)(pInstance[1] + 4) << 6);
    FUN_8016e694(0x15, pInstance[nBufferIndex + 8], 0x40);

    // Debug draw mode
    if (g_debug_draw_mode == 100) {
        FUN_8018fac4(7);
        g_current_draw_instance = pInstance;
    }

    // Custom shader effect
    if (*(short*)((char*)pInstance + 0x76) != 0) {
        g_current_draw_flags = 0;
        g_current_draw_instance = pInstance;
        FUN_8018fac4(*(short*)((char*)pInstance + 0x76));
    }

    // Submit mesh nodes
    char cMaterial = *(char*)(pInstance + 0x7C);
    if (cMaterial == -1 || cMaterial == 0 || cMaterial == 0x0E) {
        // Standard: submit with current pass
        if ((*(u32*)(pInstance[1] + 0xC) & (1 << nPassIndex)) != 0) {
            render_instance_submit_node(pInstance, 0, nPassIndex, nBufferIndex);
        }
    } else if (nBufferIndex == 0) {
        // Multi-pass for special materials (mirrored entity pass 1)
        if (nPassIndex == 1) {
            render_instance_submit_node(pInstance, 0, 0, 0);
            render_instance_submit_node(pInstance, 0, 2, 0);
            render_instance_submit_node(pInstance, 0, 1, 0);
        }
    } else if (nPassIndex == 2) {
        // Multi-pass for buffer index > 0
        render_instance_submit_node(pInstance, 0, 0, nBufferIndex);
        render_instance_submit_node(pInstance, 0, 2, nBufferIndex);
        render_instance_submit_node(pInstance, 0, 1, nBufferIndex);
    }

    // Reset state
    gx_set_indirect_blend_mode(0);
    GXSetAlphaCompare(0);

    if (*(int*)(pInstance[1] + 8) != 0) {
        FUN_8016e444(0, 0);
    }

    // Restore fog
    if (nPassIndex == 0) {
        if ((*(u32*)(*(int*)(iEntityDef + 0x24) + 0x28) & 0x20000) != 0 ||
            (*(u32*)(iEntityDef + 0x28) & 0x40000) != 0) {
            GXSetFog(/* restore */);
        }
    }

    // Clear debug state
    if (*(short*)((char*)pInstance + 0x76) != 0 || g_debug_draw_mode == 100) {
        g_current_draw_instance = NULL;
        g_current_draw_flags = 0;
        FUN_8018fb38();
    }

    // Post-draw effects for pass 1
    if (nPassIndex == 1) {
        FUN_80102508(); // shadow overlay
        FUN_801039bc(); // reflection pass
    }

    // Restore material/blend
    // ... (saved state restoration)

done:
    _restfpr_24();
}

// ============================================================================
// render_instance_draw_pass
// Address: 0x80062f8c
//
// Handles a single render pass with LOD model swapping.
// For type-8 (mirrored) entities: swaps to secondary bone buffer when the
// current viewport bit is set in the mirror visibility mask (+0xA0).
// Checks squared distance for LOD threshold - if exceeded, swaps to LOD model.
// Calls render_instance_draw for main entity and any attached entity at +0x208.
// Restores original buffer and LOD state after draw.
// ============================================================================

void render_instance_draw_pass(int* pInstance, float fParam, int nParam,
                               int nPassIndex, u32 nBufferIndex)
{
    _savefpr_27();

    int iEntityDef = *pInstance;
    int iModelDesc = *(int*)(iEntityDef + 0x24);

    // Type 8: mirrored entity buffer swap
    if (*(int*)(iEntityDef + 0x20) == 8) {
        if (pInstance[2] != 0 &&
            (*(u32*)(iEntityDef + 0xA0) & (1 << *(int*)(g_viewport_context + 0x214))) != 0) {
            pInstance[1] = pInstance[2]; // swap to mirror buffer
            pInstance[4] = pInstance[5];
        }
    }

    // LOD distance check
    if (*(int*)(iModelDesc + 8) != 0) {
        float fDeltaY = -0.5f * (*(float*)(iEntityDef + 0x68) - *(float*)(g_viewport_context + 0x280));
        float fDeltaX = *(float*)(iEntityDef + 0x64) - *(float*)(g_viewport_context + 0x27C);
        float fDeltaZ = *(float*)(iEntityDef + 0x6C) - *(float*)(g_viewport_context + 0x284);
        float fDistSq = fDeltaZ * fDeltaZ + fDeltaX * fDeltaX + fDeltaY * fDeltaY;

        // If beyond LOD threshold, swap model pointers
        if (fDistSq > *(float*)(iModelDesc + 0x18)) {
            int iBackup = *(int*)(*(int*)(iEntityDef + 4) + 0xC);
            *(int*)(*(int*)(iEntityDef + 4) + 0xC) = *(int*)(*(int*)(iEntityDef + 4) + 4);
            *(int*)(*(int*)(iEntityDef + 4) + 4) = *(int*)(iModelDesc + 8);
        }
    }

    // Draw the instance
    render_instance_draw(pInstance, fParam, nParam, nPassIndex, nBufferIndex);

    // Draw attached entity at +0x208 (e.g. weapon, accessory)
    int iAttached = *(int*)(*pInstance + 0x208);
    if (iAttached != 0 && (*(u32*)(iAttached + 0x28) & 0x100) == 0) {
        render_instance_draw(*(int**)(iAttached + 4), fParam, nParam, nPassIndex, nBufferIndex);
    }

    // Restore type 8 buffer
    if (*(int*)(*pInstance + 0x20) == 8) {
        if (pInstance[2] != 0 &&
            (*(u32*)(*pInstance + 0xA0) & (1 << *(int*)(g_viewport_context + 0x214))) != 0) {
            pInstance[1] = pInstance[3]; // restore original
            pInstance[4] = pInstance[6];
        }
    }

    // Restore LOD model pointers
    if (*(int*)(iModelDesc + 8) != 0) {
        int iBackup = *(int*)(*(int*)(iEntityDef + 4) + 0xC);
        if (iBackup != 0) {
            *(int*)(*(int*)(iEntityDef + 4) + 4) = iBackup;
            *(int*)(*(int*)(iEntityDef + 4) + 0xC) = 0;
        }
    }

    _restfpr_27();
}

// ============================================================================
// render_node_count_visible
// Address: 0x80063128
//
// Counts visible nodes between two node data offsets (stride 0xC0).
// A node is considered visible if the byte at +0x3C == 0.
// Walks from param_2 to param_1, incrementing count for each visible node.
// ============================================================================

int render_node_count_visible(int pEndNode, int pStartNode)
{
    int nCount = 0;

    while (pStartNode != pEndNode) {
        if (*(char*)(pStartNode + 0x3C) == 0) {
            nCount++;
        }
        pStartNode += 0xC0; // next node
    }

    return nCount;
}
