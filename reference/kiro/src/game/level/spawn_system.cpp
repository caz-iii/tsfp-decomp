// Address: 0x8003651c-0x8003710c | Name: spawn_system | Date: 2026-08-05
// Spawn point selection, entity creation, and respawn logic.
// This is the runtime spawn system that uses the level config data
// to place weapons, characters, and pickups in the world.

#include "common.h"
#include "spawn_system.h"
#include "level_config.h"

// Spawn point table: array of 0x20-byte entries at DAT_80611b8c
// Entry layout:
//   +0x00: int flags
//   +0x04: u32 modeFlags (which game modes this point is valid for)
//   +0x08: int groupId
//   +0x0A: s16 spawnGroupId
//   +0x0C: float posX
//   +0x10: float posY
//   +0x14: float posZ
// Total count: DAT_80611c14

// ============================================================================
// Address: 0x8003651c | spawn_select_character_wrapper2
void spawn_select_character_wrapper2(void) { spawn_select_character(); }

// ============================================================================
// Address: 0x80036540 | spawn_find_random_in_range
// Finds a random spawn point within a min/max distance range from a position.
// Uses rejection sampling: picks random points until one satisfies distance
// constraints and validity flags. Max attempts = table_size * constant.
// ============================================================================
void spawn_find_random_in_range(float* pPos, u32 uFlags) {
    float fMaxDistSq = SPAWN_MAX_DIST_SQ;
    float fMinDistSq = fMaxDistSq;

    // Decode min/max distance from packed flags
    if (uFlags & 0x40000000) {
        float fMin = (float)(uFlags & 0xFF);
        fMinDistSq = fMin * fMin;
    }
    if (uFlags & 0x20000000) {
        float fMax = (float)((uFlags >> 8) & 0xFF);
        fMaxDistSq = fMax * fMax;
    }

    int nMaxAttempts = (int)((float)g_SpawnPointCount * SPAWN_SAMPLE_RATE);
    for (int i = 0; i < nMaxAttempts; i++) {
        u32 nRandIdx = FUN_8013104c();
        int pPoint = g_SpawnPointTable +
            (int)((float)g_SpawnPointCount * (float)nRandIdx / SPAWN_RAND_SCALE) * 0x20;

        // Distance check
        float dx = *(float*)(pPoint + 0x0C) - pPos[0];
        float dy = *(float*)(pPoint + 0x10) - pPos[1];
        float dz = *(float*)(pPoint + 0x14) - pPos[2];
        float fDistSq = dx*dx + dy*dy + dz*dz;

        BOOL bValid = (fDistSq <= fMaxDistSq);
        if (bValid && fDistSq < fMinDistSq) bValid = FALSE;
        if (pPoint == 0) bValid = FALSE;
        if (g_CurrentLevel == 100 && (*(u32*)(pPoint + 4) & 0x10) == 0) bValid = FALSE;
        if (bValid && (*(u32*)(pPoint + 4) & 1) == 0) bValid = FALSE;

        if (bValid) break;  // Found valid point
    }
}

// ============================================================================
// Address: 0x80036778 | spawn_find_random_by_group
// Finds a random spawn point matching a specific group ID.
// Collects all matching points (max 20), then picks randomly.
// Returns spawn point index, or -1 if none found.
// ============================================================================
int spawn_find_random_by_group(int nGroupId) {
    int aMatches[20];
    u32 nCount = 0;

    for (int i = 0; i < g_SpawnPointCount && nCount < 20; i++) {
        if (nGroupId == *(s16*)(g_SpawnPointTable + i * 0x20 + 0x0A)) {
            aMatches[nCount++] = i;
        }
    }

    if (nCount == 0) return -1;
    u32 nRand = FUN_8013104c();
    return aMatches[nRand % nCount];
}

// ============================================================================
// Address: 0x80036818 | spawn_find_respawn_point
// Finds a valid respawn point for a player. Searches outward from the
// player's last known spawn index, alternating +/- direction.
// Validates: distance (min/max), visibility (dot product vs view direction),
// and flag checks. Uses fast inverse sqrt for normalization.
// Handles both single-player (uses player 1 position) and split-screen
// (uses player 2 position for second player's search).
// ============================================================================
void spawn_find_respawn_point(u32 uFlags) {
    // [Very large function - alternating bidirectional search with:
    //  - Configurable distance thresholds from packed flags
    //  - View direction dot product test (front/behind checks)
    //  - Fast inverse sqrt normalization for direction vectors
    //  - Per-player search (player 1 at offset 0x678, player 2 at 0x1518)
    //  - Random fallback for single-player when > 50% random threshold]
}

// ============================================================================
// Address: 0x80036d1c | spawn_try_find_respawn
// Conditionally calls spawn_find_respawn_point if high bit is set.
// ============================================================================
void spawn_try_find_respawn(u32 uFlags) {
    if (uFlags & 0x80000000) {
        spawn_find_respawn_point(uFlags);
    }
}

