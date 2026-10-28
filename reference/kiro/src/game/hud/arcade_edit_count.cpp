// Batch: batch_888 | Module: arcade map-editor cell counting / sizing / paste-buffer alloc
// Address range: 0x803141f0 - 0x80314480
// Date: 2026-09-12
//
// The counting + sizing layer that sits on top of the member/cell model built in
// arcade_edit_member.cpp (members chained through +0x40, each owning a cell list at +0x3c whose
// cells chain through +0x14 / word index 5). These helpers answer "how many cells of type X are
// placed", "how big will the map serialize", and allocate the paste/clipboard scratch buffer.
//
//   aveditor_find_cell_by_coord_or_type 0x803141f0  find a member's cell matching a coord or a type.
//   aveditor_count_active_members       0x8031424c  count members in the active list.
//   aveditor_count_cells_of_type        0x80314280  count cells in one list matching a type (<0=all).
//   aveditor_get_special_type_count     0x803142bc  editor-context special counters for type 0x46/0x47.
//   aveditor_count_placed_cells_of_type 0x803142f4  sum count_cells_of_type across all active members.
//   aveditor_count_placed_for_type      0x80314358  resolve the placed-count for a layout entry type.
//   aveditor_tally_cell_categories      0x803143e0  histogram a cell list by per-type category id.
//   aveditor_show_type_label            0x8031441c  show the label string for a type-table entry.
//   aveditor_piece_serialized_size      0x8031444c  serialized byte size of one piece type.
//   aveditor_alloc_paste_buffer         0x80314480  allocate + zero the paste/clipboard buffer.
//
// NON-MATCHING notes:
//  * aveditor_count_placed_cells_of_type (0x803142f4) is emitted by the decompiler with the
//    compiler's _savegpr_/_restgpr_ runtime helpers (FUN_803826f0 / FUN_8038273c) spilling r29-r31,
//    which drops its incoming register argument. The real ABI recovered from the disassembly is a
//    single int typeFilter in r3; it is expressed as an explicit parameter (Compile-Clean rules 1/3)
//    and set on the IDB via set_function_prototype. Its callers (0x80314358) pass 0x35 / 0x36 / the
//    layout type; the raw decompile rendered those as argument-less calls.
//  * aveditor_alloc_paste_buffer (0x80314480) is emitted with mem_alloc's full 16-arg PPC spill
//    list; the real prototype mem_alloc(size,type) collapses the call to mem_alloc(size,1). Written
//    with the true two-argument form.
//  * Cell/member links live in raw 32-bit slots; the cell "next" is word index 5 (+0x14) and the
//    coord/type fields are read as shorts at +0xc/+0xe/+0x10 and a word at +0x0.
//
// Raw FUN_/DAT_ symbols get correctly-typed per-file externs below. Named cross-module helpers
// (avanim_layout_is_0x22, get_string_by_id, mem_alloc, memset_wrapper) and this batch's own
// functions resolve through the shared headers (generated_decls.h / generated_externs.h).

#include "common.h"
#include "hud.h"

extern "C" {

// -- active member list (built by arcade_edit_grid.cpp / arcade_edit_member.cpp) --
extern int DAT_80612c14;   // active member list head (next link at +0x40)
extern int DAT_80612c18;   // active member count guard

// -- editor context + paste buffer globals --
extern int DAT_80612be4;   // editor context base (special counters at +0x3068 / +0x306a as shorts)
extern int DAT_80612c28;   // paste/clipboard buffer pointer
extern int DAT_80612c24;   // paste buffer used-size / cursor

// -- static type tables --
extern short DAT_80477bca;  // category table: short at (&DAT_80477bca)[type*0x62] -> category id
extern int   DAT_80477850;  // type label table: string id at (&DAT_80477850 + type*0x18)
extern int   DAT_8047c15c;  // piece geometry table (stride 0x140): depth
extern int   DAT_8047c160;  // piece geometry table (stride 0x140): cols
extern int   DAT_8047c164;  // piece geometry table (stride 0x140): rows

// -- raw helper not yet named: layout-type-9 placed count --
extern int FUN_80328888(void);

} // extern "C"

// Find a cell within a member's cell list that either matches an (x,y,z) short coord triple or,
// failing that, whose type word (+0x0) equals typeId. Returns the cell pointer, or 0 if none.
// (member+0x3c is the cell list head; cells chain through word index 5 / +0x14.)
int *aveditor_find_cell_by_coord_or_type(int member, short *coord, int typeId)
{
    int *cell;

    cell = *(int **)(member + 0x3c);
    while (cell != (int *)0x0) {
        if ((*(short *)((int)cell + 0xc) == coord[0]) &&
            (*(short *)((int)cell + 0xe) == coord[1]) &&
            (*(short *)((int)cell + 0x10) == coord[2])) {
            return cell;
        }
        if (*cell == typeId) {
            return cell;
        }
        cell = (int *)cell[5];
    }
    return (int *)0x0;
}

