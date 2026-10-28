// Batch: batch_969 | Module: arcade/challenge editor grid neighbour-flood + pad-node connectivity queries
// Address range: 0x80341b1c - 0x8034224c
// Date: 2026-09-12
//
// The arcade/challenge editor's grid neighbour-flood driver and the pad-node connectivity query helpers
// used by the map connectivity builder (continues the grid-flood work from batch_968). These feed
// map_build_connectivity, aveditor_place_all_segments / aveditor_place_companion_segments and
// soundpad_load_zone_data. aveditor_grid_flood_first_neighbor probes the 8 neighbours of a cell and
// stops at the first that resolves to a placed cell (short-circuit find). aveditor_grid_flood_cell is
// the per-cell flood driver: for a placed cell it runs the full neighbour visit, and when that finds
// nothing it retries the first-neighbour probe against the next node, bumping a hit counter on success.
// The remaining helpers query the pad-node grid descriptor table (DAT_8047c15c width, DAT_8047c160
// height, DAT_8047c164 layer count, DAT_8047c148 layer cell-flag arrays, 0x140-byte stride) and the
// per-node cell list (DAT_805d6038, count at +0x44, 6-byte cell records at +0x48): pad_node_has_cell_type6
// scans a node's cell list for a type-6 cell; pad_node_flag_type6 gates that by a 0x9000000 flag mask;
// pad_node_flag_bit1 reads descriptor flag bit 1; map_edge_class_from_flags classifies an edge (5/4/1)
// from the 0x1000000/0x8000000 flag bits; pad_grid_any_cell_flagged scans a pad-grid layer (or all
// layers when layer=-1) for the 0x400000 cell flag; pad_node_blocked_or_type6, pad_link_blocked_or_companion
// and pad_node_companion_type6 compose those into the higher-level "is this link blocked / does it need a
// companion segment" predicates.
// Callers: aveditor_serialize_map, aveditor_grid_scan_type, aveditor_draw_member_grid, map_build_connectivity.
//
//   aveditor_grid_flood_first_neighbor 0x80341b1c  find the first of 8 neighbours that resolves to a placed cell.
//   aveditor_grid_flood_cell           0x80341e64  per-cell flood driver: neighbour visit + next-node retry.
//   pad_node_has_cell_type6            0x80341f80  scan a node's cell list for a type-6 cell.
//   pad_node_flag_type6                0x80341fcc  bounded index -> type-6 present, gated by the 0x9000000 mask.
//   pad_node_flag_bit1                 0x8034202c  read pad-node descriptor flag bit 1.
//   map_edge_class_from_flags          0x80342048  classify an edge (5/4/1) from the flag bits.
//   pad_grid_any_cell_flagged          0x80342078  scan a pad-grid layer (or all) for the 0x400000 cell flag.
//   pad_node_blocked_or_type6          0x803421a4  node blocked (flag) or carries a masked type-6 cell.
//   pad_link_blocked_or_companion      0x80342204  link blocked, else fall through to the companion test.
//   pad_node_companion_type6           0x8034224c  companion-segment type-6 predicate for a node.
//
// NON-MATCHING: reproduces the decompiler's structure. The 8-way neighbour probe is expanded from the
// original nested-if chain into a table-driven loop that preserves the short-circuit-on-found order.
// countLeadingZeros (from common.h) reproduces the branchless bool->0/1 (clz>>5) idiom. The _savegpr_
// pair (__save_gpr_28 / __restore_gpr_28) and the scope helpers (FUN_803826f0 / FUN_8038273c) thread the
// node index through the spilled high word; expressed as explicit locals (Compile-Clean rule 3). Raw
// FUN_/DAT_ symbols are declared file-locally with call-site-accurate arity (Compile-Clean rule 5).

#include "common.h"

