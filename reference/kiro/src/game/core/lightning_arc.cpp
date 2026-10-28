// Address range: 0x80134b98 - 0x80134fac | batch_363 | 2026-09-04
//
// Procedural lightning / electric-arc generator.  A bolt is a 17-point
// (0x11) polyline stored per-instance in DAT_805863c0 (0xcc bytes/instance,
// 0xc bytes/point = xyz).  The generator midpoint-subdivides the line,
// displacing each new midpoint along a random perpendicular direction so the
// bolt jitters like a fractal lightning stroke.  A second pass builds the
// per-segment oriented render frame into DAT_80587d40.
//
//   lightning_subdivide      0x80134b98  one midpoint-displace subdivision pass
//   lightning_generate_all   0x80134f4c  run 0x20 subdivision passes
//   lightning_random_index   0x80134f88  game_rand() & 0x1f  (0..31 instance id)
//   lightning_build_frames   0x80134fac  build oriented segment frames for render

#include <math.h>
#include "common.h"

// FP scratch-frame push/pop helpers (0x8038xxxx).  The "enter" returns the
// active bolt instance index in the low word.
extern int  FUN_803826dc(void);      // enter FP frame; returns instance index
extern void FUN_80382728(void);      // leave FP frame

// Math helpers (resolved via shared headers in normal builds; this TU includes
// only common.h so they are declared locally).
extern double game_frand(void);                      // [0,1) random
extern void   mtx4_set_axis_angle(double angle, float* outMtx, float* axis);
extern void   mtx4_transform_vector_alias(float* mtx, float* vec);

// Per-instance bolt point storage: 32 instances * 0x11 points * 0xc bytes.
extern unsigned char DAT_805863c0[];   // bolt point arrays (0xcc stride/instance)
extern unsigned char DAT_80587d40[];   // per-segment render frame output

// Tuning constants (rodata singles/doubles).
extern const float  FLOAT_80615c80;    // unit / relaxation gain
extern const float  FLOAT_80615c84;    // 0.0
extern const float  FLOAT_80615c88;    // segment step
extern const float  FLOAT_80615c8c;    // 0.5 (midpoint weight)
extern const float  FLOAT_80615c90;    // min-length^2 threshold
extern const float  FLOAT_80615c94;    // 1.5 (fast-inv-sqrt Newton const)
extern const float  FLOAT_80615c98;    // rotation angle scale
extern const float  FLOAT_80615c9c;    // displacement scale
extern const double DOUBLE_80615ca0;   // 2^52 bias
extern const float  FLOAT_80615c70;    // (shared) endpoint blend a
extern const float  FLOAT_80615c74;    // (shared) endpoint blend divisor

// fast reciprocal sqrt with one Newton step, matching the PPC frsqrte idiom.
static inline float arc_rsqrt(float x, float c15)
{
    float e = 1.0f / sqrtf(x);
    return e * -(FLOAT_80615c8c * x * e * e - c15);
}

// ---------------------------------------------------------------------------
// 0x80134b98 : one subdivision pass over the active bolt.  Endpoints (index 0
// and 0x10) are pinned; interior midpoints are generated between already-fixed
// neighbours at successively finer strides (8,4,2,1).  Each new midpoint is
// the average of its neighbours plus a random offset along a perpendicular
// picked from the segment direction, so the bolt gains detail each pass.
// ---------------------------------------------------------------------------
void lightning_subdivide(void)
{
    int inst = FUN_803826dc();

    float seedX = FLOAT_80615c84;
    float step  = FLOAT_80615c88;
    float y     = FLOAT_80615c80;

    // Initialise the 17 control points: straight line, endpoints marked fixed.
    unsigned char fixed[0x11];
    for (int i = 0x10; i >= 0; i--) {
        fixed[i] = (i == 0 || i == 0x10) ? 1 : 0;
        float* p = (float*)(DAT_805863c0 + i * 0xc + inst * 0xcc);
        p[0] = seedX;
        p[1] = y;
        p[2] = seedX;
        y -= step;
    }

    for (unsigned int stride = 8; stride != 0; stride = (int)stride >> 1) {
        for (unsigned int i = stride; (int)i < 0x10; i += stride) {
            if (fixed[i] != 0) continue;

            float* a = (float*)(DAT_805863c0 + (i - stride) * 0xc + inst * 0xcc);
            float* b = (float*)(DAT_805863c0 + (i + stride) * 0xc + inst * 0xcc);
            float* m = (float*)(DAT_805863c0 + i * 0xc + inst * 0xcc);

            float dx = FLOAT_80615c8c * a[0] - FLOAT_80615c8c * b[0];
            float dy = FLOAT_80615c8c * a[1] - FLOAT_80615c8c * b[1];
            float dz = FLOAT_80615c8c * a[2] - FLOAT_80615c8c * b[2];

            m[0] = FLOAT_80615c8c * a[0] + FLOAT_80615c8c * b[0];
            m[1] = FLOAT_80615c8c * a[1] + FLOAT_80615c8c * b[1];
            m[2] = FLOAT_80615c8c * a[2] + FLOAT_80615c8c * b[2];

            float len2 = dx * dx + dy * dy + dz * dz;
            if (FLOAT_80615c90 < len2) {
                float g = FLOAT_80615c80 * arc_rsqrt(len2, FLOAT_80615c94);
                dx *= g;
                dy *= g;
                dz *= g;
            }

            // choose the smallest-magnitude component to build a perpendicular
            unsigned int axis;
            if (fabsf(dx) < fabsf(dy) && fabsf(dx) < fabsf(dz)) axis = 0;
            else if (fabsf(dz) <= fabsf(dy))                    axis = 2;
            else                                                axis = 1;

            float perp[3];
            if (axis == 1) {
                perp[0] = -dz; perp[1] = FLOAT_80615c84; perp[2] = dx;
            } else if (axis == 0) {
                perp[2] = -dy; perp[0] = FLOAT_80615c84; perp[1] = dz;
            } else {
                perp[1] = -dx; perp[0] = dy; perp[2] = FLOAT_80615c84;
            }

            // rotate the perpendicular by a random angle about the segment dir
            float rot[16];
            float dir[3] = { dx, dy, dz };
            double ang = (double)(float)((double)FLOAT_80615c98 * game_frand());
            mtx4_set_axis_angle(ang, rot, dir);
            mtx4_transform_vector_alias(rot, perp);

            // displace the midpoint, scaled by stride and a signed random
            float sc = (float)((double)CONCAT44(0x43300000,
                        (unsigned int)(stride ^ 0x80000000)) - DOUBLE_80615ca0) * FLOAT_80615c9c;
            m[0] += perp[0] * sc * (float)(game_frand() - (double)FLOAT_80615c8c);
            m[1] += perp[1] * sc * (float)(game_frand() - (double)FLOAT_80615c8c);
            m[2] += perp[2] * sc * (float)(game_frand() - (double)FLOAT_80615c8c);

            fixed[i] = 1;
        }
    }

    FUN_80382728();
}

