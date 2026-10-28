// batch_1088: the MetroTRK memory-access and exception-entry layer - the general
// guarded scalar copy, the recursive address-range validator, the debug trap, the
// SPR/segment-register save, the register-cache dirty-flag reset, the two exception
// vector entries that snapshot the full CPU context and dispatch into the monitor,
// the cache-coherent DMA read/write primitives, and the monitor-init/stop entry.
//
// Non-matching decomp: the DMA engine (FUN_803aa*), the context dispatcher
// (FUN_80396198) and continuation (FUN_80395e8c) resolve as cross-module externs;
// the PPC context save reads live registers, modelled here as function inputs so
// the save-slot layout is preserved. The two exception entries are noreturn-style
// vector handlers, so the register captures are represented as parameters/zero.
//
// Address range: 0x803950c8 - 0x80395ce7

#include "common.h"

// --- cross-module helpers ---------------------------------------------------
extern void trk_nop_392c(void);
extern void trk_memcpy_sync(int dst, int src, int n);
extern void trk_flush_cache_range(unsigned int addr, unsigned int count);
extern unsigned int trk_translate_address(unsigned int addr);   // FUN_80395e14
// PPC intrinsics used by the DMA primitives (defined in trk_lowlevel.cpp / SDK).
extern "C" void dataCacheBlockFlush(int addr);
extern "C" void dataCacheBlockInvalidate(void *addr);
extern "C" void sync(int x);
extern unsigned short FUN_803aa180(void);                 // DMA queue busy?
extern unsigned short FUN_803aa410(void);                 // DMA complete?
extern void FUN_803aa3f0(void);                           // DMA ack
extern void FUN_803aa1bc(int dir, void *buf, unsigned int addr, unsigned int len); // DMA kick
extern void leaf_3268(int dst, int src, unsigned int n);  // tail-copy helper
// Context dispatcher + continuation (defined in trk_dispatch.cpp region).
extern int  FUN_80396198(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int cause, int a10,
                         unsigned int *a11, int a12, int a13, int a14, int a15, int a16);
extern unsigned long long FUN_80395e8c(unsigned long long a1, unsigned long long a2,
                         unsigned long long a3, unsigned long long a4, unsigned long long a5,
                         unsigned long long a6, unsigned long long a7, unsigned long long a8,
                         int rc, int a10, unsigned int *a11, int a12, int a13, int a14,
                         int a15, int a16);

// --- TRK state globals ------------------------------------------------------
extern int          DAT_805f144c;      // run/stop state
extern int          DAT_805f1440;      // saved breakpoint opcode
extern unsigned int DAT_805f1930;      // cached-window base / monitor vector
// Valid-region window + permission flags used by the range validator.
extern unsigned int DAT_803e1b20;      // region start
extern unsigned int DAT_803e1b24;      // region end
extern unsigned int DAT_803e1b28;      // read-allowed flag
extern unsigned int DAT_803e1b2c;      // write-allowed flag
// Saved FP/exception context (low byte of DAT_80498010 is the fault flag).
extern unsigned int DAT_80498004;
extern unsigned int DAT_80498008;
extern unsigned int DAT_8049800c;
extern unsigned int DAT_80498010;
// Register-cache dirty flags.
extern unsigned char DAT_80497ff8;
extern unsigned char DAT_80497ff9;
// DBAT0 lower (mapping-present check in the address translator's companion).
extern unsigned int DAT_805f1690;

// Context-save array (base 0x805f1458) - GPRs, CR, SPRs. Only the slots the two
// entry points touch are declared; the SPR block is written by trk_save_sprs.
extern unsigned int  DAT_805f1458, DAT_805f145c, DAT_805f1460, DAT_805f148c;
extern unsigned int  DAT_805f1464, DAT_805f1468, DAT_805f146c, DAT_805f1470;
extern unsigned int  DAT_805f1474, DAT_805f1478, DAT_805f147c, DAT_805f1480;
extern unsigned int  DAT_805f1484, DAT_805f1488, DAT_805f1490, DAT_805f1494;
extern unsigned int  DAT_805f1498, DAT_805f149c, DAT_805f14a0, DAT_805f14a4;
extern unsigned int  DAT_805f14a8, DAT_805f14ac, DAT_805f14b0, DAT_805f14b4;
extern unsigned int  DAT_805f14b8, DAT_805f14bc, DAT_805f14c0, DAT_805f14c4;
extern unsigned int  DAT_805f14c8, DAT_805f14cc, DAT_805f14d0, DAT_805f14d4;
extern unsigned int  DAT_805f14d8, DAT_805f14dc, DAT_805f14e0;
extern unsigned int  FLOAT_8061b5e0, FLOAT_80617940;

