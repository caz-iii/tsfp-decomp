// Address range: 0x80133f18 - 0x801342e0 | batch_362 | 2026-09-04
//
// kd-tree spatial index used by the scripted animation / expression system to
// resolve a query point (typically a 3D position or a normalized parameter
// triple) against a set of stored keyframes.
//
// Node layout (packed 16-bit entries in a flat array, heap-indexed):
//   Each internal entry is a ushort:
//     bits 15..14 == 0b11  -> LEAF: bits 13..0 index the value/payload table
//     otherwise            -> SPLIT: bits 15..14 = axis (0..2),
//                                    bits 13..0  = split fraction (0..0x3fff)
//   The split position along `axis` is:
//       lo[axis] + FLOAT_80615c60 * (frac - bias) * (hi[axis] - lo[axis])
//   with lo/hi the running AABB bounds carried down the traversal.
//
// Functions:
//   kdtree_range_search_recurse 0x80133f18  recursive both-sides descent + callback
//   kdtree_find_leaf            0x801340a0  descend to the single leaf for a point
//   kdtree_range_search         0x801341d8  entry: AABB reject then recurse
//   kdtree_compute_depth        0x80134258  recursive max depth of a 0x2c-node tree
//   kdtree_node_alloc           0x801342b8  mem_alloc wrapper (+0x10 header)
//   kdtree_node_free            0x801342e0  mem_free_top wrapper

#include "common.h"

// Heap allocator (declared in mem_alloc.cpp / shared header).
extern s32  mem_alloc(s32 size, s32 type);
extern void mem_free_top(s32 ptr);

// FP scratch-frame push/pop helpers (0x8038xxxx).  Each "enter" returns a
// packed (nodePtr<<32)|dataPtr pair matching the raw r3:r4 ABI.
extern unsigned long long FUN_803826d8(void);   // enter (recurse variant)
extern unsigned long long FUN_803826e8(void);   // enter (range-search variant)
extern unsigned long long FUN_803826f0(void);   // enter (find-leaf variant)
extern void               FUN_80382724(void);    // leave (recurse variant)
extern void               FUN_80382734(void);    // leave (range-search variant)
extern void               FUN_8038273c(void);    // leave (find-leaf variant)

// AABB / point overlap predicates (return non-zero when the query overlaps).
extern int FUN_80011750(float* query, float* lo, float* hi);
extern int FUN_800117cc(float* query, float* radius, float* lo, float* hi);

extern const float  FLOAT_80615c60;   // split-fraction normalizer (1/0x3fff style)
extern const double DOUBLE_80615c68;  // 2^52 bias for the frac int->float convert

// ---------------------------------------------------------------------------
// 0x80133f18 : recursive range search.  At each split node the query interval
// [lo,hi] is tested against the split plane; whichever child sub-intervals the
// query can reach are visited (both, in the overlapping case).  Leaf nodes
// (top two bits set) invoke the user callback with the payload pointer pair.
//
//   nodes     packed node array (int* : [0]=tree base, [1]=value table base)
//   dataPtr   per-query scratch (the descended point)
//   queryLo   caller's lower bound array
//   nodeIdx   current node index (walked as nodeIdx*2 into the ushort array)
//   lo, hi    running AABB bounds (float[3] each)
//   cb        leaf callback
//   userArg   opaque callback argument
//
// Returns 1 when the callback signalled "stop" via the near-child descent.
// ---------------------------------------------------------------------------
typedef void (*KdLeafCallback)(int nodePtr, int queryLo, unsigned int arg,
                               unsigned short a, unsigned short b);

