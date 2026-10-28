// =============================================================================
// Player AI Projectile Spawn & Beam Effects
// Address Range: 0x800bc540 - 0x800bd074
// Date: 2026-08-11
// Batch: 265
//
// This module handles projectile slot spawning, initialization, random spread/
// jitter computation, and beam particle effect creation. Functions include:
//   - Empty pre-init stub (no-op)
//   - Slot initialization (field reset, sound source, team score, type callback)
//   - Simple spawn (single position/direction variant)
//   - Full spawn (separate prev/curr/dest vectors, entity owner reference)
//   - Set projectile speed
//   - Random direction vector initialization (6-component jitter)
//   - Random direction tick (interpolation timer + new random target)
//   - Compute jittered aim direction (blend interpolated random + noise)
//   - Per-frame random event (type-8 conditional dispatch)
//   - Spawn beam particles (character-target vs non-target beam rendering)
// =============================================================================
#include "common.h"
#include "player_ai.h"
#include "particle.h"
#include "sound.h"

// Globals
extern int DAT_80611d74;   // game state pointer
extern int DAT_80611d78;   // player data array base
extern int DAT_80611d54;   // player controller pointer
extern int DAT_80612610;   // sound source threshold index
extern int DAT_806109c0;   // frame counter (XOR'd for RNG seed)
extern float FLOAT_80614a5c; // 0.0f
extern float FLOAT_80614a60; // 1.0f
extern float FLOAT_80614a9c; // 2.0f
extern float FLOAT_80614bc0; // 0.5f (half-offset scale)
extern float FLOAT_80614bf4; // RNG divisor (max uint range)
extern float FLOAT_806108ec; // jitter tick speed
extern double DOUBLE_80614a68; // 4503599627370496.0 (2^52, int-to-float trick)
extern double DOUBLE_80614bf8; // 4503599627370496.0 (2^52, int-to-float trick)
extern char DAT_805015c0;  // game mode flags
extern int DAT_805015d8;   // game mode type

// Type dispatch table: 64 bytes per type, callback at offset 0x00
extern void* DAT_804045c0; // projectile type vtable base

// Forward declarations
extern int sound_source_get_nth_active(int source_array, int channel);
extern void player_ai_alert_proximity_scan(void);
extern void player_ai_set_team_score(int slot_ptr, int* slot);
extern int player_ai_projectile_character_impact(void);
extern int* player_ai_slot_recycle(void);
extern int player_ai_projectile_has_character_target(int slot);
extern int particle_spawn_beam(int handle_addr, int type, int* start_pos, int* end_pos, int mask);
extern int particle_spawn_oriented(void);
extern void particle_slot_set_opacity(double opacity, int* particle);
extern unsigned int game_rand(void);  // FUN_8013104c - LCG RNG

// =============================================================================
// Address: 0x800bc540 | player_ai_projectile_pre_init_stub
// Empty no-op stub called before slot initialization.
// Likely a debug hook or placeholder for pre-init validation.
// =============================================================================
void player_ai_projectile_pre_init_stub(void) {
    return;
}

