// =============================================================================
// player_ai_combat.cpp - AI Combat & Weapon Management System
// Address Range: 0x800aec80 - 0x800b000c
// Date: 2026-08-11
// Batch: 253
// =============================================================================
//
// AI combat behavior, weapon drop/restore, aiming, HUD kill indicators,
// voice sounds, and per-frame bone updates.
//
// Key subsystems:
//   - Weapon drop on death (queued list iteration + spawn)
//   - Combat behavior tick (table-driven animation selection)
//   - Aim position computation (multi-entity-type targeting with bone transforms)
//   - Weapon state serialization for respawn
//   - Kill/death HUD indicator bits (16 players)
//
// =============================================================================

#include "common.h"
#include "player_ai.h"
#include "entity.h"
#include "sound.h"
#include "spawn.h"

// External references
extern int DAT_805015bd;
extern int DAT_80612610;
extern int DAT_80611d78;
extern int DAT_80612098;
extern int DAT_806125fc;
extern int DAT_80612600;
extern int DAT_80612614;
extern int DAT_805cc614;
extern int DAT_80611c24;
extern int DAT_8060f948;
extern u32 DAT_80588780[];  // 16-entry HUD kill/death indicator array

extern u8 DAT_80561d80[];   // weapon save buffer (weapon type IDs per player)
extern u8 DAT_80561e20[];   // weapon save buffer (0xD8 state blocks per weapon)

extern float FLOAT_80614900;
extern float FLOAT_80614904;
extern float FLOAT_80614914;
extern float FLOAT_80614918;
extern float FLOAT_8061491c;
extern float FLOAT_80614920;
extern float FLOAT_80614944;
extern float FLOAT_80614948;
extern float FLOAT_80614960;
extern float FLOAT_80614974;
extern float FLOAT_80614980;
extern float FLOAT_80614998;
extern float FLOAT_8061499c;
extern float FLOAT_806149a0;
extern float FLOAT_806149a4;
extern float FLOAT_806149a8;
extern float FLOAT_806149ac;
extern float FLOAT_8061490c;
extern double DOUBLE_80614990;

extern u8 DAT_80403328[];   // combat behavior condition table (0x1C stride)
extern u8 DAT_804d34d8[];   // weapon type flags table
extern u8 DAT_804d34dc[];
extern u16 DAT_804d1cd0[];  // weapon type index remap
extern float DAT_803cc468;
extern float DAT_803cc46c;
extern float DAT_803cc470;
extern float DAT_8048ca94[]; // character height offset table

// Forward declarations
extern void FUN_803826f0(void);
extern void FUN_8038273c(void);
extern void FUN_803826b4(void);
extern void FUN_80382700(void);
extern void FUN_803826d0(void);
extern void FUN_8038271c(void);
extern void FUN_803826cc(void);
extern void FUN_80382718(void);
extern void FUN_80023280(double, double);
extern int  FUN_801c0894(void);
extern int  FUN_8013e9cc(int, int, int*, int);
extern int  FUN_801bb58c(int, int*);
extern int  FUN_80151b5c(int);
extern int  FUN_8002588c(int, int);
extern int  FUN_80025950(int, int, int, int);
extern void FUN_8013104c(void);
extern int  FUN_80364290(int, int);
extern int  FUN_8013f3ec(int, float*, int);
extern void FUN_801b6afc(int, int);
extern void FUN_801b6b90(int*);
extern void FUN_800b9e5c(int);
extern void FUN_8012f05c(double, float*);
extern void FUN_8012f2c4(double, double, double, double, float*);
extern void FUN_8012fbf4(float*, float*);
extern void FUN_80130bd4(float*, float*, float*);
extern int  FUN_80130fc8(void);
extern int  FUN_801b6544(int, int);
extern void FUN_8023fec0(int, float*);

extern void leaf_31e8(u32, u32, int);
extern void player_ai_list_remove_by_id(int, int, int);
extern void player_ai_list_insert_target(int, int);
extern void spawn_dropped_weapon_create(int, int*, int*);
extern int  spawn_create_instance(void);
extern void entity_destroy_with_event(int*);
extern void player_ai_reset_weapon_inventory(void);
extern int  player_ai_is_ranged_weapon(int);
extern int  player_ai_is_valid_weapon_type(int);
extern int  player_has_weapon_equipped(void);
extern u32  player_ai_char_def_get_flags(int);
extern u8*  player_ai_get_sound_table(int, int);
extern void sound_get_emitter_position(int*, float*);
extern void sound_init_play_params(int*);
extern void sound_play_request(int*);
extern void sound_clear_bank_ref(int);
extern void cutscene_bone_inverse_precompute(void);

