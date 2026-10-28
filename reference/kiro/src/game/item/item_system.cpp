// Address: 0x80087f24 - 0x80087f90 | Batch 183 | Date: 2026-08-11
// Item system: event registration and memory allocation

#include "item.h"
#include "event.h"
#include "mem_alloc.h"

// ============================================================================
// Globals
// ============================================================================
extern int DAT_80611cb0;  // Item data pool A (0x3B80 bytes = ~100 items * 0x60)
extern int DAT_80611cb4;  // Item data pool B (0x2600 bytes)

// Forward declarations for item event handlers
extern void FUN_8024083c(void);  // Item spawn handler (sub-type 0)
extern void FUN_802407f4(void);  // Item activate handler (sub-type 1)
extern void FUN_8023d3a8(void);  // Item pickup handler (sub-type 2)
extern void FUN_8023d2ec(void);  // Item destroy handler (sub-type 4)

// ============================================================================
// Address: 0x80087f24
// item_register_event_handlers - Registers item system event handlers
// Event type 0x200 (items) with 4 sub-type handlers:
//   0: spawn, 1: activate, 2: pickup, 4: destroy
// Called by: game_tick_update
// ============================================================================
void item_register_event_handlers(void) {
    event_set_handler(0x200, 0, FUN_8024083c);  // Item spawn
    event_set_handler(0x200, 1, FUN_802407f4);  // Item activate
    event_set_handler(0x200, 2, FUN_8023d3a8);  // Item pickup
    event_set_handler(0x200, 4, FUN_8023d2ec);  // Item destroy
}

// ============================================================================
// Address: 0x80087f90
// item_system_alloc - Allocates memory pools for item system
// Pool A: 0x3B80 bytes (15,232) - main item data
// Pool B: 0x2600 bytes (9,728) - secondary item state/render data
// Then calls FUN_80087fd0 (item_system_init) to initialize pools.
// Called by: entity_system_alloc
// ============================================================================
void item_system_alloc(void) {
    DAT_80611cb0 = mem_alloc(0x3B80, 1);
    DAT_80611cb4 = mem_alloc(0x2600, 1);
    FUN_80087fd0();  // item_system_init
}


// ============================================================================
// Address: 0x80087fd0
// item_system_init - Initializes item data pools
// Clears both memory pools, initializes two block allocators:
//   Pool A: 0x20 entries of 0x1DC bytes each (item state data)
//   Pool B: 0x20 entries of 0x130 bytes each (item render data)
// Called by: entity_system_reset, item_system_alloc
// ============================================================================
void item_system_init(void) {
    DAT_80611cb8 = 0;  // Active item count

    // Clear both pools
    memset_wrapper((int)DAT_80611cb0, 0, 0x3B80);
    memset_wrapper((int)DAT_80611cb4, 0, 0x2600);

    // Initialize pool allocator headers
    FUN_80133b50((int *)&DAT_8055f890);  // Pool A header init
    FUN_80133b50((int *)&DAT_8055f8a0);  // Pool B header init

    // Configure pools: stride, base pointer, max entries
    FUN_80133b18((int *)&DAT_8055f890, 0x1DC, DAT_80611cb0, 0x20);  // Pool A: 32 x 476 bytes
    FUN_80133b18((int *)&DAT_8055f8a0, 0x130, DAT_80611cb4, 0x20);  // Pool B: 32 x 304 bytes
}

// ============================================================================
// Address: 0x8008805c
// item_pool_a_alloc - Allocates a slot from item pool A (state data)
// Returns pointer to 0x1DC-byte item state block.
// Called by: item spawn event handler (FUN_8024083c)
// ============================================================================
void item_pool_a_alloc(void) {
    FUN_80133b88((int)&DAT_8055f890);  // -0x7faa0770 = &DAT_8055f890
}

// ============================================================================
// Address: 0x80088084
// item_pool_a_free - Returns a slot to item pool A
// Called by: item activate event handler (FUN_802407f4)
// ============================================================================
void item_pool_a_free(int *pSlot) {
    FUN_80133bb0((int)&DAT_8055f890, pSlot);
}

// ============================================================================
// Address: 0x800880b0
// item_pool_b_alloc - Allocates a slot from item pool B (render data)
// Returns pointer to 0x130-byte item render block.
// Called by: item spawn event handler (FUN_8024083c)
// ============================================================================
void item_pool_b_alloc(void) {
    FUN_80133b88((int)&DAT_8055f8a0);  // -0x7faa0760 = &DAT_8055f8a0
}

// ============================================================================
// Address: 0x800880d8
// item_pool_b_free - Returns a slot to item pool B
// Called by: item activate event handler (FUN_802407f4)
// ============================================================================
void item_pool_b_free(int *pSlot) {
    FUN_80133bb0((int)&DAT_8055f8a0, pSlot);
}

