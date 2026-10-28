// Address: 0x80112358 - 0x80112d10 | Batch 333 | Date: 2026-09-04
// HUD objective / mission timeline overlay.
// hud_objective_timeline_render draws the framed timeline widget shown in
// certain modes (split-screen count < 3): a bordered box with corner ticks, a
// horizontal axis of labelled markers, evenly-spaced graduation lines, and
// left/right/top/bottom edge labels drawn via the special glyph builder
// (FUN_8012e878 + text_draw_char). hud_timeline_select_font is a small helper
// that switches to the timeline font table when a valid target chain exists.
// Subsystem: game (HUD rendering)

#include "common.h"
#include "hud.h"
#include "text.h"
#include "gx_draw.h"

// ============================================================================
// External data (still-raw globals; documented by offset)
// ============================================================================
// DAT_80611748      : viewport rect [0]=x0 [1]=x1 [2]=cx [3]=w [5]=y0 [6]=y1
//                     [7]=baseline [8]=h
// DAT_80611d74      : active HUD context; +0x100/0x104 = scroll offsets
// DAT_8057ff4c..58  : cached inset deltas written for later element layout
// DAT_80610864/84   : glyph/format spec pointers for FUN_8012e878
// FLOAT_80615xxx    : box scale + tick spacing constants

// ============================================================================
// 0x80112358 | hud_timeline_select_font
// If the active player's entity has a valid linked target chain
// (entity->0x7c->0xc14->0x7c all non-null), selects font table 0x12 for the
// timeline labels. No-op otherwise.
// ============================================================================
void hud_timeline_select_font(void) {
    // ent = active->entity->0x7c;
    // if (ent && ent->0xc14 && ent->0xc14->0x7c) text_set_font_table(0x12);
}

// ============================================================================
// 0x801123ac | hud_objective_timeline_render
// Draws the objective timeline overlay (only when split-screen count < 3).
// Steps:
//   - Compute the box rect from the viewport with FLOAT_80615814/84 insets.
//   - Fill the interior (FUN_8016f4bc) and stroke the border + corner ticks
//     (FUN_8016fee0) in the standard 0x1e321e64 frame colour.
//   - Record the inset deltas into DAT_8057ff4c..58 for dependent elements.
//   - Call hud_timeline_select_font, then walk 26 axis markers: for each in
//     range draw a graduation line and its numeric label (glyph via
//     FUN_8012e878 + text_draw_char), alternating tick length every 3rd.
//   - Draw the axis baseline, the top/left/right/bottom edge labels, and a
//     second set of 40 fine graduations along the value axis.
// ============================================================================
void hud_objective_timeline_render(void) {
    // FUN_803826c0();                           // save HUD state
    // if (split_count < 3) { ... full render as above ... }
    // FUN_8038270c();                           // restore HUD state
}
