// Address: 0x8010c06c - 0x8010df94 | Batch 331 | Date: 2026-08-25
// HUD weapon bars, stat bars, lives icons, score/time panel, hit markers
// Subsystem: game (HUD rendering)

#include "common.h"
#include "hud.h"
#include "sprite.h"
#include "text.h"
#include "gx_draw.h"

// ============================================================================
// Forward declarations
// ============================================================================

void hud_draw_reload_indicator(void);
void hud_draw_lives_icons(void);
void hud_draw_weapon_panel(void);
void hud_draw_bar_needle(f32 left, f32 top, f32 right, f32 bottom,
                         f32 fill_pct, f32 time, f32 amplitude, f32 alpha,
                         s32 player_idx, s32 unused, s32 no_fade);
void hud_draw_stat_bar_simple(u8* label, f32 top, f32 right, f32 bottom, f32 fill_pct,
                              f32 time, f32 amplitude, f32 alpha);
void hud_draw_stat_bar_labeled(u8* label, f32 top, f32 right, f32 bottom,
                               f32 fill_pct, f32 time, f32 amplitude, f32 alpha,
                               s32 player_idx, s32 unused, s32 icon_sprite_id, s32 flip_dir);
void hud_draw_score_time_panel(void);
void hud_draw_panel_9slice(f32 left, f32 top, f32 right, f32 bottom, f32 alpha);
void hud_hit_marker_record(s32 param1, s32 param2, u32 tex_width,
                           f32 size_x, f32 size_y, f32 fade_speed, f32 tex_id);
void hud_hit_marker_render_all(void);

// ============================================================================
// 0x8010c06c | hud_draw_reload_indicator
// Draws the weapon reload/charge indicator sprite (0x1e2) below the ammo
// display. Only renders when weapon state matches reload conditions.
// Scales position by player count (2-player = smaller).
// ============================================================================
void hud_draw_reload_indicator(void) {
    s32* cursor_y = (s32*)gx_push_ortho_state();
    s32 hud_state = g_hud_context;
    s32 weapon_id = *(s32*)(g_hud_context + 0x14);

    gx_set_alpha((f32)(FLOAT_80615750 * /* extraout_f1 */));

    // Only draw when not in state 0x10, reload flag set, and weapon matches
    if (*(s32*)(hud_state + 0x114) != 0x10 && g_reload_flag != 0 &&
        (weapon_id == g_weapon_type_a || weapon_id == g_weapon_type_b)) {

        s32 tex_id = sprite_get_texture_id(0x1e2);
        s32 tex_entry = *g_sprite_table + tex_id * 0x58;
        u32 tex_w = *(u16*)(tex_entry + 0x4e);
        u32 tex_h = *(u16*)(tex_entry + 0x52);

        s32 player_count = hud_get_player_count();
        u32 icon_size = 0x28;
        if (player_count > 2) {
            icon_size = 0x1e;
        }

        // Compute screen position from viewport + scale
        s32 x_pos = (s32)(FLOAT_80615734 * FLOAT_806157b4 * FLOAT_8061082c +
                    (f32)(g_viewport_offset_x + *g_hud_params - DOUBLE_BIAS));
        s32 y_scaled = (s32)(FLOAT_80610830 *
                       (f32)((f32)icon_size * ((f32)tex_h / (f32)tex_w)));

        gx_bind_texture(tex_id);
        gx_draw_sprite_rect(x_pos - (s32)(icon_size >> 1), *cursor_y,
                            x_pos + (s32)(icon_size >> 1), *cursor_y + y_scaled,
                            0, 0, tex_w << 4, tex_h << 4);

        *cursor_y = y_scaled + (4 >> (g_hud_params[0xd] & 0x3f)) + *cursor_y;
    }

    gx_pop_ortho_state();
}

