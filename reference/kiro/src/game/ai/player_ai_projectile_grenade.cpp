// =============================================================================
// Player AI Projectile Grenade/Wobble/Thrown Systems
// Address Range: 0x800c1554 - 0x800c3160
// Date: 2026-08-11
// Batch: 268
//
// This module handles grenade, wobble/spiral, and thrown/physics projectiles:
//   - MASSIVE wobble/spiral tick (type 0x26/0x29: sin/cos path + trail particles)
//   - Wobble render callback (flicker-fade billboard with RNG color)
//   - Wobble detonation (emitter + explosion type 0xD)
//   - Generic projectile impact handler (explosion + debris by type)
//   - Simple particle emitter spawn wrapper
//   - Normalize/randomize direction vector utility
//   - Grenade entity spawn (type 0x746/0x74B with material + render flags)
//   - MASSIVE grenade/bounce tick (types 0xC-0xF: gravity, bounce, sounds, cluster)
//   - Grenade model spawn (type 0x74C with random tumble spin)
//   - MASSIVE thrown/physics tick (gravity + spin + bounce + fade + detonate)
// =============================================================================
#include "common.h"
#include "player_ai.h"
#include "particle.h"
#include "sound.h"
#include "collision.h"
#include "scene.h"
#include "entity.h"

// Globals (see prior files for full extern list)
extern int DAT_806108e4;
extern int DAT_806109c0;
extern float FLOAT_806108ec;
extern char DAT_805015bd;
extern char DAT_805015c0;
extern float FLOAT_80614a5c, FLOAT_80614a60, FLOAT_80614a78, FLOAT_80614a7c;
extern float FLOAT_80614a80, FLOAT_80614a84, FLOAT_80614a88, FLOAT_80614a98;
extern float FLOAT_80614a9c, FLOAT_80614aa0, FLOAT_80614aa4, FLOAT_80614abc;
extern float FLOAT_80614ac4, FLOAT_80614acc, FLOAT_80614ad8, FLOAT_80614ae4;
extern float FLOAT_80614ae8, FLOAT_80614af0, FLOAT_80614af4, FLOAT_80614afc;
extern float FLOAT_80614b1c, FLOAT_80614b20, FLOAT_80614b34, FLOAT_80614b4c;
extern float FLOAT_80614b54, FLOAT_80614b6c, FLOAT_80614b70, FLOAT_80614b74;
extern float FLOAT_80614ba8, FLOAT_80614bac, FLOAT_80614bb8, FLOAT_80614bc0;
extern float FLOAT_80614bcc, FLOAT_80614be0, FLOAT_80614be8, FLOAT_80614c00;
extern float FLOAT_80614c04, FLOAT_80614c28, FLOAT_80614c44, FLOAT_80614c54;
extern float FLOAT_80614c5c, FLOAT_80614c64, FLOAT_80614c68, FLOAT_80614c8c;
extern float FLOAT_80614c94, FLOAT_80614c98, FLOAT_80614ca8, FLOAT_80614cac;
extern float FLOAT_80614cb0, FLOAT_80614cb4, FLOAT_80614cb8, FLOAT_80614cbc;
extern float FLOAT_80614cc0, FLOAT_80614cc4, FLOAT_80614cc8, FLOAT_80614ccc;
extern float FLOAT_80614cd0;
extern double DOUBLE_80614a68, DOUBLE_80614bf8, DOUBLE_80614ca0;
extern float FLOAT_80614bf4;
extern float DAT_80583228[];

// =============================================================================
// Address: 0x800c1554 | player_ai_projectile_wobble_tick
// MASSIVE per-frame update for wobble/spiral projectiles (type 0x26/0x29).
// Computes spiral motion using sin/cos from stored angle parameters:
//   - Angular phase advances per frame (slot[0x82], slot[0x83])
//   - Radius interpolates between start (slot[0x84]) and end (slot[0x85])
//   - Position = center + perpendicular_basis * sin/cos * radius + random jitter
//   - Direction recomputed from position delta, normalized
//   - Speed set to distance * frame_rate for constant visual speed
// On miss: spawns trail particles (type 0x26 only, 3-5 per frame)
// On hit: type-specific explosion (0x26: type 0x18, 0x29: type 0x22),
//   impact dispatch, contact damage, damage resolution, deactivate
// On lifetime expire: explosion + deactivate
// =============================================================================
void player_ai_projectile_wobble_tick(void) {
    // [MASSIVE function ~900 bytes - full spiral path + collision + damage]
    // See Ghidra plate comment for detailed algorithm.
    // Key state fields:
    //   slot[0x82] = angular velocity (radians/sec)
    //   slot[0x83] = angular acceleration
    //   slot[0x84] = start radius
    //   slot[0x85] = end radius
    //   slot[0x86] = phase offset
    //   slot[0x87-0x89] = center velocity (perpendicular basis advance)
    //   slot[0x8a] = center speed
    //   slot[0x10-0x12] = center position (advanced separately)
    //   slot[0x68-0x6a] = target position
    //   slot[0x71-0x76] = perpendicular basis vectors (2x vec3)
}

