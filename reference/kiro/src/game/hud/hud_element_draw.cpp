// Address: 0x8010e1e0 - 0x8010f4a0 | Batch 332 | Date: 2026-08-25
// HUD element rendering system: type-based draw dispatchers for text, sprites,
// meters, tiled counters, and layout utilities.
// Subsystem: game (HUD rendering)

#include "common.h"
#include "hud.h"
#include "sprite.h"
#include "text.h"
#include "gx_draw.h"

// ============================================================================
// HUD Element Definition Table
// Located at DAT_8042e23c, 0xe0 bytes per entry.
// Fields: [0] type, [1] sprite_id_normal, [2] sprite_id_alt, [3] color,
//         [4] alt_color, [5] scale_y, [7] tile_dims_ptr, [0x14] scale_custom
// ============================================================================

// Element types:
//   0, 10-18: empty/unused
//   1: text with formatted value
//   2, 6, 8, 9: sprite
//   3: sprite (scaled variant)
//   4: text or sprite (adaptive by player count)
//   5: text (alternate font measurement)

// ============================================================================
// 0x8010e1e0 | hud_element_measure_digits
// Measures the pixel width of the digit character set for numeric HUD layout.
// ============================================================================
void hud_element_measure_digits(void) {
    gx_push_full();
    text_get_char_width(&g_hud_digit_chars);
    gx_pop_full();
}

// ============================================================================
// 0x8010e27c | hud_element_preload_resources
// Preloads resources for a HUD element by type (switch on 19 types).
// Called during HUD init to cache textures and compute layout dimensions.
//
// Types 1/4: load text via FUN_801a7818 + measure digits
// Types 2/6/8/9: load sprite texture
// Type 3: load text only
// Type 5: measure alternate digit chars
// Type 4 special: if player count > 2, use text; else use sprite
// ============================================================================
void hud_element_preload_resources(s32 player_idx, void* elem_def, s32 total_value) {
    u64 result = gx_push_ortho_state_f();
    s32 state = (s32)(result >> 32);
    u32* def = (u32*)result;

    switch (def[0]) {
    case 0: case 10: case 11: case 12: case 13:
    case 14: case 15: case 16: case 17: case 18:
        // Empty/unused types - no resources needed
        break;

    case 1:
        // Text element: load font + measure digits
        text_load_font_for_player(state);
        hud_element_measure_digits();
        break;

    case 2: case 6: case 8: case 9:
        // Sprite element: preload texture
        if (def[1] != (u32)-1) {
            sprite_get_texture_id(def[1]);
        }
        break;

    case 3:
        // Scaled sprite: just load text
        text_load_font_for_player(state);
        break;

    case 4: {
        // Adaptive: text if many players, sprite if few
        s32 iter = -1;
        if ((s32*)def[7] != NULL) {
            s32 tile_dim = *(s32*)def[7];
            s32 cols;
            do {
                iter++;
                s32 row_size = tile_dim << iter;
                // Find largest divisor of total_value <= row_size
                cols = row_size;
                while (cols > 1 && (total_value < cols ||
                       total_value != (total_value / cols) * cols)) {
                    cols--;
                }
                cols = total_value / cols;
            } while (cols > 1 && (tile_dim << iter) >> 2 < cols);

            s32 player_count = hud_get_player_count();
            if (player_count < 3 || cols < 2) {
                sprite_get_texture_id(def[1]);
            } else {
                hud_element_measure_digits();
            }
        }
        break;
    }

    case 5:
        // Alternate digit measurement
        text_get_char_width(&g_hud_alt_digit_chars);
        break;
    }

    gx_pop_ortho_state_f();
}

