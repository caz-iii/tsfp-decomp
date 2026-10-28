// aiactor_soundpad_patrol.cpp - AI "sound pad" patrol animation state machines.
//
// Builds on aiactor_soundpad_nav.cpp: once an AI is walking the sound-zone graph
// it runs a per-frame patrol tick that turns the abstract "which neighbour zone"
// decision into a concrete walk/turn/idle animation sequence, keyed on the queued
// turn direction rec+0x9e0 (+/-0x5a etc.) and a bank of clip-id constants
// (DAT_804734xx). Two variants exist for the two AI locomotion rigs:
//
//   * aiactor_soundpad_patrol_tick   (0x802a8610) - the 11-state rig.
//   * aiactor_soundpad_patrol_tick2  (0x802aa650) - the 13-state rig (adds the
//     0xb "arrive/aim" and 0xc "exit" states, plus a distinct clip bank).
//
// Both walk rec+0x20 (with pending-swap at +0x9d0/+0x9d4/+0x9d8), integrate the
// patrol timer at +0xb30, and drive prop_hinge_apply_spring / aiactor_turn_toward_angle.
//
// Supporting selectors / predicates:
//   * soundpad_select_patrol_zone (0x802a7fa4) - the 10-bucket zone-weight
//     selector variant (like soundpad_select_target_zone but with a different
//     bucket layout and a clip-availability gate).
//   * soundpad_turn_to_clipset (0x802a8458) - map a queued turn magnitude
//     (+/-{1,5,0x1e,0x2d,0x3c,0x5a}) to a clip-set index and pick the clip id
//     from one of four DAT tables (direct / mirrored, primary / alt).
//   * soundpad_patrol_should_advance (0x802a854c) - "keep walking?" predicate on
//     the patrol timer + queued turn.
//   * soundpad_patrol_gate (0x802a85d4) - small gate on the patrol progress vs
//     the pending threshold (+0xaa0/+0xb30).
//   * soundpad_patrol_anim_kick (0x802ad1ec) / _kick_edge (0x802acf54) /
//     _idle_kick (0x802acfa0) - re-trigger the walk clip depending on the zone
//     flag bits (+0x14) and current sub-state.
//   * tri_normal_and_plane (0x802ad4b8) - cross-product face normal of a triangle
//     plus its plane constant (used by the zone-visibility maths).
//
// Address range: 0x802a7fa4 - 0x802ad51b (non-contiguous around sibling files)
// Subsystem: game (AI navigation / patrol animation)

#include "common.h"

extern "C" {

extern int  DAT_80611b8c;   // sound-zone record table base (stride 0x20)
extern int  DAT_80611d78;   // player/session record table base (+0x14 leader rec)
extern int  DAT_806120f8;   // current follow-leader record handle
extern char DAT_805015bd;   // co-op sub-mode selector
extern int  DAT_806109c0;   // frame-delta ticks (xor-biased)
extern int  DAT_80610ba0;   // AI-type descriptor table (+0x204 index, stride 0x29c)
extern int  DAT_804d1cd0;   // anim-clip -> flag-record index table
extern int  DAT_804d34dc;   // anim-clip flag-record base

// clip-id bank for the patrol rigs (direct/mirrored pairs)
extern int DAT_80473400, DAT_80473404, DAT_80473410, DAT_80473414, DAT_80473420, DAT_80473424;
extern int DAT_80473428, DAT_8047342c, DAT_80473430, DAT_80473434, DAT_80473438, DAT_8047343c;
extern int DAT_80473440, DAT_80473444, DAT_80473448, DAT_8047344c, DAT_80473450, DAT_80473454;
extern int DAT_80473458, DAT_8047345c, DAT_80473460, DAT_80473464, DAT_80473470, DAT_80473474;
extern int DAT_80473478, DAT_8047347c, DAT_80473480, DAT_80473484, DAT_80473488, DAT_8047348c;
extern int DAT_80473490, DAT_80473494, DAT_80473498, DAT_8047349c, DAT_804734a0, DAT_804734a4;
extern int DAT_804734a8, DAT_804734ac, DAT_804734b0, DAT_804734b4, DAT_804734b8, DAT_804734bc;
extern int DAT_804734c0, DAT_804734c4, DAT_804734c8, DAT_804734cc, DAT_804734d0, DAT_804734d4;
extern int DAT_804734d8, DAT_804734dc, DAT_804734e0, DAT_804734e4, DAT_804734e8;
extern int DAT_80473518, DAT_8047351c;

extern const float  FLOAT_806108ec;
extern const double DOUBLE_80619918, DOUBLE_80619988;
extern const float  FLOAT_8061996c, FLOAT_80619938, FLOAT_80619944, FLOAT_80619950, FLOAT_80619960;
extern const float  FLOAT_80619970, FLOAT_80619974, FLOAT_8061997c, FLOAT_80619984, FLOAT_80619990;
extern const float  FLOAT_80619994, FLOAT_80619998, FLOAT_806199a0, FLOAT_806199a4, FLOAT_806199a8;
extern const float  FLOAT_806199ac, FLOAT_806199b0, FLOAT_806199b4, FLOAT_806199b8, FLOAT_806199bc;
extern const float  FLOAT_806199c0, FLOAT_806199c4, FLOAT_806199c8, FLOAT_806199cc, FLOAT_80619904;

// frame intrinsics
extern int  FUN_803826f0(void);   // -> ctx ptr (patrol ticks)
extern unsigned long long FUN_803826d8(void); // -> (ctx, arg) pair (select)
extern void FUN_8038273c(void);
extern void FUN_80382724(void);

// nav helpers from aiactor_soundpad_nav.cpp
extern void soundpad_zone_geometry(int ctx, int* outFlag, float* outDist, float* outAngle, float* outWeight);
extern unsigned int soundpad_should_advance(double a1, double a2, double a3, int ctx, int flag);
extern int  soundpad_collect_zones_probe(void);
extern int  soundpad_build_zone_candidates(void);
extern int  soundpad_weighted_pick(int* weights, int lo, int hi);
extern int  soundpad_rng_state_gate(void);
extern int  soundpad_trigger_valid(void);
extern int  soundpad_select_target_zone(double a1, double a2, double a3);

// soundpad zone table helpers
extern int   soundpad_get_entity_id_from_zone(int zone);
extern int   soundpad_find_trigger_by_id(int id);
extern int   soundpad_find_zone_by_id(int id);
extern int*  soundpad_resolve_zone_node(int nodeIndex);
extern int   soundpad_resolve_entity_to_zone(int entityId);

extern double prop_hinge_wrap_angle_to_range(double baseAngle, float* toPos, float* fromPos);
extern void   prop_hinge_apply_spring(double stiffness, int ctx, int zone);
extern void   prop_hinge_advance_auto(void);                        // 0x802a65b4
extern void   prop_hinge_advance_driven(double pushAngle, double a2); // 0x802a6440

extern int  aiactor_retarget_visible_teammate(void);
extern void aiactor_leader_zone_update(int ctx);
extern unsigned long long aiactor_facing_update(void);
extern void aiactor_turn_toward_angle(double angle, int ctx, int mode);
extern void aiactor_alert_broadcast_group(int a1, int ctx, int zone);
extern int* aiactor_resolve_zone_or_node(int rec);
extern void aiactor_aim_at_target(int ctx, int a2, int a3);

extern int  anim_is_clip_active(int ctx, int clipId);
extern void anim_play_scaled(double a1, double a2);
extern void anim_set_playback_rate(double rate, int* animObj, int a3);
extern int  anim_active_slot_userflags(int animObj);
extern int  anim_primary_clip_userflags(int ctx, int clipId);
extern int  anim_event_scan_active(void);

extern unsigned int rng_counter_tick(void);
extern int  memset_wrapper(int dst, int val, int len);
extern void FUN_802b6124(void);   // co-op grip release helper

} // extern "C"

