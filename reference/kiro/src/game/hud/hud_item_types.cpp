// Address: 0x80047bc0-0x80048070 | Name: hud_item_types | Date: 2026-08-10
// HUD item type creation API and text measurement utilities.
// These functions create specific item types (header, footer, text display,
// slider, value selector, labeled pair, wrapped text) and provide height
// calculation utilities used by the layout engine.

#include "hud_internal.h"

// ============================================================================
// Global HUD item builder context pointer
// ============================================================================

// DAT_806119dc - current HUD item being constructed/configured
extern int g_hud_current_item;  // 0x806119dc
extern int g_hud_layout_table;  // 0x806119e4

// ============================================================================
// Item Type Creators
// ============================================================================

// Address: 0x80047bc0
// Creates a header-style HUD item (type=2).
// Sets flags 0x100800 (non-selectable + divider section marker).
// Used by menu builders to create section headers that visually separate groups.
void hud_item_add_header(float height, unsigned int flags) {
    hud_item_configure_layout(height, g_hud_current_item, 2, flags);
    *(unsigned int*)(g_hud_current_item + 0x28) |= 0x100800;
}

// Address: 0x80047c00
// Creates a footer-style HUD item (type=3).
// Simple layout-only item used to close sections in menus.
void hud_item_add_footer(float height, unsigned int flags) {
    hud_item_configure_layout(height, g_hud_current_item, 3, flags);
}

// Address: 0x80047c2c
// Creates a text display HUD item (type=4).
// Measures font line height and adjusts item height when a parent item exists.
// Allocates two event strings for label/value display pairs.
void hud_item_add_text_display(float height, unsigned int flags) {
    float font_line_height;

    hud_item_configure_layout(height, g_hud_current_item, 4, flags);
    font_line_height = hud_item_get_font_line_height(g_hud_current_item);

    if (*(int*)(g_hud_current_item + 0x20) != 0) {
        *(float*)(g_hud_current_item + 0x44) -= (float)font_line_height;
    }

    hud_event_add_string(0);
    hud_event_add_string(0);
}

// Address: 0x80047c8c
// Creates a slider HUD item (type=5).
// Parameters configure the slider range and step:
//   +0x98 = min value (param_5)
//   +0xa4 = current/initial value (param_2 as float)
//   +0x94 = max value (param_7)
//   +0xa8 = step size (param_6)
// Sets flag 0x100000 (has numeric value).
void hud_item_add_slider(/* varargs via register protocol */
    int param_1, float slider_value, int param_3, int param_4,
    short min_value, int step_size, int max_value)
{
    int item;

    // Compiler prologue saves registers
    item = g_hud_current_item;
    hud_item_configure_layout(/* extracted float */, item, 5, /* extracted flags */);

    *(int*)(item + 0x48) = param_1;  // linked data pointer
    *(short*)(item + 0x98) = min_value;
    *(float*)(item + 0xa4) = slider_value;
    *(int*)(item + 0x94) = max_value;
    *(int*)(item + 0xa8) = step_size;
    *(unsigned int*)(item + 0x28) |= 0x100000;  // flag: has value
}

// Address: 0x80047d20
// Creates a value selector HUD item (type=6).
// Multi-option picker that cycles through discrete values.
// Parameters:
//   param_3 = option count (+0xa0)
//   param_4 = wrap mode (+0x9c)
//   param_5 = initial value (+0xa8)
//   param_6 = display callback (+0xac)
//   param_7 = max value (+0x94)
// Sets flags 0x1300000 (has value + display value + custom renderer).
void hud_item_add_value_selector(
    int param_1, int param_2, int option_count, int wrap_mode,
    int initial_value, int display_callback, int max_value)
{
    int item;

    item = g_hud_current_item;
    hud_item_configure_layout(/* float */, item, 6, /* flags */ | 0x200000);

    *(int*)(item + 0x48) = param_1;  // linked variable pointer
    *(int*)(item + 0x94) = max_value;
    *(int*)(item + 0xa0) = option_count;
    *(int*)(item + 0x9c) = wrap_mode;
    *(int*)(item + 0xa8) = initial_value;
    *(int*)(item + 0xac) = display_callback;
    *(unsigned int*)(item + 0x28) |= 0x1100000;  // has value + custom display

    hud_item_set_value_display(param_1);
}

// Address: 0x80047dc0
// Creates a labeled pair HUD item (type=7).
// Two-column display: left label + right value. Used for settings summary,
// stats display, and info panels. Has a sort key for ordered lists.
// Sets flags 0x200100 (pair layout + sorted).
void hud_item_add_labeled_pair(
    int param_1, int param_2, int left_label_str, int right_value_str)
{
    float sort_value;
    int item;

    item = g_hud_current_item;
    // Extract sort key from register-pair return
    hud_item_set_sort_key(item, /* sort_key */);
    hud_item_configure_layout(sort_value, item, 7, /* flags */ | 0x200100);

    hud_event_add_string(left_label_str);
    hud_event_add_string(right_value_str);
}

// Address: 0x80047e34
// Creates a wrapped-text sub-item for multi-line content.
// Measures text height for word-wrap within parent bounds. Creates a sized
// list item to contain the text, adjusts padding to match parent layout.
// References:
//   +0xb4 = item render mode (must be 3 for text)
//   +0xbc = font table index
//   +0x8c = back-link to parent item
//   +0xd0 = line count - 1 (from parent +0xc4)
void hud_item_add_wrapped_text(void) {
    int item_data;
    int parent_item;
    float text_height;
    float parent_bottom_margin;
    int parent_padding_mode;
    int sub_item;

    item_data = /* prologue register restore stub */;
    parent_item = g_hud_current_item;

    if (*(int*)(parent_item + 0xb4) == 3) {
        // Measure the text height for word-wrapping
        text_height = hud_measure_text_height(
            *(float*)(parent_item + 0x44),       // available width
            *(int*)(parent_item + 0xbc),          // font table
            *(char**)(item_data + 4)              // text string
        );

        parent_bottom_margin = *(float*)(g_hud_layout_table + 0x50);
        parent_padding_mode = *(int*)(g_hud_layout_table + 0x48);

        // Set padding before the text sub-item
        hud_set_item_padding_mode(PADDING_SMALL, 0, 1, 0);

        // Create the sized sub-item
        sub_item = hud_add_list_item_sized(text_height);

        // Link back to parent
        *(int*)(sub_item + 0x8c) = parent_item;

        // Adjust position: add font line height offset
        float line_height = hud_item_get_font_line_height(sub_item);
        *(float*)(sub_item + 0x3c) = PADDING_LINE_TOP + (float)line_height;

        // Store line count from parent
        *(int*)(sub_item + 0xd0) = *(int*)(*(int*)(sub_item + 0x8c) + 0xc4) - 1;

        // Restore padding after the sub-item (compensates for parent bottom margin)
        hud_set_item_padding_mode(
            parent_bottom_margin /* negated and converted */,
            parent_padding_mode, 1, 0
        );
    }

    // Restore builder context
    g_hud_current_item = parent_item;
}

// ============================================================================
// Height/Position Calculation Utilities
// ============================================================================

// Address: 0x80047f84
// Calculates the Y content position for an item.
// Base: parent top margin (+0x7c) + item Y offset (+0xe8).
// If flag 0x40 set (item is inside a scrolled region), adds scroll
// compensation: parent content height - parent bottom margin.
float hud_item_calc_content_y(int item) {
    int parent = *(int*)(item + 0x88);
    float y_pos = *(float*)(parent + 0x7c) + *(float*)(item + 0xe8);

    if ((*(unsigned int*)(item + 0x28) & 0x40) == 0) {
        return y_pos;
    }

    // Scrolled item: add remaining height below content area
    return y_pos + (*(float*)(parent + 0x68) - *(float*)(parent + 0x80));
}

// Address: 0x80047fb4
// Calculates text item height by measuring wrapped text within available bounds.
// Accounts for:
//   +0xc0 alignment mode (1 = right-aligned, adjusts available width)
//   +0xdc text margin
//   FLOAT_80611a68 = pixel-to-normalized X scale
//   FLOAT_80611a60 = normalized-to-pixel conversion factor
// Returns: measured height + top + bottom padding (2x PADDING_VERT).
float hud_item_calc_text_height(int item) {
    int event;
    char* text;
    float bounds[4];      // full item bounds
    float text_rect[2];   // text area start
    float text_end;       // text area end X
    float padding[4];     // label bounds (unused output)
    float available_width;
    float measured_height;

    // Get text from current event chain
    event = hud_item_get_current_event(item);
    text = (event != 0) ? *(char**)(event + 4) : NULL;

    // Compute text bounds regions
    hud_item_compute_text_bounds(item, padding, text_rect, bounds);

    // Calculate available text width based on alignment
    if (*(int*)(item + 0xc0) == 1) {
        // Right-aligned: content from right edge inward
        available_width = -(SCALE_X * PIXEL_SCALE_X - (text_end - text_rect[0]));
    } else {
        // Left-aligned: content from left edge, minus text margin
        available_width = text_end - (*(float*)(item + 0xdc) * PIXEL_SCALE_X + text_rect[0]);
    }

    // Measure wrapped text height
    measured_height = hud_measure_text_height(
        available_width * NORM_TO_PIXEL,
        *(int*)(item + 0xbc),  // font table
        text
    );

    return PADDING_VERT + (PADDING_VERT + measured_height);
}

// Address: 0x80048070
// Calculates simple text height for items with inline text pointer at +0x48.
// Uses item width (+0x44) and font table (+0xbc) for measurement.
// Returns: measured height + 2x vertical padding.
float hud_item_calc_simple_text_height(int item) {
    float measured_height;

    measured_height = hud_measure_text_height(
        *(float*)(item + 0x44),       // available width
        *(int*)(item + 0xbc),          // font table index
        *(char**)(item + 0x48)         // inline text pointer
    );

    return PADDING_VERT + (PADDING_VERT + measured_height);
}

// ============================================================================
// Address: 0x800480ac-0x800489d0 | Batch 088 | Date: 2026-08-10
// HUD item height calculation dispatch, bounds computation, and text rendering.
// ============================================================================

// Address: 0x800480ac
// Calculates height for a value selector item (type=6).
// Formats the current value into a temp string buffer (DAT_80525fb8, 128 chars)
// using the display callback, then measures the resulting text height.
float hud_item_calc_value_selector_height(int item) {
    float measured_height;

    // Format value into display string
    // FUN_80049c38 = value format dispatch (printf-like into buffer)
    // Params: buffer, max_len, linked_value, wrap_mode, max_value, display_cb, 1
    hud_item_format_value_string(
        &g_hud_temp_string_buffer,    // DAT_80525fb8
        128,                           // max length
        **(int**)(item + 0x48),        // current value (dereferenced)
        *(int*)(item + 0x9c),          // wrap mode
        *(unsigned int*)(item + 0x94), // max value
        *(int*)(item + 0xac),          // display callback
        1                              // include arrows flag
    );

    measured_height = hud_measure_text_height(
        *(float*)(item + 0x44),        // available width
        *(int*)(item + 0xbc),          // font table
        &g_hud_temp_string_buffer
    );

    return PADDING_VERT + (PADDING_VERT + measured_height);
}

// Address: 0x8004811c
// Calculates height for a labeled pair item (type=7).
// Checks if either of the two event strings (index 0 or 1) has text.
// If either has content: measures one line height + vertical padding + spacing.
// If neither has content: returns a minimal height constant.
float hud_item_calc_labeled_pair_height(int item) {
    int event;
    float height = LABELED_PAIR_MIN_HEIGHT;  // FLOAT_80613ca4

    // Check event string 0 (left label)
    event = hud_item_get_event_at_index(item, 0);
    if (event != 0 && *(int*)(event + 4) != 0) {
        goto has_text;
    }

    // Check event string 1 (right value)
    event = hud_item_get_event_at_index(item, 1);
    if (event != 0 && *(int*)(event + 4) != 0) {
has_text:
        float text_h = hud_measure_text_height(
            *(float*)(item + 0x44),
            *(int*)(item + 0xbc),
            NULL  // measures default line height
        );
        height = PADDING_VERT + PADDING_VERT + (SPACING_SMALL + text_h);
    }

    return height;
}

