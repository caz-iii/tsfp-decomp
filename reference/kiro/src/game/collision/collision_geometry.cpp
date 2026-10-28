// =============================================================================
// collision_geometry.cpp - Low-level 3D collision geometry primitives
// Address range: 0x80011110 - 0x80011d3c
// Batch: 378 | Date: 2026-09-05
//
// Primitive intersection tests used by the collision raycast layer
// (entity_raycast_triangles @ 0x80078674, player_ai_mesh_raycast @ 0x800abe68,
//  collision_entity_raycast @ 0x80065a74):
//   * seg_vs_circle_2d          (0x80011110) segment vs 2D circle, sweep dist + normal
//   * moving_circle_vs_segment  (0x800112ac) swept circle vs point/segment
//   * swept_circle_vs_segment   (0x80011604) radius-expanded segment sweep
//   * point_in_aabb             (0x80011750) point-in-box test
//   * aabb_overlap              (0x800117cc) box-box overlap test
//   * ray_vs_aabb_slab          (0x8001183c) sign-aware slab test
//   * ray_vs_triangle           (0x80011a44) ray/triangle intersect + hit + normal
//   * ray_precompute            (0x80011d00) fill ray struct (origin,dir,inv-dir,len)
// =============================================================================
#include "common.h"
#include "collision.h"

// ---- shared numeric constants (rodata @ 0x806137b8..) --------------------
extern float FLOAT_806137d8;  // FLT_MAX (7f7fffff) - "no hit" sentinel
extern float FLOAT_806137dc;  // -1e-5   small negative epsilon
extern float FLOAT_806137e0;  // 0.0
extern float FLOAT_806137e4;  // +1e-5   epsilon (squared-length guard)
extern float FLOAT_806137e8;  // 1.0

// seg_vs_circle_2d (0x80011110)
// 2D swept-point vs circle test. Given a start point (ox,oy), a max travel
// (maxDist), a direction (dx,dy) and a circle center (cx,cy), returns the
// distance along the ray at which it first touches the circle, or FLT_MAX if
// it never does. When it hits and outNormal != NULL, the unit contact normal
// is written to outNormal[0..1].
double seg_vs_circle_2d(double ox, double oy, double maxDist,
                        double dx, double dy,
                        double cx, double cy, float* outNormal)
{
    double perp;
    double along;
    double result;
    double root;

    // perpendicular (cross) distance from circle center to the ray line
    perp = (double)(float)((double)(float)(cy - oy) * dx -
                           (double)(float)((double)(float)(cx - ox) * dy));
    if ((maxDist < perp) || (maxDist < -perp)) {
        // circle entirely off to one side further than maxDist
        return (double)FLOAT_806137d8;
    }

    // projection of center onto the ray direction
    along = (double)(float)((double)(float)(cx - ox) * dx +
                            (double)(float)((double)(float)(cy - oy) * dy));
    root = math_sqrt((double)((float)(maxDist + perp) * (float)(maxDist - perp)));
    result = (double)(float)(along - root);

    if (((double)FLOAT_806137dc < result) && (result < (double)FLOAT_806137e0)) {
        result = (double)FLOAT_806137e0;
    }

    if ((double)FLOAT_806137e0 <= result) {
        if (outNormal != (float*)0) {
            outNormal[0] = (float)(perp * dy + -(double)(float)(root * dx));
            outNormal[1] = (float)(perp * -dx + -(double)(float)(root * dy));
            root = math_sqrt((double)(outNormal[0] * outNormal[0] +
                                      outNormal[1] * outNormal[1]));
            {
                double one = (double)FLOAT_806137e8;
                outNormal[0] = outNormal[0] * (float)(one / root);
                outNormal[1] = outNormal[1] * (float)(one / root);
            }
        }
    } else {
        if (outNormal != (float*)0) {
            outNormal[0] = (float)-dx;
            outNormal[1] = (float)-dy;
        }
        if ((float)(maxDist * maxDist - (double)FLOAT_806137e4) <=
            (float)(result * result + (double)(float)(perp * perp))) {
            result = (double)FLOAT_806137d8;
        } else {
            result = (double)FLOAT_806137e0;
        }
    }
    return result;
}

// moving_circle_vs_segment (0x800112ac)
// Sweep of a circle (radius folded into 'radius') from p0 toward a segment
// defined by A=(ax,ay) and B=(bx,by). Returns fraction of travel (0..1) at
// first contact, or 1.0 if no contact within the sweep. Delegates the pure
// line case to seg_vs_circle_2d.
// NOTE: in the original, 'by' arrives in a stack slot (in_stack_00000008);
// it is a genuine incoming argument, expressed here as a real parameter.
double moving_circle_vs_segment(double p0x, double p0y, double radius,
                                double dirx, double diry,
                                double ax, double ay, double bx,
                                float by, float* outNormal)
{
    float nx;
    float ny;
    double len;
    double one;
    double relBx;
    double relBy;
    double invLen;
    double segLen2;
    double result;
    double proj;
    double perp;
    double relAx;
    double relAy;

    len = math_sqrt((double)(float)(dirx * dirx + (double)(float)(diry * diry)));
    one = (double)FLOAT_806137e8;
    if ((double)FLOAT_806137e0 == len) {
        return one;
    }
    relBx = (double)(float)((double)by - ay);
    relBy = (double)(float)(bx - ax);
    invLen = (double)(float)(one / len);
    segLen2 = (double)(float)(relBy * relBy + (double)(float)(relBx * relBx));

    if (segLen2 <= (double)FLOAT_806137e0) {
        // degenerate segment -> treat as circle at endpoint
        result = seg_vs_circle_2d(p0x, p0y, radius,
                                  (double)(float)(dirx * invLen),
                                  (double)(float)(diry * invLen), bx, (double)by, outNormal);
    } else {
        if ((double)FLOAT_806137e0 <=
            (double)(float)(relBx * dirx - (double)(float)(relBy * diry))) {
            return one;
        }
        relAy = (double)(float)(p0y - ay);
        relAx = (double)(float)(p0x - ax);
        if ((double)(float)(relBx * relAx - (double)(float)(relBy * relAy)) <
            (double)FLOAT_806137e0) {
            return one;
        }
        proj = (double)(float)(relBy * relAx + (double)(float)(relBx * relAy));
        if ((double)FLOAT_806137e0 <= proj) {
            if (proj <= segLen2) {
                len = math_sqrt(segLen2);
                nx = (float)(relBx * (double)(float)(one / len));
                perp = (double)nx;
                ny = (float)(-relBy * (double)(float)(one / len));
                result = (double)ny;
                if (outNormal != (float*)0) {
                    outNormal[0] = nx;
                    outNormal[1] = ny;
                }
                proj = (double)(float)(perp * relAx + (double)(float)(result * relAy));
                result = (double)(float)(perp * (double)(float)((double)(float)(p0x + dirx) - ax) +
                          (double)(float)(result * (double)(float)((double)(float)(p0y + diry) - ay)));
                if (proj == result) {
                    return one;
                }
                if (result <= proj) {
                    if ((radius <= result) || (proj <= -radius)) {
                        return one;
                    }
                    if (proj < radius) {
                        return (double)FLOAT_806137e0;
                    }
                    result = (double)(float)(proj - radius);
                } else {
                    if ((radius <= proj) || (result <= -radius)) {
                        return one;
                    }
                    if (-radius < proj) {
                        return (double)FLOAT_806137e0;
                    }
                    result = (double)(float)(-proj - radius);
                }
            } else {
                result = seg_vs_circle_2d(p0x, p0y, radius,
                                          (double)(float)(dirx * invLen),
                                          (double)(float)(diry * invLen), bx, (double)by, outNormal);
            }
        } else {
            result = seg_vs_circle_2d(p0x, p0y, radius,
                                      (double)(float)(dirx * invLen),
                                      (double)(float)(diry * invLen), ax, ay, outNormal);
        }
    }

    if (result < len) {
        len = (double)FLOAT_806137e0;
        if (len < result) {
            len = (double)(float)(result * invLen);
        }
    } else {
        len = (double)FLOAT_806137e8;
    }
    return len;
}

// swept_circle_vs_segment (0x80011604)
// Segment sweep with the circle radius added to the perpendicular tolerance,
// normalizing the direction first, then delegating to seg_vs_circle_2d.
double swept_circle_vs_segment(double ox, double oy, double tol,
                               double dx, double dy,
                               double cx, double cy, double radius,
                               float* outNormal)
{
    double len;
    double hit;
    double invLen;

    len = math_sqrt((double)(float)(dx * dx + (double)(float)(dy * dy)));
    invLen = (double)(float)((double)FLOAT_806137e8 / len);
    hit = seg_vs_circle_2d(ox, oy, (double)(float)(tol + radius),
                           (double)(float)(dx * invLen),
                           (double)(float)(dy * invLen), cx, cy, outNormal);
    if (hit < len) {
        len = (double)FLOAT_806137e0;
        if (len < hit) {
            len = (double)(float)(hit * invLen);
        }
    } else {
        len = (double)FLOAT_806137e8;
    }
    return len;
}

// point_in_aabb (0x80011750)
// True if point p is inside the axis-aligned box [lo, hi] (inclusive).
int point_in_aabb(float* p, float* lo, float* hi)
{
    if ((lo[0] <= p[0]) && (p[0] <= hi[0])) {
        if ((lo[1] <= p[1]) && (p[1] <= hi[1])) {
            if ((lo[2] <= p[2]) && (p[2] <= hi[2])) {
                return 1;
            }
        }
    }
    return 0;
}

// aabb_overlap (0x800117cc)
// True if box A [aMin,aMax] overlaps box B [bMin,bMax] on all three axes.
int aabb_overlap(float* aMin, float* aMax, float* bMin, float* bMax)
{
    if ((((aMin[0] <= bMax[0]) && (bMin[0] <= aMax[0])) && (aMin[1] <= bMax[1])) &&
        (((bMin[1] <= aMax[1]) && (aMin[2] <= bMax[2])) && (bMin[2] <= aMax[2]))) {
        return 1;
    }
    return 0;
}

// ray_vs_aabb_slab (0x8001183c)
// Sign-aware ray/segment vs AABB slab test. 'origin' is the ray origin, 'dir'
// the ray direction, and (boxMin,boxMax) the box corners. Returns 1 on hit.
int ray_vs_aabb_slab(float* origin, float* dir, float* boxMin, float* boxMax)
{
    float ax;
    float ay;
    float az;
    float tMinX;
    float tMaxX;
    float tMinY;
    float tMaxY;
    float tMinZ;
    float tMaxZ;
    float swap;

    // X slab
    ax = dir[0];
    if (FLOAT_806137e0 <= ax) {
        tMaxX = boxMax[0] - origin[0];
        tMinX = boxMin[0] - origin[0];
    } else {
        ax = -ax;
        tMaxX = origin[0] - boxMax[0];
        tMinX = origin[0] - boxMin[0];
    }
    if ((tMinX < FLOAT_806137e0) && (tMaxX < FLOAT_806137e0)) {
        return 0;
    }

    // Y slab
    ay = dir[1];
    if (FLOAT_806137e0 <= ay) {
        tMaxY = boxMax[1] - origin[1];
        tMinY = boxMin[1] - origin[1];
    } else {
        ay = -ay;
        tMaxY = origin[1] - boxMax[1];
        tMinY = origin[1] - boxMin[1];
    }
    if ((tMinY < FLOAT_806137e0) && (tMaxY < FLOAT_806137e0)) {
        return 0;
    }

    // order X near/far
    swap = tMaxX;
    if (tMaxX < tMinX) {
        swap = tMinX;
        tMinX = tMaxX;
    }
    // order Y near/far
    tMaxX = tMinY;
    if (tMaxY < tMinY) {
        tMaxX = tMaxY;
        tMaxY = tMinY;
    }

    if (tMinX * ay <= tMaxX * ax) {
        ay = ay;              // keep ay as active X-cross scale
        tMinX = tMaxX;
        if (swap * ay < tMaxX * ax) {
            return 0;
        }
    } else {
        ay = ax;
        if (tMaxY * ax < tMinX * ay) {
            return 0;
        }
    }

    // Z slab
    az = dir[2];
    if (FLOAT_806137e0 <= az) {
        tMaxZ = boxMax[2] - origin[2];
        tMinZ = boxMin[2] - origin[2];
    } else {
        az = -az;
        tMaxZ = origin[2] - boxMax[2];
        tMinZ = origin[2] - boxMin[2];
    }
    if ((tMinZ < FLOAT_806137e0) && (tMaxZ < FLOAT_806137e0)) {
        return 0;
    }
    swap = tMinZ;
    if (tMaxZ < tMinZ) {
        swap = tMaxZ;
        tMaxZ = tMinZ;
    }

    if (swap * ay <= tMinX * az) {
        if (tMaxZ * ay < tMinX * az) {
            return 0;
        }
    } else {
        if (tMaxY * ay < tMaxX * ax) {  // reconcile X/Y extents before Z
            ay = ax;
            tMaxY = tMaxX;
        }
        if (tMaxY * az < swap * ay) {
            return 0;
        }
    }
    return 1;
}

