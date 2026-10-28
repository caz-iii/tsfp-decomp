// Address range: 0x80133b18 - 0x80133bcc | batch_361 | 2026-09-04
//
// Generic singly-linked free-list allocator.
//
// A pool is described by a small 0x10-byte control block:
//     +0x00  void*  base       first element in the backing array
//     +0x04  void*  freeHead   head of the free list (nullptr when empty)
//     +0x08  int    freeCount  number of free elements
//     +0x0c  int    capacity   total element count
//
// Each free element stores, in its first word, a pointer to the next free
// element (an intrusive link), so no extra bookkeeping memory is needed.
// This is the allocator used throughout the game for fixed-size object pools
// (particles, render nodes, event slots, ...).

#include "common.h"

// memset_wrapper is the game's memset shim (0x80003100); declare it locally
// with the same signature used in mem_alloc.cpp.
extern s32 memset_wrapper(s32 dst, u8 val, u32 size);

// ---------------------------------------------------------------------------
// 0x80133b18 : build the free list over a contiguous array.
//
//   pool      control block (0x10 bytes)
//   stride    byte size of one element
//   base      first element
//   count     number of elements
//
// Threads every element onto the free list back-to-front so that the head
// ends up pointing at the first element in the array.
// ---------------------------------------------------------------------------
void freelist_init(void** pool, int stride, void* base, int count)
{
    pool[0] = base;                       // remember backing array base
    ((int*)pool)[2] = count;              // +0x08 freeCount = count
    ((int*)pool)[3] = count;              // +0x0c capacity  = count

    void* next = 0;
    char* elem = (char*)base;
    for (int i = count; i != 0; i--) {
        *(void**)elem = next;             // element->next = previous element
        next = elem;
        elem += stride;
    }
    pool[1] = next;                       // +0x04 freeHead = last element linked
}

// ---------------------------------------------------------------------------
// 0x80133b50 : reset the pool, returning the current backing-array base.
// Zeroes the whole 0x10-byte control block.
// ---------------------------------------------------------------------------
void* freelist_reset(void** pool)
{
    void* base = pool[0];
    memset_wrapper((int)pool, 0, 0x10);
    return base;
}

// ---------------------------------------------------------------------------
// 0x80133b88 : pop one element off the free list.
// Returns nullptr when the pool is exhausted.
// ---------------------------------------------------------------------------
void* freelist_pop(int pool)
{
    void** head = *(void***)(pool + 4);   // +0x04 freeHead
    if (head != 0) {
        *(void**)(pool + 4) = *head;      // freeHead = head->next
        *(int*)(pool + 8) -= 1;           // freeCount--
    }
    return head;
}

// ---------------------------------------------------------------------------
// 0x80133bb0 : push one element back onto the free list.
// ---------------------------------------------------------------------------
void freelist_push(int pool, void** elem)
{
    *elem = *(void**)(pool + 4);          // elem->next = freeHead
    *(void***)(pool + 4) = elem;          // freeHead = elem
    *(int*)(pool + 8) += 1;               // freeCount++
}

// ---------------------------------------------------------------------------
// 0x80133bcc : peek the pool's backing-array base (first control word).
// ---------------------------------------------------------------------------
void* freelist_peek_head(void** pool)
{
    return pool[0];
}
