// MetroTRK circular receive-ring helpers.
//
// Both the AMC-DDH and GDEV transports buffer received bytes through a small
// circular ring control block. This file holds the shared push/init/count
// primitives; the per-transport drain (trk_amc_ring_drain) lives with the AMC
// driver but uses the same layout:
//   +0x00 read/write cursor   +0x04 write cursor
//   +0x08 base                +0x0c capacity
//   +0x10 total               +0x14 count(free)   +0x18 lock word
//
// Batch: batch_1092
// Source module: os/trk_ring.cpp

#include "common.h"

// memcpy-style leaf helper.
extern void leaf_31e8(unsigned int dst, unsigned int src, unsigned int len);
// Ring lock/commit hooks.
extern void FUN_80396fb8(unsigned int *lock);   // lock
extern void FUN_80396f94(void);                 // commit/unlock (push/drain)
extern void FUN_80396fe8(void);                 // init-complete hook

// ---------------------------------------------------------------------------
// Address: 0x80396a98 | Name: trk_ring_push | Date: 2026-09-14
// Pushes `len` bytes from `src` into the ring at `ring`, wrapping at the ring
// end and advancing the write cursor / total, decrementing the free count.
// Returns 0 on success, 0xffffffff when fewer than `len` bytes are free.
// ---------------------------------------------------------------------------
unsigned int trk_ring_push(int ring, unsigned int src, unsigned int len)
{
    unsigned int result;

    if (*(unsigned int *)(ring + 0x14) < len) {
        result = 0xffffffff;
    } else {
        FUN_80396fb8((unsigned int *)(ring + 0x18));
        unsigned int wr = *(unsigned int *)(ring + 4);
        unsigned int to_end = *(unsigned int *)(ring + 0xc) - (wr - *(unsigned int *)(ring + 8));
        if (to_end < len) {
            leaf_31e8(wr, src, to_end);
            leaf_31e8(*(unsigned int *)(ring + 8), src + to_end, len - to_end);
            *(unsigned int *)(ring + 4) = (*(unsigned int *)(ring + 8) + len) - to_end;
        } else {
            leaf_31e8(wr, src, len);
            *(unsigned int *)(ring + 4) = *(unsigned int *)(ring + 4) + len;
        }
        if (*(unsigned int *)(ring + 0xc) ==
            *(unsigned int *)(ring + 4) - *(unsigned int *)(ring + 8)) {
            *(unsigned int *)(ring + 4) = *(unsigned int *)(ring + 8);
        }
        *(unsigned int *)(ring + 0x14) = *(unsigned int *)(ring + 0x14) - len;
        *(unsigned int *)(ring + 0x10) = *(unsigned int *)(ring + 0x10) + len;
        FUN_80396f94();
        result = 0;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Address: 0x80396ba0 | Name: trk_ring_init | Date: 2026-09-14
// Initialises a ring over [base, base+cap): sets base/capacity, points both
// cursors at base, zeroes the count, seeds the free field to capacity.
// ---------------------------------------------------------------------------
void trk_ring_init(unsigned int *ring, unsigned int base, unsigned int cap)
{
    ring[2] = base;
    ring[3] = cap;
    ring[0] = ring[2];
    ring[1] = ring[2];
    ring[4] = 0;
    ring[5] = ring[3];
    FUN_80396fe8();
}

// ---------------------------------------------------------------------------
// Address: 0x80396bf0 | Name: trk_ring_count | Date: 2026-09-14
// Returns the buffered byte count (word at +0x10).
// ---------------------------------------------------------------------------
unsigned int trk_ring_count(int ring)
{
    return *(unsigned int *)(ring + 0x10);
}
