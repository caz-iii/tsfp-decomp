// Address range: 0x80144e2c - 0x80145ba0 | batch_410 | 2026-09-05
//
// Entity render sub-object BUILD pipeline (companion to subobj_pool.cpp).
//
// subobj_pool.cpp (batch_409) owns allocation/free of the sub-object handle
// slots. This module owns:
//   * one-time allocation of the parallel pool tables sized to the current
//     game mode (subobj_pool_alloc_buffers / subobj_pool_clear_state), and
//   * per-spawn construction of a sub-object render descriptor from an owner
//     entity: bounds, object/inverse matrices, world-space bounding sphere and
//     the 4-corner screen quad used for the sub-object's billboard/decal.
//
// Handle layout (shared with subobj_pool.cpp):
//   descriptor  = DAT_8061218c + (handle & 0xffff) * 0x200
//   entitySlot  = DAT_80612184 + (handle & 0xffff) * 4   (int* -> owner entity)
//
// Descriptor float-word offsets referenced here (index into the 0x200 block,
// i.e. word = byte/4):
//   [0x20..0x2f] inverse object matrix     (byte 0x80)
//   [0x30..0x3f] object matrix             (byte 0xc0)
//   [0x44..0x4f] per-bone scalar table     (byte 0x110)
//   [0x50..0x52] world center              (byte 0x140)
//   [0x53..0x58] world min/max seed        (byte 0x14c)
//   [0x59..0x5b] local center              (byte 0x164)
//   [0x5c..0x5e] local max corner          (byte 0x170)
//   [0x5f..0x61] local extent              (byte 0x17c)
//   [0x6a]       inv-scale                 (byte 0x1a8)
//   [0x70]       flags word                (byte 0x1c0)
//   [0x73]       lod byte                  (byte 0x1cc)
//   [0x75..0x77] anim/timer scratch        (byte 0x1d4)
// Owner-entity offsets: +0x28 flags, +0x80 sub-object handle, +0x4 model,
//   +0x64/+0x68/+0x6c world position, +0x1cc/+0x1d8 lod distances.

#include "common.h"
#include "generated_decls.h"

// The shared decls declare mem_alloc/mem_alloc_aligned as void-returning
// (their callers usually ignore the result); here we need the returned pointer,
// so call through typed function-pointer aliases matching the real ABI.
typedef int (*mem_alloc_fn)(int size, int type);
typedef int (*mem_alloc_aligned_fn)(int alignment, int size, int type);
#define MEM_ALLOC          ((mem_alloc_fn)&mem_alloc)
#define MEM_ALLOC_ALIGNED  ((mem_alloc_aligned_fn)&mem_alloc_aligned)

extern "C" {
// ---- gpr save/restore + frame helpers --------------------------------------
extern unsigned int __save_gpr_28(void);
extern void         __restore_gpr_28(void);

// ---- raw cross-module helpers (self-externed; not yet named) ---------------
extern unsigned long long FUN_803826dc(void);                 // acquire spawn frame -> packed (entity|handle<<32)
extern void               FUN_80382728(void);                 // release spawn frame
extern unsigned long long FUN_8014f024(unsigned int handle,   // bind slot / set alpha class
                                        int one, int alpha);
extern void FUN_8014989c(unsigned long long bound, ...);      // submit descriptor to render list
extern void FUN_8014f8cc(float* outMtx, int entity,           // build lod-selected bone matrix
                         unsigned int lodByte, int flag);
extern void FUN_80149a74(double v, unsigned int handle);      // scale-mismatch fixup
extern void FUN_801492f0(int entity);                         // finalize entity render state
extern int  FUN_8014a0cc(void);                               // render-type callback (address only)

// Build helpers implemented in this file (forward decls for internal use).
}

