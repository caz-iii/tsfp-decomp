// Address: 0x800acad4-0x800ad84c | Name: player_ai_spawn | Date: 2026-08-11
// AI bot spawn, weapon classification, target validation, teleport, and voice lookup.
// Core AI character management utilities called from player_character_system_init
// and various AI subsystems.

#include "common.h"
#include "player.h"
#include "entity.h"
#include "sound.h"
#include "render.h"

// Forward declarations
extern int  entity_spawn_by_type(int charType, f32* pos, f32* rot, int a, int b);
extern int  entity_type_ensure_model_loaded(void);
extern void render_instance_set_material(f32 scale, int* renderInst, int materialId);
extern int  soundpad_resolve_zone_node(int zoneId);
extern int  soundpad_zone_node_to_index(int* node);
extern int  soundpad_find_zone_by_id(int zoneId);
extern int  soundpad_resolve_entity_to_zone(int zoneId);
extern int  soundpad_find_zone_for_entity(int entityId);
extern void soundpad_on_zone_changed(void);
extern int  soundpad_get_entity_zone_id(int* node);
extern f32  floor_query_with_interact(f32 h1, f32 h2, int posPtr, int a, int* b, int c);
extern void player_init_eye_offset(int pPlayer);
extern int  player_ai_has_alt_model(int charIdx);
extern void player_ai_spawn_death_gibs(int charIdx, int slotIdx, u32 flag);
extern void player_ai_respawn_state_reset(void);
extern int  player_ai_get_bag_entity_type(void);
extern int  gib_spawn_entity(int entity, int a, void* b, int type);
extern f32  render_entity_get_lod_distance(int modelPtr);
extern int  get_string_by_id(int stringId);

// ============================================================================
// Address: 0x800acad4
// player_ai_is_ranged_weapon - AI ranged weapon whitelist check
// Returns 1 if weapon type is in the ranged category (pistols, rifles, SMGs).
// Large switch covering ~30+ weapon IDs. 20+ callers.
// ============================================================================
int player_ai_is_ranged_weapon(int weaponType) {
    switch (weaponType) {
        // Pistols
        case 0x12: // Pistol 9mm
        case 0x13: // Silenced pistol
        // SMGs
        case 0x2D: // Tommy Gun
        case 0x34: // Sci-Fi Handgun
        case 0x36: // Soviet Rifle
        // Assault rifles
        case 0x42: // Assault rifle
        case 0x43: // Machine gun
        case 0x44: // Tactical 12-gauge
        case 0x45: // Kruger
        // Specials
        case 0x4A: // Minigun
        case 0x50: // Heatseeker
        case 0x5C: // Plasma AutoRifle
        case 0x5E: // Dispersion Gun
        case 0x5F: // Electrotool
        case 0x60: // Mag-Charger
        case 0x62: // Ghost Gun
        case 0x69: // Injector
        case 0x75: // Flare Gun
        // Future weapons
        case 0x97: // Hybrid gun
        case 0x9C: // Homing launcher
        case 0xA2: // Sci-fi Sniper
        case 0xA8: // Revolver
        case 0xA9: // Double barrel
        case 0xAA: // Crossbow
            return 1;
        default:
            return 0;
    }
}

// ============================================================================
// Address: 0x800acbe0
// player_ai_is_heavy_weapon - Heavy weapon check
// Returns 1 if weapon type is heavy (minigun 0x4A or rocket launcher 0x50).
// ============================================================================
int player_ai_is_heavy_weapon(int weaponType) {
    if (weaponType == 0x50 || weaponType == 0x4A) {
        return 1;
    }
    return 0;
}

// ============================================================================
// Address: 0x800acc08
// player_ai_is_sniper_weapon - Sniper weapon check
// Returns 1 if weapon type is sniper (0x4E). Used for engagement distance.
// ============================================================================
int player_ai_is_sniper_weapon(int weaponType) {
    if (weaponType != 0x4E) {
        return 0;
    }
    return 1;
}

// ============================================================================
// Address: 0x800acc24
// player_ai_is_melee_weapon - Melee weapon check
// Returns 1 if weapon is NOT ranged AND not 0x4D/0x4F/0x64 (special weapons).
// Used by AI combat mode selection.
// ============================================================================
int player_ai_is_melee_weapon(int weaponType) {
    int result = 0;
    int isRanged = player_ai_is_ranged_weapon(weaponType);
    if (isRanged == 0 && weaponType != 0x4D &&
        weaponType != 0x4F && weaponType != 0x64) {
        result = 1;
    }
    return result;
}

