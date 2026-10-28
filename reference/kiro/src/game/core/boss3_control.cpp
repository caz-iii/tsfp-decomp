// boss3_control.cpp - lifecycle, allocation, targeting and the render entry for
// the third segmented boss (control block DAT_80612a48; see boss3_tentacle.cpp
// for the tentacle links and boss3_surface.cpp for the energy-surface sim).
//
// This module owns the boss's construction chain and per-frame control logic:
//   * allocation of the 0x64dc-byte control block + one-time GX/material setup
//   * spawn/scene init: build the 3 sub-units, 2 tentacle records, seg record,
//     tuning constants and difficulty/region variant, then boss3_reset()
//   * an angle-segment "pattern" table (10-11 [start,end] arcs in 1/1 degrees,
//     wrapping at 0x168 = 360) and a lookup that maps the current boss angle to
//     the active segment
//   * targeting: for each tracked target, either latch a tentacle grab or apply
//     an attractor pull toward the boss
//   * the top-level render entry (surface + retract beam + 3 tentacle links)
//
// DAT_80612a48 control-block layout (byte offsets, partial - extends the maps in
// boss3_tentacle.cpp / boss3_surface.cpp):
//   +0x00.. (0x8 each)  angle-segment table: [start,end] pairs (int degrees)
//   +0x58.. (0x4 each)  per-segment direction flag
//   +0x84..+0x94        angle-pattern base + current angle (+0xf0) driver state
//   +0x90               active segment index (-1 = none)   +0x94 near-midpoint flag
//   +0x98               difficulty/region variant flag (byte)
//   +0x9c..+0xe0        spawn dir + facing basis + scratch (zeroed on spawn)
//   +0x128.. (0x12c)    per sub-unit block x3 (see boss3_subunit_init)
//   +0x494 / +0x4f4     two tentacle records          +0x554 seg record
//   +0x574..+0x57c      surface centre                 +0x5c4 beam link record
//   +0x580..+0x5b8      variant tuning + run state
//   +0xf30.. (0x1a0)    per-tentacle A blocks x3 (+0xf60/+0x1100/+0x12a0 active)
//
// Functions:
//   boss3_render                (0x8029eb58)  [graphics]
//   boss3_target_grab_or_pull   (0x8029ec74)  [game]
//   boss3_process_targets       (0x8029ecd0)  [game]
//   boss3_angle_pattern_init    (0x8029ed04)  [game]
//   boss3_spawn_desc_init       (0x8029ee24)  [game]
//   boss3_subunit_init          (0x8029ee58)  [game]
//   boss3_spawn                 (0x8029efb0)  [game]
//   boss3_scene_init            (0x8029f22c)  [game]
//   boss3_alloc_and_init        (0x8029f290)  [game]
//   boss3_angle_segment_lookup  (0x8029f30c)  [game]
//
// Address range: 0x8029eb58 - 0x8029f3ef
// Subsystem: game (boss AI) + graphics (render entry)

#include "common.h"

