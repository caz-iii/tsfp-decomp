// worm_ghz_object.cpp
// GHZ reactor set-piece: entity-object class dispatcher plus its render/tick
// callbacks. This is the sibling driver to worm_ghz_state.cpp (effect
// primitives), worm_ghz_level.cpp (level driver) and worm_ghz_arc.cpp (four-
// barrel electric-arc rig). The master init here (ghz_reactor_object_init)
// routes every GHZ entity class 0x542..0x55f to its behaviour, wiring the
// arc-rig class 0x556 into ghz_arc_rig_init/tick from batch_658.
//
// Batch: batch_659  (addresses 0x80216400 .. 0x80216f80)

#include "common.h"

// ---------------------------------------------------------------------------
// Named cross-module helpers (resolved through shared headers)
// ---------------------------------------------------------------------------
extern void  ghz_arc_rig_init(double, double, double, double, double, double,
                              double, double);
extern int   player_ai_explosion_spawn(int type, int kind, float *pos,
                                        float *dir, int a, int b, int c);
extern void  entity_destroy_with_event(void *ent);
extern void  entity_set_render_type(int ent, void *cb);
extern int   entity_model_cache_alloc(double, double, double, double, double,
                                       double, double, double, int ent,
                                       int a, int size, int, int, int, int, int);
extern void  entity_register_for_update(int ent, int slot, int cb);
extern void  subobj_fracture_spawn(double, double, double, double, double,
                                    double, double, double, void *ent,
                                    unsigned int mask, int, int, void *pos,
                                    void *dir, int, int, void *);
extern void  damage_dispatch_to_entity(double, double, double, double, double,
                                        double, double, double, int *ctx);
extern void  match_process_end(long long, double, double, double, double,
                               double, double, double, int);
extern int   subobj_is_carried_by(int ent);
extern int   scene_raycast(int scene, int origin, float *dir, int, float *,
                           float *, int *, int, int, int *);
extern int   screen_effect_alloc_slot(void);
extern void  screen_effect_init_slot(int slot, int ent);
extern void *subobj_grab_attach_point(int *ent);
extern int   rng_counter_tick(void);
extern void  weapon_impact_smoke_dispatch(void);
extern void  particle_entity_disable_all(void);
extern void  particle_emitter_set_flags(int *emitter, int flags, int a);
extern void  particle_cached_enable(int *handle);
extern void  particle_spawn_all_at_once(void);
extern void  memset_wrapper(int dst, int val, int n);
extern void  leaf_31e8(unsigned int dst, unsigned int src, int n);

// ---------------------------------------------------------------------------
// File-local externs for raw ROM symbols (address-bound; per Compile-Clean
// Standard these are declared locally because the same address is invoked with
// differing signatures across files).
// ---------------------------------------------------------------------------
extern int  FUN_80220f30(int mode);                 // reactor-arm gate query
extern void FUN_80221074(double, double, double, double, double, double,
                         double, double);            // reactor sequence kickoff
extern int  FUN_8023c32c(unsigned int tag);          // find entity by 4cc tag
extern int  FUN_80241370(int a, int ent, int b, int c); // link two subobjects
extern int  FUN_803826e4(void);                      // current-entity accessor
extern void FUN_80382730(void);                      // current-entity release
extern int  FUN_8023c59c(int tag, int out, int stride); // gather entities by tag
extern int  FUN_8021653c();                          // fracture render cb (below)
extern int  FUN_802166e8();                          // meltdown render cb (below)
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern void LAB_80213c1c(void);                      // arc sub-emitter render cb

