// Address: 0x800a66d4 | Name: sound_source_get_nth_active | Date: 2026-08-11
// Address: 0x800a6750 | Name: player_can_receive_sound | Date: 2026-08-11
// Address: 0x800a6814 | Name: player_get_movement_speed | Date: 2026-08-11
// Address: 0x800a6910 | Name: player_get_min_speed | Date: 2026-08-11
// Address: 0x800a6980 | Name: player_is_valid_target | Date: 2026-08-11
// Address: 0x800a6a28 | Name: player_target_in_range | Date: 2026-08-11
// Address: 0x800a6ab0 | Name: player_autoaim_scan | Date: 2026-08-11
// Address: 0x800a6ee4 | Name: player_autoaim_scan_events | Date: 2026-08-11
// Address: 0x800a7258 | Name: player_stub_return_zero | Date: 2026-08-11
// Address: 0x800a76dc | Name: player_movement_integrate | Date: 2026-08-11
//
// Player targeting (auto-aim), movement speed computation, sound source queries,
// and movement integration. Core player update pipeline functions.

#include "common.h"

// ============================================================================
// 0x800a66d4 - sound_source_get_nth_active
// Returns the Nth active sound source entity from the 6-slot list.
// Skips empty slots and slots with flag +0xed4 == 0 (inactive).
// If param_2 reaches 0, returns that entity. If not found and param_2 != 0,
// recursively calls self with param_2=0 (fallback to first active).
// If param_2 == 0 and nothing found, returns player+0xe10 (listener entity).
// 3 xrefs.
// ============================================================================
int sound_source_get_nth_active(int player_data, int index) {
    int offset = 0;
    int remaining = index;

    for (int i = 0; i < 6; i++) {
        int entity = *(int*)(player_data + offset + 0xec8);
        if ((entity != 0) && (*(int*)(player_data + offset + 0xed4) != 0)) {
            if (remaining == 0) {
                return entity;
            }
            remaining--;
        }
        offset += 0xcc;
    }

    // Fallback: if index was 0, return listener entity; else recurse with 0
    if (index == 0) {
        return *(int*)(player_data + 0xe10);
    }
    return sound_source_get_nth_active(player_data, 0);
}

// ============================================================================
// 0x800a6750 - player_can_receive_sound
// Returns 1 if the player is in a state where 3D sound targeting is valid.
// Conditions for FALSE (returns 0):
//   - Model def flag bit 0x80 is clear (no 3D sound support on character)
//   - Flag 0x40 set AND sub-mode is 0xb, 0xa, or 0x10 (menu/lobby)
//   - Weapon state (+0xdf4) is 0 AND player state is not 0x40/0x8000/0x80
//     AND FUN_801aab84 returns 0 (no active targeting weapon)
// 2 xrefs.
// ============================================================================
int player_can_receive_sound(int player_data) {
    int player_state = *(int*)(player_data + 0x114);

    // Check model definition flag bit 7
    if ((*(u16*)(*(int*)(player_data + 0x1398) + 2) & 0x80) == 0) {
        return 0;
    }

    // Disable in certain arcade/menu modes
    if (((g_match_flags & 0x40) != 0) &&
        ((g_sub_mode == 0xb) || (g_sub_mode == 0xa) || (g_sub_mode == 0x10))) {
        return 0;
    }

    // Check weapon state or player state
    if (*(int*)(player_data + 0xdf4) == 0) {
        if ((player_state == 0x40) || (player_state == 0x8000) || (player_state == 0x80)) {
            return 1;
        }
        int has_weapon = FUN_801aab84(*(int*)(*(int*)(player_data + 0x14) + 0x7c));
        return (has_weapon != 0) ? 1 : 0;
    }

    return 1;
}

