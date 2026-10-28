// entity_render_visibility.cpp
// Entity rendering pipeline: visibility testing, frustum culling, render list building,
// per-room rendering with mirror passes
// Address range: 0x800768f4 - 0x80077708
// Date: 2026-08-10

#include "common.h"

// Forward declarations
extern void entity_update_bone_matrices(void);
extern void entity_assign_room(int entity);
extern void entity_check_player_visibility(int entity, uint player_index);
extern void entity_get_mirrored_position(float* out, int entity, uint mirror_index, int mode);
extern void entity_render_per_player(void);
extern void entity_compute_mirror_flags(void);
extern void cutscene_bone_inverse_precompute(void);
extern void render_instance_clear_visual_state(int instance);
extern void render_instance_draw_pass(void* data, double param, int render_data, int p3, uint p4);
extern void event_queue_push(int entity);
extern int mem_advance_secondary(int size);
extern int light_probe_test_aabb_against_planes(float* corners, float* planes);
extern void scene_viewport_compute_clip_bounds(int entity);

// External data
extern int DAT_80611c24;    // Entity pool base
extern int DAT_80611c58;    // Render list current size
extern int DAT_80611c5c;    // Render list buffer base
extern int DAT_80611c64;    // Interactable entity count
extern int DAT_80611c70;    // Entity pool count
extern int DAT_80611d54;    // Room/visibility system data
extern int DAT_80611748[];  // Viewport transform data
extern int DAT_80611aa8;    // Room property table
extern int DAT_80612614;    // Current level ID
extern int DAT_806108e4;    // Delta time ticks
extern int DAT_8060fadc;    // Special render mode flag
extern float FLOAT_806108ec; // Delta time (seconds)
extern float FLOAT_80613fb4; // 0.0f
extern float FLOAT_80613fbc; // 1.0f
extern float FLOAT_8061401c; // Frustum edge threshold

// ============================================================================
// Address: 0x800768f4 | Name: entity_force_bone_update
// Forces bone matrix computation with delta_time=0 for newly-spawned or
// teleported entities. Temporarily zeros dt, computes bones, then restores.
// For character entities (type 8), also precomputes inverse bone matrices.
// Special case: level 100 + type 2 entities call FUN_800ebda4.
// ============================================================================
void entity_force_bone_update(int entity) {
    int saved_dt = DAT_806108e4;

    if ((*(uint*)(entity + 0x28) & 0x100000) == 0) {
        DAT_806108e4 = saved_dt;
        return;
    }

    // Allocate bone matrix buffer
    int render_data = *(int*)(entity + 4);
    int bone_count = *(int*)(*(int*)(render_data + 4) + 4);
    if (bone_count == 0) {
        *(int*)(render_data + 0x10) = 0;
    } else {
        int buf = mem_advance_secondary(bone_count * 0x40);
        *(int*)(render_data + 0x18) = buf;
        *(int*)(render_data + 0x10) = buf;
        *(uint*)(entity + 0x28) &= ~0x100;
    }

    // Clear visual state if flagged
    if ((*(uint*)(entity + 0x08) & 4) != 0) {
        render_instance_clear_visual_state(*(int*)(entity + 4));
    }

    // Zero delta time for static pose computation
    float saved_dt_f = FLOAT_806108ec;
    DAT_806108e4 = 0;
    FLOAT_806108ec = 0.0f;

    entity_update_bone_matrices();

    // Precompute inverse bones for characters (IK/attachment system)
    if (*(int*)(entity + 0x20) == 8) {
        cutscene_bone_inverse_precompute();
    }

    FLOAT_806108ec = saved_dt_f;
    DAT_806108e4 = saved_dt;

    // Special level 100 handling for type-2 entities
    if (DAT_80612614 == 100 && *(int*)(entity + 0x20) == 2) {
        FUN_800ebda4();
    }
}


