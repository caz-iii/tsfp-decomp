// Address: 0x800ab2a4 - 0x800ab6c8 | Batch 219 | Date: 2026-08-11
// Player state management utilities, entity reference cleanup,
// death camera mode selection, event handler registration,
// and AI/bot player system allocation/teardown.

#include "common.h"
#include "player.h"
#include "entity.h"
#include "event.h"

// ============================================================================
// 0x800ab2a4 | player_set_state
// Sets the player state field (+0x114), saving the previous state to +0x110.
// Clears the state transition timer (+0x118) and removes flag bits 0x20000
// and 0x400 from the player flags (+0xC).
// Does NOT transition if the player is currently dead (state == 0x10).
// 5+ callers across state transition code.
// ============================================================================
void player_set_state(int pPlayerSlot, int newState) {
    if (*(int*)(pPlayerSlot + 0x114) == 0x10) {
        return;  // Cannot change state while dead
    }
    *(int*)(pPlayerSlot + 0x110) = *(int*)(pPlayerSlot + 0x114);  // Save previous
    *(int*)(pPlayerSlot + 0x114) = newState;
    *(int*)(pPlayerSlot + 0x118) = 0;  // Clear transition timer
    *(unsigned int*)(pPlayerSlot + 0xc) &= ~(0x20000 | 0x400);  // Clear flags
}

// ============================================================================
// 0x800ab2d8 | player_get_camera_slot_index
// Returns the camera slot index for a player by computing offset from the
// camera pool base (DAT_80611d5c). Each slot is 0xFFC bytes.
// Returns -1 if the player has no camera slot assigned.
// ============================================================================
int player_get_camera_slot_index(int pPlayerSlot) {
    int pCameraSlot = *(int*)(pPlayerSlot + 8);
    if (pCameraSlot != 0) {
        return (pCameraSlot - g_pCameraPoolBase) / 0xffc;
    }
    return -1;
}

// ============================================================================
// 0x800ab300 | player_clear_entity_references
// Iterates all local player slots and nullifies any reference to the given
// entity ID. Checks 4 reference fields per slot:
//   +0xE10: Interact target entity
//   +0xE14: Look-at target entity
//   +0xE1C: Damage source entity
//   +0xE8C: Last attacker entity
// Called when an entity is destroyed to prevent dangling pointers.
// ============================================================================
void player_clear_entity_references(int entityId) {
    if (g_pPlayerSlotBase == 0) return;

    int offset = 0;
    for (int i = 0; i < g_nPlayerCount; i++) {
        if (*(int*)(g_pPlayerSlotBase + offset + 0xe10) == entityId) {
            *(int*)(g_pPlayerSlotBase + offset + 0xe10) = 0;
        }
        if (*(int*)(g_pPlayerSlotBase + offset + 0xe14) == entityId) {
            *(int*)(g_pPlayerSlotBase + offset + 0xe14) = 0;
        }
        if (*(int*)(g_pPlayerSlotBase + offset + 0xe1c) == entityId) {
            *(int*)(g_pPlayerSlotBase + offset + 0xe1c) = 0;
        }
        if (*(int*)(g_pPlayerSlotBase + offset + 0xe8c) == entityId) {
            *(int*)(g_pPlayerSlotBase + offset + 0xe8c) = 0;
        }
        offset += 0x141c;
    }
}

// ============================================================================
// 0x800ab3a0 | player_has_weapon_equipped
// Returns 1 if the player's character data has bit 2 set in the weapon
// flags field at +0x10 of the character struct (accessed via entity +0x7c).
// Used to gate weapon-related operations (animations, HUD, etc).
// ============================================================================
int player_has_weapon_equipped(int pEntity) {
    int pCharData = *(int*)(pEntity + 0x7c);
    if ((*(unsigned int*)(pCharData + 0x10) & 4) != 0) {
        return 1;
    }
    return 0;
}

