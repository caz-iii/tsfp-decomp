// worm_effect.cpp
// Scripted multi-segment "worm" creature set-piece. Address range
// 0x801c5abc - 0x801c6abc (batch_591). The creature is a chain of up to 5
// segment entities (entity type tag 'WRM1' = 0x57524d31, one per zone id in
// DAT_805c3518[5]) driven by a shared 0x1590-byte state pool (DAT_80612630,
// 5 * 0x450-byte records) and a set of precomputed body/trail curve tables.
//
// Functions:
//   worm_particle_defs_init  (0x801c5abc) build the 3 particle emitter defs
//                             (dust/spray/body) used by the worm segments.
//   worm_build_curve_tables  (0x801c5d58) precompute the sinusoidal body-ring
//                             and trail vertex tables (DAT_805c2a98.. blocks)
//                             via math_poly_trig; two loops (20 + 11 samples).
//   worm_pool_alloc          (0x801c60c4) allocate the 5*0x450 segment state
//                             pool (DAT_80612630) as 0x20 records of 0x1590.
//   worm_segment_render_cb   (0x801c60f4) per-frame render callback: pull each
//                             segment toward the local player when within range.
//   worm_segment_think_cb    (0x801c61cc) per-frame think callback (set as the
//                             render-type fn of each WRM1 entity): drives the
//                             emerge/submerge state machine, spawns burst
//                             particles and, on the 5th surfacing, unhides the
//                             scene node 0x18 and fires the payload.
//   worm_spawn_segment       (0x801c643c) create one WRM1 segment entity for a
//                             zone slot, wire its update flags + render callback.
//   worm_scan_aim_targets    (0x801c650c) scan players in range and test the
//                             worm's facing/aim cone against them (sin/cos LUT
//                             DAT_80583228).
//   worm_setup               (0x801c672c) build tables + defs, clear the slot
//                             table, allocate & seed the 5 segment records,
//                             spawn their emitters, resolve sound zones, and
//                             attach worm_segment_render_cb to scene node 0x18.
//   worm_clamp_screen_height (0x801c69f0) clamp a projected screen-height value
//                             to the configured band.
//   worm_project_ray_to_plane(0x801c6abc) project the player view ray onto two
//                             constant Y planes and return the two hit points.
//
// Non-matching: the PPC register save/restore intrinsics (__save_gpr_*/
// __restore_gpr_*) and the wide FP-context spill frame (FUN_803826f0/
// FUN_8038273c) are dead ABI plumbing and are dropped or kept as opaque
// open/close calls; the two per-frame callbacks receive their entity pointer as
// an explicit parameter (the ROM read it from the saved-register frame);
// mem_alloc_aligned is called through a typed pointer so its returned base can
// be captured (the shared header declares it void); the int<->double magic-bias
// idiom is reduced to a plain (double)(int) cast. Raw FUN_/DAT_/FLOAT_/DOUBLE_
// symbols are declared file-locally; named helpers resolve through the shared
// headers.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {
// --- module state ---
extern int   DAT_80612630;   // segment state pool base (5 * 0x450 records)
extern int   DAT_8061266c;   // worm progress/counter block (+0x3c surfacing count)
extern int   DAT_80612634;   // tables-built flag
extern int   DAT_80612638;   // worm active flag
extern int   DAT_80610d1c;   // worm-enabled flag
extern int   DAT_80610d20;   // zone-ids-need-resolve flag
extern int   DAT_80612658;   // sound bank base index for segment particles
extern int   DAT_80611d54;   // primary player slot pointer
extern int   DAT_80611d78;   // player array base (0x141c stride)
extern int   DAT_80612610;   // active player count
extern int   DAT_806109c4;   // framebuffer width
extern int   DAT_806109c8;   // framebuffer height

// per-player worm screen-height band state
extern float FLOAT_80612648;
extern float fRam8061264c;
extern float FLOAT_80612650;
extern float fRam80612654;

// sin/cos lookup table (0x800 entries, 4-byte)
extern int  DAT_80583228;

