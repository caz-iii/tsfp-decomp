// Address: 0x800372ec-0x80037940 | Name: level_preload | Date: 2026-08-05
// Level resource preloading: ensures all required models, sounds, and
// entities are loaded into memory before gameplay starts.
// Called during level load phase to trigger streaming queue requests.

#include "common.h"
#include "level_preload.h"
#include "spawn_system.h"
#include "level_config.h"
#include "sound.h"

// ============================================================================
// Address: 0x800372ec | spawn_check_flags_or_cutscene
// Checks spawn flags. In cutscene mode, uses bit 18 directly.
// Otherwise delegates to spawn_flags_match_mode.
// ============================================================================
BOOL spawn_check_flags_or_cutscene(u32 uFlags) {
    if ((g_GameStateFlags & 0x20000000) != 0) {
        return (uFlags >> 18) & 1;  // Cutscene: use bit 18
    }
    return spawn_flags_match_mode(uFlags);
}

// ============================================================================
// Address: 0x80037328 | spawn_check_flags_with_unlock
// Validates spawn flags with additional progression/unlock check.
// For arcade hub (level 100): uses hub-specific check.
// Otherwise: checks unlock bit against player progression level.
// ============================================================================
BOOL spawn_check_flags_with_unlock(u32 uFlags, u32 uUnlockBits) {
    if (g_CurrentLevel == 100) {
        return spawn_check_flags_hub(uFlags);
    }

    int nProgression = FUN_8025b920();
    if ((uUnlockBits & 0x10000) == 0) {
        // Standard: check if player has reached required level
        if (nProgression > 0 && ((1 << (nProgression - 1)) & uUnlockBits) == 0) {
            return FALSE;
        }
    } else {
        // Extended: requires specific progression + bit match
        if (nProgression == 0 && (uUnlockBits & ~0x10000) != 0) return FALSE;
        if (((1 << (nProgression - 1)) & (uUnlockBits & ~0x10000)) == 0) return FALSE;
    }
    return spawn_check_flags_or_cutscene(uFlags);
}

// ============================================================================
// Address: 0x800373e4 | spawn_check_flags_hub
// Wrapper: calls spawn_check_flags_or_cutscene (for arcade hub).
// ============================================================================
BOOL spawn_check_flags_hub(u32 uFlags) {
    return spawn_check_flags_or_cutscene(uFlags);
}

// ============================================================================
// Address: 0x80037404 | spawn_check_flags_preload
// Wrapper: calls spawn_flags_match_mode during resource preload phase.
// ============================================================================
BOOL spawn_check_flags_preload(u32 uFlags) {
    return spawn_flags_match_mode(uFlags);
}

// ============================================================================
// Address: 0x80037424 | preload_entity_model
// Preloads an entity's model by resolving entity handle → asset ID,
// then requesting the model be loaded into memory.
// ============================================================================
void preload_entity_model(int pEntity) {
    int nAssetId = FUN_800b0704(pEntity);
    FUN_80070ba8(nAssetId);  // Request model load
}

// ============================================================================
// Address: 0x80037448 | level_preload_special_entities
// Preloads special game-mode entities:
// - Bags for bag-tag mode (mode 2): 2 bag entities (type 1)
// - Flag for CTF mode (sub-mode 10): 1 flag entity (type 0x400)
// - Story mode: preloads additional systems
// ============================================================================
void level_preload_special_entities(void) {
    if (g_GameMode == '\x02') {
        // Bag-tag: preload 2 bag models
        for (int i = 0; i < 2; i++) {
            void* pEntity = FUN_80097fa0(1, i);
            preload_entity_model((int)pEntity);
        }
    }
    if (g_GameSubMode == '\n') {
        // CTF: preload flag model
        void* pFlag = FUN_80097fa0(0x400, 0);
        preload_entity_model((int)pFlag);
    }
    if (g_GameMode == '\n') {
        FUN_801432dc();  // Story mode: preload extra systems
    }
}

