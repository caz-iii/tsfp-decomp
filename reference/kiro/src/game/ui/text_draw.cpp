// Address: 0x8000a644 | Name: text_align_x_offset | Date: 2026-07-23
// Address: 0x8000a6b4 | Name: text_draw_string | Date: 2026-07-23
// Address: 0x8000b7a0 | Name: text_render_glyph_callback | Date: 2026-07-23
// Address: 0x8000b844 | Name: text_draw_at_int_coords | Date: 2026-07-23
// Address: 0x8000b8ec | Name: text_render_string_internal | Date: 2026-07-23
// Address: 0x8000b970 | Name: text_render_colored_callback | Date: 2026-07-23
// Address: 0x8000ba44 | Name: text_draw_string_colored | Date: 2026-07-23
// Address: 0x8000bafc | Name: text_draw_char | Date: 2026-07-23
// Address: 0x8000bb34 | Name: text_draw_char_shadowed | Date: 2026-07-23
// Address: 0x8000bc50 | Name: text_draw_string_shadowed | Date: 2026-07-23
// Address: 0x8000bde0 | Name: text_draw_char_drop_shadow | Date: 2026-07-23
// Address: 0x8000be64 | Name: text_draw_char_outline | Date: 2026-07-23
// Address: 0x8000bfb0 | Name: text_prepend_label | Date: 2026-07-23
//
// Description: Text drawing/rendering functions — the output side of the text subsystem.
//
// text_draw_string (0x8000a6b4): The MAIN text rendering function (~4.4KB).
//   Walks a string character-by-character, computing glyph screen positions,
//   and calling a callback for each glyph/icon to emit. Handles:
//   - Regular glyphs with kerning
//   - Extended Latin characters (accent compositing with multiple passes)
//   - Inline icons (escape sequences \x0B + A-Z or 1-9)
//   - Tab stops, newlines, word alignment
//   - Scale escape sequences (\x0Bs, \x0Bh, \x0Bw)
//   - Clipping against viewport bounds
//   - Monospace vs proportional rendering
//
// text_draw_char (178 xrefs): Simple wrapper for rendering a single string at (x,y)
// text_draw_char_shadowed: Renders text 5 times (4 offsets + foreground) for shadow effect
// text_draw_string_shadowed: Same but for multi-line colored text
// text_draw_char_outline: Renders text with 3x3 outline pattern around glyph
// text_render_glyph_callback: The actual GX render call (clips + draws textured quad)
// text_render_colored_callback: Colored variant with per-vertex colors
//
// Rendering pipeline:
//   text_draw_char → text_render_string_internal → text_draw_string → callback per glyph
//   Callbacks: text_render_glyph_callback (simple) or text_render_colored_callback (colored)
//   Each callback calls FUN_801708b4 / FUN_8001d030 (GX textured quad draw)

#include "common.h"

// text_draw_char (178 xrefs) - renders text at float coords
void text_draw_char(f64 x, f64 y, u8* text, u32 color) {
    f32 fx = (f32)x;
    f32 fy = (f32)y;
    text_render_string_internal(&fx, &fy, text, color);
}
