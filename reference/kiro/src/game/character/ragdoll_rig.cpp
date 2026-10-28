// Address range: 0x80364808 - 0x8036568c | batch_1025 | 2026-09-12
//
// Ragdoll / dismemberment rig runtime (the counterpart to gore_rig_build in
// batch_1024, which precomputes the 7 rig templates). When a character is torn
// apart this spawns the individual body-part entities, links them with
// physics-island joints (per the rig template selected by param_11), then ticks
// them each frame: syncs the owner entity to the ragdoll's anchor, recomputes
// the pose, computes the ragdoll AABB, and finally tears the whole thing down
// and frees the pool record when the parts settle or the owner dies.
//
//   ragdoll_spawn            0x80364808  spawn + joint the ragdoll body parts
//   ragdoll_parts_alive      0x80364c40  are all ragdoll subobjects still valid
//   ragdoll_sync_anchor      0x80364cdc  per-frame: sync owner to anchor + tags
//   ragdoll_break_tick       0x80364f3c  per-part break/render tick
//   ragdoll_compute_bounds   0x80364fe8  union the part AABBs into the owner box
//   ragdoll_bake_joint_quats 0x803651a4  bake per-bone relative-rotation quats
//   ragdoll_repose           0x803652bc  repose entry (guards on lock state)
//   ragdoll_repose_impl      0x8036530c  rebuild the pose from part transforms
//   ragdoll_destroy          0x803654f0  free joints/parts + release the record
//   ptr_store                0x8036568c  trivial *p = v setter
//
// NOTE: non-matching decomp. The rig template tables are the same stride-0x4dc
// records at DAT_804920d8 used by gore_rig_config.cpp; the per-ragdoll pool
// record (from freelist -0x7fa17c10) is accessed via raw offsets.
#include "common.h"
#include "generated_decls.h"

#define RAGDOLL_POOL 0x80582630   // freelist_pop/push id (-0x7fa17c10 as int)

// ---- rig template tables (stride 0x4dc records; index * 0x137 dwords) ----
extern int DAT_804920d8;        // rig: part count (record base)
extern int PTR_DAT_804920e4;    // rig: part descriptor list ptr
extern int PTR_DAT_804920e8;    // rig: joint descriptor list ptr
extern int PTR_DAT_804920ec;    // rig: bone->part remap list ptr
extern int PTR_DAT_804920f0;    // rig: parent-index list ptr
extern short DAT_804d1cd0;      // rig anim-set id table
extern int DAT_80611824;        // animation-set handle table base
extern int DAT_80611c24;        // physics-island body pool base (stride 0x2e8)

// ---- rodata float constants ----
extern float FLOAT_8061b22c, FLOAT_8061b230, FLOAT_8061b234;   // spawn euler scale/div, life
extern float FLOAT_8061b238, FLOAT_8061b23c, FLOAT_8061b240, FLOAT_8061b244; // restitution ranges
extern float FLOAT_8061b248, FLOAT_8061b24c;                   // bounds seed min/max

// ---- raw scratch-scope + rig helpers (not yet decompiled) ----
extern unsigned long long FUN_803826c4(void);  // scratch enter (spawn: owner pair)
extern unsigned long long FUN_803826cc(void);   // scratch enter (repose: owner pair)
extern int  FUN_803826d4(void);                  // scratch enter (bake: ctx)
extern unsigned long long FUN_803826e8(void);   // scratch enter (destroy: record ptr)
extern unsigned long long FUN_803826f0(void);   // scratch enter (sync: owner pair)
extern void FUN_80382710(void);                  // scratch leave
extern void FUN_80382718(void);                  // scratch leave
extern void FUN_80382720(void);                  // scratch leave
extern void FUN_80382734(void);                  // scratch leave
extern void FUN_8038273c(void);                  // scratch leave
extern int  FUN_8036594c(void);                  // ragdoll broken-state query
extern unsigned int FUN_80365914(int rec);       // ragdoll lock/settle query
extern void FUN_8036589c(void);                  // ragdoll detach helper
extern void FUN_803656c4(int a);                 // ragdoll record cleanup

// The rig-descriptor tables are indexed as (&PTR_...)[rig * 0x137] to reach the
// stride-0x4dc record; DAT_804920dc/e0 are +4/+8 words of the same record.
#define RIG_FIELD(base, rig) (*(int*)((int)&(base) + (rig) * 0x4dc))
// +8 word of the stride-0x4dc rig record (DAT_804920e0): the root-part index.
#define RIG_ROOT_PART(rig)   (*(int*)((int)&DAT_804920d8 + (rig) * 0x4dc + 8))
// +4 word (DAT_804920dc): the rig's anim-set id.
#define RIG_ANIMSET(rig)     (*(int*)((int)&DAT_804920d8 + (rig) * 0x4dc + 4))

// Signature aliases for helpers whose generated decls dropped args/returns.
typedef int (*SpawnByTypeFn)(unsigned long long, double, unsigned long long, unsigned long long,
                             unsigned long long, unsigned long long, unsigned long long,
                             unsigned long long, int, float*, float*, int, int);
typedef unsigned int (*SubobjAllocFn)(void);
typedef void (*BuildDescFn)(unsigned long long, double, unsigned long long, unsigned long long,
                            unsigned long long, unsigned long long, unsigned long long,
                            unsigned long long, unsigned int, int, int, int, float);
typedef int (*IslandCreateFn)(unsigned int, unsigned int, float*, float*);
typedef void (*Mtx4ToQuatFn)(float* mtx, float* outQuat);
typedef unsigned long long (*TransformPointFn)(float* mtx, float* pt);
// mtx4_to_euler really writes 3 euler outputs; generated decl shows only 2.
typedef void (*Mtx4ToEulerFn)(float* mtx, float* ex, float* ey, float* ez);

// (Prototypes for functions used before their definition are provided by
//  generated_decls.h, included above.)

