// Batch: batch_886 | Module: arcade map-editor spatial grid + member pool management
// Address range: 0x80313670 - 0x80313bd4
// Date: 2026-09-11
//
// The arcade map-editor keeps a 0x9600-byte 3D occupancy grid (0x28 * 0x640-strided) that maps
// each grid cell to the member occupying it, plus free-lists of member records (0x48 stride) and
// cell records (0x3c stride). This batch is the allocation / grid-stamp / lookup / teardown layer.
//
//   aveditor_rotate_cell_local     0x80313670  rotate a cell's local (x,y) within its piece bounds.
//   aveditor_wrap_orientation_twice 0x8031370c apply the orientation wrap twice.
//   aveditor_wrap_orientation      0x80313748  wrap/clamp an orientation index into its range.
//   aveditor_init_pools            0x803137a4  build the member + cell free-lists, clear the grid.
//   aveditor_alloc_grid            0x80313888  allocate + zero the 0x9600 occupancy grid buffer.
//   aveditor_grid_stamp_member     0x803138c8  write a member pointer into every grid cell it fills.
//   aveditor_grid_clear_member     0x803139d8  clear (zero) a member's cells from the grid.
//   aveditor_grid_lookup           0x80313af4  bounds-checked read of the grid at (x,y,z).
//   aveditor_alloc_member          0x80313b7c  pop a member record off the free list.
//   aveditor_free_member           0x80313bd4  detach a member, clear its grid + groups, free it.
//
// NON-MATCHING: aveditor_alloc_grid is emitted with mem_alloc's full 16-arg PPC spill list; the
// real prototype is mem_alloc(size,type) (generated_decls.h), so the call collapses to
// mem_alloc(0x9600, 1). The grid index math the decompiler renders through the 0x43300000 /
// DOUBLE_8061a6e0 magic-double idiom is written as plain (float)(int) casts. memset_wrapper is a
// named helper not emitted by gen_decls (it lives outside src/); it is declared per-file with its
// real signature. Raw FUN_/DAT_/FLOAT_/DOUBLE_ get per-file externs; the aselgrid_*/aveditor_*
// helpers resolve via generated_decls.h.

#include "common.h"
#include "hud.h"

extern "C" {

// member free-list + count (0x48 stride, next at +0x40)
extern int DAT_80612c0c;   // member pool base
extern int DAT_80612c08;   // member free-list head
extern int DAT_80612c04;   // member free count
// cell free-list + count (0x3c stride, next at +0x14)
extern int DAT_80612c00;   // cell pool base
extern int DAT_80612bfc;   // cell free-list head
extern int DAT_80612bf8;   // cell free count
// active member list
extern float* DAT_80612c14; // active member list head (next at +0x10 float, i.e. +0x40 bytes)
extern int   DAT_80612c18;  // active member count
// occupancy grid + editor context
extern int DAT_80612c10;   // 0x9600 occupancy grid base
extern int DAT_80612be4;   // editor context (selection ptrs at +0x2f18 / +0x2f7c)

// grid geometry constants
extern float  FLOAT_8061a6d8;   // z-origin bias
extern double DOUBLE_8061a6e0;  // int->float conversion magic double

// memset_wrapper resolves via generated_externs.h (permissive stub, C++ linkage) - not declared here.

// raw FUN_ helper not yet named
extern void FUN_80314040(int member, int group);              // detach one cell group

} // extern "C"

// This file's own functions and the named aselgrid_*/aveditor_* helpers resolve via
// generated_decls.h at C++ linkage; forward prototypes for early references below.
int  aveditor_wrap_orientation(int base, int delta, int mode);
void aveditor_grid_clear_member(float* member);

// -----------------------------------------------------------------------------

