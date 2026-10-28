// Batch: batch_966 | Module: arcade/challenge editor tooltip / help-text box draw
// Address range: 0x8033ef60 - 0x8033ef78
// Date: 2026-09-12
//
// A small colour helper and the arcade editor's tooltip / help-text box renderer (called from the
// editor toolbar draw FUN_8033f224). Draws an optional leading icon glyph (for a valid palette
// index), the wrapped body text, per-line underlines, and advances the layout cursor. Uses the
// text_* draw/measure helpers and gx_draw_line_i for the underlines.
//
//   color_expand_7bit       0x8033ef60  expand the three high byte-lanes of a colour by 1 bit.
//   aveditor_draw_tooltip_box 0x8033ef78 draw a tooltip/help text box (icon + wrapped text + rules).
//
// NON-MATCHING: aveditor_draw_tooltip_box is emitted with the _savegpr_/_restgpr_ helpers and threads
// an x/y position pair through the spilled r25 pair (uVar7); expressed as plain `int x/y` locals
// seeded from the incoming position args (Compile-Clean rule 3). The pervasive int->double
// magic-constant float conversions are reproduced via CONCAT44. text_draw_char_shadowed /
// text_get_dimensions_int / text_count_lines / text_get_line_n are declared file-locally with
// call-site-accurate types (this TU includes only common.h).

#include "common.h"

extern "C" {

extern int    DAT_80611248;   // "%d" (or icon) format template for the leading glyph
extern float  FLOAT_80611674; // per-line vertical advance
extern double DOUBLE_8061ab70;// int->double bias (signed)

// named cross-module helpers (call-site-accurate types; this TU includes only common.h)
extern void  memset_wrapper(int dst, int value, int len);
extern void  text_draw_char_shadowed(double x, double y, unsigned char* text, unsigned fg, unsigned bg);
extern void  text_get_dimensions_int(int* outW, int* outH, unsigned char* text);
extern unsigned char* text_check_scale(double width, char* text);
extern double text_count_lines(double width, char* text);
extern void  text_get_line_n(unsigned char* text, int line, int* out);
extern void  gx_draw_line_i(unsigned x0, unsigned y0, unsigned x1, unsigned y1, unsigned color);

extern unsigned long long __save_gpr_25(void);
extern void  __restore_gpr_25(void);

// raw FUN_ helpers not yet named
extern void  FUN_80387fb8(unsigned long long f1, unsigned long long r4, unsigned long long r5,
                          unsigned long long r6, unsigned long long r7, unsigned long long r8,
                          unsigned long long r9, unsigned long long r10, int scratch, int fmt,
                          int argA, char* argB, unsigned argC, int argD, unsigned a15, unsigned a16);

} // extern "C"

// -----------------------------------------------------------------------------

// Expand a packed colour: keep the low byte, shift the three upper 7-bit lanes left by one so a
// 0x00-0x7f channel becomes 0x00-0xfe.
unsigned color_expand_7bit(unsigned packed)
{
    return (packed & 0xff) | ((packed & 0x7f7f7f00) << 1);
}

// Draw a tooltip / help-text box. When an icon index is valid (0 <= idx < 0x50, or < 0x2d), format
// and draw the leading glyph (advancing x and shrinking the wrap width). Then draw the wrapped body
// text at (x, y), and when underline is requested draw a horizontal rule under each wrapped line.
void aveditor_draw_tooltip_box(unsigned long long r3, unsigned long long r4, unsigned long long r5,
                               unsigned long long r6, unsigned long long r7, unsigned long long r8,
                               unsigned long long r9, unsigned long long r10, unsigned a9,
                               unsigned a10, int iconIndex, char* text, unsigned wrapWidth,
                               int underline, unsigned a15, unsigned a16)
{
    unsigned long long spill = __save_gpr_25();
    int x = (int)(unsigned)(spill >> 0x20);
    int y = (int)(unsigned)spill;
    if (text == (char*)0) {
        __restore_gpr_25();
        return;
    }

    unsigned char iconBuf[8];
    unsigned char lineBuf[64];

    if ((iconIndex >= 0 && iconIndex < 0x50) || (iconIndex >= 0 && iconIndex < 0x2d)) {
        FUN_80387fb8(r3, r4, r5, r6, r7, r8, r9, r10, (int)(intptr_t)iconBuf,
                     (int)(intptr_t)&DAT_80611248, iconIndex, text, wrapWidth, underline, a15, a16);
        double ix = (double)(float)((double)CONCAT44(0x43300000, (unsigned)y ^ 0x80000000) - DOUBLE_8061ab70);
        double iy = (double)(float)((double)CONCAT44(0x43300000, (unsigned)x ^ 0x80000000) - DOUBLE_8061ab70);
        text_draw_char_shadowed(ix, iy, iconBuf, 0x7f7f7f7f, 0xf1e3250);
        int gw = 0, gh = 0;
        text_get_dimensions_int(&gw, &gh, iconBuf);
        x += gw;
        wrapWidth -= gw;
    }

    double wcheck = (double)(float)((double)CONCAT44(0x43300000, wrapWidth ^ 0x80000000) - DOUBLE_8061ab70);
    unsigned char* wrapped = text_check_scale(wcheck, text);
    double wcount = (double)(float)((double)CONCAT44(0x43300000, wrapWidth ^ 0x80000000) - DOUBLE_8061ab70);
    unsigned lines = (unsigned)text_count_lines(wcount, text);
    int tw = 0, th = 0;
    text_get_dimensions_int(&tw, &th, wrapped);

    double bx = (double)(float)((double)CONCAT44(0x43300000, (unsigned)y ^ 0x80000000) - DOUBLE_8061ab70);
    double by = (double)(float)((double)CONCAT44(0x43300000, (unsigned)x ^ 0x80000000) - DOUBLE_8061ab70);
    text_draw_char_shadowed(bx, by, wrapped, 0x7f7f7f7f, 0xf1e3250);

    if (underline != 0) {
        for (int i = 0; i < (int)lines; i++) {
            memset_wrapper((int)(intptr_t)lineBuf, 0, 0x40);
            text_get_line_n(wrapped, i + 1, (int*)lineBuf);
            int lw = 0, lh = 0;
            text_get_dimensions_int(&lw, &lh, lineBuf);
            float ly = (float)((double)CONCAT44(0x43300000, (unsigned)x ^ 0x80000000) - DOUBLE_8061ab70) +
                       FLOAT_80611674;
            x = (int)ly;
            gx_draw_line_i((unsigned)y, (unsigned)x, (unsigned)(y + lw), (unsigned)x, 0x7f7f7f7f);
        }
    }
    __restore_gpr_25();
}