// -------------------------------------------------------------------------
// 0x80364808  ragdoll_spawn - spawn + joint the ragdoll body parts.
//   Allocates a ragdoll pool record, spawns one entity per rig part with a
//   sub-object physics body and an initial impulse derived from the owner's
//   velocity, then creates a physics-island joint (with per-joint twist and
//   restitution limits) between each part pair. Finally bakes the joint quats.
// -------------------------------------------------------------------------
void ragdoll_spawn(unsigned long long a1, unsigned long long a2, double vel, unsigned long long a4,
                   unsigned long long a5, unsigned long long a6, unsigned long long a7,
                   unsigned long long a8, unsigned int a9, unsigned int a10, int rigId)
{
    (void)a1; (void)a2; (void)a9; (void)a10;
    float ownerMtx[32];   // afStack_a8
    float boneMtx[16];    // afStack_e8
    float jointMtx[16];   // afStack_128
    float partPos[3];     // local_134..
    float partRot[3];     // local_140/13c/138
    float impulse[3];     // local_14c/148/144
    float jointPt[4];     // local_158/154/150

    unsigned long long ownerPair = FUN_803826c4();
    int owner = (int)(ownerPair >> 0x20);
    int poseTable = *(int*)(*(int*)(owner + 4) + 0x10);
    int boneMap   = **(int**)(*(int*)(owner + 4) + 0x84);

    int* rec = (int*)freelist_pop(RAGDOLL_POOL);
    if (rec == (int*)0) {
        FUN_80382710();
        return;
    }
    rec[0]    = (int)ownerPair;
    rec[1]    = rigId;
    rec[0x11] = *(int*)(owner + 0x2c);

    entity_build_local_matrix(ownerMtx, (void*)(intptr_t)owner, 1, 0);
    if ((*(unsigned int*)(owner + 0x28) & 0x200000) == 0) {
        *(unsigned int*)(owner + 0xc) |= 1;
        entity_force_bone_update(owner);
        poseTable = *(int*)(*(int*)(owner + 4) + 0x10);
    }
    *(unsigned int*)(owner + 0x28) |= 0x40020000;
    if (*(int*)(owner + 0x20) == 8) {
        entity_register_for_update(owner, 0xd, 0x80365e48);
        entity_register_for_update(owner, 0xb, 0x80364fe8);
        entity_set_render_type(owner, (int)(intptr_t)&ragdoll_break_tick);
    }

    double eulScale = (double)FLOAT_8061b22c;
    double eulDiv   = (double)FLOAT_8061b230;
    double partLife = (double)FLOAT_8061b234;

    int partOff = 0;
    int recOff  = 0;
    int partCount = RIG_FIELD(DAT_804920d8, rigId);
    for (int i = 0; i < partCount; i++, partOff += 0x1c, recOff += 4) {
        int* part = (int*)(RIG_FIELD(PTR_DAT_804920e4, rigId) + partOff);
        partPos[0] = (float)part[3];
        partPos[1] = (float)part[4];
        partPos[2] = (float)part[5];
        mtx4_multiply((float*)(poseTable + *(int*)(boneMap + part[0] * 4) * 0x40), ownerMtx, boneMtx);
        ((Mtx4ToEulerFn)&mtx4_to_euler)(boneMtx, &partRot[0], &partRot[1], &partRot[2]);
        partRot[0] = (float)((double)(float)(eulScale * (double)partRot[0]) / eulDiv);
        partRot[1] = (float)((double)(float)(eulScale * (double)partRot[1]) / eulDiv);
        partRot[2] = (float)((double)(float)(eulScale * (double)partRot[2]) / eulDiv);
        mtx4_transform_point(boneMtx, partPos);

        int ent = ((SpawnByTypeFn)&entity_spawn_by_type)(
            0, (double)partRot[0], vel, a4, a5, a6, a7, a8, part[1], partPos, partRot, 0, 0);
        *(int**)(ent + 0x7c) = rec;
        *(unsigned int*)(ent + 0x28) &= 0xfffffffe;
        *(float*)(ent + 0x1cc) = (float)partLife;
        *(unsigned int*)(ent + 0x80) = ((SubobjAllocFn)&subobj_pool_alloc)();
        ((BuildDescFn)&subobj_build_descriptor_default)(
            0, (double)partRot[0], vel, a4, a5, a6, a7, a8,
            *(unsigned int*)(ent + 0x80), ent, 0x11, -1, 8.0f);
        subobj_set_collision_groups(*(unsigned int*)(ent + 0x80), 2, 0xf1);
        subobj_dirty_list_add(*(unsigned int*)(ent + 0x80));

        bone_local_delta(*(unsigned int*)(owner + 4), (unsigned int)part[0], impulse);
        double velScale = (double)(float)part[6];
        impulse[0] = (float)((double)*(float*)(owner + 0xb4) * velScale + (double)impulse[0]);
        impulse[1] = (float)((double)*(float*)(owner + 0xb8) * velScale + (double)impulse[1]);
        impulse[2] = (float)((double)*(float*)(owner + 0xbc) * velScale + (double)impulse[2]);
        subobj_set_linear_impulse(*(unsigned int*)(ent + 0x80), (unsigned int*)impulse);

        rec[(recOff + 8) / 4] = ent;   // record slot +8 + i*4
    }

    int jRecOff = 0;
    int jOff = 0;
    for (int i = 0; i < RIG_FIELD(DAT_804920d8, rigId) - 1; i++, jRecOff += 4, jOff += 0x4c) {
        int* joint = (int*)(RIG_FIELD(PTR_DAT_804920e8, rigId) + jOff);
        jointPt[0] = (float)joint[1];
        jointPt[1] = (float)joint[2];
        jointPt[2] = (float)joint[3];
        mtx4_multiply((float*)(poseTable + *(int*)(boneMap + joint[0] * 4) * 0x40), ownerMtx, jointMtx);
        mtx4_transform_point(jointMtx, jointPt);

        unsigned int childBody = (joint[6] < 0) ? 0u
            : *(unsigned int*)(rec[joint[6] + 2] + 0x80);
        int island = ((IslandCreateFn)&subobj_island_create_axis)(
            *(unsigned int*)(rec[joint[5] + 2] + 0x80), childBody, jointPt, &jointMtx[joint[4] * 4]);
        int* islandSlot = (int*)((int)rec + jRecOff + 0x28);
        *islandSlot = island;
        subobj_island_set_rel_quat(island, (float*)(joint + 9));
        subobj_island_set_axis_world(island, 0, (float*)(joint + 0xd));
        subobj_island_set_axis_world(island, 1, (float*)(joint + 0x10));
        subobj_island_set_twist_limits((double)(float)joint[7], (double)(float)joint[8], island);
        if (rigId == 6) {
            subobj_island_set_restitution_range((double)FLOAT_8061b238, (double)FLOAT_8061b23c, island);
        } else {
            subobj_island_set_restitution_range((double)FLOAT_8061b240, (double)FLOAT_8061b244, island);
        }
    }

    int animHandle = *(int*)(DAT_80611824 + (short)(&DAT_804d1cd0)[(int)ownerPair] * 4);
    ragdoll_bake_joint_quats(animHandle, owner, (int)rec, poseTable);
    FUN_80382710();
}