// ============================================================================
// 0x8010c2dc | hud_draw_lives_icons
// Draws lives remaining as repeated icons (sprite 0x102) in a horizontal row.
// Renders up to 5 life icons when lives bits (field 0xa5c & 0x1e) are set
// on the player character data.
// ============================================================================
void hud_draw_lives_icons(void) {
    s32* cursor_y = (s32*)gx_push_ortho_partial();
    u32 scaled_w = 0;
    s32 char_data = *(s32*)(*(s32*)(g_hud_context + 0x14) + 0x7c);
    u32 scaled_h = 0;
    u32 tex_w = 0;
    u32 tex_h = 0;
    s32 y_pos = *cursor_y;
    s32 x_pos = g_viewport_offset_x + *g_hud_params;

    if ((*(u32*)(char_data + 0xa5c) & 0x1e) != 0) {
        s32 tex_id = sprite_get_texture_id(0x102);
        s32 tex_entry = *g_sprite_table + tex_id * 0x58;
        tex_w = *(u16*)(tex_entry + 0x4e);
        tex_h = *(u16*)(tex_entry + 0x52);

        scaled_w = (u32)((f32)tex_w * FLOAT_8061082c);
        scaled_h = (u32)((f32)tex_h * FLOAT_80610830);

        gx_bind_texture(tex_id);

        s32 player_count = hud_get_player_count();
        u32 icon_slot_size = 0x30;
        if (player_count == 1) {
            icon_slot_size = 0x38;
        }
        u32 slot_scaled = (u32)(FLOAT_8061082c * (f32)icon_slot_size);

        // Center horizontally
        x_pos = x_pos + ((s32)slot_scaled / 2) - ((s32)scaled_w / 2);
    }

    gx_set_alpha((f32)(FLOAT_80615750 * /* dVar12 */));

    // Draw up to 5 life icons based on bit flags
    for (s32 i = 0; i < 5; i++) {
        if ((*(u32*)(char_data + 0xa5c) & (2 << i)) != 0) {
            gx_draw_sprite_rect(x_pos, y_pos,
                                x_pos + (s32)(scaled_w / 2),
                                y_pos + (s32)(scaled_h / 2),
                                0, 0, tex_w << 4, tex_h << 4);
            x_pos += 0x1a;
        }
    }

    *cursor_y = scaled_h + (4 >> (g_hud_params[0xd] & 0x3f)) + *cursor_y;
    gx_pop_ortho_partial();
}

// ============================================================================
// 0x8010c4f8 | hud_draw_weapon_panel
// Composite weapon panel: weapon icon, ammo icons, reload indicator, and
// lives display. Lives only drawn in game mode 100 (standard deathmatch).
// ============================================================================
void hud_draw_weapon_panel(void) {
    hud_draw_weapon_icon();
    hud_draw_ammo_icons();
    hud_draw_reload_indicator();

    if (g_game_mode == 100) {
        hud_draw_lives_icons();
    }
}

