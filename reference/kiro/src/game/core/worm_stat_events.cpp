// worm_stat_events.cpp
// Per-player scoring / statistics accumulator for the mission (worm) game modes.
//
// The subsystem keeps a table of DAT_80612938[playerIndex] blocks, each 0x90c
// bytes wide (0x12 blocks for story/co-op, 2 blocks when DAT_805015bd == '\n',
// i.e. the split-screen head-to-head mode). Each block holds a flat array of
// float stat slots addressed as block[statIndex*4]; slots 0x8ac..0x90b hold a
// rolling 20-entry kill-time ring buffer plus streak/accuracy bookkeeping.
//
// Public API (defined in worm_stat_api.* region 0x80237xxx, declared here as
// file-local externs because this TU only includes common.h):
//   FUN_80237b50 = stat_add(value, statIdx, player)  accumulate (mode-gated)
//   FUN_80237a00 = stat_set(value, statIdx, player)   overwrite/raise
//   FUN_80237cb4 = stat_get(statIdx, player) -> float  read
//   FUN_80237f34 / FUN_80237e0c = relation/streak helpers
//
// batch_702: 7 functions 0x802346fc-0x802350eb (the 3 switch-trigger handlers
// of this address range are in worm_mission_hooks.cpp).
// Address range: 0x802346fc .. 0x802350eb
// NON-MATCHING decompilation.

#include "common.h"

// ---- Raw data symbols (file-local externs; per-file per Compile-Clean rule 5) ----
extern int    DAT_80612938;   // base pointer of the per-player stat table
extern char   DAT_805015bd;   // game-mode selector ('\n' == 2-player head-to-head)
extern int    DAT_80612610;   // active human player count component
extern int    DAT_806120fc;   // active bot/extra player count component
extern int    DAT_80612614;   // secondary mode id (0x65 == special 18-slot mode)
extern int    DAT_80611014;   // player cap for the 0x65 special mode
extern int    DAT_80612934;   // stat table initialised flag
extern int    DAT_80612930;   // stat table dirty/frame counter
extern int    DAT_80611934;   // scoring gate: stats only accumulate when == 7
extern int    DAT_80611928;   // numerator sample (int, promoted to double)
extern int    DAT_806109c0;   // denominator sample (int, promoted to double)
extern int    DAT_805c79f0;   // shared 11-slot float scratch row
extern int    DAT_805c7a1c;   // 8x3 int matrix (zeroed on reset)
extern int    DAT_805c7a7c;   // 8x3 int matrix (0xffffffff on reset)

extern float  FLOAT_80618798; // 0.0f default fill / zero constant
extern float  FLOAT_8061879c; // slot 0x118 seed
extern float  FLOAT_806187a0; // per-block +0x908 seed
extern float  FLOAT_806187a4; // unit weight passed to stat_add/stat_set
extern float  FLOAT_806187a8; // streak window epsilon
extern float  FLOAT_806187ac; // per-frame accrual epsilon
extern double DOUBLE_806187b0; // 4503599627370496.0 (int->double bias constant)

// ---- Public stat API (region 0x80237xxx) ----
extern void  worm_stat_add(double value, int statIdx, int player);         // 0x80237b50
extern void  worm_stat_set(double value, int statIdx, int player);         // 0x80237a00
extern double worm_stat_get(int statIdx, int player);                      // 0x80237cb4
extern void  worm_stat_rel_add(float value, int col, int player, int other); // 0x80237f34
extern void  worm_stat_grid_add(float value, int col, int player, int row);   // 0x80237e0c

// ---- Named cross-module helpers (file-local externs; TU includes only common.h) ----
extern int  aiactor_target_is_vehicle(int target);
extern void aiactor_bark_select_event(int barkId);

// ---- Table allocator (16-arg spill form matches this game's mem_alloc ABI) ----
extern int mem_alloc(double d1, double d2, double d3, double d4,
                     double d5, double d6, double d7, double d8,
                     int size, int flag, int a11, int a12,
                     int a13, int a14, int a15, int a16);

// Forward decls for functions defined below and called out of order.
static int  worm_stat_player_in_bounds(int player);
static void worm_stat_block_clear(int block);
static void worm_stat_table_reset(void);

// 0x802346fc | worm_stat_player_in_bounds
// Returns 1 if 'player' is a valid, currently-active player slot for the current
// game mode; 0 otherwise. Three regimes: split head-to-head (mode '\n', 2 slots),
// the 0x65 special mode (up to 18 slots capped by DAT_80611014), and the default
// (up to 18 slots capped by the human+extra player total).
static int worm_stat_player_in_bounds(int player)
{
    if (DAT_805015bd == '\n') {
        int ok = 0;
        if ((player < 2) && (player < DAT_80612610 + DAT_806120fc)) {
            ok = 1;
        }
        return ok;
    }
    if (DAT_80612614 == 0x65) {
        int ok = 0;
        if ((player < 0x12) && (player < DAT_80611014)) {
            ok = 1;
        }
        return ok;
    }
    int ok = 0;
    if ((player < 0x12) && (player < DAT_80612610 + DAT_806120fc)) {
        ok = 1;
    }
    return ok;
}

