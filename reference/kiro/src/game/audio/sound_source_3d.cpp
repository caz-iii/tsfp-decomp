// Address: 0x800a5d34 | Name: sound_source_compute_dot_distance | Date: 2026-08-11
// Address: 0x800a6070 | Name: sound_source_priority_insert | Date: 2026-08-11
// Address: 0x800a6114 | Name: sound_source_update_tick | Date: 2026-08-11
// Address: 0x800a6640 | Name: sound_source_find_reflection | Date: 2026-08-11
//
// 3D positional sound source management system.
// Manages a 6-slot priority list of active sound sources per player.
// Each slot is 0xCC bytes within the player data block at offset 0xEC8.

#include "common.h"

// ============================================================================
// 0x800a5d34 - sound_source_compute_dot_distance
// Computes the dot product of two vectors (direction and position), normalized
// by the magnitude of the position vector. Stores distance at param_3+0xC and
// dot product (attenuation factor) at param_3+0x8.
// Used by the 3D sound system to determine directional volume/panning.
// 3 xrefs.
// ============================================================================
void sound_source_compute_dot_distance(u32 unused1, u32 unused2, int output) {
    float* direction;
    float* position;
    float magnitude;

    // Context: direction and position come from paired-single register context
    direction = (float*)FUN_803826f0(); // OS lock returns both vectors
    position = ...; // second vector from context

    // Compute magnitude of position vector
    float mag_sq = position[0]*position[0] + position[1]*position[1] + position[2]*position[2];
    magnitude = (float)FUN_80131538((double)mag_sq); // sqrtf
    *(float*)(output + 0xc) = magnitude;

    // Compute normalized dot product (direction . position / |position|)
    float inv_mag = 1.0f / magnitude;
    *(float*)(output + 0x8) =
        direction[0] * position[0] * inv_mag +
        direction[1] * position[1] * inv_mag +
        direction[2] * position[2] * inv_mag;

    FUN_8038273c(); // OS unlock
}

// ============================================================================
// 0x800a6070 - sound_source_priority_insert
// Inserts a sound source entity into a sorted 6-slot priority list.
// Sorted by distance (descending) - closest sounds are at lowest indices.
// If all 6 slots are full and new source is farther, it's discarded.
// Uses a static table at DAT_80561d50 (entities) and DAT_80561d68 (distances).
// param_1: distance (float as double), param_2: entity pointer.
// 2 xrefs (only from sound_source_update_tick).
// ============================================================================
void sound_source_priority_insert(double distance, u32 entity_ptr) {
    int insert_idx = 0;
    int offset = 0;
    int remaining = 6;

    // Find insertion point - walk until we find an empty slot or a farther source
    while ((*(int*)(&g_sound_priority_entities + offset) != 0) &&
           (distance < (double)*(float*)(&g_sound_priority_distances + offset))) {
        insert_idx++;
        offset += 4;
        remaining--;
        if (remaining == 0) return; // list full, all closer
    }

    // Shift existing entries down to make room
    int shift_count = 6 - (insert_idx + 1);
    if (insert_idx + 1 < 6) {
        int shift_offset = 0x14; // start from end
        do {
            *(u32*)(&g_sound_priority_entities + shift_offset) =
                *(u32*)(&g_sound_priority_entities + shift_offset - 4);
            *(u32*)(&g_sound_priority_distances + shift_offset) =
                *(u32*)(&g_sound_priority_distances + shift_offset - 4);
            shift_count--;
            shift_offset -= 4;
        } while (shift_count != 0);
    }

    // Insert new entry
    *(u32*)(&g_sound_priority_entities + offset) = entity_ptr;
    *(float*)(&g_sound_priority_distances + offset) = (float)distance;
}

