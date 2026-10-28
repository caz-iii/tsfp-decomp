// Dolphin OS heap allocator (OSAlloc) + arena accessors + alarm-thread cleanup.
//
// OSAlloc is a fixed set of "heaps", each a descriptor triple at
// DAT_80613080[heapId] = { firstAllocated(+0), freeList(+4), allocList(+8) }.
// Cells are 0x20-byte-aligned; each cell carries a 0x20-byte header
// { prev(+0), next(+4), size(+8) } and hands back the payload at cell+0x20.
// The free list is kept address-sorted and coalesces adjacent cells on free.
//
// Also here: the OS "arena" hi/lo accessors (the bump region __OSInit hands to
// the game before the heap exists), and the alarm-queue sweep that cancels the
// alarms owned by a terminating thread.
//
// Batch: batch_1099
// Source module: os/os_alloc.cpp

#include "common.h"

// OSAlloc globals.
extern int  DAT_80613080;     // heap descriptor array base
extern int  DAT_80613084;     // heap count
extern unsigned int DAT_8061308c;   // arena end (aligned) reserved for heaps
extern int  DAT_80613088;     // first usable heap-cell address (aligned)
extern int  DAT_806113c0;     // current heap id
// Arena accessors' backing storage.
extern unsigned int DAT_80613090;   // arena hi (top)
extern unsigned int DAT_806113c8;   // arena lo (bottom)
// Alarm queue.
extern int *DAT_80613078;     // alarm queue head

extern void os_cancel_alarm(int *alarm);
extern int  FUN_803a5428(unsigned char *alarm);   // alarm-owner / still-live test

// Free-list insert with coalescing (declared before first use).
static unsigned int *os_heap_free_insert(unsigned int *list, unsigned int *cell);

