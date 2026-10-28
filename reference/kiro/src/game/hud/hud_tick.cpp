// Address: 0x80109024 - 0x801098e4 | Batch 329 | Date: 2026-08-25
// HUD per-frame tick, particle emitter helpers, viewport margin computation,
// and flash timer management.

#include "common.h"
#include "hud.h"
#include "particle.h"
#include "sound.h"

// ============================================================================
// Address: 0x80109024 | hud_particle_set_scale
// Sets the particle emitter opacity/scale field [3] from a HUD slot parameter.
// param_1+4 contains a float multiplied by 0.5 (FLOAT_80615734).
// ============================================================================
void hud_particle_set_scale(int pSlot) {
    double dScale;
    u32 *pEmitter;

    dScale = (double)(FLOAT_80615734 * *(float *)(pSlot + 4));
    pEmitter = particle_get_current_player_emitter();
    pEmitter[3] = (float)dScale;
}

// ============================================================================
// Address: 0x80109064 | hud_particle_camera_adjust
// Adjusts particle emitter position (XYZ), scale, and intensity fields using
// the camera lerp factor ratio. Applied to screen-space particle effects that
// need to track camera zoom/movement.
// ============================================================================
void hud_particle_camera_adjust(void) {
    u32 *pEmitter;
    double dLerpFactor;
    double dVertical;
    double dRatio;

    // Compute camera ratio from lerp factor
    dLerpFactor = player_get_camera_lerp_factor(DAT_80611d54);
    dVertical = (double)(float)((double)*(float *)(*(int *)(DAT_80611d74 + 8) + 0x394) -
                dLerpFactor);

    dLerpFactor = player_get_camera_lerp_factor(DAT_80611d54);
    dRatio = (double)(float)((double)(float)((double)*(float *)(*(int *)(DAT_80611d74 + 8) + 900) -
             dLerpFactor) / (float)dVertical);

    // Adjust emitter field [9] - position Y
    pEmitter = particle_get_current_player_emitter();
    pEmitter[9] = (int)((double)FLOAT_80615750 * dRatio +
                 (double)(float)((double)CONCAT44(0x43300000, pEmitter[9] ^ 0x80000000) -
                 DOUBLE_80615738));

    // Adjust emitter field [8] - position X
    pEmitter = particle_get_current_player_emitter();
    pEmitter[8] = (int)((double)FLOAT_806157cc * dRatio +
                 (double)(float)((double)CONCAT44(0x43300000, pEmitter[8] ^ 0x80000000) -
                 DOUBLE_80615738));

    // Adjust emitter field [10] - position Z
    pEmitter = particle_get_current_player_emitter();
    pEmitter[10] = (int)((double)FLOAT_806157d8 * dRatio +
                  (double)(float)((double)CONCAT44(0x43300000, pEmitter[10] ^ 0x80000000) -
                  DOUBLE_80615738));

    // Adjust emitter field [6] - scale
    pEmitter = particle_get_current_player_emitter();
    pEmitter[6] = (int)((double)FLOAT_806157ac * dRatio +
                 (double)(float)((double)CONCAT44(0x43300000, pEmitter[6] ^ 0x80000000) -
                 DOUBLE_80615738));

    // Adjust emitter field [0x17] - intensity
    pEmitter = particle_get_current_player_emitter();
    pEmitter[0x17] = (int)((double)FLOAT_80615784 * dRatio +
                    (double)(float)((double)CONCAT44(0x43300000, pEmitter[0x17] ^ 0x80000000) -
                    DOUBLE_80615738));
}