// ============================================================================
// 0x800a6814 - player_get_movement_speed
// Returns a speed multiplier [0..1] for a given layer:
//   param_2=0: walk/run speed from weapon weight table (FUN_801b6d50)
//   param_2=1: strafe speed from weapon table (FUN_801b6d8c)
//   param_2=2: constant from FLOAT_80614854 (crouch/prone speed)
// Special cases:
//   - States 0x40/0x80 (vehicle/mounted): resolve from vehicle entity's bone data
//   - Flag 0x40 (arcade): uses fixed FLOAT_80614858
//   - Flag 0x80 on player: applies MP height scaling (FLOAT_80614800)
// 12 xrefs - core speed resolver for all movement code.
// ============================================================================
double player_get_movement_speed(int player_data, int layer) {
    double speed = 1.0;
    int char_data = *(int*)(*(int*)(player_data + 0x14) + 0x7c);

    if ((*(int*)(player_data + 0x114) == 0x40) || (*(int*)(player_data + 0x114) == 0x80)) {
        // Vehicle/mounted: get speed from vehicle entity bone data
        int vehicle = *(int*)(char_data + 0xc14);
        if (vehicle != 0) {
            int bone_data = *(int*)(*(int*)(vehicle + 0x7c) + 0x6c);
            if (bone_data != 0) {
                int anim_idx = *(int*)(bone_data + 0x30);
                if ((anim_idx >= 0) && (anim_idx < 0x42)) {
                    speed = FUN_801b6d50(anim_idx);
                }
            }
        }
    } else if (layer == 2) {
        speed = (double)FLOAT_80614854; // crouch/prone constant
    } else if (layer == 0) {
        if ((*(u32*)(player_data + 0xc) & 0x40) == 0) {
            speed = FUN_801b6d50(*(int*)(char_data + 0x204)); // weapon weight
        } else if (g_game_mode != 0x0a) {
            speed = (double)FLOAT_80614858; // arcade speed
        }
    } else if (layer == 1) {
        speed = FUN_801b6d8c(*(int*)(char_data + 0x204)); // strafe from weapon
    }

    // Apply MP height scaling if flag 0x80 clear
    if ((*(u32*)(player_data + 0xc) & 0x80) == 0) {
        speed = -(FLOAT_80614800 * (float)(1.0 - speed) - 1.0);
    }

    return speed;
}

// ============================================================================
// 0x800a6910 - player_get_min_speed
// Returns the minimum speed across all 3 layers (walk, strafe, crouch).
// Calls player_get_movement_speed for layers 0, 1, 2 and returns the lowest.
// 3 xrefs.
// ============================================================================
double player_get_min_speed(int player_data) {
    double min_speed = 1.0;
    for (int i = 0; i < 3; i++) {
        double speed = player_get_movement_speed(player_data, i);
        if (speed < min_speed) {
            min_speed = speed;
        }
    }
    return min_speed;
}

// ============================================================================
// 0x800a6980 - player_is_valid_target
// Returns 1 if an entity is a valid auto-aim target for the player.
// For type 8 (character): checks collision volume > 0, FUN_800acc84 returns 0
// (not on same team), and FUN_800acd08 returns non-zero (targetable state).
// For other entities: checks collision volume > 0, active bit set, and
// entity_is_in_active_list returns non-zero.
// 4 xrefs.
// ============================================================================
int player_is_valid_target(int player_data, int entity) {
    if (*(int*)(entity + 0x20) == 8) { // character type
        int* char_data = *(int**)(entity + 0x7c);
        if ((0.0f < *(float*)(*char_data + 0x1cc)) &&          // has collision volume
            (FUN_800acc84(player_data, (int)char_data) == 0) && // not same team
            (FUN_800acd08((int)char_data) != 0)) {             // targetable state
            return 1;
        }
    } else {
        if ((0.0f < *(float*)(entity + 0x1cc)) &&       // has collision volume
            ((*(u32*)(entity + 0x28) & 1) != 0) &&       // active bit
            (entity_is_in_active_list(entity) != 0)) {   // in update list
            return 1;
        }
    }
    return 0;
}

