// Address: 0x80088360 - 0x80088f50 | Name: weapon_fire_system | Date: 2026-08-11
// Weapon fire system: def table accessors, fire origin, muzzle position,
// fire tick (ammo/rate/effects), weapon sounds, and pickup/equip handler.

#include "common.h"

// ============================================================================
// Weapon definition table: 25 entries x 0xB4 bytes at 0x803ff010
// ============================================================================

extern u8 g_weapon_def_table[]; // DAT_803ff010, 25 * 0xB4 bytes

#define WEAPON_DEF_COUNT    25
#define WEAPON_DEF_SIZE     0xB4

// ============================================================================
// 0x80088360 | weapon_get_def_from_entity
// Returns pointer to weapon def entry for the entity's current weapon slot.
// Checks entity weapon data (entity+0x7C) flags and index validity.
// ============================================================================
void* weapon_get_def_from_entity(int entity) {
    int weapon_data = *(int*)(entity + 0x7C);

    if (weapon_data == 0) return NULL;
    if ((*(u32*)(weapon_data + 0x8) & 0x40) == 0) return NULL;

    int weapon_index = *(int*)(weapon_data + 0xC);
    if (weapon_index < 0 || weapon_index >= WEAPON_DEF_COUNT) return NULL;

    return &g_weapon_def_table[weapon_index * WEAPON_DEF_SIZE];
}

// ============================================================================
// 0x800883a8 | weapon_get_def_by_index
// Direct index lookup into weapon def table. Bounds-checked [0, 25).
// ============================================================================
void* weapon_get_def_by_index(int index) {
    if (index < 0 || index >= WEAPON_DEF_COUNT) return NULL;
    return &g_weapon_def_table[index * WEAPON_DEF_SIZE];
}

// ============================================================================
// 0x8008874c | weapon_get_muzzle_position
// Computes muzzle/fire origin from skeleton bone data.
// Special cases for weapon type 0x18 (mounted gun) and type 5 (handheld).
// Uses bone table from entity render data to extract vertex positions.
// ============================================================================
void weapon_get_muzzle_position(void) {
    // Stack restore registers
    u64 packed = FUN_803826f0();
    int entity = (int)(packed >> 32);
    float* out_pos = (float*)(int)packed;

    int weapon_data = *(int*)(entity + 0x7C);
    u8* weapon_def = (u8*)weapon_get_def_by_index(*(int*)(weapon_data + 0xC));
    int weapon_type = *(int*)(weapon_data + 0xC);

    int* bone_table = *(int**)(*(int*)(entity + 0x4) + 0x4);
    int base_bone = *bone_table;

    if (weapon_type == 0x18) {
        // Mounted gun: use specific bone pair for position + Y offset
        float* bone_pos = (float*)bone_table[base_bone * -0x30 + 0x1B];
        float* muzzle_bone = (float*)bone_table[base_bone * -0x30 + 0x4B];
        float mtx[16];

        // Build rotation matrix from bone position + weapon Y offset
        FUN_8012f2c4((double)*bone_pos,
                     (double)(bone_pos[1] + *(float*)(weapon_data + 0x194)),
                     (double)bone_pos[2],
                     (double)((3.14159f * *(float*)(weapon_data + 0x24)) / 180.0f),
                     mtx);
        // Apply translation offset
        float neg_pos[3] = { -*bone_pos, -bone_pos[1], -bone_pos[2] };
        FUN_8012f534((double)neg_pos[0], (double)neg_pos[1], (double)neg_pos[2]);
        float result_mtx[16];
        FUN_80130bd4(result_mtx, mtx, mtx);

        // Copy muzzle bone position and transform
        out_pos[0] = muzzle_bone[0];
        out_pos[1] = muzzle_bone[1];
        out_pos[2] = muzzle_bone[2];
        FUN_8012f9b8(mtx, out_pos);
    }
    else if (weapon_type == 5) {
        // Handheld type 5: direct bone vertex lookup
        u32 packed_ref = *(u32*)(weapon_def + 0x18);
        int bone_idx = (int)(packed_ref >> 16);
        int vert_idx = packed_ref & 0xFFFF;

        int bone_data = bone_table[base_bone * -0x30 + bone_idx * 0x30 + 0x1B];
        float* vert = (float*)(bone_data + vert_idx * 0x18);

        int root_bone = bone_table[base_bone * -0x30 + 0x1B];
        out_pos[0] = vert[0];
        out_pos[1] = *(float*)(root_bone + 0x7C);
        out_pos[2] = vert[2];
    }
    else {
        // Default: bone vertex lookup + Y height offset
        u32 packed_ref = *(u32*)(weapon_def + 0x18);
        int bone_idx = (int)(packed_ref >> 16);
        int vert_idx = packed_ref & 0xFFFF;

        int bone_data = bone_table[base_bone * -0x30 + bone_idx * 0x30 + 0x1B];
        out_pos[0] = *(float*)(bone_data + vert_idx * 0x18);
        out_pos[1] = *(float*)(bone_data + vert_idx * 0x18 + 4);
        out_pos[2] = *(float*)(bone_data + vert_idx * 0x18 + 8);
        out_pos[1] += *(float*)(weapon_data + 0x194); // Y height offset
    }

    FUN_8038273c(); // Stack restore
}