// ============================================================================
// 0x8010e48c | hud_element_get_height
// Returns the rendered pixel height for a HUD element based on its type.
// Text types return text height; sprite types return scaled texture height.
// ============================================================================
s32 hud_element_get_height(s32 player_idx, u32* elem_def) {
    f32 base_scale = FLOAT_80610828;
    f32 scaled_y = base_scale * FLOAT_80610830;
    u8 text_buf[16];

    switch (elem_def[0]) {
    case 0: case 10: case 11: case 12: case 13:
    case 14: case 15: case 16: case 17: case 18:
        return 0;

    case 1: case 5: {
        // Text height
        s32 font_id = text_load_font_for_player(player_idx);
        format_text(base_scale, text_buf, &g_format_spec, font_id);
        f32 height = text_get_char_height(text_buf);
        return (s32)height;
    }

    case 2: case 6: case 8: case 9:
        // Sprite height (scaled)
        if (elem_def[1] != (u32)-1) {
            s32 tex_id = sprite_get_texture_id(elem_def[1]);
            u32 tex_h = *(u16*)(*g_sprite_table + tex_id * 0x58 + 0x52);
            return (s32)((f32)elem_def[5] * scaled_y * (f32)tex_h);
        }
        break;

    case 3:
        // Scaled sprite height (different scale factor)
        if (elem_def[1] != (u32)-1) {
            s32 tex_id = sprite_get_texture_id(elem_def[1]);
            u32 tex_h = *(u16*)(*g_sprite_table + tex_id * 0x58 + 0x52);
            return (s32)(FLOAT_806157a8 * scaled_y * (f32)tex_h);
        }
        break;

    case 4: {
        // Adaptive: text height if multi-player, sprite height otherwise
        s32 player_count = hud_get_player_count();
        if (player_count > 1) {
            s32 font_id = text_load_font_for_player(player_idx);
            format_text(base_scale, text_buf, &g_format_spec, font_id);
            f32 height = text_get_char_height(text_buf);
            return (s32)height;
        }
        if (elem_def[1] != (u32)-1) {
            s32 tex_id = sprite_get_texture_id(elem_def[1]);
            u32 tex_h = *(u16*)(*g_sprite_table + tex_id * 0x58 + 0x52);
            return (s32)(FLOAT_8061571c * scaled_y * (f32)tex_h);
        }
        break;
    }

    default:
        return 0;
    }
    return 0;
}

// ============================================================================
// 0x8010e690 | hud_element_get_type
// Returns the type field from a HUD element definition table entry.
// Table at DAT_8042e23c, entries are 0xe0 bytes each.
// ============================================================================
u32 hud_element_get_type(s32 index) {
    return *(u32*)(&g_hud_element_table + index * 0xe0);
}

// ============================================================================
// 0x8010e6a8 | hud_element_draw_text
// Renders a text-based HUD element: formats the numeric value, positions
// with left/right alignment, draws with shadow. Handles custom color from
// element definition field [3] or default white-alpha.
//
// param_15: 0 = left-align (centered), 1 = right-align
// ============================================================================
void hud_element_draw_text(s32 player_idx, u32* elem_def, s32 x_pos, s32 y_pos,
                           u32 value, s32 unused, s32 align_right) {
    u64 result = gx_push_ortho_state_reduced();
    f32 shadow_scale = FLOAT_806157c8;
    f32 text_scale = FLOAT_806157b4;
    f32 shadow_alpha = shadow_scale * /* base_alpha */;
    f32 text_alpha = text_scale * /* base_alpha */;

    gx_set_blend_mode(1);

    // Compute color from element def or default
    u32 color;
    u32 raw_color = elem_def[3];
    if (raw_color == 0) {
        s32 alpha_byte = (s32)(FLOAT_80615750 * /* base_alpha */);
        color = (alpha_byte & 0xff) | 0x7f7f7f00;
    } else {
        u32 alpha_channel = raw_color & 0xff;
        f32 scaled_alpha = (f32)alpha_channel * /* base_alpha */;
        u32 final_alpha = gx_set_alpha(scaled_alpha);
        color = (raw_color & 0xffffff00) | final_alpha;
    }

    // Format the value text
    u8* text = format_text_value(&g_format_spec, /* params */,
                                 x_pos, y_pos, value);

    // Get element height for centering
    u32 height = hud_element_get_height(player_idx, elem_def);

    // Compute final screen position
    u32 final_x, final_y;
    if (align_right == 0) {
        // Left-align: center around x_pos
        f32 text_w = text_get_char_width(text);
        final_x = (u32)((f32)(x_pos + (s32)value / 2) - text_w);
    } else {
        // Right-align
        final_x = x_pos - (s32)value / 2;
    }
    final_y = y_pos - (s32)height / 2;

    // Draw at integer coords then with shadow
    text_draw_at_int_coords(&final_x, &final_y, text, (s32)text_alpha & 0xff);
    text_draw_char_shadowed((f32)final_x, (f32)final_y, text, color,
                            (s32)shadow_alpha & 0xff);

    gx_pop_ortho_state_reduced();
}

