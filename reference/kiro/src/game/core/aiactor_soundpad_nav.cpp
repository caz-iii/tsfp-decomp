// aiactor_soundpad_nav.cpp - AI navigation across the "sound pad" zone graph.
//
// TSFP levels carry a graph of audio/nav zones (the "sound pad"): each zone is a
// record in the table at DAT_80611b8c (stride 0x20; +0xc = zone world position,
// +0x8 = zone id, +0x4 = zone flag bits). An AI walking to a heard/known target
// hops zone-to-zone: at each step it enumerates the current zone's neighbours
// (soundpad_query_adjacent_zones_simple), scores them by how well their bearing
// lines up with the desired direction and by occupancy, picks one with a weighted
// random draw, and steers toward it. The AI record is reached via the fp/gpr
// scratch frame (actor at +0x7c); its active target zone is stored at rec+0x9c0
// and the queued turn magnitude at rec+0x9e0.
//
//   * soundpad_zone_occupied_by_grab (0x802a6f80) - does any live grabbed sub-object
//     sit in this trigger's zone (and face the AI)? Used to reject blocked zones.
//   * soundpad_trigger_valid (0x802a6a98) - LOS + distance/angle gate on a trigger
//     zone; if valid, runs the occupancy check. (This is FUN_802a6a98 that the
//     carry batch referenced as subobj_soundpad_trigger_valid.)
//   * soundpad_zone_geometry (0x802a70dc) - fill {facing-flag, planar distance,
//     wrapped bearing, angular weight} for a candidate zone.
//   * soundpad_collect_zones_probe (0x802a6938) - dry enumeration of the current
//     zone's neighbours through the angle window (probe / prime pass).
//   * soundpad_build_zone_candidates (0x802a6c98) - build the scored candidate list
//     (each entry: zone index + turn direction/cost), one/two hops out.
//   * soundpad_should_advance (0x802a7328) - decision predicate: is it worth
//     committing to the picked zone this frame?
//   * soundpad_rng_state_gate (0x802a742c) - small rng gate keyed on a state mask.
//   * soundpad_weighted_pick (0x802a7b04) - weighted-random index into an int table.
//   * soundpad_nav_follow_tick (0x802a754c) - the per-frame follow/approach state
//     machine (rec+0x20) that walks toward rec+0x9c0.
//   * soundpad_select_target_zone (0x802a7c10) - the top-level selector: build a
//     bucket of directional weights from the situation, pick, resolve the chosen
//     zone's trigger, and commit it to rec+0x9c0.
//
// Address range: 0x802a6938 - 0x802a7fa3 (0x802a7fa4 belongs to the next batch)
// Subsystem: game (AI navigation)

#include "common.h"

