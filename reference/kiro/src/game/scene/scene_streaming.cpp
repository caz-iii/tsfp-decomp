// Address: 0x800515a8 | Name: scene_block_load_and_link | Date: 2026-08-10
// Address: 0x80051874 | Name: scene_block_begin_load | Date: 2026-08-10
// Address: 0x80051924 | Name: scene_block_preload_visible | Date: 2026-08-10
// Address: 0x80051a0c | Name: scene_streaming_update | Date: 2026-08-10
// Address: 0x80051be8 | Name: scene_block_load_sync | Date: 2026-08-10
// Address: 0x80051cbc | Name: scene_room_is_processed | Date: 2026-08-10
// Address: 0x80051cec | Name: scene_room_streaming_update | Date: 2026-08-10
// Address: 0x8005205c | Name: scene_room_preload_all_visible | Date: 2026-08-10
// Address: 0x80052150 | Name: scene_room_unload_all | Date: 2026-08-10
// Address: 0x80052244 | Name: scene_room_system_init | Date: 2026-08-10
// Batch 113 - Scene streaming: per-frame update, room/block load/unload, visibility-driven
//
// Two path formats:
//   Block: assets/ts/models/level%d/level%d/gamecube/output/block%03d.gcr
//   Room:  assets/ts/models/level%d/level%d/gamecube/output/room%d.gcr
//
// The streaming system checks camera visibility each frame and loads/unloads
// scene blocks and rooms on demand. Supports synchronous (blocking) and
// async (non-blocking) load paths.

#include "common.h"
#include "scene_resource.h"
#include "scene_block_pool.h"
#include "asset_load.h"
#include "stream_buffer.h"

// ---- Globals ----
static int   g_scene_pending_handle;    // DAT_80611ad4 - handle of block being loaded
static int   g_scene_pending_size;      // DAT_80611ad0 - size of pending load
static int   g_scene_load_deferred;     // DAT_80611acc - if 1, defer linking after load
static int   g_scene_room_enabled;      // DAT_80611afc - room streaming enabled
static void* g_scene_room_table;        // DAT_80611ac4 - room state table (stride 0x18)
static int*  g_scene_room_buffer;       // DAT_80611af4 - room streaming buffer (5MB)
static int*  g_scene_room_buf_ptr;      // DAT_80611af8 - current buffer write ptr
static int   g_scene_room_timer;        // DAT_80611af0 - streaming timer (resets at 0x78)
static int   g_scene_block_group_table; // DAT_80611ae0 - block group defs (stride 0x18, 6 entries)
static int   g_scene_room_count;        // DAT_80611b10 - active room count
static void* g_scene_room_ptrs;         // DAT_80611aac - room pointer array
static void* g_scene_room_block_lists;  // DAT_80611abc - per-room block index lists
static int   g_viewport_count;          // DAT_80611d58 - number of active viewports
static int   g_viewport_array;          // DAT_80611d5c - viewport array (stride 0xFFC)
extern int   g_frame_counter;           // DAT_80612028
extern int   g_current_level_id;        // DAT_80612614
extern int   g_scene_lru_lifetime;      // DAT_806108e4 - LRU lifetime in frames
extern u32   g_system_flags;            // DAT_805015c0

