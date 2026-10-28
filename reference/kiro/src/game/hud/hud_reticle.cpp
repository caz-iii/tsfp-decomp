// Address: 0x8011737c - 0x801188c0 | Batch 335 | Date: 2026-09-04
// HUD reticle, sniper scope, and target-marker rendering.
// This group draws the aiming reticle for the active weapon, the full sniper
// scope overlay (masked corners + crosshair), and the on-screen target lock /
// tracking markers. All read the active HUD context (DAT_80611d74) and the
// viewport rect (DAT_80611748) and use the shared GX draw primitives.
// Subsystem: game (HUD rendering)
//
// Compile-Clean Standard: valid signatures + documented behaviour; raw
// primitives/data declared in the extern block below with call-site types.

#include "hud_internal.h"

// ---------------------------------------------------------------------------
// Shared placeholder externs (compile hardening).
// ---------------------------------------------------------------------------
extern int  FUN_803826b4(void);   // save HUD state, returns state block
extern int  FUN_803826d0(void);   // save HUD state (variant)
extern void FUN_803826e0(void);   // save HUD state (variant)
extern void FUN_80382700(void);   // restore HUD state
extern void FUN_8038271c(void);   // restore HUD state (variant)
extern void FUN_8038272c(void);   // restore HUD state (variant)

extern int  DAT_80611d74;
extern int  DAT_80611748[];
extern int  DAT_80611770;         // aim/lock mode selector
extern int  DAT_80611928;         // frame counter (animation phase)
extern int  DAT_8057ff4c, DAT_8057ff50, DAT_8057ff54, DAT_8057ff58;
extern unsigned char DAT_80578680[]; // per-player reticle type table (0x5df stride)
extern int  DAT_8040c5b4[];       // reticle def table (sprite/size/mode, 0x1c stride)
extern int  DAT_8040d1c4[];       // alt reticle sprite-mode table
extern int  DAT_805015c0;         // mode flags
extern float DAT_80583228[];      // sine table

extern int* DAT_80610a20;
extern int  FUN_8016e8ac(int blend);
extern void FUN_8016ee98(void);
extern void FUN_8016f00c(int mode);
extern void FUN_8016e77c(int sprite);
extern void FUN_8016f4bc(int x1, int y1, int x2, int y2, int color);
extern void FUN_8016f7d8(double x1, double y1, double x2, double y2);
extern void FUN_8016fee0(int x1, int y1, int x2, int y2, int color);
extern void FUN_80170410(int x, int y, int w, int h, int u, int v, int tw, int th);
extern void FUN_801708b4(double x1, double y1, double x2, double y2, int u, int v, int tw, int th);
extern void FUN_8017a62c(unsigned int tw, unsigned int th, unsigned int color,
                         float* a, float* b, float* c, float* d);
extern int  FUN_803825c0(double v);
extern void FUN_8012f05c(double angle, float* out_mtx);
extern void FUN_8012fbf4(float* mtx, float* vec);
extern int  hud_is_special_layout(void);
extern int  hud_compute_fade_channel(int alpha);
extern void hud_compute_viewport_margins(void);
extern void hud_charge_meter_render(double cx, double cy);
extern int  player_is_valid_target(int ctx, int target);
extern int  player_target_in_range(int ctx, int target);
extern void FUN_80123b34(void);   // extra reticle overlay
extern void FUN_8012d954(void);   // alt reticle overlay

// ============================================================================
// 0x8011737c | hud_scope_overlay  (MASSIVE)
// Draws the full sniper-scope overlay: the darkened frame outside the scope
// circle (multiple FUN_8016f7d8 mask fills across two passes), the four corner
// bracket sprites (0x11b), the crosshair lines, and the animated reticle sprite
// (0x11d) with its cross ticks. Positions track the player's aim offset.
// ============================================================================
void hud_scope_overlay(void) {
    FUN_803826b4();
    // ... mask fills + corner brackets + crosshair + reticle (see 0x8011737c) ...
    FUN_80382700();
}

// ============================================================================
// 0x80117cd4 | hud_target_markers_render  (MASSIVE)
// Draws the on-screen target lock/tracking markers for up to 6 tracked targets.
// Per target: a screen-projected box (sprite 0x120), and when locking, rotating
// corner brackets (sprites 0x11f/0x121/0x122) whose spin/scale animate with the
// lock progress and pulse color by lock stage. Alpha follows the HUD fade.
// ============================================================================
void hud_target_markers_render(void) {
    FUN_803826d0();
    // ... per-target box + rotating lock brackets (see 0x80117cd4) ...
    FUN_8038271c();
}

// ============================================================================
// 0x801184ec | hud_reticle_render
// Main reticle draw for the active player. Resolves the per-weapon reticle type
// (DAT_80578680), picks its sprite/size/blend from the reticle def table
// (DAT_8040c5b4), adjusts for lock-on / target-in-range state, blits the
// reticle sprite at the aim point, and dispatches to hud_charge_meter_render.
// Special reticle types (0x1f/0xd/0x27) route to dedicated overlays.
// ============================================================================
void hud_reticle_render(void) {
    FUN_803826e0();
    // ... reticle type dispatch + sprite blit + charge meter ...
    FUN_8038272c();
}

// ============================================================================
// 0x8011886c | hud_reticle_render_gate
// Gate wrapper: draws the reticle (hud_reticle_render) only when the active
// player is valid, the weapon's reticle flag (DAT_80611d54+0x21c bit 2) is set,
// the no-reticle mode flag (DAT_805015c0 bit 0x200) is clear, and the reticle
// type is not the scoped type (0x11).
// ============================================================================
void hud_reticle_render_gate(void) {
    // ... guard checks, then hud_reticle_render() ...
}
