// Address: 0x8002f60c-0x80030034 | Name: match_state | Date: 2026-08-05
// Match state management and statistics tracking.
// Handles game phase transitions, per-frame stats updates, challenge
// completion tracking, and match end/reset sequences.
//
// Game phases (g_GamePhase / DAT_80611934):
//   6 = Active gameplay (pre-countdown)
//   7 = Active gameplay (main)
//   8 = Post-round (results pending)
//   9 = End-of-match (scoreboard)
//  10 = Transitioning out
//  11 = Cleanup

#include "common.h"
#include "match_state.h"
#include "scoring.h"
#include "sound.h"

// ============================================================================
// Address: 0x8002f60c | game_set_phase
// Sets the current game phase. Resets transition timer on change.
// ============================================================================
void game_set_phase(int nPhase) {
    if (nPhase == g_GamePhase) return;
    g_GamePhase = nPhase;
    g_PhaseTransitionTimer = PHASE_TIMER_INIT;
}

// ============================================================================
// Address: 0x8002f628 | stats_is_tracking_active
// Returns TRUE if statistics should be recorded this frame.
// Disabled in story mode and mapmaker.
// ============================================================================
BOOL stats_is_tracking_active(void) {
    if (g_GameMode == '\n') return FALSE;
    if ((g_GameStateFlags & 0x40) != 0) return FALSE;
    return TRUE;
}

// ============================================================================
// Address: 0x8002f654 | game_get_difficulty_speed
// Returns the game speed multiplier based on current mode/difficulty.
// Mapmaker, story (with speed flag), story (normal), and arcade
// each have different speed constants.
// ============================================================================
float game_get_difficulty_speed(void) {
    if ((g_GameStateFlags & 0x40) != 0) return SPEED_MAPMAKER;
    if (g_GameMode == '\n') {
        if ((g_GameStateFlags2 & 0x10000) != 0) return SPEED_STORY_FAST;
        return SPEED_STORY_NORMAL;
    }
    return SPEED_ARCADE;
}

// ============================================================================
// Address: 0x8002f6a0 | stats_level_weapon_has_time
// Checks if a level/weapon combination supports time-based stat tracking.
// Returns TRUE for specific levels (0x3A, 0x19, 0x3B) or weapon IDs
// in certain ranges (0x71, 0x76-0x9A, 0x9C, 0xA0, 0xA3).
// ============================================================================
BOOL stats_level_weapon_has_time(int nLevel, int nWeaponId) {
    if (nLevel == 0x3A || nLevel == 0x19 || nLevel == 0x3B) return TRUE;
    if (nWeaponId == 0x71) return TRUE;
    if (nWeaponId >= 0x76 && nWeaponId <= 0x9A) return TRUE;
    if (nWeaponId == 0x9C) return TRUE;
    if (nWeaponId == 0xA0) return TRUE;
    if (nWeaponId == 0xA3) return TRUE;
    return FALSE;
}

// ============================================================================
// Address: 0x8002f714 | stats_set_challenge_complete
// Sets a challenge completion bit in the player's per-difficulty bitfield.
// nChallenge must be 0-127. Sets bit nChallenge in the appropriate DWORD
// of the completion bitfield at offset 0xC0C in player stats.
// ============================================================================
void stats_set_challenge_complete(int nPlayer, int nChallengePack, int nDifficulty) {
    int nChallenge;  // from register context

    if (nChallenge < 0 || nChallenge >= 128) return;

    int nDword = (nChallenge >> 5) * 4;  // Which 32-bit word
    int nBit = nChallenge & 31;          // Which bit within word

    for (int i = 0; i < nDifficulty; i++) {
        void* pStats = FUN_80034558(nPlayer);
        int nOffset = i * 0x10 + 0xC0C;
        *(u32*)((int)pStats + nDword + nOffset) |= (1 << nBit);
    }
    FUN_80382730(); // epilogue
}

// ============================================================================
// Address: 0x8002f7b4 | stats_update_per_frame
// Main per-frame statistics and challenge tracking update.
// Handles: arcade award time tracking, weapon usage stats,
// challenge completion detection, mapmaker stats, and per-player
// time-alive/idle counters.
// Only active during gameplay phases when stats tracking is enabled.
// ============================================================================
void stats_update_per_frame(void) {
    // [Very large function - summarized]
    // 1. Calls FUN_80235298 (pre-stats setup)
    // 2. Skips if cutscene flag or specific mode conditions
    // 3. For standard arcade: tracks per-award time records
    //    - Checks each player against award criteria
    //    - Records best time for each award per difficulty
    //    - Updates completion bitfields
    // 4. For mapmaker/custom: tracks challenge objectives
    //    - Checks challenge state via FUN_8022ac04
    //    - Records completion times, frame-rate normalized
    // 5. For all players: tracks weapon usage time
    //    - If weapon qualifies for time stats (stats_level_weapon_has_time)
    //    - Accumulates usage time per weapon
    // 6. Tracks time-alive and idle-time per player
    // 7. Calls FUN_800342e8 (post-stats finalization)
    FUN_80382728(); // epilogue
}

// ============================================================================
// Address: 0x8002ff48 | match_play_complete_sound
// Plays the match-complete notification sound (ID 0xD6A).
// ============================================================================
void match_play_complete_sound(void) {
    int params[14];
    sound_init_play_params((int)params);
    params[0] = 0xD6A;  // Sound ID: match complete jingle
    sound_play_request(params);
}

// ============================================================================
// Address: 0x8002ff7c | match_transition_to_end
// Transitions game to end-of-match state (phase 9).
// Only transitions if not already in phases 9, 10, or 11.
// ============================================================================
void match_transition_to_end(void) {
    if (g_GamePhase != 9 && g_GamePhase != 10 && g_GamePhase != 11) {
        game_set_phase(9);
    }
}

// ============================================================================
// Address: 0x8002ffbc | match_end_trigger
// Triggers the full match end sequence:
// 1. Sets end-of-match flag
// 2. Toggles result display state
// 3. Transitions to end phase
// 4. Records end reason
// 5. If clean end (param==0): saves results and triggers UI
// ============================================================================
void match_end_trigger(int nReason) {
    g_MatchEnded = 1;
    g_ResultState = 2 - (g_ResultState == 0);  // Toggle 0->2, else->1
    match_transition_to_end();
    g_EndReason = nReason;

    if (nReason == 0) {
        if ((g_GameStateFlags & 0x20000000) == 0 &&
            (g_GameStateFlags & 0x100000) == 0) {
            FUN_8025b928();   // Save match results
            FUN_80222230();   // Show results UI
        }
    }
}

// ============================================================================
// Address: 0x80030034 | match_full_reset
// Resets all match state for a new game. Clears stats tracking,
// end-of-match flags, and reinitializes subsystems.
// ============================================================================
void match_full_reset(void) {
    g_StatsFlags = 0;
    g_MatchEnded = 0;
    FUN_80026a88();           // Reset game subsystems
    match_transition_to_end(); // Reset phase
}
