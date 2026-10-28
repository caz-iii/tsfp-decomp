// entity_mirror_render.cpp
// Entity mirror/reflection system + per-player rendering pipeline
// Address range: 0x80075bd0 - 0x8007666c
// Date: 2026-08-10

#include "common.h"

// Forward declarations
extern int mem_advance_secondary(int size);
extern void light_probe_entity_lighting_update(void);
extern void cutscene_lighting_pass(void);
extern void soundpad_stub_nop(void);
extern int entity_is_lod_child(int entity);
extern int entity_is_attachment_parent(int entity);
extern int scene_raycast(int room, float* origin, float* dir, int p3,
                         float* p4, float* p5, int* p6, int mask, int p8, int* p9);
extern int render_entity_resolve_position(int out_matrix, int render_data);

// External data
extern int DAT_80611d54;   // Room/visibility data
extern int DAT_80611b04;   // Scene node table
extern int DAT_80611b14;   // Scene node count
extern int DAT_80611aa8;   // Room property table
extern int DAT_805015c0;   // Global flags
extern float FLOAT_80613fb4; // 0.0f
extern float FLOAT_80613fbc; // 1.0f
extern float FLOAT_80613fe8; // PI
extern float FLOAT_80613fec; // 180.0f
extern float FLOAT_80614008; // -1.0f
extern float FLOAT_8061400c; // large distance threshold
extern float FLOAT_80614010; // mirror fade speed
extern float FLOAT_806108ec; // delta time

// ============================================================================
// Address: 0x80075bd0 | Name: entity_get_mirrored_position
// Outputs entity position with mirror axis flip applied. param_4 controls mode:
//   0 = raw position (pos.xyz)
//   1 = offset only (entity center offset 0x1A8-0x1B0, negated on mirror axis)
//   2 = position + offset (full world-space mirrored center)
// Mirror direction (bits from 0x2B4): 0=none, 1=flip X, 2=flip Y, 3=flip Z
// ============================================================================
void entity_get_mirrored_position(float* out, int entity, uint mirror_index, int mode) {
    int axis_index = mirror_index - 1;
    uint mirror_dir = (*(int*)(entity + 0x2B4) >> ((mirror_index & 0x1F) << 1)) & 3;

    float pos_x = *(float*)(entity + 0x64);
    float pos_y = *(float*)(entity + 0x68);
    float pos_z = *(float*)(entity + 0x6C);
    float off_x = *(float*)(entity + 0x1A8);
    float off_y = *(float*)(entity + 0x1AC);
    float off_z = *(float*)(entity + 0x1B0);

    // Mirror plane position stored at entity + 0x2B8 + axis_index*4
    float mirror_pos = *(float*)(entity + axis_index * 4 + 0x2B8);

    switch (mirror_dir) {
        case 0: // No mirror
            if (mode == 0) { out[0] = pos_x; out[1] = pos_y; out[2] = pos_z; }
            else if (mode == 1) { out[0] = off_x; out[1] = off_y; out[2] = off_z; }
            else if (mode == 2) { out[0] = pos_x+off_x; out[1] = pos_y+off_y; out[2] = pos_z+off_z; }
            break;
        case 1: // Flip X
            float mx = mirror_pos - (pos_x - mirror_pos);
            if (mode == 0) { out[0] = mx; out[1] = pos_y; out[2] = pos_z; }
            else if (mode == 1) { out[0] = -off_x; out[1] = off_y; out[2] = off_z; }
            else if (mode == 2) { out[0] = mx-off_x; out[1] = pos_y+off_y; out[2] = pos_z+off_z; }
            break;
        case 2: // Flip Y
            float my = mirror_pos - (pos_y - mirror_pos);
            if (mode == 0) { out[0] = pos_x; out[1] = my; out[2] = pos_z; }
            else if (mode == 1) { out[0] = off_x; out[1] = -off_y; out[2] = off_z; }
            else if (mode == 2) { out[0] = pos_x+off_x; out[1] = my-off_y; out[2] = pos_z+off_z; }
            break;
        case 3: // Flip Z
            float mz = mirror_pos - (pos_z - mirror_pos);
            if (mode == 0) { out[0] = pos_x; out[1] = pos_y; out[2] = mz; }
            else if (mode == 1) { out[0] = off_x; out[1] = off_y; out[2] = -off_z; }
            else if (mode == 2) { out[0] = pos_x+off_x; out[1] = pos_y+off_y; out[2] = mz-off_z; }
            break;
    }
}


