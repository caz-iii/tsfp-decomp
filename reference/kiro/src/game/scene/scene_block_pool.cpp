// Address: 0x80051154 | Name: scene_block_is_loaded | Date: 2026-08-10
// Address: 0x80051180 | Name: scene_block_build_path | Date: 2026-08-10
// Address: 0x800511b8 | Name: scene_block_calc_max_size | Date: 2026-08-10
// Address: 0x80051238 | Name: scene_block_pool_init | Date: 2026-08-10
// Address: 0x80051270 | Name: scene_slot_reset_channels | Date: 2026-08-10
// Address: 0x80051358 | Name: scene_slot_reset_all | Date: 2026-08-10
// Address: 0x800513f0 | Name: scene_block_unload_materials | Date: 2026-08-10
// Address: 0x800514e0 | Name: scene_block_evict_callback | Date: 2026-08-10
// Address: 0x8005154c | Name: scene_block_acquire_slot | Date: 2026-08-10
// Address: 0x80051590 | Name: scene_block_slot_is_active | Date: 2026-08-10
// Batch 112 - Level block resource pool: LRU eviction, slot management, material tracking
//
// Level geometry is stored as .gcr blocks:
//   assets/ts/models/level%d/level%d/gamecube/output/block%03d.gcr
//
// The pool manages loading/unloading of these blocks with LRU-based eviction.
// Each slot (stride 0xB4) holds a scene header with bone channels (stride 0x30).
// The LRU table (stride 8) tracks {loaded_flag, last_access_frame} per block.

#include "common.h"
#include "resource_pool.h"
#include "scene_resource.h"

// ---- Globals ----
static void* g_scene_lru_table;       // DAT_80611adc - LRU entries (stride 8)
static int   g_scene_max_blocks;      // DAT_80611ae4 - max block count
static int   g_scene_system_init;     // DAT_80611ae8 - system initialized flag
static int   g_scene_lru_counter;     // DAT_80611aec - LRU allocation counter
static void* g_scene_slot_array;      // DAT_80611aa8 - scene slot array (stride 0xB4)
static void* g_scene_material_table;  // DAT_80611aa0 - base material table (stride 0x10)
static void* g_scene_slot_data_ptrs;  // DAT_80611b04 - per-slot data pointers
static int   g_scene_slot_count;      // DAT_80611b14 - active slot count
extern int   g_frame_counter;         // DAT_80612028 - global frame counter
extern int   g_current_level_id;      // DAT_80612614 - current level ID

// Path format for level geometry blocks
// "assets/ts/models/level%d/level%d/gamecube/output/block%03d.gcr"

// ============================================================================
// 0x80051154 - scene_block_is_loaded
// Returns true if the specified block index has been loaded into the system.
// Checks the LRU table entry (loaded_flag at offset 0 in stride-8 entry).
// Returns true (1) if system is uninitialized (graceful fallback).
// ============================================================================
bool scene_block_is_loaded(int block_index)
{
    if (g_scene_system_init == 0) {
        return true;  // not initialized = assume available
    }
    // LRU table: stride 8, offset 0 = loaded flag
    return (*(int*)(g_scene_lru_table + block_index * 8) != 0);
}

// ============================================================================
// 0x80051180 - scene_block_build_path
// Formats the .gcr asset path for a given block index.
// Path: "assets/ts/models/level%d/level%d/gamecube/output/block%03d.gcr"
// Uses g_current_level_id for the level directory.
// ============================================================================
void scene_block_build_path(/* float regs */ void* _fp_regs,
                            int block_index, char* out_path)
{
    // Format: assets/ts/models/level<id>/level<id>/gamecube/output/block<idx>.gcr
    sprintf_path(out_path,
        "assets/ts/models/level%d/level%d/gamecube/output/block%03d.gcr",
        g_current_level_id, block_index);
}

// ============================================================================
// 0x800511b8 - scene_block_calc_max_size
// Iterates all block indices, builds each path, queries asset_get_size,
// and returns the maximum size found (minimum 0x96000 = 614400 bytes).
// Used to size the resource pool allocation.
// ============================================================================
u32 scene_block_calc_max_size(void)
{
    char path_buf[88];
    u32 max_size = 0x96000;  // minimum 600KB

    __ps_save_regs();

    for (int i = 0; i < g_scene_max_blocks; i++) {
        scene_block_build_path(NULL, i, path_buf);
        u32 size = asset_get_size(path_buf);
        if (max_size < size) {
            max_size = size;
        }
    }

    __ps_restore_regs();
    return max_size;
}

// ============================================================================
// 0x80051238 - scene_block_pool_init
// Initializes the level block resource pool.
// Calculates the max block size, then creates a resource pool (type 6)
// with enough capacity. Only runs if the scene system is initialized.
// ============================================================================
void scene_block_pool_init(/* float regs */ void* _fp_regs,
                           u32 param_9, u32 param_10, u32 param_11,
                           int param_12, int param_13)
{
    if (g_scene_system_init == 0) return;

    int max_size = scene_block_calc_max_size();
    resource_pool_create_wrapper(6, max_size, param_11, param_12, param_13);
}

