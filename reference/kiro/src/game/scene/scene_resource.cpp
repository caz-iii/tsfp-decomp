// Address: 0x80050b80 | Name: scene_buffer_release | Date: 2026-08-10
// Address: 0x80050bc0 | Name: scene_async_load_start | Date: 2026-08-10
// Address: 0x80050c28 | Name: scene_async_load_poll | Date: 2026-08-10
// Address: 0x80050c78 | Name: scene_async_load_cancel | Date: 2026-08-10
// Address: 0x80050ca4 | Name: scene_data_process_and_link | Date: 2026-08-10
// Address: 0x80050f24 | Name: scene_stub_noop | Date: 2026-08-10
// Address: 0x80050f28 | Name: scene_data_unload | Date: 2026-08-10
// Address: 0x80051130 | Name: scene_get_data_by_index | Date: 2026-08-10
// Batch 111 - Scene/model resource loading, processing, and unloading system

#include "common.h"
#include "stream_buffer.h"
#include "asset_load.h"

// ---- Globals ----
static int   g_scene_async_handle = -1;  // DAT_8061040c - async load handle
static u8    g_scene_async_buf[0x1F1];   // DAT_80610410 - async read buffer
static int   g_scene_load_active;        // DAT_80611ac8 - 1 if load in progress
static void* g_scene_data_table;         // DAT_80611ad8 - scene data pointer array
static int   g_scene_system_init;        // DAT_80611ae8 - 1 if system initialized
static int   g_scene_slot_count;         // DAT_80611b14 - active scene slot count
static void* g_scene_slot_array;         // DAT_80611aa8 - scene slot table base

// ============================================================================
// 0x80050b80 - scene_buffer_release
// Releases or marks a scene stream buffer for deferred free.
// If defer_free == 0: immediately frees the buffer (address at base - 0x40).
// If defer_free != 0: sets pending-release flag at base - 0x3C, clears data.
// ============================================================================
void scene_buffer_release(int buffer_base, int defer_free)
{
    if (defer_free == 0) {
        stream_buffer_free((int*)(buffer_base - 0x40));
    } else {
        *(u32*)(buffer_base - 0x3C) = 1;  // mark for deferred release
        *(u32*)(buffer_base - 0x38) = 0;  // clear associated data
    }
}

// ============================================================================
// 0x80050bc0 - scene_async_load_start
// Begins async loading of a scene/model resource from DVD.
// Calls asset_load_async to start the read, then sets up streaming metadata
// via FUN_801873f0 (scene streaming setup).
// Sets g_scene_load_active = 1.
// ============================================================================
void scene_async_load_start(/* float regs */ void* _fp_regs,
                            u8* path, u32 priority, int size_raw,
                            u32 param_12, u32 param_13, u32 param_14, u32 param_15)
{
    g_scene_async_handle = asset_load_async(
        path, 0, &g_scene_async_buf, 0x1F1,
        param_12, param_13, param_14, param_15
    );

    // Set up streaming with 32-byte aligned size
    u32 aligned_size = (size_raw + 0x1F) & ~0x1F;
    scene_streaming_setup(g_scene_async_handle, priority, 0, aligned_size);

    g_scene_load_active = 1;
}

// ============================================================================
// 0x80050c28 - scene_async_load_poll
// Polls the async scene load and finalizes when complete.
// Returns early if no load is active or if the handle isn't ready yet.
// On completion: clears g_scene_load_active and releases the async handle.
// ============================================================================
void scene_async_load_poll(void)
{
    if (g_scene_load_active == 0) return;

    int ready = scene_streaming_is_complete(g_scene_async_handle);
    if (ready != 0) {
        g_scene_load_active = 0;
        scene_streaming_release(g_scene_async_handle);
        g_scene_async_handle = -1;
    }
}

// ============================================================================
// 0x80050c78 - scene_async_load_cancel
// Force-cancels an in-progress async scene load.
// Releases the handle and clears the loading flag immediately.
// ============================================================================
void scene_async_load_cancel(void)
{
    scene_streaming_release(g_scene_async_handle);
    g_scene_load_active = 0;
}

