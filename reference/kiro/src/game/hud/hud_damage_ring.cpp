// Batch 338 | Date: 2026-09-04
// HUD directional damage-ring widget (the "you were hit from this direction"
// radial gauge). A 20-segment ring surrounds the reticle; each segment fades
// independently and a rotating needle sprite (texture 0x126) points toward the
// most recent damage source. damage_apply_feedback feeds the widget an impulse
// (set_direction + accumulate); the per-frame tick decays the segments; the
// render pass draws the ring strips and the needle.
//
// Address map:
//   0x8011b9b0 hud_dmgring_state_is_clean       (20-segment dirty compare)
//   0x8011ba64 hud_dmgring_accumulate           (add impulse to a player ring)
//   0x8011bb3c hud_dmgring_reset                 (per-player init / respawn)
//   0x8011bb8c hud_dmgring_tick                  (per-frame threshold+decay)
//   0x8011bd08 hud_dmgring_render_needle         (rotating needle sprite 0x126)
//   0x8011c2a4 hud_dmgring_render                (top-level render entry)
//   0x8011c3c0 hud_dmgring_segment_alpha_a       (segment set A alpha)
//   0x8011c42c hud_dmgring_segment_alpha_b       (segment set B alpha)
//   0x8011c498 hud_dmgring_segment_alpha         (A/B dispatcher)
//   0x8011c4d8 hud_dmgring_set_direction         (needle angle from a vector)
//   0x8011c560 hud_dmgring_render_strips         (20-segment gauge strips)
//
// Subsystem: game (HUD rendering)

#include "common.h"

// ============================================================================
// Per-file externs for raw symbols (address reused with differing signatures
// across files, so declared locally per the Compile-Clean Standard).
// ============================================================================

// --- scalar/vector float constants (read-only) ---
extern float FLOAT_80615718;   // 1.0
extern float FLOAT_80615720;   // segment-count divisor (20.0)
extern float FLOAT_80615730;   // 0.0
extern float FLOAT_80615734;   // 0.5
extern float FLOAT_80615748;   // 2.0
extern float FLOAT_80615754;   // 1.5 (Newton-Raphson rsqrt constant)
extern float FLOAT_80615778;
extern float FLOAT_806157a8;
extern float FLOAT_806157b0;
extern float FLOAT_806157b4;
extern float FLOAT_806157c0;
extern float FLOAT_806157c4;
extern float FLOAT_806157c8;
extern float FLOAT_806157d8;
extern float FLOAT_806157dc;
extern float FLOAT_806157e4;
extern float FLOAT_806157f4;
extern float FLOAT_80615824;
extern float FLOAT_80615830;
extern float FLOAT_80615848;
extern float FLOAT_8061586c;
extern float FLOAT_80615860;
extern float FLOAT_80615880;
extern float FLOAT_806158d8;
extern float FLOAT_80615924;
extern float FLOAT_80615928;
extern float FLOAT_8061592c;
extern float FLOAT_80615930;
extern float FLOAT_80615934;
extern float FLOAT_80615938;
extern float FLOAT_8061593c;
extern float FLOAT_80615940;
extern float FLOAT_80615944;
extern float FLOAT_80615750;
extern float FLOAT_80615784;
extern float FLOAT_8061575c;

extern float FLOAT_806108e8;
extern float FLOAT_806108ec;

// double-precision int->float bias helpers
extern double DOUBLE_80615738; // 4503599627370496.0 (0x4330..) unsigned bias
extern double DOUBLE_80615740; //  signed bias variant

// --- global data blocks ---
extern int    DAT_80611d54;    // active player-view context base
extern int*   DAT_80611d74;    // current player-view pointer (int array)
extern int    DAT_80611d78;    // player-slot table base
extern uint*  DAT_80611748;    // viewport rect (u32[9]: x0,y0,.. y-span at [5..8])
extern uint   DAT_806109c0;    // render width (screen units)
extern int    DAT_806109c8;    // (segment/slot count elsewhere)
extern int*   DAT_80610a20;    // texture descriptor table base
extern int    DAT_80612244;    // widescreen/aspect flag
extern int    DAT_8061179c;
extern int    DAT_806117a0;

extern float  DAT_803cca2c;    // needle base offset x
extern int    DAT_803cca30;    // needle base offset (raw)
extern float  DAT_803cca34;    // needle base offset z

// segment threshold tables (5-word stride: color rgb + two threshold floats)
extern float  DAT_8040ca24;    // segment set A table (5-word stride)
extern float  DAT_8040cbc8;    // segment set B table (5-word stride)

// large float LUT (sin/cos style) used by strip renderer
extern float  DAT_80583228;

// per-player needle animation state (8-byte stride)
extern float  DAT_8057e470;    // needle target angle
extern float  DAT_8057e474;    // needle blend weight

// per-player ring bar accumulators (0x177c stride)
extern int    DAT_805786a0;    // ring fill accumulator (int)

// GX FIFO write port / counters
extern volatile float DAT_cc008000;
extern int    DAT_80611570;
extern int    DAT_80611574;
extern int    DAT_80611578;

extern float  FLOAT_80615734;

// --- helper functions (raw addresses; local prototypes) ---
extern int    FUN_803826e8(void);         // HUD render state push
extern int    FUN_80382734(void);         // HUD render state pop
extern unsigned long long FUN_803826b4(void); // returns packed color pair (hi/lo)
extern int    FUN_803826b8(void);
extern int    FUN_80382700(void);         // strip render state pop
extern int    FUN_803825c0(double);       // float -> clamped int (0..255)
extern double FUN_80131538(double);        // ease/curve
extern double FUN_801314f4(double);        // angle wrap/normalize
extern double FUN_80390aa4(double, double); // atan2(y, x)
extern int    FUN_801c1714(void);          // active player count
extern int    sprite_get_texture_id(int spriteId);
extern void   FUN_8012f05c(double angle, float* outMtx); // build rotation matrix
extern void   FUN_8012fb90(float* mtx, float* vec);      // transform vec by mtx
extern int    FUN_8016e8ac(int mode);      // GX set blend/mode
extern int    FUN_8016e84c(void);          // GX begin untextured
extern int    FUN_8016f93c(float* a, float* b, float* c, float* d,
                           uint ca, uint cb, uint cc, uint cd); // untextured quad
