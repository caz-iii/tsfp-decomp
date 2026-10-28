// ============================================================================
// HUD Layout & Screen Border System
// Handles HUD positioning, screen border rendering, fade colors, and
// visibility checks for split-screen / widescreen layouts.
// Batch 324 | Date: 2026-08-25
// ============================================================================

#include "common.h"
#include "graphics.h"
#include "sprite.h"
#include "text.h"
#include "input.h"

// ---- Globals ----
static int* g_player_context;              // DAT_80611d74
static int* g_player_context_2;            // DAT_80611d78
static int  g_level_id;                    // DAT_80612614
static int  g_player_count_mode;           // DAT_80612610
static u8   g_game_sub_mode;               // DAT_805015bd
static int  g_viewport_data[8];            // DAT_80611748
static int  g_viewport_offset_y;           // DAT_8057ff58
static int  g_viewport_offset_x;           // DAT_8057ff50

static float g_font_height;               // FLOAT_80611674
static float g_render_scale_y;            // FLOAT_80610830
static float g_render_scale_x;            // FLOAT_8061082c

// Per-screen-mode font table pointers (indexed by get_screen_mode())
static int g_font_table_a[5];             // DAT_8040cf34
static int g_font_table_b[5];             // DAT_8040cf20
static int g_font_table_c[5];             // DAT_8040cf48
static int g_font_table_d[5];             // DAT_8040cf0c

// Constants
static const float LINE_SPACING = 1.5f;   // FLOAT_8061576c (approximate)
static const float BORDER_ALPHA = 0.5f;   // FLOAT_80615764 (approximate)

// ============================================================================
// Address: 0x801044a0 | hud_format_button_prompt
// Formats a button prompt: "[ButtonName]  [Label]" into dest_buf.
// Raw ABI (0x801044a0) is 16-slot: acc threads the caller's running
// accumulator/handle; d1..d5/g6/g7 are the register slots; button_id selects
// the pad glyph; dest_buf receives the formatted string; the trailing args feed
// sprintf_render. Returns the accumulator so callers can chain (matches the
// hint-panel formatter in hud_hint_screens.cpp).
// ============================================================================
unsigned long long hud_format_button_prompt(
        unsigned long long acc, double d1, double d2, double d3, double d4, double d5,
        unsigned long long g6, unsigned long long g7,
        int button_id, int flag, int dest_buf, void* a, void* b, void* c,
        unsigned int d, unsigned int e) {
    int button_name[16];  // 64-byte glyph-name scratch
    (void)acc; (void)d1; (void)d2; (void)d3; (void)d4; (void)d5; (void)g6; (void)g7;
    (void)flag; (void)d; (void)e;

    extern int DAT_8061083c;  // empty-string sentinel (label placeholder)

    input_get_button_name(button_id, button_name);
    sprintf_render(dest_buf, "%s  %s", button_name, &DAT_8061083c,  // s__s__s_80610848
                   a, b, c);
    return acc;
}

// ============================================================================
// Address: 0x801044fc | hud_should_show_timer
// Returns whether the round timer HUD element should be displayed.
// Checks: round timer active, player context valid, game mode (challenge=0x0A
// with >1 player has special logic based on character death state).
// ============================================================================
int hud_should_show_timer(void) {
    int timer = game_get_round_timer();

    if (timer == 0 || g_player_context == NULL || g_player_context[1] < 0) {
        return 0;
    }

    // Challenge mode (0x0A) with multiple players: check death state
    if (g_game_sub_mode == 0x0A && g_player_count_mode > 1) {
        int is_dead = 0;
        if (g_player_context_2 != NULL) {
            int* character = (int*)g_player_context_2[0x509];  // offset 0x1424
            if (character != NULL && (character[0x87] & 1) != 0) {  // offset 0x21C, flag bit 0
                is_dead = 1;
            }
        }

        if (g_player_context[1] == 0) {
            // Player 0: show only if NOT dead (CLZ trick: !is_dead)
            return (countLeadingZeros((u32)is_dead) >> 5);
        } else {
            // Other players: show if dead
            return (is_dead != 0) ? 1 : 0;
        }
    }

    return 1;
}

