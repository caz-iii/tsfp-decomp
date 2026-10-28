// Address: 0x80108418 - 0x80108e0c | Batch 328 | Date: 2026-08-25
// HUD slot management system: slot tick/update, draw dispatchers (primary + secondary),
// layout helpers (height accumulation, overlap check, pair lookup),
// final round notification, and damage particle effect update.

#include "common.h"
#include "hud.h"
#include "text.h"
#include "sound.h"

// ============================================================================
// Address: 0x80108418 | hud_slot_are_players_paired
// Checks if two player indices appear in the same pair entry in a 14-entry
// pairing table (8 bytes per entry: two player indices). Used for split-screen
// HUD overlap avoidance.
// Returns: 1 if paired, 0 if not found.
// ============================================================================
u32 hud_slot_are_players_paired(int iPlayerA, int iPlayerB) {
    int i;
    int iOffset;

    i = 0xd; // 14 entries, iterate backwards
    while (true) {
        do {
            i--;
            if (i < 0) {
                return 0;
            }
            iOffset = i * 8;
        } while ((iPlayerA != *(int *)(iOffset + 0x8040cfe8)) &&
                 (iPlayerA != *(int *)(iOffset + 0x8040cfec)));

        if (iPlayerB == *(int *)(iOffset + 0x8040cfe8)) break;
        if (iPlayerB == *(int *)(iOffset + 0x8040cfec)) {
            return 1;
        }
    }
    return 1;
}

// ============================================================================
// Address: 0x80108474 | hud_slot_check_overlap
// Checks if the current HUD slot overlaps with other paired player slots.
// If the slot's accumulated height exceeds the viewport limit for its type,
// iterates through other slots checking pair relationships to determine if
// the current slot should be suppressed.
// ============================================================================
void hud_slot_check_overlap(void) {
    int *pSlot;
    int iVar2;
    int iType;
    int iIter;

    u64 uResult = FUN_803826ec();
    pSlot = (int *)(uResult >> 32);

    if (pSlot[0x56] + *(int *)((int)uResult + *pSlot * 4) <=
        (int)(&DAT_8057ff5c)[*pSlot]) {
        iType = 1;
        iIter = 4;
        do {
            if (((*(int *)((int)uResult + iIter) != 0) && (iType != *pSlot)) &&
                (iVar2 = hud_slot_are_players_paired(iType, *pSlot), iVar2 != 0)) break;
            iType++;
            iIter += 4;
        } while (iType < 0xe);
    }
    FUN_80382738();
}

// ============================================================================
// Address: 0x80108524 | hud_slot_accumulate_height
// Adds a slot's height (offset 0x56) to an accumulator array indexed by type.
// Skips slots with timer < 0.0 or type == 8 (single_fade, no vertical space).
// Used during layout pass to compute total vertical usage per type.
// ============================================================================
void hud_slot_accumulate_height(int *pSlot, int pAccumulator) {
    if ((float)pSlot[0x51] < 0.0f) {
        return;
    }
    if (*pSlot == 8) {
        return; // type 8 (single_fade) doesn't consume vertical space
    }
    int iType = *pSlot * 4;
    *(int *)(pAccumulator + iType) = *(int *)(pAccumulator + iType) + pSlot[0x56];
}

// ============================================================================
// Address: 0x8010855c | hud_slot_accumulate_height_all
// Unconditionally adds a slot's height to the accumulator array.
// Same as hud_slot_accumulate_height but without the type == 8 exclusion.
// ============================================================================
void hud_slot_accumulate_height_all(int *pSlot, int pAccumulator) {
    if ((float)pSlot[0x51] < 0.0f) {
        return;
    }
    *(int *)(pAccumulator + *pSlot * 4) =
        *(int *)(pAccumulator + *pSlot * 4) + pSlot[0x56];
}

// ============================================================================
// Address: 0x8010858c | hud_is_player_spectating
// Returns 0 if the player is in spectator mode (state == 1), 1 otherwise.
// Used to suppress HUD drawing for spectating players.
// ============================================================================
u32 hud_is_player_spectating(int iPlayerIndex) {
    if ((iPlayerIndex >= 0) &&
        (*(int *)(&DAT_80579d90 + iPlayerIndex * 0x177c) == 1)) {
        return 0; // spectating - suppress HUD
    }
    return 1; // not spectating - show HUD
}

