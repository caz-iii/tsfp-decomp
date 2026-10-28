// Batch: batch_975 | Module: arcade/challenge editor panel scratch ring-buffer allocator
// Address range: 0x80346bb0 - 0x80346ec8
// Date: 2026-09-12
//
// The scratch ring-buffer allocator behind batch_973/974's panel strip. The panel keeps a bump
// allocator over a 0x10000-byte arena (DAT_80612d90, allocated in batch_974's panel_alloc_scratch)
// plus two more arenas allocated here: a 0x1fff0 arena (DAT_80612d84) and an 0x8000 arena
// (DAT_80612d94). Live allocations are threaded through two singly-linked lists (heads DAT_80612d8c
// and DAT_80612d88) whose records carry their start offset at +0x00 and a next pointer at +0x10.
// DAT_80612d80 is the ring's current write cursor.
//
// A request first checks whether it fits ahead of the oldest live record (panel_ring_can_fit); if
// not, panel_ring_make_room evicts the oldest records from the DAT_80612d8c list (splicing them out
// and testing them against the DAT_80612d88 list via panel_ring_record_fits) until the request fits
// or the ring is emptied. panel_ring_alloc then bump-allocates the (4-aligned) request and returns a
// pointer into the 0x10000 arena. The small +0x10 linked-list helpers (tail / link / next) are the
// list primitives shared by both allocation lists.
//
//   panel_alloc_arenas       0x80346bb0  allocate + zero the 0x1fff0 and 0x8000 scratch arenas.
//   panel_ring_can_fit       0x80346c18  test whether a byte count fits ahead of the oldest record.
//   panel_ring_record_fits   0x80346c8c  test a record's offset against both allocation-list heads.
//   panel_ring_make_room     0x80346cec  evict oldest records until a request fits (ring reclaim).
//   panel_ring_alloc         0x80346da8  bump-allocate a 4-aligned request; return the arena pointer.
//   panel_ring_reset         0x80346df0  reset the ring + both arenas; optionally re-serialize map.
//   panel_arenas_init_serialize 0x80346e60 allocate the arenas then serialize the current map.
//   list_tail_next10         0x80346e8c  walk a +0x10-linked list to its tail node.
//   list_link_next10         0x80346eb4  link a node after another via the +0x10 next pointer.
//   list_get_next10          0x80346ec8  return a node's +0x10 next pointer.
//
// Callers: the arcade editor map-edit / undo path and panel_draw_slot_content's label records.
//
// NON-MATCHING: reproduces the decompiler's structure. The alignment/bump arithmetic
// ((size + 3) & ~3) and the list splicing are reproduced verbatim. The 16-argument mem_alloc
// varargs-style tail (params 9..16) is kept so the call sites match; the second mem_alloc reuses the
// 0x200 size constant left in a register by the intervening memset, reproduced as-is. Raw
// FUN_/DAT_ symbols are declared file-locally with call-site-accurate arity (Compile-Clean rule 5);
// named helpers (mem_alloc, memset_wrapper, aveditor_serialize_map) resolve through the shared
// headers (rule 6).

#include "common.h"