// ============================================================================
// Address: 0x80036d44 | spawn_resolve_all_references
// Resolves all weapon and character references for the level's spawn points.
// Iterates spawn data (+0x14 array, stride 0x58) and character data
// (+0x24 array, stride 0x20), resolving IDs to entity handles.
// ============================================================================
void spawn_resolve_all_references(void) {
    int pLevelInfo = level_get_info_cached(g_CurrentLevel);
    if (pLevelInfo == 0) return;

    // Resolve weapon spawn references
    int nWeapons = *(int*)(pLevelInfo + 0x18);
    int pWeaponArray = *(int*)(pLevelInfo + 0x14);
    for (int i = 0; i < nWeapons; i++) {
        int pEntry = pWeaponArray + i * 0x58;
        int hWeapon = spawn_resolve_weapon(pEntry);
        if (hWeapon >= 0) {
            int hEntity = FUN_8006cefc(*(int*)(pEntry + 0x14));
            *(int*)(pEntry + 0x18) = hEntity;
        }
    }

    // Resolve character spawn references
    int nChars = *(int*)(pLevelInfo + 0x28);
    int pCharArray = *(int*)(pLevelInfo + 0x24);
    for (int i = 0; i < nChars; i++) {
        int pEntry = pCharArray + i * 0x20;
        int hEntity = FUN_8006cefc(*(int*)(pEntry + 4));
        *(int*)(pEntry + 0x0C) = hEntity;
    }
}

// ============================================================================
// Address: 0x80036df8 | spawn_apply_powerup
// Applies a powerup/buff effect to a spawned entity.
// Sets invincibility if applicable, creates visual effect timer.
// ============================================================================
void spawn_apply_powerup(int pEntity, int pSpawnData) {
    if (*(int*)(pSpawnData + 0x50) < 0) return;
    if (*(int*)(pEntity + 0x80) != 0) return;

    // Ensure entity has health
    if (*(float*)(pEntity + 0x1CC) < 0.0f || (*(u32*)(pEntity + 0x28) & 0x10) == 0) {
        *(u32*)(pEntity + 0x28) |= 0x10;
        *(float*)(pEntity + 0x1D8) = 0.0f;
        *(float*)(pEntity + 0x1CC) = 0.0f;
    }

    // Create powerup effect
    int hEffect = FUN_80144864();
    *(int*)(pEntity + 0x80) = hEffect;
    FUN_80145090(hEffect, pEntity,
                 *(u32*)(pSpawnData + 0x50) & 0xFF,
                 *(int*)(pSpawnData + 0x54),
                 *(float*)(pSpawnData + 0x58));
}

// ============================================================================
// Address: 0x80036ea4 | spawn_activate_point
// Activates a spawn point: applies health, flags, and speed to the entity.
// Only processes if the spawn data matches the pending activation.
// ============================================================================
void spawn_activate_point(int pEntity, int pSpawnData) {
    if (pSpawnData == 0 || pSpawnData != g_PendingSpawnActivation) return;
    g_PendingSpawnActivation = 0;

    // Apply health from spawn data
    float fHealth = *(float*)(pSpawnData + 0x44);
    if (fHealth >= 0.0f && (*(u32*)(pEntity + 0x28) & 0x10) == 0) {
        *(u32*)(pEntity + 0x28) |= 0x10;  // Enable health
        *(float*)(pEntity + 0x1D8) = fHealth;
        *(float*)(pEntity + 0x1CC) = fHealth;
        *(float*)(pEntity + 0x1DC) = fHealth * 0.5f;  // Half-health threshold
    }

    // Apply entity flags
    *(u32*)(pEntity + 0x9C) |= *(u32*)(pSpawnData + 0x48);

    // Apply speed override
    if (*(int*)(pSpawnData + 0x4C) != 0) {
        *(int*)(*(int*)(pEntity + 4) + 0x30) = *(int*)(pSpawnData + 0x4C);
    }

    // Apply powerup (not for arcade hub)
    if (g_CurrentLevel != 100) {
        spawn_apply_powerup(pEntity, pSpawnData);
    }
}

