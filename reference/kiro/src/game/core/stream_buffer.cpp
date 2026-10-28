// =============================================================================
// stream_buffer.cpp - Ring buffer allocator for transient data
// Address Range: 0x800507fc - 0x80050a80
// Date: 2026-08-10
// Batch: 106
//
// A circular/ring buffer allocator used for transient notification text,
// memory card data, and streaming content. The pool is a contiguous region
// pointed to by DAT_80611af4 with a roaming cursor (piRam80611af8).
//
// Block header layout (5 dwords, 0x14 bytes at block start):
//   +0x00: free flag (0 = allocated, 1 = free)
//   +0x04: user data 1
//   +0x08: user data 2
//   +0x0C: block total size (including header + user data area)
//   +0x10: prev block size (for backward coalescing)
//
// User data starts at block_ptr + 0x40 (64-byte aligned offset).
// Allocation alignment: 64 bytes (0x40).
// Pool wrap boundary for alloc cursor: base + 0x140000
// Pool total size boundary for free: base + 0x500000
// =============================================================================

#include "stream_buffer.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

// Pool base pointer
static int* g_stream_pool_base; // DAT_80611af4

// Roaming allocation cursor (circular)
static int* g_stream_pool_cursor; // piRam80611af8

// ---------------------------------------------------------------------------
// Address: 0x800507fc | Name: memcard_check_slots
//
// Checks memory card slots (0 and 1) for availability.
// Iterates over 2 card slots calling FUN_80191dac to query status.
// Returns:
//   - First available slot info (type != 4) via output params
//   - Error count via param_3
//
// Output via register pair (Ghidra artifact):
//   High word: slot type (1 = first available)
//   Low word: slot index
// param_3: pointer to error count (incremented per failed slot)
// ---------------------------------------------------------------------------
void memcard_check_slots(int param_1, int param_2, int* error_count)
{
    int slot_type = 1;  // Default: no card
    int slot_index = 0;
    int local_28[10];   // Card status buffer

    // Begin context
    notif_begin_with_args_2(); // FUN_803826e8 - sets up return pair

    *error_count = 0;

    for (int i = 0; i < 2; i++) {
        int result = memcard_query_slot(i, local_28); // FUN_80191dac

        if (result == 0) {
            // Slot accessible
            if (local_28[0] != 4 && slot_type == 1) {
                // First non-empty slot found
                slot_type = local_28[0];
                slot_index = i;
            }
        } else {
            // Slot error
            (*error_count)++;
        }
    }

    // Returns slot_type and slot_index via register pair
    notif_end_with_args_2(); // FUN_80382734
}

// ---------------------------------------------------------------------------
// Address: 0x800508ac | Name: stream_buffer_alloc
//
// Allocates a block from the circular streaming buffer.
// Searches forward from the roaming cursor for a free block large enough
// to hold the requested size (aligned to 64 bytes + 0x40 header + 0x40 extra).
//
// Algorithm:
// 1. Start at cursor position
// 2. Scan forward for a free block (flag[0] != 0) with sufficient size
// 3. Track whether any allocated blocks exist (bVar2) for wraparound detection
// 4. On wrap: restart from pool base. If wrapped twice with allocations, fail.
// 5. On success: split block - mark front as allocated, remainder as new free
// 6. Update cursor to the new free block
// 7. Return pointer to user data area (block + 0x40)
//
// Returns: pointer to allocated memory, or 0 on failure
//
// param_1: requested allocation size (bytes)
// ---------------------------------------------------------------------------
int stream_buffer_alloc(int size)
{
    bool wrapped = false;
    bool has_allocated = false;
    int aligned_size = (size + 0x3F) & ~0x3F; // 64-byte align
    int total_needed = aligned_size + 0x80;    // +header+alignment

    int* cursor = g_stream_pool_cursor;

    while (true) {
        // Track if any allocated blocks seen
        if (cursor[1] != 0) {
            has_allocated = true;
        }

        // Check if this block is free and large enough
        if (cursor[0] != 0 && (uint)cursor[3] >= total_needed) {
            break; // Found suitable block
        }

        // Advance cursor
        int* next = (int*)((int)cursor + cursor[3]);
        if (next < g_stream_pool_base + 0x50000) { // 0x140000 / 4
            cursor = next;
        } else {
            // Wrap around
            cursor = g_stream_pool_base;
            if (wrapped && has_allocated) {
                return 0; // Pool exhausted
            }
            wrapped = true;
        }
    }

    // Split block: allocate front portion, leave remainder free
    int* new_free = (int*)((int)cursor + aligned_size + 0x40);
    int* user_data = cursor + 0x10; // +0x40 bytes = user data start

    // Calculate next block address for back-link update
    int next_block = 0;
    int* after = (int*)((int)cursor + cursor[3]);
    if (after < g_stream_pool_base + 0x50000) {
        next_block = (int)after;
    }

    // Setup new free block header
    new_free[0] = 1;       // free flag
    new_free[1] = 0;       // user data 1
    new_free[2] = 0;       // user data 2
    new_free[3] = cursor[3] - (aligned_size + 0x40); // remaining size
    new_free[4] = aligned_size + 0x40;               // prev block size

    // Update allocated block
    cursor[0] = 0;         // mark allocated
    cursor[3] = aligned_size + 0x40; // new size of this block

    // Update cursor
    g_stream_pool_cursor = new_free;

    // Update next block's back-link if exists
    if (next_block != 0) {
        *(int*)(next_block + 0x10) = new_free[3];
    }

    return (int)user_data;
}

// ---------------------------------------------------------------------------
// Address: 0x80050a80 | Name: stream_buffer_free
//
// Frees a block in the streaming ring buffer and coalesces adjacent free
// blocks (merges with previous and/or next block if they are free).
//
// Algorithm:
// 1. Find previous block using back-link (block - block[4])
// 2. Find next block (block + block[3])
// 3. Find block after next (for back-link update)
// 4. Mark current block as free
// 5. If prev is free: merge current into prev (prev.size += current.size)
// 6. If next is free: merge next into result (result.size += next.size)
// 7. Update cursor if merged block replaces current cursor position
//
// param_1: pointer to block header (NOT user data - caller subtracts 0x40)
// ---------------------------------------------------------------------------
void stream_buffer_free(int* block)
{
    int* prev = NULL;
    int* next = NULL;
    int after_next = 0;

    // Find previous block
    if (block[4] > 0) {
        prev = (int*)((int)block - block[4]);
    }

    // Find next block and block-after-next
    int* potential_next = (int*)((int)block + block[3]);
    if ((uint)potential_next < (uint)g_stream_pool_base + 0x500000) {
        next = potential_next;
        int* after = (int*)((int)next + next[3]);
        if ((uint)after < (uint)g_stream_pool_base + 0x500000) {
            after_next = (int)after;
        }
    }

    // Mark block as free
    block[0] = 1;
    block[1] = 0;
    block[2] = 0;

    // Coalesce with previous block if free
    if (prev != NULL && prev[0] != 0) {
        prev[3] += block[3]; // Merge sizes
        block = prev;        // Result is now prev

        // Update next block's back-link
        if (next != NULL) {
            next[4] = prev[3];
        }
    }

    // Coalesce with next block if free
    if (next != NULL && next[0] != 0) {
        block[3] += next[3]; // Merge sizes

        // Update block-after-next's back-link
        if (after_next != 0) {
            *(int*)(after_next + 0x10) = block[3];
        }
    }

    // Update cursor if it pointed to merged-away next block
    if (next == g_stream_pool_cursor) {
        g_stream_pool_cursor = block;
    }
}
