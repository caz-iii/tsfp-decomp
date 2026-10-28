// ============================================================================
// Address range: 0x80183f20 - 0x801845f7 | batch_474 | Date: 2026-09-06
// Subsystem: os (GameCube Gekko demand-paging / page-fault handler).
// tag os-paging.
//
// The demand-paging layer that sits on top of the batch_473 hashed-PTE page
// table.  When the MMU faults on an unmapped page the fault dispatcher
// (FUN_801845f8) calls vm_page_fault_handler, which runs a FIFO/LRU victim
// scan over two page rings, writes the victim back to backing store if dirty,
// unmaps it, installs a fresh PTE for the faulting page, and DMA-reads the new
// page contents in.  The page contents are streamed to/from the aliased
// physical window at 0xa0000000 (virtual 0x80000000 + 0x60000000) via the
// device transfer helper (FUN_803abe54, an ARQ/DVD-style async read).
//
//   pte_set_no_access       0x80183f20  set PTE PP=3 (no-access) + tlbie
//   pte_clear_changed_bit   0x80183f40  clear PTE changed/W byte + tlbie
//   ring_index_advance      0x80183f60  ++ushort with wrap at limit (generic)
//   os_log_formatted        0x80183fa8  printf-style OS log wrapper
//   vsid_table_lookup       0x80183fe0  find page-map entry by low-11-bit key
//   page_dma_write          0x8018406c  DMA a page out to backing store
//   page_dma_read           0x801840f4  DMA a page in (async or spin-wait)
//   vm_page_writeback       0x801841b8  write back one page if dirty
//   vm_range_pin_pages      0x8018423c  mark a byte-range's pages used
//   vm_page_fault_handler   0x801842b0  demand-pager fault handler (LRU evict + map-in)
//
// Structurally-faithful non-matching port.  The two page rings and their
// cursors are the ROM's LRU bookkeeping:
//   DAT_8061240c  active-page ring base (mapped pages, stride 2)
//   DAT_80612400  free/victim-page ring base (stride 2)
//   DAT_806123dc  active-ring cursor      DAT_806123de  free-ring cursor
//   DAT_806123d8  active-ring size        DAT_806123da  live page count
// PTE-slot packing (slot<<12 | vpage) matches the ROM layout exactly.

#include "common.h"
#include "generated_decls.h"

// ---- PPC MMU intrinsics (Ghidra pseudo-ops, permissive variadic in
//      generated_externs.h — do NOT re-declare typed, see batch_473) ----------
//   void TLBInvalidateEntry(ea, 0);  void sync(0);

// ---- batch_473 page-table primitives (typed in generated_decls.h) -----------
//   int  pte_group_entry_addr(unsigned int key);
//   void pte_unmap(unsigned int vaddr, unsigned int *pte);
//   void pte_clear_ref_bit(unsigned int vaddr, int pte);
//   void page_bitmap_set(unsigned int page);
//
// pte_install is declared void in generated_decls.h, but the ROM returns the
// installed PTE slot index in r3.  Call it through a correctly-typed function
// pointer to recover the return without a conflicting redeclaration.
typedef short (*PteInstallFn)(void);
#define PTE_INSTALL ((PteInstallFn)pte_install)

// ---- device / cache runtime helpers -----------------------------------------
extern void  FUN_803994a4(unsigned int addr, int len);   // DCFlushRange
extern void  FUN_80399500(unsigned int addr, int len);   // DCStoreRange
extern void  FUN_803abe54(void *reqBlock, int cmd, int mode, int prio,
                          unsigned int devOffset, unsigned int memAddr,
                          int len, int callback);          // async device xfer (ARQ/DVD)
extern void *FUN_80399e88(void);                          // acquire log line buffer
extern void  FUN_8039a104(unsigned long long, unsigned long long, unsigned long long,
                          unsigned long long, unsigned long long, unsigned long long,
                          unsigned long long, unsigned long long, void *,
                          unsigned int, unsigned int, unsigned int, unsigned int,
                          unsigned int, unsigned int, unsigned int);  // vformat
