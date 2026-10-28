// Module: prop_table_loader
// TSFP GameCube main.dol non-matching decompilation.
//
// Level prop-table loader. On level load the engine loads one or more packed
// "prop table" asset files (host0/prop_table/prop_*.gcas), each a blob whose
// header at +4 gives the payload offset and whose records are 0x28 bytes. The
// loader counts the records across all loaded tables, allocates a single
// 0x30-stride prop-instance array (DAT_806125e8), and copies each source
// record into it (remapping model ids through DAT_8044ebe0 and squaring a
// radius field). A per-prop id->instance-index map is written to DAT_805c9858.
//
// The set of tables loaded depends on the game state:
//   - "generic" always: prop_generic_gc
//   - if stats tracking / arcade-lobby / attract: prop_all_gc  (single table)
//   - otherwise: prop_generic_gc + the per-level prop_lv_<d>_gc and
//     prop_c_lv_<d>_gc tables (via the loader helper).
//
// Batch: batch_710  |  Addresses: 0x8023cc70 - 0x8023d2eb (subset)
// NON-MATCHING decompilation.

#include "common.h"

// ---------------------------------------------------------------------------
// Prop-table state (file-local externs to raw symbols).
// ---------------------------------------------------------------------------
// Per-table parallel arrays, 0x14 (=0x50/4) slots each:
//   DAT_805cabe8[i] = loaded table blob pointer   (base -0x7fa35418)
//   DAT_805cac38[i] = table blob size             (base -0x7fa353c8)
//   DAT_805cac88[i] = record count for table i    (base -0x7fa35378)
extern int   DAT_805cabe8;    // table[0] blob pointer / array base
extern int   DAT_805cac38;    // table[0] size / array base (also &uint out-param)
extern int   DAT_805cac88;    // table[0] record count / array base
extern int   DAT_8061297c;    // number of loaded tables
extern int   DAT_806125e8;    // consolidated prop-instance array (0x30 stride)
extern int   DAT_80612978;    // running prop-instance write cursor
extern int   DAT_80612968;    // scratch table A (0x7500 bytes)
extern int   DAT_80612970;    // scratch table B (0x50 bytes)

extern int   DAT_80612614;    // current level id
extern int   DAT_805015c0;    // mode flags
extern char  DAT_805015bd;    // mode discriminator byte ('\n' == attract)
extern int   DAT_8044ebe0;    // model-id remap table (int stride)
extern short DAT_805c9858;    // prop id -> instance-index map (short stride)

// Prop-table asset name strings (in .rodata).
extern char s_host0_prop_table_prop_all_gcas_w_8046b460[];
extern char s_host0_prop_table_prop_generic_gc_8046b484[];
extern char s_host0_prop_table_prop_lv_d_gcas__8046b4ac[];
extern char s_host0_prop_table_prop_c_lv_d_gca_8046b4d0[];

// Prop definition table: 0x1e entries, 0xb4 stride, base 0x8046b4f8.
#define PROP_DEF_BASE    0x8046b4f8
#define PROP_DEF_COUNT   0x1e
#define PROP_DEF_STRIDE  0xb4

// ---------------------------------------------------------------------------
// Cross-module / register-spill helpers (file-local; common.h owns no raw syms).
// ---------------------------------------------------------------------------
extern int  stats_is_tracking_active(void);
extern void memset_wrapper(int dst, int val, int len);
extern int  asset_load(unsigned long long ctx, double d2, double d3,
                       unsigned long long a4, unsigned long long a5,
                       unsigned long long a6, unsigned long long a7,
                       unsigned long long a8, int nameHi, int one, unsigned int *outSize,
                       int r6, int r7, int r8, int r9, int r10);
extern unsigned long long pack_stub_noop_b0fc(void);
extern int  mem_alloc(unsigned long long ctx, double d2, double d3,
                     unsigned long long a4, unsigned long long a5,
                     unsigned long long a6, unsigned long long a7,
                     unsigned long long a8, int size, int one, int count,
                     int r6, int r7, int r8, int r9, int r10);
extern int  zone_set_interior_flag(int levelId);
extern int  FUN_80253df0(int levelId);        // interior-level id remap
extern unsigned long long FUN_80387fb8(unsigned long long ctx, double d2, double d3,
                       unsigned long long a4, unsigned long long a5,
                       unsigned long long a6, unsigned long long a7,
                       unsigned long long a8, int scratch, char *fmt, int arg,
                       int r6, int r7, int r8, int r9, int r10); // sprintf-into-scratch
extern unsigned long long FUN_803826f0(void); // level-load work context (loader)
extern unsigned long long FUN_803826e4(void); // level-load work context (one-table)
extern void FUN_80382730(void);               // release one-table context
extern void FUN_8038273c(void);               // release loader context

// Forward declarations for the two functions that call each other.
static void prop_table_load_one(unsigned long long ctx, double d2, double d3,
                                unsigned long long a4, unsigned long long a5,
                                unsigned long long a6, unsigned long long a7,
                                unsigned long long a8);
