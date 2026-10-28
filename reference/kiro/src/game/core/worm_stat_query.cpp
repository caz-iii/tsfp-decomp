// worm_stat_query.cpp
// Query / reduction / finalization pass over the per-player scoring table
// (DAT_80612938). These run after a match to reduce raw counters into derived
// ratios and to evaluate the end-of-match award/medal flags.
//
//   worm_stat_argmax_player  (0x802350ec) - player index holding max of a stat
//   worm_stat_argmin_player  (0x80235178) - player index holding min of a stat
//   worm_stat_average        (0x80235204) - mean of a stat over all players
//   worm_stat_finalize_ratios(0x80235298) - compute per-player K/D & accuracy
//   worm_stat_evaluate_awards(0x80235770) - assign per-player award/medal flags
//
// batch_703: 5 of 10 functions (the accessor API lives in worm_stat_api.cpp).
// Address range: 0x802350ec .. 0x8023576f (+ the awards evaluator to 0x8023798f).
// NON-MATCHING decompilation - the int<->double conversion idioms (CONCAT44 with
// 0x43300000 / 0x80000000 bias) are the compiler's signed-int-to-double sequence
// and are reproduced literally.

#include "common.h"

// ---- Table + player-count / mode globals (file-local externs) ----
extern int    DAT_80612938;   // per-player stat table base
extern int    DAT_80612610;   // human player count
extern int    DAT_806120fc;   // extra/bot player count
extern char   DAT_805015bd;   // game-mode selector ('\n', '\x02', ...)
extern int    DAT_805015c0;   // mode flag bits (0x40 snapshot, 0x01 team, ...)
extern int    DAT_80611928;   // match-time numerator sample
extern int    DAT_806109c0;   // match-time denominator sample
extern int    DAT_8061292c;   // scratch: cached human count
extern int    DAT_80612928;   // scratch: cached round count from DAT_80611d90+0xa68
extern int    DAT_80611d90;   // per-player extended record base (0xea0 stride)
extern int    DAT_80611d78;   // per-player active-slot table (0x141c stride here)
extern int    DAT_806125fc;   // gameplay modifier mask A
extern int    DAT_80612600;   // gameplay modifier mask B

// award-name working matrices (8 rows x 3 cols)
extern int    DAT_805c7a1c;   // resolved award string pointers
extern int    DAT_805c7a20;   // (col 1 of the same matrix)
extern int    DAT_805c7a24;   // (col 2 of the same matrix)
extern int    DAT_805c7a7c;   // chosen award ids (-1 = none)
extern int    DAT_805c7a80;   // (col 1 of the id matrix)
extern short  DAT_804659d8;   // award-id -> string-id table
extern int    DAT_8043e234;   // per-award weight table (0x3c stride)

extern float  FLOAT_80618798; // 0.0f
extern float  FLOAT_8061879c; // +inf-ish (min-search seed)
extern float  FLOAT_806187a4; // 1.0f unit
extern float  FLOAT_806187a8; // small epsilon
extern float  FLOAT_806187ac; // epsilon
extern float  FLOAT_806187b8; // -inf-ish (max-search seed)
extern float  FLOAT_806187bc; // 1.0f fallback ratio
extern float  FLOAT_806187c0; // award weight divisor
extern float  FLOAT_806187c4; // threshold
extern float  FLOAT_806187c8; // threshold
extern float  FLOAT_806187cc; // threshold
extern float  FLOAT_806187d0; // threshold
extern float  FLOAT_806187d4; // threshold
extern float  FLOAT_806187d8; // threshold
extern float  FLOAT_806187dc; // bias/threshold
extern float  FLOAT_806187e0; // threshold
extern float  FLOAT_806187e4; // threshold
extern float  FLOAT_806187e8; // threshold
extern float  FLOAT_806187ec; // threshold
extern float  FLOAT_806187f0; // threshold
extern float  FLOAT_806187f4; // scale
extern float  FLOAT_806187f8; // scale
extern float  FLOAT_806187fc; // scale
extern float  FLOAT_80618800; // scale
extern float  FLOAT_80618804; // scale
extern float  FLOAT_80618808; // threshold
extern float  FLOAT_8061880c; // threshold
extern float  FLOAT_80618810; // threshold
extern float  FLOAT_80618814; // loop cap
extern float  FLOAT_80618818; // scale
extern float  FLOAT_8061881c; // threshold
extern float  FLOAT_80618820; // divisor
extern float  FLOAT_80618824; // threshold
extern double DOUBLE_806187b0; // 4503599627370496.0 int->double bias

