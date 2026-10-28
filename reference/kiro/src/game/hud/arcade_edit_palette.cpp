// Batch: batch_885 | Module: arcade map-editor palette label lookups + cell rotation
// Address range: 0x80313258 - 0x80313608
// Date: 2026-09-11
//
// Continuation of the arcade map-editor helpers. The first group resolves palette/loadout
// display values and label strings by walking the loadout palette record and the linked
// weapon/description tables; the second group rotates cell coordinates for the editor's
// piece-rotate commands (xform_*_cells / mirror_selection call into these).
//
//   aveditor_get_loadout_palette_field2 0x80313258  read field[2] of a loadout palette record.
//   aveditor_show_loadout_palette_label 0x8031327c  show the loadout palette entry's label string.
//   aveditor_get_loadout_linked_index   0x803132a4  loadout field -> linked table index (or 0).
//   aveditor_show_loadout_linked_label  0x803132f8  show the linked entry's description string.
//   aveditor_get_loadout_linked_string_id 0x80313344 linked entry's description string id (or -1).
//   aveditor_get_palette_entry_value    0x803133ac  resolve a palette entry value by type range.
//   aveditor_rotate_coord               0x80313448  rotate an (x,y) float pair 90 deg within bounds.
//   aveditor_rotate_cell                0x803134f0  rotate one cell's (x,y) short pair.
//   aveditor_rotate_piece_cw            0x8031359c  rotate every cell of a piece clockwise.
//   aveditor_rotate_piece_ccw           0x80313608  rotate every cell of a piece counter-clockwise.
//
// NON-MATCHING: aveditor_rotate_piece_cw/ccw are emitted by the decompiler with the
// __save_gpr_28/__restore_gpr_28 prologue intrinsics and their register inputs folded into an
// undefined8 return; the real ABI (confirmed from the disassembly: r3=piece context, r4=xy short
// pair) is expressed as explicit parameters (Compile-Clean rules 1/3). The int->float
// 0x43300000 / DOUBLE_8061a5e0 magic-double idiom is written as plain (float)(int) casts. The
// prior-batch helpers aveditor_get_loadout_field / aveditor_get_loadout_palette_record return
// Ghidra `undefined4`/`undefined4*`, which gen_decls.ps1 does not emit, so they are forward
// declared per-file below with their real signatures. Raw FUN_/DAT_/DOUBLE_ get per-file externs;
// named helpers resolve via generated_decls.h.

#include "common.h"
#include "hud.h"

extern "C" {

// linked palette/description tables
extern int   DAT_80610ba0;    // linked-record array base (0x29c stride, id at +0x14)
extern int   DAT_8043e23c;    // loadout-field -> record-index map (0xf-int stride)
extern int   DAT_8042e2c4;    // description table base (0xe0 stride, +0 = string id)
extern int   DAT_8042e2c8;    // description table base (0xe0 stride, +4 field)
extern int   DAT_80477bd4;    // palette entry value table (0x31-int stride)
extern int   DAT_80477c80;    // palette entry flags table (0x31-int stride, bit2 = variant)
extern int   DAT_805d59a0;    // active palette variant index

// int->float conversion magic double
extern double DOUBLE_8061a5e0;

} // extern "C"

// Named helpers and this batch's own functions resolve via generated_decls.h at C++ linkage.
// The two prior-batch helpers below return Ghidra `undefined4`, which gen_decls skips, so they
// are declared here with their real signatures (definitions live in arcade_edit_query.cpp).
undefined4  aveditor_get_loadout_field(int index);
undefined4* aveditor_get_loadout_palette_record(int index);

// forward prototypes for this file's helpers referenced before their definition
int  aveditor_get_loadout_linked_index(int fieldIndex);
int  aveditor_get_loadout_linked_string_id(int fieldIndex);
int  aveditor_get_loadout_palette_field2(int index);
void aveditor_rotate_coord(float* x, float* y, int piece, int dir, int scale);
void aveditor_rotate_cell(short* px, short* py, int piece, int dir);

// -----------------------------------------------------------------------------

// Read field[2] of the loadout palette record selected by a loadout field index.
int aveditor_get_loadout_palette_field2(int index)
{
    undefined4* rec = aveditor_get_loadout_palette_record(index);
    return (int)rec[2];
}

