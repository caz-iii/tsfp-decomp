// Address: 0x8008919c - 0x8008bb9c | Name: weapon_equipped_state | Date: 2026-08-11
// Weapon equipped state machine: pickup proximity, state transitions (idle/align/
// mounted/firing), bone matrix computation, visual updates (barrel spin, pump,
// ammo belt), aim deflection, turret tracking, and system reset.

#include "common.h"

extern u8 g_weapon_def_table[]; // 0x803ff010, 25 entries x 0xB4
#define WEAPON_DEF_SIZE 0xB4

// ============================================================================
// 0x8008919c | weapon_check_pickup_proximity
// Iterates all players, checks distance to weapon entity. If player is in
// range and pressing use button, calls weapon_pickup_equip. Otherwise shows
// button prompt via FUN_80122e9c. Only active in state 5 (pickup-ready).
// ============================================================================
void weapon_check_pickup_proximity(void) {
    int entity = FUN_803826e0();
    int weapon_data = *(int*)(entity + 0x7C);

    if (*(int*)(weapon_data + 0x10) != 5) {
        FUN_8038272c();
        return;
    }

    int player_offset = 0;
    int num_players = FUN_801c1714(); // get player count

    for (int i = 0; i < num_players; i++) {
        int player_entity = *(int*)(DAT_80611d90 + player_offset);
        int* player_slot = *(int**)(player_entity + 0x150);
        u32 buttons = input_get_buttons_held(*player_slot);

        // Check player is in weapon-use mode and game is active
        if (player_slot[0x45] == 2 && DAT_806108e4 > 0) {
            // Distance check
            float dy = *(float*)(player_entity + 0x68) - *(float*)(entity + 0x68);
            float dx = *(float*)(player_entity + 0x64) - *(float*)(entity + 0x64);
            float dz = *(float*)(player_entity + 0x6C) - *(float*)(entity + 0x6C);
            float dist_sq = dx * dx + dy * dy + dz * dz;

            if (dist_sq < FLOAT_806143ac) { // pickup radius squared
                // Compute aim angle and check within weapon's valid range
                float entity_pos[3];
                FUN_802405e8(entity, entity_pos);
                double angle = FUN_80390aa4(
                    (double)(entity_pos[0] - (float)player_slot[0x2A]),
                    (double)(entity_pos[2] - (float)player_slot[0x2C]));
                double angle_deg = (180.0f * (float)angle) / 3.14159f
                                   - *(float*)(entity + 0x70);
                double wrapped = FUN_80390324(
                    (double)((float)(angle_deg - 90.0) / 180.0f));
                float final_angle = -(180.0f * (float)wrapped - (float)angle_deg);

                // Check if angle is within weapon's yaw limits
                if (*(float*)(weapon_data + 0x3C) == *(float*)(weapon_data + 0x40) ||
                    (*(float*)(weapon_data + 0x3C) < final_angle &&
                     final_angle < *(float*)(weapon_data + 0x40))) {

                    if ((buttons & player_slot[8]) == 0) {
                        // Show "press use" prompt
                        FUN_80122e9c(i, player_slot[8], 8.4148e-42f);
                    }
                    else {
                        // Player pressed use: equip weapon
                        weapon_pickup_equip();
                    }
                    break;
                }
            }
        }
        player_offset += 0xEA0; // player stride
    }

    FUN_8038272c(); // stack restore
}