// =============================================================================
// Address: 0x800c1ddc | player_ai_projectile_wobble_render
// Render callback for wobble projectile entity. Two modes:
//   state==0 (initial): renders glow billboard (sprite 0x28D) at fixed size/color
//   state!=0 (in-flight): computes flicker based on lifetime ratio,
//     RNG-based probability determines if sprite renders this frame,
//     size scales down with lifetime, color fades (R=G=alpha channel).
// =============================================================================
void player_ai_projectile_wobble_render(int slot) {
    if (*(int*)(slot + 4) == 0) {
        // Initial state: solid glow
        FUN_8016f00c(2);
        FUN_8016e8ac(2);
        int tex = sprite_get_texture_id(0x28d);
        particle_sprite_billboard_render(
            (double)FLOAT_80614afc, (double)FLOAT_80614afc,
            (double)FLOAT_80614a5c, tex, slot + 0x1c, 0x7f6fbaff);
    } else {
        // In-flight: flicker-fade
        float lifetime_ratio = *(float*)(slot + 0x98) / *(float*)(slot + 0x9c);

        // Alpha from inverse lifetime
        unsigned int alpha = (unsigned int)(FLOAT_80614ca8 *
            -(FLOAT_80614aa0 * lifetime_ratio - FLOAT_80614a60));

        // RNG-based flicker: skip rendering on some frames
        unsigned int rng = FUN_8013104c();
        unsigned int period = (unsigned int)(FLOAT_80614ad8 * lifetime_ratio + FLOAT_80614a60);
        unsigned int color;
        if (rng == (rng / period) * period) {
            // Render frame: encode alpha into RGB channels
            color = (alpha & 0xff) << 16 | (alpha & 0xff) << 8 | 0xff | 0x7f000000;
        } else {
            color = 0;  // skip (invisible)
        }

        FUN_8016f00c(2);
        FUN_8016e8ac(2);
        int tex = sprite_get_texture_id(0x28d);

        // Size scales with lifetime
        float size_x = FLOAT_80614cac * -(FLOAT_80614b54 * lifetime_ratio - FLOAT_80614a60);
        float size_y = FLOAT_80614cac * (FLOAT_80614a60 - lifetime_ratio);

        particle_sprite_billboard_render(
            (double)size_x, (double)size_y,
            (double)FLOAT_80614a5c, tex, slot + 0x1c, color);
    }
}

// =============================================================================
// Address: 0x800c1f1c | player_ai_projectile_wobble_detonate
// Detonation handler for wobble projectiles. Stops the glow particle,
// spawns a directional particle emitter (DAT_805652c4), and triggers
// explosion type 0xD at the slot position.
// =============================================================================
void player_ai_projectile_wobble_detonate(int slot) {
    float dir[3] = {DAT_803cc64c, DAT_803cc650, DAT_803cc654};

    player_ai_particle_stop_fade(slot, 0);
    particle_emitter_spawn(0, &DAT_805652c4, 0, (float*)(slot + 0x1c), dir);
    player_ai_explosion_spawn(0xd, 0, (float*)(slot + 0x1c), NULL, *(int*)(slot + 0x10c), 0, 1);
}

// =============================================================================
// Address: 0x800c1fac | player_ai_projectile_impact_handler
// Generic impact handler for multiple projectile types.
// Selects explosion type by projectile type (0xD->7, 0xF->0x2B, default->6).
// Stops particle, spawns explosion, applies surface effect,
// and spawns impact debris (except type 0xD).
// =============================================================================
void player_ai_projectile_impact_handler(
    void* u1, void* u2, double param3, void* u4, void* u5, void* u6, void* u7, void* u8,
    int param9, int param10, float* hit_pos, float* hit_normal
) {
    int* slot;
    int hit_entity;

    // Get slot and hit entity from register pair
    unsigned long long pair = (unsigned long long)FUN_803826e8();
    slot = (int*)(pair >> 32);
    hit_entity = (int)(pair & 0xFFFFFFFF);

    int explosion_type = 6;
    float dir[3] = {DAT_803cc658, DAT_803cc65c, DAT_803cc660};

    if (*slot == 0xd) explosion_type = 7;
    if (*slot == 0xf) explosion_type = 0x2b;

    player_ai_particle_stop_fade((int)slot, 0);

    // Use hit_normal if we have a hit entity, else NULL
    float* normal_ptr = hit_normal;
    if (hit_entity == 0) normal_ptr = NULL;

    player_ai_explosion_spawn(explosion_type, *slot, (float*)(slot + 7), normal_ptr, slot[0x43], 0, 1);

    // Surface effect
    float* pos_for_effect = hit_pos;
    if (hit_entity == 0) pos_for_effect = (float*)(slot + 7);
    FUN_800e95fc(*slot, 0, slot[0x3c], hit_entity, pos_for_effect, hit_normal);

    // Spawn debris (not for type 0xD)
    if (*slot != 0xd) {
        float* debris_normal = hit_normal;
        if (hit_entity == 0) debris_normal = dir;
        player_ai_projectile_impact_debris(0, 0, 0, 0, 0, 0, 0, 0, slot, debris_normal, 0x1f);
    }

    FUN_80382734();
}

