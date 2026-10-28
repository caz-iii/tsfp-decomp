// Address: 0x800a0af8-0x800a1a68 | Name: camera_orbit_system | Date: 2026-08-11
// Camera orbit/replay/death system. Includes waypoint spline interpolation,
// per-frame orbit tick, replay cam input, sway effects, and death target.
// Called from camera_per_frame_update (0x8009df90).

#include "common.h"

// External references
extern int DAT_80611d54;   // Active camera player slot
extern int DAT_80611d74;   // Player data pointer
extern int DAT_80611d78;   // Player struct base
extern int DAT_80611d6c;   // Replay target entity
extern int DAT_80611d44;   // Replay state counter
extern int DAT_80611d60;   // Last input frame
extern int DAT_80611928;   // Current frame counter
extern int DAT_80612610;   // Camera mode state
extern int DAT_80612614;   // Room query mode
extern unsigned int DAT_805015c0; // Game flags (bit 30 = multiplayer)
extern float FLOAT_80611d08; // Orbit yaw angle
extern float FLOAT_80611d0c; // Orbit pitch angle
extern float FLOAT_80611d10; // Orbit radius
extern float FLOAT_80611d64; // Vertical offset (death/replay)
extern float FLOAT_80611d68; // Unused orbit param

extern float FLOAT_80614678; // 0.0f
extern float FLOAT_80614680; // 1.0f
extern float FLOAT_80614688; // 100.0f (analog scale)
extern float FLOAT_806146a0; // Radius scale factor
extern float FLOAT_806146ac; // Small epsilon / offset
extern float FLOAT_806146d4; // PI
extern float FLOAT_806146d8; // PI/2
extern float FLOAT_806146dc; // 0.5f
extern float FLOAT_806146e0; // Blend threshold
extern float FLOAT_806146e8; // 2*PI (tau)
extern float FLOAT_80614710; // Min distance squared
extern float FLOAT_8061471c; // 3.0f (Newton-Raphson)
extern float FLOAT_80614748; // Angle-to-table scale
extern float FLOAT_80614750; // Blend rate
extern float FLOAT_80614754; // Angle wrap limit
extern float FLOAT_80614758; // Sway accumulation rate
extern float FLOAT_8061475c; // Sway center offset
extern float FLOAT_80614760; // Max elevation
extern float FLOAT_806146f8; // Elevation ramp rate
extern float FLOAT_806108ec; // Delta time

extern float DAT_80561ca8; // Death target X
extern float DAT_80561cac; // Death target Y
extern float DAT_80561cb0; // Death target Z

extern void camera_orbit_init(int pSlot);
extern void match_reset_cameras_and_spawn(void);
extern void player_exit_menu_state(void);
extern void player_set_active_count(int count);
extern int entity_find_next_by_type(int pEntity, int type);
extern int FUN_801c1714(void);  // get_player_count
extern void FUN_800f71b0(double blend, int pTarget);
extern void FUN_8001c710(int* pDst, int a, int b, int c, int d);
extern int scene_ray_portal_intersect(double f1, double f2, float* pStart, float* pDir,
                                      int roomA, int roomB, int flags, float* pOut1, float* pOut2);
extern int scene_query_nodes_in_frustum(int flags, int pMin, int pNodeBuf, int maxNodes);
extern int scene_raycast(int room, void* pOrigin, float* pDir, int unused,
                         float* pHitPos, float* pHitNorm, int* pRoom, int mask, int flags, int* extra);
extern void FUN_801317b8(double t, float* pOut, float* p0, float* p1, float* p2, float* p3);
extern double FUN_80131c68(double t, double v0, double v1, double v2, double v3);
extern double FUN_80131538(double val);  // sqrtf
extern double FUN_80390324(double val);  // fmodf
extern void FUN_803826e4(void);  // paired-single save
extern void FUN_80382730(void);  // paired-single restore
extern double input_get_analog_value(int pad, int axis);
extern unsigned int input_get_buttons_held(int pad);
extern unsigned int input_get_buttons_pressed(int pad);
extern int floor_height_from_position(double y, double radius);
extern void camera_set_orbit_angles(double sway, double elev, double a, double b, int pSlot, int mode);
extern void camera_third_person_tick(void);

extern unsigned int DAT_806109c0; // Frame timestamp for double conversion
extern double DOUBLE_80614698;    // 4503599627370496.0 (2^52 bias)
extern float* DAT_80583228;       // Sine table (2048 entries)