// Address: 0x800481c4
// Master height calculation dispatch. Switches on item render mode (+0xb4):
//   0 = no content (zero height)
//   1 = single line (measure NULL text = one line)
//   2 = wrapped text (calls hud_item_calc_text_height)
//   3 = fixed height (zero - uses explicit height)
//   4 = event text (measures event string text)
//   5 = simple text (calls hud_item_calc_simple_text_height)
//   6 = value selector (calls hud_item_calc_value_selector_height)
//   7 = labeled pair (calls hud_item_calc_labeled_pair_height)
float hud_item_calc_height_by_mode(int item) {
    int event;
    char* text;
    float height;

    if (*(int*)(item + 0xb4) == 0) {
        return 0.0f;
    }

    event = hud_item_get_current_event(item);

    switch (*(int*)(item + 0xb4)) {
    case 1:
        // Single line: measure NULL to get one line height
        height = hud_measure_text_height(
            *(float*)(item + 0x44), *(int*)(item + 0xbc), NULL);
        break;
    case 2:
        // Wrapped text with bounds calculation
        height = hud_item_calc_text_height(item);
        break;
    case 3:
        // Fixed height (returns 0, item uses explicit height field)
        height = 0.0f;
        break;
    case 4:
        // Event text string
        text = (event != 0) ? *(char**)(event + 4) : NULL;
        height = hud_measure_text_height(
            *(float*)(item + 0x44), *(int*)(item + 0xbc), text);
        break;
    case 5:
        // Simple inline text
        height = hud_item_calc_simple_text_height(item);
        break;
    case 6:
        // Value selector (format + measure)
        height = hud_item_calc_value_selector_height(item);
        break;
    case 7:
        // Labeled pair
        height = hud_item_calc_labeled_pair_height(item);
        break;
    default:
        height = 0.0f;
        break;
    }

    return height;
}

// Address: 0x80048398
// Computes slider item bounds: content rect, label rect, and value rect.
// If render mode != 5 (simple text), applies horizontal padding FLOAT_80613ca4.
// Output rects (each 4 floats: left, top, right, bottom):
//   pfVar3 (out1) = full content rect (inset from pixel bounds)
//   param_3 (out2) = left label rect (left portion of content)
//   param_4 (out3) = right value rect (right portion of content)
void hud_item_compute_slider_bounds(
    int param_1, int param_2, float* label_rect, float* value_rect)
{
    float pixel_bounds[4];
    float h_padding;
    float v_padding;
    float* content_rect;  // output via register pair
    int item;

    item = /* register pair high */;
    content_rect = /* register pair low */;

    // Determine horizontal padding
    if (*(int*)(item + 0xb4) != 5) {
        h_padding = SLIDER_H_PADDING * PIXEL_SCALE_X + SCREEN_OFFSET_X;
    } else {
        h_padding = 0.0f;
    }
    v_padding = SLIDER_V_PADDING * PIXEL_SCALE_Y + SCREEN_OFFSET_Y;

    hud_item_get_pixel_bounds(item, pixel_bounds);

    // Content rect: inset by padding
    if (content_rect != NULL) {
        content_rect[0] = pixel_bounds[0] + h_padding;  // left
        content_rect[1] = pixel_bounds[1];               // top
        content_rect[2] = pixel_bounds[2] - h_padding;  // right
        content_rect[3] = pixel_bounds[3];               // bottom
    }

    // Label rect: left portion with specific width ratio
    if (label_rect != NULL) {
        label_rect[0] = pixel_bounds[0];
        label_rect[1] = pixel_bounds[1];
        label_rect[2] = pixel_bounds[0] + LABEL_WIDTH_RATIO * h_padding;
        label_rect[3] = pixel_bounds[1] + v_padding;
    }

    // Value rect: right portion
    if (value_rect != NULL) {
        value_rect[0] = pixel_bounds[2] - LABEL_WIDTH_RATIO * h_padding;
        value_rect[1] = pixel_bounds[1];
        value_rect[2] = pixel_bounds[2];
        value_rect[3] = pixel_bounds[1] + v_padding;
    }
}

// Address: 0x800484c4
// Computes slider thumb position bounds. Gets pixel bounds, applies insets,
// then calculates thumb X position from the current value (offset 0xc8)
// relative to the slider range (stored in FLOAT_80613cb0 = range divisor).
// Output rects:
//   content_rect = track area (pixel bounds with small inset)
//   thumb_rect = thumb indicator position (centered on value position)
void hud_item_compute_slider_thumb_bounds(
    int param_1, int param_2, float* thumb_rect)
{
    float pixel_bounds[4];
    int item;
    float* content_rect;
    float track_left, track_right;
    float thumb_half_width;
    float value_normalized;
    float thumb_center;

    item = /* register pair high */;
    content_rect = /* register pair low */;

    hud_item_get_pixel_bounds(item, pixel_bounds);

    // Apply small vertical inset
    pixel_bounds[1] += THUMB_INSET_Y * PIXEL_SCALE_Y + pixel_bounds[1];
    pixel_bounds[0] += 0.0f;  // FLOAT_80613c8c
    pixel_bounds[2] -= 0.0f;

    float bar_top = THUMB_BAR_OFFSET * PIXEL_SCALE_Y + pixel_bounds[1];

    if (content_rect != NULL) {
        content_rect[0] = pixel_bounds[0];
        content_rect[1] = pixel_bounds[1];
        content_rect[2] = pixel_bounds[2];
        content_rect[3] = bar_top;
    }

    if (thumb_rect != NULL) {
        // Compute thumb position from current value
        // value at +0xc8, range divisor = FLOAT_80613cb0
        thumb_half_width = 0.5f * (THUMB_WIDTH * PIXEL_SCALE_X + SCREEN_OFFSET_X);

        track_left = pixel_bounds[0] + thumb_half_width;
        value_normalized = (float)(*(int*)(item + 0xc8) /* ^ sign bit */) / SLIDER_RANGE_DIVISOR;
        thumb_center = track_left + value_normalized * ((pixel_bounds[2] - thumb_half_width) - track_left);

        thumb_rect[0] = thumb_center - thumb_half_width;
        thumb_rect[1] = pixel_bounds[1];
        thumb_rect[2] = thumb_center + thumb_half_width;
        thumb_rect[3] = bar_top;
    }
}

// Address: 0x80048638
// Computes normalized screen-space bounds for an item.
// Converts from pixel coordinates to normalized [0,1] space using scale factors.
// For item types 6 (value selector) and 7 (labeled pair), uses parent layout
// bounds at +0x210 with special left/right alignment based on flags 0x800/0x400.
// Calls hud_item_calc_content_y for scroll-aware Y positioning.
void hud_item_compute_normalized_bounds(int item, float* bounds) {
    int parent = *(int*)(item + 0x88);

    // X bounds: normalize parent left + item X offset
    bounds[0] = NORM_TO_PIXEL * (*(float*)(parent + 0xa4) - SCREEN_OFFSET_X)
                + *(float*)(item + 0xe4);
    bounds[2] = bounds[0] + *(float*)(item + 0xec);

    float right_edge = RIGHT_EDGE_CONST;   // FLOAT_80613cb8
    float left_edge = LEFT_EDGE_CONST;     // FLOAT_80613cb4

    // Types 6/7 (value selector, labeled pair) use parent content width
    if (*(int*)(item + 0xc0) == 6 || *(int*)(item + 0xc0) == 7) {
        if ((*(unsigned int*)(parent + 0x40) & 0x800) != 0) {
            // Right-aligned: right edge from parent width, left from constant
            bounds[2] = *(float*)(parent + 0x210) - THUMB_INSET_Y;
            bounds[0] = left_edge;
        } else if ((*(unsigned int*)(parent + 0x40) & 0x400) != 0) {
            // Left-aligned: left edge from parent width, right from constant
            bounds[0] = THUMB_INSET_Y + *(float*)(parent + 0x210);
            bounds[2] = right_edge;
        }
    }

    // Y bounds: scroll-aware content position
    bounds[1] = hud_item_calc_content_y(item);
    bounds[3] = bounds[1] + *(float*)(item + 0xf0);
}

// Address: 0x80048714
// Sets the clip/scissor rect for an item during rendering.
// For types 6 (value selector) and 7 (labeled pair), computes pixel bounds
// and sets a custom GX scissor rectangle. For other types, delegates to the
// standard hud_item_set_scissor function.
void hud_item_set_clip_rect(int viewport, int item) {
    float pixel_bounds[4];

    if (*(int*)(item + 0xc0) == 6 || *(int*)(item + 0xc0) == 7) {
        hud_item_get_pixel_bounds(item, pixel_bounds);
        // Convert to integer coords and set GX scissor
        gx_set_scissor_rect(
            (int)pixel_bounds[0], (int)pixel_bounds[1],
            (int)pixel_bounds[2], (int)pixel_bounds[3]
        );
    } else {
        hud_item_set_scissor(viewport, item);
    }
}

// Address: 0x8004879c
// Returns a color value from the item's palette based on flag bits.
// The palette is stored at the font table pointer (+0x1c) with offsets:
//   flag 0x100 → palette[+0x58] (highlight color)
//   flag 0x200 → palette[+0x50] (secondary color)
//   default    → palette[+0x54] (primary/normal color)
unsigned int hud_item_get_palette_color(int item, unsigned int flags) {
    int font_data = *(int*)(item + 0x1c);

    if ((flags & 0x100) != 0) {
        return *(unsigned int*)(font_data + 0x58);  // highlight
    }
    if ((flags & 0x200) != 0) {
        return *(unsigned int*)(font_data + 0x50);  // secondary
    }
    return *(unsigned int*)(font_data + 0x54);      // primary/normal
}

// Address: 0x800487d0
// MASSIVE: Draws item text with full styling pipeline.
// Pipeline steps:
//   1. Set font table from item (+0xbc)
//   2. Compute item colors (foreground + shadow from palette)
//   3. Check dim state (unfocused items in certain modes get alpha fade)
//   4. If not dimmed and not focused, check flag 0x8 for opacity pulse
//   5. If flag 0x10 at +0xb8: apply custom color interpolation (focus glow)
//   6. Apply case/scale transform to text
//   7. If flag 0x4000000: apply alpha fade effect (text reveal animation)
//   8. Draw text: focused items use shadowed text, others use plain
//   9. Reset color state if alpha fade was applied
void hud_item_draw_text(
    /* register params */ float x_pos, float y_pos, float scale,
    int viewport, int item, int event_data)
{
    unsigned int fg_color;
    unsigned int shadow_color;
    unsigned char* text_ptr;
    unsigned char* styled_text;
    int is_dimmed;
    int is_focused;

    text_set_font_table(*(int*)(item + 0xbc));
    hud_item_compute_colors(viewport, item, &fg_color, &shadow_color);

    is_dimmed = hud_element_needs_dim(viewport);

    if (is_dimmed == 0) {
        // Not dimmed: check focus state
        is_focused = hud_item_is_focused(item);
        if (is_focused == 0 && (*(unsigned int*)(event_data + 0xc) & 8) != 0) {
            // Non-focused item with opacity pulse flag
            unsigned char alpha = float_to_byte(
                OPACITY_PULSE_SCALE * *(float*)(viewport + 0xf0));
            fg_color = (fg_color & 0xFFFFFF00) | alpha;
        }

        // Custom focus glow interpolation (flag 0x10 at +0xb8)
        if ((*(unsigned int*)(item + 0xb8) & 0x10) != 0) {
            float t = *(float*)(item + 0x104);  // interpolation factor
            unsigned char r = float_to_byte(-(GLOW_R * t - BASE_ALPHA));
            unsigned char g = float_to_byte(-(GLOW_G * t - BASE_ALPHA));
            unsigned char a = float_to_byte(BASE_ALPHA * *(float*)(viewport + 0xf0));
            fg_color = a | (r << 8) | (r << 16) | (g << 24);
        }
    }

    // Get text from event data
    text_ptr = *(unsigned char**)(event_data + 4);
    if (text_ptr != NULL) {
        // Apply uppercase/font scale
        styled_text = hud_text_apply_case_scale(1.0f, text_ptr, *(unsigned int*)(item + 0xb8));

        if ((*(unsigned int*)(item + 0x28) & 0x4000000) == 0) {
            // Normal text: apply scale check
            styled_text = text_check_scale(scale, styled_text);
        } else {
            // Alpha fade mode: progressive text reveal
            text_apply_alpha_fade(scale, styled_text, 0, 1);
        }

        // Draw with or without shadow based on focus
        is_focused = hud_item_is_focused(item);
        if (is_focused == 0) {
            text_draw_char(x_pos, y_pos, styled_text, fg_color);
        } else {
            text_draw_char_shadowed(x_pos, y_pos, styled_text, fg_color, shadow_color);
        }

        // Reset color if alpha fade was used
        if ((*(unsigned int*)(item + 0x28) & 0x4000000) != 0) {
            text_set_color_rgb(0.0f, 0.0f, 0.0f);
        }
    }
}

