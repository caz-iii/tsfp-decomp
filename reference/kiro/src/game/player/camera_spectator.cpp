// Address: 0x800a2088-0x800a22c0 | Name: camera_spectator | Date: 2026-08-11
// Spectator camera system. Manages an independent player slot (entity type 3)
// used for match spectating, cutscene viewing, and debug cameras.
// The spectator slot has its own viewport, render callback, and fade timer.

#include "common.h"

// External globals
extern int DAT_80611d30;       // Spectator player slot pointer (0 = inactive)
extern int DAT_80611d38;       // Spectator render callback ID
extern float FLOAT_80611d3c;   // Fade timer (countdown to 0)
extern float FLOAT_80611d40;   // Fade duration (initial value)
extern unsigned int DAT_805015c0; // Game flags
extern int DAT_80612614;       // Level/mode ID
extern int* DAT_80610a20;      // Display mode table pointer

extern float FLOAT_80614678;   // 0.0f
extern float FLOAT_80614768;   // Spectator near clip offset
extern float FLOAT_8061476c;   // Spectator far clip
extern float FLOAT_8060fad0;   // Base near clip distance

// External functions
extern int player_slot_alloc(int param);
extern void player_slot_set_entity(int pSlot, int entityType);
extern void camera_set_mode(int pSlot, void* pModeData, int flags);
extern void FUN_8001c710(int* pDst, int a, int b, int c, int d);
extern void FUN_8001c834(double near, double negNear, double far, int pDst);
extern void FUN_8017bd38(int w, int h, int pSlot, int* pCallbackOut);
extern void spectator_set_mode(double fadeDuration, int pModeData, int flags);
extern int spectator_is_ready(void);

// ============================================================================
// Address: 0x800a2088 | spectator_get_render_callback
// Returns spectator camera render callback ID. 8 xrefs from HUD/menu systems.
// ============================================================================
int spectator_get_render_callback(void) {
    return DAT_80611d38;
}

// ============================================================================
// Address: 0x800a2090 | spectator_set_viewport
// Sets spectator viewport index and configures viewport bounds from display
// mode table (0x58-byte stride per viewport entry).
// ============================================================================
void spectator_set_viewport(int viewportIndex) {
    if (DAT_80611d30 == 0) return;

    *(int*)(DAT_80611d30 + 0x220) = viewportIndex;

    // Lookup display mode entry
    int pEntry = *DAT_80610a20 + viewportIndex * 0x58;
    int width = *(unsigned short*)(pEntry + 0x4E) - 1;
    int height = *(unsigned short*)(pEntry + 0x52) - 1;

    // Set viewport rect: (0, 0, width, height)
    FUN_8001c710((int*)(DAT_80611d30 + 0x224), 0, 0, width, height);
}

// ============================================================================
// Address: 0x800a20f0 | spectator_create_slot
// Allocates spectator player slot, sets entity type 3, configures camera mode,
// viewport bounds, and registers render callback.
// Guards: no existing slot, flag 0x20000000 not set, level != 100.
// ============================================================================
void spectator_create_slot(void) {
    // Guard: only create if not already active, not in special mode, not level 100
    if (DAT_80611d30 != 0) return;
    if ((DAT_805015c0 & 0x20000000) != 0) return;
    if (DAT_80612614 == 100) return;

    DAT_80611d30 = player_slot_alloc(0);
    if (DAT_80611d30 != 0) {
        // Set entity type to 3 (spectator)
        player_slot_set_entity(DAT_80611d30, 3);

        // Set initial camera mode from static data at 0x80402ef0
        camera_set_mode(DAT_80611d30, (void*)0x80402EF0, 0);

        // Configure near/far clip planes
        float nearClip = FLOAT_80614768 + FLOAT_8060fad0;
        FUN_8001c834((double)nearClip, -(double)nearClip,
                     (double)FLOAT_8061476c, DAT_80611d30 + 0x224);

        // Clear interpolation flag
        *(unsigned int*)(DAT_80611d30 + 0x21C) &= 0xFFFFFFFE;

        // Register 256x256 render callback
        FUN_8017bd38(0x100, 0x100, DAT_80611d30, &DAT_80611d38);
    }

    // Reset fade timers
    FLOAT_80611d40 = FLOAT_80614678; // 0.0
    FLOAT_80611d3c = FLOAT_80614678; // 0.0
}

// ============================================================================
// Address: 0x800a21b8 | spectator_is_active
// Returns true if spectator slot is allocated (non-null). 6 xrefs.
// ============================================================================
bool spectator_is_active(void) {
    return (DAT_80611d30 != 0);
}

