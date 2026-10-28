// Batch: batch_892 | Module: arcade map-editor grid-view render + cell-anchor helpers
// Address range: 0x803170e8 - 0x803196e4
// Date: 2026-09-12
//
// The top-down grid-view renderer for the arcade map editor. This layer sits above the per-member
// sprite render (arcade_edit_render.cpp, batch_891) and below the viewport transform. It walks the
// active member list at DAT_80612c14 (linked via word 0x10) and, for each member whose piece row
// falls on the current grid Z (DAT_80612be4+0x264), draws the member's occupancy cells: an interior
// fill (color chosen by a per-mode palette), the four cell edges (with companion-segment joins so
// adjacent selected cells share a seam), and any corner/diagonal decorations. It then draws the
// static grid background + world-boundary walls, the group/link markers table (DAT_80612be4+0x306e),
// and the paste/clipboard buffer (DAT_80612be4+0x2f7c). A handful of tiny helpers resolve cell
// records and compute text/icon anchor points.
//
//   aveditor_draw_member_grid      0x803170e8  draw one member's grid cells: fill + edges + corners.
//   aveditor_draw_grid_background  0x80318650  fill the view, draw the grid lines + boundary walls.
//   aveditor_render_grid_view      0x80318bcc  walk the member list and draw each member's grid.
//   aveditor_draw_group_markers    0x80318e78  draw the group/link marker glyphs from the 0x306e table.
//   aveditor_draw_paste_buffer     0x803193c0  render the paste/clipboard members at their preview pos.
//   aveditor_cell_record_addr      0x803195a4  member base + index*0x10 + 0x124 -> cell record addr.
//   aveditor_cell_flag_bit         0x803195b8  return bit0 of the record's +8 word (0 for null rec).
//   aveditor_cell_query_flag       0x803195d8  look up a cell record then read its flag bit.
//   aveditor_text_anchor_from_cell 0x803195fc  compute a text label's (x,y) pixel anchor for a cell.
//   aveditor_icon_anchor_from_cell 0x803196e4  compute an icon's (x,y) pixel anchor for a cell.
//
// NON-MATCHING notes:
//  * aveditor_draw_member_grid arrives with the PPC calling convention's full FP-arg spill: the
//    decompiler renders 8 incoming doubles (param_2..param_8) of which only a few carry meaning
//    (the world x/y origin and the current scale). The call sites in aveditor_render_grid_view /
//    aveditor_draw_paste_buffer pass the members' float coords; the extra doubles are register
//    residue. They are kept in the signature so the (matching) call sites compile unchanged.
//  * The int<->float 0x43300000 / DOUBLE_8061a6f0 magic-double conversion idiom is expressed with
//    the shared CONCAT44 intrinsic from common.h and plain (float)(int) casts.
//  * The compiler _savegpr_/_restgpr_ runtime spill helpers (FUN_803826c8 / FUN_80382714 /
//    FUN_803826d8 / FUN_80382724 / FUN_803826f0 / FUN_8038273c / __save_gpr_28 / __restore_gpr_28)
//    are preserved as opaque calls; where one folds the incoming r3/r4 register pair into an
//    `undefined8` return (FUN_803826c8 / __save_gpr_28), the real values are recovered as locals.
//  * Several named helpers (aveditor_draw_grid_rect, aveditor_draw_grid_quad, aveditor_tick_pulse,
//    aveditor_draw_selection_frame, aselgrid_find_companion_segment, roster_hit_test_point,
//    aveditor_render_member_cells, leaf_31e8, gx_*) resolve through the shared generated headers.
//    Some of those generated prototypes declare a void return even though the original reads a
//    result register; to keep this translation compile-clean without fighting the shared header,
//    those results are captured through thin file-local __cdecl wrappers declared below.
//
// Raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols get correctly-typed per-file externs below.

#include "common.h"
#include "hud.h"

extern "C" {

// -- editor context + palette / geometry constants --
extern int    DAT_80612be4;     // editor context base (mode +0x254, gridZ +0x264, sel ptrs +0x2f18/+0x2f7c/+0x2f80)
extern float* DAT_80612c14;     // active member list head (next at word 0x10)
extern int    DAT_80612c88;     // pixel origin x (magic-double)
extern int    DAT_80612c84;     // pixel origin y (magic-double)
extern int    DAT_80612c7c;     // grid span x
extern int    DAT_80612c80;     // grid span y
extern float  FLOAT_80612c78;   // world origin x
extern float  FLOAT_80612c74;   // world origin y
extern float  FLOAT_80612c68;   // world extent x
extern float  FLOAT_80612c64;   // world extent y
extern float  FLOAT_80612c60;   // world->pixel x scale
extern float  FLOAT_80612c5c;   // world->pixel y scale
extern float  FLOAT_80612c58;   // inverse x scale
extern float  FLOAT_80612c54;   // inverse y scale
extern int    DAT_806109c4;     // view width
extern int    DAT_806109c8;     // view height
extern int    DAT_80611654;     // "measure text width" flag
extern unsigned char DAT_80611a50; // palette alpha low byte
extern unsigned char DAT_80611a51; // palette alpha high byte
extern float  FLOAT_80611a64;   // text anchor scale
extern float  FLOAT_80611a68;   // icon anchor scale
extern int    DAT_80612c04;     // (unused here) member free count

// grid marker table lives inside the context at +0x3070.. (short pairs, byte flags at +0x3076..)

// line-color ramp source table (three 0x10-stride RGBA rows at 0x8047fc5c)
extern int DAT_8047fc5c;   // row red
extern int DAT_8047fc60;   // row green
extern int DAT_8047fc64;   // row blue
extern int DAT_8047fc68;   // row alpha driver

// -- conversion + layout constants --
extern double DOUBLE_8061a6f0;  // int->float conversion bias (0x43300000 magic)
extern double DOUBLE_8061a780;  // int->float conversion bias (marker table variant)
extern float  FLOAT_8061a6e8;   // 1.0 (cell size)
extern float  FLOAT_8061a70c;   // 0.5 (half)
extern float  FLOAT_8061a710;   // 0.0 sentinel
extern float  FLOAT_8061a724;   // edge inset
extern float  FLOAT_8061a728;   // dash step
extern float  FLOAT_8061a734;   // dash length
extern float  FLOAT_8061a788;   // edge thickness
extern float  FLOAT_8061a78c;   // corner extent
extern float  FLOAT_8061a790;   // world max coord
extern float  FLOAT_8061a794;   // marker glyph size A
extern float  FLOAT_8061a798;   // marker glyph size B
extern float  FLOAT_8061a79c;   // marker arg c
extern float  FLOAT_8061a7a0;   // marker arg d
extern float  FLOAT_8061a7a4;   // marker arg e
extern float  FLOAT_8061a7a8;   // text anchor x scale
extern float  FLOAT_8061a7ac;   // text anchor y base
extern float  FLOAT_8061a7b0;   // icon anchor scale

// -- raw FUN_ helpers not yet individually named --
// color_scale_lo / text_get_char_width resolve via generated_externs.h (permissive `int name(...)`);
// text_get_char_height resolves via generated_decls.h (f32 text_get_char_height(u8*)); do not
// redeclare them here (avoids C2371/overload conflicts).
extern unsigned int FUN_803825c0(double v);          // round-to-int / grid-quantize
extern unsigned int FUN_80342204(int member, unsigned int gridZ); // per-member visibility test

// compiler _savegpr_/_restgpr_ spill helpers (opaque)
extern unsigned long long FUN_803826c8(void);  // save spill; folds r3/r4 grid record pair
extern void FUN_80382714(void);                // restore spill
extern void FUN_803826d8(void);                // save spill
extern void FUN_80382724(void);                // restore spill
extern void FUN_803826f0(void);                // save spill
extern void FUN_8038273c(void);                // restore spill
extern unsigned long long __save_gpr_28(void); // save r28..; folds an out-pointer pair
extern void __restore_gpr_28(void);
extern void FUN_8031a38c(double cx, double cy, double a, double b, double c, double d, double e); // marker arc cap
} // extern "C"

