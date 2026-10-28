// Address range: 0x8014e874 - 0x8014ed40 | batch_420 | 2026-09-06
//
// Entity render sub-object CONSTRAINT-ISLAND POOL + builder API (companion to
// subobj_sim.cpp / subobj_solver_iterate.cpp). This is the public interface
// game code uses to create physics constraints ("islands") between two
// sub-object rigid bodies (or one body and the world).
//
// Island pool: DAT_806121b4 is the base of 93 (0x5d) island records of 0x80
// bytes each; DAT_806121b0 is the live count. Record layout (byte offsets;
// word index = byte/4):
//   +0x00..+0x08   anchor A in body-A local space          (words 0..2)
//   +0x10..+0x18   anchor A / axis 0 in body-B local space (words 4..6)  [param idx 0]
//   +0x20..+0x28   anchor B / axis world                   (words 8..10) [param idx 2]
//   +0x40          limit low                                (word 0x10)
//   +0x44          limit high                               (word 0x11)
//   +0x48..+0x54   relative/reference quaternion            (words 0x12..0x15)
//   +0x58          type tag (as int: 1 point, 2 axis, ...)  (word 0x16) - also "in use"
//   +0x5c          drift/baumgarte gain                     (word 0x17)
//   +0x60          restitution                              (word 0x18)
//   +0x64..+0x6c   spring / (word 0x19..0x1b)
//   +0x70          body-A descriptor handle                 (word 0x1c)
//   +0x74          body-B descriptor handle (0 = world)     (word 0x1d)
//   +0x78          joint callback pointer                   (word 0x1e)
//   +0x7c          user pointer / secondary callback        (word 0x1f)
//
// The +0x58 word doubles as the "slot in use" flag AND the constraint type tag:
// pool_alloc sets it to 1 (point), the axis builder overwrites it to 2, etc.
// A slot is free when +0x58 == 0.

#include "common.h"
#include "generated_decls.h"

extern "C" {
// frame push helpers - return packed {bodyA handle, bodyB handle<<32}
extern unsigned long long FUN_803826e4(void);   extern void FUN_80382730(void);
extern unsigned long long FUN_803826dc(void);    extern void FUN_80382728(void);
}

extern int DAT_8061218c;   // descriptor pool base (stride 0x200)
extern int DAT_806121b4;   // island pool base (stride 0x80, 0x5d records)
extern int DAT_806121b0;   // island live count

extern float FLOAT_80615ea8;   // 0.0f
extern float FLOAT_80615eb4;   // limit-high default
extern float FLOAT_80615eb8;   // limit-low default
extern float FLOAT_80615f10;   // drift/baumgarte gain
extern float FLOAT_80615f80;   // default restitution

// ===========================================================================
// 0x8014e874  subobj_island_pool_alloc
// Return the index of the first free island slot (+0x58 == 0), marking it used
// (type 1 = point) with a cleared user pointer, and bump the live count. Scans
// all 0x5d slots; returns -1 when the pool is full.
// ===========================================================================
int subobj_island_pool_alloc(void)
{
    int index = 0;
    int byteOff = 0;
    int remaining = 0x5d;
    do {
        int rec = DAT_806121b4 + byteOff;
        if (*(int*)(rec + 0x58) == 0) {
            *(int*)(rec + 0x58) = 1;
            *(int*)(rec + 0x78) = 0;
            DAT_806121b0 = DAT_806121b0 + 1;
            return index;
        }
        index = index + 1;
        byteOff = byteOff + 0x80;
        remaining = remaining - 1;
    } while (remaining != 0);
    return -1;
}

// ===========================================================================
// 0x8014e8d0  subobj_island_is_active
// True when the given island slot's type/in-use word (+0x58) is non-zero.
// ===========================================================================
bool subobj_island_is_active(int index)
{
    return (bool)('\x01' - (*(int*)(DAT_806121b4 + index * 0x80 + 0x58) == 0));
}