// ray_vs_triangle (0x80011a44)
// Ray/triangle intersection. origin+dir define the ray; v0,v1,v2 the triangle.
// On hit, writes the contact point to outHit[0..2] and the (unnormalized) face
// normal to outNormal[0..2]; returns 1. Returns 0 on miss or parallel ray.
int ray_vs_triangle(float* origin, float* dir, float* v0, float* v1, float* v2,
                    float* outHit, float* outNormal)
{
    float v1x;
    float v0x;
    float v0z;
    float v2z;
    float e01x;
    float v1z;
    float v2x;
    float v0y;
    float e01z;
    float v2y;
    float v1y;
    float e01y;
    float nx;
    float ny;
    float nz;
    float denom;
    float t;
    float hx;
    float hy;
    float hz;
    float nnx;
    float nny;
    float nnz;
    int result;

    v1x = v1[0];
    v0x = v0[0];
    v0z = v0[2];
    v2z = v2[2];
    e01x = v1x - v0x;
    v1z = v1[2];
    v2x = v2[0];
    v0y = v0[1];
    e01z = v1z - v0z;
    v2y = v2[1];
    v1y = v1[1];
    e01y = v1y - v0y;

    nx = e01z * (v2x - v0x) - e01x * (v2z - v0z);
    ny = e01y * (v2z - v0z) - e01z * (v2y - v0y);
    nz = e01x * (v2y - v0y) - e01y * (v2x - v0x);

    denom = nz * dir[2] + ny * dir[0] + nx * dir[1];
    if (FLOAT_806137e0 == denom) {
        result = 0;
    } else {
        t = ((nz * v0z + ny * v0x + nx * v0y) -
             (nz * origin[2] + ny * origin[0] + nx * origin[1])) / denom;
        if (FLOAT_806137e0 <= t) {
            hz = t * dir[2] + origin[2];
            hx = t * dir[0] + origin[0];
            hy = t * dir[1] + origin[1];
            nnx = e01z * (v1x - hx) - e01x * (v1z - hz);
            e01z = e01y * (v1z - hz) - e01z * (v1y - hy);
            e01x = e01x * (v1y - hy) - e01y * (v1x - hx);
            if (FLOAT_806137e0 <=
                e01x * ((v2x - v1x) * (v2y - hy) - (v2y - v1y) * (v2x - hx)) +
                e01z * ((v2y - v1y) * (v2z - hz) - (v2z - v1z) * (v2y - hy)) +
                nnx * ((v2z - v1z) * (v2x - hx) - (v2x - v1x) * (v2z - hz))) {
                if (FLOAT_806137e0 <=
                    e01x * ((v0x - v2x) * (v0y - hy) - (v0y - v2y) * (v0x - hx)) +
                    e01z * ((v0y - v2y) * (v0z - hz) - (v0z - v2z) * (v0y - hy)) +
                    nnx * ((v0z - v2z) * (v0x - hx) - (v0x - v2x) * (v0z - hz))) {
                    if (outHit != (float*)0) {
                        outHit[0] = hx;
                        outHit[1] = hy;
                        outHit[2] = hz;
                    }
                    if (outNormal != (float*)0) {
                        outNormal[0] = ny;
                        outNormal[1] = nx;
                        outNormal[2] = nz;
                    }
                    result = 1;
                } else {
                    result = 0;
                }
            } else {
                result = 0;
            }
        } else {
            result = 0;
        }
    }
    return result;
}

// ray_precompute (0x80011d00)
// Fill a 10-slot ray/segment record: origin(3), dir(3), inverse-dir(3, 1/d per
// axis with 0 guarded), and the caller-supplied length in slot 9.
void ray_precompute(double length, float* outRay, float* origin, float* dir)
{
    float inv;

    outRay[0] = origin[0];
    outRay[1] = origin[1];
    outRay[2] = origin[2];
    outRay[3] = dir[0];
    outRay[4] = dir[1];
    outRay[5] = dir[2];
    outRay[9] = (float)length;

    inv = FLOAT_806137e0;
    if (FLOAT_806137e0 != dir[0]) {
        inv = FLOAT_806137e8 / dir[0];
    }
    outRay[6] = inv;

    inv = FLOAT_806137e0;
    if (FLOAT_806137e0 != dir[1]) {
        inv = FLOAT_806137e8 / dir[1];
    }
    outRay[7] = inv;

    inv = FLOAT_806137e0;
    if (FLOAT_806137e0 != dir[2]) {
        inv = FLOAT_806137e8 / dir[2];
    }
    outRay[8] = inv;
}

// =============================================================================
// Batch 379: extended collision-geometry set (0x80011d98 - 0x800136cc)
// Continuation of the primitives above. Callers: collision_bvh_raycast
// (0x80065504), collision_query, collision_entity_raycast.
// =============================================================================

// ---- additional numeric constants (rodata @ 0x806137ec..0x80613820) -------
extern float  FLOAT_806137ec;  // 127.0
extern float  FLOAT_806137f0;  // 1/256 (0.00390625)
extern float  FLOAT_806137f4;  // 1024.0
extern double DOUBLE_806137f8;  // uint->double conversion magic bias
extern float  FLOAT_80613800;  // ~1e-7 squared-length epsilon (degenerate guard)
extern float  FLOAT_80613804;  // 0.5   (Newton-Raphson rsqrt; also midpoint)
extern float  FLOAT_80613808;  // 1.5   (Newton-Raphson rsqrt)
extern float  FLOAT_8061380c;  // ~1e-6 squared-length epsilon
extern float  FLOAT_80613810;  // ~1.0  near-parallel threshold
extern float  DAT_80613814;    // 1.0   (work init)
extern float  DAT_80613818;    // 1.0   (work init)
extern float  FLOAT_8061381c;  // 2.0
extern double DOUBLE_80613820;  // ~1e-7 determinant epsilon

// ---- scratch work-buffer alloc/free pairs (return packed hi:lo pointers) --
// Each alloc returns two related pointers packed into a u64 (hi 32 = table A,
// lo 32 = table B / work vector); the matching free pops the scratch frame.
extern "C" unsigned long long FUN_803826cc(void);
extern "C" void               FUN_80382718(void);
extern "C" unsigned long long FUN_803826f0(void);
extern "C" void               FUN_8038273c(void);
extern "C" unsigned long long FUN_803826e8(void);
extern "C" void               FUN_80382734(void);

// ray_vs_aabb_inv (0x80011d98)
// Ray/AABB test using a precomputed ray record (see ray_precompute): slots
// 3..5 = direction, 6..8 = inverse direction, 9 = length; origin in 0..2.
// box layout: [0..2] = min, [3..5] = max. Returns 1 if the ray's [0,len]
// span intersects the box on all three axes.
int ray_vs_aabb_inv(float* ray, float* box)
{
    float tNear;
    float tFar;
    float t0;
    float t1;
    float s0;
    float s1;

    // X axis
    if (FLOAT_806137e0 == ray[3]) {
        if ((ray[0] < box[0]) || (box[3] < ray[0])) {
            return 0;
        }
        tFar = ray[9];
        tNear = FLOAT_806137e0;
    } else {
        tNear = ray[6] * (box[0] - ray[0]);
        t0 = ray[6] * (box[3] - ray[0]);
        tFar = t0;
        if (t0 < tNear) {
            tFar = tNear;
            tNear = t0;
        }
        if (tFar < FLOAT_806137e0) {
            return 0;
        }
        if (ray[9] < tNear) {
            return 0;
        }
    }

    // Z axis (slot 5 / inv slot 8, box .2/.5)
    if (FLOAT_806137e0 == ray[5]) {
        if ((ray[2] < box[2]) || (box[5] < ray[2])) {
            return 0;
        }
    } else {
        s0 = ray[8] * (box[2] - ray[2]);
        s1 = ray[8] * (box[5] - ray[2]);
        t0 = s1;
        if (s1 < s0) {
            t0 = s0;
            s0 = s1;
        }
        if ((t0 < tFar) && (tFar = t0, t0 < FLOAT_806137e0)) {
            return 0;
        }
        if ((tNear < s0) && (tNear = s0, ray[9] < s0)) {
            return 0;
        }
        if (tFar < tNear) {
            return 0;
        }
    }

    // Y axis (slot 4 / inv slot 7, box .1/.4)
    if (FLOAT_806137e0 == ray[4]) {
        if ((ray[1] < box[1]) || (box[4] < ray[1])) {
            return 0;
        }
    } else {
        s0 = ray[7] * (box[1] - ray[1]);
        s1 = ray[7] * (box[4] - ray[1]);
        t0 = s1;
        if (s1 < s0) {
            t0 = s0;
            s0 = s1;
        }
        if ((t0 < tFar) && (tFar = t0, t0 < FLOAT_806137e0)) {
            return 0;
        }
        if ((tNear < s0) && (tNear = s0, ray[9] < s0)) {
            return 0;
        }
        if (tFar < tNear) {
            return 0;
        }
    }
    (void)t1;
    return 1;
}

