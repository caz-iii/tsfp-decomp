// Address range: 0x80146cb0 - 0x801488f4 | batch_413 | 2026-09-05
//
// Entity render sub-object COLLISION / contact generation + resolution
// (companion to subobj_pool/build/dynamics/solver).
//
// This layer sits between the entity broad-phase (entity_query_aabb over the
// per-room entity id lists) and the constraint solver from batch_412. It:
//   * emits ambient positional sound as a sub-object ages (subobj_ambient_sound_tick),
//   * runs the per-frame area-effect broad-phase over the active entity list
//     (subobj_area_effect_broadphase),
//   * dispatches per-shape collision queries against neighbouring entities,
//     choosing raycast / capsule-sweep / swept-cast per shape record type
//     (subobj_collision_query_* ),
//   * generates contact constraints (point / OBB / capsule variants) and pushes
//     them onto a caller list (subobj_collision_gen_constraints_*), and
//   * resolves accumulated contacts into linear + angular impulse on one or two
//     sub-object descriptors (subobj_resolve_contact_single / _pair).
//
// Descriptor float-word offsets used here (word = byte/4):
//   [0x50..0x52] world center      [0x53..0x55] linear velocity
//   [0x59..0x5b] local center      [0x5f..0x61] local extent
//   [0x70]       flags word (byte 0x1c0)
//   [0x79]       own packed handle
//   +0x11c..+0x124 torque accum    +0x128..+0x130 angular-impulse accum
//   +0x134..+0x13c contact normal  +0x140..+0x148 contact/world origin
//   +0x14c..+0x154 linear-impulse accum
//   +0x1a8 inverse mass  +0x1c0 flags  +0x1c9/+0x1ca collision masks
//   +0x1d4 last-tick stamp  +0x1d8 ambient gain  +0x1e8 contact filter callback
// Mesh shape records are 0xc0-byte strips; each strip holds a table of 0x1c-byte
// shape entries at +0x80 whose word[0] selects sphere(<=0)/capsule(1)/segment(2).

#include "common.h"
#include "generated_decls.h"

extern "C" {
// spawn-frame push/pop variants (each pop matches its push)
extern unsigned long long FUN_803826c8(void); extern void FUN_80382714(void);
extern unsigned long long FUN_803826d8(void); extern void FUN_80382724(void);
extern unsigned long long FUN_803826bc(void); extern void FUN_80382708(void);
extern unsigned long long FUN_803826c4(void); extern void FUN_80382710(void);
extern unsigned long long FUN_803826b4(void); extern void FUN_80382700(void);
extern unsigned long long FUN_803826dc(void); extern void FUN_80382728(void);
extern unsigned int       __save_gpr_28(void); extern void __restore_gpr_28(void);

// unnamed cross-module helpers (self-externed with call-site arity)
extern int    FUN_802523ac(unsigned int soundGroup, int entity, int* outLo, int* outHi, float* outScale);
extern int    FUN_80364c40(int* p);          // culling / visibility gate
extern int    FUN_80151f54(int* actor);      // line-of-sight / owner test
extern unsigned int FUN_80159ff4(int entity);// entity capability flags
extern int    FUN_801586d4(int entityA, int entityB);
extern double FUN_80158740(double a, double b, int entityA, int entityB, int mode);
extern unsigned int FUN_801494e0(unsigned int handle); // parent/self handle resolve
extern double FUN_80149540(unsigned int handle);        // inertia / spin factor
extern void   FUN_80147c50(void);            // raycast contact gen (frame-driven, no explicit args)
}

// pool tables (shared)
extern int DAT_8061218c;   // descriptor pool base (stride 0x200)
extern int DAT_80612184;   // per-slot owner-entity pointer table
extern int DAT_80588610;   // active entity record list base (stride 4 of int*)
extern int DAT_80612098;   // active entity record count
extern int DAT_80611928;   // current frame counter
extern int DAT_806109c0;   // wind/time phase counter
extern int DAT_80612610;   // frame gate threshold
extern char DAT_805015bd;  // coop/large-arena gate byte
extern float DAT_8048ca94; // area-effect radius table (stride 0x30)
extern float DAT_8048ca9c; // area-effect height table (stride 0x30)

// shared float / double constants
extern float  FLOAT_80615ea8;   // 0.0f
extern float  FLOAT_80615eb0;   // 1.0f
extern float  FLOAT_80615ebc;   // pad / half-extent factor
extern float  FLOAT_80615ec4;   // normalize epsilon
extern float  FLOAT_80615ec8;   // 1.5 (NR rsqrt refine)
extern float  FLOAT_80615ee4;   // min age threshold
extern float  FLOAT_80615ee8;   // snap / clamp threshold
extern float  FLOAT_80615eec;   // ambient volume bias
extern float  FLOAT_80615ef0;   // ambient volume scale
extern float  FLOAT_80615ef4;   // area-effect z-extent factor
extern float  FLOAT_80615ef8;   // velocity magnitude gate
extern float  FLOAT_80615efc;   // bound-radius divisor
extern float  FLOAT_80615f04;   // spin torque scale
extern double DOUBLE_80615ed8;  // int->double conversion bias

// Geometry contact helpers: the shared decls were fixed by earlier callers and
// don't match every call-site here, so call through typed aliases.
typedef int (*obb_pt_fn)(float* a, float* b, float* half, float* mtx, float* outPt, float* outN, float* outDepth);
typedef int (*sphere_pt_fn)(double rA, double rB, float* a, float* pos, float* outPt, float* outN, float* outDepth);
typedef int (*sphere_seg_fn)(float* a, float* pos, float* segT, int mtx, float* outPt, float* outN, float* outDepth);
typedef int (*sat_fn)(float* pos, int* half, float* mA, float* mB, float* p3, float* p4, float* outPts, float* outN, float* outDepths, int maxPts, int stride);
typedef int (*sphere_edge_fn)(float* pos, int* half, float* mA, float* mB, float* seg, int mtx, float* outPts, float* outN, float* outDepths, int maxPts, int stride);
typedef int (*capsule_fn)(float* pos, int* half, int mtxA, float* mB, float* seg, int mtxB, float* outPts, float* outN, float* outDepths);
typedef int (*apply_torque_fn)(unsigned int h0, float* lever, float* axis);
typedef int (*apply_pair_fn)(unsigned int h0, unsigned int h1, float* leverA, float* leverB, float* axis);

#define CLOSEST_PT_ON_OBB       ((obb_pt_fn)&closest_pt_on_obb)
#define SPHERE_SWEEP_VS_POINT   ((sphere_pt_fn)&sphere_sweep_vs_point)
#define SPHERE_VS_SEGMENT       ((sphere_seg_fn)&sphere_vs_segment_contact)
#define SAT_OBB_VS_OBB          ((sat_fn)&sat_obb_vs_obb)
#define SPHERE_VS_OBB_EDGES     ((sphere_edge_fn)&sphere_vs_obb_edges)
#define CAPSULE_VS_CAPSULE      ((capsule_fn)&capsule_vs_capsule_contact)
#define APPLY_TORQUE_IMPULSE    ((apply_torque_fn)&subobj_constraint_apply_torque_impulse)
#define APPLY_PAIR_IMPULSE      ((apply_pair_fn)&subobj_constraint_apply_pair_impulse)