// ============================================================================
// Address: 0x800acc84
// player_ai_check_target_team - Team-based target validation
// Returns 1 if target is on same team (should not be attacked).
// Checks friendly fire flag, team ID match, entity type flag 0x8000.
// ============================================================================
u32 player_ai_check_target_team(int attacker, int target) {
    // In Virus mode with friendly fire enabled, check team match
    if (g_gameSubMode != 0x0A && (g_gameFlags & 1) != 0) {
        int attackerTeam = *(int*)(*(int*)(*(int*)(attacker + 0x14) + 0x7C) + 0x84);
        int targetTeam  = *(int*)(target + 0x84);
        if (targetTeam == attackerTeam) {
            return 1; // Same team: don't attack
        }
    }

    // Not virus mode and no team-flag mode
    if (g_gameSubMode != 0x0A && (g_gameFlags & 0x40) == 0) {
        return 0;
    }

    // Check entity type flag 0x8000 from character table
    s16 charType = *(s16*)(target + 0x48);
    return *(u32*)(g_characterTable + charType * 0x14) & 0x8000;
}

// ============================================================================
// Address: 0x800acd08
// player_ai_target_is_available - Target availability check
// Rejects turret type 0x15 without flag 0x80, rejects invulnerable targets.
// ============================================================================
int player_ai_target_is_available(int target) {
    // Reject turret entities (type 0x15) unless flagged 0x80
    if (*(s16*)(target + 0x48) == 0x15 && (*(u32*)(target + 0x10) & 0x80) == 0) {
        return 0;
    }

    // Check invulnerability timer
    u32 flags;
    if (g_gameSubMode == 0x0A) {
        flags = *(u32*)(target + 0x14);
    } else {
        flags = *(u32*)(target + 0x1C);
    }

    if ((flags & 2) != 0 && g_spawnInvulnFrames <= *(int*)(target + 8)) {
        return 0; // Still invulnerable
    }

    return 1;
}