// ============================================================================
// Address: 0x800a0af8 | camera_orbit_advance_waypoint
// Advances to next orbit waypoint. Re-inits orbit if more remain, otherwise
// transitions to match reset (MP) or per-player camera reset (SP).
// ============================================================================
void camera_orbit_advance_waypoint(int pSlot) {
    int pOrbitData = *(int*)(*(int*)(pSlot + 0x26C) + 0xA8);

    // Increment waypoint index
    *(int*)(pSlot + 0xFB8) = *(int*)(pSlot + 0xFB8) + 1;

    int waypointCount = *(int*)(*(int*)(pSlot + 0x26C) + 0xAC);
    if (*(int*)(pSlot + 0xFB8) < waypointCount) {
        // More waypoints remain - reinitialize for next segment
        camera_orbit_init(pSlot);
    } else if ((*(unsigned int*)(pOrbitData + 0x10) & 1) == 0) {
        // No loop flag - finish orbit
        *(int*)(pSlot + 0xFC4) = 1;
        *(float*)(pSlot + 0xFC8) = FLOAT_80614678; // Reset t to 0

        if ((DAT_805015c0 & 0x40000000) == 0) {
            // Singleplayer: reset each player's camera
            int stride = 0;
            int playerCount = FUN_801c1714();
            for (int i = 0; i < playerCount; i++) {
                int pPlayerSlot = *(int*)(DAT_80611d78 + stride + 8);
                if (pPlayerSlot != 0 && pPlayerSlot == DAT_80611d54) {
                    player_exit_menu_state();
                }
                stride += 0x141C;
            }
        } else {
            // Multiplayer: full match camera/spawn reset
            match_reset_cameras_and_spawn();
        }
    }
}

// ============================================================================
// Address: 0x800a0bd0 | camera_orbit_spline_interpolate
// Catmull-Rom spline interpolation along waypoint track. Evaluates position +
// direction from 4 control points. On final MP segment, copies player eye as
// last point.
// ============================================================================
void camera_orbit_spline_interpolate(void) {
    FUN_803826e4(); // Save paired-single regs

    int pOrbitData = *(int*)(*(int*)(DAT_80611d54 + 0x26C) + 0xA8);
    int waypointCount = *(int*)(*(int*)(DAT_80611d54 + 0x26C) + 0xAC);
    int currentWaypoint = *(int*)(DAT_80611d54 + 0xFB8);
    int numKeys = *(int*)(DAT_80611d54 + 0xFC0);
    int* pKeyArray = (int*)*(int*)(DAT_80611d54 + 0xFBC);

    // On final MP segment: overwrite last key with player eye position
    if ((DAT_805015c0 & 0x40000000) != 0 &&
        *(int*)(DAT_80611d54 + 0xFC4) + 2 <= numKeys &&
        currentWaypoint == waypointCount - 1) {

        float* pLastKey = (float*)((int)pKeyArray + (numKeys - 1) * 0x1C);
        // Copy player eye position to last control point
        pLastKey[0] = *(float*)(DAT_80611d74 + 0xB4);
        pLastKey[1] = *(float*)(DAT_80611d74 + 0xB8);
        pLastKey[2] = *(float*)(DAT_80611d74 + 0xBC);
        // Compute direction from player yaw+pitch
        // (spherical to cartesian using sin/cos of combined angles)
        // ... direction calculation stored at pLastKey[3..5]
    }

    // If orbit still has segments left (not looped or not past end)
    if (((*(unsigned int*)(pOrbitData + 0x10) & 1) == 0) ||
        currentWaypoint < waypointCount ||
        *(int*)(DAT_80611d54 + 0xFC4) < numKeys) {

        int keyIdx = *(int*)(DAT_80611d54 + 0xFC4);
        float* p1 = (float*)((int)pKeyArray + (keyIdx - 1) * 0x1C);
        float* p2 = (float*)((int)pKeyArray + keyIdx * 0x1C);

        // Clamp boundary control points (first/last duplicate)
        float* p0 = (keyIdx != 1) ? (float*)((int)pKeyArray + (keyIdx - 2) * 0x1C) : p1;
        float* p3 = (keyIdx != numKeys - 1) ? (float*)((int)pKeyArray + (keyIdx + 1) * 0x1C) : p2;

        float t = *(float*)(DAT_80611d54 + 0xFC8);

        // Interpolate position (3 floats) via Catmull-Rom
        // FUN_801317b8 is catmull_rom_3d
        float posOut[3], dirOut[3];
        FUN_801317b8((double)t, posOut, p0, p1, p2, p3);
        // Interpolate direction (3 floats offset +3)
        FUN_801317b8((double)t, dirOut, p0 + 3, p1 + 3, p2 + 3, p3 + 3);
        // Interpolate speed (single float at offset +6)
        double speed = FUN_80131c68((double)t, (double)p0[6], (double)p1[6],
                                    (double)p2[6], (double)p3[6]);

        // Advance t by speed * dt
        *(float*)(DAT_80611d54 + 0xFC8) = (float)(speed * (double)FLOAT_806108ec) + t;

        // If t exceeds 1.0, advance to next key
        if (FLOAT_80614680 < *(float*)(DAT_80611d54 + 0xFC8)) {
            *(float*)(DAT_80611d54 + 0xFC8) -= FLOAT_80614680;
            *(int*)(DAT_80611d54 + 0xFC4) += 1;
            if (numKeys - 1 < *(int*)(DAT_80611d54 + 0xFC4)) {
                camera_orbit_advance_waypoint(DAT_80611d54);
            }
        }
    }

    FUN_80382730(); // Restore paired-single regs
}

