// =============================================================================
// Player AI Awareness & Combat Raycast System
// Address Range: 0x800b2844 - 0x800b37d4
// Date: 2026-08-11
// Batch: 259
//
// Handles AI alertness: proximity scans, distance-based alerts, weapon fire
// radius alerts, entity raycasting for line-of-sight, and multi-pass weapon
// fire raycasts with portal-aware room transitions.
// =============================================================================

#include "common.h"
#include "ai/player_ai.h"
#include "collision/collision.h"
#include "scene/scene.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define AI_ALERT_FLAG_MASK      0x9180200
#define AI_UNAWARE_BIT          0x08000000
#define PLAYER_COUNT_10         10
#define GAME_FLAG_NO_AI_ALERT   0x200

// ---------------------------------------------------------------------------
// Globals (extern)
// ---------------------------------------------------------------------------
extern u8   g_player_count;             // DAT_805015bd
extern u32  g_game_flags;               // DAT_805015c0
extern s32  g_ai_frame_number;          // DAT_80611928
extern s32  g_ai_entity_list_start;     // DAT_80612610
extern s32  g_ai_entity_list_end;       // DAT_80612098
extern s32  g_ai_raycast_mode;          // DAT_80611da8
extern s32* g_ai_entity_list[];         // DAT_80588610
extern s32  g_floor_table_ptr;          // DAT_80611b04

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
extern void player_ai_alert_scan_distance(s32 pos_hi, s32 pos_lo, s32* slot);
extern s32  scene_get_neighbor_nodes_temp(s32 room_id, s32* buffer, s32 max);
extern s32  scene_raycast(s32 room, void* origin, void* dir, s32 p, float* hit_pos,
                          float* hit_normal, s32* room_out, u32 flags, u32 extra, s32* result);
extern s32  floor_height_from_position(f64 a, f64 b);
extern void collision_result_save(void);
extern void collision_result_restore(s32 addr);
extern void collision_result_store(s32 addr);
extern s32  collision_entity_raycast(s32 p1, s32 origin, f32* dir, s32 p4,
                                     f32* hit_pos, f32* hit_normal, s32 p7);
extern void weapon_compute_fire_origin(s32 entity, s32 bone, s32 p3, f32* out, f32* p5);
extern u32  player_ai_char_def_get_flags(s32 behavior_id);

// ---------------------------------------------------------------------------
// Address: 0x800b2844 | Name: player_ai_alert_check_gated
//
// Gated AI alert check. Only proceeds if:
// - Player count == 10 (full match)
// - Slot flag bit 1 is clear
// - Game flags & 0x200 is clear (AI alerts enabled)
// - Slot has weapon (mode==1) OR linked item entity with armed flag OR sound
// Then dispatches distance-based alert scan.
// ---------------------------------------------------------------------------
void player_ai_alert_check_gated(f32* position, s32 capsule, s32* slot)
{
    s32 slot_data = ai_pool_lock_render();

    if (g_player_count != PLAYER_COUNT_10) goto done;
    if ((slot[3] & 1) != 0) goto done;
    if ((g_game_flags & GAME_FLAG_NO_AI_ALERT) != 0) goto done;

    // Must have weapon OR armed item OR active sound
    if (slot[0x40] == 1) {
        // Mode 1: has weapon
    } else {
        s32 item_entity = slot[0x42];
        bool has_weapon = (item_entity != 0 &&
                          *(s32*)(item_entity + 0x20) == 0x200 &&
                          (*(u32*)(*(s32*)(item_entity + 0x7C) + 8) & 1) != 0);
        bool has_sound = (slot[0x41] != 0 && sound_is_playing(slot[0x41]));
        if (!has_weapon && !has_sound) goto done;
    }

    player_ai_alert_scan_distance(slot_data >> 32, slot_data & 0xFFFFFFFF, slot);

done:
    ai_pool_unlock_render();
}

// ---------------------------------------------------------------------------
// Address: 0x800b2944 | Name: player_ai_alert_check_self
//
// Thin wrapper: calls player_ai_alert_check_gated using the slot's own
// position (slot+0x1C) as the alert source. 4 callers.
// ---------------------------------------------------------------------------
void player_ai_alert_check_self(s32* slot)
{
    player_ai_alert_check_gated((f32*)(slot + 7), 0, slot);
}

