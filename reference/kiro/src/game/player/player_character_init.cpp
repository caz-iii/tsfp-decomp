// Address: 0x800a4498 | Name: player_get_analog_input | Date: 2026-08-11
// Address: 0x800a44d8 | Name: player_save_controller_config | Date: 2026-08-11
// Address: 0x800a45a8 | Name: player_apply_model_def | Date: 2026-08-11
// Address: 0x800a4608 | Name: player_clear_input_buffers | Date: 2026-08-11
// Address: 0x800a4654 | Name: player_character_system_init | Date: 2026-08-11
// Address: 0x800a4fb0 | Name: player_set_character_scale | Date: 2026-08-11
// Address: 0x800a5064 | Name: player_character_system_alloc | Date: 2026-08-11
// Address: 0x800a50f4 | Name: player_respawn | Date: 2026-08-11
// Address: 0x800a5470 | Name: player_notify_room_change_on_death | Date: 2026-08-11
// Address: 0x800a5504 | Name: player_show_respawn_location | Date: 2026-08-11
//
// Player character initialization, allocation, respawn, and related utilities.
// Part of the player subsystem managing per-player state (0x141c bytes per slot).

#include "common.h"

// ============================================================================
// 0x800a4498 - player_get_analog_input
// Returns the analog input value for a given input binding on the player's pad.
// If the binding is zero (unbound), returns 0.0f.
// 12 xrefs - called from menu input handler, weapon aim, etc.
// ============================================================================
double player_get_analog_input(int* player_data, int binding_index) {
    if (player_data[binding_index + 6] == 0) {
        return 0.0f;
    }
    return input_get_analog_value(*player_data, player_data[binding_index + 6]);
}

// ============================================================================
// 0x800a44d8 - player_save_controller_config
// Saves the current controller binding configuration to the save data block.
// Only runs in non-story, non-arcade modes (flag 0x20000000 clear, mode > 0x6a).
// Iterates over all active players and persists their pad assignment.
// Also calls FUN_800ae7bc (likely weapon/control rebind) if flag 0x100000 is clear.
// 1 xref.
// ============================================================================
void player_save_controller_config(void) {
    u8 pad_id;
    int save_ptr;

    FUN_803826f0(); // OS lock / critical section enter

    if (((g_match_flags & 0x20000000) == 0) &&
        (5 < (unsigned)(g_game_mode - 0x65)) &&
        (g_game_mode != 0x6b)) {

        for (int i = 0; i < g_player_count; i++) {
            if ((g_match_flags & 0x800000) != 0) {
                pad_id = g_pad_assignments[i];
                save_ptr = save_get_data_ptr();
                *(u8*)(save_ptr + i + 0x24) = pad_id;
                save_get_data_ptr();
            }
            if ((g_match_flags & 0x100000) == 0) {
                FUN_800ae7bc(); // rebind/input config update
            }
        }
    }

    FUN_8038273c(); // OS unlock / critical section exit
}

// ============================================================================
// 0x800a45a8 - player_apply_model_def
// Loads the player's character model definition from the model pointer table.
// If the first u16 at the model def pointer is 0, copies 0x58 bytes of raw data
// into player_data+0x18. Otherwise calls FUN_80254ed8 to resolve by ID.
// 4 xrefs - called from player_character_system_init and respawn.
// ============================================================================
void player_apply_model_def(int player_data) {
    u16* model_def_ptr = *(u16**)(player_data + 0x1398);
    u32 model_id;

    if (model_def_ptr == NULL) {
        model_id = 1;
    } else {
        model_id = (u32)*model_def_ptr;
    }

    if (model_id == 0) {
        // Raw inline model data - copy 0x58 bytes from def+4 into player render state
        leaf_31e8(player_data + 0x18, (u32)(model_def_ptr + 2), 0x58);
    } else {
        // Resolve model by ID from the character table
        FUN_80254ed8(model_id, (u32*)(player_data + 0x18));
    }
}