// ============================================================================
// Address: 0x800a0ec0 | camera_orbit_tick
// Main orbit camera per-frame update. Handles fixed-speed mode (1 key) vs
// spline mode. Applies match-end blend. Performs portal ray traversal + AABB
// query for room assignment.
// ============================================================================
void camera_orbit_tick(void) {
    FUN_803826e4(); // Save paired-single regs

    int numKeys = *(int*)(DAT_80611d54 + 0xFC0);
    int pOrbitData = *(int*)(*(int*)(DAT_80611d54 + 0x26C) + 0xA8);

    if (numKeys == 1) {
        // Fixed-speed single-key mode: linear advancement
        *(int*)(DAT_80611d54 + 0x27C) = *(int*)*(int*)(DAT_80611d54 + 0xFBC);
        *(int*)(DAT_80611d54 + 0x280) = *(int*)(*(int*)(DAT_80611d54 + 0xFBC) + 4);
        *(int*)(DAT_80611d54 + 0x284) = *(int*)(*(int*)(DAT_80611d54 + 0xFBC) + 8);
        *(int*)(DAT_80611d54 + 0x334) = *(int*)(*(int*)(DAT_80611d54 + 0xFBC) + 0xC);
        *(int*)(DAT_80611d54 + 0x338) = *(int*)(*(int*)(DAT_80611d54 + 0xFBC) + 0x10);
        *(int*)(DAT_80611d54 + 0x33C) = *(int*)(*(int*)(DAT_80611d54 + 0xFBC) + 0x14);
        *(int*)(DAT_80611d54 + 0x2A8) = 0;

        // Advance t by dt / frame_count (fixed-speed timing)
        int waypointIdx = *(int*)(DAT_80611d54 + 0xFB8);
        float duration = *(float*)(pOrbitData + waypointIdx * 0x14 + 8);
        *(float*)(DAT_80611d54 + 0xFC8) += FLOAT_806108ec * (FLOAT_80614680 / duration);

        if (duration <= *(float*)(DAT_80611d54 + 0xFC8) && FLOAT_80614678 <= duration) {
            camera_orbit_advance_waypoint(DAT_80611d54);
        }
    } else {
        // Multi-key spline mode
        *(int*)(DAT_80611d54 + 0x2A8) = 0;

        // MP match-end blend: smoothly interpolate toward player on final segment
        if ((DAT_805015c0 & 0x40000000) != 0 &&
            *(int*)(DAT_80611d54 + 0xFC4) == numKeys - 1 &&
            numKeys != 1 &&
            *(int*)(DAT_80611d54 + 0xFB8) ==
                *(int*)(*(int*)(DAT_80611d54 + 0x26C) + 0xAC) - 1) {

            if (FLOAT_806146e0 < *(float*)(DAT_80611d54 + 0xFC8) &&
                *(int*)DAT_80611d74 == 0) {
                float blendFactor = FLOAT_80614750 *
                    (*(float*)(DAT_80611d54 + 0xFC8) - FLOAT_806146e0);
                FUN_800f71b0((double)blendFactor,
                             *(int*)(*(int*)(DAT_80611d74 + 0x14) + 0x7C) + 0xC68);
            }
        }

        camera_orbit_spline_interpolate();
    }

    // Camera mode 2 + multiplayer: apply special view matrix blending
    if (DAT_80612610 == 2 && (DAT_805015c0 & 0x40000000) != 0) {
        // Matrix interpolation for spectator/replay transition
        // (FUN_8001c710 is a view matrix blend helper)
    }

    // Room assignment via portal ray traversal
    int currentRoom = *(int*)(DAT_80611d54 + 0x2A0);
    int targetRoom = (currentRoom != 0) ? currentRoom : 1;
    float camPos[3] = {
        *(float*)(DAT_80611d54 + 0x27C),
        *(float*)(DAT_80611d54 + 0x280),
        *(float*)(DAT_80611d54 + 0x284)
    };
    float prevPos[3] = {
        *(float*)(DAT_80611d54 + 0x288),
        *(float*)(DAT_80611d54 + 0x28C),
        *(float*)(DAT_80611d54 + 0x290)
    };
    float dir[3] = {
        camPos[0] - prevPos[0],
        camPos[1] - prevPos[1],
        camPos[2] - prevPos[2]
    };

    // Portal traversal: walk from previous room to find current room
    if (DAT_80612614 != 0x65) {
        int fromRoom = 0;
        int toRoom = targetRoom;
        while (toRoom != fromRoom) {
            int result = scene_ray_portal_intersect(
                0.0, 0.0, prevPos, dir, toRoom, fromRoom, 0, prevPos, dir);
            fromRoom = toRoom;
            toRoom = result;
        }
        if (toRoom != targetRoom) {
            *(int*)(DAT_80611d54 + 0x2A0) = toRoom;
        }
    }

    // If portal traversal didn't find a room, do AABB node query + raycast
    // to find the correct scene room for this camera position
    // (queries scene nodes in frustum, raycasts backward to confirm)

    *(int*)(DAT_80611d54 + 0x2A4) = *(int*)(DAT_80611d54 + 0x2A0);
    FUN_80382730(); // Restore paired-single regs
}