// segment / curve scratch tables (BSS)
extern int  DAT_805c2a98; extern int DAT_805c2a9c; extern int DAT_805c2aa0; extern int DAT_805c2aa4;
extern int  DAT_805c2bd8; extern int DAT_805c2bdc; extern int DAT_805c2be0; extern int DAT_805c2be4;
extern int  DAT_805c2d18; extern int DAT_805c2d1c; extern int DAT_805c2d20; extern int DAT_805c2d24;
extern int  DAT_805c2e58; extern int DAT_805c2e5c; extern int DAT_805c2e60; extern int DAT_805c2e64;
extern int  DAT_805c2f98; extern int DAT_805c2f9c; extern int DAT_805c2fa0; extern int DAT_805c2fa4;
extern int  DAT_805c30d8; extern int DAT_805c30dc; extern int DAT_805c30e0; extern int DAT_805c30e4;
extern int  DAT_805c3218; extern int DAT_805c321c; extern int DAT_805c3220; extern int DAT_805c3224;
extern int  DAT_805c32c8; extern int DAT_805c32cc; extern int DAT_805c32d0; extern int DAT_805c32d4;
extern int  DAT_805c3398; extern int DAT_805c339c; extern int DAT_805c33a0; extern int DAT_805c33a4;
extern int  DAT_805c3448; extern int DAT_805c344c; extern int DAT_805c3450; extern int DAT_805c3454;
extern float DAT_805c3378; extern float DAT_805c337c; extern float DAT_805c3380; extern float DAT_805c3384;
extern float DAT_805c3388; extern float DAT_805c338c; extern float DAT_805c3390; extern float DAT_805c3394;
extern float DAT_805c34f8; extern float DAT_805c34fc; extern float DAT_805c3500; extern float DAT_805c3504;
extern float DAT_805c3508; extern float DAT_805c350c; extern float DAT_805c3510; extern float DAT_805c3514;
extern int  DAT_805c3518;    // segment entity-id table [5] + zone id tables

// particle def / emitter def blobs
extern int  DAT_805c26a8; extern int DAT_805c2774; extern int DAT_805c27f8; extern int DAT_805c28c4;
extern int  DAT_805c2948; extern int DAT_805c2a14;

// zone-id tables resolved at setup
extern int  DAT_80450104; extern int DAT_8045011c;

// const vectors (bind offsets / plane data) in ROM
extern int  DAT_803df0d8; extern int DAT_803df0dc; extern int DAT_803df0e0;
extern int  DAT_803df11c; extern int DAT_803df134; extern int DAT_803df138; extern int DAT_803df13c;
extern int  DAT_803df140; extern int DAT_803df144; extern int DAT_803df148;
extern int  DAT_803df14c; extern int DAT_803df150; extern int DAT_803df154;

// --- float/double constant pool ---
extern float FLOAT_80616ed0; extern float FLOAT_80616ed4; extern float FLOAT_80616ed8;
extern float FLOAT_80616edc; extern float FLOAT_80616ee0; extern float FLOAT_80616ee4;
extern float FLOAT_80616ee8; extern float FLOAT_80616eec; extern float FLOAT_80616ef0;
extern float FLOAT_80616ef4; extern float FLOAT_80616ef8; extern float FLOAT_80616efc;
extern float FLOAT_80616f00; extern float FLOAT_80616f04; extern float FLOAT_80616f08;
extern float FLOAT_80616f0c; extern float FLOAT_80616f10; extern float FLOAT_80616f14;
extern float FLOAT_80616f18; extern float FLOAT_80616f1c; extern float FLOAT_80616f20;
extern float FLOAT_80616f24; extern float FLOAT_80616f28; extern float FLOAT_80616f2c;
extern float FLOAT_80616f30; extern float FLOAT_80616f34; extern float FLOAT_80616f38;
extern float FLOAT_80616f3c; extern float FLOAT_80616f40; extern float FLOAT_80616f44;
extern float FLOAT_80616f50; extern float FLOAT_80616f58; extern float FLOAT_80616f5c;
extern float FLOAT_80616f60; extern float FLOAT_80616f64; extern float FLOAT_80616f68;
extern float FLOAT_80616f6c; extern float FLOAT_80616f70; extern float FLOAT_80616f74;
extern float FLOAT_80616f78; extern float FLOAT_80616f7c; extern float FLOAT_80616f80;
extern float FLOAT_80616f84;
extern double DOUBLE_80616f48;

// --- raw frontier helpers (typed to their call sites) ---
extern int    FUN_803826f0(void);   // wide FP-context spill frame open (returns ptr)
extern void   FUN_8038273c(void);   // wide FP-context spill frame close
extern void   FUN_80221074(double, unsigned long long, unsigned long long, unsigned long long,
                           unsigned long long, unsigned long long, unsigned long long,
                           unsigned long long);  // payload fire (FP arg)
extern void   FUN_802212b4(int);    // payload stage set
extern void   FUN_8037dc94(double, double, double, float *, int, float *, float *,
                           unsigned int *, int, char, int);  // spawn attached effect

// mem_alloc_aligned is declared void in the shared headers but the ROM returns
// the block pointer in r3; call it through a correctly-typed pointer so the
// returned base can be captured (non-matching; links against the void def).
static void *(*const worm_alloc_aligned)(int, int, int) =
    (void *(*)(int, int, int))(void *)&mem_alloc_aligned;