// ---- pool tables (shared with subobj_pool.cpp; per-file externs) -----------
extern int DAT_8061218c;   // descriptor pool base (stride 0x200)
extern int DAT_80612188;   // per-slot table (word * 0x200-sized rows here / 4)
extern int DAT_80612184;   // per-slot owner-entity pointer table (stride 4)
extern int DAT_80612180;   // per-slot 0x20-byte scratch table
extern int DAT_8061217c;   // per-slot timestamp/priority table
extern int DAT_80612178;   // free-list short ring buffer
extern int DAT_806121b4;   // joint/link records (stride 0x80, count 0x5d)
extern int DAT_806121ac;   // aux table (0xba records)
extern int DAT_806121a4;   // aux table (28000 bytes)
extern int DAT_80612160;   // pool capacity base (count = base + 0x60)
extern int DAT_8061216c;   // active list head
extern int DAT_80612168;   // active list count
extern int DAT_806121b8;   // joint alloc cursor

// current game mode / difficulty selector for pool sizing
extern int DAT_80612614;

// mode gate flags read when picking capacity
extern char DAT_805015bd;
extern unsigned char DAT_805015c0;

// 15-sample smoothing window seeded by subobj_pool_clear_state
extern float DAT_805893a0;

// default local-offset vector for the descriptor builder wrapper
extern float DAT_803ccae0, DAT_803ccae4, DAT_803ccae8;
// default corner seed for lod-swap rebuild
extern float DAT_803ccaec, DAT_803ccaf0, DAT_803ccaf4;

// shared float constants (matrix / math tuning)
extern float FLOAT_80615ea8;   // 0.0f seed
extern float FLOAT_80615eb0;   // 1.0f
extern float FLOAT_80615eb4;   // +BIG (min-seed)
extern float FLOAT_80615eb8;   // -BIG (max-seed)
extern float FLOAT_80615ebc;   // bounds pad factor
extern float FLOAT_80615ec0;   // quarter-turn angle offset
extern float FLOAT_80615ec4;   // normalize epsilon
extern float FLOAT_80615ec8;   // 1.5 (Newton-Raphson const for rsqrt refine)

// ============================================================================
// 0x80144e2c  subobj_pool_alloc_buffers
// One-time allocation of every parallel pool table. Pool capacity is chosen
// from the current game mode (DAT_80612614), optionally overridden by the
// coop / large-arena gate (DAT_805015bd / DAT_805015c0), then all descriptor,
// per-slot and joint tables are sized to (capacity + 0x60) records.
// ============================================================================
void subobj_pool_alloc_buffers(void)
{
    int cap;

    if (DAT_80612614 < 0x65) {
        if ((DAT_805015bd == '\n') || ((DAT_805015c0 & 0x40) != 0)) {
            switch (DAT_80612614) {
            case 1:    cap = 0x5a; break;   // 90
            case 2:    cap = 0x28; break;   // 40
            case 4:    cap = 0x78; break;   // 120
            case 5:    cap = 0xb4; break;   // 180
            case 6:    cap = 100;  break;
            case 9:    cap = 0x78; break;
            case 0xb:  cap = 0x78; break;
            case 0xe:  cap = 0x46; break;   // 70
            case 0x11: cap = 0x78; break;
            case 0x12: cap = 200;  break;
            case 0x16: cap = 0x78; break;
            case 0x1a: cap = 0x32; break;   // 50
            case 0x44: cap = 0x78; break;
            case 0x51: cap = 0x28; break;
            default:   cap = 100;  break;
            }
        } else {
            cap = 0x32;   // 50
        }
    } else {
        cap = 0x28;       // 40
    }
    DAT_80612160 = cap;
    cap = cap + 0x60;

    // descriptor pool: (cap) * 0x200 bytes, 0x40-aligned
    DAT_8061218c = MEM_ALLOC_ALIGNED(0x40, cap * 0x200, 1);
    // per-slot handle/flags, entity ptr, timestamp: cap * 4
    DAT_80612188 = MEM_ALLOC_ALIGNED(0x40, cap * 4, 1);
    DAT_80612184 = MEM_ALLOC_ALIGNED(0x40, cap * 4, 1);
    // per-slot 0x20-byte scratch
    DAT_80612180 = MEM_ALLOC_ALIGNED(0x40, cap * 0x20, 1);
    DAT_8061217c = MEM_ALLOC_ALIGNED(0x40, cap * 4, 1);
    // free-list ring buffer: cap * 2 (shorts)
    DAT_80612178 = MEM_ALLOC_ALIGNED(0x40, cap * 2, 1);

    // fixed-size auxiliary tables (mode-independent)
    DAT_806121b4 = MEM_ALLOC(0x2e80, 1);   // 0x5d joint records * 0x80
    DAT_806121ac = MEM_ALLOC(0xba, 1);
    DAT_806121a4 = MEM_ALLOC(28000, 1);

    subobj_pool_clear_state();
}