// Address: 0x800489d0
// Draws value selector item text with navigation arrows.
// Pipeline:
//   1. Determines focus state and arrow visibility (left/right indicators)
//   2. Applies case/scale transform
//   3. Measures text width for alignment computation
//   4. Handles 3 alignment modes: left (default), right (mode 1/4), center (mode 2)
//   5. Draws left and right arrow indicators (unless flag 0x400000 disables them)
//   6. Calls hud_item_draw_text for the actual value text
//   7. Resets color state if alpha fade active
void hud_item_draw_value_selector(int param_1, int param_2, float* bounds) {
    int viewport;
    int item;
    int event;
    unsigned char* styled_text;
    float opacity;
    int is_dimmed;
    int is_focused;
    int left_arrow_state;   // 0=none, 1=normal, 2=pressed, 3=disabled
    int right_arrow_state;
    float text_width;
    float x_pos, y_pos;
    float arrow_spacing;
    int alignment_mode;
    float left_x, right_x;

    viewport = /* register pair high */;
    item = /* register pair low */;

    opacity = *(float*)(viewport + 0xf0);
    is_dimmed = hud_element_needs_dim(viewport);

    left_arrow_state = 0;
    right_arrow_state = 0;

    event = hud_item_get_current_event(item);
    text_set_font_table(*(int*)(item + 0xbc));

    styled_text = hud_text_apply_case_scale(
        1.0f, *(unsigned char**)(event + 4), *(unsigned int*)(item + 0xb8));

    arrow_spacing = FONT_LINE_HEIGHT - ARROW_SPACING;  // FLOAT_80611674 - FLOAT_80613ca0

    // Determine arrow visibility from focus + nav state
    if (is_dimmed == 0) {
        is_focused = hud_item_is_focused(item);
        if (is_focused != 0) {
            // Focused: show arrows based on navigation flags
            left_arrow_state = ((*(unsigned int*)(viewport + 0xc4) >> 2) & 1) + 1;
            right_arrow_state = ((*(unsigned int*)(viewport + 0xc4) & 8) != 0) ? 2 : 1;
        }
    }
    if (is_dimmed != 0 || (*(unsigned int*)(item + 0x28) & 1) != 0) {
        // Dimmed or disabled: show disabled arrows
        left_arrow_state = 3;
        right_arrow_state = 3;
    }

    // Set arrow colors from palette
    unsigned int left_color = select_list_get_weight(item, left_arrow_state);
    hud_set_sprite_color(opacity, left_color);
    unsigned int right_color = select_list_get_weight(item, right_arrow_state);
    hud_set_sprite_color(opacity, right_color);

    // Compute text position with margins
    x_pos = *(float*)(item + 0xdc) * PIXEL_SCALE_X + bounds[0];
    y_pos = *(float*)(item + 0xe0) * PIXEL_SCALE_Y + bounds[1];

    // Apply alpha fade if flag set
    if ((*(unsigned int*)(item + 0x28) & 0x4000000) != 0) {
        text_apply_alpha_fade(
            -(THUMB_INSET_Y * (arrow_spacing + ARROW_SPACING) - (bounds[2] - bounds[0])),
            styled_text, 0, 1);
    }

    // Measure text width for alignment
    text_width = text_get_char_width(styled_text);

    alignment_mode = *(int*)(item + 0xc0);
    float total_width = arrow_spacing + (ARROW_SPACING + (text_width + ARROW_SPACING + arrow_spacing));

    // Compute positions based on alignment
    if (alignment_mode == 1 || alignment_mode == 4) {
        // Right-aligned
        x_pos -= total_width;
    }

    if (alignment_mode == 2) {
        // Center-aligned
        float center = 0.5f * (bounds[0] + bounds[2]);
        float half_text = 0.5f * text_width;
        left_x = center - half_text - ARROW_SPACING - arrow_spacing;
        right_x = ARROW_SPACING + center + half_text;
        x_pos = (total_width - arrow_spacing) - ARROW_SPACING;
    } else {
        right_x = x_pos + total_width - arrow_spacing;
        left_x = ARROW_SPACING + x_pos + arrow_spacing;
    }

    // Draw arrows (unless flag 0x400000 = no arrows)
    float arrow_top = THUMB_INSET_Y + y_pos;
    float arrow_bottom = arrow_top + arrow_spacing;

    if ((*(unsigned int*)(item + 0xb8) & 0x400000) == 0) {
        // Draw right arrow
        hud_draw_arrow_right(right_x, arrow_top, right_x + arrow_spacing, arrow_bottom);
        // Draw left arrow
        hud_draw_arrow_left(x_pos, arrow_top, x_pos + arrow_spacing, arrow_bottom);
    }

    // Draw the value text itself
    hud_item_draw_text(left_x, y_pos, 1.0f, viewport, item, event);

    // Reset color state if alpha fade was used
    if ((*(unsigned int*)(item + 0x28) & 0x4000000) != 0) {
        text_set_color_rgb(0.0f, 0.0f, 0.0f);
    }
}

// ============================================================================
// Address: 0x80048ca0-0x800497cc | Batch 089 | Date: 2026-08-10
// HUD item rendering: selection highlight, text display, slider, checkbox,
// render dispatch, and popup handlers.
// ============================================================================

// Address: 0x80048ca0
// Draws selection highlight background for an item. Uses parent layout colors
// with opacity from viewport. If item is selected, applies additional tint.
void hud_item_draw_selection_highlight(int viewport, int item, float* bounds) {
    float opacity = *(float*)(viewport + 0xf0);
    int is_selected = hud_item_is_selected(item);

    float tint = 0.0f;
    if (is_selected != 0) {
        tint = *(float*)(*(int*)(viewport + 0x20) + 0x8c);
    }

    // Set gradient colors from parent layout palette
    unsigned int color1 = hud_set_sprite_color_with_tint(opacity, *(unsigned int*)(*(int*)(viewport + 0x20) + 0x84));
    hud_apply_gradient_color(tint, color1);
    unsigned int color2 = hud_set_sprite_color_with_tint(opacity, *(unsigned int*)(*(int*)(viewport + 0x20) + 0x88));
    hud_apply_gradient_color(tint, color2);

    // Draw filled gradient rectangle over item bounds
    gx_draw_gradient_rect(bounds[0], bounds[1], bounds[2], bounds[3]);
}

// Address: 0x80048d88
// Draws text display item (render mode 2). Full pipeline:
//   1. Compute alpha from viewport opacity
//   2. Get current event text
//   3. Compute text bounds (content, text area, label area)
//   4. Calculate available width based on alignment mode
//   5. Set font, apply case/scale
//   6. Draw selection highlight background
//   7. Draw fill gradient (mode depends on whether item is focused/+0x198)
//   8. Measure text width for right-alignment adjustment
//   9. Draw text with focus styling
void hud_item_draw_text_display(void) {
    int viewport;
    int item;
    int event;
    unsigned char* styled_text;
    float text_bounds[4];      // content rect
    float text_area[2];        // text start area
    float text_end;            // text end X
    float label_bounds[4];     // label rect
    float available_width;
    float x_pos;
    float y_pos;
    float text_width;

    viewport = /* register pair high */;
    item = /* register pair low */;

    // Compute alpha
    float_to_byte(BASE_ALPHA * *(float*)(viewport + 0xf0));

    event = hud_item_get_current_event(item);
    hud_item_compute_text_bounds(item, &text_bounds[0], text_area, label_bounds);

    // Calculate available width based on alignment
    if (*(int*)(item + 0xc0) == 1) {
        // Right-aligned
        available_width = -(THUMB_INSET_Y * PIXEL_SCALE_X -
                           (text_end - text_area[0]));
    } else {
        x_pos = *(float*)(item + 0xdc) * PIXEL_SCALE_X + text_area[0];
        available_width = text_end - x_pos;
    }

    text_set_font_table(*(int*)(item + 0xbc));
    styled_text = hud_text_apply_case_scale(available_width, *(unsigned char**)(event + 4),
                                            *(unsigned int*)(item + 0xb8));

    // Draw selection highlight
    hud_item_draw_selection_highlight(viewport, item, text_bounds);

    // Draw fill gradient (gradient mode based on focus state)
    hud_draw_fill_gradient(
        THUMB_INSET_Y + label_bounds[0], label_bounds[1],
        label_bounds[2] - THUMB_INSET_Y, label_bounds[3],
        (unsigned int)(*(int*)(viewport + 0x198) != item)
    );

    // Measure and adjust X for right-alignment
    text_width = text_get_char_width(styled_text);
    if (*(int*)(item + 0xc0) == 1) {
        x_pos = -(THUMB_INSET_Y * PIXEL_SCALE_X - (text_end - text_width));
    }

    // Draw text with Y offset
    y_pos = *(float*)(item + 0xe0) * PIXEL_SCALE_Y + PADDING_VERT + label_bounds[1] /*local_44*/;
    hud_item_draw_text(x_pos, y_pos, available_width, viewport, item, event);
}

// Address: 0x80048f0c
// Draws slider/simple text content (render modes 5 and 6).
// For mode 6 (value selector with slider): draws label and value gradient
// rects, formats current value into display string.
// For mode 5 (simple text): uses inline text pointer at +0x48.
// Both draw element background and text with focus colors.
void hud_item_draw_slider_content(int param_1, int param_2, float* pixel_bounds) {
    int viewport;
    int item;
    char* display_text;
    unsigned char* styled_text;
    unsigned int fg_color, shadow_color;
    float content_rect[4];
    float label_rect[4];
    float value_rect[4];
    float x_pos, y_pos;

    viewport = /* register pair high */;
    item = /* register pair low */;

    hud_item_compute_slider_bounds(item, content_rect, label_rect, value_rect);

    if (*(int*)(item + 0xb4) == 6) {
        // Value selector mode: draw label and value gradient backgrounds
        float_to_byte(BASE_ALPHA * *(float*)(viewport + 0xf0));
        hud_item_draw_selection_highlight(viewport, item, label_rect);
        hud_item_draw_selection_highlight(viewport, item, value_rect);
        hud_draw_fill_gradient(label_rect[0], label_rect[3], label_rect[2], label_rect[1], 2);
        hud_draw_fill_gradient(value_rect[0], value_rect[3], value_rect[2], value_rect[1], 3);
    }

    // Draw element background for content area
    hud_draw_element_background(viewport, content_rect);

    // Get display text
    if (*(int*)(item + 0xb4) == 6) {
        // Format value into temp buffer
        hud_item_format_value_string(
            &g_hud_temp_string_buffer, 128,
            **(int**)(item + 0x48),
            *(int*)(item + 0x9c),
            *(unsigned int*)(item + 0x94),
            *(int*)(item + 0xac),
            0  // no arrows
        );
        display_text = &g_hud_temp_string_buffer;
    } else {
        display_text = *(char**)(item + 0x48);
    }

    if (*display_text != '\0') {
        float pixel_width = *(float*)(item + 0x44) * PIXEL_SCALE_X + SCREEN_OFFSET_X;
        x_pos = *(float*)(item + 0xdc) * PIXEL_SCALE_X + content_rect[0];

        if (*(int*)(item + 0xc0) == 1) {
            // Right-aligned: use font right margin
            g_text_alignment = 2;  // DAT_80611654
            x_pos = -(*(float*)(*(int*)(viewport + 0x1c) + 0x20) * PIXEL_SCALE_X - content_rect[2]);
        }

        y_pos = *(float*)(item + 0xe0) * PIXEL_SCALE_Y + PADDING_VERT + pixel_bounds[1];

        hud_item_compute_colors(viewport, item, &fg_color, &shadow_color);
        text_set_font_table(*(int*)(item + 0x1c));
        styled_text = text_check_scale(pixel_width, display_text);

        int focused = hud_item_is_focused(item);
        if (focused == 0) {
            text_draw_char(x_pos, y_pos, styled_text, fg_color);
        } else {
            text_draw_char_shadowed(x_pos, y_pos, styled_text, fg_color, shadow_color);
        }

        g_text_alignment = 0;
    }
}

