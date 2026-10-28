// Address: 0x80033c04-0x80034a24 | Name: player_slots | Date: 2026-08-05
// Player slot management, character select, slow-motion, and game utilities.
// Covers batches 047-049.
//
// Player slot system supports up to 8 players.
// Each slot is 0x1EDC bytes at base 0x80501608 + index * 0x1EDC.
// Slot[0] = pointer to bound entity (NULL = inactive).
// Entity layout:
//   +0x04: entity data base
//   +0x20: int levelId
//   +0x24: int modeId (-1 = uninitialized)
//   +0x28: int weaponId
//   +0x2E: s16 characterId
//   +0x30: int bOwned (1 = slot owns this entity, will free on release)
//   +0x34: int bNeedsInit
//   +0x38: int* pOwnerSlot (back-pointer to owning slot)

#include "common.h"
#include "player_slots.h"

#define MAX_PLAYER_SLOTS    8
#define PLAYER_SLOT_STRIDE  0x1EDC

// ============================================================================
// Character select (batch 047)
// ============================================================================

// Address: 0x80033c04 | charselect_stub_noop
void charselect_stub_noop(void) { return; }

// Address: 0x80033c88 | charselect_stub_noop2
void charselect_stub_noop2(void) { return; }

// Address: 0x80033c08 | charselect_confirm_enter_game
// Confirms character selection, spawns player entity, enters gameplay.
void charselect_confirm_enter_game(void) {
    *(u32*)(*g_PlayerStateBase + 0x28) |= 1;  // Enable entity
    FUN_8009c2a0(0);  // Reset camera
    game_set_phase(4);
    match_enter_gameplay();

    int* pSlotFlags = FUN_800342d0(0);
    *(u16*)(pSlotFlags + 2) &= ~1;
    if (g_InverseLookSetting == 0) {
        pSlotFlags = FUN_800342d0(0);
        *(u16*)(pSlotFlags + 2) |= 1;
    }
    g_CharSelectActive = 1;
}

// Address: 0x80033c8c | charselect_input_handler
// Handles input on the character select/inverse-look screen.
// D-pad left/right toggles inverse look, A button confirms.
void charselect_input_handler(void) {
    int iEntity = *g_PlayerStateBase;
    FUN_8009c2a0(1);  // Set camera mode
    FUN_8009d0c0(*(int*)(g_PlayerDataA + 8), &g_CharSelectCamTarget, 0);
    *(int*)(*(int*)(g_PlayerDataA + 8) + 0x2A0) = 1;
    *(u32*)(iEntity + 0x28) &= ~1;  // Disable entity movement
    g_CharSelectActive = 0;

    // D-pad toggles inverse look
    u32 buttons = input_get_buttons_held(0);
    if ((buttons & 8) != 0 || (buttons & 4) != 0) {
        g_InverseLookSetting = 1 - g_InverseLookSetting;
    }

    // A confirms
    buttons = input_get_buttons_held(0);
    if ((buttons & 0x80) != 0) {
        charselect_confirm_enter_game();
    }
    input_reset_pad(0);
}

// Address: 0x80033d44 | charselect_draw_inverse_look
// Renders the "Inverse Look ON/OFF" selection UI.
// Uses text_draw_char_shadowed for highlighted/unhighlighted items.
void charselect_draw_inverse_look(void) {
    // [Large rendering function - draws two text options with selection highlight]
    // Uses: text_set_font_table(0x16), text_get_char_width, text_draw_char_shadowed
    // Highlighted item: color 0x7F7F7F7F, shadow 0x28002F
    // Normal item: color 0x7F7F7F50, shadow 0x1E
}

// ============================================================================
// Slow-motion system (batch 047)
// ============================================================================

// Address: 0x80034104 | slowmo_reset
// Resets game speed to normal (1.0) and clears the slow-motion flag.
void slowmo_reset(void) {
    g_GameSpeedMultiplier = 1.0f;
    g_GameStateFlags &= ~0x800;
}

