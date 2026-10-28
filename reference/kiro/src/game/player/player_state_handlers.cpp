// Address: 0x800a4170-0x800acab8 | Name: player_state_handlers | Date: 2026-08-11
// Player state machine tick handlers and spawn/init utilities.
// Called from player_state_dispatch and player_main_tick.

#include "common.h"
#include "player.h"
#include "camera.h"
#include "entity.h"
#include "input.h"
#include "sound.h"
#include "nav.h"
#include "score.h"

// Forward declarations
extern void player_select_spawn_zone(void);
extern void player_select_spawn_weapon(void);
extern void player_exit_menu_state(void);
extern void player_set_eye_position(f32 x, f32 y, f32 z, Player* pPlayer);
extern void player_set_state(Player* pPlayer, int state);
extern f32  player_get_camera_lerp_factor(void* pCameraData);
extern int  player_get_active_count(void);
extern void camera_set_mode_by_index(int camIdx, int mode, int entity);
extern int  soundpad_find_nearest_entity(f32 minDist, f32* pos);
extern int* soundpad_resolve_zone_node(int zoneId);
extern void character_on_death_init(int entity);
extern void damage_play_death_sound(int charType, int deathType, f32* pos);
extern void score_register_death(int* charData, int playerId, int fallDeath);
extern void player_death_standard(void);
extern int  nav_advanced_movement_dispatch(f32 yaw, f32 speed, int playerId,
                                           f32* srcPos, f32* dstPos, int* result, int a, int b);
extern f32  input_get_analog_value(int playerId, int axis);
extern u32  input_get_buttons_held(int playerId);
extern void input_reset_pad(int playerId);
extern f32  floor_height_from_position(f32 x, f32 z);

// ============================================================================
// Address: 0x800a4170
// player_on_spawn - Player spawn initialization
// Selects spawn zone, sets position from zone node, initializes eye offset,
// assigns soundpad entity, sets camera mode, equips spawn weapon.
// ============================================================================
void player_on_spawn(void) {
    Player* pPlayer = player_get_current();
    f32 zeroF = 0.0f;
    int entity = pPlayer->entityPtr->charData;

    if ((int)pPlayer->id < g_spawnCountBase + g_spawnCountOffset) {
        int zoneId = player_select_spawn_zone();
        int* zoneNode = soundpad_resolve_zone_node(zoneId);

        if ((g_gameFlags & 0x40) == 0 || g_gameMode != 4) {
            // Normal spawn: randomize position within zone
            Vec3 spawnPos;
            zone_randomize_position(pPlayer->entityPtr, zoneNode, &spawnPos);
            pPlayer->posX = spawnPos.x;
            pPlayer->posY = spawnPos.y;
            pPlayer->posZ = spawnPos.z;
        } else {
            // Mapmaker mode: use exact zone center
            pPlayer->posX = *(f32*)(&zoneNode[3]);
            pPlayer->posY = *(f32*)(&zoneNode[4]);
            pPlayer->posZ = *(f32*)(&zoneNode[5]);
        }

        // Copy to entity transform
        *(f32*)(entity + 0xBB4) = pPlayer->posX;
        *(f32*)(entity + 0xBB8) = pPlayer->posY + g_characterHeightTable[*(s16*)(entity + 0x48)];
        *(f32*)(entity + 0xBBC) = pPlayer->posZ;

        // Set facing direction from zone
        pPlayer->facingYaw = *(f32*)(&zoneNode[6]);
        pPlayer->spawnYaw  = pPlayer->facingYaw;
    } else {
        // Overflow/debug spawn: place at origin-ish
        pPlayer->posX = (f32)pPlayer->id;
        pPlayer->posY = zeroF;
        pPlayer->posZ = zeroF;
    }

    pPlayer->headBob     = 0.0f;
    pPlayer->headBobRate = 0;

    player_init_eye_offset((int)pPlayer);

    int activeCount = player_get_active_count();
    if (activeCount == 0 && (int)pPlayer->controllerId >= 0) {
        camera_set_mode_by_index(pPlayer->cameraIdx, 0, pPlayer->entityPtr);
    } else {
        if (activeCount == 0) {
            pPlayer->flags |= 0x80; // Mark as AI-controlled camera
        }
    }

    pPlayer->weaponSwitchTimer = 0;
    pPlayer->spawnInvulnTimer  = 0;

    player_select_spawn_weapon();
    anim_blend_reset(1.0f, *(int**)(pPlayer->entityPtr + 4), 1);
    player_release_current();
}

