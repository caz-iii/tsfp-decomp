// Address: 0x80382bd0 | Name: rt_double_to_ll        | Date: 2026-09-13
// Address: 0x80382c9c | Name: msl_heap_panic_report  | Date: 2026-09-13
// Address: 0x80382d54 | Name: msl_run_exit_chain     | Date: 2026-09-13
// Address: 0x80382e44 | Name: msl_free_locked        | Date: 2026-09-13
// Address: 0x80382eb4 | Name: msl_free_dispatch      | Date: 2026-09-13
// Address: 0x80382f0c | Name: msl_free_small_block   | Date: 2026-09-13
// Address: 0x80383064 | Name: msl_free_large_block   | Date: 2026-09-13
// Address: 0x803832f8 | Name: msl_heap_check_all     | Date: 2026-09-13
// Address: 0x80383368 | Name: msl_heap_walk_pools    | Date: 2026-09-13
//
// Metrowerks Standard Library (MSL) heap allocator + CodeWarrior exit glue,
// from the GCN_Mem_Alloc.c block at 0x80382bd0..0x803833f0 in main.dol.
//
// This is the classic MSL "pool_obj" allocator: the heap is a chain of pools,
// each carrying free-block lists bucketed by size. Freed blocks are coalesced
// with physically-adjacent neighbours (the low bit of the size word is the
// "prev free" flag, bit 2 is "this free") and re-linked into the pool's
// size-class free list. When a pool becomes entirely free it is unlinked and
// handed back via the lower-level pool routines (FUN_80399xxx). The allocator
// bit-layout is preserved verbatim from the decompiler because the recompile
// links against the same on-heap block headers.
//
// Also here: the double->long-long conversion runtime helper and the CodeWarrior
// __exit destructor-chain runner, which live physically adjacent to the
// allocator in the runtime image.

#include "common.h"

// --- Per-file externs for raw symbols referenced by this TU -----------------
// MSL panic strings (read as message pointers passed to OSReport).
extern char s_GCN_Mem_Alloc_c___InitDefaultHea_803e10f0[];
extern char s_Metrowerks_CW_runtime_library_in_803e1128[];

// Lower-level pool routines (heap grow/shrink, block bookkeeping).
extern int  FUN_803991b8(void);
extern uint FUN_803991b0(void);
extern int  FUN_803990d4(int, uint, int);
extern void FUN_803991c8(uint);
extern int  FUN_80399144(uint, uint);
extern void FUN_803990c4(int);
extern void FUN_80399048(int, int);

// Heap critical-section lock/unlock/flush (return the active heap handle).
extern int  FUN_80386ddc(void);   // lock, returns heap*
extern void FUN_80386dd8(void);   // unlock
extern void FUN_80386de0(void);   // flush

// Static-destructor / atexit support used by the exit-chain runner.
extern void FUN_803a10c8(void);              // final teardown
extern void aiactor_run_cleanup_list(void);  // named cross-module helper

// Per-pool validation + reporting helpers (heap check / walk).
extern int  FUN_80387190(void *pool);
extern void FUN_803872c8(int heap, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7, u64 a8, int *pool);

// Raw heap-state globals.
extern int   DAT_806113c0;   // default heap handle (-1 = uninitialised)
extern int   DAT_80612fd0;   // exit-chain-run guard
extern void (*DAT_80612fd8)(void);
extern void (*DAT_80612fdc)(void);
extern int   DAT_80612fd4;   // atexit slot count
extern int   DAT_805ef578;   // atexit function table base
extern int   DAT_803cc0a0;   // global static-destructor table base
extern char  DAT_80612fe0;   // "large free list initialised" flag
extern int   DAT_805ef678;   // large free-list root
extern uint  DAT_803e1168;   // small-block size-class table
extern int   DAT_804975e8;   // heap-pool list head
extern void  memset_wrapper(int dst, int val, int len);  // named helper

