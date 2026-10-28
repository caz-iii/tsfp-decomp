// ============================================================================
// Address range: 0x80183c50 - 0x80183f1f | batch_473 | Date: 2026-09-06
// Subsystem: os (GameCube PowerPC Gekko software page-table / MMU manager).
// tag os-mmu.
//
// The low-level page-table (hashed PTE) manager the OS memory layer uses to map
// virtual pages into the Gekko MMU.  The hardware page table is a set of PTE
// groups (PTEGs); each PTEG holds 8 two-word PTEs.  A virtual address is hashed
// (XOR 0xabba over the page index, masked to 0x3ff) to select the primary PTEG,
// and the one's-complement hash selects the secondary PTEG.  A parallel
// "page-used" bitmap (DAT_80612408) tracks which software page slots are live.
// These are all called by the higher-level VM/map routines (FUN_801841b8 /
// FUN_801842b0 / FUN_80184a5c / FUN_80184660), which are the next frontier.
//
//   mmu_page_flags_reset      0x80183c50  seed the default page-attribute floats
//   mmu_tlb_invalidate_all    0x80183c80  tlbie sweep of all 64 TLB pages
//   page_bitmap_set           0x80183c9c  set page-used bit for a page index
//   page_bitmap_clear         0x80183cc8  clear page-used bit for a page index
//   page_bitmap_test          0x80183cf4  read page-used bit for a page index
//   pte_group_entry_addr      0x80183d18  address of a numbered PTE within its PTEG
//   pte_group_find_empty      0x80183d6c  find first empty PTE across primary+secondary PTEG
//   pte_install               0x80183df0  build+store a mapping PTE (RPN/WIMG/PP/valid)
//   pte_unmap                 0x80183ee8  invalidate a PTE word0 + tlbie
//   pte_clear_ref_bit         0x80183f00  clear the PTE reference byte + tlbie
//
// Structurally-faithful non-matching port.  The 8-byte PTE is modelled as two
// 32-bit words (word0 = VSID/API/H/V, word1 = RPN/flags) and the byte-granular
// rlwimi bitfield writes from the ROM are preserved verbatim so the on-disc PTE
// layout is reproduced exactly.

#include "common.h"
#include "generated_decls.h"

// ---- Gekko/PowerPC MMU intrinsics (Ghidra pseudo-ops) -----------------------
// The decompiler lowers the PPC tlbie/sync/eieio instructions to these calls.
// They are declared as permissive variadic externs in generated_externs.h
// (pulled in via generated_decls.h), so we do NOT re-declare them here — a
// typed local extern would collide (C2668 ambiguous overload).
//   void TLBInvalidateEntry(ea, 0);  void sync(0);  int enforceInOrderExecutionIO(...);

// ---- page-table state globals -----------------------------------------------
// DAT_80612414: base of the hardware page table (array of PTEGs, 0x40 bytes each
//               = 8 PTEs x 8 bytes).  Set up by the MMU init (FUN_80184660).
// DAT_80612408: base of the page-used bitmap (1 bit per software page slot).
extern int   DAT_80612414;   // page-table (PTEG array) base address
extern int   DAT_80612408;   // page-used bitmap base address

// ---- default page-attribute floats (mmu_page_flags_reset) -------------------
extern float DAT_80411424;
extern float DAT_80411428;
extern float DAT_8041142c;
extern float DAT_80411430;
extern float DAT_80411434;
extern float DAT_80411438;
extern float FLOAT_80610a84;
extern float FLOAT_80616710;
extern float FLOAT_80616714;

// ---- spawn-frame register-pair helpers (used by pte_install) ----------------
extern unsigned long long FUN_803826f0(void);  // fetch spawn-frame arg pair (r3:r4)
extern void               FUN_8038273c(void);   // matching frame pop

// -----------------------------------------------------------------------------
// mmu_page_flags_reset (0x80183c50)
// Seed the default page-attribute float table used elsewhere in the memory
// layer.  FLOAT_80616714 is the "set" attribute, FLOAT_80616710 the "clear".
// -----------------------------------------------------------------------------
void mmu_page_flags_reset(void)
{
    DAT_80411430   = FLOAT_80616714;
    DAT_80411424   = FLOAT_80616710;
    FLOAT_80610a84 = FLOAT_80616714;
    DAT_80411428   = FLOAT_80616710;
    DAT_8041142c   = FLOAT_80616710;
    DAT_80411434   = FLOAT_80616714;
    DAT_80411438   = FLOAT_80616714;
}