// ============================================================================
// Address: 0x800a4328
// player_init_eye_offset - Initialize eye/head transform from entity position
// Copies entity position to eye/head offsets, queries floor height,
// finds nearest soundpad entity.
// ============================================================================
void player_init_eye_offset(int pPlayer) {
    int entityData = *(int*)(pPlayer + 0x14);

    // Copy world position to camera target
    *(f32*)(entityData + 0x64) = *(f32*)(pPlayer + 0xA8);
    *(f32*)(entityData + 0x68) = *(f32*)(pPlayer + 0xAC);
    *(f32*)(entityData + 0x6C) = *(f32*)(pPlayer + 0xB0);
    *(f32*)(entityData + 0x70) = *(f32*)(pPlayer + 0x100);
    *(f32*)(pPlayer + 0xEA4)  = *(f32*)(pPlayer + 0x100);

    // Copy previous frame position
    *(f32*)(entityData + 0xEC) = *(f32*)(entityData + 0x64);
    *(f32*)(entityData + 0xF0) = *(f32*)(entityData + 0x68);
    *(f32*)(entityData + 0xF4) = *(f32*)(entityData + 0x6C);
    *(f32*)(pPlayer + 0xEA8) = *(f32*)(entityData + 0x64);
    *(f32*)(pPlayer + 0xEAC) = *(f32*)(entityData + 0x68);
    *(f32*)(pPlayer + 0xEB0) = *(f32*)(entityData + 0x6C);

    // Set eye position (pos + height offset)
    player_set_eye_position(
        *(f32*)(pPlayer + 0xA8),
        *(f32*)(pPlayer + 0xAC) + *(f32*)(pPlayer + 0x90),
        *(f32*)(pPlayer + 0xB0),
        (Player*)pPlayer
    );

    // Copy position for interpolation
    *(f32*)(pPlayer + 0x14C) = *(f32*)(pPlayer + 0xA8);
    *(f32*)(pPlayer + 0x150) = *(f32*)(pPlayer + 0xAC);
    *(f32*)(pPlayer + 0x154) = *(f32*)(pPlayer + 0xB0);
    *(f32*)(pPlayer + 0x158) = *(f32*)(pPlayer + 0xB4);
    *(f32*)(pPlayer + 0x15C) = *(f32*)(pPlayer + 0xB8);
    *(f32*)(pPlayer + 0x160) = *(f32*)(pPlayer + 0xBC);

    // Find nearest soundpad entity for audio zone assignment
    int soundpadEntity = soundpad_find_nearest_entity(0.0f, (f32*)(pPlayer + 0xA8));
    int charData = *(int*)(*(int*)(pPlayer + 0x14) + 0x7C);
    *(int*)(charData + 0x678) = soundpadEntity;

    // Clear velocity
    *(f32*)(pPlayer + 0xF4) = 0.0f;
    *(f32*)(pPlayer + 0xF8) = 0.0f;
    *(f32*)(pPlayer + 0xFC) = 0.0f;

    // Query floor height (skip for certain game modes)
    if (g_levelId == 0x65 || g_levelId == 0x6B ||
        (g_levelId >= 0x67 && g_levelId <= 0x69) || g_levelId == 0x6A) {
        *(f32*)(pPlayer + 0xDF0) = 0.0f;
        *(f32*)(pPlayer + 0xDEC) = 0.0f;
    } else {
        f32 floorY = floor_height_from_position(1.0f, 1.0f);
        *(f32*)(pPlayer + 0xDF0) = floorY;
        *(f32*)(pPlayer + 0xDEC) = floorY;
        *(f32*)(entityData + 0x34) = floorY;
    }
}

// ============================================================================
// Address: 0x800a5ce8
// player_set_weapon_slot_anim - Set or clear weapon slot animation
// If param_2 is NULL, clears anim (-1). Otherwise sets anim type 3.
// Called from zone capture, weapon rack, pickup equip flows.
// ============================================================================
void player_set_weapon_slot_anim(int weaponSlot, u32* animData) {
    int animHandle = *(int*)(weaponSlot + 4);
    if (animHandle < 0) return;

    if (animData == NULL) {
        anim_slot_clear(animHandle, -1);
    } else {
        anim_slot_set_type(animHandle, 3, animData);
    }
}