// Broad/narrow-phase collision entry points: the shared decls were fixed by
// earlier callers with different arity; call through typed aliases matching the
// call sites in this module.
typedef int  (*query_aabb_fn)(int room, float* aabb, int* out, int start, int maxCount, unsigned int mask);
typedef void (*swept_default_fn)(int entity4, float* pos, float* half, float* mtx, int a, int owner, int* list, int b, int flags);
typedef void (*swept_cast_fn)(int entity4, float* origin, float* dir, int a, int owner, int* list, int inclTerrain, int flags);
typedef void (*capsule_sweep_fn)(int entity4, float* pos, float* half, float* mtx, int shape, int owner, int* list, int inclTerrain, int flags);
typedef void (*bvh_multi_fn)(int entity4, float* pos, int shape, int owner, int* list, int inclTerrain, int flags);
typedef void (*ray_sub_fn)(int hitEnt, float* pos, unsigned short shape, int owner, int* list);
typedef void (*ray_sub_multi_fn)(int hitEnt, float* pos, float* half, float* mtx, unsigned short shape, int owner, int* list);
typedef void (*swept_sub_fn)(int hitEnt, float* pos, float* half, int mtx, unsigned short shape, int owner, int* list);

#define QUERY_AABB       ((query_aabb_fn)&entity_query_aabb)
#define SWEPT_DEFAULT    ((swept_default_fn)&collision_entity_swept_cast_default_dir)
#define SWEPT_CAST       ((swept_cast_fn)&collision_entity_swept_cast)
#define CAPSULE_SWEEP    ((capsule_sweep_fn)&collision_entity_capsule_sweep)
#define BVH_MULTI        ((bvh_multi_fn)&collision_bvh_multi_hit_query)
#define RAY_SUB          ((ray_sub_fn)&entity_raycast_sub_objects)
#define RAY_SUB_MULTI    ((ray_sub_multi_fn)&entity_raycast_sub_objects_multi)
#define SWEPT_SUB        ((swept_sub_fn)&entity_swept_cast_sub_objects)

// forward decls (intra-file)
void subobj_collision_gen_constraints_obb(unsigned int shapeA, float* base, float* segT2,
                                          float* mtx, unsigned int strip, int entity, int* list);
void subobj_collision_gen_constraints_capsule(unsigned int shapeA, float* base, float* segT2,
                                              int mtx, unsigned int strip, int entity, int* list);

// ============================================================================
// 0x80146cb0  subobj_ambient_sound_tick
// If a sub-object exists and enough age has elapsed, ramp its ambient gain
// (+0x1d8) toward 1.0, then resolve the entity's ambient sound bank and, when
// the listener is in range, play a positional ambient sound scaled by dt.
// ============================================================================
void subobj_ambient_sound_tick(double dt, double maxRate, int entity)
{
    int   desc;
    float gain, one, minAge;
    int   found;
    int   lo, hi;
    float scale[2];
    double vol, worldVol;

    minAge = FLOAT_80615ee4;
    one = FLOAT_80615eb0;

    if (*(unsigned int*)(entity + 0x80) == 0) {
        desc = 0;
    } else {
        desc = DAT_8061218c + (*(unsigned int*)(entity + 0x80) & 0xffff) * 0x200;
    }
    if (dt <= (double)FLOAT_80615ea8) {
        return;
    }
    if ((desc != 0) && ((double)FLOAT_80615ea8 < dt)) {
        float phase = (float)((double)CONCAT44(0x43300000, DAT_806109c0) - DOUBLE_80615ed8);
        float ageInv = FLOAT_80615eb0 / phase;
        unsigned int delta = (DAT_80611928 - *(int*)(desc + 0x1d4)) ^ 0x80000000;
        *(int*)(desc + 0x1d4) = DAT_80611928;
        ageInv = ageInv * (float)((double)CONCAT44(0x43300000, delta) - DOUBLE_80615ed8);
        if (ageInv < minAge) {
            return;
        }
        if (ageInv <= FLOAT_80615ebc) {
            dt = (double)(float)(dt * (double)*(float*)(desc + 0x1d8));
        } else {
            *(float*)(desc + 0x1d8) = one;
        }
        scalar_step_toward_snap((double)FLOAT_80615ea8, (double)FLOAT_80615ee8, (float*)(desc + 0x1d8));
        if (dt <= (double)FLOAT_80615ea8) {
            return;
        }
    }
    found = FUN_802523ac((unsigned int)*(unsigned short*)(entity + 0x94),
                         *(int*)(entity + 0x20), &lo, &hi, scale);
    if (found != 0) {
        vol = (double)(float)(dt * (double)scale[0]);
        worldVol = (double)(float)((double)FLOAT_80615ef0 * vol + (double)FLOAT_80615eec);
        if (sound_is_position_in_range(worldVol, (float*)(entity + 100))) {
            if (maxRate < vol) {
                vol = maxRate;
            }
            (void)gain;
            sound_play_ambient_3d(worldVol, vol);
        }
    }
}

