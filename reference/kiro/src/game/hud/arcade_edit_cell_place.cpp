// Batch: batch_970 | Module: arcade/challenge editor cursor grid-coord + cell placement-validity
// Address range: 0x80342298 - 0x80342b74
// Date: 2026-09-12
//
// The arcade/challenge editor's cursor-to-grid coordinate conversion and the cell placement-validity
// tests (continues the grid connectivity work from batch_969). aveditor_cursor_grid_pos maps the screen
// cursor into grid space (via aveditor_grid_lookup), then rebases it relative to the hit cell's origin;
// aveditor_cursor_grid_pos_raw is the same conversion without the rebase. aveditor_coord_is_valid /
// aveditor_coord_set_invalid mark a short[3] grid coord with the 0x8008 (-0x7ff8) sentinel. The
// placement path decides whether a cell can be placed at a grid coordinate: aveditor_cell_can_place runs
// the link-blocked test (pad_link_blocked_or_companion), checks the target cell flag bits
// (0x1fc0 present / 0x2000 override), then dedups against the node's existing links and the pending
// placement lists; aveditor_cell_can_place_at is the grid-lookup wrapper that computes the relative
// coord first. pad_link_matches_offset_type21 walks a node's link list for one matching a coord offset
// and confirms it via the avanim layout-0x21 + entity-descriptor flag; pad_node_has_zero_z_link reports
// whether any link or pending placement sits at z==0; aveditor_relink_type21_cell rotates a cell coord
// and walks the node vertex buffer during a relink pass; node_count_flagged_verts counts the node index
// buffer entries with bit 6 set. State: the DAT_80612be4 editor context (cursor cell at +0x2f80, node
// count at +0x280, per-node records at 0xe4 stride from +0x2e0), the pad-node vertex/index buffer tables
// (DAT_805d09ec / DAT_805d0938), and the screen-cursor globals (DAT_8051a834/38, FLOAT_80612c5x/7x).
// Callers: aveditor_screen_tick, aveditor_place_all_segments/companion, aveditor_relink_companions,
// aveditor_relink_type21_cells, aveditor_get_entry_position.
//
//   aveditor_cursor_grid_pos       0x80342298  cursor -> grid coord, rebased to the hit cell origin.
//   aveditor_cursor_grid_pos_raw   0x80342478  cursor -> grid coord (no rebase).
//   aveditor_coord_is_valid        0x80342530  short[3] grid coord != the 0x8008 sentinel.
//   aveditor_coord_set_invalid     0x80342564  set a short[3] grid coord to the 0x8008 sentinel.
//   pad_link_matches_offset_type21 0x80342578  node link matches a coord offset + is a type-0x21 layout.
//   aveditor_cell_can_place        0x80342698  full placement-validity test at a grid coord.
//   aveditor_cell_can_place_at     0x80342874  grid-lookup wrapper: compute the relative coord, then test.
//   pad_node_has_zero_z_link       0x80342968  any node link / pending placement at z==0.
//   aveditor_relink_type21_cell    0x80342a0c  per-cell relink: rotate coord + walk the node vertex buffer.
//   node_count_flagged_verts       0x80342b74  count node index-buffer entries with bit 6 set.
//
// NON-MATCHING: reproduces the decompiler's structure. The int->double magic-constant conversions
// (CONCAT44 with 0x43300000 / DOUBLE_8061ac00) and the branchless abs idiom ((x>>31 ^ x) - (x>>31))
// are reproduced verbatim. The scope helpers (FUN_803826f0 / FUN_8038273c) thread the cell record
// through the spilled high word; expressed as explicit locals (Compile-Clean rule 3). Raw FUN_/DAT_
// symbols are declared file-locally with call-site-accurate arity (Compile-Clean rule 5). CONCAT22/
// CONCAT44 come from common.h.

#include "common.h"