extern void  FUN_8018c1d0(void);                          // flush log line
extern void  FUN_80397aac(void);                          // log post-hook

// ---- interrupt-disable / re-enable around the critical fault section ---------
extern void  FUN_8039f35c(void);
extern void  FUN_8039ccb4(void);
extern void  FUN_8039ccc8(void);
extern void  FUN_8039f39c(void);
extern void  FUN_80397a6c(void);
extern void  FUN_80397a74(void);
extern void  FUN_80184bec(int arg);

// ---- spawn-frame register helpers --------------------------------------------
extern unsigned long long FUN_803826dc(void);
extern unsigned long long FUN_803826e8(void);
extern unsigned long long FUN_803826f0(void);
extern void               FUN_80382728(void);
extern void               FUN_80382734(void);
extern void               FUN_8038273c(void);

// ---- completion-callback stub used by the synchronous DMA path ---------------
// The no-op device-completion callback lives in the code gap at 0x80183f80.
// Referenced by absolute address to avoid colliding with the permissive
// variadic auto-extern generated for the label symbol.
#define LAB_80183f80_ADDR ((unsigned char *)0x80183f80)

// ---- paging state globals ----------------------------------------------------
extern unsigned char  DAT_806123d8;   // active-ring size (page window)
extern unsigned char  DAT_806123da;   // live page count
extern unsigned short DAT_806123dc;   // active-ring cursor (ushort slot)
extern unsigned short DAT_806123de;   // free-ring cursor (ushort slot)
extern unsigned char  DAT_80612410;   // page-count bias
extern int            DAT_8061240c;   // active-page ring base (ushort[], stride 2)
extern unsigned short DAT_80612400;   // free/victim ring base (ushort[], stride 2)
extern unsigned int   DAT_806123e4;   // pending-region base
extern unsigned int   DAT_806123e8;   // pending-region length
extern unsigned int   DAT_806123f4;   // window top address
extern int            DAT_806123f8;   // virtual->physical window bias
extern int            DAT_806123ec;   // log-suppress flag
extern unsigned int   DAT_80610aa8;   // low-mapped guard limit

// -----------------------------------------------------------------------------
// pte_set_no_access (0x80183f20)
// Set the PTE page-protection bits (PP, low 2 bits of byte +7) to 3 (no
// access), then invalidate the TLB entry.
// -----------------------------------------------------------------------------
void pte_set_no_access(unsigned int vaddr, int pte)
{
    *(unsigned char *)(pte + 7) = *(unsigned char *)(pte + 7) & 0xfc | 3;
    sync(0);
    TLBInvalidateEntry(vaddr, 0);
    sync(0);
}

// -----------------------------------------------------------------------------
// pte_clear_changed_bit (0x80183f40)
// Clear the PTE changed/W bit (high bit of byte +7), then TLB-invalidate.
// -----------------------------------------------------------------------------
void pte_clear_changed_bit(unsigned int vaddr, int pte)
{
    *(unsigned char *)(pte + 7) = *(unsigned char *)(pte + 7) & 0x7f;
    sync(0);
    TLBInvalidateEntry(vaddr, 0);
    sync(0);
}

// -----------------------------------------------------------------------------
// ring_index_advance (0x80183f60)
// Increment a 16-bit ring cursor, wrapping back to 0 when it reaches `limit`.
// Generic helper used for both page rings.
// -----------------------------------------------------------------------------
void ring_index_advance(unsigned short *cursor, unsigned int limit)
{
    unsigned int next = *cursor + 1;
    if (next == (limit & 0xffff)) {
        next = 0;
    }
    *cursor = (unsigned short)next;
}