// ============================================================================
// 0x80089338 | weapon_equipped_tick
// MASSIVE per-frame weapon state machine while equipped. States:
//   5: Idle/waiting for player (yaw interpolation toward rest, attempt AI mount)
//   6: Aligning to mount point (yaw/pitch convergence with distance check)
//   7: Mounted/active (player input, fire rate, distance-based dismount)
//   0xD-0xE: Height transition (barrel raise/lower animation)
// Also handles:
//   - Fire rate cooldown decay
//   - Recoil damping (exponential decay per round)
//   - Barrel oscillation (yaw/pitch/roll sway from def velocities)
//   - Rumble feedback when firing
//   - Child entity (ammo belt) updates via FUN_8008a280
//   - Collision face bone rotation via collision_set_face_entry
// ============================================================================
void weapon_equipped_tick(void) {
    int entity = FUN_803826dc();
    u8* weapon_def = (u8*)weapon_get_def_from_entity(entity);
    int weapon_data = *(int*)(entity + 0x7C);

    if (DAT_806108e4 == 0) goto end_label;

    int state = *(int*)(weapon_data + 0x10);
    float dt = FLOAT_806108ec * (1.0f / (float)(DAT_806109c0 - DOUBLE_806143d0));

    // --- State 7: Mounted/active ---
    if (state == 7) {
        int mount_target = *(int*)(weapon_data + 0xD8);
        if (mount_target == 0) {
            // No mount target: revert to idle
            *(int*)(weapon_data + 0x10) = 5;
            *(u32*)(entity + 0x28) &= ~0x4000000;
            goto end_label;
        }
        int* player_slot = *(int**)(mount_target + 0x150);
        u32 buttons = 0;
        int player_data = *(int*)(mount_target + 0x7C);

        if (player_slot != NULL) {
            // Distance check: dismount if too far
            float dy = *(float*)(mount_target + 0x68) - *(float*)(entity + 0x68);
            float dx = *(float*)(mount_target + 0x64) - *(float*)(entity + 0x64);
            float dz = *(float*)(mount_target + 0x6C) - *(float*)(entity + 0x6C);
            if (dx*dx + dy*dy + dz*dz > FLOAT_806143ac) {
                // Too far: dismount
                *(int*)(weapon_data + 0x10) = 5;
                *(u32*)(entity + 0x28) &= ~0x4000000;
                FUN_800ab2a4((int)player_slot, 2);
                player_slot[0x387] = 0;
            }
            buttons = input_get_buttons_held(*player_slot);
        }

        // Check if player weapon slot is still valid
        if (*(int*)(player_data + 0xC14) == 0 ||
            (*(u32*)(player_data + 0x24) & 8) != 0) {
            // Weapon slot invalid: dismount
            *(int*)(weapon_data + 0x10) = 5;
            *(u32*)(entity + 0x28) &= ~0x4000000;
        }
        else if (player_slot == NULL || (buttons & player_slot[9]) == 0) {
            // Not pressing dismount: track aim
            if (player_slot == NULL) {
                // AI-controlled: interpolate toward target angle
                *(float*)(weapon_data + 0x24) =
                    *(float*)(mount_target + 0x70) - *(float*)(entity + 0x70);
                // Compute yaw from skeleton facing
                float target_yaw;
                FUN_801311d8(player_data + 0x9F0, &target_yaw, NULL);
                // Smooth yaw interpolation
                // ... (angle wrapping + lerp toward target)
            }
            else {
                // Player-controlled: show dismount prompt
                if (player_slot[1] >= 0) {
                    FUN_80122e9c(player_slot[1], player_slot[9], 7.78702e-42f);
                }
                // If player has HUD weapon and is on mounted gun type 0x18
                if ((*(u32*)(weapon_data + 0x8) & 0x8000) != 0 &&
                    player_slot[2] != 0 &&
                    **(int**)(player_slot[2] + 0x26C) == 0x18) {
                    weapon_turret_aim_track(); // FUN_8008b774
                }
            }
            *(u32*)(entity + 0x18) |= 2; // mark dirty
        }
        else {
            // Player pressed dismount button: detach
            *(int*)(weapon_data + 0x10) = 5;
            *(u32*)(entity + 0x28) &= ~0x4000000;
            FUN_800ab2a4((int)player_slot, 2);
            *(int*)(player_data + 0xC14) = 0;
            player_slot[0x387] = 0;
            *(int*)(weapon_data + 0xD8) = 0;
            // HUD update
            if ((*(u32*)(weapon_data + 0x8) & 0x8000) != 0 && player_slot[2] != 0) {
                FUN_8009d830(player_slot[2], 0, mount_target);
            }
            FUN_801a932c(player_data);
            weapon_play_fire_sound_3d(*(u16*)(entity + 0x94), 0, player_slot + 0x2A);
            FUN_8012cea8(player_slot[1]);
        }
    }

    // --- State 5: Idle/waiting ---
    else if (state == 5) {
        // Interpolate yaw toward rest position (FLOAT_806143b0)
        // ... (angle wrapping + smooth lerp)
        // Attempt pickup via weapon_check_pickup_proximity
        int picked_up = weapon_check_pickup_proximity();
        if (picked_up == 0) {
            // Search AI entity pool for nearby AI that wants to mount
            // ... (iterates entity pool DAT_80588610, checks distance + buttons)
        }
    }
    // --- State 6: Aligning to mount ---
    else if (state == 6) {
        int mount_target = *(int*)(weapon_data + 0xD8);
        if (mount_target == 0) {
            *(int*)(weapon_data + 0x10) = 5;
            *(u32*)(entity + 0x28) &= ~0x4000000;
            goto end_label;
        }
        // Smooth convergence of yaw/pitch toward target angles
        // When converged + position close enough: transition to state 7
        // ... (distance check, angle delta comparison, FUN_800ab2a4 on convergence)
    }
    // --- States 0xD, 0xE: Height transition ---
    else if (state > 0xC && state < 0xF) {
        // Animate weapon_data+0x194 (height) toward target
        float target_height = (state == 0xD) ? *(float*)(weapon_def + 0xC) : 0.0f;
        float speed = *(float*)(weapon_def + 0x10);
        float delta = target_height - *(float*)(weapon_data + 0x194);
        *(u32*)(entity + 0x18) |= 2;

        if (fabsf(delta) < speed * dt) {
            *(float*)(weapon_data + 0x194) = target_height;
            *(int*)(weapon_data + 0x10) = 8; // transition complete
        }
        else {
            float sign = (delta < 0) ? -1.0f : 1.0f;
            *(float*)(weapon_data + 0x194) += speed * dt * sign;
        }
    }

    // --- Fire rate cooldown decay ---
    if (*(float*)(weapon_data + 0x1C4) <= 0.0f) {
        FUN_800e4490(); // idle animation
    }
    else {
        *(float*)(weapon_data + 0x1C4) -= dt;
        if (*(float*)(weapon_data + 0x1C4) < 0.0f) {
            *(float*)(weapon_data + 0x1C4) = 0.0f;
            FUN_800e4490();
        }
        else if (*(float*)(weapon_data + 0x1C4) > FLOAT_806143c0) {
            FUN_800e442c(); // firing animation
        }
        // Fire rate interpolation on player slot
        float* slot_rate = (float*)(*(int*)(weapon_data + 0x6C) + 0x108);
        *slot_rate = -(*slot_rate * *(float*)(weapon_def + 0x9C) * dt - *slot_rate);
    }

    // --- Recoil decay (second pass) ---
    float* slot_rate2 = (float*)(*(int*)(weapon_data + 0x6C) + 0x108);
    *slot_rate2 = -(0.5f * *(float*)(weapon_def + 0x9C) * dt - *slot_rate2);
    if (*slot_rate2 < 0.0f) *slot_rate2 = 0.0f;

    // --- Ammo bob/oscillation (item_can_pickup-based) ---
    if (item_can_pickup(entity) != 0) {
        *(float*)(weapon_data + 0x1C8) += *(float*)(weapon_def + 0xA8) * dt;
        // Wrap via sin (used for floating weapon bob)
    }

    // --- Per-round recoil decay ---
    *(float*)(weapon_data + 0xB8) *= -(0.25f * FLOAT_806108ec - 1.0f);
    int num_rounds = *(int*)(weapon_def + 0x4C);
    for (int i = 0; i < num_rounds; i++) {
        float decay = -(*(float*)(weapon_def + 0x8) * FLOAT_806108ec - 1.0f);
        if (decay < 0.0f) decay = 0.0f;
        *(float*)(weapon_data + 0x198 + i * 4) *= decay;
    }

    // --- Linked round recoil propagation ---
    // (propagates max recoil between linked round slots)

    // --- Rumble feedback while firing ---
    if (FLOAT_806143c0 < *(float*)(weapon_data + 0x1B0) && DAT_806108e4 != 0 &&
        *(int*)(weapon_data + 0xD8) != 0) {
        int* player_slot2 = *(int**)(*(int*)(weapon_data + 0xD8) + 0x150);
        if (player_slot2 != NULL) {
            int pad_id = *player_slot2;
            // Check controller is connected and supports rumble
            if ((*(u16*)(*(int*)(DAT_80611d78 + pad_id * 0x141C + 0x1398) + 2) & 0x50) == 0x50) {
                int current = input_get_rumble_intensity(pad_id);
                int intensity = (int)(FLOAT_806143c4 * *(float*)(weapon_data + 0x1B0));
                input_set_rumble_intensity(pad_id, intensity + current);
            }
        }
    }

    // --- Fire animation speed decay ---
    if (*(int*)(weapon_data + 0x6C) != 0) {
        int anim_slot = *(int*)(*(int*)(weapon_data + 0x6C) + 0x30);
        if (anim_slot != -1) {
            int anim = FUN_801a72f8(DAT_80610ba0 + anim_slot * 0x29C, 0);
            *(float*)(weapon_data + 0x1B0) *=
                *(float*)(&DAT_80431204 + *(int*)(anim + 0x64) * 8);
        }
    }

    // --- Barrel oscillation (yaw/pitch/roll sway) ---
    if (fabsf(*(float*)(weapon_data + 0x4C)) >= FLOAT_806143c8 ||
        fabsf(*(float*)(weapon_data + 0x50)) >= FLOAT_806143c8 ||
        fabsf(*(float*)(weapon_data + 0x54)) >= FLOAT_806143c8) {
        // Apply angular velocity to yaw/pitch/roll with clamping and bounce
        // ... (angle wrapping, limit checks, velocity reversal at bounds)
        // Decay velocities toward zero (exponential damping)
        *(u32*)(entity + 0x18) |= 2; // mark dirty
    }

    // --- Update child bone matrices ---
    weapon_update_bone_matrices(); // FUN_8008b288

end_label:
    // --- Update child entities (ammo belt, attachments) ---
    if (entity_has_active_children(entity) != 0) {
        weapon_bone_matrix_builder(); // FUN_8008a280
    }

    // --- Collision face rotation (if weapon def has collision slot) ---
    if (*(int*)(weapon_def + 0xA4) >= 0) {
        u32 mtx_handle = FUN_8012ef14();
        FUN_8012f534((double)*(float*)(weapon_data + 0x1C8), 0.0, 0.0);
        collision_set_face_entry(*(int*)(entity + 0x4), *(int*)(weapon_def + 0xA4), mtx_handle);
    }

    FUN_80382728(); // stack restore
}

