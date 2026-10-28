// entity_update.cpp
// Entity per-frame update pipeline: transforms, room assignment, triggers, bone matrices
// Address range: 0x800739c4 - 0x80075020
// Date: 2026-08-10

#include "common.h"

// Forward declarations
extern int level_get_info_cached(int id);
extern int scene_node_get_texture_data(int node_id, int param);
extern int scene_node_get_material_count(int node_id, int param);
extern int scene_get_data_by_index(int idx);
extern bool scene_block_is_loaded(int block);
extern void render_instance_clear_visual_state(int instance);
extern int mem_advance_secondary(int size);
extern void event_dispatch(int entity, int event_type, int param);
extern int soundpad_find_zone_by_id(int zone_id);
extern void entity_update_bounds(int entity);
extern void entity_assign_room(int entity);
extern void entity_transform_collision_volumes(void);
extern void entity_transform_decal_volumes(void);
extern void entity_render_attached_effects(void);

// External data
extern int DAT_80611c24;   // Entity pool base pointer
extern int DAT_80611c28;   // Per-trigger activation counters
extern int DAT_80611c70;   // Entity pool count
extern int DAT_80611d54;   // Room/zone visibility data
extern int DAT_80611d58;   // Player count / active player mask
extern int DAT_80611d5c;   // Room PVS data pointer
extern int DAT_80611d78;   // Player info array (stride 0x141c)
extern int DAT_80611b04;   // Scene node table pointer
extern int DAT_80612610;   // Active player count
extern int DAT_80612614;   // Current level ID
extern int DAT_80611928;   // Frame counter / timestamp
extern int DAT_806108e4;   // Delta time tick count
extern int DAT_805015c0;   // Global flags (bit 29 = editor/debug mode)
extern char DAT_805015bd;  // Player index filter
extern char DAT_805015bf;  // Trigger visibility mode
extern void* DAT_80611c48; // Trigger activation callback
extern void* DAT_80540a44; // Entity dirty list head
extern int DAT_80611c4c;   // Entity dirty list count
extern char* DAT_80611c30; // Entity dirty bitfield
extern float FLOAT_80613fb4; // 0.0f
extern float FLOAT_80613fbc; // 1.0f
extern float FLOAT_80613fc0; // Identity matrix element
extern float FLOAT_806108ec; // Delta time (seconds)


// ============================================================================
// Address: 0x800739c4 | Name: entity_resolve_material
// Resolves which material/texture to use for an entity based on room visibility.
// For entities in rooms 0xCA-0xCD (player-owned), uses player camera material.
// For entities with parent attachments, recursively resolves from parent.
// Otherwise, tests materials via FUN_80099604 against each room the entity occupies.
// ============================================================================
void entity_resolve_material(void) {
    int entity;
    int room_id;
    int material_data;
    int material_count;
    int alt_room;
    int alt_material_count;
    double dist;
    int hit_result = 0;
    float hit_pos[3];

    entity = FUN_803826e8(); // get current entity from iterator
    room_id = *(int*)(entity + 0x34);

    // Player-owned rooms (0xCA-0xCD) — use player camera material directly
    if (room_id >= 0xCA && room_id <= 0xCD) {
        int player_idx = room_id - 0xCA;
        int player_data = *(int*)(DAT_80611d78 + player_idx * 0x141c + 0x14);
        int camera = *(int*)(player_data + 0x7c);
        *(int*)(entity + 0x21C) = *(int*)(camera + 0xC00);
        FUN_80382734();
        return;
    }

    // Attached entity — resolve from parent recursively
    if (*(int*)(entity + 0x78) != 0) {
        entity_resolve_material(); // recursive call on parent
        int parent = **(int**)(entity + 0x78);
        *(int*)(entity + 0x21C) = *(int*)(parent + 0x21C);
        FUN_80382734();
        return;
    }

    // Entity type 8 (character) — use camera material from entity's own camera ptr
    if (*(int*)(entity + 0x20) == 8) {
        *(int*)(entity + 0x21C) = *(int*)(*(int*)(entity + 0x7c) + 0xC00);
        FUN_80382734();
        return;
    }

    // Standard entities — find material via visibility test on each room
    if (room_id > 0 && room_id < 0xC9) {
        hit_pos[0] = *(float*)(entity + 0x64);  // pos.x
        hit_pos[1] = *(float*)(entity + 0x68) + *(float*)(entity + 0x1AC); // pos.y + height offset
        hit_pos[2] = *(float*)(entity + 0x6C);  // pos.z

        int node_idx = *(int*)(entity + 0x34);
        material_data = scene_node_get_texture_data(node_idx, 1);
        material_count = scene_node_get_material_count(node_idx, 1);
        int scene_flags = *(int*)(*(int*)(*(int*)(DAT_80611b04 + node_idx * 4) + 4) + 0x30);
        dist = (double)FUN_80099604(material_count, material_data, scene_flags, hit_pos, &hit_result);

        // Try alternate rooms if primary didn't hit
        if (hit_result == 0) {
            int num_rooms = *(char*)(entity + 0x30);
            for (int i = 0; i < num_rooms && hit_result == 0; i++) {
                alt_room = *(int*)(entity + 0x38 + i * 4);
                if (alt_room != *(int*)(entity + 0x34)) {
                    material_data = scene_node_get_texture_data(alt_room, 1);
                    alt_material_count = scene_node_get_material_count(alt_room, 1);
                    scene_flags = *(int*)(*(int*)(*(int*)(DAT_80611b04 + alt_room * 4) + 4) + 0x30);
                    dist = (double)FUN_80099604(alt_material_count, material_data, scene_flags,
                                               hit_pos, &hit_result);
                }
            }
        }

        if (hit_result != 0) {
            *(int*)(entity + 0x21C) = hit_result;
            *(float*)(entity + 0x218) = (float)dist;
        }
    }

    FUN_80382734();
}


