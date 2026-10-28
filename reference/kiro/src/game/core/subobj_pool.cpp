// Address range: 0x801447f0 - 0x80144d34 | batch_409 | 2026-09-05
//
// Entity render sub-object pool. Many entities own an auxiliary "sub-object"
// (a render/transform node with its own 0x200-byte descriptor) referenced by a
// packed handle stored at entity+0x80. This module allocates, frees and
// tracks those sub-objects. It is one of the most widely-called allocators in
// the engine (spawn/effect/gib/weapon code all acquire sub-objects), and it is
// paired with a set of 0x5d joint/link records (DAT_806121b4, stride 0x80)
// that bind sub-objects to owners.
//
// Handle layout: low 16 bits = slot index; the descriptor for a handle is
//   DAT_8061218c + (handle & 0xffff) * 0x200.
//
// Parallel per-slot tables (all indexed by slotIndex):
//   DAT_80612184  entity pointer   (stride 4)
//   DAT_80612188  handle/flags word (stride 4; bit 0x10000 = slot free)
//   DAT_8061217c  timestamp / eviction priority (stride 4)
//   DAT_80612180  0x20-byte per-slot scratch
//
// Free-list is three LIFO stacks packed into one short ring buffer
// (DAT_80612178) addressed by three cursors: DAT_80612170 (class 0),
// sRam80612172 (class 1, grows downward), sRam80612174 (class 2). Live
// sub-objects are also threaded on a doubly-linked list rooted at DAT_8061216c
// via descriptor fields +0x1f0 (prev) and +0x1f4 (next).
//
// Descriptor fields used here: +0x1c0 flags, +0x1c8 byte, +0x1cd class byte,
// +0x1d0 free bit, +0x1e4 own handle, +0x1f0 prev, +0x1f4 next, +0x1f8/+0x1fc
// scratch.

#include "common.h"
#include "generated_decls.h"

extern "C" {
// gpr save/restore frame helpers (Ghidra models a packed-arg return).
extern unsigned long long __save_gpr_28(void);
extern void               __restore_gpr_28(void);
extern unsigned int       __subobj_frame_arg(void); // FUN_803826e8 (handle arg)
extern void               __subobj_frame_end(void); // FUN_80382734

extern void FUN_80145df4(unsigned int handle, int entity); // detach transform bind
extern void FUN_80149360(unsigned int handle);             // release descriptor resources
extern void FUN_8014e8ec(int jointIndex);                  // clear joint/link record
}

// ---- pool tables (per-file externs) ----------------------------------------
extern int   DAT_8061218c;   // descriptor pool base (stride 0x200)
extern int   DAT_80612184;   // per-slot entity pointer table
extern int   DAT_80612188;   // per-slot handle/flags table (bit 0x10000 = free)
extern int   DAT_8061217c;   // per-slot timestamp/priority table
extern int   DAT_80612180;   // per-slot 0x20-byte scratch base
extern int   DAT_80612178;   // free-list short ring buffer base
extern int   DAT_80612160;   // pool capacity base
extern int   DAT_8061216c;   // active (in-use) list head descriptor
extern short DAT_80612170;   // free stack cursor class 0
extern int   DAT_806121b4;   // joint/link records (stride 0x80, count 0x5d)
extern int   DAT_80611928;   // current game-frame counter

// class-1/class-2 free cursors + a scratch short adjacent to DAT_80612170.
extern short sRam80612172;   // free stack cursor class 1 (downward)
extern unsigned short uRam80612172;
extern short sRam80612174;   // free stack cursor class 2

// reset-time scalars
extern int DAT_80612198; extern int DAT_8061219c; extern int DAT_80612190; extern int DAT_80612194;
extern int DAT_806121b8; extern int DAT_806121b0; extern int DAT_806121a0; extern int DAT_806121a4;
extern int DAT_806109a0;