// ============================================================================
// 0x800a4608 - player_clear_input_buffers
// Zeroes out the player's input history ring buffer (0x3e entries * 0x10 bytes)
// and 6 weapon input slots at offset 0xec8 (each 0xcc bytes apart).
// Also clears the input frame counter at offset 0xec0.
// 2 xrefs - called from player_character_system_init and player_respawn.
// ============================================================================
void player_clear_input_buffers(int player_data) {
    int* input_buffer = *(int**)(player_data + 0xe0c);

    // Clear 0x3e input history entries (16 bytes each)
    for (int i = 0; i < 0x3e; i++) {
        *(u32*)((u8*)input_buffer + i * 0x10) = 0;
    }

    // Clear 6 weapon input slots
    int offset = 0xec8;
    for (int i = 0; i < 6; i++) {
        *(u32*)(player_data + offset) = 0;
        offset += 0xcc;
    }

    // Clear input frame counter
    *(u32*)(player_data + 0xec0) = 0;
}

// ============================================================================
// 0x800a4654 - player_character_system_init
// MASSIVE function (2 callers: player_character_system_alloc, FUN_801c197c).
// Initializes all player character slots for a match:
// - Zeroes all per-player data (0x141c * player_count)
// - Assigns player indices, allocates camera slots (player_slot_alloc)
// - Loads character model via player_apply_model_def
// - Sets up LOD distances, visibility masks, collision radii
// - Spawns the character entity (entity_spawn_by_type)
// - Initializes weapon systems (FUN_801af8e4 x3 for 3 weapon layers)
// - Configures damage splash radius based on mode
// - Sets default movement/physics constants (speed, gravity, jump)
// - Calls player_set_character_scale for non-story modes
// - Initializes sound/animation state for each player
// ============================================================================
void player_character_system_init(void) {
    int player_index;
    int* player_data;
    int entity_id;
    int char_data_ptr;
    int model_offset;
    u8 character_id;
    char controller_slot;
    float zero_f = 0.0f;
    float one_f = 1.0f;
    float default_speed;
    float default_jump;

    FUN_803826dc(); // OS lock

    g_active_player_data_ptr = 0;
    DAT_806118e8 = 0;

    FUN_8025a41c(0, 0, 0, 0, 0); // animation system reset

    // Zero all player data blocks
    memset_wrapper(g_player_data_pool, 0, g_player_count * 0x141c);
    memset_wrapper(g_player_input_pool, 0, g_player_count * 0x3e0);

    // Link input buffer pointers into player data
    int data_offset = 0;
    int input_offset = 0;
    for (int i = 0; i < g_player_count; i++) {
        *(int*)(g_player_data_pool + data_offset + 0xe0c) = g_player_input_pool + input_offset;
        data_offset += 0x141c;
        input_offset += 0x3e0;
        *(u32*)(g_player_data_pool + data_offset - 0x141c + 4) = 0xffffffff;
        *(u32*)(g_player_data_pool + data_offset - 0x141c + 8) = 0;
    }

    // Assign controller indices from FUN_801c1714 (get connected pad count)
    data_offset = 0;
    int pad_count = FUN_801c1714();
    for (int i = 0; i < pad_count; i++) {
        *(int*)(g_player_data_pool + data_offset + 4) = i;
        data_offset += 0x141c;
    }

    // Allocate camera/player slots
    data_offset = 0;
    pad_count = FUN_801c1714();
    for (int i = 0; i < pad_count; i++) {
        int slot = player_slot_alloc(1);
        *(int*)(g_player_data_pool + data_offset + 8) = slot;
        data_offset += 0x141c;
    }

    // Validate slot allocation
    data_offset = 0;
    for (int i = 0; i < g_player_count; i++) {
        if (*(int*)(g_player_data_pool + data_offset + 8) == 0) {
            *(u32*)(g_player_data_pool + data_offset + 8) = 0;
        }
        data_offset += 0x141c;
    }

    // Per-player full initialization
    data_offset = 0;
    for (int i = 0; i < g_player_count; i++) {
        g_active_player_data_ptr = 0;
        player_data = (int*)(g_player_data_pool + data_offset);
        *player_data = i; // player index

        if ((player_data[1] >= 0) && (player_data[2] != 0)) {
            FUN_80259354(); // camera pre-init
            player_slot_reset_interpolation(player_data[2], 1);
        }

        player_data[0x4e7] = 0x7fffffff; // invulnerability timer (max = disabled)

        if (((g_match_flags & 0x20000000) == 0) &&
            (5 < (unsigned)(g_game_mode - 0x65)) &&
            (g_game_mode != 0x6b)) {

            // Active gameplay mode - spawn character entity
            if ((g_match_flags & 0x800000) == 0) {
                controller_slot = g_controller_slots[i];
                character_id = g_pad_assignments[i];
                int save_block = savegame_get_player_block(i);
                player_data[0x4e6] = save_block;
            } else {
                int save_ptr = save_get_data_ptr();
                player_data[0x4e6] = save_ptr + 0xa8;
                *(u16*)(player_data[0x4e6] + 2) &= 0xffaf;
                save_ptr = save_get_data_ptr();
                character_id = *(u8*)(i + save_ptr + 0x24);
                save_ptr = save_get_data_ptr();
                controller_slot = *(char*)(i + save_ptr + 0x4e);
            }

            player_apply_model_def((int)player_data);

            // Spawn character entity
            model_offset = (u32)character_id * 0x30;
            entity_id = FUN_800acdac(0.0f, 0.0f, 0.0f, 1.0f,
                                     character_id, character_id, i, -1, 1, (int)controller_slot);
            player_data[5] = entity_id;

            // Configure LOD distances from model data
            double lod_base = render_entity_get_lod_distance(*(int*)(*(int*)(player_data[5] + 4) + 4));
            // ... (LOD calculations, visibility mask, collision setup)

            // Set up damage splash radius based on mode
            // Configure weapon layers (3 layers)
            // Spawn paired entity (shadow/marker)
            // Initialize physics constants

            player_clear_input_buffers((int)player_data);

            // Set character scale based on mode
            if (g_game_mode != 0x0a) { // not story mode
                double char_height = FUN_803641f8(*(short*)(*(int*)(player_data[5] + 0x7c) + 0x48));
                player_set_character_scale(char_height, (int)player_data);
            }
        } else {
            // Spectator/inactive - clear entity reference
            player_data[5] = 0;
            // Initialize weapon layers with null
            // Set zero position/velocity
        }

        // Common initialization for all players
        player_data[0x47] = 0;     // state flags
        player_data[0x48] = 0;     // velocity X (float as int)
        player_data[0x49] = 0;     // velocity Z
        player_data[0x45] = 2;     // default state
        player_data[0x44] = 1;     // alive flag
        player_data[0x46] = 0;     // death timer
        // ... (remaining field initialization)

        data_offset += 0x141c;
    }

    // Set global active player pointer
    g_active_player_data_ptr = g_player_data_pool;
    g_active_camera_slot = *(u32*)(g_player_data_pool + 8);

    FUN_80254cc4(); // post-init finalization
    FUN_80382728(); // OS unlock
}

