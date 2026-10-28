// worm_shutter.cpp
// CDH1 submarine-tank "shutter" set-piece: the TRST trigger-switch, the SHUT
// shutter panel, its GSD1/GSD2 shutter-door pair, the GTLT gate-light and the
// rising water/gas column. Address range 0x801e18f4 - 0x801e28e0 (batch_612),
// the continuation of the worm-effect / CDH1 scripted-prop cluster
// (worm_render.cpp / worm_effect.cpp). subsystem:game, worm-effect module.
//
// Functions:
//   shutter_use_toggle        (0x801e18f4) TRST switch use-callback: pick the
//                             open/close branch of the shutter use-handler by
//                             the global toggle DAT_80611d74.
//   shutter_open_step         (0x801e1944) TRST think step: drive the SHUT panel
//                             material scale toward its target, flip the shutter
//                             state 2<->3, kick the HTIP tip actor and play the
//                             shutter-motor sound when fully open.
//   shutter_door_slide        (0x801e1ad4) SHUT per-frame slide: interpolate the
//                             shutter panel offset between its two limits, drive
//                             the GSD1/GSD2 doors open/closed to match, and latch
//                             the DAT_806126a4/d4 open/closed flags.
//   shutter_rig_reset         (0x801e1f30) reset the SHUT bone matrix palette to
//                             identity and rotate joint 1 about the panel pivot.
//   shutter_water_sweep       (0x801e1ff4) the rising water/gas column tick:
//                             spawn+drive the column particle emitter, advance
//                             the fill level FLOAT_806126a8, deal a swept damage
//                             pass to every entity in the column, drive the GTLT
//                             gate-light + SHUT screen-distortion, and set the
//                             water surface plane.
//   shutter_anim_gas_event    (0x801e25e4) GAS0 anim-event(4): roll the gas-jet
//                             chance and start the scaled gas animation.
//   shutter_on_spawn          (0x801e26b4) SHUT spawn-event(6): set render flags,
//                             install the render callback, seed the panel state.
//   shutter_door_anim_event   (0x801e27a0) GSD-door anim-event: on event 4 start
//                             the scaled door anim if the sub-id is 0x15; on
//                             event 1 just accept sub-id 0x15.
//   shutter_door_anim_event_b (0x801e2818) identical handler for the second door.
//   shutter_gas_ready         (0x801e2890) predicate: GAS0 absent AND the shutter
//                             is latched closed (DAT_806126d4==1).
//
// Non-matching: FUN_8023c32c is the entity-by-4CC lookup (returns entity*, arg
// is the packed tag). The PPC FP-context spill frames FUN_803826e4/e8 /
// FUN_80382728/30/34 are dead ABI plumbing kept as opaque open/close calls; the
// caller-saved-FP spill copies (in_f7/in_f8/in_f31/in_ps31_1) the decompiler
// surfaced are dropped. The int<->double magic-bias idiom
// (CONCAT44(0x43300000,x^0x80000000) - DOUBLE_80617420) is reduced to a plain
// -(double)(int) cast. Raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols are declared
// file-locally with the type/arity of their use here; named helpers resolve
// through the shared headers.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {

// --- raw data symbols (file-local, typed to use here) ---
extern int   DAT_80611d74;      // shutter open/close toggle
extern int   DAT_80611d78;      // CDH1 level-state block base
extern int   DAT_80611c24;      // trigger slot pool base
extern int   DAT_80611d54;      // player/camera state block
extern int   DAT_8053fc5c;      // entity id-list base
extern int   DAT_8054030c;      // entity id-list count
extern char  DAT_805015bd;      // level id byte
extern int   DAT_806126a4;      // shutter "open" latch
extern int   DAT_806126d4;      // shutter "closed" latch
extern int   DAT_80612698;      // door-sync enable
extern int   DAT_80612610;      // column player-slot count
extern int   DAT_8061270c;      // column particle handle
extern int   DAT_80610d78;      // column loop sound handle
extern unsigned int DAT_806109c0; // frame-count numerator (magic-bias src)
extern unsigned int DAT_806108e4; // elapsed-count (magic-bias src)

extern float  FLOAT_806126a8;   // current fill level
extern float  FLOAT_80617440;   // 0.0f
extern float  FLOAT_80617428;   // 1.0f (full)
extern float  FLOAT_8061742c;
extern float  FLOAT_80617430;
extern float  FLOAT_80617460;
extern float  FLOAT_80617468;
extern float  FLOAT_806174c4;
extern float  FLOAT_806174dc;
extern float  FLOAT_806174fc;
extern float  FLOAT_8061750c;
extern float  FLOAT_80617558;
extern float  FLOAT_80617564;
extern float  FLOAT_80617570;
extern float  FLOAT_80617574;
extern float  FLOAT_80617578;
extern float  FLOAT_8061757c;
extern double DOUBLE_80617420;
extern double DOUBLE_80617438;
extern double DOUBLE_80617568;
extern double DOUBLE_80617580;
extern float  FLOAT_80617588;

extern int   DAT_803df73c;      // rotation-pivot vec3 (3 words)
extern int   DAT_803df740;
extern int   DAT_803df744;
extern int   DAT_803df748;      // emitter spawn template (bounds/pos block)
extern int   DAT_803df74c;
extern int   DAT_803df750;
extern int   DAT_803df754;
extern int   DAT_803df758;
extern int   DAT_803df75c;
extern int   DAT_803df778;      // damage-dir block A (3 words)
extern int   DAT_803df77c;
extern int   DAT_803df780;
extern int   DAT_803df784;      // damage-dir block B (3 words)
extern int   DAT_803df788;
extern int   DAT_803df78c;
extern int   DAT_805c4ed0;      // column emitter def

// --- raw code symbols (file-local; not present in the generated headers) ---
extern int  *FUN_8023c32c(int tag);          // entity lookup by 4CC
extern unsigned int FUN_80220f30(int mask);  // event-mask query
extern void  FUN_802213b4(int animId);       // anim trigger by id
extern void  FUN_80221074(double a, double b, double c, double d,
                          double e, double f, double g, double h); // actor dispatch
extern int   FUN_803825c0(double v);          // float->fixed helper
extern int   FUN_803826e8(void);              // spill frame open (returns frame)
extern void  FUN_80382734(void);              // spill frame close
extern void  FUN_803826e4(void);              // spill frame open
extern void  FUN_80382730(void);              // spill frame close
extern int   FUN_801e60a8(int self);          // SHUT render callback

} // extern "C" (raw symbols)

// mtx4_rotate_about_point / damage_dispatch_to_entity are declared in the
// shared headers with a reduced/guessed arity that does not match these call
// sites; route them through file-local const fn-ptr aliases (initialised to the
// real symbol) with the true arity — same pattern as worm_render.cpp's __wr_*.
static void (*const __ws_mtx_rotate_about_point)(int, int *, float *) =
    (void (*)(int, int *, float *))&mtx4_rotate_about_point;
static void (*const __ws_damage_dispatch)(double, double, double, double,
                                          double, double, double, double, int *) =
    (void (*)(double, double, double, double,
              double, double, double, double, int *))&damage_dispatch_to_entity;
// rng_counter_tick is declared void in the shared header but the real routine
// returns the fresh counter word; alias it with the real return type.
static unsigned int (*const __ws_rng_tick)(void) =
    (unsigned int (*)(void))&rng_counter_tick;

// Named helpers (switch_*, trigger_*, entity_*, render_instance_*, sound_*,
// particle_*, damage_*, water_surface_*, screen_effect_*, anim_play_scaled,
// rng_counter_tick, mtx4_*) and this batch's own functions are prototyped by
// generated_decls.h (pulled via entity.h) with C++ linkage; do not redeclare.
// NOTE: switch_find_slot_by_entity returns void* there — cast at each use.

// ---------------------------------------------------------------------------
// 0x801e18f4  TRST switch use-callback.
void shutter_use_toggle(void)
{
    switch_find_slot_by_entity(0x54525354);   // 'TRST'
    if (DAT_80611d74 == 0) {
        switch_use_handler();
    } else {
        switch_use_handler();
    }
}

// ---------------------------------------------------------------------------
// 0x801e1944  TRST think step: drive shutter open, flip state, motor sound.
void shutter_open_step(double a1, double a2, double a3, double a4,
                       double a5, double a6, double a7, double a8)
{
    unsigned int *slot;
    int tip;
    int params[12];

    slot = (unsigned int *)switch_find_slot_by_entity(0x54525354);   // 'TRST'
    render_instance_set_material(FLOAT_80617440,
                                 *(int **)(slot[2] + 4), 1);

    tip = (int)FUN_8023c32c(0x48544950);   // 'HTIP'
    if (tip != 0 &&
        slot[7] == (unsigned int)*(int *)(DAT_80611d78 + 0x14) &&
        *(int *)(DAT_80611d78 + 0xdec) != 0x4c &&
        *(int *)(tip + 0x34) == 0x4c) {
        DAT_806126a4 = 0;
        FUN_802213b4(0xf);
        FUN_80221074((double)FLOAT_80617440, a2, a3, a4, a5, a6, a7, a8);
        *(float *)(tip + 0x1cc) = FLOAT_80617558;
    }

    if (FLOAT_80617440 < *(float *)(slot[2] + 0x1a4)) {
        slot[4] = slot[4] & 0xffffff7f;
    } else {
        entity_register_for_update((int)slot[2], 3, (int)0x801e1ad4);   // shutter_door_slide
        entity_register_for_update((int)slot[2], 2, (int)0x801e1f30);   // shutter_rig_reset
        int st = (int)slot[0xf];
        if (st == 2 || st == 1) {
            slot[0xf] = 3;
        } else if (st == 3 || st == 0) {
            slot[0xf] = 2;
        }
        if (*(int *)(slot[7] + 0x150) != 0) {
            *(float *)(slot[2] + 0x1a4) = FLOAT_806174dc;
        }
        sound_init_play_params((int)params);
        params[0] = 0x3aa;
        sound_play_request(params);
    }
    *(unsigned int *)(slot[2] + 0x28) |= 0x4000000;
    *(unsigned int *)(slot[2] + 0x18) |= 2;
}

