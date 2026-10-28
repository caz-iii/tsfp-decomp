// =============================================================================
// HUD Style Definition — Theme Builder API
// Address Range: 0x8004d240 - 0x8004d440
// Date: 2026-08-10
//
// These functions are used during theme initialization (hud_theme_init_default
// and menu-specific theme builders) to CREATE and CONFIGURE style descriptors.
// Unlike the style context (push/get) functions, these allocate new descriptors
// and append them to the global linked lists.
//
// Element descriptors: 0x50 bytes, linked via +0x4C, ID at +0x48
//   - Contains font/spacing ID arrays (8 variants each) at +0x00..+0x1F and +0x20..+0x3F
//   - Color ID at +0x40
//
// Font descriptors: 0xF0 bytes, linked via +0xE0, ID at +0xDC
//   - Font resource ID at +0x00
//   - Size pair (normal, small) at +0x04, +0x08
//   - RGBA color at +0x0C..+0x18
//   - Scale XY at +0x1C, +0x20
//   - Offset XY at +0x24, +0x28
//   - Shadow offset XY at +0x2C, +0x30
//
// Globals:
//   DAT_80611a34 — pointer to element descriptor being built
//   DAT_80611a30 — pointer to font descriptor being built
//   DAT_80611a24 — element linked list head
//   DAT_80611a20 — element linked list tail
//   DAT_80611a1c — font linked list head
//   DAT_80611a18 — font linked list tail
// =============================================================================

#include "common.h"

extern void* mem_alloc(int size, int align);
extern void  memset_wrapper(void* dst, int val, int size);
extern void  leaf_31e8(void* dst, void* src, int size);  // memcpy
extern void* hud_style_find_element_by_id(int id);
extern void* hud_style_find_font_by_id(int id);

// Build state
static void* g_building_element;  // DAT_80611a34
static void* g_building_font;     // DAT_80611a30

// Element linked list
static void* g_element_list_head; // DAT_80611a24
static void* g_element_list_tail; // DAT_80611a20

// Font linked list
static void* g_font_list_head;    // DAT_80611a1c
static void* g_font_list_tail;    // DAT_80611a18

