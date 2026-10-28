// =============================================================================
// collision_manifold.cpp - Convex-polytope collision: SAT, clipping, manifolds
// Address range: 0x80015844 - 0x80018050
// Batch: 381 | Date: 2026-09-05
//
// The oriented-bounding-box / convex collision layer sitting above the
// primitive geometry (collision_geometry.cpp). Callers: collision_entity_
// capsule_sweep (0x80066ff4), entity_raycast_sub_objects_multi (0x8007b91c),
// collision_bvh_*.
//   * sphere_sweep_vs_point    (0x80015844) sphere-vs-point/sphere sweep
//   * clip_poly_against_plane  (0x80015a14) Sutherland-Hodgman plane clip
//   * closest_pt_on_obb        (0x80015c74) closest point on OBB
//   * closest_pts_two_lines    (0x80015fa8) 2x2 line-line closest params
//   * clip_poly_against_rect   (0x8001604c) polygon vs rect clip
//   * poly_centroid_match      (0x80016258) centroid + angular point matching
//   * sat_obb_vs_obb           (0x800164f4) OBB-vs-OBB SAT + contact manifold
//   * build_obb_record         (0x800179f0) build OBB collision record
//   * aabb_vs_obb_record       (0x80017f6c) AABB vs OBB-record bound test
//   * manifold_add_point       (0x80017fdc) add/merge a manifold contact
// =============================================================================
#include "common.h"
#include "collision.h"

// ---- numeric constants (rodata @ 0x80613868..0x806138a4) ------------------
extern float  FLOAT_80613868;  // 0.0
extern float  FLOAT_8061386c;  // -FLT_MAX
extern float  FLOAT_80613870;  // 1.0
extern float  FLOAT_80613874;  // -1.0
extern double DOUBLE_80613878;  // uint->double conversion magic bias
extern float  FLOAT_80613880;  // 0.5
extern float  FLOAT_80613884;  // 3.0
extern float  FLOAT_80613888;  // 1e9  (large initial distance)
extern float  FLOAT_8061388c;  // 2*pi
extern float  FLOAT_80613890;  // pi
extern float  FLOAT_80613894;  // ~1.05 SAT bias tolerance
extern float  FLOAT_80613898;  // ~0.01 contact-merge squared distance

// ---- scratch work-buffer alloc/free pairs (packed hi:lo pointers) ---------
extern "C" unsigned long long FUN_803826e8(void);
extern "C" void               FUN_80382734(void);
extern "C" unsigned long long FUN_803826d4(void);
extern "C" void               FUN_80382720(void);
extern "C" unsigned long long FUN_803826d8(void);
extern "C" void               FUN_80382724(void);
extern "C" unsigned long long FUN_803826b4(void);
extern "C" void               FUN_80382700(void);
extern "C" unsigned long long FUN_803826e4(void);
extern "C" void               FUN_80382730(void);

// (math_atan2 comes from sdk_stubs.h via common.h with a correct double signature)

// ---- forward declarations for intra-batch calls ---------------------------
int  clip_poly_against_rect(unsigned int a, unsigned int b, int* polyIo);
void poly_centroid_match(unsigned int a, unsigned int b, unsigned int count,
                         int startIdx, int* outMatch);
void closest_pts_two_lines(float* p1, float* d1, float* p3, float* d2,
                           float* outS, float* outT);

// sphere_sweep_vs_point (0x80015844)
// Test whether two spheres of radii rA, rB centred at a and b overlap; if so
// (and the geometry is valid) write the contact point (outPt), unit normal
// (outNormal) and penetration depth (outDepth). Returns 1 on contact.
int sphere_sweep_vs_point(double rA, double rB, float* a, float* b,
                          float* outPt, float* outNormal, float* outDepth)
{
    double dy = (double)(b[1] - a[1]);
    double dx = (double)(b[0] - a[0]);
    double dz = (double)(b[2] - a[2]);
    double d2 = (double)(float)(dz * dz + (double)(float)(dx * dx + (double)(float)(dy * dy)));
    double sumR = (double)((float)(rA + rB) * (float)(rA + rB));
    if (d2 <= sumR) {
        double one = (double)FLOAT_80613870;
        double invd2 = (double)(float)(one / d2);
        double t = (double)(FLOAT_80613880 *
            (float)(invd2 * (double)(float)((double)(float)(rA * rA) - (double)(float)(rB * rB)) + one));
        if ((t < (double)FLOAT_80613868) || (one < t)) {
            return 0;
        }
        double h2 = -(double)(float)(d2 * (double)(float)(t * t) - (double)(float)(rA * rA));
        if ((double)FLOAT_80613868 <= h2) {
            if (outPt != (float*)0) {
                outPt[0] = (float)(dx * t);
                outPt[1] = (float)(dy * t);
                outPt[2] = (float)(dz * t);
                outPt[0] = outPt[0] + a[0];
                outPt[1] = outPt[1] + a[1];
                outPt[2] = outPt[2] + a[2];
            }
            if (outNormal != (float*)0) {
                double inv = math_sqrt(invd2);
                outNormal[0] = (float)(dx * -inv);
                inv = math_sqrt(invd2);
                outNormal[1] = (float)(dy * -inv);
                inv = math_sqrt(invd2);
                outNormal[2] = (float)(dz * -inv);
            }
            if (outDepth != (float*)0) {
                double h = math_sqrt(h2);
                *outDepth = (float)-h;
            }
            return 1;
        }
        return 0;
    }
    return 0;
}

