// =============================================================================
// plasma_morph_fx.cpp - procedural "plasma morph" point-cloud effect (batch 795).
//
// A self-contained VFX object that owns a 0xd818-byte work buffer (DAT_80612aa0)
// holding a 0x240-point (576) animated point cloud plus five target vertex grids
// built by the procgen_mesh_grid.cpp generators (batch 794). Each frame the live
// points ease toward the currently-selected target shape; the whole cloud is then
// drawn as camera-facing sprite rects. Used for a boss/level set-piece morphing
// energy blob that cycles sphere -> dome -> terrain -> cylinder -> ribbon -> flat.
//
// Work-buffer layout (offsets from DAT_80612aa0):
//   0x0000  live point cloud: 576 x { pos[3]@+0, vel[3]@+0xc } (0x18 stride)
//   0x3600  target grid 0 (sphere)      \
//   0x5100  target grid 1 (dome)         |  each 0x1b00 bytes, written by the
//   0x6c00  target grid 2 (terrain)      >  procgen_* generators; grid index
//   0x8700  target grid 3 (flat row)     |  = buffer+0xd800 selects the active one
//   0xa200  target grid 4 (cylinder)    /
//   0xbd00  ribbon/torus grid (plasma_fx_gen_ribbon_grid, this file)
//   0xd800  active target grid index (0..5, drives morph + rotate sub-modes)
//   0xd804  morph frame counter (eases in over 0x32 frames)
//   0xd808  running phase accumulator
//   0xd80c  ribbon amplitude (frame-tick scaled)
//   0xd810  spawn tick (int->float)   0xd814  rotation phase
//
//   - plasma_fx_gen_ribbon_grid (0x802c5848): build the ribbon/torus target grid
//     into buffer+0xbd00 (two swept half-rings x 24 + two cap fans x 24), phase-
//     animated off the frame tick.
//   - plasma_fx_init (0x802c5cb8): clear the point cloud, build all six target
//     grids (calls the five procgen_* generators + plasma_fx_gen_ribbon_grid),
//     reset the state header, and seed the cloud from grid 0.
//   - plasma_fx_create (0x802c5e6c): allocate the 0xd818 work buffer
//     (mem_alloc, tag 0x10000) into DAT_80612aa0 and run plasma_fx_init.
//   - plasma_fx_update (0x802c5ec0): per-frame tick. On a positive selector,
//     switch the active target grid (rebuild terrain if grid 2) and reseed the
//     ease targets; otherwise ease the live points toward their targets for the
//     first 0x32 frames, then apply the per-mode transform (mode 3 = vertical
//     sine warp, modes 4/5 = 2D rotation of a sub-range, mode 5 also twists a
//     second sub-range). Advances the phase accumulator.
//   - plasma_fx_render (0x802c62d8): project every cloud point through the shared
//     2048-entry sin/cos table (DAT_80583228) and draw it as a rotated sprite
//     rect (gx_draw_sprite_rect) using material/texture id 0x2dd, sub-material 2.
//
// Address range: 0x802c5848 - 0x802c66bc (exclusive)
// Subsystem: graphics (procedural point-cloud VFX, GX sprite submission)
//
// NON-MATCHING: frame-tick register reads (FUN_803826xx stubs) -> gfx_current_frame_tick();
// CONCAT44 int->double bias -> plain (float)(int); __save_gpr/__restore_gpr
// prologue stubs dropped; the FP-register spill locals are frame junk. Loop
// nests are expressed structurally rather than by pointer-walk.
// =============================================================================

#include "common.h"