// Rotate a cell's local (x,y) short pair 90 deg within its piece bounds, mirror-adjusting the
// leading edge for odd-width pieces. piece holds bounds at +0x14 (rows) / +0x18 (cols); bias is
// the piece origin. dir: +1 / -1 select rotation direction; 0 or out-of-range is a no-op.
void aveditor_rotate_cell_local(short* cell, int piece, int dir, short bias)
{
    short oldX = cell[0];
    if (dir == 0) {
        return;
    }
    if (dir >= 0) {
        if (dir > 1) {
            return;
        }
        cell[0] = (short)(bias + ((short)*(undefined4*)(piece + 0x18) - cell[1]));
        cell[1] = oldX;
        unsigned char flags = *(unsigned char*)((char*)cell + 6);
        if (flags > 10) return;
        if ((flags & 1) == 0) return;
        cell[0] = (short)(cell[0] - 1);
        return;
    }
    if (dir < -1) {
        return;
    }
    cell[0] = cell[1];
    cell[1] = (short)(bias + ((short)*(undefined4*)(piece + 0x14) - oldX));
    unsigned char flags = *(unsigned char*)((char*)cell + 6);
    if (flags > 10) return;
    if ((flags & 1) != 0) return;
    cell[1] = (short)(cell[1] - 1);
}

// Wrap/clamp an orientation index into range. mode 1 -> wrap into 0..3; else wrap into 0..7.
int aveditor_wrap_orientation(int base, int delta, int mode)
{
    int v = base + delta;
    if (mode == 1) {
        if (v > 3) return 0;
        if (v >= 0) return v;
        return 3;
    }
    if (mode > 0) return v;
    if (mode < 0) return v;
    if (v > 7) return 0;
    if (v >= 0) return v;
    return 7;
}

// Apply the orientation wrap twice (compose two steps).
void aveditor_wrap_orientation_twice(int base, int delta)
{
    int v = aveditor_wrap_orientation(base, delta, 0);
    aveditor_wrap_orientation(v, delta, 0);
}

// Build the member free-list (0x400 entries, 0x48 stride) and cell free-list (0x212 entries,
// 0x3c stride), reset the active-member list, and clear the occupancy grid.
void aveditor_init_pools(void)
{
    DAT_80612c14 = 0;
    DAT_80612c18 = 0;

    int off = 0;
    for (int i = 0; i < 0x3ff; i++) {
        *(int*)(DAT_80612c0c + off + 0x40) = DAT_80612c0c + (i + 1) * 0x48;
        off += 0x48;
    }
    *(undefined4*)(DAT_80612c0c + 0x3ff * 0x48 + 0x40) = 0;
    DAT_80612c04 = 0x400;
    DAT_80612c08 = DAT_80612c0c;

    off = 0;
    for (int i = 0; i < 0x211; i++) {
        *(int*)(DAT_80612c00 + off + 0x14) = DAT_80612c00 + (i + 1) * 0x3c;
        off += 0x3c;
    }
    *(undefined4*)(DAT_80612c00 + 0x211 * 0x3c + 0x14) = 0;
    DAT_80612bf8 = 0x212;
    DAT_80612bfc = DAT_80612c00;

    memset_wrapper(DAT_80612c10, 0, 0x9600);
}

// Allocate and zero the 0x9600-byte occupancy grid buffer.
void aveditor_alloc_grid(void)
{
    DAT_80612c10 = mem_alloc(0x9600, 1);
    memset_wrapper(DAT_80612c10, 0, 0x9600);
}

// Compute the flat grid slot for member-local (x,y,z) given the member's origin float coords.
static int aveditor_grid_slot(float* member, int gx, int gy, int gz, float zbias)
{
    int ix = (int)((float)gx + member[0]);
    int iy = (int)((float)gy + member[1]);
    int iz = (int)(((float)gz + member[2]) - zbias);
    return (iy * 0x28 + iz * 0x640 + ix) * 4;
}

