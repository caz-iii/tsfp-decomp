// Address: 0x80084268 - 0x80085568 | Trigger System | Date: 2026-08-11
// Trigger entity system: doors, switches, interactive objects. Handles spawn,
// per-frame activation checks, animation (18 types), sound, locked-door
// messages, and portal blocking/unblocking.

#include "trigger.h"
#include "entity.h"
#include "sound.h"
#include "mover.h"
#include "soundpad.h"
#include "scene.h"
#include "math_util.h"

// Trigger data pool: each trigger is 0x98 bytes, pool is 0x26A0 total
// Max triggers = 0x26A0 / 0x98 = 64
static void* g_triggerPool;       // DAT_80611ca0
static int   g_triggerCount;      // DAT_80611ca4

// ============================================================================
// Address: 0x80084268
// Resets trigger system: zeroes count and clears pool memory.
// ============================================================================
void trigger_system_reset(void) {
    g_triggerCount = 0;
    memset(g_triggerPool, 0, 0x26A0);
}

// ============================================================================
// Address: 0x8008429c
// Allocates trigger pool memory and resets.
// ============================================================================
void trigger_system_alloc(void) {
    g_triggerPool = mem_alloc(0x26A0, 1);
    trigger_system_reset();
}

// ============================================================================
// Address: 0x800842cc
// Registers trigger event handlers for entity type 4:
//   Slot 0 (spawn)   = trigger_on_spawn
//   Slot 2 (update)  = trigger_animate_update
//   Slot 3 (destroy) = trigger_on_destroy
// ============================================================================
void trigger_register_event_handlers(void) {
    event_set_handler(4, 0, trigger_on_spawn);
    event_set_handler(4, 2, trigger_animate_update);
    event_set_handler(4, 3, trigger_on_destroy);
}

// ============================================================================
// Address: 0x80084324
// Spawns a trigger entity from level definition data. Converts frame-rate
// dependent speed fields, then calls entity_spawn_by_type.
// ============================================================================
void trigger_spawn_entity(/* definition params */) {
    int pOwner = trigger_get_owner_internal();

    // Zero-init local spawn params (0x6C bytes)
    SpawnParams params;
    memset(&params, 0, sizeof(params));

    // Convert speed from per-second to per-frame
    params.speedPerFrame = param_angle *
        (float)((double)(0x43300000 | (g_frameCounter ^ 0x80000000)) - FRAME_DOUBLE_BIAS);
    params.travelDistance = (float)((double)(0x43300000 | (param_15 ^ 0x80000000)) - FRAME_DOUBLE_BIAS);

    // Copy position, orientation, and other fields from definition
    params.height = param_height;
    params.radius = param_radius;
    // ... additional field copies ...

    int pEntity = entity_spawn_by_type(pOwner, &params, 0);
    entity_link_to_room(pOwner, pEntity);
}