// Segment/SPR save block written by trk_save_sprs (0x805f1600..0x805f1780).
extern unsigned int  DAT_805f1600, DAT_805f1604, DAT_805f1608, DAT_805f160c;
extern unsigned int  DAT_805f1610, DAT_805f1614, DAT_805f1618, DAT_805f161c;
extern unsigned int  DAT_805f1620, DAT_805f1624, DAT_805f1628, DAT_805f162c;
extern unsigned int  DAT_805f1630, DAT_805f1634, DAT_805f1638, DAT_805f163c;
extern unsigned int  DAT_805f1640, DAT_805f1644, DAT_805f1648, DAT_805f164c;
extern unsigned int  DAT_805f1650, DAT_805f1654, DAT_805f1658, DAT_805f165c;
extern unsigned int  DAT_805f1660, DAT_805f1664, DAT_805f1668, DAT_805f166c;
extern unsigned int  DAT_805f1670, DAT_805f1674, DAT_805f1678, DAT_805f167c;
extern unsigned int  DAT_805f1680, DAT_805f1684, DAT_805f1688, DAT_805f168c;
extern unsigned int  DAT_805f1694, DAT_805f16b4, DAT_805f16b8, DAT_805f16bc;
extern unsigned int  DAT_805f16c0, DAT_805f16c4, DAT_805f16c8, DAT_805f16cc;
extern unsigned int  DAT_805f16d0, DAT_805f16d4, DAT_805f16d8, DAT_805f16dc;
extern unsigned int  DAT_805f16e0, DAT_805f16e4, DAT_805f16e8, DAT_805f16ec;
extern unsigned int  DAT_805f16f0, DAT_805f16f4, DAT_805f16f8, DAT_805f16fc;
extern unsigned int  DAT_805f1700, DAT_805f1704, DAT_805f1708, DAT_805f170c;
extern unsigned int  DAT_805f1754, DAT_805f1758, DAT_805f175c, DAT_805f1760;
extern unsigned int  DAT_805f1764, DAT_805f1768, DAT_805f176c, DAT_805f1770;
extern unsigned int  DAT_805f1774, DAT_805f1778, DAT_805f177c, DAT_805f1780;

// ---------------------------------------------------------------------------
// trk_validate_range (0x80395214) - recursively verify that [start, start+len) lies
// within the allowed target region [DAT_803e1b20, DAT_803e1b24] with the matching
// read (mode 0) or write (mode 1) permission. If the request straddles the region
// boundary it is split and each sub-range re-validated. Returns 0 when the whole
// range is permitted, else 0x700 (access denied).
int trk_validate_range(unsigned int start, int len, int mode)
{
    int rc;
    unsigned int last = (len + start) - 1;

    rc = 0x700;
    if (last < start) {
        rc = 0x700;
    } else if (start <= DAT_803e1b24 && DAT_803e1b20 <= last) {
        if ((mode == 0 && DAT_803e1b28 == 0) || (mode == 1 && DAT_803e1b2c == 0)) {
            rc = 0x700;
        } else {
            rc = 0;
            if (start < DAT_803e1b20) {
                rc = 0x700;
                unsigned int sub = DAT_803e1b20 - 1;
                if (start <= sub && start <= DAT_803e1b24 && DAT_803e1b20 <= sub) {
                    if ((mode == 0 && DAT_803e1b28 == 0) || (mode == 1 && DAT_803e1b2c == 0)) {
                        rc = 0x700;
                    } else {
                        rc = 0;
                        if (start < DAT_803e1b20) {
                            rc = trk_validate_range(start, DAT_803e1b20 - start, mode);
                        }
                        if (rc == 0 && DAT_803e1b24 < sub) {
                            rc = trk_validate_range(DAT_803e1b24, sub - DAT_803e1b24, mode);
                        }
                    }
                }
            }
            if (rc == 0 && DAT_803e1b24 < last) {
                rc = 0x700;
                unsigned int last2 = (len + start) - 2;
                if (DAT_803e1b24 <= last2 && DAT_803e1b20 <= last2) {
                    if ((mode == 0 && DAT_803e1b28 == 0) || (mode == 1 && DAT_803e1b2c == 0)) {
                        rc = 0x700;
                    } else {
                        rc = 0;
                        if (DAT_803e1b24 < DAT_803e1b20) {
                            rc = trk_validate_range(DAT_803e1b24, DAT_803e1b20 - DAT_803e1b24, mode);
                        }
                        if (rc == 0 && DAT_803e1b24 < last2) {
                            rc = trk_validate_range(DAT_803e1b24, last2 - DAT_803e1b24, mode);
                        }
                    }
                }
            }
        }
    }
    return rc;
}

