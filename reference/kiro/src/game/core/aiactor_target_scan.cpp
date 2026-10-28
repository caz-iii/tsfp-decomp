// Address range: 0x801424b4 - 0x801428ec | batch_405 (+ 0x801428ec batch_406) | 2026-09-05
//
// AI actor enemy target-selection helpers. Each of these builds a candidate
// list of actor pointers into a local scratch array and then delegates to
// aiactor_scan_nearest_visible_enemy (0x80142218, in aiactor_respawn.cpp) to
// pick the nearest visible hostile. They differ only in the filter predicate
// used to decide which active actors are eligible candidates, and in the
// default returned when the caller has no valid target grouping.
//
// Shared data:
//   * DAT_80588610  - active actor pointer list (int per slot).
//   * DAT_80612098  - active actor count (total).
//   * DAT_80612610  - base player count; the first DAT_80612610 slots are the
//                     human/own-team actors, the remainder are bots. Several
//                     variants skip this leading block (bots-only scan).
//   * DAT_80611d78  - primary/leader actor state; (+0x14) is the fallback
//                     "no target" actor pointer some variants return.
//   * DAT_80612f64  - cached target-scan result/handle (leaf accessor).
//
// Per actor (param_1): +0x7c -> render/AI object ("self"). On self:
//   +0x08 team/group id; +0xbf0 -> alert object, whose +0x74 is the desired
//   target zone/team key (negative => no grouping, take default), and whose
//   +0x08 / +0x28 hold group keys compared against candidates' alert object.
// Candidate actor c: +0x08 team id (must differ from self to be an enemy);
//   +0x48 (short) group id; +0xbf0 -> alert object with +0x08 / +0x28 keys.
//
// The local candidate array is 34 ints (0x88 bytes) on the original stack
// frame, matching the decompiler's local_88.

#include "common.h"

// ---- named cross-module helper (self-externed; common.h has no decl) -------
extern "C" {
// Defined in aiactor_respawn.cpp. Consumes a caller-built list of actor
// pointers and returns the chosen actor pointer (or a default) via EAX.
extern u32 aiactor_scan_nearest_visible_enemy(u32 selfArg0, u32 selfArg1, int listBase);
}

// ---- raw symbols (per-file externs, typed to their use here) ---------------
extern int DAT_80588610;   // active actor pointer list (base symbol)
extern int DAT_80612098;   // active actor count
extern int DAT_80612610;   // base player count (own-team block size)
extern int DAT_80611d78;   // leader/primary actor state pointer
extern int DAT_80612f64;   // cached target-scan handle

// Candidate scratch list size (ints) as in the original stack frame.
#define AISCAN_LIST_MAX 34

// ---------------------------------------------------------------------------
// aiactor_target_scan_all (0x801424b4)
// Copy the entire active actor list (DAT_80588610, DAT_80612610 entries) into
// the scratch list unconditionally, then scan for the nearest visible enemy.
void aiactor_target_scan_all(int actor)
{
    int list[AISCAN_LIST_MAX];
    int outIdx;
    int* self;
    int remaining;

    self = *(int**)(actor + 0x7c);
    outIdx = 0;   // count of copied entries
    remaining = DAT_80612610;
    if (remaining > 0)
    {
        int srcOff = 0;
        do
        {
            list[outIdx] = *(int*)((int)&DAT_80588610 + srcOff);
            srcOff += 4;
            ++outIdx;
            --remaining;
        } while (remaining != 0);
    }
    aiactor_scan_nearest_visible_enemy((u32)self, (u32)outIdx, (int)list);
}

// ---------------------------------------------------------------------------
// aiactor_target_scan_same_group_short (0x80142524)
// Bots-only scan (skips the leading DAT_80612610 own-team block). Candidate is
// eligible if its team id (+8) differs from self and its short group id (+0x48)
// equals self's alert-object target key (+0xbf0 +0x74). Returns the leader
// state (DAT_80611d78 +0x14) when self has no valid target key (<0).
u32 aiactor_target_scan_same_group_short(int actor)
{
    int list[AISCAN_LIST_MAX];
    u32 result;
    int count;
    int outOff;
    int* self;
    int selfKey;
    int c;

    self = *(int**)(actor + 0x7c);
    selfKey = *(int*)(*(int*)((int)self + 0xbf0) + 0x74);
    if (selfKey < 0)
        return *(u32*)((int)&DAT_80611d78 + 0x14);

    count = 0;
    outOff = 0;
    {
        int srcOff = DAT_80612610 << 2;
        int remaining = DAT_80612098 - DAT_80612610;
        if (DAT_80612610 < DAT_80612098)
        {
            do
            {
                c = *(int*)((int)&DAT_80588610 + srcOff);
                if ((*(int*)(c + 8) != *(int*)((int)self + 8)) &&
                    ((int)*(short*)(c + 0x48) == selfKey))
                {
                    *(int*)((int)list + outOff) = c;
                    ++count;
                    outOff += 4;
                }
                srcOff += 4;
                --remaining;
            } while (remaining != 0);
        }
    }
    result = aiactor_scan_nearest_visible_enemy((u32)self, (u32)count, (int)list);
    return result;
}