// ============================================================================
// Address: 0x80084460
// MASSIVE trigger on-spawn handler. Called when a trigger entity is created.
// Allocates a 0x98-byte data slot from the trigger pool, copies all definition
// fields (flags, speeds, sounds, portals, zones), configures:
//   - Portal blocking (blocks portal if trigger starts closed)
//   - Zone references with portal flag 0x1000000
//   - Initial state: if flag 0x1 (starts open), sets state=2 and activates
//   - Register for per-frame update if flags don't include 0xC00
//   - Sound zone lookup via soundpad_find_zone_by_id
// ============================================================================
void trigger_on_spawn(void) {
    int pEntity;
    int* pDef;
    // Get entity and definition from event context
    // ... (context retrieval) ...

    // Allocate data slot from pool
    int slotOffset = g_triggerCount * 0x98;
    g_triggerCount++;
    int* pData = (int*)((char*)g_triggerPool + slotOffset);
    *(int*)(pEntity + 0x7C) = (int)pData;
    memset(pData, 0, 0x98);

    // Copy definition fields
    pData[5] = pDef;                    // Back-reference to definition
    pData[0] = *(int*)(pEntity + 0x2C); // Entity ID
    pData[1] = pDef[0];                 // Definition ID
    pData[2] = pDef[3];                 // Type
    pData[4] = 0;                       // State = idle

    // Sound IDs
    pData[0x0B] = pDef[6];             // Activation sound
    pData[0x16] = pDef[7];             // Portal ID

    // Copy optional sound overrides
    if (pDef[0x0B] != 0) pData[0x18] = pDef[0x0B];
    if (pDef[0x0C] != 0) pData[0x19] = pDef[0x0C];
    if (pDef[0x0D] != 0) pData[0x1A] = pDef[0x0D];
    if (pDef[0x0E] != 0) pData[0x1B] = pDef[0x0E];

    // Flags and speed params
    pData[3] = pDef[8];                // Flags
    pData[7] = (pDef[10] == 0.0f) ? TRIGGER_DEFAULT_SPEED : pDef[10];
    pData[0x0C] = pDef[9] * g_frameDivisor;   // Open speed (frame-adjusted)
    pData[0x0D] = pDef[0x18] * g_frameDivisor; // Close speed (frame-adjusted)

    // Portal/zone setup
    pData[0x11] = -1;  // Portal index (unassigned)
    unsigned int zoneId = pDef[5];
    pData[0x12] = (zoneId >= 0) ? soundpad_find_zone_by_id(zoneId) : 0xFFFFFFFF;

    // Find portal that connects to this trigger's zone
    for (int i = 0; i < g_portalCount; i++) {
        unsigned int* pPortal = (unsigned int*)(g_portalBase + i * 0x14);
        if ((pPortal[0] & 0x10000) != 0 &&
            (pPortal[1] == pData[0x12] || pPortal[2] == pData[0x12]) &&
            (pPortal[0] & 4) != 0) {
            pData[0x11] = i;
            // Set flag 0x1000000 on both connected zones
            *(unsigned int*)(g_zoneBase + pPortal[1] * 0x20 + 4) |= 0x1000000;
            *(unsigned int*)(g_zoneBase + pPortal[2] * 0x20 + 4) |= 0x1000000;
        }
    }

    // Block portal if trigger starts closed
    if (pData[0x16] != -1 && (pData[3] & 1) == 0) {
        scene_portal_block(pData[0x16]);
    }

    // If starts open (flag 0x1): set to open state
    if ((pData[3] & 1) != 0) {
        pData[4] = 2;  // State = fully open
        pData[9] = 1.0f;
        trigger_activate_entity(pEntity, 1);
        if (pData[0x16] != -1) {
            scene_portal_unblock(pData[0x16]);
        }
    }

    // If flag 0x800: also start in open state
    if ((pData[3] & 0x800) != 0) {
        pData[4] = 2;
    }

    *(unsigned int*)(pEntity + 0x18) |= 0x42;
}

// ============================================================================
// Address: 0x80084848
// Checks if a trigger should activate this frame. Validates:
//   - Player entity exists (offset 0x150)
//   - Flag 0x80 (auto-activate) or flag 0x200 (requires interaction + target)
//   - Flag 0x2 (interactable) is set
//   - No debounce (offset 0x10 == 0 OR overlap check fails)
// If valid, performs LOS check:
//   Type 4: LOS check only
//   Type 8: LOS check with boolean result
// ============================================================================
void trigger_check_activation(void) {
    int pEntity;
    int pPlayer;
    int* pData;
    // Get entity + player context

    pData = (int*)*(int*)(pEntity + 0x7C);

    // Skip if player doesn't exist, unless auto-activate flags
    if (*(int*)(pPlayer + 0x150) == 0 &&
        (pData[3] & 0x80) == 0 &&
        ((pData[3] & 0x200) == 0 || pData[4] != 0)) {
        return;
    }

    if ((pData[3] & 2) == 0) return;  // Not interactable

    // Check debounce and overlap
    if (pData[4] != 0 || mover_check_trigger_overlap(*(int*)(pPlayer + 0x7C), pEntity) == 0) {
        return;
    }

    // Perform LOS check based on activation type
    unsigned int activationType = pData[3] & 0xC;
    if (activationType == 8) {
        bool result = entity_los_check(pEntity, (float*)(pPlayer + 100));
        // countLeadingZeros used for bool->int conversion
    } else if (activationType == 4) {
        entity_los_check(pEntity, (float*)(pPlayer + 100));
    }
}

// ============================================================================
// Address: 0x80084968
// Returns 1 if entity was activated on the current or previous frame.
// Used for debouncing to prevent re-triggering within 1 frame.
// ============================================================================
int trigger_is_recently_activated(int pEntity) {
    if (g_frameCounter == 0) return 0;

    int timestamp = *(int*)(pEntity + 0x90);
    if (timestamp == 0) return 0;

    if (timestamp == g_frameCounter ||
        timestamp == g_frameCounter - g_soundEnabled) {
        return 1;
    }
    return 0;
}