// Raw data / constant symbols
extern unsigned char DAT_805015bd;
extern int           DAT_80612610;
extern int           DAT_80612838;
extern int           DAT_8061282c;
extern int           DAT_80612830;
extern int           DAT_80611934;
extern int           DAT_805015c4;
extern int           DAT_80611d78;
extern int           DAT_80611e2c;
extern int           DAT_8045a180[4];
extern int           DAT_8045a208[4];
extern int           s_TNK2TNK3BLK3BLK4_8045a218[4];
extern float         FLOAT_80618090;   // 0.0f
extern float         FLOAT_80618098;   // 0.0f
extern float         FLOAT_8061809c;
extern float         FLOAT_806180a0;
extern float         FLOAT_806180a4;
extern float         FLOAT_806181bc;
extern float         FLOAT_806181c0;
extern float         FLOAT_806181c4;
extern float         FLOAT_806181c8;
extern float         FLOAT_806181a4;
extern float         FLOAT_80618130;
extern float         FLOAT_80618098;
extern double        DOUBLE_806180d0;
extern double        DOUBLE_806180d8;
extern float         DAT_806108ec;
extern float         DAT_806109c0;
extern float         DAT_803dff4c;
extern float         DAT_803dff50;
extern float         DAT_803dff54;
extern float         DAT_803dff58;
extern float         DAT_803dff5c;
extern float         DAT_803dff60;

// ---------------------------------------------------------------------------
// 0x80216400  ghz_reactor_trigger_tick
// Proximity/arm-gate trigger. When the reactor is not yet latched
// (DAT_80612838==0) and either the story flag advanced or the arm gate query
// (FUN_80220f30(7)) returns true, latch it, cache the owning object handle and
// kick off the reactor sequence.
// ---------------------------------------------------------------------------
void ghz_reactor_trigger_tick(int ent, double p2, double p3, double p4,
                              double p5, double p6, double p7, double p8,
                              int a9, int owner)
{
    unsigned int armed;

    if ((DAT_80612838 == 0) &&
        (((DAT_805015bd == '\n' && (1 < DAT_80612610)) ||
          (armed = (unsigned int)FUN_80220f30(7), armed != 0)))) {
        DAT_80612830 = *(int *)(owner + 0x150);
        DAT_80612838 = 1;
        FUN_80221074((double)FLOAT_80618098, p2, p3, p4, p5, p6, p7, p8);
    }
    (void)ent;
    (void)a9;
}

// ---------------------------------------------------------------------------
// 0x80216480  ghz_reactor_fuse_tick
// Fuse/timer render callback. Decays a per-frame timer at ent[0x69]; once it
// drops below zero, spawns a type-0x13 explosion at the entity origin and
// destroys the entity.
// ---------------------------------------------------------------------------
void ghz_reactor_fuse_tick(unsigned int *ent)
{
    float pos[3];
    unsigned int frame;

    frame = (unsigned int)DAT_806109c0 ^ 0x80000000u;
    ent[0x69] = (unsigned int)(int)-(
        (FLOAT_80618090 /
         (float)((double)(long long)(0x43300000ULL << 32 | frame) - DOUBLE_806180d0)) *
            DAT_806108ec -
        *(float *)&ent[0x69]);

    if (*(float *)&ent[0x69] < FLOAT_80618098) {
        pos[0] = DAT_803dff4c;
        pos[1] = DAT_803dff50;
        pos[2] = DAT_803dff54;
        player_ai_explosion_spawn(0x13, 0x17, (float *)(ent + 0x19), pos, 0, 0, 1);
        entity_destroy_with_event(ent);
    }
}