// ---------------------------------------------------------------------------
// 0x80134f4c : regenerate all 32 bolt instances (0x20 subdivision passes).
// ---------------------------------------------------------------------------
void lightning_generate_all(void)
{
    for (int i = 0x20; i-- > 0; ) {
        lightning_subdivide();
    }
}

// ---------------------------------------------------------------------------
// 0x80134f88 : random bolt/instance index in [0, 31].
// ---------------------------------------------------------------------------
unsigned int lightning_random_index(void)
{
    return (unsigned int)game_rand() & 0x1f;
}

// ---------------------------------------------------------------------------
// 0x80134fac : build the oriented render frame for each of the 17 bolt points.
// For every point it forms the direction between the two endpoints, derives a
// stable perpendicular basis (same smallest-component trick as the generator),
// and writes the interpolated, width-scaled frame into DAT_80587d40.
//
//   width   half-thickness of the bolt (param_1)
//   inst    bolt instance index (param_2)
//   p0, p1  bolt endpoints (param_3, param_4)
// ---------------------------------------------------------------------------
void lightning_build_frames(double width, int inst, float* p0, float* p1)
{
    float t   = FLOAT_80615c80;      // blend param, walks from 1 -> 0
    float step = FLOAT_80615c88;

    for (int i = 0x10; i >= 0; i--) {
        int off = i * 0xc;
        float ta = t - (t - step);   // per-point endpoint weights

        float* radiusPt = (float*)(DAT_805863c0 + off + inst * 0xcc);
        float baseR  = radiusPt[0];
        float tipR   = *(float*)((DAT_805863c0 + off + inst * 0xcc) + 8);

        double dx = (double)(p1[0] - p0[0]);
        double dy = (double)(p1[1] - p0[1]);
        double dz = (double)(p1[2] - p0[2]);

        float len2 = (float)(dz * dz + (double)(float)(dx * dx + (double)(float)(dy * dy)));
        if (FLOAT_80615c90 < len2) {
            float e = arc_rsqrt(len2, FLOAT_80615c94);
            double g = (double)(FLOAT_80615c80 * e * -(FLOAT_80615c8c * len2 * e * e - FLOAT_80615c94));
            dx = (double)(float)(dx * g);
            dy = (double)(float)(dy * g);
            dz = (double)(float)(dz * g);
        }

        unsigned int axis;
        if (fabs(dx) < fabs(dy) && fabs(dx) < fabs(dz)) axis = 0;
        else if (fabs(dz) <= fabs(dy))                  axis = 2;
        else                                            axis = 1;

        double u, v, w;   // perpendicular basis component
        if (axis == 1)      { v = (double)FLOAT_80615c84; u = -dz; w = dx; }
        else if (axis == 0) { u = (double)FLOAT_80615c84; v = dz;  w = -dy; }
        else                { v = -dx; u = dy; w = (double)FLOAT_80615c84; }

        float* out0 = (float*)(DAT_80587d40 + off);
        float* out1 = (float*)(DAT_80587d40 + off + 4);
        float* out2 = (float*)(DAT_80587d40 + off + 8);

        *out0 = (float)(width * (double)((float)(v * dz - (double)(float)(w * dy)) * tipR)
                        + (double)(float)(width * (double)(float)(u * baseR)
                        + (double)(p0[0] * t + p1[0] * ta)));
        *out1 = (float)(width * (double)((float)(w * dx - (double)(float)(u * dz)) * tipR)
                        + (double)(float)(width * (double)(float)(v * baseR)
                        + (double)(p0[1] * t + p1[1] * ta)));

        float carry = p0[2] * t;
        t -= step;
        *out2 = (float)(width * (double)((float)(u * dy - (double)(float)(v * dx)) * tipR)
                        + (double)(float)(width * (double)(float)(w * baseR)
                        + (double)(carry + p1[2] * ta)));
    }
}

// ===========================================================================
// Address range: 0x80164690 - 0x80164c00 | batch_438 | 2026-09-06
//
// Arc value-noise LUT + instance pool.  A second, table-driven noise layer
// used by lightning_bolt_render (0x800f0e88) to give each electric arc a
// smooth-but-jagged 1-D offset profile.  A 96-entry float lookup table
// (DAT_8058a0f8) is filled once at init (arc_noise_init) by evaluating an
// fBm-style octave sum of a cheap integer hash noise, then sampled per frame
// with clamped linear interpolation (arc_noise_lookup).  The pool init
// (arc_pool_init) resets a 10-slot arc-instance table (0x28 bytes/slot).
//
//   arc_noise_nop               0x80164690  empty stub
//   arc_noise_hash              0x80164694  integer hash -> float ~[-1,1]
//   arc_noise_smoothed          0x801646f0  1-2-1 weighted smoothing of hash
//   arc_noise_cubic_interp      0x80164758  cubic (Catmull-like) interp of 4 pts
//   arc_noise_sample_at         0x80164838  sample smoothed noise at real x
//   arc_noise_accumulate_octaves 0x80164910 fBm octave sum (returns normalized)
//   arc_noise_build_table       0x80164a54  fill 96-entry DAT_8058a0f8 LUT
//   arc_noise_init              0x80164af0  init wrapper -> build_table
//   arc_noise_lookup            0x80164b10  clamp[0,1]+lerp sample of the LUT
//   arc_pool_init               0x80164c00  reset 10-slot arc instance pool

