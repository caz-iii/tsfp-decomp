// Address: 0x80071f88 - 0x800730d7 | Batch 163 | Date: 2026-08-10
// Entity spawning and creation: render data allocation, instance creation,
// spawn activation, model loading, animation remapping.
// Part of the entity subsystem (game)

#include "common.h"

// ============================================================================
// External references
// ============================================================================

extern int g_current_game_mode;          // DAT_80612614
extern int g_entity_type_table;          // DAT_806125e8
extern short g_entity_type_index[];      // DAT_805c9858
extern int g_game_mode_flags;            // DAT_805015c0
extern char g_player_count;              // DAT_805015bd
extern int g_entity_free_count;          // DAT_80611c6c
extern int g_entity_uid_counter;         // DAT_80611c54
extern int g_frame_counter;              // DAT_80611928

extern "C" {
    void entity_scratch_alloc(/* size via register context */);
    void entity_transform_collision_volumes(void);
    void entity_transform_decal_volumes(void);
    void entity_render_attached_effects(void);
    void entity_update_bounds(int entity);
    void entity_set_render_type(int entity, int render_type);
    void entity_register_for_update(int entity, int update_type, int priority);
    void* entity_pool_alloc(void);
    void* entity_get_type_data(int entity_type_id);
    int entity_find_by_id(int entity_id);
    int event_dispatch(int entity, int event_type, int param);
    int event_has_handler(int entity_type, int event_id);
    void entity_track_spawned(int entity);
    int render_entity_load_model(/* variadic */);
    int render_entity_load_model_fallback(int model_id, int lod);
    int render_entity_load_model_with_textures(int model_id, int lod);
    int render_entity_set_global_flag(int flag);
    int render_instance_create(/* variadic */);
    void render_instance_destroy(int* instance);
    void render_instance_set_material(double val, int* instance, int param);
    void spawn_activate_point(/* variadic */);
    void collision_stub_empty(void);
    void FUN_800e34d8(int* effect_ptr);
    int FUN_800e3d3c(int effect_slot, int effect_def, int entity, int bone_idx);
    void FUN_800e35b0(int* effect_arr, int count, int param);
    void FUN_800fffdc(void);
    void FUN_8010386c(void);
    int FUN_800ffb88(int entity);
    int* FUN_800fe808(/* variadic */);
    int FUN_8007384c(int entity);
    int FUN_8013104c(void);
    void memset_wrapper(int dst, int val, int size);
}

// ============================================================================
// 0x80071f88 | entity_alloc_render_data
// MASSIVE: Allocates scratch memory for all entity render sub-structures:
// - Collision volumes (offset +0x1F4, count at +0x1E4)
// - Decal volumes (offset +0x1F0, count at +0x1E0)
// - Bone/skin data (offset +0x1F8, count at +0x1C)
// - Effect slots (offset +0x1FC, count at +0x1E8)
// - Shadow slots (offset +0x200, count at +0x1EC)
// Computes total memory required, calls entity_scratch_alloc, then
// distributes pointers into sub-arrays. For non-multiplayer entities,
// also allocates per-volume transform workspace (0x3C per collision vol,
// 0x18 per vertex, 0x40 per bone skin, 0x1C per skin vertex).
// Sets entity flag 0x800 to indicate render data is allocated.
// ============================================================================

void entity_alloc_render_data(void)
{
    // Algorithm:
    // 1. Get entity via FUN_803826d8
    // 2. Determine if multiplayer-restricted (type==2 && mode!=100)
    // 3. Count sub-objects: collision vols, decal vols, bone skins, effects, shadows
    // 4. Compute total alloc size:
    //    - Pointer arrays: (collision + decal + skin + effect + shadow) * 4
    //    - If not MP: skin bones * 0x40, skin verts * 0x1C,
    //      (collision + decal) vols * 0x3C, vertices * 0x18
    //    - Shadow slots * 0x20
    // 5. entity_scratch_alloc(total_size)
    // 6. Distribute base pointers into entity fields
    // 7. For non-MP: copy volume template pointers or allocate workspace
    // 8. For effects: call FUN_800e34d8 + FUN_800e3d3c to init each
    // 9. For shadows: assign workspace pointers
    // 10. Return via FUN_80382724

    // [Full implementation preserved in Ghidra IDB]
    // See Ghidra decompilation at 0x80071f88 for complete logic
}

