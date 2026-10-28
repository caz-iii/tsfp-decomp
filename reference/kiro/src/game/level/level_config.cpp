// Address: 0x80035410-0x800357c0 | Name: level_config | Date: 2026-08-05
// Level configuration: weapon sets, bot sets, spawn configs, and
// available weapon list construction for multiplayer matches.
//
// Data tables:
//   Weapon set table: base 0x804666C0, stride 0x34 (52 bytes per set)
//   Bot set table: base 0x80467488, stride 0x3C (60 bytes per set)
//   Level spawn config: base 0x8048F97C, stride 0xA4 (164 bytes per level)
//   Level def: stride 0x80, field +0x10 = weapon set index, +0x14 = bot set index

#include "common.h"
#include "level_config.h"

// ============================================================================
// Address: 0x80035410 | level_search_by_current_id
// Searches the level definition table to find the entry matching the
// current level ID. Iterates by string ID validation.
// ============================================================================
void level_search_by_current_id(void) {
    int iOffset = 0;
    int iResult = heap_arena_lookup();

    if (iResult == 0) {
        iResult = heap_arena_lookup();
        if (iResult == 0) {
            while (1) {
                int pStr = get_string_by_id(*(s16*)(g_LevelDefTable + iOffset + 4));
                if (pStr == 0) break;
                if (g_CurrentLevel == *(int*)(g_LevelDefTable + iOffset + 8)) break;
                iOffset += 0x80;
            }
        }
    }
    FUN_80382738(); // epilogue
}

// ============================================================================
// Address: 0x800354b0 | level_get_weapon_set_id
// Returns the weapon set ID for the current level.
// Looks up the level definition, gets weapon set index at +0x10,
// then reads ID from weapon set table at index * 0x34.
// Returns -1 if no level def found.
// ============================================================================
int level_get_weapon_set_id(void) {
    int pLevelDef = (int)level_get_current_def();
    if (pLevelDef == 0) return -1;
    int nWeaponSetIdx = *(int*)(pLevelDef + 0x10);
    return *(int*)(g_WeaponSetTable + nWeaponSetIdx * 0x34);
}

// ============================================================================
// Address: 0x800354f8 | weapon_set_get_name
// Returns the name/ID value from a weapon set table entry by index.
// ============================================================================
int weapon_set_get_name(int nIndex) {
    return *(int*)(g_WeaponSetTable + nIndex * 0x34);
}

// ============================================================================
// Address: 0x80035510 | level_get_weapon_set_ptr
// Returns pointer to the full weapon set data for the current level.
// ============================================================================
void* level_get_weapon_set_ptr(void) {
    int pLevelDef = (int)level_get_current_def();
    if (pLevelDef == 0) return NULL;
    int nIdx = *(int*)(pLevelDef + 0x10);
    return (void*)(g_WeaponSetTable + nIdx * 0x34);
}

// ============================================================================
// Address: 0x80035554 | level_get_bot_set_ptr
// Returns pointer to the bot set data for the current level.
// ============================================================================
void* level_get_bot_set_ptr(void) {
    int pLevelDef = (int)level_get_current_def();
    if (pLevelDef == 0) return NULL;
    int nIdx = *(int*)(pLevelDef + 0x14);
    return (void*)(g_BotSetTable + nIdx * 0x3C);
}

// ============================================================================
// Address: 0x80035598 | level_get_spawn_config
// Returns pointer to spawn configuration data for a given level.
// For arcade hub (level 100): uses dynamic lookup via FUN_803545dc.
// For other levels: indexes into spawn config table at stride 0xA4.
// ============================================================================
void* level_get_spawn_config(int nLevelId) {
    int pLevelInfo = FUN_80035bb4(g_CurrentLevel);
    if (nLevelId == 100) {
        void* pDynamic = FUN_803545dc(pLevelInfo);
        return (void*)((int)pDynamic + 4);
    }
    return (void*)(g_SpawnConfigTable + *(int*)(pLevelInfo + 8) * 0xA4);
}