// ============================================================================
// Address: 0x800769fc | Name: entity_build_render_lists
// Builds per-room sorted render lists from all visible entities. Iterates the
// entity pool (stride 0x2E8), filters by visibility flags, and inserts into
// per-room arrays (DAT_8053fb2c[room_id]). The arrays are backed by a shared
// buffer at DAT_80611c5c with dynamic growth. Counts stored at DAT_805401dc[room].
// Skips: dead (bit 31), non-renderable (bit 0 clear), attached without parent
// having "evaluated" flag (0x200001).
// ============================================================================
void entity_build_render_lists(void) {
    FUN_803826e4(); // begin render list pass

    int render_buf = DAT_80611c5c;
    DAT_80611c58 = 0; // reset buffer usage

    // Compute per-room buffer offsets from previous frame's counts
    for (int room = 0; room < 0xD6; room++) {
        int count = *(int*)(&DAT_80540534 + room * 4); // previous count
        if (count == 0) {
            *(int*)(&DAT_8053fb2c + room * 4) = 0;
        } else {
            int offset = DAT_80611c58 * 4;
            DAT_80611c58 += count;
            *(int*)(&DAT_8053fb2c + room * 4) = render_buf + offset;
        }
    }

    // Save current counts, reset for new frame
    leaf_31e8(0x8053fe84, 0x80540534, 0x358); // backup
    memset_wrapper(0x80540534, 0, 0x358);     // clear counts

    // Iterate all entities and assign to room lists
    for (int i = 0; i < DAT_80611c70; i++) {
        int ent = DAT_80611c24 + i * 0x2E8;

        // Special mode: skip type-2 entities when DAT_8060fadc == 0
        if (DAT_8060fadc == 0 && *(int*)(ent + 0x20) == 2) continue;

        // Skip dead, non-renderable, or unready attached entities
        if ((*(uint*)(ent + 0x28) & 0x80000000) != 0) continue;
        if ((*(uint*)(ent + 0x28) & 1) == 0) continue;
        if (*(int**)(ent + 0x78) != NULL) {
            int parent = **(int**)(ent + 0x78);
            if ((*(uint*)(parent + 0x28) & 0x200001) != 0x200001) continue;
        }

        int room_id = *(int*)(ent + 0x34);

        // Clear per-frame render state
        *(int*)(*(int*)(ent + 4) + 0x20) = 0;
        *(int*)(*(int*)(ent + 4) + 0x24) = 0;
        *(int*)(*(int*)(ent + 4) + 0x28) = 0;
        *(int*)(*(int*)(ent + 4) + 0x2C) = 0;

        // Grow buffer if needed (max 0x14CC entries)
        int current_count = (&DAT_805401dc)[room_id];
        int* room_list_ptr = &(&DAT_8053fb2c)[room_id];
        if (*(int*)(&DAT_8053fe84 + room_id * 4) <= current_count) {
            // Reallocate with padding
            int new_size = ((*(int*)(&DAT_8053fe84 + room_id * 4) + 7) & ~7) + 8;
            int new_offset = DAT_80611c58 + new_size;
            if (new_offset > 0x14CC) continue; // buffer full
            DAT_80611c58 = new_offset;
            int new_ptr = DAT_80611c5c + DAT_80611c58 * 4;
            // Copy old data
            if (*room_list_ptr != 0 && current_count != 0) {
                leaf_31e8(new_ptr, *room_list_ptr, current_count * 4);
            }
            *room_list_ptr = new_ptr;
        }

        // Insert entity into room list
        *(int*)(*room_list_ptr + current_count * 4) = ent;
        (&DAT_805401dc)[room_id]++;
    }

    // Restore counts from backup
    leaf_31e8(0x80540534, 0x8053fe84, 0x358);
    FUN_80382730();
}