// ============================================================================
// Address: 0x80036f54 | spawn_create_entity
// Creates a game entity from spawn point data.
// Dispatches to weapon or pickup creator based on entity type.
// Initializes position, rotation, flags, animation, and AI state.
// ============================================================================
void spawn_create_entity(int* pSpawnPoint) {
    int pEntity = 0;
    g_PendingSpawnActivation = pSpawnPoint;

    int nEntityType = FUN_80070b88(pSpawnPoint[3]);
    if (nEntityType == 0x80) {
        // Pickup/item type
        pEntity = FUN_8007ec0c(pSpawnPoint[3], &pSpawnPoint[4],
                               (float*)&pSpawnPoint[7], pSpawnPoint[10], 0, 0, (int)pSpawnPoint);
    } else if (nEntityType != 0x400) {
        // Weapon/standard type
        pEntity = FUN_80072d70(pSpawnPoint[3], &pSpawnPoint[4],
                               (float*)&pSpawnPoint[7], 0, (int)pSpawnPoint);
    }

    if (pEntity != 0) {
        *(int**)(pEntity + 0x2D8) = pSpawnPoint;
        if (*(int*)(pEntity + 0x80) != 0) {
            pSpawnPoint[2] |= 0x10000;
        }
        // Set entity flags and AI seed
        OSCreateThread(pEntity, pSpawnPoint[2], pSpawnPoint[0xB]);
        FUN_8013f8a8(pSpawnPoint, pEntity);
        // Additional initialization...
    }
}

// ============================================================================
// Address: 0x8003710c | spawn_flags_match_mode
// Checks if a spawn point's flag bitfield is valid for the current game
// mode, difficulty, and player count.
// Flag bits: mode 1=0x02, mode 2=0x04, mode 3=0x01, mode 4/9=0x08,
//            mode 5=0x40, mode 6=0x20, mode 7=0x10, mode 10=difficulty-based.
// Player count flags: 0x200000=1P only, 0x400000=2P only.
// ============================================================================
BOOL spawn_flags_match_mode(u32 uFlags) {
    // Mapmaker: uses custom flag check
    if ((g_GameStateFlags & 0x40) != 0 || (g_GameStateFlags & 0x8000000) != 0) {
        int pCustom = FUN_8022cf74();
        u32 uCustomFlags = FUN_8022cf84(pCustom);
        return (uFlags & uCustomFlags) != 0;
    }

    // Player count filter
    if ((uFlags & 0x600000) != 0) {
        if ((uFlags & 0x200000) != 0 && g_NumPlayers != 1) return FALSE;
        if ((uFlags & 0x400000) != 0 && g_NumPlayers != 2) return FALSE;
    }

    // Mode-specific bit check
    switch (g_GameMode) {
    case 1:  return (uFlags & 0x02) != 0;
    case 2:  return (uFlags & 0x04) != 0;
    case 3:  return (uFlags & 0x01) != 0;
    case 4:  return (uFlags & 0x08) != 0;
    case 5:  return (uFlags & 0x40) != 0;
    case 6:  return (uFlags & 0x20) != 0;
    case 7:  return (uFlags & 0x10) != 0;
    case 9:  return (uFlags & 0x08) != 0;
    case 10: // Difficulty-based: check difficulty flags (0x100/0x200/0x400)
        if ((uFlags & 0x3800) != 0) {
            u32 diffMask[3] = { g_DiffMaskEasy, g_DiffMaskMedium, g_DiffMaskHard };
            if ((uFlags & 0x3800 & diffMask[g_DifficultyLevel]) == 0) return FALSE;
        }
        if (g_DifficultyTier == 0 && (uFlags & 0x100)) return TRUE;
        if (g_DifficultyTier == 1 && (uFlags & 0x200)) return TRUE;
        if (g_DifficultyTier == 2 && (uFlags & 0x400)) return TRUE;
        break;
    }
    return FALSE;
}

// ============================================================================
// Batch 055: Entity spawn/despawn management
// ============================================================================

// Address: 0x80037974 | entity_tracking_alloc
// Allocates 128-byte (32-entry) despawn tracking list.
void entity_tracking_alloc(void) {
    g_DespawnCount = 0;
    g_DespawnList = (int)mem_alloc(0x80, 1);
}

// Address: 0x800379a8 | level_entity_spawn_init
// Initializes per-type entity tracking buffers for the level.
// Allocates separate arrays for entity instances, particles, and weapons.
void level_entity_spawn_init(void) {
    int pLevelInfo = level_get_info_cached(g_CurrentLevel);
    g_EntityInstanceTrack = 0;
    g_ParticleTrack = 0;
    g_WeaponSpawnTrack = 0;
    g_WeaponTimerTrack = 0;

    if (g_CurrentLevel != 0x66) {
        FUN_80037974(pLevelInfo, 0);
    }

    if (pLevelInfo != 0 && g_CurrentLevel != 100) {
        // Entity instances: N * 0x14 bytes, 16-aligned
        int nEntities = *(int*)(pLevelInfo + 0x10);
        if (nEntities > 0) {
            int nSize = (nEntities * 0x14 + 0xF) & ~0xF;
            g_EntityInstanceTrack = (int)mem_alloc(nSize, 1);
            memset((void*)g_EntityInstanceTrack, 0, nSize);
        }
        // Particle instances: N * 0x14 bytes
        int nParticles = *(int*)(pLevelInfo + 0x88);
        if (nParticles > 0) {
            int nSize = (nParticles * 0x14 + 0xF) & ~0xF;
            g_ParticleTrack = (int)mem_alloc(nSize, 1);
            memset((void*)g_ParticleTrack, 0, nSize);
        }
        // Weapons: N * 4 bytes (handle + timer)
        int nWeapons = *(int*)(pLevelInfo + 0x18);
        if (nWeapons > 0) {
            int nSize = (nWeapons * 4 + 0xF) & ~0xF;
            g_WeaponSpawnTrack = (int)mem_alloc(nSize, 1);
            g_WeaponTimerTrack = (int)mem_alloc(nSize, 1);
            memset((void*)g_WeaponTimerTrack, 0, nSize);
        }
    }
    FUN_80038544(1);  // Run initial spawn pass
}

