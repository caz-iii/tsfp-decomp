// Batch: batch_884 | Module: arcade map-editor query/label helpers
// Address range: 0x80312e34 - 0x80313228
// Date: 2026-09-11
//
// A block of small query/lookup helpers used by the arcade map-editor screen. They resolve
// the world position of a placed entry, report whether the map has any content, walk the
// member/cell lists to find a cell record by id, and resolve the display label/tooltip strings
// for palette and weapon-loadout entries.
//
//   aveditor_get_entry_position        0x80312e34  packed (x,y,z) world pos of a cell/roster id.
//   aveditor_get_active_segment        0x80312f6c  normalized active visible-segment index.
//   aveditor_find_cell_record_by_id    0x80312f98  find a cell record by id across the member list.
//   aveditor_has_placed_cell           0x80312fdc  any cell with a valid id in one/all members.
//   aveditor_has_any_content           0x80313050  editor has cells, a visible segment, or roster.
//   aveditor_get_palette_entry_label   0x803130bc  resolve label string for a palette entry type.
//   aveditor_build_palette_entry_label 0x80313154  build the label for a palette entry (fallback).
//   aveditor_get_loadout_slot_label    0x803131a4  weapon-loadout slot label via hud icon type.
//   aveditor_get_loadout_field         0x803131ec  read a field from the active loadout record.
//   aveditor_get_loadout_palette_record 0x80313228 palette record pointer for a loadout field.
//
// NON-MATCHING: aveditor_get_entry_position returns two packed 16.16 words (a CONCAT of two
// shorts in the high half, one short in the low half); it is expressed here with a small
// packing helper returning u64 so the register-pair return is a real C type (Compile-Clean
// rules 1/3). The int<->float conversions the decompiler renders via the 0x43300000 /
// DOUBLE_8061a5e0 magic-double idiom are written as plain (float)(int) casts. Two named
// callees are declared `void` in generated_decls.h but their register return is consumed
// (aselgrid_find_visible_segment -> int, weapon_get_hud_icon_by_type -> int); they are invoked
// through a typed function-pointer cast at the call site per Compile-Clean rule 6 rather than
// re-declared. Named helpers resolve via generated_decls.h; every raw FUN_/DAT_/DOUBLE_ symbol
// gets a per-file extern below.

#include "common.h"
#include "hud.h"

extern "C" {

// editor context / member-list globals
extern int   DAT_80612be4;    // roster array base (0xc stride entries at +0x3070)
extern int   DAT_80612c14;    // member list head (chain via +0x40, cells via +0x3c/+0x14)

// palette label table + loadout palette record base
extern short DAT_80477bc8;    // palette-entry descriptor table (0xc4 stride, +0 = string id)
extern int   DAT_8043f294;    // loadout palette record base (0x15-int stride)

// int->float conversion magic double (0x4330000080000000 bias)
extern double DOUBLE_8061a5e0;

// raw FUN_ helpers not yet named / not in generated_decls.h
extern void FUN_80342564(unsigned short* out);   // zero a default position record
extern int  FUN_80339488(void);                  // roster/select list non-empty predicate
extern int  FUN_803204e4(void);                   // active loadout slot index
extern int  FUN_8031327c(int index);              // palette label: category B
extern int  FUN_803132f8(int index);              // palette label: category C

} // extern "C"

// This batch's own functions and the named helpers (get_string_by_id,
// weapon_get_hud_icon_by_type, menu_slot_option_table_resolve,
// mode_loadout_get_active_record, aselgrid_find_visible_segment,
// aveditor_find_cell_by_id, aveditor_find_roster_slot) are declared in
// generated_decls.h (via hud.h) at C++ linkage and must NOT be re-declared here.

// forward prototypes for this file's helpers referenced before their definition
// (C++ linkage, matching what gen_decls.ps1 emits into generated_decls.h)
int  aveditor_get_active_segment(void);
void aveditor_get_loadout_slot_label(int slot);
undefined4 aveditor_get_loadout_field(int index);

// -----------------------------------------------------------------------------

// Pack three shorts into the game's (hi:x,y | lo:z<<16) return layout.
static u64 aveditor_pack_pos(short x, short y, short z)
{
    unsigned int hi = ((unsigned int)(unsigned short)x << 16) | (unsigned short)y;
    unsigned int lo = (unsigned int)(unsigned short)z << 16;
    return ((u64)hi << 32) | lo;
}