// ============================================================================
// Address: 0x800849a8
// MASSIVE trigger per-frame update. Main behavior:
//   1. Calls trigger_check_activation to see if player triggered it
//   2. If NOT activated:
//      - Sets flag 0x40000000 (visible/interactable indicator)
//      - Sets entity visibility flag 0x4000000
//      - Clears interaction flag 0x2 if player is present
//      - Shows "open" message (string 0x18B2) on player HUD
//      - Checks LOS for flag 0x20000 (visibility-gated triggers)
//   3. If activated AND has locked-door behavior:
//      - Computes proximity from frame counter difference
//      - If within activation range:
//        - If no key required: shows "locked" (0x18AF) or calls callback
//        - If key required: shows "need key" (0x18B1)
//        - If key ID > 0: looks up key name (0x18B4) and formats message
//        - Plays activation sound (0x3A6 in level 0xB, 0xD50 otherwise)
//      - Records frame counter for debounce
// ============================================================================
void trigger_tick_update(void) {
    int pEntity;
    int pPlayer;
    int* pData;
    // Get entity + player context

    pData = (int*)*(int*)(pEntity + 0x7C);

    // Skip if flag 0x1000000 (disabled)
    if ((pData[3] & 0x1000000) != 0) return;

    int activationResult;
    if (pPlayer == 0) {
        activationResult = 0;
    } else {
        activationResult = trigger_check_activation();
    }

    if (activationResult == 0) {
        // Not activated: set visual indicators
        pData[3] |= 0x40000000;
        *(unsigned int*)(pEntity + 0x28) |= 0x4000000;

        // Clear interactable if player nearby and has HUD
        if ((pData[3] & 2) != 0 &&
            (*(int*)(pPlayer + 0x150) != 0 || (pData[3] & 0x80) == 0)) {
            pData[3] &= ~2;
            if (pPlayer != 0 && *(int*)(pPlayer + 0x150) != 0) {
                char* msg = get_string_by_id(0x18B2);
                hud_show_message(*(int*)(*(int*)(pPlayer + 0x150) + 4), 2, msg);
            }
        }

        // LOS-gated activation check
        if (pPlayer != 0) {
            if ((pData[3] & 0x20000) != 0) {
                unsigned int canSee = trigger_can_see_player(pEntity);
                if (canSee != 0) {
                    bool los = entity_los_check(pEntity, (float*)(pPlayer + 100));
                    if (los) {
                        pData[3] &= ~0x800000;
                    } else {
                        pData[3] |= 0x800000;
                    }
                }
            }
            // Store entity ID for locked-door lookup
            if (*(int*)(pPlayer + 0x150) == 0) {
                if (*(int*)(pPlayer + 0x20) == 8 &&
                    *(int*)(*(int*)(pPlayer + 0x7C) + 0x44) == 1) {
                    pData[3] |= 0x4000000;
                }
            } else {
                pData[0x14] = *(int*)(pPlayer + 0x2C);
            }
        }
    } else {
        // Activated: handle locked door messages
        if (pData[4] == 0 && *(int*)(pPlayer + 0x150) != 0 &&
            *(int*)(*(int*)(pPlayer + 0x150) + 4) >= 0 &&
            (pData[3] & 0x80000) == 0) {

            // Compute proximity from frame delta
            float proximity = /* frame counter math */ 0.0f;
            float threshold = /* activation range */ 0.0f;

            if (proximity < -threshold) {
                int keyId = pData[0x1E];
                if (keyId < 1) {
                    // No key: show "locked" or call custom callback
                    if ((pData[3] & 0x20) == 0 || (pData[3] & 2) == 0) {
                        if (*(void**)(pEntity + 0x198) == NULL) {
                            char* msg = get_string_by_id(0x18AF);
                            hud_show_message(*(int*)(*(int*)(pPlayer + 0x150) + 4), 2, msg);
                        } else {
                            (*(TriggerCallback)(pEntity + 0x198))(pEntity, pPlayer);
                        }
                    } else {
                        char* msg = get_string_by_id(0x18B1);
                        hud_show_message(*(int*)(*(int*)(pPlayer + 0x150) + 4), 2, msg);
                    }
                } else {
                    // Key required: look up key name and format message
                    int pKeyEntity = entity_find_by_type_id(keyId);
                    int keyNameId;
                    if (pKeyEntity == 0) {
                        keyNameId = keyId;
                    } else {
                        keyNameId = *(int*)(*(int*)(pKeyEntity + 0x7C) + 0x14);
                    }
                    char* keyName = get_string_by_id(g_keyNameTable[keyNameId]);
                    char* format = get_string_by_id(0x18B4);
                    char* formatted = sprintf_game("%s %s", format, keyName);
                    hud_show_message(*(int*)(*(int*)(pPlayer + 0x150) + 4), 2, formatted);
                }

                // Play activation sound
                if (g_gameState == 0xB) {
                    mover_play_sound_at_entity(pEntity, 0x3A6);
                } else {
                    mover_play_sound_at_entity(pEntity, 0xD50);
                }
            }
            pData[0x25] = g_frameCounter;
        }
    }
}