// -----------------------------------------------------------------------------
// mmu_tlb_invalidate_all (0x80183c80)
// Sweep all 64 TLB pages (0x1000 apart) with tlbie.  Returns the final EA so
// the ROM's tail is preserved (the value is discarded by callers).
// -----------------------------------------------------------------------------
int mmu_tlb_invalidate_all(void)
{
    int addr  = 0;
    int count = 0x40;
    do {
        TLBInvalidateEntry(addr, 0);
        addr  += 0x1000;
        count -= 1;
    } while (count != 0);
    return addr;
}

// -----------------------------------------------------------------------------
// page_bitmap_set (0x80183c9c)
// Set the "used" bit for software page index `page` in the page bitmap.
// The ROM computes a signed floor-divide-by-8 for the byte index, then the
// residual bit within that byte.
// -----------------------------------------------------------------------------
void page_bitmap_set(unsigned int page)
{
    int byteIndex = ((int)page >> 3) + (unsigned int)((int)page < 0 && (page & 7) != 0);
    *(unsigned char *)(DAT_80612408 + byteIndex) =
        *(unsigned char *)(DAT_80612408 + byteIndex) |
        (unsigned char)(1 << (page + byteIndex * -8));
}

// -----------------------------------------------------------------------------
// page_bitmap_clear (0x80183cc8)
// Clear the "used" bit for software page index `page`.
// -----------------------------------------------------------------------------
void page_bitmap_clear(unsigned int page)
{
    int byteIndex = ((int)page >> 3) + (unsigned int)((int)page < 0 && (page & 7) != 0);
    *(unsigned char *)(DAT_80612408 + byteIndex) =
        *(unsigned char *)(DAT_80612408 + byteIndex) &
        ~(unsigned char)(1 << (page + byteIndex * -8));
}

// -----------------------------------------------------------------------------
// page_bitmap_test (0x80183cf4)
// Return the "used" bit (0/1) for software page index `page`.
// -----------------------------------------------------------------------------
unsigned int page_bitmap_test(unsigned int page)
{
    int byteIndex = ((int)page >> 3) + (unsigned int)((int)page < 0 && (page & 7) != 0);
    return (int)(unsigned int)*(unsigned char *)(DAT_80612408 + byteIndex)
               >> ((page + byteIndex * -8) & 0x3f) & 1;
}

// -----------------------------------------------------------------------------
// pte_group_entry_addr (0x80183d18)
// Compute the address of PTE #n (n = bits 12..15 of the packed key) within the
// PTEG selected by the hashed page index.  Entries 0..7 live in the primary
// PTEG (hash), entries 8..15 in the secondary PTEG (~hash).  Each PTEG is 0x40
// bytes; each PTE is 8 bytes.
// -----------------------------------------------------------------------------
int pte_group_entry_addr(unsigned int key)
{
    unsigned int entry = (key >> 0xc) & 0xf;
    // Hashed page index of the mapped page (physical window base 0xa0000000).
    unsigned int hash  = ((key & 0x7ff) * 0x1000 + 0xa0000000) >> 0xc & 0xffff ^ 0xabba;

    if (entry < 8) {
        return DAT_80612414 + (hash & 0x3ff) * 0x40 + entry * 8;
    }
    return DAT_80612414 + (~hash & 0x3ff) * 0x40 + (entry - 8) * 8;
}

// -----------------------------------------------------------------------------
// pte_group_find_empty (0x80183d6c)
// Search the primary PTEG (hash) then the secondary PTEG (~hash) of `vaddr` for
// the first empty PTE (word0 == 0).  Writes the 0..15 slot number to *outSlot
// and returns a pointer to the PTE, or NULL if both PTEGs are full.
// -----------------------------------------------------------------------------
int *pte_group_find_empty(unsigned int vaddr, int *outSlot)
{
    unsigned int hash = (vaddr >> 0xc) & 0xffff ^ 0xabba;

    int  slot = 0;
    int *pte  = (int *)(DAT_80612414 + (hash & 0x3ff) * 0x40);
    int  n    = 8;
    do {
        if (*pte == 0) {
            *outSlot = slot;
            return pte;
        }
        slot += 1;
        pte  += 2;   // advance one 8-byte PTE
        n    -= 1;
    } while (n != 0);

    slot = 0;
    pte  = (int *)(DAT_80612414 + (~hash & 0x3ff) * 0x40);
    n    = 8;
    do {
        if (*pte == 0) {
            *outSlot = slot + 8;
            return pte;
        }
        slot += 1;
        pte  += 2;
        n    -= 1;
    } while (n != 0);

    return (int *)0;
}

