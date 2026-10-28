// =============================================================================
// Character Death Start & Readiness - TSFP Decompilation
// Address Range: 0x800f9c40 - 0x800fa018
// Batch: 313 | Date: 2026-08-25
// =============================================================================

#include "common.h"
#include "character.h"
#include "particle.h"
#include "entity.h"
#include "sound.h"

// External globals
extern float FLOAT_806153b0;   // 1.0f
extern float FLOAT_806153c0;   // 0.0f
extern float FLOAT_806153e0;   // Small scale constant
extern float FLOAT_80615400;   // Duration constant
extern float FLOAT_80615414;   // Speed scale
extern float FLOAT_80615460;   // Death cleanup threshold
extern float FLOAT_806154d8;   // Teleport effect duration
extern float FLOAT_806154f0;   // Alt speed for specific modes
extern float FLOAT_806154f4;   // Sound pitch offset
extern double DOUBLE_806153d0;
extern double DOUBLE_80615428;

extern int DAT_806109c0;       // Frame count
extern int DAT_80611d54;       // Game state
extern int DAT_80612610;       // Local player index
extern int DAT_80612614;       // Game mode
extern int DAT_806125fc;       // Spawn flags A
extern int DAT_80612600;       // Spawn flags B
extern int DAT_805cc614;       // Map mode
extern char DAT_805015bd;      // Input mode byte
extern char DAT_805015c0;      // Input flags
extern u32 DAT_803cc9f4;       // Explode particle def X
extern u32 DAT_803cc9f8;       // Explode particle def Y
extern u32 DAT_803cc9fc;       // Explode particle def Z
extern void* DAT_80571a68;     // Indoor explosion emitter def
extern void* DAT_80571c84;     // Outdoor explosion emitter def
extern int DAT_80611b8c;       // Zone lookup table

// External functions
extern void* FUN_803826e4(void);
extern void FUN_80382730(void);
extern int FUN_802c78a4(void);
extern int FUN_8013104c(void);
extern u32 FUN_801c0894(void);
extern int player_get_death_camera_mode(int entity);
extern void character_teleport_effect_start(double duration, void* state, int flag);
extern void character_spawn_death_dissolve(u64, double, double, double, u64, u64, u64, u64);
extern void character_opacity_fade_out(double speed, int state);
extern void character_destroy_weather_effects(void);
extern void character_damage_effect_wrapper(void);
extern void particle_detach_from_entity(int entity_id);
extern void particle_emitter_spawn(int, void*, int, float*, float*);
extern void pickup_fade_start_fadein(double, double, int);
extern void sound_init_play_params(int* params);
extern void sound_play_request(int* params);
extern int soundpad_find_zone_for_entity(int entity_id);
extern void FUN_80023280(double, double);

