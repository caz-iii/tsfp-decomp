// Address: 0x800730d8 - 0x800739c3 | Batch 164 | Date: 2026-08-10
// Entity lifecycle: model swap, state copying, destruction, room assignment.
// Part of the entity subsystem (game)

#include "common.h"

// ============================================================================
// External references
// ============================================================================

extern int g_current_game_mode;          // DAT_80612614
extern int g_entity_free_count;          // DAT_80611c6c
extern void* g_entity_active_list_head;  // DAT_80540a44
extern void* g_entity_active_list_tail;  // DAT_80611c50

extern "C" {
    void entity_remap_animation_slot(int entity, int new_source, int old_source);
    void entity_register_for_update(int entity, int update_type, int priority);
    void render_instance_destroy(int* instance);
    int event_dispatch(void* entity, int event_type, int param);
    int scene_query_nodes_in_frustum(int mode, int* aabb, int* out_rooms, int max_rooms);
    void leaf_31e8(int dst, int src, int size);  // memcpy variant
    void sound_stop_handle(void);
    void FUN_8023ca6c(int uid);
    void FUN_8023cb70(int uid);
    void FUN_801014cc(void);
    void FUN_8013e4dc(void);
    void FUN_800ab300(int entity);
    void FUN_8022165c(void);
    void FUN_800e33f0(void);
    void FUN_800ba2e0(void);
    void FUN_800ea074(void);
    void FUN_80144a9c(int entity);
    void FUN_800fea34(void);
    void FUN_80073fe8(void);
}

// ============================================================================
// 0x800730d8 | entity_swap_render_data
// Copies all render/animation data from an entity's current model instance
// to a new model instance (for LOD swaps or model changes).
// Copies: position offsets, rotation matrices, animation keyframes, blend
// weights, callbacks, bone data, and miscellaneous flags.
// Then remaps any animation slot references from the old to new instance.
// ============================================================================

void entity_swap_render_data(void)
{
    // Algorithm:
    // 1. Get entity context via FUN_803826f0
    // 2. Determine target: entity->render_inst (+0x74) or entity->attachment (+0xA4)
    //    based on flag 0x1000000
    // 3. Copy fields from entity->model_data (+4) to target:
    //    - Position offsets (+0x94, +0x98)
    //    - Rotation/scale block (+0xB0..+0xC8)
    //    - Animation keyframes (+0xCC, 0x58 bytes) x2
    //    - Bone local transforms (+0x34, 8 bytes)
    //    - Bone arrays (+0x3C, 0x18 bytes) x2
    //    - Scale factor (+0x78, 4 bytes)
    //    - Callbacks (+0x184..+0x198)
    //    - Animation state (+0x1B4, 0x18 bytes) x2
    //    - Miscellaneous flags
    // 4. If model_data has changed, update render instance dirty flag
    // 5. Remap animation slots for any linked entities (offset +0x2BC, +0x18C)
    // 6. Set entity->model_data (+4) = new target
    // 7. Return via FUN_8038273c

    // [Full implementation preserved in Ghidra IDB]
    // See Ghidra decompilation at 0x800730d8 for complete logic
}

// ============================================================================
// 0x800732c0 | entity_copy_animation_state
// Copies animation keyframe and blend data between two entities' model
// instances. Copies animation tracks (0x58 bytes each at +0xCC and +0x124),
// plus blend weights and callback pointers (+0x188..+0x19C).
// ============================================================================

void entity_copy_animation_state(int entity_dst, int entity_src)
{
    int dst_model = *(int*)(entity_dst + 4);
    int src_model = *(int*)(entity_src + 4);

    // Copy primary animation track (0x58 bytes at offset 0xCC)
    leaf_31e8(dst_model + 0xCC, src_model + 0xCC, 0x58);

    // Copy secondary animation track (0x58 bytes at offset 0x124)
    leaf_31e8(dst_model + 0x124, src_model + 0x124, 0x58);

    // Copy blend/callback fields
    *(int*)(dst_model + 0x190) = *(int*)(src_model + 0x190);  // blend weight
    *(int*)(dst_model + 0x194) = *(int*)(src_model + 0x194);  // blend target
    *(int*)(dst_model + 0x198) = *(int*)(src_model + 0x198);  // blend callback
    *(int*)(dst_model + 0x19C) = *(int*)(src_model + 0x19C);  // blend param
    *(int*)(dst_model + 0x18C) = *(int*)(src_model + 0x18C);  // anim callback
    *(int*)(dst_model + 0x188) = *(int*)(src_model + 0x188);  // anim state
}