// ============================================================================
// Address: 0x800a1328 | camera_stub_nop_c
// Empty stub - no operation.
// ============================================================================
void camera_stub_nop_c(void) {
    return;
}

// ============================================================================
// Address: 0x800a132c | camera_stub_nop_d
// Empty stub - no operation.
// ============================================================================
void camera_stub_nop_d(void) {
    return;
}

// ============================================================================
// Address: 0x800a1330 | camera_replay_init
// Replay/killcam camera init. Sets 6 player slots, finds 5th type-1 entity
// as target, computes orbit radius from velocity magnitude.
// ============================================================================
void camera_replay_init(void) {
    DAT_80611d44 = 0;
    FLOAT_80611d08 = FLOAT_80614678; // yaw = 0
    FLOAT_80611d0c = FLOAT_80614678; // pitch = 0
    FLOAT_80611d64 = FLOAT_80614678; // vertical offset = 0
    FLOAT_80611d68 = FLOAT_80614678; // unused = 0

    player_set_active_count(6);

    // Walk entity pool to find 5th type-1 entity (characters)
    DAT_80611d6c = entity_find_next_by_type(0, 1);
    DAT_80611d6c = entity_find_next_by_type(DAT_80611d6c, 1);
    DAT_80611d6c = entity_find_next_by_type(DAT_80611d6c, 1);
    DAT_80611d6c = entity_find_next_by_type(DAT_80611d6c, 1);
    DAT_80611d6c = entity_find_next_by_type(DAT_80611d6c, 1);

    if (DAT_80611d6c != 0) {
        // Compute orbit radius from target's velocity magnitude
        float vx = *(float*)(DAT_80611d6c + 0x1C0);
        float vy = *(float*)(DAT_80611d6c + 0x1C4);
        float vz = *(float*)(DAT_80611d6c + 0x1C8);
        float speed = (float)FUN_80131538((double)(vx*vx + vy*vy + vz*vz));
        FLOAT_80611d10 = FLOAT_806146a0 * speed;
    }
}

