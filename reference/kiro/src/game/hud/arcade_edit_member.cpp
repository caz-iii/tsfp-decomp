// Batch: batch_887 | Module: arcade map-editor member/cell list management
// Address range: 0x80313cf4 - 0x80314148
// Date: 2026-09-11
//
// The member add/remove + cell alloc/append/clone layer that sits on top of the pools and grid
// built in arcade_edit_grid.cpp. Members are chained through +0x40; each member owns a cell list
// at +0x3c chained through +0x14. Cell records are 0x3c bytes; member records are 0x48 bytes.
//
//   aveditor_free_member_chain    0x80313cf4  free an entire chain of members.
//   aveditor_find_member_tail     0x80313d30  return the last member in the active list (or 0).
//   aveditor_add_member           0x80313d58  append a member to the active list + stamp the grid.
//   aveditor_finalize_member_add  0x80313da0  account serialized size of all members, then add one.
//   aveditor_create_member        0x80313e94  allocate + initialize a new member record from a def.
//   aveditor_alloc_cell           0x80313fc4  pop a cell record off the free list, init to 0xffff.
//   aveditor_append_cell          0x80314008  append a cell to the tail of a cell list.
//   aveditor_free_cell            0x80314040  unlink a cell from a member and return it to the pool.
//   aveditor_clone_cell_list      0x803140c8  clone every cell of one member's list onto another.
//   aveditor_add_cell             0x80314148  allocate + fill a cell from a source and append it.
//
// NON-MATCHING: aveditor_finalize_member_add / aveditor_create_member / aveditor_clone_cell_list /
// aveditor_add_cell are emitted by the decompiler with the compiler's _savegpr_/_restgpr_ runtime
// helpers (rendered as FUN_803826e4/e8/ec + FUN_80382730/34/38) folding their register inputs into
// an undefined8 return. Those are pure prologue/epilogue register spill with no semantic effect;
// the real ABI (recovered from the disassembly) is expressed as explicit parameters (Compile-Clean
// rules 1/3) and set on the IDB via set_function_prototype. The int<->float 0x43300000 /
// DOUBLE_8061a6e0 magic-double idiom is written as plain (float)(int) casts. member next-links
// aliased through the float array are accessed as raw 32-bit slots. aveditor_count_active_type_cells
// returns Ghidra `undefined` (gen_decls skips it); it is forward declared per-file as int. Raw
// FUN_/DAT_/DOUBLE_ get per-file externs; named leaf_31e8/aveditor_* helpers via generated_decls.h.

#include "common.h"
#include "hud.h"

extern "C" {

// member/cell pool globals
extern int   DAT_80612bfc;   // cell free-list head (next at +0x14)
extern int   DAT_80612bf8;   // cell free count
extern int   DAT_80612c14;   // active member list head (next at +0x40)
extern int   DAT_80612c18;   // active member count
extern int   DAT_80612be4;   // editor context (selection ptr at +0x2f7c)
extern int   DAT_80612be8;   // load/serialize buffer

// member type-def table + geometry constant
extern int   DAT_805d6038;   // member type -> def-record table (4-byte stride, def at +0x13c chain)
extern double DOUBLE_8061a6e0; // int->float conversion magic double

// raw FUN_ helpers not yet named
extern int  FUN_80343c4c(int typeIndex);   // per-type base cell count
extern void FUN_8030fb78(int ctx);         // recompute selection-record capacity
extern void FUN_803257a0(void* dst, int mode); // init a cell-group header

} // extern "C"

// This file's own functions + named helpers (leaf_31e8, aveditor_free_member,
// aveditor_grid_stamp_member, aveditor_alloc_member, aveditor_get_record_capacity) resolve via
// generated_decls.h. aveditor_count_active_type_cells returns Ghidra `undefined` (gen_decls skips
// it) and resolves via generated_externs.h's permissive stub - not re-declared here.

// forward prototypes for this file's helpers referenced before their definition
int  aveditor_find_member_tail(void);
void aveditor_add_member(int member);
int  aveditor_alloc_cell(void);
int  aveditor_append_cell(int listHead, int cell);

// -----------------------------------------------------------------------------

// Free an entire chain of members (walk the +0x40 links, freeing each).
void aveditor_free_member_chain(int member)
{
    while (member != 0) {
        int next = *(int*)(member + 0x40);
        aveditor_free_member((float*)(intptr_t)member);
        member = next;
    }
}

// Return the last member in the active list, or 0 when the list is empty.
int aveditor_find_member_tail(void)
{
    int m = DAT_80612c14;
    if (m == 0) {
        return 0;
    }
    while (*(int*)(m + 0x40) != 0) {
        m = *(int*)(m + 0x40);
    }
    return m;
}

// Append a member to the tail of the active list (or make it the head), then stamp the grid.
void aveditor_add_member(int member)
{
    int tail = aveditor_find_member_tail();
    if (tail != 0) {
        *(int*)(tail + 0x40) = member;
    } else {
        DAT_80612c14 = member;
    }
    aveditor_grid_stamp_member((float*)(intptr_t)member);
}