extern int    FUN_8016ee98(void);
extern int    FUN_8016f00c(int mode);
extern int    FUN_8016e77c(int texId);
extern int    FUN_80170dbc(double, double, double, double, double, double,
                           double, double, int, int, uint, int, int, uint,
                           uint, uint, float, uint, uint, uint, uint); // textured quad
extern int    FUN_8017c658(void);
extern int    FUN_8017fab4(void);
extern int    FUN_8017c670(void);
extern int    FUN_80130fc8(void);           // returns bool: inverse-fade flag
void          gx_begin_primitive(int vtxdesc, int fmt, int count); // 0x80172818 GX begin primitive

// --- named cross-module helpers (defined in sibling .cpp; not yet in a header) ---
extern double pickup_fade_get_alpha(int fadeState);
extern void   hud_set_max_ammo_display(double value, int playerIdx);
extern void   screen_effect_init_slot(int ring, int owner);

// Gekko fsqrt intrinsic used by the decompiler (single-precision square root).
static inline float SQRT(float x) {
    float r = x;
    // Newton-Raphson from a crude seed; adequate for a non-matching build.
    if (x > 0.0f) {
        r = x;
        for (int i = 0; i < 8; ++i) {
            r = 0.5f * (r + x / r);
        }
    }
    return r;
}

// forward decls within this file
extern "C" {
double hud_dmgring_segment_alpha_a(int base, int seg);
double hud_dmgring_segment_alpha_b(int base, int seg);
double hud_dmgring_segment_alpha(int set, int base, int seg);
void   hud_dmgring_render_strips(unsigned long long p1, double p2, double alpha,
                                 int ring, int cBase, int cLo, int cHi, int cTop);
}

// ============================================================================
// 0x8011b9b0 | hud_dmgring_state_is_clean
// Compares the 20 segment threshold states against the stored dirty flags for
// both threshold pairs (fill and secondary). Returns 0 when everything matches
// (clean), 1 as soon as a mismatch is found (dirty -> needs a flash).
// ring: base of the 20-entry segment state block.
// src:  source entity/weapon providing the two threshold values.
// ============================================================================
extern "C" uint hud_dmgring_state_is_clean(int ring, int src) {
    int segOff = 0;      // stride 0x14 into the threshold table
    int stateOff = 0;    // stride 4 into the ring state
    int remaining = 0x14;
    do {
        uint secondary = 0;
        if (*(int*)(src + 0x20) == 8) {
            secondary = (uint)(*(float*)(*(int*)(src + 0x7c) + 0x7c) *
                                   *(float*)((int)&DAT_8040cbc8 + segOff) <=
                               *(float*)(*(int*)(src + 0x7c) + 0x78));
        }
        uint primary = (uint)(*(float*)(src + 0x1d8) *
                                  *(float*)((int)&DAT_8040ca24 + segOff) <=
                              *(float*)(src + 0x1cc));
        if (primary != *(uint*)(ring + stateOff)) {
            return 1;
        }
        if (((uint*)(ring + stateOff))[0x14] != secondary) {
            return 1;
        }
        stateOff += 4;
        segOff += 0x14;
        remaining += -1;
    } while (remaining != 0);
    return 0;
}

// ============================================================================
// 0x8011ba64 | hud_dmgring_accumulate
// Adds a damage impulse (scaled by dt) into a player's ring fill accumulator,
// clamps it to the render width, and triggers the ring flash timer whenever
// the segment state has become dirty (or a global override is active).
// dt:        frame delta (seconds).
// playerIdx: player-view slot index.
// ============================================================================
extern "C" void hud_dmgring_accumulate(double dt, int playerIdx) {
    if (-1 < playerIdx) {
        uint widthx2 = DAT_806109c0 << 1;
        int barOff = playerIdx * 0x177c;
        int src = *(int*)(DAT_80611d78 + playerIdx * 0x141c + 0x14);
        int ring = *(int*)(src + 0x268);

        *(int*)(&DAT_805786a0 + barOff) =
            *(int*)(&DAT_805786a0 + barOff) +
            (int)((float)((double)FLOAT_80615734 * dt) *
                  (float)((double)(longlong)(int)widthx2 - (double)0.0));

        if ((int)widthx2 < *(int*)(&DAT_805786a0 + barOff)) {
            *(uint*)(&DAT_805786a0 + barOff) = widthx2;
        }

        int clean = (int)hud_dmgring_state_is_clean(ring, src);
        if ((clean == 0) || ((DAT_8061179c != 0) && (DAT_806117a0 != 0))) {
            *(float*)(ring + 0x140) = FLOAT_80615748; // flash timer = full
        }
    }
    return;
}

// ============================================================================
// 0x8011bb3c | hud_dmgring_reset
// Per-player initialization (spawn / respawn). Seeds the accumulator via the
// HUD ammo-display helper and, when the player owns a ring state block, clears
// its per-segment slots.
// ctx: player context ([1]=player index, [5]=owning entity).
// ============================================================================
extern "C" void hud_dmgring_reset(int ctx) {
    if (-1 < *(int*)(ctx + 4)) {
        hud_set_max_ammo_display((double)FLOAT_80615730, *(int*)(ctx + 4));
        int ring = *(int*)(*(int*)(ctx + 0x14) + 0x268);
        if (ring != 0) {
            screen_effect_init_slot(ring, *(int*)(ctx + 0x14));
        }
    }
    return;
}