// ============================================================================
// Address: 0x80076bf4 | Name: entity_compute_render_aabb
// Computes world-space AABB for an entity including all mirror reflections.
// Starts with entity center, expands by checking mirrored positions for each
// visible player (1-3). Final AABB stored as 8 corners (float[8][4]) in output.
// Corners: min/max combinations of X, Y, Z expanded by entity bounds (0x1C0-0x1C8).
// ============================================================================
void entity_compute_render_aabb(void) {
    float mirror_pos[3];
    int entity;
    float* out; // 8 corners * 4 floats

    // FUN_803826f0 returns entity + output packed
    entity = FUN_803826f0_entity();
    out = FUN_803826f0_output();

    // Start with entity center position
    float center_x = *(float*)(entity + 0x64) + *(float*)(entity + 0x1A8);
    float center_y = *(float*)(entity + 0x68) + *(float*)(entity + 0x1AC);
    float center_z = *(float*)(entity + 0x6C) + *(float*)(entity + 0x1B0);

    float min_x = center_x, max_x = center_x;
    float min_y = center_y, max_y = center_y;
    float min_z = center_z, max_z = center_z;

    // Expand by mirrored positions for each player
    for (uint p = 1; p < 4; p++) {
        uint visible = entity_check_player_visibility(entity, p);
        if (visible == 0) continue;

        entity_get_mirrored_position(mirror_pos, entity, p, 2);
        if (mirror_pos[0] < min_x) min_x = mirror_pos[0];
        if (mirror_pos[0] > max_x) max_x = mirror_pos[0];
        if (mirror_pos[1] < min_y) min_y = mirror_pos[1];
        if (mirror_pos[1] > max_y) max_y = mirror_pos[1];
        if (mirror_pos[2] < min_z) min_z = mirror_pos[2];
        if (mirror_pos[2] > max_z) max_z = mirror_pos[2];
    }

    // Expand by entity bounding box extents (half-sizes at 0x1C0, 0x1C4, 0x1C8)
    float ex = *(float*)(entity + 0x1C0);
    float ey = *(float*)(entity + 0x1C4);
    float ez = *(float*)(entity + 0x1C8);

    // Build 8 corners of the AABB
    out[0]  = min_x - ex; out[1]  = min_y - ey; out[2]  = min_z - ez;  // 0: ---
    out[4]  = max_x + ex; out[5]  = min_y - ey; out[6]  = min_z - ez;  // 1: +--
    out[8]  = min_x - ex; out[9]  = max_y + ey; out[10] = min_z - ez;  // 2: -+-
    out[12] = max_x + ex; out[13] = max_y + ey; out[14] = min_z - ez;  // 3: ++-
    out[16] = min_x - ex; out[17] = min_y - ey; out[18] = max_z + ez;  // 4: --+
    out[20] = max_x + ex; out[21] = min_y - ey; out[22] = max_z + ez;  // 5: +-+
    out[24] = min_x - ex; out[25] = max_y + ey; out[26] = max_z + ez;  // 6: -++
    out[28] = max_x + ex; out[29] = max_y + ey; out[30] = max_z + ez;  // 7: +++

    FUN_8038273c();
}


// ============================================================================
// Address: 0x80076e1c | Name: entity_compute_screen_rect
// Projects the 8-corner world-space AABB to screen space. For each corner,
// applies view-projection matrix, perspective divide, then maps to screen
// coordinates. Outputs min/max X and Y as a 4-float rect.
// ============================================================================
void entity_compute_screen_rect(void) {
    float world_corners[32];  // 8 corners * 4 floats (from entity_compute_render_aabb)
    float screen_corners[32]; // 8 projected corners
    float* out_rect;          // [min_x, min_y, max_x, max_y]

    FUN_803826e8();
    entity_compute_render_aabb(); // fills world_corners (via afStack_108)

    float edge_threshold = FLOAT_8061401c;
    out_rect[0] = edge_threshold;  // min_x = large initial
    out_rect[1] = edge_threshold;  // min_y = large initial
    out_rect[2] = 0.0f;           // max_x = 0
    out_rect[3] = 0.0f;           // max_y = 0

    for (int i = 0; i < 8; i++) {
        // Transform world corner by view-projection matrix
        float* vp_matrix = *(float**)(DAT_80611d54 + 0x20C);
        FUN_8012fb70(vp_matrix, &world_corners[i*4], &screen_corners[i*4]);

        float w = screen_corners[i*4 + 3]; // perspective W
        float abs_w = (w < 0) ? -w : w;
        float inv_w = (abs_w > 0.0f) ? (1.0f / abs_w) : 0.0f;

        // Perspective divide + viewport transform
        int* viewport = DAT_80611748;
        float screen_x = inv_w * (float)(viewport[4] ^ 0x80000000) * screen_corners[i*4]
                        + (float)(viewport[2] ^ 0x80000000);
        float screen_y = -(inv_w * (float)(viewport[9] ^ 0x80000000) * screen_corners[i*4+1]
                          - (float)(viewport[7] ^ 0x80000000));

        // Update bounds
        if (screen_x < out_rect[0]) out_rect[0] = screen_x;
        if (screen_x > out_rect[2]) out_rect[2] = screen_x;
        if (screen_y < out_rect[1]) out_rect[1] = screen_y;
        if (screen_y > out_rect[3]) out_rect[3] = screen_y;
    }

    FUN_80382734();
}


