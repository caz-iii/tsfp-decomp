// =============================================================================
// Character Rendering & Visual Effects - TSFP Decompilation
// Address Range: 0x800f8d1c - 0x800f9bf0
// Batch: 312 | Date: 2026-08-25
// =============================================================================

#include "common.h"
#include "character.h"
#include "particle.h"
#include "collision.h"
#include "entity.h"
#include "sprite.h"
#include "weather.h"

// External globals
extern int DAT_806108e4;       // Hazard zone active counter
extern int DAT_80611d74;       // Current player/camera context
extern int DAT_80611d54;       // Game state pointer
extern int DAT_80612098;       // Total character slot count
extern int DAT_80612610;       // Local player index
extern int DAT_80612614;       // Difficulty/mode setting
extern int DAT_806109c4;       // Screen height
extern int DAT_806109c8;       // Screen width
extern int DAT_8060f950;       // Viewport height reference
extern int DAT_8060f954;       // Viewport width reference
extern void* DAT_80588610;     // Character slot array base
extern float DAT_80583228[];   // Sin/cos lookup table (2048 entries)

extern float FLOAT_806153b0;   // 1.0f
extern float FLOAT_806153bc;   // Amplitude scale
extern float FLOAT_806153c0;   // 0.0f
extern float FLOAT_806153f4;   // Angular frequency
extern float FLOAT_80615400;   // Fadeout duration
extern float FLOAT_80615414;   // Y velocity offset factor
extern float FLOAT_80615420;   // Trail particle size
extern float FLOAT_80615440;   // Particle size factor
extern float FLOAT_80615468;   // Trail length normalization
extern float FLOAT_80615478;   // Depth layer Z
extern float FLOAT_8061547c;   // Alpha scale
extern float FLOAT_80615494;   // Sprite aspect ratio
extern float FLOAT_806154c4;   // Phase rate
extern float FLOAT_806154d4;   // Height divisor
extern float FLOAT_806154dc;   // Camera offset scale
extern float FLOAT_806154e0;   // Death timer angular speed
extern float FLOAT_806154e4;   // Y gravity on trail
extern float FLOAT_806154e8;   // Sprite screen size
extern float FLOAT_806154ec;   // Trail decay rate
extern double DOUBLE_806153d0; // Int-to-float conversion offset
extern double DOUBLE_80615428; // Viewport normalization offset

// External functions
extern void FUN_803826e8(void);
extern void FUN_803826e4(void);
extern void FUN_803826ec(void);
extern void FUN_803826f0(void);
extern void FUN_80382734(void);
extern void FUN_80382738(void);
extern void FUN_8038273c(void);
extern void FUN_80382730(void);
extern void FUN_8001d4b4(void);
extern void FUN_8001d4d4(void);
extern int FUN_8016e8ac(int);
extern int FUN_8016f00c(int);
extern void FUN_8016f0e4(int, int, int, int);
extern void FUN_80172a4c(int, int, int, int, int);
extern void FUN_80172c54(void);
extern void FUN_80172dc4(double, double, double, double, int, int);
void gx_projtex_add_point(double, double, double, double, double, double, float);
extern int FUN_800258d4(int, int);
extern void FUN_800fa718(void);
extern void FUN_800fb944(void);
extern void FUN_800fbf1c(int);
extern void FUN_800fbfb4(double, double, void*, int);
extern void FUN_800fbfdc(double, double, void*, int, int);
extern void FUN_800fc444(void);
extern void FUN_800fc448(void);
extern void FUN_800fa1f8(void);
extern void FUN_800fd6d4(int);
extern void FUN_800fd788(void);
extern void FUN_800fd92c(void);

extern void character_update_attached_particles(void);
extern void character_particle_effect_tick(int*);
extern void character_damage_sound_and_particles(void);
extern void character_foot_particle_tick(int*);
extern void character_fx_bone_particle_tick(void);
extern void character_fx_bone_particle_update(void);
extern void character_teleport_effect_tick(int*);
extern void character_opacity_tick(int);
extern void character_blood_effect_tick(int*);
extern void character_damage_flash_tick(int*);
extern void character_death_effect_dispatch(int*);
extern void character_death_effect_update_particles(void);
extern void character_death_effect_noop(void);
extern void character_render_damage_trail(void);
extern void character_death_timer_render(void);
extern void pickup_visual_tick(int);
extern void pickup_bob_tick(int*);
extern void pickup_glow_decay_tick(int);
extern void pickup_spin_tick(int*);
extern void pickup_fade_start_fadeout(double, int);
extern void render_instance_clear_visual_state(int);
extern int game_is_standard_arcade(void);
extern void ai_health_check_state(void*);
extern int entity_compute_screen_bounds(int, void*, int*, int*, int*, float*, int);
extern void collision_set_shape_params(int*, u8, int, int, u8);
extern void sprite_measure_text_region_int(float*, int, int*, float*, int);
extern int sprite_get_texture_id(int);
extern void weather_slot_destroy(int*);
extern void entity_destroy_with_event(void*);
extern void particle_detach_from_entity(int);