// ============================================================================
// 0x80146e90  subobj_area_effect_broadphase
// Per-frame: walk the active entity record list (DAT_80588610), and for each
// visible entity with an area-effect flag build an AABB from its area-effect
// radius/height tables, query neighbouring entities (entity_query_aabb over the
// entity's room list), and for each hit dispatch a swept collision test with
// gating on distance/type. Frame-scoped via FUN_803826c8.
// ============================================================================
void subobj_area_effect_broadphase(void)
{
    int*  resultList;
    int   i, rec;
    int*  ent;
    int   entity;
    unsigned int flag;

    resultList = (int*)(unsigned long long)FUN_803826c8();
    rec = 0;
    for (i = 0; i < DAT_80612098; ++i) {
        ent = *(int**)((int)&DAT_80588610 + rec);
        entity = *ent;
        if ((FUN_80364c40(ent + 0x13) == 0) &&
            ((*(unsigned int*)(entity + 0x28) & 0x60) != 0) &&
            ((ent[9] & 8U) == 0)) {

            flag = (DAT_805015bd == '\n') ? (unsigned int)ent[5] : (unsigned int)ent[7];
            if (((flag & 1) == 0) && ((ent[0x2ab] & 1U) == 0)) {
                float cx = *(float*)(entity + 100)  + *(float*)(entity + 0x1a8);
                float cy = *(float*)(entity + 0x68) + *(float*)(entity + 0x1ac);
                float cz = *(float*)(entity + 0x6c) + *(float*)(entity + 0x1b0);
                short defIdx = *(short*)(ent + 0x12);
                float zext = FLOAT_80615ef4 * *(float*)(&DAT_8048ca9c + defIdx * 0x30);
                float rext = *(float*)(&DAT_8048ca94 + defIdx * 0x30) * FLOAT_80615ebc;
                float aabb[6];    // local_13c..local_128 (min xyz, max xyz interleaved as decompiled)
                int   hits[39];   // local_e4
                int   count, j, k;

                aabb[0] = cx - zext; aabb[3] = cx + zext;   // x
                aabb[1] = cy - rext; aabb[4] = cy + rext;   // y
                aabb[2] = cz - zext; aabb[5] = cz + zext;   // z

                count = 0; k = 0;
                for (j = 0; j < *(char*)(entity + 0x30); ++j) {
                    int room = *(int*)(entity + k + 0x38);
                    if ((0 < room) && (room < 0xc9)) {
                        count = QUERY_AABB(room, aabb, hits, count, 0x14, 0x7a);
                    }
                    k += 4;
                }
                k = 0;
                for (j = 0; j < count; ++j) {
                    int hitEnt = *(int*)((int)hits + k);
                    unsigned int handle = *(unsigned int*)(hitEnt + 0x80);
                    if (handle != 0) {
                        bool skip = false;
                        int hdesc = DAT_8061218c + (handle & 0xffff) * 0x200;
                        if (*(int*)(hitEnt + 0x20) == 0x400000) {
                            int los;
                            if ((((ent == 0) || ((los = *ent) == 0)) ||
                                 ((ent[2] < DAT_80612610 ||
                                   (((*(unsigned int*)(los + 0x28) & 0x100) == 0) &&
                                    ((*(unsigned int*)(los + 0x10) & 0xffff) != 0))))) &&
                                ((los = FUN_80151f54(ent)) == 0)) {
                                unsigned int cap = FUN_80159ff4(hitEnt);
                                if (((cap & 0x10000) != 0) && (FUN_801586d4(hitEnt, entity) != 0)) {
                                    FUN_80158740((double)aabb[0], (double)aabb[3], hitEnt, entity, 1);
                                    skip = true;
                                }
                            } else {
                                skip = true;
                            }
                        } else {
                            skip = (*(unsigned int*)(hdesc + 0x1c0) & 0x1000100) == 0;
                            if (((handle & 0x80000000) != 0) &&
                                ((double)FLOAT_80615ef8 <= (double)ABS((float)cz))) {
                                skip = true;
                            }
                            unsigned int fl = *(unsigned int*)(hdesc + 0x1c0);
                            if ((((fl & 0x200000) != 0) && ((fl & 0x1000000) == 0)) &&
                                ((double)FLOAT_80615ef8 <= (double)ABS((float)cz))) {
                                skip = true;
                            }
                        }
                        if (!skip) {
                            float mtx[16];
                            float pos[3];   // local_148..local_140
                            float half[3];  // local_150, local_14c, (implicit)
                            mtx4_set_identity((int)mtx);
                            pos[0] = cx; pos[1] = cy; pos[2] = cz;
                            half[0] = zext;
                            half[1] = (float)((double)FLOAT_80615ebc *
                                              (double)*(float*)(&DAT_8048ca94 + defIdx * 0x30) + (double)zext);
                            half[2] = zext;
                            if ((*(unsigned int*)(hitEnt + 0x28) & 0x8000) == 0) {
                                SWEPT_DEFAULT(
                                    *(int*)(hitEnt + 4), pos, half, mtx, 0, entity, resultList, 0, 0x24000);
                            } else {
                                subobj_collision_gen_constraints_capsule(
                                    *(unsigned int*)(hitEnt + 0x80), pos, half, (int)mtx, 0, entity, resultList);
                            }
                        }
                    }
                    k += 4;
                }
            }
        }
        rec += 4;
    }
    FUN_80382714();
}

// ============================================================================
// 0x801472b4  subobj_collision_mask_test
// Return maskBits & the sub-object's collision-group byte (+0x1c9); returns 1
// (pass) when either arg is zero.
// ============================================================================
unsigned int subobj_collision_mask_test(unsigned int maskBits, unsigned int handle)
{
    if ((handle != 0) && (maskBits != 0)) {
        return maskBits & *(unsigned char*)(DAT_8061218c + (handle & 0xffff) * 0x200 + 0x1c9);
    }
    return 1;
}

// ============================================================================
// 0x801472e4  subobj_collision_query_swept
// Frame entry (FUN_803826d8): build the sub-object's swept AABB from its center
// and velocity, query neighbouring entities per room, and for each hit that
// passes the group/mask filters run a swept cast.
// ============================================================================
void subobj_collision_query_swept(unsigned int dummy0, unsigned int dummy1, int entity)
{
    unsigned long long frame;
    float* desc;
    float  ownHandle;
    unsigned int listType;
    double boundR;
    float  vel[3];      // local_c8/c4/c0
    float  center[3];   // local_b0/ac/a8
    float  bc[3];       // local_bc/b8/b4 (center - localCenter)
    float  aabb[6];     // local_a4..local_90
    int    hits[33];    // local_8c
    int    count, j, k;
    double lo0, lo1, lo2, hi0, hi1, hi2, dt;

    (void)dummy0; (void)dummy1;
    frame = FUN_803826d8();
    desc = (float*)(unsigned int)frame;
    ownHandle = desc[0x79];
    listType = 0x38;
    boundR = (double)((desc[0x61] + desc[0x5f] + desc[0x60]) / FLOAT_80615efc);
    if (((unsigned int)desc[0x70] & 0x20000) != 0) {
        listType = 0x3a;
    }
    dt = 1.0;   // extraout_f1 (frame dt) — treated as unit here; velocity already scaled
    vel[0] = (float)((double)desc[0x53] * dt);
    vel[1] = (float)((double)desc[0x54] * dt);
    vel[2] = (float)((double)desc[0x55] * dt);
    center[0] = desc[0x59]; center[1] = desc[0x5a]; center[2] = desc[0x5b];
    mtx4_transform_vector_alias(desc, center);
    bc[0] = desc[0x50] - center[0];
    bc[1] = desc[0x51] - center[1];
    bc[2] = desc[0x52] - center[2];

    lo0 = (double)bc[0]; hi0 = (double)(float)(lo0 + (double)vel[0]);
    lo1 = (double)bc[1]; hi1 = (double)(float)(lo1 + (double)vel[1]);
    lo2 = (double)bc[2]; hi2 = (double)(float)(lo2 + (double)vel[2]);

    aabb[0] = (float)((lo0 < hi0 ? lo0 : hi0) - boundR);
    aabb[1] = (float)((lo1 < hi1 ? lo1 : hi1) - boundR);
    aabb[2] = (float)((lo2 < hi2 ? lo2 : hi2) - boundR);
    aabb[3] = (float)(boundR + (hi0 < lo0 ? lo0 : hi0));
    aabb[4] = (float)(boundR + (hi1 < lo1 ? lo1 : hi1));
    aabb[5] = (float)(boundR + (hi2 < lo2 ? lo2 : hi2));

    count = 0;
    k = 0;
    for (j = 0; j < *(char*)(entity + 0x30); ++j) {
        int room = *(int*)(entity + k + 0x38);
        if ((0 < room) && (room < 0xc9)) {
            count = QUERY_AABB(room, aabb, hits, count, 0x14, listType);
        }
        k += 4;
    }
    k = 0;
    for (j = 0; j < count; ++j) {
        int hitEnt = *(int*)((int)hits + k);
        unsigned int handle = *(unsigned int*)(hitEnt + 0x80);
        if (((((handle & 0xffff) <= ((unsigned int)ownHandle & 0xffff)) ||
              ((handle != 0) && (FUN_801494e0(handle) != 0))) &&
             (subobj_collision_mask_test((unsigned int)*(unsigned char*)((int)desc + 0x1ca),
                                         *(unsigned int*)(hitEnt + 0x80)) != 0)) &&
            ((*(int*)(hitEnt + 0x80) != 0) || (((unsigned int)desc[0x70] & 0x40) == 0))) {
            SWEPT_CAST(
                *(int*)(*(int*)((int)hits + k) + 4), bc, vel, 0, entity,
                (int*)(unsigned int)(frame >> 0x20), 1, 0x24000);
        }
        k += 4;
    }
    FUN_80382724();
}