// ============================================================================
// Address: 0x80084d04
// MASSIVE trigger animation update. 18 animation types (switch on state 0-17):
//   0: Translate +X
//   1: Translate -X
//   2: Translate +Y
//   3: Translate -Y
//   4: Directional offset (rotate direction by angle, scale by speed*open)
//   5: Rotation around Y (reversible based on flag 0x800000)
//   6: Symmetric X (both +X and -X bones)
//   7: Symmetric Y (both +Y and -Y bones)
//   8: Asymmetric X (-X main, -X*0.5 secondary)
//   9: Dual-bone pivot rotation (two bones around vertex pivots)
//  10: Dual-bone pivot with 6DOF (full axis rotation)
//  11: Translate + pivot combo
//  12: Rotate around X axis
//  13: Rotate around Y axis
//  14: Rotate around Z axis
//  15: Special callback (level-specific)
//  16: Identity reset (bones to identity)
//  17: Level-specific animation dispatcher
//
// All types compute: openAmount = clamp(offset_0x24 + offset_0x28, 0, 1)
// Then apply: transform = speed * openAmount to bone matrices.
// Sets entity flag 0x200000 for bone matrix recalculation.
// ============================================================================
void trigger_animate_update(void) {
    int pEntity = trigger_get_entity_internal();
    int* pData = (int*)*(int*)(pEntity + 0x7C);

    // Compute clamped open amount
    float openRaw = *(float*)(pData + 0x24/4) + *(float*)(pData + 0x28/4);
    float openAmount = openRaw;
    if (openAmount > 1.0f) openAmount = 1.0f;
    if (openAmount < 0.0f) openAmount = 0.0f;

    float speed = *(float*)(pData + 0x2C/4);
    int pBoneMatrices = *(int*)(*(int*)(pEntity + 4) + 0x10);
    int pMeshData = *(int*)(*(int*)(pEntity + 4) + 4);
    int boneCount = *(int*)(*(int*)(pMeshData) + 4);

    switch (*(int*)(pData + 8/4)) {
    case 0: // Translate +X
        mtx_translate(speed * openAmount, 0.0f, 0.0f);
        mtx_apply_to_bones(pBoneMatrices, boneCount);
        break;
    case 1: // Translate -X
        mtx_translate(-speed * openAmount, 0.0f, 0.0f);
        mtx_apply_to_bones(pBoneMatrices, boneCount);
        break;
    case 2: // Translate +Y
        mtx_translate(0.0f, speed * openAmount, 0.0f);
        mtx_apply_to_bones(pBoneMatrices, boneCount);
        break;
    case 3: // Translate -Y
        mtx_translate(0.0f, -speed * openAmount, 0.0f);
        mtx_apply_to_bones(pBoneMatrices, boneCount);
        break;
    case 4: { // Directional offset
        float angle = (DEGREES_TO_RAD * *(float*)(pData + 0x18/4)) / 360.0f;
        float rotMatrix[22];
        mtx_build_rotation_y(angle, rotMatrix);
        float dir[3] = {0.0f, 0.0f, 1.0f};
        mtx_transform_vector(rotMatrix, dir);
        float scale = speed * openAmount;
        mtx_translate(dir[0] * scale, dir[1] * scale, dir[2] * scale);
        mtx_apply_to_bones(pBoneMatrices, boneCount);
        break;
    }
    case 5: { // Rotation (reversible)
        float rotAmount = speed * openAmount;
        if ((*(unsigned int*)(pData + 0x0C/4) & 0x800000) != 0) {
            rotAmount = -rotAmount;
        }
        float angle = (DEGREES_TO_RAD * rotAmount) / 360.0f;
        float rotMatrix[22];
        mtx_build_rotation_y(angle, rotMatrix);
        mtx_apply_to_bones(pBoneMatrices, boneCount);
        break;
    }
    case 6: // Symmetric X translate
        mtx_identity(pBoneMatrices);
        mtx_translate(-speed * openAmount, 0.0f, 0.0f);
        mtx_translate(speed * openAmount, 0.0f, 0.0f);
        break;
    case 7: // Symmetric Y translate
        mtx_identity(pBoneMatrices);
        mtx_translate(0.0f, speed * openAmount, 0.0f);
        mtx_translate(0.0f, -speed * openAmount, 0.0f);
        break;
    case 8: // Asymmetric X
        mtx_identity(pBoneMatrices);
        mtx_translate(-speed * openAmount, 0.0f, 0.0f);
        mtx_translate(-0.5f * speed * openAmount, 0.0f, 0.0f);
        break;
    case 9: { // Dual-bone pivot rotation
        float* pVerts = (float*)get_mesh_vertex_base(pMeshData);
        float rotAmount = speed * openAmount;
        if ((*(unsigned int*)(pData + 0x0C/4) & 0x800000) != 0) {
            rotAmount = -rotAmount;
        }
        mtx_identity(pBoneMatrices);
        // Bone 1: rotate around pivot[0..2]
        float angle1 = (DEGREES_TO_RAD * rotAmount) / 360.0f;
        mtx_rotate_around_pivot(pVerts, angle1, pBoneMatrices + 0x40);
        // Bone 2: rotate opposite around pivot[6..8]
        float angle2 = (DEGREES_TO_RAD * (-rotAmount)) / 360.0f;
        mtx_rotate_around_pivot(pVerts + 6, angle2, pBoneMatrices + 0x80);
        break;
    }
    case 10: { // Dual-bone 6DOF pivot
        float* pVerts = (float*)get_mesh_vertex_base(pMeshData);
        mtx_identity(pBoneMatrices);
        float angle = (DEGREES_TO_RAD * speed * openAmount) / 360.0f;
        mtx_rotate_6dof_pivot(pVerts, angle, pBoneMatrices + 0x40);
        mtx_rotate_6dof_pivot(pVerts + 6, -angle, pBoneMatrices + 0x80);
        break;
    }
    case 11: // Translate + pivot combo
        mtx_identity(pBoneMatrices);
        mtx_translate(speed * openAmount, 0.0f, 0.0f);
        float comboAngle = (DEGREES_TO_RAD * TRIGGER_COMBO_ANGLE * openAmount) / 360.0f;
        mtx_rotate_around_self(comboAngle, pBoneMatrices + 0x80);
        break;
    case 12: // Rotate around X
        mtx_build_rotation_x((DEGREES_TO_RAD * speed * openAmount) / 360.0f, NULL);
        mtx_apply_to_bones(pBoneMatrices, boneCount);
        break;
    case 13: // Rotate around Y
        mtx_build_rotation_y((DEGREES_TO_RAD * speed * openAmount) / 360.0f, NULL);
        mtx_apply_to_bones(pBoneMatrices, boneCount);
        break;
    case 14: // Rotate around Z
        mtx_build_rotation_z((DEGREES_TO_RAD * speed * openAmount) / 360.0f, NULL);
        mtx_apply_to_bones(pBoneMatrices, boneCount);
        break;
    case 15: // Special callback
        trigger_special_callback();
        break;
    case 16: // Identity reset
        mtx_identity(pBoneMatrices);
        break;
    case 17: // Level-specific
        if (g_gameState == 4) {
            level4_trigger_anim(speed * openAmount, pEntity);
        } else {
            short entityType = *(short*)(pEntity + 0x94);
            if (entityType == 0x238 || entityType == 0x239 || entityType == 0x23A) {
                level_generic_trigger_anim(speed * openAmount, pEntity);
            }
        }
        break;
    }

    *(unsigned int*)(pEntity + 0x28) |= 0x200000;
}

// ============================================================================
// Address: 0x80085568
// Trigger sound handler. Resolves entity from handle, stops existing looping
// sound if any, selects sound ID (flag 0x200000 uses alternate), and plays
// via mover_play_sound_at_entity.
// ============================================================================
void trigger_play_activation_sound(void) {
    int* pData = (int*)trigger_get_data_internal();

    // Resolve entity from handle
    int entityHandle = pData[0];
    int pEntity = 0;
    if (entityHandle != 0) {
        pEntity = g_entityPool + (entityHandle >> 22) * 0x2E8;
        if (*(unsigned int*)(pEntity + 0x2C) != entityHandle) {
            pEntity = 0;
            pData[0] = 0;
        }
    }

    // Get sound ID (use alternate if flag 0x200000 set)
    unsigned int soundId = pData[0x18];
    if (pData[0x21] != -1) {
        sound_stop_handle();
        pData[0x21] = -1;
    }
    if ((pData[3] & 0x200000) != 0) {
        soundId = pData[0x19];
    }

    // Play sound
    if (pData[0x21] == -1) {
        pData[0x21] = mover_play_sound_at_entity(pEntity, soundId);
    }
}