// ---------------------------------------------------------------------------
// 0x801e1ad4  SHUT per-frame slide + GSD1/GSD2 door sync.
static void shutter_close_gsd(int tag)
{
    unsigned int *d = (unsigned int *)trigger_slot_find_by_entity_id(tag);
    if (d == 0) {
        return;
    }
    if ((d[3] & 2) == 0) {
        unsigned int h = d[0];
        int ent;
        if (h == 0) {
            ent = 0;
        } else {
            ent = DAT_80611c24 + (int)(h >> 0x16) * 0x2e8;
            if (*(unsigned int *)(ent + 0x2c) != h) {
                ent = 0;
                d[0] = 0;
            }
        }
        trigger_close_if_idle(ent);
    }
    d[3] |= 2;
}

static void shutter_open_gsd(int tag, int shutSlot)
{
    unsigned int *d = (unsigned int *)trigger_slot_find_by_entity_id(tag);
    if (d == 0) {
        return;
    }
    d[3] &= 0xfffffffd;
    if (FLOAT_80617440 != *(float *)(shutSlot + 0x40)) {
        return;
    }
    unsigned int h = d[0];
    int ent;
    if (h == 0) {
        ent = 0;
    } else {
        ent = DAT_80611c24 + (int)(h >> 0x16) * 0x2e8;
        if (*(unsigned int *)(ent + 0x2c) != h) {
            ent = 0;
            d[0] = 0;
        }
    }
    if (trigger_is_fully_open(ent) != 0) {
        h = d[0];
        if (h == 0) {
            ent = 0;
        } else {
            ent = DAT_80611c24 + (int)(h >> 0x16) * 0x2e8;
            if (*(unsigned int *)(ent + 0x2c) != h) {
                ent = 0;
                d[0] = 0;
            }
        }
        trigger_open_full(ent, 0);
    }
}

void shutter_door_slide(int self)
{
    int shutEnt;
    int shutSlot;
    double base;
    double span;

    shutEnt = (int)FUN_8023c32c(0x53485554);   // 'SHUT'
    base = DOUBLE_80617420;
    shutSlot = *(int *)(self + 0x7c);
    span = (double)FLOAT_80617428;

    // panel offset := lerp toward limit, non-matching magic-bias reduced to casts
    float denom = (float)(-(double)(int)DAT_806109c0 - base);
    float elapsed = (float)(-(double)(int)DAT_806108e4 - base);
    *(float *)(*(int *)(shutSlot + 8) + 0x1a4) =
        -((float)(span / (double)denom) * elapsed -
          *(float *)(*(int *)(shutSlot + 8) + 0x1a4));

    float step = FLOAT_80617564;
    if (*(int *)(shutSlot + 0x3c) == 2) {
        // opening
        *(int *)(*(int *)(shutEnt + 0x7c) + 0xa0) = 3;
        double closeLimit = DOUBLE_80617568;
        *(unsigned int *)(shutEnt + 0x28) |= 0x4000000;
        *(float *)(shutSlot + 0x40) =
            step * elapsed * (float)(span / (double)denom) +
            *(float *)(shutSlot + 0x40);
        *(unsigned int *)(*(int *)(shutSlot + 8) + 0x18) |= 2;
        if (closeLimit < ABS((double)*(float *)(shutSlot + 0x40))) {
            *(float *)(shutSlot + 0x40) = FLOAT_80617570;
            *(int *)(shutSlot + 0x3c) = 1;
            DAT_806126d4 = 0;
            DAT_806126a4 = 1;
            *(unsigned int *)(self + 0x28) &= 0xfbffffff;
            FUN_802213b4(0x27);
        }
        shutter_close_gsd(0x47534431);   // 'GSD1'
        shutter_close_gsd(0x47534432);   // 'GSD2'
    } else if (*(int *)(shutSlot + 0x3c) == 3) {
        // closing
        double closeStep = (double)FLOAT_80617564;
        *(int *)(*(int *)(shutEnt + 0x7c) + 0xa0) = 2;
        float zero = FLOAT_80617440;
        *(unsigned int *)(shutEnt + 0x28) |= 0x4000000;
        float rate = (float)(span / (double)denom);
        float delta = (float)(closeStep * (double)elapsed);
        *(float *)(shutSlot + 0x40) =
            -(float)((double)delta * (double)rate -
                     (double)*(float *)(shutSlot + 0x40));
        *(unsigned int *)(*(int *)(shutSlot + 8) + 0x18) |= 2;
        if ((double)zero < (double)*(float *)(shutSlot + 0x40)) {
            *(float *)(shutSlot + 0x40) = zero;
            *(int *)(shutSlot + 0x3c) = 0;
            DAT_806126d4 = 1;
            DAT_806126a4 = 0;
            *(unsigned int *)(self + 0x28) &= 0xfbffffff;
            FUN_80221074((double)zero, 0, 0, 0, 0, 0, 0, 0);
            FUN_80221074((double)FLOAT_80617440, 0, 0, 0, 0, 0, 0, 0);
        }
        if (DAT_80612698 != 0) {
            shutter_open_gsd(0x47534431, shutSlot);   // 'GSD1'
        }
        shutter_open_gsd(0x47534432, shutSlot);       // 'GSD2'
    }
}