// =============================================================================
// Address: 0x800bc544 | player_ai_projectile_slot_init
// Initializes a recycled projectile slot after spawn.
// Resets position tracking, lifetime, sound handle, sets team score,
// computes aim offset position, and dispatches type-specific callback.
// =============================================================================
int* player_ai_projectile_slot_init(int* slot) {
    int type_id;
    float zero;
    float one;
    int sound_handle;
    int channel;
    int* sound_def;

    type_id = *slot;
    player_ai_projectile_pre_init_stub();

    one = FLOAT_80614a60;  // 1.0f
    zero = FLOAT_80614a5c; // 0.0f

    // Reset tracking state
    *(unsigned short*)(slot + 6) = 0;  // frame counter
    slot[0x6e] = *(int*)&one;          // lifetime max
    slot[0x5e] = 0;                    // effect handle A
    slot[100]  = 0;                    // effect handle B
    slot[99]   = 0;                    // effect handle C
    slot[0x53] = 1;                    // active flag
    slot[0x56] = 0;                    // hit count
    slot[0x57] = -1;                   // last hit entity ID
    slot[0x6c] = -1;                   // target entity ID
    slot[0x6f] = 0;                    // autoaim flag
    slot[0x67] = 0;                    // sound handle
    slot[0x6d] = 0;                    // aux counter
    slot[0x3d] = *(int*)&zero;         // distance traveled

    // Copy spawn position to previous/current/destination slots
    // Position (slots 7-9) -> prev (0x10-0x12), curr (0x13-0x15), dest (0x16-0x18)
    slot[0x10] = slot[7];
    slot[0x11] = slot[8];
    slot[0x12] = slot[9];
    slot[0x13] = slot[10];
    slot[0x14] = slot[0xb];
    slot[0x15] = slot[0xc];
    slot[0x16] = slot[0xd];
    slot[0x17] = slot[0xe];
    slot[0x18] = slot[0xf];

    // Reset interpolation factors
    slot[0x60] = *(int*)&zero;
    slot[0x61] = *(int*)&zero;
    slot[0x62] = *(int*)&zero;
    slot[0x55] = 0;

    // Check if slot qualifies for auto-aim (type==1 and game flag 0x20 set)
    if ((slot[0x40] == 1) && ((*(unsigned int*)(DAT_80611d74 + 0xc) & 0x20) != 0)) {
        slot[0x6f] = 1;
    }

    // Resolve sound source handle
    sound_def = (int*)slot[0x41];
    if (sound_def != NULL) {
        if (*(int*)(sound_def + 2) < DAT_80612610) {
            // Sound source from player data array
            channel = 0;
            if ((slot[3] & 0x4000u) == 0) {
                if ((slot[3] & 0x8000u) != 0) {
                    channel = 2;
                }
            } else {
                channel = 1;
            }
            sound_handle = sound_source_get_nth_active(
                DAT_80611d78 + *(int*)(sound_def + 2) * 0x141c, channel);
            slot[0x67] = sound_handle;
        } else {
            // Direct sound handle from definition
            slot[0x67] = *(int*)((int)sound_def + 4);
        }
        player_ai_alert_proximity_scan();
    }

    // Mark slot as initialized
    slot[0x54] = 1;

    // Set team score if flag 0x20 is set on slot flags
    if ((slot[3] & 0x20u) != 0) {
        player_ai_set_team_score((int)slot, slot);
    }

    // Compute aim offset position (position + direction offset)
    slot[0x68] = *(int*)&(*(float*)&slot[7] + *(float*)&slot[0x19]);
    slot[0x69] = *(int*)&(*(float*)&slot[8] + *(float*)&slot[0x1a]);
    slot[0x6a] = *(int*)&(*(float*)&slot[9] + *(float*)&slot[0x1b]);

    // Type 0x2a uses half-scale offset
    float half = FLOAT_80614bc0; // 0.5f
    if (*slot == 0x2a) {
        slot[0x68] = *(int*)&(half * *(float*)&slot[0x19] + *(float*)&slot[7]);
        slot[0x69] = *(int*)&(half * *(float*)&slot[0x1a] + *(float*)&slot[8]);
        slot[0x6a] = *(int*)&(half * *(float*)&slot[0x1b] + *(float*)&slot[9]);
    }

    // Dispatch type-specific init callback from vtable
    typedef void (*TypeCallback)(int*);
    TypeCallback* vtable = (TypeCallback*)((char*)&DAT_804045c0 + type_id * 0x40);
    if (*vtable != NULL) {
        (*vtable)(slot);
    }

    return slot;
}