// ============================================================================
// 0x8008a280 | weapon_bone_matrix_builder
// MASSIVE per-weapon-type bone matrix computation for attached weapon model.
// Builds rotation/translation matrices from weapon angles and applies them
// to the model's bone table slots. Switch on weapon type (12+ cases):
//   1,4: Single recoil bone
//   2: Yaw/pitch only
//   3: Pump-action bone with separate yaw
//   5: Dual pivot (grip + barrel)
//   6,7: Elevation bone with height offset
//   8,9,10: Multi-bone (ammo belt, barrel, grip, pivot x6 slots)
//   0x11-0x13: Variant bone selection based on weapon slot
//   0x14-0x16: Full 11-bone layout with per-slot selection
//   0x18: Mounted turret (dual pivot: horizontal + vertical)
// Uses FUN_8012f388 (TRS matrix), FUN_8012f2c4 (rotation matrix),
// FUN_80130bd4 (matrix multiply), FUN_80130b90 (matrix copy),
// FUN_8012f534 (translation), FUN_8012f95c (bone array fill).
// ============================================================================
void weapon_bone_matrix_builder(void) {
    int entity = FUN_803826dc();
    int weapon_data = *(int*)(entity + 0x7C);
    int def_offset = *(int*)(weapon_data + 0xC) * WEAPON_DEF_SIZE;

    // Build base rotation matrix from weapon angles + muzzle bone position
    FUN_8012ef8c(); // identity matrix init
    int* bone_table = *(int**)(*(int*)(entity + 0x4) + 0x4);
    // Get muzzle bone vertex position
    u32 packed_ref = *(u32*)(&g_weapon_def_table[def_offset + 0x18]);
    int bone_idx = (int)(packed_ref >> 16);
    int vert_idx = (packed_ref & 0xFFFF) * 0x18;
    int base = *bone_table;
    int vert_addr = bone_table[base * -0x30 + bone_idx * 0x30 + 0x1B] + vert_idx;

    float vx = *(float*)(vert_addr);
    float vy = *(float*)(vert_addr + 4);
    float vz = *(float*)(vert_addr + 8);

    // Build rotation matrix: translate to pivot, rotate, translate back
    float rot_mtx[16];
    FUN_8012f534(0.0f, *(float*)(weapon_data + 0x194), 0.0f);
    FUN_8012f388(vx, vy + *(float*)(weapon_data + 0x194), vz,
                 (3.14159f * *(float*)(weapon_data + 0x1C)) / 180.0f,
                 (3.14159f * *(float*)(weapon_data + 0x24)) / 180.0f,
                 (3.14159f * *(float*)(weapon_data + 0x2C)) / 180.0f,
                 rot_mtx);
    FUN_8012f534(-vx, -vy, -vz);
    float combined[16];
    FUN_80130bd4(combined, rot_mtx, rot_mtx);

    // Apply parent bone override if defined
    int parent_bone = *(int*)(&g_weapon_def_table[def_offset + 0x14]);
    if (parent_bone >= 0) {
        float* bone_mtx_table = *(float**)(*(int*)(entity + 0x4) + 0x10);
        FUN_80130b90(bone_mtx_table, bone_mtx_table + parent_bone * 16);
    }

    // Apply secondary bone override
    int secondary_bone = *(int*)(&g_weapon_def_table[def_offset + 0x1C]);
    if (secondary_bone >= 0) {
        float* bone_mtx_table = *(float**)(*(int*)(entity + 0x4) + 0x10);
        FUN_80130b90(rot_mtx, bone_mtx_table + secondary_bone * 16);
    }

    // Per-round recoil bones
    int num_rounds = *(int*)(&g_weapon_def_table[def_offset + 0x4C]);
    for (int i = 0; i < num_rounds; i++) {
        int recoil_bone = *(int*)(&g_weapon_def_table[def_offset + i * 0xC + 0x40]);
        if (recoil_bone >= 0) {
            float recoil = *(float*)(weapon_data + 0xB8) +
                           *(float*)(weapon_data + 0x198 + i * 4);
            FUN_8012f534(0.0f, 0.0f, -recoil);
            float* bone_mtx_table = *(float**)(*(int*)(entity + 0x4) + 0x10);
            FUN_80130bd4(combined, rot_mtx, bone_mtx_table + recoil_bone * 16);
        }
    }

    // Type-specific bone setup (switch on weapon_data+0xC)
    // ... (12+ cases handled similarly to above with type-specific bones)

    FUN_80382728(); // stack restore
}