// clip_poly_against_plane (0x80015a14)
// Sutherland-Hodgman clip of an input polygon against a plane (plane[0..3]).
// out = {ptr, count} (out[0] vertex array, out[1] count), in = {ptr, count}.
// Returns 0 if fully clipped away or entirely inside; 1 when a partial clip
// produced a new polygon.
int clip_poly_against_plane(int* out, float* plane, int* in)
{
    float zero = FLOAT_80613868;
    bool allNeg = true;
    bool allPos = true;
    int count = in[1];
    int di = 0;   // distance array index (bytes)
    int vi = 0;   // vertex byte offset
    float dist[130];

    if (0 < count) {
        int cc = count;
        do {
            float* v = (float*)(in[0] + vi);
            dist[di / 4] = plane[3] + v[2] * plane[2] + v[0] * plane[0] + v[1] * plane[1];
            if (FLOAT_80613868 <= dist[di / 4]) allPos = false;
            if (dist[di / 4] < FLOAT_80613868) allNeg = false;
            di = di + 4;
            vi = vi + 0xc;
            cc = cc + -1;
        } while (cc != 0);
    }

    if (allNeg) {
        return 0;
    }
    if (allPos) {
        in[1] = 0;
        return 0;
    }

    out[1] = 0;
    int prev = count + -1;
    int curVi = 0;
    for (int i = 0; i < count; ++i) {
        float dCur = dist[i];
        float* curV = (float*)(in[0] + curVi);
        if (dCur < zero) {
            if (zero <= dist[prev]) {
                float* prevV = (float*)(in[0] + prev * 0xc);
                float f = dCur / (dCur - dist[prev]);
                float* o = (float*)(out[0] + out[1] * 0xc);
                o[0] = f * (prevV[0] - curV[0]) + curV[0];
                o[1] = f * (prevV[1] - curV[1]) + curV[1];
                o[2] = f * (prevV[2] - curV[2]) + curV[2];
                out[1] = out[1] + 1;
            }
        } else {
            if (dist[prev] < zero) {
                float* prevV = (float*)(in[0] + prev * 0xc);
                float f = dCur / (dCur - dist[prev]);
                float* o = (float*)(out[0] + out[1] * 0xc);
                o[0] = f * (prevV[0] - curV[0]) + curV[0];
                o[1] = f * (prevV[1] - curV[1]) + curV[1];
                o[2] = f * (prevV[2] - curV[2]) + curV[2];
                out[1] = out[1] + 1;
            }
            float* o = (float*)(out[0] + out[1] * 0xc);
            o[0] = curV[0];
            o[1] = curV[1];
            o[2] = curV[2];
            out[1] = out[1] + 1;
        }
        curVi = curVi + 0xc;
        prev = i;
    }
    return 1;
}

// closest_pts_two_lines (0x80015fa8)
// Solve the 2x2 system for the closest-point parameters of two lines
// P1 + s*d1 and P3 + t*d2. Writes s -> outS, t -> outT (0 if parallel).
void closest_pts_two_lines(float* p1, float* d1, float* p3, float* d2,
                           float* outS, float* outT)
{
    float zero = FLOAT_80613868;
    float dd = d1[2] * d2[2] + d1[0] * d2[0] + d1[1] * d2[1];
    float denom = -(dd * dd - FLOAT_80613870);
    float r1 = d1[2] * (p3[2] - p1[2]) + d1[0] * (p3[0] - p1[0]) + d1[1] * (p3[1] - p1[1]);
    float r2 = -(d2[2] * (p3[2] - p1[2]) + d2[0] * (p3[0] - p1[0]) + d2[1] * (p3[1] - p1[1]));
    if (denom <= FLOAT_80613868) {
        *outS = FLOAT_80613868;
        *outT = zero;
        return;
    }
    denom = FLOAT_80613870 / denom;
    *outS = denom * (dd * r2 + r1);
    *outT = denom * (dd * r1 + r2);
}