// =============================================================================
// Address: 0x800bc760 | player_ai_projectile_spawn_simple
// Spawns a projectile with a single position/direction (duplicated to all 3
// position slots). Used for simple linear projectiles.
// Params: position vec3, direction vec3, params, owner entity, flags
// =============================================================================
void player_ai_projectile_spawn_simple(
    void* unused1, double accuracy, void* unused3, void* unused4,
    int* direction, int owner_entity, int* sound_def, int weapon_mode,
    void* unused9, int damage, unsigned int flags, int owner_id
) {
    int* spawn_pos;
    int* slot;
    int type_index;
    float zero;

    // Get spawn position from linked function (returns pos in register pair)
    spawn_pos = (int*)FUN_803826d8();

    // Check if projectile immediately impacts a character (melee/instant hit)
    if (player_ai_projectile_character_impact() != 0) {
        FUN_80382724();
        return;
    }

    // Recycle a slot from the pool
    slot = player_ai_slot_recycle();

    // Set flags (merge type flags from vtable + caller flags)
    slot[3] = flags | *(unsigned int*)((char*)&DAT_804045c0 - 4 + type_index * 0x40);
    slot[2] = owner_id;
    slot[0x40] = weapon_mode;
    slot[0x41] = (int)sound_def;
    slot[0x42] = owner_entity;
    if (sound_def != NULL) {
        owner_entity = *sound_def;
    }
    slot[0x43] = owner_entity;

    // Set accuracy and damage
    zero = FLOAT_80614a5c;  // 0.0f
    slot[0x3e] = *(int*)&(float)accuracy;
    slot[0x3c] = damage;

    // Compute frame-scaled random angle offset
    slot[0x22] = *(int*)&(float)(
        (float)((double)(*(unsigned int*)&DAT_806109c0 ^ 0x80000000) +
        4503599627370496.0 - 4503599627370496.0) * (double)zero);
    slot[0x7f] = *(int*)&zero;

    // Set position: all 3 slots (spawn/prev/dest) = same position
    slot[7]    = spawn_pos[0];
    slot[8]    = spawn_pos[1];
    slot[9]    = spawn_pos[2];
    slot[10]   = spawn_pos[0];
    slot[0xb]  = spawn_pos[1];
    slot[0xc]  = spawn_pos[2];
    slot[0xd]  = spawn_pos[0];
    slot[0xe]  = spawn_pos[1];
    slot[0xf]  = spawn_pos[2];

    // Set direction: all 3 direction slots = same direction
    slot[0x19] = direction[0];
    slot[0x1a] = direction[1];
    slot[0x1b] = direction[2];
    slot[0x1c] = direction[0];
    slot[0x1d] = direction[1];
    slot[0x1e] = direction[2];
    slot[0x1f] = direction[0];
    slot[0x20] = direction[1];
    slot[0x21] = direction[2];

    // Initialize slot state
    player_ai_projectile_slot_init(slot);

    FUN_80382724();
}

