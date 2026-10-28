// =============================================================================
// HUD Layout Measurement Utilities
// TimeSplitters: Future Perfect (GameCube)
// Decompiled: 2026-08-06
// =============================================================================
// Text measurement and layout positioning helpers used by the HUD layout system.
// All coordinate outputs are in normalized screen space (0.0-1.0) unless noted.
// These functions bridge between the text rendering system (pixel coords) and
// the HUD layout system (normalized coords).
// =============================================================================

#include "common.h"
#include "hud.h"
#include "text.h"

// External references
extern float FLOAT_80613bb0;   // 0.0f
extern float FLOAT_80613bb4;   // -1.0f
extern float FLOAT_80613bc8;   // 1.0f
extern float FLOAT_80613bd0;   // 0.5f
extern float FLOAT_80611674;   // line height in pixels
extern float FLOAT_80611a5c;   // inverse screen height (1/H for normalization)
extern float FLOAT_80611a60;   // inverse screen width (1/W for normalization)
extern float FLOAT_80611a64;   // screen scale Y
extern float FLOAT_80611a68;   // screen scale X
extern char DAT_805253e8[];    // word-wrap scratch buffer (2000 bytes)

// -----------------------------------------------------------------------------
// Address: 0x800461dc | hud_measure_item_text_width
// Measures an item's text label width in normalized screen coordinates.
// Temporarily sets the item's font, measures pixel width, restores previous font.
//
// Parameters:
//   param_1 - item pointer (text at +0x20, font at +0x1C)
//
// Returns: text width in normalized coords (0.0 to 1.0)
// -----------------------------------------------------------------------------
double hud_measure_item_text_width(int param_1) {
    int prev_font = text_set_font_table(*(int*)(param_1 + 0x1C));
    double pixel_width = text_get_char_width(*(char**)(param_1 + 0x20));
    text_set_font_table(prev_font);

    // Convert pixel width to normalized: multiply by (1.0 / screen_scale_X)
    return (double)(float)(pixel_width * (double)(FLOAT_80613bc8 / FLOAT_80611a68));
}

// -----------------------------------------------------------------------------
// Address: 0x80046248 | hud_measure_text_width_scaled
// Measures text width with an explicit scale factor applied. The scale is
// converted to pixel space before measurement.
//
// Parameters:
//   param_1 - scale factor (normalized, multiplied by screen_scale_X)
//   param_2 - font table index
//   param_3 - text string pointer
//
// Returns: text width in normalized coords
// -----------------------------------------------------------------------------
double hud_measure_text_width_scaled(double param_1, int param_2, char* param_3) {
    int prev_font = text_set_font_table(param_2);

    // Apply scale in pixel space, get scaled text
    float pixel_scale = (float)(param_1 * (double)FLOAT_80611a68);
    char* scaled_text = text_check_scale((double)pixel_scale, param_3);

    double pixel_width = text_get_char_width(scaled_text);
    text_set_font_table(prev_font);

    // Convert to normalized using inverse width
    return (double)(float)(pixel_width * (double)FLOAT_80611a60);
}

// -----------------------------------------------------------------------------
// Address: 0x800462bc | hud_measure_text_height
// Measures text height based on line count at a given wrap width.
// Returns line_count * line_height in normalized screen coordinates.
// Minimum return is 1 line height (even for empty text).
//
// Parameters:
//   param_1 - wrap width (normalized, multiplied by screen_scale_X internally)
//   param_2 - font table index
//   param_3 - text string pointer (NULL returns 1 line)
//
// Returns: total text height in normalized coords
// -----------------------------------------------------------------------------
double hud_measure_text_height(double param_1, int param_2, char* param_3) {
    int prev_font = text_set_font_table(param_2);

    double line_count = (double)FLOAT_80613bc8;  // default: 1 line
    if (param_3 != NULL) {
        // Count lines at given pixel width
        float pixel_width = (float)(param_1 * (double)FLOAT_80611a68);
        line_count = text_count_lines((double)pixel_width, param_3);
    }

    // Clamp minimum to 1 line
    if (line_count < (double)FLOAT_80613bc8) {
        line_count = (double)FLOAT_80613bc8;
    }

    // Total pixel height = line_count * line_height
    float pixel_height = (float)((double)FLOAT_80611674 * line_count);
    text_set_font_table(prev_font);

    // Convert to normalized using inverse height
    return (double)(float)(pixel_height * (double)FLOAT_80611a5c);
}