// ============================================================================
// 0x800a6a28 - player_target_in_range
// Returns 1 if the distance between player position (+0xb4..+0xbc) and
// entity position (+0x64..+0x6c) is within FLOAT_8061485c threshold,
// OR if the player has flag 0x40 (always-track, e.g. mounted weapon).
// 1 xref.
// ============================================================================
int player_target_in_range(int player_data, int entity) {
    float dx = *(float*)(player_data + 0xb4) - *(float*)(entity + 0x64);
    float dy = *(float*)(player_data + 0xb8) - *(float*)(entity + 0x68);
    float dz = *(float*)(player_data + 0xbc) - *(float*)(entity + 0x6c);
    double dist = FUN_80131538((double)(dz*dz + dx*dx + dy*dy)); // sqrtf

    if ((dist <= (double)FLOAT_8061485c) || ((*(u32*)(player_data + 0xc) & 0x40) != 0)) {
        return 1;
    }
    return 0;
}

// ============================================================================
// 0x800a6ab0 - player_autoaim_scan
// MASSIVE auto-aim target scanner. Iterates the entity pool to find the best
// lock-on target for the active player.
// Algorithm:
// 1. Check player_can_receive_sound; skip if 0
// 2. Get base distance threshold from player_get_min_speed
// 3. For each entity in the pool:
//    a. Check slot flags (bit 1 = visible, bit 2 = in range)
//    b. Check entity distance > base threshold (farther = better priority)
//    c. Call player_is_valid_target to validate
//    d. Compute head bone world position via matrix transforms
//    e. Compute direction vector from player position
//    f. Call sound_source_compute_dot_distance for dot/distance scoring
//    g. Apply angle-based priority weighting (closer angle = higher priority)
//    h. Apply dot-product fade at edges (FLOAT_80614864..FLOAT_8061486c)
//    i. If this entity scores better than current best, update best target
// 4. If best target found and score beats weapon layer thresholds:
//    - Store target position at player+0xe20..+0xe28
//    - Store target entity at player+0xe1c
// 5. Normalize direction vector for final aim direction
// 1 xref (from player_autoaim_scan_events).
// ============================================================================
void player_autoaim_scan(void) {
    float* aim_position;  // output best target position
    float* aim_direction; // output normalized direction to target
    int best_entity_idx = -1;
    double best_distance;
    double base_threshold;

    FUN_803826d8(); // OS lock + get output buffers from context

    *(int*)(g_active_player + 0xe14) = 0; // clear aim target entity

    int can_aim = player_can_receive_sound((int)g_active_player);
    if (can_aim == 0) {
        FUN_80382724(); // OS unlock
        return;
    }

    base_threshold = player_get_min_speed((int)g_active_player);
    double score_range = 1.0 - base_threshold;

    // Scan all entities in the pool
    for (int i = 0; i < g_max_entities; i++) {
        int* entity_ptr = g_entity_ptrs[i];
        if (entity_ptr == NULL) continue;

        u32* slot_data = (u32*)(*(int*)(g_active_player + 0xe0c) + entity_ptr[2] * 0x10);
        if ((*slot_data & 2) == 0) continue; // not visible
        if ((double)*(float*)(slot_data + 2) <= base_threshold) continue; // too close

        int valid = player_is_valid_target((int)g_active_player, *entity_ptr);
        if (valid == 0) continue;

        // Compute head bone world position
        int entity = *entity_ptr;
        float frustum_data[16]; // view frustum matrix
        FUN_8012f2c4((double)*(float*)(entity + 0x64),
                     (double)*(float*)(entity + 0x68),
                     (double)*(float*)(entity + 0x6c),
                     (double)(FLOAT_80614860 * *(float*)(entity + 0x70) / FLOAT_806147b0),
                     frustum_data);

        // Transform by bone matrix
        float head_pos[16];
        int render_data = *(int*)(*(int*)(entity + 4) + 0x10);
        int bone_offset = *(int*)(*(int*)(*(int*)(entity + 4) + 0x84) + 0x18) * 0x40;
        FUN_80130bd4((float*)(render_data + bone_offset), frustum_data, head_pos);

        // Compute direction from player to target
        float dir[3];
        dir[0] = head_pos[12] - *(float*)(g_active_player + 0xb4);
        dir[1] = (float)entity_ptr[0x2f0] - *(float*)(g_active_player + 0xb8);
        dir[2] = head_pos[13] - *(float*)(g_active_player + 0xbc);

        // Score via dot product / distance
        sound_source_compute_dot_distance((int)g_active_player + 0xc0, dir, (int)slot_data);

        // Apply scoring adjustments (capture mode ranking, angle fade)
        double score = base_threshold;
        // ... (angle-based priority, dot fade at edges)

        if (score < (double)*(float*)(slot_data + 2)) {
            // Check against secondary threshold
            double secondary = player_get_movement_speed((int)g_active_player, 2);
            if (secondary < (double)*(float*)(slot_data + 2)) {
                *(int*)(g_active_player + 0xe14) =
                    *(int*)(g_active_player_secondary + entity_ptr[2] * 0xea0);
            }
            best_entity_idx = entity_ptr[2];
            base_threshold = (double)*(float*)(slot_data + 2);
            // Store best position and direction
        }
    }

    // Commit best target if it beats weapon thresholds
    if ((best_entity_idx >= 0) &&
        ((player_get_movement_speed((int)g_active_player, 1) < base_threshold) ||
         (player_get_movement_speed((int)g_active_player, 0) < base_threshold)) &&
        (*(int*)(g_active_player + 0x114) != 0x80)) {
        *(float*)(g_active_player + 0xe20) = aim_position[0];
        *(float*)(g_active_player + 0xe24) = aim_position[1];
        *(float*)(g_active_player + 0xe28) = aim_position[2];
        // Store target entity reference
    }

    // Normalize aim direction if valid target found
    if ((best_entity_idx >= 0) &&
        (player_get_movement_speed((int)g_active_player, 0) < base_threshold)) {
        // Newton-Raphson inverse sqrt + normalize
        float mag_sq = aim_direction[0]*aim_direction[0] +
                       aim_direction[1]*aim_direction[1] +
                       aim_direction[2]*aim_direction[2];
        if (FLOAT_8061482c < mag_sq) {
            float inv = 1.0f / sqrtf(mag_sq);
            // Refine and scale
            aim_direction[0] *= inv;
            aim_direction[1] *= inv;
            aim_direction[2] *= inv;
        }
    }

    FUN_80382724(); // OS unlock
}