// ============================================================================
// 0x800886ac | weapon_fire_dispatch_callback
// Calls the weapon def's fire callback (def+0xB0) with entity, slot, and
// the appropriate bone index based on fire mode (primary vs alt-fire).
// ============================================================================
void weapon_fire_dispatch_callback(int entity, int slot, int fire_mode, void* out_pos) {
    // Stack restore
    u64 packed = FUN_803826ec();
    int ent = (int)(packed >> 32);
    int player_slot = (int)packed;

    u8* weapon_def = (u8*)weapon_get_def_by_index(
        *(int*)(*(int*)(ent + 0x7C) + 0xC));

    if (weapon_def == NULL) return;

    typedef void (*fire_callback_t)(int, int, int, void*);
    fire_callback_t callback = *(fire_callback_t*)(weapon_def + 0xB0);

    if (fire_mode == 1 && *(int*)(weapon_def + 0x20) >= 0) {
        // Alt-fire mode: use alt bone index at def+0x20
        callback(ent, player_slot, *(int*)(weapon_def + 0x20), out_pos);
    }
    else {
        // Normal fire: use per-slot bone index at def + slot*0xC + 0x54
        int bone_idx = *(int*)(weapon_def + player_slot * 0xC + 0x54);
        callback(ent, player_slot, bone_idx, out_pos);
    }

    FUN_80382738(); // Stack restore
}

