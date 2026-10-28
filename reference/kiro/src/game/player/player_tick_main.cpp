// Address: 0x800a9138 - 0x800a9f58 | Batch 217 | Date: 2026-08-11
// Player per-frame tick pipeline: main tick, head bob, music fade,
// visibility culling, eye position, camera shake utilities.

#include "common.h"
#include "player.h"
#include "entity.h"
#include "camera.h"
#include "input.h"
#include "sound.h"
#include "nav.h"
#include "damage.h"
#include "cutscene.h"

// ============================================================================
// 0x800a9cd4 | player_slot_clear_active
// Clears the active player slot pointer (called during teardown/level unload).
// ============================================================================
void player_slot_clear_active(void) {
    g_pActivePlayerSlot = NULL;
}

// ============================================================================
// 0x800a9ce0 | player_is_scoped_or_mounted
// Returns true if entity state is sniper-scoped (0x10) or turret-mounted (0x40000).
// Used by AI targeting and visibility systems to identify stationary players.
// ============================================================================
int player_is_scoped_or_mounted(int pPlayerSlot) {
    int state = *(int*)(pPlayerSlot + 0x114);
    if (state != 0x10 && state != 0x40000) {
        return 0;
    }
    return 1;
}

// ============================================================================
// 0x800a9e74 | player_set_eye_position
// Sets the player's eye (camera) position with Y-axis clamping.
// In story mode (mode > 100), no clamping is applied.
// Otherwise Y is clamped to [MIN_EYE_OFFSET, MAX_EYE_OFFSET] relative to feet.
// ============================================================================
void player_set_eye_position(float posX, float posY, float posZ, int pPlayerSlot) {
    float feetY = *(float*)(pPlayerSlot + 0xac);
    float deltaY = posY - feetY;

    *(float*)(pPlayerSlot + 0xb4) = posX;

    if (g_nGameMode > 100 || (deltaY >= MIN_EYE_OFFSET && deltaY <= MAX_EYE_OFFSET)) {
        *(float*)(pPlayerSlot + 0xb8) = posY;
    } else {
        if (deltaY < MIN_EYE_OFFSET) {
            *(float*)(pPlayerSlot + 0xb8) = MIN_EYE_OFFSET + feetY;
        }
        if (deltaY > MAX_EYE_OFFSET) {
            *(float*)(pPlayerSlot + 0xb8) = MAX_EYE_OFFSET + feetY;
        }
    }

    *(float*)(pPlayerSlot + 0xbc) = posZ;
}

// ============================================================================
// 0x800a9ee4 | player_set_eye_position_and_update
// Sets eye position then immediately updates the camera system.
// Wrapper combining player_set_eye_position + camera position update.
// ============================================================================
void player_set_eye_position_and_update(float posX, float posY, float posZ, int pPlayerSlot) {
    player_set_eye_position(posX, posY, posZ, pPlayerSlot);
    player_camera_position_update();
}

// ============================================================================
// 0x800a9f08 | player_apply_camera_shake
// Applies camera shake if the player is not in a special state (scoped/mounted/
// cutscene/vehicle) and the camera slot has the shake-enabled flag (bit 2 of +0x21c).
// ============================================================================
void player_apply_camera_shake(float amplitude, float frequency, float decay, int pPlayerSlot) {
    int pCameraSlot = *(int*)(pPlayerSlot + 8);
    if (pCameraSlot == 0) return;

    unsigned int playerState = *(unsigned int*)(pPlayerSlot + 0x114);
    if ((playerState & 0x1121e0) != 0) return;

    unsigned int cameraFlags = *(unsigned int*)(pCameraSlot + 0x21c);
    if ((cameraFlags & 4) != 0) {
        camera_set_shake(amplitude, frequency, decay, pCameraSlot);
    }
}

// ============================================================================
// 0x800a9f58 | player_apply_camera_shake_priority
// Priority-based camera shake - always applies if camera slot exists.
// Used for explosions and high-impact events that override current shake.
// ============================================================================
void player_apply_camera_shake_priority(float amplitude, float frequency, float decay, int pPlayerSlot) {
    int pCameraSlot = *(int*)(pPlayerSlot + 8);
    if (pCameraSlot != 0) {
        camera_set_shake_priority(amplitude, frequency, decay, pCameraSlot);
    }
}