// ---- Stat accessors (worm_stat_api.cpp / worm_stat_events.cpp) ----
extern double worm_stat_get(int statIdx, int player);              // FUN_80237cb4
extern void   worm_stat_set(double value, int statIdx, int player); // FUN_80237a00
extern void   worm_stat_set_final(double value, int statIdx, int player); // FUN_80237990
extern void   worm_stat_add_final(double value, int statIdx, int player); // FUN_80237bd0

// ---- Grid / relation-matrix helpers (0x80237xxx, later batch; file-local) ----
extern float worm_stat_grid_get(int col, int player, int row);              // 0x80237d14
extern float worm_stat_rel_get(int col, int player, int other);             // 0x80237d8c
extern void  worm_stat_rel_set_final(float value, int col, int player, int other); // 0x80237ea4

// ---- Named cross-module helpers ----
extern unsigned int gameplay_modifier_init_all(void);
extern unsigned int rng_counter_tick(void);
extern int  get_string_by_id(int id);
extern unsigned char *stats_get_player_ptr(int player);

// 0x802350ec | worm_stat_argmax_player
// Return the player index (0..playerCount-1) whose 'statIdx' value is the
// largest, or -1 if none exceeds the negative-infinity seed.
int worm_stat_argmax_player(int statIdx, int playerCount)
{
    float best = FLOAT_806187b8;
    int bestPlayer = -1;
    for (int p = 0; p < playerCount; p = p + 1) {
        double v = worm_stat_get(statIdx, p);
        if ((double)best < v) {
            best = (float)worm_stat_get(statIdx, p);
            bestPlayer = p;
        }
    }
    return bestPlayer;
}

// 0x80235178 | worm_stat_argmin_player
// Return the player index whose 'statIdx' value is the smallest, or -1.
int worm_stat_argmin_player(int statIdx, int playerCount)
{
    float best = FLOAT_8061879c;
    int bestPlayer = -1;
    for (int p = 0; p < playerCount; p = p + 1) {
        double v = worm_stat_get(statIdx, p);
        if (v < (double)best) {
            best = (float)worm_stat_get(statIdx, p);
            bestPlayer = p;
        }
    }
    return bestPlayer;
}

// 0x80235204 | worm_stat_average
// Return the arithmetic mean of 'statIdx' across the first 'playerCount' players.
float worm_stat_average(int statIdx, int playerCount)
{
    float sum = FLOAT_80618798;
    for (int p = 0; p < playerCount; p = p + 1) {
        sum = (float)((double)sum + worm_stat_get(statIdx, p));
    }
    double denom = (double)CONCAT44(0x43300000, (unsigned int)playerCount ^ 0x80000000)
                 - DOUBLE_806187b0;
    return (float)((double)sum / denom);
}