// ============================================================================
// 0x80050ca4 - scene_data_process_and_link
// Processes loaded scene data after async load completes.
//
// Algorithm:
//   1. Resolve sub-object pointer from scene header (offset + base)
//   2. Copy bone channel data (3 channels per bone, stride 0x30):
//      - Channel flags at +0x15
//      - 5 keyframe entries per channel (pos/rot/scale/tangent)
//      - 0x38-byte keyframe data block per channel
//   3. Allocate stream buffer for vertex data (16-byte aligned)
//   4. Resolve material reference table:
//      - Entries are {material_index, data_offset} pairs, terminated by -1
//      - Materials with type 0x40000000 are unresolved -> call material_resolve
//   5. Clear loading bit (bit 2), set processed bit (bit 1) in scene flags
// ============================================================================
void scene_data_process_and_link(void)
{
    u64 reg_pair = __ps_get_param_pair();
    int* scene_header = (int*)(reg_pair >> 32);
    u32* scene_desc   = (u32*)(u32)reg_pair;

    // Resolve sub-object data pointer
    int* sub_objects;
    if (*(int*)(scene_desc[3] + 4) == 0) {
        sub_objects = NULL;
    } else {
        sub_objects = (int*)(*(int*)(scene_desc[3] + 4) + scene_desc[3]);
    }

    scene_init_sub_objects(sub_objects, scene_desc[3]);

    int bone_count  = *sub_objects;
    int header_base = *scene_header;

    // Copy bone channel data from loaded buffer to scene header
    for (int bone = 0; bone < bone_count; bone++) {
        for (int chan = 0; chan < 3; chan++) {
            int dst_base = header_base * -0x30 + bone * 0x30;
            int src_base = bone_count * -0x30 + bone * 0x30;

            // Channel flags
            scene_header[dst_base + chan + 0x15] = sub_objects[src_base + chan + 0x15];

            // 5 keyframe entries per channel
            scene_header[dst_base + chan * 5 + 0] = sub_objects[src_base + chan * 5 + 0];
            scene_header[dst_base + chan * 5 + 1] = sub_objects[src_base + chan * 5 + 1];
            scene_header[dst_base + chan * 5 + 2] = sub_objects[src_base + chan * 5 + 2];
            scene_header[dst_base + chan * 5 + 4] = sub_objects[src_base + chan * 5 + 4];
            scene_header[dst_base + chan * 5 + 3] = sub_objects[src_base + chan * 5 + 3];

            // Copy 0x38-byte keyframe data block (7 DWORDs x 2 = 14 iterations)
            u32* dst_kf = (u32*)(scene_header[dst_base + bone * 0x30 + 0x27] + chan * 0x38 - 4);
            u32* src_kf = (u32*)(sub_objects[src_base + bone * 0x30 + 0x27] + chan * 0x38 - 4);
            for (int i = 0; i < 7; i++) {
                dst_kf[i*2 + 1] = src_kf[i*2 + 1];
                dst_kf[i*2 + 2] = src_kf[i*2 + 2];
            }
        }
    }

    // Allocate vertex data buffer
    if (scene_desc[4] == 0) {
        scene_calc_vertex_size(scene_header, &scene_desc[4]);
    }
    u32 buf_size = (scene_desc[4] + 0xF) & ~0xF;  // 16-byte align
    scene_desc[5] = stream_buffer_alloc(buf_size);
    scene_calc_vertex_size(scene_header, NULL);

    // Resolve material reference table
    int* mat_table;
    if (*(int*)scene_desc[3] == 0) {
        mat_table = NULL;
    } else {
        mat_table = (int*)(*(int*)scene_desc[3] + scene_desc[3]);
    }
    scene_header[10] = (int)mat_table;

    // Walk material entries until sentinel (-1)
    for (int i = 0; mat_table[i * 2] != -1; i++) {
        int* mat_entry   = &mat_table[i * 2];
        int* mat_slot    = (int*)(scene_header[4] + mat_entry[0] * 0x10);

        // Resolve data offset to absolute pointer
        if (mat_entry[1] == 0) {
            mat_entry[1] = 0;
        } else {
            mat_entry[1] = mat_entry[1] + scene_desc[3];
        }

        // If material type is 0x40000000, it's unresolved -> resolve it
        if (*mat_slot == 0x40000000) {
            material_resolve_from_data(mat_entry[1], mat_slot);
        }
    }

    // Update scene flags: clear bit 2 (loading), set bit 1 (processed)
    *scene_desc &= ~0x4;
    *scene_desc |= 0x2;

    __ps_restore_param_pair();
}