// ===========================================================================
// 0x8014e8ec  subobj_island_pool_free
// Release an island slot: clear the type/in-use word and both callback pointers
// and decrement the live count.
// ===========================================================================
void subobj_island_pool_free(int index)
{
    DAT_806121b0 = DAT_806121b0 - 1;
    int rec = DAT_806121b4 + index * 0x80;
    *(int*)(rec + 0x58) = 0;
    *(int*)(rec + 0x78) = 0;
    *(int*)(rec + 0x7c) = 0;
}

// ===========================================================================
// 0x8014ea64  subobj_island_set_anchor
// Store a 3-float vector into island[index] at sub-slot (paramIdx*0x10): used
// for the raw anchor/axis columns at +0x00 / +0x10 / +0x20.
// ===========================================================================
void subobj_island_set_anchor(int index, int paramIdx, float* vec)
{
    int rec = DAT_806121b4 + index * 0x80;
    *(float*)(rec + paramIdx * 0x10)     = vec[0];
    *(float*)(rec + paramIdx * 0x10 + 4) = vec[1];
    *(float*)(rec + paramIdx * 0x10 + 8) = vec[2];
}

// ===========================================================================
// 0x8014ed10  subobj_island_set_axis_world
// Store a 3-float axis into island[index] sub-slot (paramIdx*0x10) at the +0x20
// world-axis field (offset +0x20 relative to the sub-slot base).
// ===========================================================================
void subobj_island_set_axis_world(int index, int paramIdx, float* axis)
{
    int rec = DAT_806121b4 + index * 0x80 + paramIdx * 0x10;
    *(float*)(rec + 0x20) = axis[0];
    *(float*)(rec + 0x24) = axis[1];
    *(float*)(rec + 0x28) = axis[2];
}

// ===========================================================================
// 0x8014ece0  subobj_island_set_rel_quat
// Copy a 4-float quaternion into the island's reference-quat field (+0x48).
// ===========================================================================
void subobj_island_set_rel_quat(int index, float* quat)
{
    int rec = DAT_806121b4 + index * 0x80;
    *(float*)(rec + 0x48) = quat[0];
    *(float*)(rec + 0x4c) = quat[1];
    *(float*)(rec + 0x50) = quat[2];
    *(float*)(rec + 0x54) = quat[3];
}

// ===========================================================================
// 0x8014ea94  subobj_island_store_rel_quat
// Compute and store the reference quaternion (+0x48) for an island: conj(qA)
// when single-body, else conj(qA)*qB. qA/qB are each at descriptor +0x100.
// ===========================================================================
void subobj_island_store_rel_quat(int islandRec, int bodyA, int bodyB)
{
    float conjA[4];
    if (bodyB == 0) {
        quat_conjugate((float*)(islandRec + 0x48), (float*)(bodyA + 0x100));
    } else {
        quat_conjugate(conjA, (float*)(bodyA + 0x100));
        quat_multiply((float*)(islandRec + 0x48), conjA, (float*)(bodyB + 0x100));
    }
}

// forward decl (batch_419)
double subobj_solver_relative_twist(float* bodyA, float* bodyB, float* refDir, float* refQuat);