// =============================================================================
// Address: 0x800f8d1c | Name: character_visual_effects_tick
// Main per-frame visual effects dispatcher for a character. Calls sub-effects
// based on flag bits in param_1[1]: particles, damage, foot FX, teleport,
// opacity, pickup, blood, death, glow, spin, bob, etc.
// =============================================================================
void character_visual_effects_tick(int* param_1) {
    int iVar1;

    iVar1 = *(int*)*param_1;

    if (DAT_806108e4 > 0) {
        param_1[1] = param_1[1] & ~0x40;  // Clear "needs update" flag
        character_update_attached_particles();
        character_particle_effect_tick(param_1);
        character_damage_sound_and_particles();
        character_foot_particle_tick(param_1);
        character_fx_bone_particle_tick();
        character_fx_bone_particle_update();

        if ((param_1[1] & 0x8000) != 0) {
            character_teleport_effect_tick(param_1);
        }
    }

    character_opacity_tick((int)param_1);
    pickup_visual_tick((int)param_1);
    character_blood_effect_tick(param_1);
    character_damage_flash_tick(param_1);
    character_death_effect_dispatch(param_1);

    // Flag 0x100000: weapon glow/charge effect
    if ((param_1[1] & 0x100000) != 0) {
        FUN_800fbf1c((int)param_1);
    }

    // Flag 0x200000: shield/armor effect
    if ((param_1[1] & 0x200000) != 0) {
        FUN_800fd6d4((int)param_1);
    }

    // Flag 0x2000: death active - update particles
    if ((param_1[1] & 0x2000) != 0) {
        character_death_effect_update_particles();
    }

    // Flag 0x400: pickup bob
    if ((param_1[1] & 0x400) != 0) {
        pickup_bob_tick(param_1);
    }

    // Flag 0x800: pickup glow decay
    if ((param_1[1] & 0x800) != 0) {
        pickup_glow_decay_tick((int)param_1);
    }

    // Flag 0x1000: pickup spin
    if ((param_1[1] & 0x1000) != 0) {
        pickup_spin_tick(param_1);
    }

    // Flag 0x40000: nametag display
    if ((param_1[1] & 0x40000) != 0) {
        FUN_800fa1f8();
    }

    // Flag 0x2000000: special effect A
    if ((param_1[1] & 0x2000000) != 0) {
        FUN_800fd788();
    }

    // Flag 0x8000000: special effect B
    if ((param_1[1] & 0x8000000) != 0) {
        FUN_800fd92c();
    }

    // Entity flag bit 0: call death noop (placeholder)
    if ((*(u32*)(iVar1 + 0x28) & 1) != 0) {
        character_death_effect_noop();
    }
}

// =============================================================================
// Address: 0x800f8ea0 | Name: character_nametag_render_pass
// Iterates all player slots starting from local player index. For each player
// with nametag flag (0x40000) in standard arcade mode, renders their name tag.
// Skips if player is using a specific weapon (0x24b check).
// =============================================================================
void character_nametag_render_pass(void) {
    int iVar1;
    u32 uVar2;
    int* piVar3;
    int iVar4, iVar5, iVar6;

    FUN_803826ec();

    if (DAT_80612614 == 1) {
        iVar6 = DAT_80612610 << 2;
        iVar4 = DAT_80612610;

        while (iVar4 < DAT_80612098) {
            iVar5 = *(int*)((int)&DAT_80588610 + iVar6);

            if ((*(u32*)(iVar5 + 0xc6c) & 0x40000) != 0) {
                iVar1 = game_is_standard_arcade();
                if (iVar1 != 0 && (DAT_80611d74 == 0 || *(int*)(DAT_80611d74 + 0x1390) != 0xf)) {
                    piVar3 = *(int**)(iVar5 + 0xc68);
                    iVar5 = piVar3[0x2fc];
                    if (iVar5 != 0 &&
                        (*(u32*)(iVar5 + 0x58) & 0x40000) != 0 &&
                        FUN_800258d4(*piVar3, 0x24b) == 0) {
                        goto LAB_skip;
                    }
                }
                FUN_800fa718();  // Render nametag
            }
LAB_skip:
            iVar4++;
            iVar6 += 4;
        }
    }

    FUN_80382738();
}