// ============================================================================
// Address: 0x800a5dd4
// player_add_damage_indicator - Add directional damage indicator to HUD
// Transforms attacker position to view space, computes screen direction,
// stores in 5-slot circular buffer with color coding.
// ============================================================================
void player_add_damage_indicator(f32 attackerX, f32 attackerY, f32 attackerZ, int isExplosion) {
    Player* pPlayer = player_get_current();
    int indicatorData = (int)((u64)pPlayer >> 32); // Upper half = indicator struct

    // Get attacker world position
    f32 srcPos[3];
    srcPos[0] = *(f32*)pPlayer;
    srcPos[1] = ((f32*)pPlayer)[1];
    srcPos[2] = ((f32*)pPlayer)[2];

    // Transform to view space
    f32 viewPos[4];
    matrix_transform_point(*(f32**)(g_cameraData + 0x20C), srcPos, viewPos);

    f32 nearPlane = g_cameraNearPlane;
    f32 fovScale  = g_cameraFovScale * viewPos[3]; // depth * FOV

    // Frustum check: only add if attacker is in view volume
    if (-fovScale <= viewPos[0] && viewPos[0] <= fovScale &&
        -fovScale <= viewPos[1] && viewPos[1] <= fovScale &&
        -viewPos[3] <= viewPos[2] && viewPos[2] <= viewPos[3]) {

        int slotIdx = *(int*)(indicatorData + 0x14);
        if (slotIdx >= 5) return; // Buffer full

        *(int*)(indicatorData + 0x14) = slotIdx + 1;

        f32 halfScreen = g_halfScreenSize;
        f32* slot = (f32*)(indicatorData + slotIdx * 0x24 + 0x18);

        // Clamp depth
        f32 depth = viewPos[3];
        if (depth < nearPlane) depth = nearPlane;

        f32 scale = *(f32*)(g_cameraData + 0x268) / depth;

        // Limit scale for sniping
        if (isExplosion != 0 && *(int*)(g_playerGlobal + 0x1390) == 9) {
            if (scale > g_maxDamageIndicatorScale) {
                scale = g_maxDamageIndicatorScale;
            }
        }

        // Compute screen-space velocity
        slot[3] = attackerY * scale;
        slot[4] = attackerZ * scale;

        // Normalize to screen coordinates
        f32 normFactor = halfScreen / depth;
        viewPos[0] *= normFactor;
        viewPos[1] *= normFactor;
        viewPos[2] *= normFactor;

        // Apply view matrix inverse to get screen direction
        slot[0] = (f32)(-*(int*)(g_viewMatrix + 0x10)) * viewPos[0] +
                  (f32)(-*(int*)(g_viewMatrix + 0x08));
        slot[1] = (f32)(-*(int*)(g_viewMatrix + 0x24)) * viewPos[1] +
                  (f32)(-*(int*)(g_viewMatrix + 0x1C));
        slot[2] = viewPos[2] + halfScreen;

        // Store attacker world position for tracking
        slot[6] = *(f32*)pPlayer;
        slot[7] = ((f32*)pPlayer)[1];
        slot[8] = ((f32*)pPlayer)[2];

        // Color code: 0=none, 1=normal, 2=heavy, 3=explosion
        if (isExplosion == 0) {
            slot[5] = 0.0f;
        } else {
            if (*(f32*)(indicatorData + 4) <= g_heavyDamageThreshold) {
                slot[5] = *(f32*)&g_indicatorColor_Normal;  // 1
            } else {
                slot[5] = *(f32*)&g_indicatorColor_Heavy;   // 2
            }
            if (*(int*)(indicatorData + 0xC) != 0) {
                slot[5] = *(f32*)&g_indicatorColor_Explosion; // 3
            }
        }
    }
    player_release_current();
}