// NaN sentinel the ROM writes into segment slots 0x110/0x111 (0xffc00000).
static inline float worm_neg_nan(void)
{
    union { unsigned int u; float f; } v; v.u = 0xffc00000u; return v.f;
}
}

// 0x801c5abc - build the three particle emitter defs (dust / spray / body ring)
// used by the worm segments. Pure def-table construction via the particle API.
void worm_particle_defs_init(void)
{
    double d;

    particle_def_create((double)FLOAT_80616ed0, (double)FLOAT_80616ed0, (u32)(size_t)&DAT_805c26a8, 0, 0, 1, 0x34f, 0);
    particle_def_set_size_keyframes((double)FLOAT_80616ed4, (double)FLOAT_80616ed8, (double)FLOAT_80616edc,
                                    (double)FLOAT_80616ee0, 0, 0, 0x231f1900);
    particle_def_set_velocity_ranges((double)FLOAT_80616ed4, (double)FLOAT_80616ed8, (double)FLOAT_80616ee4,
                                     (double)FLOAT_80616ed8, (double)FLOAT_80616ed0, (double)FLOAT_80616ee0);
    d = (double)FLOAT_80616ed8;
    particle_def_set_size_start_end(d, d, d, (double)FLOAT_80616ed0);
    d = (double)FLOAT_80616ed8;
    particle_def_set_color_end(d, d, (double)FLOAT_80616ee8, d);
    particle_emitter_create((double)FLOAT_80616eec, (double)FLOAT_80616ef0, (double)FLOAT_80616edc,
                            (u32)(size_t)&DAT_805c2774, (u32)(size_t)&DAT_805c26a8, 0, 1);
    particle_emitter_set_velocity_range((double)FLOAT_80616ed0, (double)FLOAT_80616ed8);
    particle_emitter_set_color_range((double)FLOAT_80616ed0, (double)FLOAT_80616ed8, (double)FLOAT_80616ef4,
                                     (double)FLOAT_80616ed8);
    particle_emitter_set_lifetime_range((double)FLOAT_80616ed0, (double)FLOAT_80616ed8);

    particle_def_create((double)FLOAT_80616ef8, (double)FLOAT_80616ed0, (u32)(size_t)&DAT_805c27f8, 0, 0, 1, 0x34f, 0);
    particle_def_set_size_keyframes((double)FLOAT_80616ed0, (double)FLOAT_80616ed8, (double)FLOAT_80616edc,
                                    (double)FLOAT_80616ee0, 0, 0, 0x231f1900);
    particle_def_set_velocity_ranges((double)FLOAT_80616ed0, (double)FLOAT_80616ed8, (double)FLOAT_80616ef0,
                                     (double)FLOAT_80616ed8, (double)FLOAT_80616ed0, (double)FLOAT_80616ee0);
    d = (double)FLOAT_80616ed8;
    particle_def_set_size_start_end(d, d, d, (double)FLOAT_80616ed0);
    d = (double)FLOAT_80616ed8;
    particle_def_set_color_end(d, d, (double)FLOAT_80616ee8, d);
    particle_emitter_create((double)FLOAT_80616efc, (double)FLOAT_80616efc, (double)FLOAT_80616f00,
                            (u32)(size_t)&DAT_805c28c4, (u32)(size_t)&DAT_805c27f8, 0, 1);
    particle_emitter_set_velocity_range((double)FLOAT_80616ed0, (double)FLOAT_80616ed8);
    particle_emitter_set_color_range((double)FLOAT_80616f04, (double)FLOAT_80616ed8, (double)FLOAT_80616f08,
                                     (double)FLOAT_80616ed8);

    particle_def_create((double)FLOAT_80616ee0, (double)FLOAT_80616ed0, (u32)(size_t)&DAT_805c2948, 0, 1, 1, 0x34e, 0);
    d = (double)FLOAT_80616ed8;
    particle_def_set_size_keyframes((double)FLOAT_80616ed0, d, d, d, 0x400000, 0x400000, 0x183c1000);
    particle_def_set_velocity_ranges((double)FLOAT_80616edc, (double)FLOAT_80616ed8, (double)FLOAT_80616f0c,
                                     (double)FLOAT_80616ed8, (double)FLOAT_80616ed0, (double)FLOAT_80616ee0);
    d = (double)FLOAT_80616ed8;
    particle_def_set_size_start_end(d, d, d, (double)FLOAT_80616ed0);
    d = (double)FLOAT_80616ed8;
    particle_def_set_color_end(d, d, (double)FLOAT_80616f10, d);
    particle_emitter_create((double)FLOAT_80616f14, (double)FLOAT_80616f18, (double)FLOAT_80616f1c,
                            (u32)(size_t)&DAT_805c2a14, (u32)(size_t)&DAT_805c2948, 0, 0x801);
    particle_emitter_set_velocity_range((double)FLOAT_80616ed0, (double)FLOAT_80616ed8);
    particle_emitter_set_color_range((double)FLOAT_80616edc, (double)FLOAT_80616ed8, (double)FLOAT_80616ef4,
                                     (double)FLOAT_80616ed8);
    return;
}