// mesh_raycast_face (0x80011fac)
// Ray vs a single mesh polygon. Two vertex encodings are supported: when
// 'packed' == 0 the polygon indexes an array of float[3] vertices; otherwise
// it indexes an array of packed short[3] vertices that are de-quantized using
// the mesh origin (basePos) and a fixed 1/256 scale. On a closer front-facing
// hit the ray record's distance slot (ray[9]) is updated and, if provided, the
// hit point (outHit) and face barycentric direction (outDir) are written.
//   rayRec   : precomputed ray record (float[10])
//   meshHdr  : mesh header; +0x2c = index-table base
//   basePos  : mesh origin (float[3]) used for the packed decode
//   packed   : 0 = float verts, else packed-short verts
//   face     : polygon record (count at [0], flags byte at [4], idx base at +8)
//   cullBackface : if nonzero, reject back faces
//   outHit   : optional hit point out (int* raw store, matches original)
//   outDir   : optional direction out (float[3])
void mesh_raycast_face(float* rayRec, int meshHdr, float* basePos, int packed,
                       char* face, int cullBackface, int outHit, float* outDir)
{
    unsigned char flags;
    float* pA;
    float* pB;
    short* sVtx;
    short* sCur;
    float rx, ry, rz;
    float t;
    float hu, hv;
    unsigned int au, av;
    int vcount;
    int idxBase;
    int iter;
    int vertBaseFloat;
    int vertBaseShort;
    bool sidePos;
    bool sideNeg;
    unsigned long long scratch;
    float* rec;
    int rawBase;

    scratch = FUN_803826cc();
    rec = (float*)(unsigned long)(scratch >> 0x20);
    rawBase = (int)(unsigned int)scratch;

    idxBase = *(int*)(meshHdr + 0x2c) + (unsigned int)*(unsigned short*)(face + 8) * 2;

    if (packed == 0) {
        rx = (float)((double)CONCAT44(0x43300000, (int)face[2] ^ 0x80000000) - DOUBLE_806137f8) / FLOAT_806137ec;
        ry = (float)((double)CONCAT44(0x43300000, (int)face[1] ^ 0x80000000) - DOUBLE_806137f8) / FLOAT_806137ec;
        rz = (float)((double)CONCAT44(0x43300000, (int)face[3] ^ 0x80000000) - DOUBLE_806137f8) / FLOAT_806137ec;
        t = rz * rec[5] + ry * rec[3] + rx * rec[4];
        if (((cullBackface == 0) || (t < FLOAT_806137e0)) && (FLOAT_806137e0 != t)) {
            vcount = (int)face[0];
            pB = (float*)(rawBase + *(short*)(idxBase + vcount * 2 + -2) * 0xc);
            t = ((rz * pB[2] + ry * pB[0] + rx * pB[1]) -
                 (rz * rec[2] + ry * rec[0] + rx * rec[1])) / t;
            if ((FLOAT_806137e0 <= t) && (t <= rec[9])) {
                au = (unsigned char)face[4] >> 4 & 0xc;
                av = (unsigned char)face[4] >> 2 & 0xc;
                hv = t * *(float*)((int)rec + au + 0xc) + *(float*)((int)rec + au);
                hu = t * *(float*)((int)rec + av + 0xc) + *(float*)((int)rec + av);
                sidePos = true;
                sideNeg = true;
                iter = 0;
                if (0 < vcount) {
                    do {
                        pA = (float*)(av + (int)pB);
                        pB = (float*)(au + (int)pB);
                        pB = (float*)(rawBase + *(short*)(idxBase + iter) * 0xc);
                        float cross = (hv - *pB) * (*(float*)(av + (int)pB) - *pA) +
                                      (hu - *pA) * (*pB - *(float*)(au + (int)pB));
                        if (FLOAT_806137e0 < cross) {
                            sideNeg = false;
                        } else {
                            sidePos = false;
                        }
                        if ((!sideNeg) && (!sidePos)) { goto done; }
                        // when culling backfaces, a positive cross alone rejects
                        if (cullBackface != 0 && FLOAT_806137e0 < cross) { goto done; }
                        iter = iter + 2;
                        vcount = vcount + -1;
                    } while (vcount != 0);
                }
                rec[9] = t;
                if (outHit != 0) {
                    flags = (unsigned char)face[4];
                    *(float*)(outHit + au) = hv;
                    au = flags & 0xc;
                    *(float*)(outHit + av) = hu;
                    *(float*)(outHit + au) =
                        t * *(float*)((int)rec + au + 0xc) + *(float*)((int)rec + au);
                }
                if (outDir != (float*)0) {
                    outDir[0] = ry;
                    outDir[1] = rx;
                    outDir[2] = rz;
                }
            }
        }
    } else {
        rx = (float)((double)CONCAT44(0x43300000, (int)face[2] ^ 0x80000000) - DOUBLE_806137f8) / FLOAT_806137ec;
        ry = (float)((double)CONCAT44(0x43300000, (int)face[1] ^ 0x80000000) - DOUBLE_806137f8) / FLOAT_806137ec;
        rz = (float)((double)CONCAT44(0x43300000, (int)face[3] ^ 0x80000000) - DOUBLE_806137f8) / FLOAT_806137ec;
        t = rz * rec[5] + ry * rec[3] + rx * rec[4];
        if (((cullBackface == 0) || (t < FLOAT_806137e0)) && (FLOAT_806137e0 != t)) {
            vcount = (int)face[0];
            sVtx = (short*)(rawBase + *(short*)(idxBase + vcount * 2 + -2) * 6);
            {
                float vx = FLOAT_806137f0 * (float)((double)CONCAT44(0x43300000, (int)sVtx[0] ^ 0x80000000) - DOUBLE_806137f8) + basePos[0];
                float vy = FLOAT_806137f0 * (float)((double)CONCAT44(0x43300000, (int)sVtx[1] ^ 0x80000000) - DOUBLE_806137f8) + basePos[1];
                float vz = FLOAT_806137f0 * (float)((double)CONCAT44(0x43300000, (int)sVtx[2] ^ 0x80000000) - DOUBLE_806137f8) + basePos[2];
                t = ((rz * vz + ry * vx + rx * vy) -
                     (rz * rec[2] + ry * rec[0] + rx * rec[1])) / t;
            }
            if ((FLOAT_806137e0 <= t) && (t <= rec[9])) {
                flags = (unsigned char)face[4];
                au = flags >> 4 & 0xc;
                av = flags >> 2 & 0xc;
                unsigned int qa = flags >> 6;
                unsigned int qb = flags >> 4 & 3;
                hv = t * *(float*)((int)rec + au + 0xc) + *(float*)((int)rec + au);
                hu = t * *(float*)((int)rec + av + 0xc) + *(float*)((int)rec + av);
                int iu = (int)(short)(int)(FLOAT_806137f4 * (hv - *(float*)((int)basePos + au)));
                int iv = (int)(short)(int)(FLOAT_806137f4 * (hu - *(float*)((int)basePos + av)));
                sidePos = true;
                sideNeg = true;
                iter = 0;
                if (0 < vcount) {
                    do {
                        short* qA = sVtx + qb;
                        short* qB = sVtx + qa;
                        sVtx = (short*)(rawBase + *(short*)(idxBase + iter) * 6);
                        int cross = (iu - *qB) * ((int)sVtx[qb] - (int)*qA) +
                                    (iv - *qA) * ((int)*qB - (int)sVtx[qa]);
                        unsigned int sgn = (unsigned int)(-cross) & (unsigned int)(~cross);
                        if ((int)sgn < 0) { sideNeg = false; }
                        if (-1 < (int)sgn) { sidePos = false; }
                        if ((!sideNeg) && (!sidePos)) { goto done; }
                        if (cullBackface != 0 && 0 < cross) { goto done; }
                        iter = iter + 2;
                        vcount = vcount + -1;
                    } while (vcount != 0);
                }
                rec[9] = t;
                if (outHit != 0) {
                    flags = (unsigned char)face[4];
                    *(float*)(outHit + au) = hv;
                    au = flags & 0xc;
                    *(float*)(outHit + av) = hu;
                    *(float*)(outHit + au) =
                        t * *(float*)((int)rec + au + 0xc) + *(float*)((int)rec + au);
                }
                if (outDir != (float*)0) {
                    outDir[0] = ry;
                    outDir[1] = rx;
                    outDir[2] = rz;
                }
                (void)iv;
            }
        }
    }
done:
    FUN_80382718();
}

// mesh_face_point_inside (0x800125e0)
// Point-in-polygon edge-sign test over a mesh face (same vertex encodings as
// mesh_raycast_face). Walks the polygon edges; a positive cross-product on any
// edge means the projected point 'refPoint' lies outside that edge. Returns 1
// if the point is inside every edge (loop completed), 0 if outside (early
// break). The projected point is supplied via the first argument (threaded
// through the scratch frame in the original: rawHi holds it).
int mesh_face_point_inside(int refPoint, int unusedB, int meshHdr, int refPos,
                           int packed, char* face)
{
    float* eA;
    float* eB;
    unsigned int qa;
    unsigned int qb;
    int idxBase;
    int rawBase;
    int rawHi;
    int vcount;
    int iter;
    int cur;
    unsigned long long scratch;
    bool inside = true;

    (void)unusedB;

    scratch = FUN_803826f0();
    // scratch high word normally carries the query point; callers also pass it
    // explicitly as refPoint. Prefer the explicit argument when non-null.
    rawHi = (refPoint != 0) ? refPoint : (int)(unsigned long)(scratch >> 0x20);
    rawBase = (int)(unsigned int)scratch;
    qa = (unsigned int)((unsigned char)face[4] >> 6);
    qb = (unsigned char)face[4] >> 4 & 3;
    idxBase = *(int*)(meshHdr + 0x2c) + (unsigned int)*(unsigned short*)(face + 8) * 2;

    if (packed == 0) {
        int offA = qa * 4;
        int offB = qb * 4;
        vcount = (int)face[0];
        iter = 0;
        cur = rawBase + *(short*)(idxBase + vcount * 2 + -2) * 0xc;
        if (0 < vcount) {
            do {
                eA = (float*)(offB + cur);
                eB = (float*)(offA + cur);
                cur = rawBase + *(short*)(idxBase + iter) * 0xc;
                if (FLOAT_806137e0 <
                    (*(float*)(rawHi + offA) - *eB) * (*(float*)(offB + cur) - *eA) +
                    (*(float*)(rawHi + offB) - *eA) * (*eB - *(float*)(offA + cur))) {
                    inside = false;
                    break;
                }
                iter = iter + 2;
                vcount = vcount + -1;
            } while (vcount != 0);
        }
    } else {
        int iter2 = 0;
        vcount = (int)face[0];
        cur = rawBase + *(short*)(idxBase + vcount * 2 + -2) * 6;
        if (0 < vcount) {
            do {
                int cvB = (int)*(short*)(qb * 2 + cur);
                int cvA = (int)*(short*)(qa * 2 + cur);
                cur = rawBase + *(short*)(idxBase + iter2) * 6;
                if (0 < ((short)(int)(FLOAT_806137f4 *
                                      (*(float*)(rawHi + qa * 4) - *(float*)(refPos + qa * 4))) - cvA) *
                            (*(short*)(qb * 2 + cur) - cvB) +
                        ((short)(int)(FLOAT_806137f4 *
                                      (*(float*)(rawHi + qb * 4) - *(float*)(refPos + qb * 4))) - cvB) *
                            (cvA - *(short*)(qa * 2 + cur))) {
                    inside = false;
                    break;
                }
                iter2 = iter2 + 2;
                vcount = vcount + -1;
            } while (vcount != 0);
        }
    }
    FUN_8038273c();
    return inside ? 1 : 0;
}

// poly_point_inside_ring (0x80012790)
// Point-in-polygon test over a convex vertex ring stored inside an object:
// vertex count at obj+0x2a (u16), axis-index bytes at obj+0x3c / obj+0x3d,
// vertices at obj+0x40 with stride 0x1c. 'refPos' is the projected 2D point.
// Returns 1 if inside (no edge cross is positive), else 0.
int poly_point_inside_ring(int refPos, int obj)
{
    int axisU;
    int axisV;
    int prev;
    int cur;
    unsigned int count;
    int iter;

    count = (unsigned int)*(unsigned short*)(obj + 0x2a);
    iter = 0;
    axisU = *(char*)(obj + 0x3c) * 4;
    axisV = *(char*)(obj + 0x3d) * 4;
    prev = obj + (count - 1) * 0x1c + 0x40;
    while (true) {
        if (count == 0) {
            return 1;
        }
        cur = obj + iter + 0x40;
        if (FLOAT_806137e0 <
            (*(float*)(refPos + axisU) - *(float*)(axisU + prev)) *
                (*(float*)(axisV + cur) - *(float*)(axisV + prev)) +
            (*(float*)(refPos + axisV) - *(float*)(axisV + prev)) *
                (*(float*)(axisU + prev) - *(float*)(axisU + cur))) {
            break;
        }
        iter = iter + 0x1c;
        count = count - 1;
        prev = cur;
    }
    return 0;
}

// plane_project_normalize (0x8001282c)
// Project the direction implied by (segB - segC) onto the plane spanned by
// basisU (n1) and basisV (n2), forming a tangent vector into 'out', then
// normalize it using a fast reciprocal-sqrt with one Newton-Raphson refine
// (constants 0.5 / 1.5). No-op if the projected vector is degenerate.
void plane_project_normalize(float* out, float* segB, float* segC,
                             float* basisV, float* basisU)
{
    float uy = basisU[1];
    float vy = basisV[1];
    float uz = basisU[2];
    float vz = basisV[2];
    float projU = (segB[2] - segC[2]) * uz + (segB[0] - segC[0]) * basisU[0] + (segB[1] - segC[1]) * uy;
    float projV = (segB[2] - segC[2]) * vz + (segB[0] - segC[0]) * basisV[0] + (segB[1] - segC[1]) * vy;

    out[0] = projV * basisV[0] + projU * basisU[0];
    out[1] = projV * vy + projU * uy;
    out[2] = projV * vz + projU * uz;

    float ox = out[0];
    float len2 = out[2] * out[2] + ox * ox + out[1] * out[1];
    if (len2 <= FLOAT_80613800) {
        return;
    }
    float r = 1.0f / SQRT(len2);
    r = r * -(FLOAT_80613804 * len2 * r * r - FLOAT_80613808);
    float scale = FLOAT_806137e8 * r * -(FLOAT_80613804 * len2 * r * r - FLOAT_80613808);
    out[0] = ox * scale;
    out[1] = out[1] * scale;
    out[2] = out[2] * scale;
}