extern "C" {

// --- data globals ----------------------------------------------------------
extern int  DAT_80612a48;             // boss3 control block base (byte-addressed)
extern int  DAT_80612610;             // player/participant count
extern char DAT_805015bd;             // level/mode id
extern char DAT_805015bf;             // difficulty/region variant flag

// --- float / double constants ----------------------------------------------
extern const float FLOAT_80619568, FLOAT_80619574, FLOAT_80619580, FLOAT_80619584;
extern const float FLOAT_8061958c, FLOAT_806195a0, FLOAT_806195a4, FLOAT_806195a8;
extern const float FLOAT_806195b0, FLOAT_806195c4, FLOAT_806195f4, FLOAT_806195f8;
extern const float FLOAT_8061966c, FLOAT_806196d8, FLOAT_8061974c, FLOAT_80619784;
extern const float FLOAT_80619828, FLOAT_80619858, FLOAT_8061985c, FLOAT_80619860;
extern const float FLOAT_80619864, FLOAT_80619868;
extern const double DOUBLE_80619630;  // frame-delta bias
extern const double DOUBLE_80619850;  // beam length base

// --- raw helpers (still FUN_ in the IDB, outside this batch's range) --------
extern unsigned int FUN_802c6970(double a, double b, unsigned int tex, unsigned int col,
                                 float* org, int* link);   // GX ribbon/beam renderer
extern void FUN_802af38c(void);                            // boss3 material setup
extern void FUN_802ad80c(double a, double b, double c, double d);  // render state setup
extern void FUN_802c66bc(int a, int b, unsigned long long* slot);  // beam slot init
extern unsigned int FUN_802af304(double a, double b, double c, double d, double e, double f,
                                 double g, double h, int flag, int p10, int p11, int p12,
                                 int p13, int p14, int p15, int p16);
extern unsigned int FUN_802ad7d8(double a, double b, double c, double d, double e, double f,
                                 double g, double h, int i, int j, int p11, int p12,
                                 int p13, int p14, int p15, int p16);

// --- named cross-module helpers (call-site typed) --------------------------
extern void gx_set_blend_mode(int mode);
extern void gx_set_sub_material_id(int id);
extern unsigned int color_lerp(double t, unsigned int a, unsigned int b);
extern unsigned int sprite_get_texture_id(int slot);
extern void snake3_attractor_nudge(float* rec, int center, int targetPos, int enabled);
extern void snake3_seg_record_init(double a, double b, float* rec);
extern void memset_wrapper(int dst, int val, int len);
extern int  mem_alloc(int size, double b, double c, double d, double e, double f, double g,
                      double h, int tag, int flag, int p11, int p12, int p13, int p14,
                      int p15, int p16);

} // extern "C"

// boss3 helpers defined elsewhere in this module (C++ linkage)
void boss3_surface_render(void);
void boss3_link_render(int link);
void boss3_reset(void);
void boss3_tentacle_init(double phase, float* rec);   // boss3_tentacle.cpp

// same-module forward decls
static void boss3_target_grab_or_pull(int target);
static void boss3_spawn_desc_init(double x, double y, double z, float* rec,
                                  unsigned int c0, unsigned int c1, unsigned int c2,
                                  unsigned int c3);
static void boss3_subunit_init(int index);
static void boss3_spawn(void);
static void boss3_scene_init(void);
static void boss3_angle_pattern_init(void);

// boss3_tentacle_try_grab is defined in boss3_surface.cpp (C++ linkage)
void boss3_tentacle_try_grab(int target);

// store a raw 32-bit word into a float slot without a numeric conversion
static inline void store_bits_f32(float* dst, unsigned int bits) {
    *(unsigned int*)dst = bits;
}

// ===========================================================================
// 0x8029eb58  boss3_render   [graphics]
// Top-level boss render: set the blend mode, and if the surface is lit draw the
// energy surface plus (when retracting) the target-track beam; then draw all 3
// tentacle links.
// ===========================================================================
void boss3_render(void) {
    gx_set_blend_mode(2);
    if (FLOAT_80619568 < *(float*)(DAT_80612a48 + 0x1434)) {
        gx_set_sub_material_id(2);
        boss3_surface_render();
        gx_set_sub_material_id(1);
        int base = DAT_80612a48;
        if (*(int*)(DAT_80612a48 + 0x643c) == 2) {
            double phase = (double)*(float*)(DAT_80612a48 + 0x1438);
            unsigned int col = color_lerp(phase, 0x7f050f32, 0x7f000000);
            unsigned int tex = sprite_get_texture_id(0);
            FUN_802c6970((double)(float)(DOUBLE_80619850 +
                                         (double)(float)((double)FLOAT_806195a4 * phase)),
                         (double)FLOAT_80619580, tex, col,
                         (float*)(base + 0x1410), (int*)(base + 0x5c4));
        } else {
            unsigned int tex = sprite_get_texture_id(0);
            FUN_802c6970((double)FLOAT_806195a4, (double)FLOAT_80619580, tex, 0x7f000000,
                         (float*)(base + 0x1410), (int*)(base + 0x5c4));
        }
    }
    int off = 0;
    for (int i = 0; i < 3; i++) {
        boss3_link_render(DAT_80612a48 + off + 0xf30);
        off += 0x1a0;
    }
}