// ============================================================================
// Address: 0x801045bc | hud_is_special_layout
// Returns 1 if the current level uses a special HUD layout mode.
// Specific level IDs that trigger this: 0x35, 0x44, 0x4D, 0x50,
// or 0x64 with level_def->type == 0x40.
// ============================================================================
int hud_is_special_layout(void) {
    if (g_level_id == 0x4D || g_level_id == 0x44 ||
        g_level_id == 0x35 || g_level_id == 0x50) {
        return 1;
    }

    if (g_level_id == 100) {  // 0x64
        int* level_def = level_get_current_def();
        if (level_def != NULL && level_def[4] == 0x40) {
            return 1;
        }
    }

    return 0;
}

// ============================================================================
// Address: 0x80104644 | hud_compute_fade_channel
// Computes a single color channel with fade applied.
// During special layout + mid-animation (state 5..6), applies scaled dimming.
// Otherwise returns input alpha | 0x7F7F7F00 (grey with original alpha).
// ============================================================================
u32 hud_compute_fade_channel(u32 input_alpha) {
    // Check if we're in the animating state (5 < state < 7)
    int is_animating = false;
    if (g_player_context != NULL && g_player_context[1] >= 0) {
        int player_idx = g_player_context[1];
        int state = g_player_state_table[player_idx * 0x5DF];
        if (state > 4 && state < 7) {
            is_animating = true;
        }
    }

    int special = hud_is_special_layout();

    if (special == 0 || is_animating) {
        // Normal: use input alpha as-is, with grey RGB
        return (input_alpha & 0xFF) | 0x7F7F7F00;
    } else {
        // Special layout: compute scaled fade value
        float val = FADE_SCALE * (float)((s32)input_alpha ^ 0x80000000);  // int-to-float trick
        if (val > FADE_MAX) {
            val = FADE_MAX;
        }
        return (u32)(int)val & 0xFF;
    }
}

// ============================================================================
// Address: 0x80104724 | hud_compute_fade_color
// Packs 4 fade channels (RGBA) into a single u32 color.
// Each channel goes through hud_compute_fade_channel independently.
// Returns packed ABGR color.
// ============================================================================
u32 hud_compute_fade_color(u32 input_color) {
    u32 ch0 = hud_compute_fade_channel(input_color);
    u32 ch1 = hud_compute_fade_channel(input_color);
    u32 ch2 = hud_compute_fade_channel(input_color);
    u32 ch3 = hud_compute_fade_channel(input_color);

    // Pack: A in high byte, then B, G, R
    return (ch3 << 24) | ((ch2 & 0xFF00) << 8) | (ch1 >> 24) | ((ch0 >> 8) & 0xFF00);
}