// ---------------------------------------------------------------------------
// 0x801e1f30  reset SHUT bone matrix palette + rotate joint 1 about pivot.
void shutter_rig_reset(void)
{
    int frame;
    int rig;
    int *sub;
    int i;
    float *pivot;
    int off;
    int pivotVec[3];

    frame = FUN_803826e8();
    rig = *(int *)(frame + 4);
    sub = *(int **)(*(int *)(rig) + 4);
    off = 0;
    pivot = (float *)sub[*sub * -0x30 + 0x1b];
    pivotVec[0] = DAT_803df73c;
    pivotVec[1] = DAT_803df740;
    pivotVec[2] = DAT_803df744;

    for (i = 0; rig = *(int *)(frame + 4),
                i < *(int *)(*(int *)(rig + 4) + 4); i = i + 1) {
        mtx4_set_identity(*(int *)(rig + 0x10) + off);
        off = off + 0x40;
    }
    __ws_mtx_rotate_about_point(*(int *)(rig + 0x10) + 0x40, pivotVec, pivot);
    *(unsigned int *)(frame + 0x28) |= 0x200000;
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x801e1ff4  rising water/gas column tick: emitter + swept damage + surface.
void shutter_water_sweep(double a1, double a2, double a3, double a4,
                         double a5, double a6, double a7, double a8)
{
    unsigned int *pS;
    unsigned int *pD;
    unsigned int v;
    int i;
    int gate;
    int handle;
    int emitDef[6];      // local copy of the emitter bounds/pos template
    float boundsHi[6];   // local_1a4[6]
    int params[12];
    int dmg[24];         // switch_slot_reset scratch (local_a0 / local_fc)

    FUN_803826e4();

    // build the emitter template from DAT_803df748.. (byte-swapped copy loop)
    emitDef[0] = DAT_803df748;
    emitDef[1] = DAT_803df74c;
    emitDef[2] = DAT_803df750;
    boundsHi[0] = (float)DAT_803df754;
    boundsHi[1] = (float)DAT_803df758;
    boundsHi[2] = (float)DAT_803df75c;

    handle = DAT_8061270c;
    if (!particle_handle_is_valid(&handle)) {
        particle_emitter_spawn((u32)(size_t)&DAT_8061270c,
                               (u32)(size_t)&DAT_805c4ed0, 0x10000,
                               boundsHi, (float *)emitDef);
        int h2 = DAT_8061270c;
        particle_cached_enable((ParticleEmitter **)&h2);
    }

    if (DAT_806126d4 == 1) {
        int h3 = DAT_8061270c;
        particle_cached_disable((ParticleEmitter **)&h3);
        if (0 <= (int)DAT_80610d78) {
            sound_stop_handle((unsigned int)DAT_80610d78);
            DAT_80610d78 = -1;
        }
        FUN_80221074((double)FLOAT_80617440, a2, a3, a4, a5, a6, a7, a8);
    } else {
        int h4 = DAT_8061270c;
        particle_cached_enable((ParticleEmitter **)&h4);
        if ((int)DAT_80610d78 < 0) {
            sound_init_play_params((int)params);
            params[0] = 0x398;
            DAT_80610d78 = sound_play_request(params);
        }
        FUN_802213b4(0x1c);
    }

    int h5 = DAT_8061270c;
    particle_cached_set_bounds((ParticleEmitter **)&h5, 0, boundsHi);

    // advance fill level (magic-bias reduced to (double)(int) cast)
    float rate = FLOAT_80617428 / (float)(-(double)(int)DAT_806109c0 - DOUBLE_80617420);
    if (DAT_806126d4 == 0) {
        FLOAT_806126a8 = FLOAT_806126a8 + rate / FLOAT_806174dc;
    } else {
        FLOAT_806126a8 = FLOAT_806126a8 - rate / FLOAT_806174dc;
    }

    double full = (double)FLOAT_80617428;
    double lvl = (double)FLOAT_806126a8;
    if (lvl <= full) {
        if (lvl < (double)FLOAT_80617440) {
            FLOAT_806126a8 = FLOAT_80617440;
        }
    } else {
        FLOAT_806126a8 = FLOAT_80617428;
    }

    // swept contact damage to every eligible entity while filling
    if (FLOAT_80617440 < FLOAT_806126a8) {
        int idx = 0;
        for (i = 0; i < DAT_8054030c; i = i + 1) {
            int ent = *(int *)(DAT_8053fc5c + idx);
            if (*(int *)(ent + 0x34) != 0xd5 &&
                (*(unsigned int *)(ent + 0x28) & 0x80000000) == 0 &&
                *(int *)(ent + 0x20) == 8 &&
                *(int *)(ent + 0x60) != 0x48544950 &&   // not 'HTIP'
                (*(int *)(ent + 0x7c) == 0 ||
                 DAT_80612610 <= *(int *)(*(int *)(ent + 0x7c) + 8))) {
                switch_slot_reset((int)dmg);
                dmg[0] = ent;
                *(int *)((int)dmg + 0x38) = ent + 100;      // local_78
                *(float *)((int)dmg + 0x30) =               // local_80
                    (float)((double)FLOAT_80617460 * (double)FLOAT_806126a8);
                *(int **)((int)dmg + 0x40) = (int *)boundsHi; // local_70
                *(int *)((int)dmg + 0x14) = 0x15;           // local_8c
                *(int *)((int)dmg + 0x18) = 0x15;           // local_88
                *(int *)((int)dmg + 0x3c) = 0x200800;       // local_64
                __ws_damage_dispatch((double)FLOAT_80617460, lvl, full,
                                     a4, a5, a6, a7, a8, dmg);
            }
            idx = idx + 4;
        }
    }

    // per-player-slot GTLT gate-light + SHUT screen-distortion drive
    i = 0;
    int slotOff = 0;
    for (;;) {
        if (DAT_80612610 <= i) {
            if (FLOAT_806126a8 <= FLOAT_80617440 ||
                (DAT_805015bd == '\n' && 1 < DAT_80612610)) {
                collision_apply_layer_mask_to_room();
                collision_apply_layer_mask_to_room();
            } else {
                switch_find_slot_by_entity(0x54525354);   // 'TRST'
                float lvlf = FLOAT_806126a8;
                float inv = (float)((double)FLOAT_80617428 - (double)lvlf);
                float planeA = (float)((double)FLOAT_806174c4 * (double)lvlf +
                                       (double)(FLOAT_8061750c * inv));
                float planeB = (float)((double)FLOAT_80617578 * (double)lvlf +
                                       (double)(FLOAT_8061757c * inv));
                float t = (float)((double)(boundsHi[3] -
                                  *(float *)(DAT_80611d54 + 0x27c)) - (double)planeB) /
                          (float)((double)planeA - (double)planeB);
                water_surface_set_params(DOUBLE_80617580, (double)planeA,
                                         (double)planeB, (double)FLOAT_80617428, 0);
                double clamped = (double)FLOAT_80617440;
                if (clamped <= (double)t) {
                    clamped = (double)t;
                    if ((double)FLOAT_80617428 < (double)t) {
                        clamped = (double)FLOAT_80617428;
                    }
                }
                int fixed = FUN_803825c0((double)(float)((double)FLOAT_80617588 * clamped));
                (void)fixed;
                collision_forward_shape_data(2, (int)dmg);
                collision_apply_layer_mask_to_room();
                collision_apply_layer_mask_to_room();
            }
            FUN_80382730();
            return;
        }

        int stateEnt = DAT_80611d78 + slotOff;
        int fx;
        if (i == 0) {
            fx = (int)FUN_8023c32c(0x47544c54);   // 'GTLT'
        } else {
            fx = (int)FUN_8023c32c(0x53485554);   // 'SHUT'
        }
        float distMax = FLOAT_80617574;
        float zero = FLOAT_80617440;
        if (fx != 0) {
            int active = *(int *)(fx + 0x268);
            if (active != 0 || FLOAT_806126a8 <= FLOAT_80617440) {
                if (active != 0) {
                    double lvl2 = (double)FLOAT_806126a8;
                    double z2 = (double)FLOAT_80617440;
                    if (z2 < lvl2 && *(int *)(stateEnt + 0xdec) == 0x4c) {
                        // deal a distortion-band damage tick
                        int dirA[3];
                        int dirB[3];
                        dirA[0] = DAT_803df778;
                        dirA[1] = DAT_803df77c;
                        dirA[2] = DAT_803df780;
                        dirB[0] = DAT_803df784;
                        dirB[1] = DAT_803df788;
                        dirB[2] = DAT_803df78c;
                        *(float *)(fx + 0x1cc) =
                            -(float)((double)FLOAT_80617460 * lvl2 -
                                     (double)*(float *)(fx + 0x1cc));
                        if ((double)*(float *)(fx + 0x1cc) < z2) {
                            *(float *)(fx + 0x1cc) = zero;
                            switch_slot_reset((int)dmg);
                            dmg[0] = *(int *)(stateEnt + 0x14);
                            *(float *)((int)dmg + 0x30) =
                                (float)((double)FLOAT_80617460 * (double)FLOAT_806126a8);
                            *(int **)((int)dmg + 0x38) = dirA;   // local_d4
                            *(int **)((int)dmg + 0x40) = dirB;   // local_cc
                            *(int *)((int)dmg + 0x14) = 0x15;
                            *(int *)((int)dmg + 0x18) = 0x15;
                            *(int *)((int)dmg + 0x3c) = 0x200800;
                            __ws_damage_dispatch((double)FLOAT_80617460, z2, lvl2,
                                                 a4, a5, a6, a7, a8, dmg);
                        }
                        *(int *)(stateEnt + 0xe8c) = fx;
                        goto next_slot;
                    }
                }
                if (active != 0) {
                    *(float *)(fx + 0x1cc) = *(float *)(fx + 0x1cc) + FLOAT_806174fc;
                    *(int *)(stateEnt + 0xe8c) = fx;
                    if (distMax <= *(float *)(fx + 0x1cc)) {
                        *(float *)(fx + 0x1cc) = distMax;
                        *(int *)(stateEnt + 0xe8c) = 0;
                    }
                }
            } else {
                *(float *)(fx + 0x1cc) = FLOAT_80617574;
                *(float *)(fx + 0x1d8) = distMax;
                int slot = (int)screen_effect_alloc_slot();
                *(int *)(fx + 0x268) = slot;
                screen_effect_init_slot((int *)*(int *)(fx + 0x268), (int *)fx);
                *(int *)(stateEnt + 0xe8c) = fx;
            }
        }
    next_slot:
        i = i + 1;
        slotOff = slotOff + 0x141c;
    }
}

// ---------------------------------------------------------------------------
// 0x801e25e4  GAS0 anim-event(4): roll gas-jet chance, start scaled anim.
unsigned int shutter_anim_gas_event(int self, int event)
{
    unsigned int r = 0;
    if (event == 4) {
        unsigned int roll = 0;
        int chance = 1;
        if (*(short *)(*(int *)(*(int *)(self + 0x7c) + 0xbf0) + 0x94) != 0) {
            roll = __ws_rng_tick();
            chance = (int)(FLOAT_80617468 *
                           ((float)((double)(int)roll - DOUBLE_80617438) / FLOAT_8061742c) +
                           FLOAT_80617430);
        }
        if ((*(short *)(*(int *)(*(int *)(self + 0x7c) + 0xbf0) + 0x94) == 0 || chance != 0) &&
            FUN_80220f30(0xb) == 0) {
            r = 0xfffffffd;
        } else {
            anim_play_scaled((double)FLOAT_80617440, (double)FLOAT_80617428);
            r = 0xffffffff;
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// 0x801e26b4  SHUT spawn-event(6): render flags + callback + seed panel state.
unsigned int shutter_on_spawn(int self, int event)
{
    float zero = FLOAT_8061750c;
    if (event != 6) {
        return 0;
    }
    *(unsigned int *)(self + 0x28) |= 0x20001;
    *(float *)(self + 0x1cc) = zero;
    *(float *)(self + 0x1d8) = zero;
    entity_set_render_type(self, (int)&FUN_801e60a8);
    int st = *(int *)(self + 0x7c);
    if (*(int *)(st + 0xaac) == 1 && *(int *)(st + 0xaa8) == 0) {
        unsigned int m;
        if (DAT_805015bd == '\n') {
            m = *(unsigned int *)(st + 0x14);
        } else {
            m = *(unsigned int *)(st + 0x1c);
        }
        if ((m & 1) == 0) {
            goto seed_done;
        }
    }
    *(int *)(st + 0xaac) = 0;
    *(int *)(st + 0xaa8) = 8;
seed_done:
    *(unsigned char *)(self + 0x32) = 2;
    return 0xffffffff;
}

// ---------------------------------------------------------------------------
// 0x801e27a0 / 0x801e2818  GSD door anim-event handlers (identical).
static unsigned int shutter_door_anim_common(int event, int arg)
{
    unsigned int r = 0;
    if (event == 4) {
        if (arg != 0 && *(int *)(arg + 0x18) == 0x15) {
            anim_play_scaled((double)FLOAT_80617440, (double)FLOAT_80617428);
            r = 0xffffffff;
        }
    } else if (event < 4 && event == 1 && *(int *)(arg + 0x18) == 0x15) {
        r = 0xffffffff;
    }
    return r;
}

unsigned int shutter_door_anim_event(int self, int event, int arg)
{
    (void)self;
    return shutter_door_anim_common(event, arg);
}

unsigned int shutter_door_anim_event_b(int self, int event, int arg)
{
    (void)self;
    return shutter_door_anim_common(event, arg);
}

// ---------------------------------------------------------------------------
// 0x801e2890  predicate: GAS0 absent AND shutter latched closed.
unsigned int shutter_gas_ready(void)
{
    int gas = (int)FUN_8023c32c(0x47415330);   // 'GAS0'
    if (gas == 0) {
        return (DAT_806126d4 == 1) ? 1u : 0u;
    }
    return 0;
}

// ===========================================================================
// batch_613  0x801e28e0 - 0x801e2c48  CDH1 shutter set-piece continuation:
// GAS0/GAS1 spawn-point gate predicates, the music-stream ready gate, the HTIP
// tip damage tick, the shutter scratch render-target alloc and the scrolling
// shutter-texture render pass. subsystem:game, worm-effect module.
//
//   shutter_gas0_spawn_gate   (0x801e28e0) spawn-point predicate for the GAS0
//                             actor: allow while GAS0 is state 0x4c and the
//                             shutter is closed with its distortion still > 0.
//   shutter_gas1_spawn_gate   (0x801e295c) same for the GAS1 actor.
//   shutter_gas0_closed       (0x801e29d8) bool: GAS0 present+state 0x4c AND
//                             shutter closed (branchless clz>>5 of DAT_806126d4).
//   shutter_gas1_closed       (0x801e2a38) same for GAS1.
//   shutter_any_gas_active    (0x801e2a98) bool: either GAS0 or GAS1 is state
//                             0x4c.
//   shutter_music_ready_gate  (0x801e2b18) gate: event-mask(2) unless the
//                             DAT_80612480 music stream slot is still running.
//   shutter_tip_damage_tick   (0x801e2b64) HTIP tip damage tick: while HTIP is
//                             state 0x4c, the level-state door != 0x4c and the
//                             shutter is open, bleed the tip distortion down and
//                             deal a damage tick, then anim-trigger 0x10.
//   shutter_render_nop        (0x801e2c10) empty render hook.
//   shutter_rt_alloc          (0x801e2c14) allocate the 0x40x0x100 scratch
//                             render target into DAT_806126d0.
//   shutter_texture_render    (0x801e2c48) build the scrolling shutter texture:
//                             fill the scratch RT, walk the strip table drawing
//                             each tile row scrolled by the panel offset, then
//                             bind the RT as the SHUT strip-0 material.
// Non-matching: FP-context spill frames FUN_803826dc/FUN_80382728 kept opaque;
// 0x43300000 magic-bias reduced to (double)(int) casts. gx_set_color_update_mode
// is declared with a mode arg in the shared header but the ROM calls it with the
// live BP register; routed through a file-local no-arg alias.
// ===========================================================================

extern "C" {
extern int   DAT_80612480;      // CDH1 music stream slot handle
extern int   DAT_806126d0;      // shutter scratch render-target slot
extern int  *DAT_80610a20;      // strip-record pool base ptr
extern float FLOAT_80617478;    // HTIP tip damage-per-tick
extern int   FUN_803826dc(void);// spill frame open (returns frame)
extern void  FUN_80382728(void);// spill frame close
} // extern "C"

// gx_set_color_update_mode: shared header declares (int mode); the ROM call site
// passes the live mode register (save/restore around the fill). Preserve the
// real no-arg ABI via a file-local alias.
static unsigned int (*const __ws_gx_color_update)(void) =
    (unsigned int (*)(void))&gx_set_color_update_mode;

// ---------------------------------------------------------------------------
// 0x801e28e0  GAS0 spawn-point gate predicate.
unsigned int shutter_gas0_spawn_gate(void)
{
    unsigned int r = 1;
    int gas = aiactor_find_player_by_team(0x47415330);   // 'GAS0'
    if (gas != 0) {
        if (*(int *)(gas + 0x34) == 0x4c) {
            r = 0;
            if (DAT_806126d4 == 1 && FLOAT_80617440 < *(float *)(gas + 0x1cc)) {
                r = 1;
            }
        } else {
            r = 0;
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// 0x801e295c  GAS1 spawn-point gate predicate.
unsigned int shutter_gas1_spawn_gate(void)
{
    unsigned int r = 1;
    int gas = aiactor_find_player_by_team(0x47415331);   // 'GAS1'
    if (gas != 0) {
        if (*(int *)(gas + 0x34) == 0x4c) {
            r = 0;
            if (DAT_806126d4 == 1 && FLOAT_80617440 < *(float *)(gas + 0x1cc)) {
                r = 1;
            }
        } else {
            r = 0;
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// 0x801e29d8  GAS0 present && shutter closed.
unsigned int shutter_gas0_closed(void)
{
    unsigned int r = 1;
    int gas = aiactor_find_player_by_team(0x47415330);   // 'GAS0'
    if (gas != 0) {
        if (*(int *)(gas + 0x34) == 0x4c) {
            r = countLeadingZeros(DAT_806126d4) >> 5;    // DAT!=0 -> 0, ==0 -> 1
        } else {
            r = 0;
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// 0x801e2a38  GAS1 present && shutter closed.
unsigned int shutter_gas1_closed(void)
{
    unsigned int r = 1;
    int gas = aiactor_find_player_by_team(0x47415331);   // 'GAS1'
    if (gas != 0) {
        if (*(int *)(gas + 0x34) == 0x4c) {
            r = countLeadingZeros(DAT_806126d4) >> 5;
        } else {
            r = 0;
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// 0x801e2a98  either GAS0 or GAS1 active (state 0x4c).
unsigned int shutter_any_gas_active(void)
{
    unsigned int r = 0;
    int g0 = aiactor_find_player_by_team(0x47415330);   // 'GAS0'
    int g1 = aiactor_find_player_by_team(0x47415331);   // 'GAS1'
    if (g0 != 0 && *(int *)(g0 + 0x34) == 0x4c) {
        r = 1;
    }
    if (g1 != 0 && *(int *)(g1 + 0x34) == 0x4c) {
        r = 1;
    }
    return r;
}

// ---------------------------------------------------------------------------
// 0x801e2b18  music-stream ready gate.
unsigned int shutter_music_ready_gate(void)
{
    unsigned int ev = FUN_80220f30(2);
    int busy = svslot_is_stream_done(DAT_80612480);
    unsigned int r = 1;
    if (busy == 0) {
        r = ev;
    }
    return r;
}

// ---------------------------------------------------------------------------
// 0x801e2b64  HTIP tip damage tick.
void shutter_tip_damage_tick(double a1, double a2, double a3, double a4,
                             double a5, double a6, double a7, double a8)
{
    int dmg[24];     // switch_slot_reset scratch; slot 0 = tip entity
    int stateId;     // dmg[1] = level-state door id
    float perTick;   // dmg[..0x30]

    dmg[0] = aiactor_find_player_by_team(0x48544950);   // 'HTIP'
    if (dmg[0] != 0 && *(int *)(dmg[0] + 0x34) == 0x4c &&
        *(int *)(DAT_80611d74 + 0xdec) != 0x4c && DAT_806126a4 != 0) {
        switch_slot_reset((int)dmg);
        double dps = (double)FLOAT_80617478;
        stateId = *(int *)(DAT_80611d74 + 0x14);
        dmg[1] = stateId;                                 // local_64
        perTick = FLOAT_80617478;
        *(float *)((int)dmg + 0x38) = perTick;            // local_48
        *(float *)(dmg[0] + 0x1cc) =
            (float)((double)*(float *)(dmg[0] + 0x1cc) - dps);
        __ws_damage_dispatch(dps, a2, a3, a4, a5, a6, a7, a8, dmg);
        FUN_80221074((double)FLOAT_80617440, a2, a3, a4, a5, a6, a7, a8);
        FUN_802213b4(0x10);
    }
}

// ---------------------------------------------------------------------------
// 0x801e2c10  empty render hook.
void shutter_render_nop(void)
{
}

// ---------------------------------------------------------------------------
// 0x801e2c14  allocate the shutter scratch render target.
void shutter_rt_alloc(void)
{
    DAT_806126d0 = -1;
    render_target_alloc_scratch(0x40, 0x100, &DAT_806126d0);
}

// ---------------------------------------------------------------------------
// 0x801e2c48  build the scrolling shutter texture into the scratch RT.
void shutter_texture_render(void)
{
    int frame;
    int st;
    int *strip;
    int savedSub;
    int savedBlend;
    int stripIdx;
    int rec;
    double offset;
    double zero;

    frame = FUN_803826dc();
    st = *(int *)(frame + 0x7c);
    strip = (int *)collision_alloc_strip_table(*(int **)(frame + 4));
    float panel = *(float *)(st + 0x10);

    savedSub = (int)gx_set_sub_material_id(0xe);
    __ws_gx_color_update();
    savedBlend = gx_set_blend_mode(0);

    // starting scroll offset := (0x100 - panel) - base   (magic-bias -> cast)
    offset = (double)(float)(-(double)(int)(0x100 - (int)panel) - DOUBLE_80617420);
    stripIdx = *strip;
    rec = *DAT_80610a20 + stripIdx * 0x58;
    zero = DOUBLE_80617420;

    gx_fill_rect_clipped(0, (unsigned int)(int)offset, 0x40, 0x100, 0x7f000000);
    gx_set_active_material(stripIdx);

    for (double y = offset; (double)FLOAT_80617440 < y; ) {
        unsigned short h = *(unsigned short *)(rec + 0x50);
        double tileH = (double)(int)((unsigned int)h >> 1) - zero;
        gx_draw_sprite_rect(0, (unsigned int)(int)(y - tileH), 0x40,
                            (unsigned int)(int)y, 0, 0,
                            (unsigned int)*(unsigned short *)(rec + 0x4c) << 4,
                            (unsigned int)h << 4);
        y = (double)(float)(y - tileH);
    }

    render_target_clear_bind(DAT_806126d0, 1);
    gx_fill_rect_flags(0xf, 0, 0, 0x40, 0x100, 0);
    collision_set_shape_params(*(int **)(frame + 4), 1, 0xffffffff, 10, 0);
    gx_set_sub_material_id(savedSub);
    __ws_gx_color_update();
    gx_set_blend_mode(savedBlend);
    *strip = DAT_806126d0;
    FUN_80382728();
}

// ===========================================================================
// batch_614  0x801e2e50 - 0x801e34f4  EKUN electro-tank / breakable set-piece:
// the shutter-plate slide state machine, its progress-ratio helper + face-table
// matrix setup, spawn wrappers, the EKUN menu-enter/exit triggers (MACG/MACF
// spawn points), the NUKE reset, a subobj shell-eject destroy, and the FBRL
// breakable-fragment scatter. subsystem:game, worm-effect module.
//
//   ekun_plate_slide          (0x801e2e50) SHUT plate slide state machine:
//                             advance/retract the panel offset (field 0x10) by
//                             the frame delta between limits 0/FLOAT_8061758c,
//                             stepping state field 0xa0 (2->1 open, 3..->0 close).
//   ekun_plate_ratio          (0x801e2f5c) return the SHUT panel offset scaled by
//                             FLOAT_80617590 (0 while absent or state 4).
//   ekun_face_matrix_setup    (0x801e2fc0) alloc a pool matrix, build the face
//                             table, set its translation from ekun_plate_ratio,
//                             store as face entry 0.
//   ekun_spawn_reset_wrapper  (0x801e3048) reset DAT_806126cc then forward to the
//                             shared spawn helper FUN_803668a8.
//   ekun_motor_sound_step     (0x801e3094) drive the EKUN motor: when the ekun
//                             progress ratio rises past the stored peak, play the
//                             3D motor sound 0x3a5 at the bone point, then tick
//                             the shared driver FUN_803669b8.
//   ekun_enter_menu_gate      (0x801e3160) if a player holds the EKUN and no slot
//                             is already in menu-state 0x1c and the progress ratio
//                             is up, enter the MACG menu (player_enter_menu_state).
//   ekun_confirm_menu         (0x801e322c) MACF menu confirm: enter menu-state
//                             0x1d with sound 0x39f, or fall back to the cancel
//                             paths (FUN_8024206c) when not ready.
//   ekun_nuke_reset           (0x801e32ec) NUKE reset: seed the plate state, exit
//                             any player menu slots, stop the persistent sound.
//   ekun_shell_eject_destroy  (0x801e33c8) subobj shell-eject: count down the
//                             eject timer (field 0x69), then spawn the ejection
//                             explosion + shell-casing burst and destroy self.
//   ekun_fragment_scatter     (0x801e34f4) FBRL breakable scatter: gather the
//                             fragment sub-objects, compute their centroid, then
//                             convert each to a free rigid body with an outward
//                             randomised impulse (subobj_set_linear_impulse).
// Non-matching: FP-context spill frames FUN_803826dc/FUN_80382728 opaque; the
// huge in_fNN/in_psNN caller-saved-FP spill copies dropped. 0x43300000 magic-bias
// reduced to (double)(int) casts. mtx4_pool_alloc (real ptr return),
// mtx4_set_translation (dst + xyz), player_ai_explosion_spawn (float-tagged
// param_1), sound_stop_persistent_by_id (2 args) and the 12-arg FP-spill form of
// shell_casing_spawn_batch (real: shell_casing_spawn_batch_with_flags pos,id,flags)
// are routed through file-local const fn-ptr aliases.
// ===========================================================================

extern "C" {
extern int   DAT_806126cc;      // ekun spawn scratch handle
extern float FLOAT_8061758c;    // plate slide upper limit
extern float FLOAT_80617590;    // plate-ratio scale
extern float FLOAT_80617594;    // face-matrix translate bias
extern float FLOAT_80617598;    // face-matrix translate scale
extern float FLOAT_8061759c;    // fragment scatter height bias
extern float FLOAT_80617510;    // NUKE reset plate offset seed
extern float FLOAT_806108ec;    // frame delta (magic-bias companion)
extern float FLOAT_80610d7c;    // ekun motor peak-ratio store
extern float FLOAT_80610d80;    // fragment impulse Y scale
extern float FLOAT_80610d84;    // fragment impulse Z scale
extern float FLOAT_80610d88;    // fragment spin base
extern float FLOAT_80610d8c;    // fragment spin range
extern float FLOAT_806126ac;    // fragment impulse X scale
extern float FLOAT_8061757c;    // (shell-eject casing param; also used earlier)

extern double FUN_80390324(double v);   // float normalise/curve helper
extern void   FUN_803668a8(double a, double b, double c, double d,
                           double e, double f, double g, double h); // spawn helper
extern void   FUN_803669b8(double a, double b, double c, double d,
                           double e, double f, double g, double h); // shared driver
extern unsigned int FUN_80241874(void); // menu-ready query
extern void   FUN_8024206c(void);        // menu cancel/back
extern void   FUN_8024188c(int p);       // menu post-enter
extern void   FUN_802212b4(int id);      // anim trigger variant
extern unsigned int FUN_8023c59c(int tag, int outArr, int max); // collect subobjs by 4CC
} // extern "C"

// Signature-mismatch helpers routed through file-local const fn-ptr aliases.
static int  (*const __ws_mtx_pool_alloc)(void) =
    (int (*)(void))&mtx4_pool_alloc;
static void (*const __ws_mtx_set_translation)(int, double, double, double) =
    (void (*)(int, double, double, double))&mtx4_set_translation;
static void (*const __ws_explosion_spawn)(double, int, float *, float *, int, int, int) =
    (void (*)(double, int, float *, float *, int, int, int))&player_ai_explosion_spawn;
static void (*const __ws_stop_persistent)(void) =
    (void (*)(void))&sound_stop_persistent_by_id;
static void (*const __ws_shell_batch)(double, double, double, double, double, double,
                                      double, double, int, float *, float *, int) =
    (void (*)(double, double, double, double, double, double,
              double, double, int, float *, float *, int))&shell_casing_spawn_batch_with_flags;

// ---------------------------------------------------------------------------
// 0x801e2e50  SHUT plate slide state machine.
void ekun_plate_slide(int self)
{
    float limit = FLOAT_8061758c;
    float zero = FLOAT_80617440;
    int st = *(int *)(self + 0x7c);
    int state = *(int *)(st + 0xa0);
    float delta = (float)(-(double)(int)DAT_806108e4 - DOUBLE_80617420);

    if (state == 2) {
        *(float *)(st + 0x10) = *(float *)(st + 0x10) + delta;
        if (limit <= *(float *)(st + 0x10)) {
            *(float *)(st + 0x10) = limit;
            *(int *)(st + 0xa0) = 1;
        }
    } else if (state < 2) {
        if (state == 0) {
            *(float *)(st + 0x10) = FLOAT_80617440;
            *(unsigned int *)(self + 0x28) &= 0xfbffffff;
        } else if (-1 < state) {
            *(float *)(st + 0x10) = FLOAT_8061758c;
            *(unsigned int *)(self + 0x28) &= 0xfbffffff;
        }
    } else if (state != 4 && state < 4) {
        *(float *)(st + 0x10) = *(float *)(st + 0x10) - delta;
        if (*(float *)(st + 0x10) <= zero) {
            *(float *)(st + 0x10) = zero;
            *(int *)(st + 0xa0) = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801e2f5c  SHUT panel offset scaled by FLOAT_80617590.
double ekun_plate_ratio(void)
{
    int shut = (int)FUN_8023c32c(0x53485554);   // 'SHUT'
    int st;
    if (shut == 0 || (st = *(int *)(shut + 0x7c)) == 0) {
        return (double)FLOAT_80617440;
    }
    if (*(int *)(st + 0xa0) == 4) {
        return (double)FLOAT_80617440;
    }
    return (double)(*(float *)(st + 0x10) * FLOAT_80617590);
}

// ---------------------------------------------------------------------------
// 0x801e2fc0  build the face-table matrix from the plate ratio.
void ekun_face_matrix_setup(int self)
{
    int mtx = __ws_mtx_pool_alloc();
    collision_alloc_face_table(*(int **)(self + 4));
    int coll = *(int *)(*(int *)(self + 4) + 0x1e8);
    *(unsigned int *)(coll + 0x1c) |= 0x8000;
    double ratio = ekun_plate_ratio();
    __ws_mtx_set_translation(mtx,
        (double)(float)((double)FLOAT_80617598 * ratio + (double)FLOAT_80617594),
        (double)FLOAT_80617440, (double)FLOAT_80617440);
    collision_set_face_entry((u32)*(int *)(self + 4), 0, (u32)mtx);
}

// ---------------------------------------------------------------------------
// 0x801e3048  reset ekun spawn scratch, forward to shared spawn helper.
void ekun_spawn_reset_wrapper(double a1, double a2, double a3, double a4,
                              double a5, double a6, double a7, double a8)
{
    DAT_806126cc = -1;
    FUN_803668a8(a1, a2, a3, a4, a5, a6, a7, a8);
}

// ---------------------------------------------------------------------------
// 0x801e3094  drive the EKUN motor sound on rising progress.
void ekun_motor_sound_step(double a1, double a2, double a3, double a4,
                           double a5, double a6, double a7, double a8, int bone)
{
    double drv = a1;
    double ratio = worm_ekun_progress_ratio();
    if ((double)FLOAT_80617440 < ratio) {
        double cur = (double)(float)FUN_80390324(ratio);
        double peak = (double)(float)FUN_80390324((double)FLOAT_80610d7c);
        if (cur < peak) {
            FLOAT_80610d7c = (float)FUN_80390324(ratio);
            int params[16];
            sound_init_play_params((int)params);
            *(unsigned char *)((int)params + 0x24) = 1;   // local_30
            params[0] = 0x3a5;
            *(float *)((int)params + 0x28) = FLOAT_8061757c;  // local_2c
            params[3] = *(int *)(bone + 100);              // local_3c
            params[4] = *(int *)(bone + 0x68);             // local_38
            params[5] = *(int *)(bone + 0x6c);             // local_34
            sound_play_request(params);
            drv = 0;
        }
    }
    FUN_803669b8(drv, a2, a3, a4, a5, a6, a7, a8);
}

// ---------------------------------------------------------------------------
// 0x801e3160  EKUN enter-menu gate (MACG spawn point).
void ekun_enter_menu_gate(double a1, double a2, double a3, double a4,
                          double a5, double a6, double a7, double a8,
                          unsigned int a9, int holder)
{
    bool inMenu = false;
    int off = 0;
    int n = DAT_80612610;
    if (0 < DAT_80612610) {
        do {
            if (*(int *)(DAT_80611d78 + off + 0x114) == 0x100 &&
                *(int *)(DAT_80611d78 + off + 0x1390) == 0x1c) {
                inMenu = true;
            }
            off = off + 0x141c;
            n = n - 1;
        } while (n != 0);
    }
    if (holder != 0 && *(int *)(holder + 0x150) != 0 && !inMenu) {
        double ratio = worm_ekun_progress_ratio();
        if ((double)FLOAT_80617440 < ratio) {
            int sp = spawn_point_find_by_entity(0x4743414d);   // 'MACG'
            player_enter_menu_state((u32)*(int *)(holder + 0x150), (u32)sp, 0, 2, 0x1c, 0);
            FUN_80221074((double)FLOAT_80617440, a2, a3, a4, a5, a6, a7, a8);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801e322c  EKUN confirm-menu (MACF spawn point).
void ekun_confirm_menu(double a1, double a2, double a3, double a4,
                       double a5, double a6, double a7, double a8,
                       unsigned int a9, int holder)
{
    int params[12];
    if (holder != 0 && *(int *)(holder + 0x150) != 0) {
        if (*(int *)(*(int *)(holder + 0x150) + 0x114) == 0x100 ||
            FUN_80241874() == 0) {
            FUN_8024206c();
        } else {
            int sp = spawn_point_find_by_entity(0x4643414d);   // 'MACF'
            player_enter_menu_state((u32)*(int *)(holder + 0x150), (u32)sp, 0, 2, 0x1d, 0);
            sound_init_play_params((int)params);
            params[0] = 0x39f;
            sound_play_request(params);
            FUN_80221074((double)FLOAT_80617440, a2, a3, a4, a5, a6, a7, a8);
            FUN_8024188c(holder);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801e32ec  NUKE reset: seed plate state, exit menus, stop persistent sound.
void ekun_nuke_reset(double a1, double a2, double a3, double a4,
                     double a5, double a6, double a7, double a8)
{
    int nuke = (int)FUN_8023c32c(0x4e554b45);   // 'NUKE'
    float seed = FLOAT_80617510;
    if (nuke != 0) {
        int st = *(int *)(nuke + 0x7c);
        int state = *(int *)(st + 0xa0);
        if (state == 0 || state == 2) {
            *(float *)(*(int *)(nuke + 0x7c) + 0x9c) = FLOAT_80617440;
            *(int *)(*(int *)(nuke + 0x7c) + 0xa0) = 1;
            *(float *)(*(int *)(nuke + 0x7c) + 0x10) = seed;
            FUN_80221074((double)FLOAT_80617440, a2, a3, a4, a5, a6, a7, a8);
            FUN_802212b4(5);
        }
        int off = 0;
        for (int i = 0; i < DAT_80612610; i = i + 1) {
            if (*(int *)(DAT_80611d78 + off + 0x114) == 0x100 &&
                *(int *)(DAT_80611d78 + off + 0x1390) == 0x1c) {
                player_exit_menu_state();
            }
            off = off + 0x141c;
        }
    }
    __ws_stop_persistent();
}

// ---------------------------------------------------------------------------
// 0x801e33c8  subobj shell-eject: count down, then spawn burst + destroy.
void ekun_shell_eject_destroy(unsigned int *self)
{
    float zero = FLOAT_80617440;
    double full = (double)FLOAT_80617428;
    double frameDelta = (double)FLOAT_806108ec;

    // field 0x69 (eject timer) counts down by frameDelta*(full/framecount)
    self[0x69] = (unsigned int)(int)(float)(-(float)(
        frameDelta * (double)(float)(full / (double)(float)(
            -(double)(int)DAT_806109c0 - DOUBLE_80617420)) -
        (double)(float)self[0x69]));

    if ((float)self[0x69] <= zero) {
        int m;
        float dir[3];
        float pos[3];
        m = subobj_get_object_matrix(self[0x20]);
        dir[0] = -*(float *)(m + 0x10);
        m = subobj_get_object_matrix(self[0x20]);
        dir[1] = -*(float *)(m + 0x14);
        m = subobj_get_object_matrix(self[0x20]);
        dir[2] = -*(float *)(m + 0x18);
        pos[0] = (float)self[0x19] + (float)self[0x6a];
        pos[1] = (float)self[0x1a] + (float)self[0x6b];
        pos[2] = (float)self[0x1b] + (float)self[0x6c];
        __ws_explosion_spawn(10.0, 0x17, pos, dir, (int)self[0x84], (int)self, 1);
        unsigned int r = __ws_rng_tick();
        __ws_shell_batch((double)(float)self[0x86], 0, 0, 0, 0, 0, 0, 0,
                         0xd, pos, dir, (int)((r & 3) + 4));
        entity_destroy_with_event((int *)self);
    }
}

// ---------------------------------------------------------------------------
// 0x801e34f4  FBRL breakable-fragment scatter.
void ekun_fragment_scatter(double a1, double a2, double a3, double a4,
                           double a5, double a6, double a7, double a8)
{
    int frame = FUN_803826dc();
    int fr = *(int *)(frame + 4);
    int fr2 = *(int *)(frame + 8);
    unsigned char fragArr[68];   // auStack_11c: collected fragment ptr array

    if (((fr != 0 && *(int *)(fr + 0x150) != 0) ||
         (fr2 != 0 && *(int *)(fr2 + 0x150) != 0))) {
        unsigned int count = FUN_8023c59c(0x4642524c, (int)fragArr, 0x10);   // 'FBRL'
        if (count != 0) {
            // centroid of the fragment contact origins
            double cx = (double)FLOAT_80617440;
            double cy = (double)FLOAT_80617440;
            double cz = (double)FLOAT_80617440;
            int idx = count << 2;
            unsigned int k = count;
            while (k != 0) {
                k = k - 1;
                float *o = (float *)subobj_get_contact_origin(
                    *(unsigned int *)(*(int *)(fragArr + idx - 4) + 0x80));
                cx = (double)(float)(cx + (double)o[0]);
                cy = (double)(float)(cy + (double)o[1]);
                cz = (double)(float)(cz + (double)o[2]);
                idx = idx - 4;
            }
            double invN = (double)(float)((double)FLOAT_80617428 /
                (double)(float)(-(double)(int)count - DOUBLE_80617420));
            double mx = (double)(float)(cx * invN);
            double my = (double)(float)(cy * invN);
            double mz = (double)(float)(cz * invN);
            double heightBias = (double)FLOAT_8061759c;
            double full = (double)FLOAT_80617428;
            double dragBase = (double)FLOAT_806174dc;
            double dragScale = (double)FLOAT_806174c4;
            double randSpan = (double)FLOAT_8061742c;
            double randBias = DOUBLE_80617438;

            idx = count << 2;
            while (count != 0) {
                count = count - 1;
                unsigned int h = *(unsigned int *)(*(int *)(fragArr + idx - 4) + 0x80);
                float *o = (float *)subobj_get_contact_origin(h);
                unsigned int fl = subobj_get_flags(h);
                subobj_set_flags(h, (fl & 0xffbfffff) | 0x100);
                entity_set_render_type(*(int *)(fragArr + idx - 4), (int)&subobj_break_tick);
                entity_register_for_update(*(int *)(fragArr + idx - 4), 7, (int)0x801e34f4);
                *(float *)(*(int *)(fragArr + idx - 4) + 0x1cc) = FLOAT_80617440;
                *(float *)(*(int *)(fragArr + idx - 4) + 0x1d8) = FLOAT_80617440;
                FUN_80221074((double)FLOAT_80617440, a2, a3, a4, a5, a6, a7, a8);
                unsigned int r = __ws_rng_tick();
                if (r == (r / 3) * 3) {
                    entity_register_for_update(*(int *)(fragArr + idx - 4), 3, (int)0x801e33c8);
                    double spinBase = (double)FLOAT_80610d88;
                    unsigned int r2 = __ws_rng_tick();
                    *(float *)(*(int *)(fragArr + idx - 4) + 0x1a4) =
                        (float)((double)(float)((double)FLOAT_80610d8c - spinBase) *
                                (double)((float)((double)(int)r2 - DOUBLE_80617438) /
                                         FLOAT_8061742c) + spinBase);
                }
                float imp[3];
                imp[0] = (float)((double)o[0] - mx) * FLOAT_806126ac;
                imp[1] = (float)((double)o[1] - my) * FLOAT_80610d80;
                float dz = (float)((double)o[2] - mz);
                imp[2] = FLOAT_80610d84 *
                         (float)((double)(float)(full + (double)(float)(heightBias + (double)o[1])) *
                                 ABS((double)dz));
                unsigned int r3 = __ws_rng_tick();
                double drag = (double)(float)(dragBase *
                    (double)(float)((double)(float)((double)(int)r3 - randBias) / randSpan) +
                    dragScale);
                imp[0] = (float)((double)imp[0] * drag);
                imp[1] = (float)((double)imp[1] * drag);
                imp[2] = (float)((double)imp[2] * drag);
                subobj_set_linear_impulse(h, (unsigned int *)imp);
                idx = idx - 4;
            }
        }
    }
    FUN_80382728();
}

// ===========================================================================
// batch_615  0x801e3870 - 0x801e3f4c  ARTR articulated trap-door + LPTR lift
// mechanisms (CDH1 set-piece). ARTR proximity-ready predicate, its anim-event
// open/close/set-speed handlers (gated by FUN_80232740 anim-event frame test),
// the ARTR physics/settle per-frame update that drives its zone sound anchors,
// and the LPTR lift open + big per-frame settle update. subsystem:game,
// worm-effect module.
//
//   artr_is_settled           (0x801e3870) predicate: ARTR near its rest offset
//                             (FLOAT_806174c4) and nearly stopped.
//   artr_anim_stop            (0x801e38e4) anim-event: on frame hit, zero the
//                             ARTR drive via FUN_80221074.
//   artr_anim_set_pos_b       (0x801e3918) anim-event: set ARTR offset 0x18 from
//                             the event payload, state 1.
//   artr_anim_set_speed       (0x801e3988) anim-event: set ARTR speed 0x9c to
//                             FLOAT_806175a4.
//   artr_anim_open            (0x801e39e8) anim-event: seed ARTR offsets 0x10/
//                             0x18 from payload, clear speed, state 1.
//   artr_settle_update        (0x801e3a68) ARTR per-frame settle: integrate the
//                             door offset toward its target with spring/clamp
//                             over DAT_806108e4 substeps, advance state, then
//                             push the computed height into the 0x12 zone sound
//                             anchors (DAT_804529ec table) and 0xc AR** entities
//                             (DAT_80452ac4 table).
//   artr_anim_set_target      (0x801e3dd4) anim-event: copy payload into the
//                             ARTR collision target field 0x30.
//   lptr_anim_open            (0x801e3e38) LPTR anim-event open: bias offset
//                             0x18, reset DAT_80610d90 timer, kick FUN_801e4468/
//                             FUN_801e4374.
//   lptr_anim_set_pos         (0x801e3eb8) LPTR anim-event: set offset 0x10/0x18
//                             from payload (or 0), clear speed.
//   lptr_settle_update        (0x801e3f4c) LPTR per-frame settle: integrate the
//                             lift offset (spring/clamp), drive FLWG child, tick
//                             the DAT_80610d90 timer, push height into 0xc zone
//                             sound anchors (DAT_80452b54) + fixed pads, and wake
//                             the ARF2/ARB2 team actors.
// Non-matching: FP-context spill frames FUN_803826e4/FUN_80382730 and the
// __save_gpr_25/__restore_gpr_25 register-save frame kept as opaque open/close
// (the frame pointer they surface is this entity 'self'). 0x43300000 magic-bias
// reduced to (double)(int) casts. soundpad_resolve_entity_to_zone returns void*
// (cast to int at each use).
// ===========================================================================

extern "C" {
extern int   DAT_80610d90;      // LPTR open-delay timer
extern float FLOAT_80617450;    // LPTR open accel
extern float FLOAT_80617514;    // door offset -> height scale
extern float FLOAT_806174bc;    // fixed pad anchor height
extern float FLOAT_806175a0;    // ARTR settled position epsilon
extern float FLOAT_806175a4;    // ARTR set-speed constant
extern float FLOAT_806175a8;    // ARTR open accel
extern float FLOAT_806175ac;    // ARTR speed floor
extern float FLOAT_806175b0;    // ARTR overshoot damp
extern float FLOAT_806175b4;    // ARTR settle position epsilon
extern float FLOAT_806175b8;    // ARTR height bias
extern float FLOAT_806175bc;    // ARTR visibility height gate
extern float FLOAT_806175c0;    // ARTR zone-anchor height bias
extern float FLOAT_806175c4;    // ARTR entity-anchor height bias
extern float FLOAT_806175c8;    // LPTR open offset bias
extern float FLOAT_806175cc;    // LPTR overshoot damp
extern float FLOAT_806175d0;    // FLWG child height bias
extern float FLOAT_806175d4;    // LPTR zone-anchor height bias
extern float FLOAT_806175d8;    // LPTR zone-anchor pan
extern float FLOAT_8061754c;    // ARTR close accel

extern int   DAT_804529ec;      // ARTR zone-anchor table (0xc stride x0x12: id,dz,pan)
extern int   DAT_804529f0;
extern int   DAT_804529f4;
extern int   DAT_80452ac4;      // ARTR AR** entity table (0xc stride x0xc: id,dz,rot)
extern int   DAT_80452ac8;
extern int   DAT_80452acc;
extern int   DAT_80452b54;      // LPTR zone-anchor table (0xc stride x0xc: id,dz)
extern int   DAT_80452b58;

extern int   FUN_80232740(int event);   // anim-event frame test (bool)
extern void  FUN_801e4468(double a, double b, double c, double d,
                          double e, double f, double g, double h); // LPTR sub-driver
extern void  FUN_801e4374(int mode);     // LPTR mode set
extern void  FUN_801e4684(void);         // LPTR timer-expiry hook
extern int   __save_gpr_25(void);        // register-save frame open (returns self)
extern void  __restore_gpr_25(void);     // register-save frame close
} // extern "C"

// FUN_803826e4 was declared void earlier (batch_612, used as a bare spill-open);
// here the LPTR settle reads the frame pointer it returns. Alias with int return.
static int (*const __ws_spill_e4_frame)(void) = (int (*)(void))&FUN_803826e4;

// FUN_803826e4 / FUN_80382730 spill frames are already declared earlier in this
// file (batch_612). soundpad_resolve_entity_to_zone returns void* per the shared
// header; cast at each use.

// ---------------------------------------------------------------------------
// 0x801e3870  ARTR settled-and-near-rest predicate.
unsigned int artr_is_settled(void)
{
    int artr = (int)FUN_8023c32c(0x41525452);   // 'ARTR'
    if (artr == 0) {
        return 0;
    }
    int st = *(int *)(artr + 0x7c);
    if (ABS(FLOAT_806174c4 - *(float *)(st + 0x10)) < FLOAT_806175a0 &&
        ABS(*(float *)(st + 0x9c)) < FLOAT_806174fc) {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x801e38e4  ARTR anim-event: zero the drive on frame hit.
void artr_anim_stop(double a1, double a2, double a3, double a4,
                    double a5, double a6, double a7, double a8, int event)
{
    if (FUN_80232740(event)) {
        FUN_80221074((double)FLOAT_80617440, a2, a3, a4, a5, a6, a7, a8);
    }
}

// ---------------------------------------------------------------------------
// 0x801e3918  ARTR anim-event: set offset 0x18 from payload, state 1.
void artr_anim_set_pos_b(int *event)
{
    int artr = (int)FUN_8023c32c(0x41525452);   // 'ARTR'
    if (artr != 0 && FUN_80232740((int)event)) {
        int st = *(int *)(artr + 0x7c);
        *(float *)(st + 0x18) = -*(float *)(*event + 0xc);
        *(int *)(st + 0xa0) = 1;
    }
}

// ---------------------------------------------------------------------------
// 0x801e3988  ARTR anim-event: set speed 0x9c.
void artr_anim_set_speed(int event)
{
    int artr = (int)FUN_8023c32c(0x41525452);   // 'ARTR'
    if (artr != 0 && FUN_80232740(event)) {
        *(float *)(*(int *)(artr + 0x7c) + 0x9c) = FLOAT_806175a4;
    }
}

// ---------------------------------------------------------------------------
// 0x801e39e8  ARTR anim-event: open (seed offsets 0x10/0x18, clear speed).
void artr_anim_open(int *event)
{
    int artr = (int)FUN_8023c32c(0x41525452);   // 'ARTR'
    float zero = FLOAT_80617440;
    if (artr != 0 && FUN_80232740((int)event)) {
        int st = *(int *)(artr + 0x7c);
        *(float *)(st + 0x10) = -*(float *)(*event + 0xc);
        *(int *)(st + 0x18) = *(int *)(st + 0x10);
        *(float *)(st + 0x9c) = zero;
        *(int *)(st + 0xa0) = 1;
    }
}

// ---------------------------------------------------------------------------
// 0x801e3a68  ARTR per-frame settle + zone/entity sound anchors.
void artr_settle_update(void)
{
    int self = __save_gpr_25();
    float accelOpen = FLOAT_806175a8;
    float speedMax = FLOAT_806175a4;
    float accelClose = FLOAT_8061754c;
    float stopEps = FLOAT_80617460;
    float zero = FLOAT_80617440;
    float full = FLOAT_80617428;
    double base = DOUBLE_80617420;
    int st = *(int *)(self + 0x7c);

    for (int step = 0; step < DAT_806108e4; step = step + 1) {
        float spd = *(float *)(st + 0x9c);
        float dt = full / (float)(-(double)(int)*(unsigned int *)(st + 0xa0) - base);
        if (spd <= speedMax) {
            if (FLOAT_806175ac <= spd) {
                if (*(float *)(st + 0x10) <= *(float *)(st + 0x18)) {
                    if (*(float *)(st + 0x10) < *(float *)(st + 0x18)) {
                        *(float *)(st + 0x9c) =
                            accelOpen * dt *
                            (full / (float)(-(double)(int)DAT_806109c0 - base)) + spd;
                    }
                } else {
                    *(float *)(st + 0x9c) =
                        -(accelClose * dt *
                          (full / (float)(-(double)(int)DAT_806109c0 - base)) - spd);
                }
            } else {
                *(float *)(st + 0x9c) = FLOAT_806175ac;
            }
        } else {
            *(float *)(st + 0x9c) = speedMax;
        }

        if ((zero < *(float *)(st + 0x9c) &&
             *(float *)(st + 0x18) < *(float *)(st + 0x10) + dt) ||
            (*(float *)(st + 0x9c) < FLOAT_80617440 &&
             *(float *)(st + 0x10) - dt < *(float *)(st + 0x18))) {
            *(float *)(st + 0x9c) = *(float *)(st + 0x9c) * FLOAT_806175b0;
        }
        if (ABS(*(float *)(st + 0x9c)) < stopEps &&
            ABS(*(float *)(st + 0x18) - *(float *)(st + 0x10)) < FLOAT_806175b4) {
            *(float *)(st + 0x9c) = FLOAT_80617440;
            *(int *)(st + 0x10) = *(int *)(st + 0x18);
        }
        if ((*(float *)(st + 0x9c) < zero && zero < spd) ||
            (FLOAT_80617440 < *(float *)(st + 0x9c) && spd < FLOAT_80617440)) {
            int ns = *(int *)(st + 0xa0) + 1;
            *(int *)(st + 0xa0) = (ns < 5) ? ns : 5;
        }
        *(float *)(st + 0x10) =
            *(float *)(st + 0x9c) *
            (full / (float)(-(double)(int)DAT_806109c0 - base)) +
            *(float *)(st + 0x10);
    }

    *(float *)(self + 100) = FLOAT_80617514 * *(float *)(st + 0x10) + FLOAT_806175b8;
    if (DAT_80611d54 == 0 ||
        ABS(*(float *)(self + 100) - *(float *)(DAT_80611d54 + 0x27c)) <= FLOAT_806175bc) {
        *(unsigned int *)(self + 0x28) |= 1;
    } else {
        *(unsigned int *)(self + 0x28) &= 0xfffffffe;
    }

    // 0x12 zone sound anchors
    double zbias = (double)FLOAT_806175c0;
    int zoff = 0;
    for (int i = 0; i < 0x12; i = i + 1) {
        int zone = (int)soundpad_resolve_entity_to_zone(*(int *)((int)&DAT_804529ec + zoff));
        *(float *)(zone + 0xc) =
            (float)(zbias + (double)*(float *)(self + 100)) +
            *(float *)((int)&DAT_804529f0 + zoff);
        *(int *)(zone + 0x10) = *(int *)((int)&DAT_804529f4 + zoff);
        *(unsigned short *)(zone + 10) = (unsigned short)((i < 0xc) ? 0x25 : 0x44);
        zoff = zoff + 0xc;
    }

    // 0xc AR** entity anchors
    double ebias = (double)FLOAT_806175c4;
    int eoff = 0;
    for (int i = 0; i < 0xc; i = i + 1) {
        int ent = (int)FUN_8023c32c(*(unsigned int *)((int)&DAT_80452ac4 + eoff));
        if (ent != 0) {
            *(float *)(ent + 100) =
                (float)(ebias + (double)*(float *)(self + 100)) +
                *(float *)((int)&DAT_80452ac8 + eoff);
            *(int *)(ent + 0x68) = *(int *)((int)&DAT_80452acc + eoff);
            *(unsigned int *)(ent + 0x28) |= 0x40020000;
            *(int *)(ent + 0x34) = 0xd5;
        }
        eoff = eoff + 0xc;
    }

    if (DAT_806108e4 != 0) {
        *(unsigned int *)(self + 0x18) |= 2;
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// 0x801e3dd4  ARTR anim-event: copy payload into collision target field 0x30.
void artr_anim_set_target(int *event)
{
    int artr = (int)FUN_8023c32c(0x41525452);   // 'ARTR'
    if (artr != 0 && FUN_80232740((int)event)) {
        *(int *)(*(int *)(artr + 4) + 0x30) = *(int *)(*event + 0xc);
    }
}

// ---------------------------------------------------------------------------
// 0x801e3e38  LPTR anim-event open.
void lptr_anim_open(double a1, double a2, double a3, double a4,
                    double a5, double a6, double a7, double a8, int *event)
{
    int lptr = (int)FUN_8023c32c(0x4c505452);   // 'LPTR'
    if (lptr != 0 && FUN_80232740((int)event)) {
        double bias = (double)FLOAT_806175c8;
        *(float *)(*(int *)(lptr + 0x7c) + 0x18) =
            (float)(bias + (double)*(float *)(*event + 0xc));
        DAT_80610d90 = 0;
        FUN_801e4468(bias, a2, a3, a4, a5, a6, a7, a8);
        FUN_801e4374(1);
    }
}

// ---------------------------------------------------------------------------
// 0x801e3eb8  LPTR anim-event: set offset 0x10/0x18 from payload.
void lptr_anim_set_pos(int *event)
{
    int lptr = (int)FUN_8023c32c(0x4c505452);   // 'LPTR'
    float zero = FLOAT_80617440;
    if (lptr != 0 && FUN_80232740((int)event)) {
        int st = *(int *)(lptr + 0x7c);
        *(float *)(st + 0x10) = FLOAT_806175c8 + *(float *)(*event + 0xc);
        if (zero == *(float *)(*event + 0xc)) {
            *(float *)(st + 0x10) = zero;
        }
        *(int *)(st + 0x18) = *(int *)(st + 0x10);
        *(float *)(st + 0x9c) = FLOAT_80617440;
    }
}

// ---------------------------------------------------------------------------
// 0x801e3f4c  LPTR per-frame settle + FLWG child + zone anchors + AR*2 wake.
void lptr_settle_update(void)
{
    int frame = __ws_spill_e4_frame();
    int st = *(int *)(frame + 0x7c);
    float spd = *(float *)(st + 0x9c);
    double base = DOUBLE_80617420;
    float full = FLOAT_80617428;
    float frac = full / (float)(-(double)(int)DAT_806109c0 - base);
    float delta = (float)(-(double)(int)DAT_806108e4 - base);

    if (spd <= FLOAT_80617428) {
        if (FLOAT_806175ac <= spd) {
            if (*(float *)(st + 0x10) <= *(float *)(st + 0x18)) {
                if (*(float *)(st + 0x10) < *(float *)(st + 0x18)) {
                    *(float *)(st + 0x9c) = FLOAT_80617450 * delta * frac + spd;
                }
            } else {
                *(float *)(st + 0x9c) = -(FLOAT_806174fc * delta * frac - spd);
            }
        } else {
            *(float *)(st + 0x9c) = FLOAT_806175ac;
        }
    } else {
        *(float *)(st + 0x9c) = FLOAT_80617428;
    }

    if ((FLOAT_80617440 < *(float *)(st + 0x9c) &&
         *(float *)(st + 0x18) < *(float *)(st + 0x10)) ||
        (*(float *)(st + 0x9c) < FLOAT_80617440 &&
         *(float *)(st + 0x10) < *(float *)(st + 0x18))) {
        *(float *)(st + 0x9c) = *(float *)(st + 0x9c) * FLOAT_806175cc;
    }
    if (ABS(*(float *)(st + 0x9c)) < FLOAT_80617460 &&
        ABS(*(float *)(st + 0x18) - *(float *)(st + 0x10)) < FLOAT_806175b4) {
        *(float *)(st + 0x9c) = FLOAT_80617440;
        *(int *)(st + 0x10) = *(int *)(st + 0x18);
    }
    float hscale = FLOAT_80617514;
    *(float *)(st + 0x10) = delta * *(float *)(st + 0x9c) * frac + *(float *)(st + 0x10);
    *(float *)(frame + 100) = hscale * *(float *)(st + 0x10);

    int flwg = (int)FUN_8023c32c(0x464c5747);   // 'FLWG'
    if (flwg != 0) {
        *(float *)(flwg + 100) = FLOAT_806175d0 + *(float *)(frame + 100);
        *(unsigned int *)(flwg + 0x28) |= 0x40000000;
    }

    if (-1 < (int)DAT_80610d90) {
        unsigned int t = (unsigned int)DAT_80610d90 - (unsigned int)DAT_806108e4;
        DAT_80610d90 = (int)(t & ((unsigned int)((int)(-t & ~t) >> 0x1f)));
        FUN_801e4684();
    }

    double zbias = (double)FLOAT_806175d4;
    double zpan = (double)FLOAT_806175d8;
    int zoff = 0;
    for (int i = 0; i < 0xc; i = i + 1) {
        int *pid = (int *)((int)&DAT_80452b54 + zoff);
        float h = (float)(zbias + (double)*(float *)(frame + 100)) +
                  *(float *)((int)&DAT_80452b58 + zoff);
        int z = (int)soundpad_resolve_entity_to_zone(*pid);
        *(float *)(z + 0xc) = h;
        z = (int)soundpad_resolve_entity_to_zone(*pid);
        *(unsigned short *)(z + 10) = 0x61;
        z = (int)soundpad_resolve_entity_to_zone(*pid);
        *(float *)(z + 0x10) = (float)zpan;
        zoff = zoff + 0xc;
    }

    // fixed pad pans
    int z;
    z = (int)soundpad_resolve_entity_to_zone(0x60b); *(float *)(z + 0x10) = FLOAT_806174bc;
    z = (int)soundpad_resolve_entity_to_zone(0x60c); *(float *)(z + 0x10) = FLOAT_806174bc;
    z = (int)soundpad_resolve_entity_to_zone(0x61f); *(float *)(z + 0x10) = FLOAT_806174bc;
    z = (int)soundpad_resolve_entity_to_zone(0x620); *(float *)(z + 0x10) = FLOAT_806174bc;

    // wake the ARF2 / ARB2 team actors
    for (int pass = 0; pass < 2; pass = pass + 1) {
        int actor = aiactor_find_player_by_team((pass == 0) ? 0x41524632 : 0x41524232); // ARF2 / ARB2
        if (actor != 0) {
            int ast = *(int *)(actor + 0x7c);
            *(unsigned int *)(actor + 0x10) |= 1;
            *(unsigned int *)(actor + 0x28) |= 0x200000;
            *(int *)(actor + 0x34) = 0x61;
            *(float *)(ast + 0xb3c) = FLOAT_80617440;
            int child = *(int *)(ast + 700);
            if (child != 0) {
                *(unsigned int *)(child + 0x10) |= 1;
                *(unsigned int *)(*(int *)(ast + 700) + 0x28) |= 0x200000;
                *(int *)(*(int *)(ast + 700) + 0x34) = 0x61;
            }
        }
    }

    if (DAT_806108e4 != 0) {
        *(unsigned int *)(frame + 0x18) |= 2;
    }
    FUN_80382730();
}