// -------------------------------------------------------------------------
// 0x80364c40  ragdoll_parts_alive - are all ragdoll subobjects still valid.
//   Returns 0 if the record is null/empty or any part has lost its sub-object
//   body or has an out-of-range material id (tearing down the ragdoll if so),
//   1 otherwise. This is the check the grapple weapon uses before grabbing.
// -------------------------------------------------------------------------
unsigned int ragdoll_parts_alive(int* recPtr)
{
    if ((recPtr == (int*)0) || (*recPtr == 0)) {
        return 0;
    }
    int rec = *recPtr;
    int count = RIG_FIELD(DAT_804920d8, *(int*)(rec + 4));
    int off = 0;
    while (count > 0) {
        int part = *(int*)(rec + off + 8);
        int mat;
        if ((*(int*)(part + 0x80) == 0) || (mat = *(int*)(part + 0x34), mat < 1) || (0xd5 < mat)) {
            ragdoll_destroy();
            return 0;
        }
        off += 4;
        count--;
    }
    return 1;
}

// -------------------------------------------------------------------------
// 0x80364cdc  ragdoll_sync_anchor - per-frame: sync owner to anchor + tags.
//   While the ragdoll is intact, offsets the owner entity to follow the root
//   part's anchor, copies velocity/material, and gathers the union of the
//   parts' render tags (up to 10) into the owner. If the ragdoll has broken and
//   settled, detaches it. Always marks the owner's bone matrices dirty.
// -------------------------------------------------------------------------
void ragdoll_sync_anchor(void)
{
    float ownerMtx[16];   // afStack_9c
    float rootMtx[12];    // afStack_5c (+ local_2c/28/24 tail = its translation)
    float anchor[3];      // local_a8/a4/a0

    unsigned long long pair = FUN_803826f0();
    int owner = (int)(pair >> 0x20);
    int rec   = (int)pair;

    if (FUN_8036594c() == 0) {
        if ((*(unsigned int*)(owner + 0x28) & 0x200000) != 0) {
            int rig = *(int*)(rec + 4);
            entity_build_local_matrix(ownerMtx, (void*)(intptr_t)owner, 1, 0);
            mtx4_multiply((float*)(*(int*)(*(int*)(owner + 4) + 0x10) +
                                   *(int*)(*(int*)(*(int*)(owner + 4) + 0x84) + 0x18) * 0x40),
                          ownerMtx, rootMtx);
            if (RIG_ROOT_PART(rig) < 0) {
                float* o = (float*)subobj_get_contact_origin(*(unsigned int*)(*(int*)(rec + 8) + 0x80));
                anchor[0] = o[0]; anchor[1] = o[1]; anchor[2] = o[2];
            } else {
                subobj_island_get_anchor_world(
                    *(int*)(rec + RIG_ROOT_PART(rig) * 4 + 0x28), 1, anchor);
            }
            // rootMtx translation is at rootMtx[9..11] (local_2c/28/24)
            *(float*)(owner + 0x64) += (anchor[0] - rootMtx[9]);
            *(float*)(owner + 0x68) += (anchor[1] - rootMtx[10]);
            *(float*)(owner + 0x6c) += (anchor[2] - rootMtx[11]);
            *(int*)(owner + 0xb4) = *(int*)(*(int*)(rec + 8) + 0xb4);
            *(int*)(owner + 0xb8) = *(int*)(*(int*)(rec + 8) + 0xb8);
            *(int*)(owner + 0xbc) = *(int*)(*(int*)(rec + 8) + 0xbc);
            *(int*)(owner + 0x34) = *(int*)(*(int*)(rec + 8) + 0x34);
            *(unsigned char*)(owner + 0x30) = 0;

            int partIdx = 0;
            int partRecOff = 0;
            int partCount = RIG_FIELD(DAT_804920d8, rig);
            while ((partIdx < partCount) && (*(char*)(owner + 0x30) < '\n')) {
                int part = *(int*)(rec + partRecOff + 8);
                int tagOff = 0;
                for (int t = 0; t < *(char*)(part + 0x30); t++, tagOff += 4) {
                    if ('\t' < *(char*)(owner + 0x30)) break;
                    int have = (int)*(char*)(owner + 0x30);
                    bool dup = false;
                    int oOff = 0;
                    while (have > 0) {
                        if (*(int*)(owner + oOff + 0x38) == *(int*)(part + tagOff + 0x38)) { dup = true; break; }
                        oOff += 4;
                        have--;
                    }
                    if (!dup) {
                        char n = *(char*)(owner + 0x30);
                        *(char*)(owner + 0x30) = n + 1;
                        *(int*)(owner + n * 4 + 0x38) = *(int*)(part + tagOff + 0x38);
                    }
                }
                partIdx++;
                partRecOff += 4;
            }
        }
    } else {
        if (FUN_80365914(rec) == 0) {
            FUN_8036589c();
        }
    }
    *(unsigned int*)(owner + 0xc) |= 1;
    FUN_8038273c();
}