// ============================================================================
// Address: 0x800a7260
// player_sync_head_position - Sync player head to entity position
// Copies entity world position to head/eye tracking when not in special mode.
// Called from player_main_tick.
// ============================================================================
void player_sync_head_position(void) {
    int entityData = *(int*)(g_playerGlobal + 0x14);

    if (*(int*)(g_playerGlobal + 0x114) != 0x400 ||
        player_stub_return_zero() != 0) {
        // Normal: sync head position from player world pos
        *(f32*)(entityData + 0x64) = *(f32*)(g_playerGlobal + 0xA8);
        *(f32*)(entityData + 0x68) = *(f32*)(g_playerGlobal + 0xAC);
        *(f32*)(entityData + 0x6C) = *(f32*)(g_playerGlobal + 0xB0);
    }
}

// ============================================================================
// Address: 0x800a72c8
// player_state_falling_tick - Falling state tick handler
// Applies gravity, runs navigation/movement, detects landing or death from
// fall height. Triggers death via appropriate path based on game mode.
// Called from player_state_dispatch.
// ============================================================================
void player_state_falling_tick(void) {
    f32* moveResult = player_get_move_buffer();
    f32 gravityAccel = g_playerGravityAccel;
    f32 dt = g_deltaTime;
    int entity = g_playerGlobal[5];
    int* charData = *(int**)(entity + 0x7C);

    // Clear head-bob flag
    g_playerGlobal[3] &= ~0x10;

    // Integrate gravity (velocity += gravity * dt, clamped to terminal)
    charData[0x2C7] = (int)-(dt * g_fallGravityRate - (f32)charData[0x2C7]);
    if ((f32)charData[0x2C7] < gravityAccel) {
        charData[0x2C7] = (int)gravityAccel;
    }

    // Compute desired position: pos + velocity * dt
    f32 velXdt = (f32)g_playerGlobal[0x38E] * dt;
    moveResult[0] = velXdt + (f32)g_playerGlobal[0x2A];
    moveResult[1] = (f32)charData[0x2C7] * dt + (f32)g_playerGlobal[0x2B];
    f32 velZ = (f32)g_playerGlobal[0x390];
    moveResult[2] = velZ * dt + (f32)g_playerGlobal[0x2C];

    // Apply friction to lateral damping
    g_playerGlobal[0x26] = (f32)g_playerGlobal[0x26] * g_fallFriction;

    // Run nav collision / movement
    f32 yaw = (f32)g_playerGlobal[0x25] + (f32)g_playerGlobal[0x23];
    f32 speed = (f32)g_playerGlobal[0x28];
    int collResult[11];
    u32 navFlags = nav_advanced_movement_dispatch(
        yaw, speed, *g_playerGlobal,
        g_playerGlobal + 0x2A, moveResult, collResult, 1, 0
    );

    // Check if we should accept the movement
    int onPlatform = *(int*)(entity + 0x21C);
    int hitGround = (navFlags & 1);
    f32 floorY = (f32)charData[0x2DC];
    f32 fallDist = floorY - moveResult[1];

    if (onPlatform != 0 || hitGround ||
        fallDist <= g_maxFallBeforeDeath ||
        (g_gameFlags & 0x20000000) != 0 ||
        (g_gameFlags & 0x200) != 0) {

        // Accept new position
        g_playerGlobal[0x2A] = *(int*)&moveResult[0];
        g_playerGlobal[0x2B] = *(int*)&moveResult[1];
        g_playerGlobal[0x2C] = *(int*)&moveResult[2];

        // Update eye position
        player_set_eye_position(
            (f32)g_playerGlobal[0x2A],
            (f32)g_playerGlobal[0x26] + (f32)g_playerGlobal[0x2B] + (f32)g_playerGlobal[0x24],
            (f32)g_playerGlobal[0x2C],
            (Player*)g_playerGlobal
        );

        // Mark entity transform dirty
        charData[9] |= 1;

        // Camera shake from movement
        f32 shakeAmount = dt * (1.0f / (f32)g_framerate);
        camera_apply_shake(shakeAmount, 0x36, charData[2]);

        // Handle ground hit
        if (hitGround) {
            f32 landHeight = (f32)charData[0x2DC];
            f32 fallDelta = 0.0f;

            if (landHeight != g_deathPlaneY) {
                fallDelta = landHeight - (f32)charData[0x2FF];
            }

            // Compute damage from fall height
            if (landHeight != g_deathPlaneY) {
                if (fallDelta > g_fallDamageMin && fallDelta < g_fallDamageLethal) {
                    // Partial damage
                    f32 dmgFrac = (fallDelta - g_fallDamageMin) / g_fallDamageRange;
                    g_playerGlobal[0x395] = dmgFrac;
                    g_playerGlobal[0x396] = g_heavyDamageThreshold;
                } else if (fallDelta >= g_fallDamageLethal) {
                    // Lethal fall
                    g_playerGlobal[0x395] = 1.0f;
                    g_playerGlobal[0x396] = g_heavyDamageThreshold;
                }
            }

            // Accumulate fall damage over time
            if (g_fallDamageDisabled == 0 && fallDelta > g_fallSoundThreshold) {
                f32 accumDmg = g_fallDamageLethal * (fallDelta - g_fallSoundThreshold);
                *(f32*)(entity + 0x1CC) = -(accumDmg - *(f32*)(entity + 0x1CC));
            }

            // Check for death from fall
            if (*(f32*)(entity + 0x1CC) > 0.0f) {
                player_set_state((Player*)g_playerGlobal, 2); // STATE_DEAD
            } else if (onPlatform == 0 ||
                       (*(u16*)(charData[0x300] + 8) & 0x1000) == 0) {
                // Normal death or instant-kill zone
                if (g_gameSubMode == 0x0A) {
                    // Virus mode: instant death
                    switch_slot_reset(NULL);
                    charData[0x24] = 0;
                    player_death_standard();
                    score_register_death(charData, charData[2], 1);
                } else {
                    player_set_state((Player*)g_playerGlobal, 2);
                    character_on_death_init(g_playerGlobal[5]);
                }
            } else {
                // Death on special platform (e.g. crusher)
                player_set_state((Player*)g_playerGlobal, 2);
                g_playerGlobal[3] |= 0x800;
                g_playerGlobal[3] |= 0x10000;
                character_on_death_init(g_playerGlobal[5]);
            }
        }
    } else if (g_gameSubMode == 0x0A && g_levelId != 0x0B) {
        // Virus mode void-out: teleport kill
        player_void_teleport_kill(*g_playerGlobal, charData[0x19E], 0, 0, 1);
    } else {
        // Fell into void: standard death
        player_set_state((Player*)g_playerGlobal, 2);
        f32* deathPos = entity_get_world_position(*charData);
        damage_play_death_sound(
            (int)*(s16*)(charData + 0x12),
            (int)*(s8*)((int)charData + 0xC0D),
            deathPos
        );
        g_playerGlobal[3] |= 0x800;
        g_playerGlobal[3] |= 0x10000;
        character_on_death_init(entity);
    }
    player_release_move_buffer();
}