// ============================================================================
// 0x8010c55c | hud_draw_bar_needle
// Draws the animated needle/gauge of a health or armor bar using sprites
// 0xd9 (bar body segments) and 0xda (needle tip with sine oscillation).
// Supports directional rendering (left-to-right / right-to-left) and
// smooth fade color transitions.
//
// params:
//   left/top/right/bottom - bar bounding box
//   fill_pct - current fill (0.0 to 1.0)
//   time - animation time for sine wave
//   amplitude - sine wave amplitude
//   alpha - overall opacity
//   player_idx - player index for color lookup
//   no_fade - if nonzero, skip fade color computation
// ============================================================================
void hud_draw_bar_needle(f32 left, f32 top, f32 right, f32 bottom,
                         f32 fill_pct, f32 time, f32 amplitude, f32 alpha,
                         s32 player_idx, s32 unused, s32 no_fade) {
    s32 state = gx_push_ortho_state();
    f32 scale_x = FLOAT_8061082c;
    f32 scale_y = FLOAT_80610830;

    // Load bar body texture (sprite 0xd9)
    s32 tex_id = sprite_get_texture_id(0xd9);
    f32 hud_scale = FLOAT_80615734;
    s32 tex_entry = *g_sprite_table + tex_id * 0x58;
    u32 tex_w = *(u16*)(tex_entry + 0x4e);
    u32 tex_h = *(u16*)(tex_entry + 0x52);

    // Compute bar geometry from bounding box
    f32 bar_right = hud_scale * (left + bottom);
    f32 bar_width = -(hud_scale * (right - top) - hud_scale * (top + right));
    f32 bar_left_edge = bar_width + (right - top);
    f32 needle_x = FLOAT_806157d4 * scale_x + bar_width;
    f32 segment_w = scale_x * (f32)tex_w;
    f32 needle_limit = -(FLOAT_8061581c * scale_x - bar_left_edge);
    f32 bar_right_edge = bar_width + segment_w;
    f32 bar_left_final = bar_left_edge - segment_w;
    f32 half_tex_h = scale_y * (f32)(tex_h / 2);
    f32 y_top = bar_right - half_tex_h;
    f32 y_bot = bar_right + half_tex_h;

    // Set alpha based on player state
    if (state == 0) {
        if (no_fade == 0) {
            u32 alpha_val = gx_set_alpha(FLOAT_806157bc * alpha);
            hud_compute_fade_color(alpha_val);
        } else {
            gx_set_alpha(FLOAT_806157bc * alpha);
        }
    } else {
        gx_set_alpha(FLOAT_806157bc * alpha);
    }

    // Draw 3 body segments
    gx_bind_texture(tex_id);
    u32 tw = tex_w << 4;
    u32 th = tex_h << 4;
    gx_draw_quad_uv(bar_width, y_top, bar_right_edge, y_bot, 0, 0, tw, th);
    gx_draw_quad_uv(bar_left_edge, y_top, bar_left_final, y_bot, 0, 0, tw, th);
    gx_draw_quad_uv(bar_right_edge, y_top, bar_left_final, y_bot, tw, 0, tw, th);

    // Load needle tip texture (sprite 0xda)
    s32 tip_tex = sprite_get_texture_id(0xda);
    f32 range = needle_limit - needle_x;
    s32 tip_entry = *g_sprite_table + tip_tex * 0x58;
    f32 fill_pos = amplitude * range + needle_x;
    u32 tip_h = *(u16*)(tip_entry + 0x52);
    f32 tip_end = fill_pct * range + fill_pos;
    u32 tip_w = *(u16*)(tip_entry + 0x4e);

    f32 tip_half_h = scale_y * (f32)(tip_h / 2);
    f32 tip_y_top = bar_right - tip_half_h;
    f32 tip_y_bot = bar_right + tip_half_h;

    gx_bind_texture(tip_tex);

    // Draw needle with optional sine wave animation
    if (FLOAT_80615730 <= time) {
        f32 abs_fill = ABS(ABS(fill_pct) - time);

        // Compute sine-modulated alpha for pulsing effect
        u32 sine_alpha = (u32)(alpha * (FLOAT_80615820 *
                         (hud_scale * g_sine_table[(s32)(FLOAT_806157c0 *
                          (FLOAT_806157c4 * (FLOAT_80615748 * amplitude - hud_scale)) +
                          hud_scale) & 0x7ff] + hud_scale) + FLOAT_806157bc));

        u32 clamped = MIN(sine_alpha, 0x7f);
        clamped = MAX(clamped, 0);

        if (state == 0 && no_fade == 0) {
            hud_compute_fade_color(clamped);
        }

        s32 tw2 = tip_w << 4;
        gx_draw_quad_uv(fill_pos, tip_y_top, tip_end, tip_y_bot, tw2, 0, tw2, tip_h << 4);

        // Draw mirrored tip for bidirectional fill
        f32 mirror_pos = range * (amplitude + fill_pct) + needle_x;
        if (fill_pct < FLOAT_80615730) {
            abs_fill = -abs_fill;
        }
        gx_draw_quad_uv(mirror_pos, tip_y_top,
                        range * abs_fill + mirror_pos, tip_y_bot,
                        tw2, 0, tw2, tip_h << 4);
    } else {
        s32 tw2 = tip_w << 4;
        gx_draw_quad_uv(fill_pos, tip_y_top, tip_end, tip_y_bot, tw2, 0, tw2, tip_h << 4);
    }

    gx_pop_ortho_state();
}

