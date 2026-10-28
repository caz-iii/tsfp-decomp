// worm_column2.cpp
// CDH1 set-piece scripted-trigger + timeline layer (continuation of
// worm_column.cpp). Address range 0x801e68e8 - 0x801e788c (batch_618): more
// zone/actor payload gates, the 'SHUT' shutter master timeline driven by a
// scripted time phase, the big per-frame anim-event dispatcher (HTIP electro
// sparks/death, SAFE spawn, per-bot anim-event sound hooks), and the spark /
// break-off emitter helpers. subsystem:game, worm-effect module.
//
// Functions:
//   column_fbn_zone_gate       (0x801e68e8) payload gate: primary player zone
//                              0x5cd/0x5ce AND 'FBN2'/'FBNN' both absent.
//   column_shut_zone_gate      (0x801e6980) zone 0x558 gate: if the shutter is
//                              open fire the payload, else play anim 0x1f.
//   column_shut_timeline_update(0x801e69ec) SHUT master timeline: drive the
//                              shutter-panel material by a scripted time phase
//                              (DAT_80452be4 ramp table), close the GSD1/GSD2
//                              doors, and fire toggle / ekun-reset / HKDR-destroy
//                              / cutscene hooks at time thresholds.
//   column_anim_event_dispatch (0x801e6ce4) per-frame CDH1 anim-event dispatcher:
//                              HTIP electro-tip spark burst + death, SAFE spawn,
//                              and per-bot (OTH3/GUY2/GUY3/CZS1) anim-event sound
//                              slot hooks.
//   column_pete_anim_gate      (0x801e7360) 'PETE' anim gate: clips 0xbeb/0xbec
//                              set the fill, 0xbed/0xbee fire the payload + reset.
//   column_fire_payload        (0x801e73e0) unconditional payload fire.
//   column_shut_final_trigger  (0x801e7408) final shutter trigger: close GSD1/GSD2,
//                              fire payload, shutter toggle, ekun reset, destroy
//                              'HKDR', cutscene hook.
//   column_rng_spin_value      (0x801e753c) rng-scattered spin value around
//                              FLOAT_80617508.
//   column_spark_emitter_tick  (0x801e7584) spark/projectile emitter tick: count
//                              down 2 timers, spawn a weapon 0x26/0x29 projectile
//                              in a random direction with a random speed.
//   column_break_arm           (0x801e788c) break-off arm: on health threshold
//                              spawn the debris emitter + register the spark-tick
//                              and break update callbacks + set cull bit.
//
// Non-matching: FUN_8023c32c is entity-by-4CC lookup; aiactor_find_player_by_team
// / aiactor_find_bot_by_linked_entity resolve actors by 4CC. The PPC FP-context
// spill frames (FUN_803826e4 open + FUN_80382730 close) are dead ABI plumbing
// kept opaque; the int<->double magic-bias idiom is reduced to -(double)(int).
// Several named helpers whose shared prototype is void/wrong-arity
// (anim_active_clip_slot, anim_event_scan_active, spawn_create_instance,
// character_on_death_init, player_ai_projectile_spawn_simple) are routed through
// file-local typed fn-ptr aliases (__wc2_*).
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {

// --- raw data symbols (file-local, typed to use here) ---
extern int   DAT_80611d78;      // CDH1 level-state block base
extern int   DAT_80611b8c;      // zone-id table base
extern int   DAT_80611c24;      // trigger/entity slot pool base
extern int   DAT_80612610;      // active player-slot count
extern int   DAT_80612614;      // active game-mode / level id
extern int   DAT_806126d4;      // shutter "closed" latch
extern int   DAT_806126bc;      // timeline: shutter-toggle latch
extern int   DAT_806126c0;      // timeline: ekun-reset latch
extern int   DAT_80612690;      // SAFE spawned entity handle
extern int   DAT_80612720;      // deferred one-shot flag
extern unsigned int DAT_806109c0; // frame-count numerator (magic-bias src)
extern float FLOAT_806108ec;    // per-frame delta seconds
extern float DAT_805c5174;      // break debris emitter def

extern float FLOAT_80617428;    // 1.0f
extern float FLOAT_80617430;
extern float FLOAT_80617440;    // 0.0f
extern float FLOAT_80617468;
extern float FLOAT_8061742c;
extern float FLOAT_8061749c;
extern float FLOAT_806174bc;
extern float FLOAT_806174dc;
extern float FLOAT_80617508;    // spin ceiling / fill value
extern float FLOAT_806175b4;    // break health threshold
extern float FLOAT_806175f8;    // fill full value
extern float FLOAT_80617638;    // dir^2 gate
extern float FLOAT_80617644;    // HKDR anim divisor
extern float FLOAT_80617648;    // vertex dequant scale
extern float FLOAT_8061764c;    // HTIP death Y threshold
extern float FLOAT_8060faf8;    // timeline phase->frame scale
extern double DOUBLE_80617420;  // magic-bias subtrahend
extern double DOUBLE_80617438;  // rng bias subtrahend

extern int   DAT_803df844;   extern int DAT_803df848;  extern int DAT_803df84c;
extern int   DAT_803df850;   extern int DAT_803df854;
extern int   DAT_803df858;   extern int DAT_803df85c;  extern int DAT_803df860;
extern int   DAT_803df864;   extern int DAT_803df868;  extern int DAT_803df86c;
extern int   DAT_803df870;   extern int DAT_803df874;  extern int DAT_803df878;
extern int   DAT_80452be4;   extern int DAT_80452be8;  // SHUT phase ramp table (0xc stride)

// --- raw code symbols (file-local; not present in the generated headers) ---
extern int  *FUN_8023c32c(int tag);          // entity lookup by 4CC
extern int   FUN_8027ea98(void);             // primary local-player accessor
extern void  FUN_80221074(double a, double b, double c, double d,
                          double e, double f, double g, double h); // scripted payload
extern void  FUN_802212b4(int mode);         // payload variant
extern void  FUN_802213b4(int animId);       // anim trigger by id
extern void  FUN_80238fc8(int mode);         // mission/objective event
extern int   FUN_801eb154(int arg);          // deferred one-shot handler
extern int   FUN_803826e4(void);             // spill frame open (returns frame)
extern void  FUN_80382730(void);             // spill frame close

} // extern "C" (raw symbols)

