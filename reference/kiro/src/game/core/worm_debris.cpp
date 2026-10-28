// worm_debris.cpp
// CDH1 "LPTR" articulated debris / falling-object set-piece. A fixed 12-entry
// pool (DAT_805c4f54, stride 0xc: {float fill/lifetime, int* nodePtr,
// uint entityHandle}) of scripted breakable pieces spawned from the level's
// 'PTPL'/'CBLK' object defs, positioned each frame relative to the parent
// 'LPTR' rig, and decremented toward a swept impact/dropped-weapon effect.
// Address range 0x801e4374 - 0x801e4ee0 (batch_616), continuation of the
// worm-effect / CDH1 scripted-prop cluster (worm_shutter.cpp is the sibling
// 'SHUT' set-piece). subsystem:game, worm-effect module.
//
// Functions:
//   debris_set_render_flags     (0x801e4374) walk the 12-entry piece pool and
//                               either OR render flags |0x61 (enable) or clear
//                               &~0x61 (disable) on every live piece entity;
//                               drops stale handles.
//   debris_spawn_pieces         (0x801e4468) iterate the level object def table;
//                               for each 'PTPL'/'CBLK' def spawn a piece entity
//                               (cap 12), record its handle + def-body node ptr
//                               + seed fill = FLOAT_80617440 into the pool.
//   debris_seed_fill_timers     (0x801e4530) per-frame: publish the budget
//                               DAT_80610d90 = frame-count*0x28, then for each
//                               live piece roll a rng-scattered fill/lifetime
//                               value into the pool.
//   debris_pieces_update        (0x801e4684) per-frame positioning: resolve the
//                               'LPTR' rig, build its local matrix, and for each
//                               live piece (type 0x34a swinging / 800 static /
//                               default sub-object) transform its anchor into
//                               world space, sync the sub-object contact origin,
//                               decrement the fill timer and on expiry fire a
//                               swept projectile-impact or dropped-weapon effect.
//   debris_spin_accumulate      (0x801e4a74) integrate a spin phase (+0xd0) by a
//                               frame-time-scaled rate.
//   debris_rig_reset            (0x801e4ad4) reset the piece bone matrix palette
//                               to identity and rotate joints 1 & 2 about the
//                               body pivot points.
//   debris_spark_tick_cb        (0x801e4bc8) sub-object extra-word tick callback:
//                               throttled (once per frame, distance-gated) spark
//                               impact 0x3b4 at the piece contact point.
//   debris_install_spark_cb     (0x801e4d68) install debris_spark_tick_cb on the
//                               piece sub-object.
//   debris_spin_ramp_update     (0x801e4da0) 2-state spin ramp (+0x1a0): ramp the
//                               spin phase +0xd0 up (state 1) toward a limit, or
//                               back down (state 2) toward zero, and drive the
//                               render field +0x64 = base +0xa8 + phase.
//   debris_timer_texture_render (0x801e4ee0) render the 4-digit countdown texture
//                               into the scratch render target: build a strip
//                               table, draw each rolling digit at 3 vertical
//                               positions (rolling odometer), then blit into the
//                               piece material tex slot 0x90.
//
// Non-matching: FUN_8023c32c is the entity-by-4CC lookup (returns entity*, arg
// is the packed tag). The PPC FP-context spill frames FUN_803826c4/e4/e8 /
// FUN_803826f0 open + FUN_80382710/28/30/34/3c close are dead ABI plumbing kept
// as opaque open/close calls; the caller-saved-FP spill copies (in_fNN /
// in_psNN_1) the decompiler surfaced are dropped. The int<->double magic-bias
// idiom (CONCAT44(0x43300000,x^0x80000000) - DOUBLE_80617420) is reduced to a
// plain -(double)(int) cast. Several named helpers whose shared-header prototype
// has a reduced/guessed arity (spawn_create_entity, spawn_projectile_impact,
// weapon_dropped_start_effects, floor_height_from_position, mtx4_rotate_about_point,
// gx_set_color_update_mode, text_draw_char, FUN_80387fb8) are routed through
// file-local typed fn-ptr aliases (__wd_*) with the true arity/return, matching
// the worm_render.cpp / worm_shutter.cpp convention.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {

