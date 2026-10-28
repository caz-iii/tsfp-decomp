// Address: 0x8010f4d0 - 0x801108b0 | Batch 333 | Date: 2026-09-04
// HUD element layout dispatcher and pip/counter icon drawer.
// hud_row_dispatch_elements iterates the two HUD element rows for a player,
// dispatching each element to the appropriate draw routine by type. The pip
// drawer (hud_draw_pip_icon) renders the tiled "grenade/ammo pip" style
// elements (element types 10-18) with a backing plate, fill overlay, optional
// low-ammo warning arc, and a count digit.
// Subsystem: game (HUD rendering)

#include "common.h"
#include "hud.h"
#include "sprite.h"
#include "text.h"
#include "gx_draw.h"

// ============================================================================
// External data (still-raw globals; documented by offset)
// ============================================================================
// DAT_80611d74      : active HUD context; +4 = active player slot index
// DAT_80610ba0      : HUD element instance table base (0x29c bytes per player)
// DAT_8042e244      : per-element sub-item table (0xe0 stride, 0x20 per sub)
// DAT_8042e23c      : element icon sprite id (0xe0 stride)
// DAT_8040d130      : pip backing sprite id (+0=sprite, +4=flags), 8-byte stride
// DAT_80578680/8684 : per-frame pip counters / font scale table (0x177c stride)
// DAT_80611748      : viewport rect [0]=x0 [1]=x1 ... [6]=y1
// FLOAT_806108xx    : screen scale factors (x/y projection)

// ============================================================================
// 0x8010f4d0 | hud_draw_pip_icon
// Draws a single "pip" style HUD element (grenade/ammo/objective counter).
// Renders: backing quad, animated fill overlay, optional low-count warning
// glow (element subtype 0x11), a count digit, and an optional icon sprite.
// param_2 = base alpha, param_5 = current value, param_6 = max value,
// param_7 = pip index, param_8 = pip capacity, param_10/11 = source entities.
// ============================================================================
void hud_draw_pip_icon(double x_base, double alpha, int elem_index, int sub_ptr,
                       unsigned int value, unsigned int max_value, unsigned int pip_index,
                       int pip_capacity, int use_shadow, int src_entity_a, int src_entity_b) {
    // See Ghidra listing at 0x8010f4d0 for full body (textured-quad heavy).
    // Behaviour summary:
    //   - Saves GX blend/cull state, selects the HUD digit font table.
    //   - Computes aspect-corrected screen X/Y scale and the pip's anchor.
    //   - Determines a "warning" tint step from pip_capacity vs pip_index.
    //   - If no explicit backing sprite (DAT_8040d130 == -1): draws the default
    //     pip quad (sprite 0xdb) split into up to 4 sub-quads, with a special
    //     pulsing warning overlay when the element subtype == 0x11.
    //   - Otherwise draws a value-scaled meter fill (subtype-dependent scale
    //     from param_10/11), clamped to [0,1], as a partial vertical fill.
    //   - Optionally draws the count digit via text_draw_char_shadowed.
    //   - Draws the trailing frame sprite (sprite 0xdb) and, if present, the
    //     element icon (DAT_8042e23c).
    //   - Restores the saved GX state and font table.
    (void)x_base; (void)alpha; (void)elem_index; (void)sub_ptr; (void)value; (void)max_value;
    (void)pip_index; (void)pip_capacity; (void)use_shadow; (void)src_entity_a; (void)src_entity_b;
}

// ============================================================================
// 0x801104d4 | hud_row_dispatch_elements
// Iterates the two HUD element rows (top/bottom) for the active player and
// dispatches each element definition to its draw routine by type:
//   1  -> hud_element_draw_text        (formatted numeric value)
//   2  -> hud_element_draw_sprite      (state sprite)
//   3  -> hud_element_draw_meter       (progress bar)
//   4  -> hud_element_draw_tiled_counter
//   5  -> hud_element_draw_text        (alternate font)
//   6/8-> hud_element_draw_sprite_with_icon (scaled)
//   9  -> hud_element_draw_sprite_with_icon (entity-driven)
//   10-18 -> hud_draw_pip_icon         (pip / tiled counter)
// Advances the horizontal cursor per element and mirrors layout when the
// player is on the right-hand side of a split screen (mirror flag derived
// from countLeadingZeros(1 - player_layout)).
// ============================================================================
void hud_row_dispatch_elements(void) {
    // See Ghidra listing at 0x801104d4 for the full dispatch loop.
    // Structure:
    //   handle = FUN_803826b4();               // fetch per-player HUD state
    //   if (active_player < 0) return;
    //   compute mirror flag, font table (FUN_801c1714 -> split-screen count),
    //   base X (viewport edge), base Y (viewport bottom minus row height).
    //   for (row = 0; row < 2; ++row) {
    //       resolve element list id for this row;
    //       for (sub = 0; sub < 4; ++sub) {
    //           read element def; skip if type == 0;
    //           advance cursor by measured width;
    //           switch (type) { ... draw dispatch ... }
    //           advance cursor again;
    //       }
    //       if (mirror) adjust Y by alt row height;
    //   }
    //   hud_hit_marker_render_all();
    //   FUN_80382700();                         // restore per-player HUD state
}