// -----------------------------------------------------------------------------
// os_log_formatted (0x80183fa8)
// printf-style OS log: acquire a line buffer, format into it (8 varargs pairs),
// flush the line, and run the post-hook unless logging is suppressed.
// -----------------------------------------------------------------------------
void os_log_formatted(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                      unsigned long long a4, unsigned long long a5, unsigned long long a6,
                      unsigned long long a7, unsigned long long a8,
                      unsigned int v9, unsigned int v10, unsigned int v11, unsigned int v12,
                      unsigned int v13, unsigned int v14, unsigned int v15, unsigned int v16)
{
    void *buf = FUN_80399e88();
    FUN_8039a104(a1, a2, a3, a4, a5, a6, a7, a8, buf,
                 v10, v11, v12, v13, v14, v15, v16);
    FUN_8018c1d0();
    if (DAT_806123ec == 0) {
        FUN_80397aac();
    }
}

// -----------------------------------------------------------------------------
// vsid_table_lookup (0x80183fe0)
// Find the page-map entry whose low 11 bits match `key`.  Searches the active
// ring first (up to min(size,count) entries), then the wrap-around remainder in
// the free ring.  Returns the full 16-bit entry, or 0xffff if not found.
// -----------------------------------------------------------------------------
unsigned short vsid_table_lookup(unsigned int key)
{
    unsigned int size  = (unsigned int)DAT_806123d8;
    unsigned int count = size;
    if (DAT_806123da < size) {
        count = (unsigned int)DAT_806123da;
    }

    int off = 0;
    for (unsigned int i = count; i != 0; i--) {
        if (key == (*(unsigned short *)(DAT_8061240c + off) & 0x7ff)) {
            return *(unsigned short *)(DAT_8061240c + off);
        }
        off += 2;
    }

    int rem = count - size;
    off = 0;
    if (0 < rem) {
        do {
            if (key == (*(unsigned short *)(&DAT_80612400 + off) & 0x7ff)) {
                return *(unsigned short *)(&DAT_80612400 + off);
            }
            off += 2;
            rem -= 1;
        } while (rem != 0);
    }
    return 0xffff;
}