// ============================================================================
// 0x800515a8 - scene_block_load_and_link
// Processes a loaded level block: resolves materials and copies bone/mesh data
// into the target scene slots.
//
// Algorithm:
//   1. Resolve material table from block header
//   2. For each material entry (stride 0x10, terminated by -1):
//      - If 0x40000000 (unresolved): copy from base material table
//      - Otherwise: resolve the material reference
//   3. For each referenced scene slot:
//      - Clear dirty flag (bit 6 at +0xB2)
//      - Set loaded flag (bit 0 at +0x28)
//      - Copy all bone channel data (flags, keyframes, data ptrs)
//      - Set material table pointer
//      - Set flag 0x200000 (has block data)
//      - Recalculate vertex buffers and notify renderer
// ============================================================================
void scene_block_load_and_link(void)
{
    int* block_header = (int*)__ps_get_param_5();
    int  ref_count    = block_header[1];

    // Resolve material table
    int* mat_table;
    if (block_header[0] == 0) {
        mat_table = NULL;
    } else {
        mat_table = (int*)(block_header[0] + (int)block_header);
    }

    if (mat_table != NULL) {
        scene_init_material_offsets(mat_table, (int)block_header);

        // Process each material entry
        for (int i = 0; mat_table[i * 4] != -1; i++) {
            if (mat_table[i * 4] == 0x40000000) {
                // Copy from base material table (4 DWORDs)
                u32* dst = (u32*)&mat_table[i * 4];
                u32* src = (u32*)(g_scene_material_table + i * 0x10);
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = src[3];
            } else {
                // Resolve custom material
                material_resolve_from_data(mat_table[i * 4], &mat_table[i * 4]);
            }
        }
    }

    // Copy data into each referenced scene slot
    for (int ref = 0; ref < ref_count; ref++) {
        int slot_idx    = block_header[ref * 2 + 3];
        int* sub_data;
        if (block_header[ref * 2 + 2] == 0) {
            sub_data = NULL;
        } else {
            sub_data = (int*)(block_header[ref * 2 + 2] + (int)block_header);
        }

        int* scene_header = *(int**)(g_scene_slot_array + slot_idx * 0xB4);
        int  slot_base    = (int)g_scene_slot_array + slot_idx * 0xB4;

        // Clear dirty flag (bit 6)
        *(u16*)(slot_base + 0xB2) &= ~0x40;

        // Set loaded flag (bit 0) on slot data
        int slot_data = *(int*)(g_scene_slot_data_ptrs + slot_idx * 4);
        *(u32*)(slot_data + 0x28) |= 0x1;

        // Initialize sub-object offsets
        scene_init_sub_objects(sub_data, (int)block_header);

        // Copy bone frame count
        scene_header[3] = sub_data[3];

        int src_bone_count = *sub_data;
        int dst_bone_count = *scene_header;

        // Copy all bone channel data
        for (int bone = 0; bone < dst_bone_count; bone++) {
            for (int chan = 0; chan < 3; chan++) {
                int dst_base = dst_bone_count * -0x30 + bone * 0x30;
                int src_base = src_bone_count * -0x30 + bone * 0x30;

                scene_header[dst_base + chan + 0x15] = sub_data[src_base + chan + 0x15];
                scene_header[dst_base + chan * 5 + 0] = sub_data[src_base + chan * 5 + 0];
                scene_header[dst_base + chan * 5 + 1] = sub_data[src_base + chan * 5 + 1];
                scene_header[dst_base + chan * 5 + 2] = sub_data[src_base + chan * 5 + 2];
                scene_header[dst_base + chan * 5 + 4] = sub_data[src_base + chan * 5 + 4];
                scene_header[dst_base + chan * 5 + 3] = sub_data[src_base + chan * 5 + 3];
                scene_header[dst_base + chan + 0x2C] = sub_data[src_base + chan + 0x2C];
            }
            scene_header[dst_bone_count * -0x30 + bone * 0x30 + 0x27] =
                sub_data[src_bone_count * -0x30 + bone * 0x30 + 0x27];
            scene_header[dst_bone_count * -0x30 + bone * 0x30 + 0x2F] =
                sub_data[src_bone_count * -0x30 + bone * 0x30 + 0x2F];
        }

        // Link material table if present
        if (mat_table != NULL) {
            scene_header[4] = (int)mat_table;
        }

        // Mark as having block data
        scene_header[3] |= 0x200000;

        // Recalculate and notify
        scene_calc_vertex_size(scene_header, NULL);
        scene_flush_display_lists();
        renderer_notify_scene_updated(*(int*)(g_scene_slot_data_ptrs + slot_idx * 4));
    }

    __ps_restore_param_5();
}

// ============================================================================
// 0x80051874 - scene_block_begin_load
// Begins loading a specific block from disk.
// Checks asset size fits in resource pool, acquires a slot, sets LRU timestamp,
// then starts the async load via scene_async_load_start.
// ============================================================================
void scene_block_begin_load(void)
{
    u8* path_ptr;
    int block_index;

    __ps_save_regs_2();
    path_ptr    = (u8*)(__ps_get_hi());
    block_index = (int)(__ps_get_lo());

    int file_size = asset_get_size(path_ptr);
    if (file_size <= 0) goto done;

    int slot_size = get_resource_slot_size();
    if (file_size > slot_size) goto done;

    g_scene_pending_handle = scene_block_acquire_slot(block_index);
    if (g_scene_pending_handle == 0) goto done;

    g_scene_pending_size = file_size;

    // Store handle and set LRU timestamp (current frame + 2x lifetime)
    *(int*)(g_scene_lru_table + block_index * 8)     = g_scene_pending_handle;
    *(int*)(g_scene_lru_table + block_index * 8 + 4) = g_frame_counter + g_scene_lru_lifetime * 2;

    scene_async_load_start(path_ptr, g_scene_pending_handle, file_size);

done:
    __ps_restore_regs_2();
}

