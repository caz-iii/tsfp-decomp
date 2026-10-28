// =============================================================================
// weapon_def_table.cpp - weapon-definition table lookups (batch 796).
//
// The game keeps a fixed table of 0x96 (150) weapon definitions at DAT_80474680,
// each record 0x30 bytes. This module is the set of accessors/searches over that
// table, plus a couple of variant-index helpers over a secondary "variant list"
// (DAT_805cbf40 / DAT_805cc4bc / DAT_805cc4fc). Widely called from the weapon and
// HUD code (object_weapon_*, hud_*, loadtip_format).
//
// Weapon-def record layout (offsets from a DAT_80474680 entry, 0x30 stride):
//   +0x00  type id (int)          +0x03  subtype/variant (signed char, DAT_8047467d)
//   +0x14  stats/behaviour sub-record start
// The parallel short table DAT_8048ca78 (0x30 stride) holds the default HUD icon
// id per type when no subtype-specific override matches.
//
// Variant-list tables (for resolve_variant_index):
//   DAT_805cbf40  type per slot (int, stride 4)     DAT_805cc4b4  slot count
//   DAT_805cc3f0  subtype per slot (signed char)
//   DAT_805cc4bc  per-group base slot (byte)         DAT_805cc4fc  per-group length (byte)
//
//   - weapon_def_table_count (0x802c72b8): returns 0x96 (table entry count).
//   - weapon_type_is_projectile (0x802c72c0): predicate - true for the throwable/
//     projectile weapon types (0x24, 0x0a-0x0b, 0x63); else false.
//   - weapon_def_get_hud_icon (0x802c72fc): index -> HUD icon sprite id. Special-
//     cases the grenade/mine/rocket families (type 0x24/0x0b/0x0a-0x0b/0x63) by
//     subtype, otherwise returns the default icon from DAT_8048ca78.
//   - weapon_ammo_slot_offset (0x802c7460): record -> ammo-slot base offset
//     (per-group base byte + the group's slot array entry).
//   - weapon_resolve_variant_index (0x802c7488): scan the variant list for the
//     record's (type, subtype); on match write the group index (rec[0x42]) and the
//     within-group slot (rec[2+group]).
//   - weapon_def_find_record (0x802c7660): linear search the def table for
//     (type, subtype) -> record pointer (or null).
//   - weapon_def_find_stats (0x802c76b4): as find_record but returns record+0x14
//     (the stats sub-record), or null.
//   - weapon_def_find_index (0x802c76e8): linear search -> table index (or -1);
//     treats subtype -1 in the table as a wildcard match.
//   - weapon_get_hud_icon_by_type (0x802c774c): find_index(type,subtype) then
//     weapon_def_get_hud_icon.
//
// Address range: 0x802c72b8 - 0x802c7770 (excluding hud_draw_pulsing_box @0x802c7534)
// Subsystem: game (weapon definition table)
//
// NON-MATCHING: the raw DAT_* tables are accessed via typed base + byte offset
// exactly as the original; signed-char subtype reads preserved. No FP register
// artefacts in this cluster (integer table code).
// =============================================================================

#include "common.h"