// Address: 0x80034124 | slowmo_update
// Per-frame slow-motion interpolation. Uses quadratic ease-out:
// speed = base + factor * (elapsed/duration)^2
// When duration is reached, resets to normal speed.
void slowmo_update(void) {
    if ((g_GameStateFlags & 0x800) == 0) return;

    g_SlowmoElapsed += g_DeltaTime * (1.0f / (float)g_FrameRate);

    if (g_SlowmoElapsed < g_SlowmoDuration) {
        // Quadratic interpolation back to full speed
        float t = g_SlowmoElapsed / g_SlowmoDuration;
        g_GameSpeedMultiplier = SLOWMO_FACTOR * t * t + SLOWMO_BASE;
    } else {
        // Duration complete, restore normal speed
        g_GameStateFlags &= ~0x800;
        g_GameSpeedMultiplier = 1.0f;
        int pCam = FUN_8009c164();
        if (pCam != 0 && *(int*)(pCam + 0x218) != 0x1B) {
            FUN_80026da8(0);  // Restore camera mode
        }
    }
}

// Address: 0x800341e8 | slowmo_activate
// Activates slow-motion for a given duration.
void slowmo_activate(float fDuration) {
    g_SlowmoDuration = fDuration;
    g_SlowmoElapsed = 0.0f;
    if ((g_GameStateFlags & 0x800) == 0) {
        FUN_80026da8(1);  // Switch camera to slowmo mode
    }
    g_GameStateFlags |= 0x800;
    g_GameSpeedMultiplier = SLOWMO_BASE;
}

// ============================================================================
// Match load (batch 047)
// ============================================================================

// Address: 0x800340a8 | match_load_player_actor
// Loads the player's selected character actor on match start.
void match_load_player_actor(void) {
    if (g_GameMode != '\n' || g_CurrentLevel == 100) return;
    FUN_8006cb9c();
    int nActorId = level_get_player_actor_id(0);
    if (nActorId != -1) {
        level_actor_switch(nActorId);
    }
}

// Address: 0x80034248 | hud_init_weapon_display
// Initializes a HUD weapon/ammo display element.
void hud_init_weapon_display(u16* pElement) {
    pElement[0] = 1;
    pElement[1] = 0x8D | 0x70;
    *(int*)(pElement + 0x3A) = 2;
    *(int*)(pElement + 0x2E) = 0;
    *(int*)(pElement + 0x34) = 4;
    *(float*)(pElement + 0x3C) = HUD_WEAPON_SCALE;
    *(int*)(pElement + 0x36) = 3;
    *(int*)(pElement + 0x38) = 0;
    FUN_80254ed8(1, (int*)(pElement + 2));
    int uResult = FUN_802c7ba0((int*)(pElement + 0x32));
    *(int*)(pElement + 0x30) = uResult;
}

// ============================================================================
// Stats data access (batch 048)
// ============================================================================

// Address: 0x800342d0 | savegame_get_player_block
// Returns pointer to player's save data block. Stride = 0x1EDC per player.
void* savegame_get_player_block(int nPlayer) {
    return (void*)(0x80501608 + nPlayer * PLAYER_SLOT_STRIDE + 4);
}

// Address: 0x800342e8 | stats_merge_all_players
// Merges stats across all players into a combined totals block.
// For kill counts: sums across all players.
// For time records: uses best (min or max depending on challenge type).
// For completion bits: OR across all players.
void stats_merge_all_players(int nPlayerCount) {
    if ((g_GameStateFlags & 0x800000) != 0) {
        // Network mode: just clear and use single source
        stats_clear_combined((int)&g_CombinedStats);
        g_CombinedStatsPtr = &g_CombinedStats;
        return;
    }

    if (nPlayerCount == 1) {
        // Single player: point directly to their stats
        if (!player_slot_is_active(0)) {
            g_CombinedStatsPtr = &g_CombinedStats;
            return;
        }
        g_CombinedStatsPtr = stats_get_player_ptr(0);
    } else {
        // Multi-player: aggregate
        memset(&g_CombinedStats, 0, 0xC4C);
        for (int i = 0; i < nPlayerCount; i++) {
            void* pStats = stats_get_player_ptr(i);

            // Sum kill/usage counts (0xA0 DWORDs at +0x08)
            for (int j = 0; j < 0xA0; j++) {
                g_CombinedStats.counts[j] += ((int*)pStats)[j + 2];
            }

            // OR completion bitfields
            for (int d = 0; d < 3; d++) {
                g_CombinedStats.completionBits[d] |=
                    *(u32*)((int)pStats + 0xBF4 + d * 4);
                g_CombinedStats.completionBits2[d] |=
                    *(u32*)((int)pStats + 0xC00 + d * 4);
            }
            g_CombinedStats.masterBits |= *(u32*)((int)pStats + 0xBF0);

            // Best-score merge for challenge records (48 entries)
            for (int c = 0; c < 48; c++) {
                int* pCombined = &g_CombinedStats.challenges[c];
                int* pPlayer = (int*)((int)pStats + c * 0x18 + 0x490);
                if (*pCombined == 0) {
                    pCombined[1] = pPlayer[1];
                } else if (*pPlayer != 0) {
                    int nType = FUN_8022b110(c)->type;
                    if (nType == 2) {
                        // Exact match: keep either
                    } else if (nType < 2) {
                        // Min is better (time)
                        if (pPlayer[1] < pCombined[1]) pCombined[1] = pPlayer[1];
                    } else if (nType < 5) {
                        // Max is better (score)
                        if (pPlayer[1] > pCombined[1]) pCombined[1] = pPlayer[1];
                    }
                }
                *pCombined += *pPlayer;
            }
        }
        g_CombinedStatsPtr = &g_CombinedStats;
    }
    FUN_801c0818();
}

