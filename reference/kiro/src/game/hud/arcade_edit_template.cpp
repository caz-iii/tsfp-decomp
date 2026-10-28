// Batch: batch_889 | Module: arcade map-editor piece-template builder + misc editor helpers
// Address range: 0x803144fc - 0x80314b54
// Date: 2026-09-12
//
// Builds the per-piece-type templates that the placement/paste code stamps into the map. Each of
// the 0x2d piece types has a 0x140-byte template record in the static table &DAT_8047c148 (stride
// 0x140): +0x14 cols, +0x18 rows, +0x1c depth, a row-pointer table at +0x20, a cell-flag block at
// +0x48, and a 0x30-byte trailer at +0x10c. Templates are also chained into 4 rotation variants
// through +0x13c. The runtime copies live in the paste/clipboard buffer (DAT_80612c28 / cursor
// DAT_80612c24) built by arcade_edit_count.cpp::aveditor_alloc_paste_buffer.
//
//   aveditor_reset_template_table   0x803144fc  clear the piece-template pointer table.
//   aveditor_paste_alloc_piece      0x80314534  carve one piece instance out of the paste buffer.
//   aveditor_build_rotations        0x803145d8  build the 4 rotation variants of a piece template.
//   aveditor_init_templates         0x80314934  init all 0x2d piece templates + their rotations.
//   aveditor_rotate_template_dims   0x803149ec  copy a template + produce 3 dim-swapped rotations.
//   aveditor_texset_is_loaded       0x80314a58  test whether a texture-set table entry is resolved.
//   aveditor_find_loaded_texset     0x80314a84  find the first resolved texture set, store in ctx+0x60.
//   aveditor_get_slot_texset        0x80314adc  resolve the texture-set handle for an editor slot.
//   aveditor_click_rect             0x80314b00  cursor hit-test a rect and fire a click action.
//   aveditor_draw_grid_rect         0x80314b54  draw a fill rect in editor grid pixel coordinates.
//
// NON-MATCHING notes:
//  * aveditor_build_rotations (0x803145d8) is emitted with the _savegpr_/_restgpr_ runtime helpers
//    (FUN_803826e4 / FUN_80382730) that spill r29-r31; the incoming piece index in r3 is recovered
//    from the disassembly and expressed as an explicit int parameter (Compile-Clean rules 1/3).
//  * aveditor_draw_grid_rect (0x80314b54) forwards a color that lives in r3 straight through to
//    gx_draw_fill_rect_f without touching it, so the raw decompile lost the color argument. It is
//    written here as an explicit leading `uint color` parameter (matching gx_draw_fill_rect_f's r3).
//    The call site passes 4 transformed FP coords (f1=x_left, f2=y_top, f3=x_right, f4=y_bottom) but
//    the project's canonical gx_draw_fill_rect_f (defined in graphics/gx_material_state.cpp, batch_448)
//    is a 4-parameter helper (color, x0, x1, y1) that forwards to gx_draw_fill_rect_f_ex with a fixed
//    depth and leaves the leading x_left as the decompiler's `extraout_f1`. To stay consistent with
//    that established signature this batch passes (color, y_top, x_right, y_bottom); the leading
//    x_left FP argument is the same lost-to-extraout value as in the batch_448 decomp of the callee.
//  * The int<->float 0x43300000 / DOUBLE_8061a6f0 magic-double idiom is written with the shared
//    CONCAT44 / tsfp_concat44 intrinsic from common.h, matching the disassembly.
//  * memset_wrapper's first arg is the absolute address of the template table (-0x7fa29fc8 ==
//    0x805d6038); written as &DAT_805d6038.
//
// Raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols get correctly-typed per-file externs below. Named helpers
// (leaf_31e8, hud_cursor_hit_test_rect, gx_draw_fill_rect_f, memset_wrapper, aveditor_piece_
// serialized_size) and this batch's own functions resolve through the shared headers.

#include "common.h"
#include "hud.h"