// ============================================================================
// 0x800883d4 | weapon_compute_fire_origin
// Computes the world-space fire origin position and fire direction vector.
// Uses entity local matrix, weapon angles, bone positions, and camera data.
// For weapon type 0x17 (dual-wield?), applies additional yaw offset.
// Outputs: param_4 = fire origin position, param_5 = fire direction (normalized)
// ============================================================================
void weapon_compute_fire_origin(int param_1, int param_2, int fire_mode,
                                 float* out_position, float* out_direction) {
    // Stack restore
    u64 packed = FUN_803826e4();
    int entity = (int)(packed >> 32);
    int player_slot = (int)packed;

    int weapon_data = *(int*)(entity + 0x7C);
    float direction[3] = { 0.0f, 0.0f, 0.0f };

    // Get muzzle position into local buffer
    float muzzle_pos[3];
    weapon_get_muzzle_position(); // writes to stack via register convention

    // Get fire target position via dispatch callback
    float target_pos[3];
    weapon_fire_dispatch_callback(entity, player_slot, fire_mode, target_pos);

    // Direction = muzzle - target
    direction[0] = muzzle_pos[0] - target_pos[0];
    direction[1] = muzzle_pos[1] - target_pos[1];
    direction[2] = muzzle_pos[2] - target_pos[2];

    // Build entity local matrix
    float local_mtx[16];
    entity_build_local_matrix(local_mtx, entity, 0, 0);

    // Build rotation matrix from weapon angles (yaw, pitch, roll)
    float rot_mtx[16];
    FUN_8012f154(
        (double)((3.14159f * *(float*)(weapon_data + 0x1C)) / 180.0f),
        (double)((3.14159f * *(float*)(weapon_data + 0x24)) / 180.0f),
        (double)((3.14159f * *(float*)(weapon_data + 0x2C)) / 180.0f),
        rot_mtx);

    // Concatenate rotation with entity matrix
    float combined_mtx[16];
    FUN_80130bd4(rot_mtx, local_mtx, combined_mtx);

    // Get entity world position
    float world_pos[3];
    FUN_802405e8(entity, world_pos);

    // Transform direction by combined matrix
    FUN_8012fbf4(combined_mtx, direction);

    // Final fire origin = world_pos + transformed_direction
    direction[0] += world_pos[0];
    direction[1] += world_pos[1];
    direction[2] += world_pos[2];

    if (out_position != NULL) {
        out_position[0] = direction[0];
        out_position[1] = direction[1];
        out_position[2] = direction[2];
    }

    if (out_direction != NULL) {
        // Weapon type 0x17 (dual-wield): apply additional yaw offset
        if (*(int*)(weapon_data + 0xC) == 0x17) {
            FUN_8012f154(
                (double)((3.14159f * (*(float*)(weapon_data + 0x1C) +
                          *(float*)(weapon_data + 0x1CC))) / 180.0f),
                (double)((3.14159f * *(float*)(weapon_data + 0x24)) / 180.0f),
                (double)((3.14159f * *(float*)(weapon_data + 0x2C)) / 180.0f),
                rot_mtx);
            FUN_80130bd4(rot_mtx, local_mtx, combined_mtx);
        }

        float epsilon = 0.0001f;
        int camera = *(int*)(weapon_data + 0x68);
        if (camera == 0) {
            // No camera: use matrix forward column
            out_direction[0] = combined_mtx[8];  // local_c8
            out_direction[1] = combined_mtx[9];  // local_c4
            out_direction[2] = combined_mtx[10]; // local_c0
        }
        else {
            // Camera available: compute look direction from camera
            out_direction[0] = *(float*)(camera + 0x64) + *(float*)(camera + 0x1A8);
            out_direction[1] = *(float*)(camera + 0x68) + *(float*)(camera + 0x1AC);
            out_direction[2] = *(float*)(camera + 0x6C) + *(float*)(camera + 0x1B0);

            // Subtract world position to get direction
            out_direction[0] -= world_pos[0];
            out_direction[1] -= world_pos[1];
            out_direction[2] -= world_pos[2];

            // Normalize direction (fast inverse sqrt with Newton-Raphson)
            float len_sq = out_direction[0] * out_direction[0] +
                           out_direction[1] * out_direction[1] +
                           out_direction[2] * out_direction[2];

            if (epsilon < len_sq) {
                float inv_len = 1.0f / sqrtf(len_sq);
                // Two Newton-Raphson refinement iterations
                inv_len = inv_len * -(0.5f * len_sq * inv_len * inv_len - 1.5f);
                inv_len = 0.5f * inv_len * -(0.5f * len_sq * inv_len * inv_len - 1.5f);
                out_direction[0] *= inv_len;
                out_direction[1] *= inv_len;
                out_direction[2] *= inv_len;
            }
        }
    }

    FUN_80382730(); // Stack restore
}

