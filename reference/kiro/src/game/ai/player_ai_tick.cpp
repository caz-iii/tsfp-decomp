// =============================================================================
// player_ai_tick.cpp - AI Per-Frame Tick & Utility Systems
// Address Range: 0x800b0060 - 0x800b07c8
// Date: 2026-08-11
// Batch: 254
// =============================================================================
//
// AI per-frame tick dispatch, footstep sounds, aim position computation,
// floor room queries, entity resolution callbacks, and weapon linked list
// management.
//
// Key subsystems:
//   - Footstep sound (velocity-based walk/run bank selection)
//   - Per-frame tick (cooldowns, HUD scoring, AI behavior invoke)
//   - Aim position (bone matrix transform to world space)
//   - Floor room (cached floor polygon/room query)
//   - Weapon linked list (20-slot doubly-linked pool)
//
// =============================================================================

#include "common.h"
#include "player_ai.h"
#include "sound.h"

// External data
extern int DAT_806109c0;
extern int DAT_80611934;
extern int DAT_80612610;
extern int DAT_806120fc;
extern float FLOAT_80614900;
extern float FLOAT_80614904;
extern float FLOAT_80614914;
extern float FLOAT_80614918;
extern float FLOAT_80614930;
extern float FLOAT_80614944;
extern float FLOAT_80614948;
extern float FLOAT_8061490c;
extern float FLOAT_8061497c;
extern float FLOAT_806149b0;
extern float FLOAT_806149b4;
extern float FLOAT_806149b8;
extern float FLOAT_806149bc;
extern float FLOAT_806149c0;
extern float FLOAT_806149c4;
extern float FLOAT_806149c8;
extern float FLOAT_806149cc;
extern float FLOAT_806149d0;
extern float FLOAT_806149d8;
extern float FLOAT_806108ec;
extern double DOUBLE_80614930;
extern double DOUBLE_80614990;

// Forward declarations
extern int  FUN_803826f0(void);
extern void FUN_8038273c(void);
extern void FUN_80131538(double);
extern int  FUN_8013104c(void);
extern void FUN_801c2e1c(void);
extern void FUN_801aab1c(int);
extern void FUN_80237b50(double, int, int);
extern void FUN_80237e0c(int, int, int);
extern void FUN_801b2b74(int);
extern void FUN_80135ecc(void);
extern void FUN_8012f2c4(double, double, double, double, float*);
extern void FUN_80130bd4(float*, float*, float*);

extern int  player_get_active_count(void);
extern void player_ai_ambient_sound_tick(void);
extern void player_ai_list_distance_decay(int);
extern int  floor_room_query(int, float*, int*, int*, int);
extern void sound_init_play_params(int*);
extern void sound_play_request(int*);