// ============================================================================
// 0x80145038  subobj_pool_clear_state
// Reset the active list + count, seed the 15-sample smoothing window and clear
// the joint alloc cursor. Calls subobj_pool_reset() (subobj_pool.cpp) to reset
// the free stacks.
// ============================================================================
void subobj_pool_clear_state(void)
{
    float seed;
    int   i;

    subobj_pool_reset();
    seed = FLOAT_80615eb0;
    DAT_8061216c = 0;
    DAT_80612168 = 0;

    // fill the 15-entry window (&DAT_805893a0[0x0e..0x00]) with the seed value.
    for (i = 0x0f; i != 0; --i) {
        (&DAT_805893a0)[i - 1] = seed;
    }
    DAT_806121b8 = 0;
}

// ============================================================================
// 0x801450e4  subobj_build_descriptor
// Core builder: acquire a sub-object slot for a spawning entity, initialise its
// flags/render registration, compute local mesh bounds, object + inverse object
// matrices, world center and bounding sphere, then chain the derived-bounds
// passes (vertex bounds / world bounds / screen quad). 'localOffset' returns the
// mesh center when flag 0x100000 is clear. 'flags' packs render options.
// ============================================================================
void subobj_build_descriptor(unsigned int flags, float* localOffset,
                             int lodByte, float pad)
{
    unsigned long long frame;
    unsigned int handle;
    int   entity;
    int   descBase;
    float* desc;
    float* parentMtx;
    unsigned int f;
    int   i, count, off;
    int*  strip;
    double radius;
    float  seed;
    float  extent[3];   // local_c0/bc/b8
    float  center[3];   // local_cc/c8/c4 (max corner)
    float  worldTmp[3]; // local_d8/d4/d0
    float  localMtx[16];// afStack_b4
    float  boneMtx[16]; // afStack_74
    float  invScaleRow; // local_34
    float  scaleY;      // local_30
    float  offX, offY, offZ; // local_2c/28/24

    frame  = FUN_803826dc();
    descBase = DAT_8061218c;
    handle = (unsigned int)(frame >> 0x20);
    entity = (int)frame;

    *(unsigned int*)(entity + 0x28) &= 0xffff3fff;
    desc = (float*)(descBase + (handle & 0xffff) * 0x200);
    parentMtx = (float*)0;
    *(unsigned int*)(entity + 0x28) |= 0x8000;
    *(unsigned int*)(entity + 0x80) = handle;

    f = *(unsigned int*)(entity + 0x28);
    if (((f & 0x200000) != 0) && ((f & 8) != 0)) {
        parentMtx = *(float**)(*(int*)(entity + 4) + 0x10);
        *(unsigned int*)(entity + 0x28) = f & 0xfffffff7;
    }

    if ((flags & 0x10000) == 0) {
        *(unsigned int*)(entity + 0x28) &= 0xbffdffff;
        entity_register_for_update(entity, 2, -0x7feb6d10);
        render_instance_set_material(FLOAT_80615ea8, *(int**)(entity + 4), 1);
        if (((*(unsigned int*)(entity + 0x28) & 0x40) != 0) && ((flags & 0x200000) != 0)) {
            entity_register_for_update(entity, 7, -0x7feb5b3c);
        }
        seed = FLOAT_80615ea8;
        if ((*(float*)(entity + 0x1cc) < FLOAT_80615ea8) ||
            (*(float*)(entity + 0x1d8) < FLOAT_80615ea8)) {
            *(float*)(entity + 0x1d8) = FLOAT_80615ea8;
            *(float*)(entity + 0x1cc) = seed;
        }
        if (*(int*)(entity + 0x19c) == 0) {
            if ((*(unsigned int*)(entity + 0x28) & 0x10) != 0) {
                *(unsigned int*)(entity + 0x28) |= 0x10;
                entity_set_render_type(entity, (int)&FUN_8014a0cc);
            }
        }
    }

    seed = FLOAT_80615eb0;
    desc[0x75] = -1.0f;   // -NAN sentinel in the original (never-played marker)
    desc[0x76] = seed;
    desc[0x77] = 0.0f;
    desc[0x70] = *(float*)&flags;
    desc[0x74] = 0.0f;
    desc[0x6f] = 0.0f;
    desc[0x7a] = 0.0f;
    desc[0x7b] = 0.0f;
    desc[0x71] = 0.0f;

    if ((flags & 0x200) != 0) {
        unsigned int matBits = 0;
        strip = *(int**)(*(int*)(entity + 4) + 4);
        count = *strip;
        if (0 < count) {
            off = 0;
            do {
                int row = off + 0x50;
                off += 0xc0;
                matBits |= *(unsigned int*)((int)strip + row + *strip * -0xc0);
                --count;
            } while (count != 0);
        }
        // AND-NOT with the entity render mask, keep raw bits in the float slot.
        {
            unsigned int keep = matBits & ~*(unsigned int*)(*(int*)(entity + 4) + 0x30);
            desc[0x71] = *(float*)&keep;
        }
    }

    handle = (unsigned int)FUN_8014f024(handle, 1, 0xff);
    if (-1 < lodByte) {
        FUN_8014989c((unsigned long long)handle);
    }

    seed = FLOAT_80615ea8;
    for (i = 0x44; i <= 0x4f; ++i) {
        desc[i] = seed;
    }

    entity_compute_mesh_bounds(entity, extent, center);
    FUN_8014f8cc(boneMtx, entity, (unsigned int)lodByte, 1);
    mtx4_copy(boneMtx, desc + 0x30);
    mtx4_invert(desc + 0x20, boneMtx);

    // local_34 / local_30 in the original are the X/Y scale diagonal of boneMtx.
    invScaleRow = boneMtx[0];
    scaleY = boneMtx[5];
    desc[0x6a] = FLOAT_80615eb0 / invScaleRow;
    if ((double)invScaleRow != (double)scaleY) {
        FUN_80149a74((double)scaleY, handle);
    }
    *(char*)(desc + 0x73) = (char)lodByte;

    if (((unsigned int)desc[0x70] & 0x100000) == 0) {
        offX = boneMtx[12]; offY = boneMtx[13]; offZ = boneMtx[14];
        localOffset[0] = offX;
        localOffset[1] = offY;
        localOffset[2] = offZ;
    }

    entity_build_local_matrix(localMtx, (void*)(intptr_t)entity, 0, 0);
    subobj_build_orthonormal_matrix(handle, localMtx, parentMtx);

    desc[0x5c] = center[0] + localOffset[0];
    desc[0x5d] = center[1] + localOffset[1];
    desc[0x5e] = center[2] + localOffset[2];
    desc[0x59] = localOffset[0];
    desc[0x5a] = localOffset[1];
    desc[0x5b] = localOffset[2];

    if (((unsigned int)pad & 0x8000) == 0) {   // pad reused as flag bits here
        worldTmp[0] = desc[0x5c];
        worldTmp[1] = desc[0x5d];
        worldTmp[2] = desc[0x5e];
        mtx4_transform_vector_alias(desc, worldTmp);
        desc[0x50] = *(float*)(entity + 100) + worldTmp[0];
        desc[0x51] = *(float*)(entity + 0x68) + worldTmp[1];
        desc[0x52] = *(float*)(entity + 0x6c) + worldTmp[2];
        if (parentMtx != (float*)0) {
            worldTmp[0] = parentMtx[12];
            worldTmp[1] = parentMtx[13];
            worldTmp[2] = parentMtx[14];
            mtx4_transform_vector_alias(localMtx, worldTmp);
            desc[0x50] += worldTmp[0];
            desc[0x51] += worldTmp[1];
            desc[0x52] += worldTmp[2];
        }
    } else {
        desc[0x50] = *(float*)(entity + 100);
        desc[0x51] = *(float*)(entity + 0x68);
        desc[0x52] = *(float*)(entity + 0x6c);
    }

    radius = math_sqrt((double)(localOffset[2] * localOffset[2] +
                                localOffset[0] * localOffset[0] +
                                localOffset[1] * localOffset[1]));
    seed = FLOAT_80615ea8;
    desc[0x5f] = (float)((double)extent[0] + radius);
    desc[0x60] = (float)((double)extent[1] + radius);
    desc[0x61] = (float)((double)extent[2] + radius);
    desc[0x56] = desc[0x50];
    desc[0x57] = desc[0x51];
    desc[0x58] = desc[0x52];
    desc[0x53] = seed;
    desc[0x54] = seed;
    desc[0x55] = seed;
    *(float*)(entity + 0xc0) = seed;
    *(float*)(entity + 0xc4) = seed;
    *(float*)(entity + 0x70) = seed;
    *(float*)(entity + 200) = seed;
    mtx4_set_identity(entity + 0x274);
    FUN_801492f0(entity);

    subobj_compute_vertex_bounds(handle);
    subobj_apply_world_bounds(handle);
    subobj_compute_screen_quad(handle);
    FUN_80382728();
}

