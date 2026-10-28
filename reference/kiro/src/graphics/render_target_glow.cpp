// Address range: 0x8016bef8 - 0x8016c590 | batch_442 | 2026-09-06
//
// GLOW render-flag registry helpers + screen-space GLOW / soft-light
// render-target pass for a carried entity's sub-objects.
//
//   glow_flag_set_by_id       0x8016bef8  set glow flag (+0x114) on entity by id
//   glow_flag_set_group       0x8016bf2c  set glow flag + carry-state on grab group
//   glow_registry_lookup      0x8016bfb8  find registry entry by id, return +0xcc
//
// Screen-space GLOW / soft-light render-target pass for a carried entity's
// sub-objects (e.g. rotor/blade glow, weapon light halos).  The pass renders
// up to N (<=3) camera-facing textured billboard quads into a small 32x24
// off-screen render target with a colour-doubling (bright) TEV/blend setup,
// which the main scene later composites back as an additive glow.
//
// Frame flow (subobj_glow_render_pass):
//   * gated on the carried entity having >=1 sub-object (short at rec+0x74)
//   * one-time GX state / render-target init (glow_rt_setup, latched by
//     DAT_806122bc)
//   * save the active viewport (gx_viewport_save -> token), transpose the
//     camera view matrix and the entity object matrix, draw the quads
//     (glow_draw_quads), restore GX state (glow_rt_teardown_full/gx_viewport_restore).
//
//   glow_draw_quads         0x8016c00c  draw <=N bright billboard quads to the RT
//   glow_rt_setup           0x8016c1c0  one-time off-screen RT + TEV/blend init
//   glow_rt_teardown_full   0x8016c3f0  restore scissor + full render-state reset
//   glow_rt_teardown        0x8016c47c  lighter render-state reset (no scissor)
//   subobj_glow_render_pass 0x8016c4cc  per-frame entry (spawn-frame callback)
//   glow_render_stub        0x8016c590  empty stub
#include "common.h"
#include "generated_decls.h"

// ---- runtime globals ----
extern int*          DAT_80612268;   // glow registry table: [count][id][+0xcc field] ...
extern int           DAT_80611d54;   // active camera/render-context record
extern unsigned char DAT_806122b5;   // saved render-target id (from glow_rt_setup)
extern unsigned char DAT_806122bc;   // one-time-init latch for glow_rt_setup
extern int           DAT_80612314;
extern int           DAT_80612310;
extern int           DAT_80612250;
extern unsigned int  DAT_806109d0;
extern unsigned int  DAT_806109d8;
extern int           DAT_806109e8;   // saved scissor x
extern int           DAT_806109ec;   // saved scissor y
extern int           DAT_806109f0;   // saved scissor x2
extern int           DAT_806109f4;   // saved scissor y2
extern unsigned int  DAT_806164f0;   // packed clear/border colour (0x000000ff)

// ---- rodata float constants ----
extern float FLOAT_806164f8;   // 0.0
extern float FLOAT_806164fc;   // 1.0
extern float FLOAT_80616500;   // 80.0
extern float FLOAT_80616504;   // -79.0
extern float FLOAT_80616508;   // 32.0  (RT width)
extern float FLOAT_8061650c;   // 24.0  (RT height)
extern float FLOAT_80616510;   // -1.0
extern float FLOAT_80616514;   // 0.5
extern float FLOAT_80616518;   // 2.0
extern float FLOAT_8061651c;   // 0.9

// ---- texture blobs loaded into the RT (glow sprites) ----
extern unsigned char DAT_8058a920[];
extern unsigned char DAT_8058c680[];

// ---- GX / matrix runtime helpers (raw addresses; not yet decompiled) ----
extern void mtx4_transpose(float* src, float* dst);          // 0x803a1... (via generated_decls)

extern void  FUN_803826d4(void);                                             // frame scratch push
extern void  FUN_80382720(void);                                             // frame scratch pop
extern unsigned long long FUN_803826f0(void);                                // spawn-frame ptr (hi=rec)
extern void  FUN_8038273c(void);                                             // spawn-frame pop

extern void  FUN_803c861c(int rec, unsigned int* outColor);                  // read sub-object colour
extern void  FUN_803c84f4(int rec, float* ox, float* oy, float* oz);         // read sub-object world pos
extern void  FUN_803a160c(float* dst, float* src);                           // normalize/copy vec3
extern void  FUN_803c8610(int vtx, unsigned int* color);                     // set vertex colour
extern void  FUN_803c852c(double x, double y, double z, int vtx);            // set vertex position
extern void  FUN_803c83e8(double u0, double v0, double u1, double v1,        // set vertex UV/rect
                          double u2, double v2, int vtx);