// ============================================================================
// 0x800a6114 - sound_source_update_tick
// MASSIVE per-frame 3D sound source management.
// Called once per frame from the player update system (FUN_8025967c).
// Manages 6 active sound source slots (each 0xCC bytes at player+0xEC8):
//   +0x00: entity pointer (0 = empty)
//   +0x04: distance (float, for priority sorting)
//   +0x08: volume (float, fades in/out)
//   +0x0C: active flag
//   +0x10: fade direction (1 = fading in)
//   +0x14: reflection count
//   +0x18+: reflection entries (0x24 bytes each)
//
// Algorithm:
// 1. Clear priority list and per-slot distances to max
// 2. If player has active sound sources (count > 0):
//    a. Skip if player flag bit 0 set (muted)
//    b. Scan entity pool for entities with collision volume > 0
//    c. Insert into 6-slot priority list by distance
//    d. Also scan event-type sources (soundpad reverb)
//    e. Cap active count to min(6, player sound count)
//    f. For each priority entry:
//       - If already in a slot: fade volume toward target
//       - If new: find empty or lowest-priority slot, assign
//    g. Mark slots targeting current listener entity for fade-in
// 3. Per-slot maintenance:
//    - If distance == max (stale): fade out, destroy when volume <= 0
//    - If entity matches listener: set fade-in flag
//    - Fade volume toward 1.0 or 0.0 based on direction
// 4. Per-slot sound dispatch:
//    - Type 8 entities: compute position from entity + bone offset
//    - Other entities with callback: invoke callback
//    - Others: use entity position directly
//    - Call FUN_800a5dd4 for spatial sound rendering
// 1 xref.
// ============================================================================
void sound_source_update_tick(void) {
    int i, j;
    int slot_offset;
    float max_dist = FLOAT_80614844; // very large value
    float dt_scale;
    int* player = g_active_player;

    FUN_803826f0(); // OS lock

    // Compute frame delta scale
    u32 frame_counter = DAT_806109c0 ^ 0x80000000;
    dt_scale = FLOAT_806108ec * (1.0f / (float)((double)frame_counter - DOUBLE_806147a8));

    // Clear priority list and per-slot target distances
    for (i = 0; i < 6; i++) {
        g_sound_priority_entities[i] = 0;
        g_sound_priority_distances[i] = max_dist;
        *(float*)((int)player + i * 0xcc + 0xecc) = max_dist;
    }

    if (player[0x3b0] > 0) { // active sound source count
        // Scan entity pool for valid sound sources
        if ((player[0x3af] & 1) == 0) {
            for (i = 0; i < g_max_entities; i++) {
                int* entity = g_entity_ptrs[i];
                if (entity != NULL) {
                    int* slot_data = (int*)(player[899] + entity[2] * 0x10);
                    if ((entity[2] != *player) && ((*slot_data & 1) != 0)) {
                        if (0.0f < *(float*)(*entity + 0x1cc)) {
                            sound_source_priority_insert(
                                (double)*(float*)(slot_data + 2), *entity);
                        }
                    }
                }
            }
        }

        // Scan soundpad/event sources
        int reverb_count = soundpad_get_current_reverb_state();
        for (i = 0; i < reverb_count; i++) {
            int event_data = event_get_type_data(i);
            int* slot_data = (int*)(player[899] + (i + 0x20) * 0x10);
            if (((*slot_data & 1) != 0) && (0.0f < *(float*)(event_data + 0x1cc))) {
                sound_source_priority_insert((double)*(float*)(slot_data + 2), event_data);
            }
        }

        // Cap active count
        int max_sources = 6;
        if (player[0x3b0] < 6) max_sources = player[0x3b0];
        player[0x3b0] = max_sources;

        // Assign priority entries to slots (fade in/out logic)
        // ... (extensive slot matching, fade volume management)
    }

    // Per-slot maintenance: fade volumes, clear stale entries
    int active_listener_count = 0;
    for (i = 0; i < 6; i++) {
        slot_offset = i * 0xcc;
        int slot_entity = *(int*)((int)player + slot_offset + 0xec8);
        if (slot_entity != 0) {
            *(int*)((int)player + slot_offset + 0xedc) = 0; // clear reflection count

            if (max_dist == *(float*)((int)player + slot_offset + 0xecc)) {
                // Stale - fade out
                *(float*)((int)player + slot_offset + 0xed0) -= FLOAT_80614810 * dt_scale;
                if (*(float*)((int)player + slot_offset + 0xed0) <= 0.0f) {
                    *(int*)((int)player + slot_offset + 0xec8) = 0; // destroy
                }
                // Also fade secondary channel
                *(float*)((int)player + slot_offset + 0xed8) -= FLOAT_80614810 * dt_scale;
            } else if (slot_entity == player[0x387]) {
                // Matches listener entity - mark for fade-in
                if (*(int*)((int)player + slot_offset + 0xed4) == 0) {
                    *(int*)((int)player + slot_offset + 0xed4) = 1;
                    *(float*)((int)player + slot_offset + 0xed8) = 0.0f;
                }
                active_listener_count++;
            }

            // Fade secondary volume toward 1.0
            if ((max_dist != *(float*)((int)player + slot_offset + 0xecc)) &&
                (*(int*)((int)player + slot_offset + 0xed4) != 0) &&
                (active_listener_count <= player[0x3b0])) {
                *(float*)((int)player + slot_offset + 0xed8) += dt_scale;
                if (1.0f < *(float*)((int)player + slot_offset + 0xed8)) {
                    *(float*)((int)player + slot_offset + 0xed8) = 1.0f;
                }
            }
        }
    }

    // Spatial sound dispatch per slot
    for (i = 0; i < 6; i++) {
        slot_offset = i * 0xcc;
        int slot_entity = *(int*)((int)player + slot_offset + 0xec8);
        if (slot_entity != 0) {
            if (*(int*)(slot_entity + 0x20) == 8) {
                // Character entity: use position + bone height offset
                float pos[3];
                pos[0] = *(float*)(slot_entity + 0x64);
                pos[1] = *(float*)(*(int*)(slot_entity + 0x7c) + 0xbc0); // bone Y
                pos[2] = *(float*)(slot_entity + 0x6c);
                FUN_800a5dd4(FLOAT_80614850, FLOAT_80614850,
                            (int)player + slot_offset + 0xec8, pos, 1);
            } else if (*(void**)(slot_entity + 0x178) == NULL) {
                // No callback: use entity position directly
                FUN_800a5dd4(FLOAT_806147f4, FLOAT_806147f4,
                            (int)player + slot_offset + 0xec8, slot_entity + 0x64, 1);
            } else {
                // Custom callback
                (*(void(**)(int))(slot_entity + 0x178))((int)player + slot_offset + 0xec8);
            }
        }
    }

    FUN_8038273c(); // OS unlock
}