// ============================================================================
// 0x80051270 - scene_slot_reset_channels
// Resets all bone channel data for a scene slot to zero.
// Zeros: channel flags (+0x15), 5 keyframe entries per channel (stride 5),
// channel data pointers (+0x2C), and sets sentinel at +0x2F = -1.
// Also sets dirty flag (bit 6 = 0x40) on the slot's status word at +0xB2.
// ============================================================================
void scene_slot_reset_channels(int slot_index)
{
    int* scene_header = *(int**)(g_scene_slot_array + slot_index * 0xB4);
    int  slot_base    = (int)g_scene_slot_array + slot_index * 0xB4;

    // Set dirty flag (bit 6) on slot status at +0xB2
    *(u16*)(slot_base + 0xB2) |= 0x40;

    int bone_count = *scene_header;

    for (int bone = 0; bone < bone_count; bone++) {
        int base = bone_count * -0x30 + bone * 0x30;

        // Clear keyframe data pointer
        scene_header[base + 0x27] = 0;

        for (int chan = 0; chan < 3; chan++) {
            // Channel flags
            scene_header[base + chan + 0x15] = 0;
            // 5 keyframe entries
            scene_header[base + chan * 5 + 0] = 0;
            scene_header[base + chan * 5 + 1] = 0;
            scene_header[base + chan * 5 + 2] = 0;
            scene_header[base + chan * 5 + 4] = 0;
            scene_header[base + chan * 5 + 3] = 0;
            // Channel data pointer at +0x2C
            scene_header[base + chan + 0x2C] = 0;
        }

        // Sentinel marker
        scene_header[base + 0x2F] = -1;
    }
}

// ============================================================================
// 0x80051358 - scene_slot_reset_all
// Resets all scene slots and the LRU table.
// Sets each LRU entry's last_access_frame to (frame_counter - 1) and clears
// the loaded flag. Then resets all active scene slots via scene_slot_reset_channels.
// ============================================================================
void scene_slot_reset_all(void)
{
    if (g_scene_system_init == 0) return;

    g_scene_lru_counter = 0;

    // Reset LRU table: set all entries to stale
    for (int i = 0; i < g_scene_max_blocks; i++) {
        *(int*)(g_scene_lru_table + i * 8 + 4) = g_frame_counter - 1;
        *(u32*)(g_scene_lru_table + i * 8)     = 0;  // not loaded
    }

    // Reset all active scene slots
    for (int slot = 1; slot <= g_scene_slot_count; slot++) {
        scene_slot_reset_channels(slot);
    }
}

// ============================================================================
// 0x800513f0 - scene_block_unload_materials
// Unloads materials from a level block.
// Walks the material reference table (stride 0x10, terminated by -1):
//   - If the block's material is resolved (!= 0x40000000) AND the base table's
//     entry IS unresolved (== 0x40000000), release the material.
// Then resets all referenced scene slots and clears their "loaded" bit (bit 0).
// ============================================================================
void scene_block_unload_materials(void)
{
    int* block_header = (int*)__ps_get_param();
    int  ref_count    = block_header[1];

    // Resolve material table from block header offset
    int* mat_table;
    if (block_header[0] == 0) {
        mat_table = NULL;
    } else {
        mat_table = (int*)(block_header[0] + (int)block_header);
    }

    // Release materials that are unique to this block
    if (mat_table != NULL) {
        for (int i = 0; mat_table[i * 4] != -1; i++) {
            int mat_type = mat_table[i * 4];
            // If block has resolved material but base doesn't, release it
            if (mat_type != 0x40000000 &&
                *(int*)(g_scene_material_table + i * 0x10) == 0x40000000)
            {
                material_release(mat_type);
            }
        }
        material_flush_released();
    }

    // Reset all referenced scene slots
    for (int i = 0; i < ref_count; i++) {
        int slot_idx = block_header[i * 2 + 3];
        scene_slot_reset_channels(slot_idx);

        // Clear "loaded" bit (bit 0) on slot data
        int slot_data = *(int*)(g_scene_slot_data_ptrs + slot_idx * 4);
        *(u32*)(slot_data + 0x28) &= ~0x1;
    }

    __ps_restore_param();
}

// ============================================================================
// 0x800514e0 - scene_block_evict_callback
// Resource pool eviction callback. Called when the pool needs to free a slot.
// Returns 1 (evictable) if:
//   - The block's last_access_frame < current frame_counter (stale), OR
//   - force_evict != 0
// On eviction: calls scene_block_unload_materials and clears the LRU entry.
// ============================================================================
int scene_block_evict_callback(u32 param_1, u32 param_2, int block_index, int force_evict)
{
    int last_access = *(int*)(g_scene_lru_table + block_index * 8 + 4);

    if (last_access < g_frame_counter || force_evict != 0) {
        scene_block_unload_materials();
        *(u32*)(g_scene_lru_table + block_index * 8) = 0;  // mark unloaded
        return 1;  // eviction successful
    }

    return 0;  // cannot evict (still in use this frame)
}

// ============================================================================
// 0x8005154c - scene_block_acquire_slot
// Acquires a scene slot from the resource pool.
// Uses scene_block_evict_callback as the eviction function.
// Returns the slot handle, or 0 if acquisition failed.
// ============================================================================
u32 scene_block_acquire_slot(u32 block_id)
{
    u32 handle;
    int result = resource_slot_acquire(&handle, scene_block_evict_callback, block_id);
    if (result == -1) {
        handle = 0;
    }
    return handle;
}

// ============================================================================
// 0x80051590 - scene_block_slot_is_active
// Returns true if the specified block's LRU entry has its loaded flag set.
// Simple check: LRU table entry at (index * 8 + 0) != 0.
// ============================================================================
bool scene_block_slot_is_active(int block_index)
{
    return (*(int*)(g_scene_lru_table + block_index * 8) != 0);
}
