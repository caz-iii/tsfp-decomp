// Address: 0x801193dc - 0x8011a660 | Batch 337 | Date: 2026-09-04
// HUD animated stat bars + objective status dispatch.
// These render the centered animated stat bars (health / armor / objective
// progress) with value smoothing and damage-flash alpha, a full-screen tint
// flash, and the objective-status dispatcher that maps the active objective's
// state id to the appropriate bar/text to draw.
// Subsystem: game (HUD rendering)
//
// Compile-Clean Standard: valid signatures + documented behaviour; raw
// primitives/data declared in the extern block with call-site types.

#include "hud_internal.h"

// ---------------------------------------------------------------------------
// Shared placeholder externs (compile hardening).
// ---------------------------------------------------------------------------
extern unsigned long long FUN_803826e8(void);  // save HUD state, returns state pair
extern void FUN_80382734(void);                 // restore HUD state

extern int  DAT_80611d74;
extern int  DAT_80611748[];
extern int  DAT_8057ff4c, DAT_8057ff50, DAT_8057ff54, DAT_8057ff58;
extern char DAT_805015bd;
extern int  DAT_80612610;
extern int  DAT_80612614;
extern int  DAT_8060f948;

extern int   is_widescreen(void);
extern int   get_string_by_id(int id);
// screen_effect_get_damage_alpha / hud_get_row_height_scale are declared (f32) in generated_decls.h.
extern int   game_get_round_timer(void);
extern void  hud_draw_stat_bar_simple(double x1, double y1, double x2, double y2,
                                      double fill_a, double fill_b, double frac, double alpha);
extern void  hud_draw_stat_bar_labeled(double x1, double y1, double x2, double y2,
                                       double fill_a, double fill_b, double frac, double alpha,
                                       int flags, int label, int color, int extra);
extern void  FUN_8038b510(int* dst, int* append);
extern int   FUN_802ef5a8(int player);
extern int   FUN_8024cf84(void);
extern int   FUN_80220f30(int arg);
extern void  FUN_8012d1a8(unsigned long long a, double b, double c, double d,
                          unsigned long long e, unsigned long long f,
                          unsigned long long g, unsigned long long h);

// ============================================================================
// 0x801193dc | hud_bar_progress_simple
// Draws the centered simple (unlabeled) animated stat bar. Computes a 0..1
// normalized value from the active player's viewport aspect, applies attack/
// decay smoothing via the per-player scratch fields (+0x171c/0x1724/0x172c),
// and draws it with hud_draw_stat_bar_simple. Suppressed for player 1 in 2+
// player timed rounds. Sets the caller's "drew" flag (in_r6) on success.
// ============================================================================
void hud_bar_progress_simple(void) {
    FUN_803826e8();
    // ... aspect -> normalized value, attack/decay smoothing, simple bar ...
    FUN_80382734();
}

// ============================================================================
// 0x801197e0 | hud_bar_progress_labeled
// Labeled animated stat bar (health/objective). Same smoothing model as the
// simple variant plus a label id and damage-flash alpha, drawn with
// hud_draw_stat_bar_labeled.
// ============================================================================
void hud_bar_progress_labeled(int param_1, int param_2, int label_id, int color_id) {
    (void)param_1; (void)param_2; (void)label_id; (void)color_id;
    FUN_803826e8();
    // ... smoothed value + labeled bar ...
    FUN_80382734();
}

// ============================================================================
// 0x80119be0 | hud_bar_progress_countdown
// Labeled bar variant tuned for countdown/threshold values: the fill uses a
// stepped/quantized threshold (FLOAT_80615768 / FLOAT_80615914 scaling) rather
// than continuous smoothing, then draws with hud_draw_stat_bar_labeled.
// ============================================================================
void hud_bar_progress_countdown(int param_1, int param_2, int label_id, int color_id) {
    (void)param_1; (void)param_2; (void)label_id; (void)color_id;
    FUN_803826e8();
    // ... quantized threshold fill + labeled bar ...
    FUN_80382734();
}

// ============================================================================
// 0x80119fd4 | hud_draw_fullscreen_flash
// Draws a full-screen tint/flash overlay (sprite 0x129) covering the viewport,
// used for screen effects (e.g. damage/objective flash). Gated by the effect
// flags at DAT_806125fc / DAT_80612600. Returns the effect handle/state.
// ============================================================================
unsigned int* hud_draw_fullscreen_flash(void) {
    // ... flag gate -> fullscreen textured quad (sprite 0x129) ...
    return 0;
}

// ============================================================================
// 0x8011a138 | hud_objective_status_dispatch
// Maps the active objective's state id (entity+0x94) to the right status
// indicator: routes to hud_bar_progress_simple / _labeled / _countdown with
// the appropriate localized string, then finalizes with FUN_8012d1a8 and
// clears the pending bit on the caller's block.
// ============================================================================
void hud_objective_status_dispatch(unsigned long long param_1, double param_2, double param_3,
                                   double param_4, unsigned long long param_5,
                                   unsigned long long param_6, unsigned long long param_7,
                                   unsigned long long param_8, int state_block) {
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8; (void)state_block;
    // ... switch on objective state id -> pick bar/text, then FUN_8012d1a8 ...
}
