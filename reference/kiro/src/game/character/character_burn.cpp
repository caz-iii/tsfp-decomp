// =============================================================================
// character_burn.cpp - Character fire/burn damage system
// Address range: 0x800fd834 - 0x800fdc6c
// Date: 2026-08-25
// =============================================================================

#include "common.h"
#include "character.h"
#include "particle.h"
#include "sound.h"
#include "entity.h"

// External constants (from .rodata)
extern float FLOAT_806153b0; // Burn lethal threshold (1.0f)
extern float FLOAT_80615438; // Burn rate increment
extern float FLOAT_8061541c; // Initial burn intensity
extern float FLOAT_80615460; // Burn rate multiplier on re-ignite
extern float FLOAT_806153c0; // Zero threshold (0.0f)
extern float FLOAT_806153e8; // Initial burn rate
extern float FLOAT_80615400; // Comparison threshold
extern float FLOAT_806108ec; // Delta time base
extern float FLOAT_806153e0; // Burn decay factor
extern float FLOAT_80615440; // Particle scale factor
extern float FLOAT_806153bc; // Particle linear term
extern float FLOAT_80615420; // Sound scale factor
extern float FLOAT_80615558; // Sound linear term
extern float FLOAT_806153c4; // Sound normalization divisor
extern float FLOAT_806153ec; // Sound pitch offset
extern float FLOAT_806153cc; // Sound volume
extern float FLOAT_80615560; // Debug line default float

extern double DOUBLE_806153d0;
extern int DAT_806109c0;
extern int DAT_80611d90;
extern int DAT_80612610;

// External functions
extern void character_set_collision_flags(u32* pCharacter, int flagA, int flagB);
extern void FUN_8035b1c0(int mode, int entityId);
extern int* FUN_803826ec(void);
extern void FUN_80382738(void);
extern void FUN_8012f62c(double a, double b, double c);
extern void FUN_80130bd4(float* out, float* posA, float* posB);
extern double FUN_801310c0(void);
extern u32* particle_get_emitter_def(int idx);
extern void sound_update_3d_source(double, double, double, int, int, int, int);
extern void sound_stop_handle(void);
extern void switch_slot_reset(int* slot);
extern void player_ai_explosion_spawn(int, int, float*, float*, int, int, int);
extern void character_on_death_handler(u32, double, double, double, double, double, double, double);
extern void player_death_disable_entity(int entity);

// =============================================================================
// Address: 0x800fd834 | Name: character_burn_check_death_collision
//
// When burn damage exceeds threshold, enable death collision flags and notify
// AI system. Otherwise, clear the active burn bit.
// Called by: character_death_effect_dispatch
// =============================================================================
void character_burn_check_death_collision(u32* pCharacter) {
    if (*(float*)&pCharacter[0x17] < FLOAT_806153b0) {
        // Burn not yet lethal - enable death collision
        character_set_collision_flags(pCharacter, 3, 8);
        FUN_8035b1c0(1, *(int*)*pCharacter);
    } else {
        // Burn complete - clear active flag
        pCharacter[1] = pCharacter[1] & 0xFFFFFFFE;
    }
}

// =============================================================================
// Address: 0x800fd8a0 | Name: character_burn_start_or_intensify
//
// Start fire effect on character or intensify existing burn.
// Sets flag 0x8000000 and configures burn rate + intensity.
// Called by: player_ai_projectile_arrow_tick
// =============================================================================
void character_burn_start_or_intensify(int pCharacter, u32 attackerEntity) {
    float burnRate;
    float initialIntensity;

    burnRate = FLOAT_80615438;
    initialIntensity = FLOAT_8061541c;

    if ((*(u32*)(pCharacter + 4) & 0x8000000) != 0) {
        // Already burning - intensify
        *(float*)(pCharacter + 400) = *(float*)(pCharacter + 400) * FLOAT_80615460;
        *(float*)(pCharacter + 0x198) = *(float*)(pCharacter + 0x198) + burnRate;
        *(u32*)(pCharacter + 0x19c) = attackerEntity;
        return;
    }

    // Start new burn
    *(u32*)(pCharacter + 4) = *(u32*)(pCharacter + 4) | 0x8000000;
    burnRate = FLOAT_806153c0;
    *(float*)(pCharacter + 400) = initialIntensity;
    *(float*)(pCharacter + 0x194) = burnRate;
    *(float*)(pCharacter + 0x198) = FLOAT_806153e8;
    *(u32*)(pCharacter + 0x19c) = attackerEntity;
}