// 0x801c5d58 - precompute the worm's body-ring (20 samples) and trail (11
// samples) vertex/param tables from sine curves (math_poly_trig).
void worm_build_curve_tables(void)
{
    unsigned int i;
    unsigned int k;
    int off;
    double freqA, spanA, cVal, base, ampA, phase;

    freqA = (double)FLOAT_80616f20;
    spanA = (double)FLOAT_80616f24;
    i = 0;
    cVal = (double)FLOAT_80616ed8;
    phase = (double)FLOAT_80616f28;
    base = (double)FLOAT_80616ed0;
    ampA = (double)FLOAT_80616f2c;
    off = 0;
    do {
        double t = (double)(float)((double)(float)(freqA * (double)(int)i) / spanA);
        double s = math_poly_trig(t);
        double t2 = (double)(float)(t - phase);
        *(float *)((int)&DAT_805c2a98 + off) = (float)s;
        *(float *)((int)&DAT_805c2a9c + off) = (float)cVal;
        s = math_poly_trig(t2);
        *(float *)((int)&DAT_805c2aa0 + off) = (float)s;
        *(float *)((int)&DAT_805c2aa4 + off) = (float)base;
        *(float *)((int)&DAT_805c2e58 + off) = *(float *)((int)&DAT_805c2a98 + off);
        *(float *)((int)&DAT_805c2e5c + off) = (float)cVal;
        *(int *)((int)&DAT_805c2e60 + off)   = *(int *)((int)&DAT_805c2aa0 + off);
        *(float *)((int)&DAT_805c2e64 + off) = (float)base;
        s = math_poly_trig(t);
        *(float *)((int)&DAT_805c2bd8 + off) = (float)(ampA * s);
        *(float *)((int)&DAT_805c2bdc + off) = (float)base;
        s = math_poly_trig(t2);
        i = i + 1;
        *(float *)((int)&DAT_805c2be0 + off) = (float)(ampA * s);
        *(float *)((int)&DAT_805c2be4 + off) = (float)base;
        *(int *)((int)&DAT_805c2f98 + off)   = *(int *)((int)&DAT_805c2bd8 + off);
        *(float *)((int)&DAT_805c2d18 + off) = (float)cVal;
        *(float *)((int)&DAT_805c2d1c + off) = (float)cVal;
        *(float *)((int)&DAT_805c2f9c + off) = (float)cVal;
        *(float *)((int)&DAT_805c2d20 + off) = (float)cVal;
        *(float *)((int)&DAT_805c2d24 + off) = (float)base;
        *(int *)((int)&DAT_805c2fa0 + off)   = *(int *)((int)&DAT_805c2be0 + off);
        *(float *)((int)&DAT_805c2fa4 + off) = (float)base;
        *(int *)((int)&DAT_805c30d8 + off)   = *(int *)((int)&DAT_805c2bd8 + off);
        *(float *)((int)&DAT_805c30dc + off) = (float)cVal;
        *(int *)((int)&DAT_805c30e0 + off)   = *(int *)((int)&DAT_805c2be0 + off);
        *(float *)((int)&DAT_805c30e4 + off) = (float)base;
        off = off + 0x10;
    } while ((int)i < 0x14);

    double freqB = (double)FLOAT_80616f30;
    i = 0;
    double spanB = (double)FLOAT_80616f34;
    cVal = (double)FLOAT_80616ed8;
    base = (double)FLOAT_80616ed0;
    ampA = (double)FLOAT_80616f2c;
    double base2 = (double)FLOAT_80616f38;
    phase = (double)FLOAT_80616f28;
    off = 0;
    do {
        double t = (double)(float)((double)(float)(freqB * (double)(int)i) / spanB);
        double s = math_poly_trig(t);
        *(float *)((int)&DAT_805c3218 + off) = (float)s;
        *(float *)((int)&DAT_805c321c + off) = (float)cVal;
        k = i;
        if (5 < (int)i) {
            k = 10 - i;
        }
        *(float *)((int)&DAT_805c3220 + off) =
            (FLOAT_80616ef0 * (float)(int)k) / FLOAT_80616f34;
        *(float *)((int)&DAT_805c3224 + off) = (float)base;
        *(float *)((int)&DAT_805c3398 + off) = *(float *)((int)&DAT_805c3218 + off);
        *(float *)((int)&DAT_805c339c + off) = (float)cVal;
        *(int *)((int)&DAT_805c33a0 + off)   = *(int *)((int)&DAT_805c3220 + off);
        *(float *)((int)&DAT_805c33a4 + off) = (float)base;
        s = math_poly_trig(t);
        *(float *)((int)&DAT_805c32c8 + off) = (float)(ampA * s);
        *(float *)((int)&DAT_805c32cc + off) = (float)base2;
        s = math_poly_trig((double)(float)(t - phase));
        i = i + 1;
        *(float *)((int)&DAT_805c32d0 + off) = (float)(ampA * s);
        *(float *)((int)&DAT_805c32d4 + off) = (float)base;
        *(int *)((int)&DAT_805c3448 + off)   = *(int *)((int)&DAT_805c32c8 + off);
        *(float *)((int)&DAT_805c344c + off) = (float)cVal;
        *(float *)((int)&DAT_805c3450 + off) = (float)cVal;
        *(float *)((int)&DAT_805c3454 + off) = (float)base;
        off = off + 0x10;
    } while ((int)i < 0xb);

    DAT_805c3378 = FLOAT_80616ed8; DAT_805c337c = FLOAT_80616f3c;
    DAT_805c3380 = FLOAT_80616ed8; DAT_805c3384 = FLOAT_80616ed0;
    DAT_805c34f8 = FLOAT_80616ed8; DAT_805c34fc = FLOAT_80616ed8;
    DAT_805c3500 = FLOAT_80616f40; DAT_805c3504 = FLOAT_80616ed0;
    DAT_805c3388 = FLOAT_80616ed8; DAT_805c338c = FLOAT_80616ed8;
    DAT_805c3390 = FLOAT_80616f44; DAT_805c3394 = FLOAT_80616ed0;
    DAT_805c3508 = FLOAT_80616ed8; DAT_805c350c = FLOAT_80616ed8;
    DAT_805c3510 = FLOAT_80616f44; DAT_805c3514 = FLOAT_80616ed0;
    return;
}

