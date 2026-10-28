// Address: 0x8002e410-0x8002e918 | Name: scoring | Date: 2026-08-05
// Multiplayer scoring/kill tracking system.
// Manages per-player kill matrices, team scores, and player rankings.
//
// Player data layout (within 0xEA0-byte player state block):
//   +0x008: int nPlayerIndex
//   +0x021: int nTeamId (1-4)
//   +0x024: u32 flags
//   +0x048: s16 nCharacterId
//   +0x084: int nSomething (used for team comparison)
//   +0xA10: s16[N] killMatrix (kills against each opponent)
//   +0xA52: s16 nDeathCount
//   +0xA60: s16 nSpecialScore (for certain modes)
//   +0xA64: int nCachedScore
//   +0xA68: int nSortScore (used for ranking)
//
// Team scores stored at g_TeamScores[0..3] (teams indexed 1-4, stored 0-3).

#include "common.h"
#include "scoring.h"

#define MAX_TEAMS 4

// ============================================================================
// Address: 0x8002e410 | score_reset_display
// Resets the score display/notification flag.
// ============================================================================
void score_reset_display(void) {
    g_ScoreDisplayFlag = 0;
}

// ============================================================================
// Address: 0x8002e41c | score_calc_player_total
// Calculates a player's total score from the kill matrix.
// Sums kills against all opponents on different teams.
// Subtracts deaths or self-kills depending on game mode flags.
// ============================================================================
int score_calc_player_total(int* pPlayerState) {
    int nScore = 0;
    int nTotalPlayers = g_NumPlayers + g_NumBots;

    // Sum kills against opponents (different team only)
    for (int i = 0; i < nTotalPlayers; i++) {
        int* pOther = (int*)(g_PlayerDataB + i * 0xEA0);
        if (*(int*)((int)pPlayerState + 0x84) != *(int*)((int)pOther + 0x84)) {
            nScore += *(s16*)((int)pPlayerState + i * 2 + 0xA10);
        }
    }

    // Subtract penalty based on game mode
    if (g_ScoreMode != 0) {
        // Special score mode: use dedicated field
        nScore = (int)*(s16*)((int)pPlayerState + 0xA60);
    } else if ((g_GameStateFlags & 0x20) != 0) {
        // Self-kill penalty: subtract kills against self
        nScore -= *(s16*)((int)pPlayerState +
                  *(int*)((int)pPlayerState + 8) * 2 + 0xA10);
    } else if ((g_GameStateFlags & 0x10) != 0) {
        // Death penalty: subtract death count
        nScore -= *(s16*)((int)pPlayerState + 0xA52);
    }

    return nScore;
}

// ============================================================================
// Address: 0x8002e4c4 | score_get_team
// Gets the score for a team (1-indexed). Returns 0 for invalid team.
// ============================================================================
int score_get_team(int nTeam) {
    int idx = nTeam - 1;
    if (idx < 0 || idx >= MAX_TEAMS) return 0;
    return g_TeamScores[idx];
}

// ============================================================================
// Address: 0x8002e4f0 | score_set_team
// Sets the score for a team (1-indexed). No-op for invalid team.
// ============================================================================
void score_set_team(int nTeam, int nScore) {
    int idx = nTeam - 1;
    if (idx < 0 || idx >= MAX_TEAMS) return;
    g_TeamScores[idx] = nScore;
}

// ============================================================================
// Address: 0x8002e518 | score_add_team
// Adds to a team's score. Only active when team mode flag is set.
// ============================================================================
void score_add_team(int nTeam, int nDelta) {
    if ((g_GameStateFlags & 1) == 0) return;  // Not team mode
    int idx = nTeam - 1;
    if (idx < 0 || idx >= MAX_TEAMS) return;
    g_TeamScores[idx] += nDelta;
}

