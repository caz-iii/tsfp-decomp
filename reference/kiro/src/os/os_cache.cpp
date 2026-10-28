// Address: 0x80399490 | Name: os_cache (Dolphin OS cache range ops) | Date: 2026-09-14T22:10:00Z
//
// Dolphin OS data-cache range maintenance primitives (the OSCache set) plus the
// HID0 write-gather / locked-cache feature-enable helper.
//
// Each range op walks the target region one 32-byte cache block at a time and
// issues the appropriate PowerPC cache instruction:
//   - DCInvalidateRange  : dcbi  (discard, no writeback)   - no trailing sync
//   - DCFlushRange       : dcbf  (writeback + invalidate)  - trailing sync
//   - DCStoreRange       : dcbst (writeback, keep valid)   - trailing sync
//   - DCFlushRangeNoSync : dcbf                            - no trailing sync
//   - DCStoreRangeNoSync : dcbst                           - no trailing sync
// The block count is derived from the start-address misalignment plus the length,
// rounded up to whole 32-byte blocks: (len + (addr & 0x1f) + 0x1f) >> 5.
// All return the address just past the last block touched.

#include "common.h"

// ---- Ghidra intrinsic cache ops (raw dcbX helpers emitted by the decompiler) ----
extern void dataCacheBlockInvalidate(uint addr);   // dcbi
extern void dataCacheBlockFlush(uint addr);        // dcbf
extern void dataCacheBlockStore(uint addr);        // dcbst
extern void sync(int);                             // sync / eieio guard

// os_enable_locked_cache_hid0 0x80399490
// Emits a sync then returns the HID0 value with the 0x4000 feature bit (DCFA /
// locked-cache enable) set for the caller to write back into HID0. The incoming
// HID0 image arrives in the return register per the SPR-accessor calling pattern.
uint os_enable_locked_cache_hid0(uint hid0)
{
    sync(0);
    return hid0 | 0x4000;
}

// os_dc_invalidate_range 0x803994a4  (DCInvalidateRange)
uint os_dc_invalidate_range(uint addr, int len)
{
    uint blocks;

    if (len == 0) {
        return addr;
    }
    blocks = (uint)(len + (addr & 0x1f) + 0x1f) >> 5;
    do {
        dataCacheBlockInvalidate(addr);
        addr += 0x20;
        blocks--;
    } while (blocks != 0);
    return addr;
}

// os_dc_flush_range 0x803994d0  (DCFlushRange - writeback+invalidate, then sync)
uint os_dc_flush_range(uint addr, int len)
{
    uint blocks;

    if (len == 0) {
        return addr;
    }
    blocks = (uint)(len + (addr & 0x1f) + 0x1f) >> 5;
    do {
        dataCacheBlockFlush(addr);
        addr += 0x20;
        blocks--;
    } while (blocks != 0);
    sync(0);
    return addr;
}

// os_dc_store_range 0x80399500  (DCStoreRange - writeback keep-valid, then sync)
uint os_dc_store_range(uint addr, int len)
{
    uint blocks;

    if (len == 0) {
        return addr;
    }
    blocks = (uint)(len + (addr & 0x1f) + 0x1f) >> 5;
    do {
        dataCacheBlockStore(addr);
        addr += 0x20;
        blocks--;
    } while (blocks != 0);
    sync(0);
    return addr;
}

// os_dc_flush_range_no_sync 0x80399530  (DCFlushRangeNoSync)
uint os_dc_flush_range_no_sync(uint addr, int len)
{
    uint blocks;

    if (len == 0) {
        return addr;
    }
    blocks = (uint)(len + (addr & 0x1f) + 0x1f) >> 5;
    do {
        dataCacheBlockFlush(addr);
        addr += 0x20;
        blocks--;
    } while (blocks != 0);
    return addr;
}

// os_dc_store_range_no_sync 0x8039955c  (DCStoreRangeNoSync)
uint os_dc_store_range_no_sync(uint addr, int len)
{
    uint blocks;

    if (len == 0) {
        return addr;
    }
    blocks = (uint)(len + (addr & 0x1f) + 0x1f) >> 5;
    do {
        dataCacheBlockStore(addr);
        addr += 0x20;
        blocks--;
    } while (blocks != 0);
    return addr;
}