// swept_sphere_vs_cylinder (0x80012924)
// Sweep of a sphere of the given radius (radiusArg) along a ray against an
// infinite cylinder/segment axis. Builds an orthonormal basis from the axis
// direction (axisDir), transforms origin/target into that frame, solves the
// resulting quadratic (up to two roots) and, for each valid root in
// [tMin,tMax], writes the parametric t (outT[]), hit point (outHit[]) and
// surface normal (outNormal[]) via plane_project_normalize. maxDistSq bounds
// the perpendicular acceptance.
void swept_sphere_vs_cylinder(unsigned int origHandle, double radiusArg,
                              int scratchHi, float* axisDir,
                              float* segPoint, float* outT, float* outHit,
                              float* outNormal, float* outCount)
{
    (void)origHandle;
    (void)scratchHi;

    // maxDistSq arrives in an incoming FPR (radius-of-influence squared).
    double maxDistSq = (double)(FLOAT_806137e8 * FLOAT_806137e8);
    float basisU[3];
    float basisV[3];
    float axis[3];
    float* rayDir;   // caller's ray direction (frame origin dir)
    float* rayOrg;   // caller's ray origin

    // In the original these two pointers come packed in a scratch handle; here
    // they are the transform inputs supplied by the caller via axisDir/segPoint
    // adjacency. Model them as the axis-frame direction and origin.
    rayDir = axisDir;
    rayOrg = segPoint;

    axis[0] = axisDir[0];
    axis[1] = axisDir[1];
    axis[2] = axisDir[2];

    double halfLen = (double)(float)((double)FLOAT_80613804 * radiusArg);
    double zero = (double)FLOAT_806137e0;

    vec3_build_basis_from_dir(basisV, basisU, axis, 0);

    float dx = rayDir[0];
    float dy = rayDir[1];
    float dz = rayDir[2];
    double dU = (double)(basisU[1] * dz + basisU[0] * dx + basisU[2] * dy);
    double dV = (double)(basisV[1] * dz + basisV[0] * dx + basisV[2] * dy);
    double dAxis = (double)(axis[2] * dz + axis[0] * dx + axis[1] * dy);
    double dLen2 = (double)(float)(dAxis * dAxis + (double)(float)(dV * dV + (double)(float)(dU * dU)));
    if ((double)FLOAT_8061380c < dLen2) {
        double inv = math_sqrt(dLen2);
        double s = (double)(float)((double)FLOAT_806137e8 / inv);
        dV = (double)(float)(dV * s);
        dU = (double)(float)(dU * s);
        dAxis = (double)(float)(dAxis * s);
        zero = s;
    }

    float ox = rayOrg[0];
    float oy = rayOrg[1];
    float oz = rayOrg[2];
    float rx = ox - segPoint[0];
    float ry = oy - segPoint[1];
    float rz = oz - segPoint[2];
    double rV = (double)(basisV[1] * rz + basisV[0] * rx + basisV[2] * ry);
    double rU = (double)(basisU[1] * rz + basisU[0] * rx + basisU[2] * ry);
    double rAxis = (double)(axis[2] * rz + axis[0] * rx + axis[1] * ry);

    unsigned int hitCount = 0;

    if (ABS(dAxis) < (double)FLOAT_80613810) {
        if ((double)FLOAT_8061380c < ABS(dAxis)) {
            double one = (double)FLOAT_806137e8;
            double t0 = (double)((float)(one / dAxis) * (float)(halfLen - rAxis));
            float pu = (float)(t0 * dU + rU);
            float pv = (float)(t0 * dV + rV);
            bool ok0 = (double)(pv * pv + pu * pu) <= maxDistSq;
            if (ok0) {
                outNormal[0] = axis[0];
                outNormal[1] = axis[1];
                outNormal[2] = axis[2];
                outT[0] = (float)(t0 * zero);
                outHit[0] = rayDir[0] * outT[0] + ox;
                outHit[1] = rayDir[1] * outT[0] + oy;
                outHit[2] = rayDir[2] * outT[0] + oz;
            }
            hitCount = (unsigned int)ok0;
            double t1 = (double)((float)(one / dAxis) * (float)(-halfLen - rAxis));
            pu = (float)(t1 * dU + rU);
            pv = (float)(t1 * dV + rV);
            if ((double)(pv * pv + pu * pu) <= maxDistSq) {
                outNormal[hitCount * 3] = -axis[0];
                outNormal[hitCount * 3 + 1] = -axis[1];
                outNormal[hitCount * 3 + 2] = -axis[2];
                outT[hitCount] = (float)(t1 * zero);
                outHit[hitCount * 3] = rayDir[0] * outT[hitCount] + ox;
                outHit[hitCount * 3 + 1] = rayDir[1] * outT[hitCount] + oy;
                outHit[hitCount * 3 + 2] = rayDir[2] * outT[hitCount] + oz;
                hitCount = hitCount + 1;
            }
            if (hitCount != 2) {
                double a = (double)(float)(dV * dV + (double)(float)(dU * dU));
                double b = (double)(float)(rU * dV + (double)(float)(rV * dU));
                double disc = (double)(float)(b * b -
                    (double)(float)(a * (double)(float)((double)(float)(rU * rU + (double)(float)(rV * rV)) - maxDistSq)));
                if ((double)FLOAT_806137e0 <= disc) {
                    if (disc <= (double)FLOAT_806137e0) {
                        double tc = (double)(float)(-b / a);
                        if ((t1 < t0 && t1 <= tc && tc <= t0) || (t0 <= t1 && t0 <= tc && tc <= t1)) {
                            outT[hitCount] = (float)(tc * zero);
                            float* ph = outHit + hitCount * 3;
                            ph[0] = rayDir[0] * outT[hitCount] + ox;
                            ph[1] = rayDir[1] * outT[hitCount] + oy;
                            ph[2] = rayDir[2] * outT[hitCount] + oz;
                            plane_project_normalize(outNormal + hitCount * 3, ph, segPoint, basisV, basisU);
                        }
                    } else {
                        double sq = math_sqrt(disc);
                        double nb = -b;
                        double ia = (double)(float)((double)FLOAT_806137e8 / a);
                        double tc = (double)(float)(ia * (double)(float)(nb - sq));
                        if ((t1 < t0 && t1 <= tc && tc <= t0) || (t0 <= t1 && t0 <= tc && tc <= t1)) {
                            outT[hitCount] = (float)(tc * zero);
                            float* ph = outHit + hitCount * 3;
                            ph[0] = rayDir[0] * outT[hitCount] + ox;
                            ph[1] = rayDir[1] * outT[hitCount] + oy;
                            ph[2] = rayDir[2] * outT[hitCount] + oz;
                            plane_project_normalize(outNormal + hitCount * 3, ph, segPoint, basisV, basisU);
                            hitCount = hitCount + 1;
                        }
                        if (hitCount != 2) {
                            tc = (double)(float)(ia * (double)(float)(nb + sq));
                            if ((t1 < t0 && t1 <= tc && tc <= t0) || (t0 <= t1 && t0 <= tc && tc <= t1)) {
                                outT[hitCount] = (float)(tc * zero);
                                float* ph = outHit + hitCount * 3;
                                ph[0] = rayDir[0] * outT[hitCount] + ox;
                                ph[1] = rayDir[1] * outT[hitCount] + oy;
                                ph[2] = rayDir[2] * outT[hitCount] + oz;
                                plane_project_normalize(outNormal + hitCount * 3, ph, segPoint, basisV, basisU);
                            }
                        }
                    }
                }
            }
        } else if (ABS(rAxis) <= halfLen) {
            double a = (double)(float)(dV * dV + (double)(float)(dU * dU));
            double b = (double)(float)(rU * dV + (double)(float)(rV * dU));
            double disc = (double)(float)(b * b -
                (double)(float)(a * (double)(float)((double)(float)(rU * rU + (double)(float)(rV * rV)) - maxDistSq)));
            if ((double)FLOAT_806137e0 <= disc) {
                if (disc <= (double)FLOAT_806137e0) {
                    outT[0] = (float)((double)(float)(-b * zero) / a);
                    outHit[0] = rayDir[0] * outT[0] + ox;
                    outHit[1] = rayDir[1] * outT[0] + oy;
                    outHit[2] = rayDir[2] * outT[0] + oz;
                    plane_project_normalize(outNormal, outHit, segPoint, basisV, basisU);
                } else {
                    double sq = math_sqrt(disc);
                    outT[0] = (float)(zero / a) * (float)(-b - sq);
                    outT[1] = (float)(zero / a) * (float)(-b + sq);
                    outHit[0] = rayDir[0] * outT[0] + ox;
                    outHit[1] = rayDir[1] * outT[0] + oy;
                    outHit[2] = rayDir[2] * outT[0] + oz;
                    outHit[3] = rayDir[0] * outT[1] + ox;
                    outHit[4] = rayDir[1] * outT[1] + oy;
                    outHit[5] = rayDir[2] * outT[1] + oz;
                    plane_project_normalize(outNormal, outHit, segPoint, basisV, basisU);
                    plane_project_normalize(outNormal + 3, outHit + 3, segPoint, basisV, basisU);
                }
            }
        }
    } else if ((double)(float)(rU * rU + (double)(float)(rV * rV)) <= maxDistSq) {
        outT[0] = (float)(zero / dAxis) * (float)(halfLen - rAxis);
        outT[1] = (float)(zero / dAxis) * (float)(-halfLen - rAxis);
        outNormal[0] = axis[0];
        outNormal[1] = axis[1];
        outNormal[2] = axis[2];
        outNormal[3] = -axis[0];
        outNormal[4] = -axis[1];
        outNormal[5] = -axis[2];
        outHit[0] = rayDir[0] * outT[0] + ox;
        outHit[1] = rayDir[1] * outT[0] + oy;
        outHit[2] = rayDir[2] * outT[0] + oz;
        outHit[3] = rayDir[0] * outT[1] + ox;
        outHit[4] = rayDir[1] * outT[1] + oy;
        outHit[5] = rayDir[2] * outT[1] + oz;
    }

    if (outCount != (float*)0) {
        *outCount = (float)hitCount;
    }
}

// swept_sphere_vs_segment (0x8001324c)
// Sweep a sphere along a ray against a finite segment (segStart..segEnd),
// forming the segment midpoint/half-vector, calling swept_sphere_vs_cylinder,
// then selecting the nearest valid root and writing its hit point (outHit) and
// normal (outNormal).
void swept_sphere_vs_segment(unsigned int origHandle, unsigned int scratchArg,
                             float* segStart, float* segEnd, float* outHit,
                             float* outNormal)
{
    (void)origHandle;
    (void)scratchArg;

    float work[4];
    float mid[4];       // local_64/60/5c region
    float tArr[6];      // local_78 roots
    float hitArr[6];    // local_58
    float normArr[14];  // local_40

    // Scratch ray direction (was packed in a __save_gpr handle).
    float rayDir[3] = { segEnd[0] - segStart[0], segEnd[1] - segStart[1], segEnd[2] - segStart[2] };

    work[0] = DAT_80613814;
    work[1] = DAT_80613818;

    if ((double)FLOAT_8061380c <=
        (double)(rayDir[2] * rayDir[2] + rayDir[0] * rayDir[0] + rayDir[1] * rayDir[1])) {
        mid[0] = (segStart[0] + segEnd[0]) * FLOAT_80613804;
        work[2] = segEnd[0] - mid[0];
        mid[1] = (segStart[1] + segEnd[1]) * FLOAT_80613804;
        work[3] = segEnd[1] - mid[1];
        mid[2] = (segStart[2] + segEnd[2]) * FLOAT_80613804;
        float halfZ = segEnd[2] - mid[2];
        double halfLen2 = (double)(halfZ * halfZ + work[2] * work[2] + work[3] * work[3]);
        if ((double)FLOAT_8061380c <= halfLen2) {
            double hl = math_sqrt(halfLen2);
            float count = 0.0f;
            swept_sphere_vs_cylinder(origHandle,
                                     (double)(float)((double)FLOAT_8061381c * hl),
                                     0, rayDir, &work[2], mid, tArr, hitArr, normArr);
            // (count-of-hits comes back via the outCount slot in the original;
            //  approximate by scanning tArr for the nearest non-negative root)
            (void)count;
            int best = -1;
            float bestT = FLOAT_806137d8;
            for (int i = 0; i < 2; ++i) {
                float tv = tArr[i];
                if ((FLOAT_806137e0 <= tv) && (tv < bestT)) {
                    best = i;
                    bestT = tv;
                }
            }
            if (best != -1) {
                outHit[0] = hitArr[best * 3];
                outHit[1] = hitArr[best * 3 + 1];
                outHit[2] = hitArr[best * 3 + 2];
                outNormal[0] = normArr[best * 3];
                outNormal[1] = normArr[best * 3 + 1];
                outNormal[2] = normArr[best * 3 + 2];
            }
        }
    }
}

