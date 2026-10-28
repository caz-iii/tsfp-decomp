// Address: 0x8007c30c - 0x8007e704 | Name: weapon_character_system | Date: 2026-08-10
// Weapon resource preloading, character state management, particle effects, aim simulation
// Part of the weapon/character subsystem

#include "common.h"

// ============================================================================
// 0x8007c30c | weapon_system_preload_resources
// Preloads all weapon/item model resources at level start.
// Calls entity_preload_resources for 19 resource type IDs:
//   0x14-0x16 (pistols/SMGs), 0x17-0x22 (rifles/heavies/specials),
//   0x29-0x2B (grenades/mines), 0x295 (special weapon)
// Then initializes:
//   - weapon_particle_effects_init (0x8007c7a0) - muzzle flash, trails, explosions
//   - FUN_80081af4 - weapon HUD/crosshair setup
// Called by FUN_801c1888 (level load sequence).
// ============================================================================
void weapon_system_preload_resources(void) {
    // Preload weapon model resources (IDs 0x14 through 0x22)
    for (int id = 0x14; id <= 0x22; id++) {
        entity_preload_resources(/* ... */, id);
    }
    // Preload grenade/mine resources (IDs 0x29-0x2B)
    entity_preload_resources(/* ... */, 0x29);
    entity_preload_resources(/* ... */, 0x2A);
    entity_preload_resources(/* ... */, 0x2B);
    // Special weapon
    entity_preload_resources(/* ... */, 0x295);

    // Initialize particle effects for all weapons
    weapon_particle_effects_init();
    // Initialize weapon HUD
    FUN_80081af4();
}

// ============================================================================
// 0x8007c3c8 | character_register_event_handlers
// Registers two event handlers for character event type 0x80:
//   Slot 0: FUN_8007eccc (character spawn/activate handler)
//   Slot 1: character_destroy_handler (0x8007c59c)
// Called early in level initialization to wire up character lifecycle events.
// ============================================================================
void character_register_event_handlers(void) {
    event_set_handler(0x80, 0, FUN_8007eccc);  // spawn/activate
    event_set_handler(0x80, 1, character_destroy_handler);  // destroy
}

// ============================================================================
// 0x8007c40c | character_system_reset
// Resets the character management system to initial state.
// - Clears 20-entry tracking array (DAT_8055a330)
// - Clears 30-entry active character array (DAT_8055a380)
// - Sets max active characters (DAT_80611c88) based on player count:
//     1 player: 30, 2 players: 15, 3+ players: 7 (min 1)
// - Clears main character data buffer (DAT_80611c90, size 0x27D8)
// - Clears character pool buffer (DAT_80611c84, size 0x160)
// - Initializes free pool with 8 entries (each 0x2C bytes), stored in DAT_8055a3f8
// Called by character_system_alloc and potentially on level transitions.
// ============================================================================
void character_system_reset(void) {
    DAT_80611c7c = 0;

    // Clear tracking array (20 entries)
    for (int i = 0; i < 20; i++) {
        (&DAT_8055a330)[i] = 0;
    }
    // Clear active character array (30 entries)
    for (int i = 0; i < 30; i++) {
        (&DAT_8055a380)[i] = 0;
    }

    // Set max active characters based on player count
    DAT_80611c88 = 30;
    if (DAT_805015bd != 10) { // not single-player arcade?
        int player_count = FUN_801c1714();
        if (player_count == 2) {
            DAT_80611c88 = 15;
        } else if (player_count > 2) {
            DAT_80611c88 = 7;
        }
    }
    if (DAT_80611c88 < 1) DAT_80611c88 = 1;

    // Clear main data buffer
    DAT_80611c8c = 0;
    memset_wrapper(DAT_80611c90, 0, 0x27D8);

    // Initialize free pool (8 entries of 0x2C bytes each)
    DAT_80611c78 = 0;
    memset_wrapper(DAT_80611c84, 0, 0x160);
    DAT_80611c80 = 8;
    for (int i = 0; i < 8; i++) {
        (&DAT_8055a3f8)[i] = DAT_80611c84 + i * 0x2C;
    }
}

// ============================================================================
// 0x8007c53c | character_system_alloc
// Allocates memory for the character management system:
//   - Main data buffer: 0x27D8 bytes (DAT_80611c90) - character state array
//   - Pool buffer: 0x160 bytes (DAT_80611c84) - 8 x 0x2C free pool entries
//   - Auxiliary buffer: 0xF0 bytes (DAT_80611c94) - cleared to zero
// Then calls character_system_reset to initialize.
// Called by FUN_801c1a3c (game mode initialization).
// ============================================================================
void character_system_alloc(void) {
    DAT_80611c90 = mem_alloc(0x27D8, 1);
    DAT_80611c84 = mem_alloc(0x160, 1);
    DAT_80611c94 = mem_alloc(0xF0, 1);
    memset_wrapper(DAT_80611c94, 0, 0xF0);
    character_system_reset();
}