// --- rodata tuning constants for the noise layer (0x8061627c..0x806162bc) ---
extern const float  FLOAT_80616280;    // 0.25  (hash normalize divisor)
extern const double DOUBLE_80616288;   // 0x43300000_80000000 int->double magic
extern const float  FLOAT_80616290;    // 0.5   (1-2-1 outer weight)
extern const float  FLOAT_80616294;    // 2.0   (1-2-1 centre weight / recip)
extern const double DOUBLE_80616298;   // smoothstep arg A
extern const double DOUBLE_806162a0;   // smoothstep arg B
extern const float  FLOAT_806162a8;    // 0.0   (clamp low / table start)
extern const float  FLOAT_806162ac;    // 1.0   (clamp high)
extern const float  FLOAT_806162b0;    // 48.0  (table X scale)
extern const float  FLOAT_806162b4;    // ~0.01053 = 1/95 (table X step)
extern const float  FLOAT_806162b8;    // 95.0  (lookup index scale)

// 96-entry float noise lookup table.
extern unsigned char DAT_8058a0f8[];

// 10-slot arc instance pool base pointer (0x28 bytes/slot, 0x194 total).
extern void* DAT_80612238;

// Runtime / math helpers (self-externed; different arity across TUs).
extern double FUN_80390b44(double a, double b);   // smoothstep/pow easing helper
extern double FUN_803900e4(double x);             // ceil-ish
extern double FUN_80390324(double x);             // floor
extern void   FUN_80164d74(void);                 // register arc particle defs

// Forward decls within this section.
double arc_noise_hash(unsigned int n);
double arc_noise_smoothed(unsigned int n);
double arc_noise_cubic_interp(double y0, double y1, double y2, double y3, double t);
double arc_noise_sample_at(double x);
double arc_noise_accumulate_octaves(int octaves, double amplitude, double frequency);
void   arc_noise_build_table(void);

// ---------------------------------------------------------------------------
// 0x80164690 : empty stub (alignment / unused slot).
// ---------------------------------------------------------------------------
void arc_noise_nop(void)
{
}

// ---------------------------------------------------------------------------
// 0x80164694 : cheap integer hash -> float in roughly [-1, 1].
// Classic xorshift-multiply hash: mix n, run a cubic polynomial, mask to the
// low 31 bits, flip sign bit, then convert to float and normalize by 0.25.
// ---------------------------------------------------------------------------
double arc_noise_hash(unsigned int n)
{
    unsigned int h = (n << 0xd) ^ n;
    unsigned int mixed = (h * (h * h * 0x3d73 + 0xc0ae5) + 0x5208dd0d) & 0x7fffffff;
    // (int->double via PPC magic) then negate-normalize into a signed float.
    double v = (double)(int)(mixed ^ 0x80000000);
    return (double)((float)(v / (double)FLOAT_80616280));
}

// ---------------------------------------------------------------------------
// 0x801646f0 : 1-2-1 weighted smoothing of the hash at n-1, n, n+1.
//   result = 0.5*h(n-1) + 2.0*h(n) + 0.5*h(n+1)   (weights baked in rodata)
// ---------------------------------------------------------------------------
double arc_noise_smoothed(unsigned int n)
{
    double acc;
    acc = (double)(float)(arc_noise_hash(n - 1) * (double)FLOAT_80616290);
    acc = (double)(float)(arc_noise_hash(n)     * (double)FLOAT_80616294 + acc);
    return (double)(float)(arc_noise_hash(n + 1) * (double)FLOAT_80616290 + acc);
}

// ---------------------------------------------------------------------------
// 0x80164758 : cubic (Catmull-Rom-like) interpolation of four smoothed
// samples y0..y3 at fractional position t in [0,1].  The two easing weights
// come from FUN_80390b44(t, A) and FUN_80390b44(t, B).
// ---------------------------------------------------------------------------
double arc_noise_cubic_interp(double y0, double y1, double y2, double y3, double t)
{
    double d10 = (double)(float)(y0 - y1);
    double d2  = (double)(float)((double)(float)(y3 - y2) - d10);
    double wA  = (double)(float)FUN_80390b44(t, DOUBLE_80616298);
    double wB  = (double)(float)FUN_80390b44(t, DOUBLE_806162a0);

    return (double)(float)(y1 +
        (double)(float)((double)(float)(y2 - y0) * t +
            (double)(float)(d2 * wB +
                (double)(float)((double)(float)(d10 - d2) * wA))));
}

// ---------------------------------------------------------------------------
// 0x80164838 : sample the smoothed noise at real position x.  Takes the four
// smoothed samples straddling floor(x) and cubic-interpolates by the fraction.
// Returns the interpolated noise value.
// ---------------------------------------------------------------------------
double arc_noise_sample_at(double x)
{
    unsigned int i = (unsigned int)x;
    double y0 = arc_noise_smoothed(i - 1);
    double y1 = arc_noise_smoothed(i);
    double y2 = arc_noise_smoothed(i + 1);
    double y3 = arc_noise_smoothed(i + 2);

    double frac = (double)(float)(x -
        (double)(float)((double)(int)(i ^ 0x80000000) - DOUBLE_80616288));

    return arc_noise_cubic_interp(y0, y1, y2, y3, frac);
}

// ---------------------------------------------------------------------------
// 0x80164910 : fractional-Brownian-motion octave accumulation.  Sums
//   amplitude * sample_at( amplitude * ease(A, i) )
// weighted by ease(frequency, i) over `octaves` iterations, then normalizes
// by the total weight.  r3=octaves, f1=amplitude, f2=frequency.
// (The 0x803826f0/0x8038273c calls in the raw listing are the f24-f31 save/
// restore runtime stubs, not semantic calls, and are omitted here.)
// ---------------------------------------------------------------------------
double arc_noise_accumulate_octaves(int octaves, double amplitude, double frequency)
{
    double sum        = FLOAT_80616294;   // seed accumulator
    double weightSum  = FLOAT_80616294;   // seed weight
    (void)frequency;                       // frequency drives the A ease arg

    for (int i = 0; i < octaves; i++) {
        double idx = (double)(float)((double)(int)((unsigned int)i ^ 0x80000000)
                                     - DOUBLE_80616288);

        double wA = (double)(float)FUN_80390b44(DOUBLE_80616298, idx);
        double wB = FUN_80390b44(amplitude, idx);
        (void)wB;

        double s = arc_noise_sample_at((double)(float)(amplitude * wA));
        weightSum += wA;
        sum = wA * ((double)FLOAT_806162a8 + s) + sum;
    }

    return (double)(float)(sum * (double)(float)((double)FLOAT_80616294 / weightSum));
}