extern "C" {

// procedural work buffer base (allocated by plasma_fx_create)
extern int DAT_80612aa0;
extern int DAT_80612f20;      // effect-active flag cleared by the create dispatcher

// frame tick (PPC frame-helper idiom)
extern unsigned int gfx_current_frame_tick(void);

// the five mesh-grid generators from procgen_mesh_grid.cpp (batch 794)
extern void procgen_sphere_vertex_grid(void);
extern void procgen_dome_vertex_grid(void);
extern void procgen_terrain_noise_grid(unsigned int frameTick);
extern void procgen_flat_row_grid(unsigned int frameTick);
extern void procgen_cylinder_vertex_grid(void);

// shared helpers
extern double math_poly_trig(double x);
extern int  mem_alloc(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                      unsigned long long p4, unsigned long long p5, unsigned long long p6,
                      unsigned long long p7, unsigned long long p8, int size, int align,
                      int tag, int p12, int p13, int p14, int p15, int p16);

// GX sprite/material helpers (named cross-module)
extern void gx_set_sub_material_id(int id);
extern void gx_set_active_material(int matId);
extern void gx_draw_sprite_rect(int x0, int y0, int x1, int y1,
                                unsigned int u0, unsigned int v0, unsigned int u1, unsigned int v1);
extern int  sprite_get_texture_id(int spriteId);

extern int  DAT_80610a20;     // active material table base ptr

// int->double bias source
extern unsigned int DAT_806109c0;

// shared 2048-entry sin/cos lookup table (0x1000 bytes; index masked to 0x7ff)
extern const float DAT_80583228;

// tuning pool
extern const float FLOAT_80619dfc;   // trig phase offset
extern const float FLOAT_80619df0;   // ribbon amplitude scale
extern const float FLOAT_80619df4;   // half / offset const
extern const float FLOAT_80619e08;   // (unused float view of bias)
extern const float FLOAT_80619e10;   // secondary scale / rotation step
extern const float FLOAT_80619e14;   // 0.0 / constant coordinate
extern const float FLOAT_80619e40;   // ribbon latitude step A
extern const float FLOAT_80619e44;   // ribbon longitude step
extern const float FLOAT_80619e48;   // ribbon cap step
extern const float FLOAT_80619e4c;   // morph ease factor
extern const float FLOAT_80619e50;   // mode3 warp amplitude
extern const float FLOAT_80619e54;   // mode3 warp frequency
extern const float FLOAT_80619e58;   // render rotation-from-phase scale
extern const float FLOAT_80619e5c;   // render uv scale
extern const float FLOAT_80619e60;   // render uv scale B
extern const float FLOAT_80619e64;   // render size bias
extern const float FLOAT_80619e68;   // render size scale
extern const double DOUBLE_80619e08; // 2^52 int->double bias

// rotation basis constants (cos/sin for modes 4/5)
extern const float DAT_80583240;     // sin
extern const float DAT_80583a40;     // cos

// forward
void plasma_fx_gen_ribbon_grid(void);
void plasma_fx_init(unsigned int frameTick);

// -----------------------------------------------------------------------------
// plasma_fx_gen_ribbon_grid (0x802c5848)
// Build the ribbon/torus target grid into buffer+0xbd00: two swept half-rings
// (6 lat x 24 lon each) mirrored on Y, then two cap fans (6 x 24). The ribbon
// amplitude is stored at buffer+0xd80c for reuse by the update transforms.
// -----------------------------------------------------------------------------
void plasma_fx_gen_ribbon_grid(void)
{
    unsigned int tick;
    float* v;
    unsigned int r;
    int i, j, base, off;
    double phaseOff, ringA, radXZ, radius, bias;
    double cA, sA, halfXZ, yTmp;
    float tmpl[72];

    tick = gfx_current_frame_tick();
    bias = DOUBLE_80619e08;
    radius = (double)(FLOAT_80619df0 * ((float)(int)(tick ^ 0x80000000u) - (float)bias));
    *(float*)(DAT_80612aa0 + 0xd80c) = (float)radius;
    double amp = (double)(FLOAT_80619e10 * ((float)(int)(tick ^ 0x80000000u) - (float)bias));
    v = (float*)(DAT_80612aa0 + 0xbd00);

    // upper half-ring template (6 latitude rows)
    cA = math_poly_trig((double)FLOAT_80619dfc);
    sA = math_poly_trig((double)FLOAT_80619e14);
    halfXZ = (double)(float)(amp * sA);
    double yA = (double)(float)(amp * cA);
    phaseOff = (double)FLOAT_80619dfc;
    ringA = (double)FLOAT_80619e40;
    r = 0; off = 0;
    do {
        double p = (double)(float)(ringA * (double)(float)((float)(int)(r ^ 0x80000000u) - (float)bias));
        double pc = math_poly_trig((double)(float)(p - phaseOff));
        *(float*)((int)tmpl + off)     = (float)(halfXZ * pc + radius);
        *(float*)((int)tmpl + off + 4) = (float)yA;
        double ps = math_poly_trig(p);
        r = r + 1;
        *(float*)((int)tmpl + off + 8) = (float)(halfXZ * ps);
        off = off + 0xc;
    } while ((int)r < 6);

    // sweep the upper template across 24 longitude columns
    double lonStep = (double)FLOAT_80619e44;
    i = 0; base = 0;
    do {
        j = 0; off = 0;
        do {
            double p = (double)(float)(lonStep * (double)(float)((float)(int)((j + base) ^ 0x80000000) - (float)bias));
            double cc = math_poly_trig(p);
            double sc = math_poly_trig((double)(float)(p - phaseOff));
            float tx = *(float*)((int)tmpl + off);
            float tz = *(float*)((int)tmpl + off + 8);
            v[0] = (float)(cc * (double)tx);
            v[1] = (float)(sc * (double)tx);
            v[2] = tz;
            v = v + 3;
            j = j + 1;
            off = off + 0xc;
        } while (j < 6);
        i = i + 1; base = base + 6;
    } while (i < 0x18);

    // lower half-ring template (rows 6..11)
    cA = math_poly_trig((double)FLOAT_80619dfc);
    sA = math_poly_trig((double)FLOAT_80619e14);
    halfXZ = (double)(float)(amp * sA);
    yA = (double)(float)(amp * cA);
    phaseOff = (double)FLOAT_80619dfc;
    ringA = (double)FLOAT_80619e40;
    r = 6; off = 0x48;
    do {
        double p = (double)(float)(ringA * (double)(float)((float)(int)(r ^ 0x80000000u) - (float)bias));
        double pc = math_poly_trig((double)(float)(p - phaseOff));
        *(float*)((int)tmpl + off + 8) = (float)(halfXZ * pc + radius);
        *(float*)((int)tmpl + off + 4) = (float)yA;
        double ps = math_poly_trig(p);
        r = r + 1;
        *(float*)((int)tmpl + off) = (float)(halfXZ * ps);
        off = off + 0xc;
    } while ((int)r < 0xc);

    // sweep the lower template (Y negated + ribbon amplitude offset)
    lonStep = (double)FLOAT_80619e44;
    i = 0; base = 0;
    do {
        j = 6; off = 0x48;
        do {
            double p = (double)(float)(lonStep * (double)(float)((float)(int)(((j + base) - 6) ^ 0x80000000) - (float)bias));
            double cc = math_poly_trig(p);
            double sc = math_poly_trig((double)(float)(p - phaseOff));
            double tz = (double)*(float*)((int)tmpl + off + 8);
            v[0] = *(float*)((int)tmpl + off);
            v[1] = (float)(sc * -tz - (double)*(float*)(DAT_80612aa0 + 0xd80c));
            v[2] = (float)(cc * tz);
            v = v + 3;
            j = j + 1;
            off = off + 0xc;
        } while (j < 0xc);
        i = i + 1; base = base + 6;
    } while (i < 0x18);

    // top cap fan (24 x rows 12..17)
    double capStep = (double)FLOAT_80619e48;
    i = 0; base = 0;
    do {
        j = 0xc;
        do {
            double p = (double)(float)(capStep * (double)(float)((float)(int)(((j + base) - 0xc) ^ 0x80000000) - (float)bias));
            double cc = math_poly_trig(p);
            double sc = math_poly_trig((double)(float)(p - phaseOff));
            v[0] = (float)(radius * sc);
            v[1] = (float)(radius * cc);
            v[2] = FLOAT_80619e14;
            v = v + 3;
            j = j + 1;
        } while (j < 0x12);
        i = i + 1; base = base + 0x18;
    } while (i < 0x18);

    // bottom cap fan (24 x rows 18..23), Y offset by ribbon amplitude
    double capStepB = (double)FLOAT_80619e44;
    i = 0; base = 0;
    do {
        j = 0x12;
        do {
            double p = (double)(float)(capStepB * (double)(float)((float)(int)(((j + base) - 0x12) ^ 0x80000000) - (float)bias));
            double cc = math_poly_trig(p);
            double sc = math_poly_trig((double)(float)(p - phaseOff));
            v[2] = (float)(radius * sc);
            v[1] = (float)(radius * cc - (double)*(float*)(DAT_80612aa0 + 0xd80c));
            v[0] = FLOAT_80619e14;
            v = v + 3;
            j = j + 1;
        } while (j < 0x18);
        i = i + 1; base = base + 0x18;
    } while (i < 0x18);
}

// -----------------------------------------------------------------------------
// plasma_fx_init (0x802c5cb8)
// Clear the 576-point cloud to the origin constant, build all six target grids,
// reset the state header, and seed the cloud targets from grid 0.
// -----------------------------------------------------------------------------
void plasma_fx_init(unsigned int frameTick)
{
    float c = FLOAT_80619e14;
    int off;
    int i;

    // clear the point cloud (576 points x 6 floats)
    off = 0;
    for (i = 0x240; i != 0; i = i - 1) {
        *(float*)(DAT_80612aa0 + off)        = c;
        *(float*)(DAT_80612aa0 + off + 4)    = c;
        *(float*)(DAT_80612aa0 + off + 8)    = c;
        *(float*)(DAT_80612aa0 + off + 0xc)  = c;
        *(float*)(DAT_80612aa0 + off + 0x10) = c;
        *(float*)(DAT_80612aa0 + off + 0x14) = c;
        off = off + 0x18;
    }
    *(float*)(DAT_80612aa0 + 0xd808) = FLOAT_80619e14;

    // build every target grid
    procgen_sphere_vertex_grid();
    procgen_dome_vertex_grid();
    procgen_terrain_noise_grid(frameTick);
    procgen_cylinder_vertex_grid();
    plasma_fx_gen_ribbon_grid();
    procgen_flat_row_grid(frameTick);

    // reset state header, seed ease targets from grid 0
    *(int*)(DAT_80612aa0 + 0xd800) = 0;
    *(int*)(DAT_80612aa0 + 0xd804) = 0x32;
    *(float*)(DAT_80612aa0 + 0xd810) = (float)((float)(int)(frameTick ^ 0x80000000u) - (float)DOUBLE_80619e08);
    *(float*)(DAT_80612aa0 + 0xd814) = FLOAT_80619e14;

    off = 0;
    int gsrc = 0;
    for (i = 0x240; i != 0; i = i - 1) {
        int grid = *(int*)(DAT_80612aa0 + 0xd800) * 0x1b00 + gsrc + 0x3600;
        *(int*)(DAT_80612aa0 + off + 0xc)  = *(int*)(DAT_80612aa0 + grid);
        *(int*)(DAT_80612aa0 + off + 0x10) = *(int*)(DAT_80612aa0 + grid + 4);
        *(int*)(DAT_80612aa0 + off + 0x14) = *(int*)(DAT_80612aa0 + grid + 8);
        off = off + 0x18;
        gsrc = gsrc + 0xc;
    }
}

// -----------------------------------------------------------------------------
// plasma_fx_create (0x802c5e6c)
// Allocate the 0xd818 work buffer into DAT_80612aa0 and run init. The many
// pass-through params are the mem_alloc allocator context.
// -----------------------------------------------------------------------------
void plasma_fx_create(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                      unsigned long long p4, unsigned long long p5, unsigned long long p6,
                      unsigned long long p7, unsigned long long p8, unsigned int frameTick,
                      int p10, int p11, int p12, int p13, int p14, int p15, int p16)
{
    (void)p10;
    DAT_80612aa0 = mem_alloc(p1, p2, p3, p4, p5, p6, p7, p8, 0xd818, 1, 0x10000,
                             p12, p13, p14, p15, p16);
    plasma_fx_init(frameTick);
}

// -----------------------------------------------------------------------------
// plasma_fx_update (0x802c5ec0)
// Per-frame tick. selector = frame-tick register value: >=0 switches the active
// target grid, <0 eases/transforms the live cloud.
// -----------------------------------------------------------------------------
void plasma_fx_update(void)
{
    int selector;
    int off, i;
    float cosR = DAT_80583a40;
    float sinR = DAT_80583240;

    selector = (int)gfx_current_frame_tick();

    if (selector < 0) {
        if (*(int*)(DAT_80612aa0 + 0xd804) < 0x32) {
            // ease-in: integrate velocity into position for all 576 points
            off = 0;
            for (i = 0x240; i != 0; i = i - 1) {
                float* p = (float*)(DAT_80612aa0 + off);
                p[3]  = p[3]  + p[0];
                p[4]  = p[4]  + p[1];
                p[5]  = p[5]  + p[2];
                off = off + 0x18;
            }
            *(int*)(DAT_80612aa0 + 0xd804) = *(int*)(DAT_80612aa0 + 0xd804) + 1;
        } else {
            int mode = *(int*)(DAT_80612aa0 + 0xd800);
            if (mode == 3) {
                // vertical sine warp: build a per-column offset row then apply
                *(float*)(DAT_80612aa0 + 0xd814) = *(float*)(DAT_80612aa0 + 0xd814) + FLOAT_80619e10;
                double freq = (double)FLOAT_80619e54;
                double amp  = (double)FLOAT_80619e50;
                double phaseOff = (double)FLOAT_80619dfc;
                double bias = DOUBLE_80619e08;
                unsigned int c = 0;
                int rowOff = 0;
                do {
                    double t = math_poly_trig((double)(float)((double)(float)(freq *
                        (double)(float)((float)(int)(c ^ 0x80000000u) - (float)bias) +
                        (double)*(float*)(DAT_80612aa0 + 0xd814)) - phaseOff));
                    *(float*)(DAT_80612aa0 + rowOff + 0x8704) = (float)(amp * -t);
                    c = c + 1;
                    rowOff = rowOff + 0xc;
                } while ((int)c < 0x18);

                unsigned int col = 0;
                int gridOff = 0;
                do {
                    int rOff = 0;
                    for (i = 0x18; i != 0; i = i - 1) {
                        *(float*)(DAT_80612aa0 + gridOff + 0x10) =
                            *(float*)(DAT_80612aa0 + rOff + 0x8704) *
                            (float)((float)(int)(col ^ 0x80000000u) - (float)bias);
                        gridOff = gridOff + 0x18;
                        rOff = rOff + 0xc;
                    }
                    col = col + 1;
                } while ((int)col < 0x18);
            } else if (mode == 4) {
                // rotate a sub-range about the (cos,sin) basis
                off = 0x1b00;
                for (i = 0x120; i != 0; i = i - 1) {
                    float* p = (float*)(DAT_80612aa0 + off + 0xc);
                    float x = p[0];
                    p[0] = x * cosR - p[1] * sinR;
                    p[1] = x * sinR + p[1] * cosR;
                    off = off + 0x18;
                }
            } else if (mode == 5) {
                off = 0x1b00;
                for (i = 0x90; i != 0; i = i - 1) {
                    float* p = (float*)(DAT_80612aa0 + off + 0xc);
                    float x = p[0];
                    p[0] = x * cosR - p[1] * sinR;
                    p[1] = x * sinR + p[1] * cosR;
                    off = off + 0x18;
                }
                off = 0x2880;
                for (i = 0x90; i != 0; i = i - 1) {
                    float* p = (float*)(DAT_80612aa0 + off + 0xc);
                    float y = p[2];
                    float z = *(float*)(DAT_80612aa0 + 0xd80c) + p[1];
                    p[2] = y * cosR - z * sinR;
                    p[1] = (y * sinR + z * cosR) - *(float*)(DAT_80612aa0 + 0xd80c);
                    off = off + 0x18;
                }
            }
        }
    } else {
        // switch active target grid, reseed ease targets
        *(int*)(DAT_80612aa0 + 0xd800) = selector;
        *(int*)(DAT_80612aa0 + 0xd804) = 0;
        if (*(int*)(DAT_80612aa0 + 0xd800) == 2)
            procgen_terrain_noise_grid((unsigned int)selector);

        float ease = FLOAT_80619e4c;
        off = 0;
        int gsrc = 0;
        for (i = 0x240; i != 0; i = i - 1) {
            int grid = *(int*)(DAT_80612aa0 + 0xd800) * 0x1b00 + gsrc;
            float* p = (float*)(DAT_80612aa0 + off);
            p[0] = ease * (*(float*)(DAT_80612aa0 + grid + 0x3600) - p[3]);
            p[1] = ease * (*(float*)(DAT_80612aa0 + grid + 0x3604) - p[4]);
            p[2] = ease * (*(float*)(DAT_80612aa0 + grid + 0x3608) - p[5]);
            off = off + 0x18;
            gsrc = gsrc + 0xc;
        }
    }
    *(float*)(DAT_80612aa0 + 0xd808) = *(float*)(DAT_80612aa0 + 0xd808) + FLOAT_80619e4c;
}

// -----------------------------------------------------------------------------
// plasma_fx_render (0x802c62d8)
// Project each of the 576 cloud points through the shared sin/cos table and draw
// it as a rotated sprite rect. Material/texture id 0x2dd, sub-material 2.
// -----------------------------------------------------------------------------
void plasma_fx_render(void)
{
    unsigned int camA, camB;
    int texId, matRec;
    unsigned int u0, v0, u1, v1;
    int i, off;
    double bias = DOUBLE_80619e08;

    // camera basis from the frame-helper register pair
    unsigned int frameHi = gfx_current_frame_tick();
    camA = frameHi;
    camB = frameHi;

    texId  = sprite_get_texture_id(0x2dd);
    matRec = DAT_80610a20 + texId * 0x58;
    unsigned int th = (unsigned int)*(unsigned short*)(matRec + 0x4e);
    unsigned int tw = (unsigned int)*(unsigned short*)(matRec + 0x52);
    gx_set_sub_material_id(2);
    gx_set_active_material(texId);

    // uv extents (scaled texture w/h)
    v1 = (unsigned int)(FLOAT_80619e5c * ((float)(int)((tw << 4) ^ 0x80000000u) - (float)bias));
    u1 = (unsigned int)(FLOAT_80619e5c * ((float)(int)((th << 4) ^ 0x80000000u) - (float)bias));
    v0 = (unsigned int)(FLOAT_80619e60 * ((float)(int)(tw ^ 0x80000000u) - (float)bias));
    u0 = (unsigned int)(FLOAT_80619e60 * ((float)(int)(th ^ 0x80000000u) - (float)bias));

    // rotation from the running phase accumulator (sin/cos table lookup)
    unsigned int rotIdx = (unsigned int)(FLOAT_80619e58 * *(float*)(DAT_80612aa0 + 0xd808) + FLOAT_80619df4);
    double s  = (double)(&DAT_80583228)[rotIdx & 0x7ff];
    double ns = -s;
    double cval = (double)*(float*)((int)&DAT_80583228 + ((rotIdx + 0x200) * 4 & 0x1ffc));
    double sizeScale = (double)FLOAT_80619e68;
    double sizeBias  = (double)FLOAT_80619e64;

    off = 0;
    for (i = 0; i < 0x240; i = i + 1) {
        float* p = (float*)(DAT_80612aa0 + off);
        int sx = (int)(s * (double)p[5] + (double)(float)(cval * (double)p[3] +
                       (double)(float)((float)(int)(camA ^ 0x80000000u) - (float)bias)));
        int sy = (int)((float)(int)(camB ^ 0x80000000u) - (float)bias + p[4]);
        int rot = (int)(ns * (double)p[3] + (double)(float)(cval * (double)p[5]));
        float sz = (float)(sizeScale * (double)(float)((float)(int)(rot ^ 0x80000000) - (float)bias) + sizeBias);
        gx_draw_sprite_rect(sx, sy,
                            (int)((float)(int)((unsigned int)(-sx) ) + sz),
                            (int)((float)(int)((unsigned int)(-sy) ) + sz),
                            u0, u1, v0, v1);
        off = off + 0x18;
    }
}

} // extern "C"