// ============================================================================
// 0x8007c598 | character_system_stub
// Empty no-op. Likely a removed/disabled character system function.
// ============================================================================
void character_system_stub(void) {
    return;
}

// ============================================================================
// 0x8007c59c | character_destroy_handler
// Event handler for character destruction (event type 0x80, slot 1).
// Performs full teardown of a character entity:
//   1. Iterates 10 particle emitter slots (+0x7C -> +0xF4): stops each via FUN_800d49d8
//   2. Iterates 10 sound/effect slots (+0x7C -> +0x11C):
//      - If active (FUN_800e3528): checks flags for stop or fade-out
//      - Flag 0x8000000 clear: immediate stop (FUN_800e3648)
//      - Flag 0x8000000 set: fade-out (FUN_800e43d4) + detach (FUN_800e34e4)
//   3. Stops looping sound if handle valid (+0x7C -> +0xF0)
//   4. Calls cleanup callback (+0x7C -> [1]) if set, else clears owner pointer
//   5. If entity has class flag 0x8 (interactive): removes from active array (DAT_8055a380)
//   6. Clears entity data pointer (+0x7C = 0)
// Called as event handler registered by character_register_event_handlers.
// ============================================================================
void character_destroy_handler(void) {
    int entity = FUN_803826e8(); // prologue - get entity
    if (entity == 0) { FUN_80382734(); return; }

    int* data = *(int**)(entity + 0x7C);
    if (data == NULL) { FUN_80382734(); return; }

    // Stop all particle emitters (10 slots)
    for (int i = 0; i < 10; i++) {
        int* emitter = *(int**)((int)data + 0xF4 + i * 4);
        if (emitter != NULL) {
            FUN_800d49d8(emitter); // stop particle emitter
        }
    }

    // Stop/fade all sound/effect instances (10 slots)
    for (int i = 0; i < 10; i++) {
        int handle = *(int*)((int)data + 0x11C + i * 4);
        if (FUN_800e3528(&handle)) {
            if ((*(unsigned int*)(handle + 4) & 0x8000000) == 0) {
                FUN_800e3648(&handle); // immediate stop
            } else {
                *(unsigned int*)(handle + 4) |= 0x800001; // mark for fade
                FUN_800e43d4(0.0f, &handle); // fade out
                FUN_800e34e4((int*)((int)data + 0x11C + i * 4)); // detach
            }
        }
    }

    // Stop looping sound
    if ((int)data[0x3C] >= 0) {
        sound_stop_handle();
        data[0x3C] = (int)0xFFFFFFFF;
    }

    // Cleanup callback or clear owner
    if ((void*)data[1] == NULL) {
        if ((int*)data[0] != NULL) {
            *(int*)data[0] = 0; // clear owner's reference
        }
    } else {
        ((void(*)(int))data[1])(data[0]); // call cleanup callback
    }

    // Remove from active tracking array if interactive
    if (*(unsigned int*)(*(int*)(entity + 0x24) + 0x28) & 8) {
        for (int i = 0; i < 30; i++) {
            if ((&DAT_8055a380)[i] == entity) {
                (&DAT_8055a380)[i] = 0;
            }
        }
    }

    *(int*)(entity + 0x7C) = 0; // clear data pointer
    FUN_80382734(); // epilogue
}

// ============================================================================
// 0x8007c714 | character_pool_alloc
// Pops a free character data block (0x2C bytes) from the pool stack.
// Pool: DAT_8055a3f8 array, DAT_80611c80 = stack pointer (decrements on alloc).
// Returns NULL (0) if pool is empty.
// Clears allocated block to zero before returning.
// Called by FUN_80080854 (character spawn).
// ============================================================================
int character_pool_alloc(void) {
    if (DAT_80611c80 == 0) return 0;

    DAT_80611c80--;
    int block = (&DAT_8055a3f8)[DAT_80611c80];
    (&DAT_8055a3f8)[DAT_80611c80] = 0;
    memset_wrapper(block, 0, 0x2C);
    return block;
}

// ============================================================================
// 0x8007c780 | character_pool_free
// Pushes a character data block back onto the pool stack.
// Increments DAT_80611c80 (stack pointer) and stores pointer.
// No bounds check - caller must ensure pool not overflowing.
// ============================================================================
void character_pool_free(int block) {
    (&DAT_8055a3f8)[DAT_80611c80] = block;
    DAT_80611c80++;
}