// Return the packed world position of a placed cell or roster entry by id.
// For a cell, the position is the cell's float coords plus the owning member's short offset.
u64 aveditor_get_entry_position(int entryId)
{
    float* cellPos;
    int    member;

    member = aveditor_find_cell_by_id((int*)&cellPos, entryId);
    if (member != 0) {
        short* off = (short*)member;
        short x = (short)(int)(cellPos[0] + (float)(int)off[6]);   // +0xc
        short y = (short)(int)(cellPos[1] + (float)(int)off[7]);   // +0xe
        short z = (short)(int)(cellPos[2] + (float)(int)off[8]);   // +0x10
        return aveditor_pack_pos(x, y, z);
    }

    int slot = aveditor_find_roster_slot(entryId);
    if (slot < 0) {
        unsigned short def[2];
        FUN_80342564(def);
        return aveditor_pack_pos((short)def[0], (short)def[1], 0);
    }

    int rec = DAT_80612be4 + slot * 0xc;
    short* p = (short*)(rec + 0x3070);
    return aveditor_pack_pos(p[0], p[1], p[2]);
}

// Normalized active visible-segment index (the source returns idx, or clamps -1 to a stable value).
int aveditor_get_active_segment(void)
{
    int seg = ((int (*)(void))aselgrid_find_visible_segment)();
    int neg = -seg + -1;
    return neg - (((seg == -1) ? 1 : 0) + -seg + -2);
}

// Find a cell record with the given id by walking each member's cell chain. Returns 0 if none.
int aveditor_find_cell_record_by_id(int cellId)
{
    for (int member = DAT_80612c14; member != 0; member = *(int*)(member + 0x40)) {
        for (int cell = *(int*)(member + 0x3c); cell != 0; cell = *(int*)(cell + 0x14)) {
            if (*(int*)(cell + 4) == cellId) {
                return cell;
            }
        }
    }
    return 0;
}

// True if any placed cell has a valid (>=0) id. allMembers==0 scans only the given member.
int aveditor_has_placed_cell(int member, int allMembers)
{
    if (allMembers == 0) {
        for (int cell = *(int*)(member + 0x3c); cell != 0; cell = *(int*)(cell + 0x14)) {
            if (*(int*)(cell + 4) >= 0) {
                return 1;
            }
        }
    } else {
        for (; member != 0; member = *(int*)(member + 0x40)) {
            for (int cell = *(int*)(member + 0x3c); cell != 0; cell = *(int*)(cell + 0x14)) {
                if (*(int*)(cell + 4) >= 0) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

// True if the editor map has any content: a placed cell, an active segment, or roster entries.
int aveditor_has_any_content(int member)
{
    if (aveditor_has_placed_cell(member, 0) != 0) return 1;
    if (aveditor_get_active_segment() != 0) return 1;
    if (FUN_80339488() != 0) return 1;
    return 0;
}

// Resolve the display label string id for a palette entry, dispatching by type range.
int aveditor_get_palette_entry_label(int type)
{
    if (type < 0x21) {
        if (0x16 < type) {
            if (type < 0x1d) {
                return FUN_803132f8(type - 0x17);
            }
            return get_string_by_id((int)*(&DAT_80477bc8 + type * 0x62));  // 0xc4 bytes = 0x62 shorts
        }
        if (0x10 < type) {
            return FUN_8031327c(type - 0x11);
        }
    } else if (type < 0x42 && 0x37 < type) {
        // loadout-slot label resolves through get_string_by_id inside the helper (void return);
        // the original treats it as a "handled" branch, so report a non-zero handled marker.
        aveditor_get_loadout_slot_label(type - 0x38);
        return 1;
    }
    return 0;
}

// Build the label for a palette entry, falling back to the raw table string when unresolved.
void aveditor_build_palette_entry_label(int* entry)
{
    if (aveditor_get_palette_entry_label(*entry) == 0) {
        get_string_by_id((int)*(&DAT_80477bc8 + *entry * 0x62));
    }
}

// Resolve the label string for a weapon-loadout slot via its hud-icon type/subtype.
void aveditor_get_loadout_slot_label(int slot)
{
    int  active = FUN_803204e4();
    int* opts   = menu_slot_option_table_resolve(active);
    int icon = ((int (*)(int, int))weapon_get_hud_icon_by_type)
                   ((int)*((short*)opts + slot * 2 + 1),
                    (int)*(short*)((int)opts + slot * 8 + 10));
    get_string_by_id(icon);
}

// Read a 4-byte field from the active loadout record (base + 8 + index*4).
undefined4 aveditor_get_loadout_field(int index)
{
    unsigned char* rec = mode_loadout_get_active_record();
    return *(undefined4*)(rec + index * 4 + 8);
}

// Return the palette record pointer selected by a loadout field value (0x15-int stride).
undefined4* aveditor_get_loadout_palette_record(int index)
{
    int sel = (int)aveditor_get_loadout_field(index);
    return (undefined4*)&DAT_8043f294 + sel * 0x15;
}
