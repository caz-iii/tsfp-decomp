// Address: 0x800e37f4 - 0x800e3e2c | Batch 244 | Date: 2026-08-11
// Particle emitter spawning, visibility culling, and cached emitter setters
// particle_emitter_spawn is THE primary creation function (80+ callers)
// Related: particle_emitter_pool.cpp, particle_emitter_handles.cpp

#include "common.h"

// ============================================================================
// Address: 0x800e37f4 | Name: particle_emitter_visibility_check
// PVS room-per-player visibility test. For each room in the emitter's room
// list (+0xE4), checks if any active player can see that room via the PVS
// table. Writes visible player bitmask at +0x7C. Returns true if visible
// to at least one player.
// ============================================================================
bool particle_emitter_visibility_check(int emitter) {
    if ((*(int*)(emitter + 0xe0) == 0 && *(int*)(emitter + 0x88) == 0) ||
        DAT_80612614 == 0x65) {
        // No rooms or debug mode: inherit player mask directly
        *(u32*)(emitter + 0x7c) = *(u32*)(emitter + 0x78);
        return true;
    }

    *(u32*)(emitter + 0x7c) = 0;
    int room_count = *(int*)(emitter + 0xe0);

    for (int i = 0; i < room_count; i++) {
        for (int player = 0; player < DAT_80611d58; player++) {
            if ((*(u32*)(emitter + 0x78) & (1 << player)) == 0) continue;

            u32 player_flags = *(u32*)(DAT_80611d5c + player * 0xffc + 0x21c);
            if ((player_flags & 0x80000000) == 0 || (player_flags & 1) == 0) continue;

            // PVS lookup: can this player see this room?
            char visible;
            if (player < DAT_80611d58) {
                u8 room_id = *(u8*)(emitter + 0xe4 + i);
                visible = *(char*)(DAT_80611d5c + player * 0xffc + room_id + 0xd2c);
            } else {
                visible = 0;
            }

            if (visible != 0) {
                *(u32*)(emitter + 0x7c) |= (1 << player);
            }
        }
    }

    return (*(int*)(emitter + 0x7c) != 0);
}

// ============================================================================
// Address: 0x800e38dc | Name: particle_emitter_cull_check
// Visibility gate for emitter rendering. If flag 0x1 (room-based culling) is
// set, performs full PVS check and frees if invisible. If flag 0x1 is clear,
// sets deferred flag 0x40000000 and clears visibility mask.
// Returns 1 if emitter was culled/freed, 0 if still alive.
// ============================================================================
u32 particle_emitter_cull_check(int emitter) {
    if ((*(u32*)(emitter + 4) & 1) == 0) {
        // No room culling: mark deferred, clear mask
        *(u32*)(emitter + 4) |= 0x40000000;
        *(u32*)(emitter + 0x7c) = 0;
    } else {
        // Room-based culling
        bool visible = particle_emitter_visibility_check(emitter);
        if (!visible) {
            particle_emitter_pool_free(DAT_80611e30);
            return 1;  // culled
        }
    }
    return 0;
}

// ============================================================================
// Address: 0x800e3934 | Name: particle_emitter_submit_aabb_corners
// Submits all 8 corners of the emitter's AABB (+0xC8..+0xDC) to the render
// system for frustum inclusion. Uses bit manipulation to select min/max per
// axis. Skipped if flag 0x10 (no AABB) is set.
// ============================================================================
void particle_emitter_submit_aabb_corners(void) {
    int emitter = FUN_803826f0();

    if ((*(u32*)(emitter + 4) & 0x10) == 0) {
        float corner[3];
        for (u32 i = 0; i < 8; i++) {
            // Select min or max per axis based on bits of i
            corner[0] = (i & 1) == 0 ? *(float*)(emitter + 0xd4) : *(float*)(emitter + 0xc8);
            corner[1] = (i & 2) == 0 ? *(float*)(emitter + 0xd8) : *(float*)(emitter + 0xcc);
            corner[2] = (i & 4) == 0 ? *(float*)(emitter + 0xdc) : *(float*)(emitter + 0xd0);
            sprite_measure_text_region_int(corner, 0, NULL, NULL, 1);
        }
    }

    FUN_8038273c();
}