// ============================================================================
// 0x80051924 - scene_block_preload_visible
// Preloads blocks that are visible from active viewports.
// Iterates viewports, finds those with valid scene references,
// then requests loading for all 6 block indices in the block group.
// Only runs if system is initialized and streaming flag is clear.
// ============================================================================
void scene_block_preload_visible(void)
{
    __ps_save_regs_3();
    bool found = false;

    if (g_scene_system_init == 0) goto done;
    if ((g_system_flags & 0x20000000) != 0) goto done;

    for (int vp = 0; vp < g_viewport_count && !found; vp++) {
        int viewport = g_viewport_array + vp * 0xFFC;

        // Check viewport is active (bit 0) and has scene ref
        if ((*(u32*)(viewport + 0x21C) & 1) == 0) continue;
        if (*(int*)(viewport + 0x2A0) == 0) continue;

        // Get block group for this viewport's scene
        int scene_idx = *(int*)(viewport + 0x2A0);
        int group = g_scene_block_group_table +
                    *(int*)(g_scene_data_table + scene_idx * 4) * 0x18;

        found = true;
        for (int i = 0; i < 6; i++) {
            int block_id = *(int*)(group + i * 4);
            if (block_id != -1) {
                scene_block_load_sync(block_id, 1);
            }
        }
    }

done:
    __ps_restore_regs_3();
}

// ============================================================================
// 0x80051a0c - scene_streaming_update
// Main per-frame streaming update for level blocks.
// Algorithm:
//   1. Poll async load; if complete and not deferred, link the data
//   2. If no load active: iterate viewports to find visible blocks
//   3. For visible blocks: update LRU timestamps
//   4. Find first unloaded visible block and start loading it
// Only loads one block per frame (bandwidth-limited streaming).
// ============================================================================
void scene_streaming_update(void)
{
    __ps_save_regs_4();

    if (g_scene_system_init == 0) goto done;

    scene_async_load_poll();

    // If load just completed, link the data
    if (g_scene_load_active == 0 && g_scene_pending_handle != 0) {
        if (g_scene_load_deferred == 0) {
            scene_block_load_and_link();
        }
        g_scene_pending_handle = 0;
    }

    // If no load is in progress, find and start loading the next needed block
    if (g_scene_load_active != 0) goto done;

    bool started_load = false;
    for (int vp = 0; vp < g_viewport_count && !started_load; vp++) {
        int viewport = g_viewport_array + vp * 0xFFC;

        // Viewport must be active (bit 0) and visible (bit 5) with scene ref
        if ((*(u32*)(viewport + 0x21C) & 1) == 0) continue;
        if ((*(u32*)(viewport + 0x21C) & 0x20) == 0) continue;
        if (*(int*)(viewport + 0x2A0) == 0) continue;

        int scene_idx = *(int*)(viewport + 0x2A0);
        int group = g_scene_block_group_table +
                    *(int*)(g_scene_data_table + scene_idx * 4) * 0x18;

        // Update LRU timestamps for all visible blocks
        for (int i = 0; i < 6; i++) {
            int block_id = *(int*)(group + i * 4);
            if (block_id != -1 && (g_system_flags & 0x20000000) == 0 &&
                g_scene_lru_counter == 0)
            {
                *(int*)(g_scene_lru_table + block_id * 8 + 4) =
                    g_frame_counter + g_scene_lru_lifetime * 2;
            }
        }

        // Find first unloaded block and start loading it
        if ((g_system_flags & 0x20000000) != 0) continue;
        if (g_scene_lru_counter != 0) continue;

        for (int i = 0; i < 6; i++) {
            int block_id = *(int*)(group + i * 4);
            if (block_id != -1 && !scene_block_slot_is_active(block_id)) {
                char path_buf[88];
                scene_block_build_path(path_buf, g_current_level_id, block_id);
                scene_block_begin_load();
                started_load = true;
                break;
            }
        }
    }

done:
    __ps_restore_regs_4();
}

// ============================================================================
// 0x80051be8 - scene_block_load_sync
// Synchronously loads a block (blocking until complete).
// If sync_mode != 0: pauses music, spins on scene_streaming_update + dvd_poll_tick
// until the load completes, then resumes music.
// Builds path, sets LRU, starts async load, optionally waits for completion.
// ============================================================================
void scene_block_load_sync(int block_index, int sync_mode)
{
    char path_buf[64];

    // If synchronous: wait for any in-progress load to finish first
    if (sync_mode != 0) {
        music_pause();
        while (g_scene_load_active != 0) {
            scene_streaming_update();
            dvd_poll_tick();
        }
    }

    if (g_scene_load_active != 0) return;

    // Build path and set LRU timestamp
    sprintf_path(path_buf,
        "assets/ts/models/level%d/level%d/gamecube/output/block%03d.gcr",
        g_current_level_id, g_current_level_id, block_index);

    *(int*)(g_scene_lru_table + block_index * 8 + 4) =
        g_frame_counter + g_scene_lru_lifetime * 2;

    scene_block_begin_load();

    // If synchronous: spin until load completes
    if (sync_mode != 0) {
        while (g_scene_load_active != 0) {
            scene_streaming_update();
            dvd_poll_tick();
        }
        music_resume();
    }
}

