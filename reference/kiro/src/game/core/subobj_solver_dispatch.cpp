// Address range: 0x80148f50 - 0x80149540 | batch_414 | 2026-09-05
//
// Entity render sub-object SOLVER DISPATCH + dirty-list management + descriptor
// accessors (companion to subobj_pool/build/dynamics/solver/collision from
// batches 409-413).
//
// This layer drives the per-constraint resolve step and manages the global
// "dirty" descriptor list that the per-frame integrate/rebuild pass walks:
//
//   * subobj_solver_resolve_constraint  - take one constraint record, look up
//        the involved descriptor handle(s) and hand off to the pair/single
//        contact resolver from batch_413 (subobj_resolve_contact_pair/_single).
//   * subobj_constraint_list_prune_merge - sweep the constraint list, dropping
//        stale/invalid records (separating type-3 penetration constraints and
//        mismatched wind/kinematic type-mask pairs) and merging near-coincident
//        contacts, renormalizing the merged contact normal (NR rsqrt).
//   * subobj_rebuild_matrix_palette      - copy the bone-matrix palette into the
//        descriptor and clear euler accumulators; set skinning-dirty bit.
//   * subobj_dirty_list_add / _remove    - intrusive doubly-linked dirty list
//        (head DAT_80612168, links at word +0x7e/+0x7f = byte +0x1f8/+0x1fc).
//        _add is the ~40-caller "mark this sub-object dirty this frame" helper.
//   * subobj_is_idle                     - predicate: descriptor not in the
//        dirty list (stamp byte +0x1c8 == -1).
//   * subobj_get_contact_origin/_linear_impulse/_contact_normal - float-row
//        pointer accessors into the descriptor (+0x140 / +0x14c / +0x134).
//   * subobj_get_inverse_mass            - 1/mass (0 when mass is 0 / infinite).
//
// Descriptor float-word offsets used here (word = byte/4):
//   [0x50..0x52] world center           [0x5f..0x61] local extent
//   [0x6b..0x6d] world dirty-origin     [0x6f] frame stamp   [0x72] dirty stamp
//   [0x7e]       dirty-list prev link   [0x7f] dirty-list next link
//   +0x28  flags word (skinning bit 0x200000)
//   +0x70/+0xc0/+0xc8 euler accumulators
//   +0x134 contact normal   +0x140 contact/world origin   +0x14c linear-impulse
//   +0x1a8 inverse mass     +0x1c0 flags   +0x1c8 dirty stamp byte
//   +0x1f8/+0x1fc dirty-list prev/next    +0x1fc secondary link
// Constraint record float-word offsets:
//   [0]..[2] contact point   [3..5] contact normal   [9] descriptor A ptr
//   [10] descriptor B ptr    [0xd] next link          +0x2e type halfword

#include "common.h"
#include "generated_decls.h"

extern "C" {
// spawn-frame push/pop scratch (pop matches push)
extern unsigned long long FUN_803826e8(void); extern void FUN_80382734(void);

// contact resolvers (batch_413, subobj_collision.cpp)
extern void subobj_resolve_contact_pair(double px, double py, double pz, double dt,
                                        unsigned int handleA, unsigned int handleB,
                                        float* recA, float* constraint);
extern void subobj_resolve_contact_single(double px, double py, double pz, double dt,
                                           unsigned int handle, float* recA, float* constraint);

// pool / entity helpers (batch_409, subobj_pool.cpp)
extern int   FUN_80252384(unsigned int soundGroup);   // sound-group -> params ptr
extern void  FUN_80253544(void);                       // entity sound teardown
extern void  mtx4_copy_array(int dst, int src, int count);
}

// subobj_get_entity + mtx4_transform_vector_alias resolve via generated_decls.h

// pool tables (shared)
extern int   DAT_8061218c;   // descriptor pool base (stride 0x200)
extern int   DAT_80612180;   // per-slot 0x20-stride auxiliary table
extern int   DAT_80612168;   // dirty-descriptor list head
extern int   DAT_806109c0;   // wind/time phase counter (stamp seed = /6)
extern float DAT_80611928;   // current frame time / counter

// shared float constants
extern float FLOAT_80615ea8;  // 0.0f
extern float FLOAT_80615eb0;  // 1.0f (NR / reciprocal numerator)
extern float FLOAT_80615ebc;  // 0.5f
extern float FLOAT_80615ec4;  // merge min-length^2 threshold
extern float FLOAT_80615ec8;  // 3.0f (NR constant)
extern float FLOAT_80615f08;  // merge distance^2 threshold
extern float FLOAT_80615f0c;  // merge normal-dot upper bound

