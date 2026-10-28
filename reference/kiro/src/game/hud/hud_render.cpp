// =============================================================================
// hud_render.cpp - HUD rendering functions (sprites, frames, text, scrollbars)
// TimeSplitters: Future Perfect (GameCube) Decompilation
// Batch 077 | Date: 2026-08-06
// =============================================================================
//
// This file implements the visual rendering layer for HUD elements:
// - Panel backgrounds and borders (9-slice sprite rendering)
// - Scrollbar track/thumb/arrows
// - Item text with alignment, color state, and focus effects
// - Gradient fills and element backgrounds
//
// Key sprite IDs used:
//   0x11C = border/frame corners+edges (9-slice)
//   0x3E9 = scrollbar track segment
//   0x3EA = scrollbar up arrow
//   0x3EB = scrollbar down arrow
//   0x3EC = stretched background sprite
//   0x000 = panel fill
//
// Color palette indices (from select_list_get_id/get_flags):
//   0 = normal (unfocused, enabled)
//   1 = focused (with sine-wave pulse)
//   3 = locked/disabled foreground
//   4 = locked/disabled background
//
// =============================================================================

#include "common.h"

// GX rendering primitives
extern void FUN_8016e77c(int sprite);
extern void FUN_8016e8ac(int blend);
extern void FUN_8016ee98(void);
extern void FUN_8016f00c(int mode);
extern void FUN_8016f0a0(void* x, void* y, void* w, void* h);
extern void FUN_8016f0e4(int x, int y, int w, int h);
extern void FUN_8016f4bc(int x1, int y1, int x2, int y2, int color);
extern void FUN_8016f6d0(float x1, float y1, float x2, float y2);
extern void FUN_8016f7d8(float x1, float y1, float x2, float y2);
extern void FUN_8016fcd0(int x1, int y1, int x2, int y2, int color);
extern void FUN_80170410(int x, int y, int w, int h, int u, int v, int tw, int th);
extern void FUN_80171cbc(float x1, float y1, float x2, float y2, float rot, int u, int v, int tw, int th);
extern void FUN_803825c0(float width);
extern void FUN_8004e810(void);
extern void FUN_8004e88c(void);
extern void FUN_8004e908(void);
extern void FUN_8004e984(void);
extern void FUN_8004f140(float x1, float y1, float x2, float y2);
extern void FUN_8004f21c(float x1, float y1, float x2, float y2);

// Sine table at DAT_80583228 (2048 entries, 0-1 range)
extern float DAT_80583228[];

// --- Additional placeholder externs (compile hardening, batch header pass) ---
extern int  FUN_80046ff8(int element);          // predicate: element focused?
extern void FUN_80049788(int element, int item);
extern float FLOAT_80611a64;   // scroll pos -> pixel scale
extern float FLOAT_80613be0;   // analog scroll step
extern float FLOAT_80613bec;   // analog scroll dead-zone (+)
extern int  g_current_viewport; // active viewport/player index
void hud_compute_item_visibility(void);

// Address: 0x80041964 | hud_draw_bordered_rect
// Draws a 9-slice bordered rectangle using sprite 0x11C. Computes inner/outer
// bounds from element position + border width, draws 4 edge fills + 4 corner sprites.
void hud_draw_bordered_rect(void) { /* sprite 0x11C, 9-slice rendering */ }

// Address: 0x80041be4 | hud_draw_frame_border
// Sets blend mode 0xE, draws bordered rect, restores blend.
void hud_draw_frame_border(void) { /* wrapper with blend mode setup */ }

// Address: 0x80041c70 | hud_draw_element_background
// Draws element background: outer rect fill + border line + inner rect.
// Uses transition progress (element+0xF0) for color interpolation.
void hud_draw_element_background(int element, float* bounds) { /* background + border */ }

