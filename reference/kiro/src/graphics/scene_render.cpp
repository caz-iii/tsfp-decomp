// Address: 0x80057a08 - 0x80058b84 | Batch 117 | Date: 2026-08-11
// Scene rendering pipeline: init, shutdown, viewport clipping, layer rendering,
// render dispatch, visibility determination, and per-frame node list update.
// Part of the scene system (follows scene_setup.cpp)

#include "common.h"
#include "scene.h"
#include "memory.h"

// ============================================================================
// Address: 0x80057a08 | scene_system_init
// Top-level scene system initialization entry point.
// 1. Zeroes all master scene globals (nodes, materials, vbufs, lighting, etc.)
// 2. Resets world AABB to extreme values (min=FLT_MAX, max=FLT_MIN)
// 3. Dispatches based on level ID:
//    - Level 100 (mapmaker): asserts arena, calls scene_system_build_all_blocks
//    - Regular levels (<0x65): formats path, calls scene_system_load_level
// 4. Calls scene_block_pool_init (LRU streaming pool)
// 5. Calls scene_room_system_init (5MB buffer + state)
// 6. Calls scene_build_spatial_tree (KD-tree depth 6x6)
// 7. Calls FUN_800590b8 (unknown post-init)
// 8. Calls scene_level_load_finalize (entities + visibility)
// ============================================================================
void scene_system_init(void) {
    DAT_80611a9c = 0;
    DAT_80611aa8 = 0;  // sub-node array
    DAT_80611aac = 0;  // vertex buffer array
    DAT_80611ab0 = 0;
    DAT_80611e78 = 0;  // per-node lighting
    DAT_80611abc = 0;
    DAT_80611afc = 0;  // vertex colors flag

    FUN_801c2e40(); // camera system pre-init

    // Reset world AABB to extremes
    DAT_8052929c = FLT_MAX;  // world_min.x
    DAT_805292a0 = FLT_MAX;  // world_min.y
    DAT_805292a4 = FLT_MAX;  // world_min.z
    DAT_805292a8 = -FLT_MAX; // world_max.x
    DAT_805292ac = -FLT_MAX; // world_max.y
    DAT_805292b0 = -FLT_MAX; // world_max.z

    if (DAT_80612614 == 100) {
        // Mapmaker: build from tile chunks
        int arena = heap_arena_lookup();
        if (arena != 0) {
            halt_stub_assert(); // Must not have active arena
        }
        scene_system_build_all_blocks();
    } else if (DAT_80612614 < 0x65) {
        // Regular gameplay level
        char path[72];
        format_path(path, "assets/ts/models/level%d/level%d",
                    DAT_80612614, DAT_80612614, DAT_80612614);
        scene_system_load_level(path);
    }

    scene_block_pool_init();
    scene_room_system_init();
    scene_build_spatial_tree();
    FUN_800590b8();
    scene_level_load_finalize();
}

// ============================================================================
// Address: 0x80057b34 | scene_system_shutdown
// Scene teardown. Zeroes all master arrays and counts. Frees vertex color
// texture (DAT_8061040c) if vertex colors were active. Only runs if not in
// streaming lock (DAT_80611568 == 0). Clears 0x20 bytes at DAT_80611a98+0x??
// ============================================================================
void scene_system_shutdown(void) {
    if (DAT_80611568 != 0) return;

    DAT_80611aa8 = 0;  // sub-node array
    DAT_80611b18 = 0;  // render flags
    DAT_80611a98 = 0;  // material render state
    DAT_80611b04 = 0;  // visibility flags
    DAT_80611b10 = 0;  // vertex buffer count
    DAT_80611b14 = 0;  // sub-node count

    if (DAT_80611afc != 0 && DAT_8061040c != -1) {
        FUN_80187260(DAT_8061040c); // Free vertex color texture
        DAT_8061040c = -1;
    }

    memset(g_scene_misc_state, 0, 0x20);
}

