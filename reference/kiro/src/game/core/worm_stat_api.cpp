// worm_stat_api.cpp
// Accessor API for the per-player scoring/statistics table (DAT_80612938,
// 0x90c-byte blocks). Each stat slot is a float at block[statIdx*4]. Reads and
// writes are gated by the current scoring phase DAT_80611934:
//   phase 7 = live accumulation (in-match)   -> worm_stat_add / worm_stat_set
//   phase 8 = finalize/derived pass (post-match) -> worm_stat_add_final /
//             worm_stat_set_final and the derived-ratio writers.
// A read (worm_stat_get) always returns the raw slot (or 0.0f default) regardless
// of phase, guarded only by the player-slot bounds check.
//
// batch_703: 5 of 10 functions (the paired query/finalize functions live in
// worm_stat_query.cpp). Address range: 0x80237990 .. 0x80237cb3.
// NON-MATCHING decompilation.

#include "common.h"

extern int   DAT_80612938;    // per-player stat table base
extern int   DAT_80611934;    // scoring phase gate (7=live, 8=finalize)
extern int   DAT_805015c0;    // mode flags (bit 0x40 == snapshot table active)
extern int   DAT_805c79f0;    // 11-slot snapshot float row (used by phase-40 read)
extern float FLOAT_80618798;  // 0.0f default

// Player-slot bounds check (defined in worm_stat_events.cpp).
extern int worm_stat_player_in_bounds(int player);

// 0x80237990 | worm_stat_set_final
// Overwrite stat[player][statIdx] with 'value', but only during the finalize
// phase (DAT_80611934 == 8) and for a valid player slot.
void worm_stat_set_final(double value, int statIdx, int player)
{
    if ((worm_stat_player_in_bounds(player) != 0) && (DAT_80611934 == 8)) {
        *(float *)(DAT_80612938 + player * 0x90c + statIdx * 4) = (float)value;
    }
}

// 0x80237a00 | worm_stat_set
// Overwrite stat[player][statIdx] with 'value' during the live phase
// (DAT_80611934 == 7). Used by the raise-to-max helpers in worm_stat_events.
void worm_stat_set(double value, int statIdx, int player)
{
    if ((worm_stat_player_in_bounds(player) != 0) && (DAT_80611934 == 7)) {
        *(float *)(DAT_80612938 + player * 0x90c + statIdx * 4) = (float)value;
    }
}

// 0x80237b50 | worm_stat_add
// Accumulate 'value' into stat[player][statIdx] during the live phase
// (DAT_80611934 == 7). The primary in-match stat-award entry point.
void worm_stat_add(double value, int statIdx, int player)
{
    if ((worm_stat_player_in_bounds(player) != 0) && (DAT_80611934 == 7)) {
        int off = player * 0x90c + statIdx * 4;
        *(float *)(DAT_80612938 + off) =
            (float)((double)*(float *)(DAT_80612938 + off) + value);
    }
}

// 0x80237bd0 | worm_stat_add_final
// Accumulate 'value' into stat[player][statIdx] during the finalize phase
// (DAT_80611934 == 8). Used by the post-match derived aggregation.
void worm_stat_add_final(double value, int statIdx, int player)
{
    if ((worm_stat_player_in_bounds(player) != 0) && (DAT_80611934 == 8)) {
        int off = player * 0x90c + statIdx * 4;
        *(float *)(DAT_80612938 + off) =
            (float)((double)*(float *)(DAT_80612938 + off) + value);
    }
}

// 0x80237c50 | worm_stat_get_snapshot
// Read the 'statIdx'-th slot of the shared 11-entry snapshot row (DAT_805c79f0),
// but only when the snapshot table is active (mode flag bit 0x40) and the player
// slot is valid; otherwise return 0.0f.
double worm_stat_get_snapshot(int statIdx, int player)
{
    float v = FLOAT_80618798;
    if ((worm_stat_player_in_bounds(player) != 0) && ((DAT_805015c0 & 0x40) != 0)) {
        v = (float)(&DAT_805c79f0)[statIdx];
    }
    return (double)v;
}

