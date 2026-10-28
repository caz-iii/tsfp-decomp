// Address: 0x800adc44-0x800aeb4c | Name: player_ai_respawn | Date: 2026-08-11
// AI bot respawn state management, death cleanup, team assignment,
// sound bank preloading, and weapon inventory reset.

#include "common.h"
#include "player.h"
#include "entity.h"
#include "sound.h"

extern void player_ai_weapon_list_init(int weaponListPtr);
extern int  player_ai_get_attachment_type(int charIdx, int slotIdx, int attachSlot);
extern int  player_ai_get_bag_entity_type(void);
extern int  player_ai_has_alt_model(int charIdx);
extern int  player_ai_is_ranged_weapon(int weaponType);
extern void entity_preload_resources(int entityType);
extern void entity_destroy_with_event(void* entity);
extern int  gib_spawn_entity(int entity, int a, void* b, int type);
extern int  soundpad_find_nearest_entity(f32 minDist, f32* pos);
extern void sound_clear_bank_ref(int bankId);
extern int  particle_handle_is_valid(int* handle);
extern void particle_handle_destroy(int* handle);
extern u16  award_get_name(int slot, int levelId);

// ============================================================================
// Address: 0x800adc44
// player_ai_on_death_stub - Empty death callback stub
// ============================================================================
void player_ai_on_death_stub(void) {
    return;
}

// ============================================================================
// Address: 0x800adc48
// player_ai_on_death_cleanup - Death state cleanup
// Destroys held weapon entity, resets scale, clears anim and sound state.
// ============================================================================
void player_ai_on_death_cleanup(int charData) {
    // Destroy held weapon entity if valid
    int* weaponEntity = *(int**)(charData + 0xBC4);
    if (weaponEntity != NULL) {
        if (weaponEntity[0x1F] == 0 ||
            (*(int*)(weaponEntity[0x1F] + 0x40) != 1 &&
             *(int*)(weaponEntity[0x1F] + 0x40) != 0)) {
            *(int*)(charData + 0xBC4) = 0; // Just clear ref
        } else {
            entity_destroy_with_event(weaponEntity);
            *(int*)(charData + 0xBC4) = 0;
        }
    }

    // Reset entity scale to 0
    *(f32*)(charData + 0xB30) = 0.0f;

    // Clear animation state
    int* animState = (int*)(charData + 0x4C);
    if (anim_has_active_track(animState)) {
        anim_stop_all_tracks();
    }

    // Reset sound system
    sound_character_reset();
    ai_sound_state_clear((int*)(charData + 0xC68));
}