// ============================================================================
// Address: 0x800a831c
// player_state_menu_tick - Menu state tick handler
// Clears camera spring, calls menu callback if set, checks button input
// to exit menu via player_exit_menu_state.
// Called from player_state_dispatch.
// ============================================================================
void player_state_menu_tick(void) {
    // Clear camera spring value
    if (g_cameraData != 0) {
        *(f32*)(g_cameraData + 0x3B8) = 0.0f;
    }

    // Call menu tick callback if registered
    if ((void*)g_playerGlobal[0x505] != NULL) {
        ((void (*)(int))g_playerGlobal[0x505])(
            *(int*)(g_playerGlobal[2] + 0x26C)
        );
    }

    // Check controller input to exit menu
    if (g_playerGlobal[1] < 0) return;

    int padConnected = input_pad_is_connected(g_playerGlobal[1]);
    if (padConnected == 0) return;

    if ((g_playerGlobal[0x504] & 4) != 0) {
        return; // Locked: don't accept input
    }

    if ((g_playerGlobal[0x504] & 2) != 0 ||
        (input_get_buttons_held(*g_playerGlobal) & g_playerGlobal[8]) != 0) {
        // Primary button or flag set: exit menu
        if ((g_playerGlobal[0x504] & 2) == 0) {
            input_reset_pad(*g_playerGlobal);
            player_exit_menu_state();
            return;
        }
        // Secondary check
        if ((input_get_buttons_held(*g_playerGlobal) & g_playerGlobal[9]) != 0) {
            input_reset_pad(*g_playerGlobal);
            player_exit_menu_state();
            return;
        }
    }
}