extern "C" {

extern int   DAT_80612be4;    // editor context base

// screen-cursor -> grid globals
extern float DAT_8051a834;    // cursor X source (indexed by DAT_806119ec * 0x1e0)
extern float DAT_8051a838;    // cursor Y source
extern int   DAT_806119ec;    // active screen index
extern int   DAT_80612c84;    // cursor origin Y (int)
extern int   DAT_80612c88;    // cursor origin X (int)
extern float FLOAT_80612c54;  // cursor Y scale
extern float FLOAT_80612c58;  // cursor X scale
extern float FLOAT_80612c74;  // cursor Y bias
extern float FLOAT_80612c78;  // cursor X bias
extern double DOUBLE_8061ac00;

// pad-node vertex/index buffer tables (indexed by node*4)
extern int   DAT_805d09ec;
extern int   DAT_805d0938;

// named cross-module helpers
extern int   aveditor_grid_lookup(int a, int b, int c);
extern unsigned int aveditor_place_companion_segments(void);
extern unsigned int pad_node_blocked_or_type6(int node, unsigned int flags);
extern unsigned int pad_link_blocked_or_companion(int link, int layer);
extern unsigned int avanim_layout_is_0x21(int desc);
extern unsigned char entdesc_flag_bit0_cur(int desc);

// raw helpers with local call-site signatures
extern unsigned long long FUN_803826f0(void);   // scope push (cell record in high word)
extern void  FUN_8038273c(void);                 // scope pop
extern void  FUN_80342db4(short* coord, int a, int rot, int b);  // rotate coord helper

// forward decls within this TU
unsigned int aveditor_coord_is_valid(short* coord);
unsigned int aveditor_cell_can_place(int node, short* coord);

// ---------------------------------------------------------------------------
// 0x80342298 : cursor -> grid coord, rebased to the hit cell origin
// ---------------------------------------------------------------------------
void aveditor_cursor_grid_pos(short* out)
{
    double magic = DOUBLE_8061ac00;
    int    z = DAT_806119ec * 0x1e0;
    int    cell;

    out[0] = (short)(int)(FLOAT_80612c58 *
                          (*(float*)(&DAT_8051a834 + z) -
                           (float)((double)CONCAT44(0x43300000, DAT_80612c88 ^ 0x80000000) - DOUBLE_8061ac00))
                          + FLOAT_80612c78);
    out[1] = (short)(int)(FLOAT_80612c54 *
                          (*(float*)(&DAT_8051a838 + DAT_806119ec * 0x1e0) -
                           (float)((double)CONCAT44(0x43300000, DAT_80612c84 ^ 0x80000000) - magic))
                          + FLOAT_80612c74);
    out[2] = (short)*(int*)(DAT_80612be4 + 0x264);

    cell = aveditor_grid_lookup((int)out[1] ^ 0x80000000, (int)out[0] ^ 0x80000000, z);
    *(int*)(DAT_80612be4 + 0x2f80) = cell;

    magic = DOUBLE_8061ac00;
    if (*(float**)(DAT_80612be4 + 0x2f80) != (float*)0) {
        out[0] = (short)(int)((float)((double)CONCAT44(0x43300000, (int)out[0] ^ 0x80000000) - DOUBLE_8061ac00)
                              - **(float**)(DAT_80612be4 + 0x2f80));
        out[1] = (short)(int)((float)((double)CONCAT44(0x43300000, (int)out[1] ^ 0x80000000) - magic)
                              - *(float*)(*(int*)(DAT_80612be4 + 0x2f80) + 4));
        out[2] = (short)(int)((float)((double)CONCAT44(0x43300000, (int)out[2] ^ 0x80000000) - magic)
                              - *(float*)(*(int*)(DAT_80612be4 + 0x2f80) + 8));
    }
}

// ---------------------------------------------------------------------------
// 0x80342478 : cursor -> grid coord (no rebase)
// ---------------------------------------------------------------------------
void aveditor_cursor_grid_pos_raw(unsigned short* out)
{
    double magic = DOUBLE_8061ac00;
    out[0] = (short)(int)(FLOAT_80612c58 *
                          (*(float*)(&DAT_8051a834 + DAT_806119ec * 0x1e0) -
                           (float)((double)CONCAT44(0x43300000, DAT_80612c88 ^ 0x80000000) - DOUBLE_8061ac00))
                          + FLOAT_80612c78);
    out[1] = (short)(int)(FLOAT_80612c54 *
                          (*(float*)(&DAT_8051a838 + DAT_806119ec * 0x1e0) -
                           (float)((double)CONCAT44(0x43300000, DAT_80612c84 ^ 0x80000000) - magic))
                          + FLOAT_80612c74);
    out[2] = (short)*(int*)(DAT_80612be4 + 0x264);
}

// ---------------------------------------------------------------------------
// 0x80342530 : short[3] grid coord valid (none is the 0x8008 sentinel)
// ---------------------------------------------------------------------------
unsigned int aveditor_coord_is_valid(short* coord)
{
    if (((*coord != -0x7ff8) && (coord[1] != -0x7ff8)) && (coord[2] != -0x7ff8)) {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80342564 : set a short[3] grid coord to the 0x8008 sentinel
// ---------------------------------------------------------------------------
void aveditor_coord_set_invalid(unsigned short* coord)
{
    coord[0] = 0x8008;
    coord[1] = 0x8008;
    coord[2] = 0x8008;
}

// ---------------------------------------------------------------------------
// 0x80342578 : node link matches a coord offset + is a type-0x21 layout
// ---------------------------------------------------------------------------
unsigned int pad_link_matches_offset_type21(float* node, float* target)
{
    unsigned int blocked;
    unsigned char flag;
    int* link;

    if (node != (float*)0) {
        blocked = aveditor_place_companion_segments();
        blocked = pad_node_blocked_or_type6((int)node[4], blocked);
        if (blocked != 0) {
            for (link = (int*)(intptr_t)node[0xf]; link != (int*)0; link = (int*)(intptr_t)link[5]) {
                if (((*(short*)(link + 3) == (short)(int)(*target - *node)) &&
                     (*(short*)((int)(intptr_t)link + 0xe) == (short)(int)(target[1] - node[1]))) &&
                    (*(short*)(link + 4) == (short)(int)(target[2] - node[2]))) {
                    if ((avanim_layout_is_0x21(*link) != 0) &&
                        (flag = entdesc_flag_bit0_cur(*link), flag != 0)) {
                        return 1;
                    }
                    return 0;
                }
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80342698 : full placement-validity test at a grid coord
// ---------------------------------------------------------------------------
unsigned int aveditor_cell_can_place(int node, short* coord)
{
    int   result;
    int   x, y, z;
    int   grid;
    unsigned int flags;
    unsigned int mask;
    unsigned int absZ;
    int   link;
    int   ni;
    int   off;
    int*  place;
    int   cnt;
    unsigned int negZ;

    short localCoord[3];
    localCoord[0] = coord[0];
    localCoord[1] = coord[1];
    localCoord[2] = coord[2];

    if (aveditor_coord_is_valid(localCoord) == 0) {
        return 0;
    }
    if (node == 0) {
        return 0;
    }

    negZ = *(unsigned int*)(DAT_80612be4 + 0x264) ^ 0x80000000;
    mask = (unsigned int)((float)((double)CONCAT44(0x43300000, negZ) - DOUBLE_8061ac00)
                          - *(float*)(node + 8));
    absZ = ((int)mask >> 0x1f ^ mask) - ((int)mask >> 0x1f);
    if (pad_link_blocked_or_companion(node, (int)absZ) == 0) {
        return 0;
    }

    z    = (int)coord[2];
    y    = (int)coord[1];
    grid = *(int*)(node + *(int*)(node + 0xc) * 4 + 0x2c);
    x    = (int)*coord;
    flags = *(unsigned int*)(*(int*)(grid + z * 4 + 0x20) + (x + y * *(int*)(grid + 0x14)) * 4);

    if (((flags & 0x1fc0) == 0) || ((flags & 0x2000) != 0)) {
        // dedup against the node's existing links
        for (link = *(int*)(node + 0x3c); link != 0; link = *(int*)(link + 0x14)) {
            if (((*(short*)(link + 0xc) == x) && (*(short*)(link + 0xe) == y)) &&
                (*(short*)(link + 0x10) == z)) {
                return 0;
            }
        }
        // dedup against pending placements across all nodes
        off = 0;
        for (ni = 0; ni < *(int*)(DAT_80612be4 + 0x280); ni = ni + 1) {
            cnt   = *(int*)(DAT_80612be4 + off + 0x2e0);
            place = (int*)(DAT_80612be4 + off + 0x2ec);
            if (0 < cnt) {
                do {
                    if (((*place == node) && (*(short*)(place + 1) == x)) &&
                        ((*(short*)((int)place + 6) == y) && (*(short*)(place + 2) == z))) {
                        return 0;
                    }
                    place = place + 4;
                    cnt   = cnt - 1;
                } while (cnt != 0);
            }
            off = off + 0xe4;
        }
        result = 1;
    } else {
        result = 0;
    }
    return (unsigned int)result;
}

// ---------------------------------------------------------------------------
// 0x80342874 : grid-lookup wrapper: compute the relative coord, then test
// ---------------------------------------------------------------------------
unsigned int aveditor_cell_can_place_at(short* coord)
{
    float* cell;
    short  rel[3];

    cell = (float*)aveditor_grid_lookup((int)coord[1] ^ 0x80000000, 0x43300000, (int)*coord ^ 0x80000000);
    if (cell == (float*)0) {
        return 0;
    }
    rel[2] = (short)(coord[2] - (short)(int)cell[2]);
    rel[0] = (short)(*coord - (short)(int)*cell);
    rel[1] = (short)(coord[1] - (short)(int)cell[1]);
    return aveditor_cell_can_place((int)(intptr_t)cell, rel);
}

// ---------------------------------------------------------------------------
// 0x80342968 : any node link / pending placement at z==0
// ---------------------------------------------------------------------------
unsigned int pad_node_has_zero_z_link(int node)
{
    int link;
    int cnt;
    int* place;
    int  ni;
    int  off;

    if (node == 0) {
        return 0;
    }
    for (link = *(int*)(node + 0x3c); link != 0; link = *(int*)(link + 0x14)) {
        if (*(short*)(link + 0x10) == 0) {
            return 1;
        }
    }
    ni  = 0;
    off = 0;
    do {
        if (*(int*)(DAT_80612be4 + 0x280) <= ni) {
            return 0;
        }
        cnt   = *(int*)(DAT_80612be4 + off + 0x2e0);
        place = (int*)(DAT_80612be4 + off + 0x2ec);
        if (0 < cnt) {
            do {
                if ((*place == node) && (*(short*)(place + 2) == 0)) {
                    return 1;
                }
                place = place + 4;
                cnt   = cnt - 1;
            } while (cnt != 0);
        }
        ni  = ni + 1;
        off = off + 0xe4;
    } while (true);
}

// ---------------------------------------------------------------------------
// 0x80342a0c : per-cell relink: rotate coord + walk the node vertex buffer
// ---------------------------------------------------------------------------
void aveditor_relink_type21_cell(void)
{
    int   node;
    short* coord;
    int   rot;
    unsigned long long gpr;
    short local[3];
    int   vtxCount;

    gpr   = FUN_803826f0();
    node  = (int)((unsigned long long)gpr >> 0x20);
    coord = (short*)gpr;
    local[0] = coord[0];
    local[1] = coord[1];
    local[2] = coord[2];

    rot = *(int*)(node + 0xc);
    if (rot != 0) {
        FUN_80342db4(local, node, rot, 4 - rot);
    }

    // (int->double magic conversions of the rotated coord elided: the loop below
    //  only consumes the vertex count, matching the decompiler's dead-value output)
    vtxCount = *(int*)(*(int*)(&DAT_805d09ec + *(int*)(node + 0x10) * 4) + 4);
    if (0 < vtxCount) {
        do {
            vtxCount = vtxCount - 1;
        } while (vtxCount != 0);
    }

    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80342b74 : count node index-buffer entries with bit 6 set
// ---------------------------------------------------------------------------
int node_count_flagged_verts(int node)
{
    int count = 0;
    int off   = 0;
    int n     = *(int*)(*(int*)(&DAT_805d09ec + node * 4) + 4);
    if (n < 1) {
        return count;
    }
    do {
        int at = off + 0x10;
        off   = off + 0x20;
        count = count + (int)(*(unsigned int*)(*(int*)(&DAT_805d0938 + node * 4) + at) >> 6 & 1);
        n     = n - 1;
    } while (n != 0);
    return count;
}

} // extern "C"