// Show the label string for a loadout palette entry (record field[0] is the string id).
void aveditor_show_loadout_palette_label(int index)
{
    int* rec = (int*)aveditor_get_loadout_palette_record(index);
    get_string_by_id(*rec);
}

// Map a loadout field value to a linked-record field (id at +0x14), or 0 when the field is unset.
int aveditor_get_loadout_linked_index(int fieldIndex)
{
    int field = (int)aveditor_get_loadout_field(fieldIndex);
    if (field == 0) {
        return 0;
    }
    int rec = (&DAT_8043e23c)[field * 0xf];
    return *(int*)(DAT_80610ba0 + rec * 0x29c + 0x14);
}

// Show the description string for the linked entry (index 0 -> string id 0).
void aveditor_show_loadout_linked_label(int fieldIndex)
{
    int linked = aveditor_get_loadout_linked_index(fieldIndex);
    if (linked == 0) {
        get_string_by_id(0);
    } else {
        get_string_by_id(*(int*)(&DAT_8042e2c4 + linked * 0xe0));
    }
}

// Return the linked entry's description string id, or -1 when the field is unset.
int aveditor_get_loadout_linked_string_id(int fieldIndex)
{
    int field = (int)aveditor_get_loadout_field(fieldIndex);
    if (field == 0) {
        return -1;
    }
    int rec = *(int*)(DAT_80610ba0 + (&DAT_8043e23c)[field * 0xf] * 0x29c + 0x14);
    return *(int*)(&DAT_8042e2c8 + rec * 0xe0);
}

// Resolve a palette entry's value/icon by type range: 0x11..0x16 -> palette field2,
// 0x17..0x1c -> linked string id, 0x00..0x4f -> the value table (variant-aware). Else -1.
int aveditor_get_palette_entry_value(int type)
{
    if ((unsigned)(type - 0x11) < 6) {
        return aveditor_get_loadout_palette_field2(type - 0x11);
    }
    if ((unsigned)(type - 0x17) < 6) {
        return aveditor_get_loadout_linked_string_id(type - 0x17);
    }
    if (type >= 0 && type < 0x50) {
        if (((&DAT_80477c80)[type * 0x31] & 4) == 0) {
            return (&DAT_80477bd4)[type * 0x31];
        }
        return (&DAT_80477bd4)[DAT_805d59a0 * 7 + type * 0x31];
    }
    return -1;
}

// Rotate an (x,y) float pair 90 degrees within the piece's cell-grid bounds.
// dir: +1 = one way, -1 = the other; scale is the per-cell step (1). piece holds the grid
// extents at +0x14 (rows) and +0x18 (cols).
void aveditor_rotate_coord(float* x, float* y, int piece, int dir, int scale)
{
    float oldX = *x;
    if (dir == 0) {
        return;
    }
    if (dir < 0) {
        if (dir >= -1) {
            *x = *y;
            *y = -oldX + (float)(scale * (*(int*)(piece + 0x14) - 1));
        }
    } else if (dir < 2) {
        *x = -*y + (float)(scale * (*(int*)(piece + 0x18) - 1));
        *y = oldX;
    }
}

// Rotate a single cell's (x,y) short pair using the float rotation, then store back as shorts.
void aveditor_rotate_cell(short* px, short* py, int piece, int dir)
{
    float fx = (float)(int)*px;
    float fy = (float)(int)*py;
    aveditor_rotate_coord(&fx, &fy, piece, dir, 1);
    *px = (short)(int)fx;
    *py = (short)(int)fy;
}

// Rotate every cell of a piece clockwise. context holds the cell count at +0xc and the
// per-cell piece pointers at +0x2c (4-byte stride); xy is the piece's shared (x,y) pair.
void aveditor_rotate_piece_cw(int context, short* xy)
{
    int off = 0;
    for (int i = 0; i < *(int*)(context + 0xc); i++) {
        aveditor_rotate_cell(xy, xy + 1, *(int*)(context + off + 0x2c), 1);
        off += 4;
    }
}

// Rotate every cell of a piece counter-clockwise (reverse order, opposite direction).
void aveditor_rotate_piece_ccw(int context, short* xy)
{
    int count = *(int*)(context + 0xc);
    int off = count << 2;
    for (; count != 0; count--) {
        int entry = off + 0x2c;
        off -= 4;
        aveditor_rotate_cell(xy, xy + 1, *(int*)(context + entry), -1);
    }
}