// ============================================================================
// Address: 0x8002e55c | score_get_player
// Gets a player's current score, calculated differently per game mode.
// ============================================================================
int score_get_player(int nPlayerIdx) {
    switch (g_GameMode) {
    case 3:  // Deathmatch: calculate from kill matrix
        return score_calc_player_total(
            (int*)(g_PlayerDataB + nPlayerIdx * 0xEA0));
    case 9:  // Assault: depends on sub-mode
        if (g_GameSubMode == '\n') {
            // Fall through to cached score
        } else {
            return score_calc_player_total(
                (int*)(g_PlayerDataB + nPlayerIdx * 0xEA0));
        }
        // fallthrough
    case 1: case 2: case 5: case 6: case 10:
        // Other modes: use cached score
        return *(int*)(g_PlayerDataB + nPlayerIdx * 0xEA0 + 0xA64);
    default:
        return 0;
    }
}

// ============================================================================
// Address: 0x8002e5f0 | score_find_lowest_player
// Finds the player with the lowest score. Excludes:
//   - The querying player (self)
//   - Dead/spectating players (flag 0x08)
//   - Certain mode-specific exclusions
// Used for AI targeting (attack the weakest) or balancing.
// ============================================================================
void score_find_lowest_player(int pSelf) {
    int nLowest = 0x7FFFFFFF;
    int nTotalPlayers = g_NumPlayers + g_NumBots;

    for (int i = 0; i < nTotalPlayers; i++) {
        int pPlayer = g_PlayerDataB + i * 0xEA0;
        if (pSelf != 0 && pSelf == pPlayer) continue;  // Skip self
        if ((*(u32*)(pPlayer + 0x24) & 8) != 0) continue;  // Skip dead

        // Mode-specific exclusion
        if (g_GameSubMode == '\x13') {
            if (*(int*)(pPlayer + 8) >= g_NumPlayers) {
                if (FUN_800ac9c4(*(s16*)(pPlayer + 0x48)) != 0) continue;
            }
        }

        int nScore = *(int*)(pPlayer + 0xA68);
        if (nScore <= nLowest) {
            FUN_8013104c();  // Record this player as current lowest
            nLowest = nScore;
        }
    }
    FUN_80382728(); // epilogue
}

// ============================================================================
// Address: 0x8002e740 | score_find_highest_player
// Finds the player with the highest score. Same exclusions as lowest.
// Used for "target the leader" modes or end-of-round winner determination.
// ============================================================================
void score_find_highest_player(int pSelf) {
    int nHighest = -0x7FFFFFFF;
    int nTotalPlayers = g_NumPlayers + g_NumBots;

    for (int i = 0; i < nTotalPlayers; i++) {
        int pPlayer = g_PlayerDataB + i * 0xEA0;
        if (pSelf != 0 && pSelf == pPlayer) continue;
        if ((*(u32*)(pPlayer + 0x24) & 8) != 0) continue;

        if (g_GameSubMode == '\x13') {
            if (*(int*)(pPlayer + 8) >= g_NumPlayers) {
                if (FUN_800ac9c4(*(s16*)(pPlayer + 0x48)) != 0) continue;
            }
        }

        int nScore = *(int*)(pPlayer + 0xA68);
        if (nScore >= nHighest) {
            FUN_8013104c();
            nHighest = nScore;
        }
    }
    FUN_80382728(); // epilogue
}

// ============================================================================
// Address: 0x8002e890 | score_award_kill
// Awards a kill to a player. Increments their kill counter, triggers
// the kill notification UI effect, and updates team score.
// Only active in game phases 6 and 7 (active gameplay).
// ============================================================================
void score_award_kill(int* pKiller, int nPoints) {
    if (g_GamePhase != 7 && g_GamePhase != 6) return;

    pKiller[0x299] += nPoints;  // Total kills stat

    // Trigger kill notification visual effect
    if (*pKiller != 0) {
        int* pEntity = *(int**)(*pKiller + 0x150);
        if (pEntity != NULL) {
            FUN_80237b50(1.2f, 0x50, *pEntity);
        }
    }

    // Update team score
    score_add_team(pKiller[0x21], nPoints);
}