// =============================================================================
// Address: 0x800b0060 | player_ai_footstep_sound_tick
// Velocity-based footstep sound system. Computes speed magnitude from entity
// velocity vector, selects walk (0x5F3+rand) or run (0x5F6+rand) sound bank,
// plays 3D positional sound with volume proportional to speed. Accumulator at
// ai_data+0xE9C (stored as float at index 0x3A7) tracks distance traveled.
// =============================================================================
void player_ai_footstep_sound_tick(void) {
    int* ai_state = (int*)FUN_803826f0();

    if (ai_state == NULL) goto end;
    int entity = ai_state[0];
    if (entity == 0) goto end;
    if (ai_state[0x19F] == 0) goto end;

    // Check if entity has ground contact flag (0x100000)
    if ((*(u32*)(ai_state[0x19F] + 4) & 0x100000) == 0) {
        // Not grounded - reset accumulator
        ai_state[0x3A7] = *(int*)&FLOAT_806149c8;
    } else {
        // Compute speed from velocity vector
        float vx = *(float*)(entity + 0xF8);
        float vy = *(float*)(entity + 0xFC);
        float vz = *(float*)(entity + 0x100);
        float speed_sq = vx*vx + vy*vy + vz*vz;
        float speed = (float)FUN_80131538((double)speed_sq);

        // Scale speed by frame time
        float dt = FLOAT_806108ec / (float)((double)(0x43300000 | (DAT_806109c0 ^ 0x80000000)) - DOUBLE_80614930);
        float scaled_speed = speed * dt;

        // Clamp volume to [0, 1]
        float volume = FLOAT_80614904;  // 1.0
        if (scaled_speed <= FLOAT_8061497c) {
            volume = scaled_speed / FLOAT_8061497c;
        }

        // Accumulate distance, negate into accumulator
        float accum = -(volume * (FLOAT_806108ec / dt) - (float)ai_state[0x3A7]);
        ai_state[0x3A7] = *(int*)&accum;

        float threshold_run = FLOAT_806149b0;
        float threshold_walk = FLOAT_806149b8;

        if (threshold_run <= accum) {
            // Above run threshold - decay
            ai_state[0x3A7] = (int)(accum * FLOAT_806149c4);
        } else if (threshold_walk < accum) {
            // Run footstep: bank 0x5F6 + random(0..FLOAT_806149b4)
            int rand_val = FUN_8013104c();
            int bank_offset = (int)(FLOAT_806149b4 * ((float)rand_val / FLOAT_80614948) + FLOAT_8061490c);
            int sound_id = bank_offset + 0x5F6;

            // Play 3D sound at entity position
            int play_params[6];
            sound_init_play_params(play_params);
            play_params[0] = sound_id;
            // Set 3D flag, volume, pitch, position
            sound_play_request(play_params);
            ai_state[0x3A7] = *(int*)&FLOAT_8061490c;  // Reset accumulator
        } else {
            // Walk footstep: bank 0x5F3 + random(0..FLOAT_80614944)
            int rand_val = FUN_8013104c();
            int bank_offset = (int)(FLOAT_80614944 * ((float)rand_val / FLOAT_80614948) + FLOAT_8061490c);
            int sound_id = bank_offset + 0x5F3;

            int play_params[6];
            sound_init_play_params(play_params);
            play_params[0] = sound_id;
            sound_play_request(play_params);
            ai_state[0x3A7] = *(int*)&FLOAT_8061490c;
        }
    }

end:
    FUN_8038273c();
}

// =============================================================================
// Address: 0x800b0274 | player_ai_per_frame_tick
// Main AI per-frame dispatcher. Decrements cooldown timers at +0xAD4 and +0xB40,
// invokes footstep + ambient sound ticks, sends HUD scoring messages (0x25 for
// weapon pickup, 0x2A/0x2B for first/last kills in mode 7), and calls AI
// behavior system. Skipped if player_count==9 or cutscene active.
// =============================================================================
void player_ai_per_frame_tick(int entity_ptr) {
    int ai_data = *(int*)(entity_ptr + 0x7C);

    // Skip if player count == 9 (spectator mode) or cutscene active
    int count = player_get_active_count();
    if (count == 9) return;
    if (FUN_801c2e1c()) return;

    // Decrement cooldown timer at +0xAD4
    float dt = FLOAT_806108ec / (float)((double)(0x43300000 | (DAT_806109c0 ^ 0x80000000)) - DOUBLE_80614930);
    if (FLOAT_80614900 < *(float*)(ai_data + 0xAD4)) {
        *(float*)(ai_data + 0xAD4) -= dt;
    }

    // Decrement cooldown timer at +0xB40
    if (FLOAT_80614900 < *(float*)(ai_data + 0xB40)) {
        *(float*)(ai_data + 0xB40) -= dt;
    }

    // Sound ticks
    player_ai_footstep_sound_tick();
    player_ai_ambient_sound_tick();

    // HUD scoring messages
    if (*(int*)(ai_data + 0x94) != 0 || FUN_801aab1c(ai_data)) {
        FUN_80237e0c(3, *(int*)(ai_data + 8), *(int*)(ai_data + 0x94));
    }

    // Weapon pickup HUD message (type 0x25)
    if (*(int*)(ai_data + 0x80) != 0) {
        FUN_80237b50((double)(dt * FLOAT_80614904), 0x25, *(int*)(ai_data + 8));
    }

    // Mode 7 (virus/monkey assistant): first/last kill messages
    if (DAT_80611934 == 7) {
        if (*(int*)(ai_data + 0xA68) == 1) {
            FUN_80237b50((double)(dt * FLOAT_80614904), 0x2A, *(int*)(ai_data + 8));
        } else if (*(int*)(ai_data + 0xA68) == DAT_80612610 + DAT_806120fc) {
            FUN_80237b50((double)(dt * FLOAT_80614904), 0x2B, *(int*)(ai_data + 8));
        }
    }

    // AI behavior invoke (only for AI-controlled entities, not human players)
    FUN_801b2b74(ai_data);
    if (DAT_80612610 <= *(int*)(ai_data + 8) && (*(u32*)(ai_data + 0x2C) & 4) == 0) {
        FUN_80135ecc();
    }
}