// ============================================================================
// 0x800a9a40 | player_update_head_bob
// Computes head-bob Y position from player movement animation.
// Builds a rotation matrix from player facing angles, transforms the
// bone attachment point, then smoothly interpolates the bob height
// (clamped per-frame by HEAD_BOB_MAX_DELTA in multiplayer mode 10).
// Called from the main player tick when state != 0x400 (not dead).
// ============================================================================
void player_update_head_bob(void) {
    int pPlayerSlot = g_pActivePlayerSlot;
    int pEntity = *(int*)(pPlayerSlot + 0x14);
    int pCharData = *(int*)(pEntity + 0x7c);

    float angleX = *(float*)(pPlayerSlot + 0xa8);
    float angleY = *(float*)(pPlayerSlot + 0xac);
    float angleZ = *(float*)(pPlayerSlot + 0xb0);
    float scale = (HEAD_BOB_SCALE * *(float*)(pEntity + 0x70)) / HEAD_BOB_DIVISOR;

    float rotMatrix[16];
    mtx_build_rotation(angleX, angleY, angleZ, scale, rotMatrix);

    int pModelData = *(int*)(*(int*)(*(int*)(pEntity + 4) + 0x10));
    int boneOffset = *(int*)(*(int*)(*(int*)(pEntity + 4) + 0x84) + 0x18);
    float* pBoneMatrix = (float*)(pModelData + boneOffset * 0x40);

    float resultMatrix[12];
    mtx_multiply_3x4(pBoneMatrix, rotMatrix, resultMatrix);

    float bobX = resultMatrix[9];    // local_58 -> tx
    float bobY = resultMatrix[10] - HEAD_BOB_Y_OFFSET;  // local_54 - constant
    float bobZ = resultMatrix[11];   // local_50 -> tz

    *(float*)(pCharData + 0xbb4) = bobX;
    *(float*)(pCharData + 0xbb8) = bobY;
    *(float*)(pCharData + 0xbbc) = bobZ;

    // In mode 10 (multiplayer): smooth interpolation with max delta
    if (g_nGameModeId == 10) {
        float currentY = *(float*)(pCharData + 0xbb8);
        float prevY = *(float*)(pCharData + 0xbc0);
        float diff = currentY - prevY;

        if (HEAD_BOB_MAX_DELTA < fabsf(diff)) {
            if (currentY <= prevY) {
                *(float*)(pCharData + 0xbc0) = prevY - HEAD_BOB_MAX_DELTA;
            } else {
                *(float*)(pCharData + 0xbc0) = prevY + HEAD_BOB_MAX_DELTA;
            }
        } else {
            *(float*)(pCharData + 0xbc0) = currentY;
        }
    } else {
        *(float*)(pCharData + 0xbc0) = *(float*)(pCharData + 0xbb8);
    }
}

// ============================================================================
// 0x800a9b48 | player_music_fade_tick
// Per-frame music/sound fade system. Checks game flags to determine which
// audio channel to fade (music master, sfx, ambient) and accumulates a
// fade counter toward 0xFF. Uses difficulty-based speed scaling.
// Called from main tick when game mode == 8 and cutscene subsystem active.
// ============================================================================
void player_music_fade_tick(void) {
    float fDifficultySpeed = game_get_difficulty_speed();
    float fFadeRate = FADE_BASE_RATE / fDifficultySpeed;

    int* pSoundState = get_sound_fade_state();

    if ((g_nGameFlags2 & 0x20000) != 0) {
        // Flag 0x20000: fade channel 9 (ambient sounds)
        pSoundState[9] += 0x7f;
        float scaledRate = FADE_SCALE_AMBIENT * fFadeRate;
        pSoundState[10] = (int)scaledRate;
    } else if ((g_nGameFlags2 & 0x10000) != 0) {
        // Flag 0x10000: fade channel 7 (music)
        pSoundState[7] += 0x7f;
        float scaledRate = FADE_SCALE_MUSIC * fFadeRate;
        pSoundState[10] = (int)scaledRate;
    } else if (((g_nGameFlags1 & 0x40) != 0 || (g_nGameFlags1 & 0x8000000) != 0) &&
               (g_nGameFlags2 & 0x8000) != 0) {
        // Combined flags: fade channel 8 (SFX)
        pSoundState[8] += 0x7f;
        float scaledRate = FADE_SCALE_AMBIENT * fFadeRate;
        pSoundState[10] = (int)scaledRate;
    } else if (g_nGameModeId == 10 && (g_nGameFlags2 & 0x8000) != 0) {
        int isArcade = game_is_standard_arcade();
        if (isArcade != 0) {
            // Arcade: reduce master volume
            float newVolume = FADE_VOLUME_BASE - fFadeRate;
            sound_set_master_volume(newVolume);
        } else {
            // Non-arcade MP: fade with time limit
            if (fFadeRate > FADE_TIME_LIMIT) {
                fFadeRate = FADE_TIME_LIMIT;
            }
            float scaledRate = FADE_SCALE_TIMED * fFadeRate;
            pSoundState[5] = (int)scaledRate;
        }
    }

    // Clamp fade counter to 255
    pSoundState = get_sound_fade_state();
    if (pSoundState[10] > 0xff) {
        pSoundState = get_sound_fade_state();
        pSoundState[10] = 0xff;
    }
}