// -------------------------------------------------------------------------
// 0x80364f3c  ragdoll_break_tick - per-part break/render tick.
//   For the active split-screen player's view bone, if this bone maps to a
//   ragdoll part with a live sub-object, temporarily retargets the record's
//   subject to that part and runs the shared break/render tick.
// -------------------------------------------------------------------------
void ragdoll_break_tick(unsigned long long a1, unsigned long long a2, double a3, double a4,
                        double a5, double a6, double a7, unsigned long long a8, int* rec)
{
    int owner = *rec;
    int view = player_ai_get_active_index();
    int rig = *(int*)(*(int*)(owner + 0x7c) + 0x4c);
    int remap = *(int*)(RIG_FIELD(PTR_DAT_804920ec, *(int*)(rig + 4)) +
                        *(int*)(*(int*)(*(int*)(*(int*)(owner + 4) + 0x84) + 0x5c) + view * 4) * 4);
    if (remap != -1) {
        unsigned int handle = *(unsigned int*)(*(int*)(rig + remap * 4 + 8) + 0x80);
        if (handle != 0) {
            *rec = subobj_get_entity(handle);
            subobj_break_tick_impl(a1, a2, a3, a4, a5, a6, a7, a8);
            *rec = owner;
        }
    }
}

// -------------------------------------------------------------------------
// 0x80364fe8  ragdoll_compute_bounds - union the part AABBs into owner box.
//   For a live item ragdoll, walks all parts, unions their world-space
//   (centre +/- extents) boxes, then writes the combined half-extents and
//   centre offset back onto the owner entity (+0x1a8 centre, +0x1c0 extents).
// -------------------------------------------------------------------------
void ragdoll_compute_bounds(int owner)
{
    int rigHolder = *(int*)(owner + 0x7c);
    if ((*(int*)(owner + 0x20) != 8)) return;
    int rig = *(int*)(rigHolder + 0x4c);
    if (rig == 0) return;
    if (FUN_80365914(rig) != 0) return;

    int recRig = *(int*)(rigHolder + 0x4c);
    int off = 0;
    int count = RIG_FIELD(DAT_804920d8, *(int*)(recRig + 4));
    float minX = FLOAT_8061b248, maxX = FLOAT_8061b24c;
    float minY = FLOAT_8061b248, maxY = FLOAT_8061b24c;
    float minZ = FLOAT_8061b248, maxZ = FLOAT_8061b24c;

    for (int i = 0; i < count; i++, off += 4) {
        int part = *(int*)(recRig + off + 8);
        float cx = *(float*)(part + 0x64) + *(float*)(part + 0x1a8);
        float cy = *(float*)(part + 0x68) + *(float*)(part + 0x1ac);
        float cz = *(float*)(part + 0x6c) + *(float*)(part + 0x1b0);
        float ex = *(float*)(part + 0x1c0), ey = *(float*)(part + 0x1c4), ez = *(float*)(part + 0x1c8);
        float loX = cx - ex, hiX = cx + ex;
        float loY = cy - ey, hiY = cy + ey;
        float loZ = cz - ez, hiZ = cz + ez;
        if (loX < minX) minX = loX;
        if (loY < minY) minY = loY;
        if (loZ < minZ) minZ = loZ;
        if (hiX > maxX) maxX = hiX;   // note: source keeps running max via swaps
        if (hiY > maxY) maxY = hiY;
        if (hiZ > maxZ) maxZ = hiZ;
    }

    float hx = (maxX - minX) * FLOAT_8061b23c;
    float hy = (maxY - minY) * FLOAT_8061b23c;
    float hz = (maxZ - minZ) * FLOAT_8061b23c;
    *(float*)(owner + 0x1c0) = hx;
    *(float*)(owner + 0x1c4) = hy;
    *(float*)(owner + 0x1c8) = hz;
    *(float*)(owner + 0x1a8) = (hx + minX) - *(float*)(owner + 0x64);
    *(float*)(owner + 0x1ac) = (hy + minY) - *(float*)(owner + 0x68);
    *(float*)(owner + 0x1b0) = (hz + minZ) - *(float*)(owner + 0x6c);
}

// -------------------------------------------------------------------------
// 0x803651a4  ragdoll_bake_joint_quats - bake per-bone relative-rotation quats.
//   For each anim bone, composes the part matrix relative to its parent and
//   stores the resulting quaternion into the pose-bone record (+0xc..+0x18),
//   so the runtime can drive the mesh from the physics parts.
// -------------------------------------------------------------------------
void ragdoll_bake_joint_quats(int animHandle, int owner, int rec, int boneMtx)
{
    (void)animHandle; (void)owner;
    float rel[26];   // afStack_68
    float quat[4];   // local_78..local_6c

    int ctx = FUN_803826d4();
    int rig = *(int*)(rec + 4);
    int boneOff = 0;
    int mtxOff = 0;
    int poseOff = 0;
    int boneCount = *(int*)(*(int*)(DAT_80611824 +
                    (short)(&DAT_804d1cd0)[RIG_FIELD(*(int*)((int)&DAT_804920d8 + 4), rig)] * 4) + 0x14);
    for (int b = 0; b < boneCount; b++, boneOff += 4, mtxOff += 0x40, poseOff += 0x20) {
        int poseBone = *(int*)(*(int*)(ctx + 0x28) + poseOff + 0x14);
        int parent = *(int*)(RIG_FIELD(PTR_DAT_804920f0, rig) + boneOff);
        if (parent != -1) {
            mtx4_invert(rel, (float*)(boneMtx + parent * 0x40));
            mtx4_multiply((float*)(boneMtx + mtxOff), rel, rel);
        }
        if (*(int*)(RIG_FIELD(PTR_DAT_804920ec, rig) + boneOff) != -1) {
            ((Mtx4ToQuatFn)&mtx4_to_quat)(rel, quat);
            *(float*)(poseBone + 0xc)  = quat[0];
            *(float*)(poseBone + 0x10) = quat[1];
            *(float*)(poseBone + 0x14) = quat[2];
            *(float*)(poseBone + 0x18) = quat[3];
        }
    }
    FUN_80382720();
}

// -------------------------------------------------------------------------
// 0x803652bc  ragdoll_repose - repose entry (guards on lock state).
// -------------------------------------------------------------------------
void ragdoll_repose(unsigned int a1, int rec)
{
    (void)a1;
    if (FUN_80365914(rec) == 0) {
        ragdoll_repose_impl();
    }
}