// ============================================================================
// Address: 0x800355f4 | level_get_char_count
// Returns the number of available characters for the current level.
// For mapmaker: uses custom character list.
// For arcade hub (100): uses dynamic character set.
// For other levels: reads from spawn config table.
// ============================================================================
int level_get_char_count(void) {
    int pLevelInfo = FUN_80035bb4(g_CurrentLevel);
    if (*(int*)(pLevelInfo + 8) == -1) return 0;

    if ((g_GameStateFlags & 0x40) != 0) {
        // Mapmaker: custom character list
        return FUN_8022b53c();
    }

    if (g_CurrentLevel == 100) {
        // Arcade hub: dynamic character set
        int* pDynamic = (int*)FUN_803545dc(pLevelInfo);
        return *pDynamic;
    }

    // Normal level: from spawn config
    return *(int*)(g_SpawnConfigTable + *(int*)(pLevelInfo + 8) * 0xA4);
}

// ============================================================================
// Address: 0x80035670 | level_get_default_weapon
// Gets the default starting weapon for a player index from spawn config.
// For mapmaker: reads from custom weapon assignment.
// For normal levels: reads from spawn config slot array.
// ============================================================================
void level_get_default_weapon(int nPlayerIdx, int* pOutWeaponId) {
    int iResult;

    // Clamp player index
    if (nPlayerIdx > 19) nPlayerIdx = 0;

    iResult = FUN_8022a8a0();
    *pOutWeaponId = -1;

    if ((g_GameStateFlags & 0x40) == 0) {
        // Normal: from spawn config (s16 at stride 8)
        void* pConfig = level_get_spawn_config(g_CurrentLevel);
        *pOutWeaponId = (int)*(s16*)((int)pConfig + nPlayerIdx * 8);
    } else {
        // Mapmaker: from custom assignment
        *pOutWeaponId = *(int*)(*(int*)(iResult + 0x6C) + nPlayerIdx * 0x10);
    }
    FUN_80382738(); // epilogue
}

// ============================================================================
// Address: 0x80035748 | weapon_set_copy_to_match
// Copies a weapon set configuration into the match weapon buffer.
// For param_1 == 0: uses default weapon set (FUN_802c793c).
// Otherwise: uses custom weapon set (FUN_802c7a28), ensures min 6 weapons.
// Copies 0x44 bytes (weapon set header + entries).
// ============================================================================
void weapon_set_copy_to_match(int nSource, u32 pDestBuf) {
    if (nSource == 0) {
        void* pDefault = FUN_802c793c();
        memcpy((void*)pDestBuf, pDefault, 0x44);
    } else {
        void* pCustom = FUN_802c7a28();
        memcpy((void*)pDestBuf, pCustom, 0x44);
        if (*(int*)(pDestBuf + 4) == 0) {
            *(int*)(pDestBuf + 4) = 6;  // Minimum 6 weapon slots
        }
    }
}

