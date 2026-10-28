// Address range: 0x8014f9f8 - 0x80150314 (subset) | batch_423 | 2026-09-06
//
// Entity render sub-object PHYSICS-STEP support helpers (companion to
// subobj_sim.cpp / subobj_break.cpp): a per-frame deferred body-callback list
// used by subobj_physics_step, an edge-triggered effect-state latch, a link
// slot setter, and a reserved-flag predicate.
//
// Deferred callback list (globals):
//   DAT_8061219c  key array base   (0x10 bytes per entry)
//   DAT_80612198  entity-ptr array (4 bytes per entry)
//   DAT_80612190  capacity
//   DAT_80612194  live count
// subobj_physics_step allocs the arrays (subobj_defer_list_alloc), event
// resolution pushes entities (subobj_defer_list_push), and the step flushes
// them by invoking each entity's callback at +0x170 (subobj_defer_list_flush).

#include "common.h"
#include "generated_decls.h"

extern "C" {
extern int  leaf_31e8(int dst, unsigned int src, int n);   // aligned 0x10 copy
}

extern int DAT_8061219c;   // deferred key array base
extern int DAT_80612198;   // deferred entity-ptr array base
extern int DAT_80612190;   // deferred list capacity
extern int DAT_80612194;   // deferred list live count

// Edge-triggered effect-state latch table (halfword pairs).
extern unsigned short DAT_80612170;    // slot base (indexed by param for 1..2)
extern unsigned short uRam80612172;    // previous-frame value
extern int DAT_80612160;               // slot-1 base pointer

// ===========================================================================
// 0x8014f9f8  subobj_effect_edge_state
// Edge-triggered "just changed" query on a small latch table. For channel 1 it
// XORs the slot value (DAT_80612160+0x58) against the previous-frame snapshot
// (uRam80612172) and returns the sign bit of ((x>>1) - (x & cur)); channel 0
// uses DAT_80612170 with a two's-complement isolate-lowest-set test; channels
// 2 use (&DAT_80612170)[ch]. Channels > 2 return 0. Result is 0 or 1.
// ===========================================================================
unsigned int subobj_effect_edge_state(unsigned char channel)
{
    if (channel == 1) {
        unsigned int v = (unsigned int)((int)(short)uRam80612172) ^ (DAT_80612160 + 0x58);
        return (unsigned int)(((int)v >> 1) - (int)(v & (unsigned int)(DAT_80612160 + 0x58))) >> 0x1f;
    }
    if (channel == 0) {
        int d = (int)DAT_80612170;
        return (unsigned int)(-d & ~d) >> 0x1f;
    }
    if (2 < channel) {
        return 0;
    }
    unsigned short v = (&DAT_80612170)[channel] ^ uRam80612172;
    return (unsigned int)(((int)(short)v >> 1) -
                          (int)(short)(v & (&DAT_80612170)[channel])) >> 0x1f;
}

// ===========================================================================
// 0x8014ff00  subobj_defer_list_alloc
// Allocate the deferred per-frame callback list: a count*0x10 key array and a
// count*4 entity-pointer array, both zeroed. Records capacity and clears the
// live count. No-op for count <= 0. (Called from subobj_physics_step.)
// ===========================================================================
void subobj_defer_list_alloc(int count)
{
    if (0 < count) {
        unsigned int keyBytes = count << 4;
        DAT_8061219c = mem_alloc(keyBytes, 0);
        memset_wrapper(DAT_8061219c, 0, keyBytes);
        DAT_80612198 = mem_alloc(count << 2, 0);
        memset_wrapper(DAT_80612198, 0, count << 2);
        DAT_80612194 = 0;
        DAT_80612190 = count;
    }
}

// ===========================================================================
// 0x8014ff80  subobj_defer_list_push
// Append (entity, 0x10-byte key) to the deferred list if it is allocated and
// not full. (Called from subobj_constraint_resolve_events.)
// ===========================================================================
void subobj_defer_list_push(int entity, unsigned int key)
{
    if ((DAT_80612198 != 0) && (DAT_8061219c != 0) && (DAT_80612190 != 0) &&
        (DAT_80612194 < DAT_80612190)) {
        *(int*)(DAT_80612198 + DAT_80612194 * 4) = entity;
        leaf_31e8(DAT_8061219c + DAT_80612194 * 0x10, key, 0x10);
        DAT_80612194 = DAT_80612194 + 1;
    }
}

// ===========================================================================
// 0x8014fff8  subobj_defer_list_flush
// Drain the deferred list back-to-front: for each entity whose slot and key
// are non-null, invoke its callback function pointer at entity+0x170, then free
// both arrays and reset the list. (Called from subobj_physics_step.)
// ===========================================================================
void subobj_defer_list_flush(void)
{
    if ((DAT_80612198 != 0) && (DAT_8061219c != 0)) {
        while (DAT_80612194 != 0) {
            DAT_80612194 = DAT_80612194 + -1;
            int entity = *(int*)(DAT_80612198 + DAT_80612194 * 4);
            if ((entity != 0) && (*(int*)(DAT_8061219c + DAT_80612194 * 0x10) != 0)) {
                void (*cb)(void) = *(void(**)(void))(entity + 0x170);
                if (cb != 0) {
                    (*cb)();
                }
            }
        }
        mem_free_top(DAT_80612198);
        mem_free_top(DAT_8061219c);
        DAT_80612198 = 0;
        DAT_8061219c = 0;
        DAT_80612190 = 0;
        DAT_80612194 = 0;
    }
}

// ===========================================================================
// 0x801500b0  subobj_set_link_slot
// Store a value into a sub-object link-slot array on an entity's render data:
// *(entity+0x7c) + 0x4fc + index*4. (Called from FUN_8015d744.)
// ===========================================================================
void subobj_set_link_slot(int entity, int index, unsigned int value)
{
    *(unsigned int*)(*(int*)(entity + 0x7c) + index * 4 + 0x4fc) = value;
}

// ===========================================================================
// 0x80150314  subobj_flag_is_non_singleton
// Return 0 when 'flag' is exactly one of the reserved single-bit spawn flags
// (0x10,0x20,0x40,0x80,0x100,0x200,0x400,0x1000,0x2000,0x8000), otherwise 1.
// Used to gate whether a flag denotes a distinct reserved slot type.
// ===========================================================================
unsigned int subobj_flag_is_non_singleton(int flag)
{
    unsigned int result = 0;
    if (((((flag != 0x10) && (flag != 0x20) && (flag != 0x40)) &&
          ((flag != 0x80) && (flag != 0x100))) &&
         ((flag != 0x200) && ((flag != 0x400) && (flag != 0x1000)))) &&
        ((flag != 0x2000) && (flag != 0x8000))) {
        result = 1;
    }
    return result;
}