// ============================================================================
// Address: 0x800a21c8 | spectator_is_ready
// Returns true if spectator is active AND fade timer is complete (<=0).
// ============================================================================
int spectator_is_ready(void) {
    if (spectator_is_active() && FLOAT_80611d3c <= FLOAT_80614678) {
        return 1;
    }
    return 0;
}

// ============================================================================
// Address: 0x800a2210 | spectator_set_mode_if_ready
// Conditionally sets spectator camera mode only if spectator is ready.
// 8 xrefs from gameplay/cutscene triggers.
// ============================================================================
void spectator_set_mode_if_ready(double fadeDuration, int pModeData) {
    if (spectator_is_ready()) {
        spectator_set_mode(fadeDuration, pModeData, 0);
    }
}

// ============================================================================
// Address: 0x800a2260 | spectator_set_mode
// Sets camera mode on spectator slot with fade-in duration. Enables
// interpolation flag, records fade timer, clears user data at +0xFF8.
// ============================================================================
void spectator_set_mode(double fadeDuration, int pModeData, int flags) {
    camera_set_mode(DAT_80611d30, (void*)pModeData, flags);

    // Enable interpolation flag (bit 0)
    *(unsigned int*)(DAT_80611d30 + 0x21C) |= 1;

    // Set fade timer
    FLOAT_80611d40 = (float)fadeDuration;
    FLOAT_80611d3c = (float)fadeDuration;

    // Clear user data
    *(int*)(DAT_80611d30 + 0xFF8) = 0;
}

// ============================================================================
// Address: 0x800a22c0 | spectator_set_flags
// Clears and sets bits in spectator slot render/interpolation flags (+0x21C).
// ============================================================================
void spectator_set_flags(unsigned int clearMask, unsigned int setMask) {
    *(unsigned int*)(DAT_80611d30 + 0x21C) &= ~clearMask;
    *(unsigned int*)(DAT_80611d30 + 0x21C) |= setMask;
}

// ============================================================================
// Address: 0x800a22e8 | spectator_set_user_data
// Stores arbitrary value in spectator slot user data (+0xFF8).
// ============================================================================
void spectator_set_user_data(int value) {
    *(int*)(DAT_80611d30 + 0xFF8) = value;
}

// ============================================================================
// Address: 0x800a22f4 | spectator_tick_fade
// Per-frame spectator fade timer tick. Decrements by dt/frameCount.
// Clears interpolation flag when fade completes.
// ============================================================================
void spectator_tick_fade(void) {
    DAT_80611d34 = 0;

    if (!spectator_is_active()) return;

    // Decrement fade: timer -= dt * (1.0 / frameCount)
    float frameDuration = FLOAT_806108ec * (FLOAT_80614680 / (float)DAT_806109c0);
    FLOAT_80611d3c = FLOAT_80611d3c - frameDuration;

    if (FLOAT_80611d3c < FLOAT_80614678) {
        FLOAT_80611d3c = FLOAT_80614678; // Clamp to 0

        // If fade duration was non-zero (actual fade occurred), clear interp flag
        if (FLOAT_80614678 < FLOAT_80611d40) {
            *(unsigned int*)(DAT_80611d30 + 0x21C) &= 0xFFFFFFFE;
        }
    }
}

// ============================================================================
// Address: 0x800a2388 | spectator_instant_show
// Forces spectator to visible instantly. Sets constant fade duration, clears
// user data, disables interpolation.
// ============================================================================
void spectator_instant_show(void) {
    FLOAT_80611d3c = FLOAT_80614770; // Instant fade constant
    *(int*)(DAT_80611d30 + 0xFF8) = 0;
    *(unsigned int*)(DAT_80611d30 + 0x21C) &= 0xFFFFFFFE;
}

// ============================================================================
// Address: 0x800a23b0 | spectator_set_fov
// Sets FOV on spectator slot (+900).
// ============================================================================
void spectator_set_fov(double fov) {
    *(float*)(DAT_80611d30 + 900) = (float)fov;
}

// ============================================================================
// Address: 0x800a26cc | spectator_get_slot
// Returns spectator player slot pointer. 8 xrefs.
// ============================================================================
int spectator_get_slot(void) {
    return DAT_80611d30;
}

// ============================================================================
// Address: 0x800a26d4 | spectator_set_pip_position
// Sets X/Y screen position for PIP viewport rectangle.
// ============================================================================
void spectator_set_pip_position(double x, double y) {
    FLOAT_80611d48 = (float)x;
    fRam80611d4c = (float)y;
}

