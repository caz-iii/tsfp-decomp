// =============================================================================
// player_ai_list.cpp - AI Weapon Tracking Linked List & Ambient Sound System
// Address Range: 0x800b0840 - 0x800b0ac8
// Date: 2026-08-11
// Batch: 255
// =============================================================================
//
// Doubly-linked list operations for AI weapon/target tracking (20-slot pool),
// target table management (4-slot fixed array), and ambient sound start/stop.
//
// Linked List Layout (per list instance):
//   +0x000 .. +0x22F: 20 slots, 0x1C bytes each
//     Slot: [0] entity_id, [1] char_type, [2] ?, [3] char_def_ptr, [4] entity_ptr,
//           [5] prev, [6] next
//   +0x230: active list head
//   +0x234: free list head
//   +0x238: active count
//   +0x23C: max tracking distance (float)
//
// Target Table (DAT_805642a8, 4 slots x 0x10 bytes):
//   +0x00: sound sequence start
//   +0x04: sound sequence end
//   +0x08: handle/event ID
//   +0x0C: (used by sound system)
//
// =============================================================================

#include "common.h"
#include "player_ai.h"
#include "sound.h"

// External data
extern int DAT_806109c0;
extern int DAT_80611da0;       // Retarget timer (-666 sentinel)
extern u8  DAT_805642a8[];     // 4-slot target table base
extern u32 DAT_805642b0;       // Target table handle slot 0
extern u32 DAT_805642b4;       // Target table sentinel slot 0
extern u8  DAT_80403738[];     // Character definition table (0x54 per entry)

extern float FLOAT_80614904;
extern float FLOAT_806108ec;
extern float FLOAT_806149d8;
extern float FLOAT_806149dc;
extern float FLOAT_806149e8;
extern float FLOAT_806149ec;
extern double DOUBLE_806149e0;

// Forward declarations
extern int  FUN_803826f0(void);
extern void FUN_8038273c(void);
extern void sound_register_fade_out(double, double, int*, int*);

// =============================================================================
// Address: 0x800b0840 | player_ai_list_free_node
// Frees a node from the AI active list. Unlinks from prev/next chain, returns
// to free list sentinel. Decrements active count at +0x238.
// =============================================================================
void player_ai_list_free_node(int list_base, int node) {
    int old_free_head = *(int*)(list_base + 0x234);

    // Unlink from active chain
    if (*(int*)(node + 0x14) == 0) {
        // Node was head of active list
        *(int*)(list_base + 0x230) = *(int*)(node + 0x18);
    } else {
        // Update previous node's next pointer
        *(int*)(*(int*)(node + 0x14) + 0x18) = *(int*)(node + 0x18);
    }

    // Update next node's prev pointer
    if (*(int*)(node + 0x18) != 0) {
        *(int*)(*(int*)(node + 0x18) + 0x14) = *(int*)(node + 0x14);
    }

    // Push to free list head
    *(int*)(list_base + 0x234) = node;
    *(int*)(node + 0x14) = 0;
    *(int*)(node + 0x18) = old_free_head;
    *(int*)(old_free_head + 0x14) = node;

    // Decrement count
    *(int*)(list_base + 0x238) -= 1;
}

// =============================================================================
// Address: 0x800b08a8 | player_ai_list_insert_target
// Inserts target into AI tracking list. Allocates node, stores entity ID
// (from ai_data+0x0C), char type (+0x14), entity pointer, and character
// definition table pointer (DAT_80403738 + char_type * 0x54).
// =============================================================================
void player_ai_list_insert_target(int list_base, int entity_ptr) {
    int* node = (int*)player_ai_list_alloc_node(list_base);
    int ai_data = *(int*)(entity_ptr + 0x7C);

    node[4] = entity_ptr;                                   // Entity pointer
    node[0] = *(int*)(ai_data + 0x0C);                     // Entity ID
    node[1] = *(int*)(ai_data + 0x14);                     // Character type
    node[3] = (int)(DAT_80403738 + node[1] * 0x54);        // Character def table ptr
}

// =============================================================================
// Address: 0x800b0904 | player_ai_list_find_by_id
// Searches AI active list for node matching (entity_id, char_type). Walks
// active chain from head (+0x230) via next pointers at slot[6]. Returns
// matching node pointer or NULL.
// =============================================================================
int* player_ai_list_find_by_id(int list_base, int entity_id, int char_type) {
    int* node = *(int**)(list_base + 0x230);

    while (node != NULL) {
        if (node[0] == entity_id && node[1] == char_type) {
            return node;
        }
        node = (int*)node[6];  // next pointer
    }

    return NULL;
}

// =============================================================================
// Address: 0x800b0938 | player_ai_list_remove_by_id
// Combines find + free: searches for node by (entity_id, char_type), then
// frees it if found. Called from weapon drop code.
// =============================================================================
void player_ai_list_remove_by_id(int list_base, int entity_id, int char_type) {
    int* node = player_ai_list_find_by_id(list_base, entity_id, char_type);
    if (node != NULL) {
        player_ai_list_free_node(list_base, (int)node);
    }
}

