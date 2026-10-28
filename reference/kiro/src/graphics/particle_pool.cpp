// Address: 0x800e7578 - 0x800e8324 | Particle Pool Management | Date: 2026-08-25
// Manages the particle instance pool: allocation, reset, shutdown, and bucket queries.
// Pool size varies by player count (150 for 1P, 150 for 2P, 200 for 3P, 265 for 4P).
// Each particle slot is 0xB8 bytes. A single scratch particle (0xC0 bytes) is also allocated.

#include "particle.h"
#include "memory.h"

// ============================================================================
// Address: 0x800e7578 | Name: particle_pool_reset
// Full reset of the particle system pools. Clears all active particles,
// resets bucket counters, and reinitializes all 0xB8-byte particle slots.
// Also resets the 16-slot emitter priority table and per-bucket counters.
// Called from particle_pool_alloc and during level transitions.
// ============================================================================
void particle_pool_reset(void) {
    int i;

    g_particle_active_count = 0;
    g_particle_pending_count = 0;
    g_particle_free_count = 0;

    // Clear 3000-byte tracking buffer
    memset_wrapper((int)&g_particle_tracking_buffer, 0, 3000);

    // Compute bucket size from max particle count
    uint bucket_raw = g_particle_max_count + 0x10;
    g_particle_pool_size = g_particle_max_count;
    g_particle_bucket_size = (bucket_raw >> 4) + (uint)(bucket_raw < 0 && (bucket_raw & 0xf) != 0);
    if (g_particle_bucket_size < 1) {
        g_particle_bucket_size = 1;
    }

    // Compute particles per bucket (capped at 150)
    g_particles_per_bucket = g_particle_budget / g_particle_bucket_size;
    if (g_particles_per_bucket > 150) {
        g_particles_per_bucket = 150;
    }

    // Clear per-bucket counters
    for (i = 0; i < g_particle_bucket_size; i++) {
        g_particle_bucket_counts[i] = 0;
    }

    g_particle_overflow_count = 0;

    // Clear 3 priority tables (0xD5 entries x 4 bytes each = 0x354 per table)
    int offset = 0;
    for (int t = 3; t > 0; t--) {
        for (int j = 0xD4; j >= 0; j--) {
            g_particle_priority_table[j + offset / 4] = 0;
        }
        offset += 0x354;
    }

    // Initialize each particle slot (0xB8 bytes per slot)
    int slot_offset = 0;
    for (i = 0; i < g_particle_budget; i++) {
        *(uint16_t*)(g_particle_pool + slot_offset + 0x96) = 0xFFFF; // invalid marker
        *(uint32_t*)(g_particle_pool + slot_offset + 0x80) = 0;
        *(uint32_t*)(g_particle_pool + slot_offset + 0x84) = 0;
        *(uint32_t*)(g_particle_pool + slot_offset + 0x88) = 0;
        *(uint32_t*)(g_particle_pool + slot_offset + 0x8c) = 0;
        *(uint8_t*)(g_particle_pool + slot_offset + 0x99) = 0;
        slot_offset += 0xB8;
    }

    // Initialize scratch particle
    *(uint16_t*)(g_particle_scratch + 0x96) = 0xFFFF;
    *(uint32_t*)(g_particle_scratch + 0x80) = 0;
    *(uint32_t*)(g_particle_scratch + 0x84) = 0;
    *(uint32_t*)(g_particle_scratch + 0x88) = 0;
    *(uint32_t*)(g_particle_scratch + 0x8c) = 0;
    *(uint8_t*)(g_particle_scratch + 0x99) = 0;

    // Reset 16-slot emitter priority queue (100 bytes each)
    for (i = 0; i < 16; i++) {
        g_particle_emitter_queue[i] = 0;
    }

    g_particle_frame_alloc_count = 0;
    g_particle_frame_free_count = 0;
}

// ============================================================================
// Address: 0x800e7740 | Name: particle_pool_alloc
// Allocates particle pool memory based on player count.
// Budget: 1P=150, 2P=150, 3P=200, 4P=265. Each slot = 0xB8 bytes (16-aligned).
// Also allocates a single 0xC0-byte scratch particle. Calls particle_pool_reset.
// ============================================================================
void particle_pool_alloc(void) {
    int player_count = get_player_count();

    // Set budget based on player count
    if (player_count == 3) {
        g_particle_budget = 265; // 0x109
    } else if (player_count == 2) {
        g_particle_budget = 150; // 0x96
    } else if (player_count == 4) {
        g_particle_budget = 200; // 0xC8
    } else {
        g_particle_budget = 150; // default
    }

    // Allocate main pool (0xB8 per slot, 16-byte aligned)
    uint alloc_size = (g_particle_budget * 0xB8 + 0xF) & 0xFFFFFFF0;
    g_particle_pool = (uint8_t*)mem_alloc(alloc_size, 1);
    memset_wrapper((int)g_particle_pool, 0, alloc_size);

    // Allocate scratch particle (0xC0 bytes)
    g_particle_scratch = (uint8_t*)mem_alloc(0xC0, 1);
    memset_wrapper((int)g_particle_scratch, 0, 0xC0);

    particle_pool_reset();
}

// ============================================================================
// Address: 0x800e7808 | Name: particle_pool_shutdown
// Shuts down particle pool by nulling the pool pointer.
// Only executes if not in loading state (DAT_80611568 == 0).
// ============================================================================
void particle_pool_shutdown(void) {
    if (g_loading_state != 0) return;
    g_particle_pool = NULL;
}

// ============================================================================
// Address: 0x800e8324 | Name: particle_pool_get_bucket
// Returns the bucket index for a particle slot.
// If slot is full (count == 0xD5) or slot flag > 1, returns the max bucket count.
// Otherwise returns the slot's current bucket value.
// Used for priority-based particle allocation/eviction.
// ============================================================================
int particle_pool_get_bucket(int slot) {
    bool is_full = false;
    if (*(int*)(slot + 0x34) == 0xD5 || *(char*)(slot + 0x30) > 1) {
        is_full = true;
    }
    int bucket = *(int*)(slot + 0x34);
    if (is_full) {
        bucket = g_particle_pool_size;
    }
    return bucket;
}
