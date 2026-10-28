// ============================================================================
// Address range: 0x80185a54 - 0x80186168 | batch_478 | Date: 2026-09-06
// Subsystem: audio (streamed-texture voice / page-table manager).
//            tag audio-stream-tex.
//
// The page-allocation layer beneath the batch_476/477 texture streamer.  Each
// streamed texture occupies a "voice" slot (flags in DAT_80599314, stride 0xc;
// staging/size in DAT_80599304.. stride 0x18; page cursor in DAT_8059930c,
// stride 6 words) whose GX data lives in a ring of 0x800-byte pages tracked by a
// parallel state/owner table (DAT_8059d4a0 = page state, DAT_8059d4a2 = owner id,
// interleaved shorts, stride 4).  This file allocates page runs, DMA-commits the
// voice data, locks/unlocks pages against eviction, and frees them.
//
//   stream_voice_dma_commit    0x80185a54  occ_dma_upload the voice data, set busy flags
//   stream_voice_mark_pinned   0x80185adc  set the pinned flag (0x40), advance the cursor
//   stream_voice_page_alloc    0x80185b08  reserve a page run for an inactive voice
//   stream_voice_page_free_one 0x80185b88  free one voice's pages + rebind GX to fallback
//   stream_voice_evict         0x80185c38  evict a voice (free; if pinned, spin-free)
//   stream_page_ring_reserve   0x80185ce4  the page-ring allocator (find/rotate/install)
//   stream_voice_pages_release 0x80185f04  clear the voice's page-table entries
//   stream_voice_pages_lock    0x80185fe8  mark the voice's pages locked (state 2)
//   stream_voice_pages_unlock  0x801860a8  mark the voice's pages unlocked (state 1)
//   stream_voice_lock_once     0x80186168  lock pages once + set the locked flag (4)
//
// Structurally-faithful non-matching port.  The spawn-frame register pair
// (FUN_803826f0 high = voice index, low = device id / dst) is read through a
// typed function-pointer alias.  stream_page_ring_reserve returns the base page
// index in r3 (the decompiler typed it void); it is called through a typed alias
// so the return is recovered.  The __save/__restore_gpr_25 spill stubs are
// dropped as ABI artifacts.

#include "common.h"
#include "generated_decls.h"

// ---- spawn-frame register-pair helper ---------------------------------------
// high 32 = voice index, low 32 = device id / dst address.
typedef unsigned long long (*FramePairFn)(void);
#define FRAME_PAIR_F ((FramePairFn)FUN_803826f0)
extern void FUN_803826f0(void);
extern void FUN_8038273c(void);   // spawn-frame epilogue

// stream_page_ring_reserve returns the base page index in r3; typed alias.
typedef int (*PageRingReserveFn)(void);

// ---- GX bind helpers (0x803c8xxx block) -------------------------------------
extern void FUN_803c8f7c(int gxObj, int dataAddr);          // bind tex data ptr
extern void FUN_803c8fe0(double lodBias, int gxObj);        // set tex LOD/commit

// ---- shared occlusion/DMA helper --------------------------------------------
// occ_dma_upload resolves through generated_decls (declared in the occlusion
// module).  audio_voice_is_active resolves through generated_decls (batch_476).

// ---- voice slot tables (element base + ROM stride) --------------------------
extern unsigned char  DAT_80599300;   // voice GX-object id      (stride 6 words)
extern int            DAT_80599304;   // voice buffer-addr       (stride 0x18)
extern int            DAT_80599308;   // voice staging-addr      (stride 0x18)
extern int            DAT_8059930c;   // voice page cursor/addr  (stride 6 words)
extern short          DAT_80599310;   // voice size-hi (pages)   (stride 0xc)
extern short          DAT_80599312;   // voice size-lo (pages)   (stride 0x18)
extern unsigned short DAT_80599314;   // voice flags             (stride 0xc)

// ---- page ring: state + owner interleaved shorts (stride 4) ------------------
extern short DAT_8059d4a0;   // page state: 0 free, 1 allocated, 2 locked
extern short DAT_8059d4a2;   // page owner: voice index, or -1 (0xffff) if free

// ---- page-ring bookkeeping globals ------------------------------------------
extern int   DAT_80612424;   // page-ring base address
extern short DAT_8061243c;   // free page count
extern short DAT_8061243e;   // locked page count
extern int   DAT_80612438;   // page-ring rotating cursor
extern int   DAT_8061244c;   // total page count
extern int   DAT_80612470;   // committed-page counter

