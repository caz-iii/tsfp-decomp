// Address: 0x8006ba78 - 0x8006bf0c | Name: sound_pad | Date: 2026-08-10
// Sound pad / sound zone system: manages spatial audio zones that apply
// reverb and ambient effects to entities within defined regions.
// Also includes two cutscene helper functions at the start of this range.

#include "common.h"
#include "audio/sound.h"
#include "level/level_data.h"
#include "stream/stream_queue.h"

// ============================================================================
// Globals
// ============================================================================
// DAT_80611b8c - Primary sound pad entity buffer (stride 0x20, 0x12c00 bytes)
// DAT_80611b90 - Secondary sound pad buffer (88000 bytes)
// DAT_80611b94 - Tertiary sound pad buffer (0xa000 mapmaker / 0x2bc0 normal)
// DAT_80611b98 - Sound zone lookup table (0x280 bytes)
// DAT_80611b9c - Extended pad buffer (0x19c80, player count > 10)
// DAT_80611ba8 - Sound zone assignment table (stride 0x30)
// DAT_80611be0 - Vertex buffer write offset
// DAT_80611be4 - Vertex buffer pointer
// DAT_80611c0c - Tertiary pad count
// DAT_80611c10 - Active sound pad count (returned by get_active_count)
// DAT_80611c14 - Primary pad count

// ============================================================================
// 0x8006ba78 - cutscene_speed_accumulate_check
// Accumulates cutscene playback speed each frame. Returns 1 if speed
// exceeds the threshold (vsync_period * factor), indicating the cutscene
// should wait. Also checks music sync if stop was requested.
// Called by: cutscene_playback_update
// ============================================================================
int cutscene_speed_accumulate_check(void) {
    g_cutscene_speed += g_speed_delta;  // FLOAT_80611b80 += FLOAT_806108ec

    f32 vsync_period = (f32)(u32)g_vsync_counter;  // DAT_806109c0
    f32 threshold = SPEED_FACTOR * vsync_period;   // FLOAT_80613f54

    if (g_cutscene_speed <= threshold) {
        return 0;  // still within speed budget
    }
    if (g_cutscene_stop_requested != 0) {  // DAT_80610450
        int music_ready = music_update_tick();
        if (music_ready != 0) {
            return 0;  // music sync OK, allow continue
        }
    }
    return 1;  // over budget, should wait
}

// ============================================================================
// 0x8006baf8 - cutscene_bone_inverse_precompute
// Precomputes inverse bone matrices for the cutscene entity. Allocates
// a buffer of (bone_count * 0x40) bytes and inverts each bone matrix
// referenced by the skeleton's bone index table.
// Called from cutscene render pipeline.
// ============================================================================
void cutscene_bone_inverse_precompute(void) {
    int ctx = os_get_context_lower();  // FUN_803826e8
    int instance = *(int*)(ctx + 4);

    // Get bone count from skeleton reference
    int bone_count = 0;
    if (*(int*)(instance + 0x80) != 0) {
        int skel_ref = *(int*)(instance + 0x80);
        int anim_id = *(int*)(skel_ref + 8);
        bone_count = *(int*)(*(int*)(g_anim_table + (s16)g_anim_offsets[anim_id] * 4) + 0x14);
    }

    // Allocate and compute inverse matrices
    if (*(int*)(instance + 0x10) != 0 && *(int*)(instance + 8) != 0) {
        int inv_buf = mem_advance_secondary(bone_count * 0x40);
        *(int*)(instance + 0x14) = inv_buf;

        int* bone_indices = *(int**)(*(int*)(instance + 0x84));
        int src_offset = 0;
        int dst_offset = 0;
        for (int i = 0; i < bone_count; i++) {
            f32* src_mtx = (f32*)(*(int*)(instance + 0x10) + bone_indices[i] * 0x40);
            f32* dst_mtx = (f32*)(inv_buf + dst_offset);
            mtx_invert_3x4(src_mtx, dst_mtx);  // FUN_80130b90
            dst_offset += 0x40;
            src_offset += 4;
        }
    }
    os_restore_context();  // FUN_80382734
}

// ============================================================================
// 0x8006bbd4 - soundpad_stub_nop
// Empty stub function. Used as overflow handler in zone assignment.
// ============================================================================
void soundpad_stub_nop(void) {
    // intentionally empty
}

// ============================================================================
// 0x8006bbd8 - soundpad_alloc_vertex_buffer
// Allocates a vertex buffer for sound pad geometry rendering.
// Size = (count * 6) aligned to 16 bytes. Resets write offset to 0.
// Called by: scene_system_build_all_blocks
// ============================================================================
void soundpad_alloc_vertex_buffer(int vertex_count) {
    g_soundpad_vtx_offset = 0;   // DAT_80611be0
    int size = (vertex_count * 6 + 0xF) & ~0xF;
    g_soundpad_vtx_buffer = mem_alloc(size, 1);  // DAT_80611be4
}