// ============================================================================
// 0x8011bb8c | hud_dmgring_tick
// Per-frame update. Decays the ring flash timer, re-evaluates each of the 20
// segments against its threshold (re-arming the flash + a per-segment timer on
// any change), then decays the two per-segment fade timers toward zero.
// ring: ring state block base.
// src:  source entity/weapon providing threshold values.
// ============================================================================
extern "C" void hud_dmgring_tick(int ring, int src) {
    float decay = FLOAT_806108e8 *
                  (FLOAT_80615718 /
                   (float)((double)(longlong)(int)DAT_806109c0 - (double)0.0));

    if (FLOAT_80615730 < *(float*)(ring + 0x140)) {
        *(float*)(ring + 0x140) = *(float*)(ring + 0x140) - decay;
    }

    int segOff = 0;
    int stateOff = 0;
    int remaining = 0x14;
    do {
        float armTime = FLOAT_80615720;
        uint primary = (uint)(*(float*)(src + 0x1d8) *
                                  *(float*)((int)&DAT_8040ca24 + segOff) <=
                              *(float*)(src + 0x1cc));
        uint secondary = 0;
        if (*(int*)(src + 0x20) == 8) {
            secondary = (uint)(*(float*)(*(int*)(src + 0x7c) + 0x7c) *
                                   *(float*)((int)&DAT_8040cbc8 + segOff) <=
                               *(float*)(*(int*)(src + 0x7c) + 0x78));
        }
        uint* slot = (uint*)(ring + stateOff);
        if (primary != *slot) {
            *(float*)(ring + 0x140) = FLOAT_80615748;
            *slot = primary;
            slot[0x28] = (uint)armTime;
        }
        armTime = FLOAT_80615720;
        if (slot[0x14] != secondary) {
            *(float*)(ring + 0x140) = FLOAT_80615748;
            slot[0x14] = secondary;
            slot[0x3c] = (uint)armTime;
        }
        stateOff += 4;
        segOff += 0x14;
        remaining += -1;
    } while (remaining != 0);

    // decay the two per-segment fade timers
    float zero = FLOAT_80615730;
    int off = 0;
    int count = 0x14;
    do {
        int seg = ring + off;
        if ((zero < *(float*)(seg + 0xa0)) &&
            (*(float*)(seg + 0xa0) = *(float*)(seg + 0xa0) - decay,
             *(float*)(seg + 0xa0) < zero)) {
            *(float*)(seg + 0xa0) = zero;
        }
        if ((zero < *(float*)(seg + 0xf0)) &&
            (*(float*)(seg + 0xf0) = *(float*)(seg + 0xf0) - decay,
             *(float*)(seg + 0xf0) < zero)) {
            *(float*)(seg + 0xf0) = zero;
        }
        off += 4;
        count += -1;
    } while (count != 0);
    return;
}

// ============================================================================
// 0x8011c3c0 | hud_dmgring_segment_alpha_a
// Alpha for a segment in set A. When the fade timer is <=0 the alpha is a hard
// 0/1 based on the stored state; otherwise it ramps: state==0 fades in toward
// 1, state!=0 fades out from 1.
// ============================================================================
extern "C" double hud_dmgring_segment_alpha_a(int base, int seg) {
    int off = seg * 4;
    double t = (double)*(float*)(base + off + 0xa0);
    if (t <= (double)FLOAT_80615730) {
        if (*(int*)(base + off) == 0) {
            return (double)FLOAT_80615730;
        }
        return (double)FLOAT_80615718;
    }
    t = (double)(float)(t / (double)FLOAT_80615720);
    if (*(int*)(base + off) != 0) {
        double one = (double)FLOAT_80615718;
        if (one < t) t = one;
        return (double)(float)(one - t);
    }
    if ((double)FLOAT_80615718 < t) {
        return (double)FLOAT_80615718;
    }
    return t;
}

// ============================================================================
// 0x8011c42c | hud_dmgring_segment_alpha_b
// Same as set A but for the secondary segment slots (offsets 0x50/0xf0).
// ============================================================================
extern "C" double hud_dmgring_segment_alpha_b(int base, int seg) {
    int off = base + seg * 4;
    if ((double)*(float*)(off + 0xf0) <= (double)FLOAT_80615730) {
        if (*(int*)(off + 0x50) == 0) {
            return (double)FLOAT_80615730;
        }
        return (double)FLOAT_80615718;
    }
    double t = (double)(float)((double)*(float*)(off + 0xf0) / (double)FLOAT_80615720);
    if (*(int*)(off + 0x50) != 0) {
        double one = (double)FLOAT_80615718;
        if (one < t) t = one;
        return (double)(float)(one - t);
    }
    if ((double)FLOAT_80615718 < t) {
        return (double)FLOAT_80615718;
    }
    return t;
}

// ============================================================================
// 0x8011c498 | hud_dmgring_segment_alpha
// Dispatches between the two segment-set alpha getters (set 0 = A, else B).
// (The original returns void but leaves the alpha in f1; callers consume that
//  return value, so this faithful translation returns the dispatched alpha.)
// ============================================================================
extern "C" double hud_dmgring_segment_alpha(int set, int base, int seg) {
    if (set == 0) {
        return hud_dmgring_segment_alpha_a(base, seg);
    }
    return hud_dmgring_segment_alpha_b(base, seg);
}

// ============================================================================
// 0x8011c4d8 | hud_dmgring_set_direction
// Sets the needle target angle for a player from a 2D damage-direction vector
// (atan2 of x/z), biased by the player's current camera yaw. Resets the blend
// weight to full so the needle snaps then eases toward the new direction.
// playerIdx: player-view slot index.
// dir:       float[3] direction vector (dir[0]=x, dir[2]=z used).
// ============================================================================
extern "C" void hud_dmgring_set_direction(int playerIdx, float* dir) {
    *(float*)(&DAT_8057e474 + playerIdx * 8) = FLOAT_80615718;
    double a = (double)FUN_80390aa4((double)*dir, (double)dir[2]);
    *(float*)(&DAT_8057e470 + playerIdx * 8) =
        (float)a -
        (FLOAT_806157c4 * *(float*)(DAT_80611d78 + playerIdx * 0x141c + 0x100)) /
            FLOAT_80615848;
    return;
}