// ===========================================================================
// 0x8014e918  subobj_island_create_point
// Allocate a point (type 1) constraint island anchoring worldPoint to two
// bodies. Stores the anchor in each body's local frame (world offset then
// transpose-transform by the body basis) at +0x00 (A) and +0x10 (B). Records
// the body handles at +0x1c/+0x1d and seeds gain/restitution. Frame-scoped
// (FUN_803826e4 supplies the {bodyA, bodyB} handle pair).
// ===========================================================================
void subobj_island_create_point(unsigned int arg0, unsigned int arg1, float* worldPoint)
{
    (void)arg0; (void)arg1;
    unsigned long long framed = FUN_803826e4();
    unsigned int handleA = (unsigned int)(framed >> 0x20);
    unsigned int handleB = (unsigned int)framed;
    int index = subobj_island_pool_alloc();
    float gain = FLOAT_80615f10;
    if (index != -1) {
        float* rec = (float*)(DAT_806121b4 + index * 0x80);
        float* bodyA = (float*)(DAT_8061218c + (handleA & 0xffff) * 0x200);
        float* bodyB;
        if (handleB == 0) {
            bodyB = (float*)0;
        } else {
            bodyB = (float*)(DAT_8061218c + (handleB & 0xffff) * 0x200);
        }
        *(int*)&rec[0x16] = 2;              // type tag (bit-pattern 0x2 stored as float word)
        float restit = FLOAT_80615f80;
        *(unsigned int*)&rec[0x1c] = handleA;
        float zero = FLOAT_80615ea8;
        *(unsigned int*)&rec[0x1d] = handleB;
        rec[0x17] = gain;
        rec[0x18] = restit;
        rec[0x19] = zero;
        rec[0x1b] = zero;
        rec[0x1a] = zero;
        rec[0x1f] = 0.0f;
        rec[0] = worldPoint[0] - bodyA[0x50];
        rec[1] = worldPoint[1] - bodyA[0x51];
        rec[2] = worldPoint[2] - bodyA[0x52];
        mtx4_transform_vector_transpose(bodyA, rec);
        if (bodyB == (float*)0) {
            rec[4] = worldPoint[0];
            rec[5] = worldPoint[1];
            rec[6] = worldPoint[2];
        } else {
            rec[4] = worldPoint[0] - bodyB[0x50];
            rec[5] = worldPoint[1] - bodyB[0x51];
            rec[6] = worldPoint[2] - bodyB[0x52];
            mtx4_transform_vector_transpose(bodyB, rec + 4);
        }
    }
    FUN_80382730();
}

// ===========================================================================
// 0x8014eaf8  subobj_island_create_axis
// Allocate an axis/hinge (type 3) constraint island: like create_point but also
// stores a second anchor/axis (worldAxis) into each body's local frame at +0x08
// (word 8) / +0x0c (word 0xc), sets limit low/high defaults (+0x40/+0x44), then
// computes the reference quaternion (subobj_island_store_rel_quat) and seeds the
// initial twist (subobj_solver_relative_twist). Frame-scoped (FUN_803826dc).
// ===========================================================================
void subobj_island_create_axis(unsigned int arg0, unsigned int arg1, float* worldPoint,
                               float* worldAxis)
{
    (void)arg0; (void)arg1;
    unsigned long long framed = FUN_803826dc();
    unsigned int handleA = (unsigned int)(framed >> 0x20);
    unsigned int handleB = (unsigned int)framed;
    int index = subobj_island_pool_alloc();
    float limitLo = FLOAT_80615eb8;
    if (index != -1) {
        float* rec = (float*)(DAT_806121b4 + index * 0x80);
        float* bodyA = (float*)(DAT_8061218c + (handleA & 0xffff) * 0x200);
        float* bodyB;
        if (handleB == 0) {
            bodyB = (float*)0;
        } else {
            bodyB = (float*)(DAT_8061218c + (handleB & 0xffff) * 0x200);
        }
        *(int*)&rec[0x16] = 3;              // type tag (bit-pattern 0x3)
        float limitHi = FLOAT_80615eb4;
        *(unsigned int*)&rec[0x1c] = handleA;
        float gain = FLOAT_80615f10;
        *(unsigned int*)&rec[0x1d] = handleB;
        float restit = FLOAT_80615f80;
        rec[0x10] = limitLo;
        float zero = FLOAT_80615ea8;
        rec[0x11] = limitHi;
        rec[0x17] = gain;
        rec[0x18] = restit;
        rec[0x19] = zero;
        rec[0x1b] = zero;
        rec[0x1a] = zero;
        rec[0x1f] = 0.0f;
        rec[0] = worldPoint[0] - bodyA[0x50];
        rec[1] = worldPoint[1] - bodyA[0x51];
        rec[2] = worldPoint[2] - bodyA[0x52];
        mtx4_transform_vector_transpose(bodyA, rec);
        rec[8]  = worldAxis[0];
        rec[9]  = worldAxis[1];
        rec[10] = worldAxis[2];
        mtx4_transform_vector_transpose(bodyA, rec + 8);
        if (bodyB == (float*)0) {
            rec[4] = worldPoint[0];
            rec[5] = worldPoint[1];
            rec[6] = worldPoint[2];
            rec[0xc] = worldAxis[0];
            rec[0xd] = worldAxis[1];
            rec[0xe] = worldAxis[2];
        } else {
            rec[4] = worldPoint[0] - bodyB[0x50];
            rec[5] = worldPoint[1] - bodyB[0x51];
            rec[6] = worldPoint[2] - bodyB[0x52];
            mtx4_transform_vector_transpose(bodyB, rec + 4);
            rec[0xc] = worldAxis[0];
            rec[0xd] = worldAxis[1];
            rec[0xe] = worldAxis[2];
            mtx4_transform_vector_transpose(bodyB, rec + 0xc);
        }
        subobj_island_store_rel_quat((int)rec, (int)bodyA, (int)bodyB);
        subobj_solver_relative_twist(bodyA, bodyB, rec + 8, rec + 0x12);
    }
    FUN_80382728();
}

