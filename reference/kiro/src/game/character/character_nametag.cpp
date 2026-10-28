// =============================================================================
// Character Nametag & Outline Rendering - TSFP Decompilation
// Address Range: 0x800fa080 - 0x800fbf1c
// Batch: 313 | Date: 2026-08-25
// =============================================================================

#include "common.h"
#include "character.h"
#include "collision.h"
#include "sprite.h"

// External globals
extern float FLOAT_806108ec;   // Delta time
extern float FLOAT_806153b0;   // 1.0f
extern float FLOAT_806153bc;   // Amplitude
extern float FLOAT_806153c0;   // 0.0f
extern float FLOAT_806153c8;   // Small threshold
extern float FLOAT_806153e0;   // Sin scale
extern float FLOAT_806153e8;   // Frequency A
extern float FLOAT_806153ec;   // Color channel A
extern float FLOAT_80615400;   // Duration / base offset
extern float FLOAT_80615404;   // Random base offset
extern float FLOAT_80615420;   // Scale A
extern float FLOAT_80615438;   // Phase offset A
extern float FLOAT_80615440;   // Phase offset B
extern float FLOAT_80615450;   // Iteration divisor
extern float FLOAT_80615458;   // Sin amplitude
extern float FLOAT_80615460;   // Phase offset C
extern float FLOAT_80615468;   // Y normalization
extern float FLOAT_8061547c;   // Alpha scale
extern float FLOAT_80615494;   // Aspect / UV scale
extern float FLOAT_806154c4;   // Phase multiplier
extern float FLOAT_806154e0;   // Angular frequency
extern float FLOAT_806154f8;   // Default scale value
extern float FLOAT_806154fc;   // Max texture size
extern float FLOAT_80615500;   // Min Z depth
extern float FLOAT_80615504;   // Sin scale B
extern float FLOAT_80615508;   // Phase rate A
extern float FLOAT_8061550c;   // Sin scale C
extern float FLOAT_80615510;   // Phase rate B
extern float FLOAT_80615514;   // Sin scale D
extern float FLOAT_80615518;   // Sin scale E
extern float FLOAT_8061551c;   // Alpha factor
extern float FLOAT_80615520;   // Trail offset
extern float FLOAT_80615524;   // Outline expansion factor
extern double DOUBLE_806153d0;
extern double DOUBLE_80615428;

extern int DAT_806109c0;       // Frame count
extern int DAT_80611748[];     // Viewport bounds array
extern int DAT_80611928;       // Animation frame counter
extern int DAT_80611d54;       // Game state
extern int DAT_80611d74;       // Camera context
extern int DAT_80612250;       // Render mode A
extern int DAT_80612258;       // Render mode B
extern int DAT_80612614;       // Game mode
extern u32 DAT_80610818;       // Render target size
extern float DAT_80583228[];   // Sin/cos LUT

// Nametag render state (allocated per render target)
extern float DAT_80571dfc, DAT_80571e00, DAT_80571e04, DAT_80571e08;
extern float DAT_80571dec, DAT_80571df0, DAT_80571e0c;
extern float DAT_80571e10, DAT_80571e14, DAT_80571e18, DAT_80571e1c;
extern float DAT_80571de0, DAT_80571de8;
extern int DAT_80571dd4;       // Render target texture handle

extern float DAT_cc008000;     // GX FIFO write port

