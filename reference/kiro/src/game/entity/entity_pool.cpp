// Address: 0x80071d04 - 0x80071f87 | Batch 162 | Date: 2026-08-10
// Entity pool allocation and scratch buffer management.
// Part of the entity subsystem (game)

#include "common.h"

// ============================================================================
// External references
// ============================================================================

extern int g_entity_max_id;          // DAT_80611c20
extern int g_entity_pool_base;       // DAT_80611c24
extern int g_entity_pool_count;      // DAT_80611c70 (current high-water mark)
extern int g_entity_free_count;      // DAT_80611c6c (recycled slot count)
extern int g_scratch_head;           // DAT_80540df4 (scratch buffer linked list head)

extern "C" {
    int entity_find_by_id(int entity_id);
    int mem_alloc(/* variadic */);
    void memset_wrapper(int dst, int val, int size);
}

// Entity pool stride
#define ENTITY_STRIDE 0x2E8

// Scratch buffer block size
#define SCRATCH_BLOCK_SIZE    0x19000
#define SCRATCH_BLOCK_ALLOC   0x19010  // includes 16-byte header

// ============================================================================
// 0x80071d04 | entity_find_free_id
// Searches downward from g_entity_max_id for an entity ID that is not
// currently in use (entity_find_by_id returns 0 for unused IDs).
// Returns the free ID, or -1 if none available.
// ============================================================================

int entity_find_free_id(void)
{
    int entity_id = g_entity_max_id;

    do {
        entity_id--;
        if (entity_id < 0) {
            return -1;
        }
    } while (entity_find_by_id(entity_id) != 0);

    return entity_id;
}

// ============================================================================
// 0x80071d5c | entity_pool_alloc
// Allocates an entity slot from the entity pool.
// - If free-list has entries (g_entity_free_count > 0): scans for a slot
//   with the "deleted" flag (bit 31 of field[10]) set, recycles it.
// - Otherwise: appends at the end (high-water mark allocation).
// Assigns a unique entity ID via entity_find_free_id if needed.
// Returns pointer to the allocated entity (stride 0x2E8).
// ============================================================================

void* entity_pool_alloc(void)
{
    int* entity_ptr = NULL;
    int slot_index = -1;

    if (g_entity_free_count == 0) {
        // Append at high-water mark
        entity_ptr = (int*)(g_entity_pool_base + g_entity_pool_count * ENTITY_STRIDE);
        slot_index = g_entity_pool_count;
        g_entity_pool_count++;
    } else {
        // Scan for recycled slot (deleted flag = bit 31 of dword at offset 0x28)
        int offset = 0;
        for (int i = 0; i < g_entity_pool_count; i++) {
            int* candidate = (int*)(g_entity_pool_base + offset);
            if ((candidate[10] & 0x80000000) != 0) {
                slot_index = i;
                entity_ptr = candidate;
                break;
            }
            offset += ENTITY_STRIDE;
        }

        if (entity_ptr == NULL) {
            // No recycled slot found; reset free count, append
            g_entity_free_count = 0;
            entity_ptr = (int*)(g_entity_pool_base + g_entity_pool_count * ENTITY_STRIDE);
            slot_index = g_entity_pool_count;
            g_entity_pool_count++;
        } else {
            g_entity_free_count--;
            entity_ptr[10] = 0;  // Clear deleted flag
        }
    }

    // Assign entity ID
    entity_ptr[0x22] = -1;  // offset 0x88 = entity_id field (0x22 * 4 = 0x88)
    int candidate_id = entity_find_by_id(slot_index);
    if (candidate_id == 0) {
        // slot_index is available as entity ID
        entity_ptr[0x22] = slot_index;
    } else {
        // slot_index already in use; find another free ID
        int free_id = entity_find_free_id();
        entity_ptr[0x22] = free_id;
    }

    // Clear first field
    entity_ptr[0] = 0;

    return entity_ptr;
}

// ============================================================================
// 0x80071e74 | entity_scratch_clear_all
// Walks the scratch buffer linked list and resets all usage counters to 0.
// Each node: [next_ptr, used_bytes, ...data...]
// ============================================================================

void entity_scratch_clear_all(void)
{
    int* node = (int*)&g_scratch_head;

    while (node != NULL) {
        node[1] = 0;  // Reset used_bytes
        node = (int*)node[0];  // Follow next pointer
    }
}

// ============================================================================
// 0x80071e98 | entity_scratch_reset
// Resets the scratch buffer system entirely: clears the head pointer
// and zeros all usage counters.
// ============================================================================

void entity_scratch_reset(void)
{
    g_scratch_head = 0;
    entity_scratch_clear_all();
}

// ============================================================================
// 0x80071ec4 | entity_scratch_alloc
// Allocates 'size' bytes from the entity scratch buffer system.
// Scratch buffers are 0x19000-byte blocks in a linked list.
// If the current block doesn't have enough space, walks to the next.
// If no block has space, allocates a new 0x19010-byte block from the heap.
// Returns pointer to the allocated region (zeroed).
// Max single allocation: 0x19000 bytes.
// ============================================================================

void entity_scratch_alloc(unsigned int size)
{
    // Uses FUN_803826e8 to get requested size (from register context)
    // Actual size comes from paired-single register protocol

    if (size > SCRATCH_BLOCK_SIZE) {
        // Allocation too large, silently fail
        return;
    }

    int* current = (int*)&g_scratch_head;

    // Find a block with enough remaining space
    while (SCRATCH_BLOCK_SIZE - current[1] < (int)size) {
        int* next = (int*)current[0];
        if (next == NULL) {
            // Allocate new block
            int new_block = mem_alloc(/* size = */ SCRATCH_BLOCK_ALLOC, /* align = */ 1);
            current[0] = new_block;
            next = (int*)new_block;
            next[0] = 0;       // next = NULL
            next[1] = 0;       // used = 0
        }
        current = next;
    }

    // Allocate from this block
    int used = current[1];
    current[1] = used + size;

    // Zero the allocated region (data starts at offset +8 in the block)
    memset_wrapper((int)current + used + 8, 0, size);

    // Return via FUN_80382734 (register context restore)
}