// Named grid-draw helpers (aveditor_draw_grid_rect/_quad, aselgrid_find_companion_segment,
// roster_hit_test_point, aveditor_render_member_cells, aveditor_draw_selection_frame,
// aveditor_tick_pulse, leaf_31e8) all resolve through generated_decls.h and are NOT redeclared
// here. A few are declared there with a void return although the original reads a result register;
// the thin function-pointer wrappers below recover that result without fighting the header.

// Thin wrappers that recover the result register the shared void-prototypes discard.
static inline int aselgrid_companion(int rec, unsigned int px, unsigned int py, int pz,
                                     unsigned int mode, unsigned int flag)
{
    typedef int (*fn_t)(int, unsigned int, unsigned int, int, unsigned int, unsigned int);
    return ((fn_t)(void*)&aselgrid_find_companion_segment)(rec, px, py, pz, mode, flag);
}
static inline unsigned int roster_hit(unsigned long long gpr, double y, double z)
{
    typedef unsigned int (*fn_t)(unsigned long long, double, double);
    return ((fn_t)(void*)&roster_hit_test_point)(gpr, y, z);
}
static inline unsigned int __grid_pulse_color_val(void)
{
    typedef unsigned int (*fn_t)(void);
    return ((fn_t)(void*)&aveditor_tick_pulse)();
}