// ---- rodata ------------------------------------------------------------------
extern float FLOAT_80616760; // default LOD bias

// -----------------------------------------------------------------------------
// stream_voice_dma_commit (0x80185a54)
// DMA-upload the active voice's staged data into its page run, then flag the
// voice as loaded+busy (bits 0x1a set) and clear the pending-unlock bit (0x40).
// The voice index + device id arrive in the spawn-frame pair.
// -----------------------------------------------------------------------------
void stream_voice_dma_commit(void)
{
    unsigned long long frame = FRAME_PAIR_F();
    int v = (int)(frame >> 0x20);

    occ_dma_upload((int)frame, *(int *)(&DAT_80599308 + v * 0x18),
                   (unsigned int)*(unsigned short *)(&DAT_80599312 + v * 0x18) << 5);
    (&DAT_80599314)[v * 0xc] = (&DAT_80599314)[v * 0xc] | 0x1a;
    (&DAT_80599314)[v * 0xc] = (&DAT_80599314)[v * 0xc] & 0xffbf;
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// stream_voice_mark_pinned (0x80185adc)
// Set the voice's pinned flag (0x40) and return `cursor` advanced past this
// voice's low-half page span (size-lo pages * 0x20 bytes each).
// -----------------------------------------------------------------------------
int stream_voice_mark_pinned(int v, int cursor)
{
    (&DAT_80599314)[v * 0xc] = (&DAT_80599314)[v * 0xc] | 0x40;
    return cursor + (unsigned int)*(unsigned short *)(&DAT_80599312 + v * 0x18) * 0x20;
}

// -----------------------------------------------------------------------------
// stream_voice_page_alloc (0x80185b08)
// If the voice is not already active, reserve a page run for it: record the run
// base in the voice's page cursor, pin it, and return 1.  Returns 0 if the voice
// is already active or the ring is full.
// -----------------------------------------------------------------------------
int stream_voice_page_alloc(int v)
{
    if (audio_voice_is_active(v) != 0) {
        return 0;
    }
    int base = ((PageRingReserveFn)stream_page_ring_reserve)();
    if (base == 0) {
        return 0;
    }
    (&DAT_8059930c)[v * 6] = base;
    stream_voice_mark_pinned(v, base + (unsigned int)(unsigned short)(&DAT_80599310)[v * 0xc] * 0x20);
    return 1;
}

// -----------------------------------------------------------------------------
// stream_voice_page_free_one (0x80185b88)
// If the voice is loaded (flag 1|2 set, not evicting 0x10), release its page
// entries, keep only the retained bit (0x20), rebind its GX object to the
// fallback data address, and reset its LOD.  Returns whether it acted.
// -----------------------------------------------------------------------------
bool stream_voice_page_free_one(int v)
{
    unsigned short f = (&DAT_80599314)[v * 0xc];
    bool act = !(((f & 1) == 0) || ((f & 2) == 0) || ((f & 0x10) != 0));
    if (act) {
        stream_voice_pages_release(v);
        (&DAT_80599314)[v * 0xc] = (&DAT_80599314)[v * 0xc] & 0x20;
        FUN_803c8f7c((&DAT_80599300)[v * 6],
                     *(int *)(&DAT_80599308 + v * 0x18) +
                     (unsigned int)(unsigned short)(&DAT_80599310)[v * 0xc] * -0x20);
        FUN_803c8fe0((double)FLOAT_80616760, (&DAT_80599300)[v * 6]);
    }
    return act;
}

// -----------------------------------------------------------------------------
// stream_voice_evict (0x80185c38)
// Evict a voice: if it is loaded, free it once; if it was still busy (flag 1&2
// with no 0x10), set the evicting bit and spin-free until it drains.  Returns 1
// when the slot ends up free/idle.  Called by stream_tex_flush_all.
// -----------------------------------------------------------------------------
bool stream_voice_evict(int v)
{
    unsigned short *pf = &DAT_80599314 + v * 0xc;
    unsigned short f = *pf;

    bool loaded = ((f & 1) != 0) && ((f & 2) != 0) && ((f & 0x10) == 0);
    bool result;
    if (loaded) {
        result = stream_voice_page_free_one(v);
    }
    else if (((f & 1) == 0) || ((f & 2) == 0)) {
        result = true;
    }
    else {
        *pf = *pf | 0x10;
        do {
            result = stream_voice_page_free_one(v);
        } while (!result);
        result = true;
    }
    return result;
}

// -----------------------------------------------------------------------------
// stream_page_ring_reserve (0x80185ce4)
// Allocate a contiguous run of `pages` (derived from the active voice's hi+lo
// page counts, rounded up over 0x20-byte units) from the page ring.  Rotates the
// ring cursor DAT_80612438 looking for a free-or-evictable window; evicts locked
// pages it passes, installs the run (state 1, owner = voice index), and returns
// the run base index (0 on failure).  Returns the base in r3.
//
// The active voice index arrives in the frame's high word (via the __save_gpr_25
// spill, here read explicitly from the frame pair).
// -----------------------------------------------------------------------------
int stream_page_ring_reserve(void)
{
    unsigned long long frame = FRAME_PAIR_F();
    int v = (int)(frame >> 0x20);

    int bestGapStart = DAT_8061244c + 1;
    int freeRun = 0;
    int scanned = 0;
    unsigned int base = 0;
    unsigned int need = (unsigned int)(unsigned short)(&DAT_80599310)[v * 0xc] +
                        (unsigned int)*(unsigned short *)(&DAT_80599312 + v * 0x18) >> 6;
    if ((((unsigned int)(unsigned short)(&DAT_80599310)[v * 0xc] +
          (unsigned int)*(unsigned short *)(&DAT_80599312 + v * 0x18)) * 0x20 & 0x7e0) != 0) {
        need = need + 1;
    }

    unsigned int runBase = base;
    if (need <= (unsigned int)((unsigned short)DAT_8061243c + (unsigned short)DAT_8061243e)) {
        unsigned int remaining = need;
        while ((0 < (int)remaining) && (scanned < (int)(DAT_80612438 + need))) {
            scanned = scanned + 1;
            if ((unsigned int)(DAT_8061244c - 1) <= (unsigned int)DAT_80612438) {
                DAT_80612438 = 0;
                freeRun = 0;
                remaining = need;
            }
            if ((&DAT_8059d4a0)[DAT_80612438 * 2] == 1) {
                if ((freeRun < bestGapStart) && ((int)need <= freeRun)) {
                    base = DAT_80612438 - freeRun;
                    bestGapStart = freeRun;
                }
                freeRun = 0;
            }
            else {
                freeRun = freeRun + 1;
            }
            if ((&DAT_8059d4a2)[DAT_80612438 * 2] == -1) {
                if (remaining == need) {
                    runBase = DAT_80612438;
                }
                remaining = remaining - 1;
                DAT_80612438 = DAT_80612438 + 1;
            }
            else {
                DAT_80612438 = DAT_80612438 + 1;
                remaining = need;
            }
        }

        if (0 < (int)remaining) {
            if (DAT_8061244c < bestGapStart) {
                return (int)runBase;
            }
            int idx = base << 2;
            for (unsigned int i = base; runBase = base, (int)i < (int)(base + need); i = i + 1) {
                if (*(short *)((int)&DAT_8059d4a2 + idx) != -1) {
                    if (!stream_voice_page_free_one((int)*(short *)((int)&DAT_8059d4a2 + idx))) {
                        return (int)runBase;
                    }
                    DAT_80612470 = DAT_80612470 + 1;
                }
                idx = idx + 4;
            }
        }

        int fill = (runBase + need) - runBase;
        int wr = runBase << 2;
        if ((int)runBase < (int)(runBase + need)) {
            do {
                DAT_8061243c = DAT_8061243c - 1;
                *(short *)((int)&DAT_8059d4a0 + wr) = 1;
                *(short *)((int)&DAT_8059d4a2 + wr) = (short)v;
                fill = fill + -1;
                wr = wr + 4;
            } while (fill != 0);
        }
    }
    return (int)runBase;
}

// -----------------------------------------------------------------------------
// stream_voice_pages_release (0x80185f04)
// Walk the page-ring window owned by voice `v` (located from its page cursor)
// and clear each entry: reset state to 0, owner to 0xffff, bump the free count,
// and decrement the locked count for any that were locked (state 2).
// -----------------------------------------------------------------------------
void stream_voice_pages_release(int v)
{
    unsigned short f = (&DAT_80599314)[v * 0xc];
    bool loaded = !(((f & 1) == 0) || ((f & 2) == 0) || ((f & 0x10) != 0));
    if (!loaded) {
        return;
    }

    unsigned int firstPage = (unsigned int)((&DAT_8059930c)[v * 6] - DAT_80612424) >> 0xb;
    int span = DAT_8061244c - firstPage;
    unsigned int wr = (unsigned int)((&DAT_8059930c)[v * 6] - DAT_80612424) >> 9 & 0x7ffffc;
    if (DAT_8061244c <= (int)firstPage) {
        return;
    }
    bool matched = false;
    do {
        if (v == *(short *)((int)&DAT_8059d4a2 + wr)) {
            if (*(short *)((int)&DAT_8059d4a0 + wr) == 2) {
                DAT_8061243e = DAT_8061243e + -1;
            }
            matched = true;
            *(short *)((int)&DAT_8059d4a2 + wr) = (short)0xffff;
            DAT_8061243c = DAT_8061243c + 1;
            *(short *)((int)&DAT_8059d4a0 + wr) = 0;
        }
        else if (matched) {
            return;
        }
        wr = wr + 4;
        span = span + -1;
    } while (span != 0);
}

// -----------------------------------------------------------------------------
// stream_voice_pages_lock (0x80185fe8)
// Mark every page owned by voice `v` as locked (state 2) and bump the locked
// count.  Stops at the first non-owned page after the run begins.
// -----------------------------------------------------------------------------
void stream_voice_pages_lock(int v)
{
    unsigned short f = (&DAT_80599314)[v * 0xc];
    bool loaded = ((f & 1) != 0) && ((f & 2) != 0) && ((f & 0x10) == 0);
    if (!loaded) {
        return;
    }

    unsigned int firstPage = (unsigned int)((&DAT_8059930c)[v * 6] - DAT_80612424) >> 0xb;
    int span = DAT_8061244c - firstPage;
    unsigned int wr = (unsigned int)((&DAT_8059930c)[v * 6] - DAT_80612424) >> 9 & 0x7ffffc;
    if (DAT_8061244c <= (int)firstPage) {
        return;
    }
    bool matched = false;
    do {
        if (v == *(short *)((int)&DAT_8059d4a2 + wr)) {
            matched = true;
            *(short *)((int)&DAT_8059d4a0 + wr) = 2;
            DAT_8061243e = DAT_8061243e + 1;
        }
        else if (matched) {
            return;
        }
        wr = wr + 4;
        span = span + -1;
    } while (span != 0);
}

// -----------------------------------------------------------------------------
// stream_voice_pages_unlock (0x801860a8)
// Inverse of stream_voice_pages_lock: mark every page owned by voice `v` back to
// state 1 and decrement the locked count.
// -----------------------------------------------------------------------------
void stream_voice_pages_unlock(int v)
{
    unsigned short f = (&DAT_80599314)[v * 0xc];
    bool loaded = ((f & 1) != 0) && ((f & 2) != 0) && ((f & 0x10) == 0);
    if (!loaded) {
        return;
    }

    unsigned int firstPage = (unsigned int)((&DAT_8059930c)[v * 6] - DAT_80612424) >> 0xb;
    int span = DAT_8061244c - firstPage;
    unsigned int wr = (unsigned int)((&DAT_8059930c)[v * 6] - DAT_80612424) >> 9 & 0x7ffffc;
    if (DAT_8061244c <= (int)firstPage) {
        return;
    }
    bool matched = false;
    do {
        if (v == *(short *)((int)&DAT_8059d4a2 + wr)) {
            matched = true;
            *(short *)((int)&DAT_8059d4a0 + wr) = 1;
            DAT_8061243e = DAT_8061243e + -1;
        }
        else if (matched) {
            return;
        }
        wr = wr + 4;
        span = span + -1;
    } while (span != 0);
}

// -----------------------------------------------------------------------------
// stream_voice_lock_once (0x80186168)
// If the voice is loaded and not already locked (flag 4 clear), lock its pages
// and set the locked flag; return 1.  Returns 0 if already locked or not loaded.
// -----------------------------------------------------------------------------
int stream_voice_lock_once(int v)
{
    unsigned short *pf = &DAT_80599314 + v * 0xc;
    unsigned short f = *pf;

    bool loaded = ((f & 1) != 0) && ((f & 2) != 0) && ((f & 0x10) == 0);
    if (!loaded) {
        return 0;
    }
    if ((f & 4) != 0) {
        return 0;
    }
    stream_voice_pages_lock(v);
    *pf = *pf | 4;
    return 1;
}