// -----------------------------------------------------------------------------
// Address: 0x8004d240 | Name: hud_style_create_element
// Allocates a new 0x50-byte element style descriptor. If a template ID is
// provided (>= 0), copies from the existing element with that ID. Sets the
// new element's ID field at +0x48, appends to the element linked list.
// Called during theme init to define each element style.
// -----------------------------------------------------------------------------
void hud_style_create_element(int template_id, int new_id) {
    // template_id and new_id arrive packed in a paired register (longlong)
    void* elem = mem_alloc(0x50, 1);
    memset_wrapper(elem, 0, 0x50);
    g_building_element = elem;

    if (template_id >= 0) {
        void* src = hud_style_find_element_by_id(template_id);
        leaf_31e8(elem, src, 0x50);  // copy template
    }

    // Set ID at +0x48
    *(int*)((char*)elem + 0x48) = new_id;

    // Append to linked list (next pointer at +0x4C)
    if (g_element_list_head != NULL) {
        *(void**)((char*)g_element_list_tail + 0x4C) = elem;
    } else {
        g_element_list_head = elem;
    }
    g_element_list_tail = elem;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d2e0 | Name: hud_style_element_set_variants
// Sets font and spacing IDs for one or all variants in the current element.
// If variant_index == -1, sets ALL 8 variant slots. Otherwise sets only the
// specified slot. Font IDs at +0x00..+0x1C, spacing IDs at +0x20..+0x3C.
// -----------------------------------------------------------------------------
void hud_style_element_set_variants(int variant_index, int font_id, int spacing_id) {
    int start = variant_index;
    int count;

    if (variant_index == -1) {
        start = 0;
        count = 8;
    } else {
        count = 1;
    }

    for (int i = 0; i < count; i++) {
        int offset = (start + i) * 4;
        *(int*)((char*)g_building_element + offset)        = font_id;    // font slot
        *(int*)((char*)g_building_element + offset + 0x20) = spacing_id; // spacing slot
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8004d328 | Name: hud_style_element_set_color
// Sets the color palette ID at +0x40 in the current element being built.
// The color is shared across all variants.
// -----------------------------------------------------------------------------
void hud_style_element_set_color(int color_id) {
    *(int*)((char*)g_building_element + 0x40) = color_id;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d334 | Name: hud_style_create_font
// Allocates a new 0xF0-byte font style descriptor. If a template ID is
// provided (>= 0), copies from existing font. Sets ID at +0xDC, appends to
// font linked list (next at +0xE0).
// -----------------------------------------------------------------------------
void hud_style_create_font(int template_id, int new_id) {
    void* font = mem_alloc(0xF0, 1);
    memset_wrapper(font, 0, 0xE4);
    g_building_font = font;

    if (template_id >= 0) {
        void* src = hud_style_find_font_by_id(template_id);
        leaf_31e8(font, src, 0xE4);  // copy template
    }

    // Set ID at +0xDC
    *(int*)((char*)font + 0xDC) = new_id;

    // Append to linked list (next pointer at +0xE0)
    if (g_font_list_head != NULL) {
        *(void**)((char*)g_font_list_tail + 0xE0) = font;
    } else {
        g_font_list_head = font;
    }
    g_font_list_tail = font;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d3d4 | Name: hud_style_font_set_resource
// Sets the font resource/asset ID at +0x00 in the current font being built.
// This determines which font texture/glyph set to render with.
// -----------------------------------------------------------------------------
void hud_style_font_set_resource(int resource_id) {
    *(int*)g_building_font = resource_id;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d3e0 | Name: hud_style_font_set_sizes
// Sets the normal size at +0x04 and small/fallback size at +0x08 in the
// current font descriptor.
// -----------------------------------------------------------------------------
void hud_style_font_set_sizes(int normal_size, int small_size) {
    *(int*)((char*)g_building_font + 0x04) = normal_size;
    *(int*)((char*)g_building_font + 0x08) = small_size;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d3f4 | Name: hud_style_font_set_color
// Sets RGBA color components at +0x0C..+0x18 (as floats) in the current font.
// These are the default text rendering colors for this font style.
// -----------------------------------------------------------------------------
void hud_style_font_set_color(float r, float g, float b, float a) {
    *(float*)((char*)g_building_font + 0x0C) = r;
    *(float*)((char*)g_building_font + 0x10) = g;
    *(float*)((char*)g_building_font + 0x14) = b;
    *(float*)((char*)g_building_font + 0x18) = a;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d418 | Name: hud_style_font_set_scale
// Sets X and Y scale factors at +0x1C, +0x20 in the current font descriptor.
// Used for per-style text size scaling.
// -----------------------------------------------------------------------------
void hud_style_font_set_scale(float scale_x, float scale_y) {
    *(float*)((char*)g_building_font + 0x1C) = scale_x;
    *(float*)((char*)g_building_font + 0x20) = scale_y;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d42c | Name: hud_style_font_set_offset
// Sets X and Y offset at +0x24, +0x28 in the current font descriptor.
// Used for per-style text position adjustment (e.g., vertical centering).
// -----------------------------------------------------------------------------
void hud_style_font_set_offset(float offset_x, float offset_y) {
    *(float*)((char*)g_building_font + 0x24) = offset_x;
    *(float*)((char*)g_building_font + 0x28) = offset_y;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d440 | Name: hud_style_font_set_shadow
// Sets shadow offset X and Y at +0x2C, +0x30 in the current font descriptor.
// Defines the drop-shadow displacement for text rendered with this style.
// -----------------------------------------------------------------------------
void hud_style_font_set_shadow(float shadow_x, float shadow_y) {
    *(float*)((char*)g_building_font + 0x2C) = shadow_x;
    *(float*)((char*)g_building_font + 0x30) = shadow_y;
}

// =============================================================================
// Font Descriptor Setters (continued) — Batch 097
// Address Range: 0x8004d454 - 0x8004d58c
// Date: 2026-08-11
//
// Additional font descriptor configuration functions. All write to
// g_building_font (DAT_80611a30). These cover:
//   - Shadow color
//   - Text rendering style (bold/italic/underline) + focus variants
//   - Margins/padding
//   - Text bounds (max width/height/lines with wrap mode)
//   - Text animation parameters
//   - Wrap mode, truncation mode, alignment
// =============================================================================

// -----------------------------------------------------------------------------
// Address: 0x8004d454 | Name: hud_style_font_set_shadow_color
// Sets shadow color RGBA as floats at +0x34..+0x40.
// -----------------------------------------------------------------------------
void hud_style_font_set_shadow_color(float r, float g, float b, float a) {
    *(float*)((char*)g_building_font + 0x34) = r;
    *(float*)((char*)g_building_font + 0x38) = g;
    *(float*)((char*)g_building_font + 0x3C) = b;
    *(float*)((char*)g_building_font + 0x40) = a;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d478 | Name: hud_style_font_set_style
// Sets text rendering style flags. Stores 3 values at +0x44/+0x48/+0x4C
// (underline, italic, bold respectively). Also sets the focus style to the
// same values as a default.
// -----------------------------------------------------------------------------
void hud_style_font_set_style(int bold, int italic, int underline) {
    *(int*)((char*)g_building_font + 0x4C) = bold;
    *(int*)((char*)g_building_font + 0x48) = italic;
    *(int*)((char*)g_building_font + 0x44) = underline;
    hud_style_font_set_focus_style(bold, italic, underline);
}

// -----------------------------------------------------------------------------
// Address: 0x8004d4b0 | Name: hud_style_font_set_focus_style
// Sets the text rendering style when the element has focus. Stored at
// +0x50/+0x54/+0x58 (underline, italic, bold for focused state).
// -----------------------------------------------------------------------------
void hud_style_font_set_focus_style(int bold, int italic, int underline) {
    *(int*)((char*)g_building_font + 0x58) = bold;
    *(int*)((char*)g_building_font + 0x54) = italic;
    *(int*)((char*)g_building_font + 0x50) = underline;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d4cc | Name: hud_style_font_set_margins
// Sets 5 margin/padding values as floats at +0x5C..+0x6C:
//   +0x5C = left, +0x60 = right, +0x64 = top, +0x68 = bottom, +0x6C = line
// -----------------------------------------------------------------------------
void hud_style_font_set_margins(float left, float right, float top, float bottom, float line_spacing) {
    *(float*)((char*)g_building_font + 0x5C) = left;
    *(float*)((char*)g_building_font + 0x60) = right;
    *(float*)((char*)g_building_font + 0x64) = top;
    *(float*)((char*)g_building_font + 0x68) = bottom;
    *(float*)((char*)g_building_font + 0x6C) = line_spacing;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d4f8 | Name: hud_style_font_set_bounds
// Enables text bounding and sets parameters. Sets flag 0x40 in the descriptor's
// flags word (+0x00), then writes:
//   +0x70 = bounds mode (clip/wrap/shrink)
//   +0x74 = max width (float)
//   +0x78 = max height (float)
//   +0xAC = max lines
// -----------------------------------------------------------------------------
void hud_style_font_set_bounds(int mode, float max_width, float max_height, int max_lines) {
    // Enable bounds flag
    *(int*)g_building_font |= 0x40;

    *(int*)((char*)g_building_font + 0x70)  = mode;
    *(float*)((char*)g_building_font + 0x74) = max_width;
    *(float*)((char*)g_building_font + 0x78) = max_height;
    *(int*)((char*)g_building_font + 0xAC)  = max_lines;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d52c | Name: hud_style_font_set_animation
// Sets 6 text animation/transition parameters:
//   +0xD4 = anim type
//   +0xC4 = speed
//   +0xC8 = delay
//   +0xCC = easing
//   +0xD0 = duration
//   +0xD8 = flags
// -----------------------------------------------------------------------------
void hud_style_font_set_animation(int type, int speed, int delay, int easing, int duration, int flags) {
    *(int*)((char*)g_building_font + 0xD4) = type;
    *(int*)((char*)g_building_font + 0xC4) = speed;
    *(int*)((char*)g_building_font + 0xC8) = delay;
    *(int*)((char*)g_building_font + 0xCC) = easing;
    *(int*)((char*)g_building_font + 0xD0) = duration;
    *(int*)((char*)g_building_font + 0xD8) = flags;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d560 | Name: hud_style_font_set_wrap_mode
// Sets the word wrap mode at +0xA4 in the current font descriptor.
// Controls how text breaks across lines.
// -----------------------------------------------------------------------------
void hud_style_font_set_wrap_mode(int wrap_mode) {
    *(int*)((char*)g_building_font + 0xA4) = wrap_mode;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d56c | Name: hud_style_font_set_truncation
// Sets the text truncation/overflow mode at +0xB0.
// Determines behavior when text exceeds bounds (ellipsis, clip, etc.)
// -----------------------------------------------------------------------------
void hud_style_font_set_truncation(int truncation_mode) {
    *(int*)((char*)g_building_font + 0xB0) = truncation_mode;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d578 | Name: hud_style_font_set_truncation_params
// Sets truncation parameters at +0xB4 and +0xB8.
// Typically max visible chars and ellipsis character mode.
// -----------------------------------------------------------------------------
void hud_style_font_set_truncation_params(int max_chars, int ellipsis_mode) {
    *(int*)((char*)g_building_font + 0xB4) = max_chars;
    *(int*)((char*)g_building_font + 0xB8) = ellipsis_mode;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d58c | Name: hud_style_font_set_alignment
// Sets text alignment mode at +0xA0 (0=left, 1=center, 2=right).
// -----------------------------------------------------------------------------
void hud_style_font_set_alignment(int alignment) {
    *(int*)((char*)g_building_font + 0xA0) = alignment;
}

// =============================================================================
// Font Setters (final) + Color & Spacing Descriptor Builders — Batch 098
// Address Range: 0x8004d598 - 0x8004d764
// Date: 2026-08-11
//
// Completes the font descriptor builder with case mode, line height, and
// letter spacing. Then adds the color palette descriptor (0x20 bytes) and
// spacing/layout descriptor (0xA0 bytes) allocators and their setters.
//
// Color descriptors: 0x20 bytes, linked via +0x14, ID at +0x10
//   - RGBA as 4 floats at +0x00..+0x0C
//
// Spacing descriptors: 0xA0 bytes, linked via +0x94, ID at +0x90
//   - Margin pairs (5 entries, stride 8) at +0x00..+0x27
//   - Padding values (5 entries, stride 4) at +0x28..+0x3B
//   - Gap values (5 entries, stride 4) at +0x3C..+0x4F
//   - Content rect (4 values) at +0x50..+0x5C
//
// Globals:
//   DAT_80611a2c — pointer to color descriptor being built
//   DAT_80611a28 — pointer to spacing descriptor being built
//   DAT_80611a14 — color linked list head
//   DAT_80611a10 — color linked list tail
//   DAT_80611a0c — spacing linked list head
//   DAT_80611a08 — spacing linked list tail
// =============================================================================

// -----------------------------------------------------------------------------
// Address: 0x8004d598 | Name: hud_style_font_set_case_mode
// Sets text case/transform mode at +0xA8 (0=none, 1=uppercase, 2=lowercase).
// -----------------------------------------------------------------------------
void hud_style_font_set_case_mode(int case_mode) {
    *(int*)((char*)g_building_font + 0xA8) = case_mode;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d5a4 | Name: hud_style_font_set_line_height
// Sets line height multiplier (float at +0x7C). Default 1.0, values > 1.0
// add extra vertical spacing between lines.
// -----------------------------------------------------------------------------
void hud_style_font_set_line_height(float line_height) {
    *(float*)((char*)g_building_font + 0x7C) = line_height;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d5b0 | Name: hud_style_font_set_letter_spacing
// Sets additional letter spacing (float at +0x80). Applied as extra pixels
// between each glyph.
// -----------------------------------------------------------------------------
void hud_style_font_set_letter_spacing(float spacing) {
    *(float*)((char*)g_building_font + 0x80) = spacing;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d5bc | Name: hud_style_create_color
// Allocates a 0x20-byte color palette descriptor. If template_id >= 0, copies
// from existing color. Sets ID at +0x10, appends to color linked list
// (next at +0x14). Sets g_building_color for subsequent setter calls.
// -----------------------------------------------------------------------------
void hud_style_create_color(int template_id, int new_id) {
    void* color = mem_alloc(0x20, 1);
    memset_wrapper(color, 0, 0x18);
    g_building_color = color;

    if (template_id >= 0) {
        void* src = hud_style_find_color_by_id(template_id);
        leaf_31e8(color, src, 0x18);  // copy template
    }

    // Set ID at +0x10
    *(int*)((char*)color + 0x10) = new_id;

    // Append to linked list (next pointer at +0x14)
    if (g_color_list_head != NULL) {
        *(void**)((char*)g_color_list_tail + 0x14) = color;
    } else {
        g_color_list_head = color;
    }
    g_color_list_tail = color;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d65c | Name: hud_style_color_set_rgba
// Sets the RGBA color values (4 floats at +0x00..+0x0C) in the current color
// palette being built.
// -----------------------------------------------------------------------------
void hud_style_color_set_rgba(float r, float g, float b, float a) {
    float* colors = (float*)g_building_color;
    colors[0] = r;
    colors[1] = g;
    colors[2] = b;
    colors[3] = a;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d680 | Name: hud_style_create_spacing
// Allocates a 0xA0-byte spacing/layout descriptor. If template_id >= 0, copies
// from existing spacing. Sets ID at +0x90, appends to spacing linked list
// (next at +0x94). Sets g_building_spacing for subsequent setter calls.
// -----------------------------------------------------------------------------
void hud_style_create_spacing(int template_id, int new_id) {
    void* spacing = mem_alloc(0xA0, 1);
    memset_wrapper(spacing, 0, 0x98);
    g_building_spacing = spacing;

    if (template_id >= 0) {
        void* src = hud_style_find_spacing_by_id(template_id);
        leaf_31e8(spacing, src, 0x98);  // copy template
    }

    // Set ID at +0x90
    *(int*)((char*)spacing + 0x90) = new_id;

    // Append to linked list (next pointer at +0x94)
    if (g_spacing_list_head != NULL) {
        *(void**)((char*)g_spacing_list_tail + 0x94) = spacing;
    } else {
        g_spacing_list_head = spacing;
    }
    g_spacing_list_tail = spacing;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d720 | Name: hud_style_spacing_set_margin
// Sets a margin pair by index (stride 8, base +0x00) in the current spacing
// descriptor. Each margin slot has horizontal and vertical values.
// Indices 0-4 correspond to different layout contexts.
// -----------------------------------------------------------------------------
void hud_style_spacing_set_margin(int index, int margin_h, int margin_v) {
    char* base = (char*)g_building_spacing;
    *(int*)(base + index * 8)     = margin_h;
    *(int*)(base + index * 8 + 4) = margin_v;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d73c | Name: hud_style_spacing_set_padding
// Sets a padding value by index (stride 4, base +0x28) in the spacing
// descriptor. Indices 0-4 for different layout contexts.
// -----------------------------------------------------------------------------
void hud_style_spacing_set_padding(int index, int padding) {
    char* base = (char*)g_building_spacing;
    *(int*)(base + index * 4 + 0x28) = padding;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d750 | Name: hud_style_spacing_set_gap
// Sets an inter-element gap value by index (stride 4, base +0x3C).
// Controls spacing between child elements in a layout.
// -----------------------------------------------------------------------------
void hud_style_spacing_set_gap(int index, int gap) {
    char* base = (char*)g_building_spacing;
    *(int*)(base + index * 4 + 0x3C) = gap;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d764 | Name: hud_style_spacing_set_content_rect
// Sets the content inset rectangle (4 values at +0x50..+0x5C).
// Typically left, top, right, bottom insets for content area.
// -----------------------------------------------------------------------------
void hud_style_spacing_set_content_rect(int left, int top, int right, int bottom) {
    char* base = (char*)g_building_spacing;
    *(int*)(base + 0x50) = left;
    *(int*)(base + 0x54) = top;
    *(int*)(base + 0x58) = right;
    *(int*)(base + 0x5C) = bottom;
}