// -----------------------------------------------------------------------------
// Address: 0x80046350 | hud_item_get_text_height
// Gets the full text height for an item, accounting for:
// - NULL text (returns 0.0)
// - Case-scale transform (flag 0x2000): applies uppercase/scale, copies to
//   scratch buffer (DAT_805253e8, 2000 bytes), then measures wrapped height
// - Standard text: measures directly with item's font and width
//
// Parameters:
//   param_1 - item pointer
//             +0x1C: font index
//             +0x20: text string
//             +0x28: flags (bit 0x2000 = case-scale mode)
//             +0x44: item width (normalized)
//
// Returns: text height in normalized coords, or 0.0 if no text
// -----------------------------------------------------------------------------
double hud_item_get_text_height(int param_1) {
    char* text = *(char**)(param_1 + 0x20);

    if (text == NULL) {
        return (double)FLOAT_80613bb0;  // 0.0
    }

    // If case-scale mode: transform text into scratch buffer
    if ((*(u32*)(param_1 + 0x28) & 0x2000) != 0) {
        float pixel_width = *(float*)(param_1 + 0x44) * FLOAT_80611a68;
        int transformed = hud_text_apply_case_scale((double)pixel_width, text, *(u32*)(param_1 + 0x28));
        // Copy to scratch buffer (2000 char limit)
        FUN_8038b4cc(DAT_805253e8, transformed, 2000);
        text = DAT_805253e8;
    }

    return hud_measure_text_height((double)*(float*)(param_1 + 0x44),
                                   *(int*)(param_1 + 0x1C), text);
}

// -----------------------------------------------------------------------------
// Address: 0x800463dc | hud_item_calc_center_x
// Computes horizontal centering offset: half of (item_width - text_width).
// Used to horizontally center text within an item's bounding box.
//
// Parameters:
//   param_1 - item pointer (+0x6C = item width, text at +0x20)
//
// Returns: X offset for centered text (normalized)
// -----------------------------------------------------------------------------
double hud_item_calc_center_x(int param_1) {
    double text_width = hud_measure_item_text_width(param_1);
    float item_width = *(float*)(param_1 + 0x6C);

    return (double)(FLOAT_80613bd0 * (float)((double)item_width - text_width));
}

// -----------------------------------------------------------------------------
// Address: 0x80046418 | hud_item_calc_center_y
// Computes vertical centering offset: half of (item_height - text_height).
// Used to vertically center text within an item's bounding box.
//
// Parameters:
//   param_1 - item pointer (+0x74 = item height)
//
// Returns: Y offset for centered text (normalized)
// -----------------------------------------------------------------------------
double hud_item_calc_center_y(int param_1) {
    double text_height = hud_item_get_text_height(param_1);
    float item_height = *(float*)(param_1 + 0x74);

    return (double)(FLOAT_80613bd0 * (float)((double)item_height - text_height));
}

// -----------------------------------------------------------------------------
// Address: 0x80046454 | hud_item_calc_neg_half_width
// Returns half of (negative_unit - item_width). Used for right-edge alignment
// calculations where the origin is at the right side.
//
// Parameters:
//   param_1 - item pointer (+0x4C = item width)
//
// Returns: 0.5 * (-1.0 - item_width)
// -----------------------------------------------------------------------------
double hud_item_calc_neg_half_width(int param_1) {
    return (double)(FLOAT_80613bd0 * (FLOAT_80613bb4 - *(float*)(param_1 + 0x4C)));
}

// -----------------------------------------------------------------------------
// Address: 0x8004646c | hud_item_calc_content_y
// Computes Y position for content within a parent element's layout bounds.
// Walks hierarchy to find the containing element, checks for fullscreen flag
// (0x40000000 on parent), then interpolates between layout min/max Y bounds
// considering the current element's item height.
//
// Parameters:
//   param_1 - element pointer (as int array)
//             [7] = layout table ptr (+0x7C min_y, +0x80 max_y)
//             [0x19] = content height offset
//
// Returns: Y position in normalized coords
// -----------------------------------------------------------------------------
double hud_item_calc_content_y(int* param_1) {
    // Walk hierarchy to find parent container
    int parent = hud_walk_hierarchy_by_type(param_1, 1);

    BOOL is_fullscreen = FALSE;
    if (parent != 0 && (*(u32*)(parent + 0x40) & 0x40000000) != 0) {
        is_fullscreen = TRUE;
    }

    float min_y = FLOAT_80613bb0;  // default: 0.0
    if (!is_fullscreen) {
        // Use layout table min_y
        min_y = *(float*)(param_1[7] + 0x7C);
    }

    float max_y = *(float*)(param_1[7] + 0x80);
    float content_height = (float)param_1[0x19];

    // Interpolate: 0.5 * (max_y - min_y - content_height) + min_y
    return (double)(FLOAT_80613bd0 * ((max_y - min_y) - content_height) + min_y);
}