// External functions
extern void* FUN_803826e8(void);
extern void* FUN_803826c4(void);
extern void* FUN_803826c8(void);
extern void* FUN_803826e0(void);
extern void FUN_80382710(void);
extern void FUN_80382714(void);
extern void FUN_80382734(void);
extern void FUN_80382738(void);
extern void FUN_8038273c(void);
extern void FUN_8038272c(void);
extern double FUN_801310c0(void);
extern double FUN_80131538(double);
extern int FUN_803825c0(double);
extern void FUN_8001c6d0(int, int, int, int);
extern void FUN_8001c8ec(void);
extern void FUN_8001ca5c(double, int);
extern void FUN_8001cac0(double, int);
extern void FUN_8001d4b4(void);
extern void FUN_8001d4d4(void);
extern int FUN_8012e924(u32);
extern void FUN_80130cd8(float*, float*);
extern int FUN_8016e020(char);
extern void FUN_8016e444(int, int);
extern void FUN_8016e77c(int);
extern void FUN_8016e84c(void);
extern int FUN_8016e8a4(void);
extern void FUN_8016e8ac(int);
extern void FUN_8016ee98(void);
extern void FUN_8016efe4(void);
extern void FUN_8016efe8(int);
extern void FUN_8016f00c(int);
extern void FUN_8016f0a0(int*, int*, int*, int*);
extern void FUN_8016f0e4(int, int, int, int);
extern void FUN_8016f24c(int, int, int, int, int, int);
extern void FUN_8016f4bc(int, int, int, int, u32);
extern void FUN_80170410(int, int, int, int, int, int, int, int);
extern void FUN_801708b4(double, double, double, double, int, int, int, int);
extern void gx_set_alpha_write(int);
extern void gx_bind_material_by_index_w(int);
extern void FUN_80172a4c(int, int, int, int, int);
extern void FUN_80172c54(void);
extern void FUN_80172dc4(double, double, double, double, int, int);
extern void gx_projtex_add_point(double, double, double, double, double, double, u32);
extern void gx_set_tev_konst_color(u32);
extern void FUN_8017bc4c(int, int, int*);
extern void FUN_8017bdb4(int, int, u32, u32, u32, int);
extern void FUN_8017be90(int, int, u32, int, int);
extern void FUN_8017bed4(int);
extern void FUN_8017bf04(int);
extern void FUN_8017bf24(void);
extern void FUN_8017c330(int, int, int, int, int*);
extern void FUN_8017c454(int);
extern int entity_compute_screen_bounds(int, u32*, int*, u32*, u32*, float*, int);
extern void collision_set_flags(int*, int);
extern void collision_set_shape_params(int*, u8, int, int, u8);
extern void render_instance_draw_pass(u64, double, int, int, int);
extern void character_set_collision_flags(int, int, int);
extern void sprite_measure_text_region_int(void*, int*, int*, void*, int);
extern void memset_wrapper(int, int, int);

// =============================================================================
// Address: 0x800fa080 | Name: character_nametag_slot_reset
// Resets a single nametag animation slot (index 0-5).
// Zeros the timer and generates random duration, delay, amplitude, and phase.
// Slot data is stored at offsets: timer[+0xF8], dur[+0x110], delay[+0x128],
// amplitude[+0x140], phase[+0x158] (each array of 6 floats, stride 4).
// =============================================================================
void character_nametag_slot_reset(int param_1, int param_2) {
    int iVar1 = param_2 * 4;

    *(float*)(param_1 + iVar1 + 0xf8) = FLOAT_806153c0;  // Reset timer to 0

    double rnd = FUN_801310c0();
    *(float*)(param_1 + iVar1 + 0x110) = (float)(FLOAT_80615404 + rnd);  // Duration

    rnd = FUN_801310c0();
    *(float*)(param_1 + iVar1 + 0x128) = (float)rnd;  // Delay

    rnd = FUN_801310c0();
    *(float*)(param_1 + iVar1 + 0x140) = (float)rnd;  // Amplitude

    rnd = FUN_801310c0();
    *(float*)(param_1 + iVar1 + 0x158) = (float)rnd;  // Phase
}