// ============================================================================
// Address: 0x80073b98 | Name: entity_update_transform
// Per-entity transform update. Detects position/rotation changes, updates bounds,
// assigns room, transforms collision/decal volumes, computes velocity from delta.
// Called from the entity render/update loop (FUN_800753bc).
// ============================================================================
void entity_update_transform(void) {
    int entity;
    bool pos_changed = false;
    bool rot_changed = false;

    entity = FUN_803826ec(); // get current entity

    uint dirty_flags = (*(uint*)(entity + 0x18) | *(uint*)(entity + 0x08)) & 2;

    // Detect position change (compare current vs previous)
    if (*(float*)(entity + 0x64) != *(float*)(entity + 0xEC) ||
        *(float*)(entity + 0x68) != *(float*)(entity + 0xF0) ||
        *(float*)(entity + 0x6C) != *(float*)(entity + 0xF4)) {
        pos_changed = true;
        *(uint*)(entity + 0x18) |= 0x100; // mark position dirty
    }

    // Detect rotation change
    if (*(float*)(entity + 0xC0) != *(float*)(entity + 0x104) ||
        *(float*)(entity + 0x70) != *(float*)(entity + 0x108) ||
        *(float*)(entity + 0xC8) != *(float*)(entity + 0x10C)) {
        rot_changed = true;
    }

    // Update bounding volumes if rotation changed or forced dirty
    if (rot_changed || dirty_flags != 0) {
        entity_update_bounds(entity);
    }

    // Update shadow/render data if forced dirty
    if (dirty_flags != 0) {
        int* render_data = *(int**)(entity + 4);
        if (render_data != NULL && render_data[0x24] != 0) {
            FUN_8001f994(render_data);
        }
    }

    // Full transform update if position/rotation changed or first frame
    if (pos_changed || rot_changed || *(char*)(entity + 0x30) == 0 || dirty_flags != 0) {
        entity_assign_room(entity);
        if ((*(uint*)(entity + 0x28) & 0x800) != 0) {
            entity_transform_collision_volumes();
            entity_transform_decal_volumes();
            FUN_800fffdc(); // update trigger volumes
            entity_render_attached_effects();
            FUN_8010386c(); // update shadow projection
        }
        entity_resolve_material();
    } else if (((*(uint*)(entity + 0x08) | *(uint*)(entity + 0x18)) & 0x80) != 0) {
        // Collision-only update (e.g. animation changed collision shape)
        entity_transform_collision_volumes();
        *(uint*)(entity + 0x08) &= ~0x80;
        *(uint*)(entity + 0x18) &= ~0x80;
    }

    // Mark XZ movement for footstep/audio system
    if (*(float*)(entity + 0x64) != *(float*)(entity + 0xEC) ||
        *(float*)(entity + 0x6C) != *(float*)(entity + 0xF4)) {
        *(uint*)(entity + 0x2B4) |= 0x80000000;
    }

    // Store previous rotation
    if (rot_changed) {
        *(uint*)(entity + 0x28) |= 0x1000;
        *(int*)(entity + 0x104) = *(int*)(entity + 0xC0);
        *(int*)(entity + 0x108) = *(int*)(entity + 0x70);
        *(int*)(entity + 0x10C) = *(int*)(entity + 0xC8);
    }

    // Compute velocity from position delta
    float zero = FLOAT_80613fb4; // 0.0f
    *(float*)(entity + 0xF8) = zero;
    *(float*)(entity + 0xFC) = zero;
    *(float*)(entity + 0x100) = zero;

    if (pos_changed) {
        float dx = *(float*)(entity + 0x64) - *(float*)(entity + 0xEC);
        float dy = *(float*)(entity + 0x68) - *(float*)(entity + 0xF0);
        float dz = *(float*)(entity + 0x6C) - *(float*)(entity + 0xF4);

        int entity_type = *(int*)(entity + 0x20);
        // Character or vehicle entities — report movement sound
        if ((entity_type == 8 || entity_type == 0x10000000)) {
            float dist_sq = dx*dx + dy*dy + dz*dz;
            if (dist_sq > 0.0f) {
                int sound_data = 0;
                if (entity_type == 0x10000000) {
                    int parent = *(int*)(*(int*)(entity + 0x7C) + 0x10);
                    if (parent != 0) sound_data = *(int*)(parent + 0x7C);
                } else {
                    sound_data = *(int*)(entity + 0x7C);
                }
                if (sound_data != 0) {
                    double dist = FUN_80131538((double)dist_sq); // sqrt
                    FUN_80237b50(dist, 0x30, *(int*)(sound_data + 8));
                }
            }
        }

        // Compute velocity = delta / dt
        float inv_dt = (DAT_806108e4 > 0) ? (FLOAT_80613fbc / FLOAT_806108ec) : 0.0f;
        *(float*)(entity + 0xF8) = dx * inv_dt;
        *(float*)(entity + 0xFC) = dy * inv_dt;
        *(float*)(entity + 0x100) = dz * inv_dt;

        // Store current as previous
        *(int*)(entity + 0xEC) = *(int*)(entity + 0x64);
        *(int*)(entity + 0xF0) = *(int*)(entity + 0x68);
        *(int*)(entity + 0xF4) = *(int*)(entity + 0x6C);
    }

    // Snapshot flags for this frame
    *(int*)(entity + 0x98) = *(int*)(entity + 0x28);
    *(int*)(entity + 0x90) = DAT_80611928;
    FUN_80382738();
}