// =============================================================================
// Address: 0x800b0974 | player_ai_list_distance_decay
// Decrements max tracking distance at +0x23C by frame-time-scaled rate
// (FLOAT_806149dc). Clamps to minimum (FLOAT_806149d8). Called from
// player_ai_reset_path_list each frame.
// =============================================================================
void player_ai_list_distance_decay(int list_base) {
    float min_dist = FLOAT_806149d8;

    if (min_dist < *(float*)(list_base + 0x23C)) {
        // Decay: subtract (rate * dt) from current distance
        float dt = (float)((double)(0x43300000 | (DAT_806109c0 ^ 0x80000000)) - DOUBLE_806149e0);
        float decay = FLOAT_806108ec * (FLOAT_806149dc / dt);

        *(float*)(list_base + 0x23C) -= decay;

        // Clamp to minimum
        if (*(float*)(list_base + 0x23C) < min_dist) {
            *(float*)(list_base + 0x23C) = min_dist;
        }
    }
}

// =============================================================================
// Address: 0x800b09d4 | player_ai_target_table_reset
// Resets 4-slot AI target tracking table (DAT_805642a8). Clears handles to 0,
// sets sentinels to -1 (0xFFFFFFFF). Forces retarget timer (DAT_80611da0) to
// -666 sentinel value.
// =============================================================================
void player_ai_target_table_reset(void) {
    for (int i = 0; i < 4; i++) {
        *(u32*)(DAT_805642a8 + 8 + i * 0x10) = 0;          // Clear handle
        *(u32*)(DAT_805642a8 + 12 + i * 0x10) = 0xFFFFFFFF; // Set sentinel
    }
    DAT_80611da0 = -666;  // Force retarget
}

// =============================================================================
// Address: 0x800b0a10 | player_ai_target_table_find_free
// Finds first free slot in 4-slot AI target table (handle == 0). Returns
// slot base pointer or NULL if all occupied.
// Table at DAT_805642a8, 0x10 bytes per slot, handle at offset +0x08.
// =============================================================================
void* player_ai_target_table_find_free(void) {
    for (int i = 0; i < 4; i++) {
        if (*(int*)(DAT_805642a8 + 8 + i * 0x10) == 0) {
            return (void*)(DAT_805642a8 + i * 0x10);
        }
    }
    return NULL;
}

// =============================================================================
// Address: 0x800b0a44 | player_ai_ambient_sound_stop
// Stops AI ambient sound: fades out via sound_register_fade_out with configured
// fade time, then clears handle at slot+8 and zeroes the sequence start at slot+0.
// =============================================================================
void player_ai_ambient_sound_stop(int* slot_ptr) {
    // Fade out: params are fade time and target volume
    sound_register_fade_out(
        (double)FLOAT_806149e8,   // Fade duration
        (double)FLOAT_806149ec,   // Target volume (0)
        (int*)(*slot_ptr + 0x0C), // Sound handle reference
        NULL
    );

    // Clear slot
    *(int*)(*slot_ptr + 8) = 0;  // Clear event ID / handle
    *slot_ptr = 0;               // Clear sequence start
}

// =============================================================================
// Address: 0x800b0a94 | player_ai_stop_ambient_sound
// Wrapper: stops AI ambient sound at ai_data+0xE98 if active (non-zero).
// Called during entity destruction/zone change.
// =============================================================================
void player_ai_stop_ambient_sound(int entity_ptr) {
    int ai_data = *(int*)(entity_ptr + 0x7C);

    if (*(int*)(ai_data + 0xE98) != 0) {
        player_ai_ambient_sound_stop((int*)(ai_data + 0xE98));
    }
}

// =============================================================================
// Address: 0x800b0ac8 | player_ai_ambient_sound_start
// Starts AI ambient sound sequence. Binary searches a 0x7A-entry sound
// definition table (at 0x80403F88, 0x0C per entry: [event_id, seq_start, seq_end])
// by event ID. On match, allocates a target table slot and stores the sound
// sequence parameters at ai_data+0xE98.
// =============================================================================
void player_ai_ambient_sound_start(void) {
    int params = FUN_803826f0();  // Returns packed: low=event_id, high=entity_ptr
    int event_id = (int)params;
    int entity_ptr = (int)((u64)params >> 32);

    // Binary search sound def table (0x7A entries, 0x0C stride)
    int lo = 0;
    int hi = 0x7A;
    int* match = NULL;

    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        int* entry = (int*)(0x80403F88 + mid * 0x0C);

        if (event_id == entry[0]) {
            match = entry;
            break;
        } else if (entry[0] < event_id) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    if (match != NULL) {
        int ai_data = *(int*)(entity_ptr + 0x7C);

        // Allocate target table slot if not already assigned
        if (*(int*)(ai_data + 0xE98) == 0) {
            void* slot = player_ai_target_table_find_free();
            *(void**)(ai_data + 0xE98) = slot;
            if (slot == NULL) goto end;
        }

        // Store sound sequence params in slot
        int* slot = *(int**)(ai_data + 0xE98);
        slot[2] = event_id;     // Handle / event ID at +0x08
        slot[0] = match[1];     // Sequence start
        slot[1] = match[2];     // Sequence end
    }

end:
    FUN_8038273c();
}