// -----------------------------------------------------------------------------
// subobj_solver_resolve_constraint (0x80148f50)
// Resolve one constraint record: resolve the descriptor handle(s) referenced by
// the record, average the contact point between the two bodies for a pair, and
// dispatch to the two-body or single-body impulse resolver.
// -----------------------------------------------------------------------------
void subobj_solver_resolve_constraint(unsigned long long constraintArg, double dt)
{
    unsigned short type;
    float* paramsA;
    float* paramsB;
    unsigned int handleA;
    unsigned int handleB;
    float* rec;
    unsigned long long framed;

    framed = FUN_803826e8();
    rec = (float*)(unsigned int)(framed >> 0x20);
    handleA = *(unsigned int*)((int)rec[9] + 0x80);
    paramsA = (float*)FUN_80252384((unsigned int)*(unsigned char*)
                (DAT_8061218c + (handleA & 0xffff) * 0x200 + 0x1cc));
    type = *(unsigned short*)((int)rec + 0x2e);
    if (type == 1) {
        handleB = *(unsigned int*)((int)rec[10] + 0x80);
        paramsB = (float*)FUN_80252384((unsigned int)*(unsigned char*)
                    (DAT_8061218c + (handleB & 0xffff) * 0x200 + 0x1cc));
        subobj_resolve_contact_pair(
            (double)(FLOAT_80615ebc * (*paramsA + *paramsB)),
            (double)(FLOAT_80615ebc * (paramsA[1] + paramsB[1])),
            dt, dt, handleA, handleB, rec, (float*)(unsigned int)constraintArg);
    }
    else if ((type == 0) || (type < 5)) {
        subobj_resolve_contact_single(
            (double)*paramsA, (double)paramsA[1], dt, dt,
            handleA, rec, (float*)(unsigned int)constraintArg);
    }
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// subobj_constraint_list_prune_merge (0x80149074)
// Walk the singly-linked constraint list (next = word[0xd]) and:
//   * drop separating type-3 (penetration) constraints whose relative normal
//     velocity is already positive,
//   * drop mismatched kinematic/wind type-mask pairs (0x400000 vs 0x40/0x20),
//   * merge two same-body contacts that are near-coincident and nearly parallel,
//     summing and renormalizing their normals (Newton-Raphson rsqrt).
// Returns the (possibly updated) list head.
// -----------------------------------------------------------------------------
float* subobj_constraint_list_prune_merge(float* head)
{
    float nx;
    float mergeLenSq;
    bool drop;
    bool mergeable;
    float distThresh;
    float scale;
    int typeA;
    float* other;
    float* cur;
    float* prev;

    cur = head;
    prev = (float*)0x0;
    do {
        while (true) {
            if (cur == (float*)0x0) {
                return head;
            }
            distThresh = FLOAT_80615f08;
            drop = false;
            if (*(short*)((int)cur + 0x2e) == 3) {
                float fB = cur[10];
                if ((cur[2] - *(float*)((int)fB + 0x6c)) * cur[5] +
                    (*cur   - *(float*)((int)fB + 0x64)) * cur[3] +
                    (cur[1] - *(float*)((int)fB + 0x68)) * cur[4] < FLOAT_80615ea8) {
                    drop = true;
                }
            }
            else {
                typeA = *(int*)((int)cur[9] + 0x20);
                if (((typeA == 0x400000) &&
                     ((*(int*)((int)cur[10] + 0x20) == 0x40) ||
                      (*(int*)((int)cur[10] + 0x20) == 0x20))) ||
                    ((*(int*)((int)cur[10] + 0x20) == 0x400000) &&
                     ((typeA == 0x40) || (typeA == 0x20)))) {
                    drop = true;
                }
            }
            if (!drop) break;
            if (prev == (float*)0x0) {
                head = *(float**)&cur[0xd];
                cur = head;
            }
            else {
                cur = *(float**)&cur[0xd];
                *(float**)&prev[0xd] = cur;            /* unlink via prev */
                prev = cur;
            }
        }
        /* merge pass over the remainder of the list against 'cur' */
        other = *(float**)&cur[0xd];
        prev = cur;
        while (other != (float*)0x0) {
            mergeLenSq = FLOAT_80615ec4;
            drop = false;
            mergeable = false;
            if (other[9] != cur[9]) break;
            if ((cur[2] - other[2]) * (cur[2] - other[2]) +
                (*cur   - *other)   * (*cur   - *other) +
                (cur[1] - other[1]) * (cur[1] - other[1]) < distThresh) {
                drop = true;
                float dot = cur[5] * other[5] + cur[3] * other[3] + cur[4] * other[4];
                if ((FLOAT_80615ea8 < dot) && (dot < FLOAT_80615f0c)) {
                    mergeable = true;
                }
            }
            if (drop) {
                if (mergeable) {
                    cur[3] = cur[3] + other[3];
                    cur[4] = cur[4] + other[4];
                    cur[5] = cur[5] + other[5];
                    nx = cur[3];
                    float lenSq = cur[5] * cur[5] + nx * nx + cur[4] * cur[4];
                    if (mergeLenSq < lenSq) {
                        scale = 1.0f / SQRT(lenSq);
                        scale = scale * -(FLOAT_80615ebc * lenSq * scale * scale - FLOAT_80615ec8);
                        scale = FLOAT_80615eb0 * scale *
                                -(FLOAT_80615ebc * lenSq * scale * scale - FLOAT_80615ec8);
                        cur[3] = nx * scale;
                        cur[4] = cur[4] * scale;
                        cur[5] = cur[5] * scale;
                    }
                }
                *(float**)&prev[0xd] = *(float**)&other[0xd];
            }
            prev = other;
            other = *(float**)&other[0xd];
        }
        other = *(float**)&cur[0xd];
        prev = cur;
        cur = other;
    } while (true);
}

// -----------------------------------------------------------------------------
// subobj_rebuild_matrix_palette (0x801492f0)
// Copy the source bone-matrix palette into the descriptor's matrix palette
// (mtx4_copy_array), clear the euler accumulators, and set the skinning-dirty
// bit (0x200000). Called from subobj_build_descriptor.
// -----------------------------------------------------------------------------
void subobj_rebuild_matrix_palette(int desc)
{
    float zero;
    int palette;

    palette = *(int*)(*(int*)(desc + 4) + 0x10);
    if (palette != 0) {
        mtx4_copy_array(palette,
                        DAT_8061218c + (*(unsigned int*)(desc + 0x80) & 0xffff) * 0x200,
                        *(int*)(*(int*)(*(int*)(desc + 4) + 4) + 4));
        zero = FLOAT_80615ea8;
        *(float*)(desc + 0xc0) = FLOAT_80615ea8;
        *(float*)(desc + 0x70) = zero;
        *(float*)(desc + 0xc8) = zero;
    }
    *(unsigned int*)(desc + 0x28) = *(unsigned int*)(desc + 0x28) | 0x200000;
}

// -----------------------------------------------------------------------------
// subobj_dirty_list_remove (0x80149360)
// Unlink the descriptor from the global dirty list, clear the dirty flag bit
// (0x1000000), reset its dirty stamp byte to 0xff, then run entity sound
// teardown. Called from subobj_pool_free.
// -----------------------------------------------------------------------------
void subobj_dirty_list_remove(unsigned int handle)
{
    int desc;

    desc = DAT_8061218c + (handle & 0xffff) * 0x200;
    if (-1 < *(char*)(desc + 0x1c8)) {
        *(unsigned int*)(desc + 0x1c0) = *(unsigned int*)(desc + 0x1c0) & 0xfeffffff;
        *(unsigned char*)(desc + 0x1c8) = 0xff;
        if (*(int*)(desc + 0x1f8) == 0) {
            DAT_80612168 = *(int*)(desc + 0x1fc);
        }
        else {
            *(int*)(*(int*)(desc + 0x1f8) + 0x1fc) = *(int*)(desc + 0x1fc);
        }
        if (*(int*)(desc + 0x1fc) != 0) {
            *(int*)(*(int*)(desc + 0x1fc) + 0x1f8) = *(int*)(desc + 0x1f8);
        }
        *(int*)(desc + 0x1f8) = 0;
        *(int*)(desc + 0x1fc) = 0;
        subobj_get_entity(handle);
        FUN_80253544();
    }
}

// -----------------------------------------------------------------------------
// subobj_dirty_list_add (0x801493f4)
// Mark this sub-object descriptor dirty for the frame: link it at the head of
// the dirty list, seed its dirty stamp (frames = DAT_806109c0 / 6), reset the
// auxiliary accumulator, stamp the current frame, and cache the world-space
// dirty origin (local extent transformed to world + world center).
// This is the ~40-caller "mark dirty" helper referenced across the codebase.
// -----------------------------------------------------------------------------
void subobj_dirty_list_add(unsigned int handle)
{
    float frameTime;
    float* desc;
    float local[3];

    desc = (float*)(DAT_8061218c + (handle & 0xffff) * 0x200);
    if (*(char*)((int)desc + 0x1c8) == -1) {
        *(int*)&desc[0x7e] = 0;                      /* prev = 0 (new head) */
        *(int*)&desc[0x7f] = DAT_80612168;           /* next = old head */
        if (DAT_80612168 != 0) {
            *(int*)(DAT_80612168 + 0x1f8) = (int)desc;
        }
        frameTime = FLOAT_80615ea8;
        DAT_80612168 = (int)desc;
        *(char*)((int)desc + 0x1c8) = (char)(DAT_806109c0 / 6);
        *(float*)(DAT_80612180 + (handle & 0xffff) * 0x20) = frameTime;
        desc[0x74] = 0.0f;
        desc[0x6f] = DAT_80611928;
        local[0] = desc[0x5f];
        local[1] = desc[0x60];
        local[2] = desc[0x61];
        mtx4_transform_vector_alias(desc, local);
        desc[0x6b] = local[0] + desc[0x50];
        desc[0x6c] = local[1] + desc[0x51];
        desc[0x6d] = local[2] + desc[0x52];
    }
}

// -----------------------------------------------------------------------------
// subobj_is_idle (0x801494e0)
// Predicate: is the descriptor NOT in the dirty list (dirty stamp byte +0x1c8
// == -1). Uses the CLZ==0-on-negative idiom. Returns 1 when idle, 0 when dirty.
// -----------------------------------------------------------------------------
unsigned int subobj_is_idle(unsigned int handle)
{
    unsigned int lz;
    lz = countLeadingZeros((int)*(char*)(DAT_8061218c + (handle & 0xffff) * 0x200 + 0x1c8));
    return (1 << (lz & 0x1f) | 1U >> (0x20 - (lz & 0x1f))) & 1;
}

// -----------------------------------------------------------------------------
// subobj_get_contact_origin (0x80149504) - &descriptor[+0x140]
// -----------------------------------------------------------------------------
int subobj_get_contact_origin(unsigned int handle)
{
    return DAT_8061218c + (handle & 0xffff) * 0x200 + 0x140;
}

// -----------------------------------------------------------------------------
// subobj_get_linear_impulse (0x80149518) - &descriptor[+0x14c]
// -----------------------------------------------------------------------------
int subobj_get_linear_impulse(unsigned int handle)
{
    return DAT_8061218c + (handle & 0xffff) * 0x200 + 0x14c;
}

// -----------------------------------------------------------------------------
// subobj_get_contact_normal (0x8014952c) - &descriptor[+0x134]
// -----------------------------------------------------------------------------
int subobj_get_contact_normal(unsigned int handle)
{
    return DAT_8061218c + (handle & 0xffff) * 0x200 + 0x134;
}

// -----------------------------------------------------------------------------
// subobj_get_inverse_mass (0x80149540)
// Return the descriptor inverse mass at +0x1a8, or 0 when the stored mass is 0
// (returns 1/mass for a finite mass).
// -----------------------------------------------------------------------------
double subobj_get_inverse_mass(unsigned int handle)
{
    double mass;
    mass = (double)*(float*)(DAT_8061218c + (handle & 0xffff) * 0x200 + 0x1a8);
    if ((double)FLOAT_80615ea8 == mass) {
        return (double)FLOAT_80615ea8;
    }
    return (double)(float)((double)FLOAT_80615eb0 / mass);
}


// =============================================================================
// batch_415 | 2026-09-05 | 0x80149568 - 0x801497b0
// Sub-object rigid-body matrix helpers + torque/impulse/contact setters.
// Continuation of the subobj solver cluster (appended to subobj_solver_dispatch).
//
// Additional descriptor float-word / byte offsets used below:
//   +0x00  object (model->world) matrix        +0x40 (word 0x10) inverse-inertia basis
//   +0x80  bone-matrix palette base            +0x80 (word 0x20) inertia matrix
//   +0xc0  (word 0x30) world->local rotation   +0x128..+0x130 angular-impulse accum
//   +0x140..+0x148 contact/world origin        +0x14c..+0x154 linear-impulse accum
//   +0x17c world linear velocity               +0x188 world aim/contact point
// =============================================================================

extern "C" {
// spawn-frame push/pop scratch used by subobj_apply_angular_impulse_local
extern unsigned long long FUN_803826f0(void); extern void FUN_8038273c(void);
}

// -----------------------------------------------------------------------------
// subobj_get_world_velocity (0x80149568) - &descriptor[+0x17c]
// World-space linear velocity row (read by weapon smoke-trail / bone-follow fx).
// -----------------------------------------------------------------------------
int subobj_get_world_velocity(unsigned int handle)
{
    return DAT_8061218c + (handle & 0xffff) * 0x200 + 0x17c;
}

// -----------------------------------------------------------------------------
// subobj_get_world_aim_point (0x8014957c) - &descriptor[+0x188]
// World-space aim / reference point row (read by autoaim + LOS targeting).
// -----------------------------------------------------------------------------
int subobj_get_world_aim_point(unsigned int handle)
{
    return DAT_8061218c + (handle & 0xffff) * 0x200 + 0x188;
}

// -----------------------------------------------------------------------------
// subobj_get_object_matrix (0x80149590) - &descriptor[+0x00]
// Base of the descriptor's object (model->world) matrix.
// -----------------------------------------------------------------------------
int subobj_get_object_matrix(unsigned int handle)
{
    return DAT_8061218c + (handle & 0xffff) * 0x200;
}

// -----------------------------------------------------------------------------
// subobj_set_bone_palette_matrix (0x801495a0)
// Copy a 4x4 matrix into the descriptor bone-matrix palette base (+0x80).
// -----------------------------------------------------------------------------
void subobj_set_bone_palette_matrix(unsigned int handle, float* mtx)
{
    mtx4_copy((float*)(DAT_8061218c + (handle & 0xffff) * 0x200 + 0x80), mtx);
}

// -----------------------------------------------------------------------------
// subobj_compute_inertia_world_basis (0x801495d0)
// out = transpose(object) * (inverseInertiaBasis[+0xc0] * object).
// Rotates the local inverse-inertia basis into world space for impulse solving.
// -----------------------------------------------------------------------------
void subobj_compute_inertia_world_basis(unsigned int handle, float* out)
{
    float* desc;
    float objT[16];
    float tmp[16];

    desc = (float*)(DAT_8061218c + (handle & 0xffff) * 0x200);
    mtx4_transpose(desc, objT);
    mtx4_multiply(desc + 0x30, desc, tmp);   /* (+0xc0 basis) * object */
    mtx4_multiply(objT, tmp, out);
}

// -----------------------------------------------------------------------------
// subobj_update_inverse_inertia_tensor (0x80149638)
// Store world->local rotation (invert +0xc0 into out), then rebuild the world
// inverse-inertia tensor at +0x10 = transpose(object) * (inertia[+0x80] * object).
// -----------------------------------------------------------------------------
void subobj_update_inverse_inertia_tensor(unsigned int handle, float* out)
{
    float* desc;
    float objT[16];
    float tmp[16];

    desc = (float*)(DAT_8061218c + (handle & 0xffff) * 0x200);
    mtx4_copy(out, desc + 0x20);             /* seed from inertia matrix +0x80 */
    mtx4_invert(desc + 0x30, out);           /* world->local from +0xc0 */
    mtx4_transpose(desc, objT);
    mtx4_multiply(desc + 0x20, desc, tmp);   /* inertia * object */
    mtx4_multiply(objT, tmp, desc + 0x10);   /* -> world inverse-inertia +0x40 */
}

// -----------------------------------------------------------------------------
// subobj_set_angular_impulse (0x801496b8)
// Store an angular-impulse vector into the accumulator (+0x128..+0x130) and mark
// the descriptor dirty.
// -----------------------------------------------------------------------------
void subobj_set_angular_impulse(unsigned int handle, unsigned int* vec)
{
    int desc;
    desc = DAT_8061218c + (handle & 0xffff) * 0x200;
    *(unsigned int*)(desc + 0x128) = vec[0];
    *(unsigned int*)(desc + 0x12c) = vec[1];
    *(unsigned int*)(desc + 0x130) = vec[2];
    subobj_dirty_list_add(handle);
}

// -----------------------------------------------------------------------------
// subobj_apply_angular_impulse_local (0x801496fc)
// Take a local-space point (spawn-frame arg), rotate it through the world
// inertia basis (subobj_compute_inertia_world_basis) and store the result as the
// angular-impulse accumulator (+0x128..+0x130); mark the descriptor dirty.
// -----------------------------------------------------------------------------
void subobj_apply_angular_impulse_local(void)
{
    unsigned int handle;
    float* pt;
    int desc;
    unsigned long long framed;
    float local[3];
    float basis[23];

    framed = FUN_803826f0();
    handle = (unsigned int)(framed >> 0x20);
    pt = (float*)(unsigned int)framed;
    desc = DAT_8061218c + (handle & 0xffff) * 0x200;
    subobj_compute_inertia_world_basis(handle, basis);
    local[0] = pt[0];
    local[1] = pt[1];
    local[2] = pt[2];
    mtx4_transform_point_alias(basis, local);
    *(float*)(desc + 0x128) = local[0];
    *(float*)(desc + 0x12c) = local[1];
    *(float*)(desc + 0x130) = local[2];
    subobj_dirty_list_add(handle);
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// subobj_set_contact_origin (0x80149788)
// Store the contact/world origin (+0x140..+0x148). Does NOT mark dirty.
// -----------------------------------------------------------------------------
void subobj_set_contact_origin(unsigned int handle, unsigned int* vec)
{
    int desc;
    desc = DAT_8061218c + (handle & 0xffff) * 0x200;
    *(unsigned int*)(desc + 0x140) = vec[0];
    *(unsigned int*)(desc + 0x144) = vec[1];
    *(unsigned int*)(desc + 0x148) = vec[2];
}

// -----------------------------------------------------------------------------
// subobj_set_linear_impulse (0x801497b0)
// Store the linear-impulse accumulator (+0x14c..+0x154) and mark dirty.
// -----------------------------------------------------------------------------
void subobj_set_linear_impulse(unsigned int handle, unsigned int* vec)
{
    int desc;
    desc = DAT_8061218c + (handle & 0xffff) * 0x200;
    *(unsigned int*)(desc + 0x14c) = vec[0];
    *(unsigned int*)(desc + 0x150) = vec[1];
    *(unsigned int*)(desc + 0x154) = vec[2];
    subobj_dirty_list_add(handle);
}


// =============================================================================
// batch_416 | 2026-09-05 | 0x801497f4 - 0x80149d70
// Sub-object sound/collision MATERIAL assignment, MASS/inertia setup, AABB
// re-dirty broad-phase, and age/flag accessors. Continuation of the subobj
// solver cluster (appended to subobj_solver_dispatch).
//
// Material tables (shared): PTR_DAT_8046e0a8 (stride 2 words = material data ptr
// per index) and DAT_8046e0a4 (stride 8 bytes = material short id per index).
// A mesh strip record (0xc0 bytes; strip table at descriptor+4->+4, count word[0])
// stores its assigned material short at word offset 0x13 and material ptr at
// word offset 0x20 (relative to the reversed strip index iVar1*-0x30).
//
// Additional descriptor byte offsets used below:
//   +0x1c0 flags word   +0x1cc sound-material byte   +0x1d0 activation frame stamp
//   +0x1a8 inverse mass  +0x80 inertia matrix
// =============================================================================

extern "C" {
// inertia-matrix builder from a mass ratio: FUN_8014f6dc(ratio, mtx, &massInOut)
extern void FUN_8014f6dc(double ratio, int mtx, float* massInOut);
// compiler GPR save/restore intrinsics (spawn-frame prologue helpers)
extern unsigned int __save_gpr_28(void); extern void __restore_gpr_28(void);
}
// aligned block allocator: mem_alloc_aligned is declared void(align,size,type) in
// generated_decls, but returns a block ptr in EAX at the call site; call through
// a typed alias to recover the int return.
typedef int (*alloc_blk_fn)(int align, int size, int tag);
#define __ALLOC_BLK ((alloc_blk_fn)&mem_alloc_aligned)

// material tables
extern int   PTR_DAT_8046e0a8;   // material-data pointer table (stride 2 words)
extern int   DAT_8046e0a4;       // material short-id table (stride 8 bytes)
// broad-phase / timing globals
extern int   DAT_80612184;       // per-slot owner/descriptor pointer table
extern int   DAT_80612160;       // active descriptor high-water index
extern float FLOAT_80615eb4;     // default age fraction (1.0-ish)
extern float FLOAT_80615f10;     // AABB dirty-margin epsilon
extern double DOUBLE_80615ed8;   // 2^52 magic-double bias

// -----------------------------------------------------------------------------
// subobj_material_matches (0x801497f4)
// If matIndex != -1: return 1 when the strip's material ptr (word 0x20) and
// short id (word 0x13) both match table entry [matIndex]. If matIndex == -1:
// scan all strips and return 1 when any strip has a live shape (short 0x4c != 0
// and ptr 0x80 != 0). Used by damage_melee_area_scan and the material writer.
// -----------------------------------------------------------------------------
unsigned int subobj_material_matches(int* strip, int matIndex)
{
    int count, i, base, off;
    unsigned int match;

    if (matIndex != -1) {
        match = 0;
        if ((strip[*strip * -0x30 + 0x20] == (int)(&PTR_DAT_8046e0a8)[matIndex * 2]) &&
            ((int)*(short*)(strip + *strip * -0x30 + 0x13) == *(int*)(&DAT_8046e0a4 + matIndex * 8))) {
            match = 1;
        }
        return match;
    }
    count = *strip;
    off = 0;
    base = count * -0xc0;
    if (0 < count) {
        do {
            int rec = off + base;
            if ((*(short*)((int)strip + rec + 0x4c) != 0) &&
                (*(int*)((int)strip + rec + 0x80) != 0)) {
                return 1;
            }
            off += 0xc0;
            count += -1;
        } while (count != 0);
    }
    return 0;
}

// -----------------------------------------------------------------------------
// subobj_assign_sound_material (0x8014989c)
// Spawn-frame: assign a sound/collision material (index) to the descriptor's
// primary strip record. For a normal index (<0x1b), copy the table short id +
// material data ptr into the strip (words 0x13 / 0x20). For index 0x1a, allocate
// a fresh 0x20-byte material control block + 0x1c-byte payload, seed it from the
// descriptor's stored inverse-mass / inertia words (+0x1a8..+0x1c8), and link it.
// -----------------------------------------------------------------------------
void subobj_assign_sound_material(void)
{
    int stripRec;
    int* strip;
    int stripCount;
    int idx;
    int desc;
    int blk, payload;
    unsigned long long framed;

    framed = FUN_803826f0();
    idx = (int)framed;
    desc = *(int*)(DAT_80612184 + ((unsigned int)(framed >> 0x20) & 0xffff) * 4);
    if ((-1 < idx) && (idx < 0x1b)) {
        strip = *(int**)(*(int*)(desc + 4) + 4);
        stripCount = *strip;
        if (idx == 0x1a) {
            blk = __ALLOC_BLK(0x20, 4, 1);
            payload = __ALLOC_BLK(0x20, 0x1c, 1);
            *(int*)(blk + 4) = payload;
            *(int*)payload = 1;
            *(int*)(*(int*)(blk + 4) + 4)  = *(int*)(desc + 0x1a8);
            *(int*)(*(int*)(blk + 4) + 8)  = *(int*)(desc + 0x1ac);
            *(int*)(*(int*)(blk + 4) + 0xc) = *(int*)(desc + 0x1b0);
            *(int*)(*(int*)(blk + 4) + 0x10) = *(int*)(desc + 0x1c0);
            *(int*)(*(int*)(blk + 4) + 0x14) = *(int*)(desc + 0x1c4);
            *(int*)(*(int*)(blk + 4) + 0x18) = *(int*)(desc + 0x1c8);
            *(short*)(strip + stripCount * -0x30 + 0x13) = 1;
            strip[stripCount * -0x30 + 0x20] = *(int*)(blk + 4);
        }
        else {
            *(short*)(strip + stripCount * -0x30 + 0x13) = (short)*(int*)(&DAT_8046e0a4 + idx * 8);
            strip[stripCount * -0x30 + 0x20] = (int)(&(&PTR_DAT_8046e0a8)[idx * 2])[0];
        }
    }
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// subobj_set_material_and_mass (0x801499b0)
// Spawn-frame: assign the sound-material byte (+0x1cc) and recompute the body's
// inertia/mass from the ratio of the new material's density (+0x10) to the old
// material's density, scaling the stored inverse mass. Rebuilds the world
// inverse-inertia tensor and stores the new inverse mass at +0x1a8.
// -----------------------------------------------------------------------------
void subobj_set_material_and_mass(void)
{
    unsigned int handle;
    int desc;
    int oldMat, newMat;
    double ratio;
    double invMass;
    unsigned long long framed;
    float mass;
    float mtx[23];

    framed = __save_gpr_28();
    handle = (unsigned int)(framed >> 0x20);
    desc = DAT_8061218c + (handle & 0xffff) * 0x200;
    oldMat = FUN_80252384((unsigned int)*(unsigned char*)(desc + 0x1cc));
    newMat = FUN_80252384((unsigned int)framed);
    ratio = (double)(*(float*)(oldMat + 0x10) / *(float*)(newMat + 0x10));
    invMass = subobj_get_inverse_mass(handle);
    mass = (float)invMass;
    *(char*)(desc + 0x1cc) = (char)framed;
    mtx4_copy((float*)(desc + 0x80), mtx);
    FUN_8014f6dc(ratio, (int)mtx, &mass);
    subobj_update_inverse_inertia_tensor(handle, mtx);
    if (FLOAT_80615ea8 < mass) {
        *(float*)(desc + 0x1a8) = FLOAT_80615eb0 / mass;
    }
    __restore_gpr_28();
}

// -----------------------------------------------------------------------------
// subobj_set_mass (0x80149a74)
// Set the body mass/inertia directly from a mass ratio (param massRatio). Scales
// the current inverse mass by 1/ratio, rebuilds the inertia matrix + world
// inverse-inertia tensor, and stores the new inverse mass at +0x1a8.
// Called from subobj_build_descriptor / subobj_rebuild_after_lod_swap.
// -----------------------------------------------------------------------------
void subobj_set_mass(double massRatio, unsigned int handle)
{
    int desc;
    double invInertia;
    double invMass;
    float mass;
    float mtx[19];

    desc = DAT_8061218c + (handle & 0xffff) * 0x200;
    invInertia = subobj_get_inverse_mass(handle);
    mass = (float)invInertia;
    invInertia = (double)FLOAT_80615ea8;
    if ((double)FLOAT_80615ea8 != massRatio) {
        invInertia = (double)(float)((double)(float)invInertia / massRatio);
    }
    mtx4_copy((float*)(desc + 0x80), mtx);
    FUN_8014f6dc(invInertia, (int)mtx, &mass);
    subobj_update_inverse_inertia_tensor(handle, mtx);
    invMass = (double)FLOAT_80615ea8;
    if ((double)FLOAT_80615ea8 != massRatio) {
        invMass = (double)(float)((double)FLOAT_80615eb0 / massRatio);
    }
    *(float*)(desc + 0x1a8) = (float)invMass;
}

// -----------------------------------------------------------------------------
// subobj_redirty_in_aabb (0x80149b38)
// Spawn-frame: walk all active descriptors and mark dirty any whose padded world
// AABB (center +0x64/+0x68/+0x6c + object offset +0x1a8.. + half-extent +0x1c0..
// + margin FLOAT_80615f10) overlaps the query AABB (arg: min xyz at [0..2], max
// xyz at [3..5]). Used to wake neighbours after a moving body edit.
// -----------------------------------------------------------------------------
void subobj_redirty_in_aabb(void)
{
    float cx, cy, cz;
    float* query;
    int desc;
    int i, off;

    query = (float*)FUN_803826f0();
    off = 0;
    for (i = 0; i < (int)(DAT_80612160 + 0x60); i = i + 1) {
        desc = *(int*)(DAT_80612184 + off);
        if (desc != 0) {
            cx = *(float*)(desc + 0x64) + *(float*)(desc + 0x1a8);
            cy = *(float*)(desc + 0x68) + *(float*)(desc + 0x1ac);
            cz = *(float*)(desc + 0x6c) + *(float*)(desc + 0x1b0);
            if (((((cx - *(float*)(desc + 0x1c0)) - FLOAT_80615f10 <= query[3]) &&
                  (query[0] <= cx + *(float*)(desc + 0x1c0) + FLOAT_80615f10)) &&
                 ((cy - *(float*)(desc + 0x1c4)) - FLOAT_80615f10 <= query[4])) &&
                (((query[1] <= cy + *(float*)(desc + 0x1c4) + FLOAT_80615f10) &&
                  ((cz - *(float*)(desc + 0x1c8)) - FLOAT_80615f10 <= query[5])) &&
                 (query[2] <= cz + *(float*)(desc + 0x1c8) + FLOAT_80615f10))) {
                subobj_dirty_list_add(*(unsigned int*)(desc + 0x80));
            }
        }
        off = off + 4;
    }
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// subobj_redirty_in_aabb_wrapper (0x80149c50)
// -----------------------------------------------------------------------------
void subobj_redirty_in_aabb_wrapper(void)
{
    subobj_redirty_in_aabb();
}

// -----------------------------------------------------------------------------
// subobj_get_age_fraction (0x80149cd4)
// Normalized age of the descriptor: 0 fresh -> 1 fully aged, computed from the
// activation frame stamp (+0x1d0) against the global frame/phase counters. When
// the stamp is <= 0 (never activated) returns the default FLOAT_80615eb4.
// -----------------------------------------------------------------------------
double subobj_get_age_fraction(unsigned int handle)
{
    float frac;
    int stamp;

    stamp = *(int*)(DAT_8061218c + (handle & 0xffff) * 0x200 + 0x1d0);
    frac = FLOAT_80615eb4;
    if (0 < stamp) {
        int frameCounter = *(int*)&DAT_80611928;   /* +0x80611928 read as int frame counter here */
        frac = (FLOAT_80615eb0 /
                (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80615ed8)) *
               (float)((double)CONCAT44(0x43300000, (frameCounter - stamp) ^ 0x80000000) - DOUBLE_80615ed8);
    }
    return (double)frac;
}

// -----------------------------------------------------------------------------
// subobj_get_activation_frame (0x80149d48) - return activation frame stamp +0x1d0
// -----------------------------------------------------------------------------
int subobj_get_activation_frame(unsigned int handle)
{
    return *(int*)(DAT_8061218c + (handle & 0xffff) * 0x200 + 0x1d0);
}

// -----------------------------------------------------------------------------
// subobj_get_flags (0x80149d5c) - return descriptor flags word +0x1c0
// -----------------------------------------------------------------------------
unsigned int subobj_get_flags(unsigned int handle)
{
    return *(unsigned int*)(DAT_8061218c + (handle & 0xffff) * 0x200 + 0x1c0);
}

// -----------------------------------------------------------------------------
// subobj_set_flags (0x80149d70) - set descriptor flags word +0x1c0, return old
// -----------------------------------------------------------------------------
unsigned int subobj_set_flags(unsigned int handle, unsigned int flags)
{
    int desc;
    unsigned int old;
    desc = DAT_8061218c + (handle & 0xffff) * 0x200;
    old = *(unsigned int*)(desc + 0x1c0);
    *(unsigned int*)(desc + 0x1c0) = flags;
    return old;
}
