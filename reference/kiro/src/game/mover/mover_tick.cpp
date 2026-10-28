// Address: 0x80082d88 - 0x80083a5c | Mover Tick/Update System | Date: 2026-08-11
// Part of the mover/platform/elevator subsystem. Contains the main per-frame
// state machine and all helper functions for position integration, linked
// entity sync, sound, and zone updates.

#include "mover.h"
#include "entity.h"
#include "sound.h"
#include "render.h"
#include "scene.h"

// Forward declarations for cross-referenced helpers not yet decompiled
extern void soundpad_resolve_zone_node(int zoneId, int* outNode);
extern void mover_update_portal_flags(void);
extern int  mover_get_entity_internal(void);
extern void* mover_find_linked_entity(int entityId);
extern void mover_build_rotation_matrix(float angle, float* outMatrix);
extern void mover_apply_rotation(float* matrix, float* inOutPos);
extern int  entity_find_by_render_id(unsigned int id);

// ============================================================================
// Address: 0x80082d88
// Returns the mover's current state (0=idle, 1=rising, 2=falling, 3=arrived)
// ============================================================================
int mover_get_active_state(int pEntity) {
    int pMoverData = *(int*)(pEntity + 0x7C);
    return *(int*)(pMoverData + 0x74);
}

// ============================================================================
// Address: 0x80082d94
// Activates a mover: sets state=1, visibility flag, render material, plays
// activation sound (0x366) at entity world position.
// ============================================================================
void mover_activate(int pEntity) {
    int pMoverData = *(int*)(pEntity + 0x7C);

    if (*(int*)(pMoverData + 0x74) == 0) {
        *(int*)(pMoverData + 0x74) = 1;
        *(unsigned int*)(pEntity + 0x28) |= 0x4000000;

        // Set render material to start-of-travel material
        *(int*)(*(int*)(pEntity + 4) + 0x30) = *(int*)(pMoverData + 0x48);

        // Play activation sound if game state == 6 (in-level)
        if (g_gameState == 6) {
            float* pWorldPos = entity_get_world_position(pEntity);
            SoundParams params;
            sound_init_play_params(&params);
            params.soundId = 0x366;
            params.flags3d = 1;
            params.posX = pWorldPos[0];
            params.posY = pWorldPos[1];
            params.posZ = pWorldPos[2];
            *(unsigned int*)(pEntity + 0x228) = sound_play_request(&params);
        }
    }
}

// ============================================================================
// Address: 0x80082e44
// Updates zone node positions from mover displacement. Iterates zone list,
// applies X/Y/Z offset to each node, then refreshes portal flags.
// ============================================================================
void mover_update_zone_positions(double dX, double dY, double dZ) {
    int pEntity = mover_get_entity_internal();
    int pMoverData = *(int*)(pEntity + 0x7C);

    float baseY = *(float*)(*(int*)(*(int*)(pEntity + 0x1F0)) + 0x28);

    int zoneCount = *(int*)(pMoverData + 0x2F4);
    int zoneOffset = 0;

    for (int i = 0; i < zoneCount; i++) {
        int dataOfs = pMoverData + zoneOffset;
        int* pZoneNode = soundpad_resolve_zone_node(*(int*)(dataOfs + 0xB4));

        pZoneNode[3] = (int)(float)(*(float*)(dataOfs + 0xB8) + (float)dX);
        pZoneNode[4] = (int)(float)baseY;
        pZoneNode[5] = (int)(float)(*(float*)(dataOfs + 0xC0) + (float)dZ);
        *(short*)((int)pZoneNode + 10) = (short)*(int*)(pEntity + 0x34);

        zoneOffset += 0x10;
    }

    mover_update_portal_flags();
}

