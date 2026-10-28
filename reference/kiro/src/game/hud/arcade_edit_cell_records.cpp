// Batch: batch_952 | Module: arcade match-editor cell record allocation / init / node storage
// Address range: 0x80336bc0 - 0x80337008
// Date: 2026-09-12
//
// Continuation of the arcade custom-match editor. These helpers allocate a new editor cell record
// in the editor context's cell array (DAT_80612be4 + 0xdd14, 0x8c-byte stride, count at +0xdd10),
// initialise it for its mode/type (+0x80), format its display label, and flush the cell's up-to-3
// node records into the flat node tables. aveditor_place_zone_cell is the public entry that places
// a fresh type-0xe (zone/trigger) cell into the active mode via aveditor_create_zone_cell.
//
//   aveditor_mode10_field_ptr        0x80336bc0  pointer to word field (idx*5 + 5) in a mode-10 rec.
//   aveditor_store_mode10_transform  0x80336bfc  copy a 2x vec3 transform into a mode-10 BSS record.
//   aveditor_type_record_ptr         0x80336c44  pointer into the 0x40-byte type table by disp idx.
//   aveditor_init_cell_for_mode      0x80336c60  per-mode cell init (display idx, node counts).
//   aveditor_bump_placement_counter  0x80336d40  bump the per-mode/per-type placement counter.
//   aveditor_format_cell_label       0x80336dd0  format "<name> <n>" cell label via a value cb.
//   aveditor_alloc_cell              0x80336e6c  allocate + init a new cell record, format, count.
//   aveditor_store_cell_nodes        0x80336f3c  store the cell's active (0x20/0x24/0x28) nodes.
//   aveditor_store_cell_nodes_by_idx 0x80336fc4  store nodes for the cell at a short record index.
//   aveditor_place_zone_cell         0x80337008  place a new type-0xe zone cell into the mode.
//
// Raw FUN_/DAT_ symbols get correctly-typed per-file externs; already-named helpers (from prior
// arcade_edit_* batches) are re-declared here to resolve the call sites.

#include "common.h"

extern "C" {

extern int  DAT_80612be4;    // editor context base
extern int  DAT_80612d00;    // active mode / owner object (0 when no mode active)

// per-type config table: DAT_80480f30 + type*0x4c flags word (bit0 = "sub-type indexed").
extern unsigned int DAT_80480f30;
extern int          DAT_80480f10;   // per-mode string-id table (int stride 0x13 words)
extern short        DAT_80477bc8;   // per-sub-type label string-id table (0xc4-byte stride)

// mode-10 (camera) record table, 0x37-word stride; DAT_805d67d8 is field +5 of record 0.
extern unsigned int DAT_805d67d8;
extern unsigned char DAT_805dc970;  // spline node table, 0x40-byte stride

extern const char s__s__d_80611208[];   // "%s %d" label format

// save/restore GPR-28 pair used to alloc a temp cell record; returns packed (hi=cell ptr).
extern unsigned long long FUN_803826f0(void);
extern void               FUN_8038273c(void);

extern int  get_string_by_id(int id);
extern void memset_wrapper(int dst, int val, int len);
extern void FUN_80388098(unsigned long long f1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int cell, int fmtId,
                         const char *fmt, int nameId, unsigned int value, unsigned int r14,
                         unsigned int r15, unsigned int r16);
extern void FUN_803370c0(void);   // scan/select local ready squad (bump helper)

// helpers defined in prior arcade_edit_* batches
extern unsigned int *aveditor_mode10_record_ptr(int cell);        // batch_951
extern void aveditor_ensure_cell_display_idx(int cell);           // batch_951
extern void aveditor_store_node(int cell, int slot, int *src);    // batch_951
extern int  aveditor_create_zone_cell(int type, int a2, unsigned short label, int idx); // batch_949
extern void aveditor_register_placed_cell(int mode, int *cell);   // batch_949

} // extern "C"

// forward decls (defined below)
void aveditor_format_cell_label(unsigned long long f1, unsigned long long a2, unsigned long long a3,
                                unsigned long long a4, unsigned long long a5, unsigned long long a6,
                                unsigned long long a7, unsigned long long a8, int cell, int labelBase,
                                void *valueCb, unsigned int a12, unsigned int r13, unsigned int r14,
                                unsigned int r15, unsigned int r16);