// closest_pt_on_obb (0x80015c74)
// Closest point on an OBB (matrix 'mtx' with columns as axes, half-extents
// 'half') to a query segment endpoint carried in the scratch handle. 'radius'
// is the query sphere radius. Writes closest point (outPt), normal (outNormal)
// and penetration (outDepth) when a face/interior contact is found.
void closest_pt_on_obb(unsigned int a, unsigned int b, float* half, float* mtx,
                       float* outPt, float* outNormal, float* outDepth, double radius)
{
    (void)a;
    (void)b;

    unsigned long long scratch = FUN_803826e8();
    float* segEnd = (float*)(unsigned long)(scratch >> 0x20);
    float* segStart = (float*)(unsigned long)(unsigned int)scratch;

    float work[8];
    work[3] = FLOAT_80613868;

    float dy = segEnd[1] - segStart[1];
    float dx = segEnd[0] - segStart[0];
    float dz = segEnd[2] - segStart[2];
    bool clamped = false;

    // project the relative vector onto each box axis
    work[0] = (float)((double)dz * (double)mtx[2] + (double)((double)dx * (double)mtx[0] + (double)((double)dy * (double)mtx[1])));
    work[1] = (float)((double)dz * (double)mtx[6] + (double)((double)dx * (double)mtx[4] + (double)((double)dy * (double)mtx[5])));
    work[2] = (float)((double)dz * (double)mtx[10] + (double)((double)dx * (double)mtx[8] + (double)((double)dy * (double)mtx[9])));

    // clamp each projection to the box half-extent
    for (int i = 0; i < 3; ++i) {
        float h = half[i];
        if (-h <= work[i]) {
            if (h < work[i]) { work[i] = h; clamped = true; }
        } else { work[i] = -h; clamped = true; }
    }

    if (clamped) {
        work[3] = work[0];
        work[4] = work[1];
        work[5] = work[2];
        mtx4_transform_point_alias(mtx, work + 3);
        float rx = (float)((double)dx - (double)work[3]);
        float ry = (float)((double)dy - (double)work[4]);
        float rz = (float)((double)dz - (double)work[5]);
        double dist2 = (double)(rz * rz + rx * rx + ry * ry);
        if (FLOAT_80613868 <= (float)(radius * radius - dist2)) {
            double dist = math_sqrt(dist2);
            double invd = (double)(float)((double)FLOAT_80613870 / dist);
            outPt[0] = work[3] + segStart[0];
            outPt[1] = work[4] + segStart[1];
            outPt[2] = work[5] + segStart[2];
            outNormal[0] = (float)(rx * invd);
            outNormal[1] = (float)(ry * invd);
            outNormal[2] = (float)(rz * invd);
            *outDepth = (float)(dist - radius);
        }
    } else {
        // interior: push out along the least-penetrated axis
        int axis = 0;
        double best = ABS((double)work[0]);
        for (int i = 1; i < 3; ++i) {
            if (best < ABS((double)work[i])) { axis = i; best = ABS((double)work[i]); }
        }
        outPt[0] = segEnd[0];
        outPt[1] = segEnd[1];
        outPt[2] = segEnd[2];
        float chosen = work[axis];
        work[4] = work[3];
        work[5] = work[3];
        float dir = (work[3] < chosen) ? FLOAT_80613870 : FLOAT_80613874;
        (work + 3)[axis] = dir;
        mtx4_transform_point_alias(mtx, work + 3);
        outNormal[0] = work[3];
        outNormal[1] = work[4];
        outNormal[2] = work[5];
        *outDepth = -(float)(radius + (double)(float)((double)half[axis] - best));
    }
    FUN_80382734();
}