// -----------------------------------------------------------------------------
// Draw one member's occupancy cells into the grid view: an interior fill (palette selected by
// mode), the four dashed cell edges with companion-segment joins so adjacent selected cells merge,
// and the corner/diagonal decorations. gridX/gridY are the member's world origin; scale is the
// current zoom. mode picks the fill palette. member points at the member record; the piece layout
// (rows/cols, cell bitmap) lives at member[0xb].
void aveditor_draw_member_grid(unsigned long long member_pair, double gridX, double scale,
                               double gridY, double d5, double d6, double d7, double d8,
                               double d9, unsigned int pieceArg, unsigned int mode, float* member,
                               unsigned int drawMarkers, unsigned int r13, unsigned int r14,
                               unsigned int r15, int r16)
{
    unsigned int fillA, fillB, fillC, cellFlags, hit;
    int piece, rows, cols, i, j;
    unsigned long long spill;
    double cy, cyTop, cx, cxRight, x0, y0;
    double dashLen, dashStep, one, edge, t;
    unsigned char* rec;

    (void)member_pair; (void)d5; (void)d6; (void)d7; (void)d8; (void)d9; (void)pieceArg; (void)r16;

    spill = FUN_803826c8();
    piece = (int)(spill >> 0x20);        // piece-layout record (r4 half of the spill pair)
    int mode0 = (int)spill;              // active edit mode (r3 half)

    // Select the interior fill palette for this mode.
    switch (mode0) {
    default: fillA = color_scale_lo(scale, 0x5a5a5a80); fillB = color_scale_lo(scale, 0x2828285a); fillC = color_scale_lo(scale, 0x1414145a); break;
    case 1:  fillA = color_scale_lo(scale, DAT_80611a51 | 0x54545400); fillB = color_scale_lo(scale, DAT_80611a51 | 0x32323200); fillC = color_scale_lo(scale, DAT_80611a51 | 0x1e1e1e00); break;
    case 2:  fillA = color_scale_lo(scale, DAT_80611a50 | 0x503c5a00); fillB = color_scale_lo(scale, DAT_80611a50 | 0x3c324600); fillC = color_scale_lo(scale, DAT_80611a50 | 0x0f0f0f00); break;
    case 3:  fillA = color_scale_lo(scale, DAT_80611a50 | 0x7f323200); fillB = color_scale_lo(scale, DAT_80611a50 | 0x3c141400); fillC = color_scale_lo(scale, DAT_80611a50 | 0x28000000); break;
    case 4:  fillA = color_scale_lo(scale, DAT_80611a50 | 0x007f7f00); fillB = color_scale_lo(scale, DAT_80611a50 | 0x003f3f00); fillC = color_scale_lo(scale, DAT_80611a50 | 0x002b2b00); break;
    case 5:  fillA = color_scale_lo(scale, DAT_80611a51 | 0x647f7f00); fillB = color_scale_lo(scale, DAT_80611a51 | 0x003f3f00); fillC = color_scale_lo(scale, DAT_80611a51 | 0x002b2b00); break;
    case 6:  fillA = color_scale_lo(scale, 0x3232325a); fillB = color_scale_lo(scale, 0x1414145a); fillC = color_scale_lo(scale, 0x0000003c); break;
    case 7:  fillA = color_scale_lo(scale, 0x507f5040); fillB = color_scale_lo(scale, 0x1e321e40); fillC = color_scale_lo(scale, 0x0a1e0a40); break;
    case 8:  fillA = color_scale_lo(scale, 0x40404064); fillB = color_scale_lo(scale, 0x1e1e1e2d); fillC = color_scale_lo(scale, 0x0f0f0f2d); break;
    case 9:  fillA = color_scale_lo(scale, 0x5a5a5a78); fillB = color_scale_lo(scale, 0x3c3c3c5a); fillC = color_scale_lo(scale, 0x00000001); break;
    }

    // Grid Z row for this member (clamped to the piece's row count).
    unsigned int zrow = (unsigned int)(*(int*)(DAT_80612be4 + 0x264) - (int)member[2]);
    if (*(unsigned int*)(piece + 0x1c) <= zrow) zrow = *(unsigned int*)(piece + 0x1c) - 1;

    one = (double)FLOAT_8061a6e8;
    int stride = (int)(zrow * 4 + 0x20);

    // --- interior fill pass ---
    rows = *(int*)(piece + 0x18);
    for (i = 0; i < rows; i = i + 1) {
        cy = (double)(float)(gridX + (double)(float)((double)(int)i - DOUBLE_8061a6f0));
        cyTop = (double)(float)(one + cy);
        cols = *(int*)(piece + 0x14);
        for (j = 0; j < cols; j = j + 1) {
            cellFlags = *(unsigned int*)(*(int*)(piece + stride) + (unsigned int)(j + i * cols) * 4);
            if ((cellFlags & 0xffff) == 0) continue;
            cx = (double)(float)(scale + (double)(float)((double)(int)j - DOUBLE_8061a6f0));

            unsigned int vis = 1;
            if (DAT_80612c14 != 0) vis = FUN_80342204((int)member, zrow);
            if (vis == 0) {
                aveditor_draw_grid_rect(fillC, cx, cy, (double)(float)((double)FLOAT_8061a6e8 + cx), cyTop);
                continue;
            }

            if (((cellFlags & 0x100) != 0) || ((cellFlags & 0x400) != 0)) {
                // hatched fill (horizontal dashes)
                aveditor_draw_grid_rect(fillC, cx, cy, (double)(float)((double)FLOAT_8061a6e8 + cx), cyTop);
                dashLen = (double)FLOAT_8061a734; dashStep = (double)FLOAT_8061a728;
                edge = (double)FLOAT_8061a6e8;
                for (t = (double)FLOAT_8061a710; t < edge; t = (double)(float)(t + dashStep)) {
                    aveditor_draw_grid_rect(fillB, (double)(float)(cx + t), cy,
                                            (double)(float)(dashLen + (double)(float)(cx + t)), cyTop);
                }
            } else if (((cellFlags & 0x800) != 0) || ((cellFlags & 0x200) != 0)) {
                // hatched fill (vertical dashes)
                double xr = (double)(float)((double)FLOAT_8061a6e8 + cx);
                aveditor_draw_grid_rect(fillC, cx, cy, xr, cyTop);
                dashLen = (double)FLOAT_8061a734; dashStep = (double)FLOAT_8061a728;
                edge = (double)FLOAT_8061a6e8;
                for (t = (double)FLOAT_8061a710; t < edge; t = (double)(float)(t + dashStep)) {
                    aveditor_draw_grid_rect(fillB, cx, (double)(float)(cy + t), xr,
                                            (double)(float)(dashLen + (double)(float)(cy + t)));
                }
            } else if ((cellFlags & 0x80) != 0) {
                aveditor_draw_grid_rect(fillA, cx, cy, (double)(float)((double)FLOAT_8061a6e8 + cx), cyTop);
            } else if ((cellFlags & 0x1000) != 0) {
                aveditor_draw_grid_rect(fillC, cx, cy, (double)(float)((double)FLOAT_8061a6e8 + cx), cyTop);
            } else {
                aveditor_draw_grid_rect(fillB, cx, cy, (double)(float)((double)FLOAT_8061a6e8 + cx), cyTop);
            }
        }
    }

    // --- edge + corner decoration pass ---
    double thick = (double)FLOAT_8061a788;
    double corner = (double)FLOAT_8061a78c;
    one = (double)FLOAT_8061a6e8;
    for (i = 0; i < *(int*)(piece + 0x18); i = i + 1) {
        y0 = (double)(float)(gridX + (double)(float)((double)(int)i - DOUBLE_8061a6f0));
        double cellTop = (double)(float)(one + y0);
        double thickTop = (double)(float)(thick + y0);
        double cornTop = (double)(float)(corner + y0);
        double topInset = (double)(float)(cellTop - thick);
        double botOut = (double)(float)(corner + cellTop);
        double botIn = (double)(float)(cellTop - corner);
        cols = *(int*)(piece + 0x14);
        for (j = 0; j < cols; j = j + 1) {
            cellFlags = *(unsigned int*)(*(int*)(piece + stride) + (unsigned int)(j + i * cols) * 4);
            if (cellFlags == 0) continue;
            bool edgeTop = false, edgeBot = false;
            x0 = (double)(float)(scale + (double)(float)((double)(int)j - DOUBLE_8061a6f0));

            // top edge
            if (((cellFlags & 1) == 0) &&
                ((i == 0) || (*(int*)(*(int*)(piece + stride) + (unsigned int)(j + (i - 1) * cols) * 4) == 0))) {
                aveditor_draw_grid_rect(fillA, x0, y0, (double)(float)((double)FLOAT_8061a6e8 + x0), thickTop);
                edgeTop = true; edgeBot = true;
            }
            // right edge
            bool edgeRight = false;
            if (((cellFlags & 2) == 0) &&
                (((unsigned int)j == (unsigned int)cols - 1) ||
                 (*(int*)(*(int*)(piece + stride) + (unsigned int)(j + i * cols + 1) * 4) == 0))) {
                float startY = FLOAT_8061a710;
                if (edgeTop) startY = FLOAT_8061a788;
                aveditor_draw_grid_rect(fillA,
                    (double)(float)((double)(float)((double)FLOAT_8061a6e8 + x0) - (double)FLOAT_8061a788),
                    (double)(float)(y0 + (double)startY),
                    (double)(float)((double)FLOAT_8061a6e8 + x0), cellTop);
                edgeRight = true;
            }
            // bottom edge
            bool edgeLeft = false;
            if (((cellFlags & 4) == 0) &&
                ((i == *(int*)(piece + 0x18) - 1) ||
                 (*(int*)(*(int*)(piece + stride) + (unsigned int)(j + (i + 1) * cols) * 4) == 0))) {
                float endX = FLOAT_8061a710;
                if (edgeRight) endX = FLOAT_8061a788;
                aveditor_draw_grid_rect(fillA, x0, topInset,
                    (double)((float)((double)FLOAT_8061a6e8 + x0) - endX), cellTop);
                edgeLeft = true;
            }
            // left edge
            if (((cellFlags & 8) == 0) &&
                ((j == 0) ||
                 (*(int*)(*(int*)(piece + stride) + (unsigned int)(j + i * cols - 1) * 4) == 0))) {
                float botPad = FLOAT_8061a710; if (edgeLeft) botPad = FLOAT_8061a788;
                float topPad = FLOAT_8061a710; if (edgeBot) topPad = FLOAT_8061a788;
                aveditor_draw_grid_rect(fillA, x0, (double)(float)(y0 + (double)topPad),
                    (double)(float)((double)FLOAT_8061a788 + x0),
                    (double)(float)(cellTop - (double)botPad));
            }

            // corner / diagonal decorations
            if ((cellFlags & 0x10000) != 0) {
                aveditor_draw_grid_rect(fillA, x0, (double)(float)(y0 - (double)FLOAT_8061a78c),
                    (double)(float)((double)FLOAT_8061a6e8 + x0), cornTop);
            }
            if ((cellFlags & 0x20000) != 0) {
                aveditor_draw_grid_rect(fillA,
                    (double)((float)((double)FLOAT_8061a6e8 + x0) - FLOAT_8061a78c), y0,
                    (double)(FLOAT_8061a78c + (float)((double)FLOAT_8061a6e8 + x0)), cellTop);
            }
            if ((cellFlags & 0x40000) != 0) {
                aveditor_draw_grid_rect(fillA, x0, botIn, (double)(float)((double)FLOAT_8061a6e8 + x0), botOut);
            }
            if ((cellFlags & 0x80000) != 0) {
                aveditor_draw_grid_rect(fillA,
                    (double)(float)(x0 - (double)FLOAT_8061a78c), y0,
                    (double)(float)((double)FLOAT_8061a78c + x0), cellTop);
            }
            if ((cellFlags & 0x100000) != 0) {
                double c = (double)FLOAT_8061a78c;
                double xr = (double)(float)((double)FLOAT_8061a6e8 + x0);
                double xc = (double)(float)(c + x0);
                double xrc = (double)(float)(c + xr);
                aveditor_draw_grid_quad((double)(float)(x0 - c), y0, xc, (double)(float)(y0 - c),
                                        xr, botOut, xrc, botIn);
                aveditor_draw_grid_rect(fillA, x0, (double)(float)(y0 - (double)FLOAT_8061a78c), xc, cornTop);
                aveditor_draw_grid_rect(fillA, xr, botIn, xrc, botOut);
            }
            if ((cellFlags & 0x200000) != 0) {
                double c = (double)FLOAT_8061a78c;
                double xr = (double)(float)((double)FLOAT_8061a6e8 + x0);
                double xc = (double)(float)(c + x0);
                double xrc = (double)(float)(c + xr);
                aveditor_draw_grid_quad(xr, (double)(float)(y0 - c), xrc, cornTop,
                                        (double)(float)(x0 - c), cellTop, xc, botOut);
                aveditor_draw_grid_rect(fillA, (double)(float)(xr - (double)FLOAT_8061a78c), y0, xrc, cornTop);
                aveditor_draw_grid_rect(fillA, x0, botIn, xc, botOut);
            }
        }
    }

    // --- marker / companion-segment pass (only when requested) ---
    if (drawMarkers != 0) {
        int off = 0;
        double markScale = (double)(float)((double)FLOAT_8061a70c * scale);
        int markerCount = *(int*)(piece + 0x44);
        for (int k = 0; k < markerCount; k = k + 1) {
            rec = (unsigned char*)(piece + off + 0x48);
            unsigned int gz = *(unsigned int*)(DAT_80612be4 + 0x264);
            unsigned int rowGz = FUN_803825c0((double)((float)((double)(int)gz - DOUBLE_8061a6f0) - member[2]));
            if ((rec[3] == rowGz) && (rec[5] != 2)) {
                double mx = scale + (double)(float)((double)(int)rec[1] - DOUBLE_8061a780);
                int mxi = (int)mx;
                double my = gridX + (double)(float)((double)(int)rec[2] - DOUBLE_8061a780);
                int myi = (int)my;
                double mz = (double)(float)((double)(int)(gz ^ 0x80000000) - DOUBLE_8061a6f0);
                unsigned int mcol;
                if (mode0 == 9)      mcol = color_scale_lo(markScale, 0x645f7f5a);
                else if (mode0 == 8) mcol = color_scale_lo(markScale, 0x645f7f78);
                else if (mode0 == 6) mcol = color_scale_lo(markScale, 0x645f7f32);
                else                 mcol = color_scale_lo(markScale, 0x645f7f64);

                unsigned int pz = (unsigned int)(member[2] + (float)((double)(int)rec[3] - DOUBLE_8061a780));
                unsigned int glyphType = rec[0];
                // The four directional glyph cases share the same structure: hit-test the anchor,
                // try to join a companion segment, otherwise stamp the marker bar(s).
                if (glyphType == 3) {
                    hit = roster_hit((double)(float)mx, (double)(float)my, mz);
                    if ((hit & 1) != 0 &&
                        aselgrid_companion(*(int*)(DAT_80612be4 + 0x2f80), mxi, myi, pz, glyphType, rec[4]) >= 0)
                        goto next_marker;
                    aveditor_draw_grid_rect(mcol, (double)(float)mx,
                        (double)(float)((double)(float)my - (double)FLOAT_8061a78c),
                        (double)(float)((double)FLOAT_8061a6e8 + (double)(float)mx), (double)(float)my);
                } else if (glyphType == 1) {
                    hit = roster_hit((double)(float)mx, (double)(float)((double)(float)my - (double)FLOAT_8061a6e8), mz);
                    if ((hit & 4) != 0 &&
                        aselgrid_companion(*(int*)(DAT_80612be4 + 0x2f80), mxi, myi, pz, glyphType, rec[4]) >= 0)
                        goto next_marker;
                    aveditor_draw_grid_rect(mcol, (double)(float)mx, (double)(float)my,
                        (double)(float)((double)FLOAT_8061a6e8 + (double)(float)mx),
                        (double)(float)((double)FLOAT_8061a78c + (double)(float)my));
                } else if (glyphType == 2) {
                    hit = roster_hit((double)(float)mx, (double)(float)my, mz);
                    if ((hit & 8) != 0 &&
                        aselgrid_companion(*(int*)(DAT_80612be4 + 0x2f80), mxi, myi, pz, glyphType, rec[4]) >= 0)
                        goto next_marker;
                    aveditor_draw_grid_rect(mcol,
                        (double)(float)((double)(float)mx - (double)FLOAT_8061a78c), (double)(float)my,
                        (double)(float)mx, (double)(float)((double)(float)my + (double)FLOAT_8061a78c));
                } else if (glyphType == 4) {
                    hit = roster_hit((double)(float)((double)(float)mx - (double)FLOAT_8061a6e8), (double)(float)my, mz);
                    if ((hit & 2) != 0 &&
                        aselgrid_companion(*(int*)(DAT_80612be4 + 0x2f80), mxi, myi, pz, glyphType, rec[4]) >= 0)
                        goto next_marker;
                    aveditor_draw_grid_rect(mcol, (double)(float)mx, (double)(float)my,
                        (double)(float)((double)FLOAT_8061a78c + (double)(float)mx),
                        (double)(float)((double)FLOAT_8061a6e8 + (double)(float)my));
                }
            }
        next_marker:
            off = off + 6;
        }
    }

    // --- selection / member-cell overlay pass ---
    if ((mode0 != 8) && (mode0 != 9) && (mode0 != 10) && (mode0 != 6)) {
        unsigned int selMode = *(unsigned int*)(DAT_80612be4 + 0x254);
        if (((selMode == 1) || ((selMode == 4) && (*(int*)(DAT_80612be4 + 0x1185c) != 6)) || (selMode == 3)) &&
            (member != (float*)0)) {
            aveditor_render_member_cells((int*)member, selMode, DAT_80612be4, (int)r13, r14, r15, (int)r16);
        } else if (((selMode == 2) || ((selMode == 4) && (*(int*)(DAT_80612be4 + 0x1185c) == 6)))) {
            double gx = (double)((float)((double)(float)((double)(int)(*(int*)(piece + 0x14) ^ 0x80000000) - DOUBLE_8061a6f0) *
                                        (double)FLOAT_8061a70c + scale) - FLOAT_8061a724);
            double gy = (double)((float)((double)(float)((double)(int)(*(int*)(piece + 0x18) ^ 0x80000000) - DOUBLE_8061a6f0) *
                                        (double)FLOAT_8061a70c + gridX) - FLOAT_8061a724);
            unsigned int pulse = __grid_pulse_color_val();
            double gx1 = (double)(float)((double)FLOAT_8061a70c + gx);
            double gy1 = (double)(float)((double)FLOAT_8061a70c + gy);
            aveditor_draw_grid_rect(0x78787880, gx, gy, gx1, gy1);
            double in = (double)FLOAT_8061a78c;
            aveditor_draw_grid_rect(pulse, (double)(float)(in + gx), (double)(float)(in + gy),
                                    (double)(float)(gx1 - in), (double)(float)(gy1 - in));
        }
    }

    FUN_80382714();
}