// ============================================================================
// Address: 0x80073ee0 | Name: entity_select_best_room
// Iterates all entities. For entities with multiple room assignments,
// selects the best room based on priority (lowest value) and PVS visibility.
// ============================================================================
void entity_select_best_room(void) {
    int entity_base = DAT_80611c24;
    int entity_count = DAT_80611c70;

    for (int i = 0; i < entity_count; i++) {
        int ent = entity_base + i * 0x2E8;

        // Skip disabled/hidden entities (flags 0xC0000000)
        if ((*(uint*)(ent + 0x28) & 0xC0000000) != 0) continue;

        int num_rooms = (int)*(char*)(ent + 0x30);
        if (num_rooms < 2) {
            // Single room — just copy it to active slot
            *(int*)(ent + 0x34) = *(int*)(ent + 0x38);
            continue;
        }

        // Multiple rooms — pick best by priority + PVS check
        int best_priority = 999;
        int best_room = 0;
        for (int j = 0; j < num_rooms; j++) {
            int room_id = *(int*)(ent + 0x38 + j * 4);
            int priority = *(int*)(DAT_80611d54 + room_id * 0x0C + 0x3BC);
            if (priority < best_priority && priority >= 0) {
                // PVS visibility check
                char visible = 0;
                if (*(int*)(DAT_80611d54 + 0x214) < DAT_80611d58) {
                    visible = *(char*)(DAT_80611d5c +
                              *(int*)(DAT_80611d54 + 0x214) * 0xFFC + room_id + 0xD2C);
                }
                if (visible != 0) {
                    best_priority = priority;
                    best_room = room_id;
                }
            }
        }

        if (best_room < 1) {
            *(int*)(ent + 0x34) = *(int*)(ent + 0x38); // fallback to first
        } else {
            *(int*)(ent + 0x34) = best_room;
        }
    }
}