// ===========================================================================
// 0x8029ec74  boss3_target_grab_or_pull
// Per-target step: if the target's grab cooldown (+0x30) has expired and it is
// not already caught (+0x3c), try to latch a tentacle onto it; otherwise apply
// an attractor pull toward the boss centre (+0xcc).
// ===========================================================================
static void boss3_target_grab_or_pull(int target) {
    if (*(float*)(target + 0x30) <= FLOAT_80619568) {
        if (*(int*)(target + 0x3c) == 0) {
            boss3_tentacle_try_grab(target);
        }
    } else {
        snake3_attractor_nudge((float*)(target + 0x40), DAT_80612a48 + 0xcc, target + 0x24, 1);
    }
}

// ===========================================================================
// 0x8029ecd0  boss3_process_targets
// Run the grab/pull step for the two tracked target slots (+0x4f4, +0x494).
// ===========================================================================
static void boss3_process_targets(void) {
    boss3_target_grab_or_pull(DAT_80612a48 + 0x4f4);
    boss3_target_grab_or_pull(DAT_80612a48 + 0x494);
}

// ===========================================================================
// 0x8029ed04  boss3_angle_pattern_init
// Build the angle-segment table: 10 [start,end] arc pairs (int degrees, wrapping
// at 0x168 = 360) with alternating 30/42-degree steps and a per-segment direction
// flag. Seeds the pattern driver base fields (+0x84..+0x94).
// ===========================================================================
static void boss3_angle_pattern_init(void) {
    *(float*)(DAT_80612a48 + 0x84) = FLOAT_80619858;
    *(float*)(DAT_80612a48 + 0x88) = FLOAT_8061985c;
    *(float*)(DAT_80612a48 + 0x8c) = FLOAT_80619860;
    *(int*)(DAT_80612a48 + 0x90) = 0;
    *(int*)(DAT_80612a48 + 0x94) = 0;

    int off = 0;      // stride 8 into the [start,end] table
    int flagOff = 0;  // stride 4 into the direction-flag table
    int angle = 0x28;
    unsigned int i = 0;
    for (int n = 10; n != 0; n--) {
        if (angle < 0x169) {
            *(int*)(DAT_80612a48 + off) = angle;
            // parity of i selects the step size + direction flag
            if (((i & 1) ^ (i >> 0x1f)) == (i >> 0x1f)) {
                angle += 0x1e;
                *(int*)(DAT_80612a48 + flagOff + 0x58) = 0;
            } else {
                angle += 0x2a;
                *(int*)(DAT_80612a48 + flagOff + 0x58) = 1;
            }
        } else {
            angle -= 0x168;
            *(int*)(DAT_80612a48 + off) = 0x168;
        }
        if (angle < 0x169) {
            *(int*)(DAT_80612a48 + off + 4) = angle;
        } else {
            angle -= 0x168;
            *(int*)(DAT_80612a48 + off + 4) = 0x168;
            *(int*)(DAT_80612a48 + off + 8) = 0;
            *(int*)(DAT_80612a48 + off + 0xc) = angle;
            *(int*)(DAT_80612a48 + flagOff + 4 + 0x58) =
                *(int*)(DAT_80612a48 + flagOff + 4 + 0x54);
            off += 8;
            flagOff += 4;
        }
        i++;
        flagOff += 4;
        off += 8;
    }
}