// =============================================================================
// Address: 0x800f8f70 | Name: character_death_timer_render
// MASSIVE: Renders the death countdown timer HUD overlay. Computes entity screen
// bounds, draws a fading countdown quad sprite via GX FIFO writes to 0xCC008000,
// then spawns 0x28 trailing death particles along a sinusoidal spiral path.
// Uses sin/cos LUT at DAT_80583228 for particle positions.
// =============================================================================
void character_death_timer_render(void) {
    int* piVar4;
    int iVar5, iVar6, iVar7, iVar9;
    u32 uVar1, uVar8;
    float local_198;
    u32 local_194, local_190, local_18c, local_188;
    float local_184, local_180, local_17c;
    double dVar3, dVar12, dVar13, dVar14, dVar15, dVar16, dVar17, dVar18, dVar19, dVar20;

    piVar4 = (int*)FUN_803826e8();
    iVar9 = *(int*)*piVar4;

    iVar5 = entity_compute_screen_bounds(iVar9, &local_188, (int*)&local_18c,
                                          (int*)&local_190, (int*)&local_194, NULL, 0);

    if (iVar5 != 0) {
        local_198 = FLOAT_80615478;  // Z depth

        int iVar6_val = *piVar4;
        iVar7 = (int)(local_194 - local_18c) / 3;

        // Camera offset for text positioning
        local_184 = -(FLOAT_806154dc * *(float*)(DAT_80611d74 + 0xc0) - *(float*)(iVar6_val + 0xbb4));
        local_180 = -(FLOAT_806154dc * *(float*)(DAT_80611d74 + 0xc4) - *(float*)(iVar6_val + 3000));
        local_17c = -(FLOAT_806154dc * *(float*)(DAT_80611d74 + 200) - *(float*)(iVar6_val + 0xbbc));

        // Compute alpha from death timer
        uVar1 = (u32)(FLOAT_8061547c * (FLOAT_806153b0 - (float)piVar4[0x17]));
        uVar8 = (uVar1 & 0xff) | 0x7f7f7f00;

        // Compute Y position from timer
        int yPos = (int)((float)piVar4[0x17] * (float)(local_194 - local_18c) + (float)local_194);

        // Measure text region
        sprite_measure_text_region_int(&local_184, 0, NULL, &local_198, 1);

        // Setup GX state
        FUN_8001d4b4();
        FUN_8016e8ac(2);
        FUN_8016f00c(2);

        // Draw countdown sprite quad
        iVar6 = sprite_get_texture_id(0x28d);
        FUN_80172a4c(2, 0x92, 0, 4, iVar6);

        // Write 4 vertices to GX FIFO (0xCC008000) for countdown quad
        // [... GX vertex writes omitted for brevity - writes position, depth, UV, color ...]

        FUN_80172c54();

        // Death particle trail
        dVar12 = (double)(FLOAT_806153f4 * FLOAT_806154e0 * (float)piVar4[0x17]);
        uVar8 = (u32)(FLOAT_806154c4 * (float)dVar12 + FLOAT_80615400);

        dVar13 = (double)(FLOAT_80615414 * (*(float*)(iVar9 + 0x1c4) / FLOAT_806154d4) +
                          FLOAT_806154e4 * *(float*)(iVar9 + 0x1c4) * (float)piVar4[0x17] +
                          *(float*)(iVar9 + 0x68));
        dVar16 = (double)(FLOAT_806153bc * *(float*)(iVar9 + 0x1c8) *
                          DAT_80583228[uVar8 & 0x7ff] + *(float*)(iVar9 + 0x6c));
        dVar14 = (double)(FLOAT_806153bc * *(float*)(iVar9 + 0x1c0) *
                          *(float*)((int)DAT_80583228 + ((uVar8 + 0x200) * 4 & 0x1ffc)) +
                          *(float*)(iVar9 + 100));

        // Render main death indicator sprite
        iVar5 = sprite_get_texture_id(0x290);
        FUN_80172dc4(/* aspect, size, alpha, ratio */ 0, 0, 0, 0, 1, iVar5);
        gx_projtex_add_point(dVar14, dVar13, dVar16,
                     (double)(FLOAT_80615420 * *(float*)(DAT_80611d54 + 0x268)),
                     0.0, 0.0, (uVar1 & 0x7f) << 24 | 0x656565);
        FUN_80172c54();

        // Render trail particle sprites (0x28 = 40 particles)
        iVar5 = sprite_get_texture_id(0x302);
        FUN_80172dc4(/* aspect, size, alpha, ratio */ 0, 0, 0, 0, 0x28, iVar5);

        u32 trailIdx = 0;
        do {
            dVar3 = FLOAT_806154c4 * (float)dVar12;
            dVar12 = (double)((float)dVar12 - FLOAT_806154ec);
            u32 phase = (u32)(dVar3 + FLOAT_80615400);
            dVar13 = (double)((float)dVar13 - *(float*)(iVar9 + 0x1c4) / FLOAT_806154d4);

            gx_projtex_add_point(
                (double)(FLOAT_806153bc * *(float*)(iVar9 + 0x1c0) *
                         *(float*)((int)DAT_80583228 + ((phase + 0x200) * 4 & 0x1ffc)) +
                         *(float*)(iVar9 + 100)),
                dVar13,
                (double)(FLOAT_806153bc * *(float*)(iVar9 + 0x1c8) *
                         DAT_80583228[phase & 0x7ff] + *(float*)(iVar9 + 0x6c)),
                (double)(FLOAT_80615440 * *(float*)(DAT_80611d54 + 0x268) *
                         (FLOAT_806153b0 - (float)trailIdx / FLOAT_80615468)),
                0.0, 0.0, uVar1 << 24 | 0x7f7f7f);

            trailIdx++;
        } while ((int)trailIdx < 0x28);

        FUN_80172c54();
    }

    FUN_80382734();
}