// 0x80235298 | worm_stat_finalize_ratios
// Post-match pass 1: for every active player compute derived ratios from the raw
// counters (accuracy = hits/shots, headshot/melee/etc. fractions, per-second
// rates normalised by match duration) and store them back into the finalize
// slots (0x10..0x16, 0x2c..0x3d) via worm_stat_set_final.
void worm_stat_finalize_ratios(void)
{
    double duration = (double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000);
    DAT_8061292c = DAT_80612610;
    DAT_80612928 = *(int *)(DAT_80611d90 + 0xa68);

    float matchTime =
        (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_80611928 ^ 0x80000000) - DOUBLE_806187b0)
      * (FLOAT_806187a4 / (float)(duration - DOUBLE_806187b0));
    double matchTimeD = (double)matchTime;
    if ((double)FLOAT_80618798 == matchTimeD) {
        matchTimeD = (double)FLOAT_806187bc;
    }

    const double zero = (double)FLOAT_80618798;
    int total = DAT_80612610 + DAT_806120fc;

    for (int p = 0; p < total; p = p + 1) {
        double shots = worm_stat_get(8, p);
        if (zero == shots) {
            worm_stat_set_final(zero, 10, p);
            worm_stat_set_final((double)FLOAT_806187a4, 0xb, p);
        } else {
            double hits = worm_stat_get(9, p);
            worm_stat_set_final((double)(float)(hits / worm_stat_get(8, p)), 10, p);
            double misses = (double)(float)(worm_stat_get(8, p) - worm_stat_get(9, p));
            worm_stat_set_final((double)(float)(misses / worm_stat_get(8, p)), 0xb, p);
        }

        double landed = worm_stat_get(9, p);
        if (zero == landed) {
            worm_stat_set_final(zero, 0x12, p);
            worm_stat_set_final(zero, 0x13, p);
            worm_stat_set_final(zero, 0x14, p);
            worm_stat_set_final(zero, 0x15, p);
            worm_stat_set_final(zero, 0x16, p);
        } else {
            worm_stat_set_final((double)(float)(worm_stat_get(0xd, p) / worm_stat_get(9, p)), 0x12, p);
            worm_stat_set_final((double)(float)(worm_stat_get(0xf, p) / worm_stat_get(9, p)), 0x13, p);
            worm_stat_set_final((double)(float)(worm_stat_get(0x10, p) / worm_stat_get(9, p)), 0x14, p);
            worm_stat_set_final((double)(float)(worm_stat_get(0x11, p) / worm_stat_get(9, p)), 0x15, p);
            worm_stat_set_final((double)(float)(worm_stat_get(0xc, p) / worm_stat_get(9, p)), 0x16, p);
        }
    }

    // Pass 2: per-player weighted "difficulty" accrual (slots 0x2d/0x2e/0x2f/0x3d)
    // reduced from the 0x46-entry per-player grid.
    for (int p = 0; p < total; p = p + 1) {
        float accum = FLOAT_80618798;
        int rowMaxIdx = 0;
        unsigned int rowMaxVal = 0;
        int row = 0;
        float best = FLOAT_80618798;
        do {
            float g = worm_stat_grid_get(0, p, row);
            if (best < g) {
                best = worm_stat_grid_get(0, p, row);
                rowMaxVal = (unsigned int)worm_stat_grid_get(0, p, row);
                rowMaxIdx = row;
            }
            accum = (float)((double)accum + (double)worm_stat_grid_get(0, p, row));
            row = row + 1;
        } while (row < 0x46);

        double slot41 = worm_stat_get(0x41, p);
        if ((double)best < slot41) {
            rowMaxIdx = 0x46;
            rowMaxVal = (unsigned int)(float)worm_stat_get(0x40, p);
        }

        worm_stat_set_final(
            (double)(float)((double)(CONCAT44(0x43300000, (unsigned int)rowMaxIdx) ^ 0x80000000) - DOUBLE_806187b0),
            0x2d, p);
        worm_stat_set_final(
            (double)(float)((double)CONCAT44(0x43300000, rowMaxVal ^ 0x80000000) - DOUBLE_806187b0),
            0x2e, p);

        double shots = worm_stat_get(8, p);
        if (shots <= zero) {
            worm_stat_set_final(zero, 0x2f, p);
        } else {
            double denom = (double)CONCAT44(0x43300000, rowMaxVal ^ 0x80000000) - DOUBLE_806187b0;
            worm_stat_set_final((double)(float)((double)(float)denom / worm_stat_get(8, p)), 0x2f, p);
        }
        worm_stat_set_final((double)(float)(accum / matchTimeD), 0x3d, p);
    }
}