// =============================================================================
// Address: 0x800bc920 | player_ai_projectile_spawn_full
// Spawns a projectile with separate prev/curr/dest position and direction
// vectors. Used for homing, curved, or multi-phase projectiles.
// Supports entity-based owner with ID lookup from entity offset 0x60.
// =============================================================================
void player_ai_projectile_spawn_full(
    void* unused1, void* unused2,
    int* pos_prev, int* pos_curr, int* dir_prev,
    int* dir_curr, int* dir_dest,
    int owner_entity_ptr, int owner_entity_ref,
    int* sound_def, int weapon_mode,
    void* unused12, int damage, unsigned int flags
) {
    int* spawn_pos;
    int* slot;
    int type_index;
    float accuracy;
    float angle_scale;
    int owner_id;

    // Get spawn position from linked function
    spawn_pos = (int*)FUN_803826c8();

    // Check instant character impact
    if (player_ai_projectile_character_impact() != 0) {
        FUN_80382714();
        return;
    }

    // Recycle slot
    slot = player_ai_slot_recycle();

    // Set flags from vtable + caller
    slot[3] = flags | *(unsigned int*)((char*)&DAT_804045c0 - 4 + type_index * 0x40);

    // Resolve owner ID from entity pointer
    if (owner_entity_ptr == 0) {
        owner_id = -1;
    } else {
        owner_id = *(int*)(owner_entity_ptr + 0x60);
    }
    slot[2] = owner_id;

    slot[0x40] = weapon_mode;
    slot[0x41] = (int)sound_def;
    slot[0x42] = owner_entity_ref;
    if (sound_def != NULL) {
        owner_entity_ref = *sound_def;
    }
    slot[0x43] = owner_entity_ref;

    // Accuracy from entity or default zero
    accuracy = FLOAT_80614a5c;  // 0.0f
    if (owner_entity_ptr != 0) {
        accuracy = *(float*)(owner_entity_ptr + 0x18);
    }
    slot[0x3e] = *(int*)&accuracy;

    // Damage
    slot[0x3c] = damage;

    // Angle scale from entity or default 1.0f
    angle_scale = FLOAT_80614a60;  // 1.0f
    if (owner_entity_ptr != 0) {
        angle_scale = *(float*)(owner_entity_ptr + 0x1c);
    }
    // Frame-scaled random offset
    float frame_rand = (float)((double)(*(unsigned int*)&DAT_806109c0 ^ 0x80000000)
                       + 4503599627370496.0 - 4503599627370496.0);
    slot[0x22] = *(int*)&(float)(frame_rand * angle_scale);

    // If flag 0x2000000 set, scale by accuracy factor
    if ((slot[3] & 0x2000000u) != 0) {
        float scaled = *(float*)&slot[0x22] * accuracy;
        slot[0x22] = *(int*)&scaled;
    }
    slot[0x7f] = *(int*)&accuracy;

    // Set positions: spawn, prev, curr
    slot[7]    = spawn_pos[0];
    slot[8]    = spawn_pos[1];
    slot[9]    = spawn_pos[2];
    slot[10]   = pos_prev[0];
    slot[0xb]  = pos_prev[1];
    slot[0xc]  = pos_prev[2];
    slot[0xd]  = pos_curr[0];
    slot[0xe]  = pos_curr[1];
    slot[0xf]  = pos_curr[2];

    // Set directions: prev, curr, dest
    slot[0x19] = dir_prev[0];
    slot[0x1a] = dir_prev[1];
    slot[0x1b] = dir_prev[2];
    slot[0x1c] = dir_curr[0];
    slot[0x1d] = dir_curr[1];
    slot[0x1e] = dir_curr[2];
    slot[0x1f] = dir_dest[0];
    slot[0x20] = dir_dest[1];
    slot[0x21] = dir_dest[2];

    // Initialize slot state
    player_ai_projectile_slot_init(slot);

    FUN_80382714();
}

// =============================================================================
// Address: 0x800bcb3c | player_ai_projectile_set_speed
// Sets the speed (float at offset 0x9C) on a projectile slot.
// =============================================================================
void player_ai_projectile_set_speed(double speed, int slot) {
    if (slot == 0) {
        return;
    }
    *(float*)(slot + 0x9c) = (float)speed;
}

// =============================================================================
// Address: 0x800bcb4c | player_ai_projectile_jitter_init
// Initializes a 7-float jitter state structure with random values.
// Stores 6 random direction components in [-1, +1] range and resets timer.
// Used to create random bullet spread or projectile wobble.
// =============================================================================
void player_ai_projectile_jitter_init(float* jitter) {
    // Reset interpolation timer to 0.0
    jitter[6] = FLOAT_80614a5c;  // 0.0f

    // Generate 6 random values in range [-1.0, +1.0]
    // Formula: 2.0 * (rand() / MAX_RAND) - 1.0
    jitter[0] = FLOAT_80614a9c * ((float)((double)game_rand() - DOUBLE_80614bf8)
                / FLOAT_80614bf4) - FLOAT_80614a60;
    jitter[1] = FLOAT_80614a9c * ((float)((double)game_rand() - DOUBLE_80614bf8)
                / FLOAT_80614bf4) - FLOAT_80614a60;
    jitter[2] = FLOAT_80614a9c * ((float)((double)game_rand() - DOUBLE_80614bf8)
                / FLOAT_80614bf4) - FLOAT_80614a60;
    jitter[3] = FLOAT_80614a9c * ((float)((double)game_rand() - DOUBLE_80614bf8)
                / FLOAT_80614bf4) - FLOAT_80614a60;
    jitter[4] = FLOAT_80614a9c * ((float)((double)game_rand() - DOUBLE_80614bf8)
                / FLOAT_80614bf4) - FLOAT_80614a60;
    jitter[5] = FLOAT_80614a9c * ((float)((double)game_rand() - DOUBLE_80614bf8)
                / FLOAT_80614bf4) - FLOAT_80614a60;
}

