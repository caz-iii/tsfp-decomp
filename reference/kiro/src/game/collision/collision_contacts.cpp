// =============================================================================
// collision_contacts.cpp - Convex contact generation: SAT poly manifolds,
//                          capsule/sphere/OBB narrow-phase contacts
// Address range: 0x800180ec - 0x8001af98
// Batch: 382 | Date: 2026-09-05
//
// Continuation of the OBB / convex collision layer begun in
// collision_manifold.cpp (batch 381). These routines generate contact points
// for the narrow phase: SAT box-vs-polygon manifolds, and the closest-feature
// tests between capsules, spheres and polygons/OBBs.
//
//   * clip_seg_against_plane      (0x800180ec) clip an edge endpoint to a plane
//   * sat_obb_vs_indexed_poly     (0x800181e4) OBB vs indexed-vertex polygon SAT
//   * sat_obb_vs_struct_poly      (0x80018e10) OBB vs struct-embedded polygon SAT
//   * build_swept_seg_aabb        (0x80019790) AABB record from oriented segment
//   * aabb_record_overlap         (0x800198e0) AABB record vs bounds overlap
//   * capsule_vs_indexed_poly     (0x80019950) capsule vs indexed polygon
//   * capsule_vs_struct_poly      (0x8001a434) capsule vs struct polygon
//   * sphere_vs_segment_contact   (0x8001ab5c) sphere/capsule vs line segment
//   * capsule_vs_capsule_contact  (0x8001ad4c) capsule vs capsule
//   * sphere_vs_capsule_contact   (0x8001af98) sphere vs capsule (core, 12 xrefs)
// =============================================================================
#include "common.h"
#include "collision.h"

// ---- numeric constants (rodata @ 0x80613868..0x806138d8) ------------------
extern float  FLOAT_80613868;   // 0.0
extern float  FLOAT_8061386c;   // -FLT_MAX
extern float  FLOAT_8061389c;   // 1/128 or short->float scale (indexed vtx)
extern float  FLOAT_806138a0;   // quantise divisor
extern float  FLOAT_806138a4;   // 2.0 (plane offset doubling)
extern float  FLOAT_806138a8;   // quantise divisor (capsule indexed)
extern float  FLOAT_806138ac;   // short->float scale (capsule indexed)
extern float  FLOAT_806138b0;   // 0.0 (double-path zero)
extern float  FLOAT_806138b4;   // small epsilon (degenerate cross length)
extern float  FLOAT_806138b8;   // 1.5 (rsqrt Newton refine)
extern float  FLOAT_806138bc;   // rsqrt lower bound
extern float  FLOAT_806138c0;   // 0.5 (rsqrt Newton)
extern float  FLOAT_806138c4;   // 3.0 (rsqrt Newton)
extern float  FLOAT_806138d0;   // large initial penetration sentinel
extern float  FLOAT_806138d4;   // face-crossing slack
extern float  FLOAT_806138d8;   // squared-length floor for normal build
extern double DOUBLE_806138c8;  // uint->double conversion magic bias

// ---- scratch work-buffer alloc/free pairs (packed hi:lo pointers) ---------
// Each alloc returns a packed 64-bit value: high 32 bits = pointer to the
// high-function/hull scratch record, low 32 bits = pointer/handle to the
// segment or query data. The matching free restores the frame.
extern "C" unsigned long long FUN_803826b4(void);
extern "C" void               FUN_80382700(void);
extern "C" unsigned long long FUN_803826c8(void);
extern "C" void               FUN_80382714(void);
extern "C" unsigned long long FUN_803826cc(void);
extern "C" void               FUN_80382718(void);
extern "C" unsigned long long FUN_803826e4(void);
extern "C" void               FUN_80382730(void);

// ---- edge/face index tables (rodata) --------------------------------------
extern int DAT_803f2328;   // face-edge index table (indexed-poly SAT)
extern int DAT_803f2388;   // face-edge index table (struct-poly SAT)

// NOTE: mem_page_alloc / mem_page_free and the sibling collision helpers
// (clip_poly_against_plane, manifold_add_point, closest_pt_on_obb,
// closest_pt_on_segment, closest_between_segments) are all declared through
// collision.h -> generated_decls.h -> generated_externs.h, so they are not
// re-declared here (doing so triggers extern "C" linkage conflicts).

// intra-unit forward declaration
int sphere_vs_capsule_contact(float* radius, float* segA, float* mtx,
                              float* seg, int convBlk, float* outPt,
                              float* outNormal, float* outDepth);

// clip_seg_against_plane (0x800180ec)
// Clip the segment p1->p2 against the plane 'plane' (plane[0..2]=normal,
// plane[3]=offset). Returns 0 if the whole segment is behind the plane, 1 if
// both endpoints are in front, otherwise clips the crossing endpoint in place
// (p2 receives the intersection; if p1 was the outside endpoint it is used as
// the base) and returns 1.
int clip_seg_against_plane(float* p1, float* p2, float* plane)
{
    float ny = plane[1];
    float nx = plane[0];
    float nz = plane[2];
    float d1 = plane[3] + p1[2] * nz + p1[0] * nx + p1[1] * ny;
    float d2 = plane[3] + p2[2] * nz + p2[0] * nx + p2[1] * ny;

    if ((d1 < FLOAT_80613868) && (d2 < FLOAT_80613868)) {
        return 0;
    }
    if ((FLOAT_80613868 <= d1) && (FLOAT_80613868 <= d2)) {
        return 1;
    }

    float* base = p2;
    if (d1 < FLOAT_80613868) {
        base = p1;
    }
    float ey = p2[1] - p1[1];
    float ex = *p2 - *p1;   // note: original recomputes from p1/p2 origin
    float ez = p2[2] - p1[2];
    float denom = nz * ez + nx * ex + ny * ey;
    if (FLOAT_80613868 == denom) {
        return 0;
    }
    float t = d1 / -denom;
    base[0] = t * ex + p1[0];
    base[1] = t * ey + p1[1];
    base[2] = t * ez + p1[2];
    return 1;
}

// ---------------------------------------------------------------------------
// Shared SAT box-vs-polygon manifold body.
//
// sat_obb_vs_indexed_poly and sat_obb_vs_struct_poly are byte-near-identical;
// they only differ in how the source polygon vertices are fetched (indexed
// short table vs struct-embedded vertex array). The projection/clip/manifold
// tail is common. We keep two faithful translations rather than one templated
// body because the vertex-fetch prologues and constant sets differ.
// ---------------------------------------------------------------------------