// ============================================================================
// Address: 0x800357c0 | match_build_weapon_list
// Builds the available weapon list for the current match.
// Handles: story mode, arcade (from spawn config table), mapmaker (custom),
// and arcade hub (level 100, dynamic weapon set).
// Filters out weapons incompatible with bots.
// Adds special weapons for certain campaign levels (0x1B-0x1D).
// ============================================================================
void match_build_weapon_list(void) {
    if ((g_GameStateFlags & 0x40) != 0 || (g_GameStateFlags & 0x8000000) != 0) {
        // Mapmaker or network: use custom weapon assignment
        FUN_8022a8a0();
        FUN_8038272c();
        return;
    }

    if (g_GameMode == '\n' && g_CurrentLevel != 100) {
        // Story mode (non-hub): use level's weapon config directly
        FUN_80035bb4(g_CurrentLevel);
    } else if ((g_CurrentLevel != 100 || !game_is_in_cutscene_mode()) &&
               (g_CurrentLevel == 100 ||
                (g_ArcadeMapId != 0 && (g_GameStateFlags & 0x800000) == 0) ||
                FUN_80035bb4(g_CurrentLevel) == 0)) {

        // Build weapon set from config
        if (g_CurrentLevel == 100) {
            weapon_set_copy_to_match(g_ArcadeMapId, (u32)&g_MatchWeaponBuf);
        } else {
            // Default: 6 weapons from character lookup table
            g_MatchWeaponBuf.nBaseId = 0x41E;
            g_MatchWeaponBuf.nCount = 6;
            for (int i = 0; i < g_MatchWeaponBuf.nCount; i++) {
                g_MatchWeaponBuf.weapons[i] = FUN_802c7b3c(i);
            }
        }

        // Filter out bot-incompatible weapons
        for (int i = 0; i < g_MatchWeaponBuf.nCount; i++) {
            if (gamemode_supports_bots(g_GameSubMode, g_MatchWeaponBuf.weapons[i])) {
                g_MatchWeaponBuf.weapons[i] = 0;  // Remove
            }
        }

        // Add special weapons for specific campaign levels
        if (g_GameMode == '\x03' && g_CurrentLevel != 0x1C) {
            if (g_CurrentLevel == 0x1B || g_CurrentLevel == 0x1A + 1) {
                g_MatchWeaponBuf.nCount++;
                g_MatchWeaponBuf.weapons[6] = 0x2A;
            } else if (g_CurrentLevel == 0x1D || g_CurrentLevel == 0x1C + 1) {
                g_MatchWeaponBuf.nCount++;
                g_MatchWeaponBuf.weapons[6] = 0x2E;
            }
        }
    }
    FUN_8038272c(); // epilogue
}

// ============================================================================
// Address: 0x80035a28 | weapon_is_in_match_list
// Checks if a weapon ID exists in the current match's weapon list.
// Also checks mapmaker custom weapon if applicable.
// ============================================================================
BOOL weapon_is_in_match_list(int nWeaponId) {
    int pList = (int)match_build_weapon_list();
    int nCount = *(int*)(pList + 4);
    for (int i = 0; i < nCount; i++) {
        if (*(int*)(pList + i * 4 + 8) == nWeaponId) return TRUE;
    }
    // Mapmaker fallback check
    if ((g_GameStateFlags & 0x40) != 0 || (g_GameStateFlags & 0x8000000) != 0) {
        int pCustom = FUN_8022a8a0();
        return (*(int*)(pCustom + 0x74) == nWeaponId);
    }
    return FALSE;
}

// ============================================================================
// Address: 0x80035ac0 | level_get_info_by_id
// Gets level info pointer by level ID. Uses story or arcade variant
// depending on current game state (level < 0x65 + story flag).
// ============================================================================
void* level_get_info_by_id(int nLevelId) {
    void* pResult = NULL;
    int iOffset = 0;
    while (*(s16*)(g_LevelDefTable + iOffset + 4) != 0) {
        if (nLevelId == *(int*)(g_LevelDefTable + iOffset + 8)) {
            if (g_CurrentLevel < 0x65 && g_StoryProgressFlag != 0) {
                pResult = *(void**)(g_LevelDefTable + iOffset + 0x24);
            } else {
                pResult = *(void**)(g_LevelDefTable + iOffset + 0x28);
            }
            break;
        }
        iOffset += 0x80;
    }
    return pResult;
}

// ============================================================================
// Address: 0x80035b54 | level_get_info_story
// Gets level info pointer (story-only variant, always uses +0x24 field).
// ============================================================================
void* level_get_info_story(int nLevelId) {
    void* pResult = NULL;
    int iOffset = 0;
    while (*(s16*)(g_LevelDefTable + iOffset + 4) != 0) {
        if (nLevelId == *(int*)(g_LevelDefTable + iOffset + 8)) {
            pResult = *(void**)(g_LevelDefTable + iOffset + 0x24);
            break;
        }
        iOffset += 0x80;
    }
    return pResult;
}