// 0x802347d8 | worm_stat_table_alloc
// Allocate the per-player stat table: (2 or 0x12) blocks of 0x90c bytes each,
// depending on game mode, storing the base in DAT_80612938. Then reset it.
// The leading eight double / u64 spill args are the game's mem_alloc ABI prefix.
static void worm_stat_table_alloc(double d1, double d2, double d3, double d4,
                                  double d5, double d6, double d7, double d8,
                                  int a9, int a10, int a11, int a12,
                                  int a13, int a14, int a15, int a16)
{
    int blocks = 0x12;
    if (DAT_805015bd == '\n') {
        blocks = 2;
    }
    DAT_80612938 = mem_alloc(d1, d2, d3, d4, d5, d6, d7, d8,
                             blocks * 0x90c, 1, a11, a12, a13, a14, a15, a16);
    worm_stat_table_reset();
}

// 0x80234824 | worm_stat_block_clear
// Initialise one player's stat block (at absolute address 'block') to defaults:
// 0x61 leading float slots = 0.0f; slot 0x118 = FLOAT_8061879c; a 0x46-row x 5-col
// float grid at +0x184 (stride 0x14) and a 0x12-row x 6-col grid at +0x6fc
// (stride 0x18) filled with 0.0f; plus an 11-slot shared scratch row DAT_805c79f0.
static void worm_stat_block_clear(int block)
{
    float zero = FLOAT_80618798;
    int off = 0;
    int i = 0x61;
    do {
        *(float *)(block + off) = zero;
        off = off + 4;
        i = i - 1;
    } while (i != 0);

    *(float *)(block + 0x118) = FLOAT_8061879c;

    int row = 0;
    int rowOff = 0;
    do {
        int col = 0;
        int c = 5;
        do {
            int slot = col + 0x184;
            col = col + 4;
            *(float *)(block + rowOff + slot) = zero;
            c = c - 1;
        } while (c != 0);
        row = row + 1;
        rowOff = rowOff + 0x14;
    } while (row < 0x46);

    row = 0;
    rowOff = 0;
    do {
        int col = 0;
        int c = 6;
        do {
            int slot = col + 0x6fc;
            col = col + 4;
            *(float *)(block + rowOff + slot) = zero;
            c = c - 1;
        } while (c != 0);
        row = row + 1;
        rowOff = rowOff + 0x18;
    } while (row < 0x12);

    off = 0;
    i = 0xb;
    do {
        *(float *)((int)&DAT_805c79f0 + off) = zero;
        off = off + 4;
        i = i - 1;
    } while (i != 0);
}

// 0x802348e8 | worm_stat_table_reset
// Clear every active player's stat block, seed the ring-buffer bookkeeping
// (+0x8ac count = 0, +0x900 head = 0, +0x904 flag = 0, +0x908 accrual seed),
// mark the table valid (DAT_80612934 = 1), and reset the 8x3 relation matrices
// (DAT_805c7a1c = 0, DAT_805c7a7c = -1).
static void worm_stat_table_reset(void)
{
    double seed = (double)FLOAT_806187a0;
    int off = 0;
    int player = 0;
    for (;;) {
        int blocks = 0x12;
        if (DAT_805015bd == '\n') {
            blocks = 2;
        }
        if (blocks <= player) {
            break;
        }
        worm_stat_block_clear(DAT_80612938 + off);
        *(int *)(DAT_80612938 + off + 0x8ac) = 0;
        *(int *)(DAT_80612938 + off + 0x900) = 0;
        *(float *)(DAT_80612938 + off + 0x908) = (float)seed;
        *(int *)(DAT_80612938 + off + 0x904) = 0;
        player = player + 1;
        off = off + 0x90c;
    }

    DAT_80612934 = 1;
    DAT_80612930 = 0;

    int r = 0;
    int rowOff = 0;
    do {
        int col = 0;
        int c = 3;
        do {
            *(int *)((int)&DAT_805c7a1c + col + rowOff) = 0;
            *(int *)((int)&DAT_805c7a7c + col + rowOff) = -1;
            col = col + 4;
            c = c - 1;
        } while (c != 0);
        r = r + 1;
        rowOff = rowOff + 0xc;
    } while (r < 8);
}

