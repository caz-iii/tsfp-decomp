// =============================================================================
// procgen_mesh_grid.cpp - procedural animated vertex-grid generators (batch 794).
//
// Five procedural mesh builders that fill sub-ranges of the shared vertex work
// buffer at (DAT_80612aa0 + offset) with 24x24 (0x18 x 0x18) grids of position
// vertices (3 floats each, 0xc-byte stride). All of them are phase-animated off
// the current frame tick and use the shared polynomial sin/cos approximation
// (math_poly_trig) to lay out the rings/rows. They are consumed by the swept
// "energy ribbon / plasma dome" renderers in the next cluster
// (FUN_802c5cb8 / FUN_802c5ec0), which submit the buffers to GX.
//
// Because these are pure math writers the only PPC-frame artefact is the frame
// tick that Ghidra reads back from the register-save stubs (FUN_803826xx); it is
// expressed here as an explicit `u32 frameTick` parameter (non-matching).
//
//   - procgen_sphere_vertex_grid (0x802c4ba8): a full sphere/ellipsoid ring
//     grid into buffer+0x3600. Two half-passes (rows 0..11 then 12..23) sweep
//     latitude while the inner loop sweeps 24 longitude steps.
//   - procgen_dome_vertex_grid (0x802c4e0c): a single-cap dome grid into
//     buffer+0x5100 - one latitude ring template (24 entries) tiled across 24
//     longitude columns.
//   - procgen_terrain_noise_grid (0x802c4ffc): a 24x24 fractal midpoint-
//     displacement height grid into buffer+0x6c00. Seeds the four corners, then
//     for each subdivision level jitters the midpoints with rng_counter_tick and
//     bilinearly interpolates the gaps, finally box-filters the interior and
//     partitions cells above/below the mean height into two draw lists.
//   - procgen_flat_row_grid (0x802c5520): a flat 24x24 lattice into buffer+0x8700
//     (constant Y/Z, X ramps across the row) - the ground/reference plane.
//   - procgen_cylinder_vertex_grid (0x802c55c0): a cylinder/tube grid into
//     buffer+0xa200 built from a 12-entry ring template swept twice (side wall
//     rings x24, then cap rings x24).
//
// Address range: 0x802c4ba8 - 0x802c5848
// Subsystem: graphics (procedural vertex-grid generation)
//
// NON-MATCHING: frame-tick register reads -> `frameTick` parameter; the CONCAT44
// int->double bias used for int->float conversion is reduced to a plain
// (float)(int) cast; the register-save/restore prologue stubs are dropped.
// =============================================================================

#include "common.h"

