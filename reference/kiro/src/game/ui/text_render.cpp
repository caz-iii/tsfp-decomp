// Address: 0x80008c10 | Name: text_set_color_rgb | Date: 2026-07-23
// Address: 0x80008c3c | Name: text_set_alpha | Date: 2026-07-23
// Address: 0x80009244 | Name: text_set_font_table | Date: 2026-07-23
// Address: 0x800092c0 | Name: text_set_shadow_mode | Date: 2026-07-23
// Address: 0x800092d0 | Name: text_set_wrap_mode | Date: 2026-07-23
// Address: 0x800092e0 | Name: text_set_justify_mode | Date: 2026-07-23
// Address: 0x80009914 | Name: text_get_char_width | Date: 2026-07-23
// Address: 0x80009948 | Name: text_get_char_width_scaled | Date: 2026-07-23
// Address: 0x8000997c | Name: text_get_char_height | Date: 2026-07-23
// Address: 0x80009474 | Name: text_string_to_upper | Date: 2026-07-23
// Address: 0x80009144 | Name: text_truncate_ellipsis | Date: 2026-07-23
// Address: 0x80009580 | Name: text_word_wrap | Date: 2026-07-23
// Address: 0x80009d10 | Name: text_measure_string | Date: 2026-07-23
// Address: 0x800092f0 | Name: text_calc_glyph_advance | Date: 2026-07-23
// Address: 0x800087ec | Name: text_ranges_overlap | Date: 2026-07-23
// Address: 0x80008818 | Name: text_auto_select_font | Date: 2026-07-23
// Address: 0x8000892c | Name: text_get_font_variant | Date: 2026-07-23
// Address: 0x80008998 | Name: text_font_available_for_mode | Date: 2026-07-23
// Address: 0x80008a54 | Name: text_system_init | Date: 2026-07-23
// Address: 0x80008b00 | Name: text_system_init_wrapper | Date: 2026-07-23
// Address: 0x80008b24 | Name: text_recalc_dimensions | Date: 2026-07-23
// Address: 0x80008c44 | Name: text_color_fit_width | Date: 2026-07-23
// Address: 0x80008d88 | Name: text_measure_word_wrap | Date: 2026-07-23
// Address: 0x80008f74 | Name: text_apply_width_color | Date: 2026-07-23
// Address: 0x80008fc4 | Name: text_apply_fit_color | Date: 2026-07-23
// Address: 0x80009010 | Name: text_calc_fit_scale | Date: 2026-07-23
// Address: 0x80009084 | Name: text_apply_alpha_fade | Date: 2026-07-23
// Address: 0x800090f0 | Name: text_apply_alpha | Date: 2026-07-23
// Address: 0x80009114 | Name: text_apply_red_scale | Date: 2026-07-23
// Address: 0x80009540 | Name: text_check_scale | Date: 2026-07-23
// Address: 0x80009a5c | Name: text_count_lines | Date: 2026-07-23
// Address: 0x80009ae4 | Name: text_strlen | Date: 2026-07-23
// Address: 0x80009b64 | Name: text_advance_chars | Date: 2026-07-23
// Address: 0x80009bec | Name: text_parse_numeric_escape | Date: 2026-07-23
// Address: 0x80009ca4 | Name: text_get_dimensions_int | Date: 2026-07-23
// Address: 0x800099b0 | Name: text_get_line_n | Date: 2026-07-23
//
// Description: Complete text rendering subsystem for TimeSplitters: Future Perfect.
//
// Architecture:
//   - Font table: 30 entries at DAT_803ea2b8 (stride 0x14), selected by text_set_font_table
//   - Glyph data: per-font at DAT_803ea0f8 (stride 0x38), with kerning tables
//   - Character encoding: ASCII + extended Latin (high-bit = accent lookup via DAT_803ea510)
//   - Escape sequences: \x0B followed by code (s=scale, h=height, w=width, A-Z=inline icon)
//   - Text color: RGB floats at FLOAT_80611664/60/5c, alpha at FLOAT_80611658
//   - Layout: supports word-wrap, justify, multi-line, ellipsis truncation
//
// Key globals:
//   DAT_80611644 = current font index
//   DAT_80611648 = current font descriptor pointer
//   DAT_8061164c = current font metrics pointer
//   DAT_80611638 = alternate font mode flag
//   DAT_80611684 = wrap mode enabled
//   DAT_80611680 = justify mode
//   DAT_80611688 = shadow mode
//   FLOAT_80611670 = computed X scale
//   FLOAT_8061166c = computed Y scale
//   FLOAT_80611668 = computed line height
//
// text_set_font_table (323 xrefs): Selects active font, updates metrics pointers
// text_get_char_width (280 xrefs): Returns pixel width of a string
// text_set_color_rgb (99 xrefs): Sets text RGB color
// text_measure_string: Full text bounding box calculation with font metrics, kerning,
//   inline images, and escape sequences. Returns width in *param_1, height in *param_2.

#include "common.h"

// See raw Ghidra decompilations for full implementations.
// These functions form the complete text layout pipeline:
//   1. text_set_font_table → select font
//   2. text_word_wrap → insert line breaks
//   3. text_measure_string → compute bounding box
//   4. text_set_color_rgb → set color
//   5. text_draw_char → render (in separate file)