// ============================================================================
// Address: 0x80082f14
// Syncs linked entity position with the mover during rising phase.
// Applies rotation matrix based on entity angle, offsets position, and sets
// render material to rising material (offset 0x54).
// Called from mover_spawn_all_from_level during initial setup.
// ============================================================================
void mover_sync_linked_entity_rising(void) {
    int pEntity = mover_get_entity_internal_alt();
    if (pEntity == 0) return;

    int pMoverData = *(int*)(pEntity + 0x7C);
    int* pLinkedEntity = mover_find_linked_entity(*(int*)(pMoverData + 0x60));

    if (pLinkedEntity == NULL) return;

    float angleFactor = MOVER_ANGLE_SCALE;
    float angleDiv = MOVER_ANGLE_DIV;
    int pLinkedRender = pLinkedEntity[2];

    // Copy base position from mover data
    *(int*)(pLinkedRender + 100) = *(int*)(pMoverData + 100);
    *(int*)(pLinkedRender + 0x68) = *(int*)(pMoverData + 0x68);
    *(int*)(pLinkedRender + 0x6C) = *(int*)(pMoverData + 0x6C);

    // Apply rotation matrix from entity angle
    float rotMatrix[22];
    mover_build_rotation_matrix((angleFactor * *(float*)(pEntity + 0x70)) / angleDiv, rotMatrix);
    mover_apply_rotation(rotMatrix, (float*)(pLinkedRender + 100));

    // Add entity position offset
    *(float*)(pLinkedRender + 100) += *(float*)(pEntity + 100);
    *(float*)(pLinkedRender + 0x68) += *(float*)(pEntity + 0x68);
    *(float*)(pLinkedRender + 0x6C) += *(float*)(pEntity + 0x6C);

    // Set material to rising material
    *(int*)(*(int*)(pLinkedRender + 4) + 0x30) = *(int*)(pMoverData + 0x54);
}

// ============================================================================
// Address: 0x80082fdc
// Plays mover arrival/stop sound. Fades out movement loop, plays one-shot
// arrival sound (0x367) at entity position.
// ============================================================================
void mover_play_arrive_sound(int pEntity) {
    if (g_gameState != 6) return;

    // Fade out the movement loop sound
    sound_register_fade_out(1.0f, 0.0f, (int*)(pEntity + 0x228), NULL);

    // Play arrival one-shot sound at entity position
    float* pWorldPos = entity_get_world_position(pEntity);
    SoundParams params;
    sound_init_play_params(&params);
    params.soundId = 0x367;
    params.flags3d = 1;
    params.posX = pWorldPos[0];
    params.posY = pWorldPos[1];
    params.posZ = pWorldPos[2];
    sound_play_request(&params);
}