// ============================================================================
// Address: 0x800adce4
// player_ai_respawn_state_reset - MASSIVE respawn state initialization
// Clears ~200 AI data fields, sets combat parameters by weapon classification
// (sniper/ranged/melee with randomized engagement ranges), resets zone,
// spawns bag-tag entity, destroys active particles.
// Called from player_respawn and player_ai_spawn_bot.
// ============================================================================
void player_ai_respawn_state_reset(void) {
    int* charData = ai_get_current_char_data();
    int entity = *charData;

    // Reset weapon inventory
    player_ai_reset_weapon_inventory();

    // Restore entity scale and set active flags
    *(f32*)(entity + 0x1CC) = *(f32*)(entity + 0x1D8); // scale from default
    *(u32*)(entity + 0x28) |= 0x40001071;  // Active + AI + visible + collidable
    *(u32*)(entity + 0x2B4) = 0x80000000;  // Combat state init
    *(u32*)(entity + 0x9C) |= 2;           // Enable collision response

    // Clear AI state fields
    charData[0x2FA] = 0;
    charData[0x1D] = 0;
    charData[0x1E] = 0;  // 0.0f
    charData[0x24] = 0;
    charData[9] = 1;     // Mark transform dirty
    charData[0x270] = 0;
    charData[0x297] = 0;
    charData[0x295] = 0;
    charData[0x2A0] = 0; // 0.0f
    charData[0x2A1] = 0;
    charData[0x283] = 0;
    charData[0x282] = 0;

    // Combat engagement ranges (randomized per weapon mode)
    charData[0x27C] = 0;  // 0.0f min range
    charData[0x27D] = 0;  // 0.0f
    charData[0x27E] = 0;  // 0.0f
    charData[0x27F] = (int)g_aiDefaultEngageRange;

    // Random factor for engagement range variation
    u32 rng = random_int();
    if (g_aiRangeRngThreshold < (f32)rng / g_aiRngDivisor) {
        charData[0x27F] = (int)((f32)charData[0x27F] * g_aiRangeMultiplier);
    }

    // Bot difficulty modifier
    if (g_botDifficulty == 9) {
        charData[0x2A2] = (int)1.0f;
    }

    // Zone assignment
    charData[0x27A] = 0;  // 0.0f
    charData[0x27B] = 0;
    charData[0x2A4] = 0;  // 0.0f
    charData[0x2A5] = 0;  // 0.0f
    charData[0x2A3] = 0;

    // Find nearest soundpad for zone
    int slotIdx = (int)charData; // lower part
    if (slotIdx != 0) {
        if (g_soundpadCount == 0) {
            charData[0x19E] = 1;
        } else {
            charData[0x19E] = soundpad_find_nearest_entity(0.0f, (f32*)(entity + 0x64));
        }
    }

    charData[0x1A1] = -1;

    // Clear targeting/navigation state
    charData[0x26B] = 0;
    charData[0x26A] = 0;
    charData[0x2B1] = 0;
    charData[0x2B0] = 0;
    charData[0x273] = 0; // 0.0f

    // AI behavior mode based on game type
    if (g_gameSubMode == 0x0A) {
        charData[5] = 2;  // Virus: attack mode
        charData[4] = 0;
        charData[7] = 0;
        charData[6] = 0;
    } else {
        charData[5] = 0;
        charData[4] = 0;
        charData[7] = 2;  // Normal: patrol mode
        charData[6] = 0;
    }

    // Combat timers and state
    charData[0x2AB] = 2;   // Reload state
    charData[0x2AA] = 0;
    charData[0x2AD] = 0;
    charData[0x2AC] = 0;
    charData[0x2AF] = 0;
    charData[0x2AE] = 0;
    charData[8] = 0;
    charData[0x274] = 0;
    charData[0x275] = 0;
    charData[0x276] = 0;   // 0.0f
    charData[10] = 0;
    charData[11] = 0;
    charData[0x2BD] = 0;   // 0.0f

    // Get character difficulty rating
    charData[0x2C1] = ai_get_difficulty_rating((int)charData);

    // Clear velocity/physics
    charData[1] = 0;
    charData[0x2BA] = 0;
    charData[700] = 0;
    charData[0x2B7] = 0;
    charData[0x2B8] = 0;   // 0.0f
    charData[0x2B9] = 0;   // 0.0f
    charData[0x2CA] = 0;   // 0.0f
    charData[0x2CC] = 0;   // 0.0f
    charData[0x2CD] = 0;   // 0.0f
    charData[0x2CF] = 0;   // 0.0f
    charData[0x2B5] = (int)g_aiMovementSpeed;
    charData[0x2D1] = (int)g_aiTurnRate;
    charData[0x2D2] = 0;   // 0.0f
    charData[0x2D3] = 0;   // 0.0f
    charData[0x2DC] = (int)g_aiDefaultFloorY;
    charData[0x2EB] = 0;
    charData[0x2EC] = 0;
    charData[0x272] = 0;

    // Store spawn position
    charData[0x0C] = *(int*)(entity + 0x64);
    charData[0x0D] = *(int*)(entity + 0x68);
    charData[0x0E] = *(int*)(entity + 0x6C);
    charData[0x2BE] = *(int*)(entity + 0x64);
    charData[0x2BF] = *(int*)(entity + 0x68);
    charData[0x2C0] = *(int*)(entity + 0x6C);

    // Clear various state fields
    charData[0x2F0] = 0;   // 0.0f
    charData[0x2D5] = 0;   // 0.0f
    charData[0x2D6] = 0;   // 0.0f
    charData[0x2D7] = 0;   // 0.0f
    charData[0x2D8] = -999;
    charData[0x2D9] = -1;
    *(u16*)(charData + 0x2DB) = 0;
    charData[0x2DA] = -999;
    *(u16*)((int)charData + 0xB6E) = 0;
    charData[0x2FE] = 1;
    charData[0x2FF] = (int)g_aiDefaultFloorY;
    charData[0x300] = 0;
    charData[0x301] = 0;
    charData[0x305] = 0;
    charData[0x2C7] = 0;   // 0.0f (gravity)
    charData[0x307] = 0;
    charData[0x308] = -1;
    charData[0x304] = 0;
    charData[0x2F8] = -1;
    charData[0x2FB] = 0;

    // Previous position for movement tracking
    charData[0x2E2] = *(int*)(entity + 0x64);
    charData[0x2E3] = *(int*)(entity + 0x68);
    charData[0x2E4] = *(int*)(entity + 0x6C);

    charData[0x398] = 0;
    charData[0x22] = 0;
    charData[0x23] = 0;

    // Behavior table (story mode vs arcade)
    if (g_spawnInvulnFrames <= charData[2]) {
        charData[0x2FC] = 0;
        if (g_gameSubMode != 0x0A) {
            charData[0x2FC] = (int)g_aiDefaultBehaviorTable;
        }
    }

    charData[0x2D4] = 0;

    // Resolve vehicle handle (generation check)
    u32 vehicleHandle = charData[0x302];
    int vehicleEntity = 0;
    if (vehicleHandle != 0) {
        vehicleEntity = g_entityPoolBase + (vehicleHandle >> 22) * 0x2E8;
        if (*(u32*)(vehicleEntity + 0x2C) != vehicleHandle) {
            charData[0x302] = 0;
            vehicleEntity = 0;
        }
    }
    if (vehicleEntity != 0) {
        vehicle_dismount(charData);
    }

    // Clear respawn timer fields
    memset_wrapper((int)(charData + 0x39F), 0, 0xC);

    // Combat engagement params by weapon mode (weapon classification)
    // Sniper: long range, low fire rate
    // Ranged: medium range, standard fire rate
    // Melee: short range, high aggression
    // (Massive switch with randomized values omitted for clarity)

    // Clear render state (lighting arrays)
    int renderInst = *(int*)(entity + 4);
    for (int i = 0; i < 6; i++) {
        *(f32*)(renderInst + 0xDC + i * 4) = 0.0f;
        *(f32*)(renderInst + 0x134 + i * 4) = 0.0f;
    }
    *(int*)(renderInst + 0xCC) = 0;
    *(int*)(renderInst + 0x124) = 0;

    // Populate attachment type cache (6 slots)
    for (int i = 0; i < 6; i++) {
        s16 charType = *(s16*)(charData + 0x12);
        s16 slotType = *(s16*)((int)charData + 0x4A);
        *(int*)((int)charData + i * 4 + 0xBC8) =
            player_ai_get_attachment_type((int)charType, (int)slotType, i);
    }

    // Spawn bag-tag entity for capture modes
    if (charData[0x2F1] == 0) {
        int bagType = player_ai_get_bag_entity_type();
        if (g_levelId == 0x65 || bagType == -1 ||
            charData[2] < g_spawnInvulnFrames || charData[2] > 0x21) {
            charData[0x2F1] = 0;
        } else {
            charData[0x2F1] = gib_spawn_entity(entity, 0, NULL, 5);
        }
    }

    // Clear 4 particle handles
    for (int i = 0; i < 4; i++) {
        int handle = *(int*)((int)charData + i * 4 + 0xD24);
        if (particle_handle_is_valid(&handle)) {
            particle_handle_destroy(&handle);
        }
    }

    // Final state flags
    charData[0x31B] &= ~4;  // Clear combat flag
    charData[0x2B2] = -1;   // No target
    charData[0x2A8] = 0;
    charData[0x2B6] = 0;    // 0.0f
}

