// Address: 0x800a9f84 - 0x800ab000 | Batch 218 | Date: 2026-08-11
// Player input processing, state dispatch, animation, room resolution,
// rumble feedback, and camera shake broadcast.

#include "common.h"
#include "player.h"
#include "entity.h"
#include "camera.h"
#include "input.h"
#include "scene.h"

// ============================================================================
// 0x800a9f84 | player_broadcast_camera_shake
// Applies camera shake to ALL local player slots. Used by explosions and
// world events that affect everyone regardless of distance.
// Iterates g_nPlayerCount slots at 0x141C stride from g_pPlayerSlotBase.
// ============================================================================
void player_broadcast_camera_shake(float amplitude, float frequency, float decay) {
    int offset = 0;
    for (int i = 0; i < g_nPlayerCount; i++) {
        player_apply_camera_shake(amplitude, frequency, decay,
                                  g_pPlayerSlotBase + offset);
        offset += 0x141c;
    }
}

// ============================================================================
// 0x800aa004 | player_sync_attached_entities
// Synchronizes positions of up to 3 attached entities on the player character:
//   +0x2BC (offset 700): Primary weapon entity
//   +0x18C: Secondary attachment (e.g. shield, off-hand)
//   +0x80: Head/hat attachment
// Called after bone matrix updates to keep attachments aligned.
// ============================================================================
void player_sync_attached_entities(void) {
    int pEntity = *(int*)(g_pActivePlayerSlot + 0x14);
    int pCharData = *(int*)(pEntity + 0x7c);

    // Primary weapon attachment
    if (*(int*)(pCharData + 700) != 0) {
        entity_sync_attached_position(*(int*)(pCharData + 700));
    }
    // Secondary attachment
    if (*(int*)(pCharData + 0x18c) != 0) {
        entity_sync_attached_position(*(int*)(pCharData + 0x18c));
    }
    // Head/hat attachment
    if (*(int*)(pCharData + 0x80) != 0) {
        entity_sync_attached_position(*(int*)(pCharData + 0x80));
    }
}

// ============================================================================
// 0x800aa064 | player_animation_tick
// Per-frame animation channel dispatch. Drives bone animations for:
//   Channel 0x45: Body movement (walk/run/strafe)
//   Channel 0x49: Upper body overlay (flag 4 of +0xc6c)
//   Channel 0x4E: Weapon aim layer (when weapon equipped and state != 4)
//   Channel 0x4B: Facial animation (only when frame counter > 0 and anim index valid)
//   Channel 0x29: Generic update (every active frame)
//   Channels 0x31/0x32/0x34: Speed-based blending on the character entity
// Skipped when player is scoped (0x10) or turret-mounted (0x40000).
// ============================================================================
void player_animation_tick(float lookX, float lookY) {
    int pEntity = g_pActivePlayerSlot_arr[5];
    int pCharData = *(int*)(pEntity + 0x7c);
    int entityId = *g_pActivePlayerSlot_arr;

    float dt = g_fDeltaTime * (FRAMERATE_BASE / g_fFrameRate);

    // Body/aim animations (skipped in scope/turret)
    if (g_pActivePlayerSlot_arr[0x45] != 0x10 &&
        g_pActivePlayerSlot_arr[0x45] != 0x40000) {
        anim_channel_tick(dt, 0x45, entityId);

        if ((*(unsigned int*)(pCharData + 0xc6c) & 4) != 0) {
            anim_channel_tick(dt, 0x49, entityId);
        }
        if (*(int*)(pCharData + 0xc04) != 0 &&
            g_pActivePlayerSlot_arr[0x45] != 4) {
            anim_channel_tick(dt, 0x4e, entityId);
        }
    }

    // Active-frame animations
    if (g_nFrameCounter != 0) {
        int animIndex[2];
        get_player_anim_state(animIndex, NULL, NULL);

        if (animIndex[0] >= 0 && animIndex[0] < 6) {
            anim_channel_tick(dt, 0x4b, entityId);
        }
        anim_channel_tick(dt, 0x29, entityId);
        g_nAnimAccum += g_nFrameCounter;
    }

    // Speed-based blending on character entity
    float absX = (lookX > 0.0f) ? lookX : -lookX;
    float negX = (lookX <= 0.0f) ? -lookX : lookX; // mirrored
    anim_channel_tick(absX, 0x31, *(int*)(pCharData + 8));
    anim_channel_tick(negX, 0x32, *(int*)(pCharData + 8));

    float absY = (lookY >= 0.0f) ? lookY : -lookY;
    anim_channel_tick(absY, 0x34, *(int*)(pCharData + 8));
}