// =============================================================================
// Address: 0x800aec80 | player_ai_drop_queued_weapons
// Iterates the queued weapon linked list at player+0x534, spawns dropped weapon
// entities for each non-zero entry, then removes from list.
// =============================================================================
void player_ai_drop_queued_weapons(void) {
    int player;
    int* node;
    int* next;

    player = FUN_803826f0();
    node = *(int**)(player + 0x534);

    while (node != NULL) {
        if (node[4] == 0) {
            // Skip empty slots
            node = (int*)node[6];
        } else {
            // Spawn dropped weapon entity from queued weapon
            spawn_dropped_weapon_create(node[4], (int*)player, NULL);
            next = (int*)node[6];
            player_ai_list_remove_by_id(player + 0x304, node[0], node[1]);
            node = next;
        }
    }

    FUN_8038273c();
}

// =============================================================================
// Address: 0x800aecf8 | player_ai_drop_all_weapons
// Drops primary weapon (+0x80), iterates queued weapon list, drops secondary
// (+0xAF*4) and tertiary (+99*4) weapons. Respects AI flag 0x2000000 for
// drop suppression. Called on death/respawn (15+ callers).
// =============================================================================
void player_ai_drop_all_weapons(int* player) {
    // Drop primary weapon if equipped
    if (player[0x20] != 0) {
        player_ai_update_kill_hud_bits(*player, player[0x20], 0);
        spawn_dropped_weapon_create(player[0x20], player, NULL);
        player[0x20] = 0;
    }

    // Drop all queued weapons
    player_ai_drop_queued_weapons();

    // Clear weapon attachment slots
    FUN_801b6afc((int)player, (int)(player + 0x75));
    FUN_801b6afc((int)player, (int)(player + 0x29));
    FUN_801b6b90(player);

    // Drop secondary weapon (+0xAF*4 = +0x2BC)
    if (player[0xAF] != 0) {
        int should_drop = 1;
        if (DAT_805015bd == '\n') {
            // Multiplayer: check player count and AI suppress flag
            if (DAT_80612610 <= player[2] && (player[10] & 0x2000000) == 0) {
                should_drop = 1;
            } else {
                should_drop = 0;
            }
        }
        if (should_drop) {
            spawn_dropped_weapon_create(player[0xAF], player, player + 0x75);
        }
        entity_destroy_with_event((int*)player[0xAF]);
        player[0xAF] = 0;
    }

    // Drop tertiary weapon (+99*4 = +0x18C)
    if (player[99] != 0) {
        int should_drop = 1;
        if (DAT_805015bd == '\n') {
            if (DAT_80612610 <= player[2] && (player[10] & 0x2000000) == 0) {
                should_drop = 1;
            } else {
                should_drop = 0;
            }
        }
        if (should_drop) {
            spawn_dropped_weapon_create(player[99], player, player + 0x29);
        }
        entity_destroy_with_event((int*)player[99]);
        player[99] = 0;
    }

    // Reset inventory and clear weapon-active flag (bit 1)
    player_ai_reset_weapon_inventory();
    player[10] = player[10] & ~0x2;
    FUN_800b9e5c((int)player);
}

// =============================================================================
// Address: 0x800aee44 | player_ai_combat_behavior_tick
// MASSIVE table-driven combat animation selector (~0x7A4 bytes). Evaluates
// weapon state flags, target direction, animation condition table at 0x80403328
// (0x1C stride per entry). Selects combat animation from conditions including:
// ranged/melee/mounted/vehicle states, weapon flags, distance, anim events.
// Terminates on 0xFFFF sentinel in table.
// =============================================================================
void player_ai_combat_behavior_tick(void) {
    // [Large function - see Ghidra decompilation at 0x800aee44]
    // Table at DAT_80403328 with 0x1C stride per entry:
    //   +0x00: condition flags (ranged, melee, mounted, vehicle, etc.)
    //   +0x04: weapon type filter (-2 = any)
    //   +0x08: weapon capability flags mask
    //   +0x0C: animation state filter
    //   +0x10: direction requirement
    //   +0x14: action index / animation group
    //   +0x18: function pointer for weapon check
    //
    // Condition bits:
    //   0x001: weapon check callback required
    //   0x002: melee required
    //   0x004: mounted required
    //   0x008: has vehicle target
    //   0x010: weapon flag 4 check
    //   0x020: has active anim event
    //   0x040: current anim is attack
    //   0x080: anim override active
    //   0x100: ranged weapon required
    //   0x200: throwing weapon required
    //   0x400: weapon not firing
    //   0x800: vehicle with flag 0x800
    //   0x1000: random distance check
    //
    // Process: iterate table, check all conditions, first match triggers
    // combat animation via FUN_80023280.

    FUN_803826b4();
    // ... [table iteration logic] ...
    FUN_80382700();
}