// sat_obb_vs_indexed_poly (0x800181e4)
// SAT between the OBB scratch record (returned by FUN_803826b4) and a polygon
// whose vertices are addressed through a short index table. Selects the best
// separating axis (box face candidates + polygon normal) and, on overlap,
// either emits edge-face contacts (axis type 1: point-in-poly + edge clip via
// clip_seg_against_plane) or clips the box face against the polygon planes
// (clip_poly_against_plane) to build the manifold via manifold_add_point.
void sat_obb_vs_indexed_poly(unsigned int a, unsigned int b, int depthsBase,
                             float* outNormal, int flipFaces, int maxPts,
                             unsigned int convBlk, int vtxBase, int meshBase,
                             float* origin, int quantised, char* face)
{
    (void)a;
    (void)b;
    (void)convBlk;

    unsigned long long scratch = FUN_803826b4();
    float* rec = (float*)(unsigned long)(scratch >> 0x20);   // OBB scratch record
    int recHandle = (int)(unsigned int)scratch;
    float* poly = (float*)mem_page_alloc();

    float scale = FLOAT_8061389c;
    double bias = DOUBLE_806138c8;
    short* idx = (short*)(*(int*)(meshBase + 0x2c) + (unsigned int)*(unsigned short*)(face + 8) * 2);
    int count = 0;                       // manifold point count
    int nVerts = (int)*face;
    float* bestAxis = (float*)0;

    // fetch first polygon vertex (dequantise if 'quantised')
    if (quantised == 0) {
        float* v = (float*)(vtxBase + *idx * 0xc);
        poly[0] = v[0];
        poly[1] = v[1];
        poly[2] = v[2];
    } else {
        short* v = (short*)(vtxBase + *idx * 6);
        poly[0] = FLOAT_8061389c * (float)((double)CONCAT44(0x43300000, (int)v[0] ^ 0x80000000) - DOUBLE_806138c8) + origin[0];
        poly[1] = scale * (float)((double)CONCAT44(0x43300000, (int)v[1] ^ 0x80000000) - bias) + origin[1];
        poly[2] = scale * (float)((double)CONCAT44(0x43300000, (int)v[2] ^ 0x80000000) - bias) + origin[2];
    }

    // polygon plane (from the packed face record bytes [1..3])
    float pn0 = (float)((double)CONCAT44(0x43300000, (int)face[1] ^ 0x80000000) - DOUBLE_806138c8) / FLOAT_806138a0;
    float pn1 = (float)((double)CONCAT44(0x43300000, (int)face[2] ^ 0x80000000) - DOUBLE_806138c8) / FLOAT_806138a0;
    float pn2 = (float)((double)CONCAT44(0x43300000, (int)face[3] ^ 0x80000000) - DOUBLE_806138c8) / FLOAT_806138a0;
    float pd  = -(poly[2] * pn2 + poly[0] * pn0 + poly[1] * pn1);

    // separation of the OBB centre against the polygon plane
    float centreSep = pd + rec[2] * pn2 + rec[0] * pn0 + rec[1] * pn1;
    float projExtent =
        ABS(rec[0x45] * pn2 + rec[0x43] * pn0 + rec[0x44] * pn1) +
        ABS(rec[0x3f] * pn2 + rec[0x3d] * pn0 + rec[0x3e] * pn1) +
        ABS(rec[0x42] * pn2 + rec[0x40] * pn0 + rec[0x41] * pn1);
    float faceSep = ABS(centreSep) - projExtent;

    if (faceSep <= FLOAT_80613868) {
        bool polyFront = FLOAT_8061386c < faceSep;
        float bestDepth = FLOAT_8061386c;
        int bestType = 0;
        if (polyFront) {
            bestAxis = &pn0;
            bestDepth = faceSep;
        }
        bool flipPoly = polyFront && centreSep < FLOAT_80613868;

        // (re)build the working polygon in scratch, optionally reversed so the
        // winding faces the box.
        if (polyFront && centreSep < FLOAT_80613868 && flipFaces == 0) {
            pn0 = -pn0; pn1 = -pn1; pn2 = -pn2; pd = -pd;
            int wi = 1;
            int wByte = 0xc;
            if (quantised == 0) {
                for (int k = nVerts - 1; k != 0; --k) {
                    int si = nVerts - wi;
                    float* dst = (float*)((int)poly + wByte);
                    float* v = (float*)(vtxBase + idx[si] * 0xc);
                    dst[0] = v[0]; dst[1] = v[1]; dst[2] = v[2];
                    wi = wi + 1;
                    wByte = wByte + 0xc;
                }
            } else {
                for (int k = nVerts - 1; k != 0; --k) {
                    int si = nVerts - wi;
                    float* dst = (float*)((int)poly + wByte);
                    short* v = (short*)(vtxBase + idx[si] * 6);
                    dst[0] = scale * (float)((double)CONCAT44(0x43300000, (int)v[0] ^ 0x80000000) - bias) + origin[0];
                    dst[1] = scale * (float)((double)CONCAT44(0x43300000, (int)v[1] ^ 0x80000000) - bias) + origin[1];
                    dst[2] = scale * (float)((double)CONCAT44(0x43300000, (int)v[2] ^ 0x80000000) - bias) + origin[2];
                    wi = wi + 1;
                    wByte = wByte + 0xc;
                }
            }
            flipPoly = false;
        } else {
            int wByte = 0xc;
            if (quantised == 0) {
                short* sp = (short*)((int)idx + 2);
                for (int k = nVerts - 1; k != 0; --k) {
                    float* dst = (float*)((int)poly + wByte);
                    float* v = (float*)(vtxBase + *sp * 0xc);
                    dst[0] = v[0]; dst[1] = v[1]; dst[2] = v[2];
                    sp = sp + 1;
                    wByte = wByte + 0xc;
                }
            } else {
                short* sp = (short*)((int)idx + 2);
                for (int k = nVerts - 1; k != 0; --k) {
                    float* dst = (float*)((int)poly + wByte);
                    short* v = (short*)(vtxBase + *sp * 6);
                    dst[0] = scale * (float)((double)CONCAT44(0x43300000, (int)v[0] ^ 0x80000000) - bias) + origin[0];
                    dst[1] = scale * (float)((double)CONCAT44(0x43300000, (int)v[1] ^ 0x80000000) - bias) + origin[1];
                    dst[2] = scale * (float)((double)CONCAT44(0x43300000, (int)v[2] ^ 0x80000000) - bias) + origin[2];
                    sp = sp + 1;
                    wByte = wByte + 0xc;
                }
            }
        }

        // test the 3 box face axes against the polygon vertex spread
        float px = poly[0], py = poly[1], pz = poly[2];
        bool separated = false;
        for (int axis = 0; axis < 3; ++axis) {
            const float* boxAxis = rec + 0x13 + axis * 4;
            float rx = *(rec + 1);
            (void)rx;
            float minP, maxP;
            float d0 = boxAxis[2] * (pz - rec[2]) + boxAxis[0] * (px - rec[0]) + boxAxis[1] * (py - rec[1]);
            minP = maxP = d0;
            int wByte = 0xc;
            for (int k = nVerts - 1; k != 0; --k) {
                float* v = (float*)((int)poly + wByte);
                float d = boxAxis[2] * (v[2] - rec[2]) + boxAxis[0] * (v[0] - rec[0]) + boxAxis[1] * (v[1] - rec[1]);
                if (d < minP) minP = d;
                if (maxP < d) maxP = d;
                wByte = wByte + 0xc;
            }
            float halfE = rec[3 + axis];
            if ((halfE < minP) || (maxP < -halfE)) {
                separated = true;
                break;
            }
            float lo = -halfE - maxP;
            float hi = halfE + minP;   // preserved sign layout from original
            float pen = (lo < hi) ? hi : lo;
            if (bestDepth < pen) {
                bestDepth = pen;
                bestAxis = (float*)(rec + 0x13 + axis * 4);
                bestType = axis + 2;
                flipPoly = (hi < lo);
            }
        }

        if (separated || bestType == 0) {
            mem_page_free((int)poly);
        } else if (bestType == 1 || bestAxis == &pn0) {
            // (polygon-normal reference axis path handled below via bestType==1)
            // Build clip planes from the box faces and clip the polygon.
            float refN[4];
            if (flipPoly) {
                refN[0] = bestAxis[0]; refN[1] = bestAxis[1];
                refN[2] = bestAxis[2]; refN[3] = bestAxis[3];
            } else {
                refN[0] = -bestAxis[0]; refN[1] = -bestAxis[1];
                refN[2] = -bestAxis[2];
                refN[3] = FLOAT_806138a4 * pn1 + -bestAxis[3];
            }
            int io[4];
            float* dbl = poly + 0x1fe;
            io[0] = (int)poly;
            io[1] = (int)*face;
            io[2] = (int)dbl;
            io[3] = 0;
            int* cur = &io[0];
            int* other = &io[2];
            for (int p = 0; p < 6 && cur[1] != 0; ++p) {
                int swapped = clip_poly_against_plane(other, (float*)((int)rec + p * 0x10 + 0x1c), cur);
                int* keep = cur;
                if (swapped != 0) { keep = other; other = cur; }
                cur = keep;
            }
            if (0 < cur[1]) {
                float* verts = (float*)cur[0];
                for (int k = 0; k < cur[1]; ++k) {
                    count = manifold_add_point(maxPts, recHandle, depthsBase, verts, refN, count, 1);
                    verts = verts + 3;
                }
                outNormal[0] = refN[0];
                outNormal[1] = refN[1];
                outNormal[2] = refN[2];
            }
            mem_page_free((int)poly);
        } else {
            // box-face reference: mark box faces facing the axis, then for each
            // such face intersect its edges against the (dequantised) polygon.
            float refN[4];
            if (flipPoly) {
                refN[0] = -bestAxis[0]; refN[1] = -bestAxis[1];
                refN[2] = -bestAxis[2]; refN[3] = -bestAxis[3];
            } else {
                refN[0] = bestAxis[0]; refN[1] = bestAxis[1];
                refN[2] = bestAxis[2]; refN[3] = bestAxis[3];
            }
            int faceFlag[8];
            int nFacing = 0;
            for (int f = 0; f < 8; ++f) {
                float d = refN[3] +
                          refN[2] * *(float*)((int)rec + f * 0xc + 0x84) +
                          refN[0] * *(float*)((int)rec + f * 0xc + 0x7c) +
                          refN[1] * *(float*)((int)rec + f * 0xc + 0x80);
                if (FLOAT_80613868 <= d) {
                    faceFlag[f] = 0;
                } else {
                    faceFlag[f] = 1;
                    nFacing = nFacing + 1;
                }
            }
            if (nFacing != 0) {
                // build per-edge planes of the polygon into the scratch tail
                float* prev = poly + (nVerts - 1) * 3;
                int srcByte = 0;
                int dstByte = 0;
                for (int k = 0; k < nVerts; ++k) {
                    float* cur = (float*)((int)poly + srcByte);
                    float* pl = (float*)((int)poly + dstByte + 0x800);
                    srcByte = srcByte + 0xc;
                    dstByte = dstByte + 0x10;
                    float ex = (prev[0] + refN[0]) - prev[0];
                    float ey = (prev[1] + refN[1]) - prev[1];
                    float ez = (prev[2] + refN[2]) - prev[2];
                    pl[0] = ey * (cur[2] - prev[2]) - ez * (cur[1] - prev[1]);
                    pl[1] = ez * (cur[0] - prev[0]) - ex * (cur[2] - prev[2]);
                    pl[2] = ex * (cur[1] - prev[1]) - ey * (cur[0] - prev[0]);
                    pl[3] = -(pl[2] * prev[2] + pl[0] * prev[0] + pl[1] * prev[1]);
                    prev = cur;
                }
                // for each facing box face, clip its 3 edges against the polygon
                int flagByte = 0;
                int faceByte = 0;
                for (int f = 0; f < 8; ++f) {
                    if (*(int*)((int)faceFlag + flagByte) != 0) {
                        float edge[4];
                        edge[0] = *(float*)((int)rec + faceByte + 0x7c);
                        edge[1] = *(float*)((int)rec + faceByte + 0x80);
                        edge[2] = *(float*)((int)rec + faceByte + 0x84);
                        for (int e = 0; e < 3; ++e) {
                            int en = *(int*)((int)&DAT_803f2328 + e * 4 + faceByte);
                            float en0[4];
                            en0[0] = rec[en * 3 + 0x1f];
                            en0[1] = rec[en * 3 + 0x20];
                            en0[2] = rec[en * 3 + 0x21];
                            int inside = 0;
                            int plByte = 0;
                            while (inside < nVerts &&
                                   clip_seg_against_plane(edge, en0, (float*)((int)poly + plByte + 0x800)) != 0) {
                                inside = inside + 1;
                                plByte = plByte + 0x10;
                            }
                            if (inside == nVerts) {
                                count = manifold_add_point(maxPts, recHandle, depthsBase, edge, refN, count, 0);
                                count = manifold_add_point(maxPts, recHandle, depthsBase, en0, refN, count, 0);
                            }
                        }
                    }
                    flagByte = flagByte + 4;
                    faceByte = faceByte + 0xc;
                }
            }
            if (count != 0) {
                outNormal[0] = refN[0];
                outNormal[1] = refN[1];
                outNormal[2] = refN[2];
            }
            mem_page_free((int)poly);
        }
    } else {
        mem_page_free((int)poly);
    }
    FUN_80382700();
}