extern "C" {

// weapon definition table (0x96 entries x 0x30) and companions
extern int  DAT_80474680;    // def table base: type @+0
extern char DAT_8047467d;    // def table subtype byte (base+3)
extern char DAT_8047467c;    // def table record base (base+0), used for ptr returns
extern short DAT_8048ca78;   // default HUD-icon-per-type table (0x30 stride, short)

// variant-list tables (resolve_variant_index)
extern int  DAT_805cbf40;    // variant slot type (stride 4)
extern int  DAT_805cc4b4;    // variant slot count
extern char DAT_805cc3f0;    // variant slot subtype (signed char)
extern unsigned char DAT_805cc4bc;   // per-group base slot
extern unsigned char DAT_805cc4fc;   // per-group length

// -----------------------------------------------------------------------------
// weapon_def_table_count (0x802c72b8)
// -----------------------------------------------------------------------------
unsigned int weapon_def_table_count(void)
{
    return 0x96;
}

// -----------------------------------------------------------------------------
// weapon_type_is_projectile (0x802c72c0)
// -----------------------------------------------------------------------------
unsigned int weapon_type_is_projectile(int type)
{
    if (type != 0x24) {
        if (type < 0x24) {
            if (type > 0xb || type < 10)
                return 0;
        } else if (type != 99) {
            return 0;
        }
    }
    return 1;
}

// -----------------------------------------------------------------------------
// weapon_def_get_hud_icon (0x802c72fc)
// index -> HUD icon sprite id (subtype-specific for the projectile families).
// -----------------------------------------------------------------------------
int weapon_def_get_hud_icon(unsigned int index)
{
    char subtype;
    int recByte;
    int type;

    if ((int)index < 0 || 0x95 < index)
        return 0;

    recByte = index * 0x30;
    type = (&DAT_80474680)[index * 0xc];
    if (type == 0x24) {
        subtype = (&DAT_8047467d)[recByte];
        if (subtype == '\x01') return 0xd61;
        if (subtype < '\x01' && -1 < subtype) return 0xdc7;
    } else if (type < 0x24) {
        if (type == 0xb) {
            subtype = (&DAT_8047467d)[recByte];
            if (subtype == '\x01') return 0xd45;
            if (subtype < '\x01') { if (-1 < subtype) return 0xd44; }
            else if (subtype < '\x03') return 0xd46;
        } else if (type < 0xb && 9 < type) {
            subtype = (&DAT_8047467d)[recByte];
            if (subtype == '\x02') return 0xdc6;
            if (subtype < '\x02') {
                if (subtype == '\0') return 0xdc4;
                if (-1 < subtype) return 0xdc5;
            } else if (subtype < '\x04') return 0xdc3;
        }
    } else if (type == 99) {
        subtype = (&DAT_8047467d)[recByte];
        if (subtype == '\x01') return 0xdcd;
        if (subtype < '\x01' && -1 < subtype) return 0xdcc;
    }
    return (int)*(short*)(&DAT_8048ca78 + type * 0x30);
}

// -----------------------------------------------------------------------------
// weapon_ammo_slot_offset (0x802c7460)
// record -> ammo-slot base offset (per-group base + group's slot array entry).
// -----------------------------------------------------------------------------
int weapon_ammo_slot_offset(int rec)
{
    int group = *(int*)(rec + 0x108);
    return (unsigned int)(unsigned char)(&DAT_805cc4bc)[group] +
           *(int*)(rec + group * 4 + 8);
}

// -----------------------------------------------------------------------------
// weapon_resolve_variant_index (0x802c7488)
// Scan the variant list for the record's (type, subtype); on match store the
// group index (rec[0x42]) and the within-group slot (rec[2+group]).
// -----------------------------------------------------------------------------
void weapon_resolve_variant_index(int* rec)
{
    int group = 0;
    int slot = 0;
    int typeByte = 0;
    int remaining = DAT_805cc4b4;

    if (DAT_805cc4b4 < 1)
        return;

    do {
        if (rec[0] == *(int*)((int)&DAT_805cbf40 + typeByte) &&
            (rec[1] < 0 || (int)(char)(&DAT_805cc3f0)[slot] == rec[1])) {
            rec[0x42] = group;
            rec[group + 2] = slot - (unsigned int)(unsigned char)(&DAT_805cc4bc)[group];
            return;
        }
        if ((unsigned char)(&DAT_805cc4fc)[group] - 1 ==
            slot - (unsigned int)(unsigned char)(&DAT_805cc4bc)[group]) {
            group = group + 1;
        }
        slot = slot + 1;
        typeByte = typeByte + 4;
        remaining = remaining - 1;
    } while (remaining != 0);
}

// -----------------------------------------------------------------------------
// weapon_def_find_record (0x802c7660)
// Linear search the def table for (type, subtype) -> record pointer, or null.
// -----------------------------------------------------------------------------
unsigned char* weapon_def_find_record(int type, int subtype)
{
    int off = 0;
    int remaining = 0x96;
    while (type != *(int*)((int)&DAT_80474680 + off) ||
           (-1 < subtype && subtype != (char)(&DAT_8047467d)[off])) {
        off = off + 0x30;
        remaining = remaining - 1;
        if (remaining == 0)
            return (unsigned char*)0;
    }
    return (unsigned char*)(&DAT_8047467c + off);
}

// -----------------------------------------------------------------------------
// weapon_def_find_stats (0x802c76b4)
// find_record + 0x14 (stats sub-record), or null.
// -----------------------------------------------------------------------------
unsigned char* weapon_def_find_stats(int type, int subtype)
{
    unsigned char* rec = weapon_def_find_record(type, subtype);
    if (rec == (unsigned char*)0)
        return (unsigned char*)0;
    return rec + 0x14;
}

// -----------------------------------------------------------------------------
// weapon_def_find_index (0x802c76e8)
// Linear search -> table index, or -1. Subtype -1 in the table is a wildcard.
// -----------------------------------------------------------------------------
int weapon_def_find_index(int type, int subtype)
{
    int index = 0;
    int off = 0;
    int remaining = 0x96;
    while (type != *(int*)((int)&DAT_80474680 + off) ||
           (-1 < subtype && subtype != (char)(&DAT_8047467d)[off] &&
            (char)(&DAT_8047467d)[off] != -1)) {
        index = index + 1;
        off = off + 0x30;
        remaining = remaining - 1;
        if (remaining == 0)
            return -1;
    }
    return index;
}

// -----------------------------------------------------------------------------
// weapon_get_hud_icon_by_type (0x802c774c)
// find_index(type,subtype) then weapon_def_get_hud_icon.
// -----------------------------------------------------------------------------
void weapon_get_hud_icon_by_type(int type, int subtype)
{
    unsigned int idx = (unsigned int)weapon_def_find_index(type, subtype);
    weapon_def_get_hud_icon(idx);
}

// =============================================================================
// batch 797 additions - more weapon-def accessors + the per-level weapon-set
// (loadout) builder.
//
// Extra tables:
//   DAT_80474688  def table field @+8 (int per entry, 0xc-int stride)
//   DAT_8048a710  weapon name string-id table (int per entry, 8 stride)
//   DAT_805cc53c  loadout slot array (int)      DAT_805cc584  loadout flag array (int)
//   DAT_805cc5c4  built level weapon-set buffer (count @+0, entries follow)
//   DAT_805cc5c8/0x805cc5cc  extra-weapon append list (co-op mode 0x0a)
// =============================================================================

// batch-797 externs
extern int  DAT_80474688;     // def table field @ +8
extern int  DAT_8048a710;     // weapon name string-id table (8 stride)
extern int  DAT_805cc53c;     // loadout slot array
extern int  DAT_805cc584;     // loadout flag array
extern int  DAT_805cc5c4;     // built weapon-set buffer base
extern int  DAT_805cc5c8;     // append-list count
extern int  DAT_805cc5cc;     // append-list base
extern int  DAT_80612614;     // current level id
extern int  DAT_80612be8;     // active mission/level record handle
extern char DAT_805015bd;     // game mode selector
extern int  DAT_8046ab6c;     // fallback empty weapon-set
extern int  DAT_8046a908;     // canned weapon-set table base (0x44 stride)

// helpers used by the loadout builder
extern int  get_string_by_id(int id);
extern int* FUN_80339b90(int handle, int key);       // mission-record lookup (raw)
extern void memset_wrapper(int dst, int val, int len);
extern void leaf_31e8(int dst, int src, int len);    // small block copy (raw helper)

// forward
void weapon_expand_short_table(unsigned short* out, unsigned char* packed);

// -----------------------------------------------------------------------------
// weapon_def_get_field2 (0x802c7770): def-table field at +8 for a table index.
// -----------------------------------------------------------------------------
unsigned int weapon_def_get_field2(int index)
{
    return (&DAT_80474688)[index * 0xc];
}

// -----------------------------------------------------------------------------
// weapon_def_find_index_frameactor (0x802c7788): search the def table for the
// current frame-actor's (type, subtype). The type/subtype pair comes from the
// PPC frame-helper register pair; expressed here as explicit params.
// -----------------------------------------------------------------------------
int weapon_def_find_index_frameactor(int subtype, int type)
{
    unsigned int index = 0;
    int off = 0;
    do {
        if (type == *(int*)((int)&DAT_80474680 + off) &&
            (weapon_type_is_projectile(type) == 0 ||
             subtype == (int)(char)(&DAT_8047467d)[off]))
            break;
        index = index + 1;
        off = off + 0x30;
    } while (index < 0x96);
    return (int)index;
}

// -----------------------------------------------------------------------------
// weapon_def_table_count2 (0x802c7838): returns 0x96 (alt accessor).
// -----------------------------------------------------------------------------
unsigned int weapon_def_table_count2(void)
{
    return 0x96;
}

// -----------------------------------------------------------------------------
// weapon_always_enabled_stub (0x802c7840): returns 1.
// -----------------------------------------------------------------------------
unsigned int weapon_always_enabled_stub(void)
{
    return 1;
}

// -----------------------------------------------------------------------------
// weapon_get_name_string (0x802c7848): look up the weapon's localized name via
// the string-id table DAT_8048a710 (8 stride).
// -----------------------------------------------------------------------------
void weapon_get_name_string(int index)
{
    get_string_by_id(*(int*)(&DAT_8048a710 + index * 8));
}

// -----------------------------------------------------------------------------
// weapon_loadout_slot_get (0x802c787c): loadout slot array read.
// -----------------------------------------------------------------------------
unsigned int weapon_loadout_slot_get(int index)
{
    return (&DAT_805cc53c)[index];
}

// -----------------------------------------------------------------------------
// weapon_loadout_flag_get (0x802c7890): loadout flag array read.
// -----------------------------------------------------------------------------
unsigned int weapon_loadout_flag_get(int index)
{
    return (&DAT_805cc584)[index];
}

// -----------------------------------------------------------------------------
// weapon_arcade_mode_check (0x802c78a4): on the arcade level (id 100), returns 0
// unless the mission record's flag 0x8000 is set; otherwise 1.
// -----------------------------------------------------------------------------
unsigned int weapon_arcade_mode_check(void)
{
    if (DAT_80612614 == 100) {
        int* rec = FUN_80339b90(DAT_80612be8, 0xc77668);
        if ((rec[0x8e] & 0x8000U) == 0)
            return 0;
    }
    return 1;
}

// -----------------------------------------------------------------------------
// weapon_expand_short_table (0x802c78f4): expand a packed short array (count byte
// at packed[0], shorts from packed[2]) into the int-array form at out (count at
// out[2..3], ints from out[8]).
// -----------------------------------------------------------------------------
void weapon_expand_short_table(unsigned short* out, unsigned char* packed)
{
    int srcByte = 0;
    int dstByte = 0;
    int i;

    out[0] = 0;
    *(unsigned int*)(out + 2) = (unsigned int)*packed;
    for (i = 0; i < (int)(unsigned int)*packed; i = i + 1) {
        *(int*)((int)out + dstByte + 8) = (int)*(short*)(packed + srcByte + 2);
        srcByte = srcByte + 2;
        dstByte = dstByte + 4;
    }
}

// -----------------------------------------------------------------------------
// weapon_build_level_loadout (0x802c793c): build the current level's weapon-set
// list into DAT_805cc5c4. Looks up the mission record; if none, returns the empty
// fallback set. For a set id < 10 it copies a canned table entry, otherwise it
// expands the record's packed short table. In co-op (mode 0x0a) it appends weapon
// id 0x38 to the extra-list.
// -----------------------------------------------------------------------------
unsigned char* weapon_build_level_loadout(void)
{
    int* rec = FUN_80339b90(DAT_80612be8, 0xc77668);
    if (rec == (int*)0)
        return (unsigned char*)&DAT_8046ab6c;

    short setId = *(short*)(rec + 0x10);
    memset_wrapper((int)&DAT_805cc5c4, 0, 0x44);
    if (setId < 10)
        leaf_31e8((int)&DAT_805cc5c4, (setId + 0xc) * 0x44 + (int)&DAT_8046a908, 0x44);
    else
        weapon_expand_short_table((unsigned short*)&DAT_805cc5c4, (unsigned char*)((int)rec + 0x42));

    if (DAT_805015bd == '\n') {
        int slot = DAT_805cc5c8 * 4;
        DAT_805cc5c8 = DAT_805cc5c8 + 1;
        *(int*)(&DAT_805cc5cc + slot) = 0x38;
    }
    return (unsigned char*)&DAT_805cc5c4;
}

} // extern "C"
