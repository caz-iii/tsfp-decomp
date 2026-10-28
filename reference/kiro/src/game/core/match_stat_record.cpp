// ============================================================================
// Address range: 0x80366134 - 0x80366428 | batch_1029 | 2026-09-12
//
// Match stat / event recording cluster (plus a few stubs and an award-banner
// seed helper) that follows the match-phase query module. The recording path
// builds a small fixed-layout telemetry record on the stack - one field at a
// time via leaf_31e8 (an overlap-safe byte copy / memmove) - and then hands it
// to a sink that is a no-op stub in this build, so the records are assembled
// but not persisted. The per-player stat buffer is the 0x30c-stride table at
// DAT_805e8c22 (16 slots), poked by stat_buffer_set_byte and cleared en masse
// by stat_buffer_reset.
//
//   ret1_stub_d              0x80366134  stub, returns 1
//   empty_stub_c             0x8036613c  stub, no-op
//   empty_stub_d             0x80366140  stub, no-op
//   stat_buffer_set_byte     0x80366144  write one byte into a player stat slot
//   stat_buffer_reset        0x80366198  zero all 16 stat records for a player
//   stat_record_match_event  0x803661d8  assemble a match-scope stat record
//   stat_tally_squad_b       0x803662a8  count squad-B members (objective mode)
//   stat_record_player_event 0x80366358  assemble a per-player stat record
//   match_is_objective_mode  0x80366414  team/objective scoreline mode flag
//   award_banner_seed_init   0x80366428  seed the arcade award-banner RNG state
//
// NOTE: non-matching decomp. The record assembly is reproduced field-for-field
// but the retail sink is stubbed. rng_counter_tick's generated prototype is
// stripped to void(void), so award_banner_seed_init calls it through a typed
// function-pointer cast to recover its uint return.

#include "common.h"
#include "generated_decls.h"

// ---- per-file externs for raw symbols -------------------------------------
extern unsigned char DAT_805e8c22;   // player stat-buffer base (16 slots, stride 0x30c)
extern int  DAT_805e8c24;            // per-record leading word (cleared on reset)
extern int* DAT_80612e7c;            // match-context pointer (null when no match)
extern int  DAT_80612020;            // match/session id word recorded per event
extern char DAT_805015bd;            // build/config flag byte (6 == squad-B objective)
extern int  DAT_80612e90;            // 1 when team/objective scoreline mode is active
extern int  DAT_80612e98;            // award-banner: seeded flag
extern int  DAT_80612e9c;            // award-banner: seed generation counter
extern unsigned int DAT_806112d0;    // award-banner: seeded remainder value
extern int  DAT_806112d4;            // award-banner: modulus

// scratch-scope helpers (hi word of the returned pair = player id / context).
extern int  FUN_803826f0(void);   // scratch enter (event: player id)
extern void FUN_8038273c(void);   // scratch leave
extern int  FUN_803826e8(void);   // scratch enter (tally: context)
extern void FUN_80382734(void);   // scratch leave

// rng_counter_tick returns the tick value (Ghidra stripped its generated decl).
typedef unsigned int (*RngTickFn)(void);

// stat-buffer absolute addresses used by the original (record base + 1, and the
// award-banner scratch block); expressed as the raw linker addresses.
#define STAT_BUFFER_SLOT1  ((unsigned char*)0x805e8c23)   // -0x7fa173dd
#define AWARD_BANNER_BLOCK 0x805ec2a8                       // -0x7fa13d58

// -------------------------------------------------------------------------
// 0x80366134  ret1_stub_d - stub, returns 1.
// -------------------------------------------------------------------------
int ret1_stub_d(void)
{
    return 1;
}

// -------------------------------------------------------------------------
// 0x8036613c  empty_stub_c - stub, no-op.
// -------------------------------------------------------------------------
void empty_stub_c(void)
{
}

// -------------------------------------------------------------------------
// 0x80366140  empty_stub_d - stub, no-op.
// -------------------------------------------------------------------------
void empty_stub_d(void)
{
}

// -------------------------------------------------------------------------
// 0x80366144  stat_buffer_set_byte - write one byte into a player stat slot.
//   For field index 1 the write lands at the slot's fixed +1 base; other
//   in-range indices write at base + index. Both paths run the original's
//   short settling loop. Negative or >1 indices are ignored.
// -------------------------------------------------------------------------
void stat_buffer_set_byte(int index, unsigned char value, int slot)
{
    if (index == 1) {
        STAT_BUFFER_SLOT1[slot * 0x30c] = value;
        for (int i = 0x10; i != 0; i--) { }
        return;
    }
    if (index > 0) {
        return;
    }
    if (index < 0) {
        return;
    }
    (&DAT_805e8c22)[slot * 0x30c + index] = value;
    for (int i = 0x10; i != 0; i--) { }
}

