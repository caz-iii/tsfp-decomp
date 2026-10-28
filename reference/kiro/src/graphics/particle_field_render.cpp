// =============================================================================
// particle_field_render.cpp - GX billboard renderers for the two bounded
// particle systems (batch 785).
//
// Two independent particle pools are rendered here as camera-facing textured
// quads, submitted directly to the GX write-gather FIFO port at 0xCC008000:
//
//   * particle_field_render (0x802aeee0) - renders "field A": the 800-entry
//     pool at DAT_80612a74 (0x28 stride) built by particle_field_sim.cpp. Each
//     live particle (life @+0x18 > 0) becomes a rotated quad; rotation angle is
//     taken from the shared 2048-entry sine/cosine table at DAT_80583228 and
//     the vertex colour is packed from four FUN_803825c0 quantised channels.
//
//   * particle_sys_b_render (0x802b01e4) - renders "field B": the 0x2c-stride
//     pool at DAT_80612a88 (see particle_sys_b.cpp). Same billboard math with a
//     per-particle spin offset (particle[10]) and a byte-swapped colour word.
//
// Both wrap their FIFO output in gx_begin_batch_mode5(GX_QUADS=0x92, ...) /
// gx_end_batch and set additive-style blend via gx_set_blend_mode(2).
//
// Address range: 0x802aeee0, 0x802b01e4 (non-contiguous)
// Subsystem: graphics (GX FIFO vertex submission, 0xCC008000)
// =============================================================================

#include "common.h"

// ---------------------------------------------------------------------------
// GX write-gather FIFO port (0xCC008000). Every store to this address pushes
// one word into the graphics command processor. The hardware accepts both f32
// and u32 stores to the same physical port, so we express each as a typed
// volatile store rather than a single mistyped lvalue.
// ---------------------------------------------------------------------------
static inline void gx_fifo_f32(float v) { *(volatile float*)0xCC008000u = v; }
static inline void gx_fifo_u32(u32 v)   { *(volatile u32*)0xCC008000u   = v; }

extern "C" {

// field A pool (0x28 stride, 800 entries) - shared with particle_field_sim.cpp
extern int DAT_80612a60;   // field A live count
extern int DAT_80612a70;   // field A owning context handle
extern int DAT_80612a74;   // field A pool base

// field B pool (0x2c stride) - shared with particle_sys_b.cpp
extern int DAT_80612a7c;   // field B live count
extern int DAT_80612a80;   // field B owning context handle
extern int DAT_80612a88;   // field B active buffer base

// shared 2048-entry sin/cos lookup table (0x1000 bytes of float, wraps at 0x7ff)
extern const float DAT_80583228;

extern const float  FLOAT_806199d0, FLOAT_806199dc;
extern const float  FLOAT_80619a28, FLOAT_80619a3c, FLOAT_80619a40, FLOAT_80619a44;
extern const float  FLOAT_80619a48, FLOAT_80619a4c;
extern const float  FLOAT_80619a50, FLOAT_80619a8c, FLOAT_80619a9c, FLOAT_80619aa0, FLOAT_80619aa4;
extern const double DOUBLE_80619a08;

// frame-enter / leave intrinsics (return current draw-context vertex data ptr)
extern unsigned long long FUN_803826e4(void); // field A: returns ctx ptr in hi 32
extern void FUN_80382730(void);
extern int  __save_gpr_25(void);
extern void __restore_gpr_25(void);

// GX helpers
extern void gx_set_blend_mode(int mode);
extern void gx_begin_batch_mode5(int prim, int vtxfmt, short count, int handle);
extern void gx_end_batch(void);
extern int  FUN_803825c0(double x); // quantise float channel -> int

} // extern "C"

// forward decls
void particle_field_render(void);   // 0x802aeee0
void particle_sys_b_render(void);   // 0x802b01e4

// sine/cosine table access: index masked to 2048 entries; the "+0x200" quarter
// turn gives the cosine partner.
static inline const float* sincos_table(void) { return &DAT_80583228; }

