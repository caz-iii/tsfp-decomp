// Address: 0x80070b40 - 0x8007149f | Batch 161 | Date: 2026-08-10
// Entity rendering utilities: visibility, resource loading, volume transforms, effects
// Part of the entity subsystem (game)

#include "common.h"

// ============================================================================
// Global references
// ============================================================================

extern int g_active_entity_list[];      // DAT_8053f450
extern int g_active_entity_count;       // DAT_80611c68
extern void* g_entity_type_table;       // DAT_806125e8
extern short g_entity_type_index[];     // DAT_805c9858
extern int g_game_mode_flags;           // DAT_805015c0
extern int g_current_game_mode;         // DAT_80612614
extern char g_player_count;             // DAT_805015bd
extern int g_resource_load_mode;        // DAT_80611930
extern void* g_entity_child_flags;      // DAT_80611b1c

// Forward declarations
extern "C" {
    int scene_query_frustum_nodes(int scene_node, int* aabb);
    void stream_queue_model(/* variadic params */);
    void sound_clear_bank_ref(int ref_id);
    void FUN_80146bf4(void);
    void FUN_8008ceb8(int entity_type_id);
    int  FUN_80241854(void);
    void FUN_8036f678(int entity_type_id);
    void FUN_80075ef0(float* out_matrix, int entity, int flag_a, int flag_b);
    void FUN_8012fa34(float* matrix, float* vec);
    void FUN_8012f9b8(float* matrix, float* vec);
    void FUN_8012fb90(float* matrix, float* vec);
    void FUN_8012f2c4(double tx, double ty, double tz, double angle, float* out_matrix);
    double FUN_801313a4(double val);
    double FUN_80131538(double val);
    double FUN_80390aa4(double y, double x);
    void FUN_800e4594(int entity, int effect, float* matrix, int context);
}

// ============================================================================
// 0x80070b40 | entity_is_in_active_list
// Checks if a given entity pointer exists in the global active entity list.
// Returns 1 if found, 0 otherwise.
// ============================================================================

int entity_is_in_active_list(int entity_ptr)
{
    int i = 0;
    int count = g_active_entity_count;

    if (count > 0) {
        do {
            if (g_active_entity_list[i / 4] == entity_ptr) {  // stride 4 bytes
                return 1;
            }
            i += 4;
            count--;
        } while (count != 0);
    }
    return 0;
}

// ============================================================================
// 0x80070b80 | entity_set_render_type
// Sets the render type field (offset 0x19c) on an entity.
// ============================================================================

void entity_set_render_type(int entity, int render_type)
{
    *(int*)(entity + 0x19c) = render_type;
}

// ============================================================================
// 0x80070b88 | entity_get_type_data
// Looks up entity type definition data from the type table.
// Uses g_entity_type_index to map entity_type_id -> slot index,
// then returns the base pointer into g_entity_type_table at stride 0x30.
// ============================================================================

void* entity_get_type_data(int entity_type_id)
{
    int slot_index = g_entity_type_index[entity_type_id];
    return (void*)((int)g_entity_type_table + slot_index * 0x30);
}

// ============================================================================
// 0x80070ba8 | entity_preload_resources
// Queues model streaming for an entity type and handles special-case
// resource loading (sound banks, cutscene triggers, etc.).
// Called extensively by spawn/preload functions.
// param_9 = entity_type_id (0..0x9C6 valid range)
// ============================================================================