// ============================================================================
// Address: 0x801085c0 | hud_slot_tick_update
// MASSIVE per-frame HUD slot update. Iterates 16 slots for the current player:
// - Manages slot timers (fade-in delay with dt accumulation)
// - Triggers deferred sound playback when timer threshold reached
// - Checks slot overlap/pairing and suppresses conflicting slots
// - Expires slots when lifetime exceeded or vertical space overflow
// ============================================================================
void hud_slot_tick_update(void) {
    float fTimer;
    float fLifetime;
    int iVar;
    int iPlayerIndex;
    int *pSlot;
    int iSlotIdx;
    double dDeltaTime;
    int aHeightAccum[14];
    u32 aVisibleAccum[14];
    int aSoundParams[12];

    FUN_803826ec();
    iPlayerIndex = *(int *)(DAT_80611d74 + 4);

    if (iPlayerIndex < 0) {
        FUN_80382738();
        return;
    }

    // Compute delta time from frame rate
    dDeltaTime = (double)(FLOAT_806108ec *
                 (FLOAT_80615718 /
                  (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                         DOUBLE_80615738)));

    if (DAT_806108e4 != 0) {
        // Clear accumulators
        for (int i = 0; i < 14; i++) {
            aHeightAccum[i] = 0;
            aVisibleAccum[i] = 0;
        }

        // First pass: update timers, play sounds, accumulate heights
        for (iSlotIdx = 0; iSlotIdx < 16; iSlotIdx++) {
            // Compute slot pointer from circular buffer index
            int iRawIdx = *(int *)(&DAT_805786b0 + iPlayerIndex * 0x177c) + iSlotIdx;
            int iWrapped = ((iRawIdx >> 31) * 0x10 |
                           (u32)(iRawIdx * 0x10000000 + (iRawIdx >> 31)) >> 0x1c) -
                           (iRawIdx >> 31);
            pSlot = &DAT_80578680 + iPlayerIndex * 0x5df + iWrapped * 0x5b + 0xe;

            if (*pSlot != 0) {
                hud_slot_accumulate_height_all(pSlot, (int)aHeightAccum);
                int iOverlap = hud_slot_check_overlap();

                if (iOverlap == 0) {
                    // No overlap: advance timer, play sound when ready
                    if ((pSlot[0x58] < 0) ||
                        (iVar = sound_is_playing(pSlot[0x58]), iVar == 0)) {
                        pSlot[0x51] = (int)(float)((double)(float)pSlot[0x51] + dDeltaTime);
                        if ((0.0f < (float)pSlot[0x51]) && (pSlot[0x57] >= 0)) {
                            sound_init_play_params((int)aSoundParams);
                            aSoundParams[0] = pSlot[0x57];
                            sound_play_request(aSoundParams);
                            pSlot[0x57] = -1; // mark sound as played
                        }
                    } else {
                        pSlot[0x51] = (int)0.0f; // hold while sound playing
                    }
                } else {
                    // Overlap: fade timer toward 0
                    if ((double)(float)pSlot[0x51] < (double)0.0f) {
                        pSlot[0x51] = (int)(float)((double)(float)pSlot[0x51] + dDeltaTime);
                        if ((double)0.0f < (double)(float)pSlot[0x51]) {
                            pSlot[0x51] = (int)0.0f;
                        }
                    }
                }
                hud_slot_accumulate_height(pSlot, (int)aVisibleAccum);
            }
        }

        // Second pass: expire slots that exceed lifetime or overflow
        for (iSlotIdx = 0; iSlotIdx < 16; iSlotIdx++) {
            int iRawIdx = *(int *)(&DAT_805786b0 + iPlayerIndex * 0x177c) + iSlotIdx;
            int iWrapped = ((iRawIdx >> 31) * 0x10 |
                           (u32)(iRawIdx * 0x10000000 + (iRawIdx >> 31)) >> 0x1c) -
                           (iRawIdx >> 31);
            pSlot = &DAT_80578680 + iPlayerIndex * 0x5df + iWrapped * 0x5b + 0xe;

            int iType = *pSlot;
            if (iType != 0) {
                fTimer = (float)pSlot[0x51];
                fLifetime = (float)pSlot[0x54];
                int iHeight = pSlot[0x56];

                aHeightAccum[iType] -= iHeight;

                // Expire if lifetime exceeded or vertical overflow
                if ((fLifetime <= fTimer) ||
                    (((float)pSlot[0x55] <= fTimer) &&
                     ((int)(&DAT_8057ff5c)[iType] < iHeight + aHeightAccum[iType]))) {
                    *pSlot = 0; // clear slot
                }
            }
        }
    }
    FUN_80382738();
}