// ============================================================================
// 0x80147590  subobj_collision_query_dispatch
// Frame entry (FUN_803826bc): compute the sub-object's combined shape AABB by
// transforming every shape entry of its mesh strips, broad-phase query the room
// entity lists, then for each hit choose a per-mode handler (0 = sub-object,
// 1 = mesh, 2 = generic) and per-shape-type collision routine (raycast /
// capsule sweep / swept cast).
// ============================================================================
void subobj_collision_query_dispatch(unsigned int dummy0, unsigned int dummy1, int entity)
{
    unsigned long long frame;
    int*   resultList;
    float* mtx;
    unsigned int ownHandle;
    int*   strip;
    bool   first;
    unsigned int listType;
    double dx, dy, dz;
    float  center[3];     // local_114/110/10c
    float  aabb[6];       // local_d8..local_c4 (min lo, max hi)
    int    hits[38];      // local_c0
    int    baseIdx, i, s, e;

    (void)dummy0; (void)dummy1;
    frame = FUN_803826bc();
    resultList = (int*)(unsigned int)(frame >> 0x20);
    mtx = (float*)(unsigned int)frame;
    ownHandle = *(unsigned int*)(entity + 0x80);
    strip = *(int**)(*(int*)(entity + 4) + 4);
    first = true;
    listType = 0x38;
    if (((unsigned int)mtx[0x70] & 0x20000) != 0) {
        listType = 0x3a;
    }
    center[0] = mtx[0x59]; center[1] = mtx[0x5a]; center[2] = mtx[0x5b];
    mtx4_transform_vector_alias(mtx, center);
    dx = (double)(mtx[0x50] - center[0]);
    dy = (double)(mtx[0x51] - center[1]);
    dz = (double)(mtx[0x52] - center[2]);

    // build the merged shape AABB (aabb[0..2] min, aabb[3..5] max)
    baseIdx = 0;
    for (i = 0; i < *strip; ++i) {
        int rec = baseIdx + *strip * -0xc0;
        unsigned int mask = *(unsigned int*)((int)strip + rec + 0x50);
        if ((mask == 0) || ((*(unsigned int*)(*(int*)(entity + 4) + 0x30) & mask) != 0)) {
            int off = 0;
            for (s = 0; s < *(short*)((int)strip + rec + 0x4c); ++s) {
                int* shape = (int*)(*(int*)((int)strip + rec + 0x80) + off);
                int type = *shape;
                float half[3];    // local_108/104/100
                float pos[3];     // local_fc/f8/f4
                float lo[3], hi[3];
                if (type == 1) {
                    half[0] = (float)shape[4]; half[1] = (float)shape[5]; half[2] = (float)shape[6];
                } else if (type < 1) {
                    if (-1 < type) { half[0] = (float)shape[4]; half[1] = half[0]; half[2] = half[0]; }
                    else { half[0] = half[1] = half[2] = 0.0f; }
                } else { // type < 3
                    half[0] = (float)shape[4]; half[1] = (float)shape[5]; half[2] = half[0];
                }
                pos[0] = (float)shape[1]; pos[1] = (float)shape[2]; pos[2] = (float)shape[3];
                entity_transform_bounds_by_matrix(mtx, half, pos);
                pos[0] = (float)((double)pos[0] + dx);
                pos[1] = (float)((double)pos[1] + dy);
                pos[2] = (float)((double)pos[2] + dz);
                lo[0] = pos[0] - half[0]; lo[1] = pos[1] - half[1]; lo[2] = pos[2] - half[2];
                hi[0] = pos[0] + half[0]; hi[1] = pos[1] + half[1]; hi[2] = pos[2] + half[2];
                if (first) {
                    first = false;
                    leaf_31e8((unsigned int)&aabb[0], (unsigned int)lo, 0x18);
                } else {
                    if (lo[0] < aabb[0]) aabb[0] = lo[0];
                    if (lo[1] < aabb[1]) aabb[1] = lo[1];
                    if (lo[2] < aabb[2]) aabb[2] = lo[2];
                    if (aabb[3] < hi[0]) aabb[3] = hi[0];
                    if (aabb[4] < hi[1]) aabb[4] = hi[1];
                    if (aabb[5] < hi[2]) aabb[5] = hi[2];
                }
                off += 0x1c;
            }
        }
        baseIdx += 0xc0;
    }

    // broad-phase query per room
    {
        int count = 0, k = 0, j;
        for (j = 0; j < *(char*)(entity + 0x30); ++j) {
            int room = *(int*)(entity + k + 0x38);
            if ((0 < room) && (room < 0xc9)) {
                count = QUERY_AABB(room, aabb, hits, count, 0x14, listType);
            }
            k += 4;
        }
        k = 0;
        for (j = 0; j < count; ++j) {
            int hitEnt = *(int*)((int)hits + k);
            unsigned int handle = *(unsigned int*)(hitEnt + 0x80);
            if (((((handle & 0xffff) <= (ownHandle & 0xffff)) ||
                  ((handle != 0) && (FUN_801494e0(handle) != 0))) &&
                 (subobj_collision_mask_test((unsigned int)*(unsigned char*)((int)mtx + 0x1ca),
                                             *(unsigned int*)(hitEnt + 0x80)) != 0)) &&
                ((*(int*)(hitEnt + 0x80) != 0) || (((unsigned int)mtx[0x70] & 0x40) == 0))) {

                int mode;
                if ((*(int*)(hitEnt + 0x80) == 0) ||
                    (((*(unsigned int*)(entity + 0x28) & 0x8000) == 0) &&
                     ((*(unsigned int*)(hitEnt + 0x28) & 0x8000) == 0))) {
                    if (((*(unsigned int*)(*(int*)(hitEnt + 0x24) + 0x28) & 0x1000000) == 0) &&
                        ((*(unsigned int*)(hitEnt + 0x28) & 0x4000) == 0)) {
                        mode = 2;
                    } else {
                        mode = 0;
                    }
                } else {
                    mode = 1;
                }

                // per-shape-type dispatch against this hit
                int sIdx = 0, si;
                for (si = 0; si < *strip; ++si) {
                    int rec = sIdx + *strip * -0xc0;
                    unsigned int mask = *(unsigned int*)((int)strip + rec + 0x50);
                    if ((mask == 0) || ((*(unsigned int*)(*(int*)(entity + 4) + 0x30) & mask) != 0)) {
                        int off = 0, sh;
                        for (sh = 0; sh < *(short*)((int)strip + rec + 0x4c); ++sh) {
                            int* shape = (int*)(*(int*)((int)strip + rec + 0x80) + off);
                            float p[3];   // local_120/11c/118
                            int type = *shape;
                            p[0] = (float)shape[1]; p[1] = (float)shape[2]; p[2] = (float)shape[3];
                            mtx4_transform_vector_alias(mtx, p);
                            p[0] = (float)((double)p[0] + dx);
                            p[1] = (float)((double)p[1] + dy);
                            p[2] = (float)((double)p[2] + dz);
                            if (type == 0) {
                                if (mode == 1)      FUN_80147c50();
                                else if (mode == 0) RAY_SUB(
                                                        *(int*)((int)hits + k), p, (unsigned short)sh, entity, resultList);
                                else                BVH_MULTI(
                                                        *(int*)(*(int*)((int)hits + k) + 4), p, sh, entity, resultList, 1, 0x24000);
                            } else if (type == 1) {
                                if (mode == 1)      subobj_collision_gen_constraints_obb(
                                                        *(unsigned int*)(*(int*)((int)hits + k) + 0x80), p, (float*)(shape + 4), mtx, sh, entity, resultList);
                                else if (mode == 0) RAY_SUB_MULTI(
                                                        *(int*)((int)hits + k), p, (float*)(shape + 4), mtx, (unsigned short)sh, entity, resultList);
                                else                CAPSULE_SWEEP(
                                                        *(int*)(*(int*)((int)hits + k) + 4), p, (float*)(shape + 4), mtx, sh, entity, resultList, 1, 0x24000);
                            } else if (type == 2) {
                                if (mode == 1)      subobj_collision_gen_constraints_capsule(
                                                        *(unsigned int*)(*(int*)((int)hits + k) + 0x80), p, (float*)(shape + 4), (int)mtx, sh, entity, resultList);
                                else if (mode == 0) SWEPT_SUB(
                                                        *(int*)((int)hits + k), p, (float*)(shape + 4), (int)mtx, (unsigned short)sh, entity, resultList);
                                else                SWEPT_DEFAULT(
                                                        *(int*)(*(int*)((int)hits + k) + 4), p, (float*)(shape + 4), mtx, sh, entity, resultList, 1, 0x24000);
                            }
                            off += 0x1c;
                        }
                    }
                    sIdx += 0xc0;
                }
            }
            k += 4;
        }
    }
    FUN_80382708();
}