int kdtree_range_search_recurse(int* nodes, int dataPtr, int queryLo, int nodeIdx,
                                float* lo, float* hi, void* cb, unsigned int userArg)
{
    unsigned short entry = *(unsigned short*)(*nodes + nodeIdx * 2);

    if ((entry & 0xc000) == 0xc000) {
        // leaf: resolve payload and fire the callback
        unsigned short* payload =
            (unsigned short*)(nodes[1] + (entry & 0x3fff) * 2);
        ((KdLeafCallback)cb)(dataPtr, queryLo, userArg, payload[0], payload[1]);
        return 0;
    }

    int axis = (int)(unsigned int)entry >> 0xe;
    unsigned int fracBits = (entry & 0x3fff) ^ 0x80000000;
    float frac = (float)((double)CONCAT44(0x43300000, fracBits) - DOUBLE_80615c68);
    float split = FLOAT_80615c60 * frac * (hi[axis] - lo[axis]) + lo[axis];

    // Near side (towards lo): shrink hi to the split plane.
    if ((double)*(float*)(dataPtr + axis * 4) <= (double)split) {
        float childHi[3];
        childHi[0] = hi[0];
        childHi[1] = hi[1];
        childHi[2] = hi[2];
        childHi[axis] = split;
        int r = kdtree_range_search_recurse(nodes, dataPtr, queryLo, nodeIdx * 2,
                                            lo, childHi, cb, userArg);
        if (r == 1) {
            FUN_80382724();
            return 1;
        }
    }

    // Far side (towards hi): raise lo to the split plane.
    if ((double)split <= (double)*(float*)(queryLo + axis * 4)) {
        float childLo[3];
        childLo[0] = lo[0];
        childLo[1] = lo[1];
        childLo[2] = lo[2];
        childLo[axis] = split;
        kdtree_range_search_recurse(nodes, dataPtr, queryLo, nodeIdx * 2 + 1,
                                    childLo, hi, cb, userArg);
    }

    FUN_80382724();
    return 0;
}

