// Address: 0x80085618 - 0x800866f4 | Trigger State Machine | Date: 2026-08-11
// Trigger state management: open/close state machine, speed computation,
// revolving door physics, sound updates, flag manipulation, and the trigger_open API.

#include "trigger.h"
#include "entity.h"
#include "sound.h"
#include "mover.h"
#include "scene.h"
#include "math_util.h"

// ============================================================================
// Address: 0x80085618
// Plays trigger state-change sound. Resolves entity from handle in data[0].
// If param_2 is true: selects alternate sound (0x1B) if flag 0x4000000 set
// and sound ID is valid; otherwise plays default sound (0x1A).
// ============================================================================
void trigger_play_state_sound(unsigned int* pData, int playSound) {
    // Resolve entity from stored handle
    unsigned int handle = pData[0];
    int pEntity = 0;
    if (handle != 0) {
        pEntity = g_entityPool + (handle >> 22) * 0x2E8;
        if (*(unsigned int*)(pEntity + 0x2C) != handle) {
            pEntity = 0;
            pData[0] = 0;
        }
    }

    if (playSound != 0) {
        if ((int)pData[0x1B] < 0 || (pData[3] & 0x4000000) == 0) {
            mover_play_sound_at_entity(pEntity, pData[0x1A]);
        } else {
            mover_play_sound_at_entity(pEntity, pData[0x1B]);
        }
    }
}

// ============================================================================
// Address: 0x800856a4
// Computes trigger open/close speed. Three modes:
//   1. Forced open (state==3 + flag 0x2000, or flag 0x4000000): constant
//      speed with type-dependent ramp (type 5/9 use different formula)
//   2. Custom callback (offset 0x8C not null): delegates to function pointer
//   3. Distance-based: speed proportional to remaining travel, with
//      deceleration near endpoints. Handles opening (state 1) and closing
//      (state 3) differently. Clamps to [0, 1].
// ============================================================================
float trigger_compute_open_speed(float currentPos, float maxSpeed, int pEntity) {
    int* pData = (int*)*(int*)(pEntity + 0x7C);

    // Mode 1: Forced open
    if ((pData[4] == 3 && (*(unsigned int*)(pData + 3) & 0x2000) != 0) ||
        (*(unsigned int*)(pData + 3) & 0x4000000) != 0) {
        int animType = pData[2];
        if (animType == 5 || animType == 9) {
            // Accelerating rotation
            return TRIGGER_ACCEL_FACTOR * (1.0f + *(float*)(pData + 0x38/4)) * g_fixedDeltaTime
                   + *(float*)(pData + 0x38/4);
        } else {
            return TRIGGER_CONST_SPEED * g_fixedDeltaTime + *(float*)(pData + 0x38/4);
        }
    }

    // Mode 2: Custom callback
    if (*(void**)(pData + 0x8C/4) != NULL) {
        return (*(SpeedCallback)(pData + 0x8C/4))(*(float*)(pData + 0x24/4),
                                                   *(float*)(pData + 0x30/4));
    }

    // Mode 3: Distance-based speed ramp
    float maxTravel = *(float*)(pData + 0x40/4);
    float targetPos = *(float*)(pData + 0x34/4);
    float storedSpeed = *(float*)(pData + 0x38/4);
    float distToTarget = fabsf(maxSpeed - targetPos);
    float result = 0.0f;

    if (0.0f < maxTravel) {
        // Compute proportional speed
        result = maxSpeed * maxTravel - TRIGGER_DECEL_FACTOR * (distToTarget / maxTravel)
                 * (maxTravel * maxTravel);
        result = clamp(result, 0.0f, 1.0f);
    }

    int openState = pData[4];
    float delta;
    if (openState == 1) {
        delta = currentPos - (*(float*)(pData + 0x74/4) - result);
    } else if (openState == 3) {
        delta = -(currentPos - result);
    }

    if (delta <= 0.0f || result == 0.0f) {
        // Use stored speed, possibly with time-based acceleration
        result = storedSpeed;
        if (storedSpeed < maxSpeed && 0.0f < *(float*)(pData + 0x3C/4)) {
            float accel = g_fixedDeltaTime * g_frameDivisorInv *
                          (distToTarget / *(float*)(pData + 0x3C/4)) + storedSpeed;
            if (accel < targetPos) accel = targetPos;
            result = (accel < maxSpeed) ? accel : accel; // clamp
        }
    } else {
        // Deceleration proportional to remaining
        result = maxSpeed * (1.0f - (delta / result));
        if (result < targetPos) result = targetPos;
    }

    return result;
}