// -----------------------------------------------------------------------------
// Address: 0x800464ec | hud_element_apply_center_x
// Applies horizontal centering to an element: flushes pending layout changes,
// computes the centering X offset, and stores it at element+0x5C (the X
// position field used by rendering).
//
// Parameters:
//   param_1 - element pointer
// -----------------------------------------------------------------------------
void hud_element_apply_center_x(int param_1) {
    hud_flush_layout_if_active(param_1);
    double x = hud_item_calc_neg_half_width(param_1);
    *(float*)(param_1 + 0x5C) = (float)x;
}

// -----------------------------------------------------------------------------
// Address: 0x80046524 | hud_element_apply_content_y
// Applies content Y position to an element: flushes pending layout changes,
// computes Y via hierarchy-aware calculation, stores at element[0x18].
//
// Parameters:
//   param_1 - element pointer (as int array)
// -----------------------------------------------------------------------------
void hud_element_apply_content_y(int* param_1) {
    hud_flush_layout_if_active((int)param_1);
    double y = hud_item_calc_content_y(param_1);
    param_1[0x18] = (int)(float)y;
}

// =============================================================================
// Extended Positioning API (batch 082)
// =============================================================================

// -----------------------------------------------------------------------------
// Address: 0x8004655c | hud_element_apply_center_xy
// Convenience: applies both horizontal centering and content Y in one call.
// -----------------------------------------------------------------------------
void hud_element_apply_center_xy(int* param_1) {
    hud_element_apply_center_x((int)param_1);
    hud_element_apply_content_y(param_1);
}

// -----------------------------------------------------------------------------
// Address: 0x80046590 | hud_calc_center_x_in_bounds
// Pure calculation: centers item horizontally within explicit bounds.
//   result = 0.5 * (max_x - item_width) + min_x
//
// Parameters:
//   param_1 - min_x (left bound)
//   param_2 - max_x (right bound)
//   param_3 - element pointer (+0x4C = item width)
//
// Returns: centered X position (normalized)
// -----------------------------------------------------------------------------
double hud_calc_center_x_in_bounds(double min_x, double max_x, int element) {
    float item_width = *(float*)(element + 0x4C);
    return (double)(FLOAT_80613bd0 * (float)(max_x - (double)item_width) + (float)min_x);
}

// -----------------------------------------------------------------------------
// Address: 0x800465a4 | hud_calc_center_y_in_bounds
// Centers item vertically within explicit bounds, hierarchy-aware.
// Checks parent for fullscreen (0x40000000) and max-height (0x4000000) flags
// to adjust min/max Y from layout table. Accounts for content height.
//
// Parameters:
//   param_1 - min_y override
//   param_2 - max_y override
//   param_3 - element pointer (as int*)
//
// Returns: centered Y position (normalized)
// -----------------------------------------------------------------------------
double hud_calc_center_y_in_bounds(double min_y, double max_y, int* element) {
    int parent = hud_walk_hierarchy_by_type(element, 1);

    // Check fullscreen flag on parent -> ignore layout min_y
    BOOL is_fullscreen = FALSE;
    if (parent != 0 && (*(u32*)(parent + 0x40) & 0x40000000) != 0) {
        is_fullscreen = TRUE;
    }
    float layout_min_y = FLOAT_80613bb0;
    if (!is_fullscreen) {
        layout_min_y = *(float*)(element[7] + 0x7C);
    }

    // Check max-height flag on parent -> ignore layout max_y
    BOOL no_max = FALSE;
    if (parent != 0 && (*(u32*)(parent + 0x40) & 0x4000000) != 0) {
        no_max = TRUE;
    }
    float layout_max_offset = FLOAT_80613bb0;
    if (!no_max) {
        layout_max_offset = FLOAT_80613bb4 - *(float*)(element[7] + 0x80);
    }

    float available = (float)((double)(float)(max_y - (double)layout_max_offset) - (double)layout_min_y);
    float content_height = (float)element[0x19];

    return (double)(FLOAT_80613bd0 * (available - content_height) +
                    (float)(min_y + (double)layout_min_y));
}