// Address: 0x80049164
// Draws a checkbox indicator: a font-height-sized square with element background.
// If is_checked is nonzero, fills the interior with a slightly inset filled rect
// at alpha-scaled opacity.
void hud_item_draw_checkbox(int param_1, int param_2, float* bounds, int is_checked) {
    int viewport;
    float checkbox_rect[4];
    float line_height;
    float padding_x, padding_y;

    viewport = /* register pair high */;
    int item = /* register pair low */;

    padding_x = PADDING_VERT * PIXEL_SCALE_X;
    padding_y = PADDING_VERT * PIXEL_SCALE_Y;

    line_height = hud_item_get_font_line_height(item);

    // Compute checkbox rect (square, font-height sized)
    checkbox_rect[0] = bounds[0] + padding_x;
    checkbox_rect[2] = (float)line_height * PIXEL_SCALE_X + checkbox_rect[0];
    checkbox_rect[1] = bounds[1] + padding_y;
    checkbox_rect[3] = (float)line_height * PIXEL_SCALE_Y + checkbox_rect[1];

    // Draw box outline/background
    hud_draw_element_background(viewport, checkbox_rect);

    // If checked, fill interior
    if (is_checked != 0) {
        float_to_byte(BASE_ALPHA * *(float*)(viewport + 0xf0));
        gx_draw_filled_rect(
            checkbox_rect[0] + CHECKBOX_INSET,
            checkbox_rect[1] + CHECKBOX_INSET,
            checkbox_rect[2] - CHECKBOX_INSET,
            checkbox_rect[3] - CHECKBOX_INSET
        );
    }
}

// Address: 0x80049260
// Draws checkbox for wrapped text items. Checked state is determined by
// comparing item's line index (+0xd0) against the parent item's current
// value (+0xc8 via +0x8c parent link). Uses CLZ trick for equality test.
void hud_item_draw_checkbox_wrapped(int param_1, int item, float* bounds) {
    // checked = (item->line_index == parent->current_value)
    unsigned int is_checked = countLeadingZeros(
        *(int*)(item + 0xd0) - *(int*)(*(int*)(item + 0x8c) + 0xc8)
    ) >> 5;  // CLZ result: 32 if equal (shifted to 1), 0-31 if not (shifted to 0)

    hud_item_draw_checkbox(param_1, item, bounds, is_checked);
}

// Address: 0x80049298
// Draws checkbox for event text items. Checked state is determined by
// testing if the value at +0xc8 equals 1.
void hud_item_draw_checkbox_event(int param_1, int item, float* bounds) {
    // checked = (value == 1), using CLZ(1 - value) >> 5
    unsigned int is_checked = countLeadingZeros(1 - *(int*)(item + 0xc8)) >> 5;
    hud_item_draw_checkbox(param_1, item, bounds, is_checked);
}

// Address: 0x800492c8
// MASSIVE: Draws complete slider track with thumb indicator and tick marks.
// Pipeline:
//   1. Get animation values (left/right track colors)
//   2. Compute alpha from viewport opacity
//   3. Get event strings (left label, right value)
//   4. Compute slider thumb bounds (track rect + thumb rect)
//   5. Draw track in two passes (foreground + shadow offset):
//      a. Left fill sprite (track start to thumb)
//      b. Right fill sprite (thumb to track end)
//      c. Center horizontal line
//      d. Optional tick marks (count from +0x9a, evenly spaced vertical lines)
//   6. Hit-test thumb against cursor; highlight if touched
//   7. Draw thumb indicator sprite
//   8. Draw left label text (if present)
//   9. Draw right value text (if present, right-aligned)
void hud_item_draw_slider_track(int param_1, int param_2, float* bounds) {
    int viewport, item;
    int left_event, right_event;
    unsigned int left_color, right_color, tick_color;
    float track_rect[4];    // full track bounds
    float thumb_rect[4];    // thumb position bounds
    float center_y;
    float tick_spacing;
    int tick_count;
    int i;

    viewport = /* register pair high */;
    item = /* register pair low */;

    left_color = hud_element_set_anim_value(viewport, 0);
    right_color = hud_element_set_anim_value(viewport, 1);
    tick_color = (float_to_byte(BASE_ALPHA * *(float*)(viewport + 0xf0)) & 0xFF) | 0x7F7F7F00;

    float label_y = SLIDER_V_PADDING * PIXEL_SCALE_Y + bounds[1];

    left_event = hud_item_get_event_at_index(item, 0);
    right_event = hud_item_get_event_at_index(item, 1);

    hud_item_compute_slider_thumb_bounds(item, track_rect, thumb_rect);

    center_y = 0.5f * (track_rect[1] + track_rect[3]);
    float tick_top = center_y + SLIDER_V_PADDING;
    float tick_bottom = center_y - SLIDER_V_PADDING;

    // Draw track in two passes (shadow + foreground)
    for (int pass = 1; pass >= 0; pass--) {
        float offset = (pass == 0) ? 0.0f : /* shadow offset from int-to-float */;
        unsigned int pass_color = (pass == 0) ? left_color : right_color;

        // Left fill (track start to center)
        gx_draw_filled_rect_color(
            track_rect[0] + offset, track_rect[1] + offset,
            track_rect[0] + offset, track_rect[3] + offset,
            pass_color);

        // Right fill (center to track end)
        gx_draw_filled_rect_color(
            track_rect[2] - 0.0f + offset, track_rect[1] + offset,
            track_rect[2] + offset, track_rect[3] + offset,
            pass_color);

        // Center horizontal line
        gx_draw_line_h(
            track_rect[0] + offset, center_y + offset,
            track_rect[2] + offset, center_y + offset,
            pass_color);

        // Tick marks (if count > 0)
        tick_count = *(short*)(item + 0x9a);
        if (tick_count != 0) {
            tick_spacing = (track_rect[2] - track_rect[0]) / (float)(tick_count);
            float tick_x = track_rect[0] + tick_spacing;

            for (i = 0; i < tick_count; i++) {
                gx_draw_line_v(
                    (int)tick_x, (int)tick_bottom,
                    (int)tick_x, (int)tick_top,
                    tick_color);
                tick_x += (i == 0) ? 0.0f : tick_spacing;
            }
        }
    }

    // Hit-test thumb against cursor for highlight
    int cursor_on_thumb = hud_cursor_hit_test_rect(thumb_rect);
    unsigned int thumb_color = (cursor_on_thumb != 0) ? tick_color : left_color;

    // Draw thumb sprite
    gx_draw_filled_rect_color(
        thumb_rect[0], thumb_rect[1], thumb_rect[2], thumb_rect[3], thumb_color);

    // Draw label text (left side)
    text_set_font_table(*(int*)(item + 0xbc));
    if (*(int*)(left_event + 4) != 0) {
        hud_item_draw_text(bounds[0], label_y, 1.0f, viewport, item, left_event);
    }

    // Draw value text (right side, right-aligned)
    if (*(char**)(right_event + 4) != NULL) {
        unsigned char* styled = hud_text_apply_case_scale(
            1.0f, *(unsigned char**)(right_event + 4), *(unsigned int*)(item + 0xb8));
        float text_w = text_get_char_width(styled);
        hud_item_draw_text(bounds[2] - text_w, label_y, 1.0f, viewport, item, right_event);
    }
}

// Address: 0x800496a0
// Master item render dispatch. Gets pixel bounds, sets up sprite rendering,
// then switches on render mode (+0xb4):
//   1 = value selector (left/right arrows + value text)
//   2 = text display (background + aligned text)
//   4 = checkbox with event value
//   5/6 = slider content (inline text or formatted value)
//   7 = full slider track with thumb and tick marks
void hud_item_render_dispatch(int param_1, int item) {
    float pixel_bounds[4];

    hud_item_get_pixel_bounds(item, pixel_bounds);
    gx_sprite_set_blend_mode(1);
    gx_sprite_set_filter_mode(0);

    int render_mode = *(int*)(item + 0xb4);

    if (render_mode == 4) {
        hud_item_draw_checkbox_event(param_1, item, pixel_bounds);
    } else if (render_mode == 2) {
        hud_item_draw_text_display();
    } else if (render_mode == 1) {
        hud_item_draw_value_selector(param_1, item, pixel_bounds);
    } else if (render_mode == 7) {
        hud_item_draw_slider_track(param_1, item, pixel_bounds);
    } else if (render_mode == 5 || render_mode == 6) {
        hud_item_draw_slider_content(param_1, item, (int)pixel_bounds);
    }
}

// Address: 0x80049788
// HUD popup confirm handler. When the selected item matches the popup's
// target (+0x198), triggers a root screen find on the current viewport.
void hud_popup_on_confirm(int popup, int selected_item) {
    if (selected_item == *(int*)(popup + 0x198)) {
        int viewport_base = *(int*)(&g_hud_viewport_table + g_hud_current_viewport * 0x1E0);
        hud_screen_find_root(viewport_base);
    }
}

// Address: 0x800497cc
// HUD popup dismiss handler. Validates popup can be closed:
//   +0x188 >= -1 (not in error state)
//   +0x190 == -1 (no pending operation)
// Clears parent's popup reference (+0x198 = 0).
// If a selection was made (+0x188 >= 0), triggers value change callback.
// Then pops the popup screen.
void hud_popup_on_dismiss(int popup) {
    if ((*(int*)(popup + 0x188) == -1 || *(int*)(popup + 0x188) >= 0) &&
        *(int*)(popup + 0x190) == -1) {
        // Clear parent's popup pointer
        *(int*)(*(int*)(popup + 0x2c) + 0x198) = 0;

        // Trigger value change if selection was made
        if (*(int*)(popup + 0x188) >= 0) {
            hud_item_on_value_change();
        }

        hud_screen_pop(popup);
    }
}

// ============================================================================
// Batch 107 - HUD Item Type Creators & Measurement Helpers
// Address range: 0x80047bc0 - 0x80048070
// Date: 2026-08-10
// ============================================================================

// Address: 0x80047bc0 | Name: hud_item_add_header
// Creates a header-style HUD item (type=2). Sets flags 0x100800 (non-selectable + divider).
// Used by menu builders to create section headers.
void hud_item_add_header(float height, u32 flags) {
    hud_item_configure_layout(height, g_hud_current_item, 2, flags);
    g_hud_current_item->flags |= 0x100800;
}

// Address: 0x80047c00 | Name: hud_item_add_footer
// Creates a footer-style HUD item (type=3). Simple layout-only item used to close sections.
void hud_item_add_footer(float height, u32 flags) {
    hud_item_configure_layout(height, g_hud_current_item, 3, flags);
}

// Address: 0x80047c2c | Name: hud_item_add_text_display
// Creates a text display HUD item (type=4). Measures font height and adjusts layout
// when parent exists. Allocates two event strings for label/value display.
void hud_item_add_text_display(float height, u32 flags) {
    float font_height;

    hud_item_configure_layout(height, g_hud_current_item, 4, flags);
    font_height = hud_item_get_font_line_height(g_hud_current_item);

    if (g_hud_current_item->parent != NULL) {
        g_hud_current_item->content_width -= font_height;
    }

    hud_event_add_string(0);
    hud_event_add_string(0);
}

// Address: 0x80047c8c | Name: hud_item_add_slider
// Creates a slider HUD item (type=5). Configures min/max/step range values.
// Sets flag 0x100000 (has value). Slider range stored at +0x98/+0xa4/+0x94/+0xa8.
void hud_item_add_slider(u32 sort_key, float step, u16 string_id,
                         u32 initial_value, u32 max_value) {
    int item_value;

    item_value = hud_style_get_sort_key();  // FUN_803826ec
    hud_item_configure_layout(/* height from reg */, g_hud_current_item, 5, /* flags from reg */);
    g_hud_current_item->linked_value = item_value;
    g_hud_current_item->range_min = string_id;
    g_hud_current_item->range_step = step;
    g_hud_current_item->range_max_display = max_value;
    g_hud_current_item->range_max = initial_value;
    g_hud_current_item->flags |= 0x100000;
    hud_style_finalize_slider();  // FUN_80382738
}

