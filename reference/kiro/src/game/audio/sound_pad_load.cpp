// Sound Pad Zone File Loading
// Batch 142 | Date: 2026-08-11
// Handles loading zone data from .war files and assigning ambient sounds.

#include "common.h"
#include "sound_pad.h"
#include "mem_alloc.h"
#include "asset_load.h"
#include "stream_queue.h"

// ============================================================================
// Address: 0x8006e018 | Name: soundpad_load_zone_file
// MASSIVE: Loads zone data from .war file (DVD or streaming cache).
// Parses header (version 1 = stride 0x1C, version 2 = stride 0x20),
// copies zone nodes into g_zone_node_table, portals into g_portal_table,
// trigger zones into g_trigger_table.
// Version 2 adds angular height wrapping via FUN_80390324 (fmod-like).
// Resolves entity IDs to zone indices via binary search, computes
// inter-zone distances for portals, counts cross/internal portal types.
// ============================================================================
void soundpad_load_zone_file(void) {
    u32 file_size[12]; // local buffer for file info
    int* file_data;

    FUN_803826dc(); // acquire lock

    // Reset all counters
    g_zone_node_count = 0;   // DAT_80611c14
    g_portal_count = 0;      // DAT_80611c10
    DAT_80611c08 = 0;        // active zone count
    g_internal_portal_count = 0; // DAT_80611c04
    g_cross_portal_count = 0;    // DAT_80611c00
    DAT_80611bfc = 0;        // active trigger count
    DAT_80611bf0 = 0;
    DAT_80611bf8 = 0;
    DAT_80611bf4 = 0;

    // Try streaming cache first, fall back to DVD
    int stream_entry = stream_find_pending_by_name();
    if (stream_entry == 0) {
        file_data = (int*)asset_load_from_dvd(/*path*/ 0, 1, file_size);
    } else {
        file_data = *(int**)(stream_entry + 0x10);
        file_size[0] = *(u32*)(stream_entry + 0x14);
    }

    if ((int)file_size[0] <= 0) {
        FUN_80382728();
        return;
    }

    int version = file_data[0];
    g_zone_node_count = file_data[1];
    g_portal_count = file_data[2];
    int* portal_section = NULL;
    int skip_count = 0;

    if (version == 1) {
        // Version 1: zone nodes at stride 0x1C (28 bytes)
        portal_section = file_data + g_zone_node_count * 7 + 3;

        for (int i = 0; i < g_zone_node_count; i++) {
            int src_off = i * 0x1C + 0x0C;
            int dst_off = i * 0x20;
            u32* src = (u32*)((u8*)file_data + src_off);

            // Copy 7 fields from file into zone node table
            *(u32*)((u8*)g_zone_node_table + dst_off + 0x00) = src[0]; // entity_id
            *(u32*)((u8*)g_zone_node_table + dst_off + 0x04) = src[1]; // flags
            *(u16*)((u8*)g_zone_node_table + dst_off + 0x08) = *(u16*)(src + 2); // group
            *(u32*)((u8*)g_zone_node_table + dst_off + 0x0C) = src[3]; // pos_x
            *(u32*)((u8*)g_zone_node_table + dst_off + 0x10) = src[4]; // pos_y
            *(u32*)((u8*)g_zone_node_table + dst_off + 0x14) = src[5]; // pos_z
            *(u32*)((u8*)g_zone_node_table + dst_off + 0x18) = src[6]; // height
            *(u32*)((u8*)g_zone_node_table + dst_off + 0x1C) = 2;     // version marker

            if ((*(u32*)((u8*)g_zone_node_table + dst_off + 4) & 1) != 0) {
                DAT_80611c08++;
            }
        }
    } else if (version == 2) {
        // Version 2: zone nodes at stride 0x20 (32 bytes) with height wrapping
        float sentinel = FLOAT_80613f58;
        float wrap_range = FLOAT_80613f78;
        portal_section = file_data + g_zone_node_count * 8 + 3;

        for (int i = 0; i < g_zone_node_count; i++) {
            int off = i * 0x20;
            u32* src = (u32*)((u8*)file_data + off + 0x0C);

            *(u32*)((u8*)g_zone_node_table + off + 0x00) = src[0];
            *(u32*)((u8*)g_zone_node_table + off + 0x04) = src[1];
            *(u16*)((u8*)g_zone_node_table + off + 0x08) = *(u16*)(src + 2);
            *(u32*)((u8*)g_zone_node_table + off + 0x0C) = src[3];
            *(u32*)((u8*)g_zone_node_table + off + 0x10) = src[4];
            *(u32*)((u8*)g_zone_node_table + off + 0x14) = src[5];
            *(u32*)((u8*)g_zone_node_table + off + 0x18) = src[6];

            // Wrap height into valid range
            float h = *(float*)((u8*)g_zone_node_table + off + 0x18);
            float wrapped = FUN_80390324((h - sentinel) / wrap_range);
            *(float*)((u8*)g_zone_node_table + off + 0x18) = -(wrap_range * wrapped - h);

            *(u32*)((u8*)g_zone_node_table + off + 0x1C) = src[7];

            if ((*(u32*)((u8*)g_zone_node_table + off + 4) & 1) != 0) {
                DAT_80611c08++;
            }
        }
    }

    // Parse portals: resolve entity IDs to zone indices
    int portal_count = g_portal_count;
    for (int i = 0; i < portal_count; i++) {
        u32* src_portal = (u32*)((u8*)portal_section + i * 0x14);

        int zone_a = soundpad_find_zone_by_id(src_portal[1]);
        int zone_b = soundpad_find_zone_by_id(src_portal[2]);

        if (zone_a == -1 || zone_b == -1) {
            skip_count++;
            g_portal_count--;
        } else {
            int dst_off = (i - skip_count) * 0x14;
            u32* dst = (u32*)((u8*)g_portal_table + dst_off);

            dst[0] = (src_portal[0] & 0xFFFF) | 0x10000;
            dst[1] = soundpad_find_zone_by_id(src_portal[1]);
            dst[2] = soundpad_find_zone_by_id(src_portal[2]);

            // Compute distance between connected zones
            float dist = (float)FUN_8006f0e0(dst[1], dst[2]);
            dst[4] = *(u32*)&dist;
            dst[3] = *(u32*)&FLOAT_80613f5c; // default weight

            // Count portal type (cross-zone or internal)
            if (dst[0] & 0x10000) {
                s16 group_a = *(s16*)((u8*)g_zone_node_table + dst[1] * 0x20 + 8);
                s16 group_b = *(s16*)((u8*)g_zone_node_table + dst[2] * 0x20 + 8);
                if (group_a == group_b) {
                    g_internal_portal_count++;
                } else {
                    g_cross_portal_count++;
                }
            }
        }
    }

    // Parse trigger zones section
    portal_section += g_portal_count * 5;
    int trigger_count = *portal_section;
    DAT_80611c0c = trigger_count; // g_trigger_count

    if (version == 1) {
        for (int i = 0; i < trigger_count; i++) {
            u32* src = (u32*)((u8*)portal_section + i * 0x1C + 4);
            u32* dst = (u32*)((u8*)g_trigger_table + i * 0x20);

            dst[0] = src[0]; dst[1] = src[1];
            *(u16*)(dst + 2) = *(u16*)(src + 2);
            dst[3] = src[3]; dst[4] = src[4]; dst[5] = src[5]; dst[6] = src[6];
            dst[7] = 0;

            if ((dst[1] & 1) != 0) {
                DAT_80611bfc++;
                int zone = soundpad_resolve_entity_to_zone((int)*(s16*)(dst + 2));
                *(u32*)(zone + 4) |= 0x200;
            }
        }
    } else if (version == 2) {
        // Same as v1 but with height wrapping
        for (int i = 0; i < trigger_count; i++) {
            u32* src = (u32*)((u8*)portal_section + i * 0x20 + 4);
            u32* dst = (u32*)((u8*)g_trigger_table + i * 0x20);

            dst[0] = src[0]; dst[1] = src[1];
            *(u16*)(dst + 2) = *(u16*)(src + 2);
            dst[3] = src[3]; dst[4] = src[4]; dst[5] = src[5]; dst[6] = src[6];

            // Height wrap
            float h = *(float*)(dst + 6);
            float wrapped = FUN_80390324((h - FLOAT_80613f58) / FLOAT_80613f78);
            *(float*)(dst + 6) = -(FLOAT_80613f78 * wrapped - h);

            dst[7] = src[7];

            if ((dst[1] & 1) != 0) {
                DAT_80611bfc++;
                int zone = soundpad_resolve_entity_to_zone((int)*(s16*)(dst + 2));
                *(u32*)(zone + 4) |= 0x200;
            }
        }
    }

    FUN_80382728(); // release lock
}