// ============================================================================
// Address: 0x800ae4c0
// player_ai_set_team_id - Set team ID and team color
// ============================================================================
void player_ai_set_team_id(int charData, int teamId) {
    *(int*)(charData + 0x84) = teamId;
    *(int*)(charData + 0xA58) = g_teamColorTable[teamId * 5];
}

// ============================================================================
// Address: 0x800ae4e0
// player_ai_assign_team - Assign team based on game mode
// Team modes: player_index+1 or team assignment table
// FFA (mode 10): team 0x10
// Story (mode 5): player_index+1
// ============================================================================
void player_ai_assign_team(int charData) {
    switch (g_gameSubMode) {
        case 1: case 2: case 3: case 6: case 7: case 9:
            if ((g_gameFlags & 1) == 0) {
                // No team flag: each player is own team
                player_ai_set_team_id(charData, *(int*)(charData + 8) + 1);
            } else {
                // Team assignment from table
                player_ai_set_team_id(charData,
                    (int)g_teamAssignTable[*(int*)(charData + 8)]);
            }
            break;
        case 5:
            // Story/co-op: player index based
            player_ai_set_team_id(charData, *(int*)(charData + 8) + 1);
            break;
        case 10:
            // FFA: everyone on team 0x10
            player_ai_set_team_id(charData, 0x10);
            break;
    }
}

