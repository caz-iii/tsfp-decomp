// Batch: batch_950 | Module: arcade match-editor record (de)serialization copy helpers
// Address range: 0x803363d4 - 0x80336778
// Date: 2026-09-12
//
// Fixed-stride record shuffling used by the arcade custom-match map serializer/deserializer
// (callers: aveditor_serialize_map, aveditor_deserialize_map, level_preload_all_resources_pass).
// These move small fixed-size records between the live editor cell records (DAT_80612be4 + 0xdd14,
// stride 0x8c) and the flat serialization scratch buffers in BSS:
//   DAT_805d67c8 region : 0xdc-stride camera/spline records (mode-10 cells).
//   DAT_805dbdb8 region : 6-byte path/waypoint node records (mode-3 cells).
//   DAT_805dc970        : a fixed scratch pointer returned to callers.
//
//   aveditor_update_cell_display_index 0x803363d4  refresh a cell's +0x74 display index.
//   aveditor_copy_records_to_serial    0x80336408  copy N records: live(0x14) -> serial(0xdc).
//   aveditor_copy_records_from_serial  0x8033646c  copy N records: serial(0xdc) -> live(0x14).
//   aveditor_store_cell_entries        0x803364d0  copy a cell's sub-entries into the 0xdc table.
//   aveditor_store_all_mode10_cells    0x80336548  run the above for every mode-10 cell.
//   aveditor_compact_path_nodes        0x803365b8  compact mode-3 6-byte path nodes in place.
//   aveditor_path_node_ptr             0x80336690  pointer to a cell's path node at index.
//   aveditor_load_path_nodes           0x803366ac  copy 6-byte nodes into the path table.
//   aveditor_gather_path_nodes         0x803366e8  gather all mode-3 nodes into a buffer.
//   aveditor_get_node_scratch          0x80336778  return the fixed node scratch pointer.
//
// The inner record copy is a 3-iteration unrolled loop that moves fields [+4..+0xf] then a tail
// word [+0xc of the last pair]; it is reproduced faithfully with word/short reads. The negative
// constant -0x7fa2983c is the sign-extended low half of the real base 0x805d67c4 (the 0xdc table
// minus the 4-byte pre-offset the decompiler folds into the pointer). Modelled via SERIAL_TABLE.
//
// Raw FUN_/DAT_ symbols get correctly-typed per-file externs; already-named helpers are re-declared.

#include "common.h"

extern "C" {

extern int  DAT_80612be4;    // editor context base

// serialization scratch buffers (BSS)
extern unsigned char DAT_805d67d8;   // 0xdc-stride serial record table anchor (see SERIAL_TABLE)
extern unsigned char DAT_805dbdb8;   // 6-byte path node table (word part)
extern unsigned short DAT_805dbdbc;  // 6-byte path node table (short tail, stride 6 -> [*3] as u16)
extern unsigned char DAT_805dc970;   // fixed node scratch pointer target

extern int  __save_gpr_25(void);
extern void __restore_gpr_25(void);

extern int  aveditor_lookup_cell_display(int type);
extern void leaf_31e8(int dst, unsigned int src, int len);   // small memmove (6 bytes)
extern int  FUN_80337078(int mode);   // count cells of a mode
extern int  FUN_80336948(int mode);   // running node index base for a mode

} // extern "C"

// The 0xdc serial record table base (0x805d67c4): the decompiler addresses it as
// (idx*0xdc + i*0x14) + -0x7fa2983c and pre-decrements the pointer by 4, so element field access
// starts at +4. Recover the true byte base here.
#define SERIAL_TABLE ((unsigned char *)0x805d67c4u)

// Copy one 0x10-byte record body ([+4..+0x13]) from `src` to `dst`, matching the original's
// 3-step unrolled word/word/tail sequence. Both point 4 bytes before the record start.
static inline void copy_record_body(unsigned int *dst, const unsigned int *src)
{
    unsigned int *pd = dst;
    const unsigned int *ps = src;
    int n = 2;
    do {
        unsigned int w2 = ps[2];
        pd[1] = ps[1];
        pd[2] = w2;
        n = n - 1;
        ps = ps + 2;
        pd = pd + 2;
    } while (n != 0);
    pd[3] = ps[3];
}

// -----------------------------------------------------------------------------

// Refresh a cell's +0x74 display index from its type (+0x80) via the lookup table.
void aveditor_update_cell_display_index(int cell)
{
    int idx = aveditor_lookup_cell_display(*(int *)(cell + 0x80));
    *(short *)(cell + 0x74) = (short)idx;
}

// Copy `count` records from the live table at `live` (stride 0x14) into the serial table
// (stride 0xdc). Used by aveditor_serialize_map.
void aveditor_copy_records_to_serial(int live, int count)
{
    int liveOff = 0;
    int serialOff = 0;
    for (int i = 0; i < count; i = i + 1) {
        copy_record_body((unsigned int *)(live + liveOff - 4),
                         (const unsigned int *)((int)SERIAL_TABLE + serialOff));
        liveOff = liveOff + 0x14;
        serialOff = serialOff + 0xdc;
    }
}