// Address: 0x80037ae8 | level_entity_spawn_reset
// Resets entity spawn state and re-runs spawn logic.
// Clears tracking buffers unless save data indicates persistence.
void level_entity_spawn_reset(void) {
    int bPersist = FUN_8025b8c0(0);
    if (bPersist == 0) {
        if (g_EntityInstanceTrack != 0) {
            int nSize = (*(int*)(g_LevelContext + 0x10) * 0x14 + 0xF) & ~0xF;
            memset((void*)g_EntityInstanceTrack, 0, nSize);
        }
        if (g_ParticleTrack != 0) {
            int nSize = (*(int*)(g_LevelContext + 0x88) * 0x14 + 0xF) & ~0xF;
            memset((void*)g_ParticleTrack, 0, nSize);
        }
    }
    FUN_80038544(0);  // Re-run spawn logic (not first time)
    g_DespawnCount = 0;
}

// Address: 0x80037b74 | entity_process_despawns
// Destroys entities whose IDs are in the despawn tracking list.
// Collects matching active entities, clears their resource refs,
// then dispatches appropriate destroy function by type.
void entity_process_despawns(void) {
    int aMatches[80];  // Max 80 entities to despawn
    int nFound = 0;

    if (g_CurrentLevel == 0x0B) FUN_801eb37c();

    // Collect entities matching despawn IDs
    for (int i = 0; i < g_ActiveEntityCount; i++) {
        int pEntity = g_ActiveEntityBase + i * 0x2E8;
        if ((*(u32*)(pEntity + 0x28) & 0x80000000) != 0) continue;
        for (int j = 0; j < g_DespawnCount; j++) {
            if ((u16)*(u16*)(pEntity + 0x96) == *(u32*)(g_DespawnList + j * 4)) {
                if (nFound < 80) aMatches[nFound++] = pEntity;
            }
        }
    }

    // Clear resource refs for despawn IDs
    for (int i = 0; i < g_DespawnCount; i++) {
        FUN_80060994();
        *(int*)(g_ResourceTable + *(int*)(g_DespawnList + i * 4) * 0x30 + 4) = 0;
    }

    // Destroy collected entities
    for (int i = 0; i < nFound; i++) {
        int* pEntity = (int*)aMatches[i];
        if (g_GamePhase == 7) FUN_80039f34((int)pEntity);
        if ((pEntity[10] & 0x80000000) == 0) {
            if (pEntity[8] == 0x400000) FUN_8015d5b8((int)pEntity);
            if (pEntity[8] == 8) FUN_8013d170((int)pEntity);
            else FUN_800735e4(pEntity);
        }
    }
    g_DespawnCount = 0;
}

// Address: 0x80037d54 | level_spawn_actor_entities
// Spawns all entities that are bound to the current actor/character.
// Checks actor flag mask match and mode flags before spawning.
// Handles entity instances (stride 0x60), particles (0x8C), and decals (0x40).
void level_spawn_actor_entities(void) {
    int pLevelInfo = level_get_info_cached(g_CurrentLevel);
    u32 uActorFlags = level_get_current_actor_flags();

    // Entity instances
    for (int i = 0; i < *(int*)(pLevelInfo + 0x10); i++) {
        int pEntry = *(int*)(pLevelInfo + 0x0C) + i * 0x60;
        BOOL bIsActorBound = /* check if entity references actor model */;
        if (bIsActorBound && (uActorFlags & *(u32*)(pEntry + 0x40)) != 0 &&
            spawn_check_flags_or_cutscene(*(u32*)(pEntry + 8))) {
            int pEntity = spawn_create_entity(pEntry, i, 1);
            FUN_8003a028(pEntity, i, 1);
            entity_track_spawned(pEntity);
        }
    }

    // Particle instances (stride 0x8C)
    for (int i = 0; i < *(int*)(pLevelInfo + 0x88); i++) {
        int pEntry = *(int*)(pLevelInfo + 0x84) + i * 0x8C;
        /* Similar actor-bound check and spawn */
    }

    // Decals (stride 0x40)
    for (int i = 0; i < *(int*)(pLevelInfo + 0xE0); i++) {
        int* pEntry = (int*)(*(int*)(pLevelInfo + 0xDC) + i * 0x40);
        /* Similar check and spawn via FUN_8003a580 */
    }

    if (g_CurrentLevel == 0x0B) FUN_801eb2cc();
}