// ============================================================================
// Address: 0x80108830 | hud_slot_draw_secondary
// Secondary/mirror viewport draw pass. Only draws slot types that need
// secondary viewport rendering: 6 (image bar), 0xb (centered dual),
// 0xc (score entry), 0xd (centered mirror).
// ============================================================================
void hud_slot_draw_secondary(void) {
    int iVar1;
    int iPlayerIndex;
    int *pSlot;
    int iSlotIdx;
    u32 aHeightAccum[18];

    FUN_803826f0();
    iPlayerIndex = *(int *)(DAT_80611d78 + 4);

    FUN_8016f00c(1);
    iVar1 = FUN_802ef550();
    if (iVar1 == 0) {
        // Clear accumulator
        for (int i = 0; i < 14; i++) {
            aHeightAccum[i] = 0;
        }

        if (iPlayerIndex >= 0) {
            for (iSlotIdx = 0; iSlotIdx < 16; iSlotIdx++) {
                // Compute wrapped slot index
                int iRawIdx = *(int *)(&DAT_805786b0 + iPlayerIndex * 0x177c) + iSlotIdx;
                int iWrapped = ((iRawIdx >> 31) * 0x10 |
                               (u32)(iRawIdx * 0x10000000 + (iRawIdx >> 31)) >> 0x1c) -
                               (iRawIdx >> 31);
                pSlot = &DAT_805786b8 + iWrapped * 0x5b + iPlayerIndex * 0x5df;

                if ((*pSlot != 0) && (0.0f <= (float)pSlot[0x51])) {
                    int iOverlap = hud_slot_check_overlap();
                    if (iOverlap == 0) {
                        int iType = *pSlot;
                        if (iType == 0xb) {
                            hud_text_draw_centered_dual();
                            hud_slot_accumulate_height(pSlot, (int)aHeightAccum);
                        } else if (iType == 6) {
                            hud_text_draw_image_bar((int)pSlot);
                            hud_slot_accumulate_height(pSlot, (int)aHeightAccum);
                        } else if (iType == 0xd) {
                            hud_text_draw_centered_mirror();
                            hud_slot_accumulate_height(pSlot, (int)aHeightAccum);
                        } else if (iType == 0xc) {
                            hud_text_draw_score_entry();
                            hud_slot_accumulate_height(pSlot, (int)aHeightAccum);
                        }
                    }
                }
            }
        }
    }
    FUN_8038273c();
}