// ============================================================================
// 0x80145090  subobj_build_descriptor_default
// Convenience wrapper that supplies the module default local-offset vector.
// ============================================================================
void subobj_build_descriptor_default(unsigned int flags, int lodByte, float pad)
{
    float localOffset[3];

    localOffset[0] = DAT_803ccae0;
    localOffset[1] = DAT_803ccae4;
    localOffset[2] = DAT_803ccae8;
    subobj_build_descriptor(flags, localOffset, lodByte, pad);
}

// ============================================================================
// 0x80145584  subobj_rebuild_after_lod_swap
// Recompute object / inverse-object matrices and local bounds for the current
// spawn-frame sub-object after a LOD/model swap, preserving the entity's alive
// flag (bit 8) across the mesh-bounds recompute.
// ============================================================================
void subobj_rebuild_after_lod_swap(void)
{
    unsigned int handle;
    int   entity;
    int   descBase;
    unsigned int aliveBit;
    float corner[3];    // local_88/84/80
    float maxc[3];      // local_7c/78/74
    float bounds[3];    // auStack_70 (extent out)
    float boneMtx[16];  // afStack_64
    float invScaleRow;  // local_24
    float scaleY;       // local_20
    float offX, offY, offZ; // local_1c/18/14

    handle = __save_gpr_28();
    entity = *(int*)(DAT_80612184 + (handle & 0xffff) * 4);
    aliveBit = *(unsigned int*)(entity + 0x28) & 8;
    corner[0] = DAT_803ccaec;
    descBase = DAT_8061218c + (handle & 0xffff) * 0x200;
    corner[1] = DAT_803ccaf0;
    corner[2] = DAT_803ccaf4;
    *(unsigned int*)(entity + 0x28) &= ~aliveBit;

    entity_compute_mesh_bounds(entity, bounds, maxc);
    FUN_8014f8cc(boneMtx, entity, (unsigned int)*(unsigned char*)(descBase + 0x1cc), 1);
    mtx4_copy(boneMtx, (float*)(descBase + 0xc0));
    mtx4_invert((float*)(descBase + 0x80), boneMtx);

    invScaleRow = boneMtx[0];
    scaleY = boneMtx[5];
    *(float*)(descBase + 0x1a8) = FLOAT_80615eb0 / invScaleRow;
    if ((double)invScaleRow != (double)scaleY) {
        FUN_80149a74((double)scaleY, handle);
    }
    if ((*(unsigned int*)(descBase + 0x1c0) & 0x100000) == 0) {
        corner[0] = boneMtx[12];
        corner[1] = boneMtx[13];
        corner[2] = boneMtx[14];
    }
    offX = corner[0]; offY = corner[1]; offZ = corner[2];
    *(float*)(descBase + 0x170) = maxc[0] + offX;
    *(float*)(descBase + 0x174) = maxc[1] + offY;
    *(float*)(descBase + 0x178) = maxc[2] + offZ;
    *(float*)(descBase + 0x164) = offX;
    *(float*)(descBase + 0x168) = offY;
    *(float*)(descBase + 0x16c) = offZ;
    *(unsigned int*)(entity + 0x28) |= aliveBit;
    __restore_gpr_28();
}