// ray_vs_sphere (0x80013454)
// Ray/sphere intersection. sphereCenter is the sphere center; radiusSq the
// squared radius (arrives in an FPR in the original). Solves the quadratic for
// the nearest non-negative root and, when hit, writes the contact point
// (outHit) and outward normal delta (outNormal).
void ray_vs_sphere(unsigned int origHandle, unsigned int scratchArg,
                   float* sphereCenter, float* outHit, float* outNormal,
                   double radiusSq)
{
    (void)origHandle;
    (void)scratchArg;

    unsigned long long scratch = FUN_803826e8();
    float* rayOrg = (float*)(unsigned long)(scratch >> 0x20);
    float* rayDir = (float*)(unsigned long)(unsigned int)scratch;

    float ry = rayOrg[1] - sphereCenter[1];
    float rx = rayOrg[0] - sphereCenter[0];
    float rz = rayOrg[2] - sphereCenter[2];
    double b = (double)(rz * rayDir[2] + rx * rayDir[0] + ry * rayDir[1]);
    double disc = (double)(float)(b * b -
        (double)(float)((double)(rz * rz + rx * rx + ry * ry) - radiusSq));
    if ((double)FLOAT_806137e0 <= disc) {
        double sq = math_sqrt(disc);
        int best = -1;
        float roots[2];
        roots[0] = (float)(-b - sq);
        roots[1] = (float)(-b + sq);
        float bestT = FLOAT_806137d8;
        for (int i = 0; i < 2; ++i) {
            float tv = roots[i];
            if ((FLOAT_806137e0 <= tv) && (tv < bestT)) {
                best = i;
                bestT = tv;
            }
        }
        if (best != -1) {
            float hx = bestT * rayDir[0] + rayOrg[0];
            float hy = bestT * rayDir[1] + rayOrg[1];
            float hz = bestT * rayDir[2] + rayOrg[2];
            if (outHit != (float*)0) {
                outHit[0] = hx;
                outHit[1] = hy;
                outHit[2] = hz;
            }
            if (outNormal != (float*)0) {
                outNormal[0] = hx - sphereCenter[0];
                outNormal[1] = hy - sphereCenter[1];
                outNormal[2] = hz - sphereCenter[2];
            }
        }
    }
    FUN_80382734();
}

// closest_pt_on_segment (0x800135e8)
// Closest point on the segment segA->segB to the point p. Returns the squared
// distance; writes the closest point to outPt and the clamped parameter t
// (0..1) to outT when provided.
double closest_pt_on_segment(float* p, float* segA, float* segB,
                             float* outPt, float* outT)
{
    float py = p[1] - segA[1];
    float ey = segB[1] - segA[1];
    float px = p[0] - segA[0];
    float ex = segB[0] - segA[0];
    float pz = p[2] - segA[2];
    float ez = segB[2] - segA[2];
    float dot = pz * ez + px * ex + py * ey;
    float t = FLOAT_806137e0;
    if (FLOAT_806137e0 < dot) {
        float len2 = ez * ez + ex * ex + ey * ey;
        if (dot < len2) {
            t = dot / len2;
            px = -(t * ex - px);
            py = -(t * ey - py);
            pz = -(t * ez - pz);
        } else {
            px = px - ex;
            py = py - ey;
            pz = pz - ez;
            t = FLOAT_806137e8;
        }
    }
    if (outT != (float*)0) {
        *outT = t;
    }
    if (outPt != (float*)0) {
        outPt[0] = t * ex + segA[0];
        outPt[1] = t * ey + segA[1];
        outPt[2] = t * ez + segA[2];
    }
    return (double)(pz * pz + px * px + py * py);
}

// closest_between_segments (0x800136cc)
// Closest distance between two 3D segments p1->p2 and p3->p4 (Ericson-style
// clamped parametric solve). Returns the squared distance (absolute value) and,
// when provided, writes the closest point on each segment to outC1/outC2.
double closest_between_segments(float* p1, float* p2, float* p3, float* p4,
                                float* outC1, float* outC2)
{
    double d1y = (double)(p1[1] - p3[1]);
    double r1y = (double)(p2[1] - p1[1]);
    double e2y = (double)(p4[1] - p3[1]);
    double r1x = (double)(p2[0] - p1[0]);
    double e2x = (double)(p4[0] - p3[0]);
    double r1z = (double)(p2[2] - p1[2]);
    double e2z = (double)(p4[2] - p3[2]);
    double a = (double)(float)(r1z * r1z + (double)(float)(r1x * r1x + (double)(float)(r1y * r1y)));
    double b = -(double)(float)(r1z * e2z + (double)(float)(r1x * e2x + (double)(float)(r1y * e2y)));
    double e = (double)(float)(e2z * e2z + (double)(float)(e2x * e2x + (double)(float)(e2y * e2y)));
    double d1x = (double)(p1[0] - p3[0]);
    double d1z = (double)(p1[2] - p3[2]);
    double denom = ABS((double)(float)(a * e - (double)(float)(b * b)));
    double c = (double)(float)(d1z * r1z + (double)(float)(d1x * r1x + (double)(float)(d1y * r1y)));
    double distAcc = (double)(float)(d1z * d1z + (double)(float)(d1x * d1x + (double)(float)(d1y * d1y)));
    double s = (double)FLOAT_806137e0;   // t on segment 1 (dVar13)
    double u = (double)FLOAT_806137e0;   // t on segment 2 (dVar16)

    double f = -(double)(float)(d1z * e2z + (double)(float)(d1x * e2x + (double)(float)(d1y * e2y)));

    if (denom < DOUBLE_80613820) {
        // segments (nearly) parallel
        if (b <= (double)FLOAT_806137e0) {
            double negc = -c;
            if (negc < a) {
                if ((double)FLOAT_806137e0 < c) {
                    if (c < -b) {
                        u = (double)(float)(negc / b);
                        distAcc = (double)(float)(u * (double)(float)((double)FLOAT_8061381c * f + (double)(float)(e * u)) + distAcc);
                    } else {
                        distAcc = (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * f + e));
                        u = (double)FLOAT_806137e8;
                    }
                } else {
                    distAcc = (double)(float)(c * (double)(float)(negc / a) + distAcc);
                    s = (double)(float)(negc / a);
                }
            } else {
                distAcc = (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * c + a));
                s = (double)FLOAT_806137e8;
            }
        } else if (c < (double)FLOAT_806137e0) {
            if (a < -c) {
                s = (double)FLOAT_806137e8;
                if (-(double)(float)(a + c) < b) {
                    u = (double)(float)(-(double)(float)(a + c) / b);
                    distAcc = (double)(float)(u * (double)(float)(e * u +
                              (double)(float)((double)FLOAT_8061381c * (double)(float)(b + f))) +
                              (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * c + a)));
                } else {
                    distAcc = (double)(FLOAT_8061381c * (float)(f + (double)(float)(b + c)) +
                              (float)(distAcc + (double)(float)(a + e)));
                    u = s;
                }
            } else {
                s = (double)(float)(-c / a);
                distAcc = (double)(float)(c * s + distAcc);
            }
        }
    } else {
        // general (non-parallel) case
        double m = (double)(float)(b * f - (double)(float)(e * c));
        double n = (double)(float)(b * c - (double)(float)(a * f));
        if (m < (double)FLOAT_806137e0) {
            if (n < (double)FLOAT_806137e0) {
                if ((double)FLOAT_806137e0 <= c) {
                    if (f < (double)FLOAT_806137e0) {
                        if (-f < e) {
                            u = (double)(float)(-f / e);
                            distAcc = (double)(float)(f * u + distAcc);
                        } else {
                            distAcc = (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * f + e));
                            u = (double)FLOAT_806137e8;
                        }
                    }
                } else if (-c < a) {
                    s = (double)(float)(-c / a);
                    distAcc = (double)(float)(c * s + distAcc);
                } else {
                    distAcc = (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * c + a));
                    s = (double)FLOAT_806137e8;
                }
            } else if (denom < n) {
                double bc = (double)(float)(b + c);
                if ((double)FLOAT_806137e0 <= bc) {
                    if (f < (double)FLOAT_806137e0) {
                        if (-f < e) {
                            u = (double)(float)(-f / e);
                            distAcc = (double)(float)(f * u + distAcc);
                        } else {
                            distAcc = (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * f + e));
                            u = (double)FLOAT_806137e8;
                        }
                    }
                } else {
                    u = (double)FLOAT_806137e8;
                    if (-bc < a) {
                        s = (double)(float)(-bc / a);
                        distAcc = (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * f +
                                  (double)(float)(bc * s + e)));
                    } else {
                        distAcc = (double)(FLOAT_8061381c * (float)(f + bc) +
                                  (float)(distAcc + (double)(float)(a + e)));
                        s = u;
                    }
                }
            } else if (f < (double)FLOAT_806137e0) {
                if (-f < e) {
                    u = (double)(float)(-f / e);
                    distAcc = (double)(float)(f * u + distAcc);
                } else {
                    distAcc = (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * f + e));
                    u = (double)FLOAT_806137e8;
                }
            }
        } else if (denom < m) {
            if (n < (double)FLOAT_806137e0) {
                if (a <= -c) {
                    double cf = (double)(float)(b + f);
                    s = (double)FLOAT_806137e8;
                    if (cf < (double)FLOAT_806137e0) {
                        if (-cf < e) {
                            u = (double)(float)(-cf / e);
                            distAcc = (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * c +
                                      (double)(float)(cf * u + a)));
                        } else {
                            distAcc = (double)(FLOAT_8061381c * (float)(c + cf) +
                                      (float)(distAcc + (double)(float)(a + e)));
                            u = s;
                        }
                    } else {
                        distAcc = (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * c + a));
                    }
                } else if (c < (double)FLOAT_806137e0) {
                    s = (double)(float)(-c / a);
                    distAcc = (double)(float)(c * s + distAcc);
                }
            } else if (denom < n) {
                double bc = (double)(float)(b + c);
                if (a < -bc) {
                    double cf = (double)(float)(b + f);
                    s = (double)FLOAT_806137e8;
                    if (cf < (double)FLOAT_806137e0) {
                        if (-cf < e) {
                            u = (double)(float)(-cf / e);
                            distAcc = (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * c +
                                      (double)(float)(cf * u + a)));
                        } else {
                            distAcc = (double)(FLOAT_8061381c * (float)(c + cf) +
                                      (float)(distAcc + (double)(float)(a + e)));
                            u = s;
                        }
                    } else {
                        distAcc = (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * c + a));
                    }
                } else if (bc < (double)FLOAT_806137e0) {
                    s = (double)(float)(-bc / a);
                    distAcc = (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * f +
                              (double)(float)(bc * s + e)));
                    u = (double)FLOAT_806137e8;
                } else {
                    distAcc = (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * f + e));
                    u = (double)FLOAT_806137e8;
                }
            } else {
                double cf = (double)(float)(b + f);
                s = (double)FLOAT_806137e8;
                if (cf < (double)FLOAT_806137e0) {
                    if (-cf < e) {
                        u = (double)(float)(-cf / e);
                        distAcc = (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * c +
                                  (double)(float)(cf * u + a)));
                    } else {
                        distAcc = (double)(FLOAT_8061381c * (float)(c + cf) +
                                  (float)(distAcc + (double)(float)(a + e)));
                        u = s;
                    }
                } else {
                    distAcc = (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * c + a));
                }
            }
        } else if (n < (double)FLOAT_806137e0) {
            if (c < (double)FLOAT_806137e0) {
                if (-c < a) {
                    s = (double)(float)(-c / a);
                    distAcc = (double)(float)(c * s + distAcc);
                } else {
                    distAcc = (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * c + a));
                    s = (double)FLOAT_806137e8;
                }
            }
        } else if (denom < n) {
            double bc = (double)(float)(b + c);
            u = (double)FLOAT_806137e8;
            if (bc < (double)FLOAT_806137e0) {
                if (-bc < a) {
                    s = (double)(float)(-bc / a);
                    distAcc = (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * f +
                              (double)(float)(bc * s + e)));
                    u = (double)FLOAT_806137e8;
                } else {
                    distAcc = (double)(FLOAT_8061381c * (float)(f + bc) +
                              (float)(distAcc + (double)(float)(a + e)));
                    s = u;
                }
            } else {
                distAcc = (double)(float)(distAcc + (double)(float)((double)FLOAT_8061381c * f + e));
            }
        } else {
            double invd = (double)(float)((double)FLOAT_806137e8 / denom);
            u = (double)(float)(n * invd);
            s = (double)(float)(m * invd);
            distAcc = (double)(float)(distAcc + (double)(float)(s * (double)(float)((double)FLOAT_8061381c * c +
                      (double)(float)(a * s + (double)(float)(b * u))) +
                      (double)(float)(u * (double)(float)((double)FLOAT_8061381c * f +
                      (double)(float)(b * s + (double)(float)(e * u))))));
        }
    }

    if (outC1 != (float*)0) {
        outC1[0] = (float)(s * r1x + (double)p1[0]);
        outC1[1] = (float)(s * r1y + (double)p1[1]);
        outC1[2] = (float)(s * r1z + (double)p1[2]);
    }
    if (outC2 != (float*)0) {
        outC2[0] = (float)(u * e2x + (double)p3[0]);
        outC2[1] = (float)(u * e2y + (double)p3[1]);
        outC2[2] = (float)(u * e2z + (double)p3[2]);
    }
    return ABS(distAcc);
}

