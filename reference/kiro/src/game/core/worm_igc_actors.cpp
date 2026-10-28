// Module: worm_igc_actors
// TSFP GameCube main.dol non-matching decompilation.
//
// In-game-cutscene (IGC) actor registry for the worm subsystem. Manages a
// fixed table of 24 actor slots (0x5c-byte stride) keyed by an entity/actor id
// in the first field, plus a parallel per-slot active bitfield (DAT_80612944).
// Driven by the worm event system (worm_event_execute / worm_event_tick_driver).
//
// Batch: batch_706  |  Addresses: 0x80238f9c - 0x802390b3

#include "common.h"

// ---------------------------------------------------------------------------
// Raw symbols referenced by this translation unit (file-local externs).
// ---------------------------------------------------------------------------

// 24-slot IGC actor table, 0x5c-byte stride. First field of each slot is the
// actor's entity id. The symbol is mislabeled as a string in the IDB
// ("CTL7pak/igcs/igcs1") but is used here as the table base.
extern unsigned char s_CTL7pak_igcs_igcs1_80465aa8[];

// Parallel per-slot "active" bitfield (bit N set => slot N in use).
extern unsigned int DAT_80612944;

// Secondary 0x18-byte-stride descriptor table adjacent to the actor table
// (base 0x80465aa4) and its out-of-range fallback record.
extern unsigned char DAT_804666a4;

// Named cross-module helpers.
extern int light_probe_stub_return_zero(void);
extern int soundpad_resolve_entity_to_zone(int entityId);
extern unsigned int player_ai_preload_character_resources(unsigned int a0, unsigned int a1,
                                                          unsigned int a2, unsigned int a3,
                                                          unsigned int a4, unsigned int a5,
                                                          unsigned int a6, unsigned int a7);

// Frame-scratch spill helpers: FUN_803826f0 hands back the current scratch
// record pointer; FUN_8038273c releases it.
extern unsigned char *FUN_803826f0(void);
extern void FUN_8038273c(void);

// Slot teardown / dispatch helpers.
extern void FUN_80348ff0(int handlerId);

// Handler dispatch tables indexed by slot byte at (+0x12):
//   DAT_8046b3f0[n] : optional per-type update callback (code*)
//   DAT_8046b3ec[n] : per-type teardown handler id (int), interleaved 8 apart
extern unsigned char DAT_8046b3f0;
extern unsigned char DAT_8046b3ec;

#define WORM_IGC_ACTOR_STRIDE  0x5c
#define WORM_IGC_ACTOR_COUNT   0x18
#define WORM_IGC_DESC_STRIDE   0x18
#define WORM_IGC_DESC_BASE     0x80465aa4

// ---------------------------------------------------------------------------
// Slot field / bitfield accessors
// ---------------------------------------------------------------------------

// 0x80238f9c: read the first 32-bit field (entity id) of actor slot `slot`.
unsigned int worm_igc_actor_get_id(int slot)
{
    return *(unsigned int *)(s_CTL7pak_igcs_igcs1_80465aa8 + slot * WORM_IGC_ACTOR_STRIDE);
}

// 0x80238fb4: test whether slot `slot` is marked active.
unsigned int worm_igc_actor_is_active(int slot)
{
    return DAT_80612944 & (1u << slot);
}

// 0x80238fc8: mark slot `slot` active.
void worm_igc_actor_set_active(int slot)
{
    DAT_80612944 = DAT_80612944 | (1u << slot);
}

// 0x80238fe0: overwrite the whole active bitfield.
void worm_igc_actor_set_active_mask(unsigned int mask)
{
    DAT_80612944 = mask;
}

// 0x80238fe8: read the whole active bitfield.
unsigned int worm_igc_actor_get_active_mask(void)
{
    return DAT_80612944;
}

// ---------------------------------------------------------------------------
// Table lookup
// ---------------------------------------------------------------------------

// 0x80238ff0: return 1 if any of the 24 actor slots holds entity id `entityId`.
int worm_igc_actor_id_present(int entityId)
{
    int off = 0;
    int remaining = WORM_IGC_ACTOR_COUNT;
    do {
        if (entityId == *(int *)(s_CTL7pak_igcs_igcs1_80465aa8 + off)) {
            return 1;
        }
        off += WORM_IGC_ACTOR_STRIDE;
        remaining -= 1;
    } while (remaining != 0);
    return 0;
}

// 0x8023902c: return a pointer to descriptor record `index` in the 0x18-stride
// table, or the shared fallback record when the probe stub reports "busy".
unsigned char *worm_igc_actor_get_desc(int index)
{
    unsigned char *rec;
    if (light_probe_stub_return_zero() == 0) {
        rec = (unsigned char *)(index * WORM_IGC_DESC_STRIDE + WORM_IGC_DESC_BASE);
    } else {
        rec = &DAT_804666a4;
    }
    return rec;
}

// ---------------------------------------------------------------------------
// Entity resolution / dispatch
// ---------------------------------------------------------------------------

// 0x80239080: resolve an entity handle to its zone id. Ids above 8000 are real
// entity handles that must be dereferenced through the sound/zone resolver;
// smaller values are already zone ids and pass through unchanged.
int worm_igc_resolve_zone(int entityId)
{
    if (8000 < entityId) {
        int zone = soundpad_resolve_entity_to_zone(entityId);
        entityId = (int)*(short *)(zone + 8);
    }
    return entityId;
}

// 0x802390b4: tear down one actor instance. Invoke its optional per-type update
// callback, then release the associated teardown handler id (or -1 when the
// instance carries the 0x20000 "detached" flag at +0x238).
void worm_igc_actor_teardown(int actor)
{
    unsigned char typeIndex = *(unsigned char *)(actor + 0x12);
    typedef void (*IgcActorFn)(void);
    IgcActorFn *fnTable = (IgcActorFn *)(&DAT_8046b3f0 + (unsigned int)typeIndex * 8);
    if (*fnTable != (IgcActorFn)0) {
        (*fnTable)();
    }

    if ((*(unsigned int *)(actor + 0x238) & 0x20000) == 0) {
        int handlerId = *(int *)(&DAT_8046b3ec + (unsigned int)typeIndex * 8);
        FUN_80348ff0(handlerId);
    } else {
        FUN_80348ff0(-1);
    }
}

// ---------------------------------------------------------------------------
// Per-frame preload sweep
// ---------------------------------------------------------------------------

// 0x80239134: walk the current scratch record's short-entry list and preload
// character resources for every valid (id != -1) entry, then release the
// scratch record. The record's first byte is the entry count; entries are
// 4 bytes each with the id short at +2.
void worm_igc_preload_actor_resources(unsigned int a1, unsigned int a2, unsigned int a3,
                                      unsigned int a4, unsigned int a5, unsigned int a6,
                                      unsigned int a7)
{
    unsigned char *rec = FUN_803826f0();
    int off = 0;
    unsigned int carry = 0; // threaded first argument (initial scratch f1 spill)

    for (int i = 0; i < (int)(unsigned int)*rec; i = i + 1) {
        if (*(short *)(rec + off + 2) != -1) {
            carry = player_ai_preload_character_resources(carry, a1, a2, a3, a4, a5, a6, a7);
        }
        off = off + 4;
    }
    FUN_8038273c();
}