extern void  FUN_803c8628(int vtx, unsigned int mask);                       // submit quad w/ mask
extern void  FUN_803c88b0(int a, int b, int c, int d, unsigned int e, int f, int g); // set TEV/blend combine

extern void  FUN_803cb468(double a, double b, double c, double d, double e, double f); // RT clear colour/params
extern void  FUN_803cb540(int x, int y, int w, int h);                       // set scissor rect
extern void  FUN_803c78c8(int x, int y, int w, int h);                       // set viewport rect
extern void  FUN_803c7978(int w, int h, int fmt, int flag);                  // configure RT surface
extern void  FUN_803cabec(int enable);                                       // enable RT
extern void  FUN_803cab98(int a, int b, int c, int d);                       // set clear params
extern void  FUN_803a142c(double a, double b, double c, double d, double e, double f, float* outMtx); // build proj/ortho
extern void  FUN_803cb098(float* mtx, int slot);                             // load projection matrix
extern void  FUN_803ca688(int mode);                                         // set cull mode
extern void  FUN_803c6aac(int mode);                                         // set z-mode
extern void  FUN_803c8874(int enable);                                       // enable colour write
extern void  FUN_803ca2f8(int a, int b);                                     // set alpha compare
extern void  FUN_803a10e8(float* outMtx);                                    // identity matrix
extern void  FUN_803a1248(double sx, double sy, double sz, float* outMtx);   // scale matrix
extern void  FUN_803cb1c8(float* mtx, int slot);                             // load model/view matrix
extern void  FUN_803cb248(float* mtx, int slot);                             // load texture matrix
extern void  FUN_803cb298(int slot);                                         // select texture matrix
extern void  FUN_803c86a4(int slot, unsigned int* color);                    // set TEV colour register
extern void  FUN_803c7080(void);                                             // flush / finish RT
extern unsigned int FUN_803c8224(unsigned int token, int flag);              // restore RT / viewport token

extern void  FUN_8016e8ac(int param_1);                                      // set render pass state
extern int   FUN_8016e020(unsigned char param_1);                            // set/get active RT id
extern void  FUN_8016e694(int slot, unsigned char* texData, unsigned int fmt);// load texture region
extern void  FUN_8016e444(int a, int b);                                     // set draw layer
extern void  FUN_8016ef70(void);
extern void  FUN_8016ef78(void);
void gx_material_commit_w(void);
extern void  FUN_80174628(void);
extern void  FUN_8017fab4(void);
extern unsigned int FUN_8017b0d8(void);                                      // save active viewport -> token

extern void  FUN_80181cf0(int a, int b, int c, int d);
extern void  FUN_80181e58(int a, int b, int c, int d, int e);
extern void  FUN_80181d50(int a, int b, int c, int d, int e, int f);
extern void  FUN_80181ec8(int a, int b, int c, int d, int e);
extern void  FUN_80181dd4(int a, int b, int c, int d, int e, int f);

// ---- entity/grab resolution (raw addresses; not yet decompiled) ----
extern int   FUN_8023c32c(unsigned int id);                  // resolve entity by link/handle id
extern int   FUN_8023c59c(int groupId, int outArray, int max);// collect up to max grabbed entities


// glow_flag_set_by_id (0x8016bef8)
// Resolve an entity by link/handle id and set its render-data glow flag (+0x114).
void glow_flag_set_by_id(unsigned int id)
{
    int ent;

    ent = FUN_8023c32c(id);
    if (ent != 0) {
        *(int*)(*(int*)(ent + 0x7c) + 0x114) = 1;
    }
}

// glow_flag_set_group (0x8016bf2c)
// Collect up to 4 grabbed sub-objects of a group and, for each, set the glow
// flag (+0x114) and force carry-state (+0x34 = 2) on its render data.
void glow_flag_set_group(int groupId)
{
    int list[6];
    int n;
    int off;
    int rd;

    n = FUN_8023c59c(groupId, (int)list, 4);
    off = 0;
    if (0 < n) {
        do {
            int ent = *(int*)((int)list + off);
            off = off + 4;
            rd = *(int*)(ent + 0x7c);
            *(int*)(rd + 0x114) = 1;
            *(int*)(rd + 0x34) = 2;
            n = n - 1;
        } while (n != 0);
    }
}

