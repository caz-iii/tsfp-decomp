// =============================================================================
// event_dispatch.cpp - Event/message dispatch system
// TimeSplitters: Future Perfect (GameCube) decompilation
// Batch 159 | Date: 2026-08-10
//
// Implements a lightweight event dispatch system with:
//   - 3 type-level callback slots (DAT_8053f4c8)
//   - 32x6 handler dispatch matrix (DAT_8053f4d4) indexed by bit position
//   - An event queue for deferred processing
//   - Bit-mask based event classification (power-of-2 event IDs)
// =============================================================================

#include "common.h"

// =============================================================================
// Globals
// =============================================================================
// DAT_8053f450 - Event type name/data table (indexed by type)
// DAT_8053f4c8 - Type-level callback table (3 entries)
// DAT_8053f4d4 - 32x6 handler dispatch matrix (32 event bits x 6 subtypes)
// DAT_80611c40 - Event queue write index
// DAT_80611c44 - Event queue buffer pointer
// DAT_80611c48 - Active event context/owner

// =============================================================================
// 0x80070170 - event_get_type_data
// Returns the event type data entry at the given index from the global
// event type table (DAT_8053f450). Used to look up event names or metadata.
// =============================================================================
int event_get_type_data(int type_index) {
    return g_event_type_table[type_index];  // DAT_8053f450
}

// =============================================================================
// 0x80070184 - event_queue_push
// Pushes an event object onto the pending event queue. Sets the QUEUED flag
// (bit 26) and clears the PROCESSED flag (bit 25) in the object's flags
// field at offset +0x10. Queue buffer is at DAT_80611c44, write index at
// DAT_80611c40.
// No-ops if queue buffer is NULL (system not initialized).
// =============================================================================
void event_queue_push(int event_obj) {
    if (g_event_queue_buffer == 0) {  // DAT_80611c44
        return;
    }

    int write_idx = g_event_queue_write_idx;  // DAT_80611c40
    g_event_queue_write_idx = write_idx + 1;
    *(int*)(g_event_queue_buffer + write_idx * 4) = event_obj;

    // Set QUEUED flag (bit 26)
    *(u32*)(event_obj + 0x10) |= 0x4000000;
    // Clear PROCESSED flag (bit 25)
    *(u32*)(event_obj + 0x10) &= ~0x2000000u;
}

// =============================================================================
// 0x800701c0 - event_register_type_handler
// Registers a callback function for a given event type index (0-2).
// Stored in the type-level callback table (DAT_8053f4c8).
// =============================================================================
void event_register_type_handler(int type_index, void* handler) {
    g_event_type_handlers[type_index] = handler;  // DAT_8053f4c8
}

// =============================================================================
// 0x800701d4 - event_invoke_type_handler
// Invokes the registered type-level handler for the given type index.
// Passes param_2 as the argument. No-ops if handler is NULL.
// =============================================================================
void event_invoke_type_handler(int type_index, int param) {
    void (*handler)(int) = g_event_type_handlers[type_index];  // DAT_8053f4c8
    if (handler != NULL) {
        handler(param);
    }
}

// =============================================================================
// 0x80070214 - event_bit_to_index
// Converts a power-of-2 event mask to its bit index (0-31).
// Equivalent to log2 for single-bit values. Shifts right until the value
// equals 1, counting iterations. Returns -1 if no single bit is set
// within 32 iterations (invalid input).
// =============================================================================
int event_bit_to_index(int mask) {
    int index = 0;
    int remaining = 32;

    do {
        if (mask == 1) {
            return index;
        }
        mask >>= 1;
        index++;
        remaining--;
    } while (remaining != 0);

    return -1;
}

// =============================================================================
// 0x80070244 - event_set_handler
// Registers a handler in the 32x6 dispatch matrix. The event_mask must be
// a power-of-2 (single bit set). The subtype selects which of the 6 slots
// for that event bit to write. Each row is 6 entries (stride 0x18 bytes).
// =============================================================================
void event_set_handler(int event_mask, int subtype, void* handler) {
    int bit_idx = event_bit_to_index(event_mask);
    g_event_dispatch_matrix[bit_idx * 6 + subtype] = handler;  // DAT_8053f4d4
}

// =============================================================================
// 0x80070294 - event_has_handler
// Checks whether a handler is registered for the given event_mask + subtype
// combination in the 32x6 dispatch matrix. Returns true if non-null.
// =============================================================================
bool event_has_handler(int event_mask, int subtype) {
    int bit_idx = event_bit_to_index(event_mask);
    return g_event_dispatch_matrix[bit_idx * 6 + subtype] != 0;  // DAT_8053f4d4
}

// =============================================================================
// 0x800702e0 - event_dispatch
// Main event dispatch function. Reads the event context (via paired register
// return from FUN_803826f0), extracts the event mask from ctx+0x20, converts
// to bit index, then calls the appropriate handler from the 32x6 matrix.
// The subtype comes from the low part of the context return.
// Handler signature: void handler(int context_ptr, int param_3)
// =============================================================================
void event_dispatch(int unused1, int unused2, int param) {
    // Get event context (paired return: high=context ptr, low=subtype)
    u64 ctx_pair = os_get_event_context();  // FUN_803826f0
    int ctx_ptr = (int)(ctx_pair >> 32);
    int subtype = (int)ctx_pair;

    int event_mask = *(int*)(ctx_ptr + 0x20);
    int bit_idx = event_bit_to_index(event_mask);

    void (*handler)(int, int) = g_event_dispatch_matrix[bit_idx * 6 + subtype];
    if (handler != NULL) {
        handler(ctx_ptr, param);
    }

    os_restore_event_context();  // FUN_8038273c
}

// =============================================================================
// 0x8007035c - event_set_active_context
// Sets the global active event context/owner pointer. Used to track which
// entity or system is currently processing events.
// =============================================================================
void event_set_active_context(int context) {
    g_event_active_context = context;  // DAT_80611c48
}

// =============================================================================
// 0x80070364 - event_system_reset
// Resets the entire event dispatch system. Clears:
//   - All 32x6 handler slots in the dispatch matrix (32 rows * 6 entries)
//   - All 3 type-level callbacks
//   - The active event context
// Called during level transitions or system initialization.
// =============================================================================
void event_system_reset(void) {
    // Clear 32x6 dispatch matrix (32 rows, 6 entries each = 192 slots)
    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 6; col++) {
            g_event_dispatch_matrix[row * 6 + col] = 0;  // DAT_8053f4d4
        }
    }

    // Clear 3 type-level callbacks
    for (int i = 0; i < 3; i++) {
        g_event_type_handlers[i] = 0;  // DAT_8053f4c8
    }

    // Clear active context
    g_event_active_context = 0;  // DAT_80611c48
}
