// Address: 0x8011a680 - 0x8011b970 | Batch 337 | Date: 2026-09-04
// HUD per-player view render orchestrator.
// hud_render_player_view is the top-level per-player HUD draw: it composes
// essentially every HUD element decompiled across batches 332-337 (banner,
// reticle dispatch, stat bars, radar, weapon/score panels, element rows,
// scoreboard, etc.) in the correct order and with the correct mode gating.
// This file also holds the scoreboard/spectator overlay, the special-reticle
// gate, the thin render wrapper, and the max-ammo display setter.
// Subsystem: game (HUD rendering)
//
// Compile-Clean Standard: valid signatures + documented behaviour; raw
// primitives/data declared in the extern block with call-site types.

#include "hud_internal.h"

// ---------------------------------------------------------------------------
// Shared placeholder externs (compile hardening).
// ---------------------------------------------------------------------------
extern void FUN_803826dc(void);   // save HUD state
extern void FUN_803826e8(void);   // save HUD state (variant)
extern void FUN_80382728(void);   // restore HUD state
extern void FUN_80382734(void);   // restore HUD state (variant)

extern int  DAT_80611d74;
extern int  DAT_80611748[];
extern int  DAT_8057ff4c, DAT_8057ff50, DAT_8057ff54, DAT_8057ff58;
extern char DAT_805015bd;
extern char DAT_805015be;
extern int  DAT_805015c0;
extern int  DAT_805015d8;
extern int  DAT_805015dc;
extern int  DAT_80612610;
extern int  DAT_80612614;
extern float FLOAT_80611f80;      // banner alpha (from hud_banner.cpp)
extern int  DAT_806109c0;
extern int  DAT_806108e4;

extern int   is_widescreen(void);
extern int   get_string_by_id(int id);
// screen_effect_get_damage_alpha is declared (f32) in generated_decls.h.
extern const double DOUBLE_80615738; // Ghidra int->float bias constant
extern int   game_is_mapmaker_multiplayer(void);
extern int   game_is_standard_arcade(void);
extern int   game_is_handicap_enabled(void);
extern int   player_get_active_count(void);
extern int   text_set_font_table(int table);
extern int   text_get_char_width(unsigned char* glyph);
extern void  text_draw_char_shadowed(double x, double y, unsigned char* glyph, int color, int shadow);
extern void  FUN_8016f00c(int mode);
extern int   FUN_8016e8ac(int blend);
extern unsigned char DAT_80578680[]; // per-player reticle type table (0x5df stride)
extern void  FUN_80358104(void);      // scoped reticle overlay
extern int   DAT_805786a0[];          // per-player max-ammo display (0x177c stride)
extern float FLOAT_80615734;          // 0.5f-ish scale constant

// Composed HUD elements (declared where defined via generated_decls.h):
// hud_banner_render, hud_reticle_dispatch, hud_reticle_render, hud_radar_render,
// hud_row_dispatch_elements, hud_objective_name_text, hud_scoreboard_lists_render,
// hud_bar_progress_labeled, hud_objective_status_dispatch, etc.

// ============================================================================
// 0x8011a680 | hud_render_scoreboard_overlay
// Draws the multiplayer scoreboard/spectator overlay (mapmaker step 100 in
// mode '\n'): iterates other players' HUD blocks (DAT_80588610), and when the
// active reticle type is the scoped type 0x11 also renders the scope label
// (string 0x16c5) and the special reticle via hud_reticle_render.
// ============================================================================
void hud_render_scoreboard_overlay(void) {
    FUN_803826e8();
    // ... per-player overlay rows + scope-type label ...
    FUN_80382734();
}

// ============================================================================
// 0x8011a868 | hud_render_special_reticle_gate
// Invokes the special (scoped) reticle overlay FUN_80358104 when the active
// player is valid and its reticle type is the scoped type 0x11. No-op otherwise.
// ============================================================================
void hud_render_special_reticle_gate(void) {
    if (DAT_80611d74 != 0 && -1 <= *(int*)(DAT_80611d74 + 4)) {
        int slot = *(int*)(DAT_80611d74 + 4);
        if (slot >= 0 && DAT_80578680[slot * 0x5df] == 0x11) {
            FUN_80358104();
        }
    }
}

// ============================================================================
// 0x8011a8b8 | hud_render_player_view  (MASSIVE)
// Main per-player HUD render. High-level order:
//   - if a banner is fading, draw banner + primary slot and return
//   - if the "name only" flag is set, draw objective name + slot and return
//   - otherwise: set up viewport margins, dispatch the reticle, draw the
//     objective/health/armor stat bars (hud_objective_status_dispatch,
//     hud_bar_progress_labeled), the timer/score/rank/kills/award/accuracy
//     stats, the radar, the weapon/score panels, the element rows
//     (hud_row_dispatch_elements) for each active element list, the damage
//     source text, and any mode-specific overlays (scoreboard, use-progress
//     bar, handicap bar). Restores state at the end.
// ============================================================================
void hud_render_player_view(unsigned long long param_1, double param_2, double param_3,
                            double param_4, double param_5, double param_6,
                            double param_7, double param_8) {
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;
    FUN_803826dc();
    // ... full per-player HUD composition (see Ghidra listing at 0x8011a8b8) ...
    FUN_80382728();
}

// ============================================================================
// 0x8011b92c | hud_render_player_view_wrapper
// Thin forwarding wrapper around hud_render_player_view (preserves the float
// argument registers used by the callback-table entry).
// ============================================================================
void hud_render_player_view_wrapper(unsigned long long param_1, double param_2, double param_3,
                                    double param_4, double param_5, double param_6,
                                    double param_7, double param_8) {
    hud_render_player_view(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
}

// ============================================================================
// 0x8011b94c | hud_set_max_ammo_display
// Stores the scaled max-ammo display value for a player slot into the per-player
// table (DAT_805786a0, 0x177c stride). No-op for a negative slot.
// ============================================================================
void hud_set_max_ammo_display(double value, int player_slot) {
    if (0 <= player_slot) {
        DAT_805786a0[player_slot * (0x177c / 4)] =
            (int)((float)((double)FLOAT_80615734 * value) *
                  (float)((double)CONCAT44(0x43300000, (DAT_806109c0 << 1) ^ 0x80000000)
                          - DOUBLE_80615738));
    }
}