// -------------------------------------------------------------------------
// 0x8036530c  ragdoll_repose_impl - rebuild the pose from part transforms.
//   Builds each part's world matrix (from its sub-object, translated to the
//   contact origin) relative to the owner, composes with the inverse-bind, then
//   bakes the joint quaternions via ragdoll_bake_joint_quats.
// -------------------------------------------------------------------------
void ragdoll_repose_impl(void)
{
    unsigned long long pair = FUN_803826cc();
    int owner = (int)(pair >> 0x20);
    int* rec = (int*)(int)pair;
    int rig = rec[1];
    int recBase = (int)&DAT_804920d8 + rig * 0x4dc;

    float* page = (float*)mem_page_alloc();
    float* partMtx = page + 0x10;                       // per-part matrices
    float* boneOut = page + RIG_FIELD(DAT_804920d8, rig) * 0x10 + 0x20;  // composed bone matrices
    int boneCount = *(int*)(*(int*)(DAT_80611824 +
                    (short)(&DAT_804d1cd0)[RIG_FIELD(*(int*)((int)&DAT_804920d8 + 4), rig)] * 4) + 0x14);

    entity_build_local_matrix(page, (void*)(intptr_t)owner, 1, 0);
    mtx4_invert(partMtx, page);

    int mOff = 0;
    int recOff = 0;
    int partCount = RIG_FIELD(DAT_804920d8, rig);
    for (int i = 0; i < partCount; i++, mOff += 0x40, recOff += 4) {
        unsigned int handle = *(unsigned int*)(*(int*)((int)rec + recOff + 8) + 0x80);
        float* dst = (float*)((int)page + mOff + 0x80);
        if (handle == 0) {
            mtx4_copy(partMtx, dst);
        } else {
            mtx4_copy((float*)subobj_get_object_matrix(handle), dst);
            float* o = (float*)subobj_get_contact_origin(handle);
            dst[0xc] = o[0];
            dst[0xd] = *(float*)(subobj_get_contact_origin(handle) + 4);
            dst[0xe] = *(float*)(subobj_get_contact_origin(handle) + 8);
            mtx4_multiply(dst, partMtx, dst);
        }
    }

    int cOff = 0;
    int rOff = 0;
    for (int b = 0; b < boneCount; b++, cOff += 0x40, rOff += 4) {
        if (*(int*)(RIG_FIELD(PTR_DAT_804920ec, rig) + rOff) == -1) {
            mtx4_set_identity((int)((char*)boneOut + cOff));
        } else {
            mtx4_multiply((float*)(recBase + cOff + 0x1c),
                          page + *(int*)(RIG_FIELD(PTR_DAT_804920ec, rig) + rOff) * 0x10 + 0x20,
                          (float*)((int)boneOut + cOff));
        }
    }

    int animHandle = *(int*)(DAT_80611824 + (short)(&DAT_804d1cd0)[rec[0]] * 4);
    ragdoll_bake_joint_quats(animHandle, owner, (int)rec, (int)boneOut);
    mem_page_free((int)boneOut);
    FUN_80382718();
}

// -------------------------------------------------------------------------
// 0x803654f0  ragdoll_destroy - free joints/parts + release the record.
//   Frees every active physics-island joint, destroys the part entities,
//   clears the owner's ragdoll flags/render hooks, then releases the pool
//   record back to the freelist.
// -------------------------------------------------------------------------
void ragdoll_destroy(void)
{
    int* recPtr = (int*)FUN_803826e8();
    int rec = *recPtr;
    int rig = *(int*)(rec + 4);

    int off = 0;
    for (int i = 0; i < RIG_FIELD(DAT_804920d8, rig) - 1; i++, off += 4) {
        int island = *(int*)(rec + off + 0x28);
        if (subobj_island_is_active(island)) {
            subobj_island_pool_free(island);
        }
    }
    off = 0;
    for (int i = 0; i < RIG_FIELD(DAT_804920d8, rig); i++, off += 4) {
        entity_destroy_with_event(*(int**)(rec + off + 8));
    }

    unsigned int h = *(unsigned int*)(rec + 0x44);
    int body = 0;
    if (h != 0) {
        body = DAT_80611c24 + (h >> 0x16) * 0x2e8;
        if (*(unsigned int*)(body + 0x2c) != h) {
            body = 0;
            *(int*)(rec + 0x44) = 0;
        }
    }
    if (body != 0) {
        *(unsigned int*)(body + 0x28) &= 0xbffdffff;
        entity_set_render_type(body, 0);
        entity_register_for_update(body, 0xb, 0);
        entity_register_for_update(body, 0xd, 0);
    }
    *(int*)(rec + 0x44) = 0;
    FUN_803656c4(*(int*)rec);
    *(int*)rec = 0;
    freelist_push(RAGDOLL_POOL, (void**)rec);
    *recPtr = 0;
    if ((body != 0) && (*(int*)(body + 0x20) == 8)) {
        *(int*)(*(int*)(body + 0x7c) + 0x4c) = 0;
    }
    FUN_80382734();
}

// -------------------------------------------------------------------------
// 0x8036568c  ptr_store - trivial *p = v setter.
// -------------------------------------------------------------------------
void ptr_store(int* p, int v)
{
    *p = v;
}

// ============================================================================
// Address range: 0x80365694 - 0x80365bb0 | batch_1026 | 2026-09-12
//
// Rest of the ragdoll rig module: the gore-piece anim-set id recycle stack, the
// deferred physics-force queue applied post-transform, the part dirty-list
// add/remove helpers, the idle/settle queries, the pool initialiser, and the
// anchor accessor. These are the raw FUN_ helpers the grapple weapon and the
// ragdoll_* runtime above reference.
//
//   gore_id_acquire          0x80365694  pop a gore-piece anim-set id (or -1)
//   gore_id_release          0x803656c4  push a gore-piece anim-set id back
//   ragdoll_flush_force_queue 0x803656fc  apply queued forces/impulses to parts
//   ragdoll_parts_dirty_add   0x80365824  add every live part to the dirty list
//   ragdoll_parts_dirty_remove 0x8036589c remove every live part from the dirty list
//   ragdoll_is_settled        0x80365914  is the root part idle / has no body
//   ragdoll_check_settled     0x8036594c  settle test over accumulated impulses
//   ragdoll_system_init       0x80365b18  init the ragdoll pool + id stack
//   ragdoll_system_alloc      0x80365b90  forwarder to ragdoll_system_init
//   ragdoll_get_anchor        0x80365bb0  fetch a ragdoll's world anchor point
// ============================================================================