// =============================================================================
// Batch 380: swept / quantized-mesh collision resolve layer (0x80013dfc - 0x800154d0)
// Callers: collision_bvh_swept_query (0x80065f74), collision_entity_swept_cast
// (0x8006649c), collision_bvh_raycast. Continues collision_geometry.cpp.
// =============================================================================

// ---- additional numeric constants (rodata @ 0x80613828..0x80613860) -------
extern float  FLOAT_80613828;  // ~1e-4  is-point / min-radius epsilon
extern float  FLOAT_8061382c;  // 0.0
extern float  FLOAT_80613830;  // 1/256  packed-vertex scale
extern float  FLOAT_80613834;  // 127.0
extern float  FLOAT_80613838;  // -1.0
extern float  FLOAT_8061383c;  // ~0.866 cos(30 deg) edge-angle threshold
extern float  FLOAT_80613840;  // 1.0
extern double DOUBLE_80613848;  // uint->double conversion magic bias
extern float  FLOAT_80613850;  // ~1e-7  squared-length epsilon
extern float  FLOAT_80613854;  // 0.5    (Newton-Raphson rsqrt)
extern float  FLOAT_80613858;  // 1.5    (Newton-Raphson rsqrt)
extern float  FLOAT_8061385c;  // 2.0
extern float  FLOAT_80613860;  // 4.0    (quadratic 4ac)

// ---- scratch work-buffer alloc/free pairs (return packed hi:lo pointers) --
extern "C" unsigned long long FUN_803826d0(void);
extern "C" void               FUN_8038271c(void);
extern "C" unsigned long long FUN_803826c8(void);
extern "C" void               FUN_80382714(void);

// ---- forward declarations for intra-batch calls ---------------------------
void swept_sphere_vs_edge(unsigned int a, unsigned int b, float* segA, float* segB,
                          float* ioT, float* outHit, double radius);
void mesh_face_closest_edge(unsigned int a, unsigned int b, int meshHdr, float* basePos,
                            int packed, char* face, float* outPt, int* outPrevIdx, int* outIdx);
void collide_sphere_vs_mesh_static(int* state, unsigned int b, int meshHdr, float* basePos,
                                   int packed, int edgeTable, char* face, int noEdge,
                                   float* outNormal, float* outPush, float* outDepth, int flags);
void collide_swept_sphere_vs_mesh(int* state, unsigned int b, int meshHdr, float* basePos,
                                  int packed, char* face, int noEdge, float* outNormal,
                                  float* outPush, float* outDepth, float* ioT, int flags);

// ray_vs_poly_plane_ring (0x80013dfc)
// Ray vs a convex polygon stored in an object: plane normal at obj+0x30..0x38,
// plane point at obj+0x40 (first ring vertex), vertex ring at obj+0x40 with
// stride 0x1c, count u16 at obj+0x2a, projection axis bytes at obj+0x3c/0x3d
// (and store axis obj+0x3e). Rejects hits beyond maxDistSq. Returns 1 on hit
// and writes the projected hit point components into outHit (raw int base).
int ray_vs_poly_plane_ring(double maxDistSq, float* rayOrg, float* rayDir, int obj,
                           int outHit, float* outDistSq)
{
    float ny = *(float*)(obj + 0x34);
    float dy = rayDir[1];
    float nx = *(float*)(obj + 0x30);
    float dx = rayDir[0];
    float nz = *(float*)(obj + 0x38);
    float dz = rayDir[2];
    float denom = nz * dz + nx * dx + ny * dy;
    if (FLOAT_806137e0 == denom) {
        return 0;
    }
    float t = ((nz * *(float*)(obj + 0x48) + nx * *(float*)(obj + 0x40) + ny * *(float*)(obj + 0x44)) -
               (nz * rayOrg[2] + nx * rayOrg[0] + ny * rayOrg[1])) / denom;
    if (FLOAT_806137e0 <= t) {
        float dist2 = t * t * (dz * dz + dx * dx + dy * dy);
        if (outDistSq != (float*)0) {
            *outDistSq = dist2;
        }
        if ((double)dist2 <= maxDistSq) {
            unsigned int count = (unsigned int)*(unsigned short*)(obj + 0x2a);
            int iter = 0;
            int axisV = (int)*(char*)(obj + 0x3d);
            int offV = axisV * 4;
            int axisU = (int)*(char*)(obj + 0x3c);
            int offU = axisU * 4;
            float hv = t * rayDir[axisV] + rayOrg[axisV];
            float hu = t * rayDir[axisU] + rayOrg[axisU];
            int prev = obj + (count - 1) * 0x1c + 0x40;
            while (true) {
                if (count == 0) {
                    if (outHit != 0) {
                        char storeAxis = *(char*)(obj + 0x3e);
                        *(float*)(outHit + offV) = hv;
                        int offW = (int)storeAxis;
                        *(float*)(outHit + offU) = hu;
                        *(float*)(outHit + offW * 4) = t * rayDir[offW] + rayOrg[offW];
                    }
                    return 1;
                }
                int cur = obj + iter + 0x40;
                if (FLOAT_806137e0 <
                    (hv - *(float*)(offU + prev)) * (*(float*)(offV + cur) - *(float*)(offV + prev)) +
                    (hu - *(float*)(offV + prev)) * (*(float*)(offU + prev) - *(float*)(offU + cur))) {
                    break;
                }
                iter = iter + 0x1c;
                count = count - 1;
                prev = cur;
            }
            return 0;
        }
        return 0;
    }
    return 0;
}

// mesh_face_closest_edge (0x80013f84)
// Walk the edges of a mesh face and find the edge whose closest point to the
// query point (packed in the scratch handle) is nearest. Writes that closest
// point to outPt and the edge's endpoint indices to outPrevIdx / outIdx.
void mesh_face_closest_edge(unsigned int a, unsigned int b, int meshHdr, float* basePos,
                            int packed, char* face, float* outPt, int* outPrevIdx, int* outIdx)
{
    (void)a;
    (void)b;

    unsigned long long scratch = FUN_803826d0();
    int rawBase = (int)(unsigned int)scratch;
    float* queryPt = (float*)(unsigned long)(scratch >> 0x20);

    int idxBase = *(int*)(meshHdr + 0x2c) + (unsigned int)*(unsigned short*)(face + 8) * 2;
    double bestD = (double)FLOAT_806137d8;
    int first = (int)*(short*)(idxBase + *face * 2 + -2);

    float prev[3];
    float best[3] = { FLOAT_806137e0, FLOAT_806137e0, FLOAT_806137e0 };
    if (packed == 0) {
        float* pv = (float*)(rawBase + first * 0xc);
        prev[0] = pv[0]; prev[1] = pv[1]; prev[2] = pv[2];
    } else {
        short* sv = (short*)(rawBase + first * 6);
        prev[0] = FLOAT_806137f0 * (float)((double)CONCAT44(0x43300000, (int)sv[0] ^ 0x80000000) - DOUBLE_806137f8) + basePos[0];
        prev[1] = FLOAT_806137f0 * (float)((double)CONCAT44(0x43300000, (int)sv[1] ^ 0x80000000) - DOUBLE_806137f8) + basePos[1];
        prev[2] = FLOAT_806137f0 * (float)((double)CONCAT44(0x43300000, (int)sv[2] ^ 0x80000000) - DOUBLE_806137f8) + basePos[2];
    }

    int iter = 0;
    for (int i = 0; i < *face; i = i + 1) {
        int idx = (int)*(short*)(idxBase + iter);
        float cur[3];
        if (packed == 0) {
            float* pv = (float*)(rawBase + idx * 0xc);
            cur[0] = pv[0]; cur[1] = pv[1]; cur[2] = pv[2];
        } else {
            short* sv = (short*)(rawBase + idx * 6);
            cur[0] = FLOAT_806137f0 * (float)((double)CONCAT44(0x43300000, (int)sv[0] ^ 0x80000000) - DOUBLE_806137f8) + basePos[0];
            cur[1] = FLOAT_806137f0 * (float)((double)CONCAT44(0x43300000, (int)sv[1] ^ 0x80000000) - DOUBLE_806137f8) + basePos[1];
            cur[2] = FLOAT_806137f0 * (float)((double)CONCAT44(0x43300000, (int)sv[2] ^ 0x80000000) - DOUBLE_806137f8) + basePos[2];
        }
        float cp[3];
        double d = closest_pt_on_segment(queryPt, prev, cur, cp, (float*)0);
        if (d < bestD) {
            best[0] = cp[0]; best[1] = cp[1]; best[2] = cp[2];
            if (outPrevIdx != (int*)0) {
                int pi = (i == 0) ? (int)*face : i;
                *outPrevIdx = pi + -1;
            }
            bestD = d;
            if (outIdx != (int*)0) {
                *outIdx = i;
            }
        }
        iter = iter + 2;
        prev[0] = cur[0]; prev[1] = cur[1]; prev[2] = cur[2];
    }
    if (outPt != (float*)0) {
        outPt[0] = best[0];
        outPt[1] = best[1];
        outPt[2] = best[2];
    }
    FUN_8038271c();
}

// point_in_poly_edges (0x80014248)
// Point-in-convex-polygon test using per-edge outward normals. edges points to
// an array of count 2D vertices (float[2] each). For every edge the signed
// distance of (px,py) is compared against 'margin'; all must satisfy it. The
// edge normal is normalized with a fast reciprocal-sqrt + Newton refine.
int point_in_poly_edges(double px, double py, double margin, int edges, int count)
{
    int prevIdx = count << 3;
    int idx = 0;
    do {
        bool last = (count == 0);
        count = count + -1;
        prevIdx = prevIdx + -8;
        if (last) {
            return 1;
        }
        float* v = (float*)(edges + idx * 8);
        double vx = (double)v[0];
        double vy = (double)v[1];
        float ny = (float)((double)((float*)(edges + prevIdx))[1] - vy);
        float nx = -(float)((double)*(float*)(edges + prevIdx) - vx);
        float len2 = ny * ny + nx * nx;
        float half = FLOAT_80613804 * len2;
        float r = 1.0f / SQRT(len2);
        r = r * -(half * r * r - FLOAT_80613808);
        r = FLOAT_806137e8 * r * -(half * r * r - FLOAT_80613808);
        idx = count;
        if (!((double)(ny * r * (float)(px - vx) + nx * r * (float)(py - vy)) < margin)) {
            return 0;
        }
    } while (true);
}

// build_swept_bounds (0x80014300)
// Build a 14-slot swept-sphere bounds record. Slot 0 = "is (near) a point"
// flag; 1..3 = center; 7 = radius; 4..6 = sweep delta; 8..13 = expanded
// per-axis [min,max] taking the sweep and radius into account.
void build_swept_bounds(double radius, int* out, float* center, float* delta)
{
    float pointEps = FLOAT_80613828;
    int isPoint = 0;
    out[1] = (int)center[0];
    out[2] = (int)center[1];
    out[3] = (int)center[2];
    out[7] = (int)(float)radius;
    if ((ABS(delta[0]) < pointEps) && (ABS(delta[1]) < pointEps) && (ABS(delta[2]) < pointEps)) {
        isPoint = 1;
    }
    out[0] = isPoint;
    float zero = FLOAT_8061382c;
    if (out[0] != 0) {
        out[8]  = (int)(float)((double)center[0] - radius);
        out[11] = (int)(float)((double)center[0] + radius);
        out[9]  = (int)(float)((double)center[1] - radius);
        out[12] = (int)(float)((double)center[1] + radius);
        out[10] = (int)(float)((double)center[2] - radius);
        out[13] = (int)(float)((double)center[2] + radius);
        return;
    }
    out[4] = (int)delta[0];
    out[5] = (int)delta[1];
    out[6] = (int)delta[2];
    if (zero <= delta[0]) {
        out[8]  = (int)(float)((double)center[0] - radius);
        out[11] = (int)(float)(radius + (double)(center[0] + delta[0]));
    } else {
        out[8]  = (int)(float)((double)(center[0] + delta[0]) - radius);
        out[11] = (int)(float)((double)center[0] + radius);
    }
    if (FLOAT_8061382c <= delta[1]) {
        out[9]  = (int)(float)((double)center[1] - radius);
        out[12] = (int)(float)(radius + (double)(center[1] + delta[1]));
    } else {
        out[9]  = (int)(float)((double)(center[1] + delta[1]) - radius);
        out[12] = (int)(float)((double)center[1] + radius);
    }
    if (delta[2] < FLOAT_8061382c) {
        out[10] = (int)(float)((double)(center[2] + delta[2]) - radius);
        out[13] = (int)(float)((double)center[2] + radius);
        return;
    }
    out[10] = (int)(float)((double)center[2] - radius);
    out[13] = (int)(float)(radius + (double)(center[2] + delta[2]));
}