// ============================================================================
// Address: 0x80073fe8 | Name: entity_reset_bone_transforms
// Resets an entity's bone/matrix state to identity. Clears position/rotation
// offsets, attachment pointer, animation flags. Called when entity is respawned
// or model is swapped.
// ============================================================================
void entity_reset_bone_transforms(void) {
    int entity = FUN_803826f0(); // get entity from iterator

    *(int*)(entity + 0x12C) = -1;        // bone attachment index = none
    *(int*)(entity + 0x78) = 0;          // clear parent attachment ptr
    *(float*)(entity + 0x134) = 0.0f;    // offset X
    *(float*)(entity + 0x138) = 0.0f;    // offset Y
    *(float*)(entity + 0x13C) = 0.0f;    // offset Z
    *(float*)(entity + 0x140) = 0.0f;    // rotation X
    *(float*)(entity + 0x144) = 0.0f;    // rotation Y
    *(float*)(entity + 0x148) = 0.0f;    // rotation Z
    *(int*)(entity + 0x128) = 0;         // attachment mode flags
    *(uint*)(entity + 0x28) &= ~0x40000000; // clear "has attachment" flag

    // Reset bone matrices to identity if allocated
    int render_data = *(int*)(entity + 4);
    if (render_data != 0 && *(int*)(render_data + 0x10) != 0) {
        int bone_count = *(int*)(*(int*)(render_data + 4) + 4);
        int offset = 0;
        for (int i = 0; i < bone_count; i++) {
            FUN_8012ef38(*(int*)(render_data + 0x10) + offset); // identity 4x4
            offset += 0x40;
        }
    }

    FUN_8038273c();
}


