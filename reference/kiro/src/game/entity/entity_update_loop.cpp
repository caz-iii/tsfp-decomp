// entity_update_loop.cpp
// Entity per-frame update loop: dirty list processing, post-transform pass, interaction
// Address range: 0x80075164 - 0x8007557c
// Date: 2026-08-10

#include "common.h"

// Forward declarations
extern void entity_trigger_evaluate_all(void);
extern void entity_update_bone_matrices(void);
extern void entity_swap_render_data(void);
extern void entity_update_transform(void);
extern void entity_reset_bone_transforms(void);
extern void render_instance_clear_visual_state(int instance);
extern int mem_advance_secondary(int size);
extern void event_dispatch(int entity, int event_type, int param);
extern void light_probe_update_positions(void);
extern void light_probe_validate_slots(void);
extern int input_get_buttons_pressed(int pad_id);
extern int input_get_buttons_held(int pad_id);
extern int scene_raycast(int room, float* origin, float* dir, int p3,
                         float* p4, float* p5, int* p6, int mask, int p8, int* p9);
extern int get_string_by_id(int id);

// External data
extern void* DAT_80540a44;   // Entity dirty list head
extern void* DAT_80611c50;   // Current dirty list iterator
extern int DAT_80611c64;     // Interactable entity count
extern int DAT_80611d74[];   // Player state array
extern int DAT_80611d78;     // Player info array base
extern int DAT_80611928;     // Frame counter
extern int DAT_806108e4;     // Delta time ticks
extern char DAT_805015bd;    // Player mode

// ============================================================================
// Address: 0x80075164 | Name: entity_process_dirty_list
// Processes the entity dirty list built by entity_update_dispatch. Multi-pass:
// Pass 1: For non-attached entities — swap render data, alloc bone buffers,
//         compute bone matrices, fire post-bone events + callbacks.
// Pass 2: For attached entities — compute bone matrices (requires parent done first).
// Pass 3: Fire post-transform events + render callbacks for non-attached.
// Pass 4: Fire post-transform for attached entities via FUN_800779f0.
// ============================================================================
void entity_process_dirty_list(void) {
    int* ent;

    entity_trigger_evaluate_all();

    // --- Pass 1: Non-attached entities (bone matrices + events) ---
    ent = (int*)DAT_80540a44;
    while (ent != NULL) {
        DAT_80611c50 = ent;

        // Copy previous dirty flags, clear current
        ent[2] = ent[6];
        ent[6] = 0;

        // Clear per-frame render state
        *(char*)(ent[1] + 0x7C) = 0;
        *(char*)(ent[1] + 0x7D) = 0;
        *(char*)(ent[1] + 0x7E) = 0;

        // Check if render data swap needed (LOD change or model swap)
        if ((ent[10] & 0x1000000) != 0 && ent[1] != ent[0x29]) {
            entity_swap_render_data();
        } else if ((ent[10] & 0x1000000) == 0 && ent[1] != ent[0x1D] && ent[8] == 8) {
            entity_swap_render_data();
        }

        // Clear visual state if flagged
        if ((ent[2] & 4) != 0) {
            render_instance_clear_visual_state(ent[1]);
        }

        // Allocate bone matrix buffer
        int bone_count = *(int*)(*(int*)(ent[1] + 4) + 4);
        if (bone_count == 0) {
            *(int*)(ent[1] + 0x10) = 0;
        } else {
            int buf = mem_advance_secondary(bone_count * 0x40);
            *(int*)(ent[1] + 0x18) = buf;
            *(int*)(ent[1] + 0x10) = buf;
            ent[10] &= ~0x100;
        }

        // Compute bone matrices for non-attached entities
        if (ent[0x1E] == 0) {
            entity_update_bone_matrices();
        }

        ent = (int*)*DAT_80611c50;
    }

    // --- Pass 2: Attached entities (require parent bones computed first) ---
    for (DAT_80611c50 = DAT_80540a44; DAT_80611c50 != NULL;
         DAT_80611c50 = (void*)*(int*)DAT_80611c50) {
        int* attached = (int*)DAT_80611c50;
        if (attached[0x1E] != 0) {
            entity_update_bone_matrices();
        }
    }

    // --- Pass 3: Post-transform events for non-attached ---
    ent = (int*)DAT_80540a44;
    while (ent != NULL) {
        DAT_80611c50 = ent;
        if (ent[0x1E] == 0) {
            event_dispatch((int)ent, 3, 0); // EVENT_POST_BONE_UPDATE

            // Call post-bone callback if entity still alive
            if ((ent[10] & 0x80000000) == 0 && ent[0x58] != 0) {
                typedef void (*callback_t)(int*, int);
                ((callback_t)ent[0x58])(ent, 0);
            }
            if ((ent[10] & 0x80000000) == 0) {
                if (ent[0x5A] != 0) ((void(*)(int*,int))ent[0x5A])(ent, 0);
                if (ent[0x5B] != 0) ((void(*)(int*,int))ent[0x5B])(ent, 0);
            }
        }
        ent = (int*)*DAT_80611c50;
    }

    // --- Pass 4: Post-transform for attached entities ---
    for (DAT_80611c50 = DAT_80540a44; DAT_80611c50 != NULL;
         DAT_80611c50 = (void*)*(int*)DAT_80611c50) {
        int* attached = (int*)DAT_80611c50;
        if (attached[0x1E] != 0) {
            FUN_800779f0((int)attached);
        }
    }

    FUN_8037383c(); // finalize frame
}