// Address: 0x80047d20 | Name: hud_item_add_value_selector
// Creates a value selector HUD item (type=6). Configures range min/max, wrap mode,
// initial value, and display callback. Sets flags 0x1300000 (has value + displays value + custom).
// Shows current value text immediately.
void hud_item_add_value_selector(u32 sort_key, u32 flags, u32 wrap_mode,
                                 u32 range_min, u32 range_max, u32 display_cb) {
    int item_value;

    item_value = hud_style_get_sort_key_alt();  // FUN_803826e4
    hud_item_configure_layout(/* height */, g_hud_current_item, 6, /* flags */ | 0x200000);
    g_hud_current_item->linked_value = item_value;
    g_hud_current_item->range_max_display = display_cb;
    g_hud_current_item->wrap_mode = wrap_mode;
    g_hud_current_item->range_min_value = range_min;
    g_hud_current_item->range_max = range_max;
    g_hud_current_item->display_callback = display_cb;
    g_hud_current_item->flags |= 0x1100000;
    hud_item_set_value_display(item_value);
    hud_style_finalize_selector();  // FUN_80382730
}

// Address: 0x80047dc0 | Name: hud_item_add_labeled_pair
// Creates a labeled pair HUD item (type=7). Two-column layout with sort key,
// left label string and right value string. Flags 0x200100 (pair display + sorted).
void hud_item_add_labeled_pair(u32 sort_key, u32 flags, u32 label_string_id,
                               u32 value_string_id) {
    int item_value;

    item_value = hud_style_get_labeled_key();  // FUN_803826f0
    hud_item_set_sort_key(g_hud_current_item, item_value);
    hud_item_configure_layout(/* height */, g_hud_current_item, 7, /* flags */ | 0x200100);
    hud_event_add_string(label_string_id);
    hud_event_add_string(value_string_id);
    hud_style_finalize_pair();  // FUN_8038273c
}

// Address: 0x80047e34 | Name: hud_item_add_wrapped_text
// Creates a wrapped-text sub-item. Measures text height for word-wrapped content
// and creates a sized list item to contain it. Adjusts padding based on parent layout.
// Used for multi-line descriptions in menus.
void hud_item_add_wrapped_text(void) {
    int source_item;
    int parent_item;
    int new_item;
    float text_height;
    float prev_padding;
    u32 prev_padding_mode;

    source_item = hud_style_get_sort_key();  // FUN_803826ec
    parent_item = g_hud_current_item;

    if (g_hud_current_item->layout_table == 3) {
        // Measure text height from event string in available width
        text_height = hud_measure_text_height(
            (double)g_hud_current_item->content_width,
            g_hud_current_item->font_id,
            *(char**)(source_item + 4));

        prev_padding = *(float*)(g_hud_style_context + 0x50);
        prev_padding_mode = *(u32*)(g_hud_style_context + 0x48);

        hud_set_item_padding_mode(0.0f, 0, 1, 0);

        // Create sized list item regardless of parent scroll flag
        new_item = hud_add_list_item_sized(text_height);

        // Link back to source item for text rendering
        *(int*)(new_item + 0x8c) = parent_item;

        // Set item height = font line height + small padding
        float line_height = hud_item_get_font_line_height(new_item);
        *(float*)(new_item + 0x3c) = 0.5f + line_height;

        // Store event chain index (parent event count - 1)
        *(int*)(new_item + 0xd0) = *(int*)(*(int*)(new_item + 0x8c) + 0xc4) - 1;

        // Restore previous padding
        hud_set_item_padding_mode(-prev_padding, prev_padding_mode, 1, 0);
    }

    g_hud_current_item = parent_item;
    hud_style_finalize_slider();  // FUN_80382738
}

// Address: 0x80047f84 | Name: hud_item_calc_content_y
// Calculates the Y content position for an item. Returns parent top margin + item offset.
// If flag 0x40 (scrolled), adds scroll compensation (parent height - parent bottom margin).
float hud_item_calc_content_y(int item) {
    int parent = *(int*)(item + 0x88);
    float base_y = *(float*)(parent + 0x7c) + *(float*)(item + 0xe8);

    if ((*(u32*)(item + 0x28) & 0x40) == 0) {
        return base_y;
    }

    // Scroll compensation: add (parent_height - parent_bottom_margin)
    return base_y + (*(float*)(parent + 0x68) - *(float*)(parent + 0x80));
}

// Address: 0x80047fb4 | Name: hud_item_calc_text_height
// Calculates text item height by measuring wrapped text in available bounds.
// Accounts for alignment mode (+0xc0==1 means right-aligned), text margins,
// and adds vertical padding (top + bottom).
float hud_item_calc_text_height(int item) {
    int event;
    char* text;
    float text_height;
    float bounds[4];
    float origin[2];
    float extents;
    u8 stack_padding[16];

    event = hud_item_get_current_event(item);
    text = (event != 0) ? *(char**)(event + 4) : NULL;

    hud_item_compute_text_bounds(item, stack_padding, origin, bounds);

    float available_width;
    if (*(int*)(item + 0xc0) == 1) {
        // Right-aligned: compute from right margin
        available_width = -(HUD_TEXT_MARGIN_RIGHT * g_hud_pixel_scale_x - (extents - origin[0]));
    } else {
        // Left-aligned: compute from left margin
        available_width = extents - (*(float*)(item + 0xdc) * g_hud_pixel_scale_x + origin[0]);
    }

    text_height = hud_measure_text_height(
        (double)(available_width * g_hud_inv_pixel_scale_x),
        *(int*)(item + 0xbc),
        text);

    // Add top + bottom padding
    return HUD_TEXT_PADDING_V + (HUD_TEXT_PADDING_V + text_height);
}

// Address: 0x80048070 | Name: hud_item_calc_simple_text_height
// Calculates simple text height for items with static text at +0x48.
// Uses item width (+0x44) and font (+0xbc) to measure, adds top+bottom padding.
float hud_item_calc_simple_text_height(int item) {
    float text_height;

    text_height = hud_measure_text_height(
        (double)*(float*)(item + 0x44),
        *(int*)(item + 0xbc),
        *(char**)(item + 0x48));

    // Add top + bottom padding
    return HUD_TEXT_PADDING_V + (HUD_TEXT_PADDING_V + text_height);
}

// ============================================================================
// Batch 108 - HUD Item Measurement, Bounds & Rendering Helpers
// Address range: 0x800480ac - 0x8004879c
// Date: 2026-08-10
// ============================================================================

// Address: 0x800480ac | Name: hud_item_calc_value_selector_height
// Calculates height for value selector item (type=6). Formats current value into
// 128-char temp buffer via display callback, then measures wrapped text height.
// Returns height + 2x vertical padding.
float hud_item_calc_value_selector_height(int item) {
    float text_height;

    // Format the current display value into temp buffer
    hud_format_value_to_string(
        s_temp_value_buffer, 0x80,
        **(int**)(item + 0x48),         // linked value
        *(u32*)(item + 0x9c),           // range_min_value
        *(u32*)(item + 0x94),           // range_max_display
        *(int*)(item + 0xac),           // display_callback
        1);

    text_height = hud_measure_text_height(
        (double)*(float*)(item + 0x44),
        *(int*)(item + 0xbc),
        s_temp_value_buffer);

    return HUD_TEXT_PADDING_V + (HUD_TEXT_PADDING_V + text_height);
}

// Address: 0x8004811c | Name: hud_item_calc_labeled_pair_height
// Calculates height for labeled pair item (type=7). Checks if either event string
// (left label or right value) has text. If so, measures one line height + padding.
// Otherwise returns minimal constant height.
float hud_item_calc_labeled_pair_height(int item) {
    int event;
    float height;

    event = hud_item_get_event_at_index(item, 0);
    if ((event != 0 && *(int*)(event + 4) != 0) ||
        (event = hud_item_get_event_at_index(item, 1),
         event != 0 && *(int*)(event + 4) != 0)) {
        // Has text content - measure single line + padding
        float line_h = hud_measure_text_height(
            (double)*(float*)(item + 0x44),
            *(int*)(item + 0xbc),
            NULL);
        height = HUD_TEXT_PADDING_V + HUD_TEXT_PADDING_V + (HUD_LINE_SPACING + line_h);
    } else {
        height = HUD_LABELED_PAIR_MIN_HEIGHT;
    }

    return height;
}

// Address: 0x800481c4 | Name: hud_item_calc_height_by_mode
// Master height dispatch. Switches on item render mode (+0xb4):
//   0 = zero height
//   1 = single line (measure NULL text for line height)
//   2 = wrapped text (full word-wrap measurement)
//   3 = fixed (zero/constant)
//   4 = event text (measure event string)
//   5 = simple text (static text at +0x48)
//   6 = value selector (format + measure)
//   7 = labeled pair (dual event check)
// Called by layout engine to determine item content height.
float hud_item_calc_height_by_mode(int item) {
    int event;
    char* text;
    float height;

    if (*(int*)(item + 0xb4) == 0) {
        return 0.0f;
    }

    event = hud_item_get_current_event(item);

    switch (*(int*)(item + 0xb4)) {
        case 1:
            height = hud_measure_text_height(
                (double)*(float*)(item + 0x44),
                *(int*)(item + 0xbc), NULL);
            break;
        case 2:
            height = hud_item_calc_text_height(item);
            break;
        case 3:
            height = 0.0f;
            break;
        case 4:
            text = (event != 0) ? *(char**)(event + 4) : NULL;
            height = hud_measure_text_height(
                (double)*(float*)(item + 0x44),
                *(int*)(item + 0xbc), text);
            break;
        case 5:
            height = hud_item_calc_simple_text_height(item);
            break;
        case 6:
            height = hud_item_calc_value_selector_height(item);
            break;
        case 7:
            height = hud_item_calc_labeled_pair_height(item);
            break;
        default:
            height = 0.0f;
            break;
    }

    return height;
}

// Address: 0x80048298 | Name: hud_item_compute_text_bounds
// Computes three rects for text item rendering:
//   - Full pixel bounds (out_full)
//   - Text content area (out_text) - inset vertically by line height
//   - Remaining area below text (out_remainder)
void hud_item_compute_text_bounds(int item, float* out_remainder,
                                  float* out_text, float* out_full) {
    int sort_item;
    float pixel_bounds[4];
    float line_height_px;

    sort_item = hud_style_get_sort_key();  // FUN_803826ec
    // item is in high word of return

    line_height_px = hud_measure_text_height(
        (double)*(float*)(item + 0x44),
        *(int*)(item + 0xbc), NULL);
    line_height_px = g_hud_pixel_scale_y *
        (HUD_TEXT_PADDING_V + (HUD_TEXT_PADDING_V + line_height_px));

    hud_item_get_pixel_bounds(item, pixel_bounds);

    // Full bounds
    if (out_full != NULL) {
        out_full[0] = pixel_bounds[0];  // left
        out_full[1] = pixel_bounds[1];  // top
        out_full[2] = pixel_bounds[2];  // right
        out_full[3] = pixel_bounds[3];  // bottom
    }

    // Text rect (top portion, height = line_height_px)
    if (out_text != NULL) {
        out_text[0] = pixel_bounds[2] - line_height_px;  // left = right - height
        out_text[1] = pixel_bounds[1];                    // top
        out_text[2] = pixel_bounds[2];                    // right
        out_text[3] = pixel_bounds[1] + line_height_px;   // bottom
    }

    // Remainder rect (below text)
    if (out_remainder != NULL) {
        out_remainder[0] = pixel_bounds[0];                    // left
        out_remainder[1] = pixel_bounds[1];                    // top
        out_remainder[2] = pixel_bounds[2] - line_height_px;   // right (above text)
        out_remainder[3] = pixel_bounds[3];                    // bottom
    }

    hud_style_finalize_slider();  // FUN_80382738
}