// ============================================================================
// Address: 0x8007409c | Name: entity_update_dispatch
// MASSIVE: Main per-frame entity update dispatcher. Iterates all entities,
// evaluates dirty conditions (animation state, visibility, position, time-fade,
// scene block loading), builds a linked list of entities needing bone matrix
// updates, allocates bone matrix buffers from secondary memory pool.
// Two-pass: first pass determines dirty set, second pass allocates matrices.
// ============================================================================
void entity_update_dispatch(void) {
    int entity_stride = 0x2E8;
    unsigned char dirty_bitfield[320]; // local_168

    FUN_803826e8(); // begin entity iteration
    FUN_800ffbe8(); // pre-update hook (collision flush?)

    DAT_80611c30 = (char*)dirty_bitfield;
    DAT_80540a44 = NULL;  // clear dirty list head
    DAT_80611c4c = 0;     // clear dirty count
    memset_wrapper((int)DAT_80611c30, 0, 0x139);

    // --- PASS 1: Determine which entities need bone matrix updates ---
    for (int i = 0; i < DAT_80611c70; i++) {
        int* ent = (int*)(DAT_80611c24 + i * entity_stride);

        // Skip disabled entities (bit 31)
        if ((ent[10] & 0x80000000) != 0) continue;

        // Reset per-frame state
        ent[10] |= 0x100000;       // mark "needs eval"
        ent[10] &= ~0x200000;      // clear "evaluated"
        *(short*)(ent + 5) = 0;    // clear frame counter
        // Clear render state vectors
        *(int*)(ent[1] + 0x20) = 0;
        *(int*)(ent[1] + 0x24) = 0;
        *(int*)(ent[1] + 0x28) = 0;
        *(int*)(ent[1] + 0x2C) = 0;

        if ((ent[6] & 4) != 0) {
            render_instance_clear_visual_state(ent[1]);
            ent[6] &= ~4;
        }
        if (ent[7] != 0) {
            FUN_801020cc(); // clear attached particle state
        }

        int bit_index = i & 7;
        int byte_index = i >> 3;

        // Editor/debug mode — force all entities dirty
        if ((DAT_805015c0 & 0x20000000) != 0) {
            // Add to dirty list
            ent[0] = (int)DAT_80540a44;
            DAT_80611c4c++;
            DAT_80540a44 = (void*)ent;
            DAT_80611c30[byte_index] |= (1 << bit_index);
            ent[4] = ent[3] | (((1 << DAT_80611d58) - 1) & 0xFFFF);
            ent[3] = 0;
            continue;
        }

        int entity_type = ent[8]; // entity update type

        if (entity_type == 8) {
            // Character entity — complex visibility/fade logic
            int char_data = ent[0x1F]; // character-specific data
            // ... (player visibility fade, per-player dirty bits, weapon attachments)
            // Adds entity + weapon/accessory sub-entities to dirty list
            // [Condensed: full logic handles time-fade for cloaking, per-player PVS bits]
            ent[0] = (int)DAT_80540a44;
            DAT_80611c4c++;
            DAT_80540a44 = (void*)ent;
            DAT_80611c30[byte_index] |= (1 << bit_index);
            ent[4] = ent[3];
            ent[3] = 0;
        } else if (entity_type == 2) {
            // Type 2: all-players-visible
            ent[4] = ent[3] | (((1 << DAT_80611d58) - 1) & 0xFFFF);
            ent[3] = 0;
        } else {
            // Generic entities — check various dirty conditions
            uint obj_flags = *(uint*)(ent[9] + 0x28);

            if ((obj_flags & 0x20) != 0) {
                // Always skip (invisible)
                ent[4] = ent[3];
                ent[3] = 0;
            } else if ((obj_flags & 0x200) != 0) {
                // Force dirty (always animate)
                ent[0] = (int)DAT_80540a44;
                DAT_80611c4c++;
                DAT_80540a44 = (void*)ent;
                DAT_80611c30[byte_index] |= (1 << bit_index);
                ent[4] = ent[3] | (((1 << DAT_80611d58) - 1) & 0xFFFF);
                ent[3] = 0;

            } else {
                // Check multiple dirty conditions
                bool needs_update = false;

                if ((ent[6] & 0xC0) != 0) needs_update = true;        // render state changed
                if ((obj_flags & 0x40) != 0 && (ent[3] & 0xFFFF) != 0) needs_update = true; // visible + player bits
                if ((obj_flags & 0x80) != 0 && (ent[3] & 0x80000000) != 0) needs_update = true; // shadow
                if ((obj_flags & 0x4000) != 0) {
                    // Animation playing check
                    int rd = ent[1];
                    if (rd != 0 && *(int*)(rd + 0xCC) > 1) {
                        if (*(float*)(rd + 0x100) != 0.0f) needs_update = true;
                        if (*(int*)(rd + 0x124) > 1 && *(float*)(rd + 0x158) != 0.0f)
                            needs_update = true;
                    }
                }
                if ((obj_flags & 0x100) != 0 && (ent[3] & 0x40000000) != 0) needs_update = true;
                if ((obj_flags & 0x1000) != 0 && (ent[10] & 0x8000000) != 0) needs_update = true;
                if ((ent[10] & 0x4000000) != 0) needs_update = true;
                // Time-since-last-update check
                int update_interval = *(int*)(ent[9] + 0x24);
                if (update_interval != 0 && update_interval < DAT_80611928 - ent[0x24])
                    needs_update = true;
                if ((ent[6] & 2) != 0) needs_update = true;  // force flag
                if ((ent[2] & 2) != 0) needs_update = true;  // motion flag

                // Scene block visibility check
                if (!needs_update && (obj_flags & 0x800000) != 0) {
                    int scene_data = scene_get_data_by_index(ent[0xD]);
                    if (scene_block_is_loaded(scene_data)) {
                        needs_update = true;
                    }
                }

                if (needs_update) {
                    ent[0] = (int)DAT_80540a44;
                    DAT_80611c4c++;
                    DAT_80540a44 = (void*)ent;
                    DAT_80611c30[byte_index] |= (1 << bit_index);
                }
                ent[4] = ent[3];
                ent[3] = 0;
            }
        }
    }

    // --- PASS 2: Allocate bone matrix buffers for dirty entities ---
    for (int i = 0; i < DAT_80611c70; i++) {
        int ent_addr = DAT_80611c24 + i * entity_stride;
        int byte_index = i >> 3;
        int bit_index = i & 7;

        if ((DAT_80611c30[byte_index] & (1 << bit_index)) == 0) continue;

        int render_data = *(int*)(ent_addr + 4);
        int bone_count = *(int*)(*(int*)(render_data + 4) + 4);
        if (bone_count == 0) {
            *(int*)(render_data + 0x10) = 0;
        } else {
            int buf = mem_advance_secondary(bone_count * 0x40); // 4x4 matrix per bone
            *(int*)(render_data + 0x18) = buf;
            *(int*)(render_data + 0x10) = buf;
            *(uint*)(ent_addr + 0x28) &= ~0x100; // clear "needs alloc" flag
        }

        // Initialize all bone matrices to identity
        int offset = 0;
        int count = *(int*)(*(int*)(render_data + 4) + 4);
        for (int j = 0; j < count; j++) {
            FUN_8012ef38(*(int*)(render_data + 0x10) + offset);
            offset += 0x40;
        }
        *(uint*)(ent_addr + 0x28) &= ~0x08; // clear "matrices invalid" flag
    }

    FUN_80382734(); // end entity iteration
}


