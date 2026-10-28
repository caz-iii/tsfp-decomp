// Address: 0x80399588 | Name: os_locked_cache (Dolphin OS locked-cache + cache range ops) | Date: 2026-09-14T22:30:00Z
//
// Continuation of the Dolphin OS cache subsystem: the remaining data/instruction
// cache range primitives plus the Gekko locked-cache (LC) management and its
// DMA-queue loader (the LCEnable / LCDisable / LCLoadBlocks / LCLoadData set).
//
// The Gekko "locked cache" repurposes half of the L1 data cache as a fast on-chip
// SRAM window at 0xE0000000. LCEnable prefetches/zeroes the window and sets the HID2
// LCE/DMAQL feature bits; LCDisable invalidates it and clears the enable bit.
// LCLoadBlocks programs the DMAU/DMAL SPRs to move up to 0x80 (128) 32-byte cache
// lines per transfer between main memory and the LC; LCLoadData drives that in
// 0x1000-byte (0x80-block) chunks until the whole length is queued.
//
// HID2 bits used here: 0x10000000 = LCE (locked-cache enable),
//                      0x000f0000 = DMA-queue length / WPE cluster.

#include "common.h"

// ---- Ghidra cache/sync intrinsics (raw emitted helpers) ----
extern void dataCacheBlockClearToZero(uint addr);       // dcbz
extern void dataCacheBlockTouch(int addr);              // dcbt
extern void dataCacheBlockStore(int addr);              // dcbst
extern void dataCacheBlockInvalidate(int addr);         // dcbi
extern void dataCacheBlockSetToZeroLocked(int addr);    // dcbz_l
extern void instructionCacheBlockInvalidate(uint addr); // icbi
extern void instructionSynchronize(void);               // isync
extern void sync(int);                                   // sync

// ---- locked-cache DMA SPR programming (DMAU/DMAL) ----
// The original packs the transfer descriptor into the two Gekko DMA SPRs:
//   DMAU (0x39a) = (blocks[5..7] << 27) | (mem_addr & 0x0fffffff)
//   DMAL (0x39b) = (blocks[2..3] << 2)  | (lc_addr & ~0x1f) | 0x12  (LD + trigger)
// Expressed here as writes to the SPR mirrors so the packing is preserved.
extern volatile u32 os_spr_dmau;   // Gekko DMAU (SPR 0x39a)
extern volatile u32 os_spr_dmal;   // Gekko DMAL (SPR 0x39b)

// trigger low nibble: 0x12 = load (LC<-mem), 0x02 = store (mem<-LC)
static void os_lc_program_dma(uint lc_addr, uint mem_addr, int blocks, uint trigger)
{
    uint b = (uint)blocks;
    os_spr_dmau = ((b & 0x1f) << 27) | (mem_addr & 0x0fffffff);
    os_spr_dmal = ((b >> 3) << 4)    | (lc_addr & 0xffffffe0) | trigger;
}

// ---- interrupt guard used by the LC enable wrapper ----
extern int  FUN_8039cca0(void);   // OSDisableInterrupts
extern void FUN_8039ccc8(int);    // OSRestoreInterrupts

// os_dc_zero_range 0x80399588  (DCZeroRange - dcbz over each 32-byte block)
uint os_dc_zero_range(uint addr, int len)
{
    uint blocks;

    if (len == 0) {
        return addr;
    }
    blocks = (uint)(len + (addr & 0x1f) + 0x1f) >> 5;
    do {
        dataCacheBlockClearToZero(addr);
        addr += 0x20;
        blocks--;
    } while (blocks != 0);
    return addr;
}

// os_ic_invalidate_range 0x803995b4  (ICInvalidateRange - icbi over each block, sync+isync)
uint os_ic_invalidate_range(uint addr, int len)
{
    uint blocks;

    if (len == 0) {
        return addr;
    }
    blocks = (uint)(len + (addr & 0x1f) + 0x1f) >> 5;
    do {
        instructionCacheBlockInvalidate(addr);
        addr += 0x20;
        blocks--;
    } while (blocks != 0);
    sync(0);
    instructionSynchronize();
    return addr;
}

// os_enable_write_gather_hid0 0x803995e8
// Returns the HID0 image with the write-gather-pipe enable bit (0x800) set.
uint os_enable_write_gather_hid0(uint hid0)
{
    return hid0 | 0x800;
}

// os_enable_icache_hid0 0x803995f8
// isync then return the HID0 image with the instruction-cache enable bit (0x8000) set.
uint os_enable_icache_hid0(uint hid0)
{
    instructionSynchronize();
    return hid0 | 0x8000;
}

