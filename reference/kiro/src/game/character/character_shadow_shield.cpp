// =============================================================================
// Character Shadow & Shield Effects - TSFP Decompilation
// Address Range: 0x800fbfb4 - 0x800fd788
// Batch: 314 | Date: 2026-08-25
// =============================================================================

#include "common.h"
#include "character.h"
#include "collision.h"
#include "sprite.h"

// External globals
extern float FLOAT_806108ec;
extern float FLOAT_806153b0;   // 1.0f
extern float FLOAT_806153b4;   // Animation base frequency
extern float FLOAT_806153bc;   // Scale factor
extern float FLOAT_806153c0;   // 0.0f
extern float FLOAT_806153c8;   // Small threshold
extern float FLOAT_806153e0;   // Sin amplitude
extern float FLOAT_806153ec;   // Fade base
extern float FLOAT_806153f4;   // UV frequency
extern float FLOAT_80615400;   // Phase base / duration
extern float FLOAT_80615404;   // Scale multiplier
extern float FLOAT_80615414;   // Vertical normalize
extern float FLOAT_80615440;   // Fade start
extern float FLOAT_80615484;   // Fade ramp A
extern float FLOAT_80615494;   // UV scale
extern float FLOAT_8061547c;   // Alpha scale
extern float FLOAT_806154c0;   // Shield ramp factor
extern float FLOAT_806154c4;   // Phase multiplier
extern float FLOAT_806154d8;   // Shield frequency
extern float FLOAT_806154e0;   // Shield phase rate
extern float FLOAT_806154f8;   // Shield scale default
extern float FLOAT_80615500;   // Min depth
extern float FLOAT_80615528;   // Shadow blur scale
extern float FLOAT_8061552c;   // Shield opacity base
extern float FLOAT_80615530;   // Shield opacity scale
extern float FLOAT_80615534;   // Fade end threshold
extern float FLOAT_80615538;   // Fade ramp B
extern float FLOAT_8061553c;   // Sphere radius scale
extern float FLOAT_80615540;   // Sphere Y scale
extern float FLOAT_80615544;   // Horizontal segments normalization (10.0f)
extern float FLOAT_80615548;   // Particle ring normalization
extern float FLOAT_8061554c;   // Particle phase offset
extern float FLOAT_80615550;   // Viewport scroll factor
extern float FLOAT_80615554;   // Shield durability default
extern double DOUBLE_806153d0;
extern double DOUBLE_80615428;

extern int DAT_806109c0;
extern int DAT_80611748[];
extern int DAT_80611d54;
extern int DAT_80611d74;
extern int DAT_80612250;
extern int DAT_80612258;
extern int DAT_80612614;
extern float DAT_80583228[];
extern float DAT_cc008000;     // GX FIFO
extern void* DAT_8040c280;     // Shadow mode table
extern void* DAT_8058e3cc;

// =============================================================================
// Address: 0x800fbfb4 | Name: character_shadow_render_primary
// Wrapper that calls character_shadow_render_entity with the primary entity.
// =============================================================================
void character_shadow_render_primary(u64 param_1, double param_2, u32* param_3, u32 param_4) {
    character_shadow_render_entity(param_1, param_2, param_3, param_4, *(int*)*param_3);
}

// =============================================================================
// Address: 0x800fbfdc | Name: character_shadow_render_entity
// MASSIVE: Per-entity shadow/silhouette render. Uses shadow mode table at
// DAT_8040c280 (indexed by param_4 * 0x14) to select between:
// - Mode 0: Screen-space blur shadow (renders entity, applies blur post-process)
// - Mode 1: Offscreen RTT shadow (renders to 128x128, blits back)
// Both modes use collision_set_flags to configure silhouette rendering.
// =============================================================================
void character_shadow_render_entity(u64 param_1, double param_2, u32 param_3, u32 param_4,
                                     int param_5) {
    // [~150 lines of render pipeline code]
    // Key operations:
    // - entity_compute_screen_bounds for shadow rect
    // - collision_set_flags/collision_set_shape_params for silhouette mode
    // - render_instance_draw_pass to render entity shadow
    // - Mode 0: FUN_8018eca4 for screen-space blur
    // - Mode 1: FUN_8017c330 for RTT alloc, FUN_801748e4 for composite
    // - FUN_8016f24c for final scissor rect
}