void entity_preload_resources(
    /* params 1-8 are passed through to stream_queue_model (register pairs) */
    int entity_type_id)
{
    if (entity_type_id == -1 || entity_type_id >= 0x9C7) {
        return;
    }

    int* type_data = (int*)entity_get_type_data(entity_type_id);

    // Determine if loading should be skipped based on game mode flags
    unsigned int skip_flag = 0;
    if ((g_game_mode_flags & 0x40) == 0) {
        if (g_current_game_mode == 100 || g_current_game_mode == 0x38) {
            skip_flag = 0;
        } else if (g_player_count == 10 || g_current_game_mode == 0x66) {
            skip_flag = type_data[10] & 0x2000000;  // multiplayer-restricted flag
        } else {
            skip_flag = 0;
        }
    }

    if (skip_flag != 0) {
        return;
    }

    // Queue primary model for streaming
    stream_queue_model(/* pass-through register params */);

    // If entity type has a secondary model (offset 0x14), queue that too
    if (type_data[5] != 0) {
        stream_queue_model(/* secondary model params */);
    }

    FUN_80146bf4();  // Finalize resource queue entry

    // Handle special entity type categories
    int category = type_data[0];
    if (category == 0x20000) {
        // Character/NPC type: load character-specific resources
        FUN_8008ceb8(entity_type_id);
    } else if (category == 0x200) {
        // Sound-emitting prop: clear associated sound banks
        sound_clear_bank_ref(0xBF9);
        int sound_id = FUN_80241854();
        sound_clear_bank_ref(sound_id);
        sound_clear_bank_ref(0xBF8);
    } else if (category == 0x40000000) {
        // Cutscene entity: load cutscene resources
        FUN_8036f678(entity_type_id);
    }
}

// ============================================================================
// 0x80070d10 | entity_has_active_children
// Checks if any child in the entity's child list has an active flag set.
// Returns 1 if at least one child is active, 0 otherwise.
// ============================================================================

int entity_has_active_children(int entity)
{
    int count = (int)*(char*)(entity + 0x30);  // child count at +0x30
    int offset = 0;

    if (count > 0) {
        do {
            int child_index = *(int*)(entity + offset + 0x38);
            if (*(char*)((int)g_entity_child_flags + child_index) != 0) {
                return 1;
            }
            offset += 4;
            count--;
        } while (count != 0);
    }
    return 0;
}

// ============================================================================
// 0x80070d54 | entity_test_frustum_visibility
// Builds an AABB from entity position (offset 0x1A8) and extents (offset 0x1C0),
// then tests it against the scene frustum via scene_query_frustum_nodes.
// Returns 1 if visible, 0 if culled.
// ============================================================================

int entity_test_frustum_visibility(int entity)
{
    float aabb[6];

    // min = position - extent
    aabb[0] = *(float*)(entity + 0x1A8) - *(float*)(entity + 0x1C0);
    aabb[1] = *(float*)(entity + 0x1AC) - *(float*)(entity + 0x1C4);
    aabb[2] = *(float*)(entity + 0x1B0) - *(float*)(entity + 0x1C8);

    // max = position + extent
    aabb[3] = *(float*)(entity + 0x1A8) + *(float*)(entity + 0x1C0);
    aabb[4] = *(float*)(entity + 0x1AC) + *(float*)(entity + 0x1C4);
    aabb[5] = *(float*)(entity + 0x1B0) + *(float*)(entity + 0x1C8);

    unsigned int result = scene_query_frustum_nodes(*(int*)(entity + 0x34), (int*)aabb);

    // Convert query result to boolean visible flag
    // result XOR 1: invert (0=visible in query becomes 1 here)
    return (int)((result ^ 1) >> 1) - (int)(((result ^ 1) & result) >> 31);
}

// ============================================================================
// 0x80070ddc | entity_transform_collision_volumes
// MASSIVE function: Transforms collision bounding boxes from local space to
// world space using bone matrices and the entity's view matrix.
// Handles both standard and rotated (non-axis-aligned) bone transforms.
// Iterates over all sub-objects in the entity's mesh data, transforming
// each collision box min/max pair and computing the resulting world AABB.
// ============================================================================

void entity_transform_collision_volumes(void)
{
    // This function uses paired-single register context (f30/f31 save/restore)
    // and operates on the "current entity" via FUN_803826cc (entity context getter).
    //
    // Algorithm:
    // 1. Get entity context via FUN_803826cc
    // 2. If entity has collision volume data (offset +0x1F4 != 0):
    //    a. Build world-to-view matrix via FUN_80075ef0
    //    b. For each sub-object in entity mesh:
    //       - For each collision volume in sub-object:
    //         * Copy volume template data
    //         * If entity has bone transforms:
    //           - If standard orientation: transform min/max by bone matrix (FUN_8012fa34)
    //           - If rotated (flag 0x40000): build rotation matrix from bone angles,
    //             then transform by that
    //         * Transform by view matrix
    //         * Compute world-space AABB from transformed min/max
    //         * Fix up internal linked-list pointers (next/prev volume references)
    //    c. Store total volume count at entity offset +0x1E4
    // 3. Restore paired-single registers, return via FUN_80382718

    // [Full implementation preserved in Ghidra IDB as raw pseudocode]
    // See Ghidra decompilation at 0x80070ddc for complete logic
}