// ============================================================================
// Address: 0x80057bc8 | scene_viewport_compute_clip_bounds
// Computes 2D clip bounds (min_x, min_z, max_x, max_z) for a viewport entity.
// For invalid slot indices (<1 or >200): uses global viewport from DAT_80611748.
// For valid indices: iterates portal entries from DAT_80611d54 (stride 0xC at
// +0x3BC), finds min/max of portal X/Z extents (short values at +0x3C0..+0x3C6).
// Clamps result against global viewport bounds.
// Writes results to entity+0x118, +0x11C, +0x120, +0x124.
// ============================================================================
void scene_viewport_compute_clip_bounds(int entity) {
    if (*(int*)(entity + 0x34) < 1 || *(int*)(entity + 0x34) > 200) {
        // Invalid slot: use global viewport bounds
        *(float*)(entity + 0x118) = (float)(*DAT_80611748 ^ 0x80000000);
        *(float*)(entity + 0x11C) = (float)(DAT_80611748[5] ^ 0x80000000);
        *(float*)(entity + 0x120) = (float)(DAT_80611748[1] ^ 0x80000000);
        *(float*)(entity + 0x124) = (float)(DAT_80611748[6] ^ 0x80000000);
    } else {
        // Valid slot: compute from portal entries
        *(float*)(entity + 0x118) = FLOAT_80613e04; // init min_x = large
        *(float*)(entity + 0x11C) = FLOAT_80613e04; // init min_z = large
        *(float*)(entity + 0x120) = FLOAT_80613dac; // init max_x = small
        *(float*)(entity + 0x124) = FLOAT_80613dac; // init max_z = small

        int portal_count = *(s8*)(entity + 0x30);
        for (int i = 0; i < portal_count; i++) {
            int* portal = (int*)(DAT_80611d54 +
                          *(int*)(entity + i * 4 + 0x38) * 0xC + 0x3BC);
            if (*portal >= 0) {
                // Update min/max from portal short extents
                // ... (min_x, min_z, max_x, max_z from s16 fields)
            }
        }
        // Clamp against global viewport bounds
        // ...
    }
}

// ============================================================================
// Address: 0x80057f94 | scene_render_layer_forward
// Renders scene nodes for a depth layer (forward/single pass).
// Iterates nodes in the layer's visibility group (DAT_805293c8 count,
// DAT_80529418 index array, stride 200 per layer).
// Per node: frustum-tests AABB (from DAT_80611d54 + node*0xC + 0x3C0..0x3C6)
// against viewport bounds (DAT_80611748). If visible:
//   - Sets GX blend mode 0 (opaque front), draw mode 4
//   - Calls FUN_80077708 (geometry submit)
//   - Increments DAT_8061158c if first-person mode
//   - Sets viewport clip rect
// ============================================================================
void scene_render_layer_forward(void) {
    int layer = FUN_803826ec(); // pop layer index
    u8 node_count = g_layer_node_counts[layer];
    FUN_803c766c(1); // enable depth write

    for (int i = 0; i < (int)node_count; i++) {
        u32 node_idx = (u32)g_layer_node_indices[i + layer * 200];
        // Frustum test: check node AABB against viewport
        s16 min_x = *(s16*)(DAT_80611d54 + node_idx * 0xC + 0x3C0);
        s16 max_x = *(s16*)(DAT_80611d54 + node_idx * 0xC + 0x3C4);
        s16 min_z = *(s16*)(DAT_80611d54 + node_idx * 0xC + 0x3C2);
        s16 max_z = *(s16*)(DAT_80611d54 + node_idx * 0xC + 0x3C6);

        if (*DAT_80611748 <= max_x && min_x < DAT_80611748[1] &&
            min_x < max_x &&
            DAT_80611748[5] <= max_z && min_z < DAT_80611748[6] &&
            min_z < max_z) {
            if (*(int*)(DAT_80611d54 + 0x214) == 0) {
                DAT_8061158c++; // draw call counter
            }
            FUN_8016f00c(0);   // blend mode: opaque front
            FUN_8016e8ac(4);   // draw mode: solid
            FUN_8007705c();    // pre-draw setup
            FUN_8016f00c(0);
            FUN_8016e8ac(4);
            FUN_80077708();    // submit geometry
            FUN_8016f0e4(*DAT_80611748, DAT_80611748[5],
                         DAT_80611748[1], DAT_80611748[6]); // set clip rect
        }
    }
}