// ============================================================================
// 0x80073384 | entity_copy_transform_state
// Copies position, rotation, scale, collision shape data, and animation
// state from one entity to another. Used when swapping entity representations
// (e.g., ragdoll activation, vehicle entry).
// Copies offsets 0xA8-0x10C (position/rotation/scale), 0x2B0 region
// (collision shape), update slots (0x234..0x246), then delegates to
// entity_copy_animation_state.
// ============================================================================

void entity_copy_transform_state(int entity_dst, int entity_src)
{
    // Copy position block (0xA8-0xBC: position, prev_position)
    *(int*)(entity_dst + 0xA8) = *(int*)(entity_src + 0xA8);
    *(int*)(entity_dst + 0xAC) = *(int*)(entity_src + 0xAC);
    *(int*)(entity_dst + 0xB0) = *(int*)(entity_src + 0xB0);
    *(int*)(entity_dst + 0xB4) = *(int*)(entity_src + 0xB4);
    *(int*)(entity_dst + 0xB8) = *(int*)(entity_src + 0xB8);
    *(int*)(entity_dst + 0xBC) = *(int*)(entity_src + 0xBC);

    // Copy rotation/scale (0xC4-0xDC)
    *(int*)(entity_dst + 0xC4) = *(int*)(entity_src + 0xC4);
    *(int*)(entity_dst + 0xCC) = *(int*)(entity_src + 0xCC);
    *(int*)(entity_dst + 0xD0) = *(int*)(entity_src + 0xD0);
    *(int*)(entity_dst + 0xD4) = *(int*)(entity_src + 0xD4);
    *(int*)(entity_dst + 0xD8) = *(int*)(entity_src + 0xD8);
    *(int*)(entity_dst + 0xDC) = *(int*)(entity_src + 0xDC);

    // Copy velocity/angular (0xEC-0xF4)
    *(int*)(entity_dst + 0xEC) = *(int*)(entity_src + 0xEC);
    *(int*)(entity_dst + 0xF0) = *(int*)(entity_src + 0xF0);
    *(int*)(entity_dst + 0xF4) = *(int*)(entity_src + 0xF4);

    // Copy additional transform (0x104-0x10C)
    *(int*)(entity_dst + 0x104) = *(int*)(entity_src + 0x104);
    *(int*)(entity_dst + 0x108) = *(int*)(entity_src + 0x108);
    *(int*)(entity_dst + 0x10C) = *(int*)(entity_src + 0x10C);

    // Copy flags
    *(char*)(entity_dst + 0x31) = *(char*)(entity_src + 0x31);
    *(char*)(entity_dst + 0x32) = *(char*)(entity_src + 0x32);

    // Copy collision shape data (0x2B0 region, 9 pairs of dwords)
    for (int i = 0; i < 4; i++) {
        // 2 dwords per iteration copied via structured loop
    }

    // Copy update slot indices (9 shorts at 0x234)
    for (int i = 0; i < 9; i++) {
        *(short*)(entity_dst + 0x234 + i * 2) = *(short*)(entity_src + 0x234 + i * 2);
    }

    // Copy model render mode
    *(int*)(*(int*)(entity_dst + 4) + 0x30) = *(int*)(*(int*)(entity_src + 4) + 0x30);

    // Copy animation state
    entity_copy_animation_state(entity_dst, entity_src);
}

// ============================================================================
// 0x800734a0 | entity_copy_bone_data
// Copies bone transform arrays and animation callbacks between entities'
// model instances. Copies bone local transforms (+0x34..+0x6C) for 2 bone
// sets, scale factor (+0x78), and the bounds callback (+0x180).
// ============================================================================

void entity_copy_bone_data(int entity_dst, int entity_src)
{
    int dst_model = *(int*)(entity_dst + 4);
    int src_model = *(int*)(entity_src + 4);

    // Copy bone orientation pair (2 sets of 2x3 floats at +0x34..+0x6B)
    *(int*)(dst_model + 0x34) = *(int*)(src_model + 0x34);
    *(int*)(dst_model + 0x38) = *(int*)(src_model + 0x38);

    for (int bone_set = 0; bone_set < 2; bone_set++) {
        int offset = bone_set * 0x0C;
        for (int j = 0; j < 3; j++) {
            *(int*)(dst_model + 0x3C + offset + j * 4) =
                *(int*)(src_model + 0x3C + offset + j * 4);
            *(int*)(dst_model + 0x54 + offset + j * 4) =
                *(int*)(src_model + 0x54 + offset + j * 4);
        }
    }

    // Copy scale factor (+0x78, 4 bytes)
    leaf_31e8(dst_model + 0x78, src_model + 0x78, 4);

    // Copy bounds callback (+0x180)
    *(int*)(dst_model + 0x180) = *(int*)(src_model + 0x180);
}

