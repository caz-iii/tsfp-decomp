// Address: 0x801160d4 - 0x801172a0 | Batch 335 | Date: 2026-09-04
// HUD control-hint overlays.
// These draw the context-sensitive button-prompt overlays shown for special
// player states/modes: vehicle, mounted turret, spectator, mapmaker, and the
// objective pickup prompt, plus the objective title bar. Each resolves the
// bound controller glyph for an action (input_get_button_name), composes it
// with a localized label (get_string_by_id + FUN_8038b510), and draws the
// pair with text_draw_at_int_coords / text_draw_char_shadowed at a position
// derived from the viewport rect (DAT_80611748) and panel insets.
// Subsystem: game (HUD rendering)
//
// Compile-Clean Standard: valid signatures + documented behaviour; the raw
// text/render primitives and data symbols are declared in the extern block
// below with their call-site arity/return types.

#include "hud_internal.h"

// ---------------------------------------------------------------------------
// Shared placeholder externs (compile hardening).
// ---------------------------------------------------------------------------
extern void FUN_803826f0(void);   // save HUD state
extern void FUN_803826dc(void);   // save HUD state (variant)
extern void FUN_8038273c(void);   // restore HUD state
extern void FUN_80382728(void);   // restore HUD state (variant)

extern int  DAT_80611d74;         // active HUD context base
extern int  DAT_80611748[];       // viewport rect
extern int  DAT_8057ff4c, DAT_8057ff50, DAT_8057ff54, DAT_8057ff58; // panel insets
extern int  DAT_80610838;         // default HUD font table id
extern int  DAT_805015bd;         // current game mode byte
extern int  DAT_80612610;         // active player/slot count
extern int  DAT_80612614;         // mapmaker step/state
extern char DAT_80610840[], DAT_80610844[]; // composed-glyph scratch templates

extern int  get_string_by_id(int id);
extern void input_get_button_name(int button_mask, int* out_glyph);
extern void text_set_font_table(int table);
extern void text_draw_at_int_coords(unsigned int* x, unsigned int* y, unsigned char* glyph, int color);
extern void text_draw_char_shadowed(double x, double y, unsigned char* glyph, int color, int shadow);
extern int  text_get_char_width(unsigned char* glyph);
extern void FUN_8038b510(int* dst_glyph, int* append);
extern void hud_letterbox_bars(void);
extern void hud_radar_button_hints(void);
extern int  FUN_801c1714(void);   // split-screen player count
extern int  FUN_802448f8(void);   // resolve objective/carrier entity
extern int  FUN_8020750c(void);   // spectator context refresh
extern int  game_is_mapmaker_multiplayer(void);
extern int* DAT_80610a20;
extern void FUN_8016f00c(int mode);
extern void FUN_8016e77c(int sprite);
extern void FUN_80170410(int x, int y, int w, int h, int u, int v, int tw, int th);
extern int  DAT_80611f90;         // blink accumulator
extern int  DAT_806108e4;         // frame delta
extern int  DAT_806109c0;         // blink period
extern int  DAT_805015c0;         // mode flags

// ============================================================================
// 0x801160d4 | hud_hints_vehicle
// Vehicle-mode control hints: three button+label rows (steer/boost/exit) via
// input_get_button_name + get_string_by_id, plus an extra hint row when the
// vehicle step counter (DAT_80612614) reaches its terminal value 0x16.
// ============================================================================
void hud_hints_vehicle(void) {
    hud_letterbox_bars();
    // ... 3-4 button+label rows (strings 0x15ac..0x15b6) ...
}

// ============================================================================
// 0x801164ec | hud_hints_turret
// Mounted-turret control hints: aim / fire / exit rows (strings 0x15d6..0x15d8),
// each drawn as a button glyph followed by its shadowed label.
// ============================================================================
void hud_hints_turret(void) {
    // ... 3 button+label rows ...
}

// ============================================================================
// 0x801167e0 | hud_hints_spectator
// Single-line spectator control hint (string 0x15d9): refreshes the spectator
// context, draws the letterbox bars, then the one button+label row.
// ============================================================================
void hud_hints_spectator(void) {
    FUN_8020750c();
    hud_letterbox_bars();
    // ... one button+label row ...
}

// ============================================================================
// 0x80116924 | hud_hints_mapmaker
// Mapmaker control hints: several button+label rows (place/rotate/scale, etc.),
// laid out along the bottom with a horizontal step that widens in 2+ player
// mapmaker mode (DAT_805015bd == '\n' && DAT_80612610 > 1).
// ============================================================================
void hud_hints_mapmaker(void) {
    FUN_803826f0();
    // ... multi-row button hints with split-screen-aware spacing ...
    FUN_8038273c();
}

// ============================================================================
// 0x80116e40 | hud_hints_objective_pickup
// Objective/pickup prompt: draws a blinking pickup icon (sprite 0x104, blink
// via DAT_80611f90) with the "pick up" / "drop" hints, selecting strings by
// single-player vs mapmaker-multiplayer and by carrier state.
// ============================================================================
void hud_hints_objective_pickup(void) {
    FUN_803826dc();
    // ... blinking icon + pickup/drop button hints ...
    FUN_80382728();
}

// ============================================================================
// 0x8011726c | hud_objective_title_bar
// Objective title bar: draws the letterbox bars (or a solid strip when the
// objective is active), selects the title font by split-screen count, draws
// the objective title (get_string_by_id from the carrier), then the context
// button hints via hud_radar_button_hints.
// ============================================================================
void hud_objective_title_bar(void) {
    // ... letterbox/strip + title text + hud_radar_button_hints() ...
    hud_radar_button_hints();
}
