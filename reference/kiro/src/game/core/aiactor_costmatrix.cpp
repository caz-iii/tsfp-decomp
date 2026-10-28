// aiactor_costmatrix.cpp
// AI tracker cost-matrix state, recovered in batch_809. Driven by the AI
// behavior dispatch layer (aiactor_behavior_alloc_state 0x802b8d64 /
// aiactor_behavior_reset_state 0x802b8cc4), gated by
// tracker_costmatrix_mode_active(). Holds two heap buffers:
//   DAT_806120cc -> fixed 0x100-byte cost-matrix header (two -1 sentinels)
//   DAT_806120d0 -> per-player row buffer (DAT_80501572 rows * 0xc, 16-aligned)
//
// NON-MATCHING decompilation.

#include "common.h"

// ---------------------------------------------------------------------------
// Per-file externs for raw ROM symbols.
// ---------------------------------------------------------------------------
extern int          DAT_806120cc;   // cost-matrix header buffer base
extern int          DAT_806120d0;   // per-player cost row buffer base
extern signed char  DAT_80501572;   // active player/actor count

extern void memset_wrapper(int dst, int val, int n);

// mem_alloc here uses this game's wide PPC-ABI passthrough form: eight leading
// spill slots (doubles/u64) followed by (size, flag, ...). Bound to that shape.
extern int mem_alloc(unsigned long long a1, unsigned long long a2,
                     unsigned long long a3, unsigned long long a4,
                     unsigned long long a5, unsigned long long a6,
                     unsigned long long a7, unsigned long long a8,
                     int size, int flag, int a11, int a12,
                     int a13, int a14, int a15, int a16);

// ---------------------------------------------------------------------------
// 0x802768f0  tracker_costmatrix_reset
// Clear the cost-matrix header buffer (0x100 bytes) and set the two "no entry"
// sentinels at +0x08 and +0x18 to -1.
// ---------------------------------------------------------------------------
void tracker_costmatrix_reset(void)
{
    memset_wrapper(DAT_806120cc, 0, 0x100);
    *(int *)(DAT_806120cc + 0x08) = -1;
    *(int *)(DAT_806120cc + 0x18) = -1;
}

// ---------------------------------------------------------------------------
// 0x80276930  tracker_costmatrix_alloc
// Allocate the two cost-matrix buffers for the match. The leading eight args are
// the mem_alloc ABI spill prefix, forwarded verbatim. The header buffer is a
// fixed 0x100 bytes; the row buffer is (playerCount * 0xc) rounded up to a
// 16-byte boundary.
// ---------------------------------------------------------------------------
void tracker_costmatrix_alloc(unsigned long long a1, unsigned long long a2,
                              unsigned long long a3, unsigned long long a4,
                              unsigned long long a5, unsigned long long a6,
                              unsigned long long a7, unsigned long long a8,
                              int a9, int a10, int a11, int a12,
                              int a13, int a14, int a15, int a16)
{
    (void)a9;
    (void)a10;

    DAT_806120cc = mem_alloc(a1, a2, a3, a4, a5, a6, a7, a8,
                             0x100, 1, a11, a12, a13, a14, a15, a16);

    int rowBytes = ((int)DAT_80501572 * 0xc + 0xf) & ~0xf;
    DAT_806120d0 = mem_alloc(a1, a2, a3, a4, a5, a6, a7, a8,
                             rowBytes, 1, a11, a12, a13, a14, a15, a16);
}