// ============================================================================
// 0x8008b05c | weapon_on_spawn
// Initializes weapon entity after spawn. Sets weapon slot from def table,
// resets angles, computes initial room assignment via soundpad, and
// registers for per-frame update callback (type 7).
// ============================================================================
void weapon_on_spawn(int entity, int spawn_data) {
    int weapon_data = *(int*)(entity + 0x7C);

    // Set weapon animation slot
    if (*(int*)(spawn_data + 0x14) == -1) {
        *(int*)(*(int*)(weapon_data + 0x6C) + 0x30) =
            *(int*)(&g_weapon_def_table[*(int*)(spawn_data + 0x1C) * WEAPON_DEF_SIZE]);
    }
    else {
        *(int*)(*(int*)(weapon_data + 0x6C) + 0x30) = *(int*)(spawn_data + 0x14);
    }

    // Reset angles
    *(float*)(weapon_data + 0x24) = 0.0f;
    *(float*)(weapon_data + 0x1C) = 0.0f;

    // Initialize visual state
    u32 vis_handle = FUN_8009994c(FLOAT_806143c0, FLOAT_806143c0);
    *(u32*)(weapon_data + 0xB0) = vis_handle;

    FUN_8024001c(); // position update

    // Compute initial room from velocity/position
    float y_pos = *(float*)(entity + 0x68) + *(float*)(entity + 0x1AC);
    float xz_dist = sqrtf(*(float*)(entity + 0x1C0) * *(float*)(entity + 0x1C0) +
                          *(float*)(entity + 0x1C8) * *(float*)(entity + 0x1C8));
    float room_id = FUN_80099aa4(*(float*)(entity + 0x1C4), xz_dist, &y_pos, 0, NULL, 1);
    *(float*)(entity + 0x218) = room_id;

    // Fallback: find nearest sound entity for room
    if (FLOAT_806143d8 == *(float*)(entity + 0x218)) {
        int nearest = soundpad_find_nearest_entity(0.0f, &y_pos);
        int* zone = soundpad_resolve_zone_node(nearest);
        if (zone != NULL) {
            *(int*)(entity + 0x218) = zone[4];
        }
    }

    // Register for per-frame weapon update
    if ((*(u32*)(weapon_data + 0x8) & 0x80000) == 0) {
        entity_register_for_update(entity, 7, -0x7FF74A68);
    }
}

