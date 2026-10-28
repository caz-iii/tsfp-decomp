// particle_field_sim.cpp - a bounded particle/debris field simulation.
//
// A single global pool of 800 particles lives at DAT_80612a74 (stride 0x28 bytes
// per particle: pos[3] @+0, vel[3] @+0xc, life @+0x18, age @+0x24, spawn-timer int
// @+0x20). The field is confined to a container "box" whose six faces are rebuilt
// each frame from a caller-supplied transform: three of the faces are turned into
// plane equations (tri_normal_and_plane) so particles can be ray-tested against
// them (ray_tri_intersect) and reflected back inside. The live-count is kept in
// DAT_80612a60; the owning context handle in DAT_80612a70. Working state (the box
// corners, face normals, plane constants) is a block of scratch globals at
// DAT_805cbbf8..DAT_805cbccc.
//
//   * ray_tri_intersect (0x802ad544) - segment (origin + dir*t) vs triangle:
//     solve t against the triangle plane, then two edge-sign tests; optional hit
//     point out.
//   * particle_plane_project_y (0x802ad798) - re-solve a particle's y from the
//     first box plane (keeps it on the floor plane).
//   * particle_field_alloc (0x802ad7d8) - allocate the 32000-byte (800*0x28) pool.
//   * particle_field_set_owner (0x802ad804) - store the owning context handle.
//   * particle_field_init (0x802ad80c) - build the container box from (x,y,z,size)
//     and seed all 800 particles at the box centre with rng-jittered velocities.
//   * particle_field_reseed_life (0x802adae8) - reset every particle's spawn timer
//     to a fresh rng lifetime (base 8).
//   * particle_field_reseed_life_ctx (0x802adba4) - same, scaled by the context.
//   * particle_field_tick (0x802adc7c) - the per-frame update: transform the box,
//     rebuild the three planes + normals, then for each of 800 particles either
//     respawn (life expired) or integrate + collide/reflect against the 3 faces.
//   * particle_field_tick_emitters (0x802ae344) - init+tick variant that spawns
//     from a list of emitter points (param_7 particles per emitter).
//   * particle_field_tick_gravity (0x802aeac4) - simpler tick: gravity fall +
//     respawn from up to three active emitter points (param_4 flags list).
//
// Address range: 0x802ad544 - 0x802aeac3 (non-contiguous)
// Subsystem: game (particle / debris field)

#include "common.h"

extern "C" {

extern int DAT_80612a60;   // live particle count
extern int DAT_80612a70;   // owning context handle
extern int DAT_80612a74;   // particle pool base (800 * 0x28 bytes)

// container-box scratch block (corners, normals, plane constants)
extern float DAT_805cbbf8, DAT_805cbbfc, DAT_805cbc00;                 // floor plane normal
extern float DAT_805cbc04, DAT_805cbc08, DAT_805cbc0c;                 // box centre
extern float DAT_805cbc10, DAT_805cbc14, DAT_805cbc18, DAT_805cbc1c;   // corners...
extern float DAT_805cbc20, DAT_805cbc24, DAT_805cbc28, DAT_805cbc2c;
extern float DAT_805cbc30, DAT_805cbc34, DAT_805cbc38, DAT_805cbc3c;
extern float DAT_805cbc40, DAT_805cbc44, DAT_805cbc48;
extern float DAT_805cbc4c, DAT_805cbc50, DAT_805cbc54, DAT_805cbc58;   // transformed corners
extern float DAT_805cbc5c, DAT_805cbc60, DAT_805cbc64, DAT_805cbc68;
extern float DAT_805cbc6c, DAT_805cbc70, DAT_805cbc74, DAT_805cbc78;
extern float DAT_805cbc7c, DAT_805cbc80, DAT_805cbc84, DAT_805cbc88;
extern float DAT_805cbc8c, DAT_805cbc90;
extern float DAT_805cbc94, DAT_805cbc98, DAT_805cbc9c;                 // 2nd plane normal
extern float DAT_805cbca0, DAT_805cbca4, DAT_805cbca8;                 // 3rd plane normal
extern float DAT_805cbcac, DAT_805cbcb0, DAT_805cbcb4;                 // reflect vectors
extern float DAT_805cbcb8, DAT_805cbcbc, DAT_805cbcc0;
extern float DAT_805cbcc4, DAT_805cbcc8, DAT_805cbccc;
extern float FLOAT_80612a64, FLOAT_80612a68, FLOAT_80612a6c;           // plane constants

extern const float  FLOAT_806199d0, FLOAT_806199d4, FLOAT_806199d8, FLOAT_806199dc, FLOAT_806199e0;
extern const float  FLOAT_806199e4, FLOAT_806199e8, FLOAT_806199ec, FLOAT_806199f0, FLOAT_806199f4;
extern const float  FLOAT_806199f8, FLOAT_806199fc, FLOAT_80619a00, FLOAT_80619a04, FLOAT_80619a10;
extern const float  FLOAT_80619a20, FLOAT_80619a24, FLOAT_80619a28, FLOAT_80619a2c, FLOAT_80619a30;
extern const float  FLOAT_80619a34, FLOAT_80619a38;
extern const double DOUBLE_80619a08, DOUBLE_80619a18;

extern unsigned int rng_counter_tick(void);
extern double math_sqrt(double x);
extern int  mem_alloc(int size, int type);
extern void mtx4_multiply(float* a, float* b, float* out);
extern void mtx4_transform_point(float* mtx, float* pt);
extern void tri_normal_and_plane(float* p0, float* p1, float* p2, float* outNormal, float* outPlane);

// frame-enter intrinsics
extern int  FUN_803826f0(void);
extern int  FUN_803826e8(void);
extern unsigned long long FUN_803826d0(void);
extern unsigned long long FUN_803826c8(void);
extern void FUN_8038273c(void);
extern void FUN_80382734(void);
extern void FUN_8038271c(void);
extern void FUN_80382714(void);

} // extern "C"