// Count the members in the active list (walk DAT_80612c14 via +0x40). Guarded by DAT_80612c18.
int aveditor_count_active_members(void)
{
    int count;
    int member;

    count = 0;
    member = DAT_80612c14;
    if (DAT_80612c18 != 0) {
        for (; member != 0; member = *(int *)(member + 0x40)) {
            count = count + 1;
        }
        return count;
    }
    return 0;
}

// Count cells in one cell list. typeFilter < 0 counts every cell; otherwise counts cells whose
// type word (+0x0) equals typeFilter.
int aveditor_count_cells_of_type(int *cellList, int typeFilter)
{
    int count;

    count = 0;
    for (; cellList != (int *)0x0; cellList = (int *)cellList[5]) {
        if (typeFilter < 0) {
            count = count + 1;
        }
        else if (*cellList == typeFilter) {
            count = count + 1;
        }
    }
    return count;
}

// Editor-context special counters for the two "special" layout types 0x46 / 0x47 (shorts held in
// the context at +0x3068 / +0x306a). Any other type returns 0.
unsigned short aveditor_get_special_type_count(int type)
{
    unsigned short value;

    value = 0;
    if (type == 0x47) {
        value = *(unsigned short *)(DAT_80612be4 + 0x306a);
    }
    else if ((type < 0x47) && (0x45 < type)) {
        value = *(unsigned short *)(DAT_80612be4 + 0x3068);
    }
    return value;
}

// Sum aveditor_count_cells_of_type(typeFilter) across every active member's cell list.
// (NON-MATCHING: register arg dropped by the _savegpr_ spill; recovered as an explicit param.)
int aveditor_count_placed_cells_of_type(int typeFilter)
{
    int total;
    int member;

    total = 0;
    for (member = DAT_80612c14; member != 0; member = *(int *)(member + 0x40)) {
        if (*(int **)(member + 0x3c) != (int *)0x0) {
            total = total + aveditor_count_cells_of_type(*(int **)(member + 0x3c), typeFilter);
        }
    }
    return total;
}

// Resolve the placed-count for a layout entry type. Special-cased for the palette types (0x22
// family via avanim_layout_is_0x22 -> special counters), the "layout type 9" count, the 0x35/0x36
// pair (summed), and everything else counted by its own type.
unsigned int aveditor_count_placed_for_type(int type)
{
    unsigned int result;
    int a;
    int b;

    result = avanim_layout_is_0x22(type);
    if (result == 0) {
        if (type < 0x35) {
            if (type == 9) {
                result = FUN_80328888();
                return result;
            }
        }
        else if (type < 0x37) {
            a = aveditor_count_placed_cells_of_type(0x36);
            b = aveditor_count_placed_cells_of_type(0x35);
            return b + a;
        }
        result = aveditor_count_placed_cells_of_type(type);
    }
    else {
        result = (unsigned int)aveditor_get_special_type_count(type);
    }
    return result;
}

// Histogram a cell list by per-type category id. Each cell's type word indexes the category table
// (&DAT_80477bca, entry stride 0x62 shorts) to a category id, and histogram[category] is bumped.
void aveditor_tally_cell_categories(int *cellList, int *histogram)
{
    for (; cellList != (int *)0x0; cellList = (int *)cellList[5]) {
        int category = (&DAT_80477bca)[*cellList * 0x62];
        histogram[category] = histogram[category] + 1;
    }
    return;
}

// Show the label string for a type-table entry (string id lives at &DAT_80477850 + type*0x18).
void aveditor_show_type_label(int type)
{
    get_string_by_id(*(int *)(&DAT_80477850 + type * 0x18));
    return;
}

// Serialized byte size of one piece type: rows*cols*depth cells at 4 bytes each, plus a 0x140-byte
// header. Geometry read from the piece table (stride 0x140).
int aveditor_piece_serialized_size(int pieceType)
{
    int base;

    base = pieceType * 0x140;
    return *(int *)(&DAT_8047c164 + base) *
           *(int *)(&DAT_8047c160 + base) *
           *(int *)(&DAT_8047c15c + base) * 4 + 0x140;
}

// Allocate + zero the paste/clipboard scratch buffer, sized as 3x the sum of every piece type's
// serialized size (0..0x2c), rounded up to a 0x10 boundary. Stores the buffer at DAT_80612c28 and
// resets the used-size cursor DAT_80612c24.
// (NON-MATCHING: mem_alloc's 16-arg PPC spill collapsed to mem_alloc(size,1).)
void aveditor_alloc_paste_buffer(void)
{
    unsigned int size;
    int pieceSize;
    int type;
    int total;

    total = 0;
    type = 0;
    do {
        pieceSize = aveditor_piece_serialized_size(type);
        type = type + 1;
        total = total + pieceSize * 3;
    } while (type < 0x2d);

    size = (total + 0xfU) & 0xfffffff0;
    DAT_80612c28 = (int)mem_alloc(size, 1);
    memset_wrapper((void *)DAT_80612c28, 0, size);
    DAT_80612c24 = 0;
    return;
}