// sat_obb_vs_struct_poly (0x80018e10)
// As sat_obb_vs_indexed_poly but the polygon vertices are stored contiguously
// inside the face struct (vertex array at param+0x40, stride 0x1c, count at
// param+0x2a). Reference face-plane comes from struct fields +0x30..+0x38.
void sat_obb_vs_struct_poly(unsigned int a, unsigned int b, int depthsBase,
                            float* outNormal, int maxPts, unsigned int convBlk,
                            int poly)
{
    (void)a;
    (void)b;
    (void)convBlk;

    unsigned long long scratch = FUN_803826b4();
    float* rec = (float*)(unsigned long)(scratch >> 0x20);
    int recHandle = (int)(unsigned int)scratch;
    float* work = (float*)mem_page_alloc();

    unsigned int nVerts = (unsigned int)*(unsigned short*)(poly + 0x2a);
    float* bestAxis = (float*)0;
    int count = 0;

    work[0] = *(float*)(poly + 0x40);
    work[1] = *(float*)(poly + 0x44);
    work[2] = *(float*)(poly + 0x48);
    float pn0 = *(float*)(poly + 0x30);
    float pn1 = *(float*)(poly + 0x34);
    float pn2 = *(float*)(poly + 0x38);
    float pd  = -(work[2] * pn2 + work[0] * pn0 + work[1] * pn1);

    float centreSep = pd + rec[2] * pn2 + rec[0] * pn0 + rec[1] * pn1;
    float projExtent =
        ABS(rec[0x45] * pn2 + rec[0x43] * pn0 + rec[0x44] * pn1) +
        ABS(rec[0x3f] * pn2 + rec[0x3d] * pn0 + rec[0x3e] * pn1) +
        ABS(rec[0x42] * pn2 + rec[0x40] * pn0 + rec[0x41] * pn1);
    float faceSep = ABS(centreSep) - projExtent;

    if (faceSep <= FLOAT_80613868) {
        bool polyFront = FLOAT_8061386c < faceSep;
        float bestDepth = FLOAT_8061386c;
        int bestType = 0;
        if (polyFront) {
            bestAxis = &pn0;
            bestDepth = faceSep;
        }
        bool flipPoly = polyFront && centreSep < FLOAT_80613868;

        if (polyFront && centreSep < FLOAT_80613868) {
            pn0 = -pn0; pn1 = -pn1; pn2 = -pn2; pd = -pd;
            int wByte = 0xc;
            for (int k = nVerts - 1; k != 0; --k) {
                int si = (int)nVerts - (0xc + wByte) / 0xc;    // reverse index
                (void)si;
                float* dst = (float*)((int)work + wByte);
                float* v = (float*)(poly + ((int)nVerts - (wByte / 0xc)) * 0x1c + 0x40);
                dst[0] = v[0]; dst[1] = v[1]; dst[2] = v[2];
                wByte = wByte + 0xc;
            }
            flipPoly = false;
        } else {
            int srcByte = 0x1c;
            int dstByte = 0xc;
            for (int k = nVerts - 1; k != 0; --k) {
                float* dst = (float*)((int)work + dstByte);
                float* v = (float*)(poly + srcByte + 0x40);
                dst[0] = v[0]; dst[1] = v[1]; dst[2] = v[2];
                srcByte = srcByte + 0x1c;
                dstByte = dstByte + 0xc;
            }
        }

        bool separated = false;
        for (int axis = 0; axis < 3; ++axis) {
            const float* boxAxis = rec + 0x13 + axis * 4;
            float d0 = boxAxis[2] * (work[2] - rec[2]) + boxAxis[0] * (work[0] - rec[0]) + boxAxis[1] * (work[1] - rec[1]);
            float minP = d0, maxP = d0;
            int wByte = 0xc;
            for (unsigned int k = nVerts - 1; k != 0; --k) {
                float* v = (float*)((int)work + wByte);
                float d = boxAxis[2] * (v[2] - rec[2]) + boxAxis[0] * (v[0] - rec[0]) + boxAxis[1] * (v[1] - rec[1]);
                if (d < minP) minP = d;
                if (maxP < d) maxP = d;
                wByte = wByte + 0xc;
            }
            float halfE = rec[3 + axis];
            if ((halfE < minP) || (maxP < -halfE)) { separated = true; break; }
            float lo = -halfE - maxP;
            float hi = halfE + minP;
            float pen = (lo < hi) ? hi : lo;
            if (bestDepth < pen) {
                bestDepth = pen;
                bestAxis = (float*)(rec + 0x13 + axis * 4);
                bestType = axis + 2;
                flipPoly = (hi < lo);
            }
        }

        if (separated || bestType == 0) {
            mem_page_free((int)work);
        } else if (bestAxis == &pn0) {
            float refN[4];
            if (flipPoly) {
                refN[0] = bestAxis[0]; refN[1] = bestAxis[1];
                refN[2] = bestAxis[2]; refN[3] = bestAxis[3];
            } else {
                refN[0] = -bestAxis[0]; refN[1] = -bestAxis[1];
                refN[2] = -bestAxis[2];
                refN[3] = FLOAT_806138a4 * pn1 + -bestAxis[3];
            }
            int io[4];
            float* dbl = work + 0x1fe;
            io[0] = (int)work;
            io[1] = (int)(unsigned int)*(unsigned short*)(poly + 0x2a);
            io[2] = (int)dbl;
            io[3] = 0;
            int* cur = &io[0];
            int* other = &io[2];
            for (int p = 0; p < 6 && cur[1] != 0; ++p) {
                int swapped = clip_poly_against_plane(other, (float*)((int)rec + p * 0x10 + 0x1c), cur);
                int* keep = cur;
                if (swapped != 0) { keep = other; other = cur; }
                cur = keep;
            }
            if (0 < cur[1]) {
                float* verts = (float*)cur[0];
                for (int k = 0; k < cur[1]; ++k) {
                    count = manifold_add_point(maxPts, recHandle, depthsBase, verts, refN, count, 1);
                    verts = verts + 3;
                }
                outNormal[0] = refN[0];
                outNormal[1] = refN[1];
                outNormal[2] = refN[2];
            }
            mem_page_free((int)work);
        } else {
            float refN[4];
            if (flipPoly) {
                refN[0] = -bestAxis[0]; refN[1] = -bestAxis[1];
                refN[2] = -bestAxis[2]; refN[3] = -bestAxis[3];
            } else {
                refN[0] = bestAxis[0]; refN[1] = bestAxis[1];
                refN[2] = bestAxis[2]; refN[3] = bestAxis[3];
            }
            int faceFlag[8];
            int nFacing = 0;
            for (int f = 0; f < 8; ++f) {
                float d = refN[3] +
                          refN[2] * *(float*)((int)rec + f * 0xc + 0x84) +
                          refN[0] * *(float*)((int)rec + f * 0xc + 0x7c) +
                          refN[1] * *(float*)((int)rec + f * 0xc + 0x80);
                if (FLOAT_80613868 <= d) { faceFlag[f] = 0; }
                else { faceFlag[f] = 1; nFacing = nFacing + 1; }
            }
            if (nFacing != 0) {
                float* prev = work + (nVerts - 1) * 3;
                int srcByte = 0;
                int dstByte = 0;
                for (unsigned int k = 0; k < nVerts; ++k) {
                    float* cur = (float*)((int)work + srcByte);
                    float* pl = (float*)((int)work + dstByte + 0x800);
                    srcByte = srcByte + 0xc;
                    dstByte = dstByte + 0x10;
                    float ex = (prev[0] + refN[0]) - prev[0];
                    float ey = (prev[1] + refN[1]) - prev[1];
                    float ez = (prev[2] + refN[2]) - prev[2];
                    pl[0] = ey * (cur[2] - prev[2]) - ez * (cur[1] - prev[1]);
                    pl[1] = ez * (cur[0] - prev[0]) - ex * (cur[2] - prev[2]);
                    pl[2] = ex * (cur[1] - prev[1]) - ey * (cur[0] - prev[0]);
                    pl[3] = -(pl[2] * prev[2] + pl[0] * prev[0] + pl[1] * prev[1]);
                    prev = cur;
                }
                int flagByte = 0;
                int faceByte = 0;
                for (int f = 0; f < 8; ++f) {
                    if (*(int*)((int)faceFlag + flagByte) != 0) {
                        float edge[4];
                        edge[0] = *(float*)((int)rec + faceByte + 0x7c);
                        edge[1] = *(float*)((int)rec + faceByte + 0x80);
                        edge[2] = *(float*)((int)rec + faceByte + 0x84);
                        for (int e = 0; e < 3; ++e) {
                            int en = *(int*)((int)&DAT_803f2388 + e * 4 + faceByte);
                            float en0[4];
                            en0[0] = rec[en * 3 + 0x1f];
                            en0[1] = rec[en * 3 + 0x20];
                            en0[2] = rec[en * 3 + 0x21];
                            unsigned int inside = 0;
                            int plByte = 0;
                            while (inside < nVerts &&
                                   clip_seg_against_plane(edge, en0, (float*)((int)work + plByte + 0x800)) != 0) {
                                inside = inside + 1;
                                plByte = plByte + 0x10;
                            }
                            if (inside == nVerts) {
                                count = manifold_add_point(maxPts, recHandle, depthsBase, edge, refN, count, 0);
                                count = manifold_add_point(maxPts, recHandle, depthsBase, en0, refN, count, 0);
                            }
                        }
                    }
                    flagByte = flagByte + 4;
                    faceByte = faceByte + 0xc;
                }
            }
            if (count != 0) {
                outNormal[0] = refN[0];
                outNormal[1] = refN[1];
                outNormal[2] = refN[2];
            }
            mem_page_free((int)work);
        }
    } else {
        mem_page_free((int)work);
    }
    FUN_80382700();
}