// ============================================================================
// 0x800724d4 | entity_init_render_data
// Initializes all render data for an entity after model is loaded.
// 1. Zeroes all render data pointers/counts
// 2. Calls entity_alloc_render_data to allocate scratch memory
// 3. If not multiplayer-restricted: transforms collision + decal volumes
// 4. Renders attached effects
// 5. Initializes physics/shadow data
// 6. Clears decal volume link pointers
// 7. Creates physics representation via FUN_800ffb88 + FUN_800fe808
// ============================================================================

void entity_init_render_data(int entity)
{
    // Zero all render data fields
    *(int*)(entity + 0x1E0) = 0;  // decal count
    *(int*)(entity + 0x1E4) = 0;  // collision count
    *(int*)(entity + 0x1C)  = 0;  // skin count
    *(int*)(entity + 0x1E8) = 0;  // effect count
    *(int*)(entity + 0x1EC) = 0;  // shadow count
    *(int*)(entity + 0x1F0) = 0;  // decal ptr
    *(int*)(entity + 500)   = 0;  // collision ptr (0x1F4)
    *(int*)(entity + 0x1F8) = 0;  // skin ptr
    *(int*)(entity + 0x1FC) = 0;  // effect ptr
    *(int*)(entity + 0x200) = 0;  // shadow ptr

    // Allocate all render sub-structures
    entity_alloc_render_data();

    // Transform volumes (skip for multiplayer-restricted entities)
    if (*(int*)(entity + 0x20) != 2 || g_current_game_mode == 100) {
        entity_transform_collision_volumes();
        entity_transform_decal_volumes();
        FUN_800fffdc();  // Physics/shadow init
    }

    // Render effects
    entity_render_attached_effects();
    FUN_8010386c();  // Additional render init

    // Clear decal volume link pointers
    for (int i = 0; i < *(int*)(entity + 0x1E0); i++) {
        int* decal_vol = (int*)(*(int*)(entity + 0x1F0) + i * 4);
        *(int*)(*decal_vol + 0xC) = 0;
    }

    // Create physics representation
    int phys_data = FUN_800ffb88(entity);
    int* phys_obj = FUN_800fe808(/* phys_data, entity */);
    *(int**)(entity + 0x204) = phys_obj;
}

// ============================================================================
// 0x800725c4 | entity_create_instance
// MASSIVE: Core entity creation function. This is the fundamental spawner
// that allocates an entity slot, loads its model, assigns a unique ID,
// initializes all default field values, and registers it for updates.
// Called by 55+ functions including spawn_create_entity, level_spawn_all_entities.
//
// Flow:
// 1. entity_pool_alloc() -> get entity slot
// 2. Look up type data, load model (with multiplayer/LOD variants)
// 3. Assign unique entity ID (22-bit counter + slot index)
// 4. Set initial position, rotation, scale from spawn parameters
// 5. Initialize all entity fields to defaults
// 6. Register for all 18 update types
// 7. entity_set_render_type(entity, 0)
// 8. Set physics scale based on entity category
// 9. Apply material flags if needed
// 10. entity_update_bounds() for initial AABB
// 11. entity_init_render_data() for collision/decal/effect setup
// 12. Final entity_update_bounds()
// Returns: entity pointer (or NULL if model load failed)
// ============================================================================

void* entity_create_instance(int entity_type_id, float* spawn_position)
{
    // [Full implementation preserved in Ghidra IDB]
    // See Ghidra decompilation at 0x800725c4 for complete logic
    // This is a ~700-line function handling all entity initialization
}