// =============================================================================
// Address: 0x800f9c40 | Name: character_death_start
// MASSIVE: Character death start handler.
// 1. Clears existing effects (weather, particles)
// 2. Resets effect state flags to 0x106000 (retain certain visual flags)
// 3. Plays death sound with positional audio
// 4. Selects death camera mode via player_get_death_camera_mode:
//    - Mode 3: dissolve (default) - spawn dissolve particles + opacity fade
//    - Mode 5: teleport effect
//    - Mode 6: timer countdown
//    - Mode 7: explosion - zone-dependent particle emitter (indoor vs outdoor)
//    - Mode 8: special death
//    - Mode -1: skip
// 5. Detaches all particles from entity
// 6. Starts pickup fade-in if spawn conditions met
// 7. Triggers damage effect if entity has flag 0x10000000
// =============================================================================
void character_death_start(void) {
    int* piVar3;
    int iVar4, iVar9;
    int iVar8;
    u32 uVar5;
    float fVar1;
    bool bVar2;

    piVar3 = (int*)FUN_803826e4();
    iVar9 = 3;  // Default death mode: dissolve
    iVar8 = *piVar3;

    // Destroy any existing weather/particle effects
    character_destroy_weather_effects();

    fVar1 = FLOAT_806153c0;  // 0.0f

    // Reset effect state: keep only flags 0x106000
    piVar3[1] = piVar3[1] & 0x106000;
    piVar3[0x2d] = (int)fVar1;  // Zero timer
    piVar3[0x2e] = 0;
    piVar3[2] = 0;
    piVar3[4] = (int)fVar1;
    piVar3[5] = (int)fVar1;
    piVar3[3] = -1;

    // Play death sound if game conditions are met
    iVar4 = FUN_802c78a4();
    if (iVar4 != 0 && *(int*)(((int*)*piVar3)[0x2fc] + 0x8c) >= 0) {
        // Determine speed based on input mode
        bVar2 = false;
        if (DAT_805015bd == '\n' || (DAT_805015c0 & 0x40) != 0) {
            bVar2 = true;
        }

        float speed = bVar2 ? FLOAT_806154f0 : FLOAT_80615414;

        // Compute sound parameters
        int entity = *(int*)*piVar3;
        int fps = FUN_8013104c();
        float pitch = FLOAT_806153e0 * ((float)fps / FLOAT_80615424) + FLOAT_806154f4;
        float volume = *(float*)(*(int*)(*piVar3 + 0xbf0) + 0x90);
        int sound_id = *(int*)(*(int*)(*piVar3 + 0xbf0) + 0x8c);
        int duration = (int)(volume * (float)(DAT_806109c0 ^ 0x80000000));

        // Initialize and play positional death sound
        int params[32];
        sound_init_play_params((int*)params);
        // ... set position, volume, pitch, flags ...
        sound_play_request((int*)params);
    }

    // Select death camera mode
    if (DAT_805015bd == '\n') {
        iVar9 = player_get_death_camera_mode(*piVar3);
    }

    fVar1 = FLOAT_806153c0;

    // Apply death mode
    switch (iVar9) {
        case 5:
            // Teleport death
            character_teleport_effect_start((double)FLOAT_806154d8, piVar3, 1);
            *(u32*)(*(int*)*piVar3 + 0x28) &= ~1;  // Clear active flag
            piVar3[1] |= 1;     // Set death-active
            piVar3[0x15] = 5;   // State: teleport
            piVar3[0x17] = (int)fVar1;  // Reset timer
            break;

        case 6:
        case 8:
            // Timer countdown or special death
            piVar3[1] |= 1;
            piVar3[0x15] = iVar9;
            piVar3[0x17] = (int)fVar1;
            break;

        case 7: {
            // Explosion death
            float explode_dir[3];
            explode_dir[0] = *(float*)&DAT_803cc9f4;
            explode_dir[1] = *(float*)&DAT_803cc9f8;
            explode_dir[2] = *(float*)&DAT_803cc9fc;

            piVar3[1] |= 1;
            piVar3[0x15] = 7;
            *(u32*)(*(int*)*piVar3 + 0x28) |= 1;  // Set active flag

            FUN_80023280(0.0, 1.0);

            // Choose indoor vs outdoor particle based on zone
            int zone = soundpad_find_zone_for_entity(
                (int)*(short*)(DAT_80611b8c + *(int*)(*piVar3 + 0x678) * 0x20 + 8));

            if (zone < 3 || (zone - 0xd) < 2 || zone == 0x10) {
                particle_emitter_spawn(0, &DAT_80571a68, 0,
                    (float*)(*(int*)*piVar3 + 100), explode_dir);
            } else {
                particle_emitter_spawn(0, &DAT_80571c84, 0,
                    (float*)(*(int*)*piVar3 + 100), explode_dir);
            }
            break;
        }

        case -1:
            // No death effect
            break;

        default:
            // Dissolve (mode 3 and others)
            piVar3[1] |= 1;
            piVar3[0x15] = iVar9;
            piVar3[0x17] = (int)fVar1;
            character_spawn_death_dissolve(0, 0, 0, 0, 0, 0, 0, 0);
            character_opacity_fade_out((double)FLOAT_806153b0, (int)piVar3);
            break;
    }

    // Detach all particles from this entity
    particle_detach_from_entity(*(int*)(*piVar3 + 8));

    // Start pickup fade-in if spawn conditions met
    uVar5 = FUN_801c0894();
    if ((DAT_806125fc & DAT_80612600 & uVar5 & 0x100) != 0 &&
        (DAT_80612614 != 0x65 || DAT_805cc614 == 5)) {
        pickup_fade_start_fadein(0.0, (double)FLOAT_806153e0, (int)piVar3);
    }

    // Trigger damage effect if entity has combat flag
    if ((*(u32*)(iVar8 + 0x28) & 0x10000000) != 0) {
        character_damage_effect_wrapper();
    }

    FUN_80382730();
}

// =============================================================================
// Address: 0x800fa018 | Name: character_death_is_ready_for_cleanup
// Checks if a character's death animation has exceeded its duration threshold.
// Returns 0 (not ready) if timer < threshold; 1 (ready) otherwise.
// State 5 (teleport) uses a custom duration from param_1[0x21].
// All other states compare against FLOAT_80615460.
// =============================================================================
u32 character_death_is_ready_for_cleanup(int* param_1) {
    if (DAT_80612610 <= *(int*)(*param_1 + 8) && (param_1[1] & 1) != 0) {
        if (param_1[0x15] == 5) {
            // Teleport: custom duration
            if ((float)param_1[0x17] < (float)param_1[0x21]) {
                return 0;
            }
        } else {
            // All other states: fixed threshold
            if ((float)param_1[0x17] < FLOAT_80615460) {
                return 0;
            }
        }
    }
    return 1;
}