// --- raw data symbols (file-local, typed to use here) ---
extern int   DAT_80611c24;      // trigger/entity slot pool base
extern int   DAT_80611d78;      // CDH1 level-state block base
extern int   DAT_80612614;      // active game-mode / level id
extern int   DAT_80610d90;      // per-frame debris budget
extern int   DAT_80610d94;      // scratch render-target slot
extern int   DAT_80611928;      // per-frame frame stamp
extern int   DAT_806126b0;      // spark-throttle frame stamp
extern int   DAT_806126b4;      // countdown digit value latch
extern unsigned int DAT_806109c0; // frame-count numerator (magic-bias src)
extern unsigned int DAT_806108e4; // elapsed-count (magic-bias src)
extern float FLOAT_806108ec;    // per-frame delta seconds
extern int  *DAT_80610a20;      // active material table base ptr

extern int   DAT_805c4f54;      // piece pool: fill/lifetime (stride 0xc)
extern int   DAT_805c4f58;      // piece pool: node ptr
extern int   DAT_805c4f5c;      // piece pool: entity handle
extern float DAT_805c4fe4;      // last spark pos X
extern float DAT_805c4fe8;      // last spark pos Y
extern float DAT_805c4fec;      // last spark pos Z

extern float  FLOAT_80617428;   // 1.0f numerator
extern float  FLOAT_8061742c;
extern float  FLOAT_80617430;
extern float  FLOAT_80617440;   // 0.0f
extern float  FLOAT_806174c4;
extern float  FLOAT_80617508;   // spin ramp ceiling
extern float  FLOAT_8061755c;
extern float  FLOAT_80617574;
extern float  FLOAT_806175dc;   // fill scatter scale
extern float  FLOAT_806175e0;
extern float  FLOAT_806175e4;
extern float  FLOAT_806175e8;   // spin rate
extern float  FLOAT_806175ec;   // spark min dist^2
extern float  FLOAT_806175f0;   // spark move^2 gate
extern float  FLOAT_806175f4;   // spark ray back-scale
extern float  FLOAT_806175f8;   // countdown scale
extern float  FLOAT_806175fc;   // digit x spacing
extern float  FLOAT_80617600;   // digit roll threshold
extern float  FLOAT_80617604;   // digit y base
extern float  FLOAT_80617608;   // digit y step
extern double DOUBLE_80617420;  // magic-bias subtrahend
extern double DOUBLE_80617438;  // rng bias subtrahend

extern int   DAT_803df79c;      // rotation-pivot vec3 (3 words)
extern int   DAT_803df7a0;
extern int   DAT_803df7a4;
extern int   DAT_80610d98;      // digit-format template string

// --- raw code symbols (file-local; not present in the generated headers) ---
extern int  *FUN_8023c32c(int tag);          // entity lookup by 4CC
extern int   FUN_803826c4(void);             // spill frame open (returns frame)
extern void  FUN_80382710(void);             // spill frame close
extern int   FUN_803826e4(void);             // spill frame open (returns frame)
extern void  FUN_80382730(void);             // spill frame close
extern int   FUN_803826e8(void);             // spill frame open
extern void  FUN_80382734(void);             // spill frame close
extern unsigned long long FUN_803826f0(void);// spill frame open (packed pair)
extern void  FUN_8038273c(void);             // spill frame close
extern void  FUN_803669dc(void);             // scratch-RT finalize
extern int   FUN_80387fb8(double a, double b, void *c, void *d, void *e,
                          void *f, void *g, void *h, int buf, int *fmt,
                          int digit, int slot, unsigned int u0, unsigned int v0,
                          unsigned int u1, unsigned int v1); // sprintf-into-buf

} // extern "C" (raw symbols)

// --- typed fn-ptr aliases for shared-header prototypes with reduced arity ---
// spawn_create_entity(int*) in decls, but the real routine takes the packed
// spawn frame + the def entry and returns the new entity ptr.
static int (*const __wd_spawn_create_entity)(unsigned long long, double, double,
                                             void *, void *, void *, void *, void *,
                                             int defEntry, int index, int) =
    (int (*)(unsigned long long, double, double, void *, void *, void *, void *,
             void *, int, int, int))&spawn_create_entity;
