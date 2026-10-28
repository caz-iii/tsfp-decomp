// Batch: batch_862 | Module: entity/results-column descriptor table accessors
// Address range: 0x803009d0 - 0x80300b30
//
// Field accessors over the per-column "results-column config" descriptor table
// reached through arcsel_get_results_column_cfg (0x80300968, batch_861). That
// helper returns a u32* record = &DAT_80477bd4 + column*0x31 (0xc4-byte stride),
// with an optional flag-0x4 indirection via a record lookup. These ten leaf
// accessors read individual fields/flags of that record and are called all over
// the level/entity spawn path (level_spawn_all_entities_pass @0x8023xxxx,
// worm_objective_sync_state, the prop/soundpad loaders), where the "column" index
// is really an entity/prop type id (0..0x50) and the record is the type's spawn
// descriptor.
//
// Record layout (u32* base returned by arcsel_get_results_column_cfg):
//   word[0] (+0x00)  primary def/class id       (entdesc_def_id_cur / _alt / _alt2)
//   word[1] (+0x04)  secondary field            (entdesc_field1_cur)
//   word[3] (+0x0c)  position/transform code*   (entdesc_pos_callback)
//   byte @+0x08 bit0 flag                        (entdesc_flag_bit0*)
//   byte @+0x08 bit1 flag                        (entdesc_flag_bit1*)
// entdesc_type_id reads the parallel table DAT_80477c88 (record+0xc0, direct index
// by type id, no flag indirection) - the raw spawn/model type id.
//
// The "_cur" accessors implicitly use the current stream record key DAT_80612be8;
// the two "_key" variants (0x80300aa0/0x80300ac8) take an explicit key pointer.
//
// arcsel_get_results_column_cfg is a named cross-module helper resolved through the
// shared generated headers; only the raw DAT_ tables get per-file externs.
//
// NON-MATCHING: entdesc_pos_callback returns a raw function pointer (code*) as
// void*; callers cast and invoke it. Integer table code, no FP artefacts.

#include "common.h"

extern "C" {

// current stream/record key consumed by arcsel_get_results_column_cfg
extern int* DAT_80612be8;

// direct type-id descriptor table (record+0xc0), 0xc4-byte stride
extern int DAT_80477c88;

// named cross-module helper (defined in arcade_select_screen_nav.cpp, batch_861)
u32* arcsel_get_results_column_cfg(int column, int* pRecordKey);

// ---------------------------------------------------------------------------
// 0x803009d0  entdesc_def_id_cur
// Primary def/class id (word[0]) of the current-key descriptor for a type id.
// ---------------------------------------------------------------------------
u32 entdesc_def_id_cur(int typeId)
{
    u32* rec = arcsel_get_results_column_cfg(typeId, DAT_80612be8);
    return *rec;
}

// ---------------------------------------------------------------------------
// 0x803009f8  entdesc_def_id_cur_alt
// Identical accessor (separate call site) - word[0] of current-key descriptor.
// ---------------------------------------------------------------------------
u32 entdesc_def_id_cur_alt(int typeId)
{
    u32* rec = arcsel_get_results_column_cfg(typeId, DAT_80612be8);
    return *rec;
}

// ---------------------------------------------------------------------------
// 0x80300a20  entdesc_def_id_cur_alt2
// Identical accessor (separate call site) - word[0] of current-key descriptor.
// ---------------------------------------------------------------------------
u32 entdesc_def_id_cur_alt2(int typeId)
{
    u32* rec = arcsel_get_results_column_cfg(typeId, DAT_80612be8);
    return *rec;
}

// ---------------------------------------------------------------------------
// 0x80300a48  entdesc_flag_bit0_cur
// Test flag bit 0x1 in the descriptor byte at +0x08 (current key).
// ---------------------------------------------------------------------------
unsigned char entdesc_flag_bit0_cur(int typeId)
{
    u32* rec = arcsel_get_results_column_cfg(typeId, DAT_80612be8);
    return *(unsigned char*)(rec + 2) & 1;
}

// ---------------------------------------------------------------------------
// 0x80300a74  entdesc_flag_bit1_cur
// Test flag bit 0x2 in the descriptor byte at +0x08 (current key).
// ---------------------------------------------------------------------------
unsigned char entdesc_flag_bit1_cur(int typeId)
{
    u32* rec = arcsel_get_results_column_cfg(typeId, DAT_80612be8);
    return *(unsigned char*)(rec + 2) & 2;
}

// ---------------------------------------------------------------------------
// 0x80300aa0  entdesc_flag_bit0_key
// Test flag bit 0x1 at +0x08 using an explicit record key.
// ---------------------------------------------------------------------------
unsigned char entdesc_flag_bit0_key(int typeId, int* pRecordKey)
{
    u32* rec = arcsel_get_results_column_cfg(typeId, pRecordKey);
    return *(unsigned char*)(rec + 2) & 1;
}

// ---------------------------------------------------------------------------
// 0x80300ac8  entdesc_flag_bit1_key
// Test flag bit 0x2 at +0x08 using an explicit record key.
// ---------------------------------------------------------------------------
unsigned char entdesc_flag_bit1_key(int typeId, int* pRecordKey)
{
    u32* rec = arcsel_get_results_column_cfg(typeId, pRecordKey);
    return *(unsigned char*)(rec + 2) & 2;
}

// ---------------------------------------------------------------------------
// 0x80300af0  entdesc_type_id
// Raw spawn/model type id from the parallel direct-index table (record+0xc0),
// 0xc4-byte stride, no flag indirection.
// ---------------------------------------------------------------------------
u32 entdesc_type_id(int typeId)
{
    return *(u32*)((int)&DAT_80477c88 + typeId * 0xc4);
}

// ---------------------------------------------------------------------------
// 0x80300b08  entdesc_pos_callback
// Position/transform callback pointer (word[3], +0x0c) of the current-key
// descriptor; callers cast to a code* and invoke it.
// ---------------------------------------------------------------------------
void* entdesc_pos_callback(int typeId)
{
    u32* rec = arcsel_get_results_column_cfg(typeId, DAT_80612be8);
    return (void*)rec[3];
}

// ---------------------------------------------------------------------------
// 0x80300b30  entdesc_field1_cur
// Secondary field (word[1], +0x04) of the current-key descriptor.
// ---------------------------------------------------------------------------
u32 entdesc_field1_cur(int typeId)
{
    u32* rec = arcsel_get_results_column_cfg(typeId, DAT_80612be8);
    return rec[1];
}

} // extern "C"
