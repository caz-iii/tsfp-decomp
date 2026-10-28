// =============================================================================
// Player AI Projectile Guided/Arrow Systems
// Address Range: 0x800be878 - 0x800c1420
// Date: 2026-08-11
// Batch: 267
//
// This module handles guided (homing/seeking) projectiles, arrow/bolt
// projectiles, and supporting systems:
//   - Simple homing direction steer (normalize + entity yaw/pitch)
//   - Angular momentum steer (cross-product torque + quaternion rotation)
//   - Projectile visible entity spawn (type-based model 0x743/0x744/0x745)
//   - MAIN guided projectile tick (lifetime, speed, steering, raycast, explosion)
//   - Targeting reticle render (3 ring sprites in screen space)
//   - Arrow entity spawn (type 0x20/0x21, definition table, back-pointer)
//   - Arrow lifetime fade check (alpha decay + destroy)
//   - MAIN arrow projectile tick (stuck-in-entity bone tracking, gravity, damage)
//   - Wobble/spiral init (random angular velocity, radii, target)
//   - Contact damage dispatch (character voice + surface switch/damage)
// =============================================================================
#include "common.h"
#include "player_ai.h"
#include "particle.h"
#include "sound.h"
#include "collision.h"
#include "scene.h"
#include "entity.h"

// Globals
extern int DAT_80611d54;    // player controller pointer
extern int DAT_80611d78;    // player data array base
extern int DAT_80611748;    // camera matrix pointer
extern int DAT_80610a20;    // texture data pointer
extern int DAT_80612610;    // sound source threshold
extern int DAT_806108e4;    // frame valid count
extern int DAT_806109c0;    // frame counter
extern float FLOAT_806108ec; // frame delta time
extern char DAT_805015bd;   // game type flag
extern char DAT_805015c0;   // game mode flags
extern float FLOAT_80614a5c; // 0.0f
extern float FLOAT_80614a60; // 1.0f
extern float FLOAT_80614a70; // small offset
extern float FLOAT_80614a78; // 0.5f
extern float FLOAT_80614a7c; // 1.5f (Newton-Raphson)
extern float FLOAT_80614a80; // particle opacity
extern float FLOAT_80614a84; // threshold
extern float FLOAT_80614a98; // wobble scale A
extern float FLOAT_80614a9c; // 2.0f
extern float FLOAT_80614abc; // lifetime threshold / fade scale
extern float FLOAT_80614acc; // height offset / wobble radius
extern float FLOAT_80614ad8; // angular divisor
extern float FLOAT_80614ae4; // random scale
extern float FLOAT_80614ae8; // particle random spread (large)
extern float FLOAT_80614af0; // particle lifetime
extern float FLOAT_80614b08; // homing lifetime threshold
extern float FLOAT_80614b14; // speed recovery rate
extern float FLOAT_80614b20; // alert radius
extern float FLOAT_80614b28; // homing distance scale
extern float FLOAT_80614b30; // type 0x27 lifetime threshold
extern float FLOAT_80614b34; // particle offset
extern float FLOAT_80614b4c; // gravity accel
extern float FLOAT_80614b6c; // radians-to-degrees scale
extern float FLOAT_80614b74; // wobble B scale
extern float FLOAT_80614ba4; // opacity from speed
extern float FLOAT_80614ba8; // min velocity sq
extern float FLOAT_80614bac; // default homing speed / ricochet
extern float FLOAT_80614bc0; // 0.5f opacity
extern float FLOAT_80614bd8; // reticle quad size scale
extern float FLOAT_80614c00; // norm threshold
extern float FLOAT_80614c28; // pi (or deg constant)
extern float FLOAT_80614c44; // angular damping
extern float FLOAT_80614c5c; // air drag
extern float FLOAT_80614c64; // direction threshold
extern float FLOAT_80614c68; // yaw offset constant
extern float FLOAT_80614c78; // homing speed (fast)
extern float FLOAT_80614c7c; // forward offset
extern float FLOAT_80614c80; // particle scale
extern float FLOAT_80614c84; // speed decay factor
extern float FLOAT_80614c88; // min speed for particle spawn
extern float FLOAT_80614c8c; // upward particle bias
extern float FLOAT_80614c90; // reticle alpha scale
extern float FLOAT_80614c94; // collision shape alpha scale
extern float FLOAT_80614c98; // stuck offset scale
extern double DOUBLE_80614a68;
extern double DOUBLE_80614bf8;
extern float FLOAT_80614bf4;
extern float DAT_80583228[]; // sin table