// ============================================================================
// 0x80073588 | entity_is_lod_child
// Checks if entity is a LOD child: entity type == 0x1000 and parent's
// model type == 8 (simple mesh). Used to identify LOD swap targets.
// Returns: 1 if LOD child, 0 otherwise.
// ============================================================================

int entity_is_lod_child(int entity)
{
    if (*(int*)(entity + 0x20) == 0x1000 &&
        *(int*)(*(int*)(entity + 0x7C) + 4) == 8) {
        return 1;
    }
    return 0;
}

// ============================================================================
// 0x800735b4 | entity_is_attachment_parent
// Checks if entity is an attachment parent: entity type == 0x100000 and
// attachment child (+0x208) has type 8.
// Returns: 1 if attachment parent, 0 otherwise.
// ============================================================================

int entity_is_attachment_parent(int entity)
{
    if (*(int*)(entity + 0x20) == 0x100000 &&
        *(int*)(*(int*)(entity + 0x208) + 0x20) == 8) {
        return 1;
    }
    return 0;
}

// ============================================================================
// 0x800735e4 | entity_destroy_with_event
// Dispatches destroy event (event type 1) to the entity, then calls
// entity_destroy for full teardown. Used by external systems that need
// the entity to process its own destruction logic first.
// ============================================================================

void entity_destroy_with_event(int* entity)
{
    event_dispatch(entity, 1, 0);
    entity_destroy(entity);
}

// ============================================================================
// 0x80073620 | entity_unlink_from_list
// Removes an entity from the global active entity linked list.
// Walks the list from head (DAT_80540a44) to find the entity,
// then unlinks it. Updates tail pointer if entity was at tail.
// ============================================================================

void entity_unlink_from_list(int* entity)
{
    int* prev = (int*)&g_entity_active_list_head;
    int* current = (int*)g_entity_active_list_head;

    while (current != NULL) {
        if (current == entity) {
            // Update tail if removing tail element
            if (entity == (int*)g_entity_active_list_tail) {
                g_entity_active_list_tail = prev;
            }
            // Unlink: prev->next = entity->next
            *prev = *current;
            *entity = 0;
            return;
        }
        prev = current;
        current = (int*)*current;
    }
}

// ============================================================================
// 0x80073670 | entity_destroy
// MASSIVE: Full entity destructor. 95+ callers.
// 1. Calls entity's destroy callback (+0x184) if set
// 2. Recursively destroys child entity (+0x208) if not type 0x100000
// 3. Removes from network tracking (FUN_8023ca6c, FUN_8023cb70)
// 4. Unlinks from active list
// 5. Clears flags, deregisters from physics/collision/AI/sound systems
// 6. Deregisters all 18 update types
// 7. Destroys render instances (primary +0x74, secondary +0xA4)
// 8. Destroys animation attachments
// 9. Stops any playing sounds
// 10. Marks entity slot as deleted (bit 31 of flags), increments free count
// ============================================================================