// ============================================================================
// 0x8011bd08 | hud_dmgring_render_needle
// Draws the rotating direction needle sprite (texture id 0x126). Builds a
// rotation matrix from the eased needle angle, transforms the four quad corners
// into screen space around the reticle centre, and submits two passes: a dark
// backing quad and a colored needle quad whose green channel is modulated by
// the source fill ratio.
// player: pointer to the player-view state (as int*).
// ============================================================================
extern "C" void hud_dmgring_render_needle(int* player) {
    int src = *player;
    if ((*(uint*)(src + 0x10) & 1U << *(int*)(DAT_80611d54 + 0x214)) == 0) {
        return;
    }

    double alpha = pickup_fade_get_alpha((int)(player + 0x31a));

    // fast reciprocal-sqrt refinement (Gekko rsqrte + one Newton step)
    float r = 1.0f / SQRT(FLOAT_80615718);
    r = r * -(FLOAT_80615734 * r * r - FLOAT_80615754);
    float needleLen = FLOAT_80615718 * r * -(FLOAT_80615734 * r * r - FLOAT_80615754);

    // base translation for the needle quad
    float baseX = DAT_803cca2c;
    int   baseYraw = DAT_803cca30;
    float baseZ = DAT_803cca34;
    (void)needleLen; (void)baseYraw;

    // corner offset vectors (rotated below)
    float c0x = FLOAT_80615924, c0y = FLOAT_80615778, c0z = FLOAT_80615730;
    float c1x = FLOAT_80615928, c1y = FLOAT_80615778, c1z = FLOAT_80615730;
    float c2x = FLOAT_80615924, c2y = FLOAT_8061592c, c2z = FLOAT_80615730;
    float c3x = FLOAT_80615928, c3y = FLOAT_8061592c, c3z = FLOAT_80615730;

    // direction from player position to camera/reticle centre -> needle angle
    float dx = *(float*)(src + 100) - *(float*)(DAT_80611d54 + 0x27c);
    float dz = *(float*)(src + 0x6c) - *(float*)(DAT_80611d54 + 0x284);
    float len2 = dz * dz + dx * dx + FLOAT_80615730;
    if (FLOAT_80615930 < len2) {
        float ir = 1.0f / SQRT(len2);
        ir = ir * -(FLOAT_80615734 * len2 * ir * ir - FLOAT_80615754);
        float scl = FLOAT_80615718 * ir *
                    -(FLOAT_80615734 * len2 * ir * ir - FLOAT_80615754);
        dx = dx * scl;
        dz = dz * scl;
    }

    double dir = (double)(FLOAT_80615730 * needleLen * dx +
                          FLOAT_80615718 * needleLen * dz);
    float angle;
    if (*(float*)(src + 100) < *(float*)(DAT_80611d54 + 0x27c) &&
        (*(float*)(src + 0x6c) != FLOAT_80615730)) {
        // rear hemisphere: mirror the angle
        double wrapped = (double)FUN_801314f4(dir);
        angle = -((float)((double)FLOAT_80615848 * wrapped) / FLOAT_806157c4);
    } else {
        double wrapped = (double)FUN_801314f4(dir);
        angle = (float)((double)FLOAT_80615848 * wrapped) / FLOAT_806157c4;
    }

    float mtx[18];
    FUN_8012f05c((double)((FLOAT_806157c4 * angle) / FLOAT_80615848), mtx);
    FUN_8012fb90(mtx, &c0x);
    FUN_8012fb90(mtx, &c1x);
    FUN_8012fb90(mtx, &c2x);
    FUN_8012fb90(mtx, &c3x);
    FUN_8012fb90(mtx, &baseX);

    // assemble the first (dark backing) quad
    float qx0, qx1, qx2, qx3, qy0, qy1, qy2, qy3, qz0, qz1, qz2, qz3;
    float px = *(float*)(src + 100);
    qx0 = baseX + c0x + px;
    qx1 = baseX + c1x + px;
    float py = *(float*)(src + 0x68);
    qx2 = baseX + c2x + px;
    qx3 = baseX + c3x + px;
    float depth = *(float*)(*(int*)(*(int*)(src + 4) + 0x10) +
                            *(int*)(*(int*)(*(int*)(src + 4) + 0x84) + 0x18) * 0x40 + 0x34);
    qy0 = FLOAT_806157a8 + c0y + py + depth;
    float pz = *(float*)(src + 0x6c);
    qy1 = FLOAT_806157a8 + c1y + py + depth;
    qy2 = (FLOAT_806157a8 + c2y + py + depth) - FLOAT_806157e4;
    qz0 = baseZ + c0z + pz;
    qz1 = baseZ + c1z + pz;
    qz2 = baseZ + c2z + pz;
    qy3 = (FLOAT_806157a8 + c3y + py + depth) - FLOAT_806157e4;
    qz3 = baseZ + c3z + pz;

    FUN_8016e8ac(2);
    FUN_8016e84c();
    int aByte = (int)((double)FLOAT_806157d8 * alpha);
    uint darkC = (uint)aByte << 0x18;
    float back0[3] = { qx0, qy0, qz0 };
    float back1[3] = { qx1, qy1, qz1 };
    float back2[3] = { qx2, qy2, qz2 };
    float back3[3] = { qx3, qy3, qz3 };
    FUN_8016f93c(back0, back1, back2, back3, darkC, darkC, darkC, darkC);

    // second (colored) needle quad: green modulated by fill ratio
    c0x = FLOAT_80615730; c0z = FLOAT_80615730;
    c1x = FLOAT_80615734; c1y = FLOAT_80615730;
    c2x = FLOAT_8061586c +
          -((FLOAT_80615718 / *(float*)(src + 0x1d8)) * *(float*)(src + 0x1cc) -
            FLOAT_80615718);
    c1z = FLOAT_80615730;
    c2y = FLOAT_80615730;
    c2z = FLOAT_80615730;
    c3x = FLOAT_80615734;
    c3y = FLOAT_80615730;
    c3z = FLOAT_80615730;
    c0y = c2x;

    FUN_8012fb90(mtx, &c0x);
    FUN_8012fb90(mtx, &c1x);
    FUN_8012fb90(mtx, &c2x);
    FUN_8012fb90(mtx, &c3x);

    px = *(float*)(src + 100);
    qx0 = baseX + c0x + px;
    qx1 = baseX + c1x + px;
    qx2 = baseX + c2x + px;
    qx3 = baseX + c3x + px;
    py = *(float*)(src + 0x68);
    depth = *(float*)(*(int*)(*(int*)(src + 4) + 0x10) +
                      *(int*)(*(int*)(*(int*)(src + 4) + 0x84) + 0x18) * 0x40 + 0x34);
    qy0 = FLOAT_806157a8 + c0y + py + depth;
    qy1 = FLOAT_806157a8 + c1y + py + depth;
    pz = *(float*)(src + 0x6c);
    qy2 = (FLOAT_806157a8 + c2y + py + depth) - FLOAT_806157e4;
    qz0 = baseZ + c0z + pz;
    qz1 = baseZ + c1z + pz;
    qz2 = baseZ + c2z + pz;
    qy3 = (FLOAT_806157a8 + c3y + py + depth) - FLOAT_806157e4;
    qz3 = baseZ + c3z + pz;

    uint green = (uint)((FLOAT_80615934 / *(float*)(src + 0x1d8)) *
                            *(float*)(src + 0x1cc) + FLOAT_80615784);
    uint colC = darkC | (green & 0xff) << 8 | 0x14007f;
    float front0[3] = { qx0, qy0, qz0 };
    float front1[3] = { qx1, qy1, qz1 };
    float front2[3] = { qx2, qy2, qz2 };
    float front3[3] = { qx3, qy3, qz3 };
    FUN_8016f93c(front0, front1, front2, front3,
                 colC, darkC | 0x14147f, colC, darkC | 0x14147f);
    return;
}