extern "C" {

extern int  DAT_80611b8c;   // sound-zone record table base (stride 0x20)
extern int  DAT_80611d78;   // player/session record table base (+0x14 = leader rec)
extern int  DAT_806120f8;   // current follow-leader record handle
extern char DAT_805015bd;   // co-op sub-mode selector ('\n' = split-screen co-op)
extern int  DAT_806109c0;   // frame-delta ticks (xor-biased int->float source)
extern int  DAT_8053fb2c;   // per-zone object list base table (indexed by zone kind)
extern int  DAT_805401dc;   // per-zone object list count table
extern int  DAT_804d1cd0;   // anim-clip -> flag-record index table
extern int  DAT_804d34dc;   // anim-clip flag-record base

extern const float  FLOAT_806108ec;  // delta-time scale
extern const float  FLOAT_80619900, FLOAT_80619904;
extern const double DOUBLE_80619918;  // int->float reconstruction bias
extern const double DOUBLE_80619988;  // int->float reconstruction bias (rng)
extern const float  FLOAT_80619920, FLOAT_80619924, FLOAT_80619928, FLOAT_8061992c;
extern const float  FLOAT_80619938, FLOAT_8061993c, FLOAT_80619940, FLOAT_80619944, FLOAT_80619948;
extern const float  FLOAT_8061994c, FLOAT_80619950, FLOAT_80619954, FLOAT_80619958, FLOAT_8061995c;
extern const float  FLOAT_80619960, FLOAT_80619964, FLOAT_8061996c, FLOAT_80619968, FLOAT_80619970;
extern const float  FLOAT_80619974, FLOAT_80619978, FLOAT_8061997c, FLOAT_80619980, FLOAT_80619984;
extern const float  FLOAT_80619988, FLOAT_80619990, FLOAT_80619994, FLOAT_80619998, FLOAT_8061999c;
extern const float  FLOAT_806199a0, FLOAT_806199a4, FLOAT_806199a8;

// frame-enter intrinsics (see aiactor_*.cpp)
extern int  __save_gpr_25(void);
extern void __restore_gpr_25(void);
extern unsigned long long FUN_803826c0(void);
extern unsigned long long FUN_803826d8(void);
extern unsigned long long FUN_803826e4(void);
extern unsigned long long FUN_803826f0(void);
extern void FUN_8038270c(void);
extern void FUN_80382724(void);
extern void FUN_80382730(void);
extern void FUN_8038273c(void);

// soundpad / nav helpers (established signatures)
extern int   soundpad_resolve_entity_to_zone(int entityId);
extern int*  soundpad_resolve_zone_node(int nodeIndex);
extern int   soundpad_zone_node_to_index(int* zoneNode);
extern int   soundpad_get_entity_id_from_zone(int zone);
extern int   soundpad_find_trigger_by_id(int id);
extern int   soundpad_find_zone_by_id(int id);
extern int   soundpad_query_adjacent_zones_simple(int zone, int* outList, int maxCount,
                                                  int mask, int a5);

extern double prop_hinge_wrap_angle_to_range(double baseAngle, float* toPos, float* fromPos);
extern unsigned int rng_counter_tick(void);
extern int    memset_wrapper(int dst, int val, int len);
extern double math_asin(double x);
extern double math_sqrt(double x);

extern int  aiactor_retarget_visible_teammate(void);
extern void aiactor_leader_zone_update(int ctx);
extern int  aiactor_follow_zone_path(void);
extern unsigned long long aiactor_facing_update(void);
extern bool aiactor_zone_facing_test(double tol, int trigger, float* pos);
extern unsigned int subobj_get_flags(unsigned int handle);
extern int  anim_is_clip_active(int ctx, int clipId);
extern void anim_play_scaled(double a1, double a2);
extern void anim_set_playback_rate(double rate, int* animObj, int a3);

} // extern "C"

// forward decls
int  soundpad_zone_occupied_by_grab(void);                     // 0x802a6f80
int  soundpad_trigger_valid(void);                             // 0x802a6a98
void soundpad_zone_geometry(int ctxArg, int* outFlag,
                            float* outDist, float* outAngle, float* outWeight); // 0x802a70dc
void soundpad_collect_zones_probe(void);                       // 0x802a6938
int  soundpad_build_zone_candidates(void);                     // 0x802a6c98
unsigned int soundpad_should_advance(double a1, double a2, double a3, int ctx, int flag); // 0x802a7328
void soundpad_rng_state_gate(void);                            // 0x802a742c
int  soundpad_weighted_pick(int* weights, int lo, int hi);     // 0x802a7b04
void soundpad_nav_follow_tick(int ctx);                        // 0x802a754c
int  soundpad_select_target_zone(double a1, double a2, double a3); // 0x802a7c10

// ---------------------------------------------------------------------------
// helper: post the animation-request flag pair unless suppressed by input mask.
static bool nav_post_request(int rec, int flagValue)
{
    if (*(int*)(rec + 0xaac) == 1 && *(int*)(rec + 0xaa8) == 0) {
        unsigned int m = (DAT_805015bd == '\n') ? *(unsigned int*)(rec + 0x14)
                                                 : *(unsigned int*)(rec + 0x1c);
        if ((m & 1) == 0)
            return false;
    }
    *(int*)(rec + 0xaac) = flagValue;
    *(int*)(rec + 0xaa8) = 0;
    return true;
}

