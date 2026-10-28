// Address range: 0x80137ffc - 0x801389af | batch_366 | 2026-09-05
//
// AI actor body-yaw turn + turn-animation selection. A family of near-identical
// helpers that rotate the actor's body heading (actor+0xc4) toward a target
// (leader entity / arbitrary entity / world point / absolute angle) at the
// per-frame turn rate, plus the animation picker that plays the matching
// turn/strafe locomotion clip.
//
// Shared turn math:
//   target yaw (deg) = atan2(dx, dz) * FLOAT_80615cd0 / FLOAT_80615ccc
//   wrapped into (-pi, pi] relative to the model heading (actor+0x70), then the
//   body yaw is integrated toward it by (+0xb2c)*dt (x FLOAT_806118ec slow-mo
//   when cheat bit 0x800 is set and pool state != 0x1b), clamped to not
//   overshoot, and re-wrapped into [0, 2pi).
//   FLOAT_80615cd0=pi, FLOAT_80615d00=2pi, FLOAT_80615d04=-pi.
//   FUN_803826f0/__save_gpr_28 return the fp/gpr scratch frame (actor ptr in
//   the high word; a mode/target selector in the low word).
//
// Functions:
//   aiactor_turn_toward_leader   0x80137ffc  turn toward render_data+4 leader
//   aiactor_turn_toward_entity   0x801381cc  turn toward an entity pointer
//   aiactor_turn_toward_position 0x801383b4  turn toward a world point
//   aiactor_turn_toward_angle    0x80138594  turn toward an absolute yaw
//   aiactor_play_turn_animation  0x80138754  pick/play turn/strafe anim

#include "common.h"

extern char   DAT_805015bd;      // game mode byte
extern u32    DAT_805015c0;      // cheat/flag word
extern u32    DAT_805015c4;      // cheat/flag word 2
extern int    DAT_806108e4;      // subsystem-active flag
extern float  FLOAT_806108ec;    // frame-time delta
extern float  FLOAT_806118ec;    // slow-mo factor
extern float  FLOAT_80615cac;    // 0.0
extern float  FLOAT_80615ca8;    // 1.0
extern float  FLOAT_80615ccc;    // deg<->rad numerator
extern float  FLOAT_80615cd0;    // pi
extern float  FLOAT_80615cf0;    // anim-speed denominator
extern float  FLOAT_80615cf8;    // default anim blend speed
extern float  FLOAT_80615d00;    // 2*pi
extern float  FLOAT_80615d04;    // -pi
extern float  FLOAT_80615d18;    // turn-complete threshold
extern int    DAT_804d1cd0;      // floor-material index table (short[])
extern int    DAT_804d34dc;      // floor-material flags table (0x8 stride)

extern unsigned long long FUN_803826f0(void);   // fp frame enter
extern void               FUN_8038273c(void);    // fp frame leave
extern unsigned long long __save_gpr_28(void);   // gpr frame enter
extern void               __restore_gpr_28(void);
extern double math_atan2(double y, double x);
extern int    player_slot_get_pool_base(void);

// turn-animation helpers
extern void   FUN_80285528(int actor);
extern int    FUN_8002588c(int actor, int animId);
extern void   FUN_80023280(double a, double b);
extern void   FUN_800255c4(int actor, float *outA, float *outB, float *outC, int mode);
extern void   FUN_80025c04(double speed, int *defPtr);