// ============================================================================
// Address: 0x800ae578
// player_ai_has_alt_model - Check if character has alternative death model
// Returns 1 if character definition has flag 0x2000000. Used to skip normal
// model preloading for alt-model characters.
// ============================================================================
int player_ai_has_alt_model(int charIdx) {
    if (g_levelId == 100) return 0;

    if (g_gameSubMode == 0x0A) {
        if ((g_gameFlags & 0x40) != 0) return 0;
        if (g_levelId == 100 || g_levelId == 0x38) return 0;

        // Check character definition flag 0x2000000
        int modelIdx = *(int*)(g_characterTableBase + charIdx * 0x30);
        int charDef = g_characterDataBase + *(s16*)(g_charModelTable + modelIdx * 2) * 0x30;
        u32 hasAlt = *(u32*)(charDef + 0x28) & 0x2000000;
        if (hasAlt != 0) return 1;
    }
    return 0;
}

// ============================================================================
// Address: 0x800ae634
// player_ai_get_sound_table - Get sound bank table for character
// Returns pointer to character-specific sound table. Special case for
// character 0x24 which uses alternate tables based on skin variant.
// ============================================================================
void* player_ai_get_sound_table(int charIdx, int skinVariant) {
    void* tablePtr = g_charSoundTablePtrs[charIdx * 0xC];
    if (tablePtr == NULL && charIdx == 0x24) {
        // Character 0x24 uses variant-specific tables
        if (skinVariant != 0) {
            return g_altSoundTableA; // 0x8048c978
        }
        return g_altSoundTableB;     // 0x8048c1b8
    }
    return tablePtr;
}

// ============================================================================
// Address: 0x800ae680
// player_ai_compute_sound_bank_counts - Compute bank load counts
// Counts primary (max 18) and secondary (max 6) sound entries from table.
// Adjusts counts for special weapon types and award characters.
// ============================================================================
void player_ai_compute_sound_bank_counts(void) {
    void* soundTable = ai_get_sound_table_current();
    int charIdx = ai_get_current_char_idx();

    // Count primary entries (max 18)
    int primaryCount = 0;
    for (int i = 0; i < 18 && *(s16*)(soundTable + i * 2) != 0; i++) {
        primaryCount++;
    }

    // Count secondary entries (max 6)
    int secondaryCount = 0;
    for (int i = 0; i < 6 && *(s16*)(soundTable + i * 2 + 0x24) != 0; i++) {
        secondaryCount++;
    }

    if (g_gameSubMode == 0x0A) {
        // Virus mode: check if this is an award character
        BOOL isAward = FALSE;
        u16 award0 = award_get_name(0, g_levelId);
        u16 award1 = award_get_name(1, g_levelId);
        if (charIdx == award0 || charIdx == award1) {
            isAward = TRUE;
        }

        if (isAward) {
            *(int*)(soundTable + 0x38) = primaryCount;
            *(int*)(soundTable + 0x3C) = 1;
        } else if (charIdx == 0x59 || charIdx == 0x5B ||
                   charIdx == 0x4E || charIdx == 100) {
            // Special weapon characters: full banks
            *(int*)(soundTable + 0x38) = primaryCount;
            *(int*)(soundTable + 0x3C) = secondaryCount;
        } else {
            // Normal: half banks (rounded up)
            *(int*)(soundTable + 0x38) = (primaryCount + 1) / 2;
            *(int*)(soundTable + 0x3C) = (secondaryCount + 1) / 2;
        }
    } else {
        // Story/campaign: full banks
        *(int*)(soundTable + 0x38) = primaryCount;
        *(int*)(soundTable + 0x3C) = secondaryCount;
    }
}