// =============================================================================
// Address: 0x800fd8fc | Name: character_burn_is_lethal
//
// Returns 1 if burn has reached lethal threshold.
// =============================================================================
int character_burn_is_lethal(int pCharacter) {
    int result = 0;

    if ((*(u32*)(pCharacter + 4) & 0x8000000) != 0) {
        if (FLOAT_80615400 <= *(float*)(pCharacter + 0x198)) {
            result = 1;
        }
    }
    return result;
}

// =============================================================================
// Address: 0x800fd92c | Name: character_burn_tick
//
// Per-frame burn damage tick. Manages burn intensity ramp, scales particle
// emitter, updates 3D burn sound, then triggers death + explosion when lethal.
// Called by: character_visual_effects_tick
// =============================================================================
void character_burn_tick(void) {
    int* pBurnState;
    int pEntity;
    int emitterIdx;
    double deltaTime;
    double burnScale;
    float localPos[3];
    float localDir[3];
    float boneBuffer[16];
    int switchSlot[4]; // local_90 region
    // ... additional locals omitted for clarity

    pBurnState = (int*)FUN_803826ec();
    int bHasEmitter = 0;
    pEntity = *(int*)*pBurnState;

    if ((pEntity != 0) && (*(int*)(pEntity + 0x150) != 0)) {
        bHasEmitter = 1;
    }

    if (bHasEmitter) {
        emitterIdx = *(int*)(*(int*)(pEntity + 0x150) + 4);
    } else {
        emitterIdx = -1;
    }

    // Compute delta time from frame counter
    deltaTime = (double)(FLOAT_806108ec *
                (FLOAT_806153b0 / (float)((double)(DAT_806109c0 ^ 0x80000000) - DOUBLE_806153d0)));

    // Update burn intensity over time
    if ((double)FLOAT_80615400 <= (double)*(float*)&pBurnState[0x66]) {
        // Active burning phase - ramp toward 1.0
        if (FLOAT_806153c0 < *(float*)&pBurnState[100]) {
            pBurnState[0x65] = (int)(*(float*)&pBurnState[0x65] +
                (float)(deltaTime * (double)(FLOAT_806153b0 - *(float*)&pBurnState[0x65])) /
                *(float*)&pBurnState[100]);
        }
        pBurnState[100] = (int)(float)((double)*(float*)&pBurnState[100] - deltaTime);
    } else {
        // Decaying phase
        pBurnState[0x66] = (int)-(float)((double)FLOAT_806153e0 * deltaTime -
                           (double)*(float*)&pBurnState[0x66]);
        if ((double)*(float*)&pBurnState[0x65] <= (double)*(float*)&pBurnState[0x66]) {
            pBurnState[0x65] = (int)(float)((double)FLOAT_806153e8 * deltaTime +
                               (double)*(float*)&pBurnState[0x65]);
            if (*(float*)&pBurnState[0x66] < *(float*)&pBurnState[0x65]) {
                pBurnState[0x65] = pBurnState[0x66];
            }
        } else {
            pBurnState[0x65] = pBurnState[0x66];
            if (*(float*)&pBurnState[0x65] <= FLOAT_806153c0) {
                pBurnState[0x65] = (int)FLOAT_806153c0;
                pBurnState[1] = pBurnState[1] & 0xF7FFFFFF; // Clear burn flag
            }
        }
    }

    // Scale particle emitter based on burn intensity
    if (emitterIdx >= 0) {
        double intensity2 = (double)(*(float*)&pBurnState[0x65] * *(float*)&pBurnState[0x65]);
        burnScale = FUN_801310c0();
        burnScale = (double)(FLOAT_80615440 * (float)(intensity2 * (double)(float)(intensity2 * burnScale)) +
                    (float)((double)FLOAT_806153bc * intensity2 + (double)FLOAT_806153e0));
        u32* emitterDef = particle_get_emitter_def(emitterIdx);
        emitterDef[0x70] = (u32)(float)burnScale;
    }

    // Update burn 3D sound
    double intensity = (double)*(float*)&pBurnState[0x65];
    burnScale = FUN_801310c0();
    burnScale = (double)(float)((double)(float)((double)FLOAT_80615420 * intensity) * burnScale +
                (double)(float)((double)FLOAT_80615558 * intensity + (double)FLOAT_806153b0));
    FUN_8012f62c((double)FLOAT_806153b0, burnScale, burnScale);

    // Update bone transforms for fire positioning
    int boneCount = *(int*)(*(int*)(*(int*)(pEntity + 4) + 4) + 4);
    int offset = 0;
    for (int i = 0; i < boneCount; i++) {
        float* pBone = (float*)(*(int*)(*(int*)(pEntity + 4) + 0x10) + offset);
        FUN_80130bd4(boneBuffer, pBone, pBone);
        offset += 0x40;
    }

    // Update positional audio
    double soundPitch = (double)(FLOAT_80615400 + (float)(burnScale / (double)FLOAT_806153c4));
    sound_update_3d_source(
        (double)(float)(soundPitch - (double)FLOAT_806153ec),
        (double)FLOAT_806153cc,
        soundPitch,
        pEntity + 0x22c, pEntity + 100, 0x630, 1);

    // Check for death trigger
    if (*(float*)&pBurnState[100] <= FLOAT_806153c0) {
        switch_slot_reset((int*)&switchSlot);
        // Compute explosion position from entity + offset
        localPos[0] = *(float*)(pEntity + 100) + *(float*)(pEntity + 0x1a8);
        localPos[1] = *(float*)(pEntity + 0x68) + *(float*)(pEntity + 0x1ac);
        localPos[2] = *(float*)(pEntity + 0x6c) + *(float*)(pEntity + 0x1b0);
        localDir[0] = FLOAT_806153c0;
        localDir[1] = FLOAT_806153b0;
        localDir[2] = FLOAT_806153c0;

        sound_stop_handle();
        *(u32*)(pEntity + 0x22c) = 0xFFFFFFFF;

        player_ai_explosion_spawn(0x2c, 0x17, localPos, localDir, pEntity, pEntity, 1);

        // Set up death info
        int ownerEntityId = 0;
        if (pBurnState[0x67] >= 0) {
            ownerEntityId = *(int*)(DAT_80611d90 + pBurnState[0x67] * 0xEA0);
        }

        character_on_death_handler(0 /*uVar9*/, (double)localPos[2], (double)*(float*)(pEntity + 0x6c),
                                   deltaTime, 0, 0, 0, 0);

        pBurnState[1] = pBurnState[1] & 0xF7FFFFFF; // Clear burn flag

        if (*(int*)(*pBurnState + 8) < DAT_80612610) {
            player_death_disable_entity(pEntity);
        } else {
            *(u32*)(pEntity + 0x28) = *(u32*)(pEntity + 0x28) & 0xFFFFFFFE;
        }
    }

    FUN_80382738();
}

// =============================================================================
// Address: 0x800fdc6c | Name: character_burn_stub_nop
// Empty stub.
// =============================================================================
void character_burn_stub_nop(void) {
    return;
}