// Forward declarations
extern double FUN_80131538(double val);       // sqrtf
extern double FUN_80390aa4(double y, double x); // atan2f
extern double FUN_801310c0(void);             // random float [0,1)
extern unsigned int FUN_8013104c(void);       // game RNG
extern int FUN_803825c0(double val);          // float-to-int clamp
extern void FUN_8013025c(double angle, float* quat, float* axis); // axis-angle to quat
extern void FUN_8012fb90(float* quat, float* vec);  // quat rotate vector
extern void FUN_8012f388(double px, double py, double pz, double rx, double ry, double rz, float* mtx);
extern void FUN_8012f2c4(double px, double py, double pz, double ry, float* mtx);
extern void FUN_8012f154(double rx, double ry, double rz, float* mtx);
extern void FUN_8012f05c(double ry, float* mtx);
extern void FUN_8012f9b8(float* mtx, float* vec);  // transform point by matrix
extern void FUN_8012fa34(float* mtx, float* vec);  // transform point (alt)
extern void FUN_8012fdc0(float* dst, float* src);  // invert matrix
extern void FUN_80130b90(float* src, float* dst);  // copy matrix
extern void FUN_8012fb70(float* mtx, float* pos_in, float* pos_out); // world-to-screen

// =============================================================================
// Address: 0x800be878 | player_ai_projectile_steer_simple
// Steers a homing projectile by adding a scaled direction delta to the slot's
// direction vector, then normalizes. Type 0x2a uses dt-scaled momentum blend.
// Updates linked entity yaw/pitch via atan2.
// =============================================================================
void player_ai_projectile_steer_simple(
    double speed, double distance, int* slot, float* delta
) {
    float scale = (float)(speed / distance);

    // Scale delta by speed/distance ratio
    delta[0] *= scale;
    delta[1] *= scale;
    delta[2] *= scale;

    // Type 0x2a: blend with dt-scaled existing direction (momentum)
    if (*slot == 0x2a) {
        float dt_factor = (FLOAT_80614a60 / (float)((double)(DAT_806109c0 ^ 0x80000000)
                          + 4503599627370496.0 - 4503599627370496.0))
                          * (float)slot[0x19] * (float)slot[0x22];
        *(float*)&slot[0x19] = dt_factor + delta[0];
        *(float*)&slot[0x1a] = (FLOAT_80614a60 / (float)((double)(DAT_806109c0 ^ 0x80000000)
                               + 4503599627370496.0 - 4503599627370496.0))
                               * (float)slot[0x1a] * (float)slot[0x22] + delta[1];
        *(float*)&slot[0x1b] = (FLOAT_80614a60 / (float)((double)(DAT_806109c0 ^ 0x80000000)
                               + 4503599627370496.0 - 4503599627370496.0))
                               * (float)slot[0x1b] * (float)slot[0x22] + delta[2];
    } else {
        // Simple additive steering
        *(float*)&slot[0x19] += delta[0];
        *(float*)&slot[0x1a] += delta[1];
        *(float*)&slot[0x1b] += delta[2];
    }

    // Normalize direction
    float len_sq = *(float*)&slot[0x19] * *(float*)&slot[0x19]
                 + *(float*)&slot[0x1a] * *(float*)&slot[0x1a]
                 + *(float*)&slot[0x1b] * *(float*)&slot[0x1b];
    if ((double)FLOAT_80614a5c < (double)len_sq) {
        double len = FUN_80131538((double)len_sq);
        float inv_len = FLOAT_80614a60 / (float)len;
        *(float*)&slot[0x19] *= inv_len;
        *(float*)&slot[0x1a] *= inv_len;
        *(float*)&slot[0x1b] *= inv_len;
    }

    // Compute XZ distance for pitch calculation
    float xz_sq = *(float*)&slot[0x19] * *(float*)&slot[0x19]
                + *(float*)&slot[0x1b] * *(float*)&slot[0x1b];
    double xz_len = (double)FLOAT_80614a5c;
    if ((double)FLOAT_80614a5c < (double)xz_sq) {
        xz_len = FUN_80131538((double)xz_sq);
    }

    // Update entity yaw/pitch
    if (slot[0x2a] != 0) {
        double yaw = FUN_80390aa4((double)*(float*)&slot[0x1b], (double)*(float*)&slot[0x19]);
        *(float*)(slot[0x2a] + 0x70) = -((FLOAT_80614b6c * (float)yaw) / FLOAT_80614c28)
                                        - FLOAT_80614c68;
        double pitch = FUN_80390aa4((double)*(float*)&slot[0x1a], xz_len);
        *(float*)(slot[0x2a] + 0xc0) = (FLOAT_80614b6c * (float)pitch) / FLOAT_80614c28;
    }
}