// ============================================================================
// Address: 0x800acdac
// player_ai_spawn_bot - MASSIVE AI bot spawn
// Creates entity, initializes 0xEA0-byte character slot, classifies weapon type
// for AI behavior, assigns name string, sets material based on character type
// (large switch), computes LOD distances, assigns zone, optionally spawns bag
// entity for capture game modes. Called from player_character_system_init and
// various spawner systems.
// ============================================================================
void player_ai_spawn_bot(int charIdx, int slotIdx, f32 yaw,
                         int spawnZoneId, int playerId, int skinVariant) {
    int charTableEntry = g_characterTableBase + charIdx * 0x30;
    int modelIdx = *(int*)(charTableEntry);

    f32 spawnPos[3] = { 0.0f, 0.0f, 0.0f };
    f32 spawnRot[3] = { 0.0f, yaw, 0.0f };
    int zoneIndex = -1;

    // Determine if this character should have death gibs
    int charDataEntry = g_characterDataBase + *(s16*)(g_charModelTable + modelIdx * 2) * 0x30;
    u32 shouldHaveGibs = 0;

    if ((g_gameFlags & 0x40) == 0) {
        if (g_levelId == 100 || g_levelId == 0x38) {
            shouldHaveGibs = 0;
        } else if (g_gameSubMode == 0x0A || g_levelId == 0x66) {
            shouldHaveGibs = *(u32*)(charDataEntry + 0x28) & 0x2000000;
        } else {
            shouldHaveGibs = 0;
        }
    }

    u32 gibFlag = 0;
    if (shouldHaveGibs != 0) {
        gibFlag = countLeadingZeros(*(u32*)(charDataEntry + 4)) >> 5;
    }

    // Spawn entity at zone or default position
    int entity;
    if (spawnZoneId < 0) {
        entity = entity_spawn_by_type(modelIdx, &spawnPos[0], &spawnRot[0], 0, 0);
    } else {
        int* zoneNode = soundpad_resolve_entity_to_zone(spawnZoneId);
        if (zoneNode == NULL) {
            entity = entity_spawn_by_type(modelIdx, &spawnPos[0], &spawnRot[0], 0, 0);
        } else {
            spawnRot[1] = *(f32*)(&zoneNode[6]); // Facing from zone
            entity = entity_spawn_by_type(modelIdx, (f32*)(zoneNode + 3), &spawnRot[0], 0, 0);
            if ((zoneNode[1] & 0x800) == 0) {
                zoneIndex = soundpad_zone_node_to_index(zoneNode);
            } else {
                zoneIndex = soundpad_find_zone_by_id((int)*(s16*)(zoneNode + 2));
            }
        }
    }

    if (entity == 0) return; // Spawn failed

    // Set up death gibs if character has alt model
    if (player_ai_has_alt_model(charIdx)) {
        player_ai_spawn_death_gibs(charIdx, slotIdx, gibFlag);
    }

    // Configure entity flags
    *(u32*)(entity + 0x28) |= 0x161; // AI + active + visible
    if (playerId < g_spawnInvulnFrames) {
        *(u32*)(entity + 0x28) |= 0x10000000; // Invulnerable at spawn
    }

    // Configure render instance
    int* renderInst = *(int**)(entity + 4);
    *(u32*)(renderInst[0] + 0x30) |= 0xE000; // Render flags
    *(int*)(entity + 0x74) = *(int*)(entity + 4); // backup render ptr
    *(int*)(entity + 0xA4) = 0;
    *(f32*)(entity + 0x1D8) = g_aiDefaultScale;

    // Link character data slot
    *(int*)(entity + 0x7C) = g_charSlotBase + playerId * 0xEA0;
    int* charData = *(int**)(entity + 0x7C);
    charData[0] = entity;
    charData[2] = playerId;
    *(s16*)(charData + 0x12) = (s16)charIdx;
    *(s16*)((int)charData + 0x4A) = (s16)slotIdx;
    charData[0x11] = 0;         // Weapon classification
    charData[0x3A4] = -1;
    charData[0x3A5] = 0;
    charData[0x3A6] = 0;
    charData[0x2E7] = 1;        // Active flag
    charData[0x2E5] = (int)g_aiDefaultLodBias;

    // Classify weapon for AI behavior
    if (g_gameSubMode == 0x0A) {
        // Virus mode: classify based on weapon slot
        if (g_spawnInvulnFrames <= charData[2]) {
            if (player_ai_is_valid_weapon_type(slotIdx)) {
                charData[0x11] = 1; // Valid primary
            } else if (player_ai_is_ranged_weapon(slotIdx)) {
                charData[0x11] = 3; // Ranged
            } else if (player_ai_is_sniper_weapon(slotIdx)) {
                charData[0x11] = 2; // Sniper
            }
        }
    } else {
        charData[0x2FC] = (int)g_aiDefaultBehaviorTable;
    }

    // Assign name string
    charData[0x21] = 0;
    int nameId = ai_get_name_string_id(charIdx, skinVariant);
    charData[0x1C] = get_string_by_id(nameId);

    // Clear score/stats
    memset_wrapper((int)(charData + 0x284), 0, 0x42);
    *(s16*)((int)charData + 0xA52) = 0;
    charData[0x299] = 0;
    charData[0x29A] = 0;
    charData[0x29B] = 0;
    charData[0x29C] = 0;

    // Team assignment
    if (g_teamSize != 0) {
        *(s16*)(charData + 0x298) = (s16)g_teamSize;
    }

    // Initialize AI state machine
    ai_state_machine_init(charData + 0x31A, charData);
    charData[0x19E] = zoneIndex;

    // Reset respawn state
    player_ai_respawn_state_reset();

    // Disable entity for non-player bots initially
    if (g_spawnInvulnFrames <= playerId) {
        *(u32*)(entity + 0x28) &= ~1u; // Deactivate until needed
    }

    // Compute LOD distances from model
    f32 lodDist = render_entity_get_lod_distance(*(int*)(*(int*)(entity + 0x24) + 4));
    charData[0x2C3] = (int)lodDist;
    charData[0x2C2] = (int)lodDist;

    // Store in character height table
    s16 charTypeIdx = *(s16*)(charData + 0x12);
    f32* heightEntry = (f32*)(g_charHeightTable + charTypeIdx * 0x30);
    heightEntry[0] = g_lodDistMultiplier * (f32)charData[0x2C2];
    if (heightEntry[0] > g_maxLodDistance) {
        heightEntry[0] = g_maxLodDistance;
    }
    *(int*)(g_charHeightTable + charTypeIdx * 0x30 + 4) = charData[0x2C2];
    *(f32*)(g_charHeightTable + charTypeIdx * 0x30 + 8) = g_lodFarMultiplier * (f32)charData[0x2C2];
    *(int*)(g_charHeightTable + charTypeIdx * 0x30 + 0xC) = charData[0x2C2];

    // Store initial position for respawn reference
    charData[0x2ED] = *(int*)(entity + 0x64);
    charData[0x2EE] = (int)((f32)*(int*)(entity + 0x68) + heightEntry[0]);
    charData[0x2EF] = *(int*)(entity + 0x6C);

    // Handle skin variant (multiple skins per character)
    int skinCount = (int)*(u8*)(g_charSkinCountTable + charTypeIdx * 0x30);
    if (skinCount > 1) {
        if (skinVariant < 0) {
            // Random skin
            u32 rng = random_int();
            skinVariant = rng - (rng / (skinCount - 1)) * (skinCount - 1);
        }
        if (charIdx == 0) {
            skinVariant = 1; // Force variant for player 0
        }
        *(int*)(renderInst[0] + 0x30) = 1 << skinVariant;
        *(s8*)((int)charData + 0xC0D) = (s8)skinVariant;
    }

    // Load alt death model if available
    if (g_levelId != 0x65 && (g_gameFlags & 0x20000000) == 0 &&
        *(int*)(g_charAltModelTable + charIdx * 0x30) != -1) {
        int altModel = entity_type_ensure_model_loaded();
        if (altModel != 0) {
            *(int*)(renderInst[0] + 8) = altModel;
        }
    }

    // Assign material based on character model type (LARGE switch)
    int* instPtr = *(int**)(entity + 4);
    int modelType = *(int*)(instPtr[0x20] + 4);
    player_ai_assign_material_by_model_type(instPtr, modelType, charData);

    // Capture mode: spawn bag entity
    if (g_levelId == 0x65) {
        int bagType = player_ai_get_bag_entity_type();
        if (bagType == -1 || (playerId < g_spawnInvulnFrames || playerId > 0x21) &&
            g_levelId != 0x65) {
            charData[0x2F1] = 0;
        } else {
            charData[0x2F1] = gib_spawn_entity(entity, 0, NULL, 5);
        }
    }
}

