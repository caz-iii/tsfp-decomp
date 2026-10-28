// spatial_angle_sort.cpp
// Angular spatial-bucket helpers used by the point-cloud angle sorter
// (FUN_8018bcb4 / FUN_8018bea0). Each input point (a float triple x,y,z) is
// reduced to a horizontal angle via atan2, packed into a bucket key, and used
// to bin points into angle-sorted linked lists.
//
// Batch: batch_491
#include "common.h"

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
extern double math_atan2(double y, double x);
extern void   memset_wrapper(int dst, int val, int len);

// Compiler-runtime spill helpers (saved GPR frame records).
extern unsigned long long __save_gpr_25(void);
extern void               __restore_gpr_25(void);

// Frame guards used by the builder / query entry points (return packed record:
// hi word = context ptr, lo word = point ptr / count).
extern unsigned long long FUN_803826c0(void);
extern unsigned long long FUN_803826d8(void);
extern void FUN_8038270c(void);
extern void FUN_80382724(void);

// ---- Data ----
extern float FLOAT_80616818; // angle dead-zone threshold (near-zero radius)
extern float FLOAT_8061681c; // radians -> packed-angle scale
extern float FLOAT_80616820; // reciprocal-count numerator
extern float FLOAT_80616824; // initial best-distance seed (large sentinel)
}

// Forward decls for helpers defined later in this file.
static int spatial_bucket_offset(int count, int group);
static void spatial_list_walk(unsigned int *pNode);

// ---------------------------------------------------------------------------
// Address: 0x8018ba54 | Name: spatial_pack_angle_key | Date: 2026-09-06
// Pack two scaled components into one uint bucket key: low 16 bits = angle*scale,
// high 16 bits = elevation*scale.
// ---------------------------------------------------------------------------
unsigned int spatial_pack_angle_key(double scale, double angle, double elevation)
{
    return ((unsigned int)(int)(angle * scale) & 0xffffU)
         | ((unsigned int)(int)(elevation * scale) << 0x10);
}

// ---------------------------------------------------------------------------
// Address: 0x8018ba8c | Name: spatial_vec_to_angle | Date: 2026-09-06
// Reduce a vector (pVec: x,y,z) to a horizontal angle. When the point is within
// the dead-zone radius (|x|+|z| <= threshold) emit the threshold as a stable
// default; otherwise atan2(x, z) scaled to the packed-angle unit. The vertical
// component (y) is copied straight through.
// ---------------------------------------------------------------------------
void spatial_vec_to_angle(float *pOut, float *pVec)
{
    if ((float)(ABS((double)pVec[0]) + ABS((double)pVec[2])) <= FLOAT_80616818) {
        pOut[0] = FLOAT_80616818;
    } else {
        double ang = math_atan2((double)pVec[0], (double)pVec[2]);
        pOut[0] = FLOAT_8061681c * (float)ang;
    }
    pOut[1] = pVec[1];
}

// ---------------------------------------------------------------------------
// Address: 0x8018bb04 | Name: spatial_find_nearest_point | Date: 2026-09-06
// Scan a parallel index/group table (param_5 group ids, param_6 u16 point
// indices) for entries whose group matches param_3, and return the point index
// nearest to param_4 (squared distance). bestDistSq is seeded by param_2; the
// winning index is written to *pOutIndex only if one was found.
// ---------------------------------------------------------------------------
double spatial_find_nearest_point(unsigned long long param_1, double bestDistSq, int group,
                                  float *pPos, int pGroupTable, int pIndexTable,
                                  int pPointArray, int stride, int count,
                                  unsigned int *pOutIndex)
{
    int groupOff = stride << 2;
    unsigned int best = 0xffffffff;
    int idxOff = stride << 1;

    if (count > 0) {
        do {
            if (group == *(int *)(pGroupTable + groupOff)) {
                unsigned short pt = *(unsigned short *)(pIndexTable + idxOff);
                float *p = (float *)(pPointArray + (unsigned int)pt * 0xc);
                double distSq = (double)((pPos[2] - p[2]) * (pPos[2] - p[2]) +
                                         (pPos[0] - p[0]) * (pPos[0] - p[0]) +
                                         (pPos[1] - p[1]) * (pPos[1] - p[1]));
                if (distSq < bestDistSq) {
                    best = (unsigned int)pt;
                    bestDistSq = distSq;
                }
            }
            idxOff += 2;
            groupOff += 4;
            count--;
        } while (count != 0);
    }

    if (best != 0xffffffff) {
        *pOutIndex = best;
    }
    return bestDistSq;
}