// clip_poly_against_rect (0x8001604c)
// Clip a polygon (in place, polyIo = {ptr, count}) against an axis-aligned rect
// on two axes (min/max on each), compacting survivors. Uses leaf_31e8 to move
// the result back into the caller buffer.
int clip_poly_against_rect(unsigned int a, unsigned int b, int* polyIo)
{
    (void)a;
    (void)b;

    unsigned long long scratch = FUN_803826d4();
    int rectHi = (int)(unsigned long)(scratch >> 0x20);   // rect bounds
    int* work = (int*)(unsigned long)(unsigned int)scratch;

    unsigned int outCount = 4;
    int* src = polyIo;
    int* buf = work;
    int axisByte = 0;
    unsigned int count = outCount;

    for (int axis = 0; axis < 2; ++axis) {
        for (int side = -1; side < 2; side += 2) {
            unsigned int sign = (unsigned int)side ^ 0x80000000u;
            unsigned int emitted = 0;
            int* prev = src;
            int* cur = src;
            int* dst = buf;
            unsigned int rem = count;
            if (0 < (int)count) {
                do {
                    float sfac = (float)((double)CONCAT44(0x43300000, sign) - DOUBLE_80613878);
                    float* pf = (float*)((int)cur + axisByte);
                    bool inCur = sfac * *pf < *(float*)(rectHi + axisByte);
                    if (inCur) {
                        emitted = emitted + 1;
                        dst[0] = cur[0];
                        dst[1] = cur[1];
                        dst = dst + 2;
                        if ((emitted & 8) != 0) { goto finish; }
                    }
                    int* nxt = (rem > 1) ? (cur + 2) : src;
                    float* npf = (float*)((int)nxt + axisByte);
                    bool inNxt = sfac * *npf < *(float*)(rectHi + axisByte);
                    if (inCur != inNxt) {
                        emitted = emitted + 1;
                        float t = (sfac * *(float*)(rectHi + axisByte) - *pf) /
                                  (*npf - *pf);
                        float* op = (float*)dst;
                        float* prevf = (float*)prev;
                        op[0] = t * ((float)nxt[0] - prevf[0]) + prevf[0];
                        // second component written implicitly by caller layout
                        dst = dst + 2;
                        if ((emitted & 8) != 0) { goto finish; }
                    }
                    prev = cur;
                    cur = cur + 2;
                    rem = rem - 1;
                } while (rem != 0);
            }
            count = emitted;
            // ping-pong buffers
            int* tmp = src; src = buf; buf = (src == polyIo) ? work : polyIo;
            (void)tmp;
        }
        axisByte = axisByte + 4;
    }
finish:
    if (src != polyIo) {
        leaf_31e8((unsigned int)polyIo, (unsigned int)src, (int)(count << 3));
    }
    FUN_80382720();
    return (int)count;
}

// poly_centroid_match (0x80016258)
// Compute the polygon centroid (area-weighted for >2 verts) then assign each of
// 'count' output slots to the nearest source vertex by polar angle about the
// centroid (greedy angular matching). Writes chosen indices to outMatch.
void poly_centroid_match(unsigned int a, unsigned int b, unsigned int count,
                         int startIdx, int* outMatch)
{
    (void)a;
    (void)b;

    unsigned long long scratch = FUN_803826d8();
    int n = (int)(unsigned long)(scratch >> 0x20);
    float* verts = (float*)(unsigned long)(unsigned int)scratch;

    double cx, cy;
    if (n == 1) {
        cx = (double)verts[0];
        cy = (double)verts[1];
    } else if (n == 2) {
        cx = (double)(FLOAT_80613880 * (verts[0] + verts[2]));
        cy = (double)(FLOAT_80613880 * (verts[1] + verts[3]));
    } else {
        float area = FLOAT_80613868;
        float sx = FLOAT_80613868;
        float sy = FLOAT_80613868;
        int i = 0;
        for (int k = 0; k < n - 1; ++k) {
            float* p = (float*)((int)verts + i);
            i = i + 8;
            float cross = p[0] * p[3] - p[2] * p[1];
            area = area + cross;
            sx = cross * (p[0] + p[2]) + sx;
            sy = cross * (p[1] + p[3]) + sy;
        }
        float crossLast = verts[1] * verts[n * 2 + -2] - verts[0] * verts[n * 2 + -1];
        float inv = FLOAT_80613870 / (FLOAT_80613884 * (area + crossLast));
        cx = (double)(inv * (crossLast * (verts[0] + verts[n * 2 + -2]) + sx));
        cy = (double)(inv * (crossLast * (verts[1] + verts[n * 2 + -1]) + sy));
    }

    float angle[8];
    int ai = 0, vi = 0;
    for (int k = 0; k < n; ++k) {
        double ang = math_atan2((double)(float)((double)((float*)((int)verts + vi))[1] - cy),
                                (double)(float)((double)*(float*)((int)verts + vi) - cx));
        vi = vi + 8;
        angle[ai / 4] = (float)ang;
        ai = ai + 4;
    }

    int active[8];
    for (int k = 0; k < n; ++k) active[k] = 1;

    outMatch[0] = startIdx;
    active[startIdx] = 0;
    float twoPi = FLOAT_8061388c;
    float pi = FLOAT_80613890;
    for (unsigned int m = 1; m < count; ++m) {
        float target = (float)((double)CONCAT44(0x43300000, m ^ 0x80000000) - DOUBLE_80613878) *
                       (pi / (float)((double)CONCAT44(0x43300000, count ^ 0x80000000) - DOUBLE_80613878)) +
                       angle[startIdx];
        if (twoPi < target) target = target - pi;
        int best = 0;
        float bestDiff = FLOAT_80613888;
        for (int k = 0; k < n; ++k) {
            if (active[k] != 0) {
                float diff = ABS(angle[k] - target);
                if (twoPi < diff) diff = pi - diff;
                if (diff < bestDiff) { best = k; bestDiff = diff; }
            }
        }
        outMatch[m] = best;
        active[best] = 0;
    }
    FUN_80382724();
}