// ============================================================================
// 0x80147c50  subobj_collision_gen_constraints_point
// Frame entry (FUN_803826c4): for each shape entry of the frame sub-object's
// mesh strips, run the appropriate point/sphere/segment contact test against
// the passed reference point (r6) and, on contact, allocate a constraint record
// and push it to the caller list (r7). Uses register-passed args (r6/r7).
// Modelled with explicit parameters for the passed point/list.
// ============================================================================
void subobj_collision_gen_constraints_point(int otherEntity, int* list)
{
    unsigned long long frame;
    float* pt;
    float* desc;
    int    entity, i, s, off, baseIdx;
    int*   strip;
    double dx, dy, dz;

    frame = FUN_803826c4();
    pt = (float*)(unsigned int)frame;
    entity = *(int*)(DAT_80612184 + ((unsigned int)(frame >> 0x20) & 0xffff) * 4);
    desc = (float*)(DAT_8061218c + ((unsigned int)(frame >> 0x20) & 0xffff) * 0x200);
    strip = *(int**)(*(int*)(entity + 4) + 4);

    float depth = FLOAT_80615ea8;
    float center[3];   // local_9c/98/94
    center[0] = desc[0x59]; center[1] = desc[0x5a]; center[2] = desc[0x5b];
    mtx4_transform_vector_alias(desc, center);
    dx = (double)(desc[0x50] - center[0]);
    dy = (double)(desc[0x51] - center[1]);
    dz = (double)(desc[0x52] - center[2]);

    baseIdx = 0;
    for (i = 0; i < *strip; ++i) {
        int rec = baseIdx + *strip * -0xc0;
        unsigned int mask = *(unsigned int*)((int)strip + rec + 0x50);
        if ((mask == 0) || ((*(unsigned int*)(*(int*)(entity + 4) + 0x30) & mask) != 0)) {
            off = 0;
            for (s = 0; s < *(short*)((int)strip + rec + 0x4c); ++s) {
                int* shape = (int*)(*(int*)((int)strip + rec + 0x80) + off);
                int hit = 0;
                float outN[3], outPt[3];  // afStack_b4, afStack_a8
                float p[3];               // local_8c/88/84
                int type = *shape;
                p[0] = (float)shape[1]; p[1] = (float)shape[2]; p[2] = (float)shape[3];
                mtx4_transform_vector_alias(desc, p);
                p[0] = (float)((double)p[0] + dx);
                p[1] = (float)((double)p[1] + dy);
                p[2] = (float)((double)p[2] + dz);
                if (type == 1) {
                    hit = CLOSEST_PT_ON_OBB(pt, p, (float*)(shape + 4), desc, outPt, outN, &depth);
                } else if (type < 1) {
                    if (-1 < type)
                        hit = SPHERE_SWEEP_VS_POINT(0.0, (double)(float)shape[4], pt, p, outPt, outN, &depth);
                } else if (type < 3) {
                    hit = SPHERE_VS_SEGMENT(pt, p, (float*)(shape + 4), (int)desc, outPt, outN, &depth);
                }
                if (hit != 0) {
                    int* record = (int*)subobj_constraint_pool_alloc();
                    if (record != 0) {
                        subobj_constraint_init((double)FLOAT_80615ea8, (double)depth, (float*)record,
                                               outPt, outN, (unsigned short)s, entity, otherEntity, -1);
                        subobj_constraint_list_push(list, (int)record);
                    }
                }
                off += 0x1c;
            }
        }
        baseIdx += 0xc0;
    }
    FUN_80382710();
}