// Address: 0x800380e8 | entity_track_spawned
// Registers a spawned entity's ID into the despawn tracking list.
void entity_track_spawned(int pEntity) {
    entity_add_to_despawn_list((u32)*(u16*)(pEntity + 0x96));
}

// Address: 0x8003810c | entity_add_to_despawn_list
// Adds an entity ID to the despawn tracking list.
// Max 30 entries (55 for level 0x66). Deduplicates.
void entity_add_to_despawn_list(int nEntityId) {
    // Check for duplicate
    for (int i = 0; i < g_DespawnCount; i++) {
        if (nEntityId == *(int*)(g_DespawnList + i * 4)) return;
    }
    // Add
    int nMax = (g_CurrentLevel == 0x66) ? 55 : 30;
    *(int*)(g_DespawnList + g_DespawnCount * 4) = nEntityId;
    if (g_DespawnCount < nMax) g_DespawnCount++;
}

// Address: 0x80038170 | level_spawn_entity_by_id
// Finds and spawns a specific entity by its ID from the level entity table.
int level_spawn_entity_by_id(int nEntityId) {
    int nCount = *(int*)(g_LevelContext + 0x10);
    for (int i = 0; i < nCount; i++) {
        int* pEntry = (int*)(*(int*)(g_LevelContext + 0x0C) + i * 0x60);
        if (*pEntry == nEntityId) {
            return spawn_create_entity(pEntry, i, 1);
        }
    }
    return 0;
}

// Address: 0x800381dc | entity_is_actor_bound
// Checks if an entity ID references an actor-bound character model.
// Searches entities (stride 0x60), particles (0x8C), and decals (0x40).
BOOL entity_is_actor_bound(int nEntityId) {
    // Search entity instances
    int nCount = *(int*)(g_LevelContext + 0x10);
    for (int i = 0; i < nCount; i++) {
        int* pEntry = (int*)(*(int*)(g_LevelContext + 0x0C) + i * 0x60);
        if (*pEntry == nEntityId) {
            return (*(u32*)(g_ResourceTable + *(s16*)(g_CharTable + pEntry[3] * 2) * 0x30 + 0x28)
                    >> 25) & 1;
        }
    }
    // Search particles and decals similarly...
    return FALSE;
}

// Address: 0x800382fc | level_spawn_decal_by_id
// Finds and spawns a decal/overlay entity by ID from the decal table.
int level_spawn_decal_by_id(int nDecalId) {
    int nCount = *(int*)(g_LevelContext + 0xE0);
    for (int i = 0; i < nCount; i++) {
        int* pEntry = (int*)(*(int*)(g_LevelContext + 0xDC) + i * 0x40);
        if (*pEntry == nDecalId) {
            return FUN_8003a580(pEntry);
        }
    }
    return 0;
}

// ============================================================================
// Batch 056: Entity visibility, level spawning master, respawn tracking
// ============================================================================

// Address: 0x8003835c | entity_visibility_list_clear
// Clears the 350-entry entity visibility control list (0x1068 bytes).
void entity_visibility_list_clear(void) {
    memset(&g_VisibilityList, 0, 0x1068);
}

// Address: 0x8003838c | entity_visibility_register
// Registers an entity in the visibility control list.
// Marks it with a visibility group flag (0x20000000→1, 0x40000000→2).
// Used for actor-bound entities that should show/hide with character.
void entity_visibility_register(int nEntityHandle, int nGroupFlag, int nActorMask) {
    for (int i = 0; i < 350; i++) {
        if ((g_VisibilityList[i].flags & 4) == 0) {
            g_VisibilityList[i].handle = nEntityHandle;
            g_VisibilityList[i].actorMask = nActorMask;
            if (nGroupFlag == 0x20000000) g_VisibilityList[i].flags = 1;
            else if (nGroupFlag == 0x40000000) g_VisibilityList[i].flags = 2;
            g_VisibilityList[i].flags |= 4;
            return;
        }
    }
}

