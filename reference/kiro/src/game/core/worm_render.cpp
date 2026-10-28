// worm_render.cpp
// Render + per-frame update/AI for the scripted multi-segment "worm" creature
// (companion to worm_effect.cpp, batch_591). Address range 0x801c6bf8 -
// 0x801cabf8 (batch_592). The worm is a chain of up to 5 segment records in the
// shared state pool DAT_80612630 (0x450 stride) plus their WRM1 entities in
// DAT_805c3518[5]; this module draws each segment's body ribbon and runs the
// segment state machine each frame.
//
// Functions:
//   worm_segment_body_render (0x801c6bf8) draw one submerged/surfacing segment:
//                             build its body ring from the precomputed curve
//                             tables scaled by the segment's depth, project to
//                             screen, and emit two textured GX triangle strips;
//                             also drives the segment's attached WRM1 entity
//                             transform when surfacing.
//   worm_segment_head_render (0x801c7210) draw the worm head: build the head
//                             ring, spawn a jet particle, then render the head
//                             band + the 15 tapering body rings as GX strips.
//   worm_segment_trail_render(0x801c7d58) draw the segment's fading dust trail
//                             as GX strips (two colour modes by draw pass).
//   worm_head_ground_point   (0x801c8478) find where the head bone chain first
//                             dips below y=0 and return that ground contact
//                             point rotated into world space.
//   worm_segment_target_depth(0x801c85d8) query a segment's target surface
//                             depth from its entity (mode-dependent field).
//   worm_render_all          (0x801c8600) per-frame render dispatcher (from
//                             session_render_scene_by_mode): project the two
//                             screen-band reference points, then for each
//                             segment call the head/body/trail renderers by
//                             state.
//   worm_zone_slot_free      (0x801c8780) test whether the player is clear of
//                             the worm's spawn AABB / excluded zones.
//   worm_update              (0x801c88e8) the per-frame worm tick/AI (from
//                             FUN_801cd124): advance each segment's depth, spawn
//                             segments, run the emerge/hunt/attack/submerge
//                             state machine, steer toward players, apply camera
//                             shake, deal contact damage, play bark/anim events.
//   worm_effect_a_init       (0x801cab90) seed a small effect-config block
//                             (DAT_805c3530..) from the constant pool.
//   worm_burst_spawn         (0x801cabf8) one-shot: spawn the worm surfacing
//                             burst effect + sound the first time (guarded by
//                             DAT_8061266c+0x20).
//
// Non-matching: DAT_cc008000 is the GX FIFO write-gather port; the ROM streams
// vertex floats/ints to it, expressed here through the __gx_fifo_f/__gx_fifo_u
// helpers. PPC register save/restore + wide FP-context spill frames
// (FUN_803826xx/FUN_80382xxx) are dead ABI plumbing kept as opaque open/close
// calls; the many `in_fNN`/`in_psNN_1` caller-saved-FP spill copies the
// decompiler surfaced are dropped. The int<->double magic-bias idiom
// (CONCAT44(0x43300000,x^0x80000000) - DOUBLE_80616f48) is reduced to a plain
// (double)(int) cast. -NAN sentinels use the shared __worm bit helper. Raw
// FUN_/DAT_/FLOAT_/DOUBLE_ symbols are declared file-locally; named helpers
// resolve through the shared headers.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {
// --- GX FIFO write-gather port ---
extern volatile unsigned int DAT_cc008000;

// --- module state ---
extern int   DAT_80612630;   // segment state pool base (5 * 0x450 records)
extern int   DAT_8061266c;   // worm progress/counter block
extern int   DAT_80612634;   // active-segment / tables counter
extern int   DAT_80612638;   // worm active flag
extern int   DAT_80610d1c;   // worm-enabled flag
extern int   DAT_80612668;   // sound bank base index (burst)
extern int   DAT_80611b1c;   // level flags block
extern int   DAT_80611d78;   // player array base (0x141c stride)
extern int   DAT_80611b8c;   // zone node table base
extern int   DAT_80612610;   // active player count (render)
extern int   DAT_806108e4;   // active player count (update)
extern int   DAT_806109c0;   // frame-time denominator
extern int   DAT_806109c4;   // framebuffer width
extern int   DAT_806109c8;   // framebuffer height
extern int   DAT_80612254;   // bound material pointer
extern int   DAT_80610a20;   // texture table {base, ?, lo, hi}
extern int   DAT_805c3518;   // segment entity-id table [5]
extern int   DAT_80450104;   // worm patrol zone-id table [6]
extern int   DAT_8045011c;   // excluded zone-id table

// sin/cos lookup table (0x800 entries, 4-byte)
extern int  DAT_80583228;

// effect-config block seeded by worm_effect_a_init
extern float DAT_805c3530; extern float DAT_805c3534; extern float DAT_805c3538;
extern float DAT_805c353c; extern float DAT_805c3540; extern float DAT_805c3544;
extern int   DAT_805c352c;
extern int   DAT_805c28c4;  // worm death/burst particle-def blob

// per-player worm screen-height band state (written by worm_render_all)
extern float FLOAT_80612648; extern float fRam8061264c;
extern float FLOAT_80612650; extern float fRam80612654;

// precomputed body/trail curve tables (from worm_build_curve_tables)
extern int DAT_805c2a98; extern int DAT_805c2a9c; extern int DAT_805c2aa0;
extern int DAT_805c2e58; extern int DAT_805c2e5c; extern int DAT_805c2e60;
extern int DAT_805c3218; extern int DAT_805c321c; extern int DAT_805c3220; extern int DAT_805c3224;
extern int DAT_805c3398; extern int DAT_805c339c; extern int DAT_805c33a0; extern int DAT_805c33a4;
extern int DAT_805c32c8; extern int DAT_805c32cc; extern int DAT_805c32d0;
extern int DAT_805c3448; extern int DAT_805c344c; extern int DAT_805c3450;
extern int DAT_805c3378; extern int DAT_805c337c; extern int DAT_805c3380;
extern int DAT_805c34f8; extern int DAT_805c34fc; extern int DAT_805c3500;
extern int DAT_805c3368; extern int DAT_805c336c; extern int DAT_805c3370;
extern int DAT_805c34e8; extern int DAT_805c34ec; extern int DAT_805c34f0;
extern int DAT_805c32b8; extern int DAT_805c32bc; extern int DAT_805c32c0;
extern int DAT_805c3438; extern int DAT_805c343c; extern int DAT_805c3440;

// ROM const vectors / bind data
extern int DAT_803df158; extern int DAT_803df15c; extern int DAT_803df160;
extern int DAT_803df164; extern int DAT_803df168; extern int DAT_803df16c;
extern int DAT_803df170; extern int DAT_803df174; extern int DAT_803df178;
extern int DAT_803df17c; extern int DAT_803df180; extern int DAT_803df184;
extern int DAT_803df188; extern int DAT_803df18c; extern int DAT_803df190;
extern int DAT_803df194; extern int DAT_803df198; extern int DAT_803df19c;
extern int DAT_803df1a0; extern int DAT_803df1a4; extern int DAT_803df1a8;
extern int DAT_803df1ac; extern int DAT_803df1b0; extern int DAT_803df1b4;
extern int DAT_803df1b8; extern int DAT_803df1bc; extern int DAT_803df1c0;
extern int DAT_803df1c4; extern int DAT_803df1c8; extern int DAT_803df1cc;
extern int DAT_803df1d0; extern int DAT_803df1d4; extern int DAT_803df1d8;

// --- float / double constant pool ---
extern float FLOAT_80616ed0; extern float FLOAT_80616ed4; extern float FLOAT_80616ed8;
extern float FLOAT_80616edc; extern float FLOAT_80616ee0; extern float FLOAT_80616ee4;
extern float FLOAT_80616eec; extern float FLOAT_80616ef0; extern float FLOAT_80616ef4;
extern float FLOAT_80616f04; extern float FLOAT_80616f0c; extern float FLOAT_80616f24;
extern float FLOAT_80616f28; extern float FLOAT_80616f30; extern float FLOAT_80616f34;
extern float FLOAT_80616f38; extern float FLOAT_80616f3c; extern float FLOAT_80616f64;
extern float FLOAT_80616f68; extern float FLOAT_80616f70; extern float FLOAT_80616f78;
extern float FLOAT_80616f84; extern float FLOAT_80616f88; extern float FLOAT_80616f8c;
extern float FLOAT_80616f90; extern float FLOAT_80616f94; extern float FLOAT_80616f98;
extern float FLOAT_80616f9c; extern float FLOAT_80616fa0; extern float FLOAT_80616fa4;
extern float FLOAT_80616fa8; extern float FLOAT_80616fac; extern float FLOAT_80616fb0;
extern float FLOAT_80616fb4; extern float FLOAT_80616fb8; extern float FLOAT_80616fbc;
extern float FLOAT_80616fc0; extern float FLOAT_80616fc4; extern float FLOAT_80616fc8;
extern float FLOAT_80616fcc; extern float FLOAT_80616fd0; extern float FLOAT_80616fd4;
extern float FLOAT_80616fd8; extern float FLOAT_80616fdc; extern float FLOAT_80616fe0;
extern float FLOAT_80616fe4; extern float FLOAT_80616fe8; extern float FLOAT_80616fec;
extern float FLOAT_80616ff4; extern float FLOAT_80616ff8; extern float FLOAT_80616ffc;
extern float FLOAT_80617000;
extern double DOUBLE_80616f48;

// --- raw frontier helpers (typed to their call sites) ---
extern int    FUN_803826d4(void);  // spill-frame open (returns packed lo/hi)
extern int    FUN_803826d0(void);
extern int    FUN_803826c8(void);
extern int    FUN_803826bc(void);
extern int    FUN_803826f0(void);
extern void   FUN_80382720(void);  // spill-frame close variants
extern void   FUN_80382714(void);
extern void   FUN_80382708(void);
extern void   FUN_8038271c(void);
extern void   FUN_8038273c(void);
extern void   FUN_801c8478(void);        // worm_head_ground_point (batch-local)
extern void   FUN_801cef04(void);        // pre-render hook
extern void   FUN_801cc7b0(void);        // secondary render hook
extern void   FUN_801cc1d8(void);        // burst finalize
extern int    FUN_801cf874(int, unsigned int, int, int);  // segment flag apply
extern int    FUN_801cf804(double, double);               // camera kick
extern double FUN_801cf838(double, double);               // signed angle delta
extern int    FUN_801cf784(double, int, float *);         // play bark/event
extern void   FUN_8037dc94(double, double, double, int, int, float *, float *,
                           int *, int, char, int);         // spawn attached effect
extern int    FUN_80234560(double, double, double, double, double, double, double, double,
                           int, int, int, int, float *, float *, int, int);  // hit event

// GX FIFO write-gather stores (the ROM writes the same MMIO address).
static inline void __gx_fifo_f(float v){ *(volatile float *)&DAT_cc008000 = v; }
static inline void __gx_fifo_u(unsigned int v){ DAT_cc008000 = v; }
// -NAN sentinel (0xffc00000) the segment records use.
static inline float __wr_neg_nan(void){ union{unsigned int u; float f;}v; v.u=0xffc00000u; return v.f; }
}

// ---- typed aliases for header-vs-ROM signature mismatches (no redeclaration) ----
typedef unsigned long long u64;
static u64  (*const __wr_raycast)(int,float*,float*,int,float*,float*,int*,unsigned int,int,int*)
    = (u64(*)(int,float*,float*,int,float*,float*,int*,unsigned int,int,int*))&scene_raycast;
static u64  (*const __wr_switch_reset)(int) = (u64(*)(int))&switch_slot_reset;
static unsigned int (*const __wr_rng_tick)(void) = (unsigned int(*)(void))&rng_counter_tick;
static double (*const __wr_clip_phase)(int) = (double(*)(int))&anim_primary_clip_phase;
// screen-project/measure: ROM passes (packedptr, &x, &y, 0, mode)
static void (*const __wr_measure)(int,float*,float*,int*,int)
    = (void(*)(int,float*,float*,int*,int))&sprite_measure_text_region;
// 3d sound: ROM returns an int "audible" flag; arg5 is a base int
static int  (*const __wr_sound3d)(double,double,double,int,int,int,int)
    = (int(*)(double,double,double,int,int,int,int))&sound_update_3d_source;

// forward decls (defined lower in this file)
static void __worm_update_segment_state(float *seg, int ownerEnt, int i20, int segByteOff, int idOff, int nearestPlayer, float *box);
static void __worm_state_attack_or_submerge(float *seg, int ownerEnt, int segIndex, int segByteOff, int nearestPlayer);
static void worm_apply_players_camera_shake(float *seg);

// 0x801c6bf8 - render one submerged/surfacing body segment. Builds the body
// ring from the precomputed curve tables scaled by the segment depth (word 10 /
// 0xe), projects each ring vertex to screen, and emits two GX triangle strips
// (a solid pass and a textured animated pass); when the segment owns a WRM1
// entity it also drives that entity's transform.
void worm_segment_body_render(void)
{
    unsigned int lut;
    float f2, f5;
    unsigned int uv3, uv4;
    float *seg;
    int i, i2, off;
    unsigned int packedColor;
    int ownerEnt;
    double depth, depthE, base0, base1, base2, phase;
    int packed;
    float ring[80];        // local_450
    unsigned int ringB[80];// local_310
    unsigned int ringC[80];// local_1d0
    float ringD[240];      // local_810
    float scr[80];         // local_9f0 (screen x/y pairs)
    unsigned int scrB[80]; // local_950
    float tmpNear;         // local_9fc (unused write target)
    float tmpNear4;        // local_9f4

    packed = FUN_803826d4();
    seg = (float *)(size_t)((unsigned int)FUN_803826d4() >> 0x20);
    // NOTE: the ROM reads seg from the hi half of the spill-frame return; we
    // recompute it as the pool entry for this call. The renderer only touches
    // fields, so use the pool cursor the caller set up.
    depth = (double)seg[10];
    depthE = (double)seg[0xe];
    f2 = FLOAT_80616f88 * (float)(depth / (double)FLOAT_80616f0c);
    ownerEnt = (&DAT_805c3518)[((int)(size_t)seg - DAT_80612630) / 0x450];
    f5 = FLOAT_80616f88;
    if (f2 < FLOAT_80616f88) f5 = f2;
    base0 = (double)seg[0];
    base1 = (double)seg[1];
    base2 = (double)seg[2];
    packedColor = (int)f5 & 0xffU | 0x110f0c00;

    if ((seg[9] == 2.10195e-44f) || (seg[9] == 1.68156e-44f)) {
        int a = anim_is_clip_active(ownerEnt, 0x199);
        if ((a == 0) && (anim_is_clip_active(ownerEnt, 0x196) == 0)) {
            tmpNear = seg[0x10f];
            particle_cached_set_drag((double)FLOAT_80616ed8, (ParticleEmitter**)&tmpNear);
        } else {
            double ph = anim_active_clip_phase(ownerEnt);
            tmpNear = seg[0x10f];
            particle_cached_set_drag(
                (double)(FLOAT_80616f34 *
                         ((float)((double)FLOAT_80616ed0 - ph) +
                          (float)((double)(1 - (int)seg[8]))) * FLOAT_80616ee0),
                (ParticleEmitter**)&tmpNear);
        }
    } else {
        tmpNear = seg[0x10f];
        particle_cached_set_drag((double)FLOAT_80616edc, (ParticleEmitter**)&tmpNear);
    }

    i = 0; i2 = 0; off = 0;
    do {
        float *rp = (float *)((int)ring + off);
        uv3 = *(unsigned int *)((int)&DAT_805c2a9c + off);
        *rp = *(float *)((int)&DAT_805c2a98 + off);
        uv4 = *(unsigned int *)((int)&DAT_805c2aa0 + off);
        *(unsigned int *)((int)ring + off + 4) = uv3;
        *(unsigned int *)((int)ring + off + 8) = uv4;
        *(float *)((int)ring + off + 4) = (float)((double)*(float *)((int)ring + off + 4) * depth);
        if (i < 0x14) {
            f2 = (float)(depth * (double)FLOAT_80616f38 + (double)FLOAT_80616f8c);
            *rp = *rp * f2;
            *(float *)((int)ring + off + 8) = *(float *)((int)ring + off + 8) * f2;
        }
        *rp = (float)((double)*rp + base0);
        *(float *)((int)ring + off + 4) = (float)((double)*(float *)((int)ring + off + 4) + base1);
        *(float *)((int)ring + off + 8) = (float)((double)*(float *)((int)ring + off + 8) + base2);
        if (i < 0x28) {
            if (i < 0x14) {
                f2 = *(float *)((int)&DAT_805c2e5c + off);
                double m = (double)(float)(depth * (double)FLOAT_80616f38 + (double)FLOAT_80616f8c);
                f5 = *(float *)((int)&DAT_805c2e60 + off);
                *(float *)((int)ringD + off) = (float)(m * (double)*(float *)((int)&DAT_805c2e58 + off) + base0);
                *(float *)((int)ringD + off + 4) = (float)(base1 + (double)f2);
                *(float *)((int)ringD + off + 8) = (float)(m * (double)f5 + base2);
            } else {
                f2 = *(float *)((int)&DAT_805c2e5c + off);
                f5 = *(float *)((int)&DAT_805c2e60 + off);
                *(float *)((int)ringD + off) = (float)(base0 + (double)*(float *)((int)&DAT_805c2e58 + off));
                *(float *)((int)ringD + off + 4) = (float)(base1 + (double)f2);
                *(float *)((int)ringD + off + 8) = (float)(base2 + (double)f5);
            }
            float *sx = (float *)((int)scr + i2);
            float *sy = (float *)((int)scr + i2 + 4);
            __wr_measure((int)ringD + off, sx, sy, (int *)0x0, 0);
            *sx = (float)screen_norm_width((double)*sx, -6);
            *sy = (float)worm_clamp_screen_height((double)*sy, (double)*sx, -6);
        } else {
            *(float *)((int)scr + i2) = FLOAT_80616ed8;
            *(float *)((int)scr + i2 + 4) = FLOAT_80616ed8;
        }
        i = i + 1; off = off + 0x10; i2 = i2 + 8;
    } while (i < 0x3c);

    if (packed == 0) {
        gx_begin_textured_batch(2, 0x92, 0, 0x2a, -8);
        int ci = 0;
        int n = 0x15;
        do {
            int r = ci % 0x14;
            ci = ci + 1;
            __gx_fifo_f(ring[r * 4]); __gx_fifo_f(ring[r * 4 + 1]); __gx_fifo_f(ring[r * 4 + 2]);
            __gx_fifo_u(0x7f7f7f7f);
            __gx_fifo_f(scr[r * 2]); __gx_fifo_f(scr[r * 2 + 1]);
            __gx_fifo_u(ringB[r * 4]); __gx_fifo_u(ringB[r * 4 + 1]); __gx_fifo_u(ringB[r * 4 + 2]);
            __gx_fifo_u(0x7f7f7f7f);
            __gx_fifo_u(scrB[r * 2]); __gx_fifo_u(scrB[r * 2 + 1]);
            n = n + -1;
        } while (n != 0);
        gx_end_batch();
    } else {
        gx_set_sub_material_id(1);
        int tex = sprite_get_texture_id(0x333);
        int *texTab = (int *)&DAT_80610a20;
        if (((tex < 0) || (texTab[3] <= tex)) || (tex <= texTab[2])) {
            gx_bind_material_fallback();
        } else {
            DAT_80612254 = texTab[0] + tex * 0x58;
        }
        gx_begin_textured_batch(2, 0x92, 0, 0x2a, -8);
        i = 0;
        int n = 0x15;
        do {
            int r = i % 0x14;
            i = i + 1;
            __gx_fifo_u(ringB[r * 4]);
            lut = (unsigned int)(FLOAT_80616f64 *
                                 ((FLOAT_80616f30 * (float)(int)i) / FLOAT_80616f24) + FLOAT_80616ee0);
            __gx_fifo_u(ringB[r * 4 + 1]);
            __gx_fifo_u(ringB[r * 4 + 2]);
            __gx_fifo_u(packedColor);
            __gx_fifo_u(*(unsigned int *)((int)&DAT_80583228 + ((lut + 0x200) * 4 & 0x1ffc)));
            __gx_fifo_u((&DAT_80583228)[lut & 0x7ff]);
            __gx_fifo_u(ringC[r * 4]); __gx_fifo_u(ringC[r * 4 + 1]); __gx_fifo_u(ringC[r * 4 + 2]);
            __gx_fifo_u(packedColor);
            __gx_fifo_f(FLOAT_80616ed8); __gx_fifo_f(FLOAT_80616ed8);
            n = n + -1;
        } while (n != 0);
        gx_end_batch();
        if (ownerEnt != 0) {
            FUN_801c8478();
            f2 = FLOAT_80616f30; f5 = FLOAT_80616ed8;
            if (seg[0x113] == 2.8026e-45f) {
                *(float *)(ownerEnt + 100) = FLOAT_80616f70;
                *(float *)(ownerEnt + 0x68) = f5;
                *(float *)(ownerEnt + 0x6c) = FLOAT_80616f78;
                *(char *)(ownerEnt + 0x32) = 1;
                *(unsigned int *)(ownerEnt + 0x28) = *(unsigned int *)(ownerEnt + 0x28) | 0x1000;
                *(float *)(ownerEnt + 0x70) = f5;
                *(float *)(ownerEnt + 0xc4) = f5;
            } else {
                double dz = (double)FLOAT_80616f68;
                *(float *)(ownerEnt + 100) = (float)(base0 - (double)tmpNear);
                f2 = (float)(dz * depthE) / f2;
                *(float *)(ownerEnt + 0x6c) = (float)(base2 - (double)tmpNear4);
                *(char *)(ownerEnt + 0x32) = 1;
                *(unsigned int *)(ownerEnt + 0x28) = *(unsigned int *)(ownerEnt + 0x28) | 0x1000;
                *(float *)(ownerEnt + 0x68) =
                    (float)((double)FLOAT_80616f90 * (double)*(float *)(ownerEnt + 0x1c4) + base1);
                *(float *)(ownerEnt + 0x70) = f2;
                *(float *)(ownerEnt + 0xc4) = f2;
            }
        }
    }
    FUN_80382720();
    return;
}

// 0x801c7210 - render the worm head band plus its 15 tapering body rings, and
// spawn the head jet particle. Heavy screen-space projection of the curve
// tables through a per-segment rotation matrix; emits several GX strips.
void worm_segment_head_render(void)
{
    unsigned int lut;
    float *seg;
    int i, i2, off;
    int seg2, ring2;
    double yaw, hx, hy, hz;
    float mtx[16];         // afStack_120c
    float ringA[40];       // local_104c
    float ringB[40];       // local_11cc
    float scr[44];         // local_ecc
    unsigned int scrB[22]; // local_e74
    float bodyPos[340];    // local_c24
    float bodyPos2[341];   // local_6d4
    float nrm0, nrm1, nrm2, tmp;
    unsigned int rnd;

    seg = (float *)(size_t)FUN_803826c8();
    yaw = (double)seg[7];
    nrm0 = *(float *)&DAT_803df158;
    hx = (double)seg[0];
    nrm1 = *(float *)&DAT_803df15c;
    hy = (double)seg[1];
    nrm2 = *(float *)&DAT_803df160;
    hz = (double)seg[2];
    mtx4_set_rotation_y(yaw, mtx);
    mtx4_write_translation(hx, hy, hz, (int)mtx);

    i = 0; i2 = 0; off = 0;
    do {
        *(int *)((int)ringA + off) = *(int *)((int)&DAT_805c3218 + off);
        *(int *)((int)ringA + off + 4) = *(int *)((int)&DAT_805c321c + off);
        *(int *)((int)ringA + off + 8) = *(int *)((int)&DAT_805c3220 + off);
        *(int *)((int)ringA + off + 0xc) = *(int *)((int)&DAT_805c3224 + off);
        mtx4_transform_point_alias(mtx, (float *)((int)ringA + off));
        float *rb = (float *)((int)ringB + off);
        *(int *)rb = *(int *)((int)&DAT_805c3398 + off);
        *(int *)((int)ringB + off + 4) = *(int *)((int)&DAT_805c339c + off);
        *(int *)((int)ringB + off + 8) = *(int *)((int)&DAT_805c33a0 + off);
        *(int *)((int)ringB + off + 0xc) = *(int *)((int)&DAT_805c33a4 + off);
        mtx4_transform_point_alias(mtx, rb);
        float *sx = (float *)((int)scr + i2);
        float *sy = (float *)((int)scr + i2 + 4);
        __wr_measure((int)rb, sx, sy, (int *)0x0, 1);
        *sx = (float)screen_norm_width((double)*sx, -6);
        *sy = (float)worm_clamp_screen_height((double)*sy, (double)*sx, -6);
        i = i + 1; i2 = i2 + 8; off = off + 0x10;
    } while (i < 0x18);

    rnd = __wr_rng_tick();
    lut = (unsigned int)(FLOAT_80616f64 * seg[7] + FLOAT_80616ee0);
    rnd = rnd % 0xb;
    float jc = (float)(&DAT_80583228)[lut & 0x7ff];
    float js = *(float *)((int)&DAT_80583228 + ((lut + 0x200) * 4 & 0x1ffc));
    float jx = FLOAT_80616ee0 * jc + ringA[rnd * 4];
    float jy = FLOAT_80616ee0 * FLOAT_80616ed8 + ringA[rnd * 4 + 1];
    float jz = FLOAT_80616ee0 * js + ringA[rnd * 4 + 2];
    float jpos[3] = { jx, jy, jz };
    float jhandle = seg[0x10f];
    particle_cached_set_position((ParticleEmitter**)&jhandle, (Vec3*)jpos);

    double vA = (double)(jc * nrm1 - FLOAT_80616ed8 * nrm0);
    double vB = (double)(FLOAT_80616ed8 * nrm2 - js * nrm1);
    if (rnd < 5) { vB = -vB; vA = -vA; }
    float jv0 = (float)((double)(float)(vB - (double)jc) * math_sqrt((double)FLOAT_80616f94));
    float jv1 = (float)math_sqrt((double)FLOAT_80616f94);
    float jv2 = (float)((double)(float)(vA - (double)js) * math_sqrt((double)FLOAT_80616f94));
    float jvel[3] = { jv0, jv1, jv2 };
    float jhandle2 = seg[0x10f];
    particle_cached_set_velocity((ParticleEmitter**)&jhandle2, (Vec3*)jvel);
    if (7 < (int)seg[9]) {
        float h = seg[0x10f];
        particle_cached_set_size_range((double)FLOAT_80616f98, (double)FLOAT_80616f98, (ParticleEmitter**)&h);
    }
    if (seg[9] == 7.00649e-45f) {
        float h = seg[0x10f];
        double sz = (double)(FLOAT_80616ed0 + seg[10] / FLOAT_80616f9c);
        particle_cached_set_size_range(sz, sz, (ParticleEmitter**)&h);
    } else {
        float h = seg[0x10f];
        particle_cached_set_drag((double)FLOAT_80616ed0, (ParticleEmitter**)&h);
        float h2 = seg[0x10f];
        particle_cached_set_size_range((double)FLOAT_80616ed0, (double)FLOAT_80616ed0, (ParticleEmitter**)&h2);
    }

    // head band strip
    gx_begin_textured_batch(2, 0x92, 0, 0x16, -8);
    int n = 0xb; i = 0; off = 0;
    do {
        __gx_fifo_u(*(unsigned int *)((int)ringA + off));
        __gx_fifo_u(*(unsigned int *)((int)ringA + off + 4));
        __gx_fifo_u(*(unsigned int *)((int)ringA + off + 8));
        __gx_fifo_u(0x7f7f7f7f);
        __gx_fifo_u(*(unsigned int *)((int)scr + i));
        __gx_fifo_u(*(unsigned int *)((int)scr + i + 4));
        __gx_fifo_u(*(unsigned int *)((int)ringB + off));
        __gx_fifo_u(*(unsigned int *)((int)ringB + off + 4));
        __gx_fifo_u(*(unsigned int *)((int)ringB + off + 8));
        __gx_fifo_u(0x7f7f7f7f);
        __gx_fifo_u(*(unsigned int *)((int)scrB + i));
        __gx_fifo_u(*(unsigned int *)((int)scrB + i + 4));
        n = n + -1; i = i + 8; off = off + 0x10;
    } while (n != 0);
    gx_end_batch();

    // build the 15 tapering body rings from the segment's stored ring history
    // (fields at +0x50 stride) and render them as strips.
    bodyPos2[0] = ringA[0]; bodyPos2[1] = ringA[1]; bodyPos2[2] = ringA[2];
    bodyPos[0] = ringB[0]; bodyPos[1] = ringB[1]; bodyPos[2] = ringB[2];

    double taper = (double)FLOAT_80616fa0;
    double one = (double)FLOAT_80616ed0;
    int seg15 = 1;
    int base5 = 5;
    int f30 = 0x30;
    int f10 = 0x10;
    int rowOff = 0x50;
    do {
        // copy the ring template rows into this body ring row
        *(int *)((int)bodyPos2 + rowOff) = DAT_805c3218;
        *(int *)((int)bodyPos + rowOff) = DAT_805c3398;
        *(int *)((int)bodyPos2 + rowOff + 4) = DAT_805c321c;
        *(int *)((int)bodyPos + rowOff + 4) = DAT_805c339c;
        *(int *)((int)bodyPos2 + rowOff + 8) = DAT_805c3220;
        *(int *)((int)bodyPos + rowOff + 8) = DAT_805c33a0;
        *(int *)((int)bodyPos2 + rowOff + 0x10) = DAT_805c32c8;
        *(int *)((int)bodyPos + rowOff + 0x10) = DAT_805c3448;
        *(int *)((int)bodyPos2 + rowOff + 0x14) = DAT_805c32cc;
        *(int *)((int)bodyPos + rowOff + 0x14) = DAT_805c344c;
        *(int *)((int)bodyPos2 + rowOff + 0x18) = DAT_805c32d0;
        *(int *)((int)bodyPos + rowOff + 0x18) = DAT_805c3450;
        *(int *)((int)bodyPos2 + rowOff + 0x20) = DAT_805c3378;
        *(int *)((int)bodyPos + rowOff + 0x20) = DAT_805c34f8;
        *(int *)((int)bodyPos2 + rowOff + 0x24) = DAT_805c337c;
        *(int *)((int)bodyPos + rowOff + 0x24) = DAT_805c34fc;
        *(int *)((int)bodyPos2 + rowOff + 0x28) = DAT_805c3380;
        *(int *)((int)bodyPos + rowOff + 0x28) = DAT_805c3500;
        *(int *)((int)bodyPos2 + rowOff + 0x30) = DAT_805c3368;
        *(int *)((int)bodyPos + rowOff + 0x30) = DAT_805c34e8;
        *(int *)((int)bodyPos2 + rowOff + 0x34) = DAT_805c336c;
        *(int *)((int)bodyPos + rowOff + 0x34) = DAT_805c34ec;
        *(int *)((int)bodyPos2 + rowOff + 0x38) = DAT_805c3370;
        *(int *)((int)bodyPos + rowOff + 0x38) = DAT_805c34f0;
        *(int *)((int)bodyPos2 + rowOff + 0x40) = DAT_805c32b8;
        *(int *)((int)bodyPos + rowOff + 0x40) = DAT_805c3438;
        *(int *)((int)bodyPos2 + rowOff + 0x44) = DAT_805c32bc;
        *(int *)((int)bodyPos + rowOff + 0x44) = DAT_805c343c;
        *(int *)((int)bodyPos2 + rowOff + 0x48) = DAT_805c32c0;
        *(int *)((int)bodyPos + rowOff + 0x48) = DAT_805c3440;
        mtx4_set_rotation_y((double)*(float *)((int)seg + f10 + 0x33c), mtx);
        double sc = -(double)(float)((double)(float)((double)seg15 - DOUBLE_80616f48) * taper - one);
        sc = -(double)(float)((double)(int)seg15 * taper - one);
        mtx4_scale_axes(sc, sc, sc, mtx);
        mtx4_write_translation((double)*(float *)((int)seg + f30 + 0x3c),
                               (double)*(float *)((int)seg + f30 + 0x40),
                               (double)*(float *)((int)seg + f30 + 0x44), (int)mtx);
        int k = 0;
        do {
            int idx = k + base5;
            mtx4_transform_point_alias(mtx, bodyPos2 + idx * 4);
            mtx4_transform_point_alias(mtx, bodyPos + idx * 4);
            float *sx = scr + idx * 2;
            float *sy = scr + idx * 2 + 1;
            __wr_measure((int)(bodyPos + idx * 4), sx, sy, (int *)0x0, 1);
            *sx = (float)screen_norm_width((double)*sx, -6);
            *sy = (float)worm_clamp_screen_height((double)*sy, (double)*sx, -6);
            k = k + 1;
        } while (k < 5);
        seg15 = seg15 + 1; f30 = f30 + 0x30; f10 = f10 + 0x10; rowOff = rowOff + 0x50; base5 = base5 + 5;
    } while (seg15 < 0x10);

    int rowBase = 0; int rr = 0;
    do {
        gx_begin_textured_batch(2, 0x92, 0, 10, -8);
        int m = 0; int mn = 5;
        do {
            int a = m + rr;
            int b = m + (rowBase + 1) * 5;
            m = m + 1;
            __gx_fifo_f(bodyPos2[a * 4]); __gx_fifo_f(bodyPos2[a * 4 + 1]); __gx_fifo_f(bodyPos2[a * 4 + 2]);
            __gx_fifo_u(0x7f7f7f7f);
            __gx_fifo_f(scr[a * 2]); __gx_fifo_f(scr[a * 2 + 1]);
            __gx_fifo_f(bodyPos2[b * 4]); __gx_fifo_f(bodyPos2[b * 4 + 1]); __gx_fifo_f(bodyPos2[b * 4 + 2]);
            __gx_fifo_u(0x7f7f7f7f);
            __gx_fifo_f(scr[b * 2]); __gx_fifo_f(scr[b * 2 + 1]);
            mn = mn + -1;
        } while (mn != 0);
        gx_end_batch();
        rowBase = rowBase + 1; rr = rr + 5;
    } while (rowBase < 0xf);

    // shift the ring history (fields +0x33c.. and +0x3c..) down by one for next frame
    for (i = 0; i < DAT_806108e4; i = i + 1) {
        int c = 0x3f; int po = 0x2e8; int qo = 0xf8;
        do {
            *(int *)((int)seg + qo + 0x340) = *(int *)((int)seg + qo + 0x33c);
            *(int *)((int)seg + po + 0x48) = *(int *)((int)seg + po + 0x3c);
            *(int *)((int)seg + po + 0x4c) = *(int *)((int)seg + po + 0x40);
            *(int *)((int)seg + po + 0x50) = *(int *)((int)seg + po + 0x44);
            c = c + -1; po = po + -0xc; qo = qo + -4;
        } while (c != 0);
        seg[0xcf] = (float)yaw;
        seg[0xf] = (float)hx; seg[0x10] = (float)hy; seg[0x11] = (float)hz;
    }
    FUN_80382714();
    (void)tmp;
    return;
}

// 0x801c7d58 - render the segment's fading dust/spray trail as GX strips. Two
// colour modes selected by the spill-frame lo half (solid vs textured).
void worm_segment_trail_render(void)
{
    unsigned int c0, c1;
    float f3;
    float *seg;
    int mode;
    int tex;
    unsigned int lut, lut2, lut3;
    int i, i2, off;
    double d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31, d32, d33;
    float ringLocal[32];   // local_1d0
    float mtxA[16];        // afStack_210
    float mtxB[16];        // afStack_250
    float v0[4];           // local_260..
    float v1[4];           // local_270..
    float v2[4];           // local_280..
    float sx0, sy0, sx1, sy1, sx2, sy2, sx3, sy3;

    int packed = FUN_803826bc();
    seg = (float *)(size_t)((unsigned int)packed >> 0x20);
    mode = (int)packed;
    tex = sprite_get_texture_id(0x333);
    c0 = 0x1713097f;

    i = 0; off = 0;
    int m4 = 4;
    do {
        lut = (unsigned int)(FLOAT_80616f64 *
                             ((FLOAT_80616f30 * (float)(int)i) / FLOAT_80616eec) + FLOAT_80616ee0);
        f3 = (float)(&DAT_80583228)[lut & 0x7ff];
        *(float *)((int)ringLocal + off) =
            FLOAT_80616f38 * (FLOAT_80616f84 + *(float *)((int)&DAT_80583228 + ((lut + 0x200) * 4 & 0x1ffc))) + FLOAT_80616ed0;
        *(float *)((int)ringLocal + off + 4) = FLOAT_80616f38 * f3;
        *(float *)((int)ringLocal + off + 8) = FLOAT_80616ed8;
        *(float *)((int)ringLocal + off + 0xc) = FLOAT_80616ed0;
        *(float *)((int)ringLocal + off + 0x10) = -*(float *)((int)ringLocal + off);
        *(unsigned int *)((int)ringLocal + off + 0x14) = *(unsigned int *)((int)ringLocal + off + 4);
        *(float *)((int)ringLocal + off + 0x18) = FLOAT_80616ed8;
        *(float *)((int)ringLocal + off + 0x1c) = FLOAT_80616ed0;
        i = i + 1; off = off + 0x20; m4 = m4 + -1;
    } while (m4 != 0);
    if (mode == 0) c0 = 0x7f7f7f7f;

    mtx4_set_rotation_y((double)seg[7], mtxA);
    mtx4_write_translation((double)seg[0], (double)(seg[1] + seg[10]), (double)seg[2], (int)mtxA);

    if (mode == 0) {
        int r = 0; int ro = 0;
        do {
            gx_begin_textured_batch(2, 0x92, 0, 8, -8);
            int k = 0; int ko = 0;
            do {
                mtx4_transform_point4_alias(mtxA, (float *)((int)ringLocal + ko + ro), v1);
                __gx_fifo_f(v1[0]); __gx_fifo_f(v1[1]); __gx_fifo_f(v1[2]); __gx_fifo_u(c0);
                __wr_measure((int)v1, &sx0, &sy0, (int *)0x0, 1);
                d17 = screen_norm_height((double)sy0, -6);
                d18 = screen_norm_width((double)sx0, -6);
                __gx_fifo_f((float)d18); __gx_fifo_f((float)d17);
                __gx_fifo_f(v1[0]); __gx_fifo_f(v1[1]); __gx_fifo_f(v1[2]); __gx_fifo_u(c0);
                d17 = screen_norm_height((double)sy0, -6);
                d18 = screen_norm_width((double)sx0, -6);
                __gx_fifo_f((float)d18); __gx_fifo_f((float)d17);
                k = k + 1; ko = ko + 0x20;
            } while (k < 4);
            gx_end_batch();
            r = r + 4; ro = ro + 0x10;
        } while (r < 5);
    }

    d26 = (double)FLOAT_80616ed0; d29 = (double)FLOAT_80616ed8;
    d27 = (double)FLOAT_80616f38; d28 = (double)FLOAT_80616fa0;
    d30 = (double)FLOAT_80616f88; d18 = (double)FLOAT_80616ee0; d25 = (double)FLOAT_80616eec;
    unsigned int u = 0;
    do {
        mtx4_set_rotation_y((double)seg[(u + 1) * 4 + 0xcf], mtxB);
        d19 = -(double)(float)((double)(float)(d18 * (double)(int)(u + 1)) * d28 - d26);
        d32 = (double)(float)((double)(int)(u + 1) * d28);
        d33 = (double)(float)(d26 - d32);
        mtx4_scale_axes(d19, d33, d19, mtxB);
        int iSeg = u + 1;
        mtx4_write_translation((double)seg[iSeg * 0xc + 0xf], (double)seg[iSeg * 0xc + 0x10],
                               (double)seg[iSeg * 0xc + 0x11], (int)mtxB);
        unsigned int uOdd = (u & 1);
        d19 = -(double)(float)((double)(float)(d25 * (double)(int)u) * d28 - d26);
        d31 = -(double)(float)((double)(float)(d25 * (double)(int)(u + 1)) * d28 - d26);
        int r = 0; int ro = 0;
        do {
            int ec = (mode == 1) ? tex : -8;
            gx_begin_textured_batch(2, 0x92, 0, 8, ec);
            unsigned int kk = 0; int ko = 0;
            do {
                float *rp = (float *)((int)ringLocal + ko + ro);
                mtx4_transform_point4_alias(mtxA, rp, v2);
                __gx_fifo_f(v2[0]);
                d24 = (double)(float)((double)(int)kk * d27);
                __gx_fifo_f(v2[1]);
                d23 = (double)(float)(d26 - d24);
                __gx_fifo_f(v2[2]);
                d21 = (double)(float)(d23 + d19);
                d20 = d21; if (d26 < d21) d20 = d26;
                d22 = d29;
                if ((d29 <= d20) && (d22 = d21, (double)FLOAT_80616ed0 < d21)) d22 = (double)FLOAT_80616ed0;
                if (mode == 1) d22 = (double)(float)((double)FLOAT_80616ed0 - d22);
                d20 = (double)(float)((double)(int)u * d28);
                lut2 = (unsigned int)(d30 * (double)(float)(d22 * (double)(float)(d26 - d20)));
                __gx_fifo_u(c0 & 0xffffff00 | lut2);
                if (mode == 0) {
                    __wr_measure((int)v2, &sx1, &sy1, (int *)0x0, 1);
                    d20 = screen_norm_height((double)sy1, -6);
                    d21 = screen_norm_width((double)sx1, -6);
                    __gx_fifo_f((float)d21); __gx_fifo_f((float)d20);
                } else {
                    __gx_fifo_f((float)(d24 + d20));
                    __gx_fifo_f((float)((double)(int)uOdd));
                }
                mtx4_transform_point4_alias(mtxB, rp, v0);
                d21 = (double)(float)(d23 + d31);
                __gx_fifo_f(v0[0]); __gx_fifo_f(v0[1]); __gx_fifo_f(v0[2]);
                d20 = d21; if (d26 < d21) d20 = d26;
                d23 = d29;
                if ((d29 <= d20) && (d23 = d21, (double)FLOAT_80616ed0 < d21)) d23 = (double)FLOAT_80616ed0;
                if (mode == 1) d23 = (double)(float)((double)FLOAT_80616ed0 - d23);
                lut3 = (unsigned int)(d30 * (double)(float)(d23 * d33));
                c0 = c0 & 0xffffff00 | lut2 & 0xffffff00 | lut3;
                __gx_fifo_u(c0);
                if (mode == 0) {
                    __wr_measure((int)v0, &sx2, &sy2, (int *)0x0, 1);
                    d20 = screen_norm_height((double)sy2, -6);
                    d21 = screen_norm_width((double)sx2, -6);
                    __gx_fifo_f((float)d21); __gx_fifo_f((float)d20);
                } else {
                    __gx_fifo_f((float)(d24 + d32));
                    __gx_fifo_f((float)((double)(int)(uOdd + 1)));
                }
                kk = kk + 1; ko = ko + 0x20;
            } while ((int)kk < 4);
            gx_end_batch();
            r = r + 4; ro = ro + 0x10;
        } while (r < 5);
        mtx4_copy(mtxB, mtxA);
        u = u + 1;
    } while ((int)u < 0xf);
    FUN_80382708();
    (void)sx3; (void)sy3; (void)c1; (void)lut;
    return;
}

// 0x801c85d8 - query a segment's target surface depth from its entity: mode 2
// reads the bone-chain y with a bias, other modes read the stored contact y.
double worm_segment_target_depth(int seg, int ownerEnt)
{
    int render = *(int *)(*(int *)(ownerEnt + 4) + 0x10);
    if (*(int *)(seg + 0x44c) != 2) {
        return (double)*(float *)(render + 500);
    }
    return (double)(FLOAT_80616fa4 + *(float *)(render + 0x34));
}

// 0x801c8600 - per-frame worm render dispatcher (from session_render_scene_by_mode):
// project the two screen-band reference points, then per segment call the head /
// body / trail renderers according to the segment's state (word +0x24).
void worm_render_all(void)
{
    int i, off;
    int seg, st;
    float pNear[7];  // afStack_1c (out ray hit)
    float pFar[3];   // afStack_28

    FUN_803826f0();
    FUN_801cef04();
    if (*(char *)(DAT_80611b1c + 0x14) != '\0') {
        FUN_801cc7b0();
    }
    if ((*(char *)(DAT_80611b1c + 0x18) != '\0') && (DAT_80612638 != 0)) {
        gx_bind_material_fallback();
        gx_set_blend_mode(4);
        worm_project_ray_to_plane(pNear, pFar);
        __wr_measure((int)pNear, &FLOAT_80612650, &FLOAT_80612648, (int *)0x0, 0);
        __wr_measure((int)pFar, (float *)&fRam80612654, (float *)&fRam8061264c, (int *)0x0, 0);
        i = 0; off = 0;
        do {
            seg = DAT_80612630 + off;
            st = *(int *)(seg + 0x24);
            if (((st < 8) && (st != 4)) && (st != 7)) {
                worm_segment_head_render();
            }
            if (7 < *(int *)(seg + 0x24)) {
                worm_segment_body_render();
            }
            i = i + 1; off = off + 0x450;
        } while (i < 5);
        i = 0; off = 0;
        do {
            seg = DAT_80612630 + off;
            if (7 < *(int *)(seg + 0x24)) {
                worm_segment_body_render();
            }
            st = *(int *)(seg + 0x24);
            if (((st < 8) && (st != 4)) && (st != 7)) {
                worm_segment_trail_render();
            }
            i = i + 1; off = off + 0x450;
        } while (i < 5);
    }
    FUN_8038273c();
    return;
}

// 0x801c8478 - find where the head bone chain first dips below y=0 and return
// that ground-contact point rotated into the worm's world frame. `self` is the
// segment/entity pointer read from the spill frame.
void worm_head_ground_point(void)
{
    float f1, f2, f3, f4, f5;
    int selfHi;
    float *outVec;
    int render;
    float *bonePt;
    int bone, off;
    double y0, y1;
    float local[28];   // afStack_6c + local_78
    float locNear[3];

    int packed = FUN_803826f0();
    selfHi = (int)((unsigned int)packed >> 0x20);
    outVec = (float *)(size_t)(unsigned int)packed;
    double zero = (double)FLOAT_80616ed8;
    bone = 0; off = 0;
    locNear[0] = *(float *)&DAT_803df164;
    locNear[1] = *(float *)&DAT_803df168;
    locNear[2] = *(float *)&DAT_803df16c;
    while (true) {
        render = *(int *)(*(int *)(selfHi + 4) + 0x10);
        bonePt = (float *)(render + (bone + 1) * 0x40 + 0x30);
        if (bone == 7) {
            mtx4_transform_point((float *)(render + 0x1c0), locNear);
            bonePt = locNear;
        }
        f5 = FLOAT_80616f68; f4 = FLOAT_80616f30; f1 = FLOAT_80616ed8;
        render = *(int *)(*(int *)(selfHi + 4) + 0x10) + off;
        y0 = (double)*(float *)(render + 0x34);
        if ((y0 < zero) && (y1 = (double)bonePt[1], zero <= y1)) break;
        bone = bone + 1; off = off + 0x40;
        if (7 < bone) {
            outVec[0] = FLOAT_80616ed8; outVec[1] = f1; outVec[2] = f1;
            return;
        }
    }
    f2 = *(float *)(render + 0x38);
    f1 = (float)(y1 / (double)(float)(y1 - y0));
    f3 = bonePt[2];
    outVec[0] = (*(float *)(render + 0x30) - bonePt[0]) * f1 + bonePt[0];
    outVec[1] = (float)(y0 - y1) * f1 + bonePt[1];
    outVec[2] = (f2 - f3) * f1 + bonePt[2];
    mtx4_set_rotation_y((double)((f4 * *(float *)(selfHi + 0x70)) / f5), local);
    mtx4_transform_vector(local, outVec);
    (void)local;
    return;
}

// 0x801c8780 - test whether the local player is clear of the worm spawn AABB
// (const box in DAT_803df170..) and not standing in an excluded zone. Returns
// via the spill frame (the ROM leaves the result in the loop's break state).
int worm_zone_slot_free(void)
{
    float f1;
    bool clear;
    int i, j, k, off;
    int pl;
    float box[14];   // local_38

    FUN_803826f0();
    box[3] = *(float *)&DAT_803df170;
    off = 0;
    box[4] = *(float *)&DAT_803df174;
    box[5] = *(float *)&DAT_803df178;
    box[0] = *(float *)&DAT_803df17c;
    box[1] = *(float *)&DAT_803df180;
    box[2] = *(float *)&DAT_803df184;
    for (i = 0; i < DAT_80612610; i = i + 1) {
        pl = DAT_80611d78 + off;
        clear = true;
        j = 0;
        box[6] = *(float *)(pl + 0xa8);
        box[7] = *(float *)(pl + 0xac);
        box[8] = *(float *)(pl + 0xb0);
        k = 3;
        do {
            f1 = *(float *)((int)box + j + 0x18);
            if ((*(float *)((int)box + j) < f1) || (f1 < *(float *)((int)box + j + 0xc))) {
                clear = false;
            }
            j = j + 4; k = k + -1;
        } while (k != 0);
        // (excluded-zone loop bound is 0 in the ROM, i.e. skipped)
        if (clear) break;
        off = off + 0x141c;
    }
    FUN_8038273c();
    return clear ? 1 : 0;
}

// thin predicate wrapper used by the worm state machine (state 0/1/5-6 gate).
int worm_zone_slot_free_test(void)
{
    return worm_zone_slot_free();
}

// 0x801cab90 - seed the small worm effect-config block from the const pool.
void worm_effect_a_init(void)
{
    DAT_805c3530 = FLOAT_80616ff4;
    DAT_805c3534 = FLOAT_80616fb8;
    DAT_805c3538 = FLOAT_80616ff8;
    DAT_805c353c = FLOAT_80616edc;
    DAT_805c3540 = FLOAT_80616ffc;
    DAT_805c3544 = FLOAT_80617000;
    DAT_805c352c = 0;
    return;
}

// 0x801cabf8 - one-shot: on first call (guarded by DAT_8061266c+0x20) spawn the
// worm surfacing burst attached effect and run its finalizer.
void worm_burst_spawn(void)
{
    unsigned int *dst;
    unsigned int v;
    unsigned int *src;
    int c;
    float pos[3];    // local_2c..
    float pos2[3];   // local_38..
    unsigned int scratch[8];  // local_20

    if (*(int *)(DAT_8061266c + 0x20) == 0) {
        dst = scratch - 3;                 // &local_24 region
        src = (unsigned int *)&DAT_803df1d8;
        pos[0] = *(float *)&DAT_803df1c4;
        *(unsigned int *)&pos[1] = *(unsigned int *)&DAT_803df1c8;
        *(unsigned int *)&pos[2] = *(unsigned int *)&DAT_803df1cc;
        pos2[0] = *(float *)&DAT_803df1d0;
        *(unsigned int *)&pos2[1] = *(unsigned int *)&DAT_803df1d4;
        *(unsigned int *)&pos2[2] = *(unsigned int *)&DAT_803df1d8;
        c = 3;
        do {
            unsigned int *s1 = src + 1;
            src = src + 2;
            v = *src;
            dst[1] = *s1;
            dst = dst + 2;
            *dst = v;
            c = c + -1;
        } while (c != 0);
        FUN_8037dc94((double)FLOAT_80616f34, (double)FLOAT_80616ed0, (double)FLOAT_80616ed0,
                     DAT_8061266c + 0x28, 0x1f, pos, pos2, (int *)scratch, 0x200000,
                     (char)DAT_80612668, 0);
        FUN_801cc1d8();
        *(int *)(DAT_8061266c + 0x20) = 1;
    }
    return;
}

// 0x801c88e8 - per-frame worm tick / AI (from FUN_801cd124). Advances each of
// the 5 segment records: interpolates depth, spawns the WRM1 entity when
// needed, runs the emerge/hunt/attack/submerge state machine (segment word 9),
// steers toward the nearest player, applies camera shake, deals contact damage,
// and plays bark/anim events. This is the largest function in the module; its
// control flow, state constants (the small-float bit patterns are raw ints
// reinterpreted as float in the record's state word), and call order are
// preserved verbatim. The heavy int<->double magic-bias math is reduced to
// plain casts; the caller-saved FP spill copies are dropped.
void worm_update(double param_2, double param_3, double param_4,
                 double param_6, double param_7, double param_8)
{
    float f1, f2, f3, f4, f6;
    bool sound;
    float *seg;
    int e8, e9;
    int *node;
    float *pv;
    unsigned int u12;
    int nearestPlayer;
    int i13;
    float *pv14;
    int i15;
    float *pv17;
    int i20, i21, i22, i23, i24;
    double d25, d26, d28, d29;
    double dtmp;

    (void)FUN_803826d0();
    nearestPlayer = 0;
    sound = false;
    float box[6];  // local_8f0[0..5]
    box[3] = *(float *)&DAT_803df188;
    box[4] = *(float *)&DAT_803df18c;
    box[5] = *(float *)&DAT_803df190;
    box[0] = *(float *)&DAT_803df194;
    box[1] = *(float *)&DAT_803df198;
    box[2] = *(float *)&DAT_803df19c;

    if ((DAT_80612638 != 0) && ((*(char *)(DAT_80611b1c + 0x18) != '\0') || (DAT_80610d1c != 0))) {
        // --- pass 1: advance depth, spawn segment entity, update 3d sound ---
        i21 = 0; i20 = 0; i22 = 0;
        do {
            seg = (float *)(DAT_80612630 + i22);
            if (seg[9] != 5.60519e-45f) {
                f6 = seg[1]; f1 = seg[4]; f4 = seg[0] - seg[3]; f2 = seg[2]; f3 = seg[5];
                seg[3] = seg[0];
                *(int *)(DAT_80612630 + i22 + 0x10) = *(int *)(DAT_80612630 + i22 + 4);
                *(int *)(DAT_80612630 + i22 + 0x14) = *(int *)(DAT_80612630 + i22 + 8);
                d26 = math_sqrt((double)((f2 - f3) * (f2 - f3) + f4 * f4 + (f6 - f1) * (f6 - f1)));
                double spd = (double)(float)((double)(int)DAT_806109c0 * d26);
                d26 = (double)((float)(spd / (double)(int)DAT_806108e4) * FLOAT_80616f38);
                if ((double)FLOAT_80616ed0 < d26) d26 = (double)FLOAT_80616ed0;
                scalar_step_toward_snap(d26, (double)FLOAT_80616edc, (float *)(DAT_80612630 + i22 + 0x18));
                if (*(int *)((int)&DAT_805c3518 + i20) == 0) {
                    worm_spawn_segment(i21);
                }
                if (*(int *)((int)&DAT_805c3518 + i20) != 0) {
                    FUN_801cf874(*(int *)(*(int *)((int)&DAT_805c3518 + i20) + 0x7c), 0, 0x2000000, 0);
                }
                f6 = *(float *)(DAT_80612630 + i22 + 0x18);
                e8 = __wr_sound3d((double)(FLOAT_80616f8c * f6), (double)FLOAT_80616fa8,
                                            (double)(FLOAT_80616ee0 * f6 + FLOAT_80616ee0),
                                            *(int *)((int)&DAT_805c3518 + i20) + 0x22c,
                                            DAT_80612630 + i22, 0x155, 1);
                if (e8 != 0) sound = true;
            }
            i21 = i21 + 1; i22 = i22 + 0x450; i20 = i20 + 4;
        } while (i21 < 5);
        if (((*(int *)(DAT_80612630 + 0x1164) == 0) || (*(int *)(DAT_80612630 + 0x1164) == 1)) && (!sound)) {
            DAT_80610d1c = 0;
        }

        // --- pass 2: per-segment state machine ---
        i20 = 0; i22 = 0; i21 = 0;
        do {
            e8 = *(int *)((int)&DAT_805c3518 + i22);
            d28 = (double)FLOAT_80616fac;
            seg = (float *)(DAT_80612630 + i21);
            if (((e8 != 0) && (*(int *)(e8 + 4) != 0)) && ((*(unsigned int *)(e8 + 0x28) & 0x80000000) == 0)) {
                // nearest player scan
                i15 = 0; e9 = 0; i24 = DAT_80612610;
                if (0 < DAT_80612610) {
                    do {
                        i13 = DAT_80611d78 + e9;
                        f4 = *(float *)(i13 + 0xa8) - *seg;
                        f6 = *(float *)(i13 + 0xac) - seg[1];
                        f2 = *(float *)(i13 + 0xb0) - seg[2];
                        d25 = (double)(float)(f2 * f2 + (double)(float)(f4 * f4 + (double)(float)(f6 * f6)));
                        if (d25 < d28) { nearestPlayer = i15; d28 = d25; }
                        i15 = i15 + 1; e9 = e9 + 0x141c; i24 = i24 + -1;
                    } while (i24 != 0);
                }
                // render-flag by state band
                f6 = seg[9];
                if ((int)f6 < 9) {
                    if ((f6 != 2.8026e-45f) && ((1 < (int)f6) || (-1 < (int)f6))) {
                        *(unsigned int *)(e8 + 0x28) = *(unsigned int *)(e8 + 0x28) & 0xffffffbf;
                    }
                } else {
                    if (f6 != 2.24208e-44f) {
                        if (0xf < (int)f6) goto after_flag;
                        if (0xe < (int)f6) { *(unsigned int *)(e8 + 0x28) = *(unsigned int *)(e8 + 0x28) & 0xffffffbf; goto after_flag; }
                    }
                    *(unsigned int *)(e8 + 0x28) = *(unsigned int *)(e8 + 0x28) | 0x40;
                }
            after_flag:
                if (seg[9] == 5.60519e-45f) {
                    // dead/despawn state: keep or destroy the entity
                    sound = false;
                    e8 = (&DAT_805c3518)[((int)(size_t)seg - DAT_80612630) / 0x450];
                    if (((e8 != 0) && (i24 = *(int *)(e8 + 0x7c), i24 != 0)) && (i24 != -0xc68)) {
                        double diff = (double)(float)((double)*(float *)(i24 + 0xcfc) - (double)*(float *)(i24 + 0xcf8));
                        if (diff < (double)(float)((double)FLOAT_80616f34 *
                                     (double)(float)((double)FLOAT_80616ed0 / (double)(int)DAT_806109c0))) {
                            sound = true;
                        }
                    }
                    if (!sound) {
                        if (e8 != 0) {
                            aiactor_destroy_instance(0, 0, 0, 0, 0, param_6, param_7, param_8, e8);
                        }
                        (&DAT_805c3518)[((int)(size_t)seg - DAT_80612630) / 0x450] = 0;
                    }
                } else {
                    __worm_update_segment_state(seg, e8, i20, i21, i22, nearestPlayer, box);
                }
            }
            i20 = i20 + 1; i21 = i21 + 0x450; i22 = i22 + 4;
        } while (i20 < 5);
    }
    FUN_8038271c();
    (void)param_2; (void)param_3; (void)param_4; (void)pv; (void)pv14; (void)pv17;
    (void)node; (void)u12; (void)d29; (void)dtmp;
    return;
}

// Helper factored out of 0x801c88e8's per-segment `else` branch (non-matching
// split for readability): runs the full emerge/hunt/attack/submerge state
// machine for one live worm segment. `seg` is the segment record, `ownerEnt`
// its WRM1 entity, i20/segByteOff/idOff the loop indices, nearestPlayer the
// scan result, box the const AABB clamp. State is the record's word 9 (raw int
// bit patterns reinterpreted as float in the ROM; compared as float constants
// verbatim). The heavy per-player camera-shake loops and steering math preserve
// the original call order.
static void __worm_update_segment_state(float *seg, int ownerEnt, int i20, int segByteOff,
                                           int idOff, int nearestPlayer, float *box)
{
    float f1, f6;
    int e8, e9, i8, i24, i9, i15;
    int *node;
    float *pv11, *pv14;
    double d25, d26, d27, d28, d29, d30, d31, d32, d33, d34;
    double param_2, param_3, param_4, param_6, param_7, param_8;
    unsigned int u12;

    param_6 = 0; param_7 = 0; param_8 = 0;

    float st = seg[9];
    if (st == 9.80909e-45f) {
        // seeking the entry zone 0x2349: if a player is close in XZ, surface
        d28 = (double)FLOAT_80616ef4;
        i8 = 0;
        for (i24 = 0; i24 < DAT_80612610; i24 = i24 + 1) {
            e9 = soundpad_find_zone_by_id(0x2349);
            node = soundpad_resolve_zone_node(e9);
            d26 = (double)(*(float *)(DAT_80611d78 + i8 + 0xb0) - (float)node[5]);
            e9 = soundpad_find_zone_by_id(0x2349);
            node = soundpad_resolve_zone_node(e9);
            d29 = (double)(float)((double)(*(float *)(DAT_80611d78 + i8 + 0xb0) - (float)node[5]) * d26);
            e9 = soundpad_find_zone_by_id(0x2349);
            node = soundpad_resolve_zone_node(e9);
            d25 = (double)(*(float *)(DAT_80611d78 + i8 + 0xa8) - (float)node[3]);
            e9 = soundpad_find_zone_by_id(0x2349);
            node = soundpad_resolve_zone_node(e9);
            d26 = (double)(float)node[3];
            if ((double)(float)((double)(float)((double)*(float *)(DAT_80611d78 + i8 + 0xa8) - d26) * d25 + d29) < d28) {
                seg[9] = 7.00649e-45f;
                seg[0x113] = 9.80909e-45f;
            }
            i8 = i8 + 0x141c;
        }
    }

    st = seg[9];
    if (((((st == 0.0f) || ((int)st - 1U < 2)) || ((int)st - 5U < 2)) || (st == 4.2039e-45f)) && (DAT_806108e4 != 0)) {
        // active hunt/attack cluster
        e8 = worm_zone_slot_free_test();
        if (e8 == 0) {
            if ((seg[9] == 0.0f) || (seg[9] == 4.2039e-45f)) {
                seg[9] = 1.4013e-45f;
                seg[0x110] = __wr_neg_nan();
                seg[0x111] = __wr_neg_nan();
            }
        } else if (seg[9] == 1.4013e-45f) {
            seg[9] = 0.0f;
        }

        f6 = seg[9];
        if (((f6 == 1.4013e-45f) || (f6 == 2.8026e-45f)) || (f6 == 4.2039e-45f)) {
            // pick a patrol target zone, steer yaw toward it, integrate position
            f6 = (float)soundpad_find_nearest_entity((double)FLOAT_80616ed8, seg);
            if (f6 != seg[0x110]) {
                node = soundpad_resolve_zone_node((int)f6);
                d26 = (double)((float)node[4] - seg[1]);
                node = soundpad_resolve_zone_node((int)f6);
                d28 = (double)(float)((double)((float)node[4] - seg[1]) * d26);
                node = soundpad_resolve_zone_node((int)f6);
                d26 = (double)((float)node[3] - *seg);
                node = soundpad_resolve_zone_node((int)f6);
                d28 = (double)(float)((double)((float)node[3] - *seg) * d26 + d28);
                node = soundpad_resolve_zone_node((int)f6);
                d26 = (double)((float)node[5] - seg[2]);
                node = soundpad_resolve_zone_node((int)f6);
                if (FLOAT_80616ee4 > (float)((double)((float)node[5] - seg[2]) * d26 + d28)) {
                    seg[0x111] = seg[0x110];
                    if (seg[0x111] == __wr_neg_nan()) seg[0x111] = f6;
                    seg[0x110] = f6;
                }
            }

            if (seg[9] == 4.2039e-45f) {
                float navTgt[6];
                navTgt[0] = seg[0x110];
                navTgt[1] = *(float *)(*(int *)(*(int *)(DAT_80611d78 + nearestPlayer * 0x141c + 0x14) + 0x7c) + 0x678);
                navTgt[2] = 0.0f; navTgt[3] = 0.0f;
                e8 = aiactor_nav_advance_path(0, (int *)navTgt);
                if (e8 < 0) {
                    seg[9] = 0.0f;
                    pv11 = (float *)(DAT_80611d78 + nearestPlayer * 0x141c + 0xa8);
                } else {
                    node = soundpad_resolve_zone_node(e8);
                    pv11 = (float *)(node + 3);
                }
            } else {
                bool onPatrol = false;
                i24 = 0; i8 = 0; i9 = 6;
                do {
                    if (seg[0x110] == *(float *)((int)&DAT_80450104 + i8)) { onPatrol = true; break; }
                    i24 = i24 + 1; i8 = i8 + 4; i9 = i9 + -1;
                } while (i9 != 0);
                soundpad_resolve_zone_node((int)seg[0x111]);
                if (onPatrol) {
                    node = soundpad_resolve_zone_node((&DAT_80450104)[(i24 + 1) % 6]);
                    pv11 = (float *)(node + 3);
                    soundpad_resolve_zone_node((&DAT_80450104)[(i24 + 2) % 6]);
                } else {
                    d26 = (double)FLOAT_80616fac; i8 = 0; i9 = 0; i24 = 0;
                    do {
                        node = soundpad_resolve_zone_node(*(int *)((int)&DAT_80450104 + i24));
                        d28 = (double)(seg[1] - (float)node[4]);
                        node = soundpad_resolve_zone_node(*(int *)((int)&DAT_80450104 + i24));
                        d25 = (double)(float)((double)(seg[1] - (float)node[4]) * d28);
                        node = soundpad_resolve_zone_node(*(int *)((int)&DAT_80450104 + i24));
                        d28 = (double)(*seg - (float)node[3]);
                        node = soundpad_resolve_zone_node(*(int *)((int)&DAT_80450104 + i24));
                        d25 = (double)(float)((double)(*seg - (float)node[3]) * d28 + d25);
                        node = soundpad_resolve_zone_node(*(int *)((int)&DAT_80450104 + i24));
                        d28 = (double)(seg[2] - (float)node[5]);
                        node = soundpad_resolve_zone_node(*(int *)((int)&DAT_80450104 + i24));
                        d28 = (double)(float)((double)(seg[2] - (float)node[5]) * d28 + d25);
                        if (d28 < d26) { i8 = i9; d26 = d28; }
                        i9 = i9 + 1; i24 = i24 + 4;
                    } while (i9 < 6);
                    float navTgt[6];
                    navTgt[0] = seg[0x110];
                    navTgt[1] = (float)(&DAT_80450104)[i8];
                    navTgt[2] = 0.0f; navTgt[3] = 0.0f;
                    e8 = aiactor_nav_advance_path(0, (int *)navTgt);
                    if (e8 < 1) {
                        pv11 = (float *)(DAT_80611d78 + nearestPlayer * 0x141c + 0xa8);
                    } else {
                        node = soundpad_resolve_zone_node(e8);
                        pv11 = (float *)(node + 3);
                    }
                }
            }
            d28 = (double)(pv11[0] - *seg);
            d29 = (double)(pv11[2] - seg[2]);
            d25 = (double)(pv11[1] - seg[1]);
            d26 = (double)(float)math_atan2(d28, d29);
            d28 = (double)(float)(d29 * d29 + (double)(float)(d28 * d28 + (double)(float)(d25 * d25)));
            if ((double)FLOAT_80616fb0 < d28) {
                FUN_801cf804((double)FLOAT_80616ed0, d28);
            }
            d25 = (double)(FLOAT_80616f30 * FLOAT_80616fb4 *
                           (FLOAT_80616ed0 / (float)(int)DAT_806109c0) * (float)(int)DAT_806108e4);
            d28 = FUN_801cf838(d26, (double)seg[7]);
            if (d28 <= d25) {
                d26 = FUN_801cf838(d26, (double)seg[7]);
                if (d26 < -d25) seg[7] = (float)((double)seg[7] + d25);
            } else {
                seg[7] = (float)((double)seg[7] - d25);
            }
            // integrate forward along heading
            u12 = (unsigned int)(FLOAT_80616f64 * seg[7] + FLOAT_80616ee0);
            double fwd = (double)(FLOAT_80616fb8 * (FLOAT_80616ed0 / (float)(int)DAT_806109c0)) * (double)(int)DAT_806108e4;
            *seg = (float)(fwd * (double)(float)(&DAT_80583228)[u12 & 0x7ff] + (double)*seg);
            e8 = (int)(FLOAT_80616f64 * seg[7] + FLOAT_80616ee0);
            double lat = (double)*(float *)((int)&DAT_80583228 + ((e8 + 0x200) * 4 & 0x1ffcU));
            double fwd2 = (double)(FLOAT_80616fb8 * (FLOAT_80616ed0 / (float)(int)DAT_806109c0) * (float)(int)DAT_806108e4);
            seg[2] = (float)(fwd2 * lat + (double)seg[2]);
            seg[1] = FLOAT_80616fbc;
            // clamp to AABB
            i8 = 3; i24 = 0;
            d26 = (double)FLOAT_80616fc0;
            do {
                f6 = *(float *)((int)box + i24);
                if ((double)*(float *)((int)seg + i24) <= (double)f6) {
                    f6 = *(float *)((int)box + i24 + 0xc);
                    if ((double)*(float *)((int)seg + i24) < (double)f6) {
                        seg[7] = (float)((double)seg[7] + d26);
                        *(float *)((int)seg + i24) = f6;
                    }
                } else {
                    seg[7] = (float)((double)seg[7] + d26);
                    *(float *)((int)seg + i24) = f6;
                }
                i24 = i24 + 4; i8 = i8 + -1;
            } while (i8 != 0);
            __worm_state_attack_or_submerge(seg, ownerEnt, i20, segByteOff, nearestPlayer);
        } else {
            // aligned but not moving: rotate yaw toward nearest player
            e8 = DAT_80611d78 + nearestPlayer * 0x141c;
            d26 = math_atan2((double)(*(float *)(e8 + 0xa8) - *seg), (double)(*(float *)(e8 + 0xb0) - seg[2]));
            d26 = FUN_801cf838((double)seg[7], (double)(FLOAT_80616f30 + (float)d26));
            double step = (double)(FLOAT_80616f30 * FLOAT_80616f04 *
                                   (FLOAT_80616ed0 / (float)(int)DAT_806109c0) * (float)(int)DAT_806108e4);
            if (d26 <= (double)FLOAT_80616ed8) {
                seg[7] = (float)(step + (double)seg[7]);
            } else {
                seg[7] = (float)(-step - (double)-(double)seg[7]);
                seg[7] = -(float)(step - (double)seg[7]);
            }
            __worm_state_attack_or_submerge(seg, ownerEnt, i20, segByteOff, nearestPlayer);
        }
    }
    (void)pv14; (void)param_2; (void)param_3; (void)param_4; (void)d30; (void)d31; (void)d32; (void)d33; (void)d34; (void)i15;
}

// Shared camera-shake pulse over all players, scaled by distance to this segment
// (the identical loop appears in states 9 and 14 of the ROM; factored out here).
static void worm_apply_players_camera_shake(float *seg)
{
    int i8 = 0;
    double d33 = (double)FLOAT_80616ed0;
    double d34 = (double)FLOAT_80616f34;
    double d32 = (double)FLOAT_80616ef0;
    double d31 = (double)FLOAT_80616f3c;
    double d30 = (double)FLOAT_80616fd0;
    double d29 = (double)FLOAT_80616f04;
    double d28 = (double)FLOAT_80616edc;
    int i24;
    for (i24 = 0; i24 < DAT_80612610; i24 = i24 + 1) {
        int i9 = DAT_80611d78 + i8;
        double dx = (double)(seg[0] - *(float *)(i9 + 0xa8));
        float  fy = seg[1] - *(float *)(i9 + 0xac);
        float  fz = seg[2] - *(float *)(i9 + 0xb0);
        double dist = math_sqrt((double)(fz * fz + (float)(dx * dx) + fy * fy));
        double s = d33;
        if ((double)(float)(dist / d34) < d33) { s = (double)(float)(dist / d34); }
        s = (double)(float)(d32 * (double)(float)(d33 - s));
        player_apply_camera_shake((float)(d31 * s), (float)(d30 * s),
                                  (float)(d33 / (float)(int)DAT_806109c0), DAT_80611d78 + i8);
        {
            int off54 = i8 + 0xe54;
            int off58 = i8 + 0xe58;
            i8 = i8 + 0x141c;
            *(float *)(DAT_80611d78 + off54) =
                (float)(d29 * s + (double)*(float *)(DAT_80611d78 + off54));
            *(float *)(DAT_80611d78 + off58) = (float)d28;
        }
    }
}
// -----------------------------------------------------------------------------
// __worm_state_attack_or_submerge  (tail of FUN_801c88e8, states >= 8)
//   Runs the emerge/lunge/attack/submerge/retreat state machine for one worm
//   segment once it has committed to surfacing (seg[9] >= 8). Split out of
//   __worm_update_segment_state for tractability; call order preserved.
//   seg         = segment record (float*, pfVar7)
//   ownerEnt    = entity slot for this segment ((&DAT_805c3518)[idx], iVar8)
//   segIndex    = segment loop index 0..4 (iVar20)
//   segByteOff  = segIndex * 0x450 byte offset into DAT_80612630 (iVar21)
//   nearestPlayer = index of closest player (iVar23)
// Non-matching: register-pair spill locals dropped; magic-bias int->double
//   reduced to (double)(int); scene_raycast/switch_slot_reset/rng_counter_tick
//   are declared void in the shared headers but return values here, so they are
//   reached through file-local typed function-pointer aliases; anim_primary_clip_phase
//   likewise (header decl returns int, ROM returns a float phase).
// -----------------------------------------------------------------------------


static void __worm_state_attack_or_submerge(float *seg, int ownerEnt, int segIndex,
                                              int segByteOff, int nearestPlayer)
{
    (void)segByteOff;
    if (!((7 < (int)seg[9]) && (DAT_806108e4 != 0))) {
        return;
    }
    {
        int   i8, i24, i9, i15;
        float f6, f1;
        double d25, d26, d28;
        float local_8fc[3];   // burst particle spawn params (0x805c28c4 blob copy)
        float mtx[16];        // afStack_898 style transform
        float rayOrg[3], rayDir[3], rayHit[3], rayNrm[3], rayFlip[3];
        int   evt[6];         // switch/damage event block

        // seed the const-pool burst params
        local_8fc[0] = *(float *)&DAT_803df188;  // reuse of the const block
        local_8fc[1] = *(float *)&DAT_803df18c;
        local_8fc[2] = *(float *)&DAT_803df190;

        // ---- state 8 (1.12104e-44): begin surfacing ----
        if (seg[9] == 1.12104e-44f) {
            float burst[3];
            burst[0] = *(float *)&DAT_803df1a0;
            burst[1] = *(float *)&DAT_803df1a4;
            burst[2] = *(float *)&DAT_803df1a8;
            seg[10] = FLOAT_80616ed8;
            seg[0]  = seg[0xb];
            seg[1]  = seg[0xc];
            seg[2]  = seg[0xd];
            seg[9]  = 1.26117e-44f;       // -> state 9
            seg[0x112] = 0.0f;
            if (seg[0x113] != 2.8026e-45f) {
                particle_emitter_spawn(0, (u32)(size_t)&DAT_805c28c4, 0, seg, burst);
            }
        }

        f6 = seg[9];
        // ---- state 9 (1.26117e-44): emerge / lunge / bite ----
        if (f6 == 1.26117e-44f) {
            d28 = (double)seg[10];
            if (seg[0x113] == 2.8026e-45f) {
                // "buried" variant: wait for surfacing anim, then push body pts down
                i24 = anim_is_clip_active(ownerEnt, 0x149);
                if (i24 == 0) {
                    anim_play_scaled((double)FLOAT_80616ed8, (double)FLOAT_80616ed0);
                }
                i24 = anim_active_slot_userflags(*(int *)(ownerEnt + 4));
                if (i24 != 0) {
                    DAT_80612634 = DAT_80612634 + -1;
                    seg[0x113] = __wr_neg_nan();
                    seg[9]  = 8.40779e-45f;       // -> state 2 (idle-ish)
                    seg[10] = FLOAT_80616f9c;
                    i9 = 0x40; i24 = 0;
                    do {
                        *(float *)((int)(size_t)seg + i24 + 0x3c) = seg[0];
                        *(float *)((int)(size_t)seg + i24 + 0x40) = seg[1];
                        *(float *)((int)(size_t)seg + i24 + 0x44) = seg[2];
                        *(float *)((int)(size_t)seg + i24 + 0x40) =
                            *(float *)((int)(size_t)seg + i24 + 0x40) + seg[10];
                        i9 = i9 + -1; i24 = i24 + 0xc;
                    } while (i9 != 0);
                }
            } else {
                // normal emerge: play bark, raycast forward, deal contact damage
                i24 = anim_is_clip_active(ownerEnt, 0x194);
                if (i24 == 0) {
                    unsigned int r;
                    anim_play_scaled((double)FLOAT_80616ed8, (double)FLOAT_80616ed0);
                    r = __wr_rng_tick();
                    FUN_801cf784((double)FLOAT_80616ee4, (int)(r % 3 + 0x13a8), seg);
                    FUN_801cf784((double)FLOAT_80616ee4, 0x13af, seg);
                }
                i24 = *(int *)(*(int *)(ownerEnt + 4) + 0x10);
                rayOrg[0] = *(float *)(i24 + 0x1b0);
                rayOrg[1] = *(float *)(i24 + 0x1b4);
                rayOrg[2] = *(float *)(i24 + 0x1b8);
                rayDir[0] = *(float *)(i24 + 0x1f0) - rayOrg[0];
                rayDir[1] = *(float *)(i24 + 500)   - rayOrg[1];
                rayDir[2] = *(float *)(i24 + 0x1f8) - rayOrg[2];
                mtx4_set_translate_rotz((double)*(float *)(ownerEnt + 100),
                                        (double)*(float *)(ownerEnt + 0x68),
                                        (double)*(float *)(ownerEnt + 0x6c),
                                        (double)((FLOAT_80616f30 * *(float *)(ownerEnt + 0x70)) / FLOAT_80616f68),
                                        mtx);
                mtx4_transform_point(mtx, rayOrg);
                mtx4_transform_vector(mtx, rayDir);
                rayDir[0] = (float)((double)rayDir[0] * (double)FLOAT_80616fa4);
                rayDir[2] = (float)((double)rayDir[2] * (double)FLOAT_80616fa4);
                rayDir[1] = (float)((double)rayDir[1] * (double)FLOAT_80616fa4) * FLOAT_80616edc;
                rayOrg[0] = (float)((double)FLOAT_80616ef0 * (double)rayDir[0] + (double)rayOrg[0]);
                rayOrg[1] = (float)((double)FLOAT_80616ef0 * (double)rayDir[1] + (double)rayOrg[1]);
                rayOrg[2] = (float)((double)FLOAT_80616ef0 * (double)rayDir[2] + (double)rayOrg[2]);
                {
                    u64 hit = __wr_raycast(*(int *)(ownerEnt + 0x34), rayOrg, rayDir, 0,
                                           rayHit, rayNrm, (int *)0, 0xfffffff7u, 0, (int *)0);
                    int hitNode = (int)(hit >> 0x20);
                    if ((hitNode != 0) && (hitNode != ownerEnt)) {
                        rayFlip[0] = -rayNrm[0];
                        rayFlip[1] = -rayNrm[1];
                        rayFlip[2] = -rayNrm[2];
                        if (*(int *)(hitNode + 0x150) != 0) {
                            FUN_80234560((double)rayFlip[1], (double)rayFlip[0], 0, 0, 0, 0, 0, 0,
                                         0x53464343, hitNode, (int)(size_t)rayDir, 0,
                                         rayHit, rayFlip, 0, -9);
                        }
                        __wr_switch_reset((int)(size_t)evt);
                        evt[0] = hitNode;   // local_7fc
                        evt[1] = ownerEnt;
                        evt[2] = ownerEnt;
                        evt[3] = ownerEnt;
                        damage_dispatch_to_entity(evt);
                        seg[0x112] = 1.4013e-45f;
                    }
                }
                i24 = anim_active_slot_userflags(*(int *)(ownerEnt + 4));
                if (i24 != 0) {
                    int pl = DAT_80611d78 + nearestPlayer * 0x141c;
                    unsigned int u = (unsigned int)(FLOAT_80616f64 * seg[0xe] + FLOAT_80616ee0);
                    double dx = (double)(*(float *)(pl + 0xa8) - seg[0]);
                    double dz = (double)(*(float *)(pl + 0xb0) - seg[2]);
                    double dot = (double)*(float *)((int)&DAT_80583228 + ((u + 0x200) * 4 & 0x1ffc)) * dz +
                                 (double)(float)((double)(float)(&DAT_80583228)[u & 0x7ff] * dx + (double)FLOAT_80616ed8);
                    if ((seg[0x112] != 0.0f) ||
                        (((double)FLOAT_80616ed8 <= dot) &&
                         ((float)(dz * dz + (double)(float)(dx * dx + (double)FLOAT_80616ed8)) <= FLOAT_80616fe0))) {
                        seg[9] = 1.96182e-44f;    // -> state 14 (recover)
                    } else {
                        seg[9] = 1.54143e-44f;    // -> state 11 (turn)
                    }
                }
            }
            // depth tracking (worm_segment_target_depth = worm_segment_target_depth)
            {
                double td = worm_segment_target_depth((int)(size_t)seg, ownerEnt);
                d26 = (double)(float)(td * (double)FLOAT_80616f38);
                if (d26 < (double)FLOAT_80616f04) { d26 = (double)FLOAT_80616f04; }
                d25 = (double)FLOAT_80616ed8;
                if (d25 < d26) {
                    double td2 = worm_segment_target_depth((int)(size_t)seg, ownerEnt);
                    d25 = (double)(float)(td2 * (double)FLOAT_80616f38);
                    if (d25 < (double)FLOAT_80616f04) { d25 = (double)FLOAT_80616f04; }
                }
                seg[10] = (float)d25;
            }
            if ((seg[0x113] == 2.8026e-45f) && ((double)FLOAT_80616ed8 < (double)seg[10]) &&
                (d28 <= (double)FLOAT_80616ed8) &&
                (__wr_clip_phase(*(int *)(ownerEnt + 4)) > (double)FLOAT_80616edc)) {
                float burst[3], burst2[3];
                burst[0]  = *(float *)&DAT_803df1ac;
                burst[1]  = *(float *)&DAT_803df1b0;
                burst[2]  = *(float *)&DAT_803df1b4;
                burst2[0] = *(float *)&DAT_803df1b8;
                burst2[1] = *(float *)&DAT_803df1bc;
                burst2[2] = *(float *)&DAT_803df1c0;
                particle_emitter_spawn(0, (u32)(size_t)&DAT_805c28c4, 0, burst, burst2);
            }
            if (((double)seg[10] < (double)FLOAT_80616f0c) &&
                ((seg[0x113] != 2.8026e-45f) ||
                 (((double)FLOAT_80616ed8 < (double)seg[10]) && (d28 <= (double)FLOAT_80616ed8)))) {
                worm_apply_players_camera_shake(seg);
            }
        }
        // ---- state 14 (1.96182e-44): recover after bite ----
        else if (f6 == 1.96182e-44f) {
            i24 = anim_is_clip_active(ownerEnt, 0x198);
            if (i24 == 0) { anim_play_scaled((double)FLOAT_80616ed8, (double)FLOAT_80616ed0); }
            {
                double td = worm_segment_target_depth((int)(size_t)seg, ownerEnt);
                d26 = (double)(float)(td * (double)FLOAT_80616f38);
                if (d26 < (double)FLOAT_80616f04) { d26 = (double)FLOAT_80616f04; }
                d25 = (double)FLOAT_80616ed8;
                if (d25 < d26) {
                    double td2 = worm_segment_target_depth((int)(size_t)seg, ownerEnt);
                    d25 = (double)(float)(td2 * (double)FLOAT_80616f38);
                    if (d25 < (double)FLOAT_80616f04) { d25 = (double)FLOAT_80616f04; }
                }
                seg[10] = (float)d25;
            }
            if (((double)FLOAT_80616ed8 == (double)seg[10]) ||
                (anim_active_slot_userflags(*(int *)(ownerEnt + 4)) != 0)) {
                DAT_80612634 = DAT_80612634 + -1;
                seg[9]  = 8.40779e-45f;
                seg[10] = FLOAT_80616f9c;
                i9 = 0x40; i24 = 0;
                do {
                    *(float *)((int)(size_t)seg + i24 + 0x3c) = seg[0];
                    *(float *)((int)(size_t)seg + i24 + 0x40) = seg[1];
                    *(float *)((int)(size_t)seg + i24 + 0x44) = seg[2];
                    *(float *)((int)(size_t)seg + i24 + 0x40) =
                        *(float *)((int)(size_t)seg + i24 + 0x40) + seg[10];
                    i9 = i9 + -1; i24 = i24 + 0xc;
                } while (i9 != 0);
            } else {
                worm_apply_players_camera_shake(seg);
            }
        }
        // ---- state 10 (1.4013e-44): aim head toward player + bite raycast ----
        else if (f6 == 1.4013e-44f) {
            double d26b;
            i24 = *(int *)(*(int *)(ownerEnt + 4) + 0x10);
            rayOrg[0] = *(float *)(i24 + 0x1b0);
            rayOrg[1] = *(float *)(i24 + 0x1b4);
            rayOrg[2] = *(float *)(i24 + 0x1b8);
            rayDir[0] = *(float *)(i24 + 0x1f0) - rayOrg[0];
            rayDir[1] = *(float *)(i24 + 500)   - rayOrg[1];
            rayDir[2] = *(float *)(i24 + 0x1f8) - rayOrg[2];
            mtx4_set_translate_rotz((double)*(float *)(ownerEnt + 100),
                                    (double)*(float *)(ownerEnt + 0x68),
                                    (double)*(float *)(ownerEnt + 0x6c),
                                    (double)((FLOAT_80616f30 * *(float *)(ownerEnt + 0x70)) / FLOAT_80616f68),
                                    mtx);
            mtx4_transform_point(mtx, rayOrg);
            mtx4_transform_vector(mtx, rayDir);
            rayDir[0] = (float)((double)rayDir[0] * (double)FLOAT_80616fa4);
            rayDir[2] = (float)((double)rayDir[2] * (double)FLOAT_80616fa4);
            rayDir[1] = (float)((double)rayDir[1] * (double)FLOAT_80616fa4) * FLOAT_80616edc;
            rayOrg[0] = (float)((double)FLOAT_80616ef0 * (double)rayDir[0] + (double)rayOrg[0]);
            rayOrg[1] = (float)((double)FLOAT_80616ef0 * (double)rayDir[1] + (double)rayOrg[1]);
            rayOrg[2] = (float)((double)FLOAT_80616ef0 * (double)rayDir[2] + (double)rayOrg[2]);
            {
                u64 hit = __wr_raycast(*(int *)(ownerEnt + 0x34), rayOrg, rayDir, 0,
                                       rayHit, rayNrm, (int *)0, 0xfffffff7u, 0, (int *)0);
                int hitNode = (int)(hit >> 0x20);
                if ((hitNode != 0) && (hitNode != ownerEnt)) {
                    rayFlip[0] = -rayNrm[0];
                    rayFlip[1] = -rayNrm[1];
                    rayFlip[2] = -rayNrm[2];
                    __wr_switch_reset((int)(size_t)evt);
                    evt[0] = hitNode;
                    evt[1] = ownerEnt;
                    evt[2] = ownerEnt;
                    evt[3] = ownerEnt;
                    damage_dispatch_to_entity(evt);
                }
            }
            {
                int pl = DAT_80611d78 + nearestPlayer * 0x141c;
                d26b = math_atan2((double)(*(float *)(pl + 0xa8) - seg[0]),
                                  (double)(*(float *)(pl + 0xb0) - seg[2]));
                d26b = FUN_801cf838((double)seg[0xe], (double)(float)d26b);
                {
                    float rate = FLOAT_80616ed0 / (float)(int)DAT_806109c0;
                    double lo = (double)(FLOAT_80616f28 * rate) * (double)(int)DAT_806108e4;
                    if (d26b <= lo) {
                        double hi = (double)(FLOAT_80616fe4 * rate) * (double)(int)DAT_806108e4;
                        if (d26b < hi) { seg[0xe] = (float)((double)seg[0xe] - lo); }
                    } else {
                        seg[0xe] = (float)((double)seg[0xe] + lo);
                    }
                }
            }
            i24 = anim_is_clip_active(ownerEnt, 0x197);
            if (i24 == 0) { anim_play_scaled((double)FLOAT_80616ed8, (double)FLOAT_80616ed0); }
            if (anim_active_slot_userflags(*(int *)(ownerEnt + 4)) != 0) {
                seg[9] = 1.96182e-44f;
            }
        }
        // ---- state 11 (1.54143e-44): turn toward player ----
        else if (f6 == 1.54143e-44f) {
            i24 = anim_is_clip_active(ownerEnt, 0x195);
            if (i24 == 0) { anim_play_scaled((double)FLOAT_80616ed8, (double)FLOAT_80616ed0); }
            if (anim_active_slot_userflags(*(int *)(ownerEnt + 4)) != 0) {
                int pl = DAT_80611d78 + nearestPlayer * 0x141c;
                unsigned int u = (unsigned int)(FLOAT_80616f64 * seg[0xe] + FLOAT_80616ee0);
                double dx = (double)(*(float *)(pl + 0xa8) - seg[0]);
                double dz = (double)(*(float *)(pl + 0xb0) - seg[2]);
                double dot = (double)*(float *)((int)&DAT_80583228 + ((u + 0x200) * 4 & 0x1ffc)) * dz +
                             (double)(float)((double)(float)(&DAT_80583228)[u & 0x7ff] * dx + (double)FLOAT_80616ed8);
                if ((dot < (double)FLOAT_80616ed8) ||
                    ((double)FLOAT_80616fe0 < (double)(float)(dz * dz + (double)(float)(dx * dx + (double)FLOAT_80616ed8)))) {
                    seg[9] = 1.96182e-44f;
                } else {
                    seg[9] = 1.4013e-44f;
                }
            }
        }
        // ---- state 15 (2.10195e-44): finish / cleanup burst ----
        else if (f6 == 2.10195e-44f) {
            i24 = anim_is_clip_active(ownerEnt, 0x19b);
            if (i24 == 0) {
                unsigned int r;
                anim_play_scaled((double)FLOAT_80616ed8, (double)FLOAT_80616ed0);
                r = __wr_rng_tick();
                FUN_801cf784((double)FLOAT_80616ee4, (int)(r % 3 + 0x13ab), seg);
            }
            if (anim_active_slot_userflags(*(int *)(ownerEnt + 4)) != 0) {
                float handle = seg[0x10f];
                particle_handle_destroy((int *)&handle);
                character_opacity_fade_in((float)FLOAT_80616f34, *(int *)(ownerEnt + 0x7c) + 0xc68);
                seg[9] = 5.60519e-45f;    // -> despawn
                DAT_80612634 = DAT_80612634 + -1;
            }
            seg[10] = (float)((double)seg[10] * (double)FLOAT_80616ed4);
        }
        // ---- state 16 (2.24208e-44) ----
        else if (f6 == 2.24208e-44f) {
            i24 = anim_is_clip_active(ownerEnt, 0x199);
            if (i24 == 0) { anim_play_scaled((double)FLOAT_80616ed8, (double)FLOAT_80616ed0); }
            if (anim_active_slot_userflags(*(int *)(ownerEnt + 4)) != 0) {
                seg[9] = 1.82169e-44f;    // -> state 13
            }
        }
        // ---- state 12 (1.68156e-44): wind-up / count taunts ----
        else if (f6 == 1.68156e-44f) {
            i24 = anim_active_slot_userflags(*(int *)(ownerEnt + 4));
            if (i24 != 0) {
                i24 = anim_is_clip_active(ownerEnt, 0x199);
                if (i24 == 0) {
                    i8 = anim_is_clip_active(ownerEnt, 0x196);
                    if (i8 == 0) {
                        unsigned int r = __wr_rng_tick();
                        FUN_801cf784((double)FLOAT_80616ee4, (int)(r % 3 + 0x13a8), seg);
                        anim_play_scaled((double)FLOAT_80616ed8, (double)FLOAT_80616ed0);
                    } else {
                        anim_play_scaled((double)FLOAT_80616ed8, (double)FLOAT_80616ed0);
                        seg[8] = (float)((int)seg[8] + 1);
                    }
                } else {
                    anim_play_scaled((double)FLOAT_80616ed8, (double)FLOAT_80616ed0);
                    seg[8] = (float)((int)seg[8] + 1);
                }
            }
            if (0 < (int)seg[8]) {
                for (i8 = 0; i8 < (int)DAT_806108e4; i8 = i8 + 1) {
                    seg[10] = (float)((double)seg[10] * (double)FLOAT_80616fe8);
                }
            }
            if (1 < (int)seg[8]) { seg[9] = 2.10195e-44f; }
        }
        // ---- state 13 (1.82169e-44): submerge / decay speed ----
        else if (f6 == 1.82169e-44f) {
            i8 = anim_is_clip_active(ownerEnt, 0x19a);
            if (i8 == 0) { anim_play_scaled((double)FLOAT_80616ed8, (double)FLOAT_80616ed0); }
            for (i8 = 0; i8 < (int)DAT_806108e4; i8 = i8 + 1) {
                seg[10] = seg[10] * FLOAT_80616fec;
            }
            if ((double)seg[10] < (double)FLOAT_80616f98) {
                DAT_80612634 = DAT_80612634 + -1;
                seg[9]  = 8.40779e-45f;
                seg[10] = FLOAT_80616f9c;
                i9 = 0x40; i24 = 0;
                do {
                    *(float *)((int)(size_t)seg + i24 + 0x3c) = seg[0];
                    *(float *)((int)(size_t)seg + i24 + 0x40) = seg[1];
                    *(float *)((int)(size_t)seg + i24 + 0x44) = seg[2];
                    *(float *)((int)(size_t)seg + i24 + 0x40) =
                        *(float *)((int)(size_t)seg + i24 + 0x40) + seg[10];
                    i9 = i9 + -1; i24 = i24 + 0xc;
                } while (i9 != 0);
            }
        }
        (void)i15; (void)f1; (void)local_8fc;
    }
}

// forward decls for batch_593 (called before their definitions below)
void worm_splash_emit_at(float *pos);
void worm_splash_emit_oneshot(double intensity, int spawnCtx, float *pos, char flags);

// ===========================================================================
// batch_593 (0x801cacc0 - 0x801cc6dc): worm surfacing burst particle system.
// The set-piece splash/spray/dust effect that plays as the worm breaks the
// surface: a per-frame water-jet update that both animates a cached jet
// emitter and sweeps nearby entities for contact damage, the emitter spawn
// callbacks that position/colour each particle along the jet arc, a builder
// that registers the six particle defs + emitters (DAT_805c3548..805c3ca4),
// and the one-shot burst that fires all six emitters with gravity.
//
// Non-matching notes carry over from above: the wide FP-context spill frames
// (FUN_803826e8/803826b4/803826f0 open, FUN_80382734/80382700/8038273c close)
// are dead ABI plumbing kept as opaque calls; several emitter callbacks
// received their scratch pointer / segment index through caller-saved
// registers (extraout_r4 / in_r6) which the decompiler surfaced -- here they
// are explicit parameters. The int<->double magic-bias idiom is reduced to a
// plain (double)(int) cast. particle_def_create / particle_emitter_create /
// damage_dispatch_to_entity are called through file-local typed aliases
// because their generated_decls prototypes pack the leading FP args
// differently from these paired-single call sites.
extern "C" {
// spill-frame open/close variants used by this batch
extern int  FUN_803826e8(void);
extern int  FUN_803826b4(void);
extern void FUN_80382734(void);
extern void FUN_80382700(void);
extern int  FUN_8037dc78(int);                         // jet-side selector (hi-half of spill ret)
extern unsigned short FUN_8037daf0(double, ...);        // register a particle def, returns id

// worm burst emitter/def blobs (0x84-ish stride records in .bss)
extern int DAT_805c3548; extern int DAT_805c3614;
extern int DAT_805c3698; extern int DAT_805c3764;
extern int DAT_805c37e8; extern int DAT_805c38b4;
extern int DAT_805c3938; extern int DAT_805c3a04;
extern int DAT_805c3a88; extern int DAT_805c3b54;
extern int DAT_805c3bd8; extern int DAT_805c3ca4;

// burst emitter id cache slots
extern int DAT_80612668; extern int DAT_80612664; extern int DAT_80612660;
extern int DAT_8061265c; extern int DAT_80612658;

// spray colour-ramp / velocity-scatter tables (0x1c stride)
extern int DAT_80450138; extern int DAT_8045013c; extern int DAT_80450140;
extern int DAT_80450144; extern int DAT_80450148; extern int DAT_8045014c;
extern int DAT_80450150;

// nearby-entity sweep lists (shared with particle/entity system)
extern int DAT_8053fb7c;   // entity ptr table base
extern int DAT_8054022c;   // entity count
extern int DAT_80611928;   // secondary frame-time numerator

// ROM const vectors used by the burst builder / callbacks
extern int DAT_803df1f4; extern int DAT_803df1f8; extern int DAT_803df1fc;
extern int DAT_803df200; extern int DAT_803df204; extern int DAT_803df208;
extern int DAT_803df20c; extern int DAT_803df210; extern int DAT_803df214;
extern int DAT_803df218; extern int DAT_803df21c; extern int DAT_803df220;
extern int DAT_803df234; extern int DAT_803df248; extern int DAT_803df25c;
extern int DAT_803df274; extern int DAT_803df278; extern int DAT_803df27c;
extern int DAT_803df280; extern int DAT_803df284; extern int DAT_803df288;
extern int DAT_803df28c; extern int DAT_803df290; extern int DAT_803df2a4;
extern int DAT_803df2bc; extern int DAT_803df2c0; extern int DAT_803df2c4;
extern int DAT_803df2c8; extern int DAT_803df2cc; extern int DAT_803df2d0;
extern int DAT_803df2d4; extern int DAT_803df2d8; extern int DAT_803df2dc;
extern int DAT_803df2e0; extern int DAT_803df2e4; extern int DAT_803df2e8;
extern int DAT_803df2ec; extern int DAT_803df2f0; extern int DAT_803df2f4;
extern int DAT_803df2f8; extern int DAT_803df2fc; extern int DAT_803df300;
extern int DAT_803df304; extern int DAT_803df308; extern int DAT_803df30c;
extern int DAT_803df310; extern int DAT_803df314; extern int DAT_803df318;
extern int DAT_803df31c; extern int DAT_803df320; extern int DAT_803df324;
extern int DAT_803df328; extern int DAT_803df32c; extern int DAT_803df330;
extern int DAT_803df334; extern int DAT_803df338; extern int DAT_803df33c;
extern int DAT_803df340; extern int DAT_803df344; extern int DAT_803df348;
extern int DAT_803df34c; extern int DAT_803df350; extern int DAT_803df354;
extern int DAT_803df358; extern int DAT_803df35c; extern int DAT_803df360;
extern int DAT_803df364; extern int DAT_803df368; extern int DAT_803df36c;
extern int DAT_803df370; extern int DAT_803df374; extern int DAT_803df378;
extern int DAT_803df37c; extern int DAT_803df380; extern int DAT_803df384;
extern int DAT_803df388; extern int DAT_803df38c; extern int DAT_803df390;
extern int DAT_803df394;

// float/double constant pool additions for the burst system
extern float FLOAT_80616f6c; extern float FLOAT_80616f9c; extern float FLOAT_80616fd0;
extern float FLOAT_80616fec; extern float FLOAT_80616ee4;
extern float FLOAT_80617004; extern float FLOAT_80617008; extern float FLOAT_8061700c;
extern float FLOAT_80617018; extern float FLOAT_8061701c; extern float FLOAT_80617020;
extern float FLOAT_80617024; extern float FLOAT_80617028; extern float FLOAT_8061702c;
extern float FLOAT_80617030; extern float FLOAT_80617034; extern float FLOAT_80617038;
extern float FLOAT_8061703c; extern float FLOAT_80617040; extern float FLOAT_80617044;
extern float FLOAT_80617048; extern float FLOAT_8061704c; extern float FLOAT_80617050;
extern float FLOAT_80617054; extern float FLOAT_80617058; extern float FLOAT_8061705c;
extern float FLOAT_80617060; extern float FLOAT_80617064; extern float FLOAT_80617068;
extern float FLOAT_8061706c;
extern float FLOAT_80616f20; extern float FLOAT_80616f2c; extern float FLOAT_80616efc;
extern float FLOAT_80610d24;   // spray ramp scale
extern char  DAT_805015bd;     // interior-region flag byte
extern double DOUBLE_80617010; // int->float bias base for rng normalize
}

// ---- typed aliases: paired-single / variadic call sites that differ from the
//      shared-header prototypes (function-pointer casts, no redeclaration) ----
static void (*const __wr_pdef_create)(double, double, void *, int, int, int, int, int)
    = (void (*)(double, double, void *, int, int, int, int, int)) &particle_def_create;
static void (*const __wr_pemit_create)(double, double, double, void *, void *, int, int)
    = (void (*)(double, double, double, void *, void *, int, int)) &particle_emitter_create;
static void (*const __wr_damage_dispatch)(unsigned long long, double, double, double, double,
                                          double, double, double, int *)
    = (void (*)(unsigned long long, double, double, double, double, double, double, double, int *))
      &damage_dispatch_to_entity;

// 0x801cacc0 - one-shot: spawn a single screen-space splash sprite at the worm
// break point. Seeds a small colour triple from the const pool then calls the
// generic attached-effect spawner (mode 3) scaled by the passed intensity.
void worm_splash_emit_oneshot(double intensity, int spawnCtx, float *pos, char flags)
{
    float colR = *(float *)&DAT_803df1f4;
    int   colG = DAT_803df1f8;
    int   colB = DAT_803df1fc;
    FUN_8037dc94((double)(float)((double)FLOAT_80616f34 * intensity), intensity, intensity,
                 spawnCtx, 3, pos, &colR, (int *)0, 1, flags, 0);
    (void)colG; (void)colB;
}

// 0x801cad2c - begin-submerge trigger (pointed at from a worm control table):
// if the worm is in emerge phase (word 0x20 == 1) advance it to the submerge
// phase (== 2).
void worm_begin_submerge(void)
{
    if (*(int *)(DAT_8061266c + 0x20) != 1) {
        return;
    }
    *(int *)(DAT_8061266c + 0x20) = 2;
}

// 0x801cad48 - per-frame water-jet update. Drives the jet height word +0x24 up
// (phase 1) or down (phase 2) by a randomised rate, spawns splash droplets
// along the jet ray while rising, retunes the five cached spray emitters'
// drag/size by phase and by which portal/zone the worm is in, then sweeps every
// active entity within the jet radius and dispatches a knock/damage event.
void worm_water_jet_update(void)
{
    float jetTop = FLOAT_80616ed8;
    float jetCap = FLOAT_80616ed0;
    int   ctl = DAT_8061266c;
    int   phase = *(int *)(ctl + 0x20);
    int   i, off;
    unsigned int rnd;
    double dx, dy, dz;

    if (phase == 1) {
        // rise: dh = (activeDen / frameDen) * cap, clamp to cap
        float dh = (float)((double)(int)DAT_806108e4 /
                           (double)(int)DAT_806109c0) * FLOAT_80616ed0;
        *(float *)(ctl + 0x24) = dh + *(float *)(ctl + 0x24);
        if (jetCap < *(float *)(ctl + 0x24)) {
            *(float *)(ctl + 0x24) = jetCap;
        }
        dx = (double)(*(float *)(ctl + 0x14) - *(float *)(ctl + 8));
        dy = (double)(*(float *)(ctl + 0x18) - *(float *)(ctl + 0xc));
        dz = (double)(*(float *)(ctl + 0x1c) - *(float *)(ctl + 0x10));
        rnd = __wr_rng_tick();
        if (rnd - (rnd / DAT_806109c0) * DAT_806109c0 < (unsigned int)DAT_806108e4) {
            unsigned int r2 = __wr_rng_tick();
            double t = (double)(((float)((double)(int)r2 - DOUBLE_80617010) / FLOAT_80617004) *
                                *(float *)(ctl + 0x24));
            float pos[3];
            pos[0] = (float)(dx * t + (double)*(float *)(ctl + 8));
            pos[1] = (float)(dy * t + (double)*(float *)(ctl + 0xc));
            pos[2] = (float)(dz * t + (double)*(float *)(ctl + 0x10));
            worm_splash_emit_at(pos);
        }
    }
    else if (phase == 2) {
        // fall: dh negative, when it crosses the top retune emitters, else tear down
        float dh = -((float)((double)(int)DAT_806108e4 /
                             (double)(int)DAT_806109c0) * FLOAT_80617008 -
                     *(float *)(ctl + 0x24));
        *(float *)(ctl + 0x24) = dh;
        if (jetTop <= *(float *)(ctl + 0x24)) {
            i = 0; off = 0;
            do {
                if ((i != 0) && (i != 3)) {
                    int h = *(int *)(ctl + off + 0x28);
                    particle_cached_set_size_range((double)*(float *)(ctl + 0x24),
                                                   (double)*(float *)(ctl + 0x24),
                                                   (ParticleEmitter **)&h);
                }
                if ((i == 0) || (i == 3) || (i == 1)) {
                    int h = *(int *)(ctl + off + 0x28);
                    particle_cached_set_drag((double)FLOAT_80616ed8, (ParticleEmitter **)&h);
                }
                i = i + 1; off = off + 4;
            } while (i < 5);
        } else {
            i = 0; off = 0;
            do {
                int h = *(int *)(ctl + off + 0x28);
                particle_handle_destroy(&h);
                i = i + 1; off = off + 4;
            } while (i < 5);
            *(float *)(ctl + 0x24) = FLOAT_80616ed8;
            *(int *)(ctl + 0x20) = 0;
        }
    }

    // per-portal / per-zone drag retune while rising
    if ((*(int *)(DAT_80611d78 + 0xdec) != 0x18) ||
        (((DAT_805015bd == '\n' && (1 < DAT_80612610)) &&
          (*(int *)(DAT_80611d78 + 0x2208) != 0x18)) ||
         (scene_portal_is_open(0x23)))) {
        if (*(int *)(ctl + 0x20) == 1) {
            i = 0; off = 0;
            do {
                int h = *(int *)(ctl + off + 0x28);
                particle_cached_set_drag((double)FLOAT_80616f34, (ParticleEmitter **)&h);
                i = i + 1; off = off + 4;
            } while (i < 5);
        }
    } else if (scene_portal_is_open(0x19)) {
        i = 0; off = 0;
        do {
            int h = *(int *)(ctl + off + 0x28);
            particle_cached_set_drag((double)FLOAT_8061700c, (ParticleEmitter **)&h);
            i = i + 1; off = off + 4;
        } while (i < 5);
    } else {
        i = 0; off = 0;
        do {
            int h = *(int *)(ctl + off + 0x28);
            particle_cached_set_drag((double)FLOAT_80616f98, (ParticleEmitter **)&h);
            i = i + 1; off = off + 4;
        } while (i < 5);
    }

    // contact damage sweep: for each active entity near the jet ray, dispatch a hit
    phase = *(int *)(ctl + 0x20);
    if ((phase != 0) &&
        ((phase != 2) || (FLOAT_80616ee0 <= *(float *)(ctl + 0x24)))) {
        double ry = (double)(*(float *)(ctl + 0x18) - *(float *)(ctl + 0xc));
        double rx = (double)(*(float *)(ctl + 0x14) - *(float *)(ctl + 8));
        double rz = (double)(*(float *)(ctl + 0x1c) - *(float *)(ctl + 0x10));
        double rlen2 = (double)(float)(rz * rz + (double)(float)(rx * rx + (double)(float)(ry * ry)));
        int eoff = 0;
        for (i = 0; i < DAT_8054022c; i = i + 1) {
            int ent = *(int *)(DAT_8053fb7c + eoff);
            if ((*(unsigned int *)(ent + 0x28) & 0x80000000) == 0) {
                float ex = *(float *)(ent + 0x68) + *(float *)(ent + 0x1ac);
                double ey = (double)(*(float *)(ent + 100) + *(float *)(ent + 0x1a8));
                double ez = (double)(*(float *)(ent + 0x6c) + *(float *)(ent + 0x1b0));
                double proj = (double)(float)(-(double)(float)
                              (rz * (double)(float)((double)*(float *)(ctl + 0x10) - ez) +
                               (double)(float)(rx * (double)(float)((double)*(float *)(ctl + 8) - ey) +
                                (double)(float)(ry * (double)(*(float *)(ctl + 0xc) - ex)))) / rlen2);
                double projClamped = proj;
                if ((double)FLOAT_80616ed0 < proj) projClamped = (double)FLOAT_80616ed0;
                if ((double)FLOAT_80616ed8 <= projClamped) {
                    if ((double)FLOAT_80616ed0 < proj) proj = (double)FLOAT_80616ed0;
                } else {
                    proj = (double)FLOAT_80616ed8;
                }
                float cx = (float)(rx * proj + (double)*(float *)(ctl + 8));
                double capR = (double)FLOAT_80616fa4;
                float dxp = (float)((double)cx - ey);
                float cy = (float)(rz * proj + (double)*(float *)(ctl + 0xc));
                float cz = (float)(ry * proj + (double)*(float *)(ctl + 0x10));
                double czd = (double)cz;
                double dzp2 = (double)((float)(czd - ez) * (float)(czd - ez));
                double er2 = (double)(*(float *)(ent + 0x1c0) * *(float *)(ent + 0x1c0));
                double d2 = (double)(float)(dzp2 + (double)(dxp * dxp + (cy - ex) * (cy - ex)));
                if (d2 < (double)(float)(capR + (double)(float)(er2 +
                        (double)(*(float *)(ent + 0x1c8) * *(float *)(ent + 0x1c8))))) {
                    float nrm[3];
                    nrm[0] = (float)(ey - (double)cx);
                    nrm[1] = ex - cy;
                    nrm[2] = (float)(ez - czd);
                    double dist = math_sqrt(d2);
                    double inv = (double)(float)((double)FLOAT_80616ed0 / dist);
                    nrm[0] = (float)((double)nrm[0] * inv);
                    nrm[1] = (float)((double)nrm[1] * inv);
                    nrm[2] = (float)((double)nrm[2] * inv);
                    if (*(int *)(ent + 0x20) == 8) {
                        int *owner = *(int **)(ent + 0x7c);
                        if (((owner == (int *)0) || (DAT_80612610 <= owner[2])) &&
                            (*(int *)(*owner + 0x60) != 0x524f434b)) {   // 'ROCK'
                            character_damage_spawn_effects();
                        } else {
                            int ev[19];
                            unsigned long long ctx = __wr_switch_reset((int)ev);
                            float hitPos[3] = { cx, cy, cz };
                            ev[0] = ent;
                            *(int *)((char *)ev + 0x2c) = 0x15;   // local_b0
                            *(int *)((char *)ev + 0x30) = 0x15;   // local_ac
                            *(float *)((char *)ev + 0x38) = FLOAT_80616f04; // local_a4
                            *(float **)((char *)ev + 0x40) = hitPos;        // local_9c
                            *(float **)((char *)ev + 0x48) = nrm;           // local_94
                            __wr_damage_dispatch(ctx, inv, inv, inv, er2, capR, dzp2, czd, ev);
                        }
                    } else {
                        int ev[19];
                        unsigned long long ctx = __wr_switch_reset((int)ev);
                        float hitPos[3] = { cx, cy, cz };
                        ev[0] = ent;
                        *(int *)((char *)ev + 0x2c) = 0x15;
                        *(int *)((char *)ev + 0x30) = 0x15;
                        *(float *)((char *)ev + 0x38) = FLOAT_80616f04;
                        *(float **)((char *)ev + 0x40) = hitPos;
                        *(float **)((char *)ev + 0x48) = nrm;
                        __wr_damage_dispatch(ctx, inv, inv, inv, er2, capR, dzp2, czd, ev);
                    }
                }
            }
            eoff = eoff + 4;
        }
    }

    // 3d loop sound follows the jet height
    {
        float snd[3];
        snd[0] = *(float *)&DAT_803df200;
        *(int *)&snd[1] = DAT_803df204;
        *(int *)&snd[2] = DAT_803df208;
        __wr_sound3d((double)(FLOAT_80616ef0 * *(float *)(ctl + 0x24)),
                     (double)FLOAT_80616f24, (double)FLOAT_80616ed0,
                     ctl + 4, (int)(size_t)snd, 0xbfb, 0);
    }
}

// 0x801cb400 - jet-arc emitter callback: position a droplet along the jet ray
// at a random fraction (scaled by jet height in phase 1), then bend its y by a
// sine wobble (or a fixed offset on the far jet side), and lift it by the
// current jet height in submerge phase.
void worm_jet_arc_position_cb(int spillHi, float *outPos, int ctxAt4)
{
    int  ctl = DAT_8061266c;
    unsigned int rnd = __wr_rng_tick();
    float t;
    if (*(int *)(ctl + 0x20) == 1) {
        t = *(float *)(ctl + 0x24) *
            ((float)((double)(int)rnd - DOUBLE_80617010) / FLOAT_80617004);
    } else {
        t = (float)((double)(int)rnd - DOUBLE_80617010) / FLOAT_80617004;
    }
    double td = (double)t;
    outPos[0] = (float)((double)(float)((double)*(float *)(ctl + 0x14) -
                                        (double)*(float *)(ctl + 8)) * td +
                        (double)*(float *)(ctl + 8));
    outPos[1] = (float)((double)(*(float *)(ctl + 0x18) - *(float *)(ctl + 0xc)) * td +
                        (double)*(float *)(ctl + 0xc));
    outPos[2] = (float)((double)(*(float *)(ctl + 0x1c) - *(float *)(ctl + 0x10)) * td +
                        (double)*(float *)(ctl + 0x10));
    double phaseBias = (double)((float)((double)(int)DAT_80611928 - DOUBLE_80616f48) *
                                (FLOAT_80616ed0 /
                                 (float)((double)(int)DAT_806109c0 - DOUBLE_80616f48)));
    if (FUN_8037dc78(spillHi) == 0) {
        double s = math_poly_trig((double)((float)((double)FLOAT_80616f20 * td + phaseBias) -
                                           FLOAT_80616f28));
        outPos[1] = -(FLOAT_80616ee0 * (float)((double)FLOAT_80616ed0 + s) * FLOAT_80616ee0 -
                      *(float *)(ctxAt4 + 4));
    } else {
        outPos[1] = FLOAT_80617018 + *(float *)(ctxAt4 + 4);
    }
    if (*(int *)(ctl + 0x20) == 2) {
        outPos[1] = outPos[1] - (FLOAT_80616ed0 - *(float *)(ctl + 0x24));
    }
}

// 0x801cb5d0 - spray colour-ramp emitter callback: walk the piecewise colour
// ramp table (DAT_80450138, 0x1c stride) by a random parameter to interpolate
// the particle colour, then add a time+height driven sine wobble to the green
// channel from the sin/cos LUT.
void worm_spray_color_ramp_cb(int spillHi, float *outCol)
{
    unsigned int rnd = __wr_rng_tick();
    int seg = 0;
    double whole = (double)(FLOAT_80610d24 *
                            ((float)((double)(int)rnd - DOUBLE_80617010) / FLOAT_80617004));
    double frac = whole;
    while (frac > (double)*(float *)((int)&DAT_80450150 + seg)) {
        frac = (double)(float)(frac - (double)*(float *)((int)&DAT_80450150 + seg));
        seg = seg + 0x1c;
    }
    float span = (float)(frac / (double)*(float *)((int)&DAT_80450150 + seg));
    outCol[0] = *(float *)((int)&DAT_80450144 + seg) * span + *(float *)((int)&DAT_80450138 + seg);
    outCol[1] = *(float *)((int)&DAT_80450148 + seg) * span + *(float *)((int)&DAT_8045013c + seg);
    outCol[2] = *(float *)((int)&DAT_8045014c + seg) * span + *(float *)((int)&DAT_80450140 + seg);
    double frames = game_time_to_frames();
    unsigned int lut = (unsigned int)(int)(FLOAT_80616f64 *
        (float)((double)FLOAT_80616fa4 * whole +
                (double)((float)((double)(FLOAT_80616ed0 /
                                 (float)((double)(int)DAT_806109c0 - DOUBLE_80616f48)) * frames) *
                         FLOAT_80616f38)) + FLOAT_80616ee0);
    outCol[1] = FLOAT_80616f04 *
                *(float *)((int)&DAT_80583228 + ((lut + 0x200) * 4 & 0x1ffc)) + outCol[1];
    (void)spillHi;
}

// 0x801cb730 - spray-scatter emitter callback: pick a random x/z offset in a
// square (quadratic falloff) and set y from a parabola of the radial distance
// (or a flat offset on the far jet side).
void worm_spray_scatter_cb(int spillHi, float *outPos, int ctxAt4)
{
    unsigned int rnd;
    double a;
    rnd = __wr_rng_tick();
    a = (double)((float)((double)(int)rnd - DOUBLE_80617010) / FLOAT_80617004 - FLOAT_80616ee0);
    rnd = __wr_rng_tick();
    outPos[0] = FLOAT_80616fd8 *
                (float)((double)((float)((double)(int)rnd - DOUBLE_80617010) / FLOAT_80617004 -
                                 FLOAT_80616ee0) * a);
    rnd = __wr_rng_tick();
    a = (double)((float)((double)(int)rnd - DOUBLE_80617010) / FLOAT_80617004 - FLOAT_80616ee0);
    rnd = __wr_rng_tick();
    outPos[2] = FLOAT_80616fd8 *
                (float)((double)((float)((double)(int)rnd - DOUBLE_80617010) / FLOAT_80617004 -
                                 FLOAT_80616ee0) * a) + FLOAT_80616fb8;
    if (FUN_8037dc78(spillHi) == 0) {
        outPos[1] = -((outPos[0] * outPos[0] +
                       (outPos[2] - FLOAT_80616fb8) * (outPos[2] - FLOAT_80616fb8)) *
                      FLOAT_80616f38 - *(float *)(ctxAt4 + 4));
    } else {
        outPos[1] = *(float *)(ctxAt4 + 4);
    }
}

// 0x801cb894 - dust emitter callback: spawn a mote on a random ring of a random
// worm body segment. Reads the segment entity from DAT_805c3518[segIndex],
// picks one of 9 ring rows, rotates the ring edge by the segment's yaw, and
// interpolates the mote position across the ring by a random fraction.
void worm_dust_spawn_on_segment_cb(int segIndex, float *outPos)
{
    unsigned int rnd;
    int segEnt, ringBase, edgeAt;
    double t;
    float mtx[16];
    float edge[3];
    float base[3];

    FUN_803826f0();
    segEnt = (&DAT_805c3518)[segIndex];
    rnd = __wr_rng_tick();
    t = (double)((float)((double)(int)rnd - DOUBLE_80617010) / FLOAT_80617004);
    rnd = __wr_rng_tick();
    ringBase = (rnd % 9 + 1) * 0x40;
    edgeAt = *(int *)(*(int *)(segEnt + 4) + 0x10) + ringBase;
    edge[0] = *(float *)(edgeAt + 0x70) - *(float *)(edgeAt + 0x30);
    edge[1] = *(float *)(edgeAt + 0x74) - *(float *)(edgeAt + 0x34);
    edge[2] = *(float *)(edgeAt + 0x78) - *(float *)(edgeAt + 0x38);
    mtx4_set_rotation_y((double)((FLOAT_80616f30 * *(float *)(segEnt + 0x70)) / FLOAT_80616f68), mtx);
    mtx4_transform_vector(mtx, edge);
    ringBase = *(int *)(*(int *)(segEnt + 4) + 0x10) + ringBase;
    base[0] = *(float *)(ringBase + 0x30);
    base[1] = *(float *)(ringBase + 0x34);
    base[2] = *(float *)(ringBase + 0x38);
    mtx4_transform_vector(mtx, base);
    base[0] = base[0] + *(float *)(segEnt + 100);
    base[1] = base[1] + *(float *)(segEnt + 0x68);
    base[2] = base[2] + *(float *)(segEnt + 0x6c);
    outPos[0] = (float)((double)edge[0] * t + (double)base[0]);
    outPos[1] = (float)((double)edge[1] * t + (double)base[1]);
    outPos[2] = (float)((double)edge[2] * t + (double)base[2]);
    FUN_8038273c();
}

// 0x801cba14 - build the six worm-burst particle defs and their emitters. Sets
// the jet ray endpoints from the const pool, registers four raw defs via
// FUN_8037daf0, then defines and creates six particle systems (splash core,
// spray, and four dust/spume variants) with size/colour/velocity keyframes.
void worm_particle_defs_build(void)
{
    int  ctl;
    float rayEndX, rayEndY, rayEndZ;
    unsigned short id;
    int scratch[36];   // local_90
    int kf[8];

    FUN_803826e8();
    ctl = DAT_8061266c;
    *(int *)(ctl + 8)    = DAT_803df20c;
    *(int *)(ctl + 0xc)  = DAT_803df210;
    *(int *)(ctl + 0x10) = DAT_803df214;
    *(int *)(ctl + 0x14) = DAT_803df218;
    *(float *)(ctl + 0x18) = *(float *)&DAT_803df21c;
    *(int *)(ctl + 0x1c) = DAT_803df220;
    rayEndX = *(float *)&DAT_803df21c;
    rayEndY = *(float *)&DAT_803df218; rayEndZ = *(float *)&DAT_803df214;

    id = FUN_8037daf0((double)rayEndX);  DAT_80612668 = (int)id; DAT_80612664 = 0;
    id = FUN_8037daf0((double)rayEndX);  DAT_80612660 = (int)id;
    id = FUN_8037daf0((double)rayEndX);  DAT_8061265c = (int)id;
    id = FUN_8037daf0((double)rayEndX);  DAT_80612658 = (int)id;

    // splash core
    kf[0] = DAT_803df220; kf[1] = DAT_803df234;
    __wr_pdef_create((double)FLOAT_80616f6c, (double)FLOAT_80616ed0, &DAT_805c3548, 0, 0, 1, 0x300, 0);
    particle_def_set_size_keyframes((double)FLOAT_80616ed0, (double)FLOAT_80616ed8,
                                    (double)FLOAT_8061701c, (double)FLOAT_8061701c,
                                    0xe0e0e00, 0xe0e0e00, 0x1b1b1b00);
    particle_def_set_keyframes_raw((int *)(scratch + 0x14), (int *)(scratch + 0x19), 2);
    particle_def_set_size_start_end((double)FLOAT_80616ed8, (double)FLOAT_80617020,
                                    (double)FLOAT_80616ef0, (double)FLOAT_80616ed0);
    particle_def_set_color_end((double)FLOAT_80616f34, (double)FLOAT_80616ed8,
                               (double)FLOAT_80617024, (double)FLOAT_80617028);
    particle_def_set_velocity_ranges((double)FLOAT_80616fb4, (double)FLOAT_80616ed8,
                                     (double)FLOAT_8061702c, (double)FLOAT_80616ee0,
                                     (double)FLOAT_80616ed0, (double)FLOAT_80617030);
    __wr_pemit_create((double)FLOAT_80616f34, (double)FLOAT_80616ed8, (double)FLOAT_80617034,
                      &DAT_805c3614, &DAT_805c3548, 0, 1);
    particle_emitter_set_color_range((double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                                     (double)FLOAT_80616f34, (double)FLOAT_80616ed8);

    // spray
    __wr_pdef_create((double)FLOAT_80617038, (double)FLOAT_80616ed0, &DAT_805c3698, 2, 1, 1, 0x301, 0);
    particle_def_set_size_keyframes((double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                                    (double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                                    0x65656500, 0x65656500, 0);
    particle_def_set_keyframes_raw((int *)(scratch + 10), (int *)(scratch + 0xf), 5);
    particle_def_set_size_start_end((double)FLOAT_80616f98, (double)FLOAT_80616ed8,
                                    (double)FLOAT_80616eec, (double)FLOAT_80616ed0);
    particle_def_set_color_end((double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                               (double)FLOAT_80616ef4, (double)FLOAT_8061703c);
    particle_def_set_velocity_ranges((double)FLOAT_80616f3c, (double)FLOAT_80616ed8,
                                     (double)FLOAT_80616ee0, (double)FLOAT_80616f0c,
                                     (double)FLOAT_80617040, (double)FLOAT_80617030);
    __wr_pemit_create((double)FLOAT_80616fa8, (double)FLOAT_80616efc, (double)FLOAT_80616f0c,
                      &DAT_805c3764, &DAT_805c3698, 0, 1);
    particle_emitter_set_color_range((double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                                     (double)FLOAT_80616f24, (double)FLOAT_80616ed8);

    // dust A
    kf[2] = DAT_803df284; kf[3] = DAT_803df288; kf[4] = DAT_803df28c; kf[5] = DAT_803df290;
    kf[6] = DAT_803df274; kf[7] = DAT_803df278;
    __wr_pdef_create((double)FLOAT_80616ee0, (double)FLOAT_80616ed0, &DAT_805c37e8, 0, 1, 1, 0x302, 0);
    particle_def_set_size_keyframes((double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                                    (double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                                    0x65656500, 0x65656500, 0);
    particle_def_set_keyframes_raw(&kf[2], &kf[6], 4);
    particle_def_set_size_start_end((double)FLOAT_80616ed8, (double)FLOAT_80617020,
                                    (double)FLOAT_80616ed0, (double)FLOAT_80616f2c);
    particle_def_set_color_end((double)FLOAT_8061700c, (double)FLOAT_80617044,
                               (double)FLOAT_80617048, (double)FLOAT_80616ed8);
    particle_def_set_velocity_ranges((double)FLOAT_80616ee0, (double)FLOAT_80616ed8,
                                     (double)FLOAT_80616ef0, (double)FLOAT_80616ed0,
                                     (double)FLOAT_80616ed0, (double)FLOAT_80617030);
    __wr_pemit_create((double)FLOAT_8061704c, (double)FLOAT_80616ed0, (double)FLOAT_80616fd0,
                      &DAT_805c38b4, &DAT_805c37e8, 0, 1);
    particle_emitter_set_color_range((double)FLOAT_80616ed0, (double)FLOAT_80616ed8,
                                     (double)FLOAT_80617050, (double)FLOAT_80616eec);
    particle_emitter_set_lifetime_range((double)FLOAT_80616ed8, (double)FLOAT_80616ed8);

    // dust B
    __wr_pdef_create((double)FLOAT_80616ee0, (double)FLOAT_80616ed0, &DAT_805c3938, 0, 1, 1, 0x303, 0);
    particle_def_set_size_keyframes((double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                                    (double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                                    0x65656500, 0x65656500, 0);
    particle_def_set_keyframes_raw((int *)scratch, (int *)(scratch + 5), 5);
    particle_def_set_size_start_end((double)FLOAT_80616ed8, (double)FLOAT_80617020,
                                    (double)FLOAT_80616ed0, (double)FLOAT_80616f2c);
    particle_def_set_color_end((double)FLOAT_80617054, (double)FLOAT_80616ed8,
                               (double)FLOAT_80617058, (double)FLOAT_80616ed8);
    particle_def_set_velocity_ranges((double)FLOAT_80616ee0, (double)FLOAT_80616ed8,
                                     (double)FLOAT_80616eec, (double)FLOAT_80616ed0,
                                     (double)FLOAT_80616ed0, (double)FLOAT_80617030);
    __wr_pemit_create((double)FLOAT_8061704c, (double)FLOAT_80616ed0, (double)FLOAT_8061705c,
                      &DAT_805c3a04, &DAT_805c3938, 0, 1);
    particle_emitter_set_color_range((double)FLOAT_80616ed0, (double)FLOAT_80616ed8,
                                     (double)FLOAT_80617050, (double)FLOAT_80616eec);

    // spume A
    __wr_pdef_create((double)FLOAT_80617060, (double)FLOAT_80616ed0, &DAT_805c3a88, 0, 1, 1, 0x302, 0);
    particle_def_set_size_keyframes((double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                                    (double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                                    0x65656500, 0x65656500, 0);
    particle_def_set_keyframes_raw((int *)(scratch), (int *)(scratch), 4);
    particle_def_set_size_start_end((double)FLOAT_80616ed8, (double)FLOAT_80617020,
                                    (double)FLOAT_80616eec, (double)FLOAT_80616ed0);
    particle_def_set_color_end((double)FLOAT_80616f34, (double)FLOAT_80616ed8,
                               (double)FLOAT_80617064, (double)FLOAT_80617028);
    particle_def_set_velocity_ranges((double)FLOAT_80616fb4, (double)FLOAT_80616ed8,
                                     (double)FLOAT_80616f04, (double)FLOAT_80616f0c,
                                     (double)FLOAT_80616ed0, (double)FLOAT_80617030);
    __wr_pemit_create((double)FLOAT_80616f34, (double)FLOAT_80616ed8, (double)FLOAT_80616f38,
                      &DAT_805c3b54, &DAT_805c3a88, 0, 1);
    particle_emitter_set_color_range((double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                                     (double)FLOAT_80616f34, (double)FLOAT_80616ed8);

    // spume B
    __wr_pdef_create((double)FLOAT_80617068, (double)FLOAT_80616ed0, &DAT_805c3bd8, 0, 1, 1, 0x303, 0);
    particle_def_set_size_keyframes((double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                                    (double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                                    0x65656500, 0x65656500, 0);
    particle_def_set_keyframes_raw((int *)(scratch), (int *)(scratch), 4);
    particle_def_set_size_start_end((double)FLOAT_80616ed8, (double)FLOAT_80617020,
                                    (double)FLOAT_80616eec, (double)FLOAT_80616ed0);
    particle_def_set_color_end((double)FLOAT_80616f34, (double)FLOAT_80616ed8,
                               (double)FLOAT_80617064, (double)FLOAT_80617028);
    particle_def_set_velocity_ranges((double)FLOAT_80616fb4, (double)FLOAT_80616ed8,
                                     (double)FLOAT_80616f04, (double)FLOAT_80616f0c,
                                     (double)FLOAT_80616ed0, (double)FLOAT_80617030);
    __wr_pemit_create((double)FLOAT_80616f34, (double)FLOAT_80616ed8, (double)FLOAT_80616f38,
                      &DAT_805c3ca4, &DAT_805c3bd8, 0, 1);
    particle_emitter_set_color_range((double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                                     (double)FLOAT_80616f34, (double)FLOAT_80616ed8);
    FUN_80382734();
    (void)rayEndZ; (void)ctl; (void)kf;
}

// 0x801cc1d8 - one-shot burst: normalise the six direction vectors from the
// const pool (skipping any with near-zero length) and spawn all six emitters,
// each with the standard gravity applied.
void worm_burst_emit_all(void)
{
    float dir[6][3];
    float pos[6][3];
    int i;

    FUN_803826b4();
    // load the six (pos,dir) pairs straight from the const pool
    pos[0][0] = *(float *)&DAT_803df2fc; *(int *)&pos[0][1] = DAT_803df300; *(int *)&pos[0][2] = DAT_803df304;
    dir[0][0] = *(float *)&DAT_803df308; dir[0][1] = *(float *)&DAT_803df30c; dir[0][2] = *(float *)&DAT_803df310;
    pos[1][0] = *(float *)&DAT_803df314; *(int *)&pos[1][1] = DAT_803df318; *(int *)&pos[1][2] = DAT_803df31c;
    dir[1][0] = *(float *)&DAT_803df320; dir[1][1] = *(float *)&DAT_803df324; dir[1][2] = *(float *)&DAT_803df328;
    pos[2][0] = *(float *)&DAT_803df32c; *(int *)&pos[2][1] = DAT_803df330; *(int *)&pos[2][2] = DAT_803df334;
    dir[2][0] = *(float *)&DAT_803df338; dir[2][1] = *(float *)&DAT_803df33c; dir[2][2] = *(float *)&DAT_803df340;
    pos[3][0] = *(float *)&DAT_803df344; *(int *)&pos[3][1] = DAT_803df348; *(int *)&pos[3][2] = DAT_803df34c;
    dir[3][0] = *(float *)&DAT_803df350; dir[3][1] = *(float *)&DAT_803df354; dir[3][2] = *(float *)&DAT_803df358;
    pos[4][0] = *(float *)&DAT_803df35c; *(int *)&pos[4][1] = DAT_803df360; *(int *)&pos[4][2] = DAT_803df364;
    dir[4][0] = *(float *)&DAT_803df368; dir[4][1] = *(float *)&DAT_803df36c; dir[4][2] = *(float *)&DAT_803df370;
    pos[5][0] = *(float *)&DAT_803df374; *(int *)&pos[5][1] = DAT_803df378; *(int *)&pos[5][2] = DAT_803df37c;
    dir[5][0] = *(float *)&DAT_803df380; dir[5][1] = *(float *)&DAT_803df384; dir[5][2] = *(float *)&DAT_803df388;

    for (i = 0; i < 6; i = i + 1) {
        float len2 = dir[i][2] * dir[i][2] + dir[i][0] * dir[i][0] + dir[i][1] * dir[i][1];
        if (FLOAT_80616fb0 < len2) {
            float inv = 1.0f / SQRT(len2);
            inv = inv * -(FLOAT_80616ee0 * len2 * inv * inv - FLOAT_80616ee4);
            float scale = FLOAT_80616ed0 * inv *
                          -(FLOAT_80616ee0 * len2 * inv * inv - FLOAT_80616ee4);
            dir[i][0] = dir[i][0] * scale;
            dir[i][1] = dir[i][1] * scale;
            dir[i][2] = dir[i][2] * scale;
        }
    }

    static int *const emitBlob[6] = {
        &DAT_805c3614, &DAT_805c3764, &DAT_805c38b4, &DAT_805c3a04, &DAT_805c3b54, &DAT_805c3ca4
    };
    for (i = 0; i < 6; i = i + 1) {
        particle_emitter_spawn(0, (unsigned int)(size_t)emitBlob[i], 0, pos[i], dir[i]);
        particle_emitter_set_gravity((double)FLOAT_80616ee0);
    }
    FUN_80382700();
}

// 0x801cc6dc - spawn one splash droplet at a point: offset the given position
// along the fixed splash direction (DAT_803df38c) and fire the generic
// attached-effect spawner with a random per-droplet size.
void worm_splash_emit_at(float *pos)
{
    float dir[3];
    float out[3];
    unsigned int rnd;
    double sz;

    dir[0] = *(float *)&DAT_803df38c;
    dir[1] = *(float *)&DAT_803df390;
    dir[2] = *(float *)&DAT_803df394;
    rnd = __wr_rng_tick();
    out[0] = FLOAT_8061706c * dir[0] + pos[0];
    sz = (double)(float)((double)FLOAT_80616ed0 +
                         (double)((float)((double)(int)rnd - DOUBLE_80617010) / FLOAT_80617004));
    out[1] = FLOAT_8061706c * dir[1] + pos[1];
    out[2] = FLOAT_8061706c * dir[2] + pos[2];
    FUN_8037dc94((double)FLOAT_80616ed0, sz, sz, 0, 3, out, dir, (int *)0, 0xa80001, 0, 0);
}

// ===========================================================================
// batch_594 (0x801cc7b0 - 0x801cd2b4): worm lifecycle, ambient audio + death.
// The remaining top-level worm module surface: the vertical water-jet
// billboard renderer, the spin-speed ramp, the alloc/reset/level-init entry
// points, the ambient bubble/rumble sound scheduler + its per-frame audio
// tick, the master per-frame update dispatcher, and the death-gib animation
// event that spawns the debris chunks.
//
// Non-matching carries over: PPC __save_gpr_28/__restore_gpr_28 and the wide
// FP-context spill frames are dead ABI plumbing (dropped or kept as opaque
// __sgpr/__rgpr no-ops); the many caller-saved in_fNN/in_psNN_1 spill copies
// are dropped; the int<->double 0x43300000 magic-bias idiom is reduced to a
// plain (double)(int) cast; rng_counter_tick / sound_update_3d_source route
// through the existing __wr_rng_tick / __wr_sound3d aliases; several helpers
// whose generated_decls prototype is narrower than these paired-single/wide
// call sites (mem_alloc, level_actors_register, entity_spawn_by_type,
// subobj_* , worm_update/worm_particle_defs_build wide tails) are called with
// their real argument subset or through file-local typed aliases.
extern "C" {
// --- new float / double constants ---
extern float FLOAT_80617070; extern float FLOAT_80617074; extern float FLOAT_80617078;
extern float FLOAT_8061707c; extern float FLOAT_80617080; extern float FLOAT_80617084;
extern float FLOAT_80617088; extern float FLOAT_8061708c; extern float FLOAT_80617090;
extern float FLOAT_80617094; extern float FLOAT_80617098;
extern float FLOAT_80610d28;   // worm spin-speed accumulator
extern float FLOAT_8061263c;   // worm current angular velocity
extern float FLOAT_806108ec;   // frame-time delta (worm)
extern float FLOAT_80616f1c; extern float FLOAT_80616f84;

// --- new module state ---
extern int DAT_805015c0;   // level feature flags (0x40 = skip-preload bit)
extern int DAT_80612640;   // worm state reset word A
extern int DAT_80612644;   // worm state reset word B
extern int DAT_806124a8;   // worm state reset word C
extern int DAT_80611d74;   // primary player ptr (may be null)
extern int DAT_80612098;   // total entity slot count
extern int DAT_80611d90;   // entity slot array base (0xea0 stride)

// --- ROM const vectors (explosion / debris seed) ---
extern int DAT_803df398; extern int DAT_803df39c; extern int DAT_803df3a0;
extern int DAT_803df3a4; extern int DAT_803df3a8; extern int DAT_803df3ac;

// --- worm level-actor name tables ---
extern int DAT_8044ff3c; extern int DAT_80450074;

// --- raw frontier helpers (typed to their call sites) ---
extern int  FUN_8023c32c(int);            // resolve entity by fourcc tag
extern int  FUN_80244a54(int);            // begin level-actor registration
extern int  FUN_8036e3dc(void);           // effect context accessor
extern void FUN_801cd7d8(void);           // secondary effect init
extern void FUN_801ceaf0(void);           // tertiary effect init
extern void FUN_8036e03c(double, double, double, double, double, double, double, double); // pre-tick hook (wide)
extern void FUN_801ceb18(void);           // per-mode worm hook A
extern void FUN_801cec9c(void);           // per-mode worm hook B
extern int  FUN_80220f30(int);            // payload gate query
extern void FUN_802212b4(int);            // payload fire A
extern void FUN_8022136c(int);            // payload fire B
extern int  FUN_801ceddc(void);           // worm update context accessor
extern void FUN_80365ce0(int, int, int, int, void *); // spawn attached actor effect
extern void FUN_802455c8(void);           // post-tick hook
extern void __save_gpr_28(void);
extern void __restore_gpr_28(void);
}



// ---- typed aliases for narrow-header / wide call-site mismatches ----
static int  (*const __wr_mem_alloc)(int, int) = (int (*)(int, int)) &mem_alloc;
static void (*const __wr_level_actors_register)(int, void *, int)
    = (void (*)(int, void *, int)) &level_actors_register;
static int *(*const __wr_entity_spawn)(int, float *, float *, int, int)
    = (int *(*)(int, float *, float *, int, int)) &entity_spawn_by_type;
static int  (*const __wr_subobj_pool_alloc)(void) = (int (*)(void)) &subobj_pool_alloc;
static void (*const __wr_subobj_build_desc)(unsigned int, int *, int, int, float)
    = (void (*)(unsigned int, int *, int, int, float)) &subobj_build_descriptor_default;
static void (*const __wr_subobj_impulse)(int, float *, float *)
    = (void (*)(int, float *, float *)) &subobj_apply_impulse_at_local_point;

// forward decls (defined later in this batch)
void worm_ambient_sound_schedule(float lastLevel);
void worm_ambient_audio_tick(void);

// 0x801cc7b0 - render the vertical water-jet column as a screen-facing billboard
// quad strip. When active (control word +0x20 != 0) it builds a ray from the
// jet base to tip (shortened in emerge phase 1), derives a screen-perpendicular
// width vector, sets a fading vertex colour by phase, and streams a 4-vertex
// GX strip to the FIFO port with an alternate blend/material state.
void worm_jet_column_render(void)
{
    float f1, f2;
    int tex, savedSub, savedBlend;
    unsigned int savedChans;
    int ctl = DAT_8061266c;
    unsigned int col = 0xffff467f;
    double tipX, tipY, tipZ, rx, ry, rz, widthX, widthZ;

    __save_gpr_28();
    if (*(int *)(ctl + 0x20) != 0) {
        tex = (int)sprite_get_texture_id(0);
        tipX = (double)*(float *)(ctl + 0x14);
        tipY = (double)*(float *)(ctl + 0x18);
        rx = (double)(float)(tipX - (double)*(float *)(ctl + 8));
        tipZ = (double)*(float *)(ctl + 0x1c);
        ry = (double)(float)(tipY - (double)*(float *)(ctl + 0xc));
        rz = (double)(float)(tipZ - (double)*(float *)(ctl + 0x10));
        if (*(int *)(ctl + 0x20) == 1) {
            double t = (double)(FLOAT_80617070 * *(float *)(ctl + 0x24));
            tipX = (double)(float)(rx * t + (double)*(float *)(ctl + 8));
            tipY = (double)(float)(ry * t + (double)*(float *)(ctl + 0xc));
            tipZ = (double)(float)(rz * t + (double)*(float *)(ctl + 0x10));
        }
        f1 = (float)(rz * rz + (double)(float)(rx * rx + (double)(float)(ry * ry)));
        if (FLOAT_80616fb0 < f1) {
            f2 = 1.0f / SQRT(f1);
            f2 = f2 * -(FLOAT_80616ee0 * f1 * f2 * f2 - FLOAT_80616ee4);
            double n = (double)(FLOAT_80616ed0 * f2 * -(FLOAT_80616ee0 * f1 * f2 * f2 - FLOAT_80616ee4));
            rx = (double)(float)(rx * n);
            rz = (double)(float)(rz * n);
        }
        widthZ = (double)(float)((double)FLOAT_80616f0c * rz);
        widthX = (double)(float)((double)FLOAT_80616f0c * -rx);
        f1 = FLOAT_80616f2c;
        if (*(int *)(ctl + 0x20) != 1) {
            f1 = FLOAT_80616ef0 * *(float *)(ctl + 0x24) - FLOAT_80616ed0;
            f2 = FLOAT_80616ed8;
            if (FLOAT_80616ed8 < f1) f2 = f1;
            col = (unsigned int)(int)(FLOAT_80616f88 * f2) | 0xffff4600;
            f1 = FLOAT_80616f2c * *(float *)(ctl + 0x24);
        }
        double halfW = (double)f1;
        savedSub = (int)gx_set_sub_material_id(2);
        savedBlend = gx_set_blend_mode(2);
        savedChans = gx_set_num_chans(0);
        gx_begin_textured_batch(2, 0x92, 0, 6, tex);
        __gx_fifo_f((float)((double)*(float *)(ctl + 8) - widthZ));
        __gx_fifo_u(*(unsigned int *)(ctl + 0xc));
        __gx_fifo_f((float)((double)*(float *)(ctl + 0x10) - widthX));
        __gx_fifo_u(col);
        __gx_fifo_f(FLOAT_80616ed8); __gx_fifo_f(FLOAT_80616ed8);
        __gx_fifo_f((float)(tipX - widthZ));
        __gx_fifo_f((float)tipY);
        __gx_fifo_f((float)(tipZ - widthX));
        __gx_fifo_u(col);
        __gx_fifo_f(FLOAT_80616ed8); __gx_fifo_f(FLOAT_80616ed8);
        __gx_fifo_u(*(unsigned int *)(ctl + 8));
        __gx_fifo_f((float)((double)*(float *)(ctl + 0xc) + halfW));
        __gx_fifo_u(*(unsigned int *)(ctl + 0x10));
        __gx_fifo_u(0xffff4600);
        __gx_fifo_f(FLOAT_80616ed8); __gx_fifo_f(FLOAT_80616ed8);
        __gx_fifo_f((float)tipX);
        __gx_fifo_f((float)(tipY + halfW));
        __gx_fifo_f((float)tipZ);
        __gx_fifo_u(0xffff4600);
        __gx_fifo_f(FLOAT_80616ed8); __gx_fifo_f(FLOAT_80616ed8);
        __gx_fifo_f((float)((double)*(float *)(ctl + 8) + widthZ));
        __gx_fifo_u(*(unsigned int *)(ctl + 0xc));
        __gx_fifo_f((float)((double)*(float *)(ctl + 0x10) + widthX));
        __gx_fifo_u(col);
        __gx_fifo_f(FLOAT_80616ed8); __gx_fifo_f(FLOAT_80616ed8);
        __gx_fifo_f((float)(tipX + widthZ));
        __gx_fifo_f((float)tipY);
        __gx_fifo_f((float)(tipZ + widthX));
        __gx_fifo_u(col);
        __gx_fifo_f(FLOAT_80616ed8); __gx_fifo_f(FLOAT_80616ed8);
        gx_end_batch();
        gx_set_sub_material_id(savedSub);
        gx_set_blend_mode(savedBlend);
        gx_set_num_chans(savedChans);
    }
    __restore_gpr_28();
}

// 0x801ccae0 - ramp the worm's spin speed toward a phase-dependent target and
// write it into the SHKR shaker entity (+0xc0). The target step is chosen by
// comparing the current speed FLOAT_80610d28 / accumulated FLOAT_8061263c
// against several thresholds, optionally damped, then integrated by frame time.
void worm_spin_speed_update(void)
{
    int shaker = FUN_8023c32c(0x524b4853);   // 'SHKR'
    if ((FLOAT_80610d28 <= FLOAT_80616ed8) || (FLOAT_8061263c <= FLOAT_80616fb8)) {
        if ((FLOAT_80616ed8 <= FLOAT_80610d28) || (FLOAT_80617074 <= FLOAT_8061263c)) {
            if (FLOAT_80616ed8 <= FLOAT_80610d28) {
                FLOAT_80610d28 = FLOAT_80616f34;
            } else {
                FLOAT_80610d28 = FLOAT_8061707c;
            }
        } else {
            FLOAT_80610d28 = FLOAT_80617078;
        }
    } else {
        FLOAT_80610d28 = FLOAT_80616efc;
    }
    if (((FLOAT_80616ed8 < FLOAT_80610d28) && (FLOAT_80617080 < FLOAT_8061263c)) ||
        ((FLOAT_80610d28 < FLOAT_80616ed8) && (FLOAT_8061263c < FLOAT_80617084))) {
        FLOAT_80610d28 = FLOAT_80610d28 * FLOAT_80616f84;
    }
    FLOAT_8061263c = (float)(int)DAT_806108e4 * FLOAT_80610d28 *
                     (FLOAT_80616ed0 / (float)(int)DAT_806109c0) + FLOAT_8061263c;
    *(float *)(shaker + 0xc0) = FLOAT_8061263c;
}

// 0x801ccc20 - allocate the 0x50-byte worm control block, clear the worm sound
// bank references (0x163..0x17a), and unless the level opts out (feature bit
// 0x40) preload the worm entity/anim resources and a handful of extra banks.
void worm_alloc_and_preload(void)
{
    int id;
    DAT_8061266c = __wr_mem_alloc(0x50, 1);
    id = 0x163;
    do {
        sound_clear_bank_ref(id);
        id = id + 1;
    } while (id < 0x17b);
    if ((DAT_805015c0 & 0x40) == 0) {
        entity_preload_resources(0x1bc);
        entity_preload_resources(0x1cd);
        entity_preload_resources(0x1cc);
        entity_preload_resources(0x1bd);
        entity_preload_resources(0x1ce);
        entity_preload_resources(0x144);
        entity_preload_resources(0x148);
        entity_preload_resources(0x1b3);
        entity_preload_resources(0x1da);
        entity_preload_resources(0x1e3);
        sound_clear_bank_ref(0x180);
        sound_clear_bank_ref(0x145);
        sound_clear_bank_ref(0x567);
        sound_clear_bank_ref(0x139);
    }
}

// 0x801cccf0 - zero the first 0x4c bytes of the worm control block.
void worm_reset(void)
{
    memset_wrapper(DAT_8061266c, 0, 0x4c);
}

// 0x801ccd1c - level-scoped worm init: reset the control block, seed its
// handle/state fields, register the two worm level-actor name tables (unless
// the level opts out), then build the curve/particle systems and clear the
// per-run state words.
void worm_level_init(void)
{
    memset_wrapper(DAT_8061266c, 0, 0x4c);
    *(int *)(DAT_8061266c + 4) = -1;
    *(int *)(DAT_8061266c + 0x40) = -1;
    *(float *)(DAT_8061266c + 0x44) = FLOAT_80616ed0;
    FUN_80244a54(0x17);
    if ((DAT_805015c0 & 0x40) == 0) {
        __wr_level_actors_register(0, &DAT_8044ff3c, 6);
        __wr_level_actors_register(1, &DAT_80450074, 6);
        level_resource_pool_setup();
    }
    worm_setup();
    FUN_8036e3dc();
    worm_particle_defs_build();
    worm_effect_a_init();
    FUN_801cd7d8();
    FUN_801ceaf0();
    DAT_80612640 = 0;
    DAT_80612644 = 0;
    DAT_806124a8 = 0;
}

// 0x801ccde8 - ambient-sound scheduler: count down the timer word at
// controlBlock[0] by frame time; when it expires and the passed level exceeds a
// threshold, play a randomised positional bubble/rumble one-shot from bank
// 0x163+n with a random pitch, then re-arm the timer to a random interval.
void worm_ambient_sound_schedule(float lastLevel)
{
    int params[20];
    int bankIdx;
    double t, spinA, spinB;

    *(float *)DAT_8061266c =
        -(FLOAT_806108ec * (FLOAT_80616ed0 / (float)(int)DAT_806109c0) - *(float *)DAT_8061266c);
    if (*(float *)DAT_8061266c <= FLOAT_80616ed8) {
        t = (double)((float)((double)FLOAT_80616f6c - (double)lastLevel) *
                     (FLOAT_80616ed0 +
                      (float)((double)(int)__wr_rng_tick() - DOUBLE_80617010) / FLOAT_80617004));
        if ((double)FLOAT_80616ed8 < t) {
            double ang = (double)(FLOAT_80616f20 *
                          ((float)((double)(int)__wr_rng_tick() - DOUBLE_80617010) / FLOAT_80617004));
            spinA = math_poly_trig((double)(float)(ang - (double)FLOAT_80616f28));
            spinA = (double)(float)((double)FLOAT_80617088 * spinA);
            spinB = math_poly_trig(ang);
            spinB = (double)(float)((double)FLOAT_80617088 * spinB);
            bankIdx = (int)(FLOAT_8061708c *
                            ((float)((double)(int)__wr_rng_tick() - DOUBLE_80617010) / FLOAT_80617004) +
                            FLOAT_80616ee0);
            sound_init_play_params((int)(size_t)params);
            params[13] = params[13] | 0xa0;              // uStack flags -> local_78|0xa0
            *(char *)((char *)params + 0x28) = 1;         // local_60 = 1
            *(float *)((char *)params + 0x2c) = FLOAT_80617090; // local_5c
            *(float *)((char *)params + 0x18) = (float)t;        // local_70
            *(float *)((char *)params + 0x1c) = (float)spinA;    // local_6c
            *(float *)((char *)params + 0x20) = FLOAT_80616ed8;  // local_68
            *(float *)((char *)params + 0x24) = (float)spinB;    // local_64
            params[0] = bankIdx + 0x163;
            sound_play_request(params);
        }
        *(float *)DAT_8061266c =
            FLOAT_80617094 * ((float)((double)(int)__wr_rng_tick() - DOUBLE_80617010) / FLOAT_80617004) +
            FLOAT_80616f34;
    }
}

// 0x801ccfe4 - per-frame worm ambient audio tick: average a proximity intensity
// over the active players in the worm rooms/zones (room-id class checks),
// smooth-step the level toward it, drive a 3d loop sound (bank 0x139), then run
// the ambient one-shot scheduler.
void worm_ambient_audio_tick(void)
{
    unsigned int count = 0;
    int off = 0;
    int remaining = DAT_80612610;
    float level = FLOAT_80616ed8;
    int ctl = DAT_8061266c;

    if (0 < DAT_80612610) {
        do {
            if (-1 < *(int *)(DAT_80611d78 + off + 4)) {
                int room = *(int *)(*(int *)(DAT_80611d78 + off + 8) + 0x2a0);
                if (room == 0x18) {
                    level = level + FLOAT_80616ed0;
                } else if (room < 0x18) {
                    if ((room < 0x17) && (0x14 < room)) level = level + FLOAT_80616ed0;
                } else if (room == 0x2d) {
                    level = level + FLOAT_80616ee0;
                } else if ((room < 0x2d) && (0x2b < room)) {
                    level = level + FLOAT_80616ed0;
                }
                count = count + 1;
            }
            off = off + 0x141c;
            remaining = remaining - 1;
        } while (remaining != 0);
    }
    scalar_step_toward_snap((double)(level / (float)(int)count),
                            (double)(FLOAT_80616f1c * FLOAT_806108ec),
                            (float *)(ctl + 0x44));
    __wr_sound3d((double)(FLOAT_80616ef0 * *(float *)(ctl + 0x44)), (double)FLOAT_80617098,
                 (double)FLOAT_80616ed0, ctl + 0x40, 0, 0x139, 0);
    worm_ambient_sound_schedule(*(float *)(ctl + 0x44));
}

// 0x801cd100 - thin wrapper: run the ambient scheduler with a zero level (used
// when the worm is inactive so the timer keeps ticking without spawning sound).
void worm_ambient_sound_tick0(void)
{
    worm_ambient_sound_schedule(FLOAT_80616ed8);
}

// 0x801cd124 - master per-frame worm update: run the pre-tick hook + ambient
// audio, fire the per-mode scene hooks by the primary player's room, run the
// worm AI/render update + water-jet update, then trigger a scripted actor
// effect on the SCI2 team entity when present.
void worm_master_update(double p2, double p3, double p4, double p6, double p7, double p8)
{
    int nrm[3];
    int found;

    FUN_8036e03c(0, p2, p3, p4, 0, p6, p7, p8);
    worm_ambient_audio_tick();
    if ((DAT_80611d74 != 0) &&
        ((*(int *)(DAT_80611d74 + 0xdec) == 5) || (*(int *)(DAT_80611d74 + 0xdec) == 4))) {
        FUN_801ceb18();
        FUN_801cec9c();
    }
    if ((DAT_80611d74 != 0) &&
        ((*(int *)(DAT_80611d74 + 0xdec) == 0x18) || (*(int *)(DAT_80611d74 + 0xdec) == 0x14))) {
        int slotOff = DAT_80612610 * 0xea0;
        int rem = DAT_80612098 - DAT_80612610;
        if (DAT_80612610 < DAT_80612098) {
            do {
                int slot = DAT_80611d90 + slotOff;
                if ((*(int *)(slot + 0xbf0) != 0) &&
                    (*(int *)(*(int *)(slot + 0xbf0) + 8) == 0x4752444e)) {   // 'GRDN'
                    unsigned int flag = (DAT_805015bd == '\n')
                                        ? *(unsigned int *)(slot + 0x14)
                                        : *(unsigned int *)(slot + 0x1c);
                    if (((flag & 1) != 0) && (FUN_80220f30(1) == 0)) {
                        FUN_802212b4(1);
                        FUN_8022136c(1);
                    }
                    break;
                }
                slotOff = slotOff + 0xea0;
                rem = rem - 1;
            } while (rem != 0);
        }
    }
    FUN_801ceddc();
    worm_update(p2, p3, p4, p6, p7, p8);
    worm_water_jet_update();
    nrm[0] = DAT_803df398;
    nrm[1] = DAT_803df39c;
    nrm[2] = DAT_803df3a0;
    found = aiactor_find_entity_by_team(0x53434932, 2);   // 'SCI2'
    if (found != 0) {
        FUN_80365ce0(found, 0x7363316a, 3,
                     *(int *)(*(int *)(*(int *)(found + 4) + 0x84) + 0x30), nrm);
    }
    FUN_802455c8();
}

// 0x801cd2b4 - worm death-gib animation event: while the death clip plays, at
// frame 0xbd fire an explosion, and across frames 0xce..0xd2 spawn ten debris
// chunks (entity type 0x1cc) as physics sub-objects with randomised launch
// velocity, spin, and angular impulse. Runs once (clears +0x1a0 when done).
void worm_death_gib_event(int ent)
{
    int clipFrame;

    __save_gpr_28();
    if ((*(int *)(ent + 0x1a0) != 0) &&
        (anim_secondary_clip_finished(*(int *)(ent + 4)) == 0)) {
        clipFrame = anim_get_current_frame(ent);
        if (clipFrame < 0xce) {
            if (clipFrame == 0xbd) {
                float nrmA[3];
                float posA[3];
                nrmA[0] = *(float *)&DAT_803df3a4;
                *(int *)&nrmA[1] = DAT_803df3a8;
                *(int *)&nrmA[2] = DAT_803df3ac;
                posA[0] = *(float *)(ent + 100) + *(float *)(ent + 0x1a8);
                posA[1] = *(float *)(ent + 0x68) + *(float *)(ent + 0x1ac);
                posA[2] = *(float *)(ent + 0x6c) + *(float *)(ent + 0x1b0);
                player_ai_explosion_spawn(0x1c, 0, posA, nrmA, 0, 0, 1);
            }
        } else if (clipFrame < 0xd3) {
            int k = 0;
            do {
                float vel[3];
                float pos[3];
                float imp[3];
                double r0 = (double)(FLOAT_80616ef0 *
                    ((float)((double)(int)__wr_rng_tick() - DOUBLE_80617010) / FLOAT_80617004 - FLOAT_80616ee0));
                double r1 = (double)(FLOAT_80616ef0 *
                    ((float)((double)(int)__wr_rng_tick() - DOUBLE_80617010) / FLOAT_80617004 - FLOAT_80616ee0));
                vel[0] = (float)(r0 * (double)FLOAT_80616f68);
                vel[1] = (float)(r1 * (double)FLOAT_80616f68);
                double r2 = (double)(FLOAT_80616ef0 *
                    ((float)((double)(int)__wr_rng_tick() - DOUBLE_80617010) / FLOAT_80617004 - FLOAT_80616ee0));
                vel[2] = (float)(r2 * (double)FLOAT_80616f68);
                pos[0] = *(float *)(ent + 100) + *(float *)(ent + 0x1a8);
                pos[1] = *(float *)(ent + 0x68) + *(float *)(ent + 0x1ac);
                pos[2] = *(float *)(ent + 0x6c) + *(float *)(ent + 0x1b0);
                double j0 = (double)(FLOAT_80616ee4 *
                    ((float)((double)(int)__wr_rng_tick() - DOUBLE_80617010) / FLOAT_80617004 - FLOAT_80616ee0));
                pos[0] = *(float *)(ent + 0x1c0) * (float)j0 + pos[0];
                double j1 = (double)(FLOAT_80616ee4 *
                    ((float)((double)(int)__wr_rng_tick() - DOUBLE_80617010) / FLOAT_80617004 - FLOAT_80616ee0));
                pos[1] = *(float *)(ent + 0x1c4) * (float)j1 + pos[1];
                double j2 = (double)(FLOAT_80616ee4 *
                    ((float)((double)(int)__wr_rng_tick() - DOUBLE_80617010) / FLOAT_80617004 - FLOAT_80616ee0));
                pos[0] = (float)((double)pos[0] + (double)FLOAT_80616ed0);
                pos[2] = (float)((double)*(float *)(ent + 0x1c8) * (float)j2 + (double)pos[2]);

                int *chunk = __wr_entity_spawn(0x1cc, pos, vel, 0, 0);
                int h = __wr_subobj_pool_alloc();
                chunk[0x20] = h;
                if (h == 0) {
                    entity_destroy_with_event(chunk);
                } else {
                    __wr_subobj_build_desc((unsigned int)chunk[0x20], chunk, 6, -1, 2.86986e-42f);
                    subobj_set_collision_groups((unsigned int)chunk[0x20], 8, 0xf1);
                    // re-randomise launch velocity + spin for the impulse
                    vel[0] = FLOAT_80616ef0 *
                        ((float)((double)(int)__wr_rng_tick() - DOUBLE_80617010) / FLOAT_80617004 - FLOAT_80616ee0);
                    vel[1] = FLOAT_80616ef0 *
                        ((float)((double)(int)__wr_rng_tick() - DOUBLE_80617010) / FLOAT_80617004 - FLOAT_80616ee0);
                    vel[0] = vel[0] * *(float *)(ent + 0x1c0);
                    vel[1] = vel[1] * *(float *)(ent + 0x1c4);
                    vel[2] = FLOAT_80616ef0 *
                        ((float)((double)(int)__wr_rng_tick() - DOUBLE_80617010) / FLOAT_80617004 - FLOAT_80616ee0) *
                        *(float *)(ent + 0x1c8);
                    float spinX = FLOAT_80616ef0 *
                        ((float)((double)(int)__wr_rng_tick() - DOUBLE_80617010) / FLOAT_80617004 - FLOAT_80616ee0);
                    imp[1] = FLOAT_80616ef0 *
                        ((float)((double)(int)__wr_rng_tick() - DOUBLE_80617010) / FLOAT_80617004 - FLOAT_80616ee0);
                    imp[1] = (float)((double)imp[1] * (double)FLOAT_80617098);
                    imp[2] = (float)((double)(FLOAT_80616ef0 *
                        ((float)((double)(int)__wr_rng_tick() - DOUBLE_80617010) / FLOAT_80617004 - FLOAT_80616ee0)) *
                        (double)FLOAT_80617098);
                    double magX = ABS((double)(float)((double)spinX * (double)FLOAT_80617098));
                    if ((double)FLOAT_80617088 < magX) {
                        imp[0] = (float)((double)FLOAT_80616f34 * magX);
                    } else {
                        imp[0] = (float)((double)FLOAT_80616f34 * (double)FLOAT_80617088);
                    }
                    __wr_subobj_impulse(chunk[0x20], vel, imp);
                }
                k = k + 1;
            } while (k < 10);
            *(int *)(ent + 0x1a0) = 0;
        }
    }
    __restore_gpr_28();
}

// ===========================================================================
// batch_595 (0x801cd7d8 - 0x801ce410): worm secondary effects + scripted
// destruction set-pieces. The mist particle def, the slam-impact event (burst
// + shockwave + radial damage sweep), the floor/lift shatter spawners that
// break scenery into physics debris, the tree set-piece (falling-leaves +
// splash particle systems, its leaf spawn callback, init + per-frame bob), the
// cutscene-enter trigger, and a rocking-object oscillation updater.
//
// Non-matching carries over: spill frames (FUN_803826e4/803826f0 open,
// FUN_80382730/8038273c close) opaque; caller-saved FP spill copies dropped;
// int<->double magic-bias reduced to (double)(int); rng / sound3d / switch /
// damage-dispatch route through the existing __wr_* aliases; helpers whose
// generated_decls prototype is narrower than the paired-single/wide call site
// (entity_spawn_with_activation, weapon_start_delayed_event,
// particle_overlay_create, particle_emitter_set_flags, sound_stop_handle) go
// through new file-local typed aliases.
extern "C" {
// --- new float / double constants ---
extern float FLOAT_8061709c; extern float FLOAT_806170a0; extern float FLOAT_806170a4;
extern float FLOAT_806170a8; extern float FLOAT_806170ac; extern float FLOAT_806170b0;
extern float FLOAT_806170c8; extern float FLOAT_806170cc; extern float FLOAT_806170e0;
extern float FLOAT_806170e4; extern float FLOAT_806170e8;
extern double DOUBLE_806170b8; extern double DOUBLE_806170c0;
extern double DOUBLE_806170d0; extern double DOUBLE_806170d8;

// --- ROM const vectors (effect seeds) ---
extern int DAT_803df3b0; extern int DAT_803df3b4; extern int DAT_803df3b8;
extern int DAT_803df3bc; extern int DAT_803df3c0; extern int DAT_803df3c4;
extern int DAT_803df3c8; extern int DAT_803df3cc; extern int DAT_803df3d0;
extern int DAT_803df3d4; extern int DAT_803df3d8; extern int DAT_803df3dc;
extern int DAT_803df3e0; extern int DAT_803df3e4; extern int DAT_803df3e8;
extern int DAT_803df3ec; extern int DAT_803df3f0; extern int DAT_803df3f4;
extern int DAT_803df3f8; extern int DAT_803df3fc; extern int DAT_803df400;
extern int DAT_803df404; extern int DAT_803df408; extern int DAT_803df40c;
extern int DAT_803df410; extern int DAT_803df414; extern int DAT_803df418;

// --- new particle def/emitter blobs ---
extern int DAT_805c3d28; extern int DAT_805c3df4; extern int DAT_805c3e78;
extern int DAT_805c3f44; extern int DAT_805c3fc8; extern int DAT_805c4094;
extern int DAT_805c40cc;

// --- per-worm-room entity sweep lists (indexed by controlBlock+0x34) ---
extern int DAT_805401dc;   // per-room entity count table
extern int DAT_8053fb2c;   // per-room entity ptr-table base

// --- raw frontier helpers ---
extern int  FUN_803826e4(void);           // spill-frame open (returns entity ctx)
extern void FUN_80382730(void);           // spill-frame close
extern double FUN_80390ac4(double);        // smooth bob curve (sin-like)
// anim_primary_clip_time_left resolves permissively through generated_externs.h
}

// ---- typed aliases for narrow-header / wide call-site mismatches ----
static int  (*const __wr_entity_spawn_act)(int, unsigned int, int *, float *, int, int)
    = (int (*)(int, unsigned int, int *, float *, int, int)) &entity_spawn_with_activation;
static int  (*const __wr_weapon_delayed)(double, int, int)
    = (int (*)(double, int, int)) &weapon_start_delayed_event;
static void (*const __wr_emitter_set_cb)(void *)
    = (void (*)(void *)) &particle_emitter_set_flags;
static void (*const __wr_overlay_create)(double, double, double, double, double, void *, void *,
                                         int, unsigned int, int)
    = (void (*)(double, double, double, double, double, void *, void *, int, unsigned int, int))
      &particle_overlay_create;
static void (*const __wr_sound_stop)(void) = (void (*)(void)) &sound_stop_handle;

// forward decls (defined later this batch)
void worm_tree_effect_build(void);
void worm_tree_leaf_spawn_cb(int spillHi, float *outPos);

// 0x801cd7d8 - build the worm mist/spray particle def + emitter (DAT_805c3d28):
// a short-lived downward-drifting mist used by the surfacing effect.
void worm_mist_defs_build(void)
{
    particle_def_create((double)FLOAT_80616ed0, (double)FLOAT_80616ed0, &DAT_805c3d28, 2, 1, 1, 0x301, 0);
    particle_def_set_size_keyframes((double)FLOAT_80616ed0, (double)FLOAT_80616ed8,
                                    (double)FLOAT_80616ed8, (double)FLOAT_80616ed0,
                                    0x65656500, 0x65656500, 0);
    particle_def_set_size_start_end((double)FLOAT_80616f98, (double)FLOAT_80616ed8,
                                    (double)FLOAT_80616eec, (double)FLOAT_80616ed0);
    particle_def_set_color_end((double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                               (double)FLOAT_80616ef4, (double)FLOAT_8061703c);
    particle_def_set_velocity_ranges((double)FLOAT_80616f3c, (double)FLOAT_80616ed8,
                                     (double)FLOAT_80616ee0, (double)FLOAT_80616f0c,
                                     (double)FLOAT_80617040, (double)FLOAT_80617030);
    particle_emitter_create((double)FLOAT_8061709c, (double)FLOAT_80616efc, (double)FLOAT_80616f0c,
                            &DAT_805c3df4, &DAT_805c3d28, 0, 1);
    particle_emitter_set_color_range((double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                                     (double)FLOAT_806170a0, (double)FLOAT_80616ed8);
}

// 0x801cd8c0 - worm slam-impact animation event (entity via spill frame): at a
// point in the slam clip, spawn a burst emitter + a ground shockwave decal and
// play the impact sound (bank 0x144); then, once the clip is past that point,
// sweep every entity in the worm's current room and dispatch radial knock /
// damage to those inside the blast radius.
void worm_slam_impact_event(void)
{
    int ent = FUN_803826e4();
    float pos[3];
    float nrm[3];

    if ((*(int *)(ent + 0x1a0) != 0) &&
        (anim_primary_clip_time_left((double)FLOAT_80616ef0, *(int *)(ent + 4)) != 0)) {
        nrm[0] = *(float *)&DAT_803df3b0;
        *(int *)&nrm[1] = DAT_803df3b4;
        *(int *)&nrm[2] = DAT_803df3b8;
        pos[0] = *(float *)&DAT_803df3bc;
        *(int *)&pos[1] = DAT_803df3c0;
        *(int *)&pos[2] = DAT_803df3c4;
        DAT_806124a8 = 1;
        particle_emitter_spawn(0, (unsigned int)(size_t)&DAT_805c3df4, 0, pos, nrm);
        FUN_8037dc94((double)FLOAT_8061700c, (double)FLOAT_80616ed0, (double)FLOAT_80616ed0, 0, 0xf,
                     pos, nrm, (int *)0, 0x200000, (char)DAT_8061265c, 0);
        *(int *)(ent + 0x1a0) = 0;
        if (*(int *)(ent + 0x22c) < 0) {
            int sp[20];
            sound_init_play_params((int)(size_t)sp);
            sp[13] = sp[13] | 0xa0;
            *(char *)((char *)sp + 0x30) = 1;
            sp[0] = 0x144;
            *(float *)((char *)sp + 0x18) = pos[0];
            *(int *)((char *)sp + 0x1c) = *(int *)&pos[1];
            *(int *)((char *)sp + 0x20) = *(int *)&pos[2];
            *(unsigned int *)(ent + 0x22c) = sound_play_request(sp);
        }
    }
    if (*(int *)(ent + 0x1a0) == 0) {
        float center[3];
        center[0] = *(float *)&DAT_803df3c8;
        center[1] = *(float *)&DAT_803df3cc;
        center[2] = *(float *)&DAT_803df3d0;
        if (*(int *)(ent + 0x22c) < 0) {
            if (*(int *)(DAT_80611d74 + 0xdec) == *(int *)(ent + 0x34)) {
                int sp[20];
                sound_init_play_params((int)(size_t)sp);
                *(char *)((char *)sp + 0x30) = 1;
                sp[13] = sp[13] | 0xa0;
                sp[0] = 0x144;
                *(int *)((char *)sp + 0x18) = *(int *)(ent + 100);
                *(int *)((char *)sp + 0x1c) = *(int *)(ent + 0x68);
                *(int *)((char *)sp + 0x20) = *(int *)(ent + 0x6c);
                *(unsigned int *)(ent + 0x22c) = sound_play_request(sp);
            }
        } else if (*(int *)(DAT_80611d74 + 0xdec) != *(int *)(ent + 0x34)) {
            __wr_sound_stop();
            *(int *)(ent + 0x22c) = -1;
        }
        int room = *(int *)(ent + 0x34);
        int off = 0;
        for (int i = 0; i < (int)(&DAT_805401dc)[room]; i = i + 1) {
            int e2 = *(int *)((&DAT_8053fb2c)[room] + off);
            if ((*(unsigned int *)(e2 + 0x28) & 0x80000000) == 0) {
                double ex = (double)*(float *)(e2 + 100);
                float dy = (float)((double)*(float *)(e2 + 0x68) - (double)center[1]);
                double ez = (double)*(float *)(e2 + 0x6c);
                double dz2 = (double)((float)(ez - (double)center[2]) * (float)(ez - (double)center[2]));
                double d2 = (double)(float)(dz2 + (double)((float)(ex - (double)center[0]) *
                                                           (float)(ex - (double)center[0]) + dy * dy));
                if (d2 < (double)FLOAT_806170a4) {
                    float nrm2[3];
                    nrm2[0] = (float)(ex - (double)*(float *)(ent + 100));
                    nrm2[1] = (float)((double)*(float *)(e2 + 0x68) - (double)*(float *)(ent + 0x68));
                    nrm2[2] = (float)(ez - (double)*(float *)(ent + 0x6c));
                    double dist = math_sqrt(d2);
                    double inv = (double)(float)((double)FLOAT_80616ed0 / dist);
                    nrm2[0] = (float)((double)nrm2[0] * inv);
                    nrm2[1] = (float)((double)nrm2[1] * inv);
                    nrm2[2] = (float)((double)nrm2[2] * inv);
                    if (*(int *)(e2 + 0x20) == 8) {
                        int *owner = *(int **)(e2 + 0x7c);
                        if (((owner == (int *)0) || (DAT_80612610 <= owner[2])) &&
                            (*(int *)(*owner + 0x60) != 0x524f434b)) {
                            character_damage_spawn_effects();
                        } else {
                            int ev[19];
                            unsigned long long ctx = __wr_switch_reset((int)ev);
                            ev[0] = e2;
                            *(int *)((char *)ev + 0x2c) = 0x15;
                            *(int *)((char *)ev + 0x30) = 0x15;
                            *(float *)((char *)ev + 0x38) = FLOAT_80616f04;
                            *(int *)((char *)ev + 0x40) = e2 + 100;
                            *(float **)((char *)ev + 0x48) = nrm2;
                            __wr_damage_dispatch(ctx, (double)nrm2[0], inv, dz2, ez, ex, ez, ex, ev);
                        }
                    } else {
                        int ev[19];
                        unsigned long long ctx = __wr_switch_reset((int)ev);
                        ev[0] = e2;
                        *(int *)((char *)ev + 0x2c) = 0x15;
                        *(int *)((char *)ev + 0x30) = 0x15;
                        *(float *)((char *)ev + 0x38) = FLOAT_80616f04;
                        *(int *)((char *)ev + 0x40) = e2 + 100;
                        *(float **)((char *)ev + 0x48) = nrm2;
                        __wr_damage_dispatch(ctx, (double)nrm2[0], inv, dz2, ez, ex, ez, ex, ev);
                    }
                }
            }
            off = off + 4;
        }
    }
    FUN_80382730();
}

// 0x801cdc70 - worm floor-shatter: find the 'FLOR' scenery entity and spawn 12
// fragment entities (its child type) as physics sub-objects with staggered
// activation bits and a delayed cleanup event, then destroy the intact floor.
void worm_floor_shatter_spawn(double p2, double p3)
{
    unsigned int spill = FUN_803826f0();
    int *flor = (int *)FUN_8023c32c(0x464c4f52);   // 'FLOR'
    if (flor != (int *)0) {
        float seed[3];
        *(int *)&seed[0] = flor[0x30];
        *(int *)&seed[1] = flor[0x1c];
        *(int *)&seed[2] = flor[0x32];
        for (int i = 0; i < 0xc; i = i + 1) {
            int frag = __wr_entity_spawn_act(flor[8], (unsigned int)*(unsigned short *)(flor + 0x25),
                                             flor + 0x19, seed, 0, 0);
            *(int *)(*(int *)(frag + 4) + 0x30) = 1 << (i + 1);
            int h = __wr_subobj_pool_alloc();
            *(int *)(frag + 0x80) = h;
            if (h != 0) {
                __wr_subobj_build_desc((unsigned int)*(int *)(frag + 0x80), (int *)frag, 9, -1, 0.0f);
                subobj_set_collision_groups((unsigned int)*(int *)(frag + 0x80), 8, 0xf1);
                subobj_dirty_list_add((unsigned int)*(int *)(frag + 0x80));
            }
            __wr_weapon_delayed((double)FLOAT_80616fd8, frag, 8);
        }
        entity_destroy_with_event((int *)flor);
    }
    FUN_8038273c();
    (void)spill; (void)p2; (void)p3;
}

// 0x801cdd68 - worm lift-shatter: like the floor shatter but for the 'LFTA'
// entity - spawns 10 fragments and applies an upward launch force (from the
// primary player position +0xa8) to each.
void worm_lift_shatter_spawn(double p2, double p3)
{
    unsigned int spill = FUN_803826f0();
    int *lift = (int *)FUN_8023c32c(0x4154464c);   // 'LFTA'
    if (lift != (int *)0) {
        float seed[3];
        *(int *)&seed[0] = lift[0x30];
        *(int *)&seed[1] = lift[0x1c];
        *(int *)&seed[2] = lift[0x32];
        for (int i = 0; i < 10; i = i + 1) {
            int frag = __wr_entity_spawn_act(lift[8], (unsigned int)*(unsigned short *)(lift + 0x25),
                                             lift + 0x19, seed, 0, 0);
            *(int *)(*(int *)(frag + 4) + 0x30) = 1 << (i + 1);
            int h = __wr_subobj_pool_alloc();
            *(int *)(frag + 0x80) = h;
            if (h != 0) {
                __wr_subobj_build_desc((unsigned int)*(int *)(frag + 0x80), (int *)frag, 9, -1, 1.83761e-40f);
                subobj_set_collision_groups((unsigned int)*(int *)(frag + 0x80), 8, 0xf1);
                float force[3];
                force[0] = FLOAT_80616ed8;
                force[1] = FLOAT_806170a8;
                force[2] = FLOAT_80616ed8;
                subobj_add_force_at_point((unsigned int)*(int *)(frag + 0x80),
                                          (float *)(DAT_80611d74 + 0xa8), force);
            }
            __wr_weapon_delayed((double)FLOAT_80616fd8, frag, 8);
        }
        entity_destroy_with_event((int *)lift);
    }
    FUN_8038273c();
    (void)spill; (void)p2; (void)p3;
}

// 0x801cde84 - leaf-particle spawn callback: position a leaf at a random offset
// from the 'TREE' entity's origin.
void worm_tree_leaf_spawn_cb(int spillHi, float *outPos)
{
    int tree = FUN_8023c32c(0x54524545);   // 'TREE'
    float t = (float)((double)(int)__wr_rng_tick() - DOUBLE_80617010) / FLOAT_80617004;
    outPos[0] = *(float *)&DAT_803df3d4 * t + *(float *)(tree + 100);
    outPos[1] = *(float *)&DAT_803df3d8 * t + *(float *)(tree + 0x68);
    outPos[2] = *(float *)&DAT_803df3dc * t + *(float *)(tree + 0x6c);
    (void)spillHi;
}

// 0x801cdf34 - build the tree set-piece particle systems: a falling-leaves
// emitter (DAT_805c3e78, with the leaf spawn callback + a box spawn volume) and
// a water-splash emitter with a fading overlay decal (DAT_805c3fc8/805c4094).
void worm_tree_effect_build(void)
{
    float leafBox[6];   // local_3c..
    float splashBox[3]; // local_48..

    FUN_803826e4();
    leafBox[0] = *(float *)&DAT_803df3ec; *(int *)&leafBox[1] = DAT_803df3f0; *(int *)&leafBox[2] = DAT_803df3f4;
    *(int *)&leafBox[3] = DAT_803df3e0;   *(int *)&leafBox[4] = DAT_803df3e4; *(int *)&leafBox[5] = DAT_803df3e8;
    splashBox[0] = *(float *)&DAT_803df3f8; *(int *)&splashBox[1] = DAT_803df3fc; *(int *)&splashBox[2] = DAT_803df400;

    particle_def_create((double)FLOAT_80616efc, (double)FLOAT_80616ed0, &DAT_805c3e78, 5, 0, 1, 0x3b3, 0);
    particle_def_set_size_keyframes((double)FLOAT_80616ee0, (double)FLOAT_80616ed8,
                                    (double)FLOAT_80616ed8, (double)FLOAT_80616ed4,
                                    0x19200e00, 0x19200e00, 0);
    particle_def_set_velocity_ranges((double)FLOAT_80616f0c, (double)FLOAT_80616ed8,
                                     (double)FLOAT_806170ac, (double)FLOAT_80616ed8,
                                     (double)FLOAT_80616ed0, (double)FLOAT_80616ee0);
    particle_def_set_color_end((double)FLOAT_80616ed8, (double)FLOAT_80616ed0,
                               (double)FLOAT_80616ed0, (double)FLOAT_80616ed8);
    particle_def_set_size_start_end((double)FLOAT_80616ed8, (double)FLOAT_80616f20,
                                    (double)FLOAT_80616ed8, (double)FLOAT_80616ed8);
    particle_def_set_spawn_volume(leafBox, &leafBox[3], leafBox, leafBox);
    particle_emitter_create((double)FLOAT_80616f34, (double)FLOAT_80616ed8, (double)FLOAT_80616ee0,
                            &DAT_805c3f44, &DAT_805c3e78, 7, 0x8007);
    particle_emitter_set_color_range((double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                                     (double)FLOAT_80617088, (double)FLOAT_80616ed8);
    __wr_emitter_set_cb((void *)&worm_tree_leaf_spawn_cb);

    particle_def_create((double)FLOAT_80616ed0, (double)FLOAT_80616ed0, &DAT_805c3fc8, 2, 1, 1, 0x301, 0);
    particle_def_set_size_keyframes((double)FLOAT_80616ed0, (double)FLOAT_80616ed8,
                                    (double)FLOAT_80616ed8, (double)FLOAT_80616ed0,
                                    0x15157f00, 0x65656f00, 0);
    particle_def_set_size_start_end((double)FLOAT_80616f98, (double)FLOAT_80616ed8,
                                    (double)FLOAT_80616eec, (double)FLOAT_80616ed0);
    particle_def_set_color_end((double)FLOAT_80616ed8, (double)FLOAT_80616f04,
                               (double)FLOAT_80616ef4, (double)FLOAT_8061703c);
    particle_def_set_velocity_ranges((double)FLOAT_80616f3c, (double)FLOAT_80616ed8,
                                     (double)FLOAT_80616ee0, (double)FLOAT_80616f0c,
                                     (double)FLOAT_80617040, (double)FLOAT_80617030);
    __wr_overlay_create((double)FLOAT_80616f0c, (double)FLOAT_806170b0, (double)FLOAT_80617050,
                        (double)FLOAT_80616f0c, (double)FLOAT_80616f0c, &DAT_805c4094, splashBox,
                        0x290, 0xc8c8ffff, 0xa000);
    particle_overlay_set_fade_params((double)FLOAT_80616f2c, 0, 0x7f7f7f1e);
    particle_emitter_create((double)FLOAT_80616f24, (double)FLOAT_80616f24, (double)FLOAT_80616f0c,
                            &DAT_805c40cc, &DAT_805c3fc8, 0, 7);
    particle_emitter_set_color_range((double)FLOAT_80616ed8, (double)FLOAT_80616ed8,
                                     (double)FLOAT_806170a0, (double)FLOAT_80616ed8);
    particle_emitter_set_texture((int)(size_t)&DAT_805c4094);
    FUN_80382730();
}

// 0x801ce1b0 - per-frame bob updater for a floating object (the tree trunk on
// water): advance a phase counter, compute the vertical bob height from a
// smooth curve, spawn a splash when it dips below the surface, and on the final
// bob release the falling-leaves emitter + stop updating.
void worm_float_bob_update(int ent)
{
    float ampNum = FLOAT_806170cc;
    float ampDen = FLOAT_806170c8;
    if ((DAT_806108e4 != 0) && (*(int *)(ent + 0x1a0) != 0)) {
        double prev = (double)*(float *)(ent + 0x1a4);
        *(float *)(ent + 0x1a4) =
            (float)((double)(FLOAT_80616ed0 / (float)(int)DAT_806109c0) * (double)FLOAT_806108ec + prev);
        double s = (double)FUN_80390ac4((double)((ampDen * *(float *)(ent + 0x1a4)) / ampNum));
        *(float *)(ent + 200) =
            (float)((DOUBLE_806170b8 * DOUBLE_806170c0 * (s - DOUBLE_806170d0)) / DOUBLE_806170d8);
        if (((double)FLOAT_80616ed0 < (double)*(float *)(ent + 0x1a4)) &&
            (prev <= (double)FLOAT_80616ed0)) {
            worm_splash_emit_at((float *)(ent + 100));
        }
        if (FLOAT_806170e0 < *(float *)(ent + 200)) {
            float dir[3];
            dir[0] = *(float *)&DAT_803df404;
            *(int *)&dir[1] = DAT_803df408;
            *(int *)&dir[2] = DAT_803df40c;
            particle_emitter_spawn(0, (unsigned int)(size_t)&DAT_805c3f44, 0,
                                   (float *)(ent + 100), dir);
            *(int *)(ent + 0x1a0) = 0;
            *(float *)(ent + 200) = FLOAT_806170e0;
            *(float *)(ent + 0xe0) = FLOAT_80616edc * -*(float *)(ent + 0xe0);
        }
    }
}

// 0x801ce30c - scripted cutscene trigger: move the local player into the
// cutscene/menu view state anchored at the 'CLFT' spawn point.
void worm_cutscene_enter_clft(void)
{
    int playerCtx = *(int *)(DAT_80611d74 + 0x14);
    int spawn = spawn_point_find_by_entity(0x54464c43);   // 'CLFT'
    player_enter_menu_state(*(unsigned int *)(playerCtx + 0x150), (unsigned int)spawn, 0, 4, 0, 0);
}

// 0x801ce360 - tree set-piece init: find the 'TREE' entity, build its particle
// systems, seed its bob state, spawn its splash emitter, and register it for
// the per-frame bob update callback (worm_float_bob_update).
void worm_tree_effect_init(void)
{
    int tree = FUN_8023c32c(0x54524545);   // 'TREE'
    float dir[3];
    dir[0] = *(float *)&DAT_803df410;
    *(int *)&dir[1] = DAT_803df414;
    *(int *)&dir[2] = DAT_803df418;
    if (tree != 0) {
        worm_tree_effect_build();
        *(float *)(tree + 200) = FLOAT_80616edc;
        *(float *)(tree + 0xe0) = FLOAT_80616ed8;
        *(char *)(tree + 0x32) = 2;
        *(int *)(tree + 0x1a0) = 1;
        *(float *)(tree + 0x1a4) = FLOAT_80616ed8;
        particle_emitter_spawn(0, (unsigned int)(size_t)&DAT_805c40cc, 0x1000,
                               (float *)(tree + 100), dir);
        entity_register_for_update(tree, 3, -0x7fe31e50);
    }
}

// 0x801ce410 - rocking-object oscillation update (a damaged structure swinging
// on water): integrate a decaying angular velocity via the sin LUT, play a
// creak sound past a swing threshold, clamp+bounce the swing, and unregister
// after 12 rocks.
void worm_rocking_object_update(int ent)
{
    if (DAT_806108e4 != 0) {
        *(float *)(ent + 0xc0) = *(float *)(ent + 0xc0) + FLOAT_806170e4;
        float dt = (FLOAT_80616ed0 / (float)(int)DAT_806109c0) * (float)(int)DAT_806108e4;
        *(float *)(ent + 0x1a4) = dt + *(float *)(ent + 0x1a4);
        unsigned int lut = (unsigned int)(int)(FLOAT_80616f64 *
            ((FLOAT_80616f30 * *(float *)(ent + 0xc0)) / FLOAT_80616f68) + FLOAT_80616ee0);
        *(float *)(ent + 0xe0) =
            FLOAT_80617098 * (&DAT_80583228)[lut & 0x7ff] + *(float *)(ent + 0xe0);
        *(float *)(ent + 0xc0) = *(float *)(ent + 0xe0) * dt + *(float *)(ent + 0xc0);
        if ((*(int *)(ent + 0x1a0) == 0) && (FLOAT_80616fcc < *(float *)(ent + 0xc0))) {
            sound_play_stereo_pair((double)FLOAT_80616f38, (double)FLOAT_80616f6c, (double)FLOAT_806170e8);
            *(int *)(ent + 0x1a0) = *(int *)(ent + 0x1a0) + 1;
            sound_set_voice_volume_scale(4);
        }
        if (FLOAT_806170e4 < *(float *)(ent + 0xc0)) {
            *(float *)(ent + 0xc0) = FLOAT_806170e4;
            *(float *)(ent + 0xe0) = FLOAT_80616edc * -*(float *)(ent + 0xe0);
            *(int *)(ent + 0x1a0) = *(int *)(ent + 0x1a0) + 1;
        }
        if (0xb < *(int *)(ent + 0x1a0)) {
            entity_register_for_update(ent, 3, 0);
        }
        *(float *)(ent + 0xc0) = *(float *)(ent + 0xc0) - FLOAT_806170e4;
        *(unsigned int *)(ent + 0x18) = *(unsigned int *)(ent + 0x18) | 2;
    }
}

// ===========================================================================
// batch_596: per-map scripted destruction/animation event hooks (0x801ce5e4-
// 0x801cef04). These are the update/event callbacks for the various breakable
// and animated set-piece props that share this worm/level effect module: a
// hinged "pane" object that shatters and slides, the DZ01-DZ10/TBL1/TBL2
// breakable debris group, the DB01 door, the PAN1-3 physics panels, the JBTH
// character's scripted anim events, and the falling-spark render for the TREE
// set-piece. All are driven from worm_master_update / worm_render_all /
// worm_level_init (batches 594-595) and FUN_801cf3c8 (next batch).
//
// Non-matching: same conventions as the rest of this file. anim_active_clip_slot
// returns an int the ROM dereferences as an int* (the active clip record), so it
// is reached through a file-local typed alias __wr_clip_slot; spawn_create_instance,
// anim_event_scan_active, subobj_island_pool_free, sprite_measure_text_region_int
// and mtx4_rotate_about_point are declared void / narrower in the shared headers
// than the ROM call sites, so they too use __wr_ typed aliases. The int<->double
// magic-bias idiom is reduced to (double)(int). Wide FP-context spill frames
// (FUN_803826e8/FUN_80382734) are kept as opaque open/close calls; caller-saved
// in_fNN/in_psNN_1 spill copies are dropped.
extern "C" {
// raw ROM symbols new to this batch
extern float FLOAT_806170ec;   // pane initial angular-velocity seed (+0xc0)
extern float FLOAT_806170f0;   // pane debris lateral offset
extern float FLOAT_806170f4;   // pane debris forward offset
extern float FLOAT_806170f8;   // pane slide "half-open" collision threshold
extern float FLOAT_806170fc;   // pane slide fully-open clamp
extern float FLOAT_80617100;   // tree-spark active-window depth limit (+0xc8)
extern float FLOAT_80617104;   // tree-spark instant-spark lifetime
extern float FLOAT_80616f8c;   // tree-spark instant-spark scale
extern float FLOAT_80616f88;   // tree-spark streak intensity scale
extern float FLOAT_80616ee0;   // +1.0 bias (shared)
// pane debris spawn direction vector (ROM const)
extern int DAT_803df41c; extern int DAT_803df420; extern int DAT_803df424;
// tree-spark screen-space quad params (ROM const)
extern int DAT_803df428; extern int DAT_803df42c; extern int DAT_803df430;
extern void *PTR_DAT_803df434; extern int DAT_803df438; extern int DAT_803df43c;
// breakable-object per-slot state table (12 words)
extern int DAT_805c4150;
// DAT_806108ec / DAT_806109c0 already declared above; DAT_806109c4/c8 fb dims too
extern int DAT_806108ec;   // frame-time numerator
extern int DAT_806109c0;   // frame-time denominator (redecl guard: already above)
// FUN_80253ae4: return the "already-broken" subobj/island handle for a pane
extern int FUN_80253ae4(void);
} // extern "C"

// active clip record: ROM reads anim_active_clip_slot()'s return as int*
static int *(*const __wr_clip_slot)(int) = (int *(*)(int)) &anim_active_clip_slot;
// spawn_create_instance: full ROM prototype (returns the new entity ptr)
static int (*const __wr_spawn_create_instance)(double, double, double, double, double, unsigned long long,
        unsigned long long, unsigned long long, int, int, int, int, void *, int *, int, int, int, int)
    = (int (*)(double, double, double, double, double, unsigned long long, unsigned long long,
        unsigned long long, int, int, int, int, void *, int *, int, int, int, int)) &spawn_create_instance;
// anim_event_scan_active: ROM calls it with no args and reads its int return
static int (*const __wr_anim_event_scan)(void) = (int (*)(void)) &anim_event_scan_active;
// subobj_island_pool_free: ROM uses its return as the break-tick receiver ptr
static int (*const __wr_island_free)(int) = (int (*)(int)) &subobj_island_pool_free;
// mtx4_rotate_about_point: ROM (dest matrix, euler angles, pivot point)
static void (*const __wr_rotate_about_point)(int, void *, float *)
    = (void (*)(int, void *, float *)) &mtx4_rotate_about_point;
// sprite_measure_text_region_int: 5-arg ROM call site
static void (*const __wr_measure_region)(void *, void *, int *, void *, int)
    = (void (*)(void *, void *, int *, void *, int)) &sprite_measure_text_region_int;

// 0x801ce5e4 - pane set-piece init: find the 'PART' entity, seed its angular
// state, mark it, and register it for the pane shatter/slide update callback.
void worm_pane_effect_init(void)
{
    int ent = FUN_8023c32c(0x54524150);   // 'PART'
    if (ent != 0) {
        *(float *)(ent + 0xc0) = FLOAT_806170ec;
        *(float *)(ent + 0xe0) = FLOAT_80616ed8;
        *(char *)(ent + 0x32) = 2;
        *(int *)(ent + 0x1a0) = 0;
        entity_register_for_update(ent, 3, -0x7fe31bf0);   // &worm_pane_shatter_spawn
    }
}

// 0x801ce644 - pane shatter: on state 4 (shatter trigger) build the pane's world
// matrix, pick a fragment origin (fixed for clip 0x186, else a jittered scatter),
// transform it to world, and spawn a physics-debris instance (8 or 0x10 pieces by
// whether the linked object is 'HLTH'); otherwise normalise the state field.
void worm_pane_shatter_spawn(int ent)
{
    int state = *(int *)(ent + 0x1a0);
    if (state == 4) {
        float pos[17];
        entity_build_local_matrix(pos, (void *)ent, 0, 0);
        pos[0] = FLOAT_80616ed8;
        pos[1] = FLOAT_80616ed8;
        pos[2] = FLOAT_80616ed8;
        if (*(short *)(ent + 0x94) == 0x186) {
            pos[1] = FLOAT_80616ed8 + FLOAT_80616ee0;
        } else {
            unsigned int r = __wr_rng_tick();
            int sign = (int)((r & 1) * 2 - 1);
            pos[1] = pos[1] + FLOAT_806170f0;
            pos[0] = (float)((double)pos[0] + (double)FLOAT_806170f4);
            pos[2] = FLOAT_80616ed4 * (float)(-sign) + pos[2];
        }
        mtx4_transform_vector(pos, pos);
        pos[0] = (float)((double)pos[0] + (double)(*(float *)(ent + 100) + *(float *)(ent + 0x1a8)));
        pos[1] = pos[1] + *(float *)(ent + 0x68);
        pos[2] = (float)((double)pos[2] + (double)(*(float *)(ent + 0x6c) + *(float *)(ent + 0x1b0)));
        int count;
        if (*(int *)(*(int *)(ent + 0x2d8) + 4) == 0x484c5448) {   // 'HLTH'
            count = 8;
        } else {
            count = 0x10;
        }
        int *def = spawn_def_get_entry(3);
        int inst = __wr_spawn_create_instance(0.0, 0.0, 0.0, 0.0, 0.0, 0, 0, 0,
                                              0, count, 5, 0, pos, def, 0, 0, 0, 0);
        *(unsigned int *)(inst + 0x28) = *(unsigned int *)(inst + 0x28) & 0xfffffffd;
        *(int *)(ent + 0x7c) = inst;
        *(int *)(inst + 0x70) = *(int *)(ent + 0x70);
        *(int *)(ent + 0x1a0) = 0;
        return;
    }
    if ((3 < state) || (state < 2)) {
        *(int *)(ent + 0x1a0) = 2;
        return;
    }
    *(int *)(ent + 0x1a0) = 0;
}

// 0x801ce800 - pane slide state machine: raise/lower the pane's slide amount
// (+0x1a4) by a frame-scaled step, toggle the linked debris' collision bit at the
// half-open threshold, and clamp/advance at the open and closed limits. Drops the
// broken debris ref once it despawns. Marks the pane dirty each frame.
void worm_pane_slide_update(int ent)
{
    if ((*(int *)(ent + 0x7c) != 0) &&
        ((*(unsigned int *)(*(int *)(ent + 0x7c) + 0x28) & 0x80000000) != 0)) {
        *(int *)(ent + 0x7c) = 0;
    }
    int state = *(int *)(ent + 0x1a0);
    float step = (FLOAT_80616ed0 / (float)(int)DAT_806109c0) * (float)(int)DAT_806108e4;
    if (state == 2) {
        *(float *)(ent + 0x1a4) = -(step - *(float *)(ent + 0x1a4));
        int debris = *(int *)(ent + 0x7c);
        if ((debris != 0) && (*(float *)(ent + 0x1a4) <= FLOAT_806170f8)) {
            *(unsigned int *)(debris + 0x28) = *(unsigned int *)(debris + 0x28) & 0xfffffffd;
        }
        if (*(float *)(ent + 0x1a4) <= FLOAT_80616ed8) {
            *(float *)(ent + 0x1a4) = FLOAT_80616ed8;
            *(int *)(ent + 0x1a0) = 3;
        }
    } else if (state < 2) {
        if (state == 0) {
            *(float *)(ent + 0x1a4) = step + *(float *)(ent + 0x1a4);
            int debris = *(int *)(ent + 0x7c);
            if ((debris != 0) && (FLOAT_806170f8 <= *(float *)(ent + 0x1a4))) {
                *(unsigned int *)(debris + 0x28) = *(unsigned int *)(debris + 0x28) | 2;
            }
            if (FLOAT_806170fc <= *(float *)(ent + 0x1a4)) {
                *(float *)(ent + 0x1a4) = FLOAT_806170fc;
                *(int *)(ent + 0x1a0) = 1;
            }
        }
    } else if (state < 4) {
        return;
    }
    *(unsigned int *)(ent + 0x18) = *(unsigned int *)(ent + 0x18) | 2;
}

// 0x801ce994 - pane hinge matrix update: reset the two node base matrices and
// build the two hinge-rotation matrices about the pane's edge pivot points; clip
// 0x186 swaps which pivot each half rotates about. Marks the node matrices dirty.
void worm_pane_hinge_matrix_update(int ent)
{
    float euler[3];
    euler[0] = *(float *)&DAT_803df41c;
    *(int *)&euler[1] = DAT_803df420;
    *(int *)&euler[2] = DAT_803df424;
    int node = *(int *)(*(int *)(ent + 4) + 0x10);
    int *pivotTbl = *(int **)(*(int *)(ent + 4) + 4);
    int pivotBase = pivotTbl[*pivotTbl * -0x30 + 0x4b];
    mtx4_set_identity(node);
    mtx4_set_identity(node + 0x40);
    if (*(short *)(ent + 0x94) == 0x186) {
        __wr_rotate_about_point(node + 0x80, euler, (float *)(pivotBase + 0x18));
        __wr_rotate_about_point(node + 0xc0, euler, (float *)pivotBase);
    } else {
        __wr_rotate_about_point(node + 0x80, euler, (float *)pivotBase);
        __wr_rotate_about_point(node + 0xc0, euler, (float *)(pivotBase + 0x18));
    }
    *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x200000;
}

// 0x801ceaf0 - reset the 12-slot breakable-object tracking table.
void worm_breakable_state_reset(void)
{
    int *tbl = &DAT_805c4150;
    for (int i = 0; i < 12; i++) {
        tbl[i] = 0;
    }
}

// 0x801ceb18 - breakable debris group update: for each of the 12 tracked objects
// (DZ01-DZ10, TBL1, TBL2) resolve the entity, note whether it exists this frame,
// and by its active clip id toggle its collision bit (0x15a clears, 0x159/0x15b
// set). Slots whose entity has vanished transition 1->2 (broken/gone).
void worm_breakable_debris_update(void)
{
    int ids[12];
    ids[0]  = FUN_8023c32c(0x445a3031);   // 'DZ01'
    ids[1]  = FUN_8023c32c(0x445a3032);   // 'DZ02'
    ids[2]  = FUN_8023c32c(0x445a3033);   // 'DZ03'
    ids[3]  = FUN_8023c32c(0x445a3034);   // 'DZ04'
    ids[4]  = FUN_8023c32c(0x445a3035);   // 'DZ05'
    ids[5]  = FUN_8023c32c(0x445a3036);   // 'DZ06'
    ids[6]  = FUN_8023c32c(0x445a3037);   // 'DZ07'
    ids[7]  = FUN_8023c32c(0x445a3038);   // 'DZ08'
    ids[8]  = FUN_8023c32c(0x445a3039);   // 'DZ09'
    ids[9]  = FUN_8023c32c(0x445a3130);   // 'DZ10'
    ids[10] = FUN_8023c32c(0x54424c31);   // 'TBL1'
    ids[11] = FUN_8023c32c(0x54424c32);   // 'TBL2'
    int *state = &DAT_805c4150;
    for (int i = 0; i < 12; i++) {
        int ent = ids[i];
        if (ent == 0) {
            if (state[i] == 1) {
                state[i] = 2;
            }
        } else {
            int *clip = __wr_clip_slot(ent);
            state[i] = 1;
            int clipId = *clip;
            if (clipId == 0x15a) {
                *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) & 0xfffffffe;
            } else if ((clipId == 0x159) || (clipId == 0x15b)) {
                *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 1;
            }
        }
    }
}

// 0x801cec9c - DB01 door update: while its open/close clip (0x15c/0x15d) plays
// clear the door's render-cull bit; otherwise set it (hidden).
void worm_db01_door_update(void)
{
    int ent = FUN_8023c32c(0x44423031);   // 'DB01'
    if (ent != 0) {
        int *clip = __wr_clip_slot(ent);
        if ((*clip == 0x15c) || (*clip == 0x15d)) {
            *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) & 0xffffffef;
        } else {
            *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x10;
        }
    }
}

// 0x801ced08 - PAN1/2/3 panel break dispatch: map the linked object's tag
// (PAN1/2/3 -> pan1/2/3) to its physics island, and hand the pane either its
// already-broken island state (aged out, or event 0x17) or a fresh break state
// to subobj_break_tick_impl. r3 (the pane record) is the explicit param.
void worm_pane_break_dispatch(int pane)
{
    int tag = *(int *)(*(int *)pane + 0x60);
    int mapped;
    if (tag == 0x50414e32) {          // 'PAN2'
        mapped = 0x70616e32;          // 'pan2'
    } else if (tag < 0x50414e32) {
        if (tag < 0x50414e31) {       // < 'PAN1'
            return;
        }
        mapped = 0x70616e31;          // 'pan1'
    } else {
        if (0x50414e33 < tag) {       // > 'PAN3'
            return;
        }
        mapped = 0x70616e33;          // 'pan3'
    }
    int recv = pane;
    int island = subobj_island_find_by_body_b(mapped);
    if (-1 < island) {
        if ((*(int *)(pane + 0x24) == 0x17) || (FLOAT_80616f6c < *(float *)(pane + 0x20))) {
            recv = __wr_island_free(island);
        } else {
            recv = FUN_80253ae4();
        }
    }
    subobj_break_tick_impl((unsigned long long)(unsigned int)recv, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0);
}

// 0x801ceddc - JBTH character scripted anim-event hook: while the JBTH actor's
// active clip is on an event frame, drive its linked object (+0x7c) into the
// matching scripted state (anim id -> state field +0x94 / flags), and fire a
// one-shot sound for the two "attack" clips (0x13f, 400).
void worm_jbth_anim_event(void)
{
    int actor = aiactor_find_player_by_team(0x4a425448);   // 'JBTH'
    if (actor != 0) {
        int *clip = __wr_clip_slot(actor);
        if (__wr_anim_event_scan() != 0) {
            int obj;
            if (*clip == 0x17f) {
                obj = *(int *)(actor + 0x7c);
                *(int *)(obj + 0x94) = 0x3e;
                *(char *)(obj + 0x54c) = 0;
            }
            if (*clip == 0x187) {
                obj = *(int *)(actor + 0x7c);
                *(char *)(obj + 0x58a) = 0;
                *(int *)(obj + 0x94) = 0;
            }
            if (*clip == 0x13f) {
                obj = *(int *)(actor + 0x7c);
                *(int *)(obj + 0x94) = 0x35;
                *(char *)(obj + 0x54c) = 0;
                object_sound_slot_accumulate(obj, 9, 1000);
            }
            if (*clip == 0x140) {
                obj = *(int *)(actor + 0x7c);
                *(char *)(obj + 0x581) = 0;
                *(int *)(obj + 0x94) = 0;
            }
            if (*clip == 0x191) {
                obj = *(int *)(actor + 0x7c);
                *(char *)(obj + 0x581) = 0;
                *(int *)(obj + 0x94) = 0;
            }
            if (*clip == 400) {
                obj = *(int *)(actor + 0x7c);
                *(int *)(obj + 0x94) = 0x35;
                *(char *)(obj + 0x54c) = 0;
                object_sound_slot_accumulate(obj, 9, 1000);
            }
        }
    }
}

// 0x801cef04 - TREE set-piece falling-spark render: while the tree is active and
// within its break window, fade a spark intensity by the squared break progress,
// (optionally emit a one-shot instant spark), then draw the falling-spark streak
// as two GX sprite strips plus a capping textured triangle, over the screen-space
// quad defined by DAT_803df428.. and the framebuffer dimensions.
void worm_tree_spark_render(void)
{
    FUN_803826e8();   // wide FP-context spill frame open (opaque)
    int tree = FUN_8023c32c(0x54524545);   // 'TREE'
    if ((tree != 0) && (*(int *)(tree + 0x1a0) != 0) && (*(float *)(tree + 200) < FLOAT_80617100)) {
        float prog = *(float *)(tree + 200) / FLOAT_80617100;
        double fade = (double)(prog * prog);
        int fbW = (int)DAT_806109c4;
        unsigned int halfW = (unsigned int)((fbW >> 1) + (unsigned int)((fbW < 0) && ((fbW & 1) != 0)));
        int quadX  = DAT_803df428;
        int quadY  = DAT_803df42c;
        int quadZ  = DAT_803df430;
        void *quadPtr = PTR_DAT_803df434;
        int quadA  = DAT_803df438;
        int quadB  = DAT_803df43c;
        unsigned int fbH = (unsigned int)DAT_806109c8;
        (void)quadX; (void)quadY; (void)quadZ; (void)quadA; (void)quadB;

        if ((double)FLOAT_80616ed8 <= fade) {
            fade = (double)(float)((double)FLOAT_80616ed0 - fade);
            spark_spawn_instant();
            spark_set_last_scale(FLOAT_80616f8c);
            spark_set_last_lifetime(FLOAT_80617104);
        }
        unsigned int intensity = (unsigned int)((double)FLOAT_80616f88 * fade);
        unsigned int color = intensity << 0x18 | 0x7f3030;

        __wr_measure_region(&quadPtr, &halfW, (int *)&fbH, (void *)0, 1);
        int savedMat  = gx_set_sub_material_id(2);
        int savedBlend = gx_set_blend_mode(0);
        int tex = sprite_get_texture_id(0);
        gx_set_active_material(tex);

        double zero = (double)FLOAT_80616ed8;
        gx_draw_sprite_strip4(
            0, (double)FLOAT_80616f98, zero, zero, (double)(int)halfW, zero, zero,
            (double)(int)fbH, (double)(int)fbW,
            0, 0x10, 0, 0, 0x10, 0x10, 0x10,
            (float)(int)halfW, 0, color, 0, color);
        gx_draw_sprite_strip4(
            0, (double)FLOAT_80616f98, (double)(int)halfW, (double)FLOAT_80616ed8,
            (double)(int)fbW, (double)FLOAT_80616ed8, (double)(int)fbH,
            (double)(int)halfW, (double)(int)fbW,
            0, 0x10, 0, 0, 0x10, 0x10, 0x10,
            (float)(int)fbH, color, 0, color, 0);
        gx_draw_tri_textured(0, DAT_806109c8, halfW, fbH, DAT_806109c4, DAT_806109c8,
                             0, 0, 0x10, 0x10, 0, 0, 0, intensity & 0xff | 0x30307f00, 0);

        gx_set_sub_material_id(savedMat);
        gx_set_blend_mode(savedBlend);
    }
    FUN_80382734();   // wide FP-context spill frame close (opaque)
}

// ===========================================================================
// batch_596: per-map scripted destruction/animation event hooks (0x801ce5e4-
// 0x801cef04). Update/event callbacks for the breakable and animated set-piece
// props that share this worm/level effect module: a hinged "pane" object that
// shatters and slides, the DZ01-DZ10/TBL1/TBL2 breakable debris group, the DB01
// door, the PAN1-3 physics panels, the JBTH character's scripted anim events,
// and the falling-spark render for the TREE set-piece. Driven from
// worm_master_update / worm_render_all / worm_level_init and FUN_801cf3c8.
//
// Non-matching: anim_active_clip_slot returns an int the ROM dereferences as an
// int* (active clip record) -> file-local typed alias __wr_clip_slot;
// spawn_create_instance, anim_event_scan_active, subobj_island_pool_free,
// sprite_measure_text_region_int and mtx4_rotate_about_point are declared
// void/narrower in the shared headers than the ROM call sites -> __wr_ typed
// aliases. int<->double magic-bias reduced to (double)(int). Wide FP-context
// spill frames (FUN_803826e8/FUN_80382734) kept as opaque open/close; caller-
// saved in_fNN/in_psNN_1 spill copies dropped.
extern "C" {
extern float FLOAT_806170ec;   // pane initial angular-velocity seed (+0xc0)
extern float FLOAT_806170f0;   // pane debris lateral offset
extern float FLOAT_806170f4;   // pane debris forward offset
extern float FLOAT_806170f8;   // pane slide "half-open" collision threshold
extern float FLOAT_806170fc;   // pane slide fully-open clamp
extern float FLOAT_80617100;   // tree-spark active-window depth limit (+0xc8)
extern float FLOAT_80617104;   // tree-spark instant-spark lifetime
extern float FLOAT_80616f8c;   // tree-spark instant-spark scale
extern float FLOAT_80616f88;   // tree-spark streak intensity scale
extern int DAT_803df41c; extern int DAT_803df420; extern int DAT_803df424;   // pane debris dir
extern int DAT_803df428; extern int DAT_803df42c; extern int DAT_803df430;   // tree-spark quad
extern void *PTR_DAT_803df434; extern int DAT_803df438; extern int DAT_803df43c;
extern int DAT_805c4150;    // breakable-object per-slot state table (12 words)
extern int DAT_806108ec;    // frame-time numerator
extern int FUN_80253ae4(void);   // return the already-broken pane island handle
} // extern "C"

static int *(*const __wr_clip_slot)(int) = (int *(*)(int)) &anim_active_clip_slot;
static int (*const __wr_spawn_create_instance)(double, double, double, double, double, unsigned long long,
        unsigned long long, unsigned long long, int, int, int, int, void *, int *, int, int, int, int)
    = (int (*)(double, double, double, double, double, unsigned long long, unsigned long long,
        unsigned long long, int, int, int, int, void *, int *, int, int, int, int)) &spawn_create_instance;
static int (*const __wr_anim_event_scan)(void) = (int (*)(void)) &anim_event_scan_active;
static int (*const __wr_island_free)(int) = (int (*)(int)) &subobj_island_pool_free;
static void (*const __wr_rotate_about_point)(int, void *, float *)
    = (void (*)(int, void *, float *)) &mtx4_rotate_about_point;
static void (*const __wr_measure_region)(void *, void *, int *, void *, int)
    = (void (*)(void *, void *, int *, void *, int)) &sprite_measure_text_region_int;

// 0x801ce5e4 - pane set-piece init: find the 'PART' entity, seed its angular
// state, mark it, and register worm_pane_shatter_spawn as its update callback.
void worm_pane_effect_init(void)
{
    int ent = FUN_8023c32c(0x54524150);   // 'PART'
    if (ent != 0) {
        *(float *)(ent + 0xc0) = FLOAT_806170ec;
        *(float *)(ent + 0xe0) = FLOAT_80616ed8;
        *(char *)(ent + 0x32) = 2;
        *(int *)(ent + 0x1a0) = 0;
        entity_register_for_update(ent, 3, -0x7fe31bf0);
    }
}

// 0x801ce644 - pane shatter: on state 4 build the pane's world matrix, pick a
// fragment origin (fixed for clip 0x186, else jittered scatter), transform it to
// world, and spawn a physics-debris instance (8 or 0x10 pieces by whether the
// linked object is 'HLTH'); otherwise normalise the state field.
void worm_pane_shatter_spawn(int ent)
{
    int state = *(int *)(ent + 0x1a0);
    if (state == 4) {
        float pos[17];
        entity_build_local_matrix(pos, (void *)ent, 0, 0);
        pos[0] = FLOAT_80616ed8;
        pos[1] = FLOAT_80616ed8;
        pos[2] = FLOAT_80616ed8;
        if (*(short *)(ent + 0x94) == 0x186) {
            pos[1] = FLOAT_80616ed8 + FLOAT_80616ee0;
        } else {
            unsigned int r = __wr_rng_tick();
            int sign = (int)((r & 1) * 2 - 1);
            pos[1] = pos[1] + FLOAT_806170f0;
            pos[0] = (float)((double)pos[0] + (double)FLOAT_806170f4);
            pos[2] = FLOAT_80616ed4 * (float)(-sign) + pos[2];
        }
        mtx4_transform_vector(pos, pos);
        pos[0] = (float)((double)pos[0] + (double)(*(float *)(ent + 100) + *(float *)(ent + 0x1a8)));
        pos[1] = pos[1] + *(float *)(ent + 0x68);
        pos[2] = (float)((double)pos[2] + (double)(*(float *)(ent + 0x6c) + *(float *)(ent + 0x1b0)));
        int count;
        if (*(int *)(*(int *)(ent + 0x2d8) + 4) == 0x484c5448) {   // 'HLTH'
            count = 8;
        } else {
            count = 0x10;
        }
        int *def = spawn_def_get_entry(3);
        int inst = __wr_spawn_create_instance(0.0, 0.0, 0.0, 0.0, 0.0, 0, 0, 0,
                                              0, count, 5, 0, pos, def, 0, 0, 0, 0);
        *(unsigned int *)(inst + 0x28) = *(unsigned int *)(inst + 0x28) & 0xfffffffd;
        *(int *)(ent + 0x7c) = inst;
        *(int *)(inst + 0x70) = *(int *)(ent + 0x70);
        *(int *)(ent + 0x1a0) = 0;
        return;
    }
    if ((3 < state) || (state < 2)) {
        *(int *)(ent + 0x1a0) = 2;
        return;
    }
    *(int *)(ent + 0x1a0) = 0;
}

// 0x801ce800 - pane slide state machine: raise/lower the pane's slide amount
// (+0x1a4) by a frame-scaled step, toggle the linked debris' collision bit at the
// half-open threshold, and clamp/advance at the open and closed limits. Drops the
// broken debris ref once it despawns. Marks the pane dirty each frame.
void worm_pane_slide_update(int ent)
{
    if ((*(int *)(ent + 0x7c) != 0) &&
        ((*(unsigned int *)(*(int *)(ent + 0x7c) + 0x28) & 0x80000000) != 0)) {
        *(int *)(ent + 0x7c) = 0;
    }
    int state = *(int *)(ent + 0x1a0);
    float step = (FLOAT_80616ed0 / (float)(int)DAT_806109c0) * (float)(int)DAT_806108e4;
    if (state == 2) {
        *(float *)(ent + 0x1a4) = -(step - *(float *)(ent + 0x1a4));
        int debris = *(int *)(ent + 0x7c);
        if ((debris != 0) && (*(float *)(ent + 0x1a4) <= FLOAT_806170f8)) {
            *(unsigned int *)(debris + 0x28) = *(unsigned int *)(debris + 0x28) & 0xfffffffd;
        }
        if (*(float *)(ent + 0x1a4) <= FLOAT_80616ed8) {
            *(float *)(ent + 0x1a4) = FLOAT_80616ed8;
            *(int *)(ent + 0x1a0) = 3;
        }
    } else if (state < 2) {
        if (state == 0) {
            *(float *)(ent + 0x1a4) = step + *(float *)(ent + 0x1a4);
            int debris = *(int *)(ent + 0x7c);
            if ((debris != 0) && (FLOAT_806170f8 <= *(float *)(ent + 0x1a4))) {
                *(unsigned int *)(debris + 0x28) = *(unsigned int *)(debris + 0x28) | 2;
            }
            if (FLOAT_806170fc <= *(float *)(ent + 0x1a4)) {
                *(float *)(ent + 0x1a4) = FLOAT_806170fc;
                *(int *)(ent + 0x1a0) = 1;
            }
        }
    } else if (state < 4) {
        return;
    }
    *(unsigned int *)(ent + 0x18) = *(unsigned int *)(ent + 0x18) | 2;
}

// 0x801ce994 - pane hinge matrix update: reset the two node base matrices and
// build the two hinge-rotation matrices about the pane's edge pivot points; clip
// 0x186 swaps which pivot each half rotates about. Marks the node matrices dirty.
void worm_pane_hinge_matrix_update(int ent)
{
    float euler[3];
    euler[0] = *(float *)&DAT_803df41c;
    *(int *)&euler[1] = DAT_803df420;
    *(int *)&euler[2] = DAT_803df424;
    int node = *(int *)(*(int *)(ent + 4) + 0x10);
    int *pivotTbl = *(int **)(*(int *)(ent + 4) + 4);
    int pivotBase = pivotTbl[*pivotTbl * -0x30 + 0x4b];
    mtx4_set_identity(node);
    mtx4_set_identity(node + 0x40);
    if (*(short *)(ent + 0x94) == 0x186) {
        __wr_rotate_about_point(node + 0x80, euler, (float *)(pivotBase + 0x18));
        __wr_rotate_about_point(node + 0xc0, euler, (float *)pivotBase);
    } else {
        __wr_rotate_about_point(node + 0x80, euler, (float *)pivotBase);
        __wr_rotate_about_point(node + 0xc0, euler, (float *)(pivotBase + 0x18));
    }
    *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x200000;
}

// 0x801ceaf0 - reset the 12-slot breakable-object tracking table.
void worm_breakable_state_reset(void)
{
    int *tbl = &DAT_805c4150;
    for (int i = 0; i < 12; i++) {
        tbl[i] = 0;
    }
}

// 0x801ceb18 - breakable debris group update: for each of the 12 tracked objects
// (DZ01-DZ10, TBL1, TBL2) resolve the entity, note whether it exists this frame,
// and by its active clip id toggle its collision bit (0x15a clears, 0x159/0x15b
// set). Slots whose entity has vanished transition 1->2 (broken/gone).
void worm_breakable_debris_update(void)
{
    int ids[12];
    ids[0]  = FUN_8023c32c(0x445a3031);   // 'DZ01'
    ids[1]  = FUN_8023c32c(0x445a3032);   // 'DZ02'
    ids[2]  = FUN_8023c32c(0x445a3033);   // 'DZ03'
    ids[3]  = FUN_8023c32c(0x445a3034);   // 'DZ04'
    ids[4]  = FUN_8023c32c(0x445a3035);   // 'DZ05'
    ids[5]  = FUN_8023c32c(0x445a3036);   // 'DZ06'
    ids[6]  = FUN_8023c32c(0x445a3037);   // 'DZ07'
    ids[7]  = FUN_8023c32c(0x445a3038);   // 'DZ08'
    ids[8]  = FUN_8023c32c(0x445a3039);   // 'DZ09'
    ids[9]  = FUN_8023c32c(0x445a3130);   // 'DZ10'
    ids[10] = FUN_8023c32c(0x54424c31);   // 'TBL1'
    ids[11] = FUN_8023c32c(0x54424c32);   // 'TBL2'
    int *state = &DAT_805c4150;
    for (int i = 0; i < 12; i++) {
        int ent = ids[i];
        if (ent == 0) {
            if (state[i] == 1) {
                state[i] = 2;
            }
        } else {
            int *clip = __wr_clip_slot(ent);
            state[i] = 1;
            int clipId = *clip;
            if (clipId == 0x15a) {
                *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) & 0xfffffffe;
            } else if ((clipId == 0x159) || (clipId == 0x15b)) {
                *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 1;
            }
        }
    }
}

// 0x801cec9c - DB01 door update: while its open/close clip (0x15c/0x15d) plays
// clear the door's render-cull bit; otherwise set it (hidden).
void worm_db01_door_update(void)
{
    int ent = FUN_8023c32c(0x44423031);   // 'DB01'
    if (ent != 0) {
        int *clip = __wr_clip_slot(ent);
        if ((*clip == 0x15c) || (*clip == 0x15d)) {
            *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) & 0xffffffef;
        } else {
            *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x10;
        }
    }
}

// 0x801ced08 - PAN1/2/3 panel break dispatch: map the linked object's tag
// (PAN1/2/3 -> pan1/2/3) to its physics island, and hand the pane either its
// already-broken island state (aged out, or event 0x17) or a fresh break state
// to subobj_break_tick_impl. The pane record (r3) is the explicit param.
void worm_pane_break_dispatch(int pane)
{
    int tag = *(int *)(*(int *)pane + 0x60);
    int mapped;
    if (tag == 0x50414e32) {          // 'PAN2'
        mapped = 0x70616e32;          // 'pan2'
    } else if (tag < 0x50414e32) {
        if (tag < 0x50414e31) {       // < 'PAN1'
            return;
        }
        mapped = 0x70616e31;          // 'pan1'
    } else {
        if (0x50414e33 < tag) {       // > 'PAN3'
            return;
        }
        mapped = 0x70616e33;          // 'pan3'
    }
    int recv = pane;
    int island = subobj_island_find_by_body_b(mapped);
    if (-1 < island) {
        if ((*(int *)(pane + 0x24) == 0x17) || (FLOAT_80616f6c < *(float *)(pane + 0x20))) {
            recv = __wr_island_free(island);
        } else {
            recv = FUN_80253ae4();
        }
    }
    subobj_break_tick_impl((unsigned long long)(unsigned int)recv, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0);
}

// 0x801ceddc - JBTH character scripted anim-event hook: while the JBTH actor's
// active clip is on an event frame, drive its linked object (+0x7c) into the
// matching scripted state (anim id -> state field +0x94 / flags), and fire a
// one-shot sound for the two "attack" clips (0x13f, 400).
void worm_jbth_anim_event(void)
{
    int actor = aiactor_find_player_by_team(0x4a425448);   // 'JBTH'
    if (actor != 0) {
        int *clip = __wr_clip_slot(actor);
        if (__wr_anim_event_scan() != 0) {
            int obj;
            if (*clip == 0x17f) {
                obj = *(int *)(actor + 0x7c);
                *(int *)(obj + 0x94) = 0x3e;
                *(char *)(obj + 0x54c) = 0;
            }
            if (*clip == 0x187) {
                obj = *(int *)(actor + 0x7c);
                *(char *)(obj + 0x58a) = 0;
                *(int *)(obj + 0x94) = 0;
            }
            if (*clip == 0x13f) {
                obj = *(int *)(actor + 0x7c);
                *(int *)(obj + 0x94) = 0x35;
                *(char *)(obj + 0x54c) = 0;
                object_sound_slot_accumulate(obj, 9, 1000);
            }
            if (*clip == 0x140) {
                obj = *(int *)(actor + 0x7c);
                *(char *)(obj + 0x581) = 0;
                *(int *)(obj + 0x94) = 0;
            }
            if (*clip == 0x191) {
                obj = *(int *)(actor + 0x7c);
                *(char *)(obj + 0x581) = 0;
                *(int *)(obj + 0x94) = 0;
            }
            if (*clip == 400) {
                obj = *(int *)(actor + 0x7c);
                *(int *)(obj + 0x94) = 0x35;
                *(char *)(obj + 0x54c) = 0;
                object_sound_slot_accumulate(obj, 9, 1000);
            }
        }
    }
}

// 0x801cef04 - TREE set-piece falling-spark render: while the tree is active and
// within its break window, fade a spark intensity by the squared break progress,
// optionally emit a one-shot instant spark, then draw the falling-spark streak as
// two GX sprite strips plus a capping textured triangle over the screen-space quad
// (DAT_803df428..) and the framebuffer dimensions.
void worm_tree_spark_render(void)
{
    FUN_803826e8();   // wide FP-context spill frame open (opaque)
    int tree = FUN_8023c32c(0x54524545);   // 'TREE'
    if ((tree != 0) && (*(int *)(tree + 0x1a0) != 0) && (*(float *)(tree + 200) < FLOAT_80617100)) {
        float prog = *(float *)(tree + 200) / FLOAT_80617100;
        double fade = (double)(prog * prog);
        int fbW = (int)DAT_806109c4;
        unsigned int halfW = (unsigned int)((fbW >> 1) + (unsigned int)((fbW < 0) && ((fbW & 1) != 0)));
        void *quadPtr = PTR_DAT_803df434;
        unsigned int fbH = (unsigned int)DAT_806109c8;

        if ((double)FLOAT_80616ed8 <= fade) {
            fade = (double)(float)((double)FLOAT_80616ed0 - fade);
            spark_spawn_instant();
            spark_set_last_scale(FLOAT_80616f8c);
            spark_set_last_lifetime(FLOAT_80617104);
        }
        unsigned int intensity = (unsigned int)((double)FLOAT_80616f88 * fade);
        unsigned int color = intensity << 0x18 | 0x7f3030;

        __wr_measure_region(&quadPtr, &halfW, (int *)&fbH, (void *)0, 1);
        int savedMat  = gx_set_sub_material_id(2);
        int savedBlend = gx_set_blend_mode(0);
        int tex = sprite_get_texture_id(0);
        gx_set_active_material(tex);

        double zero = (double)FLOAT_80616ed8;
        gx_draw_sprite_strip4(
            0, (double)FLOAT_80616f98, zero, zero, (double)(int)halfW, zero, zero,
            (double)(int)fbH, (double)(int)fbW,
            0, 0x10, 0, 0, 0x10, 0x10, 0x10,
            (float)(int)halfW, 0, color, 0, color);
        gx_draw_sprite_strip4(
            0, (double)FLOAT_80616f98, (double)(int)halfW, (double)FLOAT_80616ed8,
            (double)(int)fbW, (double)FLOAT_80616ed8, (double)(int)fbH,
            (double)(int)halfW, (double)(int)fbW,
            0, 0x10, 0, 0, 0x10, 0x10, 0x10,
            (float)(int)fbH, color, 0, color, 0);
        gx_draw_tri_textured(0, DAT_806109c8, halfW, fbH, DAT_806109c4, DAT_806109c8,
                             0, 0, 0x10, 0x10, 0, 0, 0, intensity & 0xff | 0x30307f00, 0);

        gx_set_sub_material_id(savedMat);
        gx_set_blend_mode(savedBlend);
    }
    FUN_80382734();   // wide FP-context spill frame close (opaque)
}
// typed aliases for ROM call sites whose shared-header prototype differs
//   player_ai_projectile_spawn_simple: ROM (double,double,int,float*,int*,int,int*,int,int,int,int,int)
static void (*const __wr_proj_spawn)(double,double,int,float*,int*,int,int*,int,int,int,int,int)
    = (void(*)(double,double,int,float*,int*,int,int*,int,int,int,int,int)) &player_ai_projectile_spawn_simple;
//   particle_spawn_global: ROM (double,double,double,int,int,float*,float*)
static void (*const __wr_particle_spawn_global)(double,double,double,int,int,float*,float*)
    = (void(*)(double,double,double,int,int,float*,float*)) &particle_spawn_global;

// forward decls for the two attack helpers (defined below, called by the flock tick)
int  worm_bird_dive_raycast(int flock);
void worm_bird_dropping_spawn(void);    sound_play_request(sp);
    __wr_proj_spawn((double)FLOAT_806171ac, (double)FLOAT_80617194, 4, launch,
                    (int *)vel, bird, (int *)0, 6, 0, *(int *)(bird + 0x34), 0, 2);
    FUN_8038273c();  // spill close
}        __wr_particle_spawn_global((double)FLOAT_806171ac, (double)FLOAT_80617288, (double)FLOAT_806172c0,
                                   1, *(int *)(self + 0x34), pos, dir);    if (*(int *)(obj + 0xa0) != 7) {
        mtx4_set_identity(*(int *)(self[1] + 0x10));
        __wr_mtx_set_translation(
            (double)(FLOAT_80617198 * (*(float *)(obj + 0x50) + *(float *)(obj + 0x54))),
            (double)FLOAT_80617194, (double)FLOAT_80617194);
        mtx4_set_identity(*(int *)(self[1] + 0x10) + 0x80);
    }        entity_register_for_update((int)self, 3, 0);
        entity_destroy_with_event((int *)self);
    } else {
        *(float *)(self + 0x1a) = -(*(float *)(self + 0x2e) * FLOAT_806108ec - *(float *)(self + 0x1a));
// ===========================================================================
// batch_603: worm-effect scripted set-piece master spawn dispatcher + the
// swaying / hinged-break / DDED-physics prop update & render callbacks it
// registers. Address range 0x801d69a4 - 0x801d89e7. APPEND, continues
// batch_602. FUN_801d7ca4 (worm_scripted_prop_on_spawn) is the master on-spawn
// handler: keyed on entity subtype (+0x94) it wires up every prop's update /
// matrix / render / break callbacks.
//
// NON-MATCHING: the wide FP-context spill frames (FUN_803826e8/803826b4/
// 803826f0 open, FUN_80382734/80382700/8038273c close) are kept as opaque
// open/close brackets; the frame openers that surface the entity pointer are
// read via typed wrappers. The 0x43300000 int<->double magic-bias idiom is
// reduced to plain (float)(int) / (double)(int) math. rng_counter_tick routes
// through the existing __wr_rng_tick alias. sin-LUT reads index DAT_80583228.
// ---------------------------------------------------------------------------
extern "C" {
// Spill-frame openers that return the active entity pointer (batch_603 sites).
extern int  FUN_803826e8(void);   // spill open: returns entity ptr
extern void FUN_80382734(void);   // matching spill close
extern int  FUN_803826b4(void);   // spill open (declared above too; benign)
extern void FUN_80382700(void);   // matching spill close

// Zone-object / bot-actor queries reached from the swing + DDED callbacks.
extern int  FUN_8023cc28(unsigned int eventId);  // resolve switch event -> object
extern double FUN_80390324(double x);            // easing curve (sin-ish)
extern int  FUN_8025b8e4(int mode);              // gamemode/player-count query

// ---- batch_603 tuning constants (rig base vectors, sway/impulse scales) ----
extern float DAT_803df54c, DAT_803df550, DAT_803df554;   // swing local axis
extern float DAT_803df558, DAT_803df55c, DAT_803df560;   // swing hinge pivot
extern float DAT_803df564, DAT_803df568, DAT_803df56c;   // spark spawn offset
extern float DAT_803df570, DAT_803df574, DAT_803df578;   // dust emitter dir
extern float DAT_803df57c, DAT_803df580, DAT_803df584;   // ambient dust base pos
extern float DAT_803df588, DAT_803df58c, DAT_803df590;   // debris cluster pos A
extern float DAT_803df594, DAT_803df598, DAT_803df59c;   // debris cluster pos B
extern float DAT_803df5a0, DAT_803df5a4, DAT_803df5a8;   // debris cluster pos C
extern float DAT_803df5ac, DAT_803df5b0, DAT_803df5b4;   // debris cluster pos D
extern float DAT_803df5b8, DAT_803df5bc, DAT_803df5c0;   // debris cluster pos E
extern float DAT_803df5c4, DAT_803df5c8, DAT_803df5cc;   // debris cluster pos F
extern float DAT_803df5d0, DAT_803df5d4, DAT_803df5d8;   // debris cluster dir A
extern float DAT_803df5dc, DAT_803df5e0, DAT_803df5e4;   // debris cluster dir B
extern float DAT_803df5e8, DAT_803df5ec, DAT_803df5f0;   // debris cluster dir C
extern float DAT_803df5f4, DAT_803df5f8, DAT_803df5fc;   // debris cluster dir D
extern float DAT_803df600, DAT_803df604;                 // dded near emitter pos
extern float DAT_803df60c, DAT_803df610, DAT_803df614;   // dded damage dir
extern float DAT_803df618, DAT_803df61c, DAT_803df620;   // cspx trail base
extern float DAT_803df624, DAT_803df628, DAT_803df62c;   // 0x2af hinge pivot

extern int    DAT_80611d54;      // global game-state / view context base
extern int    DAT_806108e0;      // signed rng-ish counter A
extern int    DAT_8061267c;      // shared cached-emitter handle slot
extern int    DAT_80565348;      // ambient dust particle def
extern int    DAT_805c45f4;      // debris cluster def A
extern int    DAT_805c4678;      // debris cluster def B (shared)
extern int    DAT_805c46fc;      // debris cluster def C

extern float  FLOAT_80617328, FLOAT_8061736c, FLOAT_806172c0;  // dded integ scales
extern float  FLOAT_80617178, FLOAT_8061721c, FLOAT_8061723c, FLOAT_80617200; // sin-LUT scale
extern float  FLOAT_80617370, FLOAT_80617264, FLOAT_80617374;  // dded threshold gates
extern float  FLOAT_80617318, FLOAT_80617378;                  // camera shake params
extern float  FLOAT_80617384, FLOAT_8061737c, FLOAT_80617380;  // scrt / cspx params
extern float  FLOAT_80617234;                                  // cspx trail size min
extern float  FLOAT_80617388, FLOAT_8061738c;                  // 0x2af twist limits
extern float  FLOAT_80617340, FLOAT_8061735c;                  // angle-wrap span / mid
extern float  FLOAT_80617348, FLOAT_8061734c, FLOAT_80617350;  // NEPS rng target A/B/C
extern float  FLOAT_80617354, FLOAT_8061725c, FLOAT_806171c4;  // NEPS rng target D/E/F
extern float  FLOAT_80617358, FLOAT_8061719c;                  // dust scatter / reseed
extern float  FLOAT_806172a0, FLOAT_80617294, FLOAT_806172a4;  // sway rand rate/span
extern float  FLOAT_80617360, FLOAT_80617364, FLOAT_80617368;  // hinge break gates
extern float  FLOAT_80617288, FLOAT_80617218;                  // island drift / arm timer

// ---- typed aliases for batch_603 header-vs-ROM mismatches (no redeclare) ----
// subobj_island_create_axis returns the island index at the ROM call site.
static int (*const __wr_island_create_axis)(unsigned int,unsigned int,float*,float*)
    = (int(*)(unsigned int,unsigned int,float*,float*))&subobj_island_create_axis;
// subobj_build_descriptor_default at the ROM site takes the full arg block +
// returns nothing; carry the extra args verbatim through a wide alias.
static void (*const __wr_subobj_build_desc603)(unsigned long long,unsigned long long,double,double,
                                               double,double,double,unsigned long long,int,int,int,int,float)
    = (void(*)(unsigned long long,unsigned long long,double,double,double,double,double,
               unsigned long long,int,int,int,int,float))&subobj_build_descriptor_default;
// fluid_emitter_register returns the emitter handle at the ROM call site.
static int (*const __wr_fluid_emitter_reg)(int,int,int,int,int)
    = (int(*)(int,int,int,int,int))&fluid_emitter_register;
// particle_spawn_billboard returns the slot handle.
static int (*const __wr_spawn_billboard)(int,int,int,int*,int)
    = (int(*)(int,int,int,int*,int))&particle_spawn_billboard;
// damage_dispatch_to_entity at the ROM site takes FP context + the event block.
static void (*const __wr_damage_dispatch603)(unsigned int,double,double,double,double,double,double,
                                             unsigned long long,int*)
    = (void(*)(unsigned int,double,double,double,double,double,double,unsigned long long,int*))
      &damage_dispatch_to_entity;
} // extern "C"

// ===========================================================================
// Address: 0x801d69a4 | Name: worm_swing_update | Date: 2026-09-07
// Batch: batch_603
// ---------------------------------------------------------------------------
// Per-frame update for the swaying/hinged prop (subtype 0x280 register site).
// Builds a Y-rotation matrix from the current swing angle (+0x70), rotates the
// rig's two hinge nodes about their edge pivots, adjusts the linked cached
// particle emitters' drag by the current level state, and on 'CANR/CANM/CNR2/
// CNL2' subtypes re-seeds a random swing target (+0xc4). The swing angle then
// eases toward that target within a random per-frame rate.
// ===========================================================================
void worm_swing_update(void)
{
    int ent = FUN_803826e8();
    int rig = *(int *)(ent + 0x7c);
    int *mdl = *(int **)(*(int *)(ent + 4) + 4);
    float *pivot = (float *)mdl[*mdl * -0x30 + 0x1b];

    float axis[3];
    float hinge[3];
    float mtx[16];
    axis[0]  = DAT_803df54c; axis[1]  = DAT_803df550; axis[2]  = DAT_803df554;
    hinge[0] = DAT_803df558; hinge[1] = DAT_803df55c; hinge[2] = DAT_803df560;

    mtx4_set_rotation_y((double)((FLOAT_8061716c * *(float *)(ent + 0x70)) / FLOAT_80617170), mtx);
    mtx4_transform_vector(mtx, axis);
    __wr_rotate_about_point((int)(ent + 0x274), hinge, pivot);

    // Adjust cached-emitter drag by level state; state 4 uses a stiffer drag.
    if ((*(int *)(DAT_80611d54 + 0x2a0) == 4) && (*(int *)(ent + 0x1a0) == 1)) {
        int off = 0;
        for (int i = 0; i < *(int *)(ent + 0x1e8); i++) {
            int slot = *(int *)(*(int *)(ent + 0x1fc) + off);
            particle_cached_set_drag((float)(double)FLOAT_80617194, (ParticleEmitter **)&slot);
            off += 4;
        }
        FUN_80382734();
        return;
    }
    if (*(int *)(ent + 0x1a0) == 1) {
        int off = 0;
        for (int i = 0; i < *(int *)(ent + 0x1e8); i++) {
            int slot = *(int *)(*(int *)(ent + 0x1fc) + off);
            particle_cached_set_drag((float)(double)FLOAT_806171ac, (ParticleEmitter **)&slot);
            off += 4;
        }
    }

    float accum;
    if (*(int **)(ent + 0x1fc) == (int *)0) {
        goto apply_translation;
    } else {
        int firstSlot = **(int **)(ent + 0x1fc);
        if (particle_cached_is_starting((ParticleEmitter **)&firstSlot) == 0) {
            goto apply_translation;
        }
        // Emitter just started: scatter the swing origin and pick a subtype-keyed
        // random swing target.
        mdl = *(int **)(*(int *)(ent + 4) + 4);
        int node = mdl[*mdl * -0x30 + 0x1b];
        float sx = DAT_803df564 + *(float *)(ent + 100) + *(float *)(node + 0x18);
        float sy = DAT_803df568 + *(float *)(ent + 0x68) + *(float *)(node + 0x1c);
        float sz = DAT_803df56c + *(float *)(ent + 0x6c);
        (void)sx; (void)sy; (void)sz;

        float jx = FLOAT_8061720c * ((float)(int)__wr_rng_tick() / FLOAT_80617240 - FLOAT_80617168);
        float jy = FLOAT_8061720c * ((float)(int)__wr_rng_tick() / FLOAT_80617240 - FLOAT_80617168);
        float jz = FLOAT_8061720c * ((float)(int)__wr_rng_tick() / FLOAT_80617240 - FLOAT_80617168);
        float scale = FLOAT_80617198;
        axis[0] = axis[0] + jx * scale;
        axis[1] = axis[1] + jy * scale;
        axis[2] = axis[2] + jz * scale;

        *(int *)(ent + 100)  = *(int *)(ent + 0xa8);
        *(int *)(ent + 0x68) = *(int *)(ent + 0xac);
        *(int *)(ent + 0x6c) = *(int *)(ent + 0xb0);

        int kind = *(int *)(ent + 0x60);
        float r = (float)(int)__wr_rng_tick() / FLOAT_80617240 - FLOAT_80617168;
        if (kind == 0x43414e52) {          // 'CANR'
            *(float *)(ent + 0xc4) = FLOAT_80617348 * r * FLOAT_806171c4 + FLOAT_8061734c;
        } else if (kind < 0x43414e52) {
            if (kind != 0x43414e4d) {
                if (kind < 0x43414e4d) {
                    if (0x43414e4b < kind) {   // 'CANL'
                        *(float *)(ent + 0xc4) = FLOAT_80617348 * r + FLOAT_80617350;
                    }
                } else if (kind < 0x43414e4f) { // 'CANM'
                    *(float *)(ent + 0xc4) = FLOAT_80617348 * r;
                }
            }
        } else if (kind == 0x434e5232) {   // 'CNR2'
            *(float *)(ent + 0xc4) = FLOAT_80617348 * r + FLOAT_80617354;
        } else if ((kind < 0x434e5232) && (kind == 0x434e4c32)) { // 'CNL2'
            *(float *)(ent + 0xc4) = FLOAT_80617348 * r + FLOAT_8061725c;
        }
    }

apply_translation:
    {
        float target = *(float *)(ent + 0xc4);
        float cur    = *(float *)(ent + 0x70);
        if (cur <= FLOAT_80617294 + target) {
            if (target - FLOAT_80617294 <= cur) {
                *(float *)(ent + 0x70) = target;
            } else {
                float step = FLOAT_80617294 * (float)(int)(DAT_806108e0 ^ 0x80000000);
                *(float *)(ent + 0x70) = step + cur;
            }
        } else {
            float step = FLOAT_80617294 * (float)(int)(DAT_806108e0 ^ 0x80000000);
            *(float *)(ent + 0x70) = -(step - cur);
        }
    }
    FUN_80382734();
}

// ===========================================================================
// Address: 0x801d6f9c | Name: worm_ambient_dust_spawn | Date: 2026-09-07
// Batch: batch_603
// ---------------------------------------------------------------------------
// Level-gated ambient dust puff spawner. On a handful of level ids it counts
// down a shared timer (FLOAT_80612670); when it lapses it spawns a one-shot
// dust emitter (DAT_80565348) at a jittered position and re-arms the timer to a
// random interval.
// ===========================================================================
void worm_ambient_dust_spawn(void)
{
    float bz = DAT_803df584;
    float by = DAT_803df580;
    float bx = DAT_803df57c;
    if (DAT_80611d54 == 0) {
        return;
    }
    int lvl = *(int *)(DAT_80611d54 + 0x2a0);
    if (!((lvl == 0x48) || ((unsigned int)(lvl - 0x49) < 3) || (lvl == 0x4d) ||
          (lvl == 0x51) || (lvl == 0x52))) {
        return;
    }

    float denom = (float)(int)(DAT_806109c0 ^ 0x80000000);
    float dt    = (float)(int)(DAT_806108e4 ^ 0x80000000);
    FLOAT_80612670 = -((FLOAT_806171ac / denom) * dt - FLOAT_80612670);
    if (FLOAT_80612670 < FLOAT_80617194) {
        float dir[3];
        float pos[3];
        dir[0] = DAT_803df570; dir[1] = DAT_803df574; dir[2] = DAT_803df578;
        pos[0] = FLOAT_8061720c * ((float)(int)__wr_rng_tick() / FLOAT_80617240 - FLOAT_80617168)
                 * FLOAT_80617358 + bx;
        pos[1] = FLOAT_80617194 + by;
        pos[2] = FLOAT_8061720c * ((float)(int)__wr_rng_tick() / FLOAT_80617240 - FLOAT_80617168)
                 * FLOAT_80617358 + bz;
        particle_emitter_spawn(0, (unsigned int)(unsigned long)&DAT_80565348, 0, pos, dir);
        particle_emitter_set_size_range((float)(double)FLOAT_8061720c, (float)(double)FLOAT_8061720c);
        FLOAT_80612670 = FLOAT_80617168 * ((float)(int)__wr_rng_tick() / FLOAT_80617240) + FLOAT_8061719c;
    }
}

// ===========================================================================
// Address: 0x801d71dc | Name: worm_debris_emitter_cluster_build | Date: 2026-09-07
// Batch: batch_603
// ---------------------------------------------------------------------------
// Builds the debris/dust emitter cluster for a set-piece break: spawns one lead
// emitter (DAT_805c45f4), three shared-def puffs (DAT_805c4678) each with a
// random start delay and drag, one cached emitter from the shared block
// (DAT_80612688+0xcc), and a trailing emitter (DAT_805c46fc). Positions and
// directions come from the DAT_803df5xx constant tables.
// ===========================================================================
void worm_debris_emitter_cluster_build(void)
{
    FUN_803826b4();

    // Direction vectors (rows of the 803df5d4.. table).
    float dirB[3] = { DAT_803df5dc, DAT_803df5e0, DAT_803df5e4 };
    float dirLead[3] = { DAT_803df5d4, DAT_803df5d8, DAT_803df5dc };
    float dirTrail[3] = { DAT_803df5f4, DAT_803df5f8, DAT_803df5fc };
    (void)dirB;

    // Position vectors (rows of the 803df588.. table).
    float posLead[3]  = { DAT_803df588, DAT_803df58c, DAT_803df590 };
    float posA[3]     = { DAT_803df594, DAT_803df598, DAT_803df59c };
    float posB[3]     = { DAT_803df5a0, DAT_803df5a4, DAT_803df5a8 };
    float posC[3]     = { DAT_803df5ac, DAT_803df5b0, DAT_803df5b4 };
    float posCached[3]= { DAT_803df5b8, DAT_803df5bc, DAT_803df5c0 };
    float posTrail[3] = { DAT_803df5c4, DAT_803df5c8, DAT_803df5cc };
    float dirCached[3]= { DAT_803df5d0, DAT_803df5d4, DAT_803df5d8 };
    float dirPuff[3]  = { DAT_803df5e8, DAT_803df5ec, DAT_803df5f0 };

    particle_emitter_spawn(0, (unsigned int)(unsigned long)&DAT_805c45f4, 0, posLead, dirLead);
    particle_emitter_spawn(0, (unsigned int)(unsigned long)&DAT_805c4678, 0, posA, dirPuff);
    particle_emitter_spawn(0, (unsigned int)(unsigned long)&DAT_805c4678, 0, posB, dirPuff);

    particle_cached_set_delay(FLOAT_80617168 * ((float)(int)__wr_rng_tick() / FLOAT_80617240),
                              (ParticleEmitter **)0);
    particle_emitter_set_drag(FLOAT_8061720c * ((float)(int)__wr_rng_tick() / FLOAT_80617240)
                              + FLOAT_806171b0);
    particle_emitter_spawn(0, (unsigned int)(unsigned long)&DAT_805c4678, 0, posC, dirPuff);
    particle_cached_set_delay(FLOAT_80617168 * ((float)(int)__wr_rng_tick() / FLOAT_80617240),
                              (ParticleEmitter **)0);
    particle_emitter_set_drag(FLOAT_8061720c * ((float)(int)__wr_rng_tick() / FLOAT_80617240)
                              + FLOAT_806171b0);
    particle_emitter_spawn(0, (unsigned int)(DAT_80612688 + 0xcc), 0, posCached, dirCached);
    particle_emitter_spawn(0, (unsigned int)(unsigned long)&DAT_805c46fc, 0, posTrail, dirTrail);

    FUN_80382700();
}

// ===========================================================================
// Address: 0x801d74a4 | Name: worm_swing_angle_track_render | Date: 2026-09-07
// Batch: batch_603
// ---------------------------------------------------------------------------
// Render callback installed on the swing prop while a switch event is active:
// wraps the object's yaw (+0x70) toward the tracked switch target's yaw within
// +/-FLOAT_80617340, choosing the shortest signed delta.
// ===========================================================================
void worm_swing_angle_track_render(void)
{
    int ent = FUN_803826f0();
    unsigned int evt = (unsigned int)switch_get_current_event_id(*(int *)(ent + 8));
    switch_weapon_swap_handler();

    int obj;
    if (((int)evt >= 0) && ((obj = FUN_8023cc28(evt)) != 0)) {
        float tgt = *(float *)(obj + 0x80);
        float base = *(float *)(obj + 0x24);
        float dLo = base - (tgt + FLOAT_80617340);
        float dMid = ABS(base - tgt);
        float dHi = base - (tgt - FLOAT_80617340);
        float chosen = dHi;
        float aLo = ABS(dLo);
        if (aLo <= dMid) {
            if (aLo < ABS(dHi)) {
                chosen = dLo;
            }
        } else if (dMid < ABS(dHi)) {
            chosen = base - tgt;
        }
        *(float *)(*(int *)(ent + 8) + 0x70) = *(float *)(*(int *)(ent + 8) + 0xcc) + chosen;
    }
    FUN_8038273c();
}

// ===========================================================================
// Address: 0x801d757c | Name: worm_swing_pose_follow_update | Date: 2026-09-07
// Batch: batch_603
// ---------------------------------------------------------------------------
// Per-frame update that installs worm_swing_angle_track_render as the render cb
// and, while a switch event's tracked object has settled (+0xd8 == 0), eases
// the object's yaw and the target's two euler angles toward each other over the
// frame delta, each through the FUN_80390324 easing curve within +/-span.
// ===========================================================================
void worm_swing_pose_follow_update(int self)
{
    int rig = *(int *)(self + 0x7c);
    unsigned int evt = (unsigned int)switch_get_current_event_id(self);
    *(void **)(rig + 0x14) = (void *)&worm_swing_angle_track_render;

    int obj;
    if (((int)evt >= 0) && ((obj = FUN_8023cc28(evt)) != 0) && (*(int *)(obj + 0xd8) == 0)) {
        float span = FLOAT_80617340;
        float dtFactor = FLOAT_806108ec * (FLOAT_806171ac /
                          (float)(int)(DAT_806109c0 ^ 0x80000000));

        // --- object yaw (+0x70) toward target (+0xcc) ---
        float cur = *(float *)(self + 0x70);
        float tgt = *(float *)(self + 0xcc);
        float dLo = tgt - (cur + span);
        float dMid = ABS(tgt - cur);
        float dHi = tgt - (cur - span);
        float chosen = dHi;
        if (ABS(dLo) <= dMid) { if (ABS(dLo) < ABS(dHi)) chosen = dLo; }
        else if (dMid < ABS(dHi)) chosen = tgt - cur;
        float blended = chosen * dtFactor + cur;
        float eased = (float)FUN_80390324((double)((blended - FLOAT_8061735c) / span));
        *(float *)(self + 0x70) = -(span * eased - blended);

        // --- target euler X (+0x1c) toward object (+0x7c) ---
        cur = *(float *)(obj + 0x1c);
        tgt = *(float *)(obj + 0x7c);
        dLo = tgt - (cur + span);
        dMid = ABS(tgt - cur);
        dHi = tgt - (cur - span);
        chosen = dHi;
        if (ABS(dLo) <= dMid) { if (ABS(dLo) < ABS(dHi)) chosen = dLo; }
        else if (dMid < ABS(dHi)) chosen = tgt - cur;
        blended = chosen * dtFactor + cur;
        eased = (float)FUN_80390324((double)((blended - FLOAT_8061735c) / span));
        *(float *)(obj + 0x1c) = -(span * eased - blended);

        // --- target euler Y (+0x24) toward object (+0x80) ---
        cur = *(float *)(obj + 0x24);
        tgt = *(float *)(obj + 0x80);
        dLo = tgt - (cur + span);
        dMid = ABS(tgt - cur);
        dHi = tgt - (cur - span);
        chosen = dHi;
        if (ABS(dLo) <= dMid) { if (ABS(dLo) < ABS(dHi)) chosen = dLo; }
        else if (dMid < ABS(dHi)) chosen = tgt - cur;
        blended = chosen * dtFactor + cur;
        eased = (float)FUN_80390324((double)((blended - FLOAT_8061735c) / span));
        *(float *)(obj + 0x24) = -(span * eased - blended);
    }
}

// ===========================================================================
// Address: 0x801d77c0 | Name: worm_island_break_cleanup | Date: 2026-09-07
// Batch: batch_603
// ---------------------------------------------------------------------------
// subobj-island break cleanup: flags the body as broken (0x800100), frees the
// island slot, and clears its user extra-words. Installed as the island break
// user-callback by worm_hinge_joint_callback.
// ===========================================================================
void worm_island_break_cleanup(int islandIndex, unsigned int bodyHandle)
{
    subobj_set_flag_bits(bodyHandle, 0x800100);
    subobj_island_pool_free(islandIndex);
    subobj_set_extra_words(bodyHandle, 0, 0);
}

// ===========================================================================
// Address: 0x801d781c | Name: worm_hinge_break_render | Date: 2026-09-07
// Batch: batch_603
// ---------------------------------------------------------------------------
// Render callback for the hinged breakable (subtype 0x2af): on anim event 0x17
// it finds the physics island backing the body and, if valid, runs the break
// cleanup, then defers to subobj_break_tick for the visual break.
// ===========================================================================
void worm_hinge_break_render(unsigned long long p1, unsigned long long p2, double p3, double p4,
                             double p5, double p6, double p7, unsigned long long p8, int *self)
{
    if (self[6] == 0x17) {
        int obj = *self;
        int island = subobj_island_find_by_body_b(*(int *)(obj + 0x60));
        if (island >= 0) {
            worm_island_break_cleanup(island, *(unsigned int *)(obj + 0x80));
        }
        subobj_break_tick(p1, p2, p3, p4, p5, p6, p7, p8);
    }
}

// ===========================================================================
// Address: 0x801d7880 | Name: worm_hinge_joint_callback | Date: 2026-09-07
// Batch: batch_603
// ---------------------------------------------------------------------------
// subobj-island joint callback for the hinged breakable: when the contacting
// body is in the right state (0x400000 flag) and the applied load exceeds
// FLOAT_80617360, it tightens the island's twist limits and installs
// worm_island_break_cleanup as the island's user break callback.
// ===========================================================================
void worm_hinge_joint_callback(double load, int bodyA, int contact)
{
    unsigned short state = *(unsigned short *)(contact + 0x2e);
    if ((state == 4) || ((state < 4) && (state == 1))) {
        int other = *(int *)(contact + 0x28);
        if (bodyA == other) {
            other = *(int *)(contact + 0x24);
        }
        if (*(int *)(other + 0x20) == 0x400000) {
            int island = subobj_island_find_by_body_b(*(int *)(bodyA + 0x60));
            if (load < (double)FLOAT_80617360) {
                subobj_island_set_twist_limits((double)FLOAT_80617364, (double)FLOAT_80617368, island);
                subobj_island_set_user_ptr(island, (void *)&worm_island_break_cleanup);
            }
        }
    }
}

// ===========================================================================
// Address: 0x801d7928 | Name: worm_dded_physics_update | Date: 2026-09-07
// Batch: batch_603
// ---------------------------------------------------------------------------
// Per-frame physics for the 'DDED' falling/detonating prop. Integrates an
// rng-perturbed velocity into the object's position (+0x64/+0x68/+0x6c), spins
// up a wobble angle (+0x1a4) clamped to FLOAT_806172c0 and reads a sine-LUT
// (DAT_80583228) for the visual angle (+0xc8). Repositions the trailing cached
// emitter. When the object nears the ground it enables a puff emitter; when it
// lands it dispatches a radial damage event, destroys its emitters, shakes the
// camera and (if a 'DDD1' bot is linked) spawns hit effects.
// ===========================================================================
void worm_dded_physics_update(int self)
{
    float velScale = FLOAT_80617328;
    double bias = DOUBLE_806171d8;
    float restAngle = FLOAT_80617170;
    double dtNum = (double)FLOAT_806171ac;

    float denom = (float)(int)(DAT_806109c0 ^ 0x80000000);
    float dt    = (float)(int)(DAT_806108e4 ^ 0x80000000);
    (void)bias;

    // rng-perturbed velocity integration on X/Y/Z (+0xb4/+0xb8/+0xbc drive +0x64/+0x68/+0x6c)
    *(float *)(self + 0xb8) =
        -(FLOAT_80617328 * FLOAT_8061736c * (float)(dtNum / denom) * dt - *(float *)(self + 0xb8));
    *(float *)(self + 100) =
        *(float *)(self + 0xb4) * velScale * (float)(dtNum / denom) * dt + *(float *)(self + 100);
    *(float *)(self + 0x68) =
        *(float *)(self + 0xb8) * velScale * (float)(dtNum / denom) * dt + *(float *)(self + 0x68);
    *(float *)(self + 0x6c) =
        *(float *)(self + 0xbc) * velScale * (float)(dtNum / denom) * dt + *(float *)(self + 0x6c);
    *(float *)(self + 0x70) = restAngle;
    *(unsigned char *)(self + 0x32) = 2;

    // wobble angle spin-up, clamped
    *(float *)(self + 0x1a4) =
        velScale * (float)(dtNum / denom) * dt + *(float *)(self + 0x1a4);
    if (FLOAT_806172c0 < *(float *)(self + 0x1a4)) {
        *(float *)(self + 0x1a4) = FLOAT_80617194;
    }

    // sine-LUT visual angle
    float phase = FLOAT_8061721c * (*(float *)(self + 0x1a4) / FLOAT_806172c0) - FLOAT_8061716c;
    unsigned int idx = (unsigned int)(FLOAT_8061723c * phase + FLOAT_80617168);
    *(float *)(self + 200) = FLOAT_80617200 * (&DAT_80583228)[idx & 0x7ff];
    *(float *)(self + 0xc0) = FLOAT_80617178;

    // reposition trailing cached emitter to current position
    int trail = *(int *)(DAT_80612688 + 0x650);
    particle_cached_set_position((ParticleEmitter **)&trail, (Vec3 *)(self + 100));

    float vz = *(float *)(self + 0x6c);
    if ((vz < FLOAT_80617370) && (FLOAT_80617264 < vz)) {
        // near ground: enable puff emitter
        float pos[3] = { DAT_803df600, DAT_803df604, vz };
        int puff = DAT_8061267c;
        particle_cached_set_position((ParticleEmitter **)&puff, (Vec3 *)pos);
        int en = DAT_8061267c;
        particle_cached_enable((ParticleEmitter **)&en);
    }

    if (*(float *)(self + 0x6c) < FLOAT_80617374) {
        // landed: radial damage + cleanup + camera shake
        int bot = (int)aiactor_find_bot_by_linked_entity(0x44454431);   // 'DED1'
        int evt[8];
        int dir[3] = { *(int *)&DAT_803df60c, *(int *)&DAT_803df610, *(int *)&DAT_803df614 };
        switch_slot_reset((int)evt);
        evt[0] = self;
        *(int *)((char *)evt + 0x28) = self + 100;      // world position ptr
        *(int **)((char *)evt + 0x30) = dir;            // direction ptr
        *(float *)((char *)evt + 0x38) = FLOAT_80617324; // radius
        *(int *)((char *)evt + 0x1c) = 4;
        *(int *)((char *)evt + 0x20) = 4;
        __wr_damage_dispatch603((unsigned int)0, (double)0, (double)0, (double)0, (double)0,
                                (double)0, (double)0, (unsigned long long)0, evt);

        int dh = DAT_8061267c;
        particle_handle_destroy(&dh);
        int trail2 = *(int *)(DAT_80612688 + 0x650);
        particle_cached_disable((ParticleEmitter **)&trail2);
        player_broadcast_camera_shake((float)(double)FLOAT_806171ac, (float)(double)FLOAT_80617318,
                                      (float)(double)FLOAT_80617378);
        if ((bot != 0) && (*(int *)(bot + 0x7c) != 0)) {
            character_damage_spawn_effects();
        }
    }
}

// ===========================================================================
// Address: 0x801d7ca4 | Name: worm_scripted_prop_on_spawn | Date: 2026-09-07
// Batch: batch_603
// ---------------------------------------------------------------------------
// Master scripted-prop on-spawn dispatcher. Keyed on the entity subtype word
// (+0x94), it wires up every set-piece prop: the MSUB submersible (0x27c/0x253),
// self-lit / ambient props, jointed bodies, the NEPS/CSPX shatter debris
// (0x270..), the swaying prop (0x293/0x280), the JQDR fluid break (0x2a0+), the
// 0x2af hinge-break body (islands + worm_hinge_joint_callback +
// worm_hinge_break_render), the impact-arm props (0x2a3/0x2ab), and the SCRT
// impact-smoke props (0x2b4). Registers the matching update/render callbacks and
// sets initial physics/material state.
// ===========================================================================
void worm_scripted_prop_on_spawn(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                                 unsigned long long p4, unsigned long long p5, unsigned long long p6,
                                 unsigned long long p7, unsigned long long p8)
{
    unsigned long long packed = (unsigned long long)FUN_803826f0();
    float fSpan = FLOAT_80617384;
    float fMid  = FLOAT_80617268;
    float fSmall= FLOAT_8061720c;
    float fBias = FLOAT_806171ac;
    float fOne  = FLOAT_80617194;
    int ent = (int)(packed >> 0x20);
    int *tag = (int *)packed;
    unsigned short sub = *(unsigned short *)(ent + 0x94);
    int rig;

    if (sub == 0x27c) {
        worm_msub_hatch_set_state(ent, 0, 1);
        render_instance_set_material((double)FLOAT_80617194, *(int **)(ent + 4), 1);
        entity_register_for_update(ent, 3, -0x7fe2dfc8);
        *(int *)(DAT_80612688 + 0x658) = ent;
    } else if (sub < 0x27c) {
        if (sub == 0x266) {
            *(int *)(*(int *)(ent + 4) + 0x30) = 1;
        } else if (sub < 0x266) {
            if (sub == 0x256) {
                rig = *(int *)(ent + 0x7c);
                render_instance_set_material((double)FLOAT_80617194, *(int **)(ent + 4), 1);
                entity_register_for_update(ent, 4, -0x7fe2d92c);
                *(int *)(rig + 0xa0) = 0;
                *(float *)(rig + 0x10) = fOne;
                *(float *)(rig + 0x14) = fOne;
                *(int *)(rig + 0x144) = 0;
            } else if (0x255 < sub) {
                if (sub == 0x25a) {
                    *(unsigned int *)(ent + 0x28) |= 0x40000000;
                    *(int *)(ent + 0x34) = 0xd5;
                } else if (sub < 0x25a) {
                    if (sub < 600) {
                        rig = *(int *)(ent + 0x7c);
                        *(int *)(rig + 0xa0) = 3;
                        *(int *)(ent + 0x22c) = -1;
                        *(float *)(rig + 0x10) = fOne;
                        *(float *)(rig + 0x14) = fOne;
                        *(int *)(rig + 0xbc) = 1;
                        *(float *)(rig + 0xa4) = FLOAT_806171bc;
                        *(float *)(rig + 0xa8) = fOne;
                        *(float *)(rig + 0xac) = fOne;
                        *(unsigned char *)(ent + 0x32) = 2;
                        render_instance_set_material((double)FLOAT_80617194, *(int **)(ent + 4), 1);
                        entity_register_for_update(ent, 3, -0x7fe2fc20);
                        entity_register_for_update(ent, 0xb, -0x7fe2fc54);
                        entity_register_for_update(ent, 2, -0x7fe2fdb8);
                    }
                } else if (sub == 0x25d) {
                    *(int *)(*(int *)(ent + 4) + 0x30) = 1;
                }
            } else if (sub == 0x253) {
                worm_msub_on_spawn(ent);
            } else if (0x252 < sub) {
                if (sub < 0x255) {
                    rig = *(int *)(ent + 0x7c);
                    *(int *)(rig + 0xa0) = 3;
                    *(float *)(rig + 0x10) = fOne;
                    *(float *)(rig + 0x9c) = fOne;
                    *(int *)(ent + 0x22c) = -1;
                    render_instance_set_material((double)FLOAT_80617194, *(int **)(ent + 4), 1);
                    entity_register_for_update(ent, 3, -0x7fe2d0bc);
                } else {
                    *(int *)(*(int *)(ent + 4) + 0x30) = 1;
                }
            } else if (sub == 0xfb) {
                // shatter/billboard default prop (shared with 0x270 tail below)
                goto shatter_default;
            }
        } else {
            if (sub == 0x273) {
                entity_register_for_update(ent, 1, -0x7fdcbc50);
            } else if (0x272 < sub) {
                if (sub != 0x276) {
                    if (sub < 0x276) {
                        if (sub < 0x275) {
                            *(unsigned int *)(ent + 0x28) |= 0x40000000;
                            *(int *)(ent + 0x34) = 0xd5;
                            *(unsigned int *)(ent + 0x28) |= 0x10;
                        } else {
                            *(unsigned int *)(ent + 0x28) |= 0x40000000;
                            *(int *)(ent + 0x34) = 0xd5;
                        }
                    } else if (sub < 0x27b) {
                        *(unsigned int *)(*(int *)(ent + 4) + 0x30) |= 1;
                    }
                }
            } else if (sub == 0x270) {
                // NEPS/CSPX shatter debris prop
                rig = *(int *)(ent + 0x7c);
                *(unsigned int *)(ent + 0x28) |= 0x40000000;
                *(int *)(ent + 0x34) = 0xd5;
                *(unsigned int *)(ent + 0x28) |= 0x10;
                *(int *)(ent + 0x1a4) = 0;
                if (rig != 0) {
                    *(int *)(rig + 0xf4) = __wr_spawn_billboard(rig + 0xf4, 8, 0, (int *)(ent + 100), 0xffff);
                    particle_slot_set_opacity((float)(double)FLOAT_80617200, *(int **)(rig + 0xf4));
                    *(int *)(rig + 0xf8) = __wr_spawn_billboard(rig + 0xf8, 8, 0, (int *)(ent + 100), 0xffff);
                    particle_slot_set_opacity((float)(double)FLOAT_80617200, *(int **)(rig + 0xf8));
                }
                __wr_set_render_type(ent, (void (*)())&worm_neps_shatter_render);
                if ((tag != (int *)0) && (*tag == 0x43535058)) {   // 'CSPX'
                    float base[3] = { DAT_803df618, DAT_803df61c, DAT_803df620 };
                    entity_register_for_update(ent, 3, -0x7fe286d8);
                    *(float *)(ent + 0xbc) = FLOAT_806172a4;
                    *(float *)(ent + 0xb8) = FLOAT_8061737c;
                    particle_handle_clear((unsigned int *)&DAT_8061267c);
                    particle_emitter_spawn((unsigned int)(unsigned long)&DAT_8061267c,
                                           (unsigned int)(DAT_80612688 + 0x474), 0, base, base);
                    int dh = DAT_8061267c;
                    particle_cached_disable((ParticleEmitter **)&dh);
                    int sh = DAT_8061267c;
                    particle_cached_set_size_range((float)(double)FLOAT_80617234,
                                                   (float)(double)FLOAT_8061720c,
                                                   (ParticleEmitter **)&sh);
                    particle_emitter_spawn((unsigned int)(DAT_80612688 + 0x650),
                                           (unsigned int)(DAT_80612688 + 0x5cc), 0,
                                           (float *)(tag + 4), base);
                    if (rig != 0) {
                        particle_slot_set_opacity((float)(double)FLOAT_80617380, *(int **)(rig + 0xf4));
                        particle_slot_set_opacity((float)(double)FLOAT_80617380, *(int **)(rig + 0xf8));
                    }
                }
                entity_register_for_update(ent, 4, -0x7fe2705c);
                *(float *)(ent + 0x1cc) = FLOAT_80617168;
                goto done;
            } else if (sub < 0x270) {
                if (sub < 0x26f) goto done;
                goto shatter_default;
            } else {  // 0x271
                goto done;
            }
            goto done;
        }
    } else if (sub == 0x2a2) {
        *(unsigned int *)(ent + 0x28) |= 0x10;
        *(unsigned int *)(ent + 0x9c) |= 0xc;
        __wr_set_render_type(ent, (void (*)())&worm_fade_alpha_countdown);
        *(int *)(*(int *)(ent + 4) + 0x30) = 2;
        *(float *)(ent + 0x1d8) = FLOAT_806171b0;
        *(int *)(ent + 0x1cc) = *(int *)(ent + 0x1d8);
    } else if (sub < 0x2a2) {
        if (sub == 0x293) {
            *(void **)(*(int *)(ent + 0x7c) + 0x8c) = (void *)&worm_swing_height_curve;
        } else if (sub < 0x293) {
            if (sub == 0x280) {
                rig = *(int *)(ent + 0x7c);
                *(unsigned int *)(ent + 0x18) |= 2;
                *(int *)(rig + 0xa0) = 3;
                *(int *)(DAT_80612688 + 0x668) = 0;
                *(float *)(rig + 0x10) = fOne;
                *(float *)(rig + 0x18) = fOne;
                *(float *)(rig + 0x9c) = fOne;
                render_instance_set_material((double)FLOAT_80617194, *(int **)(ent + 4), 1);
                entity_register_for_update(ent, 3, -0x7fe2ae40);
                entity_register_for_update(ent, 2, -0x7fe2ab64);
                *(unsigned int *)(ent + 0x28) &= 0xfbffffff;
            } else if (sub < 0x280) {
                if (0x27e < sub) {
                    rig = *(int *)(ent + 0x7c);
                    *(unsigned int *)(ent + 0x18) |= 2;
                    *(int *)(rig + 0xa0) = 0;
                    *(float *)(rig + 0x10) = fBias;
                    *(float *)(rig + 0x9c) = FLOAT_80617194;
                    render_instance_set_material((double)FLOAT_80617194, *(int **)(ent + 4), 1);
                    entity_register_for_update(ent, 2, -0x7fe2c134);
                    *(unsigned int *)(ent + 0x28) |= 0x4000000;
                    *(int *)(ent + 0x34) = 0xd5;
                    *(unsigned int *)(ent + 0x28) |= 0x40000000;
                }
            } else if (0x291 < sub) {
                // 0x292: jointed shatter body with rng swing target seed
                rig = *(int *)(ent + 0x7c);
                *(int *)(ent + 0x22c) = -1;
                *(float *)(rig + 0x10) = fOne;
                *(float *)(rig + 0x9c) = FLOAT_806171ac +
                    ((float)(int)__wr_rng_tick() / FLOAT_80617240 - FLOAT_80617168);
                particle_entity_update_emit_rates(ent);
                *(int *)(ent + 0xa8) = *(int *)(ent + 100);
                *(int *)(ent + 0xac) = *(int *)(ent + 0x68);
                *(int *)(ent + 0xb0) = *(int *)(ent + 0x6c);
                *(unsigned char *)(ent + 0x32) = 2;
                render_instance_set_material((double)FLOAT_80617194, *(int **)(ent + 4), 1);
                entity_register_for_update(ent, 4, -0x7fe2965c);
                *(int *)(ent + 0x1a0) = (tag[1] == 0x4e4f4251) ? 1 : 0;   // 'NOBQ'
                int kind = *tag;
                float r = (float)(int)__wr_rng_tick() / FLOAT_80617240 - FLOAT_80617168;
                float v = FLOAT_80617194;
                int have = 0;
                if (kind == 0x43414e52) { v = FLOAT_80617348 * r * FLOAT_806171c4 + FLOAT_8061734c; have = 1; }
                else if (kind < 0x43414e52) {
                    if (kind != 0x43414e4d) {
                        if (kind < 0x43414e4d) { if (0x43414e4b < kind) { v = FLOAT_80617348 * r + FLOAT_80617350; have = 1; } }
                        else if (kind < 0x43414e4f) { v = FLOAT_80617348 * r; have = 1; }
                    }
                } else if (kind == 0x434e5232) { v = FLOAT_80617348 * r + FLOAT_80617354; have = 1; }
                else if ((kind < 0x434e5232) && (kind == 0x434e4c32)) { v = FLOAT_80617348 * r + FLOAT_8061725c; have = 1; }
                if (have) { *(float *)(ent + 0xc4) = v; *(float *)(ent + 0x70) = v; }
            }
        } else if (sub == 0x29b) {
            *(int *)(*(int *)(ent + 4) + 0x30) = 1;
        } else if (sub < 0x29b) {
            if (sub == 0x296) {
                entity_register_for_update(ent, 4, -0x7fe28a84);
                *(int *)(ent + 0xcc) = *(int *)(ent + 0x70);
            }
        } else if (0x2a0 < sub) {
            // 0x2a1: JQDR fluid break body
            rig = *(int *)(ent + 0x7c);
            *(unsigned int *)(ent + 0x28) |= 0x10;
            __wr_set_render_type(ent, (void (*)())&worm_jqdr_break_event);
            *(unsigned int *)(*(int *)(ent + 4) + 0x30) |= 2;
            *(float *)(ent + 0x1d8) = fBias;
            *(float *)(ent + 0x1cc) = fBias;
            *(float *)(rig + 0x10) = FLOAT_80617294;
            *(float *)(rig + 0x9c) = fOne;
            *(int *)(rig + 0xa0) = 5;
            *(int *)(rig + 0xbc) = __wr_fluid_emitter_reg(ent, 4, 2, 0x24, 100);
        }
    } else if (sub == 0x2af) {
        // hinge-break body: build a physics island + install joint/break cbs
        float pivot[3] = { DAT_803df624, DAT_803df628, DAT_803df62c };
        float mtx[16];
        entity_build_local_matrix(mtx, (void *)(long)ent, 1, 0);
        int *mdl = *(int **)(*(int *)(ent + 4) + 4);
        int node = mdl[*mdl * -0x30 + 0x1b];
        float anchor[3];
        anchor[0] = *(float *)(node + 0x18);
        anchor[1] = *(float *)(node + 0x1c);
        anchor[2] = *(float *)(node + 0x20);
        mtx4_transform_point(mtx, anchor);
        *(unsigned int *)(ent + 0x28) |= 0x10;
        *(float *)(ent + 0x1d8) = fOne;
        *(float *)(ent + 0x1cc) = fOne;
        *(int *)(ent + 0x80) = __wr_subobj_pool_alloc();
        __wr_subobj_build_desc603(p1, p2, p3, p4, p5, p6, p7, p8,
                                  *(int *)(ent + 0x80), ent, 2, -1, 2.938736e-39f);
        int island = __wr_island_create_axis(*(unsigned int *)(ent + 0x80), 0, anchor, pivot);
        subobj_island_set_twist_limits((double)FLOAT_80617388, (double)FLOAT_8061738c, island);
        subobj_island_set_restitution((double)FLOAT_80617294, island);
        subobj_island_set_drift_gain((double)FLOAT_80617288, island);
        subobj_island_set_joint_callback(island, (void *)(long)*tag);
        subobj_set_extra_words(*(unsigned int *)(ent + 0x80), 0, (unsigned int)(unsigned long)&worm_hinge_joint_callback);
        __wr_set_render_type(ent, (void (*)())&worm_hinge_break_render);
    } else if (sub < 0x2af) {
        if (sub == 0x2a6) {
            rig = *(int *)(ent + 0x7c);
            *(int *)(rig + 0xa0) = 0;
            *(float *)(rig + 0x10) = fOne;
            render_instance_set_material((double)FLOAT_80617194, *(int **)(ent + 4), 1);
            entity_register_for_update(ent, 3, 0);
        } else if (sub < 0x2a6) {
            if (sub < 0x2a4) {
                rig = *(int *)(ent + 0x7c);
                *(int *)(*(int *)(ent + 4) + 0x30) = 1;
                if (*(float *)(ent + 0xc0) == fOne) {
                    *(unsigned int *)(ent + 0x28) |= 0x10;
                    __wr_set_render_type(ent, (void (*)())&worm_impact_delay_arm);
                    *(float *)(ent + 0x1d8) = FLOAT_806171ac;
                    *(float *)(ent + 0x1cc) = fBias;
                    *(float *)(rig + 0x10) = FLOAT_80617218;
                    *(float *)(rig + 0x9c) = fOne;
                    *(int *)(rig + 0xa0) = 10;
                    *(int *)(rig + 0xbc) = __wr_fluid_emitter_reg(ent, 6, 0, 0x30, 10);
                } else {
                    *(unsigned int *)(ent + 0x28) &= 0xfffffffe;
                }
            }
        } else if (sub == 0x2ab) {
            *(float *)(ent + 0x1d8) = FLOAT_80617268;
            *(float *)(ent + 0x1cc) = fMid;
            *(unsigned int *)(ent + 0x28) |= 0x10;
            *(unsigned int *)(ent + 0x9c) |= 0x80002;
            *(int *)(*(int *)(ent + 4) + 0x30) = 1;
            if ((*(int *)(ent + 0x80) == 0) && (tag == (int *)0)) {
                *(int *)(ent + 0x80) = __wr_subobj_pool_alloc();
                if (*(int *)(ent + 0x80) != 0) {
                    __wr_subobj_build_desc603(p1, p2, p3, p4, p5, p6, p7, p8,
                                              *(int *)(ent + 0x80), ent, 9, -1, 1.1759248e-37f);
                }
            }
        }
    } else if ((sub != 0x2b5) && (sub < 0x2b5) && (0x2b3 < sub)) {
        // 0x2b4: SCRT impact-smoke prop
        *(unsigned int *)(ent + 0x28) |= 0x10;
        *(int *)(*(int *)(ent + 4) + 0x30) = 1;
        *(float *)(ent + 0x1d8) = fSpan;
        *(float *)(ent + 0x1cc) = fSpan;
        *(float *)(ent + 0x1dc) = fSpan;
        *(int *)(ent + 0x1a0) = (tag[1] == 0x4348414e) ? 1 : 0;   // 'CHAN'
        __wr_set_render_type(ent, (void (*)())&weapon_impact_smoke_dispatch);
        if ((tag[1] == 0x53435254) && (FUN_8025b8e4(0) > 5)) {    // 'SCRT'
            *(float *)(ent + 0x68) = *(float *)(ent + 0x68) - FLOAT_8061720c;
            *(unsigned int *)(ent + 0x28) &= 0xfffffffe;
        }
    }
    goto done;

shatter_default:
    // shared shatter/billboard default (subtypes 0xfb / 0x26f..0x272 tail)
    rig = *(int *)(ent + 0x7c);
    *(unsigned int *)(ent + 0x28) |= 0x40000000;
    *(int *)(ent + 0x34) = 0xd5;
    *(unsigned int *)(ent + 0x28) |= 0x10;
    *(int *)(ent + 0x1a4) = 0;
    if (rig != 0) {
        *(int *)(rig + 0xf4) = __wr_spawn_billboard(rig + 0xf4, 8, 0, (int *)(ent + 100), 0xffff);
        particle_slot_set_opacity((float)(double)FLOAT_80617200, *(int **)(rig + 0xf4));
        *(int *)(rig + 0xf8) = __wr_spawn_billboard(rig + 0xf8, 8, 0, (int *)(ent + 100), 0xffff);
        particle_slot_set_opacity((float)(double)FLOAT_80617200, *(int **)(rig + 0xf8));
    }
    __wr_set_render_type(ent, (void (*)())&worm_neps_shatter_render);
    entity_register_for_update(ent, 4, -0x7fe2705c);

done:
    (void)fSmall;
    FUN_8038273c();
}

// ===========================================================================
// batch_605: 0x801d98f0 - 0x801dbfa8
// The 'CDH1' underground / cave-in set-piece master: an on-spawn dispatcher
// (worm_cdh1_on_spawn) that memsets the shared control block, registers the
// level actors, arms ~40 soundpad-zone reverb/portal flags and builds the big
// object menu, plus the per-frame master update (worm_cdh1_master_update) that
// runs the spark-shower sub-update, drives the linked 'SOLY'/'LIEU'/'COLO'
// scientist bots and the 'CPTA' captain via anim events, and gates payload
// fires by the player's current zone. Supporting: a level-id -> state-class
// map, the PLNE spark-shower + explosion sub-update, the particle-def builder,
// two small anim-event break callbacks (TANK/CMDR, SRT2), a payload-fire-5
// wrapper, the 'WNLF' render-flag predicate, and the register-for-update helper.
//
// NON-MATCHING: wide FP-context spill frame (__save_gpr_28 / __restore_gpr_28)
// kept as opaque brackets; the 0x43300000 int<->double magic-bias reduced to a
// plain (double)(int) cast; caller-saved in_fNN/in_psNN_1 FP spill copies the
// decompiler surfaced are dropped; the fast inverse-sqrt Newton refinement is
// kept as ordinary float math. Raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols are
// declared file-locally with the arity/type of their call sites; the scripted
// payload/event dispatch (FUN_80221074) and fourcc entity lookups
// (FUN_8023c32c) reuse the existing local externs.
// ---------------------------------------------------------------------------
extern "C" {
// ---- batch_605 raw ROM helpers (file-local, arity/type matched to sites) ----
extern int  FUN_8023c77c(int fourcc);            // resolve entity record by fourcc (variant)
extern void FUN_8024dd80(double, double, double, double, double, double, double, double,
                         int linked, int self);  // TANK linked-break dispatch (wide FP ctx)
extern unsigned int FUN_80355348(double, double, int, void *nameTbl, int count); // build actor list -> id
extern void FUN_80251f8c(void);                  // menu item-7 build callback
extern void FUN_801dc8bc(void);                  // menu item-7 sibling callback
extern void FUN_801dc814(void);                  // menu item-0 build callback
extern void FUN_801dc9a0(void);                  // menu item-0x11 build callback

// object menu builder API (FUN_8022xxxx family, worm CDH1 on-spawn)
extern void FUN_802215a8(int slot, int stringId, int a, int b, int c); // begin menu entry
extern void FUN_8022161c(int soundId);           // set entry sound
extern void FUN_802215cc(int soundId);           // add entry sub-sound
extern void FUN_802217d4(void *cb);              // set entry build callback
extern void FUN_8022126c(int handle);            // commit entry
extern void FUN_80221bdc(void *block, int size);  // register menu block
extern void FUN_80221be8(void);                  // finalize menu
extern void FUN_80220f30(int op);                // fire scripted payload op N

// ---- batch_605 module state / tuning ----
extern int   DAT_80610d64;      // CDH1 spark-shower enable flag
extern int   DAT_80612674;      // spark-shower spawn timer accumulator
extern int   DAT_80612678;      // spark projectile colour-set index
extern int   DAT_806121e4;      // subobj linked-body list head (impact scan)
extern int   DAT_80611c24;      // entity pool base (handle validation, 0x2e8 stride)
extern int   DAT_80611d5c;      // BTOW slot table base (0xffc stride)
extern int   DAT_80611d58;      // BTOW slot count
extern char  DAT_805015bd;      // single-player level id byte
extern unsigned int DAT_805015c0;   // global gameplay flags
extern int   DAT_805c4ac0;      // CMDR break one-shot latch
extern int   DAT_805c4ac4;      // CPTA weapon-hand-off one-shot latch
extern int   DAT_805c4ab0;      // menu block (0x78 bytes)
extern int   DAT_805c4ab4;      // menu default index
extern int   DAT_806128a0;      // menu commit handle

extern int   DAT_804512dc;      // level-id -> state-class remap table [4 per entry]
extern int   DAT_804512f0;      // spark projectile colour table (0xc stride)
extern int   DAT_804512fc;      // decal spawn params (int table)
extern int   DAT_80451314;      // decal spawn params (float table)
extern int   DAT_80450a38;      // CDH1 actor name table
extern int   DAT_80450bfc;      // CDH1 actor bank name A
extern int   DAT_80450d48;      // CDH1 actor bank name B

extern float DAT_803df648, DAT_803df64c, DAT_803df650;   // spark spawn local offset
extern int   DAT_803df654, DAT_803df658, DAT_803df65c;   // falling-object raycast dir
extern float DAT_803df660, DAT_803df664, DAT_803df668;   // dust emitter scale A
extern int   DAT_803df66c, DAT_803df670, DAT_803df674;   // dust emitter scale B
extern int   DAT_803df678, DAT_803df67c, DAT_803df680;   // dust emitter scale C

extern double DOUBLE_806171d8;  // 0.5 bias constant
extern double DOUBLE_80617248;  // rng magic-bias base
extern double DOUBLE_806173b8;  // spark velocity scale
extern double DOUBLE_806173c0;  // spark position scale
extern float FLOAT_80617240;    // rng span denom
extern float FLOAT_80617228;    // spark spawn probability gate
extern float FLOAT_80617168;    // 0.5
extern float FLOAT_80617170;    // size-key A
extern float FLOAT_806171b0;    // 0.0
extern float FLOAT_806171bc;    // small init const
extern float FLOAT_806171cc;    // speed clamp threshold
extern float FLOAT_806171d0;    // ~2.0 (invsqrt refine)
extern float FLOAT_80617198;    // 1.0-ish
extern float FLOAT_8061719c;    // gravity-ish const
extern float FLOAT_806171a0;    // lifetime A
extern float FLOAT_806171ac;    // 1.0
extern float FLOAT_806171e0;    // emitter const
extern float FLOAT_80617200;    // lifetime B
extern float FLOAT_80617208;    // -1.0-ish
extern float FLOAT_8061720c;    // 2.0-ish
extern float FLOAT_8061721c;    // size-end const
extern float FLOAT_80617218;    // camera-shake amplitude base
extern float FLOAT_80617220;    // speed gate
extern float FLOAT_80617224;    // spark scatter scale
extern float FLOAT_8061722c;    // vel range const
extern float FLOAT_80617234;    // vel range const B
extern float FLOAT_80617250;    // colour-end const
extern float FLOAT_8061725c;    // drag const
extern float FLOAT_80617260;    // pitch/size const
extern float FLOAT_80617268;    // vel scale const
extern float FLOAT_80617288;    // sound volume const
extern float FLOAT_80617290;    // sound range const
extern float FLOAT_80617294;    // fall gate const
extern float FLOAT_806172a0;    // emitter const
extern float FLOAT_806172a4;    // subobj impact height gate
extern float FLOAT_806172c0;    // pitch scale const
extern float FLOAT_806172c4;    // colour const
extern float FLOAT_806172e8;    // emitter const
extern float FLOAT_806172f8;    // colour-end const
extern float FLOAT_80617328;    // colour-end const B
extern float FLOAT_80617340;    // size-key B
extern float FLOAT_80617390;    // colour const C
extern float FLOAT_806173b0;    // non-SP object-def const (declared batch_604)
extern float FLOAT_806173c8;    // sound-3d amplitude
extern float FLOAT_806173cc;    // shower gate probability
extern float FLOAT_806173d0, FLOAT_806173d4, FLOAT_806173d8, FLOAT_806173dc, FLOAT_806173e0;
extern float FLOAT_806173e4, FLOAT_806173e8, FLOAT_806173ec, FLOAT_806173f0, FLOAT_806173f4;
extern float FLOAT_806173f8, FLOAT_806173fc, FLOAT_80617400, FLOAT_80617404, FLOAT_80617408;
extern float FLOAT_8061740c;    // ambient-fire height gate

// ---- typed aliases for header-vs-ROM mismatches (batch_605) ----
// anim_event_scan_active / anim_active_clip_slot reuse batch-earlier aliases
// __wr_anim_event_scan (int(void)) and __wr_clip_slot (int*(int)).
// object_slot_switch_notify returns the running packed value at the CPTA site.
static unsigned long long (*const __wr_slot_switch_notify)(double,double,double,unsigned long long,
                                                           unsigned long long,unsigned long long,
                                                           unsigned long long,unsigned long long)
    = (unsigned long long(*)(double,double,double,unsigned long long,unsigned long long,
                             unsigned long long,unsigned long long,unsigned long long))
      &object_slot_switch_notify;
// object_slot_deselect returns a value the ROM keeps in f1; call the real fn.
static int (*const __wr_slot_deselect)(void) = (int(*)(void)) &object_slot_deselect;
// level_actors_register at the CDH1 site is the wide-FP register form -> id.
static unsigned long long (*const __wr_actors_register)(double,double,unsigned long long,
                                                        unsigned long long,unsigned long long,
                                                        unsigned long long,unsigned long long,
                                                        unsigned long long,int,void*,int)
    = (unsigned long long(*)(double,double,unsigned long long,unsigned long long,unsigned long long,
                             unsigned long long,unsigned long long,unsigned long long,int,void*,int))
      &level_actors_register;
// level_resource_pool_setup wide-FP form (return ignored).
static void (*const __wr_pool_setup)(unsigned long long,double,unsigned long long,unsigned long long,
                                     unsigned long long,unsigned long long,unsigned long long,
                                     unsigned long long)
    = (void(*)(unsigned long long,double,unsigned long long,unsigned long long,unsigned long long,
               unsigned long long,unsigned long long,unsigned long long))&level_resource_pool_setup;
// character_on_death_init at the impact-scan site is a wide-FP call -> double.
static double (*const __wr_char_on_death)(double,double,double,double,double,double,double,double,int)
    = (double(*)(double,double,double,double,double,double,double,double,int))&character_on_death_init;
} // extern "C"

// ===========================================================================
// Address: 0x801d98f0 | Name: worm_cdh1_level_state_map | Date: 2026-09-07
// Batch: batch_605
// ---------------------------------------------------------------------------
// Maps the current level id (context +0xdec) to a CDH1 state-class code used by
// the control block (+0x674). Story levels fall into 5 broad buckets (0..4);
// everything else returns 0 (inactive).
// ===========================================================================
int worm_cdh1_level_state_map(void)
{
    switch (*(int *)(DAT_80611d74 + 0xdec)) {
    default:
        return 0;
    case 1:  case 5:  case 7:  case 9:  case 10: case 0xb: case 0xd: case 0xe:
    case 0xf: case 0x10: case 0x14: case 0x15: case 0x16: case 0x17: case 0x18:
    case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1f: case 0x20:
    case 0x21: case 0x22: case 0x27: case 0x28: case 0x29: case 0x2f: case 0x33:
    case 0x5c:
        return 4;
    case 0x3e: case 0x3f: case 0x40: case 0x55: case 0x58:
        return 1;
    case 0x41: case 0x42:
        return 2;
    case 0x44: case 0x45: case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4d:
    case 0x51: case 0x52: case 0x53:
        return 3;
    }
}

// ===========================================================================
// Address: 0x801d9940 | Name: worm_cdh1_shower_update | Date: 2026-09-07
// Batch: batch_605
// ---------------------------------------------------------------------------
// Spark-shower + falling-debris sub-update for CDH1. Refreshes the shared
// state-class (+0x674) and eases the shower intensity (+0x680). When enabled
// and in state 1/2: on an rng gate it spawns a spark projectile from the 'PLNE'
// rig into a 5-slot ring (+0x684..), then integrates each live ring slot,
// firing a scaled explosion (id 0xf) with a normalized offset and playing a
// one-shot approach sound (0xc6f). In state 1 it additionally plays impact
// sounds (0x1a6/0x1a7), applies camera shake and stores the shake into the
// player context. Walks the subobj linked-body list to run character on-death
// on low bodies. Drives two looping 3D sources, and in state 4 runs the
// falling-object particle (gravity + ground raycast + landing sounds/shake).
// ===========================================================================
void worm_cdh1_shower_update(void)
{
    __save_gpr_28();

    *(int *)(DAT_80612688 + 0x674) = worm_cdh1_level_state_map();

    // ease shower intensity toward table value keyed by state-class
    float target = *(float *)(&DAT_804512dc + *(int *)(DAT_80612688 + 0x674) * 4);
    scalar_step_toward_snap((double)(-target), (double)FLOAT_8061722c,
                            (float *)(DAT_80612688 + 0x680));

    bool armed = false;
    if ((DAT_80610d64 != 0) &&
        ((*(int *)(DAT_80612688 + 0x674) == 2) || (*(int *)(DAT_80612688 + 0x674) == 1))) {
        DAT_80612674 = DAT_80612674 + DAT_806108e4;
        int halfFrame = ((DAT_806109c0 ^ 2) >> 1) - (((DAT_806109c0 ^ 2) & DAT_806109c0) >> 0x1f);
        if (halfFrame < DAT_80612674) {
            float r = (float)((double)(int)__wr_rng_tick() / FLOAT_80617240);
            if (FLOAT_80617228 < r) {
                int colBase = DAT_80612678 * 0xc;
                int plne = FUN_8023c77c(0x504c4e45);   // 'PLNE'
                if ((plne != 0) && (*(int *)(plne + 4) != 0) &&
                    (*(int *)(*(int *)(plne + 4) + 0x10) != 0)) {
                    DAT_80612674 = 0;
                    float dir[3];
                    dir[0] = DAT_803df648;
                    dir[1] = DAT_803df64c;
                    dir[2] = DAT_803df650;
                    mtx4_transform_vector(*(float **)(*(int *)(plne + 4) + 0x10), dir);

                    int base = DAT_80612688 + *(int *)(DAT_80612688 + 0x760) * 0x2c;
                    *(float *)(base + 0x69c) = (float)((double)dir[0] * DOUBLE_806173b8);
                    *(float *)(base + 0x6a0) = (float)((double)dir[1] * DOUBLE_806173b8);
                    *(float *)(base + 0x6a4) = (float)((double)dir[2] * DOUBLE_806173b8);
                    *(float *)(base + 0x684) =
                        *(float *)(plne + 0x64) + (float)((double)dir[0] * DOUBLE_806173c0);
                    *(float *)(base + 0x688) =
                        *(float *)(plne + 0x68) + (float)((double)dir[1] * DOUBLE_806173c0);
                    *(float *)(base + 0x68c) =
                        *(float *)(plne + 0x6c) + (float)((double)dir[2] * DOUBLE_806173c0);
                    *(int *)(base + 0x690) = *(int *)(&DAT_804512f0 + colBase);
                    *(int *)(base + 0x694) = *(int *)((&DAT_804512f0 + 4) + colBase);
                    *(int *)(base + 0x698) = *(int *)((&DAT_804512f0 + 8) + colBase);
                    *(int *)(base + 0x6a8) = 5;
                    *(int *)(base + 0x6ac) = 0;
                    *(int *)(DAT_80612688 + 0x760) =
                        (*(int *)(DAT_80612688 + 0x760) + 1) % 5;
                }
            }
        }
        // integrate live ring slots
        float gate = (float)((double)(int)__wr_rng_tick() / FLOAT_80617240);
        armed = (double)FLOAT_80617168 < (double)gate;
        for (int i = 0; i < 5; i++) {
            int slot = DAT_80612688 + i * 0x2c;
            if (*(int *)(slot + 0x6a8) == 0) {
                continue;
            }
            if (*(int *)(slot + 0x6ac) != 0) {
                *(int *)(slot + 0x6ac) = *(int *)(slot + 0x6ac) + -1;
                continue;
            }
            *(int *)(slot + 0x6ac) = 10;
            *(int *)(slot + 0x6a8) = *(int *)(slot + 0x6a8) + -1;

            float pos[3];
            pos[0] = *(float *)(slot + 0x684);
            pos[1] = *(float *)(slot + 0x688);
            pos[2] = *(float *)(slot + 0x68c);
            pos[0] += FLOAT_80617224 *
                      ((float)((double)(int)__wr_rng_tick() / FLOAT_80617240) - FLOAT_80617168);
            pos[1] += FLOAT_80617224 *
                      ((float)((double)(int)__wr_rng_tick() / FLOAT_80617240) - FLOAT_80617168);
            pos[2] += FLOAT_80617224 *
                      ((float)((double)(int)__wr_rng_tick() / FLOAT_80617240) - FLOAT_80617168);

            float ofs[3];
            ofs[0] = pos[0] - *(float *)(slot + 0x690);
            ofs[1] = pos[1] - *(float *)(slot + 0x694);
            ofs[2] = pos[2] - *(float *)(slot + 0x698);
            float lenSq = ofs[2] * ofs[2] + (ofs[0] * ofs[0] + ofs[1] * ofs[1]);
            if ((double)FLOAT_806171cc < (double)lenSq) {
                float inv = 1.0f / SQRT((double)lenSq);
                inv = inv * -(FLOAT_80617168 * lenSq * inv * inv - FLOAT_806171d0);
                inv = inv * -(FLOAT_80617168 * lenSq * inv * inv - FLOAT_806171d0);
                float scale = FLOAT_806171ac * inv;
                ofs[0] = ofs[0] * scale;
                ofs[1] = ofs[1] * scale;
                ofs[2] = ofs[2] * scale;
            }
            player_ai_explosion_spawn(0xf, 0xffffffff, pos, ofs, 0, 0, 1);
            *(float *)(slot + 0x684) += *(float *)(slot + 0x69c);
            *(float *)(slot + 0x688) += *(float *)(slot + 0x6a0);
            *(float *)(slot + 0x68c) += *(float *)(slot + 0x6a4);

            if (!armed) {
                armed = true;
                float vol = (float)((double)(FLOAT_806173c8 * *(float *)(DAT_80612688 + 0x680)) *
                                    (double)((float)((double)(int)__wr_rng_tick() / FLOAT_80617240)) +
                                    (double)(FLOAT_806173c8 * *(float *)(DAT_80612688 + 0x680)));
                float pitch = (float)((double)FLOAT_806171b0 *
                                      (double)((float)((double)(int)__wr_rng_tick() / FLOAT_80617240) -
                                               FLOAT_80617168) + (double)FLOAT_806171ac);
                int params[6];
                sound_init_play_params((int)params);
                params[0] = 0xc6f;
                *(float *)&params[4] = pitch;   // +0x10 (local_88)
                *(float *)&params[5] = vol;     // +0x14 (local_84)
                sound_play_request(params);
            }
        }
    }

    if (*(int *)(DAT_80612688 + 0x674) == 1) {
        float gate = (float)((double)(int)__wr_rng_tick() / FLOAT_80617240);
        if (((double)FLOAT_806173cc < (double)gate) &&
            (*(int *)(DAT_80611d74 + 0x114) != 0x100) &&
            (*(int *)(DAT_80611d74 + 8) != 0) &&
            ((double)*(float *)(*(int *)(DAT_80611d74 + 8) + 0x398) <= (double)FLOAT_80617194)) {
            float amp = FLOAT_806172c0 *
                        ((float)((double)(int)__wr_rng_tick() / FLOAT_80617240)) + FLOAT_80617198;
            int params[6];
            sound_init_play_params((int)params);
            params[0] = 0x1a6;
            *(float *)&params[6] = FLOAT_80617168 * amp;   // local_b4 (+0x18)
            sound_play_request(params);

            float g2 = (float)((double)(int)__wr_rng_tick() / FLOAT_80617240);
            if (FLOAT_80617328 < g2) {
                int rid = (int)((double)DAT_806109c0);
                int p2[7];
                sound_init_play_params((int)p2);
                p2[0] = 0x1a7;
                p2[5] = rid;                    // local_f8 (+0x14)
                *(float *)&p2[8] = FLOAT_80617168;  // local_e4 (+0x20)
                sound_play_request(p2);
            }
            player_apply_camera_shake((double)FLOAT_80617218,
                                      (double)(float)((double)FLOAT_806171b0 * (double)amp),
                                      (double)(float)((double)FLOAT_806171b8 * (double)amp),
                                      DAT_80611d74);
            *(float *)(DAT_80611d74 + 0xe54) = (float)((double)FLOAT_80617208 * (double)amp);
            *(float *)(DAT_80611d74 + 0xe58) = (float)((double)FLOAT_80617218 * (double)amp);
        }
    }

    // subobj linked-body impact scan: run character on-death on low bodies
    for (int *node = (int *)DAT_806121e4; node != (int *)0; node = (int *)*node) {
        int origin = subobj_get_contact_origin(((unsigned int *)node)[0x12]);
        if ((double)*(float *)(origin + 4) < (double)FLOAT_806172a4) {
            for (int j = 0; j < 4; j++) {
                unsigned int h = *(unsigned int *)((char *)node + j * 0x34 + 0x70);
                if (h != 0) {
                    int rec = DAT_80611c24 + (h >> 0x16) * 0x2e8;
                    if (*(unsigned int *)(rec + 0x2c) != h) {
                        *(int *)((char *)node + j * 0x34 + 0x70) = 0;
                        rec = 0;
                    }
                    __wr_char_on_death(0, 0, 0, 0, 0, 0, 0, 0, rec);
                }
            }
        }
    }

    sound_update_3d_source((double)*(float *)(DAT_80612688 + 0x680), (double)FLOAT_80617290,
                           (double)FLOAT_806171ac, DAT_80612688 + 0x7b4, 0, 3, 0);
    sound_update_3d_source((double)*(float *)(DAT_80612688 + 0x7c0), (double)FLOAT_80617290,
                           (double)FLOAT_806171ac, DAT_80612688 + 0x7bc, 0, 0x21, 0);

    if (*(int *)(DAT_80612688 + 0x674) != 4) {
        ParticleEmitter **e = (ParticleEmitter **)(DAT_80612688 + 0x4f8);
        particle_cached_disable(e);
        __restore_gpr_28();
        return;
    }

    // state 4: driven falling-debris particle
    float rayDir[3];
    rayDir[0] = (float)DAT_803df654;
    rayDir[1] = (float)DAT_803df658;
    rayDir[2] = (float)DAT_803df65c;
    {
        ParticleEmitter **e = (ParticleEmitter **)(DAT_80612688 + 0x4f8);
        particle_cached_enable(e);
        particle_cached_set_gravity((double)FLOAT_806171ac, e);
    }
    float vel[3];
    vel[0] = *(float *)(DAT_80611d74 + 0xc0);
    vel[1] = FLOAT_80617194;
    vel[2] = *(float *)(DAT_80611d74 + 0xc8);
    float velSq = vel[2] * vel[2] + vel[0] * vel[0] + vel[1] * vel[1];
    if (FLOAT_806171cc < velSq) {
        float inv = 1.0f / SQRT((double)velSq);
        inv = inv * -(FLOAT_80617168 * velSq * inv * inv - FLOAT_806171d0);
        inv = FLOAT_806171ac * inv * -(FLOAT_80617168 * velSq * inv * inv - FLOAT_806171d0);
        vel[0] = vel[0] * inv;
        vel[1] = FLOAT_80617194 * inv;
        vel[2] = vel[2] * inv;
    }
    vel[0] = vel[0] * FLOAT_80617268;
    vel[1] = vel[1] * FLOAT_80617268;
    vel[2] = vel[2] * FLOAT_80617268;

    float dstPos[3];
    dstPos[0] = ((*(float *)(DAT_80611d74 + 0xb4) + *(float *)(DAT_80611d74 + 0xb4)) -
                 *(float *)(DAT_80611d74 + 0x158)) + vel[0];
    dstPos[1] = ((*(float *)(DAT_80611d74 + 0xb8) + *(float *)(DAT_80611d74 + 0xb8)) -
                 *(float *)(DAT_80611d74 + 0x15c)) + vel[1];
    dstPos[2] = ((*(float *)(DAT_80611d74 + 0xbc) + *(float *)(DAT_80611d74 + 0xbc)) -
                 *(float *)(DAT_80611d74 + 0x160)) + vel[2];

    float hitPos[3];
    float hitNrm;
    scene_raycast(*(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x34), (int)(DAT_80611d74 + 0xb4),
                  rayDir, 1, hitPos, &hitNrm, (int *)0, 0xfffffffd, 0, (int *)0);
    dstPos[1] = vel[1] - FLOAT_806171b0;
    {
        ParticleEmitter **e = (ParticleEmitter **)(DAT_80612688 + 0x4f8);
        particle_cached_set_position(e, (Vec3 *)dstPos);
    }
    float dy = vel[1] - *(float *)(DAT_80611d74 + 0xb8);
    bool fastFall = false;
    if ((FLOAT_80617224 < dy) || (dy < FLOAT_806171b0) ||
        (FLOAT_80617294 < FLOAT_806171ac + dstPos[3 - 3 + 2] /*local_170 sentinel*/)) {
        ParticleEmitter **e = (ParticleEmitter **)(DAT_80612688 + 0x4f8);
        if ((double)FLOAT_80617220 < (double)particle_cached_get_speed(e)) {
            particle_cached_set_gravity((double)FLOAT_80617194, e);
            fastFall = true;
        }
    }
    if (!fastFall) {
        ParticleEmitter **e = (ParticleEmitter **)(DAT_80612688 + 0x4f8);
        float spd = particle_cached_get_speed(e);
        float thr = (FLOAT_806171ac / (float)((double)DAT_806109c0)) * (float)((double)DAT_806108e4);
        if (((double)spd < (double)thr) &&
            (FLOAT_80617194 != *(float *)(*(int *)(DAT_80612688 + 0x4f8) + 0x70))) {
            int params[6];
            sound_init_play_params((int)params);
            params[0] = 0x1a6;
            *(float *)&params[10] = FLOAT_80617288;   // local_114
            sound_play_request(params);
        }
        spd = particle_cached_get_speed(e);
        if (((double)spd < (double)FLOAT_80617198) &&
            (FLOAT_80617194 != *(float *)(*(int *)(DAT_80612688 + 0x4f8) + 0x70))) {
            player_broadcast_camera_shake(FLOAT_806171b0, FLOAT_806171b0, FLOAT_806171b0);
        }
    }
    __restore_gpr_28();
}

// ===========================================================================
// Address: 0x801da584 | Name: worm_cdh1_particle_defs_build | Date: 2026-09-07
// Batch: batch_605
// ---------------------------------------------------------------------------
// Builds the CDH1 set-piece particle emitter/def chain into the shared control
// block (dust puffs, smoke, spark billboards, debris) then chains into
// worm_neps_dust_defs_build. All values are the ROM's constant-pool tuning
// floats; the first emitter of each pair is created, then its def is filled
// (size/colour/velocity keyframes), then a child emitter references it.
// ===========================================================================
void worm_cdh1_particle_defs_build(void)
{
    __save_gpr_28();

    float scaleA[3];   // local_1c/18/14
    scaleA[0] = DAT_803df660;
    scaleA[1] = (float)DAT_803df664;
    scaleA[2] = (float)DAT_803df668;
    float scaleB[3];   // local_28/24/20
    scaleB[0] = (float)DAT_803df66c;
    scaleB[1] = (float)DAT_803df670;
    scaleB[2] = (float)DAT_803df674;
    float scaleC[3];   // local_34/30/2c
    scaleC[0] = (float)DAT_803df678;
    scaleC[1] = (float)DAT_803df67c;
    scaleC[2] = (float)DAT_803df680;

    particle_emitter_create((uint64_t)DAT_80612688 + 0x474, (double)FLOAT_8061719c,
                            (double)FLOAT_806171b0, DAT_80612688 + 0x21c, 0, 0, 0);
    particle_emitter_set_color_range((double)FLOAT_806171ac, (double)FLOAT_80617194,
                                     (double)FLOAT_806173d0, (double)FLOAT_80617194);
    particle_emitter_set_lifetime_range((double)FLOAT_806171ac, (double)FLOAT_80617194);
    particle_def_create((uint64_t)DAT_80612688 + 0x500, FLOAT_806171ac, 0, 0, 1, 0x326, 0, 0);
    particle_def_set_size_keyframes((double)FLOAT_806171ac, (double)FLOAT_80617194,
                                    (double)FLOAT_80617194, (double)FLOAT_80617208,
                                    0xffa00000, 0x202020ff, 0);
    particle_def_set_gravity((double)FLOAT_8061719c, 0x7f);
    particle_def_set_size_start_end((double)FLOAT_80617170, (double)FLOAT_80617340,
                                    (double)FLOAT_806171b0, (double)FLOAT_806171b0);
    particle_def_set_color_end((double)FLOAT_806172f8, (double)FLOAT_80617328,
                               (double)FLOAT_806173d4, (double)FLOAT_80617194);
    particle_def_set_velocity_ranges((double)FLOAT_806171ac, (double)FLOAT_806173d8,
                                     (double)FLOAT_806173dc, (double)FLOAT_806171b0,
                                     (double)FLOAT_806171ac, (double)FLOAT_80617260);
    particle_def_set_drag((double)FLOAT_806173e0);

    particle_emitter_create((uint64_t)DAT_80612688 + 0x5cc, (double)FLOAT_80617194,
                            (double)FLOAT_806172a0, DAT_80612688 + 0x500, 0, 1, 0);
    particle_emitter_set_color_range((double)FLOAT_806171e0, (double)FLOAT_80617194,
                                     (double)FLOAT_80617390, (double)FLOAT_80617194);
    particle_emitter_set_lifetime_range((double)FLOAT_806171e0, (double)FLOAT_80617194);
    particle_def_create((uint64_t)(int)&DAT_805c4780, FLOAT_806171ac, 0, 0, 1, 0x326, 0, 0);
    particle_def_set_size_keyframes((double)FLOAT_806171ac, (double)FLOAT_80617194,
                                    (double)FLOAT_80617194, (double)FLOAT_80617208,
                                    0xffa00000, 0x202020ff, 0);
    particle_def_set_gravity((double)FLOAT_8061719c, 0x7f);
    particle_def_set_size_start_end((double)FLOAT_80617170, (double)FLOAT_80617340,
                                    (double)FLOAT_806171b0, (double)FLOAT_806171b0);
    particle_def_set_color_end((double)FLOAT_806172f8, (double)FLOAT_80617328,
                               (double)FLOAT_806173e4, (double)FLOAT_80617194);
    particle_def_set_velocity_ranges((double)FLOAT_80617268, (double)FLOAT_806173d8,
                                     (double)FLOAT_806173dc, (double)FLOAT_806171b0,
                                     (double)FLOAT_806171ac, (double)FLOAT_80617260);
    particle_def_set_drag((double)FLOAT_806173e0);

    particle_emitter_create((uint64_t)(int)&DAT_805c4420, (double)FLOAT_80617194,
                            (double)FLOAT_806172a0, (int)&DAT_805c4780, 0, 0, 0);
    particle_emitter_set_color_range((double)FLOAT_80617194, (double)FLOAT_80617194,
                                     (double)FLOAT_806171ac, (double)FLOAT_80617194);
    particle_emitter_set_lifetime_range((double)FLOAT_80617260, (double)FLOAT_8061720c);
    particle_def_create((uint64_t)DAT_80612688, FLOAT_806171ac, 0, 0, 1, 0x34f, 0, 0);
    particle_def_set_size_keyframes((double)FLOAT_806171ac, (double)FLOAT_80617194,
                                    (double)FLOAT_80617194, (double)FLOAT_806171ac,
                                    0x41505000, 0x3c463c00, 0x3c2f2f00);
    particle_def_set_velocity_ranges((double)FLOAT_8061720c, (double)FLOAT_80617194,
                                     (double)FLOAT_8061720c, (double)FLOAT_80617194,
                                     (double)FLOAT_806171ac, (double)FLOAT_80617168);
    particle_def_set_size_start_end((double)FLOAT_80617194, (double)FLOAT_806171ac,
                                    (double)FLOAT_80617194, (double)FLOAT_806171ac);
    particle_def_set_color_end((double)FLOAT_80617250, (double)FLOAT_80617194,
                               (double)FLOAT_806173ec, (double)FLOAT_80617260);

    particle_emitter_create((uint64_t)DAT_80612688 + 0xcc, (double)FLOAT_806171ac,
                            (double)FLOAT_806173f0, DAT_80612688, 0, 0, 0);
    particle_emitter_set_velocity_range((double)FLOAT_806171ac, (double)FLOAT_80617194);
    particle_emitter_set_color_range((double)FLOAT_80617290, (double)FLOAT_80617194,
                                     (double)FLOAT_8061725c, (double)FLOAT_80617194);
    particle_emitter_set_lifetime_range((double)FLOAT_80617290, (double)FLOAT_80617194);
    particle_def_create((uint64_t)(int)&DAT_805c484c, FLOAT_806171ac, 3, 0, 1, 0x363, 0, 0);
    particle_def_set_size_keyframes((double)FLOAT_80617198, (double)FLOAT_80617194,
                                    (double)FLOAT_806171b0, (double)FLOAT_8061719c,
                                    0x46504600, 0x46504600, 0);
    particle_def_set_velocity_ranges((double)FLOAT_806171d0, (double)FLOAT_80617194,
                                     (double)FLOAT_80617234, (double)FLOAT_80617194,
                                     (double)FLOAT_806171ac, (double)FLOAT_80617168);

    particle_emitter_create((uint64_t)(int)&DAT_805c46fc, (double)FLOAT_80617194,
                            (double)FLOAT_80617194, (int)&DAT_805c484c, 0, 0, 0);
    particle_emitter_set_color_range((double)FLOAT_80617290, (double)FLOAT_80617194,
                                     (double)FLOAT_80617268, (double)FLOAT_80617194);
    particle_emitter_set_lifetime_range((double)FLOAT_80617290, (double)FLOAT_80617194);
    particle_def_create((uint64_t)DAT_80612688 + 0x21c, FLOAT_806171ac, 0, 1, 1, 0x32e, 0, 0);
    particle_def_set_size_keyframes((double)FLOAT_80617208, (double)FLOAT_80617194,
                                    (double)FLOAT_806171b0, (double)FLOAT_806171b0,
                                    0x5f4a3f00, 0x5f4a3f00, 0x1e0f0a00);
    particle_def_set_velocity_ranges((double)FLOAT_8061722c, (double)FLOAT_80617194,
                                     (double)FLOAT_80617194, (double)FLOAT_80617194,
                                     (double)FLOAT_806171ac, (double)FLOAT_80617168);
    particle_def_set_size_start_end((double)FLOAT_80617170, (double)FLOAT_80617340,
                                    (double)FLOAT_80617194, (double)FLOAT_8061720c);
    particle_def_set_color_end((double)FLOAT_80617288, (double)FLOAT_80617194,
                               (double)FLOAT_806171ac, (double)FLOAT_80617194);
    particle_def_set_drag((double)FLOAT_8061720c);

    particle_emitter_create((uint64_t)DAT_80612688 + 0x36c, (double)FLOAT_80617198,
                            (double)FLOAT_806171b0, DAT_80612688 + 0x21c, 3, 1, 0);
    particle_emitter_set_color_range((double)FLOAT_80617260, (double)FLOAT_80617194,
                                     (double)FLOAT_806173d0, (double)FLOAT_80617218);
    particle_emitter_set_lifetime_range((double)FLOAT_80617194, (double)FLOAT_80617194);
    particle_emitter_set_scale(scaleB[0]);

    particle_emitter_create((uint64_t)DAT_80612688 + 0x3f0, (double)FLOAT_80617198,
                            (double)FLOAT_80617168, DAT_80612688 + 0x21c, 4, 1, 0);
    particle_emitter_set_color_range((double)FLOAT_80617194, (double)FLOAT_80617194,
                                     (double)FLOAT_806173e0, (double)FLOAT_80617198);
    particle_emitter_set_lifetime_range((double)FLOAT_80617194, (double)FLOAT_80617194);
    particle_emitter_set_scale(scaleC[0]);

    particle_def_create((uint64_t)DAT_80612688 + 0x150, FLOAT_806171ac, 0, 0, 1, 0x32d, 0, 0);
    particle_def_set_size_keyframes((double)FLOAT_806171ac, (double)FLOAT_80617194,
                                    (double)FLOAT_80617194, (double)FLOAT_806171ac,
                                    0x46463c80, 0x46463c00, 0x5050500);
    particle_def_set_velocity_ranges((double)FLOAT_806171d0, (double)FLOAT_80617194,
                                     (double)FLOAT_806171d0, (double)FLOAT_80617194,
                                     (double)FLOAT_806171ac, (double)FLOAT_80617168);
    particle_def_set_size_start_end((double)FLOAT_80617194, (double)FLOAT_8061721c,
                                    (double)FLOAT_80617194, (double)FLOAT_80617268);
    particle_def_set_color_end((double)FLOAT_80617250, (double)FLOAT_80617194,
                               (double)FLOAT_806171d0, (double)FLOAT_80617168);
    particle_def_set_drag((double)FLOAT_8061725c);

    particle_emitter_create((uint64_t)DAT_80612688 + 0x2e8, (double)FLOAT_806171ac,
                            (double)FLOAT_806173f0, DAT_80612688 + 0x150, 3, 0x1080, 0);
    particle_emitter_set_scale(scaleA[0]);
    particle_emitter_set_color_range((double)FLOAT_80617198, (double)FLOAT_80617194,
                                     (double)FLOAT_806173f4, (double)FLOAT_80617194);
    particle_emitter_set_lifetime_range((double)FLOAT_806171a0, (double)FLOAT_80617200);
    particle_emitter_spawn(DAT_80612688 + 0x4f8, DAT_80612688 + 0x2e8, 0, scaleA, (float *)0);
    {
        ParticleEmitter **e = (ParticleEmitter **)(DAT_80612688 + 0x4f8);
        particle_cached_disable(e);
    }

    particle_def_create((uint64_t)(int)&DAT_805c5c74, FLOAT_806171ac, 0, 0, 1, 0x326, 0, 0);
    particle_def_set_size_keyframes((double)FLOAT_806171ac, (double)FLOAT_80617194,
                                    (double)FLOAT_80617194, (double)FLOAT_80617288,
                                    0x101010ff, 0x404040ff, 0);
    particle_def_set_size_start_end((double)FLOAT_80617170, (double)FLOAT_80617340,
                                    (double)FLOAT_806171b0, (double)FLOAT_806171b0);
    particle_def_set_color_end((double)FLOAT_806172f8, (double)FLOAT_80617328,
                               (double)FLOAT_806173d4, (double)FLOAT_80617194);
    particle_def_set_velocity_ranges((double)FLOAT_80617260, (double)FLOAT_806173d8,
                                     (double)FLOAT_806171e0, (double)FLOAT_806171b0,
                                     (double)FLOAT_806171ac, (double)FLOAT_80617260);
    particle_def_set_drag((double)FLOAT_80617200);

    particle_emitter_create((uint64_t)(int)&DAT_805c5174, (double)FLOAT_80617194,
                            (double)FLOAT_806172a0, (int)&DAT_805c5c74, 0, 0x800, 0);
    particle_emitter_set_color_range((double)FLOAT_8061725c, (double)FLOAT_80617194,
                                     (double)FLOAT_806173f8, (double)FLOAT_80617194);
    particle_def_create((uint64_t)(int)&DAT_805c4918, FLOAT_806171ac, 0, 0, 1, 0x326, 0, 0);
    particle_def_set_size_keyframes((double)FLOAT_80617218, (double)FLOAT_80617194,
                                    (double)FLOAT_80617194, (double)FLOAT_80617168,
                                    0x70707f00, 0x7f7f7fff, 0);
    particle_def_set_size_start_end((double)FLOAT_80617170, (double)FLOAT_80617340,
                                    (double)FLOAT_806171b0, (double)FLOAT_806171b0);
    particle_def_set_color_end((double)FLOAT_806172f8, (double)FLOAT_80617328,
                               (double)FLOAT_806173fc, (double)FLOAT_80617194);
    particle_def_set_velocity_ranges((double)FLOAT_80617194, (double)FLOAT_80617194,
                                     (double)FLOAT_8061720c, (double)FLOAT_806171b0,
                                     (double)FLOAT_806171ac, (double)FLOAT_80617260);
    particle_def_set_drag((double)FLOAT_8061725c);

    particle_emitter_create((uint64_t)(int)&DAT_805c45f4, (double)FLOAT_80617194,
                            (double)FLOAT_806172a0, (int)&DAT_805c4918, 0, 0, 0);
    particle_emitter_set_color_range((double)FLOAT_806171e0, (double)FLOAT_80617194,
                                     (double)FLOAT_806172c4, (double)FLOAT_80617194);
    particle_emitter_set_lifetime_range((double)FLOAT_806171e0, (double)FLOAT_80617194);
    particle_def_create((uint64_t)(int)&DAT_805c49e4, FLOAT_806171ac, 0, 0, 0x11, 0x34e, 0, 0);
    particle_def_set_size_keyframes((double)FLOAT_806171ac, (double)FLOAT_80617194,
                                    (double)FLOAT_80617194, (double)FLOAT_80617218,
                                    0x404060ff, 0x606070ff, 0x101010ff);
    particle_def_set_size_start_end((double)FLOAT_80617170, (double)FLOAT_80617340,
                                    (double)FLOAT_806171b0, (double)FLOAT_806171b0);
    particle_def_set_color_end((double)FLOAT_806172f8, (double)FLOAT_80617220,
                               (double)FLOAT_80617400, (double)FLOAT_80617194);
    particle_def_set_velocity_ranges((double)FLOAT_80617404, (double)FLOAT_8061722c,
                                     (double)FLOAT_80617408, (double)FLOAT_8061722c,
                                     (double)FLOAT_80617268, (double)FLOAT_8061719c);

    particle_emitter_create((uint64_t)(int)&DAT_805c4678, (double)FLOAT_80617194,
                            (double)FLOAT_806172a0, (int)&DAT_805c49e4, 0, 0, 0);
    particle_emitter_set_color_range((double)FLOAT_806173e0, (double)FLOAT_80617194,
                                     (double)FLOAT_80617218, (double)FLOAT_80617194);
    particle_emitter_set_lifetime_range((double)FLOAT_806173e0, (double)FLOAT_80617194);

    worm_neps_dust_defs_build();
    __restore_gpr_28();
}

// ===========================================================================
// Address: 0x801dae6c | Name: worm_cdh1_tank_break_event | Date: 2026-09-07
// Batch: batch_605
// ---------------------------------------------------------------------------
// Anim-event break handler for the CDH1 'TANK'/'CMDR' linked prop. If the
// self's linked entity (+0x158) matches 'TANK' it forwards to the linked-break
// dispatch (FUN_8024dd80); if it matches 'CMDR' (once, guarded by DAT_805c4ac0)
// it flags the linked body, fires an explosion (id 0x11), kills attached
// particles, latches the one-shot, and fires the scripted payload.
// ===========================================================================
void worm_cdh1_tank_break_event(unsigned long long p1, unsigned long long p2, double p3,
                                unsigned long long p4, unsigned long long p5, unsigned long long p6,
                                unsigned long long p7, unsigned long long p8, int self)
{
    if (*(int *)(self + 0x158) == 0) {
        return;
    }
    int tank = FUN_8023c32c(0x54414e4b);   // 'TANK'
    if (*(int *)(self + 0x158) == tank) {
        FUN_8024dd80((double)p1, (double)p2, p3, (double)p4, (double)p5, (double)p6,
                     (double)p7, (double)p8, *(int *)(self + 0x158), self);
        return;
    }
    int cmdr = FUN_8023c32c(0x434d4452);   // 'CMDR'
    if ((*(int *)(self + 0x158) == cmdr) && (DAT_805c4ac0 == 0)) {
        *(int *)(*(int *)(*(int *)(self + 0x158) + 4) + 0x30) = 2;
        player_ai_explosion_spawn(0x11, 0, (float *)(self + 0x1c), (float *)(self + 0x180), 0, 0, 1);
        particle_kill_by_entity();
        DAT_805c4ac0 = 1;
        FUN_80221074((double)FLOAT_80617194, (double)p2, p3, (double)p4, (double)p5, p6, p7, p8);
    }
}

// ===========================================================================
// Address: 0x801daf48 | Name: worm_cdh1_srt2_gate | Date: 2026-09-07
// Batch: batch_605
// ---------------------------------------------------------------------------
// Fires the scripted payload once the 'SRT2' entity has been cleared (resolve
// returns 0).
// ===========================================================================
void worm_cdh1_srt2_gate(unsigned long long p1, unsigned long long p2, double p3,
                         unsigned long long p4, unsigned long long p5, unsigned long long p6,
                         unsigned long long p7, unsigned long long p8)
{
    (void)p1;
    if (FUN_8023c32c(0x53525432) == 0) {   // 'SRT2'
        FUN_80221074((double)FLOAT_80617194, (double)p2, p3, (double)p4, (double)p5, p6, p7, p8);
    }
}

// ===========================================================================
// Address: 0x801daf84 | Name: worm_cdh1_master_update | Date: 2026-09-07
// Batch: batch_605
// ---------------------------------------------------------------------------
// Per-frame master update for the CDH1 set-piece. Runs the spark-shower
// sub-update and ambient dust, forwards scripted payload fires on the level
// trigger flag (+0x57c) and the intro-timer (+0x1cc), then drives the linked
// 'SOLY'/'LIEU'/'COLO' scientist bots and the 'CPTA' captain by scanning their
// active anim events (setting per-bot walk/idle fields and, for CPTA, handing
// off weapon slot 5 once). Advances the +0x670 gate for level 0x3f, scans each
// player's current zone to fire the matching payload op, and finally toggles
// the 'BTOW' entity's render flag by whether any BTOW slot is in an active
// state.
// ===========================================================================
void worm_cdh1_master_update(unsigned long long p1, unsigned long long p2, double p3,
                             unsigned long long p4, unsigned long long p5, unsigned long long p6,
                             unsigned long long p7, unsigned long long p8)
{
    __save_gpr_28();
    (void)p1;

    int lvlCtx = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);
    worm_cdh1_shower_update();
    worm_ambient_dust_spawn();
    if (*(char *)(lvlCtx + 0x57c) == '\x01') {
        FUN_80221074((double)FLOAT_80617194, (double)p2, p3, (double)p4, (double)p5, p6, p7, p8);
    }
    if ((double)*(float *)(*(int *)(DAT_80611d74 + 0x14) + 0x1cc) <= (double)FLOAT_8061740c) {
        FUN_80221074((double)FLOAT_80617194, (double)p2, p3, (double)p4, (double)p5, p6, p7, p8);
    }

    // linked scientist bots: SOLY / LIEU / COLO walk/idle anim-event fields
    struct { int tag; int clipStart; int clipEnd; } bots[3] = {
        { 0x534f4c59, 0xb61, 0xb62 },   // 'SOLY'
        { 0x4c494555, 0xb5d, 0xb5e },   // 'LIEU'
        { 0x434f4c4f, 0xb5f, 0xb60 },   // 'COLO'
    };
    for (int b = 0; b < 3; b++) {
        int bot = (int)aiactor_find_bot_by_linked_entity(bots[b].tag);
        if ((bot != 0) && (__wr_anim_event_scan() != 0)) {
            int *clip = __wr_clip_slot(bot);
            if (*clip == bots[b].clipStart) {
                int ctx = *(int *)(bot + 0x7c);
                *(int *)(ctx + 0x94) = 0;
                *(char *)(ctx + 0x57c) = 0;
            }
            if (*clip == bots[b].clipEnd) {
                int ctx = *(int *)(bot + 0x7c);
                *(int *)(ctx + 0x94) = 0x30;
                *(char *)(ctx + 0x54c) = 0;
            }
        }
    }

    // CPTA captain: weapon hand-off + anim-event field toggles
    if (((DAT_805c4ac4 == 0) || (*(int *)(DAT_80612688 + 0x668) == 0)) &&
        ((int)aiactor_find_bot_by_linked_entity(0x43505441) != 0)) {   // 'CPTA'
        int cpta = (int)aiactor_find_bot_by_linked_entity(0x43505441);
        int ctx = *(int *)(cpta + 0x7c);
        if (__wr_anim_event_scan() != 0) {
            int *clip = __wr_clip_slot(cpta);
            if ((DAT_805c4ac4 == 0) && (*clip == 0xb0f)) {
                *(int *)(ctx + 0x94) = 7;
                *(char *)(ctx + 0x551) = 0;
            }
            if (*clip == 0xb4f) { *(int *)(ctx + 0x94) = 0x3c; *(char *)(ctx + 0x551) = 0; }
            if (*clip == 0xb58) {
                *(int *)(ctx + 0x94) = 5;
                *(char *)(ctx + 0x588) = 0;
                object_sound_slot_accumulate(ctx, 1, 1000);
            }
            if (*clip == 0xb8c) { *(int *)(ctx + 0x94) = 0; *(char *)(ctx + 0x551) = 0; }
            if (*clip == 0xb8d) {
                *(int *)(ctx + 0x94) = 5;
                *(char *)(ctx + 0x54c) = 0;
                object_sound_slot_accumulate(ctx, 1, 1000);
            }
            if (*clip == 0xb7a) { *(int *)(ctx + 0x94) = 0; *(char *)(ctx + 0x551) = 0; }
        }
        if (__wr_anim_event_scan() != 0) {
            int *clip = __wr_clip_slot(cpta);
            if ((DAT_805c4ac4 == 0) && (*clip == 0xb0f)) {
                int obj = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);
                __wr_slot_switch_notify((double)FLOAT_80617194, (double)p2, p3, p4, p5, p6, p7, p8);
                object_sound_slot_add_scaled(obj, 5, 8);
                __wr_slot_switch_notify((double)FLOAT_80617194, (double)p2, p3, p4, p5, p6, p7, p8);
                object_set_weapon_slot(ctx, 5);
                __wr_slot_deselect();
                DAT_805c4ac4 = 1;
            }
            if ((*(int *)(DAT_80612688 + 0x668) == 0) && (*clip == 0xb57)) {
                *(int *)(DAT_80612688 + 0x668) = 1;
            }
        }
    }

    // level 0x3f: advance intro gate then fire SRT2 payload
    if (*(int *)(DAT_80611d74 + 0xdec) == 0x3f) {
        int g = *(int *)(DAT_80612688 + 0x670);
        if ((0 < g) && (g < 4)) {
            *(int *)(DAT_80612688 + 0x670) = g + 1;
        }
        if (*(int *)(DAT_80612688 + 0x670) == 4) {
            worm_cdh1_srt2_gate(0, p2, p3, p4, p5, p6, p7, p8);
        }
    }

    // per-player zone scan -> fire matching payload op
    for (int i = 0; i < DAT_80612610; i++) {
        int player = *(int *)(DAT_80611d78 + i * 0x141c + 0x14);
        if ((double)FLOAT_80617194 < (double)*(float *)(player + 0x1cc)) {
            int zone = *(int *)(*(int *)(player + 0x7c) + 0x678);
            if (zone == soundpad_find_zone_by_id(0x802)) {
                FUN_802212b4(1);
            }
            if ((zone == soundpad_find_zone_by_id(0xa89)) ||
                (zone == soundpad_find_zone_by_id(0x481))) {
                FUN_802212b4(2);
            }
            if (zone == soundpad_find_zone_by_id(0xae2)) {
                FUN_802212b4(4);
            }
            if ((zone == soundpad_find_zone_by_id(0x9cb)) ||
                (zone == soundpad_find_zone_by_id(0x9e0)) ||
                (zone == soundpad_find_zone_by_id(0xa42))) {
                FUN_802212b4(10);
            }
            soundpad_find_zone_by_id(0x5a0);
            if ((zone == soundpad_find_zone_by_id(0x642)) ||
                (zone == soundpad_find_zone_by_id(0x737))) {
                FUN_80221074((double)FLOAT_80617194, (double)p2, p3, (double)p4, (double)p5, p6, p7, p8);
                *(int *)(DAT_80612688 + 0x670) = 1;
            }
        }
    }

    // BTOW render-flag toggle by any BTOW slot in an active state
    int btow = FUN_8023c32c(0x42544f57);   // 'BTOW'
    if (btow != 0) {
        bool any = false;
        for (int s = 0; (s < DAT_80611d58) && (!any); s++) {
            unsigned int f = *(unsigned int *)(DAT_80611d5c + s * 0xffc + 0x21c);
            if (((f & 0x80000000) != 0) && ((f & 1) != 0)) {
                int st = *(int *)(DAT_80611d5c + s * 0xffc + 0x2c0);
                if ((st == 1) || ((unsigned int)(st - 2) < 2) || (st == 0xc) || (st == 0xff)) {
                    any = true;
                }
            }
        }
        if (any) {
            *(unsigned int *)(btow + 0x28) |= 1;
        } else {
            *(unsigned int *)(btow + 0x28) &= 0xfffffffe;
        }
    }
    __restore_gpr_28();
}

// ===========================================================================
// Address: 0x801db564 | Name: worm_cdh1_payload_fire5 | Date: 2026-09-07
// Batch: batch_605
// ---------------------------------------------------------------------------
// Menu callback: fires scripted payload op 5.
// ===========================================================================
void worm_cdh1_payload_fire5(void)
{
    FUN_80220f30(5);
}

// ===========================================================================
// Address: 0x801db588 | Name: worm_cdh1_on_spawn | Date: 2026-09-07
// Batch: batch_605
// ---------------------------------------------------------------------------
// On-spawn dispatcher for the CDH1 cave-in set-piece. Spawns the ground decal,
// hides two scene nodes, clears the shared control block (0x7c4 bytes) and
// initialises its sound handles. On the non-mapmaker path (flags &0x40 == 0) it
// registers the CDH1 level actors + resource pool, arms ~40 soundpad-zone
// reverb/portal flags (0x10000/0x20000) keyed by zone id, builds the big object
// menu (payload entries 0..0x17 with per-entry sounds + build callbacks), and
// seeds the spark emitters/particle defs. The mapmaker path just arms the
// sparks and sets the fallback object def.
// ===========================================================================
void worm_cdh1_on_spawn(unsigned long long p1, double p2, double p3, unsigned long long p4,
                        unsigned long long p5, unsigned long long p6, unsigned long long p7,
                        unsigned long long p8)
{
    decal_spawn_with_entity((int)p1, p2, p3, (int)p4, (int)p5, (int)p6, (int)p7, (int)p8,
                            1, 0x24d, 0, (int *)&DAT_804512fc, (float *)&DAT_80451314);
    scene_node_set_flags(0x5b, 8);
    scene_node_set_flags(0x5a, 8);
    memset_wrapper(DAT_80612688, 0, 0x7c4);
    *(float *)(DAT_80612688 + 0x680) = FLOAT_806171ac;
    *(int *)(DAT_80612688 + 0x7b4) = 0xffffffff;
    *(int *)(DAT_80612688 + 0x7bc) = 0xffffffff;
    *(int *)(DAT_80612688 + 0x7b8) = 0xffffffff;

    if ((DAT_805015c0 & 0x40) != 0) {
        // mapmaker path
        worm_spark_burst_arm();
        worm_cdh1_particle_defs_build();
        *(int *)(DAT_80612688 + 0x678) = 0x1628;
        *(float *)(DAT_80612688 + 0x67c) = FLOAT_806173b0;
        return;
    }

    *(int *)(DAT_80612688 + 0x678) = 0x19b;
    *(float *)(DAT_80612688 + 0x67c) = FLOAT_806173e0;
    *(unsigned int *)(DAT_80612688 + 0x4fc) =
        FUN_80355348((double)FLOAT_80617194, (double)FLOAT_806171bc, 1, &DAT_80450a38, 2);
    __wr_actors_register((double)FLOAT_806171bc, p3, p4, p5, p6, p7, p8, 0, 0, &DAT_80450bfc, 8);
    __wr_actors_register((double)FLOAT_806171bc, p3, p4, p5, p6, p7, p8, 0, 1, &DAT_80450d48, 8);
    __wr_pool_setup(0, (double)FLOAT_806171bc, p4, p5, p6, p7, p8, 0);

    // arm soundpad-zone reverb/portal flags (bit 0x10000/0x20000) by zone id
    static const struct { int zone; unsigned int bit; } zoneFlags[] = {
        {0x9d8,0x20000},{0x9d9,0x10000},{0x9d5,0x20000},{0x9d4,0x10000},{0x9e6,0x10000},
        {0x9e5,0x20000},{0x9ef,0x20000},{0x9f0,0x20000},{0x9f3,0x10000},{0xa08,0x10000},
        {0xa09,0x20000},{0xa0a,0x20000},{0xa0f,0x10000},{0xa10,0x10000},{0xa12,0x20000},
        {0xa3e,0x20000},{0xa3f,0x10000},{0x9ff,0x10000},{0xa40,0x20000},{0x9cc,0x10000},
        {0x9d0,0x20000},{0xa39,0x10000},{0xa4a,0x20000},{0xa6b,0x10000},{0xa81,0x20000},
        {0xa82,0x20000},{0xa83,0x10000},{0xa85,0x20000},{0xa86,0x20000},{0xa87,0x20000},
        {0xa9a,0x20000},{0xa9b,0x20000},{0x9c0,0x10000},{0xaa3,0x20000},{0xaa4,0x20000},
        {0xaa6,0x20000},{0xaaa,0x20000},{0xaac,0x20000},{0xabe,0x20000},{0xb25,0x20000},
    };
    for (int i = 0; i < (int)(sizeof(zoneFlags) / sizeof(zoneFlags[0])); i++) {
        int node = DAT_80611b8c + soundpad_find_zone_by_id(zoneFlags[i].zone) * 0x20;
        *(unsigned int *)(node + 4) |= zoneFlags[i].bit;
    }

    // build the object payload menu
    FUN_802215a8(1, 0xee8, 0, 0, 0);  FUN_8022161c(0x480);  FUN_8022126c(DAT_806128a0);
    FUN_802215a8(10, 0xeea, 0, 0, 0); FUN_8022126c(DAT_806128a0);
    FUN_802215a8(2, 0xee9, 0, 0, 0);  FUN_8022126c(DAT_806128a0);
    FUN_802215a8(3, 0xeeb, 0, 0, 0);  FUN_8022161c(0x95e);  FUN_8022126c(DAT_806128a0);
    FUN_802215a8(4, 0xeec, 0, 0, 0);  FUN_8022161c(0x991);  FUN_8022126c(DAT_806128a0);
    FUN_802215a8(5, 0xeed, 0, 0, 0);  FUN_802215cc(0x281);
    FUN_802217d4((void *)&FUN_80251f8c);                   FUN_8022126c(DAT_806128a0);
    if ((DAT_805015bd != '\n') || (DAT_80612610 < 2)) {
        FUN_802215a8(6, 0xeee, 0, 0, 0); FUN_8022161c(0x7b5);
    }
    if ((DAT_805015bd != '\n') || (DAT_80612610 < 2)) {
        FUN_802215a8(7, 0xeef, 1, 1, 1);
        FUN_802215cc(0x29b); FUN_802215cc(0x2a1); FUN_802215cc(0x2a2);
        FUN_802217d4((void *)&FUN_801dc8bc);               FUN_8022126c(DAT_806128a0);
    }
    FUN_802215a8(8, 0, 2, 2, 2);
    FUN_802215a8(9, 0, 2, 2, 2);  FUN_8022161c(0xb07);
    FUN_802215a8(0x12, 0, 2, 2, 2);
    FUN_802215a8(0x13, 0, 2, 2, 2);
    FUN_802215a8(0xb, 0, 2, 2, 2);
    FUN_802215a8(0xc, 0, 2, 2, 2);
    FUN_802215a8(0xd, 0, 2, 2, 2);
    FUN_802215a8(0xe, 0, 2, 2, 2);
    FUN_802215a8(0xf, 0, 2, 2, 2);  FUN_8022161c(0x59c);
    FUN_802215a8(0, 0, 2, 2, 2);
    FUN_802215cc(0x254); FUN_802215cc(0x253); FUN_802215cc(0x27c); FUN_802215cc(0x256);
    FUN_802217d4((void *)&FUN_801dc814);                   FUN_8022126c(DAT_806128a0);
    FUN_802215a8(0x14, 0, 2, 2, 2);
    FUN_802215a8(0x11, 0, 2, 2, 2);
    FUN_802215cc(0x280); FUN_802215cc(599); FUN_802215cc(0x276); FUN_802215cc(0x293);
    FUN_802217d4((void *)&FUN_801dc9a0);                   FUN_8022126c(DAT_806128a0);
    FUN_80221bdc(&DAT_805c4ab0, 0x78);
    FUN_80221be8();
    DAT_805c4ab4 = 3;
    *(int *)(DAT_80612688 + 0x668) = 0;
    FUN_802215a8(0x15, 0, 2, 2, 2);
    FUN_802215a8(0x16, 0, 2, 2, 2);
    FUN_802215a8(0x17, 0, 2, 2, 2);
    worm_spark_emitter_seed();
    worm_spark_burst_arm();
    worm_cdh1_particle_defs_build();
}

// ===========================================================================
// Address: 0x801dbf5c | Name: worm_wnlf_render_flags | Date: 2026-09-07
// Batch: batch_605
// ---------------------------------------------------------------------------
// Render-flag predicate for the 'WNLF' waterfall entity: returns its context
// render flags (+0xa0) OR'd with bit 0x80 when the control block's +0x668 latch
// is set.
// ===========================================================================
unsigned int worm_wnlf_render_flags(void)
{
    int wnlf = FUN_8023c32c(0x574e4c46);   // 'WNLF'
    return *(unsigned int *)(*(int *)(wnlf + 0x7c) + 0xa0) |
           (-(unsigned int)(*(int *)(DAT_80612688 + 0x668) != 0) & 0x80);
}

// ===========================================================================
// Address: 0x801dbfa8 | Name: worm_cdh1_register_update | Date: 2026-09-07
// Batch: batch_605
// ---------------------------------------------------------------------------
// Marks an entity's context flag (+0x18 bit1) and registers it for the
// per-frame update pass (mode 3).
// ===========================================================================
void worm_cdh1_register_update(int self)
{
    *(unsigned int *)(self + 0x18) |= 2;
    entity_register_for_update(self, 3, 0);
}
extern char  DAT_805015bd;      // single-player level id byte
// DAT_805015c0 (global gameplay flags) already declared earlier in this file.
extern int   DAT_805c4780;      // dust-puff child particle def B
extern int   DAT_805c484c;      // spark billboard particle def
extern int   DAT_805c5c74;      // smoke particle def
extern int   DAT_805c4918;      // debris smoke particle def
extern int   DAT_805c49e4;      // heavy debris particle def
extern int   DAT_805c4ac0;      // CMDR break one-shot latch// character_on_death_init at the impact-scan site is a wide-FP call -> double.
static double (*const __wr_char_on_death)(double,double,double,double,double,double,double,double,int)
    = (double(*)(double,double,double,double,double,double,double,double,int))&character_on_death_init;
// decal_spawn_with_entity at the ROM site is the 13-arg (wide FP + tables) form.
static void (*const __wr_decal_spawn)(unsigned long long,double,double,unsigned long long,
                                      unsigned long long,unsigned long long,unsigned long long,
                                      unsigned long long,int,int,int,int*,float*)
    = (void(*)(unsigned long long,double,double,unsigned long long,unsigned long long,
               unsigned long long,unsigned long long,unsigned long long,int,int,int,int*,float*))
      &decal_spawn_with_entity;
} // extern "C"    __wr_sound3d((double)*(float *)(DAT_80612688 + 0x680), (double)FLOAT_80617290,
                 (double)FLOAT_806171ac, DAT_80612688 + 0x7b4, 0, 3, 0);
    __wr_sound3d((double)*(float *)(DAT_80612688 + 0x7c0), (double)FLOAT_80617290,
                 (double)FLOAT_806171ac, DAT_80612688 + 0x7bc, 0, 0x21, 0);    scene_raycast(*(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x34), (float *)(DAT_80611d74 + 0xb4),
                  rayDir, 1, hitPos, &hitNrm, (int *)0, 0xfffffffd, 0, (int *)0);    __wr_decal_spawn(p1, p2, p3, p4, p5, p6, p7, p8,
                     1, 0x24d, 0, (int *)&DAT_804512fc, (float *)&DAT_80451314);