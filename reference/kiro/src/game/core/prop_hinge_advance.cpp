// prop_hinge_advance.cpp - advance a hinged held-prop's animation clock from
// the current hinge angle (door / valve / lever driven by anim clip frames).
//
// Both entries resolve the acting player-control context via the fp scratch
// frame (FUN_803826f0 / FUN_8038273c), read the bound anim clip's start frame
// (anim_get_frame_times) and the clip's total frame count from the clip table
// (DAT_80611824 -> clip record + 0xc), compute how far the hinge has swung
// relative to the actor's rest yaw (actor+0x70), wrap that delta into the clip's
// frame window [FLOAT_8061990c, FLOAT_80619910] stepping by FLOAT_80619908, then
// normalize by the frame count and accumulate onto the animation clock at
// actor+0xc4.
//
//   * prop_hinge_advance_auto (0x802a65b4) uses the current hinge yaw directly
//     (the passive / no-input tick).
//   * prop_hinge_advance_driven (0x802a6440) first folds in a driven-angle delta:
//     it wraps the hinge->target yaw (prop_hinge_wrap_angle_to_range), adds the
//     caller's push angle (unless the actor has no active hinge target, +0x9e0
//     == -1), clamps to the swing window [FLOAT_80619900, FLOAT_80619904], then
//     advances the clock from that clamped angle.
//
// Address range: 0x802a6440 - 0x802a668f
// Subsystem: game (held-prop animation)

#include "common.h"

extern "C" {

extern int   DAT_80611824;   // anim-clip record table base (indexed by clip id)

extern const float FLOAT_80619900;   // swing window min (degrees)
extern const float FLOAT_80619904;   // swing window max (degrees)
extern const float FLOAT_80619908;   // wrap step (full turn, degrees)
extern const float FLOAT_8061990c;   // wrap lower bound
extern const float FLOAT_80619910;   // wrap upper bound
extern const double DOUBLE_80619918; // int->float reconstruction bias (0x43300000 magic)

extern void   anim_get_frame_times(int clipHandle, int* outStart, int* outEnd);
extern double prop_hinge_wrap_angle_to_range(double baseAngle, float* toPos, float* fromPos);

extern int  FUN_803826f0(void);   // fp frame enter -> player-control context ptr
extern void FUN_8038273c(void);   // paired frame restore

} // extern "C"

// forward decls
void prop_hinge_advance_driven(double pushAngle); // 0x802a6440
void prop_hinge_advance_auto(void);               // 0x802a65b4

// ---------------------------------------------------------------------------
// 0x802a65b4 - passive advance: fold the current hinge yaw into the clip clock.
void prop_hinge_advance_auto(void)
{
    int ctx   = FUN_803826f0();
    int actor = *(int*)(ctx + 0x7c);

    int clip = *(int*)(DAT_80611824 + *(int*)(*(int*)(ctx + 4) + 0xd0) * 4);

    int startEnd[2];
    anim_get_frame_times(*(int*)(ctx + 4) + 0xcc, startEnd, (int*)0);
    unsigned int frameCount = (unsigned int)(*(int*)(clip + 0xc) - startEnd[0]);

    // current hinge yaw relative to the actor's rest yaw
    float angle = *(float*)(*(int*)(actor + 0x9c0) + 0x18) - *(float*)(ctx + 0x70);

    // wrap into the clip's frame window
    for (; angle <= FLOAT_8061990c; angle += FLOAT_80619908) { }
    for (; FLOAT_80619910 < angle; angle -= FLOAT_80619908) { }

    if (0 < (int)frameCount) {
        double denom = (double)(long long)(unsigned int)(frameCount ^ 0x80000000);
        angle = angle / (float)(denom - DOUBLE_80619918);
    }

    *(float*)(ctx + 0xc4) = *(float*)(ctx + 0xc4) + angle;
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x802a6440 - driven advance: fold a push-angle delta in first, clamp to the
// swing window, then advance the clip clock from the clamped angle.
void prop_hinge_advance_driven(double pushAngle)
{
    int ctx   = FUN_803826f0();
    int actor = *(int*)(ctx + 0x7c);

    int clip = *(int*)(DAT_80611824 + *(int*)(*(int*)(ctx + 4) + 0xd0) * 4);

    int startEnd[2];
    anim_get_frame_times(*(int*)(ctx + 4) + 0xcc, startEnd, (int*)0);
    unsigned int frameCount = (unsigned int)(*(int*)(clip + 0xc) - startEnd[0]);

    // hinge->target yaw about the actor's hinge pivot
    double wrapped = prop_hinge_wrap_angle_to_range(
        (double)*(float*)(*(int*)(actor + 0x9c0) + 0x18),
        (float*)(ctx + 100),
        (float*)(*(int*)(actor + 4) + 100));

    // no active hinge target -> ignore the caller's push delta
    if (*(int*)(actor + 0x9e0) == -1)
        pushAngle = 0.0;

    float target = (float)(wrapped + pushAngle);

    // clamp to [min, max]
    float clamped = target;
    if (FLOAT_80619904 < clamped)
        clamped = FLOAT_80619904;
    float result = FLOAT_80619900;
    if (FLOAT_80619900 <= clamped) {
        result = target;
        if (FLOAT_80619904 < target)
            result = FLOAT_80619904;
    }

    // wrap the (rest + clamped) yaw into the clip's frame window
    float angle = (*(float*)(*(int*)(actor + 0x9c0) + 0x18) + result) - *(float*)(ctx + 0x70);
    for (; angle <= FLOAT_8061990c; angle += FLOAT_80619908) { }
    for (; FLOAT_80619910 < angle; angle -= FLOAT_80619908) { }

    if (0 < (int)frameCount) {
        double denom = (double)(long long)(unsigned int)(frameCount ^ 0x80000000);
        angle = angle / (float)(denom - DOUBLE_80619918);
    }

    *(float*)(ctx + 0xc4) = *(float*)(ctx + 0xc4) + angle;
    FUN_8038273c();
}