// =============================================================================
// Address: 0x800af5e8 | player_ai_play_voice_sound
// Looks up voice sound ID from character sound table and plays at entity
// position with pitch scaled by entity scale ratio.
// =============================================================================
void player_ai_play_voice_sound(int* player, int sound_index) {
    u8* sound_table;
    int sound_id;
    float position[3];
    int play_params[5];
    float pitch;

    sound_table = player_ai_get_sound_table(
        (int)*(s16*)(player + 0x12),
        (int)*(s8*)((int)player + 0xC0D)
    );

    sound_id = (int)*(s16*)(sound_table + sound_index * 2 + 0x30);

    if (sound_id != 0) {
        // Special case: swap sound 0xC18 -> 0xC17 when flag set
        if (sound_id == 0xC18 && DAT_8060f948 != 0) {
            sound_id = 0xC17;
        }

        sound_get_emitter_position(player, position);

        // Pitch = default / (current_scale / base_scale)
        pitch = FLOAT_80614904 / ((float)player[0x2C2] / (float)player[0x2C3]);

        sound_init_play_params(play_params);
        play_params[0] = sound_id;
        // Set 3D position and pitch in params struct
        // local_24 = 1 (3D flag)
        // local_38 = pitch
        // local_30..local_28 = position xyz
        sound_play_request(play_params);
    }
}

// =============================================================================
// Address: 0x800af6c0 | player_ai_update_kill_hud_bits
// Updates the 16-player HUD kill/death indicator array (DAT_80588780).
// param_3==0: clear bits (death event), param_3==1: set bits (kill event).
// Differentiates same-team (bits 1/2) vs enemy (bits 4/8).
// =============================================================================
void player_ai_update_kill_hud_bits(int killer_entity, int victim_entity, int is_kill) {
    int killer_team = *(int*)(*(int*)(killer_entity + 0x7C) + 0x84);
    int victim_team = *(int*)(*(int*)(victim_entity + 0x7C) + 0x1C);

    if (is_kill == 0) {
        // Death event: clear indicator bits
        int same_team = (killer_team == victim_team);
        for (int i = 0; i < 16; i++) {
            if (same_team) {
                if (i == killer_team) {
                    DAT_80588780[i] &= ~0x2;  // Clear self-team death
                } else {
                    DAT_80588780[i] &= ~0x8;  // Clear other-team death
                }
            } else {
                if (i == killer_team) {
                    DAT_80588780[i] &= ~0x1;  // Clear self kill indicator
                } else {
                    DAT_80588780[i] &= ~0x4;  // Clear other kill indicator
                }
            }
        }
    } else if (is_kill == 1) {
        // Kill event: set indicator bits
        int same_team = (killer_team == victim_team);
        for (int i = 0; i < 16; i++) {
            if (same_team) {
                if (i == killer_team) {
                    DAT_80588780[i] |= 0x2;   // Set self-team kill
                } else {
                    DAT_80588780[i] |= 0x8;   // Set other-team kill
                }
            } else {
                if (i == killer_team) {
                    DAT_80588780[i] |= 0x1;   // Set self kill
                } else {
                    DAT_80588780[i] |= 0x4;   // Set other kill
                }
            }
        }
    }
}

// =============================================================================
// Address: 0x800af820 | player_ai_compute_aim_position
// MASSIVE aim position resolver (~0x58C bytes). Computes target aim point
// based on target entity type:
//   - No target: offset from own position using rotation
//   - Type 0x08 (humanoid): bone-based head/chest targeting with height offsets
//   - Type 0x40 (vehicle): entity position
//   - Type 0x80 (mounted weapon): position + offset
//   - Type 0x200 (vehicle turret): FUN_8023fec0 special handler
//   - Type 0x2000: position or position+offset based on game mode
//   - Type 0x8000000: explicit aim offset from entity
//   - Type 0x17 (special character): smoothed random aim with lerp
//
// Uses bone transforms for precise head/chest targeting. Applies height
// correction for crouching, prone, and weapon type offsets.
// =============================================================================
void player_ai_compute_aim_position(int* ai_state) {
    // [Large function - see Ghidra decompilation at 0x800af820]
    // Output: ai_state[0x2E2..0x2E4] = target aim position (x, y, z)
    //
    // Key logic paths:
    // 1. No target or flag 0x40000: aim forward from own position + rotation
    // 2. Target type 0x08 (character):
    //    a. If flag 0x4000: use specific bone (chest)
    //    b. If target is below and far enough: aim at feet
    //    c. If type 0x17 (special): smoothed random aim with lerp factor
    //    d. Normal: use head bone transform with height adjustments
    // 3. Target type 0x200: vehicle turret special function
    // 4. Target type 0x2000/0x80: position + aim offset vector
    // 5. Target type 0x8000000: explicit offset vector at 0x1A8

    if (FUN_80130fc8()) return;
    // ... [multi-path aim computation] ...
}

