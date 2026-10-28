// Address: 0x800e33f0 - 0x800e3690 | Batch 243 | Date: 2026-08-11
// Particle emitter handle/reference management + room visibility update
// Handle system: indirect pointer (*handle -> emitter, emitter+0x00 -> handle)
// Related: particle_emitter_pool.cpp, particle_emitter_tick.cpp

#include "common.h"

// ============================================================================
// Address: 0x800e33f0 | Name: particle_effect_destroy_all
// Iterates an effect's emitter reference list and destroys/disables all.
// Non-persistent emitters (flag 0x2 clear) are freed via pool_free.
// Persistent emitters (flag 0x2 set) are disabled (loop=0, owner=0).
// Also walks the global active list if owner flag 0x80000 is set.
// Called from entity_destroy and spawn_create_instance.
// ============================================================================
void particle_effect_destroy_all(void) {
    int effect = FUN_803826e8();

    // Iterate effect's emitter reference array
    int count = *(int*)(effect + 0x1e8);
    int offset = 0;
    for (int i = 0; i < count; i++) {
        u32* emitter = *(u32**)(*(int*)(effect + 0x1fc) + offset);
        if ((emitter[2] & 2) == 0) {
            // Non-persistent: free immediately
            particle_emitter_pool_free(emitter);
        } else {
            // Persistent: disable looping + clear owner
            *(u8*)((int)emitter + 0x12) = 0;
            emitter[0x22] = 0;
        }
        offset += 4;
    }
    *(u32*)(effect + 0x1e8) = 0;

    // Walk global active list if owner has flag 0x80000
    if ((*(u32*)(effect + 0x28) & 0x80000) != 0) {
        u32* emitter = (u32*)DAT_80611e10;
        while (emitter != NULL) {
            u32* next = (u32*)emitter[0x3d];
            if (emitter[0x22] == (u32)effect && (emitter[1] & 0x100000) == 0) {
                if ((emitter[2] & 2) == 0) {
                    particle_emitter_pool_free(emitter);
                } else {
                    *(u8*)((int)emitter + 0x12) = 0;
                    emitter[0x22] = 0;
                }
            }
            emitter = next;
        }
        *(u32*)(effect + 0x28) &= ~0x80000;
    }

    FUN_80382734();
}

// ============================================================================
// Address: 0x800e34d8 | Name: particle_handle_clear
// Clears a handle pointer to NULL. Does NOT notify the emitter.
// Used during pool reset and bulk teardown where emitters are already freed.
// 20 callers across all subsystems.
// ============================================================================
void particle_handle_clear(u32* handle) {
    *handle = 0;
}

// ============================================================================
// Address: 0x800e34e4 | Name: particle_handle_release
// Releases ownership: clears the emitter's back-reference to this handle,
// then clears the handle itself. Safe for dangling emitters.
// Called from character_destroy, player_ai, and various teardown paths.
// ============================================================================
void particle_handle_release(u32* handle) {
    if ((u32*)*handle != NULL) {
        // Clear emitter's back-reference (emitter[0] -> handle)
        *(u32*)*handle = 0;
    }
    *handle = 0;
}

// ============================================================================
// Address: 0x800e3504 | Name: particle_handle_transfer
// Transfers handle ownership from src to dst. Updates emitter back-reference.
// After transfer, src is NULL and dst owns the emitter.
// ============================================================================
void particle_handle_transfer(u32* dst, u32* src) {
    *dst = *src;
    *src = 0;
    if ((u32*)*dst != NULL) {
        // Update emitter's back-reference to point to new owner
        *(u32*)*dst = (u32)dst;
    }
}

// ============================================================================
// Address: 0x800e3528 | Name: particle_handle_is_valid
// Returns true if the handle points to a live emitter (non-NULL).
// Foundational validity check used by 40+ callers across all subsystems.
// ============================================================================
bool particle_handle_is_valid(int* handle) {
    return (*handle != 0);
}

// ============================================================================
// Address: 0x800e3538 | Name: particle_emitter_set_flags
// Validates handle, then modifies emitter flags at +0x04:
//   flags = (flags & ~mask) | new_bits
// If bit 0x10000 is set in new flags, promotes emitter in active list.
// 9 callers including weapon_smoke, entity systems.
// ============================================================================
void particle_emitter_set_flags(u32 new_bits, u32 unused, u32 mask) {
    u64 ctx = FUN_803826f0();
    int* handle_ptr = (int*)(ctx >> 32);

    int local_handle = *handle_ptr;
    if (particle_handle_is_valid(&local_handle)) {
        int emitter = *handle_ptr;
        *(u32*)(emitter + 4) &= ~mask;
        *(u32*)(emitter + 4) |= (u32)ctx;  // new_bits
        if (((u32)ctx & 0x10000) != 0) {
            particle_emitter_pool_promote(emitter);
        }
    }
    FUN_8038273c();
}