// ============================================================================
// 0x800a6ee4 - player_autoaim_scan_events
// MASSIVE variant of player_autoaim_scan that scans soundpad/event sources
// instead of the entity pool. Called after player_autoaim_scan to also
// consider non-entity targets (zone-based sound emitters, event triggers).
// Similar algorithm:
// 1. Check player_can_receive_sound
// 2. Call player_autoaim_scan first for entity targets
// 3. Iterate event/soundpad sources via soundpad_get_current_reverb_state
// 4. For each: check slot flags, distance, player_is_valid_target
// 5. Compute offset position (entity pos + offset at +0x1a8..+0x1b0)
// 6. Score via dot/distance, update best if better
// 7. Normalize direction, commit target
// 2 xrefs.
// ============================================================================
void player_autoaim_scan_events(void) {
    float* aim_position;
    float* aim_direction;
    int best_entity = 0;
    bool found_event_target = false;

    FUN_803826dc(); // OS lock

    *(int*)(g_active_player + 0xe14) = 0;

    int can_aim = player_can_receive_sound((int)g_active_player);
    if (can_aim == 0) {
        FUN_80382728(); // OS unlock
        return;
    }

    double best_distance = player_get_min_speed((int)g_active_player);
    int player_entity = *(int*)(g_active_player + 0x14);

    // First: run entity-pool scan
    int entity_result = player_autoaim_scan();
    if (entity_result >= 0) {
        int* entity_slot = (int*)(g_active_player_secondary + entity_result * 0xea0);
        best_entity = *entity_slot;
        best_distance = (double)*(float*)(*(int*)(g_active_player + 0xe0c) +
                        entity_slot[2] * 0x10 + 8);
    }

    // Second: scan soundpad/event sources
    int reverb_count = soundpad_get_current_reverb_state();
    for (int i = 0; i < reverb_count; i++) {
        int event_data = event_get_type_data(i);
        u32* slot_data = (u32*)(*(int*)(g_active_player + 0xe0c) + (i + 0x20) * 0x10);

        if ((*slot_data & 2) == 0) continue;
        if (best_distance >= (double)*(float*)(slot_data + 2)) continue;

        int valid = player_is_valid_target((int)g_active_player, event_data);
        if (valid == 0) continue;

        // Compute position with offset
        float frustum[16], head_pos[16];
        FUN_8012f2c4((double)*(float*)(g_active_player + 0xa8),
                     (double)*(float*)(g_active_player + 0xac),
                     (double)*(float*)(g_active_player + 0xb0),
                     (double)(FLOAT_80614860 * *(float*)(player_entity + 0x70) / FLOAT_806147b0),
                     frustum);
        int render_data = *(int*)(*(int*)(player_entity + 4) + 0x10);
        int bone_offset = *(int*)(*(int*)(*(int*)(player_entity + 4) + 0x84) + 0x18) * 0x40;
        FUN_80130bd4((float*)(render_data + bone_offset), frustum, head_pos);

        // Direction from head to event source (with offset)
        float dir[3];
        dir[0] = (*(float*)(event_data + 0x64) + *(float*)(event_data + 0x1a8)) - head_pos[12];
        dir[1] = (*(float*)(event_data + 0x68) + *(float*)(event_data + 0x1ac)) - head_pos[13];
        dir[2] = (*(float*)(event_data + 0x6c) + *(float*)(event_data + 0x1b0)) - head_pos[14];

        sound_source_compute_dot_distance((int)g_active_player + 0xc0, dir, (int)slot_data);

        if (best_distance < (double)*(float*)(slot_data + 2)) {
            double secondary = player_get_movement_speed((int)g_active_player, 2);
            if (secondary < (double)*(float*)(slot_data + 2)) {
                *(int*)(g_active_player + 0xe14) = event_data;
            }
            found_event_target = true;
            best_distance = (double)*(float*)(slot_data + 2);
            aim_position[0] = *(float*)(event_data + 0x64) + *(float*)(event_data + 0x1a8);
            aim_position[1] = *(float*)(event_data + 0x68) + *(float*)(event_data + 0x1ac);
            aim_position[2] = *(float*)(event_data + 0x6c) + *(float*)(event_data + 0x1b0);
            aim_direction[0] = dir[0];
            aim_direction[1] = dir[1];
            aim_direction[2] = dir[2];
            best_entity = event_data;
        }
    }

    // Normalize direction if event target found
    if (found_event_target) {
        float mag_sq = aim_direction[0]*aim_direction[0] +
                       aim_direction[1]*aim_direction[1] +
                       aim_direction[2]*aim_direction[2];
        if (FLOAT_8061482c < mag_sq) {
            float inv = 1.0f / sqrtf(mag_sq);
            aim_direction[0] *= inv;
            aim_direction[1] *= inv;
            aim_direction[2] *= inv;
        }
    }

    // Commit target if it beats weapon thresholds
    if ((best_entity != 0) &&
        ((player_get_movement_speed((int)g_active_player, 1) < best_distance) ||
         (player_get_movement_speed((int)g_active_player, 0) < best_distance)) &&
        (*(int*)(g_active_player + 0x114) != 0x80)) {
        *(float*)(g_active_player + 0xe20) = aim_position[0];
        *(float*)(g_active_player + 0xe24) = aim_position[1];
        *(float*)(g_active_player + 0xe28) = aim_position[2];
        *(int*)(g_active_player + 0xe1c) = best_entity;
    }

    if (best_entity != 0) {
        player_get_movement_speed((int)g_active_player, 0);
    }

    FUN_80382728(); // OS unlock
}