// ============================================================================
// Address: 0x8002e918 | score_register_death
// Registers a death/kill event between two players.
// Updates kill matrix, death counter, team scores, and handles
// various game mode scoring rules (team deathmatch, etc.).
// ============================================================================
void score_register_death(int* pVictim, int* pKiller, int nKillIdx, int nPoints) {
    // Various mode-specific validity checks
    if (pVictim != NULL && g_GameSubMode == '\x12' &&
        *pVictim == g_SomeEntity) {
        FUN_80382734();
        return;
    }

    // Mode-specific bot exclusion
    if (g_GameSubMode == '\x13') {
        int pKillerState = g_PlayerDataB + nKillIdx * 0xEA0;
        if (*(int*)(pKillerState + 8) >= g_NumPlayers) {
            if (FUN_800ac9c4(*(s16*)(pKillerState + 0x48)) != 0) {
                FUN_80382734();
                return;
            }
        }
    }

    // Same check for victim
    if (pVictim != NULL && g_GameSubMode == '\x13') {
        if (pVictim[2] >= g_NumPlayers) {
            if (FUN_800ac9c4(*(s16*)(pVictim + 0x12)) != 0) {
                // Award to special entity instead
                *(s16*)(*(int*)(g_SpecialEntity + 0x7C) + nKillIdx * 2 + 0xA10) += (s16)nPoints;
                FUN_80382734();
                return;
            }
        }
    }

    if (g_GamePhase != 7 && g_GamePhase != 6) {
        FUN_80382734();
        return;
    }

    // Core scoring update
    int* pKillerState = (int*)(g_PlayerDataB + nKillIdx * 0xEA0);
    *(s16*)((int)pKillerState + 0xA52) += 1;  // Increment death counter

    // Decrement lives in lives-mode
    if (g_ScoreMode != 0 && *(s16*)(pKillerState + 0x298) > 0) {
        *(s16*)(pKillerState + 0x298) -= 1;
    }

    // Update kill matrix (killer's row, victim's column)
    if (pVictim != NULL) {
        *(s16*)((int)pVictim + nKillIdx * 2 + 0xA10) += (s16)nPoints;
    }

    // Team score adjustments for specific modes
    if (g_GameMode == '\t' && g_ScoreMode == 0) {
        if (g_GameSubMode != '\n') {
            // Penalty scoring for team modes
            if ((g_GameStateFlags & 0x10) != 0) {
                score_add_team(pKillerState[0x21], -nPoints);
            }
            if (pVictim == pKillerState) {
                // Self-kill penalty
                if ((g_GameStateFlags & 0x20) != 0) {
                    score_add_team(pVictim[0x21], -nPoints);
                }
            } else if (pVictim != NULL && pVictim[0x21] != pKillerState[0x21]) {
                // Kill on different team: award points
                score_add_team(pVictim[0x21], nPoints);
            }
        }
    }
    FUN_80382734(); // epilogue
}

// ============================================================================
// Address: 0x8002eb04 | score_get_times_killed_by
// Gets how many times all other players killed this player.
// In mapmaker mode, returns death count directly.
// Otherwise sums killMatrix column for this player's index.
// ============================================================================
int score_get_times_killed_by(int* pPlayer) {
    if ((g_GameStateFlags & 0x40) != 0) {
        return (int)*(s16*)((int)pPlayer + 0xA52);  // Mapmaker: use death count
    }

    int nTotal = 0;
    int nPlayerIdx = *(int*)((int)pPlayer + 8);
    int nPlayers = g_NumPlayers + g_NumBots;
    int iOffset = 0;
    for (int i = 0; i < nPlayers; i++) {
        // Sum column: each other player's kills against us
        nTotal += *(s16*)(g_PlayerDataB + iOffset + nPlayerIdx * 2 + 0xA10);
        iOffset += 0xEA0;
    }
    return nTotal;
}

// ============================================================================
// Address: 0x8002eb68 | score_get_special
// Gets a player's special/alternate score value (lives, captures, etc.).
// ============================================================================
int score_get_special(int pPlayer) {
    return (int)*(s16*)(pPlayer + 0xA60);
}