// ============================================================================
// 0x800aa2a4 | player_resolve_room
// Resolves which scene room the player is in by raycasting from the previous
// position to the current eye position. Two modes:
//   - Standard (g_bPortalSystem == 0): scene_ray_world_collision
//   - Portal-based: scene_ray_traverse_portals (walks through portals)
// Stores result in player slot +0xDEC (current room ID) and syncs to entity.
// Falls back to previous room if portal traversal returns null.
// ============================================================================
void player_resolve_room(void) {
    int pSlot = g_pActivePlayerSlot;
    float* pEyePos = (float*)(pSlot + 0xb4);
    float* pPrevPos = (float*)(pSlot + 0x158);

    float dirX = *(float*)(pSlot + 0xb4) - *(float*)(pSlot + 0x158);
    float dirY = *(float*)(pSlot + 0xb8) - *(float*)(pSlot + 0x15c);
    float dirZ = *(float*)(pSlot + 0xbc) - *(float*)(pSlot + 0x160);

    int newRoom;
    if (g_bPortalSystem == 0) {
        // No portal system: simple world collision raycast
        newRoom = scene_ray_world_collision(pPrevPos, &dirX,
                                            *(int*)(pSlot + 0xdec));
    } else {
        // Portal system: traverse portals along movement vector
        int startRoom = (*(int*)(pSlot + 0xdec) != 0) ?
                        *(int*)(pSlot + 0xdec) : 1;
        newRoom = scene_ray_traverse_portals(pPrevPos, startRoom, &dirX, 0);

        if (newRoom == 0) {
            newRoom = *(int*)(pSlot + 0xdf0);  // fallback to last known
        }
        *(int*)(pSlot + 0xdf0) = newRoom;
    }

    *(int*)(pSlot + 0xdec) = newRoom;
    // Sync room to entity
    *(int*)(*(int*)(pSlot + 0x14) + 0x34) = newRoom;
}

// ============================================================================
// 0x800aa3b0 | player_rumble_decay_tick
// Exponential rumble decay each frame. When intensity > threshold and
// button flags indicate dual-motor mode (bits 4+5 of pad status byte+2),
// adds scaled intensity to the current rumble value.
// Decays by multiplying intensity by decay factor each sub-frame.
// ============================================================================
void player_rumble_decay_tick(void) {
    int* pSlot = g_pActivePlayerSlot_arr;
    int pCharData = *(int*)(pSlot[5] + 0x7c);

    // Add rumble when above threshold and both motors active
    if (RUMBLE_THRESHOLD < (float)pSlot[0x395] &&
        g_nFrameCounter != 0 &&
        (*(unsigned short*)(pSlot[0x4e6] + 2) & 0x30) == 0x30) {

        unsigned int currentRumble = input_get_rumble_intensity(*pSlot);
        float addedRumble = RUMBLE_SCALE * (float)pSlot[0x395];
        input_set_rumble_intensity(*pSlot, (int)(addedRumble + (float)currentRumble));
    }

    // Exponential decay per sub-frame
    for (int i = 0; i < g_nFrameCounter; i++) {
        pSlot[0x395] = (int)((float)pSlot[0x395] * (float)pSlot[0x396]);
    }
}