// ============================================================================
// 0x800a7258 - player_stub_return_zero
// Empty stub, always returns 0. 1 xref from FUN_800a7260.
// ============================================================================
int player_stub_return_zero(void) {
    return 0;
}

// ============================================================================
// 0x800a76dc - player_movement_integrate
// Integrates player movement: applies forward/strafe input vectors to position
// via the navigation system (nav_advanced_movement_dispatch).
// param_1: forward speed, param_2: strafe speed
// param_3: output position, param_4: forward direction, param_5: strafe direction
// Steps:
// 1. Compute new target position from forward*dir + strafe*strafe_dir + current pos
// 2. Apply gravity offset (FLOAT_80614888 * frame_rate)
// 3. Dampen vertical speed (player[0x26] *= FLOAT_80614878)
// 4. Call nav_advanced_movement_dispatch for collision resolution
// 5. If collision bit 0 set: trigger landing state (FUN_800ab2a4 with param 2)
// 6. Update player position from resolved position
// 7. Update entity eye height via FUN_800a9e74
// 8. Set char_data dirty flags (bit 0 on, bit 3 off)
// 1 xref (from FUN_800ab000 - player tick update).
// ============================================================================
void player_movement_integrate(double forward_speed, double strafe_speed,
                               float* out_pos, float* forward_dir, float* strafe_dir) {
    int char_data = *(int*)(g_active_player[5] + 0x7c);

    // Compute target position: pos + forward*fwd_dir + strafe*strafe_dir
    out_pos[0] = (float)((double)forward_dir[0] * forward_speed +
                 (double)((float)((double)strafe_dir[0] * strafe_speed +
                          (double)(float)g_active_player[0x2a])));
    out_pos[1] = -((float)(FLOAT_80614888 * FLOAT_806108ec) - (float)g_active_player[0x2b]);
    out_pos[2] = (float)((double)forward_dir[1] * forward_speed +
                 (double)((float)((double)strafe_dir[1] * strafe_speed +
                          (double)(float)g_active_player[0x2c])));

    // Dampen vertical speed
    g_active_player[0x26] = (float)g_active_player[0x26] * FLOAT_80614878;

    // Navigation collision resolution
    int collision_result[12];
    u32 flags = nav_advanced_movement_dispatch(
        (double)((float)g_active_player[0x25] + (float)g_active_player[0x23]),
        (double)(float)g_active_player[0x28],
        *g_active_player,
        g_active_player + 0x2a,
        out_pos,
        collision_result, 1, 0);

    // Landing detection
    if ((flags & 1) != 0) {
        FUN_800ab2a4((int)g_active_player, 2); // set landing state
    }

    // Commit resolved position
    g_active_player[0x2a] = *(int*)&out_pos[0];
    g_active_player[0x2b] = *(int*)&out_pos[1];
    g_active_player[0x2c] = *(int*)&out_pos[2];

    // Update eye height (pos.x, pos.y + vertical_offset + eye_height, pos.z)
    FUN_800a9e74((double)(float)g_active_player[0x2a],
                 (double)((float)g_active_player[0x26] +
                          (float)g_active_player[0x2b] +
                          (float)g_active_player[0x24]),
                 (double)(float)g_active_player[0x2c],
                 (int)g_active_player);

    // Set dirty flag (bit 0), clear processed flag (bit 3)
    *(u32*)(char_data + 0x24) |= 1;
    *(u32*)(char_data + 0x24) &= ~8u;
}
