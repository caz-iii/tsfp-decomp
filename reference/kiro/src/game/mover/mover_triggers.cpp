// Address: 0x80083ac0 - 0x8008421c | Mover Triggers & Animation | Date: 2026-08-11
// Contains trigger detection, auto-return logic, delay timers, rotating mover
// animation, sound zone assignment, and trigger overlap queries.

#include "mover.h"
#include "entity.h"
#include "sound.h"
#include "level.h"
#include "soundpad.h"
#include "math_util.h"

// ============================================================================
// Address: 0x80083ac0
// Finds which sound zone the mover entity belongs to by iterating level info
// entries. Used by soundpad_assign_ambient_sounds to determine ambient sound
// for the mover's current zone.
// ============================================================================
void mover_find_zone_for_sound(void) {
    int pEntity = mover_get_entity_internal_2();
    int pLevelInfo = level_get_info_cached(g_gameState);

    if (pLevelInfo == 0) goto done;

    int entryCount = *(int*)(pLevelInfo + 0xA8);
    int entryBase = *(int*)(pLevelInfo + 0xA4);
    int entryOffset = 0;

    for (int i = 0; i < entryCount; i++) {
        int pEntry = entryBase + entryOffset;
        int zoneRef = *(int*)(pEntry + 0x9C);

        if (zoneRef >= 0) {
            int localBuf[10];
            int entityCount = soundpad_get_entity_data(zoneRef, localBuf);
            int pZone = soundpad_resolve_entity_to_zone(pEntity);
            int entityZoneId = soundpad_get_entity_zone_id(pZone);

            if (*(int*)(pEntry + 0x9C) == entityZoneId) {
                int bufOffset = 0;
                for (int j = 0; j < entityCount; j++) {
                    int foundId = soundpad_get_entity_id_from_zone(
                        *(int*)(localBuf[0] + bufOffset));
                    if (pEntity == foundId) goto done;
                    bufOffset += 4;
                }
            }
        }
        entryOffset += 0xAC;
    }

done:
    // Epilogue (restore state)
    return;
}

// ============================================================================
// Address: 0x80083b9c
// Per-frame mover rotation animation. Two modes:
//   Flag 0x10: Axis translation (bones slide along X-axis, symmetrically)
//   Flag 0x40: Bone-pivot rotation (rotates around a vertex pivot point)
// Sets flag 0x200000 on entity to trigger bone matrix recalculation.
// ============================================================================
void mover_animate_rotation_tick(void) {
    int pEntity = mover_get_entity_internal_2();
    int pMoverData = *(int*)(pEntity + 0x7C);

    *(unsigned int*)(pEntity + 0x18) |= 2;

    if ((*(unsigned int*)(pMoverData + 4) & 0x10) == 0) {
        // Mode: bone-pivot rotation (flag 0x40)
        if ((*(unsigned int*)(pMoverData + 4) & 0x40) != 0) {
            int pBoneMatrices = *(int*)(*(int*)(pEntity + 4) + 0x10);

            // Reset bone matrices to identity
            mtx_identity(pBoneMatrices);
            mtx_identity(pBoneMatrices + 0x40);
            mtx_identity(pBoneMatrices + 0x80);
            mtx_identity(pBoneMatrices + 0xC0);

            // Get pivot vertex from mesh data
            int* pMeshData = *(int**)(*(int*)(pEntity + 4) + 4);
            int vertexBase = pMeshData[pMeshData[0] * -0x30 +
                ((*(unsigned int*)(pMoverData + 0x9C) >> 16) * 0x30) + 0x1B];
            float* pPivot = (float*)(vertexBase +
                (*(unsigned int*)(pMoverData + 0x9C) & 0xFFFF) * 0x18);

            // Translate to pivot, rotate, translate back
            mtx_translate(pPivot[0], pPivot[1], pPivot[2]);
            float rotAngle = (MOVER_ANGLE_SCALE * *(float*)(pMoverData + 0xAC))
                             / MOVER_ANGLE_DIV;
            float rotMatrix[22];
            mtx_build_rotation_y(rotAngle, rotMatrix);

            float* pTargetBone = (float*)(pBoneMatrices +
                *(int*)(pMoverData + 0x98) * 0x40);
            mtx_multiply(rotMatrix, pTargetBone, pTargetBone);

            mtx_translate(-pPivot[0], -pPivot[1], -pPivot[2]);
            mtx_multiply(rotMatrix, pTargetBone, pTargetBone);
        }
    } else {
        // Mode: axis translation (flag 0x10)
        int boneA = *(int*)(pMoverData + 0x98);
        int boneB = *(int*)(pMoverData + 0xA4);
        int pBoneMatrices = *(int*)(*(int*)(pEntity + 4) + 0x10);

        mtx_identity(pBoneMatrices);
        mtx_identity(pBoneMatrices + boneA * 0x40);
        mtx_identity(pBoneMatrices + boneB * 0x40);

        float offset = *(float*)(pMoverData + 0xAC);
        mtx_translate(offset, 0.0f, 0.0f);
        mtx_translate(-offset, 0.0f, 0.0f);

        if (MOVER_SLIDE_THRESHOLD < offset) {
            mtx_translate(offset - MOVER_SLIDE_THRESHOLD, 0.0f, 0.0f);
            mtx_translate(-(offset - MOVER_SLIDE_THRESHOLD), 0.0f, 0.0f);
        }
    }

    *(unsigned int*)(pEntity + 0x28) |= 0x200000;
}