// =============================================================================
// Address: 0x800beaac | player_ai_projectile_steer_angular
// Steers a projectile using angular momentum (torque accumulation).
// Computes cross-product between old and new direction for rotation axis,
// accumulates angular velocity at offset 0x180-0x188 with damping per frame,
// then applies rotation via quaternion (axis-angle). Updates entity orientation.
// =============================================================================
void player_ai_projectile_steer_angular(
    double speed, double distance, int slot, float* delta
) {
    float scale = (float)(speed / distance);
    float zero = FLOAT_80614a5c;

    // Scale delta
    delta[0] *= scale;
    delta[1] *= scale;
    delta[2] *= scale;

    // Save previous direction
    float prev_x = *(float*)(slot + 100);
    float prev_y = *(float*)(slot + 0x68);
    float prev_z = *(float*)(slot + 0x6c);

    // Add delta to direction
    *(float*)(slot + 100) += delta[0];
    *(float*)(slot + 0x68) += delta[1];
    *(float*)(slot + 0x6c) += delta[2];

    // Normalize new direction
    float len_sq = *(float*)(slot + 100) * *(float*)(slot + 100)
                 + *(float*)(slot + 0x68) * *(float*)(slot + 0x68)
                 + *(float*)(slot + 0x6c) * *(float*)(slot + 0x6c);
    if (zero < (float)len_sq) {
        float inv_len = FLOAT_80614a60 / (float)FUN_80131538((double)len_sq);
        *(float*)(slot + 100) *= inv_len;
        *(float*)(slot + 0x68) *= inv_len;
        *(float*)(slot + 0x6c) *= inv_len;
    }

    // Cross product: new x prev = rotation axis (torque)
    float cross_x = *(float*)(slot + 0x68) * prev_z
                  - *(float*)(slot + 0x6c) * prev_y;
    float cross_y = *(float*)(slot + 0x6c) * prev_x
                  - *(float*)(slot + 100) * prev_z;
    float cross_z = *(float*)(slot + 100) * prev_y
                  - *(float*)(slot + 0x68) * prev_x;

    // Accumulate angular velocity (torque + centripetal correction)
    float cross_len = (float)FUN_80131538((double)(cross_x*cross_x + cross_y*cross_y + cross_z*cross_z));
    *(float*)(slot + 0x180) += cross_x + *(float*)(slot + 100) * (FLOAT_80614a78 * cross_len);
    *(float*)(slot + 0x184) += cross_y + *(float*)(slot + 0x68) * (FLOAT_80614a78 * cross_len);
    *(float*)(slot + 0x188) += cross_z + *(float*)(slot + 0x6c) * (FLOAT_80614a78 * cross_len);

    // Restore original direction (torque accumulates, not direct steer)
    *(float*)(slot + 100) = prev_x;
    *(float*)(slot + 0x68) = prev_y;
    *(float*)(slot + 0x6c) = prev_z;

    // Apply per-frame damping
    float damp = FLOAT_80614c44;
    for (int i = 0; i < DAT_806108e4; i++) {
        *(float*)(slot + 0x180) *= damp;
        *(float*)(slot + 0x184) *= damp;
        *(float*)(slot + 0x188) *= damp;
    }

    // Apply angular velocity as quaternion rotation
    float ang_x = *(float*)(slot + 0x180);
    float ang_y = *(float*)(slot + 0x184);
    float ang_z = *(float*)(slot + 0x188);
    float ang_len_sq = ang_x*ang_x + ang_y*ang_y + ang_z*ang_z;
    if (zero < ang_len_sq) {
        float axis[3] = {ang_x, ang_y, ang_z};
        double ang_len = FUN_80131538((double)ang_len_sq);
        float inv_len = FLOAT_80614a60 / (float)ang_len;
        axis[0] *= inv_len; axis[1] *= inv_len; axis[2] *= inv_len;

        float frame_time = (float)((double)(DAT_806109c0 ^ 0x80000000)
                           + 4503599627370496.0 - 4503599627370496.0);
        float angle = FLOAT_806108ec * (float)ang_len * (FLOAT_80614a60 / frame_time);

        float quat[4];
        FUN_8013025c((double)angle, quat, axis);
        FUN_8012fb90(quat, (float*)(slot + 100));
    }

    // Compute XZ distance for pitch
    float xz_sq2 = *(float*)(slot + 100) * *(float*)(slot + 100)
                 + *(float*)(slot + 0x6c) * *(float*)(slot + 0x6c);
    double xz_len = zero;
    if (zero < (float)xz_sq2) {
        xz_len = FUN_80131538((double)xz_sq2);
    }

    // Update entity orientation
    if (*(int*)(slot + 0xa8) != 0) {
        *(char*)(*(int*)(slot + 0xa8) + 0x32) = 2;
        *(float*)(*(int*)(slot + 0xa8) + 200) = zero;
        double yaw = FUN_80390aa4((double)*(float*)(slot + 0x6c), (double)*(float*)(slot + 100));
        *(float*)(*(int*)(slot + 0xa8) + 0x70) = FLOAT_80614c68 - (FLOAT_80614b6c * (float)yaw) / FLOAT_80614c28;
        double pitch = FUN_80390aa4((double)*(float*)(slot + 0x68), xz_len);
        *(float*)(*(int*)(slot + 0xa8) + 0xc0) = -((FLOAT_80614b6c * (float)pitch) / FLOAT_80614c28);
    }
}