// ============================================================================
// Address: 0x800858b4
// Updates entity collision flag based on trigger open state.
// If disabled (flag 0x1000000): no-op. If fully closed (open <= 0): sets
// flag 0x4000 (solid). Otherwise clears collision flags.
// ============================================================================
void trigger_update_collision_flag(int pEntity) {
    int* pData = (int*)*(int*)(pEntity + 0x7C);

    if ((*(unsigned int*)(pData + 3) & 0x1000000) != 0) return;

    unsigned int collisionBit;
    if (*(float*)(pData + 0x74/4) <= 0.0f) {
        collisionBit = 0x4000;
    } else {
        collisionBit = 0;
    }

    *(unsigned int*)(pEntity + 0x28) &= 0xFFFF3FFF;
    *(unsigned int*)(pEntity + 0x28) |= collisionBit;
}

// ============================================================================
// Address: 0x800858fc
// AABB proximity test: checks if entity1 is within entity3's bounding volume.
// Tests X and Z against extent (data offset 0x4C), Y against height range.
// Returns 1 if inside bounds.
// ============================================================================
int trigger_check_proximity_bounds(int pEntity1, int pDef, int pEntity3, int pData) {
    float extent = *(float*)(pData + 0x4C);

    // X distance check
    if (fabsf(*(float*)(pEntity1 + 100) - *(float*)(pEntity3 + 100)) > extent)
        return 0;

    // Y height check (with offset)
    float baseY = *(float*)(pEntity3 + 0x68) + *(float*)(pEntity3 + 0x1B8);
    float heightRange = *(float*)(pEntity3 + 0x1C4);
    float testY = *(float*)(pEntity1 + 0x68) +
                  *(float*)(&g_entityTypeTable + *(short*)(pDef + 0x48) * 0x30);

    if (testY <= baseY - heightRange) return 0;
    if (testY >= baseY + heightRange) return 0;

    // Z distance check
    if (fabsf(*(float*)(pEntity1 + 0x6C) - *(float*)(pEntity3 + 0x6C)) > extent)
        return 0;

    return 1;
}