// Address: 0x80048398 | Name: hud_item_compute_slider_bounds
// Computes slider bounds: content rect (inset from pixel bounds by H/V padding),
// left label rect, and right value rect. Mode 5 uses no horizontal padding.
void hud_item_compute_slider_bounds(int item, float* out_content,
                                    float* out_left_label, float* out_right_value) {
    float h_pad;
    float v_pad;
    int sort_item;
    float pixel_bounds[4];

    sort_item = hud_style_get_labeled_key();  // FUN_803826f0
    // item is in high word

    // Horizontal padding: mode 5 has none, others use scaled constant
    if (*(int*)(item + 0xb4) == 5) {
        h_pad = 0.0f;
    } else {
        h_pad = HUD_LABELED_PAIR_MIN_HEIGHT * g_hud_pixel_scale_x + g_hud_offset_x;
    }

    // Vertical padding
    v_pad = HUD_LABELED_PAIR_MIN_HEIGHT * g_hud_pixel_scale_y + g_hud_offset_y;

    hud_item_get_pixel_bounds(item, pixel_bounds);

    // Content rect (inset by h_pad on left/right)
    if (out_content != NULL) {
        out_content[0] = pixel_bounds[0] + h_pad;   // left
        out_content[1] = pixel_bounds[1];           // top
        out_content[2] = pixel_bounds[2] - h_pad;   // right
        out_content[3] = pixel_bounds[3];           // bottom
    }

    // Left label rect
    if (out_left_label != NULL) {
        out_left_label[0] = pixel_bounds[0];
        out_left_label[1] = pixel_bounds[1];
        out_left_label[2] = HUD_SLIDER_LABEL_WIDTH * h_pad + pixel_bounds[0];
        out_left_label[3] = pixel_bounds[1] + v_pad;
    }

    // Right value rect
    if (out_right_value != NULL) {
        out_right_value[0] = pixel_bounds[2] - HUD_SLIDER_LABEL_WIDTH * h_pad;
        out_right_value[1] = pixel_bounds[1];
        out_right_value[2] = pixel_bounds[2];
        out_right_value[3] = pixel_bounds[1] + v_pad;
    }

    hud_style_finalize_pair();  // FUN_8038273c
}

// Address: 0x800484c4 | Name: hud_item_compute_slider_thumb_bounds
// Computes slider thumb position from current value. Gets pixel bounds, insets for
// track area, then calculates thumb center X from value / range divisor.
// Returns track rect (out_track) and thumb rect (out_thumb).
void hud_item_compute_slider_thumb_bounds(int item, float* out_track, float* out_thumb) {
    float pixel_bounds[4];
    float thumb_half_width;
    float track_left, track_right;
    float thumb_center_x;
    int sort_item;

    sort_item = hud_style_get_labeled_key();  // FUN_803826f0
    // item is in high word

    hud_item_get_pixel_bounds(item, pixel_bounds);

    // Apply vertical inset for track top
    pixel_bounds[1] = HUD_TEXT_PADDING_V * g_hud_pixel_scale_y + pixel_bounds[1];
    // Horizontal inset for track edges
    pixel_bounds[0] = 0.5f + pixel_bounds[0];
    pixel_bounds[2] = pixel_bounds[2] - 0.5f;

    float track_bottom = HUD_LINE_SPACING * g_hud_pixel_scale_y + pixel_bounds[1];

    // Track rect
    if (out_track != NULL) {
        out_track[0] = pixel_bounds[0];
        out_track[1] = pixel_bounds[1];
        out_track[2] = pixel_bounds[2];
        out_track[3] = track_bottom;
    }

    // Thumb rect (positioned by value)
    if (out_thumb != NULL) {
        thumb_half_width = HUD_TEXT_PADDING_V *
            (HUD_SLIDER_THUMB_SCALE * g_hud_pixel_scale_x + g_hud_offset_x);

        track_left = pixel_bounds[0] + thumb_half_width;
        track_right = pixel_bounds[2] - thumb_half_width;

        // Current value normalized to 0..1
        float normalized = (float)(*(int*)(item + 0xc8)) / HUD_SLIDER_RANGE_DIVISOR;
        thumb_center_x = normalized * (track_right - track_left) + track_left;

        out_thumb[0] = thumb_center_x - thumb_half_width;
        out_thumb[1] = pixel_bounds[1];
        out_thumb[2] = thumb_center_x + thumb_half_width;
        out_thumb[3] = track_bottom;
    }

    hud_style_finalize_pair();  // FUN_8038273c
}

// Address: 0x800485b8 | Name: hud_item_get_pixel_bounds
// Converts normalized bounds to pixel coordinates using screen scale/offset globals.
void hud_item_get_pixel_bounds(int item, float* out_bounds) {
    float norm_bounds[4];

    hud_item_compute_normalized_bounds(item, norm_bounds);

    out_bounds[0] = norm_bounds[0] * g_hud_pixel_scale_x + g_hud_offset_x;
    out_bounds[1] = norm_bounds[1] * g_hud_pixel_scale_y + g_hud_offset_y;
    out_bounds[2] = norm_bounds[2] * g_hud_pixel_scale_x + g_hud_offset_x;
    out_bounds[3] = norm_bounds[3] * g_hud_pixel_scale_y + g_hud_offset_y;
}

// Address: 0x80048638 | Name: hud_item_compute_normalized_bounds
// Computes normalized screen-space bounds for an item. Types 6/7 (value selector,
// labeled pair) use parent layout width at +0x210 with left/right alignment flags
// (0x800 = left-aligned, 0x400 = right-aligned). Calls hud_item_calc_content_y for Y.
void hud_item_compute_normalized_bounds(int item, float* out_bounds) {
    int parent = *(int*)(item + 0x88);

    // X bounds: parent offset + item local X
    out_bounds[0] = g_hud_inv_pixel_scale_x * (*(float*)(parent + 0xa4) - g_hud_offset_x)
                    + *(float*)(item + 0xe4);
    out_bounds[2] = out_bounds[0] + *(float*)(item + 0xec);

    float right_margin = HUD_NORM_RIGHT_MARGIN;
    float left_margin = HUD_NORM_LEFT_MARGIN;

    // Types 6/7 have special alignment using parent layout width
    if (*(int*)(item + 0xc0) == 6 || *(int*)(item + 0xc0) == 7) {
        if ((*(u32*)(parent + 0x40) & 0x800) != 0) {
            // Left-aligned: right edge = parent layout width - margin
            out_bounds[2] = *(float*)(parent + 0x210) - HUD_MARGIN_INSET;
            out_bounds[0] = left_margin;
        } else if ((*(u32*)(parent + 0x40) & 0x400) != 0) {
            // Right-aligned: left edge = margin + parent layout width
            out_bounds[0] = HUD_MARGIN_INSET + *(float*)(parent + 0x210);
            out_bounds[2] = right_margin;
        }
    }

    // Y bounds: scroll-aware content Y + item height
    out_bounds[1] = hud_item_calc_content_y(item);
    out_bounds[3] = out_bounds[1] + *(float*)(item + 0xf0);
}

// Address: 0x80048714 | Name: hud_item_set_clip_rect
// Sets clip/scissor rect for item rendering. Types 6/7 compute pixel bounds and
// set custom GX scissor. Other types delegate to standard hud_item_set_scissor.
void hud_item_set_clip_rect(int viewport, int item) {
    float pixel_bounds[4];

    if (*(int*)(item + 0xc0) == 6 || *(int*)(item + 0xc0) == 7) {
        hud_item_get_pixel_bounds(item, pixel_bounds);
        // Set GX scissor directly from pixel bounds
        gx_set_scissor(
            (int)pixel_bounds[0], (int)pixel_bounds[1],
            (int)pixel_bounds[2], (int)pixel_bounds[3]);
    } else {
        hud_item_set_scissor(viewport, item);
    }
}

// Address: 0x8004879c | Name: hud_item_get_palette_color
// Returns color from item palette based on flag bits.
//   Flag 0x100 = highlight color (offset +0x58 in font data)
//   Flag 0x200 = secondary color (offset +0x50)
//   Default = primary color (offset +0x54)
u32 hud_item_get_palette_color(int font_data, u32 flags) {
    if ((flags & 0x100) != 0) {
        return *(u32*)(*(int*)(font_data + 0x1c) + 0x58);  // highlight
    }
    if ((flags & 0x200) != 0) {
        return *(u32*)(*(int*)(font_data + 0x1c) + 0x50);  // secondary
    }
    return *(u32*)(*(int*)(font_data + 0x1c) + 0x54);      // primary
}

// ============================================================================
// Batch 109 - HUD Item Rendering Functions
// Address range: 0x800487d0 - 0x800492c8
// Date: 2026-08-10
// ============================================================================

// Address: 0x800487d0 | Name: hud_item_draw_text
// MASSIVE: Draws item text with full styling pipeline. Sets font, computes colors
// (fg+shadow), applies dim/focus/opacity-pulse/glow effects, transforms case/scale,
// handles alpha fade animation (flag 0x4000000), draws shadowed text when focused.
void hud_item_draw_text(float x, float y, float width, int viewport, int item, int event) {
    int sort_ctx;
    int font_item;
    u32 fg_color;
    u32 shadow_color;
    byte* text;

    sort_ctx = hud_style_get_sort_key();  // returns viewport/item pair
    font_item = (int)sort_ctx;  // low word

    text_set_font_table(*(int*)(font_item + 0xbc));
    hud_item_compute_colors(viewport, font_item, &fg_color, &shadow_color);

    int is_dim = hud_element_needs_dim(viewport);
    if (is_dim == 0) {
        // Not dimmed - check for opacity pulse on unfocused items
        int focused = hud_item_is_focused(font_item);
        if (focused == 0 && (*(u32*)(event + 0xc) & 8) != 0) {
            u32 pulse_alpha = calc_alpha_pulse(
                HUD_PULSE_SCALE * *(float*)(viewport + 0xf0));
            fg_color = (fg_color & 0xffffff00) | pulse_alpha;
        }

        // Glow effect (flag 0x10 at +0xb8)
        if ((*(u32*)(font_item + 0xb8) & 0x10) != 0) {
            float t = *(float*)(font_item + 0x104);  // animation time
            u32 r = calc_alpha_pulse(-(HUD_GLOW_R_SCALE * t - HUD_GLOW_BASE));
            u32 g = calc_alpha_pulse(-(HUD_GLOW_G_SCALE * t - HUD_GLOW_BASE));
            u32 a = calc_alpha_pulse(HUD_GLOW_BASE * *(float*)(viewport + 0xf0));
            fg_color = (a & 0xff) | (r & 0xff) << 8 | (g << 24) | (r & 0xff) << 16;
        }
    }

    char* raw_text = *(char**)(event + 4);
    if (raw_text != NULL) {
        byte* processed;
        if ((*(u32*)(font_item + 0x28) & 0x4000000) == 0) {
            // Normal: apply case transform + scale check
            raw_text = hud_text_apply_case_scale(0.0f, raw_text, *(u32*)(font_item + 0xb8));
            processed = text_check_scale(width, raw_text);
        } else {
            // Alpha fade animation mode
            raw_text = hud_text_apply_case_scale(0.0f, raw_text, *(u32*)(font_item + 0xb8));
            text_apply_alpha_fade(width, raw_text, 0, 1);
            processed = (byte*)raw_text;
        }

        // Draw with or without shadow based on focus
        int is_focused = hud_item_is_focused(font_item);
        if (is_focused == 0) {
            text_draw_char(x, y, processed, fg_color);
        } else {
            text_draw_char_shadowed(x, y, processed, fg_color, shadow_color);
        }

        // Reset color after alpha fade mode
        if ((*(u32*)(font_item + 0x28) & 0x4000000) != 0) {
            text_set_color_rgb(1.0f, 1.0f, 1.0f);
        }
    }

    hud_style_finalize_slider();  // FUN_80382738
}

