// hitrec_table.cpp
// Per-match hit-record table, recovered in batch_809. Registered as a set of
// match event handlers by match_register_event_handlers (0x801a6ea8/ed8): a
// reset handler (0x8023d13c), a per-frame flush handler (0x8023d1a8, currently a
// no-op retained by the original build), and an entry-register callback
// (0x8023d1ac) invoked from the swept-cast / hitscan collision paths
// (collision_entity_swept_cast, projectile_hitscan_room_entities, ...).
//
// Storage:
//   DAT_80612968 -> record pool base   (0x7500 bytes; cleared on reset)
//   DAT_80612970 -> index/handle table (0x50 bytes; cleared on reset)
//   DAT_80612974  = live entry count   (also the next handle id)
//   DAT_8061296c  = secondary counter  (reset to 0)
//
// NON-MATCHING decompilation.

#include "common.h"

// ---------------------------------------------------------------------------
// Per-file externs for raw ROM symbols.
// ---------------------------------------------------------------------------
extern int  DAT_80612968;   // hit-record pool base pointer
extern int  DAT_8061296c;   // secondary counter (reset to 0)
extern int  DAT_80612970;   // index/handle table base pointer
extern int  DAT_80612974;   // live entry count / next handle id

extern void memset_wrapper(int dst, int val, int n);

// ---------------------------------------------------------------------------
// 0x8023d13c  hitrec_table_reset
// Match-start reset handler: zero both counters and clear the record pool
// (0x7500 bytes) and the index table (0x50 bytes).
// ---------------------------------------------------------------------------
void hitrec_table_reset(void)
{
    DAT_8061296c = 0;
    DAT_80612974 = 0;
    memset_wrapper(DAT_80612968, 0, 0x7500);
    memset_wrapper(DAT_80612970, 0, 0x50);
}

// ---------------------------------------------------------------------------
// 0x8023d1a8  hitrec_frame_flush_noop
// Per-frame flush handler slot. Empty in the shipped build (single blr).
// ---------------------------------------------------------------------------
void hitrec_frame_flush_noop(void)
{
}

// ---------------------------------------------------------------------------
// 0x8023d1ac  hitrec_entry_register
// Register a new hit-record entry for the given collision/hitscan owner block.
// Grabs the next 8-byte index-table slot (DAT_80612970 + count*8), stores a
// back-pointer to it in owner+0x7c, zeroes the slot, writes the slot's handle
// id (== current count) into its first word, and bumps the live count.
// ---------------------------------------------------------------------------
void hitrec_entry_register(int owner)
{
    int *slot = (int *)(DAT_80612970 + DAT_80612974 * 8);

    *(int **)(owner + 0x7c) = slot;
    memset_wrapper((int)slot, 0, 8);
    slot[0] = DAT_80612974;
    DAT_80612974 = DAT_80612974 + 1;
}