// ============================================================================
// 0x8008b18c | weapon_select_visible_submesh
// Selects which sub-mesh (LOD/ammo state) is visible based on whether
// item_can_pickup returns true. Enables one sub-mesh and disables others
// using render flag bits in the entity render data.
// ============================================================================
void weapon_select_visible_submesh(int param_1, int param_2, int start_index) {
    u64 packed = FUN_803826ec();
    int entity = (int)(packed >> 32);
    u32 num_meshes = (u32)packed;

    int can_pickup = item_can_pickup(entity);
    int selected = start_index; // default

    if (can_pickup != 0 && (DAT_80611928 & 1) == 0) {
        // Randomize selection within range
        u32 rand_val = FUN_8013104c();
        selected = start_index + (rand_val - (rand_val / num_meshes) * num_meshes);
    }

    if (selected >= 0) {
        int render_data = *(int*)(entity + 0x4);
        int* bone_table = *(int**)(render_data + 0x4);
        int base = *bone_table;

        // Enable selected mesh flag
        *(u32*)(render_data + 0x30) |=
            bone_table[base * -0x30 + selected * 0x30 + 0x14];

        // Disable all other meshes
        int count = start_index + num_meshes - start_index;
        int offset = start_index * 0xC0;
        for (int i = start_index; i < (int)(start_index + num_meshes); i++) {
            if (i != selected) {
                *(u32*)(render_data + 0x30) &=
                    ~*(u32*)((int)bone_table + offset + 0x50 + base * -0xC0);
            }
            offset += 0xC0;
        }
    }

    FUN_80382738(); // stack restore
}