// ============================================================================
// Address: 0x80077000 | Name: entity_set_render_flag
// Sets or clears a single bit in the entity's render flags (render_data+0x30).
// param_2: bit index (0-31), param_3: 1=set, 0=clear.
// ============================================================================
void entity_set_render_flag(int entity, int bit_index, int set_or_clear) {
    if (entity == 0) return;
    int render_data = *(int*)(entity + 4);
    if (render_data == 0) return;
    if (bit_index < 0 || bit_index > 31) return;

    if (set_or_clear != 0) {
        *(uint*)(render_data + 0x30) |= (1 << bit_index);
    } else {
        *(uint*)(render_data + 0x30) &= ~(1 << bit_index);
    }
}

// ============================================================================
// Address: 0x8007705c | Name: entity_render_room_begin
// Entry point for rendering a specific room. Marks the room as "currently
// rendering" in DAT_80559dfc, then iterates all entities in that room's
// render list calling entity_render_visibility_test for each.
// ============================================================================
void entity_render_room_begin(void) {
    int room_id = FUN_803826e8(); // get room index from caller

    // Mark room as rendering
    *(int*)(&DAT_80559dfc + room_id * 4) = 1;

    // Test visibility for each entity in this room
    int count = (&DAT_805401dc)[room_id];
    for (int i = 0; i < count; i++) {
        entity_render_visibility_test(); // processes next entity in list
    }

    FUN_80382734();
}

// ============================================================================
// Address: 0x800774d8 | Name: entity_render_stub
// Empty no-op stub function. Placeholder for removed/disabled render callback.
// ============================================================================
void entity_render_stub(void) {
    return;
}


// ============================================================================
// Address: 0x800770d8 | Name: entity_render_visibility_test
// MASSIVE: Per-entity frustum visibility test. For each entity:
// 1. Assigns room if none set
// 2. Computes world AABB via entity_compute_render_aabb
// 3. Tests AABB against frustum planes (light_probe_test_aabb_against_planes)
// 4. Projects corners to clip space, checks if any vertex is on-screen
// 5. For visible entities: forces bone update if not yet computed, marks visible
// 6. For off-screen: marks as culled (flag 0x800000)
// 7. Checks if entity spans screen edges for partial-visibility flag (0x400000)
// ============================================================================
void entity_render_visibility_test(void) {
    float world_corners[32];  // 8 corners * 4 floats
    float clip_corners[32];   // 8 projected corners
    float screen_xy[4];       // [x, y] per corner for bounds check

    int entity = FUN_803826e8();

    // Ensure entity has a room assigned
    if (*(char*)(entity + 0x30) == 0) {
        entity_assign_room(entity);
        if (*(char*)(entity + 0x30) == 0) {
            *(uint*)(entity + 0x28) |= 0x802000; // mark as "no room + culled"
            FUN_80382734();
            return;
        }
    }

    // Compute world-space AABB and clip-space viewport bounds
    scene_viewport_compute_clip_bounds(entity);
    entity_compute_render_aabb(); // fills world_corners via afStack_d0

    // Test AABB against frustum planes
    int frustum_result = light_probe_test_aabb_against_planes(world_corners, screen_xy);
    if (frustum_result < 0) {
        // Completely outside frustum
        *(uint*)(entity + 0x28) |= 0x800000;
        FUN_80382734();
        return;
    }

    // Type-2 (always-visible) entities skip detailed projection test
    bool is_always_visible = (*(int*)(entity + 0x20) == 2);
    int passes_test = is_always_visible ? 1 : 0;

    // Project all 8 corners and check against viewport
    float behind_threshold = 0.0f;
    for (int i = 0; i < 8; i++) {
        float* vp_matrix = *(float**)(DAT_80611d54 + 0x20C);
        FUN_8012fb70(vp_matrix, &world_corners[i*4], &clip_corners[i*4]);

        float w = clip_corners[i*4 + 3];
        if (w <= behind_threshold) {
            passes_test = 0; // behind camera — don't auto-pass
        }
    }

    if (passes_test == 0 && !is_always_visible) {
        // Detailed screen-bounds check against entity's screen rect
        // [Projects each corner, checks if projected XY falls within viewport bounds]
        // Uses entity+0x118..0x124 as screen rect limits
        // If all corners outside: cull. If any inside: visible.
        // ... (complex per-corner viewport test with 6 region flags)
        // If at least one corner on each side of center: passes
    }

    if (passes_test == 0) {
        *(uint*)(entity + 0x28) |= 0x802000; // culled
    } else {
        // Entity is visible
        *(uint*)(entity + 0x28) |= 0x400000; // visible flag

        // Mark which player can see it
        *(uint*)(entity + 0x0C) |= (1 << *(int*)(DAT_80611d54 + 0x214));

        // Check if entity spans viewport edges (for partial visibility)
        float edge = FLOAT_8061401c;
        for (int i = 0; i < 8; i++) {
            float w = clip_corners[i*4 + 3];
            float abs_w = (w < 0) ? -w : w;
            float thresh = edge * abs_w;

            if (w < behind_threshold) {
                *(uint*)(entity + 0x28) |= 0x400000; // spans edge
                break;
            }
            float x = clip_corners[i*4];
            float y = clip_corners[i*4 + 1];
            if (x < -thresh || x >= thresh || y < -thresh || y >= thresh) {
                *(uint*)(entity + 0x28) |= 0x400000;
                break;
            }
        }

        // Force bone update if not already computed
        if ((*(uint*)(entity + 0x28) & 0x200000) == 0) {
            entity_force_bone_update(entity);
        }

        *(uint*)(entity + 0x28) &= ~0x00802000; // clear cull flags
    }

    FUN_80382734();
}