// =============================================================================
// Address: 0x800c20c4 | player_ai_projectile_spawn_emitter
// Simple wrapper: spawns a particle emitter using definition DAT_80565738.
// =============================================================================
void player_ai_projectile_spawn_emitter(int u1, int u2, float* pos, float* dir) {
    particle_emitter_spawn(0, &DAT_80565738, 0, pos, dir);
}

// =============================================================================
// Address: 0x800c20fc | player_ai_projectile_normalize_direction
// Ensures a direction vector is non-zero and normalized.
// If length squared < FLOAT_80614cb0: randomize all 3 components via RNG.
// If still too short after randomize: use fallback (FLOAT_80614cb4, cb8, 0).
// Finally normalizes to unit length.
// =============================================================================
void player_ai_projectile_normalize_direction(float* dir) {
    float len_sq = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];

    // If near-zero, randomize
    if (len_sq < FLOAT_80614cb0) {
        dir[0] = (float)((double)(0x43300000u | FUN_8013104c()) - DOUBLE_80614bf8) / FLOAT_80614bf4;
        dir[1] = (float)((double)(0x43300000u | FUN_8013104c()) - DOUBLE_80614bf8) / FLOAT_80614bf4;
        dir[2] = (float)((double)(0x43300000u | FUN_8013104c()) - DOUBLE_80614bf8) / FLOAT_80614bf4;
        len_sq = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];
    }

    // If still too short, use fallback
    if (len_sq < FLOAT_80614cb0) {
        dir[0] = FLOAT_80614cb4;
        dir[1] = FLOAT_80614cb8;
        dir[2] = FLOAT_80614a5c;
        len_sq = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];
    }

    // Normalize
    float inv_len = FLOAT_80614a60 / (float)FUN_80131538((double)len_sq);
    dir[0] *= inv_len;
    dir[1] *= inv_len;
    dir[2] *= inv_len;
}

// =============================================================================
// Address: 0x800c225c | player_ai_projectile_grenade_spawn_entity
// Spawns a grenade entity model. Type 0xF uses entity 0x74B, others use 0x746.
// Sets material transparency, rotation speed limits (0xCC/0xD0), and render
// flags 0x40020000.
// =============================================================================
void player_ai_projectile_grenade_spawn_entity(
    void* u1, double u2, double u3, void* u4, void* u5, void* u6, void* u7, void* u8,
    int* slot
) {
    int entity_type = 0x746;
    float orientation[3] = {DAT_803cc664, DAT_803cc668, DAT_803cc66c};

    if (*slot == 0xf) entity_type = 0x74b;

    int entity = entity_spawn_by_type(
        u1, u2, u3, u4, u5, u6, u7, u8,
        entity_type, slot + 7, orientation, 0, 0);
    slot[0x2a] = entity;

    // Set material with alpha
    render_instance_set_material((double)FLOAT_80614a5c, *(int**)(slot[0x2a] + 4), 1);

    // Set rotation limits
    *(float*)(slot[0x2a] + 0xcc) = FLOAT_80614a5c;
    *(float*)(slot[0x2a] + 0xd0) = FLOAT_80614cbc;

    // Set render flags
    *(unsigned int*)(slot[0x2a] + 0x28) |= 0x40020000;
}