// ============================================================================
// Address: 0x80085990
// MASSIVE trigger state machine. States:
//   0: Closed (portal blocked, check for activation via proximity or overlap)
//   1: Opening (integrate speed, advance open amount, transition to 2 at target)
//   2: Open (auto-close timer counting, portal unblocked)
//   3: Closing (integrate speed downward, transition to 0 at bottom)
//
// Also manages: portal zone flags (0x800000), revolving door physics (type 5),
// player activation checks (flag 0x400=AABB, else=overlap), speed integration
// via trigger_compute_open_speed, open/close callbacks (offsets 0x190/0x194),
// state sounds, and auto-close timer.
// ============================================================================
void trigger_state_machine(void) {
    int pEntity = trigger_get_entity_internal_3();
    unsigned int* pData = (unsigned int*)*(int*)(pEntity + 0x7C);
    float dt = g_fixedDeltaTime * (1.0f / g_frameDivisor);

    // Skip if disabled or in special level mode
    if ((pData[3] & 0x1000000) != 0 || g_gameState == 0x66) return;

    // Get portal reference
    unsigned int* pPortal = NULL;
    unsigned int portalIdx = pData[0x11];
    if (portalIdx > 0 && portalIdx < g_portalCount && g_gameState != 100) {
        pPortal = (unsigned int*)(g_portalBase + portalIdx * 0x14);
    }

    // --- Player proximity check for auto-activation ---
    if ((pData[3] & 0x800) == 0) {
        if (pData[4] != 1) {  // Not currently opening
            for (int i = 0; i < g_playerCount; i++) {
                int* pPlayerSlot = g_playerSlots[i];
                if (pPlayerSlot == NULL) continue;
                int pPlayer = *pPlayerSlot;
                if (pPlayer == 0) continue;
                if (*(int*)(pPlayer + 0x150) == 0 && pData[4] == 0) continue;

                int activated = 0;
                if (g_gameState == 100) {
                    // Special level mode check
                    activated = special_level_trigger_check(pPlayer, pEntity);
                    if (activated) {
                        trigger_tick_update();
                        break;
                    }
                } else if ((pData[3] & 0x400) != 0) {
                    // AABB proximity check
                    activated = trigger_check_proximity_bounds(pPlayer, pPlayerSlot, pEntity, pData);
                    if (activated) {
                        trigger_tick_update();
                        break;
                    }
                }
            }
        }
    } else if (pData[2] == 5 && pData[4] != 1) {
        // Revolving door: physics-driven
        trigger_revolving_door_physics();
    }

    // --- Handle activation flag (0x40000000) ---
    if ((pData[3] & 0x40000000) == 0) {
        pData[0x24/4] = 0;
    } else {
        unsigned int state = pData[4];
        if (state == 0) {
            // Transition: closed -> opening
            // Resolve activating entity
            pData[8/4] = 0;
            pData[0x0E] = 0;
            trigger_activate_entity(pEntity, 1);
            // Handle grab/push mechanics for specific game modes
        } else if (state == 1) {
            // Already opening: keep flags
            if ((pData[3] & 0x5400) == 0) {
                trigger_close(pEntity);
            }
        } else if (state == 2) {
            // Open: reset auto-close or keep open
            if ((pData[3] & 0x400) == 0) {
                if ((pData[3] & 0x4000) == 0) {
                    trigger_close(pEntity);
                }
            } else {
                pData[8/4] = 0;
            }
        } else {
            // Closing or other: re-activate
            trigger_activate_entity(pEntity, 1);
        }
        pData[0x24/4] = g_frameCounter;
        pData[3] &= ~0x40000000u;
    }

    // --- State update ---
    unsigned int state = pData[4];
    if (state == 0) {
        // Closed: ensure portal is blocked
        pData[8/4] = 0;
        pData[0x0E] = 0;
        if (pData[0x16] != 0xFFFFFFFF && scene_portal_is_open(pData[0x16])) {
            scene_portal_block(pData[0x16]);
        }
    } else if (state == 1) {
        // Opening: integrate speed
        float target = (float)pData[0x1D];
        if ((float)pData[9] < target) {
            float speed = trigger_compute_open_speed((float)pData[9], (float)pData[0x0C], pEntity);
            *(float*)&pData[0x0E] = speed;
            *(float*)&pData[9] += speed * dt;
        }
        // Check arrival at target
        if ((float)pData[9] >= target - TRIGGER_SNAP_THRESHOLD) {
            // Arrived: call open callback
            if (*(void**)(pEntity + 400) != NULL) {
                (*(TriggerCallback)(pEntity + 400))(pEntity, 0);
            }
            *(float*)&pData[9] = target;
            pData[8/4] = 0;
            *(float*)&pData[0x0E] = 0.0f;
            if ((pData[3] & 0x100000) != 0) {
                trigger_play_state_sound(pData, 0);
            }
            pData[4] = 2;  // -> open state
            pData[3] &= ~0x4000000u;
        }
        *(unsigned int*)(pEntity + 0x18) |= 0x42;
    } else if (state == 2) {
        // Open: auto-close timer
        if ((pData[3] & 0x44800) == 0) {
            pData[8/4] += g_soundEnabled;
            *(unsigned int*)(pEntity + 0x18) |= 0x40;
        }
        if ((pData[3] & 0x44020) == 0) {
            float closeDelay = (float)pData[7] * g_frameDivisor;
            float elapsed = (float)((int)pData[8/4]);
            if (elapsed > closeDelay) {
                trigger_close(pEntity);
            }
        }
    } else if (state == 3) {
        // Closing: integrate speed downward
        float target = (float)pData[0x1D];
        if ((float)pData[9] > target) {
            float speed = trigger_compute_open_speed((float)pData[9], (float)pData[0x0C], pEntity);
            *(float*)&pData[0x0E] = speed;
            *(float*)&pData[9] -= speed * dt;
        }
        // Check arrival at closed
        if ((float)pData[9] <= target + TRIGGER_SNAP_THRESHOLD) {
            *(float*)&pData[9] = target;
            if (*(void**)(pEntity + 0x194) != NULL) {
                (*(TriggerCallback)(pEntity + 0x194))(pEntity, 0);
            }
            pData[8/4] = 0;
            if ((pData[3] & 0x300000) != 0) {
                trigger_play_state_sound(pData, 1);
            }
            pData[4] = 0;  // -> closed state
            pData[3] &= ~0x4000000u;
            *(unsigned int*)(pEntity + 0x28) &= ~0x4000000u;
            *(float*)&pData[0x0E] = 0.0f;
        }
        *(unsigned int*)(pEntity + 0x18) |= 0x42;
    }

    // --- Speed decay (friction for revolving doors) ---
    if (0.0f < fabsf((float)pData[10])) {
        // Unblock portal while moving
        if (pData[0x16] != 0xFFFFFFFF && !scene_portal_is_open(pData[0x16])) {
            scene_portal_unblock(pData[0x16]);
        }
        // Apply friction decay
        if ((pData[3] & 0x2000000) == 0) {
            float vel = (float)pData[10];
            float friction = TRIGGER_FRICTION * g_fixedDeltaTime * g_frameDivisorInv;
            if (friction <= fabsf(vel)) {
                *(float*)&pData[10] -= friction * signf(vel);
            } else {
                *(float*)&pData[10] = 0.0f;
            }
        }
    }

    // --- Portal zone flag management ---
    if (pPortal != NULL) {
        if ((float)pData[9] <= TRIGGER_ZONE_THRESHOLD) {
            // Closed: clear zone visibility flags
            *(unsigned int*)(g_zoneBase + pPortal[1] * 0x20 + 4) &= ~0x800000u;
            *(unsigned int*)(g_zoneBase + pPortal[2] * 0x20 + 4) &= ~0x800000u;
            pPortal[0] |= 0x80000;
        } else {
            // Open: set zone visibility flags
            *(unsigned int*)(g_zoneBase + pPortal[1] * 0x20 + 4) |= 0x800000;
            *(unsigned int*)(g_zoneBase + pPortal[2] * 0x20 + 4) |= 0x800000;
            pPortal[0] &= ~0x80000u;
        }
    }

    trigger_update_special_sound();
    trigger_update_collision_flag(pEntity);
}