// glow_registry_lookup (0x8016bfb8)
// Linear scan of the glow registry table for the entry matching `id`, returning
// its +0xcc field (0 if the table is empty or no match).
int glow_registry_lookup(int id)
{
    int n;
    int off;

    if (DAT_80612268 == (int*)0) {
        return 0;
    }
    n = *DAT_80612268;
    off = 0;
    if (0 < n) {
        do {
            if (*(int*)((int)DAT_80612268 + off + 4) == id) {
                return *(int*)((int)DAT_80612268 + off + 0xcc);
            }
            off = off + 4;
            n = n - 1;
        } while (n != 0);
    }
    return 0;
}


// glow_draw_quads (0x8016c00c)
// Draw up to `count` (<=3) bright, camera-facing billboard quads into the RT.
// viewMtxT is the transposed camera view matrix (translation in the 0xc/0x1c/0x2c
// column), recBase is the first sub-object record (0x40-stride), count the number
// of quads. Each quad's colour is read from the sub-object and doubled per-channel
// (saturating at 0xff) for the bright/glow look.
void glow_draw_quads(float* viewMtxT, float* objMtxT, int recBase, int count)
{
    unsigned int r, g, b, a;
    unsigned int coverageMask;
    int recOff;
    int i;
    unsigned int color;
    float dx, dy, dz;
    float px, py, pz;
    unsigned char vtx[104];   // GX vertex scratch (auStack_68)

    (void)objMtxT;            // transposed obj matrix built by caller; RT uses view column

    FUN_803826d4();
    coverageMask = 0;
    recOff = 0;

    for (i = 0; i < count; i = i + 1) {
        int bit = 1 << i;
        coverageMask = coverageMask | bit;

        FUN_803c861c(recBase + recOff, &color);
        FUN_803c84f4(recBase + recOff, &px, &py, &pz);

        // position delta = view-matrix translation column - sub-object pos
        dx = -(*(float*)((int)viewMtxT + 0x0c)) + px;
        dy =  *(float*)((int)viewMtxT + 0x1c) - py;
        dz =  *(float*)((int)viewMtxT + 0x2c) - pz;
        {
            float pos[3];
            pos[0] = dx; pos[1] = dy; pos[2] = dz;
            FUN_803a160c(pos, pos);
            dx = pos[0]; dy = pos[1]; dz = pos[2];
        }

        // brighten: double each channel, saturate at 0xff
        r = (color >> 0x18) << 1;              if (r > 0xff) r = 0xff;
        g = ((color >> 0x10) & 0xff) << 1;     if (g > 0xff) g = 0xff;
        b = ((color >> 0x08) & 0xff) << 1;     if (b > 0xff) b = 0xff;
        a = (color & 0xff) << 1;               if (a > 0xff) a = 0xff;
        color = (r << 0x18) | (g << 0x10) | (b << 0x08) | a;

        FUN_803c8610((int)vtx, &color);
        FUN_803c852c((double)dx, (double)dy, (double)dz, (int)vtx);
        FUN_803c83e8((double)FLOAT_806164f8, (double)FLOAT_806164f8,
                     (double)FLOAT_806164fc, (double)FLOAT_80616500,
                     (double)FLOAT_806164f8, (double)FLOAT_80616504,
                     (int)vtx);
        FUN_803c8628((int)vtx, bit);

        recOff = recOff + 0x40;
    }

    FUN_803c88b0(0, 1, 0, 0, coverageMask, 0, 0);
    FUN_80382720();
}