// Address: 0x80034550 | stats_get_combined_ptr
void* stats_get_combined_ptr(void) {
    return g_CombinedStatsPtr;
}

// Address: 0x80034558 | stats_get_player_ptr
// Returns per-player stats pointer with validity check.
void* stats_get_player_ptr(int nPlayer) {
    if ((g_GameStateFlags & 0x800000) != 0) {
        return &g_CombinedStats;
    }
    if (player_slot_is_active(nPlayer)) {
        return (void*)(0x80502898 + nPlayer * PLAYER_SLOT_STRIDE);
    }
    return &g_CombinedStats;  // Fallback
}

// Address: 0x800345cc | stats_get_challenge_record
void* stats_get_challenge_record(int nPlayer, int nIndex) {
    return (void*)((int)stats_get_player_ptr(nPlayer) + nIndex * 0x18 + 0x490);
}

// Address: 0x80034608 | stats_get_award_record
void* stats_get_award_record(int nPlayer, int nIndex) {
    return (void*)((int)stats_get_player_ptr(nPlayer) + nIndex * 0x28 + 0x288);
}

// Address: 0x80034644 | stats_clear_combined
void stats_clear_combined(int pBlock) {
    memset((void*)pBlock, 0, 0xC4C);
}

// Address: 0x8003466c | game_get_settings_ptr
void* game_get_settings_ptr(void) {
    return &g_GameSettings;
}

// Address: 0x8003467c | stats_clear_player_block
void stats_clear_player_block(int pBlock) {
    memset((void*)pBlock, 0, 0x1210);
    FUN_802d45fc();  // Reinitialize
}

// Address: 0x800346b8 | stats_clear_all_memory
void stats_clear_all_memory(void) {
    memset(&g_AllPlayerStats, 0, 0xF6E0);
    memset(&g_StatsOverflow, 0, 0x2200);
    g_CombinedStatsPtr = NULL;
}

// ============================================================================
// Player slot management (batch 049)
// ============================================================================

// Address: 0x80034708 | player_slot_is_active
// Returns TRUE if the player slot has a bound entity.
BOOL player_slot_is_active(int nPlayer) {
    return *(int*)(0x80501608 + nPlayer * PLAYER_SLOT_STRIDE) != 0;
}

// Address: 0x80034724 | player_slot_get_data
// Returns pointer to player slot data with bounds checking (0-7).
// Returns NULL if index out of range or slot inactive.
void* player_slot_get_data(int nPlayer) {
    player_slot_is_active(nPlayer);  // Side-effect check
    if (nPlayer < 0 || nPlayer > 7) return NULL;
    if (!player_slot_is_active(nPlayer)) return NULL;
    return (void*)(0x80501608 + nPlayer * PLAYER_SLOT_STRIDE);
}

// Address: 0x80034788 | player_get_entity_ptr
// Returns pointer to player's entity data (+4 from entity base).
void* player_get_entity_ptr(int nPlayer) {
    int* pSlot = (int*)player_slot_get_data(nPlayer);
    if (pSlot == NULL) return NULL;
    return (void*)(*pSlot + 4);
}

// Address: 0x800347c0 | player_get_level_mode
// Returns the player's current level/mode pair (u64 at entity+0x20).
u64 player_get_level_mode(int nPlayer) {
    int* pSlot = (int*)player_slot_get_data(nPlayer);
    return *(u64*)(*pSlot + 0x20);
}