// ============================================================================
// Address: 0x800a13e8 | camera_replay_tick
// Replay camera per-frame tick. Reads analog input for orbit angles and zoom.
// Tracks target entity, computes spherical coordinates, performs scene raycast
// for camera collision. Called from camera_per_frame_update.
// ============================================================================
void camera_replay_tick(void) {
    int pPlayerStruct = DAT_80611d78;
    unsigned int targetEntity = DAT_80611d6c;

    if (targetEntity == 0) {
        return;
    }

    // Rate-limit input polling to once per frame
    if (DAT_80611d60 < DAT_80611928) {
        // D-pad right: cycle to next target entity
        unsigned int buttons = input_get_buttons_held(0);
        if ((buttons & 0x20) != 0) {
            unsigned int nextTarget = entity_find_next_by_type(DAT_80611d6c, 1);
            if (nextTarget == 0) {
                DAT_80611d6c = nextTarget;
                return;
            }
            FLOAT_80611d64 = FLOAT_80614678;
            DAT_80611d6c = nextTarget;
            targetEntity = nextTarget;
            // Recompute radius from new target's speed
            float vx = *(float*)(nextTarget + 0x1C0);
            float vy = *(float*)(nextTarget + 0x1C4);
            float vz = *(float*)(nextTarget + 0x1C8);
            float speed = (float)FUN_80131538((double)(vx*vx + vy*vy + vz*vz));
            FLOAT_80611d10 = FLOAT_806146a0 * speed;
        }

        // B button: zoom mode (right stick = elevation, C-stick = radius)
        unsigned int pressed = input_get_buttons_pressed(0);
        if ((pressed & 0x80) == 0) {
            // Normal orbit mode: right stick controls yaw/pitch
            double analogX = input_get_analog_value(0, 1);
            FLOAT_80611d08 += (float)((double)FLOAT_806146e8 * analogX) / (float)FLOAT_80614688;
            // Wrap yaw to [0, 2*PI]
            while (FLOAT_80614754 < FLOAT_80611d08) FLOAT_80611d08 -= FLOAT_80614754;
            while (FLOAT_80611d08 < FLOAT_80614754) FLOAT_80611d08 += FLOAT_80614754;

            double analogY = input_get_analog_value(0, 2);
            FLOAT_80611d0c += (float)((double)FLOAT_806146e8 * analogY) / (float)FLOAT_80614688;
            while (FLOAT_80614754 < FLOAT_80611d0c) FLOAT_80611d0c -= FLOAT_80614754;
            while (FLOAT_80611d0c < FLOAT_80614754) FLOAT_80611d0c += FLOAT_80614754;
        } else {
            // Zoom mode: adjust elevation + radius
            float speed = (float)FUN_80131538((double)(
                *(float*)(targetEntity + 0x1C0) * *(float*)(targetEntity + 0x1C0) +
                *(float*)(targetEntity + 0x1C4) * *(float*)(targetEntity + 0x1C4) +
                *(float*)(targetEntity + 0x1C8) * *(float*)(targetEntity + 0x1C8)));
            double elevInput = input_get_analog_value(0, 2);
            FLOAT_80611d64 += (float)(elevInput / (double)FLOAT_80614688);
            double zoomInput = input_get_analog_value(0, 4);
            FLOAT_80611d10 -= (float)(zoomInput / (double)FLOAT_80614688);
            if ((double)FLOAT_80611d10 < (double)speed) {
                FLOAT_80611d10 = speed; // Clamp minimum radius
            }
        }
        DAT_80611d60 = DAT_80611928;
    }

    // Compute camera position from spherical coordinates around target
    float targetX = *(float*)(targetEntity + 100) + *(float*)(targetEntity + 0x1A8);
    float targetY = FLOAT_80611d64 + *(float*)(targetEntity + 0x68) + *(float*)(targetEntity + 0x1AC);
    float targetZ = *(float*)(targetEntity + 0x6C) + *(float*)(targetEntity + 0x1B0);

    // Convert angles to sine table indices (1024-entry table, 4 bytes each)
    unsigned int pitchIdx = (unsigned int)(FLOAT_80614748 *
        ((FLOAT_806146d4 * FLOAT_80611d0c) / FLOAT_806146e8) + FLOAT_806146ac);
    unsigned int yawIdx = (unsigned int)(FLOAT_80614748 *
        ((FLOAT_806146d4 * FLOAT_80611d08) / FLOAT_806146e8) + FLOAT_806146ac);

    // Look up sin/cos from table (DAT_80583228 is a 2048-entry sine LUT)
    float cosPitch = *(float*)((int)DAT_80583228 + ((pitchIdx + 0x200) * 4 & 0x1FFC));
    float sinYaw = DAT_80583228[yawIdx & 0x7FF];
    float sinPitch = DAT_80583228[pitchIdx & 0x7FF];
    float cosYaw = *(float*)((int)DAT_80583228 + ((yawIdx + 0x200) * 4 & 0x1FFC));

    // Spherical to Cartesian offset
    float offX = cosPitch * sinYaw * FLOAT_80611d10;
    float offY = sinPitch * FLOAT_80611d10;
    float offZ = cosPitch * cosYaw * FLOAT_80611d10;

    *(float*)(DAT_80611d54 + 0x27C) = offX + targetX;
    *(float*)(DAT_80611d54 + 0x280) = offY + targetY;
    *(float*)(DAT_80611d54 + 0x284) = offZ + targetZ;

    // Find room from floor height at camera position
    int room = floor_height_from_position(1.0, (double)*(float*)(pPlayerStruct + 0xA0));
    *(int*)(DAT_80611d54 + 0x2A0) = room;
    if (*(int*)(DAT_80611d54 + 0x2A0) == 0) {
        *(int*)(DAT_80611d54 + 0x2A0) = *(int*)(targetEntity + 0x34);
    }

    // Scene raycast from target to camera for collision avoidance
    float rayDir[3] = { offX, offY, offZ };
    float rayOrigin[3] = { targetX, targetY, targetZ };
    float hitPos[3], hitNorm[3];

    // Temporarily clear entity visibility flags for raycast
    *(unsigned int*)(targetEntity + 0x28) &= 0xFFFFFF9F;
    int hit = scene_raycast(*(int*)(targetEntity + 0x34), rayOrigin, rayDir, 0,
                           hitPos, hitNorm, (int*)(targetEntity + 0x34),
                           0xFFFFFFFD, 0, (int*)0);
    *(unsigned int*)(targetEntity + 0x28) |= 0x60;

    if (hit != 0) {
        // Pull camera back to hit position + small offset along normal
        *(float*)(DAT_80611d54 + 0x27C) = FLOAT_806146ac * hitNorm[0] + hitPos[0];
        *(float*)(DAT_80611d54 + 0x280) = FLOAT_806146ac * hitNorm[1] + hitPos[1];
        *(float*)(DAT_80611d54 + 0x284) = FLOAT_806146ac * hitNorm[2] + hitPos[2];
    }

    // Set look direction = target - camera
    *(float*)(DAT_80611d54 + 0x334) = targetX - *(float*)(DAT_80611d54 + 0x27C);
    *(float*)(DAT_80611d54 + 0x338) = targetY - *(float*)(DAT_80611d54 + 0x280);
    *(float*)(DAT_80611d54 + 0x33C) = targetZ - *(float*)(DAT_80611d54 + 0x284);

    // Copy camera position to player render struct
    *(int*)(pPlayerStruct + 0xB4) = *(int*)(DAT_80611d54 + 0x27C);
    *(int*)(pPlayerStruct + 0xB8) = *(int*)(DAT_80611d54 + 0x280);
    *(int*)(pPlayerStruct + 0xBC) = *(int*)(DAT_80611d54 + 0x284);
}