// ============================================================================
// 0x8010ca9c | hud_draw_stat_bar_simple
// Draws a simple stat bar (health/armor) with border frame (sprite 0xdb),
// needle gauge via hud_draw_bar_needle, text label, and screen border effect.
// ============================================================================
void hud_draw_stat_bar_simple(u8* label, f32 top, f32 right, f32 bottom,
                              f32 fill_pct, f32 time, f32 amplitude, f32 alpha) {
    u8* text_ptr = (u8*)gx_push_ortho_state_with_text();
    f32 scale_x = FLOAT_8061082c;
    f32 scale_y = FLOAT_80610830;

    // Load border texture (sprite 0xdb)
    s32 tex_id = sprite_get_texture_id(0xdb);
    s32 tex_entry = *g_sprite_table + tex_id * 0x58;
    u32 tex_h = *(u16*)(tex_entry + 0x52);
    f32 margin = FLOAT_80615768;
    u32 tex_w = *(u16*)(tex_entry + 0x4e);

    f32 y_top = top - margin * scale_y;
    f32 y_bot = bottom + margin * scale_y;
    f32 border_h = scale_y * ((f32)tex_h - margin);
    f32 border_w = scale_x * (f32)(tex_w * 2);

    f32 inner_top = top + border_h;
    f32 inner_bot = bottom - border_h;
    f32 x_left = left + border_w;
    f32 x_right = right - border_w;

    u32 fade_alpha = gx_set_alpha(FLOAT_806157bc * alpha);
    hud_compute_fade_color(fade_alpha);

    // Draw screen border effect
    hud_render_screen_border(left, top, right, bottom, alpha);

    // Draw 4 corner/edge border pieces
    gx_bind_texture(tex_id);
    s32 tw = tex_w << 5;
    u32 th = tex_h << 4;
    gx_draw_quad_uv(left, y_top, x_left, inner_top, tw, 0, 0, th);
    gx_draw_quad_uv(right, y_top, x_right, inner_top, tw, 0, 0, th);
    gx_draw_quad_uv(left, y_bot, x_left, inner_bot, tw, 0, 0, th);
    gx_draw_quad_uv(right, y_bot, x_right, inner_bot, tw, 0, 0, th);

    // Draw needle gauge (no fade, no flip)
    hud_draw_bar_needle(left, top, right, bottom, fill_pct, time, amplitude,
                        FLOAT_80615730, 0, 0, 0);

    // Draw text label if present
    if (text_ptr != NULL) {
        s32 prev_font = text_set_font_table(2);
        f32 text_h = text_get_char_height(text_ptr);
        f32 text_y = inner_top - text_h;
        text_apply_alpha(x_right - x_left, text_ptr, 0);
        f32 text_w = text_get_char_width(text_ptr);
        f32 text_x = FLOAT_80615734 * (left + right) - text_w;

        u32 shadow_alpha = gx_set_alpha(FLOAT_806157b4 * alpha);
        u32 text_alpha = gx_set_alpha(FLOAT_806157f0 * alpha);

        text_draw_char_shadowed(text_x, text_y, text_ptr,
                                (text_alpha & 0xff) | 0x7f7f7f00,
                                shadow_alpha & 0xff);

        f32 white = FLOAT_80615718;
        text_set_color_rgb(white, white, white);
        text_set_font_table(prev_font);
    }

    gx_pop_ortho_state_with_text();
}