// ===========================================================================
// 0x8029ee24  boss3_spawn_desc_init
// Initialise a spawn/emitter descriptor (float* rec): position (x,y,z), zero the
// velocity/timer, and store the four packed 32-bit words (colour/flags) into the
// trailing slots [6..9]. The four words arrive as raw bit patterns.
// ===========================================================================
static void boss3_spawn_desc_init(double x, double y, double z, float* rec,
                                  unsigned int c0, unsigned int c1, unsigned int c2,
                                  unsigned int c3) {
    rec[5] = 0.0f;
    rec[0] = (float)x;
    store_bits_f32(&rec[9], c3);
    store_bits_f32(&rec[8], c0);
    store_bits_f32(&rec[7], c1);
    store_bits_f32(&rec[6], c2);
    rec[1] = (float)y;
    rec[2] = (float)z;
    rec[3] = FLOAT_80619568;
    rec[4] = 0.0f;
}

// ===========================================================================
// 0x8029ee58  boss3_subunit_init
// Initialise one boss sub-unit block (index * 0x12c into the control block): set
// up its three spawn descriptors, its tentacle record and clear its state.
// ===========================================================================
static void boss3_subunit_init(int index) {
    int b = index * 0x12c;
    *(float*)(DAT_80612a48 + b + 0x128) = FLOAT_80619568;
    *(float*)(DAT_80612a48 + b + 0x134) = FLOAT_80619568;

    boss3_spawn_desc_init((double)FLOAT_806195a8, (double)FLOAT_806195f4, (double)FLOAT_80619828,
                          (float*)(DAT_80612a48 + b + 0x1c4), 0x2u, 0x2u, 0xau, 0x4u);
    boss3_spawn_desc_init((double)FLOAT_806195a0, (double)FLOAT_80619568, (double)FLOAT_80619568,
                          (float*)(DAT_80612a48 + b + 0x1ec), 0x0u, 0x0u, 0x0u, 0x18u);
    boss3_spawn_desc_init((double)FLOAT_806195a4, (double)FLOAT_80619568, (double)FLOAT_80619568,
                          (float*)(DAT_80612a48 + b + 0x214), 0x0u, 0xffc00000u, 0x0u, 0xcu);

    *(int*)(DAT_80612a48 + b + 0x15c) = 0;
    *(float*)(DAT_80612a48 + b + 0x150) = FLOAT_80619568;
    *(float*)(DAT_80612a48 + b + 0x154) = FLOAT_80619568;
    *(float*)(DAT_80612a48 + b + 0x158) = FLOAT_80619568;
    *(int*)(DAT_80612a48 + b + 0x160) = 0;
    boss3_tentacle_init((double)FLOAT_806195f8, (float*)(DAT_80612a48 + b + 0x164));
    *(float*)(DAT_80612a48 + b + 0x1c0) = FLOAT_806195a8;
    *(float*)(DAT_80612a48 + b + 0x11c) = FLOAT_80619568;
    *(float*)(DAT_80612a48 + b + 0x120) = FLOAT_80619568;
    *(float*)(DAT_80612a48 + b + 0x124) = FLOAT_80619568;
}