// =============================================================================
// Address: 0x800bccb4 | player_ai_projectile_jitter_tick
// Advances the jitter interpolation timer. When timer reaches 1.0,
// shifts current target to source and generates new random target.
// This creates smooth random wandering for projectile spread.
// =============================================================================
void player_ai_projectile_jitter_tick(double dt, float* jitter) {
    float one = FLOAT_80614a60;  // 1.0f

    // Advance interpolation timer: timer += dt * speed * (1.0 / frame_time)
    float frame_time = (float)((double)(*(unsigned int*)&DAT_806109c0 ^ 0x80000000)
                       + 4503599627370496.0 - 4503599627370496.0);
    jitter[6] = (float)((double)(FLOAT_806108ec * (one / frame_time)) * dt
                + (double)jitter[6]);

    // When timer >= 1.0, shift and generate new target
    if (one <= jitter[6]) {
        // Current target becomes source
        jitter[0] = jitter[3];
        jitter[1] = jitter[4];
        jitter[2] = jitter[5];

        // Generate new random target in [-1, +1]
        jitter[3] = FLOAT_80614a9c * ((float)((double)game_rand() - DOUBLE_80614bf8)
                    / FLOAT_80614bf4) - FLOAT_80614a60;
        jitter[4] = FLOAT_80614a9c * ((float)((double)game_rand() - DOUBLE_80614bf8)
                    / FLOAT_80614bf4) - FLOAT_80614a60;
        jitter[5] = FLOAT_80614a9c * ((float)((double)game_rand() - DOUBLE_80614bf8)
                    / FLOAT_80614bf4) - FLOAT_80614a60;

        // Reset timer
        jitter[6] = FLOAT_80614a5c;  // 0.0f
    }
}

// =============================================================================
// Address: 0x800bcde4 | player_ai_projectile_compute_jittered_dir
// Computes a jittered aim direction by blending the interpolated jitter state
// with additional random noise. Output = (interpolated_jitter * scale) + noise.
// Used to add realistic inaccuracy to AI projectile aim.
// =============================================================================
void player_ai_projectile_compute_jittered_dir(
    double scale, double noise_scale, float* jitter, float* out_dir
) {
    float t = jitter[6];  // interpolation factor

    // Lerp between source (jitter[0-2]) and target (jitter[3-5])
    float interp_x = (float)((double)(t * (jitter[3] - jitter[0]) + jitter[0]) * scale);
    float interp_y = (float)((double)(t * (jitter[4] - jitter[1]) + jitter[1]) * scale);
    float interp_z = (float)((double)(t * (jitter[5] - jitter[2]) + jitter[2]) * scale);

    // Generate additional random noise in [-1, +1]
    float noise_x = FLOAT_80614a9c * ((float)((double)game_rand() - DOUBLE_80614bf8)
                    / FLOAT_80614bf4) - FLOAT_80614a60;
    float noise_y = FLOAT_80614a9c * ((float)((double)game_rand() - DOUBLE_80614bf8)
                    / FLOAT_80614bf4) - FLOAT_80614a60;
    float noise_z = FLOAT_80614a9c * ((float)((double)game_rand() - DOUBLE_80614bf8)
                    / FLOAT_80614bf4) - FLOAT_80614a60;

    // Output = interpolated component + noise * noise_scale
    out_dir[0] = interp_x + (float)((double)noise_x * noise_scale);
    out_dir[1] = interp_y + (float)((double)noise_y * noise_scale);
    out_dir[2] = interp_z + (float)((double)noise_z * noise_scale);
}