// ============================================================================
// 0x8008b288 | weapon_update_bone_matrices
// Per-frame weapon visual state update. Handles:
//   Type 1,4: Barrel spin (enable render bit 1)
//   Type 3: Pump action (double render bits when recoil > threshold)
//   Type 8,9,10: Ammo belt animation (frame-based UV scroll with rate from def)
//   Type 0x11: Variant weapon slot selection (calls weapon_select_visible_submesh)
//   Type 0x14: Same for different weapon slots
// Also handles level 100 special sounds (chaingun spin, saw blade).
// ============================================================================
void weapon_update_bone_matrices(void) {
    int entity = FUN_803826f0();
    int weapon_data = *(int*)(entity + 0x7C);

    switch (*(int*)(weapon_data + 0xC)) {
    case 1:
    case 4:
        // Barrel spin: enable render bit
        *(u32*)(*(int*)(entity + 0x4) + 0x30) |= 1;
        break;

    case 3: {
        // Pump action: double bits based on recoil threshold
        int render_data = *(int*)(entity + 0x4);
        u32 bits = *(u32*)(render_data + 0x30) & 3;
        if ((bits == 0) ||
            (*(float*)(weapon_data + 0x198) > FLOAT_806143bc &&
             (DAT_80611928 & 1) == 0)) {
            u32 doubled = bits << 1;
            if (doubled < 2) doubled = 1;
            *(u32*)(render_data + 0x30) = (*(u32*)(render_data + 0x30) & ~7) | doubled;
        }
        break;
    }

    case 8:
    case 9:
    case 10: {
        // Ammo belt animation
        int* item_def = item_def_find_by_id(*(int*)(weapon_data + 0xC));
        int can_pickup = item_can_pickup(entity);
        int rate = item_def[9] * DAT_806108e4;
        // Animate UV offset based on fire state
        if (can_pickup == 0) {
            // Ammo depleting: animate with periodic skip
            int period = item_def[10];
            if (period > 0) {
                int frame = (int)*(float*)(weapon_data + 0x1CC);
                if (frame % period != 0) {
                    *(float*)(weapon_data + 0x1CC) -= (float)rate;
                    *(float*)(weapon_data + 0x1D0) += (float)rate;
                    *(float*)(weapon_data + 0x1D4) += (float)rate;
                }
            }
        }
        else {
            // Ammo available: continuous animation
            *(float*)(weapon_data + 0x1CC) -= (float)rate;
            *(float*)(weapon_data + 0x1D0) += (float)rate;
            *(float*)(weapon_data + 0x1D4) += (float)rate;
        }
        break;
    }

    case 0x11:
        // Variant selection for weapon slot 0x2A
        if (*(int*)(*(int*)(weapon_data + 0x6C) + 0x30) == 0x2A) {
            weapon_select_visible_submesh(entity, 5, 9);
        }
        break;

    case 0x14:
        if (*(int*)(*(int*)(weapon_data + 0x6C) + 0x30) == 0x2A) {
            weapon_select_visible_submesh(entity, 5, 5);
        }
        break;
    }

    // Level 100 special sounds (chaingun spin-up, saw blade)
    if (DAT_80612614 == 100) {
        int anim_slot = *(int*)(*(int*)(weapon_data + 0x6C) + 0x30);
        if (anim_slot == 0xD) {
            // Chaingun: pitch based on spin speed
            *(float*)(entity + 0xE8) *= FLOAT_806143dc; // decay
            *(float*)(entity + 0xE8) += FLOAT_806143c0 * *(float*)(entity + 0xE4);
            *(float*)(entity + 0xE4) = 1.0f;
            FUN_801b711c(*(int*)(weapon_data + 0x6C), 0xC5D, 0xC5C, entity);
            if (*(int*)(entity + 0x228) >= 0) {
                sound_set_pitch(*(float*)(entity + 0xE8), *(u32*)(entity + 0x228));
            }
        }
        else if (anim_slot == 10) {
            // Saw blade sound
            FUN_801b711c(*(int*)(weapon_data + 0x6C), 0xC62, 0xC63, entity);
        }
    }

    FUN_8038273c(); // stack restore
}