// ---------------------------------------------------------------------------
// os_alarm_cancel_for_thread - alarm-queue sweep installed as a cleanup hook
// (referenced from the exception table at 0x804982a0). Walks the alarm queue
// and cancels each alarm that FUN_803a5428 reports is no longer live (its owning
// thread is gone). Address: 0x80398e00
// ---------------------------------------------------------------------------
int os_alarm_cancel_for_thread(int arg)
{
    if (arg != 0) {
        int *cur = DAT_80613078;
        int *next = (DAT_80613078 == (int *)0) ? (int *)0 : (int *)DAT_80613078[5];
        while (cur != (int *)0) {
            if (FUN_803a5428((unsigned char *)cur) == 0) {
                os_cancel_alarm(cur);
            }
            cur = next;
            next = (next == (int *)0) ? (int *)0 : (int *)next[5];
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------
// os_heap_free_insert - insert 'cell' into the address-sorted free list,
// coalescing with the neighbour above and/or below when physically adjacent.
// Returns the (possibly new) list head. Address: 0x80398ea0
// Cell layout: [0]=prev [1]=next [2]=size.
// ---------------------------------------------------------------------------
static unsigned int *os_heap_free_insert(unsigned int *list, unsigned int *cell)
{
    unsigned int *prev = (unsigned int *)0;
    unsigned int *cur;
    for (cur = list; (cur != (unsigned int *)0 && (cur < cell)); cur = (unsigned int *)cur[1]) {
        prev = cur;
    }

    cell[1] = (unsigned int)cur;
    cell[0] = (unsigned int)prev;

    if (cur != (unsigned int *)0) {
        *cur = (unsigned int)cell;
        // coalesce cell with the cell above it
        if ((unsigned int *)((int)cell + cell[2]) == cur) {
            cell[2] = cell[2] + cur[2];
            cur = (unsigned int *)cur[1];
            cell[1] = (unsigned int)cur;
            if (cur != (unsigned int *)0) {
                *cur = (unsigned int)cell;
            }
        }
    }

    if (prev == (unsigned int *)0) {
        return cell;
    }
    prev[1] = (unsigned int)cell;
    // coalesce prev with cell
    if ((unsigned int *)((int)prev + prev[2]) != cell) {
        return list;
    }
    prev[2] = prev[2] + cell[2];
    prev[1] = (unsigned int)cur;
    if (cur == (unsigned int *)0) {
        return list;
    }
    *cur = (unsigned int)prev;
    return list;
}

// ---------------------------------------------------------------------------
// os_alloc_from_heap - allocate 'size' bytes from heap 'heapId'. Rounds the
// request up to a 0x20 multiple (plus the 0x20 header), first-fit-walks the free
// list, splits the chosen cell when the remainder is >= 0x40, moves it onto the
// allocated list, and returns the payload pointer (cell + 0x20). Returns null on
// failure. Address: 0x80398f4c
// ---------------------------------------------------------------------------
int *os_alloc_from_heap(int heapId, int size)
{
    int heap = DAT_80613080 + heapId * 0xc;
    unsigned int need = (unsigned int)(size + 0x3f) & 0xffffffe0;

    int *cell;
    for (cell = *(int **)(heap + 4); (cell != (int *)0 && (cell[2] < (int)need));
         cell = (int *)cell[1]) {
    }
    if (cell == (int *)0) {
        return (int *)0;
    }

    int cellSize = cell[2];
    if (cellSize - (int)need < 0x40) {
        // take the whole cell: unlink from free list
        int newHead = *(int *)(heap + 4);
        if ((int *)cell[1] != (int *)0) {
            *(int *)cell[1] = *cell;
        }
        if (*cell == 0) {
            newHead = cell[1];
        } else {
            *(int *)(*cell + 4) = cell[1];
        }
        *(int *)(heap + 4) = newHead;
    } else {
        // split: shrink cell, spawn remainder back into free list
        cell[2] = need;
        int *rem = (int *)((int)cell + need);
        rem[2] = cellSize - need;
        rem[0] = cell[0];
        rem[1] = cell[1];
        if ((int *)rem[1] != (int *)0) {
            *(int **)rem[1] = rem;
        }
        if (rem[0] == 0) {
            *(int **)(heap + 4) = rem;
        } else {
            *(int **)(rem[0] + 4) = rem;
        }
    }

    // push onto the allocated list
    int *alloc = *(int **)(heap + 8);
    cell[1] = (int)alloc;
    *cell = 0;
    if (alloc != (int *)0) {
        *alloc = (int)cell;
    }
    *(int **)(heap + 8) = cell;
    return cell + 8;   // payload = cell + 0x20
}

// ---------------------------------------------------------------------------
// os_free_to_heap - return the payload at 'payload' to heap 'heapId'. Unlinks
// the cell (payload - 0x20) from the allocated list and reinserts it into the
// address-sorted free list (coalescing). Address: 0x80399048
// ---------------------------------------------------------------------------
void os_free_to_heap(int heapId, int payload)
{
    unsigned int *cell = (unsigned int *)(payload - 0x20);
    int heap = DAT_80613080 + heapId * 0xc;
    int newAlloc = *(int *)(heap + 8);

    if (*(unsigned int **)(payload - 0x1c) != (unsigned int *)0) {
        **(unsigned int **)(payload - 0x1c) = *cell;
    }
    if (*cell == 0) {
        newAlloc = *(int *)(payload - 0x1c);
    } else {
        *(int *)(*cell + 4) = *(int *)(payload - 0x1c);
    }
    *(int *)(heap + 8) = newAlloc;

    unsigned int *head = os_heap_free_insert(*(unsigned int **)(heap + 4), cell);
    *(unsigned int **)(heap + 4) = head;
}

// ---------------------------------------------------------------------------
// os_set_current_heap - set the current heap id, returning the previous one.
// Address: 0x803990c4
// ---------------------------------------------------------------------------
int os_set_current_heap(int heapId)
{
    int prev = DAT_806113c0;
    DAT_806113c0 = heapId;
    return prev;
}

// ---------------------------------------------------------------------------
// os_init_alloc - initialise the OSAlloc heap-descriptor array at 'arrayBase'
// with 'count' empty heaps, recording the arena end and the first usable
// (0x20-aligned) cell address just past the descriptor array. Address: 0x803990d4
// ---------------------------------------------------------------------------
void os_init_alloc(int arrayBase, unsigned int arenaEnd, int count)
{
    int off = 0;
    DAT_80613080 = arrayBase;
    DAT_80613084 = count;
    for (int i = 0; i < DAT_80613084; i = i + 1) {
        unsigned int *desc = (unsigned int *)(DAT_80613080 + off);
        desc[0] = 0xffffffff;   // free slot marker
        off = off + 0xc;
        desc[2] = 0;
        desc[1] = 0;
    }
    DAT_8061308c = arenaEnd & 0xffffffe0;
    DAT_806113c0 = 0xffffffff;
    DAT_80613088 = (DAT_80613080 + count * 0xc + 0x1f) & 0xffffffe0;
}

// ---------------------------------------------------------------------------
// os_create_heap - carve a heap over the address range [lo, hi). Finds the first
// free descriptor slot, seeds its free list with a single cell spanning the
// aligned range, and returns the heap id (or -1 when no slot is free).
// Address: 0x80399144
// ---------------------------------------------------------------------------
int os_create_heap(int lo, unsigned int hi)
{
    unsigned int *cell = (unsigned int *)((lo + 0x1f) & 0xffffffe0);
    int id = 0;
    int *desc = (int *)DAT_80613080;
    int remaining = DAT_80613084;
    if (0 < DAT_80613084) {
        do {
            if (*desc < 0) {   // free slot (marker 0xffffffff)
                *desc = (hi & 0xffffffe0) - (int)cell;   // total size
                cell[0] = 0;
                cell[1] = 0;
                cell[2] = *(unsigned int *)desc;
                desc[1] = (int)cell;   // free list head
                desc[2] = 0;           // allocated list empty
                return id;
            }
            desc = desc + 3;
            id = id + 1;
            remaining = remaining + -1;
        } while (remaining != 0);
    }
    return -1;
}

// ---------------------------------------------------------------------------
// os_get_arena_hi - return the current arena high (top) pointer. Address: 0x803991b0
// ---------------------------------------------------------------------------
unsigned int os_get_arena_hi(void)
{
    return DAT_80613090;
}

// ---------------------------------------------------------------------------
// os_get_arena_lo - return the current arena low (bottom) pointer. Address: 0x803991b8
// ---------------------------------------------------------------------------
unsigned int os_get_arena_lo(void)
{
    return DAT_806113c8;
}

// ---------------------------------------------------------------------------
// os_set_arena_hi - set the arena high (top) pointer. Address: 0x803991c0
// ---------------------------------------------------------------------------
void os_set_arena_hi(unsigned int hi)
{
    DAT_80613090 = hi;
}