// ============================================================================
// 0x80147eec  subobj_collision_gen_constraints_obb
// OBB-vs-OBB / sphere-vs-OBB contact generation: for each shape entry, run the
// SAT / edge test against the passed OBB (base/segT2) and push up to 8 contact
// constraints per pair.
// ============================================================================
void subobj_collision_gen_constraints_obb(unsigned int shapeA, float* base, float* segT2,
                                          float* mtx, unsigned int strip_unused, int entity, int* list)
{
    unsigned long long frame;
    float* pt;
    float* desc;
    int    self, i, s, off, baseIdx;
    int*   strip;
    double dx, dy, dz;
    float  normal[3];   // local_114/110/10c
    float  pts[8*3];    // afStack_dc (up to 8 * vec3)
    float  depths[8];   // local_fc

    (void)shapeA; (void)strip_unused;
    frame = FUN_803826b4();
    pt = (float*)(unsigned int)frame;
    self = *(int*)(DAT_80612184 + ((unsigned int)(frame >> 0x20) & 0xffff) * 4);
    desc = (float*)(DAT_8061218c + ((unsigned int)(frame >> 0x20) & 0xffff) * 0x200);
    strip = *(int**)(*(int*)(self + 4) + 4);

    float center[3];
    center[0] = desc[0x59]; center[1] = desc[0x5a]; center[2] = desc[0x5b];
    mtx4_transform_vector_alias(desc, center);
    dx = (double)(desc[0x50] - center[0]);
    dy = (double)(desc[0x51] - center[1]);
    dz = (double)(desc[0x52] - center[2]);

    baseIdx = 0;
    for (i = 0; i < *strip; ++i) {
        int rec = baseIdx + *strip * -0xc0;
        unsigned int mask = *(unsigned int*)((int)strip + rec + 0x50);
        if ((mask == 0) || ((*(unsigned int*)(*(int*)(self + 4) + 0x30) & mask) != 0)) {
            off = 0;
            for (s = 0; s < *(short*)((int)strip + rec + 0x4c); ++s) {
                int* shape = (int*)(*(int*)((int)strip + rec + 0x80) + off);
                int n = 0;
                float p[3];  // local_108/104/100
                int type = *shape;
                p[0] = (float)shape[1]; p[1] = (float)shape[2]; p[2] = (float)shape[3];
                mtx4_transform_vector_alias(desc, p);
                p[0] = (float)((double)p[0] + dx);
                p[1] = (float)((double)p[1] + dy);
                p[2] = (float)((double)p[2] + dz);
                if (type == 1) {
                    n = SAT_OBB_VS_OBB(p, shape + 4, desc, pt, base, segT2, pts, normal, depths, 8, 3);
                } else if (type < 1) {
                    if (-1 < type) {
                        n = CLOSEST_PT_ON_OBB(p, pt, base, segT2, pts, normal, depths);
                        normal[0] = -normal[0]; normal[1] = -normal[1]; normal[2] = -normal[2];
                    }
                } else if (type < 3) {
                    n = SPHERE_VS_OBB_EDGES(p, shape + 4, desc, pt, base, (int)segT2, pts, normal, depths, 8, 3);
                }
                int di = 0, pi = 0, c;
                for (c = 0; c < n; ++c) {
                    int* record = (int*)subobj_constraint_pool_alloc();
                    if (record != 0) {
                        subobj_constraint_init((double)FLOAT_80615ea8, (double)*(float*)((int)depths + di),
                                               (float*)record, (float*)((int)pts + pi), normal,
                                               (unsigned short)s, self, entity, -1);
                        subobj_constraint_list_push(list, (int)record);
                    }
                    di += 4; pi += 0xc;
                }
                off += 0x1c;
            }
        }
        baseIdx += 0xc0;
    }
    FUN_80382700();
    (void)mtx;
}

// ============================================================================
// 0x801481f0  subobj_collision_gen_constraints_capsule
// Capsule / sphere-edge / segment contact generation (structurally identical to
// the OBB variant but with the capsule contact routines and up to 4 contacts).
// ============================================================================
void subobj_collision_gen_constraints_capsule(unsigned int shapeA, float* base, float* segT2,
                                              int mtx, unsigned int strip_unused, int entity, int* list)
{
    unsigned long long frame;
    float* pt;
    float* desc;
    int    self, i, s, off, baseIdx;
    int*   strip;
    double dx, dy, dz;
    float  normal[3];   // local_c4/c0/bc
    float  pts[4*3];    // afStack_ac
    float  depths[4];   // local_d4

    (void)shapeA; (void)strip_unused;
    frame = FUN_803826b4();
    pt = (float*)(unsigned int)frame;
    self = *(int*)(DAT_80612184 + ((unsigned int)(frame >> 0x20) & 0xffff) * 4);
    desc = (float*)(DAT_8061218c + ((unsigned int)(frame >> 0x20) & 0xffff) * 0x200);
    strip = *(int**)(*(int*)(self + 4) + 4);

    float center[3];
    center[0] = desc[0x59]; center[1] = desc[0x5a]; center[2] = desc[0x5b];
    mtx4_transform_vector_alias(desc, center);
    dx = (double)(desc[0x50] - center[0]);
    dy = (double)(desc[0x51] - center[1]);
    dz = (double)(desc[0x52] - center[2]);

    baseIdx = 0;
    for (i = 0; i < *strip; ++i) {
        int rec = baseIdx + *strip * -0xc0;
        unsigned int mask = *(unsigned int*)((int)strip + rec + 0x50);
        if ((mask == 0) || ((*(unsigned int*)(*(int*)(self + 4) + 0x30) & mask) != 0)) {
            off = 0;
            for (s = 0; s < *(short*)((int)strip + rec + 0x4c); ++s) {
                int* shape = (int*)(*(int*)((int)strip + rec + 0x80) + off);
                int n = 0;
                float p[3];  // local_b8/b4/b0
                int type = *shape;
                p[0] = (float)shape[1]; p[1] = (float)shape[2]; p[2] = (float)shape[3];
                mtx4_transform_vector_alias(desc, p);
                p[0] = (float)((double)p[0] + dx);
                p[1] = (float)((double)p[1] + dy);
                p[2] = (float)((double)p[2] + dz);
                if (type == 1) {
                    n = SPHERE_VS_OBB_EDGES(p, shape + 4, desc, pt, base, mtx, pts, normal, depths, 4, 3);
                } else if (type < 1) {
                    if (-1 < type) {
                        n = SPHERE_VS_SEGMENT(p, pt, base, mtx, pts, normal, depths);
                        normal[0] = -normal[0]; normal[1] = -normal[1]; normal[2] = -normal[2];
                    }
                } else if (type < 3) {
                    n = CAPSULE_VS_CAPSULE(p, shape + 4, (int)desc, pt, base, mtx, pts, normal, depths);
                }
                int di = 0, pi = 0, c;
                for (c = 0; c < n; ++c) {
                    int* record = (int*)subobj_constraint_pool_alloc();
                    if (record != 0) {
                        subobj_constraint_init((double)FLOAT_80615ea8, (double)*(float*)((int)depths + di),
                                               (float*)record, (float*)((int)pts + pi), normal,
                                               (unsigned short)s, self, entity, -1);
                        subobj_constraint_list_push(list, (int)record);
                    }
                    di += 4; pi += 0xc;
                }
                off += 0x1c;
            }
        }
        baseIdx += 0xc0;
    }
    FUN_80382700();
    (void)segT2;
}