// =============================================================================
// Address: 0x800afebc | player_ai_save_weapon_state
// Serializes weapon inventory per player to static buffer before death/level
// transition. Saves weapon type IDs to DAT_80561d80 (0x50 stride per player)
// and 0xD8-byte state blocks to DAT_80561e20 (0x10E0 stride per player).
// Multiplayer only (DAT_805015bd == '\n').
// =============================================================================
void player_ai_save_weapon_state(void) {
    FUN_803826cc();

    if (DAT_805015bd == '\n') {
        int state_offset = 0;    // Offset into weapon ID buffer (0x50 per player)
        int block_offset = 0;    // Offset into state block buffer (0x10E0 per player)
        int player_offset = 0;   // Player struct stride (0x141C per player)

        for (int p = 0; p < DAT_80612610; p++) {
            int weapon_count = 0;
            int block_pos = 0;
            int id_pos = 0;

            // Walk linked weapon list at player+0x7C+0x534
            int node = *(int*)(*(int*)(*(int*)(DAT_80611d78 + player_offset + 0x14) + 0x7C) + 0x534);
            while (node != 0) {
                // Save weapon type ID (u16 at entity+0x94)
                *(u32*)(DAT_80561d80 + id_pos + state_offset) =
                    (u32)*(u16*)(*(int*)(node + 0x10) + 0x94);

                // Copy 0xD8 bytes of weapon state
                leaf_31e8(
                    (u32)(DAT_80561e20 + block_pos + block_offset),
                    *(u32*)(*(int*)(node + 0x10) + 0x7C),
                    0xD8
                );

                weapon_count++;
                block_pos += 0xD8;
                id_pos += 4;
                node = *(int*)(node + 0x18);
            }

            // Null-terminate weapon ID list
            *(u32*)(DAT_80561d80 + weapon_count * 4 + state_offset) = 0;

            block_offset += 0x10E0;
            state_offset += 0x50;
            player_offset += 0x141C;
        }
    }

    FUN_80382718();
}

// =============================================================================
// Address: 0x800afdac | player_ai_restore_weapons_on_respawn
// Recreates dropped weapon entities from saved state buffer on respawn.
// Calls spawn_create_instance per weapon, copies 0xD8 state block back,
// inserts into player weapon list. Multiplayer only.
// =============================================================================
void player_ai_restore_weapons_on_respawn(void) {
    FUN_803826d0();

    if (DAT_805015bd == '\n') {
        int id_offset = 0;       // 0x50 per player
        int block_offset = 0;    // 0x10E0 per player
        int player_offset = 0;   // 0x141C per player

        for (int p = 0; p < DAT_80612610; p++) {
            int id_pos = 0;
            int block_pos = 0;

            // Iterate saved weapons until null terminator
            while (*(int*)(DAT_80561d80 + id_pos + id_offset) != 0) {
                u8* saved_state = DAT_80561e20 + block_pos + block_offset;

                // Spawn weapon entity from saved definition
                int entity = spawn_create_instance(
                    /* ... params from saved_state+0x10, +0x0C, +0x14, +0x1C,
                       auStack_40, +0x18, +0x40, +0x44, +0x48, +0x4C ... */
                );

                // Restore 0xD8 state block
                leaf_31e8(*(u32*)(entity + 0x7C), (u32)saved_state, 0xD8);

                // Add to player weapon list
                player_ai_list_insert_target(
                    *(int*)(*(int*)(player_offset + DAT_80611d78 + 0x14) + 0x7C) + 0x304,
                    entity
                );

                block_pos += 0xD8;
                id_pos += 4;
            }

            id_offset += 0x50;
            player_offset += 0x141C;
            block_offset += 0x10E0;
        }
    }

    FUN_8038271c();
}

// =============================================================================
// Address: 0x800affac | player_ai_per_frame_bone_update
// Iterates all active AI entities and calls cutscene_bone_inverse_precompute
// for each. Called from frame_render_update.
// =============================================================================
void player_ai_per_frame_bone_update(void) {
    FUN_803826f0();

    for (int i = 0; i < DAT_80612098; i++) {
        cutscene_bone_inverse_precompute();
    }

    FUN_8038273c();
}

// =============================================================================
// Address: 0x800b000c | player_ai_clear_footstep_sounds
// Clears 7 AI footstep sound bank references (IDs 0x5F3-0x5F9).
// Called during level/menu transitions.
// =============================================================================
void player_ai_clear_footstep_sounds(void) {
    sound_clear_bank_ref(0x5F3);
    sound_clear_bank_ref(0x5F4);
    sound_clear_bank_ref(0x5F5);
    sound_clear_bank_ref(0x5F6);
    sound_clear_bank_ref(0x5F7);
    sound_clear_bank_ref(0x5F8);
    sound_clear_bank_ref(0x5F9);
}