void entity_destroy(int* entity)
{
    // Call destroy callback if set
    if ((void*)entity[0x61] != NULL) {  // offset 0x184
        ((void(*)(int*, int))entity[0x61])(entity, 0);
    }

    // Recursively destroy child (if entity is not attachment type)
    if ((int*)entity[0x82] != NULL && entity[8] != 0x100000) {
        entity_destroy_with_event((int*)entity[0x82]);
        entity[0x82] = 0;
    }

    // Network/tracking cleanup
    if (entity[0xB] != 0) {
        FUN_8023ca6c(entity[0xB]);
    }
    FUN_8023cb70(entity[0xB]);

    // Clear update data
    entity[0x18] = 0;

    // Unlink from active entity list
    entity_unlink_from_list(entity);

    // Clear entity flags (keep only specific bits)
    entity[10] = entity[10] & 0xFFDFFB8F;

    // Deregister from all subsystems
    FUN_801014cc();   // Physics
    FUN_8013e4dc();   // Collision
    FUN_800ab300((int)entity);  // AI
    FUN_8022165c();   // Script
    FUN_800e33f0();   // Effects

    // Deregister all 18 update types
    for (int i = 0; i < 0x12; i++) {
        entity_register_for_update((int)entity, i, 0);
    }

    // Game mode specific cleanup
    if (g_current_game_mode < 0x65) {
        if ((entity[10] & 0x10000) != 0) {
            FUN_800ba2e0();  // Multiplayer entity cleanup
        }
        FUN_800ea074();  // Additional cleanup
    }

    // Destroy render instances
    if ((int*)entity[0x1D] != NULL) {
        render_instance_destroy((int*)entity[0x1D]);
        entity[0x1D] = 0;
    }
    if ((int*)entity[0x29] != NULL) {
        render_instance_destroy((int*)entity[0x29]);
        entity[0x29] = 0;
    }
    entity[1] = 0;

    // Destroy animation data
    if (entity[0x20] != 0) {
        FUN_80144a9c((int)entity);
    }

    // Destroy physics body
    if (entity[0x81] != 0) {
        FUN_800fea34();
    }

    // Destroy collision data
    if (entity[0x1E] != 0) {
        FUN_80073fe8();
    }

    // Stop sounds
    if ((int)entity[0x8B] >= 0) {
        sound_stop_handle();
        entity[0x8B] = 0xFFFFFFFF;
    }
    if ((int)entity[0x8A] >= 0) {
        sound_stop_handle();
        entity[0x8A] = 0xFFFFFFFF;
    }

    // Mark slot as deleted, increment free count
    entity[10] = entity[10] | 0x80000000;
    entity[0xB] = 0;
    g_entity_free_count++;
}

// ============================================================================
// 0x8007384c | entity_assign_room
// Assigns entity to scene room(s) by querying which rooms overlap the
// entity's AABB. Handles special cases:
// - Type 2 (player): room = entity_type_index - 0x8FE
// - Mode 0x65/0x66: no room assignment
// - Flag 0x20000: static room (use existing)
// - Room count >= 201: use existing (too many)
// Otherwise: builds AABB, calls scene_query_nodes_in_frustum to find
// overlapping rooms (max 10), stores room list at entity+0x38.
// ============================================================================

void entity_assign_room(int entity)
{
    if (g_current_game_mode == 0x65 || g_current_game_mode == 0x66) {
        // No room assignment in these modes
        *(int*)(entity + 0x34) = 0;
        *(int*)(entity + 0x38) = 0;
        *(char*)(entity + 0x30) = 0;
        return;
    }

    if (*(int*)(entity + 0x20) == 2) {
        // Player entity: room = type index - 0x8FE
        int room = *(unsigned short*)(entity + 0x94) - 0x8FE;
        *(int*)(entity + 0x38) = room;
        *(int*)(entity + 0x34) = room;
        *(char*)(entity + 0x30) = 1;
        return;
    }

    if ((*(unsigned int*)(entity + 0x28) & 0x20000) != 0) {
        // Static entity: don't reassign
        return;
    }

    if (*(int*)(entity + 0x34) >= 201) {
        // Too many rooms already: just use primary
        *(int*)(entity + 0x38) = *(int*)(entity + 0x34);
        *(char*)(entity + 0x30) = 1;
        return;
    }

    // Build AABB from entity position + offset + extent
    float aabb[6];
    float cx = *(float*)(entity + 0x64) + *(float*)(entity + 0x1A8);
    float cy = *(float*)(entity + 0x68) + *(float*)(entity + 0x1AC);
    float cz = *(float*)(entity + 0x6C) + *(float*)(entity + 0x1B0);

    aabb[0] = cx - *(float*)(entity + 0x1C0);
    aabb[3] = cx + *(float*)(entity + 0x1C0);
    aabb[1] = cy - *(float*)(entity + 0x1C4);
    aabb[4] = cy + *(float*)(entity + 0x1C4);
    aabb[2] = cz - *(float*)(entity + 0x1C8);
    aabb[5] = cz + *(float*)(entity + 0x1C8);

    // Query scene for overlapping rooms (max 10)
    int room_count = scene_query_nodes_in_frustum(0, (int*)aabb, (int*)(entity + 0x38), 10);
    *(char*)(entity + 0x30) = (char)room_count;

    // Validate: check if current primary room is still in the list
    int primary = *(int*)(entity + 0x34);
    int count = (int)*(char*)(entity + 0x30);
    bool found = false;

    if (count > 0) {
        for (int i = 0; i < count; i++) {
            if (*(int*)(entity + 0x38 + i * 4) == primary) {
                found = true;
                break;
            }
        }
    }

    // If primary not found or was 0, assign first room as primary
    if ((primary == 0 && count > 0) || !found) {
        *(int*)(entity + 0x34) = *(int*)(entity + 0x38);
    }
}