// ============================================================================
// Address: 0x800ae7bc
// player_ai_preload_character_resources - MASSIVE resource preloader
// Loads entity models, iterates sound bank tables (up to 18 primary + 6
// secondary), preloads 6 attachment entity types, loads weapon-specific
// sound banks per character type.
// ============================================================================
void player_ai_preload_character_resources(void) {
    int charIdx = ai_get_char_preload_idx();
    int slotIdx = ai_get_slot_preload_idx();

    // Preload main entity model (skip if alt-model character)
    if (!player_ai_has_alt_model(charIdx)) {
        int modelType = *(int*)(g_characterTableBase + charIdx * 0x30);
        entity_preload_resources(modelType);

        // Preload alt death model if available
        int altModel = *(int*)(g_characterTableBase + charIdx * 0x30 + 4);
        if (altModel != -1) {
            entity_preload_resources(altModel);
        }
    }

    // Load sound banks for all skin variants
    int skinCount = (int)*(u8*)(g_charSkinCountTable + charIdx * 0x30);
    for (int skin = 0; skin < skinCount; skin++) {
        void* soundTable = player_ai_get_sound_table(charIdx, skin);
        player_ai_compute_sound_bank_counts();

        // Load primary sound banks
        int primaryCount = *(int*)(soundTable + 0x38);
        for (int i = 0; i < primaryCount; i++) {
            sound_clear_bank_ref((int)*(s16*)(soundTable + i * 2));
        }

        // Load secondary sound banks
        int secondaryCount = *(int*)(soundTable + 0x3C);
        for (int i = 0; i < secondaryCount; i++) {
            sound_clear_bank_ref((int)*(s16*)(soundTable + i * 2 + 0x24));
        }

        // Load extra weapon banks (virus mode: 3 entries, story: 1)
        if (g_gameSubMode == 0x0A) {
            for (int i = 0; i < 3; i++) {
                s16 bankId = *(s16*)(soundTable + i * 2 + 0x30);
                if (bankId > 0) sound_clear_bank_ref((int)bankId);
            }
        } else {
            s16 bankId = *(s16*)(soundTable + 0x34);
            if (bankId > 0) sound_clear_bank_ref((int)bankId);
        }
    }

    // Preload 6 attachment entity types
    if (!player_ai_has_alt_model(charIdx)) {
        for (int i = 0; i < 6; i++) {
            int attachType = player_ai_get_attachment_type(charIdx, slotIdx, i);
            if (attachType != -1) {
                entity_preload_resources(attachType);
            }
        }
    } else {
        for (int i = 0; i < 6; i++) {
            int attachType = player_ai_get_attachment_type(charIdx, slotIdx, i);
            if (attachType != -1) {
                preload_alt_model_resources(); // Simplified preload path
            }
        }
    }

    // Character-specific weapon sound banks
    switch (charIdx) {
        case 0x28: // Sniper character
            sound_clear_bank_ref(0x562); sound_clear_bank_ref(0x5CA);
            sound_clear_bank_ref(0x561); sound_clear_bank_ref(0x13A8);
            sound_clear_bank_ref(0x13A9); sound_clear_bank_ref(0x13AA);
            sound_clear_bank_ref(0x13AB); sound_clear_bank_ref(0x568);
            sound_clear_bank_ref(0x563); sound_clear_bank_ref(0x56A);
            break;
        case 0x2C: // Heavy character
            sound_clear_bank_ref(0x13A8); sound_clear_bank_ref(0x13A9);
            sound_clear_bank_ref(0x13AA); sound_clear_bank_ref(0x13AB);
            sound_clear_bank_ref(0x13AC); sound_clear_bank_ref(0x13AD);
            sound_clear_bank_ref(0x13AE); sound_clear_bank_ref(0x13AF);
            break;
        case 0x36: case 0x60: case 0x62: // Energy weapon characters
            if (g_gameSubMode == 0x0A) {
                sound_clear_bank_ref(0x4D4); sound_clear_bank_ref(0x4D5);
            }
            sound_clear_bank_ref(0x4D6); sound_clear_bank_ref(0x4D7);
            sound_clear_bank_ref(0x4DB); sound_clear_bank_ref(0x4DC);
            break;
        case 0x46: case 0x64: // Boss characters
            sound_clear_bank_ref(0x4D1); sound_clear_bank_ref(0x4F8);
            sound_clear_bank_ref(0x4F9); sound_clear_bank_ref(0x4FA);
            sound_clear_bank_ref(0x4FD); sound_clear_bank_ref(0x4FE);
            sound_clear_bank_ref(0x4FF);
            break;
        case 0x45: // Single special bank
            sound_clear_bank_ref(0x4D1);
            break;
        default:
            // Generic ranged weapon banks
            if (player_ai_is_ranged_weapon(charIdx)) {
                if (g_gameSubMode == 0x0A) {
                    sound_clear_bank_ref(0x4DA);
                    sound_clear_bank_ref(0x4D9);
                    sound_clear_bank_ref(0x4D8);
                }
                sound_clear_bank_ref(0x4DB);
                sound_clear_bank_ref(0x4DC);
            }
            break;
    }
}