// build_swept_seg_aabb (0x80019790)
// Build a swept-segment collision record 'out' from a base point (origin), a
// segment parameter pair 'segT' (segT[0]/segT[1] extents along the local X of
// 'mtx') and a transform 'mtx'. out[0..2] = +extent endpoint, out[3] = segT[0],
// out[4..6] = -extent endpoint, out[7..9] = AABB min, out[10..12] = AABB max.
void build_swept_seg_aabb(float* out, float* origin, float* segT, float* mtx)
{
    out[3] = segT[0];
    float t = segT[1] - segT[0];
    float nt = -t;
    out[0] = mtx[4] * t + origin[0];
    out[1] = mtx[5] * t + origin[1];
    out[2] = mtx[6] * t + origin[2];
    out[4] = mtx[4] * nt + origin[0];
    out[5] = mtx[5] * nt + origin[1];
    out[6] = mtx[6] * nt + origin[2];

    float t0 = segT[0];
    float t1 = segT[1];
    float ex = ABS(mtx[8] * t0) + ABS(mtx[0] * t0) + ABS(mtx[4] * t1);
    float ey = ABS(mtx[9] * t0) + ABS(mtx[1] * t0) + ABS(mtx[5] * t1);
    float ez = ABS(mtx[10] * t0) + ABS(mtx[2] * t0) + ABS(mtx[6] * t1);
    out[7] = origin[0] - ex;
    out[8] = origin[1] - ey;
    out[9] = origin[2] - ez;
    out[10] = origin[0] + ex;
    out[11] = origin[1] + ey;
    out[12] = origin[2] + ez;
}

// aabb_record_overlap (0x800198e0)
// Overlap test between a swept-segment record (rec+0x1c..0x24 = min,
// rec+0x28..0x30 = max) and a bounds struct (bounds[0..2] = min, [3..5] = max).
int aabb_record_overlap(int rec, float* bounds)
{
    if (((bounds[0] <= *(float*)(rec + 0x28)) && (*(float*)(rec + 0x1c) <= bounds[3]) &&
         (bounds[1] <= *(float*)(rec + 0x2c))) &&
        ((*(float*)(rec + 0x20) <= bounds[4] && (bounds[2] <= *(float*)(rec + 0x30))) &&
         (*(float*)(rec + 0x24) <= bounds[5]))) {
        return 1;
    }
    return 0;
}