// ---------------------------------------------------------------------------
// 0x802a6f80 - is the trigger's zone occupied by a live, facing grabbed object?
// Returns 0 (the loop `break`s on the first blocker; the ROM discards the value,
// so callers only use the side-effect of running the scan).
int soundpad_zone_occupied_by_grab(void)
{
    int* node = (int*)FUN_803826d8();
    int trigger = soundpad_find_trigger_by_id(*node);
    int zoneIdx = soundpad_zone_node_to_index(node);

    if ((node[1] & 0x40000) != 0) {
        short kind = *(short*)((int)node + 10);
        int count = (&DAT_805401dc)[kind];
        int listBase = (&DAT_8053fb2c)[kind];
        int off = 0;
        for (int i = 0; i < count; i++) {
            int obj = *(int*)(listBase + off);
            if (*(int*)(obj + 0x110) == zoneIdx && *(unsigned int*)(obj + 0x80) != 0 &&
                (subobj_get_flags(*(unsigned int*)(obj + 0x80)) & 0x800100) != 0) {
                float radius = ((node[1] & 0x20000) != 0) ? FLOAT_80619960 : FLOAT_80619938;
                float dx = *(float*)(obj + 100) - *(float*)(trigger + 0xc);
                float dz = *(float*)(obj + 0x6c) - *(float*)(trigger + 0x14);
                if (radius * radius < dx * dx + dz * dz ||
                    !aiactor_zone_facing_test((double)FLOAT_80619964, trigger,
                                              (float*)(obj + 100)))
                    break;
            }
            off += 4;
        }
    }
    FUN_80382724();
    return 0;
}

// ---------------------------------------------------------------------------
// 0x802a6a98 - LOS/distance/angle gate on the AI's active trigger; if it passes,
// run the occupancy check (soundpad_zone_occupied_by_grab). Returns 0.
int soundpad_trigger_valid(void)
{
    unsigned long long frame = FUN_803826f0();
    int* trig = (int*)(unsigned int)(frame >> 0x20);
    int ctx = (int)frame;
    int rec = *(int*)(ctx + 0x7c);

    if (trig != (int*)0 && (trig[1] & 0x800) != 0 && *(int*)(rec + 4) != 0) {
        int zone = soundpad_resolve_entity_to_zone((int)*(short*)(trig + 2));
        if ((*(unsigned int*)(zone + 4) & 0x30000) != 0) {
            zone = soundpad_resolve_entity_to_zone((int)*(short*)(trig + 2));
            bool claimOk = (*(unsigned int*)(zone + 4) & 0x40000000) == 0 ||
                           (*(int**)(rec + 0x9c0) != (int*)0 &&
                            **(int**)(rec + 0x9c0) == *trig);
            if (claimOk) {
                float dx = (float)trig[3] - *(float*)(ctx + 100);
                float dz = (float)trig[5] - *(float*)(ctx + 0x6c);
                double bearing = prop_hinge_wrap_angle_to_range(
                    (double)(float)trig[6], (float*)(trig + 3), (float*)(ctx + 100));
                if (dx * dx + dz * dz <= FLOAT_80619938 ||
                    (double)FLOAT_8061993c <= ABS(bearing)) {
                    int tgt = *(int*)(rec + 4);
                    dx = (float)trig[3] - *(float*)(tgt + 100);
                    dz = (float)trig[5] - *(float*)(tgt + 0x6c);
                    double dist2 = (double)(dx * dx + dz * dz);
                    if ((double)FLOAT_80619940 <= dist2) {
                        double b2 = prop_hinge_wrap_angle_to_range(
                            (double)(float)trig[6], (float*)(trig + 3), (float*)(tgt + 100));
                        float over = (float)(dist2 - (double)FLOAT_80619940);
                        float clamped = over;
                        if (FLOAT_80619948 < over) clamped = FLOAT_80619948;
                        float sel = FLOAT_80619944;
                        if (FLOAT_80619944 <= clamped) {
                            sel = over;
                            if (FLOAT_80619948 < over) sel = FLOAT_80619948;
                        }
                        double gate = (double)((sel / FLOAT_80619948) * FLOAT_80619904 + FLOAT_80619940);
                        if (ABS(b2) <= gate) {
                            soundpad_resolve_entity_to_zone((int)*(short*)(trig + 2));
                            soundpad_zone_occupied_by_grab();
                        }
                    }
                }
            }
        }
    }
    FUN_8038273c();
    return 0;
}