// 0x801c60c4 - allocate the segment state pool: 5 records of 0x450 bytes
// (0x1590 total), 0x20-aligned.
void worm_pool_alloc(void)
{
    DAT_80612630 = (int)(size_t)worm_alloc_aligned(0x20, 0x1590, 1);
    return;
}

// 0x801c60f4 - per-frame render callback (attached to scene node 0x18): for
// each of the 5 segment records whose state (word 9) is a live value < 8, pull
// its param[7] toward the player when within range. `self` is the scene-node
// entity pointer the ROM read from its saved-register frame.
void worm_segment_render_cb(int self)
{
    float st;
    float *segPos;
    float *playerPos;
    int i;
    int off;
    double dist;

    if (DAT_80612630 != 0) {
        i = 0;
        off = 0;
        do {
            segPos = (float *)(DAT_80612630 + off);
            st = segPos[9];
            if (((st != 5.60519e-45f) && (st != 7.00649e-45f)) && ((int)st < 8)) {
                playerPos = *(float **)(self + 0x28);
                dist = math_sqrt((double)((playerPos[2] - segPos[2]) * (playerPos[2] - segPos[2]) +
                                          (*playerPos - *segPos) * (*playerPos - *segPos) +
                                          (playerPos[1] - segPos[1]) * (playerPos[1] - segPos[1])));
                if (dist <= (double)FLOAT_80616ed0) {
                    segPos[7] = segPos[7] + FLOAT_80616f28;
                }
            }
            i = i + 1;
            off = off + 0x450;
        } while (i < 5);
    }
    return;
}