// ============================================================================
// Address: 0x8002eb70 | score_rank_by_teams
// Team-based ranking: sorts players by score within their team,
// alternating between teams for fair ordering in the scoreboard.
// Used for team assault mode (sub-mode 0x10, phase 8).
// ============================================================================
void score_rank_by_teams(void) {
    int nPlayers = g_NumPlayers + g_NumBots;
    int nTeamOrder[2];
    u32 uProcessed = 0;
    int iOutIdx = 0;

    // Determine team ordering (winning team first)
    if (g_TeamScoreCompare < 1) {
        nTeamOrder[0] = 2; nTeamOrder[1] = 1;
    } else {
        nTeamOrder[0] = 1; nTeamOrder[1] = 2;
    }

    // For each team, sort players by score descending
    for (int t = 0; t < 2; t++) {
        for (int pass = 0; pass < nPlayers; pass++) {
            int nBestIdx = -1;
            int nBestScore = -1;
            for (int i = 0; i < nPlayers; i++) {
                int pPlayer = g_PlayerDataB + i * 0xEA0;
                if (pPlayer == 0) continue;
                if (*(int*)(pPlayer + 0x84) != nTeamOrder[t]) continue;
                if ((uProcessed & (1 << i)) != 0) continue;
                int nScore = *(int*)(pPlayer + 0xA6C);
                if (nScore > nBestScore) {
                    nBestIdx = i;
                    nBestScore = nScore;
                }
            }
            if (nBestIdx >= 0) {
                uProcessed |= (1 << nBestIdx);
                g_RankOrder[iOutIdx] = *(int*)(g_PlayerDataB + nBestIdx * 0xEA0 + 8);
                iOutIdx++;
            }
        }
    }
    FUN_80382734(); // epilogue
}

