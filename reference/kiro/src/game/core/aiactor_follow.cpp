// aiactor_follow.cpp - AI companion follow/nav behavior-state ticks.
//
// Two per-frame behavior handlers dispatched from the AI behavior driver
// FUN_80281da4 (0x80281da4). Both maintain a small state machine on the actor
// (state field, previous state, transition timer) that switches between:
//   follow-ally, path-nav, idle/regroup, and combat-approach.
//
//  aiactor_follow_companion_tick  (0x8025cc7c): ally-following companion. Picks
//    the nearest visible ally, drives navigation toward them, and toggles the
//    actor's animation-intent fields (+0x2aa/+0x2ab) accordingly.
//  aiactor_follow_zone_nav_tick   (0x8025d41c): zone-path follower. Follows a
//    scripted zone path, plays scaled locomotion anims, resolves the next zone
//    from its spawn descriptor, and self-destructs when its lifetime expires.
//
// Address range: 0x8025cc7c - 0x8025d41c

#include "common.h"

extern "C" {
extern unsigned char DAT_805015bd;   // co-op sub-mode selector

extern int   DAT_80611d78;   // player record table base
extern int   DAT_80612610;   // active player count
extern int   DAT_806120a4;   // "current follow leader" player index
extern int   DAT_805889f0;   // companion pointer table (stride 8)
extern float FLOAT_806108ec; // per-frame time scale numerator
extern int   DAT_806109c0;   // frame delta ticks (signed, xor bias)

extern const float  FLOAT_80618f98;   // 0.0
extern const float  FLOAT_80618fac;   // small threshold
extern const float  FLOAT_80618fb8;   // 1.0 base
extern const float  FLOAT_80618fc0;
extern const float  FLOAT_80618fd0;
extern const float  FLOAT_80618fd4;
extern const float  FLOAT_80618fd8;
extern const float  FLOAT_80618fdc;
extern const float  FLOAT_80618fe0;
extern const float  FLOAT_80618ff0;   // anim play rate
extern const float  FLOAT_80618ff4;   // path-progress threshold
extern const float  FLOAT_80618ff8;   // lifetime threshold
extern const double DOUBLE_80618fc8;  // frame-delta bias
extern const double DOUBLE_80618fe8;  // rng bias

extern unsigned long long FUN_803826f0(void);
extern void FUN_8038273c(void);

extern int  aiactor_retarget_visible_teammate(void);
extern int  aiactor_find_nearest_visible_ally(void);
extern void aiactor_set_stance_flags(int ctx, int flags);
extern int  aiactor_zone_door_tick(void);
extern int  aiactor_nav_advance_path(int ctx, int* pathState);
extern void aiactor_pick_alternate_zone(int ctx);
extern void aiactor_mover_set_passability(int on);
extern int  aiactor_follow_zone_path(void);
extern void aiactor_destroy_instance(double lifetime, double p2, double p3, double p4,
        unsigned long long p5, unsigned long long p6, unsigned long long p7,
        unsigned long long p8, int ctx);
extern int  soundpad_find_nearest_entity(double a, float* pos);
extern int  soundpad_find_zone_by_id(int id);
extern unsigned int rng_counter_tick(void);
extern int  anim_active_slot_userflags(int animCtx);
extern int  anim_is_clip_active(int ctx, int clipId);
extern void anim_play_scaled(double rate, double p2);
}