// ============================================================================
// 0x8006bc14 - soundpad_reset_vertex_offset
// Resets the vertex buffer write offset to 0. Called when geometry is
// flushed or rebuilt without re-allocating the buffer.
// ============================================================================
void soundpad_reset_vertex_offset(void) {
    g_soundpad_vtx_offset = 0;  // DAT_80611be0
}

// ============================================================================
// 0x8006bc20 - soundpad_get_active_count
// Returns the current active sound pad count.
// ============================================================================
int soundpad_get_active_count(void) {
    return g_soundpad_active_count;  // DAT_80611c10
}

// ============================================================================
// 0x8006bc28 - soundpad_get_buffer
// Returns the secondary sound pad buffer pointer.
// ============================================================================
int soundpad_get_buffer(void) {
    return g_soundpad_secondary_buf;  // DAT_80611b90
}

// ============================================================================
// 0x8006bc30 - soundpad_build_data_path
// Builds the asset path for the level's sound pad data file.
// Format: "pad_data_level_d.war" (string at 0x803fee88).
// Queues the file via the streaming system and returns the data pointer.
// ============================================================================
void* soundpad_build_data_path(void) {
    // Build path with level ID and queue for streaming
    sprintf_and_queue(&g_soundpad_data, "pad_data_level_d_war", g_current_level_id);
    return &g_soundpad_data;  // DAT_80537588
}

// ============================================================================
// 0x8006bc70 - soundpad_system_init
// Initializes the entire sound pad system. Allocates multiple buffers:
//   - Primary entity buffer: 0x12C00 bytes (stride 0x20)
//   - Secondary buffer: 88000 bytes
//   - Extended buffer: 0x19C80 bytes (if player count > 10)
//   - Tertiary buffer: 0xA000 (mapmaker mode 100) or 0x2BC0 (normal)
//   - Zone lookup table: 0x280 bytes
// In mapmaker mode (level 100): zeroes all counters.
// Otherwise: builds data path and queues streaming asset load.
// ============================================================================
void soundpad_system_init(void) {
    // Primary entity buffer (3600 entries * stride 0x20 = 0x12C00)
    g_soundpad_primary_buf = mem_alloc(0x12C00, 1);
    memset(g_soundpad_primary_buf, 0, 0x12C00);

    // Secondary buffer (88000 bytes for zone geometry/data)
    g_soundpad_secondary_buf = mem_alloc(88000, 1);
    memset(g_soundpad_secondary_buf, 0, 88000);

    // Extended buffer for large player counts
    if (g_max_players != 10) {  // DAT_805015bd
        g_soundpad_extended_buf = mem_alloc(0x19C80, 1);
        memset(g_soundpad_extended_buf, 0, 0x19C80);
    }

    // Tertiary buffer (size depends on mapmaker mode)
    if (g_current_level_id == 100) {  // DAT_80612614 - mapmaker
        g_soundpad_tertiary_buf = mem_alloc(0xA000, 1);
        memset(g_soundpad_tertiary_buf, 0, 0xA000);
    } else {
        g_soundpad_tertiary_buf = mem_alloc(0x2BC0, 1);
        memset(g_soundpad_tertiary_buf, 0, 0x2BC0);
    }

    // Zone lookup table
    g_soundpad_zone_table = mem_alloc(0x280, 1);
    memset(g_soundpad_zone_table, 0, 0x280);

    // Mode-specific initialization
    if (g_current_level_id == 100) {
        // Mapmaker: zero all counters
        g_soundpad_counter_0 = 0;  // DAT_80611c14 thru DAT_80611c18
        g_soundpad_active_count = 0;
        g_soundpad_count_c = 0;
        g_soundpad_count_d = 0;
        g_soundpad_count_e = 0;
        g_soundpad_count_f = 0;
        g_soundpad_count_g = 0;
        g_soundpad_count_h = 0;
    } else {
        // Normal level: load sound pad data from .war file
        soundpad_build_data_path();
        stream_queue_raw_asset();
    }
}