// Address: 0x80038400 | entity_visibility_update
// Per-frame: updates entity enable/disable state based on current actor
// mask. Entities are enabled if their actor mask includes the current actor.
void entity_visibility_update(u32 uCurrentActorMask) {
    for (int i = 0; i < 350; i++) {
        if ((g_VisibilityList[i].flags & 4) == 0) continue;
        int pEntity = /* resolve handle to entity */;
        if (pEntity == 0) { g_VisibilityList[i].handle = 0; continue; }
        if ((g_VisibilityList[i].actorMask & uCurrentActorMask) == 0) {
            // Hide: clear enable bit
            if ((g_VisibilityList[i].flags & 1) != 0 && pEntity != 0)
                *(u32*)(pEntity + 0x28) &= ~1;
        } else {
            // Show: set enable bit (unless grenade type disabled)
            if (pEntity != 0)
                *(u32*)(pEntity + 0x28) |= 1;
        }
    }
}

// Address: 0x80038544 | level_spawn_all_entities
// MASSIVE: Master entity spawning function. Spawns all level content:
// entities, weapons, scripts/doors, particles, decals, sound zones,
// navigation, and mapmaker tiles.
// [Very large - documented by type in source comments above]

// Address: 0x80039aec | spawn_create_particle_entity
// Creates a particle/effect entity from spawn data. Resolves position
// from either spawn point reference or inline coordinates.
void spawn_create_particle_entity(int* pSpawnData) {
    // Resolve position from spawn point or inline
    // Create particle via FUN_80088170
    // Set flags, threading, AI state
}

// Address: 0x80039c40 | spawn_weapon_should_create
// Checks if a weapon spawn point should actually create its entity.
// Filters out: flag entities (type 0x400), items in shrink modes.
BOOL spawn_weapon_should_create(u32* pSpawnPoint) {
    if (*pSpawnPoint & 0x10000000) return FALSE;  // Disabled
    u32 nType = pSpawnPoint[1];
    if (nType == 0x400) return FALSE;  // Flag: handled separately
    if ((nType == 8 || nType == 0x10) &&
        (g_GameSubMode == '\r' || g_GameSubMode == '\x0e')) return FALSE;
    return TRUE;
}

// Address: 0x80039ca4 | spawn_stub_noop
void spawn_stub_noop(void) { return; }

// Address: 0x80039de8 | level_validate_waypoint_flags
// Validates all AI waypoint flags against current mode + unlock level.
void level_validate_waypoint_flags(void) {
    int pLevelInfo = level_get_info_cached(g_CurrentLevel);
    for (int i = 0; i < *(int*)(pLevelInfo + 0x78); i++) {
        int pWaypoint = *(int*)(pLevelInfo + 0x74) + i * 0x98;
        spawn_check_flags_with_unlock(*(u32*)(pWaypoint + 0x0C), *(u32*)(pWaypoint + 0x2C));
    }
}

// Address: 0x80039e7c | weapon_get_ammo_data_ptr
// Returns pointer to weapon ammo data for a given weapon+variant.
// Uses weapon table at DAT_8043e23c (stride 0x3C, 70 weapons max).
int weapon_get_ammo_data_ptr(int nWeaponId, int nVariant) {
    if (nVariant == -1 && nWeaponId >= 0 && nWeaponId < 70) {
        int nAmmoIdx = g_WeaponAmmoTable[nWeaponId];
        if (nAmmoIdx >= 0 && nAmmoIdx < 66) {
            if (g_GameMode == '\n')
                return g_AmmoDataBase + nAmmoIdx * 0x29C + 0x24;
            return g_AmmoDataBase + nAmmoIdx * 0x29C + 0x28;
        }
    }
    if (nVariant >= 0 && nVariant < 8) {
        return 0x803F3E20 + nVariant * 0xC + g_DifficultyTier * 4;
    }
    return 0;
}

// Address: 0x80039f34 | entity_mark_for_respawn
// Marks a destroyed entity for respawn tracking.
// Finds its spawn data in the tracking buffer and sets respawn flag (0x40).
void entity_mark_for_respawn(int pEntity) {
    if (*(int*)(pEntity + 0x2C) == 0) return;
    u32* pTrackEntry = NULL;
    if (*(int*)(pEntity + 0x20) == 0x200) {
        // Particle type: search particle tracking
        for (int i = 0; i < *(int*)(g_LevelContext + 0x88); i++) {
            if (*(int*)(pEntity + 0x2C) == *(int*)(g_ParticleTrack + i * 0x14)) {
                pTrackEntry = (u32*)(g_ParticleTrack + i * 0x14);
                break;
            }
        }
        if (pTrackEntry) FUN_8003a0b0(pTrackEntry, 0);
    } else if (*(int*)(pEntity + 0x2D8) != 0) {
        // Standard entity: compute index from spawn data pointer
        int nIdx = (*(int*)(pEntity + 0x2D8) - *(int*)(g_LevelContext + 0x0C)) / 0x60;
        if (nIdx < 0 || nIdx >= *(int*)(g_LevelContext + 0x10)) return;
        pTrackEntry = (u32*)(g_EntityInstanceTrack + nIdx * 0x14);
        FUN_8003a0b0(pTrackEntry, 1);
    }
    if (pTrackEntry) *(u8*)(pTrackEntry + 4) |= 0x40;  // Respawn flag
}