// ============================================================================
// Address: 0x80083068
// MASSIVE mover per-frame state machine.
// States:
//   0 = Idle (waiting for trigger, check auto-start delay)
//   1 = Rising (moving along +direction)
//   2 = Falling (moving along -direction)
//   3 = Arrived at top (waiting for re-trigger or auto-return)
//
// Features:
//   - Speed ramp: velocity increases from 0 to max over distance
//   - Position integration along normalized direction vector
//   - Zone position updates (sound propagation)
//   - Linked entity sync (attached platforms/doors)
//   - Player camera effects: fog density + blur proportional to travel ratio
//   - 3D sound source tracking (movement loop)
//   - State transitions with arrival callbacks
//   - Material swaps at end-of-travel thresholds
// ============================================================================
void mover_tick_update(void) {
    int pEntity = mover_get_entity_internal_main();
    int pMoverData = *(int*)(pEntity + 0x7C);
    int state = *(int*)(pMoverData + 8);

    // Compute delta time factor
    float dtFactor = g_fixedDeltaTime * (1.0f / (float)(g_frameCounter ^ 0x80000000));

    if (state == 2) {
        // === FALLING STATE ===
        float halfTravel = *(float*)(pMoverData + 0x18) * MOVER_HALF_TRAVEL_MULT;

        // Speed ramp: accelerate if past halfway or if speed is still zero
        if (-MOVER_RAMP_FACTOR * halfTravel <= *(float*)(pMoverData + 0x0C) ||
            *(float*)(pMoverData + 0x14) <= 0.0f ||
            (*(unsigned int*)(pMoverData + 4) & 0x80) != 0) {

            if (*(float*)(pMoverData + 0x14) < *(float*)(pMoverData + 0x28)) {
                if ((*(unsigned int*)(pMoverData + 4) & 1) == 0) {
                    *(float*)(pMoverData + 0x14) += *(float*)(pMoverData + 0x2C);
                } else {
                    *(float*)(pMoverData + 0x14) = *(float*)(pMoverData + 0x28);
                }
            }
        } else {
            // Deceleration near bottom
            *(float*)(pMoverData + 0x14) =
                (*(float*)(pMoverData + 0x28) *
                 (*(float*)(pMoverData + 0x18) + *(float*)(pMoverData + 0x0C))) / halfTravel;
            if (*(float*)(pMoverData + 0x14) < MOVER_MIN_SPEED) {
                *(float*)(pMoverData + 0x14) = 0.0f;
            }
        }

        // Player camera effects (fog/blur) when flag 0x80 set
        if ((*(unsigned int*)(pMoverData + 4) & 0x80) != 0) {
            mover_apply_player_camera_effects(pEntity, pMoverData);
        }

        // Integrate position: pos -= speed * dt * direction
        *(float*)(pMoverData + 0x10) = *(float*)(pMoverData + 0x0C);
        *(float*)(pMoverData + 0x0C) =
            -(float)((double)*(float*)(pMoverData + 0x14) * (double)dtFactor
                     - (double)*(float*)(pMoverData + 0x0C));

        float displacement = *(float*)(pMoverData + 0x0C);
        float dX = *(float*)(pMoverData + 0x1C) * displacement;
        float dY = *(float*)(pMoverData + 0x20) * displacement;
        float dZ = *(float*)(pMoverData + 0x24) * displacement;

        // Apply to entity position (base + direction * travel)
        *(float*)(pEntity + 100) = *(float*)(pMoverData + 0x30) + dX;
        *(float*)(pEntity + 0x68) = *(float*)(pMoverData + 0x34) + dY;
        *(float*)(pEntity + 0x6C) = *(float*)(pMoverData + 0x38) + dZ;

        mover_update_zone_positions(dX, dY, dZ);

        if (*(int*)(pMoverData + 0x60) != -1) {
            mover_sync_linked_entity();
        }

        // Material swap near bottom threshold
        if (((*(unsigned int*)(pMoverData + 4) & 4) != 0) &&
            (*(float*)(pMoverData + 0x0C) < MOVER_THRESHOLD - *(float*)(pMoverData + 0x18))) {
            mover_deactivate_linked_entities();
            *(int*)(*(int*)(pEntity + 4) + 0x30) = *(int*)(pMoverData + 0x4C);
        }

        // Check arrival at bottom
        if (-*(float*)(pMoverData + 0x18) <= *(float*)(pMoverData + 0x0C)) {
            // Not yet arrived - call per-tick callback if set
            if (*(void**)(pMoverData + 0x304) != NULL) {
                (*(MoverCallback)(pMoverData + 0x304))(pEntity, pMoverData);
            }
        } else {
            // Arrived at bottom
            *(float*)(pMoverData + 0x0C) = 0.0f;
            *(float*)(pMoverData + 0x14) = 0.0f;

            mover_play_arrive_sound(pEntity);

            if (*(int*)(pMoverData + 0x70) == 0) {
                // Transition to state 3 (arrived)
                *(int*)(pMoverData + 8) = 3;
                *(int*)(pMoverData + 0x74) = 0;
                mover_deactivate_linked_entities();
                *(int*)(*(int*)(pEntity + 4) + 0x30) = *(int*)(pMoverData + 0x4C);
                if (*(int*)(pMoverData + 0x60) != -1) {
                    mover_reset_linked_material(pEntity);
                }
            } else {
                // Loop: transition back to rising
                *(int*)(pMoverData + 8) = 1;
            }
        }
    } else if (state < 2) {
        if (state == 0) {
            // === IDLE STATE ===
            if (*(int*)(pMoverData + 0x74) == 0) {
                // Check auto-start with delay
                if (((*(unsigned int*)(pMoverData + 4) & 0x10) != 0) &&
                    (0.0f < *(float*)(pMoverData + 0xA8) ||
                     (*(unsigned int*)(pMoverData + 4) & 0x20) != 0)) {
                    mover_handle_auto_trigger(pEntity);
                }
                *(unsigned int*)(pEntity + 0x28) &= ~0x4000000u;
            } else if (((*(unsigned int*)(pMoverData + 4) & 0x10) == 0) ||
                       *(float*)(pMoverData + 0xAC) <= 0.0f) {
                // Start moving
                *(unsigned int*)(pEntity + 0x28) |= 0x4000000;
                *(int*)(pMoverData + 8) = 2;
                mover_activate_linked_entities();
            } else {
                mover_start_with_delay(pEntity);
            }
            *(float*)(pMoverData + 0x14) = 0.0f;
        } else if (state == 1) {
            // === RISING STATE ===
            float halfTravel = *(float*)(pMoverData + 0x18) * MOVER_HALF_TRAVEL_MULT;

            // Speed ramp
            if (*(float*)(pMoverData + 0x0C) <= MOVER_RAMP_FACTOR * halfTravel ||
                *(float*)(pMoverData + 0x14) <= 0.0f ||
                (*(unsigned int*)(pMoverData + 4) & 0x80) != 0) {

                if (*(float*)(pMoverData + 0x14) < *(float*)(pMoverData + 0x28)) {
                    if ((*(unsigned int*)(pMoverData + 4) & 1) == 0) {
                        *(float*)(pMoverData + 0x14) += *(float*)(pMoverData + 0x2C);
                    } else {
                        *(float*)(pMoverData + 0x14) = *(float*)(pMoverData + 0x28);
                    }
                }
            } else {
                *(float*)(pMoverData + 0x14) =
                    (*(float*)(pMoverData + 0x28) *
                     (*(float*)(pMoverData + 0x18) - *(float*)(pMoverData + 0x0C))) / halfTravel;
                if (*(float*)(pMoverData + 0x14) < MOVER_MIN_SPEED) {
                    *(float*)(pMoverData + 0x14) = 0.0f;
                }
            }

            // Player camera effects
            if ((*(unsigned int*)(pMoverData + 4) & 0x80) != 0) {
                mover_apply_player_camera_effects(pEntity, pMoverData);
            }

            // Integrate position: pos += speed * dt * direction
            *(float*)(pMoverData + 0x10) = *(float*)(pMoverData + 0x0C);
            *(float*)(pMoverData + 0x0C) =
                (float)((double)*(float*)(pMoverData + 0x14) * (double)dtFactor
                        + (double)*(float*)(pMoverData + 0x0C));

            float displacement = *(float*)(pMoverData + 0x0C);
            float dX = *(float*)(pMoverData + 0x1C) * displacement;
            float dY = *(float*)(pMoverData + 0x20) * displacement;
            float dZ = *(float*)(pMoverData + 0x24) * displacement;

            // Use rising base position (offset 0x3C-0x44)
            *(float*)(pEntity + 100) = *(float*)(pMoverData + 0x3C) + dX;
            *(float*)(pEntity + 0x68) = *(float*)(pMoverData + 0x40) + dY;
            *(float*)(pEntity + 0x6C) = *(float*)(pMoverData + 0x44) + dZ;

            mover_update_zone_positions(dX, dY, dZ);

            if (*(int*)(pMoverData + 0x60) != -1) {
                mover_sync_linked_entity();
            }

            // Material swap near top threshold
            if (((*(unsigned int*)(pMoverData + 4) & 4) != 0) &&
                (*(float*)(pMoverData + 0x18) - MOVER_THRESHOLD < *(float*)(pMoverData + 0x0C))) {
                mover_deactivate_linked_entities();
                *(int*)(*(int*)(pEntity + 4) + 0x30) = *(int*)(pMoverData + 0x50);
            }

            // Check arrival at top
            if (*(float*)(pMoverData + 0x0C) <= *(float*)(pMoverData + 0x18)) {
                if (*(void**)(pMoverData + 0x304) != NULL) {
                    (*(MoverCallback)(pMoverData + 0x304))(pEntity, pMoverData);
                }
            } else {
                // Arrived at top
                mover_play_arrive_sound(pEntity);

                if (*(int*)(pMoverData + 0x70) == 0) {
                    // Check auto-return trigger
                    if (((*(unsigned int*)(pMoverData + 4) & 0x10) != 0) &&
                        (0.0f == *(float*)(pMoverData + 0xAC))) {
                        mover_trigger_auto_return(pEntity, *(int*)(pMoverData + 0x78));
                    }
                    *(float*)(pMoverData + 0x0C) = 0.0f;
                    *(float*)(pMoverData + 0x14) = 0.0f;
                    *(int*)(pMoverData + 8) = 0;  // Back to idle
                    *(int*)(pMoverData + 0x74) = 0;
                    mover_deactivate_linked_entities();
                    *(int*)(*(int*)(pEntity + 4) + 0x30) = *(int*)(pMoverData + 0x50);
                    if (*(int*)(pMoverData + 0x60) != -1) {
                        mover_reset_linked_material(pEntity);
                    }
                } else {
                    // Loop: transition to falling
                    *(int*)(pMoverData + 8) = 2;
                    *(float*)(pMoverData + 0x0C) = 0.0f;
                    *(float*)(pMoverData + 0x14) = 0.0f;
                }
            }
        }
    } else if (state == 3) {
        // === ARRIVED-AT-TOP STATE (same logic as idle but returns to rising) ===
        if (*(int*)(pMoverData + 0x74) == 0) {
            if (((*(unsigned int*)(pMoverData + 4) & 0x10) != 0) &&
                (0.0f < *(float*)(pMoverData + 0xA8) ||
                 (*(unsigned int*)(pMoverData + 4) & 0x20) != 0)) {
                mover_handle_auto_trigger(pEntity);
            }
            *(unsigned int*)(pEntity + 0x28) &= ~0x4000000u;
        } else {
            unsigned int flags = *(unsigned int*)(pMoverData + 4);
            if (((flags & 0x10) == 0) || *(float*)(pMoverData + 0xAC) <= 0.0f) {
                if (((flags & 0x3FFFFFF) == 0) || ((flags & 0x20) == 0)) {
                    *(int*)(pMoverData + 8) = 1;
                    *(unsigned int*)(pEntity + 0x28) |= 0x4000000;
                    mover_activate_linked_entities();
                } else {
                    mover_handle_special_return(pEntity);
                }
            } else {
                mover_start_with_delay(pEntity);
            }
        }
        *(float*)(pMoverData + 0x14) = 0.0f;
    }

    // Mark entity as needing transform update
    *(unsigned int*)(pEntity + 0x18) |= 2;
    render_instance_set_material(0.0f, *(int**)(pEntity + 4), 1);

    // 3D sound tracking for movement loop
    if (g_soundEnabled != 0) {
        int prevHandle = *(int*)(pEntity + 0x22C);
        float travelRatio = fabsf(*(float*)(pMoverData + 0x14) / *(float*)(pMoverData + 0x28));
        float volume = travelRatio * MOVER_SOUND_SCALE;
        if (volume > 1.0f) volume = 1.0f;

        float soundPos[3];
        soundPos[0] = *(float*)(pEntity + 100) + *(float*)(pEntity + 0x1A8);
        soundPos[1] = *(float*)(pEntity + 0x68) + *(float*)(pEntity + 0x1AC);
        soundPos[2] = *(float*)(pEntity + 0x6C) + *(float*)(pEntity + 0x1B0);

        sound_update_3d_source(
            MOVER_SOUND_BASE_DB * volume - MOVER_SOUND_ATTEN,
            MOVER_SOUND_RADIUS,
            1.0f,
            pEntity + 0x22C,
            soundPos,
            *(int*)(pMoverData + 0x2FC),
            0
        );

        // If sound handle changed, play additional ambient sound
        if (prevHandle >= 0 && *(int*)(pEntity + 0x22C) != prevHandle) {
            int ambientId = *(int*)(pMoverData + 0x300);
            SoundParams params;
            sound_init_play_params(&params);
            params.flags3d = 1;
            params.posX = soundPos[0];
            params.posY = soundPos[1];
            params.posZ = soundPos[2];
            params.soundId = ambientId;
            sound_play_request(&params);
        }
    }
}