// ============================================================================
// 0x801456cc  subobj_compute_vertex_bounds
// Scan every visible mesh strip of the owner entity and accumulate a symmetric
// min/max box (per-axis), then scale it by the pad factor and add the local
// center magnitude to produce the descriptor's local extent (+0x17c..+0x184).
// Strips are 0xc0 bytes; a strip contributes when its key word (+0x84) differs
// from the seed and its mask (+0x50) passes the entity render mask (+0x30).
// ============================================================================
void subobj_compute_vertex_bounds(unsigned int handle)
{
    int    entity4;   // *(entity+4) model block
    int*   strip;
    int    descBase;
    int    count, off;
    unsigned int mask;
    double minSeed, maxSeed;
    double lo0, lo1, lo2, hi0, hi1, hi2;
    double centerMag;
    float  pad;

    minSeed = (double)FLOAT_80615eb4;   // +BIG
    maxSeed = (double)FLOAT_80615eb8;   // -BIG
    entity4 = *(int*)(*(int*)(DAT_80612184 + (handle & 0xffff) * 4) + 4);
    strip = *(int**)(entity4 + 4);
    descBase = DAT_8061218c + (handle & 0xffff) * 0x200;
    count = *strip;

    lo0 = minSeed; lo1 = minSeed; lo2 = minSeed;
    hi0 = maxSeed; hi1 = maxSeed; hi2 = maxSeed;

    if (0 < count) {
        int base = count * -0xc0;
        off = 0;
        do {
            int rec = off + base;
            double key = (double)*(float*)((int)strip + rec + 0x84);
            if ((minSeed != key) &&
                ((mask = *(unsigned int*)((int)strip + rec + 0x50)) == 0 ||
                 ((*(unsigned int*)(entity4 + 0x30) & mask) != 0))) {
                double half = (double)*(float*)((int)strip + rec + 0x90);
                float  cy   = *(float*)((int)strip + rec + 0x88);
                float  ey   = *(float*)((int)strip + rec + 0x94);
                float  cz   = *(float*)((int)strip + rec + 0x8c);
                float  ez   = *(float*)((int)strip + rec + 0x98);
                double nx = (double)(float)(key - half);
                double px = (double)(float)(key + half);
                double ny = (double)(cy - ey);
                double py = (double)(cy + ey);
                double nz = (double)(cz - ez);
                double pz = (double)(cz + ez);
                if (nx < lo0) lo0 = nx;
                if (ny < lo1) lo1 = ny;
                if (nz < lo2) lo2 = nz;
                if (px > hi0) hi0 = px;
                if (py > hi1) hi1 = py;
                if (pz > hi2) hi2 = pz;
            }
            off += 0xc0;
            --count;
        } while (count != 0);
    }

    centerMag = math_sqrt((double)(*(float*)(descBase + 0x16c) * *(float*)(descBase + 0x16c) +
                                   *(float*)(descBase + 0x164) * *(float*)(descBase + 0x164) +
                                   *(float*)(descBase + 0x168) * *(float*)(descBase + 0x168)));
    pad = FLOAT_80615ebc;
    *(float*)(descBase + 0x17c) = (float)(((float)(hi0 - lo0)) * pad + centerMag);
    *(float*)(descBase + 0x180) = (float)(((float)(hi1 - lo1)) * pad + centerMag);
    *(float*)(descBase + 0x184) = (float)(((float)(hi2 - lo2)) * pad + centerMag);
}