// ============================================================================
// 0x801484e4  subobj_resolve_contact_single
// Resolve one contact against a single sub-object: run the optional contact
// filter callback (+0x1e8), compute the constraint-space impulse (normalising
// the relative direction), accumulate the linear + angular impulse into the
// descriptor, refresh the contact normal (+0x134), and apply the spin-coupling
// term when the "spin" flag (+0x1c0 & 0x80) is set.
// ============================================================================
void subobj_resolve_contact_single(unsigned long long frameArg, double restitution, double relVel,
                                   double invDt, unsigned int a5, unsigned int a6, float* dir)
{
    unsigned long long h;
    unsigned int handle;
    float* pt;
    int    desc;
    int    filtered = 0;
    double dv;
    float  lever[3];   // local_7c/78/74
    float  n[3];       // local_88/84/80
    float  imp[3];     // local_70/6c/68

    (void)frameArg; (void)a5; (void)a6;
    h = __save_gpr_28();
    handle = (unsigned int)(h >> 0x20);
    pt = (float*)(unsigned int)h;
    desc = DAT_8061218c + (handle & 0xffff) * 0x200;
    dv = relVel;

    if (*(void**)(desc + 0x1e8) != 0) {
        filtered = ((int(*)(double,double,double))*(void**)(desc + 0x1e8))(relVel, relVel, invDt);
    }
    if ((((double)FLOAT_80615ea8 < dv) && ((double)FLOAT_80615ea8 < invDt)) &&
        ((*(unsigned int*)(desc + 0x1c0) & 1) == 0) && (filtered == 0)) {

        double negRest = -restitution;
        lever[0] = pt[0] - *(float*)(desc + 0x140);
        double target = (double)(float)(-dv * invDt);
        float nx = (float)((double)pt[3] * negRest + (double)dir[0]);
        float ny = (float)((double)pt[4] * negRest + (double)dir[1]);
        float nz = (float)((double)pt[5] * negRest + (double)dir[2]);
        lever[1] = pt[1] - *(float*)(desc + 0x144);
        float len2 = nz * nz + nx * nx + ny * ny;
        lever[2] = pt[2] - *(float*)(desc + 0x148);
        n[0] = nx; n[1] = ny; n[2] = nz;
        if (FLOAT_80615ec4 < len2) {
            float inv = 1.0f / SQRT(len2);
            inv = inv * -(FLOAT_80615ebc * len2 * inv * inv - FLOAT_80615ec8);
            inv = FLOAT_80615eb0 * inv * -(FLOAT_80615ebc * len2 * inv * inv - FLOAT_80615ec8);
            n[0] = nx * inv; n[1] = ny * inv; n[2] = nz * inv;
        }
        double along = (double)(nz * n[2] + nx * n[0] + ny * n[1]);
        double j = (double)APPLY_TORQUE_IMPULSE(handle, lever, n);
        if (target < j) target = j;
        if (ABS((float)along) < (double)FLOAT_80615ee8) {
            target = (double)(float)(target * (double)(float)(ABS((float)along) / (double)FLOAT_80615ee8));
        }
        float ix = (float)((double)n[0] * target);
        float iy = (float)((double)n[1] * target);
        float iz = (float)((double)n[2] * target);
        *(float*)(desc + 0x128) += (lever[1] * iz - lever[2] * iy);
        *(float*)(desc + 300)   += (lever[2] * ix - lever[0] * iz);
        *(float*)(desc + 0x130) += (lever[0] * iy - lever[1] * ix);
        float invM = *(float*)(desc + 0x1a8);
        *(float*)(desc + 0x14c) += ix * invM;
        *(float*)(desc + 0x150) += iy * invM;
        *(float*)(desc + 0x154) += iz * invM;
        imp[0] = *(float*)(desc + 0x128);
        imp[1] = *(float*)(desc + 300);
        imp[2] = *(float*)(desc + 0x130);
        mtx4_transform_vector_alias((float*)(desc + 0x40), imp);
        *(float*)(desc + 0x134) = imp[0];
        *(float*)(desc + 0x138) = imp[1];
        *(float*)(desc + 0x13c) = imp[2];

        if ((*(unsigned int*)(desc + 0x1c0) & 0x80) != 0) {
            double spin = FUN_80149540(handle);
            float sc = FLOAT_80615f04 * (float)(restitution * spin);
            float t0 = (*(float*)(desc + 0x138) * lever[2] - *(float*)(desc + 0x13c) * lever[1]) * sc;
            float t1 = (*(float*)(desc + 0x134) * lever[1] - *(float*)(desc + 0x138) * lever[0]) * sc;
            float t2 = (*(float*)(desc + 0x13c) * lever[0] - *(float*)(desc + 0x134) * lever[2]) * sc;
            *(float*)(desc + 0x11c) += (t2 * lever[1] - t0 * lever[2]);
            *(float*)(desc + 0x120) += (t0 * lever[0] - t1 * lever[2]);
            *(float*)(desc + 0x124) += (t1 * lever[2] - t2 * lever[0]);
            float d3 = pt[3], d4 = pt[4], d5 = pt[5];
            float proj = sc * -(d5 * *(float*)(desc + 0x13c) + d3 * *(float*)(desc + 0x134) + d4 * *(float*)(desc + 0x138));
            *(float*)(desc + 0x11c) += d3 * proj;
            *(float*)(desc + 0x120) += d4 * proj;
            *(float*)(desc + 0x124) += d5 * proj;
        }
    }
    __restore_gpr_28();
}