// ============================================================================
// 0x8010ce40 | hud_draw_stat_bar_labeled
// Full stat bar with text label, optional icon sprite, directional flip
// (left/right side), border, needle gauge, and viewport-clipped scrolling
// text. Main health/armor bar widget for in-game HUD.
// ============================================================================
void hud_draw_stat_bar_labeled(u8* label, f32 top, f32 right, f32 bottom,
                               f32 fill_pct, f32 time, f32 amplitude, f32 alpha,
                               s32 player_idx, s32 unused, s32 icon_sprite_id, s32 flip_dir) {
    u8* text_ptr = (u8*)gx_push_ortho_full();
    s32 state = (s32)((u64)gx_push_ortho_full() >> 32);
    f32 scale_x = FLOAT_8061082c;
    f32 scale_y = FLOAT_80610830;

    s32 prev_font = text_set_font_table(1);

    // Load border texture (sprite 0xdb)
    s32 tex_id = sprite_get_texture_id(0xdb);
    s32 tex_entry = *g_sprite_table + tex_id * 0x58;
    f32 margin = FLOAT_80615768 * scale_y;
    u32 tex_h = *(u16*)(tex_entry + 0x52);
    u32 tex_w = *(u16*)(tex_entry + 0x4e);

    f32 y_top_margin = top - margin;
    f32 y_bot_margin = bottom + margin;
    f32 border_h = scale_y * ((f32)tex_h - FLOAT_80615768);
    f32 border_w = scale_x * (f32)(tex_w * 2);
    f32 inner_top = top + border_h;
    f32 inner_bot = bottom - border_h;
    f32 x_left = left + border_w;
    f32 x_right = right - border_w;

    f32 bar_left = left;
    f32 bar_right = right;

    if (state == 0) {
        gx_set_alpha(FLOAT_806157bc * alpha);
    }

    // Clamp text within viewport if label is provided
    if (text_ptr != NULL) {
        f32 text_w = text_get_char_width(text_ptr);
        f32 overflow = FLOAT_80615734 * (text_w - (x_right - bar_left) -
                       FLOAT_8061576c * scale_x);

        if (overflow > FLOAT_80615730) {
            // Viewport clamping logic for scrolling text
            // ... (clamps bar extents to keep text visible)
        }

        // Vertical offset for attached HUD elements
        f32 y_offset = (f32)(g_viewport_y_offset + g_hud_params[5]) - (inner_top - FLOAT_80611674);
        if (y_offset > FLOAT_80615730) {
            top += y_offset;
            bottom += y_offset;
            y_top_margin += y_offset;
            inner_top += y_offset;
            inner_bot += y_offset;
            y_bot_margin += y_offset;
        }
    }

    // Draw screen border
    hud_render_screen_border(bar_left, top, bar_right, bottom, alpha);

    gx_bind_texture(tex_id);

    // Draw border + needle based on direction
    if (flip_dir == 0) {
        // Left-to-right layout
        gx_draw_quad_uv(bar_left, y_top_margin, x_left, inner_top, tex_w << 5, 0, 0, tex_h << 4);
        gx_draw_quad_uv(bar_left, y_bot_margin, x_left, inner_bot, tex_w << 5, 0, 0, tex_h << 4);
        hud_draw_bar_needle(-(FLOAT_80615768 * scale_x - x_left), top, bar_right, bottom,
                            fill_pct, time, amplitude, FLOAT_80615730, state, 0, 1);
    } else {
        // Right-to-left layout (mirrored)
        gx_draw_quad_uv(bar_right, y_top_margin, x_right, inner_top, tex_w << 5, 0, 0, tex_h << 4);
        gx_draw_quad_uv(bar_right, y_bot_margin, x_right, inner_bot, tex_w << 5, 0, 0, tex_h << 4);
        hud_draw_bar_needle(bar_left, top, FLOAT_80615768 * scale_x + x_right, bottom,
                            -fill_pct, time, amplitude, FLOAT_80615718, state, 0, 1);
    }

    // Draw optional icon sprite
    if (icon_sprite_id != -1) {
        s32 icon_tex = sprite_get_texture_id(icon_sprite_id);
        s32 icon_entry = *g_sprite_table + icon_tex * 0x58;
        u32 icon_w = *(u16*)(icon_entry + 0x4e);
        u32 icon_h = *(u16*)(icon_entry + 0x52);

        f32 icon_x;
        if (flip_dir == 0) {
            icon_x = scale_x * (f32)icon_w + bar_left;
        } else {
            icon_x = -(scale_x * (f32)icon_w - bar_right);
        }

        gx_bind_texture(icon_tex);
        f32 center_y = FLOAT_80615734 * (top + bottom);
        f32 half_h = scale_y * (f32)(icon_h / 2);
        gx_draw_quad_uv(bar_left, center_y - half_h, icon_x, center_y + half_h,
                        0, 0, icon_w << 4, icon_h << 4);
    }

    // Draw text label
    if (text_ptr != NULL) {
        f32 label_x;
        if (flip_dir == 0) {
            label_x = FLOAT_8061576c * scale_x + x_left;
        } else {
            f32 tw = text_get_char_width(text_ptr);
            label_x = -(FLOAT_8061576c * scale_x - (x_right - tw));
        }
        f32 label_y = text_get_char_height(text_ptr);
        label_y = inner_top - label_y;

        u32 shadow_a = gx_set_alpha(FLOAT_806157b4 * alpha);
        u32 text_a = gx_set_alpha(FLOAT_806157f0 * alpha);

        text_draw_char_shadowed(label_x, label_y, text_ptr,
                                (text_a & 0xff) | 0x7f7f7f00, shadow_a & 0xff);
    }

    f32 white = FLOAT_80615718;
    text_set_color_rgb(white, white, white);
    text_set_font_table(prev_font);
    gx_pop_ortho_full();
}