// ============================================================================
// Address: 0x80058154 | scene_render_layer_multipass
// Renders scene nodes for a depth layer with multiple passes:
//   Pass 1: Depth pre-pass (blend=1, mode=5)
//   Pass 2: Color pass (blend=1, mode=2) + clip rect
//   Pass 3: Overlay pass (blend=0, mode=0xB) + clip rect
//   Each pass calls FUN_800eac84 (GX draw done / pipe flush)
// Saves/restores depth-sort state via FUN_8016edac.
// Same frustum test as scene_render_layer_forward.
// ============================================================================
void scene_render_layer_multipass(void) {
    int layer = FUN_803826ec();
    u8 node_count = g_layer_node_counts[layer];
    DAT_80611ac0 = FUN_8016edac(1); // save depth sort state
    FUN_803c766c(0); // disable depth write

    for (int i = 0; i < (int)node_count; i++) {
        u32 node_idx = (u32)g_layer_node_indices[i + layer * 200];
        // Same frustum test as forward pass...
        if (/* node is visible */) {
            // Pass 1: depth pre-pass
            FUN_8016f00c(1);
            FUN_8016e8ac(5);
            FUN_80077708();
            // Pass 2: color
            FUN_8016edac(1);
            FUN_8016e8ac(2);
            FUN_80077708();
            FUN_8016f0e4(/* viewport clip */);
            // Pass 3: overlay
            FUN_8016edac(0);
            FUN_8016f0e4(/* viewport clip */);
            FUN_8016e8ac(0xB);
            FUN_8016f00c(1);
            FUN_80077708();
            FUN_8016f0e4(/* viewport clip */);
            // Flush
            FUN_800eac84();
            FUN_800eac84();
            FUN_800eac84();
            FUN_8016edac(DAT_80611ac0);
        }
    }
    FUN_8016edac(DAT_80611ac0);
}

// ============================================================================
// Address: 0x800583a8 | scene_render_dispatch
// Main scene render dispatch. Orchestrates layer rendering order:
// 1. Allocates deferred transparent object list (16 entries, growable to 0x10)
// 2. Saves depth-sort state, enables depth write
// 3. Renders forward layers ABOVE camera (layer > current)
// 4. Renders multipass layers BELOW camera (layer < current, reverse order)
// 5. Sets viewport clip, calls FUN_800fedfc
// 6. Renders forward layers AT/BELOW camera
// 7. If debug flags (0x20000000 | 0x200): renders debug wireframe (pass 0)
// 8. Switches to transparency mode, renders multipass layers downward
// 9. If debug flags: renders debug wireframe (pass 1)
// 10. Processes deferred transparent objects (FUN_800774dc with modes 0,2,1)
// 11. Frees deferred list, restores state
// ============================================================================
void scene_render_dispatch(void) {
    int current_layer = FUN_803826f0(); // current camera layer
    DAT_80611c3c = 0x10; // max deferred entries
    DAT_80611c40 = 0;    // current deferred count
    DAT_80611c44 = mem_alloc(0x40, 0); // 16 * 4 bytes

    DAT_80611ac0 = FUN_8016edac(1);
    FUN_803c766c(1); // enable depth write

    // Forward pass: layers above camera
    int layer = current_layer + 1;
    while (layer <= g_total_layers) {
        scene_render_layer_forward(layer);
        layer++;
    }

    // Multipass: layers below camera (reverse)
    for (layer = g_total_layers; current_layer < layer; layer--) {
        scene_render_layer_multipass(layer);
    }

    FUN_8016f0e4(/* viewport clip */);
    FUN_800fedfc(); // mid-frame processing

    // Forward pass: layers at/below camera
    for (layer = 0; layer <= current_layer; layer++) {
        scene_render_layer_forward(layer);
    }

    // Debug wireframe (if flags set)
    if ((DAT_805015c0 & 0x20000000) || (DAT_805015c0 & 0x200)) {
        FUN_8016f00c(1);
        FUN_8016e8ac(10);
        FUN_8006b530(0); // debug render pass 0
    }

    // Transparency passes
    FUN_803c766c(0);
    FUN_801c2c48(2);
    FUN_8016edac(0);
    FUN_801c2c48(1);
    FUN_8016edac(DAT_80611ac0);

    for (layer = current_layer; layer >= 0; layer--) {
        scene_render_layer_multipass(layer);
    }

    // Debug wireframe pass 1
    if ((DAT_805015c0 & 0x20000000) || (DAT_805015c0 & 0x200)) {
        FUN_8016f00c(1);
        FUN_8016e8ac(2);
        FUN_8006b530(1); // debug render pass 1
    }

    // Process deferred transparent objects
    if (DAT_80611c40 != 0) {
        FUN_8016edac(DAT_80611ac0);
        for (int i = DAT_80611c40 - 1; i >= 0; i--) {
            FUN_800774dc(*(int*)(DAT_80611c44 + i * 4), 0);
            FUN_800774dc(*(int*)(DAT_80611c44 + i * 4), 2);
        }
        FUN_8016edac(0);
        for (int i = DAT_80611c40 - 1; i >= 0; i--) {
            FUN_800774dc(*(int*)(DAT_80611c44 + i * 4), 1);
            int obj = *(int*)(DAT_80611c44 + i * 4);
            *(u32*)(obj + 0x10) &= ~0x04000000; // clear deferred flag
        }
    }

    DAT_80611c40 = 0;
    mem_free_top(DAT_80611c44);
    DAT_80611c44 = 0;
    FUN_8016edac(DAT_80611ac0);
}