// ===========================================================================
// 0x8014ed40  subobj_island_get_anchor_world
// Read back the world-space position of an island anchor for body paramIdx
// (0 = body A at +0x00, 1 = body B at +0x10). When that body exists, transform
// the stored local anchor by its basis and add its world center; otherwise
// return the island's fallback point (+0x10).
// ===========================================================================
void subobj_island_get_anchor_world(int index, int paramIdx, float* out)
{
    int rec = DAT_806121b4 + index * 0x80;
    int bodies[4];
    bodies[0] = DAT_8061218c + (*(unsigned int*)(rec + 0x70) & 0xffff) * 0x200;
    if (*(unsigned int*)(rec + 0x74) == 0) {
        bodies[1] = 0;
    } else {
        bodies[1] = DAT_8061218c + (*(unsigned int*)(rec + 0x74) & 0xffff) * 0x200;
    }
    float* body = (float*)bodies[paramIdx];
    if (body == (float*)0) {
        out[0] = *(float*)(rec + 0x10);
        out[1] = *(float*)(rec + 0x14);
        out[2] = *(float*)(rec + 0x18);
    } else {
        int sub = rec + paramIdx * 0x10;
        out[0] = *(float*)(rec + paramIdx * 0x10);
        out[1] = *(float*)(sub + 4);
        out[2] = *(float*)(sub + 8);
        mtx4_transform_vector(body, out);
        out[0] = out[0] + body[0x50];
        out[1] = out[1] + body[0x51];
        out[2] = out[2] + body[0x52];
    }
}

// ===========================================================================
// batch_421 additions (0x8014ee3c - 0x8014f0c0): island dispatch/collect +
// island/descriptor property setters.
// ===========================================================================

extern "C" {
extern void FUN_803826f0(void);   // push island-iteration frame
extern void FUN_8038273c(void);   // pop  (matches 803826f0)
}
// mem_page_alloc / mem_page_free resolve through generated_decls (common.h).

extern short DAT_806121a8;   // active-island count (halfword)
extern int   DAT_806121ac;   // active-island index list base (halfword entries)

// rng_counter_tick is declared void() in generated_decls but returns a 32-bit
// value; recover it through a typed function-pointer alias (rule 6 pattern).
typedef unsigned int (*isl_rng_fn)(void);
#define ISL_RNG_TICK ((isl_rng_fn)&rng_counter_tick)

// ===========================================================================
// 0x8014ef54  subobj_island_collect_active
// Build the per-frame active-island index list (DAT_806121ac, halfword entries)
// by scanning all live islands: an island is active this frame when at least
// one of its bodies is non-idle. Each collected island's bodies are re-added to
// the dirty list. Sets DAT_806121a8 to the collected count. Frame-scoped.
// ===========================================================================
void subobj_island_collect_active(void)
{
    FUN_803826f0();
    int byteOff = 0;
    DAT_806121a8 = 0;
    for (int i = 0; (DAT_806121a8 < DAT_806121b0) && (i < 0x5d); i = i + 1) {
        int rec = DAT_806121b4 + byteOff;
        if ((*(int*)(rec + 0x58) != 0) &&
            (((*(unsigned int*)(rec + 0x70) != 0) &&
              (subobj_is_idle(*(unsigned int*)(rec + 0x70)) == 0)) ||
             ((*(unsigned int*)(rec + 0x74) != 0) &&
              (subobj_is_idle(*(unsigned int*)(rec + 0x74)) == 0)))) {
            subobj_dirty_list_add(*(unsigned int*)(rec + 0x70));
            if (*(unsigned int*)(rec + 0x74) != 0) {
                subobj_dirty_list_add(*(unsigned int*)(rec + 0x74));
            }
            unsigned int slot = (unsigned int)DAT_806121a8;
            DAT_806121a8 = DAT_806121a8 + 1;
            *(short*)(DAT_806121ac + slot * 2) = (short)i;
        }
        byteOff = byteOff + 0x80;
    }
    FUN_8038273c();
}

