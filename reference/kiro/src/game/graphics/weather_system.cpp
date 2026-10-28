// =============================================================================
// weather_system.cpp - Weather strip system (rain, snow, environmental effects)
// Address range: 0x800ff9a8 - 0x800ffa04
// Date: 2026-08-25
// =============================================================================

#include "common.h"
#include "sound.h"

// Global state
// DAT_80575540 = weather strip position buffer (0x1B0 bytes)
// DAT_805756f0 = weather strip render buffer (0x5F0 bytes)
extern int DAT_80611f58;  // Weather type/config (from DAT_80612020)
extern int DAT_80611f5c;  // Active strip count
extern int DAT_80611f60;  // System enabled flag
extern int DAT_80612020;  // Default weather config

extern void memset_wrapper(int dst, int val, int size);
extern void FUN_801015c0(u32* param);
extern void weather_strip_spawn(void);

// =============================================================================
// Address: 0x800ff9a8 | Name: weather_system_reset
//
// Reset the weather strip system. Clears position and render buffers,
// sets enabled flag, resets strip count, loads default config.
// Called by: entity_system_reset
// =============================================================================
void weather_system_reset(void) {
    memset_wrapper(0x80575540, 0, 0x1B0);  // Clear position buffer (8 strips * 0x36 bytes)
    memset_wrapper(0x805756F0, 0, 0x5F0);  // Clear render buffer
    DAT_80611f60 = 1;   // Enable weather system
    DAT_80611f5c = 0;   // No active strips
    DAT_80611f58 = DAT_80612020;  // Load default weather type
}

// =============================================================================
// Address: 0x800ffa04 | Name: weather_start_strip
//
// Start a weather strip effect. Initializes sound, spawns the visual strip,
// and clears the strip state slot.
// Called by: FUN_800ffa48, FUN_800ffb10 (weather triggers)
// =============================================================================
void weather_start_strip(u32* pStripSlot) {
    FUN_801015c0((u32*)pStripSlot[1]);  // Initialize sound for strip
    weather_strip_spawn();               // Spawn visual effect
    *pStripSlot = 0;                     // Clear slot (mark as consumed)
}

// =============================================================================
// Address: 0x800ffa48 | Name: weather_find_or_alloc_strip
//
// Search 4 weather strip slots for a matching entity ID. If not found,
// evicts the current slot (calls weather_start_strip to flush), allocates it,
// and advances the circular index.
// Returns pointer to allocated slot, or NULL if entity already has a strip.
// Called by: FUN_80101218 (weather trigger)
// =============================================================================
int* weather_find_or_alloc_strip(int entityId) {
    int prevIdx = DAT_80611f5c;
    int offset = 0;

    // Search existing slots for duplicate
    for (int i = 0; i < 4; i++) {
        if (*(int*)((int)&DAT_80575540 + offset) != 0 &&
            *(int*)((int)&DAT_80575544 + offset) == entityId) {
            return NULL; // Already exists
        }
        offset += 0x6C;
    }

    // Not found - allocate current slot
    int* pSlot = (int*)(&DAT_80575540 + DAT_80611f5c * 0x1B);

    // Evict if occupied
    if (*pSlot != 0) {
        weather_start_strip(pSlot);
    }

    // Initialize new slot
    *pSlot = 1;
    (&DAT_80575544)[prevIdx * 0x1B] = entityId;

    // Advance circular index (mod 4)
    DAT_80611f5c = (DAT_80611f5c + 1) % 4;

    return pSlot;
}

// =============================================================================
// Address: 0x800ffb10 | Name: weather_check_config_change
//
// If weather config has changed (DAT_80611f58 != DAT_80612020), find an active
// strip starting from current index and restart it. Circular search over 4 slots.
// Called by: weather_tick
// =============================================================================
void weather_check_config_change(void) {
    if (DAT_80611f58 == DAT_80612020) {
        return; // No change
    }

    int idx = DAT_80611f5c;
    for (int i = 0; i < 4; i++) {
        if ((&DAT_80575540)[idx * 0x1B] != 0) {
            weather_start_strip((u32*)(&DAT_80575540 + idx * 0x1B));
            DAT_80611f58 = DAT_80612020;
            return;
        }
        idx++;
        if (idx > 3) idx = 0;
    }
}

// =============================================================================
// Address: 0x800ffbe8 | Name: weather_tick
//
// Per-frame weather system update. Computes delta time, calls config change
// check, then advances time accumulator for all 20 weather render entries
// (0x4C bytes each in render buffer at DAT_805756f0).
// Called by: entity_update_dispatch
// =============================================================================
void weather_tick(void) {
    // Compute delta time
    double dt = (double)(FLOAT_806108ec *
        (FLOAT_806155f4 / (float)((double)(DAT_806109c0 ^ 0x80000000) - DOUBLE_806155f8)));

    // Check for weather config changes
    weather_check_config_change();

    // Tick all 20 render entries - accumulate time
    int offset = 0;
    for (int i = 0; i < 20; i++) {
        *(float*)((int)&DAT_805756f8 + offset) =
            *(float*)((int)&DAT_805756f8 + offset) + (float)dt;
        offset += 0x4C;
    }
}