// ============================================================================
// Address: 0x80058678 | scene_render_full_frame
// Renders the complete scene geometry in 4 GX render passes:
//   Pass 1: blend=0, mode=4 (opaque front faces)
//   Pass 2: blend=1, mode=5 (opaque back faces / depth)
//   Pass 3: blend=1, mode=0xD (special/decal)
//   Pass 4: blend=0→1, mode=2 (transparent/alpha)
// Each pass calls FUN_80077708 (geometry submission).
// Saves/restores depth-sort state.
// ============================================================================
void scene_render_full_frame(void) {
    DAT_80611ac0 = FUN_8016edac(1);
    FUN_8016f00c(0);   // blend off
    FUN_8016e8ac(4);   // mode: opaque front
    FUN_80077708();    // submit
    FUN_8016f00c(1);   // blend on
    FUN_8016e8ac(5);   // mode: opaque back
    FUN_80077708();    // submit
    FUN_8016e8ac(0xD); // mode: decal/special
    FUN_80077708();    // submit
    FUN_8016edac(0);   // depth sort off
    FUN_8016f00c(1);   // blend on
    FUN_8016e8ac(2);   // mode: transparent
    FUN_80077708();    // submit
    FUN_8016edac(DAT_80611ac0);
}

// ============================================================================
// Address: 0x80058718 | scene_render_pipeline
// Scene render pipeline orchestrator. Full per-viewport render sequence:
// 1. Sets GX mode 0xC, blend off
// 2. Saves depth-sort state
// 3. Handles fog rendering (FUN_800f325c/FUN_800f336c) if not disabled
// 4. If DAT_80611d54+0xE04 != 0: submits fog geometry
// 5. Calls FUN_800f190c (atmospheric effects)
// 6. Sets up viewport (FUN_801c2c48 mode 0)
// 7. If NO debug flags: calls scene_render_full_frame
// 8. Calls scene_render_dispatch (main layer rendering)
// 9. If debug flags: calls scene_render_full_frame after dispatch
// 10. Calls FUN_8007b350 (post-geometry, likely shadow/decal)
// 11. If flag 0x40: calls FUN_8022d964 (water rendering)
// 12. Sets final viewport clip, enables blend, calls FUN_800eaf34
// ============================================================================
void scene_render_pipeline(void) {
    int prev_mode = FUN_8016e8ac(0xC);
    FUN_8016f00c(0);
    DAT_80611ac0 = FUN_8016edac(1);

    if ((*(u32*)(DAT_80611d54 + 0x21C) & 0x8000) == 0) {
        FUN_800f325c(); // fog setup
        if (*(int*)(DAT_80611d54 + 0xE04) != 0) {
            FUN_80077708(); // submit fog geometry
        }
        FUN_8016f00c(1);
        FUN_8016e8ac(0xC);
        FUN_8016edac(0);
        if (*(int*)(DAT_80611d54 + 0xE04) != 0) {
            FUN_80077708();
        }
        FUN_800f336c(); // fog post
        FUN_800f32a0(); // fog cleanup
    }

    FUN_800f190c(); // atmospheric effects
    FUN_8016edac(DAT_80611ac0);
    FUN_8016e8ac(4);
    FUN_801c2c48(0); // viewport setup

    if (!(DAT_805015c0 & 0x20000000) && !(DAT_805015c0 & 0x200)) {
        scene_render_full_frame();
    }

    scene_render_dispatch();

    if ((DAT_805015c0 & 0x20000000) || (DAT_805015c0 & 0x200)) {
        scene_render_full_frame();
    }

    FUN_8007b350(); // post-geometry (shadows/decals)

    if (DAT_805015c0 & 0x40) {
        FUN_8022d964(); // water rendering
    }

    FUN_8016f0e4(/* viewport clip */);
    FUN_8016f00c(1);
    FUN_800eaf34(); // final post-process
    FUN_8016e8ac(prev_mode); // restore mode
}