extern "C" {

// -- piece-template static table (0x2d entries, stride 0x140) --
extern unsigned char DAT_8047c148;   // template records base (record N at &DAT_8047c148 + N*0x140)
extern int DAT_8047c15c;             // template field: depth   (stride 0x140)
extern int DAT_8047c160;             // template field: rows    (stride 0x140)
extern int DAT_8047c164;             // template field: cols    (stride 0x140)

// -- runtime template pointer table + paste buffer --
extern int DAT_805d6038;   // piece-type -> live template pointer table (4-byte stride)
extern int DAT_80612c20;   // template-init cursor (0..0x2d)
extern int DAT_80612c28;   // paste/clipboard buffer base
extern int DAT_80612c24;   // paste buffer used-size cursor

// -- texture-set table --
extern int DAT_8047fb68;   // texture-set handle table (0x32 entries, 4-byte stride)
extern int DAT_804782b0;   // "unresolved" sentinel handle
extern int DAT_80612be4;   // editor context (per-slot records stride 0xe4 from +0x2e8; flag +0x11854)

// -- editor-grid draw transform constants --
extern float  FLOAT_80612c5c;   // grid y pixels-per-unit
extern float  FLOAT_80612c60;   // grid x pixels-per-unit
extern float  FLOAT_80612c74;   // grid y world origin
extern float  FLOAT_80612c78;   // grid x world origin
extern int    DAT_80612c84;     // grid y pixel origin (int, magic-double converted)
extern int    DAT_80612c88;     // grid x pixel origin (int, magic-double converted)
extern double DOUBLE_8061a6f0;  // int->float conversion bias (0x43300000 magic)
extern float  FLOAT_8061a6e8;   // minimum rect extent (1 pixel)

// -- raw helpers not yet named --
extern void FUN_80321908(int action);   // dispatch an editor click action

} // extern "C"

// Clear the piece-template pointer table (0xb4 bytes == 0x2d * 4 entries) and reset the init cursor.
void aveditor_reset_template_table(void)
{
    memset_wrapper(&DAT_805d6038, 0, 0xb4);
    DAT_80612c20 = 0;
    return;
}

// Carve one piece instance out of the paste buffer: lay down its row-pointer table (one pointer per
// row, each row cols*depth*4 bytes) and advance the buffer cursor by the piece's serialized size.
// Returns the base of the newly carved instance.
int aveditor_paste_alloc_piece(int pieceType)
{
    int stride;
    int rowOff;
    int base;
    int rowPtr;
    int r;

    stride = pieceType * 0x140;
    rowOff = 0;
    base = DAT_80612c28 + DAT_80612c24;
    rowPtr = base + 0x140;
    for (r = 0; r < *(int *)(&DAT_8047c164 + stride); r = r + 1) {
        *(int *)(base + rowOff + 0x20) = rowPtr;
        rowOff = rowOff + 4;
        rowPtr = rowPtr + *(int *)(&DAT_8047c160 + stride) * *(int *)(&DAT_8047c15c + stride) * 4;
    }
    stride = aveditor_piece_serialized_size(pieceType);
    DAT_80612c24 = DAT_80612c24 + stride;
    return base;
}