// ---------------------------------------------------------------------------
// aiactor_target_scan_alertkey28 (0x801425dc)
// Bots-only scan. Candidate eligible if team (+8) differs from self and its
// alert-object key (+0xbf0 +0x28) equals self's target key (+0xbf0 +0x74).
// Returns 0 when self has no valid target key.
u32 aiactor_target_scan_alertkey28(int actor)
{
    int list[AISCAN_LIST_MAX];
    u32 result;
    int count;
    int outOff;
    int* self;
    int selfKey;
    int c;

    self = *(int**)(actor + 0x7c);
    selfKey = *(int*)(*(int*)((int)self + 0xbf0) + 0x74);
    if (selfKey < 0)
        return 0;

    count = 0;
    outOff = 0;
    {
        int srcOff = DAT_80612610 << 2;
        int remaining = DAT_80612098 - DAT_80612610;
        if (DAT_80612610 < DAT_80612098)
        {
            do
            {
                c = *(int*)((int)&DAT_80588610 + srcOff);
                if ((*(int*)(c + 8) != *(int*)((int)self + 8)) &&
                    (*(int*)(*(int*)(c + 0xbf0) + 0x28) == selfKey))
                {
                    *(int*)((int)list + outOff) = c;
                    ++count;
                    outOff += 4;
                }
                srcOff += 4;
                --remaining;
            } while (remaining != 0);
        }
    }
    result = aiactor_scan_nearest_visible_enemy((u32)self, (u32)count, (int)list);
    return result;
}

// ---------------------------------------------------------------------------
// aiactor_target_scan_alertkey08_full (0x80142690)
// Full scan (all DAT_80612098 actors). Candidate eligible if team differs from
// self and EITHER it is within the leading own-team block (index < base count)
// OR its alert-object key (+0xbf0 +8) equals self's target key. Returns leader
// state default when self has no valid target key.
u32 aiactor_target_scan_alertkey08_full(int actor)
{
    int list[AISCAN_LIST_MAX];
    u32 result;
    int count;
    int outOff;
    int* self;
    int selfKey;
    int idx;
    int c;

    self = *(int**)(actor + 0x7c);
    selfKey = *(int*)(*(int*)((int)self + 0xbf0) + 0x74);
    if (selfKey < 0)
        return *(u32*)((int)&DAT_80611d78 + 0x14);

    count = 0;
    outOff = 0;
    idx = 0;
    {
        int srcOff = 0;
        int remaining = DAT_80612098;
        if (remaining > 0)
        {
            do
            {
                c = *(int*)((int)&DAT_80588610 + srcOff);
                if ((*(int*)(c + 8) != *(int*)((int)self + 8)) &&
                    ((idx < DAT_80612610) ||
                     (*(int*)(*(int*)(c + 0xbf0) + 8) == selfKey)))
                {
                    *(int*)((int)list + outOff) = c;
                    ++count;
                    outOff += 4;
                }
                ++idx;
                srcOff += 4;
                --remaining;
            } while (remaining != 0);
        }
    }
    result = aiactor_scan_nearest_visible_enemy((u32)self, (u32)count, (int)list);
    return result;
}

// ---------------------------------------------------------------------------
// aiactor_target_scan_alertkey28_full (0x80142760)
// Full scan variant of the above but comparing alert-object key (+0xbf0 +0x28).
u32 aiactor_target_scan_alertkey28_full(int actor)
{
    int list[AISCAN_LIST_MAX];
    u32 result;
    int count;
    int outOff;
    int* self;
    int selfKey;
    int idx;
    int c;

    self = *(int**)(actor + 0x7c);
    selfKey = *(int*)(*(int*)((int)self + 0xbf0) + 0x74);
    if (selfKey < 0)
        return *(u32*)((int)&DAT_80611d78 + 0x14);

    count = 0;
    outOff = 0;
    idx = 0;
    {
        int srcOff = 0;
        int remaining = DAT_80612098;
        if (remaining > 0)
        {
            do
            {
                c = *(int*)((int)&DAT_80588610 + srcOff);
                if ((*(int*)(c + 8) != *(int*)((int)self + 8)) &&
                    ((idx < DAT_80612610) ||
                     (*(int*)(*(int*)(c + 0xbf0) + 0x28) == selfKey)))
                {
                    *(int*)((int)list + outOff) = c;
                    ++count;
                    outOff += 4;
                }
                ++idx;
                srcOff += 4;
                --remaining;
            } while (remaining != 0);
        }
    }
    result = aiactor_scan_nearest_visible_enemy((u32)self, (u32)count, (int)list);
    return result;
}