// ===========================================================================
// 0x8029efb0  boss3_spawn
// Build the boss instance: zero the facing/position basis, init the two tentacle
// records + seg record, init the 3 sub-units, apply tuning constants + the
// difficulty/region variant, reset the boss (boss3_reset) and enable the tentacle
// active flags.
// ===========================================================================
static void boss3_spawn(void) {
    float z = FLOAT_80619568;
    *(float*)(DAT_80612a48 + 0x9c) = z;
    *(float*)(DAT_80612a48 + 0xa0) = z;
    *(float*)(DAT_80612a48 + 0xa4) = z;
    *(float*)(DAT_80612a48 + 0xc0) = z;
    *(float*)(DAT_80612a48 + 0xc4) = z;
    *(float*)(DAT_80612a48 + 0xc8) = z;
    *(float*)(DAT_80612a48 + 0xb4) = z;
    *(float*)(DAT_80612a48 + 0xb8) = z;
    *(float*)(DAT_80612a48 + 0xbc) = z;
    *(float*)(DAT_80612a48 + 0xcc) = z;
    *(float*)(DAT_80612a48 + 0xd0) = z;
    *(float*)(DAT_80612a48 + 0xd4) = z;
    *(float*)(DAT_80612a48 + 0xd8) = z;
    *(float*)(DAT_80612a48 + 0xdc) = z;
    *(float*)(DAT_80612a48 + 0xe0) = z;
    *(float*)(DAT_80612a48 + 0x574) = z;
    *(float*)(DAT_80612a48 + 0x578) = z;
    *(float*)(DAT_80612a48 + 0x57c) = z;

    boss3_tentacle_init((double)FLOAT_806195f4, (float*)(DAT_80612a48 + 0x494));
    boss3_tentacle_init((double)FLOAT_806195f4, (float*)(DAT_80612a48 + 0x4f4));
    snake3_seg_record_init((double)FLOAT_806196d8, (double)FLOAT_80619574,
                           (float*)(DAT_80612a48 + 0x554));

    for (int i = 0; i < 3; i++) {
        boss3_subunit_init(i);
    }

    *(float*)(DAT_80612a48 + 0x12c) = FLOAT_8061974c;
    *(float*)(DAT_80612a48 + 0x130) = FLOAT_80619568;
    *(float*)(DAT_80612a48 + 0x258) = FLOAT_80619568;
    *(float*)(DAT_80612a48 + 0x25c) = FLOAT_80619864;
    *(float*)(DAT_80612a48 + 0x388) = FLOAT_806195b0;
    *(float*)(DAT_80612a48 + 0x384) = FLOAT_8061958c;
    *(float*)(DAT_80612a48 + 0xe4) = FLOAT_80619568;
    *(float*)(DAT_80612a48 + 0xe8) = FLOAT_80619568;
    *(float*)(DAT_80612a48 + 0xec) = FLOAT_80619568;
    *(float*)(DAT_80612a48 + 0xf0) = FLOAT_80619568;
    *(float*)(DAT_80612a48 + 0xf8) = FLOAT_80619568;
    *(float*)(DAT_80612a48 + 0xfc) = FLOAT_80619568;
    *(float*)(DAT_80612a48 + 0x100) = FLOAT_80619568;
    *(int*)(DAT_80612a48 + 0x590) = 0;
    *(int*)(DAT_80612a48 + 0x5ac) = 0;
    *(float*)(DAT_80612a48 + 0x5b0) = FLOAT_80619568;
    *(int*)(DAT_80612a48 + 0x5b4) = 0;
    *(float*)(DAT_80612a48 + 0x5b8) = FLOAT_80619568;
    *(float*)(DAT_80612a48 + 0x5a8) = FLOAT_80619580;

    if (*(char*)(DAT_80612a48 + 0x98) == '\0') {
        if (DAT_805015bf == '\0') {
            *(float*)(DAT_80612a48 + 0x580) = FLOAT_80619584;
            *(float*)(DAT_80612a48 + 0x584) = FLOAT_80619868;
        } else {
            *(float*)(DAT_80612a48 + 0x580) = FLOAT_806195c4;
            *(float*)(DAT_80612a48 + 0x584) = FLOAT_8061966c;
        }
    }

    *(int*)(DAT_80612a48 + 0x588) = 0;
    *(int*)(DAT_80612a48 + 0x594) = 1;
    *(int*)(DAT_80612a48 + 0x59c) = 0;
    *(float*)(DAT_80612a48 + 0x5a4) = FLOAT_80619580;

    boss3_reset();

    *(int*)(DAT_80612a48 + 0xf60) = 1;
    *(int*)(DAT_80612a48 + 0x1100) = 1;
    *(int*)(DAT_80612a48 + 0x12a0) = 1;
    *(float*)(DAT_80612a48 + 0x6494) = FLOAT_80619568;
    *(float*)(DAT_80612a48 + 0x6498) = FLOAT_80619568;
    memset_wrapper(DAT_80612a48 + 0x6440, 0, 0x54);
}