// Build the 4 rotation variants of a piece template. Starting from the base template, each pass
// carves a fresh instance, transposes rows/cols (+0x14/+0x18), rotates every cell 90 degrees
// (mirroring across the leading edge and re-packing the per-cell flag bits), remaps the cell
// direction codes (7->8->9->10->7 and 11->12->13->14->11), copies the trailers, and chains the
// variant into +0x13c. (NON-MATCHING: incoming piece index recovered from the _savegpr_ spill.)
void aveditor_build_rotations(int pieceIndex)
{
    unsigned char code;
    char newDir;
    unsigned char *dst;
    unsigned char *src;
    int variant;
    int rowOff;
    int row;
    int col;
    int depthIdx;
    int depth;
    unsigned int *outCell;
    int inRow;
    unsigned int flags;
    int cellOff;

    variant = 1;
    src = &DAT_8047c148 + pieceIndex * 0x140;
    dst = src;
    do {
        dst = (unsigned char *)aveditor_paste_alloc_piece(pieceIndex);
        rowOff = 0;
        *(int *)(dst + 0x14) = *(int *)(src + 0x18);
        *(int *)(dst + 0x18) = *(int *)(src + 0x14);
        *(int *)(dst + 0x1c) = *(int *)(src + 0x1c);
        dst[0x12] = src[0x12];
        for (depthIdx = 0; depthIdx < *(int *)(src + 0x1c); depthIdx = depthIdx + 1) {
            outCell = *(unsigned int **)(dst + rowOff + 0x20);
            inRow = *(int *)(src + rowOff + 0x20);
            for (col = 0; col < *(int *)(src + 0x14); col = col + 1) {
                depth = *(int *)(src + 0x18);
                row = depth + -1;
                if (-1 < row) {
                    do {
                        flags = *(unsigned int *)(inRow + (col + row * *(int *)(src + 0x14)) * 4);
                        if (((flags & 0x100000) == 0) || ((flags & 0x200000) == 0)) {
                            if ((flags & 0x100000) == 0) {
                                if ((flags & 0x200000) != 0) {
                                    flags = (flags & 0xffdfffff) | 0x100000;
                                }
                            }
                            else {
                                flags = (flags & 0xffefffff) | 0x200000;
                            }
                        }
                        row = row + -1;
                        *outCell = ((flags >> 3) & 0x10000) |
                                   ((flags >> 3) & 0x100) |
                                   ((flags >> 3) & 1) | (flags & 0xfff0f0f0) | ((flags & 0x70707) << 1);
                        outCell = outCell + 1;
                        depth = depth + -1;
                    } while (depth != 0);
                }
            }
            rowOff = rowOff + 4;
        }
        *(int *)(dst + 0x44) = *(int *)(src + 0x44);
        leaf_31e8((u32)(dst + 0x48), (u32)(src + 0x48), 0xc0);
        cellOff = 0;
        for (depthIdx = 0; depthIdx < *(int *)(src + 0x44); depthIdx = depthIdx + 1) {
            code = src[cellOff + 0x48];
            if ((code != 6) && (code != 5)) {
                if (code < 7) {
                    newDir = '\x01';
                    if (code < 4) {
                        newDir = code + 1;
                    }
                    dst[cellOff + 0x48] = newDir;
                    dst[cellOff + 0x49] = (char)*(int *)(src + 0x18) - src[cellOff + 0x4a];
                    if ((src[cellOff + 0x48] & 1) == 0) {
                        dst[cellOff + 0x49] = dst[cellOff + 0x49] - src[cellOff + 0x4c];
                    }
                    dst[cellOff + 0x4a] = src[cellOff + 0x49];
                }
                else {
                    switch (code) {
                    case 7:  dst[cellOff + 0x48] = 8;    break;
                    case 8:  dst[cellOff + 0x48] = 9;    break;
                    case 9:  dst[cellOff + 0x48] = 10;   break;
                    case 10: dst[cellOff + 0x48] = 7;    break;
                    case 0xb: dst[cellOff + 0x48] = 0xc; break;
                    case 0xc: dst[cellOff + 0x48] = 0xd; break;
                    case 0xd: dst[cellOff + 0x48] = 0xe; break;
                    case 0xe: dst[cellOff + 0x48] = 0xb; break;
                    }
                    if ((dst[cellOff + 0x48] == 0xb) || (dst[cellOff + 0x48] == 0xd)) {
                        dst[cellOff + 0x49] =
                            src[cellOff + 0x4c] + ((char)*(int *)(src + 0x18) - src[cellOff + 0x4a]);
                        dst[cellOff + 0x4a] = src[cellOff + 0x49] + src[cellOff + 0x4c];
                    }
                    else {
                        dst[cellOff + 0x49] = (char)*(int *)(src + 0x18) - src[cellOff + 0x4a];
                        if (((src[cellOff + 0x48] & 1) == 0) && (dst[cellOff + 0x48] < 0xb)) {
                            dst[cellOff + 0x49] = dst[cellOff + 0x49] - src[cellOff + 0x4c];
                        }
                        dst[cellOff + 0x4a] = src[cellOff + 0x49];
                    }
                }
            }
            cellOff = cellOff + 6;
        }
        *(int *)(dst + 0x108) = *(int *)(src + 0x108);
        leaf_31e8((u32)(dst + 0x10c), (u32)(src + 0x10c), 0x30);
        variant = variant + 1;
        *(unsigned char **)(src + 0x13c) = dst;
        src = dst;
    } while (variant < 4);
    *(int *)(dst + 0x13c) = 0;
    return;
}

// Initialize all 0x2d piece templates: publish each template's pointer into the runtime table,
// fill in its row-pointer offsets, and build its rotation variants.
void aveditor_init_templates(void)
{
    int stride;
    unsigned char *tmpl;
    int rowOff;
    int r;

    for (DAT_80612c20 = 0; DAT_80612c20 < 0x2d; DAT_80612c20 = DAT_80612c20 + 1) {
        stride = DAT_80612c20 * 0x140;
        tmpl = &DAT_8047c148 + stride;
        *(unsigned char **)(&DAT_805d6038 + DAT_80612c20 * 4) = tmpl;
        rowOff = 4;
        for (r = 1; r < *(int *)(&DAT_8047c164 + stride); r = r + 1) {
            *(int *)(tmpl + rowOff + 0x20) =
                *(int *)(tmpl + rowOff + 0x1c) +
                *(int *)(&DAT_8047c15c + stride) * *(int *)(&DAT_8047c160 + stride) * 4;
            rowOff = rowOff + 4;
        }
        aveditor_build_rotations(DAT_80612c20);
    }
    return;
}