// --- typed fn-ptr aliases for shared-header prototypes with wrong type/arity ---
// anim_active_clip_slot is declared `int anim_active_clip_slot(...)` (variadic);
// the ROM returns a pointer to the active clip slot (first word = clip id).
static int *(*const __wc2_clip_slot)(int) =
    (int *(*)(int))&anim_active_clip_slot;
// anim_event_scan_active is declared (int,u16) void; the ROM call is no-arg and
// returns nonzero when an anim event just fired.
static int (*const __wc2_anim_event_scan)(void) =
    (int (*)(void))&anim_event_scan_active;
// spawn_create_instance is declared (void); the real 18-arg form takes 7 doubles
// then ints/pointers and returns the new entity ptr.
static int (*const __wc2_spawn_create_instance)(unsigned long long, double, double,
                                                double, double, double, double, double,
                                                int, int, int, int, void *, int *,
                                                int, int, int, int) =
    (int (*)(unsigned long long, double, double, double, double, double, double,
             double, int, int, int, int, void *, int *, int, int, int, int))
    &spawn_create_instance;
// character_on_death_init is declared (int); the ROM passes 8 doubles + entity.
static void (*const __wc2_char_death_init)(double, double, double, double, double,
                                           double, double, double, int) =
    (void (*)(double, double, double, double, double, double, double, double, int))
    &character_on_death_init;
// player_ai_projectile_spawn_simple returns the new slot (declared void).
static int (*const __wc2_proj_spawn)(double, double, int, void *, int *, int, int *,
                                     int, int, int, int, int) =
    (int (*)(double, double, int, void *, int *, int, int *, int, int, int, int,
             int))&player_ai_projectile_spawn_simple;