// ============================================================================
// Address: 0x8002ecb8 | score_calculate_rankings
// Main ranking calculator. Computes composite sort scores per player
// and assigns rank positions (1st, 2nd, ...). Handles:
//   - Team vs individual scoring
//   - Special mode bonuses (lives, time penalties)
//   - Mode-specific rank adjustments (assault, virus, gladiator)
//   - Tied rankings (same rank for equal scores)
// Called at round end or when rankings need updating.
// param: 0 = compute scores, non-zero = restore saved scores
// ============================================================================
void score_calculate_rankings(int bRestore) {
    int nTotalPlayers = g_NumPlayers + g_NumBots;

    if (g_ProfileReady != 0) {
        FUN_80382738();
        return;  // Already computed
    }

    if (bRestore == 0) {
        // Phase 1: Compute composite sort scores for each player
        for (int i = 0; i < nTotalPlayers; i++) {
            int pPlayer = g_PlayerDataB + i * 0xEA0;

            // Base score: individual or team
            if ((g_GameStateFlags & 1) == 0 || g_GameSubMode == 0x10) {
                *(int*)(pPlayer + 0xA6C) = score_get_player(*(int*)(pPlayer + 8));
            } else {
                *(int*)(pPlayer + 0xA6C) = score_get_team(*(int*)(pPlayer + 0x84));
            }

            // Add special score for lives/capture modes
            if (g_ScoreMode != 0) {
                if (g_GameSubMode == 7 || g_GameSubMode == 9) {
                    *(int*)(pPlayer + 0xA6C) += *(int*)(pPlayer + 0xA64);
                } else {
                    *(int*)(pPlayer + 0xA6C) += (int)*(s16*)(pPlayer + 0xA60);
                }
                *(int*)(pPlayer + 0xA70) = (int)*(s16*)(pPlayer + 0xA60);
            }

            // Time penalty for certain modes (sub-modes 7, 8, 9)
            if (g_GameSubMode == 7 || g_GameSubMode == 9) {
                *(int*)(pPlayer + 0xA6C) -= g_RoundTime * 3;
            }

            // Virus mode: infected players get penalty
            if (g_GameSubMode == 0xC) {
                if ((g_VirusMask & (1 << *(int*)(pPlayer + 8))) == 0) {
                    *(int*)(pPlayer + 0xA6C) -= g_RoundTime * 3;
                    *(int*)(pPlayer + 0xA70) = 0;
                } else {
                    *(int*)(pPlayer + 0xA6C) -= 1000;
                    *(int*)(pPlayer + 0xA70) = 1;
                }
            }

            // Gladiator exclusion penalty
            if (g_GameSubMode == 0x13 && *(int*)(pPlayer + 8) >= g_NumPlayers) {
                if (FUN_800ac9c4(*(s16*)(pPlayer + 0x48)) != 0) {
                    *(int*)(pPlayer + 0xA6C) -= 1000;
                }
            }
        }
    } else {
        // Restore saved special scores
        for (int i = 0; i < nTotalPlayers; i++) {
            int pPlayer = g_PlayerDataB + i * 0xEA0;
            if (g_ScoreMode != 0) {
                *(s16*)(pPlayer + 0xA60) = (s16)*(int*)(pPlayer + 0xA70);
            }
            if (g_GameSubMode == 0xC) {
                if (*(int*)(pPlayer + 0xA70) == 1) {
                    g_VirusMask |= (1 << *(int*)(pPlayer + 8));
                } else {
                    g_VirusMask &= ~(1 << *(int*)(pPlayer + 8));
                }
            }
        }
    }

    // Phase 2: Assign rank positions (descending sort)
    // Clear rank slots
    for (int i = 0; i < nTotalPlayers; i++) {
        *(int*)(g_PlayerDataB + i * 0xEA0 + 0xA68) = 0x80000001;  // Unranked
        g_RankOrder[i] = -1;
    }

    if (g_GameSubMode == 0x10 && g_GamePhase == 8) {
        score_rank_by_teams();  // Team assault: special ordering
    } else {
        // Standard ranking: find highest unranked, assign rank, repeat
        int nRank = 1;
        while (1) {
            int nHighest = -0x7FFFFFFF;
            for (int i = 0; i < nTotalPlayers; i++) {
                if (*(int*)(g_PlayerDataB + i * 0xEA0 + 0xA68) == -0x7FFFFFFF) {
                    int nScore = *(int*)(g_PlayerDataB + i * 0xEA0 + 0xA6C);
                    if (nScore > nHighest) nHighest = nScore;
                }
            }
            if (nHighest == -0x7FFFFFFF) break;

            int nTied = 0;
            for (int i = 0; i < nTotalPlayers; i++) {
                if (*(int*)(g_PlayerDataB + i * 0xEA0 + 0xA6C) == nHighest) {
                    *(int*)(g_PlayerDataB + i * 0xEA0 + 0xA68) = nRank;
                    nTied++;
                    g_RankOrder[nRank - 1] = *(int*)(g_PlayerDataB + i * 0xEA0 + 8);
                }
            }

            // In team mode: tied players share rank, next rank is +1
            // In FFA: next rank skips tied count
            nRank += ((g_GameStateFlags & 1) != 0) ? 1 : nTied;
        }
    }
    FUN_80382738(); // epilogue
}

// ============================================================================
// Address: 0x8002f07c | game_get_round_timer
// Returns the current round/game timer value.
// ============================================================================
int game_get_round_timer(void) {
    return g_RoundTimer;
}

// ============================================================================
// Address: 0x8002f084 | game_level_supports_scoring
// Returns TRUE if the current level supports score tracking.
// Excludes: cutscene flag, arcade hub (100), and levels 100-107.
// ============================================================================
BOOL game_level_supports_scoring(void) {
    if ((g_GameStateFlags & 0x20000000) != 0) return FALSE;
    if (g_CurrentLevel != 100 && g_CurrentLevel > 99 && g_CurrentLevel < 108) return FALSE;
    return TRUE;
}

// ============================================================================
// Address: 0x8002f0c8 | game_is_standard_arcade
// Returns TRUE if game is a standard arcade match:
// story mode + valid level (not 100, not 0x38) + not mapmaker.
// ============================================================================
BOOL game_is_standard_arcade(void) {
    if (g_GameMode != '\n') return FALSE;
    if (g_CurrentLevel == 100) return FALSE;
    if (g_CurrentLevel == 0x38) return FALSE;
    if ((g_GameStateFlags & 0x40) != 0) return FALSE;
    return TRUE;
}