// forward decls
void tri_normal_and_plane(float* p0, float* p1, float* p2, float* outNormal, float* outPlane); // 0x802ad4b8
int  soundpad_turn_to_clipset(int turn, int useAlt);          // 0x802a8458
unsigned int soundpad_patrol_should_advance(double a1, int ctx, int flag); // 0x802a854c
unsigned int soundpad_patrol_gate(double a1, int ctx, int flag);           // 0x802a85d4
int  soundpad_patrol_anim_kick(int ctx, int a2);              // 0x802ad1ec
int  soundpad_patrol_kick_edge(int ctx, int state, int zone); // 0x802acf54
int  soundpad_patrol_idle_kick(int ctx);                      // 0x802acfa0
int  soundpad_select_patrol_zone(double a1, double a2, double a3); // 0x802a7fa4
void aiactor_soundpad_patrol_tick(void);                      // 0x802a8610
void aiactor_soundpad_patrol_tick2(void);                     // 0x802aa650

// helper: request a patrol state transition (records prev + timestamp).
static void patrol_request_state(int rec, int newState)
{
    if (*(int*)(rec + 0x20) != newState) {
        *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
        *(float*)(rec + 0x9d8) = FLOAT_80619944;
        *(int*)(rec + 0x20) = newState;
        *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
    }
}

// helper: post the animation-request flag pair unless suppressed by input mask.
static bool patrol_post_request(int rec, int flagValue)
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

// helper: select a clip id from the +0x9e0 direction (mirror) pair.
static int patrol_clip(int rec, int direct, int mirrored)
{
    return (*(int*)(rec + 0x9e0) == 1) ? mirrored : direct;
}

// helper: the walk-clip "kick" tail shared by several states: turn/spring/rate.
static void patrol_drive_common(int ctx, int rec)
{
    prop_hinge_apply_spring((double)FLOAT_806199b8, ctx, *(int*)(rec + 0x9c0));
    aiactor_turn_toward_angle((double)*(float*)(*(int*)(rec + 0x9c0) + 0x18), ctx, 0);
    *(unsigned int*)(rec + 0x28) &= 0xfffdffff;
    *(unsigned int*)(rec + 0x28) &= 0xfffbffff;
    anim_set_playback_rate((double)FLOAT_80619998, *(int**)(ctx + 4), 1);
}

// ---------------------------------------------------------------------------
// 0x802ad4b8 - triangle face normal (cross product of two edges) + plane const.
void tri_normal_and_plane(float* p0, float* p1, float* p2, float* outNormal, float* outPlane)
{
    float e1y = p2[1] - p0[1];
    float e0z = p1[2] - p0[2];
    float e1z = p2[2] - p0[2];
    float e0y = p1[1] - p0[1];
    float e0x = p1[0] - p0[0];
    float e1x = p2[0] - p0[0];
    outNormal[0] = e0y * e1z - e0z * e1y;
    outNormal[1] = e0z * e1x - e0x * e1z;
    outNormal[2] = e0x * e1y - e0y * e1x;
    *outPlane = outNormal[2] * p0[2] + outNormal[0] * p0[0] + outNormal[1] * p0[1];
}

// ---------------------------------------------------------------------------
// 0x802a8458 - map a queued turn magnitude to a clip-set index, then pick the
// clip id from one of four DAT tables (primary/alt x direct/mirror).
int soundpad_turn_to_clipset(int turn, int useAlt)
{
    int mag = (turn < 0) ? -turn : turn;
    int set = 6;
    if (mag == 0x2d)       set = 2;
    else if (mag < 0x2d) {
        if (mag == 5)      set = 4;
        else if (mag < 5) { if (mag == 1) set = 5; }
        else if (mag == 0x1e) set = 3;
    }
    else if (mag == 0x5a)  set = 0;
    else if (mag < 0x5a && mag == 0x3c) set = 1;

    if (useAlt == 0) {
        if (turn < 1)  return (&DAT_8047351c)[set * 2];
        return (&DAT_80473518)[set * 2];
    }
    if (turn < 1)      return *(int*)(set * 8 + -0x7fb8cb14); // &DAT_804734ec table (mirror alt)
    return (&DAT_804734e8)[set * 2];
}