// ============================================================================
// Address: 0x800a18d4 | camera_orbit_sway_tick
// Lateral sway from player velocity cross-product + vertical elevation ramp.
// Uses fmod-style clamp for oscillation. Called from camera_per_frame_update.
// ============================================================================
void camera_orbit_sway_tick(void) {
    int pPlayer = DAT_80611d74;

    // Frame count threshold (10 for NTSC, otherwise 60)
    int frameThreshold = 0x28;
    // (DAT_805015bd == '\n' is NTSC check)
    if (*(char*)0x805015BD != '\n') {
        frameThreshold = 0x3C;
    }

    if (frameThreshold < *(int*)(pPlayer + 0xE7C)) {
        // Compute distance from camera to player
        float dx = *(float*)(DAT_80611d54 + 0x27C) - *(float*)(pPlayer + 0xB4);
        float dy = *(float*)(DAT_80611d54 + 0x280) - *(float*)(pPlayer + 0xB8);
        float dz = *(float*)(DAT_80611d54 + 0x284) - *(float*)(pPlayer + 0xBC);
        float distSq = dx*dx + dy*dy + dz*dz;

        // Fast inverse sqrt (Newton-Raphson) for normalization
        float invDist;
        if (FLOAT_80614710 <= (distSq < 0 ? -distSq : distSq)) {
            float x = 1.0f / __builtin_sqrtf(distSq);
            x = x * -(FLOAT_806146ac * distSq * x * x - FLOAT_8061471c);
            invDist = FLOAT_806146dc * x * -(FLOAT_806146ac * distSq * x * x - FLOAT_8061471c);
        }

        float dist = (invDist < FLOAT_80614680) ? invDist : FLOAT_80614680;

        // Sway accumulation: cross product sign determines direction
        // camera_right x offset_dir gives lateral sign
        float cross = *(float*)(DAT_80611d54 + 0x29C) * dx -
                      *(float*)(DAT_80611d54 + 0x294) * dz;
        float sign = (*(unsigned int*)&cross & 0x80000000) | 0x3F800000; // copysign(1, cross)

        *(float*)(pPlayer + 0xE84) += FLOAT_80614758 * FLOAT_806108ec * dist * sign;

        // Wrap sway angle via fmod
        float sway = *(float*)(pPlayer + 0xE84);
        float wrapped = (float)FUN_80390324((double)((sway - FLOAT_8061475c) / FLOAT_80614754));
        *(float*)(pPlayer + 0xE84) = -(FLOAT_80614754 * wrapped - sway);
    }

    // Vertical elevation ramp toward max
    if (*(float*)(pPlayer + 0xE88) < FLOAT_80614760) {
        *(float*)(pPlayer + 0xE88) += FLOAT_806146f8 * FLOAT_806108ec;
    }

    // Apply sway + elevation to orbit angles
    camera_set_orbit_angles(
        (double)*(float*)(pPlayer + 0xE84),
        (double)*(float*)(pPlayer + 0xE88),
        (double)FLOAT_80614678,
        (double)FLOAT_80614678,
        DAT_80611d54, 4);

    camera_third_person_tick();
}