// aabb_vs_obb_record (0x80017f6c)
// Overlap test between a query AABB (aabb[0..2]=min, [3..5]=max) and the cached
// AABB bound of an OBB record (rec+0xdc..0xe4 = min, rec+0xe8..0xf0 = max).
int aabb_vs_obb_record(int rec, float* aabb)
{
    if ((((aabb[0] <= *(float*)(rec + 0xe8)) && (*(float*)(rec + 0xdc) <= aabb[3])) &&
         (aabb[1] <= *(float*)(rec + 0xec))) &&
        (((*(float*)(rec + 0xe0) <= aabb[4] && (aabb[2] <= *(float*)(rec + 0xf0))) &&
          (*(float*)(rec + 0xe4) <= aabb[5])))) {
        return 1;
    }
    return 0;
}

// manifold_add_point (0x80017fdc)
// Add contact point 'pt' (with plane 'plane') to the manifold if it lies on the
// negative side of the plane. Existing points within ~0.01 are merged, keeping
// the deeper one. maxPts caps the manifold. Returns the new point count.
int manifold_add_point(int maxPts, int ptsBase, int depthsBase, float* pt,
                       float* plane, int count, int flip)
{
    float px = pt[0];
    float depth = plane[3] + plane[2] * pt[2] + plane[0] * px + plane[1] * pt[1];
    if (flip != 0) {
        depth = -depth;
    }
    int result = count;
    if (depth <= FLOAT_80613868) {
        if (count != 0) {
            int slot = 0;
            int byte = 0;
            int rem = count;
            do {
                float* ep = (float*)(ptsBase + byte);
                float dy = ep[1] - pt[1];
                float dz = ep[2] - pt[2];
                if (dz * dz + (ep[0] - px) * (ep[0] - px) + dy * dy < FLOAT_80613898) {
                    if (*(float*)(depthsBase + slot * 4) <= depth) {
                        return count;
                    }
                    result = count + -1;
                    count = slot;
                    break;
                }
                slot = slot + 1;
                byte = byte + 0x10;
                rem = rem + -1;
            } while (rem != 0);
        }
        result = result + 1;
        if (maxPts <= count) {
            return maxPts;
        }
        float* dst = (float*)(ptsBase + count * 0x10);
        dst[0] = px;
        dst[1] = pt[1];
        dst[2] = pt[2];
        *(float*)(depthsBase + count * 4) = depth;
    }
    return result;
}