// ============================================================================
// 0x8010d53c | hud_draw_score_time_panel
// Draws the score/time display panel (sprite 0xe0 background) showing up to
// 3 ranked players with formatted time strings (MM:SS.T) and player rank
// numbers. Used for in-game scoreboard overlay.
// ============================================================================
void hud_draw_score_time_panel(void) {
    gx_push_ortho_state();

    s32 score_data = hud_get_score_data((s32)local_d8);
    if (score_data == 0) {
        gx_pop_ortho_state();
        return;
    }

    s32 prev_font = text_set_font_table(g_hud_font_id);

    // Compute panel position from viewport
    f32 scale_y = FLOAT_80610830;
    f32 row_height = FLOAT_806157f4 * scale_y;
    f32 panel_top = FLOAT_806157cc * scale_y + (f32)(g_viewport_y_offset + g_hud_params[5]);
    f32 scale_x = FLOAT_8061082c;
    f32 panel_bottom = (f32)(g_hud_params[1] - g_viewport_y_end);

    // Load background texture (sprite 0xe0)
    s32 tex_id = sprite_get_texture_id(0xe0);
    s32 tex_entry = *g_sprite_table + tex_id * 0x58;
    u32 tex_h = *(u16*)(tex_entry + 0x52);
    u32 tex_w = *(u16*)(tex_entry + 0x4e);

    f32 bg_h = scale_y * (f32)tex_h;
    f32 row_end = panel_top + bg_h;
    f32 panel_right = -(scale_x * (f32)tex_w - panel_bottom);

    // Compute row layout
    f32 filled_end = row_end + (FLOAT_80615780 * row_height - FLOAT_80615748 * row_height);

    hud_compute_fade_color(0x30);
    gx_bind_texture(tex_id);

    u32 tw = tex_w << 4;
    u32 th = tex_h << 4;

    // Draw 3-segment background (top, middle, bottom)
    gx_draw_quad_uv(panel_right, panel_top, panel_bottom, row_end, 0, 0, tw, th);
    gx_draw_quad_uv(panel_right, row_end, panel_bottom, filled_end, 0, th, tw, th);
    gx_draw_quad_uv(panel_right, filled_end, panel_bottom, filled_end + bg_h, 0, th, tw, 0);

    // Draw each player row (up to 3)
    f32 text_margin_x = FLOAT_806157f4 * scale_x;
    f32 rank_offset = FLOAT_80615760 * scale_x;
    f32 half_scale = FLOAT_80615734;
    f32 text_left = panel_right + text_margin_x;
    f32 text_right = panel_bottom - text_margin_x;

    for (u32 i = 0; i < 3; i++) {
        f32 row_y = row_height * (f32)i + row_end - row_height;
        u32 time_val = local_d8[i];

        if (time_val == 0) {
            // Draw "--" placeholder
            format_string(text_margin_x, filled_end, panel_bottom, /* ... */,
                          &g_dash_string, /* ... */);
        } else {
            // Format as MM:SS.T
            u32 total_seconds = time_val / g_ticks_per_second;
            u32 tenths = (u32)(FLOAT_806157d4 * (f32)time_val / (f32)g_ticks_per_second);
            u32 digit = tenths % 10;
            format_string(/* ... */, "%02d:%02d.%01d",
                          (total_seconds / 60) % 60, total_seconds % 60, digit);
        }

        // Draw rank label and score text
        text_draw_shadowed_at(text_left, row_y, /* formatted */, 0x7f7f7f60, 0x20);

        leaf_92c0(0);
        s32 rank_str = get_string_by_id(0x19bd);
        u8* rank_text = format_string(/* ... */, rank_str, /* ... */);
        f32 rank_w = text_get_char_width(rank_text);
        text_draw_shadowed_at(text_right - rank_w - rank_offset, row_y,
                              rank_text, 0x7f7f7f60, 0x20);

        // Draw player number
        u8* num_text = format_string(/* ... */, &g_format_d, i + 1);
        f32 num_w = text_get_char_width(num_text);
        text_draw_shadowed_at(-(half_scale * num_w - text_right), row_y,
                              num_text, 0x7f7f7f60, 0x20);
    }

    text_set_font_table(prev_font);
    gx_pop_ortho_state();
}