// 0x801c61cc - per-frame think callback set as the render-type fn of each WRM1
// entity: advances the emerge/submerge state machine per matching segment,
// spawns burst particles, and on the 5th surfacing unhides scene node 0x18 and
// fires the payload (FUN_80221074 / FUN_802212b4). `self` is the entity pointer.
void worm_segment_think_cb(int *self, double param_2, double param_3,
                           unsigned long long p4, unsigned long long p5, unsigned long long p6,
                           unsigned long long p7, unsigned long long p8)
{
    float st;
    int ent;
    int slot;
    float *seg;
    int off;
    unsigned long long p1 = 0;
    float bindVec[3];
    float lifeTmp;
    float durTmp;

    bindVec[0] = *(float *)&DAT_803df0d8;
    bindVec[1] = *(float *)&DAT_803df0dc;
    bindVec[2] = *(float *)&DAT_803df0e0;

    if ((self[1] != 0) && (*(int *)(self[1] + 0x150) != 0)) {
        if ((self[6] != 0x15) &&
            (((self[6] != 0 && ((float *)self[10] != (float *)0x0)) &&
              ((float *)self[0xb] != (float *)0x0)))) {
            particle_emitter_spawn(0, (u32)(size_t)&DAT_805c2a14, 1, (float *)self[10], (float *)self[0xb]);
        }
        slot = 0;
        off = 0;
        do {
            ent = *self;
            seg = (float *)(DAT_80612630 + off);
            if (*(int *)(ent + 0x60) == slot + 0x57524d31) {  // 'WRM1' + slot
                *(float *)(ent + 0x1cc) = *(float *)(ent + 0x1cc) - (float)self[8];
                st = seg[9];
                if ((((st == 1.4013e-44f) || (st == 1.54143e-44f)) || (st == 1.26117e-44f)) &&
                    (seg[0x113] != 2.8026e-45f)) {
                    if (self[6] == 0x15) {
                        seg[9] = 1.68156e-44f;
                        *(int *)(DAT_8061266c + 0x3c) = *(int *)(DAT_8061266c + 0x3c) + 1;
                        if (*(int *)(DAT_8061266c + 0x3c) == 5) {
                            ent = (int)scene_node_get_render_data(0x18);
                            *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) & 0xffffffef;
                            ent = (int)scene_node_get_render_data(0x18);
                            entity_set_render_type(ent, 0);
                            FUN_80221074((double)FLOAT_80616f50, p1, param_3, p4, p5, p6, p7, p8);
                            FUN_802212b4(4);
                        }
                        seg[8] = 0.0f;
                        durTmp = seg[0x10f];
                        particle_handle_destroy((int *)&durTmp);
                        param_2 = (double)FLOAT_80616ed0;
                        param_3 = param_2;
                        FUN_8037dc94((double)FLOAT_80616f34, param_2, param_2, seg + 0x10f, 1, seg,
                                     (float *)bindVec, (unsigned int *)0x0, 1,
                                     (char)((char)DAT_80612658 + (char)slot), 0);
                        lifeTmp = seg[0x10f];
                        particle_cached_set_lifetime((double)FLOAT_80616f10, (ParticleEmitter **)&lifeTmp);
                    } else if (FLOAT_80616ed8 <= *(float *)(*self + 0x1cc)) {
                        seg[9] = 1.82169e-44f;
                    } else {
                        seg[9] = 2.10195e-44f;
                        *(int *)(DAT_8061266c + 0x3c) = *(int *)(DAT_8061266c + 0x3c) + 1;
                        if (*(int *)(DAT_8061266c + 0x3c) == 5) {
                            ent = (int)scene_node_get_render_data(0x18);
                            *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) & 0xffffffef;
                            ent = (int)scene_node_get_render_data(0x18);
                            entity_set_render_type(ent, 0);
                            FUN_80221074((double)FLOAT_80616f50, p1, param_3, p4, p5, p6, p7, p8);
                            FUN_802212b4(4);
                        }
                    }
                }
            }
            slot = slot + 1;
            off = off + 0x450;
        } while (slot < 5);
    }
    return;
}

// 0x801c643c - create one WRM1 segment entity for the given zone slot; register
// its update passes, set its collision/render flags, and attach the think
// callback (worm_segment_think_cb). Caches the entity id in DAT_805c3518[slot].
void worm_spawn_segment(int slot)
{
    float f;
    int entity;

    entity = aiactor_find_player_by_team(slot + 0x57524d31);  // 'WRM1' + slot
    if (entity != 0) {
        entity_register_for_update(entity, 3, 0);
        entity_register_for_update(entity, 4, 0);
        entity_register_for_update(entity, 0xc, -0x7fe37738);
        *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x70;
        f = FLOAT_80616efc;
        *(int *)(entity + 0x34) = 0x18;
        *(int *)(entity + 0x38) = 0x18;
        *(char *)(entity + 0x30) = 1;
        *(float *)(entity + 0x68) = *(float *)(entity + 0x68) - f;
        entity_set_render_type(entity, (int)(size_t)(void *)&worm_segment_think_cb);
        f = FLOAT_80616f58;
        (&DAT_805c3518)[slot] = entity;
        *(float *)(entity + 0x1cc) = f;
        *(float *)(entity + 0x1d8) = f;
    }
    return;
}