static void prop_table_load_all(unsigned long long ctx, double d2, double d3,
                                unsigned long long a4, unsigned long long a5,
                                unsigned long long a6, unsigned long long a7,
                                unsigned long long a8);

// 0x8023d2c0 | prop_def_by_index
// Return a pointer (as an int address) to prop-definition record 'idx' in the
// 0x1e-entry x 0xb4-stride definition table, or 0 when idx is out of range.
int prop_def_by_index(int idx)
{
    if ((-1 < idx) && (idx < PROP_DEF_COUNT)) {
        return idx * PROP_DEF_STRIDE + PROP_DEF_BASE;
    }
    return 0;
}

// 0x8023d208 | prop_table_load_one
// Load a single prop-table asset into slot 'slot' (the slot index arrives in
// the low half of the loader context r-pair). Records the blob pointer/size and
// derives the record count = (size - (blob[+4] - 8)) / 0x28.
static void prop_table_load_one(unsigned long long ctx, double d2, double d3,
                                unsigned long long a4, unsigned long long a5,
                                unsigned long long a6, unsigned long long a7,
                                unsigned long long a8)
{
    (void)ctx;
    unsigned long long c = FUN_803826e4();
    int slot = (int)c;
    unsigned int *pSize = (unsigned int *)(&DAT_805cac38 + slot);

    (&DAT_805cabe8)[slot] =
        asset_load(0, d2, d3, a4, a5, a6, a7, a8, (int)(c >> 0x20), 1, pSize, 0, 0, 0, 0, 0);

    if (((&DAT_805cabe8)[slot] != 0) && (0 < (int)*pSize)) {
        pack_stub_noop_b0fc();
        DAT_8061297c = DAT_8061297c + 1;
        (&DAT_805cac88)[slot] =
            (*pSize - (*(int *)((&DAT_805cabe8)[slot] + 4) + -8)) / 0x28;
    }
    FUN_80382730();
}