// ============================================================================
// 0x8011c2a4 | hud_dmgring_render
// Top-level render entry (called from hud_render_player_view). Pushes HUD
// render state, and while the ring flash timer is positive, computes a pulsing
// alpha (eased by FUN_80131538) and draws the segment strips via the strip
// renderer, then pops render state.
// ============================================================================
extern "C" void hud_dmgring_render(void) {
    FUN_803826e8();
    int ring = *(int*)(*(int*)(DAT_80611d74 + 0x14) + 0x268);
    if ((ring != 0) && (FLOAT_80615730 < *(float*)(ring + 0x140))) {
        double pulse = FUN_80131538((double)(*(float*)(ring + 0x140) * FLOAT_80615734));
        uint* vp = DAT_80611748;
        pulse = (double)(float)((double)FLOAT_806158d8 * pulse);
        if ((double)FLOAT_80615718 < pulse) {
            pulse = (double)FLOAT_80615718;
        }
        int c0 = FUN_803825c0((double)(float)((double)FLOAT_80615784 * pulse));
        int c1 = FUN_803825c0((double)(float)((double)FLOAT_806157d8 * pulse));
        int c2 = FUN_803825c0((double)(float)((double)FLOAT_806157b0 * pulse));
        int c3 = FUN_803825c0((double)(float)((double)FLOAT_80615750 * pulse));
        hud_dmgring_render_strips(
            (unsigned long long)0,
            (double)(float)((double)(longlong)(int)(*vp) - (double)0.0),
            (double)FLOAT_80615718, ring, c3, c2, c1, c0);
        (void)vp;
    }
    FUN_80382734();
    return;
}

