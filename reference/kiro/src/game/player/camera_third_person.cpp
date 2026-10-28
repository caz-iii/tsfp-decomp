// Address: 0x800a26e4-0x800a27c8 | Name: camera_third_person | Date: 2026-08-11
// Third-person camera math utilities. Cartesian <-> spherical coordinate
// conversion for orbit/third-person camera positioning.

#include "common.h"

extern float FLOAT_80614678;   // 0.0f
extern float FLOAT_806146d8;   // PI/2
extern float FLOAT_80614710;   // Min distance squared epsilon

extern double FUN_80131538(double val);   // sqrtf
extern double FUN_80390aa4(double y, double x);  // atan2f
extern double FUN_801314f4(double val);   // asinf
extern double FUN_801313a4(double val);   // sinf
extern void FUN_803826f0(void);  // paired-single save (variant)
extern void FUN_8038273c(void);  // paired-single restore (variant)

// ============================================================================
// Address: 0x800a26e4 | camera_compute_spherical
// Converts cartesian delta (target - source) to spherical coordinates:
//   pOut[0] = radius (sqrt of distance squared)
//   pOut[1] = yaw (atan2 of -Z, -X)
//   pOut[2] = pitch (asin of -Y/radius - PI/2)
// Returns zero vector if distance is below epsilon.
// ============================================================================
void camera_compute_spherical(float* pOut, float* pTarget, float* pSource) {
    float dx = pSource[0] - pTarget[0];
    float dy = pSource[1] - pTarget[1];
    float dz = pSource[2] - pTarget[2];

    float distSq = dx * dx + dy * dy + dz * dz;

    if (fabsf(distSq) >= FLOAT_80614710) {
        // Valid distance - compute spherical coords
        pOut[0] = (float)FUN_80131538((double)distSq); // radius
        pOut[1] = (float)FUN_80390aa4((double)(-dz), (double)(-dx)); // yaw
        pOut[2] = (float)FUN_801314f4((double)(-dy / pOut[0])) - FLOAT_806146d8; // pitch
    } else {
        // Too close - return zero
        pOut[0] = FLOAT_80614678;
        pOut[1] = FLOAT_80614678;
        pOut[2] = FLOAT_80614678;
    }
}

// ============================================================================
// Address: 0x800a27c8 | camera_spherical_to_position
// Spherical to world position: radius * cos/sin(elevation/yaw) + origin offset.
// Inverse of camera_compute_spherical.
//   Input spherical: [radius, yaw, pitch]
//   Output: world position = spherical_offset + pOrigin
// ============================================================================
void camera_spherical_to_position(float* pOut, float* pSpherical, float* pOrigin) {
    FUN_803826f0(); // Save PS regs

    float radius = pSpherical[0];
    float yaw = pSpherical[1];
    float pitch = pSpherical[2];

    // elevation = PI/2 + pitch
    float elevation = FLOAT_806146d8 + pitch;

    // cos(elevation - PI/2) = cos(pitch) effectively
    float cosElev = (float)FUN_801313a4((double)(elevation - FLOAT_806146d8));
    // sin(yaw)
    float sinYaw = (float)FUN_801313a4((double)yaw);

    // X = cosElev * radius * sinYaw
    pOut[0] = cosElev * (radius * sinYaw);

    // sin(elevation) = sin(PI/2 + pitch) = cos(pitch)
    float sinElev = (float)FUN_801313a4((double)elevation);
    // Y = radius * sinElev
    pOut[1] = radius * sinElev;

    // sin(yaw - PI/2) = -cos(yaw)
    float negCosYaw = (float)FUN_801313a4((double)(yaw - FLOAT_806146d8));
    // Z = cosElev * radius * (-cosYaw)
    pOut[2] = cosElev * (radius * negCosYaw);

    // Add origin offset
    pOut[0] += pOrigin[0];
    pOut[1] += pOrigin[1];
    pOut[2] += pOrigin[2];

    FUN_8038273c(); // Restore PS regs
}