// rng_counter_tick returns the fresh counter word.
static unsigned int (*const __wc2_rng_tick)(void) =
    (unsigned int (*)(void))&rng_counter_tick;

// forward decls (callbacks installed by column_break_arm)
double column_rng_spin_value(void);
void column_spark_emitter_tick(int self);

// ---------------------------------------------------------------------------
// 0x801e68e8  Payload gate: player zone 0x5cd/0x5ce AND 'FBN2'/'FBNN' absent.
void column_fbn_zone_gate(unsigned long long f, double a2, double a3, double a4,
                          double a5, double a6, double a7, double a8)
{
    int player = FUN_8027ea98();
    int fbn2 = aiactor_find_player_by_team(0x46424e32);  /* 'FBN2' */
    int fbnn = aiactor_find_player_by_team(0x46424e4e);  /* 'FBNN' */
    int zone = -1;
    (void)f;
    if (player != 0) zone = *(int *)(DAT_80611b8c + *(int *)(player + 0x678) * 0x20);
    if ((zone == 0x5cd || zone == 0x5ce) && fbn2 == 0 && fbnn == 0) {
        FUN_80221074((double)FLOAT_80617440, a2, a3, a4, a5, a6, a7, a8);
    }
}

// ---------------------------------------------------------------------------
// 0x801e6980  Zone 0x558 gate: fire payload if open, else play anim 0x1f.
void column_shut_zone_gate(unsigned long long f, double a2, double a3, double a4,
                           double a5, double a6, double a7, double a8)
{
    int player = FUN_8027ea98();
    int zone = -1;
    (void)f;
    if (player != 0) zone = *(int *)(DAT_80611b8c + *(int *)(player + 0x678) * 0x20);
    if (zone == 0x558) {
        if (DAT_806126d4 == 0) {
            FUN_80221074((double)FLOAT_80617440, a2, a3, a4, a5, a6, a7, a8);
        } else {
            FUN_802213b4(0x1f);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801e69ec  SHUT master timeline: drive panel by scripted time phase.
void column_shut_timeline_update(double phase)
{
    int shut;
    int ctrl;
    int tableOff;
    int steps;
    int t;

    if ((*(int *)(DAT_80611d78 + 0x14) != 0 && *(int *)(DAT_80611d78 + 0xdec) == 0x4c) ||
        (1 < DAT_80612610 && *(int *)(DAT_80611d78 + 0x1430) != 0 &&
         *(int *)(DAT_80611d78 + 0x2208) == 0x4c)) {
        shut = (int)FUN_8023c32c(0x53485554);  /* 'SHUT' */
        ctrl = *(int *)(shut + 0x7c);
        *(unsigned int *)(shut + 0x28) = *(unsigned int *)(shut + 0x28) | 0x4000000;
        t = (int)(phase * (double)FLOAT_8060faf8);

        /* 5-band ramp table: interpolate the panel material +0x10 by time band */
        tableOff = 0;
        steps = 5;
        do {
            int lo = *(int *)((int)&DAT_80452be4 + tableOff);
            int hi = *(int *)((int)&DAT_80452be8 + tableOff);
            if (t < lo || hi <= t) {
                if (hi <= t && t <= *(int *)(tableOff + -0x7fbad414)) {
                    *(float *)(ctrl + 0x10) = FLOAT_80617440;
                }
            } else {
                *(float *)(ctrl + 0x10) =
                    (float)(-(double)(int)(((hi - t) * 0x100) / (hi - lo)));
                *(int *)(ctrl + 0xa0) = 4;
            }
            tableOff += 0xc;
            steps += -1;
        } while (steps != 0);

        if (0x5c < t) {
            unsigned int *slot;
            int e;
            slot = (unsigned int *)trigger_slot_find_by_entity_id(0x47534431);  /* 'GSD1' */
            if (slot != 0) {
                if ((slot[3] & 2) == 0) {
                    unsigned int h = slot[0];
                    if (h == 0) e = 0;
                    else { e = DAT_80611c24 + (h >> 0x16) * 0x2e8;
                           if (*(unsigned int *)(e + 0x2c) != h) { e = 0; slot[0] = 0; } }
                    trigger_close_if_idle(e);
                }
                slot[3] = slot[3] | 2;
            }
            slot = (unsigned int *)trigger_slot_find_by_entity_id(0x47534432);  /* 'GSD2' */
            if (slot != 0) {
                if ((slot[3] & 2) == 0) {
                    unsigned int h = slot[0];
                    if (h == 0) e = 0;
                    else { e = DAT_80611c24 + (h >> 0x16) * 0x2e8;
                           if (*(unsigned int *)(e + 0x2c) != h) { e = 0; slot[0] = 0; } }
                    trigger_close_if_idle(e);
                }
                slot[3] = slot[3] | 2;
            }
        }

        if (t < 0xd84) { DAT_806126bc = 0; DAT_806126c0 = 0; }
        if (0x10ae < t && DAT_806126bc == 0) { shutter_use_toggle(); DAT_806126bc = 1; }
        if (0xda3 < t && DAT_806126c0 == 0) { worm_ekun_reset(); DAT_806126c0 = 1; }

        {
            int *hkdr = FUN_8023c32c(0x4b484452);  /* 'HKDR' */
            if (hkdr != 0) {
                if (t < 0x367 || 0x47d < t) {
                    if (0x47d < t) entity_destroy_with_event(hkdr);
                } else {
                    ((float *)hkdr)[0x1a] = (float)((int *)hkdr)[0x2b] +
                        (FLOAT_80617468 * (float)((int *)hkdr)[0x71] *
                         (float)(-(double)(int)(t - 0x367))) / FLOAT_80617644;
                }
            }
        }
        if (t == 0x10e5) FUN_80238fc8(4);
    }
}

// ---------------------------------------------------------------------------
// 0x801e6ce4  Per-frame CDH1 anim-event dispatcher.
void column_anim_event_dispatch(unsigned long long f, double a2, double a3, double a4,
                                double a5, double a6, double a7, double a8)
{
    int htip;
    int *clip;
    int cur, prev;
    int bot;
    int mtx[16];
    int spawnDefBlk[4];
    int soundParams[7];

    (void)f;
    FUN_803826e4();

    htip = aiactor_find_player_by_team(0x48544950);  /* 'HTIP' electro-tip */
    if (htip != 0) {
        clip = __wc2_clip_slot(htip);
        if (*clip == 0xbf8) {
            anim_get_frame_times((int)clip, &cur, &prev);
            if (cur < 0x203 || 0x241 < cur) {
                *(int *)(htip + 0x22c) = 0xffffffff;
                *(int *)(*(int *)(htip + 4) + 0x30) = 2;
            } else {
                *(int *)(*(int *)(htip + 4) + 0x30) = 1;
                if (*(int *)(htip + 0x22c) < 0) {
                    sound_init_play_params((int)soundParams);
                    soundParams[0] = 0x3a4;
                    soundParams[3] = *(int *)(htip + 100);
                    soundParams[4] = *(int *)(htip + 0x68);
                    soundParams[5] = *(int *)(htip + 0x6c);
                    *(unsigned int *)(htip + 0x22c) = sound_play_request(soundParams);
                }
                /* spawn up to 10 sparks along random tip verts */
                int rep = 0;
                do {
                    unsigned int n = 0;
                    unsigned int r = __wc2_rng_tick();
                    unsigned int *desc = *(unsigned int **)(*(int *)(htip + 4) + 4);
                    unsigned int cnt = desc[0];
                    int idx = r - (r / cnt) * cnt;
                    unsigned int vlist = desc[cnt * -0x30 + idx * 0x30 + 0x15];
                    for (; vlist != 0 && -1 < *(char *)(vlist + 3); vlist += 8) n = n + 1;
                    entity_build_local_matrix(mtx, (void *)(unsigned long long)(unsigned)htip, 1, 0);
                    if (n != 0) {
                        float pos[4];
                        r = __wc2_rng_tick();
                        int rec = desc[cnt * -0x30 + idx * 0x30 + 0x15] + (r - (r / n) * n) * 8;
                        pos[0] = *(float *)(htip + 100);
                        pos[1] = *(float *)(htip + 0x68);
                        pos[2] = *(float *)(htip + 0x6c);
                        pos[3] = *(float *)&DAT_803df844;
                        if (rec != 0 && *(char *)(rec + 2) != '\0') {
                            r = __wc2_rng_tick();
                            int rd = *(int *)(*(int *)(htip + 4) + 4);
                            int vi = r - (r / *(unsigned char *)(rec + 2)) * (unsigned int)*(unsigned char *)(rec + 2);
                            if ((*(unsigned int *)(rd + 0xc) & 0x10) == 0) {
                                float *pv = (float *)(desc[cnt * -0x30 + idx * 0x30 + 1] +
                                                      ((unsigned int)*(unsigned short *)(rec + 6) + vi) * 0xc);
                                pos[0] = pv[0]; pos[1] = pv[1]; pos[2] = pv[2]; pos[3] = pv[3];
                            } else {
                                short *pv = (short *)(desc[cnt * -0x30 + idx * 0x30 + 1] +
                                                      ((unsigned int)*(unsigned short *)(rec + 6) + vi) * 6);
                                float s = FLOAT_80617648;
                                pos[0] = (float)(s * (float)(-(double)(int)pv[0]) + *(float *)(rd + 0x2c));
                                pos[1] = (float)(s * (float)(-(double)(int)pv[1]) + *(float *)(rd + 0x30));
                                pos[2] = (float)(s * (float)(-(double)(int)pv[2]) + *(float *)(rd + 0x34));
                                pos[3] = (float)((int)pv[3] | 0x3f800000);
                            }
                            mtx4_transform_point((float *)(*(int *)(*(int *)(htip + 4) + 0x10) + idx * 0x40), pos);
                            mtx4_transform_point((float *)mtx, pos);
                        }
                        spark_spawn_fading();
                    }
                    rep = rep + 1;
                } while (rep < 10);
            }
        }
        if ((double)*(float *)(htip + 0x68) < (double)FLOAT_8061764c) {
            __wc2_char_death_init((double)*(float *)(htip + 0x68), a2, a3, a4, a5, a6, a7, a8, htip);
        }
    }

    /* SAFE: on anim event spawn the safe instance */
    {
        int safe = (int)FUN_8023c32c(0x53414645);  /* 'SAFE' */
        if (safe != 0) {
            clip = __wc2_clip_slot(safe);
            if (*clip == 0xbf5 && __wc2_anim_event_scan() != 0) {
                int *def;
                int inst;
                spawnDefBlk[0] = DAT_803df858;
                spawnDefBlk[1] = DAT_803df85c;
                spawnDefBlk[2] = DAT_803df860;
                def = spawn_def_get_entry(0);
                inst = __wc2_spawn_create_instance(0, a2, a3, a4, a5, a6, a7, a8, 0, 8, 5, 0,
                                                   spawnDefBlk, def, 0, 0, 0, 0);
                if (inst != 0) {
                    *(float *)(inst + 0x70) = FLOAT_80617440;
                    DAT_80612690 = inst;
                }
            }
        }
    }

    if (DAT_80612720 != 0 && FUN_801eb154(0) != 0) DAT_80612720 = 0;

    /* HTIP anim-event -> sound-slot hooks */
    if (htip != 0 && __wc2_anim_event_scan() != 0) {
        clip = __wc2_clip_slot(htip);
        if (*clip == 0xb9c) { int c = *(int *)(htip + 0x7c); *(char *)(c + 0x54e) = 0; *(int *)(c + 0x94) = 0; }
        if (*clip == 0xbb0) { int c = *(int *)(htip + 0x7c); *(char *)(c + 0x54e) = 0; *(int *)(c + 0x94) = 0; }
        if (*clip == 0xb9d) {
            int c = *(int *)(htip + 0x7c);
            *(int *)(c + 0x94) = 2; *(char *)(c + 0x54c) = 0;
            object_sound_slot_accumulate(c, 1, 1000);
        }
    }

    /* per-bot anim-event sound hooks */
    bot = aiactor_find_bot_by_linked_entity(0x4f544833);  /* 'OTH3' */
    if (bot != 0) {
        if (__wc2_anim_event_scan() != 0 && *__wc2_clip_slot(bot) == 0xbe0) {
            int c = *(int *)(bot + 0x7c); *(int *)(c + 0x94) = 0; *(char *)(c + 0x562) = 0;
        }
        if (__wc2_anim_event_scan() != 0 && *__wc2_clip_slot(bot) == 0xbe0) {
            int c = *(int *)(bot + 0x7c);
            *(int *)(c + 0x94) = 0x16; *(char *)(c + 0x54c) = 0;
            object_sound_slot_accumulate(c, 8, 1000);
        }
    }
    bot = aiactor_find_bot_by_linked_entity(0x47555932);  /* 'GUY2' */
    if (bot != 0) {
        if (__wc2_anim_event_scan() != 0 && *__wc2_clip_slot(bot) == 0xbd9) {
            int c = *(int *)(bot + 0x7c); *(int *)(c + 0x94) = 0; *(char *)(c + 0x57f) = 0;
        }
        if (__wc2_anim_event_scan() != 0 && *__wc2_clip_slot(bot) == 0xbd9) {
            int c = *(int *)(bot + 0x7c);
            *(int *)(c + 0x94) = 0x33; *(char *)(c + 0x54c) = 0;
            object_sound_slot_accumulate(c, 0xd, 1000);
        }
    }
    bot = aiactor_find_bot_by_linked_entity(0x47555933);  /* 'GUY3' */
    if (bot != 0) {
        if (__wc2_anim_event_scan() != 0 && *__wc2_clip_slot(bot) == 0xbda) {
            int c = *(int *)(bot + 0x7c); *(int *)(c + 0x94) = 0; *(char *)(c + 0x57f) = 0;
        }
        if (__wc2_anim_event_scan() != 0 && *__wc2_clip_slot(bot) == 0xbda) {
            int c = *(int *)(bot + 0x7c);
            *(int *)(c + 0x94) = 0x33; *(char *)(c + 0x54c) = 0;
            object_sound_slot_accumulate(c, 0xd, 1000);
        }
    }
    bot = aiactor_find_bot_by_linked_entity(0x435a5331);  /* 'CZS1' */
    if (bot != 0 && __wc2_anim_event_scan() != 0 && *__wc2_clip_slot(bot) == 0xbd7) {
        int c = *(int *)(bot + 0x7c);
        *(int *)(c + 0x94) = 0x1d; *(char *)(c + 0x54c) = 0;
        object_sound_slot_accumulate(c, 0x14, 1000);
    }

    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x801e7360  'PETE' anim gate: set fill on 0xbeb/0xbec, fire payload 0xbed/0xbee.
void column_pete_anim_gate(unsigned long long f, double a2, double a3, double a4,
                           double a5, double a6, double a7, double a8)
{
    int pete;
    int *clip;
    (void)f;
    pete = aiactor_find_player_by_team(0x50455445);  /* 'PETE' */
    if (pete != 0) {
        clip = __wc2_clip_slot(pete);
        if (*clip == 0xbeb || *clip == 0xbec) {
            *(float *)(pete + 0x1cc) = FLOAT_806175f8;
        }
        if (*clip == 0xbed || *clip == 0xbee) {
            FUN_80221074((double)FLOAT_80617440, a2, a3, a4, a5, a6, a7, a8);
            *(float *)(pete + 0x1cc) = FLOAT_80617508;
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801e73e0  Unconditional payload fire.
void column_fire_payload(unsigned long long f, double a2, double a3, double a4,
                         double a5, double a6, double a7, double a8)
{
    (void)f;
    FUN_80221074((double)FLOAT_80617440, a2, a3, a4, a5, a6, a7, a8);
}

// ---------------------------------------------------------------------------
// 0x801e7408  Final shutter trigger: close doors + payload + toggle + destroy.
void column_shut_final_trigger(unsigned long long f, double a2, double a3, double a4,
                               double a5, double a6, double a7, double a8)
{
    unsigned int *slot;
    int e;
    int *hkdr;
    (void)f;

    slot = (unsigned int *)trigger_slot_find_by_entity_id(0x47534431);  /* 'GSD1' */
    if (slot != 0) {
        if ((slot[3] & 2) == 0) {
            unsigned int h = slot[0];
            if (h == 0) e = 0;
            else { e = DAT_80611c24 + (h >> 0x16) * 0x2e8;
                   if (*(unsigned int *)(e + 0x2c) != h) { e = 0; slot[0] = 0; } }
            trigger_close_if_idle(e);
        }
        slot[3] = slot[3] | 2;
    }
    slot = (unsigned int *)trigger_slot_find_by_entity_id(0x47534432);  /* 'GSD2' */
    if (slot != 0) {
        if ((slot[3] & 2) == 0) {
            unsigned int h = slot[0];
            if (h == 0) e = 0;
            else { e = DAT_80611c24 + (h >> 0x16) * 0x2e8;
                   if (*(unsigned int *)(e + 0x2c) != h) { e = 0; slot[0] = 0; } }
            trigger_close_if_idle(e);
        }
        slot[3] = slot[3] | 2;
    }
    FUN_80221074((double)FLOAT_80617440, a2, a3, a4, a5, a6, a7, a8);
    FUN_802212b4(2);
    shutter_use_toggle();
    worm_ekun_reset();
    hkdr = FUN_8023c32c(0x4b484452);  /* 'HKDR' */
    if (hkdr != 0) entity_destroy_with_event(hkdr);
    FUN_80238fc8(4);
}

// ---------------------------------------------------------------------------
// 0x801e753c  Rng-scattered spin value around FLOAT_80617508.
double column_rng_spin_value(void)
{
    unsigned int r = __wc2_rng_tick();
    return (double)(FLOAT_80617508 *
                    ((float)((double)r - DOUBLE_80617438) / FLOAT_8061742c) + FLOAT_80617508);
}

// ---------------------------------------------------------------------------
// 0x801e7584  Spark/projectile emitter tick.
void column_spark_emitter_tick(int self)
{
    float invFrame;
    int slot;
    float dir[3];
    float pos[3];

    invFrame = FLOAT_80617428 / (float)(-(double)(int)DAT_806109c0);
    *(float *)(self + 0x1a4) = -(FLOAT_806108ec * invFrame - *(float *)(self + 0x1a4));
    invFrame = FLOAT_80617428 / (float)(-(double)(int)DAT_806109c0);
    *(float *)(self + 0x1dc) = -(FLOAT_806108ec * invFrame - *(float *)(self + 0x1dc));

    if (*(float *)(self + 0x1a4) <= FLOAT_80617440) {
        unsigned int r;
        float mag2;
        r = __wc2_rng_tick();
        dir[0] = FLOAT_80617428;
        dir[1] = (float)((double)r - DOUBLE_80617438) / FLOAT_8061742c - FLOAT_80617430;
        if (DAT_80612614 == 4) {
            r = __wc2_rng_tick();
            dir[2] = FLOAT_80617430 +
                     ((float)((double)r - DOUBLE_80617438) / FLOAT_8061742c - FLOAT_80617430);
        } else {
            r = __wc2_rng_tick();
            dir[2] = (float)((double)r - DOUBLE_80617438) / FLOAT_8061742c - FLOAT_80617430;
        }
        mag2 = dir[2] * dir[2] + dir[1] * dir[1] + dir[0] * dir[0];
        if (FLOAT_80617638 < mag2) {
            float inv = 1.0f / (float)SQRT(mag2);
            inv = inv * -(FLOAT_80617430 * mag2 * inv * inv - FLOAT_8061749c);
            float g = FLOAT_80617428 * inv * -(FLOAT_80617430 * mag2 * inv * inv - FLOAT_8061749c);
            dir[1] = dir[1] * g; dir[0] = dir[0] * g; dir[2] = dir[2] * g;
        }
        pos[0] = *(float *)(self + 100) + *(float *)(self + 0x1a8);
        pos[1] = *(float *)(self + 0x68) + *(float *)(self + 0x1ac);
        pos[2] = *(float *)(self + 0x6c) + *(float *)(self + 0x1b0);
        *(float *)(self + 0x1a4) = (float)column_rng_spin_value();
        if (DAT_80612614 == 4) {
            slot = __wc2_proj_spawn((double)FLOAT_80617508, (double)FLOAT_80617440, 0x29, pos,
                                    (int *)dir, self, (int *)0, 6, 0, *(int *)(self + 0x34), 0, -1);
        } else {
            slot = __wc2_proj_spawn((double)FLOAT_80617508, (double)FLOAT_80617440, 0x26, pos,
                                    (int *)dir, self, (int *)0, 6, 0, *(int *)(self + 0x34), 0, -1);
        }
        *(int *)(slot + 4) = 1;
        r = __wc2_rng_tick();
        player_ai_projectile_set_speed(
            (double)(FLOAT_80617468 +
                     ((float)((double)r - DOUBLE_80617438) / FLOAT_8061742c - FLOAT_80617430)),
            slot);
    }
    if (*(float *)(self + 0x1dc) <= FLOAT_80617440) {
        entity_register_for_update(self, 3, 0);
    }
}

// ---------------------------------------------------------------------------
// 0x801e788c  Break-off arm: spawn debris emitter + register update callbacks.
void column_break_arm(int *self)
{
    int entity;
    int emitter;
    int cached;
    float pos[3];
    float dir[3];
    int target[3];

    entity = *self;
    if (FLOAT_806175b4 < *(float *)(entity + 0x1cc) &&
        (*(unsigned int *)(entity + 0x28) & 0x4000000) == 0) {
        target[0] = DAT_803df864;
        target[1] = DAT_803df868;
        target[2] = DAT_803df86c;
        dir[0] = *(float *)&DAT_803df870;
        dir[1] = *(float *)&DAT_803df874;
        dir[2] = *(float *)&DAT_803df878;
        pos[0] = *(float *)(entity + 100) + *(float *)(entity + 0x1a8);
        pos[1] = *(float *)(entity + 0x68) + *(float *)(entity + 0x1ac);
        pos[2] = *(float *)(entity + 0x6c) + *(float *)(entity + 0x1b0);
        *(float *)(entity + 0x1a4) = FLOAT_806174dc;
        *(float *)(entity + 0x1dc) = FLOAT_806174bc + *(float *)(entity + 0x1a4);
        *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x4000000;
        entity_register_for_update(entity, 3, (int)(unsigned long long)&column_spark_emitter_tick);
        entity_register_for_update(entity, 5, (int)(unsigned long long)&column_break_arm);
        *(unsigned int *)(*(int *)(entity + 4) + 0x30) =
            (*(unsigned int *)(*(int *)(entity + 4) + 0x30) & 0xfffffffd) | 4;
        particle_emitter_spawn((unsigned int)(unsigned long long)&emitter,
                               (unsigned int)(unsigned long long)&DAT_805c5174, 0x1000, pos, dir);
        if (DAT_80612614 == 0xb) {
            cached = emitter;
            particle_cached_set_target((ParticleEmitter **)&cached, (Vec3 *)target);
        }
        cached = emitter;
        particle_cached_enable((ParticleEmitter **)&cached);
    }
}