// Copy a 0x140-byte template from src to dst, then produce 3 further copies each swapping the
// rows/cols fields (+0x14/+0x18) - i.e. the transposed rotation dimensions.
void aveditor_rotate_template_dims(unsigned int dst, unsigned int src)
{
    int prevOff;
    int off;
    int cur;
    int prev;
    int n;

    leaf_31e8(dst, src, 0x140);
    prevOff = 1;
    off = 0x140;
    n = 3;
    do {
        prev = prevOff + -1;
        cur = dst + off;
        prevOff = prevOff + 1;
        off = off + 0x140;
        prev = dst + prev * 0x140;
        *(int *)(cur + 0x14) = *(int *)(prev + 0x18);
        *(int *)(cur + 0x18) = *(int *)(prev + 0x14);
        n = n + -1;
    } while (n != 0);
    return;
}

// Test whether a texture-set table entry has been resolved (handle != the "unresolved" sentinel).
// Uses the Ghidra countLeadingZeros(x)>>5 branchless equality idiom.
unsigned int aveditor_texset_is_loaded(int index)
{
    unsigned int isZero;

    isZero = countLeadingZeros(*(int *)(&DAT_8047fb68 + index * 4) - DAT_804782b0);
    return isZero >> 5;
}

// Find the first resolved texture set (indices 0..0x31) and record its index in ctx+0x60.
void aveditor_find_loaded_texset(int ctx)
{
    unsigned int loaded;
    int index;

    index = 0;
    do {
        loaded = aveditor_texset_is_loaded(index);
        if (loaded != 0) {
            *(int *)(ctx + 0x60) = index;
            return;
        }
        index = index + 1;
    } while (index < 0x32);
    return;
}

// Resolve the texture-set handle for an editor slot: the slot record (stride 0xe4 from ctx+0x2e8)
// holds a texture-set index that keys into the handle table DAT_8047fb68.
int aveditor_get_slot_texset(int slot)
{
    return *(int *)(&DAT_8047fb68 + *(int *)(DAT_80612be4 + slot * 0xe4 + 0x2e8) * 4);
}

// Cursor hit-test a rect and, if the cursor is over it while the editor's "click armed" flag
// (ctx+0x11854) is set, fire the given click action.
void aveditor_click_rect(float *rect, int action)
{
    int hit;

    hit = hud_cursor_hit_test_rect(rect);
    if ((hit != 0) && (*(char *)(DAT_80612be4 + 0x11854) == '\x01')) {
        FUN_80321908(action);
    }
    return;
}

// Draw a fill rect given in editor world coordinates: transform each (x,y) into grid pixel space
// (scale FLOAT_80612c60/5c about origin FLOAT_80612c78/74, offset by the magic-double-converted
// pixel origins), clamp the rect to a minimum 1-pixel extent, and hand off to gx_draw_fill_rect_f.
// (NON-MATCHING: color is passed through r3 from the caller; expressed as an explicit param.)
void aveditor_draw_grid_rect(unsigned int color, double x0, double y0, double x1, double y1)
{
    double py0;
    double py1;
    double px0;
    double px1;

    py0 = (double)(FLOAT_80612c5c * (float)(y0 - (double)FLOAT_80612c74) +
          (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c84 ^ 0x80000000) - DOUBLE_8061a6f0));
    py1 = (double)(FLOAT_80612c5c * (float)(y1 - (double)FLOAT_80612c74) +
          (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c84 ^ 0x80000000) - DOUBLE_8061a6f0));
    px0 = (double)(FLOAT_80612c60 * (float)(x0 - (double)FLOAT_80612c78) +
          (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c88 ^ 0x80000000) - DOUBLE_8061a6f0));
    px1 = (double)(FLOAT_80612c60 * (float)(x1 - (double)FLOAT_80612c78) +
          (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c88 ^ 0x80000000) - DOUBLE_8061a6f0));
    if ((double)(float)(py1 - py0) < (double)FLOAT_8061a6e8) {
        py1 = (double)(float)((double)FLOAT_8061a6e8 + py0);
    }
    if ((double)(float)(px1 - px0) < (double)FLOAT_8061a6e8) {
        px1 = (double)(float)((double)FLOAT_8061a6e8 + px0);
    }
    // px0 is the leading x_left FP arg that the 4-param helper takes as extraout (see header note).
    (void)px0;
    gx_draw_fill_rect_f(color, py0, px1, py1);
    return;
}