// ============================================================================
// Address: 0x80083de8
// Handles auto-trigger delay for movers. Two-phase timer:
//   Phase 1 (flag 0x20 clear): Accumulates timer until threshold reached,
//     then sets flag 0x20.
//   Phase 2 (flag 0x20 set): Decrements wait timer. When expired, clears
//     flag 0x20, resets timers, and sends "SLDR" event to trigger system.
// ============================================================================
void mover_handle_auto_trigger(int pEntity) {
    int pMoverData = *(int*)(pEntity + 0x7C);
    float dt = g_fixedDeltaTime * (1.0f / (float)g_frameTimeDivisor);

    if ((*(unsigned int*)(pMoverData + 4) & 0x20) == 0) {
        // Phase 1: accumulate delay timer
        if (MOVER_TRIGGER_THRESHOLD <= *(float*)(pMoverData + 0xAC)) {
            *(unsigned int*)(pMoverData + 4) |= 0x20;
        } else {
            *(float*)(pMoverData + 0xAC) += dt;
        }
    } else {
        // Phase 2: countdown
        *(float*)(pMoverData + 0xA8) -= dt;

        if (0.0f < *(float*)(pMoverData + 0xA8) ||
            *(float*)(pMoverData + 0xAC) <= 0.0f) {

            if (*(float*)(pMoverData + 0xAC) <= 0.0f) {
                // Timer fully expired: reset and fire event
                *(float*)(pMoverData + 0xAC) = 0.0f;
                *(float*)(pMoverData + 0xA8) = 0.0f;
                *(unsigned int*)(pMoverData + 4) &= ~0x20u;

                int* pTrigger = event_find_by_fourcc(0x534C4452); // "SLDR"
                event_invoke(pTrigger);
            }
        } else {
            // Continue decrement
            *(float*)(pMoverData + 0xAC) -= dt;
        }
    }
}

// ============================================================================
// Address: 0x80083f24
// Starts mover movement after a delay. Two branches:
//   If flag 0x20 NOT set: decrements delay timer, clamps to 0.
//   If flag 0x20 set: decrements delay, clears flag, finds linked trigger
//   entity by ID and activates it.
// ============================================================================
void mover_start_with_delay(int pEntity) {
    int pMoverData = *(int*)(pEntity + 0x7C);
    float dt = g_fixedDeltaTime * (1.0f / (float)g_frameTimeDivisor);

    if ((*(unsigned int*)(pMoverData + 4) & 0x20) == 0) {
        // Decrement delay timer
        *(float*)(pMoverData + 0xAC) -= dt;
        if (*(float*)(pMoverData + 0xAC) <= 0.0f) {
            *(float*)(pMoverData + 0xAC) = 0.0f;
            *(float*)(pMoverData + 0xA8) = 0.0f;
        }
    } else {
        // Delay complete: clear flag and activate trigger
        *(float*)(pMoverData + 0xAC) -= dt;
        *(unsigned int*)(pMoverData + 4) &= ~0x20u;

        int pTriggerEntity = trigger_find_by_id(*(int*)(pMoverData + 0x88));
        trigger_activate(pTriggerEntity);
    }
}

// ============================================================================
// Address: 0x80083ff8
// Handles special return behavior for movers. Decrements delay timer by
// fixed dt. While timer > threshold, keeps render material bit 1 active.
// When timer expires, clears flag 0x20.
// ============================================================================
void mover_handle_special_return(int pEntity) {
    int pMoverData = *(int*)(pEntity + 0x7C);

    if (MOVER_SPECIAL_THRESHOLD < *(float*)(pMoverData + 0xAC)) {
        *(float*)(pMoverData + 0xAC) -= g_fixedDeltaTime;
        *(unsigned int*)(*(int*)(pEntity + 4) + 0x30) |= 1;
    } else {
        *(unsigned int*)(pMoverData + 4) &= ~0x20u;
    }
}