// =============================================================================
// Address: 0x800b04cc | player_ai_reset_path_list
// Thin wrapper: calls player_ai_list_distance_decay on weapon list at
// ai_data+0x304.
// =============================================================================
void player_ai_reset_path_list(int entity_ptr) {
    player_ai_list_distance_decay(*(int*)(entity_ptr + 0x7C) + 0x304);
}

// =============================================================================
// Address: 0x800b04f4 | player_ai_transform_aim_to_world
// Transforms aim direction to world space: generates scaled direction vector
// from position using radius, then multiplies by bone matrix.
// =============================================================================
void player_ai_transform_aim_to_world(float radius, float* position, float* bone_matrix, float* out_position) {
    float direction[3];

    // Generate direction vector at given radius from position
    FUN_8012f2c4((double)position[0], (double)position[1], (double)position[2], (double)radius, direction);

    // Transform by bone matrix
    FUN_80130bd4(bone_matrix, direction, out_position);
}

// =============================================================================
// Address: 0x800b0550 | player_ai_get_aim_world_position
// Computes world-space aim position for AI entity. Uses height-scaled radius
// (FLOAT_80614918 * entity_height / FLOAT_80614914) plus bone matrix transform.
// If position param is NULL, uses entity's own position.
// =============================================================================
void player_ai_get_aim_world_position(int entity_ptr, float* position, float* out_position) {
    float local_pos[3];

    if ((*(u32*)(entity_ptr + 0x10) & 0xFFFF) == 0) return;

    // Use provided position or entity's own
    if (position == NULL) {
        local_pos[0] = *(float*)(entity_ptr + 0x64);
        local_pos[1] = *(float*)(entity_ptr + 0x68);
        local_pos[2] = *(float*)(entity_ptr + 0x6C);
    } else {
        local_pos[0] = position[0];
        local_pos[1] = position[1];
        local_pos[2] = position[2];
    }

    // Scale radius by entity height ratio
    float radius = (FLOAT_80614918 * *(float*)(entity_ptr + 0x70)) / FLOAT_80614914;

    // Get bone matrix pointer (bone index from anim data)
    int render_data = *(int*)(entity_ptr + 4);
    float* bone_matrix = (float*)(*(int*)(*(int*)(render_data + 0x10) +
                         *(int*)(*(int*)(*(int*)(render_data + 0x84)) + 0x54) * 0x40));

    player_ai_transform_aim_to_world(radius, local_pos, bone_matrix, out_position);
}

// =============================================================================
// Address: 0x800b05fc | player_ai_get_floor_room
// Returns cached floor room for AI entity, or computes fresh via
// player_ai_compute_floor_room. Cache flag: bit 0x800 at ai_data+0x2C.
// =============================================================================
int player_ai_get_floor_room(int entity_ptr) {
    int ai_data = *(int*)(entity_ptr + 0x7C);

    // Human players (index < player_count) always use cache
    // AI with cache flag set also uses cache
    if (*(int*)(ai_data + 8) < DAT_80612610 || (*(u32*)(ai_data + 0x2C) & 0x800) != 0) {
        return *(int*)(ai_data + 0xC00);
    }

    // Compute fresh
    return player_ai_compute_floor_room(entity_ptr);
}