// ============================================================================
// Address: 0x8002f108 | game_is_mapmaker_multiplayer
// Returns TRUE if currently in mapmaker multiplayer mode.
// ============================================================================
BOOL game_is_mapmaker_multiplayer(void) {
    if ((g_GameStateFlags & 0x800000) == 0) return FALSE;
    if ((g_GameStateFlags & 0x40) == 0) return FALSE;
    return TRUE;
}

// ============================================================================
// Address: 0x8002f130 | award_find_by_id
// Searches the award table (13 entries, stride 0x44) for a matching ID.
// Returns index (0-12), or -1 if not found.
// ============================================================================
int award_find_by_id(int nId) {
    for (int i = 0; i < 13; i++) {
        if (nId == *(s16*)(&g_AwardTable[i * 0x44] + 6)) {
            return i;
        }
    }
    return -1;
}

// ============================================================================
// Address: 0x8002f174 | award_get_entry
// Returns pointer to award data entry by ID. Returns NULL if not found.
// Entry is 0x44 bytes (0x22 * 2 = 68 bytes for display purposes).
// ============================================================================
void* award_get_entry(int nId) {
    int idx = award_find_by_id(nId);
    if (idx < 0) return NULL;
    return &g_AwardTable[idx * 0x44];
}

// ============================================================================
// Address: 0x800333d8 | input_any_player_pressing_skip
// Checks if any connected player is pressing a skip button (Start, A, or B).
// Used to skip cutscenes, intros, and result screens.
// Returns TRUE if any player has a skip button held.
// ============================================================================
BOOL input_any_player_pressing_skip(void) {
    for (int i = 0; i < g_NumConnectedPads; i++) {
        u32 buttons = input_get_buttons_held(i);
        if (buttons & 0x100) return TRUE;     // Start
        buttons = input_get_buttons_held(i);
        if (buttons & 0x80) return TRUE;      // A
        if ((g_GameStateFlags & 0x200) != 0) {
            buttons = input_get_buttons_held(i);
            if (buttons & 0x8000) return TRUE; // B (when enabled)
        }
    }
    return FALSE;
}

// ============================================================================
// Address: 0x8003346c | stats_count_weapon_uses
// Counts how many times a specific weapon was used by a player.
// Scans through all 48 stat entries for matches.
// ============================================================================
int stats_count_weapon_uses(int nPlayer, int nWeaponId) {
    int nCount = 0;
    void* pStats = FUN_80034558(nPlayer);
    for (int i = 0; i < 48; i++) {
        if (*(int*)((int)pStats + i * 0x18 + 0x49C) == nWeaponId) {
            nCount++;
        }
    }
    return nCount;
}

// ============================================================================
// Address: 0x800334d0 | stats_count_unlocked_chars
// Counts unlocked characters for a player (entries 21-47 in stat table).
// ============================================================================
int stats_count_unlocked_chars(int nPlayer) {
    int nCount = 0;
    void* pStats = FUN_80034558(nPlayer);
    for (int i = 0; i < 27; i++) {
        if (*(int*)((int)pStats + (i + 21) * 0x18 + 0x49C) != 0) {
            nCount++;
        }
    }
    return nCount;
}

// ============================================================================
// Address: 0x80033528 | stats_count_unlocked_weapons
// Counts unlocked weapons for a player (first 21 entries in stat table).
// ============================================================================
int stats_count_unlocked_weapons(int nPlayer) {
    int nCount = 0;
    void* pStats = FUN_80034558(nPlayer);
    for (int i = 0; i < 21; i++) {
        if (*(int*)((int)pStats + i * 0x18 + 0x49C) != 0) {
            nCount++;
        }
    }
    return nCount;
}

// ============================================================================
// Constants
// ============================================================================
// Address: 0x80033580 | stats_get_total_chars — returns 27
int stats_get_total_chars(void) { return 27; }