// 0x801c650c - scan players within range of the worm and test the worm's
// facing/aim cone (sin/cos LUT DAT_80583228) against each. Used to decide when
// the worm can strike. (The inner counted loop that always yields >=3 is kept
// verbatim; the early breaks preserve the original short-circuit.)
void worm_scan_aim_targets(void)
{
    unsigned int idx;
    float dx, dz, dy;
    float *self;
    int off;
    int i;
    int cnt;
    int j;
    double distSq, dot, len;

    self = (float *)(size_t)FUN_803826f0();
    double rangeSq = (double)FLOAT_80616f5c;
    off = 0;
    for (i = 0; i < DAT_80612610; i = i + 1) {
        int pl = DAT_80611d78 + off;
        dx = *self - *(float *)(pl + 0xa8);
        dz = self[2] - *(float *)(pl + 0xb0);
        distSq = (double)(dx * dx + dz * dz);
        if ((distSq < rangeSq) && ((double)FLOAT_80616f60 < distSq)) {
            dx = *(float *)(pl + 0xa8) - *self;
            dz = *(float *)(pl + 0xb0) - self[2];
            dy = *(float *)(pl + 0xac) - self[1];
            idx = (unsigned int)(FLOAT_80616f64 * self[7] + FLOAT_80616ee0);
            dot = (double)(dx * (float)(&DAT_80583228)[idx & 0x7ff] +
                           dz * *(float *)((int)&DAT_80583228 + ((idx + 0x200) * 4 & 0x1ffc)));
            if (((double)FLOAT_80616ed8 < dot) &&
                (len = math_sqrt((double)(dz * dz + dx * dx + dy * dy)),
                 FLOAT_80616ee0 < (float)(dot / len))) {
                cnt = 0;
                j = 5;
                do {
                    cnt = cnt + 1;
                    j = j + -1;
                } while (j != 0);
                if (cnt < 3) break;
                pl = DAT_80611d78 + off;
                dz = self[2] - *(float *)(pl + 0xb0);
                double ddx = (double)(*self - *(float *)(pl + 0xa8));
                idx = (unsigned int)(FLOAT_80616f64 *
                                     ((FLOAT_80616f30 * *(float *)(pl + 0x100)) / FLOAT_80616f68) +
                                     FLOAT_80616ee0);
                double dot2 = (double)(float)(ddx * (double)(float)(&DAT_80583228)[idx & 0x7ff] +
                                              (double)(dz * *(float *)((int)&DAT_80583228 +
                                                       ((idx + 0x200) * 4 & 0x1ffc))));
                if (((double)FLOAT_80616ed8 < dot2) &&
                    (len = math_sqrt((double)(dz * dz + (float)(ddx * ddx + (double)FLOAT_80616ed8))),
                     FLOAT_80616f6c < (float)(dot2 / len))) break;
            }
        }
        off = off + 0x141c;
    }
    FUN_8038273c();
    return;
}

// 0x801c672c - set up the worm effect: build curve tables + particle defs,
// clear the segment id table, allocate & seed the 5 segment records (positions
// from the const bind table, initial states, per-segment emitters), resolve the
// sound zone id tables, and attach the render callback to scene node 0x18.
void worm_setup(void)
{
    int i;
    int off;
    int idxParam;
    int seedOff;
    float *seg;
    int inner;
    double zero;
    float bindTable[33];   // local_8c: floats copied from the ROM const table
    float bindTail[3];     // local_98/94/90: the DAT_803df134.. triple

    // copy the const bind vectors into locals (source DAT_803df0e0.., DAT_803df11c..)
    {
        float *src = (float *)&DAT_803df0e0;
        float *dst = bindTable + 4;
        for (i = 0; i < 7; i++) { dst[1] = src[1]; dst[2] = src[2]; src += 2; dst += 2; }
        dst[3] = src[3];
    }
    {
        int *src = (int *)(size_t)0x803df11c;
        int *dst = (int *)&bindTail[-2];
        for (i = 0; i < 2; i++) { dst[1] = src[1]; dst[2] = src[2]; src += 2; dst += 2; }
        dst[3] = src[3];
    }
    bindTail[0] = *(float *)&DAT_803df134;
    bindTail[1] = *(float *)&DAT_803df138;
    bindTail[2] = *(float *)&DAT_803df13c;

    DAT_80612634 = 1;
    worm_build_curve_tables();
    worm_particle_defs_init();

    off = 0;
    for (i = 0; i < 5; i++) {
        *(int *)((int)&DAT_805c3518 + off) = 0;
        off = off + 4;
    }

    zero = (double)FLOAT_80616ed8;
    i = 0;
    idxParam = 0;
    seedOff = 0;
    off = 0;
    do {
        seg = (float *)(DAT_80612630 + off);
        memset_wrapper((int)seg, 0, 0x450);
        float *pos = (float *)((int)bindTable + seedOff + 0x14);
        seg[0] = pos[0];
        seg[1] = *(float *)((int)bindTable + seedOff + 0x18);
        seg[2] = *(float *)((int)bindTable + seedOff + 0x1c);
        seg[3] = pos[0];
        seg[4] = *(float *)((int)bindTable + seedOff + 0x18);
        seg[5] = *(float *)((int)bindTable + seedOff + 0x1c);
        seg[6] = (float)zero;
        seg[7] = *(float *)((int)bindTable + idxParam);
        seg[0x113] = 0.0f;
        if (i == 2) {
            seg[9] = 1.12104e-44f;
            seg[0xb] = FLOAT_80616f70;
            seg[0xc] = FLOAT_80616f74;
            seg[0xd] = FLOAT_80616f78;
            seg[0x113] = 2.8026e-45f;
        } else {
            seg[9] = 1.4013e-45f;
        }
        seg[10] = (float)zero;
        seg[0x110] = worm_neg_nan();
        seg[0x111] = worm_neg_nan();
        int m = 0x40;
        int mo = 0;
        do {
            *(float *)((int)seg + mo + 0x3c) = seg[0];
            *(float *)((int)seg + mo + 0x40) = seg[1];
            *(float *)((int)seg + mo + 0x44) = seg[2];
            m = m + -1;
            mo = mo + 0xc;
        } while (m != 0);
        particle_emitter_spawn((u32)(size_t)(seg + 0x10f), (u32)(size_t)&DAT_805c2774, 0x1000, seg,
                               (float *)&bindTail[-2]);
        particle_emitter_set_drag((double)FLOAT_80616ed8);
        i = i + 1;
        seedOff = seedOff + 0xc;
        off = off + 0x450;
        idxParam = idxParam + 4;
    } while (i < 5);

    DAT_80612638 = 0;
    DAT_80610d1c = 1;
    if (DAT_80610d20 != 0) {
        off = 0;
        i = 0;
        do {
            int z = soundpad_find_zone_by_id(*(int *)((int)&DAT_80450104 + off));
            i = i + 1;
            *(int *)((int)&DAT_80450104 + off) = z;
            off = off + 4;
        } while (i < 6);
        // (the second table loop bound is 0 in the ROM, i.e. skipped)
        DAT_80610d20 = 0;
    }

    inner = (int)scene_node_get_render_data(0x18);
    *(unsigned int *)(inner + 0x28) = *(unsigned int *)(inner + 0x28) | 0x10;
    inner = (int)scene_node_get_render_data(0x18);
    entity_set_render_type(inner, (int)(size_t)(void *)&worm_segment_render_cb);
    return;
}