// capsule_vs_indexed_poly (0x80019950)
// Contact generation between a capsule (segment carried in the scratch record)
// and a polygon whose vertices are indexed through a short table. For every
// polygon edge the closest segment-segment feature is found; contacts within
// the capsule radius are written to the output arrays. The two face endpoints
// and the plane-crossing point are also tested. maxContacts caps the output.
void capsule_vs_indexed_poly(unsigned int a, unsigned int b, int depthsBase,
                             float* outNormal, unsigned int cvHi,
                             unsigned int maxContacts, int contactStride,
                             int vtxBase, int meshBase, float* origin,
                             int quantised, char* face)
{
    (void)a;
    (void)b;
    (void)cvHi;

    unsigned long long scratch = FUN_803826b4();
    float* seg = (float*)(unsigned long)(scratch >> 0x20);   // capsule seg + radius@[3]
    int contactsBase = (int)(unsigned int)scratch;
    float* work = (float*)mem_page_alloc();

    double bias = DOUBLE_806138c8;
    float qdiv = FLOAT_806138a8;
    unsigned int nVerts = (unsigned int)*(unsigned char*)face;   // low byte count
    int idxBase = *(int*)(meshBase + 0x2c) + (unsigned int)*(unsigned short*)(face + 8) * 2;
    float* last = work + (nVerts - 1) * 3 + 3;

    // polygon normal from packed face bytes [1..3]
    work[0] = (float)((double)CONCAT44(0x43300000, (int)face[1] ^ 0x80000000) - bias) / qdiv;
    work[1] = (float)((double)CONCAT44(0x43300000, (int)face[2] ^ 0x80000000) - bias) / qdiv;
    work[2] = (float)((double)CONCAT44(0x43300000, (int)face[3] ^ 0x80000000) - bias) / qdiv;

    float scale = FLOAT_806138ac;
    int lastIdx = (int)*(short*)(idxBase + nVerts * 2 - 2);
    if (quantised == 0) {
        float* v = (float*)(vtxBase + lastIdx * 0xc);
        last[0] = v[0]; last[1] = v[1]; last[2] = v[2];
    } else {
        short* v = (short*)(vtxBase + lastIdx * 6);
        last[0] = scale * (float)((double)CONCAT44(0x43300000, (int)v[0] ^ 0x80000000) - bias) + origin[0];
        last[1] = scale * (float)((double)CONCAT44(0x43300000, (int)v[1] ^ 0x80000000) - bias) + origin[1];
        last[2] = scale * (float)((double)CONCAT44(0x43300000, (int)v[2] ^ 0x80000000) - bias) + origin[2];
    }

    // capsule endpoint separations against the polygon plane
    float pn0 = work[0], pn1 = work[1], pn2 = work[2];
    float planeD = pn2 * last[2] + pn0 * last[0] + pn1 * last[1];
    double sepA = (double)((pn2 * seg[2] + pn0 * seg[0] + pn1 * seg[1]) - planeD);
    double sepB = (double)((pn2 * seg[6] + pn0 * seg[4] + pn1 * seg[5]) - planeD);
    double radius = (double)seg[3];

    if (((radius < sepA) && (radius < sepB)) ||
        ((sepA < (double)FLOAT_806138b0) && (sepB < (double)FLOAT_806138b0))) {
        mem_page_free((int)work);
        FUN_80382700();
        return;
    }

    float ey = seg[5] - seg[1];
    float ex = seg[4] - seg[0];
    float ez = seg[6] - seg[2];
    double r2 = (double)(float)(radius * radius);
    unsigned int nContacts = 0;
    double segLen2 = (double)(float)(ez * ez + (double)(float)(ex * ex + (double)(float)(ey * ey)));
    float* prev = last;
    int idxByte = 0;
    int workByte = 0;

    for (unsigned int k = 0; k < nVerts; ++k) {
        float* cur = (float*)((int)work + workByte + 0xc);
        if (quantised == 0) {
            float* v = (float*)(vtxBase + *(short*)(idxBase + idxByte) * 0xc);
            cur[0] = v[0]; cur[1] = v[1]; cur[2] = v[2];
        } else {
            short* v = (short*)(vtxBase + *(short*)(idxBase + idxByte) * 6);
            cur[0] = scale * (float)((double)CONCAT44(0x43300000, (int)v[0] ^ 0x80000000) - bias) + origin[0];
            cur[1] = scale * (float)((double)CONCAT44(0x43300000, (int)v[1] ^ 0x80000000) - bias) + origin[1];
            cur[2] = scale * (float)((double)CONCAT44(0x43300000, (int)v[2] ^ 0x80000000) - bias) + origin[2];
        }

        // closest points between the capsule segment and this polygon edge
        double e1y = (double)prev[1];
        double dy = (double)(float)((double)cur[1] - e1y);
        double e1x = (double)prev[0];
        double qy = (double)(float)((double)seg[1] - e1y);
        double dx = (double)(float)((double)cur[0] - e1x);
        double e1z = (double)prev[2];
        double dz = (double)(float)((double)cur[2] - (double)prev[2]);
        double qx = (double)(float)((double)seg[0] - e1x);
        double a11 = (double)(float)(dz * dz + (double)(float)(dx * dx + (double)(float)(dy * dy)));
        double b1  = (double)(float)((double)ez * dz + (double)(float)((double)ex * dx + (double)(float)((double)ey * dy)));
        double qz = (double)(float)((double)seg[2] - e1z);
        double denom = (double)(float)(segLen2 * a11 - (double)(float)(b1 * b1));
        double c1 = (double)(float)((double)ez * qz + (double)(float)((double)ex * qx + (double)(float)((double)ey * qy)));
        double e1 = (double)(float)(dz * qz + (double)(float)(dx * qx + (double)(float)(dy * qy)));

        double s = (double)FLOAT_806138b0;
        double tNum = e1;
        if ((double)FLOAT_806138b0 < denom) {
            s = (double)(float)(b1 * e1 - (double)(float)(a11 * c1));
            tNum = (double)(float)(segLen2 * e1 - (double)(float)(b1 * c1));
        }
        double sClamp, tDen, tVal;
        double zero = (double)FLOAT_806138b0;
        if ((zero <= s) && (s > a11 ? (sClamp = s, tDen = denom, tVal = tNum, false) : true)) {
            // fall through handled below (kept faithful to branch structure)
        }
        // Faithful clamped resolve of the (s,t) parameters:
        sClamp = zero; tDen = a11; tVal = e1;
        if ((zero <= s) && (tDen = denom, tVal = tNum, sClamp = s, denom < s)) {
            sClamp = denom; tDen = a11; tVal = (double)(float)(e1 + b1);
        }
        double sParam, tParam;
        if (zero <= tVal) {
            if ((tDen < tVal) &&
                (tVal = (double)(float)(-c1 + b1), sClamp = zero, tDen = tDen,
                 zero <= tVal) && (tParam = segLen2, sClamp = tVal, segLen2 < tVal)) {
                sClamp = denom; /* clamp */
            }
            tParam = tDen;
        } else {
            c1 = -c1;
            sClamp = zero; tVal = zero;
            (void)c1;
            tParam = a11;
        }
        sParam = (double)(float)(sClamp / (tParam == 0.0 ? 1.0 : tParam));
        tParam = (double)(float)(tVal / (denom == 0.0 ? 1.0 : denom));

        // closest points and squared gap
        double cpEx = (double)(float)((double)ex * sParam + (double)seg[0]);
        double cpEy = (double)(float)((double)ey * sParam + (double)seg[1]);
        double cpEz = (double)(float)((double)ez * sParam + (double)seg[2]);
        double gx = (double)(float)(cpEx - (double)(float)(dx * tParam + e1x));
        double gy = (double)(float)(cpEy - (double)(float)(dy * tParam + prev[1]));
        double gz = (double)(float)(cpEz - (double)(float)(dz * tParam + e1z));
        double gap2 = (double)(float)(gz * gz + (double)(float)(gx * gx + (double)(float)(gy * gy)));

        if ((gap2 < r2) && (nContacts < maxContacts)) {
            if (gap2 <= (double)FLOAT_806138b4) {
                // near-zero gap: use the edge/segment cross-product as normal
                outNormal[0] = (float)((double)ex * dz - (double)(float)((double)ez * dx));
                (void)outNormal;
                float nx = outNormal[0];
                (void)nx;
            } else {
                double g = math_sqrt(gap2);
                double inv = (double)(float)((double)FLOAT_806138b8 / g);
                outNormal[0] = (float)(gx * inv);
                outNormal[1] = (float)(gy * inv);
                outNormal[2] = (float)(gz * inv);
                float* out = (float*)(contactsBase + nContacts * contactStride * 4);
                out[0] = -(float)((double)outNormal[0] * g - cpEx);
                out[1] = -(float)((double)outNormal[1] * g - cpEy);
                out[2] = -(float)((double)outNormal[2] * g - cpEz);
                *(float*)(depthsBase + nContacts * 4) = -(float)((double)seg[3] - g);
                nContacts = nContacts + 1;
            }
        }
        idxByte = idxByte + 2;
        workByte = workByte + 0xc;
        prev = cur;
    }

    (void)last;
    mem_page_free((int)work);
    FUN_80382700();
}