// Address: 0x800489d0 | Name: hud_item_draw_value_selector
// Draws value selector text with left/right navigation arrows. Determines arrow state
// (none/normal/pressed/disabled) from focus+nav flags. Handles 3 alignment modes
// (left/right/center). Draws arrow sprites unless flag 0x400000 disables them.
void hud_item_draw_value_selector(int viewport, int item, float* bounds) {
    int sort_ctx;
    int font_item;
    int left_arrow_state, right_arrow_state;
    int event;
    byte* text;
    float arrow_size;

    sort_ctx = hud_style_get_value_key();  // FUN_803826dc

    float opacity = *(float*)(viewport + 0xf0);
    int is_dim = hud_element_needs_dim(viewport);
    left_arrow_state = 0;
    right_arrow_state = 0;

    event = hud_item_get_current_event(font_item);
    text_set_font_table(*(int*)(font_item + 0xbc));
    text = hud_text_apply_case_scale(0.0f, *(char**)(event + 4), *(u32*)(font_item + 0xb8));

    arrow_size = FONT_LINE_HEIGHT - HUD_LINE_SPACING;

    if (is_dim == 0) {
        int focused = hud_item_is_focused(font_item);
        if (focused != 0) {
            // Focused: check nav direction flags for arrow press state
            right_arrow_state = ((*(u32*)(viewport + 0xc4) >> 2) & 1) + 1;
            left_arrow_state = ((*(u32*)(viewport + 0xc4) & 8) == 0) ? 1 : 2;
        }
    } else if (is_dim != 0 || (*(u32*)(font_item + 0x28) & 1) != 0) {
        left_arrow_state = 3;   // disabled
        right_arrow_state = 3;  // disabled
    }

    // Set arrow colors based on state
    u32 left_color = select_list_get_weight(font_item, left_arrow_state);
    sprite_set_color(opacity, left_color);
    u32 right_color = select_list_get_weight(font_item, right_arrow_state);
    sprite_set_color(opacity, right_color);

    // Compute text position based on alignment mode
    float text_x, text_y;
    float text_width = text_get_char_width(text);
    int align_mode = *(int*)(font_item + 0xc0);

    // ... alignment calculations for left(0)/right(1,4)/center(2) ...

    // Draw arrows (unless disabled by flag)
    if ((*(u32*)(font_item + 0xb8) & 0x400000) == 0) {
        sprite_draw_arrow_right(right_x, top_y, right_x + arrow_size, bot_y);
        sprite_draw_arrow_left(left_x, top_y, left_x + arrow_size, bot_y);
    }

    // Draw the value text
    hud_item_draw_text(text_x, text_y, 0.0f, viewport, font_item, event);

    // Reset color after alpha fade
    if ((*(u32*)(font_item + 0x28) & 0x4000000) != 0) {
        text_set_color_rgb(1.0f, 1.0f, 1.0f);
    }

    hud_style_finalize_value();  // FUN_80382728
}

// Address: 0x80048ca0 | Name: hud_item_draw_selection_highlight
// Draws selection highlight background for an item. Uses parent layout colors
// (+0x84/+0x88) with opacity from viewport. If item is selected, applies additional
// tint from +0x8c. Draws filled gradient rect over item bounds.
void hud_item_draw_selection_highlight(int viewport, int item, float* bounds) {
    float opacity = *(float*)(viewport + 0xf0);

    int selected = hud_item_is_selected(item);
    float tint = (selected != 0)
        ? *(float*)(*(int*)(viewport + 0x20) + 0x8c)
        : 1.0f;

    // Apply opacity to top gradient color
    u32 top_color = sprite_apply_alpha(opacity, *(u32*)(*(int*)(viewport + 0x20) + 0x84));
    top_color = sprite_apply_tint(tint, top_color);

    // Apply opacity to bottom gradient color
    u32 bot_color = sprite_apply_alpha(opacity, *(u32*)(*(int*)(viewport + 0x20) + 0x88));
    bot_color = sprite_apply_tint(tint, bot_color);

    // Draw gradient fill
    draw_gradient_quad(bounds[0], bounds[1], bounds[2], bounds[3]);
}

// Address: 0x80048d88 | Name: hud_item_draw_text_display
// Draws text display item (render mode 2). Computes text bounds, draws selection
// highlight background, renders fill gradient, measures text width for alignment
// (mode 1 = right-aligned), draws text with focus styling.
void hud_item_draw_text_display(void) {
    int viewport, item, event;
    float pixel_bounds[4];
    float text_bounds[4];
    float full_bounds[4];
    float available_width;

    // Get context from style system
    int ctx = hud_style_get_text_key();  // FUN_803826e8
    viewport = (int)((u64)ctx >> 32);
    item = (int)ctx;

    calc_alpha_pulse(HUD_OPACITY_BASE * *(float*)(viewport + 0xf0));

    event = hud_item_get_current_event(item);
    hud_item_compute_text_bounds(item, &text_bounds[0], &full_bounds[0], &pixel_bounds[0]);

    // Compute available width based on alignment
    if (*(int*)(item + 0xc0) == 1) {
        // Right-aligned
        available_width = -(HUD_MARGIN_INSET * g_hud_pixel_scale_x
                           - (full_bounds[2] - full_bounds[0]));
    } else {
        available_width = full_bounds[2]
                         - (*(float*)(item + 0xdc) * g_hud_pixel_scale_x + full_bounds[0]);
    }

    text_set_font_table(*(int*)(item + 0xbc));
    byte* text = hud_text_apply_case_scale(
        available_width, *(char**)(event + 4), *(u32*)(item + 0xb8));

    // Draw selection highlight over text bounds
    hud_item_draw_selection_highlight(viewport, item, text_bounds);

    // Draw fill gradient over full bounds (with insets)
    hud_draw_fill_gradient(
        HUD_MARGIN_INSET + pixel_bounds[0], pixel_bounds[1],
        pixel_bounds[2] - HUD_MARGIN_INSET, pixel_bounds[3],
        (u32)(*(int*)(viewport + 0x198) != item));

    // Measure text for positioning
    float text_width = text_get_char_width(text);
    float draw_x;
    if (*(int*)(item + 0xc0) == 1) {
        draw_x = -(HUD_MARGIN_INSET * g_hud_pixel_scale_x - (full_bounds[2] - text_width));
    } else {
        draw_x = *(float*)(item + 0xdc) * g_hud_pixel_scale_x + full_bounds[0];
    }

    float draw_y = *(float*)(item + 0xe0) * g_hud_pixel_scale_y
                   + HUD_TEXT_PADDING_V + text_bounds[3];

    hud_item_draw_text(draw_x, draw_y, available_width, viewport, item, event);

    hud_style_finalize_text();  // FUN_80382734
}

// Address: 0x80048f0c | Name: hud_item_draw_slider_content
// Draws slider/simple text content (render modes 5/6). Computes slider bounds,
// draws label+value gradient rects for mode 6, draws element background,
// formats value string (mode 6) or uses inline text (mode 5), renders with focus colors.
void hud_item_draw_slider_content(int viewport, int item, int bounds_ptr) {
    int font_item;
    float content_bounds[4];
    float left_label[4];
    float right_value[4];
    u32 fg_color, shadow_color;
    char* text;

    int ctx = hud_style_get_sort_key();
    viewport = (int)((u64)ctx >> 32);
    font_item = (int)ctx;

    hud_item_compute_slider_bounds(font_item, content_bounds, left_label, right_value);

    if (*(int*)(font_item + 0xb4) == 6) {
        // Mode 6: value selector with labels
        calc_alpha_pulse(HUD_OPACITY_BASE * *(float*)(viewport + 0xf0));
        hud_item_draw_selection_highlight(viewport, font_item, left_label);
        hud_item_draw_selection_highlight(viewport, font_item, right_value);
        hud_draw_fill_gradient(left_label[0], left_label[3], left_label[2], left_label[1], 2);
        hud_draw_fill_gradient(right_value[0], right_value[3], right_value[2], right_value[1], 3);
    }

    hud_draw_element_background(viewport, content_bounds);

    // Get text: mode 6 formats from value, mode 5 uses static text
    if (*(int*)(font_item + 0xb4) == 6) {
        hud_format_value_to_string(
            s_temp_value_buffer, 0x80,
            **(int**)(font_item + 0x48),
            *(u32*)(font_item + 0x9c),
            *(u32*)(font_item + 0x94),
            *(int*)(font_item + 0xac), 0);
        text = s_temp_value_buffer;
    } else {
        text = *(char**)(font_item + 0x48);
    }

    if (*text != '\0') {
        float text_w = *(float*)(font_item + 0x44) * g_hud_pixel_scale_x + g_hud_offset_x;
        float text_x = *(float*)(font_item + 0xdc) * g_hud_pixel_scale_x + content_bounds[0];

        if (*(int*)(font_item + 0xc0) == 1) {
            // Right-aligned: set global justify mode
            g_text_justify_mode = 2;
            text_x = -(*(float*)(*(int*)(viewport + 0x1c) + 0x20) * g_hud_pixel_scale_x
                      - content_bounds[2]);
        }

        float text_y = *(float*)(font_item + 0xe0) * g_hud_pixel_scale_y
                       + HUD_TEXT_PADDING_V + *(float*)(bounds_ptr + 4);

        hud_item_compute_colors(viewport, font_item, &fg_color, &shadow_color);
        text_set_font_table(*(int*)(font_item + 0x1c));
        byte* scaled = text_check_scale(text_w, text);

        int focused = hud_item_is_focused(font_item);
        if (focused == 0) {
            text_draw_char(text_x, text_y, scaled, fg_color);
        } else {
            text_draw_char_shadowed(text_x, text_y, scaled, fg_color, shadow_color);
        }

        g_text_justify_mode = 0;
    }

    hud_style_finalize_slider();  // FUN_80382738
}

// Address: 0x80049130 | Name: hud_item_get_font_line_height
// Returns the font line height for an item's font descriptor, scaled by screen factor.
float hud_item_get_font_line_height(int item) {
    text_set_font_table(*(int*)(item + 0x1c));
    return FONT_LINE_HEIGHT * g_hud_norm_scale - 0.0f;
}

// Address: 0x80049164 | Name: hud_item_draw_checkbox
// Draws checkbox indicator. Renders a font-height-sized box with element background.
// If checked (param_4 != 0), fills interior with alpha-scaled color at slight inset.
void hud_item_draw_checkbox(int viewport, int item, float* bounds, int checked) {
    int sort_ctx;
    float box_bounds[4];

    sort_ctx = hud_style_get_labeled_key();  // FUN_803826f0
    int vp = (int)((u64)sort_ctx >> 32);

    float h_pad = HUD_TEXT_PADDING_V * g_hud_pixel_scale_x;
    float v_pad = HUD_TEXT_PADDING_V * g_hud_pixel_scale_y;

    float line_h = hud_item_get_font_line_height((int)sort_ctx);

    box_bounds[0] = bounds[0] + h_pad;
    box_bounds[2] = line_h * g_hud_pixel_scale_x + box_bounds[0];
    box_bounds[1] = bounds[1] + v_pad;
    box_bounds[3] = line_h * g_hud_pixel_scale_y + box_bounds[1];

    hud_draw_element_background(vp, box_bounds);

    if (checked != 0) {
        // Fill interior with alpha-scaled color
        calc_alpha_pulse(HUD_OPACITY_BASE * *(float*)(vp + 0xf0));
        draw_filled_rect(
            box_bounds[0] + HUD_LABELED_PAIR_MIN_HEIGHT,
            box_bounds[1] + HUD_LABELED_PAIR_MIN_HEIGHT,
            box_bounds[2] - HUD_LABELED_PAIR_MIN_HEIGHT,
            box_bounds[3] - HUD_LABELED_PAIR_MIN_HEIGHT);
    }

    hud_style_finalize_pair();  // FUN_8038273c
}

// Address: 0x80049260 | Name: hud_item_draw_checkbox_wrapped
// Draws checkbox for wrapped text items. Checked state = item line index (+0xd0)
// matches parent's current value (+0xc8). Uses countLeadingZeros as equality test.
void hud_item_draw_checkbox_wrapped(int viewport, int item, float* bounds) {
    u32 diff = *(int*)(item + 0xd0) - *(int*)(*(int*)(item + 0x8c) + 0xc8);
    u32 is_checked = countLeadingZeros(diff) >> 5;  // 1 if diff==0, else 0
    hud_item_draw_checkbox(viewport, item, bounds, is_checked);
}