// ============================================================================
// 0x80051cbc - scene_room_is_processed
// Returns non-zero if a room slot has been processed (bit 1 set in state).
// Returns 1 (true) if room streaming is disabled or index < 0x8FF.
// Room state table at g_scene_room_table + index * 0x18.
// ============================================================================
u32 scene_room_is_processed(int room_index)
{
    if (g_scene_room_enabled == 0 || room_index < 0x8FF) {
        return 1;
    }
    return *(u32*)(g_scene_room_table + room_index * 0x18) & 2;
}

// ============================================================================
// 0x80051cec - scene_room_streaming_update
// Main per-frame room streaming update (MASSIVE).
// Algorithm:
//   1. Walk streaming buffer, free completed entries (ref count > 1)
//   2. Clear "needed" flag (bit 0) on all room slots
//   3. Mark rooms as needed based on active viewport visibility
//   4. Unload rooms that are processed (bit 2) but no longer needed (bit 0 clear)
//   5. Process rooms that finished loading (bit 2 set, no async active)
//   6. Start loading rooms that are needed (bit 0) but not processed (bit 1 clear)
//   7. Update room boundary transitions (check adjacent room pairs)
//   8. Poll async load, advance streaming timer
// ============================================================================
void scene_room_streaming_update(void)
{
    if (g_scene_room_enabled == 0) return;

    // 1. Free completed stream buffer entries
    int* buf_ptr = g_scene_room_buffer;
    while (true) {
        if (buf_ptr[1] != 0) {
            buf_ptr[2]++;
            if (buf_ptr[2] > 1) {
                stream_buffer_free(buf_ptr);
            }
        }
        int* next = (int*)((int)buf_ptr + buf_ptr[3]);
        if (next >= g_scene_room_buffer + 0x50000) break;  // 5MB / 4
        buf_ptr = next;
    }

    // 2. Clear "needed" flag on all room slots
    for (int i = 0; i < g_scene_slot_count; i++) {
        int room_state = (int)g_scene_room_table + 0x10000 + i * 0x18;
        *(u32*)(room_state - 0x2818) &= ~0x1;
    }

    // 3. Mark rooms as needed based on viewport visibility
    for (int vp = 0; vp < g_viewport_count; vp++) {
        int viewport = g_viewport_array + vp * 0xFFC;
        if ((*(u32*)(viewport + 0x21C) & 1) == 0) continue;
        if ((*(u32*)(viewport + 0x21C) & 0x20) == 0) continue;

        int* block_list = *(int**)(g_scene_room_block_lists +
                                    *(int*)(viewport + 0x2A0) * 4);
        while (*block_list != 0) {
            int room_state = (int)g_scene_room_table + 0x10000 + *block_list * 0x18;
            *(u32*)(room_state - 0x2830) |= 0x1;  // mark needed
            block_list++;
        }
    }

    // 4. Unload rooms no longer needed
    for (int i = 0; i < g_scene_slot_count; i++) {
        u32* state = (u32*)(g_scene_room_table + (i + 0x8FF) * 0x18);
        if ((*state & 1) == 0 && (*state & 2) != 0) {
            scene_data_unload(*(u32*)(g_scene_slot_array + i * 0xB4 + 0xB4), state, 1);
        }
    }

    // 5. Process rooms that finished loading
    for (int i = 0; i < g_scene_slot_count; i++) {
        u32* state = (u32*)(g_scene_room_table + (i + 0x8FF) * 0x18);
        if ((*state & 4) != 0 && g_scene_load_active == 0) {
            scene_data_process_and_link();
            renderer_notify_scene_updated(*(int*)(g_scene_slot_data_ptrs + i * 4 + 4));
        }
    }

    // 6. Start loading needed but unprocessed rooms (one per frame)
    if (g_scene_load_active == 0) {
        for (int i = 0; i < g_scene_slot_count; i++) {
            u32* state = (u32*)(g_scene_room_table + (i + 0x8FF) * 0x18);
            if ((*state & 1) != 0 && (*state & 2) == 0) {
                char path_buf[64];
                // Path: assets/ts/models/level%d/level%d/gamecube/output/room%d.gcr
                sprintf_path(path_buf,
                    "assets/ts/models/level%d/level%d/gamecube/output/room%d.gcr",
                    g_current_level_id, g_current_level_id, i + 1);
                scene_stub_noop();
                break;
            }
        }
    }

    // 7. Update room boundary transitions
    for (int i = 0; i < g_scene_room_count; i++) {
        int* room_pair = *(int**)(g_scene_room_ptrs + i * 4 + 4);
        if (room_pair[0] > 0 && room_pair[1] > 0) {
            u32* state_a = (u32*)(g_scene_room_table + (room_pair[0] + 0x8FE) * 0x18);
            u32* state_b = (u32*)(g_scene_room_table + (room_pair[1] + 0x8FE) * 0x18);
            if ((*state_a & 2) == 0 || (*state_b & 2) == 0) {
                room_boundary_disable(i + 1);
            } else {
                room_boundary_enable(i + 1);
            }
        }
    }

    // 8. Poll and advance timer
    scene_async_load_poll();
    g_scene_room_timer += g_scene_lru_lifetime;
    if (g_scene_room_timer > 0x78) {
        g_scene_room_timer = 0;
    }

    input_get_buttons_held(0);  // debug input check
}