// ============================================================================
// Address: 0x800e39fc | Name: particle_emitter_spawn
// MASSIVE primary particle emitter creation function (80+ callers).
// Steps:
//   1. Pop emitter from pool
//   2. Set position (param_4) and direction (param_5) if provided
//   3. Init from definition table (particle_emitter_init_from_def)
//   4. Set player mask to -1 (all players)
//   5. If flag 0x20 + direction: compute velocity from direction with RNG scale
//   6. Compute gravity from def table with RNG
//   7. If flag 0x2: floor query for bounce height, then clear flag
//   8. If flag 0x40: disable emitter immediately (one-shot pre-spawn)
//   9. If def has sub-emitter or flag 0x10: set deferred flag 0x1000
//  10. If not deferred: update rooms + visibility cull (may free if invisible)
//  11. Link handle bidirectionally (handle -> emitter, emitter[0] -> handle)
// ============================================================================
void particle_emitter_spawn(u32 param_1, u32 param_2, u32 flags,
                            float* position, float* direction) {
    u64 ctx = FUN_803826e8();
    int* handle_ptr = (int*)(ctx >> 32);
    int* def = (int*)(u32)ctx;

    DAT_80611e30 = (u32*)particle_emitter_pool_pop();
    if (DAT_80611e30 == NULL) goto end;

    // Set position
    if (position != NULL) {
        DAT_80611e30[5] = *(u32*)&position[0];
        DAT_80611e30[6] = *(u32*)&position[1];
        DAT_80611e30[7] = *(u32*)&position[2];
    }

    // Set direction
    if (direction != NULL) {
        DAT_80611e30[0xb] = *(u32*)&direction[0];
        DAT_80611e30[0xc] = *(u32*)&direction[1];
        DAT_80611e30[0xd] = *(u32*)&direction[2];
    }

    // Init from definition
    particle_emitter_init_from_def((int)DAT_80611e30, (int)def);
    DAT_80611e30[0x1e] = 0xFFFFFFFF;  // player mask = all
    DAT_80611e30[0x1f] = 0xFFFFFFFF;

    // Merge caller flags
    DAT_80611e30[1] |= flags;

    // Velocity from direction (flag 0x20)
    if ((DAT_80611e30[1] & 0x20) != 0 && direction != NULL) {
        float rng = FUN_801310c0();
        float speed = (float)def[0x1e] * (FLOAT_80615074 * rng - FLOAT_80615078) + (float)def[0x1d];
        DAT_80611e30[0x26] = *(u32*)&(direction[0] * speed);
        DAT_80611e30[0x27] = *(u32*)&(direction[1] * speed);
        DAT_80611e30[0x28] = *(u32*)&(direction[2] * speed);
        // Negate direction for emission cone
        DAT_80611e30[0xb] = *(u32*)&(-direction[0]);
        DAT_80611e30[0xc] = *(u32*)&(-direction[1]);
        DAT_80611e30[0xd] = *(u32*)&(-direction[2]);
        DAT_80611e30[0x29] = def[0x1c];  // gravity
        // Offset position along velocity
        float t = FLOAT_8061507c / FLOAT_80615080;
        *(float*)&DAT_80611e30[5] += t * *(float*)&DAT_80611e30[0x26];
        *(float*)&DAT_80611e30[6] += t * *(float*)&DAT_80611e30[0x27];
        *(float*)&DAT_80611e30[7] += t * *(float*)&DAT_80611e30[0x28];
    }

    // Gravity from def with RNG
    float rng2 = FUN_801310c0();
    DAT_80611e30[0x2a] = *(u32*)&((float)def[0x20] *
        (FLOAT_80615074 * rng2 - FLOAT_80615078) + (float)def[0x1f]);

    // Floor query for bounce (flag 0x2)
    if ((DAT_80611e30[1] & 2) != 0) {
        float query_pos[3];
        int floor_result = 0;
        query_pos[0] = *(float*)&DAT_80611e30[5] + (direction ? direction[0] : 0);
        query_pos[1] = *(float*)&DAT_80611e30[6] + (direction ? direction[1] : 0);
        query_pos[2] = *(float*)&DAT_80611e30[7] + (direction ? direction[2] : 0);
        DAT_80611e30[1] &= ~2;
        float floor_y = floor_query_with_interact(FLOAT_80615084, FLOAT_80615088,
                                                   query_pos, &floor_result, NULL, 0);
        if (floor_result != 0) {
            particle_cached_set_floor_height(floor_y);
        }
    }

    // One-shot disable (flag 0x40)
    if ((DAT_80611e30[1] & 0x40) != 0) {
        *(u8*)((int)DAT_80611e30 + 0x12) = 0;
        *(u8*)(DAT_80611e30 + 4) = 0;
        DAT_80611e30[1] &= ~0x40;
    }

    // Deferred rendering (sub-emitter or flag 0x10)
    if ((def[0x15] >= 0 && *def != 7) || (DAT_80611e30[1] & 0x10) != 0) {
        DAT_80611e30[1] |= 0x1000;
    }

    // Room update + visibility cull (if not deferred)
    if ((DAT_80611e30[1] & 0x1000) == 0 && position != NULL) {
        particle_emitter_update_rooms((int)DAT_80611e30);
        int culled = particle_emitter_cull_check((int)DAT_80611e30);
        if (culled != 0) goto end;
    }

    // Link handle bidirectionally
    if (handle_ptr != NULL) {
        *DAT_80611e30 = (u32)handle_ptr;
        *handle_ptr = (int)DAT_80611e30;
    }

end:
    FUN_80382734();
}