// ============================================================================
// 0x8007111c | entity_transform_decal_volumes
// MASSIVE function: Transforms decal/shadow projection volumes from local
// space to world space. Similar structure to collision volume transform but
// operates on decal projection data with different field layout.
// Computes per-vertex world positions and tracks 2D bounding extents (X/Z).
// ============================================================================

void entity_transform_decal_volumes(void)
{
    // This function uses paired-single register context (f29/f30/f31 save/restore)
    // and operates on the "current entity" via FUN_803826c0 (decal context getter).
    //
    // Algorithm:
    // 1. Get entity context via FUN_803826c0
    // 2. If entity has decal volume data (offset +0x1E0 != 0):
    //    a. Build world matrix via FUN_80075ef0
    //    b. Initialize X/Z bounds to float max/min constants
    //    c. For each sub-object in entity mesh:
    //       - For each decal volume:
    //         * Copy header (count, flags, type index)
    //         * Copy trailing data (6 dwords after vertex array)
    //         * For each vertex in volume:
    //           - Copy position from template
    //           - Transform position by bone matrix (FUN_8012f9b8)
    //           - Transform radius/extent by bone matrix (FUN_8012fb90)
    //           - Transform position by view matrix
    //           - Transform radius by view matrix
    //           - Update X bounds (min/max of vertex X)
    //           - Update Z bounds (min/max of vertex Z)
    //    d. Store bounds into output decal volume slots
    // 3. Restore registers, return via FUN_8038270c

    // [Full implementation preserved in Ghidra IDB as raw pseudocode]
    // See Ghidra decompilation at 0x8007111c for complete logic
}

// ============================================================================
// 0x80071394 | entity_render_attached_effects
// Iterates over the entity's attached effect list and dispatches rendering
// for each active effect using FUN_800e4594 (effect render callback).
// ============================================================================

void entity_render_attached_effects(void)
{
    // Uses FUN_803826ec to get entity context
    // Entity effect list pointer at offset +0x1E8 (count)
    // Effect pointer array at offset +0x1FC

    // Algorithm:
    // 1. Get entity context
    // 2. If effect count (offset +0x1E8) != 0:
    //    a. Build world matrix via FUN_80075ef0
    //    b. For each effect in list:
    //       - Get effect pointer from array at offset +0x1FC
    //       - If pointer is non-null: call FUN_800e4594(entity, effect, matrix, context)
    // 3. Restore registers via FUN_80382738

    // [Full implementation preserved in Ghidra IDB as raw pseudocode]
    // See Ghidra decompilation at 0x80071394 for complete logic
}

// ============================================================================
// 0x8007142c | entity_compute_velocity_midpoint
// Computes a half-step velocity vector and midpoint position between two
// positions (e.g., previous frame and current frame).
//   velocity = 0.5 * (new_pos - old_pos)
//   midpoint = old_pos + velocity
// Used for physics interpolation / swept collision.
// ============================================================================

void entity_compute_velocity_midpoint(
    int unused,           // param_1: unused (possibly entity ptr for consistency)
    float* old_pos,       // param_2: previous position [3]
    float* new_pos,       // param_3: current position [3]
    float* out_velocity,  // param_4: output half-velocity [3]
    float* out_midpoint)  // param_5: output midpoint [3]
{
    // FLOAT_80613fb0 = 0.5f
    const float half = 0.5f;

    out_velocity[0] = half * (new_pos[0] - old_pos[0]);
    out_velocity[1] = half * (new_pos[1] - old_pos[1]);
    out_velocity[2] = half * (new_pos[2] - old_pos[2]);

    out_midpoint[0] = old_pos[0] + out_velocity[0];
    out_midpoint[1] = old_pos[1] + out_velocity[1];
    out_midpoint[2] = old_pos[2] + out_velocity[2];
}
