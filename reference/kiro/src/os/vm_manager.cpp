// ============================================================================
// Address range: 0x801845f8 - 0x80184c9f | batch_475 | Date: 2026-09-06
// Subsystem: os (Gekko MMU init / DSI exception handler / paged-window manager).
// tag os-vm.
//
// The top layer of the demand-paging virtual-memory subsystem, sitting above
// the hashed-PTE page table (batch_473) and the demand-pager (batch_474).  It
// owns MMU bring-up, the DSI (data-storage) exception path, region allocation
// inside the paged window, and the range prefetch / writeback helpers the asset
// and animation streamers use.
//
// The paged window maps a virtual range onto a backing-store window; the split
// between the two ends is:
//   DAT_806123fc  low bump cursor (grows up)     DAT_80610aa8  high guard limit (grows down)
//   DAT_806123f8  virtual->physical window bias   DAT_806123f4  window top address
//   DAT_806123d8  page window size (pages)        DAT_806123da  live page count
//   DAT_806123dc / de  active / free ring cursors  DAT_80612410  page-count bias
//   DAT_80612408  page-used bitmap base            DAT_8061240c  active-page ring base
//   DAT_80612414  page-table (PTEG array) base     DAT_80612418  page-table end
//   DAT_806123e0  pending async-load counter       DAT_806123e4/e8  pending region base/len
//
//   dsi_exception_handler   0x801845f8  DSI dispatch: page-fault vs protection/access
//   mmu_init                0x80184660  bring up page table + bitmap + DSI vector
//   mmu_flush_all           0x801847f8  write back every page + reset the window
//   vm_alloc_bump           0x80184908  32-byte-aligned bump alloc in the window
//   vm_range_prefetch       0x8018493c  async device read of a byte-range into the window
//   vm_wait_pending_loads   0x801849e0  spin until pending async loads drain
//   vm_region_alloc         0x801849f0  page-aligned region alloc (low or high end)
//   vm_range_writeback      0x80184a5c  write back + optionally lock a byte-range
//   vm_free_space           0x80184bdc  bytes free between the two window ends
//   exception_return        0x80184bec  rfi epilogue; returns saved r3:r4
//
// Structurally-faithful non-matching port.  Register-pair spawn-frame args are
// modelled as ull hi/lo split; the ROM's page-window bookkeeping globals and
// the 0x14-deep DMA request-block batching are preserved.

#include "common.h"
#include "generated_decls.h"

// ---- PPC intrinsics (permissive variadic in generated_externs.h; NOT
//      re-declared typed here, see batch_473/474) ----------------------------
//   void sync(0);  void returnFromInterrupt(msr, srr0);
//   OSReport(...) printf-style firmware report (variadic).

// ---- batch_473/474 primitives (typed in generated_decls.h) ------------------
//   int  pte_group_entry_addr(unsigned int key);
//   void page_bitmap_set(unsigned int page);  void page_bitmap_clear(unsigned int page);
//   void mmu_tlb_invalidate_all(void);
//   void pte_set_no_access(unsigned int vaddr, int pte);
//   void pte_clear_changed_bit(unsigned int vaddr, int pte);
//   void page_dma_read(void*, unsigned int, int, int, unsigned int*, unsigned char*);
//   unsigned short vsid_table_lookup(unsigned int key);
//   void vm_range_pin_pages(int base, int len);
//   void vm_page_writeback(unsigned int vpage);
//   int  vm_page_fault_handler(void);   // returns r3:r4 (see PTE_INSTALL note below)

// vm_page_fault_handler is declared void in generated_decls, but the ROM DSI
// handler consumes its r3:r4 return.  Call through a typed fn-ptr to recover it.
typedef unsigned long long (*VmFaultFn)(void);
#define VM_PAGE_FAULT ((VmFaultFn)vm_page_fault_handler)

// ---- runtime helpers ---------------------------------------------------------
extern int   FUN_803991b8(void);            // reserved-region base (page-map arena)
extern void  FUN_803991b0(void);            // arena reserve begin
extern void  FUN_803991c8(int endAddr);     // arena reserve commit
extern unsigned int FUN_80397a6c(void);     // read HID/config word (bit 0x40 = mem layout)
extern void  FUN_80399500(unsigned int addr, int len);   // DCStoreRange
extern void  FUN_803995b4(unsigned int addr, int len);   // ICInvalidateRange
extern void  FUN_803abe54(void *reqBlock, int cmd, int mode, int prio,
                          unsigned int devOffset, unsigned int memAddr,
                          int len, int callback);          // async device xfer
extern void  FUN_8039cca0(void);            // enter critical (disable ints)
extern void  FUN_8039ccc8(void);            // exit critical (enable ints)