// ---------------------------------------------------------------------------
// trk_access_scalar (0x803950c8) - the general guarded target memory copy used by
// trk_read_u32 and the memory commands. It saves the FP/exception context, clears
// the fault flag, translates the address, validates the range for the direction,
// and on success copies via the synchronised memcpy (flushing the cache for a write
// so the code stream sees it). Returns 0x702 with zero count on a fault.
int trk_access_scalar(int host_buf, unsigned int addr, int *io_size, int a4, int is_read)
{
    unsigned int saved0 = DAT_80498004;
    unsigned int saved1 = DAT_80498008;
    unsigned int saved2 = DAT_8049800c;
    unsigned int saved3 = DAT_80498010;
    unsigned int taddr;
    unsigned int clz;
    int rc;

    (void)a4;
    DAT_80498010 = DAT_80498010 & 0xffffff00;    // clear fault flag
    taddr = trk_translate_address(addr);

    // countLeadingZeros(is_read) >> 5 is the 0/1 direction flag (1 only if is_read==0)
    clz = 0;
    { unsigned int m = (unsigned int)is_read;
      while (clz < 32 && (m & 0x80000000) == 0) { clz++; m <<= 1; } }
    rc = trk_validate_range(taddr, *io_size, clz >> 5);
    if (rc == 0) {
        trk_nop_392c();
        if (is_read == 0) {
            trk_memcpy_sync(taddr, host_buf, *io_size);
            trk_flush_cache_range(taddr, *io_size);
            if (addr != taddr) {
                trk_flush_cache_range(addr, *io_size);
            }
        } else {
            trk_memcpy_sync(host_buf, taddr, *io_size);
        }
    } else {
        *io_size = 0;
    }
    if ((DAT_80498010 & 0xff) != 0) {
        rc = 0x702;
        *io_size = 0;
    }
    DAT_80498004 = saved0;
    DAT_80498008 = saved1;
    DAT_8049800c = saved2;
    DAT_80498010 = saved3;
    return rc;
}

// ---------------------------------------------------------------------------
// trk_trap (0x803954c0) - issue a PPC "trap always" (tw 0x1f) to force a program
// exception, used to plant/trigger a breakpoint.
void trk_trap(void)
{
    // tw 0x1f, r0, r0
    __debugbreak();
}