void aveditor_bump_placement_counter(int cell);
void aveditor_init_cell_for_mode(int cell);
void aveditor_store_cell_nodes(int cell, unsigned int paletteBase, int *src);

// -----------------------------------------------------------------------------

// Pointer to word field (index*5 + 5) inside a cell's mode-10 (0x37-word) camera record.
unsigned int *aveditor_mode10_field_ptr(int cell, int index)
{
    unsigned int *rec = aveditor_mode10_record_ptr(cell);
    return rec + index * 5 + 5;
}

// Copy a 2x vec3 transform (6 words at src-0x10..) into the mode-10 record slot for
// (recordIndex, fieldIndex) in the DAT_805d67d8 BSS table.
void aveditor_store_mode10_transform(int src, int recordIndex, int fieldIndex)
{
    unsigned int *s = (unsigned int *)(src + -4);
    unsigned int *d = &DAT_805d67d8 + fieldIndex * 5 + recordIndex * 0x37;
    int i = 2;
    unsigned int *dp;
    unsigned int *sp;
    do {
        dp = d;
        sp = s;
        unsigned int tmp = sp[2];
        dp[1] = sp[1];
        dp[2] = tmp;
        i = i + -1;
        s = sp + 2;
        d = dp + 2;
    } while (i != 0);
    dp[3] = sp[3];
}

// Pointer into the 0x40-byte type-record table (DAT_805dc970) by the cell's display index (+0x74)
// plus a per-type base offset.
unsigned char *aveditor_type_record_ptr(int cell, int type)
{
    return &DAT_805dc970 + (int)*(short *)(cell + 0x74) + type * 0x40;
}

// Per-mode cell initialisation: assign a display index and set the active node-count flags for the
// cell's type (+0x80). Mode 0xe additionally seeds three active node slots and stores default nodes.
void aveditor_init_cell_for_mode(int cell)
{
    switch (*(int *)(cell + 0x80)) {
    case 3:
        aveditor_ensure_cell_display_idx(cell);
        *(unsigned char *)(cell + 0x76) = 1;
        break;
    case 7:
    case 9:
        aveditor_ensure_cell_display_idx(cell);
        *(unsigned char *)(cell + 0x76) = 1;
        *(int *)(cell + 0x20) = 1;
        aveditor_store_node(cell, 0, (int *)cell);
        break;
    case 10:
        aveditor_ensure_cell_display_idx(cell);
        *(unsigned char *)(cell + 0x76) = 1;
        break;
    case 0xe: {
        aveditor_ensure_cell_display_idx(cell);
        *(unsigned char *)(cell + 0x76) = 3;
        *(int *)(cell + 0x20) = 1;
        *(int *)(cell + 0x24) = 1;
        *(int *)(cell + 0x28) = 1;
        int *nodeC = (int *)get_string_by_id(0);
        int nodeB = get_string_by_id(0);
        aveditor_store_cell_nodes(cell, (unsigned int)nodeB, nodeC);
        break;
    }
    }
}

// Bump the per-mode/per-type placement counter in the editor context. If the type's flags word
// (DAT_80480f30 bit0) marks it "sub-type indexed", run the sub-type selector and bump the +0x1490
// counter keyed by the sub-type (+0x84); otherwise bump the +0x15d4 counter keyed by mode (+0x80).
void aveditor_bump_placement_counter(int cell)
{
    if ((*(unsigned int *)(&DAT_80480f30 + *(int *)(cell + 0x80) * 0x4c) & 1) == 0) {
        int base = DAT_80612be4 + 0x10000 + *(int *)(cell + 0x80) * 4;
        *(int *)(base + 0x15d4) = *(int *)(base + 0x15d4) + 1;
    } else {
        FUN_803370c0();
        int base = DAT_80612be4 + 0x10000 + *(int *)(cell + 0x84) * 4;
        *(int *)(base + 0x1490) = *(int *)(base + 0x1490) + 1;
    }
}