// ============================================================================
// Address: 0x800838c4
// Syncs linked entity position with the mover. Applies rotation matrix and
// position offset. Picks material based on state: rising=0x5C, falling=0x58.
// ============================================================================
void mover_sync_linked_entity(void) {
    int pEntity = mover_get_entity_internal_alt();
    if (pEntity == 0) return;

    int pMoverData = *(int*)(pEntity + 0x7C);
    int* pLinkedEntity = mover_find_linked_entity(*(int*)(pMoverData + 0x60));

    if (pLinkedEntity == NULL) return;

    float angleFactor = MOVER_ANGLE_SCALE;
    float angleDiv = MOVER_ANGLE_DIV;
    int pLinkedRender = pLinkedEntity[2];

    // Copy base position from mover data
    *(int*)(pLinkedRender + 100) = *(int*)(pMoverData + 100);
    *(int*)(pLinkedRender + 0x68) = *(int*)(pMoverData + 0x68);
    *(int*)(pLinkedRender + 0x6C) = *(int*)(pMoverData + 0x6C);

    // Apply rotation matrix from entity angle
    float rotMatrix[22];
    mover_build_rotation_matrix((angleFactor * *(float*)(pEntity + 0x70)) / angleDiv, rotMatrix);
    mover_apply_rotation(rotMatrix, (float*)(pLinkedRender + 100));

    // Add entity position offset
    *(float*)(pLinkedRender + 100) += *(float*)(pEntity + 100);
    *(float*)(pLinkedRender + 0x68) += *(float*)(pEntity + 0x68);
    *(float*)(pLinkedRender + 0x6C) += *(float*)(pEntity + 0x6C);

    // Set material based on direction of travel
    if (*(int*)(pMoverData + 8) == 1) {
        // Rising: use material at offset 0x5C
        *(int*)(*(int*)(pLinkedRender + 4) + 0x30) = *(int*)(pMoverData + 0x5C);
    } else {
        // Falling: use material at offset 0x58
        *(int*)(*(int*)(pLinkedRender + 4) + 0x30) = *(int*)(pMoverData + 0x58);
    }
}