// glow_rt_setup (0x8016c1c0)
// One-time initialisation of the 32x24 off-screen glow render target:
// clear colour, scissor/viewport, RT surface format, TEV/blend, ortho
// projection, identity model matrix + scale texture matrix, glow textures.
void glow_rt_setup(void)
{
    unsigned int colorA;
    unsigned int colorB;
    int rtId;
    float projMtx[16];
    float modelMtx[12];
    float texMtx[12];

    FUN_803cb468((double)FLOAT_806164f8, (double)FLOAT_806164f8,
                 (double)FLOAT_80616508, (double)FLOAT_8061650c,
                 (double)FLOAT_806164f8, (double)FLOAT_806164fc);
    FUN_803cb540(0, 0, 0x20, 0x18);
    FUN_803c78c8(0, 0, 0x20, 0x18);
    FUN_803c7978(0x20, 0x18, 4, 0);
    FUN_8016e8ac(0);
    FUN_803cabec(1);
    FUN_803cab98(0, 0, 0, 7);
    FUN_803a142c((double)FLOAT_806164fc, (double)FLOAT_80616510,
                 (double)FLOAT_806164fc, (double)FLOAT_80616510,
                 (double)FLOAT_80616514, (double)FLOAT_80616518, projMtx);
    FUN_803cb098(projMtx, 1);

    rtId = FUN_8016e020(0);
    DAT_806122b5 = (unsigned char)rtId;

    FUN_803ca688(1);
    FUN_803c6aac(0);
    FUN_803c8874(1);
    FUN_80181cf0(0, 0xff, 0xff, 4);
    FUN_803ca2f8(0, 0);
    FUN_80181e58(0, 0xf, 0xf, 0xf, 10);
    FUN_80181d50(0, 0, 0, 0, 1, 0);
    FUN_80181ec8(0, 7, 7, 7, 6);
    FUN_80181dd4(0, 0, 0, 0, 1, 0);
    FUN_8017fab4();

    FUN_8016e694(9,  DAT_8058a920, 0xc);
    FUN_8016e694(10, DAT_8058c680, 0xc);

    FUN_803a10e8(modelMtx);
    FUN_803a1248((double)FLOAT_8061651c, (double)FLOAT_8061651c,
                 (double)FLOAT_8061651c, texMtx);
    FUN_803cb1c8(modelMtx, 0);
    FUN_803cb248(texMtx, 0);
    FUN_803cb298(0);
    FUN_8016e444(3, 0);
    FUN_803c88b0(2, 0, 0, 0, 0, 2, 2);

    colorB = DAT_806164f0;
    FUN_803c86a4(0, &colorB);
    colorA = DAT_806164f0;
    FUN_803c86a4(2, &colorA);
}

// glow_rt_teardown_full (0x8016c3f0)
// Restore the saved scissor rect + active RT and fully reset draw/render state
// after the off-screen glow pass.
void glow_rt_teardown_full(void)
{
    FUN_803cb540(DAT_806109e8, DAT_806109ec,
                 (DAT_806109f0 - DAT_806109e8) + 1,
                 (DAT_806109f4 - DAT_806109ec) + 1);
    FUN_8016e020(DAT_806122b5);
    FUN_8016e8ac(4);
    FUN_8016e444(0, 0);
    DAT_80612314 = 1;
    DAT_806109d0 = 0xffffffff;
    DAT_80612310 = 1;
    DAT_80612250 = 1;
    DAT_806109d8 = 0xfffffffe;
    gx_material_commit_w();
    FUN_8016ef70();
    FUN_8016ef78();
    FUN_803c7080();
}

// glow_rt_teardown (0x8016c47c)
// Lighter render-state reset (no scissor/RT restore) used when the surrounding
// pass has already restored the viewport.
void glow_rt_teardown(void)
{
    FUN_8016e444(0, 0);
    DAT_80612314 = 1;
    DAT_806109d0 = 0xffffffff;
    DAT_80612310 = 1;
    DAT_80612250 = 1;
    DAT_806109d8 = 0xfffffffe;
    gx_material_commit_w();
    FUN_803c7080();
}

// subobj_glow_render_pass (0x8016c4cc)
// Per-frame entry (spawn-frame callback FUN_803826f0/FUN_8038273c). If the
// carried entity has >=1 sub-object (short at rec+0x74), lazily run glow_rt_setup
// once, save the active viewport, transpose the camera view + entity object
// matrices, draw up to 2 bright glow quads, then restore.
void subobj_glow_render_pass(void)
{
    int rec;
    unsigned int savedViewport;
    int quadCount;
    unsigned long long frame;
    float objMtxT[16];
    float viewMtxT[22];

    frame = FUN_803826f0();
    rec = (int)(frame >> 0x20);

    if (0 < *(short*)(rec + 0x74)) {
        if (DAT_806122bc == '\0') {
            glow_rt_setup();
            DAT_806122bc = '\x01';
        }
        savedViewport = FUN_8017b0d8();
        mtx4_transpose(*(float**)(DAT_80611d54 + 0x208), viewMtxT);
        mtx4_transpose((float*)(unsigned int)frame, objMtxT);

        quadCount = 2;
        if (*(short*)(rec + 0x74) < 3) {
            quadCount = (int)*(short*)(rec + 0x74);
        }
        glow_draw_quads(viewMtxT, objMtxT, *(int*)(rec + 0x70), quadCount);
        FUN_80174628();
        FUN_803c8224(savedViewport, 1);
    }

    FUN_8038273c();
}

// glow_render_stub (0x8016c590) - empty stub.
void glow_render_stub(void)
{
}