// ============================================================================
// Address: 0x800a28b4 | camera_third_person_tick
// MASSIVE 3rd-person chase camera. Spring position tracking, yaw/pitch orbit
// with wrapping, velocity substep integration (N*5), scene_swept_aabb_query
// collision (5-iter push-out), vehicle mode, mapmaker ceiling, portal room,
// camera tilt via axis-angle rotation.
// ============================================================================
void camera_third_person_tick(void) {
    FUN_803826e0(); // Save PS regs

    if (DAT_806108e4 == 0) goto done;

    // Compute spring constant from velocity magnitude
    float velMagSq = *(float*)(DAT_80611d54 + 0xFF4);
    double velMag = FUN_80131538((double)velMagSq);
    double springDamping = (double)(FLOAT_80614778 * (float)velMag);

    // Compute orbit radius from lateral velocity (fast inv sqrt)
    float lateralSq = *(float*)(DAT_80611d54 + 0xFEC) * *(float*)(DAT_80611d54 + 0xFEC) +
                      *(float*)(DAT_80611d54 + 0xFF0) * *(float*)(DAT_80611d54 + 0xFF0);
    // Newton-Raphson approximate sqrt
    float approxDist = (float)(((int)lateralSq >> 1) + 0x1FC00000);
    double orbitRadius = (double)((approxDist * approxDist + lateralSq) /
                         (FLOAT_806146a0 * approxDist));

    // Delta time normalized by frame count
    double dt = FUN_801447f0();
    dt = (double)(FLOAT_80614680 / (float)DAT_806109c0) * dt;

    // Combined spring+damping factor
    double springFactor = (double)(*(float*)(DAT_80611d54 + 0x2EC) +
                          *(float*)(DAT_80611d54 + 0x2F4));

    camera_shake_reset_all();

    // Get player/vehicle position as orbit target
    float targetPos[3], targetDir[3];
    int pEntity;
    if (*(int*)(DAT_80611d54 + 0x218) == 0x20) {
        // Vehicle mode: get position from vehicle entity
        // ... (vehicle position query)
    } else {
        // Standard: use player render position
        pEntity = *(int*)(DAT_80611d74 + 0x14);
        targetPos[0] = *(float*)(DAT_80611d74 + 0xB4);
        targetPos[1] = *(float*)(DAT_80611d74 + 0xB8);
        targetPos[2] = *(float*)(DAT_80611d74 + 0xBC);
        targetDir[0] = *(float*)(DAT_80611d74 + 0xC0);
        targetDir[1] = *(float*)(DAT_80611d74 + 0xC4);
        targetDir[2] = *(float*)(DAT_80611d74 + 0xC8);
    }

    // Temporarily clear entity visibility flags for sweep
    // (prevents camera from colliding with player/weapon)

    // Compute normalized direction from camera to target
    float camToTarget[3];
    camToTarget[0] = *(float*)(DAT_80611d54 + 0x27C) - targetPos[0];
    camToTarget[1] = *(float*)(DAT_80611d54 + 0x280) - targetPos[1];
    camToTarget[2] = *(float*)(DAT_80611d54 + 0x284) - targetPos[2];
    // Normalize camToTarget (fast inv sqrt + Newton-Raphson)
    // Then scale by orbit radius to get desired offset

    // Yaw spring: wrap-aware angular interpolation
    float currentYaw = *(float*)(DAT_80611d54 + 0xFE4);
    float targetYaw = *(float*)(DAT_80611d54 + 0xFDC);
    // Find shortest angular path (3-way wrap comparison)
    float yawDelta = /* shortest wrap */ targetYaw - currentYaw;
    currentYaw += yawDelta * FLOAT_8061477c * (float)(dt * springFactor);
    // Wrap result via fmod
    *(float*)(DAT_80611d54 + 0xFE4) = /* fmod wrapped */ currentYaw;

    // Pitch spring: same wrap-aware interpolation
    float currentPitch = *(float*)(DAT_80611d54 + 0xFE8);
    float targetPitch = *(float*)(DAT_80611d54 + 0xFE0);
    // ... pitch spring with FLOAT_80614780 rate
    *(float*)(DAT_80611d54 + 0xFE8) = /* fmod wrapped */ currentPitch;

    // Compute spherical coordinates and desired position
    float spherical[3];
    camera_compute_spherical(spherical, targetPos + /* offset by radius */, targetPos);
    // Apply yaw from player facing direction
    float facingYaw = (float)FUN_80390aa4(-(double)targetDir[2], -(double)targetDir[0]);
    spherical[1] = facingYaw + (FLOAT_806146d4 * currentPitch) / FLOAT_806146e8;
    // Pitch from velocity Y component
    spherical[2] = (float)FUN_801314b0((double)(
        -*(float*)(DAT_80611d54 + 0xFF0) / (float)orbitRadius));
    camera_spherical_to_position(targetPos /* out */, spherical, targetPos);

    // Velocity substep integration (N*5 iterations)
    // Integrates camera velocity toward desired position with spring damping
    unsigned int substeps = DAT_806108e4 * 5;
    float subDt = (float)(dt / (double)substeps);
    float camX = *(float*)(DAT_80611d54 + 0x27C);
    float camY = *(float*)(DAT_80611d54 + 0x280);
    float camZ = *(float*)(DAT_80611d54 + 0x284);
    float velX = *(float*)(DAT_80611d54 + 0x294);
    float velY = *(float*)(DAT_80611d54 + 0x298);
    float velZ = *(float*)(DAT_80611d54 + 0x29C);
    float springK = *(float*)(DAT_80611d54 + 0xFF4);

    for (unsigned int i = 0; i < substeps; i++) {
        // Acceleration = spring force toward target + damping
        float accX = (targetPos[0] - camX) * springK + velX * (float)springDamping;
        float accY = (targetPos[1] - camY) * springK + velY * (float)springDamping;
        float accZ = (targetPos[2] - camZ) * springK + velZ * (float)springDamping;
        // Integrate velocity
        velX += accX * subDt;
        velY += accY * subDt;
        velZ += accZ * subDt;
        // Integrate position
        camX += velX * subDt;
        camY += velY * subDt;
        camZ += velZ * subDt;
    }

    // Compute movement delta from previous frame
    float moveDir[3] = {
        camX - *(float*)(DAT_80611d54 + 0x27C),
        camY - *(float*)(DAT_80611d54 + 0x280),
        camZ - *(float*)(DAT_80611d54 + 0x284)
    };

    // Scene swept AABB collision (5 iterations of push-out)
    // Prevents camera from clipping through walls
    int hitCount = scene_swept_aabb_query(/* room */, targetPos, moveDir, 0,
                                          /* hitList */, 0x78, 0x44000);
    if (hitCount != 0) {
        // 5-pass iterative push along collision normals
        for (int pass = 0; pass < 5; pass++) {
            for (/* each hit plane */) {
                float dot = /* normal . moveDir */;
                if (dot < 0.0f) {
                    float pushDist = -(dot * FLOAT_806146ac);
                    moveDir[0] += /* normal.x */ * pushDist;
                    moveDir[1] += /* normal.y */ * pushDist;
                    moveDir[2] += /* normal.z */ * pushDist;
                }
            }
        }
    }

    // Apply final position
    *(float*)(DAT_80611d54 + 0x27C) += moveDir[0];
    *(float*)(DAT_80611d54 + 0x280) += moveDir[1];
    *(float*)(DAT_80611d54 + 0x284) += moveDir[2];
    *(float*)(DAT_80611d54 + 0x294) = velX;
    *(float*)(DAT_80611d54 + 0x298) = velY;
    *(float*)(DAT_80611d54 + 0x29C) = velZ;

    // Second swept query (vehicle or standard) for additional collision

    // Mapmaker ceiling clamp (level 100)
    if (DAT_80612614 == 100) {
        // Clamp camera Y to room ceiling height - epsilon
    }

    // Compute look direction = target - camera (normalized)
    *(float*)(DAT_80611d54 + 0x334) = targetPos[0] - *(float*)(DAT_80611d54 + 0x27C);
    *(float*)(DAT_80611d54 + 0x338) = targetPos[1] - *(float*)(DAT_80611d54 + 0x280);
    *(float*)(DAT_80611d54 + 0x33C) = targetPos[2] - *(float*)(DAT_80611d54 + 0x284);
    // Normalize look direction (fast inv sqrt)

    // Portal ray traversal for room assignment
    int newRoom = scene_ray_traverse_portals(/* ... */);
    if (newRoom > 0 && newRoom < 0xC9) {
        *(int*)(DAT_80611d54 + 0x2A0) = newRoom;
    }

    // Restore entity visibility flags

    // Camera tilt: axis-angle rotation of look direction around cross(look, up)
    if (fabsf(*(float*)(DAT_80611d54 + 0xFE4)) > FLOAT_80614708) {
        // Build rotation matrix from tilt angle
        // Apply to look direction via quaternion multiply
    }

done:
    FUN_8038272c(); // Restore PS regs
}