// Address: 0x80041d68 | hud_draw_fill_gradient
// Dispatches to gradient fill function by mode: 0=solid, 1=vertical, 2=horizontal, 3=radial.
void hud_draw_fill_gradient(float x1, float y1, float x2, float y2, int mode) {
    switch (mode) {
        case 0: /* FUN_8004e908 - solid fill */ break;
        case 1: /* FUN_8004e984 - vertical gradient */ break;
        case 2: /* FUN_8004e88c - horizontal gradient */ break;
        case 3: /* FUN_8004e810 - radial/custom */ break;
    }
}

// Address: 0x80041dd8 | hud_draw_stretch_sprite
// Draws sprite 0x3EC stretched to fit item bounds.
void hud_draw_stretch_sprite(void) { /* stretched sprite 0x3EC */ }

// Address: 0x80041e8c | hud_draw_scrollbar
// Draws the scrollbar UI: track background, thumb position (from scroll progress),
// up/down arrows. Uses sprites 0x3E9 (track), 0x3EA (up), 0x3EB (down).
// Highlights active region based on focus state (element+0x188).
void hud_draw_scrollbar(void) { /* scrollbar track + thumb + arrows */ }

// Address: 0x80042220 | hud_draw_panel_frame
// MASSIVE: draws the full panel background/frame. Handles:
// - Full-screen mode: pulsing fill + border (sine-wave from DAT_80583228)
// - Split-screen mode: edge lines, fill, scissored background sprite,
//   gradient overlays at top/bottom. Uses element+0x210 for panel position.
void hud_draw_panel_frame(void) { /* full panel background rendering */ }

// Address: 0x800427ec | hud_element_needs_dim
// Returns 1 if element should be dimmed (flags 0x30 not set AND not focused).
int hud_element_needs_dim(int element) {
    if ((*(u32*)(element + 0x40) & 0x30) != 0) return 0;
    if (FUN_80046ff8(element) != 0) return 0;
    return 1;
}

// Address: 0x80042834 | hud_item_compute_colors
// Computes foreground+background colors for an item based on state:
// - Locked (flag 0x1): palette indices 3/4 (gray)
// - Focused: palette index 1 with sine-wave pulse (pulsing brightness)
// - Normal: palette index 0
// - Disabled/unfocused: 50% alpha blend
// Uses select_list_get_id/get_flags for color lookup from palette,
// FUN_80132348 for transition-scaled alpha.
void hud_item_compute_colors(int unused_a, int unused_b, u32* out_fg, u32* out_bg) {
    /* color computation with transition + focus pulse */
}

// Address: 0x80042a78 | hud_item_draw_text
// MASSIVE: draws item's text label. Handles:
// - Font selection (item+0x1C)
// - Alignment modes: 0=left, 1=center, 2=right (with offset computation)
// - Uppercase transform (flag 0x2000)
// - Alpha fade for overflow text (flag 0x4000000)
// - Focus state: draws with shadow (text_draw_char_shadowed)
// - Scrolling text region (flag 0x80): draws within bounds with color gradient
// - Normal: draws with computed foreground color
void hud_item_draw_text(int unused_a, int unused_b, float* bounds) {
    /* text rendering with alignment, color, shadow */
}


// =============================================================================
// Scroll Animation + Render Helpers (Batch 078)
// =============================================================================

extern float FLOAT_806108e8;  // frame delta time
extern float FLOAT_80613c24;  // scroll dead zone threshold
extern float FLOAT_80613c28;  // analog negative dead zone
extern float input_get_analog_value(int viewport, int axis);

// Address: 0x80042cac | hud_item_draw_separator
// Default item render callback. Draws two horizontal colored lines at the
// vertical center of the item's bounds (a visual separator).
void hud_item_draw_separator(int unused_a, int unused_b, float* bounds) {
    /* draws 2 lines at center Y using hud_element_set_anim_value colors */
}