// 0x80234a00 | worm_stat_noop
// Empty stub retained by the original build (no-op hook slot).
static void worm_stat_noop(void)
{
    return;
}

// 0x80234a04 | worm_stat_on_kill
// Kill/death event: 'killer' (param_1) killed 'victim' (param_2). Awards the base
// kill/death stats, links the relation, updates the victim's 20-entry kill-time
// ring buffer, recomputes recent-window streak counts, raises accuracy/streak
// stats, and periodically triggers a killing-spree bark on the killer.
// NON-MATCHING (int<->double promotion sequences reproduced literally).
static void worm_stat_on_kill(int *killer, int *victim)
{
    const float W = FLOAT_806187a4;
    const double BIAS = DOUBLE_806187b0;

    worm_stat_add((double)W, 1, victim[2]);   // deaths++

    if ((killer != (int *)0x0) && (killer != victim)) {
        worm_stat_add((double)W, 0, killer[2]);   // kills++
        worm_stat_rel_add(W, 1, victim[2], killer[2]);
        worm_stat_rel_add(W, 0, killer[2], victim[2]);
        worm_stat_grid_add(W, 1, killer[2], killer[0x25]);

        if (victim[0x11] == 2) {
            worm_stat_add((double)W, 0x42, killer[2]);
        } else if (aiactor_target_is_vehicle(*victim) != 0) {
            worm_stat_add((double)W, 0x43, killer[2]);
        }

        int cls = (int)*(short *)((char *)victim + 0x48);   // victim class id at +0x48
        if (((cls == 0x5d) || (((cls - 0x5e) & 0xffff) < 2)) || (cls == 0x87) ||
            ((((cls - 0x9a) & 0xffff) < 2) || (cls == 0xac))) {
            worm_stat_add((double)W, 0x4d, killer[2]);   // special-class kill bonus
        }
    }

    if (worm_stat_player_in_bounds(victim[2]) != 0) {
        int vb = DAT_80612938 + victim[2] * 0x90c;
        *(int *)(vb + 0x8ac) = 0;   // reset victim ring count
        *(int *)(vb + 0x904) = 0;
        *(int *)(vb + 0x900) = 0;
    }

    if (killer == victim) {
        worm_stat_add((double)W, 2, victim[2]);   // suicide
        return;
    }

    worm_stat_add((double)W, 4, victim[2]);   // died-to-other
    if (killer == (int *)0x0) {
        return;
    }

    worm_stat_add((double)W, 3, killer[2]);   // got-a-kill
    if (worm_stat_player_in_bounds(killer[2]) == 0) {
        return;
    }

    int kb = DAT_80612938 + killer[2] * 0x90c;

    if (*(int *)(kb + 0x8ac) != 0) {
        *(int *)(kb + 0x900) = (*(int *)(kb + 0x900) + 1) % 0x14;   // advance ring head
    }

    // Record the current match-time sample into the ring at the head slot.
    float now = (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_80611928 ^ 0x80000000) - BIAS)
              * (W / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - BIAS));
    *(float *)(kb + *(int *)(kb + 0x900) * 4 + 0x8b0) = now;
    *(int *)(kb + 0x8ac) = *(int *)(kb + 0x8ac) + 1;

    // Total-kills streak stat (slot 5): raise to running count if larger.
    unsigned int total = (unsigned int)*(int *)(kb + 0x8ac) ^ 0x80000000;
    if (*(float *)(kb + 0x14) < (float)((double)CONCAT44(0x43300000, total) - BIAS)) {
        worm_stat_set((double)(float)((double)CONCAT44(0x43300000, total) - BIAS), 5, killer[2]);
    }

    // Count how many of the last <=20 kills fall inside the recent-time window.
    unsigned int recent = 0;
    int seen = 0;
    int head = *(int *)(kb + 0x900);
    int headOff = head * 4;
    for (;;) {
        int lim = (*(int *)(kb + 0x8ac) < 0x14) ? *(int *)(kb + 0x8ac) : 0x14;
        if (lim <= seen) {
            break;
        }
        if (*(float *)(kb + headOff + 0x8b0) - FLOAT_806187a8 <
            *(float *)(kb + head * 4 + 0x8b0)) {
            recent = recent + 1;
        }
        seen = seen + 1;
        head = (head + 0x13) % 0x14;
    }

    // Multi-kill streak stat (slot 6): raise to recent-window count if larger.
    if (*(float *)(kb + 0x18) < (float)((double)CONCAT44(0x43300000, recent ^ 0x80000000) - BIAS)) {
        worm_stat_set((double)(float)((double)CONCAT44(0x43300000, recent ^ 0x80000000) - BIAS),
                     6, killer[2]);
    }

    if (*(int *)(kb + 0x904) == 0) {
        *(int *)(kb + 0x904) = 1;
    }
    if (now < FLOAT_806187ac + *(float *)(kb + 0x908)) {
        *(int *)(kb + 0x904) = *(int *)(kb + 0x904) + 1;   // consecutive-quick-kill counter
    }

    // Spree stat (slot 7): raise to the quick-kill counter if larger.
    double spree = worm_stat_get(7, killer[2]);
    unsigned int quick = (unsigned int)*(int *)(kb + 0x904) ^ 0x80000000;
    if (spree < (double)(float)((double)CONCAT44(0x43300000, quick) - BIAS)) {
        worm_stat_set((double)(float)((double)CONCAT44(0x43300000, quick) - BIAS), 7, killer[2]);
    }

    *(float *)(kb + 0x908) = now;

    // Killing-spree bark on the killer's AI actor at 3 kills, then every 15.
    if ((*killer != 0) && (*(int *)(*killer + 0x150) != 0) &&
        (-1 < *(int *)(*(int *)(*killer + 0x150) + 4))) {
        int count = *(int *)(kb + 0x8ac);
        if (count == 3) {
            aiactor_bark_select_event(0x19);
        } else if ((0xe < count) && (count == (count / 0xf) * 0xf)) {
            aiactor_bark_select_event(0x1e);
        }
    }

    // Clamp the victim's aggregate-time stats (slots 0x45/0x46/0x47) to the last hit.
    if (worm_stat_player_in_bounds(victim[2]) != 0) {
        if (worm_stat_get(0x45, victim[2]) < worm_stat_get(0x46, victim[2])) {
            worm_stat_set(worm_stat_get(0x45, victim[2]), 0x46, victim[2]);
        }
        if (worm_stat_get(0x47, victim[2]) < worm_stat_get(0x45, victim[2])) {
            worm_stat_set(worm_stat_get(0x45, victim[2]), 0x47, victim[2]);
        }
        worm_stat_set((double)FLOAT_80618798, 0x45, victim[2]);
    }
}