// ============================================================================
// Address: 0x80075e88 | Name: entity_get_center_position
// Gets entity center position (pos + offset). If entity has rotation mode 1,
// transforms offset into camera space via the global view matrix.
// ============================================================================
void entity_get_center_position(float* out, int entity) {
    float pos_y = *(float*)(entity + 0x68);
    float off_y = *(float*)(entity + 0x1AC);
    char rot_mode = *(char*)(entity + 0x31);
    float pos_z = *(float*)(entity + 0x6C);
    float off_z = *(float*)(entity + 0x1B0);

    out[0] = *(float*)(entity + 0x64) + *(float*)(entity + 0x1A8);
    out[1] = pos_y + off_y;
    out[2] = pos_z + off_z;

    // Rotation mode 1: transform into camera-local space
    if (rot_mode == 1) {
        float* view_matrix = *(float**)(DAT_80611d54 + 0x210);
        FUN_8012fa34(view_matrix, out); // matrix * point
    }
}


// ============================================================================
// Address: 0x80075ef0 | Name: entity_build_local_matrix
// Builds entity's local-to-world matrix including rotation, mirror scale,
// and position translation. Rotation mode:
//   0 = no rotation (identity)
//   1 = Y-axis only (billboard/sprite)
//   2 = full XYZ Euler rotation
//   3 = Y-axis if no parent with mode 2, else full XYZ
// Mirror direction applies a -1 scale on the appropriate axis.
// If entity has "flip scale" flag (0x20000000), applies -1 X scale.
// param_3: if nonzero, appends position translation.
// param_4: mirror_index for position mirroring.
// ============================================================================
void entity_build_local_matrix(void* out_ignored, void* out_ignored2,
                               int include_position, uint mirror_index) {
    float work_matrix[22];
    int entity;
    float* out_matrix;

    // FUN_803826e8 returns entity + output pointer packed
    // entity = low 32, out_matrix = high 32
    entity = FUN_803826e8_low();
    out_matrix = FUN_803826e8_high();

    uint mirror_dir = (*(int*)(entity + 0x2B4) >> ((mirror_index & 0x1F) << 1)) & 3;
    int mirror_axis_index = mirror_index - 1;

    char rot_mode = *(char*)(entity + 0x32);
    if (rot_mode == 0) {
        FUN_8012ef38((int)out_matrix); // identity matrix
    } else {
        // Build rotation matrix if rotation changed this frame
        if ((*(uint*)(entity + 0x28) & 0x1000) != 0) {
            *(uint*)(entity + 0x28) &= ~0x1000;
            float rx = (3.14159f * *(float*)(entity + 0xC0)) / 180.0f;
            float ry = (3.14159f * *(float*)(entity + 0x70)) / 180.0f;
            float rz = (3.14159f * *(float*)(entity + 0xC8)) / 180.0f;

            if (rot_mode == 2) {
                FUN_8012f154(rx, ry, rz, (float*)(entity + 0x274)); // full XYZ matrix
            } else if (rot_mode == 1) {
                FUN_8012f05c(ry, (float*)(entity + 0x274)); // Y-only
            } else if (rot_mode == 3) {
                // Y-only unless parent has mode 2
                int* parent = *(int**)(entity + 0x78);
                if (parent != NULL && *(char*)(*parent + 0x32) == 2) {
                    FUN_8012f154(rx, ry, rz, (float*)(entity + 0x274));
                } else {
                    FUN_8012f05c(ry, (float*)(entity + 0x274));
                }
            }

            // Apply flip scale if flagged
            if ((*(uint*)(entity + 0x28) & 0x20000000) != 0) {
                FUN_8012f62c(-1.0f, 1.0f, 1.0f); // scale(-1, 1, 1)
                FUN_80130bd4(work_matrix, (float*)(entity + 0x274), (float*)(entity + 0x274));
            }
        }

        // Copy stored rotation matrix to output
        FUN_80130b90((float*)(entity + 0x274), out_matrix);

        // Apply mirror scale on appropriate axis
        if (mirror_dir == 1) {
            FUN_8012f62c(-1.0f, 1.0f, 1.0f);    // mirror X
            FUN_80130bd4(out_matrix, work_matrix, out_matrix);
        } else if (mirror_dir == 2) {
            FUN_8012f62c(1.0f, -1.0f, 1.0f);    // mirror Y
            FUN_80130bd4(out_matrix, work_matrix, out_matrix);
        } else if (mirror_dir == 3) {
            FUN_8012f62c(1.0f, 1.0f, -1.0f);    // mirror Z
            FUN_80130bd4(out_matrix, work_matrix, out_matrix);
        }

        // Append position translation if requested
        if (include_position != 0) {
            float mirror_pos = *(float*)(entity + mirror_axis_index * 4 + 0x2B8);
            float px = *(float*)(entity + 0x64);
            float py = *(float*)(entity + 0x68);
            float pz = *(float*)(entity + 0x6C);

            switch (mirror_dir) {
                case 0: FUN_8012f588(px, py, pz, out_matrix); break;
                case 1: FUN_8012f588(mirror_pos-(px-mirror_pos), py, pz, out_matrix); break;
                case 2: FUN_8012f588(px, mirror_pos-(py-mirror_pos), pz, out_matrix); break;
                case 3: FUN_8012f588(px, py, mirror_pos-(pz-mirror_pos), out_matrix); break;
            }
        }
    }

    FUN_80382734();
}