// ============================================================================
// Address: 0x800860c8
// Revolving door physics. Computes angular push from all players near the door.
// Uses sin/cos of current rotation angle to determine push direction.
// If 1 player in contact: direct push proportional to perpendicular distance.
// If 0 players: spring-damper decay (angular friction with quadratic damping).
// Clamps rotation to valid range.
// ============================================================================
void trigger_revolving_door_physics(void) {
    int pEntity = trigger_get_entity_internal_alt2();
    int* pData = (int*)*(int*)(pEntity + 0x7C);

    float doorExtent = *(float*)(pData + 0x4C/4);
    float currentAngle = DEGREES_TO_RAD *
        (*(float*)(pData + 0x2C/4) * *(float*)(pData + 0x24/4) + *(float*)(pEntity + 0x70))
        / 360.0f;

    float sinAngle = sinf(currentAngle - TRIGGER_DOOR_ANGLE_OFFSET);
    float cosAngle = cosf(currentAngle);
    float doorY = *(float*)(pEntity + 0x68) + *(float*)(pEntity + 0x1AC);

    int pushCount = 0;
    float totalPush = 0.0f;

    for (int i = 0; i < g_playerCount; i++) {
        int* pPlayerSlot = g_playerSlots[i];
        if (pPlayerSlot == NULL) continue;
        int pPlayer = *pPlayerSlot;

        // Height overlap check
        float playerY = *(float*)(pPlayer + 0x68) + *(float*)(pPlayer + 0x1AC);
        bool yOverlap = (playerY - *(float*)(pPlayer + 0x1C4) <= doorY + *(float*)(pEntity + 0x1C4))
                     && (doorY - *(float*)(pEntity + 0x1C4) <= playerY + *(float*)(pPlayer + 0x1C4));

        // Compute perpendicular distance to door plane
        float dz = *(float*)(pEntity + 0x6C) - *(float*)(pPlayer + 0x6C);
        float dx = *(float*)(pEntity + 100) - *(float*)(pPlayer + 100);
        float perpDist = dx * cosAngle + dz * sinAngle;

        if (perpDist < 0.0f && perpDist > -fabsf(doorExtent) && yOverlap) {
            float lateralDist = dz * cosAngle - dx * sinAngle;
            float pushForce = (TRIGGER_DOOR_WIDTH - fabsf(lateralDist)) * fabsf(perpDist);
            float maxPush = TRIGGER_FRICTION;
            if (pushForce < maxPush) maxPush = pushForce;

            if (maxPush > 0.0f) {
                pushCount++;
                *(float*)(pEntity + 0xE4) = 0.0f;
                totalPush += maxPush * signf(perpDist) * signf(lateralDist);
            }
        }
    }

    if (pushCount == 1) {
        *(float*)(pData + 0x24/4) += totalPush;
    } else if (pushCount == 0) {
        // Spring-damper decay
        float angVel = TRIGGER_SPRING * *(float*)(pData + 0x24/4);
        float force = angVel * angVel * -signf(angVel) - TRIGGER_DAMPING * *(float*)(pEntity + 0xE4);
        *(float*)(pEntity + 0xE4) += (1.0f / g_frameDivisor) * force * g_fixedDeltaTime;
        *(float*)(pData + 0x24/4) += (1.0f / g_frameDivisor) * *(float*)(pEntity + 0xE4) * g_fixedDeltaTime;
        *(float*)(pData + 0x5C/4) = force;
    }

    // Clamp angle
    if (*(float*)(pData + 0x24/4) < TRIGGER_MIN_ANGLE) {
        *(float*)(pData + 0x24/4) = TRIGGER_MIN_ANGLE;
    }
    if (*(float*)(pData + 0x24/4) > 1.0f) {
        *(float*)(pData + 0x24/4) = 1.0f;
    }

    *(unsigned int*)(pEntity + 0x18) |= 0x42;
}