// ---------------------------------------------------------------------------
// 0x8021653c  ghz_reactor_fracture_tick
// Render callback for a shatterable reactor sub-part. Counts a per-frame timer
// at obj[0x73] down to zero, then emits a directional fracture burst
// (subobj_fracture_spawn) and destroys the object. Origin and impulse
// direction may be overridden through the callback context (ctx[10]/[0xb]/[0xc]).
// ---------------------------------------------------------------------------
void ghz_reactor_fracture_tick(long long p1, double p2, double p3, double p4,
                               double p5, double p6, double p7, long long p8,
                               unsigned int *ctx)
{
    unsigned int *obj;
    float origin[3];   // local_14, local_10, local_c
    float impulse[3];  // local_20, local_1c, local_18

    obj = (unsigned int *)*ctx;
    obj[0x73] = (unsigned int)(int)(*(float *)&obj[0x73] - *(float *)&ctx[8]);
    if (*(float *)&obj[0x73] <= FLOAT_80618098) {
        *(float *)&obj[0x73] = FLOAT_80618098;

        if (ctx[10] == 0) {
            origin[0] = *(float *)&obj[0x19] + *(float *)&obj[0x6a];
            origin[1] = *(float *)&obj[0x1a] + *(float *)&obj[0x6b];
            origin[2] = *(float *)&obj[0x1b] + *(float *)&obj[0x6c];
        } else {
            origin[0] = *(float *)(ctx[10]);
            origin[1] = *(float *)(ctx[10] + 4);
            origin[2] = *(float *)(ctx[10] + 8);
        }

        if (ctx[0xb] == 0) {
            if (ctx[0xc] == 0) {
                impulse[0] = FLOAT_80618098;
                impulse[1] = FLOAT_80618090;
                impulse[2] = FLOAT_80618098;
            } else {
                impulse[0] = *(float *)(ctx[0xc]);
                impulse[1] = *(float *)(ctx[0xc] + 4);
                impulse[2] = *(float *)(ctx[0xc] + 8);
            }
        } else {
            impulse[0] = -*(float *)(ctx[0xb]);
            impulse[1] = -*(float *)(ctx[0xb] + 4);
            impulse[2] = -*(float *)(ctx[0xb] + 8);
        }

        {
            int *link = *(int **)(obj[1] + 4);
            unsigned int mask =
                (1u << *link) - 1u & ~(*(unsigned int *)(obj[1] + 0x30) | 0x1e);
            subobj_fracture_spawn(
                (double)(FLOAT_806181bc * *(float *)&ctx[8]), p2, p3, p4, p5, p6,
                p7, p8, obj, mask, 1, 0, origin, impulse, 1, 8, (void *)0);
        }
        entity_destroy_with_event(obj);
    }
}