// ============================================================================
// Address: 0x800761e0 | Name: entity_compute_mirror_flags
// Computes per-entity mirror/reflection flags. Detects:
// 1. Floor reflections via vertical raycast down (material flag 0x80)
// 2. Wall reflections via portal planes in the entity's room
// Stores mirror direction bits in entity+0x2B4 and mirror plane positions
// in entity+0x2B8..0x2C0. Handles fade-in for mirror appearance.
// For attached entities, copies parent's mirror state.
// ============================================================================
void entity_compute_mirror_flags(void) {
    int entity_raw = FUN_803826ec();
    int entity = entity_raw;

    // If attached, use parent entity for mirror computation
    if (*(int**)(entity_raw + 0x78) != NULL) {
        entity = **(int**)(entity_raw + 0x78);
    }

    if ((*(uint*)(entity + 0x2B4) & 0x80000000) == 0) return; // no movement since last check

    // Clear mirror flags (keep upper bits)
    *(uint*)(entity + 0x2B4) &= 0x7FFFFF00;
    int mirror_count = 0;

    // Skip if entity type has "no mirror" flag
    if ((*(uint*)(*(int*)(entity + 0x24) + 0x28) & 0x10000) != 0) goto copy_to_child;

    // Get room properties for entity's current room
    uint room_flags = *(uint16_t*)(DAT_80611aa8 + *(int*)(entity + 0x34) * 0xB4 + 0xB2);

    // --- Check floor reflection (flag bit 0) ---
    if ((room_flags & 1) != 0) {
        if (*(int*)(entity + 0x20) == 8) {
            // Character: use camera material for floor height
            int camera = *(int*)(*(int*)(entity + 0x7C) + 0xC00);
            if (camera != 0 && (*(uint16_t*)(camera + 8) & 0x80) != 0) {
                mirror_count = 1;
                *(uint*)(entity + 0x2B4) |= 8; // floor mirror (direction=2, slot 1)
                *(float*)(entity + 0x2B8) = *(float*)(*(int*)(entity + 0x7C) + 0xBFC);
            }
        } else {
            // Generic entity: raycast down to find floor
            float center[3];
            center[0] = *(float*)(entity + 0x64) + *(float*)(entity + 0x1A8);
            center[1] = *(float*)(entity + 0x68) + *(float*)(entity + 0x1AC);
            center[2] = *(float*)(entity + 0x6C) + *(float*)(entity + 0x1B0);

            int hit_material = 0;
            float floor_y = (float)FUN_80099aa4(/*large dist*/ 0, 0, center, &hit_material, NULL, 1);
            *(float*)(entity + 0x2B8) = floor_y;

            if (hit_material != 0 && (*(uint16_t*)(hit_material + 8) & 0x80) != 0) {
                *(uint*)(entity + 0x2B4) |= 8; // floor mirror flag
            }
            mirror_count = 1;
        }

        // Fade-in for floor mirrors on characters
        if (((*(uint*)(entity + 0x2B4) >> 2) & 3) == 2) {
            if (*(int*)(entity + 0x20) == 8 || entity_is_lod_child(entity) != 0
                || entity_is_attachment_parent(entity) != 0) {
                // Fade mirror alpha over time
                if (*(int*)(entity + 0x20) == 8 &&
                    (*(uint*)(*(int*)(entity + 0x7C) + 0x24) & 8) == 0) {
                    *(float*)(entity + 0x2C4) = 0.0f; // reset if not visible
                } else {
                    *(float*)(entity + 0x2C4) += FLOAT_80614010 * FLOAT_806108ec;
                    if (*(float*)(entity + 0x2C4) > FLOAT_8061400c) {
                        *(float*)(entity + 0x2C4) = FLOAT_8061400c;
                    }
                }
                *(float*)(entity + 0x2B8) -= *(float*)(entity + 0x2C4);
            }
        }
    }

    // --- Check wall reflections (flag bit 1) ---
    if ((room_flags & 2) != 0) {
        int byte_offset = mirror_count * 4; // 4 bytes per mirror slot
        // Iterate portal planes in entity's room
        int scene_node = *(int*)(DAT_80611b04 + *(int*)(entity + 0x34) * 4);
        int portal_count = *(int*)(scene_node + 0x1E4);
        int portal_list = *(int*)(scene_node + 500); // 0x1F4

        for (int i = 0; i < portal_count && mirror_count <= 2; i++) {
            int portal = *(int*)(portal_list + i * 4);
            if ((*(uint*)(portal + 4) & 0x40) == 0) continue; // not a mirror portal

            // Determine mirror axis: compare portal extents
            float dx_extent = *(float*)(portal + 0x1C) - *(float*)(portal + 0x10); // Z extent
            float dz_extent = *(float*)(portal + 0x14) - *(float*)(portal + 0x08); // X extent

            if (ABS(dx_extent) <= ABS(dz_extent)) {
                // Z-aligned portal: mirror on X axis
                float portal_x_min = *(float*)(portal + 0x20);
                float portal_x_max = *(float*)(portal + 0x28);
                if (*(float*)(entity + 0x64) >= portal_x_min &&
                    *(float*)(entity + 0x64) <= portal_x_max) {
                    *(uint*)(entity + 0x2B4) |= (3 << (mirror_count + 1) * 2); // dir=3 (Z flip)
                    *(float*)(entity + byte_offset + 0x2B8) = *(float*)(portal + 0x10);
                    mirror_count++;
                    byte_offset += 4;
                }
            } else {
                // X-aligned portal: mirror on Z axis
                float portal_z_min = *(float*)(portal + 0x24);
                float portal_z_max = *(float*)(portal + 0x2C);
                if (*(float*)(entity + 0x6C) >= portal_z_min &&
                    *(float*)(entity + 0x6C) <= portal_z_max) {
                    *(uint*)(entity + 0x2B4) |= (1 << (mirror_count + 1) * 2); // dir=1 (X flip)
                    *(float*)(entity + byte_offset + 0x2B8) = *(float*)(portal + 0x08);
                    mirror_count++;
                    byte_offset += 4;
                }
            }
        }
    }

copy_to_child:
    // Copy mirror state from parent entity to child (if attached)
    if (entity_raw != entity) {
        // memcpy entity+0x2B0..0x2C8 (36 bytes) from parent to child
        int* src = (int*)(entity + 0x2B0);
        int* dst = (int*)(entity_raw + 0x2B0);
        for (int i = 0; i < 9; i++) dst[i] = src[i];
    }

    FUN_80382738();
}