// 0x80235018 | worm_stat_on_damage_event
// Damage-type event dispatch: given a source actor 'src', target actor 'dst' and
// a damage-category id 'kind', award the matching per-source damage stat (and, for
// the melee band 3..4, a bleed/received stat on the target).
static void worm_stat_on_damage_event(int src, int dst, int kind)
{
    const float W = FLOAT_806187a4;
    if (kind < 5) {
        if (kind < 3) {
            if ((-1 < kind) && (src != 0)) {
                worm_stat_add((double)W, 0xf, *(int *)(src + 8));
            }
        } else {
            if (src != 0) {
                worm_stat_add((double)W, 0xd, *(int *)(src + 8));
            }
            worm_stat_add((double)W, 0x1c, *(int *)(dst + 8));
        }
    } else if (kind < 0x13) {
        if (kind < 0xd) {
            if (src != 0) {
                worm_stat_add((double)W, 0x10, *(int *)(src + 8));
            }
        } else if (src != 0) {
            worm_stat_add((double)W, 0x11, *(int *)(src + 8));
        }
    }
}

// ---------------------------------------------------------------------------
// batch_809: two shared-scratch-row (DAT_805c79f0, 11 float slots) writers.
// Both are gated on the player being a valid active slot AND the mode flag
// DAT_805015c0 & 0x40 (the head-to-head relation-tracking sub-mode). They keep
// the running per-relation scratch values that worm_stat_table_reset seeds to 0.
// Address: 0x80237a70 / 0x80237ad8 | Date: 2026-09-10
// ---------------------------------------------------------------------------
extern int DAT_805015c0;   // mode/feature flag word; bit 0x40 = relation scratch active

// 0x80237a70 | worm_stat_scratch_set
// Overwrite scratch slot [slot] with 'value' when the owning player is in bounds
// and the relation-scratch sub-mode is active.
void worm_stat_scratch_set(double value, int slot, int player)
{
    if ((worm_stat_player_in_bounds(player) != 0) && ((DAT_805015c0 & 0x40) != 0)) {
        *(float *)((int)&DAT_805c79f0 + slot * 4) = (float)value;
    }
}

// 0x80237ad8 | worm_stat_scratch_add
// Accumulate 'value' into scratch slot [slot] (same gating as the setter).
void worm_stat_scratch_add(double value, int slot, int player)
{
    if ((worm_stat_player_in_bounds(player) != 0) && ((DAT_805015c0 & 0x40) != 0)) {
        float cur = *(float *)((int)&DAT_805c79f0 + slot * 4);
        *(float *)((int)&DAT_805c79f0 + slot * 4) = (float)((double)cur + value);
    }
}