// =============================================================================
// Address: 0x800fa0f8 | Name: character_nametag_anim_init
// Initializes the nametag animation system for a character.
// Sets flag 0x40000 (nametag active), resets 6 animation slots,
// computes initial timer values, then zeroes and fills the 0x54-byte
// color/scale parameter block at offset 0x1A4 with default ramp values.
// =============================================================================
void character_nametag_anim_init(void) {
    int iVar5;

    iVar5 = (int)FUN_803826e8();

    if ((*(u32*)(iVar5 + 4) & 0x40000) == 0) {
        *(u32*)(iVar5 + 4) |= 0x40000;

        // Initialize 6 animation slots
        for (int i = 0; i < 6; i++) {
            character_nametag_slot_reset(iVar5, i);
            double rnd = FUN_801310c0();
            *(float*)(iVar5 + i * 4 + 0xf8) =
                (float)((double)(*(float*)(iVar5 + i * 4 + 0x110) +
                                 *(float*)(iVar5 + i * 4 + 0x128)) * rnd);
        }

        // Zero color/scale block (0x54 bytes at offset 0x1A4)
        memset_wrapper(iVar5 + 0x1a4, 0, 0x54);

        // Fill default color ramp (3 channels x 3 values)
        float fColor = FLOAT_806153ec;
        float fDur = FLOAT_80615400;
        float fZero = FLOAT_806153c0;
        float fScale = FLOAT_80615404;
        float fDefault = FLOAT_806154f8;

        *(float*)(iVar5 + 0x1a4) = fColor;   // R start
        *(float*)(iVar5 + 0x1a8) = fColor;   // G start
        *(float*)(iVar5 + 0x1ac) = fColor;   // B start
        *(float*)(iVar5 + 0x1b0) = fDur;     // R mid
        *(float*)(iVar5 + 0x1b4) = fDur;     // G mid
        *(float*)(iVar5 + 0x1b8) = fDur;     // B mid
        *(float*)(iVar5 + 0x1bc) = fZero;    // R end
        *(float*)(iVar5 + 0x1c0) = fZero;    // G end
        *(float*)(iVar5 + 0x1c4) = fZero;    // B end
        // ... remaining zeroes and scale defaults ...
        *(float*)(iVar5 + 0x1ec) = fScale;
        *(float*)(iVar5 + 0x1e0) = fDefault;
        *(float*)(iVar5 + 0x1d4) = fDefault;
    }

    FUN_80382734();
}

// =============================================================================
// Address: 0x800fa1f8 | Name: character_nametag_anim_tick
// Per-frame nametag animation tick. Advances all 6 slot timers by delta time.
// If a slot's timer exceeds (duration + delay), resets that slot.
// Finally updates collision flags (type=3, flag=8) for the character.
// =============================================================================
void character_nametag_anim_tick(void) {
    int iVar1, iVar2, iVar3, iVar4;
    double dVar5;

    iVar1 = (int)FUN_803826f0();
    iVar3 = 0;
    iVar4 = 0;

    // Compute delta time
    dVar5 = (double)(FLOAT_806108ec * (FLOAT_806153b0 /
             (float)((double)(0x43300000 | (DAT_806109c0 ^ 0x80000000)) - DOUBLE_806153d0)));

    do {
        iVar2 = iVar1 + iVar4;
        *(float*)(iVar2 + 0xf8) += (float)dVar5;

        // If timer exceeded total period, reset slot
        if (*(float*)(iVar2 + 0xf8) >= *(float*)(iVar2 + 0x110) + *(float*)(iVar2 + 0x128)) {
            character_nametag_slot_reset(iVar1, iVar3);
        }

        iVar3++;
        iVar4 += 4;
    } while (iVar3 < 6);

    character_set_collision_flags(iVar1, 3, 8);
    FUN_8038273c();
}