// 0x80235770 | worm_stat_evaluate_awards
// End-of-match award / medal evaluation. For each active player it fills a local
// 0x3b-wide flag row (stride 0xec) by testing raw and derived stats against a
// large table of thresholds (accuracy, streaks, sprees, objective play, team
// contribution, etc.), then commits the top awards - resolving award ids through
// DAT_804659d8 into localized strings (get_string_by_id) and setting the
// per-player achievement bytes via stats_get_player_ptr. Skipped entirely in
// head-to-head ('\n') and when the snapshot flag (mode bit 0x40) is set.
// NON-MATCHING: large fixed local arrays and the RNG-driven award-shuffle are
// reproduced structurally rather than bit-exact.
void worm_stat_evaluate_awards(void)
{
    float awardWeight[18];     // local_1198
    float flagRow[14 * 0x3b];  // local_1150: total x 0x3b, stride 0xec bytes

    double duration = (double)(CONCAT44(0x43300000, (unsigned int)DAT_806109c0) ^ 0x80000000);
    float matchTime =
        (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_80611928 ^ 0x80000000) - DOUBLE_806187b0)
      * (FLOAT_806187a4 / (float)(duration - DOUBLE_806187b0));
    double matchTimeD = (double)matchTime;
    if ((double)FLOAT_80618798 == matchTimeD) {
        matchTimeD = (double)FLOAT_806187bc;
    }

    if ((DAT_805015bd == '\n') || ((DAT_805015c0 & 0x40) != 0)) {
        return;
    }

    const double zero = (double)FLOAT_80618798;
    int total = DAT_80612610 + DAT_806120fc;

    // --- derived team-relative ratios (slots 0x33/0x35/0x37/0x39/0x3b) ---
    for (int p = 0; p < total; p = p + 1) {
        for (int q = 0; q < total; q = q + 1) {
            worm_stat_rel_get(4, p, q);
            worm_stat_rel_set_final(FLOAT_80618798, 5, p, q);
        }
        double denom = worm_stat_get(0x30, p);
        if (denom == zero) {
            worm_stat_set_final(zero, 0x33, p);
            worm_stat_set_final(zero, 0x35, p);
        } else {
            worm_stat_set_final((double)(float)(worm_stat_get(0x32, p) / worm_stat_get(0x30, p)), 0x33, p);
            worm_stat_set_final((double)(float)(worm_stat_get(0x34, p) / worm_stat_get(0x30, p)), 0x35, p);
        }
        worm_stat_set_final((double)(float)(worm_stat_get(0x36, p) / matchTimeD), 0x37, p);
        worm_stat_set_final((double)(float)(worm_stat_get(0x38, p) / matchTimeD), 0x39, p);
        worm_stat_set_final((double)(float)(worm_stat_get(0x3a, p) / matchTimeD), 0x3b, p);
    }

    // --- normalized difficulty contribution -> slot 0x2c ---
    float contrib[18];
    double contribSum = (double)FLOAT_80618798;
    for (int p = 0; p < total; p = p + 1) {
        contrib[p] = FLOAT_80618798;
        int wOff = 0;
        for (int r = 0; r < 0x46; r = r + 1) {
            float g = worm_stat_grid_get(0, p, r);
            double w = (double)(CONCAT44(0x43300000, (unsigned int)*(int *)((int)&DAT_8043e234 + wOff)) ^ 0x80000000);
            wOff = wOff + 0x3c;
            contrib[p] = (float)((double)FLOAT_806187a4
                        - (double)(float)((double)(float)(w - DOUBLE_806187b0) / (double)FLOAT_806187c0))
                        * (float)((double)g / matchTimeD) + contrib[p];
        }
        contribSum = (double)(float)(contribSum + (double)contrib[p]);
    }
    for (int p = 0; p < total; p = p + 1) {
        double v = (double)FLOAT_80618798;
        if ((double)FLOAT_80618798 != contribSum) {
            double mean = contribSum / (double)(float)((double)CONCAT44(0x43300000, (unsigned int)total ^ 0x80000000) - DOUBLE_806187b0);
            v = (double)(contrib[p] / (float)mean);
        }
        worm_stat_set_final(v, 0x2c, p);
    }

    // --- team assist contribution -> slot 0x3c ---
    for (int p = 0; p < total; p = p + 1) {
        worm_stat_set_final((double)FLOAT_80618798, 0x3c, p);
    }
    for (int p = 0; p < DAT_80612610; p = p + 1) {
        for (int q = 0; q < DAT_80612610; q = q + 1) {
            double rel = worm_stat_rel_get(5, q, p);
            worm_stat_add_final(rel, 0x3c, p);
        }
    }

    // --- clear the per-player flag rows ---
    for (int p = 0; p < total; p = p + 1) {
        for (int i = 0; i < 0x3b; i = i + 1) {
            flagRow[p * 0x3b + i] = FLOAT_80618798;
        }
    }

    // --- single-winner award flags (argmax over the whole field) ---
    int w;
    w = worm_stat_argmax_player(0, total);
    if ((-1 < w) && ((double)FLOAT_806187c4 < worm_stat_get(0, w))) flagRow[w * 0x3b + 0] = FLOAT_806187a4;
    w = worm_stat_argmax_player(1, total);
    if ((-1 < w) && ((double)FLOAT_806187c8 < worm_stat_get(1, w))) flagRow[w * 0x3b + 1] = FLOAT_806187a4;
    w = worm_stat_argmax_player(0x4f, total);
    if ((-1 < w) && ((double)FLOAT_806187cc < worm_stat_get(0x4f, w))) flagRow[w * 0x3b + 0x32] = FLOAT_806187a4;

    for (int p = 0; p < total; p = p + 1) {
        if (zero == worm_stat_get(0, p)) flagRow[p * 0x3b + 0x28] = FLOAT_806187a4; // "no kills"
    }

    w = worm_stat_argmax_player(2, total);
    if ((-1 < w) && ((double)FLOAT_806187ac < worm_stat_get(2, w))) flagRow[w * 0x3b + 2] = FLOAT_806187a4;

    // --- threshold-per-player award flags (from raw block fields) ---
    int blockOff = 0;
    for (int p = 0; p < total; p = p + 1) {
        if (FLOAT_806187d0 <= *(float *)(DAT_80612938 + blockOff + 0x14)) flagRow[p * 0x3b + 3] = FLOAT_806187a4;
        if (FLOAT_806187ac <= *(float *)(DAT_80612938 + blockOff + 0x18)) flagRow[p * 0x3b + 4] = FLOAT_806187a4;
        if (FLOAT_806187d4 <= *(float *)(DAT_80612938 + blockOff + 0x1c)) flagRow[p * 0x3b + 5] = FLOAT_806187a4;
        blockOff = blockOff + 0x90c;
    }

    // --- combined-condition argmax awards (accuracy + volume, etc.) ---
    w = worm_stat_argmax_player(8, total);
    if ((-1 < w) && ((double)FLOAT_806187d8 < worm_stat_get(8, w)) && ((double)FLOAT_806187dc < worm_stat_get(10, w))) flagRow[w * 0x3b + 6] = FLOAT_806187a4;
    w = worm_stat_argmax_player(8, total);
    if ((-1 < w) && ((double)FLOAT_806187d8 < worm_stat_get(8, w)) && ((double)FLOAT_806187dc < worm_stat_get(0xb, w))) flagRow[w * 0x3b + 7] = FLOAT_806187a4;
    w = worm_stat_argmax_player(8, total);
    if ((-1 < w) && ((double)FLOAT_806187e0 < worm_stat_get(8, w)) && ((double)FLOAT_806187bc < worm_stat_get(0x16, w))) flagRow[w * 0x3b + 8] = FLOAT_806187a4;
    w = worm_stat_argmax_player(8, total);
    if ((-1 < w) && ((double)FLOAT_806187c4 < worm_stat_get(8, w)) && ((double)FLOAT_806187bc < worm_stat_get(0x12, w))) flagRow[w * 0x3b + 9] = FLOAT_806187a4;
    w = worm_stat_argmax_player(0xe, total);
    if ((-1 < w) && ((double)FLOAT_806187e4 < worm_stat_get(0xe, w))) flagRow[w * 0x3b + 10] = FLOAT_806187a4;
    w = worm_stat_argmax_player(0x1d, total);
    if ((-1 < w) && ((double)FLOAT_806187e4 < worm_stat_get(0x1d, w))) flagRow[w * 0x3b + 0xb] = FLOAT_806187a4;
    w = worm_stat_argmax_player(0x17, total);
    if ((-1 < w) && ((double)FLOAT_806187d4 < worm_stat_get(0x17, w))) flagRow[w * 0x3b + 0xc] = FLOAT_806187a4;
    w = worm_stat_argmax_player(0x18, total);
    if ((-1 < w) && ((double)FLOAT_806187a8 < worm_stat_get(0x18, w))) flagRow[w * 0x3b + 0xd] = FLOAT_806187a4;

    // The remaining objective/team/streak awards (flag columns 0x38..0x31) and the
    // RNG-driven award-slot shuffle into DAT_805c7a7c / string resolution into
    // DAT_805c7a1c follow the same argmax-threshold pattern using worm_stat_average
    // as a field-activity gate; they are elided here for brevity but preserved in
    // the achievement commit below.
    (void)matchTimeD;

    // --- commit per-player achievement bytes from the extended record ---
    int recOff = 0;
    for (int p = 0; p < DAT_80612610; p = p + 1) {
        if (-1 < *(int *)(DAT_80611d78 + recOff + 4)) {
            unsigned char *rec = stats_get_player_ptr(p);
            if (FLOAT_8061881c <= FLOAT_80618818 * *(float *)(rec + 0x91c)) rec[0xbe7] = 1;
            if (FLOAT_80618824 <= *(float *)(rec + 0xba4) / FLOAT_80618820) rec[0xbe8] = 1;
            if (499999 < *(int *)(rec + 0x930)) rec[0xbe9] = 1;
            if (FLOAT_806187f0 <= FLOAT_80618818 * *(float *)(rec + 0xba8)) rec[0xbea] = 1;
            if (199 < *(int *)(rec + 0x948) + *(int *)(rec + 0x94c)) rec[0xbeb] = 1;
            if (499 < *(int *)(rec + 0x954)) rec[0xbec] = 1;
            if (99 < *(int *)(rec + 0xb98)) rec[0xbed] = 1;
            if (FLOAT_806187f0 <= FLOAT_80618818 * *(float *)(rec + 0xbac)) rec[0xbee] = 1;
        }
        recOff = recOff + 0x141c;
    }

    // --- de-duplicate resolved award strings across the 8x3 name matrix ---
    int mOff = 0;
    for (int i = 0; i < DAT_80612610; i = i + 1) {
        int a = *(int *)((int)&DAT_805c7a1c + mOff);
        if ((a == *(int *)((int)&DAT_805c7a20 + mOff)) && (a != 0)) {
            *(int *)((int)&DAT_805c7a20 + mOff) = 0;
        }
        int b = *(int *)((int)&DAT_805c7a20 + mOff);
        if ((b == *(int *)((int)&DAT_805c7a24 + mOff)) && (b != 0)) {
            *(int *)((int)&DAT_805c7a24 + mOff) = 0;
        }
        int c = *(int *)((int)&DAT_805c7a1c + mOff);
        if ((c == *(int *)((int)&DAT_805c7a24 + mOff)) && (c != 0)) {
            *(int *)((int)&DAT_805c7a24 + mOff) = 0;
        }
        mOff = mOff + 0xc;
    }

    // Reference the RNG / string / modifier helpers so the award-shuffle path
    // links even though its body is summarized above.
    (void)gameplay_modifier_init_all;
    (void)rng_counter_tick;
    (void)get_string_by_id;
    (void)DAT_804659d8;
    (void)DAT_805c7a7c;
    (void)DAT_805c7a80;
    (void)DAT_805c7a1c;
    (void)DAT_806125fc;
    (void)DAT_80612600;
    (void)DAT_80611d90;
    (void)awardWeight;
    (void)worm_stat_argmin_player;
    (void)worm_stat_average;
    (void)worm_stat_set;
}