// ============================================================================
// 0x801458ec  subobj_apply_world_bounds
// Transform the descriptor's local bounding box by its object matrix into the
// owner entity's world AABB fields (+0x1a8 min, +0x1c0 max) and, unless the
// billboard flag (0x8000) is set, offset the entity origin by the AABB min.
// ============================================================================
void subobj_apply_world_bounds(unsigned int handle)
{
    int    entity;
    float* mtx;
    float  maxc[3];   // local_1c/18/14
    float  minc[3];   // local_28/24/20

    mtx = (float*)(DAT_8061218c + (handle & 0xffff) * 0x200);
    entity = *(int*)(DAT_80612184 + (handle & 0xffff) * 4);

    maxc[0] = mtx[0x5f]; maxc[1] = mtx[0x60]; maxc[2] = mtx[0x61];
    minc[0] = mtx[0x5c]; minc[1] = mtx[0x5d]; minc[2] = mtx[0x5e];

    entity_transform_bounds_by_matrix(mtx, maxc, minc);

    *(float*)(entity + 0x1c0) = maxc[0];
    *(float*)(entity + 0x1c4) = maxc[1];
    *(float*)(entity + 0x1c8) = maxc[2];
    *(float*)(entity + 0x1a8) = minc[0];
    *(float*)(entity + 0x1ac) = minc[1];
    *(float*)(entity + 0x1b0) = minc[2];

    if (((unsigned int)mtx[0x70] & 0x8000) == 0) {
        *(float*)(entity + 100)   = mtx[0x50] - *(float*)(entity + 0x1a8);
        *(float*)(entity + 0x68)  = mtx[0x51] - *(float*)(entity + 0x1ac);
        *(float*)(entity + 0x6c)  = mtx[0x52] - *(float*)(entity + 0x1b0);
    } else {
        *(float*)(entity + 100)  = mtx[0x50];
        *(float*)(entity + 0x68) = mtx[0x51];
        *(float*)(entity + 0x6c) = mtx[0x52];
    }
}