// ============================================================================
// Address: 0x80074828 | Name: entity_trigger_activate
// Activates a level trigger. Checks player visibility mode flags to determine
// which player can activate it, calls the trigger callback, increments counter.
// param_1: pointer to trigger entry (stride 0x34 in level trigger table)
// ============================================================================
void entity_trigger_activate(int trigger) {
    bool can_activate = false;
    uint flags = *(uint*)(trigger + 4);

    // Skip non-player-specific triggers when player filter is active
    if ((flags & 0x700) != 0 && DAT_805015bd != '\n') {
        return;
    }

    // Check which player visibility mode matches
    switch (DAT_805015bf) {
        case 0: can_activate = (flags & 0x100) != 0; break; // player 1
        case 1: // fall through
        case 3: can_activate = (flags & 0x200) != 0; break; // player 2/4
        case 2: can_activate = (flags & 0x400) != 0; break; // player 3
    }

    if (!can_activate) return;
    if (DAT_80611c48 == NULL) return;

    // Call the trigger activation callback
    typedef int (*trigger_callback_t)(int);
    trigger_callback_t callback = (trigger_callback_t)DAT_80611c48;
    int result = callback(trigger);
    if (result == 0) return;

    // Increment per-trigger activation counter
    int level_info = level_get_info_cached(DAT_80612614);
    int trigger_base = *(int*)(level_info + 0xBC);
    int trigger_index = (trigger - trigger_base) / 0x34;
    *(int*)(DAT_80611c28 + trigger_index * 4) += 1;
}


// ============================================================================
// Address: 0x80074924 | Name: entity_trigger_check_player
// Iterates all level triggers of type 1 (player-proximity).
// Checks if the current player matches the trigger's target entity,
// then calls entity_trigger_activate.
// ============================================================================
void entity_trigger_check_player(void) {
    int current_player = FUN_803826e0(); // get current player entity ID
    int level_info = level_get_info_cached(DAT_80612614);
    if (level_info == 0) return;

    int trigger_base = *(int*)(level_info + 0xBC);
    int trigger_count = *(int*)(level_info + 0xC0);

    for (int i = 0; i < trigger_count; i++) {
        int trigger = trigger_base + i * 0x34;
        // Only process type-1 triggers (player proximity)
        if (*(int*)(trigger + 0x24) != 1) continue;
        // Skip already-activated triggers
        if (*(int*)(DAT_80611c28 + i * 4) >= 1) continue;
        // Check if target entity matches current player
        int target_entity = FUN_8025c8e8(*(int*)(trigger + 0x28));
        if (current_player == target_entity) {
            entity_trigger_activate(trigger);
        }
    }

    FUN_8038272c();
}


// ============================================================================
// Address: 0x800749d0 | Name: entity_trigger_evaluate_all
// Iterates all level triggers and evaluates conditions by type:
//   Type 0: Zone-based (player in same zone as trigger)
//   Type 2: Item pickup (item is collected)
//   Type 3: Entity flag (entity has specific flag set)
//   Type 4: Room match (player in same room as trigger)
// Skips already-activated triggers (counter >= 1).
// ============================================================================
void entity_trigger_evaluate_all(void) {
    FUN_803826d8(); // begin trigger iteration

    int level_info = level_get_info_cached(DAT_80612614);
    if ((DAT_805015c0 & 0x20000000) != 0) { FUN_80382724(); return; } // skip in debug
    if (level_info == 0) { FUN_80382724(); return; }

    int trigger_base = *(int*)(level_info + 0xBC);
    if (trigger_base == 0) { FUN_80382724(); return; }

    int trigger_count = *(int*)(level_info + 0xC0);

    for (int i = 0; i < trigger_count; i++) {
        // Skip already-activated
        if (*(int*)(DAT_80611c28 + i * 4) >= 1) continue;

        int trigger = trigger_base + i * 0x34;
        int trigger_type = *(int*)(trigger + 0x24);

        switch (trigger_type) {
            case 0: {
                // Zone-based: check if any player is in the trigger's zone
                for (int p = 0; p < DAT_80612610; p++) {
                    int player_data = *(int*)(DAT_80611d78 + p * 0x141c + 0x14);
                    int player_camera = *(int*)(player_data + 0x7C);
                    int player_zone = *(int*)(player_camera + 0x678);
                    int trigger_zone = soundpad_find_zone_by_id(*(int*)(trigger + 0x28));
                    if (player_zone == trigger_zone) {
                        entity_trigger_activate(trigger);
                    }
                }
                break;
            }

            case 2: {
                // Item pickup: check if item has been collected
                int item_id = *(int*)(trigger + 0x28);
                int item = FUN_80086ab0(item_id);
                int collected = FUN_80086ba0(item);
                if (collected != 0) {
                    entity_trigger_activate(trigger);
                }
                break;
            }
            case 3: {
                // Entity flag: check if entity has flag set
                int entity_flag = *(uint*)(trigger + 0x28);
                int result = FUN_8013fe18(entity_flag);
                if (result != 0) {
                    entity_trigger_activate(trigger);
                }
                break;
            }
            case 4: {
                // Room match: check if any player is in the trigger's room
                for (int p = 0; p < DAT_80612610; p++) {
                    int player_data = *(int*)(DAT_80611d78 + p * 0x141c + 0x14);
                    if (*(int*)(player_data + 0x34) == *(int*)(trigger + 0x28)) {
                        entity_trigger_activate(trigger);
                    }
                }
                break;
            }
        }
    }

    FUN_80382724(); // end trigger iteration
}