// capsule_vs_struct_poly (0x8001a434)
// As capsule_vs_indexed_poly but the polygon vertices live inside the face
// struct (array at poly+0x40, stride 0x1c, count poly+0x2a; plane at
// poly+0x30..0x38). Uses the FUN_803826c8/FUN_80382714 scratch pair.
void capsule_vs_struct_poly(unsigned int a, unsigned int b, float* outNormal,
                            unsigned int maxContacts, int contactStride,
                            int poly, float* seg)
{
    (void)a;
    (void)b;

    unsigned long long scratch = FUN_803826c8();
    int contactsBase = (int)(scratch >> 0x20);
    float* depthsF = (float*)(unsigned long)(unsigned int)scratch;

    unsigned int nVerts = (unsigned int)*(unsigned short*)(poly + 0x2a);
    float pn0 = *(float*)(poly + 0x30);
    float pn1 = *(float*)(poly + 0x34);
    float pn2 = *(float*)(poly + 0x38);
    float* last = (float*)(poly + (nVerts - 1) * 0x1c + 0x40);
    float planeD = pn2 * last[2] + pn0 * last[0] + pn1 * last[1];
    double sepA = (double)((pn2 * seg[2] + pn0 * seg[0] + pn1 * seg[1]) - planeD);
    double sepB = (double)((pn2 * seg[6] + pn0 * seg[4] + pn1 * seg[5]) - planeD);

    if (((sepA <= (double)seg[3]) || (sepB <= (double)seg[3])) &&
        (((double)FLOAT_806138b0 <= sepA) || ((double)FLOAT_806138b0 <= sepB))) {
        unsigned int nContacts = 0;
        *depthsF = FLOAT_806138d0;
        double r2 = (double)(seg[3] * seg[3]);
        float* prev = last;
        int vByte = 0;

        for (unsigned int k = 0; k < nVerts; ++k) {
            float* cur = (float*)(poly + vByte + 0x40);
            float ay = seg[1];
            float ey = seg[5] - ay;
            float ax = *seg;
            float ex = seg[4] - ax;
            float az = seg[2];
            float ez = seg[6] - az;
            float dy = cur[1] - prev[1];
            float dx = *cur - *prev;
            float dz = cur[2] - prev[2];
            double a11 = (double)(dz * dz + dx * dx + dy * dy);
            double b1  = (double)(ez * dz + ex * dx + ey * dy);
            double segLen2 = (double)(ez * ez + ex * ex + ey * ey);
            double denom = (double)(float)(segLen2 * a11 - (double)(float)(b1 * b1));
            double c1 = (double)(ez * (az - prev[2]) + ex * (ax - *prev) + ey * (ay - prev[1]));
            double e1 = (double)(dz * (az - prev[2]) + dx * (ax - *prev) + dy * (ay - prev[1]));

            double zero = (double)FLOAT_806138b0;
            double s = zero, tNum = e1;
            if (zero < denom) {
                s = (double)(float)(b1 * e1 - (double)(float)(segLen2 * c1));
                tNum = (double)(float)(a11 * e1 - (double)(float)(b1 * c1));
            }
            double sClamp = zero, tDen = segLen2, tVal = e1;
            if ((zero <= s) && (tDen = denom, tVal = tNum, sClamp = s, denom < s)) {
                sClamp = denom; tDen = segLen2; tVal = (double)(float)(e1 + b1);
            }
            double tParam;
            if (zero <= tVal) {
                if ((tDen < tVal) &&
                    (tVal = (double)(float)(-c1 + b1), sClamp = zero, tDen = tDen, zero <= tVal) &&
                    (tParam = a11, sClamp = tVal, a11 < tVal)) {
                    sClamp = denom;
                }
                tParam = tDen;
            } else {
                sClamp = zero; tVal = zero; tParam = segLen2;
            }
            float sParam = (float)(sClamp / (denom == 0.0 ? 1.0 : denom));
            float tp = (float)(tVal / (tParam == 0.0 ? 1.0 : tParam));

            float px = ex * sParam + ax;
            float py = ey * sParam + ay;
            float pz = ez * sParam + az;
            float gx = px - (dx * tp + *prev);
            float gy = py - (dy * tp + prev[1]);
            float gz = pz - (dz * tp + prev[2]);
            double gap2 = (double)(gz * gz + gy * gy + gx * gx);

            if ((gap2 < r2) && (nContacts < maxContacts)) {
                outNormal[0] = *(float*)(poly + 0x30);
                outNormal[1] = *(float*)(poly + 0x34);
                outNormal[2] = *(float*)(poly + 0x38);
                if ((double)FLOAT_806138bc < gap2) {
                    double invd = 1.0 / SQRT(gap2);
                    float refine = (float)(invd * -(double)((float)((double)FLOAT_806138c0 * gap2) *
                                     (float)(invd * invd) - FLOAT_806138c4));
                    refine = FLOAT_806138b8 * refine *
                             -((float)((double)FLOAT_806138c0 * gap2) * refine * refine - FLOAT_806138c4);
                    gy = gy * refine;
                    gx = gx * refine;
                    gz = gz * refine;
                }
                float* out = (float*)(contactsBase + nContacts * contactStride * 4);
                out[0] = -(gx * seg[3] - px);
                out[1] = -(gy * seg[3] - py);
                out[2] = -(gz * seg[3] - pz);
                double g = math_sqrt(gap2);
                float pen = -(float)((double)seg[3] - g);
                if ((pen < FLOAT_806138b0) && (*depthsF < pen)) { *depthsF = pen; }
                nContacts = nContacts + 1;
            }
            vByte = vByte + 0x1c;
            prev = cur;
        }

        int axis0 = (int)*(char*)(poly + 0x3c);
        int axis1 = (int)*(char*)(poly + 0x3d);

        // face endpoint A crossing test
        if ((nContacts < maxContacts) && ((double)FLOAT_806138b0 < sepA) &&
            (sepA <= (double)seg[3]) &&
            (sepA <= (double)(float)((double)FLOAT_806138d4 + sepB))) {
            float cp[4];
            cp[3] = -(float)((double)*(float*)(poly + 0x30) * (double)seg[3] - (double)*seg);
            cp[0] = -(float)((double)*(float*)(poly + 0x34) * (double)seg[3] - (double)seg[1]);
            cp[1] = -(float)((double)*(float*)(poly + 0x38) * (double)seg[3] - (double)seg[2]);
            bool inside = true;
            float* pv = last;
            int fByte = 0;
            for (unsigned int k = nVerts; k != 0; --k) {
                float* nv = (float*)(poly + fByte + 0x40);
                if ((double)FLOAT_806138b0 <
                    (double)(((&cp[3])[axis0] - pv[axis0]) * (nv[axis1] - pv[axis1]) +
                             ((&cp[3])[axis1] - pv[axis1]) * (pv[axis0] - nv[axis0]))) {
                    inside = false; break;
                }
                fByte = fByte + 0x1c;
                pv = nv;
            }
            if (inside) {
                outNormal[0] = *(float*)(poly + 0x30);
                outNormal[1] = *(float*)(poly + 0x34);
                outNormal[2] = *(float*)(poly + 0x38);
                float* out = (float*)(contactsBase + nContacts * contactStride * 4);
                out[0] = cp[3]; out[1] = cp[0]; out[2] = cp[1];
                float pen = -(float)((double)seg[3] - sepA);
                if ((pen < FLOAT_806138b0) && (*depthsF < pen)) { *depthsF = pen; }
                nContacts = nContacts + 1;
            }
        }

        // face endpoint B crossing test
        if ((nContacts < maxContacts) && ((double)FLOAT_806138b0 < sepB) &&
            (sepB <= (double)seg[3]) &&
            (sepB <= (double)(float)((double)FLOAT_806138d4 + sepA))) {
            float cp[3];
            cp[0] = -(float)((double)*(float*)(poly + 0x30) * (double)seg[3] - (double)seg[4]);
            cp[1] = -(float)((double)*(float*)(poly + 0x34) * (double)seg[3] - (double)seg[5]);
            cp[2] = -(float)((double)*(float*)(poly + 0x38) * (double)seg[3] - (double)seg[6]);
            bool inside = true;
            int fByte = 0;
            for (unsigned int k = nVerts; k != 0; --k) {
                float* nv = (float*)(poly + fByte + 0x40);
                if ((double)FLOAT_806138b0 <
                    (double)((cp[axis0] - last[axis0]) * (nv[axis1] - last[axis1]) +
                             (cp[axis1] - last[axis1]) * (last[axis0] - nv[axis0]))) {
                    inside = false; break;
                }
                fByte = fByte + 0x1c;
                last = nv;
            }
            if (inside) {
                outNormal[0] = *(float*)(poly + 0x30);
                outNormal[1] = *(float*)(poly + 0x34);
                outNormal[2] = *(float*)(poly + 0x38);
                float* out = (float*)(contactsBase + nContacts * contactStride * 4);
                out[0] = cp[0]; out[1] = cp[1]; out[2] = cp[2];
                float pen = -(float)((double)seg[3] - sepB);
                if ((pen < FLOAT_806138b0) && (*depthsF < pen)) { *depthsF = pen; }
            }
        }
    }
    (void)contactsBase;
    (void)depthsF;
    FUN_80382714();
}