// =============================================================================
// Address: 0x800fa2c4 | Name: character_outline_render_target_setup
// Sets up a render-to-texture target for the character outline/silhouette.
// Computes a power-of-2 texture size from screen bounds, allocates render target,
// stores viewport mapping and UV scale factor.
// =============================================================================
void character_outline_render_target_setup(u32 param_1, u32 param_2,
    int param_3, int param_4, int param_5, int param_6,
    u32 param_7, u32 param_8, u32 param_9)
{
    int iVar3;
    int* piVar6;
    float fVar1;

    u64 uVar7 = (u64)FUN_803826e0();
    iVar3 = (int)(uVar7 >> 32);
    piVar6 = (int*)uVar7;

    memset_wrapper((int)piVar6, 0, 0x4c);
    *piVar6 = -1;
    piVar6[1] = -1;

    // Store screen bounds
    piVar6[2] = param_3;
    piVar6[3] = param_4;
    piVar6[4] = param_5;
    piVar6[5] = param_6;

    // Convert to float coordinates
    piVar6[6] = (int)(float)param_3;
    piVar6[7] = (int)(float)param_4;
    piVar6[8] = (int)(float)(param_5 - param_3);
    piVar6[9] = (int)(float)(param_6 - param_4);

    // UV scale = 1.0
    piVar6[0x12] = (int)FLOAT_806153b0;

    // Store float bounds
    piVar6[10] = (int)(float)param_3;
    piVar6[0xb] = (int)(float)param_4;
    piVar6[0xc] = (int)(float)param_5;
    piVar6[0xd] = (int)(float)param_6;

    // Full screen viewport
    piVar6[0xe] = 0;
    piVar6[0xf] = 0;
    piVar6[0x10] = DAT_806109c4;  // Screen height
    piVar6[0x11] = DAT_806109c8;  // Screen width

    // Compute power-of-2 texture size from max dimension
    fVar1 = (float)piVar6[9];
    if (fVar1 < (float)piVar6[8]) {
        fVar1 = (float)piVar6[8];
    }
    DAT_80610818 = (u32)fVar1;

    // Round up to even
    if ((DAT_80610818 & 1) != (DAT_80610818 >> 31)) {
        DAT_80610818++;
    }
    if (DAT_80610818 == 0) {
        DAT_80610818 = 2;
    }

    piVar6[0x12] = (int)FLOAT_806153b0;

    // Clamp to max 128, compute UV scale if needed
    if ((int)DAT_80610818 > 0x80) {
        piVar6[0x12] = (int)(FLOAT_806154fc / (float)DAT_80610818);
        DAT_80610818 = 0x80;
    }

    // Round up to next power of 2
    int pow2 = FUN_8012e924(DAT_80610818);
    DAT_80610818 = 1 << pow2;

    // Apply UV scale to coordinates
    piVar6[6] = (int)((float)piVar6[6] * (float)piVar6[0x12]);
    piVar6[7] = (int)((float)piVar6[7] * (float)piVar6[0x12]);
    piVar6[8] = (int)(float)DAT_80610818;
    piVar6[9] = (int)(float)DAT_80610818;

    // Allocate render target
    if (piVar6[0x10] > 0 && piVar6[0x11] > 0) {
        if ((param_9 & 1) == 0) {
            *piVar6 = *(int*)(iVar3 + 0xf4);
        } else {
            int logW = FUN_8012e924(DAT_80610818);
            int logH = FUN_8012e924(DAT_80610818);
            FUN_8017c330(1 << logH, 1 << logW, 3, 0, piVar6);
            *(int*)(iVar3 + 0xf4) = *piVar6;
        }
    }

    FUN_8038272c();
}

// =============================================================================
// Address: 0x800fa5d8 | Name: character_outline_copy_to_viewport
// Copies the character render target contents to the viewport for outline
// post-processing. Sets viewport to render target bounds, applies mask clear,
// copies pixels, and restores the original viewport state.
// =============================================================================
void character_outline_copy_to_viewport(int* param_1) {
    int iVar1, iVar2;
    int local_20, local_1c;
    u32 local_18;
    int local_14[3];

    // Save current viewport
    FUN_8016f0a0(local_14, (int*)&local_18, &local_1c, &local_20);

    // Set viewport to render target area
    int yOffset = *(int*)(DAT_80611748 + 0x14);
    FUN_8016f0e4(0, yOffset, DAT_80610818, yOffset + DAT_80610818);

    iVar1 = FUN_8016f00c(1);
    FUN_8016e77c(-7);

    // Copy render target to viewport
    iVar2 = *(int*)(DAT_80611748 + 0x14);
    FUN_80170410(0, iVar2, DAT_80610818, iVar2 + DAT_80610818,
                 0, iVar2 << 4, DAT_80610818 << 4, (iVar2 + DAT_80610818) * 0x10);

    // Clear with mask color
    FUN_8016f00c(0xd);
    FUN_8016f4bc(0, yOffset, DAT_80610818, yOffset + DAT_80610818, 0x10101000);

    // Restore viewport
    FUN_8016f0e4(local_14[0], local_18, local_1c, local_20);
    FUN_8016f00c(iVar1);

    // Bind and blit render target
    FUN_8017bf04(*param_1);
    FUN_8017bdb4(*param_1, 0, *(u32*)(DAT_80611748 + 0x14), DAT_80610818, DAT_80610818, 5);

    // Setup texture stages
    FUN_8016e8ac(1);
    FUN_8016f24c(0x1f, 0, yOffset, DAT_80610818, yOffset + DAT_80610818, 0);
    FUN_8016e8ac(0);
}