// ============================================================================
// Address: 0x80074b70 | Name: entity_compute_bone_attachment
// Computes bone transform matrices for an entity attached to a parent via bone.
// Handles multiple attachment modes:
//   0x01 + flag 2: Left hand bone attachment
//   0x01 + flag 1: Right hand bone attachment
//   0x01 + flag 8: Custom offset bone attachment
//   0x10/0x20: Same as 0x01 variants (weapon/item slots)
//   0x40: Fixed offset attachment (head/eye camera)
// Applies position offset + rotation (Euler angles converted to matrix).
// Propagates bone matrices down to child bones via FUN_8012f95c.
// ============================================================================
void entity_compute_bone_attachment(void) {
    float local_matrix[16];   // afStack_98
    float work_matrix[22];    // afStack_58 (includes temp space)

    int entity = FUN_803826f0(); // get entity from iterator
    int render_data = *(int*)(entity + 4);
    int parent = **(int**)(entity + 0x78); // dereference parent pointer
    int parent_render = *(int*)(parent + 4);

    // Check if parent has "attached" flag
    if ((*(uint*)(parent + 0x28) & 0x200000) == 0) {
        // Parent not yet evaluated — just update child render if needed
        if (*(int*)(render_data + 0x10) != 0 && *(int*)(*(int*)(render_data + 4) + 4) != 0) {
            FUN_8012ef8c(); // copy parent matrices
        }
        FUN_8038273c();
        return;
    }

    // Mark this entity as attached
    *(uint*)(entity + 0x28) |= 0x200000;

    // Determine which bone matrix to use from parent
    int bone_index;
    if (parent_render == *(int*)(parent + 0x74)) {
        bone_index = *(int*)(entity + 0x12C); // direct bone index
    } else if ((*(uint*)(parent + 0x28) & 0x1000000) == 0 ||
               parent_render != *(int*)(parent + 0xA4)) {
        bone_index = *(int*)(entity + 0x130); // alternate bone index
    } else {
        bone_index = *(int*)(entity + 0x12C);
    }

    // Copy parent's bone matrix as starting point
    int parent_matrices = *(int*)(parent_render + 0x10);
    FUN_80130b90((float*)(parent_matrices + bone_index * 0x40),
                 *(float**)(render_data + 0x10));

    // Apply position + rotation offset
    float ox = *(float*)(entity + 0x134);
    float oy = *(float*)(entity + 0x138);
    float oz = *(float*)(entity + 0x13C);
    float rx = (3.14159f * *(float*)(entity + 0x140)) / 180.0f; // deg to rad
    float ry = (3.14159f * *(float*)(entity + 0x144)) / 180.0f;
    float rz = (3.14159f * *(float*)(entity + 0x148)) / 180.0f;
    FUN_8012f388((double)ox, (double)oy, (double)oz,
                 (double)rx, (double)ry, (double)rz, work_matrix); // build offset matrix
    FUN_80130bd4(work_matrix, *(float**)(render_data + 0x10),
                 *(float**)(render_data + 0x10)); // multiply into bone 0

    // Clear translation from offset matrix (rotation only for sub-bones)
    FUN_8012ef38((int)work_matrix);

    // Mode-specific additional transform
    int mode = *(int*)(entity + 0x20);
    uint attach_flags = *(uint*)(entity + 0x128);

    if (mode == 0x40) {
        // Head/eye camera: fixed rotation offset
        int* skel = *(int**)(*(int*)(entity + 4) + 4);
        float* bone_pos = (float*)skel[skel[0] * -0x30 + 0x1B];
        if (bone_pos == NULL) {
            // Use default head rotation
            FUN_8012f388(/*default angles*/);
        } else {
            FUN_8012f154(/*axis rotation*/);
            FUN_8012f534(-bone_pos[0], -bone_pos[1], -bone_pos[2]); // translate to bone origin
            FUN_80130bd4(local_matrix, work_matrix, work_matrix);
        }
    } else if (mode == 0x10 || mode == 0x20 || mode == 1) {
        // Weapon/item attachment with bone lookup
        if ((attach_flags & 8) != 0) {
            // Custom offset from data table
            uint bone_ref = FUN_801b6d24(*(int*)(entity + 0x8C));
            FUN_8012f154(/*custom rotation*/);
            if (bone_ref != 0xFFFFFFFF) {
                int* skel = *(int**)(*(int*)(entity + 4) + 4);
                float* pos = (float*)(skel[skel[0] * -0x30 + (bone_ref >> 16) * 0x30 + 0x1B]
                             + (bone_ref & 0xFFFF) * 0x18);
                FUN_8012f534(-pos[0], -pos[1], -pos[2]);
                FUN_80130bd4(local_matrix, work_matrix, work_matrix);
            }
        } else if ((attach_flags & 1) != 0) {
            // Right hand bone
            uint bone_ref = FUN_801b6d24(*(int*)(entity + 0x8C));
            FUN_8012f154(/*hand rotation*/);
            if (bone_ref != 0xFFFFFFFF) {
                int* skel = *(int**)(*(int*)(entity + 4) + 4);
                float* pos = (float*)(skel[skel[0] * -0x30 + (bone_ref >> 16) * 0x30 + 0x1B]
                             + (bone_ref & 0xFFFF) * 0x18);
                FUN_8012f534(-pos[0], -pos[1], -pos[2]);
                FUN_80130bd4(local_matrix, work_matrix, work_matrix);
            }
        } else if ((attach_flags & 2) != 0) {
            // Left hand bone
            uint bone_ref = FUN_801b6cf8(*(int*)(entity + 0x8C));
            FUN_8012f154(/*hand rotation*/);
            if (bone_ref != 0xFFFFFFFF) {
                int* skel = *(int**)(*(int*)(entity + 4) + 4);
                float* pos = (float*)(skel[skel[0] * -0x30 + (bone_ref >> 16) * 0x30 + 0x1B]
                             + (bone_ref & 0xFFFF) * 0x18);
                FUN_8012f534(-pos[0], -pos[1], -pos[2]);
                FUN_80130bd4(local_matrix, work_matrix, work_matrix);
            }
        }
    }

    // Apply work matrix to bone 0
    FUN_80130bd4(work_matrix, *(float**)(render_data + 0x10), *(float**)(render_data + 0x10));

    // Propagate to child bones
    int total_bones = *(int*)(*(int*)(render_data + 4) + 4);
    if (total_bones > 1) {
        FUN_8012f95c(*(int*)(render_data + 0x10) + 0x40,
                     *(int*)(render_data + 0x10), total_bones - 1);
    }

    FUN_8038273c();
}