// ============================================================================
// Address: 0x8010478c | hud_render_screen_border
// MASSIVE screen border renderer. Draws a decorative frame around the viewport
// using a 9-patch sprite (texture ID 0xEF). Supports:
// - Inset border with configurable thickness
// - Multi-layer rendering (layer 0xE or 1 depending on flags)
// - Viewport-clipped inner fill pass with alpha blending
// - Aspect-correct edge scaling via g_render_scale_x
// ============================================================================
void hud_render_screen_border(float left, float top, float right, float bottom, float alpha) {
    u32 flags = gx_begin_border_pass();  // FUN_803826c4
    if (flags == 0) {
        return;
    }

    gx_set_alpha((float)(BORDER_OPACITY * alpha));  // FUN_803825c0

    int tex_id = sprite_get_texture_id(0xEF);

    float width  = right - left;
    float height = bottom - top;

    // Get texture dimensions (shifted << 4 for sub-pixel precision)
    int* tex_data = g_texture_table + tex_id * 0x58;
    u32 tex_w = (u32)tex_data[0x4E/4] << 4;  // ushort at +0x4E
    u32 tex_h = (u32)tex_data[0x52/4] << 4;  // ushort at +0x52

    // Compute inset (minimum of width/height, clamped)
    float min_dim = (width < height) ? width : height;
    float inset = INSET_SCALE * min_dim;
    if (inset > MAX_INSET) {
        inset = (width < height) ? width : height;
        inset = INSET_SCALE * inset;  // re-clamp
    }

    // Compute inner rect
    float inner_top    = top + inset;
    float inner_bottom = bottom - inset;
    float inner_left   = left - (BORDER_OFFSET * g_render_scale_x);
    float inner_right  = right + (BORDER_OFFSET * g_render_scale_x);
    float inset_left   = inner_left + inset;
    float inset_right  = inner_right - inset;

    // Set render layer if multi-pass
    int prev_layer = 0;
    if ((flags & 2) != 0) {
        prev_layer = set_render_layer((flags & 1) ? 1 : 0xE);
        clear_render_state();  // FUN_8016ee98
    }

    // Bind border texture
    bind_sprite_texture(tex_id);  // FUN_8016e77c

    // Draw 9 patches (corners + edges + center)
    // Top-left corner
    draw_sprite_quad((int)inner_left, (int)top, (int)inset_left, (int)inner_top,
                     0, 0, tex_w, tex_h);
    // Top-right corner
    draw_sprite_quad((int)inner_right, (int)top, (int)inset_right, (int)inner_top,
                     0, 0, tex_w, tex_h);
    // Bottom-left corner
    draw_sprite_quad((int)inner_left, (int)bottom, (int)inset_left, (int)inner_bottom,
                     0, 0, tex_w, tex_h);
    // Bottom-right corner
    draw_sprite_quad((int)inner_right, (int)bottom, (int)inset_right, (int)inner_bottom,
                     0, 0, tex_w, tex_h);
    // Top edge
    draw_sprite_quad((int)inset_left, (int)top, (int)inset_right, (int)inner_top,
                     tex_w, 0, tex_w, tex_h);
    // Bottom edge
    draw_sprite_quad((int)inset_left, (int)bottom, (int)inset_right, (int)inner_bottom,
                     tex_w, 0, tex_w, tex_h);
    // Left edge
    draw_sprite_quad((int)inner_left, (int)inner_top, (int)inset_left, (int)inner_bottom,
                     0, tex_h, tex_w, tex_h);
    // Right edge
    draw_sprite_quad((int)inner_right, (int)inner_top, (int)inset_right, (int)inner_bottom,
                     0, tex_h, tex_w, tex_h);
    // Center fill
    draw_sprite_quad((int)inset_left, (int)inner_top, (int)inset_right, (int)inner_bottom,
                     tex_w, tex_h, tex_w, tex_h);

    // Inner pass with viewport clipping (if multi-layer)
    if ((flags & 2) != 0) {
        // Clamp coordinates to viewport bounds
        float clamp_left   = clamp_to_viewport_x(left);
        float clamp_right  = clamp_to_viewport_x(right);
        float clamp_top    = clamp_to_viewport_y(top);
        float clamp_bottom = clamp_to_viewport_y(bottom);

        set_render_layer(6);
        clear_render_state();
        int prev_blend = set_blend_mode(1);       // FUN_8016e000
        set_blend_alpha(BORDER_ALPHA);            // FUN_8016e010
        bind_sprite_texture(-6);                  // solid color
        set_blend_mode(prev_blend);
        set_blend_alpha(0.0f);

        if ((int)clamp_left < (int)clamp_right &&
            (int)clamp_top < (int)clamp_bottom) {
            gx_set_alpha((float)(MAX_INSET * alpha));
            draw_sprite_stretched((int)clamp_left, (int)clamp_top,
                                  (int)clamp_right, (int)clamp_bottom,
                                  (int)clamp_left << 4, (int)clamp_top << 4,
                                  (int)clamp_right << 4, (int)clamp_bottom << 4,
                                  1, 3);  // FUN_801748e4
        }

        set_render_layer(prev_layer);
    }

    gx_end_border_pass();  // FUN_80382710
}

