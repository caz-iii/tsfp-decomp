// Address: 0x800522c8 | Name: scene_system_load_level | Date: 2026-08-10
// Batch 114 - Level scene system initialization (MASSIVE pointer resolver)
//
// Loads a .gcr level file and resolves all relative offsets to absolute addresses.
// Sets up the entire scene system: slot arrays, material tables, block groups,
// room lists, viewport config, and allocates auxiliary buffers.

#include "common.h"
#include "scene_resource.h"
#include "stream_queue.h"

// ============================================================================
// 0x800522c8 - scene_system_load_level
// MASSIVE level scene initialization. Loaded from streaming queue callback.
//
// The .gcr level header contains relative offsets at fixed positions:
//   +0x04: scene slot array (stride 0xB4)
//   +0x08: room pointer array
//   +0x0C: block config (first DWORD = count, then data)
//   +0x10: viewport/camera defs
//   +0x1C: room block lists (per-slot, null-terminated index lists)
//   +0x20: unknown table
//   +0x24: block group table (LRU + block indices)
//   +0x28: unknown table 2
//   +0x2C: boundary table
//
// Algorithm:
//   1. Resolve all header offsets to absolute pointers (offset + base)
//   2. Set global pointers from resolved header
//   3. Iterate scene slots:
//      a. Resolve slot scene header pointer
//      b. Set material table and base material refs
//      c. Register slot with renderer (ID = index + 0x8FE)
//      d. Mark as having block data (flag 0x200000)
//      e. If room streaming or block streaming enabled: calculate vertex sizes
//      f. Resolve sub-slot pointers (+4, +8, +C with sub-list walk)
//      g. Increment g_scene_slot_count
//   4. Call collision init with slot array
//   5. Resolve room pointer array (count rooms)
//   6. Resolve boundary table (count boundaries)
//   7. Set up viewport/camera definitions:
//      a. Store count and stride-0xC0 entries
//      b. Resolve per-viewport string pointers
//      c. Build per-slot viewport list
//   8. If room block lists present: enable room streaming, resolve per-slot lists
//   9. Allocate auxiliary buffers:
//      - Visibility flags (0xE0 bytes)
//      - Slot-to-renderer map ((slot_count+1)*4 bytes, 16-aligned)
//      - Room state array ((room_count+1)*0x28 bytes, 16-aligned)
//      - Slot data pointers ((slot_count+1)*4 bytes, 16-aligned)
//  10. Call scene_finalize_setup
// ============================================================================
void scene_system_load_level(void)
{
    // Find the loaded level data from streaming queue
    u64 result = stream_find_pending_by_name();
    int stream_handle = (int)result;
    int* header_base  = (int*)((u64)result >> 32);
    int  base_addr    = *(int*)(header_base + 0x10 / 4);  // raw data base

    // --- Resolve all header offsets to absolute pointers ---
    // Offsets at +4, +8, +C, +10, +1C, +20, +24, +28, +2C
    int offset_fields[] = {4, 8, 0xC, 0x10, 0x1C, 0x20, 0x24, 0x28, 0x2C};
    for (int i = 0; i < 9; i++) {
        int off = offset_fields[i];
        int val = *(int*)(base_addr + off);
        *(int*)(base_addr + off) = (val == 0) ? 0 : (val + base_addr);
    }

    // --- Set global scene pointers from resolved header ---
    g_scene_slot_array      = *(int*)(base_addr + 0x04);  // DAT_80611aa8
    g_scene_room_ptrs       = *(int*)(base_addr + 0x08);  // DAT_80611aac
    g_scene_boundary_table  = *(int*)(base_addr + 0x2C);  // DAT_80611ab4
    g_scene_unknown_table   = *(int*)(base_addr + 0x20);  // DAT_80611ab8

    // Block config at +0x0C: {count, data_count, LRU_entries[], data_table[], group_table[]}
    if (*(int*)(base_addr + 0x0C) != 0) {
        int* block_cfg = *(int**)(base_addr + 0x0C);
        g_scene_block_config_count = block_cfg[0];  // DAT_80611b08
        g_scene_block_config_data  = &block_cfg[1]; // DAT_80611ab0
    }

    // Block group table at +0x24: {max_blocks, data_count, lru[max*2], data[count], groups[]}
    if (*(int*)(base_addr + 0x24) != 0) {
        int* blk_grp = *(int**)(base_addr + 0x24);
        int max_blocks = blk_grp[0];
        int data_count = blk_grp[1];
        g_scene_lru_table         = &blk_grp[2];                          // DAT_80611adc
        g_scene_data_table        = (void*)((int)g_scene_lru_table + max_blocks * 8); // DAT_80611ad8
        g_scene_block_group_table = (int)g_scene_data_table + data_count * 4;         // DAT_80611ae0
        g_scene_max_blocks        = max_blocks;                           // DAT_80611ae4
    }

    // --- Initialize scene slots ---
    g_scene_system_init = 0;  // DAT_80611ae8
    g_scene_slot_count  = 0;  // DAT_80611b14

    for (int slot = 1; *(int*)(g_scene_slot_array + slot * 0xB4) != 0; slot++) {
        // Resolve scene header pointer (relative -> absolute)
        int header_offset = *(int*)(g_scene_slot_array + slot * 0xB4);
        int* scene_header = (int*)(header_offset + base_addr);
        *(int*)(g_scene_slot_array + slot * 0xB4) = (int)scene_header;

        // Set base material table ref
        scene_header[5] = (int)g_scene_material_table;  // +0x14

        // Initialize sub-object offsets
        scene_init_sub_objects(scene_header, base_addr);

        // Set material table pointer
        scene_header[4] = (int)g_scene_material_table;  // +0x10

        // Register with renderer (ID = slot + 0x8FE)
        renderer_register_scene(slot + 0x8FE, scene_header);

        // Mark as having block data
        scene_header[3] |= 0x200000;

        // Calculate vertex sizes if streaming enabled
        if (g_scene_room_enabled != 0 || g_scene_system_init != 0) {
            u32 vert_size;
            scene_calc_vertex_size(scene_header, &vert_size);
        }

        // Resolve sub-slot pointers at +4, +8, +C
        for (int sub = 1; sub <= 3; sub++) {
            int sub_off = *(int*)(g_scene_slot_array + slot * 0xB4 + sub * 4);
            if (sub_off != 0) {
                *(int*)(g_scene_slot_array + slot * 0xB4 + sub * 4) = sub_off + base_addr;
            }
        }

        // Resolve sub-list at +0xC (null-terminated pointer array)
        int sub_list = *(int*)(g_scene_slot_array + slot * 0xB4 + 0xC);
        if (sub_list != 0) {
            int* list_ptr = (int*)sub_list;
            for (int j = 0; list_ptr[j] != 0; j++) {
                list_ptr[j] = list_ptr[j] + base_addr;
            }
        }

        g_scene_slot_count++;
    }

    // --- Initialize collision system ---
    collision_init_from_scene(g_scene_slot_array);

    // --- Count and resolve room pointers ---
    g_scene_room_count = 0;  // DAT_80611b10
    for (int i = 1; *(int*)(g_scene_room_ptrs + i * 4) != 0; i++) {
        int room_off = *(int*)(g_scene_room_ptrs + i * 4);
        *(int*)(g_scene_room_ptrs + i * 4) = (room_off == 0) ? 0 : (room_off + base_addr);
        g_scene_room_count++;
    }

    // --- Count and resolve boundaries ---
    g_scene_boundary_count = 0;  // DAT_80611b0c
    if (g_scene_boundary_table != 0) {
        for (int i = 1; *(int*)(g_scene_boundary_table + i * 4) != 0; i++) {
            int bnd_off = *(int*)(g_scene_boundary_table + i * 4);
            *(int*)(g_scene_boundary_table + i * 4) = (bnd_off == 0) ? 0 : (bnd_off + base_addr);
            g_scene_boundary_count++;
        }
    }

    // --- Viewport/camera definitions at +0x10 ---
    if (*(int*)(base_addr + 0x10) != 0) {
        int* vp_base = *(int**)(base_addr + 0x10);
        g_viewport_def_count = vp_base[0];       // DAT_80611d20
        g_viewport_defs      = (int)vp_base + 4; // DAT_80611d1c (stride 0xC0)

        // Resolve per-viewport name strings
        for (int vp = 0; vp < g_viewport_def_count; vp++) {
            int name_off = *(int*)(g_viewport_defs + vp * 0xC0 + 0xA8);
            *(int*)(g_viewport_defs + vp * 0xC0 + 0xA8) =
                (name_off == 0) ? 0 : (name_off + base_addr);
        }

        // Per-slot viewport lists (after viewport defs)
        g_viewport_slot_lists = (int)vp_base + 4 + g_viewport_def_count * 0xC0; // DAT_80611d24
        for (int s = 0; s <= g_scene_slot_count; s++) {
            int list_off = *(int*)(g_viewport_slot_lists + s * 4);
            *(int*)(g_viewport_slot_lists + s * 4) = (list_off == 0) ? 0 : (list_off + base_addr);

            // Resolve each entry in the per-slot list
            int* entries = *(int**)(g_viewport_slot_lists + s * 4);
            while (*entries != 0) {
                *entries = (*entries == 0) ? 0 : (*entries + base_addr);
                entries++;
            }
        }
    }

    // --- Room block lists at +0x1C ---
    if (*(int*)(base_addr + 0x1C) != 0) {
        g_scene_room_enabled = 1;  // DAT_80611afc
        g_scene_room_block_lists = *(int*)(base_addr + 0x1C);  // DAT_80611abc
        for (int s = 0; s <= g_scene_slot_count; s++) {
            int list_off = *(int*)(g_scene_room_block_lists + s * 4);
            *(int*)(g_scene_room_block_lists + s * 4) =
                (list_off == 0) ? 0 : (list_off + base_addr);
        }
    }

    // --- Allocate auxiliary buffers ---
    g_scene_visibility_flags = mem_alloc(0xE0, 1);              // DAT_80611b1c
    g_scene_slot_renderer_map = mem_alloc(
        ((g_scene_slot_count + 1) * 4 + 0xF) & ~0xF, 1);       // DAT_80611b18
    g_scene_room_state = mem_alloc(
        ((g_scene_room_count + 1) * 0x28 + 0xF) & ~0xF, 1);    // DAT_80611a98
    g_scene_slot_data_ptrs = mem_alloc(
        ((g_scene_slot_count + 1) * 4 + 0xF) & ~0xF, 1);       // DAT_80611b04

    scene_finalize_setup();
}