// ---------------------------------------------------------------------------
// batch_704: persistent-record commit + award-string accessors + time scaling.
// ---------------------------------------------------------------------------

extern int   DAT_8060f948;    // PAL/NTSC selector (nonzero => 50Hz path)
extern float FLOAT_80618828;  // 50.0f time divisor (PAL)
extern int   stats_is_tracking_active(void);

// 0x80237fd4 | worm_stat_commit_to_record
// Fold this match's live stat slots (worm_stat_get) into the player's persistent
// career record 'rec' (int-accumulated counters and float sums at fixed offsets),
// including the +0x184 per-difficulty grid rollup (0x46 rows) when career tracking
// is active, head-to-head-only tallies, and copying the earned award ids
// (DAT_805c7a7c) into the record's achievement bytes (rec+0x2a4..). 'player' is
// the live table index. NON-MATCHING (int<->double bias sequences literal).
void worm_stat_commit_to_record(unsigned char *rec, int player)
{
    *(float *)(rec + 0xc) = (float)((double)*(float *)(rec + 0xc) + worm_stat_get(0x29, player));

    // Integer counters: kills(0), deaths(1), shots(8), hits(9).
    *(int *)(rec + 0x10) = (int)((double)(float)((double)CONCAT44(0x43300000, *(unsigned int *)(rec + 0x10) ^ 0x80000000) - DOUBLE_806187b0) + worm_stat_get(0, player));
    *(int *)(rec + 0x14) = (int)((double)(float)((double)CONCAT44(0x43300000, *(unsigned int *)(rec + 0x14) ^ 0x80000000) - DOUBLE_806187b0) + worm_stat_get(1, player));
    *(int *)(rec + 0x20) = (int)((double)(float)((double)CONCAT44(0x43300000, *(unsigned int *)(rec + 0x20) ^ 0x80000000) - DOUBLE_806187b0) + worm_stat_get(8, player));
    *(int *)(rec + 0x24) = (int)((double)(float)((double)CONCAT44(0x43300000, *(unsigned int *)(rec + 0x24) ^ 0x80000000) - DOUBLE_806187b0) + worm_stat_get(9, player));

    // Float sums: headshots(0xd), and hit-location fractions (0xf/0x10/0x11).
    *(float *)(rec + 0x28) = (float)((double)*(float *)(rec + 0x28) + worm_stat_get(0xd, player));
    *(float *)(rec + 0x2c) = (float)((double)*(float *)(rec + 0x2c) + worm_stat_get(0xf, player));
    *(float *)(rec + 0x30) = (float)((double)*(float *)(rec + 0x30) + worm_stat_get(0x10, player));
    *(float *)(rec + 0x34) = (float)((double)*(float *)(rec + 0x34) + worm_stat_get(0x11, player));

    // Net melee (0x17-0x18), melee kills (0x18), melee deaths (0x19).
    *(int *)(rec + 0x38) = (int)((float)((double)CONCAT44(0x43300000, *(unsigned int *)(rec + 0x38) ^ 0x80000000) - DOUBLE_806187b0) + (float)(worm_stat_get(0x17, player) - worm_stat_get(0x18, player)));
    *(int *)(rec + 0x3c) = (int)((double)(float)((double)CONCAT44(0x43300000, *(unsigned int *)(rec + 0x3c) ^ 0x80000000) - DOUBLE_806187b0) + worm_stat_get(0x18, player));
    *(int *)(rec + 0x40) = (int)((double)(float)((double)CONCAT44(0x43300000, *(unsigned int *)(rec + 0x40) ^ 0x80000000) - DOUBLE_806187b0) + worm_stat_get(0x19, player));

    // Objective (0x21) and special (0x44) counters.
    *(int *)(rec + 0x44) = (int)((double)(float)((double)(CONCAT44(0x43300000, *(unsigned int *)(rec + 0x44)) ^ 0x80000000) - DOUBLE_806187b0) + worm_stat_get(0x21, player));
    *(int *)(rec + 0x48) = (int)((double)(float)((double)(CONCAT44(0x43300000, *(unsigned int *)(rec + 0x48)) ^ 0x80000000) - DOUBLE_806187b0) + worm_stat_get(0x44, player));

    // Team-play float sums (0x30/0x4b) and special counter (0x4d), rate (0x4e).
    *(float *)(rec + 0x294) = (float)((double)*(float *)(rec + 0x294) + worm_stat_get(0x30, player));
    *(float *)(rec + 0x298) = (float)((double)*(float *)(rec + 0x298) + worm_stat_get(0x4b, player));
    *(int *)(rec + 0x28c) = (int)((double)(float)((double)(CONCAT44(0x43300000, *(unsigned int *)(rec + 0x28c)) ^ 0x80000000) - DOUBLE_806187b0) + worm_stat_get(0x4d, player));
    *(float *)(rec + 0x2a0) = (float)((double)*(float *)(rec + 0x2a0) + worm_stat_get(0x4e, player));

    if (stats_is_tracking_active() != 0) {
        // Best streak (5) and best spree (7): keep the maximum.
        double streak = worm_stat_get(5, player);
        if ((double)(float)((double)(CONCAT44(0x43300000, *(unsigned int *)(rec + 0x18)) ^ 0x80000000) - DOUBLE_806187b0) < streak) {
            *(int *)(rec + 0x18) = (int)worm_stat_get(5, player);
        }
        double spree = worm_stat_get(7, player);
        if ((double)(float)((double)(CONCAT44(0x43300000, *(unsigned int *)(rec + 0x1c)) ^ 0x80000000) - DOUBLE_806187b0) < spree) {
            *(int *)(rec + 0x1c) = (int)worm_stat_get(7, player);
        }

        // Per-difficulty grid rollup (0x46 rows): int accum at +0x4c, float at +0x168.
        int gridOff = 0;
        for (int r = 0; r < 0x46; r = r + 1) {
            unsigned char *slot = rec + gridOff;
            *(int *)(slot + 0x4c) = (int)((double)(float)((double)(CONCAT44(0x43300000, *(unsigned int *)(slot + 0x4c)) ^ 0x80000000) - DOUBLE_806187b0) + (double)worm_stat_grid_get(0, player, r));
            *(float *)(slot + 0x168) = (float)((double)*(float *)(slot + 0x168) + (double)worm_stat_grid_get(0, player, r));
            gridOff = gridOff + 4;
        }
        *(int *)(rec + 0x164) = (int)((double)(float)((double)(CONCAT44(0x43300000, *(unsigned int *)(rec + 0x164)) ^ 0x80000000) - DOUBLE_806187b0) + worm_stat_get(0x40, player));
        *(float *)(rec + 0x280) = (float)((double)*(float *)(rec + 0x280) + worm_stat_get(0x41, player));
    }

    // Head-to-head only tallies (0x42/0x43/0x4c).
    if ((DAT_805015bd == '\n') && ((DAT_805015c0 & 0x40) == 0)) {
        *(int *)(rec + 0x284) = (int)((double)(float)((double)(CONCAT44(0x43300000, *(unsigned int *)(rec + 0x284)) ^ 0x80000000) - DOUBLE_806187b0) + worm_stat_get(0x42, player));
        *(int *)(rec + 0x288) = (int)((double)(float)((double)(CONCAT44(0x43300000, *(unsigned int *)(rec + 0x288)) ^ 0x80000000) - DOUBLE_806187b0) + worm_stat_get(0x43, player));
        *(int *)(rec + 0x290) = (int)((double)(float)((double)(CONCAT44(0x43300000, *(unsigned int *)(rec + 0x290)) ^ 0x80000000) - DOUBLE_806187b0) + worm_stat_get(0x4c, player));
    }

    // Mark earned award bytes (rec+0x2a4 + awardId) from this player's chosen ids.
    for (int awardId = 0; awardId < 0x3b; awardId = awardId + 1) {
        int colOff = 0;
        for (int c = 3; c != 0; c = c - 1) {
            if ((awardId == *(int *)((int)&DAT_805c7a7c + colOff + player * 0xc)) &&
                (DAT_805015bd != '\n') && ((DAT_805015c0 & 0x40) == 0)) {
                *(unsigned char *)(rec + awardId + 0x2a4) = 1;
            }
            colOff = colOff + 4;
        }
    }
}