// ---------------------------------------------------------------------------
// 0x80164a54 : build the 96-entry noise lookup table DAT_8058a0f8.  Walks the
// table from index 0x5f down to 0, sampling arc_noise_accumulate_octaves at a
// position that steps by FLOAT_806162b4 (=1/95) scaled by 48.0.
// ---------------------------------------------------------------------------
void arc_noise_build_table(void)
{
    double scale = (double)FLOAT_806162b0;   // 48.0
    double stepX = (double)FLOAT_806162b4;   // ~1/95
    float  x     = FLOAT_806162a8;           // 0.0, walks up by stepX

    for (int i = 0x60; --i >= 0; ) {
        double px = (double)x;
        double v  = arc_noise_accumulate_octaves(
                        (int)(double)(float)(scale * px),
                        0.0 /* placeholder amp arg comes via reg */,
                        (double)FLOAT_80616294);
        x = (float)(px + stepX);
        *(float*)(DAT_8058a0f8 + i * 4) = (float)v;
    }
}

// ---------------------------------------------------------------------------
// 0x80164af0 : init wrapper - fill the noise LUT.
// ---------------------------------------------------------------------------
void arc_noise_init(void)
{
    arc_noise_build_table();
}

// ---------------------------------------------------------------------------
// 0x80164b10 : sample the LUT for x in [0,1] with clamped linear interpolation.
// Scales x by 95, splits into integer index + fraction, and lerps between the
// two neighbouring table entries.
// ---------------------------------------------------------------------------
double arc_noise_lookup(double x)
{
    double xc = x;
    if ((double)FLOAT_806162ac < x) xc = (double)FLOAT_806162ac;   // clamp high
    if ((double)FLOAT_806162a8 <= xc) {
        if ((double)FLOAT_806162ac < x) x = (double)FLOAT_806162ac;
    } else {
        x = (double)FLOAT_806162a8;                                 // clamp low
    }

    double scaled = (double)(float)((double)FLOAT_806162b8 * x);    // * 95
    double hi = FUN_803900e4(scaled);   // ceil index
    double lo = FUN_80390324(scaled);   // floor index

    float frac = (float)(scaled -
        (double)(float)((double)(int)((unsigned int)(int)lo ^ 0x80000000)
                        - DOUBLE_80616288));

    float a = *(float*)(DAT_8058a0f8 + (int)lo * 4);
    float b = *(float*)(DAT_8058a0f8 + (int)hi * 4);
    return (double)((FLOAT_806162ac - frac) * a + frac * b);
}

// ---------------------------------------------------------------------------
// 0x80164c00 : reset the 10-slot arc instance pool.  Zeroes the 0x194-byte
// table then stamps every slot's +4 handle field to 0xffffffff (free).
// Slots are 0x28 bytes; 10 slots = 0x190, plus a 4-byte header = 0x194.
// ---------------------------------------------------------------------------
void arc_pool_init(void)
{
    memset_wrapper(DAT_80612238, 0, 0x194);

    int off = 0;
    for (int n = 10; n != 0; n--) {
        int handle = off + 4;
        off += 0x28;
        *(unsigned int*)((char*)DAT_80612238 + handle) = 0xffffffff;
    }

    FUN_80164d74();
}

// ===========================================================================
// Address range: 0x80164c54 - 0x80165838 | batch_439 | 2026-09-06
//
// Arc instance lifecycle + entity integration.  Builds on the batch_438 noise
// layer and the 10-slot pool (DAT_80612238, 0x28 bytes/slot, free-index at
// +0x190/400).  Provides pool allocation, per-frame update (particle size,
// positional sound, and a damage sweep against nearby characters), spawning a
// new arc into the ring, and the entity render-callback plumbing that attaches
// an arc effect to a scene entity.
//
//   arc_pool_alloc            0x80164c54  alloc the 0x194 pool, then arc_pool_init
//   arc_render_init_slot      0x80164c88  init a render-prop slot for an arc
//   arc_register_particle_defs 0x80164d74 register the arc particle defs/emitters
//   arc_update                0x80165060  per-frame update + damage sweep
//   arc_spawn                 0x801653d8  spawn an arc into the ring by type
//   arc_type_is_valid         0x801656ec  arc type-code whitelist
//   arc_entity_render_cb      0x80165730  entity render callback -> arc_spawn
//   arc_entity_attach         0x801657a0  attach arc render cb + flags to entity
//   arc_entity_type_allowed   0x801657f4  entity subtype filter (2/3/5/7)
//   arc_clamp_to_bounds       0x80165838  clamp a point to the nearest node AABB

// The shared decls declare mem_alloc_aligned as void-returning 3-arg; the arc
// pool call is the 16-arg PPC ABI variant, so alias it to the real arity.
typedef void* (*arc_mem_alloc_fn)(void* a, void* b, void* c, void* d, void* e, void* f,
                                  void* g, void* h, int align, int size, int tag,
                                  int r6, int r7, int r8, int r9, int r10);
#define ARC_MEM_ALLOC ((arc_mem_alloc_fn)&mem_alloc_aligned)

extern void  leaf_31e8(unsigned int dst, unsigned int src, int size);   // small mem helper
// render_prop_get_value_at_index / scene_node_get_render_data / scene_get_node_aabb /
// particle_handle_is_valid / sound_play_request / entity_set_render_type resolve
// through generated_decls.h (canonical signatures) - do NOT redeclare here.