// ============================================================================
// Address: 0x800374dc | preload_script_with_sounds
// Preloads a script/cutscene entity's model and 4 sound bank references.
// ============================================================================
void preload_script_with_sounds(int pScriptEntry) {
    FUN_80070ba8(*(int*)(pScriptEntry + 8));  // Model
    sound_clear_bank_ref(*(int*)(pScriptEntry + 0x2C));  // Sound 1
    sound_clear_bank_ref(*(int*)(pScriptEntry + 0x30));  // Sound 2
    sound_clear_bank_ref(*(int*)(pScriptEntry + 0x34));  // Sound 3
    sound_clear_bank_ref(*(int*)(pScriptEntry + 0x38));  // Sound 4
}

// ============================================================================
// Address: 0x8003752c | preload_sound_zone_model
// Preloads a sound zone's referenced model asset.
// ============================================================================
void preload_sound_zone_model(int pSoundZone) {
    FUN_80070ba8(*(int*)(pSoundZone + 8));
}

// ============================================================================
// Address: 0x80037550 | level_preload_all_resources
// MASSIVE: Master level resource preloader. Called during level load to
// ensure ALL required assets are queued for streaming before gameplay.
//
// Preloads in order:
// 1. Sound bank tables for the level
// 2. Level .dat file (via level_load_dat_file)
// 3. Entity instance models (filtered by mode flags)
// 4. Additional level geometry models
// 5. Special mode entities (bags, flags)
// 6. Weapon/pickup models (filtered by mode, handles gun-free mode)
// 7. AI waypoint linked models + sound banks
// 8. Script/cutscene entities + their 4 sound refs each
// 9. Campaign-only: ambient sound samples (3 per entry)
// 10. Trigger models (filtered by mode flags)
// 11. Particle effect trigger data
// 12. Mapmaker tile entities (if mapmaker mode)
// 13. Particle emitter entities (filtered by mode)
// 14. Sound zone models
// 15. Decal/overlay data
// 16. Level-specific init callback (if present at +0x104)
// 17. Stats tracking entity (ID 0xE5, if stats active)
// 18. Final lighting/environment setup
//
// For arcade hub (level 100): uses simplified path (no .dat file,
// uses dynamic scene setup via FUN_80239270).
// ============================================================================
void level_preload_all_resources(void) {
    int pLevelInfo = level_get_info_cached(g_CurrentLevel);
    if (pLevelInfo == 0) { FUN_80382730(); return; }

    // 1. Sound banks
    sound_init_bank_tables(g_CurrentLevel);

    if (g_CurrentLevel == 100) {
        // Arcade hub: simplified preload
        g_SpawnTrackingIdx = 0;
        FUN_80239270();
        level_preload_special_entities();
        FUN_802321f8();
    } else {
        // 2. Load level .dat file
        level_load_dat_file();

        // 3. Entity instances (stride 0x60)
        for (int i = 0; i < *(int*)(pLevelInfo + 0x10); i++) {
            int pEntry = *(int*)(pLevelInfo + 0x0C) + i * 0x60;
            if (spawn_check_flags_preload(*(u32*)(pEntry + 8))) {
                FUN_80070ba8(*(int*)(pEntry + 0x0C));
            }
        }

        // 4. Additional geometry (stride 0x34)
        for (int i = 0; i < *(int*)(pLevelInfo + 0xC0); i++) {
            FUN_80070ba8(*(int*)(*(int*)(pLevelInfo + 0xBC) + i * 0x34 + 8));
        }

        // 5. Special entities
        level_preload_special_entities();

        // 6. Weapons/pickups (stride 0x58)
        for (int i = 0; i < *(int*)(pLevelInfo + 0x18); i++) {
            u32* pWeapon = (u32*)(*(int*)(pLevelInfo + 0x14) + i * 0x58);
            int nResolvedId = weapon_resolve_index_to_id(pWeapon[1], (int)*(s16*)(pWeapon + 4));
            if (spawn_check_flags_preload(*pWeapon)) {
                u32 nType = pWeapon[1];
                // Skip weapons in gun-free mode (except arcade hub and mapmaker)
                if ((nType == 2 || nType == 0x40) && (g_GameStateFlags & 0x100000) != 0) {
                    if (g_CurrentLevel != 100 && g_CurrentLevel != 0x66 &&
                        (g_GameStateFlags & 0x40) == 0 && g_GameMode == '\n') {
                        continue;
                    }
                }
                int pEntity = FUN_80097eec(nType, nResolvedId, (int)*(s16*)((int)pWeapon + 0x12));
                if (pEntity != 0) preload_entity_model(pEntity);
                if (pWeapon[1] == 0x400) FUN_80070ba8(300);
            }
        }

        // 7. AI waypoints (stride 0x98)
        for (int i = 0; i < *(int*)(pLevelInfo + 0x78); i++) {
            int pWaypoint = *(int*)(pLevelInfo + 0x74) + i * 0x98;
            int nModelId = (int)*(s16*)(pWaypoint + 0x14);
            if (nModelId != -1) {
                void* pModel = FUN_80097fa0(0x200, nModelId);
                preload_entity_model((int)pModel);
            }
            sound_clear_bank_ref(*(int*)(pWaypoint + 0x8C));
        }

        // 8. Scripts (stride 0x6C)
        for (int i = 0; i < *(int*)(pLevelInfo + 0x20); i++) {
            preload_script_with_sounds(*(int*)(pLevelInfo + 0x1C) + i * 0x6C);
        }

        // 9. Campaign ambient sounds (stride 0x60, 3 sounds each)
        if (g_GameMode == '\x03') {
            for (int i = 0; i < *(int*)(pLevelInfo + 0xE8); i++) {
                int pEntry = *(int*)(pLevelInfo + 0xE4) + i * 0x60;
                sound_clear_bank_ref(sound_resolve_sample_id(*(int*)(pEntry + 8)));
                sound_clear_bank_ref(sound_resolve_sample_id(*(int*)(pEntry + 0xC)));
                sound_clear_bank_ref(sound_resolve_sample_id(*(int*)(pEntry + 0x10)));
            }
            sound_clear_bank_ref(0xB9C);
        }

        // 10. Triggers (stride 0x18)
        for (int i = 0; i < *(int*)(pLevelInfo + 0x80); i++) {
            u32* pTrigger = (u32*)(*(int*)(pLevelInfo + 0x7C) + i * 0x18);
            if (spawn_check_flags_preload(*pTrigger)) {
                FUN_80070ba8(pTrigger[1]);
            }
        }

        // 11. Particle triggers (stride 0x8C)
        for (int i = 0; i < *(int*)(pLevelInfo + 0x88); i++) {
            FUN_80088104(*(int*)(pLevelInfo + 0x84) + i * 0x8C);
        }

        // 12. Mapmaker tiles (stride 0x40, if mapmaker mode)
        for (int i = 0; i < *(int*)(pLevelInfo + 0x100); i++) {
            if ((g_GameStateFlags & 0x40) != 0) {
                FUN_80223584(*(int*)(pLevelInfo + 0xFC) + i * 0x40);
            }
        }

        // 13. Particle emitters (stride 0xAC)
        for (int i = 0; i < *(int*)(pLevelInfo + 0xA8); i++) {
            int* pEmitter = (int*)(*(int*)(pLevelInfo + 0xA4) + i * 0xAC);
            if (spawn_check_flags_preload(pEmitter[2])) {
                FUN_80082838(pEmitter);
            }
        }

        // 14. Sound zones (stride 0x4C)
        for (int i = 0; i < *(int*)(pLevelInfo + 0xB0); i++) {
            preload_sound_zone_model(*(int*)(pLevelInfo + 0xAC) + i * 0x4C);
        }

        // 15. Decals (stride 0x40)
        for (int i = 0; i < *(int*)(pLevelInfo + 0xE0); i++) {
            FUN_8015d1dc(*(int*)(pLevelInfo + 0xDC) + i * 0x40);
        }

        // 16. Level-specific init callback
        if (*(void**)(pLevelInfo + 0x104) != NULL) {
            (*(void(*)(void))*(int*)(pLevelInfo + 0x104))();
        }

        // 17. Stats entity
        if (stats_is_tracking_active()) {
            FUN_80070ba8(0xE5);
        }

        // 18. Final setup
        FUN_8036f6bc();
    }
    FUN_80382730(); // epilogue
}

// ============================================================================
// Address: 0x80037940 | spawn_alloc_tracking_buffer
// Allocates a 224-byte (0xE0) buffer for spawn point tracking state.
// ============================================================================
void spawn_alloc_tracking_buffer(void) {
    g_SpawnTrackingIdx = 0;
    g_SpawnTrackingBuf = (int)mem_alloc(0xE0, 1);
}