// ---------------------------------------------------------------------------
// rt_double_to_ll (0x80382bd0) - runtime double -> signed 64-bit conversion.
// Decomposes the IEEE-754 double into its biased exponent and 53-bit mantissa,
// then shifts the mantissa left/right into the 64-bit integer position. Values
// with magnitude >= 2^63 saturate to LLONG_MAX / LLONG_MIN; magnitudes < 1
// return 0. The sign bit negates the assembled magnitude.
u64 rt_double_to_ll(u64 value)
{
    bool nonZero;
    u32 mantissa;
    u32 biasedExp;
    int shift;
    u32 resultHi;
    u32 resultLo;

    resultHi = (u32)(value >> 0x20);
    resultLo = (u32)value;
    biasedExp = resultHi >> 0x14 & 0x7ff;
    if (biasedExp < 0x3ff) {          // |value| < 1.0
        mantissa = 0;
        resultLo = 0;
    }
    else {
        mantissa = resultHi & 0xfffff | 0x100000;   // restore implicit 1 bit
        shift = biasedExp - 0x433;                   // exponent - (52 + bias)
        if (shift < 0) {
            shift = -shift;
            resultLo = resultLo >> shift | mantissa << biasedExp - 0x413 |
                       mantissa >> shift + -0x20;
            mantissa = mantissa >> shift;
        }
        else {
            if (10 < shift) {                        // overflow: saturate
                if ((value & 0x8000000000000000) == 0) {
                    mantissa = 0x7fffffff;
                    resultLo = 0xffffffff;
                }
                else {
                    mantissa = 0x80000000;
                    resultLo = 0;
                }
                return CONCAT44(mantissa, resultLo);
            }
            mantissa = mantissa << shift | resultLo >> 0x20 - shift |
                       resultLo << biasedExp - 0x453;
            resultLo = resultLo << shift;
        }
        if ((value & 0x8000000000000000) != 0) {     // apply sign
            nonZero = resultLo != 0;
            resultLo = -resultLo;
            mantissa = -(nonZero + mantissa);
        }
    }
    return CONCAT44(mantissa, resultLo);
}

// ---------------------------------------------------------------------------
// msl_heap_panic_report (0x80382c9c) - lazy default-heap init / OOM reporter.
// Called when the default heap handle is still -1: prints the two MSL runtime
// messages via OSReport, then bootstraps a default heap from the free store
// (query size, align to 32 bytes, register the region) before deferring to the
// low-level allocator for the caller's request.
void msl_heap_panic_report(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7, u64 a8,
                           int reqSize)
{
    int  heapBase;
    uint heapSize;
    int  region;

    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;
    if (DAT_806113c0 == -1) {
        // Original MSL emits these as single-argument OSReport("...") calls; the
        // decompiler's varargs shuffle surfaced a1..a8 as leading positionals.
        OSReport(s_GCN_Mem_Alloc_c___InitDefaultHea_803e10f0);
        OSReport(s_Metrowerks_CW_runtime_library_in_803e1128);
        heapBase = FUN_803991b8();
        heapSize = FUN_803991b0();
        region = FUN_803990d4(heapBase, heapSize, 1);
        FUN_803991c8(region);
        region = FUN_80399144(region + 0x1fU & 0xffffffe0, heapSize & 0xffffffe0);
        FUN_803990c4(region);
        FUN_803991c8(heapSize & 0xffffffe0);
    }
    FUN_80399048(DAT_806113c0, reqSize);
}

// ---------------------------------------------------------------------------
// msl_run_exit_chain (0x80382d54) - CodeWarrior __exit / static-destructor
// runner. On first entry it invokes the module cleanup lists and the global
// destructor table at DAT_803cc0a0, then unwinds the atexit stack (DAT_805ef578
// indexed by the DAT_80612fd4 counter) and calls any registered final hooks.
void msl_run_exit_chain(void)
{
    void (**dtor)(void);

    if (DAT_80612fd0 == 0) {
        FUN_80386ddc();
        FUN_80386dd8();
        aiactor_run_cleanup_list();
        for (dtor = (void (**)(void))&DAT_803cc0a0; *dtor != (void (*)(void))0; dtor = dtor + 1) {
            (*dtor)();
        }
        if (DAT_80612fd8 != (void (*)(void))0) {
            (*DAT_80612fd8)();
            DAT_80612fd8 = (void (*)(void))0;
        }
    }
    FUN_80386ddc();
    while (0 < DAT_80612fd4) {
        DAT_80612fd4 = DAT_80612fd4 + -1;
        (**(void (***)(void))(&DAT_805ef578 + DAT_80612fd4 * 4))();
    }
    FUN_80386dd8();
    FUN_80386de0();
    if (DAT_80612fdc != (void (*)(void))0) {
        (*DAT_80612fdc)();
        DAT_80612fdc = (void (*)(void))0;
    }
    FUN_803a10c8();
}