// ============================================================================
// Address: 0x80104ee8 | hud_get_top_y
// Computes the Y coordinate for the top of the HUD area.
// Accounts for viewport offset, font height, timer visibility, and
// an optional vertical shift from FUN_8010f4a0.
// ============================================================================
int hud_get_top_y(void) {
    int screen_mode = get_screen_mode();
    int prev_font = text_set_font_table(g_font_table_a[screen_mode]);

    // Base Y from viewport top edge
    int viewport_top = g_viewport_data[6] - g_viewport_offset_y;  // [0x18] offset
    u32 base_y = (u32)((float)viewport_top - g_font_height);

    // If timer should NOT be shown, apply additional vertical offset
    if (g_game_sub_mode != 0x0A || g_player_count_mode < 2 || !hud_should_show_timer()) {
        float shift = get_hud_vertical_shift();  // FUN_8010f4a0
        base_y = (u32)(-(LINE_SPACING * g_render_scale_y - ((float)(s32)base_y - shift)));
    }

    text_set_font_table(prev_font);
    return base_y;
}

// ============================================================================
// Address: 0x80104ff0 | hud_get_row_y
// Computes Y position for a HUD row (param_1 = row index from top).
// Uses font table height and render scale for line spacing.
// ============================================================================
int hud_get_row_y(u32 row_index) {
    int screen_mode = get_screen_mode();
    int prev_font = text_set_font_table(g_font_table_b[screen_mode]);

    u32 top_y = hud_get_top_y();

    // Offset down from top: base - (row_index * font_height) - line_spacing
    float row_offset = (float)(s32)(top_y - (int)(LINE_SPACING * g_render_scale_y));
    float scaled_row = g_font_height * (float)(s32)row_index;

    text_set_font_table(prev_font);
    return (int)(-(scaled_row - row_offset));
}

// ============================================================================
// Address: 0x801050b0 | hud_get_bottom_row_y
// Computes Y position for bottom-anchored HUD elements.
// Split-screen aware: mode >= 2 uses hud_get_top_y directly,
// mode < 2 uses hud_get_row_y(1) as base.
// ============================================================================
int hud_get_bottom_row_y(u32 row_index) {
    int screen_mode = get_screen_mode();
    int prev_font = text_set_font_table(g_font_table_c[screen_mode]);

    float result;
    int mode = get_screen_mode();

    if (mode < 2) {
        // Single-screen: offset from row 1
        int row1_y = hud_get_row_y(1);
        float base = (float)(s32)(row1_y - (int)(LINE_SPACING * g_render_scale_y));
        result = g_font_height * (float)(s32)row_index - base;
    } else {
        // Split-screen: offset from top_y directly
        u32 top_y = hud_get_top_y();
        float base = (float)(s32)(top_y - (int)(LINE_SPACING * g_render_scale_y));
        result = g_font_height * (float)(s32)row_index - base;
    }

    text_set_font_table(prev_font);
    return (int)(-result);
}

// ============================================================================
// Address: 0x801051e8 | hud_get_center_y
// Computes a vertically-centered Y position for HUD elements.
// Clamped to viewport bounds. Uses screen-mode-specific font table
// and line count (0x22 default, 0x28 for mode 1).
// ============================================================================
int hud_get_center_y(void) {
    int screen_mode = get_screen_mode();
    int prev_font = text_set_font_table(g_font_table_d[screen_mode]);

    // Get base from bottom row with 3 lines
    int base_y = hud_get_bottom_row_y(3);
    int adjusted = (int)((float)(s32)(base_y - (int)(LINE_SPACING * g_render_scale_y))
                         - g_font_height);

    // Line count depends on screen mode
    u32 line_count = 0x22;  // 34 lines default
    if (get_screen_mode() == 1) {
        line_count = 0x28;  // 40 lines for mode 1
    }

    // Viewport vertical range
    int vp_top = g_viewport_offset_x + g_viewport_data[5];  // [0x14]
    int vp_height = g_viewport_data[6] - g_viewport_offset_y;  // [0x18]

    // Clamp: if base_y would exceed viewport center, recompute
    int threshold = vp_top + (int)(g_render_scale_y * (float)(s32)line_count)
                    + (int)(LINE_SPACING * g_render_scale_y);
    if (adjusted < threshold) {
        // Center between viewport top and bottom
        int midpoint = vp_top + ((vp_height - vp_top) >> 1);
        adjusted = (int)(-(g_font_height * HALF_LINE -
                           (float)(s32)midpoint));
    }

    text_set_font_table(prev_font);
    return adjusted;
}