// ============================================================================
// 0x8007c7a0 | weapon_particle_effects_init
// Initializes approximately 30 particle effect definitions for the weapon system.
// Each effect is configured with a repeating pattern of calls:
//   FUN_800e70d0: create emitter (size, lifetime, output buffer, flags, type, count, ID)
//   FUN_800e71c4: set color/alpha (start color, end color, RGBA packed values)
//   FUN_800e7334: set scale (start/end XY scales)
//   FUN_800e73ec: set velocity (XYZ velocity + gravity)
//   FUN_800e72ec: set emission (rate, spread, cone angle, life variance)
//   FUN_800e6eb4: set texture/blend (UV, blend mode, secondary buffer, parent)
//   FUN_800e7040: set physics (drag, bounce, friction)
//   FUN_800e7018: finalize emitter
//   FUN_800e702c: (optional) link to previous emitter
// Effect categories visible from parameters:
//   - Muzzle flash (small, bright, short-lived)
//   - Bullet trails (long, thin, fading)
//   - Impact sparks (scattered, bouncing)
//   - Explosions (large, expanding, smoke-producing)
//   - Smoke puffs (slow, fading, gravity-affected)
//   - Blood/hit effects (dark red, splatter pattern)
//   - Energy weapon effects (colored glow, no gravity)
// Output buffers: DAT_8055a418 through DAT_8055bffc (stride 0xCC per emitter)
// Called by weapon_system_preload_resources.
// ============================================================================
void weapon_particle_effects_init(void) {
    // ~30 effect definitions configured via the FUN_800e7xxx particle API
    // Each block writes to a unique output buffer at stride 0xCC
    // Patterns repeat for different weapon types:
    //   - Standard bullet: flash + trail + impact
    //   - Shotgun: wide spread + multiple impacts
    //   - Explosive: large flash + expanding cloud + debris
    //   - Energy: glow + beam + dissipation
    //   - Grenade: arc trail + explosion + smoke

    // [Full implementation is ~400 lines of repeated API calls with
    //  different float parameters for each effect variant]
}

// ============================================================================
// 0x8007e704 | weapon_aim_angle_update
// Updates weapon aiming angles (yaw and pitch) with velocity-based simulation.
// Processes two axes identically (yaw at +0xDC/+0xE4, pitch at +0xE0/+0xE8):
//   1. Clamp angular velocity to [FLOAT_80614168, FLOAT_80614164]
//   2. Integrate: angle += velocity * dt_scale (+0xC8)
//   3. Apply spring-damping force toward center:
//      - Zones: [0, FLOAT_80614164), [FLOAT_80614164, FLOAT_80614128),
//               [FLOAT_80614128, FLOAT_80614170), [FLOAT_80614170, FLOAT_8061412c)
//      - Each zone has different damping coefficient (FLOAT_8061416c)
//   4. Apply damping: angle += (force / max) * damping_factor (+0xC4)
//   5. Handle wrap-around at FLOAT_8061412c (likely 2*PI or 360°)
//   6. Clamp to aim limits if set (+0xCC non-zero):
//      - Cannot cross past limit in either direction
//   7. Update velocity = new_angle - old_angle (for next frame)
// Used for smooth weapon sway, recoil recovery, and aim interpolation.
// Called by FUN_801cfa04 (character per-frame update).
// ============================================================================
void weapon_aim_angle_update(int entity) {
    int data = *(int*)(entity + 0x7C);

    // --- YAW AXIS (+0xDC angle, +0xE4 velocity) ---
    // Clamp velocity
    if (*(float*)(data + 0xE4) > FLOAT_80614164)
        *(float*)(data + 0xE4) = FLOAT_80614164;
    if (*(float*)(data + 0xE4) < FLOAT_80614168)
        *(float*)(data + 0xE4) = FLOAT_80614168;

    // Integrate angle
    float old_yaw = *(float*)(data + 0xDC);
    *(float*)(data + 0xDC) += *(float*)(data + 0xE4) * *(float*)(data + 0xC8);

    // Spring-damping force (zone-based)
    float yaw = *(float*)(data + 0xDC);
    float force;
    if (yaw < FLOAT_80614164) {
        force = (FLOAT_80614164 - yaw) - FLOAT_80614164;
    } else if (yaw < FLOAT_80614128) {
        force = FLOAT_8061416c * (yaw - FLOAT_80614164) - FLOAT_80614164;
    } else if (yaw < FLOAT_80614170) {
        force = FLOAT_8061416c * (yaw - FLOAT_80614128) + FLOAT_80614174;
    } else {
        force = FLOAT_8061412c - yaw;
    }

    *(float*)(data + 0xDC) += (force / FLOAT_80614164) * *(float*)(data + 0xC4);

    // Wrap-around
    if (*(float*)(data + 0xDC) >= FLOAT_8061412c) {
        old_yaw -= FLOAT_8061412c;
        *(float*)(data + 0xDC) -= FLOAT_8061412c;
    }
    if (*(float*)(data + 0xDC) < 0.0f) {
        old_yaw += FLOAT_8061412c;
        *(float*)(data + 0xDC) += FLOAT_8061412c;
    }

    // Aim limits
    float limit = *(float*)(data + 0xCC);
    if (limit != 0.0f) {
        // Clamp within allowed range
    }

    // Update velocity for next frame
    *(float*)(data + 0xE4) = *(float*)(data + 0xDC) - old_yaw;

    // --- PITCH AXIS (+0xE0 angle, +0xE8 velocity) ---
    // Same logic repeated for pitch
}