// forward decls for the mutually-recursive free-block helpers below
void msl_free_dispatch(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7, u64 a8,
                       u32 *pool, int *block);
void msl_free_small_block(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7, u64 a8,
                          u32 *pool, int *block, uint blockSize);
void msl_free_large_block(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7, u64 a8,
                          u32 *pool, int *block);

// ---------------------------------------------------------------------------
// msl_free_locked (0x80382e44) - public free() body under the heap lock. Lazily
// zero-inits the large free-list root (DAT_805ef678, 0x34 bytes) on first use,
// then dispatches the block to the size-appropriate free routine.
void msl_free_locked(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7, u64 a8,
                     int *block)
{
    int heap;

    heap = (int)FUN_80386ddc();
    if (DAT_80612fe0 == '\0') {
        memset_wrapper((int)&DAT_805ef678, 0, 0x34);
        DAT_80612fe0 = '\x01';
    }
    msl_free_dispatch((u64)heap, a2, a3, a4, a5, a6, a7, a8, (u32 *)&DAT_805ef678, block);
    FUN_80386dd8();
}

// ---------------------------------------------------------------------------
// msl_free_dispatch (0x80382eb4) - routes a block to the small- or large-block
// free path based on its recorded size. The size lives either in the header
// pointed at by block[-1] (allocated tag) or is derived from block[-2] when the
// block carries an inline size (in-use tag, low bit set).
void msl_free_dispatch(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7, u64 a8,
                       u32 *pool, int *block)
{
    uint blockSize;

    if (block != (int *)0) {
        if ((block[-1] & 1U) == 0) {
            blockSize = *(uint *)(block[-1] + 8);
        }
        else {
            blockSize = (block[-2] & 0xfffffff8U) - 8;
        }
        if (blockSize < 0x45) {
            msl_free_small_block(a1, a2, a3, a4, a5, a6, a7, a8, pool, block, blockSize);
        }
        else {
            msl_free_large_block(a1, a2, a3, a4, a5, a6, a7, a8, pool, block);
        }
    }
}

// ---------------------------------------------------------------------------
// msl_free_small_block (0x80382f0c) - returns a small block to its size-class
// fixed-size free list. Finds the size class by scanning DAT_803e1168, splices
// the owning fixed-size sub-heap out of the pool's active ring if it was full,
// then pushes the block onto the class free list. When the sub-heap's live
// count hits zero it is fully unlinked and handed to the large-block reclaimer.
void msl_free_small_block(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7, u64 a8,
                          u32 *pool, int *block, uint blockSize)
{
    int  *subHeap;
    int  *ring;
    uint *classPtr;
    int   classIndex;

    classIndex = 0;
    for (classPtr = &DAT_803e1168; *classPtr < blockSize; classPtr = classPtr + 1) {
        classIndex = classIndex + 1;
    }
    subHeap = (int *)block[-1];
    ring = (int *)pool + classIndex * 2 + 1;
    if (subHeap[3] == 0) {
        if ((int *)ring[1] != subHeap) {
            if ((int *)*ring == subHeap) {
                ring[1] = *(int *)ring[1];
                *ring = *(int *)*ring;
            }
            else {
                *(int *)(*subHeap + 4) = subHeap[1];
                *(int *)subHeap[1] = *subHeap;
                subHeap[1] = ring[1];
                *subHeap = *(int *)subHeap[1];
                *(int **)(*subHeap + 4) = subHeap;
                *(int **)subHeap[1] = subHeap;
                ring[1] = (int)subHeap;
            }
        }
    }
    *block = subHeap[3];
    subHeap[3] = (int)(block + -1);
    classIndex = subHeap[4];
    subHeap[4] = classIndex + -1;
    if (classIndex + -1 == 0) {
        if ((int *)ring[1] == subHeap) {
            ring[1] = subHeap[1];
        }
        if ((int *)*ring == subHeap) {
            *ring = *subHeap;
        }
        *(int *)(*subHeap + 4) = subHeap[1];
        *(int *)subHeap[1] = *subHeap;
        if ((int *)ring[1] == subHeap) {
            ring[1] = 0;
        }
        if ((int *)*ring == subHeap) {
            *ring = 0;
        }
        msl_free_large_block(a1, a2, a3, a4, a5, a6, a7, a8, pool, subHeap);
    }
}

