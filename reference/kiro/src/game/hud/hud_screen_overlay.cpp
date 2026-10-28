// Batch 341 | Date: 2026-09-04
// Full-screen HUD overlay sprite + a screen-effect distortion-grid init. These
// two came adjacent to the results-panel cluster but belong to the screen
// post-effect layer rather than the results screen.
//
//   0x80122b50 hud_render_fullscreen_overlay  (per-player fullscreen tint/vignette sprite)
//   0x80122e40 screen_effect_scanline_init    (4x18 distortion-grid vertex reset)
//
// Subsystem: game (HUD rendering)

#include "common.h"

// ============================================================================
// Per-file externs for raw symbols.
// ============================================================================
extern float FLOAT_80615730;   // 0.0
extern float FLOAT_80615734;   // 0.5
extern float FLOAT_8061571c;
extern float FLOAT_80615838;
extern float FLOAT_8061082c;
extern float FLOAT_80610830;

extern double DOUBLE_80615738; // 0x4330..0 int->double bias

extern int   DAT_80611d74;      // current player-view (int base)
extern int*  DAT_80611748;      // viewport rect
extern int   DAT_80578680;      // per-player HUD style table (0x5df stride)
extern int   DAT_8040c5b4;      // overlay sprite id table (0x1c stride)
extern float DAT_8040c5b8;      // overlay sprite scale table (0x1c stride)
extern int   DAT_8040d1cc;      // overlay blend mode
extern int*  DAT_80610a20;      // texture descriptor table base
extern float DAT_805824c4;      // distortion-grid vertex buffer (4 rows x 18 cols x 0x10)

// helpers (raw)
extern unsigned long long FUN_803826e8(void);   // HUD state push
extern int    FUN_80382734(void);               // HUD state pop
extern int    FUN_8016e8ac(int mode);
extern int    FUN_8016f00c(int mode);
extern int    FUN_8016e77c(int texId);
extern int    FUN_801708b4(double, double, double, double, int, int, uint, uint); // wobbled quad
extern int    sprite_get_texture_id(int spriteId);
extern void   hud_compute_viewport_margins(void);

// ============================================================================
// 0x80122b50 | hud_render_fullscreen_overlay
// Draws the current player's full-screen overlay sprite (tint / vignette /
// scope frame) selected by the per-player HUD style (DAT_80578680). Style 0x28
// draws two mirrored halves; other styles draw a single centered quad. Only
// runs when an overlay is armed (player-view flags at +0x1398/+0xdf4).
// ============================================================================
extern "C" void hud_render_fullscreen_overlay(void) {
    FUN_803826e8();
    int style = *(int*)(DAT_80611d74 + 4);
    int styleId = (&DAT_80578680)[style * 0x5df];
    int savedBlend = FUN_8016e8ac(0);
    hud_compute_viewport_margins();

    if ((*(int*)(*(int*)(DAT_80611d74 + 0x1398) + 0x74) != 0) ||
        (*(int*)(DAT_80611d74 + 0xdf4) != 0)) {
        // centre X/Y from the viewport plus the per-player overlay offset
        double cx = (double)(float)((double)(longlong)(int)(
                        *DAT_80611748 + DAT_80611748[4] +
                        (int)(*(float*)(DAT_80611d74 + 0xe04) *
                              (float)((double)(longlong)(int)(-(int)(FLOAT_8061571c *
                                  (float)((double)(longlong)(int)DAT_80611748[4] - (double)0.0))) - (double)0.0))) - (double)0.0);
        double cy = (double)(float)((double)(longlong)(int)(
                        DAT_80611748[5] + DAT_80611748[9] +
                        (int)(*(float*)(DAT_80611d74 + 0xe08) *
                              (float)((double)(longlong)(int)(-(int)(FLOAT_8061571c *
                                  (float)((double)(longlong)(int)DAT_80611748[9] - (double)0.0))) - (double)0.0))) - (double)0.0);

        int spriteId = *(int*)(&DAT_8040c5b4 + styleId * 0x1c);
        if (spriteId != -1) {
            int tex = sprite_get_texture_id(spriteId);
            double scale = (double)*(float*)(&DAT_8040c5b8 + styleId * 0x1c);
            FUN_8016f00c(DAT_8040d1cc);
            FUN_8016e77c(tex);
            if ((&DAT_80578680)[style * 0x5df] == 0x28) {
                // split (mirrored) overlay: top half + bottom half
                double halfScale = (double)(float)((double)FLOAT_80615734 * scale);
                int desc = *DAT_80610a20 + tex * 0x58;
                FUN_801708b4((double)(float)(cx - (double)(float)(scale * (double)FLOAT_8061082c)),
                             -(double)(float)(halfScale * (double)FLOAT_80610830 - cy),
                             (double)(float)(cx + (double)(float)(scale * (double)FLOAT_8061082c)), cy,
                             0, 0, (uint)*(unsigned short*)(desc + 0x4e) << 4,
                             (uint)*(unsigned short*)(desc + 0x52) << 4);
                desc = *DAT_80610a20 + tex * 0x58;
                FUN_801708b4((double)(float)(cx - (double)(float)(scale * (double)FLOAT_8061082c)), cy,
                             (double)(float)(cx + (double)(float)(scale * (double)FLOAT_8061082c)),
                             (double)(float)(halfScale * (double)FLOAT_80610830 + cy), 0,
                             (uint)*(unsigned short*)(desc + 0x52) << 4,
                             (uint)*(unsigned short*)(desc + 0x4e) << 4, 0);
            } else {
                int desc = *DAT_80610a20 + tex * 0x58;
                FUN_801708b4((double)(float)(cx - (double)(float)(scale * (double)FLOAT_8061082c)),
                             (double)(float)(cy - (double)(float)(scale * (double)FLOAT_80610830)),
                             (double)(float)(cx + (double)(float)(scale * (double)FLOAT_8061082c)),
                             (double)(float)(cy + (double)(float)(scale * (double)FLOAT_80610830)),
                             0, 0, (uint)*(unsigned short*)(desc + 0x4e) << 4,
                             (uint)*(unsigned short*)(desc + 0x52) << 4);
            }
        }
    }
    FUN_8016e8ac(savedBlend);
    FUN_80382734();
    return;
}

// ============================================================================
// 0x80122e40 | screen_effect_scanline_init
// Resets the screen-distortion vertex grid (4 rows x 18 columns, 0x10-byte
// entries at DAT_805824c4): x/y displacement = 0, a constant amplitude field,
// and a -NaN sentinel in the 4th slot (marks the vertex as unset). Called from
// screen_effect_system_init.
// ============================================================================
extern "C" void screen_effect_scanline_init(void) {
    float amp = FLOAT_80615838;
    float zero = FLOAT_80615730;
    int rowOff = 0;
    for (int row = 0; row < 4; row++) {
        int colOff = 0;
        int cols = 0x12;
        do {
            float* v = (float*)((int)&DAT_805824c4 + colOff + rowOff);
            colOff = colOff + 0x10;
            v[0] = zero;
            v[1] = zero;
            *(unsigned int*)&v[3] = 0xffc00000u;   // -NaN sentinel
            v[2] = amp;
            cols = cols + -1;
        } while (cols != 0);
        rowOff = rowOff + 0x120;
    }
    return;
}
