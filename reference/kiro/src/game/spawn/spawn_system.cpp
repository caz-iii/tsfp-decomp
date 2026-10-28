// =============================================================================
// spawn_system.cpp — Entity spawn definition system, pool management, zone spawning
// TimeSplitters: Future Perfect (GameCube)
// Batch 196 | Date: 2026-08-11
// =============================================================================

#include "common.h"
#include "entity.h"
#include "spawn.h"
#include "sound_pad.h"
#include "damage.h"

// Spawn definition table: 36 bytes (9 words) per entry at DAT_80401438
// Multiple sub-tables at different offsets for different entity categories

// =============================================================================
// Address: 0x8009328c | Name: spawn_def_get_entry
// Returns pointer to spawn definition entry at given index. Each entry is
// 9 words (36 bytes). Table base: DAT_80401438. Used by 8 callers across
// the spawn, AI, and level loading systems.
// =============================================================================
int* spawn_def_get_entry(int index)
{
    return (int *)(&g_spawn_def_table + index * 9);
}

// =============================================================================
// Address: 0x800932a0 | Name: spawn_def_copy
// Copies a spawn definition (9 words = 36 bytes) from source to destination.
// Used when duplicating or modifying spawn params before spawning.
// =============================================================================
void spawn_def_copy(int* dest, int* src)
{
    dest[0] = src[0];
    dest[1] = src[1];
    dest[2] = src[2];
    dest[3] = src[3];
    dest[4] = src[4];
    dest[5] = src[5];
    dest[6] = src[6];
    dest[7] = src[7];
    dest[8] = src[8];
}

// =============================================================================
// Address: 0x800932ec | Name: spawn_def_find_by_id
// Searches the secondary spawn definition table (12 entries, 0x54-byte stride
// at DAT_804018BC) for an entry matching the given ID. Returns index [0..11]
// or -1 if not found.
// =============================================================================
int spawn_def_find_by_id(int entity_id)
{
    int offset = 0;
    for (int i = 0; i < 12; i++) {
        if (entity_id == *(int *)(&g_spawn_secondary_table + offset)) {
            return i;
        }
        offset += 0x54;
    }
    return -1;
}

// =============================================================================
// Address: 0x80093330 | Name: spawn_preload_all_resources
// Preloads models/animations for all spawnable entity types. Iterates 6
// definition tables with different counts:
// - Table A (DAT_804014C8): 12 entries, stride 0x54
// - Table B (DAT_804018B8): 12 entries, stride 0x54
// - Table C (DAT_80401CA8): 5 entries, stride 0x54
// - Table D (DAT_80401E4C): 4 entries, stride 0x54
// - Table E (DAT_804020EC): 3 entries, stride 0x54
// - Table F (DAT_804021E8): 10 entries, stride 0x54
// - Plus 2 individual entries
// Total: 48 entity types preloaded.
// =============================================================================
void spawn_preload_all_resources(void)
{
    int entity_ptr = get_current_entity_from_stack();

    // Table A: 12 entries (primary spawnable characters)
    for (int i = 0; i < 12; i++) {
        preload_entity_model(entity_ptr, (int)(&g_spawn_table_a + i * 0x54));
    }

    // Table B: 12 entries (secondary spawnable characters)
    for (int i = 0; i < 12; i++) {
        preload_entity_model(entity_ptr, (int)(&g_spawn_table_b + i * 0x54));
    }

    // Table C: 5 entries (vehicles/turrets)
    for (int i = 0; i < 5; i++) {
        preload_entity_model(entity_ptr, (int)(&g_spawn_table_c + i * 0x54));
    }

    // Table D: 4 entries (special objects)
    for (int i = 0; i < 4; i++) {
        preload_entity_model(entity_ptr, (int)(&g_spawn_table_d + i * 0x54));
    }

    // Table E: 3 entries (explosives/hazards)
    for (int i = 0; i < 3; i++) {
        preload_entity_model(entity_ptr, (int)(&g_spawn_table_e + i * 0x54));
    }

    // Table F: 10 entries (pickups/items)
    for (int i = 0; i < 10; i++) {
        preload_entity_model(entity_ptr, (int)(&g_spawn_table_f + i * 0x54));
    }

    // 2 individual entries
    preload_entity_model(entity_ptr, (int)&g_spawn_entry_extra_1);
    preload_entity_model(entity_ptr, (int)&g_spawn_entry_extra_2);

    restore_stack_frame();
}

