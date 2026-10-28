// Batch: batch_968 | Module: arcade/challenge editor node-buffer loader + grid neighbour-flood
// Address range: 0x80341018 - 0x80341778
// Date: 2026-09-12
//
// The arcade/challenge editor's per-node mesh-buffer loader and the grid neighbour-flood adjacency
// walk (continues the editor cluster from batches 960-967). aveditor_node_buffers_load_all loops the
// 45 (0x2d) node descriptors and loads each one's vertex/index buffers. aveditor_node_buffer_load_one
// pulls the pending stream record for one node, allocates the vertex buffer (count*0x20 + 0x2c) and
// the index buffer, copies both from the stream payload, then (when the descriptor's low flag is set)
// bakes the node's world transform into every vertex via a translation matrix. The grid-flood helpers
// walk placed cells testing 4-/8-connected adjacency: aveditor_grid_edge_crosses hit-tests a segment's
// two endpoints against the roster grid, aveditor_grid_segment_adjacent decides whether two grid cells
// are edge/diagonal adjacent, aveditor_grid_find_matching_cell scans the placed-cell table for a cell
// matching a target grid coordinate, and aveditor_grid_flood_neighbors visits all 8 neighbours of a
// cell, invoking the match scan for each. Node state lives in the DAT_805d09ec / DAT_805d0938 buffer
// tables (indexed by node*4) and the DAT_8047c15c descriptor table (0x140-byte stride).
// Callers: aveditor_spawn_secondary_previews, FUN_80341e64.
//
//   aveditor_node_buffers_load_all   0x80341018  load all 45 node vertex/index buffers.
//   aveditor_node_buffer_load_one    0x80341058  load one node's buffers + bake its world transform.
//   aveditor_grid_edge_crosses       0x80341248  hit-test a segment's endpoints against the roster grid.
//   aveditor_grid_segment_adjacent   0x803414bc  test whether two grid cells are edge/diagonal adjacent.
//   aveditor_grid_find_matching_cell 0x8034163c  scan placed cells for one matching a target grid coord.
//   aveditor_grid_flood_neighbors    0x80341778  visit all 8 neighbours of a cell, running the match scan.
//
// NON-MATCHING: reproduces the decompiler's structure. The 64-bit register-pair returns from the
// scope-push helpers (FUN_803826e4/e8) carry the loop bound in the high word and the grid coord in the
// low word; here they are split into explicit locals seeded from a helper call (Compile-Clean rule 3).
// The int->double magic-constant conversions (CONCAT44 with 0x43300000 / DOUBLE_8061ac00) are
// reproduced verbatim. The _savegpr_ helper and mem_alloc/leaf_31e8 are declared file-locally with
// call-site-accurate arity, since the same addresses are used with different signatures across the
// tree (Compile-Clean rule 5). CONCAT22/CONCAT44 come from common.h.

#include "common.h"