// ============================================================================
// Address: 0x8007654c | Name: entity_check_player_visibility
// Checks if an entity is visible to a specific player index. Returns 1 if visible.
// Handles special cases:
//   - Type 0x754 (HUD element): visible if "HUD visible" flag set
//   - Type 0x100000 (composite): delegates to child entity at +0x208
//   - param_2==0: simple flag check
//   - Otherwise: full room/mirror/visibility check including mirror direction
//     and water-submerged check (direction==2 + water flag)
// ============================================================================
uint entity_check_player_visibility(int entity, uint player_index) {
    // HUD element type
    if (*(short*)(entity + 0x94) == 0x754) {
        return (*(uint*)(entity + 0x2B4) & 0x40000000) ? 0 : 1;
    }

    // Composite entity: delegate to child
    if (*(int*)(entity + 0x20) == 0x100000 && *(int*)(entity + 0x208) != 0) {
        return entity_check_player_visibility(*(int*)(entity + 0x208), player_index);
    }

    // Simple check (player_index == 0): just check visibility flag
    if (player_index == 0) {
        return (*(uint*)(entity + 0x2B4) & 0x40000000) ? 0 : 1;
    }

    // Full visibility check
    if (*(char*)(entity + 0x31) != 0) return 0; // hidden by script
    if (*(char*)(entity + 0x32) == 0) return 0; // no rotation mode = invisible
    if (*(int*)(entity + 0x34) <= 0) return 0;  // no room assigned
    if (*(int*)(entity + 0x34) >= DAT_80611b14) return 0; // invalid room

    // If entity moved, recompute mirror flags
    if ((*(uint*)(entity + 0x2B4) & 0x80000000) != 0) {
        entity_compute_mirror_flags();
    }

    // Get mirror direction for this player
    uint mirror_dir = (*(uint*)(entity + 0x2B4) >> ((player_index & 0x1F) << 1)) & 3;

    // Water submerge check: if mirror=Y and water flag set, hide
    if (mirror_dir == 2 && (*(uint*)(DAT_80611d54 + 0x21C) & 4) != 0 &&
        (*(uint*)(entity + 0x2B4) & 0x40000000) != 0) {
        return 0;
    }

    return (mirror_dir != 0) ? 1 : 0;
}