// -----------------------------------------------------------------------------
// Fill the whole view, draw the grid lines (with a 3-color period so every 2nd/3rd line is brighter)
// and finally the four world-boundary walls.
void aveditor_draw_grid_background(void)
{
    unsigned int lineColor[4];
    double x0f, y0f, xr, yb, xc, yc, one, bias;
    int i, tmp, lo;

    FUN_803826d8();
    gx_set_sub_material_id(1);
    gx_set_blend_mode(0);
    gx_fill_rect_clipped(0, 0, (int)DAT_806109c4, (int)DAT_806109c8, 0x07060c7f);
    gx_set_sub_material_id(2);

    xr = (double)FLOAT_80612c78;
    yb = (double)FLOAT_80612c74;
    xc = (double)(float)(xr + (double)FLOAT_80612c68);
    yc = (double)(float)(yb + (double)FLOAT_80612c64);
    bias = DOUBLE_8061a6f0;
    x0f = (double)(float)((double)(int)((int)FLOAT_80612c78 ^ 0x80000000) - bias);
    y0f = (double)(float)((double)(int)((int)FLOAT_80612c74 ^ 0x80000000) - bias);

    // build the 3-entry line color ramp
    int spanX = (int)(xc - x0f);
    lo = (((spanX * DAT_80612c7c + (int)(yc - y0f) * DAT_80612c80) - (int)(yc - y0f) * spanX) * 0x100) /
         (DAT_80612c80 * DAT_80612c7c * 2);
    int rgbOff = 0, cIdx = 0;
    for (i = 0; i < 3; i = i + 1) {
        int* rgb = (int*)((int)&DAT_8047fc5c + rgbOff);
        int a3 = *(int*)((int)&DAT_8047fc68 + rgbOff);
        unsigned int alpha = FUN_803825c0((double)(*(float*)(DAT_80612be4 + 0x22c) * (float)((double)(int)(a3 ^ 0x80000000) - bias)));
        int r2 = *(int*)((int)&DAT_8047fc64 + rgbOff);
        int g2 = *(int*)((int)&DAT_8047fc60 + rgbOff);
        lineColor[cIdx] = (alpha & 0xff) | ((unsigned int)(r2 - lo) * 0x100 & 0xff00U) |
                          ((unsigned int)(rgb[0] - lo) * 0x1000000) | ((unsigned int)(g2 - lo) * 0x10000 & 0xff0000U);
        rgbOff = rgbOff + 0x10; cIdx = cIdx + 1;
    }

    // vertical grid lines
    one = (double)FLOAT_8061a6e8;
    for (double xf = x0f; xf < xc; xf = (double)(float)(xf + one)) {
        int xi = (int)xf;
        unsigned int s = (unsigned int)(xi >> 0x1f);
        unsigned int q = s * 4 | ((unsigned int)(xi * 0x40000000 + s) >> 0x1e);
        tmp = (int)q - (int)s;
        unsigned int idx = ((tmp == 2) || (tmp == -2)) ? 2u : (unsigned int)(q != s);
        int px = (int)(FLOAT_80612c60 * (float)(xf - (double)FLOAT_80612c78) +
                       (float)((double)(int)(DAT_80612c88 ^ 0x80000000) - bias));
        int py = (int)(FLOAT_80612c5c * (float)(yb - (double)FLOAT_80612c74) +
                       (float)((double)(int)(DAT_80612c84 ^ 0x80000000) - bias));
        unsigned int py2 = (unsigned int)(FLOAT_80612c5c *
                       (float)((double)(float)(yb + (double)FLOAT_80612c64) - (double)FLOAT_80612c74) +
                       (float)((double)(int)(DAT_80612c84 ^ 0x80000000) - bias));
        gx_draw_line_counted(px, py, px, py2, lineColor[idx]);
    }
    // horizontal grid lines
    for (double yf = y0f; yf < yc; yf = (double)(float)(yf + one)) {
        int yi = (int)yf;
        unsigned int s = (unsigned int)(yi >> 0x1f);
        unsigned int q = s * 4 | ((unsigned int)(yi * 0x40000000 + s) >> 0x1e);
        tmp = (int)q - (int)s;
        unsigned int idx = ((tmp == 2) || (tmp == -2)) ? 2u : (unsigned int)(q != s);
        int py = (int)(FLOAT_80612c5c * (float)(yf - (double)FLOAT_80612c74) +
                       (float)((double)(int)(DAT_80612c84 ^ 0x80000000) - bias));
        int px = (int)(FLOAT_80612c60 * (float)(xr - (double)FLOAT_80612c78) +
                       (float)((double)(int)(DAT_80612c88 ^ 0x80000000) - bias));
        unsigned int px2 = (unsigned int)(FLOAT_80612c60 *
                       (float)((double)(float)(xr + (double)FLOAT_80612c68) - (double)FLOAT_80612c78) +
                       (float)((double)(int)(DAT_80612c88 ^ 0x80000000) - bias));
        gx_draw_line_counted(px, py, px2, py, lineColor[idx]);
    }

    // world-boundary walls (west / east / north / south)
    gx_set_sub_material_id(1);
    double ox = (double)FLOAT_80612c78;
    if ((float)((double)(int)FUN_803825c0(-ox) - DOUBLE_8061a780) < FLOAT_80612c68) {
        aveditor_draw_grid_rect(0x80, ox, (double)FLOAT_80612c74, (double)FLOAT_8061a710,
            (double)(float)((double)FLOAT_80612c74 + (double)FLOAT_80612c64));
    }
    ox = (double)FLOAT_80612c78;
    if ((double)(float)((double)(int)FUN_803825c0((double)(float)((double)FLOAT_8061a790 - ox)) - DOUBLE_8061a780) < (double)FLOAT_80612c68) {
        aveditor_draw_grid_rect(0x80, (double)FLOAT_8061a790, (double)FLOAT_80612c74,
            (double)(float)(ox + (double)FLOAT_80612c68),
            (double)(float)((double)FLOAT_80612c74 + (double)FLOAT_80612c64));
    }
    double oy = (double)FLOAT_80612c74;
    if ((float)((double)(int)FUN_803825c0(-oy) - DOUBLE_8061a780) < FLOAT_80612c64) {
        aveditor_draw_grid_rect(0x80, (double)FLOAT_80612c78, oy,
            (double)(float)((double)FLOAT_80612c78 + (double)FLOAT_80612c68), (double)FLOAT_8061a710);
    }
    oy = (double)FLOAT_80612c74;
    if ((double)(float)((double)(int)FUN_803825c0((double)(float)((double)FLOAT_8061a790 - oy)) - DOUBLE_8061a780) < (double)FLOAT_80612c64) {
        aveditor_draw_grid_rect(0x80, (double)FLOAT_80612c78, (double)FLOAT_8061a790,
            (double)(float)((double)FLOAT_80612c78 + (double)FLOAT_80612c68),
            (double)(float)(oy + (double)FLOAT_80612c64));
    }
    FUN_80382724();
}