// =============================================================================
// Address: 0x8009344c | Name: spawn_register_event_handler
// Registers the spawn event handler (FUN_80093eac) for event type 0x40
// (entity spawn request), slot 1.
// =============================================================================
void spawn_register_event_handler(void)
{
    event_set_handler(0x40, 1, spawn_event_callback);
}

// =============================================================================
// Address: 0x8009347c | Name: spawn_system_reset
// Clears all spawn system state:
// - Main pool: 0x6540 bytes (spawn slots)
// - Secondary buffer A: 0x1950 bytes
// - Secondary buffer B: 0x78 bytes
// - Secondary buffer C: 0x78 bytes
// - Resets active/pending counters
// - Calls sub-system init (FUN_80098354)
// =============================================================================
void spawn_system_reset(void)
{
    memset_wrapper(g_spawn_pool, 0, 0x6540);
    memset_wrapper(g_spawn_buffer_a, 0, 0x1950);
    memset_wrapper(g_spawn_buffer_b, 0, 0x78);
    memset_wrapper(g_spawn_buffer_c, 0, 0x78);
    g_spawn_active_count = 0;
    g_spawn_pending_count = 0;
    g_spawn_total_spawned = 0;
    spawn_wave_system_init();
}

// =============================================================================
// Address: 0x800934fc | Name: spawn_system_alloc
// Allocates the spawn pool memory (0x6540 bytes, alignment 1) and calls
// spawn_system_reset to initialize it.
// =============================================================================
void spawn_system_alloc(void)
{
    g_spawn_pool = mem_alloc(0x6540, 1);
    spawn_system_reset();
}

// =============================================================================
// Address: 0x8009352c | Name: spawn_system_clear_count
// Clears the active spawn count if the spawn system is not locked
// (DAT_80611568 == 0). Used during level transitions to reset spawn state
// without deallocating the pool.
// =============================================================================
void spawn_system_clear_count(void)
{
    if (g_spawn_locked != 0) {
        return;
    }
    g_spawn_active_count = 0;
}

// =============================================================================
// Address: 0x80093544 | Name: spawn_entity_at_zone
// Spawns an entity at a specific zone location. Called from
// level_spawn_all_entities during level load:
// 1. Resolves zone by ID via soundpad_find_zone_by_id
// 2. Gets zone world position via soundpad_resolve_zone_node
// 3. Calls the main spawner (FUN_80093688) with zone position and spawn params
// 4. Assigns zone ID to entity's char_data (offset 0x38)
// 5. In mode 6 (special): looks up trigger zone for alternate ID assignment
// =============================================================================
void spawn_entity_at_zone(void* p1, float p2, float p3, void* p4,
                          void* p5, void* p6, void* p7, void* p8,
                          int p9, int p10, int zone_id, int param_12,
                          int param_13, unsigned int* spawn_def,
                          int param_15, int param_16, int param_17, int param_18)
{
    int ctx = get_spawn_context_from_stack();

    // Default spawn def if none provided
    if (spawn_def == NULL) {
        if (g_game_mode == '\n') {
            spawn_def = &g_spawn_def_table;  // MP default
        } else {
            spawn_def = &g_spawn_def_table_sp;  // SP default
        }
    }

    // Resolve zone position
    int zone_index = soundpad_find_zone_by_id(param_13);
    int* zone_node = soundpad_resolve_zone_node(zone_index);
    float zone_pos[3];
    zone_pos[0] = *(float *)&zone_node[3];
    zone_pos[1] = *(float *)&zone_node[4];
    zone_pos[2] = *(float *)&zone_node[5];

    // Spawn entity at zone position
    int entity = spawn_entity_main(ctx, zone_id, param_12, zone_pos, spawn_def,
                                   param_15, param_16, param_17, param_18);

    // Assign zone ID to entity char data
    *(int *)(*(int *)(entity + 0x7C) + 0x38) = zone_index;

    // Mode 6 (special): assign trigger zone
    if (g_game_mode == '\x06') {
        int* trigger = soundpad_find_trigger_by_id(param_13);
        if (trigger == NULL) {
            *(int *)(*(int *)(entity + 0x7C) + 0x3C) =
                *(int *)(g_zone_data_base + zone_index * 0x20);
        } else {
            *(int *)(*(int *)(entity + 0x7C) + 0x3C) = *trigger;
        }
    } else {
        *(int *)(*(int *)(entity + 0x7C) + 0x3C) = zone_index;
    }

    restore_stack_frame();
}

