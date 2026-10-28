// ============================================================================
// Address range: 0x8036603c - 0x8036612c | batch_1028 | 2026-09-12
//
// Match-phase query accessors + a cluster of tiny game-logic shims that sit
// immediately after the HUD player-status formatter. The two phase predicates
// are the ones the match/arcade/objective code polls every frame to decide
// whether the round is still running or has entered its wind-down: they only
// consult the live match-phase counter when the top-level game-mode value is
// the special arcade/session mode (0x65), and otherwise report a fixed answer.
// The rest are session/entity accessors and no-op / constant-return stubs that
// back various registered callbacks.
//
//   match_stub_noop            0x8036603c  forwarder to empty_stub (no-op)
//   match_phase_is_active      0x80366060  round-running predicate
//   match_phase_is_ending      0x80366088  round-ending predicate
//   session_player_camera_tick_fwd 0x803660b0 forwarder to session_player_camera_tick
//   match_get_end_state        0x803660d0  read the cached match end-state word
//   ret1_stub                  0x803660d8  stub, returns 1
//   ret1_stub_b                0x803660e0  stub, returns 1
//   empty_stub_b               0x803660e8  stub, no-op
//   player_flag_refresh_bit1   0x803660ec  clear+conditionally re-set player flag bit1
//   ret1_stub_c                0x8036612c  stub, returns 1
//
// NOTE: non-matching decomp. DAT_80612614 is the top-level game-mode/state word
// (read by the main loops); 0x65 selects the arcade/session mode. DAT_80612e78
// is the match-phase counter (1 == active, 2 == ending). The player-flag record
// table DAT_805eba28 has a 0x118-byte (0x46-word) stride, the same stride as the
// player status table used by hud_format_player_status.
// session_player_camera_tick's generated prototype is stripped to void(void),
// so the forwarder calls it through a typed function-pointer cast.

#include "common.h"
#include "generated_decls.h"

// ---- per-file externs for raw symbols -------------------------------------
extern int DAT_80612614;   // top-level game-mode/state word (0x65 == arcade/session)
extern int DAT_80612e78;   // match-phase counter (1 == active, 2 == ending)
extern int DAT_80612e80;   // cached match end-state word
extern unsigned int DAT_805eba28;  // player-flag record table (stride 0x46 words)

// session_player_camera_tick real arity (Ghidra stripped the generated decl).
typedef void (*SessionCamTickFn)(unsigned long long, double, double, double, double, double,
                                 double, unsigned long long, unsigned int, unsigned int, float*,
                                 unsigned int, unsigned int, unsigned int, unsigned int,
                                 unsigned int);

// -------------------------------------------------------------------------
// 0x8036603c  match_stub_noop - forwarder to empty_stub (no-op).
// -------------------------------------------------------------------------
void match_stub_noop(void)
{
    empty_stub();
}

// -------------------------------------------------------------------------
// 0x80366060  match_phase_is_active - round-running predicate.
//   In the arcade/session mode (game-mode 0x65) the round is "active" only
//   while the match-phase counter is 1; in every other mode this always
//   reports active (1).
// -------------------------------------------------------------------------
unsigned int match_phase_is_active(void)
{
    if (DAT_80612614 == 0x65) {
        return (DAT_80612e78 == 1) ? 1u : 0u;
    }
    return 1;
}

// -------------------------------------------------------------------------
// 0x80366088  match_phase_is_ending - round-ending predicate.
//   In the arcade/session mode the round is "ending" while the match-phase
//   counter is 2; in every other mode this always reports not-ending (0).
// -------------------------------------------------------------------------
unsigned int match_phase_is_ending(void)
{
    if (DAT_80612614 == 0x65) {
        return (DAT_80612e78 == 2) ? 1u : 0u;
    }
    return 0;
}

// -------------------------------------------------------------------------
// 0x803660b0  session_player_camera_tick_fwd - forwarder.
//   Passes its full argument set straight through to the session player camera
//   tick; a thin registered-callback trampoline.
// -------------------------------------------------------------------------
void session_player_camera_tick_fwd(unsigned long long a1, double a2, double a3, double a4,
                                    double a5, double a6, double a7, unsigned long long a8,
                                    unsigned int a9, unsigned int a10, float* a11,
                                    unsigned int a12, unsigned int a13, unsigned int a14,
                                    unsigned int a15, unsigned int a16)
{
    ((SessionCamTickFn)&session_player_camera_tick)(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10,
                                                    a11, a12, a13, a14, a15, a16);
}

// -------------------------------------------------------------------------
// 0x803660d0  match_get_end_state - read the cached match end-state word.
// -------------------------------------------------------------------------
int match_get_end_state(void)
{
    return DAT_80612e80;
}

// -------------------------------------------------------------------------
// 0x803660d8  ret1_stub - stub, returns 1.
// -------------------------------------------------------------------------
int ret1_stub(void)
{
    return 1;
}

// -------------------------------------------------------------------------
// 0x803660e0  ret1_stub_b - stub, returns 1.
// -------------------------------------------------------------------------
int ret1_stub_b(void)
{
    return 1;
}

// -------------------------------------------------------------------------
// 0x803660e8  empty_stub_b - stub, no-op.
// -------------------------------------------------------------------------
void empty_stub_b(void)
{
}

// -------------------------------------------------------------------------
// 0x803660ec  player_flag_refresh_bit1 - clear + conditionally re-set flag bit1.
//   Reads the player's flag word (record table indexed by *pSlot, 0x46-word
//   stride), clears bit1 (0x2); if that bit had been set, sets it straight
//   back. A guarded refresh that leaves the bit unchanged but drops it for
//   slots where it was already clear (matches the original's read/mask/test).
// -------------------------------------------------------------------------
void player_flag_refresh_bit1(int* pSlot)
{
    unsigned int flags = (&DAT_805eba28)[*pSlot * 0x46];
    (&DAT_805eba28)[*pSlot * 0x46] = flags & 0xfffffffd;
    if ((flags & 2) == 0) {
        return;
    }
    (&DAT_805eba28)[*pSlot * 0x46] = (&DAT_805eba28)[*pSlot * 0x46] | 2;
}

// -------------------------------------------------------------------------
// 0x8036612c  ret1_stub_c - stub, returns 1.
// -------------------------------------------------------------------------
int ret1_stub_c(void)
{
    return 1;
}