// ============================================================================
// Address: 0x800a35b0 | camera_shoulder_tick
// Over-shoulder camera. Ramp-up acceleration, yaw spring, 4-point spline
// rotated by yaw matrix, Catmull-Rom eval, swept collision, portal room,
// mode transition on completion (0x19->4, else->0+flag4).
// ============================================================================
void camera_shoulder_tick(void) {
    FUN_803826e0(); // Save PS regs

    int pEntity = *(int*)(DAT_80611d74 + 0x14);
    // Temporarily clear weapon entity visibility for sweep

    // Ramp-up: acceleration curve using abs(0.5 - t) for smooth start/end
    float t = *(float*)(DAT_80611d54 + 0xFC8);
    float frameDt = FLOAT_806108ec * (FLOAT_80614680 / (float)DAT_806109c0);
    if (t < FLOAT_80614680) {
        t += FLOAT_80614790 * frameDt *
             (FLOAT_80614788 + (FLOAT_806146ac - fabsf(FLOAT_806146ac - t)));
    }
    if (t > FLOAT_80614680) t = FLOAT_80614680;
    *(float*)(DAT_80611d54 + 0xFC8) = t;

    // Yaw spring: wrap-aware interpolation toward player facing
    float playerYaw;
    FUN_801311d8(DAT_80611d74 + 0xC0, 0, &playerYaw);
    // ... angular spring with wrap

    // Build yaw rotation matrix
    float yawMatrix[16];
    FUN_8012f05c((double)(/* angle */), yawMatrix);

    // Load 4 spline control points from camera mode data, rotate by yaw
    float controlPts[4][3];
    for (int i = 0; i < 4; i++) {
        // Read from *(int*)(DAT_80611d54 + 0x26C) + offset
        // Rotate each point by yawMatrix
        FUN_8012fb90(yawMatrix, &controlPts[i]);
    }

    // Catmull-Rom spline evaluation at parameter t
    float localOffset[3];
    FUN_801317b8((double)t, localOffset, controlPts[0], controlPts[1],
                 controlPts[2], controlPts[3]);

    // Swept AABB query from player position along offset
    int hitCount = scene_swept_aabb_query(/* room */, /* playerPos */, localOffset, 0,
                                          /* hitList */, 0x78, 0x44000);
    if (hitCount != 0) {
        // Snap to first blocking hit point + offset along normal
        // (single-pass, break on first blocking plane)
    }

    // Restore entity visibility flags

    // Final camera position = player + offset
    *(float*)(DAT_80611d54 + 0x27C) = localOffset[0] + *(float*)(DAT_80611d74 + 0xB4);
    *(float*)(DAT_80611d54 + 0x280) = localOffset[1] + *(float*)(DAT_80611d74 + 0xB8);
    *(float*)(DAT_80611d54 + 0x284) = localOffset[2] + *(float*)(DAT_80611d74 + 0xBC);

    // Look direction = -offset (look back at player)
    *(float*)(DAT_80611d54 + 0x334) = -localOffset[0];
    *(float*)(DAT_80611d54 + 0x338) = -localOffset[1];
    *(float*)(DAT_80611d54 + 0x33C) = -localOffset[2];

    // Portal ray for room assignment
    int newRoom = scene_ray_world_collision(/* ... */);
    if (newRoom != 0) {
        *(int*)(DAT_80611d54 + 0x2A0) = newRoom;
    }

    // Mode transition when spline complete (t >= 1.0)
    if (t >= FLOAT_80614680 && *(int*)(DAT_80611d74 + 0x114) != 0x400) {
        if (*(int*)(DAT_80611d54 + 0x218) == 0x19) {
            camera_set_mode_by_index(DAT_80611d54, 4, pEntity);
        } else {
            camera_set_mode_by_index(DAT_80611d54, 0, pEntity);
            *(unsigned int*)(DAT_80611d54 + 0x21C) |= 4;
        }
    }

    FUN_8038272c(); // Restore PS regs
}