// =============================================================================
// Address: 0x800f95c4 | Name: character_render_callback
// Per-character render callback. Handles:
// - Arcade mode nametag display (flag 0x40000)
// - Death timer HUD rendering (state 6)
// - Shadow/outline rendering (flag 0x800 with damage trail)
// - Fading death shadow (flag 0x2000)
// - Visibility state management
// =============================================================================
void character_render_callback(void) {
    int* piVar1;
    int iVar2, iVar3, iVar4, iVar6;
    u32 uVar5;

    piVar1 = (int*)FUN_803826ec();
    iVar2 = FUN_8016f00c(1);
    iVar3 = FUN_8016e8ac(2);
    iVar6 = *(int*)*piVar1;

    if ((piVar1[1] & 0x40000) == 0) {
        goto LAB_after_nametag;
    }

    // Nametag rendering in arcade mode
    iVar4 = game_is_standard_arcade();
    if (iVar4 == 0) {
        goto LAB_after_nametag;
    }

    if (DAT_80611d74 != 0 && *(int*)(DAT_80611d74 + 0x1390) == 0xf) {
        goto LAB_render_nametag;
    }

    // Check if player has specific weapon that hides nametag
    iVar4 = ((int*)*piVar1)[0x2fc];
    if (iVar4 != 0 && (*(u32*)(iVar4 + 0x58) & 0x40000) != 0) {
        uVar5 = FUN_800258d4(*(int*)*piVar1, 0x24b);
        if (uVar5 == 0) goto LAB_after_nametag;
    }

LAB_render_nametag:
    if ((*(u32*)(iVar6 + 0x28) & 0x800000) == 0) {
        // Render nametag (same path regardless of difficulty)
        FUN_800fa718();
    }

LAB_after_nametag:
    if ((piVar1[1] & 0x40000) == 0) {
        if (DAT_80611d74 != 0 && *(int*)(DAT_80611d74 + 0x1390) == 0xf) {
            if ((*(u32*)(iVar6 + 0x28) & 0x800000) == 0) {
                FUN_800fb944();
            }
            render_instance_clear_visual_state(*(int*)(iVar6 + 4));
        }
    }

    // Death timer HUD (state 6 with active death flag)
    if ((piVar1[1] & 1) != 0) {
        if (piVar1[0x15] == 6 && (*(u32*)(iVar6 + 0x28) & 0x800000) == 0) {
            character_death_timer_render();
        }
    }

    uVar5 = piVar1[1];

    // Shadow/outline rendering
    if ((uVar5 & 0x800) != 0) {
        if (DAT_80611d74 != 0 && *(int*)*piVar1 != *(int*)(DAT_80611d74 + 0x14)) {
            if ((*(u32*)(iVar6 + 0x28) & 0x800000) == 0) {
                if ((uVar5 & 0x6000) == 0) {
                    // Normal shadow: render for each shadow source
                    iVar4 = ((int*)*piVar1)[99];
                    if (iVar4 != 0) {
                        FUN_800fbfdc(FLOAT_806153b0, 0, piVar1, 0, iVar4);
                    }
                    if (*(int*)(*piVar1 + 700) != 0) {
                        FUN_800fbfdc(FLOAT_806153b0, 0, piVar1, 0, *(int*)(*piVar1 + 700));
                    }
                } else {
                    // Death/fading shadow
                    FUN_800fbfb4(FLOAT_806153b0, 0, piVar1, 0);
                }
                goto LAB_after_shadow;
            }
        }
    }

    // Fading death shadow (flag 0x2000)
    if ((uVar5 & 0x2000) != 0) {
        if (DAT_80611d74 != 0 && *(int*)*piVar1 != *(int*)(DAT_80611d74 + 0x14)) {
            if ((*(u32*)(iVar6 + 0x28) & 0x800000) == 0) {
                FUN_800fbfb4(FLOAT_806153b0, 0, piVar1, 3);
            }
        }
    }

LAB_after_shadow:
    if ((*(u32*)(iVar6 + 0x28) & 0x800000) == 0) {
        // Damage trail
        if ((piVar1[1] & 0x80000) != 0) {
            character_render_damage_trail();
        }
        // Shield visual effect
        if ((piVar1[1] & 0x200000) != 0) {
            FUN_800fc448();
        }
    }

    // Restore render state
    FUN_8016f00c(iVar2);
    FUN_8016e8ac(iVar3);
    FUN_80382738();
}