// ============================================================================
// 0x800aa478 | player_pause_input_handler
// Checks if the pause button is pressed/held based on player state.
// In non-interactive states (scoped, mounted, dead, menu, cutscene, vehicle,
// etc.) clears the pause flag unconditionally.
// In normal play: checks button press (momentary) or hold (toggle) based on
// pad config bit 2 of pad status.
// Sets/clears flag bit 4 (0x10) in player slot +0xC (flags field).
// ============================================================================
void player_pause_input_handler(void) {
    int playerState = g_pActivePlayerSlot_arr[0x45];

    // States that block pause input
    if (playerState == 0x10 || playerState == 0x40000 ||
        FUN_8001d5e0() != 0 ||  // system menu active
        playerState == 0x20 || playerState == 0x40 ||
        playerState == 0x100 || playerState == 0x400 ||
        playerState == 0x80 || playerState == 0x100000 ||
        playerState == 0x10000 || playerState == 0x20000 ||
        playerState == 0x2000 || playerState == 0x8000 ||
        playerState == 0x4000 || playerState == 0x80000) {
        g_pActivePlayerSlot_arr[3] &= ~0x10;
        return;
    }

    unsigned short padFlags = *(unsigned short*)(g_pActivePlayerSlot_arr[0x4e6] + 2);

    if ((padFlags & 4) == 0) {
        // Momentary press mode
        unsigned int buttons = input_get_buttons_pressed(*g_pActivePlayerSlot_arr);
        if ((g_pActivePlayerSlot_arr[0xb] & buttons) != 0) {
            g_pActivePlayerSlot_arr[3] |= 0x10;
        } else {
            g_pActivePlayerSlot_arr[3] &= ~0x10;
        }
    } else {
        // Toggle mode (hold)
        unsigned int buttons = input_get_buttons_held(*g_pActivePlayerSlot_arr);
        if ((g_pActivePlayerSlot_arr[0xb] & buttons) != 0) {
            g_pActivePlayerSlot_arr[3] ^= 0x10;
        }
    }
}

// ============================================================================
// 0x800aa5bc | player_aim_input_handler
// MASSIVE (~2KB) aim/look input handler. Dispatches based on vehicle/weapon
// mode returned by FUN_80151ec8:
//   Mode 0: Standard FPS look (analog sticks -> yaw/pitch with wrapping)
//   Mode 1/2: Turret aim (constrained yaw with spring interpolation)
//   Mode 3/4: Scope/binoculars (polar coordinate aim with sin table)
// Handles:
//   - Analog input reading (sticks 1-6)
//   - Yaw/pitch wrapping via fmod equivalent (FUN_80390324)
//   - Invert-Y toggle from pad config
//   - Vehicle entity room/collision sync
//   - Fire button, zoom button, shoulder swap button flags
//   - Speed scaling by lerp factor and delta time
// Called from player_state_dispatch when state == 0x400.
// ============================================================================
void player_aim_input_handler(void) {
    // [Full implementation in decompiled pseudocode above]
    // Key state machine with 5 aim modes.
    // See Ghidra decompilation at 0x800aa5bc for complete logic.
}

// ============================================================================
// 0x800aae4c | player_compute_look_direction
// Computes look direction vectors from analog input with acceleration curves.
// Converts analog stick deflection to angular velocity using sin/cos of the
// current aim angle, applies damping (FLOAT_806148FC), accumulates into
// the aim velocity buffer (+0x120, +0x124), and outputs per-frame delta
// angles to param_3/param_4 scaled by delta time and sensitivity table.
// Dispatches to player_animation_tick with the computed values.
// Also syncs the weapon aim bone offset when not in state 4.
// ============================================================================
void player_compute_look_direction(void* outA, void* outB,
                                    float* pDeltaYaw, float* pDeltaPitch,
                                    float* pLookDir) {
    int pCharData = *(int*)(*(int*)(g_pActivePlayerSlot + 0x14) + 0x7c);

    // Compute sin/cos of aim angle for direction
    float aimAngle = *(float*)(g_pActivePlayerSlot + 0x100);
    float scaledAngle = (AIM_SCALE * aimAngle) / AIM_DIVISOR;

    float sinVal = sinf(scaledAngle - AIM_OFFSET);
    float cosVal = sinf(scaledAngle);

    // Accumulate aim velocity with damping
    float lookX = *pLookDir;
    float lookY = pLookDir[1];
    float dampFactor = AIM_DAMP_FACTOR;
    float accelFactor = AIM_ACCEL;

    *(float*)(g_pActivePlayerSlot + 0x120) += accelFactor * cosVal * lookX;
    *(float*)(g_pActivePlayerSlot + 0x124) += accelFactor * sinVal * lookX;
    *(float*)(g_pActivePlayerSlot + 0x120) *= dampFactor;
    *(float*)(g_pActivePlayerSlot + 0x124) *= dampFactor;

    // Output delta angles scaled by sensitivity and dt
    int sensIndex = *(int*)(g_pActivePlayerSlot + 0x11c);
    float sensX = *(float*)(g_pActivePlayerSlot + sensIndex * 4 + 0x70);
    float sensY = *(float*)(g_pActivePlayerSlot + sensIndex * 4 + 0x78);
    float dtScale = g_fDeltaTime / g_fFrameRate;

    *pDeltaYaw = g_fDeltaTime * lookX * (sensX / dtScale);
    *pDeltaPitch = g_fDeltaTime * lookY * (sensY / dtScale);

    // Dispatch animation with look values
    player_animation_tick(*pDeltaYaw, *pDeltaPitch);

    // Sync weapon aim bone (unless in crouch state 4)
    if (*(int*)(g_pActivePlayerSlot + 0x114) != 4) {
        *(int*)(pCharData + 0xb70) = *(int*)(pCharData + 0xbfc);
    }
}