// ============================================================================
// 0x800a6640 - sound_source_find_reflection
// Searches the player's 6 sound source slots for a specific entity (param_2),
// then within that slot's reflection entries finds one with type == 3.
// Returns the reflection's position (3 floats) into param_3.
// Returns 1 if found, 0 if not.
// Used by the AI system (FUN_800be4c8) to locate sound reflection points.
// 1 xref.
// ============================================================================
int sound_source_find_reflection(int player_data, int target_entity, float* out_position) {
    for (int i = 0; i < 6; i++) {
        int slot_base = player_data + i * 0xcc;

        if ((target_entity != 0) &&
            (*(int*)(slot_base + 0xec8) == target_entity) &&
            (*(int*)(slot_base + 0xed4) != 0)) {

            int reflection_count = *(int*)(slot_base + 0xedc);
            if (reflection_count > 0) {
                int entry_offset = 0;
                do {
                    int entry = slot_base + entry_offset + 0xee0;
                    if (*(int*)(entry + 0x14) == 3) { // type 3 = reflection
                        out_position[0] = *(float*)(entry + 0x18);
                        out_position[1] = *(float*)(entry + 0x1c);
                        out_position[2] = *(float*)(entry + 0x20);
                        return 1;
                    }
                    entry_offset += 0x24;
                    reflection_count--;
                } while (reflection_count != 0);
            }
        }
    }
    return 0;
}