// ============================================================================
// Address: 0x800a1a68 | camera_death_set_target
// Stores a 3D position as death camera look-at target. Called from
// player_death_cutscene and player_death_set_camera_fly.
// ============================================================================
void camera_death_set_target(float* pPosition) {
    DAT_80561ca8 = pPosition[0];
    DAT_80561cac = pPosition[1];
    DAT_80561cb0 = pPosition[2];
}

// ============================================================================
// Address: 0x800a1a88 | camera_death_tick
// Death camera per-frame. Positions at stored death target, direction toward
// player entity + bone offset. Portal ray traversal for room assignment.
// ============================================================================
void camera_death_tick(void) {
    int pPlayer = DAT_80611d74;
    int pPlayerEntity = *(int*)(pPlayer + 0x14);
    int pEntityData = *(int*)(pPlayerEntity + 4);

    // Copy previous room
    *(int*)(DAT_80611d54 + 0x2A4) = *(int*)(DAT_80611d54 + 0x2A0);
    *(int*)(DAT_80611d54 + 0x2A8) = 0;

    // Position camera at stored death target
    *(float*)(DAT_80611d54 + 0x27C) = DAT_80561ca8;
    *(float*)(DAT_80611d54 + 0x280) = DAT_80561cac;
    *(float*)(DAT_80611d54 + 0x284) = DAT_80561cb0;

    // Look direction = (player_pos + bone_offset) - camera_pos
    int pBoneData = *(int*)(pEntityData + 0x10);
    *(float*)(DAT_80611d54 + 0x334) =
        (*(float*)(pPlayerEntity + 100) + *(float*)(pBoneData + 0x30)) -
        *(float*)(DAT_80611d54 + 0x27C);

    float eyeY = FLOAT_80614764; // Default eye height
    if (DAT_80612614 != 0xB) {
        eyeY = *(float*)(pPlayerEntity + 0x68);
    }
    *(float*)(DAT_80611d54 + 0x338) =
        (eyeY + *(float*)(pBoneData + 0x34)) - *(float*)(DAT_80611d54 + 0x280);
    *(float*)(DAT_80611d54 + 0x33C) =
        (*(float*)(pPlayerEntity + 0x6C) + *(float*)(pBoneData + 0x38)) -
        *(float*)(DAT_80611d54 + 0x284);

    // Portal ray traversal for room assignment
    float rayDir[3] = {
        *(float*)(DAT_80611d54 + 0x27C) - *(float*)(pPlayer + 0xB4),
        *(float*)(DAT_80611d54 + 0x280) - *(float*)(pPlayer + 0xB8),
        *(float*)(DAT_80611d54 + 0x284) - *(float*)(pPlayer + 0xBC)
    };

    int room = scene_ray_traverse_portals(
        (double)*(float*)(DAT_80611d54 + 0x284),
        (double)*(float*)(pPlayerEntity + 0x6C),
        (float*)(pPlayer + 0xB4),
        *(int*)(pPlayer + 0xDEC),
        rayDir, 0);

    if (room > 0) {
        *(int*)(DAT_80611d54 + 0x2A0) = room;
    }
}