// ============================================================================
// 0x8008b598 | weapon_aim_deflection
// Computes lateral aim deflection from player movement. Projects player
// velocity onto the weapon's left/right axis, then applies to weapon pitch
// oscillation (weapon_data+0x50). Used for weapon sway while strafing.
// ============================================================================
void weapon_aim_deflection(void) {
    u64 packed = FUN_803826f0();
    int entity = (int)(packed >> 32);
    int weapon_data = *(int*)(entity + 0x7C);
    int player_slot = *(int*)packed;

    float aim_input[2]; // lateral, vertical
    if (DAT_806108e4 < 1 || *(int*)(player_slot + 0x20) != 8) {
        if (*(u32*)(player_slot + 0x80) == 0) goto end;
        // Get aim from analog stick
        FUN_80145e84(*(u32*)(player_slot + 0x80),
                     (float*)((int*)packed)[3], aim_input);
    }
    else {
        // Compute from frame timing
        float dt_inv = 1.0f / (float)(DAT_806109c0 - DOUBLE_806143d0);
        aim_input[0] = (*(float*)(*(int*)(player_slot + 0x7C) + 0x3C) *
                       (float)(DAT_806109c0 - DOUBLE_806143d0)) / FLOAT_806108ec;
        aim_input[1] = (*(float*)(*(int*)(player_slot + 0x7C) + 0x40) *
                       (float)(DAT_806109c0 - DOUBLE_806143d0)) / FLOAT_806108ec;
    }

    // Get weapon fire direction
    float fire_dir[3];
    weapon_compute_fire_origin(entity, 0, 0, NULL, fire_dir);

    // Get entity world position
    float world_pos[3];
    FUN_802405e8(entity, world_pos);

    // Project player velocity onto weapon's lateral axis
    float dot = aim_input[0] * fire_dir[0] + aim_input[1] * fire_dir[1] +
                0.0f * fire_dir[2];
    float lateral_x = -(fire_dir[0] * dot - aim_input[0]);
    float lateral_z = -(fire_dir[1] * dot - aim_input[1]);

    // Compute offset from target position
    float target_dx = (*(float*)(player_slot + 0x64) + *(float*)(player_slot + 0x1A8)) - world_pos[0];
    float target_dz = (*(float*)(player_slot + 0x6C) + *(float*)(player_slot + 0x1B0)) - world_pos[2];
    float lateral_dot = target_dz * lateral_x - target_dx * lateral_z;

    // Apply deflection if player is on the weapon's left side
    if (lateral_dot <= 0.0f) {
        float dt_scale = (1.0f / (float)(DAT_806109c0 - DOUBLE_806143d0)) * FLOAT_806108ec;
        *(float*)(weapon_data + 0x50) +=
            0.5f * (target_dz * lateral_x - target_dx * lateral_z) *
            *(float*)(weapon_data + 0x5C) * dt_scale;
    }

end:
    FUN_8038273c(); // stack restore
}