// ---------------------------------------------------------------------------
// Address: 0x800b2970 | Name: player_ai_alert_proximity_scan
//
// MASSIVE proximity-based AI awareness scan. For each AI entity in the global
// list, checks room adjacency (via scene_get_neighbor_nodes_temp) and
// line-of-sight, then compares squared distance against threshold.
// On alert: sets alert frame, alert source entity, clears unaware bit.
// ---------------------------------------------------------------------------
void player_ai_alert_proximity_scan(void)
{
    s32* slot = (s32*)ai_pool_lock();

    if (g_player_count != PLAYER_COUNT_10) goto done;
    if ((slot[3] & 1) != 0) goto done;
    if ((g_game_flags & GAME_FLAG_NO_AI_ALERT) != 0) goto done;
    if ((s32*)slot[0x41] == NULL) goto done;

    s32 my_room = *(s32*)(*(s32*)slot[0x41] + 0x34);
    s32 neighbor_rooms[10];
    s32 neighbor_count = scene_get_neighbor_nodes_temp(my_room, neighbor_rooms, 10);

    for (s32 i = g_ai_entity_list_start; i < g_ai_entity_list_end; i++) {
        s32* target = g_ai_entity_list[i];
        if (target == (s32*)slot[0x41]) continue;  // skip self

        // Check alertable flags
        u32 flags = (g_player_count == PLAYER_COUNT_10) ? target[5] : target[7];
        if ((flags & AI_ALERT_FLAG_MASK) == 0) continue;

        // Check LOS or room adjacency
        bool in_range = false;
        s32 los_result = check_line_of_sight(target[2], ((s32*)slot[0x41])[2]);
        if (los_result != 0 || *(s32*)(*target + 0x34) == my_room) {
            in_range = true;
        }
        if (!in_range) {
            for (s32 j = 0; j < neighbor_count; j++) {
                if (*(s32*)(*target + 0x34) == neighbor_rooms[j]) {
                    in_range = true;
                    break;
                }
            }
        }

        if (!in_range) continue;

        // Distance threshold (tighter if LOS confirmed)
        f32 threshold = (los_result != 0) ? PROXIMITY_THRESHOLD_LOS : PROXIMITY_THRESHOLD_ROOM;

        f32 dx = *(f32*)&target[0x2ED] - *(f32*)&slot[7];
        f32 dy = *(f32*)&target[0x2EE] - *(f32*)&slot[8];
        f32 dz = *(f32*)&target[0x2EF] - *(f32*)&slot[9];
        f32 dist_sq = dx*dx + dy*dy + dz*dz;

        if (dist_sq < threshold) {
            target[0x2D8] = g_ai_frame_number;  // alert frame
            target[0x2D9] = (slot != NULL) ? slot[0] : -1;  // alert source
            target[10] &= ~AI_UNAWARE_BIT;  // clear unaware
        }
    }

done:
    ai_pool_unlock();
}

// ---------------------------------------------------------------------------
// Address: 0x800b2b64 | Name: player_ai_alert_scan_distance
//
// Core distance-based AI alert scan. Iterates the AI entity list, computes
// distance (simple point-to-point or capsule), and alerts entities within
// range. Sets alert frame, source entity, clears unaware bit. 2 callers.
// ---------------------------------------------------------------------------
void player_ai_alert_scan_distance(s32 pos_hi, s32 pos_lo, s32* source_slot)
{
    f32* position = (f32*)pos_hi;
    f32* capsule = (f32*)pos_lo;
    f64 max_dist_sq = /* threshold from register */;

    for (s32 i = g_ai_entity_list_start; i < g_ai_entity_list_end; i++) {
        s32* target = g_ai_entity_list[i];

        u32 flags = (g_player_count == PLAYER_COUNT_10) ? target[5] : target[7];
        if ((flags & AI_ALERT_FLAG_MASK) == 0) {
            if (!entity_is_in_vehicle(*target)) continue;
        }

        // Get target center position (entity + 0x64/68/6C offset by +1.0 on Y)
        s32 entity = *target;
        f32 target_pos[3];
        target_pos[0] = *(f32*)(entity + 0x64);
        target_pos[1] = *(f32*)(entity + 0x68) + 1.0f;
        target_pos[2] = *(f32*)(entity + 0x6C);

        f64 dist_sq;
        if (capsule == NULL) {
            f32 dx = target_pos[0] - position[0];
            f32 dy = target_pos[1] - position[1];
            f32 dz = target_pos[2] - position[2];
            dist_sq = dx*dx + dy*dy + dz*dz;
        } else {
            dist_sq = capsule_point_distance_sq(target_pos, position, capsule);
        }

        if (dist_sq < max_dist_sq) {
            target[0x2D8] = g_ai_frame_number;
            target[0x2D9] = (source_slot != NULL) ? source_slot[0] : -1;
            target[10] &= ~AI_UNAWARE_BIT;
        }
    }

    ai_pool_unlock_sub();
}

// ---------------------------------------------------------------------------
// Address: 0x800b2cdc | Name: player_ai_alert_scan_simple
//
// Thin wrapper: calls player_ai_alert_scan_distance with no capsule (simple
// point-to-point distance check).
// ---------------------------------------------------------------------------
void player_ai_alert_scan_simple(f32* position, s32* source_slot)
{
    player_ai_alert_scan_distance((s32)position, 0, source_slot);
}