// ============================================================================
// 0x80088960 | weapon_fire_tick
// Core weapon firing function. Handles:
// - Fire rate timing (cooldown accumulation toward 1.0)
// - Ammo consumption and round cycling
// - Muzzle flash and shell casing spawning
// - Fire sound effects (3D positioned)
// - Particle beam/tracer between muzzle and impact bones
// Called from the main weapon update when fire button is held.
// ============================================================================
void weapon_fire_tick(int param_1, int param_2, int fire_mode,
                      int fire_type, void* out_fire_pos) {
    // Stack restore
    u64 packed = FUN_803826d4();
    int entity = (int)(packed >> 32);
    int player_slot = (int)packed;

    int weapon_data = *(int*)(entity + 0x7C);
    int weapon_idx = *(int*)(weapon_data + 0xC);
    int def_offset = weapon_idx * WEAPON_DEF_SIZE;

    // Player weapon state block
    extern int g_player_weapon_state_base; // DAT_80610ba0
    int weapon_state = g_player_weapon_state_base + *(int*)(player_slot + 0x30) * 0x29C;
    int anim_handle = *(int*)(weapon_state + 0x14);

    // Determine animation variant based on fire mode
    int anim_variant;
    if (fire_mode == 1 && *(int*)(&g_weapon_def_table[def_offset + 0x20]) >= 0) {
        anim_variant = FUN_801a72f8(weapon_state, 1);
        if (*(int*)(weapon_state + 0x1C) != 0) {
            anim_handle = *(int*)(weapon_state + 0x1C);
        }
    }
    else {
        anim_variant = FUN_801a72f8(weapon_state, 0);
    }

    float one = 1.0f;

    // Fire rate cooldown check
    if (*(float*)(weapon_data + 0x1C4) <= 0.0f) {
        // Accumulate fire rate toward 1.0
        float fire_rate = *(float*)(&g_weapon_def_table[def_offset + 0x98]);
        *(float*)(player_slot + 0x108) +=
            -(0.5f * *(float*)(player_slot + 0x108) * fire_rate - fire_rate);

        if (one <= *(float*)(player_slot + 0x108)) {
            // Fire rate reached: reset cooldown timer
            *(float*)(weapon_data + 0x1C4) =
                *(float*)(&g_weapon_def_table[def_offset + 0xA0]);
            *(float*)(player_slot + 0x108) = one;
        }

        // Ammo consumption for fire_type == 4
        if (fire_type == 4) {
            int ammo_slot = weapon_data + anim_handle * 4;
            *(int*)(ammo_slot + 0xE4) -= 1;
        }

        // Compute fire origin position
        weapon_fire_dispatch_callback(entity, *(int*)(weapon_data + 0x70),
                                      fire_mode, out_fire_pos);

        // Set fire animation speed from def table
        *(float*)(weapon_data + 0x1B0) =
            *(float*)(&g_weapon_def_table[0x431200 + *(int*)(anim_variant + 0x64) * 8]);

        // Set fire rate per-slot
        if (fire_mode == 1 && *(int*)(&g_weapon_def_table[def_offset + 0x20]) >= 0) {
            *(float*)(weapon_data + 0xB8) =
                0.75f * *(float*)(&g_weapon_def_table[def_offset + 0x04]);
        }
        else {
            *(float*)(weapon_data + *(int*)(weapon_data + 0x70) * 4 + 0x198) =
                *(float*)(&g_weapon_def_table[def_offset + 0x04]);
        }

        // Advance round counter (wraps at max rounds)
        int round = *(int*)(weapon_data + 0x70) + 1;
        *(int*)(weapon_data + 0x70) = round;
        if (*(int*)(&g_weapon_def_table[def_offset + 0x4C]) <= round) {
            *(int*)(weapon_data + 0x70) = 0;
        }

        // Muzzle flash + shell casing spawning
        if (fire_type == 4 ||
            ((*(u32*)(weapon_data + 0x8) & 0x8000) != 0 && fire_type == 1)) {

            // Spawn muzzle flash particle
            if (fire_mode == 1 && *(int*)(&g_weapon_def_table[def_offset + 0x20]) >= 0) {
                FUN_8013104c(/* alt-fire muzzle */);
            }
            else {
                FUN_8013104c(/* normal muzzle */);
            }

            // Screen shake / camera effect
            extern int g_camera_shake_handle; // DAT_80611d54
            if (g_camera_shake_handle != 0) {
                FUN_8009fe98(/* shake params */);
            }

            // Spawn tracer/beam between muzzle bones if weapon has beam effect
            int beam_effect_id = *(int*)(anim_variant + 0x54);
            int muzzle_bone_a = *(int*)(&g_weapon_def_table[def_offset + 0x28]);
            int muzzle_bone_b = *(int*)(&g_weapon_def_table[def_offset + 0x2C]);

            if (beam_effect_id != -1 && muzzle_bone_a >= 0 &&
                muzzle_bone_b >= 0 && *(int*)(weapon_data + 0xD8) != 0) {

                int skel_data = *(int*)(weapon_data + 0x6C);
                int* bone_table = *(int**)(*(int*)(weapon_data + 0xD8) + 0x7C);

                // Build rotation matrix from entity angles
                float rot_mtx[16];
                FUN_8012f154(
                    (double)((3.14159f * *(float*)(entity + 0xC0)) / 180.0f),
                    (double)((3.14159f * *(float*)(entity + 0x70)) / 180.0f),
                    (double)((3.14159f * *(float*)(entity + 0xC8)) / 180.0f),
                    rot_mtx);

                // Build weapon angle rotation
                float weapon_rot[16];
                FUN_8012f154(
                    (double)((3.14159f * *(float*)(weapon_data + 0x1C)) / 180.0f),
                    (double)((3.14159f * *(float*)(weapon_data + 0x24)) / 180.0f),
                    (double)((3.14159f * *(float*)(weapon_data + 0x2C)) / 180.0f),
                    weapon_rot);
                FUN_80130bd4(weapon_rot, rot_mtx, rot_mtx);

                // Get bone A position (beam start)
                float beam_start[3];
                u32 ref_a = *(u32*)(&g_weapon_def_table[def_offset + 0x28]);
                int bone_a_idx = (int)(ref_a >> 16);
                int vert_a_idx = ref_a & 0xFFFF;
                int* btbl = *(int**)(*(int*)(entity + 0x4) + 0x4);
                int base = *btbl;
                beam_start[0] = *(float*)(btbl[base * -0x30 + bone_a_idx * 0x30 + 0x1B] + vert_a_idx * 0x18);
                beam_start[1] = *(float*)(btbl[base * -0x30 + bone_a_idx * 0x30 + 0x1B] + vert_a_idx * 0x18 + 4);
                beam_start[2] = *(float*)(btbl[base * -0x30 + bone_a_idx * 0x30 + 0x1B] + vert_a_idx * 0x18 + 8);

                // Get bone B position (beam end)
                float beam_end[3];
                u32 ref_b = *(u32*)(&g_weapon_def_table[def_offset + 0x2C]);
                int bone_b_idx = (int)(ref_b >> 16);
                int vert_b_idx = ref_b & 0xFFFF;
                beam_end[0] = *(float*)(btbl[base * -0x30 + bone_b_idx * 0x30 + 0x1B] + vert_b_idx * 0x18);
                beam_end[1] = *(float*)(btbl[base * -0x30 + bone_b_idx * 0x30 + 0x1B] + vert_b_idx * 0x18 + 4);
                beam_end[2] = *(float*)(btbl[base * -0x30 + bone_b_idx * 0x30 + 0x1B] + vert_b_idx * 0x18 + 8);

                // Transform both by weapon rotation matrix
                FUN_8012fbf4(rot_mtx, beam_start);
                FUN_8012fbf4(rot_mtx, beam_end);

                // Offset by entity world position
                beam_start[0] += *(float*)(entity + 0x64);
                beam_start[1] += *(float*)(entity + 0x68);
                beam_start[2] += *(float*)(entity + 0x6C);
                beam_end[0] += *(float*)(entity + 0x64);
                beam_end[1] += *(float*)(entity + 0x68);
                beam_end[2] += *(float*)(entity + 0x6C);

                // Spawn beam particle effect
                FUN_800cb948(/* beam params */
                    -1, 2, beam_effect_id,
                    skel_data, 0, weapon_state, anim_variant, bone_table);
            }
        }
    }

    else if (*(int*)(player_slot + 0xF0) > 5) {
        // Fire rate exceeded without firing: play empty/click sound
        int sound_params[7];
        sound_init_play_params((int)sound_params);
        sound_params[0] = 0xD3C; // empty click sound ID
        // Set 3D position from entity
        *(float*)((u8*)sound_params + 0x1C) = *(float*)(entity + 0x64);
        *(float*)((u8*)sound_params + 0x20) = *(float*)(entity + 0x68);
        *(float*)((u8*)sound_params + 0x24) = *(float*)(entity + 0x6C);
        *(u8*)((u8*)sound_params + 0x28) = 1; // positional flag
        sound_play_request(sound_params);

        *(int*)(player_slot + 0xF0) = 0; // reset counter

        if (fire_mode == 1 || (*(u32*)(weapon_state + 0xC) & 0x400) == 0) {
            *(int*)(player_slot + 0xF4) = 0;
        }
    }

    FUN_80382720(); // Stack restore
}