// ============================================================================
// Address: 0x800a23bc | spectator_render_overlay
// Renders static/noise overlay texture (sprite 0x333) for spectator camera.
// Random UV offset for animated noise effect. Dispatches render callback
// visibility.
// ============================================================================
void spectator_render_overlay(void) {
    FUN_803826e4(); // Save PS regs

    if (!spectator_is_active()) goto done;
    if ((*(unsigned int*)(DAT_80611d30 + 0x21C) & 1) != 0) goto done;
    if (DAT_80611d34 != 1) goto done;

    FUN_8016ee98(); // Begin 2D render

    // Get noise texture (sprite ID 0x333)
    int texId = sprite_get_texture_id(0x333);
    int pTexEntry = *DAT_80610a20 + texId * 0x58;
    unsigned int texW = (unsigned int)*(unsigned short*)(pTexEntry + 0x4E) * 0x20;
    unsigned int texH = (unsigned int)*(unsigned short*)(pTexEntry + 0x52) * 0x20;

    // Random UV offset for animated noise
    unsigned int randU = FUN_8013109c() % texW;
    unsigned int randV = FUN_8013109c() % texH;

    // Setup render state and draw fullscreen quad
    FUN_8016f0e4(0, 0, 0xFF, 0xFF);   // Set rect bounds
    FUN_8016e77c(texId);                // Bind texture
    FUN_8016f00c(0);                    // Set blend mode
    FUN_8016f4bc(0, 0, 0x100, 0x100, 0); // Draw filled rect
    FUN_80170410(0, 0, 0x100, 0x100, randU, randV, texW + randU, texH + randV); // UV mapped quad

    FUN_8016ee98(); // End 2D render

    // Update render callback visibility
    unsigned int flags = *(unsigned int*)(DAT_80611d30 + 0x21C);
    int cbId = spectator_get_render_callback();
    FUN_8017bbe4(cbId, (flags >> 18) & 1);

done:
    FUN_80382730(); // Restore PS regs
}

// ============================================================================
// Address: 0x800a24f8 | spectator_render_pip
// PIP (picture-in-picture) viewport render for spectator. Bordered rectangle
// with fade alpha, widescreen height adjust. Called from frame_render_update.
// ============================================================================
void spectator_render_pip(void) {
    FUN_803826e8(); // Save PS regs (variant)

    if (!spectator_is_active()) goto done;
    if (FLOAT_80614678 >= FLOAT_80611d3c) goto done;

    // Guard: camera mode must exist and not have flag 0x200
    if (*(int*)(DAT_80611d30 + 0x26C) == 0) goto done;
    if ((*(unsigned int*)(*(int*)(DAT_80611d30 + 0x26C) + 4) & 0x200) != 0) goto done;

    // Base height: 0xA4 (164) or 0xBA (186) for widescreen
    int pipHeight = (DAT_80612244 != 0) ? 0xBA : 0xA4;
    unsigned int alpha = 0x7F;

    // Fade-in/out alpha calculation
    if (FLOAT_80611d3c >= FLOAT_806146ac) {
        if (FLOAT_80611d3c > FLOAT_80611d40 - FLOAT_806146ac) {
            // Fade-out phase: alpha ramps down
            float fadeRatio = 1.0f - (FLOAT_80611d3c - (FLOAT_80611d40 - FLOAT_806146ac)) / FLOAT_806146ac;
            pipHeight = (int)(fadeRatio * (float)pipHeight);
        }
    } else {
        // Fade-in phase: alpha from sine curve
        alpha = FUN_803825c0((double)(FLOAT_80614774 * (FLOAT_80611d3c / FLOAT_806146ac)));
    }

    // Setup render state
    FUN_8016f0e4(0, 0, DAT_806109c4 - 1, DAT_806109c8 - 1);
    FUN_8016e8ac(0);
    FUN_8016e77c(DAT_80611d38); // Bind spectator render texture

    int pipX = (int)FLOAT_80611d48;
    int pipY = (int)fRam80611d4c;

    // Draw border + textured PIP rect
    FUN_8016f00c(1); // Border blend
    FUN_8016f4bc(pipX - 2, pipY - 2, pipX + 0xA6, pipY + pipHeight + 2, alpha & 0xFF);
    FUN_8016efe8(alpha); // Set alpha
    FUN_8016f00c(0xF); // Texture blend mode

    int pTexEntry = *DAT_80610a20 + DAT_80611d38 * 0x58;
    int texW = (unsigned int)*(unsigned short*)(pTexEntry + 0x4E) << 4;
    int texH = (unsigned int)*(unsigned short*)(pTexEntry + 0x52) << 4;
    FUN_80170410(pipX, pipY, pipX + 0xA4, pipY + pipHeight, 0, 0, texW, texH);

done:
    FUN_80382734(); // Restore PS regs (variant)
}