// spawn_projectile_impact real call site passes 8 doubles + entity/idx/weaponType.
static void (*const __wd_projectile_impact)(double, double, double, double, double,
                                            double, double, double, int entity,
                                            int idx, int weaponType) =
    (void (*)(double, double, double, double, double, double, double, double,
              int, int, int))&spawn_projectile_impact;
// weapon_dropped_start_effects(void) in decls but the ROM call passes the FP
// spill frame verbatim; alias with the 8-double form (args unused downstream).
static void (*const __wd_weapon_dropped_fx)(double, double, double, double,
                                            double, double, double, double) =
    (void (*)(double, double, double, double, double, double, double, double))
    &weapon_dropped_start_effects;
// floor_height_from_position(float*, float) in decls; the ROM sentinel call
// passes two doubles (unused query result).
static int (*const __wd_floor_height)(double, double) =
    (int (*)(double, double))&floor_height_from_position;
// mtx4_rotate_about_point real form is (mtxDest, pivotVec3, srcVec3).
static void (*const __wd_rotate_about_point)(int, int *, float *) =
    (void (*)(int, int *, float *))&mtx4_rotate_about_point;
// gx_set_color_update_mode(int) in decls; used no-arg (mode already latched).
static void (*const __wd_gx_color_update)(void) =
    (void (*)(void))&gx_set_color_update_mode;
// text_draw_char returns void in decls; the ROM routine returns the advance
// (used to thread the next draw origin).
static int (*const __wd_text_draw_char)(double, double, unsigned char *,
                                        unsigned int) =
    (int (*)(double, double, unsigned char *, unsigned int))&text_draw_char;
// rng_counter_tick is declared void but returns the fresh counter word.
static unsigned int (*const __wd_rng_tick)(void) =
    (unsigned int (*)(void))&rng_counter_tick;

// forward decl (spark cb installed by debris_install_spark_cb below)
void debris_spark_tick_cb(void);

// ---------------------------------------------------------------------------
// 0x801e4374  Enable/disable render flags on every live debris piece.
void debris_set_render_flags(int enable)
{
    int i;
    int count;
    unsigned int h;
    int entity;

    if (enable != 0) {
        i = 0;
        count = 0xc;
        do {
            h = *(unsigned int *)((int)&DAT_805c4f5c + i);
            if (h == 0) {
                entity = 0;
            } else {
                entity = DAT_80611c24 + (h >> 0x16) * 0x2e8;
                if (*(unsigned int *)(entity + 0x2c) != h) {
                    entity = 0;
                    *(int *)((int)&DAT_805c4f5c + i) = 0;
                }
            }
            if (entity != 0) {
                *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x61;
            }
            i += 0xc;
            count += -1;
        } while (count != 0);
        return;
    }

    i = 0;
    count = 0xc;
    do {
        h = *(unsigned int *)((int)&DAT_805c4f5c + i);
        if (h == 0) {
            entity = 0;
        } else {
            entity = DAT_80611c24 + (h >> 0x16) * 0x2e8;
            if (*(unsigned int *)(entity + 0x2c) != h) {
                entity = 0;
                *(int *)((int)&DAT_805c4f5c + i) = 0;
            }
        }
        if (entity != 0) {
            *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) & 0xffffff9e;
        }
        i += 0xc;
        count += -1;
    } while (count != 0);
}