// Format the cell's display label "<name> <value>": call the value callback to get the numeric
// value, resolve the name string-id (per-sub-type when +0x84 >= 0, else per-mode), then sprintf via
// FUN_80388098. The float/varargs registers are threaded through unchanged.
void aveditor_format_cell_label(unsigned long long f1, unsigned long long a2, unsigned long long a3,
                                unsigned long long a4, unsigned long long a5, unsigned long long a6,
                                unsigned long long a7, unsigned long long a8, int cell, int labelBase,
                                void *valueCb, unsigned int a12, unsigned int r13, unsigned int r14,
                                unsigned int r15, unsigned int r16)
{
    (void)f1; (void)a12; (void)r13; (void)labelBase;
    unsigned int value = ((unsigned int (*)(void))valueCb)();
    int nameId;
    if (*(int *)(cell + 0x84) < 0) {
        nameId = get_string_by_id((&DAT_80480f10)[*(int *)(cell + 0x80) * 0x13]);
    } else {
        nameId = get_string_by_id((int)*(short *)(&DAT_80477bc8 + *(int *)(cell + 0x84) * 0xc4));
    }
    FUN_80388098(f1, a2, a3, a4, a5, a6, a7, a8, cell, 0x1f, s__s__d_80611208, nameId, value,
                 r14, r15, r16);
}

// Allocate + initialise a new cell record at the end of the editor cell array, format its label,
// run per-mode init, then bump the cell count. Returns nothing (the cell is appended in place).
void aveditor_alloc_cell(unsigned long long f1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8)
{
    unsigned long long packed = FUN_803826f0();
    int cell = DAT_80612be4 + *(int *)(DAT_80612be4 + 0xdd10) * 0x8c + 0xdd14;
    memset_wrapper(cell, 0, 0x8c);
    *(unsigned long long *)(cell + 0x80) = packed;
    *(int *)(cell + 0x7c) = 0;
    *(int *)(cell + 0x88) = 1;
    *(int *)(cell + 0x78) = 1;
    *(short *)(cell + 0x74) = (short)0xffff;
    *(unsigned char *)(cell + 0x76) = 0;
    aveditor_format_cell_label(f1, a2, a3, a4, a5, a6, a7, a8, cell, DAT_80612be4 + 0xdd14,
                               (void *)aveditor_bump_placement_counter, 0, 0, 0, 0, 0);
    aveditor_init_cell_for_mode(cell);
    *(int *)(DAT_80612be4 + 0xdd10) = *(int *)(DAT_80612be4 + 0xdd10) + 1;
    FUN_8038273c();
}

// Store the cell's active node records: slots 0/1/2 are flushed when their active flag
// (+0x20/+0x24/+0x28) is set. Slot 2 uses the caller-supplied `src`; slots 0/1 use the freshly
// allocated scratch record (hi word of the alloc pair).
void aveditor_store_cell_nodes(int cell_unused, unsigned int paletteBase, int *src)
{
    (void)cell_unused; (void)paletteBase;
    unsigned long long packed = FUN_803826f0();
    int cell = (int)((unsigned long long)packed >> 0x20);
    if (*(int *)(cell + 0x20) != 0) {
        aveditor_store_node(cell, 0, (int *)(unsigned int)packed);
    }
    if (*(int *)(cell + 0x24) != 0) {
        aveditor_store_node(cell, 1, (int *)(unsigned int)packed);
    }
    if (*(int *)(cell + 0x28) != 0) {
        aveditor_store_node(cell, 2, src);
    }
    FUN_8038273c();
}

// Store nodes for the cell at record short-index (+0xc): resolves the cell record in the editor
// context and forwards to aveditor_store_cell_nodes with the active-mode palette base.
void aveditor_store_cell_nodes_by_idx(int recordPtr)
{
    aveditor_store_cell_nodes(DAT_80612be4 + (int)*(short *)(recordPtr + 0xc) * 0x8c + 0xdd14,
                              (unsigned int)(DAT_80612d00 + 0x20), (int *)(recordPtr + 0x20));
}

// Place a new type-0xe (zone/trigger) cell into the active mode: allocate the cell record, create a
// zone cell, register it with the active mode object, then store its nodes. No-op when no mode is
// active (DAT_80612d00 == 0).
void aveditor_place_zone_cell(unsigned long long f1, unsigned long long a2, unsigned long long a3,
                              unsigned long long a4, unsigned long long a5, unsigned long long a6,
                              unsigned long long a7, unsigned long long a8)
{
    if (DAT_80612d00 != 0) {
        aveditor_alloc_cell(f1, a2, a3, a4, a5, a6, a7, a8);
        int *cell = (int *)aveditor_create_zone_cell(0xe, 0xc, (unsigned short)0xffff, 0xffff);
        aveditor_register_placed_cell(DAT_80612d00, cell);
        aveditor_store_cell_nodes_by_idx((int)cell);
    }
}