// ============================================================================
// 0x800a4fb0 - player_set_character_scale
// Sets the player's collision height and eye height based on character model
// height. In story mode uses fixed values; in MP uses height-proportional
// scaling with bounds from the character table.
// 6 xrefs - called from init, respawn, and virus mode (shrink).
// ============================================================================
void player_set_character_scale(double char_height, int player_data) {
    float height_mp = FLOAT_80614800;   // MP default height
    float eye_mp = FLOAT_80614804;      // MP default eye height
    float eye_story = FLOAT_806147e4;   // story eye height

    if (g_game_mode == 0x0a) { // story mode
        *(float*)(player_data + 0x70) = FLOAT_806147dc; // fixed collision height
        *(float*)(player_data + 0x78) = eye_story;      // fixed eye height
    } else if (((g_match_flags & 0x80000) == 0) &&
               ((*(u32*)(*(int*)(*(int*)(player_data + 0x14) + 0x7c) + 0xc6c) & 0x1000) == 0) &&
               (g_game_sub_mode != 0x0c)) {
        // Standard MP - use table values
        *(float*)(player_data + 0x70) = height_mp;
        *(float*)(player_data + 0x78) = eye_mp;
    } else {
        // Scaled mode (shrink/grow) - interpolate based on character height
        float scale_factor = 1.0f;
        float height_scale = FLOAT_80614808;
        *(float*)(player_data + 0x70) = scale_factor * (float)(char_height - (double)height_mp) + height_mp;
        *(float*)(player_data + 0x78) = scale_factor * (float)((double)(height_scale * (float)char_height) - (double)eye_mp) + eye_mp;
    }

    FUN_8025b9a8(); // update derived collision volumes
}