// ============================================================================
// 0x8010e888 | hud_element_draw_sprite
// Renders a sprite-based HUD element centered at (x_pos, y_pos).
// Selects sprite from element def based on state (param_13):
//   0 = normal sprite (def[1]/def[3]), 1 = alternate sprite (def[2]/def[4])
// ============================================================================
void hud_element_draw_sprite(s32 player_idx, u32* elem_def, s32 x_pos,
                             s32 y_pos, s32 state_index) {
    u64 result = gx_push_ortho_state_with_text();
    s32 ctx = (s32)(result >> 32);
    u32* def = (u32*)result;

    // Select sprite and color based on state
    s32 sprite_id, color_field;
    if (state_index == 0) {
        sprite_id = def[1];
        color_field = def[3];
    } else {
        sprite_id = def[2];
        color_field = def[4];
    }

    if (sprite_id != -1) {
        s32 tex_id = sprite_get_texture_id(sprite_id);

        // Get dimensions
        u32 width_unused = hud_element_preload_resources(ctx, def, 0);
        s32 half_w = (s32)width_unused / 2;
        u32 height = hud_element_get_height(ctx, def);
        s32 half_h = (s32)height / 2;

        // Set alpha
        if (color_field == 0) {
            gx_set_alpha(FLOAT_80615750 * /* alpha */);
        } else {
            gx_set_alpha(FLOAT_80615750 * /* alpha */);
        }

        // Draw centered quad
        gx_bind_texture(tex_id);
        s32 tex_entry = *g_sprite_table + tex_id * 0x58;
        gx_draw_sprite_rect(x_pos - half_w, y_pos - half_h,
                            x_pos + half_w, y_pos + half_h,
                            0, 0,
                            (u32)*(u16*)(tex_entry + 0x4e) << 4,
                            (u32)*(u16*)(tex_entry + 0x52) << 4);
    }

    gx_pop_ortho_state_with_text();
}

// ============================================================================
// 0x8010e9b0 | hud_element_draw_meter
// Renders a meter/progress-bar HUD element using two sprites:
//   def[2] = fill texture, def[1] = border/frame texture.
// Supports directional fill (param_13: 0=left-to-right, 1=right-to-left).
// Draws fill first (with UV proportional to value), then border overlay
// with 3 segments (left cap, middle stretch, right cap).
// ============================================================================
void hud_element_draw_meter(f32 alpha, s32 player_idx, u32* elem_def,
                            s32 x_pos, s32 y_pos, s32 direction) {
    u64 result = gx_push_ortho_extended();
    s32 ctx = (s32)(result >> 32);
    u32* def = (u32*)result;

    if (def[1] == (u32)-1 || def[2] == (u32)-1) {
        gx_pop_ortho_extended();
        return;
    }

    // Load fill texture
    s32 fill_tex = sprite_get_texture_id(def[2]);

    // Get element dimensions
    u32 total_width = hud_element_preload_resources(ctx, def, 0);
    s32 half_w = (s32)total_width / 2;
    u32 total_height = hud_element_get_height(ctx, def);
    s32 half_h = (s32)total_height / 2;

    // Compute margin offset
    s32 margin = (s32)(FLOAT_80615760 * FLOAT_80610828);
    s32 tex_entry = *g_sprite_table + fill_tex * 0x58;
    u32 fill_tex_w = *(u16*)(tex_entry + 0x4e);
    u16 fill_tex_h = *(u16*)(tex_entry + 0x52);

    gx_set_alpha(FLOAT_80615750 * alpha);

    // Compute fill region based on direction
    u32 fill_left, fill_right;
    s32 uv_left_start = 8, uv_right_start = 8;

    if (direction == 0) {
        // Left-to-right fill
        fill_left = margin + (x_pos - half_w);
        uv_left_start = (s32)(/* scale */ * (f32)(fill_tex_w - 0x10)) + 8;
        fill_right = (u32)(/* scaled width */ + (f32)fill_left);
    } else {
        // Right-to-left fill
        fill_right = (x_pos + half_w) - margin;
        uv_right_start = (s32)(/* scale */ * (f32)(fill_tex_w - 0x10)) + 8;
        fill_left = (u32)(-(/* scaled width */ - (f32)fill_right));
    }

    // Draw fill
    gx_set_blend_mode(1);
    gx_bind_texture(fill_tex);
    s32 y_top = y_pos - half_h;
    u32 y_bot = y_pos + half_h;
    u32 tex_h_scaled = (u32)fill_tex_h << 4;
    gx_draw_sprite_rect(fill_left, y_top, fill_right, y_bot,
                        uv_left_start << 4, 0, uv_right_start << 4, tex_h_scaled);

    // Draw border/frame texture (3 segments: left cap, middle, right cap)
    s32 border_tex = sprite_get_texture_id(def[1]);
    gx_bind_texture(border_tex);
    u32 border_left = margin + (x_pos - half_w);
    gx_draw_sprite_rect(x_pos - half_w, y_top, border_left, y_bot,
                        0, 0, 0x80, tex_h_scaled);
    u32 border_right = (x_pos + half_w) - margin;
    u32 uv_edge = (fill_tex_w - 8) * 0x10;
    gx_draw_sprite_rect(border_right, y_top, x_pos + half_w, y_bot,
                        uv_edge, 0, fill_tex_w << 4, tex_h_scaled);
    gx_draw_sprite_rect(border_left, y_top, border_right, y_bot,
                        0x80, 0, uv_edge, tex_h_scaled);

    gx_pop_ortho_extended();
}