// =============================================================================
// Address: 0x800b0644 | player_ai_compute_floor_room
// Performs floor room query with Y offset (FLOAT_806149cc). Caches results:
//   +0xBFC = floor height
//   +0xC00 = room ID
//   +0xC04 = floor polygon
// Sets cache flag 0x800 on ai_data+0x2C for AI entities.
// =============================================================================
int player_ai_compute_floor_room(int entity_ptr) {
    int ai_data = *(int*)(entity_ptr + 0x7C);
    int room_result = 0;
    int floor_poly = 0;

    // Position with Y offset for floor detection
    float query_pos[3];
    query_pos[0] = *(float*)(entity_ptr + 0x64);
    query_pos[1] = FLOAT_806149cc + *(float*)(entity_ptr + 0x68);
    query_pos[2] = *(float*)(entity_ptr + 0x6C);

    float height = (float)floor_room_query(
        *(int*)(entity_ptr + 0x34),
        query_pos,
        &room_result,
        &floor_poly,
        0
    );

    // Only update cache if valid result (not sentinel value)
    if (height != FLOAT_806149d0) {
        *(float*)(ai_data + 0xBFC) = height;
        *(int*)(ai_data + 0xC00) = room_result;
        *(int*)(entity_ptr + 0x21C) = room_result;  // Also store on entity
        *(int*)(ai_data + 0xC04) = floor_poly;

        // Set cache valid flag for AI entities
        if (DAT_80612610 <= *(int*)(ai_data + 8)) {
            *(u32*)(ai_data + 0x2C) |= 0x800;
        }
    }

    return *(int*)(ai_data + 0xC00);
}

// =============================================================================
// Address: 0x800b0704 | player_ai_resolve_entity_callback
// Entity resolver via function pointer. If callback at +0x48 is set, calls it
// with entity ID at +0x04. Otherwise returns entity ID directly. Used by
// spawn/preload system (6+ callers including spawn_create_instance).
// =============================================================================
int player_ai_resolve_entity_callback(int param) {
    typedef int (*resolve_fn)(int);

    resolve_fn callback = *(resolve_fn*)(param + 0x48);
    if (callback == NULL) {
        return *(int*)(param + 4);
    }
    return callback(*(int*)(param + 4));
}

// =============================================================================
// Address: 0x800b0740 | player_ai_weapon_list_init
// Initializes 20-slot doubly-linked list (0x1C bytes per slot) for AI weapon
// inventory tracking. Sets up free pool chain via prev (+0x14) and next (+0x18)
// pointers. Head sentinel at param_1+0x230, free list at +0x234, count at +0x238,
// max distance at +0x23C.
// =============================================================================
void player_ai_weapon_list_init(int list_base) {
    // Init header
    *(float*)(list_base + 0x23C) = FLOAT_806149d8;  // Max distance
    *(int*)(list_base + 0x238) = 0;                  // Active count
    *(int*)(list_base + 0x230) = 0;                  // Active head
    *(int*)(list_base + 0x234) = list_base;          // Free list head (first slot)
    *(int*)(list_base + 0x14) = 0;                   // First slot prev = NULL

    // Build prev chain (each slot points to previous)
    for (int i = 1; i < 20; i++) {
        *(int*)(list_base + i * 0x1C + 0x14) = list_base + (i - 1) * 0x1C;
    }

    // Build next chain (each slot points to next)
    for (int i = 0; i < 19; i++) {
        *(int*)(list_base + i * 0x1C + 0x18) = list_base + (i + 1) * 0x1C;
    }

    // Last slot next = NULL
    *(int*)(list_base + 19 * 0x1C + 0x18) = 0;
}

// =============================================================================
// Address: 0x800b07c8 | player_ai_list_alloc_node
// Allocates node from AI linked list free pool. Pops from free list (+0x234),
// pushes to active head (+0x230). Max 20 slots, returns node pointer or NULL
// if pool exhausted. Increments active count at +0x238.
// =============================================================================
int player_ai_list_alloc_node(int list_base) {
    // Check capacity
    if (*(int*)(list_base + 0x238) >= 20) {
        return 0;  // Pool full
    }

    // Pop from free list
    int old_head = *(int*)(list_base + 0x230);
    int new_node = *(int*)(list_base + 0x234);
    *(int*)(list_base + 0x230) = new_node;
    *(int*)(list_base + 0x234) = *(int*)(new_node + 0x18);  // Advance free list

    // Unlink new free head's prev
    if (*(int*)(list_base + 0x234) != 0) {
        *(int*)(*(int*)(list_base + 0x234) + 0x14) = 0;
    }

    // Link new node into active list
    *(int*)(new_node + 0x18) = old_head;
    *(int*)(new_node + 0x14) = 0;
    if (old_head != 0) {
        *(int*)(old_head + 0x14) = new_node;
    }

    // Increment count
    *(int*)(list_base + 0x238) += 1;

    return new_node;
}
