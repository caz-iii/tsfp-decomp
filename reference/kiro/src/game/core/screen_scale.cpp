// Address: 0x80008710 | Name: screen_scale_update | Date: 2026-07-23
// Description: Updates screen-space coordinate scaling factors.
// On first call: scales a table of 30 UI element positions (DAT_803ea2c0, stride 0x14)
// by screen width ratio. Thereafter adjusts a global scale factor (DAT_803ea42c)
// based on current aspect mode (FLOAT_8061163c) and multiplayer state (DAT_80612244).
//
// Used by the UI/HUD system to support different resolutions.
// DAT_80611640 = initialized flag (set to 1 after first-time scaling)
// DAT_806109c4 = screen width
// FLOAT_8061163c = current scale factor
// FLOAT_8061362c = base scale factor
// FLOAT_80613628 = multiplayer scale factor
// DAT_80612244 = multiplayer active flag

#include "common.h"

extern s32 DAT_80611640;
extern u32 DAT_806109c4;
extern s32 DAT_80612244;
extern f32 FLOAT_8061163c;
extern f32 FLOAT_8061362c;
extern f32 FLOAT_80613628;
extern f32 FLOAT_80613620;
extern f32 DAT_803ea2c0[];  // 30 UI positions, stride 0x14
extern f32 DAT_803ea42c;

void screen_scale_update(void) {
    if (DAT_80611640 == 0) {
        // First-time init: scale all UI positions by screen width
        for (s32 i = 0; i < 30; i++) {
            // Scale X coordinate by (screen_width * factor)
            DAT_803ea2c0[i * 5] *= (f32)DAT_806109c4 * FLOAT_80613620;
        }
        DAT_80611640 = 1;
    }

    // Adjust global scale factor
    if (FLOAT_8061363c != FLOAT_8061163c) {
        DAT_803ea42c /= FLOAT_8061163c;
    }

    FLOAT_8061163c = FLOAT_8061362c;
    if (DAT_80612244 != 0) {
        FLOAT_8061163c = FLOAT_80613628;
    }

    f32 scale = FLOAT_8061362c;
    if (DAT_80612244 != 0) {
        scale = FLOAT_80613628;
    }
    DAT_803ea42c *= scale;
}