extern "C" {

// ---- ring/arena state ------------------------------------------------------
extern int DAT_80612d80;   // ring write cursor (byte offset into the 0x10000 arena)
extern int DAT_80612d84;   // 0x1fff0 arena base ptr
extern int DAT_80612d88;   // allocation list B head
extern int DAT_80612d8c;   // allocation list A head (oldest-first)
extern int DAT_80612d90;   // 0x10000 arena base ptr (from panel_alloc_scratch, batch_974)
extern int DAT_80612d94;   // 0x8000 arena base ptr

// ---- named cross-module helpers --------------------------------------------
extern int  mem_alloc(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                      unsigned long long a4, unsigned long long a5, unsigned long long a6,
                      unsigned long long a7, unsigned long long a8, int size, int tag,
                      int a11, int a12, int a13, int a14, int a15, int a16);
extern void memset_wrapper(int dst, int val, int len);
extern void aveditor_serialize_map(void);

// ---- forward decls within this TU ------------------------------------------
int  panel_ring_can_fit(int size);
int  panel_ring_record_fits(int* rec);
int  list_tail_next10(int node);
void panel_alloc_arenas(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                        unsigned long long a4, unsigned long long a5, unsigned long long a6,
                        unsigned long long a7, unsigned long long a8, int p9, int p10,
                        int p11, int p12, int p13, int p14, int p15, int p16);

// ---------------------------------------------------------------------------
// 0x80346bb0 : allocate + zero the 0x1fff0 and 0x8000 scratch arenas
// ---------------------------------------------------------------------------
void panel_alloc_arenas(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                        unsigned long long a4, unsigned long long a5, unsigned long long a6,
                        unsigned long long a7, unsigned long long a8, int p9, int p10,
                        int p11, int p12, int p13, int p14, int p15, int p16)
{
    int size;
    (void)p9; (void)p10;

    DAT_80612d84 = mem_alloc(a1, a2, a3, a4, a5, a6, a7, a8, 0x1fff0, 1,
                             p11, p12, p13, p14, p15, p16);
    size = 0x200;
    memset_wrapper(-0x7fa1d1b0, 0, 0x200);
    DAT_80612d94 = mem_alloc(a1, a2, a3, a4, a5, a6, a7, a8, 0x8000, 1,
                             size, p12, p13, p14, p15, p16);
    memset_wrapper(DAT_80612d94, 0, 0x8000);
}

// ---------------------------------------------------------------------------
// 0x80346c18 : test whether a byte count fits ahead of the oldest record
// ---------------------------------------------------------------------------
int panel_ring_can_fit(int size)
{
    int* oldest;
    int ok;

    oldest = (int*)list_tail_next10(DAT_80612d8c);
    if (DAT_80612d80 + size < 0x10001) {
        if (((oldest == (int*)0) || (DAT_80612d80 + size <= *oldest)) ||
            (*oldest < DAT_80612d80)) {
            ok = 1;
        }
        else {
            ok = 0;
        }
    }
    else {
        ok = 0;
    }
    return ok;
}

// ---------------------------------------------------------------------------
// 0x80346c8c : test a record's offset against both allocation-list heads
// ---------------------------------------------------------------------------
int panel_ring_record_fits(int* rec)
{
    int* a;
    int* b;

    for (a = (int*)DAT_80612d8c; b = (int*)DAT_80612d88, a != (int*)0; a = (int*)a[4]) {
        if (*a < *rec) {
            return 0;
        }
    }
    while (true) {
        if (b == (int*)0) {
            return 1;
        }
        if (*b < *rec) break;
        b = (int*)b[4];
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80346cec : evict oldest records until a request fits (ring reclaim)
// ---------------------------------------------------------------------------
void panel_ring_make_room(int size)
{
    int* head;
    int* oldest;
    int  ok;
    int* prev;

    while (true) {
        ok = panel_ring_can_fit(size);
        head = (int*)DAT_80612d8c;
        if (ok != 0) {
            panel_ring_can_fit(size);
            return;
        }
        oldest = (int*)list_tail_next10(DAT_80612d8c);
        if (oldest == (int*)0) break;
        if (oldest == (int*)DAT_80612d8c) {
            DAT_80612d8c = 0;
        }
        else {
            do {
                prev = head;
                head = (int*)prev[4];
            } while (head != oldest);
            prev[4] = 0;
        }
        ok = panel_ring_record_fits(oldest);
        if (ok != 0) {
            DAT_80612d80 = 0;
        }
    }
    DAT_80612d80 = 0;
    DAT_80612d88 = 0;
    DAT_80612d8c = 0;
}

// ---------------------------------------------------------------------------
// 0x80346da8 : bump-allocate a 4-aligned request; return the arena pointer
// ---------------------------------------------------------------------------
int panel_ring_alloc(int size)
{
    unsigned int aligned;
    int start;

    aligned = size + 3U & 0xfffffffc;
    panel_ring_can_fit(aligned);
    start = DAT_80612d80;
    DAT_80612d80 = DAT_80612d80 + aligned;
    return DAT_80612d90 + start;
}

// ---------------------------------------------------------------------------
// 0x80346df0 : reset the ring + both arenas; optionally re-serialize map
// ---------------------------------------------------------------------------
void panel_ring_reset(int reserialize)
{
    DAT_80612d80 = 0;
    DAT_80612d8c = 0;
    DAT_80612d88 = 0;
    memset_wrapper(DAT_80612d90, 0, 0x10000);
    memset_wrapper(DAT_80612d94, 0, 0x8000);
    if (reserialize != 0) {
        aveditor_serialize_map();
    }
}

// ---------------------------------------------------------------------------
// 0x80346e60 : allocate the arenas then serialize the current map
// ---------------------------------------------------------------------------
void panel_arenas_init_serialize(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                                 unsigned long long a4, unsigned long long a5, unsigned long long a6,
                                 unsigned long long a7, unsigned long long a8, int p9, int p10,
                                 int p11, int p12, int p13, int p14, int p15, int p16)
{
    panel_alloc_arenas(a1, a2, a3, a4, a5, a6, a7, a8, p9, p10, p11, p12, p13, p14, p15, p16);
    aveditor_serialize_map();
}

// ---------------------------------------------------------------------------
// 0x80346e8c : walk a +0x10-linked list to its tail node
// ---------------------------------------------------------------------------
int list_tail_next10(int node)
{
    int cur;

    if (node != 0) {
        do {
            cur = node;
            node = *(int*)(cur + 0x10);
        } while (node != 0);
        return cur;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80346eb4 : link a node after another via the +0x10 next pointer
// ---------------------------------------------------------------------------
int list_link_next10(int node, int prev)
{
    if (prev == 0) {
        return node;
    }
    *(int*)(prev + 0x10) = node;
    return prev;
}

// ---------------------------------------------------------------------------
// 0x80346ec8 : return a node's +0x10 next pointer
// ---------------------------------------------------------------------------
int list_get_next10(int node)
{
    return *(int*)(node + 0x10);
}

} // extern "C"