// ---- id-recycle + force-queue globals ----
extern int DAT_80612e6c;      // gore-id stack depth
extern int DAT_805e8400;      // gore-id stack (anim-set ids 0xe5..0xe7)
extern int DAT_80612e68;      // force-queue cursor
extern int DAT_805e840c;      // force-queue: record ptr (stride 0x20)
extern int DAT_805e8410;      // force-queue: part index (stride 0x20)
extern int DAT_805e83f0;      // ragdoll pool freelist head
extern int DAT_805e850c;      // ragdoll pool storage base
extern int DAT_804920e0;      // rig record +8: root-part index (stride 0x4dc)
extern float FLOAT_8061b250;  // settle velocity^2 threshold

// register spill-frame accessor: the ragdoll record arrives in the r25 spill
// (matching the stream_*.cpp idiom); no-op restore.
extern int  __save_gpr_25(void);
static inline void __restore_gpr_25(void) {}

// -------------------------------------------------------------------------
// 0x80365694  gore_id_acquire - pop a gore-piece anim-set id (or -1).
// -------------------------------------------------------------------------
int gore_id_acquire(void)
{
    if (DAT_80612e6c != 0) {
        DAT_80612e6c -= 1;
        return (&DAT_805e8400)[DAT_80612e6c];
    }
    return -1;
}

// -------------------------------------------------------------------------
// 0x803656c4  gore_id_release - push a gore-piece anim-set id back (0xe5..0xe7).
// -------------------------------------------------------------------------
void gore_id_release(int id)
{
    if (2 < DAT_80612e6c) return;
    if (id < 0xe5) return;
    if (0xe7 < id) return;
    int* slot = &DAT_805e8400 + DAT_80612e6c;
    DAT_80612e6c += 1;
    *slot = id;
}

// -------------------------------------------------------------------------
// 0x803656fc  ragdoll_flush_force_queue - apply queued forces/impulses to parts.
//   Drains the 8-slot deferred-force queue (filled during the frame). For each
//   queued ragdoll: if the target part index is out of range, applies the
//   linear force to every part; otherwise applies an impulse at a local point
//   on the one targeted part. Runs in the post-transform pass.
// -------------------------------------------------------------------------
void ragdoll_flush_force_queue(void)
{
    __save_gpr_25();
    while (DAT_80612e68 < 8) {
        int base = DAT_80612e68 * 0x20;
        DAT_80612e68 += 1;
        int rec = *(int*)(&DAT_805e840c + base);
        unsigned int h = *(unsigned int*)(rec + 0x44);
        int body = 0;
        if (h != 0) {
            body = DAT_80611c24 + (h >> 0x16) * 0x2e8;
            if (*(unsigned int*)(body + 0x2c) != h) {
                body = 0;
                *(int*)(rec + 0x44) = 0;
            }
        }
        if (body != 0) {
            int rig = *(int*)(rec + 4);
            int part = *(int*)(&DAT_805e8410 + base);
            if ((part < 0) || (RIG_FIELD(DAT_804920d8, rig) <= part)) {
                int off = 0;
                for (int i = 0; i < RIG_FIELD(DAT_804920d8, rig); i++, off += 4) {
                    unsigned int sub = *(unsigned int*)(*(int*)(rec + off + 8) + 0x80);
                    if (sub != 0) {
                        subobj_add_linear_force(sub, (float*)(base + 0x805e8420));
                    }
                }
            } else {
                unsigned int sub = *(unsigned int*)(*(int*)(rec + part * 4 + 8) + 0x80);
                if (sub != 0) {
                    subobj_apply_impulse_at_local_point(sub, (unsigned int)(base + 0x805e8414),
                                                        (float*)(base + 0x805e8420));
                }
            }
        }
    }
    __restore_gpr_25();
}

// -------------------------------------------------------------------------
// 0x80365824  ragdoll_parts_dirty_add - add every live part to the dirty list.
// -------------------------------------------------------------------------
void ragdoll_parts_dirty_add(void)
{
    int rec = (int)FUN_803826e8();
    int rig = *(int*)(rec + 4);
    int off = 0;
    for (int i = 0; i < RIG_FIELD(DAT_804920d8, rig); i++, off += 4) {
        unsigned int sub = *(unsigned int*)(*(int*)(rec + off + 8) + 0x80);
        if (sub != 0) subobj_dirty_list_add(sub);
    }
    FUN_80382734();
}

// -------------------------------------------------------------------------
// 0x8036589c  ragdoll_parts_dirty_remove - remove every live part from dirty list.
// -------------------------------------------------------------------------
void ragdoll_parts_dirty_remove(void)
{
    int rec = (int)FUN_803826e8();
    int rig = *(int*)(rec + 4);
    int off = 0;
    for (int i = 0; i < RIG_FIELD(DAT_804920d8, rig); i++, off += 4) {
        unsigned int sub = *(unsigned int*)(*(int*)(rec + off + 8) + 0x80);
        if (sub != 0) subobj_dirty_list_remove(sub);
    }
    FUN_80382734();
}

// -------------------------------------------------------------------------
// 0x80365914  ragdoll_is_settled - is the root part idle / has no body.
// -------------------------------------------------------------------------
unsigned int ragdoll_is_settled(int rec)
{
    unsigned int sub = *(unsigned int*)(*(int*)(rec + 8) + 0x80);
    if (sub == 0) return 1;
    return subobj_is_idle(sub);
}