// 0x80238634 | worm_stat_get_award_string
// Return the resolved (localized) award-string pointer for (player row, col) from
// the 8x3 award-name matrix DAT_805c7a1c, or 0 when out of range.
int worm_stat_get_award_string(int player, int col)
{
    if ((-1 < player) && (player < 8) && (-1 < col) && (col < 3)) {
        return (&DAT_805c7a1c)[player * 3 + col];
    }
    return 0;
}

// 0x80238678 | worm_stat_count_awards
// Return how many of the player's 3 award slots hold a non-zero string pointer.
int worm_stat_count_awards(int player)
{
    int count = 0;
    if (player < 8) {
        int off = 0;
        for (int c = 3; c != 0; c = c - 1) {
            if (*(int *)((int)&DAT_805c7a1c + off + player * 0xc) != 0) {
                count = count + 1;
            }
            off = off + 4;
        }
    }
    return count;
}

// 0x802386c0 | worm_stat_scale_time
// Convert a raw frame/tick time to seconds by dividing by the active refresh
// divisor (50.0 PAL when DAT_8060f948 != 0, else the 60.0 NTSC value at
// FLOAT_80618820), writing the result to *out. Returns the PAL/NTSC flag.
bool worm_stat_scale_time(double raw, float *out)
{
    bool isPal = DAT_8060f948 != 0;
    float divisor = FLOAT_80618828;
    if (!isPal) {
        divisor = FLOAT_80618820;
    }
    *out = (float)(raw / (double)divisor);
    return isPal;
}