// ============================================================================
// Address: 0x800863ec
// Updates 3D positional sounds for special trigger animation types.
// Type 0x12 (elevator door): computes animation frame from open amount,
//   updates 3D sound 0x353 with volume based on movement speed.
// Type 0x11 (entity 0x293 = rotating fan/blade): interpolates angular velocity,
//   plays 3D sound 0x20C. Plays one-shot 0x354 on state change edge.
// ============================================================================
void trigger_update_special_sound(void) {
    int pEntity = trigger_get_entity_internal_alt2();
    int* pData = (int*)*(int*)(pEntity + 0x7C);
    int animType = pData[2];

    if (animType == 0x12) {
        // Elevator door type
        int pRenderInst = *(int*)(pEntity + 4);
        int pAnimData = *(int*)(g_animTable + *(int*)(pRenderInst + 0xD0) * 4);
        float frameDt = g_fixedDeltaTime * (1.0f / g_frameDivisor);

        // Store previous frame and compute new
        *(float*)(pRenderInst + 0xD8) = *(float*)(pRenderInst + 0xD4);
        *(float*)(pRenderInst + 0xD4) = *(float*)(pData + 0x24/4) * *(float*)(pAnimData + 0x18);

        int openState = pData[4];
        if (openState == 1) {
            // Opening: check if near end
            float maxFrame = *(float*)(pAnimData + 0x18);
            if (*(float*)(pRenderInst + 0xD4) <= maxFrame - frameDt) {
                *(float*)(pRenderInst + 0x100) = 1.0f;
            } else {
                *(float*)(pRenderInst + 0xD4) = maxFrame;
                *(float*)(pRenderInst + 0x100) = 0.0f;
            }
        } else if (openState == 3) {
            // Closing: check if near start
            if (frameDt <= *(float*)(pRenderInst + 0xD4)) {
                *(float*)(pRenderInst + 0x100) = TRIGGER_MIN_ANGLE;
            } else {
                *(float*)(pRenderInst + 0xD4) = 0.0f;
                *(float*)(pRenderInst + 0x100) = 0.0f;
            }
        } else {
            *(float*)(pRenderInst + 0x100) = 0.0f;
        }

        // 3D sound for entity type 0x55E
        if (*(short*)(pEntity + 0x94) == 0x55E) {
            float volume = TRIGGER_MIN_ANGLE;
            bool isMoving = false;
            if (openState == 1) {
                isMoving = TRIGGER_VOLUME_THRESHOLD < *(float*)(pRenderInst + 0xD4);
            } else if (openState == 3) {
                isMoving = *(float*)(pRenderInst + 0xD4) < TRIGGER_ZONE_THRESHOLD;
            }
            if (isMoving) volume = TRIGGER_MOVING_VOLUME;

            float* pWorldPos = entity_get_world_position(pEntity);
            sound_update_3d_source(volume, TRIGGER_SOUND_RADIUS, 1.0f,
                                   pEntity + 0x22C, pWorldPos, 0x353, 0);
        }
    } else if (animType > 0x10 && animType < 0x12 &&
               *(short*)(pEntity + 0x94) == 0x293 && g_soundEnabled != 0) {
        // Rotating entity (fan/blade) type 0x293
        int openState = pData[4];
        float targetVel = 0.0f;
        if (openState == 1) targetVel = TRIGGER_ZONE_THRESHOLD;
        else if (openState == 3) targetVel = 1.0f;

        float currentVel;
        math_lerp_toward(targetVel, TRIGGER_ACCEL_FACTOR, &*(float*)(pData + 0x5C/4));
        currentVel = *(float*)(pData + 0x5C/4);
        float volume = 0.5f * sinf(currentVel);

        float* pWorldPos = entity_get_world_position(pEntity);
        sound_update_3d_source(volume, TRIGGER_SOUND_RADIUS,
                               TRIGGER_ZONE_THRESHOLD + currentVel,
                               pEntity + 0x228, pWorldPos, 0x20C, 1);
    }

    // One-shot sound on state change edge
    static bool wasMoving = false;
    bool nowMoving = (isMoving != 0);
    if (!wasMoving && nowMoving) {
        mover_play_sound_at_entity(pEntity, 0x354);
        *(unsigned int*)(pData + 3) |= 0x400000;
    } else if (!nowMoving) {
        *(unsigned int*)(pData + 3) &= ~0x400000u;
    }
}