// ============================================================================
// Address: 0x801091f0 | hud_compute_viewport_margins
// Computes HUD viewport margins (top, left, right, bottom) based on player
// count, widescreen mode, and scoreboard visibility. Results stored in:
//   DAT_8057ff50 = top margin
//   DAT_8057ff4c = left margin
//   DAT_8057ff54 = right margin
//   DAT_8057ff58 = bottom margin
// ============================================================================
void hud_compute_viewport_margins(void) {
    bool bFullWidth;
    int iGameState;
    int iPlayerIndex;
    u32 uBaseMargin;
    int iTopMargin;

    if (DAT_80611d74 == 0) {
        iPlayerIndex = 0;
    } else {
        iPlayerIndex = *(int *)(DAT_80611d74 + 4);
    }

    iGameState = FUN_801c1714();
    bFullWidth = false;

    // Full width if: 4-player with index < 2, OR < 4 players with index 0
    if (((iGameState == 4) && (iPlayerIndex < 2)) ||
        ((iGameState < 4 && (iPlayerIndex == 0)))) {
        bFullWidth = true;
    }

    int iHasScoreboard = FUN_8011f6f4();

    if (iHasScoreboard == 0) {
        // No scoreboard: use fixed margins
        uBaseMargin = 0x12; // 18px
        if (*(int *)(DAT_80611748 + 0x34) != 0) {
            uBaseMargin = 10; // widescreen: 10px
        }
        iTopMargin = (int)(FLOAT_80610830 *
                    (float)((double)CONCAT44(0x43300000, uBaseMargin ^ 0x80000000) -
                    DOUBLE_80615738));

        // Extra margin for 10+ player count
        if ((DAT_805015bd == '\n') && (1 < DAT_80612610)) {
            iTopMargin += 10;
        }

        uBaseMargin = 0x12;
        if (*(int *)(DAT_80611748 + 0x34) != 0) {
            uBaseMargin = 10;
        }
        DAT_8057ff58 = (int)(FLOAT_80610830 *
                      (float)((double)CONCAT44(0x43300000, uBaseMargin ^ 0x80000000) -
                      DOUBLE_80615738));
    } else {
        // With scoreboard: compute margin from scoreboard height
        if (bFullWidth) {
            uBaseMargin = 0x12;
            if (*(int *)(DAT_80611748 + 0x34) != 0) {
                uBaseMargin = 10;
            }
            iTopMargin = (int)(FLOAT_80610830 *
                        (float)((double)CONCAT44(0x43300000, uBaseMargin ^ 0x80000000) -
                        DOUBLE_80615738));
        } else {
            uBaseMargin = 0x12;
            if (*(int *)(DAT_80611748 + 0x34) != 0) {
                uBaseMargin = 10;
            }
            int iScoreHeight = FUN_8011f684();
            int iMinMargin = (int)(FLOAT_80610830 *
                            (float)((double)CONCAT44(0x43300000, uBaseMargin ^ 0x80000000) -
                            DOUBLE_80615738));
            iTopMargin = (iScoreHeight < iMinMargin) ? iMinMargin : iScoreHeight;
        }

        // Bottom margin
        if (bFullWidth) {
            uBaseMargin = 0x12;
            if (*(int *)(DAT_80611748 + 0x34) != 0) {
                uBaseMargin = 10;
            }
            int iScoreHeight = FUN_8011f684();
            int iMinMargin = (int)(FLOAT_80610830 *
                            (float)((double)CONCAT44(0x43300000, uBaseMargin ^ 0x80000000) -
                            DOUBLE_80615738));
            DAT_8057ff58 = (iScoreHeight < iMinMargin) ? iMinMargin : iScoreHeight;
        } else {
            uBaseMargin = 0x12;
            if (*(int *)(DAT_80611748 + 0x34) != 0) {
                uBaseMargin = 10;
            }
            DAT_8057ff58 = (int)(FLOAT_80610830 *
                          (float)((double)CONCAT44(0x43300000, uBaseMargin ^ 0x80000000) -
                          DOUBLE_80615738));
        }
    }

    DAT_8057ff50 = iTopMargin;
    // Left/right margins from viewport width scale
    DAT_8057ff4c = (int)(FLOAT_806157dc * *(float *)(DAT_80611748 + 0x28));
    DAT_8057ff54 = DAT_8057ff4c;
}