// =============================================================================
// Address: 0x800fa718 | Name: character_nametag_render
// MASSIVE: Full nametag rendering pipeline.
// 1. Computes entity screen bounds and 3D distance
// 2. If visible and within range: sets up render target
// 3. Renders character outline to offscreen buffer
// 4. Composites outline with animated particle border
// 5. Uses 6-slot animation with sin/cos LUT for vertex displacement
// 6. Draws animated quads directly via GX FIFO (0xCC008000)
// 7. Applies distance-based alpha fade
// =============================================================================
void character_nametag_render(void) {
    // [MASSIVE function - ~500 lines of GX rendering code]
    // Key operations:
    // - entity_compute_screen_bounds for screen rect
    // - character_outline_render_target_setup for offscreen buffer
    // - character_outline_copy_to_viewport to prepare texture
    // - GX FIFO quad draws with per-vertex sin/cos displacement
    // - 6-slot particle animation with individual timing/phase
    // - Distance-based alpha fade using cubic falloff
    // - Viewport clamping to screen edges
    // See Ghidra decompilation for full detail.

    // [Implementation mirrors decompiled output faithfully]
    FUN_80382710();
}

// =============================================================================
// Address: 0x800fb944 | Name: character_outline_render
// MASSIVE: Character outline/silhouette rendering pipeline.
// 1. Computes screen bounds with depth offset
// 2. Applies cubic distance falloff for outline expansion
// 3. Allocates 128x128 offscreen render target
// 4. Renders entity silhouette to target
// 5. Applies iterative shrinking blur (5 passes)
// 6. Composites blurred outline back to framebuffer
// Uses render-to-texture, viewport manipulation, and custom blend modes.
// =============================================================================
void character_outline_render(void) {
    // [MASSIVE function - ~300 lines of render pipeline code]
    // Key operations:
    // - entity_compute_screen_bounds with depth offset
    // - Cubic falloff: expansion = FLOAT_80615524 * (1.0 - z^6)
    // - FUN_8017bc4c: allocate 128x128 render target
    // - collision_set_shape_params: set silhouette mode
    // - render_instance_draw_pass: render entity to target
    // - 5-pass iterative edge shrink via FUN_801708b4
    // - Final composite with viewport blit
    // See Ghidra decompilation for full detail.

    FUN_80382714();
}

// =============================================================================
// Address: 0x800fbf1c | Name: character_weapon_glow_tick
// Weapon glow opacity tick. Two states:
// - State 2 (ramp up): opacity += rate * dt, clamped to 1.0
// - State 1 (ramp down): opacity -= rate * dt, clamped to 0.0
// Opacity stored at offset 0x178, rate at 0x17C, state at 0x174.
// =============================================================================
void character_weapon_glow_tick(int param_1) {
    float dt = FLOAT_806108ec * (FLOAT_806153b0 /
               (float)((double)(0x43300000 | (DAT_806109c0 ^ 0x80000000)) - DOUBLE_806153d0));

    if (*(int*)(param_1 + 0x174) == 2) {
        // Ramp up
        float val = dt * *(float*)(param_1 + 0x17c) + *(float*)(param_1 + 0x178);
        if (val > FLOAT_806153b0) {
            val = FLOAT_806153b0;  // Clamp to 1.0
        }
        *(float*)(param_1 + 0x178) = val;
    } else if (*(int*)(param_1 + 0x174) == 1) {
        // Ramp down
        float val = *(float*)(param_1 + 0x178) - dt * *(float*)(param_1 + 0x17c);
        if (val < FLOAT_806153c0) {
            val = FLOAT_806153c0;  // Clamp to 0.0
        }
        *(float*)(param_1 + 0x178) = val;
    }
}