// aabb_vs_entity_bounds (0x800144c0)
// Overlap test between a query AABB (aabb[0..2]=min, [3..5]=max) and an entity's
// bounding box stored at ent+0x20..0x28 (min) and ent+0x2c..0x34 (max).
int aabb_vs_entity_bounds(int ent, float* aabb)
{
    if ((((aabb[0] <= *(float*)(ent + 0x2c)) && (*(float*)(ent + 0x20) <= aabb[3])) &&
         (aabb[1] <= *(float*)(ent + 0x30))) &&
        (((*(float*)(ent + 0x24) <= aabb[4] && (aabb[2] <= *(float*)(ent + 0x34))) &&
          (*(float*)(ent + 0x28) <= aabb[5])))) {
        return 1;
    }
    return 0;
}

// swept_sphere_vs_edge (0x800154d0)
// Sweep a sphere (radius) along a ray (origin/dir packed in the scratch handle)
// against a segment segA->segB. Two-phase: solve against the infinite-cylinder
// body, else against the nearer endpoint. On the earliest valid hit (< *ioT)
// updates *ioT and writes the contact point to outHit.
void swept_sphere_vs_edge(unsigned int a, unsigned int b, float* segA, float* segB,
                          float* ioT, float* outHit, double radius)
{
    (void)a;
    (void)b;

    unsigned long long scratch = FUN_803826e8();
    float* rayDir = (float*)(unsigned long)(unsigned int)scratch;
    float* rayOrg = (float*)(unsigned long)(scratch >> 0x20);

    double radiusSq = (double)(float)(radius * radius);
    double ey = (double)(segB[1] - segA[1]);
    double dy = (double)rayDir[1];
    double oy = (double)(rayOrg[1] - segA[1]);
    double dx = (double)rayDir[0];
    double ex = (double)(segB[0] - segA[0]);
    double ox = (double)(rayOrg[0] - segA[0]);
    double dz = (double)rayDir[2];
    double ez = (double)(segB[2] - segA[2]);
    double oz = (double)(rayOrg[2] - segA[2]);

    double eLen2 = (double)(float)(ez * ez + (double)(float)(ex * ex + (double)(float)(ey * ey)));
    double eDotO = (double)(float)(oz * ez + (double)(float)(ox * ex + (double)(float)(oy * ey)));
    double dLen2 = (double)(float)(dz * dz + (double)(float)(dx * dx + (double)(float)(dy * dy)));
    float eDotD = (float)(oz * ez + (double)(float)(ox * ex + (double)(float)(oy * ey)));
    float dDotE = (float)(ez * dz + (double)(float)(ex * dx + (double)(float)(ey * dy)));
    double oLen2 = (double)(float)(oz * oz + (double)(float)(ox * ox + (double)(float)(oy * oy)));

    // cylinder-body quadratic
    double A = (double)(dDotE * dDotE - (float)(eLen2 * dLen2));
    double B = (double)(FLOAT_8061385c * (eDotD * dDotE - (float)(eDotO * eLen2)));
    if ((double)FLOAT_8061382c != A) {
        double disc = (double)(float)(B * B -
            (double)((float)((double)FLOAT_80613860 * A) *
                     -(float)(oLen2 * eLen2 - (double)(eDotD * eDotD + (float)(eLen2 * radiusSq)))));
        if (disc <= (double)FLOAT_8061382c) {
            goto endpoint;
        }
        double sq = math_sqrt(disc);
        double t0 = (double)((float)(-B - sq) / (float)((double)FLOAT_8061385c * A));
        double t1 = (double)((float)(-B + sq) / (float)((double)FLOAT_8061385c * A));
        if (t1 < t0) {
            t0 = t1;
        }
        if ((t0 < (double)FLOAT_8061382c) || ((double)*ioT <= t0)) {
            goto endpoint;
        }
        double along = (double)(float)((double)(float)(
            (double)(((float)((double)rayDir[2] * t0) + rayOrg[2]) - segA[2]) * ez +
            (double)(float)((double)(((float)((double)rayDir[0] * t0) + rayOrg[0]) - segA[0]) * ex +
            (double)(float)((double)(((float)((double)rayDir[1] * t0) + rayOrg[1]) - segA[1]) * ey))) / eLen2);
        if (((double)FLOAT_8061382c <= along) && (along <= (double)FLOAT_80613840)) {
            *ioT = (float)t0;
            outHit[0] = (float)(ex * along);
            outHit[1] = (float)(ey * along);
            outHit[2] = (float)(ez * along);
            outHit[0] = outHit[0] + segA[0];
            outHit[1] = outHit[1] + segA[1];
            outHit[2] = outHit[2] + segA[2];
            goto done;
        }
    }
endpoint:
    {
        // endpoint (sphere vs segment start) quadratic
        double Be = (double)(float)((double)FLOAT_8061385c * eDotO);
        double disc = (double)(float)(Be * Be -
            (double)((float)((double)FLOAT_80613860 * dLen2) * (float)(oLen2 - radiusSq)));
        if ((double)FLOAT_8061382c < disc) {
            double sq = math_sqrt(disc);
            float t0 = (float)(-Be - sq) / (float)((double)FLOAT_8061385c * dLen2);
            float t1 = (float)(-Be + sq) / (float)((double)FLOAT_8061385c * dLen2);
            if (t1 < t0) {
                t0 = t1;
            }
            if ((FLOAT_8061382c <= t0) && (t0 < *ioT)) {
                *ioT = t0;
                outHit[0] = segA[0];
                outHit[1] = segA[1];
                outHit[2] = segA[2];
            }
        }
    }
done:
    FUN_80382734();
}

// collide_sphere_vs_mesh_static (0x80014530)
// Resolve a static sphere against a quantized mesh face: project the sphere
// center onto the face plane, test containment (mesh_face_point_inside), else
// find the closest edge (mesh_face_closest_edge) and, when the contact angle
// crosses the shared-edge threshold, compute a push-out normal/depth.
void collide_sphere_vs_mesh_static(int* state, unsigned int b, int meshHdr, float* basePos,
                                   int packed, int edgeTable, char* face, int noEdge,
                                   float* outNormal, float* outPush, float* outDepth, int flags)
{
    (void)b;

    unsigned long long scratch = FUN_803826d0();
    int rec = (int)(unsigned long)(scratch >> 0x20);
    int rawBase = (int)(unsigned int)scratch;

    int idxBase = *(int*)(meshHdr + 0x2c) + (unsigned int)*(unsigned short*)(face + 8) * 2;
    int first = (int)*(short*)(idxBase + *face * 2 + -2);
    float v0x, v0y, v0z;
    if (packed == 0) {
        float* pv = (float*)(rawBase + first * 0xc);
        v0x = pv[0]; v0y = pv[1]; v0z = pv[2];
    } else {
        short* sv = (short*)(rawBase + first * 6);
        v0x = FLOAT_80613830 * (float)((double)CONCAT44(0x43300000, (int)sv[0] ^ 0x80000000) - DOUBLE_80613848) + basePos[0];
        v0y = FLOAT_80613830 * (float)((double)CONCAT44(0x43300000, (int)sv[1] ^ 0x80000000) - DOUBLE_80613848) + basePos[1];
        v0z = FLOAT_80613830 * (float)((double)CONCAT44(0x43300000, (int)sv[2] ^ 0x80000000) - DOUBLE_80613848) + basePos[2];
    }

    // face plane normal (packed signed components in face[1..3], /127 scale)
    double nz = (double)((float)((double)CONCAT44(0x43300000, (int)face[2] ^ 0x80000000) - DOUBLE_80613848) / FLOAT_80613834);
    double ny = (double)((float)((double)CONCAT44(0x43300000, (int)face[1] ^ 0x80000000) - DOUBLE_80613848) / FLOAT_80613834);
    double nx = (double)((float)((double)CONCAT44(0x43300000, (int)face[3] ^ 0x80000000) - DOUBLE_80613848) / FLOAT_80613834);

    double signedDist = (double)((float)(nx * (double)*(float*)(rec + 0xc) +
        (double)(float)(ny * (double)*(float*)(rec + 4) + (double)(float)(nz * (double)*(float*)(rec + 8)))) -
        (float)(nx * (double)v0z + (double)(float)(ny * (double)v0x + (double)(float)(nz * (double)v0y))));

    if (ABS(signedDist) <= (double)*(float*)(rec + 0x1c)) {
        double snx = nx, sny = ny, snz = nz;
        if ((noEdge == 0) && (signedDist < (double)FLOAT_8061382c)) {
            signedDist = -signedDist;
            snx = -nx; sny = -ny; snz = -nz;
        }
        float depth = (float)(signedDist - (double)*(float*)(rec + 0x1c));
        if (FLOAT_8061382c < depth) {
            depth = FLOAT_8061382c;
        }
        *outDepth = depth;
        outNormal[0] = -(float)(snx * signedDist - (double)*(float*)(rec + 4));
        outNormal[1] = -(float)(sny * signedDist - (double)*(float*)(rec + 8));
        outNormal[2] = -(float)(snz * signedDist - (double)*(float*)(rec + 0xc));
        int inside = mesh_face_point_inside((int)outNormal, rawBase, meshHdr, (int)basePos, packed, face);
        if (inside == 0) {
            if (flags == 0) {
                // Outside the face: fall back to the nearest edge, then recompute
                // the push-out from the sphere center to that closest point.
                float closest[3];
                int prevIdx = 0;
                int curIdx = 0;
                mesh_face_closest_edge((unsigned int)(rec + 4), b, meshHdr, basePos,
                                       packed, face, closest, &prevIdx, &curIdx);
                float ex = *(float*)(rec + 4) - closest[0];
                float ey = *(float*)(rec + 8) - closest[1];
                float ez = *(float*)(rec + 0xc) - closest[2];
                double d = math_sqrt((double)(ez * ez + ex * ex + ey * ey));
                float dd = (float)(d - (double)*(float*)(rec + 0x1c));
                if (FLOAT_8061382c < dd) {
                    dd = FLOAT_8061382c;
                }
                *outDepth = dd;
                outPush[0] = ex;
                outPush[1] = ey;
                outPush[2] = ez;
                (void)edgeTable;
            }
        } else {
            outPush[0] = (float)snx;
            outPush[1] = (float)sny;
            outPush[2] = (float)snz;
        }
    }
    FUN_8038271c();
    (void)state;
}