// =============================================================================
// Address: 0x800bee80 | player_ai_projectile_spawn_entity
// Spawns a visible 3D entity for a projectile slot. Selects entity type
// by projectile type (0xB->0x745, 0x32->0x53F, 0x2A->0x744, default->0x743).
// Computes yaw/pitch from direction, spawns entity, sets render flags.
// Type 0x32 gets additional render type and lifetime reset.
// =============================================================================
void player_ai_projectile_spawn_entity(int* slot) {
    int entity_type = 0x743;  // default rocket model
    int type = *slot;

    if (type == 0xb)  entity_type = 0x745;  // homing variant
    else if (type == 0x32) entity_type = 0x53f;  // guided bomb
    else if (type == 0x2a) entity_type = 0x744;  // seeking missile

    // Compute pitch from direction
    double xz_len = FUN_80131538((double)(
        *(float*)&slot[0x19] * *(float*)&slot[0x19] +
        *(float*)&slot[0x1b] * *(float*)&slot[0x1b]));
    double pitch_rad = FUN_80390aa4((double)*(float*)&slot[0x1a], xz_len);
    float pitch = (FLOAT_80614b6c * (float)pitch_rad) / FLOAT_80614c28;

    // Compute yaw from direction
    double yaw_rad = FUN_80390aa4((double)*(float*)&slot[0x1b], (double)*(float*)&slot[0x19]);
    float yaw = (float)(-(double)(FLOAT_80614b6c * (float)yaw_rad) / (double)FLOAT_80614c28
                - (double)FLOAT_80614c68);
    float roll = FLOAT_80614a5c;
    float orientation[3] = {pitch, yaw, roll};

    // Spawn entity
    int entity = entity_spawn_by_type(
        (double)FLOAT_80614c68, (double)FLOAT_80614c28,
        (double)(FLOAT_80614b6c * (float)yaw_rad),
        0, 0, 0, 0, 0,
        entity_type, slot + 0xd, orientation, 0, 0);
    slot[0x2a] = entity;

    // Set entity flags: render mode 2, flags 0x40020000
    *(char*)(slot[0x2a] + 0x32) = 2;
    *(unsigned int*)(slot[0x2a] + 0x28) |= 0x40020000;
    FUN_800e4490();  // register entity for rendering

    // Type 0x32: additional setup
    if (*slot == 0x32) {
        *(unsigned int*)(slot[0x2a] + 0x28) |= 0x10;
        entity_set_render_type(slot[0x2a], (void*)0x800bee70);
        *(float*)&slot[0x26] = FLOAT_80614a5c;  // reset lifetime
    }

    // Clear effect counter if flag 0x2000
    if ((slot[3] & 0x2000u) != 0) {
        player_ai_projectile_clear_effect_counter((int)slot);
    }
}

