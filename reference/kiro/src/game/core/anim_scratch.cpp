// anim_scratch.cpp
// The codec scratch/arena page-pool allocator. This is the runtime allocator
// layer beneath the whole anim/msg codec: nearly every codec object
// (codebooks, channel descriptors, FFT plans, bit readers, message arenas)
// obtains its working memory from here via anim_scratch_alloc /
// anim_scratch_calloc and releases it via anim_scratch_free.
//
// The pool is initialized by anim_scratch_pool_init (see anim_codec.cpp,
// batch_523) which allocates:
//   DAT_80612508 : slot index table  (slotCount * 4000 bytes, 0x20-aligned)
//                  laid out as pairs of ints per slot: [base, sizeOrFlags]
//   DAT_8061250c : data-page arena    (slotCount * 0x64000 bytes)
//   DAT_80612500 : live-bytes cursor (running sum of active slot sizes)
//   DAT_80612504 : high-water mark   (peak of DAT_80612500)
//
// Each slot entry is two ints at DAT_80612508 + slot*8:
//   [+0] = page base pointer within the arena
//   [+4] = size, with bit 0x40000000 set meaning "free" (available for reuse)
//
// Address range: 0x801a12b0 - 0x801a1528  (batch_524, subsystem:game)
#include "common.h"

extern "C" {
// scratch pool globals (defined via the pool arena; see anim_scratch_pool_init)
extern int    DAT_80612508;   // slot index table base pointer
extern int    DAT_8061250c;   // data-page arena base pointer
extern int    DAT_80612500;   // live-bytes cursor
extern int    DAT_80612504;   // high-water mark

extern int    vm_region_alloc(int size, int prot, int flags); // reserve VM region
extern void   memset_wrapper(int dst, int val, int len);
extern int    leaf_31e8(unsigned int dst, unsigned int src, int len); // memmove(dst,src,len)

// register-pair carrier (r3:r4) for the realloc-copy entry
extern unsigned long long FUN_803826f0(void);
extern void   FUN_8038273c(void);   // restore paired with FUN_803826f0

// forward decls within this TU (allocator calls its own slot helpers)
int  anim_scratch_slot_find(unsigned int size);
int  anim_scratch_slot_size(int base);
int  anim_scratch_alloc(unsigned int size);
} // extern "C"

// ---------------------------------------------------------------------------
// anim_scratch_pool_alloc_pages (FUN_801a12b0)
// Reserve the pool's data-page arena: slotCount * 0x64000 bytes of VM
// (0x64000 = 409600 bytes per slot) into DAT_8061250c. Called from the pool
// setup path (anim_scratch_pool_init).
// ---------------------------------------------------------------------------
void anim_scratch_pool_alloc_pages(int slotCount) {
    DAT_8061250c = vm_region_alloc(slotCount * 0x64000, 2, 0);
}

// ---------------------------------------------------------------------------
// anim_scratch_slot_find (FUN_801a12e8)
// Locate the slot index to satisfy a request of `size` bytes, scanning the
// 500-entry slot table in three passes:
//   1) a free slot (bit 0x40000000 set) whose stored size exactly equals size
//   2) a free slot whose stored size is strictly larger than size
//   3) the first entirely-empty slot ([+4] == 0)
// Returns the slot index, or -1 if the table is full.
// ---------------------------------------------------------------------------
int anim_scratch_slot_find(unsigned int size) {
    int idx = 0;
    int off = 0;
    int remaining = 500;
    do {
        unsigned int entry = *(unsigned int *)(DAT_80612508 + off + 4);
        if ((entry & 0x40000000) != 0) {
            if (size == (entry & 0xbfffffff)) { return idx; }
        }
        idx = idx + 1;
        off = off + 8;
        remaining = remaining - 1;
    } while (remaining != 0);

    idx = 0;
    off = 0;
    remaining = 500;
    do {
        unsigned int entry = *(unsigned int *)(DAT_80612508 + off + 4);
        if ((entry & 0x40000000) != 0) {
            if (size < (entry & 0xbfffffff)) { return idx; }
        }
        idx = idx + 1;
        off = off + 8;
        remaining = remaining - 1;
    } while (remaining != 0);

    off = 0;
    idx = 0;
    remaining = 500;
    do {
        if (*(int *)(DAT_80612508 + off + 4) == 0) { return idx; }
        idx = idx + 1;
        off = off + 8;
        remaining = remaining - 1;
    } while (remaining != 0);
    return -1;
}