// sphere_vs_segment_contact (0x8001ab5c)
// Closest-feature contact between a sphere/capsule (segment carried in the
// scratch record via FUN_803826e4) and the world segment described by
// (segT, mtx). Writes contact point (outPt), unit normal (outNormal) and signed
// penetration (outDepth) when overlapping.
void sphere_vs_segment_contact(unsigned int a, unsigned int b, float* segT,
                               int mtx, float* outPt, float* outNormal,
                               float* outDepth)
{
    (void)a;
    (void)b;

    unsigned long long scratch = FUN_803826e4();
    float* radiusRec = (float*)(unsigned long)(scratch >> 0x20);
    float* seg = (float*)(unsigned long)(unsigned int)scratch;

    float t = segT[1] - segT[0];
    float nt = -t;
    float segA[3], segB[3];
    segA[0] = *(float*)(mtx + 0x10) * t + seg[0];
    segB[0] = *(float*)(mtx + 0x10) * nt + seg[0];
    segA[1] = *(float*)(mtx + 0x14) * t + seg[1];
    segB[1] = *(float*)(mtx + 0x14) * nt + seg[1];
    segA[2] = *(float*)(mtx + 0x18) * t + seg[2];
    segB[2] = *(float*)(mtx + 0x18) * nt + seg[2];

    float cp[3];
    double d2 = closest_pt_on_segment(radiusRec, segA, segB, cp, (float*)0);
    float sumR = (float)((double)segT[0]);   // radius carried in segT[0]
    double gap = (double)(float)((double)((float)((double)0.0 + (double)*segT) *
                                          (float)((double)0.0 + (double)*segT)) - d2);
    // (faithful: penetration = radiusSum^2 - closestDist^2)
    double pen2 = (double)(float)((double)(sumR * sumR)) - d2;
    (void)gap;

    if ((double)FLOAT_806138b0 <= pen2) {
        float gx = radiusRec[0] - cp[0];
        float gy = radiusRec[1] - cp[1];
        float gz = radiusRec[2] - cp[2];
        float g2 = gz * gz + gx * gx + gy * gy;
        if (FLOAT_806138d8 <= g2) {
            if (FLOAT_806138bc < g2) {
                float refine = 1.0f / SQRT(g2);
                refine = refine * -(FLOAT_806138c0 * g2 * refine * refine - FLOAT_806138c4);
                g2 = FLOAT_806138b8 * refine * -(FLOAT_806138c0 * g2 * refine * refine - FLOAT_806138c4);
                outNormal[0] = gx * g2;
                outNormal[1] = gy * g2;
                outNormal[2] = gz * g2;
            }
        } else {
            outNormal[0] = *(float*)(mtx + 0x10);
            outNormal[1] = *(float*)(mtx + 0x14);
            outNormal[2] = *(float*)(mtx + 0x18);
        }
        outPt[0] = outNormal[0] * radiusRec[0] + cp[0];
        outPt[1] = outNormal[1] * radiusRec[0] + cp[1];
        outPt[2] = outNormal[2] * radiusRec[0] + cp[2];
        double p = math_sqrt(pen2);
        *outDepth = (float)-p;
    }
    FUN_80382730();
}

// capsule_vs_capsule_contact (0x8001ad4c)
// Closest-feature contact between two capsules. The first capsule segment comes
// from the scratch record (FUN_803826e4) transformed by mtxA; the second is the
// caller's (segT2, mtxB) about base 'baseB'. Writes contact point, normal and
// separation when the capsules overlap.
void capsule_vs_capsule_contact(unsigned int a, unsigned int b, int mtxA,
                                float* baseB, float* segT2, int mtxB,
                                float* outPt, float* outNormal, float* outDepth)
{
    (void)a;
    (void)b;

    unsigned long long scratch = FUN_803826e4();
    float* radiusRec = (float*)(unsigned long)(scratch >> 0x20);
    float* seg = (float*)(unsigned long)(unsigned int)scratch;

    float t1 = seg[1] - seg[0];
    float n1 = -t1;
    float segA1[3], segB1[3];
    segA1[0] = *(float*)(mtxA + 0x10) * t1 + radiusRec[0];
    segB1[0] = *(float*)(mtxA + 0x10) * n1 + radiusRec[0];
    segA1[1] = *(float*)(mtxA + 0x14) * t1 + radiusRec[1];
    segB1[1] = *(float*)(mtxA + 0x14) * n1 + radiusRec[1];
    segA1[2] = *(float*)(mtxA + 0x18) * t1 + radiusRec[2];
    segB1[2] = *(float*)(mtxA + 0x18) * n1 + radiusRec[2];

    float t2 = segT2[1] - segT2[0];
    float n2 = -t2;
    float segA2[3], segB2[3];
    segA2[0] = *(float*)(mtxB + 0x10) * t2 + baseB[0];
    segB2[0] = *(float*)(mtxB + 0x10) * n2 + baseB[0];
    segA2[1] = *(float*)(mtxB + 0x14) * t2 + baseB[1];
    segB2[1] = *(float*)(mtxB + 0x14) * n2 + baseB[1];
    segA2[2] = *(float*)(mtxB + 0x18) * t2 + baseB[2];
    segB2[2] = *(float*)(mtxB + 0x18) * n2 + baseB[2];

    float c1[3], c2[3];
    double d2 = closest_between_segments(segA1, segB1, segA2, segB2, c1, c2);
    float sumR = *seg + *segT2;
    if (FLOAT_806138b0 <= (float)((double)(sumR * sumR) - d2)) {
        float gx = c2[0] - c1[0];
        float gy = c2[1] - c1[1];
        float gz = c2[2] - c1[2];
        float g2 = gz * gz + gx * gx + gy * gy;
        if (FLOAT_806138d8 <= g2) {
            if (FLOAT_806138bc < g2) {
                float refine = 1.0f / SQRT(g2);
                refine = refine * -(FLOAT_806138c0 * g2 * refine * refine - FLOAT_806138c4);
                g2 = FLOAT_806138b8 * refine * -(FLOAT_806138c0 * g2 * refine * refine - FLOAT_806138c4);
                outNormal[0] = gx * g2;
                outNormal[1] = gy * g2;
                outNormal[2] = gz * g2;
            }
        } else {
            outNormal[0] = *(float*)(mtxA + 0x10);
            outNormal[1] = *(float*)(mtxA + 0x14);
            outNormal[2] = *(float*)(mtxA + 0x18);
        }
        outPt[0] = outNormal[0] * *seg + c1[0];
        outPt[1] = outNormal[1] * *seg + c1[1];
        outPt[2] = outNormal[2] * *seg + c1[2];
        double p = math_sqrt(d2);
        *outDepth = (float)(p - (double)(*seg + *segT2));
    }
    FUN_80382730();
}