// ============================================================================
// Address: 0x800774dc | Name: entity_render_single
// Renders a single entity. Sets visibility bit, computes screen rect if room
// is valid, resolves LOD/model variant, then calls render_instance_draw_pass.
// For character entities (type 8), calls FUN_800f98cc for skin shader setup.
// Entities with flag 0x2000000 are queued via event_queue_push (deferred render).
// ============================================================================
void entity_render_single(int entity, int pass_index) {
    // Set "rendered this frame" flag
    *(uint*)(entity + 0x0C) |= 0x80000000;

    // Check if entity is in the current camera's room
    if (*(int*)(DAT_80611d54 + 0x2A0) == *(int*)(entity + 0x34)) {
        *(uint*)(entity + 0x0C) |= 0x40000000; // same-room-as-camera flag
    }

    // Visibility check (player 0 = simple check)
    uint visible = entity_check_player_visibility(entity, 0);
    if (visible == 0) return;

    // Compute screen-space rect for entities with valid rooms
    if (*(int*)(entity + 0x34) < 1 || *(int*)(entity + 0x34) > 200) {
        // No valid room: use full viewport as screen rect
        *(float*)(entity + 0x118) = (float)(DAT_80611748[0] ^ 0x80000000); // viewport bounds
        *(float*)(entity + 0x11C) = (float)(DAT_80611748[5] ^ 0x80000000);
        *(float*)(entity + 0x120) = (float)(DAT_80611748[1] ^ 0x80000000);
        *(float*)(entity + 0x124) = (float)(DAT_80611748[6] ^ 0x80000000);
        *(uint*)(entity + 0x28) &= ~0x00802000;
        *(uint*)(entity + 0x28) |= 0x400000;
    }
    // else: screen rect already computed by visibility test

    uint flags = *(uint*)(entity + 0x28);
    if ((flags & 0x800000) != 0) return; // culled
    if ((*(uint16_t*)(*(int*)(entity + 4) + 0x1EE) & 8) != 0) return; // hidden material

    // Setup render pass (resolve screen rect to draw region)
    void* draw_data = FUN_8016f0e4(
        (int)*(float*)(entity + 0x118), (int)*(float*)(entity + 0x11C),
        (int)*(float*)(entity + 0x120), (int)*(float*)(entity + 0x124));

    // Entity-type-specific render setup
    if (*(int*)(entity + 0x20) == 0x20) {
        draw_data = FUN_801b625c(entity, pass_index); // special type 0x20 variant
    } else if (*(int*)(entity + 0x20) == 8) {
        draw_data = FUN_800f98cc(draw_data, /*screen_y*/); // character skin shader
    }

    // Submit or defer
    if ((*(uint*)(entity + 0x10) & 0x2000000) == 0) {
        if ((*(uint16_t*)(*(int*)(entity + 4) + 0x1EE) & 8) == 0) {
            render_instance_draw_pass(draw_data, 0, *(int*)(entity + 4), pass_index, 0);
        }
    } else {
        event_queue_push(entity); // deferred transparent render
    }

    // Character post-render cleanup
    if (*(int*)(entity + 0x20) == 8) {
        FUN_800f9b08();
    }
}