extern "C" {

extern unsigned char* DAT_80612be4;   // editor context base (byte*)

// node vertex/index buffer tables (indexed by node*4)
extern int   DAT_805d09ec;            // vertex-buffer pointer table base
extern int   DAT_805d0938;            // index-buffer pointer table base
extern int   DAT_805d59a0;            // node-buffer alloc tag seed

// grid descriptor table (0x140 stride), transform basis
extern int   DAT_8047c15a;            // descriptor flags byte (low bit = has transform)
extern int   DAT_8047c15c;            // descriptor translation X (int)
extern float DAT_8047ba18;            // identity/basis matrix source

// int->double conversion magic
extern double DOUBLE_8061ac00;
extern float  FLOAT_8061abf8;
extern float  FLOAT_8061abfc;

// named cross-module helpers
extern int   aveditor_grid_lookup(int a, int b, int c);
extern int   roster_hit_test_point(double x, double y, double z);
extern void  aveditor_grid_flood_noop(void);
extern void  mtx4_copy(float* src, float* dst);
extern void  mtx4_write_translation(double x, double y, double z, int mtx);
extern void  mtx4_transform_point(float* mtx, float* pt);
extern int   stream_find_pending_by_name(void);

// raw helpers used with local call-site signatures
extern unsigned long long __save_gpr_25(void);
extern int   mem_alloc(unsigned long long f1, unsigned long long p2, unsigned long long p3,
                      unsigned long long p4, unsigned long long p5, unsigned long long p6,
                      unsigned long long p7, unsigned long long p8, int size, int a,
                      unsigned int tag, int seed, unsigned int r7, unsigned int r8,
                      unsigned int r9, unsigned int r10);
extern unsigned long long leaf_31e8(unsigned int dst, unsigned int src, int len);
extern unsigned long long FUN_803826e4(void);   // scope push (variant a)
extern unsigned long long FUN_803826e8(void);   // scope push (variant b)
extern void  FUN_80382730(void);                 // scope pop (variant a)
extern void  FUN_80382734(void);                 // scope pop (variant b)

// forward decl within this TU
void aveditor_node_buffer_load_one(unsigned long long p1, unsigned long long p2,
                                   unsigned long long p3, unsigned long long p4,
                                   unsigned long long p5, unsigned long long p6,
                                   unsigned long long p7, unsigned long long p8);

// ---------------------------------------------------------------------------
// 0x80341018 : load all 45 node vertex/index buffers
// ---------------------------------------------------------------------------
void aveditor_node_buffers_load_all(unsigned long long p1, unsigned long long p2,
                                    unsigned long long p3, unsigned long long p4,
                                    unsigned long long p5, unsigned long long p6,
                                    unsigned long long p7, unsigned long long p8)
{
    int i = 0;
    do {
        aveditor_node_buffer_load_one(p1, p2, p3, p4, p5, p6, p7, p8);
        i = i + 1;
    } while (i < 0x2d);
}

// ---------------------------------------------------------------------------
// 0x80341058 : load one node's vertex/index buffers + bake its world transform
// ---------------------------------------------------------------------------
void aveditor_node_buffer_load_one(unsigned long long p1, unsigned long long p2,
                                   unsigned long long p3, unsigned long long p4,
                                   unsigned long long p5, unsigned long long p6,
                                   unsigned long long p7, unsigned long long p8)
{
    int   vtxCount;
    int   idxCount;
    int   allocTag = DAT_805d59a0 * 0xb40;
    unsigned int seedSpace = 0x805d0000;
    int   pending;
    int   node;
    int   nodeSlot;
    int   vtxBytes;
    unsigned int in_r7 = 0, in_r8 = 0, in_r9 = 0, in_r10 = 0;
    unsigned long long f1 = 0;
    int*  idxSrc;
    float mtx[16];
    unsigned int negX;
    unsigned long long gpr;

    gpr  = __save_gpr_25();
    node = (int)((unsigned long long)gpr >> 0x20);
    (void)p1;

    pending  = stream_find_pending_by_name();
    vtxCount = *(int*)(*(int*)(pending + 0x10) + 4);

    if ((int)gpr != 0) {
        int vb = mem_alloc(f1, p2, p3, p4, p5, p6, p7, p8,
                           vtxCount * 0x20 + 0x2c, 1, seedSpace, allocTag,
                           in_r7, in_r8, in_r9, in_r10);
        *(int*)(&DAT_805d09ec + node * 4) = vb;
    }

    nodeSlot = node * 4;
    vtxBytes = vtxCount * 0x20 + 0x2c;
    leaf_31e8(*(unsigned int*)(&DAT_805d09ec + nodeSlot), *(unsigned int*)(pending + 0x10), vtxBytes);

    idxSrc   = (int*)(*(int*)(pending + 0x10) + vtxCount * 0x20 + 0xc +
                      *(int*)(*(int*)(pending + 0x10) + 8) * 0x14);
    idxCount = *idxSrc;

    if ((int)gpr != 0) {
        int ib = mem_alloc(0, p2, p3, p4, p5, p6, p7, p8,
                           idxCount * 0x20 + 0x2c, 1, vtxBytes, allocTag,
                           in_r7, in_r8, in_r9, in_r10);
        *(int*)(&DAT_805d0938 + nodeSlot) = ib;
    }

    *(int*)(*(int*)(&DAT_805d0938 + nodeSlot) + 4) = idxCount;
    *(unsigned int*)(*(int*)(&DAT_805d0938 + nodeSlot) + 8) = 0;
    leaf_31e8(*(int*)(&DAT_805d0938 + nodeSlot) + 0xc, (unsigned int)(idxSrc + 1), idxCount << 5);

    if (((&DAT_8047c15a)[node * 0x140] & 1) != 0) {
        int k;
        int off;
        mtx4_copy((float*)&DAT_8047ba18, mtx);
        negX = *(unsigned int*)(&DAT_8047c15c + node * 0x140) ^ 0x80000000;
        mtx4_write_translation(
            (double)(FLOAT_8061abf8 *
                     (float)((double)CONCAT44(0x43300000, negX) - DOUBLE_8061ac00)),
            (double)FLOAT_8061abfc, (double)FLOAT_8061abfc, (int)mtx);

        off = 0;
        for (k = 0; k < vtxCount; k = k + 1) {
            mtx4_transform_point(mtx, (float*)(*(int*)(&DAT_805d09ec + nodeSlot) + off + 0x18));
            off = off + 0x20;
        }
        off = 0;
        for (k = 0; k < idxCount; k = k + 1) {
            mtx4_transform_point(mtx, (float*)(*(int*)(&DAT_805d0938 + nodeSlot) + off + 0x18));
            off = off + 0x20;
        }
    }

    __save_gpr_25();  // paired _restgpr_ in the original
}

// ---------------------------------------------------------------------------
// 0x80341248 : hit-test a segment's two endpoints against the roster grid
// ---------------------------------------------------------------------------
int aveditor_grid_edge_crosses(void)
{
    short* hiPt;
    unsigned int hiHit;
    unsigned int loHit;
    int hiCell;
    int loCell;
    short* loPt;
    unsigned long long pair;

    pair = FUN_803826e8();
    hiPt = (short*)((unsigned long long)pair >> 0x20);
    loPt = (short*)pair;

    hiHit = roster_hit_test_point(
        (double)(float)((double)CONCAT44(0x43300000, (int)*hiPt ^ 0x80000000) - DOUBLE_8061ac00),
        (double)(float)((double)CONCAT44(0x43300000, (int)hiPt[1] ^ 0x80000000) - DOUBLE_8061ac00),
        (double)(float)((double)CONCAT44(0x43300000, (int)hiPt[2] ^ 0x80000000) - DOUBLE_8061ac00));
    loHit = roster_hit_test_point(
        (double)(float)((double)CONCAT44(0x43300000, (int)*loPt ^ 0x80000000) - DOUBLE_8061ac00),
        (double)(float)((double)CONCAT44(0x43300000, (int)loPt[1] ^ 0x80000000) - DOUBLE_8061ac00),
        (double)(float)((double)CONCAT44(0x43300000, (int)loPt[2] ^ 0x80000000) - DOUBLE_8061ac00));

    hiCell = aveditor_grid_lookup(loHit, (int)hiPt[1] ^ 0x80000000, 0x43300000);
    loCell = aveditor_grid_lookup((int)loPt[1] ^ 0x80000000, (int)loPt[1], 0x43300000);

    if (((hiCell != loCell) && ((hiHit & 0x4000) == 0)) && ((loHit & 0x4000) == 0)) {
        aveditor_grid_flood_noop();
        aveditor_grid_flood_noop();
    }
    FUN_80382734();
    return 0;
}

// ---------------------------------------------------------------------------
// 0x803414bc : test whether two grid cells are edge/diagonal adjacent
// ---------------------------------------------------------------------------
unsigned int aveditor_grid_segment_adjacent(short* a, short* b)
{
    int hit;

    if ((*a == *b) || (a[1] == b[1])) {
        return (unsigned int)aveditor_grid_edge_crosses();
    }
    hit = aveditor_grid_edge_crosses();
    if ((hit != 0) && (hit = aveditor_grid_edge_crosses(), hit != 0)) {
        return 1;
    }
    hit = aveditor_grid_edge_crosses();
    if ((hit != 0) && (hit = aveditor_grid_edge_crosses(), hit != 0)) {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x8034163c : scan placed cells for one matching a target grid coordinate
// ---------------------------------------------------------------------------
void aveditor_grid_find_matching_cell(unsigned int p1, unsigned int p2, int refNode,
                                      short* target, unsigned int* refCoord)
{
    int    cell;
    float* pos;
    int    stride;
    int    node;
    unsigned long long pair;
    unsigned int  segA;
    unsigned short segAhi;
    unsigned int  targetXY;
    short  targetZ;
    long long tmp;

    (void)p1; (void)p2;
    pair   = FUN_803826e4();
    node   = (int)((unsigned long long)pair >> 0x20);
    stride = node * 0xe4;

    do {
        if (((int)pair <= node) ||
            (cell = (int)(intptr_t)DAT_80612be4 + stride, *(int*)(cell + 0x2e0) != 1)) {
            goto done;
        }
        pos = *(float**)(cell + 0x2ec);
        tmp = (long long)(int)*pos;
        if (((int)*target == (int)*(short*)(cell + 0x2f0) + (int)*pos) &&
            ((tmp = (long long)(int)pos[1],
              (int)target[1] == (int)*(short*)(cell + 0x2f2) + (int)pos[1] &&
              (tmp = (long long)(int)pos[2],
               (int)target[2] == (int)*(short*)(cell + 0x2f4) + (int)pos[2])))) {
            if (pos == *(float**)(refNode * 0xe4 + (intptr_t)DAT_80612be4 + 0x2ec)) {
done:
                FUN_80382730();
                return;
            }
            segA     = *refCoord;
            segAhi   = *(unsigned short*)(refCoord + 1);
            targetXY = *(unsigned int*)target;
            targetZ  = target[2];
            {
                unsigned int packA = segA;
                unsigned short packAhi = segAhi;
                unsigned int packB = targetXY;
                short packBz = targetZ;
                short segB[3];
                short segArr[3];
                segArr[0] = (short)(packB & 0xffff);
                segArr[1] = (short)(packB >> 16);
                segArr[2] = packBz;
                segB[0] = (short)(packA & 0xffff);
                segB[1] = (short)(packA >> 16);
                segB[2] = (short)packAhi;
                if (aveditor_grid_segment_adjacent(segArr, segB) != 0) {
                    goto done;
                }
            }
        }
        node   = node + 1;
        stride = stride + 0xe4;
        (void)tmp;
    } while (true);
}

// ---------------------------------------------------------------------------
// 0x80341778 : visit all 8 neighbours of a cell, running the match scan
// ---------------------------------------------------------------------------
void aveditor_grid_flood_neighbors(unsigned int p1, unsigned int p2, int refNode, short* center)
{
    unsigned int hi;
    unsigned int lo;
    unsigned long long pair;
    short neigh[3];
    unsigned int ref[2];

    (void)p1; (void)p2;
    pair = FUN_803826e8();
    hi = (unsigned int)((unsigned long long)pair >> 0x20);
    lo = (unsigned int)pair;

    ref[0] = *(unsigned int*)center;
    ref[1] = (unsigned int)(unsigned short)center[2];

    // 8-connected neighbourhood (dx,dy in {-1,0,1}, excluding 0,0)
    #define AVE_FLOOD(DX, DY) \
        neigh[0] = (short)(*center + (DX)); \
        neigh[1] = (short)(center[1] + (DY)); \
        neigh[2] = center[2]; \
        aveditor_grid_find_matching_cell(hi, lo, refNode, neigh, ref)

    AVE_FLOOD(-1, -1);
    AVE_FLOOD( 0, -1);
    AVE_FLOOD(+1, -1);
    AVE_FLOOD(-1,  0);
    AVE_FLOOD(+1,  0);
    AVE_FLOOD(-1, +1);
    AVE_FLOOD( 0, +1);
    AVE_FLOOD(+1, +1);
    #undef AVE_FLOOD

    FUN_80382734();
}

} // extern "C"