// Particle system API (self-externed for arity clarity where not canonical).
extern void particle_def_create(double a, double b, void* def, int c, int d, int e, int id, int f);
extern void particle_def_set_size_keyframes(double a, double b, double c, double d, unsigned int k0, unsigned int k1, unsigned int k2);
extern void particle_def_set_size_start_end(double a, double b, double c, double d);
extern void particle_def_set_color_end(double a, double b, double c, double d);
extern void particle_def_set_velocity_ranges(double a, double b, double c, double d, double e, double f);
extern void particle_def_set_drag(double a);
extern void particle_emitter_create(double a, double b, double c, void* src, void* def, int d, int flags);
extern void particle_emitter_set_color_range(double a, double b, double c, double d);
extern void particle_emitter_set_velocity_range(double a, double b);
extern void particle_emitter_set_lifetime_range(double a, double b);
extern void particle_emitter_spawn(int slot, void* def, int flags, float* pos0, float* pos1);
extern double particle_cached_get_progress(int* h);
extern void particle_cached_set_size_range(double a, double b, int* h);
extern void particle_cached_set_lifetime(double t, int* h);
extern void particle_cached_set_gravity(double g, int* h);
extern void particle_handle_destroy(int* h);

extern void sound_update_3d_source(double a, double b, double c, int* p0, float* p1, int id, int f);
extern void sound_stop_handle(void);
extern void sound_init_play_params(int p);

extern void   character_damage_effect_wrapper(void);
extern int    player_ai_is_ranged_weapon(int kind);
extern void   switch_slot_reset(int p);
extern void   damage_dispatch_to_entity(double a, double b, double c, double d, double e, double f, double g, double h, int* ctx);
extern double math_sqrt(double x);
extern void   FUN_8037dc94(double a, double b, double c, int slot, int stride, float* p0, float* p1, unsigned int* q, int i, int j, int k);

// rodata singles used below.
extern const float FLOAT_806162c0;   // 0.0
extern const float FLOAT_806162c4;   // small const
extern const float FLOAT_806162c8;
extern const float FLOAT_806162cc;   // 1.0
extern const float FLOAT_806162d0;
extern const float FLOAT_806162d4;
extern const float FLOAT_806162d8;
extern const float FLOAT_806162dc;
extern const float FLOAT_806162e0;
extern const float FLOAT_806162e4;
extern const float FLOAT_806162e8;
extern const float FLOAT_806162ec;
extern const float FLOAT_806162f0;
extern const float FLOAT_806162f4;
extern const float FLOAT_806162f8;
extern const float FLOAT_806162fc;
extern const float FLOAT_80616300;
extern const float FLOAT_80616304;
extern const float FLOAT_80616308;
extern const float FLOAT_8061630c;
extern const float FLOAT_80616310;
extern const float FLOAT_80616314;
extern const float FLOAT_80616318;
extern const float FLOAT_8061631c;
extern const float FLOAT_80616320;
extern const float FLOAT_80616324;
extern const float FLOAT_80616328;
extern const float FLOAT_8061632c;
extern const float FLOAT_80616330;
extern const float FLOAT_80616334;
extern const float FLOAT_80616338;
extern const float FLOAT_8061633c;
extern const float FLOAT_80616340;
extern const float FLOAT_80616344;

// runtime globals.
extern int   DAT_80612614;    // game/scene mode selector
extern int   DAT_80612098;    // active character count
extern unsigned char DAT_80588610[];   // active-character pointer array
extern unsigned char DAT_806109b8[];   // 2 scene-node ids (bounds anchors)
extern unsigned char DAT_8058a278[];   // emitter source A
extern unsigned char DAT_8058a380[];   // particle def A
extern unsigned char DAT_8058a2fc[];   // emitter source B
extern unsigned char DAT_8058a44c[];   // particle def B
extern unsigned char DAT_8058a518[];   // particle def C
extern unsigned char DAT_8058a5e4[];   // emitter source C
extern unsigned char DAT_8058a668[];   // particle def D
extern unsigned char DAT_8058a734[];   // emitter source D
extern int   DAT_80611e2c;    // fallback render-prop record

// GPR save/restore helper (register spill stub, not a real call).
extern long long __save_gpr_25(void);
extern void      __restore_gpr_25(void);

void arc_register_particle_defs(void);   // fwd (0x80164d74)
void arc_spawn(int type, int soundEntity, float* pos);   // fwd (0x801653d8)
void arc_entity_render_cb(int* entity);  // fwd (0x80165730)

// ---------------------------------------------------------------------------
// 0x80164c54 : allocate the 10-slot arc instance pool (0x194 bytes, 0x20 align)
// and initialise it.
// ---------------------------------------------------------------------------
void arc_pool_alloc(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7, void* a8)
{
    DAT_80612238 = ARC_MEM_ALLOC(a1, a2, a3, a4, a5, a6, a7, a8, 0x20, 0x194, 1, 0, 0, 0, 0, 0);
    arc_pool_init();
}

// ---------------------------------------------------------------------------
// 0x80164c88 : initialise one render-prop-driven arc slot.  Resolves the
// render prop record for `instIndex`, copies its transform/colour block into
// `slot`, links `propRec`, and seeds the animated colour value.
//   instIndex  active arc index (was read from the save-stub reg artifact)
//   sceneCtx   scene context whose +0x1e8/+0x1fc hold the prop table
//   slot       destination arc render slot
//   propRec    render prop record to bind
// ---------------------------------------------------------------------------
void arc_render_init_slot(int instIndex, int* sceneCtx, unsigned int slot, unsigned int propRec)
{
    int* rec;
    if (instIndex < *(int*)((int)sceneCtx + 0x1e8)) {
        rec = (int*)(*(int*)((int)sceneCtx + 0x1fc) + instIndex * 4);
    } else {
        rec = (int*)&DAT_80611e2c;
    }

    leaf_31e8(slot, *(unsigned int*)(*rec + 0xc), 0x84);
    leaf_31e8(propRec, *(unsigned int*)(slot + 0x4c), 0xcc);

    float zero = FLOAT_806162c0;
    *(float*)(slot + 8)    = zero;
    *(float*)(slot + 0xc)  = zero;
    *(float*)(slot + 0x10) = zero;
    *(float*)(slot + 0x14) = zero;
    *(float*)(slot + 0x18) = zero;
    *(float*)(slot + 0x1c) = zero;
    *(unsigned int*)(slot + 0x4c) = propRec;

    int idx = render_prop_get_value_at_index(
                  *(int*)(propRec + 0x10),
                  *(int*)(*(int*)(*(int*)((int)sceneCtx + 4) + 4) + 0x14));
    *(unsigned int*)(propRec + 0x10) =
        *(unsigned int*)(*(int*)(*(int*)(*(int*)((int)sceneCtx + 4) + 4) + 0x10) + idx * 0x10);

    *(float*)(slot + 0x3c) = FLOAT_806162c4;
    *(float*)(slot + 0x40) = zero;
    *(float*)(slot + 0x34) = zero;
    *(float*)(slot + 0x38) = zero;
    *(float*)(slot + 0x48) = zero;
}