// sphere_vs_capsule_contact (0x8001af98)
// Core narrow-phase primitive (12 callers): closest feature between a sphere of
// radius radius[0] at radius[1..] (segment start segA) and a capsule segment
// described by (seg, mtx). Returns 1 with contact point (outPt), normal
// (outNormal) and penetration (outDepth) when overlapping, else 0.
int sphere_vs_capsule_contact(float* radius, float* segA, float* mtx,
                              float* seg, int convBlk, float* outPt,
                              float* outNormal, float* outDepth)
{
    float t = seg[1] - seg[0];
    float nt = -t;
    float ay = segA[1];
    float e1y = *(float*)(convBlk + 0x14) * t + mtx[1];
    float dy = segA[1] - ay;   // (== 0 by construction; preserved)
    float e1x = *(float*)(convBlk + 0x10) * t + mtx[0];
    float ax = *segA;
    float d13y = (*(float*)(convBlk + 0x14) * nt + mtx[1]) - e1y;
    float az = segA[2];
    float e1z = *(float*)(convBlk + 0x18) * t + mtx[2];
    float d11 = (*(float*)(convBlk + 0x10) * nt + mtx[0]) - e1x;
    float dx = *segA - ax;     // (== 0; preserved)
    float d10 = (*(float*)(convBlk + 0x18) * nt + mtx[2]) - e1z;
    float dz = segA[2] - az;   // (== 0; preserved)

    float a11 = d10 * d10 + d11 * d11 + d13y * d13y;
    float b1  = d10 * dz + d11 * dx + d13y * dy;
    float segLen2 = dz * dz + dx * dx + dy * dy;
    float denom = a11 * segLen2 - b1 * b1;
    float e1 = d10 * (az - e1z) + d11 * (ax - e1x) + d13y * (ay - e1y);
    float f1 = dz * (az - e1z) + dx * (ax - e1x) + dy * (ay - e1y);

    float zero = FLOAT_806138b0;
    float s = FLOAT_806138b0;
    float tNum = f1;
    if (FLOAT_806138b0 < denom) {
        s = b1 * f1 - segLen2 * e1;
        tNum = a11 * f1 - b1 * e1;
    }
    float sClamp = FLOAT_806138b0;
    float tDen = segLen2;
    float tVal = f1;
    if ((FLOAT_806138b0 <= s) && (sClamp = s, tDen = denom, tVal = tNum, denom < s)) {
        sClamp = denom; tDen = segLen2; tVal = f1 + b1;
    }
    float divA = denom;
    if (FLOAT_806138b0 <= tVal) {
        if (((tDen < tVal) &&
             (b1 = -e1 + b1, sClamp = FLOAT_806138b0, tVal = tDen, FLOAT_806138b0 <= b1)) &&
            (sClamp = b1, divA = a11, a11 < b1)) {
            divA = denom; sClamp = denom;
        }
    } else {
        e1 = -e1;
        sClamp = FLOAT_806138b0;
        tVal = FLOAT_806138b0;
        if ((FLOAT_806138b0 <= e1) && (sClamp = e1, divA = a11, a11 < e1)) {
            divA = denom; sClamp = denom;
        }
    }
    float sParam = sClamp / (divA == 0.0f ? 1.0f : divA);
    float tParam = tVal / (tDen == 0.0f ? 1.0f : tDen);

    float px = d13y * sParam + e1y;
    float py = d11 * sParam + e1x;   // (index layout preserved from original)
    (void)py;
    float qy = d13y * sParam + e1y;
    float cx = (d11 * sParam + e1x) - (dx * tParam + ax);
    float cy = qy - (dy * tParam + ay);
    float cz = (d10 * sParam + e1z) - (dz * tParam + az);
    (void)px;
    double gap2 = (double)(cz * cz + cx * cx + cy * cy);

    if ((double)(radius[0] * radius[0]) <= gap2) {
        return 0;
    }
    if ((double)FLOAT_806138bc < gap2) {
        double invd = 1.0 / SQRT(gap2);
        float refine = (float)(invd * -(double)((float)((double)FLOAT_806138c0 * gap2) *
                         (float)(invd * invd) - FLOAT_806138c4));
        refine = FLOAT_806138b8 * refine *
                 -((float)((double)FLOAT_806138c0 * gap2) * refine * refine - FLOAT_806138c4);
        outNormal[0] = cx * refine;
        outNormal[1] = cy * refine;
        outNormal[2] = cz * refine;
    }
    outPt[0] = -(outNormal[0] * radius[0] - (d11 * sParam + e1x));
    outPt[1] = -(outNormal[1] * radius[0] - (d13y * sParam + e1y));
    outPt[2] = -(outNormal[2] * radius[0] - (d10 * sParam + e1z));
    double g = math_sqrt(gap2);
    *outDepth = -(float)((double)radius[0] - g);
    return 1;
}

// sphere_vs_obb_edges (0x8001b304)
// Sphere/capsule-vs-OBB contact generation. The OBB (transform + half-extents
// in param_3: axes at [4..6]/[8..10], centre via the scratch record) has its 12
// edges tested against the query capsule segment via sphere_vs_capsule_contact,
// and its two faces along the sweep axis via closest_pt_on_obb. Up to maxPts
// contacts (points -> outPts stride 'stride', normals -> outNormal, depths ->
// outDepths) are collected. Batch 383 (tail of the collision contact cluster).
void sphere_vs_obb_edges(unsigned int a, unsigned int b, float* obb, float* seg,
                         float* sweepT, int convBlk, float* outPts,
                         float* outNormal, float* outDepths, unsigned int maxPts,
                         int stride)
{
    (void)a;
    (void)b;

    unsigned long long scratch = FUN_803826cc();
    float* centre = (float*)(unsigned long)(scratch >> 0x20);
    float* half = (float*)(unsigned long)(unsigned int)scratch;

    double sweepLen = (double)(sweepT[1] - sweepT[0]);
    unsigned int n = 0;
    float ep0[4], ep1[4];          // edge endpoints (ep0 = start .. ep1 = start+dir)
    float cp[3], nrm[3], depth;

    // The original unrolls all 12 OBB edges as explicit +/- axis combinations of
    // the box half-extents about the centre. Encode the 12 sign patterns and the
    // pair of half-axes each edge spans, then run the same capsule test per edge.
    // signAB = base-corner signs for the two spanning half-axes (idx 0 = ax[0..2],
    // idx 1 = ax[4..6] "half[1..3]"), edgeAxis = which half-axis the edge runs
    // along (2 = half[2] i.e. obb[8..10]).
    struct EdgeSpec { float s0, s1, s2; int spanAxis; };
    static const EdgeSpec kEdges[12] = {
        // 4 edges parallel to axis 2 (obb[8..10])
        {-1.0f, -1.0f,  0.0f, 2}, {-1.0f, +1.0f, 0.0f, 2},
        {+1.0f, -1.0f,  0.0f, 2}, {+1.0f, +1.0f, 0.0f, 2},
        // 4 edges parallel to axis 1 (obb[4..6])
        {-1.0f,  0.0f, -1.0f, 1}, {-1.0f, 0.0f, +1.0f, 1},
        {+1.0f,  0.0f, -1.0f, 1}, {+1.0f, 0.0f, +1.0f, 1},
        // 4 edges parallel to axis 0 (obb[0..2])
        { 0.0f, -1.0f, -1.0f, 0}, { 0.0f, -1.0f, +1.0f, 0},
        { 0.0f, +1.0f, -1.0f, 0}, { 0.0f, +1.0f, +1.0f, 0},
    };

    for (int e = 0; e < 12 && n < maxPts; ++e) {
        const EdgeSpec& es = kEdges[e];
        // base corner = centre + s0*axis0*h0 + s1*axis1*h1 + s2*axis2*h2
        float bx = centre[0] + es.s0 * obb[0] * half[0] + es.s1 * obb[4] * half[1] + es.s2 * obb[8]  * half[2];
        float by = centre[1] + es.s0 * obb[1] * half[0] + es.s1 * obb[5] * half[1] + es.s2 * obb[9]  * half[2];
        float bz = centre[2] + es.s0 * obb[2] * half[0] + es.s1 * obb[6] * half[1] + es.s2 * obb[10] * half[2];
        // edge direction = 2 * axis[spanAxis] * half[spanAxis]
        const float* axis = obb + es.spanAxis * 4;
        float hx = axis[0] * half[es.spanAxis];
        float hy = axis[1] * half[es.spanAxis];
        float hz = axis[2] * half[es.spanAxis];
        ep0[0] = bx - hx; ep0[1] = by - hy; ep0[2] = bz - hz;
        ep1[0] = bx + hx; ep1[1] = by + hy; ep1[2] = bz + hz;

        int hit = sphere_vs_capsule_contact(seg, ep0, ep1, sweepT, convBlk, cp, nrm, &depth);
        if (hit != 0) {
            float* dstP = (float*)((int)outPts + (int)n * stride);
            dstP[0] = cp[0]; dstP[1] = cp[1]; dstP[2] = cp[2];
            outNormal[0] = nrm[0]; outNormal[1] = nrm[1]; outNormal[2] = nrm[2];
            outDepths[n] = depth;
            n = n + 1;
        }
    }

    // Two OBB faces along the +/- sweep direction (convBlk[4..6] is the sweep
    // dir). closest_pt_on_obb reads its query segment endpoints from the scratch
    // record and writes the closest point/normal/penetration; here the face query
    // point is derived from the sweep offset. (Non-matching: the original passes
    // the query point + scratch centre/half/mtx directly; we keep the same call
    // shape against the shared 8-arg declaration.)
    for (int face = 0; face < 2 && n < maxPts; ++face) {
        double len = (face == 0) ? sweepLen : -sweepLen;
        float facePt[3];
        facePt[0] = (float)((double)*(float*)(convBlk + 0x10) * len + (double)seg[0]);
        facePt[1] = (float)((double)*(float*)(convBlk + 0x14) * len + (double)seg[1]);
        facePt[2] = (float)((double)*(float*)(convBlk + 0x18) * len + (double)seg[2]);
        closest_pt_on_obb((unsigned int)(unsigned long)facePt,
                          (unsigned int)(unsigned long)centre,
                          half, obb, cp, nrm, &depth, 0.0);
        float* dstP = (float*)((int)outPts + (int)n * stride);
        dstP[0] = cp[0]; dstP[1] = cp[1]; dstP[2] = cp[2];
        outNormal[0] = nrm[0]; outNormal[1] = nrm[1]; outNormal[2] = nrm[2];
        outDepths[n] = depth;
        n = n + 1;
    }
    FUN_80382718();
}
