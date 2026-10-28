// Batch: batch_951 | Module: arcade match-editor node-buffer indexing / removal
// Address range: 0x80336784 - 0x80336ba8
// Date: 2026-09-12
//
// The node-buffer bookkeeping behind the arcade custom-match editor. Each editor cell owns a run of
// fixed-size "node" records in one of three flat BSS tables, chosen by the cell's type (+0x80):
//   mode 3 (path)        -> DAT_805dbdb8, 6-byte records.
//   mode 7/9/0xe (spline)-> DAT_805dc970, 0x40-byte records.
//   mode 10 (camera)     -> DAT_805d67c8, 0x37-word (0xdc-byte) records.
// A cell's run begins at its base node index (+0x74) and spans (+0x76) records. These helpers
// compute spans/counts, move a cell's block within its table, and remove a cell's nodes while
// shifting the following same-category cells' base indices down (called by match_delete_entry).
//
//   aveditor_nodes_after_cell        0x80336784  sum node counts of same-category cells after one.
//   aveditor_node_bytes              0x80336830  per-type record byte size * count.
//   aveditor_move_cell_nodes         0x80336874  move a cell's node block within its type table.
//   aveditor_total_nodes_for_mode    0x80336948  total node/record count for a mode/type.
//   aveditor_node_multiplier         0x803369b0  per-type node display multiplier.
//   aveditor_cell_node_span          0x803369f4  cell node count (+0x76) * multiplier.
//   aveditor_remove_cell_nodes       0x80336a28  remove a cell's nodes, shift following cells down.
//   aveditor_store_node              0x80336b40  store a node record into the spline table.
//   aveditor_ensure_cell_display_idx 0x80336b7c  lazily assign a display index if unset (-1).
//   aveditor_mode10_record_ptr       0x80336ba8  pointer to a cell's mode-10 (0x37-word) record.
//
// The "category" of a mode is DAT_80480f58 + type*0x4c (0..2 == a real placement category). Cells
// in the same category share a node table region and are compacted together on removal.
//
// Raw FUN_/DAT_ symbols get correctly-typed per-file externs; already-named helpers are re-declared.

#include "common.h"

extern "C" {

extern int  DAT_80612be4;    // editor context base

// per-type config table: DAT_80480f58 + type*0x4c is the placement "category" of a mode.
extern int  DAT_80480f58;

// node scratch tables (BSS)
extern unsigned char DAT_805dbdb8;   // mode-3 path nodes, 6-byte stride
extern unsigned char DAT_805dc970;   // mode-7/9/0xe spline nodes, 0x40-byte stride
extern unsigned char DAT_805d67c8;   // mode-10 camera records, 0x37-word (0xdc-byte) stride

extern unsigned long long __save_gpr_28(void);
extern void __restore_gpr_28(void);

extern int  match_history_index_of(int cell);
extern void leaf_31e8(int dst, int src, int len);          // small memmove
extern void FUN_8038b510(int *dst, int *src);              // copy a spline node record
extern void aveditor_update_cell_display_index(int cell);  // batch_950

} // extern "C"

// -----------------------------------------------------------------------------

// Sum the node counts (+0x76) of every cell AFTER `cell` (by history order) that shares its
// placement category (DAT_80480f58 category 0..2). Used to size a compaction shift.
int aveditor_nodes_after_cell(int cell)
{
    int start = match_history_index_of(cell) + 1;
    int total = 0;
    int remaining = *(int *)(DAT_80612be4 + 0xdd10) - start;
    int off = start * 0x8c;
    if (start < *(int *)(DAT_80612be4 + 0xdd10)) {
        do {
            int cat = *(int *)(&DAT_80480f58 + *(int *)(DAT_80612be4 + off + 0xdd94) * 0x4c);
            if ((-1 < cat) && (cat < 3) &&
                (cat == *(int *)(&DAT_80480f58 + *(int *)(cell + 0x80) * 0x4c))) {
                total = total + (unsigned int)*(unsigned char *)(cell + 0x76);
            }
            off = off + 0x8c;
            remaining = remaining + -1;
        } while (remaining != 0);
    }
    return total;
}

// Byte size of `count` node records of the given mode `type`: 6 (mode 3), 0x40 (modes 7/9/0xe),
// 0x14 (mode 10), else 0.
int aveditor_node_bytes(int type, int count)
{
    int recSize = 0;
    switch (type) {
    case 3:
        recSize = 6;
        break;
    case 7:
    case 9:
    case 0xe:
        recSize = 0x40;
        break;
    case 10:
        recSize = 0x14;
        break;
    }
    return recSize * count;
}