// ============================================================================
// 0x80088e88 | weapon_fire_stub
// Empty no-op placeholder function.
// ============================================================================
void weapon_fire_stub(void) {
    return;
}

// ============================================================================
// 0x80088e8c | weapon_get_fire_sound_id
// Maps weapon type ID + fire mode to a sound effect ID.
// Returns -1 if weapon type has no specific sound.
// Known mappings:
//   Type 0xE6 + mode!=0 -> 0x668
//   Type 0xE6 + mode==0 -> -1
//   Type 0x36-0x37 + mode!=0 -> 0xD62
//   Type 0x36-0x37 + mode==0 -> 0xD63
// ============================================================================
int weapon_get_fire_sound_id(int weapon_type, int fire_mode) {
    if (weapon_type == 0xE6) {
        if (fire_mode != 0) return 0x668;
        return -1;
    }

    if (weapon_type < 0xE6 && weapon_type < 0x38 && weapon_type > 0x35) {
        // Types 0x36, 0x37 (grenade launchers?)
        if (fire_mode != 0) return 0xD62;
        return 0xD63;
    }

    return -1;
}

// ============================================================================
// 0x80088edc | weapon_play_fire_sound_3d
// Plays a positioned weapon sound effect based on weapon type and fire mode.
// Resolves sound ID via weapon_get_fire_sound_id, then plays at given position.
// ============================================================================
void weapon_play_fire_sound_3d(int weapon_type, int fire_mode, float* position) {
    int sound_id = weapon_get_fire_sound_id(weapon_type, fire_mode);

    if (sound_id != -1) {
        int sound_params[7];
        sound_init_play_params((int)sound_params);
        // Set 3D position
        *(float*)((u8*)sound_params + 0x1C) = position[0];
        *(float*)((u8*)sound_params + 0x20) = position[1];
        *(float*)((u8*)sound_params + 0x24) = position[2];
        *(u8*)((u8*)sound_params + 0x28) = 1; // positional flag
        sound_params[0] = sound_id;
        sound_play_request(sound_params);
    }
}

