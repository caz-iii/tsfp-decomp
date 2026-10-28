// Address: 0x8002f1b4-0x8002f604 | Name: gamemode | Date: 2026-08-05
// Game mode configuration, time formatting, and match queries.
// Handles mode table lookups, score type classification, bot support
// checks, and time display formatting.

#include "common.h"
#include "gamemode.h"
#include "scoring.h"

// Mode table: 17 entries at stride 0x3C, indexed by mode ID.
// Score types: 1=kills, 2=time/objective, 3=virus(lives)

// ============================================================================
// Address: 0x8002f1b4 | award_get_name
// Gets the display name for an award by ID.
// Returns male (param_1==0) or female (param_1==1) name variant.
// ============================================================================
u16 award_get_name(int nGenderVariant, int nAwardId) {
    u16* pEntry = (u16*)award_get_entry(nAwardId);
    if (pEntry == NULL) return 0;
    if (nGenderVariant == 0) {
        return pEntry[6];   // Male name
    }
    return pEntry[7];       // Female name
}

// ============================================================================
// Address: 0x8002f20c | gamemode_find_in_table
// Searches the game mode table (17 entries) for a mode ID.
// Returns table index (0-16), or -1 if not found.
// ============================================================================
int gamemode_find_in_table(int nModeId) {
    int iOffset = 0;
    for (int i = 0; i < 17; i++) {
        if (nModeId == *(int*)(&g_ModeTable + iOffset)) {
            return i;
        }
        iOffset += 0x3C;
    }
    return -1;
}

// ============================================================================
// Address: 0x8002f250 | game_subsystems_update
// Chains multiple game subsystem update calls in sequence.
// Called each frame during gameplay.
// ============================================================================
void game_subsystems_update(void) {
    FUN_80093330();   // Physics/collision update
    FUN_801a8290();   // AI update
    FUN_8022b2a4();   // Entity/gameplay update
    FUN_80103f6c();   // Effects/particles update
    FUN_80311f3c();   // Rendering prep update
}

// ============================================================================
// Address: 0x8002f280 | gamemode_supports_bots
// Returns TRUE if the given mode/sub-mode combination supports AI bots.
// Bots supported for modes 0xB/0xC with sub-modes 0xC/0xD or 0x14.
// ============================================================================
BOOL gamemode_supports_bots(int nMode, int nSubMode) {
    if ((nMode == 0xB || nMode == 0xC) &&
        (nSubMode == 0x14 || (nSubMode < 0x14 && nSubMode < 0xE && nSubMode > 0xB))) {
        return TRUE;
    }
    return FALSE;
}

// ============================================================================
// Address: 0x8002f2c0 | gamemode_get_score_type
// Returns the scoring type for a game sub-mode:
//   1 = Kill-based (deathmatch, team, etc.)
//   2 = Time/objective-based (assault, bag tag)
//   3 = Virus (special lives system)
// ============================================================================
int gamemode_get_score_type(int nSubMode) {
    switch (nSubMode) {
    case 3: case 4: case 5: case 6: case 8:
    case 10: case 15: case 16: case 17: case 18: case 19:
        return 1;  // Kill-based
    case 7: case 9:
        return 2;  // Time/objective
    case 12:
        return 3;  // Virus
    default:
        return 1;
    }
}

// ============================================================================
// Address: 0x8002f304 | gamemode_get_name_string
// Gets the localized name string for a game sub-mode.
// Looks up from string table at base ID 0xAD0 + sub-mode.
// ============================================================================
void gamemode_get_name_string(char* pOutBuf, int nSubMode) {
    const char* pStr = get_string_by_id(nSubMode + 0xAD0);
    strcpy(pOutBuf, pStr);
}

// ============================================================================
// Address: 0x8002f340 | time_format_short
// Formats a frame count as "M:SS.d" time string into output buffer.
// Minutes capped at 99. Zero time shows localized "---" string.
// ============================================================================
void time_format_short(char* pOutBuf, int nFrames) {
    int nTotalSeconds = nFrames / g_FrameRate;
    int nMinutes = nTotalSeconds / 60;
    int nTenths = ((nFrames - nTotalSeconds * g_FrameRate) * 100) / (g_FrameRate * 10);

    if (nMinutes > 99) nMinutes = 99;

    if (nFrames == 0) {
        const char* pDash = get_string_by_id(0xE38);  // "---"
        sprintf(pOutBuf, "%s", pDash);
    } else {
        sprintf(pOutBuf, "%d:%02d.%d", nMinutes, nTotalSeconds % 60, nTenths);
    }
}

// ============================================================================
// Address: 0x8002f3e4 | time_format_long
// Formats a frame count as a human-readable time string with localized
// unit labels (hours "h", minutes "m", seconds "s").
// Two formats: compact "H:MM:SS.d" or verbose "Xh Ym Z.ds"
// ============================================================================
void time_format_long(char* pOutBuf, int nFrames) {
    int nTotalSeconds = nFrames / g_FrameRate;
    int nMinutes = nTotalSeconds / 60;
    int nHours = nMinutes / 60;
    int nSecs = nTotalSeconds % 60;
    int nTenths = ((nFrames - nTotalSeconds * g_FrameRate) * 100) / (g_FrameRate * 10);

    if (nHours > 99) nHours = 99;

    *pOutBuf = '\0';

    if (nFrames == 0) {
        const char* pDash = get_string_by_id(0xE38);
        sprintf(pOutBuf, "%s", pDash);
        return;
    }

    if (g_TimeFormatMode <= 3) {
        // Verbose format with unit labels
        if (nHours > 0) {
            char* pFmt = FUN_8012e878("%d", nHours);
            strcat(pOutBuf, pFmt);
        }
        if (nHours != 0 || nMinutes > 0) {
            const char* pMinLabel = get_string_by_id(0x1695);  // "m"
            char* pFmt = FUN_8012e878("%d %s", nMinutes % 60, pMinLabel);
            strcat(pOutBuf, pFmt);
        }
        if (nSecs != 0 || nTenths != 0) {
            const char* pSecLabel = get_string_by_id(0x1696);  // "s"
            char* pFmt = FUN_8012e878("%d.%d %s", nSecs, nTenths, pSecLabel);
            strcat(pOutBuf, pFmt);
        }
    } else if (nHours < 1) {
        // Compact without hours
        sprintf(pOutBuf, "%d:%02d.%d", nMinutes % 60, nSecs, nTenths);
    } else {
        // Compact with hours
        sprintf(pOutBuf, "%d:%02d:%02d.%d", nHours, nMinutes % 60, nSecs, nTenths);
    }
    FUN_8038272c(); // epilogue
}

// ============================================================================
// Address: 0x8002f5dc | game_is_handicap_enabled
// Returns TRUE if handicap mode is enabled for the current match.
// Only applies in non-story mode (g_GameMode != '\n').
// ============================================================================
BOOL game_is_handicap_enabled(void) {
    if (g_GameMode == '\n') return FALSE;
    return (g_GameStateFlags & 0x8000) != 0;
}

// ============================================================================
// Address: 0x8002f604 | game_stub_always_false
// Stub that always returns 0. Likely a removed network/online feature check.
// ============================================================================
BOOL game_stub_always_false(void) {
    return FALSE;
}