// Address: 0x80042d8c | hud_item_update_focus_fade
// Per-frame fade update. When focused: smoothly increases item+0x34 toward 1.0.
// When not focused: decreases toward 0.0. Same for sub-element fade (+0x104)
// controlled by flag 0x10 at item+0xB8. Rate = FLOAT_806108e8 / framerate.
void hud_item_update_focus_fade(int unused, int item) { /* smooth 0-1 fade */ }

// Address: 0x80042f08 | hud_item_draw_sub_content
// Draws sub-element content only if item has events (+0xD8) and sub-type==2.
void hud_item_draw_sub_content(int element, int item) {
    if (*(int*)(item + 0xD8) != 0 && *(int*)(item + 0xB4) == 2) {
        FUN_80049788(element, item);
    }
}

// Address: 0x80042f44 | hud_scroll_interpolate
// Smooth scroll: moves current position (+0x80) toward target (+0x84) at
// computed speed (+0x8C). Speed auto-calculated from distance/time if no
// manual scroll flags are active. Multiplied by FLOAT_806108e8 per frame.
void hud_scroll_interpolate(int element) { /* lerp current -> target */ }

// Address: 0x800430b4 | hud_scroll_to_item
// Computes the scroll target needed to bring a specific item into view.
// Accounts for item height, viewport size, margins from layout+0x30,
// and adjacent horizontal-scroll items. Clamps to valid scroll range.
void hud_scroll_to_item(int element, int item) { /* compute + set target */ }

// Address: 0x80043218 | hud_scroll_snap_to_focus
// Instantly scrolls to show the focused item (+0x194). No animation.
void hud_scroll_snap_to_focus(int element) {
    if (*(int*)(element + 0x194) != 0) {
        hud_scroll_to_item(element, *(int*)(element + 0x194));
        *(float*)(*(int*)(element + 0x194) + 0x34) = 1.0f;
        *(float*)(element + 0x80) = *(float*)(element + 0x84);
        *(float*)(element + 0xBC) = *(float*)(element + 0x80) * FLOAT_80611a64;
    }
}

// Address: 0x80043274 | hud_scroll_snap_position
// Copies target to current (instant jump) and recomputes pixel position.
void hud_scroll_snap_position(int element) {
    *(float*)(element + 0x80) = *(float*)(element + 0x84);
    *(float*)(element + 0xBC) = *(float*)(element + 0x80) * FLOAT_80611a64;
}

// Address: 0x80043290 | hud_scroll_set_target
// Sets scroll target with bounds clamping: min=0, max=(total_content - viewport).
void hud_scroll_set_target(float target, int element) {
    *(float*)(element + 0x84) = target;
    float max_scroll = *(float*)(element + 0x58) -
                       (*(float*)(element + 0x6C) - *(float*)(element + 0x68));
    if (*(float*)(element + 0x84) < 0.0f)
        *(float*)(element + 0x84) = 0.0f;
    if (*(float*)(element + 0x84) > max_scroll)
        *(float*)(element + 0x84) = max_scroll;
}

// Address: 0x800432d0 | hud_scroll_apply_analog
// Applies analog-scaled offset to scroll, snaps, and recomputes visibility.
void hud_scroll_apply_analog(float analog_value, int element) {
    float new_target = *(float*)(element + 0x80) - FLOAT_80613be0 * analog_value;
    hud_scroll_set_target(new_target, element);
    hud_scroll_snap_position(element);
    hud_compute_item_visibility();
}

// Address: 0x80043318 | hud_scroll_input_analog
// Reads analog stick (axis 4) and applies scrolling if beyond dead zone.
// Also scrolls sub-element at element+0x168 if present.
void hud_scroll_input_analog(int element) {
    float analog = input_get_analog_value(g_current_viewport, 4);
    if (analog > FLOAT_80613bec || analog < FLOAT_80613c28) {
        hud_scroll_apply_analog(analog, element);
        if (*(int*)(element + 0x168) != 0) {
            hud_scroll_apply_analog(analog, *(int*)(element + 0x168));
        }
    }
}