// -----------------------------------------------------------------------------
// page_dma_write (0x8018406c)
// Write one 0x1000-byte page out to backing store.  The virtual page arrives in
// the spawn-frame pair; the physical staging address is physPage*0x1000 +
// 0x80000000.  Flushes the page from dcache, then issues the device write.
// -----------------------------------------------------------------------------
void page_dma_write(void *reqBlock, unsigned int vpage, int physPage)
{
    unsigned long long frame = FUN_803826f0();
    unsigned int *req = (unsigned int *)(unsigned int)(frame >> 0x20);
    int          bias = 0x61000000 - DAT_806123f8;
    unsigned int phys = physPage * 0x1000 + 0x80000000;

    *(unsigned char *)(req + 8) = 0;
    FUN_803994a4(phys, 0x1000);                 // DCFlushRange
    FUN_803abe54(req, 0x76, 1, 1,
                 (int)frame + bias, phys, 0x1000, -0x7fe7c080);
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// page_dma_read (0x801840f4)
// Read `len` bytes of a page window in from backing store.  If no async request
// block / callback is supplied, use the stack request block + the no-op
// completion callback and spin-wait for completion.
// -----------------------------------------------------------------------------
void page_dma_read(void *reqSlot, unsigned int devBase, int devOffset, int len,
                   unsigned int *reqBlock, unsigned char *callback)
{
    unsigned int stackReq[8];
    volatile char done;

    unsigned long long frame = FUN_803826e8();
    int  bias = 0x1000000 - DAT_806123f8;
    unsigned int phys = (int)frame * 0x1000 + devOffset + -0x80000000;

    FUN_80399500(phys, len);                    // DCStoreRange (writeback+invalidate)

    unsigned int *req = reqBlock;
    if (reqBlock == (unsigned int *)0) {
        done     = '\0';
        callback = LAB_80183f80_ADDR;
        req      = stackReq;
    }

    FUN_803abe54(req, 0x76, 0, 1, phys,
                 (int)(frame >> 0x20) * 0x1000 + bias + devOffset,
                 len, (int)callback);

    if (reqBlock == (unsigned int *)0) {
        do { } while (done == '\0');
    }
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// vm_page_writeback (0x801841b8)
// If the given virtual page is mapped, above the low-mapped guard limit, and
// dirty (PTE changed bit set), write it back to backing store.
// -----------------------------------------------------------------------------
void vm_page_writeback(unsigned int vpage)
{
    if ((DAT_80610aa8 & 0xfffff000) <=
            (vpage & 0x7ff) * 0x1000 + (0x1000000 - DAT_806123f8)) {
        int pte = pte_group_entry_addr(vpage);
        if (*(char *)(pte + 7) < '\0') {         // changed bit set
            page_dma_read((void *)(vpage & 0x7ff), *(unsigned int *)(pte + 4) >> 0xc,
                          0, 0x1000, (unsigned int *)0, (unsigned char *)0);
        }
    }
}

// -----------------------------------------------------------------------------
// vm_range_pin_pages (0x8018423c)
// Mark every page overlapping [base, base+len) as "used" in the page bitmap so
// the pager will not evict them.
// -----------------------------------------------------------------------------
void vm_range_pin_pages(int base, int len)
{
    unsigned int start = base + (0x61000000 - DAT_806123f8);
    unsigned int firstPage = start >> 0xc;
    int lastPage = (int)(((start + len + 0xfff & 0xfffff000) - (start & 0xfffff000)) >> 0xc);

    for (unsigned int p = firstPage; (int)p < lastPage; p++) {
        page_bitmap_set(p);
    }
}

// -----------------------------------------------------------------------------
// vm_page_fault_handler (0x801842b0)
// The demand-pager.  On a page fault: if the page window is full, run a FIFO/LRU
// scan for a reference-cleared victim, migrate entries between the active and
// free rings, write the victim back if dirty and unmap it; then install a fresh
// PTE for the faulting page and DMA its contents in.  When the window is not yet
// full it simply appends a new mapping.  The whole body runs with interrupts
// masked (FUN_8039ccb4 / FUN_8039ccc8).
// -----------------------------------------------------------------------------
void vm_page_fault_handler(void)
{
    unsigned char stackReq[32];
    volatile char done;

    unsigned int faultVaddr = (unsigned int)FUN_803826dc();
    FUN_8039f35c();
    FUN_8039ccb4();                              // disable interrupts

    faultVaddr &= 0xfffff000;
    done = '\x01';
    unsigned short vpageAlias = (unsigned short)(faultVaddr + 0x60000000 >> 0xc);

    if ((unsigned int)DAT_806123da == DAT_806123f4 >> 0xc) {
        // ---- window full: LRU victim scan --------------------------------
        bool found = false;
        unsigned int victimKey = 0;
        unsigned int *victimPte = (unsigned int *)0;

        unsigned int tries = 0;
        do {
            unsigned short freeEntry = *(unsigned short *)(&DAT_80612400 + (unsigned int)DAT_806123de * 2);
            victimKey = (unsigned int)freeEntry;
            victimPte = (unsigned int *)pte_group_entry_addr(victimKey);
            if ((*(unsigned char *)((int)victimPte + 6) & 1) == 0) {   // ref bit clear -> victim
                found = true;
                break;
            }
            // still referenced: rotate it from free ring into active ring
            unsigned short active = *(unsigned short *)(DAT_8061240c + (unsigned int)DAT_806123dc * 2);
            *(unsigned short *)(DAT_8061240c + (unsigned int)DAT_806123dc * 2) = freeEntry;
            ring_index_advance(&DAT_806123dc, (unsigned int)DAT_806123d8);
            int pte = pte_group_entry_addr((unsigned int)active);
            pte_clear_ref_bit((active & 0x7ff) * 0x1000 + -0x60000000, pte);
            *(unsigned short *)(&DAT_80612400 + (unsigned int)DAT_806123de * 2) = active;
            ring_index_advance(&DAT_806123de, 4);
            tries++;
        } while (tries < 4);

        if (!found) {
            victimKey = (unsigned int)*(unsigned short *)(&DAT_80612400 + (unsigned int)DAT_806123de * 2);
            victimPte = (unsigned int *)pte_group_entry_addr(victimKey);
        }

        unsigned int victimWord1 = victimPte[1];
        if (*(char *)((int)victimPte + 7) < '\0') {           // victim dirty -> write back
            page_dma_read((void *)(victimKey & 0x7ff), victimWord1 >> 0xc,
                          0, 0x1000, (unsigned int *)0, (unsigned char *)0);
            unsigned int victimPhys = (victimKey & 0x7ff) * 0x1000 + 0xa0000000;
            if (((DAT_806123e4 & 0xfffff000) <= victimPhys) &&
                (victimPhys < (DAT_806123e4 + DAT_806123e8 + 0xfff & 0xfffff000))) {
                DAT_806123e4 = 0;
                DAT_806123e8 = 0;
            }
        }

        if ((faultVaddr < (DAT_806123e4 & 0xfffff000)) ||
            ((DAT_806123e4 + DAT_806123e8 + 0xfff & 0xfffff000) <= faultVaddr)) {
            page_dma_write(stackReq, faultVaddr, victimWord1 >> 0xc);
        }

        pte_unmap((victimKey & 0x7ff) * 0x1000 + -0x60000000, victimPte);
        short slot = PTE_INSTALL();
        unsigned short active = *(unsigned short *)(DAT_8061240c + (unsigned int)DAT_806123dc * 2);
        int pte = pte_group_entry_addr((unsigned int)active);
        pte_clear_ref_bit((active & 0x7ff) * 0x1000 + -0x60000000, pte);
        *(unsigned short *)(&DAT_80612400 + (unsigned int)DAT_806123de * 2) = active;
        ring_index_advance(&DAT_806123de, 4);
        *(unsigned short *)(DAT_8061240c + (unsigned int)DAT_806123dc * 2) = slot << 0xc | vpageAlias;
        ring_index_advance(&DAT_806123dc, (unsigned int)DAT_806123d8);
    }
    else {
        // ---- window not full: append a new mapping -----------------------
        if ((faultVaddr < (DAT_806123e4 & 0xfffff000)) ||
            ((DAT_806123e4 + DAT_806123e8 + 0xfff & 0xfffff000) <= faultVaddr)) {
            page_dma_write(stackReq, faultVaddr, (unsigned int)DAT_806123da + (unsigned int)DAT_80612410);
        }
        short slot = PTE_INSTALL();
        unsigned short active = *(unsigned short *)(DAT_8061240c + (unsigned int)DAT_806123dc * 2);
        *(unsigned short *)(DAT_8061240c + (unsigned int)DAT_806123dc * 2) = slot << 0xc | vpageAlias;
        ring_index_advance(&DAT_806123dc, (unsigned int)DAT_806123d8);
        if (DAT_806123d8 <= DAT_806123da) {
            int pte = pte_group_entry_addr((unsigned int)active);
            pte_clear_ref_bit((active & 0x7ff) * 0x1000 + -0x60000000, pte);
            *(unsigned short *)(&DAT_80612400 + (unsigned int)DAT_806123de * 2) = active;
            ring_index_advance(&DAT_806123de, 4);
        }
        DAT_806123da = DAT_806123da + 1;
    }

    do { } while (done == '\0');

    FUN_8039ccc8();                              // re-enable interrupts
    FUN_8039f39c();
    FUN_80397a6c();
    FUN_80397a74();
    FUN_80184bec(-0x7fa67020);
    FUN_80382728();
}