// =============================================================================
// Address: 0x800f9838 | Name: character_render_all
// Iterates all character slots and invokes character_render_callback for each
// valid, active character with non-zero type flags and not flagged as destroyed.
// =============================================================================
void character_render_all(void) {
    int iVar1, iVar2, iVar3;

    FUN_803826f0();
    iVar3 = 0;

    for (iVar2 = 0; iVar2 < DAT_80612098; iVar2++) {
        if (*(int**)((int)&DAT_80588610 + iVar3) != NULL) {
            iVar1 = **(int**)((int)&DAT_80588610 + iVar3);
            if (iVar1 != 0) {
                if ((*(u32*)(iVar1 + 0x10) & 0xffff) != 0) {
                    if ((*(u32*)(iVar1 + 0x28) & 0x80000000) == 0 &&
                        (*(u32*)(iVar1 + 0x28) & 1) != 0) {
                        character_render_callback();
                    }
                }
            }
        }
        iVar3 += 4;
    }

    FUN_8038273c();
}

// =============================================================================
// Address: 0x800f98cc | Name: character_collision_visibility_update
// Handles character collision shape visibility:
// - State 6 (death): renders screen-space rect clipping for countdown
// - Flag 0x100000: shadow shape for weapon glow
// - Flag 0x800000: shield state update
// - Flag 0x10: AI health state check
// - Flag 0x2: sets collision shape alpha for invisible players (4 shapes)
// =============================================================================
void character_collision_visibility_update(void) {
    u32* puVar2;
    int iVar3, iVar8;
    u32 uVar4;
    int* piVar5;
    u8 uVar6, uVar7;
    int local_48, local_44, local_40;
    u32 local_3c;
    int local_38[4];

    u64 uVar9 = (u64)FUN_803826e4();
    puVar2 = (u32*)(uVar9 >> 32);
    iVar8 = (int)uVar9;
    uVar4 = puVar2[1];
    piVar5 = (int*)*puVar2;
    iVar3 = *piVar5;

    if ((uVar4 & 2) != 0) {
        // Invisible: set collision shape alpha for all 4 attached shapes
        local_38[0] = iVar3;
        local_38[1] = piVar5[99];
        local_38[2] = piVar5[0xaf];
        local_38[3] = piVar5[0x20];

        if ((puVar2[0x23] & (1 << *(int*)(DAT_80611d54 + 0x214))) == 0) {
            uVar7 = 0xff;
            uVar6 = 0;
        } else {
            uVar7 = 0xf;
            int alphaVal = (int)(FLOAT_8061547c * (FLOAT_806153b0 - (float)puVar2[0x26]));
            uVar6 = (u8)alphaVal;
        }

        for (int i = 0; i < 4; i++) {
            if (local_38[i] != 0) {
                collision_set_shape_params(*(int**)(local_38[i] + 4), uVar7, -1, 4, uVar6);
            }
        }
    } else if ((uVar4 & 1) != 0 && puVar2[0x15] == 6) {
        // Death state 6: render screen-space clip rect
        iVar3 = entity_compute_screen_bounds(iVar3, &local_3c, &local_40, &local_44, &local_48, NULL, 1);
        if (iVar3 != 0) {
            int height = local_40 - local_48;
            int yPos = (int)((float)puVar2[0x17] * (float)height);
            FUN_8016f0e4(local_3c, local_48 + yPos, local_44, local_48);
        }
    } else if ((uVar4 & 0x100000) != 0 && iVar8 == 0) {
        // Weapon glow shadow
        if ((*(u32*)(iVar3 + 0x10) & 0x4000000) == 0) {
            *(u32*)(iVar3 + 0x10) |= 0x2000000;
        } else {
            FUN_800fbfb4((double)(float)puVar2[0x5e], 0, puVar2, puVar2[0x5c]);
        }
    } else if ((uVar4 & 0x800000) != 0 && iVar8 == 1 && DAT_80611d74 == 0) {
        FUN_800fc444();
    } else if ((uVar4 & 0x10) != 0 && iVar8 == 0) {
        if ((*(u32*)(iVar3 + 0x10) & 0x4000000) == 0) {
            *(u32*)(iVar3 + 0x10) |= 0x2000000;
        } else {
            ai_health_check_state(puVar2);
        }
    }

    FUN_80382730();
}