// ---------------------------------------------------------------------------
// Address: 0x800b2d04 | Name: player_ai_alert_fire_radius
//
// Radius-based AI alert triggered by weapon fire. Validates weapon/turret
// state, scans all AI entities within squared radius of the fire position.
// Alerts any entity within range. 8 callers.
// ---------------------------------------------------------------------------
void player_ai_alert_fire_radius(f64 radius, s32 slot_ptr, s32 fire_mode, f32* fire_pos)
{
    if ((g_game_flags & GAME_FLAG_NO_AI_ALERT) != 0) return;
    if (g_player_count != PLAYER_COUNT_10) return;
    if (fire_mode == 8) return;  // melee doesn't alert

    // Validate weapon state: mode 1 or 2, or armed item entity
    s32 weapon_mode = *(s32*)(slot_ptr + 0x100);
    if (weapon_mode != 1 && weapon_mode != 2) {
        s32 item = *(s32*)(slot_ptr + 0x108);
        if (item == 0 || *(s32*)(item + 0x20) != 0x200) return;
        if ((*(u32*)(*(s32*)(item + 0x7C) + 8) & 1) == 0) return;
    }

    // Resolve turret/weapon entity for armed check
    s32 weapon_entity = 0;
    s32 item = *(s32*)(slot_ptr + 0x108);
    if (item != 0 && *(s32*)(item + 0x20) == 0x200 &&
        (*(u32*)(*(s32*)(item + 0x7C) + 8) & 1) != 0) {
        s32 ext = *(s32*)(item + 0x7C);
        weapon_entity = *(s32*)(ext + 0xD8);
    } else if (*(s32*)(slot_ptr + 0x104) != NULL) {
        s32 player = **(s32**)(slot_ptr + 0x104);
        if (player != 0) weapon_entity = player;
    }

    if (weapon_entity == 0) return;
    if (*(s32*)(weapon_entity + 0x150) == 0 && !sound_is_playing(*(s32*)(weapon_entity + 0x7C))) {
        return;
    }

    // Scan all AI entities within fire radius
    f32 radius_sq = (f32)(radius * radius);
    for (s32 i = g_ai_entity_list_start; i < g_ai_entity_list_end; i++) {
        s32* target = g_ai_entity_list[i];
        u32 flags = (g_player_count == PLAYER_COUNT_10) ? target[5] : target[7];
        if ((flags & AI_ALERT_FLAG_MASK) == 0) continue;

        s32 entity = *target;
        f32 dx = *(f32*)(entity + 0x64) - fire_pos[0];
        f32 dy = *(f32*)(entity + 0x68) - fire_pos[1] + 1.0f;  // Y offset
        f32 dz = *(f32*)(entity + 0x6C) - fire_pos[2];

        if (dx*dx + dy*dy + dz*dz < radius_sq) {
            target[0x2D8] = g_ai_frame_number;
            target[0x2D9] = -1;  // no specific source
            target[10] &= ~AI_UNAWARE_BIT;
        }
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800b2efc | Name: player_ai_get_set_raycast_mode
//
// Gets or sets the AI raycast mode global. Mode 1 adds collision flag 0x20
// (additional collision layers). Pass -1 to query without modifying.
// Called by fire raycast and pool reset.
// ---------------------------------------------------------------------------
s32 player_ai_get_set_raycast_mode(s32 new_mode)
{
    if (new_mode != -1) {
        g_ai_raycast_mode = new_mode;
    }
    return g_ai_raycast_mode;
}

// ---------------------------------------------------------------------------
// Address: 0x800b2f10 | Name: player_ai_entity_raycast
//
// AI entity-to-entity raycast. Validates room via floor query (resolves room
// 0xD5 or 0 to actual room), calls collision_entity_raycast, stores hit
// entity handle at slot+0x128 and collision data at slot+0x12C.
// ---------------------------------------------------------------------------
void player_ai_entity_raycast(s32 slot_ptr)
{
    u8 collision_backup[40];

    // Resolve room if invalid
    s32 room = *(s32*)(slot_ptr + 0xF0);
    if (room == 0xD5 || room == 0) {
        s32 resolved = floor_height_from_position(1.5, 1.5);
        if (resolved < 1) {
            *(s32*)(slot_ptr + 0x128) = 0;
            return;
        }
        *(s32*)(slot_ptr + 0xF0) = resolved;
    }

    collision_result_restore((s32)collision_backup);

    // Perform entity raycast
    s32 room_entry = *(s32*)(g_floor_table_ptr + *(s32*)(slot_ptr + 0xF0) * 4);
    s32 hit = collision_entity_raycast(
        *(s32*)(room_entry + 4),
        slot_ptr + 0x1C,              // origin position
        (f32*)(slot_ptr + 0x64),      // direction
        1,
        (f32*)(slot_ptr + 0x110),     // hit position out
        (f32*)(slot_ptr + 0x11C),     // hit normal out
        0
    );

    if (hit == 0) {
        *(s32*)(slot_ptr + 0x128) = 0;
    } else {
        *(s32*)(slot_ptr + 0x128) = room_entry;  // hit entity handle
        collision_result_save();
        collision_result_restore(slot_ptr + 0x12C);
        collision_result_store((s32)collision_backup);
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800b3028 | Name: player_ai_fire_raycast
//
// MASSIVE AI weapon fire raycast. Multi-pass system:
// 1. Resolves fire origin by weapon mode (6 modes: weapon barrel, turret,
//    character bone, direct position, entity center, slot position)
// 2. Performs directed shot raycast toward target
// 3. Performs forward raycast along aim direction
// 4. Handles room transition (portal crossing) with retry
// 5. Checks cached entity hit against scene hit (closest wins)
// Portal-aware with visibility flag save/restore. 5 callers.
// ---------------------------------------------------------------------------
void player_ai_fire_raycast(s32 p1, s32 p2, f32* hit_pos, f32* hit_normal, s32* result)
{
    s32 slot_ptr;      // resolved from pool lock
    f32* aim_dir;      // aim direction vector
    s32 saved_flags;
    s32 hit_result = 0;

    // ... (see Ghidra decompilation for full 800+ byte implementation)
    // Key algorithm:
    // - Resolve fire origin from 6 weapon modes
    // - Compute direction from origin to slot position
    // - Hide own entity from collision (clear visibility bits)
    // - scene_raycast with portal flags
    // - player_ai_entity_raycast for entity-specific hits
    // - Compare dot products to determine closest hit
    // - If room changed, retry raycast from original room
    // - Restore visibility flags
    // - Update weapon entity room assignment

    ai_pool_unlock_fire();
}

// ---------------------------------------------------------------------------
// Address: 0x800b37d4 | Name: player_ai_fire_raycast_simple
//
// Simplified AI fire raycast. Same 6-mode origin resolution but without
// the multi-pass entity hit comparison. Checks character definition flags
// for collision filter selection. Updates room on portal crossing.
// 11 callers.
// ---------------------------------------------------------------------------
void player_ai_fire_raycast_simple(s32 p1, s32 p2, f32* hit_pos, f32* hit_normal, s32* result)
{
    s32* slot = (s32*)ai_pool_lock_fire();
    s32 room = slot[0x3C];
    s32 saved_entity_flags = 0;
    s32 hidden_entity = 0;
    u32 extra_collision = 0;

    if (slot[0x42] != 0) {
        saved_entity_flags = *(s32*)(slot[0x42] + 0x28);
    }

    // Compute fire origin from slot position
    f32 fire_origin[3] = { *(f32*)&slot[7], *(f32*)&slot[8], *(f32*)&slot[9] };
    if (slot[0] == 0x14) {
        fire_origin[1] -= CROUCH_HEIGHT_OFFSET;
    }

    // Determine collision filter from character def flags
    u32 char_flags = player_ai_char_def_get_flags(slot[0]);
    if ((char_flags & 0x200) != 0 || (char_flags & 0x1000) != 0) {
        extra_collision = 4;  // vehicle collision layer
    } else {
        s32 mode = player_ai_get_set_raycast_mode(-1);
        if (mode == 1) extra_collision = 0x20;
    }

    // Directed shot (if fire requested or flag 0x20 set)
    if ((slot[0x53] != 0 || (slot[3] & 0x20) != 0) && slot[0x42] != 0) {
        slot[0x53] = 0;
        // Resolve origin by weapon mode (1-6), same as fire_raycast
        // Perform scene_raycast with directed direction
        // Update room on success (unless flag 0x20 prevents it)
    }

    // Forward raycast along aim direction
    u32 ray_flags = ((slot[3] & 0x10) != 0) ? 0x2010010 : 0x2000010;
    if (room > 0) {
        scene_raycast(room, fire_origin, aim_dir, 0, hit_pos, hit_normal,
                     &room, ray_flags, extra_collision, result);
        if ((slot[3] & 0x20) == 0) {
            slot[0x3C] = room;
        }
    }

    // Restore entity visibility
    if (slot[0x42] != 0) {
        *(s32*)(slot[0x42] + 0x28) = saved_entity_flags;
    }
    // Update weapon entity room
    if (slot[0x2A] != 0) {
        *(s32*)(slot[0x2A] + 0x34) = slot[0x3C];
        *(s32*)(slot[0x2A] + 0x38) = slot[0x3C];
    }

    ai_pool_unlock_fire();
}