// ---------------------------------------------------------------------------
// 0x801340a0 : descend to the single leaf whose cell contains the query point,
// writing the leaf payload address to *outLeaf.  Iterative (no recursion): at
// each split the point is compared to the split plane and the appropriate
// child heap-index (2i / 2i+1) is taken until a leaf entry is reached.
// ---------------------------------------------------------------------------
void kdtree_find_leaf(int* outLeaf)
{
    unsigned long long frame = FUN_803826f0();
    int* nodes = (int*)(unsigned int)(frame >> 0x20);
    float* query = (float*)(unsigned int)frame;

    int ok = FUN_80011750(query, (float*)(nodes + 2), (float*)(nodes + 5));
    if (ok != 0) {
        int treeBase = nodes[0];
        int heapIdx = 1;

        float lo[3];
        float hi[3];
        lo[0] = (float)nodes[2];
        lo[1] = (float)nodes[3];
        lo[2] = (float)nodes[4];
        hi[0] = (float)nodes[5];
        hi[1] = (float)nodes[6];
        hi[2] = (float)nodes[7];

        unsigned short entry = *(unsigned short*)(treeBase + 2);
        while ((entry & 0xc000) != 0xc000) {
            int axis = (int)(unsigned int)entry >> 0xe;
            unsigned int fracBits = (entry & 0x3fff) ^ 0x80000000;
            float frac = (float)((double)CONCAT44(0x43300000, fracBits) - DOUBLE_80615c68);
            float split = FLOAT_80615c60 * frac * (hi[axis] - lo[axis]) + lo[axis];

            if (split <= query[axis]) {
                lo[axis] = split;
                heapIdx = heapIdx * 2 + 1;
            } else {
                hi[axis] = split;
                heapIdx = heapIdx << 1;
            }
            entry = *(unsigned short*)(treeBase + heapIdx * 2);
        }

        *outLeaf = nodes[1] + (entry & 0x3fff) * 2 + 2;
    }

    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x801341d8 : range-search entry point.  Rejects the whole tree with a cheap
// AABB/radius test, then kicks off the recursive descent from the root.
// ---------------------------------------------------------------------------
void kdtree_range_search(float* queryLo, void* cb, unsigned int userArg)
{
    unsigned long long frame = FUN_803826e8();
    int nodes = (int)(unsigned int)(frame >> 0x20);
    float* query = (float*)(unsigned int)frame;

    int ok = FUN_800117cc(query, queryLo, (float*)(nodes + 8), (float*)(nodes + 0x14));
    if (ok != 0) {
        kdtree_range_search_recurse((int*)nodes, (int)query, (int)queryLo, 1,
                                    (float*)(nodes + 8), (float*)(nodes + 0x14),
                                    cb, userArg);
    }

    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x80134258 : compute the maximum depth of a 0x2c-byte binary tree node.
// Child pointer lives at +0x10; the two subtrees are the child node and the
// child+0x2c sibling.  Empty subtree (child == 0) has depth 0.
// ---------------------------------------------------------------------------
int kdtree_compute_depth(int node)
{
    int depth;
    if (*(int*)(node + 0x10) == 0) {
        depth = 0;
    } else {
        int left  = kdtree_compute_depth(*(int*)(node + 0x10));
        int right = kdtree_compute_depth(*(int*)(node + 0x10) + 0x2c);
        depth = (right < left) ? left : right;
        depth += 1;
    }
    return depth;
}

// ---------------------------------------------------------------------------
// 0x801342b8 : allocate a tree node, reserving a 0x10-byte header ahead of the
// requested payload (allocation type 0 = default/permanent).
// ---------------------------------------------------------------------------
int kdtree_node_alloc(s32 size)
{
    return mem_alloc(size + 0x10, 0);
}

// ---------------------------------------------------------------------------
// 0x801342e0 : free a tree node previously handed out by mem_alloc.
// ---------------------------------------------------------------------------
void kdtree_node_free(int ptr)
{
    mem_free_top(ptr);
}

// ===========================================================================
// kd-tree builder (batch_363) - the write side of the index read above.
//
// The builder works on a transient node graph allocated from a private free
// chain (DAT_80612080) so the whole scratch tree can be released in one pass.
// It picks, per node, the axis whose split best balances the AABB overlap of
// the child point sets (a cheap surface-area-heuristic style cost), partitions
// the points, and recurses.  Once built, the graph is measured
// (kdtree_count_serialized_size) and flattened into the packed ushort array
// the reader consumes (kdtree_serialize).
//
//   kdtree_build_node_alloc      0x80134300  alloc build node + link free chain
//   kdtree_build_free_all        0x80134338  release the whole build chain
//   kdtree_build_recursive       0x80134380  choose split axis, partition, recurse
//   kdtree_serialize             0x80134758  flatten build tree -> packed nodes
//   kdtree_count_serialized_size 0x80134894  count packed entries needed
//   kdtree_build                 0x80134914  top-level build entry point
// ===========================================================================

extern int  DAT_80612080;             // head of the transient build-node free chain

extern const float FLOAT_80615c70;    // split-quantize bias
extern const float FLOAT_80615c74;    // split-quantize scale (0x3fff range)
extern const float FLOAT_80615c78;    // initial split fraction seed

// GPR save/restore thunks emitted by the PPC prologue of the big builder.
extern unsigned long long __save_gpr_25(void);
extern void               __restore_gpr_25(void);

// A build node is a small header (payload starts +0x10 after alloc):
//   +0x00 next-in-freechain (used only while allocated)
// The logical build-node fields the builder manipulates (via int* piVar2):
//   [0]=points base, [1]=count, [2]=split frac(int), [3]=axis, [4]=child ptr,
//   [5..10]=AABB (lo xyz, hi xyz), [0xb]=right child, [0xc]=right count, ...

// ---------------------------------------------------------------------------
// 0x80134300 : allocate one build node (payload size `size`), push it onto the
// transient free chain so kdtree_build_free_all can reclaim everything, and
// return a pointer past the 0x10-byte chain header.
// ---------------------------------------------------------------------------
int* kdtree_build_node_alloc(int size)
{
    int* node = (int*)kdtree_node_alloc_raw(size + 0x10);
    *node = DAT_80612080;                 // link->next = chain head
    DAT_80612080 = (int)node;             // chain head = node
    return node + 4;                      // payload starts after 0x10-byte header
}

// ---------------------------------------------------------------------------
// 0x80134338 : free every node on the transient build chain.
// ---------------------------------------------------------------------------
void kdtree_build_free_all(void)
{
    int* p = (int*)DAT_80612080;
    while (p != 0) {
        int next = *p;
        kdtree_node_free((int)p);
        p = (int*)next;
    }
    DAT_80612080 = 0;
}

// ---------------------------------------------------------------------------
// 0x80134894 : count the packed entries a serialized subtree will occupy.
// Leaf (child ptr at +0x10 == 0) with a non-empty point count contributes
// count+1 slots; internal nodes recurse into both halves.
// ---------------------------------------------------------------------------
void kdtree_count_serialized_size(int heapIdx, int node, short* outCount)
{
    (void)heapIdx;
    if (*(int*)(node + 0x10) == 0) {
        if (*(int*)(node + 4) != 0) {
            *outCount = (short)(*outCount + (short)*(int*)(node + 4) + 1);
        }
    } else {
        int child = *(int*)(node + 0x10);
        kdtree_count_serialized_size(heapIdx * 2, child, outCount);
        kdtree_count_serialized_size(heapIdx * 2 + 1, child + 0x2c, outCount);
    }
}

// ---------------------------------------------------------------------------
// 0x80134758 : flatten a build node into the packed ushort node array.
//
//   heapIdx   destination heap slot (byte offset heapIdx*2 into `nodes`)
//   node      current build node (int[] view)
//   valCursor in/out cursor into the value/payload table (ushort*)
//   nodes     packed node array base
//   valBase   value table base
//
// Leaf nodes emit a payload run (count + point ids) and OR in the 0xc000 leaf
// marker; internal nodes emit the quantized split (axis<<14 | frac) and recurse
// into both children (child, child+0x2c).
// ---------------------------------------------------------------------------
void kdtree_serialize(int heapIdx, int node, unsigned short* valCursor,
                      int nodes, int valBase)
{
    int* n = (int*)node;
    int slot = heapIdx * 2;

    if (n[4] == 0) {
        // leaf
        unsigned short* payload =
            (unsigned short*)(valBase + (unsigned int)*valCursor * 2);
        if (n[1] == 0) {
            *(unsigned short*)(nodes + slot) = 0;
        } else {
            *(unsigned short*)(nodes + slot) = *valCursor;
        }
        *(unsigned short*)(nodes + slot) |= 0xc000;

        if (n[1] > 0) {
            *payload = (short)n[1];
            int off = 0;
            int src = 0;
            for (int i = 0; i < n[1]; i++) {
                off += 2;
                int* ptEntry = (int*)(*n + src);
                src += 4;
                *(unsigned short*)((char*)payload + off) = *(unsigned short*)(*ptEntry);
            }
            *valCursor = (unsigned short)(*valCursor + (short)n[1] + 1);
        }
    } else {
        // internal: quantized split
        short frac = (short)(int)(FLOAT_80615c74 * (float)n[2] + FLOAT_80615c70);
        *(unsigned short*)(nodes + slot) = (unsigned short)frac;
        *(unsigned short*)(nodes + slot) |= (unsigned short)(n[3] << 0xe);

        kdtree_serialize(slot, n[4], valCursor, nodes, valBase);
        kdtree_serialize(slot + 1, n[4] + 0x2c, valCursor, nodes, valBase);
    }
}

// ---------------------------------------------------------------------------
// 0x80134380 : recursively split a build node.
//
// For each candidate axis it counts, for every point, how many other points'
// intervals lie entirely to the near / far side of that point's split, and
// scores the axis by how balanced (and non-degenerate) the resulting partition
// is (0x7fffffff = unusable).  The best axis is chosen, the node is split at
// the median point, two child nodes are allocated and populated, and the
// builder recurses into both while any child remains.
//
//   node    packed pair (buildNode<<32)|pointNode passed via __save_gpr_25 ABI
//   depth   current recursion depth (param_11)
// ---------------------------------------------------------------------------
void kdtree_build_recursive(int buildNode, int pointNode, int depth)
{
    int* self  = (int*)buildNode;   // node being subdivided (piVar2)
    int* owner = (int*)pointNode;   // owning point set (piVar5)

    self[4] = 0;                    // child = none (leaf by default)
    int* rightChild = 0;

    if (*owner < self[1] && depth != owner[1]) {
        // scratch cost table, one int per point per axis
        int cost = kdtree_node_alloc_raw(self[1] << 2);

        float bestSplit[3];
        int   bestId[3];
        int   axisOff = 0;

        for (int axis = 0; axis < 3; axis++) {
            int pointOff = 0;
            int count = self[1];
            for (int i = 0; i < count; i++) {
                int nearCnt = 0, farCnt = 0;
                float pivot = *(float*)(axisOff + *(int*)(*self + pointOff) + 0x10);
                int scanOff = 0;
                for (int j = count; j > 0; j--) {
                    int entry = axisOff + *(int*)(*self + scanOff);
                    if (*(float*)(entry + 4) <= pivot) nearCnt++;
                    if (pivot < *(float*)(entry + 0x10)) farCnt++;
                    scanOff += 4;
                }
                int overlap = (nearCnt + farCnt) - count;
                if (overlap == count || nearCnt == count) {
                    *(int*)(cost + pointOff) = 0x7fffffff;
                } else {
                    int diff = nearCnt - farCnt;
                    int mag = diff < 0 ? -diff : diff;
                    *(int*)(cost + pointOff) = overlap + mag;
                }
                pointOff += 4;
            }

            // pick the cheapest point on this axis
            int best = 0;
            int scanOff = 4;
            for (int k = 1; k < count; k++) {
                if (*(int*)(cost + scanOff) < *(int*)(cost + best * 4)) best = k;
                scanOff += 4;
            }
            int bestCost = *(int*)(cost + best * 4);
            int* bestPt = *(int**)(*self + best * 4);
            *(int*)((char*)bestId + axisOff)   = bestCost;
            *(float*)((char*)bestSplit + axisOff) =
                *(float*)((char*)bestPt + axisOff + 0x10);
            axisOff += 4;
        }
        kdtree_node_free(cost);

        // choose the axis with the smallest cost across the three
        unsigned int chosen;
        if (bestId[0] < bestId[1]) {
            chosen = (bestId[0] < bestId[2]) ? 0u : 2u;
        } else {
            chosen = (bestId[1] < bestId[2]) ? 1u : 2u;
        }

        if (bestId[chosen] < 0x7fffffff) {
            self[3] = (int)chosen;
            float splitVal = bestSplit[chosen];
            float lo = (float)self[5 + chosen];
            float hi = (float)self[8 + chosen];
            self[2] = (int)(float)((splitVal - lo) / (hi - lo));

            // left child
            rightChild = kdtree_build_node_alloc(0x58);
            self[4] = (int)rightChild;

            // count points below the split for the left child
            int belowCnt = 0;
            {
                int off = 0;
                for (int i = self[1]; i > 0; i--) {
                    int* pe = (int*)((chosen * 4) + *(int*)(*self + off));
                    if ((float)pe[1] <= splitVal) belowCnt++;
                    off += 4;
                }
            }
            rightChild[1] = belowCnt;

            int* leftPts = kdtree_build_node_alloc(belowCnt << 2);
            *rightChild = (int)leftPts;

            for (int c = 5; c <= 10; c++) rightChild[c] = self[c];
            (rightChild + chosen)[8] = (int)splitVal;

            int aboveCnt = 0;
            {
                int off = 0;
                for (int i = self[1]; i > 0; i--) {
                    int* pe = (int*)((chosen * 4) + *(int*)(*self + off));
                    if (splitVal < (float)pe[4]) aboveCnt++;
                    off += 4;
                }
            }
            rightChild[0xc] = aboveCnt;

            int* rightPts = kdtree_build_node_alloc(aboveCnt << 2);
            rightChild[0xb] = (int)rightPts;

            for (int c = 0x10; c <= 0x15; c++) rightChild[c] = self[c - 0xb];
            (rightChild + chosen)[0x10] = (int)splitVal;

            // partition points into the two child arrays
            int leftOff = 0, rightOff = 0, srcOff = 0;
            for (int i = 0; i < self[1]; i++) {
                int* pe = (int*)((chosen * 4) + *(int*)(*self + srcOff));
                if ((float)*(float*)((char*)pe + 4) <= splitVal) {
                    *(int*)(*rightChild + leftOff) = *(int*)(*self + srcOff);
                    leftOff += 4;
                }
                int id = *(int*)((char*)*self + srcOff);
                if (splitVal < *(float*)((char*)pe + 0x10)) {
                    *(int*)(rightChild[0xb] + rightOff) = id;
                    rightOff += 4;
                }
                srcOff += 4;
            }
        }

        if (self[4] != 0) {
            kdtree_build_recursive((int)(rightChild + 0xb), pointNode, depth + 1);
            kdtree_build_recursive((int)rightChild, pointNode, depth + 1);
        }
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// 0x80134914 : top-level build.  Computes the overall AABB of the input point
// set, applies a small per-axis fattening, allocates the root build node,
// runs kdtree_build_recursive, sizes the output (kdtree_compute_depth for the
// node array, kdtree_count_serialized_size for the value table), allocates
// both via the caller-supplied allocator, serializes, and copies the AABB into
// the output header.  Frees the transient chain on the way out.
//
//   out       output index header (int[] : [0]=nodes,[1]=values,[2..7]=AABB)
//   points    point array base (0x1c-byte stride entries)
//   count     point count
//   alloc     allocator callback: void* alloc(int byteSize)
// ---------------------------------------------------------------------------
void kdtree_build(int* out, int points, int count, int (*alloc)(int byteSize))
    float hi[3];

    // seed bounds from the first point (offsets +4 = lo, +0x10 = hi)
    for (int a = 0; a < 3; a++) {
        lo[a] = *(float*)(points + a * 4 + 4);
        hi[a] = *(float*)(points + a * 4 + 0x10);
    }
    // accumulate over remaining points
    int rowOff = 0x1c;
    for (int i = 1; i < count; i++) {
        for (int a = 0; a < 3; a++) {
            float l = *(float*)(points + rowOff + a * 4 + 4);
            if (l < lo[a]) lo[a] = l;
            float h = *(float*)(points + rowOff + a * 4 + 0x10);
            if (h > hi[a]) hi[a] = h;
        }
        rowOff += 0x1c;
    }

    // fatten each point's interval by a fraction of the axis extent
    for (int a = 0; a < 3; a++) {
        float pad = (float)(((double)FLOAT_80615c70 * (double)(hi[a] - lo[a])) / (double)FLOAT_80615c74);
        int off = 0;
        for (int i = 0; i < count; i++) {
            *(float*)(points + off + a * 4 + 4)    -= pad;
            *(float*)(points + off + a * 4 + 0x10) += pad;
            off += 0x1c;
        }
    }

    // root build node
    int* root = kdtree_build_node_alloc(count << 2);
    // fill the root point-index array
    {
        int dst = 0, src = 0;
        for (int i = 0; i < count; i++) {
            *(int*)((int)root + dst) = points + src;
            dst += 4;
            src += 0x1c;
        }
    }

    kdtree_build_recursive((int)&root, points /*owner*/, 0);

    // size the packed node array from the tree depth
    int depth = kdtree_compute_depth((int)&root);
    int nodeSlots = 1;
    for (int d = depth; d > 0; d--) nodeSlots = nodeSlots * 2 + 1;

    // size the value/payload table
    short valCount = 1;
    kdtree_count_serialized_size(1, (int)&root, &valCount);

    out[0] = alloc((nodeSlots + 1) * 2);
    out[1] = alloc((int)valCount << 1);
    *(unsigned short*)out[1] = 0;

    unsigned short cursor = 1;
    kdtree_serialize(1, (int)&root, &cursor, out[0], out[1]);

    out[2] = (int)lo[0];
    out[3] = (int)lo[1];
    out[4] = (int)lo[2];
    out[5] = (int)hi[0];
    out[6] = (int)hi[1];
    out[7] = (int)hi[2];

    kdtree_build_free_all();
}