// ===========================================================================
// 0x8014ee3c  subobj_island_dispatch_all
// Randomly permute the active-island list (Fisher-Yates over a scratch page)
// then dispatch each island by its type tag (+0x58): type 2 -> the linear
// frame solver (subobj_solver_frame_dispatch), type 3 -> the angular/twist
// solver (subobj_solver_angular_frame_dispatch). Frame-scoped.
// ===========================================================================
void subobj_island_dispatch_all(void)
{
    FUN_803826f0();
    if (DAT_806121a8 != 0) {
        short* order = (short*)mem_page_alloc();
        order[0] = 0;
        int outByte = 2;
        for (unsigned int i = 1; (int)i < (int)(unsigned int)(unsigned short)DAT_806121a8; i = i + 1) {
            unsigned int r = ISL_RNG_TICK();
            int j = (int)(r - (r / i) * i);
            *(short*)((int)order + outByte) = order[j];
            outByte = outByte + 2;
            order[j] = (short)i;
        }
        int itByte = 0;
        for (int k = 0; k < (int)(unsigned int)(unsigned short)DAT_806121a8; k = k + 1) {
            unsigned short listIdx = *(unsigned short*)((int)order + itByte);
            unsigned short islandIdx = *(unsigned short*)(DAT_806121ac + (unsigned int)listIdx * 2);
            int type = *(int*)(DAT_806121b4 + (unsigned int)islandIdx * 0x80 + 0x58);
            if (type == 2) {
                subobj_solver_frame_dispatch();
            } else if ((1 < type) && (type < 4)) {
                subobj_solver_angular_frame_dispatch();
            }
            itByte = itByte + 2;
        }
        mem_page_free((int)order);
    }
    FUN_8038273c();
}

// ===========================================================================
// 0x8014f024  subobj_set_collision_groups
// Set a descriptor's collision group bytes (+0x1c9 group, +0x1ca mask).
// ===========================================================================
void subobj_set_collision_groups(unsigned int handle, unsigned char group, unsigned char mask)
{
    int desc = DAT_8061218c + (handle & 0xffff) * 0x200;
    *(unsigned char*)(desc + 0x1c9) = group;
    *(unsigned char*)(desc + 0x1ca) = mask;
}

// ===========================================================================
// 0x8014f03c  subobj_island_set_user_ptr  (island +0x7c)
// ===========================================================================
void subobj_island_set_user_ptr(int index, void* userPtr)
{
    *(void**)(DAT_806121b4 + index * 0x80 + 0x7c) = userPtr;
}

// ===========================================================================
// 0x8014f094  subobj_island_set_joint_callback  (island +0x78)
// ===========================================================================
void subobj_island_set_joint_callback(int index, void* callback)
{
    *(void**)(DAT_806121b4 + index * 0x80 + 0x78) = callback;
}

// ===========================================================================
// 0x8014f050  subobj_island_set_restitution
// Set both restitution words (+0x68 and +0x6c) to the same value.
// ===========================================================================
void subobj_island_set_restitution(double restitution, int index)
{
    int rec = DAT_806121b4 + index * 0x80;
    *(float*)(rec + 0x6c) = (float)restitution;
    *(float*)(rec + 0x68) = (float)restitution;
}

