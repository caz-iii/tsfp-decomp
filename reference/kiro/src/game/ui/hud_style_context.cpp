// =============================================================================
// HUD Style Context — Push/Get/Apply API
// Address Range: 0x8004d098 - 0x8004d234
// Date: 2026-08-10
//
// The HUD style context manages a set of "current" style descriptors that
// element creation functions read from. Each descriptor type (element, font,
// color, spacing) has:
//   - A global pointer to the currently active descriptor
//   - A push function that swaps to a new descriptor by ID and returns the
//     previous descriptor's ID (enabling stack-like save/restore)
//   - A getter that returns the current descriptor pointer
//
// Element descriptors aggregate font/spacing/color references in "variant"
// sub-arrays, so pushing an element implicitly sets font+spacing+color via
// hud_style_apply_variant.
//
// Globals:
//   DAT_80611a44 — current element style descriptor pointer
//   DAT_80611a48 — current variant index within element
//   DAT_80611a40 — current font style descriptor pointer
//   DAT_80611a3c — current color palette descriptor pointer
//   DAT_80611a38 — current spacing/layout descriptor pointer
// =============================================================================

#include "common.h"

// External: linked-list search functions (in hud_theme.cpp)
extern void* hud_style_find_element_by_id(int id);
extern void* hud_style_find_font_by_id(int id);
extern void* hud_style_find_color_by_id(int id);
extern void* hud_style_find_spacing_by_id(int id);

// Current style context (global state)
static void* g_current_element;   // DAT_80611a44
static int   g_current_variant;   // DAT_80611a48
static void* g_current_font;      // DAT_80611a40
static void* g_current_color;     // DAT_80611a3c
static void* g_current_spacing;   // DAT_80611a38

// -----------------------------------------------------------------------------
// Address: 0x8004d0e4 | Name: hud_style_get_font
// Returns the currently active font style descriptor pointer.
// -----------------------------------------------------------------------------
void* hud_style_get_font(void) {
    return g_current_font;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d0ec | Name: hud_style_push_font
// Push a new font style by ID. Saves previous font's ID (+0xDC), sets new via
// find_font_by_id. Returns previous font ID (or -1 if no font was active).
// -----------------------------------------------------------------------------
int hud_style_push_font(int font_id) {
    int prev_id = -1;

    if (g_current_font != NULL) {
        prev_id = *(int*)((char*)g_current_font + 0xDC);
    }

    g_current_font = hud_style_find_font_by_id(font_id);
    return prev_id;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d130 | Name: hud_style_get_color
// Returns the currently active color palette descriptor pointer.
// -----------------------------------------------------------------------------
void* hud_style_get_color(void) {
    return g_current_color;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d138 | Name: hud_style_push_color
// Push a new color palette by ID. Saves previous color's ID (+0x10), sets new
// via find_color_by_id. Returns previous color ID (or -1 if none active).
// -----------------------------------------------------------------------------
int hud_style_push_color(int color_id) {
    int prev_id = -1;

    if (g_current_color != NULL) {
        prev_id = *(int*)((char*)g_current_color + 0x10);
    }

    g_current_color = hud_style_find_color_by_id(color_id);
    return prev_id;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d17c | Name: hud_style_get_spacing
// Returns the currently active spacing/layout descriptor pointer.
// -----------------------------------------------------------------------------
void* hud_style_get_spacing(void) {
    return g_current_spacing;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d184 | Name: hud_style_push_spacing
// Push a new spacing style by ID. Saves previous spacing's ID (+0x90), sets
// new via find_spacing_by_id. Returns previous spacing ID (or -1 if none).
// -----------------------------------------------------------------------------
int hud_style_push_spacing(int spacing_id) {
    int prev_id = -1;

    if (g_current_spacing != NULL) {
        prev_id = *(int*)((char*)g_current_spacing + 0x90);
    }

    g_current_spacing = hud_style_find_spacing_by_id(spacing_id);
    return prev_id;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d1c8 | Name: hud_style_apply_variant
// Applies a variant index within the current element style. The element
// contains sub-arrays of font/spacing/color IDs indexed by variant:
//   +0x00..+0x1C: font IDs (8 variants, each int)
//   +0x20..+0x3C: spacing IDs (8 variants, each int)
//   +0x40:        color ID (shared across variants)
//
// Pushes font, spacing, and color from the element's arrays at the given
// variant offset. Returns the previous variant index.
// -----------------------------------------------------------------------------
int hud_style_apply_variant(int variant_index) {
    int prev_variant = g_current_variant;
    g_current_variant = variant_index;

    // Element sub-arrays: font IDs at +0x00, spacing IDs at +0x20, color at +0x40
    int* element_base = (int*)g_current_element;
    hud_style_push_font(element_base[variant_index]);           // +variant*4
    hud_style_push_spacing(element_base[variant_index + 8]);    // +0x20 + variant*4
    hud_style_push_color(element_base[16]);                     // +0x40 (shared)

    return prev_variant;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d098 | Name: hud_style_push_element
// Push a new element style by ID. Saves previous element's ID (+0x48), sets new
// via find_element_by_id, then applies the current variant's sub-styles.
// Returns previous element ID (or -1 if none was active).
// -----------------------------------------------------------------------------
int hud_style_push_element(int element_id) {
    int prev_id = -1;

    if (g_current_element != NULL) {
        prev_id = *(int*)((char*)g_current_element + 0x48);
    }

    g_current_element = hud_style_find_element_by_id(element_id);
    hud_style_apply_variant(g_current_variant);

    return prev_id;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d22c | Name: hud_style_get_variant
// Returns the current variant index.
// -----------------------------------------------------------------------------
int hud_style_get_variant(void) {
    return g_current_variant;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d234 | Name: hud_style_get_default_sort_key
// Gets the default sort key from an element style descriptor. Reads the pointer
// at param+0x1C and dereferences it. Used for Z-order/sort-depth initialization
// when creating new HUD elements.
// -----------------------------------------------------------------------------
int hud_style_get_default_sort_key(void* element_desc) {
    // *(*(param + 0x1C))
    int** ptr = (int**)((char*)element_desc + 0x1C);
    return **ptr;
}