// ============================================================================
// Batch 057: Entity state save/restore for respawning
// ============================================================================

// Tracking entry layout (0x14 bytes per entity):
//   +0x00: u32 entityHandle (runtime handle, upper 10 bits = pool index)
//   +0x04: u32 modelVariant (model/skin index at entity+0x30)
//   +0x08: float healthOrTime (entity+0x1CC for health)
//   +0x0C: u32 spawnPointRef (for position restoration)
//   +0x10: u8 flags (bit 0 = active, bit 6 = needs_respawn, bit 7 = permanent)
//   +0x11: u8 bDestroyOnRestore (1 = destroy instead of restore)

// Address: 0x8003a028 | entity_clear_respawn_flag
// Clears the respawn flag on a tracking entry after entity has been restored.
void entity_clear_respawn_flag(int pEntity, int nIndex, int nIsEntityType) {
    u32* pTrack;
    if (nIsEntityType == 0) {
        pTrack = (u32*)(g_ParticleTrack + nIndex * 0x14);
    } else {
        pTrack = (u32*)(g_EntityInstanceTrack + nIndex * 0x14);
    }
    if (pTrack != NULL && pEntity != 0 && *pTrack == *(u32*)(pEntity + 0x2C)) {
        entity_restore_state_from_track(pTrack, nIsEntityType);
        *(u8*)(pTrack + 4) &= ~0x40;  // Clear respawn flag
    }
}

// Address: 0x8003a0b0 | entity_save_state_to_track
// Saves an entity's current state into its tracking entry.
// Records: health, model variant, and spawn point reference.
// If entity is dead/destroyed, marks for respawn.
void entity_save_state_to_track(u32* pTrack, int nIsEntityType) {
    u32 uHandle = *pTrack;
    if (uHandle == 0) return;
    if (pTrack[4] & 0x40000000) return;  // Permanent, don't save

    // Resolve entity from handle
    int pEntity = g_EntityPool + (uHandle >> 22) * 0x2E8;
    if (*(u32*)(pEntity + 0x2C) != uHandle) {
        pEntity = 0;
        *pTrack = 0;
    }

    if (pEntity == 0 || *(int*)(pEntity + 4) == 0) {
        // Entity is gone: mark for respawn
        *(u8*)(pTrack + 4) = 1;
        pTrack[2] = 0;  // No health
        *(u8*)((int)pTrack + 0x11) = 1;  // Destroy on restore
        pTrack[3] = 0;
    } else {
        // Entity alive: save state
        *(u8*)(pTrack + 4) = 1;
        *(u8*)((int)pTrack + 0x11) = 0;
        pTrack[2] = *(u32*)(pEntity + 0x1CC);  // Health
        pTrack[1] = *(u32*)(*(int*)(pEntity + 4) + 0x30);  // Model variant
        if (nIsEntityType != 0) {
            pTrack[3] = *(u32*)(pEntity + 0x19C);  // Entity-specific data
        } else {
            if (*(int*)(pEntity + 0x7C) != 0) {
                pTrack[3] = *(u32*)(*(int*)(pEntity + 0x7C) + 0x10);
            }
        }
    }
}

// Address: 0x8003a19c | entity_restore_state_from_track
// Restores an entity from its tracking entry.
// If bDestroyOnRestore: destroys the entity.
// Otherwise: restores health, model variant, and spawn data.
void entity_restore_state_from_track(u32* pTrack, int nIsEntityType) {
    if (*(char*)(pTrack + 4) == '\0') return;

    // Resolve entity
    u32 uHandle = *pTrack;
    int* pEntity = (uHandle == 0) ? NULL :
        (int*)(g_EntityPool + (uHandle >> 22) * 0x2E8);
    if (pEntity != NULL && pEntity[0xB] != (int)uHandle) {
        pEntity = NULL;
        *pTrack = 0;
    }

    if (pEntity == NULL || pEntity[1] == 0) return;

    if (*(char*)((int)pTrack + 0x11) == '\0') {
        // Restore state
        pEntity[0x73] = (int)pTrack[2];  // Health
        *(u32*)(pEntity[1] + 0x30) = pTrack[1];  // Model variant
        if (nIsEntityType == 0) {
            if ((int*)pEntity[0x1F] != NULL) {
                *(u32*)(pEntity[0x1F] + 0x10) = pTrack[3];
            }
        } else {
            FUN_80070b80((int)pEntity, pTrack[3]);
        }
    } else {
        // Destroy
        if (nIsEntityType == 0 && (int*)pEntity[0x1F] != NULL) {
            *(int*)pEntity[0x1F] = 0;
        }
        FUN_800735e4(pEntity);
    }
}