// ============================================================================
// Address: 0x800ad528
// player_ai_teleport_wrapper - Thin wrapper for teleport
// Called from spawn_teleport_pad_activate.
// ============================================================================
void player_ai_teleport_wrapper(int param1, int param2, u32 flags) {
    player_ai_teleport_to_zone(param1, param2, flags);
}

// ============================================================================
// Address: 0x800ad54c
// player_ai_teleport_to_zone - MASSIVE AI bot teleport/respawn
// Resolves zone node, copies position, queries floor height, assigns room,
// handles zone change notifications and soundpad updates.
// Two paths: direct entity (no player controller) vs player-controlled.
// ============================================================================
void player_ai_teleport_to_zone(int param1, int param2, u32 flags) {
    int* charData = ai_get_char_data(); // upper half of return
    int slotIdx = ai_get_slot_idx();    // lower half

    int* zoneNode = soundpad_resolve_zone_node(slotIdx);
    charData[0x19E] = slotIdx;

    if (*(int**)(*charData + 0x150) == NULL) {
        // Direct entity path (no player controller)
        int* zNode = soundpad_resolve_zone_node(slotIdx);
        int entity = *charData;
        charData[9] = 1; // Mark dirty

        // Copy zone position to entity
        int px = zNode[3], py = zNode[4], pz = zNode[5];
        *(int*)(entity + 0x64) = px;
        *(int*)(entity + 0x68) = py;
        *(int*)(entity + 0x6C) = pz;
        *(int*)(entity + 0xEC) = px;
        *(int*)(entity + 0xF0) = py;
        *(int*)(entity + 0xF4) = pz;
        charData[0x2E8] = px;
        charData[0x2E9] = py;
        charData[0x2EA] = pz;

        // Handle facing direction
        if ((flags & 2) != 0) {
            *(f32*)(entity + 0xC4) = param1; // Use provided yaw
            *(f32*)(entity + 0x70) = param1;
        } else if ((flags & 1) != 0) {
            int yaw = zNode[6];
            *(int*)(entity + 0xC4) = yaw;
            *(int*)(entity + 0x70) = yaw;
        }

        // Query floor unless flag 4 set
        if ((flags & 4) == 0) {
            s16 charType = *(s16*)(charData + 0x12);
            f32 h1 = *(f32*)(g_charHeightTable + charType * 0x30);
            f32 h2 = *(f32*)(g_charHeightTable + charType * 0x30 + 8);
            f32 floor = floor_query_with_interact(h1, h2, entity + 0x64, 0, NULL, 0);
            charData[0x2DC] = (int)floor;
            charData[0x2FF] = (int)floor;
        }

        // Room assignment from zone
        *(int*)(entity + 0x34) = (int)*(s16*)((int)zNode + 10);
    } else {
        // Player-controlled path
        int playerSlot = **(int**)(*charData + 0x150);
        int pOffset = g_playerDataBase + playerSlot * 0x141C;
        int prevZone = *(int*)(pOffset + 0x1400);

        // Set position from zone
        *(int*)(pOffset + 0xA8) = zoneNode[3];
        *(int*)(pOffset + 0xAC) = zoneNode[4];
        *(int*)(pOffset + 0xB0) = zoneNode[5];

        // Room from zone
        *(int*)(*(int*)(pOffset + 0x14) + 0x34) = (int)*(s16*)((int)zoneNode + 10);

        // Re-init eye offset
        player_init_eye_offset(pOffset);
        *(f32*)(pOffset + 0x104) = 0.0f;

        // Handle facing
        if ((flags & 2) != 0) {
            *(f32*)(pOffset + 0x100) = (f32)param1;
            *(f32*)(*(int*)(pOffset + 0x14) + 0x70) = (f32)param1;
        } else if ((flags & 1) != 0) {
            int yaw = zoneNode[6];
            *(int*)(pOffset + 0x100) = yaw;
            *(int*)(*(int*)(pOffset + 0x14) + 0x70) = yaw;
        }

        // Query floor
        if ((flags & 4) == 0) {
            s16 charType = *(s16*)(charData + 0x12);
            f32 h1 = *(f32*)(g_charHeightTable + charType * 0x30);
            f32 h2 = *(f32*)(g_charHeightTable + charType * 0x30 + 8);
            f32 floor = floor_query_with_interact(h1, h2, *(int*)(pOffset + 0x14) + 0x64, 0, NULL, 0);
            charData[0x2DC] = (int)floor;
            charData[0x2FF] = (int)floor;
        }

        // Find new zone
        int newZone = soundpad_find_zone_for_entity((int)*(s16*)(zoneNode + 2));

        // Update camera yaw if attached
        int camEntity = *(int*)(pOffset + 8);
        if (camEntity != 0) {
            camera_set_yaw(0.0f, *(f32*)(*(int*)(pOffset + 0x14) + 0x70), (f32*)(camEntity + 0x2D8));
        }

        // If zone changed, notify all players
        if (prevZone != newZone) {
            *(int*)(pOffset + 0x1400) = newZone;
            int offset = 0;
            for (int i = 0; i < g_spawnInvulnFrames; i++) {
                int* node = soundpad_resolve_zone_node(*(int*)(g_charSlotBase + offset + 0x678));
                int nodeZoneId = soundpad_get_entity_zone_id(node);
                soundpad_find_zone_for_entity(nodeZoneId);
                offset += 0xEA0;
            }
            soundpad_on_zone_changed();
        }
    }

    // Update zone index from node flags
    BOOL useNodeZone = FALSE;
    if (((u32)zoneNode[1] >> 11 & 1) == 0 && (zoneNode[1] & 0x400000) == 0) {
        useNodeZone = TRUE;
    }
    if (!useNodeZone) {
        int resolvedZone = soundpad_find_zone_by_id((int)*(s16*)(zoneNode + 2));
        charData[0x19E] = resolvedZone;
    }
}

// ============================================================================
// Address: 0x800ad84c
// player_ai_get_voice_sound_id - Character -> voice bank lookup
// Maps character type indices to sound bank IDs (range 0x5C2-0x628).
// Returns -1 for unmapped character types.
// ============================================================================
int player_ai_get_voice_sound_id(int charType) {
    switch (charType) {
        case 0x16: return 0x5C2;  // Generic soldier
        case 0x17: return 0x5CA;  // Female agent
        case 0x18: return 0x5C7;  // Male agent
        case 0x1D: return 0x5CF;  // Robot
        case 0x21: return 0x5D2;  // Zombie
        case 0x23: return 0x5D7;  // Alien
        case 0x25: return 0x5E1;  // Monkey
        case 0x26: return 0x5DC;  // Creature
        case 0x27: return 0x5E4;  // Commander
        case 0x29: return 0x5E7;  // Boss
        case 0x2A: return 0x5ED;  // Special
        case 0x5F: return 0x628;  // Unique
        default:   return -1;
    }
}