// =============================================================================
// Address: 0x800ffc74 | Name: weather_strip_alloc_render_slot
//
// MASSIVE function. Allocates a render slot from the 20-slot pool for a
// weather strip collision volume. Determines strip orientation (0-3) via RNG,
// validates against height thresholds and existing assignments. Computes UV
// coordinates and quad corner geometry based on orientation and position within
// the weather strip definition texture.
//
// Slot layout (0x4C bytes):
//   +0x00: collision volume pointer
//   +0x04: active flag
//   +0x08: age/time accumulator
//   +0x0C-0x14: position XY + orientation
//   +0x18: texture def pointer (&DAT_8040c308)
//   +0x1C-0x20: UV start offset
//   +0x24-0x28: UV size
//   +0x2C-0x48: quad corner UVs (4 corners * 2 floats)
//
// Called by: FUN_801018a4, FUN_80101cb4 (weather strip spawn triggers)
// =============================================================================
void weather_strip_alloc_render_slot(int* param_3) {
    FUN_803826e0();

    // ... validation: check flags 0x100, 0x800, and slot assignment bytes ...
    // ... height threshold check against param_3[1] ...
    // ... RNG orientation selection (0-3) ...

    // Find free slot or evict oldest
    int slotIdx = -1;
    float oldestAge = FLOAT_806155f0;
    int* pSlot = NULL;
    int bFreeFound = 0;

    int offset = 0;
    for (int i = 0; i < 20; i++) {
        int* pEntry = (int*)((int)&DAT_805756f0 + offset);
        if (*(int*)((int)&DAT_805756f4 + offset) == 0) {
            // Free slot found
            pSlot = pEntry;
            bFreeFound = 1;
            slotIdx = i;
            break;
        }
        if (oldestAge <= *(float*)((int)&DAT_805756f8 + offset)) {
            slotIdx = i;
            oldestAge = *(float*)((int)&DAT_805756f8 + offset);
        }
        offset += 0x4C;
    }

    if (!bFreeFound) {
        // Evict oldest
        pSlot = (int*)(&DAT_805756f0 + slotIdx * 0x13);
        // Stop sound on evicted slot
        if ((*(u16*)(/* extraout_r4 */ +0x28) & 0x40C) == 0) {
            FUN_801015c0((u32*)*pSlot);
        } else {
            FUN_8010156c((u32*)*pSlot);
        }
    }

    // Assign slot
    // *(char*)(volume + slotByteOffset) = slotIdx;
    pSlot[1] = 1;               // Mark active
    pSlot[2] = (int)FLOAT_806155f0; // Reset age to 1.0
    *pSlot = /* extraout_r4 */0; // Link to collision volume
    pSlot[6] = (int)&DAT_8040c308; // Texture definition

    // Store position and orientation
    pSlot[3] = param_3[0];      // Position X
    pSlot[4] = param_3[1];      // Position Y
    pSlot[5] = /* orientation */0;

    // Flip UV based on orientation (1,3 flip X; 2,3 flip Y)
    // Compute UV start/size from position within texture def
    float uvX = FLOAT_80615608 * ((float)pSlot[3] / *(float*)(pSlot[6] + 4) - FLOAT_806155f4);
    float uvY = FLOAT_80615608 * ((float)pSlot[4] / *(float*)(pSlot[6] + 8) - FLOAT_806155f4);

    // Clamp UV offsets
    float clampedX = (uvX > FLOAT_806155f0) ? FLOAT_806155f0 : uvX;
    pSlot[7] = (int)(-clampedX);
    float clampedY = (uvY > FLOAT_806155f0) ? FLOAT_806155f0 : uvY;
    pSlot[8] = (int)(-clampedY);

    // Compute quad corner UVs (4 corners)
    float endX = (uvX < FLOAT_806155f0) ? FLOAT_806155f0 : uvX;
    float endY = (uvY < FLOAT_806155f0) ? FLOAT_806155f0 : uvY;
    pSlot[9] = (int)(endX - (float)pSlot[7]);   // Width
    pSlot[10] = (int)(endY - (float)pSlot[8]);  // Height
    pSlot[0xB] = pSlot[7];   // Corner 0: startX
    pSlot[0xC] = pSlot[8];   // Corner 0: startY
    pSlot[0xD] = (int)endX;  // Corner 1: endX
    pSlot[0xE] = pSlot[8];   // Corner 1: startY
    pSlot[0xF] = (int)endX;  // Corner 2: endX
    pSlot[0x10] = (int)endY; // Corner 2: endY
    pSlot[0x11] = pSlot[7];  // Corner 3: startX
    pSlot[0x12] = (int)endY; // Corner 3: endY

    FUN_8038272c();
}
