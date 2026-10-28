// prop_hinge_angle.cpp - Hinged/rotating held-prop angle constraint + player push.
//
// A held prop (door / valve / lever) rotates about a hinge. These helpers wrap
// a target angle into the hinge's [-range, +range] window, apply a spring back
// toward rest, test whether an angle is inside the swing limits, and run the
// per-frame push where the player who is holding the prop drives it toward the
// direction they face. prop_hinge_player_push_tick is the entry point
// (dispatched from the held-object update, e.g. FUN_80264bb8).
//
// Address range: 0x8025c190 - 0x8025c3e0

#include "common.h"

extern "C" {
extern unsigned char DAT_805015bd;   // co-op sub-mode selector

extern const float FLOAT_80618f98;   // 0.0
extern const float FLOAT_80618f9c;   // 180.0 (half-turn, degrees)
extern const float FLOAT_80618fa0;   // small angle/vel epsilon
extern const float FLOAT_80618fa4;   // angle->smoothstep bias
extern const float FLOAT_80618fa8;   // min planar distance to react

extern double math_sqrt(double x);
extern void   dir_to_angles(float* dir, int flags, float* outAngles);
extern double FUN_80390324(double t);           // smoothstep / ease curve
extern int    aiactor_find_nearby_special_actor(void); // (was FUN_802c163c)
extern void   prop_get_world_pivot(int prop, float* outPos);
extern int    soundpad_resolve_entity_to_zone(int entityId);

extern unsigned long long FUN_803826f0(void);   // player-control work context
extern void   FUN_8038273c(void);
}

// forward decls
double prop_hinge_wrap_angle_to_range(double baseAngle, float* toPos, float* fromPos); // 0x8025c190
unsigned int prop_hinge_angle_in_range(double angle, int prop);                        // 0x8025c2ac

// ---------------------------------------------------------------------------
// 0x8025c190 - compute the yaw from fromPos->toPos, then pick whichever of
// {angle, angle+180, angle-180} is closest to baseAngle (wrap the delta into
// the nearest half-turn). Returns the wrapped target angle.
double prop_hinge_wrap_angle_to_range(double baseAngle, float* toPos, float* fromPos)
{
    double yaw;
    double dPlus;
    double dBase;
    double dMinus;
    double best;
    float dir[3];
    float angles[3];

    dir[0] = *toPos - *fromPos;
    dir[1] = FLOAT_80618f98;
    dir[2] = toPos[2] - fromPos[2];
    dir_to_angles(dir, 0, angles);
    yaw = (double)angles[0];

    dPlus  = (double)(float)(yaw - (double)(float)(baseAngle + (double)FLOAT_80618f9c));
    dBase  = ABS((double)(float)(yaw - baseAngle));
    dMinus = (double)(float)(yaw - (double)(float)(baseAngle - (double)FLOAT_80618f9c));
    best = dMinus;
    if (ABS((double)dPlus) <= dBase) {
        if (ABS((double)dPlus) < ABS(dMinus)) {
            best = dPlus;
        }
    } else if (dBase < ABS(dMinus)) {
        best = (double)(float)(yaw - baseAngle);
    }
    return best;
}

// ---------------------------------------------------------------------------
// 0x8025c254 - spring the prop's held-angle pair (+0x64 / +0x6c) toward its
// rest values (+0xc / +0x14 on the constraint) by factor 'gain'; no-op when
// both offsets are already within epsilon.
void prop_hinge_apply_spring(double gain, int prop, int constraint)
{
    double dx;
    double dz;

    dx = (double)(*(float*)(prop + 100) - *(float*)(constraint + 0xc));
    dz = (double)(*(float*)(prop + 0x6c) - *(float*)(constraint + 0x14));
    if ((ABS(dx) <= (double)FLOAT_80618fa0) && (ABS(dz) <= (double)FLOAT_80618fa0)) {
        return;
    }
    *(float*)(prop + 100)  = -(float)(gain * dx - (double)*(float*)(prop + 100));
    *(float*)(prop + 0x6c) = -(float)(gain * dz - (double)*(float*)(prop + 0x6c));
}