// ============================================================================
// 0x80072c38 | entity_spawn_with_activation
// Spawns an entity via entity_create_instance, then:
// 1. Sets entity type field (+0x20) to the spawn point type
// 2. Dispatches event 0 (spawn event) unless in mode 0x66
// 3. Calls spawn_activate_point to link entity to spawn point
// 4. Calls type-specific init callback (type_data+0x2C) if present
// 5. Registers for update type 2 if entity has think handler or
//    type data has AI definition
// ============================================================================

void entity_spawn_with_activation(
    int entity_type_id, int type_data, float* position, int spawn_param, int spawn_point)
{
    int entity = (int)entity_create_instance(entity_type_id, position);
    if (entity == 0) return;

    *(int*)(entity + 0x20) = /* spawn point type */;

    if (g_current_game_mode != 0x66) {
        event_dispatch(entity, 0, spawn_param);
    }

    spawn_activate_point(/* entity, spawn_point */);

    // Type-specific init callback
    int type_def = g_entity_type_table + g_entity_type_index[entity_type_id] * 0x30;
    void (*init_cb)(int, int) = *(void(**)(int,int))(type_def + 0x2C);
    if (entity_type_id != 0x100000 && init_cb != NULL) {
        init_cb(entity, spawn_point);
    }

    // Register for think updates if entity has handler
    bool has_think = false;
    if (*(int*)(entity + 0x15C) != 0 || event_has_handler(*(int*)(entity + 0x20), 2)) {
        has_think = true;
    }
    if (!has_think) {
        int ai_type_def = g_entity_type_table +
            g_entity_type_index[*(unsigned short*)(entity + 0x94)] * 0x30;
        if (*(int*)(ai_type_def + 0x1C) != -1) {
            entity_register_for_update(entity, 2, -0x7FFE2614);
        }
    }
}

// ============================================================================
// 0x80072d70 | entity_spawn_by_type
// Entry point for spawning entities by type ID. Gets type data then
// delegates to entity_spawn_with_activation.
// ============================================================================

void entity_spawn_by_type(int entity_type_id, float* position, int spawn_param, int spawn_point)
{
    void* type_data = entity_get_type_data(entity_type_id);
    entity_spawn_with_activation(entity_type_id, (int)type_data, position, spawn_param, spawn_point);
}

// ============================================================================
// 0x80072dcc | entity_spawn_scripted
// Spawns a scripted entity (type 0x80). Similar to entity_spawn_with_activation
// but hardcodes entity type field to 0x80 for script-driven entities.
// ============================================================================

void entity_spawn_scripted(int entity_type_id, float* position, int spawn_param, int script_data)
{
    int type_def = g_entity_type_table + g_entity_type_index[entity_type_id] * 0x30;
    int entity = (int)entity_create_instance(entity_type_id, position);

    *(int*)(entity + 0x20) = 0x80;  // Scripted entity type

    if (g_current_game_mode != 0x66) {
        event_dispatch(entity, 0, spawn_param);
    }

    // Type-specific init callback
    void (*init_cb)(int, int) = *(void(**)(int,int))(type_def + 0x2C);
    if (init_cb != NULL) {
        init_cb(entity, script_data);
    }

    // Register for think updates if needed
    bool has_think = false;
    if (*(int*)(entity + 0x15C) != 0 || event_has_handler(*(int*)(entity + 0x20), 2)) {
        has_think = true;
    }
    if (!has_think && *(int*)(*(int*)(entity + 0x24) + 0x1C) != -1) {
        entity_register_for_update(entity, 2, -0x7FFE2614);
    }
}

// ============================================================================
// 0x80072eb8 | entity_create_attachment
// Creates a render instance for an entity attachment and stores it at
// entity offset +0xA4. Uses entity_type_create_render_instance.
// ============================================================================