// ---------------------------------------------------------------------------
// 0x80164d74 : register the arc's particle definitions and emitters.  In the
// special scene mode (DAT_80612614 == 9) two render slots are bound directly;
// otherwise the full glow/spark/smoke particle set is authored.  A trailing
// common set (defs C/D) is always registered.
// ---------------------------------------------------------------------------
void arc_register_particle_defs(void)
{
    if (DAT_80612614 == 9) {
        int rd = (int)scene_node_get_render_data(3);
        arc_render_init_slot(rd, (int*)rd, (unsigned int)(unsigned char*)DAT_8058a278, (unsigned int)(unsigned char*)DAT_8058a380);
        arc_render_init_slot(rd, (int*)rd, (unsigned int)(unsigned char*)DAT_8058a2fc, (unsigned int)(unsigned char*)DAT_8058a44c);
    } else {
        particle_def_create((double)FLOAT_806162c8, (double)FLOAT_806162cc, &DAT_8058a380, 0, 1, 1, 0x326, 0);
        particle_def_set_size_keyframes((double)FLOAT_806162cc, (double)FLOAT_806162c0, (double)FLOAT_806162c0, (double)FLOAT_806162d0, 0x70707f00, 0x7f7f7f00, 0xf0f0000);
        particle_def_set_size_start_end((double)FLOAT_806162d4, (double)FLOAT_806162d8, (double)FLOAT_806162dc, (double)FLOAT_806162dc);
        particle_def_set_color_end((double)FLOAT_806162e0, (double)FLOAT_806162c8, (double)FLOAT_806162e4, (double)FLOAT_806162c0);
        particle_def_set_velocity_ranges((double)FLOAT_806162c0, (double)FLOAT_806162c0, (double)FLOAT_806162e8, (double)FLOAT_806162dc, (double)FLOAT_806162cc, (double)FLOAT_806162ec);
        particle_def_set_drag((double)FLOAT_806162c4);
        particle_emitter_create((double)FLOAT_806162f0, (double)FLOAT_806162c0, (double)FLOAT_806162f4, &DAT_8058a278, &DAT_8058a380, 0, 0x200000);
        particle_emitter_set_color_range((double)FLOAT_806162c4, (double)FLOAT_806162c0, (double)FLOAT_806162f8, (double)FLOAT_806162c0);
    }

    particle_def_create((double)FLOAT_806162fc, (double)FLOAT_806162cc, &DAT_8058a518, 0, 0, 3, 0x34f, 0);
    particle_def_set_size_keyframes((double)FLOAT_80616300, (double)FLOAT_806162c0, (double)FLOAT_806162c0, (double)FLOAT_80616304, 0xf0f0f4f, 0xf0f0f0f, 0);
    {
        double z = (double)FLOAT_806162c0;
        particle_def_set_size_start_end(z, (double)FLOAT_80616308, z, z);
    }
    particle_def_set_color_end((double)FLOAT_8061630c, (double)FLOAT_806162d0, (double)FLOAT_80616310, (double)FLOAT_806162c0);
    particle_def_set_velocity_ranges((double)FLOAT_80616314, (double)FLOAT_806162c0, (double)FLOAT_80616318, (double)FLOAT_806162c0, (double)FLOAT_806162cc, (double)FLOAT_80616318);
    particle_emitter_create((double)FLOAT_806162c8, (double)FLOAT_8061631c, (double)FLOAT_80616320, &DAT_8058a5e4, &DAT_8058a518, 0, 1);
    particle_emitter_set_color_range((double)FLOAT_806162c4, (double)FLOAT_806162c0, (double)FLOAT_806162f8, (double)FLOAT_806162c0);
    particle_emitter_set_velocity_range((double)FLOAT_806162c0, (double)FLOAT_806162c0);

    particle_def_create((double)FLOAT_80616320, (double)FLOAT_806162cc, &DAT_8058a668, 0, 1, 1, 0x32e, 0);
    particle_def_set_size_keyframes((double)FLOAT_80616324, (double)FLOAT_806162c0, (double)FLOAT_806162dc, (double)FLOAT_806162dc, 0x5f4a3f00, 0x5f4a3f00, 0x1e0f0a00);
    {
        double z = (double)FLOAT_806162c0;
        particle_def_set_velocity_ranges((double)FLOAT_80616328, z, z, z, (double)FLOAT_806162cc, (double)FLOAT_806162d0);
    }
    particle_def_set_size_start_end((double)FLOAT_806162d4, (double)FLOAT_806162d8, (double)FLOAT_806162c0, (double)FLOAT_806162e8);
    particle_def_set_color_end((double)FLOAT_80616304, (double)FLOAT_806162c0, (double)FLOAT_806162cc, (double)FLOAT_806162c0);
    particle_def_set_drag((double)FLOAT_806162e8);
    particle_emitter_create((double)FLOAT_806162e8, (double)FLOAT_80616300, (double)FLOAT_806162dc, &DAT_8058a734, &DAT_8058a668, 0, 1);
    particle_emitter_set_color_range((double)FLOAT_806162c4, (double)FLOAT_806162c0, (double)FLOAT_806162f8, (double)FLOAT_8061632c);
    particle_emitter_set_lifetime_range((double)FLOAT_806162c0, (double)FLOAT_806162c0);
}