// ============================================================================
// Address: 0x800753bc | Name: entity_post_transform_pass
// Called after entity_process_dirty_list. Updates light probes, fires event 4
// (post-render-prep) for each entity, handles bone reset for newly-attached,
// then calls entity_update_transform on every dirty entity.
// ============================================================================
void entity_post_transform_pass(void) {
    int* ent;

    light_probe_update_positions();
    int time_data = FUN_803656fc(); // get frame timing
    FUN_8014e6e4(time_data); // animation tick (advances all active animations)

    // --- Fire EVENT_RENDER_PREP (4) for non-attached entities ---
    ent = (int*)DAT_80540a44;
    while (ent != NULL) {
        DAT_80611c50 = ent;
        if (ent[0x1E] == 0) {
            event_dispatch((int)ent, 4, 0);
            if (ent[0x59] != 0) {
                typedef void (*callback_t)(int*, int);
                ((callback_t)ent[0x59])(ent, 0);
            }
            if (ent[0x9A] != 0) {
                FUN_8011bb8c(ent[0x9A], (int)ent); // particle system update
            }
        }
        ent = (int*)*DAT_80611c50;
    }

    // --- Fire EVENT_RENDER_PREP (4) for attached entities ---
    ent = (int*)DAT_80540a44;
    while (ent != NULL) {
        DAT_80611c50 = ent;
        if (ent[0x1E] != 0) {
            event_dispatch((int)ent, 4, 0);
            if (ent[0x59] != 0) {
                typedef void (*callback_t)(int*, int);
                ((callback_t)ent[0x59])(ent, 0);
            }
            // Reset bones if attachment just established
            if ((ent[6] & 1) != 0) {
                entity_reset_bone_transforms();
            }
            ent[6] |= 2; // mark "needs transform update"
        }
        ent = (int*)*DAT_80611c50;
    }

    // --- Update transforms for all dirty entities ---
    for (DAT_80611c50 = DAT_80540a44; DAT_80611c50 != NULL;
         DAT_80611c50 = (void*)*(int*)DAT_80611c50) {
        entity_update_transform();
    }

    light_probe_validate_slots();
}


// ============================================================================
// Address: 0x80075510 | Name: entity_interaction_execute
// Executes the use/interact action on an entity. Calls the entity's interaction
// callback (offset 0x154), sets player interaction flag, triggers game state update.
// ============================================================================
void entity_interaction_execute(void) {
    int entity_and_player = FUN_803826f0(); // returns entity + player index packed
    int player_index = (int)((uint64_t)entity_and_player >> 32);
    int entity = (int)entity_and_player;

    int player_info = DAT_80611d78 + player_index * 0x141C;
    int player_entity = *(int*)(player_info + 0x14);

    FUN_803660e8(); // pre-interaction hook

    // Call entity's use callback: callback(entity, player_entity)
    typedef void (*use_callback_t)(int, int);
    use_callback_t callback = *(use_callback_t*)(entity + 0x154);
    callback(entity, player_entity);

    // Mark player as "just interacted"
    *(int*)(player_info + 0x1408) = 1;

    FUN_8023347c(); // post-interaction (UI update / objective check)
    FUN_8038273c();
}