// ============================================================================
// 0x8010d950 | hud_draw_panel_9slice
// Draws a decorative 9-slice panel: double-layer with sprite 0xdb as border
// corners/edges and sprite 0xf8 as fill. First draws fill (outline layer),
// then draws border overlay. Used as background for popup/tooltip panels.
// ============================================================================
void hud_draw_panel_9slice(f32 left, f32 top, f32 right, f32 bottom, f32 alpha) {
    f32 x_start = (f32)gx_push_ortho_state_f();
    f32 border_scale_x = FLOAT_80615824 * FLOAT_8061082c;
    f32 border_scale_y = FLOAT_80615824 * FLOAT_80610830;

    // Load border texture (sprite 0xdb)
    s32 tex_id = sprite_get_texture_id(0xdb);
    s32 tex_entry = *g_sprite_table + tex_id * 0x58;
    f32 margin = FLOAT_80615768;
    u32 tex_w = *(u16*)(tex_entry + 0x4e);
    u32 tex_h = *(u16*)(tex_entry + 0x52);

    f32 y_top = top - margin * border_scale_y;
    f32 y_bot = bottom + margin * border_scale_y;
    f32 corner_w = border_scale_x * (f32)tex_w;
    f32 x_left = x_start + corner_w;
    f32 x_right = right - corner_w;
    f32 corner_h = border_scale_y * ((f32)tex_h - margin);
    f32 inner_top = top + corner_h;
    f32 inner_bot = bottom - corner_h;

    // Set blend mode and alpha
    gx_set_alpha(FLOAT_8061575c * alpha);
    gx_set_alpha(FLOAT_806157f0 * alpha);

    // Draw fill layer (sprite 0xf8)
    s32 fill_tex = sprite_get_texture_id(0xf8);
    gx_bind_texture(fill_tex);

    u32 tw = tex_w << 4;
    u32 th = tex_h << 4;

    // 9 quads for fill: corners (4) + edges (4) + center (1)
    gx_draw_quad_uv(x_start, y_top, x_left, inner_top, 0, 0, tw, th);
    gx_draw_quad_uv(right, y_top, x_right, inner_top, 0, 0, tw, th);
    gx_draw_quad_uv(x_start, y_bot, x_left, inner_bot, 0, 0, tw, th);
    gx_draw_quad_uv(right, y_bot, x_right, inner_bot, 0, 0, tw, th);
    gx_draw_quad_uv(x_left, y_top, x_right, inner_top, tw, 0, tw, th);
    gx_draw_quad_uv(x_left, y_bot, x_right, inner_bot, tw, 0, tw, th);
    gx_draw_quad_uv(x_start, inner_top, x_left, inner_bot, 0, th, tw, th);
    gx_draw_quad_uv(right, inner_top, x_right, inner_bot, 0, th, tw, th);
    gx_draw_quad_uv(x_left, inner_top, x_right, inner_bot, tw, th, tw, th);

    // Draw border overlay (sprite 0xdb)
    gx_bind_texture(tex_id);
    gx_draw_quad_uv(x_start, y_top, x_left, inner_top, 0, 0, tw, th);
    gx_draw_quad_uv(right, y_top, x_right, inner_top, 0, 0, tw, th);
    gx_draw_quad_uv(x_start, y_bot, x_left, inner_bot, 0, 0, tw, th);
    gx_draw_quad_uv(right, y_bot, x_right, inner_bot, 0, 0, tw, th);
    gx_draw_quad_uv(x_left, y_top, x_right, inner_top, tw, 0, tw, th);
    gx_draw_quad_uv(x_left, y_bot, x_right, inner_bot, tw, 0, tw, th);
    gx_draw_quad_uv(x_start, inner_top, x_left, inner_bot, 0, th, tw, th);
    gx_draw_quad_uv(right, inner_top, x_right, inner_bot, 0, th, tw, th);

    gx_pop_ortho_state_f();
}