// Address: 0x80049298 | Name: hud_item_draw_checkbox_event
// Draws checkbox for event text items. Checked state = value at +0xc8 equals 1.
void hud_item_draw_checkbox_event(int viewport, int item, float* bounds) {
    u32 diff = 1 - *(int*)(item + 0xc8);
    u32 is_checked = countLeadingZeros(diff) >> 5;  // 1 if value==1, else 0
    hud_item_draw_checkbox(viewport, item, bounds, is_checked);
}

// Address: 0x800492c8 | Name: hud_item_draw_slider_track
// MASSIVE: Draws slider track with thumb, tick marks, and interactive cursor highlighting.
// Draws track background (left/right fill sprites), center line, optional tick marks
// (count from +0x9a, evenly spaced), thumb indicator (with cursor hit-test for highlight),
// and left/right label text.
void hud_item_draw_slider_track(int viewport, int item, float* bounds) {
    int sort_ctx;
    int font_item;
    float track_bounds[4];
    float thumb_bounds[4];
    u32 left_anim, right_anim;
    u32 track_color;

    sort_ctx = hud_style_get_full_key();  // FUN_803826c4
    int vp = (int)((u64)sort_ctx >> 32);
    font_item = (int)sort_ctx;

    // Get animation values for left/right track halves
    left_anim = hud_element_set_anim_value(vp, 0);
    right_anim = hud_element_set_anim_value(vp, 1);

    // Compute track color with opacity
    track_color = calc_alpha_pulse(HUD_OPACITY_BASE * *(float*)(vp + 0xf0));
    track_color = (track_color & 0xff) | 0x7f7f7f00;

    float label_y = HUD_LABELED_PAIR_MIN_HEIGHT * g_hud_pixel_scale_y + bounds[1];

    // Get left and right label events
    int left_event = hud_item_get_event_at_index(font_item, 0);
    int right_event = hud_item_get_event_at_index(font_item, 1);

    // Compute thumb position
    hud_item_compute_slider_thumb_bounds(font_item, track_bounds, thumb_bounds);

    // Draw track in 2 passes (shadow + main)
    float center_y = HUD_TEXT_PADDING_V * (track_bounds[1] + track_bounds[3]);

    for (int pass = 1; pass >= 0; pass--) {
        float offset = (float)pass;  // shadow offset
        u32 color = (pass == 0) ? left_anim : right_anim;

        // Left track fill
        draw_sprite_rect(
            track_bounds[0] + offset, track_bounds[1] + offset,
            track_bounds[0] + 1.0f + offset, track_bounds[3] + offset, color);

        // Right track fill
        draw_sprite_rect(
            track_bounds[2] - 1.0f + offset, track_bounds[1] + offset,
            track_bounds[2] + offset, track_bounds[3] + offset, color);

        // Center line
        draw_line(track_bounds[0] + offset, center_y + offset,
                  track_bounds[2] + offset, center_y + offset, color);

        // Tick marks (if tick count > 0)
        if (*(s16*)(font_item + 0x9a) != 0) {
            int tick_count = *(s16*)(font_item + 0x9a);
            float tick_spacing = (track_bounds[2] - track_bounds[0]) / (float)(tick_count);
            float tick_x = track_bounds[0] + tick_spacing;

            for (int i = 0; i < tick_count; i++) {
                draw_line((int)tick_x, (int)(center_y - HUD_LABELED_PAIR_MIN_HEIGHT),
                          (int)tick_x, (int)(center_y + HUD_LABELED_PAIR_MIN_HEIGHT),
                          track_color);
                tick_x += (i == 0) ? 0.0f : tick_spacing;
            }
        }
    }

    // Draw thumb with cursor highlight
    int cursor_over = hud_cursor_hit_test_rect(thumb_bounds);
    u32 thumb_color = (cursor_over != 0) ? track_color : left_anim;

    draw_sprite_rect(
        thumb_bounds[0], thumb_bounds[1],
        thumb_bounds[2], thumb_bounds[3], thumb_color);

    // Draw left/right labels
    text_set_font_table(*(int*)(font_item + 0xbc));

    if (*(int*)(left_event + 4) != 0) {
        hud_item_draw_text(bounds[0], label_y, 0.0f, vp, font_item, left_event);
    }

    if (*(char**)(right_event + 4) != NULL) {
        byte* right_text = hud_text_apply_case_scale(
            0.0f, *(char**)(right_event + 4), *(u32*)(font_item + 0xb8));
        float right_w = text_get_char_width(right_text);
        hud_item_draw_text(bounds[2] - right_w, label_y, 0.0f, vp, font_item, right_event);
    }

    hud_style_finalize_full();  // FUN_80382710
}

// ============================================================================
// Batch 110 - HUD Render Dispatch + Popup System + Value Formatting
// Address range: 0x800496a0 - 0x80049ef0
// Date: 2026-08-10
// ============================================================================

// Address: 0x800496a0 | Name: hud_item_render_dispatch
// Master item render dispatch. Gets pixel bounds then switches on render mode (+0xb4):
//   1 = value selector (arrows + text)
//   2 = text display (highlight + gradient + text)
//   4 = checkbox event
//   5/6 = slider content (label/value)
//   7 = slider track (thumb + ticks)
void hud_item_render_dispatch(int viewport, int item) {
    float pixel_bounds[4];

    hud_item_get_pixel_bounds(item, pixel_bounds);
    gx_set_blend_mode(1);   // FUN_8016f00c
    gx_set_cull_mode(0);    // FUN_8016e8ac

    int mode = *(int*)(item + 0xb4);

    if (mode == 4) {
        hud_item_draw_checkbox_event(viewport, item, pixel_bounds);
    } else if (mode == 2) {
        hud_item_draw_text_display();
    } else if (mode == 1) {
        hud_item_draw_value_selector(viewport, item, pixel_bounds);
    } else if (mode == 7) {
        hud_item_draw_slider_track(viewport, item, pixel_bounds);
    } else if (mode == 5 || mode == 6) {
        hud_item_draw_slider_content(viewport, item, (int)pixel_bounds);
    }
}

// Address: 0x80049788 | Name: hud_popup_on_confirm
// Popup confirm handler. When the selected item matches the popup's target item (+0x198),
// triggers a root screen find on the current viewport's base screen.
void hud_popup_on_confirm(int viewport, int item) {
    if (item == *(int*)(viewport + 0x198)) {
        hud_screen_find_root(*(int*)(&g_viewport_table + g_active_viewport * 0x1e0));
    }
}

// Address: 0x800497cc | Name: hud_popup_on_dismiss
// Popup dismiss handler. Validates popup state (+0x188 != pending, +0x190 == idle),
// clears parent's popup reference (+0x198=0), triggers value change callback if
// selection was made, then pops the popup screen.
void hud_popup_on_dismiss(int popup) {
    if ((*(int*)(popup + 0x188) == -1 || *(int*)(popup + 0x188) >= 0) &&
        *(int*)(popup + 0x190) == -1) {
        // Clear parent's popup reference
        *(int*)(*(int*)(popup + 0x2c) + 0x198) = 0;

        // If a selection was made (>= 0), notify value change
        if (*(int*)(popup + 0x188) >= 0) {
            hud_item_on_value_change();
        }

        hud_screen_pop(popup);
    }
}

// Address: 0x80049838 | Name: hud_popup_on_cancel
// Simple popup cancel handler. Clears parent's popup reference and pops the popup screen.
void hud_popup_on_cancel(int popup) {
    *(int*)(*(int*)(popup + 0x2c) + 0x198) = 0;
    hud_screen_pop(popup);
}

// Address: 0x80049864 | Name: hud_item_measure_max_event_width
// Measures maximum text width across all event strings in an item. Iterates event chain
// (count at +0xc4), measures each string at popup scale, returns the widest.
// Used for dropdown popup auto-sizing.
float hud_item_measure_max_event_width(int viewport, int item) {
    float max_width = 0.0f;

    for (int i = 0; i < *(int*)(item + 0xc4); i++) {
        int event = hud_item_get_event_at_index(item, i);
        float w = hud_measure_text_width_scaled(
            HUD_POPUP_TEXT_SCALE,
            *(int*)(item + 0xbc),
            *(char**)(event + 4));
        if (max_width < w) {
            max_width = w;
        }
    }

    return max_width;
}

// Address: 0x800498f0 | Name: hud_popup_create_dropdown
// MASSIVE: Creates and displays a dropdown/option popup.
// Steps:
//   1) Create element as child of viewport
//   2) Compute text bounds for positioning
//   3) Measure max event width for auto-sizing
//   4) Set popup reference (+0x198)
//   5) Init with dismiss/cancel callbacks
//   6) Set scroll (max 8-10 visible items)
//   7) Create list items for each option
//   8) Apply scroll and position
//   9) Link, show, scroll to current selection
void hud_popup_create_dropdown(void) {
    int viewport, popup_element, target_item;
    float text_bounds[4];
    int prev_variant;

    viewport = hud_style_get_value_key();  // FUN_803826dc
    popup_element = hud_element_create(viewport, 0, 2);
    target_item = *(int*)(viewport + 0x194);

    // Get position from target item's text bounds
    hud_item_compute_text_bounds(target_item, text_bounds, NULL, NULL);
    float popup_x = text_bounds[0];
    float popup_width = g_hud_inv_pixel_scale_x * (text_bounds[2] - popup_x);

    // Auto-size: if content wider than bounds, shift left (if room)
    if ((*(u32*)(target_item + 0x28) & 0x8000000) == 0) {
        float max_w = hud_item_measure_max_event_width(viewport, target_item);
        if (popup_width < (HUD_POPUP_PADDING + max_w)) {
            float shift = HUD_POPUP_SHIFT_THRESHOLD * g_hud_pixel_scale_x + g_hud_offset_x;
            if (shift < popup_x) {
                popup_x -= (HUD_POPUP_PADDING + max_w - popup_width) * g_hud_pixel_scale_x;
            }
        }
    }

    // Set popup reference
    *(int*)(viewport + 0x198) = target_item;

    // Init popup element
    hud_element_push(popup_element);
    int variant = ((*(u32*)(viewport + 0x40) & 0x800) != 0) ? 4 : 3;
    prev_variant = hud_style_apply_variant(variant);

    hud_element_init_from_layout(0x8000, 0, hud_popup_on_dismiss, hud_popup_on_cancel);
    *(u32*)(popup_element + 0x44) |= 0x10;
    hud_set_margins(1.0f, HUD_POPUP_MARGIN, 1);

    // Set visible item count (max 8 or 7, capped at event count if < 10)
    int max_visible = ((*(u32*)(target_item + 0x28) & 0x8000000) == 0) ? 8 : 7;
    if (*(int*)(target_item + 0xc4) < 10) {
        max_visible = *(int*)(target_item + 0xc4);
    }
    hud_element_scroll_horizontal(0.0f, max_visible);

    // Create list items for each option
    for (int i = 0; i < *(int*)(target_item + 0xc4); i++) {
        u32 font = *(u32*)(*(int*)(viewport + 0x194) + 0x1c);
        int list_item = hud_add_list_item();
        *(u32*)(list_item + 0x1c) = font;
    }

    hud_element_apply_scroll();
    hud_element_pop();

    // Position popup below or above depending on screen space
    float y_pos = g_hud_norm_scale * (text_bounds[3] - g_hud_offset_y);
    float popup_height = *(float*)(popup_element + 0x64);
    float viewport_space = *(float*)(viewport + 0x6c) + *(float*)(viewport + 0x60);

    if (y_pos + popup_height > viewport_space && y_pos > HUD_POPUP_FLIP_THRESHOLD) {
        // Flip above: position above the target
        y_pos = g_hud_norm_scale * (text_bounds[1] - g_hud_offset_y) - popup_height;
    }

    hud_element_set_pos_xy(
        g_hud_inv_pixel_scale_x * (popup_x - g_hud_offset_x),
        y_pos, popup_element);

    hud_element_link_and_show(viewport, popup_element);
    hud_style_apply_variant(prev_variant);

    // Scroll to current selection
    int selected = hud_find_child_by_id(popup_element, *(int*)(target_item + 0xc8));
    hud_scroll_to_item(popup_element, selected);
    hud_scroll_snap_position(popup_element);

    hud_style_finalize_value();  // FUN_80382728
}