// ---------------------------------------------------------------------------
// 0x8025cc7c - ally-following companion behavior tick.
void aiactor_follow_companion_tick(void)
{
    int ctx;
    unsigned int modeFlags;
    int rng;
    int nearest;
    int nav;
    int probeOff;
    int* actor;
    int probeCount;
    int allyState;

    ctx = (int)FUN_803826f0();
    actor = *(int**)(ctx + 0x7c);
    aiactor_retarget_visible_teammate();

    if (-1 < actor[0x274]) {
        if (actor[0x274] != actor[8]) {
            actor[0x276] = (int)FLOAT_80618f98;
            actor[0x275] = actor[8];
            actor[8] = actor[0x274];
        }
        actor[0x274] = -1;
    }
    if ((actor[0xb] & 0x80U) == 0) {
        actor[0x2a4] = (int)FLOAT_80618f98;
    } else {
        actor[0x2a4] = (int)(FLOAT_806108ec *
            (FLOAT_80618fb8 / (float)((double)CONCAT44(0x43300000,
                DAT_806109c0 ^ 0x80000000) - DOUBLE_80618fc8)) + (float)actor[0x2a4]);
    }

    if ((actor[1] == 0) ||
        (((actor != (int*)0) && (nearest = *actor, nearest != 0) && (DAT_80612610 <= actor[2])) &&
         (((*(unsigned int*)(nearest + 0x28) & 0x100) != 0) ||
          ((*(unsigned int*)(nearest + 0x10) & 0xffff) == 0)))) {
        actor[0xc] = *(int*)(*actor + 100);
        actor[0xd] = *(int*)(*actor + 0x68);
        actor[0xe] = *(int*)(*actor + 0x6c);
    } else {
        actor[0xc] = *(int*)(actor[1] + 100);
        actor[0xd] = *(int*)(actor[1] + 0x68);
        actor[0xe] = *(int*)(actor[1] + 0x6c);
    }
    aiactor_set_stance_flags(ctx, 4);

    if (aiactor_zone_door_tick() != 0) {
        actor[0x26b] = 0;
        actor[0x26a] = 0;
        if (!((actor[0x2ab] == 1) && (actor[0x2aa] == 0) &&
              ((DAT_805015bd == '\n' ? actor[5] : actor[7]) & 1) == 0)) {
            actor[0x2ab] = actor[0x2af];
            actor[0x2aa] = actor[0x2ae];
        }
        FUN_8038273c();
        return;
    }

    if ((actor[10] & 0x200U) != 0) {
        FUN_8038273c();
        return;
    }

    allyState = actor[8];
    if (allyState == 2) {
        if ((DAT_805015bd == '\x01') || (DAT_805015bd == '\x02')) {
            probeOff = 0;
            probeCount = 6;
            do {
                int companion = *(int*)((int)&DAT_805889f0 + probeOff);
                if (((companion != 0) && (*(int*)(*(int*)(companion + 0x7c) + 0xa0) == 1)) &&
                    (actor[0x21] != *(int*)(*(int*)(companion + 0x7c) + 0x1c)) &&
                    ((*(unsigned int*)(companion + 0x28) & 1) != 0)) {
                    if (!((actor[0x2ab] == 1) && (actor[0x2aa] == 0) &&
                          (((DAT_805015bd == '\n' ? actor[5] : actor[7]) & 1) == 0))) {
                        actor[0x2ab] = 0x10;
                        actor[0x2aa] = 0;
                    }
                    FUN_8038273c();
                    return;
                }
                probeOff = probeOff + 8;
                probeCount = probeCount + -1;
            } while (probeCount != 0);
        }
        nearest = aiactor_find_nearest_visible_ally();
        if (nearest != 0) {
            actor[1] = nearest;
        }
        if (actor[1] != 0) {
            if (actor[8] != 1) {
                actor[0x275] = actor[8];
                actor[8] = 1;
                actor[0x276] = (int)FLOAT_80618f98;
                actor[0x274] = actor[8];
            }
            nav = *(int*)(*(int*)(actor[1] + 0x7c) + 0x678);
            if (nav < 0) {
                actor[0x1a1] = *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
            } else {
                actor[0x1a1] = nav;
            }
            actor[0x26b] = 0;
            actor[0x26a] = 0;
            nav = aiactor_nav_advance_path(ctx, actor + 0x19e);
            if (-1 < nav) {
                if (!((actor[0x2ab] == 1) && (actor[0x2aa] == 0) &&
                      (((DAT_805015bd == '\n' ? actor[5] : actor[7]) & 1) == 0))) {
                    actor[0x2ab] = 8;
                    actor[0x2aa] = 0;
                }
            }
        }
    } else if (allyState < 2) {
        if (allyState == 0) {
            actor[0x275] = 0;
            actor[8] = 3;
            actor[0x276] = (int)FLOAT_80618f98;
            actor[0x274] = actor[8];
            rng = rng_counter_tick();
            actor[0x278] = (int)(FLOAT_80618fd4 *
                ((float)((double)CONCAT44(0x43300000, rng) - DOUBLE_80618fe8) / FLOAT_80618fd8) +
                FLOAT_80618fd0);
            actor[0x2cc] = (int)FLOAT_80618f98;
            nearest = soundpad_find_nearest_entity((double)FLOAT_80618f98, (float*)(ctx + 100));
            actor[0x19e] = nearest;
        } else if (-1 < allyState) {
            if (ABS((float)actor[0x2b8]) < FLOAT_80618fdc) {
                if (aiactor_retarget_visible_teammate() == 0) {
                    if (actor[8] != 2) {
                        actor[0x275] = actor[8];
                        actor[8] = 2;
                        actor[0x276] = (int)FLOAT_80618f98;
                        actor[0x274] = actor[8];
                    }
                } else if (actor[8] != 4) {
                    actor[0x275] = actor[8];
                    actor[8] = 4;
                    actor[0x276] = (int)FLOAT_80618f98;
                    actor[0x274] = actor[8];
                }
            }
            if (((actor[2] == DAT_806120a4) &&
                 (aiactor_retarget_visible_teammate() == 0)) && (actor[8] != 2)) {
                actor[0x275] = actor[8];
                actor[8] = 2;
                actor[0x276] = (int)FLOAT_80618f98;
                actor[0x274] = actor[8];
            }
        }
    } else if (allyState == 4) {
        if ((actor[10] & 2U) == 0) {
            actor[0x275] = 4;
            actor[8] = 3;
            actor[0x276] = (int)FLOAT_80618f98;
            actor[0x274] = actor[8];
            rng = rng_counter_tick();
            actor[0x278] = (int)(FLOAT_80618fd0 *
                ((float)((double)CONCAT44(0x43300000, rng) - DOUBLE_80618fe8) / FLOAT_80618fd8) +
                FLOAT_80618fe0);
            actor[0x2cc] = (int)FLOAT_80618f98;
        }
    } else if (allyState < 4) {
        if ((actor[1] == 0) || (aiactor_retarget_visible_teammate() == 0)) {
            if (actor[8] != 2) {
                actor[0x275] = actor[8];
                actor[8] = 2;
                actor[0x276] = (int)FLOAT_80618f98;
                actor[0x274] = actor[8];
            }
        } else if ((float)actor[0x278] < (float)actor[0x2cc]) {
            actor[10] = actor[10] | 2;
            if (actor[8] != 4) {
                actor[0x275] = actor[8];
                actor[8] = 4;
                actor[0x276] = (int)FLOAT_80618f98;
                actor[0x274] = actor[8];
            }
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8025d41c - zone-path follower behavior tick.
void aiactor_follow_zone_nav_tick(unsigned long long param_1, double param_2, double param_3,
        double param_4, unsigned long long param_5, unsigned long long param_6,
        unsigned long long param_7, unsigned long long param_8, int actorCtx)
{
    int state;
    unsigned int modeFlags;
    int zone;
    int actor;

    (void)param_1;
    actor = *(int*)(actorCtx + 0x7c);
    aiactor_retarget_visible_teammate();

    if (-1 < *(int*)(actor + 0x9d0)) {
        if (*(int*)(actor + 0x9d0) != *(int*)(actor + 0x20)) {
            *(float*)(actor + 0x9d8) = FLOAT_80618f98;
            *(int*)(actor + 0x9d4) = *(int*)(actor + 0x20);
            *(int*)(actor + 0x20) = *(int*)(actor + 0x9d0);
        }
        *(int*)(actor + 0x9d0) = -1;
    }
    if ((*(unsigned int*)(actor + 0x2c) & 0x80) == 0) {
        *(float*)(actor + 0xa90) = FLOAT_80618f98;
    } else {
        *(float*)(actor + 0xa90) = (float)((double)FLOAT_806108ec *
            (double)(float)((double)FLOAT_80618fb8 /
                (double)(float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                    DOUBLE_80618fc8)) + (double)*(float*)(actor + 0xa90));
    }

    state = *(int*)(actor + 0x20);
    if (state == 1) {
        if (aiactor_follow_zone_path() != 0) {
            if (*(int*)(actor + 0x20) != 2) {
                *(int*)(actor + 0x9d4) = *(int*)(actor + 0x20);
                *(int*)(actor + 0x20) = 2;
                *(float*)(actor + 0x9d8) = FLOAT_80618f98;
                *(int*)(actor + 0x9d0) = *(int*)(actor + 0x20);
            }
            aiactor_set_stance_flags(actorCtx, 4);
        }
        if ((FLOAT_80618ff4 < *(float*)(actor + 0xb30)) &&
            ((*(unsigned int*)(actor + 0x28) & 1) != 0)) {
            *(int*)(actor + 0xabc) = 0x800;
            *(int*)(actor + 0xab8) = 0;
            if ((*(int*)(actor + 0xaac) == 1) && (*(int*)(actor + 0xaa8) == 0)) {
                modeFlags = (DAT_805015bd == '\n') ? *(unsigned int*)(actor + 0x14)
                                                   : *(unsigned int*)(actor + 0x1c);
                if ((modeFlags & 1) == 0) {
                    return;
                }
            }
            *(int*)(actor + 0xaac) = 0x20;
            *(int*)(actor + 0xaa8) = 0;
            return;
        }
        if ((anim_active_slot_userflags(*(int*)(actorCtx + 4)) != 0) &&
            (anim_is_clip_active(actorCtx, 0x26) == 0)) {
            anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
        }
    } else if (state < 1) {
        if (-1 < state) {
            if (anim_is_clip_active(actorCtx, 0x26) == 0) {
                anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
            }
            *(int*)(actor + 0xad0) = *(int*)(actor + 0xb14);
            aiactor_set_stance_flags(actorCtx, 1);
            if (*(int*)(actor + 0x684) < 0) {
                zone = *(int*)(*(int*)(actor + 0xbf0) + 0x50);
                if (zone == 0) {
                    if (*(int*)(actor + 0x20) != 2) {
                        *(int*)(actor + 0x9d4) = *(int*)(actor + 0x20);
                        *(int*)(actor + 0x20) = 2;
                        *(float*)(actor + 0x9d8) = FLOAT_80618f98;
                        *(int*)(actor + 0x9d0) = *(int*)(actor + 0x20);
                    }
                } else {
                    zone = soundpad_find_zone_by_id(zone);
                    if (zone < 0) {
                        *(int*)(actor + 0x684) =
                            *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                    } else {
                        zone = soundpad_find_zone_by_id(*(int*)(*(int*)(actor + 0xbf0) + 0x50));
                        *(int*)(actor + 0x684) = zone;
                    }
                    *(int*)(actor + 0x9ac) = 0;
                    *(int*)(actor + 0x9a8) = 0;
                    if (*(int*)(actor + 0x20) != 1) {
                        *(int*)(actor + 0x9d4) = *(int*)(actor + 0x20);
                        *(int*)(actor + 0x20) = 1;
                        *(float*)(actor + 0x9d8) = FLOAT_80618f98;
                        *(int*)(actor + 0x9d0) = *(int*)(actor + 0x20);
                    }
                }
            } else if (*(int*)(actor + 0x20) != 1) {
                *(int*)(actor + 0x9d4) = *(int*)(actor + 0x20);
                *(int*)(actor + 0x20) = 1;
                *(float*)(actor + 0x9d8) = FLOAT_80618f98;
                *(int*)(actor + 0x9d0) = *(int*)(actor + 0x20);
            }
        }
    } else if (((state < 3) && (anim_is_clip_active(actorCtx, 0x25) == 0)) &&
               (anim_active_slot_userflags(*(int*)(actorCtx + 4)) != 0)) {
        anim_play_scaled((double)FLOAT_80618ff0, (double)FLOAT_80618fb8);
    }

    if ((double)FLOAT_80618ff8 < (double)*(float*)(actor + 0xb3c)) {
        aiactor_destroy_instance((double)*(float*)(actor + 0xb3c), param_2, param_3, param_4,
            param_5, param_6, param_7, param_8, actorCtx);
    }
}