// ============================================================================
// Address: 0x80109548 | hud_per_frame_tick
// MASSIVE main per-frame HUD update. Called once per player per frame.
// - Reads HUD state from player data (state machine index)
// - Dispatches state-specific handler via function pointer table at DAT_8040c5a8
// - Sets particle emitter scale per state (0x1b/0x26=large, 0x22=medium, 0x21=small, 0x24=tiny)
// - Manages timers (flash, fade, accumulator)
// - Calls: hud_slot_tick_update, hud_portrait_label_tick, and various subsystems
// - Checks for player death to trigger death screen
// ============================================================================
void hud_per_frame_tick(void) {
    float fTimer1, fTimer2;
    int iPlayerIndex;
    u32 *pPlayerHud;
    int iPlayerOffset;
    int iState;
    u32 *pEmitter;
    double dDeltaTime;
    bool bDeathDetected;

    FUN_803826dc();
    iPlayerIndex = *(int *)(DAT_80611d74 + 4);

    if (iPlayerIndex < 0) {
        FUN_80382728();
        return;
    }

    iPlayerOffset = iPlayerIndex * 0x177c;
    pPlayerHud = &DAT_80578680 + iPlayerIndex * 0x5df;

    dDeltaTime = (double)(FLOAT_806108ec *
                 (FLOAT_80615718 /
                  (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                  DOUBLE_80615738)));

    FUN_8025ad20(); // pre-frame setup

    // Copy HUD state from player data
    int iSrcState = DAT_80611d74;
    *pPlayerHud = *(int *)(iSrcState + 0x1390);
    *(u32 *)(&DAT_80578684 + iPlayerOffset) = *(u32 *)(iSrcState + 0x1394);
    (&DAT_80579dcc)[iPlayerOffset] = 0;
    (&DAT_80579dcd)[iPlayerOffset] = 0;

    iState = *pPlayerHud;
    FUN_8011921c();

    // Player 0 death detection
    if (*(int *)(DAT_80611d74 + 4) == 0) {
        bDeathDetected = false;
        for (int i = 0, iOffset = 0, iHudOff = 0;
             i < FUN_801c1714(); i++) {
            if (((*(int *)((int)&DAT_80578680 + iHudOff) == 10) &&
                 (int iDmgSrc = *(int *)(DAT_80611d90 + iOffset + 0xc14), iDmgSrc != 0)) &&
                (*(int *)(*(int *)(iDmgSrc + 0x7c) + 0x10) == 1)) {
                bDeathDetected = true;
            }
            iOffset += 0xea0;
            iHudOff += 0x177c;
        }
        if (bDeathDetected) {
            FUN_80111d54(); // trigger death screen
        }

        // Accumulate animation timer (wrapping)
        for (FLOAT_80611ff8 += FLOAT_806157a8;
             FLOAT_806157e0 <= FLOAT_80611ff8;
             FLOAT_80611ff8 -= FLOAT_806157e0) {
        }
    }

    // Dispatch state handler via function pointer table
    if (*(code **)(&DAT_8040c5a8 + iState * 0x1c) != NULL) {
        (**(code **)(&DAT_8040c5a8 + iState * 0x1c))(pPlayerHud);
    }

    // Set particle emitter scale based on state
    iState = *pPlayerHud;
    if ((iState == 0x1b) || (iState == 0x26)) {
        pEmitter = particle_get_current_player_emitter();
        pEmitter[3] = (float)(double)FLOAT_806157e4; // large
    } else if (iState == 0x22) {
        pEmitter = particle_get_current_player_emitter();
        pEmitter[3] = (float)(double)FLOAT_806157a8; // medium
    } else if (iState == 0x21) {
        pEmitter = particle_get_current_player_emitter();
        pEmitter[3] = (float)(double)FLOAT_80615794; // small
    } else if (iState == 0x24) {
        pEmitter = particle_get_current_player_emitter();
        pEmitter[3] = (float)(double)FLOAT_8061571c; // tiny
    }

    // Update timers
    int iFrameCount = DAT_806108e4;
    *(float *)(&DAT_805786ac + iPlayerOffset) += (float)dDeltaTime;

    int iFlashCounter = *(int *)(&DAT_805786a0 + iPlayerOffset);
    *(int *)(&DAT_805786a0 + iPlayerOffset) = iFlashCounter - iFrameCount;
    if (iFlashCounter - iFrameCount < 0) {
        *(int *)(&DAT_805786a0 + iPlayerOffset) = 0;
    }

    // Decay fade timers
    float fFade1 = *(float *)(&DAT_80579dac + iPlayerOffset) -
                   (float)((double)FLOAT_80615748 * dDeltaTime);
    *(float *)(&DAT_80579dac + iPlayerOffset) = (fFade1 < 0.0f) ? 0.0f : fFade1;

    float fFade2 = *(float *)(&DAT_80579db0 + iPlayerOffset) -
                   (float)((double)FLOAT_80615748 * dDeltaTime);
    *(float *)(&DAT_80579db0 + iPlayerOffset) = (fFade2 < 0.0f) ? 0.0f : fFade2;

    // Flash timer tick
    int iFlashTimer = *(int *)(&DAT_805786a4 + iPlayerOffset);
    if (iFlashTimer != 0) {
        if (iFlashTimer < *(int *)(&DAT_805786a8 + iPlayerOffset)) {
            *(int *)(&DAT_805786a4 + iPlayerOffset) = iFlashTimer + DAT_806108e4;
        }
        if (*(int *)(&DAT_805786a8 + iPlayerOffset) <=
            *(int *)(&DAT_805786a4 + iPlayerOffset)) {
            *(int *)(&DAT_805786a4 + iPlayerOffset) = 0;
        }
    }

    // Special state handling
    if (*pPlayerHud == 0xf) {
        pEmitter = particle_get_current_player_emitter();
        pEmitter[0x28] = 1;
    }
    if (*pPlayerHud == 0x11) {
        FUN_80357dd8(); // cinematic state handler
    }

    // Subsystem updates
    hud_slot_tick_update();
    FUN_80111f70();
    FUN_8011d46c((int)pPlayerHud);
    FUN_80123ecc();
    FUN_8035c970();
    FUN_8011d71c();

    if ((DAT_805015c0 & 0x40) != 0) {
        FUN_8011e7dc();
    }

    // Story mode portrait labels
    if (DAT_805015bd == '\x03') {
        FUN_80125654();
        hud_portrait_label_tick(*(int *)(DAT_80611d74 + 4));
    }

    // Arcade/challenge portrait labels
    if (((DAT_805015bd == '\a') ||
         (((DAT_805015c0 & 0x40) != 0 && (FUN_8022d71c() != 0)))) ||
        ((DAT_805015bd == '\n' &&
          ((DAT_80612614 == 0x1a || (DAT_80612614 == 0x11)))))) {
        FUN_80125654();
    }

    // Score/timer HUD elements
    if ((DAT_805015bd == '\n') || ((DAT_805015c0 & 0x40) != 0)) {
        FUN_80122f64();
    }

    FUN_80382728();
}

// ============================================================================
// Address: 0x801098e4 | hud_set_flash_timer
// Sets a flash/blink timer for a player's HUD element. Used for hit indicator
// flashing and pickup notifications. Handles initialization and mid-flash
// adjustment.
// ============================================================================
void hud_set_flash_timer(int iPlayerIndex, u32 uDuration) {
    if (iPlayerIndex < 0) return;
    if (iPlayerIndex > 3) return;

    int iOffset = iPlayerIndex * 0x177c;
    *(u32 *)(&DAT_805786a8 + iOffset) = uDuration;

    int iCurrent = *(int *)(&DAT_805786a4 + iOffset);
    if (iCurrent == 0) {
        // Start new flash
        *(int *)(&DAT_805786a4 + iOffset) = 1;
        return;
    }

    // Already flashing: adjust if near end
    if (iCurrent < (int)(*(int *)(&DAT_805786a8 + iOffset) - 0x20)) {
        return;
    }

    // Reset to remaining time
    *(int *)(&DAT_805786a4 + iOffset) =
        *(int *)(&DAT_805786a8 + iOffset) - iCurrent;
}