// ---------------------------------------------------------------------------
// aiactor_target_scan_alertkey08 (0x80142830)
// Bots-only scan. Candidate eligible if team differs and alert-object key
// (+0xbf0 +8) equals self's target key. Returns 0 when self has no valid key.
u32 aiactor_target_scan_alertkey08(int actor)
{
    int list[AISCAN_LIST_MAX];
    u32 result;
    int count;
    int outOff;
    int* self;
    int selfKey;
    int c;

    self = *(int**)(actor + 0x7c);
    selfKey = *(int*)(*(int*)((int)self + 0xbf0) + 0x74);
    if (selfKey < 0)
        return 0;

    count = 0;
    outOff = 0;
    {
        int srcOff = DAT_80612610 << 2;
        int remaining = DAT_80612098 - DAT_80612610;
        if (DAT_80612610 < DAT_80612098)
        {
            do
            {
                c = *(int*)((int)&DAT_80588610 + srcOff);
                if ((*(int*)(c + 8) != *(int*)((int)self + 8)) &&
                    (*(int*)(*(int*)(c + 0xbf0) + 8) == selfKey))
                {
                    *(int*)((int)list + outOff) = c;
                    ++count;
                    outOff += 4;
                }
                srcOff += 4;
                --remaining;
            } while (remaining != 0);
        }
    }
    result = aiactor_scan_nearest_visible_enemy((u32)self, (u32)count, (int)list);
    return result;
}

// ---------------------------------------------------------------------------
// aiactor_target_scan_get_cached (0x801428e4)
// Leaf accessor: return the cached target-scan handle/result (DAT_80612f64).
u32 aiactor_target_scan_get_cached(void)
{
    return (u32)DAT_80612f64;
}

// ---------------------------------------------------------------------------
// aiactor_target_scan_nearest_enemy_dist (0x801428ec)
//
// Compute the squared distance to the nearest visible hostile actor, without
// building a candidate list. Walks the whole active actor list and, for each
// candidate whose relation to self is hostile (aiactor_relation_entry != 0)
// and which is either a mission/objective actor (group id 0x4d on self or
// candidate) or a bot beyond the own-team block (c[2] < DAT_80612610), tracks
// the minimum squared distance in dVar12 (seeded from FLOAT_80615e40).
//
// The whole scan is skipped when a cached scan result already exists
// (aiactor_target_scan_get_cached() != 0). The self actor pointer arrives in
// the packed gpr scratch frame produced by __aiactor_scratch_frame_begin
// (FUN_803826e4); __aiactor_scratch_frame_end (FUN_80382730) releases it.
//
// NOTE: the original leaves the computed minimum in a local that the compiler
// spilled; the visible side effect is the cached-scan gate + the frame
// begin/end pairing. This is a faithful non-matching translation of the loop.
extern "C" {
extern int   __aiactor_scratch_frame_begin(void); // FUN_803826e4
extern void  __aiactor_scratch_frame_end(void);   // FUN_80382730
extern int   aiactor_relation_entry(int selfId, int otherId); // pairwise table -> flag byte addr
}
extern float FLOAT_80615e40;         // initial "no nearest" squared distance
// DAT_80612610 already declared at file scope (own-team block size / bots base).

float aiactor_target_scan_nearest_enemy_dist(void)
{
    int   frame;
    int*  self;
    int   cached;
    double best;
    int   i;
    int   srcOff;

    frame = __aiactor_scratch_frame_begin();
    self  = *(int**)(frame + 0x7c);

    best = (double)FLOAT_80615e40;
    cached = (int)aiactor_target_scan_get_cached();
    if (cached == 0)
    {
        srcOff = 0;
        for (i = 0; i < DAT_80612098; ++i)
        {
            int* c = *(int**)((int)&DAT_80588610 + srcOff);
            if ((c[2] != self[2]) &&
                (*(char*)(aiactor_relation_entry(self[2], c[2])) != '\0') &&
                (((*(short*)((int)self + 0x48) == 0x4d) ||
                  (*(short*)((int)c + 0x48) == 0x4d)) ||
                 ((c != (int*)0) && (c[2] < DAT_80612610))))
            {
                int   selfBody = self[0];
                int   cBody    = c[0];
                float dx = *(float*)(cBody + 100) - *(float*)(selfBody + 100);
                float dy = *(float*)(cBody + 0x68) - *(float*)(selfBody + 0x68);
                float dz = *(float*)(cBody + 0x6c) - *(float*)(selfBody + 0x6c);
                double d2 = (double)(dz * dz + dx * dx + dy * dy);
                if (d2 < best)
                    best = d2;
            }
            srcOff += 4;
        }
    }
    __aiactor_scratch_frame_end();
    return (float)best;
}