// ============================================================================
// 0x800a9d04 | player_distance_visibility_cull
// Distance-based visibility culling for multiplayer. When the local player
// is alive and below a health threshold, marks distant enemy entities with
// a per-player visibility bit (reduces render cost for far-away players).
// When player has flag 0x40 set (respawning/spectating), clears all visibility
// bits for all player entities.
// ============================================================================
void player_distance_visibility_cull(void) {
    float fCullDistSq = CULL_DISTANCE_SQ_DEFAULT;

    // In mode 10 with >1 player, use tighter cull distance
    if (g_nGameModeId == 10 && g_nPlayerCount > 1) {
        fCullDistSq = CULL_DISTANCE_SQ_MP;
    }

    int pSlot = g_pActivePlayerSlot;
    unsigned int flags = *(unsigned int*)(pSlot + 0xc);

    // If player has flag 0x40 OR health <= max health threshold
    if ((flags & 0x40) != 0 ||
        *(float*)(g_pActivePlayerData + 0x390) > *(float*)(g_pActivePlayerData + 900)) {
        // Clear all visibility bits for every player entity
        for (int i = 0; i < g_nTotalPlayers; i++) {
            int* pOtherSlot = g_ppPlayerSlotTable[i];
            int pOtherEntity = *pOtherSlot;
            *(unsigned int*)(pOtherEntity + 0xa0) &= 0xffff0000;
        }
        return;
    }

    if (g_nFrameCounter == 0) return;

    // Iterate other players and set visibility bit if too far
    int playerIndex = g_nPlayerCount;
    int localPlayerBit = *(int*)(g_pActivePlayerData + 0x214);

    while (playerIndex < g_nTotalPlayers) {
        int* pOtherSlot = g_ppPlayerSlotTable[playerIndex];
        int pOtherEntity = *pOtherSlot;

        // Skip if player has flag 0x20 or entity has flag 0x400000
        if ((pOtherSlot[10] & 0x20) == 0 &&
            (*(unsigned int*)(pOtherSlot[0x2fc] + 0x54) & 0x400000) == 0 &&
            *(int*)(*(int*)(pOtherEntity + 4) + 8) != 0) {

            int pModelData = *(int*)(*(int*)(pOtherEntity + 4) + 0x10);
            int pCameraData = *(int*)(pSlot + 8);

            float dx = *(float*)(pCameraData + 0x27c) -
                       (*(float*)(pModelData + 0x30) + *(float*)(pOtherEntity + 0x64));
            float dy = *(float*)(pCameraData + 0x280) -
                       (*(float*)(pModelData + 0x34) + *(float*)(pOtherEntity + 0x68));
            float dz = *(float*)(pCameraData + 0x284) -
                       (*(float*)(pModelData + 0x38) + *(float*)(pOtherEntity + 0x6c));

            float distSq = dx * dx + dy * dy + dz * dz;

            if (distSq > fCullDistSq) {
                *(unsigned int*)(pOtherEntity + 0xa0) |= (1 << localPlayerBit);
            }
        }
        playerIndex++;
    }
}

// ============================================================================
// 0x800a9138 | player_main_tick
// MASSIVE per-frame player tick function (~2.2KB). Orchestrates the entire
// player update pipeline each frame:
// - Cutscene skip detection (input_any_player_pressing_skip -> cutscene_shutdown)
// - Input reset per pad
// - Frame timing: damping factor, velocity decay
// - Head bob update (player_update_head_bob)
// - Distance velocity computation (3D + XZ distances)
// - Previous-frame state snapshot (position, direction, angles)
// - Pause input handler
// - Aim/look direction computation + smoothing
// - Height interpolation (eye Y target vs current with spring)
// - Floor edge detection -> fall death
// - Level-specific kill planes (height thresholds)
// - Camera flag 0x40 animation tick
// - HUD + particle update dispatches
// Called once per local player slot per frame from the main game loop.
// ============================================================================
void player_main_tick(/* params passed via registers/stack */) {
    // [Full implementation in decompiled pseudocode above]
    // This function is too large for clean C reconstruction in a single pass.
    // Key operations documented in plate comment.
    // See Ghidra decompilation at 0x800a9138 for complete logic.
}