// Address: 0x80033588 | stats_get_total_entries — returns 48
int stats_get_total_entries(void) { return 48; }

// Address: 0x80033590 | stats_get_total_weapons — returns 21
int stats_get_total_weapons(void) { return 21; }

// Address: 0x80033598 | stats_get_total_awards — returns 13
int stats_get_total_awards(void) { return 13; }

// ============================================================================
// Address: 0x800335a0 | stats_compute_all_awards
// Iterates all 13 awards across all players to compute completion stats.
// ============================================================================
void stats_compute_all_awards(int nPlayer) {
    for (int iAward = 0; iAward < 13; iAward++) {
        for (int iDiff = 0; iDiff < 3; iDiff++) {
            FUN_80034558(nPlayer);
        }
    }
    FUN_80382724(); // epilogue
}

// ============================================================================
// Address: 0x8003363c | stats_compute_wrapper
// Wrapper that calls stats_compute_all_awards.
// ============================================================================
void stats_compute_wrapper(void) {
    stats_compute_all_awards();
}

// ============================================================================
// Address: 0x80033a88 | entity_get_type_id
// Returns entity type ID from the entity type table at given index.
// ============================================================================
int entity_get_type_id(int nIndex) {
    return g_EntityTypeTable[nIndex];
}

// ============================================================================
// Address: 0x80033a9c | color_fade_team_a
// Applies alpha fade to a team color from palette table A.
// Multiplies the alpha (low byte) by fAlpha, preserves RGB.
// ============================================================================
u32 color_fade_team_a(float fAlpha, int nIndex) {
    u32 uColor = g_TeamPaletteA[nIndex];
    u8 nAlpha = (u8)((float)(uColor & 0xFF) * fAlpha);
    return (uColor & 0xFFFFFF00) | nAlpha;
}

// ============================================================================
// Address: 0x80033afc | color_fade_team_b
// Same as color_fade_team_a but from palette table B.
// ============================================================================
u32 color_fade_team_b(float fAlpha, int nIndex) {
    u32 uColor = g_TeamPaletteB[nIndex];
    u8 nAlpha = (u8)((float)(uColor & 0xFF) * fAlpha);
    return (uColor & 0xFFFFFF00) | nAlpha;
}

// ============================================================================
// Address: 0x80033b5c | color_fade_team_c
// Same as color_fade_team_a but from palette table C.
// ============================================================================
u32 color_fade_team_c(float fAlpha, int nIndex) {
    u32 uColor = g_TeamPaletteC[nIndex];
    u8 nAlpha = (u8)((float)(uColor & 0xFF) * fAlpha);
    return (uColor & 0xFFFFFF00) | nAlpha;
}

// ============================================================================
// Address: 0x80033bbc | level_get_current_id
// Returns the current level/scene ID.
// ============================================================================
int level_get_current_id(void) {
    return g_CurrentSceneId;
}

// ============================================================================
// Address: 0x80033bc4 | level_get_previous_id
// Returns the previous level/scene ID.
// ============================================================================
int level_get_previous_id(void) {
    return g_PreviousSceneId;
}

// ============================================================================
// Address: 0x80033bcc | level_push_id
// Pushes a new level ID. Stores current as previous, sets new.
// ============================================================================
void level_push_id(int nNewId) {
    g_PreviousSceneId = g_CurrentSceneId;
    g_CurrentSceneId = nNewId;
}

// ============================================================================
// Address: 0x80033bdc | match_set_paused
// Sets the match paused flag.
// ============================================================================
void match_set_paused(void) {
    g_MatchPaused = 1;
}

// ============================================================================
// Address: 0x80033be8 | match_clear_paused
// Clears the match paused flag.
// ============================================================================
void match_clear_paused(void) {
    g_MatchPaused = 0;
}

// ============================================================================
// Address: 0x80033bf4 | match_is_not_paused
// Returns TRUE if match is NOT paused (inverted logic).
// ============================================================================
BOOL match_is_not_paused(void) {
    return g_MatchPaused == 0;
}