// ============================================================================
// 0x801488f4  subobj_resolve_contact_pair
// Two-body version of the contact resolver (frame FUN_803826dc packs two
// handles): runs each body's contact filter, computes a shared impulse, and
// applies it with opposite sign to descriptor A and descriptor B, each with its
// own spin-coupling term.
// ============================================================================
void subobj_resolve_contact_pair(unsigned long long frameArg, double restitution, double relVel,
                                 double invDt, unsigned int a5, unsigned int a6,
                                 float* leverSrc, float* dir)
{
    unsigned long long h;
    unsigned int handleA, handleB;
    int descA, descB;
    int filteredA = 0, filteredB = 0;
    double dv;

    (void)frameArg; (void)a5; (void)a6;
    h = FUN_803826dc();
    handleA = (unsigned int)(h >> 0x20);
    handleB = (unsigned int)h;
    descA = DAT_8061218c + (int)((h & 0xffff0000ffffULL) >> 0x20) * 0x200;
    descB = DAT_8061218c + (int)(h & 0xffff0000ffffULL) * 0x200;
    dv = relVel;

    if (*(void**)(descA + 0x1e8) != 0)
        filteredA = ((int(*)(double,double,double,unsigned int,float*,float*))*(void**)(descA + 0x1e8))(relVel, relVel, invDt, handleA, leverSrc, dir);
    if (*(void**)(descB + 0x1e8) != 0)
        filteredB = ((int(*)(double,double,double,unsigned int,float*,float*))*(void**)(descB + 0x1e8))(dv, relVel, invDt, handleB, leverSrc, dir);

    if (((double)FLOAT_80615ea8 < dv) && ((double)FLOAT_80615ea8 < invDt)) {
        double negRest = -restitution;
        float leverA[3];  // local_90/8c/88
        float leverB[3];  // local_9c/98/94
        float n[3];       // local_a8/a4/a0
        leverA[0] = leverSrc[0] - *(float*)(descA + 0x140);
        double target = (double)(float)(-dv * invDt);
        leverA[1] = leverSrc[1] - *(float*)(descA + 0x144);
        leverA[2] = leverSrc[2] - *(float*)(descA + 0x148);
        leverB[0] = leverSrc[0] - *(float*)(descB + 0x140);
        leverB[1] = leverSrc[1] - *(float*)(descB + 0x144);
        leverB[2] = leverSrc[2] - *(float*)(descB + 0x148);
        n[0] = (float)((double)leverSrc[3] * negRest + (double)dir[0]);
        n[1] = (float)((double)leverSrc[4] * negRest + (double)dir[1]);
        n[2] = (float)((double)leverSrc[5] * negRest + (double)dir[2]);
        float len2 = n[2] * n[2] + n[0] * n[0] + n[1] * n[1];
        if (FLOAT_80615ec4 < len2) {
            float inv = 1.0f / SQRT(len2);
            inv = inv * -(FLOAT_80615ebc * len2 * inv * inv - FLOAT_80615ec8);
            inv = FLOAT_80615eb0 * inv * -(FLOAT_80615ebc * len2 * inv * inv - FLOAT_80615ec8);
            n[0] *= inv; n[1] *= inv; n[2] *= inv;
        }
        double j = (double)APPLY_PAIR_IMPULSE(handleA, handleB, leverA, leverB, n);
        if (target < j) target = j;
        double ix = (double)(float)((double)n[0] * target);
        double iy = (double)(float)((double)n[1] * target);
        double iz = (double)(float)((double)n[2] * target);

        // body A (+impulse)
        if ((filteredA == 0) && ((*(unsigned int*)(descA + 0x1c0) & 2) == 0)) {
            *(float*)(descA + 0x128) += (float)((double)leverA[1] * iz - (double)(float)((double)leverA[2] * iy));
            *(float*)(descA + 300)   += (float)((double)leverA[2] * ix - (double)(float)((double)leverA[0] * iz));
            *(float*)(descA + 0x130) += (float)((double)leverA[0] * iy - (double)(float)((double)leverA[1] * ix));
            double invM = (double)*(float*)(descA + 0x1a8);
            *(float*)(descA + 0x14c) += (float)(ix * invM);
            *(float*)(descA + 0x150) += (float)(iy * invM);
            *(float*)(descA + 0x154) += (float)(iz * invM);
            float imp[3];
            imp[0] = *(float*)(descA + 0x128); imp[1] = *(float*)(descA + 300); imp[2] = *(float*)(descA + 0x130);
            mtx4_transform_vector_alias((float*)(descA + 0x40), imp);
            *(float*)(descA + 0x134) = imp[0]; *(float*)(descA + 0x138) = imp[1]; *(float*)(descA + 0x13c) = imp[2];
            if ((*(unsigned int*)(descA + 0x1c0) & 0x80) != 0) {
                double spin = FUN_80149540(handleA);
                float sc = FLOAT_80615f04 * (float)(restitution * spin);
                float t0 = (*(float*)(descA + 0x138) * leverA[2] - *(float*)(descA + 0x13c) * leverA[1]) * sc;
                float t1 = (*(float*)(descA + 0x134) * leverA[1] - *(float*)(descA + 0x138) * leverA[0]) * sc;
                float t2 = (*(float*)(descA + 0x13c) * leverA[0] - *(float*)(descA + 0x134) * leverA[2]) * sc;
                *(float*)(descA + 0x11c) += (t1 * leverA[0] - t0 * leverA[2]);
                *(float*)(descA + 0x120) += (t2 * leverA[0] - t1 * leverA[1]);
                *(float*)(descA + 0x124) += (t0 * leverA[1] - t2 * leverA[0]);
                float d3 = leverSrc[3], d4 = leverSrc[4], d5 = leverSrc[5];
                float proj = sc * -(d5 * *(float*)(descA + 0x13c) + d3 * *(float*)(descA + 0x134) + d4 * *(float*)(descA + 0x138));
                *(float*)(descA + 0x11c) += d3 * proj;
                *(float*)(descA + 0x120) += d4 * proj;
                *(float*)(descA + 0x124) += d5 * proj;
            }
        }
        // body B (-impulse)
        if ((filteredB == 0) && ((*(unsigned int*)(descB + 0x1c0) & 2) == 0)) {
            *(float*)(descB + 0x128) -= (float)((double)leverB[1] * iz - (double)(float)((double)leverB[2] * iy));
            *(float*)(descB + 300)   -= (float)((double)leverB[2] * ix - (double)(float)((double)leverB[0] * iz));
            *(float*)(descB + 0x130) -= (float)((double)leverB[0] * iy - (double)(float)((double)leverB[1] * ix));
            double invM = (double)*(float*)(descB + 0x1a8);
            *(float*)(descB + 0x14c) -= (float)(ix * invM);
            *(float*)(descB + 0x150) -= (float)(iy * invM);
            *(float*)(descB + 0x154) -= (float)(iz * invM);
            float imp[3];
            imp[0] = *(float*)(descB + 0x128); imp[1] = *(float*)(descB + 300); imp[2] = *(float*)(descB + 0x130);
            mtx4_transform_vector_alias((float*)(descB + 0x40), imp);
            *(float*)(descB + 0x134) = imp[0]; *(float*)(descB + 0x138) = imp[1]; *(float*)(descB + 0x13c) = imp[2];
            if ((*(unsigned int*)(descB + 0x1c0) & 0x80) != 0) {
                double spin = FUN_80149540(handleB);
                float sc = FLOAT_80615f04 * (float)(restitution * spin);
                float t0 = (*(float*)(descB + 0x138) * leverB[2] - *(float*)(descB + 0x13c) * leverB[1]) * sc;
                float t1 = (*(float*)(descB + 0x134) * leverB[1] - *(float*)(descB + 0x138) * leverB[0]) * sc;
                float t2 = (*(float*)(descB + 0x13c) * leverB[0] - *(float*)(descB + 0x134) * leverB[2]) * sc;
                *(float*)(descB + 0x11c) += (t1 * leverB[0] - t0 * leverB[1]);
                *(float*)(descB + 0x120) += (t2 * leverB[0] - t1 * leverB[1]);
                *(float*)(descB + 0x124) += (t0 * leverB[1] - t2 * leverB[0]);
                float d3 = leverSrc[3], d4 = leverSrc[4], d5 = leverSrc[5];
                float proj = sc * -(d5 * *(float*)(descB + 0x13c) + d3 * *(float*)(descB + 0x134) + d4 * *(float*)(descB + 0x138));
                *(float*)(descB + 0x11c) += d3 * proj;
                *(float*)(descB + 0x120) += d4 * proj;
                *(float*)(descB + 0x124) += d5 * proj;
            }
        }
    }
    FUN_80382728();
}