// ============================================================================
// Address: 0x800e35b0 | Name: particle_emitter_set_flags2
// Same as particle_emitter_set_flags but modifies secondary flags at +0x08.
// No promote logic.
// ============================================================================
void particle_emitter_set_flags2(u32 new_bits, u32 unused, u32 mask) {
    u64 ctx = FUN_803826f0();
    int* handle_ptr = (int*)(ctx >> 32);

    int local_handle = *handle_ptr;
    if (particle_handle_is_valid(&local_handle)) {
        int emitter = *handle_ptr;
        *(u32*)(emitter + 8) &= ~mask;
        *(u32*)(emitter + 8) |= (u32)ctx;
    }
    FUN_8038273c();
}

// ============================================================================
// Address: 0x800e361c | Name: particle_cached_set_flags2
// Sets secondary flags on the currently-cached emitter (DAT_80611e30).
// No-op if no emitter is cached. Directly modifies +0x08.
// ============================================================================
void particle_cached_set_flags2(u32 new_bits, u32 mask) {
    if (DAT_80611e30 == 0) {
        return;
    }
    *(u32*)(DAT_80611e30 + 8) &= ~mask;
    *(u32*)(DAT_80611e30 + 8) |= new_bits;
}

// ============================================================================
// Address: 0x800e3648 | Name: particle_handle_destroy
// Validates handle then frees the emitter via particle_emitter_pool_free.
// 23 callers - primary destruction path for particle effects.
// ============================================================================
void particle_handle_destroy(int* handle) {
    int local_handle = *handle;
    if (particle_handle_is_valid(&local_handle)) {
        particle_emitter_pool_free((u32*)*handle);
    }
}

// ============================================================================
// Address: 0x800e3690 | Name: particle_emitter_update_rooms
// Updates the emitter's visible room list for scene rendering.
// Two paths:
//   1. If no parent entity (+0x88 == 0) or has AABB flag: performs AABB scene
//      query at +0xC8..+0xDC to find overlapping rooms (max 12).
//   2. If parent entity exists: copies room list from entity (+0x30 count,
//      +0x38 array).
// Room IDs stored as bytes at emitter+0xE4, count at +0xE0.
// ============================================================================
void particle_emitter_update_rooms(int emitter) {
    if (*(int*)(emitter + 0x88) == 0 ||
        ((*(u32*)(emitter + 4) & 0x20000) != 0 &&
         (*(u32*)(emitter + 4) & 0x10000000) != 0)) {
        // AABB-based room query
        float pad = FLOAT_80615070;  // default padding
        if ((*(u32*)(emitter + 4) & 0x20000) != 0) {
            pad = *(float*)(emitter + 0xac);
        }

        // Build padded AABB at +0xC8
        *(float*)(emitter + 0xc8) = (*(float*)(emitter + 0x14) + *(float*)(emitter + 0xb0)) - pad;
        *(float*)(emitter + 0xcc) = (*(float*)(emitter + 0x18) + *(float*)(emitter + 0xb4)) - pad;
        *(float*)(emitter + 0xd0) = (*(float*)(emitter + 0x1c) + *(float*)(emitter + 0xb8)) - pad;
        *(float*)(emitter + 0xd4) = pad + *(float*)(emitter + 0x14) + *(float*)(emitter + 0xbc);
        *(float*)(emitter + 0xd8) = pad + *(float*)(emitter + 0x18) + *(float*)(emitter + 0xc0);
        *(float*)(emitter + 0xdc) = pad + *(float*)(emitter + 0x1c) + *(float*)(emitter + 0xc4);

        // Query scene nodes overlapping AABB (max 12 rooms)
        u32 rooms[12];
        u32 count = scene_query_nodes_in_frustum(0, (int*)(emitter + 0xc8), rooms, 12);
        *(u32*)(emitter + 0xe0) = count;

        for (int i = 0; i < (int)count; i++) {
            *(u8*)(emitter + 0xe4 + i) = (u8)rooms[i];
        }
    } else {
        // Copy room list from parent entity
        int entity = *(int*)(emitter + 0x88);
        *(int*)(emitter + 0xe0) = (int)*(char*)(entity + 0x30);

        int offset = 0;
        for (int i = 0; i < *(int*)(emitter + 0xe0); i++) {
            *(u8*)(emitter + 0xe4 + i) = (u8)*(u32*)(entity + 0x38 + offset);
            offset += 4;
        }
    }
}