// ============================================================================
// Address: 0x8007666c | Name: entity_render_per_player
// Per-player entity rendering setup. For each of up to 4 players:
// 1. Checks entity_check_player_visibility
// 2. Updates lighting if first time this frame
// 3. Handles LOD model swap for character entities
// 4. Allocates per-player bone matrix buffers
// 5. Builds mirrored local-to-world matrix via entity_build_local_matrix
// 6. Multiplies bone matrices by mirror transform
// 7. Applies camera projection via FUN_80130cd8
// ============================================================================
void entity_render_per_player(void) {
    float local_matrix[16];
    float mirror_matrix[16];
    float combined_matrix[16];
    float projected_matrix[16];
    float work_matrix[30]; // afStack_78

    int entity = FUN_803826d0();
    int* render_info = *(int**)(entity + 4);
    uint player_bit = 1 << *(int*)(DAT_80611d54 + 0x214); // current player mask
    int render_data = render_info[1]; // model/bone data

    // Skip if already processed for this player
    if ((*(uint16_t*)(entity + 0x14) & player_bit) != 0) {
        FUN_8038271c();
        return;
    }

    *(uint16_t*)(entity + 0x14) |= (uint16_t)player_bit;

    // Update lighting on first player pass
    if (*(int*)(render_info[1] + 8) != 0) {
        light_probe_entity_lighting_update();
        if ((DAT_805015c0 & 0x20000000) != 0) {
            cutscene_lighting_pass(); // debug mode lighting
        }
    }

    // LOD swap for character entities with alternate model
    if (*(int*)(*render_info + 0x20) == 8 && render_info[2] != 0) {
        if ((*(uint*)(*render_info + 0xA0) & (1 << *(int*)(DAT_80611d54 + 0x214))) != 0) {
            soundpad_stub_nop(); // placeholder for LOD transition sound
            render_info[1] = render_info[2]; // swap to LOD model
            render_info[4] = render_info[5]; // swap bone data
        }
    }

    // Allocate per-player bone matrix buffers
    for (uint p = 0; p < 4; p++) {
        uint visible = entity_check_player_visibility(entity, p);
        if (visible != 0) {
            int buf_size;
            if (render_info[4] == 0) {
                buf_size = 0x40; // single matrix
            } else {
                buf_size = *(int*)(render_info[1] + 4) * 0x40; // per-bone
            }
            int buf = mem_advance_secondary(buf_size);
            *(int*)((int)render_info + p * 4 + 0x20) = buf;
        }
    }

    // Build and apply per-player mirrored matrices
    for (uint p = 0; p < 4; p++) {
        uint visible = entity_check_player_visibility(entity, p);
        if (visible == 0) continue;

        float* out_matrices = *(float**)((int)render_info + p * 4 + 0x20);

        // Build entity local-to-world with mirror
        bool has_skin = render_entity_resolve_position((int)combined_matrix, render_data);

        if (*(char*)(entity + 0x31) == 0) {
            // Has rotation: build full mirrored matrix
            entity_build_local_matrix(work_matrix, entity, 1, p);
            FUN_80130bd4(work_matrix, *(float**)(DAT_80611d54 + 0x208), mirror_matrix);
        } else {
            // No rotation: use mirror matrix directly
            entity_build_local_matrix(mirror_matrix, entity, 1, p);
        }

        // Apply to bones
        int anim_data = render_info[4];
        if (anim_data == 0) {
            // Single matrix entity
            FUN_80130bd4(combined_matrix, mirror_matrix, local_matrix);
            FUN_80130cd8(local_matrix, out_matrices); // project
        } else {
            // Skinned entity: per-bone
            int bone_count = *(int*)(render_info[1] + 4);
            int offset = 0;
            for (int b = 0; b < bone_count; b++) {
                FUN_80130bd4((float*)(anim_data + offset), mirror_matrix, projected_matrix);
                if (has_skin) {
                    FUN_80130bd4(combined_matrix, projected_matrix, projected_matrix);
                }
                FUN_80130cd8(projected_matrix, (float*)((int)out_matrices + offset));
                offset += 0x40;
            }
        }
    }

    FUN_8038271c();
}