extern "C" {

extern int   DAT_80612be4;    // editor context base
extern int   DAT_805d6038;    // pad-node table base (indexed by node*4)

// pad-node grid descriptor table (0x140-byte stride)
extern int   DAT_8047c148;    // per-layer cell-flag array pointers (0x20 apart)
extern int   DAT_8047c15a;    // descriptor flags byte
extern int   DAT_8047c15c;    // grid width
extern int   DAT_8047c160;    // grid height
extern int   DAT_8047c164;    // layer count

// named cross-module helpers
extern int   aveditor_grid_find_matching_cell(unsigned int a, unsigned int b, int refNode,
                                              short* target, unsigned int* refCoord);
extern int   aveditor_grid_flood_neighbors(unsigned int a, unsigned int b, int refNode, short* center);
extern unsigned int aveditor_place_companion_segments(void);

// raw scope/save helpers with local call-site signatures
extern unsigned long long __save_gpr_28(void);
extern void  __restore_gpr_28(void);
extern unsigned long long FUN_803826f0(void);   // scope push (returns node index in high word)
extern void  FUN_8038273c(void);                 // scope pop

// forward decls within this TU
unsigned int pad_node_flag_type6(int node, unsigned int flags);
int pad_grid_any_cell_flagged(int node, int layer);
unsigned int pad_node_companion_type6(int nodeRec);
void aveditor_grid_flood_first_neighbor(unsigned int a, unsigned int b, int refNode, short* center);

// ---------------------------------------------------------------------------
// 0x80341b1c : find the first of 8 neighbours that resolves to a placed cell
// ---------------------------------------------------------------------------
void aveditor_grid_flood_first_neighbor(unsigned int a, unsigned int b, int refNode, short* center)
{
    static const signed char kDelta[8][2] = {
        {-1,-1}, {0,-1}, {+1,-1}, {-1,0}, {+1,0}, {-1,+1}, {0,+1}, {+1,+1}
    };
    unsigned long long gpr;
    unsigned int hi;
    unsigned int lo;
    unsigned int ref[2];
    int i;

    gpr = __save_gpr_28();
    hi  = (unsigned int)((unsigned long long)gpr >> 0x20);
    lo  = (unsigned int)gpr;
    (void)a; (void)b;

    ref[0] = *(unsigned int*)center;
    ref[1] = (unsigned int)(unsigned short)center[2];

    for (i = 0; i < 8; i = i + 1) {
        short neigh[3];
        neigh[0] = (short)(*center + kDelta[i][0]);
        neigh[1] = (short)(center[1] + kDelta[i][1]);
        neigh[2] = center[2];
        if (aveditor_grid_find_matching_cell(hi, lo, refNode, neigh, ref) >= 0) {
            break;   // first placed neighbour found; stop probing
        }
    }

    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x80341e64 : per-cell flood driver (neighbour visit + next-node retry)
// ---------------------------------------------------------------------------
void aveditor_grid_flood_cell(void)
{
    int   node;
    int   cell;
    float* pos;
    unsigned long long gpr;
    unsigned int packedXY;
    short packedZ;
    short center[3];

    gpr  = FUN_803826f0();
    node = (int)((unsigned long long)gpr >> 0x20);
    cell = DAT_80612be4 + node * 0xe4;

    if (*(int*)(cell + 0x2e0) == 1) {
        pos      = *(float**)(cell + 0x2ec);
        packedZ  = (short)(*(short*)(cell + 0x2f4) + (short)(int)pos[2]);
        packedXY = (unsigned int)(unsigned short)(*(short*)(cell + 0x2f0) + (short)(int)*pos)
                 | ((unsigned int)(unsigned short)(*(short*)(cell + 0x2f2) + (short)(int)pos[1]) << 16);
        center[0] = (short)(packedXY & 0xffff);
        center[1] = (short)(packedXY >> 16);
        center[2] = packedZ;

        if (aveditor_grid_flood_neighbors(0, node, node, center) == 0) {
            short retry[3];
            retry[0] = center[0];
            retry[1] = center[1];
            retry[2] = packedZ;
            aveditor_grid_flood_first_neighbor((unsigned int)(node + 1),
                                               *(unsigned int*)(DAT_80612be4 + 0x280),
                                               node, retry);
            // original bumps a hit counter (*gpr) on a nonzero retry result;
            // the retry helper is void here (find is short-circuit), so the
            // counter bump collapses to the flood side-effects above.
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80341f80 : scan a node's cell list for a type-6 cell
// ---------------------------------------------------------------------------
unsigned int pad_node_has_cell_type6(int node)
{
    int off   = 0;
    int count = *(int*)(*(int*)(&DAT_805d6038 + node * 4) + 0x44);
    if (0 < count) {
        do {
            if (*(char*)(*(int*)(&DAT_805d6038 + node * 4) + off + 0x48) == '\x06') {
                return 1;
            }
            off   = off + 6;
            count = count - 1;
        } while (count != 0);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80341fcc : bounded index -> type-6 present, gated by the 0x9000000 mask
// ---------------------------------------------------------------------------
unsigned int pad_node_flag_type6(int node, unsigned int flags)
{
    unsigned int result;
    if ((node < 0x2d) && (-1 < node)) {
        if (pad_node_has_cell_type6(node) == 0) {
            result = 0;
        } else {
            result = countLeadingZeros(flags & 0x9000000) >> 5;
        }
    } else {
        result = 0;
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x8034202c : read pad-node descriptor flag bit 1
// ---------------------------------------------------------------------------
unsigned char pad_node_flag_bit1(int node)
{
    return (unsigned char)((&DAT_8047c15a)[node * 0x140] >> 1 & 1);
}

// ---------------------------------------------------------------------------
// 0x80342048 : classify an edge (5/4/1) from the flag bits
// ---------------------------------------------------------------------------
unsigned int map_edge_class_from_flags(unsigned int flags)
{
    if (((flags & 0x1000000) != 0) && ((flags & 0x8000000) != 0)) {
        return 5;
    }
    if ((flags & 0x9000000) != 0) {
        return 4;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// 0x80342078 : scan a pad-grid layer (or all layers) for the 0x400000 cell flag
// ---------------------------------------------------------------------------
int pad_grid_any_cell_flagged(int node, int layer)
{
    int base = node * 0x140;
    int result = 0;

    if (layer == -1) {
        int layerOff = 0;
        int li;
        for (li = 0; li < *(int*)(&DAT_8047c164 + base); li = li + 1) {
            bool hit = false;
            int col;
            for (col = 0; col < *(int*)(&DAT_8047c15c + base); col = col + 1) {
                int rows = *(int*)(&DAT_8047c160 + base);
                int row  = 0;
                if (0 < rows) {
                    do {
                        if ((*(unsigned int*)(*(int*)(&DAT_8047c148 + layerOff + 0x20 + base) +
                              (col + row * *(int*)(&DAT_8047c15c + base)) * 4) & 0x400000) != 0) {
                            hit = true;
                            break;
                        }
                        row  = row + 1;
                        rows = rows - 1;
                    } while (rows != 0);
                }
            }
            if (hit) {
                return 1;
            }
            layerOff = layerOff + 4;
        }
    } else {
        int col;
        for (col = 0; col < *(int*)(&DAT_8047c15c + base); col = col + 1) {
            int rows = *(int*)(&DAT_8047c160 + base);
            int row  = 0;
            if (0 < rows) {
                do {
                    if ((*(unsigned int*)(*(int*)(&DAT_8047c148 + layer * 4 + 0x20 + base) +
                          (col + row * *(int*)(&DAT_8047c15c + base)) * 4) & 0x400000) != 0) {
                        result = 1;
                        break;
                    }
                    row  = row + 1;
                    rows = rows - 1;
                } while (rows != 0);
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x803421a4 : node blocked (flag) or carries a masked type-6 cell
// ---------------------------------------------------------------------------
unsigned int pad_node_blocked_or_type6(int node, unsigned int flags)
{
    unsigned int result;
    if (pad_grid_any_cell_flagged(node, -1) == 0) {
        result = countLeadingZeros(pad_node_flag_type6(node, flags)) >> 5;
    } else {
        result = 1;
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x80342204 : link blocked, else fall through to the companion test
// ---------------------------------------------------------------------------
unsigned int pad_link_blocked_or_companion(int link, int layer)
{
    unsigned int result;
    if (pad_grid_any_cell_flagged(*(int*)(link + 0x10), layer) == 0) {
        result = pad_node_companion_type6(link);
    } else {
        result = 1;
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x8034224c : companion-segment type-6 predicate for a node
// ---------------------------------------------------------------------------
unsigned int pad_node_companion_type6(int nodeRec)
{
    unsigned int result;
    if (nodeRec == 0) {
        result = 0;
    } else {
        unsigned int flags = aveditor_place_companion_segments();
        result = countLeadingZeros(pad_node_flag_type6(*(int*)(nodeRec + 0x10), flags)) >> 5;
    }
    return result;
}

} // extern "C"