// ============================================================================
// Address: 0x800a3ae0 | camera_set_orbit_params
// Writes orbit config directly to slot: velocity X/Z, target yaw/pitch,
// spring constants, radius.
// ============================================================================
void camera_set_orbit_params(double velX, double velZ, double targetYaw, double targetPitch,
                             double springYaw, double springPitch, double yawAngle,
                             double pitchAngle, int pSlot) {
    *(float*)(pSlot + 0x2E8) = (float)targetYaw;
    *(float*)(pSlot + 0x2EC) = (float)targetPitch;
    *(float*)(pSlot + 0x2F0) = (float)springPitch;
    *(float*)(pSlot + 0x2F4) = (float)springYaw;
    // Additional params from stack: radius, damping, etc.
    *(float*)(pSlot + 0xFEC) = (float)velX;
    *(float*)(pSlot + 0xFF0) = (float)velZ;
    *(float*)(pSlot + 0xFE0) = (float)yawAngle;
    *(float*)(pSlot + 0xFDC) = (float)pitchAngle;
}

// ============================================================================
// Address: 0x800a3b24 | camera_set_orbit_angles
// Sets orbit angles from 0x20-byte table entry (DAT_80402fb0) indexed by mode.
// 9 xrefs from camera_set_mode, sway tick, render callbacks.
// ============================================================================
void camera_set_orbit_angles(double yawAngle, double pitchAngle, double param3,
                             double param4, int pSlot, int modeIndex) {
    int tableOffset = modeIndex * 0x20;
    // Copy 7 floats from table at DAT_80402fb0 + offset
    *(int*)(pSlot + 0x2E8) = *(int*)(0x80402FB8 + tableOffset);
    *(int*)(pSlot + 0x2EC) = *(int*)(0x80402FBC + tableOffset);
    *(int*)(pSlot + 0x2F0) = *(int*)(0x80402FC4 + tableOffset);
    *(int*)(pSlot + 0x2F4) = *(int*)(0x80402FC0 + tableOffset);
    *(float*)(pSlot + 0x2F8) = (float)param4;
    *(float*)(pSlot + 0x2FC) = (float)param3;
    *(int*)(pSlot + 0x300) = *(int*)(0x80402FC8 + tableOffset);
    *(int*)(pSlot + 0xFEC) = *(int*)(0x80402FB0 + tableOffset);
    *(int*)(pSlot + 0xFF0) = *(int*)(0x80402FB4 + tableOffset);
    *(int*)(pSlot + 0xFF4) = *(int*)(0x80402FCC + tableOffset);
    *(float*)(pSlot + 0xFE0) = (float)yawAngle;
    *(float*)(pSlot + 0xFDC) = (float)pitchAngle;
}