// ============================================================================
// Address: 0x800a83f4
// player_state_interlevel_tick - Interlevel/loading state tick
// Simple wrapper that calls the level transition update function.
// Called from player_state_dispatch.
// ============================================================================
void player_state_interlevel_tick(void) {
    level_transition_update();
}

// ============================================================================
// Address: 0x800a8424
// player_camera_look_tick - Camera look/aim tick from analog sticks
// Reads dual analog stick input with dead zone, applies sensitivity and lerp
// factor, updates yaw/pitch on both camera entities with clamping.
// Called from player_state_dispatch.
// ============================================================================
void player_camera_look_tick(void) {
    f32 deadZone = 0.0f;

    // Get both camera sub-entities from player entity chain
    int charData = *(int*)(g_playerGlobal[5] + 0x7C);
    int camEntityA = *(int*)(*(int*)(charData + 0xC14) + 0x7C);
    int camEntityB = *(int*)(*(int*)(charData + 0xC18) + 0x7C);

    // Vertical dead zone: reduced in vehicle mode
    f32 vertDeadZone = 1.0f;
    if (*(int*)(g_playerGlobal[0x4E6] + 0x74) == 2) {
        vertDeadZone = deadZone; // 0.0 in vehicle
    }

    // --- Process camera entity A (right stick typically) ---
    if (*(int*)(camEntityA + 0x10) != 9 && *(int*)(camEntityA + 0x10) != 1) {
        f32 stickX = input_get_analog_value(*g_playerGlobal, 1);
        f32 stickY = input_get_analog_value(*g_playerGlobal, 2);

        // Apply dead zone to X axis
        f32 adjX = 0.0f;
        if (stickX > deadZone) {
            adjX = stickX - deadZone;
        } else if (stickX < -deadZone) {
            adjX = stickX + deadZone;
        }
        adjX = adjX * (1.0f / (1.0f - deadZone));

        // Apply dead zone to Y axis
        f32 adjY = 0.0f;
        if (stickY > vertDeadZone) {
            adjY = stickY - vertDeadZone;
        } else if (stickY < -vertDeadZone) {
            adjY = stickY + vertDeadZone;
        }
        adjY = adjY * (1.0f / (1.0f - vertDeadZone));

        // Invert Y if setting enabled
        if ((*(u16*)(g_playerGlobal[0x4E6] + 2) & 1) != 0) {
            adjY = -adjY;
        }

        // Apply sensitivity, lerp factor, and delta time
        f32 lerpFactor = player_get_camera_lerp_factor(g_cameraData);
        f32 sensX = g_sensitivityX * (1.0f / (f32)g_framerate);
        f32 fov = *(f32*)(g_cameraData + 900);
        *(f32*)(camEntityA + 0x1C) = -(g_deltaTime * adjY * sensX * (fov / lerpFactor)
                                       - *(f32*)(camEntityA + 0x1C));

        lerpFactor = player_get_camera_lerp_factor(g_cameraData);
        f32 sensY = g_sensitivityY * (1.0f / (f32)g_framerate);
        *(f32*)(camEntityA + 0x24) = -(g_deltaTime * adjX * sensY * (fov / lerpFactor)
                                       - *(f32*)(camEntityA + 0x24));

        // Clamp yaw to limits
        f32 yawMin = *(f32*)(camEntityA + 0x34);
        f32 yawMax = *(f32*)(camEntityA + 0x38);
        if (yawMin == yawMax) {
            *(f32*)(camEntityA + 0x1C) = yawMin;
        } else {
            *(f32*)(camEntityA + 0x1C) = clamp_f32(
                *(f32*)(camEntityA + 0x1C), yawMin, yawMax);
        }

        // Clamp pitch to limits
        f32 pitchMin = *(f32*)(camEntityA + 0x3C);
        f32 pitchMax = *(f32*)(camEntityA + 0x40);
        if (pitchMin == pitchMax) {
            *(f32*)(camEntityA + 0x24) = pitchMin;
        } else {
            *(f32*)(camEntityA + 0x24) = clamp_f32(
                *(f32*)(camEntityA + 0x24), pitchMin, pitchMax);
        }

        // Clamp roll to limits
        f32 rollMin = *(f32*)(camEntityA + 0x44);
        f32 rollMax = *(f32*)(camEntityA + 0x48);
        if (rollMin == rollMax) {
            *(f32*)(camEntityA + 0x2C) = rollMin;
        } else {
            *(f32*)(camEntityA + 0x2C) = clamp_f32(
                *(f32*)(camEntityA + 0x2C), rollMin, rollMax);
        }
    }

    // --- Process camera entity B (left stick typically) ---
    if (*(int*)(camEntityB + 0x10) != 9 && *(int*)(camEntityB + 0x10) != 1) {
        f32 stickX = input_get_analog_value(*g_playerGlobal, 3);
        f32 stickY = input_get_analog_value(*g_playerGlobal, 4);

        // Apply dead zone to X axis
        f32 adjX = 0.0f;
        if (stickX > deadZone) {
            adjX = stickX - deadZone;
        } else if (stickX < -deadZone) {
            adjX = stickX + deadZone;
        }
        adjX = adjX * (1.0f / (1.0f - deadZone));

        // Apply dead zone to Y axis
        f32 adjY = 0.0f;
        if (stickY > vertDeadZone) {
            adjY = stickY - vertDeadZone;
        } else if (stickY < -vertDeadZone) {
            adjY = stickY + vertDeadZone;
        }
        adjY = adjY * (1.0f / (1.0f - vertDeadZone));

        // Invert Y if setting enabled
        if ((*(u16*)(g_playerGlobal[0x4E6] + 2) & 1) != 0) {
            adjY = -adjY;
        }

        // Apply sensitivity, lerp factor, and delta time
        f32 lerpFactor = player_get_camera_lerp_factor(g_cameraData);
        f32 sensX = g_sensitivityX * (1.0f / (f32)g_framerate);
        f32 fov = *(f32*)(g_cameraData + 900);
        *(f32*)(camEntityB + 0x1C) = -(g_deltaTime * adjY * sensX * (fov / lerpFactor)
                                       - *(f32*)(camEntityB + 0x1C));

        lerpFactor = player_get_camera_lerp_factor(g_cameraData);
        f32 sensY = g_sensitivityY * (1.0f / (f32)g_framerate);
        *(f32*)(camEntityB + 0x24) = -(g_deltaTime * adjX * sensY * (fov / lerpFactor)
                                       - *(f32*)(camEntityB + 0x24));

        // Clamp yaw
        f32 yawMin = *(f32*)(camEntityB + 0x34);
        f32 yawMax = *(f32*)(camEntityB + 0x38);
        if (yawMin == yawMax) {
            *(f32*)(camEntityB + 0x1C) = yawMin;
        } else {
            *(f32*)(camEntityB + 0x1C) = clamp_f32(
                *(f32*)(camEntityB + 0x1C), yawMin, yawMax);
        }

        // Clamp pitch
        f32 pitchMin = *(f32*)(camEntityB + 0x3C);
        f32 pitchMax = *(f32*)(camEntityB + 0x40);
        if (pitchMin == pitchMax) {
            *(f32*)(camEntityB + 0x24) = pitchMin;
        } else {
            *(f32*)(camEntityB + 0x24) = clamp_f32(
                *(f32*)(camEntityB + 0x24), pitchMin, pitchMax);
        }

        // Clamp roll
        f32 rollMin = *(f32*)(camEntityB + 0x44);
        f32 rollMax = *(f32*)(camEntityB + 0x48);
        if (rollMin == rollMax) {
            *(f32*)(camEntityB + 0x2C) = rollMin;
        } else {
            *(f32*)(camEntityB + 0x2C) = clamp_f32(
                *(f32*)(camEntityB + 0x2C), rollMin, rollMax);
        }
    }
}

// ============================================================================
// Address: 0x800acab8
// player_ai_is_explosive_weapon - Check if weapon type is explosive
// Returns 1 if weapon type equals 0x59 (rocket launcher / grenade launcher).
// Used by AI for weapon classification.
// ============================================================================
int player_ai_is_explosive_weapon(int weaponType) {
    if (weaponType != 0x59) {
        return 0;
    }
    return 1;
}