// ---------------------------------------------------------------------------
// msl_free_large_block (0x80383064) - returns a large/variable block to the
// pool, coalescing with the physically-preceding and -following blocks when
// they are free (size-word flag bits 1/2), updating the boundary-tag footers,
// and re-inserting the merged block into the pool's large free list. If the
// merge leaves the whole pool empty it is unlinked and released via
// msl_heap_panic_report's pool path.
void msl_free_large_block(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7, u64 a8,
                          u32 *pool, int *block)
{
    int  *poolHdr;
    int   listSlot;
    int  *listHead;
    int   mergedSize;
    uint  poolMax;
    uint *cur;
    uint  thisSize;
    uint *hdr;
    uint *nextHdr = (uint *)0;
    uint  nextSize = 0;

    hdr = (uint *)(block + -2);
    thisSize = block[-2] & 0xfffffff8;
    *hdr = block[-2] & 0xfffffffd;                 // clear "prev in use"
    cur = (uint *)((int)hdr + thisSize);
    poolHdr = (int *)(block[-1] & 0xfffffffe);
    *cur = *cur & 0xfffffffb;
    cur[-1] = thisSize;
    listSlot = (poolHdr[3] & 0xfffffff8U) - 4;
    if (*(int *)((int)poolHdr + listSlot) == 0) {
        *(uint **)((int)poolHdr + listSlot) = hdr;
        *block = (int)hdr;
        block[1] = (int)hdr;
    }
    else {
        *block = *(int *)(*(int *)((int)poolHdr + listSlot) + 8);
        *(uint **)(*block + 0xc) = hdr;
        block[1] = *(int *)((int)poolHdr + listSlot);
        *(uint **)(*(int *)((int)poolHdr + listSlot) + 8) = hdr;
        *(uint **)((int)poolHdr + listSlot) = hdr;
        cur = *(uint **)((int)poolHdr + listSlot);
        if (((*cur & 4) == 0) && (thisSize = cur[-1], (thisSize & 2) == 0)) {
            hdr = (uint *)((int)cur - thisSize);
            *hdr = *hdr & 7;
            *hdr = *hdr | thisSize + (*cur & 0xfffffff8) & 0xfffffff8;
            if ((*hdr & 2) == 0) {
                mergedSize = thisSize + (*cur & 0xfffffff8);
                *(int *)((int)hdr + mergedSize + -4) = mergedSize;
            }
            if (*(uint **)((int)poolHdr + listSlot) == cur) {
                *(uint *)((int)poolHdr + listSlot) = (*(uint **)((int)poolHdr + listSlot))[3];
            }
            *(uint *)(cur[3] + 8) = cur[2];
            *(uint *)(*(int *)(cur[3] + 8) + 0xc) = cur[3];
            cur = hdr;
        }
        *(uint **)((int)poolHdr + listSlot) = cur;
        nextHdr = *(uint **)((int)poolHdr + listSlot);
        nextSize = *nextHdr & 0xfffffff8;
        hdr = (uint *)((int)nextHdr + nextSize);
        thisSize = *hdr;
        if ((thisSize & 2) == 0) {
            *nextHdr = *nextHdr & 7;
            thisSize = nextSize + (thisSize & 0xfffffff8);
            *nextHdr = *nextHdr | thisSize;
            if ((*nextHdr & 2) == 0) {
                *(uint *)((int)nextHdr + (thisSize - 4)) = thisSize;
            }
            if ((*nextHdr & 2) == 0) {
                *(uint *)((int)nextHdr + thisSize) = *(uint *)((int)nextHdr + thisSize) & 0xfffffffb;
            }
            else {
                *(uint *)((int)nextHdr + thisSize) = *(uint *)((int)nextHdr + thisSize) | 4;
            }
            if (*(uint **)((int)poolHdr + listSlot) == hdr) {
                *(uint *)((int)poolHdr + listSlot) = (*(uint **)((int)poolHdr + listSlot))[3];
            }
            if (*(uint **)((int)poolHdr + listSlot) == hdr) {
                *(uint *)((int)poolHdr + listSlot) = 0;
            }
            *(uint *)(hdr[3] + 8) = hdr[2];
            *(uint *)(hdr[2] + 0xc) = hdr[3];
        }
    }
    poolMax = poolHdr[2];
    thisSize = **(uint **)((int)poolHdr + listSlot) & 0xfffffff8;
    if (poolMax < thisSize) {
        poolHdr[2] = thisSize;
    }
    listSlot = 0;
    if (((poolHdr[4] & 2U) == 0) &&
        (poolMax = poolHdr[4] & 0xfffffff8, poolMax == (poolHdr[3] & 0xfffffff8U) - 0x18)) {
        listSlot = 1;
    }
    if (listSlot != 0) {
        listHead = (int *)poolHdr[1];
        if (listHead == poolHdr) {
            listHead = (int *)0;
        }
        if ((int *)*pool == poolHdr) {
            *pool = (u32)(uintptr_t)listHead;
        }
        if (listHead != (int *)0) {
            *listHead = *poolHdr;
            *(int **)(*listHead + 4) = listHead;
        }
        poolHdr[1] = 0;
        *poolHdr = 0;
        msl_heap_panic_report(a1, a2, a3, a4, a5, a6, a7, a8, (int)poolHdr);
    }
}