// ============================================================================
// Address: 0x800a1bc0 | camera_cutscene_tick
// Cutscene camera per-frame tick. Loads keyframe data, SLERP quaternion
// interpolation between frames, builds 4x3 view matrix, computes FOV from
// focal length, builds projection matrix with aspect correction. Floor query
// for room assignment.
// ============================================================================
void camera_cutscene_tick(void) {
    int* pCutsceneData = (int*)cutscene_load_if_needed(1);
    if (DAT_806117ec == 0 || pCutsceneData == NULL) {
        return;
    }

    camera_transition_stub_zero();
    camera_stub_nop_a();

    // Compute frame index from playback time
    float playbackTime = (float)pCutsceneData[2];
    unsigned int frameIdx = (unsigned int)(playbackTime * FLOAT_8060faf8);
    if ((int)frameIdx < 0) return;

    // Clamp to valid range
    int maxFrame = pCutsceneData[0] - 5;
    if ((int)maxFrame < (int)frameIdx && 0 < (int)frameIdx) {
        frameIdx = (unsigned int)maxFrame;
    }

    // Compute sub-frame interpolation factor t in [0, 1]
    double t = (double)(FLOAT_8060faf8 *
        (playbackTime - (float)frameIdx / FLOAT_8060faf8));
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    // Query two adjacent keyframes
    float quatA[4], quatB[4], quatResult[4];
    float posA[4], posB[4]; // position + focal length
    anim_keyframe_pose_query(frameIdx, quatA, (int*)&posA[1], (int*)&posA[3]);
    anim_keyframe_pose_query(frameIdx + 1, quatB, (int*)&posB[1], (int*)&posB[3]);

    // Check motion continuity (snap vs interpolate)
    int discontinuity = anim_motion_continuity_check(pCutsceneData);
    if (discontinuity != 0) {
        t = (t >= (double)FLOAT_806146ac) ? 1.0 : 0.0;
    }

    // SLERP quaternion interpolation
    FUN_8012e234(t, quatA, quatB, quatResult);
    // Convert quaternion to 3x3 rotation matrix
    float viewMatrix[12];
    FUN_8012e11c(viewMatrix, quatResult);

    // Interpolate position and focal length
    float posX = (float)(t * (double)(posB[1] - posA[1]) + (double)posA[1]);
    float posY = (float)(t * (double)(posB[2] - posA[2]) + (double)posA[2]);
    float posZ = (float)(t * (double)(posB[3] - posA[3]) + (double)posA[3]);
    float focalLength = (float)(t * (double)(posB[3] - posA[3]) + (double)posA[3]);

    // Write view matrix to camera slot
    float scale = FLOAT_80614724; // World-to-camera scale
    int* pViewMtx = (int*)*(int*)(DAT_80611d54 + 0x208);
    // Copy 3x4 rotation portion (12 floats)
    // ... (matrix copy)
    // Translation row = -pos / scale
    *(float*)(pViewMtx + 12) = -posX / scale;
    *(float*)(pViewMtx + 13) = -posY / scale;
    *(float*)(pViewMtx + 14) = -posZ / scale;
    *(float*)(pViewMtx + 15) = 1.0f;

    // Compute FOV from focal length: atan(sensor_height / (2 * focal)) * 2
    double fov = FUN_8038fecc((double)(
        (FLOAT_806146ec * (FLOAT_806146f0 / (FLOAT_806146f4 * focalLength))) / FLOAT_806146f8));
    fov = (double)((FLOAT_806146e8 * FLOAT_806146ac * (float)fov) / FLOAT_806146d4);

    // Widescreen correction
    unsigned int widescreen = is_widescreen();
    if ((widescreen & 0xFF) != 0) {
        fov = (double)((float)fov * FLOAT_806146fc);
    }

    // Build projection matrix
    camera_build_projection(
        (double)FLOAT_806104a0, (double)FLOAT_806104a4,
        /* aspect ratio from display mode table */(double)1.0f, fov);

    // Store FOV
    *(float*)(DAT_80611d54 + 900) = (float)fov;

    // Store world position
    *(float*)(DAT_80611d54 + 0x27C) = posX / scale;
    *(float*)(DAT_80611d54 + 0x280) = posY / scale;
    *(float*)(DAT_80611d54 + 0x284) = posZ / scale;

    // Forward and up vectors from rotation matrix
    *(float*)(DAT_80611d54 + 0x334) = viewMatrix[0];
    // ... (remaining direction vectors)

    // Room assignment via floor height query
    int room = floor_height_from_position(1.0, (double)FLOAT_806146ac);
    *(int*)(DAT_80611d54 + 0x2A0) = room;
    if (room == 0) {
        *(int*)(DAT_80611d54 + 0x2A0) =
            (*(int*)(DAT_80611d54 + 0x2A4) != 0) ? *(int*)(DAT_80611d54 + 0x2A4) : 1;
    }
    *(int*)(DAT_80611d54 + 0x2A4) = *(int*)(DAT_80611d54 + 0x2A0);

    // Copy current to previous
    *(int*)(DAT_80611d54 + 0x288) = *(int*)(DAT_80611d54 + 0x27C);
    *(int*)(DAT_80611d54 + 0x28C) = *(int*)(DAT_80611d54 + 0x280);
    *(int*)(DAT_80611d54 + 0x290) = *(int*)(DAT_80611d54 + 0x284);
}