// ---------------------------------------------------------------------------
// 0x801e4468  Spawn the 'PTPL'/'CBLK' debris pieces from the level object defs.
void debris_spawn_pieces(unsigned long long frame, double a2, double a3, void *a4,
                         void *a5, void *a6, void *a7, void *a8)
{
    int levelInfo;
    int idx;
    int off;
    int spawned;
    int slot;
    int defEntry;
    int entity;

    frame = FUN_803826e4();
    levelInfo = (int)level_get_info_cached(DAT_80612614);
    idx = 0;
    off = 0;
    spawned = 0;
    slot = 0;
    while (idx < *(int *)(levelInfo + 0x10) && spawned < 0xc) {
        defEntry = *(int *)(levelInfo + 0xc) + off;
        if (*(int *)(defEntry + 4) == 0x4c505450 /* 'PTPL' */ ||
            *(int *)(defEntry + 4) == 0x424c4b43 /* 'CBLK' */) {
            entity = __wd_spawn_create_entity(frame, a2, a3, a4, a5, a6, a7, a8,
                                              defEntry, idx, 0);
            *(int *)((int)&DAT_805c4f5c + slot) = *(int *)(entity + 0x2c);
            spawned += 1;
            *(int *)((int)&DAT_805c4f58 + slot) = defEntry + 0x10;
            *(float *)((int)&DAT_805c4f54 + slot) = FLOAT_80617440;
            slot += 0xc;
            frame = 0;   /* extraout carrier reset */
        }
        idx += 1;
        off += 0x60;
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x801e4530  Per-frame: publish budget + roll rng-scattered fill timers.
void debris_seed_fill_timers(void)
{
    int entity;
    unsigned int tick;
    unsigned int i;
    int off;

    i = 0;
    DAT_80610d90 = (int)DAT_806109c0 * 0x28;
    off = 0;
    do {
        unsigned int h = *(unsigned int *)((int)&DAT_805c4f5c + off);
        if (h == 0) {
            entity = 0;
        } else {
            entity = DAT_80611c24 + (h >> 0x16) * 0x2e8;
            if (*(unsigned int *)(entity + 0x2c) != h) {
                entity = 0;
                *(int *)((int)&DAT_805c4f5c + off) = 0;
            }
        }
        if (entity != 0) {
            /* both branches identical in ROM (type 800 == default) */
            tick = __wd_rng_tick();
            *(float *)((int)&DAT_805c4f54 + off) =
                FLOAT_806175dc *
                    ((float)((double)tick - DOUBLE_80617438) / FLOAT_8061742c) +
                FLOAT_806174c4 +
                (float)(-(double)(int)i);
        }
        i += 1;
        off += 0xc;
    } while ((int)i < 0xc);
}

// ---------------------------------------------------------------------------
// 0x801e4684  Per-frame positioning + fill decrement + impact/drop dispatch.
void debris_pieces_update(void)
{
    char c;
    int lptr;
    unsigned int h;
    float *nodePtr;
    int i;
    float *pfill;
    int entity;
    double dropRate;
    float wx, wy, wz;
    float mtx[17];

    FUN_803826e8();
    /* per-frame drop rate = elapsed * (1.0 / frameCount) */
    dropRate = (double)(float)((double)FLOAT_806108ec *
                    (double)(float)((double)FLOAT_80617428 /
                    (double)(float)(-(double)(int)DAT_806109c0)));

    lptr = (int)FUN_8023c32c(0x4c505452 /* 'LPTR' */);
    if (lptr == 0) {
        FUN_80382734();
        return;
    }

    entity_build_local_matrix(mtx, (void *)(unsigned long long)(unsigned)lptr, 1, 0);
    i = 0;
    do {
        pfill = (float *)((int)&DAT_805c4f54 + i);
        h = *(unsigned int *)((int)&DAT_805c4f5c + i);
        if (h == 0) {
            entity = 0;
        } else {
            entity = DAT_80611c24 + (h >> 0x16) * 0x2e8;
            if (*(unsigned int *)(entity + 0x2c) != h) {
                entity = 0;
                *(int *)((int)&DAT_805c4f5c + i) = 0;
            }
        }

        if (entity != 0 && (*(unsigned int *)(entity + 0x28) & 1) != 0) {
            entity_assign_room(entity);
            *(int *)(entity + 0x34) = *(int *)(entity + 0x38);
            c = *(char *)(entity + 0x30);
            *(char *)(entity + 0x30) = c + '\x01';
            *(int *)(entity + c * 4 + 0x38) = 0xd5;

            if (*(short *)(entity + 0x94) == 0x34a) {
                /* swinging piece: track node anchor through a sub-object */
                if (*(int *)(entity + 0x7c) == 0 && *(int *)(entity + 0x80) != 0) {
                    nodePtr = *(float **)((int)&DAT_805c4f58 + i);
                    wx = nodePtr[0];
                    wy = nodePtr[1];
                    wz = nodePtr[2];
                    { float p[3] = { wx, wy, wz }; mtx4_transform_point(mtx, p);
                      wx = p[0]; wy = p[1]; wz = p[2]; }
                    *(float *)(entity + 100) = wx;
                    *(float *)(entity + 0x68) = wy;
                    *(float *)(entity + 0x6c) = wz;
                    {
                        float contact[3];
                        contact[0] = wx + *(float *)(entity + 0x1a8);
                        contact[1] = wy + *(float *)(entity + 0x1ac);
                        contact[2] = wz + *(float *)(entity + 0x1b0);
                        subobj_set_contact_origin(*(unsigned int *)(entity + 0x80),
                                                  (unsigned int *)contact);
                    }
                    if (DAT_80610d90 != 0) {
                        double floor0 = (double)FLOAT_80617440;
                        if (floor0 < (double)*pfill) {
                            *pfill = (float)((double)*pfill - dropRate);
                            if ((double)*pfill <= floor0) {
                                __wd_projectile_impact(floor0,
                                    (double)FLOAT_806175e0, (double)FLOAT_806175e4,
                                    (double)FLOAT_8061755c, (double)FLOAT_80617428,
                                    (double)FLOAT_806174c4, 0.0, 0.0,
                                    entity, 0, 4);
                                *pfill = FLOAT_80617440;
                            }
                        }
                    }
                }
            } else if (*(short *)(entity + 0x94) == 800) {
                /* static drop piece: use dropped-weapon effect on expiry */
                if (*(int *)(entity + 0x80) == 0) {
                    float *n = *(float **)((int)&DAT_805c4f58 + i);
                    *(float *)(entity + 100) = n[0];
                    *(float *)(entity + 0x68) = n[1];
                    *(float *)(entity + 0x6c) = n[2];
                    mtx4_transform_point(mtx, (float *)(entity + 100));
                    if (DAT_80610d90 != 0) {
                        double floor0 = (double)FLOAT_80617440;
                        if (floor0 < (double)*pfill) {
                            *pfill = (float)((double)*pfill - dropRate);
                            if ((double)*pfill <= floor0) {
                                *pfill = FLOAT_80617440;
                                __wd_weapon_dropped_fx(floor0, 0.0, 0.0, 0.0,
                                                       0.0, 0.0, 0.0, 0.0);
                            }
                        }
                    }
                }
            } else {
                /* default piece: idle sub-object anchor + swept impact */
                if (*(unsigned int *)(entity + 0x80) == 0) {
                    float *n = *(float **)((int)&DAT_805c4f58 + i);
                    *(float *)(entity + 100) = n[0];
                    *(float *)(entity + 0x68) = n[1];
                    *(float *)(entity + 0x6c) = n[2];
                    mtx4_transform_point(mtx, (float *)(entity + 100));
                } else {
                    if (subobj_is_idle(*(unsigned int *)(entity + 0x80)) != 0) {
                        nodePtr = *(float **)((int)&DAT_805c4f58 + i);
                        wx = nodePtr[0];
                        wy = nodePtr[1];
                        wz = nodePtr[2];
                        { float p[3] = { wx, wy, wz }; mtx4_transform_point(mtx, p);
                          wx = p[0]; wy = p[1]; wz = p[2]; }
                        *(float *)(entity + 100) = wx;
                        *(float *)(entity + 0x68) = wy;
                        *(float *)(entity + 0x6c) = wz;
                        {
                            float contact[3];
                            contact[0] = wx + *(float *)(entity + 0x1a8);
                            contact[1] = wy + *(float *)(entity + 0x1ac);
                            contact[2] = wz + *(float *)(entity + 0x1b0);
                            subobj_set_contact_origin(*(unsigned int *)(entity + 0x80),
                                                      (unsigned int *)contact);
                        }
                    }
                }
                if (DAT_80610d90 != 0) {
                    double floor0 = (double)FLOAT_80617440;
                    if (floor0 < (double)*pfill) {
                        *pfill = (float)((double)*pfill - dropRate);
                        if ((double)*pfill <= floor0) {
                            __wd_projectile_impact(floor0,
                                (double)FLOAT_806175e0, (double)FLOAT_806175e4,
                                (double)FLOAT_8061755c, (double)FLOAT_806174c4,
                                (double)FLOAT_80617574, 0.0, 0.0,
                                entity, 0, 0x17);
                            *pfill = FLOAT_80617440;
                        }
                    }
                }
            }
        }
        i += 0xc;
    } while (i < 0xc * 0xc);   /* 12 entries * 0xc stride */
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x801e4a74  Integrate a spin phase (+0xd0) by a frame-time-scaled rate.
void debris_spin_accumulate(int self)
{
    *(float *)(self + 0xd0) =
        FLOAT_806175e8 * (float)(-(double)(int)DAT_806108e4) *
        (FLOAT_80617428 / (float)(-(double)(int)DAT_806109c0)) +
        *(float *)(self + 0xd0);
}

// ---------------------------------------------------------------------------
// 0x801e4ad4  Reset the piece bone palette to identity + rotate joints 1 & 2.
void debris_rig_reset(void)
{
    int frame;
    int *body;
    int j;
    float *pivot;
    int off;
    int pivotVec[3];

    frame = FUN_803826c4();
    off = 0;
    pivotVec[0] = DAT_803df79c;
    pivotVec[1] = DAT_803df7a0;
    pivotVec[2] = DAT_803df7a4;

    body = *(int **)(*(int *)(frame + 4) + 4);
    pivot = (float *)body[*body * -0x30 + 0x1b];

    for (j = 0; j < *(int *)(*(int *)(*(int *)(frame + 4) + 4) + 4); j += 1) {
        mtx4_set_identity(*(int *)(*(int *)(frame + 4) + 0x10) + off);
        off += 0x40;
    }
    *(unsigned int *)(frame + 0x28) = *(unsigned int *)(frame + 0x28) | 0x200000;

    __wd_rotate_about_point(*(int *)(*(int *)(frame + 4) + 0x10) + 0x40,
                            pivotVec, pivot);
    body = *(int **)(*(int *)(frame + 4) + 4);
    __wd_rotate_about_point(*(int *)(*(int *)(frame + 4) + 0x10) + 0x80,
                            pivotVec,
                            (float *)(body[*body * -0x30 + 0x1b] + 0x18));
    FUN_80382710();
}

// ---------------------------------------------------------------------------
// 0x801e4bc8  Sub-object extra-word tick: throttled spark impact at contact.
void debris_spark_tick_cb(void)
{
    float fx, fy, fz;
    int entity;
    unsigned int tex;
    int floorHit;
    float *state;
    unsigned long long frame;
    float dst[3];
    float src[3];

    frame = FUN_803826f0();
    state = (float *)(unsigned int)frame;

    if (DAT_80611928 != DAT_806126b0 && state != (float *)0 &&
        state != (float *)0xfffffff4 &&
        FLOAT_806175ec <
            (DAT_805c4fec - state[2]) * (DAT_805c4fec - state[2]) +
            (DAT_805c4fe4 - state[0]) * (DAT_805c4fe4 - state[0]) +
            (DAT_805c4fe8 - state[1]) * (DAT_805c4fe8 - state[1])) {
        entity = subobj_get_entity((unsigned int)(frame >> 0x20));
        fx = state[0] - *(float *)(entity + 100);
        fz = state[1] - *(float *)(entity + 0x68);
        fy = state[2] - *(float *)(entity + 0x6c);
        if (fy * fy + fx * fx + fz * fz < FLOAT_806175f0) {
            dst[0] = -state[3];
            src[0] = FLOAT_806175f4 * dst[0] + state[0];
            dst[1] = -state[4];
            src[1] = FLOAT_806175f4 * dst[1] + state[1];
            dst[2] = -state[5];
            src[2] = FLOAT_806175f4 * dst[2] + state[2];
            floorHit = __wd_floor_height((double)FLOAT_80617440,
                                         (double)FLOAT_80617440);
            tex = sprite_get_texture_id(0x3b4);
            particle_spawn_impact_from_ray(tex, 1, 0x7f3f3f3f, 0, 1,
                                           (unsigned int)floorHit,
                                           (unsigned int)(unsigned long long)src,
                                           dst);
            DAT_805c4fe4 = state[0];
            DAT_805c4fe8 = state[1];
            DAT_805c4fec = state[2];
            DAT_806126b0 = DAT_80611928;
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x801e4d68  Install the spark tick callback on the piece sub-object.
void debris_install_spark_cb(int self)
{
    if (*(unsigned int *)(self + 0x80) != 0) {
        subobj_set_extra_words(*(unsigned int *)(self + 0x80),
                               (unsigned int)(unsigned long long)&debris_spark_tick_cb, 0);
    }
}

// ---------------------------------------------------------------------------
// 0x801e4da0  Two-state spin ramp: ramp +0xd0 up (state 1) / down (state 2).
void debris_spin_ramp_update(int self)
{
    float ceiling = FLOAT_80617508;
    float floor0 = FLOAT_80617440;

    if (*(int *)(self + 0x1a0) == 1) {
        *(float *)(self + 0xd0) =
            FLOAT_80617430 * (float)(-(double)(int)DAT_806108e4) *
            (FLOAT_80617428 / (float)(-(double)(int)DAT_806109c0)) +
            *(float *)(self + 0xd0);
        if (ceiling < *(float *)(self + 0xd0)) {
            *(float *)(self + 0xd0) = ceiling;
            *(int *)(self + 0x1a0) = 0;
        }
    } else if (*(int *)(self + 0x1a0) == 2) {
        *(float *)(self + 0xd0) =
            -(FLOAT_80617430 * (float)(-(double)(int)DAT_806108e4) *
              (FLOAT_80617428 / (float)(-(double)(int)DAT_806109c0)) -
              *(float *)(self + 0xd0));
        if (*(float *)(self + 0xd0) < floor0) {
            *(float *)(self + 0xd0) = floor0;
            *(int *)(self + 0x1a0) = 3;
        }
    }
    *(float *)(self + 100) = *(float *)(self + 0xa8) + *(float *)(self + 0xd0);
}

// ---------------------------------------------------------------------------
// 0x801e4ee0  Render the 4-digit rolling countdown texture into scratch RT.
void debris_timer_texture_render(unsigned long long frame, double a2, void *a3,
                                 void *a4, void *a5, void *a6, void *a7, void *a8)
{
    unsigned int digitW;
    int ctx;
    int strip;
    int digitCap;
    int prevSub;
    int prevBlend;
    int matIdx;
    int matRec;
    int col;
    unsigned int stride10000;
    unsigned int value;
    unsigned int quotient;
    unsigned int placeIdx;
    double ratio;
    double yBase;
    double yStep;
    double advScale;
    double bias;
    double xSpacing;
    double y;
    double advance;
    int slotY;
    int fontH;
    unsigned int u0;
    unsigned int u1;
    unsigned int v0;
    unsigned int v1;
    unsigned char digitBuf[8];

    frame = FUN_803826c4();
    ctx = (int)frame;
    strip = collision_alloc_strip_table(*(int **)(ctx + 4));
    stride10000 = 10000;
    prevSub = (int)gx_set_sub_material_id(1);
    __wd_gx_color_update();
    prevBlend = gx_set_blend_mode(0);
    matIdx = *(int *)(strip + 0x90);
    gx_set_active_material(matIdx);
    matRec = *DAT_80610a20 + matIdx * 0x58;
    fontH = 0x20;
    u0 = 0;
    v0 = (unsigned int)*(unsigned short *)(matRec + 0x4e) << 4;
    u1 = 0;
    v1 = (unsigned int)*(unsigned short *)(matRec + 0x52) << 4;
    gx_draw_sprite_rect(0, 0, 0x40, 0x20, 0, 0, v0, v1);
    __wd_gx_color_update();
    text_set_font_table(0x16);

    ratio = worm_ekun_progress_ratio();
    value = (unsigned int)(int)((double)FLOAT_806175f8 * ratio);
    if ((int)value < 0) {
        *(unsigned int *)(*(int *)(ctx + 4) + 0x30) =
            *(unsigned int *)(*(int *)(ctx + 4) + 0x30) & 0xfffffff0;
        value = DAT_806126b4;
    } else {
        DAT_806126b4 = value;
        int frameMod = DAT_80611928 - (DAT_80611928 / (int)DAT_806109c0) * (int)DAT_806109c0;
        int halfFrame = ((int)DAT_806109c0 >> 1) +
                        (int)((int)DAT_806109c0 < 0 && (DAT_806109c0 & 1) != 0);
        if (frameMod < halfFrame) {
            *(unsigned int *)(*(int *)(ctx + 4) + 0x30) =
                *(unsigned int *)(*(int *)(ctx + 4) + 0x30) | 0xf;
        } else {
            *(unsigned int *)(*(int *)(ctx + 4) + 0x30) =
                *(unsigned int *)(*(int *)(ctx + 4) + 0x30) & 0xfffffff0;
        }
    }

    xSpacing = (double)FLOAT_806175fc;
    digitCap = 0;
    yBase = (double)FLOAT_80617604;
    col = 0;
    advScale = (double)FLOAT_80617430;
    yStep = (double)FLOAT_80617608;
    bias = DOUBLE_80617420;
    (void)bias;

    do {
        quotient = (int)value / (int)stride10000;
        FUN_80387fb8(ratio, a2, a3, a4, a5, a6, a7, a8, (int)digitBuf,
                     &DAT_80610d98, (int)quotient % 10, fontH, u0, u1, v0, v1);
        text_get_char_width(digitBuf);

        /* rolling-odometer digit: interpolate the current digit position */
        ratio = (double)(float)((double)FLOAT_806175fc *
                    (double)((float)(-(double)(int)value) /
                              (float)(-(double)(int)stride10000) -
                             (float)(-(double)(int)quotient)));
        if (digitCap != 3) {
            if (ratio <= (double)FLOAT_80617600) {
                ratio = (double)FLOAT_80617440;
            } else {
                ratio = (double)(FLOAT_806174c4 *
                                 (float)(ratio - (double)FLOAT_80617600));
            }
        }
        ratio = (double)(float)(yBase + ratio);
        {
            unsigned int color = (digitCap != 3) ? 0x7f7f7f4f : 0x7f3c3c4f;
            double w = text_get_char_width(digitBuf);
            slotY = (col + 1) * 8;
            y = ratio;
            advance = __wd_text_draw_char(
                -(double)(float)(w * advScale - (double)(float)(-(double)(int)slotY)),
                ratio, digitBuf, color);
        }

        FUN_80387fb8((double)advance, y, a3, a4, a5, a6, a7, a8, (int)digitBuf,
                     &DAT_80610d98, (int)(quotient - 1) % 10, (int)(quotient - 1),
                     u0, u1, v0, v1);
        text_get_char_width(digitBuf);
        ratio = (double)(float)(y + xSpacing);
        {
            unsigned int color = (digitCap != 3) ? 0x7f7f7f4f : 0x7f3c3c4f;
            double w = text_get_char_width(digitBuf);
            y = ratio;
            advance = __wd_text_draw_char(
                -(double)(float)(w * advScale - (double)(float)(-(double)(int)slotY)),
                ratio, digitBuf, color);
        }

        int nextDigit = quotient + 1;
        FUN_80387fb8((double)advance, y, a3, a4, a5, a6, a7, a8, (int)digitBuf,
                     &DAT_80610d98, nextDigit % 10, nextDigit, u0, u1, v0, v1);
        text_get_char_width(digitBuf);
        a2 = (double)(float)(y - yStep);
        {
            unsigned int color = (digitCap != 3) ? 0x7f7f7f4f : 0x7f3c3c4f;
            double w = text_get_char_width(digitBuf);
            (void)__wd_text_draw_char(
                -(double)(float)(w * advScale - (double)(float)(-(double)(int)slotY)),
                a2, digitBuf, color);
        }

        digitCap += 1;
        stride10000 = (int)stride10000 / 10;
        col += 2;
    } while (digitCap < 4);

    (void)digitW;
    (void)xSpacing;
    (void)yStep;
    render_target_bind_default(DAT_80610d94);
    gx_fill_rect_flags(0xf, 0, 0, 0x40, 0x20, 0);
    gx_set_sub_material_id(prevSub);
    __wd_gx_color_update();
    gx_set_blend_mode(prevBlend);
    *(int *)(strip + 0x90) = DAT_80610d94;
    FUN_803669dc();
    FUN_80382710();
}