// ============================================================================
// Address: 0x80035bb4 | level_get_info_cached
// Cached level info lookup. Memoizes result by (levelId, gameMode) pair.
// Returns cached pointer if same level+mode requested again.
// ============================================================================
void* level_get_info_cached(int nLevelId) {
    if (g_CachedLevelId != nLevelId || g_GameMode != g_CachedGameMode) {
        g_CachedGameMode = (u32)g_GameMode;
        g_CachedLevelId = nLevelId;
        if (g_CachedGameMode == 10) {
            g_CachedLevelInfo = level_get_info_story(nLevelId);
        } else {
            g_CachedLevelInfo = level_get_info_by_id(nLevelId);
        }
    }
    return g_CachedLevelInfo;
}

// ============================================================================
// Address: 0x80035c28 | weapon_resolve_index_to_id
// Resolves a weapon index to actual weapon ID via the match weapon list.
// Handles special cases for weapon types 2 (guns), 0x40, and 0x800.
// Returns -1 if index out of bounds or weapon slot is empty.
// ============================================================================
int weapon_resolve_index_to_id(int nType, int nIndex) {
    if (nType != 0x40 && nType != 2 && nType != 0x800) {
        return nIndex;  // Direct ID for most types
    }
    int pList = (int)match_build_weapon_list();
    if (nIndex >= *(int*)(pList + 4)) return -1;
    if (nIndex == 0 && (g_GameStateFlags & 0x400) != 0 &&
        nType == 2 && g_CurrentLevel != 100) {
        return -1;  // Fists-only mode: no gun at slot 0
    }
    int nWeaponId = *(int*)(pList + nIndex * 4 + 8);
    return (nWeaponId != 0) ? nWeaponId : -1;
}

// ============================================================================
// Address: 0x80035cfc | spawn_resolve_weapon
// Resolves a spawn point's weapon assignment to an entity handle.
// Uses the spawn point's type (+4), index (+0x10), and variant (+0x12).
// ============================================================================
int spawn_resolve_weapon(int pSpawnPoint) {
    int nType = *(int*)(pSpawnPoint + 4);
    int nIndex = (int)*(s16*)(pSpawnPoint + 0x10);
    int nResolved = weapon_resolve_index_to_id(nType, nIndex);
    int nVariant = (int)*(s16*)(pSpawnPoint + 0x12);
    int pEntity = FUN_80097eec(nType, nResolved, nVariant);
    if (pEntity == 0) return -1;
    return FUN_800b0704(pEntity);
}

// ============================================================================
// Address: 0x80035d54 | spawn_resolve_entity_ptr
// Resolves a spawn point to its target entity pointer based on type.
// Type determines entity category (weapon, character, vehicle, etc.)
// and how the index field is interpreted.
// ============================================================================
// [Large switch on spawn type field at +2, maps to entity category flags]

// ============================================================================
// Address: 0x80035ea4 | spawn_resolve_entity_id
// Similar to spawn_resolve_entity_ptr but returns the entity's game ID
// rather than a pointer. Used for UI/HUD display of pickup items.
// ============================================================================
// [Large switch, same pattern as 0x80035d54]

// ============================================================================
// Address: 0x80036060 | spawn_select_character
// Selects a random character for a spawning player from the available pool.
// Filters by: team restrictions, already-spawned characters, availability
// flags, and entity validity. Falls back to forced selection if no valid
// random choice found within 1000 attempts (arcade hub) or single pass.
// For story mode (mode 3): uses campaign character lists per team.
// ============================================================================
// [Very large function - random selection with filtering]

// ============================================================================
// Address: 0x800364f8 | spawn_select_character_wrapper
// Wrapper for spawn_select_character.
// ============================================================================
void spawn_select_character_wrapper(void) {
    spawn_select_character();
}