// =============================================================================
// Address: 0x800bcf7c | player_ai_projectile_random_event_tick
// Per-frame tick for type-8 projectiles (likely flamethrower/spray).
// If game is not in specific mode (flag 0x40 + mode 5), and projectile
// type == 8, calls a secondary effect function with the slot's effect handle.
// =============================================================================
void player_ai_projectile_random_event_tick(void) {
    int slot_ptr;
    int proj_type;

    slot_ptr = FUN_803826ec();
    proj_type = *(int*)(slot_ptr + 0x20);

    // Skip if game mode flag 0x40 is set AND mode type == 5 (specific MP mode)
    if (((DAT_805015c0 & 0x40) != 0) && (DAT_805015d8 == 5)) {
        FUN_80382738();
        return;
    }

    // Advance RNG state
    game_rand();

    // Type 8 projectiles trigger additional effect
    if (proj_type == 8) {
        FUN_800fd5c4(*(int*)(slot_ptr + 0x7c));
    }

    FUN_80382738();
}

// =============================================================================
// Address: 0x800bd074 | player_ai_projectile_spawn_beam_particles
// Creates beam/tracer particle effects for a projectile slot.
// Character-targeting projectiles get dual beams (one per visibility mask)
// plus an oriented glow particle at the endpoint with opacity.
// Non-targeting projectiles get a single full-visibility beam + glow.
// =============================================================================
void player_ai_projectile_spawn_beam_particles(int slot) {
    int has_char_target;
    int beam_handle;
    int glow_handle;
    int player_bit;

    has_char_target = player_ai_projectile_has_character_target(slot);

    if (has_char_target == 0) {
        // Non-targeting: single beam, full visibility (0xFFFF mask)
        beam_handle = particle_spawn_beam(
            slot + 0xac, 0,
            (int*)(slot + 0x34),   // start position
            (int*)(slot + 0x7c),   // end position
            0xffff                  // all players can see
        );
        *(int*)(slot + 0xac) = beam_handle;

        // Glow at impact point
        glow_handle = particle_spawn_oriented();
        *(int*)(slot + 0xb8) = glow_handle;
    } else {
        // Character-targeting: dual beams split by player visibility
        player_bit = 1 << *(int*)(DAT_80611d54 + 0x214);

        // Beam visible only to targeting player
        beam_handle = particle_spawn_beam(
            slot + 0xac, 0,
            (int*)(slot + 0x28),   // targeting start pos
            (int*)(slot + 0x70),   // targeting end pos
            player_bit              // only targeting player sees this
        );
        *(int*)(slot + 0xac) = beam_handle;

        // Beam visible to everyone else
        beam_handle = particle_spawn_beam(
            slot + 0xb0, 0,
            (int*)(slot + 0x34),   // alt start pos
            (int*)(slot + 0x7c),   // alt end pos
            ~player_bit & 0xffff   // everyone except targeting player
        );
        *(int*)(slot + 0xb0) = beam_handle;

        // Glow for targeting player (with opacity from slot)
        glow_handle = particle_spawn_oriented();
        *(int*)(slot + 0xb4) = glow_handle;
        particle_slot_set_opacity((double)*(float*)(slot + 0x88),
                                  (int*)*(int*)(slot + 0xb4));

        // Glow for all
        glow_handle = particle_spawn_oriented();
        *(int*)(slot + 0xb8) = glow_handle;
    }

    // Set opacity on the main glow particle
    particle_slot_set_opacity((double)*(float*)(slot + 0x88),
                              (int*)*(int*)(slot + 0xb8));
}