// ---------------------------------------------------------------------------
// 0x80165060 : per-frame arc update.  For each of the 10 slots and its two
// particle handles: fade the arc by remaining progress, drive a 3D sound,
// and (for arc types 0/1) sweep the segment against every active character,
// applying damage where the arc crosses close enough to a body.
// ---------------------------------------------------------------------------
void arc_update(void)
{
    int base = (int)DAT_80612238;

    for (int s = 0, slotOff = 0; s < 10; s++, slotOff += 0x28) {
        int* slot   = (int*)(base + slotOff);
        int* pSound = slot + 1;
        float* pPos = (float*)(slot + 2);
        float* pDir = (float*)(slot + 5);
        int soundGate = 0;

        for (int h = 0, hOff = 0; h < 2; h++, hOff += 4) {
            int handle = *(int*)((int)slot + hOff + 0x20);
            int htmp = handle;
            if (!particle_handle_is_valid(&htmp)) continue;

            int hp = *(int*)((int)slot + hOff + 0x20);
            double prog = particle_cached_get_progress(&hp);

            double clamped = prog;
            if ((double)FLOAT_80616330 < prog) clamped = (double)FLOAT_80616330;
            if ((double)FLOAT_806162c0 <= clamped) {
                if ((double)FLOAT_80616330 < prog) prog = (double)FLOAT_80616330;
            } else {
                prog = (double)FLOAT_806162c0;
            }

            double life = (double)(float)((double)FLOAT_806162cc - prog);
            int hs = *(int*)((int)slot + hOff + 0x20);
            particle_cached_set_size_range(life, life, &hs);

            if ((double)FLOAT_806162f4 < life) {
                int type = *slot;
                if ((type == 0 || type == 1) && soundGate < 1) {
                    int soundId = (type == 0) ? 0xbfb : 0x51;
                    sound_update_3d_source(life, (double)FLOAT_806162c4, (double)FLOAT_806162cc, pSound, pPos, soundId, 0);

                    if (DAT_80612614 != 0x34) {
                        double maxReach = (double)(float)((double)FLOAT_80616334 * life);
                        for (int c = 0, cOff = 0; c < DAT_80612098; c++, cOff += 4) {
                            int* ch = *(int**)((char*)DAT_80588610 + cOff);
                            int body = *ch;

                            double px = (double)(float)slot[3];
                            double bx = (double)*(float*)(body + 0x68);
                            double dz = (double)(*(float*)(body + 100) - *pPos);
                            double dirY = (double)(float)slot[6];
                            double dy = (double)(*(float*)(body + 0x6c) - (float)slot[4]);
                            double reach = (double)(float)((double)(float)*(float*)((int)ch + 0xbc0) - bx);

                            double along = (double)(float)((double)(float)(
                                (float)(dy * (double)(float)slot[7]) +
                                (float)((double)(float)(bx - px) * dirY +
                                        (double)(float)(dz * (double)*pDir))) * dirY + px) - bx;

                            double clampedAlong = along;
                            if (reach < along) clampedAlong = reach;
                            if ((double)FLOAT_806162c0 <= clampedAlong) {
                                if (reach < along) along = reach;
                            } else {
                                along = (double)FLOAT_806162c0;
                            }

                            double t = (double)(float)((double)(float)(bx - px) + along);
                            double proj = (double)((float)(dy * (double)(float)slot[7]) +
                                          (float)(t * dirY + (double)(float)(dz * (double)*pDir)));

                            if ((double)FLOAT_806162c0 < proj && proj < maxReach) {
                                double dist = math_sqrt((double)(float)(dy * dy +
                                    (double)(float)(dz * dz + (double)(float)(t * t))));
                                if (FLOAT_80616338 < (float)(proj / dist)) {
                                    if (*slot == 0) {
                                        character_damage_effect_wrapper();
                                    } else if (player_ai_is_ranged_weapon((int)*(short*)((int)ch + 0x48)) == 0) {
                                        float negDir[3];
                                        negDir[0] = -*pDir;
                                        negDir[1] = -(float)slot[6];
                                        negDir[2] = -(float)slot[7];

                                        int ctx[16];
                                        switch_slot_reset((int)ctx);
                                        ctx[0] = *ch;
                                        ctx[4] = 0;   // +0x10
                                        int dmgId = (life <= (double)FLOAT_80616324) ? 0x16 : 0x17;
                                        ctx[5] = dmgId;   // +0x14
                                        ctx[6] = dmgId;   // +0x18
                                        *(float*)&ctx[9] = FLOAT_8061633c;
                                        *(float**)&ctx[11] = negDir;
                                        *(float**)&ctx[13] = pPos;
                                        *(float**)&ctx[15] = pDir;
                                        damage_dispatch_to_entity((double)FLOAT_8061633c, along, dz, t, dy, dirY, px, bx, ctx);
                                    }
                                }
                            }
                        }
                    }
                }
            } else {
                int hd = *(int*)((int)slot + hOff + 0x20);
                particle_handle_destroy(&hd);
                if (-1 < *pSound) {
                    sound_stop_handle();
                    *pSound = -1;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801653d8 : spawn an arc instance into the ring slot (index at base+400).
// Stores type + endpoints, tears down any live particles, then emits the
// type-appropriate particle set (and sound for type 0), and advances the ring.
//   type   arc type code
//   sndEnt sound-context entity handle
//   pos    world position (xyz)
// ---------------------------------------------------------------------------
void arc_spawn(int type, int sndEnt, float* pos)
{
    (void)sndEnt;
    __save_gpr_25();
    int base = (int)DAT_80612238;
    int slot = base + *(int*)(base + 400) * 0x28;
    float* p0 = pos;   // second endpoint comes from param path; kept as pos here
    float* p1 = pos;

    *(int*)slot = type;
    *(float*)(slot + 8)    = p0[0];
    *(float*)(slot + 0xc)  = p0[1];
    *(float*)(slot + 0x10) = p0[2];
    *(float*)(slot + 0x14) = p1[0];
    *(float*)(slot + 0x18) = p1[1];
    *(float*)(slot + 0x1c) = p1[2];

    if (-1 < *(int*)(slot + 4)) {
        sound_stop_handle();
        *(unsigned int*)(slot + 4) = 0xffffffff;
    }

    for (int i = 0, off = 0; i < 2; i++, off += 4) {
        int h = *(int*)(slot + off + 0x20);
        if (particle_handle_is_valid(&h)) {
            int hd = *(int*)(slot + off + 0x20);
            particle_handle_destroy(&hd);
        }
    }

    if (type == 2) {
        particle_emitter_spawn(slot + 0x20, &DAT_8058a734, 0x1000, p0, p1);
    } else if (type == 0) {
        int req[8];
        sound_init_play_params((int)(req + 1));
        *(unsigned char*)((char*)&req[0] + 7) = 1;
        req[1] = 0x646;
        *(float*)&req[4] = FLOAT_806162f8;
        *(float*)&req[0] = p0[0];
        *(float*)((char*)&req[0] + 4) = p0[1];
        *(float*)&req[2] = p0[2];
        sound_play_request(req + 1);

        double one = (double)FLOAT_806162cc;
        FUN_8037dc94(one, one, one, slot + 0x20, 0x20, p0, p1, (unsigned int*)0, 1, 0, 0);
        int hl = *(int*)(slot + 0x20);
        particle_cached_set_lifetime((double)FLOAT_806162f0, &hl);
    } else if (type == 1) {
        int off = 0, srcOff = 0;
        unsigned int passes = (countLeadingZeros(9 - DAT_80612614) >> 5) + 1;
        for (unsigned int i = 0; i < passes; i++) {
            particle_emitter_spawn(slot + off + 0x20, (unsigned char*)DAT_8058a278 + srcOff, 0x1000, p0, p1);
            int hg = *(int*)(slot + off + 0x20);
            particle_cached_set_gravity((double)FLOAT_80616334, &hg);
            srcOff += 0x84;
            off += 4;
        }
    } else if (type == 3) {
        particle_emitter_spawn(slot + 0x20, &DAT_8058a5e4, 0x1000, p0, p1);
    }

    *(int*)(base + 400) = (*(int*)(base + 400) + 1) % 10;
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// 0x801656ec : whitelist of arc type codes that produce an effect.
// Accepts 3..7, 0x20..0x21, and 0x28.
// ---------------------------------------------------------------------------
int arc_type_is_valid(int type)
{
    if (type < 0x22) {
        if (type < 8) {
            if (2 < type) return 1;
        } else if (0x1f < type) {
            return 1;
        }
    } else if (type == 0x28) {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80165730 : entity render callback - spawn an arc for a live entity whose
// slots +0x28/+0x2c hold the sound entity and position, gated by
// arc_type_is_valid(+0x24).
// ---------------------------------------------------------------------------
void arc_entity_render_cb(int* entity)
{
    if (DAT_80612238 != 0 && entity[10] != 0 && entity[0xb] != 0 &&
        arc_type_is_valid(entity[9]) != 0) {
        arc_spawn(*(int*)(*entity + 0x1a0), entity[10], (float*)entity[0xb]);
    }
}

// ---------------------------------------------------------------------------
// 0x801657a0 : attach the arc render callback to an entity, set the arc flag
// bit (0x10) and store the arc parameter at +0x1a0.
// ---------------------------------------------------------------------------
void arc_entity_attach(int arcParam, int entity)
{
    *(unsigned int*)(entity + 0x28) |= 0x10;
    entity_set_render_type(entity, (int)&arc_entity_render_cb);
    *(int*)(entity + 0x1a0) = arcParam;
}

// ---------------------------------------------------------------------------
// 0x801657f4 : return non-zero if `entity` is an arc-eligible subtype.
// The subtype at *(*(entity+0x7c)+0x34) must be one of {2,3,5,7}.
// ---------------------------------------------------------------------------
int arc_entity_type_allowed(int entity)
{
    if (entity == 0) return 0;
    int sub = *(int*)(*(int*)(entity + 0x7c) + 0x34);
    if (sub != 2 && sub != 5 && sub != 7 && sub != 3) return 0;
    return 1;
}

// ---------------------------------------------------------------------------
// 0x80165838 : clamp the current arc endpoint to the nearest of two scene-node
// AABBs (ids in DAT_806109b8).  Picks the closest box by Manhattan-ish overlap
// distance, then clamps each axis of the endpoint into that box, flipping the
// stored velocity sign when a clamp occurs.
// ---------------------------------------------------------------------------
void arc_clamp_to_bounds(int* pt)   // pt read from save-stub reg artifact
{
    bool anchored = false;
    for (int i = 0, off = 0; i < 2; i++, off += 4) {
        if (*(int*)((int)pt + 0x34) == *(int*)((char*)DAT_806109b8 + off)) anchored = true;
    }
    if (anchored) return;

    float box[16];
    double best = (double)FLOAT_80616340;
    int bestIdx = 0;

    for (int i = 0, nodeOff = 0; i < 2; i++, nodeOff += 4) {
        scene_get_node_aabb(*(int*)((char*)DAT_806109b8 + nodeOff), box + 3, box);
        double d = (double)FLOAT_80616344;
        for (int a = 0, axOff = 0; a < 3; a++, axOff += 4) {
            float hi = *(float*)((int)box + axOff + 0xc);
            float v  = *(float*)((int)pt + axOff + 4);
            if (hi <= v) {
                if (*(float*)((int)box + axOff) < v) {
                    d = (double)(float)(d + (double)(v - *(float*)((int)box + axOff)));
                }
            } else {
                d = (double)(float)(d + (double)(hi - v));
            }
        }
        if (d < best) { bestIdx = i; best = d; }
    }

    scene_get_node_aabb(*(int*)((char*)DAT_806109b8 + bestIdx * 4), box + 3, box);
    for (int a = 0, axOff = 0; a < 3; a++, axOff += 4) {
        int comp = (int)pt + axOff;
        float hi = *(float*)((int)box + axOff + 0xc);
        if (hi <= *(float*)(comp + 4)) {
            if (*(float*)((int)box + axOff) < *(float*)(comp + 4)) {
                *(float*)(comp + 4)   = *(float*)((int)box + axOff);
                *(float*)(comp + 0x10) = -ABS(*(float*)(comp + 0x10));
            }
        } else {
            *(float*)(comp + 4)   = hi;
            *(float*)(comp + 0x10) = ABS(*(float*)(comp + 0x10));
        }
    }
}