// =============================================================================
// Address: 0x800befe0 | player_ai_projectile_guided_tick
// MASSIVE per-frame update for guided/homing projectiles (types 0x09, 0x0B,
// 0x23, 0x27, 0x2A, 0x32). Handles:
//   1. Lifetime expiration check (type-specific thresholds)
//   2. Homing speed selection by type (with distance falloff for 0x2A)
//   3. Steering via steer_simple or steer_angular (type 0x32)
//   4. Velocity computation + raycast collision
//   5. On miss: speed decay, particle effects per type (billboard/trail/column)
//   6. On hit: explosion spawn, impact dispatch, damage resolution, deactivate
//   7. On expire: cleanup particles, explosion, deactivate
// =============================================================================
void player_ai_projectile_guided_tick(int* slot) {
    // [MASSIVE function - ~800 bytes of logic]
    // Full implementation in Ghidra IDB with plate comment.
    // Key flow:
    //   - Check DAT_806108e4 (frame valid), skip if 0
    //   - Determine if expired (type-specific lifetime checks)
    //   - If expired: stop particles, spawn explosion, deactivate
    //   - If alive + has homing flag (0x2000):
    //       Select homing speed by type switch
    //       Call steer_angular (type 0x32) or steer_simple + homing_update
    //   - Compute velocity = dir * speed * dt
    //   - Raycast (player_ai_fire_raycast_simple)
    //   - On miss: advance position, speed decay, per-type particle updates
    //   - On hit: compute reflection, impact dispatch, explosion, damage, deactivate
    // See decompiled source for full per-type particle logic (billboard types
    // 4/5/6/0x7D/0x7F, trail types 0x12/0x13, type 0x23 multi-particle jitter)
}

// =============================================================================
// Address: 0x800bfcf0 | player_ai_projectile_reticle_render
// Renders 3 targeting reticle sprites around the projectile's direction vector.
// Computes perpendicular axes via cross product, positions sprites in a ring
// using sin/cos from lookup table, projects to screen space, draws textured
// quads with alpha (sprite 0x28D). Only renders if stats_is_tracking_active().
// =============================================================================
void player_ai_projectile_reticle_render(void) {
    // [Complex rendering function - screen-space billboard quad computation]
    // Key flow:
    //   - Get slot from FUN_803826e0
    //   - Check stats_is_tracking_active()
    //   - Load sprite texture 0x28D dimensions
    //   - Compute perpendicular axes from direction (cross product with up/right)
    //   - Loop 3 times:
    //       Compute ring position from angular offset + sprite scale
    //       Transform to screen space via camera matrix
    //       Frustum clip test (all coords within [-1,1] and Z > 0)
    //       Compute quad corners from screen position + scaled size
    //       Render textured quad with alpha (FUN_8017a62c)
    //   - Cleanup: FUN_8038272c
}

// =============================================================================
// Address: 0x800c0250 | player_ai_projectile_arrow_spawn_entity
// Spawns a visible entity for arrow/bolt projectiles (type 0x20/0x21).
// Type 0x21 uses entity 0x15 + model type 0x20, type 0x20 uses 0x3A + 0x21.
// Computes yaw/pitch from direction, creates entity from spawn definition table,
// links slot back-pointer into entity's render data for bone tracking.
// =============================================================================
void player_ai_projectile_arrow_spawn_entity(void) {
    int* slot;
    int entity_id;
    int model_type;

    slot = (int*)FUN_803826f0();

    entity_id = 0x3a;
    model_type = 0x21;
    if (*slot == 0x21) {
        entity_id = 0x15;
        model_type = 0x20;
    }

    // Compute pitch
    double xz_len = FUN_80131538((double)(
        *(float*)&slot[0x19] * *(float*)&slot[0x19] +
        *(float*)&slot[0x1b] * *(float*)&slot[0x1b]));
    double pitch_rad = FUN_80390aa4((double)*(float*)&slot[0x1a], xz_len);
    float pitch = (FLOAT_80614b6c * (float)pitch_rad) / FLOAT_80614c28;

    // Compute yaw
    double yaw_rad = FUN_80390aa4((double)*(float*)&slot[0x1b], (double)*(float*)&slot[0x19]);
    float yaw = (float)(-(double)(FLOAT_80614b6c * (float)yaw_rad) / (double)FLOAT_80614c28
                - (double)FLOAT_80614c68);
    float roll = FLOAT_80614a5c;

    // Get spawn definition and create instance
    unsigned int* spawn_def = spawn_def_get_entry(2);
    int entity = spawn_create_instance(
        (double)FLOAT_80614c68, (double)FLOAT_80614c28, (double)(FLOAT_80614b6c * (float)yaw_rad),
        0, 0, 0, 0, 0,
        1, 0x40, entity_id, 0, slot + 0xd, spawn_def, model_type, 1, 0, 0);
    slot[0x2a] = entity;

    // Set entity orientation and render state
    int render_data = *(int*)(slot[0x2a] + 0x7c);
    *(float*)(slot[0x2a] + 0xc0) = pitch;
    *(float*)(slot[0x2a] + 0x70) = yaw;
    *(float*)(slot[0x2a] + 200) = roll;
    *(char*)(slot[0x2a] + 0x32) = 2;

    // Link slot back-pointer into render data
    *(int**)(render_data + 0xbc) = slot;
    *(int*)(render_data + 0xc0) = 0;  // state = in-flight

    FUN_8038273c();
}