// ---------------------------------------------------------------------------
// anim_scratch_slot_size (FUN_801a13a0)
// Given a slot's page base pointer, return that slot's stored size ([+4]).
// If no slot matches (base not found in the 500-entry table) return the
// default per-slot page size 0x64000.
// ---------------------------------------------------------------------------
int anim_scratch_slot_size(int base) {
    int off = 0;
    int remaining = 500;
    do {
        if (*(int *)(DAT_80612508 + off) == base) {
            return *(int *)(DAT_80612508 + off + 4);
        }
        off = off + 8;
        remaining = remaining - 1;
    } while (remaining != 0);
    return 0x64000;
}

// ---------------------------------------------------------------------------
// anim_scratch_noop (FUN_801a13dc)
// Empty routine (padding / no-op hook in the allocator dispatch region).
// ---------------------------------------------------------------------------
void anim_scratch_noop(void) {
}

// ---------------------------------------------------------------------------
// anim_scratch_alloc (FUN_801a13e0)
// Allocate `size` bytes from the pool. Finds the slot (anim_scratch_slot_find);
// if that slot is fresh (size word == 0) it computes the slot's page base
// (slot 0 = arena start DAT_8061250c, otherwise prev.base + prev.size masked)
// and rounds the request up to a 0x20 multiple; if the slot was previously
// free it simply clears the free bit (0x40000000). Then it advances the
// live-bytes cursor DAT_80612500 and tracks the high-water mark DAT_80612504.
// Returns the slot's page base pointer.
// ---------------------------------------------------------------------------
int anim_scratch_alloc(unsigned int size) {
    int *tbl = (int *)DAT_80612508;
    int slot = anim_scratch_slot_find(size);
    if (tbl[slot * 2 + 1] == 0) {
        if (slot == 0) {
            tbl[0] = DAT_8061250c;
        } else {
            tbl[slot * 2] = tbl[slot * 2 + -2] + (tbl[slot * 2 + -1] & 0xbfffffff);
        }
        tbl[slot * 2 + 1] = (int)(size + 0x1f) & (int)0xffffffe0;
    } else {
        tbl[slot * 2 + 1] = tbl[slot * 2 + 1] & 0xbfffffff;
    }
    DAT_80612500 = DAT_80612500 + tbl[slot * 2 + 1];
    if (DAT_80612504 < DAT_80612500) {
        DAT_80612504 = DAT_80612500;
    }
    return tbl[slot * 2];
}

// ---------------------------------------------------------------------------
// anim_scratch_calloc (FUN_801a1498)
// Allocate count*elemSize bytes (anim_scratch_alloc) and zero them. Returns the
// zeroed page base pointer.
// ---------------------------------------------------------------------------
int anim_scratch_calloc(int count, int elemSize) {
    int base = anim_scratch_alloc((unsigned int)(count * elemSize));
    memset_wrapper(base, 0, count * elemSize);
    return base;
}

// ---------------------------------------------------------------------------
// anim_scratch_free (FUN_801a14dc)
// Release the slot whose page base equals `base`: subtract its size from the
// live-bytes cursor DAT_80612500 and set the free bit (0x40000000) on its size
// word so the slot can be reused by anim_scratch_slot_find. No-op if not found.
// (Central free used across the entire anim/msg codec.)
// ---------------------------------------------------------------------------
void anim_scratch_free(int base) {
    int off = 0;
    int remaining = 500;
    do {
        if (*(int *)(DAT_80612508 + off) == base) {
            int slot = DAT_80612508 + off;
            DAT_80612500 = DAT_80612500 - *(int *)(slot + 4);
            *(unsigned int *)(slot + 4) = *(unsigned int *)(slot + 4) | 0x40000000;
            return;
        }
        off = off + 8;
        remaining = remaining - 1;
    } while (remaining != 0);
}

// ---------------------------------------------------------------------------
// anim_scratch_realloc_copy (FUN_801a1528)
// Grow/rebind a scratch region: the (newSize:oldBase) pair arrives through the
// r3:r4 carrier (FUN_803826f0). Allocates a fresh region of newSize
// (anim_scratch_alloc), copies min(newSize, oldSlotSize) bytes from the old
// region (leaf_31e8 memmove), then frees the old region (anim_scratch_free).
// ---------------------------------------------------------------------------
void anim_scratch_realloc_copy(void) {
    unsigned long long pair = FUN_803826f0();
    unsigned int oldBase = (unsigned int)((unsigned long long)pair >> 0x20);
    unsigned int newSize = (unsigned int)pair;
    unsigned int newBase = (unsigned int)anim_scratch_alloc(newSize);
    unsigned int oldSize = (unsigned int)anim_scratch_slot_size((int)oldBase);
    if (newSize < oldSize) { oldSize = newSize; }
    leaf_31e8(newBase, oldBase, (int)oldSize);
    anim_scratch_free((int)oldBase);
    FUN_8038273c();
}
