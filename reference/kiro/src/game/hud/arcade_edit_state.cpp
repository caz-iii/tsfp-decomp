// arcade_edit_state.cpp
// Arcade map-editor small state accessors: current weapon-set selector, the
// weapon-set descriptor table, and the placed-member tally record array.
//
// Globals:
//   DAT_805d59ac : current editor weapon-set / config selector (get/set)
//   DAT_8047ff80 : weapon-set descriptor table (u32[2] stride; id at [i*2])
//   DAT_805d6358 : placed-member tally record array (15 slots)
//
// Consumers: aveditor_preload_weapon_resources, aveditor_serialize_map,
//            aveditor_deserialize_map, aveditor_tally_and_trigger,
//            asel_reset_screen, aveditor_reset_config_fields.
//
// Address: 0x8031d6cc..0x8031d71c | Batch: batch_897 | Date: 2026-09-12

#include "common.h"

// ---- raw symbols referenced by this translation unit ----
extern unsigned int  DAT_805d59ac;     // current weapon-set / config selector
extern unsigned int  DAT_8047ff80;     // weapon-set descriptor table base (u32[2] stride)
extern unsigned int  DAT_805d6358;     // placed-member tally record array base (15 slots)

// 0x8031d6cc: number of weapon sets available in the editor (constant 11).
int aveditor_weapon_set_count(void)
{
    return 0xb;
}

// 0x8031d6d4: weapon-set id for set index i (word at table[i*2]).
unsigned int aveditor_weapon_set_id(int index)
{
    return (&DAT_8047ff80)[index * 2];
}

// 0x8031d6e8: read the current editor weapon-set / config selector.
unsigned int aveditor_get_current_weapon_set(void)
{
    return DAT_805d59ac;
}

// 0x8031d6f8: set the current editor weapon-set / config selector.
void aveditor_set_current_weapon_set(unsigned int value)
{
    DAT_805d59ac = value;
    return;
}

// 0x8031d708: number of placed-member tally slots (constant 15).
int aveditor_tally_slot_count(void)
{
    return 0xf;
}

// 0x8031d710: base pointer of the placed-member tally record array.
unsigned int *aveditor_tally_record(void)
{
    return &DAT_805d6358;
}

// 0x8031d71c: read placed-member tally slot i.
unsigned int aveditor_tally_slot(int index)
{
    return (&DAT_805d6358)[index];
}