// ============================================================================
// Address: 0x801089ac | hud_slot_draw_primary
// MASSIVE primary HUD slot draw dispatcher. Iterates 16 slots and dispatches
// draw calls based on slot type:
//   1 = multiline, 2 = player name, 3 = multiline_bottom,
//   4/5 = centered, 6 = image_bar (handled by secondary), 7 = typewriter,
//   8 = single_fade, 9 = header_centered (with timer gate), 10 = score_entry
// Type 7 (typewriter) also handles button-press to advance/skip (3 presses to dismiss).
// ============================================================================
void hud_slot_draw_primary(void) {
    int iVar;
    int iPlayerIndex;
    int *pSlot;
    int iSlotIdx;
    u32 uButtons;
    double dThreshold;
    u32 aHeightAccum[14];

    int iRetVal = FUN_803826ec();
    iPlayerIndex = DAT_80611d74[1];

    if (iPlayerIndex < 0) {
        FUN_80382738();
        return;
    }

    int iPlayerOffset = iPlayerIndex * 0x177c;
    FUN_8016f00c(1);

    if (DAT_80611d74[1] < 0) {
        FUN_80382738();
        return;
    }

    // Clear height accumulator
    for (int i = 0; i < 14; i++) {
        aHeightAccum[i] = 0;
    }

    for (iSlotIdx = 0; iSlotIdx < 16; iSlotIdx++) {
        // Compute wrapped slot index from circular buffer
        int iRawIdx = *(int *)(&DAT_805786b0 + iPlayerOffset) + iSlotIdx;
        int iWrapped = ((iRawIdx >> 31) * 0x10 |
                       (u32)(iRawIdx * 0x10000000 + (iRawIdx >> 31)) >> 0x1c) -
                       (iRawIdx >> 31);
        pSlot = &DAT_805786b8 + iWrapped * 0x5b + iPlayerIndex * 0x5df;

        if (((*pSlot != 0) && (0.0f <= (float)pSlot[0x51])) &&
            (iVar = hud_slot_check_overlap(), iVar == 0) &&
            ((iRetVal == 0 || (*pSlot == 9)))) {

            switch (*pSlot) {
            case 1: // Multiline text
                hud_text_draw_multiline();
                hud_slot_accumulate_height(pSlot, (int)aHeightAccum);
                break;

            case 2: // Player name with fade threshold
                dThreshold = (double)(FLOAT_80615764 * (float)pSlot[0x54]);
                hud_text_draw_player_name();
                hud_slot_accumulate_height(pSlot, (int)aHeightAccum);
                break;

            case 3: // Multi-line bottom
                hud_text_draw_multiline_bottom();
                hud_slot_accumulate_height(pSlot, (int)aHeightAccum);
                break;

            case 4: // Centered
            case 5:
                hud_text_draw_centered();
                hud_slot_accumulate_height(pSlot, (int)aHeightAccum);
                break;

            case 7: // Typewriter with button advance
                uButtons = input_get_buttons_held(*DAT_80611d74);
                if ((DAT_80611d74[8] & uButtons) != 0) {
                    input_reset_pad(*DAT_80611d74);
                    *(int *)(&DAT_80579d90 + iPlayerOffset) += 1;
                }
                hud_text_draw_typewriter();
                hud_slot_accumulate_height(pSlot, (int)aHeightAccum);
                // 3 button presses dismisses the typewriter
                if (*(int *)(&DAT_80579d90 + iPlayerOffset) == 3) {
                    *pSlot = 0;
                    *(int *)(&DAT_80579d90 + iPlayerOffset) = 0;
                }
                break;

            case 8: // Single line with fade
                hud_text_draw_single_fade((int)pSlot);
                hud_slot_accumulate_height(pSlot, (int)aHeightAccum);
                break;

            case 9: // Header centered (timer-gated)
                uButtons = hud_should_show_timer();
                if (uButtons != 0) {
                    hud_text_draw_header_centered((int)pSlot);
                }
                hud_slot_accumulate_height(pSlot, (int)aHeightAccum);
                break;

            case 10: // Score entry
                hud_text_draw_score_entry();
                hud_slot_accumulate_height(pSlot, (int)aHeightAccum);
                break;
            }
        }
    }
    FUN_80382738();
}

// ============================================================================
// Address: 0x80108c90 | hud_check_final_round_notify
// Checks if a player/team has reached score_limit - 1. If so, submits a
// "final round" text notification to all players and plays sound 0xd4a.
// Handles both FFA (player score) and team (team score) modes.
// ============================================================================
void hud_check_final_round_notify(int *pPlayerData) {
    int iScoreRemaining;
    u32 uVar;
    int iVar;
    int *pText;
    int aSoundParams[12];

    // Only in arcade mode (0x09), not in 0x0A mode (deathmatch?)
    if ((DAT_805015bd != '\t') || (DAT_805015be == '\n')) {
        return;
    }

    if (DAT_805015cc <= 0) return; // no score limit

    // Compute remaining score
    if ((DAT_805015c0 & 1) == 0) {
        // FFA mode
        uVar = FUN_80366060();
        if (uVar == 0) {
            iScoreRemaining = DAT_805015cc -
                *(int *)(DAT_80611d90 + pPlayerData[2] * 0xea0 + 0xa6c);
        } else {
            int iScore = score_get_player(pPlayerData[2]);
            iScoreRemaining = DAT_805015cc - iScore;
        }
    } else {
        // Team mode
        int iTeamScore = score_get_team(pPlayerData[0x21]);
        iScoreRemaining = DAT_805015cc - iTeamScore;
    }

    // Skip in elimination mode unless this is the active player
    if ((DAT_805015be == '\x12') && (*pPlayerData != DAT_80611910)) {
        return;
    }

    // Final round: 1 point remaining
    if (iScoreRemaining == 1) {
        if ((DAT_805015c0 & 1) == 0) {
            // FFA: "X needs 1 more to win"
            int iString = get_string_by_id(0x18ad);
            pText = (int *)FUN_8012e878(/* format params */
                &DAT_8061085c, pPlayerData[0x1c], iString);
        } else {
            // Team: "Team X needs 1 more to win"
            int iString = get_string_by_id(0x18ac);
            int iTeamName = get_string_by_id(pPlayerData[0x21] + 0xad0);
            pText = (int *)FUN_8012e878(/* format params */
                &DAT_8061085c, iTeamName, iString);
        }

        // Submit to all players
        int iNumPlayers = FUN_801c1714();
        for (int i = 0; i < iNumPlayers; i++) {
            hud_text_submit(i, 3, (u32 *)pText);
        }

        // Play notification sound
        sound_init_play_params((int)aSoundParams);
        aSoundParams[0] = 0xd4a;
        sound_play_request(aSoundParams);
    }
}