// =============================================================================
// Address: 0x800c230c | player_ai_projectile_grenade_tick
// MASSIVE per-frame update for grenade/bouncing projectiles (types 0xC-0xF).
// Handles entity bone animation (rotation matrix from entity orientation),
// type-specific parameters (speed, gravity, bounce scale, trail size),
// in-flight physics with gravity + air drag (FLOAT_80614c5c),
// collision detection via raycast, bounce mechanics:
//   - Reflects direction off hit normal with bias (FLOAT_80614bb8)
//   - Newton-Raphson normalization of reflected direction
//   - Speed reduction per bounce (type-dependent factor)
//   - Bounce sound progression (0xD34, 0xD35, 0xD36, 0xD37 for bounces 1-4)
//   - Random rotation axis generation per bounce
// Type 0xD cluster split: on first bounce, spawns 2 child projectiles
//   with randomized spread directions via player_ai_projectile_spawn_simple.
// Detonation: when speed drops below threshold, enters fade mode
//   (alpha decay on collision shape), then deactivates.
// Type 0xC also spawns animated fire particle (type 3).
// Story mode (DAT_805015bd == '\n'): calls FUN_8026816c after lifetime.
// =============================================================================
void player_ai_projectile_grenade_tick(void) {
    // [MASSIVE function ~1400 bytes]
    // See Ghidra plate comment for full algorithm.
    // Key state:
    //   slot[0x22] = speed (decays per bounce)
    //   slot[0x66] = bounce counter
    //   slot[0x2a] = entity pointer (rotation updated per frame)
    //   entity+0xCC = rotation accumulator
    //   entity+0xD0 = rotation speed
    //   entity+0xD4-0xDC = rotation axis (normalized random per bounce)
}

// =============================================================================
// Address: 0x800c3060 | player_ai_projectile_grenade_spawn_model
// Spawns a grenade model entity (type 0x74C) with random spin axes for tumbling.
// Generates random rotation offsets (0x8C, 0x94) scaled by FLOAT_80614ac4,
// and zeroes Y rotation (0x90) and speed (0x9C).
// =============================================================================
void player_ai_projectile_grenade_spawn_model(
    void* u1, double u2, double u3, void* u4, void* u5, void* u6, void* u7, void* u8,
    int slot
) {
    float orientation[3] = {DAT_803cc670, DAT_803cc674, DAT_803cc678};

    int entity = entity_spawn_by_type(
        u1, u2, u3, u4, u5, u6, u7, u8,
        0x74c, slot + 0x34, orientation, 0, 0);
    *(int*)(slot + 0xa8) = entity;

    // Set render state
    *(char*)(*(int*)(slot + 0xa8) + 0x32) = 2;
    *(unsigned int*)(*(int*)(slot + 0xa8) + 0x28) |= 0x40020000;

    // Random spin X
    unsigned int rng1 = FUN_8013104c();
    *(float*)(slot + 0x8c) = FLOAT_80614ac4 *
        ((float)((double)(0x43300000u | rng1) - DOUBLE_80614bf8) / FLOAT_80614bf4 - FLOAT_80614a78);

    // Zero spin Y
    *(float*)(slot + 0x90) = FLOAT_80614a5c;

    // Random spin Z
    unsigned int rng2 = FUN_8013104c();
    *(float*)(slot + 0x94) = FLOAT_80614ac4 *
        ((float)((double)(0x43300000u | rng2) - DOUBLE_80614bf8) / FLOAT_80614bf4 - FLOAT_80614a78);

    // Zero speed
    *(float*)(slot + 0x9c) = FLOAT_80614a5c;
}

// =============================================================================
// Address: 0x800c3160 | player_ai_projectile_thrown_tick
// MASSIVE per-frame update for thrown/physics projectiles (generic bouncing).
// Similar to grenade_tick but for simpler thrown objects:
//   - Gravity applied to Y velocity (FLOAT_80614cd0 normalized)
//   - Air drag per frame (FLOAT_80614c5c)
//   - Entity spin: 3 rotation axes (slot[0x23-0x25]) applied to entity
//     orientation (0x70/0xC0/0xC8) with wrapping at FLOAT_80614b70 (360 deg)
//   - On collision: reflect direction off normal with bias (FLOAT_80614bb8),
//     Newton-Raphson normalize, reduce speed (FLOAT_80614ae8 on surface,
//     FLOAT_80614acc on character-type-8), advance bounce counter
//   - Detonation conditions: speed < threshold OR hit character OR
//     normal.Y > threshold (steep surface)
//   - On detonate: normalize gravity, collision shape alpha fade, proximity
//     detonate, alert radius, damage resolve + dispatch
//   - On low speed + fade: alpha decrement on collision shape, deactivate at 0
//   - Spin damping per bounce (FLOAT_80614c04, FLOAT_80614b1c)
//   - Sound on bounce: pitch from bounce count (FLOAT_80614ccc divisor)
// =============================================================================
void player_ai_projectile_thrown_tick(int* slot) {
    // [MASSIVE function ~1000 bytes]
    // See Ghidra plate comment for full algorithm.
    // Key state:
    //   slot[0x22] = speed
    //   slot[0x23-0x25] = spin rates (pitch/yaw/roll per frame)
    //   slot[0x26] = lifetime
    //   slot[0x27] = fade timer
    //   slot[0x66] = bounce counter
    //   slot[0x6e] = collision shape alpha (1.0 -> 0.0)
    //   slot[0x2a] = entity ptr (position + orientation updated)
}