// Write the member pointer into every occupancy-grid cell the member's bounding box fills.
// member[(int)member[3] + 0xb] points at the member's dimension record (x/y/z counts at +0x14/+0x18/+0x1c).
void aveditor_grid_stamp_member(float* member)
{
    float zbias = FLOAT_8061a6d8;
    int dims = (int)member[(int)member[3] + 0xb];
    for (int gy = 0; gy < *(int*)(dims + 0x1c); gy++) {
        for (int gz = 0; gz < *(int*)(dims + 0x18); gz++) {
            for (int gx = 0; gx < *(int*)(dims + 0x14); gx++) {
                *(float**)(DAT_80612c10 + aveditor_grid_slot(member, gx, gz, gy, zbias)) = member;
            }
        }
    }
}

// Clear (zero) every occupancy-grid cell the member fills.
void aveditor_grid_clear_member(float* member)
{
    float zbias = FLOAT_8061a6d8;
    int dims = (int)member[(int)member[3] + 0xb];
    for (int gy = 0; gy < *(int*)(dims + 0x1c); gy++) {
        for (int gz = 0; gz < *(int*)(dims + 0x18); gz++) {
            for (int gx = 0; gx < *(int*)(dims + 0x14); gx++) {
                *(undefined4*)(DAT_80612c10 + aveditor_grid_slot(member, gx, gz, gy, zbias)) = 0;
            }
        }
    }
}

// Bounds-checked read of the occupancy grid at (x,y,z) (z offset by +2); 0 when out of range.
undefined4 aveditor_grid_lookup(int x, int y, int z)
{
    int iz = z + 2;
    if (x < 0 || x > 0x27 || y < 0 || y > 0x27 || iz < 0 || iz > 5) {
        return 0;
    }
    return *(undefined4*)(DAT_80612c10 + (y * 0x28 + iz * 0x640 + x) * 4);
}

// Pop a member record off the free list (zeroing it), or 0 when the pool is empty.
int aveditor_alloc_member(void)
{
    int rec = DAT_80612c08;
    if (rec != 0) {
        DAT_80612c08 = *(int*)(rec + 0x40);
        memset_wrapper(rec, 0, 0x48);
        DAT_80612c04 = DAT_80612c04 - 1;
    }
    return rec;
}

// Detach a member from the active list, clear its grid cells and cell groups, drop the active-
// selection references, then return the record to the free list.
void aveditor_free_member(float* member)
{
    if (*(float**)(DAT_80612be4 + 0x2f18) == member) {
        *(undefined4*)(DAT_80612be4 + 0x2f18) = 0;
    }
    if (*(float**)(DAT_80612be4 + 0x2f7c) == member) {
        *(undefined4*)(DAT_80612be4 + 0x2f7c) = 0;
    }
    aselgrid_remove_inbounds_cells();
    aselgrid_remove_active_entries();
    aselgrid_purge_active_mode3();
    aveditor_remove_member_all_groups();

    // the member's next-link occupies word index 0x10 (byte +0x40) as a raw pointer, aliased
    // through the float array; read/write it as a 32-bit slot rather than a float value.
    float* cur = DAT_80612c14;
    if (DAT_80612c14 == member) {
        DAT_80612c14 = *(float**)&member[0x10];
        DAT_80612c18 = DAT_80612c18 - 1;
        aveditor_grid_clear_member(member);
    } else {
        float* prev;
        do {
            prev = cur;
            if (prev == 0) break;
            cur = *(float**)&prev[0x10];
        } while (*(float**)&prev[0x10] != member);
        if (prev != 0) {
            *(float**)&prev[0x10] = *(float**)&member[0x10];
            DAT_80612c18 = DAT_80612c18 - 1;
            aveditor_grid_clear_member(member);
        }
    }

    while (member[0xf] != 0.0f) {
        FUN_80314040((int)member, (int)member[0xf]);
    }

    *(int*)&member[0x10] = DAT_80612c08;
    DAT_80612c08 = (int)member;
    DAT_80612c04 = DAT_80612c04 + 1;
}