// ---- spawn-frame register helpers --------------------------------------------
extern unsigned long long FUN_803826d4(void);
extern unsigned long long FUN_803826e8(void);
extern unsigned long long FUN_803826f0(void);
extern void               FUN_80382720(void);
extern void               FUN_80382734(void);
extern void               FUN_8038273c(void);

// ---- exception-message strings -----------------------------------------------
extern char s_____Data_Access_Exception_____Er_804126e4[];
extern char s_____Data_Protection_Exception____80412688[];

// ---- paged-window state globals ----------------------------------------------
extern unsigned char  DAT_806123d8;   // page window size (pages)
extern unsigned char  DAT_806123da;   // live page count
extern unsigned short DAT_806123dc;   // active-ring cursor (ushort slot)
extern unsigned short DAT_806123de;   // free-ring cursor (ushort slot)
extern int            DAT_806123e0;   // pending async-load counter
extern unsigned int   DAT_806123e4;   // pending-region base
extern unsigned int   DAT_806123e8;   // pending-region length
extern char           DAT_806123f0;   // async-load ready flag
extern unsigned int   DAT_806123f4;   // window top address
extern int            DAT_806123f8;   // virtual->physical window bias
extern unsigned int   DAT_806123fc;   // low bump cursor
extern int            DAT_80612408;   // page-used bitmap base
extern unsigned char  DAT_80612410;   // page-count bias
extern int            DAT_8061240c;   // active-page ring base (ushort[], stride 2)
extern unsigned short DAT_80612400;   // free/victim ring base (ushort[], stride 2)
extern int            DAT_80612414;   // page-table (PTEG array) base
extern int            DAT_80612418;   // page-table end
extern unsigned int   DAT_80610aa8;   // high guard limit
extern int            DAT_80599078;   // DMA request-block array (0x20 stride)

// -----------------------------------------------------------------------------
// dsi_exception_handler (0x801845f8)
// The Data Storage Interrupt dispatcher.  DSISR bit 0x40000000 => page fault:
// run the demand-pager (vm_page_fault_handler).  Bit 0x8000000 => data
// protection; else data access: OSReport the exception.  Always logs at the
// end.  Signature carries the exception frame's 8 fp doubles + 8 gpr words.
// -----------------------------------------------------------------------------
void dsi_exception_handler(unsigned long long f1, unsigned long long f2, unsigned long long f3,
                           unsigned long long f4, unsigned long long f5, unsigned long long f6,
                           unsigned long long f7, unsigned long long f8,
                           unsigned int dsisr, unsigned int r10, unsigned int r11,
                           unsigned int r12, unsigned int r13, unsigned int r14,
                           unsigned int r15, unsigned int r16)
{
    // On a page fault the demand-pager runs and returns the resolved r3:r4 pair;
    // on protection/access faults the exception is reported (the fp-register and
    // void-return plumbing the decompiler shows around OSReport are ABI artifacts
    // and are dropped here).
    unsigned long long result = 0;

    if ((dsisr & 0x40000000) == 0) {
        if ((dsisr & 0x8000000) == 0) {
            r12 = 0x80410000;
            OSReport(s_____Data_Access_Exception_____Er_804126e4, r10, dsisr,
                     0x80410000, r13, r14, r15, r16);
            r11 = dsisr;
        }
        else {
            r12 = 0x80410000;
            OSReport(s_____Data_Protection_Exception____80412688, r10, dsisr,
                     0x80410000, r13, r14, r15, r16);
            r11 = dsisr;
        }
    }
    else {
        result = VM_PAGE_FAULT();
    }

    os_log_formatted(f1, f2, f3, f4, f5, f6, f7, f8,
                     (unsigned int)(result >> 0x20), (unsigned int)result,
                     r11, r12, r13, r14, r15, r16);
}