// Move a cell's node block within its type table down to its (updated) base index. The cell record
// and its new base index arrive packed in the saved GPR pair (hi = cell record, lo = new base).
void aveditor_move_cell_nodes(void)
{
    unsigned long long packed = __save_gpr_28();
    int cell = (int)((unsigned long long)packed >> 0x20);
    int srcIndex = (int)packed;
    int dstIndex = (int)*(short *)(cell + 0x74);

    int nodes = aveditor_nodes_after_cell(cell);
    int bytes = aveditor_node_bytes(*(int *)(cell + 0x80), nodes);

    switch (*(int *)(cell + 0x80)) {
    case 3:
        leaf_31e8((int)&DAT_805dbdb8 + dstIndex * 6, (int)&DAT_805dbdb8 + srcIndex * 6, bytes);
        break;
    case 7:
    case 9:
    case 0xe:
        leaf_31e8((int)(&DAT_805dc970 + dstIndex), (int)(&DAT_805dc970 + srcIndex), bytes);
        break;
    case 10:
        leaf_31e8((int)(&DAT_805d67c8 + dstIndex * 0x37), (int)(&DAT_805d67c8 + srcIndex * 0x37),
                  bytes);
        break;
    }
    __restore_gpr_28();
}

// Total node/record count for mode `type`: for mode 3 sum the per-cell entry counts (+0x20);
// otherwise sum the per-cell node counts (+0x76).
int aveditor_total_nodes_for_mode(int type)
{
    int total = 0;
    int off = 0;
    int remaining = *(int *)(DAT_80612be4 + 0xdd10);
    if (0 < remaining) {
        do {
            int cell = DAT_80612be4 + off + 0xdd14;
            if (*(int *)(cell + 0x80) == type) {
                if (type == 3) {
                    total = total + *(int *)(cell + 0x20);
                } else {
                    total = total + (unsigned int)*(unsigned char *)(cell + 0x76);
                }
            }
            off = off + 0x8c;
            remaining = remaining + -1;
        } while (remaining != 0);
    }
    return total;
}

// Per-type node display multiplier: 10 (mode 3), 0x40 (modes 7/9/0xe), 1 (mode 10), else 0.
int aveditor_node_multiplier(int cell)
{
    int result = 0;
    switch (*(int *)(cell + 0x80)) {
    case 3:
        result = 10;
        break;
    case 4:
    case 5:
    case 6:
    case 8:
    case 0xb:
    case 0xc:
    case 0xd:
        result = 0;
        break;
    case 7:
    case 9:
    case 0xe:
        result = 0x40;
        break;
    case 10:
        result = 1;
        break;
    default:
        result = 0;
        break;
    }
    return result;
}

// A cell's node span: node count (+0x76) * per-type multiplier.
int aveditor_cell_node_span(int cell)
{
    int mul = aveditor_node_multiplier(cell);
    return (unsigned int)*(unsigned char *)(cell + 0x76) * mul;
}

// Remove `cell`'s nodes: when the cell owns nodes and belongs to a real placement category (0..2),
// shift the base index (+0x74) of every same-category cell that follows it down by this cell's
// span, then compact the underlying node table.
void aveditor_remove_cell_nodes(int cell)
{
    if ((*(short *)(cell + 0x74) != -1) || (*(char *)(cell + 0x76) != '\0')) {
        int catOff = *(int *)(cell + 0x80) * 0x4c;
        int span = aveditor_cell_node_span(cell);
        if ((-1 < *(int *)(&DAT_80480f58 + catOff)) && (*(int *)(&DAT_80480f58 + catOff) < 3)) {
            int off = 0;
            for (int i = 0; i < *(int *)(DAT_80612be4 + 0xdd10); i = i + 1) {
                int other = DAT_80612be4 + off + 0xdd14;
                if ((other != cell) &&
                    (-1 < *(int *)(&DAT_80480f58 + *(int *)(other + 0x80) * 0x4c)) &&
                    (*(int *)(&DAT_80480f58 + *(int *)(other + 0x80) * 0x4c) < 3) &&
                    (*(int *)(&DAT_80480f58 + *(int *)(other + 0x80) * 0x4c) ==
                        *(int *)(&DAT_80480f58 + catOff)) &&
                    (*(short *)(cell + 0x74) < *(short *)(other + 0x74))) {
                    *(short *)(other + 0x74) = *(short *)(other + 0x74) - (short)span;
                }
                off = off + 0x8c;
            }
            aveditor_move_cell_nodes();
        }
    }
}

// Store node record `src` into the spline table (0x40-byte stride) at cell base (+0x74) + slot.
void aveditor_store_node(int cell, int slot, int *src)
{
    FUN_8038b510((int *)(&DAT_805dc970 + (int)*(short *)(cell + 0x74) + slot * 0x40), src);
}

// Lazily assign the cell's display index if it is still unset (-1).
void aveditor_ensure_cell_display_idx(int cell)
{
    if (*(short *)(cell + 0x74) == -1) {
        aveditor_update_cell_display_index(cell);
    }
}

// Pointer to a cell's mode-10 (0x37-word) camera record in DAT_805d67c8.
unsigned int *aveditor_mode10_record_ptr(int cell)
{
    // 0x37 is a word (u32) stride in the original (undefined4* base), i.e. 0xdc bytes per record.
    return (unsigned int *)&DAT_805d67c8 + *(short *)(cell + 0x74) * 0x37;
}