// ============================================================================
// 0x80050f24 - scene_stub_noop
// Empty placeholder - no operation.
// ============================================================================
void scene_stub_noop(void)
{
    // intentionally empty
}

// ============================================================================
// 0x80050f28 - scene_data_unload
// Unloads scene data: frees stream buffers, zeros bone/mesh channel arrays,
// and unlinks shared materials.
//
// For materials: checks all other active scene slots to see if they reference
// the same material. If shared, just detaches. If unique, fully releases it.
// ============================================================================
void scene_data_unload(u32 param_1, u32 param_2, int defer_free)
{
    u64 reg_pair = __ps_get_param_pair_3();
    int* scene_header = (int*)(reg_pair >> 32);
    u32* scene_desc   = (u32*)(u32)reg_pair;

    int header_base = *scene_header;

    // Free scene data buffer and vertex buffer
    scene_buffer_release(scene_desc[3], defer_free);
    scene_desc[3] = 0;
    scene_buffer_release(scene_desc[5], defer_free);
    scene_desc[5] = 0;

    // Clear processed flag
    *scene_desc &= ~0x2;

    // Zero all bone channel data
    for (int bone = 0; bone < *scene_header; bone++) {
        int base = header_base * -0x30 + bone * 0x30;
        scene_header[base + 0x27] = 0;  // keyframe data ptr
        for (int chan = 0; chan < 3; chan++) {
            scene_header[base + chan + 0x15] = 0;
            scene_header[base + chan * 5 + 0] = 0;
            scene_header[base + chan * 5 + 1] = 0;
            scene_header[base + chan * 5 + 2] = 0;
            scene_header[base + chan * 5 + 4] = 0;
            scene_header[base + chan * 5 + 3] = 0;
        }
    }

    // Unlink materials - check for shared references
    int mat_idx = 0;
    while (true) {
        int* mat_table = (int*)scene_header[10];
        if (mat_table[mat_idx * 2] == -1) {
            scene_header[10] = 0;
            __ps_restore_param_pair_3();
            return;
        }

        int* mat_entry = &mat_table[mat_idx * 2];
        int* mat_slot  = (int*)(scene_header[4] + mat_entry[0] * 0x10);

        // Check if any other scene uses this material
        int shared = 0;
        for (int slot = 1; slot <= g_scene_slot_count; slot++) {
            int* other_header = *(int**)(g_scene_slot_array + slot * 0xB4);
            if (other_header == scene_header) continue;
            if (other_header[10] == 0) continue;

            int* other_mat_table = (int*)other_header[10];
            for (int j = 0; other_mat_table[j * 2] != -1; j++) {
                if (mat_entry[0] == other_mat_table[j * 2]) {
                    material_detach_shared();
                    shared = 1;
                    break;
                }
            }
            if (shared) break;
        }

        if (!shared) {
            // Fully release the material
            material_release(*mat_slot);
            *mat_slot = 0x40000000;  // reset to unresolved marker
        }

        mat_idx++;
    }
}

// ============================================================================
// 0x80051130 - scene_get_data_by_index
// Returns a scene data pointer by index from the global scene table.
// Returns 0 (NULL) if the scene system has not been initialized.
// ============================================================================
u32 scene_get_data_by_index(int index)
{
    if (g_scene_system_init == 0) {
        return 0;
    }
    return *(u32*)(g_scene_data_table + index * 4);
}