// collide_swept_sphere_vs_mesh (0x80014a54)
// Sweep a sphere along its motion delta against a quantized mesh face. Projects
// the swept sphere onto the face plane over the sweep interval; on containment
// writes the plane normal, otherwise sweeps each edge (swept_sphere_vs_edge)
// and, on a hit, forms the contact normal (fast-normalized) and penetration.
void collide_swept_sphere_vs_mesh(int* state, unsigned int b, int meshHdr, float* basePos,
                                  int packed, char* face, int noEdge, float* outNormal,
                                  float* outPush, float* outDepth, float* ioT, int flags)
{
    (void)b;

    unsigned long long scratch = FUN_803826c8();
    int rec = (int)(unsigned long)(scratch >> 0x20);
    int rawBase = (int)(unsigned int)scratch;

    // face plane normal / 127
    double ny = (double)((float)((double)CONCAT44(0x43300000, (int)face[1] ^ 0x80000000) - DOUBLE_80613848) / FLOAT_80613834);
    int idxBase = *(int*)(meshHdr + 0x2c) + (unsigned int)*(unsigned short*)(face + 8) * 2;
    double nz = (double)((float)((double)CONCAT44(0x43300000, (int)face[2] ^ 0x80000000) - DOUBLE_80613848) / FLOAT_80613834);
    double nx = (double)((float)((double)CONCAT44(0x43300000, (int)face[3] ^ 0x80000000) - DOUBLE_80613848) / FLOAT_80613834);
    int first = (int)*(short*)(idxBase + *face * 2 + -2);

    float v0x, v0y, v0z;
    if (packed == 0) {
        float* pv = (float*)(rawBase + first * 0xc);
        v0x = pv[0]; v0y = pv[1]; v0z = pv[2];
    } else {
        short* sv = (short*)(rawBase + first * 6);
        v0x = FLOAT_80613830 * (float)((double)CONCAT44(0x43300000, (int)sv[0] ^ 0x80000000) - DOUBLE_80613848) + basePos[0];
        v0y = FLOAT_80613830 * (float)((double)CONCAT44(0x43300000, (int)sv[1] ^ 0x80000000) - DOUBLE_80613848) + basePos[1];
        v0z = FLOAT_80613830 * (float)((double)CONCAT44(0x43300000, (int)sv[2] ^ 0x80000000) - DOUBLE_80613848) + basePos[2];
    }

    float distStart = (float)(nx * (double)v0x + (double)(float)(ny * (double)v0y + (double)(float)(nz * (double)v0z)));
    // distance of swept end and start from the plane
    float distEnd = (float)(nx * (double)(*(float*)(rec + 0xc) + *(float*)(rec + 0x18)) +
        (double)(float)(ny * (double)(*(float*)(rec + 4) + *(float*)(rec + 0x10)) +
        (double)(float)(nz * (double)(*(float*)(rec + 8) + *(float*)(rec + 0x14))))) - distStart;
    float dist0 = (float)(nx * (double)*(float*)(rec + 0xc) +
        (double)(float)(ny * (double)*(float*)(rec + 4) + (double)(float)(nz * (double)*(float*)(rec + 8)))) - distStart;

    double snx = nx, sny = ny, snz = nz;
    if ((noEdge == 0) && (dist0 < FLOAT_8061382c)) {
        double s = (double)FLOAT_80613838;   // -1.0
        dist0 = -dist0;
        distEnd = -distEnd;
        snx = (double)(float)(nx * s);
        sny = (double)(float)(ny * s);
        snz = (double)(float)(nz * s);
    }

    float radius = *(float*)(rec + 0x1c);
    float tHit;
    if (radius <= ABS(dist0)) {
        if ((dist0 <= radius) || (radius <= distEnd)) {
            goto done;
        }
        tHit = (dist0 - radius) / (dist0 - distEnd);
        *outPush = FLOAT_8061382c;
    } else {
        tHit = FLOAT_8061382c;
        *outPush = dist0 - *(float*)(rec + 0x1c);
    }

    if (tHit <= *ioT) {
        outNormal[0] = -(float)(snx * (double)(*(float*)(rec + 0x1c) + *outPush) -
                               (double)(tHit * *(float*)(rec + 0x10) + *(float*)(rec + 4)));
        outNormal[1] = -(float)(sny * (double)(*(float*)(rec + 0x1c) + *outPush) -
                               (double)(tHit * *(float*)(rec + 0x14) + *(float*)(rec + 8)));
        outNormal[2] = -(float)(snz * (double)(*(float*)(rec + 0x1c) + *outPush) -
                               (double)(tHit * *(float*)(rec + 0x18) + *(float*)(rec + 0xc)));
        int inside = mesh_face_point_inside((int)outNormal, rawBase, meshHdr, (int)basePos, packed, face);
        if (inside == 0) {
            if (flags == 0) {
                int iter = 0;
                int hits = 0;
                tHit = FLOAT_80613840;
                float prev[3] = { v0x, v0y, v0z };
                for (int i = 0; i < *face; i = i + 1) {
                    int idx = (int)*(short*)(idxBase + iter);
                    float cur[3];
                    if (packed == 0) {
                        float* pv = (float*)(rawBase + idx * 0xc);
                        cur[0] = pv[0]; cur[1] = pv[1]; cur[2] = pv[2];
                    } else {
                        short* sv = (short*)(rawBase + idx * 6);
                        cur[0] = FLOAT_80613830 * (float)((double)CONCAT44(0x43300000, (int)sv[0] ^ 0x80000000) - DOUBLE_80613848) + basePos[0];
                        cur[1] = FLOAT_80613830 * (float)((double)CONCAT44(0x43300000, (int)sv[1] ^ 0x80000000) - DOUBLE_80613848) + basePos[1];
                        cur[2] = FLOAT_80613830 * (float)((double)CONCAT44(0x43300000, (int)sv[2] ^ 0x80000000) - DOUBLE_80613848) + basePos[2];
                    }
                    float tArr[1] = { tHit };
                    swept_sphere_vs_edge((unsigned int)(rec + 4), (unsigned int)(rec + 0x10),
                                         prev, cur, tArr, outNormal, (double)*(float*)(rec + 0x1c));
                    tHit = tArr[0];
                    if (tArr[0] != FLOAT_80613840) { hits = hits + 1; }
                    iter = iter + 2;
                    prev[0] = cur[0]; prev[1] = cur[1]; prev[2] = cur[2];
                }
                if (hits != 0) {
                    outPush[0] = (*(float*)(rec + 0x10) * tHit + *(float*)(rec + 4)) - outNormal[0];
                    outPush[1] = (*(float*)(rec + 0x14) * tHit + *(float*)(rec + 8)) - outNormal[1];
                    outPush[2] = (*(float*)(rec + 0x18) * tHit + *(float*)(rec + 0xc)) - outNormal[2];
                    float px = outPush[0];
                    float pl2 = outPush[2] * outPush[2] + px * px + outPush[1] * outPush[1];
                    if (FLOAT_80613850 < pl2) {
                        float r = 1.0f / SQRT(pl2);
                        r = r * -(FLOAT_80613854 * pl2 * r * r - FLOAT_80613858);
                        r = FLOAT_80613840 * r * -(FLOAT_80613854 * pl2 * r * r - FLOAT_80613858);
                        outPush[0] = px * r;
                        outPush[1] = outPush[1] * r;
                        outPush[2] = outPush[2] * r;
                    }
                    float ey = outNormal[1] - *(float*)(rec + 8);
                    float ex = outNormal[0] - *(float*)(rec + 4);
                    float ez = outNormal[2] - *(float*)(rec + 0xc);
                    double dsq = (double)(ez * ez + ex * ex + ey * ey);
                    double dlen = math_sqrt(dsq);
                    float depth = FLOAT_8061382c;
                    if ((float)(dlen - (double)*(float*)(rec + 0x1c)) <= FLOAT_8061382c) {
                        dlen = math_sqrt(dsq);
                        depth = (float)(dlen - (double)*(float*)(rec + 0x1c));
                    }
                    *outDepth = depth;
                    *ioT = tHit;
                }
            }
        } else {
            outPush[0] = (float)snx;
            outPush[1] = (float)sny;
            outPush[2] = (float)snz;
            *ioT = tHit;
        }
    }
done:
    FUN_80382714();
    (void)state;
}

// collide_swept_sphere_vs_poly (0x800150d8)
// Sweep a sphere against a convex polygon ring (obj layout as in
// poly_point_inside_ring). Plane test over the sweep, containment test, else
// per-edge swept_sphere_vs_edge and contact-normal/penetration output.
void collide_swept_sphere_vs_poly(unsigned int a, unsigned int b, int obj, int noEdge,
                                  float* outNormal, float* outPush, float* outDepth,
                                  float* ioT, int flags)
{
    (void)a;
    (void)b;

    unsigned long long scratch = FUN_803826cc();
    float* recHi = (float*)(unsigned long)(scratch >> 0x20);  // sphere center/motion record
    float* recLo = (float*)(unsigned long)(unsigned int)scratch;  // sphere delta

    double ny = (double)*(float*)(obj + 0x34);
    float* prev = (float*)(obj + (*(unsigned short*)(obj + 0x2a) - 1) * 0x1c + 0x40);
    double nx = (double)*(float*)(obj + 0x30);
    double nz = (double)*(float*)(obj + 0x38);
    float distStart = (float)(nz * (double)prev[2] + (double)(float)(nx * (double)prev[0] + (double)(float)(ny * (double)prev[1])));
    double dist0 = (double)((float)(nz * (double)recHi[2] + (double)(float)(nx * (double)recHi[0] + (double)(float)(ny * (double)recHi[1]))) - distStart);
    double distEnd = (double)((float)(nz * (double)(float)((double)recHi[2] + (double)recLo[2]) +
        (double)(float)(nx * (double)(float)((double)recHi[0] + (double)recLo[0]) +
        (double)(float)(ny * (double)(float)((double)recHi[1] + (double)recLo[1])))) - distStart);

    double snx = nx, sny = ny, snz = nz;
    if ((noEdge == 0) && (dist0 < (double)FLOAT_8061382c)) {
        double s = (double)FLOAT_80613838;
        dist0 = -dist0;
        distEnd = -distEnd;
        snx = (double)(float)(nx * s);
        sny = (double)(float)(ny * s);
        snz = (double)(float)(nz * s);
    }

    // sphere radius lives in an incoming FPR in the original (extraout_f1);
    // it is the record's radius slot in this layer.
    double radius = (double)recHi[3];
    float tHit;
    if (radius <= ABS(dist0)) {
        if ((dist0 <= radius) || (radius <= distEnd)) {
            goto done;
        }
        tHit = (float)(dist0 - radius) / (float)(dist0 - distEnd);
        *outDepth = FLOAT_8061382c;
    } else {
        tHit = FLOAT_8061382c;
        *outDepth = (float)(dist0 - radius);
    }

    if (tHit <= *ioT) {
        outNormal[0] = -(float)(snx * (double)(float)(radius + (double)*outDepth) -
                               (double)(tHit * recLo[0] + recHi[0]));
        outNormal[1] = -(float)(sny * (double)(float)(radius + (double)*outDepth) -
                               (double)(tHit * recLo[1] + recHi[1]));
        outNormal[2] = -(float)(snz * (double)(float)(radius + (double)*outDepth) -
                               (double)(tHit * recLo[2] + recHi[2]));
        int inside = poly_point_inside_ring((int)outNormal, obj);
        if (inside == 0) {
            if (flags == 0) {
                int iter = 0;
                int hits = 0;
                tHit = FLOAT_80613840;
                for (int i = 0; i < (int)(unsigned int)*(unsigned short*)(obj + 0x2a); i = i + 1) {
                    float* cur = (float*)(obj + iter + 0x40);
                    float tArr[1] = { tHit };
                    swept_sphere_vs_edge((unsigned int)recHi, (unsigned int)recLo,
                                         prev, cur, tArr, outNormal, radius);
                    tHit = tArr[0];
                    if (tArr[0] != FLOAT_80613840) { hits = hits + 1; }
                    iter = iter + 0x1c;
                    prev = cur;
                }
                if (hits != 0) {
                    outPush[0] = (recLo[0] * tHit + recHi[0]) - outNormal[0];
                    outPush[1] = (recLo[1] * tHit + recHi[1]) - outNormal[1];
                    outPush[2] = (recLo[2] * tHit + recHi[2]) - outNormal[2];
                    float px = outPush[0];
                    float pl2 = outPush[2] * outPush[2] + px * px + outPush[1] * outPush[1];
                    if (FLOAT_80613850 < pl2) {
                        float r = 1.0f / SQRT(pl2);
                        r = r * -(FLOAT_80613854 * pl2 * r * r - FLOAT_80613858);
                        r = FLOAT_80613840 * r * -(FLOAT_80613854 * pl2 * r * r - FLOAT_80613858);
                        outPush[0] = px * r;
                        outPush[1] = outPush[1] * r;
                        outPush[2] = outPush[2] * r;
                    }
                    double dsq = (double)((outNormal[2] - recHi[2]) * (outNormal[2] - recHi[2]) +
                        (outNormal[0] - recHi[0]) * (outNormal[0] - recHi[0]) +
                        (outNormal[1] - recHi[1]) * (outNormal[1] - recHi[1]));
                    double dlen = math_sqrt(dsq);
                    float depth = FLOAT_8061382c;
                    if ((float)(dlen - radius) <= FLOAT_8061382c) {
                        dlen = math_sqrt(dsq);
                        depth = (float)(dlen - radius);
                    }
                    *outDepth = depth;
                    *ioT = tHit;
                }
            }
        } else {
            outPush[0] = (float)snx;
            outPush[1] = (float)sny;
            outPush[2] = (float)snz;
            *ioT = tHit;
        }
    }
done:
    FUN_80382718();
}

// collide_mesh_dispatch (0x80015044)
// Entry point picking the swept vs static mesh collision path from a state flag
// (state[0]): 0 => swept sphere, else => static sphere. Sets the sweep-fraction
// output slot accordingly. Caller: collision_bvh_swept_query.
void collide_mesh_dispatch(int* state, unsigned int b, int meshHdr, float* basePos,
                           int packed, int edgeTable, char* face, int noEdge,
                           float* outNormal, float* outPush, float* outDepth,
                           float* ioT, int flags)
{
    if (*state == 0) {
        *ioT = FLOAT_80613840;
        collide_swept_sphere_vs_mesh(state, b, meshHdr, basePos, packed, face, noEdge,
                                     outNormal, outPush, outDepth, ioT, flags);
    } else {
        *ioT = FLOAT_8061382c;
        collide_sphere_vs_mesh_static(state, b, meshHdr, basePos, packed, edgeTable, face,
                                      noEdge, outNormal, outPush, outDepth, flags);
    }
}