// ---------------------------------------------------------------------------
// batch_704: stat read + 2D-grid / relation-matrix accessors.
// Two secondary tables live inside each 0x90c player block:
//   +0x184 : a 0x46-row x 5-col float grid (per-difficulty / per-slot metrics),
//            row stride 0x14, addressed [row*0x14 + col*4].
//   +0x6fc : a per-opponent relation grid, stride 0x18, [other*0x18 + col*4].
// Grid writers are phase-gated exactly like the scalar API (7=live, 8=finalize).
// The (float value, ...) writers receive their value in f1 after the varargs
// spill-load prologue in the original; here it is a normal leading parameter.
// ---------------------------------------------------------------------------

// 0x80237cb4 | worm_stat_get
// Read stat[player][statIdx] (raw float slot), or 0.0f if the player slot is
// out of bounds. Phase-independent read used throughout the query/finalize code.
double worm_stat_get(int statIdx, int player)
{
    float v = FLOAT_80618798;
    if (worm_stat_player_in_bounds(player) != 0) {
        v = *(float *)(DAT_80612938 + player * 0x90c + statIdx * 4);
    }
    return (double)v;
}

// 0x80237d14 | worm_stat_grid_get
// Read the +0x184 grid slot [row*0x14 + col*4] for a player, or 0.0f when the
// player is invalid or row >= 0x46.
float worm_stat_grid_get(int col, int player, int row)
{
    if ((worm_stat_player_in_bounds(player) != 0) && (row < 0x46)) {
        return *(float *)(DAT_80612938 + player * 0x90c + row * 0x14 + col * 4 + 0x184);
    }
    return FLOAT_80618798;
}

// 0x80237e0c | worm_stat_grid_add
// Accumulate 'value' into the +0x184 grid slot during the live phase
// (DAT_80611934 == 7), for a valid player and row < 0x46.
void worm_stat_grid_add(float value, int col, int player, int row)
{
    if ((worm_stat_player_in_bounds(player) != 0) && (row < 0x46) && (DAT_80611934 == 7)) {
        int base = DAT_80612938 + player * 0x90c + row * 0x14 + col * 4;
        *(float *)(base + 0x184) = (float)((double)*(float *)(base + 0x184) + (double)value);
    }
}

// 0x80237d8c | worm_stat_rel_get
// Read the +0x6fc relation grid slot [other*0x18 + col*4] for (player, other),
// or 0.0f if either slot index is out of bounds.
float worm_stat_rel_get(int col, int player, int other)
{
    if ((worm_stat_player_in_bounds(player) != 0) && (worm_stat_player_in_bounds(other) != 0)) {
        return *(float *)(DAT_80612938 + player * 0x90c + other * 0x18 + col * 4 + 0x6fc);
    }
    return FLOAT_80618798;
}

// 0x80237f34 | worm_stat_rel_add
// Accumulate 'value' into the +0x6fc relation grid during the live phase
// (DAT_80611934 == 7), for valid player and other.
void worm_stat_rel_add(float value, int col, int player, int other)
{
    if ((worm_stat_player_in_bounds(player) != 0) && (worm_stat_player_in_bounds(other) != 0) &&
        (DAT_80611934 == 7)) {
        int base = DAT_80612938 + player * 0x90c + other * 0x18 + col * 4;
        *(float *)(base + 0x6fc) = (float)((double)*(float *)(base + 0x6fc) + (double)value);
    }
}

// 0x80237ea4 | worm_stat_rel_set_final
// Overwrite the +0x6fc relation grid slot during the finalize phase
// (DAT_80611934 == 8), for valid player and other.
void worm_stat_rel_set_final(float value, int col, int player, int other)
{
    if ((worm_stat_player_in_bounds(player) != 0) && (worm_stat_player_in_bounds(other) != 0) &&
        (DAT_80611934 == 8)) {
        *(float *)(DAT_80612938 + player * 0x90c + other * 0x18 + col * 4 + 0x6fc) = value;
    }
}