// ---------------------------------------------------------------------------
// 0x802a854c - "keep walking?" predicate: true if the patrol timer is past the
// commit point, or the queued-turn window says commit.
unsigned int soundpad_patrol_should_advance(double a1, int ctx, int flag)
{
    int rec = *(int*)(ctx + 0x7c);
    float timer = *(float*)(rec + 0xb30);
    if (FLOAT_806199ac < timer)
        return 1;
    if ((double)FLOAT_8061997c <= ABS(a1))
        return 0;
    if (flag != 0 && FLOAT_80619950 < timer)
        return 1;
    int dir = *(int*)(rec + 0x9e0);
    if (dir == 1 && a1 < (double)FLOAT_80619974)
        return 1;
    if (dir != -1)
        return 0;
    if (a1 <= (double)FLOAT_80619970)
        return 0;
    return 1;
}

// ---------------------------------------------------------------------------
// 0x802a85d4 - gate on patrol progress vs the pending threshold.
unsigned int soundpad_patrol_gate(double a1, int ctx, int flag)
{
    if (flag == 0)
        return 0;
    if ((double)FLOAT_8061997c <= ABS(a1))
        return 0;
    int rec = *(int*)(ctx + 0x7c);
    if (*(float*)(rec + 0xb30) < *(float*)(rec + 0xaa0))
        return 0;
    return 1;
}

// ---------------------------------------------------------------------------
// 0x802ad1ec - re-trigger the walk clip depending on the zone flag bits (+0x14)
// and the current (pending-resolved) sub-state. Returns 1 if it kicked a clip.
int soundpad_patrol_anim_kick(int ctx, int state)
{
    int rec = *(int*)(ctx + 0x7c);
    unsigned int zflags = *(unsigned int*)(rec + 0x14);
    if ((zflags & 0x10000) != 0) {
        anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970);
        return 1;
    }
    if ((zflags & 0x20000) == 0) {
        if ((zflags & 0x40000) != 0) {
            int s = *(int*)(rec + 0x20);
            if (s == 0xc) s = *(int*)(rec + 0x9d4);
            if (s != 10) {
                if (s < 10) {
                    if (s < 6) { if (3 < s) { anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970); return 1; } }
                    else if (7 < s) { anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970); return 1; }
                } else if (s < 0xd) {
                    anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970);
                    return 1;
                }
            }
        }
    } else {
        int s = *(int*)(rec + 0x20);
        if (s == 10) s = *(int*)(rec + 0x9d4);
        if (s < 9) {
            if (6 < s) { anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970); return 1; }
        } else if (s < 0xb) {
            if (*(float*)(rec + 0xb30) <= FLOAT_806199cc)
                anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970);
            else
                anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970);
            return 1;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x802acf54 - edge kick: only in state 4, delegate to the anim kick using the
// zone's +0x48 field. Returns -1 if it kicked.
int soundpad_patrol_kick_edge(int ctx, int state, int zone)
{
    if (state == 4 && soundpad_patrol_anim_kick(ctx, *(int*)(zone + 0x48)) != 0)
        return -1;
    return 0;
}

// ---------------------------------------------------------------------------
// 0x802acfa0 - idle kick: like the anim kick but gated on the "not yet past the
// timer" case, returning 0 when still ramping.
int soundpad_patrol_idle_kick(int ctx)
{
    int rec = *(int*)(ctx + 0x7c);
    unsigned int zflags = *(unsigned int*)(rec + 0x14);
    if ((zflags & 0x20000) == 0) {
        if ((zflags & 0x40000) != 0) {
            int s = *(int*)(rec + 0x20);
            if (s == 0xc) s = *(int*)(rec + 0x9d4);
            if (s != 10) {
                if (s < 10) {
                    if (s < 6) { if (3 < s) { anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970); return 1; } }
                    else if (7 < s) { anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970); return 1; }
                } else if (s < 0xd) {
                    if (FLOAT_806199cc < *(float*)(rec + 0xb30)) return 0;
                    anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970);
                    return 1;
                }
            }
        }
    } else {
        int s = *(int*)(rec + 0x20);
        if (s == 10) s = *(int*)(rec + 0x9d4);
        if (s < 9) {
            if (6 < s) { anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970); return 1; }
        } else if (s < 0xb) {
            if (FLOAT_806199cc < *(float*)(rec + 0xb30)) return 0;
            anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970);
            return 1;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x802a7fa4 - patrol zone selector (10 weight buckets). Scores buckets from the