// -----------------------------------------------------------------------------
// pte_install (0x80183df0)
// Build and store a mapping PTE.  The virtual page and its physical page number
// arrive in the spawn-frame register pair (hi = vaddr, lo = physical page).
// The routine locates a free PTE in the vaddr's PTEG, then writes word1 (RPN +
// WIMG + PP) and word0 (VSID/API + H + V) with the exact rlwimi bitfields from
// the ROM, bracketed by sync/eieio memory barriers.
// -----------------------------------------------------------------------------
void pte_install(void)
{
    unsigned long long frame = FUN_803826f0();
    unsigned int vaddr = (unsigned int)(frame >> 0x20);   // r3 : virtual address
    unsigned int phys  = (unsigned int)frame;             // r4 : physical page number

    // API field for word0 (bits 26..31 of vaddr rotated left 10).
    unsigned int api = (vaddr << 0xa) >> 0x1a;            // rlwinm r30,r31,0xa,0x1a,0x1f

    int slot;
    unsigned char *pte = (unsigned char *)pte_group_find_empty(vaddr, &slot);

    // WIMG memory/coherency bits: read the page-used bit of the aliased window
    // (vaddr + 0x60000000) and OR in the guarded bit (0x2).
    unsigned int wimg = page_bitmap_test((vaddr + 0x60000000) >> 0xc) | 2;

    sync(0);

    // ---- word1: RPN + WIMG + PP ---------------------------------------------
    unsigned int *w = (unsigned int *)pte;
    // RPN = physical page number << 12 into bits 0..19.
    w[1] = (phys << 0xc) | (w[1] & 0xfff);
    // Clear the referenced/changed/WIMG/PP scratch bits, then set WIMG(guard)+PP.
    pte[6] &= 0xfe;                                   // clear ref/changed low bit
    pte[7] &= 0x7f;                                   // clear W bit
    pte[7] &= 0x87;                                   // clear I/M/G nibble
    pte[7]  = ((unsigned char)wimg & 3) | 2 | (pte[7] & 0xfc);  // set G + PP low

    enforceInOrderExecutionIO();                      // eieio

    // ---- word0: VSID + H + V ------------------------------------------------
    // VSID = 0x0aba (0x10000 - 0x5446 = 0xaba) shifted into bits 1..24.
    w[0] = (w[0] & 0x8000007f) | 0x55dd00;            // rlwimi VSID field

    // H (hash-select) bit from the found slot: slots 8..15 -> secondary hash.
    pte[3] = (unsigned char)(((slot >= 8) ? 1 : 0) << 6 & 0x40) | (pte[3] & 0xbf);
    // API into word0 bits 26..31.
    pte[3] = (unsigned char)api | (pte[3] & 0xc0);
    // V (valid) bit.
    pte[0] = (pte[0] & 0x7f) | 0x80;

    sync(0);
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// pte_unmap (0x80183ee8)
// Invalidate the PTE: clear word0 (valid + VSID), then tlbie the page, with
// sync barriers around the tlbie so the invalidate is globally visible.
// -----------------------------------------------------------------------------
void pte_unmap(unsigned int vaddr, unsigned int *pte)
{
    *pte = 0;
    sync(0);
    TLBInvalidateEntry(vaddr, 0);
    sync(0);
}

// -----------------------------------------------------------------------------
// pte_clear_ref_bit (0x80183f00)
// Clear the PTE's reference byte (used by the page-aging / LRU sweep) and tlbie
// so the hardware re-sets it on the next access.
// -----------------------------------------------------------------------------
void pte_clear_ref_bit(unsigned int vaddr, int pte)
{
    *(unsigned char *)(pte + 6) = *(unsigned char *)(pte + 6) & 0xfe;
    sync(0);
    TLBInvalidateEntry(vaddr, 0);
    sync(0);
}