// ---------------------------------------------------------------------------
// 0x8025c2ac - test whether 'angle' lies within the prop's hinge swing limits.
// Reads the two limit offsets (+0x3c / +0x40 on the prop's object), eases each
// through FUN_80390324, and returns 1 when 'angle' is inside the [lo,hi] arc
// (handles the wrapped case where hi < lo). Degenerate (both limits 0) -> 1.
unsigned int prop_hinge_angle_in_range(double angle, int prop)
{
    int object;
    unsigned int inside;
    double hi;
    double lo;
    double eased;

    object = *(int*)(prop + 0x7c);
    if ((FLOAT_80618f98 == *(float*)(object + 0x3c)) &&
        (FLOAT_80618f98 == *(float*)(object + 0x40))) {
        return 1;
    }

    hi = (double)(*(float*)(object + 0x40) + *(float*)(prop + 0x70));
    eased = FUN_80390324((double)((float)(hi - (double)FLOAT_80618fa4) / FLOAT_80618f9c));
    hi = -(double)(float)((double)FLOAT_80618f9c * (double)(float)eased - hi);

    lo = (double)(*(float*)(*(int*)(prop + 0x7c) + 0x3c) + *(float*)(prop + 0x70));
    eased = FUN_80390324((double)(float)((double)(float)(lo - (double)FLOAT_80618fa4) /
                                         (double)FLOAT_80618f9c));
    lo = -(double)(float)((double)FLOAT_80618f9c * (double)(float)eased - lo);

    if ((double)FLOAT_80618fa0 <= ABS((float)(hi - lo))) {
        if (lo <= hi) {
            inside = 0;
            if ((lo < angle) && (angle < hi)) {
                inside = 1;
            }
        } else {
            inside = 0;
            if ((lo < angle) || (angle < hi)) {
                inside = 1;
            }
        }
    } else {
        inside = 1;
    }
    return inside;
}

// ---------------------------------------------------------------------------
// 0x8025c3e0 - per-frame push of a held hinged prop by the player holding it:
// resolve the prop, require the player-facing constraint present and outside a
// 0x1000 lock bit, compute planar offset player->hinge, and if far enough,
// wrap the facing angle into the hinge range and (when inside limits) set the
// prop's drive fields (+0xaac / +0xaa8) and record its zone (+0x9c0).
void prop_hinge_player_push_tick(void)
{
    int player;
    int prop;
    unsigned int modeFlags;
    int propObject;
    double dist;
    double wrapped;
    double eased;
    float pivot[2];
    float pivotZ;

    player = (int)FUN_803826f0();
    player = *(int*)(player + 0x7c);
    if ((((*(unsigned int*)(*(int*)(player + 0xbf0) + 0x58) & 0x1000) != 0) ||
         (prop = aiactor_find_nearby_special_actor(), prop == 0)) || (*(int*)(player + 4) == 0)) {
        FUN_8038273c();
        return;
    }
    propObject = *(int*)(prop + 0x7c);
    prop_get_world_pivot(prop, pivot);
    pivotZ = *(float*)(*(int*)(player + 4) + 0x6c) - pivotZ;
    pivot[0] = *(float*)(*(int*)(player + 4) + 100) - pivot[0];
    dist = math_sqrt((double)(pivot[0] * pivot[0] + pivotZ * pivotZ));
    if (dist <= (double)FLOAT_80618fa8) {
        FUN_8038273c();
        return;
    }

    wrapped = prop_hinge_wrap_angle_to_range((double)FLOAT_80618f98, pivot,
                                             (float*)(*(int*)(player + 4) + 100));
    eased = FUN_80390324((double)((float)(wrapped - (double)FLOAT_80618fa4) / FLOAT_80618f9c));
    if (prop_hinge_angle_in_range(
            -(double)(float)((double)FLOAT_80618f9c * (double)(float)eased - wrapped), prop) == 0) {
        FUN_8038273c();
        return;
    }

    if ((*(int*)(player + 0xaac) == 1) && (*(int*)(player + 0xaa8) == 0)) {
        if (DAT_805015bd == '\n') {
            modeFlags = *(unsigned int*)(player + 0x14);
        } else {
            modeFlags = *(unsigned int*)(player + 0x1c);
        }
        if ((modeFlags & 1) == 0) {
            *(int*)(player + 0xaac) = 0x100;
            *(int*)(player + 0xaa8) = 0;
        }
    } else {
        *(int*)(player + 0xaac) = 0x100;
        *(int*)(player + 0xaa8) = 0;
    }
    *(int*)(player + 0x9c0) = soundpad_resolve_entity_to_zone(*(int*)(propObject + 0x1c0));
    FUN_8038273c();
}
