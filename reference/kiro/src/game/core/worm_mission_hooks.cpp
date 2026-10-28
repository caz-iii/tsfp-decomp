// worm_mission_hooks.cpp
// Per-game-mode mission hook dispatchers and the "cdh1" objective object
// lifecycle, plus a proximity-based objective tick. These are invoked from
// statlvl_mission_tick / the level MASTER driver (see worm_stat_level.cpp).
//
// Batch: batch_679
// Source: TimeSplitters: Future Perfect (GameCube, main.dol)

#include "common.h"

// ---------------------------------------------------------------------------
// File-local externs for raw symbols referenced by these functions.
// Signatures/types match the call sites in this translation unit only.
// ---------------------------------------------------------------------------

// Named cross-module helpers (resolved via shared headers) are declared there;
// declare the raw FUN_/DAT_ symbols locally.

// Class/objective registry (statlvl neighborhood)
extern int   DAT_806128ac;   // base of 0x4c-stride class slot array
extern int   DAT_80612898;   // int[] of active class indices
extern int   DAT_8061289c;   // count of active class indices

// Proximity objective tick globals
extern int    DAT_80612098;  // player-record count
extern int    DAT_80611d90;  // base of player records (0xea0 stride)
extern uint   DAT_806120d8;  // team bitmask (which side is "hunter")
extern float  FLOAT_8061856c; // initial clamp hi for min-distance
extern float  FLOAT_80618570; // clamp lo
extern float  FLOAT_80618574; // curve numerator / offset
extern float  FLOAT_80618578; // distance bias
extern float  FLOAT_8061857c; // distance scale divisor
extern float  FLOAT_80618568; // completion threshold
extern float  FLOAT_80618580; // cooldown increment
extern float  FLOAT_806108ec; // rate multiplier
extern float  FLOAT_80612888; // accumulator (mutable state)
extern double DOUBLE_80618588; // player-count normalizer base
extern int    DAT_806109c0;   // player-count (int, converted to double)

// Mode gating + object lifecycle
extern int  DAT_80612614;    // current map/mode id
extern int  DAT_805015d8;    // extra-bank-load flag
extern int  DAT_806128b0;    // pointer to the 0x50-byte cdh1 objective object
extern int  DAT_80611d74;    // level-state record base

// Level-phase query table (0x18 stride, indexed by phase param)
extern float DAT_8045f5a8;   // phase threshold table base
extern float fRam8045f5c0;   // phase-1 threshold constant

// Named cross-module helpers. generated_decls.h is not pulled into this TU
// (matching the worm_stat_level.cpp convention), so each helper is declared as
// a file-local extern with a signature matching its call site here.
extern double       math_sqrt(double x);                       // @0x...
extern void         sound_init_play_params(int paramsBlock);   // @0x8000ea08
extern unsigned int sound_play_request(int *paramsBlock);      // @0x8000e924
extern void         memset_wrapper(int dst, int value, int len); // @0x80003100
extern int          mem_alloc(int size, int type);             // shared 2-arg form
extern void         sound_clear_bank_ref(int bank);            // @0x8000ed94 (void)
extern void         entity_preload_resources(int entity_type_id);
extern void         worm_cdh1_tank_break_event(unsigned long long p1,
                                               unsigned long long p2, double p3,
                                               unsigned long long p4,
                                               unsigned long long p5,
                                               unsigned long long p6,
                                               unsigned long long p7,
                                               unsigned long long p8, int self);

// Raw callees not yet decompiled to source.
extern void FUN_80353c3c(int param_1);                         // map-100 arcade hook body
extern void statlvl_class_force_complete(int classId);         // force-complete a class objective

// ---------------------------------------------------------------------------

// 0x802224f0 - Iterate the active-class registry and force-complete any class
// whose slot flag byte (+9) has bit 1 (0x2) set. Called from statlvl_mission_tick.
void statlvl_force_complete_flagged_classes(void)
{
    int idx = 0;
    int byteOff = 0;
    while (idx < DAT_8061289c) {
        int classId = *(int *)(DAT_80612898 + byteOff);
        if ((*(byte *)(DAT_806128ac + classId * 0x4c + 9) & 2) != 0) {
            statlvl_class_force_complete(classId);
        }
        idx = idx + 1;
        byteOff = byteOff + 4;
    }
}

// 0x80222560 - Proximity objective tick: split active players into "hunter"
// (team bit set) and "prey" groups, find the smallest squared distance between
// any hunter/prey pair, drive an accumulator via a distance curve, and when it
// drops to/below the threshold play alarm sound 0x1566 and reset the cooldown.
void statlvl_proximity_objective_tick(void)
{
    float dx;
    float dy;
    float dz;
    double best;
    double d;
    double clamped;
    int preyCount = 0;
    int preyOff = 0;
    int hunterCount = 0;
    int hunterOff = 0;
    int recOff = 0;
    int rec;
    int prey[32];
    int hunters[32];
    int playParams[12];

    int remaining = DAT_80612098;
    if (DAT_80612098 > 0) {
        do {
            int recPtr = *(int *)(DAT_80611d90 + recOff);
            if (recPtr != 0) {
                if ((DAT_806120d8 & (1 << ((int *)(DAT_80611d90 + recOff))[2])) == 0) {
                    if ((*(int *)(recPtr + 0x150) != 0) &&
                        (*(int *)(*(int *)(recPtr + 0x150) + 4) != -1)) {
                        preyCount = preyCount + 1;
                        *(int *)((int)prey + preyOff) = recPtr;
                        preyOff = preyOff + 4;
                    }
                } else {
                    hunterCount = hunterCount + 1;
                    *(int *)((int)hunters + hunterOff) = recPtr;
                    hunterOff = hunterOff + 4;
                }
            }
            recOff = recOff + 0xea0;
            remaining = remaining - 1;
        } while (remaining != 0);
    }

    if ((hunterCount != 0) && (preyCount != 0)) {
        best = (double)FLOAT_8061856c;
        int pOff = 0;
        for (int i = 0; i < preyCount; i = i + 1) {
            int hOff = 0;
            int hRem = hunterCount;
            if (hunterCount > 0) {
                do {
                    int a = *(int *)((int)prey + pOff);
                    int b = *(int *)((int)hunters + hOff);
                    dx = *(float *)(a + 0x68) - *(float *)(b + 0x68);
                    dy = *(float *)(a + 100) - *(float *)(b + 100);
                    dz = *(float *)(a + 0x6c) - *(float *)(b + 0x6c);
                    d = (double)(dz * dz + dy * dy + dx * dx);
                    clamped = best;
                    if (d < best) {
                        clamped = d;
                    }
                    best = (double)FLOAT_80618570;
                    if ((double)FLOAT_80618570 < clamped) {
                        best = clamped;
                    }
                    hOff = hOff + 4;
                    hRem = hRem - 1;
                } while (hRem != 0);
            }
            pOff = pOff + 4;
        }

        double dist = math_sqrt(best);
        float playerNorm = (float)((double)DAT_806109c0 - DOUBLE_80618588);
        FLOAT_80612888 =
            -((FLOAT_80618574 / playerNorm) *
              (FLOAT_80618574 - (float)(dist - (double)FLOAT_80618578) / FLOAT_8061857c) *
              FLOAT_806108ec - FLOAT_80612888);
        if (FLOAT_80612888 <= FLOAT_80618568) {
            sound_init_play_params((int)playParams);
            playParams[0] = 0x1566;
            sound_play_request(playParams);
            FLOAT_80612888 = FLOAT_80612888 + FLOAT_80618580;
        }
    }
}

// 0x8022274c - Map-100 (arcade) gated hook: only dispatch on that map.
void worm_map100_arcade_hook(int param_1)
{
    if (DAT_80612614 == 100) {
        FUN_80353c3c(param_1);
    }
}

// 0x8022277c - No-op hook slot.
void worm_hook_noop(void)
{
}

// 0x80222780 - Map-4 gated hook: forward tank-break event to the cdh1 handler.
// The trailing register-spill params from the decompiler are collapsed to the
// tank-break handler's real signature (see worm_cdh1_tank_break_event decl).
void worm_map4_tank_break_hook(unsigned long long p1, unsigned long long p2,
                               double p3, unsigned long long p4,
                               unsigned long long p5, unsigned long long p6,
                               unsigned long long p7, unsigned long long p8,
                               int self)
{
    if (DAT_80612614 == 4) {
        worm_cdh1_tank_break_event(p1, p2, p3, p4, p5, p6, p7, p8, self);
    }
}

// 0x802227ac - cdh1 objective object spawn/init: allocate the 0x50-byte object,
// preload its sound bank (0xd4a) and entity resources (0x1da), and optionally
// load an extra bank (0x631) when the extra-content flag is set. The extra
// register-spill args produced by the decompiler are dropped to match the
// shared 2-arg mem_alloc / 1-arg entity_preload_resources signatures.
void worm_cdh1_obj_spawn_init(void)
{
    DAT_806128b0 = mem_alloc(0x50, 1);
    sound_clear_bank_ref(0xd4a);
    entity_preload_resources(0x1da);
    if (DAT_805015d8 == 1) {
        sound_clear_bank_ref(0x631);
    }
}

// 0x80222804 - Zero the cdh1 objective object body (0x4c bytes).
void worm_cdh1_obj_reset(void)
{
    memset_wrapper(DAT_806128b0, 0, 0x4c);
}

// 0x80222830 - Level-phase query: given a phase index, return a small flag
// describing whether the current level progress (DAT_80611d74+0xac) has passed
// the phase threshold in the 0x18-stride table at DAT_8045f5a8, with special
// handling for phase 1 (uses the level's stage counter at +0xdec).
uint worm_cdh1_level_phase_query(int param_1)
{
    int stage;

    if (param_1 == 1) {
        if (*(float *)(DAT_80611d74 + 0xac) <= fRam8045f5c0) {
            stage = -*(int *)(DAT_80611d74 + 0xdec) + 0x10;
            return (uint)(stage - ((uint)(stage == 0) +
                                   (uint)(-*(int *)(DAT_80611d74 + 0xdec) + 0xf)));
        }
        return 1;
    }
    if (param_1 < 1) {
        if (param_1 >= 0) {
            if (*(float *)(&DAT_8045f5a8 + param_1 * 0x18) < *(float *)(DAT_80611d74 + 0xac)) {
                return 1;
            }
            if ((*(int *)(DAT_80611d74 + 0xdec) != 1) &&
                (*(int *)(DAT_80611d74 + 0xdec) != 2)) {
                return 1;
            }
            return 0;
        }
    } else if (param_1 < 3) {
        return (uint)(*(float *)(&DAT_8045f5a8 + param_1 * 0x18) <
                      *(float *)(DAT_80611d74 + 0xac));
    }
    return 0;
}

// 0x80222918 - Per-tick update of the cdh1 object: decrement countdown byte
// (+0xc) and bump the elapsed-tick counter (+0x1c).
void worm_cdh1_obj_tick_decrement(void)
{
    *(char *)(DAT_806128b0 + 0xc) = *(char *)(DAT_806128b0 + 0xc) + -1;
    *(int *)(DAT_806128b0 + 0x1c) = *(int *)(DAT_806128b0 + 0x1c) + 1;
}

// 0x8022293c - Map an entity's asset id (+0x94) to a scalar value: the special
// ids 0x5c7 / 0x5d2 / 0x5e1 map to 100, everything else to 0x4b (75).
uint worm_map_id_to_value(int param_1)
{
    ushort id = *(ushort *)(param_1 + 0x94);
    if (id != 0x5d2) {
        if (id < 0x5d2) {
            if (id != 0x5c7) {
                return 0x4b;
            }
        } else if (id != 0x5e1) {
            return 0x4b;
        }
    }
    return 100;
}

// ===========================================================================
// Batch: batch_680
// The "cdh1" objective object (DAT_806128b0, allocated in batch_679) scoring
// and per-frame tick, plus the "CZS" capture-zone objective object
// (DAT_806128b4, a larger 0x19e0-byte object) lifecycle and its zone-capture
// tick. These are the score/update halves of the mission-mode hook table
// installed by FUN_8022c054.
// Source: TimeSplitters: Future Perfect (GameCube, main.dol)
// ===========================================================================

// --- Additional file-local externs for batch_680 -------------------------
// DAT_806128b0 (cdh1 obj) is declared above as `int`; batch_680 treats it as a
// base pointer with word/byte offsets, so all accesses go through casts of that
// int base to preserve a single consistent declaration.

extern int  DAT_806128b4;    // pointer to the 0x19e0-byte CZS objective object
extern int  DAT_80611d78;    // secondary level-state record base
extern int  DAT_80610eb8;    // CZS phase countdown accumulator
extern int  DAT_806108e4;    // per-frame delta (tick increment)
extern int  DAT_8045f5b8;    // CZS phase-table stride base (count field)
extern int  PTR_DAT_8045f5b4; // CZS phase descriptor pointer table base

extern float  FLOAT_80618590; // score/announce timing constant
extern float  FLOAT_80618594; // zero comparison constant (0.0f)
extern float  FLOAT_80618598; // capture rate a
extern float  FLOAT_8061859c; // capture rate b
extern float  FLOAT_806185a8; // clamp constant a
extern float  FLOAT_806185ac; // clamp constant b
extern double DOUBLE_806185a0; // unsigned->double normalizer base

// HUD / string / audio helpers (call-site signatures)
extern void         hud_worldmsg_push(int *rec, short amount);
extern void         hud_text_submit(int channel, int slot, uint *text);
extern double       hud_results_init(int a, uint b, int *c, int d, int e);
extern double       hud_results_init_simple(int score, uint bonus);
extern int          hud_results_poll_finished(void);
extern int          get_string_by_id(int id);
extern int          debug_log_formatted(double d1, double d2, double d3, double d4,
                                        unsigned long long p5, unsigned long long p6,
                                        unsigned long long p7, unsigned long long p8,
                                        int fmtId, int arg, int *p11, int p12,
                                        int p13, int p14, int p15, int p16);
extern void         player_ai_explosion_spawn(int a, int b, float *pos, float *dir,
                                               int owner, int src, int flag);
extern void         character_on_death_init(double d1, double d2, double d3, double d4,
                                            unsigned long long p5, unsigned long long p6,
                                            unsigned long long p7, unsigned long long p8,
                                            int levelRec);

// Raw callees not yet decompiled to source (call-site signatures).
extern void   FUN_803826f0(void);
extern void   FUN_8038273c(void);
extern void   FUN_80237ad8(double a, int b, int c);
extern void   FUN_80237a70(double a, int b, int c);
extern double FUN_80237c50(int a, int b);
extern void   FUN_802477b8(double a, double b, double c);
extern int    FUN_802470d4(void);
extern void   FUN_80244a54(int a);
extern void   FUN_80247898(int a);
extern void   FUN_8024784c(void);
extern void   FUN_802455c8(void);

// ---------------------------------------------------------------------------

// 0x80222974 - Award the fixed 0x32-point objective bonus: push a world message
// and add to the cdh1 object's running score (+0) and short mirror (+6).
void worm_cdh1_award_points(int *param_1)
{
    hud_worldmsg_push(param_1, 0x32);
    *(int *)DAT_806128b0 = *(int *)DAT_806128b0 + 0x32;
    *(short *)(DAT_806128b0 + 6) = *(short *)(DAT_806128b0 + 6) + 0x32;
}

// 0x802229b8 - Score an objective event. param_1 selects the objective kind and
// param_2 is the source entity record. In "extra" mode (DAT_805015d8==2) a chain
// multiplier is accumulated; otherwise a fixed value (0x4b/100/0x32) is awarded.
void worm_cdh1_objective_score(int param_1, int param_2)
{
    int value;
    int *obj = (int *)DAT_806128b0;

    int owner = *(int *)(param_2 + 8);
    if ((owner != *(int *)(DAT_80611d74 + 0x14)) &&
        (*(int *)(param_2 + 4) != *(int *)(DAT_80611d74 + 0x14))) {
        return;
    }

    if ((DAT_805015d8 == 2) && (param_1 == 1)) {
        if (owner != 0) {
            value = (int)worm_map_id_to_value(owner);
            obj[0x10] = obj[0x10] + 1;
            if (obj[0x11] == 0) {
                obj[0x11] = value;
            } else {
                obj[0x11] = obj[0x11] * (obj[0x10] + 1);
            }
            obj[0xe] = *(int *)(param_2 + 8);
            if (*(int *)(param_2 + 0x28) != 0) {
                FUN_80237ad8((double)FLOAT_80618590, 0, *(int *)DAT_80611d74);
                hud_worldmsg_push(*(int **)(param_2 + 0x28), (short)value);
            }
        }
        obj[0xf] = 0;
        return;
    }

    if (param_1 == 2) {
        value = 0x32;
    } else if (param_1 < 2) {
        if (param_1 == 0) {
            value = 0x4b;
            FUN_80237ad8((double)FLOAT_80618590, 0, *(int *)DAT_80611d74);
        } else if (param_1 < 0) {
            value = 0x32;
        } else {
            value = 100;
            FUN_80237ad8((double)FLOAT_80618590, 0, *(int *)DAT_80611d74);
        }
    } else if (3 < param_1) {
        value = 0x32;
    } else {
        value = 0x32;
    }

    if (*(int *)(param_2 + 0x28) != 0) {
        hud_worldmsg_push(*(int **)(param_2 + 0x28), (short)value);
    }
    *(int *)DAT_806128b0 = *(int *)DAT_806128b0 + value;
    *(short *)(DAT_806128b0 + 6) = *(short *)(DAT_806128b0 + 6) + (short)value;
    obj[0xf] = 0;
}

// 0x80222b50 - Return the cdh1 objective object's running score.
unsigned int worm_cdh1_get_score(void)
{
    return *(unsigned int *)DAT_806128b0;
}

// 0x80222b5c - CZS ("capture zone") per-frame capture tick. The current mode
// short (+8) selects one of the CZS1..CZS5 mode tags; when the capture cadence
// and player-count gates pass, advance the capture counters and, on a full
// sub-capture, play the capture sound. Returns 1 when a capture step advanced.
unsigned int worm_czs_zone_capture_tick(int param_1)
{
    short mode;
    unsigned int result;
    int modeTag;
    double thresh;
    int *obj = (int *)DAT_806128b0;

    mode = *(short *)(DAT_806128b0 + 8);
    if (mode == 2) {
        modeTag = 0x435a5333;      // "CZS3"
    } else if (mode < 2) {
        if (mode == 0) {
            modeTag = 0x435a5331;  // "CZS1"
        } else if (-1 < mode) {
            modeTag = 0x435a5332;  // "CZS2"
        } else {
            modeTag = 0x435a5335;  // "CZS5"
        }
    } else if (mode < 4) {
        modeTag = 0x435a5334;      // "CZS4"
    } else {
        modeTag = 0x435a5335;      // "CZS5"
    }

    thresh = (double)FLOAT_80618594;
    if ((thresh < (double)*(float *)(DAT_806128b0 + 0x10)) ||
        (*(int *)(DAT_806128b0 + 0x24) <= (int)(uint)*(byte *)(DAT_806128b0 + 10)) ||
        (modeTag < *(int *)(param_1 + 0x28)) ||
        (0xb < DAT_80612098)) {
        result = 0;
    } else {
        *(float *)(DAT_806128b0 + 0x10) =
            (float)((double)(unsigned int)*(unsigned int *)(DAT_806128b0 + 0x2c) - DOUBLE_806185a0);
        *(char *)(DAT_806128b0 + 0xc) = *(char *)(DAT_806128b0 + 0xc) + 1;
        *(char *)(DAT_806128b0 + 10) = *(char *)(DAT_806128b0 + 10) + 1;

        int sub = (int)*(short *)(DAT_806128b0 + 8);
        if ((DAT_805015d8 == 0) && (2 < sub) &&
            ((int)(uint)*(byte *)(DAT_806128b0 + 0xb) < sub - 2) &&
            ((uint)*(byte *)(DAT_806128b0 + 10) ==
             (uint)((*(byte *)(DAT_806128b0 + 0xb) + 1) *
                    (*(int *)(DAT_806128b0 + 0x24) / (sub - 1))))) {
            FUN_802477b8((double)FLOAT_80618598, thresh, (double)FLOAT_8061859c);
            *(char *)(DAT_806128b0 + 0xb) = *(char *)(DAT_806128b0 + 0xb) + 1;
        }
        result = 1;
    }
    (void)obj;
    return result;
}

// 0x80222cdc - cdh1 mission main tick: advances phase/stage state, drives the
// HUD scoreboard text and results panel, plays phase/finish sounds, and on the
// terminal phase invokes the level end-of-round handler. Runs every frame.
void worm_cdh1_mission_tick(unsigned long long param_1, double param_2,
                            unsigned long long param_3, double param_4,
                            unsigned long long param_5, unsigned long long param_6,
                            unsigned long long param_7, unsigned long long param_8)
{
    float fv;
    int iv;
    unsigned int uv;
    unsigned int uv2;
    double d1 = 0.0;
    double d3 = 0.0;
    int *obj = (int *)DAT_806128b0;
    int *fmtArgs = 0;
    int p12 = 0, p13 = 0, p14 = 0, p15 = 0, p16 = 0;
    int local_78[12];
    int local_a8[12];
    int local_d8[12];

    FUN_803826f0();

    if (DAT_805015d8 == 1) {
        *(char *)(DAT_806128b0 + 6) = 1;
    } else if (DAT_805015d8 < 1) {
        if ((DAT_805015d8 >= 0) &&
            (*(char *)(DAT_806128b0 + 6) = 0, obj[8] == 0)) {
            FUN_80244a54(1);
            FUN_80244a54(2);
            FUN_80247898(2);
            FUN_8024784c();
            obj[8] = 1;
        }
    } else if (DAT_805015d8 < 3) {
        obj[0xf] = obj[0xf] + DAT_806108e4;
        if ((((int)DAT_806109c0 >> 1) < obj[0xf]) && (obj[0x11] != 0)) {
            *(int *)DAT_806128b0 = *(int *)DAT_806128b0 + obj[0x11];
            *(short *)(DAT_806128b0 + 6) = *(short *)(DAT_806128b0 + 6) + (short)obj[0x11];
            if (1 < obj[0x10]) {
                d1 = FUN_80237c50(2, *(int *)DAT_80611d74);
                iv = (int)d1;
                int strId = get_string_by_id(0x66c);
                fmtArgs = (int *)debug_log_formatted(d1, param_2, (double)param_3, param_4,
                                                     param_5, param_6, param_7, param_8,
                                                     strId, obj[0x10], fmtArgs, p12, p13,
                                                     p14, p15, p16);
                hud_text_submit(*(int *)(DAT_80611d74 + 4), 1, (uint *)fmtArgs);
                FUN_80237ad8((double)FLOAT_80618590, 1, *(int *)DAT_80611d74);
                if (iv < obj[0x10]) {
                    FUN_80237a70((double)(float)((double)obj[0x10]), 2, *(int *)DAT_80611d74);
                }
            }
            obj[0x11] = 0;
            obj[0xe] = 0;
            obj[0x10] = 0;
        }
        *(char *)(DAT_806128b0 + 6) = 2;
    }

    if (*(short *)(DAT_806128b0 + 1) == 0) {
        *(short *)(DAT_806128b0 + 2) = 0;
        int table = (&PTR_DAT_8045f5b4)[(uint)*(byte *)(DAT_806128b0 + 6) * 6];
        int slot = *(short *)(DAT_806128b0 + 1) * 0xc;
        obj[9] = *(int *)(table + slot);
        obj[0xb] = (int)*(float *)(table + slot + 4);
        obj[0xc] = *(int *)(table + slot + 8);
    }

    obj[0xd] = obj[0xd] + DAT_806108e4;
    if ((double)FLOAT_80618594 != (double)(float)obj[4]) {
        fv = (float)((double)(float)obj[4] -
                     (double)(float)((double)FLOAT_806108ec / (double)DAT_806109c0));
        obj[4] = (int)fv;
    }
    if ((double)FLOAT_80618594 == (double)(float)obj[5]) {
        obj[5] = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x1cc);
    }

    if (((uint)*(byte *)(DAT_806128b0 + 10) == (uint)obj[9]) &&
        (*(char *)(DAT_806128b0 + 3) == 0) &&
        (*(char *)(DAT_806128b0 + 0x12) == 0) &&
        (FUN_802470d4() == 0)) {
        int strId = get_string_by_id(0x680);
        int *txt = (int *)debug_log_formatted(d1, param_2, d3, param_4, param_5, param_6,
                                              param_7, param_8, strId,
                                              *(short *)(DAT_806128b0 + 2) + 1, fmtArgs,
                                              p12, p13, p14, p15, p16);
        hud_text_submit(*(int *)(DAT_80611d74 + 4), 1, (uint *)txt);
        if (DAT_805015d8 == 1) {
            iv = (obj[0xc] * DAT_806109c0 - obj[0xd]) / DAT_806109c0;
            *(short *)(DAT_806128b0 + 6) = (short)(obj[9] * 100);
            uv = (unsigned int)(iv * 100);
            if (obj[0xd] < obj[0xc] * DAT_806109c0) {
                *(short *)(DAT_806128b0 + 6) = *(short *)(DAT_806128b0 + 6) + (short)uv;
            }
            d1 = hud_results_init(obj[9] * 100, uv, (int *)(obj[0xc] * DAT_806109c0),
                                  obj[0xd], 4);
        } else {
            unsigned int captured =
                (*(float *)(*(int *)(DAT_80611d74 + 0x14) + 0x1cc) == (float)obj[5]);
            if (captured != 0) {
                *(short *)(DAT_806128b0 + 6) = *(short *)(DAT_806128b0 + 6) << 1;
                sound_init_play_params((int)local_78);
                local_78[0] = 0xd64;
                sound_play_request(local_78);
            }
            d1 = hud_results_init_simple((int)*(short *)(DAT_806128b0 + 6), captured);
        }
        *(char *)(DAT_806128b0 + 0x12) = 1;
    }

    if (((uint)*(byte *)(DAT_806128b0 + 10) == (uint)obj[9]) &&
        (*(char *)(DAT_806128b0 + 3) == 0) &&
        (hud_results_poll_finished() != 0) &&
        (*(char *)(DAT_806128b0 + 0x12) != 0) &&
        (FUN_802470d4() == 0)) {
        if (*(short *)(DAT_806128b0 + 1) + 1 <
            *(int *)(&DAT_8045f5b8 + (uint)*(byte *)(DAT_806128b0 + 6) * 0x18)) {
            *(short *)(DAT_806128b0 + 1) = (short)(*(short *)(DAT_806128b0 + 1) + 1);
            *(short *)(DAT_806128b0 + 2) = *(short *)(DAT_806128b0 + 2) + 1;
            int table = (&PTR_DAT_8045f5b4)[(uint)*(byte *)(DAT_806128b0 + 6) * 6];
            int slot = *(short *)(DAT_806128b0 + 1) * 0xc;
            obj[9] = *(int *)(table + slot);
            obj[0xb] = (int)*(float *)(table + slot + 4);
            obj[0xc] = *(int *)(table + slot + 8);
        } else {
            obj[9] = obj[9] + 7;
            *(short *)(DAT_806128b0 + 2) = *(short *)(DAT_806128b0 + 2) + 1;
            fv = (float)((double)obj[0xb] - DOUBLE_806185a0) - FLOAT_806185a8;
            obj[0xb] = (int)fv;
            double lo = (double)(float)((double)obj[0xb] - DOUBLE_806185a0);
            if ((double)FLOAT_80618590 <= lo) {
                fv = (float)lo;
            }
            obj[0xb] = (int)fv;
            double base = (double)(float)((double)obj[0xc] - DOUBLE_806185a0);
            obj[0xc] = (int)(base + (double)FLOAT_806185ac);
        }
        int strId = get_string_by_id(0x681);
        int *txt = (int *)debug_log_formatted(d1, param_2, d3, param_4, param_5, param_6,
                                              param_7, param_8, strId,
                                              *(short *)(DAT_806128b0 + 2) + 1, obj,
                                              p12, p13, p14, p15, p16);
        hud_text_submit(*(int *)(DAT_80611d74 + 4), 1, (uint *)txt);
        sound_init_play_params((int)local_a8);
        local_a8[0] = 0xd69;
        sound_play_request(local_a8);
        obj[5] = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x1cc);
        *(int *)DAT_806128b0 = *(int *)DAT_806128b0 + *(short *)(DAT_806128b0 + 6);
        *(char *)(DAT_806128b0 + 10) = 0;
        obj[7] = 0;
        *(short *)(DAT_806128b0 + 6) = 0;
        obj[0xd] = 0;
        *(char *)(DAT_806128b0 + 0x12) = 0;
        *(char *)(DAT_806128b0 + 0xb) = 0;
    }

    uv2 = worm_cdh1_level_phase_query((uint)*(byte *)(DAT_806128b0 + 6));
    iv = DAT_80610eb8;
    if (uv2 == 0) {
        DAT_80610eb8 = DAT_806109c0 * 3;
    } else {
        if (DAT_80610eb8 == (DAT_80610eb8 / DAT_806109c0) * DAT_806109c0) {
            int strId = get_string_by_id(0x682);
            int *txt = (int *)debug_log_formatted(d1, param_2, d3, param_4, param_5, param_6,
                                                  param_7, param_8, strId, iv / DAT_806109c0,
                                                  fmtArgs, p12, p13, p14, p15, p16);
            hud_text_submit(*(int *)(DAT_80611d74 + 4), 2, (uint *)txt);
            sound_init_play_params((int)local_d8);
            local_d8[0] = 0xd4a;
            sound_play_request(local_d8);
        }
        DAT_80610eb8 = DAT_80610eb8 - DAT_806108e4;
        if (DAT_80610eb8 == 0) {
            character_on_death_init(d1, param_2, d3, param_4, param_5, param_6, param_7,
                                    param_8, *(int *)(DAT_80611d74 + 0x14));
        }
    }

    if (DAT_805015d8 == 0) {
        FUN_802455c8();
    }
    FUN_8038273c();
}

// 0x80223470 - cdh1 timer-expiry explosion: when the object's mode (+0x24) is
// 0x19, count down the level timer (+0x1cc); when it reaches zero, spawn an
// explosion at the owner's position and flag the owner entity.
void worm_cdh1_timer_expire_explosion(int *param_1)
{
    int rec;
    int flags;
    float pos[3];
    float dir[3];

    rec = *param_1;
    if ((param_1[9] == 0x19) && (rec != 0)) {
        *(float *)(rec + 0x1cc) = *(float *)(rec + 0x1cc) - (float)param_1[8];
        if (*(float *)(rec + 0x1cc) <= FLOAT_80618594) {
            flags = *(int *)(rec + 0x7c);
            *(float *)(rec + 0x1cc) = FLOAT_80618594;
            pos[0] = *(float *)(rec + 100) + *(float *)(rec + 0x1a8);
            pos[1] = *(float *)(rec + 0x68) + *(float *)(rec + 0x1ac);
            pos[2] = *(float *)(rec + 0x6c) + *(float *)(rec + 0x1b0);
            dir[0] = FLOAT_80618594;
            dir[1] = FLOAT_80618590;
            dir[2] = FLOAT_80618594;
            player_ai_explosion_spawn(0x2d, 0x17, pos, dir, rec, rec, 1);
            *(uint *)(flags + 0x2c) = *(uint *)(flags + 0x2c) | 0x100000;
        }
    }
}

// 0x80223540 - CZS objective object spawn/init: allocate the 0x19e0-byte CZS
// object and preload its three sound banks (0xd46 / 0xd6f / 0xd70). The extra
// register-spill args from the decompiler collapse to the shared 2-arg
// mem_alloc(size, type) signature.
void worm_czs_obj_spawn_init(void)
{
    DAT_806128b4 = mem_alloc(0x19e0, 1);
    sound_clear_bank_ref(0xd46);
    sound_clear_bank_ref(0xd6f);
    sound_clear_bank_ref(0xd70);
}

// 0x80223584 - CZS objective object resource preload: forward the entity type
// id stored at (+4) of the descriptor to entity_preload_resources.
void worm_czs_obj_preload(int param_9)
{
    entity_preload_resources(*(int *)(param_9 + 4));
}

// 0x802235a8 - Zero the CZS objective object body, then (in mode 4) set a flag
// bit (0x8000) on the secondary level-state record.
void worm_czs_obj_reset(void)
{
    memset_wrapper(DAT_806128b4, 0, 0x19d4);
    memset_wrapper(DAT_806128b4 + 0xc, 0, 0x19c8);
    if (DAT_805015d8 == 4) {
        *(uint *)(DAT_80611d78 + 0xc) = *(uint *)(DAT_80611d78 + 0xc) | 0x8000;
    }
}

// 0x8022360c - Return the CZS objective object's running score.
unsigned int worm_czs_get_score(void)
{
    return *(unsigned int *)DAT_806128b4;
}

// ===========================================================================
// Batch: batch_681
// The "CZS" capture-zone objective's item/pickup subsystem. Each CZS item is a
// 0x58-byte record living in a table at DAT_806128b4+0xc (capacity 0x4b), with
// the record count at DAT_806128b4[1]. These functions locate items, run the
// per-item collect render callbacks, test reach/visibility/zone-ownership,
// build the item table from level info, and drive the massive per-frame item
// animation + collection tick. The final function begins a further objective
// object (DAT_806128b8, 0x60 bytes).
// Source: TimeSplitters: Future Perfect (GameCube, main.dol)
// ===========================================================================

// --- Additional file-local externs for batch_681 -------------------------
extern int DAT_806128b8;    // pointer to a further 0x60-byte objective object
extern int DAT_806128e0;    // per-team CZS ownership-threshold table base

extern float FLOAT_806185b4; // player-count lifetime scale
extern float FLOAT_806185b8; // fade alpha scale
extern float FLOAT_806185bc; // trig curve numerator
extern float FLOAT_806185c0; // trig curve offset a
extern float FLOAT_806185c4; // trig curve divisor
extern float FLOAT_806185c8; // trig curve offset b
extern float FLOAT_806185cc; // orbit radius
extern float FLOAT_806185d0; // snap-to-target distance threshold

extern int DAT_803dffe0;    // spawn descriptor scratch a
extern int DAT_803dffe8;    // spawn descriptor scratch b

// Helpers (call-site signatures)
extern int    FUN_8022cf74(void);                       // current CZS submode (3/4/5)
extern int    FUN_80223658(int *item);                  // CZS item score value
extern double FUN_80237cb4(int a, int b);               // stat query
extern void   FUN_80237b50(double a, int b, int c);     // stat set
extern void   entity_set_render_type(int entity, int type);
extern void   collision_set_shape_params(int *shape, int a, int b, int c, char d);
extern int    soundpad_get_entity_id_from_zone(int zone);
extern int    level_get_info_cached(int levelId);
extern int    entity_spawn_by_type(double d1, double d2, double d3, double d4,
                                   unsigned long long p5, unsigned long long p6,
                                   unsigned long long p7, unsigned long long p8,
                                   int typeId, int *descriptor, int *scratch,
                                   int a, int b);
extern void   render_instance_set_material(double d, int *inst, int flag);
extern void   entity_register_for_update(int entity, int phase, int cb);
extern double math_poly_trig(double x);
extern void   mtx4_set_rotation_y(double angle, float *outMtx);
extern void   mtx4_transform_point(float *mtx, float *pt);
extern int    entity_destroy_with_event(int *entity);
extern unsigned long long __save_gpr_28(void);
extern void   __restore_gpr_28(void);

// FUN_803826f0 is declared void in generated_decls.h (its return-in-r3 context
// pointer is dropped project-wide). Route through a typed function-pointer alias
// to the ROM address so this TU can read the returned entity/context pointer.
// The leading __ makes gen_externs skip it (avoids a conflicting extern).
static int *(*const __worm_ctx_current)(void) = (int *(*)(void))0x803826f0;

// ---------------------------------------------------------------------------

// 0x80223618 - Linear-scan the CZS item table (stride 0x58, first field +0xc is
// the item's spawn-descriptor id) for a matching id; return the item index or
// -1. Capacity is 0x4b (75) slots.
int worm_czs_item_find_index(int param_1)
{
    int index = 0;
    int off = 0;
    int remaining = 0x4b;
    do {
        if (*(int *)(DAT_806128b4 + off + 0xc) == param_1) {
            return index;
        }
        index = index + 1;
        off = off + 0x58;
        remaining = remaining - 1;
    } while (remaining != 0);
    return -1;
}

// 0x80223718 - Entity render/update callback for a collected CZS item (variant
// A). Plays the collect sound (0xd46) at the item's position, applies the
// submode-gated score penalty/message, hides the entity, and marks the item
// state (+5) as 4 (collected). Also nudges a related stat pair when the owner
// is a bot in state 8.
void worm_czs_item_collect_render_a(void)
{
    int *self;
    int mode;
    int owner;
    int aiRec;
    double sVal;
    double sVal2;
    int params[16];
    int *posSrc;

    self = __worm_ctx_current();
    int idx = worm_czs_item_find_index(*self);
    if (-1 < idx) {
        posSrc = (int *)self[10];
        sound_init_play_params((int)params);
        *(char *)((int)params + 0x28) = 1;
        params[0] = 0xd46;
        params[3] = posSrc[0];
        params[4] = posSrc[1];
        params[5] = posSrc[2];
        sound_play_request(params);
        mode = FUN_8022cf74();
        if ((mode == 4) || (FUN_8022cf74() == 5)) {
            *(int *)DAT_806128b4 = *(int *)DAT_806128b4 + -100;
            hud_worldmsg_push((int *)self[10], (short)0xff9c);
        } else if (FUN_8022cf74() == 3) {
            hud_worldmsg_push((int *)self[10], (short)0xff9c);
        }
        entity_set_render_type(*self, 0);
        *(char *)(DAT_806128b4 + idx * 0x16 + 5) = 4;
        owner = self[1];
        if ((owner != 0) && (*(int *)(owner + 0x20) == 8)) {
            aiRec = *(int *)(owner + 0x7c);
            sVal = FUN_80237cb4(8, *(int *)(aiRec + 8));
            sVal2 = FUN_80237cb4(9, *(int *)(aiRec + 8));
            if (sVal2 < sVal) {
                FUN_80237b50((double)FLOAT_80618590, 9, *(int *)(aiRec + 8));
            }
        }
    }
    FUN_8038273c();
}

// 0x8022386c - Entity collect handler for a CZS item (variant B). Same shape as
// variant A but the score delta is computed by FUN_80223658 (submode 4/5 add
// value, submode 3 shows the stored value) and the item id is passed in.
void worm_czs_item_collect_render_b(int *param_1)
{
    int idx = worm_czs_item_find_index(*param_1);
    if (-1 < idx) {
        int mode = FUN_8022cf74();
        if (mode == 4) {
            int value = FUN_80223658(param_1);
            *(int *)DAT_806128b4 = *(int *)DAT_806128b4 + value;
            hud_worldmsg_push((int *)param_1[10], (short)value);
        } else if (FUN_8022cf74() == 3) {
            hud_worldmsg_push((int *)param_1[10],
                              (short)*(int *)(DAT_806128b4 + idx * 0x16 * 4 + 0x30));
        } else if (FUN_8022cf74() == 5) {
            int value = FUN_80223658(param_1);
            *(int *)DAT_806128b4 = *(int *)DAT_806128b4 + value;
            hud_worldmsg_push((int *)param_1[10], (short)value);
        }
        entity_set_render_type(*param_1, 0);
        *(char *)(DAT_806128b4 + idx * 0x16 + 5) = 4;
        int owner = param_1[1];
        if ((owner != 0) && (*(int *)(owner + 0x20) == 8)) {
            int aiRec = *(int *)(owner + 0x7c);
            double sVal = FUN_80237cb4(8, *(int *)(aiRec + 8));
            double sVal2 = FUN_80237cb4(9, *(int *)(aiRec + 8));
            if (sVal2 < sVal) {
                FUN_80237b50((double)FLOAT_80618590, 9, *(int *)(aiRec + 8));
            }
        }
    }
}

// 0x802239c4 - Predicate: has a CZS item reached its target condition given its
// move-type bits (low 3 bits of +10). Type 1 uses a signed height compare
// against a moving band; type 2 uses a fixed height; types 3/4 use a 3D
// distance-from-target compare. Returns nonzero when reached.
unsigned int worm_czs_item_reached_check(int *param_1)
{
    float dx;
    float dy;
    float dz;
    double dist;
    int ent = *param_1;
    unsigned char kind = *(unsigned char *)((int)param_1 + 40) & 7;

    if (kind != 3) {
        if (kind < 3) {
            if (kind == 1) {
                if (FLOAT_80618594 < (float)param_1[0x11]) {
                    return (unsigned int)(*(float *)(ent + 0x70) <=
                                          (float)param_1[0xf] + (float)param_1[0xe]);
                }
                if ((float)param_1[0x11] < FLOAT_80618594) {
                    return (unsigned int)(*(float *)(ent + 0x70) <=
                                          (float)param_1[0xe] - (float)param_1[0xf]);
                }
                return 0;
            }
            if ((*(unsigned char *)((int)param_1 + 40) & 7) != 0) {
                return (unsigned int)((float)param_1[0xf] <= (float)param_1[3]);
            }
        } else if (kind < 5) {
            dx = *(float *)(ent + 0x68) - (float)param_1[0xc];
            dy = *(float *)(ent + 100) - (float)param_1[0xb];
            dz = *(float *)(ent + 0x6c) - (float)param_1[0xd];
            dist = math_sqrt((double)(dz * dz + dy * dy + dx * dx));
            return (unsigned int)((double)(float)param_1[0xf] <= dist);
        }
    }
    return 0;
}

// 0x80223af8 - Shrink/fade a CZS item's collision shape over its lifetime. On
// the first frame (timer +0x1a0 == 0) sets flag bit 2; afterwards scales the
// collision shape opacity by remaining-lifetime fraction.
void worm_czs_item_shrink_fade(int param_1)
{
    float lifeMax;
    unsigned int remaining;

    if (*(int *)(param_1 + 0x1a0) == 0) {
        *(unsigned int *)(param_1 + 0x18) = *(unsigned int *)(param_1 + 0x18) | 2;
    } else {
        remaining = *(unsigned int *)(param_1 + 0x1a4);
        lifeMax = FLOAT_806185b4 * (float)DAT_806109c0;
        if ((float)remaining <= lifeMax) {
            collision_set_shape_params(
                *(int **)(param_1 + 4), 0xf, -1, 0xff,
                (char)(int)((FLOAT_806185b8 * (float)remaining) / lifeMax));
        }
    }
}

// 0x80223bb8 - Predicate: is a CZS item's zone owned by the local player? For
// normal items compares the item's two zone ids (+0x50/+0x54) against the
// local player's current soundpad zone; for team items (+0x28 bit 0x40)
// compares a per-team ownership threshold from DAT_806128e0.
unsigned int worm_czs_zone_owned_by_local(int param_1)
{
    int zoneEnt;

    if ((*(unsigned char *)(param_1 + 0x28) & 0x40) == 0) {
        zoneEnt = soundpad_get_entity_id_from_zone(*(int *)(DAT_80611d90 + 0x678));
        if ((*(int *)(param_1 + 0x50) == zoneEnt) ||
            (*(int *)(param_1 + 0x54) ==
             soundpad_get_entity_id_from_zone(*(int *)(DAT_80611d90 + 0x678)))) {
            return 1;
        }
    } else if ((unsigned int)(*(int *)(param_1 + 0x50) * DAT_806109c0) <
               *(unsigned int *)(&DAT_806128e0 + *(int *)(DAT_80611d74 + 4) * 4)) {
        return 1;
    }
    return 0;
}

// 0x80223c58 - Predicate: is the local player standing in the CZS "special"
// zone for the current game mode (mode 4 -> zone id 0x3fe; modes 3/5 -> 0x429)?
unsigned int worm_czs_special_zone_check(void)
{
    int zoneEnt;

    if (DAT_805015d8 == 4) {
        zoneEnt = soundpad_get_entity_id_from_zone(*(int *)(DAT_80611d90 + 0x678));
        if (zoneEnt == 0x3fe) {
            return 1;
        }
    } else if ((DAT_805015d8 < 4) && (2 < DAT_805015d8)) {
        zoneEnt = soundpad_get_entity_id_from_zone(*(int *)(DAT_80611d90 + 0x678));
        if (zoneEnt == 0x429) {
            return 1;
        }
    }
    return 0;
}

// 0x80223cd4 - Build the CZS item table from the level info descriptor list
// (only in game modes 3/4/5). Each level descriptor (stride 0x40) is expanded
// into a 0x58-byte CZS item record appended at DAT_806128b4[1]++, copying the
// spawn descriptor pointer, position/orientation fields, and setting the
// "special" flag (+6) for descriptor ids 0xf1/0xf3-0xf4/0xf6-0xf7/0xf8.
void worm_czs_item_table_build(void)
{
    float zero;
    int info;
    int srcOff;
    int *dst;
    int i;
    int *src;
    int descId;

    info = level_get_info_cached(DAT_80612614);
    zero = FLOAT_80618594;
    if ((DAT_805015d8 == 4) || (DAT_805015d8 == 3) || (DAT_805015d8 == 5)) {
        srcOff = 0;
        for (i = 0; i < *(int *)(info + 0x100); i = i + 1) {
            src = (int *)(*(int *)(info + 0xfc) + srcOff);
            descId = src[1];
            dst = (int *)(DAT_806128b4 + *(int *)(DAT_806128b4 + 4) * 0x58 + 0xc);
            dst[0] = 0;
            dst[1] = (int)src;
            dst[3] = (int)zero;
            dst[4] = (int)zero;
            *(char *)(dst + 5) = 0;
            *(char *)(dst + 2) = 0;
            if ((descId == 0xf1) || (descId - 0xf3U < 2) ||
                (descId - 0xf6U < 2) || (descId == 0xf8)) {
                dst[6] = 1;
            } else {
                dst[6] = 0;
            }
            dst[7] = 0;
            srcOff = srcOff + 0x40;
            dst[8] = 0;
            dst[9] = src[0xc];
            *(char *)(dst + 10) = (char)src[2];
            *(char *)((int)dst + 0x29) = (char)src[0];
            dst[0xb] = src[3];
            dst[0xc] = src[4];
            dst[0xd] = src[5];
            dst[0xe] = src[6];
            dst[0xf] = src[7];
            dst[0x10] = src[8];
            dst[0x11] = src[9];
            dst[0x12] = src[10];
            dst[0x13] = src[0xb];
            dst[0x14] = src[0xd];
            dst[0x15] = src[0xe];
            *(int *)(DAT_806128b4 + 4) = *(int *)(DAT_806128b4 + 4) + 1;
        }
    }
}

// 0x80223e40 - Per-frame update of every CZS item: spawn its entity when the
// item's zone becomes locally owned, run the per-item motion state machine
// (idle / rise / orbit / oscillate / collected-shrink), test the reach/collect
// condition, and apply collection scoring. Large state machine over the item's
// motion-type byte (+2). The wide FP-register spill / __save_gpr frame from the
// PPC ABI is carried verbatim.
void worm_czs_item_tick_all(double param_1, double param_2, double param_3,
                            double param_4, unsigned long long param_5,
                            unsigned long long param_6, unsigned long long param_7,
                            unsigned long long param_8)
{
    float zero;
    unsigned char state;
    int ent;
    int spawnType;
    int i;
    int off;
    int *item;
    double d13;
    double d14;
    double d15;
    double d16;
    double d17;
    float local_178[3];
    float local_16c[3];
    int spawnScratch;
    float mtxA[16];
    float mtxB[17];

    d13 = (double)__save_gpr_28();
    off = 0;
    for (i = 0; i < *(int *)(DAT_806128b4 + 4); i = i + 1) {
        item = (int *)(DAT_806128b4 + off + 0xc);
        if ((item[8] == 0) && (worm_czs_zone_owned_by_local((int)item) != 0)) {
            int descScratch = DAT_803dffe0;
            spawnScratch = DAT_803dffe8;
            int typeId = *(int *)(item[1] + 4);
            *(int *)(DAT_806128b4 + 8) = *(int *)(DAT_806128b4 + 8) + 1;
            ent = entity_spawn_by_type(d13, param_2, param_3, param_4, param_5, param_6,
                                       param_7, param_8, typeId, item + 0xb,
                                       &descScratch, 0, 0);
            (void)spawnScratch;
            d13 = (double)FLOAT_80618594;
            *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x70;
            render_instance_set_material(d13, *(int **)(ent + 4), 1);
            item[0] = ent;
            spawnType = *(int *)(item[1] + 4);
            if ((spawnType == 0xf1) || (spawnType - 0xf3U < 2) ||
                (spawnType - 0xf6U < 2) || (spawnType == 0xf8)) {
                entity_set_render_type(ent, (int)&worm_czs_item_collect_render_b);
            } else {
                entity_set_render_type(ent, (int)&worm_czs_item_collect_render_a);
            }
            entity_register_for_update(ent, 3, -0x7fddc508);
            if (*(char *)((int)item + 0x29) == 2) {
                int p[16];
                sound_init_play_params((int)p);
                *(char *)((int)p + 0x28) = 1;
                p[0] = 0xd6f;
                p[3] = *(int *)(ent + 100);
                p[4] = *(int *)(ent + 0x68);
                p[5] = *(int *)(ent + 0x6c);
                sound_play_request(p);
            } else {
                int p[16];
                sound_init_play_params((int)p);
                *(char *)((int)p + 0x28) = 1;
                p[0] = 0xd70;
                p[3] = *(int *)(ent + 100);
                p[4] = *(int *)(ent + 0x68);
                p[5] = *(int *)(ent + 0x6c);
                sound_play_request(p);
            }
            item[8] = 1;
        }

        if (item[0] != 0) {
            item[3] = (int)(float)((double)(float)item[3] + (double)FLOAT_806108ec);
            zero = FLOAT_80618594;
            state = *(unsigned char *)(item + 2);
            if (state == 2) {
                unsigned char sub = *(unsigned char *)((int)item + 0x29);
                if (sub == 2) {
                    *(float *)(item[0] + 0x68) =
                        -(FLOAT_806108ec * (float)item[0x11] - *(float *)(item[0] + 0x68));
                } else if (sub < 2) {
                    if (sub != 0) {
                        d13 = math_poly_trig((double)((FLOAT_806185bc *
                                             (FLOAT_806185c0 - *(float *)(item[0] + 0x70))) /
                                             FLOAT_806185c4));
                        *(float *)(item[0] + 100) =
                            -((float)item[0x11] * (float)((double)FLOAT_806108ec * d13) -
                              *(float *)(item[0] + 100));
                        d13 = math_poly_trig((double)((FLOAT_806185bc *
                                             (FLOAT_806185c0 - *(float *)(item[0] + 0x70))) /
                                             FLOAT_806185c4 - FLOAT_806185c8));
                        *(float *)(item[0] + 0x6c) =
                            -((float)item[0x11] * (float)((double)FLOAT_806108ec * d13) -
                              *(float *)(item[0] + 0x6c));
                    }
                } else if ((sub < 6) && (3 < sub)) {
                    ent = item[0];
                    d15 = (double)*(float *)(ent + 100);
                    d17 = (double)*(float *)(ent + 0x68);
                    d16 = (double)*(float *)(ent + 0x6c);
                    d13 = (double)((FLOAT_806185bc * *(float *)(ent + 0x70)) / FLOAT_806185c4);
                    if (sub == 5) {
                        if ((float)item[0x11] < FLOAT_80618594) {
                            d14 = math_poly_trig(d13);
                            d14 = (double)(float)((double)FLOAT_806185cc * d14 + d15);
                            d13 = math_poly_trig((double)(float)(d13 - (double)FLOAT_806185c8));
                            d13 = -(double)(float)((double)FLOAT_806185cc * d13 - d16);
                        } else {
                            d14 = math_poly_trig(d13);
                            d14 = -(double)(float)((double)FLOAT_806185cc * d14 - d15);
                            d13 = math_poly_trig((double)(float)(d13 - (double)FLOAT_806185c8));
                            d13 = (double)(float)((double)FLOAT_806185cc * d13 + d16);
                        }
                    } else if ((float)item[0x11] < FLOAT_80618594) {
                        d14 = math_poly_trig(d13);
                        d14 = -(double)(float)((double)FLOAT_806185cc * d14 - d15);
                        d13 = math_poly_trig((double)(float)(d13 - (double)FLOAT_806185c8));
                        d13 = (double)(float)((double)FLOAT_806185cc * d13 + d16);
                    } else {
                        d14 = math_poly_trig(d13);
                        d14 = (double)(float)((double)FLOAT_806185cc * d14 + d15);
                        d13 = math_poly_trig((double)(float)(d13 - (double)FLOAT_806185c8));
                        d13 = -(double)(float)((double)FLOAT_806185cc * d13 - d16);
                    }
                    local_178[0] = (float)(d15 - d14);
                    local_178[1] = (float)(d17 - d17);
                    local_178[2] = (float)(d16 - d13);
                    d15 = (double)(FLOAT_806108ec * -(float)item[0x11]);
                    mtx4_set_rotation_y(d15, mtxA);
                    mtx4_transform_point(mtxA, local_178);
                    float advance = (float)((double)FLOAT_806185c4 * d15) / FLOAT_806185bc;
                    *(float *)(item[0] + 100) = (float)(d14 + (double)local_178[0]);
                    *(float *)(item[0] + 0x68) = (float)(d17 + (double)local_178[1]);
                    *(float *)(item[0] + 0x6c) = (float)(d13 + (double)local_178[2]);
                    *(float *)(item[0] + 0x70) = *(float *)(item[0] + 0x70) + advance;
                }
                ent = item[0];
                float ddx = *(float *)(ent + 100) - (float)item[0xb];
                float ddz = *(float *)(ent + 0x6c) - (float)item[0xd];
                d13 = math_sqrt((double)(ddz * ddz + ddx * ddx));
                zero = FLOAT_80618594;
                if (d13 < (double)FLOAT_806185d0) {
                    *(int *)(item[0] + 100) = item[0xb];
                    *(int *)(item[0] + 0x68) = item[0xc];
                    *(int *)(item[0] + 0x6c) = item[0xd];
                    item[4] = (int)zero;
                    *(char *)((int)item + 9) = 3;
                    *(char *)(item + 2) = 1;
                }
            } else if (state < 2) {
                if (state == 0) {
                    unsigned int reached = worm_czs_item_reached_check(item);
                    if (reached == 0) {
                        unsigned char sub = *(unsigned char *)((int)item + 0x29);
                        if (sub == 2) {
                            *(float *)(item[0] + 0x68) =
                                (float)((double)FLOAT_806108ec * (double)(float)item[0x11] +
                                        (double)*(float *)(item[0] + 0x68));
                        } else if (sub < 2) {
                            if (sub == 0) {
                                *(char *)((int)item + 9) = 2;
                                *(char *)(item + 2) = 1;
                            } else {
                                d13 = math_poly_trig((double)((FLOAT_806185bc *
                                             (FLOAT_806185c0 - *(float *)(item[0] + 0x70))) /
                                             FLOAT_806185c4));
                                *(float *)(item[0] + 100) =
                                    (float)item[0x11] * (float)((double)FLOAT_806108ec * d13) +
                                    *(float *)(item[0] + 100);
                                d13 = math_poly_trig((double)((FLOAT_806185bc *
                                             (FLOAT_806185c0 - *(float *)(item[0] + 0x70))) /
                                             FLOAT_806185c4 - FLOAT_806185c8));
                                *(float *)(item[0] + 0x6c) =
                                    (float)((double)(float)item[0x11] *
                                            (double)(float)((double)FLOAT_806108ec * d13) +
                                            (double)*(float *)(item[0] + 0x6c));
                            }
                        } else if ((sub < 6) && (3 < sub)) {
                            ent = item[0];
                            d17 = (double)*(float *)(ent + 100);
                            d15 = (double)*(float *)(ent + 0x68);
                            d16 = (double)*(float *)(ent + 0x6c);
                            d13 = (double)((FLOAT_806185bc * *(float *)(ent + 0x70)) /
                                           FLOAT_806185c4);
                            if (sub == 5) {
                                if ((float)item[0x11] < FLOAT_80618594) {
                                    d14 = math_poly_trig(d13);
                                    d14 = (double)(float)((double)FLOAT_806185cc * d14 + d17);
                                    d13 = math_poly_trig((double)(float)(d13 - (double)FLOAT_806185c8));
                                    d13 = -(double)(float)((double)FLOAT_806185cc * d13 - d16);
                                } else {
                                    d14 = math_poly_trig(d13);
                                    d14 = -(double)(float)((double)FLOAT_806185cc * d14 - d17);
                                    d13 = math_poly_trig((double)(float)(d13 - (double)FLOAT_806185c8));
                                    d13 = (double)(float)((double)FLOAT_806185cc * d13 + d16);
                                }
                            } else if ((float)item[0x11] < FLOAT_80618594) {
                                d14 = math_poly_trig(d13);
                                d14 = -(double)(float)((double)FLOAT_806185cc * d14 - d17);
                                d13 = math_poly_trig((double)(float)(d13 - (double)FLOAT_806185c8));
                                d13 = (double)(float)((double)FLOAT_806185cc * d13 + d16);
                            } else {
                                d14 = math_poly_trig(d13);
                                d14 = (double)(float)((double)FLOAT_806185cc * d14 + d17);
                                d13 = math_poly_trig((double)(float)(d13 - (double)FLOAT_806185c8));
                                d13 = -(double)(float)((double)FLOAT_806185cc * d13 - d16);
                            }
                            local_16c[0] = (float)(d17 - d14);
                            local_16c[1] = (float)(d15 - d15);
                            local_16c[2] = (float)(d16 - d13);
                            d16 = (double)(float)((double)FLOAT_806108ec * (double)(float)item[0x11]);
                            mtx4_set_rotation_y(d16, mtxB);
                            mtx4_transform_point(mtxB, local_16c);
                            float advance = (float)((double)FLOAT_806185c4 * d16) / FLOAT_806185bc;
                            *(float *)(item[0] + 100) = (float)(d14 + (double)local_16c[0]);
                            *(float *)(item[0] + 0x68) = (float)(d15 + (double)local_16c[1]);
                            *(float *)(item[0] + 0x6c) = (float)(d13 + (double)local_16c[2]);
                            *(float *)(item[0] + 0x70) =
                                (float)((double)*(float *)(item[0] + 0x70) + (double)advance);
                        }
                    } else {
                        *(char *)((int)item + 9) = 2;
                        *(char *)(item + 2) = 1;
                    }
                } else {
                    if ((float)item[4] <= FLOAT_80618594) {
                        item[4] = item[3];
                    }
                    if ((*(unsigned char *)(item + 10) & 0x20) == 0) {
                        float dur;
                        if (*(char *)((int)item + 9) == 2) {
                            dur = (float)item[0x12];
                        } else {
                            dur = (float)item[0x13];
                        }
                        if ((double)dur <=
                            (double)(float)((double)(float)item[3] - (double)(float)item[4])) {
                            *(char *)(item + 2) = *(char *)((int)item + 9);
                        }
                    } else {
                        if ((double)(float)item[0x12] <=
                            (double)(float)((double)(float)item[3] - (double)(float)item[4])) {
                            *(char *)(item + 2) = 3;
                        }
                    }
                }
            } else if (state == 4) {
                if (FUN_8022cf74() == 3) {
                    if ((item[6] == 0) && (item[7] == 0)) {
                        item[7] = 1;
                        *(int *)DAT_806128b4 = *(int *)DAT_806128b4 - item[9];
                    } else if ((item[6] != 0) && (item[7] == 0)) {
                        item[7] = 1;
                        *(int *)DAT_806128b4 = *(int *)DAT_806128b4 + item[9];
                    }
                }
                ent = item[0];
                if (ent != 0) {
                    *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) & 0xffffff9f;
                    ent = item[0];
                    if (*(int *)(ent + 0x1a0) == 0) {
                        *(int *)(ent + 0x1a0) = 1;
                        *(int *)(*item + 0x1a4) =
                            (int)((double)FLOAT_806185b4 * (double)(float)DAT_806109c0);
                    } else {
                        *(int *)(ent + 0x1a4) = *(int *)(ent + 0x1a4) - DAT_806108e4;
                        if (*(int *)(item[0] + 0x1a4) < 1) {
                            entity_destroy_with_event((int *)item[0]);
                            item[0] = 0;
                        }
                    }
                }
            } else if (state < 4) {
                unsigned char band = *(unsigned char *)(item + 10) & 0x38;
                if (band == 0x10) {
                    item[3] = (int)FLOAT_80618594;
                    item[4] = (int)zero;
                    *(char *)(item + 2) = 0;
                } else if (band < 0x10) {
                    if (band == 8) {
                        *(char *)(item + 2) = 4;
                    }
                } else if ((band == 0x20) && (FUN_8022cf74() == 3) && (item[6] != 0)) {
                    *(char *)(item + 5) = *(char *)(item + 5) + 1;
                    if ((unsigned int)*(unsigned char *)(item + 5) ==
                        (unsigned int)((int)DAT_806109c0 >> 1)) {
                        *(char *)(item + 5) = 0;
                        int v = item[9];
                        item[9] = v - 1;
                        if (-1 < v) {
                            v = item[9];
                            item[9] = v - 1;
                        }
                        item[9] = v;
                    }
                }
            }
        }
        off = off + 0x58;
    }
    __restore_gpr_28();
}

// 0x80224884 - Preload the CZS2 objective's entity resources (types 0xe5/0x385)
// and its 9 sound banks (0xd71..0xd7a range + 0x3b2/0x3b3), then allocate the
// 0x60-byte CZS2 objective object (DAT_806128b8). Wide register-spill args
// collapse to the shared 1-arg entity_preload_resources / 2-arg mem_alloc.
void worm_czs2_obj_preload_init(void)
{
    entity_preload_resources(0xe5);
    entity_preload_resources(0x385);
    sound_clear_bank_ref(0xd71);
    sound_clear_bank_ref(0xd72);
    sound_clear_bank_ref(0xd74);
    sound_clear_bank_ref(0xd77);
    sound_clear_bank_ref(0xd78);
    sound_clear_bank_ref(0xd79);
    sound_clear_bank_ref(0xd7a);
    sound_clear_bank_ref(0x3b2);
    sound_clear_bank_ref(0x3b3);
    DAT_806128b8 = mem_alloc(0x60, 1);
}

// ===========================================================================
// Batch: batch_682
// The CZS2 objective object (DAT_806128b8, 0x60 bytes, allocated in batch_681)
// setup / HUD-arrow render / per-frame tick, plus the "BTTL" and "PLAT"
// level-mechanic handlers (BTL* / PLAT tagged entities, moving-platform and
// battery objectives). Continuation of the mission-mode hook cluster.
// Source: TimeSplitters: Future Perfect (GameCube, main.dol)
// ===========================================================================

// --- Additional file-local externs for batch_682 -------------------------
extern int DAT_806128dc;    // BTTL one-time init latch
extern int DAT_806128e8;    // per-team CZS2 arrow-target entity id table base
extern int DAT_80612610;    // team/local player count (spawn gating)
extern int DAT_80612098;    // total player-record count (declared earlier as extern above)
extern int DAT_80611d78;    // secondary level-state record base (declared earlier)
extern int DAT_80611934;    // current sub-phase id
extern int DAT_80611b04;    // scene material table base
extern int DAT_806108e0;    // secondary per-frame delta
extern int DAT_805015bd;    // single-player / story flag byte
extern unsigned char DAT_80578680; // per-level behaviour table base
extern int DAT_80588610;    // active-entity pointer array base
extern int DAT_805c6db0;    // BTTL HUD label string table a
extern int DAT_805c6fb0;    // BTTL HUD label string table b
extern int DAT_80610ebc;    // hud format context a
extern int DAT_80610ec0;    // hud format context b
extern int s__s___d__s_804604c4; // "%s: %d %s" format string

extern int DAT_803dffec, DAT_803dfff0, DAT_803dfff4, DAT_803dfff8, DAT_803dfffc, DAT_803e0000;
extern int DAT_803e0004, DAT_803e0008, DAT_803e000c, DAT_803e0010, DAT_803e0014, DAT_803e0018;
extern int DAT_803e001c, DAT_803e0020, DAT_803e0024, DAT_803e0028, DAT_803e002c, DAT_803e0030;
extern int DAT_803e0034, DAT_803e0038, DAT_803e003c, DAT_803e0040, DAT_803e0044, DAT_803e0048;
extern int DAT_803e004c, DAT_803e0050, DAT_803e0054, DAT_803e0058, DAT_803e005c, DAT_803e0060;
extern int DAT_803e0064, DAT_803e0068, DAT_803e006c, DAT_803e0070, DAT_803e0074, DAT_803e0078;
extern int DAT_8045fe70, DAT_8045ff30, DAT_8045ffb0; // CZS2 waypoint-path descriptor tables

extern float FLOAT_806185d4; // hud-arrow scroll rate / anim scale
extern float FLOAT_806185d8; // sweep radius / base sound index
extern float FLOAT_806185dc; // 3d sound param
extern float FLOAT_806185e0; // step-toward rate
extern float FLOAT_806185e4; // step-toward scale
extern float FLOAT_806185e8; // rng sound-index scale
extern float FLOAT_806185ec; // rng sound-index divisor
extern float FLOAT_806185f0; // remote alarm pitch
extern double DOUBLE_806185f8; // rng normalizer base
extern float FLOAT_80618600; // platform velocity damp
extern float FLOAT_80618604; // switch-callback param
extern float FLOAT_80618608; // spin rate

// Helpers (call-site signatures)
extern unsigned int entity_spawn_with_activation(double d1, double d2, double d3, double d4,
                                                 unsigned long long p5, unsigned long long p6,
                                                 unsigned long long p7, unsigned long long p8,
                                                 int flags, int typeId, int *desc,
                                                 int *pos, int a, int b);
extern unsigned long long subobj_dirty_list_add(unsigned int id);
extern int  sprite_get_texture_id(int id);
extern void gx_material_state_nop(void);
extern void gx_set_sub_material_id(int id);
extern void gx_set_active_material(int mat);
extern void gx_draw_quad_ptr4_mat(double d1, double d2, double d3, double d4, int mat,
                                  int verts, int *uvA, int *uvB, int *color);
extern void FUN_80242e8c(int entity, float *outPos);
extern int  FUN_80242fb4(void);
extern void build_swept_bounds(double radius, int *outBounds, float *pos, float *vel);
extern int  aabb_vs_entity_bounds(int bounds, float *ent);
extern void sound_update_3d_source(double d1, double d2, double d3, int src, int a, int bank, int b);
extern void scalar_step_toward_snap(double rate, double target, float *value);
extern unsigned int rng_counter_tick(void);
extern double FUN_80237c50(int a, int b);
extern void FUN_80237a70(double a, int b, int c);
extern int  get_string_by_id(int id);
extern int  debug_log_formatted(double d1, double d2, double d3, double d4,
                                unsigned long long p5, unsigned long long p6,
                                unsigned long long p7, unsigned long long p8,
                                int ctx, int fmtId, int p11, int p12,
                                int p13, int p14, int p15, int p16);
extern void hud_text_submit(int channel, int slot, unsigned int *text);
extern void FUN_803826e8(void);
extern void FUN_80382734(void);
extern unsigned long long FUN_8038b510(int *a, int *b);
extern void FUN_80387fb8(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                         unsigned long long p4, unsigned long long p5, unsigned long long p6,
                         unsigned long long p7, unsigned long long p8, int a, int *b,
                         int c, int *d, int e, int f, int g, int h);
extern int  FUN_8023c59c(int tag, int outArray, int max);
extern int  FUN_8023c32c(int tag);
extern void switch_slot_reset(int block);
extern int  scene_node_get_texture_data(int node, int a);
extern int  scene_node_get_material_count(int node, int a);
extern int  floor_polygon_search(int matCount, int texData, unsigned int flags, float *pos, int *out);
extern void damage_character_receive(unsigned long long p1, double p2, double p3, double p4,
                                     double p5, double p6, double p7, unsigned long long p8,
                                     int *victim);
extern int  soundpad_find_zone_by_id(int zoneId);
extern int  projectile_strip_iterate_entities(void);

// anim_play_scaled is declared (ull,double) in generated_decls.h but the call
// here passes two doubles; route through a typed fn-ptr alias to the ROM addr.
static void (*const __worm_anim_play_scaled)(double, double) =
    (void (*)(double, double))0x80023280;

// ---------------------------------------------------------------------------

// 0x80224908 - CZS2 objective spawn/init: zero the 0x54-byte object, set state
// 3, then (by game mode 6/7/8) spawn one or two beacon entities (type 0x385)
// from a per-mode position/orientation table (DAT_803dffec.. blocks) and record
// the waypoint-path descriptor (DAT_8045fe70/ff30/ffb0) + phase count in the
// object. The second beacon only spawns for >1 team.
void worm_czs2_obj_spawn_entities(unsigned long long param_1, double param_2, double param_3,
                                  unsigned long long param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8)
{
    int posA[3];
    int oriA[3];
    int posB[3];
    int oriB[3];
    double ctx = (double)__save_gpr_28();

    memset_wrapper(DAT_806128b8, 0, 0x54);
    *(int *)DAT_806128b8 = 3;
    *(int *)(DAT_806128b8 + 0xf * 4) = -1;
    *(float *)(DAT_806128b8 + 0x14 * 4) = FLOAT_80618590;

    if (DAT_805015d8 == 7) {
        oriA[0] = DAT_803e001c; oriA[1] = DAT_803e0020; oriA[2] = DAT_803e0024;
        posA[0] = DAT_803e0028; posA[1] = DAT_803e002c; posA[2] = DAT_803e0030;
        int ent = entity_spawn_with_activation(ctx, param_2, param_3, param_4, param_5, param_6,
                                               param_7, param_8, 0x10000000, 0x385, posA, oriA, 0, 0);
        *(int *)(DAT_806128b8 + 2 * 4) = ent;
        *(unsigned int *)(*(int *)(ent + 0x7c) + 0x24) |= 1;
        subobj_dirty_list_add(*(unsigned int *)(ent + 0x80));
        if (1 < DAT_80612610) {
            oriB[0] = DAT_803e0034; oriB[1] = DAT_803e0038; oriB[2] = DAT_803e003c;
            posB[0] = DAT_803e0040; posB[1] = DAT_803e0044; posB[2] = DAT_803e0048;
            int ent2 = entity_spawn_with_activation(ctx, param_2, param_3, param_4, param_5, param_6,
                                                    param_7, param_8, 0x10000000, 0x385, posB, oriB, 0, 0);
            *(int *)(DAT_806128b8 + 0xf * 4) = ent2;
            *(unsigned int *)(*(int *)(ent2 + 0x7c) + 0x24) |= 1;
        }
        *(int *)(DAT_806128b8 + 0x12 * 4) = (int)&DAT_8045ff30;
        *(int *)(DAT_806128b8 + 0x11 * 4) = 4;
        *(int *)(DAT_806128b8 + 0xe * 4) = 1;
    } else if (DAT_805015d8 < 7) {
        if (5 < DAT_805015d8) {
            oriA[0] = DAT_803dffec; oriA[1] = DAT_803dfff0; oriA[2] = DAT_803dfff4;
            posA[0] = DAT_803dfff8; posA[1] = DAT_803dfffc; posA[2] = DAT_803e0000;
            int ent = entity_spawn_with_activation(ctx, param_2, param_3, param_4, param_5, param_6,
                                                   param_7, param_8, 0x10000000, 0x385, posA, oriA, 0, 0);
            *(int *)(DAT_806128b8 + 2 * 4) = ent;
            *(unsigned int *)(*(int *)(ent + 0x7c) + 0x24) |= 1;
            if (1 < DAT_80612610) {
                oriB[0] = DAT_803e0004; oriB[1] = DAT_803e0008; oriB[2] = DAT_803e000c;
                posB[0] = DAT_803e0010; posB[1] = DAT_803e0014; posB[2] = DAT_803e0018;
                int ent2 = entity_spawn_with_activation(ctx, param_2, param_3, param_4, param_5, param_6,
                                                        param_7, param_8, 0x10000000, 0x385, posB, oriB, 0, 0);
                *(int *)(DAT_806128b8 + 0xf * 4) = ent2;
                *(unsigned int *)(*(int *)(ent2 + 0x7c) + 0x24) |= 1;
            }
            *(int *)(DAT_806128b8 + 0x12 * 4) = (int)&DAT_8045fe70;
            *(int *)(DAT_806128b8 + 0x11 * 4) = 6;
            *(int *)(DAT_806128b8 + 0xe * 4) = 1;
        }
    } else if (DAT_805015d8 < 9) {
        oriA[0] = DAT_803e004c; oriA[1] = DAT_803e0050; oriA[2] = DAT_803e0054;
        posA[0] = DAT_803e0058; posA[1] = DAT_803e005c; posA[2] = DAT_803e0060;
        int ent = entity_spawn_with_activation(ctx, param_2, param_3, param_4, param_5, param_6,
                                               param_7, param_8, 0x10000000, 0x385, posA, oriA, 0, 0);
        *(int *)(DAT_806128b8 + 2 * 4) = ent;
        *(unsigned int *)(*(int *)(ent + 0x7c) + 0x24) |= 1;
        if (1 < DAT_80612610) {
            oriB[0] = DAT_803e0064; oriB[1] = DAT_803e0068; oriB[2] = DAT_803e006c;
            posB[0] = DAT_803e0070; posB[1] = DAT_803e0074; posB[2] = DAT_803e0078;
            int ent2 = entity_spawn_with_activation(ctx, param_2, param_3, param_4, param_5, param_6,
                                                    param_7, param_8, 0x10000000, 0x385, posB, oriB, 0, 0);
            *(int *)(DAT_806128b8 + 0xf * 4) = ent2;
            *(unsigned int *)(*(int *)(ent2 + 0x7c) + 0x24) |= 1;
        }
        *(int *)(DAT_806128b8 + 0x12 * 4) = (int)&DAT_8045ffb0;
        *(int *)(DAT_806128b8 + 0x11 * 4) = 7;
        *(int *)(DAT_806128b8 + 0xe * 4) = 1;
    }
    __restore_gpr_28();
    (void)param_1;
}

// 0x80224cd8 - Per-view HUD "objective arrow" render for CZS2: scrolls two UV
// offsets (+0x4c/+0x50) at a player-count-normalized rate, wrapping at
// FLOAT_806185d4, and draws a textured quad (sprite texture 0x106) at the
// current waypoint's screen coords. GX render path.
void worm_czs2_hud_arrow_render(void)
{
    int base;
    int tex;
    int wpOff;
    int uvA[8];
    int uvB[8];

    FUN_803826f0();
    base = DAT_806128b8 + *(int *)DAT_80611d74 * 0x34;
    tex = sprite_get_texture_id(0x106);

    wpOff = *(int *)(base + 4) * 0x20;
    int wp = *(int *)(DAT_806128b8 + 0x48) + wpOff;
    uvA[2] = *(int *)(wp + 8);
    uvA[3] = *(int *)(DAT_806128b8 + 0x48) + wpOff;   // preserve source pattern
    *(float *)&uvA[1] = *(float *)(wp + 4) + FLOAT_806185b4;
    uvB[0] = *(int *)(wp + 0xc);
    *(float *)&uvB[1] = *(float *)(wp + 0x10) + FLOAT_806185b4;
    uvB[2] = *(int *)(wp + 0x14);

    float invCount = FLOAT_80618590 / (float)DAT_806109c0;
    *(float *)(DAT_806128b8 + 0x4c) += FLOAT_806108ec * invCount;
    *(float *)(DAT_806128b8 + 0x50) += FLOAT_806108ec * invCount;
    if (FLOAT_806185d4 < *(float *)(DAT_806128b8 + 0x4c)) {
        *(float *)(DAT_806128b8 + 0x4c) -= FLOAT_806185d4;
    }
    if (FLOAT_806185d4 < *(float *)(DAT_806128b8 + 0x50)) {
        *(float *)(DAT_806128b8 + 0x50) -= FLOAT_806185d4;
    }

    gx_material_state_nop();
    gx_set_sub_material_id(1);
    gx_set_active_material(tex);
    int verts = *(int *)(DAT_806128b8 + 0x48) + *(int *)(base + 4) * 0x20;
    gx_draw_quad_ptr4_mat((double)FLOAT_80618594, (double)*(float *)(DAT_806128b8 + 0x4c),
                          (double)*(float *)(DAT_806128b8 + 0x50), (double)FLOAT_80618590,
                          tex, verts, &uvA[3], (int *)(verts + 0xc), uvB);
    FUN_8038273c();
}

// 0x80224eb0 - Predicate: does entity param_1's swept bounding volume this frame
// overlap the CZS2 objective's current waypoint AABB? Builds the entity's swept
// bounds from its velocity and tests against the waypoint min/max box.
bool worm_czs2_entity_in_zone_check(int param_1)
{
    float entPos[3];
    float boxMin;
    int boxA;
    float boxCz;
    float boxMaxX;
    float boxLo;
    float boxHi;
    int bounds[15];
    int base = DAT_806128b8 + *(int *)DAT_80611d74 * 0x34;

    FUN_80242e8c(param_1, entPos);
    int wpOff = *(int *)(base + 4) * 0x20;
    float a = *(float *)(*(int *)(DAT_806128b8 + 0x48) + wpOff);
    boxMin = *(float *)(*(int *)(DAT_806128b8 + 0x48) + wpOff + 0xc);
    if (a < boxMin) { boxMin = a; }

    wpOff = *(int *)(base + 4) * 0x20;
    a = *(float *)(*(int *)(DAT_806128b8 + 0x48) + wpOff);
    boxCz = *(float *)(*(int *)(DAT_806128b8 + 0x48) + wpOff + 0xc);
    if (boxCz < a) { boxCz = a; }

    int wp = *(int *)(DAT_806128b8 + 0x48) + *(int *)(base + 4) * 0x20;
    boxA = *(int *)(wp + 4);
    boxLo = FLOAT_806185b4 + *(float *)(wp + 0x10);
    boxMaxX = *(float *)(wp + 0x14);
    if (*(float *)(wp + 8) < boxMaxX) { boxMaxX = *(float *)(wp + 8); }

    wp = *(int *)(DAT_806128b8 + 0x48) + *(int *)(base + 4) * 0x20;
    a = *(float *)(wp + 8);
    boxHi = *(float *)(wp + 0x14);
    if (boxHi < a) { boxHi = a; }

    (void)boxCz; (void)boxA; (void)boxLo; (void)boxMaxX; (void)boxHi;
    build_swept_bounds((double)(*(float *)(param_1 + 0x1c0) * FLOAT_806185d8), bounds,
                       (float *)(param_1 + 100), entPos);
    return aabb_vs_entity_bounds((int)bounds, &boxMin) != 0;
}

// 0x80225004 - CZS2 per-frame objective tick: advance the tracked entity's
// waypoint progress, play proximity/step/reach sounds, detect zone overlap via
// worm_czs2_entity_in_zone_check, advance the capture count, and on completion
// award the objective + emit the HUD messages. __save_gpr_28 spill frame.
void worm_czs2_obj_tick(unsigned long long param_1, unsigned long long param_2, double param_3,
                        double param_4, unsigned long long param_5, unsigned long long param_6,
                        unsigned long long param_7, unsigned long long param_8)
{
    int *obj;
    int trackedEnt;
    int inZone;
    int phase;
    int p11 = 0, p12 = 0, p13 = 0, p14 = 0, p15 = 0, p16 = 0;
    int local_ec[12];
    int local_bc[5];
    int local_8c[13];
    double dv;

    __save_gpr_28();
    obj = (int *)(DAT_806128b8 + *(int *)DAT_80611d74 * 0x34);
    trackedEnt = obj[2];
    inZone = FUN_80242fb4();
    phase = 3 - obj[0];

    if (*(int *)DAT_80611d74 == 0) {
        sound_update_3d_source((double)*(float *)(DAT_806128b8 + 0x40), (double)FLOAT_806185dc,
                               (double)FLOAT_80618590, DAT_806128b8 + 0x3c, 0, 0xd72, 0);
        float invCount = FLOAT_80618590 / (float)DAT_806109c0;
        scalar_step_toward_snap((double)FLOAT_806185e0,
                                (double)(float)((double)FLOAT_806185e4 *
                                                (double)(float)((double)FLOAT_806108ec *
                                                                (double)invCount)),
                                (float *)(DAT_806128b8 + 0x40));
    }

    if ((phase < 3) && (DAT_80611934 != 5) &&
        ((&DAT_80578680)[*(int *)(DAT_80611d78 + 4) * 0x5df] != 0xb)) {
        obj[phase * 2 + 3] = obj[phase * 2 + 3] + DAT_806108e4;
    }

    if ((inZone != 0) && (obj[0xc] == 0)) {
        unsigned int r = rng_counter_tick();
        int idx = (int)(FLOAT_806185e8 * (float)(((double)r - DOUBLE_806185f8) /
                                                 (double)FLOAT_806185ec) + FLOAT_806185d8);
        sound_init_play_params((int)local_8c);
        local_8c[0] = idx + 0xd77;
        sound_play_request(local_8c);
    }
    obj[0xc] = inZone;

    bool hit = worm_czs2_entity_in_zone_check(trackedEnt);
    if (hit) {
        obj[1] = obj[1] + 1;
        float pitch = FLOAT_80618590;
        if (*(int *)DAT_80611d74 != 0) { pitch = FLOAT_806185f0; }
        sound_init_play_params((int)local_bc);
        *(float *)&local_bc[4] = pitch;
        local_bc[0] = 0xd74;
        sound_play_request(local_bc);

        if (obj[9] != 0) {
            obj[9] = 0;
            unsigned int best = 0x7fffffff;
            obj[0] = obj[0] - 1;
            sound_init_play_params((int)local_ec);
            local_ec[0] = 0xd71;
            sound_play_request(local_ec);
            obj[1] = 0;
            int o = 0;
            int n = 3;
            do {
                unsigned int v = *(unsigned int *)((int)obj + o + 0xc);
                if ((0 < (int)v) && ((int)v < (int)best)) { best = v; }
                o = o + 8;
                n = n - 1;
            } while (n != 0);
            if (best == 0x7fffffff) { best = 0; }
            if (best != 0) {
                dv = FUN_80237c50(10, *(int *)DAT_80611d74);
                if ((double)(float)best < dv) {
                    int strId = get_string_by_id(0x67f);
                    unsigned int *txt = (unsigned int *)debug_log_formatted(
                        dv, DOUBLE_806185a0, param_3, param_4, param_5, param_6, param_7, param_8,
                        (int)&DAT_80610ebc, strId, p11, p12, p13, p14, p15, p16);
                    hud_text_submit(*(int *)(DAT_80611d74 + 4), 3, txt);
                }
            }
            FUN_80237a70((double)(float)best, 10, *(int *)DAT_80611d74);
        }
        if (obj[1] == *(int *)(DAT_806128b8 + 0x44) + -1) {
            obj[9] = 1;
        }
    }

    // waypoint approach / progress
    float epx = *(float *)(trackedEnt + 100);
    float epy = *(float *)(trackedEnt + 0x68);
    float epz = *(float *)(trackedEnt + 0x6c);
    float lag[3];
    FUN_80242e8c(trackedEnt, lag);
    int wp = *(int *)(DAT_806128b8 + 0x48) + obj[1] * 0x20;
    float wy = *(float *)(wp + 4);
    float wx = *(float *)(*(int *)(DAT_806128b8 + 0x48) + obj[1] * 0x20);
    float wz = *(float *)(wp + 8);
    float d0x = epy - wy, d0y = epx - wx, d0z = epz - wz;
    float d1x = (epy - lag[1]) - wy, d1y = (epx - lag[0]) - wx, d1z = (epz - lag[2]) - wz;
    if (d0z * d0z + d0y * d0y + d0x * d0x <= d1z * d1z + d1y * d1y + d1x * d1x) {
        obj[10] = (int)FLOAT_80618594;
        obj[0xb] = 0;
    } else {
        float invCount = FLOAT_80618590 / (float)DAT_806109c0;
        obj[10] = (int)(FLOAT_806108ec * invCount + (float)obj[10]);
        if (FLOAT_80618590 <= (float)obj[10]) {
            unsigned int *txt = (unsigned int *)get_string_by_id(0x654);
            hud_text_submit(*(int *)(DAT_80611d74 + 4), 1, txt);
            obj[10] = (int)FLOAT_80618594;
            obj[0xb] = 1;
        }
        if (obj[0xb] != 0) {
            *(float *)(DAT_806128b8 + 0x40) = FLOAT_80618590;
        }
    }
    __restore_gpr_28();
    (void)param_1; (void)param_2;
}

// 0x80225488 - CZS2 HUD score render: publish the objective's arrow-target
// entity id into the per-team table (DAT_806128e8) and submit the two HUD label
// widgets (string 0x66d) for both teams.
void worm_czs2_hud_score_render(unsigned long long param_1, unsigned long long param_2,
                                unsigned long long param_3, unsigned long long param_4,
                                unsigned long long param_5, unsigned long long param_6,
                                unsigned long long param_7, unsigned long long param_8)
{
    int i = 0;
    int strOff = 0;

    FUN_803826e8();
    *(int *)(&DAT_806128e8 + *(int *)DAT_80611d74 * 4) =
        *(int *)(DAT_806128b8 + *(int *)DAT_80611d74 * 0x34);
    do {
        int *label = (int *)get_string_by_id(0x66d);
        unsigned long long widget = FUN_8038b510((int *)(&DAT_805c6db0 + strOff), label);
        FUN_80387fb8(widget, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     (int)(&DAT_805c6fb0 + strOff + *(int *)(DAT_80611d74 + 4) * 0x1fe),
                     &DAT_80610ec0,
                     *(int *)(&DAT_806128e8 + *(int *)(DAT_80611d74 + 4) * 4),
                     (int *)DAT_80611d74, 0, 0, 0, 0);
        i = i + 1;
        strOff = strOff + 0xff;
    } while (i < 2);
    FUN_80382734();
    (void)param_1;
}

// 0x8022554c - BTTL objective: count active 'BTTL' (0x4254544c) tagged level
// entities into a scratch array (max 0x23).
void worm_bttl_count_active(void)
{
    unsigned char scratch[152];
    FUN_8023c59c(0x4254544c, (int)scratch, 0x23);
}

// 0x8022557c - BTTL/BTL* platform mechanic per-frame tick. One-time snaps AI
// bots into behaviour 10; re-anchors BTL* child entities to their parent;
// drives the 5 BTLC platform movers (floor-height search + vertical velocity
// integrate + switch-callback fire on reaching floor); and spins the 'SPIN'
// entity.
void worm_bttl_platform_tick(void)
{
    int off;
    int i;
    int child;
    int mover;
    float zero;
    int switchBlock[2];
    int local_90;
    float local_78;

    FUN_803826e8();
    if (DAT_806128dc == 0) {
        off = DAT_80612610 << 2;
        for (i = DAT_80612610; i < DAT_80612098; i = i + 1) {
            if (*(int *)(*(int *)((int)&DAT_80588610 + off) + 0x20) != 10) {
                *(int *)(*(int *)((int)&DAT_80588610 + off) + 0x9d0) = 10;
            }
            DAT_806128dc = 1;
            off = off + 4;
        }
    }

    off = DAT_80612610 << 2;
    for (i = DAT_80612610; i < DAT_80612098; i = i + 1) {
        int rec = *(int *)((int)&DAT_80588610 + off);
        if (rec != 0) {
            child = FUN_8023c32c(*(int *)(rec + 8) + 0x42544c30);
            zero = FLOAT_80618594;
            unsigned int flags;
            if (DAT_805015bd == 10) {
                flags = *(unsigned int *)(rec + 0x14);
            } else {
                flags = *(unsigned int *)(rec + 0x1c);
            }
            if (((flags & 1) != 0) && (child != 0) && (*(int **)(child + 0x78) != 0)) {
                *(float *)(child + 100) =
                    *(float *)(**(int **)(child + 0x78) + 100) + *(float *)(child + 0x248);
                *(float *)(child + 0x68) =
                    *(float *)(**(int **)(child + 0x78) + 0x68) + *(float *)(child + 0x24c);
                *(float *)(child + 0x6c) =
                    *(float *)(**(int **)(child + 0x78) + 0x6c) + *(float *)(child + 0x250);
                *(int *)(child + 0x78) = 0;
                *(int *)(child + 0x128) = 0;
                *(int *)(child + 300) = -1;
                *(float *)(child + 0xc0) = zero;
                *(float *)(child + 0x70) = zero;
                *(float *)(child + 200) = zero;
                *(char *)(child + 0x32) = 2;
                *(unsigned int *)(child + 0x28) = *(unsigned int *)(child + 0x28) & 0xbfffffff;
                *(int *)(child + 0x1a0) = 1;
            }
        }
        off = off + 4;
    }

    off = 0;
    do {
        mover = FUN_8023c32c(off + 0x42544c31);
        if ((mover != 0) && (*(int *)(mover + 0x1a0) == 1)) {
            int node = *(int *)(mover + 0x34);
            float invCount = FLOAT_806108ec * (FLOAT_80618590 / (float)DAT_806109c0);
            double dv = (double)invCount;
            int texData = scene_node_get_texture_data(node, 1);
            int matCount = scene_node_get_material_count(*(int *)(mover + 0x34), 1);
            float floorH = (float)floor_polygon_search(
                matCount, texData,
                *(unsigned int *)(*(int *)(*(int *)(DAT_80611b04 + node * 4) + 4) + 0x30),
                (float *)(mover + 100), 0);
            *(float *)(mover + 0x218) = floorH;
            *(float *)(mover + 0xb8) =
                -(float)((double)FLOAT_80618600 * dv - (double)*(float *)(mover + 0xb8));
            *(float *)(mover + 100) += (float)((double)*(float *)(mover + 0xb4) * dv);
            *(float *)(mover + 0x68) += (float)((double)*(float *)(mover + 0xb8) * dv);
            *(float *)(mover + 0x6c) += (float)((double)*(float *)(mover + 0xbc) * dv);
            float dh = *(float *)(mover + 0x68) - *(float *)(mover + 0x1c4);
            if ((dh <= *(float *)(mover + 0x218)) || (dh <= FLOAT_80618594)) {
                switch_slot_reset((int)switchBlock);
                local_78 = FLOAT_80618604;
                local_90 = *(int *)(DAT_80611d74 + 0x14);
                switchBlock[0] = mover;
                (void)local_90; (void)local_78;
                if (*(int *)(mover + 0x19c) != 0) {
                    ((void (*)(int *))*(int *)(mover + 0x19c))(switchBlock);
                }
            }
        }
        off = off + 1;
    } while (off < 5);

    int spin = FUN_8023c32c(0x5350494e);
    if (spin != 0) {
        float dt2 = (float)DAT_806108e0;
        float invCount = FLOAT_80618590 / (float)DAT_806109c0;
        *(float *)(spin + 0x70) =
            -(FLOAT_80618608 * dt2 * invCount - *(float *)(spin + 0x70));
    }
    FUN_80382734();
}

// 0x802258ec - BTTL objective bonus award: add (playerCount * 5) to the team's
// score accumulator (DAT_806128e0) and emit the localized bonus HUD message.
void worm_bttl_award_bonus(unsigned long long param_1, unsigned long long param_2,
                           unsigned long long param_3, unsigned long long param_4,
                           unsigned long long param_5, unsigned long long param_6,
                           unsigned long long param_7, unsigned long long param_8)
{
    int teamOff = *(int *)(DAT_80611d74 + 4) * 4;
    *(int *)(&DAT_806128e0 + teamOff) = *(int *)(&DAT_806128e0 + teamOff) + DAT_806109c0 * 5;
    int name = get_string_by_id(0x1696);
    int fmt = get_string_by_id(0x672);
    unsigned int *txt = (unsigned int *)debug_log_formatted(
        param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
        (int)&s__s___d__s_804604c4, fmt, 5, name, 0, 0, 0, 0);
    hud_text_submit(0, 3, txt);
}

// 0x8022599c - PLAT objective: count active 'PLAT' (0x504c4154) tagged entities
// whose material subtype (+0x30) is 2, plus 7 strips of projectile entities.
int worm_plat_count_active(void)
{
    int count = 0;
    int scratch[61];
    int n = FUN_8023c59c(0x504c4154, (int)scratch, 0x3c);
    int off = 0;
    if (0 < n) {
        do {
            if (*(int *)(*(int *)(*(int *)((int)scratch + off) + 4) + 0x30) == 2) {
                count = count + 1;
            }
            off = off + 4;
            n = n - 1;
        } while (n != 0);
    }
    int a = projectile_strip_iterate_entities();
    int b = projectile_strip_iterate_entities();
    int c = projectile_strip_iterate_entities();
    int d = projectile_strip_iterate_entities();
    int e = projectile_strip_iterate_entities();
    int f = projectile_strip_iterate_entities();
    int g = projectile_strip_iterate_entities();
    return count + a + b + c + d + e + f + g;
}

// 0x80225ab8 - BTTL character damage + respawn hook: applies damage to a victim
// character, then (if it had a linked body) plays the death anim, resolves its
// respawn soundpad zone, and forces the AI into behaviour 0xe (respawn).
void worm_bttl_damage_and_respawn(unsigned long long param_1, double param_2, double param_3,
                                  double param_4, double param_5, double param_6, double param_7,
                                  unsigned long long param_8, int *param_9)
{
    int victim = *param_9;
    param_9[8] = (int)FLOAT_80618590;
    damage_character_receive(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                             param_8, param_9);
    if ((*(int **)(victim + 0x78) != 0) && (**(int **)(victim + 0x78) != 0)) {
        int body = **(int **)(victim + 0x78);
        int aiRec = *(int *)(body + 0x7c);
        __worm_anim_play_scaled((double)FLOAT_80618594, (double)FLOAT_806185d4);
        int zone = soundpad_find_zone_by_id(*(int *)(aiRec + 0x9dc));
        if (zone < 0) {
            *(int *)(aiRec + 0x684) =
                *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        } else {
            *(int *)(aiRec + 0x684) = zone;
        }
        *(int *)(aiRec + 0x9ac) = 0;
        *(int *)(aiRec + 0x9a8) = 0;
        if (*(int *)(aiRec + 0x20) != 0xe) {
            *(int *)(aiRec + 0x9d0) = 0xe;
        }
    }
}

// ============================================================================
// batch_683: 0x80225b74-0x80226550 - the BTTL 'battery/moving-platform'
// mover subsystem (10-slot mover table DAT_805c73ac + a 4-slot battery-entity
// table DAT_805c73d4) plus the start of the PLAT objective object DAT_806128bc.
// Continues the mission-hook cluster; APPENDED to worm_mission_hooks.cpp.
// ============================================================================

// --- Additional file-local externs for batch_683 -------------------------
extern int   DAT_805c73ac;    // base of the 10-slot BTTL mover table (int[10])
extern int   DAT_805c73d4;    // base of the 4-slot BTTL battery-entity table (int[4])
extern int   DAT_806128bc;    // pointer to the 0x1e0-byte PLAT objective object
extern int   DAT_806128d8;    // PLAT one-time HUD-marker latch
extern float FLOAT_80618600;  // platform velocity damp (declared batch_682)
extern float FLOAT_8061860c;  // anim-play scaled arg a
extern float FLOAT_80618610;  // battery link param x
extern float FLOAT_80618614;  // battery link param y
extern float FLOAT_80618618;  // battery link param z
extern float FLOAT_8061861c;  // battery link param w
extern float FLOAT_80618620;  // battery link param v
extern float FLOAT_80618624;  // PLAT spawn scale (fields +0x1cc/+0x1d8)
extern float FLOAT_80618628;  // PLAT hud marker y-offset a
extern float FLOAT_8061862c;  // PLAT hud marker y-offset b
extern float FLOAT_80618630;  // PLAT hud marker param
// PLAT spawn descriptor table (position + orientation words, stride 0x28).
extern int   DAT_803e007c, DAT_803e0080, DAT_803e0084;
extern int   DAT_803e0088, DAT_803e008c, DAT_803e0090;
extern int   entity_special_list_remove(int entity);
extern int  *soundpad_resolve_zone_node(int zone);
extern int   FUN_8023c990(void);                       // returns a scratch/context ptr
extern int   FUN_80355348(int a, double b, int count, int *rec, int param);
extern void  FUN_80355574(double a, int handle);
extern int   damage_filter_invincible(int *ctx);
// switch_slot_reset is declared void elsewhere in the file, but FUN_80225f50
// needs its return value (a scratch block ptr). Alias to the ROM entry with a
// value-returning signature so gen_externs skips it (leading __).
static int (*const __worm_switch_slot_reset)(int) = (int (*)(int))0x80068318;
// FUN_803826f0 is declared void in generated_decls.h; here it is used in its
// index-returning form (self entity index into DAT_80588610). Alias to the ROM
// entry with an int() signature (leading __ so gen_externs skips it).
static int (*const __worm_ctx_index)(void) = (int (*)(void))0x803826f0;

// 0x80225b74 - BTTL mover attach: find the first free slot in the 10-entry
// mover table DAT_805c73ac and attach 'victim' to it, seeding its start
// position from the linked parent (+0x78) plus per-part offsets (+0x248..),
// then compute the initial descent velocity from the objective transform
// (param_1 + 0x274) scaled by FLOAT_806185d4.
void worm_bttl_mover_attach_victim(int obj, int victim)
{
    int off = 0;
    int slots = 10;
    do {
        if (*(int *)((int)&DAT_805c73ac + off) == 0) {
            float startY;
            float local_14;
            float startZ;
            *(int *)((int)&DAT_805c73ac + off) = victim;
            startY = FLOAT_80618594;
            startZ = FLOAT_80618590;
            *(float *)(victim + 100) =
                *(float *)(**(int **)(victim + 0x78) + 100) + *(float *)(victim + 0x248);
            *(float *)(victim + 0x68) =
                *(float *)(**(int **)(victim + 0x78) + 0x68) + *(float *)(victim + 0x24c);
            *(float *)(victim + 0x6c) =
                *(float *)(**(int **)(victim + 0x78) + 0x6c) + *(float *)(victim + 0x250);
            *(int *)(victim + 0x78) = 0;
            *(int *)(victim + 0x128) = 0;
            *(int *)(victim + 300) = -1;
            *(float *)(victim + 0xc0) = startY;
            *(float *)(victim + 0x70) = startY;
            *(float *)(victim + 200) = startY;
            *(unsigned char *)(victim + 0x32) = 2;
            *(unsigned int *)(victim + 0x28) = *(unsigned int *)(victim + 0x28) & 0xbfffffff;
            *(int *)(victim + 0x1a0) = 1;
            *(int *)(obj + 0x20c) = 0;
            local_14 = startY;
            mtx4_transform_point((float *)(obj + 0x274), &startY);
            {
                float scale = FLOAT_806185d4;
                *(float *)(victim + 0xb4) = FLOAT_806185d4 * startY;
                *(float *)(victim + 0xb8) = scale * local_14;
                *(float *)(victim + 0xbc) = scale * startZ;
            }
            return;
        }
        off = off + 4;
        slots = slots - 1;
    } while (slots != 0);
}

// 0x80225ca0 - BTTL mover per-frame render callback (installed on the battery
// entity). Resolves the current context entity, drives its attached mover if
// present, syncs the objective soundpad zone (+0x684) to the current player
// zone, plays a scaled anim, and (once, when AI behaviour != 0xe) awards the
// player-count*5 team bonus with the localized "%s: %d %s" HUD message.
void worm_bttl_mover_render_tick(unsigned long long param_1, double param_2, double param_3,
                                 double param_4, double param_5, double param_6, double param_7,
                                 unsigned long long param_8)
{
    int *ctxSelf = __worm_ctx_current();
    int self = *ctxSelf;
    int mover = *(int *)(self + 0x20c);
    int aiRec = *(int *)(self + 0x7c);
    if ((mover != 0) && (*(int *)(mover + 0x78) != 0)) {
        worm_bttl_mover_attach_victim(self, mover);
    }
    {
        int zone = soundpad_find_zone_by_id(*(int *)(aiRec + 0x9dc));
        if (zone < 0) {
            *(int *)(aiRec + 0x684) =
                *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        } else {
            *(int *)(aiRec + 0x684) = zone;
        }
    }
    *(int *)(aiRec + 0x9ac) = 0;
    *(int *)(aiRec + 0x9a8) = 0;
    __worm_anim_play_scaled((double)FLOAT_8061860c, (double)FLOAT_806185b4);
    {
        unsigned long long inv = (unsigned long long)damage_filter_invincible(ctxSelf);
        if (*(int *)(aiRec + 0x20) != 0xe) {
            int team = *(int *)(DAT_80611d74 + 4) * 4;
            int nameId;
            int fmtId;
            unsigned int *txt;
            *(int *)(&DAT_806128e0 + team) = *(int *)(&DAT_806128e0 + team) + DAT_806109c0 * 5;
            nameId = get_string_by_id(0x1696);
            fmtId = get_string_by_id(0x672);
            txt = (unsigned int *)debug_log_formatted(
                inv, (double)FLOAT_806185b4, param_3, param_4, param_5, param_6, param_7, param_8,
                (int)&s__s___d__s_804604c4, fmtId, 5, nameId, 0, 0, 0, 0);
            hud_text_submit(0, 3, txt);
            if (*(int *)(aiRec + 0x20) != 0xe) {
                *(int *)(aiRec + 0x9d0) = 0xe;
            }
        }
    }
    FUN_8038273c();
}

// 0x80225ddc - BTTL spawn/pair: locate the BTL* battery entity (type 0x5d,
// child slot [8]==0) and the first free-standing entity in DAT_805c73d4, then
// wire their render callbacks (render_tick / damage_and_respawn), set the shared
// render + collision flags (0x4000061), link the free entity's parent to the
// battery, and force the battery model into state 2. Latches the battery's
// AI behaviour word (+0x274) to 0xc when not already set.
void worm_bttl_spawn_pair(void)
{
    int ctx = __worm_ctx_index();
    int battery = 0;
    int *rec = (int *)(&DAT_80588610)[ctx];
    int other;
    int off;
    int slots;
    if ((*(short *)(rec + 0x12) == 0x5d) && (rec[8] == 0)) {
        battery = *rec;
    }
    off = 0;
    slots = 4;
    other = 0;
    do {
        other = *(int *)((int)&DAT_805c73d4 + off);
        if ((other != 0) && ((*(unsigned int *)(other + 0x28) & 1) == 0)) break;
        off = off + 4;
        slots = slots - 1;
        other = 0;
    } while (slots != 0);
    if ((battery != 0) && (other != 0)) {
        float p0 = FLOAT_80618610;
        float p1 = FLOAT_80618614;
        float p2 = FLOAT_80618618;
        float p3 = FLOAT_8061861c;
        float p4 = FLOAT_80618620;
        entity_special_list_remove(battery);
        entity_set_render_type(battery, (int)&worm_bttl_mover_render_tick);
        entity_set_render_type(other, (int)&worm_bttl_damage_and_respawn);
        *(unsigned int *)(other + 0x28) = *(unsigned int *)(other + 0x28) | 0x4000061;
        *(unsigned int *)(battery + 0x28) = *(unsigned int *)(battery + 0x28) | 0x4000061;
        *(unsigned int *)(battery + 0x10) = *(unsigned int *)(battery + 0x10) | 0xffff;
        *(unsigned int *)(other + 0x10) = *(unsigned int *)(other + 0x10) | 0xffff;
        *(int *)(other + 0x78) = *(int *)(battery + 4);
        *(int *)(other + 300) = *(int *)(*(int *)(*(int *)(battery + 4) + 0x84) + 0x30);
        *(int *)(battery + 0x20c) = other;
        *(float *)(other + 0x134) = p0;
        *(float *)(other + 0x138) = p1;
        *(float *)(other + 0x13c) = p2;
        *(float *)(other + 0x140) = p3;
        *(float *)(other + 0x148) = p4;
        *(int *)(*(int *)(other + 4) + 0x30) = 2;
        if (rec[8] != 0xc) {
            rec[0x274] = 0xc;
        }
    }
    FUN_8038273c();
}

// 0x80225f50 - BTTL platform master tick: iterate the 10-entry mover table,
// snap each mover's floor height (floor_polygon_search on its scene node),
// integrate its descent velocity (damped by FLOAT_80618600), and when it
// reaches the target floor apply damage+respawn (worm_bttl_damage_and_respawn)
// and free the slot. Then, per current player zone, fire the spawn-pair trigger
// worm_bttl_spawn_pair for each of the mapped battery zones.
void worm_bttl_platform_master_tick(double param_1, double param_2, double param_3, double param_4,
                                    double param_5, double param_6, double param_7,
                                    unsigned long long param_8)
{
    int i = 0;
    int off = 0;
    int levelRec = *(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c);
    double normDenom = (double)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000)
                                - DOUBLE_806185a0);
    double invCount = (double)(FLOAT_806108ec * (FLOAT_80618590 / (float)normDenom));
    double damp = (double)(float)((double)FLOAT_80618600 * invCount);
    int zone;
    __save_gpr_28();
    do {
        int mover = *(int *)((int)&DAT_805c73ac + off);
        if (mover != 0) {
            int node = *(int *)(mover + 0x34);
            int texData = scene_node_get_texture_data(node, 1);
            int matCount = scene_node_get_material_count(*(int *)(mover + 0x34), 1);
            float floorH = (float)floor_polygon_search(
                matCount, texData,
                *(unsigned int *)(*(int *)(*(int *)(DAT_80611b04 + node * 4) + 4) + 0x30),
                (float *)(mover + 100), (int *)0);
            double velY = (double)*(float *)(mover + 0xbc);
            double stepY = (double)(float)((double)*(float *)(mover + 0xb8) * invCount);
            *(float *)(mover + 0x218) = floorH;
            *(float *)(mover + 0xb8) = (float)((double)*(float *)(mover + 0xb8) - damp);
            *(float *)(mover + 100) =
                *(float *)(mover + 100) + (float)((double)*(float *)(mover + 0xb4) * invCount);
            *(float *)(mover + 0x68) = (float)((double)*(float *)(mover + 0x68) + stepY);
            *(float *)(mover + 0x6c) = *(float *)(mover + 0x6c) + (float)(velY * invCount);
            if (*(float *)(mover + 0x68) <= *(float *)(mover + 0x218)) {
                int switchBlock[2];
                int handle = __worm_switch_slot_reset((int)switchBlock);
                float dmg = FLOAT_80618590;
                int arg[3];
                arg[0] = mover;
                arg[1] = 0;
                (void)handle;
                (void)dmg;
                worm_bttl_damage_and_respawn(0, velY, stepY, param_4, param_5, param_6, param_7,
                                             param_8, arg);
                *(int *)((int)&DAT_805c73ac + off) = 0;
            }
        }
        i = i + 1;
        off = off + 4;
    } while (i < 10);

    zone = soundpad_find_zone_by_id(0x494);
    if ((*(int *)(levelRec + 0x678) == zone) ||
        (zone = soundpad_find_zone_by_id(0x487), *(int *)(levelRec + 0x678) == zone)) {
        worm_bttl_spawn_pair();
    }
    zone = soundpad_find_zone_by_id(0x3f3);
    if ((*(int *)(levelRec + 0x678) == zone) ||
        (zone = soundpad_find_zone_by_id(0x450), *(int *)(levelRec + 0x678) == zone)) {
        worm_bttl_spawn_pair();
    }
    zone = soundpad_find_zone_by_id(0x481);
    if (*(int *)(levelRec + 0x678) == zone) {
        worm_bttl_spawn_pair();
    }
    zone = soundpad_find_zone_by_id(0x447);
    if (((*(int *)(levelRec + 0x678) == zone) ||
         (zone = soundpad_find_zone_by_id(0x44f), *(int *)(levelRec + 0x678) == zone)) ||
        (zone = soundpad_find_zone_by_id(0x441), *(int *)(levelRec + 0x678) == zone)) {
        worm_bttl_spawn_pair();
    }
    *(int *)(DAT_80611d78 + 0xe10) = 0;
    __restore_gpr_28();
}

// 0x802261ac - PLAT objective: count active 'PLAT' (0x504c4154) tagged
// entities whose material subtype (+0x30) == 2. (Variant of
// worm_plat_count_active without the projectile-strip tally.)
int worm_plat_count_active_material2(void)
{
    int count = 0;
    int scratch[61];
    int n = FUN_8023c59c(0x504c4154, (int)scratch, 0x3c);
    int off = 0;
    if (0 < n) {
        do {
            if (*(int *)(*(int *)(*(int *)((int)scratch + off) + 4) + 0x30) == 2) {
                count = count + 1;
            }
            off = off + 4;
            n = n - 1;
        } while (n != 0);
    }
    return count;
}

// 0x80226220 - PLAT spawn: clear the render/collision flags on all active
// player-record entities (and reset any BTL battery behaviour word), then
// spawn 4 PLAT platform entities from the descriptor table DAT_803e007c,
// tagging each active (flag 0x10, state 2) and scaling fields +0x1cc/+0x1d8.
void worm_plat_spawn_entities(double param_1, double param_2, double param_3, double param_4,
                              double param_5, double param_6, double param_7,
                              unsigned long long param_8)
{
    int posDesc[3];
    int oriDesc[3];
    int off;
    int i;
    int slot;
    posDesc[0] = DAT_803e007c;
    posDesc[1] = DAT_803e0080;
    posDesc[2] = DAT_803e0084;
    oriDesc[0] = DAT_803e0088;
    oriDesc[1] = DAT_803e008c;
    oriDesc[2] = DAT_803e0090;
    memset_wrapper((int)&DAT_806128bc, 0, 0x28);
    off = DAT_80612610 << 2;
    for (i = DAT_80612610; i < DAT_80612098; i = i + 1) {
        int *rec = *(int **)((int)&DAT_80588610 + off);
        *(unsigned int *)(*rec + 0x28) = *(unsigned int *)(*rec + 0x28) & 0xffffff9e;
        if ((*(short *)(rec + 0x12) == 0x5d) && (rec[8] != 0)) {
            rec[0x274] = 0;
        }
        off = off + 4;
    }
    i = 0;
    slot = 0;
    do {
        int ent = (int)entity_spawn_by_type(param_1, param_2, param_3, param_4, param_5, param_6,
                                            param_7, param_8, 0xa0, posDesc, oriDesc, 0, 0);
        if (ent != 0) {
            float scale = FLOAT_80618624;
            *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x10;
            *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) & 0xfbffff9e;
            *(int *)((int)&DAT_805c73d4 + slot) = ent;
            *(int *)(*(int *)(ent + 4) + 0x30) = 2;
            *(float *)(ent + 0x1d8) = scale;
            *(float *)(ent + 0x1cc) = scale;
            FUN_8023c990();
        }
        i = i + 1;
        slot = slot + 4;
    } while (i < 4);
}

// 0x80226388 - PLAT: is the local player standing in the objective zone for the
// current map, with the objective entity marked reached (+0x80 != 0)? Maps the
// current map id (DAT_80612614) to a soundpad zone id.
int worm_plat_local_in_zone_check(void)
{
    int zoneId = -1;
    if (DAT_80612614 == 0x49) {
        zoneId = 0x51a;
    } else if (DAT_80612614 < 0x49) {
        if (0x47 < DAT_80612614) {
            zoneId = 0x485;
        }
    } else if (DAT_80612614 < 0x4b) {
        zoneId = 0x4a3;
    }
    if (((zoneId < 0) ||
         (zoneId = soundpad_find_zone_by_id(zoneId), *(int *)(DAT_80588610 + 0x678) != zoneId)) ||
        (*(int *)(DAT_80588610 + 0x80) == 0)) {
        return 0;
    }
    return 1;
}

// 0x80226420 - PLAT HUD marker: resolve the soundpad zone node for zoneId, take
// its centre (+0xc..+0x18), lift it by FLOAT_80618628/2c, and submit a HUD
// results/objective marker via FUN_80355348 + FUN_80355574.
void worm_plat_hud_marker_submit(int zoneId, int param_2, int lifted)
{
    int zone = soundpad_find_zone_by_id(zoneId);
    int *node = soundpad_resolve_zone_node(zone);
    int markerPos[3];
    float y = FLOAT_8061862c;
    int handle;
    markerPos[0] = node[3];
    if (lifted != 0) {
        y = FLOAT_80618628;
    }
    ((float *)markerPos)[1] = (float)node[4] + y;
    markerPos[2] = node[5];
    handle = FUN_80355348((int)(float)node[6], (double)FLOAT_80618630, 3, markerPos, param_2);
    FUN_80355574((double)FLOAT_80618594, handle);
}

// 0x802264b0 - PLAT: one-time HUD marker trigger. When the objective entity is
// reached (+0x80 != 0) and the latch DAT_806128d8 is clear, resolve the current
// map's zone id + marker param and submit the marker once.
void worm_plat_hud_trigger_once(void)
{
    int zoneId = -1;
    int param = -1;
    if ((*(int *)(DAT_80588610 + 0x80) != 0) && (DAT_806128d8 == 0)) {
        if (DAT_80612614 == 0x49) {
            zoneId = 0x51a;
            param = 0xc;
        } else if (DAT_80612614 < 0x49) {
            if (0x47 < DAT_80612614) {
                zoneId = 0x485;
                param = 6;
            }
        } else if (DAT_80612614 < 0x4b) {
            zoneId = 0x4a3;
            param = 0x13;
        }
        worm_plat_hud_marker_submit(zoneId, param, 0);
        DAT_806128d8 = 1;
    }
}

// 0x80226550 - PLAT objective object preload/alloc: allocate the 0x1e0-byte
// PLAT objective object DAT_806128bc and preload entity resource 0x115.
// (register-spill args collapsed to the shared mem_alloc(size,type) /
// entity_preload_resources(id) signatures.)
void worm_plat_obj_preload_init(void)
{
    DAT_806128bc = mem_alloc(0x1e0, 1);
    entity_preload_resources(0x115);
}

// ============================================================================
// batch_684: 0x80226584-0x80227234 - the full lifecycle of the ASL (assault /
// timed-wave) objective object DAT_806128bc (the 0x1e0-byte object allocated by
// worm_plat_obj_preload_init in batch_683). init/particle-defs, wave spawn,
// per-entity fuse tick, HUD marker render, master per-frame tick, and a mover
// attach helper on the object's own 10-slot table (obj+0x68). Continues the
// mission-hook cluster; APPENDED to worm_mission_hooks.cpp.
// ============================================================================

// --- Additional file-local externs for batch_684 -------------------------
extern int   DAT_80460f68;    // per-wave spawn-count table base (int[5])
extern int   DAT_80611748;    // camera/viewport matrix scratch (int[10], read as words)
extern int   DAT_80611d54;    // active-camera record base (matrices at +0x204/+0x208)
extern int   DAT_803e00a4;    // particle keyframe source block B (DAT_803e0090 declared earlier)
void worm_asl_entity_detonate(int *param_1);   // forward decl (used by spawn_next_entity)
extern float DAT_803e00bc, DAT_803e00c0, DAT_803e00c4; // dust emitter base velocity
extern float DAT_803e00c8, DAT_803e00cc, DAT_803e00d0; // explosion direction vector
extern float FLOAT_80618634, FLOAT_80618638, FLOAT_8061863c; // particle def color/size consts
extern float FLOAT_80618640, FLOAT_80618644, FLOAT_80618648; // particle velocity-range consts
extern float FLOAT_8061864c;  // dust-velocity min-length threshold
extern float FLOAT_80618650, FLOAT_80618654; // fuse phase band lo/hi
extern float FLOAT_80618658;  // fuse advance rate
extern float FLOAT_8061865c, FLOAT_80618660, FLOAT_80618664, FLOAT_80618668; // HUD marker rect corners
extern float FLOAT_8061866c;  // wave phase-timer threshold
extern float FLOAT_80618670;  // detonate anim rng scale
extern void   particle_emitter_spawn(int a, int emitter, int b, float *pos, float *vel);
extern void   particle_emitter_set_gravity(double g);
extern void   particle_def_create(double d1, double d2, int def, int a, int b, int c, int d, int e);
extern void   particle_def_set_size_keyframes(double d1, double d2, double d3, double d4,
                                              int c0, int c1, int c2);
extern void   particle_def_set_keyframes_raw(int a, int b, int c);
extern void   particle_def_set_size_start_end(double d1, double d2, double d3, double d4);
extern void   particle_def_set_color_end(double d1, double d2, double d3, double d4);
extern void   particle_def_set_velocity_ranges(double d1, double d2, double d3, double d4,
                                               double d5, double d6);
extern void   particle_emitter_create(double d1, double d2, double d3, int emitter, int def,
                                       int a, int b);
extern void   particle_emitter_set_color_range(double d1, double d2, double d3, double d4);
extern void   anim_set_blend_weight(double w, int model);
extern void   character_damage_spawn_effects(void);
extern void   player_ai_teleport_wrapper(int *entity, int zone, int flag);
extern void   particle_kill_by_entity(void);
extern void   gx_material_state_nop(void);
extern void   gx_set_sub_material_id(int id);
extern void   mtx4_transform_point_alias(float *mtx, float *pt);
extern void   mtx4_transform_point4_alias(float *mtx, float *pt, float *out);
extern void   gx_draw_fill_rect_f_ex(double x0, double y0, double x1, double y1, double z);
extern void   FUN_803826e4(void);
extern void   FUN_80382730(void);

// 0x80226584 - ASL objective init: zero the 0x1e0 object, seed the per-wave
// spawn-count pointer (+0x18 = DAT_80460f68), copy two particle keyframe source
// blocks into local scratch, then build the dust particle definition (+0x114)
// and its emitter (+0x90) with size/color/velocity ranges. Sets object mode
// word (+0xc) to 6.
void worm_asl_obj_init(void)
{
    // Two keyframe blocks laid out contiguously in one scratch buffer, matching
    // the ROM stack layout: block A copied to buf[5..8], block B to buf[0..3];
    // keyframes_raw then reads buf (block B) and buf+5 (block A).
    int buf[16];
    int i;
    int *src;
    int *dst;
    memset_wrapper(DAT_806128bc, 0, 0x1e0);
    *(int *)(DAT_806128bc + 0x18) = DAT_80460f68;
    src = (int *)&DAT_803e0090;
    dst = &buf[4];
    for (i = 2; i != 0; i = i - 1) {
        dst[1] = src[1];
        dst[2] = src[2];
        src = src + 2;
        dst = dst + 2;
    }
    dst[3] = src[3];
    src = (int *)&DAT_803e00a4;
    dst = &buf[8];
    for (i = 2; i != 0; i = i - 1) {
        dst[1] = src[1];
        dst[2] = src[2];
        src = src + 2;
        dst = dst + 2;
    }
    dst[3] = src[3];
    particle_def_create((double)FLOAT_806185f0, (double)FLOAT_80618590, DAT_806128bc + 0x114,
                        0, 0, 1, 0x300, 0);
    particle_def_set_size_keyframes((double)FLOAT_80618590, (double)FLOAT_80618594,
                                    (double)FLOAT_80618634, (double)FLOAT_80618634,
                                    0xe0e0e00, 0xe0e0e00, 0x1b1b1b00);
    particle_def_set_keyframes_raw((int)&buf[8], (int)&buf[5], 2);
    particle_def_set_size_start_end((double)FLOAT_80618594, (double)FLOAT_806185c0,
                                    (double)FLOAT_806185d4, (double)FLOAT_80618590);
    particle_def_set_color_end((double)FLOAT_806185ac, (double)FLOAT_80618594,
                               (double)FLOAT_80618638, (double)FLOAT_8061863c);
    particle_def_set_velocity_ranges((double)FLOAT_80618640, (double)FLOAT_80618594,
                                     (double)FLOAT_80618644, (double)FLOAT_806185d8,
                                     (double)FLOAT_80618590, (double)FLOAT_80618590);
    particle_emitter_create((double)FLOAT_806185ac, (double)FLOAT_80618594, (double)FLOAT_80618648,
                            DAT_806128bc + 0x90, DAT_806128bc + 0x114, 0, 1);
    particle_emitter_set_color_range((double)FLOAT_80618594, (double)FLOAT_80618594,
                                     (double)FLOAT_80618590, (double)FLOAT_80618594);
    *(int *)(DAT_806128bc + 0xc) = 6;
}

// 0x80226714 - ASL dust emitter spawn: normalize the base dust velocity
// DAT_803e00bc.. (renormalized to unit-ish length when above threshold), and
// when the entity's fuse phase (+0x9e0) is within the band [FLOAT_80618650,
// FLOAT_80618654) spawn the object's dust emitter (+0x90) at the entity pos.
void worm_asl_spawn_dust_emitter(int entity)
{
    float vel[3];
    float phase;
    float lenSq;
    vel[0] = DAT_803e00bc;
    vel[1] = DAT_803e00c0;
    vel[2] = DAT_803e00c4;
    lenSq = DAT_803e00c4 * DAT_803e00c4 + DAT_803e00bc * DAT_803e00bc + DAT_803e00c0 * DAT_803e00c0;
    if (FLOAT_8061864c < lenSq) {
        float inv = 1.0f / (float)math_sqrt((double)lenSq);
        float s = inv * -(FLOAT_806185d8 * lenSq * inv * inv - FLOAT_806185b4);
        vel[2] = FLOAT_80618590 * s * -(FLOAT_806185d8 * lenSq * s * s - FLOAT_806185b4);
        vel[0] = DAT_803e00bc * vel[2];
        vel[1] = DAT_803e00c0 * vel[2];
        vel[2] = DAT_803e00c4 * vel[2];
    }
    phase = *(float *)(*(int *)(entity + 0x7c) + 0x9e0);
    if ((FLOAT_80618650 < phase) && (phase < FLOAT_80618654)) {
        particle_emitter_spawn(0, DAT_806128bc + 0x90, 0, (float *)(entity + 100), vel);
        particle_emitter_set_gravity((double)FLOAT_80618590);
    }
}

// 0x80226808 - ASL entity fuse tick (render callback): while the entity's AI
// state (+0x20) is 3, advance the fuse counter (+0x9e0) by scaled damage, blend
// the wobble anim, and once the fuse passes FLOAT_80618654 spawn the damage
// effects; when it passes FLOAT_80618628 detonate a size-0x20 explosion, clear
// the entity render callback and decrement the object's active count (+0xc).
void worm_asl_entity_fuse_tick(void)
{
    int *ctx = __worm_ctx_current();
    int entity = *ctx;
    int aiRec = *(int *)(entity + 0x7c);
    if (*(int *)(aiRec + 0x20) == 3) {
        ctx[8] = (int)((float)ctx[8] * FLOAT_806185d4);
        float scale = FLOAT_806185d4;
        if (*(float *)(aiRec + 0x9e0) < FLOAT_806185d4) {
            *(float *)(aiRec + 0x9e0) =
                (float)ctx[8] * FLOAT_80618658 + *(float *)(aiRec + 0x9e0);
            anim_set_blend_weight((double)(scale * *(float *)(aiRec + 0x9e0)),
                                  *(int *)(entity + 4));
        }
        if (FLOAT_80618654 <= *(float *)(aiRec + 0x9e0)) {
            *(float *)(aiRec + 0x9e0) = *(float *)(aiRec + 0x9e0) + FLOAT_806185a8;
            character_damage_spawn_effects();
            entity_register_for_update(entity, 3, 0);
            if (FLOAT_80618628 < *(float *)(aiRec + 0x9e0)) {
                float dir[3];
                dir[0] = DAT_803e00c8;
                dir[1] = DAT_803e00cc;
                dir[2] = DAT_803e00d0;
                player_ai_explosion_spawn(0x20, 0xffffffff, (float *)(entity + 100), dir, 0,
                                          entity, 1);
                if (*(int *)(aiRec + 0x20) != 0) {
                    *(int *)(aiRec + 0x9d0) = 0;
                }
                *(int *)(DAT_806128bc + 0xc) = *(int *)(DAT_806128bc + 0xc) - 1;
                entity_set_render_type(entity, 0);
            }
        }
        damage_filter_invincible(ctx);
    }
    FUN_8038273c();
}

// 0x80226974 - ASL spawn next entity: find the first free 0x5e-type entity in
// the active player-record list (DAT_80588610), install the fuse-tick render
// callback + per-frame update, set its render/collision flags, latch its AI
// behaviour (+0x274) to 1, and bump the object's spawned count (+0x14).
void worm_asl_spawn_next_entity(void)
{
    int entity = 0;
    int *rec = (int *)0;
    int off = DAT_80612610 << 2;
    int n = DAT_80612098 - DAT_80612610;
    if (DAT_80612610 < DAT_80612098) {
        do {
            rec = *(int **)((int)&DAT_80588610 + off);
            if ((*(short *)(rec + 0x12) == 0x5e) && (rec[8] == 0)) {
                entity = *rec;
                break;
            }
            off = off + 4;
            n = n - 1;
        } while (n != 0);
    }
    if (entity != 0) {
        entity_set_render_type(entity, (int)&worm_asl_entity_fuse_tick);
        entity_register_for_update(entity, 3, (int)&worm_asl_entity_detonate);
        *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x4000001;
        *(unsigned int *)(entity + 0x10) = *(unsigned int *)(entity + 0x10) | 0xffff;
        if (rec[8] != 1) {
            rec[0x274] = 1;
        }
        *(int *)(DAT_806128bc + 0x14) = *(int *)(DAT_806128bc + 0x14) + 1;
    }
}

// 0x80226a64 - ASL HUD marker render (render callback): while the entity is in
// AI state 3, project its world position into screen space via the active
// camera matrices (DAT_80611d54 +0x204/+0x208), and if on-screen draw a pair of
// fuse-progress fill-rect markers (height scaled by fuse fraction +0x9e0). Pure
// 2D overlay; math heavy (paired-single / 0x43300000 int->double biases).
void worm_asl_hud_marker_render(int entity)
{
    int aiRec = *(int *)(entity + 0x7c);
    if (*(int *)(aiRec + 0x20) == 3) {
        float p[3];
        float proj[4];
        p[0] = *(float *)(entity + 100);
        p[1] = *(float *)(entity + 0x68);
        p[2] = *(float *)(entity + 0x6c);
        gx_material_state_nop();
        mtx4_transform_point_alias(*(float **)(DAT_80611d54 + 0x208), p);
        mtx4_transform_point4_alias(*(float **)(DAT_80611d54 + 0x204), p, proj);
        {
            float w = proj[3];
            float sx = proj[0] / w;
            if ((-w < proj[0]) && (proj[0] < w)) {
                // viewport-scale words from DAT_80611748 (biased int->float)
                float *vp = (float *)&DAT_80611748;
                float baseX = (-vp[0]) * (proj[1] / w) + (-vp[2]);
                float baseY = -((-vp[9]) * (proj[2] / w) - (-vp[7]));
                float clipL = -vp[0];
                float clipR = -vp[5];
                float clipT = -vp[1];
                float clipB = -vp[6];
                float fuse = *(float *)(aiRec + 0x9e0);
                float frac;
                float clamped = (fuse < FLOAT_806185b4) ? fuse : FLOAT_806185b4;
                float ratio = clamped / FLOAT_806185b4;
                float m0[3];
                float m1[3];
                float ax, ay, bx, by;
                if (ratio <= FLOAT_80618590) {
                    frac = ratio;
                } else {
                    frac = FLOAT_80618590;
                }
                if (frac < FLOAT_80618594) {
                    frac = FLOAT_80618594;
                }
                m0[0] = FLOAT_8061865c;
                m0[1] = FLOAT_80618660;
                m0[2] = w;
                mtx4_transform_point_alias(*(float **)(DAT_80611d54 + 0x204), m0);
                ax = m0[0] / w;
                ay = m0[1] / w;
                m1[0] = FLOAT_80618664;
                m1[1] = FLOAT_80618668;
                m1[2] = w;
                mtx4_transform_point_alias(*(float **)(DAT_80611d54 + 0x204), m1);
                bx = m1[0] / w;
                by = m1[1] / w;
                {
                    float cx = sx + FLOAT_80618590;
                    float x0 = baseX - (ax + bx);
                    float y0 = baseY - (ay + by);
                    float x1 = x0 + ax;
                    float y1 = y0 + ay;
                    if ((clipL < x1) && (x0 < clipT) &&
                        (y1 > clipR) && (y0 < clipB) && (cx < FLOAT_80618590)) {
                        gx_set_sub_material_id(1);
                        gx_draw_fill_rect_f_ex((double)x0, (double)y0, (double)x1, (double)y1,
                                               (double)cx);
                        gx_draw_fill_rect_f_ex((double)x0, -(double)(frac * ax - y1),
                                               (double)x1, (double)y1, (double)cx);
                    }
                }
            }
        }
    }
}

// 0x80226e74 - ASL master per-frame tick: on the first frame teleport all
// active players to the staging zone 0x41e; advance the wave phase timer (+0x8)
// and, when it passes FLOAT_8061866c, advance the wave index (+0x10), refresh
// the per-wave spawn count (+0x18 table or +3), and push the wave HUD message
// 0x681. When the spawn timer (+0x4) elapses and more entities are due, spawn
// the next entity and reset the timer.
void worm_asl_master_tick(double param_1, double param_2, double param_3, double param_4,
                          double param_5, double param_6, double param_7, unsigned long long param_8)
{
    int *obj = (int *)DAT_806128bc;
    double normDenom;
    double dt;
    FUN_803826e4();
    normDenom = (double)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_806185a0);
    dt = (double)(FLOAT_806108ec * (float)((double)FLOAT_80618590 / (double)(float)normDenom));
    if (*obj == 0) {
        int off = DAT_80612610 << 2;
        int i;
        for (i = DAT_80612610; i < DAT_80612098; i = i + 1) {
            int *rec = *(int **)((int)&DAT_80588610 + off);
            int ent = *rec;
            int zone;
            entity_register_for_update(ent, 10, (int)&worm_asl_hud_marker_render);
            if (*(short *)(rec + 0x12) == 0x5e) {
                rec[8] = 0;
            }
            *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) & 0xffffffef;
            zone = soundpad_find_zone_by_id(0x41e);
            player_ai_teleport_wrapper(rec, zone, 1);
            off = off + 4;
            *obj = 1;
        }
    }
    obj[2] = (int)(float)((double)(float)obj[2] + dt);
    if ((double)FLOAT_8061866c < (double)(float)obj[2]) {
        int strId;
        unsigned int *txt;
        obj[4] = obj[4] + 1;
        obj[2] = (int)FLOAT_80618594;
        obj[5] = 0;
        if (obj[4] < 5) {
            obj[6] = (&DAT_80460f68)[obj[4]];
        } else {
            obj[6] = obj[6] + 3;
        }
        strId = get_string_by_id(0x681);
        txt = (unsigned int *)debug_log_formatted(
            (double)(float)obj[2], (double)FLOAT_80618590, param_3, param_4, param_5, param_6,
            param_7, param_8, strId, obj[4] + 1, 0, 0, 0, 0, 0, 0);
        hud_text_submit(*(int *)(DAT_80611d74 + 4), 3, txt);
    }
    obj[1] = (int)(float)((double)(float)obj[1] - dt);
    if ((obj[5] < obj[6]) && ((float)obj[1] < FLOAT_80618594)) {
        worm_asl_spawn_next_entity();
        obj[1] = (int)FLOAT_806185ac;
    }
    FUN_80382730();
}

// 0x80227070 - ASL: decrement the objective active-entity count (+0xc).
void worm_asl_obj_dec_active(void)
{
    *(int *)(DAT_806128bc + 0xc) = *(int *)(DAT_806128bc + 0xc) - 1;
}

// 0x80227084 - ASL: return the objective active-entity count (+0xc).
int worm_asl_obj_get_active(void)
{
    return *(int *)(DAT_806128bc + 0xc);
}

// 0x80227090 - ASL entity detonate: kill the entity's particles, clear its
// velocity + render/collision flags, and (if it had a linked body param[2])
// record the detonation position/count into the object (+0x24/+0x2c..+0x38),
// bump the success count (+0x20) when in time, and nudge the owner bot stat.
// If the entity had a mover parent (+0x78), play the scaled collapse anim and
// unlink it, then play the detonation sound 0xd7b at the level anchor.
void worm_asl_entity_detonate(int *param_1)
{
    int entity = *param_1;
    int playParams[12];
    float zero;
    particle_kill_by_entity();
    zero = FLOAT_80618594;
    *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) & 0xfbffff9e;
    *(float *)(entity + 0xb4) = zero;
    *(float *)(entity + 0xb8) = zero;
    *(float *)(entity + 0xbc) = zero;
    if (param_1[2] != 0) {
        *(int *)(DAT_806128bc + 0x24) = *(int *)(DAT_806128bc + 0x24) + 1;
        *(int *)(DAT_806128bc + 0x2c) = *(int *)(entity + 100);
        *(int *)(DAT_806128bc + 0x30) = *(int *)(entity + 0x68);
        *(int *)(DAT_806128bc + 0x34) = *(int *)(entity + 0x6c);
        if (zero < *(float *)(DAT_806128bc + 0x38)) {
            *(int *)(DAT_806128bc + 0x20) = *(int *)(DAT_806128bc + 0x20) + 1;
        }
        *(float *)(DAT_806128bc + 0x38) = FLOAT_806185d4;
        if (*(int **)(param_1[2] + 0x150) != (int *)0) {
            FUN_80237b50((double)FLOAT_80618590, 0x44, **(int **)(param_1[2] + 0x150));
        }
    }
    if (*(int *)(entity + 0x78) != 0) {
        unsigned int r = rng_counter_tick();
        double normR = (double)((double)(unsigned int)r - DOUBLE_806185f8);
        __worm_anim_play_scaled((double)FLOAT_8061860c,
                                (double)(FLOAT_80618670 * ((float)normR / FLOAT_806185ec)
                                         + FLOAT_806185b4));
        *(int *)(**(int **)(entity + 0x78) + 0x20c) = 0;
        *(int *)(entity + 0x78) = 0;
        *(int *)(entity + 0x128) = 0;
        *(int *)(entity + 300) = -1;
    }
    *(int *)(*(int *)(entity + 4) + 0x30) = 2;
    {
        int anchor = *(int *)(DAT_80611d74 + 0x14);
        sound_init_play_params((int)playParams);
        *(unsigned char *)((int)playParams + 0x20) = 1;   // local_20 flag byte
        playParams[0] = 0xd7b;                             // sound id
        playParams[7] = *(int *)(anchor + 100);            // local_2c pos.x (@+0x1c)
        playParams[8] = *(int *)(anchor + 0x68);           // local_28 pos.y (@+0x20)
        playParams[9] = *(int *)(anchor + 0x6c);           // local_24 pos.z (@+0x24)
        sound_play_request((int *)playParams);
    }
}

// 0x80227234 - ASL mover attach: like worm_bttl_mover_attach_victim but the
// 10-slot mover table lives inside the objective object at DAT_806128bc+0x68.
// Attach victim to the first free slot, seed pos from linked parent + part
// offsets, unlink parent, compute descent velocity from obj+0x274 transform.
void worm_asl_mover_attach_victim(int obj, int victim)
{
    int off = 0;
    int slots = 10;
    do {
        if (*(int *)(DAT_806128bc + off + 0x68) == 0) {
            float startY;
            float local_14;
            float startZ;
            *(int *)(DAT_806128bc + off + 0x68) = victim;
            startY = FLOAT_80618594;
            startZ = FLOAT_80618590;
            *(float *)(victim + 100) =
                *(float *)(**(int **)(victim + 0x78) + 100) + *(float *)(victim + 0x248);
            *(float *)(victim + 0x68) =
                *(float *)(**(int **)(victim + 0x78) + 0x68) + *(float *)(victim + 0x24c);
            *(float *)(victim + 0x6c) =
                *(float *)(**(int **)(victim + 0x78) + 0x6c) + *(float *)(victim + 0x250);
            *(int *)(victim + 0x78) = 0;
            *(int *)(victim + 0x128) = 0;
            *(int *)(victim + 300) = -1;
            *(float *)(victim + 0xc0) = startY;
            *(float *)(victim + 0x70) = startY;
            *(float *)(victim + 200) = startY;
            *(unsigned char *)(victim + 0x32) = 2;
            *(unsigned int *)(victim + 0x28) = *(unsigned int *)(victim + 0x28) & 0xbfffffff;
            *(int *)(victim + 0x1a0) = 1;
            *(int *)(obj + 0x20c) = 0;
            local_14 = startY;
            mtx4_transform_point((float *)(obj + 0x274), &startY);
            {
                float scale = FLOAT_806185d4;
                *(float *)(victim + 0xb4) = FLOAT_806185d4 * startY;
                *(float *)(victim + 0xb8) = scale * local_14;
                *(float *)(victim + 0xbc) = scale * startZ;
            }
            return;
        }
        off = off + 4;
        slots = slots - 1;
    } while (slots != 0);
}

// ============================================================================
// batch_685: 0x80227364-0x80227e54 - the remainder of the ASL objective object
// DAT_806128bc (render-tick / spawn-pair / master-tick2 / reset) plus the start
// of a NEW 0x140-byte 'UPL' (uplink/carry-turret) objective object DAT_806128c0
// (init/preload/target-pos/turret-input/engine-sound). Continues the
// mission-hook cluster; APPENDED to worm_mission_hooks.cpp.
// ============================================================================

// --- Additional file-local externs for batch_685 -------------------------
extern int   DAT_806128c0;    // pointer to the 0x140-byte UPL objective object
extern int   DAT_806128d4;    // ASL wave/spawn counter
extern int   DAT_806128f4;    // ASL cumulative team score accumulator
extern int   DAT_80611b8c;    // soundpad zone-record table base (0x20 stride)
extern float FLOAT_80611d80;  // per-frame input scale (60fps normalizer)
extern float FLOAT_80618674;  // ASL spawn-interval decay
extern float FLOAT_80618678;  // ASL spawn-interval base
extern float FLOAT_8061867c;  // UPL aim limit x
extern float FLOAT_80618680;  // UPL aim limit y
extern float FLOAT_80618684;  // UPL turret pitch clamp
extern float FLOAT_80618688;  // UPL turret yaw clamp half-range
extern int   subobj_is_idle(unsigned int subId);
extern int   soundpad_resolve_entity_to_zone(int entity);
extern void  sound_set_listener_override(int flag);
extern int   FUN_8022b45c(void);   // ASL alive/pending entity count
extern double input_get_analog_value(int pad, int axis);
extern double player_get_camera_lerp_factor(int cam);
extern double angle_wrap_clamp(double angle, double lo, double hi);
// sound_update_3d_source is declared void elsewhere in the file, but
// worm_upl_engine_sound_tick uses its return (handle). Alias to the ROM entry
// with a value-returning signature (leading __ so gen_externs skips it).
static int (*const __worm_sound_update_3d)(double, double, double, int, int, int, unsigned int) =
    (int (*)(double, double, double, int, int, int, unsigned int))0x800102e4;

void worm_asl_spawn_pair(void);   // forward decl (used by render_tick's pairing)

// 0x80227364 - ASL entity render callback: if the entity has an attached mover
// with a linked parent, run the obj-local mover attach (which unlinks it),
// bump the destroyed count (+0x1c), and snapshot the level anchor position into
// the object (+0x2c..+0x34). Always plays the rng-jittered wobble anim.
void worm_asl_render_tick(void)
{
    int *ctx = __worm_ctx_current();
    int obj = *ctx;
    int victim = *(int *)(obj + 0x20c);
    int aiRec = *(int *)(obj + 0x7c);
    if ((victim != 0) && (*(int *)(victim + 0x78) != 0)) {
        worm_asl_mover_attach_victim(obj, victim);
        *(int *)(DAT_806128bc + 0x1c) = *(int *)(DAT_806128bc + 0x1c) + 1;
        *(int *)(DAT_806128bc + 0x2c) = *(int *)(aiRec + 0xbb4);
        *(int *)(DAT_806128bc + 0x30) = *(int *)(aiRec + 3000);
        *(int *)(DAT_806128bc + 0x34) = *(int *)(aiRec + 0xbbc);
    }
    {
        unsigned int r = rng_counter_tick();
        double normR = (double)((double)(unsigned int)r - DOUBLE_806185f8);
        __worm_anim_play_scaled((double)FLOAT_8061860c,
                                (double)(FLOAT_80618670 * ((float)normR / FLOAT_806185ec)
                                         + FLOAT_806185b4));
    }
    damage_filter_invincible(ctx);
    FUN_8038273c();
}

// 0x80227438 - ASL spawn pair: locate the BTL* battery entity (type 0x5d,
// child slot[8]==0) and the first free platform entity in the object's +0x40
// slot table, then wire their render callbacks (render_tick / detonate), set
// shared render+collision flags 0x4000061, link the platform's parent to the
// battery, seed its launch offsets (+0x134..) from stored anchor (+0x1c0/+0x1c4),
// and latch the battery AI behaviour (+0x274) to 4.
void worm_asl_spawn_pair(void)
{
    int off;
    int slots;
    int battery = 0;
    int other = 0;
    int *rec = (int *)0;
    FUN_803826f0();
    off = DAT_80612610 << 2;
    slots = DAT_80612098 - DAT_80612610;
    if (DAT_80612610 < DAT_80612098) {
        do {
            rec = *(int **)((int)&DAT_80588610 + off);
            if ((*(short *)(rec + 0x12) == 0x5d) && (rec[8] == 0)) {
                battery = *rec;
                break;
            }
            off = off + 4;
            slots = slots - 1;
        } while (slots != 0);
    }
    off = 0;
    slots = 10;
    for (;;) {
        int slot = *(int *)(DAT_806128bc + off + 0x40);
        if ((slot != 0) && ((*(unsigned int *)(slot + 0x28) & 1) == 0)) {
            other = slot;
            break;
        }
        off = off + 4;
        slots = slots - 1;
        if (slots == 0) break;
    }
    if ((battery != 0) && (other != 0)) {
        float launchZ = FLOAT_80618670;
        float launchXY = FLOAT_806185a8;
        entity_set_render_type(battery, (int)&worm_asl_render_tick);
        entity_set_render_type(other, (int)&worm_asl_entity_detonate);
        *(unsigned int *)(other + 0x28) = *(unsigned int *)(other + 0x28) | 0x4000061;
        *(unsigned int *)(battery + 0x28) = *(unsigned int *)(battery + 0x28) | 0x4000061;
        *(unsigned int *)(battery + 0x10) = *(unsigned int *)(battery + 0x10) | 0xffff;
        *(unsigned int *)(other + 0x10) = *(unsigned int *)(other + 0x10) | 0xffff;
        *(int *)(other + 0x78) = *(int *)(battery + 4);
        *(int *)(other + 300) = *(int *)(*(int *)(*(int *)(battery + 4) + 0x84) + 0x30);
        *(int *)(battery + 0x20c) = other;
        *(float *)(other + 0x134) = *(float *)(other + 0x1c0) - launchXY;
        *(float *)(other + 0x138) = *(float *)(other + 0x1c4) - launchZ;
        *(float *)(other + 0x13c) = launchZ;
        *(int *)(*(int *)(other + 4) + 0x30) = 1;
        if (rec[8] != 4) {
            rec[0x274] = 4;
        }
    }
    FUN_8038273c();
}

// 0x802275cc - ASL master tick (v2): drive the 10-slot mover table at
// DAT_806128bc+0x68 (floor-snap + descent integrate, detonate on reach),
// tally scoring (destroyed*-0x4b + captured*0x32 + bonus*100, capped 500, x2)
// -> hud_worldmsg_push + team score DAT_806128f4, then spawn gating: while
// alive-count < 6 attach pending movers, else advance the wave spawn timer and
// fire worm_asl_spawn_pair, decaying the interval (+0x3c) by FLOAT_80618674.
void worm_asl_master_tick2(void)
{
    int i = 0;
    int off = 0;
    double normDenom;
    double dt;
    double damp;
    int score;
    int capped;
    int aliveCheck;
    FUN_803826e8();
    normDenom = (double)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_806185a0);
    dt = (double)(FLOAT_806108ec * (FLOAT_80618590 / (float)normDenom));
    damp = (double)(float)((double)FLOAT_80618600 * dt);
    do {
        int mover = *(int *)(DAT_806128bc + off + 0x68);
        if (mover != 0) {
            int node = *(int *)(mover + 0x34);
            int texData = scene_node_get_texture_data(node, 1);
            int matCount = scene_node_get_material_count(*(int *)(mover + 0x34), 1);
            float floorH = (float)floor_polygon_search(
                matCount, texData,
                *(unsigned int *)(*(int *)(*(int *)(DAT_80611b04 + node * 4) + 4) + 0x30),
                (float *)(mover + 100), (int *)0);
            *(float *)(mover + 0x218) = floorH;
            *(float *)(mover + 0xb8) = (float)((double)*(float *)(mover + 0xb8) - damp);
            *(float *)(mover + 100) =
                *(float *)(mover + 100) + (float)((double)*(float *)(mover + 0xb4) * dt);
            *(float *)(mover + 0x68) =
                *(float *)(mover + 0x68) + (float)((double)*(float *)(mover + 0xb8) * dt);
            *(float *)(mover + 0x6c) =
                *(float *)(mover + 0x6c) + (float)((double)*(float *)(mover + 0xbc) * dt);
            if (*(float *)(mover + 0x68) - *(float *)(mover + 0x1c4) <= *(float *)(mover + 0x218)) {
                int block[2];
                float dmg;
                switch_slot_reset((int)block);
                dmg = FLOAT_80618590;
                (void)dmg;
                block[0] = mover;
                worm_asl_entity_detonate(block);
                *(int *)(DAT_806128bc + off + 0x68) = 0;
            }
        }
        i = i + 1;
        off = off + 4;
    } while (i < 10);

    *(int *)(DAT_806128bc + 0x28) =
        *(int *)(DAT_806128bc + 0x28) + *(int *)(DAT_806128bc + 0x24);
    score = *(int *)(DAT_806128bc + 0x24) * 0x32;
    if ((*(int *)(DAT_806128bc + 0x20) != 0) && (*(int *)(DAT_806128bc + 0x24) != 0)) {
        score = score + *(int *)(DAT_806128bc + 0x20) * 100;
    }
    score = score + *(int *)(DAT_806128bc + 0x1c) * -0x4b;
    capped = (score < 500) ? score : 500;
    capped = capped * 2;
    *(int *)(DAT_806128bc + 0x24) = 0;
    *(int *)(DAT_806128bc + 0x1c) = 0;
    if (capped != 0) {
        hud_worldmsg_push((int *)(DAT_806128bc + 0x2c), (short)capped);
        DAT_806128f4 = DAT_806128f4 + capped;
    }
    if ((FLOAT_80618594 < *(float *)(DAT_806128bc + 0x38)) &&
        (*(float *)(DAT_806128bc + 0x38) =
             (float)((double)*(float *)(DAT_806128bc + 0x38) - dt),
         *(float *)(DAT_806128bc + 0x38) <= FLOAT_80618594)) {
        *(float *)(DAT_806128bc + 0x38) = FLOAT_80618594;
        *(int *)(DAT_806128bc + 0x20) = 0;
    }

    aliveCheck = FUN_8022b45c();
    if (aliveCheck < 6) {
        int half = ((int)DAT_806109c0 >> 1) +
                   (unsigned int)((int)DAT_806109c0 < 0 && (DAT_806109c0 & 1) != 0);
        if (FUN_8022b45c() < half) {
            int j = 0;
            int o = 0;
            do {
                int slot = *(int *)(DAT_806128bc + o + 0x40);
                if (((slot != 0) && ((*(unsigned int *)(slot + 0x28) & 1) != 0)) &&
                    (*(int *)(slot + 0x20c) != 0)) {
                    worm_asl_mover_attach_victim(*(int *)(slot + 0x20c), slot);
                }
                j = j + 1;
                o = o + 4;
            } while (j < 10);
        }
    } else {
        *(float *)(DAT_806128bc + 4) =
            (float)((double)*(float *)(DAT_806128bc + 4) - dt);
        if (*(float *)(DAT_806128bc + 4) <= FLOAT_80618594) {
            worm_asl_spawn_pair();
            DAT_806128d4 = DAT_806128d4 + 1;
            *(int *)(DAT_806128bc + 4) = *(int *)(DAT_806128bc + 0x3c);
            if (FLOAT_806185d4 < *(float *)(DAT_806128bc + 0x3c)) {
                *(float *)(DAT_806128bc + 0x3c) =
                    -(float)((double)FLOAT_80618674 * dt -
                             (double)*(float *)(DAT_806128bc + 0x3c));
            }
        }
    }
    FUN_80382734();
}

// 0x80227904 - ASL objective reset: zero the object + its +0x68 mover table,
// seed the spawn-interval (+0x3c=FLOAT_80618678), spawn 10 platform entities
// (type 0x115) into the +0x40 slot table (each active flag 0x10, state 1), then
// clear the render/collision flags on all active player entities (reset any BTL
// battery behaviour) and clear the wave counter DAT_806128d4.
void worm_asl_obj_reset(double param_1, double param_2, double param_3, double param_4,
                        double param_5, double param_6, double param_7, unsigned long long param_8)
{
    int desc[3];
    int i;
    int off;
    memset_wrapper(DAT_806128bc, 0, 0x1e0);
    memset_wrapper(DAT_806128bc + 0x68, 0, 0x28);
    *(float *)(DAT_806128bc + 0x3c) = FLOAT_80618678;
    *(float *)&desc[0] = FLOAT_80618594;   // local_1c
    *(float *)&desc[1] = FLOAT_80618594;   // local_18
    *(float *)&desc[2] = FLOAT_80618594;   // local_14
    // local_28..local_20 orientation block
    {
        int ori[3];
        int slot = 0;
        *(float *)&ori[0] = FLOAT_80618594;
        *(float *)&ori[1] = FLOAT_80618594;
        *(float *)&ori[2] = FLOAT_80618594;
        for (i = 0; i < 10; i = i + 1) {
            int ent = (int)entity_spawn_by_type((double)FLOAT_80618678, param_2, param_3, param_4,
                                                param_5, param_6, param_7, param_8, 0x115,
                                                desc, ori, 0, 0);
            if (ent != 0) {
                *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x10;
                *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) & 0xfbffff9e;
                *(int *)(DAT_806128bc + slot + 0x40) = ent;
                *(int *)(*(int *)(ent + 4) + 0x30) = 1;
            }
            slot = slot + 4;
        }
    }
    off = DAT_80612610 << 2;
    for (i = DAT_80612610; i < DAT_80612098; i = i + 1) {
        int *rec = *(int **)((int)&DAT_80588610 + off);
        *(unsigned int *)(*rec + 0x28) = *(unsigned int *)(*rec + 0x28) & 0xffffff9e;
        if ((*(short *)(rec + 0x12) == 0x5d) && (rec[8] != 0)) {
            rec[0x274] = 0;
        }
        off = off + 4;
    }
    DAT_806128d4 = 0;
}

// ============================================================================
// UPL (uplink/carry-turret) objective object DAT_806128c0 (0x140 bytes).
// ============================================================================

// 0x80227ac4 - UPL objective preload/alloc: allocate the 0x140-byte object
// DAT_806128c0 and preload entity resources 0x111-0x114 + clear 5 sound banks.
void worm_upl_obj_preload(void)
{
    DAT_806128c0 = mem_alloc(0x140, 1);
    entity_preload_resources(0x111);
    entity_preload_resources(0x112);
    entity_preload_resources(0x113);
    entity_preload_resources(0x114);
    sound_clear_bank_ref(0xd7d);
    sound_clear_bank_ref(0xd7c);
    sound_clear_bank_ref(0xd7e);
    sound_clear_bank_ref(0xd7f);
    sound_clear_bank_ref(0x1623);
}

// 0x80227a54 - UPL objective init: zero the object, set mode word (+0x74)=4 and
// active flag (+8)=1, disable the sound listener override, and seed the turret
// aim limits (+0x18/+0x1c/+0x20).
void worm_upl_obj_init(void)
{
    memset_wrapper(DAT_806128c0, 0, 0x134);
    *(int *)(DAT_806128c0 + 0x74) = 4;
    *(int *)(DAT_806128c0 + 8) = 1;
    sound_set_listener_override(0);
    *(float *)(DAT_806128c0 + 0x18) = FLOAT_8061867c;
    *(float *)(DAT_806128c0 + 0x1c) = FLOAT_806185d0;
    *(float *)(DAT_806128c0 + 0x20) = FLOAT_80618680;
}

// 0x80227b38 - UPL set target pos: copy the given entity's position into the
// object's target slot (+0x7c..+0x84), lifting y by FLOAT_80618590 and z by
// FLOAT_8061862c.
void worm_upl_set_target_pos(int entity)
{
    float dy = FLOAT_80618590;
    float dz = FLOAT_8061862c;
    *(int *)(DAT_806128c0 + 0x7c) = *(int *)(entity + 100);
    *(int *)(DAT_806128c0 + 0x80) = *(int *)(entity + 0x68);
    *(int *)(DAT_806128c0 + 0x84) = *(int *)(entity + 0x6c);
    *(float *)(DAT_806128c0 + 0x84) = *(float *)(DAT_806128c0 + 0x84) + dz;
    *(float *)(DAT_806128c0 + 0x80) = *(float *)(DAT_806128c0 + 0x80) + dy;
}

// 0x80227e54 - UPL get target pos: read the object's stored target position
// (+0x0/+0x4/+0x8) into the out vector, applying the biased int->float convert.
void worm_upl_get_target_pos(int *outX, float *outY, float *outZ)
{
    int *obj = (int *)DAT_806128c0;
    double bias = DOUBLE_806185a0;
    *outX = obj[2];
    *outY = (float)((double)(unsigned int)(obj[1] ^ 0x80000000) - bias);
    *outZ = (float)((double)(unsigned int)(obj[0] ^ 0x80000000) - bias);
}

// 0x80227b88 - UPL turret input tick: read the local player's aim analog axes
// (input_get_analog_value 3/4), scale by the framerate normalizer, integrate
// into the turret pitch (level +0x40) and yaw (level +0x41) with per-frame
// camera-lerp scaling, then clamp pitch to +/-FLOAT_80618688 around the zone's
// stored angle (+0x18) and yaw to [FLOAT_80618684, 0].
void worm_upl_turret_input_tick(void)
{
    int *lvl = (int *)&DAT_80611d74;   // DAT_80611d74 indexed as int[]
    int cam = DAT_80611d54;
    int zone = soundpad_resolve_entity_to_zone(
        *(int *)(DAT_80611b8c + *(int *)(*(int *)(lvl[5] + 0x7c) + 0x678) * 0x20));
    double ax = input_get_analog_value(lvl[0], 3);
    double ay = input_get_analog_value(lvl[0], 4);
    double lerp = player_get_camera_lerp_factor(cam);
    double normDenom = (double)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_806185a0);
    float inputScale = FLOAT_80611d80 * (FLOAT_80618590 / (float)normDenom);
    double clamped;
    // yaw axis (level word 0x41)
    lvl[0x41] = (int)-(FLOAT_806108ec *
                       (float)(ay * (double)inputScale) *
                       (float)((double)*(float *)(cam + 900) / lerp) -
                       (float)lvl[0x41]);
    clamped = angle_wrap_clamp((double)(float)lvl[0x41], (double)FLOAT_80618684,
                               (double)FLOAT_80618594);
    lvl[0x41] = (int)(float)clamped;
    lerp = player_get_camera_lerp_factor(cam);
    // pitch axis (level word 0x40) clamped around the zone aim angle (+0x18)
    lvl[0x40] = (int)-(FLOAT_806108ec *
                       (float)(ax * (double)inputScale) *
                       (float)((double)*(float *)(cam + 900) / lerp) -
                       (float)lvl[0x40]);
    clamped = angle_wrap_clamp((double)(float)lvl[0x40],
                               (double)(*(float *)(zone + 0x18) - FLOAT_80618688),
                               (double)(FLOAT_80618688 + *(float *)(zone + 0x18)));
    lvl[0x40] = (int)(float)clamped;
}

// 0x80227d18 - UPL engine sound tick: compute the carried subobject's speed
// (from velocity +0xf8/+0xfc/+0x100), ramp the engine level (+0xe0) toward the
// speed fraction, and drive the 3D engine loop sound 0xd7c with pitch/volume
// derived from the level; sets the "playing" flag bit 0x40 on +0x18.
void worm_upl_engine_sound_tick(int obj)
{
    double speed = math_sqrt((double)(*(float *)(obj + 0x100) * *(float *)(obj + 0x100) +
                                      *(float *)(obj + 0xf8) * *(float *)(obj + 0xf8) +
                                      *(float *)(obj + 0xfc) * *(float *)(obj + 0xfc)));
    double normSpeed = (double)(float)((double)(float)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000)
                                                       - DOUBLE_806185a0) * speed);
    unsigned int idle = subobj_is_idle(*(unsigned int *)(obj + 0x80));
    unsigned int bank = (idle != 0) ? 1 : 5;
    double frac = (double)(float)(normSpeed / (double)FLOAT_80618678);
    double clampHi = frac;
    int handle;
    if ((double)FLOAT_80618590 < frac) {
        clampHi = (double)FLOAT_80618590;
    }
    if ((double)FLOAT_80618594 <= clampHi) {
        if ((double)FLOAT_80618590 < frac) {
            frac = (double)FLOAT_80618590;
        }
    } else {
        frac = (double)FLOAT_80618594;
    }
    scalar_step_toward_snap(frac, (double)FLOAT_80618624, (float *)(obj + 0xe0));
    handle = __worm_sound_update_3d(
        (double)(FLOAT_806185b4 * *(float *)(obj + 0xe0) - FLOAT_806185a8),
        (double)FLOAT_806185dc,
        (double)(FLOAT_806185e4 * *(float *)(obj + 0xe0) + FLOAT_806185d8),
        obj + 0x22c, obj + 100, 0xd7c, bank);
    if (handle != 0) {
        *(unsigned int *)(obj + 0x18) = *(unsigned int *)(obj + 0x18) | 0x40;
    }
}

// ===========================================================================
// batch_686: UPL (uplink / carry-turret) objective object DAT_806128c0 body.
// The 0x140-byte UPL object was allocated/initialised by worm_upl_obj_init /
// worm_upl_obj_preload (batch_685). These functions form the full runtime of
// that objective: entity spawn set (a 0x113 "core" + three 0x111 turret rigs +
// six 0x112 platform pads), the per-frame master tick that drives the analog
// turret, capture/scoring, and phase progression, plus a standalone scripted-
// prop random spawner (0x80229450) that seeds decorative/ambient props.
//
// Address: 0x80227eb4 | Name: worm_upl_obj_spawn_entities   | 2026-09-09
// Address: 0x802282b0 | Name: worm_upl_award_and_hud        | 2026-09-09
// Address: 0x802283a8 | Name: worm_upl_obj_tick             | 2026-09-09
// Address: 0x80228d28 | Name: worm_upl_distance_to_points   | 2026-09-09
// Address: 0x80228e34 | Name: worm_upl_recompute_score      | 2026-09-09
// Address: 0x80228f10 | Name: worm_upl_get_score            | 2026-09-09
// Address: 0x80228f1c | Name: worm_upl_score_strip          | 2026-09-09
// Address: 0x80228fac | Name: worm_upl_advance_phase        | 2026-09-09
// Address: 0x80229434 | Name: worm_upl_get_remaining        | 2026-09-09
// Address: 0x80229450 | Name: worm_prop_random_spawn_tick   | 2026-09-09
// ---------------------------------------------------------------------------
// File-local externs new to batch_686 (call-site typed).
extern int   DAT_806128cc;      // one-shot "special prop already spawned" latch
extern int   DAT_80612a9c;      // spawned-pad running counter (reset each spawn)
extern int   DAT_8060f948;      // active game-mode word (==4 selects alt string)
extern uint  DAT_805015c0;      // session flag word (bit 0x40000000 = frozen)
extern int   DAT_8057ff4c;      // fixed-point X base for pip position
extern int   DAT_8057ff58;      // fixed-point Z base for pip position
extern int   DAT_80461830;      // int[] material-id table (indexed by rng band)
extern int   DAT_80610ed4;      // int[] scripted-prop type-id table (by rng band)
extern int   DAT_80610ec4;      // HUD glyph string: neutral/empty
extern int   DAT_80610ec8;      // HUD glyph string: big-gain suffix
extern int   DAT_80610ecc;      // HUD glyph string: alt-mode prefix
extern int   DAT_80610ed0;      // HUD glyph string: up-arrow (delta >= 0)
// Per-phase spawn descriptor tables (float triples + per-phase count/stride).
extern int   DAT_804613b8;      // per-phase pad count (0x13-int stride)
extern float DAT_804613bc;      // per-phase pad offset X (0xc-byte stride)
extern float DAT_804613c0;      // per-phase pad offset Y
extern float DAT_804613c4;      // per-phase pad offset Z
extern int   DAT_80583228;      // sine LUT base (0x800-entry, & 0x7ff)
// UPL fixed source-position / descriptor float pool (0x803e00d4..0x803e0190).
extern int   DAT_803e00d4; extern int DAT_803e00d8; extern int DAT_803e00dc;
extern int   DAT_803e00e0; extern int DAT_803e00e4; extern int DAT_803e00e8;
extern int   DAT_803e00ec; extern int DAT_803e00f0; extern int DAT_803e00f4;
extern int   DAT_803e00f8; extern float DAT_803e00fc; extern int DAT_803e0100;
extern float DAT_803e0104; extern float DAT_803e0108; extern float DAT_803e010c;
extern float DAT_803e0110; extern float DAT_803e0114; extern float DAT_803e0118;
extern int   DAT_803e011c; extern int DAT_803e0120; extern int DAT_803e0124;
extern int   DAT_803e0128; extern int DAT_803e012c; extern int DAT_803e0130;
extern int   DAT_803e0134; extern int DAT_803e0138; extern int DAT_803e013c;
extern int   DAT_803e0140; extern int DAT_803e0144; extern int DAT_803e0148;
extern int   DAT_803e014c; extern int DAT_803e0150; extern int DAT_803e0154;
extern int   DAT_803e0158; extern int DAT_803e015c; extern int DAT_803e0160;
extern int   DAT_803e0164; extern int DAT_803e0168; extern int DAT_803e016c;
extern int   DAT_803e0170; extern int DAT_803e0174; extern int DAT_803e0178;
extern int   DAT_803e017c; extern int DAT_803e0180; extern int DAT_803e0184;
extern float DAT_803e0188; extern float DAT_803e018c; extern float DAT_803e0190;
extern int   DAT_803e0194; extern int DAT_803e0198; extern int DAT_803e019c;
extern int   DAT_803e01a0; extern int DAT_803e01a4; extern int DAT_803e01a8;
// Float/double tuning constants new to this batch.
extern float FLOAT_8061082c; extern float FLOAT_80610830;
extern float FLOAT_8061868c; extern float FLOAT_80618690;
extern float FLOAT_80618694; extern float FLOAT_80618698; extern float FLOAT_8061869c;
extern float FLOAT_806186a0; extern float FLOAT_806186a4; extern float FLOAT_806186a8;
extern float FLOAT_806186ac; extern float FLOAT_806186b0; extern float FLOAT_806186b4;
extern float FLOAT_806186b8; extern float FLOAT_806186bc; extern float FLOAT_806186c0;
extern float FLOAT_806186c4;
// Cross-module helpers (call-site typed, file-local for this TU).
extern int    player_set_state(int who, int stateBits);
extern void   spectator_set_pip_position(double x, double y);
extern int    spectator_get_slot(void);
extern void   spectator_instant_show(void);
extern void   spectator_set_mode_if_ready(double d, int *modeDesc);
extern void   camera_set_mode(int cam, int spawn, int target);
extern void   camera_set_mode_by_index(int cam, int index, int spawnIdx);
extern void   camera_set_mode_from_spawn(int *modeDesc, int target);
extern uint   subobj_pool_alloc(void);
extern void   subobj_build_descriptor_default(double d1, double d2, double d3, double d4,
                                              unsigned long long p5, unsigned long long p6,
                                              unsigned long long p7, unsigned long long p8,
                                              unsigned int subId, int entity,
                                              int a, int b, float f);
extern int    subobj_set_contact_origin(unsigned int subId, void *pos);
extern void   subobj_add_angular_force(unsigned int subId, void *vec);
extern void   subobj_add_linear_force(unsigned int subId, void *vec);
extern void   quat_from_axis_angle(double angle, float *outQuat, float *axis);
extern void   quat_rotate_vector(float *quat, float *inVec, float *outVec);
extern uint   input_get_buttons_held(int pad);
extern void   damage_play_pain_sound(int typeId, int flag, int *pos);
extern int   *entity_get_world_position(int entity);
extern int    spawn_point_find_by_entity(int fourcc);
extern void   mtx4_transform_vector(float *mtx, float *vec);
extern void   player_init_spawn_position(int slot, int *pos, int desc);
extern int    scene_query_nodes_in_frustum(int a, int bounds, int outNode, int max);
extern int    floor_height_from_position(double x, double z);
extern int    event_has_handler(int handlerBlock, int eventId);
extern void   anim_blend_eval_direct(void);
extern int    FUN_8022b45c(void);                     // ASL/UPL alive-entity count
extern void   worm_scripted_prop_on_spawn(double d1, double d2, double d3,
                                          unsigned long long p4, unsigned long long p5,
                                          unsigned long long p6, unsigned long long p7,
                                          unsigned long long p8);
// FP-spill ABI stubs and a math rounding helper used here.
extern void   FUN_803826e4(void);
extern void   FUN_80382730(void);
extern void   FUN_803826f0(void);
extern void   FUN_8038273c(void);
extern double FUN_80390324(double x);                 // round toward zero
extern int    debug_log_formatted(double d1, double d2, double d3, double d4,
                                  unsigned long long p5, unsigned long long p6,
                                  unsigned long long p7, unsigned long long p8, ...);
extern int    s__s_s_d_s_80461534;                    // "%s%s %d %s" fmt string
// Forward declarations (defined later in this TU; referenced earlier).
int  worm_upl_obj_spawn_entities(void);
int  worm_upl_recompute_score(void);
int  worm_upl_get_score(void);
void worm_upl_score_strip(void);
void worm_upl_advance_phase(unsigned long long param_1, double param_2, double param_3,
                            double param_4, unsigned long long param_5,
                            unsigned long long param_6, unsigned long long param_7,
                            unsigned long long param_8);
void worm_upl_award_and_hud(unsigned long long param_2, unsigned long long param_3,
                            unsigned long long param_4, unsigned long long param_5,
                            unsigned long long param_6, unsigned long long param_7,
                            unsigned long long param_8);

// 0x80227eb4 - UPL objective spawn: place the objective "core" entity 0x113 and
// its three 0x111 turret rigs into obj +0x34.., then spawn the six 0x112 pad
// entities into obj +0x44.. from the per-phase offset table DAT_804613bc. Each
// rig/pad gets a physics subobject (subobj_pool_alloc + default descriptor) and
// the shared render+collision flag mask (bits 0x61 for the primary, 0x10 for
// pads). Also arms the object (+0x6c = 1) and enters spectator PiP framing.
int worm_upl_obj_spawn_entities(void)
{
    float local_6c, local_68, local_64;
    int   local_60;
    float local_5c;
    int   local_58, local_54, local_50, local_4c;
    float local_48;
    int   local_44, local_40, local_3c, local_38, local_34;
    unsigned int uStack_2c, local_30_hi, local_28_hi, uStack_24;
    int obj = DAT_806128c0;
    int coreEnt, ret;
    int i, slot, off;
    double dCore;

    FUN_803826e4();
    // turret-rig descriptor block (0x113 core uses local_48/local_54..)
    local_3c = DAT_803e00d4; local_38 = DAT_803e00d8; local_34 = DAT_803e00dc;
    local_48 = *(float *)&DAT_803e00e0; local_44 = DAT_803e00e4; local_40 = DAT_803e00e8;
    local_54 = DAT_803e00ec; local_50 = DAT_803e00f0; local_4c = DAT_803e00f4;
    local_60 = DAT_803e00f8; local_5c = DAT_803e00fc; local_58 = DAT_803e0100;

    *(int *)(obj + 0x6c) = 1;                          // arm objective
    player_set_state(DAT_80611d74, 0x20000);

    // Build the spectator picture-in-picture anchor from fixed-point bases.
    uStack_2c = (unsigned int)(DAT_8057ff4c + *(int *)DAT_80611748) ^ 0x80000000;
    local_30_hi = 0x43300000;
    uStack_24 = (unsigned int)(((int *)DAT_80611748)[6] - DAT_8057ff58) ^ 0x80000000;
    double d688 = (double)FLOAT_80618688;
    local_28_hi = 0x43300000;
    double d68c = (double)FLOAT_8061868c;
    double pipY = -(double)(float)(d68c * (double)FLOAT_80610830 -
                     (double)(float)((double)CONCAT44(0x43300000, uStack_24) - DOUBLE_806185a0));
    spectator_set_pip_position(
        (double)(float)(d688 * (double)FLOAT_8061082c +
                        (double)(float)((double)CONCAT44(0x43300000, uStack_2c) - DOUBLE_806185a0)),
        pipY);

    // Advance the animated banner slot and spawn the objective "core" (0x113).
    double bannerPrev = (double)local_5c;
    local_5c = (float)(bannerPrev + (double)FLOAT_806185d0);
    coreEnt = entity_spawn_by_type(bannerPrev, pipY, d68c, DOUBLE_806185a0, d688, 0, 0, 0,
                                   0x113, &local_60, (int *)&local_48, 0, 0);
    *(int *)(obj + 0x5c) = coreEnt;
    *(unsigned int *)(*(int *)(obj + 0x5c) + 0x28) =
        *(unsigned int *)(*(int *)(obj + 0x5c) + 0x28) & 0xfffffffe;

    // Spawn the three 0x111 turret rigs into obj +0x34..+0x40.
    slot = 0;
    for (i = 0; i < 3; i++) {
        ret = entity_spawn_by_type(bannerPrev, pipY, d68c, DOUBLE_806185a0, d688, 0, 0, 0,
                                   0x111, &local_3c, (int *)&local_48, 0, 0);
        *(int *)(obj + slot + 0x34) = ret;
        off = slot + 0x34;
        *(unsigned int *)(*(int *)(obj + off) + 0x80) = subobj_pool_alloc();
        *(unsigned int *)(*(int *)(obj + off) + 0x28) =
            *(unsigned int *)(*(int *)(obj + off) + 0x28) & 0xffffff9e;
        subobj_build_descriptor_default(bannerPrev, pipY, d68c, DOUBLE_806185a0, d688, 0, 0, 0,
                                        *(unsigned int *)(*(int *)(obj + off) + 0x80),
                                        *(int *)(obj + off), 0x18, -1, 1.1755302e-38f);
        entity_register_for_update(*(int *)(obj + slot + 0x34), 3, -0x7fdd82e8);
        slot += 4;
    }
    // Primary rig gets the full render+collision mask and contact origin.
    *(unsigned int *)(*(int *)(obj + 0x34) + 0x28) =
        *(unsigned int *)(*(int *)(obj + 0x34) + 0x28) | 0x61;
    *(int *)(*(int *)(obj + 0x34) + 100)  = local_54;
    *(int *)(*(int *)(obj + 0x34) + 0x68) = local_50;
    *(int *)(*(int *)(obj + 0x34) + 0x6c) = local_4c;
    subobj_set_contact_origin(*(unsigned int *)(*(int *)(obj + 0x34) + 0x80), &local_54);

    // Spawn the six 0x112 platform pads into obj +0x44.., positioned from the
    // per-phase offset table for phases with DAT_804613b8 entries.
    DAT_80612a9c = 0;
    int padIdx = 0, tblByte = 0, phaseIdx = 0;
    do {
        local_6c = DAT_803e0104; local_68 = DAT_803e0108; local_64 = DAT_803e010c;
        if (phaseIdx < DAT_804613b8) {
            local_6c = DAT_803e0110 + *(float *)((int)&DAT_804613bc + tblByte);
            local_68 = (float)((double)DAT_803e0114 + (double)*(float *)((int)&DAT_804613c0 + tblByte));
            local_64 = (float)((double)DAT_803e0118 + (double)*(float *)((int)&DAT_804613c4 + tblByte));
        }
        ret = entity_spawn_by_type((double)local_64, (double)local_68, (double)local_6c,
                                   DOUBLE_806185a0, d688, 0, 0, 0,
                                   0x112, (int *)&local_6c, (int *)&local_48, 0, 0);
        *(int *)(obj + padIdx + 0x44) = ret;
        *(unsigned int *)(*(int *)(obj + padIdx + 0x44) + 0x80) = subobj_pool_alloc();
        {
            int pad = *(int *)(obj + padIdx + 0x44);
            subobj_build_descriptor_default((double)local_68, (double)local_64,
                                            (double)local_6c, DOUBLE_806185a0, d688, 0, 0, 0,
                                            *(unsigned int *)(pad + 0x80), pad, 0x18, -1,
                                            1.1755302e-38f);
        }
        subobj_dirty_list_add(*(unsigned int *)(*(int *)(obj + padIdx + 0x44) + 0x80));
        {
            int pad = *(int *)(obj + padIdx + 0x44);
            *(unsigned int *)(pad + 0x28) = *(unsigned int *)(pad + 0x28) | 0x10;
            *(unsigned int *)(pad + 0x28) = *(unsigned int *)(pad + 0x28) & 0xffffff9e;
        }
        phaseIdx++;
        padIdx += 4;
        tblByte += 0xc;
    } while (phaseIdx < 6);

    // Enable render/collision on the active pads for this phase.
    slot = 0;
    for (i = 0; i < DAT_804613b8; i++) {
        int pad = *(int *)(obj + slot + 0x44);
        *(unsigned int *)(pad + 0x28) = *(unsigned int *)(pad + 0x28) | 0x61;
        slot += 4;
    }
    *(int *)(obj + 0x40) = DAT_804613b8;
    dCore = (double)coreEnt;                            // keep spawn result live
    (void)dCore;
    FUN_80382730();
    return coreEnt;
}

// 0x802282b0 - Award the pending score delta to the local player and push a
// localized HUD banner describing the change. iVar6 is (newScore - stored);
// small gains play cue 0xd7e, big gains (>=500) play cue 0xd7f, and the sign of
// the delta selects the up/down glyph strings.
void worm_upl_award_and_hud(unsigned long long param_2, unsigned long long param_3,
                            unsigned long long param_4, unsigned long long param_5,
                            unsigned long long param_6, unsigned long long param_7,
                            unsigned long long param_8)
{
    int  paramsSmall[12];
    int  paramsBig[18];
    int obj = DAT_806128c0;
    int newScore, delta;
    void *glyphA = &DAT_80610ec4;
    void *glyphB = &DAT_80610ec4;
    void *sign   = &DAT_80610ec4;

    __save_gpr_28();
    newScore = worm_upl_recompute_score();
    delta = newScore - *(int *)(obj + 0x10);
    if (delta != 0) {
        if (delta < 500) {
            if (delta > 0) {
                sound_init_play_params((int)paramsSmall);
                paramsSmall[0] = 0xd7e;
                sound_play_request(paramsSmall);
            }
        } else {
            sound_init_play_params((int)paramsBig);
            paramsBig[0] = 0xd7f;
            sound_play_request(paramsBig);
            glyphB = &DAT_80610ec8;
            glyphA = &DAT_80610ec4;
            if (DAT_8060f948 == 4) {
                glyphA = &DAT_80610ecc;
            }
        }
        *(int *)(obj + 0x10) = newScore;
        if (delta >= 0) {
            sign = &DAT_80610ed0;
        }
        int *text = (int *)debug_log_formatted(0, param_2, param_3, param_4, param_5, param_6,
                                               param_7, param_8, &s__s_s_d_s_80461534,
                                               sign, glyphA, delta, glyphB);
        hud_text_submit(*(int *)(DAT_80611d74 + 4), 0xb, (uint *)text);
    }
    __restore_gpr_28();
}

// 0x802283a8 - UPL objective MASTER per-frame tick (installed via FUN_8022c054).
// Drives the animated banner scroll, one-time turret-entity binding to 0x5d
// battery entities, and the full analog-turret capture flow: reads the local
// pad, integrates the turret aim (words obj[0]/obj[1]) with rounding via
// FUN_80390324, fires cue 0xd7d on trigger, walks the capture state machine
// (obj[2]) that hands control between the three rig slots (obj[10]/obj[0xd..]),
// applies angular/linear force to the carried rig, and re-frames the camera.
// Skipped entirely while the session is frozen (DAT_805015c0 bit 0x40000000).
void worm_upl_obj_tick(void)
{
    int   obj = DAT_806128c0;
    int  *objw = (int *)DAT_806128c0;
    int   lvl = DAT_80611d78;
    float local_138, local_134, local_130;
    float qtmp[4];
    float local_11c;
    int   local_118, local_114;
    float aim110, aim10c, aim108;
    float force104, force100, forcefc;
    int   camDesc_f8, camDesc_f4, camDesc_f0;
    int   spawnDesc_ec, spawnDesc_e8, spawnDesc_e4;
    float ang_e0, ang_dc;
    int   ang_d8;
    int   soundParams[12];
    float mtx[17];
    double d;

    FUN_803826e4();

    // Banner scroll + score-to-pitch mapping (uses the sine LUT DAT_80583228).
    double coreVal = (double)(CONCAT44(0x43300000, (unsigned int)*objw) ^ 0x80000000);
    unsigned int lutIdx = (unsigned int)((double)FLOAT_80618690 *
        ((double)(float)((FLOAT_806185bc * (float)(coreVal - DOUBLE_806185a0)) / FLOAT_806185c4))
        + (double)FLOAT_806185d8);
    double wob = (double)(float)((double)FLOAT_806185d8 *
                 (double)(float)(&DAT_80583228)[lutIdx & 0x7ff] + (double)FLOAT_806185d8);
    double clampA = wob;
    if ((double)FLOAT_80618590 < wob) clampA = (double)FLOAT_80618590;
    double clampB = (double)FLOAT_80618594;
    if (((double)FLOAT_80618594 <= clampA) &&
        (clampB = wob, (double)FLOAT_80618590 < wob)) {
        clampB = (double)FLOAT_80618590;
    }
    double pitch;
    if (DAT_806109c0 == 0x32) {
        pitch = (double)FLOAT_80618698 * clampB + (double)FLOAT_80618694;
    } else {
        pitch = (double)FLOAT_80618698 * clampB + (double)FLOAT_8061869c;
    }
    pitch = (double)(float)pitch;

    if (objw[0x1b] == 0) {
        worm_upl_obj_spawn_entities();
    }
    // One-time bind: attach the objective core to each 0x5d battery entity.
    if (objw[0x1c] == 0) {
        int i6 = DAT_80612610 << 2;
        for (int i10 = DAT_80612610; i10 < DAT_80612098; i10++) {
            int *ent = *(int **)((int)&DAT_80588610 + i6);
            if (*(short *)((int)ent + 0x48) == 0x5d) {
                spawnDesc_ec = DAT_803e0128; spawnDesc_e8 = DAT_803e012c; spawnDesc_e4 = DAT_803e0130;
                int bound = entity_spawn_by_type(pitch, clampA, clampB, (double)FLOAT_80618690,
                                                 (double)FLOAT_806185d8, 0, 0, 0,
                                                 0x114, &spawnDesc_ec, (int *)&spawnDesc_ec, 0, 0);
                if (ent[8] != 6) ent[0x274] = 6;
                *(int *)(bound + 0x78) = *(int *)(*ent + 4);
                *(int *)(bound + 300) = *(int *)(*(int *)(*(int *)(*ent + 4) + 0x84) + 0x30);
                *(float *)(bound + 0x140) = FLOAT_806186a0;
                *(float *)(bound + 0x144) = FLOAT_806186a4;
                *(float *)(bound + 0x148) = FLOAT_806185e8;
            }
            i6 += 4;
            objw[0x1c] = 1;
        }
    }

    if ((DAT_805015c0 & 0x40000000) == 0) {
        // Integrate turret aim word obj[0] toward a rounded target.
        double v0 = (double)(CONCAT44(0x43300000, (unsigned int)*objw) ^ 0x80000000);
        double d0 = (double)(float)(v0 - DOUBLE_806185a0);
        double r0 = FUN_80390324((double)((float)(d0 - (double)FLOAT_80618594) / FLOAT_806185c0));
        *objw = (int)-(float)((double)FLOAT_806185c0 * (double)(float)r0 - d0);
        unsigned int v1bits = (unsigned int)objw[1] ^ 0x80000000;
        double d1 = (double)(float)((double)CONCAT44(0x43300000, v1bits) - DOUBLE_806185a0);
        double r1 = FUN_80390324((double)(float)((double)(float)(d1 - (double)FLOAT_80618594) /
                                                  (double)FLOAT_806185c0));
        objw[1] = (int)-(float)((double)FLOAT_806185c0 * (double)(float)r1 - d1);

        unsigned int held = input_get_buttons_held(*(int *)DAT_80611d74);
        if ((objw[2] != 0) && ((held & 0x80) != 0)) {
            sound_init_play_params((int)soundParams);
            soundParams[0] = 0xd7d;
            sound_play_request(soundParams);
            objw[2] = objw[2] + 1;
        }

        if (objw[10] < 3) {
            unsigned int idle = subobj_is_idle(*(unsigned int *)(objw[objw[10] + 0xd] + 0x80));
            int camObj = *(int *)(lvl + 8);
            if ((idle == 0) || (camObj == 0) ||
                (*(int *)(camObj + 0x218) != 4) ||
                (*(int *)(camObj + 0x2ac) != objw[objw[10] + 0xd])) {
                camObj = *(int *)(lvl + 8);
                if ((camObj != 0) && (*(int *)(camObj + 0x218) == 4) &&
                    (*(int *)(camObj + 0x2ac) == objw[objw[10] + 0xd])) {
                    worm_upl_set_target_pos(objw[objw[10] + 0xd]);
                }
            } else {
                camDesc_f8 = DAT_803e0134; camDesc_f4 = DAT_803e0138; camDesc_f0 = DAT_803e013c;
                camera_set_mode_by_index(camObj, 0, *(int *)(lvl + 0x14));
                int slotc = spectator_get_slot();
                *(unsigned int *)(slotc + 0x21c) = *(unsigned int *)(slotc + 0x21c) & 0xfffeffff;
                objw[10] = objw[10] + 1;
                if (objw[10] < 3) {
                    *(unsigned int *)(objw[objw[10] + 0xd] + 0x28) =
                        *(unsigned int *)(objw[objw[10] + 0xd] + 0x28) | 0x61;
                    *(int *)(objw[objw[10] + 0xd] + 100)  = camDesc_f8;
                    *(int *)(objw[objw[10] + 0xd] + 0x68) = camDesc_f4;
                    *(int *)(objw[objw[10] + 0xd] + 0x6c) = camDesc_f0;
                    subobj_set_contact_origin(*(unsigned int *)(objw[objw[10] + 0xd] + 0x80), &camDesc_f8);
                }
                objw[2] = 1;
                sound_set_listener_override(0);
                worm_upl_award_and_hud(0, 0, 0, 0, 0, 0, 0);
                spectator_instant_show();
                player_set_state(lvl, 0x20000);
                if (2 < objw[10]) {
                    objw[10] = 0;
                    worm_upl_advance_phase(0, 0, 0, 0, 0, 0, 0, 0);
                }
            }

            if (objw[2] == 3) {
                local_11c = *(float *)&DAT_803e0140; local_118 = DAT_803e0144; local_114 = DAT_803e0148;
                int spawn = spawn_point_find_by_entity(0x54475443);   // 'CTGT'
                aim110 = FLOAT_8061867c; aim10c = FLOAT_806185d0; aim108 = FLOAT_80618680;
                *(unsigned int *)(objw[0x17] + 0x28) = *(unsigned int *)(objw[0x17] + 0x28) & 0xfffffffe;
                objw[2] = 0;
                sound_set_listener_override(objw[objw[10] + 0xd] + 100);
                int *wpos = entity_get_world_position(objw[objw[10] + 0xd]);
                damage_play_pain_sound(0x5d, 0, wpos);
                force104 = (float)((double)*(float *)&objw[6] * pitch);
                force100 = (float)((double)*(float *)&objw[7] * pitch);
                forcefc  = (float)((double)*(float *)&objw[8] * pitch);
                double v1 = (double)(CONCAT44(0x43300000, (unsigned int)objw[1]) ^ 0x80000000);
                double t = math_poly_trig((double)((FLOAT_806185bc * (float)(v1 - DOUBLE_806185a0)) /
                                                   FLOAT_806185c4 - FLOAT_806185c8));
                ang_dc = -(FLOAT_80618628 * (float)((double)FLOAT_806186a8 * t));
                ang_e0 = *(float *)&DAT_803e011c; ang_d8 = DAT_803e0124;
                quat_from_axis_angle((double)FLOAT_806186ac, qtmp, &local_11c);
                quat_rotate_vector(qtmp, &force104, &aim110);
                aim110 = aim110 * FLOAT_80618658;
                aim10c = aim10c * FLOAT_80618658;
                aim108 = aim108 * FLOAT_80618658;
                subobj_add_angular_force(*(unsigned int *)(objw[objw[10] + 0xd] + 0x80), &ang_e0);
                subobj_add_linear_force(*(unsigned int *)(objw[objw[10] + 0xd] + 0x80), &force104);
                camera_set_mode(*(int *)(lvl + 8), spawn, objw[objw[10] + 0xd]);
                camera_set_mode_from_spawn(objw + 0x1d, objw[objw[10] + 0xd]);
                int slotc = spectator_get_slot();
                *(int *)(slotc + 0x2ac) = objw[objw[10] + 0xd];
                spectator_set_mode_if_ready((double)FLOAT_8061866c, objw + 0x1d);
                player_init_spawn_position(slotc, (int *)(objw[objw[10] + 0xd] + 100),
                                           *(int *)(objw[objw[10] + 0xd] + 0x34));
                objw[0x18] = *(int *)(objw[objw[10] + 0xd] + 100);
                objw[0x19] = *(int *)(objw[objw[10] + 0xd] + 0x68);
                objw[0x1a] = *(int *)(objw[objw[10] + 0xd] + 0x6c);
                player_set_state(lvl, 0x20000);
            }
            if (objw[2] == 1) {
                *objw = *objw + 4;
            }
            if (objw[2] == 2) {
                int zone = soundpad_resolve_entity_to_zone(
                    *(int *)(DAT_80611b8c + *(int *)(*(int *)(((int *)DAT_80611d74)[5] + 0x7c) + 0x678) * 0x20));
                local_138 = FLOAT_8061867c; local_134 = FLOAT_806185d0; local_130 = FLOAT_80618680;
                objw[1] = objw[1] + 3;
                *(unsigned int *)(objw[0x17] + 0x28) = *(unsigned int *)(objw[0x17] + 0x28) | 1;
                double vv = (double)(CONCAT44(0x43300000, (unsigned int)objw[1]) ^ 0x80000000);
                double tt = math_poly_trig((double)((FLOAT_806185bc * (float)(vv - DOUBLE_806185a0)) /
                                                    FLOAT_806185c4 - FLOAT_806185c8));
                *(float *)(objw[0x17] + 0x70) =
                    (float)((double)FLOAT_80618688 * tt + (double)*(float *)(zone + 0x18));
                unsigned int vbits = (unsigned int)objw[1] ^ 0x80000000;
                double t2 = math_poly_trig((double)((FLOAT_806185bc *
                    (float)((double)CONCAT44(0x43300000, vbits) - DOUBLE_806185a0)) /
                    FLOAT_806185c4 - FLOAT_806185c8));
                objw[9] = (int)(float)((double)FLOAT_80618688 * t2);
                double cl = angle_wrap_clamp((double)(float)objw[9], (double)FLOAT_806186b0,
                                             (double)FLOAT_80618688);
                objw[9] = (int)(float)cl;
                mtx4_set_rotation_y((double)((FLOAT_806185bc * (float)objw[9]) / FLOAT_806185c4), mtx);
                mtx4_transform_vector(mtx, &local_138);
                objw[6] = (int)local_138;
                objw[7] = (int)local_134;
                objw[8] = (int)local_130;
            }
        }
        objw[0x18] = *(int *)(objw[objw[10] + 0xd] + 100);
        objw[0x19] = *(int *)(objw[objw[10] + 0xd] + 0x68);
        objw[0x1a] = *(int *)(objw[objw[10] + 0xd] + 0x6c);
    }
    (void)d;
    FUN_80382730();
}

// 0x80228d28 - Map a rig's distance-from-target to a point value. Computes the
// 3D distance between the rig contact point (+0x64/+0x68/+0x6c) and the fixed
// target DAT_803e014c.. then buckets it into 500/400/200/100/0x32/0x14/10.
int worm_upl_distance_to_points(int rig)
{
    int   points = 0;
    float dx = *(float *)&DAT_803e0150 - *(float *)(rig + 0x68);
    float dy = *(float *)&DAT_803e014c - *(float *)(rig + 100);
    float dz = *(float *)&DAT_803e0154 - *(float *)(rig + 0x6c);
    double dist = ABS(math_sqrt((double)(dz * dz + dy * dy + dx * dx)));
    if ((double)FLOAT_806186b4 <= dist) {
        if ((double)FLOAT_806186b8 <= dist) {
            if ((double)FLOAT_806186bc <= dist) {
                if ((double)FLOAT_806186c0 <= dist) {
                    if ((double)FLOAT_806186c4 <= dist) {
                        if ((double)FLOAT_80618660 <= dist) {
                            if (dist < (double)FLOAT_80618688) points = 10;
                        } else {
                            points = 0x14;
                        }
                    } else {
                        points = 0x32;
                    }
                } else {
                    points = 100;
                }
            } else {
                points = 200;
            }
        } else {
            points = 400;
        }
    } else {
        points = 500;
    }
    return points;
}

// 0x80228e34 - Recompute the objective's running score into obj +0xc (this-pass)
// and obj +0xc + baseline +0x14 into the per-player HUD mirror DAT_806128e8 and
// the global tally DAT_806128f4. Returns the total. When disarmed (+0x6c == 0)
// it zeroes everything.
int worm_upl_recompute_score(void)
{
    int obj = DAT_806128c0;
    int result = 0;
    *(int *)(obj + 0xc) = 0;
    if (*(int *)(obj + 0x6c) == 0) {
        result = 0;
        DAT_806128f4 = 0;
        *(int *)(&DAT_806128e8 + *(int *)(DAT_80611d74 + 4) * 4) = 0;
    } else {
        int rigIdx = 0;
        int cur = obj;
        for (int i = 0; i <= *(int *)(obj + 0x28); i++) {
            if (i < 3) {
                *(int *)(obj + 0xc) += worm_upl_distance_to_points(*(int *)(obj + rigIdx + 0x34));
            }
            rigIdx += 4;
            cur = obj;
        }
        DAT_806128f4 = *(int *)(obj + 0x14) + *(int *)(obj + 0xc);
        *(int *)(&DAT_806128e8 + *(int *)(DAT_80611d74 + 4) * 4) = DAT_806128f4;
        result = *(int *)(cur + 0x14) + *(int *)(cur + 0xc);
    }
    return result;
}

// 0x80228f10 - Return this-pass score (obj +0xc).
int worm_upl_get_score(void)
{
    return *(int *)(DAT_806128c0 + 0xc);
}

// 0x80228f1c - Score the current phase's pad strip: for each active pad
// (count from the per-phase table indexed by phase +0x2c) accumulate its
// distance-based points. Only runs while the objective is armed.
void worm_upl_score_strip(void)
{
    int obj = DAT_806128c0;
    __save_gpr_28();
    if (*(int *)(obj + 0x6c) != 0) {
        int padIdx = 0;
        int count = (&DAT_804613b8)[*(int *)(obj + 0x2c) * 0x13];
        for (int i = 0; i < count; i++) {
            worm_upl_distance_to_points(*(int *)(obj + padIdx + 0x44));
            padIdx += 4;
        }
    }
    __restore_gpr_28();
}

// 0x80228fac - Advance to the next objective phase (or finish). Banks the
// running score into +0x14, and if another phase exists (phase+1 < 5) whose
// strip score beats the current, bumps phase +0x2c, announces via string
// 0x66f, tears down and re-spawns the three turret rigs (0x111) and the pad
// strip (0x112) for the new phase. Otherwise sets the finished flag +0x30 and
// resets the rig contact origins.
void worm_upl_advance_phase(unsigned long long param_1, double param_2, double param_3,
                            double param_4, unsigned long long param_5,
                            unsigned long long param_6, unsigned long long param_7,
                            unsigned long long param_8)
{
    int   obj = DAT_806128c0;
    float local_4c, local_48, local_44;
    int   local_40, local_3c, local_38;
    float local_34;
    int   local_30, local_2c;
    int   local_28, local_24, local_20;
    int   i, slot, phaseByte, count;

    FUN_803826e4();
    local_28 = DAT_803e0158; local_24 = DAT_803e015c; local_20 = DAT_803e0160;
    local_34 = *(float *)&DAT_803e0164; local_30 = DAT_803e0168; local_2c = DAT_803e016c;
    local_40 = DAT_803e0170; local_3c = DAT_803e0174; local_38 = DAT_803e0178;

    *(int *)(obj + 0x14) += *(int *)(obj + 0xc);
    if (*(int *)(obj + 0x2c) + 1 < 5) {
        // score_strip returns the strip-score total; get_score returns the
        // running this-pass score. Advance only when the strip beat the pass.
        worm_upl_score_strip();          // iVar1 (strip return, see ROM)
        int stripScore = worm_upl_get_score();   // reuse as comparison proxy
        int passScore  = worm_upl_get_score();
        if (stripScore < passScore) {
            *(int *)(obj + 0x2c) += 1;
            int fmt = get_string_by_id(0x66f);
            int *text = (int *)debug_log_formatted(0, param_2, param_3, param_4, param_5,
                                                   param_6, param_7, param_8, fmt,
                                                   *(int *)(obj + 0x2c) + 1, obj);
            hud_text_submit(*(int *)(DAT_80611d74 + 4), 0xb, (uint *)text);

            // Tear down + respawn the three 0x111 turret rigs for the new phase.
            slot = 0;
            for (i = 0; i < 3; i++) {
                int *old = *(int **)(obj + slot + 0x34);
                if (old != 0) entity_destroy_with_event(old);
                int rig = entity_spawn_by_type((double)param_2, param_3, param_4,
                                               DOUBLE_806185a0, (double)FLOAT_80618688,
                                               0, 0, 0, 0x111, &local_28, (int *)&local_34, 0, 0);
                *(int *)(obj + slot + 0x34) = rig;
                *(unsigned int *)(*(int *)(obj + slot + 0x34) + 0x80) = subobj_pool_alloc();
                subobj_build_descriptor_default((double)param_2, param_3, param_4,
                                                DOUBLE_806185a0, (double)FLOAT_80618688, 0, 0, 0,
                                                *(unsigned int *)(*(int *)(obj + slot + 0x34) + 0x80),
                                                *(int *)(obj + slot + 0x34), 0x18, -1, 1.1755302e-38f);
                *(unsigned int *)(*(int *)(obj + slot + 0x34) + 0x28) =
                    *(unsigned int *)(*(int *)(obj + slot + 0x34) + 0x28) | 0x10;
                *(unsigned int *)(*(int *)(obj + slot + 0x34) + 0x28) =
                    *(unsigned int *)(*(int *)(obj + slot + 0x34) + 0x28) & 0xffffff9e;
                entity_register_for_update(*(int *)(obj + slot + 0x34), 3, -0x7fdd82e8);
                slot += 4;
            }
            *(unsigned int *)(*(int *)(obj + 0x34) + 0x28) =
                *(unsigned int *)(*(int *)(obj + 0x34) + 0x28) | 0x61;
            *(int *)(*(int *)(obj + 0x34) + 100)  = local_40;
            *(int *)(*(int *)(obj + 0x34) + 0x68) = local_3c;
            *(int *)(*(int *)(obj + 0x34) + 0x6c) = local_38;
            subobj_set_contact_origin(*(unsigned int *)(*(int *)(obj + 0x34) + 0x80), &local_40);

            // Re-spawn the pad strip (0x112) for the new phase.
            phaseByte = 0; slot = 0; i = 0;
            do {
                local_4c = DAT_803e017c; local_48 = DAT_803e0180; local_44 = DAT_803e0184;
                int *oldPad = *(int **)(obj + slot + 0x44);
                if (oldPad != 0) entity_destroy_with_event(oldPad);
                if (i < (&DAT_804613b8)[*(int *)(obj + 0x2c) * 0x13]) {
                    int row = phaseByte + *(int *)(obj + 0x2c) * 0x4c;
                    local_4c = DAT_803e0188 + *(float *)((int)&DAT_804613b8 + row + 4);
                    local_48 = (float)((double)DAT_803e018c +
                               (double)*(float *)((int)&DAT_804613b8 + row + 8));
                    local_44 = (float)((double)DAT_803e0190 +
                               (double)*(float *)((int)&DAT_804613b8 + row + 0xc));
                }
                int pad = entity_spawn_by_type((double)local_48, (double)local_44,
                                               (double)local_4c, DOUBLE_806185a0,
                                               (double)FLOAT_80618688, 0, 0, 0,
                                               0x112, (int *)&local_4c, (int *)&local_34, 0, 0);
                *(int *)(obj + slot + 0x44) = pad;
                *(unsigned int *)(*(int *)(obj + slot + 0x44) + 0x80) = subobj_pool_alloc();
                subobj_build_descriptor_default((double)local_48, (double)local_44,
                                                (double)local_4c, DOUBLE_806185a0,
                                                (double)FLOAT_80618688, 0, 0, 0,
                                                *(unsigned int *)(*(int *)(obj + slot + 0x44) + 0x80),
                                                *(int *)(obj + slot + 0x44), 0x18, -1, 1.1755302e-38f);
                subobj_dirty_list_add(*(unsigned int *)(*(int *)(obj + slot + 0x44) + 0x80));
                *(unsigned int *)(*(int *)(obj + slot + 0x44) + 0x28) =
                    *(unsigned int *)(*(int *)(obj + slot + 0x44) + 0x28) | 0x10;
                *(unsigned int *)(*(int *)(obj + slot + 0x44) + 0x28) =
                    *(unsigned int *)(*(int *)(obj + slot + 0x44) + 0x28) & 0xffffff9e;
                i++; phaseByte += 0xc; slot += 4;
            } while (i < 6);

            slot = 0;
            count = (&DAT_804613b8)[*(int *)(obj + 0x2c) * 0x13];
            for (i = 0; i < count; i++) {
                int pad = *(int *)(obj + slot + 0x44);
                *(unsigned int *)(pad + 0x28) = *(unsigned int *)(pad + 0x28) | 0x61;
                slot += 4;
            }
            *(int *)(obj + 0x40) = (&DAT_804613b8)[*(int *)(obj + 0x2c) * 0x13];
            FUN_80382730();
            return;
        }
    }
    // Objective finished: latch +0x30 and reset the three rig contact origins.
    *(int *)(obj + 0x30) = 1;
    slot = 0;
    for (i = 0; i < 3; i++) {
        int rig = *(int *)(obj + slot + 0x34);
        *(int *)(rig + 100)  = local_28;
        *(int *)(rig + 0x68) = local_24;
        *(int *)(rig + 0x6c) = local_20;
        subobj_set_contact_origin(*(unsigned int *)(rig + 0x80), &local_28);
        *(unsigned int *)(rig + 0x28) = *(unsigned int *)(rig + 0x28) & 0xffffff9e;
        slot += 4;
    }
    FUN_80382730();
}

// 0x80229434 - Remaining rig count = 3 - active-rig index (obj +0x28).
int worm_upl_get_remaining(void)
{
    return 3 - *(int *)(DAT_806128c0 + 0x28);
}

// 0x80229450 - Scripted-prop random spawner tick. Picks two rng-band indices,
// spawns an activation prop (type 0x80) using either the special one-shot id
// 0xfb (latched by DAT_806128cc) or a table-selected id from DAT_80610ed4,
// installs its update phases, applies a band-selected material, then does a
// frustum-visibility floor snap and marks the prop for the ambient update pass.
void worm_prop_random_spawn_tick(void)
{
    float local_a8;
    int   local_a4, local_a0, local_9c, local_98, local_94;
    float local_90, local_8c, local_88, local_84, local_80, local_7c;
    float local_48, local_44, local_40;
    unsigned int uStack_34, uStack_24;
    int band0, prop;
    int bandSel;
    double frac;

    FUN_803826f0();
    local_9c = DAT_803e0194; local_98 = DAT_803e0198; local_94 = DAT_803e019c;
    local_a8 = *(float *)&DAT_803e01a0; local_a4 = DAT_803e01a4; local_a0 = DAT_803e01a8;

    uStack_34 = rng_counter_tick();
    band0 = (int)(FLOAT_80618604 *
             ((float)((double)CONCAT44(0x43300000, uStack_34) - DOUBLE_806185f8) / FLOAT_806185ec)
             + FLOAT_806185d8);
    uStack_24 = rng_counter_tick();
    frac = (double)FLOAT_806185d8 +
           (double)(float)((double)(float)((double)CONCAT44(0x43300000, uStack_24) - DOUBLE_806185f8)
                           / (double)FLOAT_806185ec);
    bandSel = (int)frac;

    int alive = FUN_8022b45c();
    if ((alive / DAT_806109c0 < 0x1e) && (DAT_806128cc == 0)) {
        prop = entity_spawn_with_activation(0, 0, DOUBLE_806185f8, 0, 0, 0, 0, 0,
                                            0x80, 0xfb, &local_9c, (int *)&local_a8, 0, 0);
        worm_scripted_prop_on_spawn(0, DOUBLE_806185f8, 0, 0, 0, 0, 0, 0);
        DAT_806128cc = 1;
    } else {
        prop = entity_spawn_with_activation(0, 0, DOUBLE_806185f8, 0, 0, 0, 0, 0,
                                            0x80, *(int *)(&DAT_80610ed4 + bandSel * 4),
                                            &local_9c, (int *)&local_a8, 0, 0);
    }

    bool hasHandler = false;
    if ((*(int *)(prop + 0x15c) != 0) || event_has_handler(*(int *)(prop + 0x20), 2)) {
        hasHandler = true;
    }
    if (!hasHandler) {
        entity_register_for_update(prop, 2, -0x7ffdffcc);
    }
    if (prop != 0) {
        entity_register_for_update(prop, 3, -0x7fe27380);
        render_instance_set_material((double)FLOAT_80618644, *(int **)(prop + 4),
                                     *(int *)(&DAT_80461830 + band0 * 4));
        anim_blend_eval_direct();
        local_48 = local_48 + *(float *)(prop + 0x1a8);
        local_44 = local_44 + *(float *)(prop + 0x1ac);
        local_40 = local_40 + *(float *)(prop + 0x1b0);
        local_84 = local_48 + *(float *)(prop + 0x1c0);
        local_90 = local_48 - *(float *)(prop + 0x1c0);
        local_80 = local_44 + *(float *)(prop + 0x1c4);
        local_8c = local_44 - *(float *)(prop + 0x1c4);
        local_7c = local_40 + *(float *)(prop + 0x1c8);
        local_88 = local_40 - *(float *)(prop + 0x1c8);
        int vis = scene_query_nodes_in_frustum(0, (int)&local_90, prop + 0x38, 10);
        *(char *)(prop + 0x30) = (char)vis;
        if (*(char *)(prop + 0x30) == '\0') {
            *(int *)(prop + 0x34) = floor_height_from_position((double)FLOAT_806185a8,
                                                               (double)FLOAT_806185a8);
            *(unsigned int *)(prop + 0x28) = *(unsigned int *)(prop + 0x28) | 0x40000000;
        } else {
            *(int *)(prop + 0x34) = *(int *)(prop + 0x38);
        }
        *(unsigned int *)(prop + 0x28) = *(unsigned int *)(prop + 0x28) | 0x40000000;
        *(int *)(prop + 0x34) = 0xd5;
        *(unsigned int *)(prop + 0x18) = *(unsigned int *)(prop + 0x18) | 2;
        FUN_8023c990();
    }
    FUN_8038273c();
}

// ===========================================================================
// batch_687: scripted-prop spawn cadence/scoring/preload + the NETB
// (net/ball goal) objective object DAT_806128c4 (0x130 bytes).
//
// The first four functions close out the scripted-prop subsystem started in
// batch_686 (worm_prop_random_spawn_tick @0x80229450): a per-frame spawn-rate
// gate, the pickup score handler, resource preloading, and the per-round reset.
// The remaining six implement a distinct "NETB/BALL" goal objective: the object
// caches goal-line AABBs built from 'NETB' entities and, each frame, tests every
// 'BALL' entity's swept motion against those goal lines to award points.
//
// Address: 0x80229724 | Name: worm_prop_spawn_cadence_tick  | 2026-09-09
// Address: 0x8022976c | Name: worm_prop_award_pickup        | 2026-09-09
// Address: 0x802297c4 | Name: worm_prop_preload_resources   | 2026-09-09
// Address: 0x8022981c | Name: worm_prop_obj_reset           | 2026-09-09
// Address: 0x80229864 | Name: worm_netb_obj_alloc           | 2026-09-09
// Address: 0x80229898 | Name: worm_netb_obj_reset           | 2026-09-09
// Address: 0x802298c4 | Name: worm_netb_build_goal_bounds   | 2026-09-09
// Address: 0x80229a70 | Name: worm_netb_test_ball_crossed   | 2026-09-09
// Address: 0x80229b28 | Name: worm_netb_ball_score_value    | 2026-09-09
// Address: 0x80229b94 | Name: worm_netb_ball_master_tick    | 2026-09-09
// ---------------------------------------------------------------------------
// File-local externs new to batch_687.
extern int   DAT_806128d0;      // scripted-prop spawn-rate accumulator
extern int   DAT_806128c4;      // pointer to the 0x130-byte NETB objective object
extern int   DAT_803e01ac;      // small-ball explosion offset vec (x)
extern int   DAT_803e01b0;      // small-ball explosion offset vec (y)
extern int   DAT_803e01b4;      // small-ball explosion offset vec (z)
extern int   DAT_803e01b8;      // scored-ball explosion offset vec (x)
extern int   DAT_803e01bc;      // scored-ball explosion offset vec (y)
extern int   DAT_803e01c0;      // scored-ball explosion offset vec (z)
extern float FLOAT_806186c8;    // ball lane upper bound
extern float FLOAT_806186cc;    // ball lane lower bound
extern float FLOAT_806186d0;    // mover-progress despawn threshold
extern float FLOAT_806186d4;    // ball min-height gate
extern float FLOAT_806186d8;    // stereo cue pan
extern float FLOAT_806186dc;    // stereo cue volume
// Cross-module helpers (call-site typed, file-local for this TU).
extern void  object_activate_slot_tagged(int slotBlock, int tag);
extern void  entity_get_sub_object_vertex_world_pos(float *out, int entity, int vtxA, int vtxB);
extern int   mover_find_by_entity_id(int fourcc);
extern int   mover_get_active_state(int mover);
extern void  mover_activate(int mover);
extern void  sound_play_stereo_pair(double a, double b, double c);
extern void  subobj_clear_flag_bits(int subId, int bits);

// 0x80229724 - Scripted-prop spawn cadence: accumulate the per-frame delta and,
// once it exceeds playerCount*5, fire a random prop spawn and reset the timer.
void worm_prop_spawn_cadence_tick(void)
{
    DAT_806128d0 = DAT_806128d0 + DAT_806108e4;
    if (DAT_806109c0 * 5 < DAT_806128d0) {
        worm_prop_random_spawn_tick();
        DAT_806128d0 = 0;
    }
}

// 0x8022976c - Award points for collecting a scripted prop. The special prop id
// 0xfb is worth 100; all others are worth 0x19. Bumps the global tally
// DAT_806128f4 and pushes the world message on the prop's record.
void worm_prop_award_pickup(int propId, int *rec)
{
    if (propId == 0xfb) {
        DAT_806128f4 = DAT_806128f4 + 100;
        hud_worldmsg_push(rec, 100);
    } else {
        DAT_806128f4 = DAT_806128f4 + 0x19;
        hud_worldmsg_push(rec, 0x19);
    }
}

// 0x802297c4 - Preload the scripted-prop resources: the two table entries in
// DAT_80610ed4 plus the special prop id 0xfb.
void worm_prop_preload_resources(void)
{
    FUN_803826f0();
    int off = 0;
    for (int i = 0; i < 2; i++) {
        entity_preload_resources(*(int *)(&DAT_80610ed4 + off));
        off += 4;
    }
    entity_preload_resources(0xfb);
    FUN_8038273c();
}

// 0x8022981c - Per-round reset of the scripted-prop subsystem: re-tag the
// level's activation slot block and clear the spawn accumulator + one-shot latch.
void worm_prop_obj_reset(void)
{
    object_activate_slot_tagged(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c), 0);
    DAT_806128d0 = 0;
    DAT_806128cc = 0;
}

// 0x80229864 - Allocate the 0x130-byte NETB (net/ball goal) objective object
// DAT_806128c4 and reserve its cue bank 0xd82.
void worm_netb_obj_alloc(void)
{
    DAT_806128c4 = mem_alloc(0x130, 1);
    sound_clear_bank_ref(0xd82);
}

// 0x80229898 - Zero the NETB object's live state (0x124 bytes; leaves the
// trailing goal-count/frame counters untouched).
void worm_netb_obj_reset(void)
{
    memset_wrapper(DAT_806128c4, 0, 0x124);
}

// 0x802298c4 - Build the goal-line AABB table from up to 10 'NETB' entities.
// For each net entity it reads two edge vertices (world space) and stores the
// per-axis min/max box (stride 0x1c) plus the entity handle at +0x00 into the
// object's goal-line array; +0x120 holds the count. Skips nets with no model.
void worm_netb_build_goal_bounds(void)
{
    float local_58, local_54, local_50, local_4c, local_48, local_44;
    int   nets[16];
    int   obj = DAT_806128c4;
    int   count, off = 0;

    __save_gpr_28();
    count = FUN_8023c59c(0x424e4554, (int)nets, 10);      // 'NETB'
    *(int *)(obj + 0x120) = count;
    int i3 = 0;
    for (int i = 0; i < *(int *)(obj + 0x120); i++) {
        if (*(int *)(*(int *)(*(int *)((int)nets + i3) + 4) + 0x10) == 0) goto done;
        entity_get_sub_object_vertex_world_pos(&local_4c, *(int *)((int)nets + i3), 0, 1);
        entity_get_sub_object_vertex_world_pos(&local_58, *(int *)((int)nets + i3), 0, 0);
        // X min/max
        *(float *)(obj + off + 4)   = (local_4c < local_58) ? local_4c : local_58;
        *(float *)(obj + off + 0x10) = (local_58 < local_4c) ? local_4c : local_58;
        // Y min/max
        *(float *)(obj + off + 8)   = (local_48 < local_54) ? local_48 : local_54;
        *(float *)(obj + off + 0x14) = (local_54 < local_48) ? local_48 : local_54;
        // Z min/max
        *(float *)(obj + off + 0xc)  = (local_44 < local_50) ? local_44 : local_50;
        *(float *)(obj + off + 0x18) = (local_50 < local_44) ? local_44 : local_50;
        *(int *)(obj + off) = *(int *)((int)nets + i3);
        i3 += 4;
        off += 0x1c;
    }
    *(int *)(obj + 0x118) = *(int *)(obj + 0x118) + 1;    // net-scan generation
done:
    __restore_gpr_28();
}

// 0x80229a70 - Test whether a ball's swept motion (from param_2 to param_3,
// radius param_1) crosses any cached goal-line AABB. Returns the goal-line
// index, or -1 if none.
int worm_netb_test_ball_crossed(double radius, float *from, float *to)
{
    float vel[3];
    int   bounds[17];
    int   obj = DAT_806128c4;

    vel[0] = *to - *from;
    vel[1] = to[1] - from[1];
    vel[2] = to[2] - from[2];
    build_swept_bounds(radius, bounds, from, vel);
    int idx = 0, off = 0;
    while (idx < *(int *)(obj + 0x120)) {
        if (aabb_vs_entity_bounds((int)bounds, (float *)(obj + off + 4)) != 0) {
            return idx;
        }
        idx++;
        off += 0x1c;
    }
    return -1;
}

// 0x80229b28 - Score value for a ball that reached a goal line. A "clean" ball
// (+200 == 0.0) scores 0x32/100/0x14 by lane position (+100), a dirty ball
// scores 10; the value is halved if the ball is below the level floor plane
// (+0x68 < level +0xac).
int worm_netb_ball_score_value(int ball)
{
    int value;
    if (FLOAT_80618594 == *(float *)(ball + 200)) {
        if (*(float *)(ball + 100) <= FLOAT_806186c8) {
            if (FLOAT_806186cc <= *(float *)(ball + 100)) {
                value = 0x32;
            } else {
                value = 100;
            }
        } else {
            value = 0x14;
        }
    } else {
        value = 10;
    }
    if (*(float *)(ball + 0x68) < *(float *)(DAT_80611d74 + 0xac)) {
        value = value >> 1;
    }
    return value;
}

// 0x80229b94 - NETB ball MASTER per-frame tick. Rebuilds goal bounds while the
// net-scan generation is below 5 and re-arms the 'INTL' intro mover, then for
// each 'BALL' (0x42414c4c) entity: balls above the min-height gate that cross a
// goal line award points (accumulated onto DAT_806128f4) with a stereo cue and,
// in the 0x12 sub-mode, a scoring explosion + world message + destroy; balls
// below the height gate spawn a small explosion and are destroyed.
void worm_netb_ball_master_tick(void)
{
    int   balls[26];
    float scoredFx_c8; int scoredFx_c4, scoredFx_c0;
    float smallFx_bc;  int smallFx_b8, smallFx_b4;
    int   obj = DAT_806128c4;

    FUN_803826e4();
    int count = FUN_8023c59c(0x42414c4c, (int)balls, 0x19);    // 'BALL'
    int mover = mover_find_by_entity_id(0x494e544c);           // 'INTL'
    if ((DAT_80611934 != 5) && (mover_get_active_state(mover) == 0)) {
        mover_activate(mover);
    }

    // Refresh the goal-line bounds until the scan generation reaches 5, and
    // wake up any idle net subobjects so their world verts are valid.
    if (*(int *)(obj + 0x118) < 5) {
        worm_netb_build_goal_bounds();
        int o = 0;
        for (int i = 0; i < count; i++) {
            int ball = *(int *)((int)balls + o);
            if ((ball != 0) && (*(unsigned int *)(ball + 0x80) != 0) &&
                (subobj_is_idle(*(unsigned int *)(ball + 0x80)) != 0)) {
                subobj_dirty_list_add(*(unsigned int *)(ball + 0x80));
            }
            o += 4;
        }
    }
    if ((count == 0) || (*(float *)(mover + 0x68) < FLOAT_806186d0)) {
        *(int *)(obj + 0x11c) = 1;    // intro complete
    }

    int o = 0;
    for (int i = 0; i < count; i++) {
        int *ball = *(int **)((int)balls + o);
        if (ball != 0) {
            if (FLOAT_806186d4 <= (float)*(float *)&ball[0x1a]) {
                int goal = worm_netb_test_ball_crossed(
                    (double)(*(float *)&ball[0x71] * FLOAT_806185d8),
                    (float *)&ball[0x19], (float *)&ball[0x2a]);
                if (goal != -1) {
                    unsigned int vbits = (unsigned int)worm_netb_ball_score_value(
                                             *(int *)(obj + goal * 0x1c)) ^ 0x80000000;
                    double add = (double)(float)((double)CONCAT44(0x43300000, vbits) - DOUBLE_806185a0);
                    if (add <= (double)FLOAT_80618594) {
                        add = (double)FLOAT_80618590;
                    }
                    unsigned int tbits = (unsigned int)DAT_806128f4 ^ 0x80000000;
                    DAT_806128f4 = (int)((double)(float)((double)CONCAT44(0x43300000, tbits) -
                                                         DOUBLE_806185a0) + add);
                    sound_play_stereo_pair((double)FLOAT_806185a8, (double)FLOAT_806186d8,
                                           (double)FLOAT_806186dc);
                    if (DAT_805015d8 == 0x12) {
                        scoredFx_c8 = *(float *)&DAT_803e01b8;
                        scoredFx_c4 = DAT_803e01bc;
                        scoredFx_c0 = DAT_803e01c0;
                        hud_worldmsg_push(&ball[0x19], (short)(int)add);
                        player_ai_explosion_spawn(0x22, 0xffffffff, (float *)&ball[0x19],
                                                  &scoredFx_c8, 0, (int)ball, 1);
                        subobj_clear_flag_bits(ball[0x20], 0x4000000);
                        entity_destroy_with_event(ball);
                        ball = 0;
                    }
                }
                if (ball != 0) {
                    ball[0x2a] = ball[0x19];   // store previous position
                    ball[0x2b] = ball[0x1a];
                    ball[0x2c] = ball[0x1b];
                    if (ball[0x20] == 0) {
                        entity_destroy_with_event(ball);
                    }
                }
            } else {
                smallFx_bc = *(float *)&DAT_803e01ac;
                smallFx_b8 = DAT_803e01b0;
                smallFx_b4 = DAT_803e01b4;
                player_ai_explosion_spawn(0x22, 0xffffffff, (float *)&ball[0x19],
                                          &smallFx_bc, 0, (int)ball, 1);
                subobj_clear_flag_bits(ball[0x20], 0x4000000);
                entity_destroy_with_event(ball);
            }
        }
        o += 4;
    }
    FUN_80382730();
}

// ===========================================================================
// batch_688: the SPRG/SPR2 (dual-runner sprint/relay) objective object
// DAT_806128c8 (0x20 bytes) plus a mode-dispatch trampoline. This objective
// tracks two named runner entities 'SPRG' and 'SPR2', picks which one is the
// active target (rng weighted by which are alive), spawns split-screen chase
// cameras for both, and counts down a timer while awarding pickup points for
// tagged 'N' checkpoint entities.
//
// Address: 0x80229e90 | Name: worm_sprg_obj_alloc          | 2026-09-09
// Address: 0x80229ebc | Name: worm_sprg_obj_reset          | 2026-09-09
// Address: 0x80229ee8 | Name: worm_sprg_get_score          | 2026-09-09
// Address: 0x80229f1c | Name: worm_sprg_both_active        | 2026-09-09
// Address: 0x80229f8c | Name: worm_sprg_hud_pickup         | 2026-09-09
// Address: 0x8022a010 | Name: worm_sprg_select_runner      | 2026-09-09
// Address: 0x8022a25c | Name: worm_sprg_award_pickup       | 2026-09-09
// Address: 0x8022a280 | Name: worm_sprg_activate_next      | 2026-09-09
// Address: 0x8022a358 | Name: worm_sprg_master_tick        | 2026-09-09
// Address: 0x8022a8a0 | Name: worm_mission_mode_dispatch   | 2026-09-09
// ---------------------------------------------------------------------------
// File-local externs new to batch_688.
extern int   DAT_806128c8;      // pointer to the 0x20-byte SPRG objective object
extern int   DAT_806109c4;      // screen width (for split-screen viewport rect)
extern int   DAT_806109c8;      // screen height
extern int   DAT_804619d8;      // int[] soundpad zone-id table (runner routes)
extern int   DAT_804619e8;      // countdown step table: threshold (0xc stride)
extern float DAT_804619ec;      // countdown step table: value  (0xc stride)
extern int   DAT_80461a0c;      // countdown fallback threshold base (>=4)
extern float FLOAT_806186e0;    // viewport near-depth base
extern float FLOAT_806186e4;    // viewport far-depth
extern float FLOAT_8060fad0;    // viewport depth bias
// Cross-module helpers (call-site typed, file-local for this TU).
extern void  hud_text_submit_pickup(int channel, int strId, int value);
extern void  hud_text_submit_objective(int channel, int strId, int value);
extern int   screen_effect_alloc_slot(void);
extern int   screen_effect_init_slot(int slot, int entity);
extern int   player_slot_alloc(double d1, double d2, double d3, double d4,
                               unsigned long long p5, unsigned long long p6,
                               unsigned long long p7, unsigned long long p8, int flag);
extern void  viewport_set_depth_params(double near, double far, double bias, int outBlock);
extern void  viewport_set_rect(int *rect, int x, int y, int w, int h);
extern int   FUN_8023cc28(int fourcc);          // resolve named entity record (SPRG/SPR2)
extern void  FUN_80240710(int *rec);            // install runner chase camera rig
extern int   FUN_8023d2c0(int id);              // resolve node -> camera-lerp source
unsigned char *worm_mode_get_object(int mode);  // 0x8022d9a4 mode->object table lookup (defined below)

// 0x80229e90 - Allocate the 0x20-byte SPRG (dual-runner) objective object.
void worm_sprg_obj_alloc(void)
{
    DAT_806128c8 = mem_alloc(0x20, 1);
}

// 0x80229ebc - Zero the SPRG object live state (0x14 bytes).
void worm_sprg_obj_reset(void)
{
    memset_wrapper(DAT_806128c8, 0, 0x14);
}

// 0x80229ee8 - Return the SPRG score (+0x10), but only in the relay sub-mode
// (session flag bit 0x40 set and extra-mode == 0x13); otherwise 0.
int worm_sprg_get_score(void)
{
    if (((DAT_805015c0 & 0x40) != 0) && (DAT_805015d8 == 0x13)) {
        return *(int *)(DAT_806128c8 + 0x10);
    }
    return 0;
}

// 0x80229f1c - Are both runners still active? Returns 1 only when neither
// SPRG nor SPR2 has its "eliminated" marker (+0x1cc > 0.0).
int worm_sprg_both_active(void)
{
    int sprg = FUN_8023c32c(0x53505247);   // 'SPRG'
    int spr2 = FUN_8023c32c(0x53505232);   // 'SPR2'
    if ((FLOAT_80618594 < *(float *)(sprg + 0x1cc)) ||
        (FLOAT_80618594 < *(float *)(spr2 + 0x1cc))) {
        return 0;
    }
    return 1;
}

// 0x80229f8c - Push the checkpoint HUD message. 'SPRG' checkpoints use the
// pickup channel, everything else uses the objective channel; both use string
// 0x15ef and the value at (+8) of the checkpoint record.
void worm_sprg_hud_pickup(int entity, int rec)
{
    if (*(int *)(entity + 0x60) == 0x53505247) {
        int str = get_string_by_id(0x15ef);
        hud_text_submit_pickup(*(int *)(DAT_80611d74 + 4), str, *(int *)(rec + 8));
    } else {
        int str = get_string_by_id(0x15ef);
        hud_text_submit_objective(*(int *)(DAT_80611d74 + 4), str, *(int *)(rec + 8));
    }
}

// 0x8022a010 - Choose which runner (SPRG or SPR2) is the current target. If the
// stored target (+4) has been eliminated it is cleared; then if no valid target
// remains and at least one runner is alive, an rng roll (weighted by which are
// alive) picks the new target, stamps its 4CC at +0x9dc, resolves its soundpad
// route zone into +0x684, and resets the per-target counters +0x9a8/+0x9ac.
void worm_sprg_select_runner(void)
{
    int obj = (int)__worm_ctx_current();
    int sprg = FUN_8023c32c(0x53505247);
    int spr2 = FUN_8023c32c(0x53505232);
    int cur = *(int *)(obj + 4);
    if (cur != 0) {
        if ((cur != sprg) || (FLOAT_80618594 < *(float *)(sprg + 0x1cc))) {
            if ((cur == spr2) && (*(float *)(spr2 + 0x1cc) <= FLOAT_80618594)) {
                *(int *)(obj + 4) = 0;
            }
        } else {
            *(int *)(obj + 4) = 0;
        }
    }
    cur = *(int *)(obj + 4);
    if (((cur == 0) || ((cur != sprg) && (cur != spr2))) &&
        ((FLOAT_80618594 < *(float *)(sprg + 0x1cc)) ||
         (FLOAT_80618594 < *(float *)(spr2 + 0x1cc)))) {
        int roll;
        unsigned int r;
        if (FLOAT_80618594 < *(float *)(sprg + 0x1cc)) {
            if (FLOAT_80618594 < *(float *)(spr2 + 0x1cc)) {
                r = rng_counter_tick();
                roll = (int)(FLOAT_806185e8 *
                             ((float)((double)CONCAT44(0x43300000, r) - DOUBLE_806185f8) / FLOAT_806185ec)
                             + FLOAT_806185d8);
            } else {
                r = rng_counter_tick();
                roll = (int)(FLOAT_806185d8 +
                             (float)((double)CONCAT44(0x43300000, r) - DOUBLE_806185f8) / FLOAT_806185ec);
            }
        } else {
            r = rng_counter_tick();
            roll = (int)(FLOAT_806185d8 +
                         (float)((double)CONCAT44(0x43300000, r) - DOUBLE_806185f8) / FLOAT_806185ec) + 2;
        }
        if (roll < 2) {
            *(int *)(obj + 4) = sprg;
            *(int *)(obj + 0x9dc) = 0x53505247;
        } else {
            *(int *)(obj + 4) = spr2;
            *(int *)(obj + 0x9dc) = 0x53505232;
        }
        int zone = soundpad_find_zone_by_id(*(int *)(&DAT_804619d8 + roll * 4));
        if (zone < 0) {
            *(int *)(obj + 0x684) =
                *(int *)(*(int *)(*(int *)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        } else {
            *(int *)(obj + 0x684) = soundpad_find_zone_by_id(*(int *)(&DAT_804619d8 + roll * 4));
        }
        *(int *)(obj + 0x9ac) = 0;
        *(int *)(obj + 0x9a8) = 0;
    }
    FUN_8038273c();
}

// 0x8022a25c - Award a checkpoint pickup: +0x19 to the global tally
// DAT_806128f4 and (unless the entity is already dead, +0x20==1) latch its
// "scored" flag at +0x9d0.
void worm_sprg_award_pickup(int entity, int rec)
{
    (void)entity;
    DAT_806128f4 = DAT_806128f4 + 0x19;
    if (*(int *)(rec + 0x20) == 1) {
        return;
    }
    *(int *)(rec + 0x9d0) = 1;
}

// 0x8022a280 - Find the next unclaimed checkpoint ('N' entity with the ready
// flag bit 0x10000000 and behaviour 1), mark it reached (+0x1cc = timeout),
// force behaviour 2, and bump the object's reached count (+0xc).
void worm_sprg_activate_next(void)
{
    int  idx = DAT_80612610 << 2;
    int  remaining = DAT_80612098 - DAT_80612610;
    int  found = 0;
    int *ent = 0;
    if (DAT_80612610 < DAT_80612098) {
        do {
            ent = *(int **)((int)&DAT_80588610 + idx);
            if (*(short *)((int)ent + 0x48) == 0x4e) {
                unsigned int flags = (DAT_805015bd == '\n') ? (unsigned int)ent[5]
                                                            : (unsigned int)ent[7];
                if (((flags & 0x10000000) != 0) && (ent[8] == 1)) {
                    found = *ent;
                    break;
                }
            }
            idx += 4;
            remaining += -1;
        } while (remaining != 0);
    }
    if (found != 0) {
        *(float *)(found + 0x1cc) = FLOAT_806185a8;
        if (ent[8] != 2) {
            ent[0x274] = 2;
        }
        *(int *)(DAT_806128c8 + 0xc) = *(int *)(DAT_806128c8 + 0xc) + 1;
    }
}

// 0x8022a358 - SPRG relay MASTER per-frame tick. On first frame it wires the
// local player to the SPRG runner + a split-screen SPR2 chase camera (screen
// effect slots, viewport rect/depth) and marks all 'N' checkpoints armed
// (+0x20=1). Each frame it scans checkpoints, awarding those the active runner
// reaches (worm_sprg_award_pickup), decrements the phase timer +4 by the
// per-frame rate, and on timer/threshold rollover advances the phase +8 with
// HUD messages 0x680/0x681 and cue 0xd69, else runs worm_sprg_activate_next.
void worm_sprg_master_tick(unsigned long long param_1, unsigned long long param_2, double param_3,
                           unsigned long long param_4, unsigned long long param_5,
                           unsigned long long param_6, unsigned long long param_7,
                           unsigned long long param_8)
{
    int   soundParams[12];
    int  *obj;
    int   sprg, spr2, node, camLerpSrc;
    int   aliveCount = 0;

    FUN_803826e8();
    double rate = (double)(FLOAT_806108ec *
                  (float)((double)FLOAT_80618590 /
                          (double)(float)((double)CONCAT44(0x43300000,
                                          (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_806185a0)));

    if (*(int *)DAT_806128c8 == 0) {
        // ---- first-frame setup: bind SPRG runner + SPR2 chase camera --------
        int *rec = (int *)FUN_8023cc28(0x53505247);
        int lvl = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);
        player_set_state(DAT_80611d74, 0x10000);
        *(unsigned int *)(DAT_80611d74 + 0xc) = *(unsigned int *)(DAT_80611d74 + 0xc) | 0x80;
        FUN_80240710(rec);
        if (*(int *)(DAT_80611d74 + 8) != 0) {
            camera_set_mode_by_index(*(int *)(DAT_80611d74 + 8), 2, *rec);
        }
        if (*(int *)(lvl + 0x94) == *(int *)(lvl + 0x98)) {
            *(int *)(lvl + 0x98) = *(int *)(lvl + 0x94);
        }
        *(int *)(lvl + 0x94) = 0;
        if (*(int *)(DAT_80611d74 + 8) != 0) {
            node = FUN_8023d2c0(rec[3]);
            double lerp = (double)*(float *)(node + 0x2c);
            if (lerp <= (double)FLOAT_80618594) {
                lerp = player_get_camera_lerp_factor(DAT_80611d54);
            }
            *(float *)(*(int *)(DAT_80611d74 + 8) + 0x388) = (float)lerp;
            *(float *)(*(int *)(DAT_80611d74 + 8) + 900)   = (float)lerp;
        }
        *(int *)(lvl + 0xc14) = *rec;
        *(float *)(*rec + 0x1cc) = FLOAT_806185ac;
        *(int *)(*rec + 0x268) = screen_effect_alloc_slot();
        screen_effect_init_slot(*(int *)(*rec + 0x268), *rec);
        rec[0x36] = *(int *)(DAT_80611d74 + 0x14);
        rec[0x37] = (int)FLOAT_80618594;
        *(unsigned int *)(*rec + 0x28) = *(unsigned int *)(*rec + 0x28) | 0x4000000;
        ((int *)DAT_806128c8)[4] = player_slot_alloc((double)FLOAT_80618590, param_3, (double)param_4,
                                                     param_5, param_6, param_7, param_8, 0, 0);
        if (((int *)DAT_806128c8)[4] != 0) {
            int *rec2 = (int *)FUN_8023cc28(0x53505232);
            int lvl2 = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);
            int node2 = FUN_8023d2c0(rec2[3]);
            double lerp2 = (double)*(float *)(node2 + 0x2c);
            if (lerp2 <= (double)FLOAT_80618594) {
                lerp2 = player_get_camera_lerp_factor(DAT_80611d54);
            }
            viewport_set_depth_params((double)(FLOAT_806186e0 + FLOAT_8060fad0),
                                      -(double)(FLOAT_806186e0 + FLOAT_8060fad0),
                                      (double)FLOAT_806186e4, ((int *)DAT_806128c8)[4] + 0x224);
            unsigned int w = (unsigned int)(DAT_806109c4 - 1);
            int half = ((int)w >> 1) + (((int)w < 0) && ((w & 1) != 0) ? 1 : 0);
            viewport_set_rect((int *)(((int *)DAT_806128c8)[4] + 0x224), half + 1, 0,
                              (int)w, DAT_806109c8 - 1);
            FUN_80240710(rec2);
            camera_set_mode_by_index(((int *)DAT_806128c8)[4], 2, *rec2);
            *(float *)(((int *)DAT_806128c8)[4] + 0x388) = (float)lerp2;
            *(float *)(((int *)DAT_806128c8)[4] + 900)   = (float)lerp2;
            *(int *)(lvl2 + 0xc18) = *rec2;
            *(float *)(*rec2 + 0x1cc) = FLOAT_806185ac;
            *(int *)(*rec2 + 0x268) = screen_effect_alloc_slot();
            screen_effect_init_slot(*(int *)(*rec2 + 0x268), *rec2);
            rec2[0x36] = *(int *)(DAT_80611d74 + 0x14);
            rec2[0x37] = (int)FLOAT_80618594;
            *(unsigned int *)(*rec2 + 0x28) = *(unsigned int *)(*rec2 + 0x28) | 0x4000000;
        }
        // arm all 'N' checkpoint entities
        int e = DAT_80612610 << 2;
        for (int i = DAT_80612610; i < DAT_80612098; i++) {
            int ent = *(int *)((int)&DAT_80588610 + e);
            if (*(short *)(ent + 0x48) == 0x4e) {
                *(int *)(ent + 0x20) = 1;
            }
            e += 4;
        }
        *(int *)DAT_806128c8 = 1;
    }

    // ---- per-frame: scan checkpoints, award those the runner has reached ----
    int e = DAT_80612610 << 2;
    for (int i = DAT_80612610; i < DAT_80612098; i++) {
        int *cp = *(int **)((int)&DAT_80588610 + e);
        if (*(short *)((int)cp + 0x48) == 0x4e) {
            unsigned int flags = (DAT_805015bd == '\n') ? (unsigned int)cp[5]
                                                        : (unsigned int)cp[7];
            if (((flags & 0x10000000) == 0) || (cp[8] != 1)) {
                aliveCount++;
                if (*(float *)(*cp + 0x1cc) <= FLOAT_80618594) {
                    unsigned int f2 = (DAT_805015bd == '\n') ? (unsigned int)cp[5]
                                                             : (unsigned int)cp[7];
                    if ((f2 & 1) == 0) {
                        worm_sprg_award_pickup(*cp, (int)cp);
                    }
                }
            }
        }
        e += 4;
    }

    // ---- phase timer / countdown table --------------------------------------
    obj = (int *)DAT_806128c8;
    int phase = obj[2];
    int threshold; float step;
    if (phase < 4) {
        threshold = *(int *)(&DAT_804619e8 + phase * 0xc);
        step      = *(float *)(&DAT_804619ec + phase * 0xc);
    } else {
        threshold = DAT_80461a0c + (phase - 4) * 3;
        step      = FLOAT_806185e8;
    }
    obj[1] = (int)(float)((double)(float)obj[1] - rate);
    double timer = (double)(float)obj[1];
    if (((double)FLOAT_80618594 < timer) || (threshold <= obj[3])) {
        if ((threshold <= obj[3]) && (aliveCount == 0)) {
            int s = get_string_by_id(0x680);
            int *t = (int *)debug_log_formatted(timer, 0, param_3, param_4, param_5, param_6,
                                                param_7, param_8, s, obj[2] + 1, (int)obj);
            hud_text_submit(*(int *)(DAT_80611d74 + 4), 0xb, (uint *)t);
            sound_init_play_params((int)soundParams);
            soundParams[0] = 0xd69;
            sound_play_request(soundParams);
            obj[2] = obj[2] + 1;
            s = get_string_by_id(0x681);
            t = (int *)debug_log_formatted(0, 0, param_3, param_4, param_5, param_6,
                                           param_7, param_8, s, obj[2] + 1, (int)obj);
            hud_text_submit(*(int *)(DAT_80611d74 + 4), 0xb, (uint *)t);
            obj[3] = 0;
        }
    } else {
        worm_sprg_activate_next();
        obj[1] = (int)(float)(double)step;
    }
    FUN_80382734();
}

// 0x8022a8a0 - Mission-mode dispatch trampoline: forward the current extra-mode
// id to the per-mode master handler.
void worm_mission_mode_dispatch(void)
{
    (void)worm_mode_get_object(DAT_805015d8);
}

// ===========================================================================
// batch_689: mission-mode challenge results/scoring HUD subsystem. These read
// the per-mode objective descriptor list (resolved via FUN_8022d9a4) to find
// the "target" (bit 0x80) and "best" (bit 0x100) descriptors, format the
// current/target/best values into HUD strings by stat type, and evaluate
// whether the player's score passed the challenge threshold.
//
// Address: 0x8022a8cc | Name: worm_results_get_score        | 2026-09-09
// Address: 0x8022a8d4 | Name: worm_results_get_prev_score   | 2026-09-09
// Address: 0x8022a8dc | Name: worm_results_format_line      | 2026-09-09
// Address: 0x8022aa8c | Name: worm_results_mark_complete    | 2026-09-09
// Address: 0x8022ac04 | Name: worm_results_check_challenge  | 2026-09-09
// Address: 0x8022ae0c | Name: worm_results_format_stat      | 2026-09-09
// Address: 0x8022b078 | Name: worm_results_format_target    | 2026-09-09
// Address: 0x8022b0c4 | Name: worm_results_format_best      | 2026-09-09
// Address: 0x8022b110 | Name: worm_mode_find_target_desc    | 2026-09-09
// Address: 0x8022b16c | Name: worm_mode_find_best_desc      | 2026-09-09
// ---------------------------------------------------------------------------
// File-local externs new to batch_689.
extern int   DAT_806128f0;      // secondary/previous score tally
extern uint  DAT_805015c4;      // session challenge flag word
extern float FLOAT_806186e8;    // time->units divisor (60fps=NTSC? default)
extern float FLOAT_806186ec;    // time->units divisor (0x3c/50Hz path)
extern float FLOAT_806186f0;    // accuracy percentage scale
extern float FLOAT_806186f4;    // accuracy denominator
extern int   DAT_80610edc;      // fmt "%s / %s" (current / target)
extern int   s__d__s_80610ee4;  // fmt "%d %s"
extern int   s__s___1f___80461c5c; // fmt "%s %.1f%%"
extern int   stats_get_challenge_record(int a, int mode);   // -> record base
extern void  hud_format_rank_string(unsigned long long p1, unsigned long long p2,
                                    unsigned long long p3, unsigned long long p4,
                                    unsigned long long p5, unsigned long long p6,
                                    unsigned long long p7, unsigned long long p8,
                                    int a, uint value, int b, int c,
                                    int d, int e, int f, int g);
extern void  time_format_long(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                             unsigned long long p4, unsigned long long p5, unsigned long long p6,
                             unsigned long long p7, unsigned long long p8);
// Value-returning aliases for ROM entries whose returns were dropped project-wide
// (leading __ so gen_externs skips them). FUN_8022d9a4 returns the mode object
// base; worm_mission_mode_dispatch (0x8022a8a0) tail-calls it and yields the same.
static unsigned char *(*const __worm_mode_object)(int) =
    (unsigned char *(*)(int))0x8022d9a4;
static int (*const __worm_mode_object_current)(void) =
    (int (*)(void))0x8022a8a0;

// Forward declarations (defined later in this TU; referenced earlier).
int worm_results_get_score(void);
int worm_results_get_prev_score(void);
void worm_results_mark_complete(void);
unsigned short *worm_mode_find_target_desc(int mode);
unsigned short *worm_mode_find_best_desc(int mode);
void worm_results_format_stat(unsigned long long, unsigned long long, unsigned long long,
                              unsigned long long, unsigned long long, unsigned long long,
                              unsigned long long, unsigned long long, int, uint, int, int,
                              int, int, int, int);
void worm_results_format_target(unsigned long long, unsigned long long, unsigned long long,
                                unsigned long long, unsigned long long, unsigned long long,
                                unsigned long long, unsigned long long, int, uint, int, int,
                                int, int, int, int);
void worm_results_format_best(unsigned long long, unsigned long long, unsigned long long,
                              unsigned long long, unsigned long long, unsigned long long,
                              unsigned long long, unsigned long long, int, uint, int, int,
                              int, int, int, int);

// 0x8022a8cc - Return the current challenge score (global tally DAT_806128f4).
int worm_results_get_score(void)
{
    return DAT_806128f4;
}

// 0x8022a8d4 - Return the previous/secondary challenge score (DAT_806128f0).
int worm_results_get_prev_score(void)
{
    return DAT_806128f0;
}

// 0x8022a8dc - Format the results line "<current> / <best>". Reads the challenge
// record for the current mode, converts the raw score to display units (time
// modes divide by the 50/60 Hz divisor), formats the target via
// worm_results_format_target and the best via worm_results_format_best, then
// composes them with fmt DAT_80610edc.
void worm_results_format_line(unsigned long long param_1, double param_2, unsigned long long param_3,
                              unsigned long long param_4, unsigned long long param_5,
                              unsigned long long param_6, unsigned long long param_7,
                              unsigned long long param_8, int param_11, int param_12,
                              int param_13, int param_14, int param_15, int param_16)
{
    int   targetBuf[32];
    int   bestBuf[32];
    unsigned char *rec;
    unsigned short *desc;
    int   mode;
    unsigned int value;

    mode = __save_gpr_28();
    rec = (unsigned char *)stats_get_challenge_record(0, mode);
    value = *(unsigned int *)(rec + 4);
    desc = worm_mode_find_target_desc(mode);
    if (desc[1] == 2) {
        double t = (double)(float)((double)CONCAT44(0x43300000, value ^ 0x80000000) - DOUBLE_806185a0);
        value = (DAT_806109c0 == 0x3c) ? (unsigned int)(t / (double)FLOAT_806186ec)
                                       : (unsigned int)(t / (double)FLOAT_806186e8);
    }
    // format_target writes into its own scratch and emits via DAT_80610ebc; the
    // ROM's leftover r3 (piVar4) is consumed by FUN_8038b510 into targetBuf.
    worm_results_format_target(param_2, param_2, param_3, param_4, param_5,
                               param_6, param_7, param_8, mode, value,
                               param_11, param_12, param_13, param_14, param_15, param_16);
    desc = worm_mode_find_best_desc(mode);
    if (desc != 0) {
        value = *(unsigned int *)(rec + 8);
        FUN_8038b510(bestBuf, targetBuf);
        if (desc[1] == 2) {
            double t = (double)(float)((double)CONCAT44(0x43300000, value ^ 0x80000000) - DOUBLE_806185a0);
            value = (DAT_806109c0 == 0x3c) ? (unsigned int)(t / (double)FLOAT_806186ec)
                                           : (unsigned int)(t / (double)FLOAT_806186e8);
        }
        worm_results_format_best(param_2, param_2, param_3, param_4, param_5,
                                 param_6, param_7, param_8, mode, value,
                                 param_11, param_12, param_13, param_14, param_15, param_16);
        unsigned long long w = FUN_8038b510(targetBuf, bestBuf);
        debug_log_formatted((double)w, param_2, param_3, param_4, param_5, param_6, param_7,
                            param_8, &DAT_80610edc, (unsigned long long)(int)targetBuf,
                            (unsigned long long)(int)bestBuf, param_12, param_13, param_14,
                            param_15, param_16);
    }
    __restore_gpr_28();
}

// 0x8022aa8c - Mark the current-mode challenge complete: if the mode object is
// not already flagged done (obj +0x18 bit 8) or the session is in a forced
// state (DAT_805015c4 & 0xc00000), re-resolve the best descriptor and, when the
// record's completion field (+0x10) is set, refresh it.
void worm_results_mark_complete(void)
{
    __save_gpr_28();
    int obj = __worm_mode_object_current();
    if (((*(unsigned int *)(obj + 0x18) & 8) == 0) || ((DAT_805015c4 & 0xc00000) != 0)) {
        unsigned char *rec = (unsigned char *)stats_get_challenge_record(0, DAT_805015d8);
        unsigned short *desc = worm_mode_find_best_desc(DAT_805015d8);
        if ((desc != 0) && (worm_results_get_prev_score(), *(int *)(rec + 0x10) != 0)) {
            worm_mode_find_best_desc(DAT_805015d8);
        }
    }
    __restore_gpr_28();
}

// 0x8022ac04 - Evaluate whether the player passed the current challenge. Reads
// the record + target descriptor, converts the target threshold to display
// units, then compares the current score against it per the descriptor's
// comparison operator (+6): op 3 = greater, op<2 = at-least (>=), op 2 = equal,
// op 5 = always, op<5 = at-most (<=). On success calls mark_complete.
void worm_results_check_challenge(void)
{
    int *rec;
    unsigned int cur, target;
    unsigned short *desc;
    int op;

    __save_gpr_28();
    rec = (int *)stats_get_challenge_record(0, DAT_805015d8);
    if ((DAT_805015c4 & 0x8000) != 0) {
        cur = worm_results_get_score();
        if (*rec == 0) {
            worm_results_mark_complete();
        } else {
            target = rec[1];
            desc = worm_mode_find_target_desc(DAT_805015d8);
            op = *(int *)(desc + 6);
            if (desc[1] == 2) {
                double t = (double)(float)((double)CONCAT44(0x43300000, target ^ 0x80000000) - DOUBLE_806185a0);
                target = (DAT_806109c0 == 0x3c) ? (unsigned int)(t / (double)FLOAT_806186ec)
                                                : (unsigned int)(t / (double)FLOAT_806186e8);
            }
            if (op == 3) {
                if ((int)target < (int)cur) worm_results_mark_complete();
            } else if (op < 3) {
                if (op < 2) {
                    if ((-1 < op) && ((int)cur < (int)target)) worm_results_mark_complete();
                } else if (cur == target) {
                    worm_results_mark_complete();
                }
            } else if (op == 5) {
                worm_results_mark_complete();
            } else if ((op < 5) && ((int)target <= (int)cur)) {
                worm_results_mark_complete();
            }
        }
    }
    __restore_gpr_28();
}

// 0x8022ae0c - Format a single stat value into a HUD string by its display type
// (param_11). Types include: default counter, rank string, long time, accuracy
// percentage, and a family of "%d <label>" forms selecting different label
// strings (0x655..0x66d) by type.
void worm_results_format_stat(unsigned long long param_1, unsigned long long param_2,
                              unsigned long long param_3, unsigned long long param_4,
                              unsigned long long param_5, unsigned long long param_6,
                              unsigned long long param_7, unsigned long long param_8,
                              int outBuf, uint value, int type, int param_12,
                              int param_13, int param_14, int param_15, int param_16)
{
    int str;
    switch (type) {
    default:
        FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     outBuf, (int *)&DAT_80610ec0, value, (int *)0x80460000, param_13,
                     param_14, param_15, param_16);
        break;
    case 1:
        hud_format_rank_string(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                               param_8, outBuf, value, 0, 2, param_13, param_14, param_15, param_16);
        break;
    case 2:
        time_format_long(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
        break;
    case 3:
        str = get_string_by_id(0x655);
        FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     outBuf, &s__d__s_80610ee4, value, (int *)str, param_13, param_14,
                     param_15, param_16);
        break;
    case 4:
        str = get_string_by_id(0x656);
        FUN_80387fb8((double)(float)((double)FLOAT_806186f0 *
                     (double)((float)((double)CONCAT44(0x43300000, value ^ 0x80000000) -
                                      DOUBLE_806185a0) / FLOAT_806186f4)),
                     DOUBLE_806185a0, (double)FLOAT_806186f0, param_4, param_5, param_6, param_7,
                     param_8, outBuf, &s__s___1f___80461c5c, str, (int *)0x80460000,
                     param_13, param_14, param_15, param_16);
        break;
    case 5:
    case 0xd:
        str = get_string_by_id((value == 1) ? 0x668 : 0x667);
        FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     outBuf, &s__d__s_80610ee4, value, (int *)str, param_13, param_14,
                     param_15, param_16);
        break;
    case 7: {
        double v = (double)(float)((double)CONCAT44(0x43300000, value ^ 0x80000000) - DOUBLE_806185a0);
        FUN_80387fb8(v, DOUBLE_806185a0, param_3, param_4, param_5, param_6, param_7, param_8,
                     outBuf, (int *)&DAT_80610ec0, (int)(v / (double)FLOAT_806186f4),
                     (int *)0x80460000, param_13, param_14, param_15, param_16);
        break;
    }
    case 8:
        str = get_string_by_id(0x662);
        FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     outBuf, &s__d__s_80610ee4, value, (int *)str, param_13, param_14,
                     param_15, param_16);
        break;
    case 9:
        str = get_string_by_id(0x661);
        FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     outBuf, &s__d__s_80610ee4, value, (int *)str, param_13, param_14,
                     param_15, param_16);
        break;
    case 10:
        str = get_string_by_id(0x660);
        FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     outBuf, &s__d__s_80610ee4, value, (int *)str, param_13, param_14,
                     param_15, param_16);
        break;
    case 0xb:
        str = get_string_by_id(0x65f);
        FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     outBuf, &s__d__s_80610ee4, value, (int *)str, param_13, param_14,
                     param_15, param_16);
        break;
    case 0xc:
        str = get_string_by_id(0x65e);
        FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     outBuf, &s__d__s_80610ee4, value, (int *)str, param_13, param_14,
                     param_15, param_16);
        break;
    case 0xe:
        str = get_string_by_id((value == 1) ? 0x66b : 0x66a);
        FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     outBuf, &s__d__s_80610ee4, value, (int *)str, param_13, param_14,
                     param_15, param_16);
        break;
    case 0x10:
        str = get_string_by_id(0x66d);
        FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     outBuf, &s__d__s_80610ee4, value, (int *)str, param_13, param_14,
                     param_15, param_16);
        break;
    }
}

// 0x8022b078 - Format the target descriptor's value: look up the target
// descriptor for the mode, format its value by type (worm_results_format_stat),
// and emit via fmt DAT_80610ebc into the caller's buffer.
void worm_results_format_target(unsigned long long param_1, unsigned long long param_2,
                                unsigned long long param_3, unsigned long long param_4,
                                unsigned long long param_5, unsigned long long param_6,
                                unsigned long long param_7, unsigned long long param_8,
                                int mode, uint value, int param_11, int param_12,
                                int param_13, int param_14, int param_15, int param_16)
{
    unsigned char scratch[260];
    unsigned short *desc = worm_mode_find_target_desc(mode);
    unsigned int type = (unsigned int)desc[1];
    worm_results_format_stat(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             (int)scratch, value, type, param_12, param_13, param_14, param_15,
                             param_16);
    debug_log_formatted(0, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                        &DAT_80610ebc, (unsigned long long)(int)scratch, type, param_12, param_13,
                        param_14, param_15, param_16);
}

// 0x8022b0c4 - Same as worm_results_format_target but for the best descriptor.
void worm_results_format_best(unsigned long long param_1, unsigned long long param_2,
                              unsigned long long param_3, unsigned long long param_4,
                              unsigned long long param_5, unsigned long long param_6,
                              unsigned long long param_7, unsigned long long param_8,
                              int mode, uint value, int param_11, int param_12,
                              int param_13, int param_14, int param_15, int param_16)
{
    unsigned char scratch[260];
    unsigned short *desc = worm_mode_find_best_desc(mode);
    unsigned int type = (unsigned int)desc[1];
    worm_results_format_stat(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             (int)scratch, value, type, param_12, param_13, param_14, param_15,
                             param_16);
    debug_log_formatted(0, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                        &DAT_80610ebc, (unsigned long long)(int)scratch, type, param_12, param_13,
                        param_14, param_15, param_16);
}

// 0x8022b110 - Find the "target" objective descriptor for a mode: scan the
// mode object's descriptor list (0x14 stride at obj+0x1c, count obj+0x20) for
// the first with flag bit 0x80 set; returns the descriptor or NULL.
unsigned short *worm_mode_find_target_desc(int mode)
{
    unsigned char *obj = __worm_mode_object(mode);
    int count = *(int *)(obj + 0x20);
    int off = 0;
    while (count > 0) {
        if ((*(unsigned short *)(*(int *)(obj + 0x1c) + off) & 0x80) != 0) {
            return (unsigned short *)(*(int *)(obj + 0x1c) + off);
        }
        off += 0x14;
        count += -1;
    }
    return 0;
}

// 0x8022b16c - Find the "best" objective descriptor for a mode: same scan but
// for flag bit 0x100.
unsigned short *worm_mode_find_best_desc(int mode)
{
    unsigned char *obj = __worm_mode_object(mode);
    int count = *(int *)(obj + 0x20);
    int off = 0;
    while (count > 0) {
        if ((*(unsigned short *)(*(int *)(obj + 0x1c) + off) & 0x100) != 0) {
            return (unsigned short *)(*(int *)(obj + 0x1c) + off);
        }
        off += 0x14;
        count += -1;
    }
    return 0;
}

// ===========================================================================
// Batch: batch_690
// The mission-mode preload/reset/spawn dispatch layer plus the challenge
// results readout helpers. FUN_8022b1c8 fans out per-mode preload to the
// worm_*_obj_* allocators decompiled in prior batches; FUN_8022b2a4 walks all
// 0x15 modes to preload every possible objective object; FUN_8022b310 resets
// objective state and fans out per-mode reset; FUN_8022b424 spawns entities for
// the ASL/plat mode. The remaining functions read/format the current mode
// object's target descriptor list (used by the results HUD).
// Source: TimeSplitters: Future Perfect (GameCube, main.dol)
// ===========================================================================

// --- Additional file-local externs for batch_690 --------------------------
// Per-player flag-mask table: uint[] indexed by (localPlayer*4) selecting which
// descriptor flag bit identifies "this player's" target row.
extern int  DAT_80461d54;
// Score/time accessor: given an objective descriptor row pointer, return its
// signed score/time value.
extern int  FUN_8022d938(int descRow);
// Session objective-mode bitmask query (returns one of 0x100000..0x800000).
extern int  FUN_8022c5bc(void);
// Preload helper that scans all sprite/flag masks and loads matching assets.
extern void sprite_load_by_flag_mask(unsigned long long p1, unsigned long long p2,
                                     unsigned long long p3, unsigned long long p4,
                                     unsigned long long p5, unsigned long long p6,
                                     unsigned long long p7, unsigned long long p8);

// Switch jump table backing FUN_8022b1c8 (referenced as a raw label operand by
// the decompiler; it is passed through as the mode-descriptor arg 9). Declared
// as a byte so &-of yields a usable pointer without a competing definition.
extern unsigned char switchdataD_80461cac;

// "%s: %s / %s" style compound format literal used by the objective readout.
extern char s__s__s__s_80461d67[];

// ---------------------------------------------------------------------------

// 0x8022b1c8 - Per-mode objective-object PRELOAD dispatch. When the session has
// the mission-objective flag (DAT_805015c0 & 0x40), switch on the current mode
// index (DAT_805015d8) and forward to that mode's allocate/preload routine. The
// register-spill args produced by the decompiler are preserved as an opaque
// 16-arg passthrough so each callee keeps the ABI shape it was decompiled with;
// arg 9 carries the mode-descriptor jump-table base.
void worm_mode_preload_dispatch(unsigned long long param_1, unsigned long long param_2,
                                unsigned long long param_3, unsigned long long param_4,
                                unsigned long long param_5, unsigned long long param_6,
                                unsigned long long param_7, unsigned long long param_8,
                                int param_9, int param_10, int param_11, int param_12,
                                int param_13, int param_14, int param_15, int param_16)
{
    // The register-spill args (param_9..16, incl. the &switchdataD_80461cac jump
    // table base) are ROM-ABI passthrough only; every callee below reads the
    // active mode object from globals and takes no arguments.
    (void)param_1; (void)param_2; (void)param_3; (void)param_4;
    (void)param_5; (void)param_6; (void)param_7; (void)param_8;
    (void)param_9; (void)param_10; (void)param_11; (void)param_12;
    (void)param_13; (void)param_14; (void)param_15; (void)param_16;

    DAT_806128b8 = 0;
    if ((DAT_805015c0 & 0x40) == 0) {
        return;
    }
    switch (DAT_805015d8) {
    case 0:
    case 1:
    case 2:
        worm_cdh1_obj_spawn_init();
        break;
    case 3:
    case 4:
    case 5:
        worm_czs_obj_spawn_init();
        break;
    case 6:
    case 7:
    case 8:
        worm_czs2_obj_preload_init();
        break;
    case 0xc:
    case 0xd:
    case 0xe:
        sound_clear_bank_ref(0xd30);
        sound_clear_bank_ref(0x13f0);
        sound_clear_bank_ref(0x13f1);
        sound_clear_bank_ref(0x13f2);
        sound_clear_bank_ref(0x13f3);
        sound_clear_bank_ref(0x13f4);
        sound_clear_bank_ref(0x13f5);
        sound_clear_bank_ref(0x81);
        break;
    case 0xf:
    case 0x10:
        worm_plat_obj_preload_init();
        break;
    case 0x11:
        worm_upl_obj_preload();
        break;
    case 0x12:
        worm_netb_obj_alloc();
        break;
    case 0x13:
        worm_sprg_obj_alloc();
        break;
    case 0x14:
        worm_prop_preload_resources();
        break;
    default:
        break;
    }
}

// 0x8022b2a4 - Preload ALL mode objects: temporarily force the objective flag
// on, iterate every mode index 0..0x14 running the per-mode preload dispatch,
// restore the previous flag/mode, then run the sprite/flag-mask asset preload.
// The decompiler's register-pair artifacts collapse to plain iteration.
void worm_mode_preload_all(unsigned long long param_1, unsigned long long param_2,
                           unsigned long long param_3, unsigned long long param_4,
                           unsigned long long param_5, unsigned long long param_6,
                           unsigned long long param_7, unsigned long long param_8,
                           int param_9, int param_10, int param_11, int param_12,
                           int param_13, int param_14, int param_15, int param_16)
{
    int savedMode = DAT_805015d8;
    unsigned int savedFlags = (unsigned int)DAT_805015c0;

    DAT_805015c0 = DAT_805015c0 | 0x40;
    for (int mode = 0; mode < 0x15; mode = mode + 1) {
        DAT_805015d8 = mode;
        worm_mode_preload_dispatch(param_1, param_2, param_3, param_4, param_5, param_6,
                                   param_7, param_8, param_9, param_10, param_11, param_12,
                                   param_13, param_14, param_15, param_16);
    }
    DAT_805015c0 = (int)savedFlags;
    DAT_805015d8 = savedMode;

    sprite_load_by_flag_mask(param_1, param_2, param_3, param_4, param_5, param_6,
                             param_7, param_8);
}

// 0x8022b310 - Per-mode objective RESET dispatch. Clears the shared objective
// score/state globals (score DAT_806128f4, prev DAT_806128f0, the two 2-entry
// per-player target arrays DAT_806128e8/DAT_806128e0, and phase counters), sets
// the "results pending" session bit (DAT_805015c4 |= 0x4000), then fans out to
// the current mode's reset/init routine.
void worm_mode_reset_dispatch(unsigned long long param_1, double param_2, double param_3,
                              unsigned long long param_4, unsigned long long param_5,
                              unsigned long long param_6, unsigned long long param_7,
                              unsigned long long param_8)
{
    DAT_806128f4 = 0;
    DAT_805015c4 = DAT_805015c4 | 0x4000;
    DAT_806128f0 = 0;
    DAT_806128dc = 0;
    DAT_806128d8 = 0;
    for (int i = 0; i < 8; i = i + 4) {
        *(int *)((int)&DAT_806128e8 + i) = 0;
        *(int *)((int)&DAT_806128e0 + i) = 0;
    }

    switch (DAT_805015d8) {
    case 0:
    case 1:
    case 2:
        worm_cdh1_obj_reset();
        break;
    case 3:
    case 4:
    case 5:
        worm_czs_obj_reset();
        break;
    case 6:
    case 7:
    case 8:
        worm_czs2_obj_spawn_entities(param_1, param_2, param_3, param_4, param_5, param_6,
                                     param_7, param_8);
        break;
    case 0xc:
        worm_plat_hud_marker_submit(0x3e9, 0x20, 1);
        break;
    case 0xd:
        worm_plat_hud_marker_submit(0x3ea, 0x12, 1);
        break;
    case 0xe:
        worm_plat_hud_marker_submit(0x400, 1, 1);
        break;
    case 0xf:
        worm_asl_obj_init();
        break;
    case 0x10:
        worm_asl_obj_reset(param_1, param_2, param_3, param_4, param_5, param_6,
                           param_7, param_8);
        break;
    case 0x11:
        worm_upl_obj_init();
        break;
    case 0x12:
        worm_netb_obj_reset();
        break;
    case 0x13:
        worm_sprg_obj_reset();
        break;
    case 0x14:
        worm_prop_obj_reset();
        break;
    default:
        break;
    }
}

// 0x8022b424 - Per-mode entity SPAWN dispatch: only mode 10 (the ASL/plat
// escort mode) spawns its scripted entities each activation.
void worm_mode_spawn_dispatch(unsigned long long param_1, double param_2, double param_3,
                              unsigned long long param_4, unsigned long long param_5,
                              unsigned long long param_6, unsigned long long param_7,
                              unsigned long long param_8)
{
    if (DAT_805015d8 == 10) {
        worm_plat_spawn_entities(param_1, param_2, param_3, param_4, param_5, param_6,
                                 param_7, param_8);
    }
}

// 0x8022b45c - Find the next active target descriptor for the local player and
// return its remaining value (row value minus this player's already-scored
// value from DAT_806128e0). Returns the remaining amount (>=0), 0 if already
// met, or -1 when no matching row / objective already complete.
int worm_mode_get_next_target_time(void)
{
    int obj = __worm_mode_object_current();
    if ((*(unsigned int *)(obj + 0x18) & 1) != 0) {
        return -1;
    }

    int count = *(int *)(obj + 0x20);
    int rowOff = 0;
    while (count > 0) {
        unsigned short *row = (unsigned short *)(*(int *)(obj + 0x1c) + rowOff);
        if ((*(int *)(row + 2) == 0) && ((*row & 0x41) != 0)) {
            int value = FUN_8022d938((int)row);
            int scored = *(int *)((int)&DAT_806128e0 + *(int *)(DAT_80611d74 + 4) * 4);
            if (value - scored >= 0) {
                return value - scored;
            }
            return 0;
        }
        rowOff = rowOff + 0x14;
        count = count - 1;
    }
    return -1;
}

// 0x8022b4f4 - Return the local player's currently-scored target value from the
// per-player array DAT_806128e0, or -1 when the objective is already complete
// (mode object flag +0x18 bit0 set).
int worm_mode_get_current_target(void)
{
    int obj = __worm_mode_object_current();
    if ((*(unsigned int *)(obj + 0x18) & 1) != 0) {
        return -1;
    }
    return *(int *)((int)&DAT_806128e0 + *(int *)(DAT_80611d74 + 4) * 4);
}

// 0x8022b53c - Count the entries in the current mode object's secondary
// descriptor list at (+0x6c): 0x10-stride rows terminated by a -1 first word.
int worm_mode_count_descriptors(void)
{
    int obj = __worm_mode_object_current();
    int list = *(int *)(obj + 0x6c);
    int count = 0;
    for (int off = 0; *(int *)(list + off) != -1; off = off + 0x10) {
        count = count + 1;
    }
    return count;
}

// 0x8022b580 - Format the objective readout line for the results HUD. Scans the
// current mode object's descriptor rows for the one matching this player's flag
// mask (DAT_80461d54[localPlayer]); a special "reference" row (flag 4) provides
// a comparison value. When the matched row and reference row share the same
// unit (word +2), a single value is formatted; otherwise both are formatted and
// combined via the "%s: %s / %s" template.
void worm_mode_format_objective_line(unsigned long long param_1, unsigned long long param_2,
                                     unsigned long long param_3, unsigned long long param_4,
                                     unsigned long long param_5, unsigned long long param_6,
                                     unsigned long long param_7, unsigned long long param_8)
{
    unsigned char lineBuf[256];
    unsigned char valBufA[256];
    unsigned char valBufB[256];
    unsigned char scratch[280];

    int obj = __worm_mode_object_current();
    int count = *(int *)(obj + 0x20);
    unsigned short *refRow = 0;

    // First pass: locate the reference row (flag bit 4).
    int off = 0;
    for (int i = count; i > 0; i = i - 1) {
        unsigned short *row = (unsigned short *)(*(int *)(obj + 0x1c) + off);
        if ((*row & 4) != 0) {
            refRow = row;
        }
        off = off + 0x14;
    }

    int localPlayer = *(int *)(DAT_80611d74 + 4);
    unsigned int mask = *(unsigned int *)((int)&DAT_80461d54 + localPlayer * 4);

    // Second pass: format the first row that matches this player's mask.
    off = 0;
    for (int i = count; i > 0; i = i - 1) {
        unsigned short *row = (unsigned short *)(*(int *)(obj + 0x1c) + off);
        if (((unsigned int)*row & mask) != 0) {
            unsigned int value = (unsigned int)FUN_8022d938((int)row);
            if ((refRow == 0) || (row[1] == refRow[1])) {
                unsigned int unit = (unsigned int)row[1];
                worm_results_format_stat(param_1, param_2, param_3, param_4, param_5, param_6,
                                         param_7, param_8, (int)scratch, value, unit,
                                         localPlayer, 0, 0, 0, 0);
                if (*(int *)(row + 4) == -3) {
                    int labelId = get_string_by_id(0x673);
                    debug_log_formatted((double)param_1, (double)param_2, (double)param_3,
                                        (double)param_4, param_5, param_6, param_7, param_8,
                                        (int)&DAT_80610edc, (int)scratch, (int *)labelId,
                                        localPlayer, 0, 0, 0, 0);
                } else {
                    debug_log_formatted((double)param_1, (double)param_2, (double)param_3,
                                        (double)param_4, param_5, param_6, param_7, param_8,
                                        (int)&DAT_80610ebc, (int)scratch, (int *)(int)unit,
                                        localPlayer, 0, 0, 0, 0);
                }
            } else {
                int sep = 0x42a;
                if (row[1] == 2) {
                    sep = 0x42b;
                }
                unsigned int refValue = (unsigned int)FUN_8022d938((int)refRow);
                worm_results_format_stat(param_1, param_2, param_3, param_4, param_5, param_6,
                                         param_7, param_8, (int)valBufA, refValue,
                                         (unsigned int)refRow[1], localPlayer, 0, 0, 0, 0);
                worm_results_format_stat(param_1, param_2, param_3, param_4, param_5, param_6,
                                         param_7, param_8, (int)valBufB, value,
                                         (unsigned int)row[1], localPlayer, 0, 0, 0, 0);
                int sepStr = get_string_by_id(sep);
                FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                             param_8, (int)lineBuf, (int *)(s__s__s__s_80461d67 + 1),
                             (int)valBufA, (int *)sepStr, (int)valBufB, 0, 0, 0);
                debug_log_formatted((double)param_1, (double)param_2, (double)param_3,
                                    (double)param_4, param_5, param_6, param_7, param_8,
                                    (int)&DAT_80610ebc, (int)lineBuf, (int *)(int)valBufA,
                                    sepStr, (int)valBufB, 0, 0, 0);
            }
            break;
        }
        off = off + 0x14;
    }
}

// 0x8022b714 - Resolve the two per-player HUD label strings (name + tag) for the
// given player slot from the fixed 0xff-stride string tables DAT_805c6db0 (row
// name) and DAT_805c6fb0 (row tag, offset by localPlayer*0x1fe). Out-params are
// set to 0 when the corresponding table entry is empty.
void worm_mode_get_player_name_strings(int *outName, int *outTag, int slot)
{
    *outName = 0;
    *outTag = 0;
    char *nameTable = (char *)&DAT_805c6db0;
    char *tagTable = (char *)&DAT_805c6fb0;

    if (nameTable[slot * 0xff] != '\0') {
        *outName = (int)(nameTable + slot * 0xff);
    }
    int tagOff = slot * 0xff + *(int *)(DAT_80611d74 + 4) * 0x1fe;
    if (tagTable[tagOff] == '\0') {
        return;
    }
    *outTag = (int)(tagTable + tagOff);
}

// 0x8022b770 - Map the session objective-mode bitmask (FUN_8022c5bc) to a small
// 1-based mode index: 0x100000->1, 0x200000->2, 0x400000->3, 0x800000->4, else 0.
unsigned int worm_session_get_mode_index(void)
{
    unsigned int result = 0;
    int mask = FUN_8022c5bc();
    if (mask != 0) {
        if (mask == 0x100000) {
            result = 1;
        } else if (mask == 0x200000) {
            result = 2;
        } else if (mask == 0x400000) {
            result = 3;
        } else if (mask == 0x800000) {
            result = 4;
        }
    }
    return result;
}

// ===========================================================================
// Batch: batch_691
// The mission-mode results/HUD readout builders and the per-frame MASTER score
// tick dispatch. FUN_8022b7f4 builds the two per-player objective label rows;
// FUN_8022bc34/bdc0 build the challenge pass/completion value strings; the
// *_get_*_strings helpers hand those cached strings to the results screen;
// FUN_8022befc fires the "almost complete" nag; FUN_8022bff0 accumulates the
// per-player elapsed-time score; FUN_8022c054 is the mode MASTER tick (the
// score/update counterpart to worm_mode_preload_dispatch); FUN_8022c2e8 and
// FUN_8022c3a0 are the comparison-op and player-count scaling helpers.
// Source: TimeSplitters: Future Perfect (GameCube, main.dol)
// ===========================================================================

// --- Additional file-local externs for batch_691 --------------------------
// Cached HUD result string tables (byte buffers; &-of yields row pointers).
extern int  DAT_805c73e4;   // pass/complete row-0 label (0xff-stride implied by usage)
extern int  DAT_805c74e4;   // pass/complete row-1 label table (0xff stride per player)
extern int  DAT_805c76e4;   // pass/complete row-2 label table (0xff stride per player)
extern int  DAT_805c6eaf;   // default "cleared" name literal
extern int  DAT_805c70af;   // default "cleared" per-player tag literal (0x1fe stride)
// Frame-context helper (mode 6-8 czs2 pre-tick housekeeping); returns a frame
// pointer passthrough (r3) reused as the next call's param_1.
extern int  FUN_80243bc8(unsigned long long p1, double p2);
// Value-returning alias for worm_results_check_challenge (0x8022ac04): the ROM
// leaves the pass/fail outcome in r3 (2 == failed/partial -> use best desc);
// the project-wide decl is void, so read the return via this alias (leading __
// so gen_externs skips it).
static int (*const __worm_results_check_challenge)(void) =
    (int (*)(void))0x8022ac04;

// ---------------------------------------------------------------------------

// 0x8022b7f4 - Build the two per-player objective label rows for the results
// HUD. For each of the two rows (row 0 = primary, row 1 = secondary) select the
// mode-specific label string id, copy it into the row-name table DAT_805c6db0,
// and format the accumulated per-player value into the row-tag table
// DAT_805c6fb0 (offset by localPlayer*0x1fe) using template DAT_80610ec0.
void worm_mode_build_objective_labels(unsigned long long param_1, unsigned long long param_2,
                                      unsigned long long param_3, unsigned long long param_4,
                                      unsigned long long param_5, unsigned long long param_6,
                                      unsigned long long param_7, unsigned long long param_8)
{
    char *nameTable = (char *)&DAT_805c6db0;
    char *tagTable = (char *)&DAT_805c6fb0;

    FUN_803826e4();
    int rowOff = 0;
    for (int row = 0; row < 2; row = row + 1) {
        int *rowNamePtr = (int *)(nameTable + rowOff);
        *(char *)rowNamePtr = 0;
        int localPlayer = *(int *)(DAT_80611d74 + 4);
        int tagBase = localPlayer * 0x1fe;
        tagTable[rowOff + tagBase] = 0;

        // Pick the label string id for this mode + row. -1 means no label for
        // this mode (the switch simply falls through in the ROM).
        int strId = -1;
        switch (DAT_805015d8) {
        case 0:  case 1:  case 2:
        case 3:  case 4:  case 5:
            strId = (row != 0) ? 0x683 : 0x684;
            break;
        case 6:  case 7:  case 8:
            strId = 0x66d;
            break;
        case 9:
            strId = (row != 0) ? 0x65c : 0x65d;
            break;
        case 10:
            strId = (row != 0) ? 0x658 : 0x659;
            break;
        case 0xb:
            strId = (row != 0) ? 0x65a : 0x65b;
            break;
        case 0xf:
            strId = (row != 0) ? 0x669 : 0x66a;
            break;
        case 0x10:
            strId = (row != 0) ? 0x683 : 0x667;
            break;
        case 0x11:
            strId = 0x665;
            break;
        case 0x12: case 0x13: case 0x14:
            strId = (row != 0) ? 0x683 : 0x667;
            break;
        default:
            break;
        }

        if (strId != -1) {
            int *nameStr = (int *)get_string_by_id(strId);
            unsigned long long w = FUN_8038b510(rowNamePtr, nameStr);
            int tagOff = localPlayer * 0x1fe;
            int value = *(int *)((int)&DAT_806128e8 + localPlayer * 4);
            FUN_80387fb8(w, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                         (int)(tagTable + rowOff + tagOff), (int *)&DAT_80610ec0, value,
                         (int *)&DAT_805c6fb0, tagOff, 0, 0, 0);
        }
        rowOff = rowOff + 0xff;
    }
    FUN_80382730();
}

// 0x8022bbe8 - Resolve the two cached "pass" result strings (name DAT_805c73e4
// and per-player tag DAT_805c74e4[localPlayer]); out-params are 0 when empty.
void worm_results_get_pass_strings(int *outName, int *outTag)
{
    *outName = 0;
    *outTag = 0;
    char *nameStr = (char *)&DAT_805c73e4;
    char *tagTable = (char *)&DAT_805c74e4;

    if (*nameStr != '\0') {
        *outName = (int)nameStr;
    }
    int off = *(int *)(DAT_80611d74 + 4) * 0xff;
    if (tagTable[off] == '\0') {
        return;
    }
    *outTag = (int)(tagTable + off);
}

// 0x8022bc34 - Build the challenge score string for the results screen. Check
// pass/fail (worm_results_check_challenge); pick the target or best descriptor;
// if the descriptor unit is a time/rank (word +2 == 1 or word[1] == 2) emit via
// time_format_long, otherwise format the numeric score into DAT_805c74e4.
void worm_results_build_score_string(unsigned long long param_1, unsigned long long param_2,
                                     unsigned long long param_3, unsigned long long param_4,
                                     unsigned long long param_5, unsigned long long param_6,
                                     unsigned long long param_7, unsigned long long param_8)
{
    int targetBuf[32];
    char *tagTable = (char *)&DAT_805c74e4;

    int result = __worm_results_check_challenge();
    unsigned short *desc = worm_mode_find_target_desc(DAT_805015d8);
    int levelRec = DAT_80611d74;
    int localPlayer = *(int *)(DAT_80611d74 + 4);

    DAT_805c73e4 = 0;
    tagTable[localPlayer * 0xff] = 0;

    if (result == 2) {
        desc = worm_mode_find_best_desc(DAT_805015d8);
    }

    if (*(int *)(desc + 2) == 1) {
        worm_results_get_score();
        time_format_long(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    } else if (desc[1] == 2) {
        time_format_long(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    } else {
        unsigned int value = (unsigned int)worm_results_get_score();
        worm_results_format_target(param_1, param_2, param_3, param_4, param_5, param_6,
                                   param_7, param_8, DAT_805015d8, value, levelRec, 0,
                                   0, 0, 0, 0);
        FUN_8038b510((int *)(tagTable + localPlayer * 0xff), targetBuf);
    }
}

// 0x8022bd48 - Resolve the three cached result strings (name DAT_805c73e4, and
// per-player tag/detail rows DAT_805c74e4 / DAT_805c76e4); out-params 0 when empty.
void worm_results_get_three_strings(int *outName, int *outTag, int *outDetail)
{
    *outName = 0;
    *outTag = 0;
    *outDetail = 0;
    char *nameStr = (char *)&DAT_805c73e4;
    char *tagTable = (char *)&DAT_805c74e4;
    char *detailTable = (char *)&DAT_805c76e4;
    int off = *(int *)(DAT_80611d74 + 4) * 0xff;

    if (*nameStr != '\0') {
        *outName = (int)nameStr;
    }
    if (tagTable[off] != '\0') {
        *outTag = (int)(tagTable + off);
    }
    if (detailTable[off] == '\0') {
        return;
    }
    *outDetail = (int)(detailTable + off);
}

// 0x8022bdc0 - Build the completion result strings for the "collect/deliver"
// family of modes (9..0x11). For the upload mode (0x11) or the delivery band
// (9..0xe), stamp the fixed "cleared" name/tag literals; otherwise, when no
// string is yet set, emit the "completed" label (0xe3e) plus the formatted
// target score into DAT_805c74e4.
void worm_results_build_completion_string(unsigned long long param_1, unsigned long long param_2,
                                          unsigned long long param_3, unsigned long long param_4,
                                          unsigned long long param_5, unsigned long long param_6,
                                          unsigned long long param_7, unsigned long long param_8)
{
    int levelRec = DAT_80611d74;
    int mode = DAT_805015d8;
    char *nameStr = (char *)&DAT_805c73e4;
    char *tagTable = (char *)&DAT_805c74e4;
    char *detailTable = (char *)&DAT_805c76e4;
    int localPlayer = *(int *)(DAT_80611d74 + 4);
    int off = localPlayer * 0xff;

    *nameStr = '\0';
    tagTable[off] = 0;
    detailTable[off] = 0;

    bool below0x11 = mode < 0x11;
    bool is0x11 = mode == 0x11;
    if (is0x11 || (below0x11 && (mode < 0xf) && (8 < mode))) {
        FUN_8038b510((int *)&DAT_805c73e4, (int *)&DAT_805c6eaf);
        off = localPlayer * 0xff;
        FUN_8038b510((int *)(tagTable + off), (int *)((char *)&DAT_805c70af + localPlayer * 0x1fe));
    }

    off = localPlayer * 0xff;
    if ((detailTable[off] == '\0') && (*nameStr == '\0') && (tagTable[off] == '\0')) {
        int targetBuf[32];
        int *labelStr = (int *)get_string_by_id(0xe3e);
        FUN_8038b510((int *)&DAT_805c73e4, labelStr);
        unsigned int value = (unsigned int)worm_results_get_score();
        worm_results_format_target(param_1, param_2, param_3, param_4, param_5, param_6,
                                   param_7, param_8, mode, value, off, levelRec, 0, 0, 0, 0);
        FUN_8038b510((int *)(tagTable + localPlayer * 0xff), targetBuf);
    }
}

// 0x8022befc - "Almost complete" nag: scan the current mode object's target
// rows (flag bit 1); if any target is within 10*playerCount of the local
// player's scored value and the nag has not yet fired (DAT_805015c4 & 2 clear),
// submit the near-complete HUD string (0xe40), play cue 0xd4a, and latch the bit.
void worm_mode_check_near_completion(void)
{
    int playParams[18];

    __save_gpr_28();
    int modeObj = __worm_mode_object_current();
    int rowOff = 0;
    int count = *(int *)(modeObj + 0x20);
    for (int i = 0; i < count; i = i + 1) {
        unsigned short *row = (unsigned short *)(*(int *)(modeObj + 0x1c) + rowOff);
        if ((*(int *)(row + 2) == 0) && ((*row & 1) != 0)) {
            int value = FUN_8022d938((int)row);
            int scored = *(int *)((int)&DAT_806128e0 + *(int *)(DAT_80611d74 + 4) * 4);
            if ((value - scored < DAT_806109c0 * 10) && ((DAT_805015c4 & 2) == 0)) {
                unsigned int *txt = (unsigned int *)get_string_by_id(0xe40);
                hud_text_submit(0, 3, txt);
                sound_init_play_params((int)playParams);
                playParams[0] = 0xd4a;
                sound_play_request(playParams);
                DAT_805015c4 = DAT_805015c4 | 2;
            }
        }
        rowOff = rowOff + 0x14;
    }
    __restore_gpr_28();
}

// 0x8022bff0 - Accumulate the local player's elapsed-time score DAT_806128e0
// by the per-frame delta DAT_806108e4, gated off during game-over (state 5),
// the "no-score" player mode (+0x28 == 0xb), and the paused flag (0x40000000).
void worm_mode_accumulate_time(void)
{
    if (DAT_80611934 == 5) {
        return;
    }
    if (*(int *)((int)&DAT_80578680 + *(int *)(DAT_80611d78 + 4) * 0x5df) == 0xb) {
        return;
    }
    if ((DAT_805015c0 & 0x40000000) != 0) {
        return;
    }
    int off = *(int *)(DAT_80611d74 + 4) * 4;
    *(int *)((int)&DAT_806128e0 + off) = *(int *)((int)&DAT_806128e0 + off) + DAT_806108e4;
}

// 0x8022c054 - Mission-mode MASTER per-frame tick. Switch on the current mode
// (DAT_805015d8): cache that mode's score into the per-player array DAT_806128e8
// and run the mode's master update. Modes 6-8 (czs2) do extra per-entity flag
// clearing + a level-intro sound before ticking. Finishes with the elapsed-time
// accumulator. The register-spill args from the decompiler collapse to each
// callee's real (8-arg) signature.
void worm_mode_master_tick(unsigned long long param_1, double param_2, double param_3,
                           double param_4, unsigned long long param_5, unsigned long long param_6,
                           unsigned long long param_7, unsigned long long param_8)
{
    int scoreOff = (int)(*(int *)(DAT_80611d74 + 4)) * 4;

    switch (DAT_805015d8) {
    case 0:
    case 1:
    case 2:
        *(unsigned int *)((int)&DAT_806128e8 + scoreOff) = worm_cdh1_get_score();
        worm_cdh1_mission_tick(param_1, param_2, param_3, param_4, param_5, param_6,
                               param_7, param_8);
        break;
    case 3:
    case 4:
    case 5:
        *(unsigned int *)((int)&DAT_806128e8 + scoreOff) = worm_czs_get_score();
        worm_czs_item_tick_all((double)param_1, param_2, param_3, param_4, param_5, param_6,
                               param_7, param_8);
        break;
    case 6:
    case 7:
    case 8:
        if (*(int *)(DAT_806128b8 + 0x38) != 0) {
            int entOff = 0;
            for (int i = 0; i < DAT_80612610; i = i + 1) {
                int rec = *(int *)(DAT_80611d78 + entOff + 0x14);
                *(unsigned int *)(rec + 0x28) = *(unsigned int *)(rec + 0x28) & 0xffffff9e;
                entOff = entOff + 0x141c;
            }
            if (*(int *)(DAT_80611d78 + 0x14) != 0) {
                param_1 = (unsigned long long)FUN_80243bc8(param_1, param_2);
            }
            if (DAT_805015d8 != 7) {
                if ((DAT_805015d8 < 7) && (5 < DAT_805015d8)) {
                    int p[13];
                    sound_init_play_params((int)p);
                    p[0] = 0x11;
                    sound_play_request(p);
                } else if ((6 < DAT_805015d8) && (DAT_805015d8 < 9)) {
                    int p[12];
                    sound_init_play_params((int)p);
                    p[0] = 0x18;
                    sound_play_request(p);
                }
            }
        }
        *(unsigned int *)(DAT_806128b8 + 0x38) = 0;
        worm_czs2_obj_tick(param_1, param_2, param_3, param_4, param_5, param_6,
                           param_7, param_8);
        break;
    case 9:
        *(int *)((int)&DAT_806128e8 + scoreOff) = worm_plat_count_active();
        break;
    case 10:
        *(int *)((int)&DAT_806128e8 + scoreOff) = worm_plat_count_active_material2();
        worm_bttl_platform_master_tick((double)param_1, param_2, param_3, param_4,
                                       (double)param_5, (double)param_6, (double)param_7,
                                       param_8);
        break;
    case 0xb:
        worm_bttl_platform_tick();
        worm_bttl_count_active();
        *(unsigned int *)((int)&DAT_806128e8 + scoreOff) = DAT_806128f4;
        break;
    case 0xc:
    case 0xd:
    case 0xe:
        worm_plat_hud_trigger_once();
        break;
    case 0xf:
        *(unsigned int *)((int)&DAT_806128e8 + scoreOff) = (unsigned int)worm_asl_obj_get_active();
        worm_asl_master_tick((double)param_1, param_2, param_3, param_4, (double)param_5,
                             (double)param_6, (double)param_7, param_8);
        break;
    case 0x10:
        *(unsigned int *)((int)&DAT_806128e8 + scoreOff) = DAT_806128f4;
        worm_asl_master_tick2();
        break;
    case 0x11:
        worm_upl_recompute_score();
        worm_upl_obj_tick();
        break;
    case 0x12:
        *(unsigned int *)((int)&DAT_806128e8 + scoreOff) = DAT_806128f4;
        worm_netb_ball_master_tick();
        break;
    case 0x13:
        *(unsigned int *)((int)&DAT_806128e8 + scoreOff) = DAT_806128f4;
        worm_sprg_master_tick(param_1, param_2, param_3, param_4, param_5, param_6,
                              param_7, param_8);
        break;
    case 0x14:
        *(unsigned int *)((int)&DAT_806128e8 + scoreOff) = DAT_806128f4;
        worm_prop_spawn_cadence_tick();
        break;
    default:
        *(unsigned int *)((int)&DAT_806128e8 + scoreOff) = DAT_806128f4;
        break;
    }
    worm_mode_accumulate_time();
}

// 0x8022c2e8 - Compare two signed values per a comparison-operator selector:
// op 0 = at-most (a <= b), op 1 = at-least (a <= b variant on b), op 2 = equal,
// op 3 = not-equal (xor sign test), op 5 = at-least (b <= a). Returns 0/1.
unsigned int worm_compare_values_by_op(unsigned int a, unsigned int b, int op)
{
    unsigned int result = 0;
    if (op == 3) {
        result = (unsigned int)(((int)(a ^ b) >> 1) - ((a ^ b) & a)) >> 0x1f;
    } else {
        int signA = (int)a >> 0x1f;
        int signB = (int)b >> 0x1f;
        if (op < 3) {
            if (op == 1) {
                result = (unsigned int)(signB + ((int)(unsigned int)(a <= b) - signA));
            } else if (op < 1) {
                if (op >= 0) {
                    result = (unsigned int)(((int)(b ^ a) >> 1) - ((b ^ a) & b)) >> 0x1f;
                }
            } else {
                result = countLeadingZeros(b - a);
                result = result >> 5;
            }
        } else if (op == 5) {
            result = (unsigned int)(signA + ((int)(unsigned int)(b <= a) - signB));
        } else if (op < 5) {
            result = (unsigned int)(signA + ((int)(unsigned int)(b <= a) - signB));
        }
    }
    return result;
}

// 0x8022c3a0 - Scale a raw counter by the player-count ratio: convert to float,
// normalize against the active player count (DAT_806109c0) using the standard
// unsigned->double bias trick, apply the FLOAT_806185ac scale, and floor.
int worm_scale_by_player_count(unsigned int value)
{
    float valF = (float)((double)value - DOUBLE_806185a0);
    float countF = (float)((double)DAT_806109c0 - DOUBLE_806185a0);
    int scaled = (int)((FLOAT_806185ac * valF) / countF);
    float scaledF = (float)((double)(unsigned int)scaled - DOUBLE_806185a0);
    return (int)((scaledF * countF) / FLOAT_806185ac);
}

// ===========================================================================
// Batch: batch_692
// The objective-descriptor evaluation core. worm_objective_get_value is the
// value-source accessor (15 kinds: per-player time/kills/team/special counters,
// timer, custom code callback); worm_objective_eval_all runs every descriptor
// row through the value+compare pipeline and stashes the primary/secondary
// scores; worm_objective_resolve_round is the full end-of-round win/loss/
// complete resolver (HUD strings + save write + match_round_end); the small
// query helpers report mode index / object fields / display-row presence; and
// worm_objective_draw_display renders the on-screen objective readout.
// Source: TimeSplitters: Future Perfect (GameCube, main.dol)
// ===========================================================================

// --- Additional file-local externs for batch_692 --------------------------
extern int   DAT_806120fc;      // count of extra (bot/AI) player records
extern int   FLOAT_806186f8;    // rank-value scale (score display)
extern int   DAT_80610eec;      // "%d" style format template (alt palette)
extern int   DAT_80610ef0;      // "%s: " label+value compound template

// Objective-eval scope save/restore intrinsics (paired push/pop of GPR window).
extern void  FUN_803826c4(void);
extern void  FUN_80382710(void);
extern void  FUN_803826b4(void);
extern void  FUN_80382700(void);
extern int   FUN_803826d4(void);   // returns mode object base in r3/r4 hi word
extern unsigned int FUN_803825c0(double v); // float->packed color component
extern void  FUN_80382720(void);

// Score/stat query helpers (per-player record based).
extern int  score_calc_player_total(int playerRec);
extern int  score_get_times_killed_by(int playerRec);
extern int  score_get_team(int teamId);
extern int  score_get_player(int playerIdx);
extern int  score_get_special(int playerRec);

// HUD / save / input helpers used by the round resolver + display.
extern void hud_update_rank_display(unsigned long long p1, unsigned long long p2,
                                    unsigned long long p3, unsigned long long p4,
                                    unsigned long long p5, unsigned long long p6,
                                    unsigned long long p7, unsigned long long p8);
extern void match_round_end(void);
extern int  save_get_data_ptr(void);
extern void input_stub_empty(void);
extern void text_draw_char_shadowed(double x, double y, unsigned char *text,
                                    unsigned int colorA, unsigned int colorB);
extern double text_get_char_width(unsigned char *text);
extern double text_get_char_height(unsigned char *text);

// ---------------------------------------------------------------------------

// 0x8022c430 - Objective value-source accessor. `kind` selects one of 15 value
// sources; `arg` is a player index (cast), a code-callback pointer (kind 10),
// or a packed (lo|hi) stat key (kind 9). Returns the raw objective quantity.
unsigned int worm_objective_get_value(int kind, unsigned char *arg)
{
    unsigned int value;
    int player = (int)arg;

    switch (kind) {
    case 0:
        value = worm_scale_by_player_count(*(unsigned int *)((int)&DAT_806128e0 +
                                                             *(int *)(DAT_80611d74 + 4) * 4));
        break;
    case 1:
        value = worm_scale_by_player_count(*(unsigned int *)(DAT_80611d90 + player * 0xea0 + 0xa64));
        break;
    case 2:
    case 3:
        value = *(unsigned int *)(DAT_80611d90 + player * 0xea0 + 0xa68);
        break;
    case 4:
        value = (unsigned int)(FLOAT_806186f4 *
                               *(float *)(*(int *)(DAT_80611d90 + player * 0xea0) + 0x1cc));
        break;
    case 5:
        value = (unsigned int)score_calc_player_total(DAT_80611d90 + player * 0xea0);
        break;
    case 6:
        value = (unsigned int)score_get_times_killed_by(DAT_80611d90 + player * 0xea0);
        break;
    case 7:
        value = (unsigned int)score_get_team(*(int *)(DAT_80611d90 + player * 0xea0 + 0x84));
        break;
    case 8:
        value = (unsigned int)score_get_player(player);
        break;
    case 9: {
        double d = FUN_80237cb4((unsigned int)arg & 0xffff, (unsigned int)arg >> 0x10);
        value = (unsigned int)((double)FLOAT_806186f4 * d);
        break;
    }
    case 10: {
        int (*cb)(void) = (int (*)(void))arg;
        value = (unsigned int)cb();
        break;
    }
    case 0xb:
        value = (unsigned int)score_get_special(DAT_80611d90 + player * 0xea0);
        break;
    case 0xc:
        value = countLeadingZeros(8 - DAT_80611934) >> 5;
        break;
    case 0xd:
        value = *(unsigned int *)(DAT_80611d90 + player * 0xea0 + 0xc6c) >> 2 & 1;
        break;
    case 0xe:
    default:
        value = 0;
        break;
    }
    return value;
}

// Shared helper: evaluate one objective descriptor row against its value source.
// Handles the "per-player" (kind != -3/-1/-2) and aggregate (-1 all players,
// -2 all incl. bots, -3 all opposing-team) argument forms. Returns 1 if any
// evaluated comparison passed.
static unsigned int worm_objective_eval_row(unsigned short *row)
{
    unsigned int passed = 0;
    int kind = *(int *)(row + 2);
    int arg = *(int *)(row + 4);
    int op = *(int *)(row + 6);

    if ((kind == 10) || (kind == 9) || (arg != -3)) {
        if ((kind == 10) || (kind == 9) || (arg != -1)) {
            if ((kind == 10) || (kind == 9) || (arg != -2)) {
                unsigned int v = worm_objective_get_value(kind, (unsigned char *)arg);
                unsigned int target = FUN_8022d938((int)row);
                if (worm_compare_values_by_op(v, target, op) != 0) {
                    passed = 1;
                }
            } else {
                for (int p = 0; p < DAT_80612610; p = p + 1) {
                    unsigned int v = worm_objective_get_value(kind, (unsigned char *)p);
                    unsigned int target = FUN_8022d938((int)row);
                    if (worm_compare_values_by_op(v, target, op) != 0) {
                        passed = 1;
                    }
                }
            }
        } else {
            for (int p = 0; p < DAT_80612610 + DAT_806120fc; p = p + 1) {
                unsigned int v = worm_objective_get_value(kind, (unsigned char *)p);
                unsigned int target = FUN_8022d938((int)row);
                if (worm_compare_values_by_op(v, target, op) != 0) {
                    passed = 1;
                }
            }
        }
    } else {
        int myTeam = *(int *)(DAT_80611d90 + 0x84);
        unsigned int base = worm_objective_get_value(kind, (unsigned char *)0);
        int recOff = 0;
        for (int p = 0; p < DAT_80612610 + DAT_806120fc; p = p + 1) {
            if (myTeam != *(int *)(DAT_80611d90 + recOff + 0x84)) {
                unsigned int v = worm_objective_get_value(kind, (unsigned char *)p);
                int target = FUN_8022d938((int)row);
                if (worm_compare_values_by_op(base, v + target, op) != 0) {
                    passed = 1;
                }
            }
            recOff = recOff + 0xea0;
        }
    }
    return passed;
}

// 0x8022c5bc - Evaluate every objective descriptor row of the current mode
// object once (driving the per-player comparisons for their side effects) and
// stash the primary (flag 0x80) / secondary (flag 0x100) score values into the
// global tallies DAT_806128f4 / DAT_806128f0.
void worm_objective_eval_all(void)
{
    FUN_803826c4();
    int modeObj = __worm_mode_object_current();

    // (leading scan for the flag-0x20 row is a side-effect-free ROM artifact)
    int count = *(int *)(modeObj + 0x20);
    int off = 0;
    for (int i = 0; i < count; i = i + 1) {
        if ((*(unsigned short *)(*(int *)(modeObj + 0x1c) + off) & 0x20) != 0) {
            break;
        }
        off = off + 0x14;
    }

    off = 0;
    for (int i = 0; i < *(int *)(modeObj + 0x20); i = i + 1) {
        unsigned short *row = (unsigned short *)(*(int *)(modeObj + 0x1c) + off);
        worm_objective_eval_row(row);

        if (((*row & 0x80) != 0) || ((*row & 0x100) != 0)) {
            int kind = *(int *)(row + 2);
            int arg = *(int *)(row + 4);
            if ((kind != 10) && (kind != 9) &&
                ((unsigned int)(arg + 2) < 2 || (arg == -3))) {
                arg = 0;
            }
            unsigned int v = worm_objective_get_value(kind, (unsigned char *)arg);
            if ((*row & 0x80) != 0) {
                DAT_806128f4 = v;
            }
            if ((*row & 0x100) != 0) {
                DAT_806128f0 = v;
            }
        }
        off = off + 0x14;
    }
    FUN_80382710();
}

// 0x8022c940 - End-of-round objective resolver. Evaluates all descriptor rows
// while tracking win / all-clear / bonus / special booleans; on a "win" row
// (flag 1 passed) or exhausting the list, emits the appropriate results HUD
// text (win 0x685 / lose 0x17bd / bonus 0x17bf), latches the results-state bits
// in DAT_805015c4, writes the score into the save block, and ends the round.
void worm_objective_resolve_round(unsigned long long param_1, unsigned long long param_2,
                                  unsigned long long param_3, unsigned long long param_4,
                                  unsigned long long param_5, unsigned long long param_6,
                                  unsigned long long param_7, unsigned long long param_8)
{
    FUN_803826b4();
    int modeObj = __worm_mode_object_current();
    bool win = true;         // bVar8
    bool allClear = true;    // bVar4
    bool bonusOk = true;     // bVar5
    bool hasFlag20 = false;  // bVar6
    bool partial = false;    // bVar3
    bool isWinRowZero = false; // bVar9
    bool timeScore = false;  // bVar7
    unsigned int state = 0;  // uVar23

    worm_mode_build_objective_labels(param_1, param_2, param_3, param_4, param_5, param_6,
                                     param_7, param_8);

    int count = *(int *)(modeObj + 0x20);
    int off = 0;
    for (int i = 0; i < count; i = i + 1) {
        if ((*(unsigned short *)(*(int *)(modeObj + 0x1c) + off) & 0x20) != 0) {
            hasFlag20 = true;
        }
        off = off + 0x14;
    }

    off = 0;
    int idx = 0;
    while (idx < *(int *)(modeObj + 0x20)) {
        unsigned short *row = (unsigned short *)(*(int *)(modeObj + 0x1c) + off);
        unsigned int passed = worm_objective_eval_row(row);

        unsigned short flags = *row;
        if (((flags & 2) != 0) && (passed == 0)) {
            win = false; allClear = false; bonusOk = false; hasFlag20 = false;
        }
        if (((flags & 4) != 0) && (passed == 0)) { win = false; }
        if (((flags & 8) != 0) && (passed == 0)) { allClear = false; }
        if (((flags & 0x10) != 0) && (passed == 0)) { bonusOk = false; }
        if (((flags & 0x20) != 0) && (passed == 0)) { hasFlag20 = false; }

        if (((flags & 0x80) != 0) || ((flags & 0x100) != 0)) {
            int kind = *(int *)(row + 2);
            int arg = *(int *)(row + 4);
            if ((kind != 10) && (kind != 9) &&
                ((unsigned int)(arg + 2) < 2 || (arg == -3))) {
                arg = 0;
            }
            unsigned int v = worm_objective_get_value(kind, (unsigned char *)arg);
            if (((flags & 0x80) != 0)) {
                DAT_806128f4 = v;
                if (row[1] == 2) { timeScore = true; }
            }
            if ((flags & 0x100) != 0) {
                DAT_806128f0 = v;
            }
        }

        if (((*row & 1) != 0) && (passed != 0)) {
            state = 1;
            win = false; allClear = false; bonusOk = false; hasFlag20 = false;
            if (*(int *)(row + 2) == 0) { isWinRowZero = true; }
            break;   // -> resolve
        }

        if (((*row & 0x40) != 0)) {
            partial = true;
            if (passed < state) { /* keep state */ } else { state = passed; }
        }
        idx = idx + 1;
        off = off + 0x14;
    }

    // ---- resolve (LAB_8022ccdc) ----
    if ((!win) || (((!partial || (state == 0)) && partial))) {
        if (win || (state == 0)) {
            worm_mode_check_near_completion();
        } else {
            worm_results_build_completion_string(param_1, param_2, param_3, param_4, param_5,
                                                 param_6, param_7, param_8);
            if (isWinRowZero) {
                if ((*(unsigned int *)(modeObj + 0x18) & 4) == 0) {
                    unsigned int *txt = (unsigned int *)get_string_by_id(0x17bf);
                    hud_text_submit(0, 4, txt);
                } else {
                    hud_update_rank_display(param_1, param_2, param_3, param_4, param_5, param_6,
                                            param_7, param_8);
                }
                DAT_805015c4 = DAT_805015c4 | 0x20000;
            } else if ((*(unsigned int *)(modeObj + 0x18) & 4) == 0) {
                bool teamMode = (DAT_805015d8 == 0x13) || (DAT_805015d8 == 0x11);
                int slot = teamMode ? 0xb : 4;
                unsigned int *txt = (unsigned int *)get_string_by_id(0x17bd);
                hud_text_submit(0, slot, txt);
            } else {
                hud_update_rank_display(param_1, param_2, param_3, param_4, param_5, param_6,
                                        param_7, param_8);
            }
            int save = save_get_data_ptr();
            if (save != 0) {
                *(int *)(save_get_data_ptr() + 0xc) = 0;
                *(int *)(save_get_data_ptr() + 0x128) = 0;
            }
            DAT_805015c4 = DAT_805015c4 | 0x10000;
            match_round_end();
        }
    } else {
        if ((*(unsigned int *)(modeObj + 0x18) & 4) == 0) {
            bool teamMode = (DAT_805015d8 == 0x13) || (DAT_805015d8 == 0x11);
            int slot = teamMode ? 0xb : 4;
            unsigned int *txt = (unsigned int *)get_string_by_id(0x685);
            hud_text_submit(0, slot, txt);
        } else {
            hud_update_rank_display(param_1, param_2, param_3, param_4, param_5, param_6,
                                    param_7, param_8);
        }
        unsigned int bits = DAT_805015c4 | (allClear ? 0x308000 : 0x108000);
        DAT_805015c4 = bits;
        if (bonusOk) { DAT_805015c4 = DAT_805015c4 | 0x400000; }
        if (hasFlag20) { DAT_805015c4 = DAT_805015c4 | 0x800000; }
        worm_results_build_score_string(param_1, param_2, param_3, param_4, param_5, param_6,
                                        param_7, param_8);
        match_round_end();
        input_stub_empty();

        if (timeScore) {
            float f = FLOAT_806186ec *
                      (float)((double)(unsigned int)DAT_806128f4 - DOUBLE_806185a0);
            if (DAT_806109c0 != 0x3c) {
                f = FLOAT_806186e8 *
                    (float)((double)(unsigned int)DAT_806128f4 - DOUBLE_806185a0);
            }
            DAT_806128f4 = (unsigned int)f;
            int scaled = (int)DAT_806128f4 / 800;
            *(int *)(save_get_data_ptr() + 0x128) = scaled;
        } else {
            *(unsigned int *)(save_get_data_ptr() + 0x128) = DAT_806128f4;
        }
    }
    FUN_80382700();
}

// 0x8022cf74 - Return the current mission-mode index (DAT_805015d8).
unsigned int worm_mode_get_index(void)
{
    return (unsigned int)DAT_805015d8;
}

// 0x8022cf84 - Return field +0x14 of the mode object for the given mode index.
unsigned int worm_mode_get_field14(int mode)
{
    unsigned char *obj = __worm_mode_object(mode);
    return *(unsigned int *)(obj + 0x14);
}

// 0x8022cfa8 - Return 1 if any descriptor row of the given mode object carries
// the on-screen display flag (0x200), else 0.
unsigned int worm_mode_has_display_row(int modeObj)
{
    int count = *(int *)(modeObj + 0x20);
    int off = 0;
    for (int i = 0; i < count; i = i + 1) {
        if ((*(unsigned short *)(*(int *)(modeObj + 0x1c) + off) & 0x200) != 0) {
            return 1;
        }
        off = off + 0x14;
    }
    return 0;
}

// 0x8022cfe4 - Render the on-screen objective readout. For each descriptor row
// flagged for display (0x200), format its value by unit type (time/count/rank
// families, string ids 0x4b3..0x66a) into two scratch strings (label + value),
// then draw them shadowed left/right-aligned, advancing the y cursor per row.
// The register-spill inputs are the row layout vector (posv: x, top, bottom),
// the running y position, and the draw-enable flag.
void worm_objective_draw_display(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, float *posv, unsigned int yPos,
                                 int drawFlag)
{
    unsigned char labelBuf[256];
    unsigned char valueBuf[256];

    int modeObj = FUN_803826d4();
    double titleH = (double)*(float *)((int)modeObj + 0xf0);
    unsigned int colorA = (FUN_803825c0((double)(float)((double)FLOAT_806185b8 * titleH)) & 0xff)
                          | 0x64646400;
    double valueScale = (double)(float)((double)FLOAT_806186f8 * titleH);
    unsigned int colorB = (FUN_803825c0(valueScale) & 0xff) | 0x5a144600;
    float rightEdge = posv[2];

    int off = 0;
    for (int i = 0; i < *(int *)((int)modeObj + 0x20); i = i + 1) {
        unsigned short *row = (unsigned short *)(*(int *)((int)modeObj + 0x1c) + off);
        if ((*row & 0x200) != 0) {
            int tmpl = (DAT_8060f948 == 1) ? (int)&DAT_80610eec : (int)&DAT_80610ec4;
            int labelId = -1;
            switch (row[1]) {
            case 2:  labelId = 0x4b4; break;
            case 8:  labelId = 0x662; break;
            case 9:  labelId = 0x661; break;
            case 10: labelId = 0x660; break;
            case 0xb: labelId = 0x65f; break;
            case 0xd: labelId = 0x4b5; break;
            case 0xe: labelId = 0x66a; break;
            case 0xf: labelId = 0x4b6; break;
            case 0x10: labelId = 0x4b3; break;
            default: break;
            }
            if (labelId != -1) {
                int str = get_string_by_id(labelId);
                int *r = (int *)debug_log_formatted(valueScale, param_2, param_3, param_4, param_5,
                                                    param_6, param_7, param_8, (int)&DAT_80610ef0,
                                                    str, (int *)tmpl, drawFlag, (int)yPos, 0, 0, 0);
                FUN_8038b510((int *)labelBuf, r);
                if (row[1] == 2) {
                    time_format_long(param_1, param_2, param_3, param_4, param_5, param_6,
                                     param_7, param_8);
                } else {
                    r = (int *)debug_log_formatted(valueScale, param_2, param_3, param_4, param_5,
                                                   param_6, param_7, param_8, (int)&DAT_80610ec0,
                                                   *(int *)(row + 8), (int *)tmpl, drawFlag,
                                                   (int)yPos, 0, 0, 0);
                    FUN_8038b510((int *)valueBuf, r);
                }
            }

            if (drawFlag != 0) {
                double yF = (double)(float)((double)(unsigned int)yPos - DOUBLE_806185a0);
                text_draw_char_shadowed((double)(float)((double)FLOAT_806185ac + (double)posv[0]),
                                        yF, labelBuf, colorA, colorB);
                double w = text_get_char_width(valueBuf);
                text_draw_char_shadowed((double)((float)((double)rightEdge - w) - FLOAT_806185ac),
                                        yF, valueBuf, colorA, colorB);
            }
            double h = text_get_char_height(labelBuf);
            h = (double)(float)((double)FLOAT_80618604 + h);
            yPos = (unsigned int)((double)(float)((double)(unsigned int)yPos - DOUBLE_806185a0) + h);
        }
        off = off + 0x14;
    }
    FUN_80382720();
}

// 0x8022d43c - For the czs2 modes (6-8), copy the 3 phase-value words from the
// czs2 object (DAT_806128b8, 0x34-stride per player-team) into the out buffer.
// Returns 1 when populated, 0 for non-czs2 modes.
unsigned int worm_czs2_get_display_values(int outBuf)
{
    if ((DAT_805015d8 != 6) && (DAT_805015d8 != 7) && (DAT_805015d8 != 8)) {
        return 0;
    }
    int dstOff = 0;
    int srcOff = 0;
    for (int i = 0; i < 3; i = i + 1) {
        int base = *(int *)DAT_80611d74 * 0x34 + srcOff;
        *(unsigned int *)(outBuf + dstOff) = *(unsigned int *)(DAT_806128b8 + base + 0xc);
        srcOff = srcOff + 8;
        dstOff = dstOff + 4;
    }
    return 1;
}

// 0x8022d4ac - Per-mode "run zone-capture tick" gate. Modes 0-2 run the CZS
// capture tick; modes 4/0xf/0x12 are disabled (0); the rest default to enabled (1).
unsigned int worm_mode_capture_enabled(int param_1)
{
    unsigned int result;
    switch (DAT_805015d8) {
    case 0:
    case 1:
    case 2:
        result = worm_czs_zone_capture_tick(param_1);
        break;
    case 4:
    case 0xf:
    case 0x12:
        result = 0;
        break;
    case 0x11:
        result = 1;
        break;
    default:
        result = 1;
        break;
    }
    return result;
}

// 0x8022d520 - Per-mode bonus/decrement hook: mode 0xb awards the battle bonus;
// modes 0-2 decrement the cdh1 countdown, and mode 1 additionally scores the
// "objective kind 3" event for the given source entity.
void worm_mode_bonus_hook(unsigned long long param_1, unsigned long long param_2,
                          unsigned long long param_3, unsigned long long param_4,
                          unsigned long long param_5, unsigned long long param_6,
                          unsigned long long param_7, unsigned long long param_8, int param_9)
{
    if (DAT_805015d8 == 0xb) {
        worm_bttl_award_bonus(param_1, param_2, param_3, param_4, param_5, param_6,
                              param_7, param_8);
    } else if ((DAT_805015d8 < 3) && (DAT_805015d8 >= 0)) {
        worm_cdh1_obj_tick_decrement();
        if (DAT_805015d8 == 1) {
            worm_cdh1_objective_score(3, param_9);
        }
    }
}

// ===========================================================================
// Batch: batch_693
// Mode-classification predicates and the two core mode-object accessors that
// the rest of the mission-hooks module has been calling all along:
// worm_objective_scale_target (FUN_8022d938) scales a descriptor's raw target
// by the player count, and worm_mode_get_object (FUN_8022d9a4) is the mode ->
// objective-object pointer-table lookup. The remaining functions classify the
// current mode (czs2-active / objective-mode / spawn zone / sound zone) and
// query the czs2 tracked entity for HUD/pose data.
// Source: TimeSplitters: Future Perfect (GameCube, main.dol)
// ===========================================================================

// --- Additional file-local externs for batch_693 --------------------------
// Per-mode soundpad zone-id tables (0x20-stride, indexed by the czs2 phase id).
extern int  DAT_8045fe8c;   // czs2 modes 6 zone-id table base
extern int  DAT_8045ff4c;   // czs2 mode 7 zone-id table base
extern int  DAT_8045ffcc;   // czs2 mode 8 zone-id table base
extern int  FLOAT_806186fc; // yaw offset applied to the tracked-entity angle
// Mode -> objective-object pointer table (indexed 0..0x14) + extended overflow
// table for indices >= 0x15.
extern int  PTR_DAT_80461ee8; // primary mode-object pointer table base
extern int  DAT_804653bc;     // extended mode-object pointer table base
// Predicates / math helpers.
extern int  game_is_mapmaker_multiplayer(void);
extern void dir_to_angles(int dirVec, int flags, float *outAngles);

// ---------------------------------------------------------------------------

// 0x8022d5a0 - Return the spawn-zone/team selector for a player record under the
// current objective mode: modes 0/2 and 0xc-0xe use zone 5, mode 1 uses zone 7.
// Returns -1 (ineligible) when the player is over the active count or is flagged
// out via the linked control block (+0xbf0, state +0x54 bit 0x80).
unsigned int worm_mode_get_spawn_zone(int playerRec)
{
    if (((playerRec != 0) && (*(int *)(playerRec + 8) < DAT_80612610)) ||
        ((*(int *)(playerRec + 0xbf0) != 0) &&
         ((*(unsigned int *)(*(int *)(playerRec + 0xbf0) + 0x54) & 0x80) != 0))) {
        return 0xffffffff;
    }
    if (DAT_805015d8 == 2) {
        return 5;
    }
    if (DAT_805015d8 < 2) {
        if (DAT_805015d8 == 0) {
            return 5;
        }
        if (DAT_805015d8 >= 0) {
            return 7;
        }
    } else if ((DAT_805015d8 < 0xf) && (0xb < DAT_805015d8)) {
        return 5;
    }
    return 0xffffffff;
}

// 0x8022d634 - Resolve the soundpad zone handle for the current czs2 sub-mode
// (6/7/8), reading the per-mode zone-id table (DAT_8045fe8c / ff4c / ffcc,
// 0x20-stride) indexed by the czs2 object's active phase id, then mapping it via
// soundpad_find_zone_by_id. Returns -1 for non-czs2 modes.
int worm_mode_resolve_sound_zone(void)
{
    int phase = *(int *)(DAT_806128b8 + *(int *)DAT_80611d74 * 0x34 + 4);
    if (DAT_805015d8 == 7) {
        return soundpad_find_zone_by_id(*(int *)((int)&DAT_8045ff4c + phase * 0x20));
    }
    if (DAT_805015d8 < 7) {
        if (5 < DAT_805015d8) {
            return soundpad_find_zone_by_id(*(int *)((int)&DAT_8045fe8c + phase * 0x20));
        }
    } else if (DAT_805015d8 < 9) {
        return soundpad_find_zone_by_id(*(int *)((int)&DAT_8045ffcc + phase * 0x20));
    }
    return -1;
}

// 0x8022d71c - Return 1 when the current mode is a czs2 sub-mode (6-8) and the
// session is not a mapmaker multiplayer game, else 0.
unsigned int worm_mode_is_czs2_active(void)
{
    if (game_is_mapmaker_multiplayer() != 0) {
        return 0;
    }
    if ((DAT_805015d8 < 9) && (5 < DAT_805015d8)) {
        return 1;
    }
    return 0;
}

// 0x8022d778 - Return 1 when the current mode is a scored objective mode:
// indices 0..0xb, 0xf..0x14 (excluding the 0xc..0xe HUD-marker band and 0x11
// upload). Else 0.
unsigned int worm_mode_is_objective_mode(void)
{
    if (DAT_805015d8 == 0x11) {
        return 0;
    }
    if (DAT_805015d8 < 0x11) {
        if (DAT_805015d8 < 0xc) {
            if (DAT_805015d8 >= 0) {
                return 1;
            }
        } else if (0xe < DAT_805015d8) {
            return 1;
        }
    } else if (DAT_805015d8 < 0x15) {
        return 1;
    }
    return 0;
}

// 0x8022d7c8 - Test bit `bit` in the entity flag word at (+0xa0). Returns
// true when the bit is set.
bool worm_entity_test_flag_bit(int entity, int bit)
{
    return (*(unsigned int *)(entity + 0xa0) & (1 << bit)) != 0;
}

// 0x8022d7e4 - Decide whether an entity is eligible for scoring/interaction
// under the current objective mode. Only applies when the objective flag is set
// (DAT_805015c0 & 0x40); each mode band imposes its own entity-state gate on the
// entity's kind (+0x20) / control block. Returns 1 (eligible) by default.
unsigned int worm_mode_entity_eligible(int *entity)
{
    if ((DAT_805015c0 & 0x40) != 0) {
        if (DAT_805015d8 == 0xf) {
            if ((entity[8] != 3) && (entity[8] != 2)) {
                return 0;
            }
        } else if (DAT_805015d8 < 0xf) {
            if ((DAT_805015d8 < 0xc) && (9 < DAT_805015d8)) {
                return 0;
            }
        } else if ((DAT_805015d8 < 0x11) &&
                   ((entity[8] != 5) || (*(int *)(*entity + 0x20c) == 0))) {
            return 0;
        }
    }
    return 1;
}

// 0x8022d878 - Fill the czs2 tracked-entity pose: position (3 floats) from the
// tracked entity's world coords (+0x64/+0x68/+0x6c) and a yaw angle derived from
// its facing direction (via dir_to_angles), biased by FLOAT_806186fc.
void worm_czs2_get_target_pose(unsigned int *outPos, float *outAngle)
{
    float angles[5];

    angles[0] = FLOAT_806185c4;
    int trackedEnt = *(int *)(DAT_806128b8 + *(int *)DAT_80611d74 * 0x34 + 8);
    int dirRec = *(int *)(*(int *)(trackedEnt + 4) + 0x10);
    outPos[0] = *(unsigned int *)(trackedEnt + 100);
    outPos[1] = *(unsigned int *)(*(int *)(DAT_806128b8 + *(int *)DAT_80611d74 * 0x34 + 8) + 0x68);
    outPos[2] = *(unsigned int *)(*(int *)(DAT_806128b8 + *(int *)DAT_80611d74 * 0x34 + 8) + 0x6c);
    if (dirRec != 0) {
        dir_to_angles(dirRec + 0x20, 0, angles);
    }
    *outAngle = angles[0] - FLOAT_806186fc;
}

// 0x8022d938 - Scale an objective descriptor's raw target value (+0x10) by the
// active player count when the descriptor's aggregation field (+4) is 0 or 1
// (per-player targets scale with player count); otherwise return it verbatim.
// This is the target-normalizer used throughout the objective evaluation core.
int worm_objective_scale_target(int descRow)
{
    if ((*(int *)(descRow + 4) != 0) && (*(int *)(descRow + 4) != 1)) {
        return *(int *)(descRow + 0x10);
    }
    return *(int *)(descRow + 0x10) * DAT_806109c0;
}

// 0x8022d964 - For czs2 sub-modes (6-8), render the on-screen tracking arrow.
void worm_czs2_hud_arrow_hook(void)
{
    if ((DAT_805015d8 < 9) && (5 < DAT_805015d8)) {
        worm_czs2_hud_arrow_render();
    }
}

// 0x8022d9a4 - Mode -> objective-object pointer lookup. For mode indices 0..0x14
// index the primary pointer table PTR_DAT_80461ee8; higher indices fall through
// to the extended table DAT_804653bc. This is the accessor behind the
// __worm_mode_object* aliases used across this module.
unsigned char *worm_mode_get_object(int mode)
{
    if (mode < 0x15) {
        return (unsigned char *)(&PTR_DAT_80461ee8)[mode];
    }
    return *(unsigned char **)((int)&DAT_804653bc + mode * 4);
}

// ===========================================================================
// Batch: batch_694
// Source: TimeSplitters: Future Perfect (GameCube, main.dol)
// The objective-class member enumeration + per-frame objective state machine.
// These functions sit on top of the class registry (DAT_806120c4, 0xa8 stride)
// and the objective-slot table (DAT_806128ac, 0x4c stride) established by the
// accessors in earlier batches. They collect the entities that belong to an
// objective class, resolve the nearest member, push objective HUD strings/barks,
// evaluate completion conditions, and finally diff the computed member bitmask
// against last frame to drive switch/toggle/damage/complete side effects.
// ===========================================================================

// --- batch_694 file-local externs (raw symbols not declared above) ---------
extern int  *DAT_806120c4;    // objective-class registry: *DAT_806120c4 = count;
                              //   records at (base + idx*0xa8 + 4), 0x2a int stride
extern int   DAT_80611c24;    // entity-handle table base (0x2e8 stride, +0x2c gen)
extern int   DAT_80611b1c;    // per-entity "spawn gib" flag byte table base
extern int   DAT_8061208c;    // active level-info record pointer
extern int   DAT_80611cd0;    // pickup/prop instance array base (0xd8 stride)
extern int   DAT_80611cd4;    // pickup/prop instance count
extern int   DAT_80611b94;    // vehicle zone-record table base (biased)
extern int   DAT_80611b98;    // turret zone-record table base (biased)
extern int   DAT_80611c14;    // generic zone-record count bound

extern float FLOAT_80618700;  // "alive/eligible" health threshold (also 0.0f sentinel)
extern float FLOAT_8061870c;  // max zone-distance cutoff for nearest-member search
extern float FLOAT_80618704;  // yaw scale numerator (member facing)
extern float FLOAT_80618708;  // yaw scale denominator
extern float FLOAT_80618710;  // owner-stat weight passed to FUN_80237b50
extern float FLOAT_80618714;  // RNG stamp base
extern float FLOAT_80618718;  // RNG stamp range
extern float FLOAT_8061871c;  // RNG normaliser divisor
extern float FLOAT_80618720;  // damage-dispatch scale for auto-fail members
extern double DOUBLE_80618728; // RNG i2f bias constant (0x43300000 magic)

extern unsigned char *PTR_DAT_80477bd0; // per-object-type descriptor pointer table (0x31 stride)

// Named statlvl / objective helpers (raw addresses, this-TU signatures)
extern unsigned int  statlvl_class_is_state5(int classRec);
extern unsigned int *statlvl_resolve_objective_string(unsigned int a, unsigned int b, unsigned int c,
                                                      unsigned int d, unsigned int e, unsigned int f,
                                                      unsigned int g, unsigned int h, unsigned int stringId);
extern void          statlvl_class_complete(double w, unsigned int b, unsigned int c, unsigned int d,
                                            unsigned int e, unsigned int f, unsigned int g, unsigned int h);
extern int           sound_resolve_sample_id(unsigned int objectiveStringId);
extern void          aiactor_bark_queue_insert(int sampleId, int priority);
extern void          gib_trail_spawn(void);
extern void          switch_toggle_state(int entity);
extern void          damage_dispatch_to_entity(double scale, double b, double c, unsigned int d,
                                               unsigned int e, unsigned int f, unsigned int g,
                                               unsigned int h, int *req);

// Soundpad / misc helpers used here
extern void  soundpad_get_entity_position(int entity, float *outPos);
extern int   soundpad_find_nearest_entity(double radius, float *pos);
extern int   soundpad_get_entity_zone_id(int zoneNode);

// Cross-module objective/type helpers
extern double FUN_8027919c(int zoneA, int zoneB);       // zone graph distance
extern void   FUN_8034f194(int *outIds, float *outPos, unsigned int *outPtrs); // mode3 member fill
extern int    FUN_8034eb18(void);                       // mode3 completion predicate
extern unsigned int FUN_80366060(void);                 // "objectives active this frame" gate
extern void   mem_free_top(int block);                  // pop the top-of-stack scratch alloc
extern int   *FUN_8034e480(int classIndex);             // resolve objective-object list for class
extern unsigned int FUN_8034e980(int objectRow);        // object "captured/active" predicate
extern int    FUN_8034946c(int handle);                 // resolve object instance from handle
extern int    FUN_8034b694(int inst);                   // resolve object sub-record
extern int    FUN_80300b30(int typeId);                 // resolve type descriptor index
extern int    FUN_80349794(unsigned char *desc, int outArr, int mode, int typeRec); // enumerate members

// Ghidra varargs spill save/restore intrinsics (no-op in the recomp).
extern void FUN_803826d0(void);
extern void FUN_8038271c(void);
extern void FUN_803826dc(void);
extern void FUN_80382728(void);

// Forward declarations for the within-batch helpers (defined below).
int  worm_class_find_first_active(int **outRec);
void worm_class_collect_members(int *outIds, float *outPos, unsigned int *outPtrs, int maxCount);
void worm_objective_show_string(unsigned int a2, unsigned int a3, unsigned int a4, unsigned int a5,
                                unsigned int a6, unsigned int a7, unsigned int a8, int objInst);
void worm_objective_mark_captured(int carrier, int objInst);

// 0x8022d9d8 - Objective descriptor accessor by index. Descriptors live in two
// 0x38-stride tables split at index 7: indices 0..6 index the table at
// 0x80461f3c, indices >=7 index the table at 0x80462218. Returns the row address.
int worm_objective_get_descriptor(int index)
{
    if (index < 7) {
        return index * 0x38 + (int)0x80461f3c;
    }
    return (index - 7) * 0x38 + (int)0x80462218;
}

// 0x8022da0c - Return the index of the first objective class that is NOT yet in
// state 5 (completed). Only meaningful in the story/single-player context
// (DAT_805015bd == 3); otherwise 0. If every class is complete, returns the last
// index (count - 1). Records are at (DAT_806120c4 + idx*0xa8 + 4).
int worm_class_find_first_active_index(void)
{
    int index;
    unsigned int state5;
    int off;

    if (DAT_805015bd == 3) {
        off = 0;
        for (index = 0; index < *DAT_806120c4; index = index + 1) {
            state5 = statlvl_class_is_state5(*(int *)((int)DAT_806120c4 + off + 4));
            if (state5 == 0) {
                return index;
            }
            off = off + 0xa8;
        }
        return *DAT_806120c4 + -1;
    }
    return 0;
}

// 0x8022da98 - Same scan as above but returns the class record POINTER of the
// first not-yet-completed objective class (or the last record if all complete).
// Returns null outside the story context. This is the active-objective-class
// resolver reused by the member enumeration and completion evaluators below.
int worm_class_find_first_active(int **outRec)
{
    int *rec;
    unsigned int state5;
    int off;
    int index;

    if (DAT_805015bd == 3) {
        off = 0;
        for (index = 0; index < *DAT_806120c4; index = index + 1) {
            rec = (int *)((int)DAT_806120c4 + off + 4);
            state5 = statlvl_class_is_state5(*rec);
            if (state5 == 0) {
                if (outRec) *outRec = rec;
                return (int)rec;
            }
            off = off + 0xa8;
        }
        rec = DAT_806120c4 + (*DAT_806120c4 + -1) * 0x2a + 1;
    } else {
        rec = (int *)0;
    }
    if (outRec) *outRec = rec;
    return (int)rec;
}

// 0x8022db38 - Collect up to maxCount members of the active objective class into
// the caller's parallel output arrays: entity ids (outIds), world positions
// (outPos, xyz triples) and entity pointers (outPtrs). The active class is
// resolved via worm_class_find_first_active; its member-source mode is the
// record's field [1]:
//   mode 2: iterate the 10-slot member table at objectSlot+0x54, computing world
//           position (rotate the local hardpoint by the member yaw when the pack
//           index +0x50 is non-negative, else use the cached +0x1a8 offset).
//   mode 0: iterate the same slot table, keep members whose "alive" value
//           (+0x1cc) exceeds FLOAT_80618700, report cached position.
//   mode 1: single soundpad-tracked entity; find the nearest entity to it.
//   mode 3 (<4): delegate to FUN_8034f194.
void worm_class_collect_members(int *outIds, float *outPos, unsigned int *outPtrs, int maxCount)
{
    int *rec;
    int mode;
    int slotBase;
    int slotIdx;
    int member;
    int packRec;
    unsigned int packIdx;
    int idOff;
    int posOff;
    int wrote;
    int loop;
    int *poseRec;
    float *pose;
    float mtx[28];
    float px, py, pz;
    float trackPos[3];

    FUN_803826d0();
    rec = (int *)0;
    worm_class_find_first_active(&rec);
    mode = rec[1];
    slotBase = DAT_806128ac + *rec * 0x4c;

    if (mode == 2) {
        slotBase = *(int *)(slotBase + 0xc);
        wrote = 0;
        posOff = 0;
        idOff = 0;
        loop = 0;
        slotIdx = 0;
        while ((loop < 10) && (wrote < maxCount)) {
            member = *(int *)(slotBase + slotIdx + 0x54);
            if (member != 0) {
                packRec = *(int *)(member + 0x7c);
                if (*(int *)(packRec + 0x18) == 0) {
                    if (outIds != (int *)0) {
                        *(int *)((int)outIds + idOff) = *(int *)(packRec + 4);
                    }
                    if (outPos != (float *)0) {
                        packIdx = *(unsigned int *)(packRec + 0x50);
                        if ((int)packIdx < 0) {
                            px = *(float *)(member + 0x1a8) + *(float *)(member + 100);
                            py = *(float *)(member + 0x1ac) + *(float *)(member + 0x68);
                            pz = *(float *)(member + 0x1b0) + *(float *)(member + 0x6c);
                        } else {
                            poseRec = *(int **)(*(int *)(member + 4) + 4);
                            pose = (float *)(poseRec[*poseRec * -0x30 + ((int)packIdx >> 0x10) * 0x30 + 0x1b] +
                                             (packIdx & 0xffff) * 0x18);
                            px = pose[0];
                            py = pose[1];
                            pz = pose[2];
                            mtx4_set_rotation_y((double)((FLOAT_80618704 * *(float *)(member + 0x70)) /
                                                         FLOAT_80618708), mtx);
                            mtx4_transform_vector(mtx, &px);
                            px = px + *(float *)(member + 100);
                            py = py + *(float *)(member + 0x68);
                            pz = pz + *(float *)(member + 0x6c);
                        }
                        pose = (float *)((int)outPos + posOff);
                        pose[0] = px;
                        pose[1] = py;
                        pose[2] = pz;
                    }
                    if (outPtrs != (unsigned int *)0) {
                        *(int *)((int)outPtrs + idOff) = member;
                    }
                    wrote = wrote + 1;
                    posOff = posOff + 0xc;
                    idOff = idOff + 4;
                }
            }
            loop = loop + 1;
            slotIdx = slotIdx + 4;
        }
    } else if (mode < 2) {
        if (mode == 0) {
            slotBase = *(int *)(slotBase + 0xc);
            wrote = 0;
            posOff = 0;
            idOff = 0;
            loop = 0;
            slotIdx = 0;
            while ((loop < 10) && (wrote < maxCount)) {
                member = *(int *)(slotBase + slotIdx + 0x54);
                if ((member != 0) && (FLOAT_80618700 < *(float *)(member + 0x1cc))) {
                    if (outIds != (int *)0) {
                        *(int *)((int)outIds + idOff) = *(int *)(member + 0x110);
                    }
                    if (outPos != (float *)0) {
                        pose = (float *)((int)outPos + posOff);
                        pose[0] = *(float *)(member + 0x1a8) + *(float *)(member + 100);
                        pose[1] = *(float *)(member + 0x1ac) + *(float *)(member + 0x68);
                        pose[2] = *(float *)(member + 0x1b0) + *(float *)(member + 0x6c);
                    }
                    if (outPtrs != (unsigned int *)0) {
                        *(int *)((int)outPtrs + idOff) = member;
                    }
                    wrote = wrote + 1;
                    posOff = posOff + 0xc;
                    idOff = idOff + 4;
                }
                loop = loop + 1;
                slotIdx = slotIdx + 4;
            }
        } else if (mode >= 0) {
            soundpad_get_entity_position(rec[0x1e], trackPos);
            if (outIds != (int *)0) {
                outIds[0] = soundpad_find_nearest_entity((double)FLOAT_80618700, trackPos);
            }
            if (outPos != (float *)0) {
                outPos[0] = trackPos[0];
                outPos[1] = trackPos[1];
                outPos[2] = trackPos[2];
            }
            if (outPtrs != (unsigned int *)0) {
                outPtrs[0] = 0;
            }
        }
    } else if (mode < 4) {
        FUN_8034f194(outIds, outPos, outPtrs);
    }
    FUN_8038271c();
}

// 0x8022de70 - Find the class member nearest (by zone-graph distance) to the
// current level-state reference entity. Collects up to 10 member ids via
// worm_class_collect_members, resolves each member's zone id, and keeps the one
// with the smallest FUN_8027919c distance under the FLOAT_8061870c cutoff.
// Returns the winning member entity id (-1 if none). NON-MATCHING: Ghidra typed
// this void, but it returns the selected id through the frame.
int worm_class_find_nearest_member(void)
{
    int refEntity;
    int count;
    int *zoneNode;
    int refZone;
    int memberZone;
    int best;
    double bestDist;
    double dist;
    int i;
    int off;
    int members[20];

    refEntity = (int)FUN_803826e4;  /* spill-frame reference entity handle */
    (void)refEntity;
    FUN_803826e4();
    worm_class_collect_members(members, (float *)0, (unsigned int *)0, 10);
    count = 0;
    /* worm_class_collect_members reports its count through the shared spill
       frame; re-derive it here by scanning the id array for the terminator. */
    while ((count < 10) && (members[count] != 0)) {
        count = count + 1;
    }
    bestDist = (double)FLOAT_8061870c;
    best = -1;
    zoneNode = soundpad_resolve_zone_node(*(int *)DAT_80611d74);
    refZone = soundpad_get_entity_zone_id((int)zoneNode);
    off = 0;
    for (i = 0; i < count; i = i + 1) {
        zoneNode = soundpad_resolve_zone_node(*(int *)((int)members + off));
        memberZone = soundpad_get_entity_zone_id((int)zoneNode);
        dist = FUN_8027919c(refZone, memberZone);
        if ((best == -1) || (dist < bestDist)) {
            best = *(int *)((int)members + off);
            bestDist = dist;
        }
        off = off + 4;
    }
    FUN_80382730();
    return best;
}

// 0x8022df38 - Push the current objective's HUD string (and matching AI bark) for
// one objective instance. The objective's display descriptor is reached via
// objInst->[0x14]->[0x7c]->[0x84]; value 1 uses the class string id at slot+2,
// value 2 uses slot+4. The resolved string is submitted to HUD channel 3 for the
// instance's player (objInst+4). When the map/mode is not the "silent" id 100,
// the string's sample is queued as a priority-5 bark.
void worm_objective_show_string(unsigned int a2, unsigned int a3, unsigned int a4, unsigned int a5,
                                unsigned int a6, unsigned int a7, unsigned int a8, int objInst)
{
    int *rec;
    int descKind;
    unsigned int *str;
    int slot;
    int sample;

    rec = (int *)0;
    worm_class_find_first_active(&rec);
    descKind = *(int *)(*(int *)(*(int *)(objInst + 0x14) + 0x7c) + 0x84);
    slot = DAT_806128ac + *rec * 0x4c;
    if (descKind == 1) {
        str = statlvl_resolve_objective_string(a2, a3, a4, a5, a6, a7, a8,
                                               *(unsigned int *)(objInst + 4),
                                               (unsigned int)*(unsigned short *)(slot + 2));
        hud_text_submit(*(int *)(objInst + 4), 3, str);
        if (DAT_80612614 != 100) {
            sample = sound_resolve_sample_id((unsigned int)*(unsigned short *)(slot + 2));
            aiactor_bark_queue_insert(sample, 5);
        }
    } else if (descKind == 2) {
        str = statlvl_resolve_objective_string(a2, a3, a4, a5, a6, a7, a8,
                                               *(unsigned int *)(objInst + 4),
                                               (unsigned int)*(unsigned short *)(slot + 4));
        hud_text_submit(*(int *)(objInst + 4), 3, str);
        if (DAT_80612614 != 100) {
            sample = sound_resolve_sample_id((unsigned int)*(unsigned short *)(slot + 4));
            aiactor_bark_queue_insert(sample, 5);
        }
    }
}

// 0x8022e014 - Objective completion evaluator. Dispatched on a mode word passed
// through the spill frame:
//   mode 4: an objective's display string changed -> re-broadcast the objective
//           string to every active player (worm_objective_show_string) and reset
//           the render/collision bits on the class members (spawning gib trails
//           unless suppressed by DAT_805015bd == '\n').
//   modes 2 < m < 4: test the specific class's completion condition by its type
//           field piVar12[1]:
//             1 -> "players in zone" count (local players + the DAT_80588610 list)
//             0 -> member fill-count reaches +0x18 target
//             2 -> all members attached
//             3 -> FUN_8034eb18 predicate
//           on success: statlvl_class_complete + FUN_80237b50 owner stat.
// Saves/restores the DAT_80611d74 level-state reference around the scan.
void worm_objective_evaluate_completion(unsigned int a2, unsigned int a3, unsigned int a4,
                                        unsigned int a5, unsigned int a6, unsigned int a7,
                                        unsigned int a8)
{
    float aliveThresh;
    int modeWord;
    unsigned short *modeRec;
    int *classRec;
    int savedState;
    int recOff;
    int i;
    int handle;
    int packRec;
    int *classPtr;
    unsigned int gen;
    int j;
    int k;
    int filled;
    int captured;
    int total;
    int guard;
    int slotBase;
    int member;
    unsigned int *objPtr;
    int local_7c[8];
    int local_5c[21];

    /* modeWord/modeRec arrive via the varargs spill frame. */
    FUN_803826dc();
    modeRec = (unsigned short *)0;
    modeWord = 0;
    savedState = DAT_80611d74;

    if (modeWord == 4) {
        classRec = (int *)0;
        worm_class_find_first_active(&classRec);
        savedState = DAT_80611d74;
        if (*(unsigned short *)(DAT_806128ac + *classRec * 0x4c) != (modeRec ? *modeRec : 0)) {
            recOff = 0;
            for (i = 0; i < DAT_80612610; i = i + 1) {
                DAT_80611d74 = DAT_80611d78 + recOff;
                if (-1 < *(int *)(DAT_80611d74 + 4)) {
                    worm_objective_show_string(a2, a3, a4, a5, a6, a7, a8, DAT_80611d74);
                }
                recOff = recOff + 0x141c;
            }
            recOff = 0;
            for (i = 0; i < classRec[0x14]; i = i + 1) {
                handle = FUN_8023c32c(*(unsigned int *)((int)classRec + recOff + 0x54));
                if (handle != 0) {
                    packRec = *(int *)(handle + 0x7c);
                    if (packRec != 0) {
                        *(unsigned int *)(packRec + 0x10) = *(unsigned int *)(packRec + 0x10) & 0xfffffffd;
                        if (*(int *)(packRec + 0xc) == 0x800) {
                            *(unsigned int *)(handle + 0x28) = *(unsigned int *)(handle + 0x28) | 2;
                            *(unsigned int *)(*(int *)(packRec + 0xd4) + 0x28) =
                                *(unsigned int *)(*(int *)(packRec + 0xd4) + 0x28) | 1;
                        } else {
                            *(unsigned int *)(handle + 0x28) = *(unsigned int *)(handle + 0x28) | 3;
                        }
                        if ((DAT_805015bd != '\n') &&
                            (*(char *)(DAT_80611b1c + *(int *)(handle + 0x34)) != '\0')) {
                            gib_trail_spawn();
                        }
                    }
                }
                recOff = recOff + 4;
            }
        }
    } else {
        savedState = DAT_80611d74;
        if ((modeWord < 4) && (2 < modeWord)) {
            classPtr = (int *)((int)DAT_806120c4 + (unsigned int)(modeRec ? *modeRec : 0) * 0xa8 + 4);
            classRec = (int *)0;
            worm_class_find_first_active(&classRec);
            savedState = DAT_80611d74;
            if ((classPtr == classRec) &&
                (modeRec != 0) &&
                (*(char *)((int)modeRec + 0xb) != '\x05') && (*(char *)((int)modeRec + 0xb) != '\x04') &&
                (FUN_80366060() != 0)) {
                aliveThresh = FLOAT_80618700;
                savedState = DAT_80611d74;
                modeWord = classPtr[1];
                if (modeWord == 1) {
                    double thresh = (double)FLOAT_80618700;
                    recOff = 0;
                    for (i = 0; i < DAT_80612610; i = i + 1) {
                        member = *(int *)(DAT_80611d78 + recOff + 0x14);
                        if ((thresh < (double)*(float *)(member + 0x1cc)) &&
                            (member = *(int *)(member + 0x7c), *(int *)(member + 0x84) == 1)) {
                            int *zn = soundpad_resolve_zone_node(*(int *)(member + 0x678));
                            int mz = soundpad_get_entity_zone_id((int)zn);
                            k = 0;
                            for (j = 0; j < classPtr[0x1d]; j = j + 1) {
                                if (mz == *(int *)((int)classPtr + k + 0x78)) {
                                    FUN_80237b50((double)FLOAT_80618710, 0x51, modeWord);
                                    statlvl_class_complete((double)FLOAT_80618700, a2, a3, a4, a5, a6, a7, a8);
                                }
                                k = k + 4;
                            }
                        }
                        recOff = recOff + 0x141c;
                    }
                    recOff = DAT_80612610 << 2;
                    for (i = DAT_80612610; savedState = DAT_80611d74, i < DAT_80612098; i = i + 1) {
                        classPtr = *(int **)((int)&DAT_80588610 + recOff);
                        if ((classPtr[0x21] == 1) && (FLOAT_80618700 < *(float *)(*classPtr + 0x1cc))) {
                            int *zn = soundpad_resolve_zone_node(classPtr[0x19e]);
                            int mz = soundpad_get_entity_zone_id((int)zn);
                            k = 0;
                            for (j = 0; j < classPtr[0x1d]; j = j + 1) {
                                if (mz == *(int *)((int)classPtr + k + 0x78)) {
                                    statlvl_class_complete((double)FLOAT_80618700, a2, a3, a4, a5, a6, a7, a8);
                                }
                                k = k + 4;
                            }
                        }
                        recOff = recOff + 4;
                    }
                } else if (modeWord == 0) {
                    slotBase = *(int *)(modeRec + 6);
                    j = 0;
                    filled = 0;
                    k = 0;
                    recOff = 0;
                    guard = 10;
                    do {
                        member = *(int *)(slotBase + recOff + 0x54);
                        if ((member == 0) || (aliveThresh < *(float *)(member + 0x1cc))) break;
                        gen = *(unsigned int *)(member + 0x214);
                        if (gen == 0) {
                            handle = 0;
                        } else {
                            handle = DAT_80611c24 + (gen >> 0x16) * 0x2e8;
                            if (*(unsigned int *)(handle + 0x2c) != gen) {
                                handle = 0;
                                *(unsigned int *)(member + 0x214) = 0;
                            }
                        }
                        if ((handle != 0) && (*(unsigned int **)(handle + 0x150) != (unsigned int *)0)) {
                            filled = filled + 1;
                            *(unsigned int *)((int)local_7c + k) = **(unsigned int **)(handle + 0x150);
                            k = k + 4;
                        }
                        j = j + 1;
                        recOff = recOff + 4;
                        guard = guard + -1;
                    } while (guard != 0);
                    savedState = DAT_80611d74;
                    if ((j != *(int *)(modeRec + 0x16)) &&
                        (*(int *)(modeRec + 0x16) = j, savedState = DAT_80611d74,
                         *(int *)(modeRec + 0x18) == *(int *)(modeRec + 0x16))) {
                        statlvl_class_complete((double)FLOAT_80618700, a2, a3, a4, a5, a6, a7, a8);
                        recOff = 0;
                        for (i = 0; savedState = DAT_80611d74, i < filled; i = i + 1) {
                            FUN_80237b50((double)FLOAT_80618710, 0x51, *(int *)((int)local_7c + recOff));
                            recOff = recOff + 4;
                        }
                    }
                } else if (modeWord == 2) {
                    slotBase = *(int *)(modeRec + 6);
                    captured = 0;
                    k = 0;
                    total = 0;
                    filled = 0;
                    recOff = 0;
                    guard = 10;
                    do {
                        member = *(int *)(slotBase + recOff + 0x54);
                        if (member == 0) break;
                        member = *(int *)(member + 0x7c);
                        total = total + 1;
                        if (*(int *)(member + 0x18) != 0) {
                            objPtr = *(unsigned int **)(*(int *)(member + 0x1c) + 0x150);
                            if (objPtr != (unsigned int *)0) {
                                captured = captured + 1;
                                *(unsigned int *)((int)local_5c + k) = *objPtr;
                                k = k + 4;
                            }
                            filled = filled + 1;
                        }
                        recOff = recOff + 4;
                        guard = guard + -1;
                    } while (guard != 0);
                    if (total == filled) {
                        statlvl_class_complete((double)FLOAT_80618700, a2, a3, a4, a5, a6, a7, a8);
                        recOff = 0;
                        for (i = 0; savedState = DAT_80611d74, i < captured; i = i + 1) {
                            FUN_80237b50((double)FLOAT_80618710, 0x51, *(int *)((int)local_5c + recOff));
                            recOff = recOff + 4;
                        }
                    }
                } else if ((modeWord == 3) && (FUN_8034eb18() != 0)) {
                    savedState = DAT_80611d74;
                    statlvl_class_complete((double)FLOAT_80618700, a2, a3, a4, a5, a6, a7, a8);
                    savedState = DAT_80611d74;
                }
            }
        }
    }
    DAT_80611d74 = savedState;
    FUN_80382728();
}

// 0x8022e534 - Mark an objective carrier item as captured: set the objective
// instance's captured flag (+0x20 = 1), OR in its "collected" bit (+0xc |= 0x10),
// and clear the carrier entity's render/collision bits (+0x28 &= ~3).
void worm_objective_mark_captured(int carrier, int objInst)
{
    *(int *)(objInst + 0x20) = 1;
    *(unsigned char *)(objInst + 0xc) = *(unsigned char *)(objInst + 0xc) | 0x10;
    *(unsigned int *)(carrier + 0x28) = *(unsigned int *)(carrier + 0x28) & 0xfffffffc;
}

// 0x8022e558 - Per-frame objective state synchroniser. Two passes over the class
// registry (DAT_806120c4): pass 1 recomputes each class's live "member captured"
// bitmask (by class mode 0/1/2/<4), stamps a state-5 (complete) class with an
// RNG value in the high bits, and stores it at (base + idx*0x2a + 3). Pass 2
// diffs the freshly computed mask against last frame's stored mask
// (base + idx*0xa8 + 0xe); for every changed member it fires the registered
// class callback (DAT_806120c4[0x151]) and drives the concrete side effects:
// switch toggles, member auto-fail damage, and object capture
// (worm_objective_mark_captured). Classes whose bit 0x8000 (state-5) is set fire
// statlvl_class_complete. Finally the stored mask is refreshed to this frame.
void worm_objective_sync_state(unsigned int a1, double a2, double a3, unsigned int a4,
                               unsigned int a5, unsigned int a6, unsigned int a7, unsigned int a8)
{
    unsigned short prevMask;
    unsigned int mask;
    int *classRec;
    int recOff;
    int classIdx;
    int slotIdx;
    int bit;
    int guard;
    int member;
    int mode;
    unsigned int localMask;
    int total;
    int *objList;
    int listOff;
    int objRow;
    int subType;
    int i;
    int scratchR7;
    int *scratchR8;
    int *scratchR9;
    int scratchR10;
    unsigned int rngRaw;
    long long rngWide;
    int allocBlk;
    int memberCount;
    int idx;
    int typeIdx;
    unsigned char *typeDesc;
    int captureReq[9];

    FUN_803826b4();

    if (FUN_80366060() != 0) {
        recOff = 0;
        for (classIdx = 0; classIdx < *DAT_806120c4; classIdx = classIdx + 1) {
            mask = 0;
            classRec = (int *)((int)DAT_806120c4 + recOff + 4);
            prevMask = *(unsigned short *)((char *)classRec + 8);
            mode = classRec[1];
            if (mode == 2) {
                bit = 0;
                slotIdx = 0;
                guard = 10;
                do {
                    member = *(int *)(*(int *)(DAT_806128ac + *classRec * 0x4c + 0xc) + slotIdx + 0x54);
                    if ((member != 0) && (*(int *)(*(int *)(member + 0x7c) + 0x18) != 0)) {
                        mask = mask | (1 << bit) & 0xffffU;
                    }
                    bit = bit + 1;
                    slotIdx = slotIdx + 4;
                    guard = guard + -1;
                } while (guard != 0);
            } else if (mode < 2) {
                if (mode == 0) {
                    bit = 0;
                    slotIdx = 0;
                    guard = 10;
                    do {
                        member = *(int *)(*(int *)(DAT_806128ac + *classRec * 0x4c + 0xc) + slotIdx + 0x54);
                        if ((member == 0) || (*(float *)(member + 0x1cc) < FLOAT_80618700)) {
                            mask = mask | (1 << bit) & 0xffffU;
                        }
                        bit = bit + 1;
                        slotIdx = slotIdx + 4;
                        guard = guard + -1;
                    } while (guard != 0);
                }
            } else if (mode < 4) {
                int shift = 0;
                int width = 0;
                objList = (int *)FUN_8034e480(classIdx);
                total = objList[1];
                listOff = 0;
                for (i = 0; i < total; i = i + 1) {
                    localMask = 0;
                    objRow = *objList + listOff;
                    subType = *(int *)(*(int *)(objRow + 4) + 8);
                    if (subType < 5) {
                        if ((subType == 0) || ((subType > 0) && (subType > 2))) {
                            localMask = (FUN_8034e980(objRow) != 0);
                            width = 1;
                        } else if (subType > 0) {
                            int inst = FUN_8034946c(*(int *)(objRow + 8));
                            int sub = FUN_8034b694(*(int *)(inst + 8));
                            int n = *(int *)(sub + 4);
                            bit = 0;
                            slotIdx = 0;
                            while (n > 0) {
                                if (*(int *)(sub + slotIdx + 0x30) != 0) {
                                    localMask = localMask | (1 << bit) & 0xffffU;
                                }
                                bit = bit + 1;
                                slotIdx = slotIdx + 4;
                                n = n + -1;
                            }
                            width = *(int *)(sub + 4);
                        }
                    } else if (subType == 7) {
                        int inst = FUN_8034946c(*(int *)(objRow + 8));
                        localMask = ((*(unsigned char *)(inst + 0xc) & 0x10) != 0);
                        width = 1;
                    } else if ((6 < subType) && (subType < 10)) {
                        int lvl = level_get_info_cached(DAT_80612614);
                        int inst = FUN_8034946c(*(int *)(objRow + 8));
                        typeIdx = *(int *)(*(int *)(lvl + 0x128) + *(int *)(inst + 8) * 0x8c + 0x84);
                        typeDesc = (&PTR_DAT_80477bd0)[typeIdx * 0x31];
                        idx = FUN_80300b30(typeIdx);
                        scratchR8 = (int *)0;
                        listOff = 0;
                        int cnt = DAT_80611cd4;
                        if (0 < DAT_80611cd4) {
                            do {
                                scratchR9 = (int *)(DAT_80611cd0 + listOff);
                                scratchR10 = *scratchR9;
                                if (((*(unsigned int *)scratchR9[6] & 0x100) != 0) &&
                                    ((int *)scratchR9[5] == (int *)typeDesc) &&
                                    (scratchR9[3] == idx)) {
                                    if ((*(unsigned int *)(scratchR10 + 0x28) & 2) == 0) {
                                        localMask = localMask | (1 << (int)scratchR8) & 0xffffU;
                                    }
                                    scratchR8 = (int *)((int)scratchR8 + 1);
                                }
                                listOff = listOff + 0xd8;
                                cnt = cnt + -1;
                            } while (cnt != 0);
                        }
                        width = 1;
                    }
                    localMask = localMask << shift;
                    shift = shift + width;
                    listOff = listOff + 0xc;
                    mask = mask | localMask & 0xffff;
                }
            }
            if (statlvl_class_is_state5(*classRec) != 0) {
                mask = mask | 0x8000;
            }
            prevMask = (unsigned short)mask;
            if (mask != (unsigned int)(*(unsigned short *)((char *)classRec + 8) & 0x83ff)) {
                if (statlvl_class_is_state5(*classRec) != 0) {
                    rngRaw = rng_counter_tick();
                    a3 = (double)(float)((double)(long long)(unsigned int)rngRaw - DOUBLE_80618728);
                    a2 = (double)(float)(a3 / (double)FLOAT_8061871c);
                    rngRaw = (unsigned int)((double)FLOAT_80618718 * a2 + (double)FLOAT_80618714);
                    rngWide = (long long)(int)rngRaw;
                    (void)rngWide;
                    prevMask = prevMask | (unsigned short)((rngRaw & 0x3f) << 10);
                }
                *(unsigned short *)((int)DAT_806120c4 + *classRec * 0xa8 + 0xc + 3) = prevMask;
            }
            recOff = recOff + 0xa8;
        }
    }

    recOff = 0;
    for (classIdx = 0; classIdx < *DAT_806120c4; classIdx = classIdx + 1) {
        classRec = (int *)((int)DAT_806120c4 + recOff + 4);
        int classId = *classRec;
        short cur = *(short *)((int)DAT_806120c4 + classId * 0xa8 + 0xc + 3);
        short old = *(short *)((int)DAT_806120c4 + classId * 0xa8 + 0xe);
        if (cur != old) {
            unsigned short thisMask = *(unsigned short *)((char *)classRec + 8);
            unsigned int newBits = thisMask & 0x83ff;
            mode = classRec[1];
            unsigned int oldBits = *(unsigned short *)((char *)classRec + 0xa) & 0x83ff;
            if (mode == 2) {
                unsigned int b = 0;
                slotIdx = 0;
                int slotBase = *(int *)(DAT_806128ac + classId * 0x4c + 0xc);
                do {
                    member = *(int *)(slotBase + slotIdx + 0x54);
                    if (member != 0) {
                        member = *(int *)(member + 0x7c);
                        unsigned int nb = (int)newBits >> (b & 0x3f) & 1;
                        if (((int)oldBits >> (b & 0x3f) & 1U) != nb) {
                            if ((void *)(uintptr_t)classRec[0x151] != (void *)0) {
                                ((void (*)(int *, unsigned int))(uintptr_t)classRec[0x151])(classRec, b);
                            }
                            if ((nb != 0) && (*(int *)(member + 0x18) == 0)) {
                                switch_toggle_state(member);
                            }
                            if ((nb == 0) && (*(int *)(member + 0x18) != 0)) {
                                switch_toggle_state(member);
                            }
                        }
                    }
                    b = b + 1;
                    slotIdx = slotIdx + 4;
                } while ((int)b < 10);
            } else if (mode < 2) {
                if (mode == 0) {
                    unsigned int b = 0;
                    slotIdx = 0;
                    int slotBase = *(int *)(DAT_806128ac + classId * 0x4c + 0xc);
                    do {
                        if ((((int)oldBits >> (b & 0x3f) & 1U) == 0) &&
                            (((int)newBits >> (b & 0x3f) & 1U) != 0) &&
                            ((void *)(uintptr_t)classRec[0x151] != (void *)0)) {
                            ((void (*)(int *, unsigned int, int))(uintptr_t)classRec[0x151])
                                (classRec, b, *(int *)(slotBase + slotIdx + 0x54));
                        }
                        b = b + 1;
                        slotIdx = slotIdx + 4;
                    } while ((int)b < 10);
                }
            } else if (mode < 4) {
                unsigned int b = 0;
                objList = (int *)FUN_8034e480(recOff);
                if (objList == (int *)0) break;
                total = objList[1];
                listOff = 0;
                for (idx = 0; idx < total; idx = idx + 1) {
                    int *row = (int *)(*objList + listOff);
                    unsigned int cap = FUN_8034e980((int)row);
                    unsigned int nShift = (int)newBits >> (b & 0x3f);
                    unsigned int oShift = (int)oldBits >> (b & 0x3f);
                    subType = *(int *)(row[1] + 8);
                    if (subType < 5) {
                        if (subType == 0) {
                            int ent = *row;
                            bool act = false;
                            if (((nShift & 1) == 0) && ((oShift & 1) != 0)) {
                                act = cap != 0;
                                if ((void *)(uintptr_t)classRec[0x151] != (void *)0) {
                                    ((void (*)(int *, int, int))(uintptr_t)classRec[0x151])(classRec, idx, ent);
                                }
                            } else if (((nShift & 1) != 0) && ((oShift & 1) == 0)) {
                                act = cap == 0;
                                if ((void *)(uintptr_t)classRec[0x151] != (void *)0) {
                                    ((void (*)(int *, int, int))(uintptr_t)classRec[0x151])(classRec, idx, ent);
                                }
                            }
                            if (act) {
                                switch_slot_reset((int)captureReq);
                                captureReq[1] = *(int *)(DAT_80611d78 + 0x14);
                                *(float *)&captureReq[8] =
                                    (float)((double)FLOAT_80618720 * (double)*(float *)(ent + 0x1cc));
                                captureReq[4] = 3;
                                captureReq[3] = 3;
                                captureReq[0] = ent;
                                captureReq[2] = captureReq[1];
                                damage_dispatch_to_entity((double)FLOAT_80618720, a2, a3, a4, a5, a6, a7, a8,
                                                          captureReq);
                            }
                            b = b + 1;
                        } else if (subType > 0) {
                            if (subType < 3) {
                                int done = 0;
                                int inst = FUN_8034946c(row[2]);
                                int sub = FUN_8034b694(*(int *)(inst + 8));
                                slotIdx = 0;
                                for (i = 0; i < *(int *)(sub + 4); i = i + 1) {
                                    if (((nShift & 0xffff & (1 << i)) != 0) &&
                                        ((oShift & 0xffff & (1 << i)) == 0)) {
                                        if ((void *)(uintptr_t)classRec[0x151] != (void *)0) {
                                            ((void (*)(int *, int, int))(uintptr_t)classRec[0x151])(classRec, idx, 0);
                                        }
                                        if (*(int *)(sub + slotIdx + 0x30) == 0) {
                                            done = done + 1;
                                            *(int *)(sub + slotIdx + 0x30) = 1;
                                        }
                                        if ((*(int *)(row[1] + 8) == 1) || (done == *(int *)(sub + 4))) {
                                            *(int *)(inst + 0x20) = 1;
                                            *(int *)(inst + 0x18) = 1;
                                        }
                                    }
                                    b = b + 1;
                                    slotIdx = slotIdx + 4;
                                }
                            } else {
                                int ent = *row;
                                bool act = false;
                                bool toggleOn = false;
                                if (((nShift & 1) == 0) && ((oShift & 1) != 0)) {
                                    act = cap != 0;
                                    toggleOn = false;
                                    if ((void *)(uintptr_t)classRec[0x151] != (void *)0) {
                                        ((void (*)(int *, int, int))(uintptr_t)classRec[0x151])(classRec, idx, ent);
                                    }
                                } else if (((nShift & 1) != 0) && ((oShift & 1) == 0)) {
                                    act = cap == 0;
                                    toggleOn = act;
                                    if ((void *)(uintptr_t)classRec[0x151] != (void *)0) {
                                        ((void (*)(int *, int, int))(uintptr_t)classRec[0x151])(classRec, idx, ent);
                                    }
                                }
                                if (act && (ent != 0)) {
                                    ent = *(int *)(ent + 0x7c);
                                    if (toggleOn && (*(int *)(ent + 0x18) == 0)) {
                                        switch_toggle_state(ent);
                                    }
                                    if ((!toggleOn) && (*(int *)(ent + 0x18) != 0)) {
                                        switch_toggle_state(ent);
                                    }
                                }
                                b = b + 1;
                            }
                        }
                    } else if (subType == 7) {
                        if ((cap == 0) || ((nShift & 1) != 0)) {
                            if (((nShift & 1) != 0) && ((oShift & 1) == 0) && (cap == 0)) {
                                int found = 0;
                                int inst = FUN_8034946c(row[2]);
                                int t = *(int *)(*(int *)(DAT_8061208c + 0x128) +
                                                 *(short *)(row[1] + 0xc) * 0x8c + 0x84);
                                typeDesc = (&PTR_DAT_80477bd0)[t * 0x31];
                                int typeRec = FUN_80300b30(t);
                                int n = FUN_80349794(typeDesc, 0, 0, typeRec);
                                allocBlk = mem_alloc((n * 4 + 0xf) & 0xfffffff0, 0);
                                FUN_80349794(typeDesc, allocBlk, 0, typeRec);
                                listOff = 0;
                                if (0 < n) {
                                    do {
                                        int m10 = 0;
                                        int e = **(int **)(allocBlk + listOff);
                                        int zid = *(int *)(*(int *)(e + 0x7c) + 0x3c);
                                        if ((7999 < zid) && (zid < 0x209e)) {
                                            m10 = 1;
                                        }
                                        int *want = *(int **)(inst + 0x14);
                                        int *got;
                                        if (m10 == 0) {
                                            if ((0x209d < zid) && (zid < 0x20b2)) {
                                                got = *(int **)(DAT_80611b98 + zid * 0x20 + -0x413c0);
                                            } else if ((-1 < zid) && (zid < DAT_80611c14)) {
                                                got = *(int **)(DAT_80611b8c + zid * 0x20);
                                            } else {
                                                got = (int *)0xffffffff;
                                            }
                                        } else {
                                            got = *(int **)(DAT_80611b94 + zid * 0x20 + -0x3e800);
                                        }
                                        if (want == got) {
                                            if ((*(unsigned int *)(e + 0x28) & 3) != 0) {
                                                found = e;
                                            }
                                            break;
                                        }
                                        listOff = listOff + 4;
                                        n = n + -1;
                                    } while (n != 0);
                                }
                                if ((void *)(uintptr_t)classRec[0x151] != (void *)0) {
                                    ((void (*)(int *, int, int))(uintptr_t)classRec[0x151])(classRec, idx, found);
                                }
                                worm_objective_mark_captured(found, inst);
                                mem_free_top(allocBlk);
                            }
                            b = b + 1;
                        }
                    } else if ((6 < subType) && (subType < 10)) {
                        int inst = FUN_8034946c(row[2]);
                        int t = *(int *)(*(int *)(DAT_8061208c + 0x128) +
                                         *(short *)(row[1] + 0xc) * 0x8c + 0x84);
                        typeDesc = (&PTR_DAT_80477bd0)[t * 0x31];
                        int typeRec = FUN_80300b30(t);
                        int n = FUN_80349794(typeDesc, 0, 1, typeRec);
                        allocBlk = mem_alloc((n * 4 + 0xf) & 0xfffffff0, 0);
                        FUN_80349794(typeDesc, allocBlk, 1, typeRec);
                        listOff = 0;
                        for (i = 0; i < n; i = i + 1) {
                            int e = **(int **)(allocBlk + listOff);
                            if (((nShift & 0xffff & (1 << i)) != 0) &&
                                ((oShift & 0xffff & (1 << i)) == 0)) {
                                if ((void *)(uintptr_t)classRec[0x151] != (void *)0) {
                                    ((void (*)(int *, int, int))(uintptr_t)classRec[0x151])(classRec, idx, e);
                                }
                                if ((*(unsigned int *)(e + 0x28) & 3) != 0) {
                                    worm_objective_mark_captured(e, inst);
                                }
                            }
                            listOff = listOff + 4;
                        }
                        mem_free_top(allocBlk);
                    }
                    listOff = listOff + 0xc;
                }
            }
            if ((thisMask & 0x8000) != 0) {
                if ((void *)(uintptr_t)classRec[0x151] != (void *)0) {
                    ((void (*)(int *, int, int))(uintptr_t)classRec[0x151])(classRec, 0xf, 0);
                }
                statlvl_class_complete((double)FLOAT_80618700, a2, a3, a4, a5, a6, a7, a8);
            }
            *(short *)((int)DAT_806120c4 + classId * 0xa8 + 0xe) =
                *(short *)((int)DAT_806120c4 + classId * 0xa8 + 0xc + 3);
        }
        recOff = recOff + 0xa8;
    }
    FUN_80382700();
}

// ===========================================================================
// Batch: batch_695
// Source: TimeSplitters: Future Perfect (GameCube, main.dol)
// The level-event / objective-callback subsystem: the per-level objective
// callback resolver, the objective-class registry initializer, the two giant
// event dispatchers (a "should this event fire yet?" condition tick and the
// "make the event happen" execute dispatch), plus their small helpers
// (impulse, explosion, bark trigger/stop). These are driven from the level
// MASTER loop alongside the objective evaluators authored in batch_694.
// ===========================================================================

// --- batch_695 file-local externs (raw symbols not declared above) ---------
extern int   DAT_80612910;    // event-condition list base (ptr array)
extern int   DAT_80612914;    // event-condition list count
extern int   DAT_80612924;    // event-execute list base (0x48 stride records)
extern int   DAT_806128f8;    // event-execute list count
extern int   DAT_80612900;    // "has active sound events" gate count
extern int   DAT_80501572;    // debug/replay feature toggle byte
extern unsigned char DAT_802192d0; // level-0x30 objective callback table blob
extern float DAT_803e01c8;    // default explosion origin x
extern float DAT_803e01cc;    // default explosion origin y
extern float DAT_803e01d0;    // default explosion origin z

extern float FLOAT_8061873c;  // event-sound distance gate A
extern float FLOAT_80618738;  // gib timer reset value
extern float FLOAT_80618740;  // event-sound distance gate B
extern float FLOAT_80618744;  // 0.0f sentinel / threshold
extern float FLOAT_8061874c;  // 1.0f
extern float FLOAT_80618748;  // rng range denom A
extern float FLOAT_806108ec;  // per-event random-fire scale
extern float FLOAT_80618760;  // deg->rad numerator (rotation submesh)
extern float FLOAT_80618764;  // deg->rad denominator
extern float FLOAT_8061876c;  // anim slot speed value
extern float FLOAT_80618768;  // volume normaliser
extern float FLOAT_80618770;  // stereo pair vol
extern float FLOAT_80618774;  // stereo pair pan L
extern float FLOAT_80618778;  // stereo pair pan R
extern float FLOAT_8061877c;  // low-priority sound vol
extern float FLOAT_80618780;  // 3d sound resume vol
extern double DOUBLE_80618750; // rng i2f bias A
extern double DOUBLE_80618758; // rng i2f bias B (player-count magic)

// Named helpers this batch introduces (raw addresses, this-TU signatures)
extern int  FUN_80233510(int condRec);              // mark event condition satisfied
extern void mtx4_set_rotation_zyx(double rx, double ry, double rz, float *outMtx);
extern int  aiactor_find_player_by_team(int teamId);
extern void aiactor_play_bark_3d(int actor, int sampleId, unsigned int flags, int *ctx,
                                 int a5, int a6, int a7, int a8);
extern void aiactor_play_bark_global(int sampleId, int flags, int *ctx);
extern void aiactor_clear_bark_state(int actor);

// Per-level objective callback targets (resolved by worm_event_resolve_level_callback)
extern int mths_story_event_dispatch;          // levels 0x1b-0x1c
extern int ekun_wall_event_dispatch;           // levels 0x1c-0x1d
extern int statlvl_damage_apply_to_zone_actors; // level 0x3b

// Class-registry setup helpers
extern void statlvl_class_register_with_ids(unsigned int id, unsigned int a, short c, short d,
                                            int e, int f, int g);
extern void statlvl_class_add_member(int memberId);
extern void statlvl_class_clear_tally_flag(void);
extern void statlvl_class_set_complete_cb(void *cb);
extern int  FUN_8034e9e0(void);                 // default all-complete class action

// Spill save/restore intrinsics used by this batch (no-ops in the recomp)
extern void FUN_803826d8(void);
extern void FUN_80382724(void);

// Condition-tick (FUN_8022f49c) leaf helpers
extern int  sound_is_playing(unsigned int handle);
extern void sound_stop_handle(void);
extern int  sound_get_position(unsigned int handle, void *outPos);
extern int  FUN_802344a0(void);                 // alloc a new event-execute record
// worm_event_arm / worm_event_is_satisfied are defined in the batch_696 block
// below; forward-declared here so the batch_695 dispatchers can call them.
void worm_event_arm(double a1, double a2, double a3, double a4, double a5, double a6,
                    int a7, int a8, int *rec, int b, int c, int d, int e, int f, int g, int h);
unsigned int worm_event_is_satisfied(int *condRec);
extern void *trigger_slot_find_by_entity_id(int entityId);
extern int  trigger_is_fully_open(unsigned int slot);
extern int  trigger_is_moving(unsigned int slot);
extern int  FUN_80238fb4(int a);                // scene event ready predicate
extern void camera_splitscreen_restore(void);
extern int  object_fade_begin(int mode);
extern int  level_actor_switch(double a1, double a2, double a3, double a4, double a5, double a6,
                               int a7, int a8, int actorId);
extern unsigned long long FUN_80233de4(void);   // paired ready-flag probe
extern int  subobj_slot_occupied(int obj, int slotKind);
extern void subobj_grab_commit_attach(double a1, double a2, int obj, int carrier, int slotKind);
extern void subobj_grab_attach_release(int obj, int carrier, int a3, int a4, int a5);
extern int  spawn_point_find_by_entity(int entityId);
extern int  spectator_get_slot(void);
extern int  anim_active_slot_userflags(int slot);
extern int  anim_primary_clip_userflags(int inst, unsigned int clip);
extern void entity_register_for_update(int inst, int a, int b);
extern void entity_destroy_with_event(void *inst);

// Execute-dispatch (FUN_8022fe70) leaf helpers
extern void FUN_80234470(int rec);
extern void hud_banner_show_persistent(int a, int b);
extern void hud_banner_show(int a, int b);
extern void hud_banner_hide(void);
extern void FUN_80231220(double a1, double a2, double a3, double a4, int a5, int a6, int a7, int a8);
extern void anim_set_blend_weight(double w, int inst);
extern void anim_active_slot_set_speed(double s, int slot);
extern void render_instance_set_material(double a, int *inst, int matId);
extern void anim_play_scaled(double a, double b);
extern void FUN_80357bf0(void);
extern void sound_init_play_params(int params);
extern unsigned int sound_play_request(int *params);
extern void sound_set_pitch(double p, int handle);
extern void sound_play_or_resume_3d(double a, double b, double c, int handle, void *pos, int f);
extern void sound_play_stereo_pair(double a, double b, double c);
extern void *scene_node_get_submesh(int node, unsigned int idx);
extern int  trigger_open_full(int slot, int a);
extern void trigger_close_if_idle(int slot);
extern void trigger_open_immediate(int slot);
extern void trigger_close_immediate(int slot);
extern int  soundpad_find_pad_by_extref(int ref);
extern int  FUN_80238f9c(int idx);
extern int  cutscene_load_and_start(double a1, double a2, double a3, double a4, int a5, int a6,
                                    int a7, int a8, int defA, int defB);
extern void cutscene_begin_playback(void);
extern void player_slot_assign_pause_camera(int cam, int a, unsigned int flags);
extern void FUN_802387e4(int a, int b);
extern void FUN_80233d50(double a1, double a2, double a3, double a4, int a5, int a6, int a7, int a8,
                         int ctx, int rec, int a11, int a12, int a13, int a14, int a15, int a16);
extern void statlvl_class_force_complete(int id);
extern void statlvl_class_set_state2(int id);
extern void statlvl_class_arm(int id);
extern void statlvl_class_disarm(int id);
extern void statlvl_class_advance(int id);
extern void statlvl_class_set_state4(int id);
extern void sound_set_mute_flag(int on);
extern void sound_set_voice_base_volume(double v);
extern void sound_set_voice_volume_scale(int v);
extern int  entity_needs_respawn(int idx);
extern int  level_spawn_entity_by_id(double a1, double a2, double a3, double a4, int a5, int a6,
                                     int a7, int a8, unsigned int id);
extern void entity_restore_by_spawn_ptr(int inst);
extern int  FUN_8023c59c(int a, int outArr, int max);
extern void level_spawn_decal_by_id(double a1, double a2, double a3, double a4, int a5, int a6,
                                    int a7, int a8, unsigned int id);
extern void subobj_carry_channel_set_param(double p, int obj);
extern void *subobj_get_contact_origin(unsigned int handle);
extern int  subobj_carry_nav_block_ptr(int obj);
extern void subobj_carry_nav_init(int obj, int a, int b, int c);
extern void subobj_apply_transform_snapshot(int obj, void *xf, float *pos);
extern void FUN_80273284(void);
extern void FUN_8027317c(int a, int b, int c);
extern int *FUN_802325b8(int id);
extern void FUN_8023265c(int *rec);
extern int *switch_find_slot_by_entity(int id);
extern void FUN_802332d0(int *slot, int a, int b);
extern void FUN_802325fc(int id);
extern int  mover_find_by_entity_id(int id);
extern void mover_activate(int mover);
extern void mover_set_trigger_delay(int mover);
extern void mover_activate_linked_entities(void);
extern void FUN_8025ff4c(int a, int *rec, int b);
extern int  aiactor_find_entity_by_team(int team, int a);
extern void FUN_80262c34(int actor, int a);
extern void mths_soundzone_set_by_id(int id);
extern void player_enter_menu_state(int a, int spawn, int b, unsigned int flags, int e, int f);
extern void spectator_set_mode_if_ready(double a, int spawn);
extern int  match_process_end(double a1, double a2, double a3, double a4, int a5, int a6, int a7,
                              int a8, int a9);
extern int  aiactor_deactivate(int a);
extern void aiactor_activate(int a);
extern int  FUN_8025c9d0(int a);
extern void sound_play_persistent(int sample, int a, int b);
extern int  FUN_8023c520(int a);
extern int  player_ai_explosion_spawn(unsigned int a, unsigned int b, float *dir, float *pos,
                                      int e, int f, int g);
extern void subobj_add_linear_force(unsigned int handle, float *force);
extern void subobj_add_force_at_point(unsigned int handle, float *at, float *force);
extern int  aiactor_alert_zone_broadcast_b(double a1, double a2, double a3, double a4, int a5,
                                           int a6, int a7, int a8);
extern void hud_hint_show_reload(void);
extern void hud_hint_show_swap_controls(void);
extern void hud_hint_show_empty(void);
extern void hud_hint_show_generic(void);
extern void hud_hint_show_pickup_controls(void);
extern void hud_hint_show_grab_controls(void);
extern void hud_hint_show_mode4_controls(void);

// Forward declarations for within-batch helpers
int  worm_event_default_class_callback(int *classRec, int code);
int  worm_event_resolve_level_callback(void);
void worm_event_explosion_spawn(int *condRec);
void worm_event_apply_impulse(int *condRec);
int  worm_event_play_bark(int *ctx);
void worm_event_execute(int *ctx);

// 0x8022f15c - Default objective-class callback (installed as DAT_806120c4[0x151]
// when the level has no dedicated handler). On the all-complete notification
// (code 0xf), find the class index in the registry whose id matches *classRec
// and invoke FUN_8034e9e0.
int worm_event_default_class_callback(int *classRec, int code)
{
    int index;
    int off;
    int remaining;
    int found;

    if (code == 0xf) {
        index = 0;
        off = 0;
        remaining = *DAT_806120c4;
        found = -1;
        if (0 < remaining) {
            do {
                found = index;
                if (*(int *)((int)DAT_806120c4 + off + 4) == *classRec) break;
                index = index + 1;
                off = off + 0xa8;
                remaining = remaining + -1;
                found = -1;
            } while (remaining != 0);
        }
        if (found != -1) {
            FUN_8034e9e0();
        }
    }
    return found;
}

// 0x8022f1d8 - Resolve the objective-callback function pointer for the current
// level id (DAT_80612614). Special levels (0x1b-0x1c, 0x1c-0x1d, 0x3b, 0x30, 100)
// map to dedicated dispatchers; everything else has no callback (null).
int worm_event_resolve_level_callback(void)
{
    if (DAT_80612614 == 0x30) {
        return (int)&DAT_802192d0;
    }
    if (DAT_80612614 < 0x30) {
        if (DAT_80612614 != 0x1c) {
            if (DAT_80612614 < 0x1c) {
                if (0x1a < DAT_80612614) {
                    return (int)&mths_story_event_dispatch;
                }
            } else if (DAT_80612614 < 0x1e) {
                return (int)&ekun_wall_event_dispatch;
            }
        }
    } else {
        if (DAT_80612614 == 100) {
            return (int)worm_event_default_class_callback;
        }
        if ((DAT_80612614 < 100) && (DAT_80612614 == 0x3b)) {
            return (int)&statlvl_damage_apply_to_zone_actors;
        }
    }
    return 0;
}

// 0x8022f268 - Objective-class registry initializer. For a story level
// (DAT_805015bd==3) with a valid level-info record, walk the level's class
// definition table (info+0xe4, count info+0xe8, 0x60 stride): zero each class
// record in the registry (DAT_806120c4, 0xa8/0x2a stride), register its ids and
// members via statlvl_class_register_with_ids/add_member, copy the target-count
// and tally short arrays (defn+0x40 and defn+0x20, up to 16 each), and install
// the completion callback (worm_objective_evaluate_completion). Installs the
// per-level event callback (worm_event_resolve_level_callback) at index 0x151.
void worm_event_registry_init(void)
{
    short v;
    int classId;
    int info;
    void *cb;
    int off40;
    int off20;
    int defn;
    int kind;
    int recByte;
    int i;
    int defnOff;

    FUN_803826dc();
    info = level_get_info_cached(DAT_80612614);
    if ((info != 0) && (DAT_805015bd == 3)) {
        *DAT_806120c4 = 0;
        cb = (void *)worm_event_resolve_level_callback();
        DAT_806120c4[0x151] = (int)cb;
        defnOff = 0;
        for (i = 0; i < *(int *)(info + 0xe8); i = i + 1) {
            off40 = 0;
            off20 = 0;
            defn = *(int *)(info + 0xe4) + defnOff;
            classId = *(int *)(defn + 4);
            recByte = classId * 0xa8;
            DAT_806120c4[classId * 0x2a + 1] = classId;
            *(short *)(DAT_806120c4 + classId * 0x2a + 4) = 0;
            *(short *)((int)DAT_806120c4 + recByte + 0x12) = 0;
            DAT_806120c4[classId * 0x2a + 0x15] = 0;
            *(short *)(DAT_806120c4 + classId * 0x2a + 3) = 0;
            *(short *)((int)DAT_806120c4 + recByte + 0xe) = 0;
            DAT_806120c4[classId * 0x2a + 0x27] = 0;
            DAT_806120c4[classId * 0x2a + 2] = *(int *)(defn + 0x14);
            *DAT_806120c4 = *DAT_806120c4 + 1;
            statlvl_class_register_with_ids(*(unsigned int *)(defn + 4), *(unsigned int *)(defn + 8),
                                            (short)*(int *)(defn + 0xc), (short)*(int *)(defn + 0x10),
                                            0, 0, 0);
            kind = *(int *)(defn + 0x14);
            if (kind == 2) {
                statlvl_class_add_member(*(int *)(defn + 0x18));
            } else if (kind < 2) {
                if (kind == 0) {
                    statlvl_class_clear_tally_flag();
                    statlvl_class_add_member(*(int *)(defn + 0x18));
                } else if (kind >= 0) {
                    DAT_806120c4[classId * 0x2a + 0x1f] = *(int *)(defn + 0x1c);
                    DAT_806120c4[classId * 0x2a + 0x1e] = DAT_806120c4[classId * 0x2a + 0x1e] + 1;
                }
            }
            kind = 0x10;
            do {
                v = *(short *)(defn + off40 + 0x40);
                if (0 < v) {
                    *(short *)((int)DAT_806120c4 + recByte + off40 + 0x34) = v;
                    *(short *)((int)DAT_806120c4 + recByte + 0x12) =
                        *(short *)((int)DAT_806120c4 + recByte + 0x12) + 1;
                }
                off40 = off40 + 2;
                kind = kind + -1;
            } while (kind != 0);
            kind = 0x10;
            do {
                v = *(short *)(defn + off20 + 0x20);
                if (0 < v) {
                    *(short *)((int)DAT_806120c4 + recByte + off20 + 0x14) = v;
                    *(short *)(DAT_806120c4 + classId * 0x2a + 4) =
                        *(short *)(DAT_806120c4 + classId * 0x2a + 4) + 1;
                }
                off20 = off20 + 2;
                kind = kind + -1;
            } while (kind != 0);
            statlvl_class_set_complete_cb((void *)worm_objective_evaluate_completion);
            defnOff = defnOff + 0x60;
        }
    }
    FUN_80382728();
}

// 0x8022f49c - Event-CONDITION tick. Walks the active event-condition list
// (DAT_80612910, count DAT_80612914) and, for each armed unsatisfied condition
// (rec[0xc]==1 && rec[7]==0), evaluates its type-specific "has it happened yet?"
// test (big switch on the event descriptor's type word at *rec+8): looping/one
// -shot sound state, anim clip user-flags, trigger open/moving, grab-slot
// occupancy, spawn-point occupancy, spectator state, etc. Satisfied conditions
// call FUN_80233510. A random-fire branch (bVar2) can re-arm the paired execute
// record via worm_event_execute when its probability roll passes.
void worm_event_condition_tick(double aF1, double a2, double a3, double a4, double a5, double a6,
                               int a7, int a8)
{
    bool randomFire;
    float f;
    int handle2;
    int *rec2;
    unsigned int u;
    int inst;
    unsigned int *slot;
    unsigned int r4;
    double dv;
    unsigned int in_r6;
    int in_r7 = 0, in_r8 = 0, in_r9 = 0, in_r10 = 0;
    int i;
    int off;
    unsigned int *cond;
    unsigned int desc;
    int local48, local44, local40;

    FUN_803826d8();
    i = 0;
    off = 0;
    (void)aF1; (void)r4;
    while (true) {
        if (DAT_80612914 <= i) {
            FUN_80382724();
            return;
        }
        cond = *(unsigned int **)(DAT_80612910 + off);
        if ((cond[0xc] == 1) && (cond[7] == 0)) {
            desc = *cond;
            randomFire = false;
            if ((*(int *)(desc + 8) != 3) && (*(int *)(desc + 0x3c) != 0)) {
                u = cond[9];
                if (u == 0) {
                    inst = 0;
                } else {
                    inst = DAT_80611c24 + (u >> 0x16) * 0x2e8;
                    if (*(unsigned int *)(inst + 0x2c) != u) {
                        inst = 0;
                        cond[9] = 0;
                    }
                }
                if (inst != 0) {
                    (**(int (**)(void))(desc + 0x3c))();
                }
            }
            switch (*(int *)(desc + 8)) {
            case 0: case 6: case 0x35: case 0x36: case 0x37: case 0x38: case 0x3a:
            case 0x42: case 0x43: case 0x48: case 0x49: case 0x4a:
            case 0x58: case 0x59: case 0x5a:
                FUN_80233510((int)cond);
                break;
            case 1: {
                int sample = sound_resolve_sample_id(*(int *)(*cond + 0x10));
                unsigned int handle = cond[9];
                int rec7;
                unsigned int soundH = cond[8];
                bool present = handle != 0;
                bool did = false;
                in_r6 = 0;
                if (handle == 0) {
                    rec7 = 0;
                } else {
                    rec7 = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
                    if (*(unsigned int *)(rec7 + 0x2c) != handle) {
                        rec7 = 0;
                        cond[9] = 0;
                    }
                }
                if ((-1 < sample) || ((double)(float)cond[0xe] >= (double)FLOAT_8061873c)) {
                    if (-1 < sample) {
                        if ((double)(float)cond[0xe] <= (double)FLOAT_80618740) {
                            in_r6 = sound_is_playing(soundH);
                        } else {
                            in_r6 = 1;
                        }
                    }
                } else {
                    in_r6 = 1;
                }
                if ((rec7 == 0) || (in_r6 == 0)) {
                    if ((rec7 == 0) && present && (in_r6 != 0)) {
                        sound_stop_handle();
                        FUN_80233510((int)cond);
                        did = true;
                    } else if (in_r6 == 0) {
                        FUN_80233510((int)cond);
                        did = true;
                    }
                } else {
                    local48 = *(int *)(rec7 + 100);
                    local44 = *(int *)(rec7 + 0x68);
                    local40 = *(int *)(rec7 + 0x6c);
                    sound_get_position(soundH, &local48);
                }
                if (did && (0 < DAT_80612900)) {
                    int *nrec = (int *)FUN_802344a0();
                    worm_event_arm(aF1, a2, a3, a4, a5, a6, a7, a8, nrec, r4, handle, in_r6,
                                   in_r7, in_r8, in_r9, in_r10);
                    worm_event_play_bark(nrec);
                    nrec[0xc] = 1;
                }
                break;
            }
            case 3: case 4: case 7: {
                int *ent;
                u = cond[9];
                if (u == 0) {
                    ent = (int *)0;
                } else {
                    ent = (int *)(DAT_80611c24 + (u >> 0x16) * 0x2e8);
                    if ((unsigned int)ent[0xb] != u) {
                        ent = (int *)0;
                        cond[9] = 0;
                    }
                }
                if (ent == (int *)0) {
                    FUN_80233510((int)cond);
                    goto next;
                }
                if (*(int *)(desc + 8) == 3) {
                    if (anim_active_slot_userflags(ent[1]) != 0) {
                        if ((*(unsigned int *)(desc + 0xb0) & 0x10) == 0) {
                            if ((int (*)(void))ent[0x56] != (int (*)(void))0) {
                                ((void (*)(int *, unsigned int *))ent[0x56])(ent, cond);
                                entity_register_for_update((int)ent, 1, 0);
                            }
                            entity_destroy_with_event(ent);
                        }
                        FUN_80233510((int)cond);
                    }
                } else {
                    u = (unsigned int)*(short *)(*cond + 0x18);
                    if (anim_primary_clip_userflags((int)ent, u) != 0) {
                        FUN_80233510((int)cond);
                    }
                }
                break;
            }
            case 5:
                FUN_80233510((int)cond);
                break;
            case 10:
                randomFire = false;
                if ((*(char *)(DAT_80611b1c + *(int *)(*cond + 0x14)) != '\0') &&
                    (worm_event_is_satisfied((int *)cond) != 0)) {
                    randomFire = true;
                }
                f = FLOAT_80618738;
                *(short *)(cond + 2) = 0;
                cond[0xe] = (unsigned int)f;
                break;
            case 0x10: case 0x12: {
                slot = (unsigned int *)trigger_slot_find_by_entity_id(*(int *)(*cond + 0x14));
                if (slot != (unsigned int *)0) {
                    u = *slot;
                    unsigned int ts;
                    if (u == 0) {
                        ts = 0;
                    } else {
                        ts = DAT_80611c24 + (u >> 0x16) * 0x2e8;
                        if (*(unsigned int *)(ts + 0x2c) != u) {
                            ts = 0;
                            *slot = 0;
                        }
                    }
                    if (ts != 0) {
                        if (*(int *)(*cond + 0xc) == 0) {
                            if (trigger_is_fully_open(ts) != 0) {
                                FUN_80233510((int)cond);
                            }
                        } else {
                            if (trigger_is_moving(ts) != 0) {
                                FUN_80233510((int)cond);
                            }
                        }
                    }
                }
                break;
            }
            case 0x18:
                if (FUN_80238fb4(*(int *)(desc + 0x18)) != 0) {
                    FUN_80233510((int)cond);
                }
                break;
            case 0x19:
                if ((*(unsigned int *)(DAT_80611d54 + 0x21c) & 0x200000) != 0) {
                    FUN_80233510((int)cond);
                    camera_splitscreen_restore();
                    object_fade_begin(0);
                }
                break;
            case 0x1a:
                if (level_actor_switch(aF1, a2, a3, a4, a5, a6, a7, a8,
                                       *(int *)(desc + 0x10)) != 0) {
                    FUN_80233510((int)cond);
                }
                break;
            case 0x1c: {
                unsigned long long r = FUN_80233de4();
                if ((int)(r >> 0x20) != 0) {
                    FUN_80233510((int)cond);
                }
                break;
            }
            case 0x31: {
                int obj = FUN_8023c32c(*(unsigned int *)(*cond + 0x14));
                int slotKind = *(int *)(desc + 0xc);
                int player = *(int *)(desc + 0x10);
                if ((obj != 0) && (-1 < player) && (player < 4)) {
                    if (subobj_slot_occupied(obj, slotKind) != 0) {
                        subobj_grab_commit_attach(aF1, a2, obj,
                                                  *(int *)(DAT_80611d78 + player * 0x141c + 0x14),
                                                  slotKind);
                    }
                    FUN_80233510((int)cond);
                }
                break;
            }
            case 0x32: {
                int player = *(int *)(desc + 0x10);
                if ((-1 < player) && (player < 4)) {
                    if (*(int *)(DAT_80611d78 + player * 0x141c + 0x114) == 0x400) {
                        int obj = FUN_8023c32c(*(unsigned int *)(*cond + 0x14));
                        if (obj != 0) {
                            int carrier = *(int *)(DAT_80611d78 + player * 0x141c + 0x14);
                            if (*(unsigned int *)(obj + 0x2c) ==
                                *(unsigned int *)(*(int *)(carrier + 0x7c) + 0xc08)) {
                                subobj_grab_attach_release(obj, carrier, 0, 0, 1);
                            }
                            FUN_80233510((int)cond);
                        }
                    } else {
                        FUN_80233510((int)cond);
                    }
                }
                break;
            }
            case 0x3c: {
                int leader = *(int *)(DAT_80611d78 + 8);
                int sp = spawn_point_find_by_entity(*(int *)(desc + 0xc));
                if (*(int *)(leader + 0x26c) != sp) {
                    FUN_80233510((int)cond);
                }
                break;
            }
            case 0x3d: {
                int spec = spectator_get_slot();
                int sp;
                if (((*(unsigned int *)(spec + 0x21c) & 1) == 0) ||
                    (sp = spawn_point_find_by_entity(*(int *)(desc + 0xc)),
                     *(int *)(spec + 0x26c) != sp)) {
                    FUN_80233510((int)cond);
                }
                break;
            }
            default:
                break;
            }
            if (randomFire && ((double)*(float *)(desc + 0xa8) > (double)FLOAT_80618744)) {
                unsigned int rngRaw = rng_counter_tick();
                double numer = (double)FLOAT_8061874c;
                double denom = (double)(float)((double)(long long)(unsigned int)(DAT_806109c0)
                                               - DOUBLE_80618758);
                double pcInv = (double)(float)(numer / denom);
                double roll = (double)(float)(((double)(long long)(unsigned int)rngRaw
                                               - DOUBLE_80618750) / (double)FLOAT_80618748);
                desc = *cond;
                if (-(double)(float)(pcInv * (double)(*(float *)(desc + 0xa8) * FLOAT_806108ec)
                                     - numer) <= roll) {
                    cond[0xc] = 0;
                    worm_event_execute((int *)cond);
                }
            }
        }
    next:
        i = i + 1;
        off = off + 4;
    }
}

// 0x8022fb1c - Spawn an explosion for a "detonate" event. Resolve the world
// position from the condition's submesh bone (rotated by the bone's zyx angles)
// or the entity origin, then call player_ai_explosion_spawn and fire the
// optional per-event callback (+0x38).
void worm_event_explosion_spawn(int *condRec)
{
    int desc;
    float *bone;
    float pos[3];
    float mtx[17];

    pos[0] = DAT_803e01c8;
    pos[1] = DAT_803e01cc;
    pos[2] = DAT_803e01d0;
    desc = *condRec;
    if ((int)*(unsigned int *)(desc + 0x18) < 0) {
        pos[0] = *(float *)(desc + 0x2c);
        bone = (float *)(desc + 0x20);
        pos[1] = *(float *)(desc + 0x30);
        pos[2] = *(float *)(desc + 0x34);
    } else {
        bone = (float *)scene_node_get_submesh(*(int *)(desc + 0x14), *(unsigned int *)(desc + 0x18));
        if (bone != (float *)0) {
            mtx4_set_rotation_zyx((double)((FLOAT_80618760 * bone[3]) / FLOAT_80618764),
                                  (double)((FLOAT_80618760 * bone[4]) / FLOAT_80618764),
                                  (double)((FLOAT_80618760 * bone[5]) / FLOAT_80618764), mtx);
            mtx4_transform_vector(mtx, pos);
        }
    }
    player_ai_explosion_spawn(*(unsigned int *)(*condRec + 0x10), 0xffffffff, bone, pos, 0, 0, 1);
    if (*(int (**)(void))(*condRec + 0x38) != (int (*)(void))0) {
        (*(void (**)(int *))(*condRec + 0x38))(condRec);
    }
}

// 0x8022fc30 - Apply an impulse event: resolve the grabbed subobject and add a
// scaled linear force (or force-at-point when flag 0x4000 set) along the event's
// direction vector.
void worm_event_apply_impulse(int *condRec)
{
    int obj;
    unsigned int handle;
    int desc;
    float force[3];

    obj = FUN_8023c32c(*(unsigned int *)(*condRec + 0x14));
    desc = *condRec;
    if (obj != 0) {
        handle = *(unsigned int *)(obj + 0x80);
        if (handle != 0) {
            float scale = *(float *)(desc + 0x10);
            force[0] = *(float *)(desc + 0x2c) * scale;
            force[1] = *(float *)(desc + 0x30) * scale;
            force[2] = *(float *)(desc + 0x34) * scale;
            if ((*(unsigned int *)(desc + 0xb0) & 0x4000) == 0) {
                subobj_add_linear_force(handle, force);
            } else {
                subobj_add_force_at_point(handle, (float *)(desc + 0x20), force);
            }
        }
    }
}

// 0x8022fcc8 - Bark trigger: resolve the target actor from the event's team/
// entity/attacker fields (0x3f3f3f3f = "any"), then play a 3D bark on that
// actor's model or a global bark. Uses the spill frame for the event record.
int worm_event_play_bark(int *ctx)
{
    int satisfied;
    int desc;
    int attacker;
    int teamId;
    unsigned int attackerId;
    int sampleWord;
    int in_r7 = 0, in_r8 = 0, in_r9 = 0, in_r10 = 0;

    FUN_803826e4();
    satisfied = 0;
    desc = *ctx;
    attacker = 0;
    teamId = *(int *)(desc + 0x14);
    sampleWord = *(int *)(desc + 0x10);
    attackerId = *(unsigned int *)(desc + 0x18);
    int killerId = *(int *)(desc + 0xc);

    if (teamId != 0) {
        if ((teamId != 0x3f3f3f3f) && (satisfied = aiactor_find_player_by_team(teamId), satisfied == 0))
            goto done;
    }
    if (killerId != 0) {
        if ((killerId != 0x3f3f3f3f) && (satisfied = FUN_8023c520(killerId), satisfied == 0))
            goto done;
    }
    if (attackerId != 0) {
        if (attackerId != 0x3f3f3f3f) {
            satisfied = aiactor_find_player_by_team(attackerId);
            if ((satisfied == 0) && (satisfied = FUN_8023c32c(attackerId), satisfied == 0))
                goto done;
        }
    }
    if ((satisfied != 0) && (*(int *)(satisfied + 0x20) == 8)) {
        attacker = *(int *)(satisfied + 0x7c);
    }
    if (attacker == 0) {
        aiactor_play_bark_global(sampleWord, *(int *)(desc + 0xb0), ctx);
    } else {
        aiactor_play_bark_3d(attacker, sampleWord, *(unsigned int *)(desc + 0xb0), ctx,
                             in_r7, in_r8, in_r9, in_r10);
    }
done:
    FUN_80382730();
    return satisfied;
}

// 0x8022fdd4 - Stop event sounds/barks. When targetTeam == 0x3f3f3f3f, stop every
// active looping event sound in the condition list; otherwise clear the resolved
// actor's bark state.
void worm_event_stop_bark(int targetTeam)
{
    int *cond;
    int off;
    int i;
    int actor;

    if (targetTeam == 0x3f3f3f3f) {
        off = 0;
        for (i = 0; i < DAT_80612914; i = i + 1) {
            cond = *(int **)(DAT_80612910 + off);
            if ((*(int *)(*cond + 8) == 1) && (-1 < cond[8])) {
                sound_stop_handle();
                cond[8] = -1;
            }
            off = off + 4;
        }
    } else {
        actor = aiactor_find_player_by_team(targetTeam);
        if (actor != 0) {
            aiactor_clear_bark_state(actor);
        }
    }
}

// 0x8022fe70 - Event-EXECUTE dispatcher. Given an armed event record, run the
// side effect selected by the descriptor's type word (*rec[0] + 8): ~90 cases
// covering sounds (looping/3d/stereo/persistent), HUD banners/hints, anim/material
// changes, trigger open/close/toggle, cutscenes, movers, objective-class state
// ops (complete/arm/disarm/advance), spawn/destroy entities & decals, grab
// attach/release, camera/menu/spectator, mute/volume, and match end. Marks the
// record done (rec[0xc]=rec[0xd]=1) and calls the optional per-event callback
// (+0x38 / +0xe) where the case defines one.
void worm_event_execute(int *ctx)
{
    float f1, f2, f3;
    short sv;
    int *rec;
    int r;
    unsigned int u;
    unsigned int *slot;
    int desc;
    int i, off;
    int local_d8[7];
    int local_bc, local_b8, local_b4; unsigned char local_b0;
    int local_a8[4]; unsigned int local_98; float local_90;
    int local_8c, local_88, local_84; unsigned char local_80;
    int local_78[20];
    unsigned int uStack_24;

    rec = ctx;
    desc = *rec;
    rec[0xc] = 1;
    rec[0xd] = 1;
    switch (*(int *)(desc + 8)) {
    case 0: case 0x1d: case 0x1e:
        if (*(void (**)(int *))(desc + 0x38) != (void (*)(int *))0) {
            (*(void (**)(int *))(desc + 0x38))(rec);
        }
        break;
    case 1: {
        off = 0;
        for (i = DAT_806128f8; i > 0; i = i - 1) {
            int *other = (int *)(DAT_80612924 + off);
            if ((other != rec) && (*(int *)(*other + 8) == 1) && (other[0xb] != 0) &&
                (other[0xc] != 0)) {
                if ((*(unsigned int *)(*other + 0xb0) & 0x40) != 0) {
                    if ((*(unsigned int *)(desc + 0xb0) & 0x40) != 0) {
                        FUN_80234470((int)rec);
                    }
                    goto done;
                }
                if ((*(unsigned int *)(desc + 0xb0) & 0x40) != 0) {
                    FUN_80234470((int)rec);
                    goto done;
                }
            }
            off = off + 0x48;
        }
        worm_event_play_bark(rec);
        break;
    }
    case 2: {
        int strId = *(int *)(*rec + 0x10);
        unsigned int *str;
        if ((*(unsigned int *)(desc + 0xb0) & 0x80) == 0) {
            hud_banner_show_persistent(300, 0);
        } else {
            hud_banner_show(300, 0);
        }
        str = (unsigned int *)get_string_by_id(strId);
        hud_text_submit(0, 8, str);
        if (*(void (**)(int *))(desc + 0x38) != (void (*)(int *))0) {
            (*(void (**)(int *))(desc + 0x38))(rec);
        }
        break;
    }
    case 3:
        FUN_80231220(0, 0, 0, 0, 0, 0, 0, 0);
        break;
    case 4: case 5: {
        int inst = FUN_8023c32c(*(unsigned int *)(*rec + 0x14));
        if (inst != 0) {
            if (*(int *)(*(int *)(inst + 4) + 0xcc) != 0) {
                float w = FLOAT_8061874c;
                if (*(int *)(desc + 8) != 4) {
                    w = *(float *)(*rec + 0x1c);
                }
                anim_set_blend_weight((double)w, *(int *)(inst + 4));
                rec[9] = *(int *)(inst + 0x2c);
                *(short *)(desc + 0x18) = (short)*(int *)(*(int *)(inst + 4) + 0xcc);
            }
            if (*(void (**)(int))(desc + 0x38) != (void (*)(int))0) {
                (*(void (**)(int))(desc + 0x38))(inst);
            }
        }
        break;
    }
    case 6: {
        int inst = FUN_8023c32c(*(unsigned int *)(*rec + 0x14));
        if ((inst != 0) && (*(int *)(*(int *)(inst + 4) + 0xcc) != 0)) {
            anim_active_slot_set_speed((double)FLOAT_8061876c, inst);
        }
        break;
    }
    case 7: {
        int inst = FUN_8023c32c(*(unsigned int *)(*rec + 0x14));
        if (inst != 0) {
            if ((*(unsigned int *)(desc + 0xb0) & 0x80000) == 0) {
                render_instance_set_material((double)*(float *)(*rec + 0x1c),
                                             *(int **)(inst + 4), (int)*(short *)(*rec + 0x18));
            } else {
                if (*(int *)(inst + 0x20) == 0x40000000) {
                    entity_register_for_update(inst, 2, (int)0x80202998);
                }
                anim_play_scaled((double)FLOAT_80618744, (double)*(float *)(*rec + 0x1c));
            }
            *(int *)(*(int *)(inst + 4) + 0x11c) = 0;
            rec[9] = *(int *)(inst + 0x2c);
        }
        break;
    }
    case 8:
        FUN_80357bf0();
        break;
    case 9: {
        int actor = aiactor_find_player_by_team(*(int *)(desc + 0x14));
        if (actor != 0) {
            actor = *(int *)(actor + 0x7c);
            *(int *)(actor + 0xa0c) = *(int *)(desc + 4);
            *(int *)(actor + 0xa08) = *(int *)desc;
            *(int *)(actor + 0xc64) = (int)*(short *)(*rec + 0x18);
        }
        break;
    }
    case 10: {
        int e = *rec;
        if (*(int *)(e + 0xc) == -1) {
            worm_event_explosion_spawn(rec);
        } else {
            int *bone;
            if ((int)*(unsigned int *)(e + 0x18) < 0) {
                bone = (int *)(desc + 0x20);
            } else {
                bone = (int *)scene_node_get_submesh(*(int *)(e + 0x14), *(unsigned int *)(e + 0x18));
            }
            if (bone != (int *)0) {
                int snd = *(int *)(*rec + 0xc);
                sound_init_play_params((int)local_d8);
                local_b0 = 1;
                local_bc = bone[0];
                local_b8 = bone[1];
                local_b4 = bone[2];
                local_d8[0] = snd;
                rec[8] = sound_play_request(local_d8);
            }
        }
        break;
    }
    case 0xb: {
        u = *(unsigned int *)(desc + 0xb0);
        if ((u & 0x2000000) == 0) {
            if ((u & 0x8000000) == 0) {
                float v = FLOAT_8061874c;
                if ((u & 0x800) != 0) v = FLOAT_8061877c;
                sound_init_play_params((int)local_a8);
                local_a8[0] = *(int *)(*rec + 0xc);
                if ((*(unsigned int *)(desc + 0xb0) & 0x4000) != 0) {
                    local_80 = 1;
                    local_8c = *(int *)(desc + 0x20);
                    local_88 = *(int *)(desc + 0x24);
                    local_84 = *(int *)(desc + 0x28);
                }
                if ((*(unsigned int *)(desc + 0xb0) & 0x20000) != 0) {
                    local_98 = local_98 | 0x100;
                }
                local_90 = v;
                rec[8] = sound_play_request(local_a8);
                if ((*(unsigned int *)(desc + 0xb0) & 0x10000) != 0) {
                    sound_set_pitch((double)FLOAT_80618744, rec[8]);
                }
            } else {
                float v = FLOAT_8061874c;
                if ((u & 0x800) != 0) v = FLOAT_8061877c;
                if ((u & 0x4000) == 0) {
                    sound_play_or_resume_3d((double)FLOAT_80618744, (double)FLOAT_8061874c,
                                            (double)FLOAT_8061874c, *(int *)(*rec + 0xc), 0, 0);
                } else {
                    sound_play_or_resume_3d((double)FLOAT_80618780, (double)FLOAT_8061874c,
                                            (double)v, *(int *)(*rec + 0xc),
                                            (void *)(desc + 0x20), 0);
                }
            }
        } else {
            sound_play_stereo_pair((double)FLOAT_80618770, (double)FLOAT_80618774,
                                   (double)FLOAT_80618778);
        }
        if (*(void (**)(int *))(desc + 0x38) != (void (*)(int *))0) {
            (*(void (**)(int *))(desc + 0x38))(rec);
        }
        break;
    }
    case 0xc: {
        int e = *rec;
        unsigned int newBits = *(unsigned int *)(e + 0xc);
        unsigned int mask = *(unsigned int *)(e + 0x18);
        int inst = FUN_8023c32c(*(unsigned int *)(e + 0x14));
        if (inst != 0) {
            *(unsigned int *)(*(int *)(inst + 4) + 0x30) =
                *(unsigned int *)(*(int *)(inst + 4) + 0x30) & ~mask;
            *(unsigned int *)(*(int *)(inst + 4) + 0x30) =
                *(unsigned int *)(*(int *)(inst + 4) + 0x30) | newBits;
            *(unsigned int *)(inst + 0x18) = *(unsigned int *)(inst + 0x18) | 2;
        }
        break;
    }
    case 0xd: {
        u = *(unsigned int *)(*rec + 0xc);
        int ts = (int)trigger_slot_find_by_entity_id(*(int *)(*rec + 0x14));
        if (ts != 0) *(unsigned int *)(ts + 0xc) = *(unsigned int *)(ts + 0xc) | u;
        break;
    }
    case 0xe: {
        u = *(unsigned int *)(*rec + 0xc);
        int ts = (int)trigger_slot_find_by_entity_id(*(int *)(*rec + 0x14));
        if (ts != 0) *(unsigned int *)(ts + 0xc) = *(unsigned int *)(ts + 0xc) & ~u;
        break;
    }
    case 0xf: {
        u = *(unsigned int *)(*rec + 0xc);
        int ts = (int)trigger_slot_find_by_entity_id(*(int *)(*rec + 0x14));
        if (ts != 0) *(unsigned int *)(ts + 0xc) = *(unsigned int *)(ts + 0xc) ^ u;
        break;
    }
    case 0x11: {
        int stateArg = *(int *)(*rec + 0x10);
        slot = (unsigned int *)trigger_slot_find_by_entity_id(*(int *)(*rec + 0x14));
        if (slot != (unsigned int *)0) {
            u = *slot;
            int inst;
            if (u == 0) inst = 0;
            else {
                inst = DAT_80611c24 + (u >> 0x16) * 0x2e8;
                if (*(unsigned int *)(inst + 0x2c) != u) { inst = 0; *slot = 0; }
            }
            if (inst != 0) {
                unsigned int st = slot[4];
                if (st == 3) {
                    trigger_open_full(inst, 1);
                } else if (((int)st < 3) && (st == 0)) {
                    u = slot[3];
                    if ((u & 0x20000) != 0) {
                        if (stateArg == 0) slot[3] = u & 0xff7fffff;
                        else slot[3] = u | 0x800000;
                    }
                    trigger_open_full(inst, 1);
                } else {
                    trigger_close_if_idle(inst);
                }
            }
        }
        break;
    }
    case 0x12: {
        int ts = (int)trigger_slot_find_by_entity_id(*(int *)(*rec + 0x14));
        *(unsigned int *)(ts + 0xc) = *(unsigned int *)(ts + 0xc) | 0x4000000;
    }
    /* fallthrough */
    case 0x10: {
        int e = *rec;
        int wantOpen = *(int *)(e + 0xc);
        int keepArg = *(int *)(e + 0x10);
        slot = (unsigned int *)trigger_slot_find_by_entity_id(*(int *)(e + 0x14));
        if (slot != (unsigned int *)0) {
            u = *slot;
            int inst;
            if (u == 0) inst = 0;
            else {
                inst = DAT_80611c24 + (u >> 0x16) * 0x2e8;
                if (*(unsigned int *)(inst + 0x2c) != u) { inst = 0; *slot = 0; }
            }
            if (inst != 0) {
                if (wantOpen == 0) {
                    trigger_close_if_idle(inst);
                } else {
                    u = slot[3];
                    if ((u & 0x20000) != 0) {
                        if (keepArg == 0) slot[3] = u & 0xff7fffff;
                        else slot[3] = u | 0x800000;
                    }
                    trigger_open_full(inst, 1);
                }
            }
        }
        break;
    }
    case 0x13: {
        int e = *rec;
        int wantClose = *(int *)(e + 0xc);
        int keepArg = *(int *)(e + 0x10);
        slot = (unsigned int *)trigger_slot_find_by_entity_id(*(int *)(e + 0x14));
        if (slot != (unsigned int *)0) {
            u = *slot;
            int inst;
            if (u == 0) inst = 0;
            else {
                inst = DAT_80611c24 + (u >> 0x16) * 0x2e8;
                if (*(unsigned int *)(inst + 0x2c) != u) { inst = 0; *slot = 0; }
            }
            if (inst != 0) {
                if (wantClose == 0) {
                    trigger_open_immediate(inst);
                } else {
                    u = slot[3];
                    if ((u & 0x20000) != 0) {
                        if (keepArg == 0) slot[3] = u & 0xff7fffff;
                        else slot[3] = u | 0x800000;
                    }
                    trigger_close_immediate(inst);
                }
            }
        }
        break;
    }
    case 0x14: {
        int ts = (int)trigger_slot_find_by_entity_id(*(int *)(*rec + 0x14));
        f1 = *(float *)(desc + 0x10);
        f2 = *(float *)(desc + 0x18);
        f3 = *(float *)(desc + 0x1c);
        if (FLOAT_80618744 < *(float *)(desc + 0xc)) {
            *(float *)(ts + 0x30) = *(float *)(desc + 0xc) *
                (float)((double)(long long)(unsigned int)(DAT_806109c0) - DOUBLE_80618758);
        }
        if (FLOAT_80618744 <= f3) {
            *(float *)(ts + 0x34) = f3 *
                (float)((double)(long long)(unsigned int)(DAT_806109c0) - DOUBLE_80618758);
        }
        if (FLOAT_80618744 <= f1) *(float *)(ts + 0x3c) = f1;
        if (FLOAT_80618744 <= f2) *(float *)(ts + 0x40) = f2;
        break;
    }
    case 0x15: { int p = soundpad_find_pad_by_extref(*(int *)(*rec + 0xc));
        if (p != 0) *(unsigned int *)(p + 0x60) = *(unsigned int *)(p + 0x60) | 0x20; break; }
    case 0x16: { int p = soundpad_find_pad_by_extref(*(int *)(*rec + 0xc));
        if (p != 0) *(unsigned int *)(p + 0x60) = *(unsigned int *)(p + 0x60) & 0xffffffdf; break; }
    case 0x17: { int p = soundpad_find_pad_by_extref(*(int *)(*rec + 0xc));
        if (p != 0) *(unsigned int *)(p + 0x60) = *(unsigned int *)(p + 0x60) ^ 0x20; break; }
    case 0x18: {
        FUN_80238f9c(*(int *)(desc + 0x18));
        int ok = cutscene_load_and_start(0, 0, 0, 0, 0, 0, 0, 0,
                                         *(int *)(desc + 0x18) * 0x5c + (int)0x80465aa0,
                                         *(int *)(desc + 0x18) * 0x5c + (int)0x80465a50);
        if (ok == 0) {
            rec[0xb] = 0; rec[0xc] = 0; rec[0xd] = 0;
        } else {
            cutscene_begin_playback();
            player_slot_assign_pause_camera(DAT_80611d54, 5, 0);
            FUN_802387e4(*(int *)(desc + 0x18), *(int *)(desc + 0x1c));
            if ((*(unsigned int *)(desc + 0xb0) & 0x4000000) == 0) {
                *(unsigned int *)(DAT_80611d54 + 0x21c) =
                    *(unsigned int *)(DAT_80611d54 + 0x21c) & 0xfdffffff;
            } else {
                *(unsigned int *)(DAT_80611d54 + 0x21c) =
                    *(unsigned int *)(DAT_80611d54 + 0x21c) | 0x2000000;
            }
        }
        break;
    }
    case 0x19: {
        u = *(unsigned int *)(desc + 0xb0);
        unsigned int flags = 0;
        if ((u & 0x100000) != 0) flags = 0x100000;
        if ((u & 0x200000) != 0) flags |= 0x800000;
        if ((u & 0x1000000) != 0) flags |= 0x1000000;
        player_slot_assign_pause_camera(DAT_80611d54, (int)*(short *)(*rec + 0x18), flags);
        sv = *(short *)(*rec + 0x18);
        if ((sv == 0x9b5) || (sv == 0x7c6) || (sv == 0x701) || (sv == 0x71c) || (sv == 0x775)) {
            object_fade_begin(1);
        }
        break;
    }
    case 0x1c:
        FUN_80233d50(0, 0, 0, 0, 0, 0, 0, 0, (int)(rec + 7), desc, 0, 0, 0, 0, 0, 0);
        break;
    case 0x1f:
        statlvl_class_complete((double)FLOAT_80618744, 0, 0, 0, 0, 0, 0, 0);
        break;
    case 0x20: statlvl_class_force_complete(*(int *)(desc + 0xc)); break;
    case 0x21: statlvl_class_set_state2(*(int *)(desc + 0xc)); break;
    case 0x22: statlvl_class_arm(*(int *)(desc + 0xc)); break;
    case 0x23: statlvl_class_disarm(*(int *)(desc + 0xc)); break;
    case 0x24: statlvl_class_advance(*(int *)(desc + 0xc)); break;
    case 0x25: statlvl_class_set_state4(*(int *)(desc + 0xc)); break;
    case 0x26: sound_set_mute_flag(1); break;
    case 0x27: sound_set_mute_flag(0); break;
    case 0x28:
        sound_set_voice_base_volume((double)((float)((double)(long long)(unsigned int)(*(int *)(desc + 0xc))
                                                      - DOUBLE_80618758) / FLOAT_80618768));
        break;
    case 0x29: sound_set_voice_base_volume((double)FLOAT_8061874c); break;
    case 0x2a: sound_set_voice_volume_scale(*(int *)(desc + 0xc)); break;
    case 0x2b: {
        unsigned int eid = *(unsigned int *)(*rec + 0x14);
        int inst = FUN_8023c32c(eid);
        if (inst == 0) {
            int needResp = 0, idx = 0, o = 0;
            int cnt = *(int *)(DAT_8061208c + 0x10);
            if (0 < cnt) {
                do {
                    if (*(unsigned int *)(*(int *)(DAT_8061208c + 0xc) + o) == eid) {
                        needResp = entity_needs_respawn(idx);
                        break;
                    }
                    idx = idx + 1; o = o + 0x60; cnt = cnt - 1;
                } while (cnt != 0);
            }
            if (needResp == 0) {
                int sp = level_spawn_entity_by_id(0, 0, 0, 0, 0, 0, 0, 0, eid);
                if (sp == 0) { rec[0xc] = 0; rec[0xd] = 0; }
                else { entity_restore_by_spawn_ptr(sp); rec[9] = *(int *)(sp + 0x2c); }
            }
        }
        break;
    }
    case 0x2c: {
        int inst = FUN_8023c32c(*(unsigned int *)(*rec + 0x14));
        if (inst != 0) entity_destroy_with_event((void *)inst);
        break;
    }
    case 0x2d: {
        int n = FUN_8023c59c(*(int *)(*rec + 0xc), (int)local_78, 0x14);
        off = 0;
        for (i = 0; i < n; i = i + 1) {
            entity_destroy_with_event(*(void **)((int)local_78 + off));
            off = off + 4;
        }
        break;
    }
    case 0x2e: {
        unsigned int eid = *(unsigned int *)(*rec + 0x14);
        if (FUN_8023c32c(eid) == 0) {
            level_spawn_decal_by_id(0, 0, 0, 0, 0, 0, 0, 0, eid);
        }
        break;
    }
    case 0x2f: {
        int inst = FUN_8023c32c(*(unsigned int *)(*rec + 0x14));
        if (inst != 0) subobj_carry_channel_set_param((double)*(float *)(*rec + 0x1c), inst);
        break;
    }
    case 0x30: {
        int inst = FUN_8023c32c(*(unsigned int *)(*rec + 0x14));
        if (inst != 0) {
            float *origin = (float *)subobj_get_contact_origin(*(unsigned int *)(inst + 0x80));
            int block = subobj_carry_nav_block_ptr(inst);
            int nearest = soundpad_find_nearest_entity((double)FLOAT_80618744, origin);
            int a = nearest;
            if (-1 < *(int *)(desc + 0xc)) a = *(int *)(desc + 0xc);
            int b = *(int *)(desc + 0x10);
            if ((b < 0) && (b = nearest, -1 < *(int *)(block + 0x14))) b = *(int *)(block + 0x14);
            subobj_carry_nav_init(inst, nearest, a, b);
        }
        break;
    }
    case 0x33: {
        int inst = FUN_8023c32c(*(unsigned int *)(*rec + 0x14));
        void *xf = (void *)0; float *pos = (float *)0;
        if (inst != 0) {
            if ((*(unsigned int *)(desc + 0xb0) & 0x4000) != 0) xf = (void *)(desc + 0x20);
            if ((*(unsigned int *)(desc + 0xb0) & 0x400000) != 0) pos = (float *)(desc + 0x2c);
            subobj_apply_transform_snapshot(inst, xf, pos);
        }
        break;
    }
    case 0x35:
        worm_event_apply_impulse(rec);
        break;
    case 0x36:
        if ('\0' < (char)DAT_80501572) FUN_80273284();
        break;
    case 0x37:
        if ('\0' < (char)DAT_80501572)
            FUN_8027317c(*(int *)(desc + 0xc), *(int *)(desc + 0x10), *(int *)(desc + 0x14));
        break;
    case 0x38:
        for (i = 0; i < 5; i = i + 1) {
            int id = *(int *)(desc + 0xc + i * 4);
            if (id != 0) FUN_8023265c(FUN_802325b8(id));
        }
        break;
    case 0x39: {
        int *sw = switch_find_slot_by_entity(*(int *)(desc + 0xc));
        FUN_802332d0(sw, 0, 1);
        break;
    }
    case 0x3a:
        for (i = 0; i < 5; i = i + 1) {
            int id = *(int *)(desc + 0xc + i * 4);
            if (id != 0) FUN_802325fc(id);
        }
        break;
    case 0x3b:
        if ((*(int *)(*rec + 0x14) != 0) &&
            ((r = aiactor_find_player_by_team(*(int *)(*rec + 0x14))) != 0)) {
            aiactor_clear_bark_state(r);
        }
        break;
    case 0x3c: {
        u = 0;
        int leader = *(int *)(DAT_80611d78 + 0x14);
        if ((*(unsigned int *)(desc + 0xb0) & 0x100000) != 0) u = 4;
        if ((*(unsigned int *)(desc + 0xb0) & 0x20000000) != 0) u = u | 8;
        int sp = spawn_point_find_by_entity(*(int *)(desc + 0xc));
        player_enter_menu_state(*(int *)(leader + 0x150), sp, 0, u, *(int *)(desc + 0x10), 0);
        break;
    }
    case 0x3d: {
        int sp = spawn_point_find_by_entity(*(int *)(desc + 0xc));
        spectator_set_mode_if_ready((double)(float)*(int *)(desc + 0x10), sp);
        break;
    }
    case 0x3e: {
        int m = mover_find_by_entity_id(*(int *)(*rec + 0x14));
        mover_activate(m);
        break;
    }
    case 0x3f: {
        int m = mover_find_by_entity_id(*(int *)(*rec + 0x14));
        mover_set_trigger_delay(m);
        break;
    }
    case 0x40: case 0x41: {
        int m = mover_find_by_entity_id(*(int *)(*rec + 0x14));
        int src = *(int *)(m + 0x7c);
        *(int *)(m + 100) = *(int *)(src + 0x30);
        *(int *)(m + 0x68) = *(int *)(src + 0x34);
        *(int *)(m + 0x6c) = *(int *)(src + 0x38);
        *(int *)(*(int *)(m + 4) + 0x30) = *(int *)(src + 0x50);
        mover_activate_linked_entities();
        break;
    }
    case 0x42:
        FUN_8025ff4c(2, (int *)desc, 0);
        break;
    case 0x43: {
        int e = *(int *)(desc + 0xc);
        if (e < 0) e = *(int *)(DAT_80611d78 + (-1 - e) * 0x141c + 0x14);
        else e = aiactor_find_entity_by_team(e, 0);
        if (e != 0) {
            u = *(unsigned int *)(e + 0x28);
            if ((u & 0x1000000) == 0) *(unsigned int *)(e + 0x28) = u | 0x1000000;
            else *(unsigned int *)(e + 0x28) = u & 0xfeffffff;
        }
        break;
    }
    case 0x44: hud_banner_show_persistent(0xffffffff, 0); break;
    case 0x45: hud_banner_hide(); break;
    case 0x46:
        off = 0;
        for (i = 0; i < DAT_80612610; i = i + 1) {
            *(unsigned int *)(DAT_80611d78 + off + 0xc) =
                *(unsigned int *)(DAT_80611d78 + off + 0xc) | 0x8000;
            off = off + 0x141c;
        }
        break;
    case 0x47:
        off = 0;
        for (i = 0; i < DAT_80612610; i = i + 1) {
            *(unsigned int *)(DAT_80611d78 + off + 0xc) =
                *(unsigned int *)(DAT_80611d78 + off + 0xc) & 0xffff7fff;
            off = off + 0x141c;
        }
        break;
    case 0x48: {
        int actor = aiactor_find_player_by_team(*(int *)(desc + 0x14));
        if (actor != 0) FUN_80262c34(actor, *(int *)(desc + 0x10));
        break;
    }
    case 0x49: mths_soundzone_set_by_id(*(int *)(desc + 0xc)); break;
    case 0x4a: {
        int actor = aiactor_find_entity_by_team(*(int *)(desc + 0xc), 0);
        int obj = FUN_8023c32c(*(int *)(desc + 0x10));
        if ((actor != 0) && (obj != 0)) {
            int am = *(int *)(actor + 0x7c);
            int slotKind = *(int *)(desc + 0x14);
            if (((*(unsigned int *)(am + 0x24) & 0x100) == 0) &&
                (subobj_slot_occupied(obj, slotKind) != 0)) {
                subobj_grab_commit_attach(0, 0, obj, actor, slotKind);
                if (*(int *)(am + 0xaac) == 1 && *(int *)(am + 0xaa8) == 0) {
                    if (DAT_805015bd == '\n') u = *(unsigned int *)(am + 0x14);
                    else u = *(unsigned int *)(am + 0x1c);
                    if ((u & 1) == 0) break;
                }
                *(int *)(am + 0xaac) = 0x8000;
                *(int *)(am + 0xaa8) = 0;
            }
        }
        break;
    }
    case 0x4b: {
        int actor = aiactor_find_entity_by_team(*(int *)(desc + 0xc), 0);
        int obj = FUN_8023c32c(*(int *)(desc + 0x10));
        if ((actor != 0) && (obj != 0) &&
            ((*(unsigned int *)(*(int *)(actor + 0x7c) + 0x24) & 0x100) != 0) &&
            (*(int *)(*(int *)(actor + 0x7c) + 0xc08) == *(int *)(obj + 0x2c))) {
            subobj_grab_attach_release(obj, actor, 0, 0, 1);
        }
        break;
    }
    case 0x4c: hud_hint_show_reload(); break;
    case 0x4d: hud_hint_show_swap_controls(); break;
    case 0x4e: hud_hint_show_empty(); break;
    case 0x4f: hud_hint_show_generic(); break;
    case 0x50: hud_hint_show_pickup_controls(); break;
    case 0x51: hud_hint_show_grab_controls(); break;
    case 0x52: hud_hint_show_mode4_controls(); break;
    case 0x53:
        for (i = 0; i < 4; i = i + 1) {
            if (*(int *)(desc + 0xc + i * 4) != 0)
                aiactor_alert_zone_broadcast_b(0, 0, 0, 0, 0, 0, 0, 0);
        }
        break;
    case 0x54:
        for (i = 0; i < 4; i = i + 1) {
            if (*(int *)(desc + 0xc + i * 4) != 0) worm_event_stop_bark(0x3f3f3f3f);
        }
        break;
    case 0x55: FUN_8025c9d0(*(int *)(desc + 0xc)); break;
    case 0x56: {
        int sample = sound_resolve_sample_id(*(int *)(*rec + 0x10));
        int is3d = 0;
        if ((*(int *)(*rec + 0x14) != 0) && (*(int *)(*rec + 0x14) != 0x3f3f3f3f)) is3d = 1;
        sound_play_persistent(sample, is3d, 1);
        break;
    }
    case 0x57:
        sound_play_persistent(*(int *)(*rec + 0xc), *(int *)(desc + 0x14), 0);
        break;
    case 0x58:
        DAT_805015c4 = DAT_805015c4 | 0x8000;
        match_process_end(0, 0, 0, 0, 0, 0, 0, 0, 0);
        break;
    case 0x59:
        for (i = DAT_80612610; i < DAT_80612098; i = i + 1) {
            int e = ((int *)(&DAT_80588610)[i])[0x2fc];
            if (((e == 0) || ((*(unsigned int *)(e + 0x58) & 0x10000000) == 0)) &&
                (aiactor_deactivate(*(int *)(&DAT_80588610)[i]) != 0)) {
                i = i - 1;
            }
        }
        break;
    case 0x5a:
        off = DAT_80612610 * 0xea0;
        for (i = DAT_80612610; i < DAT_80612610 + DAT_806120fc; i = i + 1) {
            int p = *(int *)(DAT_80611d90 + off);
            if ((p != 0) && ((*(int *)(DAT_80611d90 + off + 0x2c) & 4) != 0)) {
                aiactor_activate(p);
            }
            off = off + 0xea0;
        }
        break;
    case 0x5c:
        hud_banner_show(0xffffffff, 1);
        break;
    default:
        break;
    }
done:
    return;
    (void)uStack_24;
}

// ===========================================================================
// Batch: batch_696
// Source: TimeSplitters: Future Perfect (GameCube, main.dol)
// The event-execution engine: the master per-frame event tick driver, the
// entity-spawn event handler, the event "is satisfied" state query, the
// arm/advance chain-link machinery, and the objective chain-link condition
// evaluator. This is the heart that drives the condition/execute dispatchers
// authored in batch_695 - worm_event_tick_driver walks the condition list each
// frame, counts down armed timers, and fires worm_event_execute.
// ===========================================================================

// --- batch_696 file-local externs (raw symbols not declared above) ---------
extern int   DAT_80611824;    // anim descriptor pointer table base (indexed by slot)
extern short DAT_804d1cd0;    // event anim-slot -> descriptor index LUT (short[])
extern int   DAT_806125e8;    // per-actor def table base (0x30 stride)
extern short DAT_805c9858;    // level-actor id remap table (short[])
extern int   DAT_80587db4;    // pickup/switch state table base (0x60 stride)
extern int   DAT_805cb94c;    // switch-state table base (8 stride, +0 = state)

// Named helpers this batch introduces / references
extern int  entity_spawn_with_activation(double a1, double a2, double a3, double a4, double a5,
                                         double a6, int a7, int a8, int flags, int typeId,
                                         int posSrc, float *pos, int e, int ctx);
extern int  event_has_handler(int entityType, int kind);
extern void anim_blend_eval_direct(void);
extern int  scene_query_nodes_in_frustum(int a, int box, int out, int max);
extern int  floor_height_from_position(double a, double b);
extern void anim_sample_keyframe(int desc, int a, int b, float *out);
extern void FUN_8023c8a0(int a, int spawned);   // link spawned entity to trigger
extern int  FUN_8023c990(void);                 // post-spawn notify
extern int  anim_get_descriptor(int animId);
extern int  FUN_80232740(int condRec);          // event prerequisite gate
extern int  FUN_8023351c(void);                 // global "events allowed" gate
extern int  level_get_current_id(void);
extern int  level_actor_check_active(void);
extern int  mover_get_active_state(int mover);
extern int  entity_is_actor_bound(int entityId);
extern int  level_actor_get_load_status(int levelId);
extern unsigned long long FUN_80238728(void);   // level-switch spill wrapper
extern void FUN_8025b9b4(double a1, double a2, double a3, double a4, double a5, double a6,
                         int a7, int a8, int *rec, int desc, int a11, int a12, int a13, int a14,
                         int a15, int a16);      // type-0x1b arm dispatch
extern int  soundpad_get_entity_id_from_zone(int zoneNode);
extern void FUN_8025bdb8(void);                 // link-cond type-0xb evaluator
extern int  subobj_carry_held_anim_ptr(int obj);
extern char *aiactor_relation_entry(int a, int b);
extern int  FUN_8025c8e8(int a);                // switch-slot index resolver
extern int  trigger_is_recently_activated(int slot);
extern int  level_actor_is_loaded(void);

// Ghidra spill save/restore intrinsic used only here
extern void FUN_803826f0(void);

// Forward declarations for within-batch helpers
int  worm_event_anim_descriptor_exists(int *condRec);
void worm_event_advance_chain(void);
void worm_event_arm_chain_link(int linkRec, int mode);
void worm_event_set_chain_armed(int linkRec);

// 0x80231220 - Entity-spawn event handler. Spawn an entity with the event's
// activation flags (normal / 0x80 pre-placed / 0x4000000 streamed by the +0xb0
// flag bits 8 and 0x400), then wire it up: register for update unless it has a
// spawn handler; apply the optional +0x3c update callback; set material or play
// the spawn anim; compute floor placement (frustum node query, else raw floor
// height); set render bit 2; fire the event's +0x38 callback and any linked
// trigger/notify. Record ptr arrives via the spill frame.
void worm_event_spawn_entity(double aF1, double a2, double a3, double a4, double a5, double a6,
                             int a7, int a8, int *rec)
{
    int desc;
    int posSrc;
    float *pos;
    int spawned;
    bool hasHandler;
    float box[7];
    float bmaxX, bmaxY, bmaxZ, bminX, bminY, bminZ;
    float cx, cy, cz;

    desc = *rec;
    posSrc = desc + 0x20;
    pos = (float *)(desc + 0x2c);
    if ((*(unsigned int *)(desc + 0xb0) & 8) == 0) {
        if ((*(unsigned int *)(desc + 0xb0) & 0x400) == 0) {
            spawned = entity_spawn_with_activation(aF1, a2, a3, a4, a5, a6, a7, a8, 1,
                                                   *(int *)(desc + 0x10), posSrc, pos, 0, (int)rec);
        } else {
            spawned = entity_spawn_with_activation(aF1, a2, a3, a4, a5, a6, a7, a8, 0x80,
                                                   *(int *)(desc + 0x10), posSrc, pos, 0, 0);
        }
        hasHandler = false;
        if ((*(int *)(spawned + 0x15c) != 0) ||
            (event_has_handler(*(int *)(spawned + 0x20), 2))) {
            hasHandler = true;
        }
        if (!hasHandler) {
            entity_register_for_update(spawned, 2, (int)0x80020034);
        }
    } else {
        spawned = entity_spawn_with_activation(aF1, a2, a3, a4, a5, a6, a7, a8, 0x4000000,
                                               *(int *)(desc + 0x10), posSrc, pos, 0, (int)rec);
        entity_register_for_update(spawned, 2, (int)0x8001d9ec);
    }
    if (spawned != 0) {
        if (*(int *)(desc + 0x3c) != 0) {
            entity_register_for_update(spawned, 3, *(int *)(desc + 0x3c));
        }
        if ((*(unsigned int *)(desc + 0xb0) & 0x80000) == 0) {
            render_instance_set_material((double)*(float *)(*rec + 0x1c),
                                         *(int **)(spawned + 4), (int)*(short *)(*rec + 0x18));
        } else {
            anim_play_scaled((double)FLOAT_80618744, (double)*(float *)(*rec + 0x1c));
        }
        if ((*(unsigned int *)(desc + 0xb0) & 8) == 0) {
            anim_blend_eval_direct();
            cx = cx + *(float *)(spawned + 0x1a8);
            cy = cy + *(float *)(spawned + 0x1ac);
            cz = cz + *(float *)(spawned + 0x1b0);
            bmaxX = cx + *(float *)(spawned + 0x1c0);
            bminX = cx - *(float *)(spawned + 0x1c0);
            bmaxY = cy + *(float *)(spawned + 0x1c4);
            bminY = cy - *(float *)(spawned + 0x1c4);
            bmaxZ = cz + *(float *)(spawned + 0x1c8);
            bminZ = cz - *(float *)(spawned + 0x1c8);
            box[0] = bminX; box[1] = bmaxX; box[2] = bminY;
            box[3] = bmaxY; box[4] = bminZ; box[5] = bmaxZ;
            *(char *)(spawned + 0x30) =
                (char)scene_query_nodes_in_frustum(0, (int)box, spawned + 0x38, 10);
            if (*(char *)(spawned + 0x30) == '\0') {
                *(int *)(spawned + 0x34) =
                    floor_height_from_position((double)FLOAT_80618770, (double)FLOAT_80618770);
                *(unsigned int *)(spawned + 0x28) = *(unsigned int *)(spawned + 0x28) | 0x40000000;
            } else {
                *(int *)(spawned + 0x34) = *(int *)(spawned + 0x38);
            }
        } else {
            anim_sample_keyframe(*(int *)(DAT_80611824 +
                                          (short)(&DAT_804d1cd0)[*(short *)(*rec + 0x18)] * 4),
                                 0, 0, box);
            *(int *)(spawned + 0x34) =
                floor_height_from_position((double)FLOAT_80618770, (double)FLOAT_80618770);
        }
        if ((*(unsigned int *)(desc + 0xb0) & 0x8000) != 0) {
            *(int *)(spawned + 0x34) = 0xd5;
            *(unsigned int *)(spawned + 0x28) = *(unsigned int *)(spawned + 0x28) | 0x40000000;
        }
        *(unsigned int *)(spawned + 0x18) = *(unsigned int *)(spawned + 0x18) | 2;
        if (*(void (**)(int))(desc + 0x38) != (void (*)(int))0) {
            (*(void (**)(int))(desc + 0x38))(spawned);
        }
        if (*(int *)(*rec + 0x14) != 0) {
            FUN_8023c8a0(*(int *)(*rec + 0x14), spawned);
        }
        if (*(int *)(*rec + 0xc) != 0) {
            FUN_8023c990();
        }
        rec[9] = *(int *)(spawned + 0x2c);
    }
}

// 0x8023152c - Predicate: does the event's anim descriptor (by anim id at
// *rec+0x18) exist?
int worm_event_anim_descriptor_exists(int *condRec)
{
    return anim_get_descriptor((int)*(short *)(*condRec + 0x18)) != 0;
}

// 0x8023155c - Event "is satisfied" state query. Gated by the prerequisite
// check FUN_80232740; when armed-state (rec[0xc]) is 2 it is not satisfied.
// Otherwise the per-type test (switch on *rec+8) reports completion: pointer/
// handle presence (via countLeadingZeros(x)>>5 == (x==0)), the cached satisfied
// flag rec[7], sound playing, scene-event ready, splitscreen flag, level-actor
// active, mover active.
unsigned int worm_event_is_satisfied(int *condRec)
{
    unsigned int result;
    unsigned int probe;
    int desc;

    desc = *condRec;
    if (!FUN_80232740((int)condRec)) {
        return 0;
    }
    if (condRec[0xc] == 2) {
        return 0;
    }
    switch (*(int *)(desc + 8)) {
    case 0: case 0xc: case 0x11: case 0x13: case 0x1d: case 0x1e:
    case 0x26: case 0x27: case 0x2a:
        result = countLeadingZeros(*(unsigned int *)(desc + 0x3c)) >> 5;
        break;
    case 1: case 3: case 4: case 5: case 6: case 7: case 0x10: case 0x12:
    case 0x1c: case 0x31: case 0x32: case 0x3c: case 0x3d:
        result = condRec[7];
        break;
    case 10:
        if (*(int *)(*condRec + 0xc) == -1) {
            result = 1;
        } else {
            probe = sound_is_playing(condRec[8]);
            result = countLeadingZeros(probe) >> 5;
        }
        break;
    case 0xb:
        probe = sound_is_playing(condRec[8]);
        result = countLeadingZeros(probe) >> 5;
        break;
    case 0x18:
        result = FUN_80238fb4(*(int *)(desc + 0x18));
        break;
    case 0x19:
        result = *(unsigned int *)(DAT_80611d54 + 0x21c) & 0x200000;
        break;
    case 0x1a:
        level_get_current_id();
        probe = level_actor_check_active();
        result = countLeadingZeros(probe) >> 5;
        break;
    case 0x3e: {
        int mover = mover_find_by_entity_id(*(int *)(*condRec + 0x14));
        probe = mover_get_active_state(mover);
        result = countLeadingZeros(probe) >> 5;
        break;
    }
    default:
        result = 1;
        break;
    }
    return result;
}

// 0x80231684 - Advance to the next chained event record. Resolve this record's
// index in the execute list (DAT_80612924, 0x48 stride), step to the next, and
// conditionally re-arm its two chain links (worm_event_arm_chain_link via
// worm_event_set_chain_armed) based on each link's mode (3 gated on the spill
// result, 2 unconditional).
void worm_event_advance_chain(void)
{
    int index;
    int *next;
    int desc;
    int gate;

    FUN_803826f0();
    /* the record ptr / spill gate arrive via the varargs frame; the index is
       recomputed from the record's list position. */
    gate = 0;
    index = 0;
    if (index < DAT_806128f8 + -1) {
        next = (int *)(DAT_80612924 + (index + 1) * 0x48);
    } else {
        next = (int *)0;
    }
    if (next != (int *)0) {
        desc = *next;
        if (((*(int *)(desc + 0x40) == 3) && (gate != 0)) || (*(int *)(desc + 0x40) == 2)) {
            worm_event_set_chain_armed((int)(next + 1));
        }
        if ((*(int *)(desc + 0x60) != 0) &&
            (((*(int *)(desc + 100) == 3) && (gate != 0)) || (*(int *)(desc + 100) == 2))) {
            worm_event_set_chain_armed((int)(next + 4));
        }
    }
    FUN_8038273c();
}

// 0x80231748 - Arm a chain-link record: clear its ready flag (+4=0), push the
// linked class to state2, then set the ready flag (+4=1) for the trigger modes
// that fire immediately (1, 6, 7, 0xf).
void worm_event_arm_chain_link(int linkRec, int mode)
{
    *(short *)(linkRec + 4) = 0;
    if (-1 < *(int *)(linkRec + 8)) {
        statlvl_class_set_state2(*(int *)(linkRec + 8));
    }
    if (mode < 8) {
        if (mode != 1) {
            if (mode < 1) return;
            if (mode < 6) return;
        }
    } else if (mode != 0xf) {
        return;
    }
    *(short *)(linkRec + 4) = 1;
}

// 0x802317cc - Master event-tick DRIVER. Walks the condition list
// (DAT_80612910, count DAT_80612914) every frame. For each condition it checks
// the prerequisite gate (FUN_80232740) and the chain-link arming conditions
// (arming a newly-eligible link via worm_event_arm). For armed conditions it
// runs the timed-countdown branch: decrement the +0xe timer by dt, gate the
// fire on level-actor-loaded state for the relevant event types, and when the
// timer expires fire worm_event_execute (respecting the +0x5c chain mode and the
// rec[10] link prerequisite). After firing it queries worm_event_is_satisfied,
// advances the chain (worm_event_advance_chain), re-arms looping events (flag
// 0x20), and re-triggers the detonate helper (worm_event_explosion_spawn) for
// type-10 events.
void worm_event_tick_driver(int a1, int a2, int p3, int a4, int a5, int a6, int a7, int a8)
{
    bool gate;
    int desc;
    int i;
    int off;
    int *condRec;
    int r4 = 0;
    double dF1 = 0, in_f2 = 0, in_f3 = 0, in_f4 = 0, in_f5 = 0, in_f6 = 0;
    int in_f7 = 0, in_f8 = 0;
    unsigned int sat;
    int linkMode;
    int fire;
    (void)a1; (void)a2; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;

    FUN_803826e4();
    i = 0;
    off = 0;
    while (true) {
        if (DAT_80612914 <= i) {
            FUN_80382730();
            return;
        }
        condRec = *(int **)(DAT_80612910 + off);
        desc = *condRec;
        if (condRec[0xd] == 2) condRec[0xd] = 0;
        if (condRec[0xc] != 2) {
            gate = FUN_80232740((int)condRec) != 0;
            if ((!gate) || (condRec[0xc] == 0)) {
                if ((condRec[0xb] == 0) &&
                    (condRec[7] == 0) && (*(int *)(desc + 0x5c) == 2) && (condRec[10] != 0) &&
                    (FUN_80232740(condRec[10]))) {
                    worm_event_arm(dF1, in_f2, in_f3, in_f4, in_f5, in_f6, in_f7, in_f8, condRec,
                                   r4, p3, a4, a5, a6, a7, a8);
                }
                if ((condRec[0xb] == 0) && (condRec[7] == 0)) {
                    int linkKind = *(int *)(desc + 0x60);
                    fire = 0;
                    if (linkKind == 2) {
                        if ((*(short *)(condRec + 2) != 0) || (*(int *)(*condRec + 0x40) == 0) ||
                            (*(short *)(condRec + 5) != 0) || (*(int *)(*condRec + 100) == 0)) {
                            fire = 1;
                        }
                    } else if (linkKind < 2) {
                        if (linkKind == 0) {
                            if ((*(short *)(condRec + 2) != 0) || (*(int *)(*condRec + 0x40) == 0)) {
                                fire = 1;
                            }
                        } else if (linkKind >= 0) {
                            if (((*(short *)(condRec + 2) != 0) || (*(int *)(*condRec + 0x40) == 0)) &&
                                ((*(short *)(condRec + 5) != 0) || (*(int *)(*condRec + 100) == 0))) {
                                fire = 1;
                            }
                        }
                    } else if (linkKind < 4) {
                        if (((*(short *)(condRec + 2) != 0) || (*(int *)(*condRec + 0x40) == 0)) &&
                            (*(short *)(condRec + 5) == 0) && (*(int *)(*condRec + 100) != 0)) {
                            fire = 1;
                        }
                    }
                    if (fire != 0) {
                        worm_event_arm(dF1, in_f2, in_f3, in_f4, in_f5, in_f6, in_f7, in_f8, condRec,
                                       r4, p3, a4, a5, a6, a7, a8);
                    }
                }
            }
            if ((condRec[0xb] != 0) &&
                ((FUN_80232740((int)condRec)) || (FUN_8023351c() != 0))) {
                double decr = (double)(float)((double)FLOAT_806108ec /
                                              (double)(float)((double)(long long)(unsigned int)(DAT_806109c0)
                                                              - DOUBLE_80618758));
                condRec[0xe] = (int)(float)((double)(float)condRec[0xe] - decr);
                int et = *(int *)(desc + 8);
                bool blocked = false;
                /* level-actor-loaded gating for streamed/actor-bound event types */
                if ((et == 0x2e) || (et == 0x2b) || (et == 0x33) ||
                    ((et > 0x2f) && (et < 0x32))) {
                    if (entity_is_actor_bound(*(int *)(*condRec + 0x14)) != 0) {
                        if (level_actor_get_load_status(level_get_current_id()) != 0) blocked = true;
                    }
                } else if (et == 0x18) {
                    if (FUN_80238fb4(*(int *)(desc + 0x18)) == 0) {
                        int want = FUN_80238f9c(*(int *)(desc + 0x18));
                        if (level_get_current_id() == want) {
                            unsigned long long r = level_actor_check_active();
                            if ((int)(r >> 0x20) != 0) blocked = true;
                        } else {
                            level_actor_switch(dF1, in_f2, in_f3, in_f4, in_f5, in_f6, in_f7, in_f8, want);
                            FUN_80238728();
                            blocked = true;
                        }
                    }
                } else if ((et == 7) || (et == 3)) {
                    level_get_current_id();
                    bool notExist = !worm_event_anim_descriptor_exists(condRec);
                    int idx2 = *(int *)(*condRec + 0x10) * 2;
                    unsigned int chk = 0;
                    if ((DAT_805015c0 & 0x40) == 0) {
                        if ((DAT_80612614 == 100) || (DAT_80612614 == 0x38)) {
                            chk = 0;
                        } else if ((DAT_805015bd == '\n') || (DAT_80612614 == 0x66)) {
                            chk = *(unsigned int *)(DAT_806125e8 +
                                                    (&DAT_805c9858)[idx2] * 0x30 + 0x28) & 0x2000000;
                        }
                    }
                    if (chk != 0) {
                        if (level_actor_get_load_status(level_get_current_id()) != 0) notExist = true;
                    }
                    if (notExist) blocked = true;
                } else if (et == 0x4a) {
                    if (entity_is_actor_bound(*(int *)(*condRec + 0x10)) != 0) {
                        if (level_actor_get_load_status(level_get_current_id()) != 0) blocked = true;
                    }
                }
                if (!blocked && ((double)(float)condRec[0xe] <= (double)FLOAT_80618744)) {
                    if (condRec[0xc] == 0) {
                        linkMode = *(int *)(desc + 0x5c);
                        bool doExec = false;
                        if (linkMode == 2) doExec = true;
                        else if (linkMode < 2) {
                            if (linkMode == 0) doExec = true;
                            else if ((linkMode >= 0) && (condRec[10] != 0)) {
                                if (FUN_80232740(condRec[10])) doExec = true;
                                else goto advance;
                            }
                        } else if ((linkMode < 4) && (condRec[10] != 0)) {
                            if (FUN_80232740(condRec[10])) goto advance;
                            doExec = true;
                        }
                        if (doExec) {
                            worm_event_execute(condRec);
                        }
                    }
                    sat = worm_event_is_satisfied(condRec);
                    worm_event_advance_chain();
                    if (sat != 0) {
                        condRec[0xb] = 0;
                        if ((*(unsigned int *)(*condRec + 0xb0) & 0x20) != 0) {
                            worm_event_arm_chain_link((int)(condRec + 1), *(int *)(desc + 0x40));
                            if (*(int *)(desc + 0x60) != 0) {
                                worm_event_arm_chain_link((int)(condRec + 4), *(int *)(desc + 100));
                            }
                            condRec[0xc] = 0;
                            condRec[0xe] = *(int *)(desc + 0xa4);
                            condRec[7] = 0;
                            condRec[0xd] = 2;
                        }
                        if ((*(int *)(desc + 8) == 10) && (*(int *)(*condRec + 0xc) != -1)) {
                            worm_event_explosion_spawn(condRec);
                        }
                    }
                }
            }
        }
    advance:
        i = i + 1;
        off = off + 4;
    }
}

// 0x80231d68 - Arm an event: set the countdown timer (+0xe) from the descriptor
// (+0xa4), mark it armed (+0xb=1), and for type 0x1b dispatch the special
// FUN_8025b9b4 arm handler.
void worm_event_arm(double a1, double a2, double a3, double a4, double a5, double a6,
                    int a7, int a8, int *rec, int b, int c, int d, int e, int f, int g, int h)
{
    int desc = *rec;
    rec[0xe] = *(int *)(desc + 0xa4);
    rec[0xb] = 1;
    if (*(int *)(desc + 8) == 0x1b) {
        FUN_8025b9b4(a1, a2, a3, a4, a5, a6, a7, a8, rec, desc, c, d, e, f, g, h);
    }
    (void)b;
}

// 0x80231da8 - Trivial setter: arm a chain-link record (+4 = 1).
void worm_event_set_chain_armed(int linkRec)
{
    *(short *)(linkRec + 4) = 1;
}

// 0x80231db4 - Objective chain-link CONDITION evaluator. Resolves the chain-link
// record for the current event (from the spill frame) and, based on the link's
// mode word: mode 4 arms the link (worm_event_set_chain_armed), mode 3 runs a
// big switch on the link condition type - players/actors in one of 4 zone ids,
// class-complete, switch state, held-object zone, actor relation, level-loaded,
// etc. - and on match fires statlvl_class_complete.
void worm_event_chain_link_evaluate(double a1, double a2, double a3, double a4, double a5,
                                    double a6, int a7, int a8)
{
    unsigned short *cur;
    int frameVal;
    int *baseRec;
    int *linkRec;
    int *modeWord;
    int i, off;
    int e, z;
    int held;
    char *rel;

    FUN_803826f0();
    /* cur (hi word) and frameVal (lo word) arrive via the spill frame */
    cur = (unsigned short *)0;
    frameVal = 0;
    baseRec = (int *)(DAT_80612924 + (unsigned int)(cur ? cur[0x15] : 0) * 0x48);
    if (baseRec[3] == (unsigned int)(cur ? *cur : 0)) {
        linkRec = baseRec + 1;
        modeWord = (int *)(*baseRec + 0x40);
    } else {
        linkRec = baseRec + 4;
        modeWord = (int *)(*baseRec + 100);
    }
    if (frameVal == 4) {
        if (*(short *)(linkRec + 1) == 0) {
            worm_event_set_chain_armed((int)linkRec);
        }
    } else if (frameVal == 3) {
        switch (*modeWord) {
        case 1: {
            double thresh = (double)FLOAT_80618744;
            off = 0;
            for (i = 0; i < DAT_80612610; i = i + 1) {
                e = *(int *)(DAT_80611d78 + off + 0x14);
                if ((thresh < (double)*(float *)(e + 0x1cc)) &&
                    ((z = soundpad_get_entity_id_from_zone(*(int *)(*(int *)(e + 0x7c) + 0x678)),
                      z == modeWord[2] || z == modeWord[3] || z == modeWord[4] || z == modeWord[5]))) {
                    statlvl_class_complete((double)FLOAT_80618744, a2, a3, a4, a5, a6, a7, a8);
                    break;
                }
                off = off + 0x141c;
            }
            break;
        }
        case 4:
            if (FUN_80232740(*linkRec)) {
                statlvl_class_complete((double)FLOAT_80618744, a2, a3, a4, a5, a6, a7, a8);
            }
            break;
        case 6: {
            int actor = aiactor_find_player_by_team(modeWord[1]);
            if ((actor != 0) &&
                ((z = soundpad_get_entity_id_from_zone(*(int *)(*(int *)(actor + 0x7c) + 0x678)),
                  z == modeWord[2] || z == modeWord[3] || z == modeWord[4] || z == modeWord[5]))) {
                statlvl_class_complete((double)FLOAT_80618744, a2, a3, a4, a5, a6, a7, a8);
            }
            break;
        }
        case 8:
            if (*(int *)((int)&DAT_80587db4 + modeWord[2] * 0x60) != 0) {
                statlvl_class_complete((double)FLOAT_80618744, a2, a3, a4, a5, a6, a7, a8);
            }
            break;
        case 9:
            if (statlvl_class_is_state5(modeWord[2]) != 0) {
                statlvl_class_complete((double)FLOAT_80618744, a2, a3, a4, a5, a6, a7, a8);
            }
            break;
        case 10: {
            int slot = (int)trigger_slot_find_by_entity_id(modeWord[1]);
            if ((slot != 0) && (trigger_is_recently_activated(slot) != 0)) {
                statlvl_class_complete((double)FLOAT_80618744, a2, a3, a4, a5, a6, a7, a8);
            }
            break;
        }
        case 0xb:
            FUN_8025bdb8();
            break;
        case 0xf: {
            int obj = FUN_8023c32c(modeWord[1]);
            if ((obj != 0) && (*(int *)(obj + 0x20) == 0x400000)) {
                int *heldPtr = (int *)subobj_carry_held_anim_ptr(obj);
                z = soundpad_get_entity_id_from_zone(*heldPtr);
                if ((z == modeWord[2]) || (z == modeWord[3]) || (z == modeWord[4]) ||
                    (z == modeWord[5])) {
                    statlvl_class_complete((double)FLOAT_80618744, a2, a3, a4, a5, a6, a7, a8);
                }
            }
            break;
        }
        case 0x10: {
            int actor = aiactor_find_player_by_team(modeWord[2]);
            if ((actor != 0) && (*(int *)(actor + 0x20) == 8)) {
                int am = *(int *)(actor + 0x7c);
                if ((int)modeWord[3] < 0) {
                    for (i = 0; i < DAT_80612610; i = i + 1) {
                        e = *(int *)(DAT_80611d78 + (modeWord[3] + 1) * -0x141c + 0x14);
                        if ((e != 0) && (*(int *)(e + 0x20) == 8) &&
                            (rel = aiactor_relation_entry(*(int *)(*(int *)(e + 0x7c) + 8),
                                                          *(int *)(am + 8)), *rel != '\0')) {
                            statlvl_class_complete((double)FLOAT_80618744, a2, a3, a4, a5, a6, a7, a8);
                            break;
                        }
                    }
                } else {
                    int other = aiactor_find_player_by_team(modeWord[3]);
                    if ((other != 0) && (*(int *)(other + 0x20) == 8) &&
                        (rel = aiactor_relation_entry(*(int *)(*(int *)(other + 0x7c) + 8),
                                                      *(int *)(am + 8)), *rel != '\0')) {
                        statlvl_class_complete((double)FLOAT_80618744, a2, a3, a4, a5, a6, a7, a8);
                    }
                }
            }
            break;
        }
        case 0x11:
            held = FUN_8025c8e8(modeWord[2]);
            if ((&DAT_805cb94c)[held * 8] == 1) {
                statlvl_class_complete((double)FLOAT_80618744, a2, a3, a4, a5, a6, a7, a8);
            }
            break;
        case 0x12:
            if (level_actor_is_loaded() != 0) {
                statlvl_class_complete((double)FLOAT_80618744, a2, a3, a4, a5, a6, a7, a8);
            }
            break;
        default:
            break;
        }
    }
    FUN_8038273c();
    (void)a1;
}

// ===========================================================================
// Batch: batch_697
// Source: TimeSplitters: Future Perfect (GameCube, main.dol)
// The event-subsystem lifecycle + looping-sound management layer: the per-frame
// event master tick entry (drives the tick-driver + condition-tick from prior
// batches), the level event-count computation and its accessors, the reset and
// allocation of the event condition/execute lists, and the helpers that scan
// active looping-sound events to query / set-pitch / fade them out.
// ===========================================================================

// --- batch_697 file-local externs (raw symbols not declared above) ---------
extern int   DAT_80611568;    // "event registration suppressed" flag
extern int   DAT_80612904;    // computed active-event count (conditions)
extern int   DAT_80612908;    // computed active-event count (with chain links)
extern int   DAT_8061291c;    // event scratch table base (0x10 stride, zeroed)
extern float FLOAT_80618784;  // looping-sound fade-out time

extern unsigned int spawn_check_flags_or_cutscene(unsigned int flags); // event-active predicate
extern int  sound_is_handle_valid(unsigned int handle);
extern void sound_register_fade_out(double vol, double time, int *handleSlot, unsigned int *extra);
extern void FUN_80232830(void);                 // populate the freshly-allocated event lists

// Ghidra spill save/restore intrinsics used only here
extern void FUN_803826e8(void);
extern void FUN_80382734(void);

// 0x802321d4 - Per-frame event master tick. Runs the event tick driver
// (worm_event_tick_driver: advance timers + fire executes) then the condition
// tick (worm_event_condition_tick: re-evaluate condition satisfaction). This is
// the top-level entry the level MASTER loop calls each frame.
void worm_event_master_tick(double a2, double a3, double a4, double a5, double a6,
                            int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16)
{
    worm_event_tick_driver(a9, a10, a11, a12, a13, a14, a15, a16);
    worm_event_condition_tick(0, a2, a3, a4, a5, a6, 0, 0);
}

// 0x802321f8 - Compute the level's active-event counts. When registration is not
// suppressed (DAT_80611568==0) and the level info exists, walk the level event
// table (info+0xcc, count info+0xd0, stride 200): count events whose flags pass
// spawn_check_flags_or_cutscene into DAT_80612904, and into DAT_80612908 (adding
// 2 instead of 1 when the event has a second chain link at +0x60).
void worm_event_compute_counts(void)
{
    int info;
    int table;
    int off;
    int i;
    int rec;
    unsigned int active;
    int count2;

    FUN_803826e8();
    info = level_get_info_cached(DAT_80612614);
    if ((DAT_80611568 == 0) && (info != 0)) {
        table = *(int *)(info + 0xcc);
        off = 0;
        DAT_80612904 = 0;
        DAT_80612908 = 0;
        for (i = 0; i < *(int *)(info + 0xd0); i = i + 1) {
            rec = table + off;
            active = spawn_check_flags_or_cutscene(*(unsigned int *)(rec + 0xb4));
            count2 = DAT_80612908;
            if (active != 0) {
                DAT_80612904 = DAT_80612904 + 1;
                count2 = DAT_80612908 + 1;
                if (*(int *)(rec + 0x60) != 0) {
                    count2 = DAT_80612908 + 2;
                }
            }
            DAT_80612908 = count2;
            off = off + 200;
        }
    }
    FUN_80382734();
}

// 0x802322b0 - Accessor for the active-event (condition) count. Returns
// DAT_80612904, or 0 for the special level id 0x65.
unsigned int worm_event_get_count(void)
{
    return DAT_80612904 & ~-(unsigned int)(DAT_80612614 == 0x65);
}

// 0x802322cc - Accessor for the active-event count including chain links.
// Returns DAT_80612908, or 0 for the special level id 0x65.
unsigned int worm_event_get_count_with_links(void)
{
    return DAT_80612908 & ~-(unsigned int)(DAT_80612614 == 0x65);
}

// 0x802322e8 - Reset the execute-list count (DAT_806128f8 = 0).
void worm_event_reset_list(void)
{
    DAT_806128f8 = 0;
}

// 0x802322f4 - Allocate the event condition/execute lists sized to the active
// event count: the execute list (0x48 stride), the condition-pointer array
// (4 stride), and a 0x10-stride scratch table (zeroed), then populate them
// (FUN_80232830).
void worm_event_alloc_lists(void)
{
    unsigned int n;

    n = worm_event_get_count();
    DAT_80612924 = mem_alloc(n * 0x48, 1);
    DAT_80612910 = mem_alloc(n << 2, 1);
    DAT_8061291c = mem_alloc(n << 4, 1);
    memset_wrapper(DAT_8061291c, 0, n << 4);
    FUN_80232830();
}

// 0x80232364 - Find whether an owner's looping event sound is still PLAYING.
// Scans the execute list (DAT_80612924, 0x48 stride) for a type-0xb event owned
// by the current record (+0xac) whose sound handle (+8) is playing. Owner ref
// arrives via the spill frame.
void worm_event_find_playing_sound(void)
{
    int owner;
    int off;
    int i;
    int rec;
    unsigned int handle;

    owner = (int)(long long)0; /* owner ref supplied through the spill frame */
    FUN_803826f0();
    off = 0;
    for (i = 0; i < DAT_806128f8; i = i + 1) {
        rec = *(int *)(DAT_80612924 + off);
        if ((*(int *)(rec + 8) == 0xb) && (*(int *)(rec + 0xac) == owner) &&
            (handle = ((int *)(DAT_80612924 + off))[8], -1 < (int)handle) &&
            (sound_is_playing(handle) != 0)) {
            break;
        }
        off = off + 0x48;
    }
    FUN_8038273c();
}

// 0x802323fc - Same scan as worm_event_find_playing_sound but tests handle
// VALIDITY (sound_is_handle_valid) rather than actively playing.
void worm_event_find_valid_sound(void)
{
    int owner;
    int off;
    int i;
    int rec;
    unsigned int handle;

    owner = (int)(long long)0; /* owner ref supplied through the spill frame */
    FUN_803826f0();
    off = 0;
    for (i = 0; i < DAT_806128f8; i = i + 1) {
        rec = *(int *)(DAT_80612924 + off);
        if ((*(int *)(rec + 8) == 0xb) && (*(int *)(rec + 0xac) == owner) &&
            (handle = ((int *)(DAT_80612924 + off))[8], -1 < (int)handle) &&
            (sound_is_handle_valid(handle) != 0)) {
            break;
        }
        off = off + 0x48;
    }
    FUN_8038273c();
}

// 0x80232494 - Reset the pitch (to FLOAT_8061874c = 1.0) on every active looping
// event sound owned by the current record (+0xac). Owner ref via __save_gpr_28.
void worm_event_reset_sound_pitch(void)
{
    int owner;
    int off;
    int i;
    int *rec;

    owner = (int)__save_gpr_28();
    off = 0;
    for (i = 0; i < DAT_806128f8; i = i + 1) {
        rec = (int *)(DAT_80612924 + off);
        if ((*(int *)(*rec + 0xac) == owner) && (-1 < rec[8]) &&
            (sound_is_playing(rec[8]) != 0)) {
            sound_set_pitch((double)FLOAT_8061874c, rec[8]);
        }
        off = off + 0x48;
    }
    __restore_gpr_28();
}

// 0x80232520 - Fade out and clear every active looping event sound owned by the
// current record (+0xac). When the spill gate is set, register a fade-out
// (sound_register_fade_out) before invalidating the handle (+8 = -1). Owner ref
// via the spill frame.
void worm_event_fade_sounds(void)
{
    int owner;
    int gate;
    int off;
    int i;
    int *rec;
    int handleSlot[10];

    FUN_803826e4();
    owner = 0;  /* owner ref (hi) and gate (lo) supplied via the spill frame */
    gate = 0;
    off = 0;
    for (i = 0; i < DAT_806128f8; i = i + 1) {
        rec = (int *)(DAT_80612924 + off);
        if (*(int *)(*rec + 0xac) == owner) {
            handleSlot[0] = rec[8];
            if (gate != 0) {
                sound_register_fade_out((double)FLOAT_8061874c, (double)FLOAT_80618784,
                                        handleSlot, (unsigned int *)0);
            }
            rec[8] = -1;
        }
        off = off + 0x48;
    }
    FUN_80382730();
}

// ============================================================================
// batch_698: event-record lookup / reset / list-build layer
// 10 fns 0x802325b8-0x80232cdc | mission-hooks | subsystem:game (+audio)
// ----------------------------------------------------------------------------
// The mission-event subsystem's record management layer: locate an execute-list
// record by objective-class id, test/reset record activity, build the execute
// list from the level's event table (with per-record chain-link setup), bind
// spawnpoint trigger records to their owning event records, pack per-record
// runtime state into the scratch/netsync table, and arm a chain link when a
// switch it watches becomes active.
// ============================================================================

// --- batch_698 file-local externs (raw symbols not yet declared above) ------
extern int   DAT_806128fc;   // "sound events present" scratch dirty flag
extern int   DAT_80612918;   // event-list init sentinel (0xffffffbb)
extern int   DAT_80612920;   // count of type-5/<0xe records registered in &DAT_805c7960
extern int   DAT_80465480;   // objective-type value-source callback table base (8-stride entries)
extern int   DAT_805c7960;   // registered "special" event-record pointer array base
extern float FLOAT_80618788; // rng normalize divisor (player-count magic)
extern void  statlvl_class_alloc_slot(int classId, int a, int b, int c, int d, int flags);
extern void  statlvl_class_set_param(short param);
extern int   spawn_check_flags_hub(unsigned int flags);        // spawnpoint active predicate
extern int   aiactor_spawnpoint_eval_trigger_open;             // spawnpoint trigger-eval fn address (compared as fnptr)
static int   worm_event_check_difficulty_gate(int eventDef);   // 0x802341e0, defined in batch_700 below; was event-type -> chain-link count (0/1/2)
extern int   FUN_8025b920(void);                               // active-switch id getter (DAT_806129bc)

// spill-frame helpers reused: FUN_803826f0 (returns u64: lo=ptr,hi=&outPtr),
// FUN_8038273c (release). worm_event_chain_link_evaluate declared above.

// batch_698 forward decls (within-batch self-calls)
static int *worm_event_find_record_by_class(int classId);
static void worm_event_setup_chain_link(int *linkSlot, int linkDesc, short slotIndex, int classId);
static void worm_event_bind_spawnpoint_triggers(void);
static void worm_event_pack_state_snapshot(void);

// batch_698 scratch/state block bases (memset targets, sign-extended in ROM)
extern int DAT_80612938; // event runtime-state block A (0x28 bytes)
extern int DAT_80612960; // event runtime-state block B (100 bytes)
extern int DAT_806129c4; // event runtime-state block C (0x28 bytes)

// 0x802325b8 | worm_event_find_record_by_class
// Scan the event-execute list (DAT_80612924, 0x48 stride, DAT_806128f8 records)
// for the record whose owning objective-class id (+0xac) matches classId.
static int *worm_event_find_record_by_class(int classId)
{
    int off = 0;
    int remaining = DAT_806128f8;
    if (0 < DAT_806128f8) {
        do {
            if (*(int *)(*(int *)(DAT_80612924 + off) + 0xac) == classId) {
                return (int *)(DAT_80612924 + off);
            }
            off += 0x48;
            remaining -= 1;
        } while (remaining != 0);
    }
    return (int *)0x0;
}

// 0x802325fc | worm_event_flag_player_by_team
// Resolve the player actor for teamId; if its render object (+0x7c) has any of
// mask 0x1201 set at +0x10, OR in bit 0x20000000 at +0x28 (mark for update).
static void worm_event_flag_player_by_team(int teamId)
{
    int actor = aiactor_find_player_by_team(teamId);
    if (actor != 0) {
        int obj = *(int *)(actor + 0x7c);
        if ((*(unsigned int *)(obj + 0x10) & 0x1201) != 0) {
            *(unsigned int *)(obj + 0x28) = *(unsigned int *)(obj + 0x28) | 0x20000000;
        }
    }
}

// 0x8023265c | worm_event_reset_record  [subsystem:audio]
// Reset an execute-list record: clear countdown/pos words (+0x2c..+0x38), reset
// countdown timer (+0x38) to the 0.0f threshold, fade out any live type-0xb
// looping sound it owns, and re-clear its 1-2 chain-link states via
// statlvl_class_set_state2.
static void worm_event_reset_record(int *rec)
{
    float threshold = FLOAT_80618744;
    if (rec != (int *)0x0) {
        rec[0xb] = 0;
        rec[0xc] = 0;
        rec[0xd] = 0;
        rec[0xe] = (int)threshold;
        if (*(int *)(*rec + 8) == 0xb) {
            double vol  = (double)FLOAT_8061874c;
            double time = (double)(float)((double)FLOAT_8061874c /
                          (double)(FLOAT_80618788 *
                                   (float)((double)CONCAT44(0x43300000,
                                                            (unsigned int)DAT_806109c0 ^ 0x80000000) -
                                           DOUBLE_80618758)));
            sound_register_fade_out(vol, time, rec + 8, (unsigned int *)0x0);
            rec[8] = -1;
        }
        rec[7] = 0;
        *(unsigned short *)(rec + 2) = 0;
        if (-1 < rec[3]) {
            statlvl_class_set_state2(rec[3]);
        }
        if (*(int *)(*rec + 0x60) != 0) {
            *(unsigned short *)(rec + 5) = 0;
            if (-1 < rec[6]) {
                statlvl_class_set_state2(rec[6]);
            }
        }
    }
}

// 0x80232740 | worm_event_record_is_active
// A record is active if non-null and its +0x34 word (chain-link state slot) != 0.
static bool worm_event_record_is_active(int rec)
{
    if (rec != 0) {
        return (bool)('\x01' - (*(int *)(rec + 0x34) == 0));
    }
    return false;
}

// 0x80232760 | worm_event_class_is_active
// Find the execute record owning objective-class classId, then test activity.
static bool worm_event_class_is_active(int classId)
{
    int *rec = worm_event_find_record_by_class(classId);
    return worm_event_record_is_active((int)rec);
}

// 0x80232784 | worm_event_setup_chain_link
// Initialize one chain-link slot. The link descriptor lives at linkDesc (the
// event def's +0x40 or +100 sub-block); linkSlot is the record's chain-link
// state slot (rec+1 or rec+4). For an existing-link mode (descriptor word == 4)
// bind the slot to the referenced record; for the "no class" modes
// (2 <= mode < 4) mark the slot id -1; otherwise allocate a fresh statlvl
// objective-class slot wired to worm_event_chain_link_evaluate.
// NON-MATCHING: the ROM reads (mode, linkedClass, outSlot) via a spill frame
// (FUN_803826f0); expressed here as the real inputs the caller provides.
static void worm_event_setup_chain_link(int *linkSlot, int linkDesc, short slotIndex, int classId)
{
    int mode = *(int *)linkDesc;
    if (mode == 4) {
        int *rec = worm_event_find_record_by_class(*(int *)(linkDesc + 4));
        *linkSlot = (int)rec;
    }
    else if ((mode < 4) && (1 < mode)) {
        linkSlot[2] = 0xffffffff;
        *(unsigned short *)(linkSlot + 1) = 0;
        FUN_8038273c();
        return;
    }
    statlvl_class_alloc_slot(classId, 0, 2, 2, 2, 0xe00077f);
    statlvl_class_set_param(slotIndex);
    statlvl_class_set_complete_cb((void *)worm_event_chain_link_evaluate);
    linkSlot[2] = classId;
    *(unsigned short *)(linkSlot + 1) = 0;
    FUN_8038273c();
}

// 0x80232830 | worm_event_build_lists
// Build the event-execute list for the current level: walk the level event-def
// table (info+0xcc base, +0xd0 count, 200-byte stride), keep only events whose
// spawn/cutscene flags pass, copy the def ptr into a 0x48-stride execute record,
// register type-5/<0xe events into the &DAT_805c7960 special array, then set up
// each record's 1-2 chain links (worm_event_setup_chain_link) and reset its
// runtime fields. Finally bind spawnpoint triggers and clear the sound-dirty flag.
static void worm_event_build_lists(void)
{
    FUN_803826dc();
    int classCursor = 0x2d;
    int lvl = level_get_info_cached(DAT_80612614);
    int defBase = *(int *)(lvl + 0xcc);
    int defCount = *(int *)(lvl + 0xd0);
    DAT_806128f8 = defCount;
    memset_wrapper((int)&DAT_80612938, 0, 0x28);  // -0x7fa386c8 = 0x80612938
    DAT_80612900 = 0;
    DAT_80612914 = 0;
    DAT_80612918 = 0xffffffbb;
    memset_wrapper((int)&DAT_80612960, 0, 100);   // -0x7fa386a0 = 0x80612960
    int recCursor = 0;
    int defIdx = 0;
    DAT_80612920 = 0;

    // pass 1: filter defs into execute records
    for (defIdx = 0; defIdx < defCount; defIdx += 1) {
        int def = defBase + defIdx * 200;
        unsigned int active = spawn_check_flags_or_cutscene(*(unsigned int *)(def + 0xb4));
        if (active == 0) {
            DAT_806128f8 = DAT_806128f8 - 1;
        }
        else {
            int *rec = (int *)(DAT_80612924 + recCursor);
            *rec = def;
            rec[0xf] = *(int *)(def + 0xb8);
            int etype = *(int *)(def + 0x40);
            if (etype < 0xc) {
                if (etype == 5) {
                    int *slot = &DAT_805c7960 + DAT_80612920;
                    DAT_80612920 = DAT_80612920 + 1;
                    *slot = (int)rec;
                }
            }
            else if ((etype != 0xe) && (etype < 0xe)) {
                int *slot = &DAT_805c7960 + DAT_80612920;
                DAT_80612920 = DAT_80612920 + 1;
                *slot = (int)rec;
            }
            recCursor += 0x48;
        }
    }

    // pass 2: per-record chain-link setup + runtime reset
    float threshold = (float)(double)FLOAT_80618744;
    int recOff = 0;
    for (int i = 0; i < DAT_806128f8; i += 1) {
        int *rec = (int *)(DAT_80612924 + recOff);
        int def = *rec;
        // bind linked-event owner (+0x5c mode, +0x58 target class id)
        if (*(int *)(def + 0x5c) != 0) {
            int scanOff = 0;
            int remaining = DAT_806128f8;
            if (0 < DAT_806128f8) {
                do {
                    if (*(int *)(def + 0x58) == *(int *)(*(int *)(DAT_80612924 + scanOff) + 0xac)) {
                        rec[10] = DAT_80612924 + scanOff;
                        break;
                    }
                    scanOff += 0x48;
                    remaining -= 1;
                } while (remaining != 0);
            }
        }
        int nlinks = worm_event_check_difficulty_gate(def);
        if (nlinks == 0) {
            rec[3] = -1;
            *(unsigned short *)(rec + 2) = 0;
            rec[6] = -1;
            *(unsigned short *)(rec + 5) = 0;
        }
        else {
            worm_event_setup_chain_link(rec + 1, def + 0x40, (short)i, classCursor);
            if (*(int *)(def + 0x60) == 0) {
                *(unsigned short *)(rec + 5) = 0;
                classCursor += 1;
            }
            else {
                worm_event_setup_chain_link(rec + 4, def + 100, (short)i, classCursor + 1);
                classCursor += 2;
            }
        }
        rec[0xb] = 0;
        rec[0xc] = 0;
        rec[0xd] = 0;
        rec[0xe] = (int)threshold;
        rec[7] = 0;
        rec[8] = -1;
        rec[9] = 0;
        recOff += 0x48;
    }

    worm_event_bind_spawnpoint_triggers();
    memset_wrapper((int)&DAT_806129c4, 0, 0x28);  // -0x7fa3863c = 0x806129c4
    DAT_806128fc = 0;
    FUN_80382728();
}

// 0x80232a84 | worm_event_bind_spawnpoint_triggers
// For each level spawnpoint whose flags pass and whose eval callback is the
// trigger-open handler, resolve the event record for its +0x38 class id and
// cache the record pointer into the spawnpoint at +0x3c.
static void worm_event_bind_spawnpoint_triggers(void)
{
    __save_gpr_28();
    if (DAT_8061208c != 0) {
        int off = 0;
        for (int i = 0; i < *(int *)(DAT_8061208c + 0x78); i += 1) {
            int sp = *(int *)(DAT_8061208c + 0x74) + off;
            int ok = spawn_check_flags_hub(*(unsigned int *)(sp + 0xc));
            if ((ok != 0) && (*(int *)(sp + 0x6c) == (int)&aiactor_spawnpoint_eval_trigger_open)) {
                int *rec = worm_event_find_record_by_class(*(int *)(sp + 0x38));
                *(int **)(sp + 0x3c) = rec;
            }
            off += 0x98;
        }
    }
    __restore_gpr_28();
}

// 0x80232b18 | worm_event_pack_state_snapshot
// Pack each execute record's runtime state into the 0x10-stride scratch table
// (DAT_8061291c) for netsync/save: byte-packed flags word, optional type value
// via the value-source callback table (DAT_80465480), and for type-3/type-0x2b
// events resolve the tracked entity handle (DAT_80611c24, 0x2e8 stride, +0x2c gen)
// and copy its transform words.
static void worm_event_pack_state_snapshot(void)
{
    FUN_803826e4();
    DAT_806128fc = 0;
    int recOff = 0;
    int scratchOff = 0;
    int defOff = 0;
    for (int i = 0; i < DAT_806128f8; i += 1) {
        int *rec = (int *)(DAT_80612924 + recOff);
        int def = *rec;
        *(unsigned int *)(DAT_8061291c + scratchOff) =
            (*(unsigned short *)(rec + 5) & 0xff) |
            ((*(unsigned short *)(rec + 2) & 0xff) << 8) |
            ((rec[0xd] & 0xff) << 0x10) |
            (rec[0xb] << 0x18);
        int typeSel = *(int *)(*(int *)(DAT_8061208c + 0xcc) + defOff + 0xbc);
        if (typeSel != 0) {
            unsigned int val = (*(unsigned int (**)(void))(&DAT_80465480 + typeSel * 8))();
            *(unsigned int *)(DAT_8061291c + scratchOff + 4) = val;
        }
        if (*(int *)(def + 8) == 3) {
            unsigned int handle = rec[9];
            int ent;
            if (handle == 0) {
                ent = 0;
            }
            else {
                ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
                if (*(unsigned int *)(ent + 0x2c) != handle) {
                    ent = 0;
                    rec[9] = 0;
                }
            }
            if (((ent != 0) && (ent = *(int *)(ent + 4), ent != 0)) &&
                (*(int *)(ent + 0xcc) != 0)) {
                *(unsigned int *)(DAT_8061291c + scratchOff + 0xc) = *(unsigned int *)(ent + 0x100);
                *(unsigned int *)(DAT_8061291c + scratchOff + 8)   = *(unsigned int *)(ent + 0xd4);
            }
        }
        if ((*(int *)(def + 8) == 0x2b) && (rec[0xb] != 0)) {
            unsigned int handle = rec[9];
            int ent;
            if (handle == 0) {
                ent = 0;
            }
            else {
                ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
                if (*(unsigned int *)(ent + 0x2c) != handle) {
                    ent = 0;
                    rec[9] = 0;
                }
            }
            if ((ent != 0) && (*(int *)(ent + 4) != 0)) {
                *(unsigned int *)(DAT_8061291c + scratchOff) = 0;
            }
        }
        defOff += 200;
        scratchOff += 0x10;
        recOff += 0x48;
    }
    FUN_80382730();
}

// 0x80232cdc | worm_event_arm_switch_link
// If a chain link watches a switch (type 0x13) and the currently-active switch
// id matches the link's watched id (+2 word), arm the link.
static void worm_event_arm_switch_link(int *link, int linkRec)
{
    if ((*link == 0x13) && (link[2] == FUN_8025b920())) {
        worm_event_set_chain_armed(linkRec);
    }
}

// ============================================================================
// batch_698b (batch_699): event snapshot-apply / deferred-anim replay /
//                         destroy+switch notification layer
// 10 fns 0x80232d38-0x8023351c | mission-hooks | subsystem:game
// ----------------------------------------------------------------------------
// The netsync/replay side of the event engine: apply a packed state snapshot
// back onto the execute list and re-tick each record (worm_event_apply_snapshot_tick),
// queue and later flush deferred animation-gated events once their level actors
// finish loading, and the entity-destroyed / switch-changed notification hooks
// that arm the chain links watching those world events.
// ============================================================================

// --- batch_699 file-local externs -------------------------------------------
extern int   DAT_805c79c4;   // deferred-anim replay queue base (int[] of record indices)
extern int   DAT_80465484;   // value-source callback table +4 (paired w/ DAT_80465480)
extern int   FUN_80233608(void); // condition-evaluator dispatch (next batch; returns satisfied flag)

// Note: worm_event_execute / worm_event_record_is_active / worm_event_reset_record /
// worm_event_arm_switch_link / worm_event_set_chain_armed are all defined static
// earlier in this file and are in scope here. anim_get_descriptor, level_actor_is_loaded,
// switch_toggle_state, and the DAT_* event globals are declared above.
// countLeadingZeros is a Ghidra intrinsic from common.h.

// batch_699 forward decls (within-batch self-calls)
static void worm_event_queue_deferred_anim(int recordIndex);
static int  worm_event_link_match_entity(int *record, int *entity, int linkSel, int isDestroyed);
static void worm_event_link_arm_by_switch(int *record, int switchObj, int linkSel);
static void worm_event_notify_entity_destroyed(int entityHandle, int entity, int isDestroyed);
static void worm_event_notify_switch_changed(int switchObj);

// 0x80232d38 | worm_event_apply_snapshot_tick
// Unpack the 0x10-stride scratch snapshot (DAT_8061291c) back onto each execute
// record (DAT_80612924, 0x48 stride), then per record: re-run the value-source
// callback (DAT_80465484 table), arm its 1-2 chain links, fire worm_event_execute
// for armed type-0x1b / flagged records, reset finished type-0x2b records, and
// for anim-gated type-3 events either fire the execute (copying the tracked
// entity transform) or defer via worm_event_queue_deferred_anim.
// NON-MATCHING: ROM threads a spill frame + register args; expressed as the
// documented worm_event_execute(record) form.
static void worm_event_apply_snapshot_tick(void)
{
    int scratchOff = 0;
    int recOff = 0;
    for (int i = 0; i < DAT_806128f8; i += 1) {
        int *rec = (int *)(DAT_80612924 + recOff);
        int def = *rec;
        unsigned int packed = *(unsigned int *)(DAT_8061291c + scratchOff);
        rec[0xb] = packed >> 0x18;
        rec[0xd] = (packed >> 0x10) & 0xff;
        *(unsigned short *)(rec + 2) = (unsigned short)((packed >> 8) & 0xff);
        *(unsigned short *)(rec + 5) = (unsigned short)(packed & 0xff);

        if ((rec[0xb] != 0) && (!worm_event_record_is_active((int)rec))) {
            rec[0xe] = *(int *)(*rec + 0xa4);
        }

        int etype = *(int *)(def + 8);
        if (etype == 0x1b) {
            if (worm_event_record_is_active((int)rec)) {
                worm_event_execute(rec);
            }
        }
        else {
            if ((etype != 3) && ((*(unsigned int *)(def + 0xb0) & 0x1000) != 0)) {
                if (worm_event_record_is_active((int)rec)) {
                    worm_event_execute(rec);
                }
            }
            else if (worm_event_record_is_active((int)rec) &&
                     ((*(unsigned int *)(def + 0xb0) & 0x20) == 0)) {
                rec[7] = 1;
            }
        }

        if ((*(int *)(def + 8) == 0x2b) && (rec[0xb] == 0)) {
            worm_event_reset_record(rec);
        }

        // re-run the packed value-source callback (netsync value apply)
        if (*(int *)(def + 0xbc) != 0) {
            int sel = *(int *)(def + 0xbc);
            (**(void (**)(int))(&DAT_80465484 + sel * 8))(*(int *)(DAT_8061291c + scratchOff + 4));
        }

        // arm the record's chain links (may match a switch that is already active)
        worm_event_arm_switch_link((int *)(def + 0x40), (int)(rec + 1));
        if (*(int *)(def + 0x60) != 0) {
            worm_event_arm_switch_link((int *)(def + 100), (int)(rec + 4));
        }

        // anim-gated type-3: fire now if descriptor ready & not multiplayer-suppressed,
        // else defer to the flush queue
        if ((*(int *)(def + 8) == 3) && ((*(unsigned int *)(def + 0xb0) & 0x10) != 0)) {
            if (worm_event_record_is_active((int)rec)) {
                int desc = anim_get_descriptor((int)*(short *)(*rec + 0x18));
                int animIdx = *(int *)(*rec + 0x10) * 2;
                int actor = DAT_806125e8 + *(short *)(&DAT_805c9858 + animIdx) * 0x30;
                unsigned int suppress;
                if ((DAT_805015c0 & 0x40) == 0) {
                    if ((DAT_80612614 == 100) || (DAT_80612614 == 0x38)) {
                        suppress = 0;
                    }
                    else if ((DAT_805015bd == '\n') || (DAT_80612614 == 0x66)) {
                        suppress = *(unsigned int *)(actor + 0x28) & 0x2000000;
                    }
                    else {
                        suppress = 0;
                    }
                }
                else {
                    suppress = 0;
                }

                if ((suppress == 0) && (desc != 0)) {
                    worm_event_execute(rec);
                    unsigned int handle = rec[9];
                    int ent;
                    if (handle == 0) {
                        ent = 0;
                    }
                    else {
                        ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
                        if (*(unsigned int *)(ent + 0x2c) != handle) {
                            ent = 0;
                            rec[9] = 0;
                        }
                    }
                    if (((ent != 0) && (ent = *(int *)(ent + 4), ent != 0)) &&
                        (*(int *)(ent + 0xcc) != 0)) {
                        anim_get_descriptor(*(int *)(ent + 0xcc));
                        *(unsigned int *)(ent + 0xd4)  = *(unsigned int *)(DAT_8061291c + scratchOff + 8);
                        *(unsigned int *)(ent + 0x100) = *(unsigned int *)(DAT_8061291c + scratchOff + 0xc);
                    }
                }
                else {
                    worm_event_queue_deferred_anim(i);
                    rec[0xc] = 0;
                    rec[7] = 0;
                }
            }
        }

        scratchOff += 0x10;
        recOff += 0x48;
    }
}

// 0x80233038 | worm_event_queue_deferred_anim
// Append recordIndex to the deferred-anim replay queue (DAT_805c79c4), unless the
// record's anim id is the "none" sentinel (0x2ec) or it is already queued.
static void worm_event_queue_deferred_anim(int recordIndex)
{
    if (*(int *)(*(int *)(DAT_80612924 + recordIndex * 0x48) + 0x10) == 0x2ec) {
        return;
    }
    int off = 0;
    int remaining = DAT_806128fc;
    if (0 < DAT_806128fc) {
        do {
            if (recordIndex == *(int *)((int)&DAT_805c79c4 + off)) {
                return;
            }
            off += 4;
            remaining -= 1;
        } while (remaining != 0);
    }
    (&DAT_805c79c4)[DAT_806128fc] = recordIndex;
    DAT_806128fc = DAT_806128fc + 1;
}

// 0x802330a0 | worm_event_flush_deferred_anim
// Replay each queued deferred-anim event once its level actor has loaded and its
// anim descriptor is ready: fire worm_event_execute, copy the tracked entity's
// transform, and clear the queue slot. Clears the whole queue when all drained.
static void worm_event_flush_deferred_anim(void)
{
    bool allDone = true;
    if (DAT_80612614 < 0x65) {
        int off = 0;
        for (int i = 0; i < DAT_806128fc; i += 1) {
            unsigned int loaded = level_actor_is_loaded();
            if (loaded == 0) {
                allDone = false;
            }
            else if (*(int *)((int)&DAT_805c79c4 + off) < 0) {
                allDone = false;
            }
            else {
                int *rec = (int *)(DAT_80612924 + *(int *)((int)&DAT_805c79c4 + off) * 0x48);
                if ((rec[0xc] == 0) && (rec[7] == 0)) {
                    int def = *rec;
                    if (*(short *)(&DAT_805c9858 + *(int *)(def + 0x10) * 2) == -1) {
                        allDone = false;
                    }
                    else {
                        int desc = anim_get_descriptor((int)*(short *)(def + 0x18));
                        if (desc == 0) {
                            allDone = false;
                        }
                        else {
                            worm_event_execute(rec);
                            unsigned int handle = rec[9];
                            int ent;
                            if (handle == 0) {
                                ent = 0;
                            }
                            else {
                                ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
                                if (*(unsigned int *)(ent + 0x2c) != handle) {
                                    ent = 0;
                                    rec[9] = 0;
                                }
                            }
                            if (((ent != 0) && (ent = *(int *)(ent + 4), ent != 0)) &&
                                (*(int *)(ent + 0xcc) != 0)) {
                                anim_get_descriptor(*(int *)(ent + 0xcc));
                                int idx = ((int)rec - DAT_80612924) / 0x48;
                                *(unsigned int *)(ent + 0xd4) =
                                    *(unsigned int *)(DAT_8061291c + idx * 0x10 + 8);
                                *(unsigned int *)(ent + 0x100) =
                                    *(unsigned int *)(DAT_8061291c + idx * 0x10 + 0xc);
                            }
                            *(int *)((int)&DAT_805c79c4 + off) = 0xffffffff;
                        }
                    }
                }
                else {
                    *(int *)((int)&DAT_805c79c4 + off) = 0xffffffff;
                }
            }
            off += 4;
        }
        if (allDone) {
            DAT_806128fc = 0;
        }
    }
}

// 0x8023325c | worm_event_link_match_entity
// Test whether a record's chain link (linkSel 1 -> +0x40/rec+1, else +100/rec+4)
// is an entity-destroy watcher (type 5) matching *entity; if so and isDestroyed,
// arm the link. Returns 1 on match.
static int worm_event_link_match_entity(int *record, int *entity, int linkSel, int isDestroyed)
{
    int *linkRec;
    int *linkDesc;
    if (linkSel == 1) {
        linkRec  = record + 1;
        linkDesc = (int *)(*record + 0x40);
    }
    else {
        linkRec  = record + 4;
        linkDesc = (int *)(*record + 100);
    }
    if ((*linkDesc == 5) && (linkDesc[1] == *entity)) {
        if (isDestroyed != 0) {
            worm_event_set_chain_armed((int)linkRec);
        }
        return 1;
    }
    return 0;
}

// 0x802332d0 | worm_event_notify_entity_destroyed
// On an entity destroy/change, walk the "special" event-record list (DAT_805c7960,
// DAT_80612920 entries) and arm any type-5 links watching this entity; when a
// match fires and the event isn't switch-suppressed (+0xb0 & 0x2000), toggle the
// associated switch state once. isDestroyed==0 stashes the entity ptr/handle into
// the record (+0x40/+0x44) instead of resetting it.
// NON-MATCHING: entity comes via spill frame (FUN_803826d0); expressed as params.
static void worm_event_notify_entity_destroyed(int entityHandle, int entity, int isDestroyed)
{
    int *ent = (int *)entity;
    bool toggled = false;
    if ((ent[4] & 1) == 0) {
        unsigned int destroyedFlag = countLeadingZeros(isDestroyed) >> 5;
        int off = 0;
        for (int i = 0; i < DAT_80612920; i += 1) {
            int *rec = *(int **)((int)&DAT_805c7960 + off);
            int matched2 = 0;
            if (rec != (int *)0x0) {
                int matched1 = worm_event_link_match_entity(rec, ent, 1, destroyedFlag);
                if (*(int *)(*rec + 0x60) != 0) {
                    matched2 = worm_event_link_match_entity(rec, ent, 2, destroyedFlag);
                }
                if ((matched1 != 0) || (matched2 != 0)) {
                    if (isDestroyed == 0) {
                        rec[0x10] = *ent;
                        rec[0x11] = entityHandle;
                    }
                    else {
                        worm_event_reset_record(rec);
                    }
                    if ((!toggled) && ((*(unsigned int *)(*rec + 0xb0) & 0x2000) == 0)) {
                        switch_toggle_state((int)ent);
                        toggled = true;
                    }
                }
            }
            off += 4;
        }
    }
}

// 0x802333ec | worm_event_on_entity_destroyed
// Public hook: notify with isDestroyed = 0 (entity gone / disabled).
static void worm_event_on_entity_destroyed(int entityHandle, int entity)
{
    worm_event_notify_entity_destroyed(entityHandle, entity, 0);
}

// 0x80233410 | worm_event_link_arm_by_switch
// Arm a record's chain link (linkSel) if it watches a switch-state condition
// (link type in [0xc,0xe)) whose watched id matches the switch object's +0x60 id.
static void worm_event_link_arm_by_switch(int *record, int switchObj, int linkSel)
{
    int *linkRec;
    int *linkDesc;
    if (linkSel == 1) {
        linkRec  = record + 1;
        linkDesc = (int *)(*record + 0x40);
    }
    else {
        linkRec  = record + 4;
        linkDesc = (int *)(*record + 100);
    }
    if (((*linkDesc < 0xe) && (0xb < *linkDesc)) &&
        (linkDesc[1] == *(int *)(switchObj + 0x60))) {
        worm_event_set_chain_armed((int)linkRec);
    }
}

// 0x8023347c | worm_event_notify_switch_changed
// Walk the special event-record list and arm any switch-watching links for the
// switch object that just changed.
// NON-MATCHING: switchObj arrives via spill frame (FUN_803826e8) in ROM;
// expressed here as the caller-supplied parameter (Compile-Clean rule 3).
static void worm_event_notify_switch_changed(int switchObj)
{
    int off = 0;
    for (int i = 0; i < DAT_80612920; i += 1) {
        int *rec = *(int **)((int)&DAT_805c7960 + off);
        if (rec != (int *)0x0) {
            worm_event_link_arm_by_switch(rec, switchObj, 1);
            if (*(int *)(*rec + 0x60) != 0) {
                worm_event_link_arm_by_switch(rec, switchObj, 2);
            }
        }
        off += 4;
    }
    FUN_80382734();
}

// 0x80233510 | worm_event_mark_condition_met
// Set the "condition satisfied" flag (+0x1c = 1) on a condition record.
static void worm_event_mark_condition_met(int condRecord)
{
    *(int *)(condRecord + 0x1c) = 1;
}

// 0x8023351c | worm_event_both_conditions_met
// True only if the condition-evaluator dispatch (worm_event_condition_eval,
// 0x80233608) reports satisfied on two successive polls.
static int worm_event_both_conditions_met(void)
{
    int result = 0;
    if ((FUN_80233608() != 0) && (FUN_80233608() != 0)) {
        result = 1;
    }
    return result;
}

// ============================================================================
// batch_700: objective-condition evaluator dispatch + zone-match predicates +
//            difficulty-gated condition-list build + timed-countdown machinery
// 10 fns 0x80233578-0x802341e0 | mission-hooks | subsystem:game
// ----------------------------------------------------------------------------
// The condition side of the event engine: the big per-type condition dispatch
// (worm_event_condition_eval) plus its zone-membership predicates, the routine
// that builds the active condition list filtered by the difficulty gate, and
// the allocate/tick pair for timed "countdown" objective events (with the HUD
// banner + per-player camera interpolation).
// ============================================================================

// --- batch_700 file-local externs (raw symbols) ----------------------------
extern int   FUN_8023c32c(int a);   // held-object / carrier query (returns handle-ish)
extern int   FUN_8027ea98(void);    // local-player actor resolver
extern char  DAT_80501602;          // current difficulty level (0/1/2)
extern float FLOAT_8061878c;        // camera-lerp wrap epsilon
extern float FLOAT_80618790;        // camera-lerp bias / table scale
extern float FLOAT_80618794;        // camera-lerp table index scale
extern int   DAT_80583228;          // sine/interp lookup table base (0x800-entry masked)

// Named cross-module helpers used by this batch. This TU includes only
// common.h, so every named helper needs a file-local extern (matching the
// call-site arity/type). countLeadingZeros/ABS are common.h intrinsics.
extern int  player_ai_is_sniper_weapon(int weaponType);
extern int  soundpad_find_zone_for_entity(int zoneId);
extern int  worm_draw_gate_is_closed(void);
extern int  worm_cdh1_payload_fire5(void);
extern int  worm_swarm_is_active(void);
extern int  worm_wlft_bot_trigger_check(void);
extern int  shutter_gas0_closed(void);
extern int  shutter_gas1_closed(void);
extern int  shutter_gas0_spawn_gate(void);
extern int  shutter_gas1_spawn_gate(void);
extern int  shutter_any_gas_active(void);
extern int  shutter_music_ready_gate(void);
extern int  shutter_gas_ready(void);
extern int  artr_is_settled(void);
extern int  worm_spark_burst_tick(void);
extern int  arc_entity_type_allowed(int entity);
extern void *award_get_entry(int levelId);
extern void match_reset_cameras_and_spawn(void);
extern void input_reset_pad(int padIndex);
// NOTE: statlvl_class_is_state5, switch_find_slot_by_entity, object_fade_begin,
// hud_banner_show, soundpad_get_entity_id_from_zone already declared above.

// batch_700 forward decls (within-batch self-calls)
static int  worm_cond_count_snipers(void);
static int  worm_cond_entity_zone_matches(int condDesc, int player);
static int  worm_cond_entity_zone_matches_node(int condDesc, int player);
static int  worm_cond_entity_zone_matches_mapped(int condDesc, int player);
static void worm_event_build_condition_list(void);
static int  worm_event_check_difficulty_gate(int eventDef);

// 0x80233578 | worm_cond_count_snipers
// Count how many active players (DAT_80612610..DAT_80612098 in DAT_80588610,
// 4-stride) are holding a sniper-class weapon; stops early at 2.
// NON-MATCHING: ROM returns the count via a spill register; recomputed here.
static int worm_cond_count_snipers(void)
{
    __save_gpr_28();
    int count = 0;
    int off = DAT_80612610 << 2;
    int idx = DAT_80612610;
    while ((idx < DAT_80612098) && (count < 2)) {
        int isSniper = player_ai_is_sniper_weapon(
            (int)*(short *)(*(int *)((int)&DAT_80588610 + off) + 0x48));
        if (isSniper != 0) {
            count += 1;
        }
        idx += 1;
        off += 4;
    }
    __restore_gpr_28();
    return count;
}

// 0x80233b88 | worm_cond_entity_zone_matches
// True if the player's held-object zone id (via soundpad_get_entity_id_from_zone
// on +0x678) matches any of the condition descriptor's 4 target zone ids
// (condDesc +4/+8/+0xc/+0x10, -1 = unused).
static int worm_cond_entity_zone_matches(int condDesc, int player)
{
    if (player != 0) {
        int zid = soundpad_get_entity_id_from_zone(*(int *)(player + 0x678));
        if (((-1 < *(int *)(condDesc + 4))    && (zid == *(int *)(condDesc + 4))) ||
            ((-1 < *(int *)(condDesc + 8))    && (zid == *(int *)(condDesc + 8))) ||
            ((-1 < *(int *)(condDesc + 0xc))  && (zid == *(int *)(condDesc + 0xc))) ||
            ((-1 < *(int *)(condDesc + 0x10)) && (zid == *(int *)(condDesc + 0x10)))) {
            return 1;
        }
    }
    return 0;
}

// 0x80233c1c | worm_cond_entity_zone_matches_node
// As above but resolves the zone via soundpad_resolve_zone_node then
// soundpad_get_entity_zone_id.
static int worm_cond_entity_zone_matches_node(int condDesc, int player)
{
    if (player != 0) {
        int *padEntry = soundpad_resolve_zone_node(*(int *)(player + 0x678));
        int zid = soundpad_get_entity_zone_id((int)padEntry);
        if (((-1 < *(int *)(condDesc + 4))    && (zid == *(int *)(condDesc + 4))) ||
            ((-1 < *(int *)(condDesc + 8))    && (zid == *(int *)(condDesc + 8))) ||
            ((-1 < *(int *)(condDesc + 0xc))  && (zid == *(int *)(condDesc + 0xc))) ||
            ((-1 < *(int *)(condDesc + 0x10)) && (zid == *(int *)(condDesc + 0x10)))) {
            return 1;
        }
    }
    return 0;
}

// 0x80233cb4 | worm_cond_entity_zone_matches_mapped
// As _node but maps the resolved zone through soundpad_find_zone_for_entity
// before comparing.
static int worm_cond_entity_zone_matches_mapped(int condDesc, int player)
{
    if (player != 0) {
        int *padEntry = soundpad_resolve_zone_node(*(int *)(player + 0x678));
        int zid = soundpad_get_entity_zone_id((int)padEntry);
        zid = soundpad_find_zone_for_entity(zid);
        if (((-1 < *(int *)(condDesc + 4))    && (zid == *(int *)(condDesc + 4))) ||
            ((-1 < *(int *)(condDesc + 8))    && (zid == *(int *)(condDesc + 8))) ||
            ((-1 < *(int *)(condDesc + 0xc))  && (zid == *(int *)(condDesc + 0xc))) ||
            ((-1 < *(int *)(condDesc + 0x10)) && (zid == *(int *)(condDesc + 0x10)))) {
            return 1;
        }
    }
    return 0;
}

// 0x80233608 | worm_event_condition_eval
// Per-condition-type dispatch: given a condition record (spill frame in ROM),
// evaluate its type (*rec) and run the matching predicate over the active player
// list (DAT_80611d78, 0x141c stride, DAT_80612610 players) or a global gate.
// Types: 0 always; 1..9 subsystem gates (draw-gate/payload/swarm/wlft-bot/gas
// shutters); 0xb..0x16 zone/held-object membership over players (via the three
// zone predicates, "any" vs "all" polarity); 0x17..0x1b local-player variants;
// 0x1c..0x1f misc gates; 0x20 class-state5; 0x21 switch-by-entity; 0x22..0x24
// held-object; 0x25 sniper count.
// NON-MATCHING: condition record arrives via spill frame; expressed as a local.
static void worm_event_condition_eval(void)
{
    unsigned int *rec = (unsigned int *)__save_gpr_28();
    if (rec != (unsigned int *)0x0) {
        switch (*rec) {
        case 0:
            break;
        case 1:  worm_draw_gate_is_closed(); break;
        case 2:  worm_cdh1_payload_fire5();  break;
        case 3:  worm_swarm_is_active();     break;
        case 4:  worm_wlft_bot_trigger_check(); break;
        case 5:  shutter_gas0_closed();      break;
        case 6:  shutter_gas1_closed();      break;
        case 7:  shutter_gas0_spawn_gate();  break;
        case 8:  shutter_gas1_spawn_gate();  break;
        case 9:  shutter_any_gas_active();   break;
        case 10: break;
        case 0xb: {  // any player in zone (via id-from-zone)
            int off = 0;
            for (int i = 0; i < DAT_80612610; i += 1) {
                int m = worm_cond_entity_zone_matches((int)rec,
                            *(int *)(*(int *)(DAT_80611d78 + off + 0x14) + 0x7c));
                if (m != 0) break;
                off += 0x141c;
            }
            break;
        }
        case 0xc: {  // all players in zone (id-from-zone)
            int off = 0;
            for (int i = 0; i < DAT_80612610; i += 1) {
                int m = worm_cond_entity_zone_matches((int)rec,
                            *(int *)(*(int *)(DAT_80611d78 + off + 0x14) + 0x7c));
                if (m == 0) break;
                off += 0x141c;
            }
            break;
        }
        case 0xd: {  // any player in zone (id-from-zone, dup polarity)
            int off = 0;
            for (int i = 0; i < DAT_80612610; i += 1) {
                int m = worm_cond_entity_zone_matches((int)rec,
                            *(int *)(*(int *)(DAT_80611d78 + off + 0x14) + 0x7c));
                if (m != 0) break;
                off += 0x141c;
            }
            break;
        }
        case 0xe: {  // any player in zone (via node)
            int off = 0;
            for (int i = 0; i < DAT_80612610; i += 1) {
                int m = worm_cond_entity_zone_matches_node((int)rec,
                            *(int *)(*(int *)(DAT_80611d78 + off + 0x14) + 0x7c));
                if (m != 0) break;
                off += 0x141c;
            }
            break;
        }
        case 0xf: {  // any player in zone (via mapped)
            int off = 0;
            for (int i = 0; i < DAT_80612610; i += 1) {
                int m = worm_cond_entity_zone_matches_mapped((int)rec,
                            *(int *)(*(int *)(DAT_80611d78 + off + 0x14) + 0x7c));
                if (m != 0) break;
                off += 0x141c;
            }
            break;
        }
        case 0x10:  // player-2 in zone (via node)
            if (1 < DAT_80612610) {
                worm_cond_entity_zone_matches_node((int)rec,
                    *(int *)(*(int *)(DAT_80611d78 + 0x1430) + 0x7c));
            }
            break;
        case 0x11: {  // all players in zone (via node)
            int off = 0;
            for (int i = 0; i < DAT_80612610; i += 1) {
                int m = worm_cond_entity_zone_matches_node((int)rec,
                            *(int *)(*(int *)(DAT_80611d78 + off + 0x14) + 0x7c));
                if (m == 0) break;
                off += 0x141c;
            }
            break;
        }
        case 0x12: {  // all players in zone (via mapped)
            int off = 0;
            for (int i = 0; i < DAT_80612610; i += 1) {
                int m = worm_cond_entity_zone_matches_mapped((int)rec,
                            *(int *)(*(int *)(DAT_80611d78 + off + 0x14) + 0x7c));
                if (m == 0) break;
                off += 0x141c;
            }
            break;
        }
        case 0x13: {  // any player mapped-zone (CLZ short-circuit)
            int off = 0;
            for (int i = 0; i < DAT_80612610; i += 1) {
                int r = worm_cond_entity_zone_matches_mapped((int)rec,
                            *(int *)(*(int *)(DAT_80611d78 + off + 0x14) + 0x7c));
                if ((countLeadingZeros(r) >> 5) != 0) break;
                off += 0x141c;
            }
            break;
        }
        case 0x14: {  // any player node-zone (CLZ short-circuit)
            int off = 0;
            for (int i = 0; i < DAT_80612610; i += 1) {
                int r = worm_cond_entity_zone_matches_node((int)rec,
                            *(int *)(*(int *)(DAT_80611d78 + off + 0x14) + 0x7c));
                if ((countLeadingZeros(r) >> 5) != 0) break;
                off += 0x141c;
            }
            break;
        }
        case 0x15: {  // any player node-zone
            int off = 0;
            for (int i = 0; i < DAT_80612610; i += 1) {
                int m = worm_cond_entity_zone_matches_node((int)rec,
                            *(int *)(*(int *)(DAT_80611d78 + off + 0x14) + 0x7c));
                if (m != 0) break;
                off += 0x141c;
            }
            break;
        }
        case 0x16: {  // any player mapped-zone
            int off = 0;
            for (int i = 0; i < DAT_80612610; i += 1) {
                int m = worm_cond_entity_zone_matches_mapped((int)rec,
                            *(int *)(*(int *)(DAT_80611d78 + off + 0x14) + 0x7c));
                if (m != 0) break;
                off += 0x141c;
            }
            break;
        }
        case 0x17: { int p = FUN_8027ea98(); worm_cond_entity_zone_matches((int)rec, p); break; }
        case 0x18: { int p = FUN_8027ea98(); worm_cond_entity_zone_matches_node((int)rec, p); break; }
        case 0x19: { int p = FUN_8027ea98(); int r = worm_cond_entity_zone_matches_node((int)rec, p); countLeadingZeros(r); break; }
        case 0x1a: { int p = FUN_8027ea98(); worm_cond_entity_zone_matches_mapped((int)rec, p); break; }
        case 0x1b: FUN_8027ea98(); break;
        case 0x1c: shutter_music_ready_gate(); break;
        case 0x1d: shutter_gas_ready();        break;
        case 0x1e: artr_is_settled();          break;
        case 0x1f: worm_spark_burst_tick();    break;
        case 0x20: statlvl_class_is_state5(rec[1]); break;
        case 0x21: switch_find_slot_by_entity(rec[1]); break;
        case 0x22: { int r = FUN_8023c32c(rec[1]); countLeadingZeros(r); break; }
        case 0x23: FUN_8023c32c(rec[1]); break;
        case 0x24: { int h = FUN_8023c32c(rec[1]); arc_entity_type_allowed(h); break; }
        case 0x25: worm_cond_count_snipers(); break;
        }
    }
    __restore_gpr_28();
}

// 0x80233d50 | worm_event_countdown_alloc
// For a countdown-type event (+0xc == 1): allocate a 4-byte counter, seed it from
// the event's per-player scaled target (+0x10 * playerCount), and begin the HUD
// fade. Stores the counter ptr at record+4. Called by worm_event_execute.
// NON-MATCHING: ROM passes counter target via spill/regs; expressed directly.
static void worm_event_countdown_alloc(int record, int eventDef)
{
    if (*(int *)(eventDef + 0xc) == 1) {
        int counter = mem_alloc(4, 0);
        *(int *)(record + 4) = counter;
        **(int **)(record + 4) =
            (int)(*(float *)(eventDef + 0x10) *
                  (float)((double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000) -
                          DOUBLE_80618758));
        object_fade_begin(1);
    }
}

// 0x80233de4 | worm_event_countdown_tick
// Per-frame tick for an active countdown event: decrement the counter, show the
// remaining-count HUD banner when it changes, drive each player's camera lerp
// toward the award pose via the sine table, and on expiry release the counter +
// end the fade + reset pads.
// NON-MATCHING: record + eventDef arrive via spill frame (FUN_803826dc) in ROM;
// expressed here as caller-supplied parameters (Compile-Clean rule 3).
static void worm_event_countdown_tick(int record, int eventDef)
{
    int outSlot = record;
    if (*(int *)(eventDef + 0xc) == 1) {
        unsigned int *counter = *(unsigned int **)(outSlot + 4);
        double pcount = (double)(long long)((unsigned long long)DAT_806109c0 ^ 0x80000000ULL);
        unsigned int cur = *counter;
        unsigned int fullSpan = (unsigned int)((float)(pcount - DOUBLE_80618758) *
                                (*(float *)(eventDef + 0x10) - *(float *)(eventDef + 0x14)));
        unsigned int target = (unsigned int)(*(float *)(eventDef + 0x10) *
                              (float)((double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000) -
                                      DOUBLE_80618758));
        short *award = (short *)award_get_entry(DAT_80612614);
        if ((DAT_805015c0 & 0x40000000) != 0) {
            match_reset_cameras_and_spawn();
        }
        if (cur == target) {
            hud_banner_show(target, 0);
            int off = 0;
            for (int i = 0; i < DAT_80612610; i += 1) {
                *(int *)(DAT_80611d78 + off + 0x100) = *(int *)((int)award + 0x34);
                *(int *)(DAT_80611d78 + off + 0x104) = *(int *)((int)award + 0x38);
                off += 0x141c;
            }
        }
        *counter = cur - 1;
        double curD = (double)(long long)(unsigned int)(cur ^ 0x80000000);
        float remaining = (float)(curD - DOUBLE_80618758);
        float expiry = -*(float *)(eventDef + 0x18) *
                       (float)((double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000) -
                               DOUBLE_80618758);
        if (expiry < remaining) {
            if (((int)cur < (int)fullSpan) && (0 < (int)cur)) {
                // camera lerp toward award pose (x then y), sine-table eased
                float ax = *(float *)((int)award + 0x34);
                float ax2 = *(float *)((int)award + 0x3c);
                float dx = ax2 - (ax + FLOAT_8061878c);
                float baseX = ABS(ax2 - ax);
                float pickX = ax2 - (ax - FLOAT_8061878c);
                if (ABS(dx) <= baseX) {
                    if (ABS(dx) < ABS(pickX)) pickX = dx;
                } else if (baseX < ABS(pickX)) {
                    pickX = ax2 - ax;
                }
                float ay = *(float *)((int)award + 0x38);
                float ay2 = *(float *)((int)award + 0x40);
                float dy = ay2 - (ay + FLOAT_8061878c);
                float baseY = ABS(ay2 - ay);
                float pickY = ay2 - (ay - FLOAT_8061878c);
                if (ABS(dy) <= baseY) {
                    if (ABS(dy) < ABS(pickY)) pickY = dy;
                } else if (baseY < ABS(pickY)) {
                    pickY = ay2 - ay;
                }
                double fullSpanD = (double)(long long)(unsigned int)(fullSpan ^ 0x80000000);
                float ox = *(float *)((int)award + 0x34);
                float oy = *(float *)((int)award + 0x38);
                float t = ((float)(curD - DOUBLE_80618758) -
                           (float)((double)(long long)(unsigned int)(cur ^ 0x80000000) - DOUBLE_80618758)) /
                          (float)(fullSpanD - DOUBLE_80618758);
                int tableIdx = (int)(FLOAT_80618794 * FLOAT_80618760 * t - FLOAT_80618790 + FLOAT_80618790) & 0x7ff;
                float ease = FLOAT_80618790 * (float)(&DAT_80583228)[tableIdx] + FLOAT_80618790;
                int off = 0;
                for (int i = 0; i < DAT_80612610; i += 1) {
                    *(float *)(DAT_80611d78 + off + 0x100) = ease * pickX + ox;
                    *(float *)(DAT_80611d78 + off + 0x104) = ease * pickY + oy;
                    off += 0x141c;
                }
            }
        } else {
            object_fade_begin(0);
            mem_free_top(*(int *)(outSlot + 4));
        }
        input_reset_pad(0);
        input_reset_pad(1);
    }
    FUN_80382728();
}

// 0x8023411c | worm_event_rebuild_condition_list
// Public wrapper -> worm_event_build_condition_list.
static void worm_event_rebuild_condition_list(void)
{
    worm_event_build_condition_list();
}

// 0x8023413c | worm_event_build_condition_list
// Rebuild the active condition list (DAT_80612910 / count DAT_80612914) for the
// current spawn-flag mask: include each execute record whose spawn-flag word
// (+0x3c) intersects the current mask AND passes the difficulty gate, or which is
// already flagged active (+0x30). Records the mask in DAT_80612918.
// NON-MATCHING: mask arrives via spill register; captured as a local.
static void worm_event_build_condition_list(void)
{
    unsigned int mask = (unsigned int)__save_gpr_28();
    int off = 0;
    DAT_80612914 = 0;
    DAT_80612918 = mask;
    int i = 0;
    while (true) {
        if (DAT_806128f8 <= i) break;
        int *rec = (int *)(DAT_80612924 + off);
        if ((((rec[0xf] & mask) != 0) && (worm_event_check_difficulty_gate(*rec) != 0)) ||
            (rec[0xc] != 0)) {
            *(int **)(DAT_80612910 + DAT_80612914 * 4) = rec;
            DAT_80612914 = DAT_80612914 + 1;
        }
        i += 1;
        off += 0x48;
    }
    __restore_gpr_28();
}

// 0x802341e0 | worm_event_check_difficulty_gate
// True if an event def's difficulty flags (+0xb0 low 3 bits) admit the current
// difficulty (DAT_80501602): no bits = always; bit0 = easy(0); bit1 = normal(1);
// bit2 = hard(2).
static int worm_event_check_difficulty_gate(int eventDef)
{
    unsigned int flags = *(unsigned int *)(eventDef + 0xb0);
    if ((flags & 7) == 0) {
        return 1;
    }
    if (((flags & 1) != 0) && (DAT_80501602 == '\0')) {
        return 1;
    }
    if ((flags & 2) != 0) {
        if (DAT_80501602 == '\x01') {
            return 1;
        }
    }
    if (((flags & 4) != 0) && (DAT_80501602 == '\x02')) {
        return 1;
    }
    return 0;
}

// ============================================================================
// batch_701: event-execute action handlers (alert / camera-move / attached-effect
//            spawn / pending-record queue / carrier flag / arm-by-class)
// 10 fns 0x80234268-0x8023458c | mission-hooks | subsystem:game
// ----------------------------------------------------------------------------
// Individual action handlers invoked by the event-execute dispatch: broadcast
// an AI alert, move a player camera to a zone, spawn a subobject-attached
// effect, push/pop the "pending" event-record queue (DAT_805c7938), toggle a
// carrier object flag, and resolve-record-by-class then arm (optionally fire).
// ============================================================================

// --- batch_701 file-local externs -------------------------------------------
extern void aiactor_alert_zone_broadcast(int a1, double a2, double a3, double a4,
                                         int a5, int a6, int a7, int a8);
extern void subobj_build_orthonormal_matrix(unsigned int subObj, float *basis, float *up);
extern void subobj_apply_world_bounds(unsigned int subObj);
extern void FUN_8025b148(int who, int zone, unsigned int mode, int a4, int a5); // camera-to-zone move
extern int  DAT_805c7938;    // pending event-record queue base (int[])

// worm_event_find_record_by_class / worm_event_arm / worm_event_execute are
// defined/forward-declared earlier in this file and in scope here.
// entity_spawn_by_type, soundpad_find_zone_by_id, FUN_8023c32c, the DAT_* event
// globals and FLOAT_80618744 are declared above.

// 0x80234268 | worm_event_alert_broadcast
// Thin pass-through to the AI alert-zone broadcast (event action type).
static void worm_event_alert_broadcast(int a1, double a2, double a3, double a4,
                                       int a5, int a6, int a7, int a8)
{
    aiactor_alert_zone_broadcast(a1, a2, a3, a4, a5, a6, a7, a8);
}

// 0x80234290 | worm_event_camera_move_p1
// Move player-1's camera to the event's target zone (mode 5 if the event's +0xb0
// has bit 0x800000, else 1), then zero the P1 camera offset triple at +0xe38.
static void worm_event_camera_move_p1(int *record)
{
    unsigned int mode = 1;
    if ((*(unsigned int *)(*record + 0xb0) & 0x800000) != 0) {
        mode = 5;
    }
    int zone = soundpad_find_zone_by_id(*(int *)(*record + 0xc));
    FUN_8025b148(0, zone, mode, 0, 0);
    float z = FLOAT_80618744;
    *(float *)(DAT_80611d78 + 0xe38) = z;
    *(float *)(DAT_80611d78 + 0xe3c) = z;
    *(float *)(DAT_80611d78 + 0xe40) = z;
}

// 0x8023430c | worm_event_camera_move_p2
// Same as _p1 but for player-2 (only in 2-player splitscreen with a live P2
// object); zeroes the P2 camera offset triple at +0x2254.
static void worm_event_camera_move_p2(int *record)
{
    if ((1 < DAT_80612610) && (*(int *)(DAT_80611d78 + 0x1430) != 0)) {
        if (*(int *)(*(int *)(DAT_80611d78 + 0x1430) + 0x7c) != 0) {
            unsigned int mode = 1;
            if ((*(unsigned int *)(*record + 0xb0) & 0x800000) != 0) {
                mode = 5;
            }
            int zone = soundpad_find_zone_by_id(*(int *)(*record + 0xc));
            FUN_8025b148(1, zone, mode, 0, 0);
            float z = FLOAT_80618744;
            *(float *)(DAT_80611d78 + 0x2254) = z;
            *(float *)(DAT_80611d78 + 0x2258) = z;
            *(float *)(DAT_80611d78 + 0x225c) = z;
        }
    }
}

// 0x802343b0 | worm_event_spawn_attached_effect
// For a subobject event (+0x94 == 0x273), spawn effect type 0x2ab at the subobj
// origin. If the spawned effect has no subobj (+0x80 == 0), copy the source's
// material/flags; otherwise build the effect's orthonormal matrix from the
// source basis (+0x274) and apply its world bounds.
// NON-MATCHING: spawn ABI args threaded through spill; source passed as param.
static void worm_event_spawn_attached_effect(double d1, double d2, double d3, double d4,
                                             unsigned long long p5, unsigned long long p6,
                                             unsigned long long p7, unsigned long long p8,
                                             int source)
{
    if (*(short *)(source + 0x94) == 0x273) {
        float pos[3];
        pos[0] = FLOAT_80618744;
        pos[1] = FLOAT_80618744;
        pos[2] = FLOAT_80618744;
        int fx = entity_spawn_by_type(d1, d2, d3, d4, p5, p6, p7, p8, 0x2ab,
                                      (int *)(source + 100), (int *)pos, 0, 0);
        if (*(unsigned int *)(fx + 0x80) == 0) {
            *(unsigned int *)(fx + 0xc0) = *(unsigned int *)(source + 0xc0);
            *(unsigned int *)(fx + 0x70) = *(unsigned int *)(source + 0x70);
            *(unsigned int *)(fx + 200)  = *(unsigned int *)(source + 200);
        }
        else {
            float *up;
            if ((*(unsigned int *)(source + 0x28) & 0x200000) == 0) {
                up = (float *)0x0;
            }
            else {
                up = *(float **)(*(int *)(source + 4) + 0x10);
            }
            subobj_build_orthonormal_matrix(*(unsigned int *)(fx + 0x80),
                                            (float *)(source + 0x274), up);
            subobj_apply_world_bounds(*(unsigned int *)(fx + 0x80));
        }
    }
}

// 0x80234470 | worm_event_register_pending
// Push a record onto the pending event-record queue (DAT_805c7938), mark it
// state 2 (+0x30) and clear its spawn-flag word (+0x3c = -1).
static void worm_event_register_pending(int record)
{
    (&DAT_805c7938)[DAT_80612900] = record;
    DAT_80612900 = DAT_80612900 + 1;
    *(int *)(record + 0x30) = 2;
    *(int *)(record + 0x3c) = 0xffffffff;
}

// 0x802344a0 | worm_event_pop_pending
// Pop the front record off the pending queue (shift the rest down) and return it.
static int worm_event_pop_pending(void)
{
    int front = DAT_805c7938;
    int remaining = DAT_80612900 - 1;
    int off = 0;
    int idx = 0;
    if (0 < remaining) {
        do {
            idx += 1;
            *(int *)((int)&DAT_805c7938 + off) = (&DAT_805c7938)[idx];
            off += 4;
            remaining -= 1;
        } while (remaining != 0);
    }
    DAT_80612900 = DAT_80612900 - 1;
    return front;
}

// 0x802344f0 | worm_event_carrier_set_flag
// Set the "held" flag (+0x1a0 = 1) on the carrier object referenced by the
// event's held-object handle (+0x14, resolved via FUN_8023c32c).
static void worm_event_carrier_set_flag(int *record)
{
    int carrier = FUN_8023c32c(*(unsigned int *)(*record + 0x14));
    if (carrier != 0) {
        *(int *)(carrier + 0x1a0) = 1;
    }
}

// 0x80234528 | worm_event_carrier_clear_flag
// Clear the "held" flag (+0x1a0 = 0) on the event's carrier object.
static void worm_event_carrier_clear_flag(int *record)
{
    int carrier = FUN_8023c32c(*(unsigned int *)(*record + 0x14));
    if (carrier != 0) {
        *(int *)(carrier + 0x1a0) = 0;
    }
}

// 0x80234560 | worm_event_arm_by_class
// Resolve the execute record owning objective-class classId and arm it.
// NON-MATCHING: ROM threads the full spill frame into worm_event_arm; only the
// record is meaningful for the common path, spill args passed as 0.
static void worm_event_arm_by_class(int classId)
{
    int *rec = worm_event_find_record_by_class(classId);
    if (rec != (int *)0x0) {
        worm_event_arm(0, 0, 0, 0, 0, 0, 0, 0, rec, 0, 0, 0, 0, 0, 0, 0);
    }
}

// 0x8023458c | worm_event_arm_and_fire_by_class
// Resolve the record for classId, arm it, clear its +0x30 state, then fire the
// event immediately (worm_event_execute).
static void worm_event_arm_and_fire_by_class(int classId)
{
    int *rec = worm_event_find_record_by_class(classId);
    if (rec != (int *)0x0) {
        worm_event_arm(0, 0, 0, 0, 0, 0, 0, 0, rec, 0, 0, 0, 0, 0, 0, 0);
        rec[0xc] = 0;
        worm_event_execute(rec);
    }
}

// ---------------------------------------------------------------------------
// batch_702 (switch-trigger handlers): three event-execute handlers that toggle
// switch-class objects. Grouped here with the mission-hooks event dispatch;
// the remaining seven functions of batch_702 (the per-player scoring/stat
// accumulator, 0x90c stride table) live in worm_stat_events.cpp.
// ---------------------------------------------------------------------------

// Raw symbols referenced by the switch handlers below.
extern void FUN_8036fcfc(void);   // broadcast: fire all switch-changed listeners
extern int  DAT_8061290c;         // guard flag: 1 while broadcasting switch change

// 0x802345d0 | worm_event_switch_toggle_single
// Resolve the switch object referenced by the event record (rec[0]->+0x14 tag),
// invoke its virtual toggle handler (+0x154)(obj, 0) if present, then notify the
// mission system that a switch changed.
static void worm_event_switch_toggle_single(int *rec)
{
    int obj = FUN_8023c32c(*(unsigned int *)(*rec + 0x14));
    if (obj != 0) {
        void (**vtoggle)(int, int) = (void (**)(int, int))(obj + 0x154);
        if (*vtoggle != (void (*)(int, int))0x0) {
            (*vtoggle)(obj, 0);
        }
        worm_event_notify_switch_changed(obj);
    }
}

// 0x80234628 | worm_event_switch_toggle_group
// Gather up to 0x19 switch objects that share the event record's tag
// (FUN_8023c59c bucket enumerate), toggle each via its virtual handler (+0x154),
// and notify the mission system after every toggle.
static void worm_event_switch_toggle_group(int *rec)
{
    int list[30];
    int count = FUN_8023c59c(*(int *)(*rec + 0x14), (int)list, 0x19);
    int off = 0;
    for (int i = 0; i < count; i = i + 1) {
        void (*toggle)(int, int) =
            (void (*)(int, int)) *(int *)(*(int *)((int)list + off) + 0x154);
        if (toggle != (void (*)(int, int))0x0) {
            toggle(*(int *)((int)list + off), 0);
        }
        worm_event_notify_switch_changed(*(int *)((int)list + off));
        off = off + 4;
    }
}

// 0x802346b0 | worm_event_switch_broadcast
// Resolve the switch object, and if it exists raise the broadcast guard
// (DAT_8061290c=1), fan out the switch-changed broadcast to all listeners
// (FUN_8036fcfc), then lower the guard.
static void worm_event_switch_broadcast(int *rec)
{
    int obj = FUN_8023c32c(*(unsigned int *)(*rec + 0x14));
    if (obj != 0) {
        DAT_8061290c = 1;
        FUN_8036fcfc();
        DAT_8061290c = 0;
    }
}