// ============================================================================
// Address: 0x800588cc | scene_visibility_and_render
// Scene visibility determination + render entry point.
// 1. Assigns sub-nodes to depth layers by portal connectivity:
//    - Normal nodes (no flag 0x8): added to layer's standard list (max 200)
//    - Transparent nodes (flag 0x8): added to separate list (max 10 per layer)
// 2. Appends transparent layers after standard ones
// 3. Clamps max layers to 0x4F (79)
// 4. Resets draw call counter (DAT_8061158c) if first-person
// 5. Sets viewport clip rect
// 6. Optionally adds exterior node to last layer (flag 0x4 + conditions)
// 7. Calls FUN_80174564 (visibility pre-pass)
// 8. Calls FUN_8007705c (frame setup)
// 9. Enters GPU performance timing (FUN_803578bc/FUN_80357980)
// 10. Calls scene_render_pipeline
// 11. Restores layer count
// ============================================================================
void scene_visibility_and_render(void) {
    FUN_803826f0(); // pop params
    int original_layer_count = *(int*)(DAT_80611d54 + 0xD28);
    DAT_80611b20 = 0;

    memset(g_layer_node_counts, 0, 0x50);
    memset(g_transparent_layer_counts, 0, 0x50);

    // Assign nodes to layers
    for (u32 node = 1; node < (u32)(DAT_80611b14 + 1); node++) {
        int layer_idx = *(int*)(DAT_80611d54 + node * 0xC + 0x3BC);
        if (layer_idx >= 0) {
            if ((*(u16*)(DAT_80611aa8 + node * 0xB4 + 0xB2) & 8) == 0) {
                // Standard node
                u8 count = g_layer_node_counts[layer_idx];
                g_layer_node_counts[layer_idx] = count + 1;
                g_layer_node_indices[layer_idx * 200 + count] = (u8)node;
            } else {
                // Transparent node (separate list, max 10)
                if (g_transparent_layer_counts[layer_idx] < 10) {
                    u8 count = g_transparent_layer_counts[layer_idx];
                    g_transparent_layer_counts[layer_idx] = count + 1;
                    g_transparent_node_indices[layer_idx * 10 + count] = (u8)node;
                }
            }
        }
    }

    // Append transparent layers
    // ... (merge transparent into standard layer list)

    // Clamp max layers
    if (*(int*)(DAT_80611d54 + 0xD28) > 0x4F) {
        *(int*)(DAT_80611d54 + 0xD28) = 0x4F;
    }

    if (*(int*)(DAT_80611d54 + 0x214) == 0) {
        DAT_8061158c = 0; // reset draw call counter
    }

    FUN_8016f0e4(/* viewport clip */);

    // Call render pipeline
    FUN_80174564(g_layer_node_counts, layer_count_total, g_layer_node_indices);
    FUN_8007705c(); // frame setup
    FUN_803578bc(); // GPU perf begin
    scene_render_pipeline();
    FUN_80357980(); // GPU perf end

    *(int*)(DAT_80611d54 + 0xD28) = original_layer_count;
}