// ============================================================================
// Address: 0x80108e0c | hud_damage_particle_update
// Updates the screen damage/blood particle effect based on player health state.
// Adjusts particle emitter intensity, position offset, and scale using the
// camera lerp factor for smooth transitions. More damage = more particles.
// ============================================================================
void hud_damage_particle_update(void) {
    u32 *pEmitter;
    int iDamageSource;
    double dHealthRatio;
    double dLerpFactor;
    double dVerticalRatio;

    // Get damage source entity from current player data
    iDamageSource = *(int *)(*(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c) + 0xc14);
    if (iDamageSource == 0) return;

    dHealthRatio = (double)*(float *)(*(int *)(iDamageSource + 0x7c) + 0xdc);

    if (*(int *)(*(int *)(iDamageSource + 0x7c) + 0x10) == 1) {
        // Dead: max particle intensity
        pEmitter = particle_get_current_player_emitter();
        pEmitter[0x17] = 0x7f;
    } else {
        int iIsBlocking = FUN_8024077c(iDamageSource);
        if (iIsBlocking == 0) {
            // Normal damage: base intensity + health-scaled bonus
            pEmitter = particle_get_current_player_emitter();
            pEmitter[0x17] += 0x14; // base increment

            if (dHealthRatio < (double)FLOAT_80615718) {
                // Below full health: additional particles proportional to damage
                pEmitter = particle_get_current_player_emitter();
                pEmitter[0x17] += (int)(FLOAT_806157c8 *
                                       (float)((double)FLOAT_80615718 - dHealthRatio));
            }
        } else {
            // Blocking/shielding: high intensity
            pEmitter = particle_get_current_player_emitter();
            pEmitter[0x17] += 0x50;
        }
    }

    // Apply camera-based particle position/scale adjustment
    if (*(short *)(iDamageSource + 0x94) == 0) {
        dLerpFactor = player_get_camera_lerp_factor(DAT_80611d54);
        float fCamHeight = *(float *)(*(int *)(DAT_80611d74 + 8) + 0x394);
        dVerticalRatio = (double)(float)((double)fCamHeight - dLerpFactor);

        dLerpFactor = player_get_camera_lerp_factor(DAT_80611d54);
        float fCamDist = *(float *)(*(int *)(DAT_80611d74 + 8) + 900);
        double dRatio = (double)(float)((double)(float)((double)fCamDist - dLerpFactor) /
                        (float)dVerticalRatio);

        // Adjust emitter position (offset 0xd) - vertical shake
        pEmitter = particle_get_current_player_emitter();
        pEmitter[0xd] = (int)((double)FLOAT_80615750 * dRatio +
                        (double)(float)((double)CONCAT44(0x43300000, pEmitter[0xd] ^ 0x80000000) -
                        DOUBLE_80615738));

        // Adjust emitter scale X (offset 6)
        pEmitter = particle_get_current_player_emitter();
        pEmitter[6] = (int)((double)FLOAT_806157cc * dRatio +
                     (double)(float)((double)CONCAT44(0x43300000, pEmitter[6] ^ 0x80000000) -
                     DOUBLE_80615738));

        // Adjust emitter scale Y (offset 5)
        pEmitter = particle_get_current_player_emitter();
        pEmitter[5] = (int)((double)FLOAT_806157d0 * dRatio +
                     (double)(float)((double)CONCAT44(0x43300000, pEmitter[5] ^ 0x80000000) -
                     DOUBLE_80615738));

        // Adjust emitter intensity (offset 0x17) by camera ratio
        pEmitter = particle_get_current_player_emitter();
        pEmitter[0x17] = (int)((double)FLOAT_806157d4 * dRatio +
                        (double)(float)((double)CONCAT44(0x43300000, pEmitter[0x17] ^ 0x80000000) -
                        DOUBLE_80615738));
    }
}