// ============================================================================
// 0x80088f50 | weapon_pickup_equip
// Handles weapon pickup and equip transition. When an item entity reaches
// state 5 (pickup range), this transitions it to state 6 (equipped).
// Computes aim angles from atan2, plays pickup sound, links to player entity,
// sets weapon flags, and triggers camera/UI effects.
// ============================================================================
void weapon_pickup_equip(void) {
    // Stack restore
    u64 packed = FUN_803826e4();
    int entity = (int)(packed >> 32);
    int player_slot = (int)packed;

    int weapon_data = *(int*)(entity + 0x7C);

    // Only process if in pickup-ready state (state == 5)
    if (*(int*)(weapon_data + 0x10) != 5) {
        FUN_80382730();
        return;
    }

    // Get player entity from player slot
    int* player_bones = *(int**)(*(int*)(player_slot + 0x14) + 0x7C);
    int player_bone_base = *player_bones;

    // Get weapon def for this entity
    u8* weapon_def = (u8*)weapon_get_def_from_entity(entity);

    // Get entity world position
    float entity_pos[3];
    FUN_802405e8(entity, entity_pos);

    // Compute horizontal aim angle (atan2 of XZ offset from player)
    float dx = entity_pos[0] - *(float*)(player_slot + 0xA8);
    float dz = entity_pos[2] - *(float*)(player_slot + 0xB0);
    double angle_h = (double)FUN_80390aa4((double)dx, (double)dz);
    double angle_deg = (double)((180.0f * (float)angle_h) / 3.14159f);

    // Compute vertical aim angle with clamping
    float clamp_factor = 180.0f / 3.14159f; // reused
    double angle_raw = (double)((float)(angle_deg - (double)90.0f) / 180.0f);
    double angle_wrapped = (double)FUN_80390324(angle_raw);
    *(float*)(weapon_data + 0x74) =
        -(180.0f * (float)angle_wrapped - (float)angle_deg);

    // Compute pitch from player vertical aim
    double pitch_raw = (double)(float)(
        (-(double)*(float*)(player_slot + 0x104) - (double)90.0f) / 180.0f);
    double pitch_wrapped = (double)FUN_80390324(pitch_raw);
    *(float*)(weapon_data + 0x78) =
        -(180.0f * (float)pitch_wrapped -
          (-(double)*(float*)(player_slot + 0x104)));

    // Apply pitch clamping from weapon def min/max
    float pitch_min = *(float*)(weapon_data + 0x34);
    float pitch_max = *(float*)(weapon_data + 0x38);
    if (pitch_min != pitch_max) {
        float raw_pitch = -*(float*)(player_slot + 0x104);
        float clamped = raw_pitch;
        if (pitch_max < raw_pitch) clamped = pitch_max;
        if (pitch_min <= clamped) {
            clamped = raw_pitch;
            if (pitch_max < raw_pitch) clamped = pitch_max;
        }
        *(float*)(weapon_data + 0x78) = clamped;
    }

    // Play pickup/equip sound (3D positioned)
    weapon_play_fire_sound_3d((u32)*(u16*)(entity + 0x94), 1,
                              (float*)(player_slot + 0xA8));

    // Transition to equipped state
    *(int*)(weapon_data + 0x10) = 6;

    // Store player bone base for attachment
    *(int*)(weapon_data + 0xD8) = player_bone_base;

    // Set entity movement flags
    FUN_800ab2a4(player_slot, 0x20);

    // Link weapon to player entity
    player_bones[0x305] = entity;
    *(u32*)(entity + 0x28) |= 0x4000000; // "attached" flag

    // Reset fire timer
    *(int*)(player_slot + 0xE1C) = 0;

    // Initialize weapon animations
    FUN_801a92b4((int)player_bones);
    FUN_801afd14(player_slot, *(int*)(*(int*)(weapon_data + 0x6C) + 0x30));

    // Level-specific camera effect (levels 2, 0x1A)
    extern int g_current_level_id; // DAT_80612614
    if (g_current_level_id == 2 || g_current_level_id == 0x1A) {
        FUN_8012c264(); // camera transition
    }

    // Player HUD weapon display update
    if ((*(u32*)(weapon_data + 0x8) & 0x8000) != 0 &&
        *(int*)(player_slot + 0x8) != 0) {
        FUN_800a3ae0(/* HUD weapon icon params */);
        FUN_8009d830(*(int*)(player_slot + 0x8), 5, player_bone_base);
    }

    // Zoom reset if weapon supports it
    if ((*(u32*)(weapon_data + 0x8) & 0x4000) != 0 &&
        *(int*)(weapon_def + 0x38) >= 0) {
        FUN_80023280((double)0.0f, (double)1.0f);
    }

    FUN_80382730(); // Stack restore
}