// ============================================================================
// 0x8010ecb0 | hud_element_draw_sprite_with_icon
// Renders a sprite HUD element with an overlaid icon badge attachment.
// First draws the main sprite (def[2]) scaled proportionally, then draws
// the icon element via hud_element_draw_sprite for state indicators.
// ============================================================================
void hud_element_draw_sprite_with_icon(f32 alpha, s32 player_idx, u32* elem_def,
                                       s32 x_pos, s32 y_pos) {
    u64 result = gx_push_ortho_full();
    s32 ctx = (s32)(result >> 32);
    u32* def = (u32*)result;

    if (def[2] == (u32)-1) {
        gx_pop_ortho_full();
        return;
    }

    // Load main sprite
    s32 tex_id = sprite_get_texture_id(def[2]);

    // Get element dimensions
    u32 total_width = hud_element_preload_resources(ctx, def, 0);
    s32 half_w = (s32)total_width / 2;
    u32 total_height = hud_element_get_height(ctx, def);
    s32 half_h = (s32)total_height / 2;

    // Draw main sprite
    gx_set_blend_mode(1);
    gx_bind_texture(tex_id);
    gx_set_alpha(FLOAT_80615750 * alpha);

    s32 tex_entry = *g_sprite_table + tex_id * 0x58;
    s32 scaled_w = half_w * 2;
    u32 tex_w_uv = (u32)(/* scale */ * (f32)*(u16*)(tex_entry + 0x4e)) << 4;
    u32 tex_h_uv = (u32)*(u16*)(tex_entry + 0x52) << 4;

    gx_draw_sprite_rect(x_pos - half_w, y_pos - half_h,
                        (s32)(/* scaled width */ + (f32)(x_pos - half_w)),
                        y_pos + half_h,
                        0, 0, tex_w_uv, tex_h_uv);

    // Draw icon overlay
    hud_element_draw_sprite(alpha, ctx, def, x_pos, y_pos, 0);

    gx_pop_ortho_full();
}