// ============================================================================
// Address: 0x800866ac
// Sets trigger flags: ORs param_2 bits into data flags (offset 0xC).
// ============================================================================
void trigger_set_flags(int pEntity, unsigned int flags) {
    if (pEntity == 0) return;
    int* pData = (int*)*(int*)(pEntity + 0x7C);
    if (pData == NULL) return;
    *(unsigned int*)(pData + 3) |= flags;
}

// ============================================================================
// Address: 0x800866d0
// Clears trigger flags: ANDs ~param_2 into data flags (offset 0xC).
// ============================================================================
void trigger_clear_flags(int pEntity, unsigned int flags) {
    if (pEntity == 0) return;
    int* pData = (int*)*(int*)(pEntity + 0x7C);
    if (pData == NULL) return;
    *(unsigned int*)(pData + 3) &= ~flags;
}

// ============================================================================
// Address: 0x800866f4
// Opens a trigger entity. Sets open target (param_1), transitions to state 1
// (opening) if not already opening/open. Plays sound via callback (0x188) or
// positional sound. Unblocks linked portal.
// ============================================================================
void trigger_open(float targetAmount, int pEntity, int playSound) {
    if (pEntity == 0) return;
    int* pData = (int*)*(int*)(pEntity + 0x7C);
    if (pData == NULL) return;
    if ((*(unsigned int*)(pData + 3) & 0x1000000) != 0) return;  // Disabled

    *(float*)(pData + 0x74/4) = targetAmount;

    // Only transition if not already opening or open
    if (pData[4] == 1 || pData[4] == 2) return;

    pData[4] = 1;  // State = opening
    *(unsigned int*)(pEntity + 0x18) |= 0x40;

    if (playSound != 0) {
        if (*(void**)(pEntity + 0x188) != NULL) {
            // Custom sound callback
            (*(TriggerCallback)(pEntity + 0x188))(pEntity, 0);
        } else {
            if ((*(unsigned int*)(pData + 3) & 0x100000) != 0) {
                // Looping activation sound
                trigger_play_activation_sound();
            } else {
                // One-shot sound
                int soundId;
                if (*(int*)(pData + 0x6C/4) < 0 ||
                    (*(unsigned int*)(pData + 3) & 0x4000000) == 0) {
                    soundId = *(int*)(pData + 0x60/4);
                } else {
                    soundId = *(int*)(pData + 0x6C/4);
                }
                if (soundId >= 0) {
                    mover_play_sound_at_entity(pEntity, soundId);
                }
            }
        }
    }

    // Unblock linked portal
    if (*(int*)(pData + 0x58/4) != -1) {
        scene_portal_unblock(*(int*)(pData + 0x58/4));
    }
}