// ---------------------------------------------------------------------------
// Address: 0x8018bbac | Name: spatial_alloc_sort_buffers | Date: 2026-09-06
// Initialise a sort-context header (pCtx) and carve two index buffers out of a
// caller-supplied allocator callback (param_7). The context stores the point
// count, its reciprocal (FLOAT_80616820 / count), bucket count, and the base
// pointers of the per-point and per-bucket regions; the freshly allocated
// index buffer is zero-filled.
// ---------------------------------------------------------------------------
void spatial_alloc_sort_buffers(unsigned int param_1, unsigned int param_2, int param_3,
                                float param_4, float bucketCount, float param_6,
                                void *param_7)
{
    unsigned long long frame = __save_gpr_25();
    int *pCtx = (int *)(frame >> 0x20);
    float *pHdr = (float *)frame;

    spatial_bucket_offset((int)bucketCount, param_3);

    unsigned int bucketBytes = (unsigned int)((int)bucketCount * 4);
    pHdr[0] = bucketCount;
    pHdr[1] = (float)((double)FLOAT_80616820 / (double)bucketCount);
    pHdr[2] = bucketCount;
    pHdr[3] = param_4;
    pHdr[6] = param_6;
    pHdr[4] = (float)((int)pHdr[6] + param_3 * 4);
    pHdr[5] = (float)((int)pHdr[4] + bucketBytes);

    // Allocate the combined point+bucket index array through the callback.
    int alloc = ((int (*)(int))param_7)(((int)bucketCount + param_3) * 4);
    pCtx[0] = alloc;
    pCtx[1] = pCtx[0] + bucketBytes;
    memset_wrapper(pCtx[0], 0, bucketBytes);

    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// Address: 0x8018bc84 | Name: spatial_bucket_offset | Date: 2026-09-06
// Small layout helper: byte offset of a record given a point count and group
// count (count*4 for the u32 index stride + group*6 for the paired short/entry).
// ---------------------------------------------------------------------------
static int spatial_bucket_offset(int count, int group)
{
    return count * 4 + group * 6;
}

// ---------------------------------------------------------------------------
// Address: 0x8018bc9c | Name: spatial_list_walk | Date: 2026-09-06
// Walk an intrusive singly-linked list to its terminator. Retained as a
// standalone routine for call-site parity (the loop body is empty in the ROM;
// it acts as a chain-length / prefetch barrier).
// ---------------------------------------------------------------------------
static void spatial_list_walk(unsigned int *pNode)
{
    while (pNode != (unsigned int *)0) {
        pNode = (unsigned int *)*pNode;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x8018bcb4 | Name: spatial_build_angle_buckets | Date: 2026-09-06
// Build the angle-sorted bucket index for a point cloud. For each point,
// compute its bucket key (spatial_vec_to_angle + spatial_pack_angle_key) and
// push it onto the head of the matching bucket's intrusive list. Then flatten
// each bucket into the output arrays: per-bucket start offset and count (ctx
// +0x10), the per-entry source index (ctx +0x14) and packed key (ctx +0x18).
// Finally invoke the caller's finalize callback (param_7).
// pCtx layout: +0x04 scale, +0x0c point array, +0x10 bucket hdr, +0x14 idx,
// +0x18 keys. Reached from render_mesh_transform_normals.
// ---------------------------------------------------------------------------
void spatial_build_angle_buckets(unsigned int param_1, unsigned int param_2, int count,
                                 float pointArray, float bucketCount,
                                 void *param_6, void *pFinalize)
{
    float local_out[2];   // spatial_vec_to_angle scratch (angle, elevation-copy)
    int ctxHdr[2];        // {bucketHead base, entry base} from alloc

    unsigned long long frame = FUN_803826c0();
    int pCtx = (int)(frame >> 0x20);
    float bucketCountF = (float)frame;

    spatial_alloc_sort_buffers((unsigned int)&ctxHdr[0], (unsigned int)pCtx, count,
                               pointArray, bucketCountF, bucketCount, param_6);
    int headBase = ctxHdr[0];
    int entryBase = ctxHdr[1];
    unsigned int nBuckets = (unsigned int)bucketCountF;

    // Bin every point onto its bucket's list head.
    int entryOff = 0;
    int ptOff = 0;
    for (int i = 0; i < count; i++) {
        spatial_vec_to_angle(&local_out[0], (float *)((int)pointArray + ptOff));
        unsigned int key = spatial_pack_angle_key((double)*(float *)(pCtx + 4),
                                                  (double)local_out[0], (double)local_out[1]);
        unsigned int *pNode = (unsigned int *)(entryBase + entryOff);
        entryOff += 4;
        ptOff += 0xc;
        int slot = (int)(key % nBuckets) * 4;
        *pNode = *(unsigned int *)(headBase + slot);
        *(unsigned int **)(headBase + slot) = pNode;
    }

    // Flatten each bucket list into the packed output arrays.
    short runningTotal = 0;
    int keyOff = 0;
    int idxOff = 0;
    int hdrOff = 0;
    for (unsigned int b = 0; b < nBuckets; b++) {
        unsigned int *pNode = *(unsigned int **)(headBase + hdrOff);
        short bucketCnt = 0;
        spatial_list_walk(pNode);
        *(short *)(*(int *)(pCtx + 0x10) + hdrOff + 2) = runningTotal;
        for (; pNode != (unsigned int *)0; pNode = (unsigned int *)*pNode) {
            unsigned int rel = (unsigned int)((int)pNode - entryBase);
            double scale = (double)*(float *)(pCtx + 4);
            int srcIdx = (int)rel >> 2;
            spatial_vec_to_angle(&local_out[0], (float *)((int)pointArray + srcIdx * 0xc));
            unsigned int key = spatial_pack_angle_key(scale, (double)local_out[0], (double)local_out[1]);
            bucketCnt++;
            runningTotal++;
            *(unsigned int *)(*(int *)(pCtx + 0x18) + keyOff) = key;
            keyOff += 4;
            *(short *)(*(int *)(pCtx + 0x14) + idxOff) = (short)srcIdx;
            idxOff += 2;
        }
        *(short *)(*(int *)(pCtx + 0x10) + hdrOff) = bucketCnt;
        hdrOff += 4;
    }

    ((void (*)(int))pFinalize)(ctxHdr[0]);
    FUN_8038270c();
}

// ---------------------------------------------------------------------------
// Address: 0x8018bea0 | Name: spatial_query_nearest | Date: 2026-09-06
// Nearest-point query against a built bucket index. Start in the query point's
// own bucket, then expand outward in growing square rings of bucket cells until
// a candidate is found, calling spatial_find_nearest_point on each non-empty
// bucket to keep the best (smallest squared distance) match in local_best.
// pCtx layout: +0x04 scale, +0x08 bucket modulus, +0x0c point array,
// +0x10 bucket hdr, +0x14 idx array, +0x18 key array.
// ---------------------------------------------------------------------------
void spatial_query_nearest(void)
{
    float angle[2];        // spatial_vec_to_angle scratch
    unsigned int local_best = 0xffffffff;

    unsigned long long frame = FUN_803826d8();
    int pCtx = (int)(frame >> 0x20);
    float *pVec = (float *)(int)frame;
    double scale = (double)*(float *)(pCtx + 4);
    double bestDistSq = (double)FLOAT_80616824;
    unsigned int modulus = *(unsigned int *)(pCtx + 8);
    int hdrBase = *(int *)(pCtx + 0x10);
    int keyBase = *(int *)(pCtx + 0x18);
    int idxBase = *(int *)(pCtx + 0x14);
    int ptArray = *(int *)(pCtx + 0xc);

    spatial_vec_to_angle(&angle[0], pVec);
    unsigned int key = spatial_pack_angle_key(scale, (double)angle[0], (double)angle[1]);
    int slot = (int)(key % modulus) * 4;
    unsigned int cnt = *(unsigned short *)(hdrBase + slot);

    // Center bucket first.
    if (cnt != 0) {
        bestDistSq = spatial_find_nearest_point(scale, (double)FLOAT_80616824, key, pVec,
                                                keyBase, idxBase, ptArray,
                                                *(unsigned short *)(hdrBase + slot + 2), cnt,
                                                &local_best);
    }

    // Expanding square rings around the center cell.
    int cx = (int)(short)(int)((double)angle[1] * scale); // hi component (elevation)
    int cy = (int)(short)(int)((double)angle[0] * scale); // lo component (angle)
    int ring = 1;
    while (local_best == 0xffffffff) {
        short r = (short)ring;
        int negR = -(int)r;

        // Top & bottom edges (hi = +/-r, lo scans -r..r).
        for (int lo = negR; (short)lo <= ring; lo++) {
            unsigned int k = (unsigned int)((cy + (short)lo) & 0xffff) | (unsigned int)((cx + negR) << 0x10);
            unsigned short *pBucket = (unsigned short *)(hdrBase + (int)(k % modulus) * 4);
            if (*pBucket != 0) {
                bestDistSq = spatial_find_nearest_point(scale, bestDistSq, k, pVec, keyBase,
                                                        idxBase, ptArray, pBucket[1], *pBucket,
                                                        &local_best);
            }
        }
        for (short lo = negR; lo <= ring; lo++) {
            unsigned int k = (unsigned int)((cy + lo) & 0xffff) | (unsigned int)((cx + (int)r) << 0x10);
            unsigned short *pBucket = (unsigned short *)(hdrBase + (int)(k % modulus) * 4);
            if (*pBucket != 0) {
                bestDistSq = spatial_find_nearest_point(scale, bestDistSq, k, pVec, keyBase,
                                                        idxBase, ptArray, pBucket[1], *pBucket,
                                                        &local_best);
            }
        }
        // Left & right edges (lo = +/-r, hi scans (1-r)..(r-1)).
        short hi = (short)(1 - (int)r);
        for (short h = hi; h < ring; h++) {
            unsigned int k = (unsigned int)((cy + negR) & 0xffff) | (unsigned int)((cx + h) << 0x10);
            unsigned short *pBucket = (unsigned short *)(hdrBase + (int)(k % modulus) * 4);
            if (*pBucket != 0) {
                bestDistSq = spatial_find_nearest_point(scale, bestDistSq, k, pVec, keyBase,
                                                        idxBase, ptArray, pBucket[1], *pBucket,
                                                        &local_best);
            }
        }
        for (; hi < ring; hi++) {
            unsigned int k = (unsigned int)((cy + (int)r) & 0xffff) | (unsigned int)((cx + hi) << 0x10);
            unsigned short *pBucket = (unsigned short *)(hdrBase + (int)(k % modulus) * 4);
            if (*pBucket != 0) {
                bestDistSq = spatial_find_nearest_point(scale, bestDistSq, k, pVec, keyBase,
                                                        idxBase, ptArray, pBucket[1], *pBucket,
                                                        &local_best);
            }
        }
        ring++;
    }

    FUN_80382724();
}
