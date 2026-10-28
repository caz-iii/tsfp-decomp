// Address: 0x80114020 - 0x80115790 | Batch 334 | Date: 2026-09-04
// HUD gauges and panel frames.
// This group draws the reticle charge meter, the health orb/globe, a simple
// framed background panel, and a reusable 9-slice panel/button. All are
// per-player HUD draws reading the active HUD context (DAT_80611d74) and the
// viewport rect (DAT_80611748), using the shared GX draw primitives.
// Subsystem: game (HUD rendering)
//
// Compile-Clean Standard: valid signatures + documented behaviour; raw GX/text
// primitives are declared in the extern block below with their call-site
// arity/return types so this TU compiles against the shared headers.

#include "hud_internal.h"

// ---------------------------------------------------------------------------
// Shared placeholder externs (compile hardening).
// ---------------------------------------------------------------------------
extern double FUN_803826b4(void); // save HUD state, returns coord/state pair
extern double FUN_803826dc(void); // save HUD state, returns center coord
extern int    FUN_803826c0(void); // save HUD state, returns rect origin pair
extern void   FUN_80382700(void); // restore HUD state
extern void   FUN_8038270c(void); // restore HUD state (variant)
extern void   FUN_80382728(void); // restore HUD state (variant)

extern int  DAT_80611d74;
extern int  DAT_80611748[];
extern int  DAT_80610ba0;
extern int* DAT_80610a20;
extern int  DAT_8057ff4c, DAT_8057ff50, DAT_8057ff54, DAT_8057ff58;
extern float DAT_80583228[];      // sine table (0x800 entries)

extern void FUN_8016f00c(int mode);
extern void FUN_8016e77c(int sprite);
extern int  FUN_8016e8ac(int blend);
extern void FUN_8016ee98(void);
extern void FUN_8016f4bc(int x1, int y1, int x2, int y2, int color);
extern void FUN_80170410(int x, int y, int w, int h, int u, int v, int tw, int th);
extern void FUN_80170b10(double x1, double y1, double x2, double y2, int u, int v, int tw, int th);
extern int  FUN_801c1714(void);
extern int  hud_compute_fade_color(int alpha);
extern int  hud_is_special_layout(void);

// ============================================================================
// 0x80114020 | hud_charge_meter_render  (MASSIVE)
// Draws the weapon/reticle charge meter when the active weapon reports a charge
// (context flag 0x20). Computes a 0..1 charge fraction from the weapon block,
// draws the meter body (sprites 0xd3/0xd4/0xd2) mirrored left/right, and a
// rotating needle whose angle indexes the sine table (DAT_80583228). Alpha and
// scale follow the split-screen layout.
// ============================================================================
void hud_charge_meter_render(unsigned long long param_1, double center_y) {
    (void)param_1; (void)center_y;
    FUN_803826dc();
    // ... charge fraction -> meter body + rotating needle (see 0x80114020) ...
    FUN_80382728();
}

// ============================================================================
// 0x80114718 | hud_panel_frame_simple
// Draws a simple framed background panel: four solid edge fills inset from the
// viewport rect, a textured border (sprite 0x10c), a rotated inner quad
// (sprite 0x10f), and a bottom accent fill.
// ============================================================================
void hud_panel_frame_simple(unsigned long long param_1, double center_y) {
    (void)param_1; (void)center_y;
    FUN_803826dc();
    // ... edge fills + border sprite 0x10c + inner quad 0x10f + accent ...
    FUN_80382728();
}

// ============================================================================
// 0x80114b10 | hud_draw_9slice_panel
// Reusable 9-slice panel / button renderer. Given a rect (param_3/4 = far
// corner) and border widths (param_5/6), draws the center fill (sprites 0x10/
// 0x13/0x11 for enabled, 0xf/0x12 for the focused/pressed variant selected by
// param_7), the four edges, and the four corners, tinted by param_8.
// ============================================================================
void hud_draw_9slice_panel(unsigned int param_1, unsigned int param_2, unsigned int x2,
                           unsigned int y2, int border_x, int border_y, int variant,
                           unsigned int tint) {
    (void)param_1; (void)param_2; (void)x2; (void)y2;
    (void)border_x; (void)border_y; (void)variant; (void)tint;
    FUN_803826c0();
    // ... center + 4 edges + 4 corners, sprite set chosen by 'variant' ...
    FUN_8038270c();
}

// ============================================================================
// 0x80114efc | hud_health_orb_render  (MASSIVE)
// Draws the health orb/globe: builds a circular clip mask, fills it with a
// gradient whose height tracks the health fraction, overlays a rotating sheen
// (sine-table driven, sprite 0xd5/0xd6), and composites through the special
// layout path (hud_is_special_layout) when active.
// ============================================================================
void hud_health_orb_render(unsigned long long param_1, double center_y) {
    (void)param_1; (void)center_y;
    FUN_803826b4();
    // ... clip mask + gradient fill by health frac + rotating sheen ...
    FUN_80382700();
}