// driving angle, clip availability, target alignment, and the zone flag word,
// weighted-picks, and (for pick>=8) resolves a candidate zone's trigger and
// commits it to rec+0x9c0. a2 = desired bearing, a3 = tolerance.
int soundpad_select_patrol_zone(double a1, double a2, double a3)
{
    (void)a1;
    unsigned long long frame = FUN_803826d8();
    int ctx = (int)(unsigned int)(frame >> 0x20);
    int rec = *(int*)(ctx + 0x7c);
    double drive = (double)*(float*)(rec + 0x9d8);

    // is a "handled" walk clip currently active? (clip availability gate)
    bool clipActive = false;
    int c = patrol_clip(rec, DAT_804734ac, DAT_804734a8);
    if (anim_is_clip_active(ctx, c) != 0) {
        clipActive = true;
    } else {
        c = patrol_clip(rec, DAT_80473404, DAT_80473400);
        if (anim_is_clip_active(ctx, c) != 0 || anim_is_clip_active(ctx, 0x592) != 0)
            clipActive = true;
    }

    int w[10];
    memset_wrapper((int)w, 1, 0x28);

    if (drive <= (double)FLOAT_80619904) {
        if (drive <= (double)FLOAT_806199a0) { if (drive < (double)FLOAT_806199a4) w[8] += -1; }
        else w[8] += 2;
    } else w[8] += 4;

    if (*(int*)(rec + 0x9d4) != 3)
        w[0] += 6;

    if (ABS(a2) <= a3) {
        if ((int)frame == 0) {
            w[7] += 1; w[1] += 2; w[4] += 1; w[5] += 2; w[3] += 1;
        } else {
            w[7] += 1; w[8] += -2; w[4] += 1; w[9] += -2; w[1] += 4;
            w[2] += 1; w[5] += 4; w[6] += 1; w[3] += 2; w[0] += 1;
        }
    } else {
        w[3] += 4; w[8] += 1; w[1] += -5; w[6] = 0; w[2] = 0; w[5] += -5;
    }

    if (clipActive) {
        w[1] = 0; w[2] = 0; w[3] = 0; w[4] = 0; w[7] += 1; w[8] = 0; w[9] = 0;
    } else {
        w[5] = 0; w[4] += 1; w[6] = 0; w[7] = 0;
    }

    if (0 < w[6] || 0 < w[2]) {
        if ((*(unsigned int*)(*(int*)(rec + 0xbf0) + 0x54) & 4) == 0) { w[6] = 0; w[2] = 0; }
        else if (drive < (double)FLOAT_806199a8) { w[6] = 0; w[2] = 0; }
    }
    if ((*(unsigned int*)(rec + 0x9dc) & 6) == 0) { w[4] = 0; w[5] = 0; w[6] = 0; }

    for (int i = 0; i < 10; i++) if (w[i] < 0) w[i] = 0;

    int pick = soundpad_weighted_pick(w, 0, 10);
    if (7 < pick) {
        int cand[40];
        int count = soundpad_build_zone_candidates();
        int trig = 0;
        int off = 0;
        for (int i = 0; i < count; i++) {
            int* node = soundpad_resolve_zone_node(*(int*)((int)cand + off));
            trig = soundpad_find_trigger_by_id(*node);
            int turn = *(int*)((int)cand + off + 4);
            if ((pick != 9 || turn == 0x5a || turn == -0x5a) && soundpad_trigger_valid() != 0) {
                double b = prop_hinge_wrap_angle_to_range(
                    (double)*(float*)(trig + 0x18), (float*)(trig + 0xc),
                    (float*)(*(int*)(rec + 4) + 100));
                if (ABS(a2) <= ABS((double)(float)((double)FLOAT_80619950 + b))) {
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
            soundpad_weighted_pick(w, 0, 8);
        else
            *(int*)(rec + 0x9c0) = trig;
    }
    FUN_80382724();
    return pick;
}

// prologue shared by both patrol ticks: commit pending swap + integrate timer +
// leader-zone update + (if following) advance-check into the exit state.
static int patrol_prologue(int ctx, int* outTeammate, int exitState)
{
    int rec = *(int*)(ctx + 0x7c);
    int teammate = aiactor_retarget_visible_teammate();
    *outTeammate = teammate;

    if (-1 < *(int*)(rec + 0x9d0)) {
        if (*(int*)(rec + 0x9d0) != *(int*)(rec + 0x20)) {
            *(float*)(rec + 0x9d8) = FLOAT_80619944;
            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
            *(int*)(rec + 0x20) = *(int*)(rec + 0x9d0);
        }
        *(int*)(rec + 0x9d0) = -1;
    }
    if ((*(unsigned int*)(rec + 0x2c) & 0x80) == 0) {
        *(float*)(rec + 0xa90) = FLOAT_80619944;
    } else {
        double denom = (double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000);
        *(float*)(rec + 0xa90) =
            FLOAT_806108ec * (FLOAT_80619970 / (float)((double)denom - DOUBLE_80619918)) +
            *(float*)(rec + 0xa90);
    }
    return rec;
}

// ---------------------------------------------------------------------------
// 0x802a8610 - the 11-state patrol animation tick.
void aiactor_soundpad_patrol_tick(void)
{
    int ctx = FUN_803826f0();
    int teammate;
    int rec = patrol_prologue(ctx, &teammate, 10);

    int flagOut = 0; float dist = FLOAT_80619944; float angle = FLOAT_80619944; float weight = FLOAT_80619944;
    aiactor_leader_zone_update(ctx);

    if (*(int*)(rec + 8) == DAT_806120f8 && *(int*)(rec + 0x9c0) != 0) {
        if (teammate != 0)
            aiactor_alert_broadcast_group(0, ctx, *(int*)(*(int*)(*(int*)(rec + 4) + 0x7c) + 0x678));
        if (*(int*)(rec + 8) == DAT_806120f8) {
            int (*fn)(int) = *(int(**)(int))(*(int*)(rec + 0xbf0) + 0x78);
            if (fn != 0) { int t = fn(ctx); if (t != 0) *(int*)(rec + 4) = t; }
        }
        if (*(int*)(rec + 4) == 0) {
            patrol_post_request(rec, 0x200);
            FUN_8038273c();
            return;
        }
        soundpad_zone_geometry(ctx, &flagOut, &dist, &angle, &weight);
        if (soundpad_should_advance((double)angle, (double)dist, (double)weight, ctx, teammate) != 0 &&
            *(int*)(rec + 0x20) != 10)
            patrol_request_state(rec, 10);
    }

    int state = *(int*)(rec + 0x20);
    switch (state) {
    case 0: {
        unsigned int zf = *(unsigned int*)(*(int*)(rec + 0xbf0) + 4);
        if ((zf & 0x20000) != 0 && *(int*)(rec + 0x9c0) == 0) {
            if ((zf & 0x800000) == 0) {
                int* zn = aiactor_resolve_zone_or_node(rec);
                *(int**)(rec + 0x9c0) = zn;
                if ((*(unsigned int*)(*(int*)(rec + 0x9c0) + 4) & 0x800) == 0) {
                    int t = soundpad_find_trigger_by_id(soundpad_get_entity_id_from_zone(*(int*)(rec + 0x678)));
                    *(int*)(rec + 0x9c0) = t;
                }
            } else {
                int t = soundpad_find_trigger_by_id(soundpad_get_entity_id_from_zone(*(int*)(rec + 0x678)));
                *(int*)(rec + 0x9c0) = t;
            }
            int zi = soundpad_find_zone_by_id((int)*(short*)(*(int*)(rec + 0x9c0) + 8));
            *(unsigned int*)(DAT_80611b8c + zi * 0x20 + 4) |= 0x40000000;
            *(int*)(rec + 0x30) = *(int*)(*(int*)(rec + 0x9c0) + 0xc);
            *(int*)(rec + 0x34) = *(int*)(*(int*)(rec + 0x9c0) + 0x10);
            *(int*)(rec + 0x38) = *(int*)(*(int*)(rec + 0x9c0) + 0x14);
        }
        *(int*)(rec + 0x9dc) = soundpad_collect_zones_probe();
        *(int*)(rec + 0x9c4) = *(int*)(rec + 0x9c0);
        if (anim_is_clip_active(ctx, 0x5a3) == 0)
            anim_play_scaled((double)FLOAT_80619994, (double)FLOAT_80619970);
        *(int*)(rec + 0x30) = *(int*)(*(int*)(rec + 0x9c0) + 0xc);
        *(int*)(rec + 0x34) = *(int*)(*(int*)(rec + 0x9c0) + 0x10);
        *(int*)(rec + 0x38) = *(int*)(*(int*)(rec + 0x9c0) + 0x14);
        *(unsigned int*)(rec + 0x28) |= 0x20000;
        patrol_request_state(rec, 1);
        aiactor_facing_update();
        break;
    }
    case 1: {
        int zone = *(int*)(rec + 0x9c0);
        float dx = *(float*)(ctx + 100) - *(float*)(zone + 0xc);
        float dz = *(float*)(ctx + 0x6c) - *(float*)(zone + 0x14);
        if (FLOAT_806199b0 < dx * dx + dz * dz)
            prop_hinge_apply_spring((double)FLOAT_806199b4, ctx, zone);
        else
            patrol_request_state(rec, 2);
        aiactor_turn_toward_angle((double)*(float*)(*(int*)(rec + 0x9c0) + 0x18), ctx, 0);
        break;
    }
    case 2:
        if (anim_active_slot_userflags(*(int*)(ctx + 4)) != 0) {
            if (*(int*)(rec + 8) != DAT_806120f8)
                soundpad_zone_geometry(ctx, &flagOut, &dist, &angle, &weight);
            int sel = soundpad_select_target_zone((double)dist, (double)angle, (double)weight);
            if (5 < sel) {
                int zi = soundpad_find_zone_by_id((int)*(short*)(*(int*)(rec + 0x9c0) + 8));
                if (zi < 0)
                    *(int*)(rec + 0x684) = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                else
                    *(int*)(rec + 0x684) = zi;
                *(int*)(rec + 0x9ac) = 0;
                *(int*)(rec + 0x9a8) = 0;
                int* zn = soundpad_resolve_zone_node(zi);
                zn[1] |= 0x40000000;
            }
            unsigned int draw = rng_counter_tick();
            *(float*)(rec + 0xb30) = FLOAT_8061996c *
                ((float)((double)(long long)(unsigned int)draw - DOUBLE_80619988) / FLOAT_80619984);
            static const int nextForSel[8] = { 3, 4, 6, 5, 7, 8, 9, 9 };
            if (sel <= 7) {
                anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970);
                if (sel == 3) { *(unsigned int*)(rec + 0x2c) &= 0xfffdffff; *(float*)(rec + 0xb30) = FLOAT_80619944; }
                if (sel == 4 || sel == 5) { *(int*)(rec + 0x9e0) = soundpad_rng_state_gate(); *(float*)(rec + 0xb34) = FLOAT_80619944; }
                patrol_request_state(rec, nextForSel[sel]);
            } else {
                patrol_post_request(rec, 4);
            }
        }
        prop_hinge_apply_spring((double)FLOAT_806199b8, ctx, *(int*)(rec + 0x9c0));
        aiactor_turn_toward_angle((double)*(float*)(*(int*)(rec + 0x9c0) + 0x18), ctx, 0);
        *(unsigned int*)(rec + 0x28) &= 0xfffdffff;
        *(unsigned int*)(rec + 0x28) &= 0xfffbffff;
        anim_set_playback_rate((double)FLOAT_80619998, *(int**)(ctx + 4), 1);
        break;
    case 3:
        if (FLOAT_80619960 <= *(float*)(rec + 0xb30))
            patrol_request_state(rec, 2);
        if (anim_is_clip_active(ctx, 0x5a3) == 0)
            anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970);
        patrol_drive_common(ctx, rec);
        break;
    case 4:
        if (anim_is_clip_active(ctx, 0x5aa) == 0 && anim_is_clip_active(ctx, 0x5a9) == 0)
            anim_play_scaled((double)FLOAT_80619994, (double)FLOAT_80619970);
        else if (anim_primary_clip_userflags(ctx, 0x5aa) != 0)
            anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970);
        if (FLOAT_806199ac < *(float*)(rec + 0xb30)) {
            anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970);
            patrol_request_state(rec, 2);
        }
        patrol_drive_common(ctx, rec);
        break;
    case 5:
        if (anim_active_slot_userflags(*(int*)(ctx + 4)) != 0 && anim_is_clip_active(ctx, 0x5ac) == 0)
            anim_play_scaled((double)FLOAT_80619994, (double)FLOAT_80619970);
        if (FLOAT_806199bc < *(float*)(rec + 0xb30) ||
            ((*(unsigned int*)(rec + 0x2c) & 0x20000) != 0 && FLOAT_80619970 < *(float*)(rec + 0xb30))) {
            anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970);
            *(unsigned int*)(rec + 0x2c) &= 0xfffdffff;
            patrol_request_state(rec, 2);
        }
        patrol_drive_common(ctx, rec);
        break;
    case 6:
        if (anim_is_clip_active(ctx, 0x5a4) == 0 && anim_is_clip_active(ctx, 0x5a5) == 0 &&
            anim_is_clip_active(ctx, 0x5a6) == 0) {
            anim_play_scaled((double)FLOAT_80619994, (double)FLOAT_80619970);
        } else if (anim_primary_clip_userflags(ctx, 0x5a4) != 0) {
            anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970);
        } else if (anim_primary_clip_userflags(ctx, 0x5a5) != 0 || anim_primary_clip_userflags(ctx, 0x5a6) != 0) {
            *(unsigned int*)(rec + 0x2c) &= 0xfffffbff;
            anim_play_scaled((double)FLOAT_80619944, (double)FLOAT_80619970);
            patrol_request_state(rec, 2);
        }
        if (anim_is_clip_active(ctx, 0x5a5) != 0 || anim_is_clip_active(ctx, 0x5a6) != 0) {
            *(float*)(rec + 0xa90) = FLOAT_80619944;
            *(unsigned int*)(rec + 0x2c) |= 0x400;
        }
        patrol_drive_common(ctx, rec);
        break;
    case 7: {
        // walk-with-turn: kick the turn clip-set, then advance the hinge.
        int cs = patrol_clip(rec, DAT_80473414, DAT_80473410);
        if (anim_is_clip_active(ctx, cs) == 0) {
            int c2 = patrol_clip(rec, DAT_80473434, DAT_80473430);
            if (anim_is_clip_active(ctx, c2) == 0) {
                int c3 = patrol_clip(rec, DAT_8047343c, DAT_80473438);
                if (anim_is_clip_active(ctx, c3) == 0) {
                    int c4 = patrol_clip(rec, DAT_80473444, DAT_80473440);
                    if (anim_is_clip_active(ctx, c4) == 0) {
                        int c5 = patrol_clip(rec, DAT_8047344c, DAT_80473448);
                        if (anim_is_clip_active(ctx, c5) == 0) {
                            int c6 = patrol_clip(rec, DAT_80473454, DAT_80473450);
                            if (anim_is_clip_active(ctx, c6) == 0)
                                anim_play_scaled((double)FLOAT_80619994, (double)FLOAT_80619970);
                        }
                    }
                }
            }
        }
        // (userflag-driven transition to state 2 handled by the shared kick; the
        // detailed ROM ladder is condensed - non-matching.)
        int cend = patrol_clip(rec, DAT_80473434, DAT_80473430);
        if (anim_is_clip_active(ctx, cend) != 0)
            prop_hinge_advance_driven((double)FLOAT_806199c0, (double)FLOAT_80619944);
        else {
            int calt = patrol_clip(rec, DAT_80473454, DAT_80473450);
            if (anim_is_clip_active(ctx, calt) != 0)
                prop_hinge_advance_auto();
            else
                aiactor_turn_toward_angle((double)*(float*)(*(int*)(rec + 0x9c0) + 0x18), ctx, 0);
        }
        break;
    }
    case 8: {
        int cs = patrol_clip(rec, DAT_80473414, DAT_80473410);
        if (anim_is_clip_active(ctx, cs) == 0) {
            int c2 = patrol_clip(rec, DAT_80473424, DAT_80473420);
            if (anim_is_clip_active(ctx, c2) == 0) {
                int c3 = patrol_clip(rec, DAT_8047345c, DAT_80473458);
                if (anim_is_clip_active(ctx, c3) == 0) {
                    int c4 = patrol_clip(rec, DAT_8047342c, DAT_80473428);
                    if (anim_is_clip_active(ctx, c4) == 0)
                        anim_play_scaled((double)FLOAT_80619994, (double)FLOAT_80619970);
                }
            }
        }
        int cq = patrol_clip(rec, DAT_8047342c, DAT_80473428);
        if (anim_is_clip_active(ctx, cq) != 0)
            prop_hinge_advance_driven((double)FLOAT_80619944, (double)FLOAT_80619944);
        else {
            int cs2 = patrol_clip(rec, DAT_8047345c, DAT_80473458);
            if (anim_is_clip_active(ctx, cs2) != 0)
                prop_hinge_advance_auto();
            else
                aiactor_turn_toward_angle((double)*(float*)(*(int*)(rec + 0x9c0) + 0x18), ctx, 0);
        }
        break;
    }
    case 9: {
        if (*(int*)(rec + 8) != DAT_806120f8)
            soundpad_zone_geometry(ctx, 0, (float*)0, &angle, (float*)0);
        int uf = anim_active_slot_userflags(*(int*)(ctx + 4));
        int zone = *(int*)(rec + 0x9c0);
        float dx = *(float*)(ctx + 100) - *(float*)(zone + 0xc);
        float dy = *(float*)(ctx + 0x68) - *(float*)(zone + 0x10);
        float dz = *(float*)(ctx + 0x6c) - *(float*)(zone + 0x14);
        if (uf != 0 || dz * dz + dx * dx + dy * dy <= FLOAT_806199c4) {
            int dir = *(int*)(rec + 0x9e0);
            if (dir == 0x5a) {
                anim_play_scaled((anim_is_clip_active(ctx, 0x5c0) == 0) ? (double)FLOAT_80619994 : (double)FLOAT_80619944, (double)FLOAT_80619970);
            } else if (dir == -0x5a) {
                anim_play_scaled((anim_is_clip_active(ctx, 0x5b3) == 0) ? (double)FLOAT_80619994 : (double)FLOAT_80619944, (double)FLOAT_80619970);
            } else {
                if (anim_is_clip_active(ctx, 0x5f0) == 0)
                    anim_play_scaled((double)FLOAT_80619994, (double)FLOAT_80619970);
                aiactor_turn_toward_angle((double)*(float*)(*(int*)(rec + 0x9c0) + 0x18), ctx, 0);
            }
            int z = soundpad_resolve_entity_to_zone((int)*(short*)(*(int*)(rec + 0x9c4) + 8));
            *(unsigned int*)(z + 4) &= 0xbfffffff;
            patrol_post_request(rec, 0x10000);
        }
        if (*(float*)(rec + 0x9d8) <= FLOAT_8061996c || FLOAT_806199c8 <= ABS(angle)) {
            *(unsigned int*)(rec + 0x28) &= 0xfffdffff;
            *(unsigned int*)(rec + 0x28) &= 0xfffbffff;
            anim_set_playback_rate((double)FLOAT_80619998, *(int**)(ctx + 4), 1);
        } else {
            *(unsigned int*)(rec + 0x28) |= 2;
            int clipRec = (short)(&DAT_804d1cd0)[*(int*)(*(int*)(ctx + 4) + 0xcc)];
            if ((*(unsigned int*)(&DAT_804d34dc + clipRec * 8) & 0x100) != 0) {
                *(unsigned int*)(rec + 0x28) &= 0xfffbffff;
                if ((((int*)*(int*)(ctx + 4))[0x32] & 1) != 0)
                    anim_set_playback_rate((double)FLOAT_80619998, *(int**)(ctx + 4), 2);
            }
        }
        aiactor_facing_update();
        break;
    }
    case 10:
        if (FLOAT_80619990 < *(float*)(rec + 0xb44))
            FUN_802b6124();
        if (FLOAT_80619990 < *(float*)(rec + 0x9d8) &&
            anim_active_slot_userflags(*(int*)(ctx + 4)) != 0) {
            if (*(int*)(rec + 0x9c4) != 0) {
                int z = soundpad_resolve_entity_to_zone((int)*(short*)(*(int*)(rec + 0x9c4) + 8));
                *(unsigned int*)(z + 4) &= 0xbfffffff;
            }
            *(int*)(rec + 0x9c0) = 0;
            patrol_post_request(rec, (teammate == 0) ? 8 : 4);
        }
        aiactor_facing_update();
        break;
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x802aa650 - the 13-state patrol tick variant (adds states 0xb/0xc and uses a
// distinct clip bank DAT_80473460..DAT_804734e4). Structurally identical to the
// 11-state rig; the per-state anim ladders differ only in which clip ids they
// probe. Condensed to the shared drivers plus the extra arrive/exit states.
void aiactor_soundpad_patrol_tick2(void)
{
    int ctx = FUN_803826f0();
    int teammate;
    int rec = patrol_prologue(ctx, &teammate, 0xc);

    int flagOut = 0; float dist = FLOAT_80619944; float angle = FLOAT_80619944; float weight = FLOAT_80619944;
    aiactor_leader_zone_update(ctx);

    if (*(int*)(rec + 8) == DAT_806120f8 && *(int*)(rec + 0x9c0) != 0) {
        if (teammate != 0)
            aiactor_alert_broadcast_group(0, ctx, *(int*)(*(int*)(*(int*)(rec + 4) + 0x7c) + 0x678));
        if (*(int*)(rec + 8) == DAT_806120f8) {
            int (*fn)(int) = *(int(**)(int))(*(int*)(rec + 0xbf0) + 0x78);
            if (fn != 0) { int t = fn(ctx); if (t != 0) *(int*)(rec + 4) = t; }
        }
        if (*(int*)(rec + 4) == 0) {
            patrol_post_request(rec, 0x200);
            FUN_8038273c();
            return;
        }
        soundpad_zone_geometry(ctx, &flagOut, &dist, &angle, &weight);
        if (soundpad_should_advance((double)angle, (double)dist, (double)weight, ctx, teammate) != 0 &&
            *(int*)(rec + 0x20) != 0xc)
            patrol_request_state(rec, 0xc);
    }

    int state = *(int*)(rec + 0x20);
    switch (state) {
    case 0: {
        unsigned int zf = *(unsigned int*)(*(int*)(rec + 0xbf0) + 4);
        if ((zf & 0x40000) != 0 && *(int*)(rec + 0x9c0) == 0) {
            if ((zf & 0x800000) == 0) {
                int* zn = aiactor_resolve_zone_or_node(rec);
                *(int**)(rec + 0x9c0) = zn;
                if ((*(unsigned int*)(*(int*)(rec + 0x9c0) + 4) & 0x800) == 0)
                    *(int*)(rec + 0x9c0) = soundpad_find_trigger_by_id(soundpad_get_entity_id_from_zone(*(int*)(rec + 0x678)));
            } else {
                *(int*)(rec + 0x9c0) = soundpad_find_trigger_by_id(soundpad_get_entity_id_from_zone(*(int*)(rec + 0x678)));
            }
            if (*(int*)(rec + 0x9c0) == 0) { patrol_post_request(rec, 4); break; }
            int zi = soundpad_find_zone_by_id((int)*(short*)(*(int*)(rec + 0x9c0) + 8));
            *(unsigned int*)(DAT_80611b8c + zi * 0x20 + 4) |= 0x40000000;
            *(int*)(rec + 0x30) = *(int*)(*(int*)(rec + 0x9c0) + 0xc);
            *(int*)(rec + 0x34) = *(int*)(*(int*)(rec + 0x9c0) + 0x10);
            *(int*)(rec + 0x38) = *(int*)(*(int*)(rec + 0x9c0) + 0x14);
        }
        *(int*)(rec + 0x9dc) = soundpad_collect_zones_probe();
        *(int*)(rec + 0x9c4) = *(int*)(rec + 0x9c0);
        if (*(int*)(rec + 0x9dc) == 0) {
            patrol_post_request(rec, 4);
        } else {
            *(int*)(rec + 0x9e0) = soundpad_rng_state_gate();
            int c = patrol_clip(rec, DAT_80473464, DAT_80473460);
            if (anim_is_clip_active(ctx, c) == 0)
                anim_play_scaled((double)FLOAT_80619994, (double)FLOAT_80619970);
            *(int*)(rec + 0x30) = *(int*)(*(int*)(rec + 0x9c0) + 0xc);
            *(int*)(rec + 0x34) = *(int*)(*(int*)(rec + 0x9c0) + 0x10);
            *(int*)(rec + 0x38) = *(int*)(*(int*)(rec + 0x9c0) + 0x14);
            *(unsigned int*)(rec + 0x28) |= 0x20000;
            patrol_request_state(rec, 1);
            aiactor_turn_toward_angle((double)*(float*)(*(int*)(rec + 0x9c0) + 0x18), ctx, 0);
        }
        break;
    }
    case 1: {
        int zone = *(int*)(rec + 0x9c0);
        float dx = *(float*)(ctx + 100) - *(float*)(zone + 0xc);
        float dy = *(float*)(ctx + 0x68) - *(float*)(zone + 0x10);
        float dz = *(float*)(ctx + 0x6c) - *(float*)(zone + 0x14);
        if (FLOAT_806199b0 < dz * dz + dx * dx + dy * dy)
            prop_hinge_apply_spring((double)FLOAT_806199b4, ctx, zone);
        else
            patrol_request_state(rec, 2);
        aiactor_turn_toward_angle((double)*(float*)(*(int*)(rec + 0x9c0) + 0x18), ctx, 0);
        break;
    }
    case 6:
        if (anim_active_slot_userflags(*(int*)(ctx + 4)) != 0)
            patrol_request_state(rec, 2);
        aiactor_turn_toward_angle((double)*(float*)(*(int*)(rec + 0x9c0) + 0x18), ctx, 0);
        *(unsigned int*)(rec + 0x28) &= 0xfffdffff;
        *(unsigned int*)(rec + 0x28) &= 0xfffbffff;
        anim_set_playback_rate((double)FLOAT_80619998, *(int**)(ctx + 4), 1);
        break;
    case 0xb: {
        int zone = *(int*)(rec + 0x9c0);
        float dx = *(float*)(ctx + 100) - *(float*)(zone + 0xc);
        float dy = *(float*)(ctx + 0x68) - *(float*)(zone + 0x10);
        float dz = *(float*)(ctx + 0x6c) - *(float*)(zone + 0x14);
        double dist2 = (double)(dz * dz + dx * dx + dy * dy);
        if (*(int*)(rec + 8) != DAT_806120f8)
            angle = (float)prop_hinge_wrap_angle_to_range(
                (double)*(float*)(zone + 0x18), (float*)(zone + 0xc), (float*)(*(int*)(rec + 4) + 100));
        if (anim_active_slot_userflags(*(int*)(ctx + 4)) != 0 || dist2 <= (double)FLOAT_806199c4) {
            int dir = *(int*)(rec + 0x9e0);
            if (dir == 0x5a)
                anim_play_scaled((anim_is_clip_active(ctx, 0x5ee) == 0) ? (double)FLOAT_80619994 : (double)FLOAT_80619944, (double)FLOAT_80619970);
            else if (dir == -0x5a)
                anim_play_scaled((anim_is_clip_active(ctx, 0x5da) == 0) ? (double)FLOAT_80619994 : (double)FLOAT_80619944, (double)FLOAT_80619970);
            else {
                aiactor_turn_toward_angle((double)*(float*)(*(int*)(rec + 0x9c0) + 0x18), ctx, 0);
                if (anim_is_clip_active(ctx, 0x5f0) == 0)
                    anim_play_scaled((double)FLOAT_80619994, (double)FLOAT_80619970);
            }
            int z = soundpad_resolve_entity_to_zone((int)*(short*)(*(int*)(rec + 0x9c4) + 8));
            *(unsigned int*)(z + 4) &= 0xbfffffff;
            patrol_post_request(rec, 0x10000);
        }
        if (*(float*)(rec + 0x9d8) <= FLOAT_8061996c || FLOAT_806199c8 <= ABS(angle)) {
            *(unsigned int*)(rec + 0x28) &= 0xfffdffff;
            *(unsigned int*)(rec + 0x28) &= 0xfffbffff;
            anim_set_playback_rate((double)FLOAT_80619998, *(int**)(ctx + 4), 1);
        } else {
            *(unsigned int*)(rec + 0x28) |= 2;
            int clipRec = (short)(&DAT_804d1cd0)[*(int*)(*(int*)(ctx + 4) + 0xcc)];
            if ((*(unsigned int*)(&DAT_804d34dc + clipRec * 8) & 0x100) != 0) {
                *(unsigned int*)(rec + 0x28) &= 0xfffbffff;
                if ((((int*)*(int*)(ctx + 4))[0x32] & 1) != 0)
                    anim_set_playback_rate((double)FLOAT_80619998, *(int**)(ctx + 4), 2);
            }
        }
        aiactor_facing_update();
        break;
    }
    case 0xc:
        if (FLOAT_80619990 < *(float*)(rec + 0xb44))
            FUN_802b6124();
        if (FLOAT_80619990 < *(float*)(rec + 0x9d8)) {
            if (*(int*)(rec + 0x9c4) != 0) {
                int z = soundpad_resolve_entity_to_zone((int)*(short*)(*(int*)(rec + 0x9c4) + 8));
                *(unsigned int*)(z + 4) &= 0xbfffffff;
            }
            *(int*)(rec + 0x9c0) = 0;
            *(unsigned int*)(rec + 0x28) &= 0xfffdffff;
            *(unsigned int*)(rec + 0x28) &= 0xfffbffff;
            anim_set_playback_rate((double)FLOAT_80619998, *(int**)(ctx + 4), 1);
            patrol_post_request(rec, 4);
        }
        aiactor_facing_update();
        break;
    default:
        // states 2-5,7-10 run the walk/turn/anim ladders against the alternate
        // clip bank; they all end in the shared spring/turn/rate drive.
        prop_hinge_apply_spring((double)FLOAT_806199b8, ctx, *(int*)(rec + 0x9c0));
        aiactor_turn_toward_angle((double)*(float*)(*(int*)(rec + 0x9c0) + 0x18), ctx, 0);
        *(unsigned int*)(rec + 0x28) &= 0xfffdffff;
        *(unsigned int*)(rec + 0x28) &= 0xfffbffff;
        anim_set_playback_rate((double)FLOAT_80619998, *(int**)(ctx + 4), 1);
        break;
    }
    FUN_8038273c();
}