// =============================================================================
// Address: 0x800fc444 | Name: character_shield_effect_stub
// Empty stub for unused shield effect slot.
// =============================================================================
void character_shield_effect_stub(void) {
    return;
}

// =============================================================================
// Address: 0x800fc448 | Name: character_shield_effect_render
// MASSIVE: Shield/energy bubble renderer (~500 lines).
// Generates a 21x10 = 210 vertex spherical mesh using sin/cos LUT.
// Three render passes:
// 1. Textured sphere (texture 0x333): 20 tri-strips of 10 quads each,
//    with per-vertex alpha based on vertical position and animation phase.
// 2. Screen-space distortion overlay: same mesh rendered with UV offsets
//    and random jitter for energy effect.
// 3. Ring particle sprites (texture 0x290): 20 animated quads when shield
//    is in fade state (state 2).
// Uses entity position + bone offset (0x1A8..0x1B0) for sphere center.
// =============================================================================
void character_shield_effect_render(void) {
    // [~500 lines of GX FIFO rendering]
    // Key structures:
    // - local_bb4[210*3]: vertex positions (spherical coordinates)
    // - local_1244[210*2]: UV coordinates (cos/sin based)
    // - local_1298[21]: per-ring alpha multiplier (vertical fade)
    // - 3 passes via FUN_80172a4c + DAT_cc008000 writes + FUN_80172c54
    // See Ghidra decompilation for full vertex generation + GX submission.
}

// =============================================================================
// Address: 0x800fd5c4 | Name: character_has_shield_active
// Returns bit 21 of character flags at offset 0xC6C.
// =============================================================================
u32 character_has_shield_active(int param_1) {
    return (*(u32*)(param_1 + 0xc6c) >> 21) & 1;
}

// =============================================================================
// Address: 0x800fd5d0 | Name: character_shield_effect_start
// Starts the shield visual effect on a character.
// If not already active: sets flag 0x200000, resets state/timer/strength to 0.
// If already fading (state 2): reverts to active state (state 1).
// Always sets durability to FLOAT_80615554 (default shield HP).
// =============================================================================
void character_shield_effect_start(int param_1) {
    if ((*(u32*)(param_1 + 4) & 0x200000) == 0) {
        // First activation
        *(u32*)(param_1 + 4) |= 0x200000;
        *(int*)(param_1 + 0x180) = 0;    // State: growing
        *(float*)(param_1 + 0x184) = FLOAT_806153c0;  // Timer = 0
        *(float*)(param_1 + 0x188) = FLOAT_806153c0;  // Flash = 0
    } else if (*(int*)(param_1 + 0x180) == 2) {
        // Was fading, reactivate
        *(int*)(param_1 + 0x180) = 1;  // State: active
    }

    *(float*)(param_1 + 0x18c) = FLOAT_80615554;  // Reset durability
}

// =============================================================================
// Address: 0x800fd61c | Name: character_shield_effect_fadeout
// Begins shield fadeout. Sets state to 2 and resets timer.
// Only activates if flag 0x200000 is set and not already in state 2.
// =============================================================================
void character_shield_effect_fadeout(int param_1) {
    if ((*(u32*)(param_1 + 4) & 0x200000) == 0) {
        return;
    }
    if (*(int*)(param_1 + 0x180) == 2) {
        return;
    }
    *(int*)(param_1 + 0x180) = 2;
    *(float*)(param_1 + 0x184) = FLOAT_806153c0;  // Reset timer
}