// ============================================================================
// 0x8010ee44 | hud_element_draw_tiled_counter
// MASSIVE tiled counter element: renders repeated sprite tiles for ammo
// counters, health pips, etc. Handles:
//   - Tile grid layout from element definition's tile_dims table
//   - Partial row fills for current value
//   - Damage flash: red sine-pulsed overlay when health decreasing
//   - Hit marker spawning for each lost pip
//   - Directional layout (param_16: 0=left, 1=right)
//   - Fallback to text mode when player count > 2
//
// param_13: total capacity
// param_14: current filled count
// param_15: number of recently lost pips (for hit markers)
// param_16: 0=left-aligned, 1=right-aligned
// ============================================================================
void hud_element_draw_tiled_counter(u32 x_pos, s32 y_pos, s32 total_capacity,
                                    s32 current_filled, s32 lost_count,
                                    s32 align_right) {
    u64 result = gx_push_ortho_max();
    s32 ctx = (s32)(result >> 32);
    s32 elem_base = (s32)result;

    if (*(s32*)(elem_base + 4) == -1 || *(s32**)(elem_base + 0x1c) == NULL) {
        gx_pop_ortho_max();
        return;
    }

    // Compute tile grid dimensions
    s32 tile_base = **(s32**)(elem_base + 0x1c);
    s32 shift = -1;
    s32 cols_per_row;
    s32 num_rows;
    do {
        shift++;
        s32 row_size = tile_base << shift;
        cols_per_row = row_size;
        while (cols_per_row > 1 &&
               (current_filled < cols_per_row ||
                current_filled != (current_filled / cols_per_row) * cols_per_row)) {
            cols_per_row--;
        }
        num_rows = current_filled / cols_per_row;
    } while (num_rows > 1 && (tile_base << shift) >> 2 < num_rows);

    s32 player_count = hud_get_player_count();

    if (player_count < 2 || num_rows < 2) {
        // === SPRITE TILE MODE ===

        // Compute color/alpha
        u32 fg_color, bg_color;
        u32 raw_color = *(u32*)(elem_base + 0xc);
        if (raw_color == 0) {
            u32 alpha_val = gx_set_alpha(FLOAT_80615750 * /* base_alpha */);
            fg_color = (alpha_val & 0xff) | 0x7f7f7f00;
        } else {
            u32 alpha_scaled = gx_set_alpha((f32)(raw_color & 0xff) * /* alpha */);
            fg_color = (raw_color & 0xffffff00) | alpha_scaled;
        }

        // Background/empty color
        u32 raw_bg = *(u32*)(elem_base + 0x10);
        if (raw_bg == 0) {
            gx_set_alpha(FLOAT_80615830 * /* alpha */);
        } else {
            gx_set_alpha((f32)(raw_bg & 0xff) * /* alpha */);
        }

        // Damage flash: sine-pulsed red overlay
        s32 show_flash = (current_filled >> 31) +
                         ((u32)(total_capacity - 1 <= (u32)(current_filled >> 2)) -
                          ((s32)(total_capacity - 1) >> 31));
        if (show_flash != 0) {
            u32 flash_alpha = gx_set_alpha(FLOAT_80615750 * /* alpha */);
            // Draw sine-pulsed red highlight
            gx_draw_colored_quad(FLOAT_80615734 *
                g_sine_table[/* sine_index */] + FLOAT_80615734,
                fg_color, (flash_alpha & 0xff) | 0x7f2f2f00);
        }

        // Load tile texture
        f32 custom_scale = FLOAT_80610828 * *(f32*)(elem_base + 0x14);
        s32 tex_id = sprite_get_texture_id(*(s32*)(elem_base + 4));
        s32 tex_entry = *g_sprite_table + tex_id * 0x58;
        u32 tex_w = *(u16*)(tex_entry + 0x4e);
        u32 tex_h = *(u16*)(tex_entry + 0x52);

        f32 tile_px_w = (f32)((s32)tex_w / tile_base);
        s32 tile_h_px = (s32)((f32)tex_h * custom_scale);
        s32 half_tile_h = tile_h_px / 2;
        s32 half_total_w = (s32)(FLOAT_80615734 * custom_scale * (f32)(tile_px_w * cols_per_row));

        // Position grid vertically centered
        s32 grid_y = (y_pos + (tile_h_px + 1) * ((s32)(num_rows - (num_rows & 1)) / 2)) -
                     (1 - (num_rows & 1)) * half_tile_h;

        gx_set_blend_mode(1);
        gx_bind_texture(tex_id);

        s32 tile_row_size = tile_base << shift;
        u32 tex_h_uv = tex_h << 4;
        u32 full_row_uv = (s32)tile_px_w * (tile_row_size - cols_per_row) * 0x10;
        s32 drawn = 0;
        u32 remaining = 0;

        s32 x_left = (s32)x_pos - half_total_w;
        s32 x_right = (s32)x_pos + half_total_w;
        u32 tex_w_uv = ((u32)tex_w << shift) << 4;

        // Draw filled rows
        while (drawn < total_capacity) {
            remaining = total_capacity - drawn;
            if ((s32)remaining < cols_per_row) {
                // Partial last row
                drawn += remaining;
                s32 partial_w = (s32)(custom_scale * (f32)remaining * (-tile_px_w));

                s32 draw_left, draw_right;
                if (align_right == 0) {
                    draw_left = x_left;
                    draw_right = x_left + partial_w;
                } else {
                    draw_left = x_right - partial_w;
                    draw_right = x_right;
                }

                gx_draw_sprite_rect(draw_left, grid_y - half_tile_h,
                                    draw_right, grid_y + half_tile_h,
                                    (s32)tile_px_w * (tile_row_size - remaining) * 0x10,
                                    0, tex_w_uv, tex_h_uv);
            } else {
                // Full row
                gx_draw_sprite_rect(x_left, grid_y - half_tile_h,
                                    x_right, grid_y + half_tile_h,
                                    full_row_uv, 0, tex_w_uv, tex_h_uv);
                drawn += cols_per_row;
                grid_y -= (tile_h_px + 1);
            }
        }

        // Spawn hit markers for lost pips
        f32 half_scale = FLOAT_80615734;
        f32 tile_world_w = custom_scale * (-tile_px_w);
        for (s32 i = lost_count - 1; i >= 0; i--) {
            s32 pip_index = remaining + i;
            s32 col_offset = pip_index - (pip_index / cols_per_row) * cols_per_row;
            s32 marker_x = (s32)(half_scale * tile_world_w +
                           tile_world_w * (f32)col_offset);

            s32 final_x;
            if (align_right == 0) {
                final_x = marker_x + x_left;
            } else {
                final_x = x_right - marker_x;
            }

            hud_hit_marker_record(final_x,
                                  grid_y - tile_h_px * (pip_index / (cols_per_row + 1)),
                                  x_pos, (f32)tile_world_w,
                                  (f32)(half_tile_h * 2), tex_id, tile_px_w);
        }

        // Draw remaining empty rows below filled
        if ((s32)remaining < cols_per_row) {
            drawn += (cols_per_row - remaining);
            s32 empty_w = half_total_w * 2 - /* partial_offset */;

            s32 draw_left, draw_right;
            if (align_right == 0) {
                draw_left = x_right - empty_w;
                draw_right = x_right;
            } else {
                draw_left = x_left;
                draw_right = x_left + empty_w;
            }
            gx_draw_sprite_rect(draw_left, grid_y - half_tile_h,
                                draw_right, grid_y + half_tile_h,
                                (s32)tile_px_w * (tile_row_size - (cols_per_row - remaining)) * 0x10,
                                0, tex_w_uv, tex_h_uv);
            grid_y -= (tile_h_px + 1);
        }

        // Fill remaining full empty rows
        while (drawn < current_filled) {
            gx_draw_sprite_rect(x_left, grid_y - half_tile_h,
                                x_right, grid_y + half_tile_h,
                                full_row_uv, 0, tex_w_uv, tex_h_uv);
            drawn += cols_per_row;
            grid_y -= (tile_h_px + 1);
        }

    } else {
        // === TEXT FALLBACK MODE (3+ players) ===
        u32 width = hud_element_preload_resources(ctx, elem_base, current_filled);
        hud_element_draw_text(ctx, elem_base, x_pos, y_pos, width,
                              total_capacity, 1 - align_right);
    }

    gx_pop_ortho_max();
}

// ============================================================================
// 0x8010f4a0 | hud_get_row_height_scale
// Returns the HUD vertical row spacing scale factor.
// Combines aspect ratio correction with the base row height constant.
// Used by layout functions to compute Y offsets between HUD rows.
// ============================================================================
f32 hud_get_row_height_scale(void) {
    f32 aspect = screen_effect_get_aspect_scale();
    return FLOAT_80615834 * FLOAT_80610830 * aspect;
}