// ===========================================================================
// 0x8014f068  subobj_island_set_restitution_range
// Set the min/max restitution words (+0x68 min, +0x6c max) independently.
// ===========================================================================
void subobj_island_set_restitution_range(double minRest, double maxRest, int index)
{
    int rec = DAT_806121b4 + index * 0x80;
    *(float*)(rec + 0x68) = (float)minRest;
    *(float*)(rec + 0x6c) = (float)maxRest;
}

// ===========================================================================
// 0x8014f080  subobj_island_set_drift_gain  (island +0x5c)
// ===========================================================================
void subobj_island_set_drift_gain(double gain, int index)
{
    *(float*)(DAT_806121b4 + index * 0x80 + 0x5c) = (float)gain;
}

// ===========================================================================
// 0x8014f0a8  subobj_island_set_twist_limits  (island +0x40 low, +0x44 high)
// ===========================================================================
void subobj_island_set_twist_limits(double low, double high, int index)
{
    int rec = DAT_806121b4 + index * 0x80;
    *(float*)(rec + 0x40) = (float)low;
    *(float*)(rec + 0x44) = (float)high;
}

// ===========================================================================
// 0x8014f0c0  subobj_island_set_spring  (island +0x64)
// ===========================================================================
void subobj_island_set_spring(double spring, int index)
{
    *(float*)(DAT_806121b4 + index * 0x80 + 100) = (float)spring;
}

// ===========================================================================
// batch_422 additions (0x8014f0d4 - 0x8014f15c): island lookup + accessors
// ===========================================================================

// ===========================================================================
// 0x8014f0d4  subobj_island_find_by_body_b
// Scan the live island pool for the first in-use slot (+0x58 != 0) whose
// body-B descriptor handle (+0x78) equals the query. Returns the slot index,
// or -1 if none / pool empty. NOTE +0x78 here is the joint-callback word in
// the batch_420 layout comment, but this lookup treats it as the secondary
// body/owner handle it is keyed on.
// ===========================================================================
int subobj_island_find_by_body_b(int bodyHandle)
{
    int index = 0;
    int byteOff = 0;
    while (true) {
        if ((0x5c < index) || (DAT_806121b0 == 0)) {
            return -1;
        }
        if ((*(int*)(DAT_806121b4 + byteOff + 0x58) != 0) &&
            (*(int*)(DAT_806121b4 + byteOff + 0x78) == bodyHandle)) {
            break;
        }
        index = index + 1;
        byteOff = byteOff + 0x80;
    }
    return index;
}

// ===========================================================================
// 0x8014f12c  subobj_island_get_bodies
// Read the two body descriptor handles out of an island slot: +0x70 (body A)
// into outBodyA and +0x74 (body B, 0 = world) into outBodyB. Either output
// pointer may be null.
// ===========================================================================
void subobj_island_get_bodies(int index, unsigned int* outBodyA, unsigned int* outBodyB)
{
    int rec = DAT_806121b4 + index * 0x80;
    if (outBodyA != 0) {
        *outBodyA = *(unsigned int*)(rec + 0x70);
    }
    if (outBodyB == 0) {
        return;
    }
    *outBodyB = *(unsigned int*)(rec + 0x74);
}

// ===========================================================================
// 0x8014f15c  subobj_island_refresh_twist
// Recompute an island's reference twist quaternion from the current poses of
// its two bodies. Resolves body-A (+0x70) and, if present, body-B (+0x74)
// descriptor pointers from the descriptor pool (DAT_8061218c, stride 0x200,
// index = handle & 0xffff), then calls subobj_solver_relative_twist with the
// island's local axis frame (+0x20) writing the twist quat back to +0x48.
// ===========================================================================
void subobj_island_refresh_twist(int index)
{
    int bodyB = 0;
    int rec = DAT_806121b4 + index * 0x80;
    if (*(unsigned int*)(rec + 0x74) != 0) {
        bodyB = DAT_8061218c + (*(unsigned int*)(rec + 0x74) & 0xffff) * 0x200;
    }
    subobj_solver_relative_twist(
        (float*)(DAT_8061218c + (*(unsigned int*)(rec + 0x70) & 0xffff) * 0x200),
        (float*)bodyB,
        (float*)(rec + 0x20),
        (float*)(rec + 0x48));
}