// ---------------------------------------------------------------------------
// trk_save_sprs (0x803954e0) - snapshot the PPC segment registers and special-
// purpose registers into the register cache (base 0x805f1600) on monitor entry:
// SR0..SR15, TBL/TBU, HID0/HID1, SRR1, PVR, the eight BAT pairs, SDR1, DAR, DSISR,
// the four SPRGs, IABR/EAR/DABR, the performance-monitor SPRs, and the thermal/
// L2/ICTC control registers, zeroing the reserved slots.
void trk_save_sprs(void)
{
    // Live SPR/SR reads are modelled as zero-initialised captures; the point of the
    // decomp is the save-slot layout, which is preserved below.
    unsigned int z = 0;
    DAT_805f1600 = z; DAT_805f1604 = z; DAT_805f1608 = z; DAT_805f160c = z;
    DAT_805f1610 = z; DAT_805f1614 = z; DAT_805f1618 = z; DAT_805f161c = z;
    DAT_805f1620 = z; DAT_805f1624 = z; DAT_805f1628 = z; DAT_805f162c = z;
    DAT_805f1630 = z; DAT_805f1634 = z; DAT_805f1638 = z; DAT_805f163c = z;
    DAT_805f1640 = z; DAT_805f1644 = z;                       // TBL, TBU
    DAT_805f1648 = z; DAT_805f164c = z;                       // HID0, HID1
    DAT_805f1650 = z; DAT_805f1654 = z;                       // SRR1, PVR
    DAT_805f1658 = z; DAT_805f165c = z; DAT_805f1660 = z; DAT_805f1664 = z;
    DAT_805f1668 = z; DAT_805f166c = z; DAT_805f1670 = z; DAT_805f1674 = z;
    DAT_805f1678 = z; DAT_805f167c = z; DAT_805f1680 = z; DAT_805f1684 = z;
    DAT_805f1688 = z; DAT_805f168c = z; DAT_805f1690 = z; DAT_805f1694 = z;
    DAT_805f16b4 = z; DAT_805f16b8 = z; DAT_805f16bc = z;     // SDR1, DAR, DSISR
    DAT_805f16c0 = z; DAT_805f16c4 = z; DAT_805f16c8 = z; DAT_805f16cc = z; // SPRG0..3
    DAT_805f16d0 = 0;
    DAT_805f16d4 = z; DAT_805f16d8 = z;                       // IABR, EAR
    DAT_805f1754 = 0; DAT_805f1758 = 0; DAT_805f175c = 0; DAT_805f1760 = 0;
    DAT_805f1764 = 0; DAT_805f1768 = 0; DAT_805f176c = 0; DAT_805f1770 = 0;
    DAT_805f1774 = z; DAT_805f1778 = z; DAT_805f177c = z; DAT_805f1780 = z; // HID2,WPAR,DMAU,DMAL
    DAT_805f16dc = z;                                         // DABR
    DAT_805f16e0 = z; DAT_805f16e4 = z; DAT_805f16e8 = z; DAT_805f16ec = z; // PMC1..4
    DAT_805f16f0 = z; DAT_805f16f4 = z; DAT_805f16f8 = z;     // SIA, MMCR0, MMCR1
    DAT_805f16fc = z; DAT_805f1700 = z; DAT_805f1704 = z;     // THRM1..3
    DAT_805f1708 = z; DAT_805f170c = z;                       // ICTC, L2CR
}

// ---------------------------------------------------------------------------
// trk_clear_reg_dirty (0x80395698) - reset the two register-cache dirty flags.
void trk_clear_reg_dirty(void)
{
    DAT_80497ff8 = 0;
    DAT_80497ff9 = 0;
}

// ---------------------------------------------------------------------------
// Shared context-save + dispatch used by both exception vectors. Captures the GPRs,
// CR and the request words into the save array, snapshots the SPRs, and dispatches
// with the given cause; a dispatcher result of 1 means "resume" (return the saved
// resume word), otherwise the continuation builds the reply.
static unsigned long long trk_exception_common(int cause,
        unsigned long long a2, unsigned long long a3, unsigned long long a4,
        unsigned long long a5, unsigned long long a6, unsigned long long a7,
        unsigned long long a8, unsigned int *req_buf, int handle, int req_addr,
        int req13, int req14, int req15, int req16)
{
    DAT_805f1460 = FLOAT_8061b5e0;
    DAT_805f148c = FLOAT_80617940;
    DAT_805f14e0 = 0;                          // packed CR (live read in the original)
    DAT_805f1458 = 0;                          // r0
    DAT_805f145c = 0;                          // r1 (sp)
    DAT_805f1464 = cause;
    DAT_805f1468 = (unsigned int)handle;
    DAT_805f146c = (unsigned int)(uintptr_t)req_buf;
    DAT_805f1470 = (unsigned int)req_addr;
    DAT_805f1474 = (unsigned int)req13;
    DAT_805f1478 = (unsigned int)req14;
    DAT_805f147c = (unsigned int)req15;
    DAT_805f1480 = (unsigned int)req16;
    DAT_805f1484 = 0; DAT_805f1488 = 0;
    DAT_805f1490 = 0; DAT_805f1494 = 0; DAT_805f1498 = 0; DAT_805f149c = 0;
    DAT_805f14a0 = 0; DAT_805f14a4 = 0; DAT_805f14a8 = 0; DAT_805f14ac = 0;
    DAT_805f14b0 = 0; DAT_805f14b4 = 0; DAT_805f14b8 = 0; DAT_805f14bc = 0;
    DAT_805f14c0 = 0; DAT_805f14c4 = 0; DAT_805f14c8 = 0; DAT_805f14cc = 0;
    DAT_805f14d0 = 0; DAT_805f14d4 = 0; DAT_805f14d8 = 0; DAT_805f14dc = 0;

    trk_save_sprs();

    int rc = FUN_80396198(0, a2, a3, a4, a5, a6, a7, a8, cause, DAT_805f1468,
                          &DAT_805f146c, DAT_805f1470, DAT_805f1474, DAT_805f1478,
                          DAT_805f147c, DAT_805f1480);
    if (rc == 1) {
        return (unsigned long long)DAT_805f14dc;   // resume word
    }
    return FUN_80395e8c(0, a2, a3, a4, a5, a6, a7, a8, rc, DAT_805f1468,
                        &DAT_805f146c, DAT_805f1470, DAT_805f1474, DAT_805f1478,
                        DAT_805f147c, DAT_805f1480);
}