// os_lc_enable 0x8039960c  (LCEnable)
// Warms the locked-cache window: touch+store 0x400 blocks starting at 0x80000000,
// isync, then dcbz_l 0x200 blocks of the LC window at 0xE0000000, and return the
// HID2 image with the LCE + DMA-queue-length feature bits (0x100f0000) set.
u64 os_lc_enable(u64 hid2)
{
    int addr;
    int count;

    addr  = (int)0x80000000;
    count = 0x400;
    do {
        dataCacheBlockTouch(addr);
        dataCacheBlockStore(addr);
        addr += 0x20;
        count--;
    } while (count != 0);
    instructionSynchronize();

    addr  = (int)0xE0000000;
    count = 0x200;
    do {
        dataCacheBlockSetToZeroLocked(addr);
        addr += 0x20;
        count--;
    } while (count != 0);

    return hid2 | 0x100f0000ULL;
}

// os_lc_enable_locked 0x803996d8
// Enables the locked cache under an interrupt lock (disable ints, LCEnable, restore).
void os_lc_enable_locked(void)
{
    int state;

    state = FUN_8039cca0();
    os_lc_enable(0);
    FUN_8039ccc8(state);
}

// os_lc_disable 0x80399710  (LCDisable)
// Invalidates the 0x200-block LC window at 0xE0000000 and returns the HID2 image
// with the LCE bit (0x10000000) cleared.
u64 os_lc_disable(u64 hid2)
{
    int addr;
    int count;

    addr  = (int)0xE0000000;
    count = 0x200;
    do {
        dataCacheBlockInvalidate(addr);
        addr += 0x20;
        count--;
    } while (count != 0);

    return hid2 & 0xffffffffefffffffULL;
}

// os_lc_load_blocks 0x80399738  (LCLoadBlocks)
// Programs the DMAU/DMAL locked-cache DMA SPRs to transfer `blocks` 32-byte cache
// lines between main memory (mem_addr) and the LC window (lc_addr). The register
// packing is done in os_lc_program_dma (the mtspr DMAU/DMAL sequence).
void os_lc_load_blocks(uint lc_addr, uint mem_addr, int blocks)
{
    os_lc_program_dma(lc_addr, mem_addr, blocks, 0x12);   // load: LC <- main memory
}

// os_lc_store_blocks 0x8039975c  (LCStoreBlocks)
// Same DMAU/DMAL programming as LCLoadBlocks but with the store trigger (0x2):
// moves `blocks` 32-byte cache lines from the LC window back to main memory.
// (Corrected from the earlier batch_1101 mislabel "os_lc_queue_length"; the
// instruction stream is the mtspr DMAU/DMAL store pair, not a mask.)
void os_lc_store_blocks(uint lc_addr, uint mem_addr, int blocks)
{
    os_lc_program_dma(lc_addr, mem_addr, blocks, 0x2);    // store: main memory <- LC
}

// os_lc_load_data 0x80399780  (LCLoadData)
// Loads a byte range into the locked cache by rounding the length up to whole
// 32-byte blocks then issuing LCLoadBlocks in chunks of up to 0x80 blocks
// (0x1000 bytes) per transfer, stepping both LC and main-memory pointers by 0x1000.
// Returns the number of DMA queue entries used (block count rounded up to 0x80).
uint os_lc_load_data(uint lc_addr, uint mem_addr, int len)
{
    uint blocks;
    uint queued;

    blocks = (uint)(len + 0x1f) >> 5;
    queued = blocks + 0x7f;
    while (blocks != 0) {
        if (blocks < 0x80) {
            os_lc_load_blocks(lc_addr, mem_addr, (int)blocks);
            blocks = 0;
        } else {
            os_lc_load_blocks(lc_addr, mem_addr, 0);   // 0 encodes a full 0x80-block transfer
            blocks -= 0x80;
            lc_addr  += 0x1000;
            mem_addr += 0x1000;
        }
    }
    return queued >> 7;
}

// os_lc_store_data 0x8039982c  (LCStoreData)
// Store-direction mirror of os_lc_load_data: rounds the byte length up to whole
// 32-byte blocks then issues LCStoreBlocks in chunks of up to 0x80 blocks (0x1000
// bytes) per transfer, stepping the LC and main-memory pointers. Returns the
// number of DMA queue entries used (block count rounded up to 0x80).
uint os_lc_store_data(uint lc_addr, uint mem_addr, int len)
{
    uint blocks;
    uint queued;

    blocks = (uint)(len + 0x1f) >> 5;
    queued = blocks + 0x7f;
    while (blocks != 0) {
        if (blocks < 0x80) {
            os_lc_store_blocks(lc_addr, mem_addr, (int)blocks);
            blocks = 0;
        } else {
            os_lc_store_blocks(lc_addr, mem_addr, 0);   // 0 encodes a full 0x80-block transfer
            blocks -= 0x80;
            lc_addr  += 0x1000;
            mem_addr += 0x1000;
        }
    }
    return queued >> 7;
}

// os_lc_queue_wait 0x803998d8  (LCQueueWait)
// Spins until the locked-cache DMA queue depth (HID2 bits 24..27) drops to at most
// `level` outstanding entries. Reads HID2 each iteration.
extern uint os_get_hid2_raw(void);   // HID2 image (queue-depth field in bits 24..27)
void os_lc_queue_wait(int level)
{
    while (level < (int)((os_get_hid2_raw() >> 0x18) & 0xf)) {
        /* spin */
    }
}