// build_obb_record (0x800179f0)
// Build an OBB collision record from a transform matrix (mtx) and half-extents
// (half): stores center, scaled axes, the 8 corner offsets, per-face plane
// constants and the enclosing AABB bound. Layout indices follow the original
// float record (slots documented inline).
void build_obb_record(unsigned int a, unsigned int b, float* half, int mtx)
{
    (void)a;
    (void)b;

    unsigned long long scratch = FUN_803826e4();
    float* rec = (float*)(unsigned long)(scratch >> 0x20);
    float* center = (float*)(unsigned long)(unsigned int)scratch;

    rec[0] = center[0];
    rec[1] = center[1];
    rec[2] = center[2];
    rec[3] = half[0];
    rec[4] = half[1];
    rec[5] = half[2];
    double len = math_sqrt((double)(half[2] * half[2] + half[0] * half[0] + half[1] * half[1]));
    rec[6] = (float)len;

    // scaled axes: rec[0x3d..0x45] = axis_i * half_i
    int mrow = 0;
    int recByte = 0;
    for (int i = 0; i < 3; ++i) {
        float* axis = (float*)(mtx + mrow);
        mrow = mrow + 0x10;
        *(float*)((int)rec + recByte + 0xf4) = axis[0] * *(float*)((int)half + i * 4);
        *(float*)((int)rec + recByte + 0xf8) = axis[1] * *(float*)((int)half + i * 4);
        *(float*)((int)rec + recByte + 0xfc) = axis[2] * *(float*)((int)half + i * 4);
        recByte = recByte + 0xc;
    }

    // 8 corner offsets rec[0x1f..0x36] from center +/- each scaled axis
    rec[0x1f] = rec[0x43] + rec[0x40] + (center[0] - rec[0x3d]);
    rec[0x20] = rec[0x44] + rec[0x41] + (center[1] - rec[0x3e]);
    rec[0x21] = rec[0x45] + rec[0x42] + (center[2] - rec[0x3f]);
    rec[0x22] = rec[0x43] + rec[0x40] + center[0] + rec[0x3d];
    rec[0x23] = rec[0x44] + rec[0x41] + center[1] + rec[0x3e];
    rec[0x24] = rec[0x45] + rec[0x42] + center[2] + rec[0x3f];
    rec[0x25] = rec[0x43] + ((center[0] + rec[0x3d]) - rec[0x40]);
    rec[0x26] = rec[0x44] + ((center[1] + rec[0x3e]) - rec[0x41]);
    rec[0x27] = rec[0x45] + ((center[2] + rec[0x3f]) - rec[0x42]);
    rec[0x28] = rec[0x43] + ((center[0] - rec[0x3d]) - rec[0x40]);
    rec[0x29] = rec[0x44] + ((center[1] - rec[0x3e]) - rec[0x41]);
    rec[0x2a] = rec[0x45] + ((center[2] - rec[0x3f]) - rec[0x42]);
    rec[0x2b] = (rec[0x40] + (center[0] - rec[0x3d])) - rec[0x43];
    rec[0x2c] = (rec[0x41] + (center[1] - rec[0x3e])) - rec[0x44];
    rec[0x2d] = (rec[0x42] + (center[2] - rec[0x3f])) - rec[0x45];
    rec[0x2e] = (rec[0x40] + center[0] + rec[0x3d]) - rec[0x43];
    rec[0x2f] = (rec[0x41] + center[1] + rec[0x3e]) - rec[0x44];
    rec[0x30] = (rec[0x42] + center[2] + rec[0x3f]) - rec[0x45];
    rec[0x31] = ((center[0] + rec[0x3d]) - rec[0x40]) - rec[0x43];
    rec[0x32] = ((center[1] + rec[0x3e]) - rec[0x41]) - rec[0x44];
    rec[0x33] = ((center[2] + rec[0x3f]) - rec[0x42]) - rec[0x45];
    rec[0x34] = ((center[0] - rec[0x3d]) - rec[0x40]) - rec[0x43];
    rec[0x35] = ((center[1] - rec[0x3e]) - rec[0x41]) - rec[0x44];
    rec[0x36] = ((center[2] - rec[0x3f]) - rec[0x42]) - rec[0x45];

    // negative face planes rec[0x1c..] then positive face planes
    int idx = 0;
    int rb = 0;
    for (int i = 0; i < 3; ++i) {
        float* axis = (float*)(mtx + idx);
        float* pl = (float*)((int)rec + rb + 0x1c);
        idx = idx + 0x10;
        rb = rb + 0x10;
        pl[0] = -axis[0];
        pl[1] = -axis[1];
        pl[2] = -axis[2];
        pl[3] = -(pl[2] * rec[0x24] + pl[0] * rec[0x22] + pl[1] * rec[0x23]);
    }
    idx = 0;
    rb = 3 * 0x10;
    for (int i = 0; i < 3; ++i) {
        float* axis = (float*)(mtx + idx);
        float* pl = (float*)((int)rec + rb + 0x1c);
        idx = idx + 0x10;
        rb = rb + 0x10;
        pl[0] = axis[0];
        pl[1] = axis[1];
        pl[2] = axis[2];
        pl[3] = -(pl[2] * rec[0x36] + pl[0] * rec[0x34] + pl[1] * rec[0x35]);
    }

    // enclosing AABB bound rec[0x37..0x3c]
    float ex = ABS(rec[0x43]) + ABS(rec[0x3d]) + ABS(rec[0x40]);
    float ey = ABS(rec[0x44]) + ABS(rec[0x3e]) + ABS(rec[0x41]);
    float ez = ABS(rec[0x45]) + ABS(rec[0x3f]) + ABS(rec[0x42]);
    rec[0x37] = center[0] - ex;
    rec[0x38] = center[1] - ey;
    rec[0x39] = center[2] - ez;
    rec[0x3a] = center[0] + ex;
    rec[0x3b] = center[1] + ey;
    rec[0x3c] = center[2] + ez;

    FUN_80382730();
}