// trk_exception_dispatch (0x80395850) - primary exception vector entry: saves the
// interrupted CPU context and dispatches with the cause supplied in the request slot.
unsigned long long trk_exception_dispatch(unsigned long long a1, unsigned long long a2,
        unsigned long long a3, unsigned long long a4, unsigned long long a5,
        unsigned long long a6, unsigned long long a7, unsigned long long a8,
        int cause, int handle, unsigned int *req_buf, int req_addr,
        int req13, int req14, int req15, int req16)
{
    (void)a1;
    return trk_exception_common(cause, a2, a3, a4, a5, a6, a7, a8,
                                req_buf, handle, req_addr, req13, req14, req15, req16);
}

// trk_exception_dispatch_2 (0x803958e8) - secondary exception vector entry (cause 2,
// e.g. a data/instruction storage fault): identical save/dispatch with a fixed cause.
unsigned long long trk_exception_dispatch_2(unsigned long long a1, unsigned long long a2,
        unsigned long long a3, unsigned long long a4, unsigned long long a5,
        unsigned long long a6, unsigned long long a7, unsigned long long a8,
        int cause, int handle, unsigned int *req_buf, int req_addr,
        int req13, int req14, int req15, int req16)
{
    (void)a1; (void)cause;
    return trk_exception_common(2, a2, a3, a4, a5, a6, a7, a8,
                                req_buf, handle, req_addr, req13, req14, req15, req16);
}

// ---------------------------------------------------------------------------
// trk_read_target_memory (0x8039597c) - copy a block FROM target memory into a
// staging buffer using the AR/DMA engine. It first flushes the source range from
// the data cache in 0x100/0x20 strides, waits for the DMA queue to drain, DMAs any
// unaligned head/tail 0x20-byte chunk through a stack bounce buffer (patching the
// straddling bytes back with leaf_3268), then DMAs the aligned body. Sizes below
// 0x4000 or ranges past 0x8000000 are rejected (handled elsewhere as uncached).
void trk_read_target_memory(int dst, unsigned int size, int *count)
{
    unsigned int aligned_addr;
    unsigned int body_len;
    unsigned int nblk32;
    unsigned short busy, done;
    int off;
    unsigned int tail_addr;
    unsigned int tail;
    unsigned char bounce[60];

    if (size < 0x4000) return;
    if (0x8000000 < size + (unsigned int)*count) return;

    aligned_addr = size & 0xffffffe0;
    off = 0;
    body_len = (*count + (size & 0x1f) + 0x1f) & 0xffffffe0;
    nblk32 = (body_len + 0x1f) >> 5;
    if (body_len != 0) {
        unsigned int nblk256 = (body_len + 0x1f) >> 8;
        if (nblk256 != 0) {
            do {
                dataCacheBlockFlush(off + dst);
                dataCacheBlockFlush(off + 0x20 + dst);
                dataCacheBlockFlush(off + 0x40 + dst);
                dataCacheBlockFlush(off + 0x60 + dst);
                dataCacheBlockFlush(off + 0x80 + dst);
                dataCacheBlockFlush(off + 0xa0 + dst);
                dataCacheBlockFlush(off + 0xc0 + dst);
                dataCacheBlockFlush(off + 0xe0 + dst);
                off = off + 0x100;
                nblk256 = nblk256 - 1;
            } while (nblk256 != 0);
            nblk32 = nblk32 & 7;
            if (nblk32 == 0) goto body;
        }
        do {
            dataCacheBlockFlush(off + dst);
            off = off + 0x20;
            nblk32 = nblk32 - 1;
        } while (nblk32 != 0);
    }
body:
    do { busy = FUN_803aa180(); } while (busy != 0);
    done = FUN_803aa410();
    {
        unsigned int last_aligned = 0x8000000;
        if ((size & 0x1f) != 0) {
            dataCacheBlockInvalidate(bounce);
            FUN_803aa3f0();
            FUN_803aa1bc(1, bounce, aligned_addr, 0x20);
            do { done = FUN_803aa410(); } while (done == 0);
            leaf_3268(dst, (int)bounce, size & 0x1f);
            dataCacheBlockFlush(dst);
            last_aligned = aligned_addr;
        }
        tail_addr = size + (unsigned int)*count;
        tail = tail_addr & 0x1f;
        if (tail != 0) {
            if ((tail_addr & 0xffffffe0) != last_aligned) {
                dataCacheBlockInvalidate(bounce);
                FUN_803aa3f0();
                FUN_803aa1bc(1, bounce, tail_addr & 0xffffffe0, 0x20);
                do { done = FUN_803aa410(); } while (done == 0);
            }
            leaf_3268(dst + tail_addr, (int)(bounce + tail), 0x20 - tail);
            dataCacheBlockFlush(dst + tail_addr);
        }
        sync(0);
        FUN_803aa3f0();
        FUN_803aa1bc(0, (void *)(intptr_t)dst, aligned_addr, body_len);
        if (done == 0) {
            do { done = FUN_803aa410(); } while (done == 0);
            FUN_803aa3f0();
        }
    }
}

