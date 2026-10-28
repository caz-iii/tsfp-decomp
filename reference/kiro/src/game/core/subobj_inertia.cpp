// Address: 0x8014f6dc | Name: subobj_inertia_scale | Date: 2026-09-15
//
// Scale a rigid-body sub-object's mass term and its full 3x3 inertia tensor by a
// single ratio. Used by the sub-object mass setters (subobj_set_mass 0x80149a74,
// subobj_set_material_and_mass 0x801499b0) when a body's material/mass is (re)assigned
// so the cached inertia stays consistent with the new mass.
//
// Layout (verbatim from the decompiler, addressed by raw byte offset):
//   massTerm : float pointed to by param_3 (the scalar 1/mass-style term).
//   inertia  : float[3][3] at param_2, row stride 0x10 bytes (3 packed floats + 4
//              bytes padding per row -> the matrix is stored as three 16-byte rows).
//
// The scale is applied in double precision (each float is promoted, multiplied by
// the double ratio, then stored back as float) to mirror the original PPC fmuls/frsp
// idiom. NON-MATCHING: the 0x10 row stride and the double-precision multiply are kept
// verbatim; no external symbols are referenced so no per-file externs are needed.

#include "common.h"

void subobj_inertia_scale(double ratio, int inertia, float *massTerm)
{
    int row;
    int rowOffset;
    int col;
    int colCount;

    row = 0;
    rowOffset = 0;

    // Scale the scalar mass term first.
    *massTerm = (float)((double)*massTerm * ratio);

    // Then scale each element of the 3x3 inertia matrix (row stride 0x10 bytes).
    do {
        col = 0;
        colCount = 3;
        do {
            *(float *)(inertia + rowOffset + col) =
                (float)((double)*(float *)(inertia + rowOffset + col) * ratio);
            col = col + 4;
            colCount = colCount + -1;
        } while (colCount != 0);
        row = row + 1;
        rowOffset = rowOffset + 0x10;
    } while (row < 3);
}