// =============================================================================
// Address: 0x800c03ac | player_ai_projectile_arrow_lifetime_fade
// Checks if a stuck arrow entity has expired. Decrements frame counter,
// applies alpha fade to collision shape as lifetime decreases, and
// destroys the entity when counter reaches 0.
// Returns 1 if entity was destroyed, 0 otherwise.
// =============================================================================
int player_ai_projectile_arrow_lifetime_fade(int* slot, int* entity) {
    if ((*slot != 0x20 && *slot != 0x21) || entity[0x68] == 0) {
        return 0;
    }

    // Decrement frame counter
    entity[0x69] -= DAT_806108e4;

    // Compute alpha fade based on remaining lifetime
    float frame_time = FLOAT_80614a7c * (float)((double)(DAT_806109c0 ^ 0x80000000)
                       + 4503599627370496.0 - 4503599627370496.0);
    float remaining = (float)((double)(entity[0x69] ^ 0x80000000)
                      + 4503599627370496.0 - 4503599627370496.0);

    if (remaining <= frame_time) {
        // Apply alpha to collision shape
        char alpha = (char)(int)((FLOAT_80614c94 * remaining) / frame_time);
        collision_set_shape_params((int*)entity[1], 0xf, 0xffffffff, 0xff, alpha);
    }

    // Destroy if expired
    if ((int)entity[0x69] < 1) {
        slot[0x56] = 0;
        slot[0x57] = -1;
        entity_destroy_with_event(entity);
        return 1;
    }

    return 0;
}

// =============================================================================
// Address: 0x800c04c8 | player_ai_projectile_arrow_tick
// MASSIVE per-frame update for arrow/bolt projectiles (type 0x20/0x21).
// Two major states:
//   A) Stuck (direction == zero): Track attached entity's bone transform,
//      apply fade lifetime check, update position from entity matrix.
//   B) In-flight (direction != zero): Apply gravity to Y velocity, air drag,
//      raycast, on-hit-character: bone lookup + attach + damage + sound,
//      on-hit-surface: stick with mesh raycast + column particle,
//      on-hit-entity-type-2: destroy with state flag.
// Updates entity position/orientation each frame.
// =============================================================================
void player_ai_projectile_arrow_tick(void) {
    // [MASSIVE function - ~1200 bytes]
    // Key flow:
    //   slot = FUN_803826e8()
    //   entity = slot[0x2a], render_data = entity->0x7c
    //
    //   IF direction == (0,0,0):  [STUCK STATE]
    //     Set entity dirty flag 0x2
    //     Check arrow_lifetime_fade -> if destroyed, done
    //     If stuck-to-entity (slot[0x56] != 0):
    //       Get bone matrix from entity
    //       Transform local hit position + normal by bone matrix
    //       Update slot position + entity position from transformed coords
    //       Update entity yaw/pitch from transformed normal
    //
    //   ELSE:  [IN-FLIGHT STATE]
    //     Set entity dirty flag 0x2
    //     Compute velocity = direction * frame_dt (note: not speed-scaled like guided)
    //     Raycast (player_ai_fire_raycast_simple)
    //     On damage received if hits > 0
    //
    //     IF no hit:
    //       Spawn trail billboard (type 10) if not exists
    //       Advance position += velocity
    //       Accumulate distance
    //       Apply gravity: slot[0x1a] += FLOAT_80614b4c / frame_time * dt
    //       Apply air drag: direction *= FLOAT_80614c5c
    //       Update billboard position
    //       Update entity yaw/pitch + position
    //
    //     IF hit character (entity type 8):
    //       voice_pitch_update, play sound 0xCB9
    //       impact_dispatch, position to hit+normal
    //       Mesh raycast to find stick point
    //       Store stuck-entity ref (slot[0x56]), mark entity flag 0x10000
    //       Bone lookup (3 fallback attempts via active index)
    //       Damage dispatch via switch_slot
    //       Set render_data->0xC0 = 1 (stuck in character)
    //
    //     IF hit surface (not type 2, not type 8):
    //       collision_result_restore
    //       Store stuck-entity, bone lookup
    //       Spawn column particle
    //       Set render_data->0xC0 = 2 (stuck in surface)
    //
    //     IF hit type 2 (breakable):
    //       Set render_data->0xC0 = 2, clear slot refs
    //
    //     Post-hit: transform position/normal to entity-local space,
    //       normalize, store in slot[0x58-0x5d]
    //       Update entity position + stop trail particle
    //       Zero direction (enter stuck state)
    //       Alert + damage dispatch
}