// ============================================================================
// 0x8008b774 | weapon_turret_aim_track
// Mounted turret aim tracking. Raycasts from camera toward crosshair target,
// computes direction from weapon to hit point, converts to yaw angle,
// and smoothly interpolates weapon yaw toward that angle.
// Called when player is on a type-0x18 mounted weapon.
// ============================================================================
void weapon_turret_aim_track(void) {
    u64 packed = FUN_803826e0();
    int entity = (int)(packed >> 32);
    int* player_slot = *(int**)((int)packed + 0x150);
    int weapon_data = *(int*)(entity + 0x7C);
    int camera = *(int*)(player_slot + 8);

    // Compute direction from camera to target
    float dir[3];
    dir[0] = *(float*)(player_slot + 0xB4) - *(float*)(camera + 0x27C);
    dir[1] = *(float*)(player_slot + 0xB8) - *(float*)(camera + 0x280);
    dir[2] = *(float*)(player_slot + 0xBC) - *(float*)(camera + 0x284);

    // Normalize direction (fast inverse sqrt)
    float len_sq = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];
    if (len_sq > 0.0001f) {
        float inv_len = 1.0f / sqrtf(len_sq);
        // Newton-Raphson refinement
        inv_len *= -(0.5f * len_sq * inv_len * inv_len - 1.5f);
        inv_len = 0.5f * inv_len * -(0.5f * len_sq * inv_len * inv_len - 1.5f);
        dir[0] *= inv_len;
        dir[1] *= inv_len;
        dir[2] *= inv_len;
    }

    // Convert to yaw angle
    float target_yaw;
    FUN_801311d8(dir, &target_yaw, NULL);

    // Compute screen-space angular offset
    target_yaw -= *(float*)(player_slot + 0x104);
    float scale = FLOAT_806143e4 * target_yaw;
    if (scale > 0.0f) scale = 0.0f;

    // Apply camera effect for recoil feedback
    FUN_800a3ae0(*(float*)(camera + 0xFEC), FLOAT_806143e8 + scale,
                 /* ... camera params ... */);

    // Raycast toward target to find impact point
    float ray_dir[3];
    ray_dir[0] = FLOAT_806143ec * *(float*)(camera + 0x334);
    ray_dir[1] = FLOAT_806143ec * *(float*)(camera + 0x338);
    ray_dir[2] = FLOAT_806143ec * *(float*)(camera + 0x33C);

    // Temporarily clear collision flags for self + player
    u32 save_flags_entity = *(u32*)(entity + 0x28);
    u32 save_flags_player = *(u32*)(*(int*)(player_slot + 0x14) + 0x28);
    *(u32*)(*(int*)(player_slot + 0x14) + 0x28) &= ~0x60;
    *(u32*)(entity + 0x28) &= ~0x60;

    float hit_pos[3];
    int hit = scene_raycast(*(int*)(player_slot + 0xDEC),
                            camera + 0x27C, ray_dir, 0, hit_pos,
                            NULL, NULL, 0x2110070, 1, NULL);
    if (hit == 0) {
        // No hit: use endpoint
        hit_pos[0] = ray_dir[0] + *(float*)(camera + 0x27C);
        hit_pos[1] = ray_dir[1] + *(float*)(camera + 0x280);
        hit_pos[2] = ray_dir[2] + *(float*)(camera + 0x284);
    }

    // Restore collision flags
    *(u32*)(*(int*)(player_slot + 0x14) + 0x28) |= save_flags_player & 0x60;
    *(u32*)(entity + 0x28) |= save_flags_entity & 0x60;

    // Compute direction from weapon to hit point
    float entity_pos[3];
    FUN_802405e8(entity, entity_pos);
    hit_pos[0] -= entity_pos[0];
    hit_pos[1] -= entity_pos[1];
    hit_pos[2] -= entity_pos[2];

    // Normalize hit direction
    len_sq = hit_pos[0]*hit_pos[0] + hit_pos[1]*hit_pos[1] + hit_pos[2]*hit_pos[2];
    if (len_sq > 0.0001f) {
        float inv = 1.0f / sqrtf(len_sq);
        inv *= -(0.5f * len_sq * inv * inv - 1.5f);
        inv = 0.5f * inv * -(0.5f * len_sq * inv * inv - 1.5f);
        hit_pos[0] *= inv; hit_pos[1] *= inv; hit_pos[2] *= inv;
    }

    // Negate for facing direction
    hit_pos[0] = -hit_pos[0];
    hit_pos[1] = -hit_pos[1];
    hit_pos[2] = -hit_pos[2];

    // Convert to target yaw for weapon
    float weapon_target_yaw;
    FUN_801311d8(hit_pos, &weapon_target_yaw, NULL);

    // Smooth interpolation toward target yaw
    float current_yaw = *(float*)(weapon_data + 0x1C);
    float dt = FLOAT_806143b4 * FLOAT_806108ec *
               (1.0f / (float)(DAT_806109c0 - DOUBLE_806143d0));

    // Compute shortest angle delta with wrapping
    float delta = weapon_target_yaw - current_yaw;
    // ... (wrap to [-180, 180], apply dt-scaled interpolation)

    *(float*)(weapon_data + 0x1C) = /* interpolated result */;

    FUN_8038272c(); // stack restore
}

// ============================================================================
// 0x8008bb64 | weapon_system_pool_reset
// Clears weapon system pool (0x690 bytes at 0x8055F8B0) and resets counter.
// Called during level transitions and system init.
// ============================================================================
void weapon_system_pool_reset(void) {
    memset_wrapper(0x8055F8B0, 0, 0x690); // -0x7FAA0750 as signed = 0x8055F8B0
    DAT_80611cc0 = 0;
}

// ============================================================================
// 0x8008bb9c | weapon_system_reset
// Wrapper that calls weapon_system_pool_reset.
// Registered as the weapon subsystem reset handler.
// ============================================================================
void weapon_system_reset(void) {
    weapon_system_pool_reset();
}