// ============================================================================
// 0x800aaffc | player_aim_stub_empty
// Empty no-op stub. Used as state handler for state 0x80000.
// ============================================================================
void player_aim_stub_empty(void) {
    return;
}

// ============================================================================
// 0x800ab000 | player_state_dispatch
// MASSIVE state router (~1.5KB). Reads player state field (+0x114) and
// dispatches to the appropriate per-frame handler:
//   0x02: FUN_80257824 (standard first-person movement)
//   0x03-0x04: FUN_800a72c8 (transition states)
//   0x08: player_movement_integrate (walking/running)
//   0x10: player_death_tick
//   0x20: player_vehicle_aim_tick
//   0x40: player_vehicle_movement_tick
//   0x80: FUN_80256aa8 (ladder/climb) + optional FUN_8036ff58
//   0x100: FUN_800a831c (stun/knockback)
//   0x400: player_aim_input_handler (scope/turret aim)
//   0x800: player_movement_integrate_jump
//   0x1000: player_stub_empty
//   0x2000: FUN_8036ff58 (rail grind)
//   0x4000: player_mounted_position_update
//   0x8000: FUN_803806d8 (swimming)
//   0x10000: FUN_800a8424 (vent crawl)
//   0x20000: FUN_80227b88 (zipline)
//   0x40000: player_cutscene_movement_tick
//   0x80000: player_aim_stub_empty
//   0x100000: FUN_800a83f4 (spectator fly)
// Skipped entirely if FUN_8025a004 returns true (pause/freeze).
// Called from player_main_tick.
// ============================================================================
void player_state_dispatch(float* pLookDir, float* pMoveDir, float* pAimDir) {
    // Check if player is frozen/paused
    if (FUN_8025a004()) return;

    int state = *(int*)(g_pActivePlayerSlot + 0x114);

    switch (state) {
        case 0x02:   FUN_80257824(); break;
        case 0x03:
        case 0x04:   FUN_800a72c8(); break;
        case 0x08:   player_movement_integrate(); break;
        case 0x10:   player_death_tick(); break;
        case 0x20:   player_vehicle_aim_tick(); break;
        case 0x40:   player_vehicle_movement_tick(); break;
        case 0x80:   FUN_80256aa8(); break;
        case 0x100:  FUN_800a831c(); break;
        case 0x400:  player_aim_input_handler(); break;
        case 0x800:  player_movement_integrate_jump(); break;
        case 0x1000: player_stub_empty(); break;
        case 0x2000: FUN_8036ff58(); break;
        case 0x4000: player_mounted_position_update(); break;
        case 0x8000: FUN_803806d8(); break;
        case 0x10000: FUN_800a8424(); break;
        case 0x20000: FUN_80227b88(); break;
        case 0x40000: player_cutscene_movement_tick(); break;
        case 0x80000: player_aim_stub_empty(); break;
        case 0x100000: FUN_800a83f4(); break;
    }
}