// =============================================================================
// Address: 0x800c11cc | player_ai_projectile_wobble_init
// Initializes wobble/spiral flight parameters for a projectile entity.
// Randomizes:
//   - Angular velocity direction (+/- based on RNG bit)
//   - Angular velocity magnitude (offset 0x208)
//   - Angular drag (offset 0x20C, opposite sign * 2x)
//   - Radii at start/end (offset 0x210/0x214, from RNG + constants)
//   - Phase angle (offset 0x218, random * 2 * pi)
// Also stores initial direction + basis vectors and sets speed to 2.0.
// Computes target position from entity pointer or sound source.
// =============================================================================
void player_ai_projectile_wobble_init(int slot) {
    // Random factor for radius distribution
    unsigned int rng = FUN_8013104c();
    double rng_f = (double)((float)((double)(0x43300000u | rng) - DOUBLE_80614bf8) / FLOAT_80614bf4);

    // Compute direction angles
    double xz_len = FUN_80131538((double)(
        *(float*)(slot + 100) * *(float*)(slot + 100) +
        *(float*)(slot + 0x6c) * *(float*)(slot + 0x6c)));
    FUN_80390aa4((double)*(float*)(slot + 0x68), xz_len);
    FUN_80390aa4((double)*(float*)(slot + 0x6c), (double)*(float*)(slot + 100));

    // Compute perpendicular basis vectors
    player_ai_projectile_compute_basis(slot);

    // Random angular velocity direction
    unsigned int rng2 = FUN_8013104c();
    int sign = (rng2 & 1) ? 1 : -1;

    // Angular velocity (0x208): (random[0.5,1.0]) * sign
    unsigned int rng3 = FUN_8013104c();
    float ang_vel = (FLOAT_80614a78 * ((float)((double)(0x43300000u | rng3) - DOUBLE_80614bf8)
                    / FLOAT_80614bf4) + FLOAT_80614a78)
                    * (float)((double)(sign ^ 0x80000000) + 4503599627370496.0 - 4503599627370496.0);
    *(float*)(slot + 0x208) = ang_vel;

    // Angular drag (0x20C): -2 * ang_vel * random
    unsigned int rng4 = FUN_8013104c();
    *(float*)(slot + 0x20c) = FLOAT_80614a9c * -ang_vel
        * ((float)((double)(0x43300000u | rng4) - DOUBLE_80614bf8) / FLOAT_80614bf4);

    // Radii (0x210 start, 0x214 end): from RNG factor + constants
    *(float*)(slot + 0x210) = (float)((double)FLOAT_80614a98 * rng_f + (double)FLOAT_80614acc);
    float complement = FLOAT_80614b74 * (float)((double)FLOAT_80614a60 - rng_f);
    *(float*)(slot + 0x214) = (float)((double)FLOAT_80614a98 * (double)complement + (double)FLOAT_80614acc);

    // Phase angle (0x218): random * 2 * pi
    unsigned int rng5 = FUN_8013104c();
    *(float*)(slot + 0x218) = FLOAT_80614a9c * FLOAT_80614c28
        * ((float)((double)(0x43300000u | rng5) - DOUBLE_80614bf8) / FLOAT_80614bf4);

    // Store initial direction
    *(int*)(slot + 0x21c) = *(int*)(slot + 100);
    *(int*)(slot + 0x220) = *(int*)(slot + 0x68);
    *(int*)(slot + 0x224) = *(int*)(slot + 0x6c);
    *(int*)(slot + 0x228) = *(int*)(slot + 0x88);

    // Set speed to 2.0
    *(float*)(slot + 0x9c) = FLOAT_80614a9c;

    // Compute target position from entity or sound source
    int sound_def = *(int*)(slot + 0x104);
    if (sound_def == 0 || *(int*)(sound_def + 8) <= DAT_80612610) {
        // Use entity position + aim offset
        int target_ent = *(int*)(slot + 0x19c);
        if (target_ent != 0 && (*(unsigned int*)(target_ent + 0x28) & 0x80000000) == 0) {
            *(float*)(slot + 0x1a0) = *(float*)(target_ent + 100) + *(float*)(target_ent + 0x1a8);
            *(float*)(slot + 0x1a4) = *(float*)(target_ent + 0x68) + *(float*)(target_ent + 0x1ac);
            *(float*)(slot + 0x1a8) = *(float*)(target_ent + 0x6c) + *(float*)(target_ent + 0x1b0);
        }
    } else {
        // Use sound source position
        *(int*)(slot + 0x1a0) = *(int*)(sound_def + 0xb88);
        *(int*)(slot + 0x1a4) = *(int*)(sound_def + 0xb8c);
        *(int*)(slot + 0x1a8) = *(int*)(sound_def + 0xb90);
    }

    // Clear state flag
    *(int*)(slot + 4) = 0;
}