// ---------------------------------------------------------------------------
// 0x802166e8  ghz_reactor_meltdown_tick
// Meltdown countdown render callback. While the target object is absent or
// inert, drains the meltdown timer at obj+0x1cc. When it expires (and the
// meltdown latch is clear but the reactor is armed), triggers the big central
// explosion; in game-mode 7 sets the objective bit and ends the match.
// ---------------------------------------------------------------------------
void ghz_reactor_meltdown_tick(long long p1, double p2, long long p3, long long p4,
                               long long p5, long long p6, long long p7,
                               long long p8, int *ctx)
{
    long long boom;
    float pos[3];        // local_c, local_8, local_4
    float dir[3];        // local_18, local_14, local_10

    if ((ctx[1] == 0) || (*(int *)(ctx[1] + 0x150) == 0)) {
        p2 = (double)*(float *)(*ctx + 0x1cc);
        *(float *)(*ctx + 0x1cc) =
            (float)(p2 - (double)(*(float *)&ctx[8] / FLOAT_806181c0));
    }

    if ((*(float *)(*ctx + 0x1cc) < FLOAT_80618098) && (DAT_8061282c == 0) &&
        (DAT_80612838 != 0)) {
        dir[0] = DAT_803dff58;
        dir[1] = DAT_803dff5c;
        dir[2] = DAT_803dff60;
        DAT_8061282c = 1;
        pos[0] = *(float *)(*ctx + 100);
        pos[2] = *(float *)(*ctx + 0x6c);
        pos[1] = *(float *)(*ctx + 0x68) + FLOAT_806181c4;
        boom = player_ai_explosion_spawn(0x12, 0x17, pos, dir, 0, 0, 1);
        if (DAT_80611934 == 7) {
            DAT_805015c4 = DAT_805015c4 | 0x10000;
            match_process_end(boom, p2, p3, p4, p5, p6, p7, p8, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80216810  ghz_reactor_meltdown_event
// Entity event handler: on event 6, install the meltdown render callback.
// ---------------------------------------------------------------------------
int ghz_reactor_meltdown_event(int ent, int event)
{
    if (event == 6) {
        entity_set_render_type(ent, (void *)FUN_802166e8);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80216854  ghz_reactor_screen_effect_event
// Entity event handler for the full-screen reactor effect. Events 5/6 allocate
// and bind a screen-effect slot and publish the owning entity to the global HUD
// pointer (DAT_80611d78+0xe8c); events 3/5 clear it.
// ---------------------------------------------------------------------------
int ghz_reactor_screen_effect_event(int ent, int event)
{
    int slot;

    if (event != 5) {
        if (4 < event) {
            if (6 < event) {
                return 0;
            }
            slot = screen_effect_alloc_slot();
            *(int *)(ent + 0x268) = slot;
            screen_effect_init_slot(*(int *)(ent + 0x268), ent);
            *(int *)(DAT_80611d78 + 0xe8c) = ent;
            return 0;
        }
        if (event != 3) {
            return 0;
        }
    }
    *(int *)(DAT_80611d78 + 0xe8c) = 0;
    return 0;
}

// ---------------------------------------------------------------------------
// 0x802168dc  ghz_reactor_arc_damage_tick
// Per-contact callback for the arc body (class 0x557). When a valid carried
// class-0x557 body collides (flag bit0), the reactor is being held and the
// story gate is open, scales the collision damage by FLOAT_80618130 and
// forwards it to the touched entity.
// ---------------------------------------------------------------------------
void ghz_reactor_arc_damage_tick(long long p1, double p2, double p3, long long p4,
                                 long long p5, long long p6, long long p7,
                                 long long p8, int self, unsigned int hit)
{
    int  carried;
    double dmg;
    // Copied contact record: leaf_31e8 fills 0x5c bytes from the hit entity.
    // The damage scalar lives at ctx+0x20 (local_48 in the original frame).
    int    ctx[0x18];
    float *scaled = (float *)&ctx[8];   // local_48 == ctx + 0x20

    if ((*(int *)(hit + 4) != 0) &&
        (*(short *)(*(int *)(hit + 4) + 0x94) == 0x557) &&
        ((*(unsigned int *)(*(int *)(self + 0x7c) + 0x58) & 1) != 0) &&
        ((carried = subobj_is_carried_by(DAT_80611d78), carried != 0) &&
         ((DAT_805015bd != '\n') || (DAT_80612610 < 2))) &&
        (*(int *)(DAT_80611d78 + 0xe8c) != 0)) {
        leaf_31e8((unsigned int)ctx, hit, 0x5c);
        ctx[0] = *(int *)(DAT_80611d78 + 0xe8c);
        dmg = (double)*scaled;
        *scaled = (float)(dmg * (double)FLOAT_80618130);
        damage_dispatch_to_entity(dmg, p2, p3, p4, p5, p6, p7, p8, ctx);
    }
}

// ---------------------------------------------------------------------------
// 0x802169ac  ghz_reactor_object_init
// Master class dispatcher for every GHZ reactor entity (0x542..0x55f). Each
// case configures the entity's render/collision flags, allocates model-cache
// state and registers the appropriate per-frame update callbacks. Notable:
//   0x556 - arc-rig parent: allocate 0xd18 state, wire ghz_arc_rig_init and the
//           arc render/tick/aim callbacks, seed randomised anim phase.
//   0x557/0x558 - arc sub-emitters (only when spawn flag == 0x8000000).
//   0x55a - portal blast: disable all particles then re-enable the 4 cached
//           reactor emitters.
//   0x55b - shatter part: install ghz_reactor_fracture_tick render cb.
// ---------------------------------------------------------------------------
void ghz_reactor_object_init(double p1, double p2, double p3, double p4,
                             double p5, double p6, double p7, double p8,
                             int ent, int a10, int a11, int a12, int a13,
                             int a14, int a15, int a16)
{
    float phase;
    int   sub;
    int   i;
    int  *slot;
    int   seed;
    int   handle;   // local_1c
    int   emitter;  // local_18
    int   enable;   // local_20

    phase = FLOAT_806181c8;
    switch (*(unsigned short *)(ent + 0x94)) {
    case 0x542:
    case 0x543:
    case 0x545:
    case 0x546:
    case 0x547:
        *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x40000000;
        *(int *)(ent + 0x34) = 0xd5;
        break;

    case 0x54a:
        entity_set_render_type(ent, (void *)weapon_impact_smoke_dispatch);
        break;

    case 0x556:
        sub = entity_model_cache_alloc(p1, p2, p3, p4, p5, p6, p7, p8, ent, a10,
                                       0xd18, a12, a13, a14, a15, a16);
        *(int *)(ent + 0x7c) = sub;
        memset_wrapper(*(int *)(ent + 0x7c), 0, 0xd18);
        *(unsigned char *)(ent + 0x30) = 1;
        *(int *)(ent + 0x38) = 0xd5;
        *(int *)(ent + 0x34) = 0xd5;
        *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x40020000;
        ghz_arc_rig_init(p1, p2, p3, p4, p5, p6, p7, p8);
        seed = rng_counter_tick();
        *(float *)(ent + 0xd0) =
            FLOAT_806180a0 *
            ((float)((double)(long long)(0x43300000ULL << 32 | (unsigned int)seed) -
                     DOUBLE_806180d8) /
             FLOAT_806180a4);
        entity_register_for_update(ent, 3, (int)0x802135d8);   // ghz_arc_rig_tick
        entity_register_for_update(ent, 0xc, (int)0x802134d4);
        entity_register_for_update(ent, 10, (int)0x802134a8);
        break;

    case 0x557:
    case 0x558:
        if (*(int *)(ent + 0x20) == 0x8000000) {
            *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x10;
            *(unsigned char *)(ent + 0x30) = 1;
            *(int *)(ent + 0x38) = 0xd5;
            *(int *)(ent + 0x34) = 0xd5;
            *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x40020000;
            *(unsigned char *)(ent + 0x32) = 2;
            entity_register_for_update(ent, 3, (int)0x80213ccc);
            entity_set_render_type(ent, (void *)LAB_80213c1c);
            *(float *)(ent + 0x1cc) = FLOAT_80618090;
        }
        break;

    case 0x559:
        sub = entity_model_cache_alloc(p1, p2, p3, p4, p5, p6, p7, p8, ent, a10,
                                       0x20, a12, a13, a14, a15, a16);
        *(int *)(ent + 0x7c) = sub;
        *(unsigned char *)(ent + 0x30) = 1;
        *(int *)(ent + 0x38) = 0xd5;
        *(int *)(ent + 0x34) = 0xd5;
        *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x40020000;
        entity_register_for_update(ent, 3, (int)0x802161b4);   // ghz_arc_beam_aim_tick
        entity_register_for_update(ent, 10, (int)0x802163d8);
        break;

    case 0x55a:
        particle_entity_disable_all();
        for (i = 0; i < 4; i = i + 1) {
            if (DAT_8045a180[i] < *(int *)(ent + 0x1e8)) {
                slot = (int *)(*(int *)(ent + 0x1fc) + DAT_8045a180[i] * 4);
            } else {
                slot = (int *)&DAT_80611e2c;
            }
            handle = *slot;
            emitter = handle;
            particle_emitter_set_flags(&handle, 0x21000, 0);
            enable = emitter;
            particle_cached_enable(&enable);
            particle_spawn_all_at_once();
        }
        break;

    case 0x55b:
        *(float *)(ent + 0x1cc) = FLOAT_806181c8;
        *(float *)(ent + 0x1d8) = phase;
        *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x10;
        entity_set_render_type(ent, (void *)FUN_8021653c);   // ghz_reactor_fracture_tick
        break;

    case 0x55d:
        *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 4;
        entity_register_for_update(ent, 0, (int)0x80216400); // ghz_reactor_trigger_tick
        break;

    case 0x55f:
        *(float *)(ent + 0x1a4) = FLOAT_806181a4;
        entity_register_for_update(ent, 3, (int)0x80216480); // ghz_reactor_fuse_tick
        break;
    }
    (void)a11;
}

// ---------------------------------------------------------------------------
// 0x80216d34  ghz_reactor_nearest_target_scan
// Scans the four reactor spark-source entity tables (DAT_8045a208[]) for the
// nearest candidate that is not a 0x556/0x559 arc part and is visible via a
// line-of-sight raycast from the current entity. Clears then restores each
// candidate's cull bits (0x60) around the raycast so it doesn't self-occlude.
// ---------------------------------------------------------------------------
void ghz_reactor_nearest_target_scan(void)
{
    unsigned int cull;
    int   self;
    int   t;
    int   count;
    int   i;
    int   los;
    double best;
    double d2;
    float dx, dy, dz;   // local_a0, local_9c, local_98
    int   list[31];     // local_94

    self = FUN_803826e4();
    t = *(int *)(self + 0x7c);
    if ((*(int *)(t + 4) != 0) && (*(int *)(*(int *)(t + 4) + 0x150) != 0)) {
        *(int *)(t + 4) = 0;
    }

    best = (double)FLOAT_8061809c;
    for (t = 0; t < 4; t = t + 1) {
        count = FUN_8023c59c(DAT_8045a208[t], (int)list, 0x14);
        for (i = 0; i < count; i = i + 1) {
            if ((*(short *)(list[i] + 0x94) != 0x559) &&
                (*(short *)(list[i] + 0x94) != 0x556) &&
                ((*(unsigned int *)(list[i] + 0x28) & 0x80000000) == 0)) {
                dx = *(float *)(list[i] + 100) - *(float *)(self + 100);
                dy = *(float *)(list[i] + 0x68) - *(float *)(self + 0x68);
                dz = *(float *)(list[i] + 0x6c) - *(float *)(self + 0x6c);
                d2 = (double)(dz * dz + dx * dx + dy * dy);
                if (d2 < best) {
                    cull = *(unsigned int *)(list[i] + 0x28) & 0x60;
                    *(unsigned int *)(list[i] + 0x28) =
                        *(unsigned int *)(list[i] + 0x28) & ~cull;
                    los = scene_raycast(*(int *)(self + 0x34), self + 100, &dx, 0,
                                        (float *)0, (float *)0, (int *)0,
                                        0x2410278, 0, (int *)0);
                    if (los == 0) {
                        best = d2;
                    }
                    *(unsigned int *)(list[i] + 0x28) =
                        *(unsigned int *)(list[i] + 0x28) | cull;
                }
            }
        }
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x80216f80  ghz_reactor_tank_link_tick
// Links the reactor to the first available of the four TNK2/TNK3/BLK3/BLK4
// tank props (tag table s_TNK2TNK3BLK3BLK4_8045a218). For each tag it looks up
// the prop, skips ones whose meltdown timer (+0x1cc) is spent, then attempts
// the subobject link (FUN_80241370); stops at the first success.
// ---------------------------------------------------------------------------
void ghz_reactor_tank_link_tick(void)
{
    int          self;
    unsigned int *attach;
    int          prop;
    int         *ent;
    int          i;

    self = __save_gpr_28();
    ent = *(int **)(self + 0x7c);
    attach = (unsigned int *)subobj_grab_attach_point(ent);
    ent[1] = 0;
    i = 0;
    while ((i < 4) &&
           (((prop = FUN_8023c32c(*(unsigned int *)(s_TNK2TNK3BLK3BLK4_8045a218 + i * 4)),
              prop == 0) ||
             (*(float *)(prop + 0x1cc) <= FLOAT_80618098)) ||
            (prop = FUN_80241370(*(int *)*attach, prop, 1, 0), prop == 0))) {
        i = i + 1;
    }
    __restore_gpr_28();
}
