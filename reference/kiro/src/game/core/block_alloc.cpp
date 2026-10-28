// Address: 0x80007528 | Name: block_alloc_init | Date: 2026-07-23
// Address: 0x80007594 | Name: block_alloc_create | Date: 2026-07-23
// Address: 0x800075c8 | Name: block_alloc_clear_ptrs | Date: 2026-07-23
// Address: 0x800075e4 | Name: block_free | Date: 2026-07-23
// Address: 0x800076ac | Name: block_free_with_destructor | Date: 2026-07-23
// Address: 0x8000771c | Name: block_free_simple | Date: 2026-07-23
// Address: 0x80007744 | Name: block_gc_find_or_evict | Date: 2026-07-23
// Address: 0x80007864 | Name: block_gc_wrapper | Date: 2026-07-23
// Address: 0x80007888 | Name: block_alloc_evictable | Date: 2026-07-23
// Address: 0x800079a4 | Name: block_alloc_permanent | Date: 2026-07-23
// Address: 0x80007a34 | Name: block_alloc_dispatch | Date: 2026-07-23
//
// Description: Free-list block allocator with garbage collection and eviction.
// Manages a contiguous memory region as a linked list of blocks.
// Each block has a 32-byte header:
//   [0x00] status (0=free, 1=evictable, 2=permanent)
//   [0x04] block size (including header)
//   [0x08] prev block size
//   [0x0C] callback function (destructor)
//   [0x10] userdata for callback
//
// Free blocks coalesce with adjacent free blocks (buddy merging).
// GC scans for free blocks, evicting used blocks if no free space available.
//
// Globals:
//   DAT_80611610 = total free space
//   DAT_80611614 = max contiguous free
//   DAT_80611618 = pool base pointer
//   DAT_8061161c = current scan pointer (for GC)
//   DAT_80611620 = pool start (after permanent blocks)
//   DAT_80611624 = pool end
//   DAT_80611628 = has evictable blocks flag
//   DAT_8060f9a8 = pool total size

#include "common.h"

// See raw Ghidra decompilation for full implementations.
// Key algorithms:
//   block_free: coalesces with prev/next free blocks
//   block_gc_find_or_evict: circular scan, evicts after 20 failures
//   block_alloc_evictable: splits free block, creates new free remainder
//   block_alloc_permanent: allocates from start, shifts pool start forward