// Address: 0x800347ec | player_get_character_id
// Returns the player's character ID (s16 at entity+0x2E).
int player_get_character_id(int nPlayer) {
    int* pSlot = (int*)player_slot_get_data(nPlayer);
    return (int)*(s16*)(*pSlot + 0x2E);
}

// Address: 0x80034814 | player_get_weapon_id
// Returns the player's current weapon ID (int at entity+0x28).
int player_get_weapon_id(int nPlayer) {
    int* pSlot = (int*)player_slot_get_data(nPlayer);
    return *(int*)(*pSlot + 0x28);
}

// Address: 0x8003483c | player_slot_bind_entity
// Binds an entity to a player slot. Handles ownership transfer:
// - If slot already active with different entity: releases old
// - If entity was owned by another slot: transfers ownership
// - If entity is fresh (level/mode == -1/-1): inits HUD + stats
void player_slot_bind_entity(int nPlayer, void* pUnused, int pEntity) {
    int* pSlot = (int*)(0x80501608 + nPlayer * PLAYER_SLOT_STRIDE);
    BOOL bSameEntity = FALSE;

    if (player_slot_is_active(nPlayer)) {
        if (pEntity == 0 || *pSlot != pEntity) {
            player_slot_release_wrapper(nPlayer);
        } else {
            bSameEntity = TRUE;
        }
    }

    if (pEntity == 0) {
        // Allocate new entity
        int newEntity = FUN_80034a8c(nPlayer, -1, 0);
        *pSlot = newEntity;
        *(int**)(*pSlot + 0x38) = pSlot;
        *(int*)(*pSlot + 0x30) = 1;  // Owned
    } else {
        // Bind existing entity
        if (*(int**)(pEntity + 0x38) != NULL) {
            **(int**)(pEntity + 0x38) = 0;  // Clear old owner
        }
        *pSlot = pEntity;
        *(int**)(pEntity + 0x38) = pSlot;
        if (!bSameEntity) {
            if ((*(int*)(pEntity + 0x24) != -1 || *(int*)(pEntity + 0x20) != -1) &&
                (*(int*)(pEntity + 0x24) != 2 || *(int*)(pEntity + 0x20) != 0)) {
                *(int*)(pEntity + 0x34) = 1;  // Needs init
            }
        }
    }

    // Initialize HUD/stats if entity is fresh
    int entity = *pSlot;
    if (entity != 0 && *(int*)(entity + 0x24) == -1 && *(int*)(entity + 0x20) == -1) {
        hud_init_weapon_display((u16*)(nPlayer * PLAYER_SLOT_STRIDE + 0x80501614));
        stats_clear_player_block(nPlayer * PLAYER_SLOT_STRIDE + 0x80501688);
        stats_clear_combined(nPlayer * PLAYER_SLOT_STRIDE + 0x80502898);
    }
}

// Address: 0x800349c0 | player_slot_release
// Releases an entity from a player slot. Frees entity if owned.
void player_slot_release(int* pSlot) {
    int entity = *pSlot;
    if (entity == 0) {
        *pSlot = 0;
        return;
    }
    if (*(int*)(entity + 0x30) == 0) {
        // Not owned: just clear back-pointer
        *(int*)(entity + 0x38) = 0;
    } else {
        // Owned: clear and free
        *(int*)(entity + 0x38) = 0;
        FUN_80034b64((int*)entity);
    }
    *pSlot = 0;
}

// Address: 0x80034990 | player_slot_release_wrapper
void player_slot_release_wrapper(int nPlayer) {
    player_slot_release((int*)(0x80501608 + nPlayer * PLAYER_SLOT_STRIDE));
}

// Address: 0x80034a24 | player_slot_set_properties
// Sets level, mode, weapon ID, and character ID on a player slot's entity.
void player_slot_set_properties(int nPlayer, int nLevelId, int nModeId,
                                int nWeaponId, s16 nCharId) {
    int* pSlot = (int*)player_slot_get_data(nPlayer);
    int entity = *pSlot;
    *(int*)(entity + 0x30) = 0;  // Clear owned flag
    *(int*)(entity + 0x24) = nModeId;
    *(int*)(entity + 0x20) = nLevelId;
    *(int*)(entity + 0x28) = nWeaponId;
    *(s16*)(entity + 0x2E) = nCharId;
}