// ============================================================================
// Address: 0x800a3b88 | camera_get_split_mask
// Returns split-screen camera mask.
// ============================================================================
int camera_get_split_mask(void) {
    return DAT_80611d18;
}

// ============================================================================
// Address: 0x800a3b90 | camera_set_split_mask
// Sets split-screen camera mask.
// ============================================================================
void camera_set_split_mask(int mask) {
    DAT_80611d18 = mask;
}

// ============================================================================
// Address: 0x800a3b98 | camera_splitscreen_setup
// Split-screen pause camera setup. Renders weapon entities, gives requesting
// player full viewport, disables other player's interpolation.
// ============================================================================
void camera_splitscreen_setup(void) {
    int* pPauseData = (int*)FUN_803826f0();

    // Render weapon entities for all active players
    int stride = 0;
    for (int i = 0; i < DAT_80612610; i++) {
        int pCharData = *(int*)(*(int*)(DAT_80611d78 + stride + 0x14) + 0x7C);
        // Resolve weapon entity from packed ref
        unsigned int weaponRef = *(unsigned int*)(pCharData + 0xC08);
        int pWeapon = 0;
        if (weaponRef != 0) {
            pWeapon = DAT_80611c24 + (weaponRef >> 22) * 0x2E8;
            if (*(unsigned int*)(pWeapon + 0x2C) != weaponRef) {
                pWeapon = 0;
                *(int*)(pCharData + 0xC08) = 0;
            }
        }
        if (pWeapon != 0) {
            FUN_80153344(pWeapon, *(int*)(DAT_80611d78 + stride + 0x14), 0, 0, 0);
        }
        stride += 0x141C;
    }

    // NTSC 2-player: give pausing player full viewport
    if (DAT_805015bd == '\n' && DAT_80612610 > 1) {
        int pausingPlayer = pPauseData[0];
        int pSlot = pPauseData[2];

        // Set full-screen viewport on pausing player's slot
        FUN_8001c710((int*)(pSlot + 0x224), 0, 0, DAT_806109c4 - 1, DAT_806109c8 - 1);

        if (pausingPlayer == 0) {
            // P0 paused: disable P1 interpolation
            int pP1Slot = *(int*)(DAT_80611d78 + 0x1424);
            if (pP1Slot != 0) {
                *(unsigned int*)(pP1Slot + 0x21C) &= 0xFFFFFFFE;
                *(unsigned int*)(DAT_80611d78 + 0x1428) |= 2;
            }
        } else {
            // P1 paused: disable P0 interpolation
            int pP0Slot = *(int*)(DAT_80611d78 + 8);
            if (pP0Slot != 0) {
                *(unsigned int*)(pP0Slot + 0x21C) &= 0xFFFFFFFE;
                *(unsigned int*)(DAT_80611d78 + 0xC) |= 1;
            }
        }

        DAT_80611d18 = 1 << pausingPlayer; // Split mask = pausing player only
    }

    FUN_8038273c(); // Restore PS regs
}

// ============================================================================
// Address: 0x800a3d30 | camera_splitscreen_restore
// Restores split-screen cameras after pause. Re-enables interpolation, clears
// viewport override flags, resets split mask.
// ============================================================================
void camera_splitscreen_restore(void) {
    if (DAT_805015bd != '\n' || DAT_80612610 <= 1) return;

    // Re-enable P0 interpolation
    *(unsigned int*)(*(int*)(DAT_80611d78 + 8) + 0x21C) |= 1;
    FUN_80259354(); // Recalculate viewport

    // Re-enable P1 interpolation
    int pP1Slot = *(int*)(DAT_80611d78 + 0x1424);
    if (pP1Slot != 0) {
        *(unsigned int*)(pP1Slot + 0x21C) |= 1;
        FUN_80259354();
    }

    // Clear override flags
    *(unsigned int*)(DAT_80611d78 + 0xC) &= 0xFFFFFFFE;
    *(unsigned int*)(DAT_80611d78 + 0x1428) &= 0xFFFFFFFD;

    DAT_80611d18 = 0; // Clear split mask
}