// ============================================================================
// 0x8010de8c | hud_hit_marker_record
// Records a hit/damage marker into an 8-slot ring buffer. Each slot is 0x28
// bytes storing: fade timers, screen position, size, texture ID, and fade
// speed. Called when player deals damage to record the on-screen indicator.
// ============================================================================
void hud_hit_marker_record(s32 param1, s32 param2, u32 tex_width,
                           f32 size_x, f32 size_y, f32 fade_speed, f32 tex_id) {
    u64 result = gx_push_ortho_state_with_text();
    u32 screen_x = (u32)(result >> 32);

    if (*(s32*)(g_hud_context + 4) != 0) {
        // Advance ring buffer index (mod 8)
        s32 prev = g_hit_marker_write_idx + 1;
        s32 sign = prev >> 31;
        g_hit_marker_write_idx = (sign * 8 | (u32)(prev * 0x20000000 + sign) >> 29) - sign;

        // Get slot pointer
        f32* slot = (f32*)(&g_hit_marker_buffer +
                    g_hit_marker_write_idx * 0x28 +
                    *(s32*)(g_hud_context + 4) * 0x140);

        f32 elapsed = (f32)(screen_x - (g_viewport_offset_x + *g_hud_params)) - DOUBLE_BIAS;

        // Store marker data
        slot[4] = (f32)(screen_x - (g_viewport_offset_x + *g_hud_params));
        slot[5] = (f32)((s32)result - (g_viewport_y_offset + g_hud_params[5]));
        slot[6] = size_x;
        slot[7] = size_y;
        slot[2] = FLOAT_80615828 * (elapsed - (f32)tex_width);
        slot[3] = FLOAT_80615760;
        slot[8] = fade_speed;
        slot[9] = tex_id;
        slot[1] = /* initial_fade */;
        slot[0] = /* initial_fade */;
    }

    gx_pop_ortho_state_with_text();
}

// ============================================================================
// 0x8010df94 | hud_hit_marker_render_all
// Renders all active hit markers from the 8-slot ring buffer. Each marker
// fades out over time with position interpolation, size decay, and alpha
// blending. Uses additive blend mode (mode 2) for the glow effect.
// ============================================================================
void hud_hit_marker_render_all(void) {
    gx_push_ortho_full();

    if (*(s32*)(g_hud_context + 4) < 0) {
        gx_pop_ortho_full();
        return;
    }

    for (s32 i = 0; i < 8; i++) {
        f32* slot = (f32*)(&g_hit_marker_buffer + i * 0x28 +
                    *(s32*)(g_hud_context + 4) * 0x140);

        if (FLOAT_80615730 < slot[0]) {
            // Compute fade progress (0.0 = fresh, 1.0 = gone)
            f32 t = (slot[1] - slot[0]) / slot[1];

            // Interpolate position
            s32 x_center = (s32)slot[4] + g_viewport_offset_x + *g_hud_params +
                           (s32)(slot[2] * t);
            s32 y_center = (s32)slot[5] + g_viewport_y_offset + g_hud_params[5] +
                           (s32)(slot[3] * t);

            // Compute decaying size
            s32 half_w = (s32)(FLOAT_8061082c *
                         (f32)((s32)slot[6] + (s32)(FLOAT_80615760 * t)) / 2);
            s32 half_h = (s32)(FLOAT_80610830 *
                         (f32)((s32)slot[7] + (s32)(FLOAT_80615760 * t)) / 2);

            // Set additive blend mode
            gx_set_blend_mode(2);
            gx_bind_texture((s32)slot[8]);

            // Compute fading alpha
            gx_set_alpha(FLOAT_8061582c * (FLOAT_80615718 - t));

            // Draw the marker quad
            u32 tex_h = *(u16*)(*g_sprite_table + (s32)slot[8] * 0x58 + 0x52);
            gx_draw_sprite_rect(x_center - half_w, y_center - half_h,
                                x_center + half_w, y_center + half_h,
                                0, 0, (s32)slot[9] << 4, tex_h << 4);

            // Decay the timer
            slot[0] = -(FLOAT_806108ec * (FLOAT_80615718 / (f32)g_ticks_per_second) - slot[0]);
        }
    }

    gx_pop_ortho_full();
}