// ============================================================================
// 0x800ab410 | player_get_death_camera_mode
// Determines which death camera mode to use based on game mode and state:
//   - Character types 0x34, 0x2D, 0x36: check entity flag 0x80 -> return 5
//   - Mode 100 (story): return 3
//   - Mode 0x11 (assault) with AI/no weapon: return 7
//   - Story flag 0x40 set: delegate to FUN_8022d5a0
//   - Otherwise: return -1 (no special camera)
// Type 0x36 also triggers FUN_800fd5d0 in mode 0xE (virus).
// ============================================================================
int player_get_death_camera_mode(int pCharData) {
    short charType = *(short*)(pCharData + 0x48);

    // Special character types that may use camera mode 5
    if (charType == 0x34 || charType == 0x2d) {
        if ((*(unsigned int*)(*(int*)(pCharData + 0xbf0) + 0x54) & 0x80) == 0) {
            return 5;
        }
    } else if (charType == 0x36) {
        if (g_nGameMode == 0xe) {
            FUN_800fd5d0(pCharData + 0xc68);
        }
        if ((*(unsigned int*)(*(int*)(pCharData + 0xbf0) + 0x54) & 0x80) == 0) {
            return 5;
        }
    }

    // Mode-based selection
    if (g_nGameMode == 100) {
        return 3;  // Story mode death camera
    } else if (g_nGameMode == 0x11 &&
               (pCharData == 0 || g_nPlayerCount <= *(int*)(pCharData + 8)) &&
               (*(int*)(pCharData + 0xbf0) == 0 ||
                (*(unsigned int*)(*(int*)(pCharData + 0xbf0) + 0x54) & 0x80) == 0)) {
        return 7;  // Assault mode AI death camera
    } else if ((g_nGameFlags1 & 0x40) == 0) {
        return -1;  // No special camera
    } else {
        return FUN_8022d5a0(pCharData);  // Story scripted camera
    }
}

// ============================================================================
// 0x800ab51c | player_register_event_handlers
// Registers two event handlers for event type 8 (player events):
//   Slot 3: FUN_800b0274 (player spawn handler)
//   Slot 4: FUN_800b04cc (player destroy handler)
// Called from game_tick_update during initialization.
// ============================================================================
void player_register_event_handlers(void) {
    event_set_handler(8, 3, FUN_800b0274);
    event_set_handler(8, 4, FUN_800b04cc);
}

// ============================================================================
// 0x800ab560 | player_ai_pool_reset
// Clears all AI player pool memory. Pool size is 32 slots (0xEA0 each) in
// normal modes, or 4 slots in mode 0x65 (challenge/tutorial).
// Resets the AI active counter to 0.
// ============================================================================
void player_ai_pool_reset(void) {
    int slotCount = 0x20;
    if (g_nGameMode == 0x65) {
        slotCount = 4;
    }
    memset(g_pAIPlayerPool, 0, slotCount * 0xea0);
    g_nAIPlayerCount = 0;
}

// ============================================================================
// 0x800ab5b0 | player_ai_system_alloc
// Allocates the AI player system pool and initializes globals.
// Pool size: 32 * 0xEA0 bytes (normal) or 4 * 0xEA0 (mode 0x65).
// Also clears the 0xA0-byte AI state struct and 0x21C0-byte AI data buffer.
// Checks combined bot flags (DAT_806125fc & DAT_80612600 & capability & 0x1000)
// to determine if bots should be enabled.
// Only allocates if DAT_80611930 != 0 (multiplayer active).
// ============================================================================
void player_ai_system_alloc(void) {
    int slotCount = 0x20;
    if (g_nGameMode == 0x65) {
        slotCount = 4;
    }

    if (g_bMultiplayerActive == 0) return;

    // Clear AI state structs
    memset(&g_AIStateCompact, 0, 0xa0);
    memset(&g_AIStateExtended, 0, 0x21c0);

    // Allocate pool
    g_pAIPlayerPool = mem_alloc(slotCount * 0xea0, 1);

    // Clear bot lookup table (0xB1 entries * 4 bytes)
    for (int i = 0; i < 0xb1; i++) {
        g_pBotLookupTable[i] = 0;
    }

    // Check bot capability flags
    unsigned int caps = FUN_801c0894();
    int botsEnabled = 0;
    if ((g_nPlayerFlags1 & g_nPlayerFlags2 & caps & 0x1000) != 0) {
        if (g_nGameMode != 0x65 || g_nBotDifficulty == 5) {
            botsEnabled = 1;
        }
    }

    g_nAIActiveSlot = 0;
    g_bBotsEnabled = botsEnabled;
    player_ai_pool_reset();
}

// ============================================================================
// 0x800ab6b0 | player_ai_system_shutdown
// Nullifies the AI pool pointer during shutdown. Conditional on
// DAT_80611568 (level transition flag) - skips if transitioning
// to preserve state across level loads.
// ============================================================================
void player_ai_system_shutdown(void) {
    if (g_bLevelTransition != 0) return;
    g_pAIPlayerPool = NULL;
}

// ============================================================================
// 0x800ab6c8 | player_ai_swap_buffers
// Swaps the AI state double-buffer pointer. Copies the back buffer pointer
// to the front buffer, making last frame's AI decisions available for
// rendering/interpolation this frame.
// ============================================================================
void player_ai_swap_buffers(void) {
    g_pAIStateFront = g_pAIStateBack;
}