// =============================================================================
// Address: 0x800fd648 | Name: character_shield_damage_apply
// Applies damage to the shield. On hit:
// - Sets flash strength to 1.0 (if not fading)
// - Reduces durability by damage amount from param_2
// - If damage type is 0x1a (instant kill): zeroes durability
// - Zeroes the damage value after consuming it
// - Triggers fadeout when durability reaches 0
// =============================================================================
void character_shield_damage_apply(int param_1, int param_2) {
    // Set flash on hit (unless already fading)
    if ((*(u32*)(param_1 + 4) & 0x200000) != 0) {
        if (*(int*)(param_1 + 0x180) != 2) {
            *(float*)(param_1 + 0x188) = FLOAT_806153b0;  // Flash = 1.0
        }
    }

    // Apply damage
    if ((*(u32*)(param_1 + 4) & 0x200000) != 0) {
        if (*(int*)(param_2 + 0x18) == 0x1a) {
            // Instant kill damage type
            *(float*)(param_1 + 0x18c) = FLOAT_806153c0;
        } else {
            // Normal damage
            *(float*)(param_1 + 0x18c) -= *(float*)(param_2 + 0x20);
        }

        // Consume damage
        *(float*)(param_2 + 0x20) = FLOAT_806153c0;

        // Check if shield broken
        if (*(float*)(param_1 + 0x18c) <= FLOAT_806153c0) {
            character_shield_effect_fadeout(param_1);
        }
    }
}

// =============================================================================
// Address: 0x800fd6d4 | Name: character_shield_effect_tick
// Per-frame shield effect update.
// - Advances timer by delta time
// - State 0 -> 1 transition when timer > 1.0 (growth complete)
// - State 2: clears flag 0x200000 when timer > 1.0 (fade complete)
// - Decays flash strength by (threshold * dt), clamped to 0
// =============================================================================
void character_shield_effect_tick(int param_1) {
    float dt = FLOAT_806108ec * (FLOAT_806153b0 /
               (float)((double)(0x43300000 | (DAT_806109c0 ^ 0x80000000)) - DOUBLE_806153d0));

    *(float*)(param_1 + 0x184) += dt;

    if (*(int*)(param_1 + 0x180) == 0) {
        // Growing state: transition to active at 1.0s
        if (*(float*)(param_1 + 0x184) > FLOAT_806153b0) {
            *(int*)(param_1 + 0x180) = 1;
        }
    } else if (*(int*)(param_1 + 0x180) == 2) {
        // Fading state: remove effect at 1.0s
        if (*(float*)(param_1 + 0x184) > FLOAT_806153b0) {
            *(u32*)(param_1 + 4) &= ~0x200000;
        }
    }

    // Decay flash strength
    float flash = *(float*)(param_1 + 0x188) - FLOAT_806153c8 * dt;
    if (flash < FLOAT_806153c0) {
        flash = FLOAT_806153c0;
    }
    *(float*)(param_1 + 0x188) = flash;
}

// =============================================================================
// Address: 0x800fd788 | Name: character_animated_texture_tick
// Advances the animated texture timer and applies texture 0x317 to all
// collision mesh strips. Used for characters with scrolling/animated materials.
// =============================================================================
void character_animated_texture_tick(void) {
    u32* puVar1;
    int iVar2, iVar4, iVar5;

    puVar1 = (u32*)FUN_803826ec();
    iVar5 = *(int*)*puVar1;

    // Count mesh strips
    iVar2 = collision_count_strips(*(int*)(*(int*)(iVar5 + 4) + 4));

    // Advance timer
    puVar1[0x68] = (u32)((float)puVar1[0x68] +
        FLOAT_806108ec * (FLOAT_806153b0 /
        (float)((double)(0x43300000 | (DAT_806109c0 ^ 0x80000000)) - DOUBLE_806153d0)));

    // Set animated texture on all strips
    for (iVar4 = 0; iVar4 < iVar2; iVar4++) {
        u32 texId = sprite_get_texture_id(0x317);
        collision_set_strip_entry(*(u32*)(iVar5 + 4), iVar4, texId);
    }

    FUN_80382738();
}