// -----------------------------------------------------------------------------
// Walk the active member list twice: first the members below the current grid Z (drawn without
// markers), then those at/above it (drawn with markers + selection highlight). Draws the paste
// selection frame in paste mode.
void aveditor_render_grid_view(void)
{
    bool baseMode;
    unsigned int hl;
    int gridZ;
    unsigned int r7 = 0, r8 = 0, r9 = 0; int r10 = 0;
    float* m;
    double x, y, ax, ay, ox, in7 = 0.0, in8 = 0.0;

    FUN_803826f0();
    baseMode = false;
    int selMode = *(int*)(DAT_80612be4 + 0x254);
    gridZ = *(int*)(DAT_80612be4 + 0x264);
    if ((selMode == 0) || (selMode == 2) || ((*(int*)(DAT_80612be4 + 0x1185c) == 6) && (selMode == 4)))
        baseMode = true;

    // pass 1: members strictly below the current layer
    *(int*)(DAT_80612be4 + 0x264) = *(int*)(DAT_80612be4 + 0x268);
    for (m = DAT_80612c14; m != (float*)0; m = (float*)(intptr_t)m[0x10]) {
        if (*(int*)((int)m[0xb] + 0x1c) == gridZ - (int)m[2]) {
            float piece = m[(int)m[3] + 0xb];
            ox = (double)FLOAT_80612c78;
            x = (double)*m; y = (double)m[1];
            ax = (double)(float)(x + (double)(float)((double)(int)(*(int*)((int)piece + 0x14) ^ 0x80000000) - DOUBLE_8061a6f0));
            ay = (double)(float)(y + (double)(float)((double)(int)(*(int*)((int)piece + 0x18) ^ 0x80000000) - DOUBLE_8061a6f0));
            if ((x < (double)(float)(ox + (double)FLOAT_80612c68)) &&
                (y < (double)(float)((double)FLOAT_80612c74 + (double)FLOAT_80612c64)) &&
                (ox <= ax) && ((double)FLOAT_80612c74 <= ay)) {
                aveditor_draw_member_grid(0, x, y, (double)FLOAT_8061a6e8, ax, ay, ox, in7,
                                          in8, (unsigned int)piece, 6, m, 1, r7, r8, r9, r10);
            }
        }
    }

    // pass 2: members at/above the current layer, with markers + selection state
    *(int*)(DAT_80612be4 + 0x264) = gridZ;
    *(unsigned int*)(DAT_80612be4 + 0x25c) = *(unsigned int*)(DAT_80612be4 + 0x25c) | 0x800000;
    for (m = DAT_80612c14; m != (float*)0; m = (float*)(intptr_t)m[0x10]) {
        if ((unsigned int)(*(int*)(DAT_80612be4 + 0x264) - (int)m[2]) < *(unsigned int*)((int)m[0xb] + 0x1c)) {
            float piece = m[(int)m[3] + 0xb];
            ox = (double)FLOAT_80612c78;
            x = (double)*m; y = (double)m[1];
            ax = (double)(float)(x + (double)(float)((double)(int)(*(int*)((int)piece + 0x14) ^ 0x80000000) - DOUBLE_8061a6f0));
            ay = (double)(float)(y + (double)(float)((double)(int)(*(int*)((int)piece + 0x18) ^ 0x80000000) - DOUBLE_8061a6f0));
            if ((x < (double)(float)(ox + (double)FLOAT_80612c68)) &&
                (y < (double)(float)((double)FLOAT_80612c74 + (double)FLOAT_80612c64)) &&
                (ox <= ax) && ((double)FLOAT_80612c74 <= ay)) {
                hl = 0;
                if ((m == *(float**)(DAT_80612be4 + 0x2f18)) && baseMode) hl = 1;
                else if (m == *(float**)(DAT_80612be4 + 0x2f18)) hl = 7;
                if (((unsigned int)m[5] & 0xa000000) != 0) hl = hl | 4;
                aveditor_draw_member_grid(0, x, y, (double)FLOAT_8061a6e8, ax, ay, ox, in7,
                                          in8, (unsigned int)piece, hl, m, 1, r7, r8, r9, r10);
            }
        }
    }

    if (*(int*)(DAT_80612be4 + 0x254) == 4) aveditor_draw_selection_frame();
    *(unsigned int*)(DAT_80612be4 + 0x25c) = *(unsigned int*)(DAT_80612be4 + 0x25c) & 0xff7fffff;
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// Draw the group/link marker glyphs from the context's marker table at +0x3070 (12-byte records:
// short x/y/z at +0/+2/+4, byte type at +6, selection state at +7, edge mask at +8). Markers on the
// current grid Z that are selected/highlighted get a colored bar or a rotated quad + endpoint call.
void aveditor_draw_group_markers(void)
{
    unsigned int color;
    int i, off, base;
    short sx, sy;
    double dxq, dyq, one, cc, x0, y0, y1, y2, y3, t0, t1, t2, t3;
    double bias = DOUBLE_8061a6f0;

    __save_gpr_28();
    i = 0; off = 0;
    while (true) {
        if ((int)(unsigned int)*(unsigned short*)(DAT_80612be4 + 0x306e) <= i) { __restore_gpr_28(); return; }
        base = DAT_80612be4 + off;
        unsigned char type = *(unsigned char*)(base + 0x3076);
        double mx = (double)(int)*(short*)(base + 0x3070);
        double myf = (double)(float)((double)(int)(*(short*)(base + 0x3072) ^ 0) - bias);
        (void)myf;

        if ((int)*(short*)(base + 0x3074) == *(int*)(DAT_80612be4 + 0x264)) {
            unsigned char selState = *(unsigned char*)(base + 0x3077);
            if (selState == 1) {
                color = 0x50507880;
            } else if (selState == 0) {
                unsigned char mask = *(unsigned char*)(base + 0x3078);
                if ((mask & 1) != 0)       color = 0x781e1e80;
                else if ((mask & 2) != 0)  color = 0x32326e80;
                else if ((mask & 4) != 0)  color = 0x6e6e3280;
                else if ((mask & 8) != 0)  color = 0x326e3280;
                else                       color = 0x784b1e80;
            } else {
                goto next;
            }

            if ((type < 0x0b) || (0x0e < type)) {
                // simple bar marker (axis-aligned) with a perpendicular cap on each end
                double fx, fy;
                float dw = (type & 1) ? FLOAT_8061a70c : FLOAT_8061a788; // width picks orientation
                float dh = (type & 1) ? FLOAT_8061a788 : FLOAT_8061a70c;
                float base_y = (float)((double)(int)(*(short*)(base + 0x3072) ^ 0) - bias);
                if ((type & 1) == 0) {
                    fx = (float)(mx - bias) - FLOAT_8061a78c;
                    fy = base_y + FLOAT_8061a724;
                } else {
                    fx = (float)(mx - bias) + FLOAT_8061a724;
                    fy = base_y - FLOAT_8061a78c;
                }
                double bx = (double)fx, by = (double)fy;
                double bx1 = (double)(float)(bx + (double)dw);
                double by1 = (double)(float)(by + (double)dh);
                aveditor_draw_grid_rect(color, bx, by, bx1, by1);
                if (((*(unsigned char*)(DAT_80612be4 + off + 0x3076)) & 1) == 0) {
                    aveditor_draw_grid_rect(0x5a5a5a80, bx, (double)(float)(by - (double)FLOAT_8061a724), bx1, by);
                    aveditor_draw_grid_rect(0x5a5a5a80, bx, by1, bx1, (double)(float)((double)FLOAT_8061a724 + by1));
                } else {
                    aveditor_draw_grid_rect(0x5a5a5a80, (double)(float)(bx - (double)FLOAT_8061a724), by, bx, by1);
                    aveditor_draw_grid_rect(0x5a5a5a80, bx1, by, (double)(float)((double)FLOAT_8061a724 + bx1), by1);
                }
            } else {
                // diagonal marker (type 0x0b..0x0e): rotated quad + endpoint arc-cap call
                dyq = (double)(float)((double)(int)(*(short*)(base + 0x3070) ^ 0) - DOUBLE_8061a6f0);
                dxq = (double)(float)((double)(int)(*(short*)(base + 0x3072) ^ 0) - DOUBLE_8061a6f0);
                cc = (double)FLOAT_8061a78c;
                float capArg;
                if ((type == 0x0d) || (type == 0x0b)) {
                    x0 = (double)(float)((double)FLOAT_8061a6e8 + dxq);
                    y0 = (double)(float)((double)FLOAT_8061a6e8 + dyq);
                    t2 = (double)(float)(cc + dyq); t1 = (double)(float)(cc + x0); t0 = (double)(float)(cc + y0);
                    double xx = (double)(float)(x0 - cc);
                    aveditor_draw_grid_quad((double)(float)(dyq - cc), dxq, t2, (double)(float)(dxq - cc), y0, t1, t0, xx);
                    aveditor_draw_grid_rect(0x5a5a5a80, dyq, (double)(float)(dxq - (double)FLOAT_8061a78c), t2,
                                            (double)(float)((double)FLOAT_8061a78c + dxq));
                    aveditor_draw_grid_rect(0x5a5a5a80, y0, xx, t0, t1);
                    sy = (short)(int)(FLOAT_8061a6e8 + (float)((double)(int)(*(short*)(DAT_80612be4 + off + 0x3070) ^ 0) - DOUBLE_8061a6f0));
                    sx = (short)(int)(FLOAT_8061a6e8 + (float)((double)(int)(*(short*)(DAT_80612be4 + off + 0x3072) ^ 0) - DOUBLE_8061a6f0));
                    capArg = FLOAT_8061a794;
                } else {
                    x0 = (double)(float)((double)FLOAT_8061a6e8 + dyq);
                    y1 = (double)(float)((double)FLOAT_8061a6e8 + dxq);
                    t0 = (double)(float)(cc + dyq); t1 = (double)(float)(cc + x0);
                    t2 = (double)(float)(cc + dxq); y3 = (double)(float)(cc + y1);
                    aveditor_draw_grid_quad(x0, (double)(float)(dyq - cc), t1, t0, (double)(float)(dxq - cc), y1, t2, y3);
                    aveditor_draw_grid_rect(0x5a5a5a80, (double)(float)(x0 - (double)FLOAT_8061a78c), dyq, t1, t0);
                    aveditor_draw_grid_rect(0x5a5a5a80, dxq, (double)(float)(y1 - (double)FLOAT_8061a78c), t2, y3);
                    sy = *(short*)(DAT_80612be4 + off + 0x3070);
                    sx = (short)(int)(FLOAT_8061a6e8 + (float)((double)(int)(*(short*)(DAT_80612be4 + off + 0x3072) ^ 0) - DOUBLE_8061a6f0));
                    capArg = FLOAT_8061a798;
                }
                FUN_8031a38c((double)(float)((double)(int)(sy ^ 0) - DOUBLE_8061a6f0),
                             (double)(float)((double)(int)(sx ^ 0) - DOUBLE_8061a6f0),
                             (double)FLOAT_8061a70c, (double)FLOAT_8061a79c, (double)FLOAT_8061a7a0,
                             (double)FLOAT_8061a7a4, (double)capArg);
            }
        }
    next:
        i = i + 1;
        off = off + 0x0c;
    }
}

// -----------------------------------------------------------------------------
// Render the paste/clipboard buffer: the members hanging off DAT_80612be4+0x2f7c are drawn at their
// live preview position (offset by the paste anchor at +0x2f84/+0x2f88), in mode 2 or 3 depending on
// whether the paste is confirmed.
void aveditor_draw_paste_buffer(void)
{
    float* head;
    unsigned int r8 = 0, r9 = 0; int r10 = 0;
    double sx, sy, px, py, x, y, in7 = 0.0, in8 = 0.0;
    double bias = DOUBLE_8061a6f0;
    unsigned char buf[0x48];

    FUN_803826f0();
    head = *(float**)(DAT_80612be4 + 0x2f7c);
    if (head != (float*)0) {
        unsigned int mode = 3;
        if ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x410004) == 4) mode = 2;

        sx = (double)FLOAT_80612c58;
        double dx = (double)(*(float*)(DAT_80612be4 + 0x2f84) -
                    (float)((double)(int)(DAT_80612c88 ^ 0x80000000) - bias));
        px = (double)(float)(sx * dx + (double)FLOAT_80612c78);
        double half = (double)FLOAT_8061a710;
        py = (double)(FLOAT_80612c54 *
                     (*(float*)(DAT_80612be4 + 0x2f88) - (float)((double)(int)(DAT_80612c84 ^ 0x80000000) - bias)) +
                     FLOAT_80612c74);

        for (float* m = head; m != (float*)0; m = (float*)(intptr_t)m[0x10]) {
            leaf_31e8((unsigned int)(intptr_t)buf, (unsigned int)(intptr_t)m, 0x48);
            x = (double)(float)((double)*m + px);
            if (x < half) x = (double)(float)(x - (double)FLOAT_8061a6e8);
            int xi = (int)x;
            y = (double)(float)((double)m[1] + py);
            if (y < half) y = (double)(float)(y - (double)FLOAT_8061a6e8);
            int yi = (int)y;
            float row = (float)((double)(int)(*(unsigned int*)(DAT_80612be4 + 0x264) ^ 0x80000000) - bias) + m[2];
            double ax = (double)(float)((double)(int)(xi ^ 0x80000000) - bias);
            double ay = (double)(float)((double)(int)(yi ^ 0x80000000) - bias);
            aveditor_draw_member_grid(0, ax, ay, (double)FLOAT_8061a6e8, (double)row, dx, sx, in7, in8,
                                      (unsigned int)m[(int)m[3] + 0xb], mode, (float*)buf, 1,
                                      (unsigned int)(intptr_t)head, r8, r9, r10);
        }
    }
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// member base + index*0x10 + 0x124 -> cell record address.
int aveditor_cell_record_addr(int member, int index)
{
    return member + index * 0x10 + 0x124;
}