// 15-sample running-average window + divisor (used by camera + sub-object LOD).
extern float DAT_805893a0;
extern float FLOAT_80615ea8;   // running-average seed
extern float FLOAT_80615eac;   // running-average divisor (15.0)

// ---------------------------------------------------------------------------
// subobj_window_average (0x801447f0)
// Sum a 15-entry float window (&DAT_805893a0, indices 0..14 read high-to-low)
// plus a seed, divided by FLOAT_80615eac. Used as a smoothed metric by the
// third-person camera and the sub-object LOD path.
double subobj_window_average(void)
{
    int off = 0x38;
    float sum = FLOAT_80615ea8;
    for (int i = 0xf; i != 0; --i)
    {
        sum += *(float*)((int)&DAT_805893a0 + off);
        off -= 4;
    }
    return (double)(sum / FLOAT_80615eac);
}

// ---------------------------------------------------------------------------
// subobj_get_descriptor (0x80144824)
int subobj_get_descriptor(unsigned int handle)
{
    return DAT_8061218c + (handle & 0xffff) * 0x200;
}

// ---------------------------------------------------------------------------
// subobj_get_entity (0x80144834)
int subobj_get_entity(unsigned int handle)
{
    return *(int*)(DAT_80612184 + (handle & 0xffff) * 4);
}

// ---------------------------------------------------------------------------
// subobj_is_handle_free (0x80144844)
// Returns 1 if the slot's free bit (+0x1d0) is zero, else 0. The
// (-x & ~x) >> 31 idiom yields 1 exactly when x == 0.
unsigned int subobj_is_handle_free(unsigned int handle)
{
    unsigned int v = *(unsigned int*)(DAT_8061218c + (handle & 0xffff) * 0x200 + 0x1d0);
    return ((0u - v) & ~v) >> 0x1f;
}

// ---------------------------------------------------------------------------
// subobj_find_lru_evictable (0x80144a50)
// Walk the active list (root DAT_8061216c, next at +0x1f4) for the entry with
// class byte (+0x1cd) == 2 and lowest timestamp; return its own handle
// (+0x1e4) or 0 if none.
unsigned int subobj_find_lru_evictable(void)
{
    unsigned int best = 0xffffffff;
    unsigned int chosen = 0;
    for (int node = DAT_8061216c; node != 0; node = *(int*)(node + 0x1f4))
    {
        if (*(char*)(node + 0x1cd) == '\x02')
        {
            unsigned int ts = *(unsigned int*)(DAT_8061217c + (*(unsigned int*)(node + 0x1e4) & 0xffff) * 4);
            if (ts < best)
            {
                chosen = *(unsigned int*)(node + 0x1e4);
                best = ts;
            }
        }
    }
    return chosen;
}