// Account the serialized byte-size of the given member's whole chain into *sizeOut (0x28 header +
// 8 per cell + per-group 0x1c), refresh the selection-record capacity, bump the active-member
// count for the trailing members, then add the head member to the list.
void aveditor_finalize_member_add(int member, int* sizeOut)
{
    int cellBytes = 0;
    int groupBytes = 0;
    for (int m = member; m != 0; m = *(int*)(m + 0x40)) {
        for (int group = *(int*)(m + 0x3c); group != 0; group = *(int*)(group + 0x14)) {
            groupBytes += 0x1c;
        }
        int base = FUN_80343c4c(*(int*)(m + 0x10));
        int active = aveditor_count_active_type_cells();
        cellBytes += (active + base) * 8 + 0x28;
    }
    if (sizeOut != 0) {
        *sizeOut += cellBytes + groupBytes;
    }
    if (*(int*)(DAT_80612be4 + 0x2f7c) != 0) {
        FUN_8030fb78(DAT_80612be8);
    }
    // count the members after the head (they are already linked), then add the head + itself
    for (int m = member; *(int*)(m + 0x40) != 0; m = *(int*)(m + 0x40)) {
        DAT_80612c18 = DAT_80612c18 + 1;
    }
    aveditor_add_member(member);
    DAT_80612c18 = DAT_80612c18 + 1;
}

// Allocate and initialize a new member record from a type def. typeIndex selects the def record;
// x/y/z are the placement coords (z passed as an int, x/y as floats); orientation/flags fill +0xc/+0x44.
void aveditor_create_member(int typeIndex, int zInt, int orientation, int flags, float x, float y)
{
    float* member = (float*)(intptr_t)aveditor_alloc_member();
    *(int*)&member[0x10] = typeIndex;
    *(int*)&member[0x11] = flags;               // +0x44
    *(unsigned char*)((char*)member + 0x14) = 0; // +0x14 = 0
    // walk the def chain: type table -> def, then +0x13c three times
    int def = *(int*)(DAT_805d6038 + typeIndex * 4);
    *(int*)&member[0xb] = def;                   // +0x2c
    *(int*)&member[0xc] = *(int*)(def + 0x13c);  // +0x30
    *(int*)&member[0xd] = *(int*)(*(int*)&member[0xc] + 0x13c); // +0x34
    *(int*)&member[0xe] = *(int*)(*(int*)&member[0xd] + 0x13c); // +0x38
    FUN_803257a0((unsigned char*)((char*)member + 0x18), 1);
    member[0] = x;
    member[1] = (float)zInt;
    member[2] = y;
    *(int*)&member[3] = orientation;             // +0xc
}

// Pop a cell record off the free list and initialize its id/coords to 0xffff / -1; 0 when empty.
int aveditor_alloc_cell(void)
{
    int* cell = (int*)DAT_80612bfc;
    if (cell == 0) {
        return 0;
    }
    DAT_80612bfc = cell[5];                       // next at +0x14
    *(unsigned short*)(cell + 3) = 0xffff;        // +0xc
    *(unsigned short*)((char*)cell + 0xe) = 0xffff;
    *(unsigned short*)(cell + 4) = 0xffff;        // +0x10
    cell[0] = -1;
    cell[1] = -1;
    DAT_80612bf8 = DAT_80612bf8 - 1;
    return (int)cell;
}

// Append a cell to the tail of a cell list (next link at +0x14). Returns the (possibly new) head.
int aveditor_append_cell(int listHead, int cell)
{
    *(int*)(cell + 0x14) = 0;
    if (listHead != 0) {
        int node = listHead;
        int next;
        while ((next = *(int*)(node + 0x14)) != 0) {
            node = next;
        }
        *(int*)(node + 0x14) = cell;
        return listHead;
    }
    return cell;
}

// Unlink a cell from a member's cell list (if present) and return it to the free list.
void aveditor_free_cell(int member, int cell)
{
    // (the leading free-list scan in the original is a no-op search; the real work is the unlink)
    if (member != 0) {
        int node = *(int*)(member + 0x3c);
        int prev = 0;
        while (node != 0 && node != cell) {
            prev = node;
            node = *(int*)(node + 0x14);
        }
        if (node == cell) {
            if (prev == 0) {
                *(int*)(member + 0x3c) = *(int*)(cell + 0x14);
            } else {
                *(int*)(prev + 0x14) = *(int*)(cell + 0x14);
            }
        }
    }
    *(int*)(cell + 0x14) = DAT_80612bfc;
    DAT_80612bfc = cell;
    DAT_80612bf8 = DAT_80612bf8 + 1;
}

// Clone every cell of srcMember's cell list onto dstMember (allocating a fresh cell each time,
// copying 0x3c bytes and resetting its id at +4 to -1).
void aveditor_clone_cell_list(int srcMember, int dstMember)
{
    int src = *(int*)(srcMember + 0x3c);
    while (src != 0) {
        int cell = aveditor_alloc_cell();
        if (cell == 0) {
            break;
        }
        leaf_31e8((unsigned)cell, (unsigned)src, 0x3c);
        if (*(int*)(cell + 4) >= 0) {
            *(int*)(cell + 4) = -1;
        }
        *(int*)(dstMember + 0x3c) = aveditor_append_cell(*(int*)(dstMember + 0x3c), cell);
        src = *(int*)(src + 0x14);
    }
}

// Allocate a cell for a member, copy 0x3c bytes from srcCell and 6 bytes of header from hdr into
// +0xc, set its value at +8 (clamped >=0), append to the member's list. Returns the new cell (or 0).
int aveditor_add_cell(int member, int hdr, int srcCell)
{
    int cell = aveditor_alloc_cell();
    if (cell == 0) {
        return 0;
    }
    if (srcCell == 0) {
        return 0;
    }
    leaf_31e8((unsigned)cell, (unsigned)srcCell, 0x3c);
    leaf_31e8((unsigned)(cell + 0xc), (unsigned)hdr, 6);
    if (*(int*)(srcCell + 8) < 0) {
        *(int*)(cell + 8) = 0;
    } else {
        *(int*)(cell + 8) = *(int*)(srcCell + 8);
    }
    *(int*)(member + 0x3c) = aveditor_append_cell(*(int*)(member + 0x3c), cell);
    return cell;
}