// bit0 of the record's +8 word via count-leading-zeros; 0 for a null record.
unsigned int aveditor_cell_flag_bit(int rec)
{
    if (rec != 0) return countLeadingZeros(*(unsigned int*)(rec + 8)) >> 5;
    return 0;
}

// look up a cell record for (member,index) then read its flag bit.
void aveditor_cell_query_flag(int member, int index)
{
    int rec = aveditor_cell_record_addr(member, index);
    aveditor_cell_flag_bit(rec);
}

// -----------------------------------------------------------------------------
// Compute a text label's pixel anchor (x,y) for a cell. out[0]=x, out[1]=y written through the
// r3/r4 out-pointer pair folded by the _savegpr_ spill. If the "measure width" flag is set, the run
// width shifts the x anchor. text points at the label string; row is the cell's grid row.
void aveditor_text_anchor_from_cell(int outX, int outY, unsigned char* text, int row)
{
    unsigned long long out;
    float w = FLOAT_8061a710;
    double h;

    out = __save_gpr_28();
    (void)outX; (void)outY;
    int* px = (int*)(unsigned int)(out >> 0x20);
    unsigned int* py = (unsigned int*)(unsigned int)out;

    if (DAT_80611654 == 1) {
        double cw = text_get_char_width(text);
        w = (float)(cw * (double)FLOAT_8061a70c);
    }
    *px = (int)(FLOAT_8061a7a8 * FLOAT_80611a64 +
                (float)((double)(int)((row + 0x1c) ^ 0x80000000) - DOUBLE_8061a6f0) + w);
    h = text_get_char_height(text);
    *py = (int)(FLOAT_8061a70c * (float)((double)FLOAT_8061a7ac - h) +
                (float)((double)(int)(*py ^ 0x80000000) - DOUBLE_8061a6f0));
    __restore_gpr_28();
}

// -----------------------------------------------------------------------------
// Compute an icon's pixel anchor (x,y). outX/outY point at the destination words; row is the cell's
// grid row. The x anchor uses a fixed icon scale; y uses the text-anchor base.
void aveditor_icon_anchor_from_cell(int* outX, unsigned int* outY, int unused, unsigned int row)
{
    (void)unused;
    float scaleY = FLOAT_8061a70c;
    float iconScale = FLOAT_8061a7b0;
    double rowf = (double)(int)(row ^ 0x80000000) - DOUBLE_8061a6f0;
    *outY = (int)((float)((double)(int)(*outY ^ 0x80000000) - DOUBLE_8061a6f0) +
                  FLOAT_8061a7a8 * FLOAT_80611a64 + FLOAT_8061a7ac);
    *outX = (int)(iconScale * FLOAT_80611a68 * scaleY + (float)rowf);
}