// ============================================================================
// Address: 0x8006e590 | Name: soundpad_assign_ambient_sounds
// Assigns random ambient sound IDs to active zone nodes and trigger zones.
// For primary zones: RNG sound ID, and if flagged 0x2000 (mapmaker) or non-mapmaker
// level, elevates the Y position for overhead ambient effect.
// For trigger zones: just assigns the random sound ID.
// ============================================================================
void soundpad_assign_ambient_sounds(void) {
    for (int i = 0; i < g_zone_node_count; i++) {
        int off = i * 0x20;
        if ((*(u32*)((u8*)g_zone_node_table + off + 4) & 1) == 0) continue;

        if (FUN_80083ac0() != 0) continue; // skip if audio system busy

        u16 sound_id = FUN_8009994c(FLOAT_80613f7c, FLOAT_80613f68);
        *(u16*)((u8*)g_zone_node_table + off + 10) = sound_id;

        if (sound_id > 0 &&
            (DAT_80612614 != 100 || (*(u32*)((u8*)g_zone_node_table + off + 4) & 0x2000) != 0)) {
            *(float*)((u8*)g_zone_node_table + off + 0x10) = FLOAT_80613f88 + FLOAT_80610494;
        }
    }

    // Assign to trigger zones too
    for (int i = 0; i < DAT_80611c0c; i++) {
        int off = i * 0x20;
        if ((*(u32*)((u8*)g_trigger_table + off + 4) & 1) == 0) continue;

        u16 sound_id = FUN_8009994c(FLOAT_80613f7c, FLOAT_80613f58);
        *(u16*)((u8*)g_trigger_table + off + 10) = sound_id;
    }
}

// ============================================================================
// Address: 0x8006e6d8 | Name: soundpad_validate_connectivity
// Debug/validation pass: iterates zones and portals but the inner loops
// have been optimized to no-ops. Likely stripped in release build.
// Only bails early if zone count == 4 (special case).
// ============================================================================
void soundpad_validate_connectivity(void) {
    if (g_zone_node_count == 4) return;

    // Iterate zones + portals (optimized out in release)
    for (int i = 0; i < g_zone_node_count; i++) {
        if ((*(u32*)((u8*)g_zone_node_table + i * 0x20 + 4) & 1) != 0) {
            // inner portal loop body removed by optimizer
        }
    }
    // Another portal loop (also optimized out)
}