// ============================================================================
// 0x8011c560 | hud_dmgring_render_strips
// The heavy renderer: draws the needle sprite (texture 0x126) plus the two
// 20-segment gauge strips (three passes with textures 0x31e / 799 / 800). Each
// segment quad is built from the DAT_80583228 angle LUT and the per-segment
// alpha getters, then streamed directly to the GX FIFO at DAT_cc008000.
//
// This is a faithful non-matching translation: the exact FIFO byte writes are
// reproduced through the DAT_cc008000 volatile port and the primitive-begin
// helper FUN_80172818. Color-channel packing uses the game's signed/unsigned
// int->float bias trick.
//
// p2:    reticle centre Y (screen units, biased float in original).
// alpha: overall widget alpha.
// ring:  ring state block base (holds the 20 segment states).
// cBase..cHi: precomputed pulse color components from hud_dmgring_render.
// ============================================================================
extern "C" void hud_dmgring_render_strips(unsigned long long p1, double p2,
                                          double alpha, int ring, int cBase,
                                          int cLo, int cHi, int cTop) {
    (void)p1;
    unsigned long long packed = FUN_803826b4();
    int colHi = (int)(packed >> 0x20);
    int colLo = (int)packed;

    // widget scale depends on split-screen player count
    double scale;
    int players = FUN_801c1714();
    if (players < 3) {
        scale = (double)FLOAT_806158d8;
    } else {
        players = FUN_801c1714();
        if (players == 3) {
            scale = (DAT_80611d74[1] == 0) ? (double)FLOAT_806158d8
                                           : (double)FLOAT_80615938;
        } else {
            scale = (double)FLOAT_80615938;
        }
    }
    scale = (double)(float)(alpha * scale);

    uint widthUnits;
    players = FUN_801c1714();
    if (players < 3) {
        widthUnits = 0x200;
    } else {
        bool twoUp = false;
        players = FUN_801c1714();
        if ((players == 3) && (DAT_80611d74[1] == 0)) {
            twoUp = true;
        }
        widthUnits = twoUp ? 0x200 : 300;
    }
    scale = (double)(float)(scale *
             (double)(FLOAT_80615734 * (float)((double)(longlong)(int)widthUnits - (double)0.0)));

    uint heightUnits;
    players = FUN_801c1714();
    if (players == 1) {
        heightUnits = (DAT_80612244 != 0) ? 0x184 : 0x154;
    } else {
        heightUnits = (DAT_80612244 != 0) ? 0xfa : 0xdc;
    }

    float widthScaled = (float)(alpha *
        (double)(FLOAT_80615734 * (float)((double)(longlong)(int)heightUnits - (double)0.0)));

    // centre X/Y from viewport rect
    double centerX = (double)((float)((double)(longlong)(int)DAT_80611748[2] - (double)0.0) +
                              (double)0.0) -
                     (double)(float)((double)(longlong)(int)DAT_80611748[0] - (double)0.0);
    (void)centerX;

    double left  = (double)(float)((double)0.0);  // placeholder; recomputed below
    (void)left;

    // -- recompute centre coordinates faithfully --
    double vpX0 = (double)(longlong)(int)DAT_80611748[0];
    double vpX2 = (double)(longlong)(int)DAT_80611748[2];
    double vpY5 = (double)(longlong)(int)DAT_80611748[5];
    double vpY7 = (double)(longlong)(int)DAT_80611748[7];
    float cx = (float)((float)(vpX2) + (float)p2) - (float)(vpX0);
    (void)cx;

    double halfW = (double)(float)((double)(float)(vpX2) - scale);
    double halfWr = (double)(float)((double)(float)(vpX2) + scale);
    double innerL = (double)(float)(halfW + (double)(float)((double)FLOAT_80615824 * scale));
    double innerR = (double)(float)(halfWr - (double)(float)((double)FLOAT_80615824 * scale));

    float cyf = (float)((float)(vpY7) + (float)p2) - (float)(vpY5);
    double topY = (double)(cyf - widthScaled);
    double botY = (double)(cyf + widthScaled);

    int playerBit;
    if (DAT_80611d74 == (int*)0) {
        playerBit = *(int*)(DAT_80611d54 + 0x214);
    } else {
        playerBit = *DAT_80611d74;
    }

    int needleTex = sprite_get_texture_id(0x126);
    int texDesc = *DAT_80610a20 + needleTex * 0x58;
    playerBit = playerBit * 8;
    ushort th = *(ushort*)(texDesc + 0x52);
    ushort tw = *(ushort*)(texDesc + 0x4e);

    // ease the needle blend weight toward its target
    float* blend = (float*)(&DAT_8057e474 + playerBit);
    *blend = -(FLOAT_80615748 *
               (FLOAT_80615718 / (float)((double)(longlong)(uint)DAT_806109c0 - (double)0.0)) *
               FLOAT_806108ec - *blend);
    if (FUN_80130fc8()) {
        *blend = -(FLOAT_80615748 *
                   (FLOAT_80615718 / (float)((double)(longlong)(uint)DAT_806109c0 - (double)0.0)) -
                   *blend);
    }
    if (*blend < FLOAT_80615730) {
        *blend = FLOAT_80615730;
    }

    uint fillByte = (int)(FLOAT_80615830 * *blend) << 0x18 | 0x32;

    uint ang0 = (uint)(FLOAT_806157c0 * *(float*)(&DAT_8057e470 + playerBit) + FLOAT_80615734);
    uint ang1 = (uint)(FLOAT_806157c0 *
                       (FLOAT_80615860 + *(float*)(&DAT_8057e470 + playerBit)) + FLOAT_80615734);

    float radius = FLOAT_80615940 *
                       (float)((double)(longlong)(int)DAT_80611748[3] - (double)0.0) -
                   FLOAT_806157f4;
    float sin0 = (&DAT_80583228)[ang0 & 0x7ff];
    float cos0 = *(float*)((int)&DAT_80583228 + ((ang0 + 0x200) * 4 & 0x1ffc));
    float sin1 = (&DAT_80583228)[ang1 & 0x7ff];
    float cos1 = *(float*)((int)&DAT_80583228 + ((ang1 + 0x200) * 4 & 0x1ffc));

    double baseCol = (double)(float)((double)(longlong)(int)DAT_80611748[2] - (double)0.0);
    double baseRow = (double)(float)((double)(longlong)(int)DAT_80611748[7] - (double)0.0);
    double aspect = (double)((FLOAT_8061593c *
                         (float)((double)(longlong)(int)DAT_80611748[8] - (double)0.0)) /
                        (FLOAT_80615940 *
                         (float)((double)(longlong)(int)DAT_80611748[3] - (double)0.0)));

    double nx = (double)(float)(sin0 * radius + (float)baseCol);
    double ny = (double)(float)(aspect * (double)(float)(cos0 * radius) + (float)baseRow);

    FUN_8016ee98();
    FUN_8016e8ac(0);
    FUN_8016f00c(2);
    FUN_8016e77c(needleTex);

    double uvS = (double)FLOAT_8061575c;
    uint uW = (uint)th << 4;
    uint uH = (uint)tw << 4;
    float ex = (float)(cos1 * uvS);
    float ey = (float)(ny + (double)(float)(sin1 * uvS));
    float sx = (float)(sin0 * uvS);
    float sy = (float)(ny - (double)(float)(sin1 * uvS));
    FUN_80170dbc((double)FLOAT_80615730, (double)(sy - sx),
                 (double)(float)(aspect * (double)((float)(-cos0 * uvS) - ex) + ny),
                 (double)(sy - sx),
                 (double)(float)(aspect * (double)((float)(-cos0 * uvS) + ex) + ny),
                 (double)(sy + sx),
                 (double)(float)(aspect * (double)((float)(cos0 * uvS) - ex) + ny),
                 (double)(sy + sx), 0, 0, uW, 0, 0, uH, uW, uH,
                 (float)(aspect * (double)((float)(cos0 * uvS) + ex) + ny),
                 fillByte, fillByte, fillByte, fillByte);
    (void)nx;

    FUN_8017c658();
    FUN_8017fab4();
    FUN_8016e8ac(0);

    double spanY = (double)(float)(botY - topY);

    // three passes: fill (0x31e), then the two rim strips (799, 800)
    int passTex = 0;
    int pass = 0;
    do {
        if (pass == 1) {
            passTex = sprite_get_texture_id(800);
            FUN_8016f00c(1);
        } else if (pass < 1) {
            if (-1 < pass) {
                passTex = sprite_get_texture_id(0x31e);
                FUN_8016f00c(2);
            }
        } else if (pass < 3) {
            passTex = sprite_get_texture_id(799);
            FUN_8016f00c(2);
        }
        FUN_8016e77c(passTex);

        int set = 0;
        do {
            int segCount = (set == 0) ? 8 : 10;
            int seg = 0;
            int segState = 0;
            int segTbl = 0;
            double curL = innerL;
            do {
                uint c0pack = 0;
                uint c1pack = 0;
                int stateVal;
                float* tbl;
                float* tblNext;
                double a;
                float wA, wB;

                if (set == 0) {
                    stateVal = *(int*)(colHi + segTbl);
                    tbl = &DAT_8040ca24;
                    tblNext = &DAT_8040ca24 + (seg + 1) * 5;
                    double keepL = curL;
                    double keepIL = innerL;
                    curL = halfW;
                    a = hud_dmgring_segment_alpha_a(colHi, seg);
                    innerL = keepIL;
                    (void)keepL;
                    wA = FLOAT_80615718;
                    wB = FLOAT_80615730;
                } else {
                    stateVal = *(int*)(colHi + segTbl + 0x50);
                    tbl = &DAT_8040cbc8;
                    tblNext = &DAT_8040cbc8 + (seg + 1) * 5;
                    a = hud_dmgring_segment_alpha_b(colHi, seg);
                    wA = FLOAT_80615730;
                    wB = FLOAT_80615718;
                }
                double wBd = (double)wB;
                double wAd = (double)wA;
                (void)wBd; (void)wAd;

                double u0 = (double)*(float*)((int)tbl + segTbl + 4);
                double x0 = (double)(float)(u0 * spanY + topY);
                double x1 = (double)(float)((double)(float)tblNext[1] * spanY + topY);

                if (pass == 1) {
                    int aByte = (stateVal != 0) ? colLo : cBase;
                    uint b = *(uint*)((int)tbl + segTbl + 8);
                    uint g = *(uint*)((int)tbl + segTbl + 0xc);
                    uint rr = *(uint*)((int)tbl + segTbl + 0x10);
                    uint bd = (uint)(FLOAT_80615880 * (float)(int)b);
                    uint gd = (uint)(FLOAT_80615880 * (float)(int)g);
                    uint rd = (uint)(FLOAT_80615880 * (float)(int)rr);
                    c0pack =
                        (int)(a * (double)(int)(b - bd) + (double)(int)bd) |
                        (int)(a * (double)(int)(g - gd) + (double)(int)gd) << 8 |
                        aByte << 0x18 |
                        (int)(a * (double)(int)(rr - rd) + (double)(int)rd) << 0x10;

                    uint nb = tblNext[2];
                    uint ng = tblNext[3];
                    uint nr = tblNext[4];
                    uint nbd = (uint)(FLOAT_80615880 * (float)(int)nb);
                    uint ngd = (uint)(FLOAT_80615880 * (float)(int)ng);
                    uint nrd = (uint)(FLOAT_80615880 * (float)(int)nr);
                    c1pack =
                        (int)(a * (double)(int)(nb - nbd) + (double)(int)nbd) |
                        (int)(a * (double)(int)(ng - ngd) + (double)(int)ngd) << 8 |
                        aByte << 0x18 |
                        (int)(a * (double)(int)(nr - nrd) + (double)(int)nrd) << 0x10;
                } else if (pass < 1) {
                    if (-1 < pass) {
                        double m0 = -(double)(FLOAT_80615748 *
                                              ABS((float)(u0 - (double)FLOAT_80615734)) -
                                          FLOAT_80615718);
                        double m1 = -(double)(FLOAT_80615748 *
                                              ABS((float)((double)(float)tblNext[1] -
                                                          (double)FLOAT_80615734)) -
                                          FLOAT_80615718);
                        if (seg < 0x13) {
                            double an = hud_dmgring_segment_alpha(set, colHi, seg + 1);
                            double sc = (double)(float)((double)FLOAT_80615734 * an);
                            if (a < sc) a = sc;
                        }
                        double span = (double)(long long)(cTop - cBase);
                        (void)span;
                        int aB = FUN_803825c0((double)(float)(
                            m0 * (double)(float)((double)(float)(cTop - cBase) * a +
                                                 (double)(float)(double)cBase)));
                        double gcst = (double)FLOAT_806157c8;
                        double rcst = (double)FLOAT_806157b4;
                        double bcst = (double)FLOAT_806157dc;
                        float inv = (float)((double)FLOAT_80615718 - a);
                        uint b0 = *(uint*)((int)tbl + segTbl + 8);
                        uint g0 = *(uint*)((int)tbl + segTbl + 0xc);
                        uint r0 = *(uint*)((int)tbl + segTbl + 0x10);
                        c0pack =
                            (int)((float)(int)b0 * inv + (float)(rcst * a)) |
                            (int)((float)(int)g0 * inv + (float)(bcst * a)) << 8 |
                            aB << 0x18 |
                            (int)((float)(int)r0 * inv + (float)(gcst * a)) << 0x10;

                        uint nb = tblNext[2];
                        uint ng = tblNext[3];
                        uint nr = tblNext[4];
                        double an2 = a;
                        if (seg < 0x13) {
                            an2 = hud_dmgring_segment_alpha(set, colHi, seg + 1);
                            if (seg < 0x12) {
                                double an3 = hud_dmgring_segment_alpha(set, colHi, seg + 2);
                                double sc2 = (double)(float)((double)FLOAT_80615734 * an3);
                                if (an2 < sc2) an2 = sc2;
                            }
                            aB = FUN_803825c0((double)(float)(
                                m1 * (double)(float)((double)(float)(cTop - cBase) * an2 +
                                                     (double)(float)(double)cBase)));
                        }
                        c1pack =
                            (int)((float)(int)nb * inv + (float)(rcst * a)) |
                            (int)((float)(int)ng * inv + (float)(bcst * a)) << 8 |
                            aB << 0x18 |
                            (int)((float)(int)nr * inv + (float)(gcst * a)) << 0x10;
                    }
                } else if (pass < 3) {
                    if (seg < 0x13) {
                        double an = hud_dmgring_segment_alpha(set, colHi, seg + 1);
                        double sc = (double)(float)((double)FLOAT_80615734 * an);
                        if (a < sc) a = sc;
                    }
                    int aB = FUN_803825c0((double)(float)(
                        (double)(float)((double)(long long)colLo - (double)0.0) * a +
                        (double)FLOAT_80615730));
                    uint b0 = tblNext[2];
                    int g0 = (int)tblNext[3];
                    int r0 = (int)tblNext[4];
                    c0pack = *(uint*)((int)tbl + segTbl + 8) |
                             *(int*)((int)tbl + segTbl + 0xc) << 8 |
                             aB << 0x18 |
                             *(int*)((int)tbl + segTbl + 0x10) << 0x10;
                    if (seg < 0x13) {
                        double an = hud_dmgring_segment_alpha(set, colHi, seg + 1);
                        if (seg < 0x12) {
                            double an2 = hud_dmgring_segment_alpha(set, colHi, seg + 2);
                            double sc = (double)(float)((double)FLOAT_80615734 * an2);
                            if (an < sc) an = sc;
                        }
                        aB = FUN_803825c0((double)(float)(
                            (double)(float)((double)(long long)colLo - (double)0.0) * an +
                            (double)FLOAT_80615730));
                    }
                    c1pack = b0 | g0 << 8 | aB << 0x18 | r0 << 0x10;
                }

                // vertical extents of this segment quad
                double y0, y1;
                if (seg < segCount) {
                    y0 = -(double)(FLOAT_80615748 * *(float*)((int)tbl + segTbl + 4) -
                                   FLOAT_80615718);
                    y1 = -(double)(FLOAT_80615748 * (float)tblNext[1] - FLOAT_80615718);
                    if (seg == 0) {
                        y0 = (double)(float)(y0 - (double)FLOAT_80615778);
                    } else if (seg == segCount + -1) {
                        y1 = (double)(float)(y1 + (double)FLOAT_80615778);
                    }
                } else {
                    y0 = (double)(FLOAT_80615748 *
                                  (*(float*)((int)tbl + segTbl + 4) - FLOAT_80615734));
                    y1 = (double)(FLOAT_80615748 * ((float)tblNext[1] - FLOAT_80615734));
                    if (seg == segCount) {
                        y0 = (double)(float)(y0 + (double)FLOAT_80615778);
                    }
                }

                // stream the quad (2 tris worth of 4 verts) into the GX FIFO
                gx_begin_primitive(0x98, 0, 4);
                unsigned char c0b0 = (unsigned char)c0pack;
                unsigned char c0b1 = (unsigned char)(c0pack >> 8);
                unsigned char c0b2 = (unsigned char)(c0pack >> 0x10);
                unsigned char c0b3 = (unsigned char)(c0pack >> 0x18);
                unsigned char c1b0 = (unsigned char)c1pack;
                unsigned char c1b1 = (unsigned char)(c1pack >> 8);
                unsigned char c1b2 = (unsigned char)(c1pack >> 0x10);
                unsigned char c1b3 = (unsigned char)(c1pack >> 0x18);

                // vertex 0
                DAT_cc008000 = (float)curL;
                DAT_cc008000 = (float)x0;
                DAT_cc008000 = FLOAT_80615944;
                *(volatile unsigned char*)&DAT_cc008000 = c0b0;
                *(volatile unsigned char*)&DAT_cc008000 = c0b1;
                *(volatile unsigned char*)&DAT_cc008000 = c0b2;
                *(volatile unsigned char*)&DAT_cc008000 = c0b3;
                // vertex 1
                DAT_cc008000 = (float)y0;
                DAT_cc008000 = (float)innerL;
                DAT_cc008000 = (float)x0;
                DAT_cc008000 = FLOAT_80615944;
                *(volatile unsigned char*)&DAT_cc008000 = c0b0;
                *(volatile unsigned char*)&DAT_cc008000 = c0b1;
                *(volatile unsigned char*)&DAT_cc008000 = c0b2;
                *(volatile unsigned char*)&DAT_cc008000 = c0b3;
                DAT_cc008000 = (float)y0;
                DAT_cc008000 = (float)curL;
                DAT_cc008000 = (float)x1;
                // vertex 2
                DAT_cc008000 = FLOAT_80615944;
                *(volatile unsigned char*)&DAT_cc008000 = c1b0;
                *(volatile unsigned char*)&DAT_cc008000 = c1b1;
                *(volatile unsigned char*)&DAT_cc008000 = c1b2;
                *(volatile unsigned char*)&DAT_cc008000 = c1b3;
                DAT_cc008000 = (float)y1;
                DAT_cc008000 = (float)innerL;
                DAT_cc008000 = (float)x1;
                // vertex 3
                DAT_cc008000 = FLOAT_80615944;
                *(volatile unsigned char*)&DAT_cc008000 = c1b0;
                *(volatile unsigned char*)&DAT_cc008000 = c1b1;
                *(volatile unsigned char*)&DAT_cc008000 = c1b2;
                *(volatile unsigned char*)&DAT_cc008000 = c1b3;
                DAT_cc008000 = (float)y1;

                DAT_80611570 = DAT_80611570 + 4;
                DAT_80611574 = DAT_80611574 + 2;
                DAT_80611578 = DAT_80611578 + 2;

                seg += 1;
                segTbl += 0x14;
                segState += 4;
                curL = innerL;
                (void)segState; (void)x1;
            } while (seg < 0x14);
            set += 1;
        } while (set < 2);
        pass += 1;
    } while (pass < 3);

    FUN_8017c670();
    FUN_80382700();
    (void)colLo; (void)cLo; (void)cHi;
    return;
}