// =============================================================================
// Address: 0x800c1420 | player_ai_projectile_contact_damage_dispatch
// Handles damage dispatch when a projectile entity contacts another entity.
// Two cases:
//   - Character hit (entity type 8): team check, apply voice + death sound
//   - Surface with flag 0x2 (breakable/switchable): damage dispatch to entity
// =============================================================================
void player_ai_projectile_contact_damage_dispatch(
    void* unused1, double param2, double param3,
    void* unused4, void* unused5, void* unused6, void* unused7, void* unused8,
    int param9, int param10, int param11, int param12
) {
    int* slot;
    int hit_entity;
    int local_ctx[8];

    // Get slot and hit entity from register pair
    unsigned long long pair = (unsigned long long)FUN_803826ec();
    slot = (int*)(pair >> 32);
    hit_entity = (int)(pair & 0xFFFFFFFF);

    if (*(int*)(hit_entity + 0x20) == 8) {
        // Character hit
        int is_friendly = 0;
        if (slot[0x41] != 0) {
            int target_render = *(int*)(*(int*)(hit_entity + 0x7c) + 0x84);
            int owner_team = *(int*)(slot[0x41] + 0x84);
            if ((DAT_805015c0 & 1) != 0 && target_render == owner_team) {
                is_friendly = 1;
            }
        }

        // Play damage sound unless friendly in team mode (without flag 8)
        if ((DAT_805015c0 & 1) == 0 || (DAT_805015c0 & 8) != 0 || !is_friendly) {
            int shield = FUN_80364290(*(int*)(hit_entity + 0x7c), 0x4000);
            if (shield == 0) {
                FUN_800f3f50();  // play unshielded hit sound
            } else {
                FUN_800f3f50();  // play shielded hit sound
            }
        }

        player_ai_voice_pitch_update(slot);
    } else if ((*(unsigned int*)(hit_entity + 0x9c) & 2) != 0) {
        // Breakable/switchable surface
        switch_slot_reset((int*)local_ctx);
        switch_slot_load_from_def((int*)local_ctx, slot);

        // Setup damage context
        // local_48 = slot + 0x19 (direction)
        // local_58 = 0.0f, local_44 = 1.0f
        local_ctx[0] = hit_entity;
        damage_dispatch_to_entity(
            (double)FLOAT_80614a5c, param2, param3, 0, 0, 0, 0, 0, local_ctx);
    }

    FUN_80382738();
}