extern "C" {

// shared vertex work buffer base (sub-ranges are written per generator)
extern int DAT_80612aa0;

// current frame tick (PPC frame-helper idiom, was FUN_803826xx read-back).
// Used purely as a 32-bit tick seed for the int->float phase conversions.
extern unsigned int gfx_current_frame_tick(void);

// shared polynomial sin/cos approximation
extern double math_poly_trig(double x);
extern unsigned int rng_counter_tick(void);

// int->double bias sources (CONCAT44 low word)
extern unsigned int DAT_806109c0;

// procedural-mesh tuning pool (0x80619d.. / 0x80619e.. floats + biases)
extern const float FLOAT_80619dfc;   // sin/cos phase A offset
extern const float FLOAT_80619df0;   // radius scale A
extern const float FLOAT_80619df4;   // radius scale B / sign
extern const float FLOAT_80619df8;   // latitude step scale
extern const float FLOAT_80619e00;   // ring step scale (dome/sphere)
extern const float FLOAT_80619e10;   // secondary radius scale
extern const float FLOAT_80619e14;   // ring phase / constant Y
extern const float FLOAT_80619e18;   // terrain corner seed base
extern const float FLOAT_80619e1c;   // terrain amplitude scale
extern const float FLOAT_80619e20;   // terrain divisor
extern const float FLOAT_80619e24;   // terrain jitter divisor
extern const float FLOAT_80619e28;   // box-filter weight (1/9)
extern const float FLOAT_80619e2c;   // partition threshold factor
extern const float FLOAT_80619e38;   // sphere longitude step scale
extern const float FLOAT_80619e3c;   // sphere latitude step scale
extern const double DOUBLE_80619e08; // 2^52 int->double bias
extern const double DOUBLE_80619e30; // 2^52 int->double bias (jitter)

// -----------------------------------------------------------------------------
// procgen_sphere_vertex_grid (0x802c4ba8)
// Fill buffer+0x3600 with a sphere/ellipsoid ring grid (2 half-passes x 24 rings
// x 24 longitude steps). Position = radius(lat) * (cos/sin longitude), y = ring.
// -----------------------------------------------------------------------------
void procgen_sphere_vertex_grid(void)
{
    unsigned int tick;
    float* v;
    int ringPass;
    unsigned int lon;
    int latIndex;
    double phaseOff, latStep, ringStepA, ringStepB, radius, radiusB, bias;
    double ringPhase, cA, sA, radXZ, yConst;

    tick = gfx_current_frame_tick();
    ringPass = 0;
    lon = 0;
    v = (float*)(DAT_80612aa0 + 0x3600);
    ringStepA = (double)FLOAT_80619e00;
    phaseOff  = (double)FLOAT_80619dfc;
    radius    = (double)(FLOAT_80619df0 * ((float)(int)(tick ^ 0x80000000u) - (float)DOUBLE_80619e08));
    ringStepB = (double)FLOAT_80619df8;
    radiusB   = (double)(float)((double)FLOAT_80619df4 * radius);
    bias      = DOUBLE_80619e08;

    // upper hemisphere (rows 0..11, latitude index steps of 2)
    latIndex = 0;
    do {
        ringPhase = (double)(float)(ringStepB *
            (double)(float)((double)(int)(latIndex * 0 + (int)(lon ^ 0x80000000u)) - bias));
        (void)ringPhase;
        double lp = (double)(float)(ringStepB *
            (double)(float)((float)(int)(((unsigned int)0u) ^ 0x80000000u) - (float)bias));
        (void)lp;
        // latitude sweep uses the pass counter
        double latPhase = (double)(float)(ringStepB *
            (double)(float)((float)(int)((unsigned int)((ringPass * 2)) ^ 0x80000000u) - (float)bias));
        cA = math_poly_trig((double)(float)(latPhase - phaseOff));
        sA = math_poly_trig(latPhase);
        radXZ = (double)(float)(radius * cA);
        yConst = (double)(float)(radius * sA - radiusB);
        lon = 0;
        do {
            double lonPhase = (double)(float)(ringStepA *
                (double)(float)((float)(int)(lon ^ 0x80000000u) - (float)bias));
            double lc = math_poly_trig((double)(float)(lonPhase - phaseOff));
            v[0] = (float)(radXZ * lc);
            v[1] = (float)yConst;
            double ls = math_poly_trig(lonPhase);
            lon = lon + 1;
            v[2] = (float)(radXZ * ls);
            v = v + 3;
        } while ((int)lon < 0x18);
        ringPass = ringPass + 1;
    } while (ringPass < 0xc);

    // lower hemisphere (rows 12..23, latitude index continues at 0x18)
    ringStepA = (double)FLOAT_80619e00;
    int lat2 = 0xc;
    int idx2 = 0x18;
    phaseOff  = (double)FLOAT_80619dfc;
    double latShift = (double)FLOAT_80619df4;
    ringStepB = (double)FLOAT_80619df8;
    do {
        double latPhase = (double)(float)(ringStepB *
            (double)(float)((float)(int)((idx2 + 1) ^ 0x80000000) - (float)bias));
        cA = math_poly_trig((double)(float)(latPhase - phaseOff));
        sA = math_poly_trig(latPhase);
        radXZ = (double)(float)(radius * cA);
        yConst = (double)(float)(radius * sA - radiusB);
        lon = 0;
        do {
            double lonPhase = (double)(float)(ringStepA *
                (double)(float)(latShift +
                    (double)(float)((float)(int)(lon ^ 0x80000000u) - (float)bias)));
            double lc = math_poly_trig((double)(float)(lonPhase - phaseOff));
            v[0] = (float)(radXZ * lc);
            v[1] = (float)yConst;
            double ls = math_poly_trig(lonPhase);
            lon = lon + 1;
            v[2] = (float)(radXZ * ls);
            v = v + 3;
        } while ((int)lon < 0x18);
        lat2 = lat2 + 1;
        idx2 = idx2 + 2;
    } while (lat2 < 0x18);
}

// -----------------------------------------------------------------------------
// procgen_dome_vertex_grid (0x802c4e0c)
// Fill buffer+0x5100 with a single-cap dome: one 24-entry latitude ring template
// tiled across 24 longitude columns.
// -----------------------------------------------------------------------------
void procgen_dome_vertex_grid(void)
{
    unsigned int tick;
    float* v;
    unsigned int col;
    int i;
    double phaseOff, ringStep, radius, radiusB, bias, yConst;
    double ringPhase, cA, sA, radXZ;
    float ring[72];     // 24 entries x (x,y,z)

    tick = gfx_current_frame_tick();
    v = (float*)(DAT_80612aa0 + 0x5100);
    radius  = (double)(FLOAT_80619df0 * ((float)(int)(tick ^ 0x80000000u) - (float)DOUBLE_80619e08));
    radiusB = (double)(FLOAT_80619e10 * ((float)(int)(tick ^ 0x80000000u) - (float)DOUBLE_80619e08));
    cA = math_poly_trig((double)FLOAT_80619dfc);
    sA = math_poly_trig((double)FLOAT_80619e14);
    radXZ  = (double)(float)(radiusB * sA);
    ringStep = (double)FLOAT_80619e00;
    yConst = (double)(float)(radiusB * cA);
    phaseOff = (double)FLOAT_80619dfc;
    bias = DOUBLE_80619e08;

    // build the latitude ring template
    i = 0;
    unsigned int r = 0;
    int off = 0;
    do {
        double p = (double)(float)(ringStep *
            (double)(float)((float)(int)(r ^ 0x80000000u) - (float)bias));
        double pc = math_poly_trig((double)(float)(p - phaseOff));
        *(float*)((int)ring + off)     = (float)(radXZ * pc + radius);
        *(float*)((int)ring + off + 4) = (float)yConst;
        double ps = math_poly_trig(p);
        r = r + 1;
        *(float*)((int)ring + off + 8) = (float)(radXZ * ps);
        off = off + 0xc;
    } while ((int)r < 0x18);
    (void)i;

    // tile the ring across 24 longitude columns
    double colStep = (double)FLOAT_80619e00;
    col = 0;
    phaseOff = (double)FLOAT_80619dfc;
    do {
        double p = (double)(float)(colStep *
            (double)(float)((float)(int)(col ^ 0x80000000u) - (float)bias));
        double cc = math_poly_trig(p);
        double sc = math_poly_trig((double)(float)(p - phaseOff));
        int k = 0x18;
        off = 0;
        do {
            float tx = *(float*)((int)ring + off);
            float tz = *(float*)((int)ring + off + 8);
            v[0] = (float)(cc * (double)tx);
            v[1] = (float)(sc * (double)tx);
            v[2] = tz;
            v = v + 3;
            k = k - 1;
            off = off + 0xc;
        } while (k != 0);
        col = col + 1;
    } while ((int)col < 0x18);
}

// -----------------------------------------------------------------------------
// procgen_flat_row_grid (0x802c5520)
// Fill buffer+0x8700 with a flat 24x24 lattice: constant Y/Z per column, X ramps
// linearly across each row. Used as the ground / reference plane.
// -----------------------------------------------------------------------------
void procgen_flat_row_grid(unsigned int frameTick)
{
    float* v;
    int row;
    unsigned int col;
    float yConst, base, stepX, span;

    yConst = FLOAT_80619e14;
    row = 0;
    v = (float*)(DAT_80612aa0 + 0x8700);
    span  = FLOAT_80619e1c * ((float)(int)(frameTick ^ 0x80000000u) - (float)DOUBLE_80619e08);
    stepX = span / FLOAT_80619e20;
    base  = FLOAT_80619df4 * -span;
    do {
        col = 0;
        int k = 0x18;
        do {
            unsigned int c = col ^ 0x80000000u;
            col = col + 1;
            v[0] = (float)((float)(int)c - (float)DOUBLE_80619e08) * stepX + base;
            v[1] = yConst;
            v[2] = yConst;
            v = v + 3;
            k = k - 1;
        } while (k != 0);
        row = row + 1;
    } while (row < 0x18);
}

// -----------------------------------------------------------------------------
// procgen_cylinder_vertex_grid (0x802c55c0)
// Fill buffer+0xa200 with a cylinder/tube: a 12-entry ring template swept across
// 24 side-wall rings then 24 cap rings.
// -----------------------------------------------------------------------------
void procgen_cylinder_vertex_grid(void)
{
    unsigned int tick;
    float* v;
    int ring, seg, col, base;
    double phaseOff, radius, radiusB, bias;
    double cA, sA, radXZ, yConst, latStepA, latStepB;
    float tmpl[72];     // 12 entries x (x,y,z) template (plus headroom)

    tick = gfx_current_frame_tick();
    v = (float*)(DAT_80612aa0 + 0xa200);
    radius  = (double)(FLOAT_80619df0 * ((float)(int)(tick ^ 0x80000000u) - (float)DOUBLE_80619e08));
    radiusB = (double)(FLOAT_80619e10 * ((float)(int)(tick ^ 0x80000000u) - (float)DOUBLE_80619e08));
    cA = math_poly_trig((double)FLOAT_80619dfc);
    sA = math_poly_trig((double)FLOAT_80619e14);
    radXZ  = (double)(float)(radiusB * sA);
    yConst = (double)(float)(radiusB * cA);
    phaseOff = (double)FLOAT_80619dfc;
    bias = DOUBLE_80619e08;
    latStepA = (double)FLOAT_80619e38;

    // build the 12-entry ring template
    unsigned int r = 0;
    int off = 0;
    do {
        double p = (double)(float)(latStepA *
            (double)(float)((float)(int)(r ^ 0x80000000u) - (float)bias));
        double pc = math_poly_trig((double)(float)(p - phaseOff));
        *(float*)((int)tmpl + off)     = (float)(radXZ * pc + radius);
        *(float*)((int)tmpl + off + 4) = (float)yConst;
        double ps = math_poly_trig(p);
        r = r + 1;
        *(float*)((int)tmpl + off + 8) = (float)(radXZ * ps);
        off = off + 0xc;
    } while ((int)r < 0xc);

    // side wall: 24 rings x 12 template entries
    latStepB = (double)FLOAT_80619e3c;
    ring = 0;
    base = 0;
    do {
        seg = 0;
        col = 0;
        do {
            double p = (double)(float)(latStepB *
                (double)(float)((float)(int)((col + base) ^ 0x80000000) - (float)bias));
            double cc = math_poly_trig(p);
            double sc = math_poly_trig((double)(float)(p - phaseOff));
            float tx = *(float*)((int)tmpl + seg);
            float tz = *(float*)((int)tmpl + seg + 8);
            v[0] = (float)(cc * (double)tx);
            v[1] = (float)(sc * (double)tx);
            v[2] = tz;
            v = v + 3;
            col = col + 1;
            seg = seg + 0xc;
        } while (col < 0xc);
        ring = ring + 1;
        base = base + 0xc;
    } while (ring < 0x18);

    // cap: 24 rings x 12 entries, constant radius + Y
    latStepB = (double)FLOAT_80619e3c;
    ring = 0;
    base = 0;
    do {
        col = 0;
        do {
            double p = (double)(float)(latStepB *
                (double)(float)((float)(int)((col + base) ^ 0x80000000) - (float)bias));
            double ps = math_poly_trig(p);
            double pc = math_poly_trig((double)(float)(p - phaseOff));
            v[0] = (float)(radius * pc);
            v[1] = (float)(radius * ps);
            v[2] = (float)FLOAT_80619e14;
            v = v + 3;
            col = col + 1;
        } while (col < 0xc);
        ring = ring + 1;
        base = base + 0x18;
    } while (ring < 0x18);
}

// -----------------------------------------------------------------------------
// procgen_terrain_noise_grid (0x802c4ffc)
// Fill buffer+0x6c00 with a 24x24 fractal midpoint-displacement height grid,
// then box-filter the interior and split cells above/below the mean into two
// draw lists (appended after the grid).
// -----------------------------------------------------------------------------
void procgen_terrain_noise_grid(unsigned int frameTick)
{
    float* v;
    int levels;
    int step;
    int r, cRow, cCol;
    int i;
    float baseVal, ampScale, yConst;
    double mean, threshold, jitterScale, biasJ, bias;
    float* grid;
    float* out;

    // grid[] laid out as 24x24 x 3 floats; work in the +0x6c00 sub-buffer
    grid = (float*)(DAT_80612aa0 + 0x6c00);

    (void)gfx_current_frame_tick();     // frame-helper artefact
    yConst = FLOAT_80619e14;
    bias = DOUBLE_80619e08;
    ampScale = FLOAT_80619e1c * ((float)(int)(frameTick ^ 0x80000000u) - (float)bias);
    baseVal = FLOAT_80619df4 * -ampScale;
    ampScale = ampScale / FLOAT_80619e20;

    // count subdivision levels: while (1<<n) < 24
    levels = 0;
    for (i = 1; i < 0x18; i = i << 1)
        levels = levels + 1;

    // seed the full lattice X ramp + constant Y/Z (mirrors flat grid seed)
    {
        float* w = grid;
        unsigned int row = 0;
        int off = 0;
        do {
            unsigned int col = 0;
            int rem = 0x18;
            float rowX = (float)((double)(int)(row ^ 0x80000000u) - bias) * ampScale + baseVal;
            do {
                float* cell = (float*)((int)grid + off + (int)col * 0xc);
                col = col + 1;
                cell[0] = rowX;
                cell[1] = yConst;
                cell[2] = rowX;
                rem = rem - 1;
            } while (rem != 0);
            row = row + 1;
            off = off + 0x120;
        } while ((int)row < 0x18);
        (void)w;
    }

    // fractal midpoint displacement of the Y (+1) channel per level
    threshold  = (double)FLOAT_80619df4;   // reused as running mean seed
    jitterScale = (double)FLOAT_80619e24;
    biasJ = DOUBLE_80619e30;
    step = 0x18;
    for (i = 1; i < levels; i = i + 1) {
        step = step >> 1;
        // jitter the level's sample points
        for (r = step; r < 0x18; r = r + step * 2) {
            for (cCol = step; cCol < 0x18; cCol = cCol + step * 2) {
                unsigned int rw = rng_counter_tick();
                grid[r * 3 + cCol * 0x48 + 1] =
                    (float)((double)FLOAT_80619e18 *
                        (double)(float)(((float)((double)(int)rw - biasJ)) / jitterScale) +
                        (double)grid[r * 3 + cCol * 0x48 + 1]);
            }
        }
        // interpolate along rows
        for (cRow = 0; cRow < 0x18; cRow = cRow + step) {
            for (cCol = 0; cCol < 0x18 - step; cCol = cCol + step) {
                float lo = grid[cRow * 3 + cCol * 0x48 + 1];
                float hi = grid[cRow * 3 + (cCol + step) * 0x48 + 1];
                int k;
                for (k = 1; k < step; k = k + 1) {
                    grid[cRow * 3 + (cCol + k) * 0x48 + 1] =
                        ((hi - lo) / (float)((double)(int)(step ^ 0x80000000) - bias)) *
                        (float)((double)(int)(k ^ 0x80000000) - bias) +
                        grid[cRow * 3 + cCol * 0x48 + 1];
                }
            }
        }
        // interpolate along columns
        for (cRow = 0; cRow < 0x18; cRow = cRow + step) {
            for (cCol = 0; cCol < 0x18 - step; cCol = cCol + step) {
                float hi = grid[cRow * 0x48 + (cCol + step) * 3 + 1];
                float lo = grid[cRow * 0x48 + cCol * 3 + 1];
                int k;
                for (k = 1; k < step; k = k + 1) {
                    grid[cRow * 0x48 + (cCol + k) * 3 + 1] =
                        ((hi - lo) / (float)((double)(int)(step ^ 0x80000000) - bias)) *
                        (float)((double)(int)(k ^ 0x80000000) - bias) +
                        grid[cRow * 0x48 + cCol * 3 + 1];
                }
            }
        }
    }

    // 3x3 box-filter the interior, accumulate the mean height
    mean = (double)FLOAT_80619e14;
    {
        int row = 1;
        int off = 0x120;
        do {
            int colOff = 0xc;
            int rem = 0x16;
            do {
                int idx = colOff + off - 0x1b98;
                colOff = colOff + 0xc;
                *(float*)((int)grid + idx + 0x1b9c) = FLOAT_80619e28 * (
                    *(float*)((int)grid + idx + 0x1a18) +
                    *(float*)((int)grid + idx + 0x1a0c) +
                    *(float*)((int)grid + idx + 0x1a00) +
                    *(float*)((int)grid + idx + 0x1ba8) +
                    *(float*)((int)grid + idx + 0x1b9c) +
                    *(float*)((int)grid + idx + 0x1b90) +
                    *(float*)((int)grid + idx + 0x1930) +
                    *(float*)((int)grid + idx + 0x1918) +
                    *(float*)((int)grid + idx + 0x1924));
                mean = (double)(float)(mean + (double)*(float*)((int)grid + idx + 0x1b9c));
                rem = rem - 1;
            } while (rem != 0);
            row = row + 1;
            off = off + 0x120;
        } while (row < 0x17);
    }

    // partition cells above/below the mean into two appended draw lists
    threshold = (double)FLOAT_80619e2c;
    out = (float*)(DAT_80612aa0 + 0x6c00);
    {
        int row = 0;
        int off = 0;
        do {
            int colOff = 0;
            int rem = 0x18;
            do {
                float* cell = (float*)((int)grid + colOff + off);
                float h = cell[1];
                if ((float)(mean * threshold) <= h) {
                    out[0] = cell[0];
                    out[1] = h;
                    out[2] = cell[2];
                    out = out + 3;
                }
                colOff = colOff + 0xc;
                rem = rem - 1;
            } while (rem != 0);
            row = row + 1;
            off = off + 0x120;
        } while (row < 0x18);

        row = 0;
        off = 0;
        do {
            int colOff = 0;
            int rem = 0x18;
            do {
                float* cell = (float*)((int)grid + colOff + off);
                float h = cell[1];
                if (h < (float)(mean * threshold)) {
                    out[0] = cell[0];
                    out[1] = h;
                    out[2] = cell[2];
                    out = out + 3;
                }
                colOff = colOff + 0xc;
                rem = rem - 1;
            } while (rem != 0);
            row = row + 1;
            off = off + 0x120;
        } while (row < 0x18);
    }
    (void)v;
    (void)step;
}

} // extern "C"