// ============================================================================
// 0x800a5064 - player_character_system_alloc
// Allocates memory pools for the player character system:
// - Main player data: player_count * 0x141c bytes (aligned to 16)
// - Secondary data:   player_count * 0x238 bytes (aligned to 16)
// - Input buffers:    player_count * 0x3e0 bytes
// Then calls player_character_system_init to populate.
// 1 xref (from FUN_801c1a3c - level load sequence).
// ============================================================================
void player_character_system_alloc(void) {
    // Allocate main player data pool (0x141c per player, 16-byte aligned)
    g_player_data_pool = mem_alloc((g_player_count * 0x141c + 0xf) & 0xfffffff0, 1);

    // Allocate secondary per-player data (0x238 per player, 16-byte aligned)
    g_player_secondary_pool = mem_alloc((g_player_count * 0x238 + 0xf) & 0xfffffff0, 1);
    u32 sec_size = (g_player_count * 0x238 + 0xf) & 0xfffffff0;
    memset_wrapper(g_player_secondary_pool, 0, sec_size);

    // Allocate input ring buffers (0x3e0 per player)
    g_player_input_pool = mem_alloc(g_player_count * 0x3e0, 1);

    FUN_8025b928(); // input system init
    player_character_system_init();
}

// ============================================================================
// 0x800a50f4 - player_respawn
// Resets a player to spawn state after death:
// - Clears movement state, physics, weapon slots
// - Destroys attached effect entities (muzzle flash, etc)
// - Resets LOD/render distances
// - Calls player_on_spawn for position/zone/weapon selection
// - Clears input buffers and resets velocity
// - Re-applies character scale
// - Handles special mode logic (virus transfer, spectator, story)
// 3 xrefs: game_phase_state_machine, level_spawn_all_entities, FUN_800a7b6c.
// ============================================================================
void player_respawn(void) {
    int* player_data;
    int entity_data;
    int render_instance;
    int* char_data;

    FUN_803826e8(); // OS lock + get active player from context

    // ... (retrieve player_data from context register)

    char_data = *(int**)(player_data[5] + 0x7c);
    render_instance = *(int*)(player_data[5] + 4);

    player_data[0x45] = 2;  // state = alive
    player_data[0x44] = 1;  // alive flag
    player_data[0x46] = 0;  // death timer clear

    // Set entity flags for active gameplay
    *(u32*)(player_data[5] + 0x28) |= 0x161;

    // Clear velocities
    player_data[0x47] = 0;
    player_data[0x48] = 0; // vel X
    player_data[0x49] = 0; // vel Z
    player_data[0x394] = 0;

    // Destroy attached effect entities (muzzle flash, trails)
    if (entity_data != 0) {
        if ((u32*)char_data[0xaf] != NULL) {
            entity_destroy_with_event((u32*)char_data[0xaf]);
            char_data[0xaf] = 0;
        }
        if ((u32*)char_data[99] != NULL) {
            entity_destroy_with_event((u32*)char_data[99]);
            char_data[99] = 0;
        }
    }

    // Reset weapon state
    FUN_800aecf8(char_data);
    FUN_800adce4();
    FUN_8011bb3c((int)player_data);
    spawn_clear_player_slot_data(*player_data);

    // Handle teleport flag - reassign room
    if ((player_data[3] & 0x10000) != 0) {
        *(u32*)(player_data[5] + 0x34) = 0;
        *(u8*)(player_data[5] + 0x30) = 0;
        entity_assign_room(player_data[5]);
    }

    // Clear state flags
    player_data[3] &= 0xfffef5cf;

    // Recalculate LOD near distance
    double lod_dist = render_entity_get_lod_distance(*(int*)(render_instance + 4));
    player_data[0x23] = (int)(float)(FLOAT_806147c4 * lod_dist);

    FUN_801af9f4((int)player_data); // weapon layer reset

    // Clear movement/aim state
    player_data[0x37d] = 0;
    player_data[0x37f] = 0; // aim pitch
    player_data[0x380] = 0; // aim yaw
    player_data[0x381] = 0;
    player_data[0x382] = 0;
    player_data[0x39d] = 0;
    player_data[0x39e] = 0;
    player_data[0x385] = 0;
    player_data[0x386] = *(int*)&one_f; // 1.0f
    player_data[0x37e] = -1;
    *(u16*)(player_data + 0x4e8) = 0;
    player_data[0x501] = 0;

    FUN_802591f0((int)player_data); // HUD state reset
    player_on_spawn();              // position + weapon + zone setup
    player_clear_input_buffers((int)player_data);

    // Sync entity position from player state
    *(int*)(player_data[5] + 0x64) = player_data[0x2a]; // pos X
    *(int*)(player_data[5] + 0x68) = player_data[0x2b]; // pos Y
    *(int*)(player_data[5] + 0x6c) = player_data[0x2c]; // pos Z

    // Clear acceleration
    player_data[0x395] = 0;
    player_data[0x396] = 0;
    player_data[0x27] = 0;
    player_data[0x3d] = 0;
    player_data[0x3e] = 0;
    player_data[0x3f] = 0;

    // Re-apply character scale for non-story modes
    if (g_game_mode != 0x0a) {
        double char_height = FUN_803641f8(*(short*)(*(int*)(player_data[5] + 0x7c) + 0x48));
        player_set_character_scale(char_height, (int)player_data);
    }

    // Mode-specific post-respawn logic
    if (g_game_sub_mode != 0x08) {
        FUN_800241d8((int)char_data); // room visibility update
    }

    FUN_80106344(player_data[1], -1); // HUD notification clear

    // Story mode coop respawn
    if ((player_data[1] >= 0) && (entity_data == 0) && (g_game_mode == 0x03)) {
        FUN_8022df38((int)player_data); // coop partner spawn
    }

    FUN_80382734(); // OS unlock
}