// ============================================================================
// Address: 0x800e3d3c | Name: particle_emitter_spawn_attached
// Wrapper: spawns emitter then attaches to parent entity.
// Sets burst flag 0x200000 if def has no sub-emitter (def[0x15] < 0).
// Sets entity pointer at +0x88, owner flag 0x80000 on entity, callback at +0x8c.
// Called from entity_alloc_render_data.
// ============================================================================
void particle_emitter_spawn_attached(u32 p1, u32 p2, int entity, u32 callback) {
    u64 ctx = FUN_803826f0();
    int* handle_ptr = (int*)(ctx >> 32);

    particle_emitter_spawn(handle_ptr, (int)ctx, 0, NULL, NULL);
    int emitter = *handle_ptr;

    if (emitter != 0) {
        // Set burst if no sub-emitter
        if (*(int*)(*(int*)(emitter + 0xc) + 0x54) < 0) {
            *(u32*)(emitter + 4) |= 0x200000;
        }
        *(int*)(emitter + 0x88) = entity;
        *(u32*)(entity + 0x28) |= 0x80000;  // owner flag
        *(u32*)(emitter + 0x8c) = callback;
    }

    FUN_8038273c();
}

// ============================================================================
// Address: 0x800e3dc0 | Name: particle_cached_set_entity
// Set parent entity on the cached emitter (DAT_80611e30).
// Also sets flag 0x100000 (entity-attached). No-op if no cached emitter.
// ============================================================================
void particle_cached_set_entity(u32 entity) {
    if (DAT_80611e30 == 0) return;
    *(u32*)(DAT_80611e30 + 0x88) = entity;
    *(u32*)(DAT_80611e30 + 4) |= 0x100000;
}

// ============================================================================
// Address: 0x800e3de4 | Name: particle_cached_set_entity_no_flag
// Set parent entity on cached emitter without setting attached flag.
// Used for position-only tracking without lifecycle coupling.
// ============================================================================
void particle_cached_set_entity_no_flag(u32 entity) {
    if (DAT_80611e30 == 0) return;
    *(u32*)(DAT_80611e30 + 0x88) = entity;
}

// ============================================================================
// Address: 0x800e3df8 | Name: particle_handle_set_entity
// Set parent entity via handle indirection + flag 0x100000.
// No-op if handle is null.
// ============================================================================
void particle_handle_set_entity(int* handle, u32 entity) {
    int emitter = *handle;
    if (emitter == 0) return;
    *(u32*)(emitter + 0x88) = entity;
    *(u32*)(emitter + 4) |= 0x100000;
}

// ============================================================================
// Address: 0x800e3e18 | Name: particle_cached_set_delay
// Set emission delay timer on cached emitter (+0x64).
// Emitter won't start emitting until delay expires.
// ============================================================================
void particle_cached_set_delay(float delay) {
    if (DAT_80611e30 == 0) return;
    *(float*)(DAT_80611e30 + 0x64) = delay;
}

// ============================================================================
// Address: 0x800e3e2c | Name: particle_cached_set_floor_height
// Set floor height for bounce on cached emitter (+0x84).
// Enables bounce flag 0x4, clears flag 0x2 (floor query consumed).
// ============================================================================
void particle_cached_set_floor_height(float floor_y) {
    if (DAT_80611e30 == 0) return;
    *(float*)(DAT_80611e30 + 0x84) = floor_y;
    *(u32*)(DAT_80611e30 + 4) |= 4;    // enable bounce
    *(u32*)(DAT_80611e30 + 4) &= ~2;   // clear floor query flag
}