// -------------------------------------------------------------------------
// 0x8036594c  ragdoll_check_settled - settle test over accumulated impulses.
//   Accumulates each non-idle part's linear impulse and contact normal; the
//   ragdoll is considered still moving while the summed impulse magnitude
//   exceeds FLOAT_8061b250. (Return surfaced via countLeadingZeros in the
//   original; here the aggregate comparison drives the result.)
// -------------------------------------------------------------------------
unsigned int ragdoll_check_settled(void)
{
    int rec = __save_gpr_25();
    float seed = FLOAT_8061b234;
    int rig = *(int*)(rec + 4);
    float impX = seed, impY = seed, impZ = seed;
    float nX = seed, nY = seed, nZ = seed;
    int off = 0;
    unsigned int result = 0;

    for (int i = 0; i < RIG_FIELD(DAT_804920d8, rig); i++, off += 4) {
        unsigned int sub = *(unsigned int*)(*(int*)(rec + off + 8) + 0x80);
        unsigned int flags = subobj_get_flags(sub);
        if ((flags & 0x4000) != 0) {
            __restore_gpr_25();
            return 0;
        }
        if ((sub != 0) && (subobj_is_idle(sub) == 0)) {
            float* imp = (float*)subobj_get_linear_impulse(sub);
            impX = (float)((double)impX + (double)imp[0]);
            impY = (float)((double)impY + (double)imp[1]);
            impZ = (float)((double)impZ + (double)imp[2]);
            float* nrm = (float*)subobj_get_contact_normal(sub);
            nX = (float)((double)nX + (double)nrm[0]);
            nY = (float)((double)nY + (double)nrm[1]);
            nZ = (float)((double)nZ + (double)nrm[2]);
        }
    }

    if ((impZ * impZ + (impX * impX + impY * impY)) <= FLOAT_8061b250) {
        result = (FLOAT_8061b250 < (nZ * nZ + (nX * nX + nY * nY))) ? 1u : 0u;
    } else {
        result = 1u;
    }
    __restore_gpr_25();
    return result;
}

// -------------------------------------------------------------------------
// 0x80365b18  ragdoll_system_init - init the ragdoll pool + gore-id stack.
//   Initialises the ragdoll pool freelist (0x48-byte records x 0xb), seeds the
//   gore-id recycle stack with anim-set ids 0xe5/0xe6/0xe7, and marks the
//   force-queue empty.
// -------------------------------------------------------------------------
void ragdoll_system_init(void)
{
    freelist_init((void**)&DAT_805e83f0, 0x48, (void*)&DAT_805e850c, 0xb);
    DAT_80612e6c = 3;
    int id = 0xe5;
    int off = 0;
    for (int i = 0; i < 3; i++) {
        *(int*)((int)&DAT_805e8400 + off) = id;
        id += 1;
        off += 4;
    }
    DAT_80612e68 = 8;
}

// -------------------------------------------------------------------------
// 0x80365b90  ragdoll_system_alloc - forwarder to ragdoll_system_init.
// -------------------------------------------------------------------------
void ragdoll_system_alloc(void)
{
    ragdoll_system_init();
}

// -------------------------------------------------------------------------
// 0x80365bb0  ragdoll_get_anchor - fetch a ragdoll's world anchor point.
//   For a rig with no jointed root (root-part index < 0) returns the root
//   part's contact origin; otherwise the root island's anchor. Returns the
//   root part's sub-object handle.
// -------------------------------------------------------------------------
unsigned int ragdoll_get_anchor(int rec, float* out)
{
    if (RIG_ROOT_PART(*(int*)(rec + 4)) < 0) {
        float* o = (float*)subobj_get_contact_origin(*(unsigned int*)(*(int*)(rec + 8) + 0x80));
        out[0] = o[0];
        out[1] = o[1];
        out[2] = o[2];
    } else {
        subobj_island_get_anchor_world(
            *(int*)(rec + RIG_ROOT_PART(*(int*)(rec + 4)) * 4 + 0x28), 0, out);
    }
    return *(unsigned int*)(*(int*)(rec + 8) + 0x80);
}

// ============================================================================
// Address range: 0x80365c4c - 0x80365eab | batch_1027 | 2026-09-12
//
// Ragdoll island-joint attach path + the per-frame force helpers. When some
// external system (a level node placer, the worm boss, the grapple weapon)
// wants to pin or push the ragdoll, these lazily create a physics-island joint
// between a driver body and the ragdoll's parts, apply queued forces to every
// part, and (on the death-projectile update callback registered in
// ragdoll_spawn) spawn the finishing projectile and tear the ragdoll down.
//
//   ragdoll_create_island_point   0x80365c4c  create an island point on a part
//   ragdoll_attach_island_joint   0x80365ce0  lazily joint a driver body -> part
//   ragdoll_apply_force_all_parts 0x80365dc8  push every live part with a force
//   ragdoll_spawn_death_projectile 0x80365e48 death cb: spawn projectile + destroy
//
// NOTE: non-matching decomp. The driver body arrives in the r25 spill (the
// same idiom used by ragdoll_check_settled above); FUN_803826e4 is the
// scratch-enter that returns the (forceVec, record) pair for the force helper.
// ============================================================================

// ---- additional rodata float constants for the joint attach path ----
extern float FLOAT_8061b254;   // island drift gain
extern float FLOAT_8061b258;   // island restitution

// scratch-enter for the force helper: hi word = record ptr, the low bits are
// the packed force-vector pointer (used directly as float* uVar6).
extern unsigned long long FUN_803826e4(void);   // scratch enter (force: vec + rec)
extern void FUN_80382730(void);                  // scratch leave