// ---------------------------------------------------------------------------
// 0x802aeee0 - render field A (800-entry, 0x28 stride) as camera-facing quads.
// ---------------------------------------------------------------------------
void particle_field_render(void)
{
    float* mtx = (float*)FUN_803826e4(); // current view/orientation basis
    if (DAT_80612a60 <= 0) {
        FUN_80382730();
        return;
    }

    gx_set_blend_mode(2);
    gx_begin_batch_mode5(0x92, 0, (short)(DAT_80612a60 << 2), DAT_80612a70);

    const double life_floor = (double)FLOAT_806199d0;
    const float* tbl = sincos_table();

    for (int i = 0, off = 0; i < 800; i++, off += 0x28) {
        float* p = (float*)(DAT_80612a74 + off);
        double life = (double)p[6];
        if (life <= life_floor)
            continue;

        float age = p[8];
        u32 ang = (u32)(FLOAT_80619a3c * p[9] + FLOAT_806199dc);
        // scale = age-driven falloff toward end of life
        float radius = (float)(life_floor /*0*/ + (double)(float)((double)FLOAT_80619a28 - life));
        float s = radius * tbl[ang & 0x7ff];
        float c = radius * *(const float*)((const char*)tbl + ((ang + 0x200) * 4 & 0x1ffc));

        // four rotated corner offsets
        float ox0 =  s + c, oy0 =  c - s;
        float ox1 = -s - c, oy1 = -c + s;
        float ox2 = -s + c, oy2 =  s - c;

        double m0 = (double)mtx[0],  m1 = (double)mtx[1];
        double m4 = (double)mtx[4],  m5 = (double)mtx[5];
        double m8 = (double)mtx[8],  m9 = (double)mtx[9];
        double px = (double)p[0],    py = (double)p[1],    pz = (double)p[2];

        // pack colour: age-driven RGBA via quantiser
        double t2 = (double)((float)((double)CONCAT44(0x43300000, (u32)age) - DOUBLE_80619a08)
                             * (float)(life * life));
        int r = FUN_803825c0((double)(float)((double)FLOAT_80619a4c * t2));
        u32 rc = ((u32)(r + (int)age) * 0x100) & 0xff00;
        int g = FUN_803825c0((double)(FLOAT_80619a48 *
                    (float)((double)CONCAT44(0x43300000, (u32)age) - DOUBLE_80619a08)));
        int b = FUN_803825c0(t2);
        b += g;
        u32 a2 = (u32)FUN_803825c0((double)(FLOAT_80619a44 *
                    (float)((double)CONCAT44(0x43300000, (u32)age) - DOUBLE_80619a08)));
        u32 a3 = (u32)FUN_803825c0((double)(float)((double)FLOAT_80619a40 * life));
        u32 col = (u32)b * 0x1000000 | rc << 8 | (a2 & 0xff) << 8 | (a3 & 0xff);

        // vertex 0
        gx_fifo_f32((float)((double)(float)(oy0 * m0 + (double)(float)(ox0 * m1)) + px));
        gx_fifo_f32((float)((double)(float)(oy0 * m4 + (double)(float)(ox0 * m5)) + py));
        gx_fifo_f32((float)((double)(float)(oy0 * m8 + (double)(float)(ox0 * m9)) + pz));
        gx_fifo_u32(col);
        gx_fifo_f32(FLOAT_80619a28);
        gx_fifo_f32(FLOAT_80619a28);

        // vertex 1
        gx_fifo_f32((float)((double)(float)(ox0 * m0 + (double)(float)(oy0 * m1)) + px));
        gx_fifo_f32((float)((double)(float)(ox0 * m4 + (double)(float)(oy0 * m5)) + py));
        gx_fifo_f32((float)((double)(float)(ox0 * m8 + (double)(float)(oy0 * m9)) + pz));
        gx_fifo_u32(col);
        gx_fifo_f32(FLOAT_80619a28);
        gx_fifo_f32(FLOAT_806199d0);

        // vertex 2
        gx_fifo_f32((float)((double)(float)(oy1 * m0 + (double)(float)(ox1 * m1)) + px));
        gx_fifo_f32((float)((double)(float)(oy1 * m4 + (double)(float)(ox1 * m5)) + py));
        gx_fifo_f32((float)((double)(float)(oy1 * m8 + (double)(float)(ox1 * m9)) + pz));
        gx_fifo_u32(col);
        gx_fifo_f32(FLOAT_806199d0);
        gx_fifo_f32(FLOAT_806199d0);

        // vertex 3
        gx_fifo_f32((float)((double)(float)(ox2 * m0 + (double)(float)(oy2 * m1)) + px));
        gx_fifo_f32((float)((double)(float)(ox2 * m4 + (double)(float)(oy2 * m5)) + py));
        gx_fifo_f32((float)((double)(float)(ox2 * m8 + (double)(float)(oy2 * m9)) + pz));
        gx_fifo_u32(col);
        gx_fifo_f32(FLOAT_806199d0);
        gx_fifo_f32(FLOAT_80619a28);
    }

    gx_end_batch();
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x802b01e4 - render field B (0x2c stride) as camera-facing quads. Rotation
// angle uses a per-particle spin (particle[10]); colour word is byte-swapped
// from the stored ABGR to the FIFO's RGBA order.
// ---------------------------------------------------------------------------
void particle_sys_b_render(void)
{
    float* mtx = (float*)__save_gpr_25(); // current view basis
    if (DAT_80612a7c <= 0) {
        __restore_gpr_25();
        return;
    }

    gx_set_blend_mode(2);
    gx_begin_batch_mode5(0x92, 0, (short)(DAT_80612a7c << 2), DAT_80612a80);

    const double base   = (double)FLOAT_80619a9c;
    const double lifeC  = (double)FLOAT_80619a8c;
    const double spinK  = (double)FLOAT_80619aa0;
    const double alphaK = (double)FLOAT_80619aa4;
    const float* tbl    = sincos_table();

    for (int i = 0, off = 0; i < DAT_80612a7c; i++, off += 0x2c) {
        float* p = (float*)(DAT_80612a88 + off);
        u32 ang = (u32)(spinK * (double)p[10] + base);
        float radius = (float)(base * (double)(float)(lifeC - (double)p[6]));
        float s = radius * tbl[ang & 0x7ff];
        float c = radius * *(const float*)((const char*)tbl + ((ang + 0x200) * 4 & 0x1ffc));

        float ox0 =  c - s, oy0 = s + c;
        float ox1 =  s - c, oy1 = -s + c;
        float ox2 = -s - c;

        int alpha = FUN_803825c0((double)(float)(alphaK * (double)p[6]));

        double m0 = (double)mtx[0], m1 = (double)mtx[1];
        double m4 = (double)mtx[4], m5 = (double)mtx[5];
        double m8 = (double)mtx[8], m9 = (double)mtx[9];
        float px = p[0], py = p[1], pz = p[2];
        float cw = p[8]; // stored packed colour word (as float bits reused)

        u32 cwi = (u32)cw;
        u32 col = (u32)((int)cw << 24) | ((cwi & 0xff00) << 8) |
                  ((cwi & 0xff0000) >> 8) | ((u32)(alpha << 24) >> 24);

        // vertex 0
        gx_fifo_f32((float)(ox0 * m0 + (double)(float)(oy0 * m1)) + px);
        gx_fifo_f32((float)(ox0 * m4 + (double)(float)(oy0 * m5)) + py);
        gx_fifo_f32((float)(ox0 * m8 + (double)(float)(oy0 * m9)) + pz);
        gx_fifo_u32(col);
        gx_fifo_f32(FLOAT_80619a8c);
        gx_fifo_f32(FLOAT_80619a8c);

        // vertex 1
        gx_fifo_f32((float)(oy0 * m0 + (double)(float)(ox1 * m1)) + px);
        gx_fifo_f32((float)(oy0 * m4 + (double)(float)(ox1 * m5)) + py);
        gx_fifo_f32((float)(oy0 * m8 + (double)(float)(ox1 * m9)) + pz);
        gx_fifo_u32(col);
        gx_fifo_f32(FLOAT_80619a8c);
        gx_fifo_f32(FLOAT_80619a50);

        // vertex 2
        gx_fifo_f32((float)(oy1 * m0 + (double)(float)(ox2 * m1)) + px);
        gx_fifo_f32((float)(oy1 * m4 + (double)(float)(ox2 * m5)) + py);
        gx_fifo_f32((float)(oy1 * m8 + (double)(float)(ox2 * m9)) + pz);
        gx_fifo_u32(col);
        gx_fifo_f32(FLOAT_80619a50);
        gx_fifo_f32(FLOAT_80619a50);

        // vertex 3
        gx_fifo_f32((float)(ox2 * m0 + (double)(float)(ox0 * m1)) + px);
        gx_fifo_f32((float)(ox2 * m4 + (double)(float)(ox0 * m5)) + py);
        gx_fifo_f32((float)(ox2 * m8 + (double)(float)(ox0 * m9)) + pz);
        gx_fifo_u32(col);
        gx_fifo_f32(FLOAT_80619a50);
        gx_fifo_f32(FLOAT_80619a8c);
    }

    gx_end_batch();
    __restore_gpr_25();
}