// 0x801c69f0 - clamp a projected screen-height value to the configured band
// when the per-player pitch (DAT_80611d54+0x27c) exceeds a threshold.
double worm_clamp_screen_height(double param_1, double param_2, int param_3)
{
    double h;
    double band;

    h = screen_norm_height(param_1, param_3);
    band = (double)FLOAT_80612650;
    if (((band != (double)fRam80612654) && (FLOAT_80616f7c < *(float *)(DAT_80611d54 + 0x27c))) &&
        (band = (double)(((fRam8061264c - FLOAT_80612648) *
                          ((float)(param_2 * (double)(float)((double)(int)DAT_806109c4 - band) /
                          (float)((double)fRam80612654 - band)) + FLOAT_80612648) /
                         (float)((double)(int)DAT_806109c8) - FLOAT_80616f80)), band < h)) {
        h = band;
    }
    return h;
}

// 0x801c6abc - project the local player's view ray onto two constant-Y planes
// (FLOAT_80616f84 and FLOAT_80616ed0) and return the two world hit points via
// outNear / outFar. Uses the player's two view matrices at +0x208 and +0x210.
void worm_project_ray_to_plane(float *outNear, float *outFar)
{
    float dxx;
    float tNear, tFar;
    float a0, a1, a2;
    float b0, b1, b2;

    a0 = *(float *)&DAT_803df140;
    a1 = *(float *)&DAT_803df144;
    a2 = *(float *)&DAT_803df148;
    b0 = *(float *)&DAT_803df14c;
    b1 = *(float *)&DAT_803df150;
    b2 = *(float *)&DAT_803df154;
    mtx4_transform_point(*(float **)(DAT_80611d54 + 0x208), &a0);
    mtx4_transform_point(*(float **)(DAT_80611d54 + 0x208), &b0);
    dxx = b0 - a0;
    if (dxx != FLOAT_80616ed8) {
        tNear = (FLOAT_80616f84 - a0) / dxx;
        tFar = (FLOAT_80616ed0 - a0) / dxx;
        outNear[0] = dxx * tNear + a0;
        outNear[1] = (b1 - a1) * tNear + a1;
        outNear[2] = (b2 - a2) * tNear + a2;
        outFar[0] = dxx * tFar + a0;
        outFar[1] = (b1 - a1) * tFar + a1;
        outFar[2] = (b2 - a2) * tFar + a2;
        mtx4_transform_point(*(float **)(DAT_80611d54 + 0x210), outNear);
        mtx4_transform_point(*(float **)(DAT_80611d54 + 0x210), outFar);
    }
    return;
}