// -----------------------------------------------------------------------------
// Address: 0x80046688 | hud_element_apply_center_x_bounds
// Applies centered X with custom bounds: flushes layout, computes centering,
// stores at element+0x5C.
//
// Parameters:
//   param_1 - min_x bound
//   param_2 - max_x bound
//   param_3 - element pointer
// -----------------------------------------------------------------------------
void hud_element_apply_center_x_bounds(double min_x, double max_x, int element) {
    hud_flush_layout_if_active(element);
    double x = hud_calc_center_x_in_bounds(min_x, max_x, element);
    *(float*)(element + 0x5C) = (float)x;
}

// -----------------------------------------------------------------------------
// Address: 0x800466e0 | hud_element_apply_center_y_bounds
// Applies centered Y with custom bounds: flushes layout, computes centering
// (hierarchy-aware), stores at element[0x18].
//
// Parameters:
//   param_1 - min_y bound
//   param_2 - max_y bound
//   param_3 - element pointer (as int*)
// -----------------------------------------------------------------------------
void hud_element_apply_center_y_bounds(double min_y, double max_y, int* element) {
    hud_flush_layout_if_active((int)element);
    double y = hud_calc_center_y_in_bounds(min_y, max_y, element);
    element[0x18] = (int)(float)y;
}

// -----------------------------------------------------------------------------
// Address: 0x80046738 | hud_element_apply_center_xy_bounds
// Applies both centered X and Y with custom bounds in one call.
//
// Parameters:
//   param_1 - min_x
//   param_2 - min_y
//   param_3 - max_x
//   param_4 - max_y
//   param_5 - element pointer
// -----------------------------------------------------------------------------
void hud_element_apply_center_xy_bounds(double min_x, double min_y,
                                         double max_x, double max_y, int* element) {
    hud_element_apply_center_x_bounds(min_x, max_x, (int)element);
    hud_element_apply_center_y_bounds(min_y, max_y, element);
}

// -----------------------------------------------------------------------------
// Address: 0x80046790 | hud_element_set_pos_x
// Sets explicit X position. No centering. Flushes layout first.
//
// Parameters:
//   param_1 - X position (normalized)
//   param_2 - element pointer
// -----------------------------------------------------------------------------
void hud_element_set_pos_x(double x, int element) {
    hud_flush_layout_if_active(element);
    *(float*)(element + 0x5C) = (float)x;
}

// -----------------------------------------------------------------------------
// Address: 0x800467cc | hud_element_set_pos_y
// Sets explicit Y position. No centering. Flushes layout first.
//
// Parameters:
//   param_1 - Y position (normalized)
//   param_2 - element pointer
// -----------------------------------------------------------------------------
void hud_element_set_pos_y(double y, int element) {
    hud_flush_layout_if_active(element);
    *(float*)(element + 0x60) = (float)y;
}

// -----------------------------------------------------------------------------
// Address: 0x80046808 | hud_element_set_pos_xy
// Sets explicit X and Y position. No centering. Flushes layout first.
//
// Parameters:
//   param_1 - X position (normalized)
//   param_2 - Y position (normalized)
//   param_3 - element pointer
// -----------------------------------------------------------------------------
void hud_element_set_pos_xy(double x, double y, int element) {
    hud_flush_layout_if_active(element);
    *(float*)(element + 0x5C) = (float)x;
    *(float*)(element + 0x60) = (float)y;
}

// -----------------------------------------------------------------------------
// Address: 0x80046854 | hud_element_link_and_show
// Links a child element to a parent and triggers the child's show transition.
// Used when opening sub-menus or dialogs from a parent menu.
//
// If parent is NULL: directly shows the child.
// If parent exists: sets parent's active child pointer (+0x16C), copies the
// child's render callback (+0x170 from child+0x04), then shows the child.
// Gated by focus restore state: skips entirely unless in snap mode or no
// restore active (prevents double-show during focus restoration).
//
// Parameters:
//   param_1 - parent element (or 0 for root-level show)
//   param_2 - child element to show
// -----------------------------------------------------------------------------
void hud_element_link_and_show(int parent, int child) {
    // Gate: only proceed if no focus restore active, or snap mode
    if (DAT_806119e4 == 0 || *(int*)(DAT_806119e4 + 4) != 0) {
        if (parent == 0) {
            hud_element_show(child);
        } else {
            *(int*)(parent + 0x16C) = 0;
            *(int*)(parent + 0x170) = 0;

            if (child != 0 && *(int*)(child + 0x2C) == parent) {
                *(int*)(parent + 0x16C) = child;
                *(int*)(parent + 0x170) = *(int*)(child + 4);
                hud_element_show(child);
            }
        }
    }
}