// ============================================================================
// Address: 0x8008403c
// Sets the mover's auto-trigger delay timer to a constant value.
// ============================================================================
void mover_set_trigger_delay(int pEntity) {
    *(float*)(*(int*)(pEntity + 0x7C) + 0xA8) = MOVER_DEFAULT_DELAY;
}

// ============================================================================
// Address: 0x8008404c
// Initiates auto-return for a mover. If the delay timer (0xAC) has elapsed:
// sets delay to default, finds linked trigger entity, and sends start event.
// Then calls mover_handle_auto_trigger for ongoing timer management.
// ============================================================================
void mover_trigger_auto_return(int pEntity, int triggerId) {
    if (*(float*)(*(int*)(pEntity + 0x7C) + 0xAC) <= 0.0f) {
        *(float*)(*(int*)(pEntity + 0x7C) + 0xA8) = MOVER_DEFAULT_DELAY;
        int pTrigger = trigger_find_by_id(triggerId);
        trigger_start(pTrigger, 0);
    }
    mover_handle_auto_trigger(pEntity);
}

// ============================================================================
// Address: 0x800840ac
// Checks trigger conditions at both mover endpoints per frame.
//   - Bottom trigger (offset 0x78): checked when mover state == 0 (idle)
//   - Top trigger (offset 0x88): checked when mover state == 3 (arrived)
// If trigger is active and mover is idle at that end, calls
// mover_trigger_auto_return to initiate movement.
// ============================================================================
void mover_check_triggers(void) {
    int pEntity;
    int linkedEntity;
    // Get entity context (returns entity + linked flag)
    pEntity = mover_get_entity_internal_alt();
    int pMoverData = *(int*)(pEntity + 0x7C);

    // Check bottom trigger
    int bottomResult;
    if (linkedEntity == 0) {
        bottomResult = 0;
    } else {
        trigger_find_by_id(*(int*)(pMoverData + 0x78));
        bottomResult = mover_is_trigger_active();
    }
    if (bottomResult == 0 && *(int*)(pMoverData + 8) == 0) {
        mover_trigger_auto_return(pEntity, *(int*)(pMoverData + 0x78));
    }

    // Check top trigger
    int topResult;
    if (linkedEntity == 0) {
        topResult = 0;
    } else {
        trigger_find_by_id(*(int*)(pMoverData + 0x88));
        topResult = mover_is_trigger_active();
    }
    if (topResult == 0 && *(int*)(pMoverData + 8) == 3) {
        mover_trigger_auto_return(pEntity, *(int*)(pMoverData + 0x88));
    }
}

// ============================================================================
// Address: 0x80084160
// Plays a 3D positioned sound at the entity's world position.
// Volume depends on entity type (0x55E gets a louder base volume).
// Sets radius, attenuation, and passes a custom volume from mover data.
// Called by many mover/trigger functions for activation/movement sounds.
// ============================================================================
void mover_play_sound_at_entity(int pEntity, int soundId) {
    int pMoverData = *(int*)(pEntity + 0x7C);

    float baseVolume = MOVER_SOUND_VOL_DEFAULT;
    if (*(short*)(pEntity + 0x94) == 0x55E) {
        baseVolume = MOVER_SOUND_VOL_LOUD;
    }

    float* pWorldPos = entity_get_world_position(pEntity);
    float customVol = *(float*)(pMoverData + 0x70);

    SoundParams params;
    sound_init_play_params(&params);
    params.flags3d = 1;
    params.volume = customVol;
    params.radius = baseVolume;
    params.posX = pWorldPos[0];
    params.posY = pWorldPos[1];
    params.posZ = pWorldPos[2];
    params.falloff = MOVER_SOUND_FALLOFF;
    params.soundId = soundId;
    sound_play_request(&params);
}

// ============================================================================
// Address: 0x8008421c
// Checks if an entity overlaps a trigger volume. Queries the collision system
// at the entity's trigger data (offset 0x304) with range 0x200 against the
// trigger ID from mover data (offset 0x78).
// Returns 1 if overlap found, 0 otherwise.
// ============================================================================
int mover_check_trigger_overlap(int pQueryEntity, int pMoverEntity) {
    int triggerId = *(int*)(*(int*)(pMoverEntity + 0x7C) + 0x78);

    if (triggerId < 0) return 0;

    int* pResult = collision_query_trigger(pQueryEntity + 0x304, 0x200, triggerId);
    if (pResult == NULL) return 0;

    return 1;
}