// 0x8023cc70 | prop_table_load_all
// Load the prop tables appropriate to the current game state, consolidate every
// record into the 0x30-stride prop-instance array DAT_806125e8, and build the
// prop id -> instance-index map (DAT_805c9858).
static void prop_table_load_all(unsigned long long ctx, double d2, double d3,
                                unsigned long long a4, unsigned long long a5,
                                unsigned long long a6, unsigned long long a7,
                                unsigned long long a8)
{
    unsigned char au_scratch[280];   // sprintf scratch for the per-level names

    unsigned long long c = FUN_803826f0();
    bool useSingleAllTable = false;

    // Use the single "prop_all" table for stats-tracking / arcade-lobby /
    // attract-mode states; otherwise use generic + per-level tables.
    if (stats_is_tracking_active() != 0) {
        useSingleAllTable = true;
    } else if (((DAT_80612614 != 100) && ((DAT_805015c0 & 0x40) == 0)) &&
               ((DAT_805015bd != '\n' || (DAT_80612614 != 0x38)))) {
        useSingleAllTable = false;
    } else {
        useSingleAllTable = true;
    }

    // Clear the prop id -> index map (0x138e bytes at 0x8046b458).
    memset_wrapper(0x8046b458, 0xff, 0x138e);

    if (useSingleAllTable) {
        DAT_805cabe8 = asset_load(c, d2, d3, a4, a5, a6, a7, a8,
                                  (int)s_host0_prop_table_prop_all_gcas_w_8046b460, 1,
                                  (unsigned int *)&DAT_805cac38, 0, 0, 0, 0, 0);
        if ((DAT_805cabe8 != 0) && (0 < DAT_805cac38)) {
            pack_stub_noop_b0fc();
            DAT_8061297c = DAT_8061297c + 1;
        }
        DAT_805cac88 = (unsigned int)(DAT_805cac38 - (*(int *)(DAT_805cabe8 + 4) + -8)) / 0x28;
    } else {
        DAT_805cabe8 = asset_load(c, d2, d3, a4, a5, a6, a7, a8,
                                  (int)s_host0_prop_table_prop_generic_gc_8046b484, 1,
                                  (unsigned int *)&DAT_805cac38, 0, 0, 0, 0, 0);
        if ((DAT_805cabe8 != 0) && (0 < DAT_805cac38)) {
            pack_stub_noop_b0fc();
            DAT_8061297c = DAT_8061297c + 1;
        }
        DAT_805cac88 = (unsigned int)(DAT_805cac38 - (*(int *)(DAT_805cabe8 + 4) + -8)) / 0x28;

        // Per-level "prop_lv_<d>" table.
        if (zone_set_interior_flag(DAT_80612614) == 0) {
            FUN_80387fb8(0, d2, d3, a4, a5, a6, a7, a8, (int)au_scratch,
                         s_host0_prop_table_prop_lv_d_gcas__8046b4ac, DAT_80612614, 0, 0, 0, 0, 0);
        } else {
            int lv = FUN_80253df0(DAT_80612614);
            FUN_80387fb8(0, d2, d3, a4, a5, a6, a7, a8, (int)au_scratch,
                         s_host0_prop_table_prop_lv_d_gcas__8046b4ac, lv, 0, 0, 0, 0, 0);
        }
        prop_table_load_one((unsigned long long)(int)au_scratch, d2, d3, a4, a5, a6, a7, a8);

        // Per-level "prop_c_lv_<d>" (collision) table.
        if (zone_set_interior_flag(DAT_80612614) == 0) {
            FUN_80387fb8(0, d2, d3, a4, a5, a6, a7, a8, (int)au_scratch,
                         s_host0_prop_table_prop_c_lv_d_gca_8046b4d0, DAT_80612614, 0, 0, 0, 0, 0);
        } else {
            int lv = FUN_80253df0(DAT_80612614);
            FUN_80387fb8(0, d2, d3, a4, a5, a6, a7, a8, (int)au_scratch,
                         s_host0_prop_table_prop_c_lv_d_gca_8046b4d0, lv, 0, 0, 0, 0, 0);
        }
        prop_table_load_one((unsigned long long)(int)au_scratch, d2, d3, a4, a5, a6, a7, a8);
    }

    // Total record count across every loaded table.
    int total = 0;
    {
        int off = 0;
        for (int i = 0; i < DAT_8061297c; i = i + 1) {
            total = total + *(int *)((int)&DAT_805cac88 + off);
            off = off + 4;
        }
    }

    // Allocate the consolidated 0x30-stride instance array.
    DAT_806125e8 = mem_alloc(0, d2, d3, a4, a5, a6, a7, a8, total * 0x30, 1, total, 0, 0, 0, 0, 0);

    // Copy every source record (0x28 bytes) into an instance slot (0x30 bytes).
    int off = 0;
    for (int t = 0; t < DAT_8061297c; t = t + 1) {
        int blob = *(int *)((int)&DAT_805cabe8 + off);
        int recOff = 0;
        int payload = *(int *)(blob + 4);
        int recCount = *(int *)((int)&DAT_805cac88 + off);
        for (int r = 0; r < recCount; r = r + 1) {
            int *rec = (int *)(blob + payload + 8 + recOff);
            // Rebase intra-blob offsets to absolute pointers.
            rec[3] = *(int *)((int)&DAT_805cabe8 + off) + rec[3] + 8;
            rec[2] = *(int *)((int)&DAT_805cabe8 + off) + rec[2] + 8;
            if (rec[4] < 0) {
                rec[4] = 0;
            } else {
                rec[4] = *(int *)((int)&DAT_805cabe8 + off) + rec[4] + 8;
            }
            recOff = recOff + 0x28;
            rec[9] = *(int *)(&DAT_8044ebe0 + rec[9] * 4);   // remap model id

            int idx = DAT_80612978;
            int arr = DAT_806125e8;
            (&DAT_805c9858)[rec[0]] = (short)DAT_80612978;   // id -> index map
            *(int *)(arr + idx * 0x30)        = rec[1];
            *(int *)(arr + idx * 0x30 + 4)    = 0;
            *(int *)(arr + idx * 0x30 + 8)    = 0;
            *(int *)(arr + idx * 0x30 + 0xc)  = rec[2];
            *(int *)(arr + idx * 0x30 + 0x10) = rec[3];
            *(int *)(arr + idx * 0x30 + 0x14) = rec[4];
            *(float *)(arr + idx * 0x30 + 0x18) = (float)rec[5] * (float)rec[5]; // radius^2
            *(int *)(arr + idx * 0x30 + 0x1c) = rec[6];
            *(int *)(arr + idx * 0x30 + 0x20) = 0;
            *(int *)(arr + idx * 0x30 + 0x24) = rec[7];
            *(int *)(arr + idx * 0x30 + 0x28) = rec[8];
            *(int *)(arr + idx * 0x30 + 0x2c) = rec[9];
            DAT_80612978 = DAT_80612978 + 1;
        }
        off = off + 4;
    }
    FUN_8038273c();
}

// 0x8023d0ac | prop_table_reset
// Reset the prop-table state, clear the three 0x50-byte parallel arrays,
// allocate the two scratch tables, and load all prop tables for the level.
void prop_table_reset(unsigned long long ctx, double d2, double d3,
                      unsigned long long a4, unsigned long long a5,
                      unsigned long long a6, unsigned long long a7,
                      unsigned long long a8)
{
    DAT_80612978 = 0;
    memset_wrapper(0x8046ac38, 0, 0x50);   // -0x7fa353c8 == 0x8046ac38 (counts)
    memset_wrapper(0x8046abe8, 0, 0x50);   // -0x7fa35418 == 0x8046abe8 (pointers)
    memset_wrapper(0x8046ac88, 0, 0x50);   // -0x7fa35378 == 0x8046ac88 (sizes)
    DAT_8061297c = 0;
    DAT_80612968 = mem_alloc(ctx, d2, d3, a4, a5, a6, a7, a8, 0x7500, 1, 0x50, 0, 0, 0, 0, 0);
    DAT_80612970 = mem_alloc(ctx, d2, d3, a4, a5, a6, a7, a8, 0x50, 1, 0x50, 0, 0, 0, 0, 0);
    prop_table_load_all(ctx, d2, d3, a4, a5, a6, a7, a8);
}