// NOTE: the named island helpers (subobj_island_find_by_body_b,
// subobj_island_create_point, subobj_island_set_joint_callback/_drift_gain/
// _restitution/_anchor, subobj_add_scaled_linear_force, projectile_spawn,
// projectile_attach_effect) resolve through generated_decls.h /
// generated_externs.h - do NOT redeclare them here (the generated permissive
// "int name(...)" would clash with a typed extern; see the subobj_* helpers
// already used above for the same reason).
//
// A few of those generated prototypes were stripped by Ghidra (void return or
// missing args); we call through typed function-pointer casts to match the
// real call site, the same idiom ragdoll_spawn uses for entity_spawn_by_type.
typedef int  (*IslandCreatePointFn)(unsigned int body, int which, float* pt);
typedef void (*IslandSetCbFn)(int island, int body);
typedef void (*IslandSetAnchorFn)(int island, int which, unsigned int* anchor);
typedef unsigned int (*ProjSpawnFn)(unsigned long long, double, double, unsigned long long,
                                    unsigned long long, unsigned long long, unsigned long long,
                                    unsigned long long);
typedef void (*ProjAttachFn)(int, double, double, unsigned long long, unsigned long long,
                             unsigned long long, unsigned long long, unsigned long long,
                             unsigned int, unsigned int, int);

// -------------------------------------------------------------------------
// 0x80365c4c  ragdoll_create_island_point - create an island point on a part.
//   Builds the owner's local matrix, composes it with the target part's bone
//   transform, then creates a physics-island point on that part's sub-object
//   body at the resulting world position. Returns the new island handle (or
//   -1 when the owner is not in the ragdoll state).
// -------------------------------------------------------------------------
int ragdoll_create_island_point(int recArg, int partArg, int partIdx, int boneIdx)
{
    (void)recArg; (void)partArg;
    float composed[18];   // afStack_98
    float pt[4];          // afStack_68
    float ownerMtx[22];   // afStack_58

    int island = -1;
    unsigned long long pair = FUN_803826f0();
    int owner = (int)(pair >> 0x20);
    if ((*(unsigned int*)(owner + 0x28) & 0x200000) != 0) {
        int part = *(int*)((int)pair + partIdx * 4 + 8);
        entity_build_local_matrix(ownerMtx, (void*)(intptr_t)owner, 1, 0);
        mtx4_multiply((float*)(*(int*)(*(int*)(owner + 4) + 0x10) + boneIdx * 0x40),
                      ownerMtx, composed);
        island = ((IslandCreatePointFn)&subobj_island_create_point)(
            *(unsigned int*)(part + 0x80), 0, pt);
    }
    FUN_8038273c();
    return island;
}

// -------------------------------------------------------------------------
// 0x80365ce0  ragdoll_attach_island_joint - lazily joint a driver body -> part.
//   Given a driver physics body (arriving in the r25 spill), finds an existing
//   island joint for it. If none exists and the owner is a live ragdoll whose
//   parts are all valid, creates a new island point on the requested part and
//   configures the joint (callback, drift gain, restitution). Either way, if a
//   joint is present its anchor is set to the supplied point.
//   Callers: ghz_gz2_node_place, worm_master_update.
// -------------------------------------------------------------------------
void ragdoll_attach_island_joint(int recArg, int bodyArg, int partIdx, int boneIdx,
                                 unsigned int* anchor)
{
    (void)recArg; (void)bodyArg;
    unsigned long long pair = (unsigned long long)(unsigned int)__save_gpr_25();
    int owner = (int)(pair >> 0x20);
    int body  = (int)pair;

    int island = subobj_island_find_by_body_b(body);
    if ((island == -1) && (owner != 0) &&
        ((*(unsigned int*)(owner + 0x28) & 0x200000) != 0) &&
        ((*(unsigned int*)(owner + 0x28) & 0x100000) != 0)) {
        int rig = *(int*)(owner + 0x7c);
        if (ragdoll_parts_alive((int*)(rig + 0x4c)) != 0) {
            island = ragdoll_create_island_point(owner, *(int*)(rig + 0x4c), partIdx, boneIdx);
            if (island != -1) {
                ((IslandSetCbFn)&subobj_island_set_joint_callback)(island, body);
                subobj_island_set_drift_gain((double)FLOAT_8061b254, island);
                subobj_island_set_restitution((double)FLOAT_8061b258, island);
            }
        }
    }
    if (island != -1) {
        ((IslandSetAnchorFn)&subobj_island_set_anchor)(island, 1, anchor);
    }
    __restore_gpr_25();
}

// -------------------------------------------------------------------------
// 0x80365dc8  ragdoll_apply_force_all_parts - push every live part with a force.
//   Adds a scaled linear force (the packed vector returned by the scratch
//   enter) to every ragdoll part that still has a sub-object body.
//   Caller: grapple_attached_update.
// -------------------------------------------------------------------------
void ragdoll_apply_force_all_parts(void)
{
    unsigned long long pair = FUN_803826e4();
    int rec = (int)(pair >> 0x20);
    int rig = *(int*)(rec + 4);
    int off = 0;
    int count = RIG_FIELD(DAT_804920d8, rig);
    for (int i = 0; i < count; i++, off += 4) {
        unsigned int sub = *(unsigned int*)(*(int*)(rec + off + 8) + 0x80);
        if (sub != 0) {
            subobj_add_scaled_linear_force(sub, (float*)(unsigned int)pair);
        }
    }
    FUN_80382730();
}

// -------------------------------------------------------------------------
// 0x80365e48  ragdoll_spawn_death_projectile - death cb: spawn projectile + destroy.
//   The entity update-callback (id 0xd) registered by ragdoll_spawn. Spawns
//   the finishing projectile; if the ragdoll parts are still alive it attaches
//   the projectile effect to the ragdoll rig and then tears the ragdoll down.
// -------------------------------------------------------------------------
void ragdoll_spawn_death_projectile(unsigned long long a1, double a2, double a3,
                                    unsigned long long a4, unsigned long long a5,
                                    unsigned long long a6, unsigned long long a7,
                                    unsigned long long a8, int owner)
{
    unsigned int proj = ((ProjSpawnFn)&projectile_spawn)(a1, a2, a3, a4, a5, a6, a7, a8);
    int rig = *(int*)(owner + 0x7c);
    if (ragdoll_parts_alive((int*)(rig + 0x4c)) != 0) {
        ((ProjAttachFn)&projectile_attach_effect)(0, a2, a3, a4, a5, a6, a7, a8, proj,
                                                  *(unsigned int*)(rig + 0x4c), 0);
        ragdoll_destroy();
    }
}