// ============================================================================
// 0x800a5470 - player_notify_room_change_on_death
// When a player dies and respawns in a different room, displays a HUD message
// showing which room the killer was in (string ID 0x15ef).
// Only in non-story modes. Skipped in flag 0x800000 (custom/online) mode.
// 1 xref - called from character_on_death_handler.
// ============================================================================
void player_notify_room_change_on_death(int killed_player, int killer_player) {
    if ((g_game_mode != 0x0a) && (*(int*)(killed_player + 8) != 0)) {
        int killer_room = *(int*)(killer_player + 8);
        int player_room = *(int*)(*(int*)(*(int*)(killed_player + 0x14) + 0x7c) + 8);

        if ((killer_room != player_room) && ((g_match_flags & 0x800000) == 0)) {
            int msg_str = get_string_by_id(0x15ef); // "Killed by player in room %d"
            FUN_8010621c(*(u32*)(killed_player + 4), msg_str, killer_room);
        }
    }
}

// ============================================================================
// 0x800a5504 - player_show_respawn_location
// Shows the respawn location HUD message to the player.
// In story mode shows a fixed string (0xe3d). In MP, resolves the spawn entity
// and displays a room notification (string 0x189c) if the player respawned
// in a different room from their last position.
// 1 xref - called from FUN_800a583c (post-respawn HUD update).
// ============================================================================
void player_show_respawn_location(int player_data) {
    if (*(int*)(player_data + 8) == 0) return;

    int char_data_ptr = *(int*)(*(int*)(player_data + 0x14) + 0x7c);

    FUN_80106344(*(int*)(player_data + 4), -1); // clear previous notifications

    if (g_game_mode == 0x0a) { // story mode
        u32* checkpoint_str = (u32*)get_string_by_id(0xe3d); // "Checkpoint"
        FUN_80105d5c(*(u32*)(player_data + 4), 4, checkpoint_str);
    } else {
        // Resolve spawn entity from packed reference
        u32 spawn_ref = *(u32*)(char_data_ptr + 0x90);
        int spawn_entity = 0;

        if (spawn_ref != 0) {
            spawn_entity = g_entity_pool_base + (spawn_ref >> 22) * 0x2e8;
            // Generation check
            if (*(u32*)(spawn_entity + 0x2c) != spawn_ref) {
                spawn_entity = 0;
                *(u32*)(char_data_ptr + 0x90) = 0;
            }
        }

        if ((spawn_entity != 0) && (*(int*)(spawn_entity + 0x20) == 8)) {
            int spawn_room = *(int*)(*(int*)(spawn_entity + 0x7c) + 8);
            int player_room = *(int*)(char_data_ptr + 8);

            if ((spawn_room != player_room) && ((g_match_flags & 0x800000) == 0)) {
                int msg_str = get_string_by_id(0x189c); // "Respawned in room %d"
                FUN_8010621c(*(u32*)(player_data + 4), msg_str, spawn_room);
            }
        }
    }
}