// ===========================================================================
// 0x8029f22c  boss3_scene_init
// Level entry: clear the variant flag, spawn the boss, build the angle pattern,
// preload the beam texture (sprite id 0x300) and set up the boss material/render
// state.
// ===========================================================================
static void boss3_scene_init(void) {
    *(char*)(DAT_80612a48 + 0x98) = 0;
    boss3_spawn();
    boss3_angle_pattern_init();
    *(float*)(DAT_80612a48 + 0x5a0) = FLOAT_80619568;
    sprite_get_texture_id(0x300);
    FUN_802af38c();
    sprite_get_texture_id(0x300);
    double zero = (double)FLOAT_80619568;
    FUN_802ad80c(zero, zero, zero, (double)FLOAT_80619580);
}

// ===========================================================================
// 0x8029f290  boss3_alloc_and_init
// Allocate the 0x64dc-byte boss3 control block (choosing a co-op variant when the
// level id is 0x0a and there is more than one participant), run the GX/material
// setup passthroughs, init the beam slot, then run the scene init.
// ===========================================================================
void boss3_alloc_and_init(double p1, double p2, double p3, double p4, double p5, double p6,
                          double p7, double p8, int p9, int p10, int p11, int p12, int p13,
                          int p14, int p15, int p16) {
    (void)p9;
    unsigned int r;
    if ((DAT_805015bd == '\n') && (1 < DAT_80612610)) {
        r = FUN_802af304(p1, p2, p3, p4, p5, p6, p7, p8, 1, p10, p11, p12, p13, p14, p15, p16);
    } else {
        r = FUN_802af304(p1, p2, p3, p4, p5, p6, p7, p8, 0, p10, p11, p12, p13, p14, p15, p16);
    }
    r = FUN_802ad7d8((double)r, p2, p3, p4, p5, p6, p7, p8, (int)((unsigned long long)r >> 0x20),
                     (int)r, p11, p12, p13, p14, p15, p16);
    DAT_80612a48 = mem_alloc((int)r, p2, p3, p4, p5, p6, p7, p8, 0x64dc, 1, p11, p12, p13, p14,
                             p15, p16);
    FUN_802c66bc(8, 8, (unsigned long long*)(DAT_80612a48 + 0x5c4));
    boss3_scene_init();
}

// ===========================================================================
// 0x8029f30c  boss3_angle_segment_lookup
// Map the current boss angle (+0xf0) to its angle-segment: scan up to 11 [start,
// end] pairs; store the matching index at +0x90 (-1 if none) and set the
// near-midpoint flag at +0x94.
// ===========================================================================
void boss3_angle_segment_lookup(void) {
    double bias = DOUBLE_80619630;
    *(int*)(DAT_80612a48 + 0x94) = 0;

    int off = 0;
    int idx = 0;
    unsigned int endDeg = 0;
    for (int n = 0xb; ; n--) {
        float angle = *(float*)(DAT_80612a48 + 0xf0);
        unsigned int startDeg = *(unsigned int*)(DAT_80612a48 + off);
        float start = (float)((double)CONCAT44(0x43300000, startDeg ^ 0x80000000) - bias);
        endDeg = ((unsigned int*)(DAT_80612a48 + off))[1];
        float end = (float)((double)CONCAT44(0x43300000, endDeg ^ 0x80000000) - bias);
        if ((start <= angle) && (angle < end)) {
            break;
        }
        idx++;
        off += 8;
        if (n - 1 == 0) {
            *(int*)(DAT_80612a48 + 0x90) = -1;
            return;
        }
    }

    float angle = *(float*)(DAT_80612a48 + 0xf0);
    int mid = *(int*)(DAT_80612a48 + off) + (int)endDeg;
    float midDeg = (float)((double)CONCAT44(0x43300000, (unsigned int)mid ^ 0x80000000) - bias);
    if (ABS(FLOAT_80619574 * midDeg - angle) < FLOAT_80619784) {
        *(int*)(DAT_80612a48 + 0x94) = 1;
    }
    *(int*)(DAT_80612a48 + 0x90) = idx;
}