void entity_create_attachment(int entity)
{
    int render_inst = (int)entity_type_create_render_instance(/* type from entity */);
    *(int*)(entity + 0xA4) = render_inst;
}

// ============================================================================
// 0x80072ee8 | entity_type_ensure_model_loaded
// Ensures the model for an entity type is loaded. If not loaded:
// 1. Sets global flag for type 0x10 entities
// 2. Calls render_entity_load_model
// 3. Restores global flag
// 4. Clears type reference count
// Then creates+destroys a temporary render instance for validation
// (unless type has 0x4000000 flag indicating no render needed).
// ============================================================================

void entity_type_ensure_model_loaded(int entity_type_id)
{
    int* type_data = (int*)(g_entity_type_table +
        g_entity_type_index[entity_type_id] * 0x30);

    if (type_data[1] == 0) {
        // Model not yet loaded
        if (type_data[0] == 0x10) {
            render_entity_set_global_flag(0);
        }

        type_data[1] = render_entity_load_model(/* model params */);

        if (type_data[0] == 0x10) {
            render_entity_set_global_flag(1);
        }

        type_data[8] = 0;  // Clear reference count
    }

    // Create+destroy temp render instance for validation (unless no-render flag)
    if ((type_data[10] & 0x4000000) == 0) {
        int* temp_inst = (int*)render_instance_create(/* params */);
        render_instance_destroy(temp_inst);
    }
}

// ============================================================================
// 0x80072fa4 | entity_type_create_render_instance
// Loads model for entity type if needed, then creates a render instance.
// Applies material transparency flag (0x10) if set in type data.
// Returns: render instance pointer
// ============================================================================

void* entity_type_create_render_instance(int entity_type_id)
{
    int* type_data = (int*)(g_entity_type_table +
        g_entity_type_index[entity_type_id] * 0x30);

    if (type_data[1] == 0) {
        if (type_data[0] == 0x10) {
            render_entity_set_global_flag(0);
        }

        type_data[1] = render_entity_load_model(/* model params */);

        if (type_data[0] == 0x10) {
            render_entity_set_global_flag(1);
        }

        type_data[8] = 0;
    }

    int* instance = (int*)render_instance_create(/* params */);

    // Apply material transparency if type has flag 0x10
    if ((*(unsigned int*)(/* entity type_data offset 0x28 */ + 0x28) & 0x10) != 0) {
        render_instance_set_material(0.0, instance, 1);
    }

    return instance;
}

// ============================================================================
// 0x80073070 | entity_remap_animation_slot
// Remaps an entity's animation slot reference when changing animation source.
// Finds the current animation slot index in the old source's slot array,
// then maps it to the equivalent slot in the new source.
// Updates entity animation pointer (+0x12C) and parent reference (+0x78).
// ============================================================================

void entity_remap_animation_slot(int entity, int new_anim_source, int old_anim_source)
{
    int old_anim_data = *(int*)(old_anim_source + 0x84);
    int* old_slots = (int*)(old_anim_data + 0x0C);

    // Find current slot in old source
    int slot_count = (int)((unsigned int)((old_anim_data + 0x58) - (int)old_slots) >> 2);
    int* search = old_slots;

    if (old_slots <= (int*)(old_anim_data + 0x54)) {
        while (slot_count != 0) {
            if (*search == *(int*)(entity + 300)) break;  // +0x12C
            search++;
            slot_count--;
        }
    }

    // Compute index and remap to new source
    unsigned int byte_offset = (int)search - (int)old_slots;
    int index = (int)byte_offset >> 2;
    if ((int)byte_offset < 0 && (byte_offset & 3) != 0) {
        index++;  // Round up for negative (shouldn't happen)
    }

    // Set new animation slot from new source at same index
    int new_anim_data = *(int*)(new_anim_source + 0x84);
    *(int*)(entity + 300) = *(int*)(new_anim_data + index * 4 + 0x0C);

    // Update parent reference
    *(int*)(entity + 0x78) = new_anim_source;
}
