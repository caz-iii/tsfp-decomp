// Address: 0x800e3104 - 0x800e332c | Batch 242 | Date: 2026-08-11
// Particle emitter pool: doubly-linked list pool management (alloc/free/promote)
// Pool: 250 emitters x 0xF8 bytes each. Called from weather_system_alloc/reset.

#include "common.h"

// Globals (at 0x80611e08..0x80611e30):
// DAT_80611e08 = pool base pointer
// DAT_80611e0c = free list head
// DAT_80611e10 = active list tail
// DAT_80611e14 = active list head
// DAT_80611e18 = active count
// DAT_80611e1c = pool capacity (250)
// DAT_80611e30 = cached "current" emitter pointer

// ============================================================================
// Address: 0x800e3104 | Name: particle_emitter_pool_alloc
// Allocates the emitter pool: 250 slots * 0xF8 bytes = 62000 bytes.
// Called from weather_system_alloc during level load.
// ============================================================================
void particle_emitter_pool_alloc(void) {
    DAT_80611e1c = 250;  // capacity
    DAT_80611e08 = mem_alloc(62000, 1);
    particle_emitter_pool_reset();
}

// ============================================================================
// Address: 0x800e3140 | Name: particle_emitter_pool_reset
// Resets the pool: memset to zero, builds doubly-linked free list.
// Each slot is 0xF8 bytes with prev/next pointers at +0xF0/+0xF4.
// Called from pool_alloc and weather_system_reset.
// ============================================================================
void particle_emitter_pool_reset(void) {
    memset_wrapper(DAT_80611e08, 0, DAT_80611e1c * 0xF8);

    int offset = 0;
    DAT_80611e18 = 0;  // active count = 0

    for (int i = 0; i < DAT_80611e1c; i++) {
        // prev pointer (+0xF0)
        if (i == 0) {
            *(u32*)(DAT_80611e08 + offset + 0xF0) = 0;
        } else {
            *(int*)(DAT_80611e08 + offset + 0xF0) = DAT_80611e08 + (i - 1) * 0xF8;
        }
        // next pointer (+0xF4)
        if (i == DAT_80611e1c - 1) {
            *(u32*)(DAT_80611e08 + offset + 0xF4) = 0;
        } else {
            *(int*)(DAT_80611e08 + offset + 0xF4) = DAT_80611e08 + (i + 1) * 0xF8;
        }
        offset += 0xF8;
    }

    DAT_80611e10 = 0;         // active tail = null
    DAT_80611e0c = DAT_80611e08;  // free head = pool base
    DAT_80611e14 = 0;         // active head = null
    DAT_80611e30 = 0;         // cached emitter = null
    FUN_800e34d8(&DAT_80611e2c);  // reset sub-state
}

// ============================================================================
// Address: 0x800e3224 | Name: particle_emitter_pool_pop
// Pops an emitter from the free list and moves it to the active list head.
// Returns pointer to the allocated 0xF8-byte emitter slot, or 0 if full.
// ============================================================================
int particle_emitter_pool_pop(void) {
    int slot = DAT_80611e0c;
    if (slot == 0) {
        return 0;  // pool exhausted
    }

    // Remove from free list
    DAT_80611e0c = *(int*)(slot + 0xF4);
    if (DAT_80611e0c != 0) {
        *(u32*)(DAT_80611e0c + 0xF0) = 0;
    }

    // Clear slot
    memset_wrapper(slot, 0, 0xF8);

    // Insert at active list head
    *(int*)(slot + 0xF0) = DAT_80611e14;
    if (DAT_80611e14 != 0) {
        *(int*)(DAT_80611e14 + 0xF4) = slot;
    }
    DAT_80611e14 = slot;

    // If active tail is null, this is the only element
    if (DAT_80611e10 == 0) {
        DAT_80611e10 = slot;
    }

    DAT_80611e18++;  // active count++
    return slot;
}

// ============================================================================
// Address: 0x800e32c4 | Name: particle_emitter_pool_promote
// Moves an emitter to the head of the active list (priority/LRU).
// No-op if already at tail (last rendered).
// ============================================================================
void particle_emitter_pool_promote(int emitter) {
    if (emitter == DAT_80611e10) {
        return;  // already at tail, no-op
    }

    // Unlink from current position
    if (*(int*)(emitter + 0xF4) == 0) {
        DAT_80611e14 = *(u32*)(emitter + 0xF0);
    } else {
        *(u32*)(*(int*)(emitter + 0xF4) + 0xF0) = *(u32*)(emitter + 0xF0);
    }
    if (*(int*)(emitter + 0xF0) != 0) {
        *(u32*)(*(int*)(emitter + 0xF0) + 0xF4) = *(u32*)(emitter + 0xF4);
    }

    // Insert at tail
    if (DAT_80611e10 != 0) {
        *(int*)(DAT_80611e10 + 0xF0) = emitter;
    }
    *(int*)(emitter + 0xF4) = DAT_80611e10;
    DAT_80611e10 = emitter;
    *(u32*)(emitter + 0xF0) = 0;
}

// ============================================================================
// Address: 0x800e332c | Name: particle_emitter_pool_free
// Frees an emitter: releases particle buffer, unlinks from active list,
// returns to free list head. Clears cached pointer if it matches.
// ============================================================================
void particle_emitter_pool_free(u32* emitter) {
    // Free particle ring buffer
    particle_buffer_free((int)emitter);

    // Clear parent reference
    if ((u32*)*emitter != NULL) {
        *(u32*)*emitter = 0;
    }

    // Unlink from active list (prev/next at +0xF0/+0xF4 = indices 0x3c/0x3d)
    if (emitter[0x3d] == 0) {
        DAT_80611e14 = emitter[0x3c];
    } else {
        *(u32*)(emitter[0x3d] + 0xF0) = emitter[0x3c];
    }
    if (emitter[0x3c] == 0) {
        DAT_80611e10 = emitter[0x3d];
    } else {
        *(u32*)(emitter[0x3c] + 0xF4) = emitter[0x3d];
    }

    // Return to free list
    emitter[0x3c] = 0;
    emitter[0x3d] = (u32)DAT_80611e0c;
    if (DAT_80611e0c != NULL) {
        *(u32*)((int)DAT_80611e0c + 0xF0) = (u32)emitter;
    }
    DAT_80611e0c = emitter;
    DAT_80611e18--;  // active count--

    // Clear cached pointer
    if (emitter == (u32*)DAT_80611e30) {
        DAT_80611e30 = NULL;
    }
}