// ============================================================================
// Address: 0x800839a8
// Resets linked entity's render material to idle/default material (offset 0x54).
// ============================================================================
void mover_reset_linked_material(int pEntity) {
    if (pEntity == 0) return;

    int pMoverData = *(int*)(pEntity + 0x7C);
    int* pLinkedEntity = mover_find_linked_entity(*(int*)(pMoverData + 0x60));

    if (pLinkedEntity != NULL) {
        int pLinkedRender = pLinkedEntity[2];
        *(int*)(*(int*)(pLinkedRender + 4) + 0x30) = *(int*)(pMoverData + 0x54);
    }
}

// ============================================================================
// Address: 0x800839f8
// Activates up to 4 linked entities by setting their render material to 1.
// Used when mover starts moving to show associated geometry.
// ============================================================================
void mover_activate_linked_entities(void) {
    int pEntitySlots = mover_get_entity_slots();

    for (int i = 0; i < 4; i++) {
        int pLinked = entity_find_by_render_id(*(unsigned int*)(pEntitySlots + i * 4));
        if (pLinked != 0) {
            *(int*)(*(int*)(pLinked + 4) + 0x30) = 1;
        }
    }
}

// ============================================================================
// Address: 0x80083a5c
// Deactivates up to 4 linked entities by setting their render material to 0.
// Used when mover stops to hide associated geometry.
// ============================================================================
void mover_deactivate_linked_entities(void) {
    int pEntitySlots = mover_get_entity_slots();

    for (int i = 0; i < 4; i++) {
        int pLinked = entity_find_by_render_id(*(unsigned int*)(pEntitySlots + i * 4));
        if (pLinked != 0) {
            *(int*)(*(int*)(pLinked + 4) + 0x30) = 0;
        }
    }
}