// ============================================================================
// Address: 0x80058b84 | scene_update_visible_nodes
// Per-frame visible node list rebuild for all active viewports.
// Skips first frame (DAT_80611a9c transitions from 0→1).
// Per viewport (DAT_80611d58 viewports, stride 0xFFC at DAT_80611d5c):
//   - Only processes viewports with flag 0x20 at +0x21C
//   - Clears node counts, rebuilds layer→node assignment
//   - For each node: checks if portal connectivity reaches it (layer index >=0)
//   - Frustum-tests node AABB (min_x < max_x, within viewport X/Z)
//   - Adds passing nodes to visible list at +0xE0C (max 0xD5 = 213 entries)
//   - Then adds neighbor nodes via FUN_800598c4 (up to 0x1E = 30 neighbors)
//   - Deduplicates neighbor entries before adding
// ============================================================================
void scene_update_visible_nodes(void) {
    if (DAT_80611a9c == 0) {
        DAT_80611a9c = 1; // First frame init
        return;
    }

    for (int vp = 0; vp < DAT_80611d58; vp++) {
        int vp_state = DAT_80611d5c + vp * 0xFFC;

        if ((*(u32*)(vp_state + 0x21C) & 0x20) == 0) continue;

        *(u16*)(vp_state + 0xE08) = 0; // visible count
        *(u16*)(vp_state + 0xE0A) = 0; // total count

        // Clamp layers
        if (*(int*)(vp_state + 0xD28) > 0x4F) {
            *(int*)(vp_state + 0xD28) = 0x4F;
        }

        // Clear layer node counts
        for (int l = 0; l <= *(int*)(vp_state + 0xD28); l++) {
            g_layer_node_counts[l] = 0;
        }

        // Assign nodes to layers
        for (int n = 1; n < DAT_80611b14 + 1; n++) {
            int layer = *(int*)(vp_state + n * 0xC + 0x3BC);
            if (layer >= 0 && layer < 0x50) {
                u8 count = g_layer_node_counts[layer];
                g_layer_node_counts[layer] = count + 1;
                g_layer_node_indices[count + layer * 200] = (u8)n;
            }
        }

        // Build visible list (back to front)
        for (int l = *(int*)(vp_state + 0xD28); l >= 0; l--) {
            u8 count = g_layer_node_counts[l];
            for (int i = 0; i < (int)count; i++) {
                u32 node = (u32)g_layer_node_indices[i + l * 200];
                s16 max_x = *(s16*)(vp_state + node * 0xC + 0x3C4);
                s16 min_x = *(s16*)(vp_state + node * 0xC + 0x3C0);
                s16 max_z = *(s16*)(vp_state + node * 0xC + 0x3C6);
                s16 min_z = *(s16*)(vp_state + node * 0xC + 0x3C2);

                if (max_x >= 0 && min_x < DAT_806109c4 &&
                    min_x < max_x && max_z >= 0 && min_z < max_z) {
                    s16 vis_count = *(s16*)(vp_state + 0xE08);
                    *(u16*)(vp_state + vis_count * 2 + 0xE0C) = (u16)node;
                    *(s16*)(vp_state + 0xE08) = vis_count + 1;
                }
            }
        }

        *(u16*)(vp_state + 0xE0A) = *(u16*)(vp_state + 0xE08);

        // Add neighbor nodes (if not at max capacity 0xD5)
        if (*(int*)(vp_state + 0x2A0) != 0xD5) {
            int local_neighbors[32];
            int neighbor_count = FUN_800598c4(*(int*)(vp_state + 0x2A0),
                                              local_neighbors, 0x1E);
            for (int n = 0; n < neighbor_count; n++) {
                // Deduplicate
                bool found = false;
                for (int v = 0; v < *(s16*)(vp_state + 0xE08); v++) {
                    if ((int)*(s16*)(vp_state + v * 2 + 0xE0C) == local_neighbors[n]) {
                        found = true;
                        break;
                    }
                }
                if (!found && *(s16*)(vp_state + 0xE08) < 0xD5) {
                    s16 vis = *(s16*)(vp_state + 0xE08);
                    *(s16*)(vp_state + 0xE08) = vis + 1;
                    *(s16*)(vp_state + vis * 2 + 0xE0C) = (s16)local_neighbors[n];
                }
            }
        }
    }
}
