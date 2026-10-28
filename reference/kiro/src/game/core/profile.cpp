// Address: 0x8002e274-0x8002e3d8 | Name: profile | Date: 2026-08-05
// Player profile system: serializes player stats from active game state
// into saveable profile data slots. Each slot is 0x58 bytes.

#include "common.h"
#include "profile.h"

#define PROFILE_SLOT_SIZE  0x58
#define PROFILE_DATA_SIZE  0x4D0  // ~14 slots worth

// ============================================================================
// Address: 0x8002e274 | profile_get_slot
// Returns pointer to profile data slot at given index.
// ============================================================================
void* profile_get_slot(int nIndex) {
    return &g_ProfileData + nIndex * PROFILE_SLOT_SIZE;
}

// ============================================================================
// Address: 0x8002e288 | profile_serialize_players
// Serializes current player states into profile data slots.
// Copies: player index, team, per-opponent stats (if networking enabled),
// player name (32 chars), weapon stats, and various gameplay counters.
// Only active in non-story modes.
// ============================================================================
void profile_serialize_players(void) {
    int nPlayerCount = g_NumPlayersActive;

    if (g_GameMode == '\n') {
        FUN_8038272c();
        return;  // No profiles in story mode
    }

    for (int i = 0; i < nPlayerCount; i++) {
        int* pPlayerState = (int*)(g_PlayerDataB + i * 0xEA0);
        int nSlotOffset = i * PROFILE_SLOT_SIZE;

        // Player index and team
        g_ProfileSlots[nSlotOffset + 0] = (char)pPlayerState[2];
        g_ProfileSlots[nSlotOffset + 1] = (char)pPlayerState[0x21];

        // Per-opponent stats (if local multiplayer)
        if (g_NetworkFlag == 0) {
            for (int j = 0; j < nPlayerCount; j++) {
                *(u16*)(&g_ProfileSlots[nSlotOffset + 2 + j * 2]) =
                    *(u16*)((int)pPlayerState + 0xA10 + j * 2);
            }
            *(u16*)(&g_ProfileSlots[nSlotOffset + 0x40]) =
                *(u16*)((int)pPlayerState + 0xA52);
        }

        // Check if player has special flags
        if (*(int*)(*pPlayerState + 0x150) != 0) {
            int iPlayerData = g_PlayerDataA + pPlayerState[2] * 0x141C;
            if (*(int*)(iPlayerData + 4) >= 0) {
                *(u32*)(iPlayerData + 0xC) |= 0x40000;
            }
        }

        // Player name (32 chars)
        strncpy(&g_ProfileSlots[nSlotOffset + 0x20], (char*)pPlayerState[0x1C], 32);

        // Weapon and gameplay stats
        *(u16*)(&g_ProfileSlots[nSlotOffset + 0x42]) = *(u16*)(pPlayerState + 0x298);
        *(int*)(&g_ProfileSlots[nSlotOffset + 0x44]) = pPlayerState[0x299];
        *(int*)(&g_ProfileSlots[nSlotOffset + 0x48]) = pPlayerState[0x29A];
        *(int*)(&g_ProfileSlots[nSlotOffset + 0x4C]) = pPlayerState[0x29B];
        *(int*)(&g_ProfileSlots[nSlotOffset + 0x50]) = pPlayerState[0x29C];
        *(u16*)(&g_ProfileSlots[nSlotOffset + 0x54]) = *(u16*)(pPlayerState + 0x12);
        g_ProfileSlots[nSlotOffset + 0x56] = *(u8*)((int)pPlayerState + 0xC0D);
    }

    g_ProfileReady = 1;
    FUN_8038272c(); // epilogue
}

// ============================================================================
// Address: 0x8002e3d8 | profile_clear_all
// Clears all profile data slots and resets ready flag.
// ============================================================================
void profile_clear_all(void) {
    memset(&g_ProfileData, 0, PROFILE_DATA_SIZE);
    g_ProfileReady = 0;
}