// ---------------------------------------------------------------------------
// 0x802a70dc - fill zone geometry for a candidate: facing flag (dot -> asin <
// threshold), planar distance, wrapped bearing, and angular weight.
void soundpad_zone_geometry(int ctxArg, int* outFlag,
                            float* outDist, float* outAngle, float* outWeight)
{
    (void)ctxArg; (void)outFlag;
    unsigned long long frame = FUN_803826e4();
    int ctx = (int)(unsigned int)(frame >> 0x20);
    int rec = *(int*)(ctx + 0x7c);
    unsigned int* flagOut = (unsigned int*)(unsigned int)frame;

    if (flagOut != (unsigned int*)0) {
        int tgt = *(int*)(rec + 4);
        int tgtRec = *(int*)(tgt + 0x7c);

        float ax = *(float*)(ctx + 100) - *(float*)(tgt + 100);
        float bx = *(float*)(tgtRec + 0xb8c) - *(float*)(tgt + 0x68);
        float bz = *(float*)(tgtRec + 0xb88) - *(float*)(tgt + 100);
        float ay = *(float*)(ctx + 0x68) - *(float*)(tgt + 0x68);
        float bw = *(float*)(tgtRec + 0xb90) - *(float*)(tgt + 0x6c);
        float az = *(float*)(ctx + 0x6c) - *(float*)(tgt + 0x6c);

        float lenB = bw * bw + bz * bz + bx * bx;
        if (FLOAT_80619968 < lenB) {
            float r = 1.0f / (float)math_sqrt((double)lenB);
            r = r * -(FLOAT_8061996c * lenB * r * r - FLOAT_80619960);
            float s = FLOAT_80619970 * r * -(FLOAT_8061996c * lenB * r * r - FLOAT_80619960);
            bz = bz * s;
            bw = bw * s;
        }
        float lenA = az * az + ax * ax + ay * ay;
        if (FLOAT_80619968 < lenA) {
            float r = 1.0f / (float)math_sqrt((double)lenA);
            r = r * -(FLOAT_8061996c * lenA * r * r - FLOAT_80619960);
            float s = FLOAT_80619970 * r * -(FLOAT_8061996c * lenA * r * r - FLOAT_80619960);
            ax = ax * s;
            az = az * s;
        }
        double dot = (double)(bz * ax + bw * az);
        double clampd = dot;
        if ((double)FLOAT_80619970 < dot) clampd = (double)FLOAT_80619970;
        if ((double)FLOAT_80619974 <= clampd) {
            if ((double)FLOAT_80619970 < dot) dot = (double)FLOAT_80619970;
        } else {
            dot = (double)FLOAT_80619974;
        }
        dot = math_asin(dot);
        *flagOut = (unsigned int)(dot < (double)FLOAT_80619978);
    }

    if (outDist != (float*)0) {
        float dx = *(float*)(ctx + 100) - *(float*)(*(int*)(rec + 4) + 100);
        float dz = *(float*)(ctx + 0x6c) - *(float*)(*(int*)(rec + 4) + 0x6c);
        *outDist = (float)math_sqrt((double)(dx * dx + dz * dz));
    }
    if (outAngle != (float*)0) {
        double a = prop_hinge_wrap_angle_to_range(
            (double)*(float*)(*(int*)(rec + 0x9c0) + 0x18),
            (float*)(*(int*)(rec + 0x9c0) + 0xc),
            (float*)(*(int*)(rec + 4) + 100));
        *outAngle = (float)a;
    }
    if (outWeight != (float*)0) {
        float over = *outDist - FLOAT_80619950;
        float clamped = over;
        if (FLOAT_80619954 < over) clamped = FLOAT_80619954;
        float sel = FLOAT_80619944;
        if (FLOAT_80619944 <= clamped) {
            sel = over;
            if (FLOAT_80619954 < over) sel = FLOAT_80619954;
        }
        *outWeight = FLOAT_80619940 + (FLOAT_80619950 * sel) / FLOAT_80619954;
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x802a6938 - probe: enumerate the current zone's neighbours through the angle
// window, resolving each trigger's bearing (priming pass; writes nothing).
void soundpad_collect_zones_probe(void)
{
    int ctx = __save_gpr_25();
    int* zoneNode = (int*)soundpad_resolve_entity_to_zone((int)*(short*)(ctx + 8));
    int idx = soundpad_zone_node_to_index(zoneNode);
    int list[32];
    int count = soundpad_query_adjacent_zones_simple(idx, list, 0x14, 0x1c0020, 0);

    double win = (double)FLOAT_80619904;
    int off = 0;
    for (int i = 0; i < count; i++) {
        int zone = *(int*)((int)list + off);
        double bearing = prop_hinge_wrap_angle_to_range(
            (double)*(float*)(ctx + 0x18), (float*)(ctx + 0xc),
            (float*)(DAT_80611b8c + zone * 0x20 + 0xc));
        if ((bearing <= win || (double)FLOAT_80619920 <= bearing) &&
            (bearing <= (double)FLOAT_80619924 || (double)FLOAT_80619900 <= bearing)) {
            int* node = soundpad_resolve_zone_node(zone);
            if ((node[1] & 0x30000) != 0) {
                int trig = soundpad_get_entity_id_from_zone(zone);
                trig = soundpad_find_trigger_by_id(trig);
                prop_hinge_wrap_angle_to_range(
                    (double)*(float*)(ctx + 0x18), (float*)(ctx + 0xc), (float*)(trig + 0xc));
            }
        }
        off += 4;
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// 0x802a6c98 - build the scored candidate-zone list (out buffer high word of the
// frame). Each entry is {zone index, turn cost}; one direct hop plus two-hop
// expansion for zones that fail the direct angle window. Returns the count.
int soundpad_build_zone_candidates(void)
{
    unsigned long long frame = FUN_803826c0();
    int base = (int)(unsigned int)(frame >> 0x20);   // situation record
    int outBuf = (int)frame;                          // candidate out-array
    int maxCount = 0; // in-r6: capacity; modelled below

    // r6 (capacity) is passed in a register the decompiler names in_r6; there is
    // no recoverable source, so treat "full" as the query cap (0x14 entries).
    maxCount = 0x14;

    int* node = (int*)soundpad_resolve_entity_to_zone((int)*(short*)(base + 8));
    int written = 0;
    int outOff = 0;
    int idx = soundpad_zone_node_to_index(node);
    int list[40];
    int count = soundpad_query_adjacent_zones_simple(idx, list, 0x14, 0x1c0020, 0);
    double win = (double)FLOAT_80619904;

    int off = 0;
    for (int i = 0; i < count; i++) {
        int zone = *(int*)((int)list + off);
        double bearing = prop_hinge_wrap_angle_to_range(
            (double)*(float*)(base + 0x18), (float*)(base + 0xc),
            (float*)(DAT_80611b8c + zone * 0x20 + 0xc));

        if (ABS(bearing) <= win || (double)FLOAT_80619920 <= ABS(bearing)) {
            int* node2 = soundpad_resolve_zone_node(zone);
            if ((node2[1] & 0x30000) != 0) {
                node2 = soundpad_resolve_zone_node(zone);
                if ((node2[1] & 0x40000000) == 0) {
                    int trig = soundpad_get_entity_id_from_zone(zone);
                    trig = soundpad_find_trigger_by_id(trig);
                    double b2 = prop_hinge_wrap_angle_to_range(
                        (double)*(float*)(base + 0x18), (float*)(base + 0xc),
                        (float*)(trig + 0xc));
                    if ((double)FLOAT_80619928 < ABS(b2) && ABS(b2) < (double)FLOAT_8061992c) {
                        *(int*)(outBuf + outOff) = zone;
                        int turn = (b2 <= (double)FLOAT_80619944) ? -0x5a : 0x5a;
                        written++;
                        ((int*)(outBuf + outOff))[1] = turn;
                        outOff += 8;
                        if (written == maxCount) break;
                    }
                }
            }
        } else {
            // two-hop expansion for a zone outside the direct window
            int sub[20];
            int subCount = soundpad_query_adjacent_zones_simple(zone, sub, 0x14, 0x1c0020, 0);
            int outSlot = written << 3;
            int subOff = 0;
            for (int j = 0; j < subCount; j++) {
                int* subNode = soundpad_resolve_zone_node(*(int*)((int)sub + subOff));
                if ((subNode[1] & 0x30000) != 0) {
                    subNode = soundpad_resolve_zone_node(*(int*)((int)sub + subOff));
                    if ((subNode[1] & 0x40000000) == 0) {
                        double sb = prop_hinge_wrap_angle_to_range(
                            (double)*(float*)(base + 0x18),
                            (float*)(DAT_80611b8c + zone * 0x20 + 0xc),
                            (float*)(DAT_80611b8c + *(int*)((int)sub + subOff) * 0x20 + 0xc));
                        if ((double)FLOAT_8061994c < ABS(sb) && ABS(sb) < (double)FLOAT_80619928) {
                            int cost;
                            if ((double)FLOAT_80619950 <= sb) {
                                if ((double)FLOAT_80619954 <= sb) {
                                    if ((double)FLOAT_80619958 <= sb) {
                                        cost = ((double)FLOAT_8061995c <= sb) ? 0x3c : 0x2d;
                                    } else cost = 0x1e;
                                } else cost = 5;
                            } else cost = 1;
                            if (bearing < (double)FLOAT_80619944)
                                cost = -cost;
                            *(int*)(outBuf + outSlot + 4) = cost;
                            written++;
                            outOff += 8;
                            *(int*)(outBuf + outSlot) = *(int*)((int)sub + subOff);
                            outSlot += 8;
                            if (written == maxCount) break;
                        }
                    }
                }
                subOff += 4;
            }
        }
        off += 4;
    }
    FUN_8038270c();
    return written;
}

// ---------------------------------------------------------------------------
// 0x802a7328 - decision predicate: should the AI commit to the picked zone?
unsigned int soundpad_should_advance(double a1, double a2, double a3, int ctx, int flag)
{
    int rec = *(int*)(ctx + 0x7c);
    soundpad_resolve_entity_to_zone((int)*(short*)(*(int*)(rec + 0x9c0) + 8));
    if (soundpad_zone_occupied_by_grab() == 0)
        return 1;

    if (ABS(a1) <= a3) {
        if (a2 < (double)FLOAT_80619950)
            return 1;
    } else {
        int tgtRec = *(int*)(*(int*)(rec + 4) + 0x7c);
        if ((flag != 0 && ABS(a1) < (double)FLOAT_8061997c) ||
            *(float*)(tgtRec + 0xb4c) < FLOAT_80619980 ||
            *(float*)(tgtRec + 0xb44) < FLOAT_8061996c ||
            a2 < (double)FLOAT_80619938)
            return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x802a742c - small rng gate keyed on a nav state mask (rec+0x9dc).
void soundpad_rng_state_gate(void)
{
    unsigned long long frame = FUN_803826f0();
    unsigned int mask = ((int)frame == 0) ? 10u : 2u;
    // the driving angle arrives in f1 (Ghidra extraout_f1); the ROM re-reads the
    // last-computed bearing. Recover it from the situation record it just built.
    int situation = (int)(unsigned int)(frame >> 0x20);
    float bearing = *(float*)(*(int*)(situation + 0x7c) + 0x9d8);
    if ((double)FLOAT_8061997c < ABS((double)bearing) &&
        (*(unsigned int*)(*(int*)(situation + 0x7c) + 0x9dc) & mask) != 0)
        rng_counter_tick();
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x802a7b04 - weighted-random pick: sum weights[lo..hi), draw an rng value, then
// walk the cumulative sum until it crosses the scaled draw. (Return via register;
// the ROM leaves the chosen running index in a scratch register the caller reads.)
int soundpad_weighted_pick(int* weights, int lo, int hi)
{
    int total = 0;
    for (int i = lo; i < hi; i++)
        total += weights[i];

    unsigned int draw = rng_counter_tick();
    int accum = 0;
    int chosen = lo;
    for (int i = lo; i < hi; i++) {
        accum += weights[i];
        int threshold = (int)((float)((double)(long long)(unsigned int)((unsigned int)(total - 1) ^ 0x80000000)
                                       - DOUBLE_80619918)
                              * ((float)((double)(long long)(unsigned int)draw - DOUBLE_80619988) / FLOAT_80619984)
                              + FLOAT_8061996c) + 1;
        if (threshold <= accum) { chosen = i; break; }
    }
    FUN_80382730();
    return chosen;
}

// ---------------------------------------------------------------------------
// 0x802a754c - the per-frame follow/approach state machine toward rec+0x9c0.
void soundpad_nav_follow_tick(int ctx)
{
    int rec = *(int*)(ctx + 0x7c);
    int teammate = aiactor_retarget_visible_teammate();

    // commit pending pose swap
    if (-1 < *(int*)(rec + 0x9d0)) {
        if (*(int*)(rec + 0x9d0) != *(int*)(rec + 0x20)) {
            *(float*)(rec + 0x9d8) = FLOAT_80619944;
            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
            *(int*)(rec + 0x20) = *(int*)(rec + 0x9d0);
        }
        *(int*)(rec + 0x9d0) = -1;
    }

    // integrate follow timer
    if ((*(unsigned int*)(rec + 0x2c) & 0x80) == 0) {
        *(float*)(rec + 0xa90) = FLOAT_80619944;
    } else {
        double denom = (double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000);
        *(float*)(rec + 0xa90) =
            FLOAT_806108ec * (FLOAT_80619970 / (float)((double)denom - DOUBLE_80619918)) +
            *(float*)(rec + 0xa90);
    }

    if ((teammate != 0 || FLOAT_80619990 <= *(float*)(rec + 0xb34)) && teammate != 0)
        *(float*)(rec + 0xb34) = FLOAT_80619944;

    // if leader match fails the trigger check, or there is no active zone, re-plan
    if ((DAT_806120f8 == *(int*)(rec + 8) && soundpad_trigger_valid() == 0) ||
        *(int*)(rec + 0x9c0) == 0) {
        nav_post_request(rec, 4);
        return;
    }
    int zone = *(int*)(rec + 0x9c0);

    int state = *(int*)(rec + 0x20);
    if (state != 1) {
        if (0 < state) return;
        if (state < 0) return;
        // state 0: claim the zone and enter follow (state 1)
        int zi = soundpad_find_zone_by_id((int)*(short*)(zone + 8));
        int zrec = DAT_80611b8c + zi * 0x20;
        *(unsigned int*)(zrec + 4) |= 0x40000000;
        zi = soundpad_find_zone_by_id(**(int**)(rec + 0x9c0));
        if (zi < 0)
            *(int*)(rec + 0x684) = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
        else
            *(int*)(rec + 0x684) = soundpad_find_zone_by_id(**(int**)(rec + 0x9c0));
        *(int*)(rec + 0x9ac) = 0;
        *(int*)(rec + 0x9a8) = 0;
        if (*(int*)(rec + 0x20) != 1) {
            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
            *(float*)(rec + 0x9d8) = FLOAT_80619944;
            *(int*)(rec + 0x20) = 1;
            *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
        }
        aiactor_facing_update();
        return;
    }

    // state 1: approach the active zone
    float dx = *(float*)(ctx + 100) - *(float*)(zone + 0xc);
    float dy = *(float*)(ctx + 0x68) - *(float*)(zone + 0x10);
    float dz = *(float*)(ctx + 0x6c) - *(float*)(zone + 0x14);
    double dist2 = (double)(dz * dz + dx * dx + dy * dy);

    if (anim_is_clip_active(ctx, 0x5f0) == 0 && anim_is_clip_active(ctx, 0x5f1) == 0 &&
        anim_is_clip_active(ctx, 0x5f2) == 0)
        anim_play_scaled((double)FLOAT_80619994, (double)FLOAT_80619970);

    if (anim_is_clip_active(ctx, 0x5f0) == 0) {
        *(int*)(rec + 0x30) = *(int*)(*(int*)(rec + 0x9c0) + 0xc);
        *(int*)(rec + 0x34) = *(int*)(*(int*)(rec + 0x9c0) + 0x10);
        *(int*)(rec + 0x38) = *(int*)(*(int*)(rec + 0x9c0) + 0x14);
        aiactor_leader_zone_update(ctx);
    } else {
        soundpad_find_zone_by_id(**(int**)(rec + 0x9c0));
        aiactor_follow_zone_path();
    }
    aiactor_facing_update();
    *(unsigned int*)(rec + 0x28) |= 2;

    int clipRec = (short)(&DAT_804d1cd0)[*(int*)(*(int*)(ctx + 4) + 0xcc)];
    if ((*(unsigned int*)(&DAT_804d34dc + clipRec * 8) & 0x100) != 0) {
        *(unsigned int*)(rec + 0x28) &= 0xfffbffff;
        if ((((int*)*(int*)(ctx + 4))[0x32] & 1) != 0)
            anim_set_playback_rate((double)FLOAT_80619998, *(int**)(ctx + 4), 2);
    }

    if ((*(unsigned int*)(rec + 0x28) & 1) != 0) {
        unsigned int uHi = (DAT_805015bd == '\n') ? *(unsigned int*)(rec + 0x10)
                                                   : *(unsigned int*)(rec + 0x18);
        unsigned int uLo = (DAT_805015bd == '\n') ? *(unsigned int*)(rec + 0x14)
                                                   : *(unsigned int*)(rec + 0x1c);
        *(unsigned int*)(rec + 0xabc) = uLo;
        *(unsigned int*)(rec + 0xab8) = uHi;
        nav_post_request(rec, 0x20);
        return;
    }

    if ((double)FLOAT_8061999c < dist2)
        return;

    int zi = soundpad_find_zone_by_id((int)*(short*)(*(int*)(rec + 0x9c0) + 8));
    if ((*(unsigned int*)(DAT_80611b8c + zi * 0x20 + 4) & 0x20000) == 0)
        nav_post_request(rec, 0x20000);
    else
        nav_post_request(rec, 0x40000);
    *(unsigned int*)(rec + 0x28) &= 0xfffbffff;
}

// ---------------------------------------------------------------------------
// 0x802a7c10 - top-level zone selector: score directional buckets from the
// current situation, weighted-pick one, resolve its trigger, and commit to
// rec+0x9c0. param_2 = desired bearing, param_3 = bearing tolerance.
int soundpad_select_target_zone(double a1, double param_2, double param_3)
{
    (void)a1;
    unsigned long long frame = FUN_803826d8();
    int rec = *(int*)((int)(unsigned int)(frame >> 0x20) + 0x7c);

    // the driving angle arrives in f1; recover the queued bearing from the record
    double driveAngle = (double)*(float*)(rec + 0x9d8);

    int w[8];
    memset_wrapper((int)w, 1, 0x20);

    if (driveAngle <= (double)FLOAT_80619904) {
        if (driveAngle <= (double)FLOAT_806199a0) {
            if (driveAngle < (double)FLOAT_806199a4) w[4] += -1;
        } else {
            w[4] += 2;
        }
    } else {
        w[4] += 4;
    }
    if (*(int*)(rec + 0x9d4) != 3)
        w[0] += 6;

    if (ABS(param_2) <= param_3) {
        if ((int)frame == 0) {
            w[6] += 3; w[3] += 3; w[7] += 3;
        } else {
            w[4] += -2; w[5] += -2; w[3] += 2; w[7] += 1;
            w[0] += -1; w[6] += 3; w[1] += 2; w[2] += 2;
        }
    } else {
        w[4] += 1; w[6] += -1; w[1] += -1; w[3] += 3; w[7] += 3;
    }

    if ((*(unsigned int*)(rec + 0x9dc) & 6) == 0) {
        w[3] = 0; w[2] = 0;
    }
    if (0 < w[2]) {
        if ((*(unsigned int*)(*(int*)(rec + 0xbf0) + 0x54) & 4) == 0)
            w[2] = 0;
        else if (driveAngle < (double)FLOAT_806199a8)
            w[2] = 0;
    }

    for (int i = 0; i < 8; i++)
        if (w[i] < 0) w[i] = 0;

    int pick = soundpad_weighted_pick(w, 0, 8);
    if (5 < pick) {
        int cand[40];
        int count = soundpad_build_zone_candidates();
        int trig = 0;
        int off = 0;
        for (int i = 0; i < count; i++) {
            int* node = soundpad_resolve_zone_node(*(int*)((int)cand + off));
            trig = soundpad_find_trigger_by_id(*node);
            int turn = *(int*)((int)cand + off + 4);
            if ((pick != 7 || turn == 0x5a || turn == -0x5a) && soundpad_trigger_valid() != 0) {
                double b = prop_hinge_wrap_angle_to_range(
                    (double)*(float*)(trig + 0x18), (float*)(trig + 0xc),
                    (float*)(*(int*)(rec + 4) + 100));
                if (ABS(param_2) <= ABS((double)(float)((double)FLOAT_80619950 + b))) {
                    unsigned int draw = rng_counter_tick();
                    int roll = (int)(FLOAT_80619950 *
                        ((float)((double)(long long)(unsigned int)draw - DOUBLE_80619988) / FLOAT_80619984) +
                        FLOAT_8061996c);
                    if (roll != 0) { off += 8; continue; }
                }
                *(int*)(rec + 0x9e0) = *(int*)((int)cand + off + 4);
                break;
            }
            off += 8;
        }
        if (trig == 0)
            soundpad_weighted_pick(w, 0, 6);
        else
            *(int*)(rec + 0x9c0) = trig;
    }
    FUN_80382724();
    return pick;
}