// ============================================================================
// 0x8005205c - scene_room_preload_all_visible
// Preloads all rooms referenced by active viewports.
// For each visible viewport's block list, builds path and triggers load
// via scene_stub_noop + renderer notify.
// ============================================================================
void scene_room_preload_all_visible(void)
{
    char path_buf[64];

    if (g_scene_room_enabled == 0) return;

    for (int vp = 0; vp < g_viewport_count; vp++) {
        int viewport = g_viewport_array + vp * 0xFFC;
        if ((*(u32*)(viewport + 0x21C) & 1) == 0) continue;

        int* block_list = *(int**)(g_scene_room_block_lists +
                                    *(int*)(viewport + 0x2A0) * 4);
        while (*block_list != 0) {
            sprintf_path(path_buf,
                "assets/ts/models/level%d/level%d/gamecube/output/room%d.gcr",
                g_current_level_id, g_current_level_id, *block_list);
            scene_stub_noop();
            renderer_notify_scene_updated(*(int*)(g_scene_slot_data_ptrs + *block_list * 4));
            block_list++;
        }
    }
}

// ============================================================================
// 0x80052150 - scene_room_unload_all
// Unloads all room data and resets the streaming buffer.
// Cancels any active async load, unloads all processed rooms,
// then reinitializes the streaming buffer as a single 5MB free block.
// ============================================================================
void scene_room_unload_all(void)
{
    if (g_scene_room_enabled == 0) return;

    scene_async_load_cancel();

    // Unload all processed rooms
    for (int i = 0; i < g_scene_slot_count; i++) {
        u32* state = (u32*)(g_scene_room_table + (i + 0x8FF) * 0x18);
        if ((*state & 2) != 0) {
            scene_data_unload(*(u32*)(g_scene_slot_array + i * 0xB4 + 0xB4), state, 0);
        }
        *state = 0;
    }

    // Reset streaming buffer: single 5MB free block
    g_scene_room_buf_ptr = g_scene_room_buffer;
    g_scene_room_buffer[0] = 1;       // active flag
    g_scene_room_buffer[1] = 0;       // ref count
    g_scene_room_buffer[2] = 0;       // age counter
    g_scene_room_buffer[3] = 0x500000; // size (5MB)
    g_scene_room_buffer[4] = 0;       // next ptr
}

// ============================================================================
// 0x80052244 - scene_room_system_init
// Initializes the room streaming system.
// Allocates a 5MB streaming buffer (64-byte aligned) and a 0xEAB0-byte room
// state table. Calls scene_room_unload_all to reset state.
// If room streaming is disabled, sets all pointers to NULL.
// ============================================================================
void scene_room_system_init(void)
{
    if (g_scene_room_enabled == 0) {
        g_scene_room_buffer = NULL;
        g_scene_room_table  = NULL;
        g_scene_load_deferred = 0;
    } else {
        // Allocate 5MB streaming buffer (64-byte aligned)
        g_scene_room_buffer = (int*)mem_alloc_aligned(0x40, 0x500000, 1);

        // Allocate room state table (0xEAB0 bytes)
        g_scene_room_table = (void*)mem_alloc(0xEAB0, 1);
        memset(g_scene_room_table, 0, 0xEAA8);

        scene_room_unload_all();
    }
}