// =============================================================================
// Address: 0x800f9b08 | Name: character_render_stub
// Empty stub for unused character render slot.
// =============================================================================
void character_render_stub(void) {
    return;
}

// =============================================================================
// Address: 0x800f9b0c | Name: character_effect_state_init
// Initializes a character effect state struct (0x1F8 bytes total).
// Zeroes all memory, sets entity reference, initializes animation slot
// indices to -1 (0xFFFFFFFF).
// =============================================================================
void character_effect_state_init(u32* param_1, u32 param_2) {
    memset_wrapper((int)param_1, 0, 0x1f8);
    *param_1 = param_2;          // Entity reference
    param_1[0x15] = 0xffffffff;  // Animation slot A = invalid
    param_1[0x1d] = 0xffffffff;  // Animation slot B = invalid
}

// =============================================================================
// Address: 0x800f9b5c | Name: character_destroy_weather_effects
// Destroys weather effect slots and up to 4 attached entities during character
// teardown. Called before entity pool free.
// =============================================================================
void character_destroy_weather_effects(void) {
    int iVar1;
    u32* puVar2;
    int iVar3, iVar4;

    iVar1 = (int)FUN_803826e8();

    // Destroy primary weather slot
    if (*(int**)(iVar1 + 0x18) != NULL) {
        weather_slot_destroy(*(int**)(iVar1 + 0x18));
        *(u32*)(iVar1 + 0x18) = 0;
    }

    // Destroy secondary weather slot
    if (*(int**)(iVar1 + 0x60) != NULL) {
        weather_slot_destroy(*(int**)(iVar1 + 0x60));
        *(u32*)(iVar1 + 0x60) = 0;
    }

    // Destroy up to 4 attached entities (offset 0x64..0x70)
    iVar4 = 0;
    iVar3 = 0;
    do {
        puVar2 = *(u32**)(iVar1 + iVar4 + 100);
        if (puVar2 != NULL) {
            entity_destroy_with_event(puVar2);
            *(u32*)(iVar1 + iVar4 + 100) = 0;
        }
        iVar3++;
        iVar4 += 4;
    } while (iVar3 < 4);

    FUN_80382734();
}

// =============================================================================
// Address: 0x800f9bf0 | Name: character_destroy_cleanup
// Character destroy handler. Chains: weather effect teardown -> pickup fadeout
// -> clear spawn flag (0x01000000) -> detach particles from entity.
// =============================================================================
void character_destroy_cleanup(int* param_1) {
    character_destroy_weather_effects();
    pickup_fade_start_fadeout((double)FLOAT_80615400, (int)param_1);
    param_1[1] = param_1[1] & ~0x01000000;
    particle_detach_from_entity(*(int*)(*param_1 + 8));
}