// -------------------------------------------------------------------------
// 0x80366198  stat_buffer_reset - zero all 16 stat records for a player.
//   Walks the 16 stride-0x30c records for the given slot, clearing each
//   record's leading word (DAT_805e8c24) and its flag byte (DAT_805e8c22).
// -------------------------------------------------------------------------
void stat_buffer_reset(int slot)
{
    int off = 0;
    for (int n = 0x10; n != 0; n--) {
        *(int*)(&DAT_805e8c24 + slot * 4 + off) = 0;
        (&DAT_805e8c22)[slot + off] = 0;
        off += 0x30c;
    }
}

// -------------------------------------------------------------------------
// 0x803661d8  stat_record_match_event - assemble a match-scope stat record.
//   When a match context is present, copies the event fields (player id, the
//   event sub-code, the match-context key, and the session id word) into a
//   stack record, hands it to the (stubbed) sink, and records the event byte
//   into the player's stat buffer.
// -------------------------------------------------------------------------
void stat_record_match_event(void)
{
    unsigned char rec[0x108];       // stack record buffer
    unsigned char playerId;
    unsigned char subCode;
    unsigned char ctxKey;
    int sessionId;

    int pid = FUN_803826f0();
    if (DAT_80612e7c != (int*)0) {
        playerId = (unsigned char)pid;
        leaf_31e8((u32)&rec[0], (u32)&playerId, 1);
        subCode = (unsigned char)pid;   // event sub-code (r4 in the original)
        leaf_31e8((u32)&rec[1], (u32)&subCode, 1);
        ctxKey = (unsigned char)*DAT_80612e7c;
        leaf_31e8((u32)&rec[2], (u32)&ctxKey, 1);
        sessionId = DAT_80612020;
        leaf_31e8((u32)&rec[3], (u32)&sessionId, 4);
        match_stub_noop();
        stat_buffer_set_byte(pid, subCode, *DAT_80612e7c);
    }
    FUN_8038273c();
}

// -------------------------------------------------------------------------
// 0x803662a8  stat_tally_squad_b - count squad-B members (objective mode).
//   In an active match with the squad-B objective config (DAT_805015bd == 6),
//   iterates the squad-B roster; the per-member accumulation is stubbed in this
//   build. Returns the member count consumed by stat_record_player_event.
// -------------------------------------------------------------------------
int stat_tally_squad_b(void)
{
    FUN_803826e8();
    int count = 0;
    if ((match_phase_is_active() != 0) && (DAT_805015bd == '\x06')) {
        count = aiactor_squad_b_count();
        for (int i = 0; i < aiactor_squad_b_count(); i++) {
            // per-member tally stubbed in this build
        }
    }
    FUN_80382734();
    return count;
}

// -------------------------------------------------------------------------
// 0x80366358  stat_record_player_event - assemble a per-player stat record.
//   In an active match, builds a record from the player's id byte, a per-player
//   status word (p[0x4e7]) and the current squad-B tally, then hands it to the
//   (stubbed) sink.
// -------------------------------------------------------------------------
void stat_record_player_event(unsigned int* p)
{
    unsigned char rec[0x108];       // stack record buffer
    unsigned char playerId;
    int statusWord;
    int tally;

    if (match_phase_is_active() != 0) {
        playerId = (unsigned char)*p;
        leaf_31e8((u32)&rec[0], (u32)&playerId, 1);
        statusWord = (int)p[0x4e7];
        leaf_31e8((u32)&rec[1], (u32)&statusWord, 4);
        tally = stat_tally_squad_b();
        leaf_31e8((u32)&rec[5], (u32)&tally, 4);
        ret0_stub();
    }
}

// -------------------------------------------------------------------------
// 0x80366414  match_is_objective_mode - team/objective scoreline mode flag.
//   Returns 1 when the objective/team scoreline mode is active (DAT_80612e90
//   == 1), else 0. Used by the HUD status formatter (batch_1027).
// -------------------------------------------------------------------------
unsigned int match_is_objective_mode(void)
{
    return (DAT_80612e90 == 1) ? 1u : 0u;
}

// -------------------------------------------------------------------------
// 0x80366428  award_banner_seed_init - seed the arcade award-banner RNG state.
//   Draws an RNG tick, marks the banner seeded, stores the tick reduced modulo
//   DAT_806112d4, clears the 0x6c-byte banner scratch block, and bumps the
//   seed generation counter.
// -------------------------------------------------------------------------
void award_banner_seed_init(void)
{
    unsigned int tick = ((RngTickFn)&rng_counter_tick)();
    DAT_80612e98 = 1;
    unsigned int rounded = (tick / (unsigned int)DAT_806112d4) * (unsigned int)DAT_806112d4;
    memset_wrapper(AWARD_BANNER_BLOCK, 0, 0x6c);
    DAT_806112d0 = tick - rounded;
    DAT_80612e9c = DAT_80612e9c + 1;
}