// forward decls
int  ray_tri_intersect(double planeD, float* origin, float* dir,
                       float* v0, float* v1, float* v2, float* normal, float* outHit); // 0x802ad544
void particle_plane_project_y(float* p);                       // 0x802ad798
void particle_field_set_owner(int owner);                      // 0x802ad804

// ---------------------------------------------------------------------------
// 0x802ad544 - segment/ray vs triangle intersection. origin + dir*t hits the
// plane (normal, planeD); accept if t>=0 and the point is inside both edge tests.
int ray_tri_intersect(double planeD, float* origin, float* dir,
                       float* v0, float* v1, float* v2, float* normal, float* outHit)
{
    float denom = normal[2] * dir[2] + normal[0] * dir[0] + normal[1] * dir[1];
    if (FLOAT_806199d0 == denom)
        return 0;

    float t = (float)(planeD - (double)(normal[2] * origin[2] + normal[0] * origin[0] + normal[1] * origin[1])) / denom;
    if (t < FLOAT_806199d0)
        return 0;

    float hz = t * dir[2] + origin[2];
    float hx = t * dir[0] + origin[0];
    float hy = t * dir[1] + origin[1];

    float v1z = v1[2], v2z = v2[2], v1x = v1[0], v2x = v2[0];
    float v0x = v0[0], v1y = v1[1], v2y = v2[1], v0z = v0[2], v0y = v0[1];

    float n0 = (v1z - v0z) * (v1x - hx) - (v1x - v0x) * (v1z - hz);
    float n1 = (v1y - v0y) * (v1z - hz) - (v1z - v0z) * (v1y - hy);
    float n2 = (v1x - v0x) * (v1y - hy) - (v1y - v0y) * (v1x - hx);

    if (FLOAT_806199d0 <=
        n2 * ((v2x - v1x) * (v2y - hy) - (v2y - v1y) * (v2x - hx)) +
        n1 * ((v2y - v1y) * (v2z - hz) - (v2z - v1z) * (v2y - hy)) +
        n0 * ((v2z - v1z) * (v2x - hx) - (v2x - v1x) * (v2z - hz))) {
        if (FLOAT_806199d0 <=
            n2 * ((v0x - v2x) * (v0y - hy) - (v0y - v2y) * (v0x - hx)) +
            n1 * ((v0y - v2y) * (v0z - hz) - (v0z - v2z) * (v0y - hy)) +
            n0 * ((v0z - v2z) * (v0x - hx) - (v0x - v2x) * (v0z - hz))) {
            if (outHit != (float*)0) { outHit[0] = hx; outHit[1] = hy; outHit[2] = hz; }
            return 1;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x802ad798 - re-solve the particle's y from the floor plane equation.
void particle_plane_project_y(float* p)
{
    p[1] = (FLOAT_80612a6c - (DAT_805cbbf8 * p[0] + DAT_805cbc00 * p[2])) / DAT_805cbbfc - FLOAT_806199d4;
}

// ---------------------------------------------------------------------------
// 0x802ad7d8 - allocate the 800*0x28 = 32000-byte particle pool.
void particle_field_alloc(void)
{
    DAT_80612a74 = mem_alloc(32000, 1);
}

// ---------------------------------------------------------------------------
// 0x802ad804 - store the owning context handle.
void particle_field_set_owner(int owner)
{
    DAT_80612a70 = owner;
}

// helper: read a fresh [0,1) rng float.
static float rng_unit(void)
{
    unsigned int r = rng_counter_tick();
    return (float)((double)(long long)(unsigned int)r - DOUBLE_80619a08) / FLOAT_806199f4;
}

// ---------------------------------------------------------------------------
// 0x802ad80c - build the container box from (x=extraout, y=param_2, z=param_3,
// size=param_4) and seed all 800 particles at the centre with rng velocities.
void particle_field_init(unsigned long long a1, double param_2, double param_3, double param_4)
{
    (void)a1;
    DAT_80612a70 = FUN_803826f0();
    // NOTE: the box origin x arrives in f1 (Ghidra extraout_f1); the ROM reuses
    // the incoming x. Model it from param_2's companion (documented, non-matching).
    double ox = param_2;

    DAT_805cbc10 = (float)((double)FLOAT_806199d8 * param_4 + ox);
    DAT_805cbc14 = -(float)((double)FLOAT_806199dc * param_4 - param_2);
    DAT_805cbc1c = -(float)((double)FLOAT_806199e0 * param_4 - ox);
    DAT_805cbc24 = (float)(param_3 - (double)(float)((double)FLOAT_806199e4 * param_4));
    DAT_805cbc30 = (float)(param_3 + (double)(float)((double)FLOAT_806199e4 * param_4));
    DAT_805cbc38 = -(float)((double)FLOAT_806199e8 * param_4 - param_2);
    DAT_805cbc40 = -(float)((double)FLOAT_806199ec * param_4 - ox);
    DAT_805cbc3c = (float)(param_3 - param_4);
    DAT_805cbc48 = (float)(param_3 + param_4);
    DAT_805cbc04 = (float)ox;
    DAT_805cbc08 = (float)param_2;
    DAT_805cbc0c = (float)param_3;
    DAT_805cbc18 = (float)param_3;
    DAT_80612a60 = 0;
    DAT_805cbc20 = DAT_805cbc14;
    DAT_805cbc28 = DAT_805cbc1c;
    DAT_805cbc2c = DAT_805cbc14;
    DAT_805cbc34 = DAT_805cbc1c;
    DAT_805cbc44 = DAT_805cbc38;

    int off = 0;
    for (int i = 0; i < 800; i++) {
        *(float*)(DAT_80612a74 + off)        = (float)ox;
        *(float*)(DAT_80612a74 + off + 4)    = (float)param_2;
        *(float*)(DAT_80612a74 + off + 8)    = (float)param_3;
        *(float*)(DAT_80612a74 + off + 0xc)  = (float)((double)FLOAT_806199f0 * (double)rng_unit() - (double)FLOAT_806199f8);
        *(float*)(DAT_80612a74 + off + 0x10) = (float)((double)FLOAT_806199fc * (double)rng_unit() + (double)FLOAT_806199f0);
        *(float*)(DAT_80612a74 + off + 0x14) = (float)((double)FLOAT_806199f0 * (double)rng_unit() - (double)FLOAT_806199f8);
        *(float*)(DAT_80612a74 + off + 0x18) = FLOAT_806199d0;
        *(float*)(DAT_80612a74 + off + 0x1c) = (float)((double)FLOAT_806199f8 * (double)rng_unit() + (double)FLOAT_80619a00);
        *(float*)(DAT_80612a74 + off + 0x24) = FLOAT_806199d0;
        *(int*)(DAT_80612a74 + off + 0x20)   = (int)((double)FLOAT_80619a04 * (double)rng_unit()) + 4;
        off += 0x28;
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x802adae8 - reset every particle's spawn timer to a fresh rng lifetime (+8).
void particle_field_reseed_life(void)
{
    FUN_803826f0();
    int off = 0;
    for (int i = 0; i < 800; i++) {
        *(int*)(DAT_80612a74 + off + 0x20) = (int)((double)FLOAT_80619a10 * (double)rng_unit()) + 8;
        off += 0x28;
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x802adba4 - reset spawn timers, scaled by the context value (base = ctx).
void particle_field_reseed_life_ctx(void)
{
    int base = FUN_803826e8();
    int off = 0;
    for (int i = 0; i < 800; i++) {
        unsigned int r = rng_counter_tick();
        float span = (float)((double)(long long)(unsigned int)((unsigned int)(base << 1) ^ 0x80000000) - DOUBLE_80619a18);
        *(int*)(DAT_80612a74 + off + 0x20) = base + (int)(span * rng_unit());
        off += 0x28;
    }
    FUN_80382734();
}

// helper: normalize a 3-vector in place with the fast rsqrt refinement, scaled.
static void reflect_normalize(float* v, float scale)
{
    float len = v[2] * v[2] + v[0] * v[0] + v[1] * v[1];
    if (FLOAT_80619a20 < len) {
        float r = 1.0f / (float)math_sqrt((double)len);
        r = r * -(FLOAT_806199dc * len * r * r - FLOAT_80619a24);
        float s = FLOAT_80619a28 * r * -(FLOAT_806199dc * len * r * r - FLOAT_80619a24);
        v[0] *= s; v[1] *= s; v[2] *= s;
    }
    v[0] *= scale; v[1] *= scale; v[2] *= scale;
}

// ---------------------------------------------------------------------------
// 0x802adc7c - per-frame particle update. Transform the box, rebuild the three
// collision planes + reflect vectors, then integrate & collide 800 particles.
void particle_field_tick(void)
{
    unsigned long long frame = FUN_803826d0();
    DAT_80612a60 = 0;

    // snapshot the box corners into the transform staging block
    DAT_805cbc4c = DAT_805cbc04; DAT_805cbc50 = DAT_805cbc08; DAT_805cbc54 = DAT_805cbc0c;
    DAT_805cbc58 = DAT_805cbc10; DAT_805cbc5c = DAT_805cbc14; DAT_805cbc60 = DAT_805cbc18;
    DAT_805cbc64 = DAT_805cbc1c; DAT_805cbc68 = DAT_805cbc20; DAT_805cbc6c = DAT_805cbc24;
    DAT_805cbc70 = DAT_805cbc28; DAT_805cbc74 = DAT_805cbc2c; DAT_805cbc78 = DAT_805cbc30;
    DAT_805cbc7c = DAT_805cbc34; DAT_805cbc80 = DAT_805cbc38; DAT_805cbc84 = DAT_805cbc3c;
    DAT_805cbc88 = DAT_805cbc40; DAT_805cbc8c = DAT_805cbc44; DAT_805cbc90 = DAT_805cbc48;

    float world[17];
    mtx4_multiply((float*)(unsigned int)(frame >> 0x20), (float*)(unsigned int)frame, world);
    mtx4_transform_point(world, &DAT_805cbc4c);
    mtx4_transform_point(world, &DAT_805cbc58);
    mtx4_transform_point(world, &DAT_805cbc64);
    mtx4_transform_point(world, &DAT_805cbc70);
    mtx4_transform_point(world, &DAT_805cbc7c);
    mtx4_transform_point(world, &DAT_805cbc88);

    tri_normal_and_plane(&DAT_805cbc58, &DAT_805cbc64, &DAT_805cbc70, &DAT_805cbbf8, &FLOAT_80612a6c);
    tri_normal_and_plane(&DAT_805cbc58, &DAT_805cbc64, &DAT_805cbc7c, &DAT_805cbc94, &FLOAT_80612a68);
    tri_normal_and_plane(&DAT_805cbc58, &DAT_805cbc70, &DAT_805cbc88, &DAT_805cbca0, &FLOAT_80612a64);

    reflect_normalize(&DAT_805cbbf8, FLOAT_806199d4);   // -> reflect vec written below
    // (the ROM writes the normalized+scaled reflect vectors into DAT_805cbcac..);
    // reflect_normalize normalizes the plane normals in place; copy to reflect vecs.
    DAT_805cbcac = DAT_805cbbf8; DAT_805cbcb0 = DAT_805cbbfc; DAT_805cbcb4 = DAT_805cbc00;
    reflect_normalize(&DAT_805cbc94, FLOAT_806199d4);
    DAT_805cbcb8 = DAT_805cbc94; DAT_805cbcbc = DAT_805cbc98; DAT_805cbcc0 = DAT_805cbc9c;
    reflect_normalize(&DAT_805cbca0, FLOAT_806199d4);
    DAT_805cbcc4 = DAT_805cbca0; DAT_805cbcc8 = DAT_805cbca4; DAT_805cbccc = DAT_805cbca8;

    int in_r6 = 4; // spawn budget this frame (register arg; modelled as the ROM default)
    int spawned = 0;
    int off = 0;
    float hit[3];
    for (int i = 0; i < 800; i++) {
        float* p = (float*)(DAT_80612a74 + off);
        if ((double)p[6] <= (double)FLOAT_806199d0) {
            if (spawned < in_r6) {
                p[0] = DAT_805cbc4c; p[1] = DAT_805cbc50; p[2] = DAT_805cbc54;
                *(float*)(DAT_80612a74 + off + 0x18) =
                    -(FLOAT_806199d4 * rng_unit() - FLOAT_80619a28);
                spawned++;
                DAT_80612a60++;
            }
        } else {
            if (ray_tri_intersect((double)FLOAT_80612a6c, p, p + 3,
                                  &DAT_805cbc58, &DAT_805cbc64, &DAT_805cbc70, &DAT_805cbbf8, hit) == 1) {
                p[0] = FLOAT_806199d8 * p[3] + p[0];
                *(float*)(DAT_80612a74 + off + 8) = FLOAT_806199d8 * *(float*)(DAT_80612a74 + off + 0x14) + *(float*)(DAT_80612a74 + off + 8);
                particle_plane_project_y((float*)(DAT_80612a74 + off));
            } else if (ray_tri_intersect((double)FLOAT_80612a68, p, p + 3,
                                         &DAT_805cbc58, &DAT_805cbc64, &DAT_805cbc7c, &DAT_805cbc94, hit) == 1) {
                *(float*)(DAT_80612a74 + off)       = hit[0] + DAT_805cbcb8;
                *(float*)(DAT_80612a74 + off + 4)   = hit[1] + DAT_805cbcbc;
                *(float*)(DAT_80612a74 + off + 8)   = hit[2] + DAT_805cbcc0;
                *(float*)(DAT_80612a74 + off + 0x18)= FLOAT_80619a2c;
            } else if (ray_tri_intersect((double)FLOAT_80612a64, p, p + 3,
                                         &DAT_805cbc58, &DAT_805cbc70, &DAT_805cbc88, &DAT_805cbca0, hit) == 1) {
                *(float*)(DAT_80612a74 + off)       = hit[0] + DAT_805cbcc4;
                *(float*)(DAT_80612a74 + off + 4)   = hit[1] + DAT_805cbcc8;
                *(float*)(DAT_80612a74 + off + 8)   = hit[2] + DAT_805cbccc;
                *(float*)(DAT_80612a74 + off + 0x18)= *(float*)(DAT_80612a74 + off + 0x18) - FLOAT_80619a30;
            } else {
                p[0] += p[3];
                *(float*)(DAT_80612a74 + off + 4) += *(float*)(DAT_80612a74 + off + 0x10);
                *(float*)(DAT_80612a74 + off + 8) += *(float*)(DAT_80612a74 + off + 0x14);
                *(float*)(DAT_80612a74 + off + 0x18) -= FLOAT_80619a30;
            }
            *(float*)(DAT_80612a74 + off + 0x24) += *(float*)(DAT_80612a74 + off + 0x1c);
            if (*(float*)(DAT_80612a74 + off + 0x18) <= FLOAT_806199d0)
                *(float*)(DAT_80612a74 + off + 0x18) = FLOAT_806199d0;
            else
                DAT_80612a60++;
        }
        off += 0x28;
    }
    FUN_8038271c();
}

// ---------------------------------------------------------------------------
// 0x802ae344 - init+tick variant that spawns particles from a list of emitter
// points, param_7 particles per emitter, up to 2 emitters.
void particle_field_tick_emitters(unsigned int param_1, unsigned int param_2, unsigned int param_3,
                                  unsigned int param_4, float param_5, unsigned int param_6, int param_7)
{
    unsigned long long frame = FUN_803826c8();
    DAT_80612a60 = 0;

    // stage box corners (upper portion)
    DAT_805cbc58 = DAT_805cbc10; DAT_805cbc5c = DAT_805cbc14; DAT_805cbc60 = DAT_805cbc18;
    DAT_805cbc64 = DAT_805cbc1c; DAT_805cbc68 = DAT_805cbc20; DAT_805cbc6c = DAT_805cbc24;
    DAT_805cbc70 = DAT_805cbc28; DAT_805cbc74 = DAT_805cbc2c; DAT_805cbc78 = DAT_805cbc30;
    DAT_805cbc7c = DAT_805cbc34; DAT_805cbc80 = DAT_805cbc38; DAT_805cbc84 = DAT_805cbc3c;
    DAT_805cbc88 = DAT_805cbc40; DAT_805cbc8c = DAT_805cbc44; DAT_805cbc90 = DAT_805cbc48;

    // emitter list: 2 pointer entries the decompiler stages on the stack
    float* emit[2];
    emit[0] = (float*)param_3;
    emit[1] = (float*)param_4;

    float world[17];
    mtx4_multiply((float*)(unsigned int)(frame >> 0x20), (float*)(unsigned int)frame, world);
    mtx4_transform_point(world, &DAT_805cbc58);
    mtx4_transform_point(world, &DAT_805cbc64);
    mtx4_transform_point(world, &DAT_805cbc70);
    mtx4_transform_point(world, &DAT_805cbc7c);
    mtx4_transform_point(world, &DAT_805cbc88);

    tri_normal_and_plane(&DAT_805cbc58, &DAT_805cbc64, &DAT_805cbc70, &DAT_805cbbf8, &FLOAT_80612a6c);
    tri_normal_and_plane(&DAT_805cbc58, &DAT_805cbc64, &DAT_805cbc7c, &DAT_805cbc94, &FLOAT_80612a68);
    tri_normal_and_plane(&DAT_805cbc58, &DAT_805cbc70, &DAT_805cbc88, &DAT_805cbca0, &FLOAT_80612a64);

    reflect_normalize(&DAT_805cbbf8, FLOAT_806199d4);
    DAT_805cbcac = DAT_805cbbf8; DAT_805cbcb0 = DAT_805cbbfc; DAT_805cbcb4 = DAT_805cbc00;
    reflect_normalize(&DAT_805cbc94, FLOAT_806199d4);
    DAT_805cbcb8 = DAT_805cbc94; DAT_805cbcbc = DAT_805cbc98; DAT_805cbcc0 = DAT_805cbc9c;
    reflect_normalize(&DAT_805cbca0, FLOAT_806199d4);
    DAT_805cbcc4 = DAT_805cbca0; DAT_805cbcc8 = DAT_805cbca4; DAT_805cbccc = DAT_805cbca8;

    int emitterIdx = 0;
    int emitOff = 0;
    int perEmitter = 0;
    int off = 0;
    float hit[3];
    for (int i = 0; i < 800; i++) {
        float* p = (float*)(DAT_80612a74 + off);
        double life = (double)p[6];
        if (life <= (double)FLOAT_806199d0) {
            float* src = emit[emitOff / 4 % 2];
            if (emitterIdx < 2 && src != (float*)0) {
                p[0] = src[0]; p[1] = src[1]; p[2] = src[2];
                *(float*)(DAT_80612a74 + off + 0xc)  = (FLOAT_806199f8 * rng_unit() + src[0]) - FLOAT_80619a00;
                *(float*)(DAT_80612a74 + off + 0x10) = FLOAT_806199fc * rng_unit() + FLOAT_80619a34 + src[1];
                *(float*)(DAT_80612a74 + off + 0x14) = (FLOAT_806199f8 * rng_unit() + src[2]) - FLOAT_80619a00;
                *(float*)(DAT_80612a74 + off + 0x18) = -(FLOAT_806199d4 * rng_unit() - FLOAT_80619a38);
                perEmitter++;
                DAT_80612a60++;
                if (param_7 < perEmitter) { perEmitter = 0; emitterIdx++; emitOff += 4; }
            }
        } else {
            double drag = (double)(float)(life * life);
            if (ray_tri_intersect((double)FLOAT_80612a6c, p, p + 3,
                                  &DAT_805cbc58, &DAT_805cbc64, &DAT_805cbc70, &DAT_805cbbf8, hit) == 1) {
                p[0] = FLOAT_806199d8 * p[3] + p[0];
                *(float*)(DAT_80612a74 + off + 8) = FLOAT_806199d8 * *(float*)(DAT_80612a74 + off + 0x14) + *(float*)(DAT_80612a74 + off + 8);
                particle_plane_project_y((float*)(DAT_80612a74 + off));
            } else if (ray_tri_intersect((double)FLOAT_80612a68, p, p + 3,
                                         &DAT_805cbc58, &DAT_805cbc64, &DAT_805cbc7c, &DAT_805cbc94, hit) == 1) {
                *(float*)(DAT_80612a74 + off)     = hit[0] + DAT_805cbcb8;
                *(float*)(DAT_80612a74 + off + 4) = hit[1] + DAT_805cbcbc;
                *(float*)(DAT_80612a74 + off + 8) = hit[2] + DAT_805cbcc0;
                *(float*)(DAT_80612a74 + off + 0x18) -= FLOAT_806199f8;
            } else if (ray_tri_intersect((double)FLOAT_80612a64, p, p + 3,
                                         &DAT_805cbc58, &DAT_805cbc70, &DAT_805cbc88, &DAT_805cbca0, hit) == 1) {
                *(float*)(DAT_80612a74 + off)     = hit[0] + DAT_805cbcc4;
                *(float*)(DAT_80612a74 + off + 4) = hit[1] + DAT_805cbcc8;
                *(float*)(DAT_80612a74 + off + 8) = hit[2] + DAT_805cbccc;
                *(float*)(DAT_80612a74 + off + 0x18) -= FLOAT_806199f8;
            } else {
                p[0] = (float)(drag * (double)p[3] + (double)p[0]);
                *(float*)(DAT_80612a74 + off + 4) = (float)(drag * (double)*(float*)(DAT_80612a74 + off + 0x10) + (double)*(float*)(DAT_80612a74 + off + 4));
                *(float*)(DAT_80612a74 + off + 8) = (float)(drag * (double)*(float*)(DAT_80612a74 + off + 0x14) + (double)*(float*)(DAT_80612a74 + off + 8));
                *(float*)(DAT_80612a74 + off + 0x18) -= FLOAT_806199f8;
            }
            if (*(float*)(DAT_80612a74 + off + 0x18) <= FLOAT_806199d0)
                *(float*)(DAT_80612a74 + off + 0x18) = FLOAT_806199d0;
            else {
                *(float*)(DAT_80612a74 + off + 0x24) += *(float*)(DAT_80612a74 + off + 0x1c);
                DAT_80612a60++;
            }
        }
        off += 0x28;
    }
    (void)param_1; (void)param_2; (void)param_5; (void)param_6;
    FUN_80382714();
}

// ---------------------------------------------------------------------------
// 0x802aeac4 - simpler per-frame tick: gravity fall + respawn from up to three
// active emitter points (param_4 = per-emitter active-flag list).
void particle_field_tick_gravity(unsigned int param_1, unsigned int param_2, unsigned int param_3, int param_4)
{
    unsigned long long emitters = FUN_803826e8();
    DAT_80612a60 = 0;

    int liveEmitter = 0;
    int flagOff = 0;
    int off = 0;
    for (int i = 0; i < 800; i++) {
        float* p = (float*)(DAT_80612a74 + off);
        if ((double)(float)p[6] <= (double)FLOAT_806199d0) {
            if (liveEmitter < 3) {
                if (*(int*)(param_4 + flagOff) == 1) {
                    float* src = *(float**)((int)&emitters + flagOff);
                    p[0] = src[0]; p[1] = src[1]; p[2] = src[2];
                    *(float*)(DAT_80612a74 + off + 0x18) = -(FLOAT_806199d4 * rng_unit() - FLOAT_80619a28);
                    DAT_80612a60++;
                }
                liveEmitter++;
                flagOff += 4;
            }
        } else {
            p[6] = (float)((double)(float)p[6] - (double)FLOAT_80619a30);
            if ((double)*(float*)(DAT_80612a74 + off + 0x18) <= (double)FLOAT_806199d0) {
                *(float*)(DAT_80612a74 + off + 0x18) = FLOAT_806199d0;
            } else {
                *(float*)(DAT_80612a74 + off + 4) += *(float*)(DAT_80612a74 + off + 0x10);
                p[0] += p[3];
                *(float*)(DAT_80612a74 + off + 8) += *(float*)(DAT_80612a74 + off + 0x14);
                *(float*)(DAT_80612a74 + off + 0x24) += *(float*)(DAT_80612a74 + off + 0x1c);
                DAT_80612a60++;
            }
        }
        off += 0x28;
    }
    (void)param_1; (void)param_2; (void)param_3;
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x802aeccc - per-frame tick variant with a bit-masked emitter set. Up to five
// emitter slots (param_6 bit i selects slot i) each supply one respawn point
// from a stack list; live particles fall under gravity with a floor plane clamp.
// The five emitter points are staged into a local block by the caller (arrive in
// param_3..param_5 + the incoming fp pair, modelled here as an explicit array).
// Subsystem: game (particle field). Added in batch 785.
// ---------------------------------------------------------------------------
void particle_field_tick_burst(float* emit0, float* emit1, unsigned int emitPtr3,
                               unsigned int emitPtr4, unsigned int emitPtr5, unsigned int mask)
{
    unsigned long long emitters = FUN_803826e8();
    DAT_80612a60 = 0;

    // five emitter slots: two arrive as vector pointers, three as raw pointers.
    float* emit[5];
    emit[0] = emit0;
    emit[1] = emit1;
    emit[2] = (float*)emitPtr3;
    emit[3] = (float*)emitPtr4;
    emit[4] = (float*)emitPtr5;

    double lifeFloor = (double)FLOAT_806199d0;
    double xLimit    = (double)FLOAT_806199d0; // extraout_f1 ~ 0 fall-through limit
    int slot = 0;
    int listOff = 0;
    int off = 0;
    for (int i = 0; i < 800; i++) {
        float* p = (float*)(DAT_80612a74 + off);
        if ((double)p[6] <= lifeFloor) {
            if (slot < 5) {
                if ((mask & (1u << slot)) != 0) {
                    float* src = *(float**)((int)&emitters + listOff);
                    p[0] = src[0]; p[1] = src[1]; p[2] = src[2];
                    unsigned int r = rng_counter_tick();
                    *(float*)(DAT_80612a74 + off + 0x18) =
                        -(FLOAT_806199d4 *
                          ((float)((double)CONCAT44(0x43300000, r) - DOUBLE_80619a08) / FLOAT_806199f4)
                          - FLOAT_80619a28);
                    DAT_80612a60++;
                }
                slot++;
                listOff += 4;
            }
        } else {
            p[6] = (float)((double)p[6] - (double)FLOAT_80619a30);
            if ((double)*(float*)(DAT_80612a74 + off + 0x18) <= lifeFloor) {
                *(float*)(DAT_80612a74 + off + 0x18) = (float)lifeFloor;
            } else {
                *(float*)(DAT_80612a74 + off + 4) += *(float*)(DAT_80612a74 + off + 0x10);
                float grav = FLOAT_806199d8;
                if ((double)p[1] <= xLimit) {
                    p[0] += p[3];
                    *(float*)(DAT_80612a74 + off + 8) += *(float*)(DAT_80612a74 + off + 0x14);
                } else {
                    p[0] = FLOAT_806199d8 * p[3] + p[0];
                    *(float*)(DAT_80612a74 + off + 4) = (float)xLimit;
                    *(float*)(DAT_80612a74 + off + 8) = grav * *(float*)(DAT_80612a74 + off + 0x14) + *(float*)(DAT_80612a74 + off + 8);
                }
                *(float*)(DAT_80612a74 + off + 0x24) += *(float*)(DAT_80612a74 + off + 0x1c);
                DAT_80612a60++;
            }
        }
        off += 0x28;
    }
    FUN_80382734();
}