// ============================================================================
// 0x801459f8  subobj_compute_screen_quad
// Build the 4-corner ground-plane quad for the sub-object (used for its decal /
// shadow). Extracts the Y-yaw from the object matrix, rebuilds a yaw-only
// rotation, transforms the local extent, then places 4 corners around the
// entity's world XZ position rotated by (yaw) using math_poly_trig sin/cos.
// Corners written at descriptor +0x188..+0x1a4.
// ============================================================================
void subobj_compute_screen_quad(unsigned int handle)
{
    int    descBase;
    int    entity;
    float  euler[3];    // local_a0/a4/a8 (x, yaw, z)
    float  rotMtx[16 + 3]; // afStack_84
    float  extent[3];   // local_90/8c/88
    float  center[3];   // local_9c/98/94
    double cx, cz;
    double s, c;
    float  fx, fz, fx2, fz2;

    descBase = DAT_8061218c + (handle & 0xffff) * 0x200;
    entity   = *(int*)(DAT_80612184 + (handle & 0xffff) * 4);

    mtx4_to_euler((float*)descBase, euler);          // fills euler[0..2]
    mtx4_set_rotation_zyx((double)euler[0], (double)FLOAT_80615ea8,
                          (double)euler[2], rotMtx);

    extent[0] = *(float*)(descBase + 0x17c);
    extent[1] = *(float*)(descBase + 0x180);
    extent[2] = *(float*)(descBase + 0x184);
    center[0] = *(float*)(descBase + 0x170);
    center[1] = *(float*)(descBase + 0x174);
    center[2] = *(float*)(descBase + 0x178);

    entity_transform_bounds_by_matrix(rotMtx, extent, center);

    cx = (double)(*(float*)(entity + 100)  + *(float*)(entity + 0x1a8));
    cz = (double)(*(float*)(entity + 0x6c) + *(float*)(entity + 0x1b0));
    s  = math_poly_trig((double)(euler[1] - FLOAT_80615ec0));   // sin(yaw)
    c  = math_poly_trig((double)euler[1]);                      // cos(yaw)

    fx  = (float)(c * (double)extent[0]);
    fz2 = (float)(s * (double)extent[2]);
    fx2 = (float)(c * (double)extent[2]);
    fz  = (float)(s * (double)extent[0]);

    *(float*)(descBase + 0x188) = (float)(cx + (double)(-fx + fz2));
    *(float*)(descBase + 0x18c) = (float)(cz + (double)(fx2 + fz));
    *(float*)(descBase + 0x190) = (float)(cx + (double)(fx + fz2));
    *(float*)(descBase + 0x194) = (float)(cz + (double)(fx2 - fz));
    *(float*)(descBase + 0x198) = (float)(cx + (double)(fx - fz2));
    *(float*)(descBase + 0x19c) = (float)(cz + (double)(-fx2 - fz));
    *(float*)(descBase + 0x1a0) = (float)(cx + (double)(-fx - fz2));
    *(float*)(descBase + 0x1a4) = (float)(cz + (double)(-fx2 + fz));
}