// ============================================================================
// 0x8006bdb8 - soundpad_update
// Per-frame sound pad update. Clears dirty flags on all primary and tertiary
// pad entries (bit mask 0xBFDFFFFF at offset +4), zeroes the zone table,
// then rebuilds zone assignments from level data.
// For each sound zone defined in the level: copies the zone ID and calls
// the zone entity resolver. Finally dispatches to soundpad_assign_entities.
// ============================================================================
void soundpad_update(void) {
    os_get_context_lower();  // FUN_803826ec (push context)

    // Clear dirty/active flags on primary pads
    int offset = 0;
    for (int i = 0; i < g_soundpad_primary_count; i++) {  // DAT_80611c14
        *(u32*)(g_soundpad_primary_buf + offset + 4) &= 0xBFDFFFFF;
        offset += 0x20;
    }

    // Clear dirty/active flags on tertiary pads
    offset = 0;
    for (int i = 0; i < g_soundpad_tertiary_count; i++) {  // DAT_80611c0c
        *(u32*)(g_soundpad_tertiary_buf + offset + 4) &= 0xBFDFFFFF;
        offset += 0x20;
    }

    // Reset zone lookup table
    memset(g_soundpad_zone_table, 0, 0x280);

    // Load sound zone definitions from level data
    int level_info = level_get_info_cached(g_current_level_id);
    if (level_info == 0) goto done;

    level_info = level_get_info_cached(g_current_level_id);
    int zone_count = *(int*)(level_info + 0x68);
    if (zone_count <= 0) goto done;

    // Allocate temp zone assignment table
    int zone_table_size = *(int*)(g_level_def_cached + 0x68) * 0x30;
    memset(g_soundpad_zone_assign, 0, zone_table_size);

    // Copy zone IDs and resolve entities
    int src_offset = 0;
    int dst_offset = 0;
    for (int i = 0; i < zone_count; i++) {
        int* dst = (int*)(g_soundpad_zone_assign + dst_offset);
        int src_data = level_get_info_cached(g_current_level_id);
        *dst = *(int*)(*(int*)(src_data + 0x64) + src_offset);  // zone ID

        // Resolve zone entity reference
        int entity_ref = soundpad_resolve_zone_entity();  // FUN_8006fef8
        dst[0xB] = entity_ref;  // store at +0x2C in zone entry

        src_offset += 0x58;
        dst_offset += 0x30;
    }

    // Assign entities to zones
    soundpad_assign_entities();  // FUN_8006bf0c

done:
    os_restore_context();  // FUN_80382738
}

// ============================================================================
// 0x8006bf0c - soundpad_assign_entities
// Maps entities to their containing sound zones. Iterates all active sound
// pads (primary + tertiary), checks if each has the ACTIVE|VALID flags
// (0x101 at offset +4), looks up which zone it belongs to, and assigns
// it to that zone's entity list (max 10 entities per zone, stride 0x30).
// Overflows are silently dropped via soundpad_stub_nop.
// ============================================================================
void soundpad_assign_entities(void) {
    os_get_context_lower();  // FUN_803826ec

    // Allocate temp tracking buffer (1 byte per zone, rounded to 16)
    int level_info = level_get_info_cached(g_current_level_id);
    int zone_count = *(int*)(level_info + 0x68);
    int track_buf = mem_alloc((zone_count + 0xF) & ~0xF, 0);

    level_info = level_get_info_cached(g_current_level_id);
    memset(track_buf, 0, (*(int*)(level_info + 0x68) + 0xF) & ~0xF);

    // Iterate all pads: primary (0..c14) then tertiary (c14..c14+c0c)
    int total = g_soundpad_tertiary_count + g_soundpad_primary_count;
    int primary_offset = 0;

    for (int i = 0; i < total; i++) {
        int* pad_entry;
        if (i < g_soundpad_primary_count) {
            pad_entry = (int*)(g_soundpad_primary_buf + primary_offset);
        } else {
            pad_entry = (int*)(g_soundpad_tertiary_buf + (i - g_soundpad_primary_count) * 0x20);
        }

        // Check ACTIVE | VALID flags
        if ((pad_entry[1] & 0x101) == 0x101) {
            int entity_id = soundpad_get_entity_id((int)pad_entry);  // FUN_8006fb84
            int zone_idx = soundpad_find_zone_for_entity(entity_id);  // FUN_8006fbd8

            if (zone_idx != -1) {
                u8 slot = *(u8*)(track_buf + zone_idx);
                if (slot < 10) {
                    // Assign entity to zone slot
                    int zone_base = g_soundpad_zone_assign;
                    *(u8*)(track_buf + zone_idx) = slot + 1;
                    *(int*)(zone_base + zone_idx * 0x30 + (u32)slot * 4 + 4) = *pad_entry;
                } else {
                    soundpad_stub_nop();  // overflow: max 10 per zone
                }
            }
        }
        primary_offset += 0x20;
    }

    mem_free_top(track_buf);
    os_restore_context();  // FUN_80382738
}