// ---------------------------------------------------------------------------
// subobj_pool_alloc (0x80144864)
// Allocate a sub-object slot for the requesting entity. The packed frame carries
// the class byte (low 8 bits) and the owner entity pointer (high 32 bits). Pops
// a free index from the class stack (evicting the LRU entry via
// subobj_find_lru_evictable when the class is exhausted), initialises the
// descriptor, links it at the head of the active list, and stores it as the new
// active head DAT_8061216c.
void subobj_pool_alloc(void)
{
    unsigned long long frame = __save_gpr_28();
    unsigned int packed = (unsigned int)frame;
    int cls = (int)(packed & 0xff);
    int newHead = DAT_8061216c;
    unsigned int idx = 0;
    bool have = true;

    short* cursors = &DAT_80612170;

    if (cls == 1)
    {
        if ((int)DAT_80612160 + 0x58 <= (int)cursors[cls])
        {
            have = false;
        }
        else
        {
            if ((int)sRam80612174 == (int)cursors[cls])
            {
                unsigned int victim = subobj_find_lru_evictable();
                if (victim == 0) have = false;
                else entity_destroy_with_event(*(int**)(DAT_80612184 + (victim & 0xffff) * 4));
            }
            if (have)
            {
                idx = (unsigned int)*(unsigned short*)(DAT_80612178 + cursors[cls] * 2);
                cursors[cls] = cursors[cls] + 1;
            }
        }
    }
    else if ((packed & 0xff) == 0)
    {
        if (cursors[0] == 0) have = false;
        else
        {
            short v = cursors[0] - 1;
            cursors[0] = v;
            idx = (unsigned int)*(unsigned short*)(DAT_80612178 + v * 2);
        }
    }
    else
    {
        if (2 < (packed & 0xff)) have = false;
        else
        {
            if (sRam80612172 == cursors[cls])
            {
                unsigned int victim = subobj_find_lru_evictable();
                if (victim == 0) have = false;
                else entity_destroy_with_event(*(int**)(DAT_80612184 + (victim & 0xffff) * 4));
            }
            if (have)
            {
                cursors[cls] = cursors[cls] - 1;
                idx = (unsigned int)*(unsigned short*)(DAT_80612178 + cursors[cls] * 2);
            }
        }
    }

    if (have)
    {
        int slotOff = idx * 4;
        int desc = DAT_8061218c + (*(unsigned int*)(DAT_80612188 + slotOff) & 0xffff) * 0x200;
        *(unsigned int*)(desc + 0x1e4) = *(unsigned int*)(DAT_80612188 + slotOff);
        *(char*)(desc + 0x1cd) = (char)packed;
        *(int*)(DAT_80612184 + slotOff) = (int)(frame >> 0x20);   // owner entity
        *(int*)(DAT_8061217c + slotOff) = DAT_80611928;
        memset_wrapper(DAT_80612180 + idx * 0x20, 0, 0x20);
        *(unsigned char*)(desc + 0x1c8) = 0xff;
        *(int*)(desc + 0x1fc) = 0;
        *(int*)(desc + 0x1f8) = 0;
        *(int*)(desc + 0x1f4) = DAT_8061216c;
        *(int*)(desc + 0x1f0) = 0;
        if (DAT_8061216c != 0)
            *(int*)(DAT_8061216c + 0x1f0) = desc;
        newHead = desc;
    }

    DAT_8061216c = newHead;
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// subobj_pool_free (0x80144b80)
// Release the sub-object identified by the packed frame handle: clear any joint
// records that reference it, unlink it from the active list, mark its slot free
// (handle table bit 0x10000), and push its index back onto the owning class
// free stack.
void subobj_pool_free(void)
{
    unsigned int handle = __subobj_frame_arg();
    FUN_80149360(handle);

    int desc = DAT_8061218c + (handle & 0xffff) * 0x200;
    // clear any of the 0x5d joint records that bind this sub-object.
    int jointOff = 0;
    for (int j = 0; j < 0x5d; ++j)
    {
        int rec = DAT_806121b4 + jointOff;
        if (((*(unsigned int*)(rec + 0x70) == handle) || (*(unsigned int*)(rec + 0x74) == handle)) &&
            (*(int*)(rec + 0x58) != 0))
        {
            FUN_8014e8ec(j);
        }
        jointOff += 0x80;
    }

    // unlink from active doubly-linked list.
    if (*(int*)(desc + 0x1f0) == 0)
        DAT_8061216c = *(int*)(desc + 0x1f4);
    else
        *(int*)(*(int*)(desc + 0x1f0) + 0x1f4) = *(int*)(desc + 0x1f4);
    if (*(int*)(desc + 0x1f4) != 0)
        *(int*)(*(int*)(desc + 0x1f4) + 0x1f0) = *(int*)(desc + 0x1f0);

    int slotOff = (handle & 0xffff) * 4;
    *(int*)(desc + 0x1f4) = 0;
    *(int*)(desc + 0x1f0) = 0;
    *(int*)(DAT_80612184 + slotOff) = 0;
    *(unsigned int*)(DAT_80612188 + slotOff) |= 0x10000;

    // push index back onto the appropriate class free stack.
    unsigned int cls = (unsigned int)*(unsigned char*)(desc + 0x1cd);
    unsigned short slot = (unsigned short)handle;
    short* cursors = &DAT_80612170;
    if (cls == 1)
    {
        sRam80612172 = sRam80612172 - 1;
        *(unsigned short*)(DAT_80612178 + sRam80612172 * 2) = slot;
    }
    else if (cls == 0)
    {
        int at = (int)DAT_80612170;
        DAT_80612170 = DAT_80612170 + 1;
        *(unsigned short*)(DAT_80612178 + at * 2) = slot;
    }
    else if (cls < 3)
    {
        short at = cursors[cls];
        cursors[cls] = at + 1;
        *(unsigned short*)(DAT_80612178 + at * 2) = slot;
    }

    __subobj_frame_end();
}

// ---------------------------------------------------------------------------
// subobj_detach_from_entity (0x80144a9c)
// Release the sub-object owned by an entity (handle at entity+0x80): tear down
// its transform bind, free the pool slot, clear the entity's render flags and
// handle, and (unless the descriptor is a persistent kind, +0x1c0 bit 0x10000)
// unregister the entity from the update/render lists.
void subobj_detach_from_entity(int entity)
{
    unsigned int handle = *(unsigned int*)(entity + 0x80);
    if (handle != 0)
    {
        unsigned int descFlags =
            *(unsigned int*)(DAT_8061218c + (handle & 0xffff) * 0x200 + 0x1c0);
        if (((*(unsigned int*)(entity + 0x28) & 0x80000000) == 0) && (*(int*)(entity + 4) != 0))
        {
            FUN_80145df4(handle, entity);
            if ((*(int*)(*(int*)(entity + 4) + 0x10) != 0) &&
                ((*(unsigned int*)(entity + 0x28) & 0x200000) != 0))
            {
                mtx4_table_reset_identity();
            }
        }
        subobj_pool_free();
        *(unsigned int*)(entity + 0x28) &= 0xffff7fff;
        *(int*)(entity + 0x80) = 0;
        if ((descFlags & 0x10000) == 0)
        {
            entity_register_for_update(entity, 2, 0);
            entity_register_for_update(entity, 7, 0);
            entity_set_render_type(entity, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// subobj_pool_reset (0x80144d34)
// Initialise the whole sub-object pool: seed the free-list ring with every
// slot index, mark all slots free, reset the 0x5d joint records, and clear the
// scheduler scalars.
void subobj_pool_reset(void)
{
    sRam80612174 = (short)(DAT_80612160 + 0x60);
    DAT_80612170 = 0x58;
    uRam80612172 = 0x58;

    int ringOff = 0;
    int slotOff = 0;
    int cap = DAT_80612160 + 0x60;
    for (int i = 0; i < cap; ++i)
    {
        *(short*)(DAT_80612178 + ringOff) = (short)i;
        ringOff += 2;
        *(unsigned int*)(DAT_80612188 + slotOff) = (unsigned int)i | 0x10000;
        *(int*)(DAT_80612184 + slotOff) = 0;
        *(int*)(DAT_8061217c + slotOff) = 0;
        slotOff += 4;
    }

    int jointOff = 0;
    for (int j = 0x5d; j != 0; --j)
    {
        *(int*)(DAT_806121b4 + jointOff + 0x58) = 0;
        *(int*)(DAT_806121b4 + jointOff + 0x78) = 0;
        *(int*)(DAT_806121b4 + jointOff + 0x7c) = 0;
        *(int*)(DAT_806121b4 + jointOff + 0x70) = 0;
        *(int*)(DAT_806121b4 + jointOff + 0x74) = 0;
        jointOff += 0x80;
    }

    DAT_80612198 = 0;
    DAT_8061219c = 0;
    DAT_80612190 = 0;
    DAT_80612194 = 0;
    DAT_806121b8 = 0xffffffff;
    DAT_806121b0 = 0;
    DAT_806121a0 = DAT_806121a4;
    DAT_806109a0 = 0xffffffff;
}