// ---------------------------------------------------------------------------
// trk_write_target_memory (0x80395b68) - copy a block TO target memory via the DMA
// engine. Invalidates the destination range from the data cache in strides, waits
// for the queue to drain, then DMAs the whole aligned block out. Same 0x4000/
// 0x8000000 bounds guard as the read primitive.
void trk_write_target_memory(int src, unsigned int size, int *count)
{
    unsigned int body_len;
    unsigned int nblk32;
    unsigned short busy, done;
    int off;

    if (size < 0x4000) return;
    if (0x8000000 < size + (unsigned int)*count) return;

    off = 0;
    body_len = (*count + (size & 0x1f) + 0x1f) & 0xffffffe0;
    nblk32 = (body_len + 0x1f) >> 5;
    if (body_len != 0) {
        unsigned int nblk256 = (body_len + 0x1f) >> 8;
        if (nblk256 != 0) {
            do {
                dataCacheBlockInvalidate((void *)(intptr_t)(off + src));
                dataCacheBlockInvalidate((void *)(intptr_t)(off + 0x20 + src));
                dataCacheBlockInvalidate((void *)(intptr_t)(off + 0x40 + src));
                dataCacheBlockInvalidate((void *)(intptr_t)(off + 0x60 + src));
                dataCacheBlockInvalidate((void *)(intptr_t)(off + 0x80 + src));
                dataCacheBlockInvalidate((void *)(intptr_t)(off + 0xa0 + src));
                dataCacheBlockInvalidate((void *)(intptr_t)(off + 0xc0 + src));
                dataCacheBlockInvalidate((void *)(intptr_t)(off + 0xe0 + src));
                off = off + 0x100;
                nblk256 = nblk256 - 1;
            } while (nblk256 != 0);
            nblk32 = nblk32 & 7;
            if (nblk32 == 0) goto body;
        }
        do {
            dataCacheBlockInvalidate((void *)(intptr_t)(off + src));
            off = off + 0x20;
            nblk32 = nblk32 - 1;
        } while (nblk32 != 0);
    }
body:
    do { busy = FUN_803aa180(); } while (busy != 0);
    done = FUN_803aa410();
    FUN_803aa3f0();
    FUN_803aa1bc(1, (void *)(intptr_t)src, size & 0xffffffe0, body_len);
    do { done = FUN_803aa410(); } while (done == 0);
    if (busy == 0) {
        FUN_803aa3f0();
    }
}

// ---------------------------------------------------------------------------
// trk_monitor_init (0x80395c9c) - one-time monitor setup from trk_init: mark the
// target stopped, capture the current no-op-hook value as the saved breakpoint
// opcode, and seed the monitor address window base (0xe0000000). Returns 0.
int trk_monitor_init(void)
{
    DAT_805f144c = 1;
    trk_nop_392c();                    // hook (returns r3; captured below as the saved opcode)
    DAT_805f1440 = 0;
    DAT_805f1930 = 0xe0000000;
    return 0;
}