// Copy `count` records from the serial table (stride 0xdc) into the live table at `live`
// (stride 0x14). Used by aveditor_deserialize_map. (Inverse of aveditor_copy_records_to_serial.)
void aveditor_copy_records_from_serial(int live, int count)
{
    int liveOff = 0;
    int serialOff = 0;
    for (int i = 0; i < count; i = i + 1) {
        copy_record_body((unsigned int *)((int)SERIAL_TABLE + liveOff),
                         (const unsigned int *)(live + serialOff - 4));
        liveOff = liveOff + 0x14;
        serialOff = serialOff + 0x14;   // note: original advances the live side by 0x14
    }
}

// Copy a cell's sub-entries (count at +0x70, entry pointers at +0x20 stride 8) into the serial
// table row selected by the cell's display index (+0x74), stride 0x14 within the 0xdc row.
void aveditor_store_cell_entries(int cell)
{
    int rowOff = 0;
    int entOff = 0;
    for (int i = 0; i < *(int *)(cell + 0x70); i = i + 1) {
        copy_record_body(
            (unsigned int *)((int)&DAT_805d67d8 + *(short *)(cell + 0x74) * 0xdc + rowOff),
            (const unsigned int *)(*(int *)(cell + entOff + 0x20) + 0x14));
        rowOff = rowOff + 0x14;
        entOff = entOff + 8;
    }
}

// Run aveditor_store_cell_entries for every mode-10 cell in the current mode set.
void aveditor_store_all_mode10_cells(void)
{
    int off = 0;
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0xdd10); i = i + 1) {
        int cell = DAT_80612be4 + off + 0xdd14;
        if (*(int *)(cell + 0x80) == 10) {
            aveditor_store_cell_entries(cell);
        }
        off = off + 0x8c;
    }
}

// Compact the mode-3 (path) 6-byte node records in place: walk the mode cells top-down and, for
// each mode-3 cell, move its nodes down so they pack contiguously in the DAT_805dbdb8 table.
void aveditor_compact_path_nodes(void)
{
    __save_gpr_25();
    int cellCount = FUN_80337078(3);
    int writeIndex = FUN_80336948(3) - 1;

    if (1 < cellCount) {
        int mode = *(int *)(DAT_80612be4 + 0xdd10) + -1;
        int off = mode * 0x8c;
        for (; -1 < mode; mode = mode + -1) {
            int cell = DAT_80612be4 + off + 0xdd14;
            if (*(int *)(cell + 0x80) == 3) {
                int n = *(int *)(cell + 0x20);
                int dstByteOff = writeIndex * 6;
                while (n = n + -1, -1 < n) {
                    unsigned int dst = (unsigned int)((int)&DAT_805dbdb8 + dstByteOff);
                    writeIndex = writeIndex + -1;
                    dstByteOff = dstByteOff + -6;
                    leaf_31e8((int)&DAT_805dbdb8 + (*(short *)(cell + 0x74) + n) * 6, dst, 6);
                }
            }
            off = off + -0x8c;
        }
    }
    __restore_gpr_25();
}

// Pointer to path node `index` for cell `cell`, in the DAT_805dbdb8 table (6-byte stride, offset by
// the cell's base node index at +0x74).
int aveditor_path_node_ptr(int cell, int index)
{
    return (int)&DAT_805dbdb8 + (*(short *)(cell + 0x74) + index) * 6;
}

// Copy `byteLen`/8 six-byte node records from `src` (8-byte stride source) into the path table.
void aveditor_load_path_nodes(int src, unsigned int byteLen)
{
    unsigned int count = byteLen >> 3;
    int off = 0;
    if (count == 0) {
        return;
    }
    do {
        *(unsigned int *)((int)&DAT_805dbdb8 + off) = *(unsigned int *)(src + off);
        *(unsigned short *)((int)&DAT_805dbdbc + off) =
            *(unsigned short *)((unsigned int *)(src + off) + 1);
        count = count - 1;
        off = off + 6;
    } while (count != 0);
}

// Gather every mode-3 cell's 6-byte path nodes into the buffer at `dst` (6-byte stride).
void aveditor_gather_path_nodes(int dst)
{
    int dstOff = 0;
    int modeOff = 0;
    for (int i = 0; i < *(int *)(DAT_80612be4 + 0xdd10); i = i + 1) {
        int cell = DAT_80612be4 + modeOff + 0xdd14;
        if (*(int *)(cell + 0x80) == 3) {
            for (int j = 0; j < *(int *)(cell + 0x20); j = j + 1) {
                unsigned int *out = (unsigned int *)(dst + dstOff);
                dstOff = dstOff + 6;
                int node = *(short *)(cell + 0x74) + j;
                *out = *(unsigned int *)((int)&DAT_805dbdb8 + node * 6);
                *(unsigned short *)(out + 1) = (&DAT_805dbdbc)[node * 3];
            }
        }
        modeOff = modeOff + 0x8c;
    }
}

// Return the fixed node scratch pointer.
unsigned char *aveditor_get_node_scratch(void)
{
    return &DAT_805dc970;
}