// ============================================================================
// Address: 0x80088104
// item_preload_resources - Preloads item model, animation, and sounds
// Loads the entity model (param_9+0xc), optional animation (param_9+0x14),
// and if flag 0x40 set: clears two sound bank references for the item.
// Called by: level_preload_all_resources, FUN_8015d1dc
// ============================================================================
void item_preload_resources(/* coroutine params */, int pItemDef) {
    // Preload entity model
    entity_preload_resources(*(int *)(pItemDef + 0xc));

    // Preload optional animation
    if (*(int *)(pItemDef + 0x14) != -1) {
        FUN_801a80f4(*(int *)(pItemDef + 0x14));  // Animation preload
    }

    // If has sound flag (0x40): preload sound banks
    if ((*(unsigned int *)(pItemDef + 0x10) & 0x40) != 0) {
        int soundId0 = FUN_80088e8c(*(int *)(pItemDef + 0xc), 0);
        sound_clear_bank_ref(soundId0);

        int soundId1 = FUN_80088e8c(*(int *)(pItemDef + 0xc), 1);
        sound_clear_bank_ref(soundId1);
    }
}

// ============================================================================
// Address: 0x80088170
// item_spawn_entity - Spawns an item entity from definition
// Creates entity with type from param_9[3], sets initial rotation to
// FLOAT_80614358 for X and Z (rest pose), Y from param_1.
// Stores item definition ID at entity+0x60.
// Calls FUN_800e4490 for post-spawn item setup.
//
// Called by: spawn_create_particle_entity, level spawners
// Returns: spawned entity pointer
// ============================================================================
int item_spawn_entity(double yRotation, /* params */, int *pItemDef, int param_10) {
    float orient[3];
    orient[1] = (float)yRotation;       // Y rotation from caller
    orient[0] = FLOAT_80614358;          // Default X rotation
    orient[2] = FLOAT_80614358;          // Default Z rotation

    int pEntity = entity_spawn_by_type(
        pItemDef[3],        // Entity type ID
        param_10,           // Activation param
        orient,             // Orientation
        pItemDef,           // Definition data
        0);                 // No parent

    // Store item definition ID on entity
    *(int *)(pEntity + 0x60) = *pItemDef;

    // Post-spawn item setup (animation, collision, etc.)
    FUN_800e4490();

    return pEntity;
}

// ============================================================================
// Address: 0x80088230
// item_def_find_by_id - Searches item definition table for matching ID
// Scans 16 entries (stride 0x2C) at DAT_804001a4, comparing first field.
// Returns pointer to matching entry or NULL.
// Called by: FUN_8008b288 (item collection logic)
// ============================================================================
int *item_def_find_by_id(int itemId) {
    int *result = NULL;
    int offset = 0;
    int count = 15;  // Actually 16 iterations (0..15)

    do {
        if (itemId == *(int *)((int)&DAT_804001a4 + offset)) {
            result = (int *)((int)&DAT_804001a4 + offset);
        }
        offset += 0x2C;
        count--;
    } while (count != 0);

    return result;
}

// ============================================================================
// Address: 0x8008826c
// item_get_attach_position - Computes world position from bone+vertex ref
// Uses packed param_3: upper 16 bits = bone group index, lower 16 = vertex offset.
// Looks up model bone data, computes offset between vertex and root bone,
// adds to base position (from FUN_8008874c).
// Uses coroutine-style iterator (FUN_803826f0 / FUN_8038273c).
//
// Called by: (registered as callback - no direct callers in scan)
// ============================================================================
void item_get_attach_position(int param_1, int param_2, unsigned int packedRef, float *pOutPos) {
    int pEntity = FUN_803826f0();

    // Get model data
    int *pModelData = *(int **)(*(int *)(pEntity + 4) + 4);

    // Root bone position (bone 0)
    float *pRootBone = (float *)pModelData[*pModelData * -0x30 + 0x4B];

    // Target vertex position from packed reference
    int boneGroupIndex = (int)packedRef >> 16;
    int vertexOffset = packedRef & 0xFFFF;
    float *pTargetVertex = (float *)(pModelData[*pModelData * -0x30 + boneGroupIndex * 0x30 + 0x1B]
                           + vertexOffset * 0x18);

    // Get base position (FUN_8008874c fills local_28/24/20)
    float basePos[3];
    FUN_8008874c(/* fills basePos */);

    // Output = base + (target - root)
    pOutPos[0] = basePos[0] + (pTargetVertex[0] - pRootBone[0]);
    pOutPos[1] = basePos[1] + (pTargetVertex[1] - pRootBone[1]);
    pOutPos[2] = basePos[2] + (pTargetVertex[2] - pRootBone[2]);

    FUN_8038273c();  // End iterator
}

// ============================================================================
// Address: 0x80088320
// item_can_pickup - Checks if an item entity is eligible for pickup
// Conditions for pickup (returns 1):
//   - Either ammo count A (iVar1+0xf0) <= 9 OR ammo count B (iVar1+0xf4) <= 9
//   - AND distance metric (data+0x1c4) <= FLOAT_80614360
// Returns 0 if item is full (both ammo >= 10) or too far away.
//
// Called by: item collection logic, weapon ammo checks
// ============================================================================
int item_can_pickup(int pEntity) {
    int pItemData = *(int *)(*(int *)(pEntity + 0x7c) + 0x6c);

    // Check if both ammo slots are full (>= 10)
    if (*(int *)(pItemData + 0xf0) > 9 && *(int *)(pItemData + 0xf4) > 9) {
        return 0;
    }

    // Check distance threshold
    if (*(float *)(*(int *)(pEntity + 0x7c) + 0x1c4) > FLOAT_80614360) {
        return 0;
    }

    return 1;
}