// ============================================================================
// Address: 0x80075020 | Name: entity_update_bone_matrices
// Entry point for per-entity bone matrix computation. Determines whether
// entity needs full bone eval or just identity reset. For attached entities,
// dispatches to entity_compute_bone_attachment. For standalone entities,
// calls event_dispatch or custom callback for bone computation.
// ============================================================================
void entity_update_bone_matrices(void) {
    int entity = FUN_803826ec(); // get entity from iterator
    int render_data = *(int*)(entity + 4);

    if (*(int*)(entity + 0x20) == 0x100000) {
        // Special "static prop" type — mark as done + needs cleanup
        *(uint*)(entity + 0x28) |= 0x200008;
    } else if (*(float**)(render_data + 0x10) == NULL) {
        // No bone matrix buffer allocated — just mark done
        *(uint*)(entity + 0x28) |= 0x200000;
    } else if (*(int*)(entity + 0x78) == 0) {
        // No parent attachment — compute from scratch
        // Initialize bone 0 to identity
        **(float**)(render_data + 0x10) = FLOAT_80613fc0; // 1.0 at [0][0]

        // Call custom bone update callback or event
        if (*(void**)(entity + 0x15C) == NULL) {
            event_dispatch(entity, 2, 0); // generic bone update event
        } else {
            typedef void (*bone_callback_t)(int, int);
            bone_callback_t cb = *(bone_callback_t*)(entity + 0x15C);
            cb(entity, 0);
        }

        // Check if callback produced any transform (bone[0][0] still identity?)
        if (FLOAT_80613fc0 == **(float**)(render_data + 0x10)) {
            // No transform produced — set all bones to identity
            int bone_count = *(int*)(*(int*)(render_data + 4) + 4);
            int offset = 0;
            for (int i = 0; i < bone_count; i++) {
                FUN_8012ef38(*(int*)(render_data + 0x10) + offset);
                offset += 0x40;
            }
            *(uint*)(entity + 0x28) |= 0x200000;  // mark evaluated
            *(uint*)(entity + 0x28) &= ~0x08;     // clear matrices-invalid
        } else {
            *(uint*)(entity + 0x28) |= 0x08; // matrices valid, needs further processing
        }
    } else {
        // Has parent — compute via bone attachment
        entity_compute_bone_attachment();
        *(uint*)(entity + 0x28) |= 0x08;
    }

    FUN_80382738(); // end entity iteration
}