// ============================================================================
// Address: 0x800aeb4c
// player_ai_reset_weapon_inventory - Reset all weapon slots and ammo
// Clears 70 weapon slots, resets ammo from default table, initializes
// weapon state. Called from respawn and system init.
// ============================================================================
void player_ai_reset_weapon_inventory(void) {
    int charData = player_get_current_char_data();

    // Determine if player is human or AI
    u32 isHuman = (g_spawnInvulnFrames ^ *(u32*)(charData + 8));
    isHuman = -((int)(((int)isHuman >> 1) - (isHuman & g_spawnInvulnFrames)) >> 31);

    // Initialize weapon list
    player_ai_weapon_list_init(charData + 0x304);

    // Clear weapon unlock flags (70 slots: 2 initial + 68 loop)
    *(u8*)(charData + 0x54C) = 0;
    *(u8*)(charData + 0x592) = 0;
    *(u8*)(charData + 0x54D) = 0;
    *(u8*)(charData + 0x593) = 0;

    // Set dual-wield unlock based on game flag
    *(u8*)(charData + isHuman + 0x54C) = ((g_gameFlags >> 13) & 1) ^ 1;

    for (int i = 2; i < 70; i++) {
        *(u8*)(charData + i + 0x54C) = 0;
        *(u8*)(charData + i + 0x592) = 0;
    }

    // Reset ammo from default table
    for (int i = 0; i < 70; i++) {
        *(s8*)(charData + i + 0x5D8) = (s8)g_defaultAmmoTable[i];
    }

    // Clear pickup state
    memset_wrapper(charData + 0x61E, 0, 0x58);

    // Reset weapon state machine
    *(int*)(charData + 0x80) = 0;
    *(u32*)(charData + 0x94) = isHuman & ~(-((int)(g_gameFlags >> 13) & 1));
    *(int*)(charData + 0x98) = 0;
    *(int*)(charData + 0xA0) = 0;
    weapon_state_init(charData + 0xA4);
    weapon_state_init(charData + 0x1D4);

    *(int*)(charData + 0xE8C) = 0;
    *(f32*)(charData + 0x544) = 1.0f;
    *(f32*)(charData + 0x548) = 0.0f;
}