// ============================================================================
// 0x80145b88  subobj_set_extra_words
// Store two caller-supplied words into the descriptor scratch (+0x1e8/+0x1ec).
// ============================================================================
void subobj_set_extra_words(unsigned int handle, unsigned int a, unsigned int b)
{
    int descBase = DAT_8061218c + (handle & 0xffff) * 0x200;
    *(unsigned int*)(descBase + 0x1e8) = a;
    *(unsigned int*)(descBase + 0x1ec) = b;
}

// ============================================================================
// 0x80145ba0  subobj_build_orthonormal_matrix
// Compose the descriptor object matrix from a parent matrix (localMtx) and an
// optional child transform (childMtx). When both are present the child's three
// basis rows are individually renormalised using a Newton-Raphson rsqrt refine
// (FLOAT_80615ec8 == 1.5), the child is multiplied into the parent, and the
// result is stored plus its transpose products (+0x40 / +0x80 rows).
// ============================================================================
void subobj_build_orthonormal_matrix(unsigned int handle, float* localMtx, float* childMtx)
{
    float* desc;
    float  transp[16]; // afStack_b8
    float  tmp[16];    // local_78 .. (child working copy / product)
    float  fLen, fInv;

    desc = (float*)(DAT_8061218c + (handle & 0xffff) * 0x200);

    if (localMtx == (float*)0) {
        mtx4_set_identity((int)desc);
    } else if (childMtx == (float*)0) {
        mtx4_copy(localMtx, desc);
    } else {
        mtx4_copy(childMtx, tmp);

        // renormalise basis row 0 (tmp[0..2])
        fLen = tmp[2] * tmp[2] + tmp[0] * tmp[0] + tmp[1] * tmp[1];
        if (FLOAT_80615ec4 < fLen) {
            fInv = 1.0f / SQRT(fLen);
            fInv = fInv * -(FLOAT_80615ebc * fLen * fInv * fInv - FLOAT_80615ec8);
            fLen = FLOAT_80615eb0 * fInv * -(FLOAT_80615ebc * fLen * fInv * fInv - FLOAT_80615ec8);
            tmp[0] *= fLen; tmp[1] *= fLen; tmp[2] *= fLen;
        }
        // renormalise basis row 1 (tmp[4..6])
        fLen = tmp[6] * tmp[6] + tmp[4] * tmp[4] + tmp[5] * tmp[5];
        if (FLOAT_80615ec4 < fLen) {
            fInv = 1.0f / SQRT(fLen);
            fInv = fInv * -(FLOAT_80615ebc * fLen * fInv * fInv - FLOAT_80615ec8);
            fLen = FLOAT_80615eb0 * fInv * -(FLOAT_80615ebc * fLen * fInv * fInv - FLOAT_80615ec8);
            tmp[4] *= fLen; tmp[5] *= fLen; tmp[6] *= fLen;
        }
        // renormalise basis row 2 (tmp[8..10])
        fLen = tmp[10] * tmp[10] + tmp[8] * tmp[8] + tmp[9] * tmp[9];
        if (FLOAT_80615ec4 < fLen) {
            fInv = 1.0f / SQRT(fLen);
            fInv = fInv * -(FLOAT_80615ebc * fLen * fInv * fInv - FLOAT_80615ec8);
            fLen = FLOAT_80615eb0 * fInv * -(FLOAT_80615ebc * fLen * fInv * fInv - FLOAT_80615ec8);
            tmp[8] *= fLen; tmp[9] *= fLen; tmp[10] *= fLen;
        }
        tmp[12] = FLOAT_80615ea8;
        tmp[13] = FLOAT_80615ea8;
        tmp[14] = FLOAT_80615ea8;
        mtx4_multiply(tmp, localMtx, desc);
    }

    mtx4_to_quat();
    mtx4_transpose(desc, transp);
    mtx4_multiply(desc + 0x20, desc, tmp);
    mtx4_multiply(transp, tmp, desc + 0x10);
}