// ---------------------------------------------------------------------------
// aiactor_turn_toward_leader 0x80137ffc
// ---------------------------------------------------------------------------
void aiactor_turn_toward_leader(void)
{
    float yaw;
    int actor;
    int leader;
    int rd;
    double ang, rate, target;
    unsigned long long frame;

    frame = FUN_803826f0();
    actor = (int)((unsigned long long)frame >> 0x20);
    rd = *(int *)(actor + 0x7c);
    leader = *(int *)(rd + 4);
    if ((DAT_806108e4 != 0) && (leader != 0)) {
        ang = math_atan2((double)(*(float *)(leader + 100) - *(float *)(actor + 100)),
                         (double)(*(float *)(leader + 0x6c) - *(float *)(actor + 0x6c)));
        yaw = (FLOAT_80615cd0 * (float)ang) / FLOAT_80615ccc;
        if (yaw < FLOAT_80615cac) {
            yaw = yaw + FLOAT_80615d00;
        }
        for (yaw = yaw - *(float *)(actor + 0x70); yaw <= FLOAT_80615d04; yaw = yaw + FLOAT_80615d00) { }
        for (; FLOAT_80615cd0 < yaw; yaw = yaw - FLOAT_80615d00) { }
        *(float *)(rd + 0xae0) = yaw;

        if ((int)frame == 1) {
            *(float *)(actor + 0xc4) = *(float *)(actor + 0xc4) + *(float *)(rd + 0xae0);
        }
        else {
            rate = (double)(*(float *)(rd + 0xb2c) * FLOAT_806108ec);
            if (((((DAT_805015c0 & 0x800) != 0) && ((DAT_805015c4 & 0x2000000) == 0)) &&
                 (player_slot_get_pool_base() != 0)) &&
                (*(int *)(player_slot_get_pool_base() + 0x218) != 0x1b)) {
                rate = (double)(float)(rate * (double)FLOAT_806118ec);
            }
            target = (double)*(float *)(rd + 0xae0);
            if (ABS(target) < rate) {
                *(float *)(actor + 0xc4) = (float)((double)*(float *)(actor + 0xc4) + target);
            }
            else if (target <= (double)FLOAT_80615cac) {
                *(float *)(actor + 0xc4) = (float)((double)*(float *)(actor + 0xc4) - rate);
            }
            else {
                *(float *)(actor + 0xc4) = (float)((double)*(float *)(actor + 0xc4) + rate);
            }
        }
        if (FLOAT_80615d00 < *(float *)(actor + 0xc4)) {
            *(float *)(actor + 0xc4) = *(float *)(actor + 0xc4) - FLOAT_80615d00;
        }
        if (*(float *)(actor + 0xc4) < FLOAT_80615cac) {
            *(float *)(actor + 0xc4) = *(float *)(actor + 0xc4) + FLOAT_80615d00;
        }
    }
    FUN_8038273c();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_turn_toward_entity 0x801381cc
// ---------------------------------------------------------------------------
void aiactor_turn_toward_entity(u32 param_1, u32 param_2, int instant)
{
    float yaw;
    int actor;
    int target;
    int rd;
    double ang, rate, delta;
    unsigned long long frame;
    (void)param_1; (void)param_2;

    frame = __save_gpr_28();
    actor = (int)((unsigned long long)frame >> 0x20);
    target = (int)frame;
    rd = *(int *)(actor + 0x7c);
    if ((DAT_806108e4 != 0) && (target != 0)) {
        ang = math_atan2((double)(*(float *)(target + 100) - *(float *)(actor + 100)),
                         (double)(*(float *)(target + 0x6c) - *(float *)(actor + 0x6c)));
        yaw = (FLOAT_80615cd0 * (float)ang) / FLOAT_80615ccc;
        if (yaw < FLOAT_80615cac) {
            yaw = yaw + FLOAT_80615d00;
        }
        yaw = yaw - *(float *)(actor + 0x70);
        for (delta = (double)yaw; delta <= (double)FLOAT_80615d04; delta = delta + (double)FLOAT_80615d00) {
            yaw = (float)delta;
            delta = (double)yaw;
        }
        for (; (double)FLOAT_80615cd0 < delta; delta = (double)(float)(delta - (double)FLOAT_80615d00)) { }

        if (instant == 1) {
            *(float *)(actor + 0xc4) = (float)((double)*(float *)(actor + 0xc4) + delta);
        }
        else {
            rate = (double)(*(float *)(rd + 0xb2c) * FLOAT_806108ec);
            if (((((DAT_805015c0 & 0x800) != 0) && ((DAT_805015c4 & 0x2000000) == 0)) &&
                 (player_slot_get_pool_base() != 0)) &&
                (*(int *)(player_slot_get_pool_base() + 0x218) != 0x1b)) {
                rate = (double)(float)(rate * (double)FLOAT_806118ec);
            }
            if (ABS(delta) < rate) {
                *(float *)(actor + 0xc4) = (float)((double)*(float *)(actor + 0xc4) + delta);
            }
            else if (delta <= (double)FLOAT_80615cac) {
                *(float *)(actor + 0xc4) = (float)((double)*(float *)(actor + 0xc4) - rate);
            }
            else {
                *(float *)(actor + 0xc4) = (float)((double)*(float *)(actor + 0xc4) + rate);
            }
        }
        if (FLOAT_80615d00 < *(float *)(actor + 0xc4)) {
            *(float *)(actor + 0xc4) = *(float *)(actor + 0xc4) - FLOAT_80615d00;
        }
        if (*(float *)(actor + 0xc4) < FLOAT_80615cac) {
            *(float *)(actor + 0xc4) = *(float *)(actor + 0xc4) + FLOAT_80615d00;
        }
    }
    __restore_gpr_28();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_turn_toward_position 0x801383b4
// ---------------------------------------------------------------------------
void aiactor_turn_toward_position(u32 param_1, u32 param_2, int instant)
{
    float yaw;
    int actor;
    int rd;
    double ang, rate, delta;
    unsigned long long frame;
    float *targetPos;
    (void)param_1; (void)param_2;

    frame = __save_gpr_28();
    actor = (int)((unsigned long long)frame >> 0x20);
    targetPos = (float *)(u32)frame;
    rd = *(int *)(actor + 0x7c);
    if (DAT_806108e4 != 0) {
        ang = math_atan2((double)(targetPos[0] - *(float *)(actor + 100)),
                         (double)(targetPos[2] - *(float *)(actor + 0x6c)));
        yaw = (FLOAT_80615cd0 * (float)ang) / FLOAT_80615ccc;
        if (yaw < FLOAT_80615cac) {
            yaw = yaw + FLOAT_80615d00;
        }
        yaw = yaw - *(float *)(actor + 0x70);
        for (delta = (double)yaw; delta <= (double)FLOAT_80615d04; delta = delta + (double)FLOAT_80615d00) {
            yaw = (float)delta;
            delta = (double)yaw;
        }
        for (; (double)FLOAT_80615cd0 < delta; delta = (double)(float)(delta - (double)FLOAT_80615d00)) { }

        if (instant == 1) {
            *(float *)(actor + 0xc4) = (float)((double)*(float *)(actor + 0xc4) + delta);
        }
        else {
            rate = (double)(*(float *)(rd + 0xb2c) * FLOAT_806108ec);
            if (((((DAT_805015c0 & 0x800) != 0) && ((DAT_805015c4 & 0x2000000) == 0)) &&
                 (player_slot_get_pool_base() != 0)) &&
                (*(int *)(player_slot_get_pool_base() + 0x218) != 0x1b)) {
                rate = (double)(float)(rate * (double)FLOAT_806118ec);
            }
            if (ABS(delta) < rate) {
                *(float *)(actor + 0xc4) = (float)((double)*(float *)(actor + 0xc4) + delta);
            }
            else if (delta <= (double)FLOAT_80615cac) {
                *(float *)(actor + 0xc4) = (float)((double)*(float *)(actor + 0xc4) - rate);
            }
            else {
                *(float *)(actor + 0xc4) = (float)((double)*(float *)(actor + 0xc4) + rate);
            }
        }
        if (FLOAT_80615d00 < *(float *)(actor + 0xc4)) {
            *(float *)(actor + 0xc4) = *(float *)(actor + 0xc4) - FLOAT_80615d00;
        }
        if (*(float *)(actor + 0xc4) < FLOAT_80615cac) {
            *(float *)(actor + 0xc4) = *(float *)(actor + 0xc4) + FLOAT_80615d00;
        }
    }
    __restore_gpr_28();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_turn_toward_angle 0x80138594
// Returns true when the actor is now within the turn-complete threshold.
// ---------------------------------------------------------------------------
bool aiactor_turn_toward_angle(double targetYaw, int actor, int instant)
{
    float residual;
    double delta, rate;

    for (delta = targetYaw - (double)*(float *)(actor + 0x70);
         delta = (double)(float)delta, delta <= (double)FLOAT_80615d04;
         delta = delta + (double)FLOAT_80615d00) { }
    for (; (double)FLOAT_80615cd0 < delta; delta = (double)(float)(delta - (double)FLOAT_80615d00)) { }

    rate = delta;
    if (instant == 0) {
        rate = (double)(*(float *)(*(int *)(actor + 0x7c) + 0xb2c) * FLOAT_806108ec);
    }
    if (((((DAT_805015c0 & 0x800) != 0) && ((DAT_805015c4 & 0x2000000) == 0)) &&
         (player_slot_get_pool_base() != 0)) &&
        (*(int *)(player_slot_get_pool_base() + 0x218) != 0x1b)) {
        rate = (double)(float)(rate * (double)FLOAT_806118ec);
    }
    if (ABS(delta) < rate) {
        *(float *)(actor + 0xc4) = (float)targetYaw;
    }
    else if (delta <= (double)FLOAT_80615cac) {
        *(float *)(actor + 0xc4) = (float)((double)*(float *)(actor + 0xc4) - rate);
    }
    else {
        *(float *)(actor + 0xc4) = (float)((double)*(float *)(actor + 0xc4) + rate);
    }
    if (FLOAT_80615d00 < *(float *)(actor + 0xc4)) {
        *(float *)(actor + 0xc4) = *(float *)(actor + 0xc4) - FLOAT_80615d00;
    }
    if (*(float *)(actor + 0xc4) < FLOAT_80615cac) {
        *(float *)(actor + 0xc4) = *(float *)(actor + 0xc4) + FLOAT_80615d00;
    }

    for (residual = (float)(targetYaw - (double)*(float *)(actor + 0xc4));
         residual <= FLOAT_80615d04; residual = residual + FLOAT_80615d00) { }
    for (; FLOAT_80615cd0 < residual; residual = residual - FLOAT_80615d00) { }
    return ABS(residual) < FLOAT_80615d18;
}

// ---------------------------------------------------------------------------
// aiactor_play_turn_animation 0x80138754
// ---------------------------------------------------------------------------
void aiactor_play_turn_animation(int actor)
{
    float angVel;
    u32 flags;
    int rd;
    float outC;
    float outB;
    float outA[2];

    rd = *(int *)(actor + 0x7c);
    if (*(int *)(rd + 0x44) == 1) {
        FUN_80285528(actor);
        return;
    }

    flags = *(u32 *)(rd + 0x24);
    if (((flags & 8) != 0) || ((flags & 0x40) != 0)) {
        return;
    }

    if ((flags & 4) == 0) {
        angVel = *(float *)(rd + 0xb28);
        if (angVel < FLOAT_80615cac) {
            if (-*(float *)(rd + 0xb18) <= angVel) {
                if (FUN_8002588c(actor, 0x2b) == 0) FUN_80023280((double)FLOAT_80615cf8, (double)FLOAT_80615ca8);
            }
            else {
                if (FUN_8002588c(actor, 0x27) == 0) FUN_80023280((double)FLOAT_80615cf8, (double)FLOAT_80615ca8);
            }
        }
        else if (angVel <= *(float *)(rd + 0xb18)) {
            if (FUN_8002588c(actor, 0x2a) == 0) FUN_80023280((double)FLOAT_80615cf8, (double)FLOAT_80615ca8);
        }
        else {
            if (FUN_8002588c(actor, 0x26) == 0) FUN_80023280((double)FLOAT_80615cf8, (double)FLOAT_80615ca8);
        }
    }
    else {
        if (FUN_8002588c(actor, 0x25) == 0) FUN_80023280((double)FLOAT_80615cf8, (double)FLOAT_80615ca8);
    }

    // On floors flagged 0x200000, tune turn-anim playback speed to angular vel.
    int matFlagsIdx = (short)(&DAT_804d1cd0)[*(int *)(*(int *)(actor + 4) + 0xcc)] * 8;
    if (((*(u32 *)(&DAT_804d34dc + matFlagsIdx) & 0x200000) != 0) &&
        (FLOAT_80615cac == *(float *)(*(int *)(actor + 4) + 400))) {
        FUN_800255c4(actor, outA, &outB, &outC, 2);
        if ((ABS(ABS(outB - outA[0])) <= FLOAT_80615ca8) ||
            (FLOAT_80615cac == *(float *)(rd + 0xb28))) {
            FUN_80025c04((double)FLOAT_80615ca8, *(int **)(actor + 4));
        }
        else {
            FUN_80025c04((double)((outC * ABS(*(float *)(rd + 0xb28))) /
                                  (FLOAT_80615cf0 * ABS(outB - outA[0]))),
                         *(int **)(actor + 4));
        }
    }
    return;
}