// ---------------------------------------------------------------------------
// msl_heap_check_all (0x803832f8) - integrity sweep across every pool in the
// heap-pool list (DAT_804975e8). For each pool with an active flag-field it runs
// FUN_80387190 and returns 0xffffffff if any pool reports corruption, else 0.
u32 msl_heap_check_all(void)
{
    u32  *pool;
    int   bad;
    u32   result;

    pool = (u32 *)&DAT_804975e8;
    result = 0;
    while (true) {
        if (pool == (u32 *)0) break;
        if (((*(u16 *)(pool + 1) >> 6 & 7) != 0) && (bad = FUN_80387190(pool), bad != 0)) {
            result = 0xffffffff;
        }
        pool = (u32 *)pool[0x13];
    }
    return result;
}

// ---------------------------------------------------------------------------
// msl_heap_walk_pools (0x80383368) - locked walk over the pool list running the
// per-pool report/callback (FUN_803872c8) on active pools, marking swept pools
// and either recycling their headers or draining them through msl_free_locked
// when they still hold live allocations.
void msl_heap_walk_pools(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7, u64 a8)
{
    int *pool;
    int *cur;
    int  heap;

    heap = (int)FUN_80386ddc();
    pool = (int *)&DAT_804975e8;
    while (true) {
        cur = pool;
        if (cur == (int *)0) break;
        if ((*(u16 *)(cur + 1) >> 6 & 7) != 0) {
            FUN_803872c8(heap, a2, a3, a4, a5, a6, a7, a8, cur);
        }
        pool = (int *)cur[0x13];
        if (*(char *)(cur + 3) == '\0') {
            *(u16 *)(cur + 1) = *(u16 *)(cur + 1) & 0xfe3f | 0xc0;
            if ((pool != (int *)0) && (*(char *)(pool + 3) != '\0')) {
                cur[0x13] = 0;
            }
        }
        else {
            msl_free_locked((u64)heap, a2, a3, a4, a5, a6, a7, a8, cur);
            heap = (int)FUN_80386ddc();
        }
    }
    FUN_80386dd8();
}