// Address: 0x8003a28c | entity_needs_respawn
// Checks if an entity at given index needs respawning.
// Returns TRUE if both active flag and destroy flag are set.
BOOL entity_needs_respawn(int nIndex) {
    int pTrack = g_EntityInstanceTrack + nIndex * 0x14;
    if (*(char*)(pTrack + 0x10) == '\0') return FALSE;
    if (*(char*)(pTrack + 0x11) == '\0') return FALSE;
    return TRUE;
}

// Address: 0x8003a2bc | entity_restore_by_spawn_ptr
// Restores a specific entity by computing its index from spawn data pointer.
void entity_restore_by_spawn_ptr(int pEntity) {
    int nIdx = (*(int*)(pEntity + 0x2D8) - *(int*)(g_LevelContext + 0x0C)) / 0x60;
    if (nIdx < 0 || nIdx >= *(int*)(g_LevelContext + 0x10)) return;
    entity_restore_state_from_track((u32*)(g_EntityInstanceTrack + nIdx * 0x14), 1);
}

// Address: 0x8003a314 | level_save_all_entity_states
// Saves state of ALL entities and particles to tracking buffers.
// Also saves weapon spawn flags for respawn timing.
void level_save_all_entity_states(void) {
    int pLevelInfo = level_get_info_cached(g_CurrentLevel);

    // Save entity instances
    int nEntities = *(int*)(pLevelInfo + 0x10);
    for (int i = 0; i < nEntities; i++) {
        entity_save_state_to_track((u32*)(g_EntityInstanceTrack + i * 0x14), 1);
    }

    // Save particles
    int nParticles = *(int*)(pLevelInfo + 0x88);
    for (int i = 0; i < nParticles; i++) {
        entity_save_state_to_track((u32*)(g_ParticleTrack + i * 0x14), 0);
    }

    // Save weapon enable flags
    int nWeapons = *(int*)(pLevelInfo + 0x18);
    for (int i = 0; i < nWeapons; i++) {
        if (*(int*)(g_WeaponSpawnTrack + i * 4) != 0) {
            *(int*)(g_WeaponTimerTrack + i * 4) =
                *(int*)(*(int*)(g_WeaponSpawnTrack + i * 4) + 0x28);
        }
    }
}

// Address: 0x8003a3e8 | level_restore_all_entity_states
// Restores ALL entity and particle states from tracking buffers.
// Also restores weapon enable flags.
void level_restore_all_entity_states(void) {
    int pLevelInfo = level_get_info_cached(g_CurrentLevel);

    for (int i = 0; i < *(int*)(pLevelInfo + 0x10); i++) {
        entity_restore_state_from_track((u32*)(g_EntityInstanceTrack + i * 0x14), 1);
    }
    for (int i = 0; i < *(int*)(pLevelInfo + 0x88); i++) {
        entity_restore_state_from_track((u32*)(g_ParticleTrack + i * 0x14), 0);
    }
    for (int i = 0; i < *(int*)(pLevelInfo + 0x18); i++) {
        if (*(int*)(g_WeaponSpawnTrack + i * 4) != 0) {
            *(int*)(*(int*)(g_WeaponSpawnTrack + i * 4) + 0x28) =
                *(int*)(g_WeaponTimerTrack + i * 4);
        }
    }
}

// Address: 0x8003a4bc | spawn_find_team_character
// Finds a team-specific character entry in the character spawn table.
// Used for team-locked character assignments.
void spawn_find_team_character(int nTeamSlot) {
    int pLevelInfo = level_get_info_cached(g_CurrentLevel);
    if (pLevelInfo == 0 || g_CurrentLevel == 100) return;
    int nCount = *(int*)(pLevelInfo + 0x28);
    int iOffset = 0;
    int nFound = 0;
    for (int i = 0; i < nCount; i++) {
        if ((*(u32*)(*(int*)(pLevelInfo + 0x24) + iOffset) & 0x1800000) != 0) {
            if (nFound == nTeamSlot * 2) break;
            nFound++;
        }
        iOffset += 0x20;
    }
}

// Address: 0x8003a578 | spawn_always_true
// Stub: always returns 1.
BOOL spawn_always_true(void) { return TRUE; }

// Address: 0x8003a580 | spawn_create_decal_entity
// Creates a decal/overlay entity from spawn point data.
// Uses model ID at [2], position at [3-5], scale at [6-8].
int spawn_create_decal_entity(int* pSpawnData) {
    int pEntity = FUN_80072d70(pSpawnData[2], &pSpawnData[3], (float*)&pSpawnData[6], 0, (int)pSpawnData);
    if (pEntity == 0) return 0;
    FUN_8015d744();  // Decal-specific init
    FUN_8023c8a0(*pSpawnData, pEntity);  // Register with scene
    return pEntity;
}