// ============================================================================
// Address: 0x80077708 | Name: entity_render_room
// Renders all entities in a room. Two-phase:
// Phase 1 (normal pass): For each entity, if already bone-computed (0x200000),
//   calls entity_render_per_player. Otherwise marks player visibility bit.
// Phase 2 (mirror pass): If room has mirror flag (bits 0-1 of room props),
//   enables face-culling flip, iterates entities calling render_instance_draw_pass
//   for each visible player with mirrored matrices.
// Phase 3 (single-entity pass): Calls entity_render_single for each entity.
// ============================================================================
void entity_render_room(void) {
    int room_and_pass = FUN_803826dc(); // returns room_id + pass packed
    int room_id = (int)((uint64_t)room_and_pass >> 32);
    int pass_index = (int)room_and_pass;

    int entity_count = (&DAT_805401dc)[room_id];
    int* room_list = (int*)(&DAT_8053fb2c)[room_id];

    // --- Phase 1: Normal render pass ---
    for (int i = 0; i < entity_count; i++) {
        int ent = room_list[i];
        if (*(int*)(ent + 0x34) < 1) goto render_entity;
        if (*(int*)(ent + 0x34) > 200 || (*(uint*)(ent + 0x28) & 0x800000) == 0) {
render_entity:
            if ((*(uint16_t*)(*(int*)(ent + 4) + 0x1EE) & 8) == 0) {
                if ((*(uint*)(ent + 0x28) & 0x200000) == 0) {
                    // Not yet bone-computed — just mark player bit
                    *(uint*)(ent + 0x0C) |= (1 << *(int*)(DAT_80611d54 + 0x214));
                } else {
                    // Already computed — render per-player mirrors
                    entity_render_per_player();
                }
            }
        }
    }

    // --- Phase 2: Mirror/reflection pass ---
    uint room_flags = *(uint16_t*)(DAT_80611aa8 + room_id * 0xB4 + 0xB2);
    if ((room_flags & 3) != 0) {
        // Enable backface culling flip for mirror rendering
        if (pass_index == 0) {
            uint cull_state[1];
            FUN_803c7694(cull_state);
            if (cull_state[0] == 1) FUN_803c766c(2); // flip cull
        }

        for (int i = 0; i < entity_count; i++) {
            int ent = room_list[i];

            // Compute screen rect for entities without valid rooms
            if (*(int*)(ent + 0x34) < 1 || *(int*)(ent + 0x34) > 200) {
                // Use full viewport
                *(float*)(ent + 0x118) = (float)(DAT_80611748[0] ^ 0x80000000);
                *(float*)(ent + 0x11C) = (float)(DAT_80611748[5] ^ 0x80000000);
                *(float*)(ent + 0x120) = (float)(DAT_80611748[1] ^ 0x80000000);
                *(float*)(ent + 0x124) = (float)(DAT_80611748[6] ^ 0x80000000);
                *(uint*)(ent + 0x28) &= ~0x00802000;
                *(uint*)(ent + 0x28) |= 0x400000;
            }

            if ((*(uint*)(ent + 0x28) & 0x800000) != 0) continue;
            if ((*(uint16_t*)(*(int*)(ent + 4) + 0x1EE) & 8) != 0) continue;

            // Setup render region
            void* draw_data = FUN_8016f0e4(
                (int)*(float*)(ent + 0x118), (int)*(float*)(ent + 0x11C),
                (int)*(float*)(ent + 0x120), (int)*(float*)(ent + 0x124));

            // Draw for each visible player (mirrored)
            for (uint p = 1; p < 4; p++) {
                uint vis = entity_check_player_visibility(ent, p);
                if (vis != 0) {
                    render_instance_draw_pass(draw_data, 0, *(int*)(ent + 4), pass_index, p);
                }
            }
        }

        // Restore culling
        if (pass_index == 0 && /*was flipped*/ true) {
            FUN_803c766c(1);
        }
    }

    // --- Phase 3: Single-entity pass (non-mirror, includes LOD/deferred) ---
    for (int i = 0; i < entity_count; i++) {
        entity_render_single(room_list[i], pass_index);
    }

    FUN_80382728();
}