// -----------------------------------------------------------------------------
// mmu_init (0x80184660)
// Bring up the demand-paging MMU.  The virtual window base:top arrives in the
// spawn-frame pair.  Carves the page-used bitmap and active-page ring out of the
// reserved arena, 64KB-aligns the PTEG page table after them, zeroes both, sets
// the window bookkeeping, invalidates the TLB, and installs the DSI exception
// handler stub (copied to vector 0x300 or the alternate high vector).  Called by
// gx_subsystem_boot.
// -----------------------------------------------------------------------------
void mmu_init(void)
{
    unsigned long long frame = FUN_803826f0();
    unsigned int windowTop = (unsigned int)(frame >> 0x20);   // r3 : window top / size hi
    unsigned int windowBase = (unsigned int)frame;            // r4 : window base

    int  arena = FUN_803991b8();
    FUN_803991b0();

    int pageCount = ((unsigned int)frame >> 0xc) - 4;
    DAT_80612408 = arena + (pageCount * 2 & 0x1fffe);         // page-used bitmap
    unsigned int bitmapBytes = (windowTop >> 0xc) + 7 & 0xfffffff8;
    int afterBitmap = DAT_80612408 + bitmapBytes;

    DAT_806123d8 = (unsigned char)pageCount;
    DAT_806123f4 = (unsigned int)frame;
    DAT_806123f8 = windowTop;
    DAT_8061240c = arena;                                     // active-page ring
    memset_wrapper(DAT_80612408, 0, bitmapBytes);

    DAT_80612414 = afterBitmap + 0xffff & 0xffff0000;         // page table (64KB aligned)
    DAT_806123fc = 0x1000000 - DAT_806123f8;                  // low bump cursor
    DAT_80612418 = DAT_80612414 + 0x10000;
    DAT_806123f0 = 1;
    FUN_803991c8(DAT_80612418 + DAT_806123f4);                // commit arena reservation
    DAT_80612410 = (unsigned char)(DAT_80612418 + 0x80000000 >> 0xc);

    mmu_tlb_invalidate_all();
    memset_wrapper(DAT_80612414, 0, 0x10000);

    // Install the DSI handler stub at the exception vector (0x300 normal, or the
    // alternate high vector when the mem-layout bit is set).
    unsigned int hid = FUN_80397a6c();
    int vec = ((hid & 0x40) == 0) ? 0x300 : -0xffd00;
    leaf_31e8(vec + 0x80000000, 0x80184ca0, 0x100);
    FUN_80399500(vec + 0x80000000, 0x100);                    // DCStoreRange
    FUN_803995b4(vec + 0x80000000, 0x100);                    // ICInvalidateRange
    sync(0);
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// mmu_flush_all (0x801847f8)
// Full VM reset: write back every currently-mapped page (both rings), invalidate
// the TLB, zero the page table, reset the ring cursors + live count, and re-clear
// the page-used bitmap up to the guard limit.  Called by gx_fifo_resize_for_region.
// -----------------------------------------------------------------------------
void mmu_flush_all(void)
{
    unsigned int n = (unsigned int)DAT_806123d8;
    if ((unsigned int)DAT_806123da < (unsigned int)DAT_806123d8) {
        n = (unsigned int)DAT_806123da;
    }
    int off = 0;
    for (int i = 0; i < (int)n; i++) {
        vm_page_writeback((unsigned int)*(unsigned short *)(DAT_8061240c + off));
        off += 2;
    }

    unsigned int size  = (unsigned int)DAT_806123d8;
    unsigned int count = (unsigned int)DAT_806123da;
    off = 0;
    for (int i = 0; i < (int)(count - size); i++) {
        vm_page_writeback((unsigned int)*(unsigned short *)(&DAT_80612400 + off));
        off += 2;
    }

    DAT_806123fc = 0x1000000 - DAT_806123f8;
    mmu_tlb_invalidate_all();
    memset_wrapper(DAT_80612414, 0, 0x10000);
    DAT_806123da = 0;

    unsigned int span = DAT_80610aa8 - DAT_806123fc;
    unsigned int fullBytes = span >> 0xc & 0xffff8;
    DAT_806123dc = 0;
    DAT_806123de = 0;
    memset_wrapper(DAT_80612408, 0, fullBytes);
    for (unsigned int p = fullBytes; (int)p < (int)(span >> 0xc); p++) {
        page_bitmap_clear(p);
    }
}

// -----------------------------------------------------------------------------
// vm_alloc_bump (0x80184908)
// 32-byte-aligned bump allocation from the low window cursor.  Returns the
// virtual address of the block (cursor - window bias).
// -----------------------------------------------------------------------------
int vm_alloc_bump(int size)
{
    unsigned int addr = DAT_806123fc + 0x1f & 0xffffffe0;
    DAT_806123fc = addr + (size + 0x1f & 0xffffffe0);
    return addr - (0x61000000 - DAT_806123f8);
}

// -----------------------------------------------------------------------------
// vm_range_prefetch (0x8018493c)
// Pin the pages of a byte-range and kick an async device read of the whole range
// into the window; bumps the pending-load counter (interrupt-guarded) so
// vm_wait_pending_loads can block on it.  Range base arrives in the spawn frame.
// Called by anim_load_descriptor.
// -----------------------------------------------------------------------------
void vm_range_prefetch(unsigned int devBase, unsigned int arg2, int len, unsigned int *reqBlock)
{
    unsigned long long frame = FUN_803826e8();
    int base = (int)(frame >> 0x20);
    unsigned int alignedLen = len + 0x1f & 0xffffffe0;

    vm_range_pin_pages(base, len);
    int bias = 0x61000000 - DAT_806123f8;

    FUN_8039cca0();                     // disable ints
    DAT_806123e0 = DAT_806123e0 + 1;    // pending-load count++
    FUN_8039ccc8();                     // enable ints

    FUN_80399500((unsigned int)frame, alignedLen);          // DCStoreRange
    FUN_803abe54(reqBlock, 0x76, 0, 1, (unsigned int)frame,
                 base + bias, alignedLen, -0x7fe7c068);      // async read + completion cb
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// vm_wait_pending_loads (0x801849e0)
// Spin until all async page-range loads have completed (counter drains to 0).
// -----------------------------------------------------------------------------
void vm_wait_pending_loads(void)
{
    do { } while (0 < DAT_806123e0);
}

// -----------------------------------------------------------------------------
// vm_region_alloc (0x801849f0)
// Page-aligned region allocation.  mode==1 grows the low bump cursor up; else it
// grows the high guard limit down.  When `record` is set, the allocated region
// is remembered as the pending region (DAT_806123e4/e8) the fault handler checks.
// -----------------------------------------------------------------------------
void vm_region_alloc(int size, int mode, int record)
{
    unsigned int alignedLen = size + 0xfff & 0xfffff000;
    unsigned int regionBase;

    if (mode == 1) {
        regionBase   = DAT_806123fc + 0xfff & 0xfffff000;
        DAT_806123fc = regionBase + alignedLen;
    }
    else {
        regionBase   = (DAT_80610aa8 & 0xfffff000) - alignedLen;
        DAT_80610aa8 = regionBase;
    }

    if (record == 0) {
        return;
    }
    DAT_806123e4 = regionBase - (0x61000000 - DAT_806123f8);
    DAT_806123e8 = alignedLen;
}

// -----------------------------------------------------------------------------
// vm_range_writeback (0x80184a5c)
// Write back (and optionally lock/no-access) every mapped page overlapping a
// byte-range.  Walks the range page-by-page, looks each up in the page map, and
// for mapped pages issues a DMA writeback through a round-robin bank of 0x14 DMA
// request blocks (DAT_80599078), waiting on the ready flag before reusing a bank.
// `lock` (param_3) additionally sets no-access + pins the page.
// -----------------------------------------------------------------------------
void vm_range_writeback(unsigned int arg1, unsigned int arg2, int lock)
{
    unsigned long long frame = FUN_803826d4();
    unsigned int vbase = (int)(frame >> 0x20) + 0x60000000;
    unsigned int remaining = (int)frame + 0xfff & 0xfffff000;
    int pageOff = vbase - (vbase & 0xfffff000);

    unsigned int chunk = 0x1000;
    if (remaining < 0x1000) {
        chunk = remaining;
    }
    chunk -= pageOff;

    int pageIdx = 0;
    int bank    = 0;
    while (0 < (int)remaining) {
        unsigned int vpage = (vbase >> 0xc) + pageIdx;
        unsigned short entry = vsid_table_lookup(vpage);
        unsigned int key = (unsigned int)entry;
        remaining -= chunk;

        if (bank == 0) {
            do { } while (DAT_806123f0 == '\0');   // wait for a free DMA bank cycle
            DAT_806123f0 = '\0';
        }

        if (entry != 0xffff) {
            int pte = pte_group_entry_addr(key);
            unsigned char last = 0;
            if ((bank == 0x13) || (remaining == 0)) {
                last = 1;
            }
            page_dma_read((void *)vpage, *(unsigned int *)(pte + 4) >> 0xc, pageOff, chunk,
                          (unsigned int *)(&DAT_80599078 + bank * 0x20),
                          (unsigned char *)(-(unsigned int)last & 0x80183f8c));
            bank++;
            if (lock != 0) {
                pte_set_no_access((key & 0x7ff) * 0x1000 + -0x60000000, pte);
            }
            pte_clear_changed_bit((key & 0x7ff) * 0x1000 + -0x60000000, pte);
            if (bank == 0x14) {
                bank = 0;
            }
        }
        if (lock != 0) {
            page_bitmap_set(vpage);
        }
        pageOff = 0;
        chunk = 0x1000;
        if ((int)remaining < 0x1000) {
            chunk = remaining;
        }
        pageIdx++;
    }

    DAT_806123e4 = 0;
    DAT_806123e8 = 0;
    FUN_80382720();
}

// -----------------------------------------------------------------------------
// vm_free_space (0x80184bdc)
// Bytes free in the window = high guard limit - low bump cursor.
// -----------------------------------------------------------------------------
int vm_free_space(void)
{
    return DAT_80610aa8 - DAT_806123fc;
}

// -----------------------------------------------------------------------------
// exception_return (0x80184bec)
// Exception epilogue: rfi back to the interrupted context (restoring SRR0 from
// the exception frame +0x84) and hand back the saved r3:r4 pair (+0xc).
// -----------------------------------------------------------------------------
unsigned long long exception_return(int frame)
{
    unsigned int msr = 0;   // in_MSR — current MSR consumed by rfi
    returnFromInterrupt(msr, *(unsigned int *)(frame + 0x84));
    return *(unsigned long long *)(frame + 0xc);
}