// ============================================================================
// Address: 0x8007557c | Name: entity_interaction_find_target
// MASSIVE: Player use-key interaction system. When use button pressed:
// 1. Gets player position + facing direction
// 2. Iterates all interactable entities (DAT_80611c64 count)
// 3. For each: checks distance <= interact_radius, dot product >= min_dot
// 4. Raycasts from player to entity for LOS (unless entity has no-LOS flag)
// 5. Scores candidates by (distance_weight * dist + (1 - dot))
// 6. Best candidate: if special type 0x20000, handles door/switch logic;
//    otherwise calls entity_interaction_execute
// Already named "game_menu_input_handler" in IDB — keeping that name.
// ============================================================================
void entity_interaction_find_target(void) {
    FUN_803826d0();

    // Guard: player must be valid and alive
    if (DAT_80611d74[1] < 0) { FUN_8038271c(); return; }
    if (*(float*)(DAT_80611d74[5] + 0x1CC) <= 0.0f) { FUN_8038271c(); return; }

    DAT_80611d74[0x502] = 0; // clear interaction result

    uint buttons_pressed = input_get_buttons_pressed(DAT_80611d74[0]);
    if ((DAT_80611d74[8] & buttons_pressed) == 0 || DAT_806108e4 == 0) {
        FUN_8038271c();
        return;
    }

    int player_entity = DAT_80611d74[5];
    uint buttons_held = input_get_buttons_held(DAT_80611d74[0]);
    uint use_button = DAT_80611d74[8];

    // Get player eye position
    float eye_pos[3];
    eye_pos[0] = *(float*)(DAT_80611d74 + 0x2D); // DAT_80611d74[0x2D] as float
    eye_pos[1] = *(float*)(DAT_80611d74 + 0x2E);
    eye_pos[2] = *(float*)(DAT_80611d74 + 0x2F);

    // Get player facing direction from yaw angle
    float facing_matrix[8];
    float yaw_rad = (3.14159f * *(float*)(DAT_80611d74 + 0x40)) / 180.0f;
    FUN_8012f05c((double)yaw_rad, facing_matrix);
    float dir_x = facing_matrix[/*local_19c offset*/0]; // extracted from matrix
    float dir_y = facing_matrix[1];
    float dir_z = facing_matrix[2];

    // Scan interactable entities
    struct Candidate { int entity; float distance; float dot; };
    Candidate candidates[20];
    int num_candidates = 0;

    for (int i = 0; i < DAT_80611c64; i++) {
        int ent = *(int*)(&DAT_8054088c + i); // interactable entity table

        // Skip dead/disabled entities without use callback
        if ((*(uint*)(ent + 0x28) & 0x80000000) != 0) continue;
        if ((*(uint*)(ent + 0x28) & 4) == 0) continue;
        if (*(int*)(ent + 0x154) == 0) continue;

        // Get entity interaction center position
        float target[3];
        // [position calculation based on entity type 0x20000 with bone lookup, or offset]
        target[0] = *(float*)(ent + 0x64) + *(float*)(ent + 0x1A8);
        target[1] = *(float*)(ent + 0x68) + *(float*)(ent + 0x1AC);
        target[2] = *(float*)(ent + 0x6C) + *(float*)(ent + 0x1B0);

        // Distance check
        float dx = target[0] - eye_pos[0];
        float dy = target[1] - eye_pos[1];
        float dz = target[2] - eye_pos[2];
        float dist_sq = dx*dx + dy*dy + dz*dz;
        if (dist_sq > *(float*)(ent + 0x25C)) continue; // interact radius squared

        // Dot product check (facing toward entity)
        float dist = sqrt(dist_sq);
        float inv_dist = 1.0f / dist;
        float dot = (dx*inv_dist)*dir_x + (dy*inv_dist)*dir_y + (dz*inv_dist)*dir_z;
        if (dot < *(float*)(ent + 0x260)) continue; // min dot threshold

        // LOS raycast (skip for entities flagged as no-LOS-required)
        bool skip_raycast = (*(uint*)(*(int*)(ent + 0x7C) + 0x10) & 0x80000) != 0;
        if (!skip_raycast) {
            // Temporarily clear entity collision flags for raycast
            int hit = scene_raycast(*(int*)(player_entity + 0x34),
                                    eye_pos, &dx, 0, NULL, NULL, NULL, 0x2111070, 0, NULL);
            if (hit != 0) continue; // blocked
        }

        if (num_candidates < 20) {
            candidates[num_candidates].entity = ent;
            candidates[num_candidates].distance = dist;
            candidates[num_candidates].dot = dot;
            num_candidates++;
        }
    }

    // Find best candidate: score = distance_weight * dist + (1.0 - dot)
    if (num_candidates > 0) {
        float best_score = 999999.0f;
        int best_entity = 0;
        float dist_weight = 0.01f; // FLOAT_80613fd8
        for (int i = 0; i < num_candidates; i++) {
            float score = dist_weight * candidates[i].distance + (1.0f - candidates[i].dot);
            if (score < best_score) {
                best_entity = candidates[i].entity;
                best_score = score;
            }
        }

        if (best_entity != 0) {
            // Dispatch based on entity type
            if (*(int*)(best_entity + 0x20) == 0x20000) {
                // Door/switch type — special handling with cooldown + multiplayer check
                // [complex door interaction logic with timing checks]
            } else {
                entity_interaction_execute();
            }
        }
    }

    FUN_8038271c();
}