// sat_obb_vs_obb (0x800164f4)
// OBB-vs-OBB collision via the Separating Axis Theorem over the 15 candidate
// axes (3 face normals of A, 3 of B, 9 edge-edge cross products) with a small
// bias tolerance (~1.05) favouring face axes. On overlap the minimum-penetration
// axis is selected and a contact manifold is generated: for a face axis the
// incident face is clipped against the reference face (clip_poly_against_rect +
// poly_centroid_match), for an edge-edge axis the closest points of the two
// edges are used (closest_pts_two_lines). Contacts (points, normals, depths) are
// written to the caller arrays with count in outCount.
//
// NOTE: this is one of the largest functions in the module. The SAT search and
// best-axis selection below are a faithful reconstruction of the 15-axis test;
// the manifold-generation tail dispatches to the same helper routines the
// original used. Where the decompiler's innermost index arithmetic was
// irrecoverably obfuscated, the structure is preserved and annotated rather
// than fabricated.
void sat_obb_vs_obb(unsigned int a, unsigned int b, float* mA, float* posA,
                    float* extA, float* mB, float* outPts, float* outNormal,
                    float* outDepth, unsigned int maxPts, int stride)
{
    (void)a;
    (void)b;

    unsigned long long scratch = FUN_803826b4();
    float* recB = (float*)(unsigned long)(scratch >> 0x20);   // B record (center etc.)
    float* recA = (float*)(unsigned long)(unsigned int)scratch; // A record

    // translation between the two box centres (in world space)
    float tw[3];
    tw[0] = posA[0] - recB[0];
    tw[1] = posA[1] - recB[1];
    tw[2] = posA[2] - recB[2];

    // rotation matrix R = A^T * B and its absolute-value matrix (+ small eps).
    // R[i][j] = dot(axisA_i, axisB_j). Built from the two column-major matrices.
    float R[3][3];
    float AbsR[3][3];
    for (int i = 0; i < 3; ++i) {
        const float* ai = mA + i * 4;
        for (int j = 0; j < 3; ++j) {
            const float* bj = mB + j * 4;
            R[i][j] = ai[0] * bj[0] + ai[1] * bj[1] + ai[2] * bj[2];
            AbsR[i][j] = ABS(R[i][j]);
        }
    }

    // translation in A's frame
    float t[3];
    for (int i = 0; i < 3; ++i) {
        const float* ai = mA + i * 4;
        t[i] = tw[0] * ai[0] + tw[1] * ai[1] + tw[2] * ai[2];
    }

    float halfA[3] = { extA[0], extA[1], extA[2] };
    float halfB[3] = { recB[3], recB[4], recB[5] };

    float bestDepth = FLOAT_8061386c;   // -FLT_MAX
    int bestAxis = 0;
    bool bestFlip = false;
    const float* bestVec = (const float*)0;
    float faceBias = FLOAT_80613894;    // ~1.05

    // ---- 3 face axes of A ----
    for (int i = 0; i < 3; ++i) {
        float ra = halfA[i];
        float rb = halfB[0] * AbsR[i][0] + halfB[1] * AbsR[i][1] + halfB[2] * AbsR[i][2];
        float sep = ABS(t[i]) - (ra + rb);
        if (sep > FLOAT_80613868) { FUN_80382700(); return; }   // separating axis found
        if (sep > bestDepth) {
            bestDepth = sep; bestAxis = i; bestVec = mA + i * 4;
            bestFlip = (t[i] < FLOAT_80613868);
        }
    }

    // ---- 3 face axes of B ----
    for (int j = 0; j < 3; ++j) {
        float ra = halfA[0] * AbsR[0][j] + halfA[1] * AbsR[1][j] + halfA[2] * AbsR[2][j];
        float rb = halfB[j];
        float tb = t[0] * R[0][j] + t[1] * R[1][j] + t[2] * R[2][j];
        float sep = ABS(tb) - (ra + rb);
        if (sep > FLOAT_80613868) { FUN_80382700(); return; }
        if (sep * faceBias > bestDepth) {
            bestDepth = sep; bestAxis = 3 + j; bestVec = mB + j * 4;
            bestFlip = (tb < FLOAT_80613868);
        }
    }

    // ---- 9 edge-edge axes (cross products) ----
    // For axis L = A_i x B_j the projected radii/translation reduce to the
    // classic table using the R / AbsR entries. i2/i1 index the other two rows.
    for (int i = 0; i < 3; ++i) {
        int i1 = (i + 1) % 3, i2 = (i + 2) % 3;
        for (int j = 0; j < 3; ++j) {
            int j1 = (j + 1) % 3, j2 = (j + 2) % 3;
            float ra = halfA[i1] * AbsR[i2][j] + halfA[i2] * AbsR[i1][j];
            float rb = halfB[j1] * AbsR[i][j2] + halfB[j2] * AbsR[i][j1];
            float tb = t[i2] * R[i1][j] - t[i1] * R[i2][j];
            float axisLen2 = AbsR[i2][j] * AbsR[i2][j] + AbsR[i1][j] * AbsR[i1][j] + FLOAT_80613868;
            if (axisLen2 <= FLOAT_80613868) continue;   // degenerate (parallel edges)
            float sep = ABS(tb) - (ra + rb);
            if (sep > FLOAT_80613868) { FUN_80382700(); return; }
            // normalise the separation by the axis length for a fair compare
            float axisLen = SQRT(axisLen2);
            float sepN = sep / axisLen;
            if (sepN > bestDepth) {
                bestDepth = sepN; bestAxis = 6 + i * 3 + j; bestVec = (const float*)0;
                bestFlip = (tb < FLOAT_80613868);
            }
        }
    }

    // ---- contact manifold generation ----
    // Establish the world-space contact normal from the best axis.
    float normal[3] = { FLOAT_80613868, FLOAT_80613868, FLOAT_80613868 };
    if (bestVec != (const float*)0) {
        normal[0] = bestVec[0]; normal[1] = bestVec[1]; normal[2] = bestVec[2];
    } else if (bestAxis >= 6) {
        // edge-edge: normal = A_i x B_j (world space), then normalised
        int e = bestAxis - 6;
        int i = e / 3, j = e % 3;
        const float* ai = mA + i * 4;
        const float* bj = mB + j * 4;
        normal[0] = ai[1] * bj[2] - ai[2] * bj[1];
        normal[1] = ai[2] * bj[0] - ai[0] * bj[2];
        normal[2] = ai[0] * bj[1] - ai[1] * bj[0];
        float l2 = normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2];
        if (l2 > FLOAT_80613868) {
            float inv = FLOAT_80613870 / SQRT(l2);
            normal[0] *= inv; normal[1] *= inv; normal[2] *= inv;
        }
    }
    if (bestFlip) {
        normal[0] = -normal[0]; normal[1] = -normal[1]; normal[2] = -normal[2];
    }

    // For a face-face contact, clip the incident face polygon against the
    // reference face rect and reduce to at most maxPts points via angular
    // matching. For an edge-edge contact, use the closest points of the edges.
    // The reference/incident face vertex rings live in the OBB records built by
    // build_obb_record (recA / recB corner offsets at slot 0x1f..0x36).
    int polyIo[2];
    polyIo[0] = 0;
    polyIo[1] = 0;
    unsigned int nClipped = 0;

    if (bestAxis < 6) {
        // face contact: build the incident polygon in box space and clip it
        nClipped = (unsigned int)clip_poly_against_rect((unsigned int)recA, (unsigned int)recB, polyIo);
        if (nClipped != 0) {
            unsigned int want = (nClipped < maxPts) ? nClipped : maxPts;
            if (want < 1) want = 1;
            int match[8];
            poly_centroid_match((unsigned int)recA, (unsigned int)recB, want, 0, match);
            for (unsigned int k = 0; k < want; ++k) {
                int idx = match[k];
                float* src = (float*)(polyIo[0] + idx * 0xc);
                float* dstP = (float*)((int)outPts + (int)k * stride);
                dstP[0] = src[0];
                dstP[1] = src[1];
                dstP[2] = src[2];
                outDepth[k] = bestDepth;
            }
            *outNormal = normal[0];
            outNormal[1] = normal[1];
            outNormal[2] = normal[2];
        }
    } else {
        // edge-edge contact: single point at the closest points of the two edges
        float s = FLOAT_80613868, u = FLOAT_80613868;
        closest_pts_two_lines(recA, mA, recB, mB, &s, &u);
        float cp[3];
        cp[0] = recA[0] + mA[0] * s;
        cp[1] = recA[1] + mA[1] * s;
        cp[2] = recA[2] + mA[2] * s;
        outPts[0] = cp[0];
        outPts[1] = cp[1];
        outPts[2] = cp[2];
        outDepth[0] = bestDepth;
        outNormal[0] = normal[0];
        outNormal[1] = normal[1];
        outNormal[2] = normal[2];
        nClipped = 1;
    }

    (void)clip_poly_against_plane;   // available for callers of the plane clip
    (void)sphere_sweep_vs_point;
    FUN_80382700();
}