// =============================================================================
// Address: 0x80092f44 | Name: spawn_projectile_impact
// Projectile/bullet impact damage handler. Called when a projectile hits a
// surface or entity:
// 1. Computes reflected/bounced direction using time-scaled angular values
//    (sin/cos via FUN_80390324 for deflection angles)
// 2. Normalizes the direction vector (fast inverse sqrt)
// 3. Computes hit origin from entity position + direction * collision_radius
// 4. Raycasts from origin along direction using scene_raycast
// 5. If raycast hits the same entity (self-hit validation):
//    - Builds damage context with time-interpolated damage value
//    - Dispatches via damage_dispatch_to_entity
// =============================================================================
void spawn_projectile_impact(void* p1, float base_angle, float angle_offset,
                             float angle_scale, float min_damage, float max_damage,
                             void* p7, void* p8, int p9, int p10, int weapon_type)
{
    // [Large function with paired-single register saves]
    int entity_ptr = get_current_entity_from_stack();

    // 1. Compute deflected direction
    unsigned int timer = get_game_timer();
    float time_norm = (float)((double)timer - g_timer_offset_dbl) / g_time_divisor;
    float angle_a = angle_scale * (0.5f * time_norm + 0.5f) + angle_offset;
    float sin_a = sin_approx((angle_a - g_angle_offset) / g_angle_scale);

    timer = get_game_timer();
    time_norm = (float)((double)timer - g_timer_offset_dbl) / g_time_divisor;
    float angle_b = base_angle * (0.5f * time_norm + 0.5f) + base_angle;  // extraout_f1
    float sin_b = sin_approx((angle_b - g_angle_offset) / g_angle_scale);

    // 2. Build direction from deflection angles
    float dir[3];
    compute_direction_from_angles(-sin_a * g_angle_scale - angle_a,
                                  -sin_b * g_angle_scale - angle_b,
                                  dir);

    // 3. Normalize direction
    float len_sq = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];
    if (len_sq > g_melee_min_dist) {
        float inv_len = fast_inv_sqrt(len_sq);
        // Newton-Raphson x2
        dir[0] *= inv_len;
        dir[1] *= inv_len;
        dir[2] *= inv_len;
    }

    // 4. Compute origin from entity center + direction * radius
    float origin[3];
    float radius = sqrt(*(float *)(entity_ptr + 0x1C0) * *(float *)(entity_ptr + 0x1C0) +
                        *(float *)(entity_ptr + 0x1C8) * *(float *)(entity_ptr + 0x1C8) +
                        *(float *)(entity_ptr + 0x1C4) * *(float *)(entity_ptr + 0x1C4));
    origin[0] = *(float *)(entity_ptr + 0x64) + *(float *)(entity_ptr + 0x1A8) - dir[0] * radius;
    origin[1] = *(float *)(entity_ptr + 0x68) + *(float *)(entity_ptr + 0x1AC) - dir[1] * radius;
    origin[2] = *(float *)(entity_ptr + 0x6C) + *(float *)(entity_ptr + 0x1B0) - dir[2] * radius;

    // 5. Raycast
    float hit_normal[3], hit_pos[3];
    int hit_entity = scene_raycast(*(int *)(entity_ptr + 0x34), origin, dir, 1,
                                   hit_pos, hit_normal, NULL,
                                   ~*(unsigned int *)(entity_ptr + 0x20), 0, NULL);

    // 6. If hit self: build damage context and dispatch
    if (hit_entity == entity_ptr) {
        int damage_ctx[20];
        switch_slot_reset((int)damage_ctx);
        damage_ctx[0] = entity_ptr;  // target
        damage_ctx[10] = (int)hit_pos;
        damage_ctx[11] = (int)hit_normal;
        damage_ctx[12] = (int)dir;

        // Time-interpolated damage
        timer = get_game_timer();
        time_norm = (float)((double)timer - g_timer_offset_dbl) / g_time_divisor;
        float damage = (max_damage - min_damage) * time_norm + min_damage;
        damage_ctx[13] = *(int *)&damage;
        damage_ctx[14] = weapon_type;

        damage_dispatch_to_entity(damage_ctx);
    }

    restore_stack_frame();
}
